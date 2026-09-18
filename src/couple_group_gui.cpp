/*
 * This file is part of OpenTTD.
 * OpenTTD is free software; you can redistribute it and/or modify it under the terms of the GNU General Public License as published by the Free Software Foundation, version 2.
 * OpenTTD is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 * See the GNU General Public License for more details. You should have received a copy of the GNU General Public License along with OpenTTD. If not, see <https://www.gnu.org/licenses/old-licenses/gpl-2.0>.
 */

/** @file couple_group_gui.cpp R3R: the window to manage couple groups. */

#include "stdafx.h"
#include "couple_group_gui.h"

#include "command_func.h"
#include "company_base.h"
#include "company_func.h"
#include "couple_group.h"
#include "couple_group_cmd.h"
#include "gfx_func.h"
#include "querystring_gui.h"
#include "strings_func.h"
#include "textbuf_gui.h"
#include "tilehighlight_func.h"
#include "tilehighlight_type.h"
#include "train.h"
#include "vehicle_base.h"
#include "window_func.h"
#include "window_gui.h"
#include "core/geometry_func.hpp"
#include "r3r_perf.h"
#include "widgets/couple_group_widget.h"
#include "table/strings.h"

#include "safeguards.h"

/** Layout of the couple group window. */
static constexpr NWidgetPart _nested_couple_group_widgets[] = {
	NWidget(NWID_HORIZONTAL),
		NWidget(WWT_CLOSEBOX, Colours::Grey),
		NWidget(WWT_CAPTION, Colours::Grey, WID_CG_CAPTION), SetStringTip(STR_COUPLE_GROUP_CAPTION, STR_TOOLTIP_WINDOW_TITLE_DRAG_THIS),
		NWidget(WWT_SHADEBOX, Colours::Grey),
		NWidget(WWT_DEFSIZEBOX, Colours::Grey),
		NWidget(WWT_STICKYBOX, Colours::Grey),
	EndContainer(),

	NWidget(NWID_HORIZONTAL),
		NWidget(WWT_PANEL, Colours::Grey, WID_CG_GROUPS), SetMinimalSize(170, 130), SetResize(1, 10), SetToolTip(STR_COUPLE_GROUP_GROUPS_TOOLTIP), SetScrollbar(WID_CG_GROUPS_SCROLLBAR), EndContainer(),
		NWidget(NWID_VSCROLLBAR, Colours::Grey, WID_CG_GROUPS_SCROLLBAR),
		NWidget(WWT_PANEL, Colours::Grey, WID_CG_SEGMENTS), SetMinimalSize(250, 130), SetResize(2, 10), SetToolTip(STR_COUPLE_GROUP_SEGMENTS_TOOLTIP), SetScrollbar(WID_CG_SEGMENTS_SCROLLBAR), EndContainer(),
		NWidget(NWID_VSCROLLBAR, Colours::Grey, WID_CG_SEGMENTS_SCROLLBAR),
	EndContainer(),

	NWidget(NWID_HORIZONTAL),
		NWidget(WWT_PANEL, Colours::Grey),
			NWidget(NWID_VERTICAL),
				NWidget(NWID_HORIZONTAL),
					NWidget(WWT_TEXT, Colours::Invalid, WID_CG_CROSS_COMPANY_TEXT), SetFill(1, 0), SetResize(1, 0), SetStringTip(STR_COUPLE_GROUP_CROSS_COMPANY, STR_COUPLE_GROUP_CROSS_COMPANY_TOOLTIP),
					NWidget(WWT_BOOLBTN, Colours::Grey, WID_CG_CROSS_COMPANY), SetToolTip(STR_COUPLE_GROUP_CROSS_COMPANY_TOOLTIP),
				EndContainer(),
				NWidget(NWID_HORIZONTAL),
					NWidget(NWID_HORIZONTAL, NWidContainerFlag::EqualSize),
						NWidget(WWT_PUSHTXTBTN, Colours::Grey, WID_CG_NEW), SetResize(1, 0), SetFill(1, 0), SetStringTip(STR_COUPLE_GROUP_NEW, STR_COUPLE_GROUP_NEW_TOOLTIP),
						NWidget(WWT_PUSHTXTBTN, Colours::Grey, WID_CG_RENAME), SetResize(1, 0), SetFill(1, 0), SetStringTip(STR_BUTTON_RENAME, STR_COUPLE_GROUP_RENAME_TOOLTIP),
						NWidget(WWT_PUSHTXTBTN, Colours::Grey, WID_CG_DELETE), SetResize(1, 0), SetFill(1, 0), SetStringTip(STR_COUPLE_GROUP_DELETE, STR_COUPLE_GROUP_DELETE_TOOLTIP),
						NWidget(WWT_PUSHTXTBTN, Colours::Grey, WID_CG_REMOVE_SEGMENT), SetResize(1, 0), SetFill(1, 0), SetStringTip(STR_COUPLE_GROUP_REMOVE_SEGMENT, STR_COUPLE_GROUP_REMOVE_SEGMENT_TOOLTIP),
						NWidget(WWT_PUSHTXTBTN, Colours::Grey, WID_CG_ADD_SEGMENT), SetResize(1, 0), SetFill(1, 0), SetStringTip(STR_COUPLE_GROUP_ADD_SEGMENT, STR_COUPLE_GROUP_ADD_SEGMENT_TOOLTIP),
					EndContainer(),
					NWidget(WWT_RESIZEBOX, Colours::Grey),
				EndContainer(),
			EndContainer(),
		EndContainer(),
	EndContainer(),
};

