/*
 * This file is part of OpenTTD.
 * OpenTTD is free software; you can redistribute it and/or modify it under the terms of the GNU General Public License as published by the Free Software Foundation, version 2.
 * OpenTTD is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 * See the GNU General Public License for more details. You should have received a copy of the GNU General Public License along with OpenTTD. If not, see <https://www.gnu.org/licenses/old-licenses/gpl-2.0>.
 */

/** @file couple_group.cpp R3R: couple groups -- the hard whitelist for coupling. */

#include "stdafx.h"
#include "couple_group.h"

#include "train.h"
#include "vehicle_base.h"
#include "depot_map.h"
#include "station_map.h"
#include "group.h"
#include "r3r_perf.h"
#include "core/pool_func.hpp"

#include <string_view>

#include "safeguards.h"

CoupleGroupPool _couplegroup_pool("CoupleGroup");
INSTANTIATE_POOL_METHODS(CoupleGroup)

/**
 * R3R: find the vehicle which carries the couple group of the segment that
 * contains \a v.
 *
 * A segment is delimited by the SegmentFront marker; the leading segment of a
 * chain carries no marker of its own and belongs to the chain head instead.
 * The group is stored on that single vehicle only, so the group always travels
 * with its segment, however the chain is re-ordered later on (Q3).
 *
 * @param v Any vehicle of the segment, may be nullptr.
 * @return The vehicle owning the segment's group, or nullptr if \a v is nullptr.
 */
template <typename T>
static T *R3RGetCoupleGroupCarrier(T *v)
{
	if (v == nullptr) return nullptr;
	for (T *p = v; p != nullptr; p = p->Previous()) {
		if (p->IsSegmentFront()) return p;
	}
	return v->First();
}

/**
 * R3R: is \a v the vehicle which carries the couple group of its segment?
 * @param v Vehicle to test, may be nullptr.
 * @return whether \a v is a segment head.
 */
static bool R3RIsCoupleGroupCarrier(const Train *v)
{
	return v != nullptr && (v->Previous() == nullptr || v->IsSegmentFront());
}

/**
 * R3R: the vehicle of the segment which follows \a t.
 *
 * The segment ends before the next vehicle carrying the SegmentFront marker, so
 * a segment keeps all of its cars (and therefore its couple groups) even when
 * the chain is re-ordered by a coupling flip: only the order inside the segment
 * changes, never its vehicle set.
 *
 * @param t Vehicle to step from.
 * @return The next vehicle of the same segment, or nullptr at the segment end.
 */
template <typename T>
static T *R3RNextSegmentVehicle(T *t)
{
	if (t->Next() == nullptr) return nullptr;
	T *next = T::From(t->Next());
	return next->IsSegmentFront() ? nullptr : next;
}

/**
 * R3R: move the couple groups of a segment onto its head vehicle.
 *
 * The group travels with the segment (Q3), but the vehicle which heads the
 * segment may change (e.g. after a coupling flip) while the mask stays where it
 * was written. Reading unions the whole segment, so the assignment is never
 * lost; normalising before a write keeps the mask from spreading over the
 * segment and makes "remove" and "clear" deterministic.
 *
 * @param v Any vehicle of the segment.
 */
static void R3RNormaliseCoupleGroupsOfSegment(Train *v)
{
	Train *head = R3RGetCoupleGroupCarrier(v);
	if (head == nullptr) return;

	CoupleGroupMask groups = COUPLE_GROUP_MASK_NONE;
	for (Train *t = head; t != nullptr; t = R3RNextSegmentVehicle(t)) groups |= t->couple_groups;
	for (Train *t = head; t != nullptr; t = R3RNextSegmentVehicle(t)) t->couple_groups = COUPLE_GROUP_MASK_NONE;
	head->couple_groups = groups;
}

bool R3RIsValidCoupleGroup(CoupleGroupID group)
{
	return group != INVALID_COUPLE_GROUP && CoupleGroup::GetIfValid(group) != nullptr;
}

/**
 * R3R: drop the bits of \a mask which do not refer to an existing group.
 * @param mask Group set to sanitise.
 * @return The sanitised set.
 */
static CoupleGroupMask R3RSanitiseCoupleGroupMask(CoupleGroupMask mask)
{
	CoupleGroupMask result = COUPLE_GROUP_MASK_NONE;
	for (uint i = 0; i < R3R_COUPLE_GROUP_MASK_BITS; i++) {
		const CoupleGroupMask bit = CoupleGroupMask(1) << i;
		if ((mask & bit) != 0 && R3RIsValidCoupleGroup(CoupleGroupID(static_cast<uint16_t>(i)))) result |= bit;
	}
	return result;
}

bool R3RCoupleGroupMasksCompatible(CoupleGroupMask a, CoupleGroupMask b)
{
	/* Q2: every segment without an assigned group forms one single implicit
	 * group, so unassigned segments may couple amongst themselves, while an
	 * unassigned segment never couples with an explicitly assigned group. */
	if (a == COUPLE_GROUP_MASK_NONE || b == COUPLE_GROUP_MASK_NONE) return a == b;
	/* Q5: a segment in several groups couples with any partner sharing one. */
	return (a & b) != COUPLE_GROUP_MASK_NONE;
}

bool R3RCoupleGroupMasksAllowCrossCompany(CoupleGroupMask a, CoupleGroupMask b)
{
	const CoupleGroupMask shared = a & b;
	if (shared == COUPLE_GROUP_MASK_NONE) return false;

	for (uint i = 0; i < R3R_COUPLE_GROUP_MASK_BITS; i++) {
		const CoupleGroupMask bit = CoupleGroupMask(1) << i;
		if ((shared & bit) == 0) continue;
		const CoupleGroup *cg = CoupleGroup::GetIfValid(CoupleGroupID(static_cast<uint16_t>(i)));
		/* R3R (D6-①+D6-③): the two savegame bits which back the one "open for
		 * other companies" switch are always read as one, so a group written by
		 * an older build (which only knew one half) counts as open as well. */
		if (cg != nullptr && cg->AllowsOthers()) return true;
	}
	return false;
}

uint32_t R3RGetCoupleGroupFlags(CoupleGroupID group)
{
	const CoupleGroup *cg = CoupleGroup::GetIfValid(group);
	return cg != nullptr ? cg->flags : 0;
}

uint R3RSetCoupleGroupAllowOthers(CoupleGroupID group, bool allow_others)
{
	CoupleGroup *cg = CoupleGroup::GetIfValid(group);
	if (cg == nullptr) return 0;

	/* Both halves of the switch always move together (D6-①+D6-③): opening the
	 * group lets other companies join it *and* couples their segments across
	 * the company boundary, and each half on its own would be either useless
	 * (joining without being able to couple) or unreachable (coupling without
	 * anybody being able to join). See #CoupleGroup::CGF_ALLOW_OTHERS. */
	const uint32_t mask = static_cast<uint32_t>(CoupleGroup::CGF_ALLOW_OTHERS);

	if (allow_others) {
		cg->flags |= mask;
		return 0;
	}

	cg->flags &= ~mask;

	/* Withdrawing the authorisation also takes the foreign members out of the
	 * group: a group which is not open is invisible to the other companies,
	 * so a segment left behind would sit in a group its company can neither
	 * select nor leave anymore. Segments of the owner itself always stay, and
	 * nothing is decoupled here -- only the membership is withdrawn, so a chain
	 * which is already running keeps running. */
	const CoupleGroupMask bit = R3RCoupleGroupBit(group);
	if (bit == COUPLE_GROUP_MASK_NONE) return 0;

	uint evicted = 0;
	for (Train *t : Train::Iterate()) {
		/* Only the head of a segment carries the mask. */
		if (!R3RIsCoupleGroupCarrier(t)) continue;
		if (t->owner == cg->owner) continue;
		/* R3R (第 146 轮 / 需求叁改): the *stored* mask decides who has to leave --
		 * a segment which only reaches this group through its parent has nothing to
		 * clear here, it is handled by the detach below. */
		if ((R3RGetCoupleGroupsOfSegment(t) & bit) == COUPLE_GROUP_MASK_NONE) continue;
		R3RRemoveCoupleGroupFromSegment(t, group);
		evicted++;
	}

	/* R3R (第 146 轮 / 需求叁改): the hierarchy is a membership relation now, so a
	 * group which is closed again must also let go of the *foreign* sub groups hanging
	 * below it: they would keep inheriting this group through a parent they can no
	 * longer even see (so their owner could not remove that parent any more either).
	 * Only the direct children are detached, the tree below them is their owner's
	 * own business. */
	uint detached = 0;
	for (CoupleGroup *other : CoupleGroup::Iterate()) {
		if (other->owner == cg->owner || other->parent != group) continue;
		other->parent = INVALID_COUPLE_GROUP;
		detached++;
	}
	if (evicted != 0 || detached != 0) {
		R3RDbgWrite("CGRP-CLOSE group=%u evicted=%u detached=%u\n",
				(unsigned)group.base(), evicted, detached);
	}
	return evicted;
}

