/*
 * This file is part of OpenTTD.
 * OpenTTD is free software; you can redistribute it and/or modify it under the terms of the GNU General Public License as published by the Free Software Foundation, version 2.
 * OpenTTD is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 * See the GNU General Public License for more details. You should have received a copy of the GNU General Public License along with OpenTTD. If not, see <https://www.gnu.org/licenses/old-licenses/gpl-2.0>.
 */

/** @file yapf_rail.cpp The rail pathfinding. */

#include "../../stdafx.h"

#include "yapf.hpp"
#include "yapf_cache.h"
#include "yapf_node_rail.hpp"
#include "yapf_costrail.hpp"
#include "yapf_destrail.hpp"
#include "../../viewport_func.h"
#include "../../newgrf_station.h"
#include "../../tracerestrict.h"
#include "../../debug.h"
#include "../../misc/dbg_helpers.h"
#include "../../couple_group.h"
#include "../../pbs.h"
#include "../../rail_map.h" // R3R (KI-504 探针): GetRailReservationTrackBits, to log the reservation already on a tile
#include "../../date_func.h" // R3R (第 246 轮): _tick_counter, for the synchronized couple-scan throttle
#include <map> // R3R (KI-190): per-vehicle couple scan throttle state

#include "../../safeguards.h"

#if defined(UNIX) && defined(__GLIBC__)
#include <unistd.h>
#endif

/* R3R (第 346 轮续, KI-516): "挂接路径已经算出来了，但被第三方车占位否决"这一情形。
 *
 * 现场（build\R3R_debug.log, 2026-10-28）：
 *   CPL-PATHFOUND veh=57 found=1
 *   CPL-BACK      pPrev=5,15 td=0 parent=15,15 ptd=2
 *   FSCP          tile=13,15 fail=notSeg best=54 ord=6 sf=1
 *   CPL-SAFE-FAIL veh=57 tile=5,15
 *   CPL-PF-FALLBACK veh=57 ret=0x0 dest=5,14 final=12,15
 * 挂接路径本身是对的（(15,15) 沿 y=15 股道直达车底），只是回溯经过的 (13,15) 上停着
 * 过路车 54。安全位判据把它当硬否决 ⇒ 整条路径作废 ⇒ train_cmd.cpp 降级普通寻路 ⇒
 * 目标 (12,15) 被车底自己占着永远到不了 ⇒ 只订到 (5,14)，而那是 y=14 的另一条股道
 * ⇒ 玩家看到的"走错股道"。
 *
 * 玩家口径（2026-10-28 原话）：「它自己的路径被挡住了那就等待啊，等到路径被放出来啊」。
 * 故此处只记录"本次 INVALID_TRACK 属于被别的车挡"，由 train_cmd.cpp 据此**不降级**、
 * 原地等待下一 tick 重算。found=0（真的没路）不置位，降级行为逐字不变。
 */
static bool _r3r_couple_path_blocked = false;

bool R3RCouplePathBlockedByThirdParty()
{
	return _r3r_couple_path_blocked;
}

/* R3R (第 525 轮, KI-690): pbs.cpp 里的"无主预留位回收"。手写前向声明，不改 .h
 * （本树 ninja 不跟踪头文件依赖，改 .h 会触发全量重编）。 */
int R3RReapOrphanReservationBits(TileIndex tile);

/* R3R (第 342 轮, 方案 ε-A): 把"自锁交叉"这个失败原因交回调用方。
 *
 * 背景（§37/§38 定案）：挂接寻路在某一格失败，可能是**自己的车节占住了交叉位**
 * （现场 15,14：veh=59 占 TRACK_X，57 想订 TRACK_LOWER）。这种路线物理上不可达——
 * 道岔位已锁在 X，计划里的 LOWER 永远等不到。而 ChooseTrainTrack 的
 * "cpl-partial-res" 出口会保留半截预留让车继续开，车于是沿被锁的 X 位直行，
 * 与计划方向矛盾（玩家实测的"走错岔道"）。
 *
 * yapf 对外只返回 PBSTileInfo，没有失败原因位，又不能为了一个标志去改 .h
 * （会触发全量重编），故沿用本文件既有的"手写前向声明"做法：这里定义，
 * src\train_cmd.cpp 侧手写 extern 声明并读取。语义是"最近一次预留尝试里，
 * 是否有格因自锁而订不上"，取走即清，且每次 TryReservePath 入口清零，
 * 不可能跨调用陈旧。 */
static TileIndex _r3r_selflock_tile = INVALID_TILE;
static uint32_t _r3r_selflock_veh = UINT32_MAX;

void R3RNoteSelfLock(TileIndex tile, VehicleID veh)
{
	_r3r_selflock_tile = tile;
	_r3r_selflock_veh = veh.base();
}

bool R3RTakeSelfLock(VehicleID veh, TileIndex *tile)
{
	if (_r3r_selflock_veh != veh.base()) return false;
	const TileIndex t = _r3r_selflock_tile;
	_r3r_selflock_veh = UINT32_MAX;
	_r3r_selflock_tile = INVALID_TILE;
	if (tile != nullptr) *tile = t;
	return true;
}

void R3RClearSelfLock()
{
	_r3r_selflock_veh = UINT32_MAX;
	_r3r_selflock_tile = INVALID_TILE;
}

/* R3R (答案本 / 2026-11-09): 上一次 A* 算出来的路径，下一次直接照抄铺预留。
 *
 * 为什么存这里而不存轨道位（见 R3R_perf_yapf_cache_plan_memo.md §17.1）：
 *   - 轨道位 `_m[t].m2` bit 8-11 上能存的只有"预留"= 排他占位，一旦写上就挡别人、
 *     随存档走、还必须由订它的车负责撤销（KI-203/345/346 全是撤销出事的伤疤）。
 *     答案若存那里，答案长度就等于预留长度，"答案长、预留短"直接失效。
 *   - 引擎侧存的是"记忆"：不占位、不挡人、失效就丢、零副作用。
 *
 * 为什么用文件内 static 表 + 非 static 接口（不碰 .h）：
 *   沿用本文件既有的跨文件通信做法（见 :59-71 R3RNoteSelfLock 那段注释）。
 *   数据本体 static，接口函数非 static，train_cmd.cpp 侧手写 extern 声明。
 *
 * 本表是纯运行时态：读档清空、卖车/解挂由 R3RAnswerDrop() 清。
 * 失效判据不外挂——抄答案时本来就要逐格 TryReserveRailTrack，订不上即失效。 */
struct R3RAnswerStep {
	TileIndex tile;
	Trackdir td;
};

static std::map<uint32_t, std::vector<R3RAnswerStep>> _r3r_answer_steps;
static std::map<uint32_t, PBSTileInfo> _r3r_answer_origin;
/* 记录该答案是为"哪个订单目的地"算的（v->dest_tile，订单目的地，不是 A* 的 res_dest_tile）。
 * 换单是唯一一个"车的物理状态一点没变、但答案指向完全错的地方"的失效场景：
 * origin 定位照样能命中（车确实还停在旧路径上），逐格预留也照样能订上
 * （轨道没变），于是会一路开到旧目的地。只有比对目的地才能抓到。 */
static std::map<uint32_t, TileIndex> _r3r_answer_dest;
static std::map<uint32_t, uint32_t> _r3r_answer_store_count;
static std::map<uint32_t, uint32_t> _r3r_answer_hit_count;

/** 丢弃某车的答案（卖车/解挂/掉头等链结构变化处显式调用；换单由回放里的目的地比对兜住）。reason 只用于探针。 */
void R3RAnswerDrop(VehicleID veh, const char *reason)
{
	const uint32_t key = veh.base();
	const bool had_answer = _r3r_answer_steps.erase(key) != 0;
	_r3r_answer_origin.erase(key);
	_r3r_answer_dest.erase(key);
	_r3r_answer_store_count.erase(key);
	_r3r_answer_hit_count.erase(key);
	if (!had_answer) return;
}

/** 本次 TryReservePath 逐格走过的 (tile, td)，成功后由 R3RAnswerCommit() 收进表。 */
static std::vector<R3RAnswerStep> _r3r_answer_build;

/** 把本次刚铺过的路径收进答案本。只在主寻路（非 unstafe_pos）路径上调用。 */
static void R3RAnswerCommit(VehicleID veh, TileIndex order_dest)
{
	const uint32_t key = veh.base();
	if (_r3r_answer_build.size() < 2) {
		_r3r_answer_build.clear();
		return;
	}
	std::vector<R3RAnswerStep> &dst = _r3r_answer_steps[key];
	dst.swap(_r3r_answer_build);
	_r3r_answer_origin[key] = PBSTileInfo(dst.front().tile, dst.front().td, true);
	_r3r_answer_dest[key] = order_dest;
	++_r3r_answer_store_count[key];
}

template <typename Tpf> void DumpState(Tpf &pf1, Tpf &pf2)
{
	DumpTarget dmp1, dmp2;
	pf1.DumpBase(dmp1);
	pf2.DumpBase(dmp2);

#if defined(UNIX) && defined(__GLIBC__)
	static unsigned int num = 0;
	int pid = getpid();
	std::string fn1;
	std::string fn2;
	std::optional<FileHandle> f1;
	std::optional<FileHandle> f2;
	for(;;) {
		fn1 = fmt::format("yapf-{}-{}-1.txt", pid, num);
		f1 = FileHandle::Open(fn1, "wx");
		if (!f1.has_value() && errno == EEXIST) {
			num++;
			continue;
		}
		fn2 = fmt::format("yapf-{}-{}-2.txt", pid, num);
		f2 = FileHandle::Open(fn2, "w");
		num++;
		break;
	}
	Debug(desync, 0, "Dumping YAPF state to {} and {}", fn1, fn2);
#else
	auto f1 = FileHandle::Open("yapf1.txt", "wt");
	auto f2 = FileHandle::Open("yapf2.txt", "wt");
#endif
	assert(f1.has_value());
	assert(f2.has_value());
	fwrite(dmp1.output_buffer.data(), 1, dmp1.output_buffer.size(), *f1);
	fwrite(dmp2.output_buffer.data(), 1, dmp2.output_buffer.size(), *f2);
}

template <class Node>
struct CYapfReserveTrackSafePositionNodes {
	static inline std::vector<Node *> safe_position_nodes;
};

template <class Types>
class CYapfReserveTrack {
public:
	typedef typename Types::Tpf Tpf; ///< the pathfinder class (derived from THIS class)
	typedef typename Types::TrackFollower TrackFollower;
	typedef typename Types::NodeList::Item Node; ///< this will be our node type

protected:
	/** @copydoc CYapfBaseT::Yapf */
	inline Tpf &Yapf()
	{
		return *static_cast<Tpf *>(this);
	}

private:
	TileIndex res_dest_tile;   ///< The reservation target tile
	Trackdir res_dest_td;      ///< The reservation target trackdir
	Node *res_dest_node;       ///< The reservation target node
	TileIndex res_fail_tile;   ///< The tile where the reservation failed
	Trackdir res_fail_td;      ///< The trackdir where the reservation failed
	TileIndex origin_tile;     ///< Tile our reservation will originate from

	bool FindSafePositionProc(TileIndex tile, Trackdir td)
	{
		if (IsSafeWaitingPosition(Yapf().GetVehicle(), tile, td, true, !TrackFollower::Allow90degTurns())) {
			this->res_dest_tile = tile;
			this->res_dest_td = td;
			return false;   // Stop iterating segment
		}
		return true;
	}


	/**
	 * R3R: couple-specific safe-position detector (ported from the pxp-decouple
	 * patch). Tracks the waiting consist's own reservation so the coupling
	 * locomotive drives right up to it.
	 */
	bool FindSafeCouplePositionProc(TileIndex tile, Trackdir td)
	{
		if (IsRailDepotTile(tile)) {
		return false;
	}
		TrackdirBits tdb = TrackdirToTrackdirBits(td);
		TrackBits tracks = TrackdirBitsToTrackBits(tdb);
		if (HasReservedTracks(tile, tracks)) {
			Train *best = nullptr;
			Train *second_best = nullptr;
			/* R3R (REG-359-I): 整个格的链头集合，**故意不套 :199 的轨道筛选**。
			 * 同格可能停着两列车的链头（解挂后机车半与车底半被摆在同一格的不同轨上），
			 * 而 check_train_on_tile() 的轨道位筛选会让其中一台完全不可见 —— 于是 best
			 * 只剩一台，判据看起来是"这格只有一辆车"，实际是"看不见的那辆决定了结论"
			 * （现场：best=57 而 index 更小的 veh0 在同一格却未计入）。
			 * 这类格本帧无法判定 ⇒ 暂存（视为可通行、但**不当作终点**），下一 tick 寻路
			 * 重入时再扫；等那台车开走、格上只剩一个链头，判据自然恢复。 */
			static const uint MAX_TILE_HEADS = 4;
			Train *tile_heads[MAX_TILE_HEADS] = {};
			uint n_tile_heads = 0;
			bool tile_heads_have_target = false;
			auto note_tile_head = [&](TileIndex t) {
				for (Train *tr : VehiclesOnTile<VehicleType::Train>(t)) {
					if (tr->vehstatus.Test(VehState::Crashed)) continue;
					Train *head = tr->First();
					bool seen = false;
					for (uint i = 0; i < n_tile_heads; i++) {
						if (tile_heads[i]->index == head->index) {
							seen = true;
							break;
						}
					}
					if (seen) continue;
					if (n_tile_heads < MAX_TILE_HEADS) tile_heads[n_tile_heads++] = head;
					/* 只对"格上确实存在合法挂接目标"的多链头格暂存：整格全是无关车
					 * （都进不了配对）时仍按障碍处理，不放松对普通占位列的判别。 */
					if (R3RIsCoupleTarget(head) && R3RCoupleAllowed(Train::From(Yapf().GetVehicle()), head)) {
						tile_heads_have_target = true;
					}
				}
			};
			auto check_train_on_tile = [&](TileIndex t) {
				for (Train *tr : VehiclesOnTile<VehicleType::Train>(t)) {
					if (tr->vehstatus.Test(VehState::Crashed)) continue;
					if (tr->track == TRACK_BIT_WORMHOLE || HasBit((TrackBits)tr->track, TrackdirToTrack(td))) {
						Train *head = tr->First();
						if (best != nullptr && head->index != best->index) second_best = head;
						if (best == nullptr || head->index < best->index) best = head;
					}
				}
			};
			if (IsRailStationTile(tile)) {
				TileIndexDiff diff = TileOffsByDiagDir(TrackdirToExitdir(ReverseTrackdir(td)));
				for (TileIndex st_tile = tile + diff; IsCompatibleTrainStationTile(st_tile, tile); st_tile += diff) {
					check_train_on_tile(st_tile);
					note_tile_head(st_tile);
				}
			}
			check_train_on_tile(tile);
			note_tile_head(tile);
			/* KI-562（玩家拍板 C-562-D，2026-11-06）：**数到发线上的链头数，只有
			 * 一个链头时才放行，多链头一律算卡住**。
			 *
			 * 旧判据（REG-359-I）是 `n_tile_heads >= 2 && tile_heads_have_target`
			 * ⇒ return true：多链头格"暂存"（视为可通行、但不当终点），指望下一
			 * tick 重入时另一台车已开走。KI-562 的现场恰恰相反 —— 解挂留下的前链
			 * 未脱离、永远停在同一格（它自己也卡着），暂存于是变成**永久放行**：
			 * 寻路把该格当可通行、把后链当终点，命中最小的那条链或直接拼上去，
			 * 两列车体坐标重合。
			 *
			 * 按玩家口径收口：判不了就不放行。多链头 ⇒ 本格不是安全位置 ⇒ 挂接
			 * 寻路摸不到终点 ⇒ 机车原地不动（玩家说的"算卡住"），等前链脱离、
			 * 该到发线只剩一个链头时判据自然恢复。 */
			if (n_tile_heads >= 2) {
				/* 按"格 + 最小两个车号"变化各打一行，避免每 tick 刷屏。 */
				static uint32_t last_key = 0xFFFFFFFF;
				uint32_t key = (tile.base() << 8) ^ (tile_heads[0]->index.base() << 4) ^ tile_heads[1]->index.base();
				if (key != last_key) {
					last_key = key;
				}
				return false;
			}
			if (best != nullptr) {
				/* R3R: the tile may be occupied by the pathfinding locomotive's
				 * own consist (it is still standing on the first segment of its
				 * path, e.g. straddling a depot door or the rail in front of it).
				 * That train drives away, so its occupied tile is not an obstacle
				 * for the couple path - treat it as passable. Without this the
				 * back-walk's CheckSafePositionOnNode fails on the loco's own
				 * first tile (FSCP ... fail=notWC best=<self>) and the locomotive
				 * is locked forever: no track, speed 0, COUPLE-FAIL every tick. */
				if (best->index == Yapf().GetVehicle()->index) return true;
				/* R3R (KI-62): same gate as the pathfinder's destination test -- only a
				 * real segment waiting to be coupled onto counts as a couple target, so
				 * its occupied tile is passable for the couple path. A loose wagon chain
				 * ("散链") is an obstacle like any other train. */
				if (!R3RIsCoupleTarget(best)) {
					return false;
				}
				/* R3R: couple group whitelist (step 5). A waiting consist in a
				 * different couple group is not a couple target at all, so its
				 * occupied tile counts as an obstacle for the couple path exactly
				 * like any other train. No probe here: this runs for every tile of
				 * every back-walk and a rejection is a normal outcome -- the
				 * caller already reports CPL-SAFE-FAIL. */
				if (!R3RCoupleAllowed(Train::From(Yapf().GetVehicle()), best)) return false;
				if (second_best != nullptr) {
					return false;
				}
				Vehicle *other_train = nullptr;
				FollowTrainReservation(best, &other_train);
				/* R3R (S2 后续, 走错岔道真因): 若这里的"第三人"就是询路车**自己**，
				 * 说明两边的预留已经对接（本车的挂接预留接到了车底平台上），
				 * 这不是第三方冲突，不该否决整条挂接路径。
				 *
				 * 现场（build\R3R_debug.log:1769-1776，S2 版 exe）：
				 *   CPL-PATHFOUND veh=57 found=1          ← 路径找到了
				 *   FSCP tile=5,15 fail=other best=0 other=57   ← 占位者是自己
				 *   CPL-SAFE-FAIL veh=57 tile=5,15        ← 整条路径作废
				 *   CPL-RET ... ret=0x1 why=cpl-moving-keep   ← 退回普通寻路方向(X)
				 *   LOCO-MOVE veh=57 tile=15,14 track=01  ← 于是沿 X 直行
				 * 而上一 tick 的挂接预留明明成功订到了 LOWER
				 *   （CPL-RESERVE reserved=1 next=3 path=4 booked=4, res=0x8）。
				 * 结果就是"预留成功却走错道岔"。豁免本车与 :222 的既有豁免同源：
				 * 自己手里的预留不是障碍。 */
				if (other_train != nullptr && other_train != best && other_train->index != Yapf().GetVehicle()->index) {
					return false;
				}
			}
		} else if (GetReservedTrackbits(tile) != TRACK_BIT_NONE) {
			/* R3R (第 340 轮, 定案 R338-A + 方案 δ): 该格已有预留位，但占位者可能是
			 * 询路车**自己**——自己编组的某一节就站在该格上（现场：15,14 上
			 * veh=59 占 TRACK_X，而 57 想订交叉位 TRACK_LOWER）。TryReserveRailTrack
			 * 的交叉互斥只看位、不看 owner，于是"自己挡自己"被误判成阻塞，
			 * FindSafeCouplePositionProc 在这个格硬失败，挂接寻路永远出不来
			 * （KI-510/KI-358 的真因，见备忘 §37）。这里只做一件事：若占位者是
			 * 本链车头，则本车已持有该格，视为不阻塞，让 back-walk 继续。
			 * **不放宽**任何位级互斥——TryReserveRailTrack 本身一个字没改。 */
			Train *const coupler = const_cast<Train *>(Train::From(Yapf().GetVehicle()));
			bool self_held = false;
			{
				const uint8_t reserved = (uint8_t)(GetReservedTrackbits(tile) & TRACK_BIT_MASK);
				for (Track tk = TRACK_BEGIN; tk != TRACK_END; tk++) {
					if (!HasBit(reserved, tk)) continue;
					const Train *const holder = GetTrainForReservation(tile, tk);
					if (holder == nullptr || holder->First() != coupler->First()) continue;
					/* 二次确认：GetTrainForReservation 在站台会整板扫描，可能指到
					 * 本链车头但实际占位属于别人；该格上必须真有本链车节才豁免。 */
					bool on_tile = false;
					for (Train *t : VehiclesOnTile<VehicleType::Train>(tile)) {
						if (t->First() == coupler->First()) { on_tile = true; break; }
					}
					if (!on_tile) continue;
					self_held = true;
					break;
				}
			}
			/* R3R (第 341 轮, 方案 δ 已被实测证伪, 见备忘 §38): "本车已持有该格"
			 * **不等于** "本车能通过该格"。该格的位锁在 X 时，道岔物理上只能沿 X
			 * 走，而 yapf 计划写的是交叉位 LOWER —— δ 放行后列车就在该格沿 X 直行，
			 * 与计划方向不符，表现为"走错岔道"。所以 self_held 只做诊断（FSCP 行的
			 * self=%d / FSCP-SELFHELD），**不参与判定**，该格照旧以
			 * TryReserveRailTrack 的结果为准：拒绝即拒绝，这正是道岔位的真实状态。
			 * 真正的修法在上层（计划层判该机动不可达 / 先退车释放 X 位），见备忘 §38.4。 */
			if (!TryReserveRailTrack(tile, TrackdirToTrack(td))) {
				/* R3R (第 342 轮, 方案 ε-A): 只有"失败 + 占位者是本链"才是自锁。
				 * 若 self_held 但 TryReserveRailTrack 成功（两位不交叉），不算自锁。 */
				if (self_held) R3RNoteSelfLock(tile, coupler->index);
				return false;
			}
			/* R3R (第 243 轮, REG-243-O): name this release (the couple pathfinder's
			 * own rollback of a half-reserved safe tile) so the UNRES-RAIL line says
			 * "yapf-fscp" instead of inheriting the caller's phase. Log only, no
			 * behaviour. */
			R3RResvPhaseGuard r3r_phase_fscp("yapf-fscp", R3RResvActorID(Yapf().GetVehicle()));
			UnreserveRailTrack(tile, TrackdirToTrack(td));
		}
		return true;
	}	/**
	 * Reserve a railway platform. Tile contains the failed tile on abort.
	 * @param tile The start tile.
	 * @param dir The direction to reserve further tiles in.
	 * @return \c true iff reservation succeeded.
	 */
	bool ReserveRailStationPlatform(TileIndex &tile, DiagDirection dir, Trackdir td)
	{
		TileIndex     start = tile;
		TileIndexDiff diff = TileOffsByDiagDir(dir);

		do {
			/* R3R (第 402 轮, KI-571): 原来这里是硬判 `HasStationReservation(tile)`
			 * ⇒ 只要整格位被任何人占着就整段失败。而 `TryReserveRailTrack()` 的
			 * Station 分支早已有 RESV-PAIR-AIR / RESV-SELF-AIR 语义（第 345/368 轮）：
			 * 归**配对伙伴**或**自己**的整格位是"空气"——不写、不失败。
			 *
			 * 站台段正是挂接的终点段，目标车底自己占着平台末端完全正常（物理事实）：
			 * 现场 booked=2/3、`tile=248,133 owner=5 selfheld=0`，而 veh=5 就是配对
			 * 目标链的一员 —— 硬判让整段平台一格都订不上，机车再也进不了站台。
			 * 改走同一个入口后：空格照旧订上；伙伴/自己的位当空气继续推进
			 * （于是 130/131/132 这些**没被车底压住**的格能被订到，机车才能开进去
			 * 贴住车底）；别人的位仍然失败，不越权。
			 * trigger_stations=false：随机化仍在末尾统一触发一次，与改动前一致。 */
			/* R3R (第 432 轮, KI-626): 上面那笔改动把"整格位预留"换成了逐格
			 * TryReserveRailTrack()，于是这个循环**第一次真的逐格检查"这格有没有
			 * 这条轨"**——而 `td` 只是**进入站台那一格**的 trackdir，整段平台每格
			 * 都在复用它。IsCompatibleTrainStationTile() 只保证同站/同轴/轨型兼容，
			 * 并不保证每格都真有入口那条轨（站台两端接不同 track 的弯道时就会不同），
			 * 走到那种格必然触发 pbs.cpp:545 的断言（现场 tile 107,127）。
			 *
			 * 关键：TryReserveRailTrack() 的 Station 分支写的是**整格位**
			 * (SetRailStationReservation)，`track` 参数在那一支一次都没被读——
			 * 只被 545 行的断言检查。所以这里改用**该格真实存在的轨道**喂进去，
			 * 对预留语义零影响，只是让断言拿到一个必然成立的值。 */
			const TrackBits plat_tracks = TrackdirBitsToTrackBits(GetTileTrackdirBits(tile, ::TransportType::Rail, 0));
			if (plat_tracks == TRACK_BIT_NONE) return false;
			const Track plat_track = FindFirstTrack(plat_tracks);
			if (!TryReserveRailTrack(tile, plat_track, false)) return false;
			MarkTileDirtyByTile(tile, VMDF_NOT_MAP_MODE);
			tile = TileAdd(tile, diff);
		} while (IsCompatibleTrainStationTile(tile, start) && tile != this->origin_tile);

		auto *st = Station::GetByTile(start);
		TriggerStationRandomisation(st, start, StationRandomTrigger::PathReservation);
		TriggerStationAnimation(st, start, StationAnimationTrigger::PathReservation);

		return true;
	}

