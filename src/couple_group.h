/*
 * This file is part of OpenTTD.
 * OpenTTD is free software; you can redistribute it and/or modify it under the terms of the GNU General Public License as published by the Free Software Foundation, version 2.
 * OpenTTD is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 * See the GNU General Public License for more details. You should have received a copy of the GNU General Public License along with OpenTTD. If not, see <https://www.gnu.org/licenses/old-licenses/gpl-2.0>.
 */

/** @file couple_group.h R3R: couple groups -- the hard whitelist for coupling. */

#ifndef COUPLE_GROUP_H
#define COUPLE_GROUP_H

#include "company_type.h"
#include "couple_group_type.h"
#include "core/pool_type.hpp"
#include <string>

struct Train;
struct Vehicle;

/** R3R: the maximum length of a couple group name in characters including '\0'. */
static const uint MAX_LENGTH_COUPLE_GROUP_NAME_CHARS = 32;

struct CoupleGroup;
using CoupleGroupPool = Pool<CoupleGroup, CoupleGroupID, 64>;

extern CoupleGroupPool _couplegroup_pool;

/**
 * R3R: a couple group.
 *
 * Couple groups are the hard whitelist for coupling: two chains may only be
 * coupled when the segments which meet at the coupling point belong to the
 * same couple group. Segments without an assigned group form one single
 * implicit group (see #INVALID_COUPLE_GROUP), so a game which never assigns a
 * group keeps its previous, unrestricted behaviour.
 *
 * A group is attached to a *segment*: the value is stored on the vehicle which
 * heads the segment (the one carrying the SegmentFront marker, or the chain
 * head of the leading segment, which has no marker of its own). A segment may
 * be a member of several groups at once, so the membership is kept as a
 * #CoupleGroupMask bitmask (Q5).
 */
struct CoupleGroup : CoupleGroupPool::PoolItem<&_couplegroup_pool> {
	/**
	 * R3R (D6-①+D6-③): flag bits of a couple group.
	 *
	 * "Open for other companies" is a single decision of the owner -- may other
	 * companies put their own segments into this group *and* couple them across
	 * a company boundary through it -- but it is stored in two bits (D6-③ the
	 * joining half, D6-① the coupling half), because that is how the savegame
	 * field has been laid out since the opt-ins were introduced.
	 *
	 * Only both bits together ever authorise anything, so the game treats them
	 * as one switch: every reader tests #CGF_ALLOW_OTHERS and every writer sets
	 * or clears the two bits at once. Reading both bits also keeps savegames
	 * written before the merge readable -- a group carrying either bit counts
	 * as open -- so no savegame migration is needed.
	 */
	enum CoupleGroupFlag : uint32_t {
		CGF_SHARED = 1 << 0,        ///< R3R (D6-③): the group is open, other companies may assign their own segments to it.
		CGF_CROSS_COMPANY = 1 << 1, ///< R3R (D6-①): segments of different companies may be coupled through this group.
		CGF_ALLOW_OTHERS = CGF_SHARED | CGF_CROSS_COMPANY, ///< R3R: the one "open for other companies" switch; both bits always move together.
	};

	std::string name; ///< Group name, player editable.
	Owner owner;      ///< Company which owns the group.
	uint32_t flags;   ///< Reserved flag bits, see #CoupleGroupFlag.

	CoupleGroup(CoupleGroupID index, CompanyID owner = CompanyID::Invalid()) : PoolItemBase(index), owner(owner), flags(0) {}

	/** R3R: is this group open for other companies (see #CGF_ALLOW_OTHERS)? */
	bool AllowsOthers() const { return (this->flags & static_cast<uint32_t>(CGF_ALLOW_OTHERS)) != 0; }
};

/** @return whether \a group refers to an existing couple group. */
bool R3RIsValidCoupleGroup(CoupleGroupID group);

/**
 * R3R: may the two group sets be coupled together?
 * An empty set counts as one single implicit group, therefore two unassigned
 * segments are compatible, while an unassigned segment is never compatible with
 * an explicitly assigned group. Otherwise a non-empty intersection of the two
 * sets is required (Q5: a segment in several groups matches a partner which
 * shares at least one of them).
 * @param a First group set.
 * @param b Second group set.
 * @return whether the two sets may meet at a coupling point.
 */
bool R3RCoupleGroupMasksCompatible(CoupleGroupMask a, CoupleGroupMask b);

/**
 * R3R (D6-①+D6-③): do \a a and \a b share a group which is open for other companies?
 *
 * Crossing a company boundary is an explicit opt-in on the shared group
 * (#CoupleGroup::CGF_ALLOW_OTHERS), so the implicit group formed by two
 * unassigned segments never authorises it: without an assigned group there is
 * no group the opt-in could have been expressed on.
 *
 * @param a First group set.
 * @param b Second group set.
 * @return whether at least one shared group is open for other companies.
 */
