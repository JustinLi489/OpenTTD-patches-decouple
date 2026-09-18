/*
 * This file is part of OpenTTD.
 * OpenTTD is free software; you can redistribute it and/or modify it under the terms of the GNU General Public License as published by the Free Software Foundation, version 2.
 * OpenTTD is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 * See the GNU General Public License for more details. You should have received a copy of the GNU General Public License along with OpenTTD. If not, see <https://www.gnu.org/licenses/old-licenses/gpl-2.0>.
 */

/** @file couple_group_cmd.cpp R3R: commands to manage couple groups. */

#include "stdafx.h"
#include "couple_group_cmd.h"

#include "company_func.h"
#include "couple_group.h"
#include "string_func.h"
#include "r3r_perf.h"
#include "train.h"
#include "window_func.h"

#include "table/strings.h"

#include "safeguards.h"

/**
 * R3R: is \a name a name which may be used for a couple group?
 * @param name The name to check.
 * @return true iff the name is non-empty and not too long.
 */
static bool R3RIsValidCoupleGroupName(const std::string &name)
{
	if (name.empty()) return false;
	size_t length = Utf8StringLength(name);
	return length > 0 && length < MAX_LENGTH_COUPLE_GROUP_NAME_CHARS;
}

/**
 * R3R: is \a name already used by another couple group of \a company?
 * @param exclude The couple group to ignore (i.e. the one being renamed).
 * @param company The company whose couple groups are checked.
 * @param name The name to check.
 * @return true iff another couple group of the company already uses the name.
 */
static bool R3RIsCoupleGroupNameInUse(CoupleGroupID exclude, Owner company, const std::string &name)
{
	for (const CoupleGroup *cg : CoupleGroup::Iterate()) {
		if (cg->index == exclude) continue;
		if (cg->owner == company && cg->name == name) return true;
	}
	return false;
}

/**
 * R3R: create a new couple group.
 * @param flags type of operation
 * @param name The name of the new couple group.
 * @return the cost of this operation or an error
 */
CommandCost CmdCreateCoupleGroup(DoCommandFlags flags, const std::string &name)
{
	if (!R3RIsValidCoupleGroupName(name)) return CMD_ERROR;
	if (R3RIsCoupleGroupNameInUse(INVALID_COUPLE_GROUP, _current_company, name)) return CommandCost(STR_ERROR_NAME_MUST_BE_UNIQUE);
	if (!CoupleGroup::CanAllocateItem()) return CMD_ERROR;

	CommandCost result;
	if (flags.Test(DoCommandFlag::Execute)) {
		CoupleGroup *cg = CoupleGroup::Create(_current_company);
		cg->name = name;

		result.SetResultData(cg->index);
		InvalidateWindowClassesData(WindowClass::CoupleGroup, 0);
	}
	return result;
}

/**
 * R3R: rename an existing couple group.
 * @param flags type of operation
 * @param group The couple group to rename.
 * @param name The new name of the couple group.
 * @return the cost of this operation or an error
 */
CommandCost CmdRenameCoupleGroup(DoCommandFlags flags, CoupleGroupID group, const std::string &name)
{
	CoupleGroup *cg = CoupleGroup::GetIfValid(group);
	if (cg == nullptr) return CMD_ERROR;
	if (!R3RCoupleGroupIsManageable(cg, _current_company)) return CMD_ERROR;
	if (!R3RIsValidCoupleGroupName(name)) return CMD_ERROR;
	if (R3RIsCoupleGroupNameInUse(group, cg->owner, name)) return CommandCost(STR_ERROR_NAME_MUST_BE_UNIQUE);

	if (flags.Test(DoCommandFlag::Execute)) {
		cg->name = name;
		InvalidateWindowClassesData(WindowClass::CoupleGroup, 0);
	}
	return CommandCost();
}

/**
 * R3R: delete an existing couple group.
 *
 * All segments in the group are moved into the implicit group (i.e. they are
 * no longer restricted in which segments they may couple with), the group
 * itself is then removed.
 *
 * @param flags type of operation
 * @param group The couple group to delete.
 * @return the cost of this operation or an error
 */
CommandCost CmdDeleteCoupleGroup(DoCommandFlags flags, CoupleGroupID group)
{
	CoupleGroup *cg = CoupleGroup::GetIfValid(group);
	if (cg == nullptr) return CMD_ERROR;
	if (!R3RCoupleGroupIsManageable(cg, _current_company)) return CMD_ERROR;

	if (flags.Test(DoCommandFlag::Execute)) {
		R3RUnassignCoupleGroup(group);
		delete cg;

		/* R3R (KI-56): deliberately no CloseWindowById(WindowClass::CoupleGroup, ...)
		 * here.  The couple group window is a single window registered with
		 * window_number 0, so using the *group* id as window number closed the window
		 * itself as soon as group 0 (the first group created) was deleted.  The list
		 * is refreshed through the invalidation below. */
		InvalidateWindowClassesData(WindowClass::CoupleGroup, 0);
	}
	return CommandCost();
}

