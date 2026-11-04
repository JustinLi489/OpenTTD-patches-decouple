/*
 * This file is part of OpenTTD.
 * OpenTTD is free software; you can redistribute it and/or modify it under the terms of the GNU General Public License as published by the Free Software Foundation, version 2.
 * OpenTTD is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 * See the GNU General Public License for more details. You should have received a copy of the GNU General Public License along with OpenTTD. If not, see <https://www.gnu.org/licenses/old-licenses/gpl-2.0>.
 */

/** @file timetable_cmd.cpp Commands related to time tabling. */

#include "stdafx.h"
#include "command_func.h"
#include "company_func.h"
#include "date_func.h"
#include "date_type.h"
#include "debug.h"
#include "window_func.h"
#include "vehicle_base.h"
#include "vehicle_func.h"
#include "train.h"
#include "settings_type.h"
#include "company_base.h"
#include "settings_type.h"
#include "scope.h"
#include "timetable_cmd.h"

#include "r3r_perf.h"

#include "widgets/vehicle_widget.h"

#include "table/strings.h"

#include "safeguards.h"

/**
 * R3R (AUD-390-4): apply a timetable operation to every vehicle which actually
 * drives the same order list as \a v_start.
 *
 * This is deliberately not FirstShared()/NextShared(): a vehicle which follows
 * the order list of a control segment (R3R borrowed orders) runs exactly the same
 * timetable but is not a member of the shared order list ring, so a plain ring
 * walk left it with a different view of that timetable - the switch flipped on
 * some vehicles of a timetable but not on all of them.
 *
 * @param v_start Any vehicle of the timetable; its effective order list decides the group.
 * @param func    Callable applied to every vehicle in that group.
 */
template <typename Func>
static void R3RForEachTimetableVehicle(Vehicle *v_start, Func func)
{
	const OrderList *const list = v_start->GetEffectiveOrders();
	if (list == nullptr) {
		func(v_start);
		return;
	}
	for (Vehicle *v : Vehicle::Iterate()) {
		if (!v->IsPrimaryVehicle()) continue;
		if (v->GetEffectiveOrders() != list) continue;
		func(v);
	}
}

/**
 * Change/update a particular timetable entry.
 * @param v            The vehicle to change the timetable of.
 * @param order_number The index of the timetable in the order list.
 * @param val          The new data of the timetable entry.
 * @param mtf          Which part of the timetable entry to change.
 * @param timetabled   If the new value is explicitly timetabled.
 * @param ignore_lock  If the change should be applied even if the value is locked.
 */
static void ChangeTimetable(Vehicle *v, VehicleOrderID order_number, uint32_t val, ModifyTimetableFlags mtf, bool timetabled, bool ignore_lock = false)
{
	Order *order = v->GetOrder(order_number);
	assert(order != nullptr);
	if (order->HasNoTimetableTimes()) return;

	/* R3R (KI-391): #OT_DECOUPLE is an instant command and carries neither a wait
	 * nor a travel time, while #OT_WAIT_COUPLE only waits for a coupling partner.
	 * Clamp (instead of rejecting) so that stale values written by older builds are
	 * cleared as well and the duration bookkeeping stays consistent. */
	if ((mtf == MTF_WAIT_TIME && !order->HasWaitTimetableTime()) ||
			(mtf == MTF_TRAVEL_TIME && !order->HasTravelTimetableTime())) {
		val = 0;
		timetabled = false;
		ignore_lock = true;
	}

	int total_delta = 0;
	int timetable_delta = 0;

	switch (mtf) {
		case MTF_WAIT_TIME:
			if (!ignore_lock && order->IsWaitFixed()) return;
			if (!order->IsType(OT_CONDITIONAL)) {
				total_delta = val - order->GetWaitTime();
				timetable_delta = (timetabled ? val : 0) - order->GetTimetabledWait();
			}
			order->SetWaitTime(val);
			order->SetWaitTimetabled(timetabled);
			if (v->vehicle_flags.Test(VehicleFlag::ScheduledDispatch) && timetabled && order->IsScheduledDispatchOrder(true)) {
				/* R3R (AUD-390-4): the dispatch lateness bookkeeping of a timetable
				 * also covers the vehicles following its order list. */
				R3RForEachTimetableVehicle(v, [&](Vehicle *u) {
					if (u->cur_implicit_order_index == order_number && order->IsBaseStationOrder() && u->last_station_visited == order->GetDestination()) {
						u->lateness_counter += timetable_delta;
					}
				});
			}
			break;

		case MTF_TRAVEL_TIME:
			if (!ignore_lock && order->IsTravelFixed()) return;
			if (!order->IsType(OT_CONDITIONAL)) {
				total_delta = val - order->GetTravelTime();
				timetable_delta = (timetabled ? val : 0) - order->GetTimetabledTravel();
			}
			if (order->IsType(OT_CONDITIONAL)) assert_msg(val == order->GetTravelTime(), "{} == {}", val, order->GetTravelTime());
			order->SetTravelTime(val);
			order->SetTravelTimetabled(timetabled);
			break;

		case MTF_TRAVEL_SPEED:
			order->SetMaxSpeed(val);
			break;

		case MTF_SET_WAIT_FIXED:
			order->SetWaitFixed(val != 0);
			break;

		case MTF_SET_TRAVEL_FIXED:
			order->SetTravelFixed(val != 0);
			break;

		case MTF_SET_LEAVE_TYPE:
			order->SetLeaveType((OrderLeaveType)val);
			break;

		case MTF_ASSIGN_SCHEDULE:
			if ((int)val >= 0) {
				for (int n = 0; n < v->GetNumOrders(); n++) {
					Order *o = v->GetOrder(n);
					if (o->GetDispatchScheduleIndex() == (int)val) {
						o->SetDispatchScheduleIndex(-1);
					}
				}
			}
			order->SetDispatchScheduleIndex((int)val);
			break;

		default:
			NOT_REACHED();
	}
	v->GetEffectiveOrders()->UpdateTotalDuration(total_delta);
	v->GetEffectiveOrders()->UpdateTimetableDuration(timetable_delta);

	SetTimetableWindowsDirty(v, (mtf == MTF_ASSIGN_SCHEDULE) ? STWDF_SCHEDULED_DISPATCH : STWDF_NONE);

	/* R3R (AUD-390-4): the in-flight order of every vehicle running this timetable
	 * has to follow the change, including the followers of a control segment. */
	R3RForEachTimetableVehicle(v, [&](Vehicle *v2) {
		if (v2->cur_real_order_index == order_number && v2->current_order.IsDerivedFrom(*order)) {
			switch (mtf) {
				case MTF_WAIT_TIME:
					v2->current_order.SetWaitTime(val);
					v2->current_order.SetWaitTimetabled(timetabled);
					break;

				case MTF_TRAVEL_TIME:
					v2->current_order.SetTravelTime(val);
					v2->current_order.SetTravelTimetabled(timetabled);
					break;

				case MTF_TRAVEL_SPEED:
					v2->current_order.SetMaxSpeed(val);
					break;

				case MTF_SET_WAIT_FIXED:
					v2->current_order.SetWaitFixed(val != 0);
					break;

				case MTF_SET_TRAVEL_FIXED:
					v2->current_order.SetTravelFixed(val != 0);
					break;

				case MTF_SET_LEAVE_TYPE:
					v2->current_order.SetLeaveType((OrderLeaveType)val);
					break;

				case MTF_ASSIGN_SCHEDULE:
					v2->current_order.SetDispatchScheduleIndex((int)val);
					break;

				default:
					NOT_REACHED();
			}
		}
	});
}