static WindowDesc _couple_group_desc(__FILE__, __LINE__,
	WindowPosition::Automatic, "couple_group", 440, 226,
	WindowClass::CoupleGroup, WindowClass::None,
	WindowDefaultFlag::Construction,
	_nested_couple_group_widgets
);

/**
 * R3R: Window listing the couple groups and, for the selected group, the segments which belong to it.
 */
struct CoupleGroupWindow : Window {
private:
	Scrollbar *group_scroll = nullptr;   ///< Scrollbar of the couple group list.
	Scrollbar *segment_scroll = nullptr; ///< Scrollbar of the segment list.

	std::vector<CoupleGroupID> groups{};  ///< Groups shown in the left pane, ascending by ID.
	std::vector<uint> group_segments{};   ///< Number of segments per entry of #groups.
	std::vector<VehicleID> segments{};    ///< Segment heads of the selected group (right pane).
	std::vector<uint> segment_cars{};     ///< Number of vehicles per entry of #segments.

	int selected_group = -1;   ///< Selected index into #groups, or -1 if there is no selection.
	int selected_segment = -1; ///< Selected index into #segments, or -1 if there is no selection.

	bool query_is_rename = false; ///< Whether the pending query string renames instead of creates.
	bool pending_select_newest = false; ///< Select the newest group on the next rebuild (set after a "new group" query).

	/**
	 * R3R: the company whose couple groups this window manages.
	 *
	 * Kept in a separate member on purpose: Window::InitializeData() (called
	 * from FinishInitNested(), i.e. from the constructor below) resets
	 * Window::owner to INVALID_OWNER, so the company may only be written to
	 * this->owner once OnInit() runs.  Setting it in the constructor body would
	 * be silently undone again.
	 */
	Owner company = INVALID_OWNER;

	/**
	 * Number of vehicles of the segment which starts at \a head,
	 * i.e. everything up to (but excluding) the next segment head.
	 */
	static uint CountSegmentVehicles(const Train *head)
	{
		uint count = 1;
		for (const Train *v = head->Next(); v != nullptr; v = v->Next()) {
			if (v->IsSegmentFront()) break;
			count++;
		}
		return count;
	}

