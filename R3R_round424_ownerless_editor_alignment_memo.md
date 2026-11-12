# 第 424 轮：无主排程编辑器向有主 `OrdersWindow` 对齐（**不上手论证**）

- **轮次**：第 424 轮（2026-11-12）
- **性质**：**只论证，不改任何源码，不构建**。本文所有行号均为当前工作树实读值。
- **对应 KI**：`R3R_KNOWN_ISSUES.md` 的 **KI-607**（状态由「待实现」改为「论证完成，待拍板」）。
- **玩家原话**：「为什么无主排程的编辑不能向有主的编辑对齐（除了依附者这一属性之外，其它的最好照抄）」+「让我们先做一个不上手的论证」。

---

## 一、结论先行

1. **可对齐度比预期高**：有主窗口里**真正必须依赖车辆实时状态的控件只有 4 个**（Skip to order、Refit、Stop sharing、Occupancy/当前订单显示），其余 **20 余个控件**的参数编辑全部汇聚到**唯一命令** `Commands::ModifyOrder`（`src\order_gui.cpp:88` 的公共 `PostModifyOrder` 助手）。
2. **真正的差距只有三样「状态源」**，不是「功能多寡」：
   - **车型 `VehicleType`**：`OrderList` 不带，无主窗口只能硬编码 `Train`（`order_gui.cpp:5544`）；
   - **公司 `CompanyID`**：`OrderList` **已经有**（`GetR3RScheduleOwner()`，见 `order_cmd.cpp:4172`）⇒ 这一项已就绪；
   - **车辆实时状态**：`cur_real_order_index` / `vehicle_flags`（`ScheduledDispatch`、`TimetableSeparation`）/ 载货能力 / `current_order`。
3. **命令层不能"复制一份 `CmdModifyOrder`"**。`CmdModifyOrder` 里那张 `switch (mof)` 的白名单是 **KI-124 的事故现场**（白名单漏项 = UI 静默失败、无编译期提示，`order_cmd.cpp:2650-2652` 的注释就是那次的教训）。复制一份必然出现两份白名单漂移。
4. **推荐路线 R2**：把 `CmdModifyOrder` 的**判定体**抽成一个 `OrderHost` 载体（有主传车辆、无主传 `OrderList*` + `vtype` + `owner`），命令壳只做一层薄转发。这样「对齐」= 让无主侧走进**同一张 switch**，而不是并列第二张。
5. **前置必修**：`OrderList` 需要一个 `VehicleType` 载体（含存档），否则「目的地枚举」和一半的参数校验无法判定（详见 §五）。

---

## 二、两侧现状事实（实读）

### 2.1 有主 `OrdersWindow`：控件 → 命令归属

控件全集见 `src\widgets\order_widget.h:16-81`（`WID_O_*`）。按「能不能脱离车辆」分三类：

| 类 | 控件 | 发什么命令 | 是否必须车辆 |
|---|---|---|---|
| **A** | `NON_STOP` / `GOTO`(下车地点) / `FULL_LOAD` / `UNLOAD` / `DEPOT_ACTION` / `REVERSE` / `COND_*`（变量/比较/值/货物/槽/计数器/时间/时刻表/调度）/ `SLOT` / `SLOT_GROUP` / `COUNTER_*` / `TEXT_LABEL` / `DEPARTURE_VIA_TYPE` / `R3R_YARD` / `COUPLE_SIDE` / `COUPLE_TEMP_GROUP` / `R3R_SEPARATION` | **全部** `Commands::ModifyOrder`（`MOF_*`） | 形参要 `VehicleID`，但**语义只碰 `Order` 本体**（`order_cmd.cpp:2670-3030+` 的 switch 内只读 `v->owner` / `v->type` / `v->IsGroundVehicle()` / 列表派生查询） |
| **B** | `DUPLICATE` / `MOVE_UP` / `MOVE_DOWN` / 列表内重排 | `DuplicateOrder`（`:2465`、`:4688`）、`MoveOrder`（`:4718`） | 需要「列表 + 索引」，**无主侧已有等价物**（`Duplicate/MoveR3RLooseOrder`） |
| **C** | `SKIP` / `REFIT` / `REFIT_DROPDOWN` / `STOP_SHARING` / `MGMT_*` / `SHARED_ORDER_LIST` / `ADD_VEH_GROUP` / `OCCUPANCY_*` | `SkipToOrder`（`:2390`）、`OrderRefit`（`:2442`、`:2445`）、`CloneOrder`（`:2427`、`:4884`）、`BulkOrder` | **真依赖车辆**：`v->cur_implicit_order_index` 与 `v->GetNumOrders()<2`（`CmdSkipToOrder` 首行 `order_cmd.cpp:2208`）、`GetRefitCapacity()`（改装容量）、共享链与编组 |

> **关键事实**：`Vehicle::HasFullLoadOrder()` / `HasConditionalOrder()` / `HasUnbunchingOrder()` 的实现**只遍历订单表**（`src\vehicle.cpp:4201-4236`，函数体是 `for (const Order *o : this->Orders())`），零物理状态依赖 ⇒ 可以无损下沉到 `OrderList` 侧。

### 2.2 无主 `R3RScheduleEditorWindow`

- 类定义 `src\order_gui.cpp:5487-5675`，布局 `:5446-5474`，`WindowDesc` `:5677-5682`，入口 `ShowR3RScheduleEditor()` `:5684-5689`。
- 窗口号 = `OrderList` 索引（`:5543`）；成员只有 `OrderListID schedule` / `VehicleType vtype`（写死 `Train`，`:5544`）/ `int selected` / `Scrollbar *vscroll`。
- **5 个按钮**（`order_widget.h:84-93`）：`ADD / DELETE / DUPLICATE / MOVE_UP / MOVE_DOWN`；`OnClick` `:5601-5647` 全部发 `Commands::*R3RLooseOrder`（`order_cmd.h:101-104`）。
- `ADD` 只开下拉，`ShowAddOrderDropDown()`（`:5527-5538`）**只遍历本公司有设施的车站**；回调（`:5649-5665`）只产生 `MakeGoToStation` / `MakeGoToWaypoint` 两种订单。
- 绘制：`DrawOrderString(nullptr, order, i, y, …, this->vtype)`（`:5596`）。
- `GetInsertPosition()`（`:5521-5525`）：插在选中项之后，无选中则追加（**这是无主侧对「锚点依赖」的唯一改造，语义清晰，无需回退**）。

### 2.3 绘制层已经支持 `v == nullptr`