/**
 * Change timetable data of an order.
 * @param flags Operation to perform.
 * @param veh Vehicle with the orders to change.
 * @param order_number Order index to modify.
 * @param mtf Timetable data to change (@see ModifyTimetableFlags)
 * @param data The data to modify as specified by \c mtf.
 *             0 to clear times, UINT16_MAX to clear speed limit.
 * @param ctrl_flags Control flags (ModifyTimetableCtrlFlag::ClearField to clear timetable wait/travel time)
 * @return the cost of this operation or an error
 */
CommandCost CmdChangeTimetable(DoCommandFlags flags, VehicleID veh, VehicleOrderID order_number, ModifyTimetableFlags mtf, uint32_t data, ModifyTimetableCtrlFlags ctrl_flags)
{
	Vehicle *v = Vehicle::GetIfValid(veh);
	if (v == nullptr || !IsCompanyBuildableVehicleType(v) || !v->IsPrimaryVehicle()) return CMD_ERROR;

	CommandCost ret = CheckOwnership(v->owner);
	if (ret.Failed()) return ret;

	Order *order = v->GetOrder(order_number);
	if (order == nullptr || order->IsType(OT_IMPLICIT) || order->HasNoTimetableTimes()) return CMD_ERROR;

	if (mtf >= MTF_END) return CMD_ERROR;

	bool clear_field = ctrl_flags.Test(ModifyTimetableCtrlFlag::ClearField);

	TimetableTicks wait_time   = order->GetWaitTime();
	TimetableTicks travel_time = order->GetTravelTime();
	int max_speed   = order->GetMaxSpeed();
	bool wait_fixed = order->IsWaitFixed();
	bool travel_fixed = order->IsTravelFixed();
	OrderLeaveType leave_type = order->GetLeaveType();
	int dispatch_index = order->GetDispatchScheduleIndex();
	switch (mtf) {
		case MTF_WAIT_TIME:
			wait_time = data;
			if (clear_field && wait_time != 0) return CMD_ERROR;
			break;

		case MTF_TRAVEL_TIME:
			travel_time = data;
			if (clear_field && travel_time != 0) return CMD_ERROR;
			break;

		case MTF_TRAVEL_SPEED:
			max_speed = static_cast<uint16_t>(data);
			if (max_speed == 0) max_speed = UINT16_MAX; // Disable speed limit.
			break;

		case MTF_SET_WAIT_FIXED:
			wait_fixed = data != 0;
			if (v->type != VehicleType::Train && wait_fixed && order->IsType(OT_GOTO_WAYPOINT)) {
				return CommandCost(STR_ERROR_TIMETABLE_ONLY_WAIT_AT_STATIONS);
			}
			break;

		case MTF_SET_TRAVEL_FIXED:
			travel_fixed = data != 0;
			break;

		case MTF_SET_LEAVE_TYPE:
			leave_type = (OrderLeaveType)data;
			if (leave_type >= OLT_END) return CMD_ERROR;
			break;

		case MTF_ASSIGN_SCHEDULE:
			dispatch_index = (int)data;
			if (dispatch_index < -1 || dispatch_index >= (int)v->GetEffectiveOrders()->GetScheduledDispatchScheduleCount()) return CMD_ERROR;
			break;

		default:
			NOT_REACHED();
	}

	if (wait_time != order->GetWaitTime() || leave_type != order->GetLeaveType()) {
		switch (order->GetType()) {
			case OT_GOTO_STATION:
				if (order->GetNonStopType() & ONSF_NO_STOP_AT_DESTINATION_STATION) {
					if (mtf == MTF_WAIT_TIME && clear_field) break;
					return CommandCost(STR_ERROR_TIMETABLE_NOT_STOPPING_HERE);
				}
				break;

			case OT_GOTO_WAYPOINT:
				if (v->type != VehicleType::Train && !clear_field) {
					return CommandCost(STR_ERROR_TIMETABLE_ONLY_WAIT_AT_STATIONS);
				}
				break;

			case OT_GOTO_DEPOT:
				break;

			case OT_CONDITIONAL:
				break;

			case OT_GOTO_COUPLE:
			case OT_WAIT_COUPLE:
				/* R3R (KI-391): waiting for a coupling partner is a timetabled wait. */
				break;

			default: return CommandCost(STR_ERROR_TIMETABLE_ONLY_WAIT_AT_STATIONS);
		}
	}

	if (dispatch_index != order->GetDispatchScheduleIndex()) {
		switch (order->GetType()) {
			case OT_GOTO_STATION:
				if (order->GetNonStopType() & ONSF_NO_STOP_AT_DESTINATION_STATION) {
					if (mtf == MTF_ASSIGN_SCHEDULE && dispatch_index == -1) break;
					return CommandCost(STR_ERROR_TIMETABLE_NOT_STOPPING_HERE);
				}
				break;

			case OT_GOTO_DEPOT:
			case OT_GOTO_WAYPOINT:
				break;

			default: return CommandCost(STR_ERROR_TIMETABLE_ONLY_WAIT_AT_STATIONS);
		}
	}

	if (travel_time != order->GetTravelTime() && order->IsType(OT_CONDITIONAL)) return CMD_ERROR;
	/* R3R (KI-391): #OT_DECOUPLE and #OT_WAIT_COUPLE have no travel time. Clearing
	 * (travel_time == 0) stays allowed so stale values can be removed. */
	if (travel_time > 0 && !order->HasTravelTimetableTime()) return CMD_ERROR;
	if (travel_fixed != order->IsTravelFixed() && order->IsType(OT_CONDITIONAL)) return CMD_ERROR;
	if (max_speed != order->GetMaxSpeed() && (order->IsType(OT_CONDITIONAL) || v->type == VehicleType::Aircraft)) return CMD_ERROR;
	if (leave_type != order->GetLeaveType() && order->IsType(OT_CONDITIONAL)) return CMD_ERROR;

	if (flags.Test(DoCommandFlag::Execute)) {
		switch (mtf) {
			case MTF_WAIT_TIME:
				/* Set time if changing the value or confirming an estimated time as timetabled. */
				if (wait_time != order->GetWaitTime() || (clear_field == order->IsWaitTimetabled())) {
					ChangeTimetable(v, order_number, wait_time, MTF_WAIT_TIME, !clear_field, true);
				}
				break;

			case MTF_TRAVEL_TIME:
				/* Set time if changing the value or confirming an estimated time as timetabled. */
				if (travel_time != order->GetTravelTime() || (clear_field == order->IsTravelTimetabled())) {
					ChangeTimetable(v, order_number, travel_time, MTF_TRAVEL_TIME, !clear_field, true);
				}
				break;

			case MTF_TRAVEL_SPEED:
				if (max_speed != order->GetMaxSpeed()) {
					ChangeTimetable(v, order_number, max_speed, MTF_TRAVEL_SPEED, max_speed != UINT16_MAX, true);
				}
				break;

			case MTF_SET_WAIT_FIXED:
				if (wait_fixed != order->IsWaitFixed()) {
					ChangeTimetable(v, order_number, wait_fixed ? 1 : 0, MTF_SET_WAIT_FIXED, false, true);
				}
				break;

			case MTF_SET_TRAVEL_FIXED:
				if (travel_fixed != order->IsTravelFixed()) {
					ChangeTimetable(v, order_number, travel_fixed ? 1 : 0, MTF_SET_TRAVEL_FIXED, false, true);
				}
				break;

			case MTF_SET_LEAVE_TYPE:
				if (leave_type != order->GetLeaveType()) {
					ChangeTimetable(v, order_number, leave_type, MTF_SET_LEAVE_TYPE, true);
				}
				break;

			case MTF_ASSIGN_SCHEDULE:
				if (dispatch_index != order->GetDispatchScheduleIndex()) {
					ChangeTimetable(v, order_number, dispatch_index, MTF_ASSIGN_SCHEDULE, true);
				}
				break;

			default:
				break;
		}

		/* R3R (AUD-390-4): unbunching data is no longer valid for any vehicle in this
		 * timetable, including the followers of a control segment. */
		R3RForEachTimetableVehicle(v, [&](Vehicle *u) {
			u->ResetDepotUnbunching();
		});
	}

	return CommandCost();
}