	/**
	 * Reserve a single track/platform.
	 * @param tile The start tile.
	 * @param td The track direction that is to be reserved.
	 * @return \c true iff reservation succeeded.
	 * @param reserver Explicit vehicle to reserve for. Passed only by the answer-book
	 *                 replay, which runs *before* FindPath(); at that point
	 *                 Yapf().GetVehicle() is still nullptr (yapf_base.hpp only assigns
	 *                 `vehicle` inside FindPath()), so it must never be used there.
	 */
	bool ReserveSingleTrack(TileIndex tile, Trackdir td, const Train *reserver = nullptr)
	{
		const Train *const veh = (reserver != nullptr) ? reserver : Yapf().GetVehicle();
		if (IsRailStationTile(tile)) {
			if (!ReserveRailStationPlatform(tile, TrackdirToExitdir(ReverseTrackdir(td)), td)) {
				/* Platform could not be reserved, undo. */
				this->res_fail_tile = tile;
				this->res_fail_td = td;
			}
		} else {
			bool r3r_track_reserved = TryReserveRailTrackdir(veh, tile, td);
			if (!r3r_track_reserved) {
				/* R3R (第 525 轮, KI-690): 订不上时先看是不是"无主位"在挡路 —— 无主位不是
				 * 合法障碍（没有任何车会来释放它，见 pbs.cpp 的 R3RReapOrphanReservationBits），
				 * 清掉冲突的那根 Track 再重试一次。只有真订不上才付这个代价。 */
				if (R3RReapOrphanReservationBits(tile) > 0) {
					r3r_track_reserved = TryReserveRailTrackdir(veh, tile, td);
				}
			}
			if (!r3r_track_reserved) {
				/* Tile couldn't be reserved, undo. */
				this->res_fail_tile = tile;
				this->res_fail_td = td;
				return false;
			}

			if (IsRailWaypointTile(tile)) {
				auto *st = BaseStation::GetByTile(tile);
				TriggerStationRandomisation(st, tile, StationRandomTrigger::PathReservation);
				TriggerStationAnimation(st, tile, StationAnimationTrigger::PathReservation);
			}
		}

		return tile != this->res_dest_tile || td != this->res_dest_td;
	}

