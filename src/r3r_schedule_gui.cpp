/*
 * This file is part of OpenTTD.
 * OpenTTD is free software; you can redistribute it and/or modify it under the terms of the GNU General Public License as published by the Free Software Foundation, version 2.
 * OpenTTD is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 * See the GNU General Public License for more details. You should have received a copy of the GNU General Public License along with OpenTTD. If not, see <https://www.gnu.org/licenses/old-licenses/gpl-2.0>.
 */

/**
 * @file r3r_schedule_gui.cpp
 * R3R (第 417 轮): the schedule pool (排程池) management window.
 *
 * The window lists every order list of the savegame. A list which some vehicle
 * points at is shown as an "owned" schedule (the schedule a train carries around
 * with it); a list nothing points at is a "loose" (无主) schedule created from
 * this window, which lives in the pool only.
 *
 * "New" creates an empty loose schedule right away (KI-595). "Assign" opens a
 * picker which shows the trains with their images; pressing a train there only
 * re-points `Vehicle::orders` at the selected list, i.e. the train and every other
 * user run the very same list afterwards (KI-593).
 */

#include "stdafx.h"
#include "r3r_schedule_gui.h"
#include "command_func.h"
#include "company_base.h"
#include "company_func.h"
#include "dropdown_func.h"
#include "error.h"
#include "gfx_func.h"
#include "gui.h"
#include "order_base.h"
#include "order_cmd.h"
#include "palette_func.h"
#include "sprite.h"
#include "strings_func.h"
#include "textbuf_gui.h"
#include "train.h"
#include "vehicle_base.h"
#include "vehicle_gui.h"
#include "window_func.h"
#include "window_gui.h"
#include "zoom_func.h"
#include "widgets/r3r_schedule_widget.h"

#include "table/strings.h"

#include "safeguards.h"

/**
 * R3R (第 419 轮): open the picker which assigns \a schedule to one of our trains.
 *
 * The picker is a window of its own so it can draw the train images next to the
 * names; the pool window no longer carries a "target train" drop down (KI-593).
 */
static void ShowR3RScheduleAssignWindow(CompanyID company, OrderListID schedule);

/**
 * R3R (第 419 轮): the display label of a schedule.
 *
 * The stored name wins; an unnamed schedule of a train falls back to that train and
 * an unnamed loose schedule to a generic label. Shared by the pool rows and the
 * train picker so both sides read the same text.
 */
static std::string R3RScheduleLabelOf(const OrderList *ol)
{
	if (ol == nullptr) return {};
	if (!ol->GetR3RScheduleName().empty()) return ol->GetR3RScheduleName();
	const Vehicle *const first = ol->GetFirstSharedVehicle();
	if (first != nullptr) return GetString(STR_VEHICLE_NAME, first->index.base());
	return GetString(STR_R3R_SCHEDULE_UNNAMED);
}