/**
 * Change timetable data of all orders of a vehicle.
 * @param flags Operation to perform.
 * @param veh Vehicle with the orders to change.
 * @param mtf Timetable data to change (@see ModifyTimetableFlags)
 * @param data The data to modify as specified by \c mtf.
 *             0 to clear times, UINT16_MAX to clear speed limit.
 * @param ctrl_flags Control flags (ModifyTimetableCtrlFlag::ClearField to clear timetable wait/travel time)
 * @return the cost of this operation or an error
 */
CommandCost CmdBulkChangeTimetable(DoCommandFlags flags, VehicleID veh, ModifyTimetableFlags mtf, uint32_t data, ModifyTimetableCtrlFlags ctrl_flags)
{
	Vehicle *v = Vehicle::GetIfValid(veh);
	if (v == nullptr || !IsCompanyBuildableVehicleType(v) || !v->IsPrimaryVehicle()) return CMD_ERROR;

	CommandCost ret = CheckOwnership(v->owner);
	if (ret.Failed()) return ret;

	if (mtf >= MTF_END) return CMD_ERROR;

	if (v->GetNumOrders() == 0) return CMD_ERROR;

	if (flags.Test(DoCommandFlag::Execute)) {
		for (VehicleOrderID order_number = 0; order_number < v->GetNumOrders(); order_number++) {
			Order *order = v->GetOrder(order_number);
			if (order == nullptr || order->IsType(OT_IMPLICIT)) continue;

			/* Exclude waypoints from set all wait times command */
			if (mtf == MTF_WAIT_TIME && !ctrl_flags.Test(ModifyTimetableCtrlFlag::ClearField) && order->IsType(OT_GOTO_WAYPOINT)) continue;

			Command<Commands::ChangeTimetable>::Do(flags, v->index, order_number, mtf, data, ctrl_flags);
		}
	}

	return CommandCost();
}


/**
 * R3R (KI-389): read-only probe for "the three timetable buttons do nothing".
 *
 * The timetable window leaves "automate" / "autofill" / "separation" enabled for own
 * vehicles, so a click which silently has no effect means the DoCommand below hit a
 * `return CMD_ERROR` guard. Printing who the window's vehicle actually is tells the
 * three candidate causes apart:
 *  - H-a: a car inside a segment (its GVSF_FRONT was cleared when it was absorbed),
 *  - H-b: the window was opened on a non-front car (`ShowTimetableWindow` keeps the
 *         clicked index as window number and never normalises to `First()`),
 *  - H-c: the control segment itself lost GVSF_FRONT at some commit point.
 * Logged unconditionally (subject to R3RDbgOn()), i.e. for successful clicks too, so
 * "no TT-CMD line at all" really means the command was never invoked.
 */
static void R3RTimetableCmdProbe(const char *cmd, const Vehicle *v, VehicleID veh)
{
	if (!R3RDbgOn()) return;

	if (v == nullptr) {
		R3RDbgWrite("TT-CMD %s veh=%d found=0\n", cmd, (int)veh.base());
		return;
	}

	bool front = false;
	bool front_wagon = false;
	if (v->type == VehicleType::Train) {
		const Train *const t = Train::From(v);
		front = t->IsFrontEngine();
		front_wagon = t->IsFrontWagon();
	}

	R3RDbgWrite("TT-CMD %s veh=%d found=1 type=%d primary=%d front=%d frontwagon=%d orders=%p owner=%d first=%d segid=%u prio=%u borrowed=%d\n",
			cmd, (int)veh.base(), (int)v->type, v->IsPrimaryVehicle() ? 1 : 0,
			front ? 1 : 0, front_wagon ? 1 : 0, (const void *)v->GetEffectiveOrders(),
			(int)v->owner.base(), (int)v->First()->index.base(), (unsigned)v->r3r_segment_id,
			(unsigned)v->r3r_priority, v->r3r_orders_borrowed ? 1 : 0);
}

/**
 * Clear the lateness counter to make the vehicle on time.
 * @param flags Operation to perform.
 * @param veh Vehicle with the orders to change.
 * @param apply_to_group Set to reset the late counter for all vehicles sharing the orders.
 * @return the cost of this operation or an error
 */
CommandCost CmdSetVehicleOnTime(DoCommandFlags flags, VehicleID veh, bool apply_to_group)
{
	Vehicle *v = Vehicle::GetIfValid(veh);
	R3RTimetableCmdProbe("on-time", v, veh);
	if (v == nullptr || !IsCompanyBuildableVehicleType(v) || !v->IsPrimaryVehicle() || v->GetEffectiveOrders() == nullptr) return CMD_ERROR;

	/* A vehicle can't be late if its timetable hasn't started.
	 * If we're setting all vehicles in the group, we handle that below. */
	if (!apply_to_group && !v->vehicle_flags.Test(VehicleFlag::TimetableStarted)) return CommandCost(STR_ERROR_TIMETABLE_NOT_STARTED);

	CommandCost ret = CheckOwnership(v->owner);
	if (ret.Failed()) return ret;

	if (flags.Test(DoCommandFlag::Execute)) {
		if (apply_to_group) {
			/* R3R (AUD-390-4): "the group" is every vehicle which drives this
			 * timetable, which also covers vehicles following a control segment's
			 * order list (they are not members of the shared order ring). */
			int32_t most_late = 0;
			R3RForEachTimetableVehicle(v, [&](Vehicle *u) {
				/* A vehicle can't be late if its timetable hasn't started. */
				if (!v->vehicle_flags.Test(VehicleFlag::TimetableStarted)) return;

				if (u->lateness_counter > most_late) {
					most_late = u->lateness_counter;
				}

				/* Unbunching data is no longer valid. */
				u->ResetDepotUnbunching();
			});
			if (most_late > 0) {
				R3RForEachTimetableVehicle(v, [&](Vehicle *u) {
					/* A vehicle can't be late if its timetable hasn't started. */
					if (!v->vehicle_flags.Test(VehicleFlag::TimetableStarted)) return;

					u->lateness_counter -= most_late;
					SetWindowDirty(WindowClass::VehicleTimetable, u->index);
				});
			}
		} else {
			v->lateness_counter = 0;
			/* Unbunching data is no longer valid. */
			v->ResetDepotUnbunching();
			SetWindowDirty(WindowClass::VehicleTimetable, v->index);
		}
	}

	return CommandCost();
}