`DrawOrderString`（`order_gui.cpp:1034-1673`）内部有**至少 10 处** `v == nullptr` 分支：`:1038`（车型取 `vtype`）、`:1039`（地面车推断）、`:1043/:1047`（不画当前/隐式订单箭头）、`:1052`（不画空中编组灰字）、`:1084`（跳过车站可用性校验）、`:1363`（跳过班次名）、`:1450`（不收窄槽内措辞）、`:1549`（跳过槽位组告警）、`:1633`（跳过航程检查）、`:1652`（跳过已排定发车尾注）。

**判读**：这既是利好（绘制不用重写），也是隐患（这 10 处里至少 `:1363/:1450/:1549/:1633/:1652` 只是「当时没车就跳过了」，对齐后应当逐个复核是**必须跳过**还是**本该显示**）。

---

## 三、差距的本质：缺三个状态源，不缺控件

| 状态源 | 有主 | 无主 | 现成程度 |
|---|---|---|---|
| 订单表本体 | `v->GetEffectiveOrders()` | `OrderList::GetIfValid(schedule)` | **已有** |
| 公司 | `v->owner` + `CheckOwnership` | `list->GetR3RScheduleOwner()`（`order_cmd.cpp:4172`）+ `R3RScheduleEditableBy`（`:4182`） | **已有** |
| 车型 | `v->type` | 窗口成员写死 `Train`（`:5544`），`OrderList` 无此字段 | **缺，需前置改造** |
| 订单索引 | `cur_real/implicit_order_index` | 无（也不该有） | 结构性不需要 |
| 载货/改装能力 | `GetRefitCapacity()`、`cargo_capacity` | 无 | 结构性不需要（降级） |
| 车辆运动态 | `vehicle_flags` / `current_order` / `HasUnbunchingOrder()` 等 | `Has*Order()` 可从列表重算；`vehicle_flags` 无对应物 | **半缺 → 需定义默认语义** |

### 3.1 `CmdModifyOrder` 对 `v` 的真实依赖清单（`order_cmd.cpp:2584-3030+`）

| 依赖点 | 位置 | 无主侧替代 |
|---|---|---|
| `Vehicle::GetIfValid` + `IsPrimaryVehicle` | `:2590-2591` | 换成 `R3RGetEditableLooseSchedule()`（`:4274-4282`，已有硬闸门「无车引用」） |
| `CheckOwnership(v->owner)` | `:2593` | `R3RScheduleEditableBy(list, _current_company)` |
| `R3ROrdersSharedWithOtherCompany(v)`（KI-132） | `:2600` | 无主列表 `num_vehicles==0` ⇒ 恒 `false`，天然放行 |
| `v->GetNumOrders()` / `v->GetOrder(sel_ord)` | `:2603-2605` | `list->GetNumOrders()` / `list->GetOrder()` |
| `v->IsGroundVehicle()`（`MOF_NON_STOP`） | `:2674/:2678` | `vtype` 派生 |
| `v->type`（`MOF_STOP_LOCATION` / `COND_*` 槽位 / `MOF_R3R_*`） | `:2682/:2742/:2769/:2776/:2819/:2827/:2940/:2948/:2954/:2997/:3007/:3013` | `vtype`（**故必须存进 `OrderList`**） |
| `v->owner`（slot/counter/group 可用性、临时分组可见性） | `:2810/:2820/:2828/:2868/:2941/:2949/:2962/:3023` | 列表 owner |
| `v->HasUnbunchingOrder()` / `HasConditionalOrder()` / `HasFullLoadOrder()` | `:2718/:2727/:2733/:2735` | 自由函数，遍历 `*list`（同 `vehicle.cpp:4201-4236`） |
| `v->vehicle_flags.Test(ScheduledDispatch)` / `TimetableSeparation` | `:2729-2730` | **无对应物** ⇒ 无主列表按「未启用」处理（见 §七 R-3） |
| `v->GetEffectiveOrders()->GetScheduledDispatchScheduleCount()` | `:2881` | `list->GetScheduledDispatchScheduleCount()` |

> 结论：**除 `vehicle_flags` 两项外，全部可替换**。这两项只影响 `MOF_DEPOT_ACTION = DA_UNBUNCH` 时的三条否决，属可接受降级。

---

## 四、能力分级（对齐目标形态）

### A 档：可直接照抄（约 85% 控件）

目的地类型全集（车站 / 路点 / 车库 / 挂接 / 等待挂接 / 解挂 / 条件 / 槽 / 槽组 / 计数器 / 标签）、非直达、上下客方式、改装货物类型（`MOF_CARGO_TYPE_*`，**注意与 `OrderRefit` 是两件事**）、车库动作、逆向、解挂边界与模式、挂接侧、临时挂接分组、站场、自动分离、等待调度、条件订单全部参数、槽位、计数器、文本标签、发车板子类型。

### B 档：需前置改造才能照抄

| 项 | 缺什么 | 改造量 |
|---|---|---|
| 目的地枚举放开到路点/车库/挂接/解挂/条件 | `ShowAddOrderDropDown()` 只枚举车站（`:5527-5538`） | 中：需要一个「无主版 `GetOrderCmdFromTile`」，把 `v->type/owner/compatible_roadtypes/IsGroundVehicle` 换成 `vtype/owner` |
| 车型相关校验与显示 | `OrderList` 无 `VehicleType` | 中：字段 + 存档（`ORDL` 版本或 R3R 自有块）+ 窗口读它 |

### C 档：必须降级（4 个，建议**灰掉 + tooltip**，不要静默失败）

| 控件 | 硬依赖 | 降级文案口径 |
|---|---|---|
| Skip to order | `cur_implicit_order_index` / `GetNumOrders()>=2`（`order_cmd.cpp:2208`） | 「排程未绑定车辆，无法跳转」 |
| Refit（`WID_O_REFIT` / `WID_O_REFIT_DROPDOWN`） | `GetRefitCapacity()` | 「未绑定车辆，无法计算改装容量」 |
| Stop sharing / 共享列表 / 编组 | 共享链（`first_shared`）、`VehicleGroup` | 「无主排程不支持共享链」 |
| Occupancy / 当前订单 | `current_order`、实时占用 | 「无主排程无当前订单」 |

---

## 五、命令层三条候选路线（对比）

### R1：新开 `Commands::ModifyR3RLooseOrder`

- 复制 `CmdModifyOrder` 的 `switch (mof)` 到 `CmdR3RModifyLooseOrder`。
- **优点**：不动既有有主路径，零回归风险。
- **致命缺点**：**两份 MOF 白名单**。KI-124 已经证明这类白名单漏一项 = UI 静默失败且无编译期提示（`order_cmd.cpp:2650-2652` 的注释原文就是为这次事故留的）。此后每加一个 `MOF_*` 必须改两处，必然漂移。
- **结论：不推荐**（除非时间极紧且接受长期双写）。

