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
#include "core/pool_func.hpp"

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
		if ((R3RGetCoupleGroupsOfSegment(t) & bit) == COUPLE_GROUP_MASK_NONE) continue;
		R3RRemoveCoupleGroupFromSegment(t, group);
		evicted++;
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

bool R3RCoupleAllowed(const Train *coupler, const Train *target)
{
	if (coupler == nullptr || target == nullptr) return false;

	const CoupleGroupMask coupler_groups = R3RGetCoupleGroupsOfSegment(coupler);
	const CoupleGroupMask target_groups = R3RGetCoupleGroupsOfSegment(target);
	if (!R3RCoupleGroupMasksCompatible(coupler_groups, target_groups)) return false;

	/* R3R (D6-①): the company boundary is a second, independent gate. Two
	 * segments of the same company couple exactly as before; two segments of
	 * different companies additionally need a shared group which the owner
	 * opened for other companies. A rejected candidate is reported upstream as
	 * "no waiting consist here", so a locomotive never couples across companies
	 * by accident and simply keeps looking for another candidate. */
	if (coupler->owner != target->owner) {
		return R3RCoupleGroupMasksAllowCrossCompany(coupler_groups, target_groups);
	}
	return true;
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
		const CoupleGroupMask groups = R3RGetCoupleGroupsOfSegment(t);
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
}