static constexpr NWidgetPart _nested_r3r_schedule_widgets[] = {
	NWidget(NWID_HORIZONTAL),
		NWidget(WWT_CLOSEBOX, Colours::Grey),
		NWidget(WWT_CAPTION, Colours::Grey, WID_SCHED_CAPTION), SetStringTip(STR_R3R_SCHEDULE_CAPTION, STR_TOOLTIP_WINDOW_TITLE_DRAG_THIS),
		NWidget(WWT_SHADEBOX, Colours::Grey),
		NWidget(WWT_DEFSIZEBOX, Colours::Grey),
		NWidget(WWT_STICKYBOX, Colours::Grey),
	EndContainer(),

	/* R3R (第 419 轮): the "target train" drop down is gone. The player selects a
	 * schedule first and then presses "Assign", which opens a picker showing the
	 * trains with their images (KI-593). */
	NWidget(NWID_HORIZONTAL),
		NWidget(WWT_PANEL, Colours::Grey, WID_SCHED_LIST), SetMinimalSize(360, 130), SetResize(1, 10), SetToolTip(STR_R3R_SCHEDULE_LIST_TOOLTIP), SetScrollbar(WID_SCHED_SCROLLBAR), EndContainer(),
		NWidget(NWID_VSCROLLBAR, Colours::Grey, WID_SCHED_SCROLLBAR),
	EndContainer(),

	/* R3R (第 418 轮): the button rows must expand with the window, otherwise the
	 * equal-size container keeps its 4x80px minimum width and leaves an empty strip
	 * on the right of the bar (the "镂空" the players reported). */
	NWidget(NWID_HORIZONTAL),
		NWidget(NWID_HORIZONTAL, NWidContainerFlag::EqualSize),
			NWidget(WWT_PUSHTXTBTN, Colours::Grey, WID_SCHED_NEW), SetMinimalSize(80, 12), SetStringTip(STR_R3R_SCHEDULE_NEW, STR_R3R_SCHEDULE_NEW_TOOLTIP),
			NWidget(WWT_PUSHTXTBTN, Colours::Grey, WID_SCHED_RENAME), SetMinimalSize(80, 12), SetStringTip(STR_R3R_SCHEDULE_RENAME, STR_R3R_SCHEDULE_RENAME_TOOLTIP),
			NWidget(WWT_PUSHTXTBTN, Colours::Grey, WID_SCHED_EDIT), SetMinimalSize(80, 12), SetStringTip(STR_R3R_SCHEDULE_EDIT, STR_R3R_SCHEDULE_EDIT_TOOLTIP),
			NWidget(WWT_PUSHTXTBTN, Colours::Grey, WID_SCHED_PREVIEW), SetMinimalSize(80, 12), SetStringTip(STR_R3R_SCHEDULE_PREVIEW, STR_R3R_SCHEDULE_PREVIEW_TOOLTIP),
			SetResize(1, 0), SetFill(1, 0),
		EndContainer(),
		NWidget(WWT_RESIZEBOX, Colours::Grey),
	EndContainer(),

	NWidget(NWID_HORIZONTAL),
		NWidget(NWID_HORIZONTAL, NWidContainerFlag::EqualSize),
			/* This fork has no WWT_CHECKBOX; a lowered (pressed) text button is the
			 * established way of showing an on/off state. */
			NWidget(WWT_TEXTBTN, Colours::Grey, WID_SCHED_PUBLIC), SetMinimalSize(100, 12), SetStringTip(STR_R3R_SCHEDULE_PUBLIC, STR_R3R_SCHEDULE_PUBLIC_TOOLTIP),
			NWidget(WWT_PUSHTXTBTN, Colours::Grey, WID_SCHED_DELETE), SetMinimalSize(80, 12), SetStringTip(STR_R3R_SCHEDULE_DELETE, STR_R3R_SCHEDULE_DELETE_TOOLTIP),
			NWidget(WWT_PUSHTXTBTN, Colours::Grey, WID_SCHED_ASSIGN), SetMinimalSize(80, 12), SetStringTip(STR_R3R_SCHEDULE_ASSIGN, STR_R3R_SCHEDULE_ASSIGN_TOOLTIP),
			SetResize(1, 0), SetFill(1, 0),
		EndContainer(),
		NWidget(WWT_RESIZEBOX, Colours::Grey),
	EndContainer(),

	NWidget(WWT_PANEL, Colours::Grey, WID_SCHED_HINT), SetMinimalSize(360, 30), SetResize(0, 0), EndContainer(),
};

/** R3R (第 417 轮): the schedule pool window. One instance per player. */
struct R3RSchedulePoolWindow : Window {
private:
	Scrollbar *scroll = nullptr;          ///< Scrollbar of the schedule list.
	CompanyID company = CompanyID::Invalid(); ///< Company this window was opened for.
	VehicleID target = VehicleID::Invalid(); ///< Train whose schedule is marked with "* " (context only, there is no drop down any more).
	std::vector<OrderListID> rows;        ///< Schedules listed in the window, in pool order.
	std::vector<OrderListID> known_rows;  ///< Schedules seen by the previous #Rebuild, used to spot a newly created one.
	bool pending_select_new = false;      ///< Select the schedule created by the last "New" click as soon as it shows up.
	int selected = -1;                    ///< Index into #rows, -1 if nothing is selected.
	OrderListID pending_delete = OrderListID::Invalid(); ///< Schedule the pending delete confirmation applies to.
	OrderListID query_schedule = OrderListID::Invalid(); ///< Schedule the open rename query applies to (indices may shift meanwhile).

	/** Whether \a ol can be run by a train and therefore belongs in the list. */
	static bool IsListable(const OrderList *ol)
	{
		if (ol == nullptr) return false;
		const Vehicle *const first = ol->GetFirstSharedVehicle();
		/* R3R (第 417 轮): a loose schedule has no vehicle, so it is offered to
		 * every type; an owned schedule is only listed when a train owns it. */
		return first == nullptr || first->type == VehicleType::Train;
	}

