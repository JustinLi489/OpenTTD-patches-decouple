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
inline uint32_t _r3r_cpl_scan_rej[4] = { 0, 0, 0, 0 }; ///< GROUP / FIT / TARGET / ORDER
inline uint32_t _r3r_cpl_scan_acc[2] = { 0, 0 };       ///< car-only / waiting
inline uint32_t _r3r_cpl_scan_accwp = 0;      ///< R3R 第 276 轮：等待区路点被接受为终点（截断模式）
inline uint32_t _r3r_cpl_scan_last_tile = 0;  ///< 最近扫到的车底所在格（TileIndex::base()）
inline int _r3r_cpl_scan_last_veh = -1;       ///< 最近扫到的车底链头车号（-1=无）
inline int _r3r_cpl_scan_last_why = -1;       ///< -1=无 / 0..3=rej / 4=acc-car / 5=acc-wait / 6=cut-wp
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
	_r3r_cpl_scan_rej[0] = _r3r_cpl_scan_rej[1] = _r3r_cpl_scan_rej[2] = _r3r_cpl_scan_rej[3] = 0;
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
	static const char * const why_names[7] = { "GROUP", "FIT", "TARGET", "ORDER", "acc-car", "acc-wait", "cut-wp" };
	const bool has_last = (_r3r_cpl_scan_last_why >= 0);
	const char *why = (has_last && _r3r_cpl_scan_last_why < 7) ? why_names[_r3r_cpl_scan_last_why] : "none";
	/* R3R (第 255 轮, P0)：补 tick= 与 nodes=。tick 是同步 tick（30/s），有了它才能把
	 * "一次挂接在 YAPF 里烧掉多少节点"与"挂车总耗时多久"直接相减；nodes = closed + enter
	 * 是本次搜索真正走过的节点规模（enter 含 PfCalcEstimate 的重复访问，closed 只算扩展），
	 * 二者一起才能回答"大地图上一次搜索是不是 O(地图)"。 */
	fprintf(dbg, "CPL-SCAN tick=%u veh=%d found=%d closed=%u limit=%u nodes=%u enter=%u noplat=%u nores=%u noconsist=%u consist=%u "
			"rej=%u/%u/%u/%u acc=%u/%u accwp=%u last=%d,%d lastveh=%d why=%s\n",
			(unsigned)tick, veh, found, closed, limit, (unsigned)nodes,
			(unsigned)_r3r_cpl_scan_enter, (unsigned)_r3r_cpl_scan_noplat,
			(unsigned)_r3r_cpl_scan_nores, (unsigned)_r3r_cpl_scan_noconsist,
			(unsigned)_r3r_cpl_scan_consist,
			(unsigned)_r3r_cpl_scan_rej[0], (unsigned)_r3r_cpl_scan_rej[1],
			(unsigned)_r3r_cpl_scan_rej[2], (unsigned)_r3r_cpl_scan_rej[3],
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
				this->dest_trackdirs = GetTileTrackdirBits(this->dest_tile, TRANSPORT_RAIL, 0);
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

public:
	void SetDestination(const Train *v)
	{
		this->dest_order.AssignOrder(v->current_order);
		this->CYapfDestinationRailBase::SetDestination(v);
		this->dest_tile = (v->dest_tile == INVALID_TILE) ? TileIndex{} : v->dest_tile;
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

	/* R3R (第 276 轮, KI-429): 等待区路点截断。
	 *
	 * 玩家口径（Q-276-4 拍板）：机车前往 GOTO_COUPLE 的途中，只要还没到"可以
	 * 延伸预留"的状态，整条 PBS 预留就只允许订到**等待区路点**为止，绝不允许
	 * 一路铺到（甚至越过）目标车底。截断点因此天然落在一个合法停车位上，不会
	 * 再停在道岔格里（KI-429 的 FSCP tile=258,233）。
	 *
	 * 判定入口在 yapf_rail.cpp 的 FindNearestCoupleTrain()：由
	 * R3RShouldCutCoupleAtWaitPoint()（train_cmd.cpp）决定本次寻路要不要截断，
	 * 每 tick 重算 ⇒ A 停稳后自动解除，机车这才去订到 A 的那一段。
	 *
	 * 默认 false：非挂接寻路（其它 YAPF 用途不经过这里）与"已放行"的挂接寻路
	 * 逐字不变。 */
	bool couple_wait_cut = false;

	/* 截断模式下唯一允许的终点：等待区路点的 StationID + 1（0 = 不限定）。
	 *
	 * 必须点名而不是"任一路点都算"：线路上可能还有别的路点（含刚驶过的那个），
	 * 若一律接受，YAPF 的最小代价落点就可能变成身后的路点，把机车往回引。 */
	uint32_t couple_wait_cut_wp = 0;

	void SetCoupleWaitCut(bool cut, uint32_t wait_wp)
	{
		this->couple_wait_cut = cut;
		this->couple_wait_cut_wp = wait_wp;
	}

	/** @copydoc CYapfBaseT::PfDetectDestinationTileFunc */
	inline bool PfDetectDestination(TileIndex tile, Trackdir td)
	{
		/* R3R (第 236 轮, KI-344): 无条件清点本次扫描，见文件头的 census 说明。 */
		++_r3r_cpl_scan_enter;

		/* R3R (第 276 轮, KI-429): 截断模式——本次寻路只认等待区路点作终点。
		 *
		 * 其它格（含站台、车库、以及"别的路点"）一律否决，这样 YAPF 的落点
		 * 必定是路点、回溯/预留必定止于路点。
		 *
		 * 路点格恒是单轴站台格（GetRailStationTrackBits() = AxisToTrackBits(axis)），
		 * 所以这里不额外判"是否单轨"——多轨路点在游戏里不存在。 */
		if (this->couple_wait_cut) {
			if (!IsRailWaypointTile(tile) ||
					(this->couple_wait_cut_wp != 0 && (uint32_t)GetStationIndex(tile).base() + 1 != this->couple_wait_cut_wp)) {
				++_r3r_cpl_scan_noplat;
				return false;
			}
			++_r3r_cpl_scan_accwp;
			_r3r_cpl_scan_last_tile = tile.base();
			_r3r_cpl_scan_last_why = 6;
			/* 一次扫描里路点可能被反复探测（含多格路点），只在第一格打一行。 */
			if (_r3r_cpl_scan_accwp == 1) {
				FILE *dbg = R3RFopenDbg("a");
				if (dbg != nullptr) {
					fprintf(dbg, "PFD-ACCEPT kind=waitwp tile=%d,%d\n", TileX(tile), TileY(tile));
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
		static uint32_t r3r_dest_rej_seen[4] = { 0, 0, 0, 0 };
		auto r3r_dest_reject = [](int why, TileIndex tile, const Train *t) {
			static const char * const names[4] = { "PFD-REJ-GROUP", "PFD-REJ-FIT", "PFD-REJ-TARGET", "PFD-REJ-ORDER" };
			if (why < 0 || why >= 4) return;
			/* R3R (第 236 轮, KI-344): 计入本次扫描汇总 —— 必须在节流 return 之前，
			 * 否则 found=0 现场只会看到 1/64 的样本。 */
			++_r3r_cpl_scan_rej[why];
			_r3r_cpl_scan_last_tile = tile.base();
			_r3r_cpl_scan_last_veh = (int)t->index.base();
			_r3r_cpl_scan_last_why = why;
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