/**
 * Order vehicles based on their timetable. The vehicles will be sorted in order
 * they would reach the first station.
 * @copydoc GUIList::Sorter
 */
static bool VehicleTimetableSorter(Vehicle * const &a, Vehicle * const &b)
{
	VehicleOrderID a_order = a->cur_real_order_index;
	VehicleOrderID b_order = b->cur_real_order_index;
	int j = (int)b_order - (int)a_order;

	/* Are we currently at an ordered station (un)loading? */
	bool a_load = (a->current_order.IsType(OT_LOADING) && a->current_order.GetNonStopType() != ONSF_STOP_EVERYWHERE) || a->current_order.IsType(OT_LOADING_ADVANCE);
	bool b_load = (b->current_order.IsType(OT_LOADING) && b->current_order.GetNonStopType() != ONSF_STOP_EVERYWHERE) || b->current_order.IsType(OT_LOADING_ADVANCE);

	/* If the current order is not loading at the ordered station, decrease the order index by one since we have
	 * not yet arrived at the station (and thus the timetable entry; still in the travelling of the previous one).
	 * Since the ?_order variables are unsigned the -1 will flow under and place the vehicles going to order #0 at
	 * the begin of the list with vehicles arriving at #0. */
	if (!a_load) a_order--;
	if (!b_load) b_order--;

	/* First check the order index that accounted for loading, then just the raw one. */
	int i = (int)b_order - (int)a_order;
	if (i != 0) return i < 0;
	if (j != 0) return j < 0;

	/* Look at the time we spent in this order; the higher, the closer to its destination. */
	i = b->current_order_time - a->current_order_time;
	if (i != 0) return i < 0;

	/* If all else is equal, use some unique index to sort it the same way. */
	int k = b->unitnumber - a->unitnumber;
	if (k != 0) return k < 0;

	return b->index < a->index;
}

/**
 * Set the start date of the timetable.
 * @param flags Operation to perform.
 * @param veh Vehicle ID.
 * @param timetable_all Set to set timetable start for all vehicles sharing this order
 * @param start_state_tick The state tick when the timetable starts.
 * @return The error or cost of the operation.
 */
CommandCost CmdSetTimetableStart(DoCommandFlags flags, VehicleID veh, bool timetable_all, StateTicks start_state_tick)
{
	Vehicle *v = Vehicle::GetIfValid(veh);
	if (v == nullptr || !IsCompanyBuildableVehicleType(v) || !v->IsPrimaryVehicle() || v->GetEffectiveOrders() == nullptr) return CMD_ERROR;

	CommandCost ret = CheckOwnership(v->owner);
	if (ret.Failed()) return ret;

	/* Don't let a timetable start more than 15 unscaled years into the future... */
	if (start_state_tick - _state_ticks > 15 * DAY_TICKS * DAYS_IN_LEAP_YEAR) return CMD_ERROR;
	/* ...or 1 unscaled year in the past. */
	if (_state_ticks - start_state_tick > DAY_TICKS * DAYS_IN_LEAP_YEAR) return CMD_ERROR;

	if (timetable_all && !v->GetEffectiveOrders()->IsCompleteTimetable()) return CommandCost(STR_ERROR_TIMETABLE_INCOMPLETE);

	if (flags.Test(DoCommandFlag::Execute)) {
		std::vector<Vehicle *> vehs;

		if (timetable_all) {
			/* R3R (AUD-390-4): "all vehicles of this timetable" includes the vehicles
			 * following the order list of a control segment. */
			R3RForEachTimetableVehicle(v, [&](Vehicle *w) {
				vehs.push_back(w);
			});
			SetTimetableWindowsDirty(v);
		} else {
			vehs.push_back(v);
			SetWindowDirty(WindowClass::VehicleTimetable, v->index);
		}

		int total_duration = v->GetEffectiveOrders()->GetTimetableTotalDuration();
		int num_vehs = (uint)vehs.size();

		if (num_vehs >= 2) {
			std::sort(vehs.begin(), vehs.end(), &VehicleTimetableSorter);
		}

		int idx = 0;

		for (Vehicle *w : vehs) {
			w->lateness_counter = 0;
			w->vehicle_flags.Reset(VehicleFlag::TimetableStarted);
			/* Do multiplication, then division to reduce rounding errors. */
			w->timetable_start = start_state_tick + ((idx * total_duration) / num_vehs);

			/* Unbunching data is no longer valid. */
			v->ResetDepotUnbunching();

			++idx;
		}

	}

	return CommandCost();
}


/**
 * Start or stop filling the timetable automatically from the time the vehicle
 * actually takes to complete it. When starting to autofill the current times
 * are cleared and the timetable will start again from scratch.
 * @param flags Operation to perform.
 * @param veh Vehicle index.
 * @param autofill Enable or disable autofill
 * @param preserve_wait_time Set to preserve waiting times in non-destructive mode
 * @return the cost of this operation or an error
 */
CommandCost CmdAutofillTimetable(DoCommandFlags flags, VehicleID veh, bool autofill, bool preserve_wait_time)
{
	Vehicle *v = Vehicle::GetIfValid(veh);
	R3RTimetableCmdProbe("autofill", v, veh);
	if (v == nullptr || !IsCompanyBuildableVehicleType(v) || !v->IsPrimaryVehicle() || v->GetEffectiveOrders() == nullptr) return CMD_ERROR;

	CommandCost ret = CheckOwnership(v->owner);
	if (ret.Failed()) return ret;

	if (flags.Test(DoCommandFlag::Execute)) {
		if (autofill) {
			/* Start autofilling the timetable, which clears the
			 * "timetable has started" bit. Times are not cleared anymore, but are
			 * overwritten when the order is reached now. */
			v->vehicle_flags.Set(VehicleFlag::AutofillTimetable);
			v->vehicle_flags.Reset(VehicleFlag::TimetableStarted);

			/* Overwrite waiting times only if they got longer */
			if (preserve_wait_time) v->vehicle_flags.Set(VehicleFlag::AutofillPreserveWaitTime);

			v->timetable_start = StateTicks{0};
			v->lateness_counter = 0;
		} else {
			v->vehicle_flags.Reset(VehicleFlag::AutofillTimetable);
			v->vehicle_flags.Reset(VehicleFlag::AutofillPreserveWaitTime);
		}

		/* R3R (AUD-390-4): only one vehicle of a timetable may autofill, and that
		 * includes followers which are not members of the shared order list ring. */
		R3RForEachTimetableVehicle(v, [&](Vehicle *v2) {
			if (v2 != v) {
				/* Stop autofilling; only one vehicle at a time can perform autofill */
				v2->vehicle_flags.Reset(VehicleFlag::AutofillTimetable);
				v2->vehicle_flags.Reset(VehicleFlag::AutofillPreserveWaitTime);
			}
		});
		SetTimetableWindowsDirty(v);
	}

	return CommandCost();
}

/**
* Start or stop automatic management of timetables.
 * @param flags Operation to perform.
 * @param veh Vehicle index.
 * @param automate Whether to enable/disable automation.
 * @return the cost of this operation or an error
 */
