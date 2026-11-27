/*
 * This file is part of OpenTTD.
 * OpenTTD is free software; you can redistribute it and/or modify it under the terms of the GNU General Public License as published by the Free Software Foundation, version 2.
 * OpenTTD is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 * See the GNU General Public License for more details. You should have received a copy of the GNU General Public License along with OpenTTD. If not, see <https://www.gnu.org/licenses/old-licenses/gpl-2.0>.
 */

/** @file couple_group_sl.cpp R3R: save/load of couple groups. */

#include "../stdafx.h"
#include "../couple_group.h"

#include "../train.h"
#include "../vehicle_base.h"

#include "saveload.h"

#include "../safeguards.h"

/* R3R (第 246 轮 / 第 5 项): implemented in train_cmd.cpp next to the two tables it
 * writes (the decouple seam interlock and the orphan reservation age stamps). Keeping
 * the code there means no header grows an accessor. */
void Save_R3DS();
void Load_R3DS();

/**
 * R3R: couple group pool chunk (one entry per group).
 */
static const NamedSaveLoad _couple_group_desc[] = {
	NSL("name",  SLE_SSTR(CoupleGroup, name, SLE_STR | SLF_ALLOW_CONTROL)),
	NSL("owner", SLE_VAR(CoupleGroup, owner, SLE_UINT8)),
	NSL("flags", SLE_VAR(CoupleGroup, flags, SLE_UINT32)),
	/* R3R (第 144 轮 / 需求叁): parent group of the hierarchy. Old savegames have no
	 * such field, so it reads as 0 == INVALID_COUPLE_GROUP ... except that 0 is also
	 * a *valid* group index; AfterLoadCoupleGroups() therefore normalises a parent
	 * which does not name a live group back to INVALID_COUPLE_GROUP. */
	NSL("parent", SLE_VAR(CoupleGroup, parent, SLE_UINT16)),
};

static void Load_CGPP()
{
	SaveLoadTableData slt = SlTableHeaderOrRiff(_couple_group_desc);

	int index;
	while ((index = SlIterateArray()) != -1) {
		CoupleGroup *group = CoupleGroup::CreateAtIndex(CoupleGroupID(index));
		SlObjectLoadFiltered(group, slt);
	}
}

static void Save_CGPP()
{
	SaveLoadTableData slt = SlTableHeader(_couple_group_desc);

	for (CoupleGroup *group : CoupleGroup::Iterate()) {
		SlSetArrayIndex(group->index);
		SlObjectSaveFiltered(group, slt);
	}
}

/**
 * R3R: vehicle -> couple group mapping chunk.
 *
 * The field itself is deliberately NOT part of the vehicle table: that way this
 * fork never changes the layout of the vehicle savegame table (which is shared
 * with upstream), and savegames written without any couple group simply do not
 * contain this chunk at all. The mapping is sparse: only segment heads with an
 * assigned group are stored, everything else falls back to the implicit group.
 */
static const NamedSaveLoad _couple_group_vehicle_desc[] = {
	NSL("groups", SLE_VAR(Vehicle, couple_groups, SLE_UINT64)),
};

static void Load_CGVR()
{
	SaveLoadTableData slt = SlTableHeaderOrRiff(_couple_group_vehicle_desc);

	int index;
	while ((index = SlIterateArray()) != -1) {
		Vehicle *v = Vehicle::GetIfValid(index);
		if (v == nullptr) continue;
		SlObjectLoadFiltered(v, slt);
	}

	/* Repair dangling references before the game starts running. */
	AfterLoadCoupleGroups();
}

static void Save_CGVR()
{
	SaveLoadTableData slt = SlTableHeader(_couple_group_vehicle_desc);

	for (Vehicle *v : Vehicle::Iterate()) {
		if (v->couple_groups == COUPLE_GROUP_MASK_NONE) continue;
		SlSetArrayIndex(v->index);
		SlObjectSaveFiltered(v, slt);
	}
}

