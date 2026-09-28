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
#include "dropdown_func.h"
#include "dropdown_type.h"
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
				/* R3R (第 147 轮 / 需求叁改续): 「设为某分组的子分组」下拉框已删除 ——
				 * 改父组一律靠拖动（见 OnMouseDrag()/OnDragDrop()）：拖到某一行上 =
				 * 挂到它下面，拖到列表空白处/窗口其它地方 = 回到顶层。与普通列车分组
				 * 窗口的操作方式保持一致。 */
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

	std::vector<CoupleGroupID> groups{};  ///< Groups shown in the left pane, in parent/child tree order.
	std::vector<uint> group_segments{};   ///< Number of segments per entry of #groups.
	std::vector<uint8_t> group_depth{};   ///< Depth inside the parent tree per entry of #groups.
	std::vector<uint16_t> group_level_mask{}; ///< Tree lines to draw per entry of #groups.
	std::vector<uint8_t> group_has_children{}; ///< Whether the entry of #groups has children (foldable).
	std::vector<VehicleID> segments{};    ///< Segment heads of the selected group (right pane).
	std::vector<uint> segment_cars{};     ///< Number of vehicles per entry of #segments.

	/**
	 * R3R (第 145 轮 / 需求叁改): 折叠起来的挂接分组 —— 整棵子树都不列出来，和普通列车
	 * 分组(gui 的 GroupFoldBits)一致。只是窗口内的显示状态，不进存档。
	 * 用 vector 而不是 set：组数很少，且本文件没有引入 <set>。
	 */
	std::vector<CoupleGroupID> folded_groups{};
	/// 正在被拖动的分组（拖动改父组），INVALID_COUPLE_GROUP 表示没有拖动。
	CoupleGroupID drag_group = INVALID_COUPLE_GROUP;
	/// 拖动中悬停到的目标分组（=即将成为其子分组），INVALID_COUPLE_GROUP 表示没有。
	CoupleGroupID group_drop_target = INVALID_COUPLE_GROUP;

	int selected_group = -1;   ///< Selected index into #groups, or -1 if there is no selection.
	int selected_segment = -1; ///< Selected index into #segments, or -1 if there is no selection.

	/**
	 * R3R (第 146 轮 / 需求叁改): the player deliberately deselected the group (clicking the
	 * selected row once more), so RebuildGroups() must not auto-select the first row again.
	 */
	bool no_auto_select = false;

	bool query_is_rename = false; ///< Whether the pending query string renames instead of creates.
	/**
	 * R3R (第 146 轮 / 需求叁改): the group a "new group" query will create a *sub* group of.
	 * Captured when the query is opened (the selection may change while it is open);
	 * #INVALID_COUPLE_GROUP means "no group was selected, create a top level group".
	 */
	CoupleGroupID pending_new_parent = INVALID_COUPLE_GROUP;
	/**
	 * R3R (第 146 轮): select this group on the next rebuild (set after a "new group" query).
	 * By id, not by index: the list is ordered by the parent tree, so the row depends on
	 * where the new group ends up. The pool reuses freed slots, so the newest group is not
	 * necessarily the one with the highest id -- the caller diffs the pool instead.
	 */
	CoupleGroupID pending_select_group = INVALID_COUPLE_GROUP;
	/**
	 * R3R (第 145 轮 / 需求叁改): "assign segment" picking mode is active.
	 *
	 * Not derived from the engine's _thd any more: selecting a group in the list
	 * also arms a drag/drop (HT_DRAG) with this window as callback, so
	 * _thd.GetCallbackWnd() == this alone can no longer tell the two apart --
	 * exactly the trap KI-59 fell into (the picking mode could then never be
	 * entered because the button always looked "already picking").
	 */
	bool segment_picking = false; ///< Whether this window is picking a segment.

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
				 * as soon as this group is one of them.
				 * R3R (第 146 轮 / 需求叁改): 用有效集合 —— 子组的段在父组下面也要列出来
				 * （"石头里面的列车也自动就是滚木里面的列车"）。 */
				if ((R3RGetEffectiveCoupleGroupsOfSegment(t) & bit) == COUPLE_GROUP_MASK_NONE) continue;
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
		/* R3R (第 145 轮 / 需求叁改): 这个用 push_back 累积，必须清干净 ——
		 * 否则上一次的行数更多时 resize() 会把新写进去的值截掉、留下旧值。 */
		this->group_has_children.clear();
		uint iterated = 0;
		uint visible = 0;
		std::vector<CoupleGroupID> visible_groups;
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
			visible_groups.push_back(cg->index);
		}
		std::sort(visible_groups.begin(), visible_groups.end());

		/* R3R (第 144 轮 / 需求叁): 按父子层级做深度优先排序 —— 子组紧跟在它的父组
		 * 之后（父组内部再按 id 排序）。下拉列表 order_gui.cpp 用的是同一口径，因此
		 * 两处列出的顺序一致，缩进也一致。用显式栈而不是递归：旧档里可能残留父子环，
		 * 递归会直接爆栈。 */
		this->group_depth.assign(visible_groups.size(), 0);
		std::vector<std::pair<CoupleGroupID, uint>> stack;
		const auto visible_contains = [&visible_groups](CoupleGroupID id) {
			return std::binary_search(visible_groups.begin(), visible_groups.end(), id);
		};
		const auto is_folded = [this](CoupleGroupID id) {
			return std::find(this->folded_groups.begin(), this->folded_groups.end(), id) != this->folded_groups.end();
		};
		const auto push_children = [&](CoupleGroupID parent, uint depth) {
			/* 逆序压栈 → 弹出即按 id 正序。 */
			for (auto it = visible_groups.rbegin(); it != visible_groups.rend(); ++it) {
				if (R3RGetCoupleGroupParent(*it) == parent) stack.emplace_back(*it, depth);
			}
		};
		/* 根 = 没有父组，或父组不在本窗口可见范围里；逆序压栈 → 弹出即正序。 */
		for (auto it = visible_groups.rbegin(); it != visible_groups.rend(); ++it) {
			const CoupleGroupID parent = R3RGetCoupleGroupParent(*it);
			if (parent == INVALID_COUPLE_GROUP || !visible_contains(parent)) stack.emplace_back(*it, 0);
		}
		while (!stack.empty()) {
			const CoupleGroupID id = stack.back().first;
			const uint depth = stack.back().second;
			stack.pop_back();
			if (std::find(this->groups.begin(), this->groups.end(), id) != this->groups.end()) continue; // 环保护
			bool has_children = false;
			for (const CoupleGroupID cand : visible_groups) {
				if (R3RGetCoupleGroupParent(cand) == id) { has_children = true; break; }
			}
			this->groups.push_back(id);
			this->group_segments.push_back(R3RCountSegmentsInCoupleGroup(id));
			this->group_depth[this->groups.size() - 1] = static_cast<uint8_t>(std::min<uint>(depth, 8));
			this->group_has_children.push_back(has_children ? 1 : 0);
			/* 折叠的父组：整棵子树都不列出来（和 group_gui 的 GuiGroupListAddChildren 一致）。 */
			if (has_children && !is_folded(id)) push_children(id, depth + 1);
		}
		this->group_depth.resize(this->groups.size());
		this->group_has_children.resize(this->groups.size(), 0);
		/* 兜底：只出现在环里的组（深度优先走不到）仍要列出来，绝不静默丢组。 */
		for (const CoupleGroupID id : visible_groups) {
			if (std::find(this->groups.begin(), this->groups.end(), id) == this->groups.end()) {
				this->groups.push_back(id);
				this->group_segments.push_back(R3RCountSegmentsInCoupleGroup(id));
				this->group_depth[this->groups.size() - 1] = 0;
				this->group_has_children.push_back(0);
			}
		}
		/* R3R: 树线 —— 与 group_gui.cpp 的 level_mask 同口径：从下往上逆推，每对相邻条目
		 * 只需要关心「较后那个的层级」那一位。bit l 置位 = 第 l 级缩进的竖线要贯穿整行。 */
		this->group_level_mask.assign(this->groups.size(), 0);
		uint16_t level_mask = 0;
		for (size_t i = this->groups.size(); i-- > 1; ) {
			const uint cur = this->group_depth[i];
			if (cur < 16) {
				if (cur <= this->group_depth[i - 1]) {
					level_mask |= static_cast<uint16_t>(1u << cur);
				} else {
					level_mask &= static_cast<uint16_t>(~(1u << cur));
				}
			}
			this->group_level_mask[i - 1] = level_mask;
		}
		this->group_scroll->SetCount(this->groups.size());

		this->selected_group = -1;
		if (old != INVALID_COUPLE_GROUP) {
			auto it = std::find(this->groups.begin(), this->groups.end(), old);
			if (it != this->groups.end()) this->selected_group = static_cast<int>(std::distance(this->groups.begin(), it));
		}
		/* A freshly created group is selected right away: the user almost always
		 * wants to fill it next. (The list is ordered by the parent tree, so it is
		 * *not* necessarily the last entry -- hence the id is remembered exactly.
		 * R3R 第 146 轮: 原来取"id 最大的那个"，但分组池会复用空出来的下标，删掉一个
		 * 分组再新建就不一定是最大 id 了，会选错行。) */
		if (this->pending_select_group != INVALID_COUPLE_GROUP) {
			auto it = std::find(this->groups.begin(), this->groups.end(), this->pending_select_group);
			this->pending_select_group = INVALID_COUPLE_GROUP;
			if (it != this->groups.end()) {
				this->selected_group = static_cast<int>(std::distance(this->groups.begin(), it));
				this->no_auto_select = false;
			}
		}
		/* R3R (第 146 轮 / 需求叁改): the player may deliberately deselect a group
		 * (clicking the selected row again). Do not silently select the first row
		 * again then, or "deselect" would be impossible. */
		if (this->selected_group < 0 && !this->groups.empty() && !this->no_auto_select) this->selected_group = 0;
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

	/** R3R (第 145 轮 / 需求叁改): 树的一级缩进宽度 —— 树线与折叠三角共用同一网格。 */
	static int TreeStep()
	{
		return std::max<int>(GetScaledSpriteSize(SPR_CIRCLE_FOLDED).width, static_cast<int>(WidgetDimensions::scaled.hsep_indent));
	}

	/** R3R (第 145 轮 / 需求叁改): 第 \a i 行所属节点（树线交点、折叠三角）的 x 坐标。 */
	int GetNodeX(const Rect &r, int i) const
	{
		const bool rtl = _current_text_dir == TD_RTL;
		const Rect ir = r.Shrink(WidgetDimensions::scaled.framerect);
		const int step = TreeStep();
		const int depth = static_cast<int>(this->group_depth[i]);
		return rtl ? ir.right - step / 2 - depth * step : ir.left + step / 2 + depth * step;
	}

	/** R3R (第 145 轮 / 需求叁改): 鼠标所在的列表行，-1 表示不在任何行上。 */
	int GetGroupRowFromPoint(Point pt) const
	{
		const auto sel = this->group_scroll->GetScrolledRowFromWidget(pt.y, this, WID_CG_GROUPS, WidgetDimensions::scaled.framerect.top);
		if (sel == INT_MAX || sel >= static_cast<int>(this->groups.size())) return -1;
		return static_cast<int>(sel);
	}

	/** R3R (第 145 轮 / 需求叁改): 该分组当前是否被折叠。 */
	bool IsGroupFolded(CoupleGroupID id) const
	{
		return std::find(this->folded_groups.begin(), this->folded_groups.end(), id) != this->folded_groups.end();
	}

	/**
	 * R3R (第 145 轮 / 需求叁改): 折叠/展开一个分组 —— 只改窗口内的显示状态，
	 * 整棵子树都不再列出来（和 group_gui 的 GroupFoldBits::GroupView 一致）。
	 */
	void ToggleGroupFold(CoupleGroupID id)
	{
		auto it = std::find(this->folded_groups.begin(), this->folded_groups.end(), id);
		if (it == this->folded_groups.end()) {
			this->folded_groups.push_back(id);
		} else {
			this->folded_groups.erase(it);
		}
		this->RebuildGroups();
	}

	/**
	 * R3R (KI-90): the company whose groups this window lists.
	 *
	 * Needed by ShowCoupleGroupWindow(): the window is a singleton, so it has to
	 * be able to tell whether an already open window is already showing the
	 * requested company or has to be re-targeted.
	 */
	Owner GetCompany() const { return this->company; }

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

	/* R3R (第 147 轮 / 需求叁改续): 原来这里有一组「父分组下拉框」的辅助函数
	 * （GetSelectedParentGroup / ParentGroupLabel / GetWidgetString / SetParentOfSelected）。
	 * 下拉框已删除，改父组只能靠拖动，所以这些全都不再需要 —— 拖动侧只需要
	 * R3RGetCoupleGroupParent()（高亮判据）与 R3RCanCoupleGroupHaveParent()（防成环）。 */

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
			const bool rtl = _current_text_dir == TD_RTL;
			const int step = TreeStep();
			const PixelColour linecolour = GetColourGradient(Colours::Orange, Shade::Normal);
			int y = ir.top;
			for (int i = this->group_scroll->GetPosition(); i < static_cast<int>(this->groups.size()) && this->group_scroll->IsVisible(i); i++, y += this->resize.step_height) {
				const int bottom = y + this->resize.step_height - 1;
				if (i == this->selected_group) GfxFillRect(r.left + 1, y, r.right, bottom, PC_DARK_GREY);
				if (!CoupleGroup::IsValidID(this->groups[i])) continue;
				const CoupleGroup *cg = CoupleGroup::Get(this->groups[i]);
				if (cg == nullptr) continue;
				/* R3R (第 145 轮 / 需求叁改): 拖动分组时高亮鼠标所在的那一行 —— 松开就会把
				 * 拖动中的分组挂到它下面（见 OnDragDrop()）。 */
				if (this->groups[i] == this->group_drop_target) {
					GfxFillRect(Rect(r.left + 1, y, r.right, bottom).Shrink(WidgetDimensions::scaled.bevel), GetColourGradient(Colours::Grey, Shade::Lightest));
				}

				/* R3R (第 145 轮 / 需求叁改): 树线 —— 与 group_gui 的 level_mask 同口径（见
				 * RebuildGroups()），但把「某一级是否还要继续往下画」换算成像素列，好和
				 * 折叠三角对齐。bit l 对应从最左边算起第 l 级祖先所在的列。 */
				const int depth = static_cast<int>(this->group_depth[i]);
				const int node_x = this->GetNodeX(r, i);
				const uint16_t mask = this->group_level_mask[i];
				for (int lvl = 0; lvl <= depth; lvl++) {
					if (!HasBit(mask, lvl)) continue;
					const int x = node_x + (rtl ? 1 : -1) * (depth - lvl) * step;
					GfxDrawLine(x, y, x, bottom, linecolour, WidgetDimensions::scaled.fullbevel.top);
				}
				const int ycentre = (y + bottom) / 2;
				if (!HasBit(mask, depth)) GfxDrawLine(node_x, y, node_x, ycentre, linecolour, WidgetDimensions::scaled.fullbevel.top);

				/* 折叠三角：只有真的有子组才画，点了它整棵子树收起来。 */
				if (this->group_has_children[i] != 0) {
					const Dimension d = GetScaledSpriteSize(SPR_CIRCLE_FOLDED);
					const Rect fr(node_x - d.width / 2, y, node_x + d.width / 2, bottom);
					DrawSpriteIgnorePadding(this->IsGroupFolded(this->groups[i]) ? SPR_CIRCLE_FOLDED : SPR_CIRCLE_UNFOLDED, PAL_NONE, fr, SA_CENTER);
				} else {
					GfxDrawLine(node_x, ycentre, node_x + (rtl ? -1 : 1) * (step / 2), ycentre, linecolour, WidgetDimensions::scaled.fullbevel.top);
				}

				/* R3R (第 144/145 轮): 按层级做像素缩进，与上面的树线同一网格。 */
				std::string text = GetString(STR_COUPLE_GROUP_LIST_ITEM, cg->name, this->group_segments[i]);
				/* R3R (D6-③): a group of another company only shows up here
				 * because its owner opened it for us, so mark it instead of
				 * letting it look like one of our own (its buttons are
				 * restricted, too). */
				if (cg->owner != this->company) text.append(GetString(STR_COUPLE_GROUP_LIST_SHARED));
				Rect tr = ir.Indent((depth + 1) * step, rtl);
				tr.top = y;
				tr.bottom = bottom;
				DrawString(tr, text);
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
				 * multi-group segment is not mistaken for a single-group one.
				 * R3R (第 146 轮 / 需求叁改): 用有效集合 —— 通过父组继承来的成员关系也
				 * 要显示出来（在 滚木 下面看 石头 的段就是"+石头"）。 */
				const CoupleGroupMask others = R3RGetEffectiveCoupleGroupsOfSegment(t) & ~R3RCoupleGroupBit(this->GetSelectedGroup());
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
		/* R3R (KI-90): this window can be opened for a company which is not the one
		 * currently playing -- the depot window passes its own owner through, and a
		 * shared depot hands over the owner of the depot tile. Every command
		 * authorises against _current_company, so a control which looks enabled in
		 * another company's window fails silently when clicked (exactly the report
		 * behind this issue: "the buttons should be grey"). Editing therefore
		 * follows the local company only, while the lists stay readable so another
		 * company's groups can still be inspected. */
		const bool can_edit = (this->company == _local_company);
		const bool can_manage = can_edit && this->IsSelectedGroupManageable();
		/* R3R (D6-③): assigning and removing our own segments only needs the
		 * weaker membership right, so a group which another company shared with
		 * us can be used without giving us power over the group itself. */
		const bool can_join = can_edit && this->IsSelectedGroupJoinable();
		/* R3R (KI-55): "new group" needs a usable company. this->company holds the
		 * company this window was opened for (with the local company as fallback,
		 * see the constructor), and KI-90 additionally restricts it to the local
		 * company: creating a group while looking at somebody else's window would
		 * file it under our own name and it would not show up in the list. */
		const bool can_create = can_edit && Company::IsValidID(this->company);
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
		this->SetWidgetLoweredState(WID_CG_ADD_SEGMENT, this->segment_picking);

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
				/* R3R (KI-56): clicking the empty area below the list keeps the current
				 * selection instead of clearing it.  Clearing it would disable every
				 * management button -- including "assign segment" -- so a stray click
				 * next to the list made the buttons turn grey (and look broken). */
				const int sel = this->GetGroupRowFromPoint(pt);
				if (sel < 0) break;

				/* R3R (第 145 轮 / 需求叁改): 点折叠三角 —— 整棵子树收起来/展开。 */
				if (this->group_has_children[sel] != 0) {
					const Rect r = this->GetWidget<NWidgetCore>(WID_CG_GROUPS)->GetCurrentRect();
					const int node_x = this->GetNodeX(r, sel);
					const int half = std::max<int>(GetScaledSpriteSize(SPR_CIRCLE_FOLDED).width, TreeStep()) / 2 + 2;
					if (pt.x >= node_x - half && pt.x <= node_x + half) {
						this->ToggleGroupFold(this->groups[sel]);
						break;
					}
				}

				if (sel == this->selected_group) {
					/* R3R (第 146 轮 / 需求叁改): 再点一次已经选中的那一行 = 取消选中。
					 * 这是"新建时不想要子分组"的入口 —— 选中 滚木 之后点新建会建出它的
					 * 子分组，要建顶层分组就先把自己取消选中。 */
					this->selected_group = -1;
					this->no_auto_select = true;
					this->RebuildSegments();
					this->SetDirty();
				} else {
					this->selected_group = sel;
					this->no_auto_select = false;
					this->RebuildSegments();
					this->SetDirty();
				}
				/* R3R (第 145 轮 / 需求叁改): 选中即开始一次「拖动改父组」。
				 * 松手时由 OnDragDrop() 决定是不是真的改了父组；单纯的点选（鼠标没动）
				 * 会被丢掉，因为目标是分组自己。正在挑车底时不动这一套，免得换模式。
				 * 取消选中也照常武装：否则"把当前选中的分组拖到别处"需要先点两下。 */
				if (!this->segment_picking) {
					this->drag_group = this->groups[sel];
					this->group_drop_target = INVALID_COUPLE_GROUP;
					SetObjectToPlaceWnd(SPR_CURSOR_MOUSE, PAL_NONE, HT_DRAG, this);
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
				/* R3R (第 146 轮 / 需求叁改): 选中的分组就是新分组的父组 —— 在列表里挑好
				 * "滚木"再点新建，建出来的"石头"直接挂到它下面，不用再改一次父组。
				 * 父组在这一刻定下来（输入框开着的时候选择还可能变）。 */
				this->pending_new_parent = this->GetSelectedGroup();
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
				 * active mode must NOT be read from the widget's lowered flag:
				 * DispatchLeftClickEvent() calls HandleButtonClick() -- which does
				 * LowerWidget() -- *before* it calls OnClick(), so IsWidgetLowered()
				 * is unconditionally true in here.  A toggle based on it therefore
				 * always took the "cancel" branch and the picking mode could never
				 * be entered at all -- that was the "assign segment does nothing"
				 * bug.  (第 145 轮: the flag moved from _thd to this->segment_picking,
				 * see its declaration -- list clicks now also arm HT_DRAG.) */
				R3RDbgWrite("[R3R] CG-CLICK-ADD manage=%d groups=%u sel=%d picking=%d\n",
						(int)this->IsSelectedGroupManageable(), (unsigned)this->groups.size(),
						this->selected_group, (int)this->segment_picking);
				if (!this->IsSelectedGroupJoinable()) break;
				if (this->segment_picking) {
					/* Already picking for this window: cancel the mode. */
					this->RaiseWidget(WID_CG_ADD_SEGMENT);
					this->SetWidgetDirty(WID_CG_ADD_SEGMENT);
					ResetObjectToPlace();
					break;
				}
				this->segment_picking = true;
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
		 * again when the click timeout expires), so test our own picking flag. */
		if (!this->segment_picking) return false;

		R3RDbgWrite("[R3R] CG-PICK veh=%d\n", (int)(v == nullptr ? -1 : (int)v->index.base()));

		this->segment_picking = false;
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

	/**
	 * R3R (第 145 轮 / 需求叁改): a group is dragged over the list
	 * (or over anything else in this window).
	 */
	void OnMouseDrag(Point pt, WidgetID widget) override
	{
		if (this->drag_group == INVALID_COUPLE_GROUP) return;

		CoupleGroupID over = INVALID_COUPLE_GROUP;
		if (widget == WID_CG_GROUPS) {
			const int row = this->GetGroupRowFromPoint(pt);
			if (row >= 0) over = this->groups[row];
		}
		/* Dropping a group onto itself, onto its current parent or onto one of
		 * its own descendants would change nothing / build a cycle; the command
		 * rejects the latter, so never highlight it. */
		if (over != INVALID_COUPLE_GROUP &&
				(over == this->drag_group || over == R3RGetCoupleGroupParent(this->drag_group) ||
				 !R3RCanCoupleGroupHaveParent(this->drag_group, over))) {
			over = INVALID_COUPLE_GROUP;
		}

		if (over != this->group_drop_target) {
			this->group_drop_target = over;
			this->SetDirty();
		}
	}

	/** R3R (第 145 轮 / 需求叁改): finish the drag -- hang the group below the hovered one. */
	void OnDragDrop(Point pt, WidgetID widget) override
	{
		if (this->drag_group == INVALID_COUPLE_GROUP) return;

		const CoupleGroupID group = this->drag_group;
		this->drag_group = INVALID_COUPLE_GROUP;
		this->group_drop_target = INVALID_COUPLE_GROUP;
		this->SetDirty();

		/* Dropping on a row hangs the group below it; dropping below the last
		 * row (or anywhere outside the list) makes it a top level group again,
		 * like dropping a train group onto "all vehicles" does. The row is read
		 * from the release position again -- group_drop_target only exists to
		 * highlight the row while dragging. */
		CoupleGroupID new_parent = INVALID_COUPLE_GROUP;
		if (widget == WID_CG_GROUPS) {
			const int row = this->GetGroupRowFromPoint(pt);
			if (row >= 0) new_parent = this->groups[row];
		}

		if (new_parent == group || new_parent == R3RGetCoupleGroupParent(group)) return;
		if (new_parent != INVALID_COUPLE_GROUP && !R3RCanCoupleGroupHaveParent(group, new_parent)) return;
		Command<Commands::SetCoupleGroupParent>::Post(STR_ERROR_CAN_T_DO_THIS, group, new_parent);
	}

	void OnPlaceObjectAbort() override
	{
		/* R3R (KI-59): the engine calls this when the picking mode ends (world
		 * click, ESC, or another window taking over), so always restore the button
		 * regardless of its lowered state.
		 * R3R (第 145 轮 / 需求叁改): the same callback ends a group drag/drop. */
		this->segment_picking = false;
		this->drag_group = INVALID_COUPLE_GROUP;
		this->group_drop_target = INVALID_COUPLE_GROUP;
		this->RaiseWidget(WID_CG_ADD_SEGMENT);
		this->SetWidgetDirty(WID_CG_ADD_SEGMENT);
		this->SetDirty();
	}

	void OnQueryTextFinished(std::optional<std::string> str) override
	{
		if (!str.has_value() || str->empty()) return;
		if (this->query_is_rename) {
			const CoupleGroupID id = this->GetSelectedGroup();
			if (id == INVALID_COUPLE_GROUP) return;
			Command<Commands::RenameCoupleGroup>::Post(STR_ERROR_CAN_T_DO_THIS, id, *str);
		} else {
			/* R3R (第 146 轮 / 需求叁改): 新分组直接建在"打开输入框时选中的那个分组"下面
			 * （见 WID_CG_NEW）。建完立刻把它选上：父组树展开在它上面，用户接着就能往
			 * 里面放段。 */
			const CoupleGroupID parent = this->pending_new_parent;
			this->pending_new_parent = INVALID_COUPLE_GROUP;

			/* 认出新分组用"池子的差集"：分组池会复用空出来的下标，"id 最大"并不一定
			 * 就是刚建的那个；命令被拒绝时差集是空的，也就不会乱选一行。 */
			std::vector<CoupleGroupID> before;
			for (const CoupleGroup *cg : CoupleGroup::Iterate()) before.push_back(cg->index);

			Command<Commands::CreateCoupleGroup>::Post(STR_ERROR_CAN_T_DO_THIS, *str, parent);

			for (const CoupleGroup *cg : CoupleGroup::Iterate()) {
				if (std::find(before.begin(), before.end(), cg->index) != before.end()) continue;
				this->pending_select_group = cg->index;
				this->no_auto_select = false;
				break;
			}
			this->RebuildGroups();
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

	/* R3R (KI-90): the window is a singleton (id 0), but it is *not*
	 * interchangeable between companies -- it only ever lists the groups of one
	 * company. Simply bringing an already open window to the front would show
	 * company A's groups while the player asked for company B's, so an existing
	 * window is re-targeted instead. Note that a depot tile may be owned by
	 * something which is not a company; such a request is normalised to the local
	 * company here, exactly like CoupleGroupWindow's constructor does. */
	if (!Company::IsValidID(company)) company = _local_company;
	Window *existing = BringWindowToFrontById(WindowClass::CoupleGroup, 0);
	if (existing != nullptr) {
		if (static_cast<CoupleGroupWindow *>(existing)->GetCompany() == company) return;
		CloseWindowById(WindowClass::CoupleGroup, 0);
	}
	new CoupleGroupWindow(_couple_group_desc, company);
}
