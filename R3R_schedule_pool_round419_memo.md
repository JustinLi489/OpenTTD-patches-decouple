# 第 419 轮：排程池 / 调度池 界面五项优化（临时分析报告）

日期：2026-11-11
来源：玩家五条需求（原话要点见下）
状态：分析登记中（本文件先落盘，随后逐项取证 + 实现 + 构建自证）

## 一、玩家需求原文要点

1. **车站排程池界面**有一处显示 `invalid parameter`，应该是用于选场的，但按钮按不下去。
2. **公司排程管理界面**现在是用窗口顶部的下拉框选中要分配排程的列车。改成：先选中排程 → 点「分配」→ 弹出一个带**列车图像**的列车列表页供玩家点选。
3. 自家公司的排程管理界面里，对**公开的自家排程**，希望像公开路签那样，在排程名称前加一个标记（比如一面小旗子）。
4. 文案/行为调整：
   - 编辑排程的按钮文本「命令」→「编辑排程」；
   - 查看自有表指向该排程的按钮「列车」→「查看使用者」；
   - 新建排程按钮：点一下**直接创建一个空的无主排程**（不再弹输入框？待确认口径）。
5. 公司排程管理界面需要**及时刷新**：新建排程后必须重开窗口才能看到的荒唐情况。

## 二、涉及窗口定位

- 第 1 条：`StationDispatchWindow`（车站调度池/排程池），在 `src\station_gui.cpp`；选场控件 `WID_SD_YARD_SEL`。
- 第 2~5 条：排程池窗口 `R3RScheduleWindow`（第 418 轮改过的 `src\r3r_schedule_gui.cpp` + `src\widgets\r3r_schedule_widget.h`）。

## 三、逐项初步判断（待取证）

### 3.1 第 1 条：invalid parameter

`src\lang\simplified_chinese.txt` / `english.txt` 里 `STR_R3R_DISPATCH_POOL_YARD_SEL : {BLACK}场: {STRING}`，
而代码把 `YardLabel()` 返回的 `std::string` 直接当 `{STRING}` 参数传进 `GetString()`。
→ `{STRING}` 需要 `StringID`，传 `std::string` 会被当成 raw string 参数 ⇒ 显示 `(invalid parameter)`。
与 KI-262（站场窗口标题少传参数）同族。

「按钮按不下去」待查：
- 若 `OnPaint()` 里 `SetWidgetDisabledState(WID_SD_YARD_SEL, !can_edit)` 命中（非本公司车站 / can_edit 判据过严），点击会在 `DispatchLeftClickEvent` 的 `if (nw->IsDisabled()) return;` 被丢弃 ⇒ 表现为「按不下去」；
- 另一可能：`R3RNumYards()==0` 时下拉表为空，`ShowDropDownList()` 弹出空表 ⇒ 视觉上等同无反应。
两者都需在实现时一并处理：**能选就必须有内容，不能选就明确灰显并给提示**。

### 3.2 第 2 条：分配流程改造

现状（待复核）：窗口顶部 `WID_SCHED_ASSIGN` 附近有一个「列车」下拉框（选目标列车）+ 「分配」按钮。
目标：去掉下拉框；点「分配」时弹出**带列车图像**的列车列表窗口供点选。

注意：本 fork 的车辆列表行带图像（`vehicle_gui.cpp` 的 `DrawVehicleImage`），可复用
`ShowVehicleListWindow` 的思路，但需要「点选即回调」的语义，不能直接开普通列表窗口。
候选实现：
- (A) 新增一个轻量「选择列车」窗口（列表 + 图像 + 点击回调），复用 `GUIVehicleGroup`/`VehicleList` 数据；
- (B) 复用调度池窗口里已有的「模板车下拉」风格，但要求图像 ⇒ 需自绘，选 (A)。

### 3.3 第 3 条：公开标记（小旗子）

公开标志在 `R3RScheduleMeta::public`（第 418 轮新增）。列表行文本走 `R3RScheduleLineString()` 一类函数。
加旗子：优先用现有 sprite（路签公开用的是哪张图需查），或在行首渲染一个 sprite（`DrawSprite`）
+ 文本缩进。需要确认 `SPR_*` 名称。

### 3.4 第 4 条：文案与新建行为

