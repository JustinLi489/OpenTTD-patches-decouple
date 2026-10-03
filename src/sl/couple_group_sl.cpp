/*
 * This file is part of OpenTTD.
 * OpenTTD is free software; you can redistribute it and/or modify it under the terms of the GNU General Public License as published by the Free Software Foundation, version 2.
 * OpenTTD is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 * See the GNU General Public License for more details. You should have received a copy of the GNU General Public License along with OpenTTD. If not, see <https://www.gnu.org/licenses/old-licenses/gpl-2.0>.
 */

/** @file couple_group_sl.cpp R3R: save/load of couple groups. */

#include "../stdafx.h"
#include "../couple_group.h"

#include "../r3r_perf.h"
#include "../train.h"
#include "../vehicle_base.h"

#include "saveload.h"

#include "../safeguards.h"

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
	NSL("orders_backup",        SLE_REF(Vehicle, orders_backup, REF_ORDERLIST)),
	NSL("order_backup_index",   SLE_VAR(Vehicle, orders_backup_real_index, SLE_VEHORDERID)),
	NSL("order_backup_index_i", SLE_VAR(Vehicle, orders_backup_implicit_index, SLE_VEHORDERID)),
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
	NSL("priority",             SLE_VAR(Vehicle, r3r_priority, SLE_UINT16)),
	/* R3R (第 152 轮): which segment this car belongs to. The identity data lives
	 * in the R3SG chunk below, so this is only the address; a car whose chain
	 * holds a single segment keeps R3R_SEGMENT_NONE and writes nothing. */
	NSL("segment_id",           SLE_VAR(Vehicle, r3r_segment_id, SLE_UINT16)),
};

/** Is there any point in writing this vehicle's park state out? */
static bool R3RHasParkState(const Vehicle *v)
{
	return v->orders_backup != nullptr ||
			v->orders_backup_real_index != INVALID_VEH_ORDER_ID ||
			v->orders_backup_implicit_index != INVALID_VEH_ORDER_ID ||
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

	/* R3R (第 191 轮 probe, read-only): 停放副本的读档留底，与 SEGSAVE-PARK 逐车比对。
	 * 车号读档错（KI-293）如果出自"存档往返"，本行与 SEGSAVE-PARK 就会有差；
	 * 若两者一致，则元凶在读档后的重建（SEGTR-SNAP tag=load 那一条）。 */
	FILE *dbg = R3RDbgOn() ? fopen("R3R_debug.log", "a") : nullptr;

	int index;
	while ((index = SlIterateArray()) != -1) {
		Vehicle *v = Vehicle::GetIfValid(index);
		if (v == nullptr) continue;
		SlObjectLoadFiltered(v, slt);
		if (dbg != nullptr) {
			fprintf(dbg, "SEGLOAD-PARK veh=%u unit=%u ubk=%u n=\"%s\" nbk=\"%s\" gid=%d gbk=%d seg=%u borrowed=%d prio=%u\n",
					(unsigned)v->index.base(),
					(unsigned)v->unitnumber,
					(unsigned)v->unitnumber_backup,
					v->name.empty() ? "" : v->name.c_str(),
					v->name_backup.empty() ? "" : v->name_backup.c_str(),
					(v->group_id == GroupID::Invalid()) ? -1 : (int)v->group_id.base(),
					(v->group_id_backup == GroupID::Invalid()) ? -1 : (int)v->group_id_backup.base(),
					(unsigned)v->r3r_segment_id,
					v->r3r_orders_borrowed ? 1 : 0,
					(unsigned)v->r3r_priority);
		}
	}
	if (dbg != nullptr) fclose(dbg);
}

static void Save_R3VP()
{
	SaveLoadTableData slt = SlTableHeader(_r3r_park_desc);

	/* R3R (第 191 轮 probe, read-only): 存档侧留底。玩家 2026-10-01 报"耦合的列车存档再
	 * 读档后车号错"，两个可能差了一个存档往返：(a) 号/名压根没写进档（R3VP 判定漏了某个
	 * 字段），(b) 写了但读档重建时覆盖掉。SEGSAVE-PARK（写档时的停放副本）与读档期的
	 * SEGTR-SNAP tag=load-raw（读回来的样子）逐车比对即可区分元凶。
	 * 只打有停放态的车辆，与写出判据同源（R3RHasParkState），不会给正常档添噪声。 */
	const bool dbg_on = R3RDbgOn();
	FILE *dbg = dbg_on ? fopen("R3R_debug.log", "a") : nullptr;

	for (Vehicle *v : Vehicle::Iterate()) {
		if (!R3RHasParkState(v)) continue;
		if (dbg != nullptr) {
			fprintf(dbg, "SEGSAVE-PARK veh=%u unit=%u ubk=%u n=\"%s\" nbk=\"%s\" gid=%d gbk=%d seg=%u borrowed=%d prio=%u\n",
					(unsigned)v->index.base(),
					(unsigned)v->unitnumber,
					(unsigned)v->unitnumber_backup,
					v->name.empty() ? "" : v->name.c_str(),
					v->name_backup.empty() ? "" : v->name_backup.c_str(),
					(v->group_id == GroupID::Invalid()) ? -1 : (int)v->group_id.base(),
					(v->group_id_backup == GroupID::Invalid()) ? -1 : (int)v->group_id_backup.base(),
					(unsigned)v->r3r_segment_id,
					v->r3r_orders_borrowed ? 1 : 0,
					(unsigned)v->r3r_priority);
		}
		SlSetArrayIndex(v->index);
		SlObjectSaveFiltered(v, slt);
	}
	if (dbg != nullptr) fclose(dbg);
}

