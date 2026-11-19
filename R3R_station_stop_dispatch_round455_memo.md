# 第 455 轮：玩家五条新需求（车站停止 / 临时分组后缀 / 无主预览 / 调度池空报错 / 分配规则照抄信号灯）

- 报告时间：2026-11-17
- 前置好消息：**KI-647「读档 Referencing invalid OrderList (R3SG)」已由玩家确认消失**（A 读档自愈 + B 写档过滤生效）。
- 本文档 = 需求登记（第一落盘），随后才做代码取证。代码锚点随取证逐节回填，不覆盖初判。

## 一、玩家原话（逐条，不改写）

1. 「我需要一个在车站停止的命令，也就是到达车站并停稳之后不再推进排程，而是把列车置为停止状态，需要玩家手动启动」
2. 「在命令列表，当一个前往挂接命令或等待挂接命令有临时挂接分组时，在该命令后面加上该命令的临时挂接分组名，例如这样：等待挂接（挂接分组：滚木）」
3. 「无主排程的R3R命令的临时挂接分组预览有问题，没有向有主的对齐」
4. 「假若列车在某个站场等待分配命令，但是该站场调度池内没有命令，则冒一个类似于列车迷路的报错框“列车XXX所在的站场没有调度命令可分配”，同时该列车不推进排程，一直执行那个尝试领取调度的命令」
5. 「调度分配规则我也不知道为什么，反正我玩的时候条件缩进显示有问题，而且列车也没有正确的按规则分配调度。还有就是，调度分配规则的判断条件（如列车功率那些的），我现在希望照抄可编程信号灯的条件（尤其是少了一条“机车类型”【蒸汽，内燃，电力，磁浮，……】，当然，如果其它不能照抄，那也别抄了）」

## 二、需求拆解

### D-455-1 车站停止命令（新订单语义）

- 期望：到站 + **停稳**后不推进排程，把列车置为停止（stopped）状态，玩家手动启动才继续。
- 与现有命令的区别：现有 `GOTO_STATION` 到站后按 `ProcessOrders` 正常推进到下一条；本命令要在"停稳"这个点截住。
- 待定项：B-455-1（形态）见 §四。
- 预估影响面：`order_type.h`/`order_base.h`（新类型或 GOTO_STATION 上的新 flag）、`order_cmd.cpp`（命令校验/白名单/序列化）、`order_gui.cpp`（下拉/回显）、`train_cmd.cpp`（到站处理与 `ProcessOrders` 的推进闸门）、`lang/*.txt`（双语）。
- 风险：若是新 `OrderType` 枚举值 ⇒ 触碰订单类型字段（存档兼容方向：新档→旧档不可读，反向可读；R3R 既有先例允许）；若复用 GOTO_STATION + flag，则零格式变更，但需确认订单 flag 位还有空位。
- 待验证锚点：`ProcessOrders` 里 GOTO_STATION 到站后的推进点、`TrainEnterStation`/`CheckIfTrainNeedsService` 附近、`VS_STOPPED` 的置位/清除入口（`CmdStartStopVehicle`）、`SetWindowDirty`/`InvalidateVehicleOrder` 的既有节流。

### D-455-2 命令列表里追加临时挂接分组名

- 期望：`前往挂接` / `等待挂接` 命令后追加 `（挂接分组：<名>）`，例「等待挂接（挂接分组：滚木）」。
- 事实：`OT_GOTO_COUPLE`=16、`OT_WAIT_COUPLE`=17（`order_type.h`）；临时分组字段既有（第 143 轮 KI-225 已让 `MOF_COUPLE_TEMP_GROUP` 支持 `OT_WAIT_COUPLE`）。
- 预估影响面：`order_gui.cpp` 的订单标签组装点 + 新 lang 串（中英双语）+ 可能的 `strings.cpp` 取参。
- 待验证锚点：订单行渲染函数（`GetOrderString`/`DrawOrderLabel`/`OrderStringArgs`）、现有 `R3RYardLabel`/`CoupleTempGroupLabel` 的回显写法（照抄其取会话/取组名的姿势）。

### D-455-3 无主排程的临时挂接分组预览未对齐

- 期望：无主排程（借用/跟随态，`GetEffectiveOrders() != orders`）时，R3R 命令的临时挂接分组**预览**应与有主排程一致。
- 预估影响面：`order_gui.cpp` 里"预览"分支（下拉预填、点击循环、`GetEffectiveOrders()` 取表路径），可能还存在"仅 `v->orders` 判断"的旧守卫。
- 待验证锚点：D-455-2 同一渲染函数附近的 `GetEffectiveOrders()` 使用点、临时分组字段的读取点、`MOF_COUPLE_TEMP_GROUP` 的 UI 读取处。

### D-455-4 站场调度池为空时报错框

- 期望：列车在某站场等待"分配命令"、而该站场调度池内无命令 ⇒ 弹框「列车 XXX 所在的站场没有调度命令可分配」，且**不推进排程**，一直重复执行那条"尝试领取调度"的命令。
- 预估影响面：站场调度池读取点（`station_base.h` 的 `R3RStationYard` 调度字段 / `r3r_dispatch_program_gui.*` 同族数据）、订单执行点（"领取调度"命令的分支）、错误提示（`ErrorMessageData`/`ShowErrorMessage`，参照"列车迷路"文案）。
- 风险：**弹框必须节流/去重**，否则每 tick 弹一次会淹没界面（参照既有 `R3RDbgEdge`/一次性集合的写法）。
- 待验证锚点：站场调度池的容器与"取一条"接口、"领取调度"订单类型的执行点、现有"列车迷路"报错的产生方式。

### D-455-5 调度分配规则：显示缩进 + 实际分配 + 条件照抄信号灯

- (a) **缩进显示错误**：规则条件树的 GUI 布局（长条件/嵌套条件下缩进错位）——GUI 层 bug。
- (b) **分配不正确**：列车没有按规则被分配 —— 逻辑层 bug，需现场日志（哪列车、哪条规则、期望分到哪个池，实际分到哪个）。
- (c) **条件集合照抄可编程信号灯**，尤其缺「机车类型」（蒸汽/内燃/电力/磁浮/…）；玩家口径：能照抄的照抄，不能照抄的**不要硬抄**。
- 事实：`src/r3r_dispatch_program_gui.h` = 第 451 轮续「调度池分配规则程序编辑窗口」入口。
- 预估影响面：`r3r_dispatch_program_gui.cpp`（条件列表/布局）、条件枚举与求值（新条件"机车类型"需要读 `RailVehInfo(...)->engclass`/`RailType` 之类）、`lang/*.txt`。
- 待验证锚点：调度规则条件枚举与求值函数、可编程信号灯条件枚举（供对比清单）、GUI 缩进计算处（`NWidget`/`SetPadding`/`SetPIP` 一类）。

## 三、待验证锚点（取证后回填）

| 编号 | 要查的东西 | 结论 |
| --- | --- | --- |
| A-455-1 | 订单类型枚举与是否还有空闲 flag 位 | 待填 |
| A-455-2 | 到站停稳点 + 排程推进点 | 待填 |
| A-455-3 | 订单标签渲染函数 | 待填 |
| A-455-4 | 站场调度池容器与"领取调度"订单类型 | 待填 |
| A-455-5 | 调度规则条件枚举/求值/GUI 缩进 | 待填 |
| A-455-6 | 可编程信号灯条件全集（对比基准） | 待填 |

## 四、待玩家确认（按编号回）