CoupleGroupMask R3RGetCoupleGroupsOfSegment(const Train *v)
{
	if (v == nullptr) return COUPLE_GROUP_MASK_NONE;
	const Train *head = R3RGetCoupleGroupCarrier(v);
	if (head == nullptr) return COUPLE_GROUP_MASK_NONE;

	CoupleGroupMask groups = COUPLE_GROUP_MASK_NONE;
	for (const Train *t = head; t != nullptr; t = R3RNextSegmentVehicle(t)) groups |= t->couple_groups;
	/* Dangling references (e.g. a hand edited savegame) behave like "no group". */
	return R3RSanitiseCoupleGroupMask(groups);
}

/**
 * R3R (第 146 轮 / 需求叁改): add the ancestors of every group in \a groups.
 *
 * The stored mask holds the groups a segment was explicitly put into; a segment
 * of the sub group 石头 whose parent is 滚木 is also a train of 滚木, so every
 * *read* which asks "is this segment in that group" adds the whole parent chain.
 */
static CoupleGroupMask R3RAddCoupleGroupAncestors(CoupleGroupMask groups)
{
	CoupleGroupMask result = groups;
	for (uint i = 0; i < R3R_COUPLE_GROUP_MASK_BITS; i++) {
		if ((groups & (CoupleGroupMask(1) << i)) == 0) continue;
		/* Walked defensively: a hand edited savegame may contain a cycle, which is
		 * also why R3RGetCoupleGroupDepth() caps its walk. */
		CoupleGroupID cur = R3RGetCoupleGroupParent(CoupleGroupID(static_cast<uint16_t>(i)));
		for (uint steps = 0; cur != INVALID_COUPLE_GROUP && steps < R3R_COUPLE_GROUP_MASK_BITS; steps++) {
			result |= R3RCoupleGroupBit(cur);
			cur = R3RGetCoupleGroupParent(cur);
		}
	}
	return result;
}

CoupleGroupMask R3RGetEffectiveCoupleGroupsOfSegment(const Train *v)
{
	return R3RAddCoupleGroupAncestors(R3RGetCoupleGroupsOfSegment(v));
}

void R3RAddCoupleGroupToSegment(Train *v, CoupleGroupID group)
{
	const CoupleGroupMask bit = R3RCoupleGroupBit(group);
	if (bit == COUPLE_GROUP_MASK_NONE) return;
	R3RNormaliseCoupleGroupsOfSegment(v);
	Train *head = R3RGetCoupleGroupCarrier(v);
	if (head != nullptr) head->couple_groups |= bit;
}

void R3RRemoveCoupleGroupFromSegment(Train *v, CoupleGroupID group)
{
	const CoupleGroupMask bit = R3RCoupleGroupBit(group);
	if (bit == COUPLE_GROUP_MASK_NONE) return;
	R3RNormaliseCoupleGroupsOfSegment(v);
	Train *head = R3RGetCoupleGroupCarrier(v);
	if (head != nullptr) head->couple_groups &= ~bit;
}

void R3RClearCoupleGroupsOfSegment(Train *v)
{
	R3RNormaliseCoupleGroupsOfSegment(v);
	Train *head = R3RGetCoupleGroupCarrier(v);
	if (head != nullptr) head->couple_groups = COUPLE_GROUP_MASK_NONE;
}

/**
 * R3R (KI-306, round 202): probe for the restored same-company group whitelist.
 *
 * The whitelist is only consulted when one of the two orders explicitly
 * declares a temporary couple group, so this is a rare path -- but it still
 * runs inside the couple pathfinder (three resolution levels), hence the
 * throttle: one line per (coupler,target) id pair, and only when the verdict
 * actually changes. Key = coupler<<16 | target; 64 pairs is plenty for the
 * handful of locomotives that can be trying to couple at any moment, and the
 * table is deliberately not saved (pure diagnostics).
 *
 * NOTE: do not name a parameter `cdecl` -- MSVC's windows headers
 * (`#define cdecl __cdecl`) turn it into the calling convention keyword and the
 * file stops compiling with C2059 __cdecl. `coupler_decl` / `target_decl` it is.
 */
static void R3RCoupleGateProbe(const Train *coupler, const Train *target, bool coupler_decl,
		bool target_decl, CoupleGroupMask cmask, CoupleGroupMask tmask, bool compatible)
{
	static uint32_t keys[64] = {};
	static bool vals[64] = {};
	static uint32_t used = 0;
	const uint32_t max_entries = (uint32_t)(sizeof(keys) / sizeof(keys[0]));

	const uint32_t key = (coupler->index.base() << 16) | (target->index.base() & 0xFFFF);
	for (uint32_t i = 0; i < used; i++) {
		if (keys[i] != key) continue;
		if (vals[i] == compatible) return; // unchanged verdict: stay quiet
		vals[i] = compatible;
		R3RDbgWrite("CG-GATE compatible=%d coupler=%d target=%d cdecl=%d tdecl=%d cmask=0x%X tmask=0x%X\n",
				(int)compatible, (int)coupler->index.base(), (int)target->index.base(),
				(int)coupler_decl, (int)target_decl, (unsigned)cmask, (unsigned)tmask);
		return;
	}
	if (used < max_entries) {
		keys[used] = key;
		vals[used] = compatible;
		used++;
	}
	R3RDbgWrite("CG-GATE compatible=%d coupler=%d target=%d cdecl=%d tdecl=%d cmask=0x%X tmask=0x%X\n",
			(int)compatible, (int)coupler->index.base(), (int)target->index.base(),
			(int)coupler_decl, (int)target_decl, (unsigned)cmask, (unsigned)tmask);
}