	/**
	 * Unreserve a single track/platform. Stops when the previous failure is reached.
	 * @param tile The start tile.
	 * @param td The track direction that is to be unreserved.
	 * @return \c true iff the unreservation succeeded.
	 */
	bool UnreserveSingleTrack(TileIndex tile, Trackdir td)
	{
		if (IsRailStationTile(tile)) {
			TileIndex     start = tile;
			TileIndexDiff diff = TileOffsByDiagDir(TrackdirToExitdir(ReverseTrackdir(td)));
			while ((tile != this->res_fail_tile || td != this->res_fail_td) && IsCompatibleTrainStationTile(tile, start)) {
				SetRailStationReservation(tile, false);
				tile = TileAdd(tile, diff);
			}
		} else if (tile != this->res_fail_tile || td != this->res_fail_td) {
			UnreserveRailTrackdir(tile, td);
		}
		return (tile != this->res_dest_tile || td != this->res_dest_td) && (tile != this->res_fail_tile || td != this->res_fail_td);
	}

public:
	/**
	 * Set the target to where the reservation should be extended.
	 * @param node The destination node.
	 * @param tile The destination tile.
	 * @param td The destination track direction.
	 */
	/**
	 * R3R (修法第三档 S2): 按"物理格序"逐格预订 —— 两遍法的第二遍。
	 *
	 * 与 TryReservePath 的区别：后者按 YAPF 节点链订，而节点 segment 是跨站台
	 * 合并的，按它订会把车底整段一起订进去；本函数只订摸路(rp)实际走过的那几格，
	 * 止于会合点。
	 *
	 * KI-563（2026-11-06）：预留粒度按原生 PBS —— 遇到第一个订不上的格（信号 /
	 * 他人预留）就停，已订上的格**保留**，下一个 tick 由 TryReservePath 重新摸路
	 * 续订。只有一格都订不上才返回失败。
	 *
	 * KI-563 修法 D（2026-11-07，第 395 轮）：截断判据由「第一个信号格」改为
	 * 「第一个安全停车位」（IsSafeWaitingPosition），与原生 PBS / 驾驶预留
	 * （ExtendTrainReservation）用同一把尺子。
	 *
	 * @param steps 摸路产出的 (格, 轨) 序列。
	 * @param booked 出参：已成功预订的格数。
	 * @return booked > 0 为 true（可能是部分预留）。
	 */
	bool R3RReservePathSequence(const std::vector<std::pair<TileIndex, Trackdir>> &steps, size_t &booked)
	{
		booked = 0;
		this->res_fail_tile = INVALID_TILE;
		/* R3R (REG-359-L 探针, 只读): 以前失败只把格写进 res_fail_tile，不打日志，
		 * 于是 CPL-RESERVE 只报 booked=0，"哪一格 / 第几个 / 被谁挡"全靠猜。
		 * 判读:
		 *   nth/total      —— 第几格失败(0 基) / 共几格待订(等于 path)
		 *   booked-before  —— 失败前已订上的格数；整批回滚后 CPL-RESERVE 必报 booked=0
		 *   owner/otile/ospd/ord —— 该格现有预留的归属者(Arbitrated 版，KI-350 口径)
		 *   selfheld       —— 归属者与询路车同链(自锁)；0 表示第三方
		 *   type/plain/st/depot —— 格型，区分"站台整格位冲突"与"普通轨位冲突"
		 * 本探针不改控制流: 失败仍然整批回滚。 */
		bool stopped_at_goal = false;
		Train *const r3r_coupler = const_cast<Train *>(Train::From(Yapf().GetVehicle()));
		/* R3R (第 511 轮, KI-684 A 方案): 挂接预留的**站台硬边界**。
		 *
		 * 玩家口径（2026-11-26 原话）：「KI684 的核心是 veh15 的挂接寻路预留超出
		 * 站台，也就是说，预留从站台一边进入，又从另一边出来，但 veh15 的命令是
		 * 去那个站台挂接。」⇒ 缺陷维度是**范围**（预留终止边界缺失），不是槽位、
		 * 不是双写、不是撞车。
		 *
		 * 判据只有一条：**配对伙伴所在的那一段站台（连续同站、同轴、同轨，直到
		 * 遇到不可兼容格为止）就是预留范围的右界**。摸路一旦踩进这段站台，之后
		 * 就不再往站台外铺；站台被 TrackFollower 整段跳格（tiles_skipped 让
		 * r3r_resv_path 里根本没有站台格）时，用同排坐标的"整段跨过"判据兜住。
		 *
		 * 停止优先于延伸：最坏结果是少订一格（机车多等一 tick 重算再续订），绝不
		 * 多订到站台外 —— 后者正是玩家看到的"预留从站台一边穿到另一边"。
		 *
		 * 作用域刻意收窄：只对"配对伙伴停在站台上"的挂接生效。伙伴在库里/野地/
		 * 路点上时 r3r_plat_ref 保持 INVALID_TILE，本段逻辑整块不参与，行为逐字
		 * 不变（普通行驶列车根本不进这个函数）。 */
		TileIndex r3r_plat_ref = INVALID_TILE;
		int r3r_pair_idx = -1;
		{
			const Train *r3r_partner = R3RGetCouplePairPartner(r3r_coupler);
			if (r3r_partner != nullptr) r3r_pair_idx = (int)r3r_partner->index.base();
			for (const Train *t = r3r_partner; t != nullptr; t = t->Next()) {
				if (IsRailStationTile(t->tile) && !IsRailWaypointTile(t->tile)) {
					r3r_plat_ref = t->tile;
					break;
				}
			}
			/* R3R (第 515 轮, KI-684): 配对锁不是可靠锚点。
			 * 现场读数 `CPL-PAIRSCAN tick=41925 act=15 ... best=118 bestDist=23
			 * paired=-1` —— 寻路那一刻 veh=15 的锁还没建立（锁是 CPL-PAIRLOCK 在
			 * 别的 tick 扫出来的，且属于 75/18 那一对），于是
			 * R3RGetCouplePairPartner() 返回 nullptr、整块站台判据被静默跳过，
			 * 第 514 轮才会"EXT-STOP 一次都不出现"。
			 *
			 * 补两个不依赖锁的锚点（顺序即优先级）：
			 *   ① 摸路终点 steps.back()：它是 SetReservationTarget() 收到的会合点，
			 *      必然紧邻配对车底；
			 *   ② 会合点同排 ±8 格内第一个真站台格（排除路点）。
			 * 都找不到才放弃 —— 此时行为退回第 511 轮之前，逐字不变。 */
			if (r3r_plat_ref == INVALID_TILE && !steps.empty()) {
				const TileIndex r3r_goal = steps.back().first;
				if (IsRailStationTile(r3r_goal) && !IsRailWaypointTile(r3r_goal)) {
					r3r_plat_ref = r3r_goal;
				} else {
					for (int d = 1; d <= 8 && r3r_plat_ref == INVALID_TILE; d++) {
						const int dx4[4] = { d, -d, 0, 0 };
						const int dy4[4] = { 0, 0, d, -d };
						for (int k = 0; k < 4; k++) {
							const TileIndex nt = TileAddWrap(r3r_goal, dx4[k], dy4[k]);
							if (nt == INVALID_TILE) continue;
							if (IsRailStationTile(nt) && !IsRailWaypointTile(nt)) {
								r3r_plat_ref = nt;
								break;
							}
						}
					}
				}
			}
			/* 判据完全没有锚点时也必须留痕：否则"日志里没有"无法与"没走到这里"
			 * 区分（第 514 轮就栽在这上面）。按车号节流，每车只报一次。 */
			if (r3r_plat_ref == INVALID_TILE) {
				static uint32_t r3r_noref_seen[64] = { 0 };
				const uint32_t r3r_vk = (uint32_t)r3r_coupler->index.base() + 1;
				bool r3r_seen = false;
				for (int k = 0; k < 64; k++) {
					if (r3r_noref_seen[k] == r3r_vk) { r3r_seen = true; break; }
				}
				if (!r3r_seen) {
					for (int k = 0; k < 63; k++) r3r_noref_seen[k] = r3r_noref_seen[k + 1];
					r3r_noref_seen[63] = r3r_vk;
				}
			}
		}
		int r3r_plat_lo = 0;      ///< 站台沿轴向的低端坐标（含）
		int r3r_plat_hi = 0;      ///< 站台沿轴向的高端坐标（含）
		int r3r_plat_perp = 0;    ///< 站台所在那一排的垂直坐标（Axis::X ⇒ TileY）
		bool r3r_plat_row_x = true; ///< 站台轴向是 X（沿 x 延伸）还是 Y
		if (r3r_plat_ref != INVALID_TILE) {
			r3r_plat_row_x = (GetRailStationAxis(r3r_plat_ref) == Axis::X);
			r3r_plat_perp = r3r_plat_row_x ? (int)TileY(r3r_plat_ref) : (int)TileX(r3r_plat_ref);
			const int r3r_plat_dx = r3r_plat_row_x ? 1 : 0;
			const int r3r_plat_dy = r3r_plat_row_x ? 0 : 1;
			TileIndex r3r_plat_end = r3r_plat_ref;
			for (int k = 0; k < 64; k++) {
				const TileIndex nt = TileAddWrap(r3r_plat_end, r3r_plat_dx, r3r_plat_dy);
				if (nt == INVALID_TILE || !IsCompatibleTrainStationTile(nt, r3r_plat_ref)) break;
				r3r_plat_end = nt;
			}
			r3r_plat_hi = r3r_plat_row_x ? (int)TileX(r3r_plat_end) : (int)TileY(r3r_plat_end);
			r3r_plat_end = r3r_plat_ref;
			for (int k = 0; k < 64; k++) {
				const TileIndex nt = TileAddWrap(r3r_plat_end, -r3r_plat_dx, -r3r_plat_dy);
				if (nt == INVALID_TILE || !IsCompatibleTrainStationTile(nt, r3r_plat_ref)) break;
				r3r_plat_end = nt;
			}
			r3r_plat_lo = r3r_plat_row_x ? (int)TileX(r3r_plat_end) : (int)TileY(r3r_plat_end);
		}
		bool r3r_plat_entered = false; ///< 本轮摸路已经踩进伙伴所在那段站台
		bool r3r_plat_seen_lo = false; ///< 同排、轴向坐标低于站台起点（从低侧接近）
		bool r3r_plat_seen_hi = false; ///< 同排、轴向坐标高于站台终点（从高侧接近）
		/* R3R (第 504 轮收口, REG-504-E, 玩家 2026-11-24 拍板 C-6/C-7):
		 * 「安全位」在本函数里只保留一个含义 —— **本车要去挂的目的地**。
		 *
		 * 这一刀同时否掉两块东西：
		 *  ① 终点例外（REG-399-C）：例外之所以存在，是因为原判据
		 *     (IsSafeWaitingPosition) 看不见「这格是我的目的地」这个维度 ——
		 *     它对会合点和一个无关的空道岔给出**完全相同**的答案。改用
		 *     「目的地即安全位」之后，会合点天然是安全位，"找不到最后一个
		 *     安全位"这种状态不会发生，例外连同它的 steps.size() 兜底初值
		 *     一起失去存在理由（例外加例外 = 意外）。
		 *  ② 单轨格限制（REG-504-A / M1）：M1 靠"必须单轨"来排除被误判成
		 *     线路尽头的道岔（现场 1457,1874）。玩家的判据更准 —— 道岔能否
		 *     当安全位的条件是"上面有没有我的等待列车"，不是"单轨/多轨"。
		 *     只订到目的地之后，中途格根本不参与安全位判定，M1 的尺子即不需要。
		 *
		 * 于是正向扫退化为一条规则：**逐格订，订到目的地为止，中途一律不截断**。
		 * 中段信号/道岔/站台都不再是截断点 —— 这正是挂接需要的语义：机车要
		 * 一路开到会合点合并，中途停下就自锁（REG-399-C 的原始推导）。
		 *
		 * 第三方占用仍由 ReserveSingleTrack 的失败分支兜住（KI-563 A：保留
		 * 已订格并原地返回），不会因为删掉安全位判据而多占别人的轨道。 */
		for (size_t i = 0; i < steps.size(); ++i) {
			const TileIndex stile = steps[i].first;
			const Trackdir stdir = steps[i].second;
			/* R3R (第 511 轮, KI-684 A): 站台硬边界，判据见函数开头。 */
			if (r3r_plat_ref != INVALID_TILE) {
				const int r3r_axial = r3r_plat_row_x ? (int)TileX(stile) : (int)TileY(stile);
				const int r3r_perp = r3r_plat_row_x ? (int)TileY(stile) : (int)TileX(stile);
				const bool r3r_same_row = (r3r_perp == r3r_plat_perp);
				/* 站台段内的格理论上不会进 steps（r3r_resv_path 明确排除站台格），
				 * 只有"整段跨过"分支会把会合点自身补进来。保留 on_piece 是为了让
				 * entered 仍有确切定义：踩上站台段的那一格。 */
				const bool r3r_on_piece = r3r_same_row && r3r_axial >= r3r_plat_lo && r3r_axial <= r3r_plat_hi;
				/* below/above 只认站台两端±8 格内的"贴边格"：同排但隔着几百格的
				 * 另一段线路不是接近侧，不能参与"整段跨过"判据（否则同 y 坐标的
				 * 远处线路会让跳格判据误触发）。跳格落点恰好是 hi+1 / lo-1。 */
				const bool r3r_below = r3r_same_row && r3r_axial < r3r_plat_lo && r3r_axial >= r3r_plat_lo - 8;
				const bool r3r_above = r3r_same_row && r3r_axial > r3r_plat_hi && r3r_axial <= r3r_plat_hi + 8;
				/* 判据 ①（同排整段跨过）：从低侧贴过站台、现在却已经跑到站台高侧之外
				 * （或反之），说明 TrackFollower 把整段站台一步跳过去了 —— 这一格已
				 * 经在站台外。 */
				const bool r3r_leaped = (r3r_below && r3r_plat_seen_hi) || (r3r_above && r3r_plat_seen_lo);
				/* 判据 ②（踩进去之后的任何越界）：第 514 轮把它写成 `entered` 单独
				 * 成立就停，既等于"第二次踩上站台就停"（错），又因为站台格被
				 * r3r_resv_path 排除而恒不可达（更错）。现在只保留正确语义：踩上
				 * 站台段之后，**任何**不在该段内的格都算越界 —— 换排、轴向越端都算。 */
				const bool r3r_off_piece = r3r_plat_entered && !r3r_on_piece;
				/* 判据 ③（换排拐出，REG-515-A）：现场越界形态是"从站台这一排拐到
				 * 相邻排再穿出去"（站台 y=1874 → 越界格 y=1873/1875/1876），此时
				 * 判据 ①② 的"同排"前提全部为假，一行都不会响。定义：已经在站台
				 * 自己那一排读到过贴边格（说明正在朝站台开），下一格却落到相邻排
				 * 且轴向仍落在站台±8 格范围内 —— 那是绕着站台走，不是进站。 */
				const bool r3r_adj_row = (r3r_perp == r3r_plat_perp - 1) || (r3r_perp == r3r_plat_perp + 1);
				const bool r3r_cross_row = r3r_adj_row
						&& (r3r_plat_seen_hi || r3r_plat_seen_lo)
						&& r3r_axial >= r3r_plat_lo - 8 && r3r_axial <= r3r_plat_hi + 8;
				if (r3r_leaped || r3r_off_piece || r3r_cross_row) {
					/* 不订这一格，就地收尾：booked 里已经是站台内/站台前的格，调用方
					 * （booked < steps.size()）会把预留终点收到最后一格已订。 */
					break;
				}
				if (r3r_on_piece) r3r_plat_entered = true;
				if (r3r_below) r3r_plat_seen_lo = true;
				if (r3r_above) r3r_plat_seen_hi = true;
			}
			this->ReserveSingleTrack(stile, stdir);
			if (this->res_fail_tile != INVALID_TILE) {
				/* KI-563（玩家拍板 A，2026-11-06 第 393 轮）：预留粒度完全按原生 PBS
				 * ——订到第一个失败格即停，**保留**已订上的格，不再整批回滚。
				 *
				 * 原生本来就是"订一段、走一段、在信号或他人预留前停、下一 tick 续订"；
				 * 整批回滚会让列车在他人预留/信号前一动不动（第 392 轮现场 533 次
				 * CPL-RES-FAIL、CPL-RESERVE found=0 booked=0），比原生更死。
				 * 有格可走就算成功，剩余部分由下一次 TryReservePath 重新摸路续订
				 * （已订格对自己视为成功，见 pbs.cpp 的 KI-361 自订豁免）。 */
				if (booked > 0) {
					/* 与全成功路径一致：已订的轨道要让 YAPF 缓存失效，否则下一 tick
					 * 的寻路仍看见"未预留"。 */
					YapfNotifyTrackLayoutChange(INVALID_TILE, INVALID_TRACK);
					return true;
				}
				return false;
			}
			++booked;
			/* R3R (第 504 轮收口, REG-504-E): 目的地即终点，且是**唯一**的终点。
			 *
			 * 本格是摸路的最后一格 —— rp()/reach() 摸到会合点即停，steps.back()
			 * 就是车底前沿那一格，调用方（:2142 一带）也已经先把预留终点设成它。
			 * 订上它之后立即结束，不再做任何"安全位"判定：
			 *
			 *  · 中段信号不再截断。原生 PBS 按"第一个安全位"收尾是对的，因为
			 *    驾驶侧下一 tick 从车头重算、能自己往前长；挂接预留是一次性摸
			 *    完整条路再订，中途停下就自锁（REG-399-C 的原始推导）。
			 *  · 中段道岔/站台不再需要任何排除。M1 的单轨尺子是用来防"道岔被
			 *    误判成线路尽头"的，现在中段根本不判，尺子自然不需要。
			 *  · 不再有"最后一个安全位"的反向扫描，也就不存在"遍寻不着 ⇒ 兜底
			 *    订满"这条把异常当常态的路径（玩家口径：例外加例外 = 意外）。
			 *
			 * 判在**订成功之后**：目的地格自己必须被订下（机车要开进去贴住车底）。
			 * 若它被第三方或车底占着，上面的 KI-563 A 分支会保留已订格并就地
			 * 返回，与原生 PBS 的粒度一致。
			 *
			 * 因为这里 break 时 booked 已包含最后一格，调用方的
			 * `booked < steps.size()` 收缩分支不会触发，预留终点保持在会合点。 */
			if (i + 1 == steps.size()) {
				stopped_at_goal = true;
				break;
			}
		}
		/* R3R (REG-359-K 探针, 只读): 成功时列出本次实际订下的格序，用来回答
		 * "第 5 次 booked=6 究竟订了哪 6 格、有没有覆盖 36,12/37,12"。
		 *
		 * R3R (第 504 轮收口): `term_exempt=` 字段已删除（终点例外机制整体不再
		 * 存在）。保留 `stop_safe=` 只为兼容既有判读，它与新的 `goal=` 现在
		 * 同义 —— 都是"本轮预留是否在目的地收尾"。因为正常路径恒在最后一格
		 * break，健康形态是 `total=N booked=N stop_safe=1 goal=1`。 */
		if (!steps.empty()) YapfNotifyTrackLayoutChange(INVALID_TILE, INVALID_TRACK);
		/* 修法 D 起，"第一个安全位就被别人占着"会在**一格都没订**的情况下走到这里
		 * （第 394 轮的写法总是先订再判，故 booked 必 >= 1）。此时必须如实报失败，
		 * 否则调用方会把整条摸路当成已预留、只把终点收到会合点（r3r_booked=0 时
		 * 连收缩都不做），再次出现 "reserved=1 而 booked=0" 的空真。 */
		return booked > 0;
	}

	inline void SetReservationTarget(Node *node, TileIndex tile, Trackdir td)
	{
		this->res_dest_node = node;
		this->res_dest_tile = tile;
		this->res_dest_td = td;
	}

	/**
	 * Check the node for a possible reservation target.
	 * @param node The node to check.
	 * Return true if found.
	 */
	inline bool FindSafePositionOnNode(Node *node)
	{
		dbg_assert(node->parent != nullptr);

		if (!node->IterateTiles(Yapf().GetVehicle(), Yapf(), *this, &CYapfReserveTrack<Types>::FindSafePositionProc)) {
			this->res_dest_node = node;
			return true;
		}
		return false;
	}


	/**
	 * R3R: couple-specific node check (ported from pxp-decouple). Uses
	 * FindSafeCouplePositionProc so the coupling locomotive tracks the waiting
	 * consist's reservation instead of stopping at an ordinary safe position.
	 */
	inline bool CheckSafePositionOnNode(Node *node)
	{
		dbg_assert(node->parent != nullptr);

		if (!node->IterateTiles(Yapf().GetVehicle(), Yapf(), *this, &CYapfReserveTrack<Types>::FindSafeCouplePositionProc)) {
			return false;
		}
		return true;
	}

	/**
	 * R3R (第 525 轮续, KI-691): CheckSafePositionOnNode() 的"判到 stop_tile 为止"版本。
	 *
	 * 节点的 segment 可能是「整段站台跳格」（现场 1442 → 1424 一步跨过整个站台），
	 * 于是判据会去判车底远端那几格 —— 那些格本车永远不会开过去，判它们只会把
	 * 整条路误判成不通。本函数走到 stop_tile（= 会合点）即停，之后的格一概不问。
	 *
	 * 必须在基类里实现：FindSafeCouplePositionProc 是 private，派生类
	 * CYapfFollowCoupleRailT 拿不到成员指针。
	 *
	 * @param node       待检查的节点
	 * @param stop_tile  行驶终点（含）。到达该格即停止检查，不再往后判。
	 * @param fail_tile  出参：判据失败的那一格（未失败时不动）。
	 * @return true 表示从节点起点到 stop_tile 为止全部通过。
	 */
	inline bool CheckSafePositionOnNodeTo(Node *node, TileIndex stop_tile, TileIndex *fail_tile)
	{
		dbg_assert(node->parent != nullptr);

		bool ok = true;
		node->template IterateTiles<CYapfReserveTrack>(Yapf().GetVehicle(), Yapf(), [&](TileIndex tile, Trackdir td) -> bool {
			if (tile == stop_tile) return false;   // 行驶终点之后是车底自己的位置，与本次行驶无关
			if (!this->FindSafeCouplePositionProc(tile, td)) {
				ok = false;
				if (fail_tile != nullptr) *fail_tile = tile;
				return false;
			}
			return true;
		});
		return ok;
	}	/** Find a reservation target on the path ending at this node. */
	void FindSafePositionOnSafePositionNodes()
	{
		const auto &safe_position_nodes = CYapfReserveTrackSafePositionNodes<Node>::safe_position_nodes;
		for (auto it = safe_position_nodes.rbegin(); it != safe_position_nodes.rend(); ++it) {
			if (this->FindSafePositionOnNode(*it)) return;
		}
	}