- **B-455-1（车站停止命令的形态）**：①新订单类型（命令列表里独立一条"在车站停止"）②`GOTO_STATION` 上挂一个开关（例如"到达后停止"勾选框）。我倾向 ①（语义清晰、不影响既有 GOTO_STATION 行为），但 ② 存档零变更。
- **B-455-2（停止后玩家怎么启动）**：点车辆窗口"启动/停止"按钮即可继续下一条命令？还是需要一个更明确的"继续执行排程"动作（避免误触就跑了）？
- **B-455-3（停止时机）**：装载/卸载完成后停，还是"进站停稳即停"（不等装卸）？若站台有装卸，停止是否应先完成装卸？
- **B-455-4（D-455-4 弹框频率）**：每列车只弹一次（首次发现池空），还是每次尝试领取都弹（会很吵）？建议每题一次 + 日志记录。
- **B-455-5（D-455-5(b) 现场）**：请给一份"分配不正确"的现场（哪列车、哪条规则、条件读数、期望分配到哪个池/实际分到哪个），否则我只能按代码推断，无法定论。
- **B-455-6（D-455-5(c) 照抄范围）**：可编程信号灯哪些条件你认为应该照抄、哪些明确不需要（例如"其他公司拥有"或"信号灯状态"类可能与本场景无关）。

## 四bis、猜测根因（待验证，按"有疑点/猜测根因立即落盘"规则登记）

- **G-455-1（强嫌疑，D-455-5(b) 分配不正确）**：`R3RDispatchRuleVariable`（`src\station_base.h:898-914`）里 `NumGreen`/`NumRed`/`OtherSignalState` 三项是从可编程信号灯照搬来的概念，注释自陈「恒 0」（`:908-912`），求值函数 `R3RDispatchRuleVariableValue()`（`src\station_cmd.cpp:2438-2458`）对它们没有真实取值来源 ⇒ 规则里一旦用这三项作条件，判定必为假（`>=` 恒不成立）⇒ 该规则永不匹配 ⇒ 列车被分到别的池或干脆不分配。这同时解释玩家"不知道为什么分配不对"。
  - 待验证判据：请玩家贴出出问题的那几条规则内容（是否用了绿灯/红灯/其他信号灯状态）；若确实用了 ⇒ G-455-1 成立，修法 = 删掉这三项（与 D-455-5(c)「不能照抄就别抄」一致）。
- **G-455-2（中嫌疑，D-455-3 无主预览不对齐）**：临时挂接分组的**下拉列表**在两处用的 owner 不同——订单窗口用车辆所有者 `this->vehicle->owner`（`src\order_gui.cpp:3987`），无主排程编辑器用当前公司 `this->GetCompany()`（`:6123`）。`CoupleTempGroupDropDownList()` 按 `R3RCoupleGroupIsVisibleTo(owner)` 过滤组（`:874` 起），owner 不同 ⇒ 列表内容与预选值不同 ⇒ 玩家看到的"预览"不一致。
  - 另有一处缺口：`WID_O_COUPLE_TEMP_GROUP` 在 `GetWidgetString()` 里只有 `order_gui.cpp:3610` 一个回显分支（走 `CoupleTempGroupLabel(order)`），排程编辑器没有独立回显分支 ⇒ 若两窗口不共用该函数，编辑器里的按钮文本就恒空。
  - 待验证判据：同一条命令在订单窗口与排程编辑器里，临时分组按钮文本/下拉预选是否不同；多公司存档（排程所有者 ≠ 当前公司）下差异是否放大。

## 五、落地顺序建议（技术依赖）

1. **D-455-2 / D-455-3**（同一渲染函数，一起做，零存档影响）——最快见效。
2. **D-455-5(a) 缩进**（纯 GUI 布局，独立）。
3. **D-455-4**（需要看清"领取调度"执行点，弹框节流）。
4. **D-455-5(c) 条件照抄 + 机车类型**（需先出条件对比清单给玩家确认）。
5. **D-455-5(b) 分配逻辑**（需现场数据，最后做）。
6. **D-455-1 车站停止命令**（可能要动订单枚举/存档，放最后，且先等 B-455-1/2/3 拍板）。

## 六、状态

- 本文件最初为需求登记（2026-11-17），随后开始落地；进度按条更新如下。
- KI 条目：`R3R_KNOWN_ISSUES.md`「第 455 轮」小节 KI-648~KI-653（逐条对应 D-455-1~5 + 需求 6）。
- **D-455-2（KI-649）已落盘**：`lang` 中英各加 `STR_ORDER_COUPLE_TEMP_GROUP_SUFFIX`；`order_gui.cpp` 加 `R3RHasTemporaryCoupleGroup()` 并在 `OT_GOTO_COUPLE`/`OT_WAIT_COUPLE` 两分支尾各追加一次（`:1534-1537` / `:1543-1546`）。`read_lints` 0 条，**尚未编译**。
- 因本轮需新增多条 lang 串（649/648/651/652），统一策略：全部 lang 改齐后只做**一次全量重编**（改 `src/lang/*.txt` 会触发 strgen 重建 `strings.h` ⇒ 全量 16~20 分钟）。
- 落地顺序（按 §五）：2（已完成）→ 3 → 5a → 4 → 5c → 5b → 1。

## 七、拍板回填（玩家 2026-11-17 第二次答复，按编号）

- **B-455-1 = ①'（第三种答案，不是我列的两个）**：不做新命令类型，而是给「前往车站」加一个**命令属性**「到达后停止」，并且**与「等待分配命令」「自动调节班次」共用一个下拉框**（即现有 `WID_O_R3R_SEPARATION` 那个两选一互斥下拉扩成三选一）。
  ⇒ 落地口径：`MOF_R3R_*` 家族新增一个「到达后停止」标志位，三者在 UI 上互斥/或可并存按现有 `WID_O_R3R_SEPARATION` 的选中值机制扩展；**不碰 `OrderType` 枚举、不改订单类型存档字段**。
- **B-455-2 = 点车辆窗口「启动/停止」按钮即继续下一条**：停止后走 `CmdStartStopVehicle` 既有路径恢复，恢复时推进到下一条命令（不需要额外"继续排程"动作）。
- **B-455-3 = 进站停稳即停**（不等装卸完成）；并且**这类命令不参与客货流计算**（不进 link graph 的货运流统计）。
- **B-455-4 = 每列车只弹一次**（首次发现池空弹框，之后静默重复尝试）。
- **B-455-5 = 「傻瓜的部分」我自己改，设计口径的部分先报给玩家**：
  - 傻瓜可改：①删掉 `NumGreen`/`NumRed`/`OtherSignalState` 三个恒 0 条件（与"不能照抄就别抄"一致，也消除 G-455-1）；②新增「机车类型」条件（蒸汽/内燃/电力/磁浮，取 `RailVehInfo(...)->engclass`）；③修条件缩进。
  - 需要玩家定口径：可编程信号灯条件全集里哪些要、哪些不要（如"其他公司拥有""信号灯状态""计数器"类），以及"机车类型"是否要区分单/双头、是否要按**段内引擎**还是**整链任一引擎**判定。

## 八、需求 6（玩家 2026-11-17 追加）：购买列车时弹原生 NewGRF 报错

- 玩家原话：「R3R版本内普通的购买列车行为会跳一个原生OTTD不会跳的报错：NewGRF XXX提供了错误的信息 购买列表中XXX（列车名）的货物/改装信息与购买后的信息不同。这可能会导致自动更新或自动替换时无法正确改装此载具，这是我不期望看到的」。
- 定性：这是**引擎自带的购买一致性校验**（购买列表里某引擎的 cargo/refit 信息，与真正造出来的车辆的 cargo/refit 信息不一致时报），原生 JGRPP 不报、R3R 报 ⇒ 说明 R3R 改了「造出来的车辆」的容量/refit 相关字段而没同步改「购买列表」那一侧（或反之）。
- 强嫌疑方向（待验证）：
  - `R3RDetailsWholeConsistCapacity` / `R3RConsistRepresentativeEngine` 一族（第 445 轮 C-2）把**代表引擎**从链头改成了"原型有名字的那节"，若购买列表用 `Engine::Get(engine_type)` 的 refit 掩码、而造车后用 R3R 的代表引擎算容量，两侧就不一致。
  - 假引擎/段身份（`SegmentFront`/`FakeDualHead`）让 `IsEngine()` 与 `railveh_type` 在造车后被改写 ⇒ `Vehicle::GetEngineInfo()` 与购买列表的 `Engine` 不同源。
  - 需先找到报错的确切产生点（`src\lang\english.txt` 里搜 "purchase list"/"refit information"，再回溯到 `engine_gui.cpp`/`build_vehicle_gui.cpp`/`newgrf.cpp`）。