### R2：把判定体抽成 `OrderHost`（**推荐**）

- 定义 `struct R3ROrderHost { OrderList *list; VehicleType vtype; CompanyID owner; Vehicle *veh /* 可为 nullptr */; }`；
- `CmdModifyOrder` 变成：构造 host（`veh` 非空）→ 调 `R3RModifyOrderImpl(host, sel_ord, mof, data, cargo, text)`；
- 新增 `CmdR3RModifyLooseOrder`：构造 host（`veh = nullptr`、`vtype` 来自 `OrderList`、`owner` 来自列表）→ 调**同一个** impl。
- **优点**：单一白名单、单一校验体；有主侧行为可做到逐字不变（host 里 `veh` 非空时走原分支）。
- **代价**：必须把 `CmdModifyOrder` 里 8 处 `v->owner`、9 处 `v->type`、2 处 `v->vehicle_flags`、4 处 `Has*Order()` 全部改写成 host 访问。**改动集中在一个函数 (~450 行)**，但属于「高危重构」——需要 `DoCommandFlag::Execute` 前后行为逐字对照。
- **风险点**：`CmdModifyOrder` 是 `DEF_CMD_TUPLE_LT`（线性测试/执行两遍语义），抽函数时**不能**把 `CheckOwnership` / `R3ROrdersSharedWithOtherCompany` 移到外面，否则 Test 与 Execute 的权限判定会分叉。

### R3：UI 侧挂到「临时车」再发原命令（否决）

- 优点：零命令层改动。
- **否决理由**：会凭空造车 / 需要一辆存档里的隐藏车 ⇒ 污染存档、污染车辆列表、污染 `Vehicle::Iterate()` 的所有统计（含 `GroupStatistics`、`VehicleGroupLengthSorter`），并与 KI-93 的 `loading_vehicles` 悬挂同类风险。**不做**。

### 5.1 前置改造：`OrderList` 的 `VehicleType`

- 现状：`OrderList`（`order_base.h:1638` 起，成员 `1647-1666`）无车型字段；窗口硬编码 `Train`（`order_gui.cpp:5544`）。
- 三选一：
  - (a) 新增 `VehicleType r3r_vtype` + 存档字段（增量小，但改 `order_base.h` ⇒ 触发全量重编 692 步）；
  - (b) 复用 `r3r_*` 现有位/字段的空位（若有）；
  - (c) **暂不存**，无主编辑器继续只服务列车（维持现状），把「公路/船/飞机排程」列为独立后续项。
- **建议 (c) 起步**：`vtype = Train` 是当前唯一使用场景，先让 A 档控件全部可用；等玩家真的需要公路排程再上 (a)。

---

## 六、目的地枚举放开（B 档细化）

有主侧建订单的实际路径是 `GetOrderCmdFromTile(v, tile)`（`order_gui.cpp:1681-1757`）——由**点击地块**反推订单类型，逐类依赖：`v->type`、`v->owner`、`compatible_roadtypes`、`IsGroundVehicle`、车库归属、路点归属。

无主侧两条路：

- **N1（推荐）**：不点地图，改为**类型菜单 → 二级下拉**（车站/路点/车库/挂接/等待/解挂/条件/槽/计数器/标签），每个二级下拉只枚举该类型的合法目标。这样彻底绕开 `GetOrderCmdFromTile` 的车辆依赖，且不改变鼠标交互模型（无主窗口本来就没有地图锚点）。
- **N2**：复用 `GetOrderCmdFromTile`，传一个「合成 v」或把签名改成 `(vtype, owner, tile)`。改动面比 N1 大，且仍要处理「车库归属公司」「路点类型」等判定。

**注意一个既有事实**：`OT_GOTO_COUPLE` / `OT_WAIT_COUPLE` / `OT_DECOUPLE` 与 `OT_GOTO_DEPOT` 在 `PreInsertOrderCheck`（`order_cmd.cpp:1310+`）里有车辆级校验，而 `CmdR3RInsertLooseOrder` **故意跳过** `PreInsertOrderCheck`（`:4287-4292` 注释明确说明：无主列表允许暂时不完整，交给绑定时再校验）。⇒ **放开目的地类型不需要新增校验**，只需放开枚举。

---

## 七、风险与不变量（必须写进落地判据）

| 编号 | 风险 | 说明 / 缓解 |
|---|---|---|
| **R-1** | `R3RGetEditableLooseSchedule` 的「无车引用」硬闸门（`order_cmd.cpp:4279`） | 这是**防悬挂**的唯一保险。R2 把 `CmdModifyOrder` 抽象后，**绝不能**让有车引用的列表走无主命令路径，否则改完不重算 `cur_real_order_index` ⇒ 索引越界/跳单。 |
| **R-2** | 插入只带 `GetCmdRefFields()` 四字段（`order_base.h:262`：`type/flags/dest/refit_cargo`） | 这不是差距——有主侧 `InsertOrderCmdData` 同样只带这四字段，`wait_time`/`timetable`/`OrderExtraInfo.xflags` 一律靠后续 `ModifyOrder` 补。**对齐后两侧一致**。 |
| **R-3** | `vehicle_flags` 两项（`ScheduledDispatch` / `TimetableSeparation`）在无主侧无对应物 | 无主列表按「未启用」处理 ⇒ `DA_UNBUNCH` 的三条否决（`order_cmd.cpp:2729-2730`）永不触发。**这是「宽松」而非「更严」**，不会让玩家填出非法组合。 |
| **R-4** | 绑定时必须重跑完整校验 | 现在 `R3RAssignSchedule`（`CmdR3RAssignSchedule`）把列表交给车辆时是否调 `PreInsertOrderCheck`/`CmdInsertOrderIntl` 级的校验，需要落地前核实；若不校验，无主编辑器可以造出「一把全载 + 一个条件订单 + 解挂」这类有主侧禁止的组合。**这是本轮论证唯一的硬未确认项（U-1）**。 |

---

## 八、需玩家拍板的决策点

