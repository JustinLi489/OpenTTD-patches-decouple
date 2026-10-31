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
#include "../../r3r_perf.h"
#include "../../couple_group.h"
#include "../../pbs.h"
#include "../../rail_map.h" // R3R (KI-504 探针): GetRailReservationTrackBits, to log the reservation already on a tile
#include "../../date_func.h" // R3R (第 246 轮): _tick_counter, for the synchronized couple-scan throttle
#include <map> // R3R (KI-190): per-vehicle couple scan throttle state

#include "../../safeguards.h"

#if defined(UNIX) && defined(__GLIBC__)
#include <unistd.h>
#endif

/* R3R (第 276 轮, KI-429): 定义在 src\train_cmd.cpp。这里手写前向声明，避免为了一个
 * 函数原型去改头文件（改 .h 会触发全量重编）。语义见 src\train_cmd.cpp 的定义处注释。
 * 返回值：0 = 不截断；1 = 截断且终点 = 等待区路点；2 = 已在路点上但未放行。 */
int R3RShouldCutCoupleAtWaitPoint(const Train *v, uint32_t *wait_wp);

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
	inline static std::vector<Node *> safe_position_nodes;
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
		FILE *dbg = R3RFopenDbg("a");
		if (dbg != nullptr) {
			fprintf(dbg, "FSCP tile=%d,%d fail=depot\n", (int)TileX(tile), (int)TileY(tile));
			fclose(dbg);
		}
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
			if (n_tile_heads >= 2 && tile_heads_have_target) {
				/* 按"格 + 最小两个车号"变化各打一行，避免每 tick 刷屏。 */
				static uint32_t last_key = 0xFFFFFFFF;
				uint32_t key = (tile.base() << 8) ^ (tile_heads[0]->index.base() << 4) ^ tile_heads[1]->index.base();
				if (key != last_key) {
					last_key = key;
					FILE *dbg = R3RFopenDbg("a");
					if (dbg != nullptr) {
						fprintf(dbg, "FSCP tile=%d,%d skip=multihead n=%u h=%d,%d\n",
								(int)TileX(tile), (int)TileY(tile), (uint)n_tile_heads,
								(int)tile_heads[0]->index.base(), (int)tile_heads[1]->index.base());
						fclose(dbg);
					}
				}
				return true;
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
					FILE *dbg = R3RFopenDbg("a");
					if (dbg != nullptr) {
						fprintf(dbg, "FSCP tile=%d,%d fail=notSeg best=%d ord=%d sf=%d\n", (int)TileX(tile), (int)TileY(tile), (int)best->index.base(), (int)best->current_order.GetType(), (int)best->IsSegmentFront());
						fclose(dbg);
					}
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
					FILE *dbg = R3RFopenDbg("a");
					if (dbg != nullptr) {
						fprintf(dbg, "FSCP tile=%d,%d fail=2nd best=%d 2nd=%d\n", (int)TileX(tile), (int)TileY(tile), (int)best->index.base(), (int)second_best->index.base());
						fclose(dbg);
					}
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
					FILE *dbg = R3RFopenDbg("a");
					if (dbg != nullptr) {
						fprintf(dbg, "FSCP tile=%d,%d fail=other best=%d other=%d\n", (int)TileX(tile), (int)TileY(tile), (int)best->index.base(), (int)other_train->index.base());
						fclose(dbg);
					}
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
					FILE *sdbg = R3RFopenDbg("a");
					if (sdbg != nullptr) {
						fprintf(sdbg, "FSCP-SELFHELD tile=%d,%d veh=%d tk=%d want=%d res=0x%X\n",
							(int)TileX(tile), (int)TileY(tile), (int)coupler->index.base(),
							(int)tk, (int)TrackdirToTrack(td), (unsigned)reserved);
						fclose(sdbg);
					}
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
				FILE *dbg = R3RFopenDbg("a");
				if (dbg != nullptr) {
					fprintf(dbg, "FSCP tile=%d,%d fail=tryReserve td=%d want=%d type=%d tbits=0x%X res=0x%X plain=%d depot=%d st=%d self=%d\n",
						(int)TileX(tile), (int)TileY(tile), (int)td, (int)TrackdirToTrack(td),
						(int)GetTileType(tile), (unsigned)GetTrackBits(tile), (unsigned)GetReservedTrackbits(tile),
						(int)IsPlainRail(tile), (int)IsRailDepotTile(tile), (int)IsRailStationTile(tile),
						(int)self_held);
					const uint8_t rb = (uint8_t)(GetReservedTrackbits(tile) & TRACK_BIT_MASK);
					for (Track tk = TRACK_BEGIN; tk != TRACK_END; tk++) {
						if (!HasBit(rb, tk)) continue;
						Train *owner = GetTrainForReservation(tile, tk);
						if (owner == nullptr) {
							fprintf(dbg, "FSCP-RES tile=%d,%d tk=%d owner=-1\n",
								(int)TileX(tile), (int)TileY(tile), (int)tk);
							continue;
						}
						/* R3R (KI-162 probe): the blocker is named, but not yet
						 * classified. These columns decide whether the owner is
						 * (a) still rolling / shrinking its reservation while it
						 * clears the coupler's path, or (b) already standing still
						 * and merely holding its own forward reservation.
						 * ostop/ospd = parked?  oend = where the owner's own
						 * reservation ends, othru = does it really pass through
						 * this tile, octgt/ocplok = is the owner the coupler's
						 * legitimate couple target at all. */
						Train *oh = const_cast<Train *>(owner->First());
						const PBSTileInfo oend = FollowTrainReservation(oh, nullptr);
						fprintf(dbg, "FSCP-RES tile=%d,%d tk=%d owner=%d otile=%d,%d odir=%d ospd=%d ostop=%d ord=%d oroi=%d oend=%d,%d oendtd=%d othru=%d octgt=%d ocplok=%d cpl=%d,%d\n",
							(int)TileX(tile), (int)TileY(tile), (int)tk, (int)oh->index.base(),
							(int)TileX(oh->tile), (int)TileY(oh->tile), (int)oh->direction, (int)oh->cur_speed,
							(int)oh->vehstatus.Test(VehState::Stopped), (int)oh->current_order.GetType(), (int)oh->cur_real_order_index,
							(int)TileX(oend.tile), (int)TileY(oend.tile), (int)oend.trackdir,
							(int)TrainReservationPassesThroughTile(oh, tile),
							(int)R3RIsCoupleTarget(oh), (int)R3RCoupleAllowed(coupler, oh),
							(int)TileX(coupler->tile), (int)TileY(coupler->tile));
					}
					/* R3R (KI-162 probe, round 2; SEMANTICS CORRECTED in round 91):
					 * Neither API here can answer "who reserved this tile".
					 *  - GetTrainForReservation() ("owner=" above) follows the
					 *    reservation to one end and names the train standing there,
					 *    and on station tiles it scans the whole platform (pbs.cpp
					 *    CheckTrainsOnTrack), so it can even name a train on another
					 *    track. See KI-162 round 90.
					 *  - TrainReservationPassesThroughTile() (this block) walks the
					 *    *contiguous* run of reserved trackbits starting at the
					 *    train's moving front (pbs.cpp FollowReservationEnumerate;
					 *    the reserved bits are global and carry no owner). A train
					 *    parked on that run therefore "reaches" the tile *through
					 *    someone else's* reservation and is listed as well. It means
					 *    "this chain's reservation reaches the tile", NOT "this
					 *    chain holds the reservation" -- do not read it as ownership.
					 * Verified in round 91: at 60,53 the parked consist veh=1 was
					 * listed next to the actually holding loco veh=0, while the
					 * consist's own reservation only covered 60,48..60,51.
					 * The reliable ownership evidence is the FSCP-VEH line below (a
					 * train physically standing on the tile) plus the owner's
					 * ord/dir/spd. REACH-NONE keeps its meaning: no chain can reach
					 * this tile along a contiguous reservation at all, which would be
					 * a genuinely stale/orphan reservation. */
					{
						static TileIndex s_fscp_reach_none_tile = INVALID_TILE;
						uint reach_hits = 0;
						for (Train *tr : Train::Iterate()) {
							if (tr->First() != tr) continue;
							if (!TrainReservationPassesThroughTile(tr, tile)) continue;
							reach_hits++;
							const PBSTileInfo tend = FollowTrainReservation(tr, nullptr);
							Train *gtr = nullptr;
							for (Track tk2 = TRACK_BEGIN; tk2 != TRACK_END; tk2++) {
								if (!HasBit(rb, tk2)) continue;
								Train *g = GetTrainForReservation(tile, tk2);
								if (g != nullptr) { gtr = g->First(); break; }
							}
							fprintf(dbg, "FSCP-REACH tile=%d,%d veh=%d ord=%d roi=%d pos=%d,%d dir=%d spd=%d stopped=%d seg=%d cplok=%d ctgt=%d end=%d,%d endtd=%d isGtr=%d cpl=%d,%d\n",
								(int)TileX(tile), (int)TileY(tile), (int)tr->index.base(),
								(int)tr->current_order.GetType(), (int)tr->cur_real_order_index,
								(int)TileX(tr->tile), (int)TileY(tr->tile), (int)tr->direction,
								(int)tr->cur_speed, (int)tr->vehstatus.Test(VehState::Stopped),
								(int)tr->IsSegmentFront(), (int)R3RCoupleAllowed(coupler, tr),
								(int)R3RIsCoupleTarget(tr),
								(int)TileX(tend.tile), (int)TileY(tend.tile), (int)tend.trackdir,
								(int)(gtr == tr),
								(int)TileX(coupler->tile), (int)TileY(coupler->tile));
						}
						if (reach_hits == 0) {
							if (s_fscp_reach_none_tile != tile) {
								s_fscp_reach_none_tile = tile;
								fprintf(dbg, "FSCP-REACH-NONE tile=%d,%d (no chain reaches this tile along a contiguous reservation)\n",
									(int)TileX(tile), (int)TileY(tile));
							}
						} else {
							s_fscp_reach_none_tile = INVALID_TILE;
						}
					}
					for (Train *tr : VehiclesOnTile<VehicleType::Train>(tile)) {
						fprintf(dbg, "FSCP-VEH tile=%d,%d veh=%d track=0x%X dir=%d front=%d ord=%d\n",
							(int)TileX(tile), (int)TileY(tile), (int)tr->index.base(),
							(unsigned)tr->track, (int)tr->direction, (int)tr->IsFrontEngine(),
							(int)tr->current_order.GetType());
					}
					/* R3R (第 339 轮, 疑点 R338-A): 上三条探针都答不出"这格的预留是谁的"
					 * （FSCP-RES 顺预留找端点、FSCP-REACH 只在"预留可达"意义上成立、
					 * FSCP-VEH 只报格上站着谁）。这里补一条能直接判定「自交叉」的读数：
					 * 询路车**自己的链**上有哪一节压在这格上、它的 track 与被占位是否
					 * 重叠。若 overlap != 0 ⇒ 是询路车自己的后半节占住了交叉轨，
					 * TryReserveRailTrack 必然失败（PBS 铁律：交叉轨不能同时预留），
					 * 与谁先订无关；此时 KI-510 方案 γ 的截断点会退化到 origin
					 * （等于原地不动），修法必须改成"询路前先收缩本车预留"或
					 * "禁止挂接寻路规划出自交叉机动"。overlap == 0 则说明占位者
					 * 是别的车/别的链，γ 的截断路径成立。 */
					{
						Train *const cpl_first = coupler->First();
						const PBSTileInfo cpl_end = FollowTrainReservation(cpl_first, nullptr);
						fprintf(dbg, "FSCP-CPL tile=%d,%d veh=%d pos=%d,%d dir=%d spd=%d end=%d,%d endtd=%d look=%d real=%d\n",
							(int)TileX(tile), (int)TileY(tile), (int)cpl_first->index.base(),
							(int)TileX(cpl_first->tile), (int)TileY(cpl_first->tile),
							(int)cpl_first->direction, (int)cpl_first->cur_speed,
							(int)TileX(cpl_end.tile), (int)TileY(cpl_end.tile), (int)cpl_end.trackdir,
							(cpl_first->lookahead != nullptr) ? 1 : 0,
							(int)cpl_first->cur_real_order_index);
						for (Train *c = cpl_first; c != nullptr; c = c->Next()) {
							if (c->tile != tile) continue;
							fprintf(dbg, "FSCP-SELF tile=%d,%d veh=%d track=0x%X dir=%d front=%d overlap=0x%X\n",
								(int)TileX(tile), (int)TileY(tile), (int)c->index.base(),
								(unsigned)c->track, (int)c->direction, (int)c->IsFrontEngine(),
								(unsigned)((c->track & rb) & TRACK_BIT_MASK));
						}
					}
					fclose(dbg);
				}
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
	bool ReserveRailStationPlatform(TileIndex &tile, DiagDirection dir)
	{
		TileIndex     start = tile;
		TileIndexDiff diff = TileOffsByDiagDir(dir);

		do {
			if (HasStationReservation(tile)) return false;
			SetRailStationReservation(tile, true);
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
	 */
	bool ReserveSingleTrack(TileIndex tile, Trackdir td)
	{
		if (IsRailStationTile(tile)) {
			if (!ReserveRailStationPlatform(tile, TrackdirToExitdir(ReverseTrackdir(td)))) {
				/* Platform could not be reserved, undo. */
				this->res_fail_tile = tile;
				this->res_fail_td = td;
			}
		} else {
			if (!TryReserveRailTrackdir(Yapf().GetVehicle(), tile, td)) {
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
	 * 止于会合点。失败时回滚本次已订的格，由调用方如实返回失败。
	 *
	 * @param steps 摸路产出的 (格, 轨) 序列。
	 * @param booked 出参：已成功预订的格数。
	 * @return 全部成功为 true。
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
		for (size_t i = 0; i < steps.size(); ++i) {
			const TileIndex stile = steps[i].first;
			const Trackdir stdir = steps[i].second;
			this->ReserveSingleTrack(stile, stdir);
			if (this->res_fail_tile != INVALID_TILE) {
				{
					FILE *dbg = R3RFopenDbg("a");
					if (dbg != nullptr) {
						Train *const coupler = const_cast<Train *>(Train::From(Yapf().GetVehicle()));
						const TileIndex ftile = this->res_fail_tile;
						const Track want = TrackdirToTrack(stdir);
						Train *owner = IsRailStationTile(ftile)
								? GetTrainForWholeTileReservationArbitrated(ftile)
								: GetTrainForReservationArbitrated(ftile, want);
						if (owner == nullptr && IsRailStationTile(ftile)) owner = GetTrainForReservationArbitrated(ftile, want);
						const bool selfheld = (owner != nullptr && (owner == coupler || owner->First() == coupler->First()));
						/* 位读数必须先自证 plain rail: GetTrackBits/GetReservedTrackbits
						 * 在非 plain rail 上是 dbg_assert(KI-207c 附记 7b 的教训)。 */
						const bool plain = IsPlainRail(ftile);
						fprintf(dbg, "CPL-RES-FAIL veh=%d nth=%u/%u tile=%d,%d td=%d want=%d booked-before=%u failtd=%d type=%d tbits=%d res=%d plain=%d st=%d depot=%d owner=%d selfheld=%d otile=%d,%d ospd=%d ord=%d\n",
								(int)coupler->index.base(), (unsigned)i, (unsigned)steps.size(),
								(int)TileX(ftile), (int)TileY(ftile), (int)stdir, (int)want,
								(unsigned)booked, (int)this->res_fail_td,
								(int)GetTileType(ftile),
								plain ? (int)GetTrackBits(ftile) : -1,
								plain ? (int)GetReservedTrackbits(ftile) : -1,
								(int)plain, (int)IsRailStationTile(ftile), (int)IsRailDepotTile(ftile),
								(owner == nullptr) ? -1 : (int)owner->index.base(), (int)selfheld,
								(owner == nullptr) ? -1 : (int)TileX(owner->tile),
								(owner == nullptr) ? -1 : (int)TileY(owner->tile),
								(owner == nullptr) ? -1 : (int)owner->cur_speed,
								(owner == nullptr) ? -1 : (int)owner->current_order.GetType());
						fclose(dbg);
					}
				}
				for (size_t k = booked; k > 0; --k) {
					UnreserveRailTrackdir(steps[k - 1].first, steps[k - 1].second);
				}
				booked = 0;
				return false;
			}
			++booked;
		}
		/* R3R (REG-359-K 探针, 只读): 成功时列出本次实际订下的格序，用来回答
		 * "第 5 次 booked=6 究竟订了哪 6 格、有没有覆盖 36,12/37,12"。 */
		if (!steps.empty()) {
			FILE *dbg = R3RFopenDbg("a");
			if (dbg != nullptr) {
				fprintf(dbg, "CPL-RES-OK veh=%d total=%u tiles=",
						(int)Train::From(Yapf().GetVehicle())->index.base(), (unsigned)steps.size());
				for (size_t i = 0; i < steps.size() && i < 16; ++i) {
					fprintf(dbg, "%s%d,%d/%d", (i == 0) ? "" : ";",
							(int)TileX(steps[i].first), (int)TileY(steps[i].first), (int)TrackdirToTrack(steps[i].second));
				}
				if (steps.size() > 16) fprintf(dbg, ";...(truncated)");
				fprintf(dbg, "\n");
				fclose(dbg);
			}
		}
		if (!steps.empty()) YapfNotifyTrackLayoutChange(INVALID_TILE, INVALID_TRACK);
		return true;
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
	 * @param target End location of the reservation.
	 * @param origin Start location of the reservation.
	 * @return \c true iff the path could be reserved.
	 */
	bool TryReservePath(PBSTileInfo *target, TileIndex origin, bool unsafe_pos = false)
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
		if (!unsafe_pos && !IsWaitingPositionFree(Yapf().GetVehicle(), this->res_dest_tile, this->res_dest_td, false, &restricted_signal_state)) return false;

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
		 * （build\R3R_debug.log:1406-1407），而 unsafe_pos=true 的调用路径
		 * （本文件 :1380 挂接寻路）在格级失败时既不回滚也不早退 ——
		 * 见下方 `if (!unsafe_pos && this->res_fail_tile != INVALID_TILE)`，
		 * 于是"reserved=1"可能是空心的（一格都没订上却返回成功）。
		 * 这两个计数器是该判断的唯一依据，零行为改动。 */
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
						FILE *dbg = R3RFopenDbg("a");
						if (dbg != nullptr) {
							fprintf(dbg, "RPFAIL veh=%d tick=%u origin=%d,%d tile=%d,%d td=%d unsafe=%d nTry=%d nOK=%d\n",
									(int)r3r_vid, (uint)_tick_counter,
									(int)TileX(this->origin_tile), (int)TileY(this->origin_tile),
									(int)TileX(tile), (int)TileY(tile), (int)td,
									(int)unsafe_pos, r3r_ntry, r3r_nok);
							fclose(dbg);
						}
					}
				}
				return r3r_tok;
			});
			if (!unsafe_pos && this->res_fail_tile != INVALID_TILE) {
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
			const uint32_t r3r_vid = Yapf().GetVehicle()->index.base();
			const TileIndex r3r_node_tile = this->res_dest_node->GetLastTile();
			const uint64_t r3r_cplres_sig = ((uint64_t)(uint32_t)r3r_node_tile.base() << 40)
					^ ((uint64_t)(uint32_t)this->origin_tile.base() << 16)
					^ ((uint64_t)(uint32_t)r3r_ntry << 8)
					^ ((uint64_t)(uint32_t)r3r_nok << 4)
					^ (uint64_t)(uint32_t)this->res_dest_td;
			uint64_t &r3r_cplres_prev = r3r_cplres_seen[r3r_vid];
			if (r3r_cplres_prev != r3r_cplres_sig) {
				r3r_cplres_prev = r3r_cplres_sig;
				FILE *dbg = R3RFopenDbg("a");
				if (dbg != nullptr) {
					fprintf(dbg, "CPLRES veh=%d tick=%u origin=%d,%d destTile=%d,%d destTd=%d node=%d,%d root=%d unsafe=%d nTry=%d nOK=%d empty=%d\n",
							(int)r3r_vid, (uint)_tick_counter,
							(int)TileX(this->origin_tile), (int)TileY(this->origin_tile),
							(int)TileX(this->res_dest_tile), (int)TileY(this->res_dest_tile), (int)this->res_dest_td,
							(int)TileX(r3r_node_tile), (int)TileY(r3r_node_tile),
							(this->res_dest_node->parent == nullptr) ? 1 : 0,
							(int)unsafe_pos, r3r_ntry, r3r_nok, (r3r_ntry == 0) ? 1 : 0);
					fclose(dbg);
				}
			}
		}

		if (target != nullptr) target->okay = true;

		if (Yapf().CanUseGlobalCache(*this->res_dest_node)) {
			YapfNotifyTrackLayoutChange(INVALID_TILE, INVALID_TRACK);
		}

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
		if (F.Follow(old_node.GetLastTile(), old_node.GetLastTrackdir())) {
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
		/* R3R (第 236 轮, KI-344): 清零本次扫描的 census，结束时由 CPL-SCAN 无条件回放。 */
		R3RCoupleScanReset();
		PBSTileInfo origin = PBSTileInfo(v->tile, v->GetVehicleTrackdir(), false);
		{
			FILE *dbg = R3RFopenDbg("a");
			if (dbg != nullptr) {
				fprintf(dbg, "CPL-ORIGIN veh=%d origin=%d,%d td=%d vehDir=%d vehTd=%d dontReserve=%d\n",
						(int)v->index.base(), (int)TileX(origin.tile), (int)TileY(origin.tile),
						(int)origin.trackdir, (int)v->direction, (int)v->GetVehicleTrackdir(), (int)dont_reserve);
				fclose(dbg);
			}
		}
		/* R3R（第 330 轮取证，只读，KI-446）：起点格到底能往哪些方向走。
		 *
		 * 27-G 断言"YAPF 起点只用单一 trackdir、起点格不能换轨"，这需要先证明
		 * 起点格的其它出口在地图上确实存在、且通向哪里。这里只 Follow 一步，
		 * 不预订、不改列车状态。注意 tdb 打的是 GetTileTrackdirBits 原值
		 * （REACH 里的 bits= 经 TrackdirBitsToTrackBits 压缩过，正是 REG-330-B
		 * 里 16,14 显示 bits=0x1 与 CPL-TRACE tdb=0x101 对不上的来源）。 */
		{
			const TrackdirBits r3r_ob = GetTileTrackdirBits(v->tile, ::TransportType::TRANSPORT_RAIL, 0);
			FILE *dbg = R3RFopenDbg("a");
			if (dbg != nullptr) {
				fprintf(dbg, "CPL-ORIGIN-TD veh=%d tile=%d,%d tdb=0x%x tbits=0x%x\n",
						(int)v->index.base(), (int)TileX(v->tile), (int)TileY(v->tile),
						(unsigned)r3r_ob, (unsigned)TrackdirBitsToTrackBits(r3r_ob));
				fclose(dbg);
			}
			for (TrackdirBits tb = r3r_ob; tb != TRACKDIR_BIT_NONE; tb = KillFirstBit(tb)) {
				const Trackdir d = (Trackdir)FindFirstBit(tb);
				TrackFollower fx(v, Yapf().GetCompatibleRailTypes());
				const bool ok = fx.Follow(v->tile, d);
				dbg = R3RFopenDbg("a");
				if (dbg != nullptr) {
					fprintf(dbg, "CPL-ORIGIN-FOLLOW veh=%d td=%d trk=%d ok=%d nt=%d,%d nbits=0x%x st=%d skip=%d\n",
							(int)v->index.base(), (int)d, (int)TrackdirToTrack(d), (int)ok,
							ok ? (int)TileX(fx.new_tile) : -1, ok ? (int)TileY(fx.new_tile) : -1,
							(unsigned)fx.new_td_bits, (int)fx.is_station, (int)fx.tiles_skipped);
					fclose(dbg);
				}
			}
		}
		/* Set origin and destination. */
		Yapf().SetOrigin(origin.tile, origin.trackdir);

		/* R3R (第 276 轮, KI-429): 等待区路点截断（玩家口径 Q-276-4 / §21.9）。
		 *
		 * 只要还没到"可以延伸预留"的状态（A 没有逐节停稳在同一站台区间，或者本车
		 * 自己还没到等待区路点），本次寻路就只允许以等待区路点作终点 ⇒ 回溯/预留
		 * 止于路点，不会再一路铺到目标车底、更不会铺进道岔区（KI-429 的
		 * FSCP tile=258,233）。
		 *
		 * 判定每 tick 由 train_cmd.cpp 的 R3RShouldCutCoupleAtWaitPoint() 重算：
		 *   0 = 不截断（没有等待区路点，或本次挂接已放行）
		 *   1 = 截断，终点 = 等待区路点
		 *   2 = 本车已站在等待区路点上但还没放行 */
		uint32_t r3r_cut_wp = 0;
		const int r3r_wait_cut = R3RShouldCutCoupleAtWaitPoint(v, &r3r_cut_wp);

		/* 边沿触发：状态变化才写一行，避免每 tick 刷屏。 */
		{
			static std::map<uint32_t, int> r3r_cut_state;
			const uint32_t r3r_key = v->index.base();
			auto it = r3r_cut_state.find(r3r_key);
			if (it == r3r_cut_state.end() || it->second != r3r_wait_cut) {
				r3r_cut_state[r3r_key] = r3r_wait_cut;
				FILE *dbg = R3RFopenDbg("a");
				if (dbg != nullptr) {
					fprintf(dbg, "CPL-WAITCUT veh=%d state=%d wp=%u tile=%d,%d\n",
							(int)v->index.base(), r3r_wait_cut, (unsigned int)r3r_cut_wp,
							(int)TileX(v->tile), (int)TileY(v->tile));
					fclose(dbg);
				}
			}
		}

		/* R3R (第 346 轮续, KI-516): 每次挂接寻路先清"被第三方车挡"标志，
		 * 只有下面回溯阶段的 CPL-SAFE-FAIL 才会重新置位。 */
		_r3r_couple_path_blocked = false;

		if (r3r_wait_cut == 2) {
			/* 已经停在等待区路点上、只是还没放行：本次寻路没有合法终点。
			 * 直接报"无路径"——不能放任 YAPF 去找终点，否则它会为了一条自己
			 * 脚下的路点把整张图搜一遍（每 tick 一次）。 */
			return INVALID_TRACKDIR;
		}

		Yapf().SetCoupleWaitCut(r3r_wait_cut == 1, r3r_cut_wp);

		Yapf().SetDestination(v);

		bool path_found = Yapf().FindPath(v);
		{
			FILE *dbg = R3RFopenDbg("a");
			if (dbg != nullptr) {
				fprintf(dbg, "CPL-PATHFOUND veh=%d found=%d\n", (int)v->index.base(), (int)path_found);
				fclose(dbg);
			}
		}
		/* R3R (第 236 轮, KI-344): 无条件清点回放 —— 这是回答"found=0 到底是等待链
		 * 不在场还是被闸门拒掉"的唯一出口，与 CPL-PATHFOUND 同频（每次寻路一行）。 */
		/* R3R (第 255 轮, P0): nodes = closed + enter，即本次搜索真正走过的节点数。 */
		R3RCoupleScanDump((int)v->index.base(), path_found ? 1 : 0,
				(uint)Yapf().nodes.ClosedCount(), (uint)Yapf().GetMaxSearchNodes(),
				(uint)(Yapf().nodes.ClosedCount() + _r3r_cpl_scan_enter), (uint32_t)_tick_counter);
		if (!path_found) return INVALID_TRACKDIR;

		/* Found a destination, set as reservation target. */
		Node *pNode = Yapf().GetBestNode();
		/* R3R: route to the consist head car itself instead of the platform dest tile -
		 * the platform dest may sit on the far side of the consist, making the path
		 * cut straight through it. */
		TileIndex consist_head_tile = INVALID_TILE;
		bool best_on_consist = false;
		if (pNode != nullptr && IsRailStationTile(pNode->GetLastTile())) {
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
			{
				FILE *dbg = R3RFopenDbg("a");
				if (dbg != nullptr) {
					fprintf(dbg, "CPL-HEAD best=%d,%d head=%d,%d on=%d\n",
							(int)TileX(pNode->GetLastTile()), (int)TileY(pNode->GetLastTile()),
							(int)TileX(consist_head_tile), (int)TileY(consist_head_tile),
							(int)best_on_consist);
					fclose(dbg);
				}
			}
		}
		{
			FILE *dbg = R3RFopenDbg("a");
			if (dbg != nullptr) {
				fprintf(dbg, "CPL-BEST veh=%d tile=%d,%d td=%d cost=%d\n",
						(int)v->index.base(), (int)TileX(pNode->GetLastTile()), (int)TileY(pNode->GetLastTile()),
						(int)pNode->GetLastTrackdir(), (int)pNode->cost);
				fclose(dbg);
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
		while (pNode->parent != nullptr) {
			pPrev = pNode;
			pNode = pNode->parent;
			++r3r_hops;

			{
				FILE *dbg = R3RFopenDbg("a");
				if (dbg != nullptr) {
					fprintf(dbg, "CPL-BACK pPrev=%d,%d td=%d parent=%d,%d ptd=%d\n", (int)TileX(pPrev->GetLastTile()), (int)TileY(pPrev->GetLastTile()), (int)pPrev->GetLastTrackdir(), (int)TileX(pNode->GetLastTile()), (int)TileY(pNode->GetLastTile()), (int)pNode->GetLastTrackdir());
					fclose(dbg);
				}
			}
			if (!this->CheckSafePositionOnNode(pPrev)) {
				{
					FILE *dbg = R3RFopenDbg("a");
					if (dbg != nullptr) {
						fprintf(dbg, "CPL-SAFE-FAIL veh=%d tile=%d,%d\n",
								(int)v->index.base(), (int)TileX(pPrev->GetLastTile()), (int)TileY(pPrev->GetLastTile()));
						fclose(dbg);
					}
				}
				/* R3R (第 346 轮续, KI-516): 路径算出来了、只是回溯的安全位判据被别的
				 * 车否决 ⇒ 标记"被挡"，让 train_cmd.cpp 不要降级去绕别的股道，
				 * 而是原地等障碍离开后重算（玩家 2026-10-28 口径）。 */
				_r3r_couple_path_blocked = true;
				return INVALID_TRACKDIR;
			}
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
		Trackdir best_td = INVALID_TRACKDIR;
		/* R3R (第 332 轮): best_td 属于哪一格。贪婪扫描取 st（机车自身格），
		 * 采纳 YAPF 首步时取首步格 —— 后者才是 new_tile。 */
		TileIndex best_start = st;
		std::function<bool(TileIndex, Trackdir, int)> reach = [&](TileIndex cur, Trackdir cur_td, int depth) -> bool {
			if (depth <= 15) {
				FILE *dbg = R3RFopenDbg("a");
				if (dbg != nullptr) {
					fprintf(dbg, "REACH cur=%d,%d td=%d depth=%d trk=%d bits=0x%x\n", (int)TileX(cur), (int)TileY(cur), (int)cur_td, depth, (int)TrackdirToTrack(cur_td), (unsigned)TrackdirBitsToTrackBits(GetTileTrackdirBits(cur, ::TransportType::TRANSPORT_RAIL, 0)));
					fclose(dbg);
				}
			}
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
					!HasBit(GetTileTrackdirBits(cur, ::TransportType::TRANSPORT_RAIL, 0), cur_td)) {
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
		tdb = GetTileTrackdirBits(st, ::TransportType::TRANSPORT_RAIL, 0);
		{
			FILE *dbg = R3RFopenDbg("a");
			if (dbg != nullptr) {
				fprintf(dbg, "CPL-TRACE st=%d,%d tdb=0x%x\n", (int)TileX(st), (int)TileY(st), (unsigned)tdb);
				fclose(dbg);
			}
		}

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
		const TrackdirBits first_tdb = GetTileTrackdirBits(first_tile, ::TransportType::TRANSPORT_RAIL, 0);
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
		{
			/* R3R (第 332 轮, 只读): 本次采纳的是 YAPF 首步还是贪婪扫描结果，
			 * 以及该 trackdir 属于哪一格（应按 new_tile 解读）。 */
			FILE *dbg = R3RFopenDbg("a");
			if (dbg != nullptr) {
				fprintf(dbg, "CPL-FIRST-TD st=%d,%d hops=%u yapf=%d at=%d,%d tdb=0x%x ok=%d best=%d start=%d,%d trk=%d\n",
						(int)TileX(st), (int)TileY(st), (unsigned)r3r_hops,
						(int)yapf_first_td, (int)TileX(first_tile), (int)TileY(first_tile),
						(unsigned)first_tdb, yapf_first_ok ? 1 : 0, (int)best_td,
						(int)TileX(best_start), (int)TileY(best_start),
						(best_td == INVALID_TRACKDIR) ? -1 : (int)TrackdirToTrack(best_td));
				fclose(dbg);
			}
		}
		{
			FILE *dbg = R3RFopenDbg("a");
			if (dbg != nullptr) {
				fprintf(dbg, "CPL-TRACE-RESULT st=%d,%d best=%d\n", (int)TileX(st), (int)TileY(st), (int)best_td);
				fclose(dbg);
			}
		}
		if (best_td != INVALID_TRACKDIR) {
			next_trackdir = best_td;
			if (!dont_reserve) {
				std::function<bool(TileIndex, Trackdir, int)> rp;
				/* rp_impl = 原来的 rp() 本体；递归调用一律走 rp（带标记回滚的外壳）。 */
				std::function<bool(TileIndex, Trackdir, int)> rp_impl = [&](TileIndex cur, Trackdir cur_td, int depth) -> bool {
					if (depth <= 15) {
						FILE *dbg = R3RFopenDbg("a");
						if (dbg != nullptr) {
							fprintf(dbg, "RP cur=%d,%d td=%d depth=%d trk=%d bits=0x%x\n", (int)TileX(cur), (int)TileY(cur), (int)cur_td, depth, (int)TrackdirToTrack(cur_td), (unsigned)TrackdirBitsToTrackBits(GetTileTrackdirBits(cur, ::TransportType::TRANSPORT_RAIL, 0)));
							fclose(dbg);
						}
					}
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
					if (cur == tg) return true;
					if (!IsRailStationTile(cur) && !IsRailDepotTile(cur)) {
						/* 只记录物理走过、且需要预订的格：站台/车库不订（与 rp_last_tile
						 * 的口径一致——预留止于会合点，不越过车底）；起点格也不订
						 * （TryReservePath 的节点链同样跳过根节点）。 */
						if (cur != st || depth != 0) r3r_resv_path.push_back(R3RCoupleStep{cur, cur_td});
					}
					/* R3R: do not walk the path through a depot - but allow the start tile,
					 * which may itself be the depot the loco is leaving (cur == st
					 * only occurs at the first call, at depth 0). */
					if (IsRailDepotTile(cur) && cur != st) return false;
					/* R3R (第 333 轮): 同 reach()，Follow 之前自证 (cur, cur_td) 同格。 */
					if (cur_td == INVALID_TRACKDIR ||
							!HasBit(GetTileTrackdirBits(cur, ::TransportType::TRANSPORT_RAIL, 0), cur_td)) {
						return false;
					}
					TrackFollower f3(v, Yapf().GetCompatibleRailTypes());
					if (!f3.Follow(cur, cur_td)) return false;
					if (f3.is_station && f3.tiles_skipped > 0 && R3RSkippedStationStretchContains(cur, f3.new_tile, f3.exitdir, f3.tiles_skipped, tg)) {
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
			 * 这里按序逐格预订（第二遍）。失败则回滚已订格并如实返回失败，
			 * 交 train_cmd.cpp 的 KI-316 路径（FreeTrainTrackReservation +
			 * MarkTrainAsStuck）处置；绝不再出现 "reserved=1 而 nTry=0" 的空真。
			 */
			if (!r3r_resv_path.empty()) {
				const R3RCoupleStep &r3r_last = r3r_resv_path.back();
				this->SetReservationTarget(pNode, r3r_last.first, r3r_last.second);
				reserved = this->R3RReservePathSequence(r3r_resv_path, r3r_booked);
			}
			/* 摸路一格都没给出 ⇒ 没有可订的东西，直接报失败（不许空手成功）。 */
		} else {
			reserved = this->TryReservePath(nullptr, pNode->GetLastTile(), true);
		}
			{
				FILE *dbg = R3RFopenDbg("a");
				if (dbg != nullptr) {
					fprintf(dbg, "CPL-RESERVE veh=%d reserved=%d next=%d path=%d booked=%d\n",
							(int)v->index.base(), (int)reserved, (int)next_trackdir, (int)r3r_resv_path.size(), (int)r3r_booked);
							fclose(dbg);
				}
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
		Yapf().SetOrigin(origin.tile, origin.trackdir, INVALID_TILE, INVALID_TRACKDIR, 1);
		Yapf().SetTreatFirstRedTwoWaySignalAsEOL(true);
		Yapf().SetDestination(v);

		/* find the best path */
		path_found = Yapf().FindPath(v);
		{
			FILE *dbg = R3RFopenDbg("a");
			if (dbg != nullptr) {
				fprintf(dbg, "CRT veh=%d order=%d dir=%d origin=%d,%d td=%d found=%d\n",
						(int)v->index.base(), (int)v->current_order.GetType(), (int)v->direction,
						(int)TileX(origin.tile), (int)TileY(origin.tile), (int)origin.trackdir, (int)path_found);
				fclose(dbg);
			}
		}

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
				this->TryReservePath(target, node->GetLastTile());
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
		FILE *dbg = R3RFopenDbg("a");
		if (dbg != nullptr) {
			fprintf(dbg, "CRT-NOTD veh=%d db=%d front(idx=%d tile=%d,%d trk=0x%X dir=%d crashed=%d td=%d) back(idx=%d tile=%d,%d trk=0x%X dir=%d crashed=%d td=%d)\n",
					(int)v->index.base(), (int)v->vehicle_flags.Test(VehicleFlag::DrivingBackwards),
					(int)moving_front->index.base(), (int)TileX(moving_front->tile), (int)TileY(moving_front->tile),
					(uint)moving_front->track, (int)moving_front->direction,
					(int)moving_front->vehstatus.Test(VehState::Crashed), (int)td,
					(int)moving_back->index.base(), (int)TileX(moving_back->tile), (int)TileY(moving_back->tile),
					(uint)moving_back->track, (int)moving_back->direction,
					(int)moving_back->vehstatus.Test(VehState::Crashed), (int)td_back);
			fclose(dbg);
		}
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
			FILE *dbg = R3RFopenDbg("a");
			if (dbg != nullptr) {
				fprintf(dbg, "CRT-FOLD veh=%d tile=%d,%d dir=%d backTile=%d,%d rel=%d,%d dot=%d\n",
						(int)v->index.base(), (int)TileX(tile), (int)TileY(tile), (int)front_dir,
						(int)TileX(tile_rev), (int)TileY(tile_rev), rel_x, rel_y,
						rel_x * delta.x + rel_y * delta.y);
				fclose(dbg);
			}
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
	bool logged = false;     ///< Whether the skipped-call probe was already written for this episode.
	/** R3R (第 272 轮, C-6): which of the four destination gates (GROUP/FIT/TARGET/ORDER)
	 * rejected at least one candidate during the last real search. Zero means "nothing was
	 * rejected", i.e. the empty result came from an empty reachable area instead. */
	uint32_t last_rej_mask = 0;
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
	if (!st.logged) {
		/* R3R probe: the full search was skipped by the throttle above. Logged
		 * once per throttle episode so a waiting locomotive does not flood the
		 * log it is supposed to stop flooding. */
		st.logged = true;
		FILE *dbg = R3RFopenDbg("a");
		if (dbg != nullptr) {
			fprintf(dbg, "CPL-SKIP veh=%d tile=%d,%d orderType=%d dontReserve=%d retryIn=%u holdAge=%u\n",
					(int)v->index.base(), (int)TileX(v->tile), (int)TileY(v->tile),
					(int)v->current_order.GetType(), (int)dont_reserve,
					(uint)(R3R_COUPLE_SCAN_RETRY - age), (uint)age);
			fclose(dbg);
		}
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
		st.last_rej_mask = 0;
	} else {
		st.holding = true;
		st.empty_tick = (uint64_t)_tick_counter;
		st.logged = false;
		/* R3R (第 272 轮, C-6): the destination gates counted rejections during this
		 * very search (R3RCoupleScanReset() clears them just before it starts), so an
		 * empty result with a non-zero mask means candidates *were* seen and refused,
		 * as opposed to "there is simply no consist within reach". */
		st.last_rej_mask = 0;
		for (uint i = 0; i < 4; i++) {
			if (_r3r_cpl_scan_rej[i] != 0) st.last_rej_mask |= (1U << i);
		}
	}
}

/**
 * R3R (第 272 轮, C-6): how the destination gates rejected candidates in the last
 * real (non-throttled) couple search of @p v.
 * @param v The locomotive whose last search is in question (nullptr safe).
 * @return Bit mask, bit 0 = GROUP, 1 = FIT, 2 = TARGET, 3 = ORDER; 0 = nothing rejected.
 */
uint32_t R3RCoupleSearchRejectMask(const Train *v)
{
	if (v == nullptr) return 0;
	const auto it = _r3r_couple_scan_state.find((int)v->index.base());
	return (it != _r3r_couple_scan_state.end()) ? it->second.last_rej_mask : 0;
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

	{
		FILE *dbg = R3RFopenDbg("a");
		if (dbg != nullptr) {
			fprintf(dbg, "CPL-ENTRY veh=%d tile=%d,%d orderType=%d dontReserve=%d\n",
					(int)v->index.base(), (int)TileX(v->tile), (int)TileY(v->tile),
					(int)v->current_order.GetType(), (int)dont_reserve);
			fclose(dbg);
		}
	}
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
