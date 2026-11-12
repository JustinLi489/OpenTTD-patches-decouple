# R3R 第 422 轮临时分析报告（2026-11-12）：无主排程编辑器崩溃 + 无主/有主编辑差异 + 等待调度池不分配

> 状态：**取证中（尚未改码、尚未构建）**。本文件按「先落盘再取证」铁律建立，后续每得一条读数即追加。
> 关联：`R3R_KNOWN_ISSUES.md` 的 `## 第 422 轮（2026-11-12）`；前序 `R3R_schedule_editor_round421_memo.md`（KI-600 E-2 编辑器落地）、`R3R_round420_six_issues_memo.md` §3.7。

---

## 零、玩家三问（原话登记）

1. **编辑无主排程时，游戏出现了崩溃。**
2. **请告诉我编辑无主排程和有主排程的区别在哪。**
3. **我在一个存档里面尝试让列车等待分配调度。但是列车迟迟没有分配到（站场调度池确定有调度）。**

⇒ 对应条目：Q1 → KI-604（高，崩溃）；Q2 → 口径答复（非缺陷，见 §二）；Q3 → KI-605（高，功能不生效）。

---

## 一、现状基线（本轮取证起点）

| 判据 | 读数 | 来源 |
|---|---|---|
| 玩家所用 exe | **2026-11-11 21:15:24，52 807 680 B** | `Get-Item build\openttd.exe` |
| 该 exe 对应轮次 | 第 421 轮修复版（含 KI-600 E-2 编辑器 + 语言包版本对齐） | `build\R3R_incbuild.done` = `EXIT_CODE=0` |
| 运行日志 | `build\R3R_debug.log`，**23 964 564 B / 161 355 行**，mtime **2026-11-12 17:00:03** | 实测 |
| 崩溃日志 | 工作区内 `crash-*.log` **零命中** | `Get-ChildItem -Recurse -Filter crash-*.log` |
| 源码状态 | `src\r3r_schedule_gui.cpp` 21:00:34 / `src\order_gui.cpp` 20:19:04（第 421 轮版） | 上一轮实测 |