	/** Rebuild #rows from the order list pool. */
	void Rebuild()
	{
		this->rows.clear();
		for (OrderList *ol : OrderList::Iterate()) {
			if (this->IsListable(ol)) this->rows.push_back(ol->index);
		}
		this->scroll->SetCount(this->rows.size());
		if (this->selected >= static_cast<int>(this->rows.size())) this->selected = -1;
		if (this->selected < 0 && !this->rows.empty()) this->selected = 0;

		/* R3R (第 419 轮): "New" posts its command, which only runs on the next tick, so
		 * select whichever schedule was not in the list the last time we looked (KI-596). */
		if (this->pending_select_new) {
			for (size_t i = 0; i < this->rows.size(); i++) {
				bool was_known = false;
				for (const OrderListID &id : this->known_rows) {
					if (id == this->rows[i]) {
						was_known = true;
						break;
					}
				}
				if (!was_known) {
					this->selected = static_cast<int>(i);
					this->pending_select_new = false;
					break;
				}
			}
		}
		this->known_rows = this->rows;

		/* The target train may have been sold in the meantime. It is only used to mark
		 * the schedule it runs, so no replacement train is picked any more. */
		const Vehicle *const t = Vehicle::GetIfValid(this->target);
		if (t == nullptr || t->owner != this->company || t->type != VehicleType::Train || !t->IsPrimaryVehicle()) {
			this->target = VehicleID::Invalid();
		}
	}

	/** The selected schedule, or nullptr. */
	OrderList *GetSelectedList() const
	{
		if (this->selected < 0 || this->selected >= static_cast<int>(this->rows.size())) return nullptr;
		return OrderList::GetIfValid(this->rows[this->selected]);
	}

	/** The target train, or nullptr. */
	Vehicle *GetTargetVehicle() const
	{
		return Vehicle::GetIfValid(this->target);
	}

	/** Whether the local company may edit the pool. */
	bool CanEdit() const
	{
		return Company::IsValidID(this->company) && this->company == _local_company;
	}

	/**
	 * R3R (第 418 轮): whether \a ol belongs to the company shown in this window.
	 *
	 * Only the owner may rename, publish or delete a schedule. A schedule which
	 * records no owner yet (loose schedule of an older savegame) counts as ours so
	 * the player is never locked out of it.
	 */
	bool IsOwnSchedule(const OrderList *ol) const
	{
		if (ol == nullptr) return false;
		const CompanyID owner = ol->GetR3RScheduleOwner();
		return owner == CompanyID::Invalid() || owner == this->company;
	}

	/** R3R (第 418 轮): the display label of a schedule, used by the rows and the drop downs. */
	std::string ScheduleLabel(const OrderList *ol) const
	{
		return R3RScheduleLabelOf(ol);
	}

	/** Confirmation callback for deleting the selected loose schedule. */
	static void DeleteScheduleCallback(Window *w, bool confirmed);

public:
	R3RSchedulePoolWindow(WindowDesc &desc, CompanyID company, VehicleID target) : Window(desc), company(company), target(target)
	{
		this->CreateNestedTree();
		this->scroll = this->GetScrollbar(WID_SCHED_SCROLLBAR);
		this->FinishInitNested(0);
	}

	void OnInit() override
	{
		/* Window::InitializeData() resets Window::owner after the constructor body. */
		this->owner = Company::IsValidID(this->company) ? this->company : _local_company;
		this->company = static_cast<CompanyID>(this->owner);
		this->Rebuild();
	}

	/** Re-target an already open window (the pool window is a singleton). */
	void SetCompanyAndTarget(CompanyID new_company, VehicleID new_target)
	{
		if (Company::IsValidID(new_company)) {
			this->company = new_company;
			this->owner = new_company;
		}
		if (new_target != VehicleID::Invalid()) this->target = new_target;
		this->Rebuild();
		this->SetDirty();
	}

	void UpdateWidgetSize(WidgetID widget, Dimension &size, const Dimension &padding, Dimension &fill, Dimension &resize) override
	{
		if (widget == WID_SCHED_LIST) {
			resize.height = GetCharacterHeight(FontSize::Normal) + 2;
			size.height = resize.height * 10 + WidgetDimensions::scaled.framerect.Vertical();
		} else if (widget == WID_SCHED_HINT) {
			size.height = GetCharacterHeight(FontSize::Normal) * 2 + WidgetDimensions::scaled.framerect.Vertical() + 2;
		}
	}