bool R3RCoupleAllowedIgnoringPair(const Train *coupler, const Train *target)
{
	if (coupler == nullptr || target == nullptr) return false;

	/* R3R (KI-165): the order's own destination is the first half of the permit.
	 *
	 * A GOTO_COUPLE order stores the station (or depot) the consist waits at --
	 * that is what "go to and couple" picks in the order window and what the
	 * order list prints ("go to and couple at <station>"). The destination was
	 * never consulted when candidates were resolved, so any platform or depot
	 * holding a waiting consist was accepted. Observed 2026-09-22: loco veh=0
	 * held GOTO_COUPLE station 0 (60,48-60,51) but was routed to station 2
	 * (60,58-60,61) and reported COUPLE-OK there, merely because the waiting
	 * consist stood at station 2 -- exactly the "it coupled where it has no
	 * order to couple" complaint.
	 *
	 * This function is the one place all three resolution levels ask: the couple
	 * pathfinder's destination test (yapf_destrail.hpp PfDetectDestination), the
	 * back-walk safety test which decides whose occupied tiles the couple path
	 * may pass through (yapf_rail.cpp CheckSafePositionOnNode), and the arrival
	 * gate in TrainCoupleHandler (R3RCanCoupleNow -> reject=dest-mismatch).
	 * Enforcing it here therefore keeps "the target the path was planned for" and
	 * "the coupling that is finally executed" in agreement. */
	const Order &order = coupler->current_order;
	if (order.IsType(OT_GOTO_COUPLE)) {
		const DestinationID dest = order.GetDestination();
		if (order.GetCoupleIsDepot()) {
			/* Depot order: only a candidate standing on a tile of the named depot
			 * is refused here. A candidate in a depot or on the approach track
			 * just outside the depot mouth (which is no longer a depot tile) keeps
			 * the old tile-agnostic behaviour -- that track is where a consist
			 * regularly waits, and a "couple at depot" order cannot name it.
			 *
			 * R3R (KI-288, round 188): 「非车库格一律放行」是个洞 —— 它把**车站
			 * 站台**也放了进来。观测 2026-10-01：机车 veh=26 执行
			 * GOTO_COUPLE→车库 1,11（DEPOT-ARR destTx=1 destTy=11 destDepot=1），
			 * 却停在车站格 4,11，把刚在**同一格**解下来的车底 23 挂上
			 * （COUPLE-OK loco=26 ... tx=4 ty=11）—— 命令写在车库、挂接点在车站。
			 * 站台永远不属于任何车库，所以这里是"目的地不匹配"的另一种形态，
			 * 与下面车站分支对称。车库门口那段接近轨道仍是普通轨道，不受影响。
			 * 见 train_cmd.cpp 的 R3RCoupleTargetAtOrderStation()（必须同步）。 */
			if (IsRailDepotTile(target->tile) && GetDepotIndex(target->tile) != dest.ToDepotID()) return false;
			if (IsRailStationTile(target->tile)) return false;
		} else {
			/* Station order: a candidate parked at some *other* station is not a
			 * candidate at all. Candidates in a depot, or off any permanent way,
			 * are untouched -- see R3RCoupleTargetAtOrderStation() in
			 * train_cmd.cpp, which labels the rejection and must stay in sync. */
			if (IsRailStationTile(target->tile) && GetStationIndex(target->tile) != dest.ToStationID()) return false;
		}
	}

	/* R3R (KI-170): 「临时挂接分组」。
	 *
	 * 玩家口径：机车 A 属于真分组"石头"，车底 B 属于真分组"滚木"；A 执行的那条
	 * 「前往挂接」命令带着属性"滚木"，于是 A 在命令执行期间"暂时同时属于滚木和
	 * 石头" —— 就是参与挂接的那个段的有效分组 = 它自己的真分组 ∪ 命令指定的那个
	 * 真分组。因此它挂得上滚木组的车底，而分组无交集的照样被白名单拒绝：这不是
	 * "万能放行"，只是临时多了一个真实身份。
	 * （第 109 轮曾把这道白名单整体废除，于是本段所述机制变成死代码；第 202 轮
	 * KI-306 已按"命令声明了临时分组才重新启用白名单"恢复，见下方闸门。）
	 *
	 * 这层身份完全由命令派生，不写进段的数据（段上仍然只有"石头"），命令一被推进掉
	 * （挂接成功，见 train_cmd.cpp 的 Couple() / CGRP-FAKE-DESTROY）就随之消失。
	 * 命令里的分组被删掉时按"不指定"处理（R3RIsValidCoupleGroup()），与没写属性等价。
	 *
	 * 跨公司那道独立闸门(D6-①)照常用合并后的分组集判定：临时并入一个开放的真分组
	 * 时效果与真的属于该组一致，并入一个不对外的组则依旧跨不过公司边界。 */
	/* R3R (第 146 轮 / 需求叁改): 用"有效集合"（含所有祖先组）判定：子组的车也属于父组。 */
	CoupleGroupMask coupler_groups = R3RGetEffectiveCoupleGroupsOfSegment(coupler);
	CoupleGroupMask target_groups = R3RGetEffectiveCoupleGroupsOfSegment(target);
	bool coupler_declared_temp = false;
	bool target_declared_temp = false;
	if (order.IsType(OT_GOTO_COUPLE)) {
		const CoupleGroupID temp_group = order.GetCoupleTempGroup();
		if (R3RIsValidCoupleGroup(temp_group)) {
			coupler_groups |= R3RCoupleGroupBit(temp_group);
			coupler_declared_temp = true;
		}
	}
	/* R3R (第 143 轮, KI-225): 等待挂接的车底也能在它的 WAIT_COUPLE 命令上声明一个
	 * 临时分组，语义与机车侧的 GOTO_COUPLE 对称——"我在等人按这个分组来接我"。
	 * 于是跨公司那道闸门（下面）能同时看见双方声明的分组：机车声明组 A、等待车底声明
	 * 组 B，只要 A 与 B 有交集（且该组对其它公司开放）就能跨公司挂上，不需要任何一方
	 * 真的把自己的段落进对方的真挂接分组里。
	 * 这里按整条等待链取（链头的命令），不受"只有承载命令的段"限制：等待的是整列车。
	 * 命令里的分组被删掉时按"不指定"处理（R3RIsValidCoupleGroup()）。 */
	if (target != nullptr) {
		const Train *wait_head = Train::From(target->First());
		if (wait_head != nullptr && wait_head->current_order.IsType(OT_WAIT_COUPLE)) {
			const CoupleGroupID wait_group = wait_head->current_order.GetCoupleTempGroup();
			if (R3RIsValidCoupleGroup(wait_group)) {
				target_groups |= R3RCoupleGroupBit(wait_group);
				target_declared_temp = true;
			}
		}
	}

	/* R3R (第 109 轮, 2026-09-24): 常规分组（真挂接分组）的白名单闸门已废除。
	 *
	 * 玩家口径：「废除常规分组和路签对挂接的影响」。原先这里要求
	 * R3RCoupleGroupMasksCompatible(机车段掩码, 车底段掩码) 为真，即两段的真分组
	 * 必须有交集，否则候选直接被判成"这里没有等待的车底"。这条闸门现在只按「公司在
	 * 不同公司时必须共用一个对外的分组」的公司边界（下面那段）保留，不再按同公司内
	 * 的分组归属否决候选。
	 *
	 * 废除理由：分组本是玩家用来组织车队的账目，把它当成挂接许可，会让"忘记把车底
	 * 加进同一分组"直接表现成机车到了站台却找不到挂接目标（候选择被剔出目的地集合
	 * -> 无预留 -> 沿站台乱跑）；而真正该管住"只能挂上目标那一列"的机制是 KI-182 的
	 * 一对一配对锁（R3RCoupleAllowed() 里的 r3r_couple_target/requester），它比分组
	 * 白名单精确得多。
	 *
	 * 连带影响（第 109 轮原文，已被第 202 轮 KI-306 部分撤回）：命令级的「临时挂接
	 * 分组」(GetCoupleTempGroup) 当时对同公司挂接不再有任何作用（本来就是为了通过
	 * 这道白名单）；它仍然有效的地方是公司边界——临时并入一个对外的真分组，就能跨
	 * 公司挂上对方那一列。coupler_groups 的合并（上面几行）因此必须保留。真挂接
	 * 分组本身、分组管理 UI、白名单比较函数 R3RCoupleGroupMasksCompatible() 都保留
	 * 不动，只是不再**无条件**否决挂接。
	 *
	 * R3R (第 202 轮, KI-306)：上面那条"不再有任何作用"正是玩家报的问题——设了与
	 * 没设完全一样（`build\R3R_debug.log` 里 4 个空组 + `CPL-PAIR act=48/54 tgt=0`
	 * 无限刷屏，机车被自己车库里的车底吸住）。修法不是恢复无条件白名单（那会退回
	 * KI-195 想治的老毛病），而是**由命令显式声明临时分组时才启用**：声明 = 玩家主动
	 * 表达"我要按分组挂"，此时分组必须相容；不声明 = 维持本轮的"一律放行"。
	 *
	 * 注意：本函数是三个解析层级共用的唯一判据（yapf_destrail.hpp 的目的地测试、
	 * yapf_rail.cpp 的 CheckSafePositionOnNode 回溯安全测试、train_cmd.cpp 的到点闸门），
	 * 所以这一处改动对三者同时生效；train_cmd.cpp 里 R3RCanCoupleNow 的 reject 标签
	 * (R3REDGE_COUPLEGATE) 也随之一并只反映目的地与公司边界。 */

	/* R3R (D6-①): the company boundary is a second, independent gate. Two
	 * segments of the same company couple exactly as before; two segments of
	 * different companies additionally need a shared group which the owner
	 * opened for other companies. A rejected candidate is reported upstream as
	 * "no waiting consist here", so a locomotive never couples across companies
	 * by accident and simply keeps looking for another candidate. */
	if (coupler->owner != target->owner) {
		return R3RCoupleGroupMasksAllowCrossCompany(coupler_groups, target_groups);
	}

	/* R3R (第 202 轮, KI-306): 同公司路径上，只有**命令显式声明了临时挂接分组**时
	 * 才重新启用分组白名单：
	 *   - 机车侧的 GOTO_COUPLE 声明了分组（tooltip：「再额外拥有该分组，因此可以挂上
	 *     属于它的车底」），或
	 *   - 等待侧的 WAIT_COUPLE 声明了分组（KI-225 的对称语义「我在等人按这个分组来接我」）。
	 * 二者都没有时返回 true，即 KI-195 的"同公司不设门槛"照旧——"忘记把车底加进分组"
	 * 不会再次变成"到了站台找不到挂接目标"。
	 *
	 * 判据用的是合并后的**有效集合**（真分组 ∪ 临时分组，且含所有祖先组，见上面几行），
	 * 所以：声明"滚木"的机车能挂上滚木组（或滚木的子/父组）的车底；而掩码为
	 * COUPLE_GROUP_MASK_NONE 的车底（现场那条没分组的 27 节车库车底）会被
	 * R3RCoupleGroupMasksCompatible() 的 `a==b` 分支判 false ⇒ 直接剔出候选集。
	 * 被拒候选按既有语义上报为"这里没有等待的车底"，机车继续找别的候选，找不到就走
	 * KI-193 的 COUPLE-DEST-EMPTY（等待 500 tick 后跳过命令），不会卡死。
	 *
	 * 这一处对三个解析层级同时生效（本函数是唯一判据，见上方 KI-165 注释），
	 * train_cmd.cpp 的到点闸门 R3RCanCoupleNow() 因此也自动受约束。 */
	if (coupler_declared_temp || target_declared_temp) {
		const bool compatible = R3RCoupleGroupMasksCompatible(coupler_groups, target_groups);
		R3RCoupleGateProbe(coupler, target, coupler_declared_temp, target_declared_temp,
				coupler_groups, target_groups, compatible);
		return compatible;
	}
	return true;
}

