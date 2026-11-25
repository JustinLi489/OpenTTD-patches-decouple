/*
 * This file is part of OpenTTD.
 * OpenTTD is free software; you can redistribute it and/or modify it under the terms of the GNU General Public License as published by the Free Software Foundation, version 2.
 * OpenTTD is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 * See the GNU General Public License for more details. You should have received a copy of the GNU General Public License along with OpenTTD. If not, see <https://www.gnu.org/licenses/old-licenses/gpl-2.0>.
 */

/** @file yapf_destrail.hpp Determining the destination for rail vehicles. */

#ifndef YAPF_DESTRAIL_HPP
#define YAPF_DESTRAIL_HPP

#include "../../train.h"
#include "../../vehicle_func.h"
#include "../pathfinder_func.h"
#include "../pathfinder_type.h"
#include "../../r3r_perf.h"
#include "../../couple_group.h"

/* R3R (第 236 轮, KI-344): 挂接目的地扫描的"无条件清点"(census)。
 *
 * 起因（第 234 轮日志取证留下的唯一缺口）：PFD-ACCEPT 与 PFD-REJ-* 都是**条件打印**
 * ——只有当 YAPF 探到站台格、格上又确实扫出一个车底、并且判据走到了相应分支时才会
 * 出声。于是 `CPL-PATHFOUND found=0` 的现场在日志里永远是静默的，两种截然相反的
 * 成因无法区分：
 *   (a) 等待链根本不在场：YAPF 没探到任何站台格，或探到了但格上没有合法车底；
 *   (b) 等待链在场但被三道闸门之一拒掉（GROUP / FIT / TARGET / ORDER）。
 * 本探针把"一次 FindNearestCoupleTrain"的整次扫描做成**恰好一行**汇总（与既有
 * CPL-PATHFOUND 同频，故不额外刷屏），并在行尾回放最近一次扫到的车底（格 + 车号 +
 * 结局），使 (a)(b) 在当前日志里一眼可分。全程只读、零行为变更。
 *
 * 计数口径：enter 是 PfDetectDestination 的调用次数，**节点级**而非格级 —— YAPF 在
 * 节点扩展与 PfCalcEstimate 里各调一次，故 enter ≈ 2×实际参与判定的节点数。各分支
 * 计数同样按调用次数累计，只用于同一次寻路内部的相对比较，不要当"格数"读。 */
inline uint32_t _r3r_cpl_scan_enter = 0;     ///< PfDetectDestination 调用次数（节点级）
inline uint32_t _r3r_cpl_scan_noplat = 0;    ///< 早退：不是站台/车库格
inline uint32_t _r3r_cpl_scan_nores = 0;     ///< 早退：站台格但没有预留位且非站台分支
inline uint32_t _r3r_cpl_scan_noconsist = 0; ///< 早退：格合格但格上扫不出合法车底
inline uint32_t _r3r_cpl_scan_consist = 0;   ///< 扫到车底，进入三道闸门
inline uint32_t _r3r_cpl_scan_rej[5] = { 0, 0, 0, 0, 0 }; ///< GROUP / FIT / TARGET / ORDER / LINE
inline uint32_t _r3r_cpl_scan_acc[2] = { 0, 0 };       ///< car-only / waiting
inline uint32_t _r3r_cpl_scan_accwp = 0;      ///< R3R 第 276 轮：等待区路点被接受为终点（截断模式）
inline uint32_t _r3r_cpl_scan_last_tile = 0;  ///< 最近扫到的车底所在格（TileIndex::base()）
inline int _r3r_cpl_scan_last_veh = -1;       ///< 最近扫到的车底链头车号（-1=无）
/* R3R（第 501 轮, S2）：新增 LINE 一类（rej[4]），为了不打乱旧日志的判读口径，
 * 它在 last_why 里固定映射到 7（0..3 仍是原四类 rej，4/5/6 仍是 acc-car/acc-wait/cut-wp）。 */
inline int _r3r_cpl_scan_last_why = -1;       ///< -1=无 / 0..3=rej / 4=acc-car / 5=acc-wait / 6=cut-wp / 7=LINE
/* R3R（第 330 轮取证，只读，KI-446）：noconsist 早退的格坐标 dump 配额。
 * noconsist 是"搜索走到了合格站台/车库格、但格上扫不出车底"，它的坐标集合就是
 * 搜索树的**落点分布**——用来判定第二次搜索到底有没有进入 y=15 那一行（27-G）。
 * 每次扫描最多 8 行（`_nocdump`，随 Reset 清零），全局最多 240 行（`_nocdump_total`，
 * 跨扫描累计、不随 Reset 清零），避免每 tick found=0 时刷屏。 */
inline uint32_t _r3r_cpl_scan_nocdump = 0;
inline uint32_t _r3r_cpl_scan_nocdump_total = 0;

/** 每次挂接寻路开始前清零（调用点：CYapfTrainRailT::FindNearestCoupleTrain）。 */
inline void R3RCoupleScanReset()
{
	_r3r_cpl_scan_enter = 0;
	_r3r_cpl_scan_noplat = 0;
	_r3r_cpl_scan_nores = 0;
	_r3r_cpl_scan_noconsist = 0;
	_r3r_cpl_scan_consist = 0;
	_r3r_cpl_scan_rej[0] = _r3r_cpl_scan_rej[1] = _r3r_cpl_scan_rej[2] = _r3r_cpl_scan_rej[3] = _r3r_cpl_scan_rej[4] = 0;
	_r3r_cpl_scan_acc[0] = _r3r_cpl_scan_acc[1] = 0;
	_r3r_cpl_scan_accwp = 0;
	_r3r_cpl_scan_last_tile = 0;
	_r3r_cpl_scan_last_veh = -1;
	_r3r_cpl_scan_last_why = -1;
	_r3r_cpl_scan_nocdump = 0;
}

/** 每次挂接寻路结束后无条件打一行（found 为 YAPF 的最终结果）。
 *  closed / limit 取自 YAPF 自身的节点统计：closed == limit != 0 表示"搜索被节点上限
 *  截断"，此时 found=0 只说明没搜完，**不能**据此断言"候选不在场"；只有 closed < limit
 *  （或 limit == 0 无限）才是"真的穷尽了可达空间"。 */
inline void R3RCoupleScanDump(int veh, int found, uint closed, uint limit, uint nodes, uint32_t tick)
{
	FILE *dbg = R3RFopenDbg("a");
	if (dbg == nullptr) return;
	static const char * const why_names[8] = { "GROUP", "FIT", "TARGET", "ORDER", "acc-car", "acc-wait", "cut-wp", "LINE" };
	const bool has_last = (_r3r_cpl_scan_last_why >= 0);
	const char *why = (has_last && _r3r_cpl_scan_last_why < 8) ? why_names[_r3r_cpl_scan_last_why] : "none";
	/* R3R (第 255 轮, P0)：补 tick= 与 nodes=。tick 是同步 tick（30/s），有了它才能把
	 * "一次挂接在 YAPF 里烧掉多少节点"与"挂车总耗时多久"直接相减；nodes = closed + enter
	 * 是本次搜索真正走过的节点规模（enter 含 PfCalcEstimate 的重复访问，closed 只算扩展），
	 * 二者一起才能回答"大地图上一次搜索是不是 O(地图)"。 */
	fprintf(dbg, "CPL-SCAN tick=%u veh=%d found=%d closed=%u limit=%u nodes=%u enter=%u noplat=%u nores=%u noconsist=%u consist=%u "
			"rej=%u/%u/%u/%u/%u acc=%u/%u accwp=%u last=%d,%d lastveh=%d why=%s\n",
			(unsigned)tick, veh, found, closed, limit, (unsigned)nodes,
			(unsigned)_r3r_cpl_scan_enter, (unsigned)_r3r_cpl_scan_noplat,
			(unsigned)_r3r_cpl_scan_nores, (unsigned)_r3r_cpl_scan_noconsist,
			(unsigned)_r3r_cpl_scan_consist,
			(unsigned)_r3r_cpl_scan_rej[0], (unsigned)_r3r_cpl_scan_rej[1],
			(unsigned)_r3r_cpl_scan_rej[2], (unsigned)_r3r_cpl_scan_rej[3],
			(unsigned)_r3r_cpl_scan_rej[4],
			(unsigned)_r3r_cpl_scan_acc[0], (unsigned)_r3r_cpl_scan_acc[1],
			(unsigned)_r3r_cpl_scan_accwp,
			has_last ? (int)TileX(TileIndex(_r3r_cpl_scan_last_tile)) : -1,
			has_last ? (int)TileY(TileIndex(_r3r_cpl_scan_last_tile)) : -1,
			_r3r_cpl_scan_last_veh, why);
	fclose(dbg);
}