⇒ 日志是「新 exe」产生的，采样有效；崩溃日志不在工作区，**待玩家提供路径/文件**（默认应在 `%USERPROFILE%\Documents\OpenTTD\crash\`）。

---

## 二、Q2 答复：编辑无主排程 vs 编辑有主排程（差异表）

口径来源：`src\order_gui.cpp` 的 `R3RScheduleEditorWindow`（:5478 起） vs 原生 `OrdersWindow`（:960 起）；分流入口 `src\r3r_schedule_gui.cpp:402-414`。

| 维度 | 有主排程（原生 `OrdersWindow`） | 无主排程（`R3RScheduleEditorWindow`，第 421 轮新增） |
|---|---|---|
| 打开方式 | 排程池「编辑」→ `ShowOrdersWindow(first_vehicle)` | 排程池「编辑」→ `ShowR3RScheduleEditor(ol)` |
| 身份锚点 | **`VehicleID`**（窗口号=车辆号） | **`OrderListID`**（窗口号=排程号） |
| 命令投递 | `Command<...>::Post(vehicle_id, ...)`，走车辆命令族（`CmdModifyOrder`/`CmdInsertOrder`/`CmdDeleteOrder`/`CmdMoveOrder`） | 第 421 轮新增四个**列表命令**：`InsertR3RLooseOrder` / `DeleteR3RLooseOrder` / `MoveR3RLooseOrder` / `DuplicateR3RLooseOrder`（`src\command_type.h:802-805`），全部 `CommandType::OtherManagement` + `_NT` |
| 操作对象 | 该车辆当前借用的那张 `OrderList` | 该 `OrderList` 本体（不依赖任何车辆） |
| 可做 | 全部订单编辑 + **改装(refit)** + **跳转到当前订单** + **当前订单高亮/箭头** + 时刻表 + 共享排程 | **只有增 / 删 / 复制 / 上移 / 下移** |
| 不可做（本轮刻意不提供） | — | refit、跳转订单、当前订单标记、时刻表编辑 —— 这些都要真实车辆语义 |
| 列表渲染 | `DrawOrderString(v, ...)`，`v` 非空 ⇒ 可画「当前订单」箭头 | `DrawOrderString(nullptr, ...)`，`vtype` 固定 `VehicleType::Train`，**永不画箭头**（`src\order_gui.cpp:1027-1028`） |
| 无订单时 | 空白列表 | 居中占位串 `STR_R3R_SCHEDULE_EDITOR_EMPTY` |
| `Add` 内容 | 原生下拉（含 GOTO/装载/条件跳转/交接等全部类型） | 只列**本公司的、有设施的车站/路点**（`StationID` 当 `index` 回传） |
| 插入位置 | 由点击的订单行决定 | 「当前选中项的下一行」，未选中则追加到末尾 |
| 同一排程多开 | 同一车辆只能一个订单窗口 | `BringWindowToFrontById` 保证同一 `OrderListID` 只有一个窗口 |
| 权限 | 车辆所属公司 | `OrderList` 所属公司（`CanEdit()`） |
| 车辆被借用中 | 正常编辑（改的是借用表） | 同样可编辑 —— **这正是崩疑点所在，见 §三** |

一句话：**有主 = 借车辆的手改表；无主 = 直接改表**。前者所有按钮都能点，后者只提供不依赖车辆的五个动作。

---

## 三、Q1 崩溃：待取证清单（**猜测根因（待验证）**）

### 3.1 疑点登记（REG-422-A）

- **期望**：`R3RScheduleEditorWindow` 的每个按钮都只碰 `OrderList` 本体，不碰 `Vehicle`。
- **实测**：玩家在「编辑无主排程」时崩溃。
- **为什么存疑**：第 421 轮只做过编译验证，**游戏内零复测**（见 `R3R_KNOWN_ISSUES.md` 第 421 轮「待复测 9 条」）。代码里有几条可疑路径尚未核对：

| # | 可疑点 | 位置 | 风险 |
|---|---|---|---|
| A | `Delete` 分支的 `GetFirstSharedVehicle()` / `GetNumVehicles()` —— 排程池里的 `OrderList` 可能是**已被 `FreeChain` 释放**或**从未 `Initialize`** 的表，`first_shared` 悬垂 ⇒ 解引用 | `src\r3r_schedule_gui.cpp:434-447` | 高（0 地址读） |
| B | `CanEdit()` 用的 `CompanyID` 若来自 `first_shared` / 车辆，无车时可能取到 `Invalid` | `src\r3r_schedule_gui.cpp` | 中 |
| C | `DrawOrderString(nullptr, ...)` 内部是否仍有 `v->` 解引用（第 421 轮只把首参放宽为可空，未逐行核查函数体） | `src\order_gui.cpp:1027` + `DrawOrderString` 函数体 | **高（最可疑：渲染期崩溃）** |
| D | 编辑器窗口的 `schedule`(`OrderListID`) 在排程被删/被合并后失效，窗口未随 `WindowClass::R3RScheduleEditor` 关闭 | `ShowR3RScheduleEditor` / `InvalidateWindowData` | 中 |
| E | `InsertR3RLooseOrder` 载荷 `InsertLooseOrderCmdData` 含 `Order`（内部有 `std::string`/指针），若命令层把它当普通 payload 拷贝或 `DoCommandP` 期间重入 | `src\order_gui.cpp:5654` | 中 |

### 3.2 需要的证据（待玩家提供）

1. `crash-YYYYMMDDTHHMMSSZ.log`（含 `Exception handler` 段与调用栈）—— **最关键**。
2. 崩溃前最后一次操作的描述（点了哪个按钮：编辑 / Add / Delete / Duplicate / MoveUp / MoveDown；或只是打开窗口就崩）。
3. 该排程是「本次会话新建」还是「旧存档带入」。
4. `build\R3R_debug.log` 崩溃时刻的尾部（本文件 §四 已有一次尾部，但那段时间看起来是另一场景）。

---

## 四、Q3 等待调度池不分配：现场读数（已取）

### 4.1 日志尾部（`build\R3R_debug.log` 末 120 行）关键形态

```
SKIP-STOPPED veh=24 order=0 real=1 spd=0 tile=452,76 parked=0 front=1 nord=3 idx=0 tt=0 blocked=753  wclr=0
SKIP-STOPPED veh=27 order=0 real=1 spd=0 tile=452,76 parked=0 front=1 nord=3 idx=0 tt=0 blocked=911  wclr=0
...
SKIP-STOPPED veh=24 order=0 real=1 spd=0 tile=452,76 ... blocked=15121 wclr=0
SKIP-STOPPED veh=27 order=0 real=1 spd=0 tile=452,76 ... blocked=15121 wclr=0
```

读数解读：

| 字段 | 值 | 含义 |
|---|---|---|
| `order=0` | `OT_NOTHING` | 当前订单类型为空 —— 与「等待被派单」形态一致 |
| `real=1` | 真实订单索引 1 | 表有 3 条（`nord=3`），已推进到第 2 条 |
| `spd=0` | 静止 | 从未起步 |
| `blocked` | 753 → 15121 单调累加 | **阻塞计数器持续增长，从未清零** |
| `parked=0` / `wclr=0` | 未停放、未清等待 | 不是 KI-169 的停放态 |
| `nord=3` | 3 条订单 | 排程存在 |

⇒ 两列车（24、27）长期停在同一格 `452,76`，`blocked` 一路涨到 15121 且从未被派单。**「站场调度池有调度」但列车没被分配** —— 与玩家描述逐字吻合。

### 4.2 疑点登记（REG-422-B）

- **期望**：站场调度池里有可用调度时，等待中的列车应被分到某条调度并被改写订单。
- **实测**：`blocked` 单调增长，`order=0` 恒不变，两车同格同状态。
- **为什么存疑**：`blocked` 只增不减说明走到了「阻塞/等待」分支后**没有任何路径把它清掉**；而 `real=1` 说明订单索引没被推进，可能停在一条 `OT_NOTHING`/`OT_WAIT_*` 上永远等不到事件。

### 4.3 待验证方向（猜测根因，待验证）

| # | 猜测 | 支撑 | 待验证判据 |
|---|---|---|---|
| B-1 | 等待方停在 `452,76` 但**不是** `OT_WAIT_COUPLE`（`order=0`），因此调度池派单的「候选筛选」判据（要求 `OT_WAIT_*` 或特定订单类型）永远选不中它 | `order=0` 是硬读数 | 找调度池派单的候选筛选函数，确认它要求什么订单类型 |
| B-2 | 两车**同格 452,76** ⇒ 派单后目标格被占 / 互锁（同 KI-330 的「同格误判」家族） | 24/27 tile 完全相同 | 派单日志（若有）是否出现 target 冲突 |
| B-3 | `blocked` 计数器本身是「本 tick 没能推进」的计数，**没有任何超时/兜底**，于是永久停摆 | 15121 单调增长 | 搜 `blocked` 的写入点与清零点 |
| B-4 | 站场调度池的「有调度」与列车的「要调度」两侧**判据不对称**（一个按站场 ID、一个按车站 tile/场号） | 第 421 轮删场重编号经验 | 核对两侧取场号的方式 |

---

## 五、下一步（本轮计划）

1. 读 `DrawOrderString` 函数体，确认 `nullptr` 首参是否安全 → 直接判 Q1 的 C 疑点。
2. 读 `GetFirstSharedVehicle` / `GetNumVehicles`，确认无主表安全性 → 判 Q1 的 A 疑点。
3. 搜站场调度池派单的候选筛选与 `blocked` 计数写入/清零点 → 判 Q3。
4. 得到结论后再改码、构建、回填本文与 KI 条目。

---

## 六、未确认项（U）

- U-1：崩溃是否与「编辑器刚打开就崩」还是「点了某个按钮才崩」有关（缺崩溃栈）。
- U-2：崩溃场景的排程是新建还是存档带入（影响 A 疑点的「未 Initialize」假设）。
- U-3：Q3 的存档里，`452,76` 属于哪个站场、该站场调度池有几条调度、列车订单 3 条具体是什么类型（需要玩家描述或存档）。
- U-4：`blocked` 的确切语义与写入点尚未核对。

## 七、KI-604 崩溃根因做实（崩溃栈已取得，2026-11-12 本轮续）

### 7.1 崩溃日志

路径：`C:\Users\冯洁敏\Documents\OpenTTD\crash-20261111T135654Z.log`（12 813 B，写入时间 2026-11-11 21:57:11）。
头信息：`Crash in progress. Last saved game: '(null)'` / `Build date: Nov 11 2026 20:21:20` / 版本串 `r3r-stable-2026-11-08-m (2)`。
⇒ 这是 2026-11-11 21:56:54Z（本地 21:56）那次会话，产生者就是第 421 轮修复版 exe，与本报告「编辑无主排程时崩溃」同源。

### 7.2 崩溃栈（原文）

```
[00] DrawOrderString(const Vehicle* const, ...)  src/order_gui.cpp:1624
[01] R3RScheduleEditorWindow::DrawWidget(...)    src/order_gui.cpp:5588  (DrawWidget+769)
[02] Window::OnPaint(...)                        src/window.cpp:1178     (OnPaint+309)
[03] DrawOverlappedWindowForAll(...)             src/window.cpp:2255
```

关键行：
- `Faulting instruction address: ...+0x2C` / `Fault address: 0x3C`
- `Exception C0000005 (Access violation) reading 0x3C`

⇒ **空指针解引用**。`0x3C` 正是 `Vehicle` 上 `type` 成员的字节偏移（`nullptr + 0x3C`），即 `v->type` 且 `v == nullptr`。

### 7.3 与源码逐字对上

当前 `src\order_gui.cpp:1633`（本文件第 422 轮改码前为 **:1624**，与崩溃栈行号完全一致）：

```cpp
/* Check range for aircraft. */
if (v->type == VehicleType::Aircraft && Aircraft::From(v)->GetRange() > 0 && order->IsGotoOrder()) {
```

该处位于 `DrawOrderString()` 的**公共尾段**（switch 之后、任何订单类型都会走到），而第 421 轮新增的 `R3RScheduleEditorWindow::DrawWidget` 每帧都以 `DrawOrderString(nullptr, order, i, y, ...)` 绘制无主排程 —— **只要无主排程里有一条订单，编辑器一开就必崩**，与玩家「编辑无主排程时崩溃」逐字吻合。

崩溃前最后三条命令（日志 `Commands:` 段）：

```
cmd: 101   CmdR3RInsertLooseOrder | orderlist 3, 65535, order: t: 21, f: 0, d: 0, rc: FE
0x44       CmdR3RCreateLooseSchedule
0x11       CmdPause
```

⇒ 玩家先新建无主排程、插入一条订单，随即打开编辑器 → 首帧绘制即崩。

### 7.4 静态扫描：`DrawOrderString` 内全部空指针点（5 处，已全部加固）

| 位置（改码前行号） | 代码 | 后果 |
|---|---|---|
| :1358-1359 | `if (schedule_id < v->GetEffectiveOrders()->GetScheduledDispatchScheduleCount())`（`OT_CONDITIONAL` + `DispatchSlot`） | 含「调度槽条件」订单即崩 |
| :1445 → `OrderStringForVariable(v, ocv)` 内 :951-952 | `if (ocv == ... && v->type != ...)` | 含 `VehicleInSlot*` 条件订单即崩 |
| :1543 | `GetSlotGroupWarning(..., v->owner)` | `OT_GOTO_STATION` 且要报槽组警告时崩 |
| **:1624** | `if (v->type == VehicleType::Aircraft && ...)` | **本次崩溃点，任何订单类型都走到** |
| :1643 | `if (v->vehicle_flags.Test(VehicleFlag::ScheduledDispatch) && ...)` | 调度表订单的附加标注，修完 1624 后会接着崩 |

### 7.5 修法（仅 `src\order_gui.cpp`，未碰 `src\*.h`）

1. `OrderStringForVariable()` 两处 `v->type` 加 `v != nullptr`（无车时不做车型收窄，只给变量名）。
2. `:1358` 改为 `v != nullptr && schedule_id < ...`；无车时回落「schedule N」文案（无主排程没有车辆侧的调度表可取名）。
3. `:1543` 改为 `(v != nullptr) ? GetSlotGroupWarning(..., v->owner) : STR_NULL`（无主排程没有属主公司可校验槽组）。
4. `:1624` 改为 `v != nullptr && v->type == VehicleType::Aircraft && ...`（无车时不画航程越界警示）。
5. `:1643` 改为 `v != nullptr && v->vehicle_flags.Test(...) && ...`。

口径：**无车主时，一切依赖车辆状态/属主的「附加标注」一律降级为不显示，订单本职文案（类型 + 目的地 + 参数）照常绘制**。不改 `DrawOrderString` 签名，不改有主路径的任何行为（`v != nullptr` 时判据与改码前逐字等价）。

### 7.6 复测判据

1. 新建一条无主排程 → 插入普通 `GOTO_STATION` 订单 → 打开编辑器窗口**不崩**，订单行正常显示。
2. 无主排程插入 `OT_CONDITIONAL`（调度槽条件）→ 打开编辑器不崩，条件行显示「schedule N」而非槽名。
3. 无主排程插入 `VehicleInSlot*` 条件 → 不崩。
4. 有主排程（列车订单窗口）绘制逐字不变：倒计时箭头、当前订单箭头、航程警示、调度表后缀全部照旧。
5. 编辑器内「上移/下移/复制/删除」四个按钮各点若干次，窗口不崩、行内容随之更新。

## 八、KI-605「等待分配」迟迟不分配 —— 根因做实（2026-11-12 本轮续）

### 8.1 现场读数

`build\R3R_debug.log` 尾部（161348~161355 行）连续两组，唯一形态：

```
SKIP-STOPPED veh=24 order=0 real=1 spd=0 tile=452,76 parked=0 front=1 nord=3 idx=0 tt=0 blocked=13857 wclr=0
SKIP-STOPPED veh=27 order=0 real=1 spd=0 tile=452,76 parked=0 front=1 nord=3 idx=0 tt=0 blocked=13857 wclr=0
... blocked 单调 13857 -> 14489 -> 15121
```

全日志 `DISP-POOL-*`（WAIT / BIND / RETURN）**零命中** ⇒ 领取钩子从头到尾一次都没执行到判定内部。

### 8.2 判据链（`SKIP-STOPPED` 是 `TrainLocoHandler` 的早退门，不是订单探针）

`src\train_cmd.cpp:22879`（改码前行号）：

```cpp
if (!r3r_pending_depot_couple && consist->vehstatus.Test(VehState::Stopped) && consist->cur_speed == 0) {
    ...
    R3RDbgWrite("SKIP-STOPPED veh=%d order=%d real=%d ... blocked=%u wclr=%u\n", ...);
    return true;   // <- 直接返回，本 tick 什么都不做
}
```

同一函数的 `TrainDispatchHandler(consist)` 原调用点在 `:23792`（`if (j < adv_spd) { if (cur_speed == 0) { ... } }` 内部）—— **在早退门之后**。`blocked=15121` 就是被这道门连续挡回的 tick 数 ⇒ 等待分配的列车（被停止的那一辆）永远走不到领取钩子。**这是第一重死因。**

### 8.3 第二重死因：钩子只看 `current_order`

`TrainDispatchHandler` 原判据 `if (!v->current_order.IsType(OT_GOTO_STATION)) return;`。
而 `current_order` 只是「此刻正在执行」的那一份拷贝：列车完成装卸后它是 `OT_NOTHING`（现场 `order=0`），真正带「等待分配」标记的那条命令仍挂在 `cur_real_order_index`（现场 `real=1`）上。
⇒ 即使把调用点挪到门之前，只看 `current_order` 依然不成立。

### 8.4 修法（仅 `src\train_cmd.cpp`，未碰 `src\*.h`）

1. `TrainDispatchHandler()` 判据改为「先看 `current_order`，不是 `OT_GOTO_STATION` 就回退到 `GetEffectiveOrders()[cur_real_order_index]`」，两者都必须 `IsType(OT_GOTO_STATION)` 且 `HasR3RWaitDispatch()`；目的地站仍取这条命令的 `GetDestination().ToStationID()`。
2. 领取成功（借用池表成功）后新增 `vehstatus.Reset(VehState::Stopped)` —— 等待分配的列车本来就是被「停止」按住的，不解除则分配对它毫无效果；`DISP-POOL-BIND` 补 `unstop=%d`。
3. `TrainDispatchHandler(consist)` 调用点从 `:23792` **上移到 `:22879` 早退门之前**（原位置改为注释说明，不再重复调用）。

口径：只有「停稳 + 停在目标站格上 + 该索引命令确为带等待分配标记的到站命令」三条同时成立才会绑定，普通被停列车、普通到站命令一字不动。

### 8.5 复测判据

1. 站场调度池里放一条有表、有余量的调度，把列车停在池站的站台上（停止状态），应出现
   `DISP-POOL-BIND veh=N host=M st=S yard=Y orders=K bk=/ unstop=1`，且该列车随即自行发车执行池表。
2. 池里没有空坑位时应出现 `DISP-POOL-WAIT veh=N st=S yard=Y count=0`（每列车只喊一次），车继续原地等。
3. 池表最后一条跑完应出现 `DISP-POOL-RETURN veh=N host=M ...`，列车回到自己表的「分配点下一条」。
4. 负判据：没有「等待分配」标记的普通到站命令、没有停在目标站格上的列车、正常跑车的列车，都不得出现 `DISP-POOL-BIND`。
5. 负判据：被玩家停止、但订单里没有等待分配标记的列车，仍然保持停止不动（不得被 R3R 自行解除停止）。

### 8.6 未确认（留给复测）

- U-1：现场 veh=24/27 同格 `452,76`、两节都是 `front=1`，是否同一场里的两条独立链（同格叠放）——不影响本次判据。
- U-2：`blocked` 计数只增不减属探针语义（早退门挡回次数），不是缺陷。

## 九、第 422 轮构建自证（两轮增量，全部通过）

本轮两处改动分属两个 `.cpp`，改动时间不同，因此**分两次增量构建**（护栏两次均判 `incremental is safe`，未触发全量）。构建入口一律复用工作区既有的 `R3R_fullbuild.cmd inc detach`，**未新建任何 `.cmd`**。

### 9.1 第一次构建（KI-604，`src\order_gui.cpp`）

| 项 | 读数 |
|---|---|
| `build\R3R_incbuild.done` | `EXIT_CODE=0` @ 2026-11-12 17:41 |
| `order_gui.cpp` | 17:31 |
| `order_gui.cpp.obj` | 17:33（晚于源码 ✅） |
| `build\openttd.exe` | 17:41 / 52 807 680 B |

### 9.2 第二次构建（KI-605，`src\train_cmd.cpp`）

启动前用 `tasklist` 确认 `openttd.exe` 未在运行（否则闸门写 `EXIT_CODE=97`）。

| 项 | 读数 |
|---|---|
| `build\R3R_incbuild.done` | `EXIT_CODE=0` @ 2026-11-12 17:48:34 |
| 日志尾部 | `[2/3] Building CXX object ...\train_cmd.cpp.obj` → `[3/3] Linking CXX executable openttd.exe` |
| `src\train_cmd.cpp` | 17:35 |
| `train_cmd.cpp.obj` | 17:47:14（晚于源码 ✅；改前是 11-11 21:07 的陈旧对象） |
| `build\openttd.exe` | 17:48:21 / 52 807 680 B |
| `read_lints` | `order_gui.cpp`、`train_cmd.cpp` 均 0 条 |

### 9.3 关于「exe 字节数与上一版相同」的说明

两次构建后 `openttd.exe` 均为 **52 807 680 B**，与第 421 轮基线（2026-11-11 21:15:24 / 52 807 680 B）字节数一致。**这不代表没编**：判据以时间戳 + 日志 Linking 行 + 内容字面量为准，三者齐全：

- `train_cmd.cpp.obj` 从 11-11 21:07 更新到 11-12 17:47:14；
- 日志有 `[2/3]`→`[3/3] Linking CXX executable openttd.exe`；
- `findstr /M /C:"unstop=%d" build\openttd.exe` **命中** —— `unstop=%d` 是本轮 KI-605 新增的探针字段，旧 exe 里不存在。

### 9.4 当前 exe 判定

**`build\openttd.exe`（2026-11-12 17:48:21）同时包含 KI-604 与 KI-605 两项修复**，可直接用于游戏内复测。复测判据见 KI 条目与本文 §7.6、§8.5。

### 9.5 本轮仍未做

- 发布树 `build-release` 未重编（按口径，日常只编探针内测树；发布版停留在含第 421 轮改动、**不含**本轮两项修复的状态）。
- 游戏内复测两项均未做（需玩家操作）。
- KI-604 的疑点 A/C/D/E 无证据触发，未改码；KI-605 的 U-1/U-2/U-3 未取数。