	/**
	 * Try to reserve the path till the reservation target.
	 *
	 * R3R (第 503 轮): the old third parameter `unsafe_pos` was one boolean doing three
	 * unrelated jobs (skip the endpoint test / do not roll back on a failed tile / do
	 * not feed the answer book), which is why the couple path could not be made atomic
	 * without also losing its answer-book opt-out. It is now three explicit flags.
	 *
	 * @param target End location of the reservation.
	 * @param origin Start location of the reservation.
	 * @param atomic All-or-nothing: on the first tile that cannot be booked, undo
	 *        everything this call reserved and return \c false. The couple path used to
	 *        pass `unsafe_pos=true` (i.e. non-atomic), and that is exactly what produced
	 *        "half a path reserved, then the train is sent down an unreserved branch".
	 * @param check_target_free Run the endpoint test (IsWaitingPositionFree) first. The
	 *        couple path can pass \c true now that pbs.cpp treats the pair partner's own
	 *        reservation as air there; it used to have to skip the test because its
	 *        destination always lies *on* the partner.
	 * @param record_answer Push the walked tiles into the answer book (main pathfinding
	 *        only; the couple path keeps its own two-pass walk).
	 * @return \c true iff the path could be reserved.
	 */
	bool TryReservePath(PBSTileInfo *target, TileIndex origin, bool atomic = true, bool check_target_free = true, bool record_answer = true)
	{
		/* R3R (KI-207): every pathfinder-made reservation passes through here, so
		 * record which train is booking.
		 *
		 * R3R (第 243 轮, REG-243-M): this used to pass nullptr and inherit the
		 * caller's phase ("choose-track"), which made a release made *inside* YAPF
		 * indistinguishable from one made by the caller *after* YAPF returned -- both
		 * printed the same phase. It now names its own phase so the two separate.
		 * No behaviour change, only a new log tag. */
		R3RResvPhaseGuard r3r_phase("yapf-resv", R3RResvActorID(Yapf().GetVehicle()));
		this->res_fail_tile = INVALID_TILE;
		this->origin_tile = origin;
		/* R3R (答案本): 本次铺预留的逐格序列，成功收尾时由 R3RAnswerCommit() 取走。 */
		_r3r_answer_build.clear();
		/* R3R (第 342 轮, ε-A): 每次预留尝试从"无自锁"开始，避免陈旧记录 */
		R3RClearSelfLock();

		if (target != nullptr) {
			target->tile = this->res_dest_tile;
			target->trackdir = this->res_dest_td;
			target->okay = false;
		}

		/* Don't bother if the target is reserved. */
		PBSWaitingPositionRestrictedSignalState restricted_signal_state;
		restricted_signal_state.defer_test_if_slot_conditional = true;
		if (check_target_free && !IsWaitingPositionFree(Yapf().GetVehicle(), this->res_dest_tile, this->res_dest_td, false, &restricted_signal_state)) return false;

		/* The temporary slot state only needs to be pushed to the stack (i.e. activated) on first use */
		static TraceRestrictSlotTemporaryState temporary_slot_state;
		assert(temporary_slot_state.IsEmpty() && !temporary_slot_state.IsActive());

		struct IntermediaryTraceRestrictSignalInfo {
			const TraceRestrictProgram *prog;
			TileIndex tile;
			Trackdir trackdir;
			bool front_side;
		};
		/* Nodes are iterated in reverse order (from the target), but tiles within the node are iterated in forward order (towards the target).
		 * intermediary_restricted_signals is in reverse order, (the first signal to evaluate at the end).
		 */
		static std::vector<IntermediaryTraceRestrictSignalInfo> intermediary_restricted_signals;
		intermediary_restricted_signals.clear();

		/* R3R 探针 (第 343 轮, KI-505): 只数"真正置位成功了多少格"。
		 * 动机：同一 tick 出现 CPL-RESERVE reserved=1 与 CPL-RET res=0x0 并存
		 * （build\R3R_debug.log:1406-1407），而当时 unsafe_pos=true 的挂接路径
		 * 在格级失败时既不回滚也不早退 ⇒ "reserved=1"可能是空心的。
		 * 第 503 轮把 unsafe_pos 拆成 atomic/check_target_free/record_answer 三参，
		 * 挂接也改走 atomic=true，空心成功已结构性不可能；计数器保留继续兜底。 */
		int r3r_ntry = 0;
		int r3r_nok = 0;

		for (Node *node = this->res_dest_node; node->parent != nullptr; node = node->parent) {
			const size_t intermediary_restricted_signals_current_size = intermediary_restricted_signals.size();
			node->template IterateTiles<CYapfReserveTrack>(Yapf().GetVehicle(), Yapf(), [&](TileIndex tile, Trackdir td) -> bool {
				/* Cheapest tests first */
				if (IsTileType(tile, TileType::Railway) && HasSignals(tile) && IsRestrictedSignal(tile) && HasSignalOnTrack(tile, TrackdirToTrack(td))) {
					const bool front_side = HasSignalOnTrackdir(tile, td);

					TraceRestrictProgramActionsUsedFlags au_flags = TRPAUF_SLOT_ACQUIRE;
					if (front_side) {
						/* Passing through a signal from the front side */
						au_flags |= TRPAUF_WAIT_AT_PBS;
					}

					const TraceRestrictProgram *prog = GetExistingTraceRestrictProgram(tile, TrackdirToTrack(td));
					if (prog != nullptr && prog->actions_used_flags & au_flags) {
						/* Insert at intermediary_restricted_signals_current_size, such that if there are multiple signals for this node, they end up in reverse order */
						intermediary_restricted_signals.insert(intermediary_restricted_signals.begin() + intermediary_restricted_signals_current_size, { prog, tile, td, front_side });
					}
				}

				const bool r3r_tok = this->ReserveSingleTrack(tile, td);
				/* R3R (答案本): 只记主寻路这条路径的格；挂接寻路(record_answer=false)不参与。 */
				if (r3r_tok && record_answer) _r3r_answer_build.push_back(R3RAnswerStep{tile, td});
				++r3r_ntry;
				if (r3r_tok) {
					++r3r_nok;
				} else {
					/* 失败格：(veh, tile, td, 已成功格数) 去重，避免每 tick 刷屏 */
					static std::map<uint32_t, uint64_t> r3r_rpf_seen;
					const uint32_t r3r_vid = Yapf().GetVehicle()->index.base();
					const uint64_t r3r_sig = ((uint64_t)r3r_nok << 48) ^ ((uint64_t)tile.base() << 16)
							^ ((uint64_t)td << 8) ^ (uint64_t)r3r_ntry;
					uint64_t &r3r_prev = r3r_rpf_seen[r3r_vid];
					if (r3r_prev != r3r_sig) {
						r3r_prev = r3r_sig;
					}
				}
				return r3r_tok;
			});
			if (atomic && this->res_fail_tile != INVALID_TILE) {
				/* Reservation failed, undo. */
				Node *fail_node = this->res_dest_node;
				TileIndex stop_tile = this->res_fail_tile;
				do {
					/* If this is the node that failed, stop at the failed tile. */
					this->res_fail_tile = fail_node == node ? stop_tile : INVALID_TILE;
					fail_node->IterateTiles(Yapf().GetVehicle(), Yapf(), *this, &CYapfReserveTrack<Types>::UnreserveSingleTrack);
				} while (fail_node != node && (fail_node = fail_node->parent) != nullptr);

				if (temporary_slot_state.IsActive()) temporary_slot_state.PopFromChangeStackRevertTemporaryChanges(Yapf().GetVehicle()->index);
				return false;
			}
		}

		auto undo_reservation = [&]() {
			for (Node *node = this->res_dest_node; node->parent != nullptr; node = node->parent) {
				node->IterateTiles(Yapf().GetVehicle(), Yapf(), *this, &CYapfReserveTrack<Types>::UnreserveSingleTrack);
			}
			if (temporary_slot_state.IsActive()) temporary_slot_state.PopFromChangeStackRevertTemporaryChanges(Yapf().GetVehicle()->index);
		};

		/* Iterate in reverse order */
		for (auto iter = intermediary_restricted_signals.rbegin(); iter != intermediary_restricted_signals.rend(); ++iter) {
			extern TileIndex VehiclePosTraceRestrictPreviousSignalCallback(const Train *v, const void *, TraceRestrictPBSEntrySignalAuxField mode);

			TraceRestrictProgramInput input(iter->tile, iter->trackdir, &VehiclePosTraceRestrictPreviousSignalCallback, nullptr);
			if (iter->prog->actions_used_flags & TRPAUF_SLOT_ACQUIRE) {
				input.permitted_slot_operations = TRPISP_ACQUIRE_TEMP_STATE;

				if (!temporary_slot_state.IsActive()) {
					/* The temporary slot state needs to be be pushed because permission to use it is granted by TRPISP_ACQUIRE_TEMP_STATE */
					temporary_slot_state.PushToChangeStack();
				}
			}

			TraceRestrictProgramResult out;
			iter->prog->Execute(Yapf().GetVehicle(), input, out);
			if (iter->front_side && out.flags & TRPRF_WAIT_AT_PBS) {
				/* Wait at PBS is set, take this as waiting at the start signal */
				undo_reservation();
				return false;
			}
		}

		if (restricted_signal_state.deferred_test) {
			/* The IsWaitingPositionFree restricted signal test was deferred due to possible slot changes during reservation, test it now */
			if (!IsWaitingPositionFreeTraceRestrictExecute(restricted_signal_state.prog, Yapf().GetVehicle(), restricted_signal_state.tile, restricted_signal_state.trackdir)) {
				/* Target is reserved, undo reservation */
				undo_reservation();
				return false;
			}
		}

		/* This must be done before calling TraceRestrictExecuteResEndSlot */
		TraceRestrictSlotTemporaryState::ClearChangeStackApplyAllTemporaryChanges(Yapf().GetVehicle());

		restricted_signal_state.TraceRestrictExecuteResEndSlot(Yapf().GetVehicle());

		/* R3R 探针 (第 344 轮, 修法第三档 S1): 无条件汇总，取代原先只在失败时
		 * 打印的 RPSUM —— 探针总数不增。
		 *
		 * 目的：把「挂接订路是不是从一开头就 nTry==0」从推断变成可统计。nTry==0
		 * 只有一种成因：:637 的循环体一次都没进，而循环门槛是 node->parent != nullptr，
		 * 所以它等价于 **res_dest_node 就是根节点**。故直接打印 node 的格与 root 标志，
		 * 一眼可判 —— 现场（build\R3R_debug.log:1757-1758）正是
		 *   CPL-RESERVE reserved=1 与 CPL-RET res=0x0 并存。
		 * 签名去重，零行为改动。 */
		{
			static std::map<uint32_t, uint64_t> r3r_cplres_seen;
			const Vehicle *const r3r_v = Yapf().GetVehicle();
			const uint32_t r3r_vid = r3r_v->index.base();
			const TileIndex r3r_node_tile = this->res_dest_node->GetLastTile();
			/* R3R (第 459 轮, REG-456-E): 原来只打 origin 的坐标，看不出这个 origin
			 * 到底是「车脚下」「目的地」还是第三处（上一段预留终点 / 站台回扫 / 站台
			 * 末端）。玩家报的现象是车在 211,116、订单去 221,129，而订路的入口却在
			 * 218,122 —— 只有把 origin 归到类里才能判它是哪一种。纯读。 */
			const int r3r_vtx = (int)TileX(r3r_v->tile);
			const int r3r_vty = (int)TileY(r3r_v->tile);
			const int r3r_odx = (int)TileX(this->origin_tile);
			const int r3r_ody = (int)TileY(this->origin_tile);
			const uint64_t r3r_cplres_sig = ((uint64_t)(uint32_t)r3r_node_tile.base() << 40)
					^ ((uint64_t)(uint32_t)this->origin_tile.base() << 16)
					^ ((uint64_t)(uint32_t)r3r_ntry << 8)
					^ ((uint64_t)(uint32_t)r3r_nok << 4)
					^ (uint64_t)(uint32_t)this->res_dest_td;
			uint64_t &r3r_cplres_prev = r3r_cplres_seen[r3r_vid];
			if (r3r_cplres_prev != r3r_cplres_sig) {
				r3r_cplres_prev = r3r_cplres_sig;
			}
		}

		if (target != nullptr) target->okay = true;

		if (Yapf().CanUseGlobalCache(*this->res_dest_node)) {
			YapfNotifyTrackLayoutChange(INVALID_TILE, INVALID_TRACK);
		}

		return true;
	}

	/**
	 * R3R (答案本, 2026-11-09): 照抄上一次 A* 算出来的路径，逐格铺预留，不跑搜索。
	 *
	 * 等价性的全部依据有两条：
	 *   1. A* 无随机项 —— 地图（轨道/信号/别人的预留）不变则答案必然相同；
	 *   2. 铺预留本身就是校验 —— 抄的过程仍走 ReserveSingleTrack，
	 *      订不上即答案已失效，不需要额外的失效通知机制。
	 *
	 * 安全边界：只抄到"答案段内最后一个岔路之前"。岔路 = 该格有多条轨道可走，
	 * 那里才是真正的决策点，一律留给 A*（见备忘 §17.4）。
	 *
	 * 为什么必须放在 CYapfReserveTrack 而不是派生类 CYapfFollowRailT：
	 * ReserveSingleTrack / origin_tile / res_fail_tile / res_dest_* 都是本类
	 * private 成员，派生类看不见（第一版写在派生类里，编译报 C2248）。
	 *
	 * @return true 表示已铺好预留，调用方应直接返回 first_td 而不进 YAPF。
	 */
	bool R3RAnswerReplay(const Train *v, const PBSTileInfo &origin, PBSTileInfo *target, TileIndex *dest, Trackdir *first_td)
	{
		/* 开 desync/yapfdesync 调试时会同一场景跑两遍做逐节点比对，此时一律走原生
		 * 寻路，保证两次调用的行为与改前逐字相同，不受答案本影响。 */
		if (GetDebugLevel(DebugLevelID::yapfdesync) >= 1 || GetDebugLevel(DebugLevelID::desync) >= 2) return false;

		/* 车必须由调用方传入，绝不能用 Yapf().GetVehicle()：本函数跑在 FindPath() 之前，
		 * 基类的 vehicle 成员此刻还没赋值（yapf_base.hpp:163 只在 FindPath 里写），
		 * 首次寻路就是 nullptr。第 409 轮崩溃正是这里解引用 nullptr + 0x38：
		 * crash-20261108T164103Z.log 栈 [00] PoolID<VehicleIDTag>::base [01] R3RAnswerReplay。 */
		assert(v != nullptr);
		const Train *const veh = v;
		const uint32_t key = veh->index.base();
		if (veh->current_order.IsType(OT_GOTO_COUPLE)) return false; // 挂接寻路有独立语义，不抄答案

		auto it = _r3r_answer_steps.find(key);
		if (it == _r3r_answer_steps.end()) return false;
		const std::vector<R3RAnswerStep> &steps = it->second;

		/* 换单是唯一"车的物理状态分毫未变、答案却指向别处"的失效场景：origin 照样能
		 * 定位（车就停在旧路径上）、轨道照样能订上，于是一路开到旧目的地。记录时存的是
		 * 当时的订单目的地 v->dest_tile，这里比一次即可，且不依赖任何外部通知
		 * （玩家改单、自动推进到下一条单、条件单跳转都可能不经过 InvalidateVehicleOrder）。 */
		const auto dit = _r3r_answer_dest.find(key);
		if (dit == _r3r_answer_dest.end() || dit->second != veh->dest_tile) {
			R3RAnswerDrop(veh->index, "dest-changed");
			return false;
		}

		/* 车辆可能停在上次答案段的中间，故按 (tile, td) 在答案里定位。
		 * 定位不到（换道/掉头/被挪走）即作废，回落原生寻路。 */
		size_t idx = steps.size();
		for (size_t i = 0; i < steps.size(); i++) {
			if (steps[i].tile == origin.tile && steps[i].td == origin.trackdir) { idx = i; break; }
		}
		if (idx >= steps.size() || idx + 1 >= steps.size()) {
			R3RAnswerDrop(veh->index, "origin-not-in-answer");
			return false;
		}

		R3RResvPhaseGuard r3r_phase("yapf-answer", R3RResvActorID(veh));
		this->res_fail_tile = INVALID_TILE;
		this->res_fail_td = INVALID_TRACKDIR;
		this->origin_tile = origin.tile;
		/* ReserveSingleTrack() 的返回值是 "tile != res_dest_tile || td != res_dest_td"，
		 * 而复刻路径从不调用 SetReservationTarget()，必须显式清掉上一轮的残留目标，
		 * 否则恰好抄到同一格时会被误判成"预订失败"。 */
		this->res_dest_tile = INVALID_TILE;
		this->res_dest_td = INVALID_TRACKDIR;

		size_t booked = 0;
		size_t last = idx;
		bool hit_choice = false;
		for (size_t i = idx + 1; i < steps.size(); i++) {
			if (!this->ReserveSingleTrack(steps[i].tile, steps[i].td, veh)) break;
			/* 站台分支订不上时只在 res_fail_tile 上留痕、返回值仍是 true，
			 * 必须在这里补一次检查，否则会把失败当成功继续往下订。 */
			if (this->res_fail_tile != INVALID_TILE) break;
			++booked;
			last = i;
			/* 订完这一格后若它自己就是岔路格，就地收手：岔路之后的走向是决策，不作数。 */
			const bool plain = IsTileType(steps[i].tile, TileType::Railway) && IsPlainRail(steps[i].tile);
			if (plain && KillFirstBit(GetTrackBits(steps[i].tile)) != TRACK_BIT_NONE) { hit_choice = true; break; }
		}

		if (booked == 0) {
			/* 一格都没订上：答案已失效，交给 A* 重算（本 tick 不重复尝试）。 */
			R3RAnswerDrop(veh->index, "nothing-booked");
			return false;
		}

		if (target != nullptr) {
			target->tile = steps[last].tile;
			target->trackdir = steps[last].td;
			target->okay = true;
		}
		if (dest != nullptr) *dest = steps[last].tile;
		/* 返回值语义 = "下一格上要走的 trackdir"（原生取根节点第一个子节点的
		 * trackdir），不是 origin 格自己的出口方向 —— 故取第一格抄到的步。 */
		*first_td = steps[idx + 1].td;
		++_r3r_answer_hit_count[key];
		return true;
	}

	static void stDesyncCheck(Tpf &pf1, Tpf &pf2, const char *name, bool check_res)
	{
		Node *n1 = pf1.GetBestNode();
		Node *n2 = pf2.GetBestNode();
		uint depth = 0;
		for (;;) {
			if ((n1 != nullptr) != (n2 != nullptr)) {
				Debug(desync, 0, "{}: node nonnull state at {} = [{}, {}]", name, depth, (n1 != nullptr), (n2 != nullptr));
				DumpState(pf1, pf2);
				return;
			}
			if (n1 == nullptr) break;

			if (n1->GetTile() != n2->GetTile()) {
				Debug(desync, 0, "{} tile mismatch at {} = [0x{:X}, 0x{:X}]", name, depth, n1->GetTile(), n2->GetTile());
				DumpState(pf1, pf2);
				return;
			}
			if (n1->GetTrackdir() != n2->GetTrackdir()) {
				Debug(desync, 0, "{} trackdir mismatch at {} = [0x{:X}, 0x{:X}]", name, depth, n1->GetTrackdir(), n2->GetTrackdir());
				DumpState(pf1, pf2);
				return;
			}
			n1 = n1->parent;
			n2 = n2->parent;
			depth++;
		}

		if (check_res && (pf1.res_dest_tile != pf2.res_dest_tile || pf1.res_dest_td != pf2.res_dest_td)) {
			Debug(desync, 0, "{} reservation target mismatch = [(0x{:X}, {}), (0x{:X}, {})]", name, pf1.res_dest_tile, pf1.res_dest_td, pf2.res_dest_tile, pf2.res_dest_td);
			DumpState(pf1, pf2);
			return;
		}
	}
};

template <class Types>
class CYapfFollowAnyDepotRailT {
public:
	typedef typename Types::Tpf Tpf; ///< the pathfinder class (derived from THIS class)
	typedef typename Types::TrackFollower TrackFollower;
	typedef typename Types::NodeList::Item Node; ///< this will be our node type
	typedef typename Node::Key Key; ///< key to hash tables

protected:
	/** @copydoc CYapfBaseT::Yapf */
	inline Tpf &Yapf()
	{
		return *static_cast<Tpf *>(this);
	}

public:
	/** @copydoc CYapfBaseT::PfFollowNodeFunc */
	inline void PfFollowNode(Node &old_node)
	{
		const Train *v = Yapf().GetVehicle();
		TrackFollower follower(v);
		if (old_node.flags_u.flags_s.reverse_pending && old_node.segment->end_segment_reason.Any({EndSegmentReason::SafeTile, EndSegmentReason::Depot, EndSegmentReason::DeadEnd})) {
			Node *rev_node = &old_node;
			uint length = 0;
			while (rev_node && !rev_node->segment->end_segment_reason.Test(EndSegmentReason::Reverse)) {
				length += rev_node->GetNodeLength(v, Yapf(), *this);
				rev_node = rev_node->parent;
			}
			if (rev_node && length >= v->gcache.cached_total_length) {
				if (follower.Follow(rev_node->GetLastTile(), ReverseTrackdir(rev_node->GetLastTrackdir()))) {
					Yapf().AddMultipleNodes(&old_node, follower, [&](Node &n) {
						n.flags_u.flags_s.reverse_pending = false;
						n.flags_u.flags_s.teleport = true;
					});
				}
				return;
			} else if (old_node.segment->end_segment_reason.Any({EndSegmentReason::Depot, EndSegmentReason::DeadEnd})) {
				return;
			}
		}
		if (follower.Follow(old_node.GetLastTile(), old_node.GetLastTrackdir())) {
			Yapf().AddMultipleNodes(&old_node, follower);
		}
	}

	/** @copydoc CYapfBaseT::TransportTypeCharFunc */
	inline char TransportTypeChar() const
	{
		return 't';
	}

	static FindDepotData stFindNearestDepotTwoWay(const Train *v, TileIndex t1, Trackdir td1, TileIndex t2, Trackdir td2, int max_penalty, int reverse_penalty)
	{
		Tpf pf1;
		/*
		 * With caching enabled it simply cannot get a reliable result when you
		 * have limited the distance a train may travel. This means that the
		 * cached result does not match uncached result in all cases and that
		 * causes desyncs. So disable caching when finding for a depot that is
		 * nearby. This only happens with automatic servicing of vehicles,
		 * so it will only impact performance when you do not manually set
		 * depot orders and you do not disable automatic servicing.
		 */
		if (max_penalty != 0) pf1.DisableCache(true);
		FindDepotData result1 = pf1.FindNearestDepotTwoWay(v, t1, td1, t2, td2, max_penalty, reverse_penalty);

		if (GetDebugLevel(DebugLevelID::yapfdesync) > 0 || GetDebugLevel(DebugLevelID::desync) >= 2) {
			Tpf pf2;
			pf2.DisableCache(true);
			FindDepotData result2 = pf2.FindNearestDepotTwoWay(v, t1, td1, t2, td2, max_penalty, reverse_penalty);
			if (result1.tile != result2.tile || (result1.reverse != result2.reverse)) {
				Debug(desync, 0, "CACHE ERROR: FindNearestDepotTwoWay() = [{}, {}]",
						result1.tile != INVALID_TILE ? "T" : "F",
						result2.tile != INVALID_TILE ? "T" : "F");
				DumpState(pf1, pf2);
			}
		}

		return result1;
	}

	inline FindDepotData FindNearestDepotTwoWay(const Train *v, TileIndex t1, Trackdir td1, TileIndex t2, Trackdir td2, int max_penalty, int reverse_penalty)
	{
		/* set origin and destination nodes */
		Yapf().SetOrigin(t1, td1, t2, td2, reverse_penalty);
		Yapf().SetTreatFirstRedTwoWaySignalAsEOL(true);
		Yapf().SetDestination(v);
		Yapf().SetMaxCost(max_penalty);

		/* find the best path */
		if (!Yapf().FindPath(v)) return FindDepotData();

		/* Some path found. */
		Node *n = Yapf().GetBestNode();

		/* walk through the path back to the origin */
		Node *node = n;
		while (node->parent != nullptr) {
			node = node->parent;
		}

		/* if the origin node is our front vehicle tile/Trackdir then we didn't reverse
		 * but we can also look at the cost (== 0 -> not reversed, == reverse_penalty -> reversed) */
		return FindDepotData(n->GetLastTile(), n->cost, node->cost != 0);
	}
};

