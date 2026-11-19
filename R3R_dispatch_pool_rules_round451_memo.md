# R3R 车站调度池「分配规则」— 第 451 轮备忘（2026-11-15）

## 一、本条需求的来源（玩家本次原话）

> 「我需要添加新功能：在车站调度池添加分配的规则。这是我们之前拍板决定的内容，你可以在KI寻找有关”调度分配“的最早的记录」

按指示回溯 KI，最早的调度分配口径记录是 **第 412 轮 §10.1（`R3R_station_dispatch_separation_round412_memo.md:507-511`，玩家 2026-11-10 原话）**，定案表在 §10.2（`:513-522`），实现项在 §10.3（`:524-532`）：

> 「C8，能用"这辆车本身的性能"判断能否分配
> C9，这些表就像占坑位一样，只有固定数量的车能够在分配这些表
> C10，这些分配的表跑完一个回环，就应当交回池子里。
> C11，实际上，这些什么"如果极速达XXX"的分配条件，是写在车站上面的，而不是列车上。比如说，车站里面写：表A的分配条件是列车极速达160，表B的分配条件是最大牵引力达多少千牛。然后一台机车来这个车站领取分配命令，就按照这个规则来。C12应当就清晰了」

定案：
- **C-8 = A**：判断对象是**来领表的这辆车自己的性能**（极速、最大牵引力、可靠性…），达标才发。
- **C-9 = 坑位制**：每份表固定坑位数 N，坑满不发。
- **C-10 = 跑完一个回环交回池**，释放坑位。
- **C-11 = 条件写在车站池条目的「进入条件」上**，列车到站申领时匹配一次；不在列车上、不在班表任意位置。
- **C-12 = B**：条件与表都属于某个车站 ⇒ 只有放它的那个车站能发。

## 二、现状盘点（第 416 轮 KI-586 已落地部分）

已实现：`Station::R3RStationYard::R3RDispatchPoolEntry { VehicleID host; uint16_t limit = 1; uint16_t issued = 0; }` + `std::vector<...> pool`；命令 `AddR3RDispatchPoolEntry / RemoveR3RDispatchPoolEntry / SetR3RDispatchPoolLimit`；`TrainDispatchHandler()` → `R3RDispatchPoolPick` 领表 → `R3RBeginBorrow(..., true)` + `r3r_follow_pool`；`R3RReturnDispatchPool()` 回环归还。界面 = `StationDispatchWindow`（`WindowClass::StationDispatch`）。

⇒ **C-9（坑位）与 C-10（回环归还）已落地；本条需求缺的正是 C-8 + C-11 = 池条目上的「分配规则 / 进入条件」**，以及 §10.3 的 P-1（条件变量缺 Power / MaxTractiveEffort）。

## 三、本轮目标（待实现清单）

1. 池条目新增「分配规则」数据（按 C-11 落在车站池条目上，随存档）。
2. 申领时按规则匹配**来领表这辆车自己的性能**（C-8），不达标不发、继续等或分流到下一份表。
3. 池窗口里可编辑 / 查看该规则；规则可视化（例如「极速 ≥160」）。
4. 条件变量补齐 Power / MaxTractiveEffort（P-1，`OrderConditionVariable` 缺项）。

## 四、待拍板（C-451-x）

- **C-451-1 规则表达能力**：
  - (A) 复用 `OrderCondition` 全套（变量 + 比较符 + 值，可多条 AND）—— 与订单条件同构、玩家已熟悉，但 `OrderCondition` 是订单内嵌结构，搬进池条目要新增存档块字段。
  - (B) 只支持「性能 ≥ 阈值」若干条固定槽（极速 / 最大牵引力 / 功率 / 可靠性 / 载重），实现最简。
  - 建议：**(A)**，因为 C-11 举的例子正是条件式，且后续想加"载重/车重/编组数"零成本。
- **C-451-2 多条规则的匹配顺序**：池内条目按列表顺序、第一条满足即发（并占用坑位），还是"全部规则都满足的表才候选、按优先级取"？
- **C-451-3 不达标时的行为**：原地继续等（与坑满同款 `DISP-POOL-NOSLOT`），还是直接跳过等待分配标记继续跑自己的表？
- **C-451-4 规则编辑入口**：池窗口每行一个「条件」按钮弹独立小窗，还是复用订单条件那套下拉？

（本节等待玩家按编号回填；未回填前先按建议项做，实现后再按答复调整。）

## 五、取证与落地记录

### 5.1 取证（第 451 轮）

