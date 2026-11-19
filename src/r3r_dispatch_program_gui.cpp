/*
 * This file is part of OpenTTD.
 * OpenTTD is free software; you can redistribute it and/or modify it under the terms of the GNU General Public License as published by the Free Software Foundation, version 2.
 * OpenTTD is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 * See the GNU General Public License for more details. You should have received a copy of the GNU General Public License along with OpenTTD. If not, see <http://www.gnu.org/licenses/>.
 */

/** @file r3r_dispatch_program_gui.cpp R3R (第 451 轮续): 调度池「分配规则」程序编辑窗口。
 *  架构照抄可编程信号机的条件编辑界面：一个指令行列表 + 底部「变量/比较符/阈值/结果」
 *  控件，点列表选中一行、改动只作用在选中行；区别是结果换成「允许/拒绝分配调度 X」，
 *  程序挂在场（yard）上而不是挂在某格信号的某 track 上。
 */

#include "stdafx.h"
#include "r3r_dispatch_program_gui.h"

#include "command_func.h"
#include "core/string_consumer.hpp"
#include "dropdown_func.h"
#include "gfx_func.h"
#include "order_base.h"
#include "querystring_gui.h"
#include "station_base.h"
#include "station_cmd.h"
#include "strings_func.h"
#include "vehicle_base.h"
#include "window_func.h"
#include "widgets/station_widget.h"

#include "table/strings.h"

#include "safeguards.h"

/** 变量枚举 → 显示名（下标必须与 R3RDispatchRuleVariable 同序）。 */
static const StringID _dispatch_var_strings[] = {
	STR_R3R_DP_VAR_NONE, STR_R3R_DP_VAR_SPEED, STR_R3R_DP_VAR_POWER, STR_R3R_DP_VAR_TE,
	STR_R3R_DP_VAR_RELIABILITY, STR_R3R_DP_VAR_POOL_ISSUED, STR_R3R_DP_VAR_POOL_REMAINING,
	STR_R3R_DP_VAR_NUM_GREEN, STR_R3R_DP_VAR_NUM_RED, STR_R3R_DP_VAR_SIGNAL_STATE, STR_R3R_DP_VAR_COUNTER,
	STR_R3R_DP_VAR_ENGINE_CLASS,
};
static_assert(lengthof(_dispatch_var_strings) == static_cast<size_t>(R3RDispatchRuleVariable::EngineClass) + 1);

/**
 * R3R (第 455 轮, KI-652): 下拉里**真正给玩家选**的变量。
 *
 * 三个「恒 0」的信号条件（前方绿灯数 / 红灯数 / 另一信号的状态）按玩家口径
 * 「能抄的抄、不能抄的别硬抄」从这里剔除 —— 领表上下文里没有信号格可看，留着只会
 * 让规则悄悄永远不成立。它们的枚举值仍保留（见 station_base.h），所以存档编号不动，
 * 旧档里的这类条件在求值时按「不设限」处理。
 */
static const R3RDispatchRuleVariable _dispatch_var_selectable[] = {
	R3RDispatchRuleVariable::None,
	R3RDispatchRuleVariable::MaxSpeed,
	R3RDispatchRuleVariable::Power,
	R3RDispatchRuleVariable::MaxTractiveEffort,
	R3RDispatchRuleVariable::Reliability,
	R3RDispatchRuleVariable::PoolIssued,
	R3RDispatchRuleVariable::PoolRemaining,
	R3RDispatchRuleVariable::Counter,
	R3RDispatchRuleVariable::EngineClass,
};
static const StringID _dispatch_var_selectable_strings[] = {
	STR_R3R_DP_VAR_NONE, STR_R3R_DP_VAR_SPEED, STR_R3R_DP_VAR_POWER, STR_R3R_DP_VAR_TE,
	STR_R3R_DP_VAR_RELIABILITY, STR_R3R_DP_VAR_POOL_ISSUED, STR_R3R_DP_VAR_POOL_REMAINING,
	STR_R3R_DP_VAR_COUNTER, STR_R3R_DP_VAR_ENGINE_CLASS,
};
static_assert(lengthof(_dispatch_var_selectable) == lengthof(_dispatch_var_selectable_strings));

/** 枚举 → 可选列表序号（旧档里已停用的变量落到「不设条件」）。 */
static int DispatchVarSelectIndex(R3RDispatchRuleVariable var)
{
	for (size_t i = 0; i < lengthof(_dispatch_var_selectable); i++) {
		if (_dispatch_var_selectable[i] == var) return static_cast<int>(i);
	}
	return 0;
}

/** 可选列表序号 → 枚举。 */
static R3RDispatchRuleVariable DispatchVarFromSelectIndex(int index)
{
	if (index < 0 || static_cast<size_t>(index) >= lengthof(_dispatch_var_selectable)) {
		return R3RDispatchRuleVariable::None;
	}
	return _dispatch_var_selectable[index];
}

static StringID DispatchOpString(R3RDispatchRuleOp op);

/** R3R (第 455 轮, KI-652): 机车类型（EngineClass）的显示名。 */
static const StringID _dispatch_engine_class_strings[] = {
	STR_R3R_DP_CLASS_STEAM, STR_R3R_DP_CLASS_DIESEL, STR_R3R_DP_CLASS_ELECTRIC,
	STR_R3R_DP_CLASS_MONORAIL, STR_R3R_DP_CLASS_MAGLEV,
};

static std::string DispatchEngineClassLabel(uint32_t value)
{
	const size_t idx = std::min<size_t>(static_cast<size_t>(value), lengthof(_dispatch_engine_class_strings) - 1);
	return GetString(_dispatch_engine_class_strings[idx]);
}

/** 条件的比较符文案；机车类型是枚举，只给「是 / 不是」两档。 */
static std::string DispatchConditionOpLabel(const R3RDispatchInstruction &ins)
{
	if (ins.var == R3RDispatchRuleVariable::EngineClass) {
		return GetString(ins.op == R3RDispatchRuleOp::NotEqual ? STR_R3R_DP_OP_IS_NOT : STR_R3R_DP_OP_IS);
	}
	return GetString(DispatchOpString(ins.op));
}

static uint32_t DispatchValueStep(R3RDispatchRuleVariable var)
{
	switch (var) {
		case R3RDispatchRuleVariable::MaxSpeed:          return 10;
		case R3RDispatchRuleVariable::Power:             return 100;
		case R3RDispatchRuleVariable::MaxTractiveEffort: return 10;
		default:                                         return 1;
	}
}