/**
 * R3R: does the waiting consist's tile (target) lie on the station stretch the
 * TrackFollower skipped over in its last step?
 *
 * A rail TrackFollower jumps from the platform entry tile straight to the far
 * end of the platform in a single step (CFollowTrackT::CanEnterNewTile), so a
 * consist parked on the platform would be skipped entirely. The exact stretch
 * is reconstructible from the follower's own state: it starts one step from
 * old_tile in the follower's exit direction and covers tiles_skipped + 1 tiles,
 * ending on new_tile.
 *
 * A previous version scanned along the platform axis in *both* directions with
 * a pure coordinate comparison, unbounded by the follower's real step, so any
 * tile that merely shared the axis line - e.g. the consist standing at another
 * station on the same row/column - was mistaken for the skipped target. That
 * produced a bogus "already reached" result and, in the tracer below, a
 * departure direction (and reservation) leading away from the consist.
 *
 * @param old_tile      Tile the follower started from.
 * @param new_tile      Tile the follower landed on (far end of the platform).
 * @param exitdir       Direction of travel used by the follower.
 * @param tiles_skipped Number of tiles the follower jumped over.
 * @param target        Tile to look for on the skipped stretch.
 * @return true iff target lies on the tiles the follower skipped over.
 */
static bool R3RSkippedStationStretchContains(TileIndex old_tile, TileIndex new_tile, DiagDirection exitdir, int tiles_skipped, TileIndex target)
{
	if (tiles_skipped <= 0 || !IsValidDiagDirection(exitdir)) return false;
	const TileIndexDiff step = TileOffsByDiagDir(exitdir);
	TileIndex t = old_tile;
	for (int i = 0; i <= tiles_skipped; ++i) {
		t += step;
		/* Every skipped tile belongs to the same platform; anything else means
		 * the follower did not leap a station and the stretch is not valid. */
		if (!IsRailStationTile(t)) return false;
		if (t == target) return true;
		if (t == new_tile) break;
	}
	return false;
}

/**
 * Follow class for the "couple pathfinder": standard rail following used to
 * search for the nearest waiting consist (see CYapfDestinationTrainRailT).
 */
template <class Types>
class CYapfFollowCoupleRailT : public CYapfReserveTrack<Types> {
public:
	typedef typename Types::Tpf Tpf;                     ///< the pathfinder class (derived from THIS class)
	typedef typename Types::TrackFollower TrackFollower;
	typedef typename Types::NodeList::Item Node;        ///< this will be our node type
	typedef typename Node::Key Key;                      ///< key to hash tables

protected:
	/** @copydoc CYapfBaseT::Yapf */
	inline Tpf &Yapf()
	{
		return *static_cast<Tpf *>(this);
	}

public:
	/** @copydoc CYapfBaseT::PfFollowNodeFunc */
	inline void PfFollowNode(Node &old_node)
	{
		/* R3R: a depot tile is a dead end - never extend a path through it. */
		if (IsRailDepotTile(old_node.GetLastTile()) && old_node.parent != nullptr) return;
		TrackFollower F(Yapf().GetVehicle(), Yapf().GetCompatibleRailTypes());
		/* R3R（KI-676）：挂接寻路必须逐格走站台。默认的 TrackFollower 一进站台就整段跳到站台
		 * 末端（follow_track.hpp 的 platform leap），会合点（如 1439,1874）因此从不成为节点，
		 * 精确格判定永远 found=0。这里关掉跳跃，只作用于挂接寻路。 */
		F.platform_leap = false;
		/* R3R（KI-676 诊断 S5-cpp）：只对 y=1874、x∈[1436,1446] 这一排定点取证。
		 * 放在 .cpp 而不是 yapf_destrail.hpp，是因为本树的 ninja 不跟踪头文件依赖
		 * （KI-15），改头文件必须全量重编，改 .cpp 只重编本 TU。 */
		const TileIndex ot = old_node.GetLastTile();
		const TileIndex kt = old_node.GetKey().tile;
		const bool probe_geo = (TileY(ot) == 1874 && TileX(ot) >= 1436 && TileX(ot) <= 1446);
		const bool probe = (TileY(kt) == 1874 && TileX(kt) >= 1436 && TileX(kt) <= 1446);
		static bool r3r_pfx_geo_dumped = false;
		if (probe_geo && !r3r_pfx_geo_dumped) {
			r3r_pfx_geo_dumped = true;
		}
		const bool pfx_ok = F.Follow(old_node.GetLastTile(), old_node.GetLastTrackdir());
		if (probe) {
			static uint32_t r3r_pfx_seen[16] = {0};
			const uint px = TileX(kt) - 1436;
			if (px < 16 && r3r_pfx_seen[px] < 3) {
				++r3r_pfx_seen[px];
			}
		}
		if (pfx_ok) {
			Yapf().AddMultipleNodes(&old_node, F);
		}
	}

	/** @copydoc CYapfBaseT::TransportTypeCharFunc */
	inline char TransportTypeChar() const
	{
		return 't';
	}

	static Trackdir stFindNearestCoupleTrain(const Train *v, bool dont_reserve)
	{
		/* Create pathfinder instance */
		Tpf pf1;
		pf1.DisableCache(true);
		return pf1.FindNearestCoupleTrain(v, dont_reserve);
	}