- 池条目：`src\station_base.h:1000-1013` `Station::R3RStationYard::R3RDispatchPoolEntry { host, schedule, limit, issued }`；池容器 `pool`；整站池 `r3r_whole_pool`。
- 领表：`src\station_cmd.cpp:2448` `Station::R3RDispatchPoolPick(yard, except_schedule, out_schedule, stats)`；唯一调用点 `src\train_cmd.cpp:8284`（`TrainDispatchHandler`，拿得到 `Train *v`）。
- 归还：`R3RReturnDispatchPool()`（回环交回，第 416 轮已落地），按排程匹配，与规则无关。
- 命令层：`src\station_cmd.h:41-43` 已有 Add/Remove/SetLimit 的 `DEF_CMD_TUPLE_NT`；枚举在 `src\command_type.h:792-794`。
- 存档：`src\sl\station_yard_sl.cpp` SYRD chunk，v8 = `0xFFFFFF07`，池每条 4 word（host/limit/issued/schedule）。
- 条件表达：**不复用** `OrderCondition`（`OrderConditionVariable` 22 个值、`OrderConditionComparator` 5 个比较符），因为订单条件需要 `Order` 宿主与整套条件下拉；池规则只需要「变量 + 比较 + 数值」。⇒ 自建 3 值枚举 + 2 值比较符。
- 单位口径（关键，避免第二次返工）：`{VELOCITY}` 入参 = `PackVelocity(显示速度, type)`；`{POWER}` 入参 = 内部 hp；`{FORCE}` 入参 = **牛顿**（`src\strings.cpp:1039-1044`，`_units_force` 的 SI 系数 0.001）。因此规则阈值统一存「速度 kmh-ish / 功率 hp / 牵引力 kN / 可靠性 %」，显示时牵引力 ×1000 交给 `{FORCE}`。

### 5.2 落地（本轮，已实现）

| 文件 | 改动 |
| --- | --- |
| `src\station_base.h` | 新增 `R3RDispatchRuleVariable`（None/MaxSpeed/Power/MaxTractiveEffort/Reliability）、`R3RDispatchRuleOp`（AtLeast/AtMost）、`R3RDispatchClaimStats`；`R3RDispatchPoolEntry` 加 `rule_var/rule_op/rule_value`；`R3RDispatchPoolSetRule()` 与 `Pick(..., const R3RDispatchClaimStats *stats = nullptr)` 声明 |
| `src\station_cmd.cpp` | `Station::R3RDispatchPoolSetRule()`（阈值钳到 0..65535）；`R3RDispatchRuleMatches()`（`None` 恒真，缺 stats 恒假）；`Pick()` 增加规则过滤；`CmdR3RSetDispatchPoolRule()`（校验 yard/var/op、`CheckOwnership`、`InvalidateWindowData`） |
| `src\station_cmd.h` / `src\command_type.h` | `Commands::SetR3RDispatchPoolRule` + `DEF_CMD_TUPLE_NT(..., CmdDataT<StationID, uint16_t, uint16_t, uint8_t, uint8_t, int32_t>)` |
| `src\train_cmd.cpp` | `TrainDispatchHandler` 领表前填 `R3RDispatchClaimStats`（`GetDisplayMaxSpeed()` / `cached_power` / `cached_max_te/1000` / `reliability`）并传给 `Pick` |
| `src\sl\station_yard_sl.cpp` | SYRD 升 **v9**（`0xFFFFFF08`）：池条目 4→7 word；读取 v2..v8 时规则恒 `None`（旧档行为不变） |
| `src\widgets\station_widget.h` / `src\station_gui.cpp` | 工具条新增一行：规则下拉 `WID_SD_RULE`、比较符 `WID_SD_RULE_OP`、阈值 `-`/`+`（`WID_SD_RULE_DOWN/UP`）；行尾追加规则文本；窗口高 200→214 |
| `src\lang\english.txt` / `simplified_chinese.txt` | 16 条 `STR_R3R_DISPATCH_RULE_*` |

设计要点：
- **一份表一条规则**：池内按排程去重（`R3RDispatchPoolAdd`），所以同一张表在同一场只有一条条目 ⇒ 规则天然唯一。
- **不达标 = 继续扫下一条**（等价于 C-451-3 建议项），全不满足则维持既有 `DISP-POOL-WAIT` 原地等，不新增"拒绝进站"分支。
- **空规则保持旧行为**：`rule_var == None` 时 `Matches()` 恒真，所以既有存档/既有池条目行为逐字不变。

### 5.3 构建自证

- 命令：`R3R_fullbuild.cmd inc detach`（改到 `station_base.h` / `command_type.h` / `station_widget.h`，护栏判为不安全 ⇒ 走了大范围重编）。
- 结果：`build\R3R_incbuild.done = EXIT_CODE=0`；`build\openttd.exe` 2026-11-15 **22:13:56**，**52 936 704 B**（首次 22:11:29 后只再改了 `train_cmd.cpp` 的一行注释，重编复核）。
- `read_lints`：0。
- 语言文件通过 strgen（构建内含，若 `STR_*` 缺失或格式符不匹配会直接失败）。

### 5.4 交付状态