	void DrawWidget(const Rect &r, WidgetID widget) const override
	{
		if (widget == WID_SCHED_HINT) {
			const Rect ir = r.Shrink(WidgetDimensions::scaled.framerect);
			DrawStringMultiLine(ir.left, ir.right, ir.top, ir.bottom, GetString(STR_R3R_SCHEDULE_HINT), TextColour::White, {AlignmentH::Centre, AlignmentV::Top});
			return;
		}
		if (widget != WID_SCHED_LIST) return;

		const Rect ir = r.Shrink(WidgetDimensions::scaled.framerect);
		if (this->rows.empty()) {
			DrawString(ir.left, ir.right, ir.top, STR_R3R_SCHEDULE_EMPTY);
			return;
		}

		/* The row pitch must come from the list widget itself (KI-262 family: the
		 * container's resize_y can be dragged to 0 by a zero-step sibling). */
		const NWidgetBase *const list_widget = this->GetWidget<NWidgetBase>(WID_SCHED_LIST);
		const int step = (list_widget != nullptr && list_widget->resize_y > 0) ? static_cast<int>(list_widget->resize_y) : (GetCharacterHeight(FontSize::Normal) + 2);

		const Vehicle *const target = this->GetTargetVehicle();
		const OrderList *const current = (target != nullptr) ? target->orders : nullptr;

		for (int i = this->scroll->GetPosition(), y = ir.top; i < static_cast<int>(this->rows.size()) && this->scroll->IsVisible(i); i++, y += step) {
			if (i == this->selected) GfxFillRect(r.left + 1, y, r.right, y + step - 1, PC_DARK_GREY);
			const OrderList *const ol = OrderList::GetIfValid(this->rows[i]);
			if (ol == nullptr) continue;

			/* R3R (第 418 轮): the schedules of other companies are listed too. Their company
			 * colour is drawn as a small block in front of the name, and a schedule which is
			 * not published (private) is greyed out because our trains may not be pointed at
			 * it (C-4 visibility, C-5 authorisation). */
			int left = ir.left;
			const CompanyID owner = ol->GetR3RScheduleOwner();
			if (Company::IsValidID(owner) && owner != this->company) {
				GfxFillRect(left, y + 2, left + 3, y + step - 3, GetColourGradient(Company::Get(owner)->colour, Shade::Dark));
				left += 6;
			}

			/* R3R (第 419 轮): a published schedule of our own is marked with a small
			 * flag in front of its name, the same way a published sign is (KI-594). */
			if (ol->IsR3RSchedulePublic() && this->IsOwnSchedule(ol)) {
				const Dimension flag = GetSpriteSize(SPR_FLAG_VEH_RUNNING);
				DrawSprite(SPR_FLAG_VEH_RUNNING, PAL_NONE, left, y + (step - static_cast<int>(flag.height)) / 2);
				left += flag.width + 2;
			}

			std::string text;
			const Vehicle *const first = ol->GetFirstSharedVehicle();
			if (first == nullptr && ol->GetR3RScheduleName().empty()) {
				text = GetString(STR_R3R_SCHEDULE_ROW_LOOSE, static_cast<uint>(ol->GetNumOrders()));
			} else {
				/* {RAW_STRING} needs an already rendered string, so fetch the label first. */
				const std::string name = this->ScheduleLabel(ol);
				if (first != nullptr && ol->GetNumVehicles() > 1) {
					text = GetString(STR_R3R_SCHEDULE_ROW_OWNED_SHARED, name, static_cast<uint>(ol->GetNumOrders()), static_cast<uint>(ol->GetNumVehicles()));
				} else {
					text = GetString(STR_R3R_SCHEDULE_ROW_OWNED, name, static_cast<uint>(ol->GetNumOrders()));
				}
			}
			/* Mark the schedule the selected train is currently running. */
			if (ol == current) text.insert(0, "* ");

			DrawString(left, ir.right, y, text, R3RScheduleAccessibleBy(ol, this->company) ? TextColour::White : TextColour::Grey);
		}
	}

	void OnResize() override
	{
		this->scroll->SetCapacityFromWidget(this, WID_SCHED_LIST, WidgetDimensions::scaled.framerect.Vertical());
	}