/**
 * R3R: parked schedule / segment bookkeeping chunk (KI-169a).
 *
 * When a segment's own schedule is handed over to another segment (route A
 * borrow of Couple(), or the depot drag which splices a chain into another one)
 * the schedule is parked in Vehicle::orders_backup and the vehicle remembers
 * where it was and what it was called. Those fields used to be runtime-only, so
 * a save/load round trip silently dropped the park: the vehicle came back with
 * an empty/"no schedule" state and the borrow could never be given back
 * (R3RSyncDrivingOrders() finds nothing to restore).
 *
 * Like the couple group mapping, the fields are deliberately NOT part of the
 * vehicle table (that layout is shared with upstream and must not change) and
 * the mapping is sparse: only vehicles carrying actual park state are written.
 * The order list itself is written by the ORDL chunk -- OrderList::Iterate()
 * covers every list of the pool, including lists which are only referenced from
 * here -- and, as for Vehicle::orders, the reference is turned back into a
 * pointer by the pointer pass below.
 *
 * Savegames written before this chunk existed simply do not contain it, which
 * leaves every field at its default value; see R3RRebuildCouplePriorities().
 */
static const NamedSaveLoad _r3r_park_desc[] = {
	/* R3R (KI-465 修法 D / 第 294 轮): the following pointer. Non-null = the vehicle executes
	 * that order list while its own `orders` still points at its own list. Written here (not
	 * in the vehicle table) exactly like orders_backup, so the upstream vehicle layout stays
	 * untouched. The described table is table-header driven (SlTableHeader), so a savegame
	 * written before this field existed loads it as nullptr without any version bump. */
	NSL("follow_orders",        SLE_REF(Vehicle, r3r_follow_orders, REF_ORDERLIST)),
	NSL("follow_owner",         SLE_VAR(Vehicle, r3r_follow_owner, SLE_UINT32)),
	NSL("orders_backup",        SLE_REF(Vehicle, orders_backup, REF_ORDERLIST)),
	NSL("order_backup_index",   SLE_VAR(Vehicle, orders_backup_real_index, SLE_VEHORDERID)),
	NSL("order_backup_index_i", SLE_VAR(Vehicle, orders_backup_implicit_index, SLE_VEHORDERID)),
	/* R3R (第 416 轮): 时间表进度也有自己的停放副本 —— 跑调度池表期间它是池表的坐标，
	 * 归还时不能拿它当自己的位置，必须用这里存的那一份。表驱动读写，旧档读回 INVALID。 */
	NSL("order_backup_index_t", SLE_VAR(Vehicle, orders_backup_timetable_index, SLE_VEHORDERID)),
	NSL("unitnumber_backup",    SLE_VAR(Vehicle, unitnumber_backup, SLE_UINT16)),
	/* R3R (第 160 轮 / KI-261): 车名的停放副本。原来是 NOSAVE（vehicle_base.h:380），
	 * 于是"耦合时链头穿上车组的名字、自己的名字停在 name_backup"这一对借用/返还
	 * 里的返还端在存档往返后消失 —— 读档后再解挂，链头永远叫车组的名字、车组自己
	 * 也拿不回原名（玩家报的"名称还是跟链头"里最像"和旧 R3R 版本存档有关"的那一半）。
	 * 车号侧早就随本块存档，名字侧补齐后两者才真正同构。
	 * 用 SLE_CONDSTR（TinyString，length=0 取 sizeof），与车辆表里 Vehicle::name 的写法一致。 */
	NSL("name_backup",          SLE_CONDSTR(Vehicle, name_backup, SLE_STR | SLF_ALLOW_CONTROL, 0, SL_MIN_VERSION, SL_MAX_VERSION)),
	/* R3R (第 144 轮 / 需求伍): 段的「列车分组」归属停放（整链统一到控制段时借出，
	 * 该段重新成为链头时取回）。与其它停放字段一样只在稀疏块里写，不动车辆表布局。 */
	NSL("group_id_backup",      SLE_VAR(Vehicle, group_id_backup, SLE_UINT16)),
	NSL("orders_borrowed",      SLE_VAR(Vehicle, r3r_orders_borrowed, SLE_BOOL)),
	/* R3R (第 416 轮): 跟随的是「调度池条目」而不是别段的表。存档往返必须保留，
	 * 否则读档后这份日程永远不会归还（列车会一直跑池表，池表末条后又从头绕）。 */
	NSL("follow_pool",          SLE_VAR(Vehicle, r3r_follow_pool, SLE_BOOL)),
	/* R3R (第 429 轮 / 玩家需求壹): 耦合借用期间被挂起的调度池表。借用分三层
	 * （自有 → 调度池 → 耦合方），而耦合会把 follow_orders 覆盖掉，没有这一格
	 * 池层就整层丢失，归还时还会拿耦合方的表去释放池坑位。表驱动读写，旧档读回 nullptr。 */
	NSL("pool_orders",          SLE_REF(Vehicle, r3r_pool_orders, REF_ORDERLIST)),
	NSL("priority",             SLE_VAR(Vehicle, r3r_priority, SLE_UINT16)),
	/* R3R (第 152 轮): which segment this car belongs to. The identity data lives
	 * in the R3SG chunk below, so this is only the address; a car whose chain
	 * holds a single segment keeps R3R_SEGMENT_NONE and writes nothing. */
	NSL("segment_id",           SLE_VAR(Vehicle, r3r_segment_id, SLE_UINT16)),
};