static void Ptrs_R3VP()
{
	SaveLoadTableData slt = SlPrepareNamedSaveLoadTableForPtrOrNull(_r3r_park_desc);

	for (Vehicle *v : Vehicle::Iterate()) {
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
};

static void Load_R3SG()
{
	SaveLoadTableData slt = SlTableHeaderOrRiff(_r3r_segment_desc);

	/* R3R (第 191 轮 probe, read-only): 段行的读档留底，与 SEGSAVE-ROW（写档侧）成对
	 * —— 两者不一致即"存档往返本身丢/改了数据"，两者一致而链上 SEGTR-SNAP 的 row
	 * 不同即"读档重建把行覆盖了"（KI-294/295 的两种分叉）。 */
	FILE *dbg = R3RDbgOn() ? fopen("R3R_debug.log", "a") : nullptr;

	int index;
	while ((index = SlIterateArray()) != -1) {
		if (index <= 0 || index > R3R_SEGMENT_ID_MAX) continue;
		R3RSegmentRecord *rec = R3RSegmentGetOrCreate(static_cast<uint16_t>(index));
		if (rec == nullptr) continue;
		SlObjectLoadFiltered(rec, slt);
		if (dbg != nullptr) {
			fprintf(dbg, "SEGLOAD-ROW id=%u u=%u n=\"%s\" g=%d borrowed=%d orders=%d\n",
					(unsigned)index,
					(unsigned)rec->unitnumber,
					rec->name.empty() ? "" : rec->name.c_str(),
					(rec->group_id == GroupID::Invalid()) ? -1 : (int)rec->group_id.base(),
					rec->orders_borrowed ? 1 : 0,
					(rec->orders != nullptr) ? 1 : 0);
		}
	}
	if (dbg != nullptr) fclose(dbg);
}

static void Save_R3SG()
{
	SaveLoadTableData slt = SlTableHeader(_r3r_segment_desc);

	/* R3R (第 191 轮 probe, read-only): 段行是号/名的正式宿主，写档时把每一行原样留底，
	 * 与读档后的 SEGTR-SNAP `row u=/n=` 比对。name 走 SLE_SSTR（std::string），写空串是
	 * 合法的，所以"行的名字丢了"只能由本行与读取侧两头对照才能定性（KI-294）。 */
	FILE *dbg = R3RDbgOn() ? fopen("R3R_debug.log", "a") : nullptr;

	for (size_t id = 1; id < R3RSegmentPoolSize(); id++) {
		R3RSegmentRecord *rec = R3RSegmentGet(static_cast<uint16_t>(id));
		if (rec == nullptr) continue;
		if (dbg != nullptr) {
			fprintf(dbg, "SEGSAVE-ROW id=%u use=%d u=%u n=\"%s\" g=%d borrowed=%d orders=%d\n",
					(unsigned)id, rec->in_use ? 1 : 0,
					(unsigned)rec->unitnumber,
					rec->name.empty() ? "" : rec->name.c_str(),
					(rec->group_id == GroupID::Invalid()) ? -1 : (int)rec->group_id.base(),
					rec->orders_borrowed ? 1 : 0,
					(rec->orders != nullptr) ? 1 : 0);
		}
		SlSetArrayIndex(static_cast<uint>(id));
		SlObjectSaveFiltered(rec, slt);
	}
	if (dbg != nullptr) fclose(dbg);
}

static void Ptrs_R3SG()
{
	SaveLoadTableData slt = SlPrepareNamedSaveLoadTableForPtrOrNull(_r3r_segment_desc);

	for (size_t id = 1; id < R3RSegmentPoolSize(); id++) {
		R3RSegmentRecord *rec = R3RSegmentGet(static_cast<uint16_t>(id));
		if (rec == nullptr) continue;
		SlObjectPtrOrNullFiltered(rec, slt);
	}
}

extern const ChunkHandler couple_group_chunk_handlers[] = {
	{ 'CGPP', Save_CGPP, Load_CGPP, nullptr, nullptr, CH_TABLE },          // Couple Group Pool chunk
	{ 'CGVR', Save_CGVR, Load_CGVR, nullptr, nullptr, CH_SPARSE_TABLE },   // Couple Group Vehicle mapping chunk
	{ 'R3VP', Save_R3VP, Load_R3VP, Ptrs_R3VP, nullptr, CH_SPARSE_TABLE }, // R3R parked schedule / segment bookkeeping chunk
	{ 'R3SG', Save_R3SG, Load_R3SG, Ptrs_R3SG, nullptr, CH_SPARSE_TABLE }, // R3R segment identity table chunk
};

extern const ChunkHandlerTable _couple_group_chunk_handlers(couple_group_chunk_handlers);