static constexpr uint32_t R3R_DISPATCH_VALUE_MAX = 1000000;

static std::string DispatchVarLabel(R3RDispatchRuleVariable var, uint32_t extra)
{
	const size_t idx = std::min<size_t>(static_cast<size_t>(var), lengthof(_dispatch_var_strings) - 1);
	std::string label = GetString(_dispatch_var_strings[idx]);
	if (var == R3RDispatchRuleVariable::Counter) label += GetString(STR_R3R_DP_COUNTER_SUFFIX, extra);
	return label;
}

static StringID DispatchOpString(R3RDispatchRuleOp op)
{
	switch (op) {
		case R3RDispatchRuleOp::AtMost:   return STR_R3R_DP_OP_AT_MOST;
		case R3RDispatchRuleOp::Equal:    return STR_R3R_DP_OP_EQUAL;
		case R3RDispatchRuleOp::NotEqual: return STR_R3R_DP_OP_NOT_EQUAL;
		case R3RDispatchRuleOp::Greater:  return STR_R3R_DP_OP_GREATER;
		case R3RDispatchRuleOp::Less:     return STR_R3R_DP_OP_LESS;
		case R3RDispatchRuleOp::AtLeast:
		default:                          return STR_R3R_DP_OP_AT_LEAST;
	}
}

static std::string DispatchScheduleName(uint32_t schedule)
{
	if (schedule == Station::R3R_DISPATCH_NO_SCHEDULE) return GetString(STR_R3R_DP_NO_SCHEDULE);
	const OrderList *ol = OrderList::GetIfValid(OrderListID(schedule));
	if (ol == nullptr) return GetString(STR_R3R_DISPATCH_POOL_ENTRY_GONE);
	const std::string &name = ol->GetR3RScheduleName();
	if (name.empty()) return GetString(STR_R3R_DISPATCH_POOL_ENTRY_GONE);
	return name;
}

/** 场号 → 显示文字（整站 / 场 n / 玩家自定义名）。 */
static std::string DispatchYardLabel(const Station *st, uint16_t yard)
{
	if (yard == Station::R3R_YARD_NONE || st == nullptr || yard > st->R3RNumYards()) {
		return GetString(STR_R3R_DP_YARD_WHOLE);
	}
	const std::string &name = st->R3RGetYardName(yard);
	if (!name.empty()) return name;
	return GetString(STR_R3R_DP_YARD_INDEX, yard);
}

/** R3R (第 451 轮续二): 池条目指向的排程下标。
 *  与 station_cmd.cpp 的 R3RDispatchEntryScheduleIndex 同构（旧档条目没记录排程时
 *  回退到模板车名下的表），两边必须给出同一个序号，否则多选掩码的 bit 对不上。 */
static uint32_t DispatchPoolEntrySchedule(const Station::R3RStationYard::R3RDispatchPoolEntry &e)
{
	if (e.schedule != Station::R3R_DISPATCH_NO_SCHEDULE) return e.schedule;
	if (!Vehicle::IsValidID(e.host)) return Station::R3R_DISPATCH_NO_SCHEDULE;
	const Vehicle *const v = Vehicle::Get(e.host);
	if (v == nullptr || v->orders == nullptr) return Station::R3R_DISPATCH_NO_SCHEDULE;
	return v->orders->index.base();
}

/** R3R (第 451 轮续二): 多选掩码的位宽（bit i = 去重后第 i 份调度）。
 *  上限 32：一条指令只带一个 32 位 extra 字段，且本场池容量最多 256 条。 */
static constexpr uint R3R_DISPATCH_MASK_BITS = 32;

/** R3R (第 451 轮续二): 本场池里「去重后的调度序号」列表。
 *  下标与求值器里的 slot 一一对应（同一份排程登记在多条条目上只占一个 bit）。 */
static void DispatchCollectSchedules(const Station *st, uint16_t yard, std::vector<uint32_t> &out)
{
	out.clear();
	if (st == nullptr) return;
	const std::vector<Station::R3RStationYard::R3RDispatchPoolEntry> *const pool = st->R3RDispatchPoolRef(yard);
	if (pool == nullptr) return;
	for (const Station::R3RStationYard::R3RDispatchPoolEntry &e : *pool) {
		const uint32_t schedule = DispatchPoolEntrySchedule(e);
		if (schedule == Station::R3R_DISPATCH_NO_SCHEDULE) continue;
		if (std::find(out.begin(), out.end(), schedule) == out.end()) out.push_back(schedule);
	}
}

/** R3R (第 451 轮续二): 多选掩码 → 显示文字（逗号分隔的调度名；空掩码显示「无」）。 */
static std::string DispatchMaskLabel(const std::vector<uint32_t> &schedules, uint32_t mask)
{
	std::string text;
	for (uint i = 0; i < static_cast<uint>(schedules.size()) && i < R3R_DISPATCH_MASK_BITS; i++) {
		if (!HasBit(mask, i)) continue;
		if (!text.empty()) text += ", ";
		text += DispatchScheduleName(schedules[i]);
	}
	if (text.empty()) return GetString(STR_R3R_DP_NO_SCHEDULE);
	return text;
}

/** 一条指令渲染成一行文字（只拼接既有语言串，不新增 STR_*，以便走增量构建）。 */
static std::string DispatchInstructionLine(const R3RDispatchInstruction &ins, const std::vector<uint32_t> &schedules)
{
	switch (ins.kind) {
		case R3RDispatchInstructionKind::Else:  return GetString(STR_R3R_DP_INS_ELSE);
		case R3RDispatchInstructionKind::EndIf: return GetString(STR_R3R_DP_INS_ENDIF);
		case R3RDispatchInstructionKind::Result:
			return GetString((ins.result == R3RDispatchResultKind::Deny) ? STR_R3R_DP_INS_DENY : STR_R3R_DP_INS_ALLOW)
					+ " " + DispatchMaskLabel(schedules, ins.extra);
		default: break;
	}

	const std::string head = GetString((ins.kind == R3RDispatchInstructionKind::Or) ? STR_R3R_DP_INS_OR : STR_R3R_DP_INS_IF);
	/* R3R (第 455 轮, KI-652): 机车类型不显示成数字，也不显示比较符（见 DispatchConditionOpLabel）。 */
	const std::string value_text = (ins.var == R3RDispatchRuleVariable::EngineClass)
			? DispatchEngineClassLabel(ins.value)
			: GetString(STR_R3R_DP_VALUE, ins.value);
	return head + " " + DispatchVarLabel(ins.var, ins.extra) + " " + DispatchConditionOpLabel(ins)
			+ " " + value_text;
}