	Trackdir FindNearestCoupleTrain(const Train *v, bool dont_reserve)
	{
		/* R3R (KI-207): this is the GOTO_COUPLE pathfinder -- it books the route the
		 * loco drives to the waiting consist. Name it for the reservation trace. */
		R3RResvPhaseGuard r3r_phase("yapf-couple", R3RResvActorID(v));
		PBSTileInfo origin = PBSTileInfo(v->tile, v->GetVehicleTrackdir(), false);
		/* Set origin and destination. */
		Yapf().SetOrigin(origin.tile, origin.trackdir);

		/* R3R (第 386 轮, KI-551): 「等待区路点预留截断」（KI-429）已整机制删除。
		 *
		 * 玩家 2026-11-02 拍板：该机制在现场表现为"无条件把预留在路点截止"，
		 * 不再修补条件判断，直接删除。此处原先会调用
		 * R3RShouldCutCoupleAtWaitPoint() 并用 SetCoupleWaitCut() 把 YAPF 的
		 * 终点集合收窄到等待区路点；现在终点判定回到普通口径
		 * （站台/车库 + 已有预留），挂接寻路可以一路铺到目标车底。
		 *
		 * 注意：R3RShouldCutCoupleAtWaitPoint() 本体保留，它仍被"等待区路点
		 * 头停车"与"越墙判定"使用，与预留无关。 */

		/* R3R (第 346 轮续, KI-516): 每次挂接寻路先清"被第三方车挡"标志，
		 * 只有下面回溯阶段的 CPL-SAFE-FAIL 才会重新置位。 */
		_r3r_couple_path_blocked = false;

		Yapf().SetDestination(v);

		const bool path_found = Yapf().FindPath(v);
		if (!path_found) return INVALID_TRACKDIR;

		/* Found a destination, set as reservation target. */
		Node *pNode = Yapf().GetBestNode();
		/* R3R: route to the consist head car itself instead of the platform dest tile -
		 * the platform dest may sit on the far side of the consist, making the path
		 * cut straight through it. */
		TileIndex consist_head_tile = INVALID_TILE;
		bool best_on_consist = false;
		/* R3R（第 502 轮, S4, KI-676）：静态终点接管。
		 *
		 * 配对锁一旦建立，"会合点在哪"就只由车底自身决定，与"best 格上是谁"无关。
		 * 因此先读 S2b 快照（O(1) 纯比较），命中就整块跳过下面的动态解析：
		 *   反查 GetTrainForReservation 的预留主人 → 排除自己（KI-432）→
		 *   扫格上别的链 → 按曼哈顿距离选近端（KI-200）。
		 *
		 * 那套动态解析存在的唯一理由是"终点动态"（不知道车底是谁，只能从地图上
		 * 猜）。快照失效时（无配对锁 / 车底已不在站台 / 被移走）它原样保留作回退，
		 * 所以本次是"静态优先、动态兜底"，不存在删掉既有补丁分支的风险。
		 *
		 * best_on_consist 仍必须算：YAPF 若停在车底自己占着的那一格，路径终点就是
		 * 它，预留目标必须同格（KI-200 的硬约束），绝不能用"近端"覆盖。 */
		const TileIndex r3r_snap_tile = Yapf().R3RStaticTargetTile();
		if (pNode != nullptr && r3r_snap_tile != INVALID_TILE) {
			const VehicleID r3r_snap_veh = Yapf().R3RStaticTargetVehicle();
			for (Train *tr : VehiclesOnTile<VehicleType::Train>(pNode->GetLastTile())) {
				if (tr->First()->index == r3r_snap_veh) { best_on_consist = true; break; }
			}
			const TileIndex r3r_snap_far = Yapf().R3RStaticTargetFarTile();
			/* R3R（第 503 轮, S5, KI-676）：会合点优先。
			 *
			 * 会合点 = 车底近端「预留所在格」按轨型再往外一格（现场 1439,1874），
			 * 由快照 O(1) 算出。它一旦成立，"终点在哪"就没有第二种可能，下面那套
			 * "best 落在车底上取 best 格 / 否则按曼哈顿选近端"的端选逻辑整块跳过 ——
			 * 那套逻辑正是 20/23 条现场停在远端 1424（穿过整条车底、越站台后被
			 * 道岔轨型带进 1873）的来源。
			 *
			 * best_on_consist 只作诊断保留：终点收到会合点后，YAPF 不会停在车底
			 * 占着的格上，所以正常应为 0。 */
			const TileIndex r3r_meet = Yapf().R3RStaticMeetTile();
			if (r3r_meet != INVALID_TILE) {
				consist_head_tile = r3r_meet;
			} else if (best_on_consist) {
				consist_head_tile = pNode->GetLastTile();
			} else if (r3r_snap_far != INVALID_TILE &&
					DistanceManhattan(v->tile, r3r_snap_far) < DistanceManhattan(v->tile, r3r_snap_tile)) {
				consist_head_tile = r3r_snap_far;
			} else {
				consist_head_tile = r3r_snap_tile;
			}
		} else if (pNode != nullptr && IsRailStationTile(pNode->GetLastTile())) {
			/* R3R (第 276 轮续, KI-432): 绝不能把"自己"认成等待车底。
			 * 放行之后 TryReservePath() 把路径铺到 best 节点，那一格的预留记在
			 * 本车名下；下一 tick 在这里解析"谁等在这格"时 GetTrainForReservation()
			 * 先命中自己 ⇒ ct = 本车 ⇒ h = 本车链头（自己脚下） ⇒ consist_head_tile
			 * 等于起点 ⇒ reach() 首条即真 ⇒ best_td = 0，预留退化成 1 格、随即被
			 * CLEAR-STN 释放，挂接永远不成立（现场 CPL-HEAD head=254,233 与
			 * CPL-ENTRY tile=254,233 同格）。
			 * GetCouplePosition() 早有 other_vehicle->First()->index == v->index 的
			 * 同一守卫，这里补齐：命中自己就丢弃，回落到"格上真正停着的别的链"；
			 * 若该格上没有别的链，consist_head_tile 保持 INVALID_TILE，下游会退到
			 * pNode->GetLastTile()，绝不用自己脚下当耦合端。 */
			const VehicleID self_first = v->First()->index;
			Train *ct = GetTrainForReservation(pNode->GetLastTile(), TrackdirToTrack(pNode->GetLastTrackdir()));
			if (ct != nullptr && ct->First()->index == self_first) ct = nullptr;
			if (ct == nullptr) {
				for (Train *tr : VehiclesOnTile<VehicleType::Train>(pNode->GetLastTile())) {
					if (tr->IsFrontEngine() && tr->First()->index != self_first) { ct = tr; break; }
				}
			}
			if (ct != nullptr) {
				Train *h = ct->First();
				Train *t = ct->Last();
				if (h != nullptr && t != nullptr) {
					for (Train *tr : VehiclesOnTile<VehicleType::Train>(pNode->GetLastTile())) {
						if (tr->First() == h) { best_on_consist = true; break; }
					}
				}
				if (h != nullptr && t != nullptr) {
					/* R3R (KI-200): path end and reservation target must be the SAME end.
					 * YAPF accepted the search on a real consist tile (the log line
					 * "PFD-ACCEPT ... tile=58,19" shows the accepted tile carries the
					 * consist's head car) -- that tile is the end the loco can actually
					 * reach. Overriding it with the Manhattan-nearest end (58,23) made the
					 * path end at 58,19 while the reservation target was 58,23, so the
					 * hand-rolled reserve walk below had to trace a route back through the
					 * consist, reserving the whole detour (log: "CPL-BEST 58,19" then
					 * "CPL-HEAD best=58,19 head=58,23" then a 15-tile RP chain). Only when
					 * YAPF stopped on an EMPTY platform tile (the probe scans the rest of
					 * the platform for the consist) does the nearest end have to be
					 * resolved here. */
					if (best_on_consist) {
						consist_head_tile = pNode->GetLastTile();
					} else if (DistanceManhattan(v->tile, t->tile) < DistanceManhattan(v->tile, h->tile)) {
						consist_head_tile = t->tile;
					} else {
						consist_head_tile = h->tile;
					}
				} else if (h != nullptr) {
					consist_head_tile = h->tile;
				}
			}
		}
		if (pNode->parent == nullptr) {
			/* R3R: zero-length couple path - the best node is the origin itself
			 * (the locomotive already shares a tile with the waiting consist's
			 * coupling end). There is no path segment to walk back or reserve;
			 * report no track so ChooseTrainTrack parks the locomotive and the
			 * collision/couple handlers perform the physical join. Without this
			 * guard pPrev stays nullptr and the walk-back below dereferences it. */
			return INVALID_TRACKDIR;
		}
		/* R3R: reserve up to the consist's head car (the coupling point) instead of
		 * the far end of the platform - reserving the far end routes the loco through
		 * the consist and extends the reservation past the platform. */
		const TileIndex res_target_tile = (consist_head_tile != INVALID_TILE) ? consist_head_tile : pNode->GetLastTile();
		this->SetReservationTarget(pNode, res_target_tile, pNode->GetLastTrackdir());

		/* Walk through the path back to the origin. */
		Trackdir next_trackdir = INVALID_TRACKDIR;
		Node *pPrev = nullptr;
		TileIndex rp_last_tile = INVALID_TILE;
		/* R3R (修法第三档 S2): 摸路两遍法所需的状态。
		 *
		 * 病根：单段分支最后用 SetReservationTarget(pNode, rp_last_tile, ...)
		 * 把预留目标挂到了**根节点**（= 起点格）上，而 TryReservePath 的循环门槛是
		 * node->parent != nullptr ⇒ 循环零次、返回 true ⇒ "空手报成功"。
		 * 现场证据（build\R3R_debug.log:1631）：
		 *   CPLRES veh=57 origin=16,14 destTile=13,15 node=16,14 root=1 nTry=0 empty=1
		 *
		 * 治法是让"路径"和"目标"同源：rp() 摸路时只记录走过的格（第一遍，只读），
		 * 摸完之后按这份序列逐格预订（第二遍），不再依赖节点链 —— 节点链的 segment
		 * 是跨站台合并的，按它订会把车底整段一起订进去。
		 * 这也顺带避开 KI-504 的陷阱：边摸边订会被自己刚订的轨挡住，现在订的动作
		 * 全部挪到摸完之后。 */
		using R3RCoupleStep = std::pair<TileIndex, Trackdir>;
		std::vector<R3RCoupleStep> r3r_resv_path;
		bool r3r_single_seg = false;   ///< 走的是单段分支（rp() 摸路的那一支）
		uint r3r_hops = 0;
		/* R3R (第 525 轮, KI-690): 回溯段失败不再把整条路径作废，而是"截断到最后一格
		 * 已验证位"—— 这就是原版预留循环的粒度（铺到下一个安全位为止，下一 tick 再试）。
		 * 判据见下面注释；零进展时仍走旧行为。 */
		bool r3r_truncated = false;
		TileIndex r3r_trunc_tile = INVALID_TILE;
		/* R3R (第 525 轮续, KI-691): 安全位判据只能覆盖"本车真的要开过去的那一段"。
		 *
		 * 现场（build\R3R_debug.log:1966-1978，第 525 轮盘）：
		 *   CPL-BACK pPrev=1424,1874 parent=1442,1874     ← YAPF 的 best 落在车底远端
		 *   FSCP tile=1424,1874 skip=multihead-blocked n=2 h=18,75
		 *   CPL-SAFE-FAIL veh=15 tile=1424,1874
		 *   CPL-SAFE-TRUNC veh=15 from=1474,1884 tile=1442,1874
		 *   CPL-RES-SAFE-STOP … tile=1442,1874 booked=42 goal=1
		 * 1424,1874 是**车底自己那一排**的远端（车底长 216 节，18/75 两条链都压在那格），
		 * 本车根本不会开到那里 —— 它要去的只是会合点 `consist_head_tile`（现场 1438,1874）。
		 * 但 best 节点的 segment 是「整段站台跳格」（1442 → 1424 一步跨过），于是
		 * ① 判据在 1424 上失败 ⇒ ② 整条路被截断到 1442（比会合点还靠后 4 格）⇒
		 * ③ 预留就截止在 1442——而那正是岔口 1441（X|LOWER）前一格，不安全、且下一 tick
		 *    ExtendTrainReservation 从 1442 再往前只看见 choice（steps=0），永远长不出去。
		 *
		 * 所以：行驶终点 `r3r_drive_end`（= 会合点）之后的节点一律不参与安全位判定，
		 * 且终点所在节点的 segment 只判到会合点为止。判据从此只回答"我要走的路通不通"，
		 * 不再回答"车底内部能不能站"。 */
		const TileIndex r3r_drive_end = (consist_head_tile != INVALID_TILE) ? consist_head_tile : pNode->GetLastTile();
		Node *r3r_goal_node = nullptr;   ///< 行驶终点所在的节点（比它更深的节点都在会合点之外）
		for (Node *r3r_n = pNode; r3r_n != nullptr; r3r_n = r3r_n->parent) {
			bool r3r_has_end = false;
			r3r_n->template IterateTiles<CYapfReserveTrack<Types>>(v, Yapf(), [&](TileIndex t, Trackdir) -> bool {
				if (t == r3r_drive_end) { r3r_has_end = true; return false; }
				return true;
			});
			if (r3r_has_end) { r3r_goal_node = r3r_n; break; }
		}
		bool r3r_goal_passed = (r3r_goal_node == nullptr);
		while (pNode->parent != nullptr) {
			pPrev = pNode;
			pNode = pNode->parent;
			++r3r_hops;

			if (!r3r_goal_passed) {
				/* 比行驶终点更深的节点（车底内部/站台另一侧）：不判、也不截断。 */
				if (pPrev != r3r_goal_node) continue;
				r3r_goal_passed = true;
			}
			/* 终点所在节点只判到会合点为止：会合点之后是车底自己的位置，与本次行驶无关。 */
			TileIndex r3r_fail_tile = INVALID_TILE;
			if (!this->CheckSafePositionOnNodeTo(pPrev, r3r_drive_end, &r3r_fail_tile)) {
				/* R3R (第 525 轮, KI-690, 修法 2): 记下"这一段的上一格"作为截断位，继续回溯。
				 * 回溯顺序是 目标→起点，所以更靠起点的失败会覆盖成更近的截断位 ——
				 * 最终得到的就是"从起点往前走能走到的最后一格已验证位"。
				 *
				 * 旧行为（KI-516）是整条路径作废 + 标记"被第三方挡住" + 原地等下一 tick。
				 * 它与原版预留循环的差别是致命的：原版遇到挡路只退回本次这一小段，车照样
				 * 开到能到的地方；旧行为是一格都不铺，车原地不动（本现场 `CPL-SAFE-FAIL
				 * veh=15 tile=1442,1874` 之后 wait 一路涨到 62）。
				 *
				 * 截断位等于起点（一格都过不去）时仍走旧行为，见循环后的判据。 */
				r3r_truncated = true;
				r3r_trunc_tile = pNode->GetLastTile();
			}
		}

		if (r3r_truncated && (r3r_trunc_tile == INVALID_TILE || r3r_trunc_tile == pNode->GetLastTile())) {
			/* 一格可走都没有：维持 KI-516 的"原地等"，不要降级去绕别的股道。 */
			_r3r_couple_path_blocked = true;
			return INVALID_TRACKDIR;
		}
		

		next_trackdir = pPrev->GetTrackdir();
	if (pNode->parent == nullptr && pPrev != nullptr) {
		/* R3R: single-segment couple path. The target node's GetTrackdir() is
		 * the consist platform direction (points away from the loco), so the
		 * departure trackdir would send the loco the wrong way. Trace the rail
		 * network from the start tile to the consist to get the true departure
		 * direction. The walk is read-only since KI-504 / E1b: it no longer books
		 * any track of the path (booking from inside the pathfinder made the loco
		 * mask its own continuation on the next tick - see the note inside rp()). */
		TrackFollower ft(v, Yapf().GetCompatibleRailTypes());
		TileIndex st = pNode->GetLastTile();
		TileIndex tg = (consist_head_tile != INVALID_TILE) ? consist_head_tile : pPrev->GetLastTile();
		/* R3R (第 525 轮, KI-690): 回溯段有失败时，摸路终点收到截断位 —— reach() 与 rp()
		 * 都只走到这里，第二遍就只铺到"能走到的最后一格已验证位"，车开过去停住，
		 * 下一 tick 重算续铺（与原生 PBS 的逐段推进同构）。 */
		if (r3r_truncated) tg = r3r_trunc_tile;
		Trackdir best_td = INVALID_TRACKDIR;
		/* R3R (第 332 轮): best_td 属于哪一格。贪婪扫描取 st（机车自身格），
		 * 采纳 YAPF 首步时取首步格 —— 后者才是 new_tile。 */
		TileIndex best_start = st;
		std::function<bool(TileIndex, Trackdir, int)> reach = [&](TileIndex cur, Trackdir cur_td, int depth) -> bool {
			
			if (depth > 512) return false;
			if (cur == tg) return true;
			/* R3R: never route the couple loco through a depot - but allow the
			 * start tile, which may itself be the depot the loco is leaving
			 * (cur == st only occurs at the first call, at depth 0). */
			if (IsRailDepotTile(cur) && cur != st) return false;
			/* R3R (第 333 轮): 递进前自证 (cur, cur_td) 属于同一格 ——
			 * follow_track.hpp:140 的 dbg_assert_tile 对跨格的一对会直接断言崩溃，
			 * 任何调用方（含将来的）都不该把这种对子喂进来。 */
			if (cur_td == INVALID_TRACKDIR ||
					!HasBit(GetTileTrackdirBits(cur, ::TransportType::Rail, 0), cur_td)) {
				return false;
			}
			TrackFollower f2(v, Yapf().GetCompatibleRailTypes());
			if (!f2.Follow(cur, cur_td)) return false;
			/* R3R: TrackFollower leaps a whole platform in one step, so the
			 * consist head (tg) lying on that platform stretch would be skipped.
			 * Reconstruct the exact stretch the follower jumped over (its exit
			 * direction plus tiles_skipped) and treat tg as reached when it lies
			 * on it. Do NOT scan along the platform axis in both directions: any
			 * tile merely sharing the axis line (e.g. the consist standing at a
			 * neighbouring station on the same row) was mistaken for the target,
			 * which handed the walk-back a bogus departure direction pointing
			 * away from the consist. */
			if (f2.is_station && R3RSkippedStationStretchContains(cur, f2.new_tile, f2.exitdir, f2.tiles_skipped, tg)) return true;
			TrackdirBits tdb2 = f2.new_td_bits;
			if (tdb2 == TRACKDIR_BIT_NONE) return false;
			if (KillFirstBit(tdb2) == TRACKDIR_BIT_NONE) {
				return reach(f2.new_tile, FindFirstTrackdir(tdb2), depth + 1);
			}
			for (TrackdirBits tb = tdb2; tb != TRACKDIR_BIT_NONE; tb = KillFirstBit(tb)) {
				if (reach(f2.new_tile, (Trackdir)FindFirstBit(tb), depth + 1)) return true;
			}
			return false;
		};
		TrackdirBits tdb;
		tdb = GetTileTrackdirBits(st, ::TransportType::Rail, 0);

		/* R3R (第 332/333 轮, KI-446 续): ChooseTrainTrack 要的是 new_tile 上的轨，
		 * 也就是 YAPF 路径的**首步**。取首步必须按格配对 —— yapf_node_rail.hpp 的
		 * 两套访问器属于两个不同的格：
		 *   GetTile()     / GetTrackdir()      —— 节点自己那一格（key，同格，见 :85/:88）
		 *   GetLastTile() / GetLastTrackdir()  —— 本节点 segment 被截断的那一格（:157/:163）
		 * 第 332 轮把 GetLastTile() 与 GetTrackdir() 拼在一起，segment 跨多格时等于
		 * 拿 A 格的 trackdir 去 Follow B 格 ⇒ follow_track.hpp:140 的 dbg_assert_tile
		 * 直接崩（玩家 2026-10-27 报的崩溃）；本轮改成 GetTile()，并在交给
		 * TrackFollower 之前自证该格确有这根轨。
		 *
		 * 现场（build\R3R_debug.log）：CPL-BACK pPrev=15,15 td=2 parent=16,14 ptd=0
		 * —— 15,15/td=2 是 pPrev 的 segment **末端**那一对，不是它的 key；而贪婪扫描
		 * 的起点 st=16,14 只有 X（CPL-ORIGIN-TD tdb=0x101 tbits=0x1），能选到的只有
		 * td=0/8 ⇒ TrackdirToTrack=TRACK_X，交回 ChooseTrainTrack 后列车在岔口恒走直行。
		 *
		 * 所以：只要 pPrev 不是目标节点本身（r3r_hops > 1），直接采纳 YAPF 的首步，
		 * 并让 rp() 从首步格起走；只有单跳路径（pPrev 就是目标节点）时，它的
		 * trackdir 是车底的站台方向（背向机车，见下面 R3R 注释），才回落到原来的
		 * 贪婪扫描。校验 reach(first_tile, ...) 失败（例如首步格是车库、被 reach 的
		 * 车库守卫拒掉）时同样回落，保证该分支与改动前逐字一致。 */
		const TileIndex first_tile = pPrev->GetTile();
		const Trackdir yapf_first_td = pPrev->GetTrackdir();
		const TrackdirBits first_tdb = GetTileTrackdirBits(first_tile, ::TransportType::Rail, 0);
		const bool first_td_on_tile = (yapf_first_td != INVALID_TRACKDIR) && HasBit(first_tdb, yapf_first_td);
		const bool yapf_first_ok = (r3r_hops > 1) && first_td_on_tile &&
				(first_tile != st) && reach(first_tile, yapf_first_td, 0);
		if (yapf_first_ok) {
			best_td = yapf_first_td;
			best_start = first_tile;
		} else {
			for (; tdb != TRACKDIR_BIT_NONE; tdb = KillFirstBit(tdb)) {
				Trackdir td = (Trackdir)FindFirstBit(tdb);
				if (reach(st, td, 0)) { best_td = td; break; }
			}
		}
		if (best_td != INVALID_TRACKDIR) {
			next_trackdir = best_td;
			if (!dont_reserve) {
				std::function<bool(TileIndex, Trackdir, int)> rp;
				/* rp_impl = 原来的 rp() 本体；递归调用一律走 rp（带标记回滚的外壳）。 */
				std::function<bool(TileIndex, Trackdir, int)> rp_impl = [&](TileIndex cur, Trackdir cur_td, int depth) -> bool {
					
					/* R3R (KI-504, E1b): rp() is read-only - it deliberately books NOTHING.
					 * Booking from inside the pathfinder turns the loco's own reservation
					 * into its next obstacle: TrackFollower::CanEnterNewTile() removes every
					 * already-reserved track from the enterable set and does not exempt the
					 * booking train itself (follow_track.hpp), so the correct continuation
					 * gets masked and only the wrong branch stays enterable. Live case:
					 * 15,14 had TRACK_X booked, so the loco could no longer go straight on
					 * into 15,15 and was pushed onto the other branch. rp() now only walks
					 * the path to find rp_last_tile; the consist's whole-tile reservation
					 * plus the loco's normal PBS reservation protect the route. E2 replaces
					 * this with ExtendTrainReservation, E4 deletes rp() altogether. */
					if (depth > 512) return false;
					if (cur == tg) {
						/* R3R (KI-684, 第 517 轮, 2026-11-26, E-1): returning here meant the meeting
						 * tile itself never entered r3r_resv_path, so a plain-rail tg was never
						 * booked -- only the station-jump branch below ever pushed tg (KI-569).
						 * Live case: tg = 1439,1874 is a plain PBS signal tile, so 1439 was never
						 * reserved: the loco could not enter the last tile and the couple never
						 * fired (CPL-RES-OK ended at 1440,1874, RESV-SELF-AIR 1439,1874 had zero
						 * lines, only one COUPLE-OK in the whole session).
						 * Book tg like every other tile the loco has to drive into. Station and
						 * depot tiles keep the old rule -- the reservation stops at the platform
						 * and the station branch below books the whole platform in one step.
						 * Safety: if the bit is already ours, TryReserveRailTrack() answers
						 * RESV-SELF-AIR and counts it as a success (pbs.cpp R3RReservationHeldBySelf);
						 * if it is someone else's, the booking fails and leaves CPL-SAFE-FAIL on this
						 * very tile -- which is the diagnostic we want instead of a silent gap. */
						if ((cur != st || depth != 0) && !IsRailStationTile(cur) && !IsRailDepotTile(cur)) {
							r3r_resv_path.push_back(R3RCoupleStep{cur, cur_td});
						}
						return true;
					}
					if (!IsRailStationTile(cur) && !IsRailDepotTile(cur)) {
						/* 只记录物理走过、且需要预订的格：站台/车库不订（与 rp_last_tile
						 * 的口径一致——预留止于会合点，不越过车底）；起点格也不订
						 * （TryReservePath 的节点链同样跳过根节点）。
						 * 唯一例外是下面站台跳格分支补压的 tg 自己（KI-569）。 */
						if (cur != st || depth != 0) r3r_resv_path.push_back(R3RCoupleStep{cur, cur_td});
					}
					/* R3R: do not walk the path through a depot - but allow the start tile,
					 * which may itself be the depot the loco is leaving (cur == st
					 * only occurs at the first call, at depth 0). */
					if (IsRailDepotTile(cur) && cur != st) return false;
					/* R3R (第 333 轮): 同 reach()，Follow 之前自证 (cur, cur_td) 同格。 */
					if (cur_td == INVALID_TRACKDIR ||
							!HasBit(GetTileTrackdirBits(cur, ::TransportType::Rail, 0), cur_td)) {
						return false;
					}
					TrackFollower f3(v, Yapf().GetCompatibleRailTypes());
					if (!f3.Follow(cur, cur_td)) return false;
					if (f3.is_station && f3.tiles_skipped > 0 && R3RSkippedStationStretchContains(cur, f3.new_tile, f3.exitdir, f3.tiles_skipped, tg)) {
						/* R3R (第 400 轮, KI-569): 上面把站台格排除在清单之外，而这条跳格分支
						 * 一旦命中就直接宣布"路线经过 tg"。只认路不铺路 ⇒ 第二遍
						 * R3RReservePathSequence() 无格可订 ⇒ 站台段整段无人预订 ⇒ 机车开到
						 * 站台前一格就无已订格可进，停车后每 tick 从原地重算 ⇒ 自锁
						 * （现场：停在 248,127，248,130~248,133 全空，path=2 booked=2）。
						 * 这里补压 tg 自己一格：ReserveSingleTrack() 对站台格走整段平台预订
						 * （ReserveRailStationPlatform() 从 tg 沿回退方向逐格置位到平台头），
						 * 一格即覆盖整段；若改成逐格压，第二格会撞上自己刚订的位而失败。 */
						if (cur != tg) r3r_resv_path.push_back(R3RCoupleStep{tg, cur_td});
						return true;
					}
					TrackdirBits tdb3 = f3.new_td_bits;
					if (tdb3 == TRACKDIR_BIT_NONE) return false;
					if (KillFirstBit(tdb3) == TRACKDIR_BIT_NONE) {
						const Trackdir nd = FindFirstTrackdir(tdb3);
						if (rp(f3.new_tile, nd, depth + 1)) {
							return true;
						}
						return false;
					}
					for (TrackdirBits tb = tdb3; tb != TRACKDIR_BIT_NONE; tb = KillFirstBit(tb)) {
						const Trackdir nd = (Trackdir)FindFirstBit(tb);
						if (rp(f3.new_tile, nd, depth + 1)) {
							return true;
						}
					}
					return false;
				};
				/* R3R (第 332 轮): 从 best_td 真正所属的那一格开始走，否则
				 * Follow(first_tile 上的 trackdir) 会在 st 上直接失败，白走一趟。 */
				rp = [&](TileIndex cur, Trackdir cur_td, int depth) -> bool {
					/* 失败即回滚本次尝试记录下来的格，保证 r3r_resv_path 里只剩
					 * "最终成功的那条路线" —— 否则失败分支的格会被一起预订。 */
					const size_t r3r_mark = r3r_resv_path.size();
					if (!rp_impl(cur, cur_td, depth)) {
						r3r_resv_path.resize(r3r_mark);
						return false;
					}
					return true;
				};
				r3r_single_seg = true;
				rp(best_start, best_td, 0);
				if (rp_last_tile != INVALID_TILE) {
					this->SetReservationTarget(pNode, rp_last_tile, pNode->GetLastTrackdir());
				}
			}
		} else {
			/* R3R: no reachable departure direction (the only path went through the depot) - wait. */
			next_trackdir = INVALID_TRACKDIR;
		}
	}
		if (!dont_reserve) {
			bool reserved = false;
		size_t r3r_booked = 0;
		if (r3r_single_seg) {
			/* R3R (修法第三档 S2): 单段分支改走"两遍法" —— 目标与路径同源。
			 *
			 * 摸路（第一遍，只读）已经给出物理路线 r3r_resv_path，例如
			 *   15,14 → 15,15 → 14,15 → 13,15（止于会合点，不越过车底）。
			 * 这里按序逐格预订（第二遍）。按 KI-563 的原生粒度口径：订到第一个
			 * 失败格即停、保留已订格；只有一格都没订上时才如实返回失败，交
			 * train_cmd.cpp 的 KI-316 路径（FreeTrainTrackReservation +
			 * MarkTrainAsStuck）处置；绝不再出现 "reserved=1 而 nTry=0" 的空真。
			 */
			if (!r3r_resv_path.empty()) {
				const PBSTileInfo origin = PBSTileInfo(v->tile, v->GetVehicleTrackdir(), false);
				/* R3R (第 525 轮续, KI-692)：第一段预留必须铺到**机车脚下那一格**。
				 *
				 * 玩家 2026-11-27 原话：「veh15 的第一段预留应该到 1457,1874 截止，但它实际
				 * 到了 1456,1874 才截止」。现场（build\R3R_debug.log:3033）：
				 *   CPL-FIRST-TD st=1457,1874 … at=1456,1874 … start=1456,1874
				 *   CPL-RES-OK … tiles=1456,1874/0;1455,1874/0;…
				 * `st` 就是机车脚下格，而 `rp()` 从 YAPF 首步格（1456）起步 ——
				 * rp_impl() 里"跳过起点格"的写法是 `cur != st || depth != 0`，只有 `rp()`
				 * 从 `st` 起步时才生效；从首步格起步时 `st` 根本没被访问，跳过变成整格丢失。
				 *
				 * 补法：把 `{origin.tile, origin.trackdir}`（机车自身格 + 自身轨向）先尽力订
				 * 一次，订上了才插到清单头。订不上（例如该格交叉轨被别人占）就不插，绝不
				 * 让整条序列在第一步失败。插进来之后第二遍会以 RESV-SELF-AIR 通过，不影响
				 * booked 的语义（它本来就是自己压着的格）。 */
				if (r3r_resv_path.front().first != origin.tile &&
						origin.trackdir != INVALID_TRACKDIR &&
						!IsRailStationTile(origin.tile) && !IsRailDepotTile(origin.tile) &&
						TryReserveRailTrackdir(v, origin.tile, origin.trackdir)) {
					r3r_resv_path.insert(r3r_resv_path.begin(), R3RCoupleStep{origin.tile, origin.trackdir});
				}
				/* R3R (第 525 轮续, KI-691)：被截断的路径必须收在**安全位**上。
				 *
				 * 玩家 2026-11-27 原话：「veh15 的预留在不安全的位置截止，不安全的截止位
				 * 坐标为 (1442,1874)」。原版 ExtendTrainReservation() 只把预留推进到
				 * 下一个 IsSafeWaitingPosition 为止（:17512-17526），所以它的预留端点
				 * 永远是安全位；挂接这条被截断的路则是"能走到哪儿算哪儿"，端点落到了
				 * 岔口 1441（X|LOWER）的**前一格**，既不能停、下一 tick 也再长不出去
				 * （Extend 从 1442 只看见 choice，steps=0）。
				 *
				 * 语义与原生同构：截断时把清单收到路径上**最后一个安全位**（含）；
				 * 路径上根本没有安全位就一格都不订，交回上层"原地等"。
				 * 未截断（正常走到会合点）时不参与本判定 —— 会合点本身就是终点。 */
				if (r3r_truncated) {
					size_t r3r_safe_len = 0;
					for (size_t i = 0; i < r3r_resv_path.size(); i++) {
						if (IsSafeWaitingPosition(v, r3r_resv_path[i].first, r3r_resv_path[i].second, true, !TrackFollower::Allow90degTurns())) {
							r3r_safe_len = i + 1;
						}
					}
					if (r3r_safe_len < r3r_resv_path.size()) {
						r3r_resv_path.resize(r3r_safe_len);
					}
					if (r3r_resv_path.empty()) {
						/* 一路没有安全位：不铺任何预留（绝不铺到不安全位置），原地等。 */
						_r3r_couple_path_blocked = true;
					}
				}
			}
			if (!r3r_resv_path.empty()) {
				const R3RCoupleStep &r3r_last = r3r_resv_path.back();
				this->SetReservationTarget(pNode, r3r_last.first, r3r_last.second);
				reserved = this->R3RReservePathSequence(r3r_resv_path, r3r_booked);
				/* KI-563：部分预留时把预留终点收到"最后一格已订"，否则后续逻辑
				 * 以为整条到会合点都在自己名下。原生 PBS 的预留终点本来就是
				 * 逐段推进的。 */
				if (reserved && r3r_booked > 0 && r3r_booked < r3r_resv_path.size()) {
					const R3RCoupleStep &r3r_stop = r3r_resv_path[r3r_booked - 1];
					this->SetReservationTarget(pNode, r3r_stop.first, r3r_stop.second);
				}
			}
			/* 摸路一格都没给出 ⇒ 没有可订的东西，直接报失败（不许空手成功）。 */
		} else {
			/* R3R (第 503 轮): 挂接摸路也走"全有或全无"。原来这里传 unsafe_pos=true，
			 * 即"不查终点、格级失败不回滚、答案本不参与"；现在终点交给
			 * pbs.cpp 的 partner-air 豁免放行（check_target_free=true），格级失败
			 * 整段回滚（atomic=true），答案本仍不参与（record_answer=false）。 */
			reserved = this->TryReservePath(nullptr, pNode->GetLastTile(),
					/*atomic*/ true, /*check_target_free*/ true, /*record_answer*/ false);
		}
			return reserved ? next_trackdir : INVALID_TRACKDIR;
		}

		return next_trackdir;
	}
};