	/** Rebuild the right pane for the currently selected couple group. */
	void RebuildSegments()
	{
		const VehicleID old = this->GetSelectedSegment();
		const CoupleGroupID group = this->GetSelectedGroup();

		this->segments.clear();
		this->segment_cars.clear();
		if (group != INVALID_COUPLE_GROUP) {
			const CoupleGroupMask bit = R3RCoupleGroupBit(group);
			for (const Train *t : Train::Iterate()) {
				/* Only the head of a chain or an explicit segment head carries a couple group. */
				if (t->Previous() != nullptr && !t->IsSegmentFront()) continue;
				/* Q5: a segment may be a member of several groups; it is listed here
				 * as soon as this group is one of them. */
				if ((R3RGetCoupleGroupsOfSegment(t) & bit) == COUPLE_GROUP_MASK_NONE) continue;
				this->segments.push_back(t->index);
				this->segment_cars.push_back(CountSegmentVehicles(t));
			}
		}
		this->segment_scroll->SetCount(this->segments.size());

		this->selected_segment = -1;
		if (old != VehicleID::Invalid()) {
			auto it = std::find(this->segments.begin(), this->segments.end(), old);
			if (it != this->segments.end()) this->selected_segment = static_cast<int>(std::distance(this->segments.begin(), it));
		}
		if (this->selected_segment >= 0) this->segment_scroll->ScrollTowards(this->selected_segment);
	}

public:
	CoupleGroupWindow(WindowDesc &desc, Owner company) : Window(desc)
	{
		/* R3R: a depot tile can be owned by something which is not a company
		 * (public/neutral tile), and the depot window passes its own owner
		 * through.  The couple group window always manages the groups of a real
		 * company, so fall back to the local company in that case -- otherwise
		 * every button would be disabled and no group would be listed. */
		const Owner arg_company = company;
		if (!Company::IsValidID(company)) company = _local_company;
		this->company = company;

		R3RDbgWrite("[R3R] CG-WIN open arg=%u arg_valid=%d company=%u company_valid=%d local=%u\n",
				(unsigned)arg_company.base(), (int)Company::IsValidID(arg_company),
				(unsigned)company.base(), (int)Company::IsValidID(company),
				(unsigned)_local_company.base());

		this->CreateNestedTree();
		this->group_scroll = this->GetScrollbar(WID_CG_GROUPS_SCROLLBAR);
		this->segment_scroll = this->GetScrollbar(WID_CG_SEGMENTS_SCROLLBAR);
		this->FinishInitNested(0);
	}

	void OnInit() override
	{
		/* R3R (KI-55): FinishInitNested() -> Window::InitializeData() resets
		 * Window::owner to INVALID_OWNER *after* the constructor body has run, so
		 * the company has to be re-applied here.  With Window::owner left at
		 * INVALID_OWNER the window lists no groups at all (the visibility filter
		 * rejects every group), treats the selected group as unmanageable (grey
		 * buttons) and cannot create anything which would show up afterwards. */
		this->owner = this->company;
		this->RebuildGroups();

		R3RDbgWrite("[R3R] CG-INIT owner=%u owner_valid=%d company=%u company_valid=%d groups=%u\n",
				(unsigned)this->owner.base(), (int)Company::IsValidID(this->owner),
				(unsigned)this->company.base(), (int)Company::IsValidID(this->company),
				(unsigned)this->groups.size());
	}

	/** Rebuild the left pane from the couple group pool, keeping the selection if possible. */
	void RebuildGroups()
	{
		const CoupleGroupID old = this->GetSelectedGroup();

		this->groups.clear();
		this->group_segments.clear();
		uint iterated = 0;
		uint visible = 0;
		for (const CoupleGroup *cg : CoupleGroup::Iterate()) {
			iterated++;
			/* R3R (KI-58): filter by this->company, never by this->owner.
			 * Window::owner belongs to the engine and is reset to INVALID_OWNER
			 * while the window is (re-)initialised; since every group is owned by
			 * company 0, such a frame made the *whole* list invisible: groups
			 * flickered between "1" and "0", can_manage followed it and the
			 * management buttons were disabled on exactly the frames the user
			 * clicked them. this->company is our own member, always holds a real
			 * company and is never touched by the engine. */
			if (!R3RCoupleGroupIsVisibleTo(cg, this->company)) continue;
			visible++;
			this->groups.push_back(cg->index);
			this->group_segments.push_back(R3RCountSegmentsInCoupleGroup(cg->index));
		}
		std::sort(this->groups.begin(), this->groups.end());
		this->group_scroll->SetCount(this->groups.size());

		this->selected_group = -1;
		if (old != INVALID_COUPLE_GROUP) {
			auto it = std::find(this->groups.begin(), this->groups.end(), old);
			if (it != this->groups.end()) this->selected_group = static_cast<int>(std::distance(this->groups.begin(), it));
		}
		/* A freshly created group lands at the end of the list (it is sorted by id)
		 * and is selected right away: the user almost always wants to fill it next. */
		if (this->pending_select_newest) {
			this->pending_select_newest = false;
			if (!this->groups.empty()) this->selected_group = static_cast<int>(this->groups.size()) - 1;
		}
		if (this->selected_group < 0 && !this->groups.empty()) this->selected_group = 0;
		if (this->selected_group >= 0) this->group_scroll->ScrollTowards(this->selected_group);

		/* R3R: edge-triggered probe for KI-58 -- records every distinct
		 * (owner, company, iterated, visible) combination, so a flickering list can
		 * be told apart from a genuine filtering result. */
		const uint rbg_state = (uint)this->owner.base() | ((uint)this->company.base() << 8) | (iterated << 16) | (visible << 24);
		static uint last_rbg_state = 0xFFFFFFFFu;
		if (rbg_state != last_rbg_state) {
			last_rbg_state = rbg_state;
			R3RDbgWrite("[R3R] CG-RBG owner=%u company=%u iterated=%u visible=%u sel=%d\n",
					(unsigned)this->owner.base(), (unsigned)this->company.base(), iterated, visible, this->selected_group);
		}

		this->RebuildSegments();
		this->SetDirty();
	}