### 8.1 取证（2026-11-17，报错产生点已定位）

- 语言串：`src\lang\english.txt` 的 `STR_NEWGRF_BUGGY_ARTICULATED_CARGO`（"…the cargo/refit information of XXX in the purchase list is different from the information after purchase…"），标题串 `STR_NEWGRF_BUGGY`。
- 唯一产生点：**`src\articulated_vehicles.cpp:338-374` 的 `CheckConsistencyOfArticulatedVehicle(const Vehicle *v)`**，第 371-372 行
  `if (real_refit_union != purchase_refit_union || real_refit_intersection != purchase_refit_intersection || carries_more) ShowNewGrfVehicleError(engine->index, STR_NEWGRF_BUGGY, STR_NEWGRF_BUGGY_ARTICULATED_CARGO, GRFBug::VehRefit, false);`
  （`newgrf_commons.cpp` 的 `ShowNewGrfVehicleError` 用 `if (!grfconfig->grf_bugs.Test(bug_type))` ⇒ **每 GRF 每局只弹一次**，与玩家"买某型车就弹"吻合。）
- 三条判据：
  1. `real_refit_union != purchase_refit_union`（实际各节 refit 掩码并集 vs 购买列表）；
  2. `real_refit_intersection != purchase_refit_intersection`（交集）；
  3. `carries_more`：`real_default_cargoes`（**实际车节 `cargo_cap > 0` 的 `cargo_type` 集合**）里有 `purchase_default_capacity[cargo] == 0` 的货种。
- "购买列表"侧是静态量：`GetArticulatedRefitMasks(v->engine_type, true, …)` + `GetCapacityOfArticulatedParts(v->engine_type)`，只按**头车 `engine_type`** 遍历原型回调算出来，与实例无关。
- "实际"侧是实例量：循环 `do { GetAvailableVehicleCargoTypes(v->engine_type, true); if (v->cargo_cap > 0) real_default_cargoes.Set(v->cargo_type); } while (v->HasArticulatedPart() ? (v = v->GetNextArticulatedPart()) : (v = nullptr))`。
- 调用点（`src\train_cmd.cpp`）：`:2327`（`CmdBuildRailWagon`，造车厢）、`:3641`（`CmdBuildRailVehicle`，造机车），两处都在 `AddArticulatedParts(v)` **与** `v->First()->ConsistChanged(CCF_ARRANGE)` **之后**。

### 8.2 猜测根因（待验证）

- **G6-1（强，指向一条 R3R 自己的补丁）**：`src\train_cmd.cpp:614-626` 的 KI-641 保护
  `if (u->IsArticGroupMember() && !u->IsArticGroupHead() && new_cap == 0 && u->cargo_cap > 0) new_cap = u->cargo_cap;`
  对**刚造出来的真 artic 部件**同样成立（`IsArticGroupMember()` = `IsArticulatedPart()` = true、`IsArticGroupHead()` = false）。若 GRF 的 capacity 回调对新造部件返回 0（本仓 R3R 又把 NewGRF 的 position-in-consist 改成了**段内计数**，见记忆 96042581，回调读数与购买列表算时不同），上游会把该节 `cargo_cap` 清 0 ⇒ 该 `cargo_type` 不进 `real_default_cargoes`；R3R 保留了 `rvi->capacity` ⇒ 该货种进 `real_default_cargoes`，而 `purchase_default_capacity[cargo] == 0` ⇒ `carries_more = true` ⇒ 弹窗。
  支撑：报错在第 371 行，且 Check 在 ConsistChanged **之后**；KI-641 是本轮前才加的"阻止破坏性清零"补丁，正好只影响"新造部件的 0 容量"这一个量。
  待验证：在 `CheckConsistencyOfArticulatedVehicle` 前插只读探针，dump 每节 `engine_type/cargo_type/cargo_cap/IsArticGroupMember/IsArticGroupHead/new_cap`，看是否出现"某节 cap>0 而 `GetCapacityOfArticulatedParts` 该货种为 0"。
- **G6-2（次）**：`src\vehicle_base.h:1300-1303` 把 `HasArticulatedPart()` 从上游的 `Next()->IsArticulatedPart()` 改成 `Next()->IsArticGroupMember()`（R3R 组角色视图，`train.h:212-215` 为 `IsArticulatedPart() || ArticGroupMember`）。购买路径上若新造车后面已经挂着同 depot 的另一条链（`Next()` 非空且对方带 `ArticGroupMember` 角色位），do-while 会多遍历 ⇒ union/intersection 变化。购入单节车时 `Next()==nullptr`，故此支只在"车库内已有打散组且被并入"时可能成立，与"普通购买"吻合度低。
- **G6-3（弱）**：`R3RConsistReferenceEngine` 只改 `rvi_v`（`pow_wag_power`）与 `first_engine`，不碰 `cargo_cap`/`cargo_type`，**已排除**（`rvi_v` 在 `train_cmd.cpp` 只有 468/587/588 三处，全在 wagon power 分支）。
- 修法候选（未落地）：把 KI-641 的保留条件收窄到"非新建车"（例如仅在 `u->cargo_cap` 来自既有车辆的 settle 路径生效，或按 `ConsistChangeFlag::Capacity` 的调用来源区分），使新造车仍走上游的清零语义；或在该保护前判 `u->IsArticulatedPart()`（真部件不清零保护、只保护打散组成员）。
- 状态：**已取证报告错产生点 + 三条判据 + 猜测根因，待验证（需玩家用带探针的 exe 复跑一次"买那型车"）。** 见 KI-653。

## 九、落地进展（2026-11-17，逐条回填，只记事实）

### 9.1 D-455-2（KI-649）已改代码

- `src/lang/english.txt` / `simplified_chinese.txt`：新增
  `STR_ORDER_COUPLE_TEMP_GROUP_SUFFIX : (group: {RAW_STRING})` / `（挂接分组: {RAW_STRING})`（实际中文为 `（挂接分组: {RAW_STRING}）`，以文件为准）。
- `src/order_gui.cpp`：新增文件内 `R3RHasTemporaryCoupleGroup(const Order*)`（判 `OT_GOTO_COUPLE`/`OT_WAIT_COUPLE` + `HasCoupleTempGroup()` + 组名存在）；
  `DrawOrderString()` 的 `case OT_GOTO_COUPLE`（`:1534-1537`）与 `case OT_WAIT_COUPLE`（`:1543-1546`）在 `break;` 前各追加
  `AppendStringInPlace(line, STR_ORDER_COUPLE_TEMP_GROUP_SUFFIX, CoupleTempGroupLabel(order));`。
- 姿势与既有 `STR_ORDER_GO_TO_COUPLE_CARGO`（`:1516`）、`STR_ORDER_R3R_WAIT_SEPARATION_SUFFIX`（`:1145-1149`）一致：StringID + 参数，不手工拼 std::string。

### 9.6 逐条落地状态核实（2026-11-17，工具故障恢复后重新实测）