- `STR_R3R_SCHEDULE_EDIT` → 「编辑排程」/「Edit schedule」；
- `STR_R3R_SCHEDULE_PREVIEW` → 「查看使用者」/「Users」（第 418 轮刚加的按钮）；
- 新建：`STR_R3R_SCHEDULE_NEW` 目前弹 `ShowQueryString`（`STR_R3R_SCHEDULE_RENAME_TITLE`），
  改为**直接建空的无主排程**（名字空、owner 无效、私有）。需确认「无主」的表示：owner 用 INVALID_COMPANY？
  以及谁能看到/删除无主排程、能否分配（分配即认领 owner？）。**口径待玩家确认**。

### 3.5 第 5 条：及时刷新

`CmdCreateR3RSchedule` 成功后应 `InvalidateWindowClassesData(WindowClass::...)` + 选中新行。
第 418 轮已有 `InvalidateWindowClassesData` 的模式，但列表可能只在 `OnInvalidateData` 里重建且带了
「已有选中项就不重建」的短路 ⇒ 需要核对。

## 四、待玩家确认的口径（C 系列）

- **C-1**（第 4 条）「新建即空无主排程」：名字留空显示 `未命名排程`，owner = 无（`INVALID_COMPANY`），
  仅本窗口可见？还是所有公司都可见但不可用？分配时是否把 owner 认领成本公司？
- **C-2**（第 2 条）点选列车列表的范围：本公司全部列车？还是仅「有排程/无排程」过滤？
  是否需要"多选"（一次把排程分配给一列车）？
- **C-3**（第 3 条）旗子位置：排程名之前（用户原话）＋是否需要 hover 提示「公开」。

以上问题同时写入 KI-592..KI-596 的条目里，玩家按编号回填。

## 五、取证记录（逐步追加）

### 5.1 KI-592 `invalid parameter` + 按钮按不下去（车站排程池「选场」）

- 显示层：`src\station_gui.cpp:3776`
  `if (widget == WID_SD_YARD_SEL) return GetString(STR_R3R_DISPATCH_POOL_YARD_SEL, this->YardLabel(this->yard));`
  而 `src\lang\english.txt:4043` 原文是 `STR_R3R_DISPATCH_POOL_YARD_SEL :{BLACK}Yard: {STRING}`。
  `{STRING}` 的参数必须是 `StringID`，此处传进去的是**已渲染的 `std::string`** ⇒ 运行时渲染成
  `(invalid parameter)`。**结论：成立，是字符串参数类型错，不是文本缺失。**
- 交互层：同文件 `OnPaint()` 里对 `WID_SD_YARD_SEL` 调过 `SetWidgetDisabledState(..., !can_edit)`，
  而 `DispatchLeftClickEvent()` 对 disabled 控件直接 `return` ⇒ 非本公司可编辑时**根本收不到点击**，
  这就是玩家说的「按钮按不下去」。另外无场时下拉列表为空，点了也没有任何反馈（像死键）。
- 修法：`{STRING}` → `{RAW_STRING}`；删掉这条 disabled；无场时弹
  `STR_R3R_DISPATCH_POOL_NO_YARD`（本轮新增串）。**选场是只读操作，本来就不该受编辑权限约束。**

### 5.2 KI-594/595/593/596（公司排程管理界面）

- KI-595（新建直建空排程）：`src\r3r_schedule_gui.cpp` 的 `WID_SCHED_NEW` 原实现是弹
  「空 / 复制某排程」下拉（`STR_R3R_SCHEDULE_NEW_EMPTY` / `NEW_SEL`）⇒ 与玩家要求不符。
- KI-593（先选排程→分配→带图的列车页）：原 `WID_SCHED_ASSIGN` 依赖窗口顶部
  `NWID_BUTTON_DROPDOWN(WID_SCHED_TARGET_SEL)` 选出的 `this->target`；`OnPaint` 里
  `SetWidgetDisabledState(WID_SCHED_ASSIGN, ... || GetTargetVehicle() == nullptr)` ⇒ 没有目标车就永远不可点。
- KI-594（自家公开排程加小旗）：行绘制只画文本，没有「公开」视觉标记；`IsR3RSchedulePublic()` 现成可用，
  `SPR_FLAG_VEH_RUNNING` 现成可用（路签用的同一套旗）。
- KI-596（新建后要重开窗口才看到）：`src\order_cmd.cpp` 的 `CmdR3RCreateLooseSchedule` /
  `CmdR3RDeleteLooseSchedule` 结尾**没有** `InvalidateWindowClassesData(WindowClass::R3RSchedule)`，
  rename/public 两条命令有 ⇒ 只有增删不广播，窗口自然不会刷新。**结论：成立，漏广播。**