	CoupleGroupID GetSelectedGroup() const
	{
		if (this->selected_group < 0 || this->selected_group >= static_cast<int>(this->groups.size())) return INVALID_COUPLE_GROUP;
		return this->groups[this->selected_group];
	}

	VehicleID GetSelectedSegment() const
	{
		if (this->selected_segment < 0 || this->selected_segment >= static_cast<int>(this->segments.size())) return VehicleID::Invalid();
		return this->segments[this->selected_segment];
	}

	/** Confirmation callback for deleting the selected couple group. */
	static void DeleteCoupleGroupCallback(Window *w, bool confirmed);

	/** Can the selected couple group be renamed, deleted or edited by this window? */
	bool IsSelectedGroupManageable() const
	{
		/* R3R (KI-58): like RebuildGroups() this must never use Window::owner --
		 * the engine resets that field, which made the buttons flicker between
		 * enabled and disabled. GetIfValid() tolerates a stale/absent selection, so
		 * the buttons stay disabled instead of misbehaving when nothing is selected. */
		return R3RCoupleGroupIsManageable(CoupleGroup::GetIfValid(this->GetSelectedGroup()), this->company);
	}

	/**
	 * R3R (D6-③): may this company add its own segments to the selected group
	 * (or take them out again)?
	 *
	 * This is the weaker, membership-only right: our own group, or a group which
	 * another company opened for us. It never grants rights over a vehicle of
	 * somebody else -- the command still checks the ownership of the vehicle --
	 * so a shared group may be filled by every company, but only with its own
	 * rolling stock. Renaming, deleting and the opt-in over the company
	 * boundary stay owner-only (IsSelectedGroupManageable()).
	 */
	bool IsSelectedGroupJoinable() const
	{
		return R3RCoupleGroupIsJoinableBy(CoupleGroup::GetIfValid(this->GetSelectedGroup()), this->company);
	}

	void UpdateWidgetSize(WidgetID widget, Dimension &size, const Dimension &padding, Dimension &fill, Dimension &resize) override
	{
		switch (widget) {
			case WID_CG_GROUPS:
			case WID_CG_SEGMENTS:
				resize.height = GetCharacterHeight(FontSize::Normal) + 2;
				size.height = resize.height * 6 + WidgetDimensions::scaled.framerect.Vertical();
				break;

			case WID_CG_NEW:
				size = adddim(GetStringBoundingBox(STR_COUPLE_GROUP_NEW), padding);
				break;

			case WID_CG_RENAME:
				size = adddim(GetStringBoundingBox(STR_BUTTON_RENAME), padding);
				break;

			case WID_CG_DELETE:
				size = adddim(GetStringBoundingBox(STR_COUPLE_GROUP_DELETE), padding);
				break;

			case WID_CG_REMOVE_SEGMENT:
				size = adddim(GetStringBoundingBox(STR_COUPLE_GROUP_REMOVE_SEGMENT), padding);
				break;

			case WID_CG_ADD_SEGMENT:
				size = adddim(GetStringBoundingBox(STR_COUPLE_GROUP_ADD_SEGMENT), padding);
				break;

			default:
				break;
		}
	}

