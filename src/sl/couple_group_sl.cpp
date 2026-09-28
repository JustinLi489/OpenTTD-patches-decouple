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
	/* R3R (第 144 轮 / 需求伍): 段的「列车分组」归属停放（整链统一到控制段时借出，
	 * 该段重新成为链头时取回）。与其它停放字段一样只在稀疏块里写，不动车辆表布局。 */
	NSL("group_id_backup",      SLE_VAR(Vehicle, group_id_backup, SLE_UINT16)),
	NSL("orders_borrowed",      SLE_VAR(Vehicle, r3r_orders_borrowed, SLE_BOOL)),
	NSL("priority",             SLE_VAR(Vehicle, r3r_priority, SLE_UINT16)),
};

/** Is there any point in writing this vehicle's park state out? */
static bool R3RHasParkState(const Vehicle *v)
{
	return v->orders_backup != nullptr ||
			v->orders_backup_real_index != INVALID_VEH_ORDER_ID ||
			v->orders_backup_implicit_index != INVALID_VEH_ORDER_ID ||
			v->unitnumber_backup != 0 ||
			v->r3r_orders_borrowed ||
			v->group_id_backup != GroupID::Invalid() ||
			v->r3r_priority != 1;
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

static void Save_R3VP()
{
	SaveLoadTableData slt = SlTableHeader(_r3r_park_desc);

	for (Vehicle *v : Vehicle::Iterate()) {
		if (!R3RHasParkState(v)) continue;
		SlSetArrayIndex(v->index);
		SlObjectSaveFiltered(v, slt);
	}
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

extern const ChunkHandler couple_group_chunk_handlers[] = {
	{ 'CGPP', Save_CGPP, Load_CGPP, nullptr, nullptr, CH_TABLE },          // Couple Group Pool chunk
	{ 'CGVR', Save_CGVR, Load_CGVR, nullptr, nullptr, CH_SPARSE_TABLE },   // Couple Group Vehicle mapping chunk
	{ 'R3VP', Save_R3VP, Load_R3VP, Ptrs_R3VP, nullptr, CH_SPARSE_TABLE }, // R3R parked schedule / segment bookkeeping chunk
};

extern const ChunkHandlerTable _couple_group_chunk_handlers(couple_group_chunk_handlers);