- **KI-649 = 已落盘**：`order_gui.cpp:1538-1549`（`OT_GOTO_COUPLE` / `OT_WAIT_COUPLE` 两处 `AppendStringInPlace`）；`R3RHasTemporaryCoupleGroup` 定义 `:868`。
- **KI-650 = 已落盘**：`order_gui.cpp:6002-6011` 的 `GetWidgetString` 里给 `WID_O_COUPLE_TEMP_GROUP` 补了无主排程分支（从选中命令取组名，不再吃复用控件树里写死的 `STR_ORDER_COUPLE_TEMP_GROUP_NONE`）。
- **KI-652(a)(c) = 已落盘**：
  - (a) 缩进 `r3r_dispatch_program_gui.cpp:571-586`：`EndIf`/`Else`/`Or` 三者统一先退层，再由 `If || Or || Else` 进层。
  - (c) 可选变量表 `_dispatch_var_selectable[]`（去 `NumGreen`/`NumRed`/`OtherSignalState`）、新增 `EngineClass`（枚举末尾追加，编号安全）、求值 `station_cmd.cpp:2466-2474`（三个恒 0 项直接 `return true`；`EngineClass` 用相等比较）、填充 `train_cmd.cpp:8283`、GUI 值编辑走 5 项下拉。
  - 结论句：KI-652 的「傻瓜部分」三条全部完成，剩 (b) 等玩家口径（备忘 §9.5）。
- **KI-648 = UI 已落盘，行为侧未落盘**：`order_base.h:732-746`（`HasR3RStopAtStation`/`SetR3RStopAtStation`，xflags bit 7）、`order_type.h:387`（`MOF_R3R_STOP_AT_STATION`）、`order_cmd.cpp:3303`、`order_gui.cpp:1164`（后缀）、`:4029`/`:6199`（下拉第三项）、`:4616-4633`（三选一互斥切换）、`:6510-6538`（相邻同类命令批量复制）。**但 `src\train_cmd.cpp` 里搜不到任何 `HasR3RStopAtStation`** ⇒ 到站停稳闸门、排程不推进、启动恢复、货流排除四项行为逻辑全部未写。
- **KI-651 = 未落盘**：`src` 内无「没有调度命令可分配」类新串，也无池空弹框代码。
- **需求 6 = 未取证**（仅备忘 §八 记录强嫌疑方向）。

### 9.7 本轮补写的代码（2026-11-17）

**KI-648 行为侧（`src\train_cmd.cpp`，`TrainEnterStation()` 内、`R3RSettleStaleCargoPayment()` 之后 / `BeginLoading()` 之前）**
- 判据：`current_order.IsType(OT_GOTO_STATION) && current_order.HasR3RStopAtStation()`。
- 动作：`cur_speed=0` + `UpdateTrainSpeedAdaptationLimit(0)` + `force_proceed=TFP_NONE`；**不调 `BeginLoading()`**（因此不 PrepareUnload、不产生 CargoPayment、站台库存不动 ⇒ 不参与客货流）；`DeleteUnreachedImplicitOrders()` → `UpdateVehicleTimetable(v,true)` → `current_order.Free()` → `SetDestTile(INVALID_TILE)` → `IncrementRealOrderIndex()` → `vehstatus.Set(VehState::Stopped)`；仍触发 `TriggerStationRandomisation/Animation(VehicleArrives)`；探针 `STOP-AT-STATION veh= st= real= tile= stopped=1`。
- **口径差异（U-455-1，已改文案使其自洽）**：玩家 B-455-3 原话是「不推进排程」，但**索引必须推进一格**，否则玩家点「启动」的瞬间 `ProcessOrders` 会重新取到同一条 GOTO_STATION、车已在该站 ⇒ 立刻再次触发 `TrainEnterStation` ⇒ 「点启动即又停」死结。因此实现=「索引前移一格 + 车被置为已停止」，玩家在「启动/停止」按钮上看到的效果正是 B-455-2 的「即继续下一条」。两条 TOOLTIP（中/英）已从「不推进排程」改写为「不会自动继续执行下一条命令」。若玩家复测后坚持索引必须原地不动，则须改走另一条路（在 `CmdStartStopVehicle` 里按「该命令带停站标记且车已在该站」补一次跳过），成本约 +40 行。

**KI-651（`src\train_cmd.cpp`，`TrainDispatchHandler()` 的 `R3RDispatchPoolPick() < 0` 分支）**
- 新增 `static std::set<uint32_t> _r3r_dispatch_empty_notified`（紧邻 `_r3r_dispatch_no_slot`）。
- 判据：`st->R3RDispatchPoolCount(yard) == 0`（只有「池里真的一条都没有」才弹；坑位满/规则不满足属正常等待，不打扰）且本车未弹过。
- 动作：`ShowErrorMessage(GetEncodedString(STR_ERROR_R3R_DISPATCH_NO_SCHEDULE, v->index), {}, WarningLevel::Warning)` + 探针 `DISP-POOL-EMPTY-NOTIFY veh= st= yard=`；照旧 `return` ⇒ 排程零推进、列车原地等。
- 节流含 B-455-4「每列车只弹一次」：集合只插不删（补日程后能领到就自然不再走这条分支）。

**新增语言串**（`src\lang\english.txt` / `simplified_chinese.txt`，各 1 条，紧跟 `STR_ERROR_R3R_DISPATCH_POOL_NO_SUCH_ENTRY`）
- `STR_ERROR_R3R_DISPATCH_NO_SCHEDULE` = `{WHITE}{VEHICLE} is waiting at a yard whose dispatch pool has no schedule at all` / `{WHITE}{VEHICLE} 所在的站场没有调度命令可分配`。

**lint**：`train_cmd.cpp` / `order_gui.cpp` / `r3r_dispatch_program_gui.cpp` 三文件 `read_lints` = 0 条。
**尚未做的**：需求 6 取证；全量重编（本轮改了 lang 与头文件级串 ⇒ 必须全量）。

### 9.2 D-455-3（KI-650）已改代码

- `OrderHost::GetOrders()` 本来就是 `veh->GetEffectiveOrders()` ⇒ `GetOrder()` 取到的已经是有效表，无主/借用态不需要另找表。
- `OrderWindow::GetOrderListCompany()`（`:2126` 起）已按 `host.company` 兜底（`host.veh == nullptr` 时用 host 自带公司号），无主态取公司号不会错。
- 无主排程编辑器 `R3RScheduleEditorWindow::GetWidgetString` 新增 `WID_O_COUPLE_TEMP_GROUP` 分支（`:5989-5997`，注释标 KI-650），显示与有主窗口同款 `CoupleTempGroupLabel()`；
  其下拉预填在 `:6154` 走 `this->GetCompany()`，有主窗口在 `:4009` 走 `this->vehicle->owner` —— 两者最终都落到 `R3RCoupleGroupIsVisibleTo(owner)`，对齐成立。

### 9.3 D-455-5(a) 缩进已修（KI-652 的 (a)）

- 根因（`src\r3r_dispatch_program_gui.cpp` 列表绘制）：缩进深度只在 `EndIf`/`Else` 时退回，但 `Or` 既**不退回**又在画完后 `depth++`
  ⇒ `IF / OR / 体 / ENDIF` 一层层多缩进，玩家看到的"缩进错位"就是这个。
- 修法：把 `Or` 并入"先退一层"的集合（`EndIf`/`Else`/`Or`），画完后仍由既有的 `If || Or || Else` 分支进它自己的体。

### 9.4 D-455-5(c) 条件集已按"能抄的抄、不能抄的别抄"处理（KI-652 的 (c)）

- **关键存档约束**：`R3RDispatchInstruction::var` 以 uint8 逐条入档，删枚举会让其后所有编号前移、旧档条件静默变成另一个变量
  ⇒ 三个恒 0 项（`NumGreen`/`NumRed`/`OtherSignalState`）**保留枚举编号不动**，改为：
  1) 编辑器下拉不再出现（新增 `_dispatch_var_selectable[]` + `_dispatch_var_selectable_strings[]`，`WID_DP_VAR` 弹这张表，选中回填用 `DispatchVarFromSelectIndex()`）；
  2) 求值时三者直接 `return true`（「不设限」），不再按 0 比较 —— 旧档里留着的这类条件既不会让规则永远假，也不会让"至多 0"永远真。
