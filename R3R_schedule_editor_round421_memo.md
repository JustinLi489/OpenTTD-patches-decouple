# R3R 第 421 轮临时分析报告（2026-11-11）：无主排程编辑器（KI-600 / C-8 E-2）+ 语言包版本报错收口

> 状态：**已实现 + 已编译**（`EXIT_CODE=0`，exe 2026-11-11 21:15:24），游戏内复测待做。
> 关联：`R3R_KNOWN_ISSUES.md` 的 `## 第 421 轮（2026-11-11）`；前序备忘 `R3R_round420_six_issues_memo.md` §3.7（E-2 方案）、§6.4（KI-600 待实现）。

---

## 一、本轮目标

玩家在 C-8 上拍板：「编辑侧 = **E-2 独立排程编辑窗口**；删除侧 = **方案 1**，借用中则拒绝删除并提示」。

- **KI-600 编辑侧（E-2）**：无主（loose）排程没有 `Vehicle`，而 `OrdersWindow` 的每个按钮都按 `VehicleID` 发车辆命令 ⇒ 必须新开一个「只认 `OrderListID`」的订单编辑窗口。
- **KI-600 删除侧（方案 1）**：有主排程点删除不静默失败，而是弹提示；命令层仍拒绝。

---

## 二、落地实现

### 2.1 新增文件

| 文件 | 作用 |
|---|---|
| `src\r3r_schedule_gui.cpp` / `.h` | 排程池窗口（第 417~419 轮已存在）；本轮 `ShowR3RScheduleEditor()` 原型与「编辑按钮分流」 |
| `src\widgets\r3r_schedule_widget.h` | 排程池窗口控件 ID（已有） |
| `src\order_gui.cpp`（新增区段 :5434~:5680） | `_nested_r3r_schedule_editor_widgets` + `struct R3RScheduleEditorWindow` + `_r3r_schedule_editor_desc` + `ShowR3RScheduleEditor()` |

### 2.2 新增命令（四个，全部 `OtherManagement`、`NT` 即不占网络序号）

`src\command_type.h:802-805`：

```cpp
InsertR3RLooseOrder,      ///< 插入一条订单到无主排程
DeleteR3RLooseOrder,      ///< 删除无主排程的一条订单
MoveR3RLooseOrder,        ///< 重排无主排程的两条订单
DuplicateR3RLooseOrder,   ///< 复制无主排程的一条订单
```

`src\order_cmd.h:101-104` 的签名：

```cpp
DEF_CMD_DIRECT_NT(Commands::InsertR3RLooseOrder,    CmdR3RInsertLooseOrder,    {}, CommandType::OtherManagement, InsertLooseOrderCmdData)
DEF_CMD_TUPLE_NT (Commands::DeleteR3RLooseOrder,    CmdR3RDeleteLooseOrder,    {}, CommandType::OtherManagement, CmdDataT<uint32_t, uint16_t>)
DEF_CMD_TUPLE_NT (Commands::MoveR3RLooseOrder,      CmdR3RMoveLooseOrder,      {}, CommandType::OtherManagement, CmdDataT<uint32_t, uint16_t, uint16_t, uint16_t>)
DEF_CMD_TUPLE_NT (Commands::DuplicateR3RLooseOrder, CmdR3RDuplicateLooseOrder, {}, CommandType::OtherManagement, CmdDataT<uint32_t, uint16_t>)
```

`InsertR3RLooseOrder` 的载荷 `InsertLooseOrderCmdData`（`OrderListID` + 插入位置 + `Order`）**不可序列化**，
因此投递只能用 `DoCommandP<T>(payload, err)`，不能用 `Command<T>::Post`：

```cpp
DoCommandP<Commands::InsertR3RLooseOrder>(InsertLooseOrderCmdData(this->schedule.base(), this->GetInsertPosition(), order), STR_ERROR_CAN_T_DO_THIS);
```

（`src\order_gui.cpp:5654`）

### 2.3 窗口结构

- 控件：`WID_R3RSE_CAPTION` / `WID_R3RSE_ORDER_LIST`（可滚动，最小 360×100，`SetResize(1,1)`）/ `WID_R3RSE_SCROLLBAR` / `WID_R3RSE_ADD` / `WID_R3RSE_DELETE` / `WID_R3RSE_DUPLICATE` / `WID_R3RSE_MOVE_UP` / `WID_R3RSE_MOVE_DOWN`（后五个 `EqualSize` 容器）。
- 窗口号 = `OrderListID`，`BringWindowToFrontById(WindowClass::R3RScheduleEditor, ol->index.base())` 保证同一排程只有一个窗口（`src\order_gui.cpp:5675-5680`）。
- `WindowDesc`：`WindowClass::R3RScheduleEditor` + `WindowClass::None`（必须补 `parent_class`）。
- 绘制：`DrawOrderString(nullptr, order, i, y, ..., this->vtype)` —— 本轮把 `DrawOrderString` 的第一个参数放宽为可空，并在「无 `Vehicle` 时不画当前订单箭头」（`src\order_gui.cpp:1027-1028`）。
- `Add` 走下拉（本公司的、有设施的车站/路点），选中后按「当前选中项的下一行，否则末尾」插入。
- 刻意**不提供**：改装（refit）、跳转到订单、当前订单占用——这些都需要真实车辆。