	void DrawWidget(const Rect &r, WidgetID widget) const override
	{
		if (widget == WID_CG_GROUPS) {
			const Rect ir = r.Shrink(WidgetDimensions::scaled.framerect);
			if (this->groups.empty()) {
				DrawString(ir.left, ir.right, ir.top, STR_COUPLE_GROUP_NONE);
				return;
			}
			int y = ir.top;
			for (int i = this->group_scroll->GetPosition(); i < static_cast<int>(this->groups.size()) && this->group_scroll->IsVisible(i); i++, y += this->resize.step_height) {
				if (i == this->selected_group) GfxFillRect(r.left + 1, y, r.right, y + this->resize.step_height - 1, PC_DARK_GREY);
				if (!CoupleGroup::IsValidID(this->groups[i])) continue;
				const CoupleGroup *cg = CoupleGroup::Get(this->groups[i]);
				if (cg == nullptr) continue;
				std::string text = GetString(STR_COUPLE_GROUP_LIST_ITEM, cg->name, this->group_segments[i]);
				/* R3R (D6-③): a group of another company only shows up here
				 * because its owner opened it for us, so mark it instead of
				 * letting it look like one of our own (its buttons are
				 * restricted, too). */
				if (cg->owner != this->company) text.append(GetString(STR_COUPLE_GROUP_LIST_SHARED));
				DrawString(ir.left, ir.right, y, text);
			}
		} else if (widget == WID_CG_SEGMENTS) {
			const Rect ir = r.Shrink(WidgetDimensions::scaled.framerect);
			if (this->segments.empty()) {
				DrawString(ir.left, ir.right, ir.top, STR_COUPLE_GROUP_NO_SEGMENTS);
				return;
			}
			int y = ir.top;
			for (int i = this->segment_scroll->GetPosition(); i < static_cast<int>(this->segments.size()) && this->segment_scroll->IsVisible(i); i++, y += this->resize.step_height) {
				if (i == this->selected_segment) GfxFillRect(r.left + 1, y, r.right, y + this->resize.step_height - 1, PC_DARK_GREY);
				const Train *t = Train::GetIfValid(this->segments[i]);
				if (t == nullptr) continue;
				std::string text = GetString(STR_COUPLE_GROUP_SEGMENT_ITEM, t->unitnumber, this->segment_cars[i]);
				/* Q5: show the other groups the segment is a member of, so that a
				 * multi-group segment is not mistaken for a single-group one. */
				const CoupleGroupMask others = R3RGetCoupleGroupsOfSegment(t) & ~R3RCoupleGroupBit(this->GetSelectedGroup());
				if (others != COUPLE_GROUP_MASK_NONE) {
					text.append(" ");
					text.append(GetString(STR_COUPLE_GROUP_SEGMENT_ALSO_IN, R3RGetCoupleGroupsNameList(others)));
				}
				/* R3R: mark the segment whose orders the whole chain follows. */
				const Train *owner = nullptr;
				uint index = 0;
				uint total = 0;
				if (R3RGetChainScheduleOwner(t, &owner, &index, &total) && owner == t) {
					text.append(" ");
					text.append(GetString(STR_COUPLE_GROUP_OWNER_MARK, index, total));
				}
				DrawString(ir.left, ir.right, y, text);
			}
		}
	}

	void OnResize() override
	{
		this->group_scroll->SetCapacityFromWidget(this, WID_CG_GROUPS, WidgetDimensions::scaled.framerect.Vertical());
		this->segment_scroll->SetCapacityFromWidget(this, WID_CG_SEGMENTS, WidgetDimensions::scaled.framerect.Vertical());
	}