- **新增「机车类型」**：`R3RDispatchRuleVariable::EngineClass` 追加在枚举**末尾**（值 10，旧档无此值 ⇒ 编号安全）；
  `R3RDispatchClaimStats` 加 `uint8_t engine_class`；`train_cmd.cpp` 领取调度处用
  `RailVehInfo(R3RConsistReferenceEngine(v)->engine_type)->engclass` 填充（与 ConsistChanged 取"代表引擎"同口径）。
- 求值：`R3RDispatchConditionHolds` 对 `EngineClass` 用**相等**比较（枚举不可比大小），编辑器里比较符固定显示「是」（新增 `STR_R3R_DP_OP_IS`）且 `ToggleOp()` 对它直接 return。
- 值编辑：`WID_DP_VALUE` 在 `EngineClass` 时弹 5 项下拉（蒸汽/内燃/电力/单轨/磁浮），选中直接 `SetValue(index)`；`SetValue`/`StepValue` 都对上界 `lengthof(_dispatch_engine_class_strings)-1` 做钳制。
- 新增 lang 串（中英各 7 条）：`STR_R3R_DP_VAR_ENGINE_CLASS`、`STR_R3R_DP_CLASS_{STEAM,DIESEL,ELECTRIC,MONORAIL,MAGLEV}`、`STR_R3R_DP_OP_IS`。

### 9.5 D-455-5(b)「没按规则分配」的新强嫌疑（**待验证，尚未改码**）

- 事实：`R3RStationYard::R3RDispatchPoolEntry` 有一套**旧式单条件** `rule_var/rule_op/rule_value`（第 451 轮第一版），
  第 451 轮续又加了 `prog`（程序）。而 `R3RDispatchPoolPick()`（`station_cmd.cpp:2631` 起）**只跑 `prog`**，
  不再看 `rule_var/rule_op/rule_value`。
- 因此：**玩家在池条目上看到的"规则"（`R3RDispatchRuleText`，`station_gui.cpp:3680-3699`）与实际发牌逻辑是两套**。
  老档（只有单条件）在新程序为空时，单条件被完全忽略 ⇒ 表现为"我设了规则，列车却没按规则分配"。
- 另外 `R3RDispatchRuleText` 只处理 `MaxSpeed/Power/MaxTractiveEffort/Reliability/None` 五项，其余变量一律回落到"不设条件"显示 ⇒ 显示与执行进一步脱节。
- 需要在改码前先向玩家确认口径：是把池条目的规则统一成程序（删掉旧单条件 UI + 读档时把旧单条件转成一条 `If` 程序），还是反过来让单条件继续生效。

## 十、第 455 轮第一次实机复测（2026-11-17）：机车类型条件不可用

### 10.1 玩家原话

> 「编译已经完成，向你报告：当我选择判断条件为"机车类型"的时候，会跳出红框"不能这样做"，同时规则的进度无法保存。此外，无法选择机车类型为"是"或"否"，这是因为判断条件只有"至多""至少"导致的。我希望你根据每个判断条件添加符合的判据（是否【布尔类型】，大小于，等于，大小等于【数值类型】）」

### 10.2 三个症状

1. 选「机车类型」→ 红框「不能这样做」（= `STR_ERROR_CAN_T_DO_THIS` 家族），规则改动不落盘。
2. 比较符只有「至多 / 至少」两种，枚举型变量无法用「是 / 否」表达。
3. 期望：按变量类型给判据集合 —— 布尔/枚举类给「是 / 否」，数值类给「大于 / 小于 / 等于 / 大于等于 / 小于等于」。

### 10.3 猜测根因（待验证）

- **G-455-4（强嫌疑，症状 1）**：`R3RDispatchInstruction` 的校验/存档读入侧对 `var` 有**白名单或上界判断**（很可能是 `if (ins.var > R3RDispatchRuleVariable::Counter) return CMD_ERROR` 或 switch 无 default 分支落空），我在枚举末尾追加的 `EngineClass = 10` 没进了校验集合 ⇒ 命令层直接返回 `CMD_ERROR` ⇒ UI 弹「不能这样做」、`Apply()` 失败 ⇒ 改动静默丢弃。
  待验证判据：`src\station_cmd.cpp` 里校验 dispatch 程序的函数（`CmdSetR3RDispatchProgram*` 一族）对 `var` 的取值范围检查；以及 `Apply()`（`src\r3r_dispatch_program_gui.cpp`）里弹错的调用点。
- **G-455-5（确定，症状 2）**：`R3RDispatchRuleOp` 目前只有 `AtLeast`/`AtMost` 两个成员（`station_base.h:917-920`），`DispatchOpString` 也只映射这两项、`ToggleOp()` 只在两者间来回切 ⇒ 枚举型变量只能"至少 3"这种读法，没法"是 蒸汽"。

### 10.4 修法口径（按玩家要求，待落地）

- `R3RDispatchRuleOp` 追加 `Equal`/`Greater`/`Less`/`NotEqual`，**数值追加在末尾**（旧值 `AtLeast=0`/`AtMost=1` 不动，存档里已写的条件语义不变）。
- 变量分两类：
  - **数值类**（MaxSpeed/Power/MaxTractiveEffort/Reliability/PoolIssued/PoolRemaining/Counter）：判据 = 大于等于/小于等于/等于/大于/小于。
  - **枚举类**（EngineClass）：判据 = 是/否（`Equal`/`NotEqual`），值为类型下拉。
- 求值时按 op 分派；`EngineClass` 只做等值比较。
- UI：`WID_DP_OP` 若可点则改成**下拉**（按当前变量给可用判据集合），否则 `ToggleOp()` 按可用集合循环。

## 十、构建树核实（玩家 2026-11-17 第三次答复「你似乎用了另一棵构建树，而非内测树」）

只读取证，本轮**未启动任何构建**，证据全部来自文件时间戳与日志原文：

| 证据 | 读数 |
| --- | --- |
| `build\R3R_fullbuild.log` | **2026/11/17 22:34**，15 986 B，首行 `ninja: Entering directory 'build'`，步数 `[1/705]`→`[704]`，尾部停在 `[128/704] ... script_game.cpp.obj` 后 `^C` |
| `build\R3R_fullbuild.done` | **不存在**（脚本只在正常结束时写 done）⇒ 那次全量没跑完、没链接 |
| `build\openttd.exe` | 仍是 **2026/11/16 21:39:28**（旧版，未被子进程更新） |
| `build\R3R_incbuild.done` | 2026/11/16 21:39:32 `EXIT_CODE=0`，guard=`incremental is safe`，昨天只重编了 `src\sl\couple_group_sl.cpp` |
| `build-release\R3R_release_fullbuild.log/.done` | 2026/11/13 03:10（与本次无关，今天零写入） |
| `build-release\R3R_release_build.done` | 2026/11/09 01:09 |
| 进程 | 无 `ninja` / `link` / `openttd` 残留 |

