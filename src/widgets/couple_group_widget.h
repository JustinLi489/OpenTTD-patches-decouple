/*
 * This file is part of OpenTTD.
 * OpenTTD is free software; you can redistribute it and/or modify it under the terms of the GNU General Public License as published by the Free Software Foundation, version 2.
 * OpenTTD is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 * See the GNU General Public License for more details. You should have received a copy of the GNU General Public License along with OpenTTD. If not, see <https://www.gnu.org/licenses/old-licenses/gpl-2.0>.
 */

/** @file couple_group_widget.h Types related to the couple group widgets. */

#ifndef WIDGETS_COUPLE_GROUP_WIDGET_H
#define WIDGETS_COUPLE_GROUP_WIDGET_H

/** Widgets of the #CoupleGroupWindow class. */
enum CoupleGroupWidgets : WidgetID {
	WID_CG_CAPTION,             ///< Caption of the window.
	WID_CG_GROUPS,              ///< List of couple groups.
	WID_CG_GROUPS_SCROLLBAR,    ///< Scrollbar of the couple group list.
	WID_CG_SEGMENTS,            ///< Segments which belong to the selected couple group.
	WID_CG_SEGMENTS_SCROLLBAR,  ///< Scrollbar of the segment list.
	WID_CG_NEW,                 ///< Create a new couple group.
	WID_CG_RENAME,              ///< Rename the selected couple group.
	WID_CG_DELETE,              ///< Delete the selected couple group.
	WID_CG_REMOVE_SEGMENT,      ///< Take the selected segment out of the selected couple group.
	WID_CG_ADD_SEGMENT,         ///< Add a segment to the selected couple group by picking it in a depot.
	WID_CG_CROSS_COMPANY_TEXT,  ///< R3R (D6-1+D6-3): label of the one opt-in over the company boundary.
	WID_CG_CROSS_COMPANY,       ///< R3R (D6-1+D6-3): open the selected group for other companies (join + cross-company coupling).
};

#endif /* WIDGETS_COUPLE_GROUP_WIDGET_H */