/** 窗口号 = 站号（低 16 位）| 场号（高 16 位）：一个站的每个场各开一个窗口。 */
static inline int PackDispatchProgramWindowNumber(StationID station_id, uint16_t yard)
{
	return static_cast<int>(static_cast<uint32_t>(station_id.base()) & 0xFFFF) | (static_cast<int>(yard) << 16);
}

struct DispatchProgramWindow;
static void ShowDispatchSchedulePicker(DispatchProgramWindow *parent);

/** R3R (第 451 轮续): 「分配规则」程序编辑窗口。 */
struct DispatchProgramWindow : Window {
	StationID station_id;                       ///< 编辑哪个站
	uint16_t yard;                              ///< 编辑哪个场（R3R_YARD_NONE = 整站那一份）
	std::vector<R3RDispatchInstruction> program; ///< 本地副本，每次改动整份下发命令
	int sel;                                    ///< 选中下标，-1 = 没选中
	int top;                                    ///< 列表第一行显示的下标

	DispatchProgramWindow(WindowDesc &desc, StationID station_id, uint16_t yard) : Window(desc),
			station_id(station_id), yard(yard), sel(-1), top(0)
	{
		this->CreateNestedTree();
		/* 窗口号要把场也编进去，否则同一个站的两个场会共用一个窗口。 */
		this->FinishInitNested(PackDispatchProgramWindowNumber(station_id, yard));
		this->Reload();
		this->UpdateBottomControls();
		this->SetDirty();
	}

	~DispatchProgramWindow()
	{
		/* 多选窗口没有父窗口类，主窗口关掉时必须顺手关掉它，否则留下孤儿窗口。 */
		CloseWindowById(WindowClass::DispatchSchedulePick, this->window_number, false);
	}

	void Reload()
	{
		this->program.clear();
		const Station *st = Station::GetIfValid(this->station_id);
		if (st != nullptr) {
			const std::vector<R3RDispatchInstruction> *const src = st->R3RDispatchProgramRef(this->yard);
			if (src != nullptr) this->program = *src;
		}
		if (this->sel >= static_cast<int>(this->program.size())) this->sel = static_cast<int>(this->program.size()) - 1;
		this->UpdateScroll();
	}

	R3RDispatchInstruction *Selected()
	{
		if (this->sel < 0 || this->sel >= static_cast<int>(this->program.size())) return nullptr;
		return &this->program[static_cast<size_t>(this->sel)];
	}

	int LineHeight() const { return GetCharacterHeight(FontSize::Normal) + 3; }

	int VisibleLines() const
	{
		const NWidgetBase *const list = this->GetWidget<NWidgetBase>(WID_DP_LIST);
		if (list == nullptr) return 1;
		return std::max(1, (static_cast<int>(list->current_y) - 4) / this->LineHeight());
	}

	void UpdateScroll()
	{
		const int lines = this->VisibleLines();
		const int count = static_cast<int>(this->program.size());
		this->top = Clamp(this->top, 0, std::max(0, count - lines));
		Scrollbar *sb = this->GetScrollbar(WID_DP_SCROLLBAR);
		sb->SetCount(std::max<uint32_t>(1, static_cast<uint32_t>(this->program.size())));
		sb->SetCapacity(lines);
		sb->SetPosition(this->top);
	}

	/** 整份下发命令；命令层会做结构校验，所以本地状态一律以命令执行后的站内数据为准。 */
	void Apply()
	{
		std::string blob;
		blob.reserve(this->program.size() * 24);
		for (const R3RDispatchInstruction &ins : this->program) {
			const uint32_t words[3] = {
				static_cast<uint32_t>(static_cast<uint8_t>(ins.kind)) |
						(static_cast<uint32_t>(static_cast<uint8_t>(ins.var)) << 8) |
						(static_cast<uint32_t>(static_cast<uint8_t>(ins.op)) << 16) |
						(static_cast<uint32_t>(static_cast<uint8_t>(ins.result)) << 24),
				ins.value,
				ins.extra,
			};
			for (int w = 0; w < 3; w++) {
				blob.push_back(static_cast<char>(words[w] & 0xFF));
				blob.push_back(static_cast<char>((words[w] >> 8) & 0xFF));
				blob.push_back(static_cast<char>((words[w] >> 16) & 0xFF));
				blob.push_back(static_cast<char>((words[w] >> 24) & 0xFF));
			}
			blob.append(12, '\0'); // 3 个保留字
		}
		/* 不在这里 Reload：命令是排队的，立刻读站内数据只会把刚做的改动冲掉。
		 * 命令执行后会 InvalidateWindowClassesData(WindowClass::DispatchProgram)，
		 * 由 OnInvalidateData() 统一回读。 */
		Command<Commands::SetR3RDispatchProgram>::Post(STR_ERROR_CAN_T_DO_THIS, this->station_id, this->yard, blob);
		this->SetDirty();
	}

	/** R3R (第 451 轮续二): 新建结果指令默认只勾选本场池里第一份有效调度。
	 *  extra 现在是多选位掩码（bit i = 去重后第 i 份调度），空池 ⇒ 0 = 谁都不匹配。 */
	uint32_t DefaultScheduleMask() const
	{
		std::vector<uint32_t> list;
		DispatchCollectSchedules(Station::GetIfValid(this->station_id), this->yard, list);
		return list.empty() ? 0 : 1u;
	}

	/** 追加一条指令，插在选中行之后（没选中就追加到末尾）。 */
	void AppendInstruction(R3RDispatchInstructionKind kind, R3RDispatchResultKind result)
	{
		if (this->program.size() >= static_cast<size_t>(R3R_DISPATCH_PROGRAM_MAX)) return;
		R3RDispatchInstruction ins;
		ins.kind = kind;
		ins.result = result;
		ins.var = R3RDispatchRuleVariable::None;
		ins.op = R3RDispatchRuleOp::AtLeast;
		ins.value = 0;
		ins.extra = 0;
		if (kind == R3RDispatchInstructionKind::If || kind == R3RDispatchInstructionKind::Or) {
			ins.var = R3RDispatchRuleVariable::MaxSpeed;
			ins.value = 100;
		}
		if (kind == R3RDispatchInstructionKind::Result) ins.extra = this->DefaultScheduleMask();

		const int count = static_cast<int>(this->program.size());
		const int at = (this->sel >= 0 && this->sel < count) ? this->sel + 1 : count;
		this->program.insert(this->program.begin() + at, ins);
		this->sel = at;
		this->Apply();
		this->UpdateBottomControls();
	}