/** Is there any point in writing this vehicle's park state out? */
static bool R3RHasParkState(const Vehicle *v)
{
	return v->r3r_follow_orders != nullptr ||
			v->r3r_follow_owner != VehicleID::Invalid() ||
			v->r3r_pool_orders != nullptr ||
			v->orders_backup != nullptr ||
			v->orders_backup_real_index != INVALID_VEH_ORDER_ID ||
			v->orders_backup_implicit_index != INVALID_VEH_ORDER_ID ||
			v->orders_backup_timetable_index != INVALID_VEH_ORDER_ID ||
			v->r3r_follow_pool ||
			v->unitnumber_backup != 0 ||
			!v->name_backup.empty() ||
			v->r3r_orders_borrowed ||
			v->group_id_backup != GroupID::Invalid() ||
			v->r3r_priority != 1 ||
			v->r3r_segment_id != R3R_SEGMENT_NONE;
}

static void Load_R3VP()
{
	SaveLoadTableData slt = SlTableHeaderOrRiff(_r3r_park_desc);

	int index;
	while ((index = SlIterateArray()) != -1) {
		Vehicle *v = Vehicle::GetIfValid(index);
		if (v == nullptr) continue;
		SlObjectLoadFiltered(v, slt);
	}
}

/**
 * R3R (第 526 轮, KI-696): is \a ol still a live member of the OrderList pool?
 *
 * 见下方 R3ROrderListIsLive() 的实现说明：只做指针身份比较，绝不解引用。
 * 这里前置声明，是因为 R3VP 的两个函数在本文件里排在 R3SG 之前。
 */
static bool R3ROrderListIsLive(const OrderList *ol);

static void Save_R3VP()
{
	SaveLoadTableData slt = SlTableHeader(_r3r_park_desc);

	for (Vehicle *v : Vehicle::Iterate()) {
		if (!R3RHasParkState(v)) continue;
		/* R3R (第 526 轮, KI-696): 与 R3SG 的 KI-647 同族。
		 *
		 * R3VP 存了三个"挂起订单表"指针（orders_backup / r3r_follow_orders /
		 * r3r_pool_orders）。其中任何一个都可能先于本车被销毁（表的主人被卖掉、
		 * 池条目被清理），而 R3RFindOrderListReferrer() 只看车辆，不会通知本车。
		 * 悬空指针一旦被写进存档，ReferenceToInt() 会去读已释放对象的 `index`，
		 * 轻则写入一个不存在的池索引（读档 "Referencing invalid OrderList" abort），
		 * 重则把垃圾值当索引写出去。
		 *
		 * 与 R3SG 一样只清死指针：那张表的内容本来就不可恢复，不能为了它让整份
		 * 存档变得读不进来。 */
		if (v->orders_backup != nullptr && !R3ROrderListIsLive(v->orders_backup)) v->orders_backup = nullptr;
		if (v->r3r_follow_orders != nullptr && !R3ROrderListIsLive(v->r3r_follow_orders)) {
			v->r3r_follow_orders = nullptr;
			/* 跟随指针没了，"我在跑别人的表"这一层借用事实也就不存在了。 */
			v->r3r_follow_owner = VehicleID::Invalid();
			v->r3r_follow_pool = false;
			v->r3r_orders_borrowed = false;
		}
		if (v->r3r_pool_orders != nullptr && !R3ROrderListIsLive(v->r3r_pool_orders)) v->r3r_pool_orders = nullptr;
		SlSetArrayIndex(v->index);
		SlObjectSaveFiltered(v, slt);
	}
}