### 2.4 删除侧（方案 1）

`src\r3r_schedule_gui.cpp:434-447`：

```cpp
case WID_SCHED_DELETE: {
    const OrderList *const ol = this->GetSelectedList();
    if (ol == nullptr || !this->CanEdit()) break;
    if (ol->GetFirstSharedVehicle() != nullptr || ol->GetNumVehicles() != 0) {
        ShowErrorMessage(GetEncodedString(STR_R3R_SCHEDULE_DELETE_IN_USE), {}, WarningLevel::Info);
        break;
    }
    ...
}
```

配套：`WID_SCHED_DELETE` 的 `SetWidgetDisabledState`（`OnPaint`，`:348`）**去掉了「有车就禁用」**，改为保持可点击，
否则按钮灰掉时玩家点下去连提示都没有——「点不动」正是 KI-602 那一类抱怨的来源。

### 2.5 编辑按钮分流

`src\r3r_schedule_gui.cpp:402-414`：有主排程仍开原来的 `ShowOrdersWindow(first)`；无主排程走 `ShowR3RScheduleEditor(const_cast<OrderList *>(ol))`。

---

## 三、编译失败 8 类与修法（本轮全部结清）

| # | 编译错误 | 根因 | 修法 |
|---|---|---|---|
| 1 | 默认参数重复定义 | `DrawOrderString` 默认实参同时写在 `.h` 与 `.cpp` | 默认参数只留 `src\order_func.h`；`src\order_gui.cpp` 定义处去掉 |
| 2 | `Window` 无两参构造 | API 是单参 `Window(WindowDesc &)` | `R3RScheduleEditorWindow(WindowDesc &desc, int window_number) : Window(desc)` |
| 3 | `TC_FROMSTRING` 未声明 | 该常量不存在 | `TextColour::FromString` |
| 4 | `OnDropdownSelect` 签名不符 | 基类是三参 | `void OnDropdownSelect(WidgetID widget, int index, int click_result) override` |
| 5 | `Command<T>::Post` 载荷不可序列化 | `Types` 要求可序列化 | 改 `DoCommandP<T>(payload, err)` |
| 6 | `WindowDesc` 缺 `parent_class` | 构造签名要求 | 补 `WindowClass::None` |
| 7 | `ShowErrorMessage` 错用 | 见 #8 同族 | 见 #8 |
| 8 | `ShowErrorMessage`: 没有重载函数接受 2 个参数（`src\r3r_schedule_gui.cpp(441)`） | 现存只有 `(EncodedString&&, int, int, CommandCost&)` 与 `(EncodedString&&, EncodedString&&, WarningLevel, int=0, int=0, EncodedString&&={}, CompanyID=Invalid())`；`(StringID, StringID)` 那条不接受 `INVALID_STRING_ID` 之外的组合 | 改三参：`ShowErrorMessage(GetEncodedString(STR_R3R_SCHEDULE_DELETE_IN_USE), {}, WarningLevel::Info)` |

修完 `read_lints` 0 条。

---

## 四、「No available language packs (invalid versions?)」根因与收口

### 4.1 现象

玩家在编译「成功」后启动游戏，弹出 `No available language packs (invalid versions?)`。

### 4.2 猜测根因（已证实）

`.lng` 已按**新**的 `LANGUAGE_PACK_VERSION` 重新生成，但 `openttd.exe` 仍是**旧**文件 ⇒ 两边的版本哈希不相等 ⇒ 加载器把全部语言包判为无效。

证据链：

1. 上一轮（含 #8 修完之前那次）构建实际是 `build\R3R_incbuild.done = EXIT_CODE=1`（`ShowErrorMessage` 的 C2661）⇒ **链接没有执行**，`openttd.exe` 保持旧文件。
2. 但 `strgen` 阶段已跑过并写出新的 `build\lang\*.lng` 与 `build\generated\table\strings.h`（新增 16 个 `STR_R3R_SCHEDULE_EDITOR_*` / `STR_R3R_SCHEDULE_DELETE_IN_USE`）⇒ `LANGUAGE_PACK_VERSION` 变成 **`0x138AA7C4`**（`LANGUAGE_TOTAL_STRINGS = 7093`）。
3. 旧 exe 内嵌的是旧版本常量 ⇒ 全部 66 个 `.lng` 都被判 invalid。

⇒ 这是**构建失败的回声**，不是语言包本身坏了，也不是 `build\lang` 权限/编码问题。

### 4.3 修法

无需改任何源码：修掉 #8 的 C2661 后重跑增量构建即可让 exe 与 `.lng` 对齐。

### 4.4 验证数据（实测）

