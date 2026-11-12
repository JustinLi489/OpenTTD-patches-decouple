# 第 423 轮：economy.cpp:1650 断言崩溃 + 无主排程编辑器为何不能对齐有主编辑器

日期：2026-11-12　状态：取证中（第一条根因待做实，第二条为口径/需求问题）

---

## 一、KI-606：`economy.cpp:1650` 断言崩溃

### 1.1 现场落点（已读代码确认）

```1650:1650:src/economy.cpp
	assert(front_v->cargo_payment == nullptr);
```

所在函数：`void PrepareUnload(Vehicle *front_v)`（`src\economy.cpp:1639`，注释见 `:1636-1638`）。

上下文：

```1639:1656:src/economy.cpp
void PrepareUnload(Vehicle *front_v)
{
	Station *curr_station = Station::Get(front_v->last_station_visited);
	curr_station->loading_vehicles.push_back(front_v);

	/* At this moment loading cannot be finished */
	front_v->vehicle_flags.Reset(VehicleFlag::LoadingFinished);

	/* Start unloading at the first possible moment */
	front_v->load_unload_ticks = 1;

	assert(front_v->cargo_payment == nullptr);
	/* One CargoPayment per vehicle and the vehicle limit equals the
	 * limit in number of CargoPayments. Can't go wrong. */
	static_assert(CargoPaymentPool::MAX_SIZE == VehiclePool::MAX_SIZE);
	assert(CargoPayment::CanAllocateItem());
	front_v->cargo_payment = CargoPayment::Create(front_v);
```

### 1.2 断言语义（不变量）

`Vehicle::cargo_payment` 的原生生命周期是**严格成对**的：

| 阶段 | 动作 |
|---|---|
| 进站开始装卸 | `PrepareUnload()` 断言「进来时必为空」并 `CargoPayment::Create(front_v)` |
| 离站 | `Vehicle::LeaveStation()` 里 `Finalize()` 并把 `cargo_payment` 置 `nullptr` |

⇒ 断言触发 = **对同一辆「当前被当作 front 的车」连续调用了两次 `PrepareUnload`，而两次之间没有任何一次成功的 `LeaveStation` 收尾**。

即不变量被破坏的实质是「`cargo_payment` 的持有者（谁算 front）在装卸途中被换掉了」：`cargo_payment` 挂在具体某个 `Vehicle*` 上，而 `front_v` 是**调用点按当时的链头现算出来的**。只要链头身份在「已 PrepareUnload 但还没 LeaveStation」的窗口内发生变化，就会出现「A 车挂着 payment、B 车被当成 front 又进 PrepareUnload」的空窗——如果 B 恰好是之前挂过 payment 却没被收尾的那辆，就命中断言；反过来更常见的是 payment 泄漏到**已不是 front** 的旧车上，永不 Finalize（钱与货流统计一起错）。

### 1.3 猜测根因（待验证，按可疑度排序）

玩家的猜测是「调度难以计算客货流」。目前看更可能是**同一件事的表象**：R3R 的链编辑会改写「谁是链头 / 谁持有 payment」，而客货流计算按链头取 payment。登记如下。

| # | 猜测 | 支撑 | 待验证判据 |
|---|---|---|---|
| A-1 | **链头身份迁移没带 `cargo_payment`**：耦合时 `Couple()` / `R3RRelocateFrontIdentity()` 把 front 从 v 换到新链头，payment 留在旧对象上，新链头进站时又 Create ⇒ 断言（或旧对象 payment 悬挂泄漏） | 原生不变量「一车一 payment、只有 front 用」与 R3R 允许 front 变更直接冲突 | 崩前日志是否恰有 `COUPLE-OK`/`DECOUPLE-DONE`/车库拖动，且崩在随后第一次进站 |
| A-2 | **解挂后两半都自认 front**：`DecoupleTrain` 把链切成 v/u，两半各自 `IsPrimaryVehicle()`；若 v 侧在装卸途中被切，v 仍持 payment，u 侧进站不受影响，但 v 再进站时命中 | 解挂提交点确实不调 `LeaveStation` | `DECOUPLE-DONE` 之后 v 侧是否存在 `PrepareUnload` 两次而中间无 `LeaveStation` |
| A-3 | **单段化/段化（settle）把 `loading_vehicles` 里的 front 变成非 front**：`PrepareUnload` 已把 `front_v` 压进 `station->loading_vehicles`，之后链头改变 ⇒ 摘除方（`Vehicle::LeaveStation`/`PreDestructor`）按新的 `last_station_visited`/新 front 反查，条目残留（与 KI-95/SL-STNN-PURGE 同族） | KI-95 已证同类残留 | 崩前是否有 `SL-STNN-PURGE` |
| A-4 | **纯原生缺陷被 R3R 放大**：`PrepareUnload` 只在 `TrainEnterStation` 调用一次，正常不会重入；若某路径（如自动驾驶/强制调头重进站）重复进入，则与 R3R 无关 | 需读调用点 | `PrepareUnload` 全部调用点清单 + 是否存在重入路径 |