## 六、实现落地

（第 419 轮全部已改源码，未新增 .cmd 脚本）

1. **KI-592**（`src\station_gui.cpp` + 两个 lang）
   - `english.txt` / `simplified_chinese.txt`：`STR_R3R_DISPATCH_POOL_YARD_SEL` 的 `{STRING}` → `{RAW_STRING}`；
     新增 `STR_R3R_DISPATCH_POOL_NO_YARD`（本站还没有场，请先在车站窗口里新建）。
   - `OnPaint()`：删掉 `SetWidgetDisabledState(WID_SD_YARD_SEL, !can_edit)`（`station_gui.cpp:3785` 一带的注释同步改写）。
   - `OnClick()` 的 `WID_SD_YARD_SEL` 分支：`st == nullptr || st->R3RNumYards() == 0` 时
     `ShowErrorMessage(STR_R3R_DISPATCH_POOL_NO_YARD, ..., WarningLevel::Warning)` 并 `break`，不再弹空下拉。
2. **KI-593**（`src\r3r_schedule_gui.cpp` + `src\widgets\r3r_schedule_widget.h` + lang）
   - 删掉窗口顶部 `NWID_BUTTON_DROPDOWN(WID_SCHED_TARGET_SEL)`，嵌套树只剩「列表 + 滚动条」。
   - `OnPaint()`：不再设置 `WID_SCHED_TARGET_SEL` 的 disabled；`WID_SCHED_ASSIGN` 的判据变成
     「可编辑 + 有选中排程 + `R3RScheduleAccessibleBy`」，与目标车无关。
   - `WID_SCHED_ASSIGN` 点击 → `ShowR3RScheduleAssignWindow(this->company, ol->index)`。
   - 新增 `R3RScheduleAssignWindow`（`WindowClass::R3RSchedule`，**window number = 1**，避免和池窗口的
     number 0 撞车）：列表每行 `DrawVehicleImage(..., EIT_IN_LIST, 0)` 画列车缩略图（宽度上限=行宽一半，
     逐节累加 `GetDisplayImageWidth()`），右侧 `STR_R3R_SCHEDULE_ASSIGN_ROW`（车名 + 它当前排程名）。
     点一行 → `Command<Commands::AssignR3RSchedule>::Post(..., v->index.base(), ol->index.base())` → 关窗。
     空列表只可能出现在还没有主营列车时，显示 `STR_R3R_SCHEDULE_ASSIGN_PICK_EMPTY`。
   - 行高/命中一律取列表控件自己的 `resize_y`（KI-262 家族教训），不碰窗口级 `resize.step_*`。
   - 新增串：`STR_R3R_SCHEDULE_ASSIGN_PICK_TITLE`（`{RAW_STRING}`）、`_TOOLTIP`、`_EMPTY`、`_ROW`（两个 `{RAW_STRING}`）。
3. **KI-594**（`src\r3r_schedule_gui.cpp`）
   - `DrawWidget(WID_SCHED_LIST)`：`ol->IsR3RSchedulePublic() && IsOwnSchedule(ol)` 时在名称前
     `DrawSprite(SPR_FLAG_VEH_RUNNING, PAL_NONE, left, y + (step - flag.height)/2)`，`left += flag.width + 2`。
     （用 `GetSpriteSize()` 取尺寸，垂直居中。）工具提示同步说明这面小旗。
4. **KI-595**（`src\r3r_schedule_gui.cpp`）
   - `WID_SCHED_NEW` → 直接 `Command<Commands::CreateR3RLooseSchedule>::Post(STR_ERROR_CAN_T_DO_THIS, 0)`，
     并置 `pending_select_new = true`；`Rebuild()` 用上一轮 `known_rows` 与当前 `rows` 的**差集**认出新建的那条
     并选中（命令下一 tick 才生效，所以必须靠「新出现的行」而不是「最后一个下标」）。`STR_R3R_SCHEDULE_NEW`
     /`_NEW_TOOLTIP` 文案改为「直接新建一个空的无主排程」。
5. **KI-596**（`src\order_cmd.cpp`）
   - `CmdR3RCreateLooseSchedule` 在 `list->Initialize(nullptr)` 之后、
     `CmdR3RDeleteLooseSchedule` 在 `list->FreeChain(false)` 之后，各补
     `InvalidateWindowClassesData(WindowClass::R3RSchedule);`