class CYapfDestinationRailBase {
protected:
	RailTypes compatible_railtypes;

public:
	void SetDestination(const Train *v, bool override_rail_type = false)
	{
		this->compatible_railtypes = v->compatible_railtypes;
		if (override_rail_type) this->compatible_railtypes.Set(GetAllCompatibleRailTypes(v->railtypes));
	}

	bool IsCompatibleRailType(RailType rt)
	{
		return this->compatible_railtypes.Test(rt);
	}

	RailTypes GetCompatibleRailTypes() const
	{
		return this->compatible_railtypes;
	}

	/* R3R: 会合点断段开关的默认实现。
	 * 只有挂接寻路（CYapfDestinationTrainRailT）会覆盖它；其余 rail 目的地类型
	 * （任意车库 / 任意安全位 / 到站）必须走这里返回 false，保持上游行为。 */
	bool R3RMeetActive() const { return false; }
};

template <class Types>
class CYapfDestinationAnyDepotRailT : public CYapfDestinationRailBase {
public:
	typedef typename Types::Tpf Tpf; ///< the pathfinder class (derived from THIS class)
	typedef typename Types::NodeList::Item Node; ///< this will be our node type
	typedef typename Node::Key Key; ///< key to hash tables

	/** @copydoc CYapfBaseT::Yapf */
	Tpf &Yapf()
	{
		return *static_cast<Tpf *>(this);
	}

	/** @copydoc CYapfBaseT::PfDetectDestinationFunc */
	inline bool PfDetectDestination(Node &n)
	{
		return this->PfDetectDestination(n.GetLastTile(), n.GetLastTrackdir());
	}

	/** @copydoc CYapfBaseT::PfDetectDestinationTileFunc */
	inline bool PfDetectDestination(TileIndex tile, [[maybe_unused]] Trackdir td)
	{
		return IsRailDepotTile(tile);
	}

	/** @copydoc CYapfBaseT::PfCalcEstimateFunc */
	inline bool PfCalcEstimate(Node &n)
	{
		n.estimate = n.cost;
		return true;
	}

	inline int TeleportCost(TileIndex cur_tile, TileIndex prev_tile)
	{
		return 0;
	}
};

template <class Types>
class CYapfDestinationAnySafeTileRailT : public CYapfDestinationRailBase {
public:
	typedef typename Types::Tpf Tpf; ///< the pathfinder class (derived from THIS class)
	typedef typename Types::NodeList::Item Node; ///< this will be our node type
	typedef typename Node::Key Key; ///< key to hash tables
	typedef typename Types::TrackFollower TrackFollower; ///< TrackFollower. Need to typedef for gcc 2.95

	/** @copydoc CYapfBaseT::Yapf */
	Tpf &Yapf()
	{
		return *static_cast<Tpf *>(this);
	}

	/** @copydoc CYapfBaseT::PfDetectDestinationFunc */
	inline bool PfDetectDestination(Node &n)
	{
		return this->PfDetectDestination(n.GetLastTile(), n.GetLastTrackdir());
	}

	/** @copydoc CYapfBaseT::PfDetectDestinationTileFunc */
	inline bool PfDetectDestination(TileIndex tile, Trackdir td)
	{
		return IsSafeWaitingPosition(Yapf().GetVehicle(), tile, td, true, !TrackFollower::Allow90degTurns()) &&
				IsWaitingPositionFree(Yapf().GetVehicle(), tile, td, !TrackFollower::Allow90degTurns());
	}

	/** @copydoc CYapfBaseT::PfCalcEstimateFunc */
	inline bool PfCalcEstimate(Node &n)
	{
		n.estimate = n.cost;
		return true;
	}

	inline int TeleportCost(TileIndex cur_tile, TileIndex prev_tile)
	{
		return 0;
	}
};

template <class Types>
class CYapfDestinationTileOrStationRailT : public CYapfDestinationRailBase {
public:
	typedef typename Types::Tpf Tpf; ///< the pathfinder class (derived from THIS class)
	typedef typename Types::NodeList::Item Node; ///< this will be our node type
	typedef typename Node::Key Key; ///< key to hash tables

protected:
	TileIndex dest_tile;
	TrackdirBits dest_trackdirs;
	StationID dest_station_id;
	bool any_depot;

	/* R3R: station yard (站场). 只在 SetDestination() 那一刻算一次；之后每节点
	 * 只做一次二分查找。绝不每 tick 遍历站台。 */
	uint16_t dest_yard_sel;                   ///< R3R: 订单的目的场（0=整站）
	bool dest_yard_active;                    ///< R3R: 是否启用场地过滤
	std::vector<TileIndex> dest_yard_tiles;   ///< R3R: 允许停靠的 tile（升序、无重复）

	/** @copydoc CYapfBaseT::Yapf */
	Tpf &Yapf()
	{
		return *static_cast<Tpf *>(this);
	}

public:
	void SetDestination(const Train *v)
	{
		this->any_depot = false;
		this->dest_yard_sel = Station::R3R_YARD_NONE;
		this->dest_yard_active = false;
		this->dest_yard_tiles.clear();
		switch (v->current_order.GetType()) {
			case OT_GOTO_WAYPOINT:
				if (!Waypoint::Get(v->current_order.GetDestination().ToStationID())->IsSingleTile()) {
					/* In case of 'complex' waypoints we need to do a look
					 * ahead. This look ahead messes a bit about, which
					 * means that it 'corrupts' the cache. To prevent this
					 * we disable caching when we're looking for a complex
					 * waypoint. */
					Yapf().DisableCache(true);
				}
				[[fallthrough]];

			case OT_GOTO_STATION:
				this->dest_tile = CalcClosestStationTile(v->current_order.GetDestination().ToStationID(), v->GetMovingFront()->tile, v->current_order.IsType(OT_GOTO_STATION) ? StationType::Rail : StationType::RailWaypoint);
				this->dest_station_id = v->current_order.GetDestination().ToStationID();
				this->dest_trackdirs = INVALID_TRACKDIR_BIT;
				break;

			case OT_GOTO_DEPOT:
				if (v->current_order.GetDepotActionType() & ODATFB_NEAREST_DEPOT) {
					this->any_depot = true;
				}
				[[fallthrough]];

			default:
				this->dest_tile = (v->dest_tile == INVALID_TILE) ? TileIndex{} : v->dest_tile;
				this->dest_station_id = StationID::Invalid();
				this->dest_trackdirs = GetTileTrackdirBits(this->dest_tile, TransportType::Rail, 0);
				break;
		}

		/* R3R: station yard (站场) —— 整个寻路只在这里遍历一次站点数据。
		 * 场为空 / 车站没有任何场 / 场里没有有效 tile ⇒ 完全按原生整站处理。 */
		if (v->current_order.IsType(OT_GOTO_STATION) && this->dest_station_id != StationID::Invalid()) {
			const uint16_t yard = v->current_order.GetR3RYard();
			if (yard != Station::R3R_YARD_NONE) {
				const Station *st = Station::GetIfValid(this->dest_station_id);
				if (st != nullptr && st->R3RHasAnyYard()) {
					R3RCollectYardDestinationTiles(st, yard, this->dest_yard_tiles);
					this->dest_yard_sel = yard;
					this->dest_yard_active = !this->dest_yard_tiles.empty();
				}
			}
		}

		this->CYapfDestinationRailBase::SetDestination(v);
	}