**排除项初判**：与「调度池派单（`TrainDispatchHandler`）」本身无直接关系——派单不写 `cargo_payment`，它只改订单与停驶位。玩家看到的「客货流算不出来」应是 payment 泄漏/错挂的**后果**，不是原因。待日志验证。

### 1.4 需要的证据（请玩家提供）

1. `crash-*.log`（默认在 `%USERPROFILE%\Documents\OpenTTD\crash\`）——要调用栈，确认崩在 `PrepareUnload` 且能看到它上面的调用者（`TrainEnterStation` / `VehicleEnterStation` / 哪个 vehicle id）。
2. 崩溃前最后 200 行 `build\R3R_debug.log`（或整份），重点看：崩溃前 1 分钟内是否有 `COUPLE-OK` / `DECOUPLE-DONE` / `RESPACE-AFTER-EDIT` / `SETTLE` 类行。
3. 崩的时候那列车在做什么（刚进站？刚挂上？刚解挂？），以及是否用了「调度池 / 等待分配」。

### 1.5 复测判据（修法落地后）

1. 复现场景不再命中 `economy.cpp:1650`。
2. 挂/解/车库编辑之后第一次进站，`cargo_payment` 的 Create/Finalize 次数配对（新增探针计数）。
3. 负判据：不做任何链编辑的普通进出站，payment 行为与改前逐字一致。
4. 钱数一致：同一趟运输的收入与改前相同（不出现重复结算或漏结算）。

---

## 二、KI-607（口径问题）：无主排程编辑器为何不能对齐有主编辑器

玩家要求：**除「依附者」这一属性外，其余尽量照抄有主编辑器的能力**。

这条不是缺陷，是「两种编辑器能不能同构」的设计问题。待取证：`src\r3r_schedule_gui.cpp` 现有按钮/动作清单 vs `order_gui.cpp` 有主路径的动作清单，逐项判定「是否需要车辆」——只有真正需要车（读车辆状态、发车辆命令、按车算锚点）的项才允许不对齐，其余应补齐。取证结果填在 §2.2。

### 2.1 判定口径（先立规矩，再逐项填表）

一项操作在无主编辑器里「可以照抄」的充要条件：

- 它只读写 `OrderList` 本体（`Order` 数组、索引、标记位）；
- 它不需要 `Vehicle*` 作为**锚点**（比如「从车辆当前位置/朝向推断插入点」「按车辆载货算可行性」）；
- 它不通过 `DoCommandP` 走车辆命令族（`CmdModifyOrder` 之外的 `CmdSkipToOrder`、`CmdReverseTrainDirection` 等）。

不满足的项，要么改为「基于订单本体的等价操作」，要么明确降级（灰掉 + tooltip 说明），**不能假装可用后静默失败**。

### 2.2 差异清单（已取证）

**无主编辑器现状**（`src\order_gui.cpp:5445-5538`，`R3RScheduleEditorWindow`）：

- 按钮只有 5 个：`WID_R3RSE_ADD` / `DELETE` / `DUPLICATE` / `MOVE_UP` / `MOVE_DOWN`。
- `ShowAddOrderDropDown()` 只遍历**本公司、且 `facilities` 非空的车站**，drop-down 项 `index` 直接当 `StationID` 回传 ⇒ **路点、车库、挂接/等待挂接/解挂命令全都加不进去**。
- 类注释（`:5476-5486`）自陈裁剪标准是「按钮是否发车辆命令」：*"every one of its buttons posts a vehicle command … anything which needs a real vehicle (refitting, skip to order, occupancy of the current order) is simply not offered."*

**判定：这个裁剪过宽。** 类别判定表：

| 类别 | 操作 | 无主侧能否照抄 | 依据 |
|---|---|---|---|
| 目的地类型 | 车站 / 路点 / 车库 | **能，且应补** | 只写 `Order::GetDestination()` 与 `OrderType`；路点/车库在 `Station::Iterate` 之外另有枚举，现有实现根本没遍历 |
| 交接命令 | 挂接 `GOTO_COUPLE` / 等待挂接 `WAIT_COUPLE` / 解挂 `DECOUPLE` | **能，且应补** | 都是纯订单类型，参数是边界/侧/模式，全在 `Order` 里 |
| 订单参数 | 装载方式（全载/不载/装满…）、解挂边界与模式、挂接侧、逆向、`ModifyOrderFlags` 类 | **能，且应补** | 原生走的就是 `CmdModifyOrder`，其判据只碰 `OrderList`（`order_cmd.cpp` 白名单） |
| 列表操作 | 增 / 删 / 复制 / 上移 / 下移 | 已有 | — |
| 锚点类 | 「按车辆当前位置/朝向决定插入点」 | **需改造** | 无车可依，但等价物是「选中项之后 / 末尾」，已实现（`GetInsertPosition()`），只是没有有主窗口那套按车推断的入口 |
| 预览类 | 航程/装载能力/耗时预览 | **需改造** | 依赖车型 `Engine` 容量。窗口已有 `vtype`（`:5490`，当前恒 `Train`），可按 vtype 取默认车型算，属可选增强 |
| 真车辆依赖 | Skip to order、Refit（改装）、当前订单占用/进度显示 | **必须降级** | 要改车辆索引或车辆货舱；`CmdSkipToOrder`/`CmdRefitVehicle` 都以 `VehicleID` 为键 |
| 采样类 | 自动填充命令（`AUTO-ORD`，按列车脚下路网采样） | **必须降级** | 需要真实列车位置与走行路径 |

⇒ 回答玩家的「为什么不能对齐」：**不是技术上不能，是第 421 轮为尽快止崩做了最小实现**，且用了「只要不是纯订单本体就一律不给」的一刀切标准。按上表，除最后三类，其余全部可以照抄；其中「路点/车库/交接命令/订单参数」是当前最明显的功能缺口。

---

## 三、本轮待办

- [ ] 取证 `cargo_payment` 的赋值/清除点与 `PrepareUnload` 调用点，做实 KI-606 根因。
- [ ] 取证 `r3r_schedule_gui.cpp` 的按钮/动作清单，产出 §2.2 差异表并按「可照抄 / 需改造 / 必须降级」分类。
- [ ] 按分类落地补齐无主编辑器（改 `src\r3r_schedule_gui.cpp` / `src\order_gui.cpp`，不碰 `.h` 时走增量）。
- [ ] 构建 + 回填 KI 状态。

---

## 四、第 423 轮崩溃日志取证（2026-11-12，KI-606 根因做实中）

### 4.1 日志来源与版本核对

| 项 | 读数 |
|---|---|
| 文件 | `%USERPROFILE%\Documents\OpenTTD\crash-20261112T095420Z.log`（14 176 B） |
| 崩溃时刻 | 2026-11-12 09:54:20 UTC（游戏内 1920-02-10，读档于 09:52:01，**788 状态 tick 后**） |
| exe | `r3r-stable-2026-11-08-m (2)`，**Build date: Nov 11 2026 20:21:20** |
| 判定 | **这不是 11-12 17:48 那个 exe**（后者含 KI-604/KI-605 修复）。本崩溃来自 11-11 20:21 的旧二进制 ⇒ 与第 422 轮两项修复无关，是**独立新缺陷** |

### 4.2 栈（决定性）

```
[03] assert_str_error            src\error.cpp:31
[05] PrepareUnload               src\economy.cpp:1650   ← assert(front_v->cargo_payment == nullptr)
[06] Vehicle::BeginLoading       src\vehicle.cpp:3660
[07] TrainEnterStation           src\train_cmd.cpp:19898
[08] TrainController             src\train_cmd.cpp:21125
[09] TrainLocoHandler            src\train_cmd.cpp:23975
[10] Train::Tick                 src\train_cmd.cpp:24106
```

与第 423 轮 §1.2 的推断一致：`PrepareUnload` 被 `Vehicle::BeginLoading` 调起（**进站路径**），不是别的调用者。

### 4.3 崩溃上下文（Who is front_v）

```
0: TrainController: veh: 0: (Train 1, c:0, st:FE, vs:Ds, vf:Tpa, vcf:zd, gvf:, tf:HS,
   trk: 0x01, [tile: 99B0 (432 x 76), type: 50 (Station), height: 01,
   data: 60 0001 C0 00 00 86 00 0000], CP)