- 状态：**已实现 + 已编译**，游戏内复测待玩家。
- 未做：`build-release\`（发布树）未编；C-451-1..4 的拍板回填。

### 5.5 复测步骤（给玩家）

1. 打开任一车站 → 场窗口 → 「调度池」。
2. 池里挑一条，底部新出现的一行：`规则: 无限制  [>=]  [-] [+]`。
3. 点「规则」下拉选「最高速度」，阈值默认 100，用 `+`/`-` 调到 160。
4. 行尾会显示 `最高速度 >= 160 km/h`；点 `>=` 可切成 `<=`。
5. 让一列最高速度 120 的车来领：领不到（原地等）；换一列 200 的车：能领到。
6. 选回「无限制」⇒ 恢复到改动前的行为。

---

## 六、第 451 轮续（2026-11-15）：规则系统改为「全站一条条件程序」+ 独立窗口

### 6.1 玩家最新原话（决定性口径，覆盖 §五）

> 「我希望规则系统如下：进入车站调度池，点击规则按钮，会单独出现一个窗口而非一个下拉框（直接一个规则管全站场，不要一条调度一个规则）。这个窗口可以照抄可编程信号机条件编辑界面的所有架构。架构如下：条件判断有"如果""否则""或者""elif(请帮我翻译成中文)"。判断条件是所有可编程信号灯的既有条件。判断结果有"允许分配调度XXX""拒绝分配调度XXX"（可以放置多个分配调度）。」

⇒ 推翻 §五 的「每条池条目一个 (变量, 比较符, 阈值)」：
- **D-1** 规则**不再挂在池条目上**，改为「一个站场一份规则程序」（题述"管全站场"）。
- **D-2** 编辑入口 = 池窗口里的「规则」按钮，**弹出独立窗口**（新 `WindowClass`），不是下拉。
- **D-3** 窗口**照抄可编程信号机的条件编辑界面架构**：指令行列表、点行尾插入、条件行（变量+比较符+数值）、缩进显示、最多指令数限制等。
- **D-4** 分支结构要有「如果 / 否则 / 或者（elif 的中文名）」。
- **D-5** 判断结果 = 「允许分配调度 XXX」/「拒绝分配调度 XXX」，**可放置多个**（即结果指令带一个"哪份调度"参数）。

### 6.2 关键词对应（可编程信号机 → 调度规则）

| 可编程信号机 | 本功能 | 说明 |
| --- | --- | --- |
| `SignalProgram`（指令树） | `DispatchProgram`（或复用改造后的 `SignalProgram`） | 指令树本身与位置无关，可整段复用 |
| `PSO_IF` / `PSO_IF_ELSE` / `PSO_IF_ENDIF` | 如果 / 否则 / 结束如果 | 逐条对应 |
| `PSO_SET_SIGNAL`（置信号 Green/Red） | 「允许分配调度 X」/「拒绝分配调度 X」 | 结果指令，带调度 ID |
| `SignalVM` | 同一 VM，额外携带 `R3RDispatchClaimStats` | 求值时需知道来领表的车 |
| `_signal_programs`（key=SignalReference） | 每站场一份，挂在 `Station::R3RStationYard` 上 | 不注册进信号程序表 |
| `SignalCondition` 家族 | 条件类家族，条件码扩充 | 见 6.3 |

**elif 的中文译名**：建议「**否则如果**」（与「如果 / 否则 / 结束如果」同族、两字起的固定复合词，不与既有 `STR_*` 冲突）。备选「再如果」「不然如果」。落地采用「否则如果」。

### 6.3 疑点：可编程信号机的 8 个既有条件里有一半在领表场景无信号上下文

`SignalConditionCode`（`src\programmable_signals.h:120-131`）：

| 条件码 | 中文 | 领表上下文是否可求值 | 拟定处理 |
| --- | --- | --- | --- |
| `PSC_ALWAYS` / `PSC_NEVER` | 总是 / 从不 | 可以 | 原样 |
| `PSC_NUM_GREEN` / `PSC_NUM_RED` | 前方绿灯数 / 红灯数 | **不能**（车站没有信号格） | 见 C-451-5 |
| `PSC_SIGNAL_STATE` | 另一信号的状态 | **不能**（依赖 `SignalReference`） | 见 C-451-5 |
| `PSC_SLOT_OCC` | 槽占用数 | 可映射为「本池已发坑位数」 | 复用语义 |
| `PSC_SLOT_OCC_REM` | 槽剩余 | 可映射为「本池剩余坑位」 | 复用语义 |
| `PSC_COUNTER` | 计数器值 | 可以（计数器是全局 id） | 原样 |

同时 C-8（2026-11-10 拍板）要求按「来领表这辆车自己的性能」判断，故**必须新增**性能条件：最高速度 / 功率 / 最大牵引力 / 可靠性（沿用 §五 已采集的 `R3RDispatchClaimStats`）。

- **C-451-5（待玩家确认）**：三个无上下文的条件（绿灯数 / 红灯数 / 另一信号状态）要 (A) 从下拉里删掉、(B) 保留但恒为「从不」、(C) 强行给个伪造值？建议 **(A)**。
- **C-451-6（待玩家确认）**：规则程序的作用域 = 「站场（yard）」还是「整站（含全部场）」？题述"管全站场"，暂按**站场**实现（每个 yard 一份程序，`R3R_YARD_NONE` 的整站池另有一份）。
- **C-451-7（待玩家确认）**：一份调度（order list）只能被「允许」一次还是可被多条「允许」语句提及？暂按"可多次提及、第一条命中的结果生效"。

（有疑问先按建议项实现，玩家回填后再调。）

### 6.4 取证（第 451 轮续）

见本轮对话取证结果，要点：
- 指令树 / 条件求值 / VM / GUI 列表渲染 / 存档 VLI 编码 **全部与 tile/track 无关**，只有最外层壳依赖（构造参数、map key、命令参数、窗口号编码）。
- `SignalProgramWindow` 类在 `src\programmable_signals_gui.cpp`，widgets 在 `src\widgets\programmable_signals_widget.h`。
- 直接依赖信号格、不可复用的是 `SignalStateCondition`（要 `GetSignalStateByTrackdir`）与数值 `ShowQueryString` 的 signal 选中流程（`PROGRAM_WIDGET_COND_GOTO_SIGNAL`）。

### 6.5 关键决定：**独立实现 `R3RDispatchProgram`，不复用 `SignalProgram` 对象**（2026-11-15）

复用 `SignalProgram` 需要动上游公共代码，风险过高，逐条否决：

| 复用障碍 | 说明 |
| --- | --- |
| 构造函数 | `SignalProgram(SignalReference)` 是隐式的（`SignalProgram(TileIndex, Track, bool)`），`tile/track` 是成员且被 `CloneInstructions`/条件用到 |
| `SignalSet` | 结果指令只能写 `SignalState`（Green/Red），要变成「允许/拒绝分配调度 X」必须改上游类 |
| 全局表 | `_signal_programs` 是 `btree_map<SignalReference, SignalProgram*>`，挂到 `Station` 上不能入表 ⇒ 存档/释放/依赖失效全要另写，等于再写一遍 |
| 存档 | `src\sl\signal_sl.cpp` 按 map 迭代写；站场程序必须进 `SYRD` chunk |
| GUI | `SignalProgramWindow` 的 `window_number` 打包 tile+track，每个 OnClick 都发信号命令 |

⇒ **最终形态**：新建独立数据结构 + 独立求值器 + 独立 GUI 窗口，**架构照抄**（指令流、行式列表、缩进、点行插入、条件=变量+比较符+值、多结果），但代码独立。同时把上一轮临时加进 `programmable_signals.h` 的 `PSO_DISPATCH_RESULT` / `PSC_DISPATCH_*` **撤回**（改为本功能自己的枚举，不污染信号系统的下拉遍历）。

### 6.6 落地设计（第 451 轮续）

数据（`src\station_base.h`）：

```cpp
enum R3RDispatchInstructionKind : uint8_t { RDK_IF, RDK_ELSE, RDK_OR, RDK_ENDIF, RDK_RESULT };
enum R3RDispatchRuleVariable : uint8_t {
    None, MaxSpeed, Power, MaxTractiveEffort, Reliability,   // 来车性能（C-8）
    PoolIssued, PoolRemaining,                               // = PSC_SLOT_OCC / _REM
    NumGreen, NumRed, OtherSignalState,                      // = 既有条件，无信号上下文 ⇒ 恒假
    Counter,                                                 // = PSC_COUNTER（value 存 counter id，比较 0）
};
struct R3RDispatchInstruction {
    uint8_t kind, var, op, result;      // op: 0=>= 1=<=；result: 0=允许 1=拒绝
    uint32_t value;                     // 阈值 / counter id
    uint32_t schedule;                  // RDK_RESULT：调度（OrderList 池下标）
};
std::vector<R3RDispatchInstruction> program;   // 挂在 R3RStationYard 与整站
```

- **作用域**：每个场一份（`R3RStationYard::program`）+ 整站一份（`Station::r3r_whole_program`）。
- **求值**：`R3RDispatchProgramAllows(program, schedule, stats, entry)`；空程序 ⇒ 允许（旧档行为不变）。
- **指令语义**（照抄可编程信号机）：`IF` 条件真→进入分支，假→跳到同层下一个 `OR`/`ELSE`/`ENDIF`；`OR`（「或者」= elif）同上；`ELSE` 执行到即跳到 `ENDIF` 之后；`ENDIF` 空操作；`RESULT` 立即终止并返回 允许/拒绝。
- **存档**：`SYRD` 升 **v10**（`0xFFFFFF09`）；每场一份程序块 + 整站一份，每条指令 12 字节（3 word）。
- **命令**：`Commands::SetR3RDispatchProgram`，参数 `CmdDataT<StationID, uint16_t, std::string>`（二进制 12 字节/条），命令侧校验「长度、IF/ENDIF 配对、OR/ELSE 位置、结果指令合法性、条数上限」。
- **GUI**：新窗口 `WindowClass::R3RDispatchRule`，`StationDispatchWindow` 的 `WID_SD_RULE` 从下拉改为按钮（点开新窗）；窗口内行式列表 + 每行右侧 `+` 插入菜单 + 条件三段（变量下拉/比较符下拉/数值输入）。
- **废弃**：池条目的 `rule_var/rule_op/rule_value`（第 451 轮 §五）**保留字段与存档**（避免再升一次存档版本），但 `Pick()` 不再读它们，GUI 不再编辑，注释标注「第 451 轮续起废弃」。

### 6.7 进度（随时更新，防上下文丢失）

**2026-11-15 复核（结论只认文件，不认对话记忆）**

已在盘上的实现：

1. `src\station_base.h`：`R3RDispatchInstructionKind{If, Or, Else, EndIf, Result}`、`R3RDispatchResultKind{Allow, Deny}`、`R3RDispatchRuleVariable{None, MaxSpeed, Power, MaxTractiveEffort, Reliability, PoolIssued, PoolRemaining, NumGreen, NumRed, OtherSignalState, Counter}`、`R3RDispatchRuleOp{AtLeast, AtMost}`、`R3RDispatchInstruction{kind, var, op, result, value, extra}`、`R3R_DISPATCH_PROGRAM_MAX`、`R3R_DISPATCH_NO_SCHEDULE`、`Station::R3RStationYard::program` 与整站 `r3r_whole_program`、`R3RDispatchProgramRef/Set/Allows`。
2. `command_type.h` + `station_cmd.h`：`Commands::SetR3RDispatchProgram`，参数 `CmdDataT<StationID, uint16_t yard, std::string blob>`；**程序以「每指令 24 字节」blob 传入**（3 个 word + 12 字节保留）。
3. `src\station_cmd.cpp`：`CmdR3RSetDispatchProgram`（结构校验 + 落库）、`R3RDispatchProgramAllows`（求值器）、池领取侧改走 `Allows` 过滤。
4. `src\widgets\station_widget.h`：`DispatchProgramWidgets`（`WID_DP_CAPTION` … `WID_DP_SCROLLBAR`），池窗口新增 `WID_SD_RULE` 按钮。
5. `src\r3r_dispatch_program_gui.h`：`ShowDispatchProgramWindow(StationID, uint16_t yard)` 声明。
6. `src\station_gui.cpp`：`WID_SD_RULE` 点击 → 调 `ShowDispatchProgramWindow`。

**2026-11-15 收口：`src\r3r_dispatch_program_gui.cpp` 已写完（约 530 行），`read_lints` 0 条。**

补齐项（本轮实际改的）：
- `OnQueryTextFinished(std::optional<std::string> str)` —— 原写成 `char *str` 且方法体没闭合、类也没闭合，**整份文件之前是编不过的**。
- `OnScrollbarScroll(WidgetID widget)` —— 滚动条回调叫这个名字；`OnScroll(Point delta)` 是视口滚动，别混。
- `FS_NORMAL` → `FontSize::Normal`（`GetCharacterHeight` 的新枚举）。
- 残留占位注释 `/* R3R_DP_REST2 */` 仍在第 340 行附近，无害，编译前可顺手删。

`src\CMakeLists.txt:433` 已显式列出 `r3r_dispatch_program_gui.cpp` / `.h` ⇒ **S-5 关闭**（无需手加源列表）。
S-6 核对：全 `src` 搜 `PSO_DISPATCH` / `PSC_DISPATCH` / `DISPATCH_RESULT` **零命中** ⇒ 按 §6.5「不复用 SignalProgram」的决定，信号机那边的临时枚举已撤回干净。


另：`programmable_signals.h` 曾经加过的 `PSO_DISPATCH_RESULT` / `PSC_DISPATCH_*` 按 §6.5 决定应**撤回**（不复用 SignalProgram），本轮末尾核对是否已清干净。

### 6.8 下一步（按序，防重复取证）

- **S-1** 新建 `src\r3r_dispatch_program_gui.cpp`：`DispatchProgramWindow`，照抄可编程信号机 GUI 架构（指令行列表 + 底部「变量 / 比较符 / 阈值 / 结果」控件 + 插入 / 删除 + 滚动条）。
- **S-2** 写盘方式：`write_to_file` 单次内容过长会**整体失败**（不是截断）⇒ 先写骨架（≤100 行），再用 `replace_in_file` 逐段追加（每段 ≤120 行）。
- **S-3** `Apply()` 把本地 program 拼成 24B/指令 blob 下发，**不做本地乐观更新**，以命令执行后的站内数据为准（`Reload()`）。
- **S-4** 编译自证：`R3R_fullbuild.cmd inc detach`（改头文件则自动退化全量）。
- **S-5** 待核：CMake 是否 glob `src/*.cpp`；若不是，需把新文件加进 `CMakeLists.txt`。
- **S-6** 文档回填 + `R3R_KNOWN_ISSUES.md` 的 KI-642 状态。

### 6.9 工具/环境限制（本轮实测）

- `write_to_file` 超长 ⇒ 整体失败，必须分块。
- `execute_command` 与部分读取结果会被 microcompact 清除 ⇒ 任何结论**立刻**写进本文件，不留在对话里。

### 6.10 构建坑（第 451 轮续实测，2026-11-15 23:44 EXIT_CODE=1）

首次 `R3R_fullbuild.cmd inc detach` 失败：

```
FAILED: CMakeFiles/openttd_lib.dir/src/script/api/script_event.cpp.obj
build\generated\script\api\script_window.hpp(2968): error C2143 / C2059 / C2334
```

生成文件现场（`build\generated\script\api\script_window.hpp:2967-2969`）：

```
		WID_SD_RULE = ...(::StationDispatchWidgets::WID_SD_RULE),                              ///< R3R (
 451 
): open the allocation rule program editor of this yard.
```

⇒ **根因：`src\widgets\station_widget.h` 里 `///<` 行尾注释含中文「第 451 轮续」，脚本 API 生成器把注释原样搬进 `script_window.hpp` 时按字节宽度断行，中文被吃掉、`451` 落到裸代码行上 ⇒ 语法错误。**

- 铁律补一条：**`src\widgets\*.h` 的 `///<` 行尾注释必须纯 ASCII**（中文只允许写在 `window_type.h` 的 `WindowClass` 块注释里 —— 实测该文件既有的中文注释不会进生成文件，生成文件里中文命中 0）。
- 修法：`station_widget.h:64` 改成 `///< R3R (round 451): open the allocation rule program editor of this yard.`。
- 同理，`WID_DP_*` 的全部新注释一开始就是纯 ASCII，无需再改。

### 6.11 复核结论（2026-11-15，落盘为准）

已确认在盘上且接线完整：

1. `src\station_cmd.cpp:2586` `Station::R3RDispatchProgramAllows()`（按 schedule 找池条目 → `R3RDispatchProgramRun`）。
2. `src\station_cmd.cpp:2602` `Station::R3RDispatchPoolPick()` 循环内 `:2616` **已调用** `R3RDispatchProgramRun(*prog_ptr, schedule, e, stats)` ⇒ 规则真正参与领表过滤（空程序 = 放行）。
3. `src\train_cmd.cpp:8276-8284` `claim_stats` 四项已按整链口径填好（`GetDisplayMaxSpeed` / `gcache.cached_power` / `cached_max_te/1000` / `reliability`），并打 `DISP-POOL-PICK ... spd= pow= te= rel=` 探针。
4. `src\station_gui.cpp:3716-3719` 池窗口 `WID_SD_RULE` 已是**按钮**（`STR_R3R_DISPATCH_RULE_BTN/_TOOLTIP`），`:3834` → `ShowDispatchProgramWindow(station_id, yard)`；`:3927` 禁用条件 `!can_edit`（与是否选中条目无关）；`:4019-4021` 点击分发。
5. `src\r3r_dispatch_program_gui.cpp`：`DrawWidget`(:342) 手绘指令列表、`OnDropdownSelect`(:428)、`OnQueryTextFinished`(:465)、`OnScrollbarScroll`(:473)、`WindowDesc`(:517)、`ShowDispatchProgramWindow`(:524)。
6. `src\window_type.h:875` `WindowClass::DispatchProgram` 已加。
7. `src\sl\station_yard_sl.cpp` 升 `SYRD_V10_MAGIC`（:105 0xFFFFFF09），`has_program` 能力位(:297)，每场程序(:342) + 整站程序(:350)。
8. `read_lints` 六个关键文件 **0 条**。

### 6.12 语言串核对（2026-11-16 00:05）

- `src\lang\english.txt:4065-4104` 共 **40** 条 `STR_R3R_DP_*`（含 `YARD_WHOLE` / `YARD_INDEX`），格式 `{BLACK}...`；`STR_R3R_DP_VALUE :{BLACK}{NUM}`、`STR_R3R_DP_COUNTER_SUFFIX :{BLACK}#{NUM}`、`STR_R3R_DP_CAPTION :{BLACK}Dispatch rules: {STATION} {STRING}`（GUI 里 `STR_R3R_DP_CAPTION` 由 `SetStringTip` 无参使用，实际参数在 `Reload()` 里补 → 见窗口标题）。
- `src\lang\simplified_chinese.txt` 同名单亦 **40** 条（`Measure-Object` 计数）。
- **工具坑（重要）**：`search_content` 的 **`outputMode="count"` 对 `src\lang\*.txt` 返回过假的 `Found 0 matching results`**（同一 pattern 用 `Select-String` 得 40 条、`read_file` 也能看到）。⇒ 判 lang 是否已加串**只能用 `Select-String ... | Measure-Object -ExpandProperty Count` 或 `read_file`**，不要用 `search_content` 的 count/files_with_matches 模式，否则会得出「未添加」的错误结论并重复写一遍。

### 6.13 游戏内复测判据（待玩家执行）

| # | 操作 | 期望 |
|---|---|---|
| 1 | 车站窗口 →「调度池」→ 本场点「规则…」 | 弹出**独立窗口**（不是下拉框），标题 = `Dispatch rules: <站名> <场名>`；关闭不影响池窗口 |
| 2 | 新场首次打开 | 列表区显示 `(no instructions - every train may take any schedule of this yard)`；领表行为与未加规则时完全相同 |
| 3 | 选中行 →「插入」→ 选 `if` + `max speed` + `at least` + 值调 100 → 再插入 `allow schedule` 并选一份调度 → 再插入 `end if` | 只有最高速度 ≥100 的来车能领到该调度；低速车被拒后**继续尝试下一条池条目**（日志 `DISP-POOL-PICK ... spd=` 可对照） |
| 4 | 在 `if` 与 `allow` 之间插入 `or`（中文「否则如果」）与第二个条件 | 列表缩进正确：`if`/`or`/`else`/`end if` 同层，`allow`/`deny` 缩进一层；`else` 分支在条件假时生效 |
| 5 | 保存 → 读档 → 再打开规则窗口 | 程序逐行保留（`SYRD v10` 的 `has_program`）；读档后领表结果与存档前一致 |
| 6 | 用旧档（v9 及更早）读入 | 程序为空、无异常；既有场/共享/时间表数据不变 |

负判据：把程序清空（「清空」按钮）后，领表必须回到「不看性能」的旧行为；`R3RDispatchProgramAllows` 不得因为 `stats` 为空而误拒（无性能条件时恒 true）。

### 6.14 构建自证（2026-11-16 00:41，S-4 关闭）

入口＝既有 `R3R_fullbuild.cmd inc detach`（**未新建任何 .cmd**，符合记忆 74852371）。护栏 `build\R3R_inc_guard.ps1` 判 `incremental is safe`（本轮只改 .cpp，未碰 `src\*.h` / `src\lang\*.txt`）。

三次串行修错（每一轮都是「编译 → 读日志 → 改一处 → 重编」）：

| 轮 | 报错 | 根因 | 修法 |
|---|---|---|---|
| 1 | `error C2678`（`r3r_dispatch_program_gui.cpp:325` `++it`） | `const auto it = std::find(...)` 声明成 const 迭代器，不能自增 | 改 `auto it = std::find(...)` |
| 1 | `error C2039`（`r3r_dispatch_program_gui.cpp:509` `std::strtoul` 不是 `std` 成员） | **`src\safeguards.h` 把 `<cstdlib>` 的数值解析函数全换成毒宏**（`#define strtoul SAFEGUARD_DO_NOT_USE_THIS_METHOD` 一类），项目约定禁止 strtoul | 删 `#include <cstdlib>`，改 `#include "core/string_consumer.hpp"` 并改用 `ParseInteger<uint32_t>(*str, 10, true).value_or(0)`（clamp=true 直接夹到 `uint32_t` 上限，连 `R3R_DISPATCH_VALUE_MAX` 的 `std::min` 都省了） |
| 2 | `LNK2019/LNK1120`（`station_cmd.cpp:3050` `CmdR3RSetDispatchProgram` 与 `command_table.cpp` 声明不匹配） | 生成命令表的形参是 **`const std::string &`**（照 `CmdRenameStation` 惯例），而我的定义写成了按值 `std::string blob` ⇒ 两个不同符号 | 定义改成 `const std::string &blob`（GUI 侧 `Command<...>::Post(..., blob)` 不受影响） |

第 3 次是**功能缺陷**（不是编译报错，读代码时发现）：`STR_R3R_DP_CAPTION` = `{BLACK}Dispatch rules: {STATION} {STRING}` 带两个参数，而标题控件只会走 `SetString(StringID)` ⇒ 运行期会画成 "(invalid parameter)"。修法＝重写虚函数 `Window::GetWidgetString(WidgetID, StringID)`（`widget.cpp` 的 `GetStringForWidget()` 正是从这里取文字，默认实现只是 `GetString(stringid)`），在 `WID_DP_CAPTION` 分支返回 `GetString(STR_R3R_DP_CAPTION, station_id, DispatchYardLabel(st, yard))`；新增 `DispatchYardLabel()`＝整站串 / 自定义场名 / `STR_R3R_DP_YARD_INDEX + 场号`。

最终判据（全部满足）：

- `build\R3R_incbuild.done` = `EXIT_CODE=0`；
- 日志尾 `[3/3] Linking CXX executable openttd.exe`；`findstr "error C|fatal error|FAILED:"` 计数 **0**；
- `src\r3r_dispatch_program_gui.cpp` 00:39 → `r3r_dispatch_program_gui.cpp.obj` 00:40（1 978 855 B）；
- `src\station_cmd.cpp` 00:31 → `station_cmd.cpp.obj` 00:33（9 940 931 B）；
- `build\openttd.exe` **2026-11-16 00:41（53 002 240 B）**，晚于全部改动源码；
- `read_lints` 两个文件 **0 条**；
- 新文件确已被构建系统收录（`src\CMakeLists.txt:433` 显式列出，obj 已生成）；
- 40 条 `STR_R3R_DP_*` 已进 `build\generated\table\strings.h`（`findstr "STR_R3R_DP_"` 命中）。

发布树（`build-release`）**未编**，按口径只在用户明确要求发布时编。

### 6.15 残留（本轮未做 / 只能在游戏内验证）

- §6.13 的 6 条判据 + 负判据 **全部待玩家实机复测**（GUI 交互无法静态验证：标题文案、下拉/查询框回填、缩进绘制、滚动条边界）。标题参数已按 §6.14 第 3 条改成 `GetWidgetString` 自绘，静态上不再可能出 "(invalid parameter)"。
- 底部五个控件（变量 / 比较符 / 阈值 / 结果类型 / 调度名）走 **`STR_NULL` 占位 + `DrawWidget` 自绘**。`NWidgetCore::SetString` 只吃 `StringID`，动态文本（场名、阈值数字、调度名）塞不进去；若后续要加 tooltip 之外的"按状态变色"，也在 `DrawBottomText()` 里改。
- 调度名只能用「点一次换下一个」循环切换（`NextSchedule()`），因为 `ShowDropDownMenu` 同样只吃 `StringID` 数组，动态列表要另造控件（未做）。
- `WID_DP_SCHEDULE` 在**非 result 行**上点无效（有意为之），`WID_DP_VALUE_UP/DOWN` 在非条件行上同样空转。

### 6.16 第 451 轮续二：玩家实机复测的 4 条缺陷（2026-11-16）

#### 6.16.1 玩家原话（逐字保留）

> 编译已经成功。
> 独立的规则界面已经出现，
> 但是，1，窗口标题处有（invalid parameter）
> 2,我在这个窗口内做任何操作，都会跳红框”不能这样做“，然后任何规则都无法保存
> 3，“允许分配调度”和选择调度那边完全是灰的按不下去
> 4，我希望允许或不允许分配可以多选调度

#### 6.16.2 根因定位（三条已定位到代码，第四条是设计变更）

| # | 现象 | 根因（代码事实，带行号） | 判定 |
|---|---|---|---|
| 1 | 标题处 `(invalid parameter)` | `src\lang\english.txt` 的 `STR_R3R_DP_CAPTION :{BLACK}Dispatch rules: {STATION} {STRING}` 用的是 **`{STRING}`**，而 `DispatchProgramWindow::GetWidgetString()` 传的第 2 个参数是 `std::string`（场名原文）。`src\strings.cpp:1727-1735` 的 `case SCC_STRING` 走 `args.GetNextParameter<StringID>()`；`src\strings_internal.h` 的 `GetNextParameter()` 碰到**字符串型参数会抛 `std::out_of_range`**，`FormatString` 捕获后把 `(invalid parameter)` 追加到输出并**终止本次格式化** ⇒ 最终画出 `Dispatch rules: <站名> (invalid parameter)`。**§6.14 第 3 条"重写 `GetWidgetString()`"的方向是对的，错在字符串控码选型。** | 成立 |
| 2 | 窗口内**任何操作**都弹红框「不能这样做」、规则存不下来 | `src\station_cmd.cpp` 的 `CmdR3RSetDispatchProgram` 在参数校验尾部有**括号平衡硬校验**：`if (depth != 0) return CMD_ERROR;`，且 `Or/Else/EndIf` 在 `depth == 0` 时也 `return CMD_ERROR`。GUI 是「逐条插入」的编辑器：插第一条 `if` 时 depth=1、插 `else`/`end if` **之前** depth 必然 ≠0 ⇒ **每一次下发都被服务端拒绝**，弹的正是 `STR_ERROR_CAN_T_DO_THIS`（「不能这样做」）。本地 `program` 与服务端永远对不上 ⇒ 任何规则都无法保存。 | 成立（三条现象里唯一能解释"任何操作"的全局原因） |
| 3 | 「允许分配调度」「选择调度」纯灰 | `UpdateBottomControls()` 把 `WID_DP_RESULT_KIND` / `WID_DP_SCHEDULE` 的可用性绑在「当前选中行是 `Result`」上；程序为空 ⇒ 无选中行 ⇒ 恒 disabled。而因为第 2 条连一条 `Result` 都插不进去 ⇒ 玩家看到的就是"永远灰"。 | 成立（第 2 条的次生现象，但仍需单独改 UX） |
| 4 | 要求「允许/拒绝分配」可**多选**调度 | 现状 `R3RDispatchInstruction.extra` 是**单个** schedule id，`NextSchedule()` 只能"点一次换下一个"。 | 设计变更（原 §6.15 已列为"未做"） |

#### 6.16.3 修法清单

- **修 1（标题）**：两份 lang 把 `STR_R3R_DP_CAPTION` 的 `{STRING}` 改成 **`{RAW_STRING}`**（`src\strings.cpp:1723-1725` 的 `case SCC_RAW_STRING_POINTER` 正是 `args.GetNextParameterString()`，专收原始文本参数）。C++ 侧不动。代价：改 `src\lang\*.txt` ⇒ 护栏判全量重编（约 16~20 分钟）。
- **修 2（红框）**：删掉 `CmdR3RSetDispatchProgram` 的 `depth != 0` / `depth == 0` 结构平衡校验，只保留「每条指令字段范围 + 条数上限」校验。理由：GUI 编辑器必须允许中间态（半截 `if`），否则永远无法编辑；求值器本身已对不平衡程序做了防御（`R3RDispatchMatchingEndIf` 越界回退、`Else` 在 depth 0 时跳过）。
- **修 3（灰控件）**：`UpdateBottomControls()` 放宽——无选中行时也允许点「结果类型 / 选择调度」，点击行为＝先追加一条 `Result` 行再编辑（不再要求玩家先会用「插入」下拉）。有选中但选的是条件行时仍 disabled（避免误改）。
- **修 4（多选调度）**：`extra` 由「单个 schedule id」改为**位掩码**（bit i = 本场调度池**去重后**第 i 份调度，上限 32）；求值器 `Result` 判据改 `HasBit(ins.extra, slot)`；GUI「选择调度」改为弹出小型多选窗口（自绘勾选列表，勾选即下发命令）。

#### 6.16.4 待定（U-451-x）

- U-451-1：位掩码 32 位上限 vs 场池容量上限（默认 256）。取「去重后调度数」，现实场景远小于 32；超限只在 U 里登记，不做分页。
- U-451-2：掩码下标按「池条目下标」还是「去重后调度序号」⇒ 取**去重后调度序号**（同一份调度可能出现在多条池条目上，按条目下标会重复）。
- U-451-3：旧程序兼容性 ⇒ 本轮之前没有任何程序成功存过（第 2 条），无风险；仍保留「掩码为 0 = 谁都不匹配」的语义。

#### 6.16.5 复测判据（替换 §6.13 的第 1/3 条）

1. 打开规则窗口：标题 = `分配规则: <站名> <场名>`，**无 `(invalid parameter)`**。
2. 「插入 → 如果」立刻成功（无红框），列表出现 `如果 …`；再插「或者 / 否则 / 结束如果」均成功；关窗重开、存档读档后逐行保留。
3. 程序为空时点「允许/拒绝分配」按钮或「选择调度」：自动出现一条 Result 行并可编辑（不再纯灰）。
4. 点「选择调度」：弹出多选列表，勾选 2~3 份调度后行文本显示多份名字，领表时这几份调度都能被领到，未勾选的领不到。
5. 负判据：不勾任何调度 ⇒ 该 Result 行不匹配任何调度（不写成"全部允许"）。