	/** @copydoc CYapfBaseT::PfDetectDestinationFunc */
	inline bool PfDetectDestination(Node &n)
	{
		return this->PfDetectDestination(n.GetLastTile(), n.GetLastTrackdir());
	}

	/** @copydoc CYapfBaseT::PfDetectDestinationTileFunc */
	inline bool PfDetectDestination(TileIndex tile, Trackdir td)
	{
		if (this->dest_station_id != StationID::Invalid()) {
			if (!HasStationTileRail(tile)
					|| (GetStationIndex(tile) != this->dest_station_id)
					|| (GetRailStationTrack(tile) != TrackdirToTrack(td))) {
				return false;
			}
			/* R3R: 场地过滤。dest_yard_tiles 恒为升序，故此处是 O(log n) 二分。
			 * 注意：平台是否空闲/被预留不参与到达判定（与原版一致），列车照常
			 * 进站排队；场地只缩小「允许停靠的 tile 集合」。 */
			if (this->dest_yard_active && !std::binary_search(this->dest_yard_tiles.begin(), this->dest_yard_tiles.end(), tile)) {
				return false;
			}
			return true;
		}

		if (this->any_depot) {
			return IsRailDepotTile(tile);
		}

		return (tile == this->dest_tile) && HasTrackdir(this->dest_trackdirs, td);
	}

	/** R3R（第 504 轮, KI-676）：会合点断段是否启用。
	 *  默认关闭（普通 rail 寻路保持原行为）；挂接寻路在快照成立时覆盖它为 true，
	 *  让 yapf_costrail.hpp 的段推进逐格问终点、并在命中处把段截断。 */
	bool R3RMeetActive() const { return false; }

	/** @copydoc CYapfBaseT::PfCalcEstimateFunc */
	inline bool PfCalcEstimate(Node &n)
	{
		if (this->PfDetectDestination(n)) {
			n.estimate = n.cost;
			return true;
		}

		n.estimate = n.cost + OctileDistanceCost(n.GetLastTile(), n.GetLastTrackdir(), this->dest_tile);
		assert(n.estimate >= n.parent->estimate);
		return true;
	}

	inline int TeleportCost(TileIndex cur_tile, TileIndex prev_tile)
	{
		auto calculate_distance_cost = [&](TileIndex t, int d_adjust) -> int {
			int x1 = 2 * TileX(t);
			int y1 = 2 * TileY(t);
			int x2 = 2 * TileX(this->dest_tile);
			int y2 = 2 * TileY(this->dest_tile);
			int dx = abs(x1 - x2) + d_adjust;
			int dy = abs(y1 - y2);
			int dmin = std::min(dx, dy) + d_adjust; // up to 2x track exit dir tile offsets in opposite directions
			int dxy = abs(dx - dy) + d_adjust; // "
			return dmin * YAPF_TILE_CORNER_LENGTH + (dxy - 1) * (YAPF_TILE_LENGTH / 2);
		};
		return std::max<int>(0, calculate_distance_cost(prev_tile, 8) - calculate_distance_cost(cur_tile, 0));
	}
};

/**
 * Destination: a waiting consist (R3R consist-group free-wagon chain).
 * The pathfinder searches for the nearest platform/depot tile whose reserved
 * train is a consist waiting to be coupled onto.
 */
template <class Types>
class CYapfDestinationTrainRailT : public CYapfDestinationRailBase {
public:
	typedef typename Types::Tpf Tpf;              ///< the pathfinder class (derived from THIS class)
	typedef typename Types::NodeList::Item Node; ///< this will be our node type
	typedef typename Node::Key Key;               ///< key to hash tables

protected:
	Tpf &Yapf()
	{
		return *static_cast<Tpf *>(this);
	}

	TileIndex dest_tile; ///< heuristic destination (the order's station/depot).

protected:
	Order dest_order; ///< Copy of the locomotive's GOTO_COUPLE order (pxp-decouple reference).

	/* R3R（第 502 轮, S2b, KI-676）：静态终点快照。
	 *
	 * 前提（玩家 2026-11-24 拍板）：挂接寻路的被动方是**静态等待**的 —— 只要配对锁
	 * 建立并存在，主动方要去的那个"排"就是固定的。于是 SetDestination() 里算一次
	 * 「车底停在哪一排 + 订单约束过不过」，之后每个节点只做 O(1) 比较，不再
	 * 逐格扫站台、逐格反查预留、逐格重跑四次闸门。
	 *
	 * 快照失效（车底没了/被移走/订单变了/主动方配对锁改指）时 snap_valid 置假，
	 * PfDetectDestination() 回落到原来的动态扫描链 —— 保证行为不回归。 */
	bool snap_valid = false;                    ///< 快照可用（配对锁成立 + 车底在站台 + 订单约束已评估）
	VehicleID snap_veh = VehicleID::Invalid();  ///< 车底链头
	TileIndex snap_tile = INVALID_TILE;         ///< 车底链头所在格（复验用）
	StationID snap_stn = StationID::Invalid();  ///< 车底所在车站
	Axis snap_axis = Axis::X;                   ///< 车底所在站台轴
	uint16_t snap_row = 0;                      ///< 排号：轴 X 时 = TileY，轴 Y 时 = TileX
	bool snap_car_only = false;                 ///< 车底是纯车底链（accept kind=car-only）还是等待段
	/* R3R（第 503 轮, S5, KI-676）：会合点格 = 车底近端「预留所在格」按轨型再往外一格。
	 *
	 * 玩家 2026-11-25 口径（原话）：「车底近端那一端的预留所在格，再按轨型往外一格」。
	 * 现场：车底 1424..1437,1874，站台近端末格 1438 是车底站台整格预留的边界，
	 * 会合点 = 1439,1874 —— 该格既无车底的车也无车底的预留，机车可以正常开进去。
	 * INVALID_TILE 表示这一端算不出（车底不在站台/两端同格），此时终点判定退回
	 * 原来的「同排」口径，行为与改动前逐字一致。 */
	TileIndex snap_meet_tile = INVALID_TILE;
	int snap_rej = -1;                          ///< -1=可接受；0..3 = GROUP/FIT/TARGET/ORDER

public:
	void SetDestination(const Train *v)
	{
		this->dest_order.AssignOrder(v->current_order);
		this->CYapfDestinationRailBase::SetDestination(v);
		this->dest_tile = (v->dest_tile == INVALID_TILE) ? TileIndex{} : v->dest_tile;
		this->R3RTakeStaticTargetSnapshot(v);
		/* R3R（第 502 轮, S2c, KI-676）：启发与接受同源。
		 *
		 * 快照成立时，A* 真正的目标不是"订单目的地附近随便哪一格"（v->dest_tile
		 * 是 CalcClosestStationTile 的近似落点，可能落在同站别的排上），而是车底
		 * 实际停着的这一格。把 dest_tile 钉到 snap_tile 后：
		 *   ① 启发值对整排的估计更紧，展开数下降（S2b 已把接受判据收到同一排）；
		 *   ② 与 S4 的"tg 用快照终点"共用同一个锚，不再有两套目标口径。
		 * 快照失效时 dest_tile 保持订单目的地，行为与改动前逐字相同。 */
		/* R3R（第 503 轮, S5, KI-676）：锚点改到会合点格。原来钉在 snap_tile（车底
		 * 链头，现场 1424）会把 A* 强烈拉向背离机车的那一端 —— 这是 20/23 条现场
		 * 停在远端 1424 的直接推手。现在启发锚 = 接受格 = 会合点，三者同源。 */
		{
			const TileIndex meet = this->R3RStaticTargetTile();
			if (meet != INVALID_TILE) this->dest_tile = meet;
		}
	}