	void OnPaint() override
	{
		const bool can_manage = this->IsSelectedGroupManageable();
		/* R3R (D6-③): assigning and removing our own segments only needs the
		 * weaker membership right, so a group which another company shared with
		 * us can be used without giving us power over the group itself. */
		const bool can_join = this->IsSelectedGroupJoinable();
		/* R3R (KI-55): "new group" needs a usable company. this->company holds the
		 * company this window was opened for (with the local company as fallback),
		 * while this->owner is re-applied in OnInit(); accept either so the button
		 * can never stay grey just because of how the window was opened. */
		const bool can_create = Company::IsValidID(this->company) || Company::IsValidID(this->owner);
		this->SetWidgetDisabledState(WID_CG_NEW, !can_create);
		this->SetWidgetDisabledState(WID_CG_RENAME, !can_manage);
		this->SetWidgetDisabledState(WID_CG_DELETE, !can_manage);
		this->SetWidgetDisabledState(WID_CG_REMOVE_SEGMENT, !can_join || this->GetSelectedSegment() == VehicleID::Invalid());
		this->SetWidgetDisabledState(WID_CG_ADD_SEGMENT, !can_join);

		/* R3R (KI-59): keep the button visually pressed while this window owns the
		 * picking mode. HandleButtonClick() arms a click timeout and the engine then
		 * calls RaiseButtons(true), which un-presses every push button a few ticks
		 * after the click even though the picking mode is still active -- making the
		 * mode look as if it had never started. Re-asserting it every frame keeps the
		 * button in sync with the real picking state. */
		this->SetWidgetLoweredState(WID_CG_ADD_SEGMENT, _thd.GetCallbackWnd() == this);

		/* R3R (D6-①+D6-③): the opt-in over the company boundary belongs to the
		 * selected group, so the checkbox mirrors it -- opened or closed as one
		 * switch -- and is only usable while that group is manageable by this
		 * company. Both savegame bits behind it are read as one, so a group
		 * which an older build opened for only one of the two halves still
		 * shows up as open (see CoupleGroup::CGF_ALLOW_OTHERS). */
		const CoupleGroup *sel_group = CoupleGroup::GetIfValid(this->GetSelectedGroup());
		const bool allow_others = sel_group != nullptr && sel_group->AllowsOthers();
		this->SetWidgetLoweredState(WID_CG_CROSS_COMPANY, allow_others);
		this->SetWidgetDisabledState(WID_CG_CROSS_COMPANY, !can_manage);

		/* R3R: edge-triggered diagnostics for KI-55/KI-56 -- reports whether the
		 * paint-time button state really ends up enabled and why (selection). */
		static uint last_report = 0xFFFFFFFFu;
		const uint report = (uint)(can_create ? 1 : 0) | ((uint)(can_manage ? 1 : 0) << 1) | ((uint)this->groups.size() << 2) |
				((uint)(this->selected_group + 1) << 12) | ((uint)this->segments.size() << 20) | ((uint)allow_others << 30);
		if (report != last_report) {
			last_report = report;
			const CoupleGroup *sel_cg = CoupleGroup::GetIfValid(this->GetSelectedGroup());
			const NWidgetCore *add_btn = this->GetWidget<NWidgetCore>(WID_CG_ADD_SEGMENT);
			R3RDbgWrite("[R3R] CG-PAINT owner=%u owner_valid=%d company=%u local=%u can_create=%d can_manage=%d groups=%u segments=%u sel=%d sel_cg=%d sel_cg_owner=%d add_disabled=%d lowered=%d\n",
					(unsigned)this->owner.base(), (int)Company::IsValidID(this->owner),
					(unsigned)this->company.base(), (unsigned)_local_company.base(),
					(int)can_create, (int)can_manage, (unsigned)this->groups.size(), (unsigned)this->segments.size(),
					this->selected_group, (int)(sel_cg != nullptr),
					(int)(sel_cg != nullptr ? sel_cg->owner.base() : 0xFFFFu),
					(int)(add_btn != nullptr && add_btn->IsDisabled()),
					(int)this->IsWidgetLowered(WID_CG_ADD_SEGMENT));
		}

		this->DrawWidgets();
	}

	void OnInvalidateData(int data = 0, bool gui_scope = true) override
	{
		if (!gui_scope) return;
		this->RebuildGroups();
	}