bool R3RCoupleGroupMasksAllowCrossCompany(CoupleGroupMask a, CoupleGroupMask b);

/**
 * R3R: the flags of \a group.
 * @param group Group to inspect.
 * @return The group's flags, or 0 when the group does not exist.
 */
uint32_t R3RGetCoupleGroupFlags(CoupleGroupID group);

/**
 * R3R (D6-①+D6-③): open or close \a group for segments of other companies.
 *
 * This is the one opt-in the owner has over the company boundary, and it covers
 * both halves of it: an open group is visible to the other companies and lets
 * them assign *their own* segments to it (D6-③), and it is also what authorises
 * a coupling between the segments of different companies (D6-①). The two
 * savegame bits which back it (#CoupleGroup::CGF_ALLOW_OTHERS) are always set
 * and cleared together, because never does only one half have an effect:
 * sharing without cross-company coupling would leave foreign segments in a
 * group they can never be coupled through, and cross-company coupling without
 * sharing is unreachable because no foreign segment can ever enter the group.
 *
 * Closing the group withdraws that authorisation and therefore also evicts the
 * segments of other companies from it: a group which is not open must not keep
 * a foreign member, or that member would sit in a group its owner cannot even
 * see anymore. Vehicles which are already coupled together are never separated
 * by this, only the group membership is withdrawn.
 *
 * @param group Group to change.
 * @param allow_others The new state of #CoupleGroup::CGF_ALLOW_OTHERS.
 * @return Number of foreign segments which had to leave the group.
 */
uint R3RSetCoupleGroupAllowOthers(CoupleGroupID group, bool allow_others);

/**
 * R3R: the couple groups of the segment which contains \a v.
 * @param v Any vehicle of the segment.
 * @return The segment's group set, or #COUPLE_GROUP_MASK_NONE when it has none.
 */
CoupleGroupMask R3RGetCoupleGroupsOfSegment(const Train *v);

/**
 * R3R: add a couple group to the segment which contains \a v.
 * The value is stored on the segment head, so callers may pass any vehicle of
 * the segment (Q3: the group follows the segment, not the individual car).
 * Adding a group twice is idempotent.
 * @param v Any vehicle of the segment.
 * @param group Group to add; #INVALID_COUPLE_GROUP is ignored.
 */
void R3RAddCoupleGroupToSegment(Train *v, CoupleGroupID group);

/**
 * R3R: remove a couple group from the segment which contains \a v.
 * Removing the last group puts the segment back into the implicit group.
 * @param v Any vehicle of the segment.
 * @param group Group to remove; #INVALID_COUPLE_GROUP is ignored.
 */
void R3RRemoveCoupleGroupFromSegment(Train *v, CoupleGroupID group);

/**
 * R3R: take the segment which contains \a v out of every couple group.
 * @param v Any vehicle of the segment.
 */
void R3RClearCoupleGroupsOfSegment(Train *v);

/**
 * R3R: are \a coupler and \a target allowed to be coupled?
 *
 * This is the single gate every coupling path goes through, so it also owns the
 * company boundary (D6-①): a coupling which crosses companies additionally
 * requires a shared group which is open for other companies
 * (#CoupleGroup::CGF_ALLOW_OTHERS). The owner of the vehicles is never changed
 * by a coupling (D3).
 *
 * @param coupler The moving chain which wants to couple.
 * @param target The chain which is being coupled onto.
 * @return whether the coupling is allowed by the couple group whitelist.
 */
bool R3RCoupleAllowed(const Train *coupler, const Train *target);

/**
 * R3R: the number of segments currently assigned to \a group.
 * @param group The group to count; #INVALID_COUPLE_GROUP counts the unassigned segments.
 * @return Number of segments.
 */
uint R3RCountSegmentsInCoupleGroup(CoupleGroupID group);

/** @return the name of \a group, or nullptr when the group does not exist. */
const char *R3RGetCoupleGroupName(CoupleGroupID group);

/**
 * R3R: list the names of every group in \a groups, joined with "+".
 * Intended for the one-line places which show the groups of a segment (vehicle
 * details, depot chain tag). Dangling bits are skipped.
 * @param groups Group set to describe.
 * @return The joined names, or an empty string when the set contains no live group.
 */
std::string R3RGetCoupleGroupsNameList(CoupleGroupMask groups);

/**
 * R3R: the number of live groups in \a groups.
 * @param groups Group set to test.
 * @return Number of group bits which refer to an existing group.
 */
uint R3RCountCoupleGroups(CoupleGroupMask groups);