/**
 * R3R (KI-182): the chain head stored in one half of a pair flag, if it is
 * still a live chain head. A lock whose counterpart has been rearranged away
 * (the stored index is no longer a head, or not a train at all) counts as gone.
 */
static Train *R3RResolveCouplePairHalf(VehicleID id)
{
	if (id == VehicleID::Invalid()) return nullptr;
	Train *other = Train::GetIfValid(id);
	if (other == nullptr) return nullptr;
	Train *other_head = Train::From(other->First());
	if (other_head == nullptr || other_head->index != id) return nullptr;
	return other_head;
}

bool R3RCouplePairMatches(const Train *coupler, const Train *target)
{
	if (coupler == nullptr || target == nullptr) return false;
	const Train *c = Train::From(coupler->First());
	const Train *t = Train::From(target->First());
	if (c == nullptr || t == nullptr) return false;
	return c->r3r_couple_target == t->index && t->r3r_couple_requester == c->index;
}

Train *R3RGetCouplePairPartner(const Train *v)
{
	if (v == nullptr) return nullptr;
	const Train *head = Train::From(v->First());
	if (head == nullptr) return nullptr;

	/* Active half: this chain stores the consist it has locked onto. */
	Train *partner = R3RResolveCouplePairHalf(head->r3r_couple_target);
	if (partner != nullptr && partner->r3r_couple_requester != head->index) partner = nullptr;

	/* Passive half: this chain stores the locomotive which locked onto it. */
	if (partner == nullptr) {
		partner = R3RResolveCouplePairHalf(head->r3r_couple_requester);
		if (partner != nullptr && partner->r3r_couple_target != head->index) partner = nullptr;
	}
	return partner;
}

bool R3RHasCouplePair(const Train *v)
{
	if (v == nullptr) return false;
	const Train *head = Train::From(v->First());
	if (head == nullptr) return false;
	return head->r3r_couple_target != VehicleID::Invalid() || head->r3r_couple_requester != VehicleID::Invalid();
}

bool R3RPairCoupleTargets(Train *coupler, Train *target)
{
	if (coupler == nullptr || target == nullptr) return false;
	Train *c = Train::From(coupler->First());
	Train *t = Train::From(target->First());
	if (c == nullptr || t == nullptr || c == t) return false;

	/* Drop whatever either side was locked to before, so no chain is left
	 * pointing at a consist which has just been handed to somebody else. */
	R3RUnpairCoupleTargets(c);
	R3RUnpairCoupleTargets(t);

	c->r3r_couple_target = t->index;
	t->r3r_couple_requester = c->index;
	return true;
}

void R3RUnpairCoupleTargets(Train *v)
{
	if (v == nullptr) return;
	Train *head = Train::From(v->First());
	if (head == nullptr) return;

	/* Clear the counter half first: once our own fields are gone the partner
	 * can no longer be found through them. */
	Train *partner = R3RGetCouplePairPartner(head);
	if (partner != nullptr) {
		if (partner->r3r_couple_target == head->index) partner->r3r_couple_target = VehicleID::Invalid();
		if (partner->r3r_couple_requester == head->index) partner->r3r_couple_requester = VehicleID::Invalid();
	}
	head->r3r_couple_target = VehicleID::Invalid();
	head->r3r_couple_requester = VehicleID::Invalid();
}

bool R3RCoupleAllowed(const Train *coupler, const Train *target)
{
	/* R3R (KI-182): the pair flag -- "已有耦合目标" -- is the outer gate.
	 *
	 * A locomotive which has locked onto a consist only ever drives to and
	 * couples onto that consist, and a consist which is somebody's target is
	 * nobody else's candidate. Every level of the resolution asks this one
	 * function (pathfinder destination, back-walk safety, arrival gate), so the
	 * lock cannot be bypassed by any of them. */
	if (!R3RCouplePairMatches(coupler, target)) return false;
	return R3RCoupleAllowedIgnoringPair(coupler, target);
}

CoupleGroupID R3RGetTempCoupleGroup(const Train *v)
{
	if (v == nullptr) return INVALID_COUPLE_GROUP;

	/* The order which carries the group is the head's: ProcessOrders() ticks
	 * only the chain head and R3RCoupleAllowed() reads that vehicle's
	 * current_order (see the comment there). R3R (KI-225): a waiting consist
	 * carries the group on its WAIT_COUPLE order, which is the symmetric half
	 * of the same handshake. */
	const Train *head = Train::From(v->First());
	if (head == nullptr) return INVALID_COUPLE_GROUP;
	if (!head->current_order.IsType(OT_GOTO_COUPLE) && !head->current_order.IsType(OT_WAIT_COUPLE)) return INVALID_COUPLE_GROUP;

	/* Only the segment which carries the order gets the group; the segments
	 * coupled on further back keep the groups they really have. */
	if (R3RGetCoupleGroupCarrier(v) != head) return INVALID_COUPLE_GROUP;

	/* A group the order names but which no longer exists counts as "none". */
	const CoupleGroupID group = head->current_order.GetCoupleTempGroup();
	return R3RIsValidCoupleGroup(group) ? group : INVALID_COUPLE_GROUP;
}

bool R3RHasTempCoupleGroup(const Train *v)
{
	return R3RGetTempCoupleGroup(v) != INVALID_COUPLE_GROUP;
}

bool R3RGetChainScheduleOwner(const Train *v, const Train **owner, uint *index, uint *total)
{
	if (owner != nullptr) *owner = nullptr;
	if (index != nullptr) *index = 0;
	if (total != nullptr) *total = 0;
	if (v == nullptr) return false;

	const Train *best = nullptr;
	uint best_pos = 0;
	uint num_segments = 0;

	for (const Train *t = Train::From(v->First()); t != nullptr; ) {
		/* A segment starts at the chain head and at every SegmentFront marker. */
		if (t->Previous() == nullptr || t->IsSegmentFront()) {
			num_segments++;
			if (best == nullptr || t->r3r_priority < best->r3r_priority) {
				best = t;
				best_pos = num_segments;
			}
		}
		const Vehicle *next = t->Next();
		t = (next != nullptr) ? Train::From(next) : nullptr;
	}

	if (total != nullptr) *total = num_segments;
	if (owner != nullptr) *owner = best;
	if (index != nullptr) *index = (best != nullptr) ? best_pos : 0;
	return true;
}