	void OnClick(Point pt, WidgetID widget, int click_count) override
	{
		switch (widget) {
			case WID_CG_GROUPS: {
				const int sel = this->group_scroll->GetScrolledRowFromWidget(pt.y, this, WID_CG_GROUPS, WidgetDimensions::scaled.framerect.top);
				/* R3R (KI-56): clicking the empty area below the list keeps the current
				 * selection instead of clearing it.  Clearing it would disable every
				 * management button -- including "assign segment" -- so a stray click
				 * next to the list made the buttons turn grey (and look broken). */
				if (sel == INT_MAX || sel >= static_cast<int>(this->groups.size())) break;
				if (sel != this->selected_group) {
					this->selected_group = sel;
					this->RebuildSegments();
					this->SetDirty();
				}
				break;
			}

			case WID_CG_SEGMENTS: {
				const int sel = this->segment_scroll->GetScrolledRowFromWidget(pt.y, this, WID_CG_SEGMENTS, WidgetDimensions::scaled.framerect.top);
				this->selected_segment = (sel == INT_MAX || sel >= static_cast<int>(this->segments.size())) ? -1 : sel;
				this->SetDirty();
				break;
			}

			case WID_CG_NEW:
				this->query_is_rename = false;
				ShowQueryString(std::string_view{}, STR_COUPLE_GROUP_QUERY_NEW, MAX_LENGTH_COUPLE_GROUP_NAME_CHARS, this, CS_ALPHANUMERAL, QueryStringFlag::LengthIsInChars);
				break;

			case WID_CG_RENAME: {
				const CoupleGroupID id = this->GetSelectedGroup();
				if (!this->IsSelectedGroupManageable()) break;
				this->query_is_rename = true;
				ShowQueryString(CoupleGroup::Get(id)->name, STR_COUPLE_GROUP_QUERY_RENAME, MAX_LENGTH_COUPLE_GROUP_NAME_CHARS, this, CS_ALPHANUMERAL, QueryStringFlag::LengthIsInChars);
				break;
			}

			case WID_CG_DELETE: {
				const CoupleGroupID id = this->GetSelectedGroup();
				if (!this->IsSelectedGroupManageable()) break;
				ShowQuery(GetEncodedString(STR_COUPLE_GROUP_QUERY_DELETE_CAPTION), GetEncodedString(STR_COUPLE_GROUP_QUERY_DELETE, CoupleGroup::Get(id)->name), this, DeleteCoupleGroupCallback);
				break;
			}

			case WID_CG_CROSS_COMPANY: {
				/* R3R (D6-①+D6-③): toggle the one opt-in over the company
				 * boundary on the selected group. Opening it lets the other
				 * companies assign their own segments to the group *and* couples
				 * them across the company boundary through it; closing it is not
				 * just cosmetic -- the command takes the segments of the other
				 * companies back out of the group again. The command is the only
				 * writer of the flags, so a click which is refused (not
				 * manageable) leaves the checkbox as it was. */
				const CoupleGroupID id = this->GetSelectedGroup();
				if (!this->IsSelectedGroupManageable()) break;
				const CoupleGroup *cg = CoupleGroup::Get(id);
				const bool enable = cg == nullptr || !cg->AllowsOthers();
				Command<Commands::SetCoupleGroupAllowOthers>::Post(STR_ERROR_CAN_T_DO_THIS, id, enable);
				break;
			}

			case WID_CG_REMOVE_SEGMENT: {
				const VehicleID vid = this->GetSelectedSegment();
				const CoupleGroupID group = this->GetSelectedGroup();
				if (vid == VehicleID::Invalid() || group == INVALID_COUPLE_GROUP || !this->IsSelectedGroupJoinable()) break;
				/* Q5: only this group is removed; the segment keeps its other groups. */
				Command<Commands::RemoveCoupleGroup>::Post(STR_ERROR_CAN_T_DO_THIS, vid, group);
				break;
			}

			case WID_CG_ADD_SEGMENT:
				/* R3R (KI-59): "assign segment" is a picking-mode toggle.  The
				 * active mode must be read from the engine's picking state, NOT
				 * from the widget's lowered flag: DispatchLeftClickEvent() calls
				 * HandleButtonClick() -- which does LowerWidget() -- *before* it
				 * calls OnClick(), so IsWidgetLowered() is unconditionally true in
				 * here.  A toggle based on it therefore always took the "cancel"
				 * branch and the picking mode could never be entered at all -- that
				 * was the "assign segment does nothing" bug. */
				R3RDbgWrite("[R3R] CG-CLICK-ADD manage=%d groups=%u sel=%d picking=%d\n",
						(int)this->IsSelectedGroupManageable(), (unsigned)this->groups.size(),
						this->selected_group, (int)(_thd.GetCallbackWnd() == this));
				if (!this->IsSelectedGroupJoinable()) break;
				if (_thd.GetCallbackWnd() == this) {
					/* Already picking for this window: cancel the mode. */
					this->RaiseWidget(WID_CG_ADD_SEGMENT);
					this->SetWidgetDirty(WID_CG_ADD_SEGMENT);
					ResetObjectToPlace();
					break;
				}
				this->LowerWidget(WID_CG_ADD_SEGMENT);
				this->SetWidgetDirty(WID_CG_ADD_SEGMENT);
				SetObjectToPlaceWnd(SPR_CURSOR_MOUSE, PAL_NONE, HT_VEHICLE, this);
				break;

			default:
				break;
		}
	}