/**
 * R3R: describe the schedule ownership of the chain which contains \a v.
 * The segment with the lowest #r3r_priority owns the schedule the whole chain
 * executes (route A); this reports that segment together with its 1-based
 * position among the chain's segments, i.e. the "segment k of N" shown by the
 * vehicle details window and the depot chain tag. A segment starts at the chain
 * head and at every vehicle carrying the SegmentFront marker.
 * @param v Any vehicle of the chain; may be nullptr.
 * @param owner[out] Head vehicle of the owning segment, or nullptr. May be nullptr.
 * @param index[out] 1-based position of the owning segment, 0 when unknown. May be nullptr.
 * @param total[out] Number of segments in the chain, 0 when unknown. May be nullptr.
 * @return whether \a v was a valid train to inspect.
 */
bool R3RGetChainScheduleOwner(const Train *v, const Train **owner, uint *index, uint *total);

/**
 * R3R: where in the chain does the segment which contains \a v sit?
 * Unlike R3RGetChainScheduleOwner() this answers "which segment is this car in",
 * which is what the depot chain tag shows next to the schedule ownership: with
 * several segments in one chain the answer differs per row, so it can no longer
 * be misread as "this chain has k segments".
 * @param v Any vehicle of the chain; may be nullptr.
 * @param index[out] 1-based position of \a v's segment, 0 when unknown. May be nullptr.
 * @param total[out] Number of segments in the chain, 0 when unknown. May be nullptr.
 * @return whether \a v was a valid train to inspect.
 */
bool R3RGetSegmentPosition(const Train *v, uint *index, uint *total);

/**
 * R3R (D4-1): does the chain which contains \a v span several companies?
 *
 * A coupling never changes the owner of a vehicle (D3), so a chain which was
 * formed across a company boundary holds vehicles of more than one company.
 * Such a chain is frozen read-only for its cross-company part: while it is
 * coupled, neither side may sell, refit or otherwise reconfigure the other
 * side's segment. The owner of a segment always keeps its rights and can
 * decouple and reclaim it whenever it wants.
 *
 * @param v Any vehicle of the chain; may be nullptr.
 * @param company Company which wants to know whether it is part of a mixed chain.
 * @return whether the chain holds vehicles of \a company and of another company.
 */
bool R3RChainSpansCompanies(const Vehicle *v, Owner company);

/**
 * R3R (D4-1): is \a v a foreign segment which is frozen for \a company?
 *
 * This is the predicate every disposal/modification path uses: it is true when
 * \a v belongs to another company but is currently coupled into a chain which
 * also holds vehicles of \a company. A command which would sell, refit or
 * dereference such a vehicle on behalf of \a company must be refused.
 *
 * @param v Vehicle to test; may be nullptr.
 * @param company Company whose rights are queried.
 * @return whether \a v must be treated as read-only for \a company.
 */
bool R3RIsFrozenForeignSegment(const Vehicle *v, Owner company);

/**
 * R3R: remove \a group from every segment which references it.
 * Used when a group is deleted; the segments stay in the game but fall back
 * into the implicit (unassigned) group.
 * @param group Group which is going away.
 */
void R3RUnassignCoupleGroup(CoupleGroupID group);

/**
 * R3R: may \a company see the given group?
 * A group is visible to its owner, and (D6-③) to every company once it is
 * shared -- otherwise nobody would ever find a group they are allowed to join.
 * @param group Group to test, may be nullptr.
 * @param company Company which wants to look at the group.
 * @return whether the group is visible for that company.
 */
bool R3RCoupleGroupIsVisibleTo(const CoupleGroup *group, Owner company);

/**
 * R3R: may \a company rename/delete the given group or assign segments to it?
 * @param group Group to test, may be nullptr.
 * @param company Company which wants to change the group.
 * @return whether the group is manageable by that company.
 */
bool R3RCoupleGroupIsManageable(const CoupleGroup *group, Owner company);

/**
 * R3R (D6-③): may \a company add its own segments to the given group?
 *
 * This is the weaker, "membership only" right: the owner may always fill its
 * own group, every other company only once the owner opened it with
 * #CoupleGroup::CGF_ALLOW_OTHERS. Whoever passes this test may only ever assign
 * vehicles it owns itself -- the ownership of the vehicle is checked separately
 * by the command, so the sharing flag never hands out control over somebody
 * else's rolling stock (D3).
 *
 * @param group Group to test, may be nullptr.
 * @param company Company which wants to join the group.
 * @return whether the company may assign its own segments to that group.
 */
bool R3RCoupleGroupIsJoinableBy(const CoupleGroup *group, Owner company);

/**
 * R3R: drop every reference to a couple group which is missing from the
 * savegame. This is a post-load repair: a dangling reference must never be
 * observed by the game, it would silently block coupling forever.
 */
void AfterLoadCoupleGroups();

#endif /* COUPLE_GROUP_H */