static void Ptrs_R3VP()
{
	SaveLoadTableData slt = SlPrepareNamedSaveLoadTableForPtrOrNull(_r3r_park_desc);

	for (Vehicle *v : Vehicle::Iterate()) {
		/* R3R (第 526 轮, KI-696): 与 Ptrs_R3SG() 同一条防线 —— 存档里可能带着
		 * 一个已经不存在的池索引（挂起表先于本车被销毁）。IntToReference() 遇到
		 * 这种值会 abort 整个读档，所以要在指针解析**之前**清掉。
		 * 此刻字段里还是原始整数 "索引 + 1"（见 ReferenceToInt()）。 */
		for (OrderList **slot : { &v->orders_backup, &v->r3r_follow_orders, &v->r3r_pool_orders }) {
			const size_t raw = (size_t)*slot;
			if (raw != 0 && !OrderList::IsValidID(raw - 1)) *slot = nullptr;
		}
		SlObjectPtrOrNullFiltered(v, slt);

		/* R3R (第 144 轮 / 需求伍): 没有哪个段会"把自己借给自己"——备份与当前分组相同时
		 * 它就是无意义的（旧存档没有这个字段，SLE 读出来是 0 = DEFAULT_GROUP，也走这条
		 * 路清掉），立刻归一为 Invalid，免得 R3RHasParkState() 白白多写一节。 */
		if (v->group_id_backup == v->group_id) v->group_id_backup = GroupID::Invalid();
	}
}

/**
 * R3R (第 152 轮): segment identity table chunk (one entry per segment).
 *
 * The traits which belong to a *segment* rather than to the car which happens to
 * carry them -- the segment's own schedule, where it got in it, its unit number,
 * its name and its "列车分组" -- live here, addressed by the segment ID which
 * every car of that segment stores in Vehicle::r3r_segment_id (written by the
 * R3VP chunk). Before this table those traits were anchored in two different
 * places (the schedule on the lowest-r3r_priority segment head, the unit number
 * and name on the chain head), so a chain whose control segment was not its
 * chain head had two competing answers to "whose number is this".
 *
 * The index of a row is the segment ID itself, which is why this is a sparse
 * table: IDs are recycled through a free list (see R3RSegmentAlloc()), so the
 * saved indices are not contiguous and a game which never splits a chain into
 * several segments writes an empty chunk.
 *
 * Every live row is written, even one whose traits are all still at their
 * defaults: the existence of the row *is* the fact that the segment exists, and
 * a car referring to a segment which has no row (because it was never written)
 * would be indistinguishable from a car of a single-segment chain. The row does
 * not own the order list it points at -- the ORDL chunk writes every list of the
 * pool, including lists which are only referenced from here, exactly as it does
 * for Vehicle::orders -- so the reference is turned back into a pointer by the
 * pointer pass below.
 *
 * Savegames written before this chunk existed simply do not contain it; the
 * cars then all read back as R3R_SEGMENT_NONE, which is what a chain of single
 * segments is anyway. R3RSegmentTableReset() (called from ResetSaveloadData())
 * clears the table before the load, so nothing of a previous game can survive
 * into a savegame which has no R3SG chunk.
 */
static const NamedSaveLoad _r3r_segment_desc[] = {
	NSL("orders",          SLE_REF(R3RSegmentRecord, orders, REF_ORDERLIST)),
	NSL("real_index",      SLE_VAR(R3RSegmentRecord, real_index, SLE_VEHORDERID)),
	NSL("implicit_index",  SLE_VAR(R3RSegmentRecord, implicit_index, SLE_VEHORDERID)),
	NSL("timetable_index", SLE_VAR(R3RSegmentRecord, timetable_index, SLE_VEHORDERID)),
	NSL("orders_borrowed", SLE_VAR(R3RSegmentRecord, orders_borrowed, SLE_BOOL)),
	NSL("unitnumber",      SLE_VAR(R3RSegmentRecord, unitnumber, SLE_UINT16)),
	NSL("name",            SLE_SSTR(R3RSegmentRecord, name, SLE_STR | SLF_ALLOW_CONTROL)),
	NSL("group_id",        SLE_VAR(R3RSegmentRecord, group_id, SLE_UINT16)),
	/* R3R (KI-541): the auto recorded ranges (KI-388/KI-395) are no longer saved.
	 * Their keys (auto_begin/auto_count/auto_signature, auto_num_ranges and the
	 * three slot arrays) simply disappear from the table; a savegame which still
	 * carries them has them skipped on load, because the table header format
	 * ignores keys the running binary does not know. */
};

