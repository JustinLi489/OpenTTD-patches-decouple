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
	/**
	 * R3R (第 144 轮 / 需求叁): the parent group, i.e. this group is a *sub* group of it.
	 *
	 * This is both the display hierarchy (the management list and the order drop
	 * down draw it indented under its parent) and, since 第 146 轮, a *membership*
	 * relation: a segment which is in a sub group counts as a member of every
	 * ancestor as well -- "石头 里面的列车也自动就是 滚木 里面的列车". The stored
	 * mask keeps holding the groups a segment was explicitly put into; the
	 * ancestors are OR-ed in on every read, see
	 * R3RGetEffectiveCoupleGroupsOfSegment(). #INVALID_COUPLE_GROUP means "top level".
	 */
	CoupleGroupID parent = INVALID_COUPLE_GROUP;

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
 * R3R (第 146 轮 / 需求叁改): the couple groups of \a v *including its ancestors*.
 *
 * The parent tree is a membership relation: a segment in the sub group "石头"
 * whose parent is "滚木" is a member of 滚木 as well, so every read which decides
 * "is this segment a member of that group" (management list, counts, the name
 * shown on the vehicle, the cross-company gate) must use this instead of
 * R3RGetCoupleGroupsOfSegment(). The stored mask is never expanded, so dragging a
 * group to another parent changes the membership of all its members at once.
 *
 * Do NOT use this where a mask is written back (e.g. copying the control
 * segment's groups onto the other segment heads after coupling): that would
 * bake the ancestors into the savegame.
 *
 * @param v Any vehicle of the segment.
 * @return The segment's effective group set (own groups plus every ancestor's).
 */
CoupleGroupMask R3RGetEffectiveCoupleGroupsOfSegment(const Train *v);

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
 * R3R (KI-182): the couple target lock -- "已有耦合目标" (the pair flag).
 *
 * A locomotive which starts executing a GOTO_COUPLE order immediately locks
 * onto exactly one waiting segment, and both sides record the other's chain
 * head (#Vehicle::r3r_couple_target on the locomotive, #Vehicle::r3r_couple_requester
 * on the waiting consist). From then on the locomotive only ever drives to,
 * and may only ever couple onto, that one consist: R3RCoupleAllowed() requires
 * a matching pair, which is the single gate every level of the couple
 * resolution asks (pathfinder destination, back-walk safety, arrival gate).
 *
 * A lock is recognised as live only while *both* halves point at each other,
 * and the side keeping the lock is still in the right order state. That makes a
 * stale lock harmless: it simply stops matching and the next scan re-locks.
 */

/**
 * R3R (KI-182): do \a coupler and \a target carry a matching pair flag?
 * This is the pure flag test; the order state of either side is not inspected.
 * @param coupler The chain which wants to couple.
 * @param target The chain which would be coupled onto.
 * @return whether the two chains have locked onto each other.
 */
bool R3RCouplePairMatches(const Train *coupler, const Train *target);

/**
 * R3R (KI-182): the chain which is locked to \a v as a couple partner, if any.
 * Symmetric: it answers both for the locomotive (which stores the target) and
 * for the waiting consist (which stores the locomotive), and it returns the
 * other side only while the lock is a live, two-sided pair. Callers therefore
 * also use it to ask "is this consist already somebody else's target?".
 * @param v Any vehicle of the chain; may be nullptr.
 * @return The paired chain head, or nullptr when \a v carries no live lock.
 */
Train *R3RGetCouplePairPartner(const Train *v);

/**
 * R3R (KI-182): does the chain which contains \a v currently carry a couple target lock?
 * @param v Any vehicle of the chain; may be nullptr.
 * @return whether either half of the pair flag is set on the chain head.
 */
bool R3RHasCouplePair(const Train *v);

/**
 * R3R (KI-182): lock \a coupler and \a target onto each other.
 * Both sides are expected to be the chain heads; the previous lock of either
 * side (and any counter-lock pointing at them) is dropped first.
 * @param coupler The locomotive's chain head.
 * @param target The waiting consist's chain head.
 * @return whether the pair was established.
 */
bool R3RPairCoupleTargets(Train *coupler, Train *target);