	/** R3R（第 502 轮, S2b）：在 SetDestination() 时一次性评估终点，见 snap_* 字段说明。
	 *
	 * 不查预留（reservation）：车底的身份与位置直接来自配对锁 r3r_couple_target，
	 * 比"谁占着这一格"稳 —— 后者在机车自己的预留盖住站台格时还得特判（见
	 * PfDetectDestination() 里那段 "the consist is never GOTO_COUPLE" 补丁）。 */
	void R3RTakeStaticTargetSnapshot(const Train *v)
	{
		snap_valid = false;
		snap_rej = -1;
		snap_car_only = false;
		snap_veh = VehicleID::Invalid();
		snap_tile = INVALID_TILE;
		snap_stn = StationID::Invalid();
		snap_row = 0;
		snap_meet_tile = INVALID_TILE;

		/* depot 挂接一律不动：继续走原来的扫描分支（玩家 Q-501-2 口径）。 */
		if (dest_order.GetCoupleIsDepot()) return;

		const Train *act = Train::From(v);
		const Train *ch = (act != nullptr) ? Train::From(act->First()) : nullptr;
		if (ch == nullptr) return;
		if (ch->r3r_couple_target == VehicleID::Invalid()) return;
		if (!Train::IsValidID(ch->r3r_couple_target)) return;
		Train *t = Train::Get(ch->r3r_couple_target);
		if (t == nullptr || !t->IsPrimaryVehicle()) return;
		if (!t->current_order.IsType(OT_WAIT_COUPLE)) return;
		/* 车底不在站台格（库内/连接轨上）：快照没有"排"可锚，交回动态扫描。 */
		if (!IsRailStationTile(t->tile)) return;
		/* 配对锁必须双向成立 —— 与扫描层、回溯安全位、到点闸门同一条判据。 */
		if (!R3RCouplePairMatches(act, t)) return;

		snap_veh = t->index;
		snap_tile = t->tile;
		snap_stn = GetStationIndex(t->tile);
		snap_axis = GetRailStationAxis(t->tile);
		snap_row = (snap_axis == Axis::X) ? TileY(t->tile) : TileX(t->tile);
		snap_car_only = R3RIsCarOnlyFormation(t);
		/* R3R（第 503 轮, S5, KI-676）：会合点 = 近端（离机车那一端）预留边界格再往外一格。
		 * 近端由曼哈顿距离判定，与回溯阶段的口径一致；两端同格或近端不在站台时
		 * R3RComputeMeetTile() 返回 INVALID_TILE，终点退回「同排」口径。 */
		{
			const TileIndex cons_head = t->tile;
			const TileIndex cons_tail = t->Last()->tile;
			const bool head_is_near = DistanceManhattan(v->tile, cons_head) <= DistanceManhattan(v->tile, cons_tail);
			snap_meet_tile = R3RComputeMeetTile(head_is_near ? cons_head : cons_tail,
					head_is_near ? cons_tail : cons_head);
		}
		snap_valid = true;

		/* 订单约束一次性评估：这些都是"同一列固定车底"的属性，逐节点重算没有意义。 */
		if (!R3RCoupleAllowed(act, t)) {
			snap_rej = 0;
		} else if (!TrainFitStation(t)) {
			snap_rej = 1;
		} else if (!R3RIsCoupleTarget(t)) {
			snap_rej = 2;
		} else if (!(CheckOrderLoad(t) && CheckOrderCargoType(t) && CheckNumberOfWagons(t))) {
			snap_rej = 3;
		}

		/* R3R（第 503 轮续, S5-dbg, KI-676）：会合点自身的唯一出口。
		 *
		 * 为什么需要它：`CPL-HEAD-STATIC` 只在 path_found==true 之后才打印，于是
		 * found=0 的现场（本次 279 次 rej[4]、consist=0、acc=0）看不到 meet 到底是
		 * 哪一格，无法区分"算错格"和"算对但不可达"。本行只读、节流前 12 次。 */
		if (snap_valid && snap_meet_tile != INVALID_TILE) {
			static uint32_t r3r_snap_meet_seen = 0;
			if (++r3r_snap_meet_seen <= 12) {
				FILE *dbg = R3RFopenDbg("a");
				if (dbg != nullptr) {
					fprintf(dbg, "CPL-SNAP veh=%d near=%d,%d meet=%d,%d head=%d,%d tail=%d,%d rej=%d\n",
							(int)v->index.base(),
							(int)TileX(DistanceManhattan(v->tile, t->tile) <= DistanceManhattan(v->tile, t->Last()->tile) ? t->tile : t->Last()->tile),
							(int)TileY(DistanceManhattan(v->tile, t->tile) <= DistanceManhattan(v->tile, t->Last()->tile) ? t->tile : t->Last()->tile),
							(int)TileX(snap_meet_tile), (int)TileY(snap_meet_tile),
							(int)TileX(t->tile), (int)TileY(t->tile),
							(int)TileX(t->Last()->tile), (int)TileY(t->Last()->tile),
							snap_rej);
					fclose(dbg);
				}
			}
		}
	}

	/** R3R（第 502 轮, S2b）：快照是否仍然成立（O(1) 复验，不含任何扫描）。 */
	bool R3RStaticTargetValid() const
	{
		if (!snap_valid) return false;
		if (!Train::IsValidID(snap_veh)) return false;
		const Train *t = Train::Get(snap_veh);
		if (t == nullptr || !t->IsPrimaryVehicle()) return false;
		if (!t->current_order.IsType(OT_WAIT_COUPLE)) return false;
		/* 车底被移走（另一个 tick/另一条命令）⇒ 快照的"排"不再可信。 */
		if (t->tile != snap_tile) return false;
		return true;
	}

	/** R3R（第 502 轮, S2b）：tile 是否落在快照锚定的那一排上（同站 + 同轴 + 同排号）。 */
	bool R3RStaticIsSameLine(TileIndex tile) const
	{
		if (!IsRailStationTile(tile)) return true; // 库格/非站台格：放行，由动态分支处理
		if (GetStationIndex(tile) != snap_stn) return false;
		if (GetRailStationAxis(tile) != snap_axis) return false;
		return ((snap_axis == Axis::X) ? TileY(tile) : TileX(tile)) == snap_row;
	}

	/* R3R（第 502 轮, S4, KI-676）：把静态终点交回 yapf_rail.cpp 的回溯/摸路阶段。
	 *
	 * 这三个访问器都是 O(1)，不含任何世界扫描。命中时回溯阶段不再需要
	 * "反查预留主人 + 排除自己 + 站台横向扫描" 那一整套动态端点解析 ——
	 * 会合点由配对锁唯一确定。 */
	VehicleID R3RStaticTargetVehicle() const
	{
		return this->R3RStaticTargetValid() ? snap_veh : VehicleID::Invalid();
	}

	TileIndex R3RStaticTargetTile() const
	{
		if (!this->R3RStaticTargetValid()) return INVALID_TILE;
		/* R3R（第 503 轮, S5, KI-676）：有会合点就用会合点（站台外一格），
		 * 算不出（库内挂接 / 车底两端同格 / 近端不在站台）时退回链头格，
		 * 由 yapf_rail.cpp 的回溯阶段继续按原来的"取近端"逻辑处理。 */
		return (snap_meet_tile != INVALID_TILE) ? snap_meet_tile : snap_tile;
	}

	/** 会合点格（车底近端预留边界再往外一格）；INVALID_TILE = 本形态没有会合点。 */
	TileIndex R3RStaticMeetTile() const
	{
		if (!this->R3RStaticTargetValid()) return INVALID_TILE;
		return snap_meet_tile;
	}

	/** R3R（第 504 轮, KI-676）：会合点断段开关（覆盖基类的默认 false）。
	 *  段会把无信号连续轨道一次吞掉，会合点落在段中间就永远判不到；
	 *  打开后 cost 计算会逐格判定并截断段，同时禁用跨寻路的段缓存。 */
	bool R3RMeetActive() const
	{
		return this->R3RStaticMeetTile() != INVALID_TILE;
	}

	/** 车底另一端（链尾）所在格，供"取近端"做 O(1) 比较。 */
	TileIndex R3RStaticTargetFarTile() const
	{
		if (!this->R3RStaticTargetValid()) return INVALID_TILE;
		const Train *t = Train::Get(snap_veh);
		return (t == nullptr) ? INVALID_TILE : t->Last()->tile;
	}

	/** @copydoc CYapfBaseT::PfDetectDestinationFunc */
	inline bool PfDetectDestination(Node &n)
	{
		return this->PfDetectDestination(n.GetLastTile(), n.GetLastTrackdir());
	}