	void OnPaint() override
	{
		const bool can_edit = this->CanEdit();
		const OrderList *const sel = this->GetSelectedList();
		const bool sel_own = this->IsOwnSchedule(sel);
		const bool sel_has_vehicle = sel != nullptr && sel->GetFirstSharedVehicle() != nullptr;

		this->SetWidgetDisabledState(WID_SCHED_NEW, !can_edit);
		/* R3R (第 418 轮): renaming, publishing and deleting stay with the owner (C-5). */
		this->SetWidgetDisabledState(WID_SCHED_RENAME, !can_edit || !sel_own);
		this->SetWidgetDisabledState(WID_SCHED_PUBLIC, !can_edit || !sel_own);
		/* R3R (第 421 轮, C-8 方案 1): the button stays clickable for a schedule in use so
		 * that the click can explain why it cannot be deleted; the command itself still
		 * refuses it (a vehicle would be left pointing at a freed list). */
		this->SetWidgetDisabledState(WID_SCHED_DELETE, !can_edit || !sel_own || sel == nullptr);
		/* Editing needs only the schedule itself: a loose one is edited in the dedicated
		 * editor window (C-1). The vehicle-list preview still needs a train (C-3). */
		this->SetWidgetDisabledState(WID_SCHED_EDIT, !can_edit || !sel_own);
		this->SetWidgetDisabledState(WID_SCHED_PREVIEW, !sel_has_vehicle);
		/* Our own schedules are always assignable, the ones of other companies only when
		 * they are published (C-5). The train is picked in the following window, so this
		 * button no longer depends on a target train (KI-593). */
		this->SetWidgetDisabledState(WID_SCHED_ASSIGN, !can_edit || sel == nullptr ||
				!R3RScheduleAccessibleBy(sel, this->company));
		this->SetWidgetLoweredState(WID_SCHED_PUBLIC, sel != nullptr && sel->IsR3RSchedulePublic());
		this->DrawWidgets();
	}

	void OnInvalidateData(int data = 0, bool gui_scope = true) override
	{
		if (!gui_scope) return;
		this->Rebuild();
		this->SetDirty();
	}

	void OnClick(Point pt, WidgetID widget, int click_count) override
	{
		switch (widget) {
			case WID_SCHED_LIST: {
				const size_t sel = this->scroll->GetScrolledRowFromWidget(pt.y, this, WID_SCHED_LIST, WidgetDimensions::scaled.framerect.top);
				if (sel == Scrollbar::npos || sel >= this->rows.size()) break;
				if (static_cast<int>(sel) != this->selected) {
					this->selected = static_cast<int>(sel);
					this->SetDirty();
				}
				break;
			}

			case WID_SCHED_NEW: {
				if (!this->CanEdit()) break;
				/* R3R (第 419 轮): one click creates an empty loose schedule; there is no
				 * drop down and no query any more (KI-595). The command only runs on the
				 * next tick, so remember to select the new row once it shows up. */
				this->pending_select_new = true;
				Command<Commands::CreateR3RLooseSchedule>::Post(STR_ERROR_CAN_T_DO_THIS, 0);
				this->SetDirty();
				break;
			}

			case WID_SCHED_RENAME: {
				const OrderList *const ol = this->GetSelectedList();
				if (ol == nullptr || !this->CanEdit() || !this->IsOwnSchedule(ol)) break;
				/* Remember the schedule, the row index may shift while the query is open. */
				this->query_schedule = ol->index;
				ShowQueryString(this->ScheduleLabel(ol), STR_R3R_SCHEDULE_RENAME_TITLE, 32, this, CS_ALPHANUMERAL, QueryStringFlag::LengthIsInChars);
				break;
			}

			case WID_SCHED_EDIT: {
				const OrderList *const ol = this->GetSelectedList();
				if (ol == nullptr || !this->CanEdit() || !this->IsOwnSchedule(ol)) break;
				const Vehicle *const first = ol->GetFirstSharedVehicle();
				if (first != nullptr) {
					ShowOrdersWindow(first);
				} else {
					/* R3R (第 421 轮, C-8 / E-2): a loose schedule has no vehicle at all,
					 * so it is edited through the dedicated order editor. */
					ShowR3RScheduleEditor(const_cast<OrderList *>(ol));
				}
				break;
			}

			case WID_SCHED_PREVIEW: {
				const OrderList *const ol = this->GetSelectedList();
				if (ol == nullptr) break;
				const Vehicle *const first = ol->GetFirstSharedVehicle();
				if (first == nullptr) break;
				/* C-3 = A: reuse the shared-orders vehicle list, it draws the train images. */
				ShowVehicleListWindow(first);
				break;
			}

			case WID_SCHED_PUBLIC: {
				const OrderList *const ol = this->GetSelectedList();
				if (ol == nullptr || !this->CanEdit() || !this->IsOwnSchedule(ol)) break;
				Command<Commands::SetR3RSchedulePublic>::Post(STR_ERROR_CAN_T_DO_THIS, ol->index.base(), !ol->IsR3RSchedulePublic());
				this->SetDirty();
				break;
			}

			case WID_SCHED_DELETE: {
				const OrderList *const ol = this->GetSelectedList();
				if (ol == nullptr || !this->CanEdit()) break;
				if (ol->GetFirstSharedVehicle() != nullptr || ol->GetNumVehicles() != 0) {
					/* R3R (第 421 轮, C-8 方案 1): deleting a schedule which is in use would
					 * leave its vehicles pointing at a freed order list, so it is refused.
					 * Say why instead of silently ignoring the click. */
					ShowErrorMessage(GetEncodedString(STR_R3R_SCHEDULE_DELETE_IN_USE), {}, WarningLevel::Info);
					break;
				}
				this->pending_delete = ol->index;
				ShowQuery(GetEncodedString(STR_R3R_SCHEDULE_DELETE_TITLE), GetEncodedString(STR_R3R_SCHEDULE_DELETE_CONFIRM, static_cast<uint>(ol->GetNumOrders())), this, DeleteScheduleCallback);
				break;
			}

			case WID_SCHED_ASSIGN: {
				const OrderList *const ol = this->GetSelectedList();
				if (ol == nullptr || !this->CanEdit()) break;
				if (!R3RScheduleAccessibleBy(ol, this->company)) break;
				/* R3R (第 419 轮): pick the train in a window of its own instead of
				 * choosing it from a drop down beforehand (KI-593). */
				ShowR3RScheduleAssignWindow(this->company, ol->index);
				break;
			}

			default:
				break;
		}
	}