/**
 * R3R (KI-182): drop every couple target lock which involves the chain of \a v.
 * Clears both halves stored on the chain head and the counter-half stored on
 * whatever chain the head was locked with.
 * @param v Any vehicle of the chain; may be nullptr.
 */
void R3RUnpairCoupleTargets(Train *v);

/**
 * R3R (KI-182): R3RCoupleAllowed() without the pair flag test.
 *
 * Used while *selecting* a target -- at that moment no pair exists yet -- and
 * nowhere else: every path which actually resolves or executes a coupling must
 * ask R3RCoupleAllowed() so the pair flag is enforced.
 *
 * @param coupler The moving chain which wants to couple.
 * @param target The chain which is being coupled onto.
 * @return whether the couple group whitelist and the order destination allow it.
 */
bool R3RCoupleAllowedIgnoringPair(const Train *coupler, const Train *target);

/**
 * R3R (KI-170): which couple group is the segment of \a v temporarily a member of?
 *
 * The player's rule: a GOTO_COUPLE order may name one real couple group (the
 * temporary, "fake" couple group), and while the train runs that order the
 * segment carrying it counts as a member of that group *as well* -- the
 * locomotive of group "石头" whose order names group "滚木" temporarily belongs
 * to both, so it may couple onto a consist of "滚木" and still onto the stock of
 * its own group. R3RCoupleAllowed() ORs the group into the segment's real mask,
 * so a group it does not share stays rejected: it is no master key.
 *
 * A group named by an order which has since been deleted behaves like "none".
 *
 * @param v Any vehicle of the chain; may be nullptr.
 * @return The group joined, or #INVALID_COUPLE_GROUP when there is none.
 */
CoupleGroupID R3RGetTempCoupleGroup(const Train *v);

/**
 * R3R (KI-170): does the segment which contains \a v currently hold a temporary
 * ("fake") couple group?
 *
 * That membership is derived state, never stored: it is in force exactly while
 * the order is the current OT_GOTO_COUPLE order, and it is gone as soon as the
 * order advances after a successful coupling (Couple() logs that as
 * CGRP-FAKE-DESTROY). The order lives on the chain head, because ProcessOrders()
 * only ticks there and R3RCoupleAllowed() reads that same vehicle's
 * current_order, so the temporary group belongs to the head's segment and to no
 * other segment of the chain.
 *
 * This is the read-only mirror of that rule for the user interface (the depot
 * window and the vehicle list badge the chain while it is in force); it decides
 * nothing about coupling itself.
 *
 * @param v Any vehicle of the chain; may be nullptr.
 * @return whether \a v belongs to a segment which holds a temporary group.
 */
bool R3RHasTempCoupleGroup(const Train *v);

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

/**
 * R3R (第 144 轮 / 需求叁): the parent of \a group.
 * @param group Group to inspect.
 * @return The parent group, or #INVALID_COUPLE_GROUP when \a group is a top level
 *         group (or does not exist).
 */
CoupleGroupID R3RGetCoupleGroupParent(CoupleGroupID group);

/**
 * R3R (第 144 轮 / 需求叁): how deep \a group sits in the parent tree (0 = top level).
 * Defensive against a broken/cyclic chain in an old savegame: walking stops after
 * #MAX_LENGTH_COUPLE_GROUP_NAME_CHARS steps.
 * @param group Group to inspect.
 * @return Nesting depth, 0 when the group does not exist.
 */
uint R3RGetCoupleGroupDepth(CoupleGroupID group);

/**
 * R3R (第 144 轮 / 需求叁): may \a parent become the parent of \a group?
 *
 * Refuses a group as its own parent, an unknown parent, and every assignment which
 * would close a cycle (that would make the tree unwalkable and R3RGetCoupleGroupDepth()
 * would spin forever on it).
 * @param group Group which should get a parent.
 * @param parent The candidate parent; #INVALID_COUPLE_GROUP means "make it top level".
 * @return whether the assignment is allowed.
 */
bool R3RCanCoupleGroupHaveParent(CoupleGroupID group, CoupleGroupID parent);

#endif /* COUPLE_GROUP_H */