/**
 * R3R: add a train segment to a couple group.
 *
 * The group is stored on the segment of \a vehicle, so it does not matter
 * which vehicle of the segment is passed in here. A segment may be a member of
 * several groups at once (Q5), so this only adds \a group; adding a group which
 * the segment is already in does nothing at all.
 *
 * A company may only ever assign a vehicle it owns itself (CheckOwnership), so
 * the sharing flag (D6-③) only decides which groups may receive our segment:
 * our own group, or one which its owner opened for other companies. A shared
 * group may therefore be filled by anybody, but always only with that company's
 * own rolling stock (D3).
 *
 * @param flags type of operation
 * @param vehicle The vehicle whose segment is to be assigned.
 * @param group The couple group to add the segment to; #INVALID_COUPLE_GROUP is ignored.
 * @return the cost of this operation or an error
 */
CommandCost CmdSetCoupleGroup(DoCommandFlags flags, VehicleID vehicle, CoupleGroupID group)
{
	Train *t = Train::GetIfValid(vehicle);
	if (t == nullptr) return CMD_ERROR;

	CommandCost ret = CheckOwnership(t->owner);
	if (ret.Failed()) return ret;

	if (group == INVALID_COUPLE_GROUP) return CMD_ERROR;
	const CoupleGroup *cg = CoupleGroup::GetIfValid(group);
	if (cg == nullptr || !R3RCoupleGroupIsJoinableBy(cg, _current_company)) return CMD_ERROR;

	if (flags.Test(DoCommandFlag::Execute)) {
		/* D6-③: a segment joining the group of another company is the event
		 * which makes the cross-company opt-in reachable at all, so log it. */
		if (cg->owner != t->owner) {
			R3RDbgWrite("[R3R] CG-JOIN-foreign veh=%u car_owner=%u group_owner=%u group=%u\n",
					vehicle.base(), t->owner.base(), cg->owner.base(), group.base());
		}
		R3RAddCoupleGroupToSegment(t, group);

		SetWindowDirty(WindowClass::VehicleView, vehicle.base());
		InvalidateWindowClassesData(WindowClass::CoupleGroup, 0);
	}
	return CommandCost();
}

/**
 * R3R (D6-①+D6-③): open or close a couple group for the segments of other companies.
 *
 * This is the one opt-in the owner has over the company boundary, and it covers
 * both halves of it: an open group is visible to the other companies and lets
 * them assign *their own* segments to it (D6-③), which is the only way two
 * companies can ever end up in the same group, and it is what authorises the
 * coupling of those segments across the company boundary (D6-①). The owner of
 * the vehicles is never touched; the switch only widens (or narrows) the
 * whitelist consulted by R3RCoupleAllowed().
 *
 * Closing it withdraws that authorisation and takes the segments of the other
 * companies back out of the group (see R3RSetCoupleGroupAllowOthers). Neither
 * direction changes the owner of a vehicle, and a chain which is already
 * coupled together is never separated by this (D3).
 *
 * @param flags type of operation
 * @param group The couple group to change.
 * @param allow_others The new state of #CoupleGroup::CGF_ALLOW_OTHERS.
 * @return the cost of this operation or an error
 */
CommandCost CmdSetCoupleGroupAllowOthers(DoCommandFlags flags, CoupleGroupID group, bool allow_others)
{
	CoupleGroup *cg = CoupleGroup::GetIfValid(group);
	if (cg == nullptr) return CMD_ERROR;
	if (!R3RCoupleGroupIsManageable(cg, _current_company)) return CMD_ERROR;

	if (flags.Test(DoCommandFlag::Execute)) {
		const uint evicted = R3RSetCoupleGroupAllowOthers(group, allow_others);
		R3RDbgWrite("[R3R] CG-OPEN group=%u allow_others=%u evicted=%u\n",
				group.base(), allow_others ? 1 : 0, evicted);
		InvalidateWindowClassesData(WindowClass::CoupleGroup, 0);
	}
	return CommandCost();
}

/**
 * R3R: take a train segment out of one couple group.
 *
 * Removing the last group puts the segment back into the implicit group, i.e.
 * it may couple with any other unassigned segment again.
 *
 * @param flags type of operation
 * @param vehicle The vehicle whose segment is to be changed.
 * @param group The couple group to remove from the segment.
 * @return the cost of this operation or an error
 */
CommandCost CmdRemoveCoupleGroup(DoCommandFlags flags, VehicleID vehicle, CoupleGroupID group)
{
	Train *t = Train::GetIfValid(vehicle);
	if (t == nullptr) return CMD_ERROR;

	CommandCost ret = CheckOwnership(t->owner);
	if (ret.Failed()) return ret;

	if (group == INVALID_COUPLE_GROUP || CoupleGroup::GetIfValid(group) == nullptr) return CMD_ERROR;

	if (flags.Test(DoCommandFlag::Execute)) {
		R3RRemoveCoupleGroupFromSegment(t, group);

		SetWindowDirty(WindowClass::VehicleView, vehicle.base());
		InvalidateWindowClassesData(WindowClass::CoupleGroup, 0);
	}
	return CommandCost();
}