结论（按文件判定）：上一轮启动的是**内测树 `build\`**（`R3R_fullbuild.cmd`，`ninja` 自己打印的目录就是 `build`），不是 `build-release`；发布树今天没有任何写入。**但那次是全量模式（705 步）**，原因是本轮改了 `src\order_base.h`、`src\station_base.h`、`src\lang\english.txt`、`src\lang\simplified_chinese.txt`，`R3R_inc_guard.ps1` 按既有规则判「新 header/lang 文件晚于最新 obj」→ 删光 obj 转全量。⇒ 「看起来像另一棵树」的观感来自**全量 705 步的规模**，不是树走错。

当前构建树状态：护栏已删光 obj，随后只编回 128 个就中断 ⇒ `build\` 现在是「128 个新 obj + 576 个缺失 obj」的半全量状态；下次 `inc` 会由 ninja 自动补齐剩余步骤（约 15~18 分钟），护栏是否再判 REMOVED 都不影响正确性。

口径（硬性，已按玩家纠正固化）：日常编译唯一入口 = 工作区根目录 `R3R_fullbuild.cmd inc detach`，产物判据看 `build\openttd.exe` + `build\R3R_fullbuild.log`；`build-release\` 只在玩家明确要求发布版时才允许触碰。

源码落盘核实（`git status --short -- src` + 关键字搜索）：第 455 轮改动**已全部落盘**——`order_gui.cpp`（`R3RHasTemporaryCoupleGroup` / `STR_ORDER_COUPLE_TEMP_GROUP_SUFFIX`）、`order_base.h`（`HasR3RStopAtStation`）、`train_cmd.cpp`（`_r3r_dispatch_empty_notified`）、`r3r_dispatch_program_gui.cpp`（`STR_R3R_DP_VAR_ENGINE_CLASS`）、双语 lang 均已命中。另有两处历史残留未跟踪备份 `src\order_gui.cpp.bak`、`src\train_cmd.cpp.r3r_bak`（不进构建，待玩家决定删留）。

## 十一、KI-653（需求 6）取证探针落地 + 本轮构建（2026-11-17 晚）

### 11.1 已确认的触发点唯一性

- `CheckConsistencyOfArticulatedVehicle()`（`src\articulated_vehicles.cpp:339-377`）是 `STR_NEWGRF_BUGGY_ARTICULATED_CARGO`（「购买列表与购买后的货物/改装信息不同」）在**全树唯一**的 `ShowNewGrfVehicleError` 调用点（:375），因此玩家看到的弹框一定来自这里，不可能是别的地方改的文案。
- 该函数比较四样东西：
  - 购买侧：`GetArticulatedRefitMasks(v->engine_type, true, &pu, &pi)`（:346）+ `GetCapacityOfArticulatedParts(v->engine_type)`（:347）；
  - 实车侧：沿 `v->HasArticulatedPart() ? v->GetNextArticulatedPart() : nullptr` 逐节累 `ru/ri`（:353-362），再用 `cargo_cap > 0` 的节汇出 `real_default_cargoes` 判 `carries_more`（:364-371）。
- ⇒ 不一致只可能来自两类原因：**(甲) 实车侧走到的节比购买侧少/多**（artic 链在造车瞬间就被改写，例如 R3R 的 `ConsistChanged` 把部件清成非 artic），或 **(乙) 同一节的 refit 掩码本身不同**（引擎/GRF 侧数据被改）。这两个解释的判别数据完全不同，所以本轮**不加任何行为改动**，只加一行取证。

### 11.2 探针（只读，事件级）

- `src\articulated_vehicles.cpp` 的触发点内加 `BUY-REFIT eng= veh= n= pu= pi= ru= ri= more= dflt=`：
  - `n` = 沿与判定完全相同的 `HasArticulatedPart()/GetNextArticulatedPart()` 走到的节数（**甲/乙的判别主字段**：`n` 少于购买侧应有节数 ⇒ 甲）；
  - `pu/pi` 与 `ru/ri` 并排 ⇒ 直接看出是 union 少了位、还是 intersection 多了位；
  - `head` 变量（:342）此前已埋但未使用，本轮把它用起来（它是 `v` 被推进到 nullptr 之前的原始头指针）。
- 探针 tag 为 `BUY`，**必须**登记进 `R3RDbgTagAllowed()` 白名单，否则会被 tag 过滤吞掉（该陷阱已连续踩过三次，见 `r3r_perf.h:337-350` 的注释）。已在 `src\r3r_perf.h` 的 `SPD` 之后加 `if (n >= 3 && strncmp(fmt, "BUY", 3) == 0) return true;`。

### 11.3 猜测根因（待验证）G-455-3

- **G-455-3**：R3R 在造车提交点把「真 artic 部件」改造为「打散组」（`ArticGroupHead`/`ArticGroupMember` 角色位 + 清 `GVSF_ARTICULATED_PART`）或假引擎身份，使 `HasArticulatedPart()` 在 `CheckConsistencyOfArticulatedVehicle()` 运行时已经为 false ⇒ 实车侧只走到父车一节 ⇒ `ru/ri` 恒等于父车自己的掩码，与购买侧整组 union 不等 ⇒ 弹框。
  - 支持证据：第 170 轮（KI-268/KI-273 一族）已确认 R3R 会在多条路径上把真 artic 组打散成普通车挂成物理链，而打散后的车不再报 `HasArticulatedPart()`。
  - 待验证判据：`BUY-REFIT` 的 `n` 是否明显小于该引擎 GRF 的 artic 部件数（例如 1 vs 3）；若是，则甲成立，修法方向 = 该判定改用「购买时的引擎 artic 链」而非实车链，或把打散时机推迟到判定之后。
  - 反判据：若 `n` 与购买侧节数相同而 `pu != ru`，则乙成立，问题在引擎侧数据被 R3R 改过，需要另查 `GetAvailableVehicleCargoTypes`/`GetArticulatedRefitMasks` 的 R3R 改动面。

### 11.3bis 【已撤回】根因推断（原写"读码即可定案"，实为误判，见 §12）

> **撤回声明（2026-11-17 晚，第 455 轮续）**：下面这一节的结论**错误**，已由 §12 逐行证据推翻。
> 错因：只读了 `vehicle_base.h` 的 `HasArticulatedPart()`，没读 `Train::IsArticGroupMember()` 的重写
> （`src\train.h:212-215` 为 `IsArticulatedPart() || flags.Test(ArticGroupMember)`）⇒ 真 artic 部件本来就
> 满足该谓词。据此落地的 `articulated_vehicles.cpp` 改法在语义上是 **no-op**，已回退为原生写法。
> 本节正文保留原样，仅供追溯，**不得再作为结论引用**。

读 `src\vehicle_base.h:1294-1314` 得到逐字证据：

```1288:1303:src\vehicle_base.h
		return !this->IsArticulatedPart() && this->Next() != nullptr && this->Next()->IsArticGroupMember();
	}
	/** R3R: is this vehicle inside an articulated-group semantic (head or member)? */
	virtual bool InArticGroup() const { return this->IsArticGroupHead() || this->IsArticGroupMember(); }
	/**
	 * Check if an engine has an articulated part.
	 * @note The group-role view is used here (next vehicle is a group member), so
	 * de-articulated groups are traversed as one unit exactly like real ones.
	 */
	inline bool HasArticulatedPart() const
	{
		return this->Next() != nullptr && this->Next()->IsArticGroupMember();
	}