| 编号 | 问题 | 选项 | 建议 |
|---|---|---|---|
| **C-1** | 命令层走 R1（复制）还是 R2（抽象）？ | R1 / R2 | **R2**：单一白名单，避免 KI-124 复发 |
| **C-2** | 车型载体是否现在就加？ | (a) 加字段+存档 / (b) 复用空位 / (c) 暂只支持列车 | **(c)**：当前唯一场景是列车，先拿 A 档 |
| **C-3** | 目的地交互走 N1（类型菜单）还是 N2（点地块）？ | N1 / N2 | **N1**：无主窗口无地图锚点，N1 更自然且零车辆依赖 |
| **C-4** | C 档四个控件是「灰掉 + tooltip」还是「直接不显示」？ | 灰掉 / 隐藏 | **灰掉**：玩家能看到「为什么不能点」 |
| **C-5** | `DrawOrderString` 的 10 处 `v==nullptr` 跳过项，哪些要恢复显示？ | 全恢复 / 逐个复核 / 保持现状 | **逐个复核**：至少 `:1363`（班次名）、`:1652`（发车尾注）应恢复 |
| **C-6** | 顺序：先做「参数编辑（A 档）」还是先做「目的地放开（B 档）」？ | A 先 / B 先 | **A 先**：A 档是玩家日常使用频次最高的（装载方式、边界、挂接侧） |

### 8.1 C-1~C-6 选项详解（供拍板用，2026-11-12 补）

> 本节把上表的简写展开成「选了这个会看到什么 / 代价是什么」，玩家按编号答一句即可。

#### C-1 命令层怎么造「无主版的参数编辑」

- **在问什么**：有主窗口改一个参数（非直达、全载、解挂边界、挂接侧、站场……）都是发同一条 `Commands::ModifyOrder`。这条命令一进门就要求「给我一辆车」。无主排程没有车，所以要么造一条新命令，要么让原命令接受「没有车」这种输入。
- **R1 = 新开一条 `CmdR3RModifyLooseOrder`，把原命令的参数处理整段复制一份**
  - 玩家可见：功能上一模一样，能改的都能改。
  - 代价：从今往后每加一个新参数，必须改**两处**。漏改一处 = 那个参数在无主窗口点了没反应、也不报错。KI-124 就是这么发生的（目的地下拉点了没反应，因为白名单漏了一项），当时排了很久。
  - 适合：只想最快看到效果，接受以后可能出这种"点了没反应"的小毛病。
- **R2 = 把「参数处理」抽出来，让有主和无主共用同一份**
  - 玩家可见：功能一样，而且不会出现「有主能改、无主改不了」的错位。
  - 代价：要动 `CmdModifyOrder` 这个约 450 行的核心函数（把 20 多处"车主的公司/车的类型"改成从参数里取）。改动集中、一次性，但必须保证有主窗口行为**逐字不变**，我要做前后对照。
  - 适合：愿意为一次性重构多花一点时间，换长期不出错。

#### C-2 无主排程要不要记住「是给什么车用的」

- **在问什么**：很多参数是否出现、下拉框列什么，取决于车型。例如"站台停靠位置（早/中/晚）"只有列车有、"非直达"只有地面车有。有主窗口从车里读车型，`OrderList` 里**没有**存这个，所以无主窗口现在写死"列车"。
- **(a) 现在就在 `OrderList` 里加一个车型字段并写进存档**
  - 玩家可见：将来无主排程也能给汽车/船/飞机编排程，参数显示正确。
  - 代价：要改核心头文件，触发**全量重编约 20 分钟**；还要加存档字段（老档读入默认按列车处理，一般安全）。
- **(b) 借用现有字段的空位**
  - 省一次全量重编，但字段布局一旦算错会踩存档兼容，风险大于收益。**不建议**。
- **(c) 暂不做，无主排程继续只服务列车**
  - 玩家可见：现在就能用，但无主排程只能编列车排程。以后真要给汽车编，再做一次。
  - 代价：零。
- **前提问题**：你打算让无主排程只服务列车，还是将来也要给公路/船/飞机用？只服务列车 ⇒ 选 (c)。

#### C-3 加订单时怎么挑「去哪儿 / 干什么」

- **在问什么**：无主窗口的「+」现在只列**本公司有车站设施的车站**，所以只能加"去某站"。有主窗口是**用鼠标点地图**来决定加什么（点车站=去站、点路点=去路点、点车库=去车库、点挂接=去挂接），靠 `GetOrderCmdFromTile` 反推，而这个函数要看车的类型、所属公司、兼容路型。
- **N1 = 先选「订单类型」，再选具体目标（两级下拉）**
  - 玩家可见：「+」先弹类型菜单（去车站/去路点/去车库/挂接/等待挂接/解挂/条件/槽/计数器/文本标签），选类型后再弹该类别的目标列表；等待挂接、解挂这类不需要目标。
  - 好处：完全不依赖车辆，实现干净；也不会误加（比如列不出没权限的车站）。
  - 代价：每种类型要写一个枚举列表，工作量中等但直白。
- **N2 = 让无主也能点地图**
  - 问题：无主窗口是个列表窗口，本来没有地图操作；点地图要判定"点的是什么设施""这辆车能不能去"，正是依赖车辆的地方。要么伪造一辆车（污染存档，已否决），要么把函数签名改成只吃(车型/公司/地块)——改动比 N1 更大，还得额外处理车库归属、路点类型。
  - **不建议**。

#### C-4 四个做不到的按钮怎么摆

- **在问什么**：跳转到订单、改装（Refit）、停止共享、当前占用/当前订单——这四项真的需要一辆活车（要算改装容量、要推进索引、要看共享链）。无主侧做不到，问题是**怎么告诉玩家**。
- **灰掉 + 悬停提示**：按钮仍在但变灰，鼠标停上去显示「未绑定车辆，无法跳转 / 无法计算改装容量 / 无主排程不支持共享链 / 无当前订单」。
  - 好处：玩家能看出"功能存在、只是这里用不了"，不会以为功能没做。
- **直接不显示**：界面更干净，但容易反复被问"为什么这里没有"。
- 另有一个折中（显示但点了弹错误）：不符合 OpenTTD 惯例，**不推荐**。

#### C-5 订单行文字里那些"没车就不显示"的内容

- **在问什么**：画一行订单文字的函数里，有 10 处写了"如果没有车就跳过这段显示"。当初是为了不崩先跳过，但其中有些其实跟车无关、无主排程也该显示。
- 逐项含义与判断：