bool R3RGetSegmentPosition(const Train *v, uint *index, uint *total)
{
	if (index != nullptr) *index = 0;
	if (total != nullptr) *total = 0;
	if (v == nullptr) return false;

	/* The segment which contains v; found by walking back to its segment head. */
	const Train *self_head = R3RGetCoupleGroupCarrier(v);

	const Train *head = Train::From(v->First());
	uint num_segments = 0;
	uint self_pos = 0;

	for (const Train *t = head; t != nullptr; ) {
		if (t->Previous() == nullptr || t->IsSegmentFront()) {
			num_segments++;
			if (t == self_head) self_pos = num_segments;
		}
		const Vehicle *next = t->Next();
		t = (next != nullptr) ? Train::From(next) : nullptr;
	}

	if (total != nullptr) *total = num_segments;
	if (index != nullptr) *index = self_pos;
	return true;
}

bool R3RChainSpansCompanies(const Vehicle *v, Owner company)
{
	if (v == nullptr) return false;

	bool has_company = false;
	bool has_foreign = false;
	for (const Vehicle *t = v->First(); t != nullptr; t = t->Next()) {
		if (t->owner == company) {
			has_company = true;
		} else {
			has_foreign = true;
		}
		if (has_company && has_foreign) return true;
	}
	return false;
}

bool R3RIsFrozenForeignSegment(const Vehicle *v, Owner company)
{
	/* A vehicle of our own company is never frozen, and neither is one which is
	 * merely parked on our tracks: the freeze only exists inside a chain which
	 * was actually coupled across the company boundary. */
	if (v == nullptr || v->owner == company) return false;
	return R3RChainSpansCompanies(v, company);
}

uint R3RCountSegmentsInCoupleGroup(CoupleGroupID group)
{
	const CoupleGroupMask bit = R3RCoupleGroupBit(group);
	uint count = 0;
	for (const Train *t : Train::Iterate()) {
		if (!R3RIsCoupleGroupCarrier(t)) continue;
		/* R3R (第 146 轮 / 需求叁改): 子组的段也算在本组里 —— 列表上显示的"段数"
		 * 就是"挂在这一支下面的所有车"，与乘客看到的成员关系一致。 */
		const CoupleGroupMask groups = R3RGetEffectiveCoupleGroupsOfSegment(t);
		if (group == INVALID_COUPLE_GROUP) {
			/* The implicit group is formed by every unassigned segment. */
			if (groups == COUPLE_GROUP_MASK_NONE) count++;
		} else if (bit != COUPLE_GROUP_MASK_NONE && (groups & bit) != COUPLE_GROUP_MASK_NONE) {
			count++;
		}
	}
	return count;
}

uint R3RCountCoupleGroups(CoupleGroupMask groups)
{
	uint count = 0;
	for (uint i = 0; i < R3R_COUPLE_GROUP_MASK_BITS; i++) {
		if ((groups & (CoupleGroupMask(1) << i)) == 0) continue;
		if (CoupleGroup::GetIfValid(CoupleGroupID(static_cast<uint16_t>(i))) != nullptr) count++;
	}
	return count;
}

std::string R3RGetCoupleGroupsNameList(CoupleGroupMask groups)
{
	std::string result;
	for (uint i = 0; i < R3R_COUPLE_GROUP_MASK_BITS; i++) {
		if ((groups & (CoupleGroupMask(1) << i)) == 0) continue;
		const CoupleGroup *cg = CoupleGroup::GetIfValid(CoupleGroupID(static_cast<uint16_t>(i)));
		if (cg == nullptr) continue;
		if (!result.empty()) result += '+';
		result += cg->name;
	}
	return result;
}

const char *R3RGetCoupleGroupName(CoupleGroupID group)
{
	const CoupleGroup *cg = CoupleGroup::GetIfValid(group);
	return cg != nullptr ? cg->name.c_str() : nullptr;
}

CoupleGroupID R3RGetCoupleGroupParent(CoupleGroupID group)
{
	const CoupleGroup *cg = CoupleGroup::GetIfValid(group);
	return cg != nullptr ? cg->parent : INVALID_COUPLE_GROUP;
}

uint R3RGetCoupleGroupDepth(CoupleGroupID group)
{
	uint depth = 0;
	/* 旧档里可能残留历史环，所以给步数一个硬上限，绝不让界面绘制陷进去。 */
	for (CoupleGroupID cur = R3RGetCoupleGroupParent(group);
			cur != INVALID_COUPLE_GROUP && depth < MAX_LENGTH_COUPLE_GROUP_NAME_CHARS;
			cur = R3RGetCoupleGroupParent(cur)) {
		depth++;
	}
	return depth;
}

bool R3RCanCoupleGroupHaveParent(CoupleGroupID group, CoupleGroupID parent)
{
	if (!R3RIsValidCoupleGroup(group)) return false;
	if (parent == INVALID_COUPLE_GROUP) return true; // 变回顶层组永远合法
	if (!R3RIsValidCoupleGroup(parent) || parent == group) return false;

	/* 顺着拟定的父组往上走：撞到自己就是成环。步数上限同样是为了防旧档里的历史环。 */
	uint steps = 0;
	for (CoupleGroupID cur = parent; cur != INVALID_COUPLE_GROUP && steps <= MAX_LENGTH_COUPLE_GROUP_NAME_CHARS; steps++) {
		if (cur == group) return false;
		cur = R3RGetCoupleGroupParent(cur);
	}
	return true;
}

void R3RUnassignCoupleGroup(CoupleGroupID group)
{
	const CoupleGroupMask bit = R3RCoupleGroupBit(group);
	if (bit == COUPLE_GROUP_MASK_NONE) return;
	/* Clear the bit everywhere: the mask is normally on the segment head only,
	 * but a segment head change may have left a copy behind on another car. */
	for (Train *t : Train::Iterate()) t->couple_groups &= ~bit;
}

bool R3RCoupleGroupIsVisibleTo(const CoupleGroup *group, Owner company)
{
	if (group == nullptr) return false;
	/* Groups without an owner (created by a server console/script) are public. */
	if (group->owner == OWNER_NONE || group->owner == company) return true;
	/* D6-③: a group which the owner opened for other companies is listed for
	 * every company, otherwise nobody would ever find a group they are allowed
	 * to join. */
	return group->AllowsOthers();
}

bool R3RCoupleGroupIsManageable(const CoupleGroup *group, Owner company)
{
	if (group == nullptr) return false;
	return group->owner == company;
}

bool R3RCoupleGroupIsJoinableBy(const CoupleGroup *group, Owner company)
{
	if (group == nullptr) return false;
	/* D6-③: the owner may always fill its own group, every other company only
	 * once the owner opened it for them. The vehicle which is being assigned is
	 * checked separately by the command, so this never grants rights over the
	 * rolling stock of somebody else (D3). */
	return group->owner == company || group->AllowsOthers();
}

void AfterLoadCoupleGroups()
{
	for (Train *t : Train::Iterate()) {
		const CoupleGroupMask groups = R3RSanitiseCoupleGroupMask(t->couple_groups);
		if (groups != t->couple_groups) t->couple_groups = groups;
	}

	/* R3R (第 144 轮 / 需求叁): 父组必须指向一个活着的组，且不能成环。旧存档没有这个
	 * 字段（读出来是 0，0 又恰好是个合法下标），损坏的层级也要在这里断掉 —— 否则
	 * R3RGetCoupleGroupDepth() 与界面绘制会一直踩一个已经不存在的组。 */
	for (CoupleGroup *cg : CoupleGroup::Iterate()) {
		if (cg->parent == INVALID_COUPLE_GROUP) continue;
		if (!R3RCanCoupleGroupHaveParent(cg->index, cg->parent)) {
			R3RDbgWrite("CGRP-PARENT-RESET g=%d bad=%d\n", (int)cg->index.base(), (int)cg->parent.base());
			cg->parent = INVALID_COUPLE_GROUP;
		}
	}
}