	void OnQueryTextFinished(std::optional<std::string> str) override
	{
		const OrderList *const ol = OrderList::GetIfValid(this->query_schedule);
		this->query_schedule = OrderListID::Invalid();
		if (!str.has_value() || ol == nullptr || !this->CanEdit() || !this->IsOwnSchedule(ol)) return;
		/* An empty name is allowed: it falls back to the name of the first train. */
		Command<Commands::RenameR3RSchedule>::Post(STR_ERROR_CAN_T_DO_THIS, ol->index.base(), *str);
		this->SetDirty();
	}
};

void R3RSchedulePoolWindow::DeleteScheduleCallback(Window *w, bool confirmed)
{
	if (!confirmed) return;
	R3RSchedulePoolWindow *sw = static_cast<R3RSchedulePoolWindow *>(w);
	const OrderListID id = sw->pending_delete;
	sw->pending_delete = OrderListID::Invalid();
	if (id == OrderListID::Invalid()) return;
	Command<Commands::DeleteR3RLooseSchedule>::Post(STR_ERROR_CAN_T_DO_THIS, id.base());
}

static WindowDesc _r3r_schedule_desc(__FILE__, __LINE__,
	WindowPosition::Automatic, "r3r_schedule_pool", 380, 230,
	WindowClass::R3RSchedule, WindowClass::None,
	{}, _nested_r3r_schedule_widgets
);

/** R3R (第 417 轮): open (or re-target and bring to the front) the schedule pool window. */
void ShowR3RScheduleWindow(CompanyID company, VehicleID target)
{
	if (!Company::IsValidID(company)) company = _local_company;

	R3RSchedulePoolWindow *w = static_cast<R3RSchedulePoolWindow *>(FindWindowById(WindowClass::R3RSchedule, 0));
	if (w == nullptr) {
		new R3RSchedulePoolWindow(_r3r_schedule_desc, company, target);
		return;
	}
	/* Re-target an already open window; the pool window is a singleton. */
	w->SetCompanyAndTarget(company, target);
	BringWindowToFrontById(WindowClass::R3RSchedule, 0);
}

/**
 * R3R (第 419 轮): the train picker of "Assign" (KI-593).
 *
 * The player selects a schedule in the pool window, presses "Assign" and gets this
 * window: every train of the company with its image. Clicking a train re-points its
 * schedule pointer at the selected schedule and closes the picker.
 */