| 位置 | 跳过的是什么 | 无主侧该不该显示 |
|---|---|---|
| `:1038` `:1039` | 车型（改用 `vtype`）/ 是否地面车 | **必须跳过**（本来就用 `vtype`） |
| `:1043` `:1047` | 该行左侧的"当前订单/下一个订单"箭头 | **必须跳过**（无主没有"当前"） |
| `:1052` | 空中编组灰字 | 待核（跟订单有关就恢复） |
| `:1084` | 车站可用性检查（站被拆/关闭时打警告） | **应恢复**：否则玩家看到指向已拆车站的订单却毫无提示 |
| `:1363` | 班次名 | **应恢复**（是订单内容） |
| `:1450` | 槽内措辞是否收窄 | 体验项，可保持 |
| `:1549` | 槽位组告警 | **应恢复**（有冲突要提示） |
| `:1633` | 航程检查 | **应恢复**（同可用性提示） |
| `:1652` | "已排定发车"尾注 | **应恢复**（订单自带发车时刻） |

- **选项**：全恢复（可能显示"当前订单"等无意义内容）/ 逐个复核（按上表判断）/ 保持现状（继续不提示车站已拆等）。
- **建议逐个复核**。

#### C-6 先做哪一半

- **在问什么**：要让无主排程追上，大致两件事——A「能把已有订单的参数改全」（装载方式、非直达、解挂边界、挂接侧、站场、自动分离、等待调度）与 B「能加更多类型的订单」（路点/车库/挂接/等待/解挂/条件/槽/计数器/标签）。
- **A 先**：马上可用，覆盖日常最高频的操作。
- **B 先**：能加更多订单类型，但因为参数还改不了，加出来的订单是"半成品"（比如加了条件订单却设不了条件和跳转槽）。
- 两者不是二选一，只是顺序。**建议 A 先**。

### 8.2 推荐组合（若不想逐项选，可直接说"按推荐来"）

`C-1=R2`、`C-2=(c) 暂只支持列车`、`C-3=N1`、`C-4=灰掉`、`C-5=逐个复核`、`C-6=A 先`。

---

## 九、落地后的复测判据（预留）

1. 无主排程可加入路点 / 车库 / 挂接 / 等待挂接 / 解挂 / 条件 / 槽 / 计数器 / 标签各类订单；
2. 每种订单的每种参数（`MOF_*` 全集）可编辑且**回显正确**（尤其 `MOF_DECOUPLE_BOUNDARY`、`MOF_COUPLE_SIDE`、`MOF_COUPLE_TEMP_GROUP`、`MOF_R3R_YARD`、`MOF_R3R_STATION_SEPARATION`）；
3. **有主窗口行为逐字不变**（重点：`CmdModifyOrder` 的 Test/Execute 双遍、`CheckOwnership` 报错串、KI-132 跨公司否决）；
4. Skip / Refit / 共享 / Occupancy 在无主侧呈**灰态 + tooltip**，点击无命令下发（日志无 `CmdSkipToOrder` / `CmdOrderRefit`）；
5. 负判据：有车引用的列表**永远**无法进入无主编辑命令（`R3RGetEditableLooseSchedule` 返回 `nullptr`，与 R-1 对应）；
6. 负判据：无主列表「编辑 → 绑定车辆 → 运行 → 解绑」全程无 assert、无索引越界（重点看 R-4）。

---

## 十、未确认项（落地前必须补证）

- **U-1**：`CmdR3RAssignSchedule`（`order_cmd.h:94`）在把无主列表交给车辆时，是否重跑 `PreInsertOrderCheck` / `CmdInsertOrderIntl` 级的车辆级校验？若否，无主编辑器可造出有主侧禁止的组合（见 R-4）。
- **U-2**：`OrderList::GetOrder()` / `GetNumOrders()` 与 `v->GetNumOrders()` 在**共享链**下是否等价（无主列表无共享链，理论上等价，但需确认 `GetNumOrders()` 里没有 `IsOrderListShared()` 分支）。
- **U-3**：`Order` 的 `OrderExtraInfo`（`xflags`：`HasR3RStationSeparation` / `HasR3RWaitDispatch` / `IsWaitFixed` / `LeaveType`）是否全部能经 `MOF_*` 覆盖；若有字段无 `MOF` 入口，则两侧都缺（不是对齐问题，但会影响判据 2 的完备性）。

---

## 十一、本轮口径