/**
 * R3R（2026-09-23）：读档收尾把每条物理链的「列车分组」与「挂接分组」收敛到实际控制段。
 *
 * 与 train_cmd.cpp 里 Couple() / DecoupleTrain() 提交点上调用的 R3RNormaliseChainGroups()
 * 同一口径（实际控制段 = R3RGetChainScheduleOwner() 选出的 r3r_priority 最小者），差别只有两点：
 *  - 面向整张地图遍历（读档时所有车辆刚建好），而不是单条链；
 *  - 不做 num_vehicle 簿记 —— 它由紧随其后的 GroupStatistics::UpdateAfterLoad() 整表重算，
 *    连 SetTrainGroupID 顺带维护的 num_engines 也一并被重算覆盖。
 *
 * 调用点（saveload/afterload.cpp）必须夹在 AfterLoadVehiclesPhase2() 之后、
 * GroupStatistics::UpdateAfterLoad() 之前：前者保证 orders 这条 REF 指针已经修正
 * （读档时 CGVR 的 post 钩子太早，那时表里还是裸序号，绝不能解引用），后者是统计口径。
 * 世界刚刚从文件读出来，这里只应与一处不一致的存档对上；正常存档零命中。
 */
static void R3RSegmentReconcileRows(const char *tag);

void R3RNormaliseChainGroupsAfterLoad()
{
	uint chains = 0;
	uint group_fixes = 0;
	uint mask_fixes = 0;

	for (Train *t : Train::Iterate()) {
		if (t->Previous() != nullptr || !t->IsFrontEngine()) continue;
		chains++;

		const Train *owner = nullptr;
		R3RGetChainScheduleOwner(t, &owner, nullptr, nullptr);
		if (owner == nullptr) continue;

		/* 列车分组：整链推平为控制段的分组。判据扫全链而不是只比链头 —— 不一致的
		 * 往往正是被并入的那一段。 */
		bool uniform = true;
		for (const Vehicle *w = t; w != nullptr; w = w->Next()) {
			if (w->group_id != owner->group_id) { uniform = false; break; }
		}
		if (!uniform) {
			const GroupID old_g = t->group_id;
			/* R3R (第 144 轮 / 需求伍): 与 train_cmd.cpp 的 R3RNormaliseChainGroups() 同口径 ——
			 * 先把"分组会被改写"的段头（含链头）自己的分组停放，等该段重新成为链头时由
			 * NormaliseTrainHead() 取回。读档路径不做 num_vehicle 簿记：紧随其后的
			 * GroupStatistics::UpdateAfterLoad() 会整表重算。 */
			for (Train *s = t; s != nullptr; ) {
				if ((s->Previous() == nullptr || s->IsSegmentFront()) && s->group_id != owner->group_id &&
						s->group_id_backup == GroupID::Invalid()) {
					s->group_id_backup = s->group_id;
					R3RDbgWrite("GRP-PARK-LOAD veh=%d g=%u\n", (int)s->index.base(), (uint)s->group_id.base());
				}
				Vehicle *next = s->Next();
				s = (next != nullptr) ? Train::From(next) : nullptr;
			}
			SetTrainGroupID(t, owner->group_id);
			group_fixes++;
			R3RDbgWrite("GRP-NORM-LOAD head=%d old=%u new=%u\n", (int)t->index.base(),
					(uint)old_g.base(), (uint)owner->group_id.base());
		}

		/* 挂接分组（第 159 轮 / q-2=肆）：与 train_cmd.cpp 的 R3RNormaliseChainGroups() 同口径 ——
		 * 取全链段头掩码的**并集（OR）**再广播回每一个段头，不再照抄控制段那一段（覆盖）。 */
		CoupleGroupMask target = COUPLE_GROUP_MASK_NONE;
		for (Train *s = t; s != nullptr; ) {
			if (s->Previous() == nullptr || s->IsSegmentFront()) {
				target |= R3RGetCoupleGroupsOfSegment(s);
			}
			Vehicle *next = s->Next();
			s = (next != nullptr) ? Train::From(next) : nullptr;
		}
		for (Train *s = t; s != nullptr; ) {
			if (s->Previous() == nullptr || s->IsSegmentFront()) {
				if (R3RGetCoupleGroupsOfSegment(s) != target) {
					R3RClearCoupleGroupsOfSegment(s);
					for (uint i = 0; i < R3R_COUPLE_GROUP_MASK_BITS; i++) {
						if ((target & (CoupleGroupMask(1) << i)) != 0) {
							R3RAddCoupleGroupToSegment(s, CoupleGroupID(static_cast<uint16_t>(i)));
						}
					}
					mask_fixes++;
					R3RDbgWrite("CGRP-NORM-LOAD seg=%d mask=%llu\n", (int)s->index.base(),
							(unsigned long long)target);
				}
			}
			Vehicle *next = s->Next();
			s = (next != nullptr) ? Train::From(next) : nullptr;
		}
	}

	R3RDbgWrite("GRP-NORM-LOAD-SUM chains=%u groupfix=%u maskfix=%u\n", chains, group_fixes, mask_fixes);

	/* R3R (第 156 轮 / 落地清单 ⑦): the rows of the R3SG chunk are restored verbatim, so
	 * a savegame written between two commit points can carry rows which no car claims
	 * (or cars whose row is empty). Reconcile them now that both the cars and their
	 * order lists are fully in memory; see R3RSegmentReconcileRows(). */
	R3RSegmentReconcileRows("load");
}

/*
 * R3R (第 152 轮): the segment identity table.
 *
 * One row per segment (see #R3RSegmentRecord), addressed by #Vehicle::r3r_segment_id.
 * The table is deliberately a flat std::vector indexed by the ID itself: looking
 * up a segment is then one array index plus a bounds check, with no hashing and
 * no traversal, which is what lets the identity be queried from the hot paths
 * without a cost worth measuring. IDs are recycled through a free list, so the
 * vector stays as large as the highest ID still in use rather than as large as
 * the number of segments ever created.
 *
 * Lifetime rules which the rest of R3R must respect:
 *  - A row is created by R3RSegmentAlloc() (or materialised by the R3SG loader)
 *    and destroyed by R3RSegmentFree(). Slots are never removed from the vector.
 *  - The returned pointer is valid only until the next R3RSegmentAlloc() /
 *    R3RSegmentGetOrCreate(), which may grow (and therefore move) the vector.
 *    Callers which hold a row across such a call must re-fetch it by ID.
 *  - Freeing a row must never free the order list it points at: the list is
 *    owned by the R3R schedule machinery (or by a car), and a borrowed row only
 *    ever held a reference to somebody else's list.
 */

/** R3R (第 152 轮): segment rows by ID. Slot #R3R_SEGMENT_NONE is never handed out, so the table is never empty. */
static std::vector<R3RSegmentRecord> _r3r_segments(1);

/** R3R (第 152 轮): IDs which were released and may be handed out again (LIFO). */
static std::vector<uint16_t> _r3r_free_segment_ids;

size_t R3RSegmentPoolSize()
{
	return _r3r_segments.size();
}

R3RSegmentRecord *R3RSegmentGet(uint16_t id)
{
	if (id == R3R_SEGMENT_NONE || id >= _r3r_segments.size()) return nullptr;
	R3RSegmentRecord &rec = _r3r_segments[id];
	return rec.in_use ? &rec : nullptr;
}

R3RSegmentRecord *R3RSegmentGetOrCreate(uint16_t id)
{
	if (id == R3R_SEGMENT_NONE || id > R3R_SEGMENT_ID_MAX) return nullptr;
	if (id >= _r3r_segments.size()) _r3r_segments.resize(static_cast<size_t>(id) + 1);

	R3RSegmentRecord &rec = _r3r_segments[id];
	if (!rec.in_use) {
		rec = R3RSegmentRecord{};
		rec.in_use = true;
	}
	return &rec;
}

uint16_t R3RSegmentAlloc()
{
	uint16_t id;
	if (!_r3r_free_segment_ids.empty()) {
		id = _r3r_free_segment_ids.back();
		_r3r_free_segment_ids.pop_back();
	} else {
		if (_r3r_segments.size() > R3R_SEGMENT_ID_MAX) return R3R_SEGMENT_NONE;
		id = static_cast<uint16_t>(_r3r_segments.size());
		_r3r_segments.emplace_back();
	}

	R3RSegmentRecord &rec = _r3r_segments[id];
	rec = R3RSegmentRecord{};
	rec.in_use = true;
	return id;
}

void R3RSegmentFree(uint16_t id)
{
	R3RSegmentRecord *rec = R3RSegmentGet(id);
	if (rec == nullptr) return;

	*rec = R3RSegmentRecord{};
	_r3r_free_segment_ids.push_back(id);
}