	void DeleteSelected()
	{
		if (this->sel < 0 || this->sel >= static_cast<int>(this->program.size())) return;
		this->program.erase(this->program.begin() + this->sel);
		if (this->sel >= static_cast<int>(this->program.size())) this->sel = static_cast<int>(this->program.size()) - 1;
		this->Apply();
		this->UpdateBottomControls();
	}

	void ClearProgram()
	{
		if (this->program.empty()) return;
		this->program.clear();
		this->sel = -1;
		this->Apply();
		this->UpdateBottomControls();
	}

	/** 阈值步进；步长按变量类型给（速度 10 / 功率 100 / 其它 1）。 */
	void StepValue(bool up)
	{
		R3RDispatchInstruction *const ins = this->Selected();
		if (ins == nullptr) return;
		if (ins->kind != R3RDispatchInstructionKind::If && ins->kind != R3RDispatchInstructionKind::Or) return;
		const uint32_t step = DispatchValueStep(ins->var);
		/* 机车类型只有 0..N-1，步进不得越过上界。 */
		const uint32_t cap = (ins->var == R3RDispatchRuleVariable::EngineClass)
				? static_cast<uint32_t>(lengthof(_dispatch_engine_class_strings) - 1)
				: R3R_DISPATCH_VALUE_MAX;
		if (up) {
			ins->value = std::min<uint32_t>(cap, ins->value + step);
		} else {
			ins->value = (ins->value >= step) ? ins->value - step : 0;
		}
		this->Apply();
	}

	void ToggleOp()
	{
		R3RDispatchInstruction *const ins = this->Selected();
		if (ins == nullptr) return;
		if (ins->kind != R3RDispatchInstructionKind::If && ins->kind != R3RDispatchInstructionKind::Or) return;
		/* R3R (第 455 轮续二, KI-652): 枚举指标（机车类型）在「是 / 不是」之间切换；
		 * 数值指标在六档里循环（至少 → 至多 → 等于 → 不等于 → 大于 → 小于）。 */
		if (ins->var == R3RDispatchRuleVariable::EngineClass) {
			ins->op = (ins->op == R3RDispatchRuleOp::NotEqual) ? R3RDispatchRuleOp::Equal : R3RDispatchRuleOp::NotEqual;
		} else {
			static const R3RDispatchRuleOp order[] = {
				R3RDispatchRuleOp::AtLeast, R3RDispatchRuleOp::AtMost, R3RDispatchRuleOp::Equal,
				R3RDispatchRuleOp::NotEqual, R3RDispatchRuleOp::Greater, R3RDispatchRuleOp::Less,
			};
			size_t next = 0;
			for (size_t i = 0; i < lengthof(order); i++) {
				if (order[i] == ins->op) {
					next = (i + 1) % lengthof(order);
					break;
				}
			}
			ins->op = order[next];
		}
		this->Apply();
	}

	void ToggleResult()
	{
		R3RDispatchInstruction *const ins = this->Selected();
		if (ins == nullptr || ins->kind != R3RDispatchInstructionKind::Result) return;
		ins->result = (ins->result == R3RDispatchResultKind::Allow) ? R3RDispatchResultKind::Deny : R3RDispatchResultKind::Allow;
		this->Apply();
	}

	void SetVariable(R3RDispatchRuleVariable var)
	{
		R3RDispatchInstruction *const ins = this->Selected();
		if (ins == nullptr) return;
		if (ins->kind != R3RDispatchInstructionKind::If && ins->kind != R3RDispatchInstructionKind::Or) return;
		ins->var = var;
		if (var == R3RDispatchRuleVariable::MaxSpeed && ins->value == 0) ins->value = 100;
		/* R3R (第 455 轮, KI-652): 切到机车类型时把阈值收进合法的类型编号范围，
		 * 并把比较符收进「等于 / 不等于」（枚举只有「是 / 不是」两种读法；
		 * 求值侧同样只认这两项，见 R3RDispatchConditionHolds）。 */
		if (var == R3RDispatchRuleVariable::EngineClass) {
			if (ins->value >= lengthof(_dispatch_engine_class_strings)) ins->value = 0;
			if (ins->op != R3RDispatchRuleOp::Equal && ins->op != R3RDispatchRuleOp::NotEqual) {
				ins->op = R3RDispatchRuleOp::Equal;
			}
		}
		this->Apply();
	}

	/** R3R (第 451 轮续二): 由多选窗口整份回写掩码。
	 *  掩码全 0 表示这条结果谁都不匹配（合法的负判据）。 */
	void SetScheduleMask(uint32_t mask)
	{
		R3RDispatchInstruction *const ins = this->Selected();
		if (ins == nullptr || ins->kind != R3RDispatchInstructionKind::Result) return;
		if (ins->extra == mask) return;
		ins->extra = mask;
		this->Apply();
		this->SetDirty();
	}

	/** R3R (第 451 轮续二): 没选中任何行时，结果类控件也要能按下去 ——
	 *  按一下先追加一条结果指令，后续点击就作用在它身上。 */
	bool EnsureResultRow()
	{
		R3RDispatchInstruction *const ins = this->Selected();
		if (ins != nullptr) return ins->kind == R3RDispatchInstructionKind::Result;
		this->AppendInstruction(R3RDispatchInstructionKind::Result, R3RDispatchResultKind::Allow);
		const R3RDispatchInstruction *const added = this->Selected();
		return added != nullptr && added->kind == R3RDispatchInstructionKind::Result;
	}

	/** 底部控件的文案与可用性跟着选中行走。 */
	void UpdateBottomControls()
	{
		const R3RDispatchInstruction *const ins = this->Selected();
		const bool cond = ins != nullptr && (ins->kind == R3RDispatchInstructionKind::If || ins->kind == R3RDispatchInstructionKind::Or);
		/* 没选中任何行时（空程序最常见）结果类控件不再置灰：
		 * 点一下由 EnsureResultRow() 自动建一条结果指令。 */
		const bool res = ins == nullptr || ins->kind == R3RDispatchInstructionKind::Result;

		this->SetWidgetDisabledState(WID_DP_VAR, !cond);
		this->SetWidgetDisabledState(WID_DP_OP, !cond);
		this->SetWidgetDisabledState(WID_DP_VALUE_DOWN, !cond);
		this->SetWidgetDisabledState(WID_DP_VALUE_UP, !cond);
		this->SetWidgetDisabledState(WID_DP_RESULT_KIND, !res);
		this->SetWidgetDisabledState(WID_DP_SCHEDULE, !res);
		this->SetWidgetDisabledState(WID_DP_DELETE, ins == nullptr);
		this->SetWidgetDisabledState(WID_DP_CLEAR, this->program.empty());
		/* 底部控件的「当前值」文案是动态文本（变量名/阈值/调度名），
		 * 而 NWidgetCore::SetString 只吃 StringID，故文字在 DrawWidget 里自绘。 */
	}