template <class Types>
class CYapfFollowAnySafeTileRailT : public CYapfReserveTrack<Types> {
public:
	typedef typename Types::Tpf Tpf; ///< the pathfinder class (derived from THIS class)
	typedef typename Types::TrackFollower TrackFollower;
	typedef typename Types::NodeList::Item Node; ///< this will be our node type
	typedef typename Node::Key Key; ///< key to hash tables

protected:
	/** @copydoc CYapfBaseT::Yapf */
	inline Tpf &Yapf()
	{
		return *static_cast<Tpf *>(this);
	}

public:
	/** @copydoc CYapfBaseT::PfFollowNodeFunc */
	inline void PfFollowNode(Node &old_node)
	{
		TrackFollower follower{Yapf().GetVehicle(), Yapf().GetCompatibleRailTypes()};
		if (follower.Follow(old_node.GetLastTile(), old_node.GetLastTrackdir()) && follower.MaskReservedTracks()) {
			Yapf().AddMultipleNodes(&old_node, follower);
		}
	}

	/** @copydoc CYapfBaseT::TransportTypeCharFunc */
	inline char TransportTypeChar() const
	{
		return 't';
	}

	static bool stFindNearestSafeTile(const Train *v, TileIndex t1, Trackdir td, bool override_railtype)
	{
		/* Create pathfinder instance */
		Tpf pf1;
		bool result1;
		if (GetDebugLevel(DebugLevelID::yapfdesync) < 1 && GetDebugLevel(DebugLevelID::desync) < 2) {
			result1 = pf1.FindNearestSafeTile(v, t1, td, override_railtype, false);
		} else {
			bool found_path_1, found_path_2;
			pf1.FindNearestSafeTile(v, t1, td, override_railtype, true, &found_path_1);
			Tpf pf2;
			pf2.DisableCache(true);
			result1 = pf2.FindNearestSafeTile(v, t1, td, override_railtype, false, &found_path_2);
			if (found_path_1 != found_path_2) {
				Debug(desync, 0, "CACHE ERROR: FindSafeTile() = [{}, {}]", found_path_1 ? "T" : "F", found_path_2 ? "T" : "F");
				DumpState(pf1, pf2);
			} else if (found_path_2) {
				CYapfFollowAnySafeTileRailT::stDesyncCheck(pf1, pf2, "CACHE ERROR: FindSafeTile()", true);
			}
		}

		return result1;
	}

	bool FindNearestSafeTile(const Train *v, TileIndex t1, Trackdir td, bool override_railtype, bool dont_reserve, bool *found_path = nullptr)
	{
		/* Set origin and destination. */
		Yapf().SetOrigin(t1, td);
		Yapf().SetTreatFirstRedTwoWaySignalAsEOL(true);
		Yapf().SetDestination(v, override_railtype);

		bool bFound = Yapf().FindPath(v);
		if (found_path) *found_path = bFound;
		if (!bFound) return false;

		/* Found a destination, set as reservation target. */
		Node *node = Yapf().GetBestNode();
		this->SetReservationTarget(node, node->GetLastTile(), node->GetLastTrackdir());

		/* Walk through the path back to the origin. */
		auto &safe_position_nodes = CYapfReserveTrackSafePositionNodes<Node>::safe_position_nodes;
		safe_position_nodes.clear();
		Node *prev = nullptr;
		while (node->parent != nullptr) {
			prev = node;
			node = node->parent;

			safe_position_nodes.push_back(prev);
		}
		this->FindSafePositionOnSafePositionNodes();

		return dont_reserve || this->TryReservePath(nullptr, node->GetLastTile());
	}
};

template <class Types>
class CYapfFollowRailT : public CYapfReserveTrack<Types> {
public:
	typedef typename Types::Tpf Tpf; ///< the pathfinder class (derived from THIS class)
	typedef typename Types::TrackFollower TrackFollower;
	typedef typename Types::NodeList::Item Node; ///< this will be our node type
	typedef typename Node::Key Key; ///< key to hash tables

protected:
	/** @copydoc CYapfBaseT::Yapf */
	inline Tpf &Yapf()
	{
		return *static_cast<Tpf *>(this);
	}

public:
	/** @copydoc CYapfBaseT::PfFollowNodeFunc */
	inline void PfFollowNode(Node &old_node)
	{
		const Train *v = Yapf().GetVehicle();
		TrackFollower follower(v);
		if (old_node.flags_u.flags_s.reverse_pending && old_node.segment->end_segment_reason.Any({EndSegmentReason::SafeTile, EndSegmentReason::Depot, EndSegmentReason::DeadEnd})) {
			Node *rev_node = &old_node;
			uint length = 0;
			while (rev_node != nullptr && !rev_node->segment->end_segment_reason.Test(EndSegmentReason::Reverse)) {
				length += rev_node->GetNodeLength(v, Yapf(), *this);
				rev_node = rev_node->parent;
			}
			if (rev_node != nullptr && length >= v->gcache.cached_total_length) {
				if (follower.Follow(rev_node->GetLastTile(), ReverseTrackdir(rev_node->GetLastTrackdir()))) {
					Yapf().AddMultipleNodes(&old_node, follower, [&](Node &n) {
						n.flags_u.flags_s.reverse_pending = false;
						n.flags_u.flags_s.teleport = true;
					});
				}
				return;
			} else if (old_node.segment->end_segment_reason.Any({EndSegmentReason::Depot, EndSegmentReason::DeadEnd})) {
				return;
			}
		}
		if (follower.Follow(old_node.GetLastTile(), old_node.GetLastTrackdir())) {
			Yapf().AddMultipleNodes(&old_node, follower);
		}
	}

	/** @copydoc CYapfBaseT::TransportTypeCharFunc */
	inline char TransportTypeChar() const
	{
		return 't';
	}

	static Trackdir stChooseRailTrack(const Train *v, TileIndex tile, DiagDirection enterdir, TrackBits tracks, bool &path_found, bool reserve_track, PBSTileInfo *target, TileIndex *dest)
	{
		/* create pathfinder instance */
		Tpf pf1;
		Trackdir result1;

		if (GetDebugLevel(DebugLevelID::yapfdesync) < 1 && GetDebugLevel(DebugLevelID::desync) < 2) {
			result1 = pf1.ChooseRailTrack(v, tile, enterdir, tracks, path_found, reserve_track, target, dest);
		} else {
			result1 = pf1.ChooseRailTrack(v, tile, enterdir, tracks, path_found, false, nullptr, nullptr);
			Tpf pf2;
			pf2.DisableCache(true);
			Trackdir result2 = pf2.ChooseRailTrack(v, tile, enterdir, tracks, path_found, reserve_track, target, dest);
			if (result1 != result2) {
				Debug(desync, 0, "CACHE ERROR: ChooseRailTrack() = [{}, {}]", result1, result2);
				DumpState(pf1, pf2);
			} else if (result1 != INVALID_TRACKDIR) {
				CYapfFollowRailT::stDesyncCheck(pf1, pf2, "CACHE ERROR: ChooseRailTrack()", true);
			}
		}

		return result1;
	}

	inline Trackdir ChooseRailTrack(const Train *v, TileIndex, DiagDirection, TrackBits, bool &path_found, bool reserve_track, PBSTileInfo *target, TileIndex *dest)
	{
		if (target != nullptr) target->tile = INVALID_TILE;
		if (dest != nullptr) *dest = INVALID_TILE;

		/* set origin and destination nodes */
		PBSTileInfo origin;
		if (v->current_order.IsType(OT_GOTO_COUPLE) && !v->current_order.GetCoupleIsDepot()) {
			/* R3R: when coupling, start the path from the loco itself.
			 * FollowTrainReservation would walk the consist's platform reservation up
			 * to its far end, and the path would then extend past the platform.
			 * Depot-type coupling (GetCoupleIsDepot()) must NOT take this shortcut:
			 * it drives to a depot exactly like OT_GOTO_DEPOT, and pinning the origin
			 * to the vehicle's own tile (which is the depot tile when parked inside)
			 * makes the safe-position back-walk fail, so no reservation is ever made. */
			origin = PBSTileInfo(v->tile, v->GetVehicleTrackdir(), false);
		} else {
			origin = FollowTrainReservation(v, nullptr, FollowTrainReservationFlag::OkayUnused);
		}
		/* R3R (答案本): 先试照抄上一次的路径，铺成功即不进 YAPF。 */
		if (reserve_track) {
			Trackdir answer_td = INVALID_TRACKDIR;
			if (this->R3RAnswerReplay(v, origin, target, dest, &answer_td)) {
				path_found = true;
				return answer_td;
			}
		}

		Yapf().SetOrigin(origin.tile, origin.trackdir, INVALID_TILE, INVALID_TRACKDIR, 1);
		Yapf().SetTreatFirstRedTwoWaySignalAsEOL(true);
		Yapf().SetDestination(v);

		/* find the best path */
		path_found = Yapf().FindPath(v);

		/* if path not found - return INVALID_TRACKDIR */
		Trackdir next_trackdir = INVALID_TRACKDIR;
		Node *node = Yapf().GetBestNode();
		if (node != nullptr) {
			/* reserve till end of path */
			this->SetReservationTarget(node, node->GetLastTile(), node->GetLastTrackdir());

			/* path was found or at least suggested
			 * walk through the path back to the origin */
			auto &safe_position_nodes = CYapfReserveTrackSafePositionNodes<Node>::safe_position_nodes;
			safe_position_nodes.clear();
			Node *prev = nullptr;
			while (node->parent != nullptr) {
				prev = node;
				node = node->parent;

				safe_position_nodes.push_back(prev);
			}
			this->FindSafePositionOnSafePositionNodes();

			/* If the best PF node has no parent, then there is no (valid) best next trackdir to return.
			 * This occurs when the PF is called while the train is already at its destination. */
			if (prev == nullptr) return INVALID_TRACKDIR;

			/* return trackdir from the best origin node (one of start nodes) */
			Node &best_next_node = *prev;
			next_trackdir = best_next_node.GetTrackdir();

			if (reserve_track && path_found) {
				if (dest != nullptr) *dest = Yapf().GetBestNode()->GetLastTile();
				if (this->TryReservePath(target, node->GetLastTile())) {
					/* R3R (答案本): 本次路径已逐格铺好，收进答案本供下一次照抄。 */
					R3RAnswerCommit(v->index, v->dest_tile);
				}
			}
		}

		/* Treat the path as found if stopped on the first two way signal(s). */
		path_found |= Yapf().stopped_on_first_two_way_signal;
		return next_trackdir;
	}

	static bool stCheckReverseTrain(const Train *v, TileIndex t1, Trackdir td1, TileIndex t2, Trackdir td2, int reverse_penalty, TraceRestrictProgramInputFlags tracerestrict_flags)
	{
		Tpf pf1;
		bool result1 = pf1.CheckReverseTrain(v, t1, td1, t2, td2, reverse_penalty, tracerestrict_flags);

		if (GetDebugLevel(DebugLevelID::yapfdesync) > 0 || GetDebugLevel(DebugLevelID::desync) >= 2) {
			Tpf pf2;
			pf2.DisableCache(true);
			bool result2 = pf2.CheckReverseTrain(v, t1, td1, t2, td2, reverse_penalty, tracerestrict_flags);
			if (result1 != result2) {
				Debug(desync, 2, "CACHE ERROR: CheckReverseTrain() = [{}, {}]", result1 ? "T" : "F", result2 ? "T" : "F");
				DumpState(pf1, pf2);
			} else if (result1) {
				CYapfFollowRailT::stDesyncCheck(pf1, pf2, "CACHE ERROR: CheckReverseTrain()", false);
			}
		}

		return result1;
	}

	inline bool CheckReverseTrain(const Train *v, TileIndex t1, Trackdir td1, TileIndex t2, Trackdir td2, int reverse_penalty, TraceRestrictProgramInputFlags tracerestrict_flags)
	{
		/* create pathfinder instance
		 * set origin and destination nodes */
		Yapf().SetOrigin(t1, td1, t2, td2, reverse_penalty);
		Yapf().SetTreatFirstRedTwoWaySignalAsEOL(false);
		Yapf().SetTraceRestrictProgramInputFlags(tracerestrict_flags);
		Yapf().SetDestination(v);

		/* find the best path */
		if (!Yapf().FindPath(v)) return false;

		/* path was found
		 * walk through the path back to the origin */
		Node *node = Yapf().GetBestNode();
		while (node->parent != nullptr) {
			node = node->parent;
		}

		/* check if it was reversed origin */
		bool reversed = (node->cost != 0);
		return reversed;
	}
};

template <class Tpf_, class Ttrack_follower, template <class Types> class TdestinationT, template <class Types> class TfollowT>
struct CYapfRail_TypesT {
	typedef CYapfRail_TypesT<Tpf_, Ttrack_follower, TdestinationT, TfollowT>  Types;

	typedef Tpf_                                Tpf;
	typedef Ttrack_follower                     TrackFollower;
	typedef CRailNodeList                       NodeList;
	typedef Train                               VehicleType;
	typedef CYapfBaseT<Types>                   PfBase;
	typedef TfollowT<Types>                     PfFollow;
	typedef CYapfOriginTileTwoWayT<Types>       PfOrigin;
	typedef TdestinationT<Types>                PfDestination;
	typedef CYapfSegmentCostCacheGlobalT<Types> PfCache;
	typedef CYapfCostRailT<Types>               PfCost;
};

template <typename Types>
struct CYapfRailBase : CYapfT<Types> {
	typedef typename Types::NodeList::Item Node;

	/**
	 * In some cases an intermediate node branch should be pruned.
	 * The most prominent case is when a red EOL signal is encountered, but
	 * there was a segment change (e.g. a rail type change) before that. If
	 * the branch would not be pruned, the rail type change location would
	 * remain the best intermediate node, and thus the vehicle would still
	 * go towards the red EOL signal.
	 * @param n The node to start pruning at.
	 */
	void PruneIntermediateNodeBranch(Node *n)
	{
		bool intermediate_on_branch = false;
		while (n != nullptr && !n->segment->end_segment_reason.Test(EndSegmentReason::ChoiceFollows)) {
			if (n == this->best_intermediate_node) intermediate_on_branch = true;
			n = n->parent;
		}
		if (intermediate_on_branch) this->best_intermediate_node = n;
	}
};

struct CYapfRail         : CYapfRailBase<CYapfRail_TypesT<CYapfRail        , CFollowTrackRail    , CYapfDestinationTileOrStationRailT, CYapfFollowRailT>> {};
struct CYapfRailNo90     : CYapfRailBase<CYapfRail_TypesT<CYapfRailNo90    , CFollowTrackRailNo90, CYapfDestinationTileOrStationRailT, CYapfFollowRailT>> {};

struct CYapfAnyDepotRail     : CYapfRailBase<CYapfRail_TypesT<CYapfAnyDepotRail,     CFollowTrackRail    , CYapfDestinationAnyDepotRailT     , CYapfFollowAnyDepotRailT>> {};
struct CYapfAnyDepotRailNo90 : CYapfRailBase<CYapfRail_TypesT<CYapfAnyDepotRailNo90, CFollowTrackRailNo90, CYapfDestinationAnyDepotRailT     , CYapfFollowAnyDepotRailT>> {};

struct CYapfCoupleRail     : CYapfRailBase<CYapfRail_TypesT<CYapfCoupleRail,     CFollowTrackRail    , CYapfDestinationTrainRailT, CYapfFollowCoupleRailT>> {};
struct CYapfCoupleRailNo90 : CYapfRailBase<CYapfRail_TypesT<CYapfCoupleRailNo90, CFollowTrackRailNo90, CYapfDestinationTrainRailT, CYapfFollowCoupleRailT>> {};

struct CYapfAnySafeTileRail     : CYapfRailBase<CYapfRail_TypesT<CYapfAnySafeTileRail    , CFollowTrackFreeRail    , CYapfDestinationAnySafeTileRailT , CYapfFollowAnySafeTileRailT>> {};
struct CYapfAnySafeTileRailNo90 : CYapfRailBase<CYapfRail_TypesT<CYapfAnySafeTileRailNo90, CFollowTrackFreeRailNo90, CYapfDestinationAnySafeTileRailT , CYapfFollowAnySafeTileRailT>> {};


Track YapfTrainChooseTrack(const Train *v, TileIndex tile, DiagDirection enterdir, TrackBits tracks, bool &path_found, bool reserve_track, PBSTileInfo *target, TileIndex *dest)
{
	Trackdir td_ret = _settings_game.pf.forbid_90_deg
		? CYapfRailNo90::stChooseRailTrack(v, tile, enterdir, tracks, path_found, reserve_track, target, dest)
		: CYapfRail::stChooseRailTrack(v, tile, enterdir, tracks, path_found, reserve_track, target, dest);

	return (td_ret != INVALID_TRACKDIR) ? TrackdirToTrack(td_ret) : FindFirstTrack(tracks);
}