static void Load_R3SG()
{
	SaveLoadTableData slt = SlTableHeaderOrRiff(_r3r_segment_desc);

	int index;
	while ((index = SlIterateArray()) != -1) {
		if (index <= 0 || index > R3R_SEGMENT_ID_MAX) continue;
		R3RSegmentRecord *rec = R3RSegmentGetOrCreate(static_cast<uint16_t>(index));
		if (rec == nullptr) continue;
		SlObjectLoadFiltered(rec, slt);
	}
}

/**
 * R3R (第 454 轮 / KI-647): is \a ol still a live member of the OrderList pool?
 *
 * Pointer identity only -- the pointer handed in may already be dangling (a parked
 * list which was destroyed while only a segment row still pointed at it, see
 * R3RFindOrderListReferrer() in order_cmd.cpp which only looks at vehicles), so it
 * must never be dereferenced here.
 */
static bool R3ROrderListIsLive(const OrderList *ol)
{
	for (const OrderList *live : OrderList::Iterate()) {
		if (live == ol) return true;
	}
	return false;
}

static void Save_R3SG()
{
	SaveLoadTableData slt = SlTableHeader(_r3r_segment_desc);

	for (size_t id = 1; id < R3RSegmentPoolSize(); id++) {
		R3RSegmentRecord *rec = R3RSegmentGet(static_cast<uint16_t>(id));
		if (rec == nullptr) continue;
		/* R3R (第 454 轮 / KI-647): the row's parked order list may already be dead --
		 * R3RFindOrderListReferrer() only scans vehicles, so a list referenced by a
		 * segment row alone is destroyed without anybody telling the row. Writing such
		 * a stale pointer stores an index which no longer exists in the OrderList pool,
		 * and the next load aborts with "Referencing invalid OrderList". Drop it here;
		 * the parked schedule of that row is unrecoverable either way. */
		if (rec->orders != nullptr && !R3ROrderListIsLive(rec->orders)) {
			rec->orders = nullptr;
		}
		SlSetArrayIndex(static_cast<uint>(id));
		SlObjectSaveFiltered(rec, slt);
	}
}

static void Ptrs_R3SG()
{
	SaveLoadTableData slt = SlPrepareNamedSaveLoadTableForPtrOrNull(_r3r_segment_desc);

	/* R3R (第 454 轮 / KI-647): savegames written before this round can carry an order
	 * list index which does not exist any more -- a parked list which was destroyed
	 * while only a segment row still pointed at it. IntToReference() would then abort
	 * the whole load with "Referencing invalid OrderList", i.e. the savegame becomes
	 * unreadable forever. Clear such a value *before* the pointer pass so the file
	 * stays loadable; every other field of that row is kept.
	 *
	 * The value still holds the raw "index + 1" here (see ReferenceToInt()), so the
	 * pool ID to validate is raw - 1. */

	for (size_t id = 1; id < R3RSegmentPoolSize(); id++) {
		R3RSegmentRecord *rec = R3RSegmentGet(static_cast<uint16_t>(id));
		if (rec == nullptr) continue;
		const size_t raw = (size_t)rec->orders;
		if (raw != 0 && !OrderList::IsValidID(raw - 1)) {
			rec->orders = nullptr;
		}
		SlObjectPtrOrNullFiltered(rec, slt);
	}
}

extern const ChunkHandler couple_group_chunk_handlers[] = {
	{ 'CGPP', Save_CGPP, Load_CGPP, nullptr, nullptr, ChunkType::Table },          // Couple Group Pool chunk
	{ 'CGVR', Save_CGVR, Load_CGVR, nullptr, nullptr, ChunkType::SparseTable },   // Couple Group Vehicle mapping chunk
	{ 'R3VP', Save_R3VP, Load_R3VP, Ptrs_R3VP, nullptr, ChunkType::SparseTable }, // R3R parked schedule / segment bookkeeping chunk
	{ 'R3SG', Save_R3SG, Load_R3SG, Ptrs_R3SG, nullptr, ChunkType::SparseTable }, // R3R segment identity table chunk
	{ 'R3DS', Save_R3DS, Load_R3DS, nullptr, nullptr, ChunkType::SparseTable },   // R3R decouple seam / orphan age chunk
};

extern const ChunkHandlerTable _couple_group_chunk_handlers(couple_group_chunk_handlers);