CommandCost CmdAutomateTimetable(DoCommandFlags flags, VehicleID veh, bool automate)
{
	Vehicle *v = Vehicle::GetIfValid(veh);
	R3RTimetableCmdProbe("automate", v, veh);
	if (v == nullptr || !IsCompanyBuildableVehicleType(v) || !v->IsPrimaryVehicle()) return CMD_ERROR;

	CommandCost ret = CheckOwnership(v->owner);
	if (ret.Failed()) return ret;

	if (flags.Test(DoCommandFlag::Execute)) {
		/* R3R (AUD-390-4): automation is a property of the timetable, so it has to be
		 * switched for followers as well, not only for the shared ring members. */
		R3RForEachTimetableVehicle(v, [&](Vehicle *v2) {
			if (automate) {
				/* Automated timetable. Set flags and clear current times if also auto-separating. */
				v2->vehicle_flags.Set(VehicleFlag::AutomateTimetable);
				v2->vehicle_flags.Reset(VehicleFlag::AutofillTimetable);
				v2->vehicle_flags.Reset(VehicleFlag::AutofillPreserveWaitTime);
				if (v2->vehicle_flags.Test(VehicleFlag::TimetableSeparation)) v2->timetable_start = StateTicks{0};
				v2->StopSeparation();
			} else {
				/* De-automate timetable. Clear flags. */
				v2->vehicle_flags.Reset(VehicleFlag::AutomateTimetable);
				v2->vehicle_flags.Reset(VehicleFlag::AutofillTimetable);
				v2->vehicle_flags.Reset(VehicleFlag::AutofillPreserveWaitTime);
				v2->vehicle_flags.Reset(VehicleFlag::SeparationActive);
			}
		});
		SetTimetableWindowsDirty(v);
	}

	return CommandCost();
}

/**
 * Enable or disable auto timetable separation
 * @param flags Operation to perform.
 * @param veh Vehicle index.
 * @param separation Whether to enable/disable auto separatiom.
 * @return the cost of this operation or an error
 */
CommandCost CmdTimetableSeparation(DoCommandFlags flags, VehicleID veh, bool separation)
{
	Vehicle *v = Vehicle::GetIfValid(veh);
	R3RTimetableCmdProbe("separation", v, veh);
	if (v == nullptr || !IsCompanyBuildableVehicleType(v) || !v->IsPrimaryVehicle()) return CMD_ERROR;

	CommandCost ret = CheckOwnership(v->owner);
	if (ret.Failed()) return ret;

	if (separation) {
		/* R3R (AUD-390-4): the mutual exclusion with scheduled dispatch has to hold
		 * for the whole timetable, not just for the vehicle the player clicked on,
		 * otherwise a follower could keep a combination which is forbidden. */
		bool conflict = false;
		R3RForEachTimetableVehicle(v, [&](Vehicle *v2) {
			if (v2->vehicle_flags.Test(VehicleFlag::ScheduledDispatch) || v2->HasUnbunchingOrder()) conflict = true;
		});
		if (conflict) return CommandCost(STR_ERROR_SEPARATION_MUTUALLY_EXCLUSIVE);
	}

	if (flags.Test(DoCommandFlag::Execute)) {
		R3RForEachTimetableVehicle(v, [&](Vehicle *v2) {
			if (separation) {
				v2->vehicle_flags.Set(VehicleFlag::TimetableSeparation);
			} else {
				v2->vehicle_flags.Reset(VehicleFlag::TimetableSeparation);
			}
			v2->vehicle_flags.Reset(VehicleFlag::SeparationActive);
		});
		SetTimetableWindowsDirty(v, STWDF_SCHEDULED_DISPATCH);
	}

	return CommandCost();
}

static inline bool IsOrderUsableForSeparation(const Order *order)
{
	if (order->HasNoTimetableTimes()) return true;

	/* R3R (KI-391/KI-392): #OT_DECOUPLE is an instant command and #OT_WAIT_COUPLE
	 * only waits, so neither can ever carry the travel time checked for below.
	 * Without this they made every schedule containing an R3R command unusable
	 * for automatic separation. */
	if (!order->HasTravelTimetableTime()) return true;

	if (order->GetWaitTime() == 0 && order->IsType(OT_GOTO_STATION) && !(order->GetNonStopType() & ONSF_NO_STOP_AT_DESTINATION_STATION)) {
		// non-station orders are permitted to have 0 wait times
		return false;
	}

	if (order->GetTravelTime() == 0 && !order->IsTravelTimetabled()) {
		// 0 travel times are permitted, if explicitly timetabled
		return false;
	}

	return true;
}

std::vector<TimetableProgress> PopulateSeparationState(const Vehicle *v_start)
{
	std::vector<TimetableProgress> out;

	/* R3R (AUD-390-3): the separation group is "every vehicle which actually
	 * drives this order list", not "every vehicle on the shared order list ring".
	 * A vehicle which follows the order list of a control segment (borrowed
	 * orders) runs the same timetable but is not a ring member, so it used to be
	 * silently dropped from the ordering and the others computed their gap
	 * against a neighbour which was never in the array. */
	Vehicle *const start = Vehicle::Get(v_start->index);
	if (start == nullptr) return out;
	const OrderList *const list = start->GetEffectiveOrders();
	if (list == nullptr || list->GetNumOrders() == 0) return out;

	for (Vehicle *v : Vehicle::Iterate()) {
		if (!v->IsPrimaryVehicle()) continue;
		if (v->GetEffectiveOrders() != list) continue;
		if (!v->vehicle_flags.Test(VehicleFlag::SeparationActive)) continue;
		bool separation_valid = true;
		const int n = v->cur_real_order_index;
		int cumulative_ticks = 0;
		bool vehicle_ok = true;
		int order_count = n * 2;
		/* R3R (AUD-390-3): the stored progress may point past the end of the order
		 * list (see KI-555) or still be INVALID_VEH_ORDER_ID. Taking the order at
		 * that index yielded a null pointer which was dereferenced immediately
		 * below, so skip such a vehicle instead of crashing. */
		if (n < 0 || n >= (int)list->GetNumOrders()) continue;
		for (int i = 0; i < n; i++) {
			const Order *order = v->GetOrder(i);
			if (order == nullptr || order->IsType(OT_CONDITIONAL)) {
				vehicle_ok = false;
				break;
			}
			if (!IsOrderUsableForSeparation(order)) separation_valid = false;
			cumulative_ticks += order->GetTravelTime() + order->GetWaitTime();
		}
		if (!vehicle_ok) continue;

		const Order *order = v->GetOrder(n);
		if (order == nullptr || order->IsType(OT_CONDITIONAL)) continue;
		if (!IsOrderUsableForSeparation(order)) separation_valid = false;
		if (order->IsType(OT_GOTO_DEPOT) && (order->GetDepotOrderType().Test(OrderDepotTypeFlag::Service) || order->GetDepotActionType() & ODATFB_HALT)) {
			// Do not try to separate vehicles on depot service or halt orders
			separation_valid = false;
		}
		if (order->IsSlotCounterOrder() || order->IsType(OT_DUMMY) || order->IsType(OT_LABEL)) {
			// Do not try to separate vehicles on slot, change counter, or invalid orders
			separation_valid = false;
		}
		int order_ticks;
		if (order->GetType() == OT_GOTO_STATION && (v->current_order.IsType(OT_LOADING) || v->current_order.IsType(OT_LOADING_ADVANCE)) &&
				v->last_station_visited == order->GetDestination()) {
			order_count++;
			order_ticks = order->GetTravelTime() + v->current_loading_time;
			cumulative_ticks += order->GetTravelTime() + std::min(v->current_loading_time, order->GetWaitTime());
		} else {
			order_ticks = v->current_order_time;
			cumulative_ticks += std::min(v->current_order_time, order->GetTravelTime());
		}

		out.push_back({ v->index, order_count, order_ticks, separation_valid ? cumulative_ticks : -1 });
	}

	std::sort(out.begin(), out.end());

	return out;
}