```

- **【错误】** ~~结论：R3R 把 `HasArticulatedPart()` 的判据从「下一节是**真** artic 部件」换成「下一节是**组角色成员**」。真 artic 部件由 `AddArticulatedParts()` 创建，只被打上 `GVSF_ARTICULATED_PART`（`IsArticulatedPart()` 为真），**不**打 `ArticGroupMember` ⇒ 对真铰接车该谓词恒 false。~~
  - 实际：`Train::IsArticGroupMember()` **已经 OR 上了** `IsArticulatedPart()`，真部件恒为组角色成员 ⇒ 谓词对真铰接车恒 **true**。
- **【错误】** ~~于是 `CheckConsistencyOfArticulatedVehicle()` 的 do-while 在父车就停…每次购买真铰接车必弹。~~
- 修法（**已回退**）：当时把遍历改成「真部件 or 组角色成员」都认；因两者等价，该改动是 no-op，已于第 455 轮续回退成原生 `v->HasArticulatedPart() ? v->GetNextArticulatedPart() : nullptr`，只保留注释说明。
- 同函数内保留 `BUY-REFIT` 只读探针（本轮已扩成三行：汇总 + 实车逐节 + 购买侧逐节），复测判据改为「`n == pn` 且 `pu==ru && pi==ri` 且 `more=0`」。
- 同族疑点 **REG-455-A 已排除**：`articulated_vehicles.cpp` 的 `GetCargoTypesOfArticulatedVehicle()` 用的同一谓词对本谓词无回归（真部件满足组角色成员），**无需改动**。

### 11.4 本轮构建

- 入口沿用 `R3R_fullbuild.cmd inc detach`（未新建任何 `.cmd`）。
- 护栏 `build\R3R_incbuild.guard.log`：`1 header/lang file(s) are newer than the newest object file` / `newest object = 2026-11-17 22:34:01` / `newer: src\r3r_perf.h (2026-11-17 22:43:20)` / `REMOVED 47 object file(s) - upgrading this build to a FULL rebuild`。
  - 说明：上一轮中断后 `build\` 只剩 47 个 obj，故本次只需补 705-47 步左右，属全量但基数小。
- 构建自证待回填（见 §11.5）。

### 11.5 构建自证（已回填）

- **第一次（23:0x 前）**：`EXIT_CODE=1`，唯一失败点 `articulated_vehicles.cpp(392-394): error C2440 无法从 CargoTypes 转换为 uint` —— 探针里 `(uint)purchase_refit_union` 写法不合法（`CargoTypes` 是 `EnumBitSet`，必须走 `.base()`）。改为 `purchase_refit_union.base()` 等五处后重建。
- **第二次**：`build\R3R_incbuild.guard.log` = `GUARD: incremental is safe (no header/lang file is newer than the newest object)` ⇒ 走增量 247 步（第一次已编完 248 步）。
- **判决**：`build\R3R_incbuild.done` = `EXIT_CODE=0`；日志尾部 `[247/247] Linking CXX executable openttd.exe`；`error C*` / `FAILED:` 计数 0。
- **产物**：`build\openttd.exe` @ 2026-11-17 23:12:46（53 028 352 B）；`articulated_vehicles.cpp` @ 22:59:32 → `articulated_vehicles.cpp.obj` @ 23:00:03 → exe @ 23:12:46 ⇒ obj/exe 均晚于源码。
- **exe 字面量 3/3 命中**：`BUY-REFIT` / `STOP-AT-STATION` / `DISP-POOL-EMPTY-NOTIFY`。
- **生成头新串 5/5 命中**（`build\generated\table\strings.h`）：`STR_ORDER_R3R_STOP_AT_STATION=0xC85`、`STR_ORDER_R3R_STOP_AT_STATION_TOOLTIP=0xC86`、`STR_ORDER_R3R_STOP_AT_STATION_SUFFIX=0xC89`、`STR_R3R_DP_VAR_ENGINE_CLASS=0xCC1`、`STR_R3R_DP_CLASS_STEAM=0xCC2`、`STR_ERROR_R3R_DISPATCH_NO_SCHEDULE=0xCE3`、`STR_ORDER_COUPLE_TEMP_GROUP_SUFFIX=0xF60`。
- **read_lints**：`order_gui.cpp` / `train_cmd.cpp` / `station_cmd.cpp` / `r3r_dispatch_program_gui.cpp` / `station_base.h` / `order_base.h` / `articulated_vehicles.cpp` 全 0 条。
- 发布树 `build-release` **未编**（按口径只在玩家明确要求时编）。

### 11.6 本轮未做

- KI-652(b)（实际分配不正确）**仍待玩家现场**：已登记两个强嫌疑 G-455-1（恒 0 三项条件恒判假，导致规则永不匹配）与 G-455-2'（池条目的旧式单条件 `rule_var/rule_op/rule_value` 仍随档保存，但 `R3RDispatchPoolPick()` 只跑 `prog`，显示与执行是两套）——需要玩家提供「界面上显示的规则文本 + 该车实际的类型/速度/功率读数 + 分配结果」，否则改任一侧都可能改错方向。
- 需求 6 的**修复**未做，只做到「能定位」。


## 十二、第 455 轮续（2026-11-17 深夜）：KI-653 根因误判撤回 + 探针扩到「能直接指认」

### 12.1 撤回：§11.3bis 的「根因做实」不成立

本轮实读逐行证据：

- `src\vehicle_base.h:1312-1315`：`HasArticulatedPart()` 恒为 `Next() != nullptr && Next()->IsArticGroupMember()`。
- `src\train.h:212-215`：`Train::IsArticGroupMember()` 恒为 `this->IsArticulatedPart() || this->flags.Test(VehicleRailFlag::ArticGroupMember)`。

⇒ 真 artic 部件（`GVSF_ARTICULATED_PART`）**恒满足** `IsArticGroupMember()`，所以 `HasArticulatedPart()` 对真铰接车同样为真；
§11.3bis 的「实车侧只走父车一节」在代码上不可能发生（除非整组被 `ClearArticulatedPart()` 且未授组角色位，那时两个谓词又同为假）。
⇒ 同族疑点 **REG-455-A 一并关档**：`GetCargoTypesOfArticulatedVehicle()` 用同一谓词**没有**回归。

### 12.2 代码回退（只改 `.cpp`，未碰 `src\*.h`）

- `src\articulated_vehicles.cpp` 的 `CheckConsistencyOfArticulatedVehicle()` 遍历回退成原生
  `v = v->HasArticulatedPart() ? v->GetNextArticulatedPart() : nullptr;`，并把注释改成正确说明
  （原注释把"只认组角色位"当事实，是错的）。回退后语义与第 455 轮第一版逐字相同，**零行为差异**。

### 12.3 探针扩到「能直接指认」

原 `BUY-REFIT` 只有一行且**没有购买侧节数**，`n` 无法判读（这正是上一轮误判的温床）。现在 mismatch 分支内输出三行：

- `BUY-REFIT eng= veh= n= pn= pu= pi= ru= ri= more= dflt=`
  - 新增 `pn` = 购买侧节数（与 `GetArticulatedRefitMasks()` 同一条引擎回调链，`front=nullptr` 口径）。
- `BUY-REFIT-PART i= eng= cargo= cap= mask= pcap=`（实车逐节；`pcap` = 该货种在购买侧的默认容量，`pcap=0` 即 `carries_more` 的成因节）
- `BUY-REFIT-PP i= eng=`（购买侧逐节）

判读表（复测后按此填，不要自由发挥）：

| 观测 | 判定 | 下一步 |
|---|---|---|
| `n == pn` 且 `pu==ru && pi==ri` 且 `more=0` | 不弹框 | 需求 6 结案 |
| `n != pn` | 实车链与购买链**长度**不同（造车瞬间链被改写） | 对照 `PART`/`PP` 的 `eng=` 序列，找出多/少的那一节 |
| `n == pn` 但 `pu!=ru` 或 `pi!=ri` | 同一引擎的 refit 掩码两侧不同 ⇒ 引擎侧数据被改写 | 查 `GetAvailableVehicleCargoTypes` 的 R3R 改动面 |
| `more=1` 且某节 `pcap=0` | 该节 `cargo_cap>0` 的货种不在购买侧默认容量里 | 查 `ConsistChanged` 容量块 / KI-641 保护 |

### 12.4 仍然未做

- KI-652(b)「实际分配不正确」：**仍待玩家现场**（同 §11.6 第一条，需要界面规则文本 + 该车实测读数 + 分配结果）。
- 需求 6 的**行为修复**仍未做：必须先拿到 §12.3 的探针日志才能定方向（本轮只把判据做成可读的，避免再出现上一轮那种"读一半代码就宣布根因"）。


## 十三、第 455 轮续二（2026-11-18 凌晨）：§10.4 三个症状的实证与落地

### 13.1 G-455-4 实证成立（症状 1「选机车类型 → 红框不能这样做」）

逐行证据（本轮只读，未改）：

- `src\station_cmd.cpp:3127`：`if (ins.var > R3RDispatchRuleVariable::Counter) return CMD_ERROR;`
  —— 指令校验的上界写死在 `Counter`（值 9），而第 455 轮**追加在枚举末尾**的 `EngineClass`（值 10）落在上界之外 ⇒ 任何带「机车类型」条件的程序被命令层整份拒绝 ⇒ UI 弹 `STR_ERROR_CAN_T_DO_THIS` 家族红框、`Apply()` 失败、改动不落盘。**症状 1 = 纯上界笔误，与 GRF/存档无关。**
- 同族第二处：`src\sl\station_yard_sl.cpp:243`：`if (ins.var > R3RDispatchRuleVariable::Counter) ins.var = R3RDispatchRuleVariable::None;`
  —— 即使绕过校验写入成功，**读档时也会把 EngineClass 静默改成「无条件」**。必须同步放宽，否则"存档往返丢条件"。
- 同族第三处（op 上界，症状 2 的伏笔）：`station_cmd.cpp:3128` 与 `station_yard_sl.cpp:244` 都是 `> AtMost`；`station_cmd.cpp:3063` 旧式单条件命令同样是 `> AtMost`。

### 13.2 G-455-5 确认（症状 2「比较符只有至多/至少」）

- `src\station_base.h:950` 的 `R3RDispatchRuleOp` 只有 `AtLeast`/`AtMost` 两员；`r3r_dispatch_program_gui.cpp:128-131` 的 `DispatchOpString()` 也只映射这两项。
- 因此枚举类变量（EngineClass）在数据层无法表达「是/否」；当前 `DispatchConditionOpLabel()`（:102-106）只是**显示**成「是」，底层 op 仍恒为 `AtLeast`，求值时 `R3RDispatchConditionHolds()` 对 EngineClass 走 `have == value`（`station_cmd.cpp:2474`）才碰巧正确。一旦玩家想表达「不是电力」，当前结构无解。

### 13.3 【撤回】G-455-2'（"显示与执行两套"不成立）

- `R3RDispatchRuleText()`（`src\station_gui.cpp:3680`）是 `static` 函数，全树**零调用点**（`search_content` 只命中定义行 3680，无任何引用）。
- ⇒ 池条目的旧式单条件 `rule_var/rule_op/rule_value` 现在既不被求值（`R3RDispatchPoolPick()` 只跑 `prog`）也不被显示（显示走 `r3r_dispatch_program_gui.cpp` 的程序视图）。
- 结论：G-455-2' 的「玩家看到单条件、实际按程序发牌」在**当前 GUI 下不可能发生**（该函数是第 451 轮旧 UI 的死代码）。
- 残留（低）：死函数 + 旧字段仍随档保存（`station_yard_sl.cpp:164-201` 仍写读），属可清理项，不构成行为缺陷。**KI-652(b) 的剩余嫌疑 = 无**（症状 1 被 13.1 完全解释：条件存不进档 ⇒ 规则等于不存在 ⇒ "没按规则分配"）。

### 13.4 待落地清单（按 §10.4 玩家口径，下一轮直接照此做）

1. `station_base.h`：`R3RDispatchRuleOp` 末尾追加 `Equal`、`NotEqual`、`Greater`、`Less`（`AtLeast=0`/`AtMost=1` 不动，旧档语义不变）。
2. `station_cmd.cpp`：`ins.var` 上界 `Counter` → `EngineClass`（:3127）；`ins.op` 上界 `AtMost` → `Less`（:3128、:3063）；`R3RDispatchConditionHolds()` 按 op 分派（`EngineClass` 只认 Equal/NotEqual）。
3. `src\sl\station_yard_sl.cpp`：两处 clamp 上界同步（:243 `EngineClass`、:244 `Less`）。
4. `r3r_dispatch_program_gui.cpp`：`DispatchOpString()` 补四串；`ToggleOp()` 按变量类别循环（数值 6 档 / 枚举 2 档）；切到 `EngineClass` 时 op 置 `Equal`（现 :421 置 `AtLeast`，与新语义不符）；`DispatchConditionOpLabel()` 对 EngineClass 按 op 显示「是/否」。
5. `lang\english.txt` + `simplified_chinese.txt`：新增 `STR_R3R_DP_OP_EQUAL/NOT_EQUAL/GREATER/LESS`（英文 equal/not equal/greater/less；中文 等于/不等于/大于/小于）。
6. 改 `station_base.h` ⇒ 护栏必判全量（约 700 步、16~20 分钟），按口径只编内测树。

### 13.5 本轮构建

- `R3R_fullbuild.cmd inc detach` @ 2026-11-18 00:11 启动，`build\R3R_incbuild.guard.log` = `GUARD: incremental is safe`，ninja PID 4312 运行中（承接 §12.2 的探针回退 + `INVALID_ENGINE`→`EngineID::Invalid()` 修正）。

### 13.6 §13.4 已落地（2026-11-18，改 5 个文件）

| 文件 | 改动 |
| --- | --- |
| `src\station_base.h` | `R3RDispatchRuleOp` 末尾追加 `Equal`/`NotEqual`/`Greater`/`Less`（0/1 不动，附"编号进存档、只能追加"注释） |
| `src\station_cmd.cpp` | ①`CmdR3RSetDispatchPoolRule` 上界 `Reliability`→`EngineClass`、`AtMost`→`Less`；②`CmdR3RSetDispatchProgram` 上界 `Counter`→`EngineClass`（**症状 1 的真凶**）、`AtMost`→`Less`；③`R3RDispatchConditionHolds()` 改 `switch(op)` 五档 + `EngineClass` 只认 `Equal`/`NotEqual`（旧档 `AtLeast` 仍按"相等"解释，行为向后兼容） |
| `src\sl\station_yard_sl.cpp` | `R3RUnpackProgram()` 两处 clamp 上界同步为 `EngineClass` / `Less`（否则读档把新条件静默改成"无条件"） |
| `src\r3r_dispatch_program_gui.cpp` | ①`DispatchOpString()` 六档 switch；②`DispatchConditionOpLabel()` 枚举给「是/不是」；③`ToggleOp()` 数值六档循环、枚举「是↔不是」；④`SetVariable()` 切到枚举时把非法 op 收成 `Equal` |
| `src\lang\english.txt` / `simplified_chinese.txt` | 新增 `STR_R3R_DP_OP_IS_NOT`/`EQUAL`/`NOT_EQUAL`/`GREATER`/`LESS`（is not/equal to/not equal to/greater than/less than；不是/等于/不等于/大于/小于） |

- `read_lints` 上述 4 个 .cpp/.h 全 0 条。
- 因改了 `station_base.h` + 两个 lang txt，下一次构建必被护栏判全量（`REMOVED 620 object file(s)`），按口径只编内测树。

### 13.7 第一次构建失败（EXIT_CODE=1）与原因 —— **工具层面的假成功**

- `build\R3R_incbuild.done` @ 2026-11-18 00:20 = `EXIT_CODE=1`；`build\R3R_incbuild.log` 报错全部集中在 `r3r_dispatch_program_gui.cpp(1337/4009/4112/4113/4116/4447...)`：`error C2838/C2065 "Equal"/"NotEqual"/"Greater"/"Less": 未声明的标识符`。
- 排查 `src\station_base.h:924-928` 发现枚举**仍是两员**：`replace_in_file` 对该文件的第一次调用**返回了「Replace file succeeded」，但内容没有落盘**（同一批里 `station_yard_sl.cpp` 那次是正常落盘的）。
- 补救：改用**纯 ASCII 锚点**（`AtMost,` 单行）重做插入，再清掉临时插入造成的重复声明，最终 `station_base.h:925-935` 为 `AtLeast/AtMost/Equal/NotEqual/Greater/Less` 六员（`read_file` 复核 + `search_content` 复核 8 处引用点全在）。
- **教训（写入工具纪律）**：`replace_in_file` 之后必须 `read_file`/`search_content` 复核**落盘结果**，尤其同一批并行改多个文件时；不能只信工具返回的 success。
- 复核后的二次构建：`R3R_fullbuild.cmd inc detach` @ 2026-11-18 00:33（本次改了两处 lang + 一个 .h ⇒ 护栏应判 `REMOVED 620 object file(s)` 全量，约 692 步）。

