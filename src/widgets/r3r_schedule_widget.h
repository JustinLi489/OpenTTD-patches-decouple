/*
 * This file is part of OpenTTD.
 * OpenTTD is free software; you can redistribute it and/or modify it under the terms of the GNU General Public License as published by the Free Software Foundation, version 2.
 * OpenTTD is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 * See the GNU General Public License for more details. You should have received a copy of the GNU General Public License along with OpenTTD. If not, see <https://www.gnu.org/licenses/old-licenses/gpl-2.0>.
 */

/** @file r3r_schedule_widget.h Widgets of the R3R schedule pool window (pure ASCII on purpose, see KI-584). */

#ifndef WIDGETS_R3R_SCHEDULE_WIDGET_H
#define WIDGETS_R3R_SCHEDULE_WIDGET_H

/** Widgets of the #R3RSchedulePoolWindow class. */
enum R3RScheduleWidgets : WidgetID {
	WID_SCHED_CAPTION,        ///< Caption of the window.
	WID_SCHED_LIST,           ///< List of every schedule in this savegame.
	WID_SCHED_SCROLLBAR,      ///< Scrollbar of the schedule list.
	WID_SCHED_NEW,            ///< Create an empty loose (ownerless) schedule right away.
	WID_SCHED_RENAME,         ///< Rename the selected schedule.
	WID_SCHED_EDIT,           ///< Open the order window of the selected schedule.
	WID_SCHED_PREVIEW,        ///< List the trains which point at the selected schedule.
	WID_SCHED_PUBLIC,         ///< Publish the selected schedule (other companies may use it).
	WID_SCHED_DELETE,         ///< Delete the selected loose schedule from the pool.
	WID_SCHED_ASSIGN,         ///< Open the picker which points one of our trains at the selected schedule.
	WID_SCHED_HINT,           ///< Permanent explanation of the assignment semantics.
};

/** Widgets of the #R3RScheduleAssignWindow class (the train picker of "Assign"). */
enum R3RScheduleAssignWidgets : WidgetID {
	WID_SCHED_AS_CAPTION,     ///< Caption of the window.
	WID_SCHED_AS_LIST,        ///< List of our trains with their images.
	WID_SCHED_AS_SCROLLBAR,   ///< Scrollbar of the train list.
};

#endif /* WIDGETS_R3R_SCHEDULE_WIDGET_H */