void UpdateSeparationOrder(Vehicle *v_start)
{
	v_start->vehicle_flags.Set(VehicleFlag::SeparationActive);

	std::vector<TimetableProgress> progress_array = PopulateSeparationState(v_start);
	if (progress_array.size() < 2) return;

	const uint duration = v_start->GetEffectiveOrders()->GetTotalDuration();
	Vehicle *v = Vehicle::Get(progress_array.back().id);
	Vehicle *v_ahead = Vehicle::Get(progress_array.front().id);
	uint behind_index = (uint)progress_array.size() - 1;
	for (uint i = 0; i < progress_array.size(); i++) {
		const TimetableProgress &info_behind = progress_array[behind_index];
		behind_index = i;
		Vehicle *v_behind = v;

		const TimetableProgress &info = progress_array[i];
		v = v_ahead;

		uint ahead_index = (i + 1 == progress_array.size()) ? 0 : i + 1;
		const TimetableProgress &info_ahead = progress_array[ahead_index];
		v_ahead = Vehicle::Get(info_ahead.id);

		if (v->vehicle_flags.Test(VehicleFlag::TimetableStarted) &&
				v_ahead->vehicle_flags.Test(VehicleFlag::TimetableStarted) &&
				v_behind->vehicle_flags.Test(VehicleFlag::TimetableStarted)) {
			if (info_behind.IsValidForSeparation() && info.IsValidForSeparation() && info_ahead.IsValidForSeparation()) {
				/*
				 * The below is equivalent to:
				 * int separation_ahead = info_ahead.cumulative_ticks - info.cumulative_ticks;
				 * int separation_behind = info.cumulative_ticks - info_behind.cumulative_ticks;
				 * int separation_delta = separation_ahead - separation_behind;
				 */
				int separation_delta = info_ahead.cumulative_ticks + info_behind.cumulative_ticks - (2 * info.cumulative_ticks);

				if (i == 0) {
					separation_delta -= duration;
				} else if (ahead_index == 0) {
					separation_delta += duration;
				}

				Company *owner = Company::GetIfValid(v->owner);
				uint8_t timetable_separation_rate = owner ? owner->settings.auto_timetable_separation_rate : 100;
				int new_lateness = separation_delta / 2;
				v->lateness_counter = (new_lateness * timetable_separation_rate +
						v->lateness_counter * (100 - timetable_separation_rate)) / 100;
			}
		}
	}
}

/**
 * Get next scheduled dispatch time
 * @param ds Dispatch schedule.
 * @param leave_time Leave time.
 * @return Pair of:
 * * Dispatch time, or INVALID_STATE_TICKS
 * * Index of departure slot, or -1
 */
std::pair<StateTicks, int> GetScheduledDispatchTime(const DispatchSchedule &ds, StateTicks leave_time)
{
	const uint32_t dispatch_duration = ds.GetScheduledDispatchDuration();
	const int32_t max_delay          = ds.GetScheduledDispatchDelay();
	const StateTicks minimum         = leave_time - max_delay;
	StateTicks begin_time            = ds.GetScheduledDispatchStartTick();
	if (ds.GetScheduledDispatchReuseSlots()) {
		begin_time -= dispatch_duration;
	}

	int32_t last_dispatched_offset = ds.GetScheduledDispatchLastDispatch();

	if (minimum < begin_time) {
		const uint32_t duration_adjust = (uint32_t)CeilDivT<uint64_t>((begin_time - minimum).base(), dispatch_duration);
		begin_time -= dispatch_duration * duration_adjust;
		last_dispatched_offset += dispatch_duration * duration_adjust;
	}

	if (ds.GetScheduledDispatchLastDispatch() == INVALID_SCHEDULED_DISPATCH_OFFSET || ds.GetScheduledDispatchReuseSlots()) {
		last_dispatched_offset = -1;
	}

	StateTicks first_slot = INVALID_STATE_TICKS;
	int first_slot_index = -1;

	/* Find next available slots */
	int slot_idx = 0;
	for (const DispatchSlot &slot : ds.GetScheduledDispatch()) {
		int this_slot = slot_idx++;

		auto current_offset = slot.offset;
		if (current_offset >= dispatch_duration) continue;

		int32_t threshold = last_dispatched_offset;
		if (HasBit(slot.flags, DispatchSlot::SDSF_REUSE_SLOT)) threshold--;
		if ((int32_t)current_offset <= threshold) {
			current_offset += ((threshold + dispatch_duration - current_offset) / dispatch_duration) * dispatch_duration;
		}

		StateTicks current_departure = begin_time + current_offset;
		if (current_departure < minimum) {
			current_departure += ((minimum + dispatch_duration - current_departure - 1) / dispatch_duration) * dispatch_duration;
		}

		if (first_slot == INVALID_STATE_TICKS || first_slot > current_departure) {
			first_slot = current_departure;
			first_slot_index = this_slot;
		}
	}

	return std::make_pair(first_slot, first_slot_index);
}

LastDispatchRecord MakeLastDispatchRecord(const DispatchSchedule &ds, StateTicks slot, int slot_index)
{
	uint8_t record_flags = 0;
	if (slot_index == 0) SetBit(record_flags, LastDispatchRecord::RF_FIRST_SLOT);
	if (slot_index == (int)(ds.GetScheduledDispatch().size() - 1)) SetBit(record_flags, LastDispatchRecord::RF_LAST_SLOT);
	const DispatchSlot &dispatch_slot = ds.GetScheduledDispatch()[slot_index];
	return {
		slot,
		dispatch_slot.offset,
		dispatch_slot.flags,
		dispatch_slot.route_id,
		record_flags,
	};
}

/**
 * Update the timetable for the vehicle.
 * @param v The vehicle to update the timetable for.
 * @param travelling Whether we just travelled or waited at a station.
 */