static constexpr NWidgetPart _nested_r3r_schedule_assign_widgets[] = {
	NWidget(NWID_HORIZONTAL),
		NWidget(WWT_CLOSEBOX, Colours::Grey),
		NWidget(WWT_CAPTION, Colours::Grey, WID_SCHED_AS_CAPTION), SetStringTip(STR_R3R_SCHEDULE_ASSIGN_PICK_TITLE, STR_TOOLTIP_WINDOW_TITLE_DRAG_THIS),
		NWidget(WWT_SHADEBOX, Colours::Grey),
		NWidget(WWT_STICKYBOX, Colours::Grey),
	EndContainer(),

	NWidget(NWID_HORIZONTAL),
		NWidget(WWT_PANEL, Colours::Grey, WID_SCHED_AS_LIST), SetMinimalSize(320, 110), SetResize(1, 10), SetToolTip(STR_R3R_SCHEDULE_ASSIGN_PICK_TOOLTIP), SetScrollbar(WID_SCHED_AS_SCROLLBAR), EndContainer(),
		NWidget(NWID_VSCROLLBAR, Colours::Grey, WID_SCHED_AS_SCROLLBAR),
	EndContainer(),

	NWidget(NWID_HORIZONTAL),
		NWidget(WWT_PANEL, Colours::Grey), SetMinimalSize(0, 0), SetResize(1, 0), SetFill(1, 0), EndContainer(),
		NWidget(WWT_RESIZEBOX, Colours::Grey),
	EndContainer(),
};

/** R3R (第 419 轮): the "pick a train" window opened by the schedule pool. */
struct R3RScheduleAssignWindow : Window {
private:
	Scrollbar *scroll = nullptr;              ///< Scrollbar of the train list.
	CompanyID company = CompanyID::Invalid(); ///< Company whose trains are listed.
	OrderListID schedule = OrderListID::Invalid(); ///< Schedule the picked train is pointed at.
	std::vector<VehicleID> rows;              ///< Trains offered in this window.

	/** Collect the company's trains (the command only accepts the schedule owner). */
	void Rebuild()
	{
		this->rows.clear();
		for (Vehicle *v : Vehicle::Iterate()) {
			if (v->type != VehicleType::Train) continue;
			if (v->owner != this->company) continue;
			/* Only the head of a train owns the schedule pointer, see CmdR3RAssignSchedule. */
			if (!v->IsPrimaryVehicle()) continue;
			this->rows.push_back(v->index);
		}
		this->scroll->SetCount(this->rows.size());
	}

public:
	/** Re-target the already open picker at another schedule. */
	void SetSchedule(OrderListID new_schedule)
	{
		this->schedule = new_schedule;
		this->SetDirty();
	}

	R3RScheduleAssignWindow(WindowDesc &desc, CompanyID company, OrderListID schedule) : Window(desc), company(company), schedule(schedule)
	{
		this->CreateNestedTree();
		this->scroll = this->GetScrollbar(WID_SCHED_AS_SCROLLBAR);
		this->FinishInitNested(1);
	}

	void OnInit() override
	{
		/* Window::InitializeData() resets Window::owner after the constructor body. */
		this->owner = Company::IsValidID(this->company) ? this->company : _local_company;
		this->company = static_cast<CompanyID>(this->owner);
		this->Rebuild();
	}

	void UpdateWidgetSize(WidgetID widget, Dimension &size, const Dimension &padding, Dimension &fill, Dimension &resize) override
	{
		if (widget == WID_SCHED_AS_LIST) {
			resize.height = std::max<uint>(ScaleGUITrad(GetVehicleHeight(VehicleType::Train)), GetCharacterHeight(FontSize::Normal)) + 2;
			size.height = resize.height * 6 + WidgetDimensions::scaled.framerect.Vertical();
		}
	}