6. **文案**（KI-595/594 顺带）：`STR_R3R_SCHEDULE_EDIT` = `Edit schedule`/`编辑排程`；
   `STR_R3R_SCHEDULE_PREVIEW` = `Users`/`查看使用者`。

### 6.1 与玩家口径的对应关系（C 系列回填）

- C-1（新建即空无主）→ **采纳**：`CreateR3RLooseSchedule` 建的就是 owner 为空的散排程，名字留空显示
  `STR_R3R_SCHEDULE_UNNAMED`；不再提供「复制现有排程」入口（旧下拉已删）。
- C-2（选车范围）→ **本公司全部主营列车（`IsPrimaryVehicle()`）**，不做过滤、不做多选；点一辆即完成。
- C-3（旗子位置）→ 名称之前，与路签公开标记同一张 `SPR_FLAG_VEH_RUNNING`。

## 七、构建自证

复用既有入口 `R3R_fullbuild.cmd inc detach`（**未新建任何 .cmd**）。

### 7.1 第一次（因改头文件退化为全量）：失败于一个缺失的 include

- `build\R3R_incbuild.guard.log`：`REMOVED 620 object file(s) - upgrading this build to a FULL rebuild`
  （`src\widgets\r3r_schedule_widget.h` 02:51:05 新于最新 obj，符合 KI-183 规则）。
- 708 步全量跑到 661 步时红：
  `src\r3r_schedule_gui.cpp(565): error C3861: "ScaleGUITrad": 找不到标识符`
  `src\r3r_schedule_gui.cpp(584): error C3861: "ScaleGUITrad": 找不到标识符`
  `build\R3R_incbuild.done` = `EXIT_CODE=1`（03:16:57）。
- 根因：`ScaleGUITrad()` 声明在 `src\zoom_func.h:117`，本文件原先没 include（此前本文件不用缩放函数）。
  修法：`#include "zoom_func.h"`（`r3r_schedule_gui.cpp:42`）。**不涉及任何行为改动。**

### 7.2 第二次（增量）：通过

- `build\R3R_incbuild.guard.log` = `GUARD: incremental is safe (no header/lang file is newer than the newest object)`
- 100 步（补齐第一次中断后未编到的尾巴）：`[100/100] Linking CXX executable openttd.exe`
- `build\R3R_incbuild.done` = `EXIT_CODE=0`（2026-11-11 03:26:33）
- 日志 `error C*` / `fatal error` / `FAILED:` 计数 = 0
- 时间戳链：
  - `src\r3r_schedule_gui.cpp` 03:18:01 → `r3r_schedule_gui.cpp.obj` 03:18:48
  - `src\station_gui.cpp` 02:45:56 → `station_gui.cpp.obj` 03:20:53
  - `src\order_cmd.cpp` 02:46:03 → `order_cmd.cpp.obj` 03:16:11
  - `build\generated\table\strings.h` 02:56:07 → `strings.cpp.obj` 03:21:11
  - `build\openttd.exe` **2026-11-11 03:26:18，52 777 472 B**（晚于全部 obj 与源码）
- 语言串入库：`strings.h` 有 `STR_R3R_DISPATCH_POOL_NO_YARD = 0xC94`、
  `STR_R3R_SCHEDULE_ASSIGN_PICK_TITLE = 0xCA8`、`_TOOLTIP = 0xCA9`、`_EMPTY = 0xCAA`、`_ROW = 0xCAB`、
  `STR_R3R_SCHEDULE_EDIT = 0xCB7`、`_EDIT_TOOLTIP = 0xCB8`、`STR_R3R_SCHEDULE_PREVIEW = 0xCB9`、`_PREVIEW_TOOLTIP = 0xCBA`；
  `build\lang\simplified_chinese.lng` 02:56:03、229 207 B。
- `read_lints`：`r3r_schedule_gui.cpp` / `widgets\r3r_schedule_widget.h` / `station_gui.cpp` / `order_cmd.cpp` 合计 **0 条**。
- 本轮**只编内测树**（探针 ON），发布树 `build-release` 未动，仍停留在第 418 轮那一版。

### 7.3 状态

**已实现 + 已编译（EXIT_CODE=0）**，游戏内复测待做；8 条复测判据见 KI 文件「第 419 轮」小节末尾的表格。