void UpdateVehicleTimetable(Vehicle *v, bool travelling)
{
	if (!travelling) v->current_loading_time++; // +1 because this time is one tick behind
	uint time_taken = v->current_order_time;
	uint time_loading = v->current_loading_time;

	v->current_order_time = 0;
	v->current_loading_time = 0;

	if (v->current_order.IsType(OT_IMPLICIT)) return; // no timetabling of auto orders

	if (v->cur_real_order_index >= v->GetNumOrders()) return;
	Order *real_current_order = v->GetOrder(v->cur_real_order_index);
	Order *real_timetable_order = v->cur_timetable_order_index != INVALID_VEH_ORDER_ID ? v->GetOrder(v->cur_timetable_order_index) : nullptr;

	auto guard = scope_guard([v, travelling]() {
		/* On next call, when updating waiting time, use current order even if travel field of current order isn't being updated */
		if (travelling) v->cur_timetable_order_index = v->cur_real_order_index;
	});

	VehicleOrderID first_manual_order = 0;
	for (Order *o : v->Orders()) {
		if (!o->HasNoTimetableTimes() && !o->IsType(OT_IMPLICIT)) break;
		++first_manual_order;
	}

	bool just_started = false;
	bool set_scheduled_dispatch = false;

	/* Start scheduled dispatch at first opportunity */
	if (v->vehicle_flags.Test(VehicleFlag::ScheduledDispatch) && v->cur_implicit_order_index != INVALID_VEH_ORDER_ID) {
		Order *real_implicit_order = v->GetOrder(v->cur_implicit_order_index);
		if (real_implicit_order->IsScheduledDispatchOrder(true) && travelling) {
			DispatchSchedule &ds = v->GetEffectiveOrders()->GetDispatchScheduleByIndex(real_implicit_order->GetDispatchScheduleIndex());

			/* Update scheduled information */
			ds.UpdateScheduledDispatch(v);

			const int wait_offset = real_current_order->GetTimetabledWait();

			StateTicks slot;
			int slot_index;
			std::tie(slot, slot_index) = GetScheduledDispatchTime(ds, _state_ticks + wait_offset);

			if (slot != INVALID_STATE_TICKS) {
				just_started = !v->vehicle_flags.Test(VehicleFlag::TimetableStarted);
				v->vehicle_flags.Set(VehicleFlag::TimetableStarted);
				v->lateness_counter = (_state_ticks - slot + wait_offset).AsTicks();
				ds.SetScheduledDispatchLastDispatch((slot - ds.GetScheduledDispatchStartTick()).AsTicks());
				SetTimetableWindowsDirty(v, STWDF_SCHEDULED_DISPATCH);
				set_scheduled_dispatch = true;
				v->dispatch_records[static_cast<uint16_t>(real_implicit_order->GetDispatchScheduleIndex())] = MakeLastDispatchRecord(ds, slot, slot_index);
				if (_settings_client.gui.show_vehicle_route_id_vehicle_view) {
					SetWindowWidgetDirty(WindowClass::VehicleView, v->index, WID_VV_CAPTION);
				}
			}
		}
	}

	/* Start automated timetables at first opportunity */
	if (!v->vehicle_flags.Test(VehicleFlag::TimetableStarted) && v->vehicle_flags.Test(VehicleFlag::AutomateTimetable)) {
		v->vehicle_flags.Reset(VehicleFlag::SeparationActive);
		v->vehicle_flags.Set(VehicleFlag::TimetableStarted);
		/* If the lateness is set by scheduled dispatch above, do not reset */
		if (!v->vehicle_flags.Test(VehicleFlag::ScheduledDispatch)) v->lateness_counter = 0;
		if (v->vehicle_flags.Test(VehicleFlag::TimetableSeparation)) UpdateSeparationOrder(v);
		SetTimetableWindowsDirty(v);
		return;
	}

	/* This vehicle is arriving at the first destination in the timetable. */
	if (v->cur_real_order_index == first_manual_order && travelling) {
		/* If the start date hasn't been set, or it was set automatically when
		 * the vehicle last arrived at the first destination, update it to the
		 * current time. Otherwise set the late counter appropriately to when
		 * the vehicle should have arrived. */
		if (!set_scheduled_dispatch) just_started = !v->vehicle_flags.Test(VehicleFlag::TimetableStarted);

		if (v->timetable_start != 0) {
			v->lateness_counter = (_state_ticks - v->timetable_start).AsTicks();
			v->timetable_start = StateTicks{0};
		}

		v->vehicle_flags.Set(VehicleFlag::TimetableStarted);
		SetWindowDirty(WindowClass::VehicleTimetable, v->index);
	}

	if (!v->vehicle_flags.Test(VehicleFlag::TimetableStarted)) return;
	if (real_timetable_order == nullptr) return;

	bool autofilling = v->vehicle_flags.Test(VehicleFlag::AutofillTimetable);
	bool is_conditional = real_timetable_order->IsType(OT_CONDITIONAL);
	bool remeasure_wait_time = !is_conditional && (!real_timetable_order->IsWaitTimetabled() ||
			(autofilling && !v->vehicle_flags.Test(VehicleFlag::AutofillPreserveWaitTime)));

	if (travelling && remeasure_wait_time) {
		/* We just finished travelling and want to remeasure the loading time,
		 * so do not apply any restrictions for the loading to finish. */
		v->current_order.SetWaitTime(0);
	}

	bool travel_field = travelling;
	if (is_conditional) {
		if (travelling) {
			/* conditional orders use the wait field for the jump-taken travel time */
			travel_field = false;
		} else {
			/* doesn't make sense to update wait time for conditional orders */
			return;
		}
	} else {
		/*
		 * R3R: upstream asserts that the timetable index points at the same order as
		 * the real index. Several R3R paths change order indices behind each other's
		 * back (borrowing a segment's schedule on couple, pinning the index on
		 * OT_GOTO_COUPLE, re-pointing schedules in depots), and an index that was
		 * pinned while a GOTO_COUPLE order was being executed can survive a save/load
		 * and end up one order behind afterwards. The assert then takes the game down
		 * in Debug builds ("real_timetable_order == real_current_order").
		 *
		 * Re-sync the timetable index to the real one instead -- the same thing the
		 * scope guard above does on a travelling update -- and log the event so the
		 * offending path can be found in R3R_debug.log via the TT-DESYNC lines.
		 */
		if (real_timetable_order != real_current_order) {
			FILE *dbg = R3RFopenDbg("a");
			if (dbg != nullptr) {
				fprintf(dbg, "TT-DESYNC veh=%d trav=%d real=%d tt=%d impl=%d n=%d curType=%d realType=%d ttType=%d types=",
						(int)v->index.base(), travelling ? 1 : 0, (int)v->cur_real_order_index, (int)v->cur_timetable_order_index,
						(int)v->cur_implicit_order_index, (int)v->GetNumOrders(), (int)v->current_order.GetType(),
						(int)(real_current_order != nullptr ? real_current_order->GetType() : 255),
						(int)(real_timetable_order != nullptr ? real_timetable_order->GetType() : 255));
				VehicleOrderID i = 0;
				for (const Order *o : v->Orders()) {
					fprintf(dbg, "%d%s", (int)o->GetType(), (++i < v->GetNumOrders()) ? "," : "");
				}
				fprintf(dbg, "\n");
				fclose(dbg);
			}
			v->cur_timetable_order_index = v->cur_real_order_index;
			real_timetable_order = real_current_order;
		}
	}

	if (just_started) return;

	/* Before modifying waiting times, check whether we want to preserve bigger ones. */
	if ((travelling || time_taken > real_timetable_order->GetWaitTime() || remeasure_wait_time)) {
		/* Round the time taken up to the nearest timetable rounding factor
		 * (default: day), as this will avoid confusion for people who are
		 * timetabling in days, and can be adjusted later by people who aren't.
		 * For trains/aircraft multiple movement cycles are done in one
		 * tick. This makes it possible to leave the station and process
		 * e.g. a depot order in the same tick, causing it to not fill
		 * the timetable entry like is done for road vehicles/ships.
		 * Thus always make sure at least one tick is used between the
		 * processing of different orders when filling the timetable. */
		Company *owner = Company::GetIfValid(v->owner);
		uint rounding_factor = owner != nullptr ? owner->settings.timetable_autofill_rounding : 0;
		if (rounding_factor == 0) {
			if (_settings_game.game_time.time_in_minutes) {
				rounding_factor = _settings_game.game_time.ticks_per_minute;
			} else if (EconTime::UsingWallclockUnits()) {
				rounding_factor = TICKS_PER_SECOND;
			} else {
				rounding_factor = DAY_TICKS;
			}
		}
		uint time_to_set = CeilDiv(std::max(time_taken, 1U), rounding_factor) * rounding_factor;

		/* R3R (KI-391): #OT_DECOUPLE has no timetable times at all and #OT_WAIT_COUPLE
		 * has no travel time. Do not measure (and hence never autofill) a field that
		 * does not apply to the order; ChangeTimetable() clamps it to zero anyway. */
		if (travel_field && real_timetable_order->HasTravelTimetableTime() &&
				(autofilling || !real_timetable_order->IsTravelTimetabled())) {
			ChangeTimetable(v, v->cur_timetable_order_index, time_to_set, MTF_TRAVEL_TIME, autofilling);
		} else if (!travel_field && real_timetable_order->HasWaitTimetableTime() &&
				(autofilling || !real_timetable_order->IsWaitTimetabled())) {
			ChangeTimetable(v, v->cur_timetable_order_index, time_to_set, MTF_WAIT_TIME, autofilling);
		}
	}

	if (v->cur_real_order_index == first_manual_order && travelling) {
		/* If we just started we would have returned earlier and have not reached
		 * this code. So obviously, we have completed our round: So turn autofill
		 * off again. */
		v->vehicle_flags.Reset(VehicleFlag::AutofillTimetable);
		v->vehicle_flags.Reset(VehicleFlag::AutofillPreserveWaitTime);
	} else if (autofilling) {
		if (!set_scheduled_dispatch) v->lateness_counter = 0;
		return;
	}

	uint timetabled = travel_field ? real_timetable_order->GetTimetabledTravel() :
			real_timetable_order->GetTimetabledWait();

	/* Update the timetable to gradually shift order times towards the actual travel times. */
	if (timetabled != 0 && v->vehicle_flags.Test(VehicleFlag::AutomateTimetable)) {
		int32_t new_time;
		if (travelling) {
			new_time = time_taken;
			if (new_time > (int32_t)timetabled * 4 && new_time > (int32_t)timetabled + 3000 && !(real_timetable_order->IsType(OT_GOTO_DEPOT) && real_timetable_order->GetDepotOrderType().Test(OrderDepotTypeFlag::Service))) {
				/* Possible jam, clear time and restart timetable for all vehicles.
				 * Otherwise we risk trains blocking 1-lane stations for long times. */
				ChangeTimetable(v, v->cur_timetable_order_index, 0, travel_field ? MTF_TRAVEL_TIME : MTF_WAIT_TIME, false);
				if (!v->vehicle_flags.Test(VehicleFlag::ScheduledDispatch)) {
					/* R3R (AUD-390-4): restart the timetable for every vehicle which
					 * actually drives it, followers of a control segment included. */
					R3RForEachTimetableVehicle(v, [&](Vehicle *v2) {
						/* Clear VehicleFlag::TimetableStarted but do not clear VehicleFlag::SeparationActive */
						v2->vehicle_flags.Reset(VehicleFlag::TimetableStarted);
						v2->lateness_counter = 0;
					});
				}
				SetTimetableWindowsDirty(v);
				return;
			} else if (new_time >= (int32_t)timetabled / 2) {
				/* Compute running average, with sign conversion to avoid negative overflow.
				 * This is biased to favour negative adjustments */
				if (new_time < (int32_t)timetabled) {
					new_time = ((int32_t)timetabled * 3 + new_time * 2 + 2) / 5;
				} else {
					new_time = ((int32_t)timetabled * 9 + new_time + 5) / 10;
				}
			} else {
				/* new time is less than half the old time, set value directly */
			}
		} else {
			new_time = time_loading;
			/* Compute running average, with sign conversion to avoid negative overflow.
			 * This is biased to favour positive adjustments */
			if (new_time > (int32_t)timetabled) {
				new_time = ((int32_t)timetabled * 3 + new_time * 2 + 2) / 5;
			} else {
				new_time = ((int32_t)timetabled * 9 + new_time + 5) / 10;
			}
		}

		if (new_time < 1) new_time = 1;
		if (new_time != (int32_t)timetabled) {
			ChangeTimetable(v, v->cur_timetable_order_index, new_time, travel_field ? MTF_TRAVEL_TIME : MTF_WAIT_TIME, true);
			timetabled = travel_field ? real_timetable_order->GetTimetabledTravel() : real_timetable_order->GetTimetabledWait();
		}
	} else if (timetabled == 0 && v->vehicle_flags.Test(VehicleFlag::AutomateTimetable)) {
		/* Add times for orders that are not yet timetabled, even while not autofilling */
		const int32_t new_time = travelling ? time_taken : time_loading;
		if (travel_field) {
			ChangeTimetable(v, v->cur_timetable_order_index, new_time, MTF_TRAVEL_TIME, true);
			timetabled = real_timetable_order->GetTimetabledTravel();
		} else {
			ChangeTimetable(v, v->cur_timetable_order_index, new_time, MTF_WAIT_TIME, true);
			timetabled = real_timetable_order->GetTimetabledWait();
		}
	}

	bool is_timetabled = travel_field ? real_timetable_order->IsTravelTimetabled() :
			real_timetable_order->IsWaitTimetabled();

	/* Vehicles will wait at stations if they arrive early even if they are not
	 * timetabled to wait there, so make sure the lateness counter is updated
	 * when this happens. */
	if (timetabled == 0 && !is_timetabled && (travelling || v->lateness_counter >= 0)) return;

	if (set_scheduled_dispatch) {
		// do nothing
	} else if (v->vehicle_flags.Test(VehicleFlag::TimetableSeparation) && v->vehicle_flags.Test(VehicleFlag::TimetableStarted)) {
		v->current_order_time = time_taken;
		v->current_loading_time = time_loading;
		UpdateSeparationOrder(v);
		v->current_order_time = 0;
		v->current_loading_time = 0;
	} else {
		v->lateness_counter -= (timetabled - time_taken);
	}

	/* When we are more late than this timetabled bit takes we (somewhat expensively)
	 * check how many ticks the (fully filled) timetable has. If a timetable cycle is
	 * shorter than the amount of ticks we are late we reduce the lateness by the
	 * length of a full cycle till lateness is less than the length of a timetable
	 * cycle. When the timetable isn't fully filled the cycle will be INVALID_TICKS. */
	if (v->lateness_counter > (int)timetabled) {
		Ticks cycle = v->GetEffectiveOrders()->GetTimetableTotalDuration();
		if (cycle != INVALID_TICKS && v->lateness_counter > cycle) {
			if (cycle == 0) {
				v->lateness_counter = 0;
			} else {
				v->lateness_counter %= cycle;
			}
		}
	}

	SetTimetableWindowsDirty(v);
}