```

- `Train 1`、`c:0` ⇒ **单车（组内 1 节）**，不是长链。
- `st:FE` = FrontEngine；`vs:Ds` = 车辆状态 **Ds**（停住/停止位）；`vf:Tpa`；`gvf:` 空 ⇒ **没有任何 VehicleRailFlag**（无 ★ 段头、无假引擎、无 DB）。
- 位置 `432,76` 是**车站格**（type 50 Station）。
- 末位 `CP` ⇒ 该车挂着 `cargo_payment`。

⇒ 现场是一列**单车、无段标记、停驶状态、在站台格上仍持有 payment** 的车，进站时再 `Create` 一次 ⇒ 断言。

### 4.4 命令日志（关键旁证）

```
3 | 1920-01-29, 33 | 452 x 76 | cmd: 09E CmdStartStopVehicle | 12, false
4 | 1920-01-29, 33 | 452 x 76 | cmd: 09E CmdStartStopVehicle |  0, false
5 | 1920-01-29, 33 |           | cmd: 055 CmdPause         |  1, false
```

玩家在 `452,76` 对 veh 12 与 veh 0 各下发过一次 **StartStop（启停）**，随后暂停/继续，788 tick 后崩。

⇒ **新猜测根因（登记为 KI-608，待验证）**：`CmdStartStopVehicle` 让列车在**装卸途中**被停住/重新启动，`Vehicle::BeginLoading` 的"已经开始装卸就不再重入"的保护依赖 `cargo_payment == nullptr` 这一条，而手动启停路径既不清 `cargo_payment`、也可能没有把车从 `station->loading_vehicles` 摘掉，于是车头再次被 `TrainEnterStation` 判定为"进站"⇒ 二次 `PrepareUnload` ⇒ 断言。

**待验证判据**：①`CmdStartStopVehicle` 的清账动作（是否只 `load_unload_ticks = 0` 而不 `LeaveStation`）；②`Vehicle::BeginLoading`（`vehicle.cpp:3660` 上方）在什么条件下会对同一车二次调用 `PrepareUnload`；③`TrainEnterStation` 的"已在该站"守卫（`last_station_visited` 比较）为何没挡住。

### 4.5 版本提醒

请玩家确认复测用的 exe：**必须用 `build\openttd.exe`（11-12 17:48，含 KI-604/KI-605 修复）**，而崩溃日志里这个 `r3r-stable-2026-11-08-m (2)` / Build 11-11 20:21 是旧二进制。若玩家用的是稳定版备份 exe，本轮修复自然不在里面。

---

## 五、第 423 轮：根因做实 + 两处修法（2026-11-12 18:0x）

### 5.1 结论先行

`economy.cpp:1650` 的 `assert(front_v->cargo_payment == nullptr)` 被踩，**不是**进站链路自己坏了，而是「有人把 `OT_LOADING` 的订单 `Free()` 掉了却没办离站手续」——`CargoPayment` 因此悬挂，下一次进站 `BeginLoading() → PrepareUnload()` 撞断言。

本轮做两件事：

1. **治根**（KI-608）：`TrainDispatchHandler()` 在把列车绑进调度池之前补一次装卸结算，堵掉这条新开出来的泄漏口（第 422 轮 KI-605 放宽判据后第一次可达）。
2. **兜底**（KI-606）：`TrainEnterStation()` 在 `consist->BeginLoading()` 之前清掉仍挂着的陈旧 `cargo_payment`（只释放账，不改订单）。

### 5.2 治根：为什么调度池绑定会漏账（KI-608）

第 422 轮 KI-605 只改了一处判据（`src\train_cmd.cpp` 的 `TrainDispatchHandler()`）：

```cpp
/* 旧 */ if (!v->current_order.IsType(OT_GOTO_STATION)) return;
/* 新 */ 先看 current_order；不是到站命令就回退到 GetEffectiveOrders()[cur_real_order_index]
```

理由是：停住的列车 `current_order` 常常已经是 `OT_NOTHING`（KI-605 现场 `order=0` / `real=1`），只看 `current_order` 钩子永远不成立。

**副作用**：装卸中的列车此刻 `current_order` 正是 `OT_LOADING`，而订单表里 `cur_real_order_index` 那一位仍然是为「等待分配」打过标记的 `OT_GOTO_STATION` ⇒ 新判据对「正在装卸」的列车**成立**。于是钩子继续往下走：

```cpp
r3r_begin_borrow(...)
v->cur_real_order_index = 0;      // 借来的池日程
...
v->current_order.Free();          // ← 把 OT_LOADING 抹掉，不调 LeaveStation()
```

`Vehicle::LeaveStation()`（`src\vehicle.cpp`）才是 `delete cargo_payment` + 从 `station->loading_vehicles` 摘除 + `HideFillingPercent()` 的唯一出口。`current_order.Free()` 绕过它 ⇒ payment 与站台登记双双悬挂、进度条残留；列车随后进任何站，`PrepareUnload()` 的第一句断言就炸。

与崩溃日志的吻合点：`1920-01-29` 玩家在 `452,76` 对 veh 12 / veh 0 各下发一次 `CmdStartStopVehicle`（把站内列车停住/放行），随后 `CmdPause`；788 tick 后在同一站 `432,76` 进站崩溃。停住 → 调度钩子接管 → 订单被换表 → 泄漏。

**修法**（`src\train_cmd.cpp`，`TrainDispatchHandler()` 内 `R3RBeginBorrow(...)` 之前）：

```cpp
R3RSettleLoadingBeforeChainEdit(v, "dispatch-bind");
```

走的正是既有的「链编辑前结算」助手（`:9213`，正常路径上 `cargo_payment == nullptr` 时零动作，装卸中则办完整离站手续）。因为 `TrainDispatchHandler()` 定义在 `:7726`、助手定义在 `:9213`，另在文件前向声明区（`:140`）补了一行 `static` 前向声明。

顺序上的选择：结算放在 `cur_real_order_index = 0` 之前。`LeaveStation()` 会调 `UpdateVehicleTimetable()`，可能挪动 `cur_timetable_order_index`；放在绑定前、索引归零后，那几个索引仍按调度池日程写入，语义不受影响。

### 5.3 兜底：进站前清陈旧账（KI-606）

`src\train_cmd.cpp` 新增 `static void R3RSettleStaleCargoPayment(Train *consist, const char *site)`（`:19829`），在 `TrainEnterStation()` 的 `consist->BeginLoading();` 之前调用（`"enter-station"`）。

它与链编辑结算助手的**关键差异**：只 `delete cargo_payment` + 摘 `loading_vehicles` + `HideFillingPercent` + 复位 `LoadingFinished` / `CargoUnloading` / `load_unload_ticks`，**绝不改订单**。原因：调用点紧接着就要 `BeginLoading()`，订单必须仍是 `OT_GOTO_STATION` 且目的地=本站；若像链编辑那样走 `LeaveStation()`，订单会被写成 `OT_LEAVESTATION`，本次到站就退化成「意外停靠」（`BeginLoading()` 的 else 分支只插隐式订单），运输行为被悄悄改掉。

健康到站时 `cargo_payment` 恒为空 ⇒ 该函数在正常路径上是零动作，不影响收入与装卸。

### 5.4 排除的猜测

- **`CmdStartStopVehicle` 不是直接原因**：该命令只翻 `VehState::Stopped`，不碰 `cargo_payment` / `loading_vehicles`；装卸中的列车被停住本身是合法状态（上游允许，恢复后 `LeaveStation()` 正常收账）。
- **`TrainEnterStation` 的「已在该站」守卫不是漏洞**：它靠 `current_order.GetDestination() == station` 与 `last_station_visited` 判重；本次进站是**真正的新到站**，守卫没有失职——问题在于上一次离站从来没发生过。

### 5.5 构建自证（第 423 轮）

复用既有入口 `R3R_fullbuild.cmd inc detach`（未新建任何 `.cmd`），只改 `src\train_cmd.cpp`（未碰 `src\*.h`）⇒ 增量合法，日志 `[1/5]` 版本生成 → `[2/3] train_cmd.cpp.obj` → `[3/3] Linking CXX executable openttd.exe`。

| 项 | 读数 |
|---|---|
| `build\R3R_incbuild.done` | `EXIT_CODE=0` |
| 日志末行 | `[3/3] Linking CXX executable openttd.exe` |
| 错误计数（`error C` / `FAILED:` / `ninja: build stopped`） | 0 |
| `src\train_cmd.cpp` | 2026-11-12 18:10:26，1 336 617 B |
| `build\CMakeFiles\openttd_lib.dir\src\train_cmd.cpp.obj` | 18:11:58（晚于源码） |
| `build\openttd.exe` | 2026-11-12 18:13:38，52 807 680 B（晚于 obj） |
| `read_lints` | 0 条 |

发布树 `build-release\` 未编译（按口径：只在玩家明确要求时编）。**复测必须用这个 18:13 的 `build\openttd.exe`**；崩溃日志里的 `r3r-stable-2026-11-08-m (2)` / Build 11-11 20:21 是旧二进制，不含 KI-604/605，更不含本轮修复。
