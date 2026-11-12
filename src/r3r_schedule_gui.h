/*
 * This file is part of OpenTTD.
 * OpenTTD is free software; you can redistribute it and/or modify it under the terms of the GNU General Public License as published by the Free Software Foundation, version 2.
 * OpenTTD is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 * See the GNU General Public License for more details. You should have received a copy of the GNU General Public License along with OpenTTD. If not, see <https://www.gnu.org/licenses/old-licenses/gpl-2.0>.
 */

/** @file r3r_schedule_gui.h R3R: the schedule pool (排程池) management window. */

#ifndef R3R_SCHEDULE_GUI_H
#define R3R_SCHEDULE_GUI_H

#include "company_type.h"
#include "vehicle_type.h"

/**
 * R3R (第 417 轮): open (or bring to the front) the schedule pool window of \a company.
 * @param company The company whose schedules are managed.
 * @param target  The vehicle the "assign" button initially applies to, or an invalid id.
 */
void ShowR3RScheduleWindow(CompanyID company, VehicleID target);

struct OrderList;

/**
 * R3R (第 421 轮, C-8 / E-2): open the order editor of an ownerless (loose) schedule.
 *
 * A loose schedule has no vehicle, so the normal VehicleOrders window cannot be used:
 * it is keyed on a VehicleID and its buttons post vehicle commands. This window edits
 * the order list directly through the dedicated R3R commands.
 *
 * @param ol The ownerless schedule to edit; must not be nullptr.
 */
void ShowR3RScheduleEditor(OrderList *ol);

#endif /* R3R_SCHEDULE_GUI_H */