	/** Check the load-state requirement from the GOTO_COUPLE order. */
	bool CheckOrderLoad(const Train *t) const
	{
		switch (dest_order.GetCoupleLoad()) {
			case ODC_ANY: return true;
			case ODC_IS_EMPTY: return t->cargo.StoredCount() == 0;
			case ODC_IS_FULL: return t->cargo.StoredCount() == t->cargo_cap;
			default: NOT_REACHED();
		}
	}

	/** Check the cargo-type requirement from the GOTO_COUPLE order. */
	bool CheckOrderCargoType(const Train *t) const
	{
		if (!dest_order.HasCoupleCargoType()) return true;
		CargoType cargo_type = dest_order.GetCoupleCargoType();
		for (const Train *v = t; v != nullptr; v = v->Next()) {
			if (v->cargo_type == cargo_type && v->cargo_cap > 0) return true;
		}
		return false;
	}

	/** Check the wagon-count requirement from the GOTO_COUPLE order. */
	bool CheckNumberOfWagons(const Train *t) const
	{
		if (dest_order.GetNumCouple() == 0) return true;
		return dest_order.GetNumCouple() == CountVehiclesInChain(t);
	}

	/** R3R（第 501 轮, S2, KI-676）：接受格与车底实际停靠格是否在同一条到发线上。
	 *
	 * 同一条到发线 = 同一个车站 + 同一个站台轴 + 同一排（轴为 X 时比 TileY，轴为 Y
	 * 时比 TileX）。IsCompatibleTrainStationTile() 只覆盖前两项，**排号必须自己比**
	 * —— 平行站台误判（车底在 y=1874，YAPF 判到达落在 1424,1875/1876）就出在缺这一项。
	 *
	 * 任一格不是站台格时返回 true（depot 挂接、车底链头停在站台外的连接轨上）：
	 * 这道闸只收紧站台挂接，不对其它形态引入新拒绝。 */
	static bool R3RSameParkingLine(TileIndex tile, const Train *t)
	{
		const TileIndex parked = t->tile;
		if (!IsRailStationTile(tile) || !IsRailStationTile(parked)) return true;
		if (GetStationIndex(tile) != GetStationIndex(parked)) return false;
		const Axis axis = GetRailStationAxis(tile);
		if (axis != GetRailStationAxis(parked)) return false;
		return (axis == Axis::X) ? (TileY(tile) == TileY(parked)) : (TileX(tile) == TileX(parked));
	}

	/** R3R（第 503 轮, S5, KI-676）：求「车底近端预留所在格再往外一格」。
	 *
	 * 车底在站台上停稳后，它自己的站台整格预留覆盖到站台近端末格（现场 1438），
	 * 那一格仍属于车底，机车开不进去；再往外一格（1439）才是合法的会合点。
	 *
	 * 求法：以近端车格为起点、沿站台轴朝着「背离车底」的方向逐格前进，第一个
	 * 不再是同一站台格的格即为会合点（O(站台剩余长度)，上限 64 格）。
	 *
	 * @param near_tile 离机车近的那一端车节所在格
	 * @param far_tile  另一端车节所在格，只用来定方向
	 * @return 会合点格；近端不在站台、两端同格或方向判不出时返回 INVALID_TILE
	 *         （调用方据此退回原来的「同排」终点口径） */
	static TileIndex R3RComputeMeetTile(TileIndex near_tile, TileIndex far_tile)
	{
		if (near_tile == far_tile) return INVALID_TILE;
		if (!IsRailStationTile(near_tile)) return INVALID_TILE;
		const Axis axis = GetRailStationAxis(near_tile);
		const int near_c = (axis == Axis::X) ? (int)TileX(near_tile) : (int)TileY(near_tile);
		const int far_c = (axis == Axis::X) ? (int)TileX(far_tile) : (int)TileY(far_tile);
		if (near_c == far_c) return INVALID_TILE;
		/* R3R (KI-687, 第 518 轮, 2026-11-26): 紧贴车底近端车格**一格**，不再走到
		 * 站台外第一格。
		 *
		 * 旧实现沿站台轴一路前进，把车底近端与站台端点之间的**空格**也算进"车底
		 * 的预留"，于是在站台外第一格收尾。现场（build\R3R_debug.log，第 517 轮
		 * exe，2026-11-26 03:26）：
		 *   CPL-PAIRLOCK ... tgt=18 tgtTile=1424,1874 tgtLen=216
		 *   CPL-SNAP veh=15 near=1437,1874 meet=1439,1874 head=1424,1874 tail=1437,1874
		 *   CPL-RES-OK veh=15 total=45 booked=45 tiles=…;1440,1874/0;1439,1874/0
		 * 车底车尾在 1437（车底占 1424..1437），会合点却算出 1439 ⇒ 机车停在 1439
		 * 时与车尾 1437 之间隔着一个空的站台格 1438，两车钩头相距一格，
		 * 本场 COUPLE-OK 出现 0 次。挂接要求两车钩头相邻，所以会合点必须是
		 * 「近端车格沿轴向背离车底的那一格」= 1438。
		 *
		 * 站台整格预留允许同站台按段分配（车底 1424..1437，机车进 1438 不重叠），
		 * 这一格可订；若它不可通行（非铁轨/非法格）才退回旧的 INVALID_TILE 口径，
		 * 由调用方退回「同排」判据，行为与改动前一致。 */
		const int step_sign = (near_c > far_c) ? 1 : -1;
		const int step_dx = (axis == Axis::X) ? step_sign : 0;
		const int step_dy = (axis == Axis::X) ? 0 : step_sign;
		const TileIndex meet = TileAddWrap(near_tile, step_dx, step_dy);
		if (meet == INVALID_TILE) return INVALID_TILE;
		if (!IsTileType(meet, TileType::Railway) && !IsRailStationTile(meet)) return INVALID_TILE;
		return meet;
	}

	/* R3R (第 109 轮, 2026-09-24): CheckOrderSlot() 已废除。
	 *
	 * 玩家口径：「废除常规分组和路签对挂接的影响」——候选车底是否落在机车那条
	 * GOTO_COUPLE 命令所选的路签（trace restrict slot）里，不再参与目的地判定。
	 *
	 * 废除理由（原实现的两条硬伤）：①order 上的路签槽选择器在上游就只能读不能写
	 * （没有任何 SetCoupleSlot 调用点、订单窗口也没有对应控件），所以它永远读到
	 * xdata 默认值 0，判定退化成靠 GetIfValid() 的兜底空转；②真正会拒绝的那种情形
	 * ——槽里恰好停着别的车底——在本 mod 的挂接流程下根本不成立：解挂流程从不把等待
	 * 中的 WAIT_COUPLE 车底自动登记进机车的槽（占用只可能来自地图的 tracerestrict
	 * 布设），于是「有占用但不是我」几乎总是误判，直接把唯一的等待车底从目的地集合里
	 * 剔掉，表现就是机车到了站台却找不到挂接目标、不铺预留、沿站台乱跑。
	 *
	 * 挂接候选的判据现在只剩三道：R3RCoupleAllowed()（目的地 + 公司边界）、
	 * TrainFitStation()（站台长度）、R3RIsCoupleTarget()（★段头 + WAIT_COUPLE），
	 * 再加装货/货种/车数三条命令属性。机车自身路径的预留仍照常执行 tracerestrict
	 * 程序（含 slot acquire/release），那属于寻路与信号系统，与挂接判定无关。 */

	/* R3R (第 386 轮, KI-551): 原先这里是「等待区路点截断」的
	 * couple_wait_cut / couple_wait_cut_wp 成员与 SetCoupleWaitCut()。玩家
	 * 2026-11-02 拍板整机制删除（现场表现为无条件把预留在路点截止），故连同
	 * PfDetectDestination() 里的截断分支一并移除；终点判定回到普通口径。 */

