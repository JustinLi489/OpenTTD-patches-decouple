/*
 * This file is part of OpenTTD.
 * OpenTTD is free software; you can redistribute it and/or modify it under the terms of the GNU General Public License as published by the Free Software Foundation, version 2.
 * OpenTTD is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 * See the GNU General Public License for more details. You should have received a copy of the GNU General Public License along with OpenTTD. If not, see <https://www.gnu.org/licenses/old-licenses/gpl-2.0>.
 */

/** @file couple_group_type.h R3R: couple group pool ID types. */

#ifndef COUPLE_GROUP_TYPE_H
#define COUPLE_GROUP_TYPE_H

#include "core/pool_type.hpp"

/** Couple group pool ID type. */
struct CoupleGroupIDTag : public PoolIDTraits<uint16_t, 0xFFF0, 0xFFFF> {};
using CoupleGroupID = PoolID<CoupleGroupIDTag>;

/**
 * R3R: sentinel for "this segment has no couple group assigned".
 * All segments carrying this value are treated as one single implicit group,
 * so savegames which never assign a group keep their previous behaviour.
 */
static constexpr CoupleGroupID INVALID_COUPLE_GROUP{0xFFFF};

/** R3R: sentinel used by commands/UI to request the creation of a new couple group. */
static constexpr CoupleGroupID NEW_COUPLE_GROUP{0xFFFE};

/**
 * R3R: a set of couple groups, stored as one bit per group.
 *
 * A segment may belong to several couple groups at once (Q5), so the membership
 * is kept as a bitmask instead of a single group ID. The couple group pool holds
 * at most 64 entries (see #CoupleGroupPool), therefore the 64 bits of the mask
 * can address every group which can ever exist; a mask bit which does not
 * correspond to a live group is simply ignored (see AfterLoadCoupleGroups).
 *
 * A mask of #COUPLE_GROUP_MASK_NONE means "not assigned to any group" and forms
 * one single implicit group together with every other unassigned segment, so a
 * game which never assigns a group keeps its previous, unrestricted behaviour.
 */
using CoupleGroupMask = uint64_t;

/** R3R: the number of groups which fit into a #CoupleGroupMask. */
static constexpr uint R3R_COUPLE_GROUP_MASK_BITS = 64;

/** R3R: mask of a segment which is not assigned to any couple group. */
static constexpr CoupleGroupMask COUPLE_GROUP_MASK_NONE = 0;

/**
 * R3R: the single-bit mask of \a group.
 * @param group Group to convert; #INVALID_COUPLE_GROUP and other out-of-range IDs map to no bit at all.
 * @return A mask with exactly the bit of \a group set, or #COUPLE_GROUP_MASK_NONE.
 */
inline CoupleGroupMask R3RCoupleGroupBit(CoupleGroupID group)
{
	return (group.base() < R3R_COUPLE_GROUP_MASK_BITS) ? (CoupleGroupMask(1) << group.base()) : COUPLE_GROUP_MASK_NONE;
}

/**
 * R3R (第 152 轮): the segment identity a car carries while it has none.
 *
 * Every car stores the ID of the segment it belongs to in
 * #Vehicle::r3r_segment_id, so a segment keeps its identity -- its schedule,
 * its unit number, its name and its train group -- when the physical front
 * marker (#VehicleRailFlag::SegmentFront) moves, when cars are spliced in or
 * out, or when the vehicle pool is shuffled by unrelated deletions. The
 * identity data itself lives in the segment table (see #R3RSegmentRecord),
 * which is what makes moving the marker free: only the marker moves, never the
 * data.
 *
 * 0 is never a valid segment ID. A chain which consists of a single segment has
 * no second identity to keep apart -- the chain head's own fields are the only
 * authority -- so its cars keep this value and nothing about a plain train is
 * ever written to the savegame or looked up in the table. IDs are handed out
 * the moment a chain really holds two or more segments, see R3RSegmentAlloc().
 */
static constexpr uint16_t R3R_SEGMENT_NONE = 0;

/** R3R (第 152 轮): the highest segment ID the identity table can hand out. */
static constexpr uint16_t R3R_SEGMENT_ID_MAX = 0xFFFE;

#endif /* COUPLE_GROUP_TYPE_H */