void R3RSegmentTableReset()
{
	_r3r_segments.clear();
	_r3r_segments.emplace_back();
	_r3r_free_segment_ids.clear();
}

/**
 * R3R（第 159 轮 / q-0=贰乙 玩家口径）：「控制段」= 命令所有者段。
 *
 * 第 159 轮把口径从**链头所在段**（P1-甲）改成**命令所有者段** —— 也就是
 * `r3r_priority` 最小的那个段头（train_cmd.cpp 的 `R3RGetPriorityHead()`）。
 * 现场样本 `INVAR-CTRL tag=couple head=48 nseg=2 ctrl=48 owner=21`：链头 48 与
 * 真正的排程主人 21 本来就是两个段，乙口径下"控制段"必须是后者 —— 链头只是
 * 承载者（carrier），它借用控制段的特质与排程，把控制段显现给玩家。
 *
 * 这里是 train_cmd.cpp 那条规则的等价复刻：读侧在最热的一段代码上（每个车节
 * 每次重绘都要认控制段），不能为它跨文件去问 owner。
 *
 * @param chain 链头（可为 nullptr）。
 * @return 控制段的段头；chain 为 nullptr 时返回 nullptr。
 */
static const Vehicle *R3RSegmentControlHead(const Vehicle *chain)
{
	if (chain == nullptr) return nullptr;

	const Vehicle *best = nullptr;
	for (const Vehicle *v = chain; v != nullptr && v->type == VehicleType::Train; v = v->Next()) {
		/* 段 = 链头本身或带 ★ 的车；每段的优先级取段头的 r3r_priority。 */
		if (v != chain && !Train::From(v)->IsSegmentFront()) continue;
		if (best == nullptr || Train::From(v)->r3r_priority < Train::From(best)->r3r_priority) best = v;
	}
	return (best != nullptr) ? best : chain;
}

/**
 * R3R (第 155 轮 / 落地清单 ③，第 159 轮改口径): the row which holds the traits
 * of the segment \a v is part of, or nullptr when those traits are read from the
 * chain head itself.
 *
 * nullptr is returned for the chain head row itself (its live fields always carry
 * the traits of the **control segment**, see below), for the **control segment**
 * (第 159 轮 / q-0=贰乙: the command-owner segment, i.e. the segment with the
 * lowest #Train::r3r_priority), recognised by both cars carrying the same
 * #Vehicle::r3r_segment_id -- and for every car whose segment has no row
 * (single segment chains, whose id is #R3R_SEGMENT_NONE, and cars of an older
 * savegame).
 *
 * 控制段的特质由**链头自身**承载（借用层）：各交接点把不再当链头的那一段自己的
 * unitnumber / name / group_id 停进自己的 *_backup（车号同时占住号池位），链头的
 * 活字段换成控制段的那一套；提交点再把停放的那一套收进链头段的行
 * （R3RBorrowControlTraits()）、把活字段收进控制段的行（R3RSyncSegmentTraits()）。
 * 于是链头一行"回退到 v->First()"读到的就是控制段的特质，字面实现"由控制段显现"。
 */
static const R3RSegmentRecord *R3RSegmentTraitRow(const Vehicle *v)
{
	const Vehicle *const head = v->First();

	/* 第 159 轮 / q-0=贰乙：链头那一行**永远**读链头的活字段。乙口径下链头是
	 * **承载者**：各交接点把链头段自己的号/名/组停进它的 *_backup、活字段换成控制段
	 * 的那一套（R3RBorrowControlTraits() 把停放的那一套收进链头段的行，
	 * R3RSyncSegmentTraits() 把活字段收进控制段的行），所以"回退到 v->First()"
	 * 读出来的正好是控制段的那一套 —— 主行由此显现控制段。
	 * 缺了这条判断时 chain->r3r_segment_id 与控制段不同，主行会读回链头段自己那一行
	 * （停放副本），显现的就成了"承载者自己的"特质。 */
	if (v == head) return nullptr;

	/* 第 159 轮 / q-0=贰乙：控制段 = 命令所有者段（不再是链头所在段）。 */
	const Vehicle *const ctrl = R3RSegmentControlHead(head);
	if (ctrl != nullptr && ctrl->r3r_segment_id == v->r3r_segment_id) return nullptr;
	return R3RSegmentGet(v->r3r_segment_id);
}

UnitID R3RSegmentUnitNumber(const Vehicle *v)
{
	if (v == nullptr) return 0;

	const R3RSegmentRecord *const row = R3RSegmentTraitRow(v);
	if (row != nullptr && row->unitnumber != 0) return row->unitnumber;
	return v->First()->unitnumber;
}

std::string R3RSegmentName(const Vehicle *v)
{
	if (v == nullptr) return std::string();

	const R3RSegmentRecord *const row = R3RSegmentTraitRow(v);
	if (row != nullptr && !row->name.empty()) return row->name;

	/* 第 159 轮 / q-0=贰乙：控制段（命令所有者段）的名称由链头承载，段内任何一节
	 * 都读链头那一份 —— 不然控制段里的第二、第三节会回退到它们自己空名字。
	 * 其余段仍回退**本车自己的名字**：隐藏段没有自己的名字时必须保持无名，
	 * 否则它的子行会读成上一行的副本。#TinyString 空的时候存的是 nullptr，
	 * 绝不能直接交给 std::string（KI-245）：走它的 string_view 转换。 */
	const Vehicle *const head = v->First();
	const Vehicle *const ctrl = R3RSegmentControlHead(head);
	if (ctrl != nullptr && ctrl->r3r_segment_id == v->r3r_segment_id) {
		return std::string(static_cast<std::string_view>(head->name));
	}
	return std::string(static_cast<std::string_view>(v->name));
}

GroupID R3RSegmentGroupID(const Vehicle *v)
{
	if (v == nullptr) return GroupID::Invalid();

	const R3RSegmentRecord *const row = R3RSegmentTraitRow(v);
	if (row != nullptr && row->group_id != GroupID::Invalid()) return row->group_id;
	return v->First()->group_id;
}

/*
 * R3R (第 170 轮 / R170-C)：子行读的必须是**这一段自己**那一份特质。
 *
 * 上面三个访问器是"承载者视角"：链头的活字段替控制段作答（链头穿着控制段的
 * 那一套），主行靠这个显现控制段，无可替代。但子行不行 —— 当子行代表的那一段
 * 正好是**链头段自己**（乙口径下控制段 = 命令所有者段，与链头段完全可以不是
 * 同一段：车库"拖到列车前面"、耦合并入整段都是这种情况）时，"问链头"得到的
 * 又是控制段的那一套，子行就变成上一行的副本；而它的号/名其实正停在自己段的
 * 行里（R3RBorrowControlTraits() 写的）。
 *
 * 所以子行改用这一组：永远先读**本段**的行，读不到才退回本段自己的活字段 ——
 * 绝不回退到链头（链头手上是控制段的特质）。R170-C 修好了
 * R3RSegmentHiddenHeads()（把链头段自己也列出来），这三个是它的配套读侧。
 */
UnitID R3RSegmentSectionUnitNumber(const Vehicle *section)
{
	if (section == nullptr) return 0;

	const R3RSegmentRecord *const row = R3RSegmentGet(section->r3r_segment_id);
	if (row != nullptr && row->unitnumber != 0) return row->unitnumber;
	return section->unitnumber;
}

std::string R3RSegmentSectionName(const Vehicle *section)
{
	if (section == nullptr) return std::string();

	const R3RSegmentRecord *const row = R3RSegmentGet(section->r3r_segment_id);
	if (row != nullptr && !row->name.empty()) return row->name;

	/* #TinyString 空的时候存的是 nullptr，绝不能直接交给 std::string（KI-245）：
	 * 走它的 string_view 转换。 */
	return std::string(static_cast<std::string_view>(section->name));
}

GroupID R3RSegmentSectionGroupID(const Vehicle *section)
{
	if (section == nullptr) return GroupID::Invalid();

	const R3RSegmentRecord *const row = R3RSegmentGet(section->r3r_segment_id);
	if (row != nullptr && row->group_id != GroupID::Invalid()) return row->group_id;
	return section->group_id;
}