	/** 直接键入阈值（照抄可编程信号机的数值输入方式）。 */
	void SetValue(uint32_t value)
	{
		R3RDispatchInstruction *const ins = this->Selected();
		if (ins == nullptr) return;
		if (ins->kind != R3RDispatchInstructionKind::If && ins->kind != R3RDispatchInstructionKind::Or) return;
		ins->value = std::min<uint32_t>(
				(ins->var == R3RDispatchRuleVariable::EngineClass)
						? static_cast<uint32_t>(lengthof(_dispatch_engine_class_strings) - 1)
						: R3R_DISPATCH_VALUE_MAX,
				value);
		this->Apply();
	}
	/* R3R_DP_REST2 */

	const R3RDispatchInstruction *SelectedConst() const
	{
		if (this->sel < 0 || this->sel >= static_cast<int>(this->program.size())) return nullptr;
		return &this->program[static_cast<size_t>(this->sel)];
	}

	/**
	 * 标题栏带 {STATION} {STRING} 两个参数，而 SetString 只吃 StringID，
	 * 带参串直接塞控件会画成 "(invalid parameter)" ⇒ 在这里给出成品文本。
	 */
	std::string GetWidgetString(WidgetID widget, StringID stringid) const override
	{
		if (widget == WID_DP_CAPTION) {
			const Station *const st = Station::GetIfValid(this->station_id);
			return GetString(STR_R3R_DP_CAPTION, this->station_id, DispatchYardLabel(st, this->yard));
		}
		return this->Window::GetWidgetString(widget, stringid);
	}

	/** 底部控件的「当前值」文字：控件本身设成 STR_NULL，文字在这里画。 */
	void DrawBottomText(const Rect &r, WidgetID widget) const
	{
		const R3RDispatchInstruction *const ins = this->SelectedConst();
		const bool cond = ins != nullptr && (ins->kind == R3RDispatchInstructionKind::If || ins->kind == R3RDispatchInstructionKind::Or);
		const bool res = ins != nullptr && ins->kind == R3RDispatchInstructionKind::Result;

		std::string text;
		switch (widget) {
			case WID_DP_VAR:
				text = cond ? DispatchVarLabel(ins->var, ins->extra) : GetString(STR_R3R_DP_VAR_NONE);
				break;
			case WID_DP_OP:
				text = cond ? DispatchConditionOpLabel(*ins) : GetString(STR_R3R_DP_OP_AT_LEAST);
				break;
			case WID_DP_VALUE:
				if (!cond) {
					text = std::string("-");
				} else if (ins->var == R3RDispatchRuleVariable::EngineClass) {
					text = DispatchEngineClassLabel(ins->value);
				} else {
					text = GetString(STR_R3R_DP_VALUE, ins->value);
				}
				break;
			case WID_DP_RESULT_KIND:
				text = (res && ins->result == R3RDispatchResultKind::Deny) ? GetString(STR_R3R_DP_INS_DENY) : GetString(STR_R3R_DP_INS_ALLOW);
				break;
			case WID_DP_SCHEDULE: {
				if (!res) {
					text = GetString(STR_R3R_DP_NO_SCHEDULE);
					break;
				}
				std::vector<uint32_t> schedules;
				DispatchCollectSchedules(Station::GetIfValid(this->station_id), this->yard, schedules);
				text = DispatchMaskLabel(schedules, ins->extra);
				break;
			}
			default:
				return;
		}
		const int height = GetCharacterHeight(FontSize::Normal);
		const int top = r.top + std::max(0, (r.bottom - r.top + 1 - height) / 2);
		DrawString(r.left + 4, r.right - 4, top, text, TextColour::Black);
	}

	void DrawWidget(const Rect &r, WidgetID widget) const override
	{
		if (widget == WID_DP_VAR || widget == WID_DP_OP || widget == WID_DP_VALUE ||
				widget == WID_DP_RESULT_KIND || widget == WID_DP_SCHEDULE) {
			this->DrawBottomText(r, widget);
			return;
		}
		if (widget != WID_DP_LIST) return;

		const int step = this->LineHeight();
		if (this->program.empty()) {
			DrawString(r.left + 2, r.right - 2, r.top + 2, STR_R3R_DP_EMPTY, TextColour::Black);
			return;
		}

		const int count = static_cast<int>(this->program.size());
		std::vector<uint32_t> schedules;
		DispatchCollectSchedules(Station::GetIfValid(this->station_id), this->yard, schedules);
		int depth = 0;
		int y = r.top + 2;
		for (int i = 0; i < count; i++) {
			const R3RDispatchInstruction &ins = this->program[static_cast<size_t>(i)];
			/* R3R (第 455 轮, KI-652): 「或者」和「否则」一样是「如果」链上的同层分支 —— 先退回到
			 * 「如果」那一层再画，画完再进它自己的体。旧写法只给「否则」退层，「或者」既不退层
			 * 又多进一层，于是 IF / OR / 体 / ENDIF 每一层的缩进都对不上。 */
			if (ins.kind == R3RDispatchInstructionKind::EndIf || ins.kind == R3RDispatchInstructionKind::Else ||
					ins.kind == R3RDispatchInstructionKind::Or) {
				depth = std::max(0, depth - 1);
			}
			if (i >= this->top) {
				if (i == this->sel) GfxFillRect(r.left + 1, y, r.right - 1, y + step - 1, PC_DARK_GREY);
				DrawString(r.left + 4 + depth * 8, r.right - 4, y, DispatchInstructionLine(ins, schedules), TextColour::Black);
				y += step;
				if (y + step > r.bottom) break;
			}
			if (ins.kind == R3RDispatchInstructionKind::If || ins.kind == R3RDispatchInstructionKind::Or ||
					ins.kind == R3RDispatchInstructionKind::Else) {
				depth++;
			}
		}
	}