| 判据 | 读数 |
|---|---|
| `build\generated\table\strings.h` | `LANGUAGE_PACK_VERSION = 0x138AA7C4`，`LANGUAGE_TOTAL_STRINGS = 7093` |
| `build\lang\*.lng` 总数 | **66** |
| 头部第 5~8 字节（小端版本）≠ `0x138AA7C4` 的个数 | **0**（`ver 0x138AA7C4 x66`） |
| 最新 `.lng` mtime | `vietnamese.lng` / `welsh.lng` 均 `20:21:19` |
| `build\openttd.exe` | `21:15:24`，52 807 680 B ⇒ **晚于全部 `.lng`** |

⇒ 版本一致，游戏内「No available language packs」应消失。

### 4.5 教训（已记入 KI-603）

**判断「编译成功」不能只看 cmd 窗口有没有报错、也不能只看 `.lng` 是否更新**；
必须看 `build\R3R_incbuild.done` 的 `EXIT_CODE`。`EXIT_CODE!=0` 时 `strgen`/`lang` 已经写出新文件，
而 exe 仍旧，症状恰好是「语言包无效」，极易误判成语言/编码问题。

---

## 五、构建自证（第 421 轮，2026-11-11 21:15 完成）

| 判据 | 读数 |
|---|---|
| 护栏 `build\R3R_incbuild.guard.log` | `GUARD: incremental is safe (no header/lang file is newer than the newest object)` |
| 步数 | 100 步增量 |
| 日志末行 | `[100/100] Linking CXX executable openttd.exe` |
| `build\R3R_incbuild.done` | `EXIT_CODE=0`（`2026/11/11 21:15:48`） |
| 错误计数（`error C*` / `fatal error` / `FAILED:` / `build stopped`） | **0** |
| `build\openttd.exe` | **2026-11-11 21:15:24**，52 807 680 B |
| obj（均早于 exe） | `order_gui.cpp.obj` 20:49:49 / `order_cmd.cpp.obj` 20:49:49 / `r3r_schedule_gui.cpp.obj` 21:02:25 / `strings.cpp.obj` 21:05:28 |
| 源码（均早于对应 obj） | `src\order_gui.cpp` 20:19:04 / `src\r3r_schedule_gui.cpp` 21:00:34 / `src\order_func.h` 20:18:29 |
| 新串入库 | `STR_R3R_SCHEDULE_DELETE_IN_USE=0xCB7`、`STR_R3R_SCHEDULE_EDITOR_CAPTION=0xCB8` … `STR_R3R_SCHEDULE_EDITOR_MOVE_DOWN_TOOLTIP=0xCC4`（共 14 条，另 2 条为标题/确认串） |
| `read_lints` | 0 |

口径：**只编内测树 `build\`（探针 ON）**；发布树 `build-release\` 未动，仍早于本轮。

顺带说明：链接前曾出现一个 104 814 880 B 的中间态 `openttd.exe`（21:13:08），那是链接器写到一半被读到的结果，
最终文件是 52 807 680 B @ 21:15:24，以最终值为准——**不要用中途读到的体积判断构建产物**。

---

## 六、待复测判据（游戏内）

1. 排程池选中**无主**排程 → 「编辑」应弹出新窗口（标题栏不是 `(invalid parameter)`），窗口号 = 该排程。
2. 新窗口 `Add` 下拉只列本公司有设施的车站/路点；选中后插到「当前选中项的下一行」，未选中则追加到末尾。
3. `Delete` / `Duplicate` / `MoveUp` / `MoveDown` 四个按钮：未选中时灰，边界项（首行上移 / 末行下移）灰，其余可用；操作后选中行跟随移动。
4. 空排程时列表区显示 `STR_R3R_SCHEDULE_EDITOR_EMPTY`（居中），不崩溃。
5. 选中**有主**排程 → 「编辑」仍开原订单窗口（行为不变）。
6. 选中**有主**排程 → 「删除」**可点**，点击弹 `STR_R3R_SCHEDULE_DELETE_IN_USE` 提示而不是静默无反应。
7. 无主排程删除仍走确认框（`STR_R3R_SCHEDULE_DELETE_TITLE` / `_CONFIRM`），确认后消失。
8. 语言包下拉正常列出各语言，不再报 `No available language packs (invalid versions?)`。
9. 中文界面下新窗口 14 条串全部为中文（不是英文回落）。

---

## 七、未做 / 边界

- 新编辑器**不支持**改装、跳转订单、当前订单标记、时刻表编辑（都需要真实车辆语义）。
- 新命令只拒绝「排程不存在 / 无编辑权」；**没有**在命令层校验「该排程已被车辆借用」以外的并发情形（GUI 已挡）。
- 编辑器的 `vtype` 固定为 `VehicleType::Train`（排程池目前只处理列车排程），若将来支持公路/船只会画错图标。
- `WID_R3RSE_ADD` 的下拉用 `StationID` 当 `index` 回传，不走 item 的 `value`；当前列表项 `value` 就是 `StationID`，两者一致，将来改列表必须同步（与第 420 轮 KI-598 的同类脆弱点）。
- 发布树未编。