- 第 424 轮前半：**零源码改动、零构建**。`build\openttd.exe` 仍是第 423 轮 18:13:38（52 807 680 B）的 KI-606/608 修复版；发布树 `build-release\` 仍早于第 423 轮。
- 结论只认文件：本文 + `R3R_KNOWN_ISSUES.md` 的 KI-607。

---

## 十二、玩家拍板（2026-11-12，原话要点）

| 编号 | 拍板 | 玩家原话要点 |
|---|---|---|
| C-1 | **R2** | 「C1，按R2」⇒ 抽 `OrderHost`，有主/无主共用同一张 `MOF_*` switch |
| C-2 | **(c)** | 「C2，暂时按c」⇒ `OrderList` 暂不加 `VehicleType`，无主排程只服务列车 |
| C-3 | **N1 + 地图点选（混合）** | 「我希望先下拉选订单类型，像前往车站这一类的再到地图上面去点选」 |
| C-4 | **灰掉** | 「C4，做不到的按钮直接灰掉」 |
| C-5 | **逐个复核** | 「C5，按你推荐的来」 |
| C-6 | **A 先做完再验收** | 「我希望全做完再验收，所以按你推荐的来」 |

**附加硬要求（玩家原话）**：

1. 「无主排程的编辑窗口一定要最大限度向现有窗口靠拢。」
2. 「全部源码改完再全量。」⇒ 中途**不做任何构建**，所有源码改完后一次性全量构建（改 `src\*.h` 触发全量是必然，正好一次付清）。

### 12.1 C-3 的落地形态（混合方案，替代原先的 N1 纯下拉）

C-3 不是原来两个选项之一，而是：

1. **一级**：点「+」先选**订单类型**（前往车站 / 前往路点 / 前往车库 / 挂接 / 等待挂接 / 解挂 / 条件 / 槽 / 计数器 / 文本标签 / 自动分离…）——纯下拉，无车辆依赖。
2. **二级**：若该类型需要「目标」，进入**地图点选**（`VpSetPlaceMode` + `OnPlaceObject`），只接受与该类型匹配的地块：
   - 前往车站 / 条件(车站) / 班次途经站 → 只接受车站地块；
   - 前往路点 → 只接受路点地块；
   - 前往车库 / 挂接(车库) → 只接受车库地块；
   - 挂接(车站) → 只接受车站地块；
   - 等待挂接 / 解挂 / 计数器 / 文本标签 / 槽 → **无需目标**，一级选完直接插入。
3. 点选阶段的合法性判定**不依赖车辆**：公司与车型已由 `OrderHost`（C-2 恒为列车）给出，只需 `IsInfraUsageAllowed(vtype, company, tile_owner)`；从地块反查 `StationID`/`WaypointID`/`DepotID` 不需要车辆。

**与原来 N2 的区别**：N2 是要复用 `GetOrderCmdFromTile(v, tile)` 那套「点什么都行、由地块反推订单类型」的逻辑（依赖 `v` 判车型与可去性）；本方案**先由下拉锁定类型**，点选阶段只做「取目标 ID + 公司权限」两件事，因此完全不需要车。`GetOrderCmdFromTile` 一行都不用改。

### 12.2 落地顺序（按 C-6「全做完再验收」）

阶段 1 命令层（C-1=R2）：`src\order_cmd.cpp` 的 `CmdModifyOrder` 抽 `OrderHost{list, vtype, company, veh}`；新增 `CmdR3RModifyLooseOrder` 只做「构造 host(veh=nullptr) + 调共用实现」；`InsertLooseOrderCmdData` 旁的 loose 命令族补齐。

阶段 2 窗口对齐（硬要求 1）：无主编辑器不再自建控件，改成 `OrdersWindow` 的「无车模式」——沿用现有 widget 树（`_orders_train_desc`），仅把 4 个必须依赖车辆的控件（Skip / Refit / Stop sharing / Occupancy）置灰 + tooltip；`+` 走 12.1 的两级流程。

> 阶段 2 实际落地口径（修正，2026-11-12）：`OrdersWindow` 的 `OnPaint/OnClick/各 dropdown 构造` 通篇以 `this->vehicle` 为第一参数（车号、车型、refit 货物清单、共享排程判定都从它取），改成"无车模式"要在几十处插空分支，代价与风险都高于收益。故保留独立窗口 `R3RScheduleEditorWindow`，但**控件集合与判据向有主窗口逐条对齐**（见 §十三）。

阶段 3 `DrawOrderString` 的 10 处 `v==nullptr` 跳过项逐个复核（C-5）。

阶段 4 全部改完 → 一次性全量构建（`R3R_fullbuild.cmd`）→ 复测判据见 §九。

---

## 十三、参数行逻辑层落地（第 424 轮，`src\order_gui.cpp`）

### 13.1 平面结构
参数行由 `WID_R3RSE_SEL_PARAMS`（`NWidgetStacked`，`_nested_r3r_schedule_editor_widgets` 内）承载 7 个平面：

| plane | 订单类型 | 控件 |
|---|---|---|
| 0 | 无选中 / 无参数类型 | 空占位 |
| 1 | `OT_GOTO_STATION` | LOAD / UNLOAD / NON_STOP / R3R_YARD / SEPARATION |
| 2 | `OT_GOTO_DEPOT` | NON_STOP / DEPOT_ACTION |
| 3 | `OT_GOTO_WAYPOINT` | NON_STOP |
| 4 | `OT_GOTO_COUPLE` | COUPLE_SIDE / COUPLE_TEMP_GROUP |
| 5 | `OT_WAIT_COUPLE` | COUPLE_TEMP_GROUP |
| 6 | `OT_DECOUPLE` | DECOUPLE_BOUNDARY |

`UpdateParameterWidgets()` 在 `OnPaint()` 内调用：先 `SetDisplayedPlane(plane)`，再写 disabled/lowered 状态。

**硬约束（易崩）**：只能写"当前平面"的控件。`Window::SetWidgetLoweredState()` / `SetWidgetDisabledState()` 内部是 `GetWidget<T>(w)->...`，**不判空**；写成跨平面统一刷状态会在非当前平面拿到 nullptr 直接崩。构造期也不必预置其它平面（默认 plane 0）。

### 13.2 本轮对齐修正（5 处）
1. **no_stop 时置灰 LOAD/UNLOAD**：命令层 `order_cmd.cpp:2783`（`MOF_UNLOAD`）与 `:2807`（`MOF_LOAD`）对 `ONSF_NO_STOP_AT_DESTINATION_STATION` 一律 `CMD_ERROR`。UI 端在站台平面算出 `no_stop` 后 `SetWidgetDisabledState(WID_R3RSE_LOAD/UNLOAD, no_stop)`，与有主窗口同判据（其 `UpdateWidgetStates` 站台分支同样按此置灰 FULL_LOAD）。
2. **间隔/等待调度共用一个按钮**：两项共用一个下拉（`SEPARATION` = 间隔、`WAIT_DISPATCH` = 等待调度，二者互斥），故按下态改为 `HasR3RStationSeparation() || HasR3RWaitDispatch()`；旧写法只看前者，选"等待调度"时按钮不亮。
3. **`MOF_UNLOAD` 同值即报错**：`order_cmd.cpp:2787` 对"新值 == 旧值"直接 `return CMD_ERROR`（会弹 `STR_ERROR_CAN_T_MODIFY_THIS_ORDER`）。UI 端改为先取 `order->GetUnloadType()` 比较，**只有真的变了**才 Post。`MOF_LOAD` 侧同值返回成功（无操作），不需要守卫。
4. **互斥清理不留半态**：选间隔时先设 `MOF_R3R_STATION_SEPARATION=1`，再**回读** `GetSelectedOrder()` 确认该标记已落地，才清 `MOF_R3R_WAIT_DISPATCH`；反之亦然。旧写法只看旧状态就直接连发两条，第一条被命令层拒绝时会把另一个标记也清掉。
5. **解挂边界查询标题**：改用 `STR_ORDER_DECOUPLE_HEAD_VALUE_CAPTION` / `STR_ORDER_DECOUPLE_TAIL_VALUE_CAPTION`（`order_gui.cpp:2330-2331` 有主窗口用的就是这两个），原先误用 `STR_ORDER_DECOUPLE_QUERY_*`（那是下拉条目文案）。

### 13.3 下拉索引口径（易错，已逐项核对）
- `_order_full_load_dropdown`：`0 LoadIfPossible / 1 分隔线 / 2 FullLoadAll / 3 FullLoadAny / 4 NoLoading / 5 分隔线 / 6 按货物类型` ⇒ 无主侧隐藏掩码 **`0x62`**（隐藏 1/5/6，按货物类型需要车辆 cargo 选择器）。
- `_order_unload_dropdown`：`0 UnloadIfPossible / 1 Unload / 2 Transfer / 3 分隔线 / 4 NoUnloading / 5 按货物类型` ⇒ 隐藏掩码 **`0x28`**（隐藏 3/5）。
- 关键：`OrderLoadType` / `OrderUnloadType` 的**枚举值恰好等于列表下标**，所以 `OnDropdownSelect` 里可直接 `static_cast<OrderUnloadType>(index)`，无需查表；`NoLoad` 用 `to_underlying(OrderLoadType::NoLoad)` 提交。

### 13.4 命令口径
`ModifySelectedOrder(mof, data)` → `Command<Commands::ModifyR3RLooseOrder>::Post(STR_ERROR_CAN_T_MODIFY_THIS_ORDER, schedule.base(), selected, mof, data, {}, {})`（后两个参数是 `CargoType` / `std::string`，默认空）。
命令层 `CmdR3RModifyLooseOrder` 与有主 `CmdModifyOrder` **共用同一张 `switch (mof)`**（同一套白名单 + 同一段执行），所以本窗口只需要保证"不提交非法组合"，不需要另建参数语义。

### 13.5 刻意偏离（1 处）
FULL_LOAD 按钮按下态：本窗口用 `IsFullLoadOrderLoadType()`（`FullLoad` 与 `FullLoadAny` 都亮），有主窗口只在 `FullLoadAny` 时亮（≈`order_gui.cpp:2953`）。理由＝本窗口该按钮文案是 widget 树里的静态串，无法像有主窗口那样靠文案区分两档，故用按下态表达"整列装载已启用"。

### 13.6 未对齐项（后续轮次）
- 停车位置 `MOF_STOP_LOCATION`（`order_type.h` 已有该 flag，`:2705` 一带其在白名单中）
- 路点逆向 `MOF_WAYPOINT_FLAGS`、条件/槽/计数器/标签参数
- 4 个必须依赖车辆、本窗口未布置的控件：Skip / Refit / Stop sharing / Occupancy

### 13.7 疑点登记
- **D-1（疑点）**：`MOF_R3R_STATION_SEPARATION` 与 unbunching 冲突时命令层可能拒绝，此时 UI 只弹命令错误、按钮保持不亮，用户无法从界面看出"为什么设不上"。未实机验证拒绝条件。
- **D-2（疑点）**：`MOF_R3R_STATION_SEPARATION` 执行段会遍历同 `OrderList` 的车辆清 `VehicleFlag::SeparationActive`；无主表没有车辆 ⇒ 该循环为空集，按代码应是无害 no-op，但"空集路径不报错"未实机确认。
- **D-3（已解释）**：`UpdateParameterWidgets()` 在 `OnPaint()` 里调 `SetDisplayedPlane()` 会重建 widget lookup（`widget.cpp:1396-1403`），属绘制期内部状态，不影响命令/网络；布局是否闪烁需实机目视。

### 13.8 构建自证
见 §十四。

## 十四、构建自证（2026-11-12）

### 14.1 首次构建失败与两处编译期错误（必须记录）

首次 `R3R_fullbuild.cmd inc detach`（20:11:40 结束）判 `EXIT_CODE=1`，日志 `build\R3R_incbuild.log` 命中两处问题，均为参数行逻辑层落地时写错名字/写错接收者，**不是设计问题**：

1. `src\order_cmd.cpp(2698): error C2039: "GetOrder": 不是 "OrderList" 的成员`（备注 `order_base.h(1638)`）。`R3ROrderHost` 抽出共用函数体后，把 `Vehicle::GetOrder()` 的调用改成 `host.list->GetOrder()`，而 `OrderList` 的访问器叫 **`GetOrderAt()`**（`order_base.h:1732`，const/非 const 两个重载）。修法：`host.list->GetOrderAt(sel_ord)`。
   - 教训：`Vehicle::GetOrder()` 与 `OrderList::GetOrderAt()` 名字**不同**，host 化改写时不能机械替换接收者。
2. `src\order_gui.cpp(175): warning C4717: "CargoTypeOrdersWindow::GetOrder": 如递归所有控件路径，函数将导致运行时堆栈溢出`。该 helper 原意是"本窗口永远绑定车辆，直接转发车辆"，实际写成 `return this->GetOrder(index);` —— **自递归**（不是编译错误，但一按即栈溢出崩溃）。修法：`return this->vehicle->GetOrder(index);`。
   - 该行是本轮 stage 1 引入的，因首次构建在此前已中断、从未成功链接，故直到本轮才暴露。

### 14.2 最终构建自证（内测树 `build\`）

- 入口：`R3R_fullbuild.cmd inc detach`（复用既有脚本，未新建任何 `.cmd`）；`tasklist` 确认 `openttd.exe` 未运行。
- 护栏：`build\R3R_incbuild.guard.log` = `GUARD: incremental is safe (no header/lang file is newer than the newest object)`。
- 首轮失败后重跑（第二次）从 `[1/115]` 推进到收尾：`[115/115] Linking CXX executable openttd.exe`，日志尾 `==== R3R build ended 2026/11/12 周四 20:28:02.98  MODE=inc  EXIT_CODE=0`。
- `build\R3R_incbuild.done` = `EXIT_CODE=0`。
- 日志中 `error C` / `FAILED:` / `build stopped` 匹配计数 = **0**。
- 时间戳链（源码 → obj → exe，全部合规）：
  - `src\order_cmd.cpp` 20:14:14 → `order_cmd.cpp.obj` 20:16:01
  - `src\order_gui.cpp` 20:14:20 → `order_gui.cpp.obj` 20:16:01
  - `build\openttd.exe` **2026-11-12 20:26:46（52 842 496 B）**
- `read_lints`：`src\order_cmd.cpp`、`src\order_gui.cpp` 均 0 条。
- 本轮只改 `.cpp`（未碰 `src\*.h`、未改 `src\lang\*.txt`），故无存档格式/版本变更，也无语言串变更。

### 14.3 状态

- KI-609（参数行逻辑层与命令层语义不一致）：**已修 + 已编译（20:26:46 版）**，实机复测待做。
- KI-610（未布置参数清单）：仍为**待实现**，本轮未动。
- 发布树 `build-release` 未重编（按口径只在用户明确要求时编）。

## 十五、REG-424-A：玩家报「无主排程编辑器 GUI 还是老样子」

**玩家原话（2026-11-12）**：「什么东西，为什么我看到的无主排程编辑的GUI还是老样子」

**先落地的事实（已核，行号为当前工作树）**：

1. 参数行控件**确实**在 widget 树里：`src\order_gui.cpp:5499` 的 `NWidget(NWID_SELECTION, ..., WID_R3RSE_SEL_PARAMS)`，plane 0 = 空占位 panel（`:5500`），plane 1..6 = 站台/车库/路点/挂接/等待挂接/解挂（`:5502`–`:5541`）。
2. `UpdateParameterWidgets()` 在 `OnPaint()` 里每帧调用（`:5620` 定义、`:5736` 调用），选中订单后按 `order->GetType()` 切 plane。
3. 入口唯一：`r3r_schedule_gui.cpp:411`，从排程列表窗口双击/回车一条**无主**排程时调 `ShowR3RScheduleEditor()`（有车主的走 `ShowOrdersWindow`）。
4. 两棵树 exe 时间戳：`build\openttd.exe` = **2026-11-12 20:26:46（52 842 496 B）**（含本轮改动，`order_gui.cpp` 20:14:20 → obj 20:16:01 → exe）；`build-release\openttd.exe` = **2026-11-09 01:09:10（23 588 352 B）**（**不含**本轮改动）。

**猜测根因（待验证，按优先级）**：

- **G-1（最可能）：玩家运行的不是 `build\openttd.exe`（20:26:46）**。若双击的是 `build-release\openttd.exe`（11-09）或旧版拷贝/快捷方式，第 424 轮的任何改动都看不到，表现就是"完全没变"。
- **G-2：窗口认错了**。R3R 有两个相关窗口：`WindowClass::R3RSchedule`（排程列表，`r3r_schedule_gui.cpp`）与 `WindowClass::R3RScheduleEditor`（本编辑器，`order_gui.cpp`）。第 424 轮**只改后者**；若玩家打开的是排程列表窗口，则确实"老样子"。
- **G-3：已进编辑器但没选中订单**。plane 0 是 12px 高的空 panel，此时窗口外观与改动前逐像素相同（这是设计如此，不是缺陷）。选中一条命令后参数行才出现。
- **G-4：参数行出现但玩家期望的是"未布置控件"**（KI-610 那批：Skip/Refit/Stop sharing/Occupancy、条件/槽/计数器/标签、`MOF_STOP_LOCATION`/`MOF_WAYPOINT_FLAGS`）。本轮只做了 stage A 的 5 处对齐修正，**没有新增控件**，所以"看到的东西一样多"是符合本轮范围的。

**待玩家确认（一次性问清，避免反复）**：

- R-1 你用哪个 exe 启动？（`build\openttd.exe` 20:26 还是 `build-release\openttd.exe` 11-09 还是别的路径）
- R-2 窗口标题栏文字是「排程编辑」类还是「排程列表」类？是否有 5 个按钮（新建/删除/复制/上移/下移）？
- R-3 是否有选中某条命令？选中后窗口底部是否出现一行下拉框？

**判据**：用 20:26:46 的 `build\openttd.exe`，在有 5 个按钮的编辑器窗口里选中一条「前往站台」命令 ⇒ 底部应出现一行 5 个下拉框（装满/卸货/非停/站场/间隔）。

## 十六、REG-424-B：玩家拍板「照搬普通订单窗口 + 不能实现的置灰」（第 424 轮第二轮）

**玩家原话（2026-11-12）**：「我想要的就是无主排程编辑器向普通排程编辑窗口对齐啊，但是现在什么也没有对齐。你就不能先把普通排程编辑窗口搬过来，然后再直接把不能实现的功能置灰无法点击吗」

**口径（决定性，按此执行，不再走"最小 plane 行"路线）**：

1. **不再**用当前 `_nested_r3r_schedule_editor_widgets`（只有 列表+1 行参数+1 行操作按钮）。
2. **直接搬** `OrdersWindow` 的完整 widget 树 `_nested_orders_train_widgets`（`src\order_gui.cpp:5066-5251`，187 行，含 6 行参数区、条件/槽/计数器/标签/出发经由、管理菜单、Skip/Stop sharing/GoTo）。它是 `static constexpr std::initializer_list<NWidgetPart>`，类型与 `WindowDesc` 要求的完全一致 ⇒ 可以让 `_r3r_schedule_editor_desc` 直接引用**同一个树**，不复制代码。
3. 搬过来后**能实现的接上**，**依赖车辆的实现不了的一律置灰**（`SetWidgetDisabledState`），而不是隐藏。置灰候选：`WID_O_TIMETABLE_VIEW`、`WID_O_OCCUPANCY_TOGGLE`、`WID_O_REFIT`、`WID_O_REVERSE`、`WID_O_REFIT_DROPDOWN`、`WID_O_SKIP`、`WID_O_STOP_SHARING`、`WID_O_SHARED_ORDER_LIST`、`WID_O_ADD_VEH_GROUP`，以及条件下拉里依赖"本车时间表/计数器运行值"的判定项。
4. 需要重写的是**窗口类的 handler**：`R3RScheduleEditorWindow` 现有全部 `WID_R3RSE_*` 引用必须映射到 `WID_O_*`（ID 是窗口内局部的，跨窗口复用合法）：
   - `WID_R3RSE_CAPTION` → `WID_O_CAPTION`
   - `WID_R3RSE_ORDER_LIST` → `WID_O_ORDER_LIST`
   - `WID_R3RSE_SCROLLBAR` → `WID_O_SCROLLBAR`
   - `WID_R3RSE_DELETE` → `WID_O_DELETE`
   - `WID_R3RSE_ADD` → `WID_O_GOTO` 下拉里的"插入/前往"项（OrdersWindow 同款菜单）
   - `WID_R3RSE_DUPLICATE` / `MOVE_UP` / `MOVE_DOWN` → `WID_O_MGMT_BTN` 下拉菜单（OrdersWindow 同款 Manage 菜单）
   - 现参数行 5 件套 → 树里本来就有：`WID_O_FULL_LOAD` / `WID_O_UNLOAD` / `WID_O_NON_STOP` / `WID_O_R3R_YARD` / `WID_O_R3R_SEPARATION`，另加 `WID_O_DEPOT_ACTION` / `WID_O_COUPLE_SIDE` / `WID_O_COUPLE_TEMP_GROUP`
5. 判据（玩家验收）：打开无主排程编辑器，**控件数量与排布与普通列车订单窗口肉眼一致**，其中依赖车辆的那批是灰的、点不动；能用的（改订单类型、装满/卸货/非停、站场/间隔、车库动作、挂接侧/临时分组、条件/槽/计数器/标签、删除/复制/上移/下移、插入）都能用。

**风险与做法**：`R3RScheduleEditorWindow` 现在约 510 行（`:5568-6081`），handler 只覆盖 6 个控件；搬树后必须**一次性**把 `OnClick` / `UpdateButtonStates`（原 `DrawWidget` 里的按钮状态）/ `OnPaint` 的平面切换全部补齐，否则 `GetWidget<WID_O_*>()` 找不到控件会在运行期断言崩溃。因此本轮必须"改完即编"，不可中途只改树不补 handler。