	void OnClick(Point pt, WidgetID widget, int click_count) override
	{
		switch (widget) {
			case WID_DP_LIST: {
				const NWidgetBase *const list = this->GetWidget<NWidgetBase>(WID_DP_LIST);
				if (list == nullptr || this->program.empty()) break;
				const int row = this->top + std::max(0, (pt.y - static_cast<int>(list->pos_y) - 2) / this->LineHeight());
				this->sel = Clamp(row, 0, static_cast<int>(this->program.size()) - 1);
				this->UpdateBottomControls();
				this->SetDirty();
				/* 多选窗口的勾选状态活读「当前选中行」，一旦选中行不是结果指令，
				 * 那个窗口就没有编辑对象了，直接关掉，免得勾了没反应。 */
				{
					const R3RDispatchInstruction *const ins = this->Selected();
					if (ins == nullptr || ins->kind != R3RDispatchInstructionKind::Result) {
						CloseWindowById(WindowClass::DispatchSchedulePick, this->window_number, false);
					} else {
						SetWindowDirty(WindowClass::DispatchSchedulePick, this->window_number);
					}
				}
				break;
			}

			case WID_DP_INSERT: {
				static const StringID insert_list[] = {
					STR_R3R_DP_INS_IF, STR_R3R_DP_INS_OR, STR_R3R_DP_INS_ELSE, STR_R3R_DP_INS_ENDIF,
					STR_R3R_DP_RESULT_ALLOW_BTN, STR_R3R_DP_RESULT_DENY_BTN,
				};
				ShowDropDownMenu(this, insert_list, 0, WID_DP_INSERT, 0, 0);
				break;
			}

			case WID_DP_VAR: {
				/* R3R (第 455 轮, KI-652): 弹的是「可选列表」而不是整张枚举表 ——
				 * 三个恒 0 的信号条件已按玩家口径移除。 */
				const R3RDispatchInstruction *const ins = this->Selected();
				const int sel = (ins != nullptr) ? DispatchVarSelectIndex(ins->var) : 0;
				ShowDropDownMenu(this, _dispatch_var_selectable_strings, sel, WID_DP_VAR, 0, 0);
				break;
			}

			/* R3R (第 451 轮续二): 调度改成多选 ⇒ 弹自绘勾选窗口（下拉菜单只能单选）。
			 * 没选中任何行时先追加一条结果指令，再让用户勾选。 */
			case WID_DP_SCHEDULE: {
				if (!this->EnsureResultRow()) break;
				ShowDispatchSchedulePicker(this);
				break;
			}

			case WID_DP_DELETE: this->DeleteSelected(); break;
			case WID_DP_CLEAR:  this->ClearProgram(); break;
			case WID_DP_OP:     this->ToggleOp(); break;
			case WID_DP_VALUE: {
				const R3RDispatchInstruction *const ins = this->Selected();
				if (ins == nullptr) break;
				if (ins->kind != R3RDispatchInstructionKind::If && ins->kind != R3RDispatchInstructionKind::Or) break;
				/* 机车类型是枚举：给下拉而不是数字输入框。 */
				if (ins->var == R3RDispatchRuleVariable::EngineClass) {
					const int sel = std::min<int>(static_cast<int>(ins->value),
							static_cast<int>(lengthof(_dispatch_engine_class_strings) - 1));
					ShowDropDownMenu(this, _dispatch_engine_class_strings, sel, WID_DP_VALUE, 0, 0);
					break;
				}
				ShowQueryString(GetString(STR_R3R_DP_VALUE, ins->value), STR_R3R_DP_VALUE, 7, this, CS_NUMERAL, {});
				break;
			}
			case WID_DP_VALUE_DOWN: this->StepValue(false); break;
			case WID_DP_VALUE_UP:   this->StepValue(true); break;
			case WID_DP_RESULT_KIND:
				if (this->EnsureResultRow()) this->ToggleResult();
				break;
			default: break;
		}
	}

	void OnDropdownSelect(WidgetID widget, int index, int click_result) override
	{
		switch (widget) {
			case WID_DP_INSERT:
				switch (index) {
					case 0: this->AppendInstruction(R3RDispatchInstructionKind::If, R3RDispatchResultKind::Allow); break;
					case 1: this->AppendInstruction(R3RDispatchInstructionKind::Or, R3RDispatchResultKind::Allow); break;
					case 2: this->AppendInstruction(R3RDispatchInstructionKind::Else, R3RDispatchResultKind::Allow); break;
					case 3: this->AppendInstruction(R3RDispatchInstructionKind::EndIf, R3RDispatchResultKind::Allow); break;
					case 4: this->AppendInstruction(R3RDispatchInstructionKind::Result, R3RDispatchResultKind::Allow); break;
					case 5: this->AppendInstruction(R3RDispatchInstructionKind::Result, R3RDispatchResultKind::Deny); break;
					default: break;
				}
				break;

			case WID_DP_VAR:
				this->SetVariable(DispatchVarFromSelectIndex(index));
				break;

			case WID_DP_VALUE:
				/* 只有机车类型走下拉（其余变量是数字输入框，不会到这里）。 */
				this->SetValue(static_cast<uint32_t>(std::max(0, index)));
				break;

			default: break;
		}
	}

	void OnResize() override
	{
		this->UpdateScroll();
		this->SetDirty();
	}

	void OnInvalidateData(int data = 0, bool gui_scope = true) override
	{
		if (!gui_scope) return;
		this->Reload();
		this->UpdateBottomControls();
		this->SetDirty();
		/* 多选窗口的勾选状态是从本窗口活读的，回读后要让让它重画一次。 */
		SetWindowDirty(WindowClass::DispatchSchedulePick, this->window_number);
	}
	/** 数值输入框回填。 */
	void OnQueryTextFinished(std::optional<std::string> str) override
	{
		if (!str.has_value() || str->empty()) return;
		/* 项目禁止 strtoul（safeguards.h 已把 <cstdlib> 的数值解析函数全部改成毒宏），
		 * 统一走 ParseInteger；clamp 让超大输入直接夹到上限而不是溢出。 */
		this->SetValue(ParseInteger<uint32_t>(*str, 10, true).value_or(0));
	}

	/** 拖动滚动条只改视窗起点，不改数据。 */
	void OnScrollbarScroll(WidgetID widget) override
	{
		if (widget != WID_DP_SCROLLBAR) return;
		this->top = static_cast<int>(this->GetScrollbar(WID_DP_SCROLLBAR)->GetPosition());
		this->SetDirty();
	}
};