	/** @copydoc CYapfBaseT::PfDetectDestinationTileFunc */
	inline bool PfDetectDestination(TileIndex tile, Trackdir td)
	{
		/* R3R (第 236 轮, KI-344): 无条件清点本次扫描，见文件头的 census 说明。 */
		++_r3r_cpl_scan_enter;

		/* R3R (第 386 轮, KI-551): 「等待区路点截断」分支已删除。原先
		 * couple_wait_cut 为真时这里只接受等待区路点作终点（并打
		 * PFD-ACCEPT kind=waitwp），使回溯/预留止于路点。现在一律走下面的
		 * 普通终点判定。 */

		/* R3R（第 503 轮, S5, KI-676）：会合点精确判据 —— 必须放在"只收站台/库格"之前。
		 *
		 * 会合点（站台近端预留边界格再往外一格）通常**不是站台格**（现场 1439,1874
		 * 在站台外），先被下面那道站台闸拒掉就永远到不了。这里一旦有会合点，就把它
		 * 当作唯一合法终点，其余格一律 reject：
		 *   车底 1424..1437 + 站台到 1438 ⇒ 会合点 1439 ⇒ 机车从东侧进来即停。
		 * 这也是 20/23 条现场停在远端 1424、3/23 停在近端车格 1437 的直接解法。
		 * 会合点算不出（库内挂接/车底两端同格）时整段跳过，行为与改动前逐字一致。 */
		if (snap_valid && snap_meet_tile != INVALID_TILE && R3RStaticTargetValid()) {
			/* R3R (KI-687, 第 518 轮续, 2026-11-26): 会合点落在**连续站台段内部**时的兜底。
			 *
			 * TrackFollower 进入站台格会一步跳段（follow_track.hpp 的 CanEnterNewTile:
			 * new_tile += step*(length-1)），该段内部每一格都不会作为节点被探测 ⇒
			 * 精确判据 `tile == snap_meet_tile` 永不可达。现场（build\R3R_debug.log，
			 * 第 518 轮 exe，2026-11-26 03:42）：
			 *   :1420 CPL-SNAP veh=15 meet=1438,1874 head=1424,1874 tail=1437,1874
			 *   :1463 PFX-GEO t=1438,1874 ... stn=1   （1439 才是站台外）
			 *   :2084 CPL-SCAN found=0 consist=0 rej=0/0/0/0/761 why=LINE last=1406,1869
			 * 即 1438 从未被探测，761 个被检查格全部落在别的排/别的站。
			 * 兜底口径 = 「同站 + 同轴 + 同排」，与 S2b 逐字一致：
			 * 平行线（现场 y=1873/1875）仍被拒，KI-684 不回退；
			 * 物理停位与预设终点仍由 R3RStaticTargetTile()/回溯阶段收紧。 */
			const bool meet_unreachable_in_platform = IsRailStationTile(snap_meet_tile) &&
					IsRailStationTile(tile) &&
					GetStationIndex(snap_meet_tile) == snap_stn &&
					GetRailStationAxis(snap_meet_tile) == snap_axis &&
					R3RStaticIsSameLine(tile);
			if (tile != snap_meet_tile && !meet_unreachable_in_platform) {
				/* R3R（第 503 轮续, S5-dbg, KI-676）：记录搜索到底探了哪些格。
				 * 只读、节流前 40 次（即第一次找路的前 40 个探测格），用于判断
				 * "meet 算错"还是"meet 算对但搜索没走到"。 */
				static uint32_t r3r_meet_miss_seen = 0;
				if (++r3r_meet_miss_seen <= 40) {
					FILE *dbg = R3RFopenDbg("a");
					if (dbg != nullptr) {
						fprintf(dbg, "PFD-MEETMISS tile=%d,%d meet=%d,%d n=%u\n",
								(int)TileX(tile), (int)TileY(tile),
								(int)TileX(snap_meet_tile), (int)TileY(snap_meet_tile),
								(unsigned)r3r_meet_miss_seen);
						fclose(dbg);
					}
				}
				/* 计入 LINE 一类（异排/非会合点），保持 census 口径不新增桶。 */
				++_r3r_cpl_scan_rej[4];
				_r3r_cpl_scan_last_tile = tile.base();
				_r3r_cpl_scan_last_veh = (int)snap_veh.base();
				_r3r_cpl_scan_last_why = 7;
				return false;
			}
			if (snap_rej >= 0) {
				++_r3r_cpl_scan_rej[snap_rej];
				_r3r_cpl_scan_last_tile = tile.base();
				_r3r_cpl_scan_last_veh = (int)snap_veh.base();
				_r3r_cpl_scan_last_why = snap_rej;
				return false;
			}
			++_r3r_cpl_scan_consist;
			++_r3r_cpl_scan_acc[snap_car_only ? 0 : 1];
			_r3r_cpl_scan_last_tile = tile.base();
			_r3r_cpl_scan_last_veh = (int)snap_veh.base();
			_r3r_cpl_scan_last_why = snap_car_only ? 4 : 5;
			static uint32_t r3r_meet_acc_seen = 0;
			if (++r3r_meet_acc_seen <= 16) {
				FILE *dbg = R3RFopenDbg("a");
				if (dbg != nullptr) {
					fprintf(dbg, "PFD-MEET kind=%s tile=%d,%d t=%d n=%u\n",
							snap_car_only ? "car-only" : "waiting",
							(int)TileX(tile), (int)TileY(tile),
							(int)snap_veh.base(), (unsigned)r3r_meet_acc_seen);
					fclose(dbg);
				}
			}
			return true;
		}

		/* Only platform or depot tiles qualify. */
		if (!IsRailStationTile(tile) && !IsRailDepotTile(tile)) {
			++_r3r_cpl_scan_noplat;
			return false;
		}

		/* R3R（第 502 轮, S2b, KI-676）：静态终点判定（O(1)）。
		 *
		 * 前提：被动方是静态等待的，配对锁一建立，「车底停在哪一排」就固定了。
		 * 于是 SetDestination() 已把「排 + 订单约束」算进 snap_*，这里只做比较：
		 *   异排 -> reject(LINE)；同排但订单约束不过 -> reject(snap_rej)；否则 accept。
		 * 不再逐格扫站台、不再反查预留、不再逐格重跑四次闸门。
		 *
		 * 只对站台格生效：库格（depot 挂接）仍走下面的动态扫描，行为逐字不变。
		 * 快照失效（车底被移走/订单变了/配对锁改指）时自动回落到动态链。 */
		if (snap_valid && IsRailStationTile(tile) && R3RStaticTargetValid()) {
			if (!R3RStaticIsSameLine(tile)) {
				++_r3r_cpl_scan_consist;
				++_r3r_cpl_scan_rej[4];
				_r3r_cpl_scan_last_tile = tile.base();
				_r3r_cpl_scan_last_veh = (int)snap_veh.base();
				_r3r_cpl_scan_last_why = 7;
				return false;
			}
			if (snap_rej >= 0) {
				++_r3r_cpl_scan_consist;
				++_r3r_cpl_scan_rej[snap_rej];
				_r3r_cpl_scan_last_tile = tile.base();
				_r3r_cpl_scan_last_veh = (int)snap_veh.base();
				_r3r_cpl_scan_last_why = snap_rej;
				return false;
			}
			/* 不能复用下面的 r3r_dest_accept lambda（它在本函数后半段才定义），
			 * 故自带计数与一行节流日志；acc/rej 口径与旧统计合并，不倒退。 */
			++_r3r_cpl_scan_consist;
			++_r3r_cpl_scan_acc[snap_car_only ? 0 : 1];
			_r3r_cpl_scan_last_tile = tile.base();
			_r3r_cpl_scan_last_veh = (int)snap_veh.base();
			_r3r_cpl_scan_last_why = snap_car_only ? 4 : 5;
			static uint32_t r3r_static_acc_seen = 0;
			if (++r3r_static_acc_seen <= 16) {
				FILE *dbg = R3RFopenDbg("a");
				if (dbg != nullptr) {
					fprintf(dbg, "PFD-STATIC kind=%s tile=%d,%d t=%d row=%d stn=%d n=%u\n",
							snap_car_only ? "car-only" : "waiting",
							(int)TileX(tile), (int)TileY(tile), (int)snap_veh.base(),
							(int)snap_row, (int)snap_stn.base(), (unsigned)r3r_static_acc_seen);
					fclose(dbg);
				}
			}
			return true;
		}

		/* R3R (KI-165): the GOTO_COUPLE order's own destination is enforced one
		 * level up, in R3RCoupleAllowed() (couple_group.cpp) -- called at the end
		 * of this function. Keeping it there means the destination set of THIS
		 * pathfinder, the back-walk safety test in yapf_rail.cpp and the arrival
		 * gate in train_cmd.cpp all judge candidates by the same rule. */

		TrackdirBits tdb = TrackdirToTrackdirBits(td);
		bool has_res = HasReservedTracks(tile, TrackdirBitsToTrackBits(tdb));
		if (!has_res && !IsRailStationTile(tile)) {
			++_r3r_cpl_scan_nores;
			return false;
		}

		Train *t = nullptr;
		if (has_res) {
			t = GetTrainForReservation(tile, TrackdirToTrack(td));
			/* R3R: the coupling loco's own reservation may cover the consist's
			 * platform tile, making GetTrainForReservation return the loco itself
			 * and mask the consist (couple pathfinder found=0 right next to it).
			 * The consist is never GOTO_COUPLE - ignore such reservations. */
			if (t != nullptr && t->IsPrimaryVehicle() && t->current_order.IsType(OT_GOTO_COUPLE)) t = nullptr;
		}
		if (t == nullptr && IsRailStationTile(tile)) {
			/* R3R: the whole-platform reservation may leave reserved tiles that
			 * have no consist on them (the consist is elsewhere on the platform).
			 * Scan the rest of the platform for a consist before giving up. */
			const Track track = TrackdirToTrack(td);
			const TileIndexDiff delta = TileOffsByAxis(GetRailStationAxis(tile));
			for (int dir = 0; dir < 2 && t == nullptr; ++dir) {
				TileIndex st = tile + (dir == 0 ? delta : -delta);
				while (IsCompatibleTrainStationTile(st, tile)) {
					if (HasReservedTracks(st, TrackToTrackBits(track))) {
						t = GetTrainForReservation(st, track);
						if (t != nullptr && t->IsPrimaryVehicle() && t->current_order.IsType(OT_GOTO_COUPLE)) t = nullptr;
						if (t == nullptr) {
							for (Train *tr : VehiclesOnTile<VehicleType::Train>(st)) {
								Train *first = tr->First();
								/* R3R (KI-62): only a real segment waiting to be coupled
								 * onto may be picked up here. A loose wagon chain ("散链")
								 * or a consist holding some other order must not become the
								 * couple destination. */
								if (tr->IsFrontEngine() && R3RIsCoupleTarget(first)) {
									t = first; break;
								}
							}
						}
						if (t != nullptr) break;
					}
					st += (dir == 0 ? delta : -delta);
				}
			}
		}
		if (t == nullptr) {
			/* R3R (第 236 轮, KI-344): 站台格上没有任何合法车底 —— 这是"等待链不在场"
			 * 的第一个可辨形态（另有 noplat / nores 两种更早的早退）。 */
			++_r3r_cpl_scan_noconsist;
			/* R3R（第 330 轮取证，只读，KI-446）：把"走到了合格格但扫不出车底"的
			 * 坐标记下来。这一组坐标 = 搜索树在站台/车库上的落点分布，用来判定
			 * 第二次搜索（found=0）是否真的没进入 y=15 那一行（27-G）。
			 * 配额见 _r3r_cpl_scan_nocdump 的声明处。 */
			if (_r3r_cpl_scan_nocdump < 8 && _r3r_cpl_scan_nocdump_total < 240) {
				++_r3r_cpl_scan_nocdump;
				++_r3r_cpl_scan_nocdump_total;
				FILE *dbg = R3RFopenDbg("a");
				if (dbg != nullptr) {
					fprintf(dbg, "PFD-NOCONSIST tile=%d,%d td=%d res=%d n=%u\n",
							(int)TileX(tile), (int)TileY(tile), (int)td, (int)has_res,
							(unsigned)_r3r_cpl_scan_noconsist);
					fclose(dbg);
				}
			}
			return false;
		}
		++_r3r_cpl_scan_consist;
		t = t->First();

		/* R3R (KI-194, 2026-09-24): the destination probe must not report a candidate
		 * BEFORE the gates have accepted it.
		 *
		 * The old accept-dump sat exactly here -- in front of the couple-group
		 * whitelist, TrainFitStation() and R3RIsCoupleTarget() below -- and printed
		 * every vehicle that merely had a reservation, so a purely REJECTED vehicle
		 * (a real train holding GOTO_STATION, wc=0, fit=1) read like a valid couple
		 * destination in the log and made a correct `found=0` look like a bug
		 * (KI-191: veh=27 in the 2026-09-23 log, veh=33/27 in the 2026-09-24 log).
		 *
		 * yapf_destrail.hpp cannot reuse R3RDbgEdge() (both the gate and its tag enum
		 * are private to train_cmd.cpp), so rejections are throttled by a tiny
		 * per-reason counter and accepts are printed one for one (they are rare and
		 * are the interesting case). */
		static uint32_t r3r_dest_rej_seen[5] = { 0, 0, 0, 0, 0 };
		auto r3r_dest_reject = [](int why, TileIndex tile, const Train *t) {
			static const char * const names[5] = { "PFD-REJ-GROUP", "PFD-REJ-FIT", "PFD-REJ-TARGET", "PFD-REJ-ORDER", "PFD-REJ-LINE" };
			if (why < 0 || why >= 5) return;
			/* R3R (第 236 轮, KI-344): 计入本次扫描汇总 —— 必须在节流 return 之前，
			 * 否则 found=0 现场只会看到 1/64 的样本。 */
			++_r3r_cpl_scan_rej[why];
			_r3r_cpl_scan_last_tile = tile.base();
			_r3r_cpl_scan_last_veh = (int)t->index.base();
			/* R3R（第 501 轮, S2）：LINE 是 rej[4]，但 last_why 为了让旧日志口径不变
			 * （4/5/6 = acc-car/acc-wait/cut-wp）映射到 7。 */
			_r3r_cpl_scan_last_why = (why == 4) ? 7 : why;
			if (++r3r_dest_rej_seen[why] % 64 != 0) return;
			FILE *dbg = R3RFopenDbg("a");
			if (dbg != nullptr) {
				fprintf(dbg, "%s tile=%d,%d t=%d ordType=%d wc=%d rej=%u\n", names[why],
						(int)TileX(tile), (int)TileY(tile), (int)t->index.base(),
						(int)t->current_order.GetType(),
						(int)(t->IsPrimaryVehicle() && t->current_order.IsType(OT_WAIT_COUPLE)),
						(unsigned)r3r_dest_rej_seen[why]);
				fclose(dbg);
			}
		};
		auto r3r_dest_accept = [](const char *kind, TileIndex tile, const Train *t) {
			/* R3R (第 236 轮, KI-344): 计入汇总；kind 首字母区分两种接受分支。 */
			const bool car_only = (kind[0] == 'c');
			++_r3r_cpl_scan_acc[car_only ? 0 : 1];
			_r3r_cpl_scan_last_tile = tile.base();
			_r3r_cpl_scan_last_veh = (int)t->index.base();
			_r3r_cpl_scan_last_why = car_only ? 4 : 5;
			FILE *dbg = R3RFopenDbg("a");
			if (dbg != nullptr) {
				fprintf(dbg, "PFD-ACCEPT kind=%s tile=%d,%d t=%d ordType=%d wc=%d\n", kind,
						(int)TileX(tile), (int)TileY(tile), (int)t->index.base(),
						(int)t->current_order.GetType(),
						(int)(t->IsPrimaryVehicle() && t->current_order.IsType(OT_WAIT_COUPLE)));
				fclose(dbg);
			}
		};

		/* R3R: couple group whitelist (step 5). A waiting consist whose segment
		 * belongs to a different couple group is not a candidate at all: it must
		 * vanish from the destination set (PfDetectDestination returns false) so the
		 * pathfinder keeps looking for a consist in the same group and otherwise
		 * ends up in the ordinary "no couple target" outcome. The rejection itself is
		 * reported by the throttled PFD-REJ-GROUP probe (see above): this runs for
		 * every station/depot tile of every path search, so it may not be logged one
		 * for one. */
		if (!R3RCoupleAllowed(Train::From(Yapf().GetVehicle()), t)) {
			/* R3R (第 289 轮, 只读取证): CPL-SCAN 的 rej[0]=3 已经证明拒绝来自
			 * R3RCoupleAllowed() 的第一个子判据，但「pair 不成立 / 目的地不在本车站 /
			 * 分组白名单」三种成因在日志里都只能推断，不能直读 —— 扫描层
			 * TrainCoupleHandler 用的是**不含 pair**的 R3RCoupleAllowedIgnoringPair()，
			 * 它能过、寻路层却全拒，本身就把嫌疑指向 pair-mismatch，但仍需铁证。
			 *
			 * 这一行把三个子判据的真值连同双方锁字段、停驶标志一次打出来：
			 *   pairOK=0        -> R3RCouplePairMatches() 假（锁为空/只有一半）
			 *   pairOK=1 ignOK=0 -> 目的地或分组；再看 tStn vs destStn / isDepot
			 *   aStop/tStop     -> 直接检验「停驶即清锁」那条机制是否在生效
			 * 限量 16 行/进程（BSS 静态计数），零行为变更。 */
			static uint32_t r3r_why_seen = 0;
			if (++r3r_why_seen <= 16) {
				const Train *r3r_act = Train::From(Yapf().GetVehicle());
				const Train *r3r_ch = (r3r_act != nullptr) ? Train::From(r3r_act->First()) : nullptr;
				const Train *r3r_th = Train::From(t->First());
				if (r3r_act != nullptr && r3r_ch != nullptr && r3r_th != nullptr) {
					const bool r3r_pair_ok = R3RCouplePairMatches(r3r_act, t);
					const bool r3r_ign_ok = R3RCoupleAllowedIgnoringPair(r3r_act, t);
					const Order &r3r_co = r3r_ch->current_order;
					const bool r3r_is_depot = r3r_co.GetCoupleIsDepot();
					const uint r3r_dest_stn = r3r_is_depot ? 0u : (uint)r3r_co.GetDestination().ToStationID().base();
					const uint r3r_a_stn = IsRailStationTile(r3r_ch->tile) ? (uint)GetStationIndex(r3r_ch->tile).base() : 0xFFFFu;
					const uint r3r_t_stn = IsRailStationTile(r3r_th->tile) ? (uint)GetStationIndex(r3r_th->tile).base() : 0xFFFFu;
					FILE *dbg = R3RFopenDbg("a");
					if (dbg != nullptr) {
						fprintf(dbg, "CPL-WHY node=%d,%d act=%d tgt=%d ch=%d th=%d pairTgt=%d pairReq=%d pairOK=%d ignOK=%d aStn=%d tStn=%d destStn=%d isDepot=%d aStop=%d tStop=%d n=%u\n",
								(int)TileX(tile), (int)TileY(tile),
								(int)r3r_act->index.base(), (int)t->index.base(),
								(int)r3r_ch->index.base(), (int)r3r_th->index.base(),
								(int)r3r_ch->r3r_couple_target.base(), (int)r3r_th->r3r_couple_requester.base(),
								(int)r3r_pair_ok, (int)r3r_ign_ok,
								(int)r3r_a_stn, (int)r3r_t_stn, (int)r3r_dest_stn, (int)r3r_is_depot,
								(int)r3r_act->vehstatus.Test(VehState::Stopped),
								(int)t->vehstatus.Test(VehState::Stopped), (unsigned)r3r_why_seen);
						fclose(dbg);
					}
				}
			}
			r3r_dest_reject(0, tile, t);
			return false;
		}

		/* R3R (pxp-decouple): the waiting formation must fit into its station. */
		if (!TrainFitStation(t)) {
			r3r_dest_reject(1, tile, t);
			return false;
		}

		/* R3R (KI-62): only a real segment that is waiting to be coupled onto is a
		 * couple destination. The segment-front marker is what tells a segment
		 * ("第 k/N 段") from a loose wagon chain ("散链") in the depot list, and a
		 * loose chain must never be aimed at. This replaces the old car-only
		 * short-circuit, which let any zero-power formation through regardless of
		 * which order it held. */
		if (!R3RIsCoupleTarget(t)) {
			r3r_dest_reject(2, tile, t);
			return false;
		}

		/* R3R（第 501 轮, S2, KI-676）：站台挂接的接受格必须落在**车底实际停着的
		 * 那一排到发线**上（同站 + 同轴 + 同排号）。
		 *
		 * 基线判据只要求"这一格能扫出一个等待车底"，而整站台预留会把同站其它排的格
		 * 也覆盖进来，于是 YAPF 把平行线判成到达（现场：车底在 y=1874，判到达落在
		 * 1424,1875 / 1424,1876），回溯与预留一并铺到平行线上，机车永远追不上耦合端。
		 * IsCompatibleTrainStationTile() 只保证同站同轴、不含排号，所以整条扫描链都
		 * 看不出这个差别 —— 这道闸就是补上排号。
		 *
		 * 为什么不钉成单格：TrackFollower 进站台一步跳到末端，钉单格会让中间格永远
		 * 判不到（第 475 轮 endhit=0 死锁）。接受集合仍是整排站台格，跳过站台后的
		 * 落点仍在排内，故不会复发。
		 *
		 * 代价（玩家 2026-11-24 已拍板接受）：车底那一排不可达时，结果从"错铺到平行
		 * 线"变成 found=0（挂接失败），而不是错误铺轨。
		 * depot 挂接一律不动；判不了排（非站台格）一律放行。 */
		if (!dest_order.GetCoupleIsDepot() && !R3RSameParkingLine(tile, t)) {
			r3r_dest_reject(4, tile, t);
			return false;
		}

		/* Target 1: a car-only formation (zero-power train front) waiting to be
		 * coupled. */
		if (R3RIsCarOnlyFormation(t)) {
			r3r_dest_accept("car-only", tile, t);
			return true;
		}

		/* Target 2 (pxp-decouple reference): a segment which is an independent chain
		 * head declaring WAIT_COUPLE. That is exactly what R3RIsCoupleTarget() (the
		 * gate above) has already established -- this comment used to claim the
		 * WAIT_COUPLE test lived in this branch, which read as if any plain running
		 * train with a matching load were a valid destination (KI-194 evaluation,
		 * 2026-09-24: the implementation was correct and the comment was misplaced;
		 * the accept-dump in front of the gates is what made it look otherwise).
		 * The explicit IsType() check below is a redundant belt-and-braces guard, not
		 * a behaviour change. On top of it the candidate must satisfy the load / cargo
		 * type / wagon count requirements of the locomotive's GOTO_COUPLE order.
		 * (The trace-restrict-slot requirement used to be a fourth condition here;
		 * it was abolished in 第 109 轮 -- see the note where CheckOrderSlot() was.) */
		if (t->IsPrimaryVehicle() && t->current_order.IsType(OT_WAIT_COUPLE)) {
			if (CheckOrderLoad(t) && CheckOrderCargoType(t) &&
					CheckNumberOfWagons(t)) {
				r3r_dest_accept("waiting", tile, t);
				return true;
			}
			r3r_dest_reject(3, tile, t);
			return false;
		}

		return false;
	}

	/** @copydoc CYapfBaseT::PfCalcEstimateFunc */
	inline bool PfCalcEstimate(Node &n)
	{
		if (this->PfDetectDestination(n)) {
			n.estimate = n.cost;
			return true;
		}
		n.estimate = n.cost + OctileDistanceCost(n.GetLastTile(), n.GetLastTrackdir(), this->dest_tile);
		assert(n.estimate >= n.parent->estimate);
		return true;
	}

	inline int TeleportCost(TileIndex cur_tile, TileIndex prev_tile)
	{
		return 0;
	}
};

#endif /* YAPF_DESTRAIL_HPP */
