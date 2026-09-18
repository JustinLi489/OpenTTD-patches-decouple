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

extern const ChunkHandler couple_group_chunk_handlers[] = {
	{ 'CGPP', Save_CGPP, Load_CGPP, nullptr, nullptr, CH_TABLE },        // Couple Group Pool chunk
	{ 'CGVR', Save_CGVR, Load_CGVR, nullptr, nullptr, CH_SPARSE_TABLE }, // Couple Group Vehicle mapping chunk
};

extern const ChunkHandlerTable _couple_group_chunk_handlers(couple_group_chunk_handlers);