/** R3R (第 451 轮续二): 多选调度窗口的控件号（只在本文件用，故不进 station_widget.h）。 */
enum DispatchSchedulePickWidgets : WidgetID {
	WID_DSP_CAPTION,
	WID_DSP_LIST,
	WID_DSP_SCROLLBAR,
	WID_DSP_RESIZEBOX,
};

/**
 * R3R (第 451 轮续二): 「这条结果允许哪些调度」的多选窗口。
 * 下拉菜单只能单选，而调度名是动态文本（不是 StringID），所以自绘一份勾选列表；
 * 点一行即时切换该位并立即下发命令，因此没有「确定」按钮。
 */
struct DispatchSchedulePickWindow : Window {
	StationID station_id; ///< 哪个站
	uint16_t yard;        ///< 哪个场
	uint32_t mask;        ///< 本地掩码副本（bit i = 去重后第 i 份调度）
	int top;              ///< 列表第一行显示的下标

	DispatchSchedulePickWindow(WindowDesc &desc, StationID station_id, uint16_t yard, uint32_t mask) : Window(desc),
			station_id(station_id), yard(yard), mask(mask), top(0)
	{
		this->CreateNestedTree();
		this->FinishInitNested(PackDispatchProgramWindowNumber(station_id, yard));
		this->UpdateScroll();
		this->SetDirty();
	}

	/** 父窗口（同一个窗口号 + 程序窗口类）；已关掉则返回 nullptr。 */
	DispatchProgramWindow *Parent()
	{
		Window *const w = FindWindowById(WindowClass::DispatchProgram, this->window_number);
		return (w == nullptr) ? nullptr : static_cast<DispatchProgramWindow *>(w);
	}

	/** 掩码一律从父窗口当前选中行活读：命令被拒/程序被读档重载后本地副本会过期。 */
	uint32_t CurrentMask() const
	{
		DispatchProgramWindow *const parent = const_cast<DispatchSchedulePickWindow *>(this)->Parent();
		if (parent == nullptr) return this->mask;
		const R3RDispatchInstruction *const ins = parent->SelectedConst();
		return (ins == nullptr) ? this->mask : ins->extra;
	}

	int LineHeight() const { return GetCharacterHeight(FontSize::Normal) + 3; }

	void UpdateScroll()
	{
		std::vector<uint32_t> schedules;
		DispatchCollectSchedules(Station::GetIfValid(this->station_id), this->yard, schedules);
		const NWidgetBase *const list = this->GetWidget<NWidgetBase>(WID_DSP_LIST);
		const int lines = (list == nullptr) ? 1 : std::max(1, (static_cast<int>(list->current_y) - 4) / this->LineHeight());
		const int count = static_cast<int>(schedules.size());
		this->top = Clamp(this->top, 0, std::max(0, count - lines));
		Scrollbar *const sb = this->GetScrollbar(WID_DSP_SCROLLBAR);
		sb->SetCount(std::max<uint32_t>(1, static_cast<uint32_t>(schedules.size())));
		sb->SetCapacity(lines);
		sb->SetPosition(this->top);
		sb->SetStepSize(std::max(1, lines));
	}

	void DrawWidget(const Rect &r, WidgetID widget) const override
	{
		if (widget != WID_DSP_LIST) return;

		std::vector<uint32_t> schedules;
		DispatchCollectSchedules(Station::GetIfValid(this->station_id), this->yard, schedules);
		if (schedules.empty()) {
			DrawString(r.left + 2, r.right - 2, r.top + 2, STR_R3R_DP_NO_SCHEDULE, TextColour::Black);
			return;
		}

		const int step = this->LineHeight();
		const int count = static_cast<int>(schedules.size());
		const uint32_t mask = this->CurrentMask();
		int y = r.top + 2;
		for (int i = this->top; i < count; i++) {
			const bool on = (i < static_cast<int>(R3R_DISPATCH_MASK_BITS)) && HasBit(mask, static_cast<uint>(i));
			const std::string text = std::string(on ? "[X] " : "[ ] ") + DispatchScheduleName(schedules[static_cast<size_t>(i)]);
			DrawString(r.left + 4, r.right - 4, y, text, TextColour::Black);
			y += step;
			if (y + step > r.bottom) break;
		}
	}

	void OnClick(Point pt, WidgetID widget, int click_count) override
	{
		if (widget != WID_DSP_LIST) return;

		DispatchProgramWindow *const parent = this->Parent();
		if (parent == nullptr) {
			CloseWindowById(WindowClass::DispatchSchedulePick, this->window_number, false);
			return;
		}

		std::vector<uint32_t> schedules;
		DispatchCollectSchedules(Station::GetIfValid(this->station_id), this->yard, schedules);
		if (schedules.empty()) return;

		const NWidgetBase *const list = this->GetWidget<NWidgetBase>(WID_DSP_LIST);
		if (list == nullptr) return;
		const int row = this->top + std::max(0, (pt.y - static_cast<int>(list->pos_y) - 2) / this->LineHeight());
		if (row < 0 || row >= static_cast<int>(schedules.size())) return;
		if (row >= static_cast<int>(R3R_DISPATCH_MASK_BITS)) return;

		const uint32_t bit = 1u << row;
		const uint32_t mask = this->CurrentMask();
		this->mask = HasBit(mask, static_cast<uint>(row)) ? (mask & ~bit) : (mask | bit);
		parent->SetScheduleMask(this->mask);
		this->SetDirty();
	}

	void OnResize() override
	{
		this->UpdateScroll();
		this->SetDirty();
	}

	void OnScrollbarScroll(WidgetID widget) override
	{
		if (widget != WID_DSP_SCROLLBAR) return;
		this->top = static_cast<int>(this->GetScrollbar(WID_DSP_SCROLLBAR)->GetPosition());
		this->SetDirty();
	}
};

static constexpr NWidgetPart _nested_dispatch_schedule_pick_widgets[] = {
	NWidget(NWID_HORIZONTAL),
		NWidget(WWT_CLOSEBOX, Colours::Grey),
		NWidget(WWT_CAPTION, Colours::Grey, WID_DSP_CAPTION), SetStringTip(STR_R3R_DP_SCHEDULE_TOOLTIP, STR_TOOLTIP_WINDOW_TITLE_DRAG_THIS),
		NWidget(WWT_SHADEBOX, Colours::Grey),
		NWidget(WWT_STICKYBOX, Colours::Grey),
	EndContainer(),

	NWidget(NWID_HORIZONTAL),
		NWidget(WWT_PANEL, Colours::Grey, WID_DSP_LIST), SetMinimalSize(240, 150), SetResize(1, 10), SetToolTip(STR_R3R_DP_SCHEDULE_TOOLTIP), SetScrollbar(WID_DSP_SCROLLBAR), EndContainer(),
		NWidget(NWID_VSCROLLBAR, Colours::Grey, WID_DSP_SCROLLBAR),
	EndContainer(),

	NWidget(NWID_HORIZONTAL),
		NWidget(WWT_PANEL, Colours::Grey), SetMinimalSize(0, 0), SetResize(1, 0), EndContainer(),
		NWidget(WWT_RESIZEBOX, Colours::Grey),
	EndContainer(),
};

