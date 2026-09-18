/*
 * This file is part of OpenTTD.
 * OpenTTD is free software; you can redistribute it and/or modify it under the terms of the GNU General Public License as published by the Free Software Foundation, version 2.
 * OpenTTD is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 * See the GNU General Public License for more details. You should have received a copy of the GNU General Public License along with OpenTTD. If not, see <https://www.gnu.org/licenses/old-licenses/gpl-2.0>.
 */

/** @file couple_group_gui.h R3R: GUI for the couple groups. */

#ifndef COUPLE_GROUP_GUI_H
#define COUPLE_GROUP_GUI_H

#include "company_type.h"
#include "couple_group_type.h"

/**
 * R3R: Show the couple group management window for \a company, or bring it to the front if it is already open.
 * @param company The company whose couple groups should be shown.
 */
void ShowCoupleGroupWindow(Owner company);

/**
 * R3R: Enter "pick a segment" mode for \a group. While this mode is active,
 * clicking a vehicle in a depot assigns the segment of that vehicle to \a group.
 * @param group The group to add the picked segment to.
 */
void R3RStartCoupleGroupPick(CoupleGroupID group);

/** R3R: Is the "pick a segment" mode active? */
bool R3RIsCoupleGroupPickActive();

/** R3R: The group which is the target of the "pick a segment" mode, or #INVALID_COUPLE_GROUP. */
CoupleGroupID R3RGetCoupleGroupPickTarget();

/** R3R: Leave the "pick a segment" mode. */
void R3RCancelCoupleGroupPick();

#endif /* COUPLE_GROUP_GUI_H */