static bool YapfTrainCheckReverse(const Train *v, TileIndex tile, Trackdir td, TileIndex tile_rev, Trackdir td_rev, int reverse_penalty)
{
	TraceRestrictProgramInputFlags input_flags{};
	bool swapped = false;
	if (reverse_penalty < 0) {
		reverse_penalty = -reverse_penalty;
		swapped = true;
		input_flags.Set(TraceRestrictProgramInputFlag::InvertDrivingDirection);

		std::swap(tile, tile_rev);
		std::swap(td, td_rev);
	} else if (reverse_penalty == 0) {
		/* slightly hackish: If the pathfinders finds a path, the cost of the first node is tested to distinguish between forward- and reverse-path. */
		reverse_penalty = 1;
	}

	bool reverse = _settings_game.pf.forbid_90_deg
		? CYapfRailNo90::stCheckReverseTrain(v, tile, td, tile_rev, td_rev, reverse_penalty, input_flags)
		: CYapfRail::stCheckReverseTrain(v, tile, td, tile_rev, td_rev, reverse_penalty, input_flags);

	return reverse != swapped;
}

constexpr int DRIVING_BACKWARDS_PENALTY = 100 * YAPF_TILE_LENGTH;

bool YapfTrainCheckReverse(const Train *v)
{
	const Train *moving_front = v->GetMovingFront();
	const Train *moving_back = v->GetMovingBack();

	/* get trackdirs of both ends */
	Trackdir td = moving_front->GetVehicleTrackdir();
	Trackdir td_back = moving_back->GetVehicleTrackdir();

	/* R3R (KI-107): GetVehicleTrackdir() can legitimately return INVALID_TRACKDIR
	 * without asserting internally in two cases: (a) the vehicle is flagged
	 * crashed, and (b) it sits in a tunnel/bridge (track == TRACK_BIT_WORMHOLE)
	 * whose across-tunnel/bridge track bits cannot be resolved (pre-existing
	 * upstream guard) or whose track does not match the bridge exit direction
	 * (TrackExitdirToTrackdir returns INVALID_TRACKDIR). Every other "no track
	 * bits" case used to assert inside TrackDirectionToTrackdir(), so it is not
	 * what the crash report shows. CheckReverseTrain() only guards the FRONT end
	 * (moving_front->track != TRACK_BIT_NONE) and the original code fed
	 * moving_back->GetVehicleTrackdir() straight into ReverseTrackdir(), which
	 * asserts on INVALID_TRACKDIR (track_func.h:249) and killed the game right
	 * after Couple(). Never suggest reversing unless both ends have a usable
	 * trackdir, and log the state so the true source can be pinned down. */
	if (unlikely(td == INVALID_TRACKDIR || td_back == INVALID_TRACKDIR ||
			moving_front->track == TRACK_BIT_NONE || moving_back->track == TRACK_BIT_NONE)) {
		return false;
	}

	Trackdir td_rev = ReverseTrackdir(td_back);

	/* tiles where front and back are */
	TileIndex tile = moving_front->tile;
	TileIndex tile_rev = moving_back->tile;

	/* R3R: after a nose-to-nose coupling the consist can sit spatially folded -
	 * its "back" vehicle is physically in FRONT of the locomotive (several
	 * vehicles share one tile). The reverse heuristic compares the forward path
	 * from the front origin with the backward path from the back origin; a
	 * folded chain misleads it into recommending a flip onto a heading that
	 * cannot reach the destination (REVERSEDIR then CRT found=0, train stranded).
	 * Detect the fold: the back wagon lies ahead of the front vehicle along the
	 * front vehicle's *travelling* direction -> never suggest reversing in that state.
	 *
	 * R3R (KI-242, 2026-09-28): the yardstick must be the direction the chain
	 * TRAVELS (GetMovingDirection(), i.e. reverse(direction) while
	 * DrivingBackwards), not the raw `direction`. With db=1 the chain order is
	 * inverted relative to `direction`, so the raw value reads a perfectly
	 * healthy backward-driving consist as folded and vetoes the very reversal
	 * that would extricate it.
	 *
	 * Live report (build/R3R_debug.log, "1917行附近"): loco 27 was decoupled out
	 * of a consist at station 60,2x and left as chain 27(60,21) -> 28(60,20) ->
	 * 29(60,20) with db=1 (so its moving front is its tail 29 at 60,20 and it
	 * travels NORTH), while its next order's destination was tile 60,31, i.e.
	 * SOUTH, and the released consist 0..5 stood parked on 60,20/60,19 to the
	 * north holding their own reservations. North was therefore blocked, and the
	 * only way out was to reverse (flip DrivingBackwards,
	 * train_flip_reverse_allowed = none) and leave through the south end - but
	 * this test vetoed it: "CRT-FOLD veh=27 tile=60,20 dir=3 backTile=60,21
	 * rel=0,1 dot=1" followed by ~100 lines of "TRP veh=27 ... spd=0 => ok=0
	 * res=0" (no path, no reservation, train frozen) until the unrelated
	 * "reverse at signals" stuck-timer finally turned it around.
	 *
	 * `direction` 3 (SE) = tile delta (0,+1); rel = tail 27 - head 29 = (0,+1),
	 * dot = +1 -> vetoed. Measured against the travelled direction 7 (NW) =
	 * (0,-1) the same pair is dot = -1, i.e. the tail is properly *behind* the
	 * front, which is what it is. Genuinely folded chains are reached with db=0
	 * (Couple() normalises the flag), so their verdict is unchanged; this only
	 * stops the false positive. Identical reasoning to KI-173, which already
	 * switched R3RCheckChainFoldedDirection() (train_cmd.cpp) to the moving
	 * direction. */
	if (moving_front->track != TRACK_BIT_DEPOT && moving_back->track != TRACK_BIT_DEPOT) {
		const Direction front_dir = moving_front->GetMovingDirection();
		const TileIndexDiffC delta = TileIndexDiffCByDir(front_dir);
		const int rel_x = TileX(tile_rev) - TileX(tile);
		const int rel_y = TileY(tile_rev) - TileY(tile);
		if (rel_x * delta.x + rel_y * delta.y > 0) {
			return false;
		}
	}
	int reverse_penalty = 0;

	/* Consider whether the train might back up at reduced speed. */
	if (_settings_game.difficulty.train_flip_reverse_allowed == TrainFlipReversingAllowed::None && !v->Last()->CanLeadTrain()
			&& moving_front->track != TRACK_BIT_DEPOT && moving_back->track != TRACK_BIT_DEPOT) {
		if (!v->vehicle_flags.Test(VehicleFlag::DrivingBackwards)) {
			/* We're currently driving forwards at full speed, and would rather not reverse if possible. */
			reverse_penalty += DRIVING_BACKWARDS_PENALTY;
		} else {
			/* We're currently driving backwards slowly, prefer reversing. */
			reverse_penalty -= DRIVING_BACKWARDS_PENALTY;
		}
	}

	if (moving_front->track & TRACK_BIT_WORMHOLE) {
		/* front in tunnel / on bridge */
		DiagDirection dir_into_wormhole = GetTunnelBridgeDirection(tile);

		/* Current position of the train in the wormhole */
		TileIndex cur_tile = TileVirtXY(moving_front->x_pos, moving_front->y_pos);

		/* Add distance to drive in the wormhole as penalty for the forward path, i.e. bonus for the reverse path
		 * Note: Negative penalties are ok for the start tile. */
		if (TrackdirToExitdir(td) == dir_into_wormhole) {
			reverse_penalty += DistanceManhattan(cur_tile, tile) * YAPF_TILE_LENGTH;
		} else {
			reverse_penalty -= DistanceManhattan(cur_tile, tile) * YAPF_TILE_LENGTH;
		}
	}

	if (moving_back->track & TRACK_BIT_WORMHOLE) {
		/* back in tunnel / on bridge */
		DiagDirection dir_into_wormhole = GetTunnelBridgeDirection(tile_rev);

		/* Current position of the last wagon in the wormhole */
		TileIndex cur_tile = TileVirtXY(moving_back->x_pos, moving_back->y_pos);

		/* Add distance to drive in the wormhole as penalty for the revere path. */
		if (TrackdirToExitdir(td_rev) == dir_into_wormhole) {
			reverse_penalty -= DistanceManhattan(cur_tile, tile_rev) * YAPF_TILE_LENGTH;
		} else {
			reverse_penalty += DistanceManhattan(cur_tile, tile_rev) * YAPF_TILE_LENGTH;
		}
	}

	return YapfTrainCheckReverse(v, tile, td, tile_rev, td_rev, reverse_penalty);
}

bool YapfTrainCheckDepotReverse(const Train *v, TileIndex forward_depot, TileIndex reverse_depot)
{
	int reverse_penalty = 1;
	if (_settings_game.difficulty.train_flip_reverse_allowed == TrainFlipReversingAllowed::None && !v->Last()->CanLeadTrain()) {
		/* Apply penalties to prefer driving out in forward direction. */
		if (v->vehicle_flags.Test(VehicleFlag::DrivingBackwards)) {
			/* Prefer reverse depot. */
			reverse_penalty = -DRIVING_BACKWARDS_PENALTY;
		} else {
			/* Prefer forward depot. */
			reverse_penalty = DRIVING_BACKWARDS_PENALTY;
		}
	}

	return YapfTrainCheckReverse(v, forward_depot, DiagDirToDiagTrackdir(GetRailDepotDirection(forward_depot)),
			reverse_depot, DiagDirToDiagTrackdir(GetRailDepotDirection(reverse_depot)), reverse_penalty);
}

FindDepotData YapfTrainFindNearestDepot(const Train *v, int max_penalty)
{
	const Train *moving_back = v->GetMovingBack();

	PBSTileInfo origin = FollowTrainReservation(v, nullptr, FollowTrainReservationFlag::OkayUnused);
	TileIndex last_tile = moving_back->tile;
	/* R3R (KI-107): never feed an invalid trackdir into ReverseTrackdir()
	 * (assertion at track_func.h:249); see YapfTrainCheckReverse(). A chain end
	 * without trackdir cannot start a two-way depot search. */
	const Trackdir td_back = moving_back->GetVehicleTrackdir();
	if (unlikely(td_back == INVALID_TRACKDIR)) return FindDepotData();
	Trackdir td_rev = ReverseTrackdir(td_back);

	return _settings_game.pf.forbid_90_deg
		? CYapfAnyDepotRailNo90::stFindNearestDepotTwoWay(v, origin.tile, origin.trackdir, last_tile, td_rev, max_penalty, YAPF_INFINITE_PENALTY)
		: CYapfAnyDepotRail::stFindNearestDepotTwoWay(v, origin.tile, origin.trackdir, last_tile, td_rev, max_penalty, YAPF_INFINITE_PENALTY);
}

bool YapfTrainFindNearestSafeTile(const Train *v, TileIndex tile, Trackdir td, bool override_railtype)
{
	return _settings_game.pf.forbid_90_deg
		? CYapfAnySafeTileRailNo90::stFindNearestSafeTile(v, tile, td, override_railtype)
		: CYapfAnySafeTileRail::stFindNearestSafeTile(v, tile, td, override_railtype);
}

/**
 * Find the track to take when approaching a waiting consist to couple with.
 * @param v The train to find a track for.
 * @param dont_reserve Whether to skip making a reservation.
 * @return The track to take, or #INVALID_TRACK if no path was found.
 */
/**
 * R3R (KI-190): throttle the "no path" couple scan.
 *
 * A locomotive holding a GOTO_COUPLE order whose consist has not turned up (or
 * whose one candidate is filtered out by the couple whitelist) used to run the
 * full-map couple pathfinder on every single tick: YAPF walks the whole reachable
 * network, which the probes recorded as 16271 "PFD" lines in a single session
 * (73% of the log) and which also is the CPU part of the cost, for a locomotive
 * that is only parked and waiting.
 *
 * The interesting state only changes when the locomotive itself moves, turns
 * around or advances its order, so remember the last outcome per vehicle: as long
 * as the last search came back empty, answer the next 7 calls with "no path"
 * without searching at all. A consist that becomes available is therefore picked
 * up within a handful of ticks instead of immediately, and everything else stays
 * as it was, because a successful search clears the hold again.
 */
struct R3RCoupleScanState {
	uint64_t empty_tick = 0; ///< Tick of the last search that came back empty.
	bool holding = false;    ///< Whether the retry throttle is currently armed.
};

static std::map<int, R3RCoupleScanState> _r3r_couple_scan_state;
static const uint R3R_COUPLE_SCAN_RETRY = 8;

/**
 * Try to skip the upcoming couple search for this vehicle.
 *
 * R3R (第 246 轮 / 第 4 项): the window is measured against `_tick_counter`, not
 * against a per-machine count of calls. The old form decremented a counter once
 * per call, so on a machine that issued the same commands but reached this code
 * a different number of times (a client that joined or loaded mid-session, a
 * different amount of pathfinder traffic before the same tick) the throttle
 * expired after a different number of *ticks*. The throttle decides whether a
 * locomotive searches for a coupling target at all, and the search decides where
 * it books a path, so the two machines could end up with different reservations.
 * A tick-based window is the same on every machine. `_tick_counter` is part of
 * the savegame and advanced by the game loop on every machine, and the anchor
 * (the tick of the last empty search) is itself derived from it.
 *
 * @param v The train whose search is about to run.
 * @param dont_reserve Whether the search would be a "look, do not book" pass.
 * @return 0 if the search must run, otherwise the number of calls still skipped.
 */
static uint R3RCoupleScanHold(const Train *v, bool dont_reserve)
{
	R3RCoupleScanState &st = _r3r_couple_scan_state[(int)v->index.base()];
	if (!st.holding) return 0;
	const uint64_t age = (uint64_t)_tick_counter - st.empty_tick;
	if (age >= (uint64_t)R3R_COUPLE_SCAN_RETRY) {
		st.holding = false;
		return 0;
	}
	return (uint)(R3R_COUPLE_SCAN_RETRY - age);
}

/**
 * R3R (第 271 轮): 上一次 YapfTrainCoupleTrack() 的 INVALID_TRACK 是不是
 * "本轮被节流开关跳过"造成的。
 *
 * 现场（build/R3R_debug.log）：
 *   CPL-SKIP veh=21 tile=205,273 orderType=16 dontReserve=0 retryIn=8 holdAge=0
 *   RESV-GC veh=21 tile=205,267 track=0x1 why=free-path
 *   RESV-LEDGER-FORGET veh=21 why=free-path mask=0x1 released=95 kept=0 phase=choose-track
 *
 * 节流（R3RCoupleScanHold：空搜索后 8 tick 内不再搜）把 INVALID_TRACK 交回去，
 * 而 ChooseTrainTrack() 只看到"没找到可挂车底"，于是执行 KI-316 的
 * FreeTrainTrackReservation() + MarkTrainAsStuck()，把已经铺好的一整段预留
 * （现场 95 格）连同账本一起清掉。玩家看到的"明明已经铺了预留，走过去之前又被
 * 神秘销毁，导致无法达速"就是这一段：每 8 tick 清一次，车永远只能走到预留末端。
 *
 * 节流跳过不改变任何地图状态，因此既不能撤销预留，也不该把车标成 stuck；
 * 保持现状、下一 tick 再判断即可。
 */
static std::map<int, uint64_t> _r3r_couple_skipped_tick;

/**
 * Whether the couple search for @p v was skipped by the retry throttle on this tick.
 * Callers must not treat that as "no coupling target exists".
 * @param v The locomotive whose search is in question (nullptr safe).
 */
bool R3RCoupleSearchSkipped(const Train *v)
{
	if (v == nullptr) return false;
	const auto it = _r3r_couple_skipped_tick.find((int)v->index.base());
	return it != _r3r_couple_skipped_tick.end() && it->second == (uint64_t)_tick_counter;
}

/** Remember whether this couple search found a path, so an empty result can be retried less eagerly. */
static void R3RCoupleScanResult(const Train *v, bool found)
{
	R3RCoupleScanState &st = _r3r_couple_scan_state[(int)v->index.base()];
	if (found) {
		st.holding = false;
	} else {
		st.holding = true;
		st.empty_tick = (uint64_t)_tick_counter;
	}
}

/**
 * Find the track to take when approaching a waiting consist to couple with.
 * @param v The train to find a track for.
 * @param dont_reserve Whether to skip making a reservation.
 * @return The track to take, or #INVALID_TRACK if no path was found.
 */
Track YapfTrainCoupleTrack(const Train *v, bool dont_reserve)
{
	const uint hold = R3RCoupleScanHold(v, dont_reserve);
	if (hold != 0) {
		/* R3R (第 271 轮): 这次 INVALID_TRACK 只是"本轮不搜"，必须让上层看得见，
		 * 否则它会当作"没有可挂车底"并撤销整段预留（见 R3RCoupleSearchSkipped 的注释）。 */
		_r3r_couple_skipped_tick[(int)v->index.base()] = (uint64_t)_tick_counter;
		return INVALID_TRACK;
	}
	/* A real search ran this tick, so any stale "was skipped" stamp is gone. */
	_r3r_couple_skipped_tick.erase((int)v->index.base());

	Trackdir ret = _settings_game.pf.forbid_90_deg
		? CYapfCoupleRailNo90::stFindNearestCoupleTrain(v, dont_reserve)
		: CYapfCoupleRail::stFindNearestCoupleTrain(v, dont_reserve);

	R3RCoupleScanResult(v, ret != INVALID_TRACKDIR);

	return (ret != INVALID_TRACKDIR) ? TrackdirToTrack(ret) : INVALID_TRACK;
}

/** if any track changes, this counter is incremented - that will invalidate segment cost cache */
int CSegmentCostCacheBase::s_rail_change_counter = 0;

void YapfCheckRailSignalPenalties()
{
	bool negative = false;
	int p0 = _settings_game.pf.yapf.rail_look_ahead_signal_p0;
	int p1 = _settings_game.pf.yapf.rail_look_ahead_signal_p1;
	int p2 = _settings_game.pf.yapf.rail_look_ahead_signal_p2;
	for (int i = 0; i < (int) _settings_game.pf.yapf.rail_look_ahead_max_signals; i++) {
		if (p0 + i * (p1 + i * p2) < 0) negative = true;
	}
	if (negative) {
		Debug(misc, 0, "Settings: pf.yapf.rail_look_ahead_signal_p0, pf.yapf.rail_look_ahead_signal_p1, pf.yapf.rail_look_ahead_signal_p2 and pf.yapf.rail_look_ahead_max_signal "
				"are set to incorrect values (i.e. resulting in negative penalties), negative penalties will be truncated");
	}
}