	void DrawWidget(const Rect &r, WidgetID widget) const override
	{
		if (widget != WID_SCHED_AS_LIST) return;
		const Rect ir = r.Shrink(WidgetDimensions::scaled.framerect);

		if (this->rows.empty()) {
			DrawStringMultiLine(ir.left, ir.right, ir.top, ir.bottom, GetString(STR_R3R_SCHEDULE_ASSIGN_PICK_EMPTY), TextColour::White, {AlignmentH::Centre, AlignmentV::Top});
			return;
		}

		/* The row pitch must come from the list widget itself (KI-262 family). */
		const NWidgetBase *const list_widget = this->GetWidget<NWidgetBase>(WID_SCHED_AS_LIST);
		const int step = (list_widget != nullptr && list_widget->resize_y > 0) ? static_cast<int>(list_widget->resize_y) : (GetCharacterHeight(FontSize::Normal) + 2);
		const bool rtl = _current_text_dir == TD_RTL;
		const int image_height = ScaleGUITrad(GetVehicleHeight(VehicleType::Train));

		int y = ir.top;
		for (int i = this->scroll->GetPosition(); i < static_cast<int>(this->rows.size()) && this->scroll->IsVisible(i); i++, y += step) {
			const Train *const t = Train::GetIfValid(this->rows[i]);
			if (t == nullptr) continue;

			const Rect row = ir.WithY(y, y + step - 1);

			/* Draw the train image over at most half of the row; the width is the sum of
			 * the cars which still fit, so the thumbnail stops inside this train. */
			const int thumb_limit = std::max(0, ir.Width() / 2);
			int thumb_width = 0;
			for (const Train *u = t; u != nullptr && thumb_width < thumb_limit; u = u->Next()) {
				Point offset;
				thumb_width += u->GetDisplayImageWidth(&offset);
			}
			thumb_width = std::min(thumb_width, thumb_limit);
			const Rect thumb = row.WithWidth(thumb_width, rtl).WithHeight(image_height, false).Translate(0, (step - image_height) / 2);
			DrawVehicleImage(t, thumb, VehicleID::Invalid(), EngineImageType::InList, 0);

			const Rect tr = row.Indent(thumb_width + WidgetDimensions::scaled.hsep_normal, rtl);
			const std::string text = GetString(STR_R3R_SCHEDULE_ASSIGN_ROW,
					GetString(STR_VEHICLE_NAME, t->index.base()), R3RScheduleLabelOf(t->orders));
			DrawString(tr, text, TextColour::White);
		}
	}

	void OnResize() override
	{
		this->scroll->SetCapacityFromWidget(this, WID_SCHED_AS_LIST, WidgetDimensions::scaled.framerect.Vertical());
	}

	void OnPaint() override
	{
		this->DrawWidgets();
	}

	void OnInvalidateData(int data = 0, bool gui_scope = true) override
	{
		if (!gui_scope) return;
		this->Rebuild();
		this->SetDirty();
	}

	void OnClick(Point pt, WidgetID widget, int click_count) override
	{
		if (widget != WID_SCHED_AS_LIST) return;

		const size_t sel = this->scroll->GetScrolledRowFromWidget(pt.y, this, WID_SCHED_AS_LIST, WidgetDimensions::scaled.framerect.top);
		if (sel == Scrollbar::npos || sel >= this->rows.size()) return;

		const Vehicle *const v = Vehicle::GetIfValid(this->rows[sel]);
		const OrderList *const ol = OrderList::GetIfValid(this->schedule);
		if (v == nullptr || ol == nullptr) {
			this->Close();
			return;
		}
		/* Commands authorise against the local company; a picker opened for another
		 * company can be read but not used (same rule as the pool window). */
		if (this->company != _local_company) return;

		Command<Commands::AssignR3RSchedule>::Post(STR_ERROR_CAN_T_DO_THIS, v->index.base(), ol->index.base());
		this->Close();
	}

	std::string GetWidgetString(WidgetID widget, StringID stringid) const override
	{
		if (widget == WID_SCHED_AS_CAPTION) {
			return GetString(stringid, R3RScheduleLabelOf(OrderList::GetIfValid(this->schedule)));
		}
		return this->Window::GetWidgetString(widget, stringid);
	}
};

static WindowDesc _r3r_schedule_assign_desc(__FILE__, __LINE__,
	WindowPosition::Center, "r3r_schedule_assign", 320, 170,
	WindowClass::R3RSchedule, WindowClass::None,
	{}, _nested_r3r_schedule_assign_widgets
);

/**
 * R3R (第 419 轮): open (or re-target and bring to the front) the train picker.
 *
 * The picker lives in the pool's window class but under window number 1, so the
 * singleton lookup of the pool window (number 0) never finds it.
 */
static void ShowR3RScheduleAssignWindow(CompanyID company, OrderListID schedule)
{
	if (!Company::IsValidID(company)) company = _local_company;
	if (!OrderList::IsValidID(schedule)) return;

	R3RScheduleAssignWindow *w = static_cast<R3RScheduleAssignWindow *>(FindWindowById(WindowClass::R3RSchedule, 1));
	if (w == nullptr) {
		new R3RScheduleAssignWindow(_r3r_schedule_assign_desc, company, schedule);
		return;
	}
	w->SetSchedule(schedule);
	BringWindowToFrontById(WindowClass::R3RSchedule, 1);
}