static WindowDesc _dispatch_schedule_pick_desc(__FILE__, __LINE__,
	WindowPosition::Automatic, "r3r_dispatch_schedule_pick", 240, 180,
	WindowClass::DispatchSchedulePick, WindowClass::None,
	{}, _nested_dispatch_schedule_pick_widgets
);

static void ShowDispatchSchedulePicker(DispatchProgramWindow *parent)
{
	if (parent == nullptr) return;
	if (BringWindowToFrontById(WindowClass::DispatchSchedulePick, parent->window_number) != nullptr) {
		/* 已开着：勾选状态是活读父窗口的，只需重画一次。 */
		SetWindowDirty(WindowClass::DispatchSchedulePick, parent->window_number);
		return;
	}
	const R3RDispatchInstruction *const ins = parent->SelectedConst();
	new DispatchSchedulePickWindow(_dispatch_schedule_pick_desc, parent->station_id, parent->yard,
			ins != nullptr ? ins->extra : 0);
}

/** R3R (第 451 轮续): layout of the dispatch allocation rule program window. */
static constexpr NWidgetPart _nested_dispatch_program_widgets[] = {
	NWidget(NWID_HORIZONTAL),
		NWidget(WWT_CLOSEBOX, Colours::Grey),
		NWidget(WWT_CAPTION, Colours::Grey, WID_DP_CAPTION), SetStringTip(STR_R3R_DP_CAPTION, STR_TOOLTIP_WINDOW_TITLE_DRAG_THIS),
		NWidget(WWT_SHADEBOX, Colours::Grey),
		NWidget(WWT_DEFSIZEBOX, Colours::Grey),
		NWidget(WWT_STICKYBOX, Colours::Grey),
	EndContainer(),

	NWidget(NWID_HORIZONTAL),
		NWidget(WWT_PANEL, Colours::Grey, WID_DP_LIST), SetMinimalSize(360, 150), SetResize(1, 10), SetToolTip(STR_R3R_DP_LIST_TOOLTIP), SetScrollbar(WID_DP_SCROLLBAR), EndContainer(),
		NWidget(NWID_VSCROLLBAR, Colours::Grey, WID_DP_SCROLLBAR),
	EndContainer(),

	NWidget(NWID_HORIZONTAL),
		NWidget(WWT_PUSHTXTBTN, Colours::Grey, WID_DP_INSERT), SetMinimalSize(110, 12), SetResize(1, 0), SetFill(1, 0), SetStringTip(STR_R3R_DP_INSERT_BTN, STR_R3R_DP_INSERT_TOOLTIP),
		NWidget(WWT_PUSHTXTBTN, Colours::Grey, WID_DP_DELETE), SetMinimalSize(60, 12), SetStringTip(STR_R3R_DP_DELETE_BTN, STR_R3R_DP_DELETE_TOOLTIP),
		NWidget(WWT_PUSHTXTBTN, Colours::Grey, WID_DP_CLEAR), SetMinimalSize(60, 12), SetStringTip(STR_R3R_DP_CLEAR_BTN, STR_R3R_DP_CLEAR_TOOLTIP),
	EndContainer(),

	NWidget(NWID_HORIZONTAL),
		NWidget(NWID_BUTTON_DROPDOWN, Colours::Grey, WID_DP_VAR), SetMinimalSize(110, 12), SetResize(1, 0), SetFill(1, 0), SetStringTip(STR_NULL, STR_R3R_DP_VAR_TOOLTIP),
		NWidget(WWT_PUSHTXTBTN, Colours::Grey, WID_DP_OP), SetMinimalSize(50, 12), SetStringTip(STR_NULL, STR_R3R_DP_OP_TOOLTIP),
		NWidget(WWT_PUSHIMGBTN, Colours::Grey, WID_DP_VALUE_DOWN), SetMinimalSize(12, 12), SetSpriteTip(SPR_ARROW_DOWN, STR_R3R_DP_STEP_TOOLTIP),
		NWidget(WWT_PUSHTXTBTN, Colours::Grey, WID_DP_VALUE), SetMinimalSize(64, 12), SetStringTip(STR_NULL, STR_R3R_DP_STEP_TOOLTIP),
		NWidget(WWT_PUSHIMGBTN, Colours::Grey, WID_DP_VALUE_UP), SetMinimalSize(12, 12), SetSpriteTip(SPR_ARROW_UP, STR_R3R_DP_STEP_TOOLTIP),
	EndContainer(),

	NWidget(NWID_HORIZONTAL),
		NWidget(WWT_PUSHTXTBTN, Colours::Grey, WID_DP_RESULT_KIND), SetMinimalSize(80, 12), SetResize(1, 0), SetFill(1, 0), SetStringTip(STR_NULL, STR_R3R_DP_RESULT_KIND_TOOLTIP),
		NWidget(WWT_PUSHTXTBTN, Colours::Grey, WID_DP_SCHEDULE), SetMinimalSize(170, 12), SetResize(1, 0), SetFill(1, 0), SetStringTip(STR_NULL, STR_R3R_DP_SCHEDULE_TOOLTIP),
		NWidget(WWT_RESIZEBOX, Colours::Grey),
	EndContainer(),
};

static WindowDesc _dispatch_program_desc(__FILE__, __LINE__,
	WindowPosition::Automatic, "r3r_dispatch_program", 404, 240,
	WindowClass::DispatchProgram, WindowClass::None,
	{}, _nested_dispatch_program_widgets
);

/** R3R (第 451 轮续): 打开某个场（0 = 整站）的「分配规则程序」编辑窗口。 */
void ShowDispatchProgramWindow(StationID station_id, uint16_t yard)
{
	if (Station::GetIfValid(station_id) == nullptr) return;
	const int number = PackDispatchProgramWindowNumber(station_id, yard);
	if (BringWindowToFrontById(WindowClass::DispatchProgram, number) != nullptr) return;
	new DispatchProgramWindow(_dispatch_program_desc, station_id, yard);
}