void R3RSegmentStoreTraits(const Vehicle *v)
{
	if (v == nullptr) return;

	/* Traits are written for the control segment only: it is the one the player
	 * edits (everything the GUI offers lands on the chain head), and the one whose
	 * row is purely a mirror of the car. The other segments are collected from
	 * their cars at the commit points instead (R3RSyncSegmentTraits()).
	 *
	 * 第 159 轮 / q-0=贰乙：控制段 = **命令所有者段**，链头（玩家编辑的那辆车）
	 * 只是承载者 —— 它的活字段在提交点被换成控制段的那一套（R3RBorrowControlTraits）。
	 * 所以这里要写的是**控制段的行**：玩家在链头那一行改的号/名/组落到控制段头上，
	 * 而链头段自己的特质留在它的行里（由提交点按 parked 副本补齐），两者不再互相污染。 */
	const Vehicle *const head = v->First();
	const Vehicle *const ctrl = R3RSegmentControlHead(head);
	const uint16_t target_id = (ctrl != nullptr) ? ctrl->r3r_segment_id : head->r3r_segment_id;
	if (target_id == R3R_SEGMENT_NONE) return;

	R3RSegmentRecord *const row = R3RSegmentGet(target_id);
	if (row == nullptr) return;

	/* R3R (第 156 轮 / 落地清单 ③ 残留): write "the car's own value first, the parked
	 * copy as a fallback" instead of overwriting the whole row from the live fields.
	 * The chain head can be parked at this very moment -- a car-only consist which
	 * lent its unit number, name or group away keeps those in the *_backup fields
	 * while the live ones are zeroed. Overwriting from the live fields then erases
	 * the very traits the player just looked at, and the callers of this function
	 * (rename, set group) are exactly the ones which flush the row mid-frame. */
	row->unitnumber = (head->unitnumber != 0) ? head->unitnumber : head->unitnumber_backup;

	if (!head->name.empty()) {
		row->name = head->name;
	} else if (!head->name_backup.empty()) {
		row->name = head->name_backup;
	} else {
		row->name.clear();
	}

	if (head->group_id != DEFAULT_GROUP) {
		row->group_id = head->group_id;
	} else if (head->group_id_backup != GroupID::Invalid()) {
		row->group_id = head->group_id_backup;
	} else {
		row->group_id = head->group_id;
	}
}

/**
 * R3R (第 156 轮 / 落地清单 ⑦): reconcile the segment table with the live cars.
 *
 * Segment rows carry the R3R segment traits and are rewritten at every commit point
 * (R3RSyncSegmentTraits() / R3RSyncHiddenSegmentTraits()), yet three situations can
 * leave "a row without a car" or "a car with an empty row" behind:
 *  - the R3SG chunk restores rows verbatim, so a savegame written between two commit
 *    points can carry rows whose car is long gone;
 *  - a downgrade / merge path which missed its R3RSegmentFree() leaks such a row;
 *  - rows written by a build predating the "fall back to the live fields" rule can be
 *    completely empty.
 *
 * Two deliberately conservative actions:
 *  - a row no car claims is freed, but only when it is entirely empty (no order list
 *    pointer, no number, no name, no group): anything else may still be a live borrow
 *    and is left alone;
 *  - a claimed row whose three traits are all empty is rebuilt from its segment head,
 *    preferring the parked copies (a segment which lent its traits away keeps them in
 *    the *_backup fields) -- the same value rule R3RSyncHiddenSegmentTraits() uses.
 *
 * No order list pointer, car field or chain order is touched, so this is safe to run
 * while the map is already live.
 *
 * @param tag Short tag for the debug log.
 */
static void R3RSegmentReconcileRows(const char *tag)
{
	/* Which segments are still claimed, and by which car -- their head (the ★ car, or
	 * for the first segment the chain head). Every car of a segment shares its ID, so
	 * the head is the car which opens it in chain order. */
	std::vector<const Train *> claimed(R3RSegmentPoolSize(), nullptr);

	for (const Vehicle *v : Vehicle::Iterate()) {
		if (v->type != VehicleType::Train) continue;
		const Train *const t = Train::From(v);
		const uint16_t id = t->r3r_segment_id;
		if (id == R3R_SEGMENT_NONE || id > R3R_SEGMENT_ID_MAX || id >= claimed.size()) continue;

		const Vehicle *const prev = t->Previous();
		const bool seg_front = t->IsSegmentFront() || prev == nullptr || prev->r3r_segment_id != id;
		if (seg_front && (claimed[id] == nullptr || !claimed[id]->IsSegmentFront())) claimed[id] = t;
	}

	uint orphan = 0;
	uint rebuilt = 0;

	for (size_t id = 1; id < claimed.size(); id++) {
		R3RSegmentRecord *const row = R3RSegmentGet(static_cast<uint16_t>(id));
		if (row == nullptr) continue;

		const Train *const head = claimed[id];
		if (head == nullptr) {
			if (row->orders != nullptr || row->unitnumber != 0 || !row->name.empty() ||
					row->group_id != GroupID::Invalid()) {
				continue;	// Still carries something: possibly a live borrow.
			}
			R3RSegmentFree(static_cast<uint16_t>(id));
			orphan++;
			continue;
		}

		if (row->unitnumber != 0 || !row->name.empty() || row->group_id != GroupID::Invalid()) continue;

		/* R3R (第 159 轮 / q-0=贰乙): a chain head which is currently the borrower
		 * of another segment's traits (r3r_orders_borrowed) carries the **control**
		 * segment's unitnumber / name / group_id in its live fields, so those must
		 * never be copied in as this segment's own values -- only the parked copies
		 * count. (A section head which is not the chain head never borrows.) */
		const bool borrowed = head->r3r_orders_borrowed;
		if (head->unitnumber_backup != 0) {
			row->unitnumber = head->unitnumber_backup;
		} else if (!borrowed) {
			row->unitnumber = head->unitnumber;
		}
		if (!head->name_backup.empty()) {
			row->name = head->name_backup;
		} else if (!borrowed) {
			row->name = head->name;
		}
		if (head->group_id_backup != GroupID::Invalid()) {
			row->group_id = head->group_id_backup;
		} else if (!borrowed) {
			row->group_id = head->group_id;
		}
		rebuilt++;
	}

	if (orphan == 0 && rebuilt == 0) return;

	R3RDbgWrite("SEGROW-RECONCILE tag=%s rows=%u orphan=%u rebuilt=%u\n",
			tag, (uint)claimed.size(), orphan, rebuilt);
}

std::vector<const Vehicle *> R3RSegmentHiddenHeads(const Vehicle *chain)
{
	std::vector<const Vehicle *> heads;
	if (chain == nullptr) return heads;

	/* A segment starts at a car which carries #VehicleRailFlag::SegmentFront (★);
	 * the leading segment starts at the chain head and is skipped by starting the
	 * walk at the second car. Articulated parts belong to their parent's section
	 * and are never section heads, so they are passed over here as well.
	 *
	 * 第 159 轮 / q-0=贰乙：子行列出的是"非控制段"，而控制段 = 命令所有者段。
	 * 于是控制段那一段不再列子行 —— 它的特质已经由链头那一行显现。
	 *
	 * 第 170 轮 / R170-C：链头段自己也要列出来（只要它不是控制段）。乙口径下控制段
	 * 与链头段完全可以不是同一段（车库"拖到列车前面"、耦合并入整段都是），这时链头
	 * 那一行显现的是**控制段**的特质，而链头段自己的号/名只停在自己的行里 —— 不给它
	 * 子行，玩家在车队列表里就看不到这一段（现场 R170-C：车库拖动耦合出来的那一段
	 * 没有子行）。承载者仍用链头本身：段 ID 放在 r3r_hidden_section，绘制子行时走
	 * R3RSegmentSection*() 读本段的行，不会读成链头手上的控制段特质。 */
	const Vehicle *const ctrl = R3RSegmentControlHead(chain);
	if (ctrl != nullptr && ctrl->r3r_segment_id != chain->r3r_segment_id) heads.push_back(chain);
	for (const Vehicle *v = chain->Next(); v != nullptr && v->type == VehicleType::Train; v = v->Next()) {
		if (v->IsArticulatedPart()) continue;
		if (ctrl != nullptr && v == ctrl) continue;
		if (Train::From(v)->IsSegmentFront()) heads.push_back(v);
	}
	return heads;
}