	/**
	 * R3R: assign the segment of the clicked vehicle to the selected couple group.
	 * Only reached while this window is in picking mode.
	 */
	bool OnVehicleSelect(const Vehicle *v) override
	{
		/* R3R (KI-59): see OnClick -- the widget's lowered flag is not a reliable
		 * "am I picking?" test (the engine lowers it before OnClick() and raises it
		 * again when the click timeout expires), so test the engine picking state. */
		if (_thd.GetCallbackWnd() != this) return false;

		R3RDbgWrite("[R3R] CG-PICK veh=%d\n", (int)(v == nullptr ? -1 : (int)v->index.base()));

		this->RaiseWidget(WID_CG_ADD_SEGMENT);
		this->SetWidgetDirty(WID_CG_ADD_SEGMENT);
		ResetObjectToPlace();

		const CoupleGroupID group = this->GetSelectedGroup();
		if (group == INVALID_COUPLE_GROUP || v == nullptr) return true;
		/* The command resolves the segment head itself; the group follows the
		 * segment, not the individual car (Q3). */
		Command<Commands::SetCoupleGroup>::Post(STR_ERROR_CAN_T_DO_THIS, v->index, group);
		return true;
	}

	void OnPlaceObjectAbort() override
	{
		/* R3R (KI-59): the engine calls this when the picking mode ends (world
		 * click, ESC, or another window taking over), so always restore the button
		 * regardless of its lowered state. */
		this->RaiseWidget(WID_CG_ADD_SEGMENT);
		this->SetWidgetDirty(WID_CG_ADD_SEGMENT);
	}

	void OnQueryTextFinished(std::optional<std::string> str) override
	{
		if (!str.has_value() || str->empty()) return;
		if (this->query_is_rename) {
			const CoupleGroupID id = this->GetSelectedGroup();
			if (id == INVALID_COUPLE_GROUP) return;
			Command<Commands::RenameCoupleGroup>::Post(STR_ERROR_CAN_T_DO_THIS, id, *str);
		} else {
			/* R3R: select the created group as soon as the command went through. */
			this->pending_select_newest = true;
			Command<Commands::CreateCoupleGroup>::Post(STR_ERROR_CAN_T_DO_THIS, *str);
		}
	}
};

void CoupleGroupWindow::DeleteCoupleGroupCallback(Window *w, bool confirmed)
{
	if (!confirmed) return;
	const CoupleGroupID id = static_cast<CoupleGroupWindow *>(w)->GetSelectedGroup();
	if (id == INVALID_COUPLE_GROUP) return;
	Command<Commands::DeleteCoupleGroup>::Post(STR_ERROR_CAN_T_DO_THIS, id);
}

/** R3R: Show the couple group window for \a company. */
void ShowCoupleGroupWindow(Owner company)
{
	/* R3R: entry point probe -- written for every request (the depot button),
	 * while CG-WIN above is only written when the window is actually built. */
	R3RDbgWrite("[R3R] CG-SHOW req owner=%u valid=%d local=%u\n",
			(unsigned)company.base(), (int)Company::IsValidID(company),
			(unsigned)_local_company.base());

	if (BringWindowToFrontById(WindowClass::CoupleGroup, 0) != nullptr) return;
	new CoupleGroupWindow(_couple_group_desc, company);
}
