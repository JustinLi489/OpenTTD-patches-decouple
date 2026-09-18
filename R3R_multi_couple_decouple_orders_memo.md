# R3R 多次连挂 / 解挂 · 调度(orders)继承机制调研备忘

- 日期：2026-09-11
- 分支：`feature/decouple`
- 目的：为后续"多次连挂/解挂"功能开发，先摸清 R3R 现有的**调度继承方式**（谁持排程、排程在挂/解中如何流转、现有局限）。
- 本文所有行号基于调研时的 `src/` 工作树。

---

## 0. 结论速览

1. 排程**跟着"列车"走，不跟着"段"走**。段只是物理边界 + 身份标记；`GOTO_COUPLE` / `WAIT_COUPLE` / `DECOUPLE` 是**列车排程里的交接点**。
2. 排程交接只有**一个入口**：`Couple()` 里的 `orders` 指针交换；还原只有**一个出口**：`DecoupleTrain()` 里的反向交换。
3. 承载"机车自己那份排程"的容器是**单层备份槽** `Vehicle::orders_backup`（`vehicle_base.h:375-379`），
   - **无栈**：连续两次交接会覆盖 → 嵌套连挂丢排程；
   - **`NOSAVE`**：连挂状态下存档/读档 → 备份丢失。
4. 上游 JGRPP 遗留的"解挂排程策略"字段（`ODOF_*`、`GetNumCouple`）**全部未接线**，R3R 用硬编码规则替代。
5. `orders` 交接是**裸指针直搬**，不调用 `DeleteVehicleOrders()`：
   - 假设"排程独有、不与他人共享"（代码注释已声明该前提）；
   - 一旦排程是**共享订单链**（Ctrl+共享），裸搬会破坏共享链（`num_vehicles` 计数泄漏、`first_shared` 悬空）。

---

## 1. 身份模型：谁能持排程

| 身份 | 判定 | 能否持 orders |
|---|---|---|
| 机车列车 | 真引擎在链头、`IsFrontEngine()` | ✅ |
| 段（segment） | 耦合时在段头打 `VehicleRailFlag::SegmentFront`（★） | ✅（段头可能是真引擎或假引擎） |
| car-only formation（零动力假引擎车组） | `R3RIsCarOnlyFormation()`：engine 位 + `railveh_type` 仍是 `Wagon` | ✅ |
| 裸 free wagon 链 | 无引擎 | ❌ |

判据是 `TrainHasEngine()`——**纯车厢链不能持排程，因此永远不被当作可解挂的段**，必须先 `MakeSegment`：

```cpp
// src/train_cmd.cpp:3662-3675
/**
 * Whether the chain contains an engine, i.e. is a powered segment that can
 * hold its own schedule. Unpowered groups (pure wagon chains) cannot carry
 * orders, so they are never treated as decouplable segments.
 */
static bool TrainHasEngine(const Train *v)
```

> 推论：解挂后若解出的部分原本无引擎，会被 `R3RCreateCarOnlyFormation(u)` 升级为**假引擎前端**并**新分配车号**，从而获得"独立持排程"的资格（`train_cmd.cpp:3897-3908`）。

---

## 2. 排程继承的唯一入口：`Couple()` 的 hand-over

**进入条件**：`u->orders != nullptr`（被挂的等待方持有排程）。

```cpp
// src/train_cmd.cpp:4925-4944  （Couple 内的交接块）
	if (u->orders != nullptr) {
		v->orders_backup = v->orders;
		v->orders = u->orders;
		u->orders = nullptr;
		v->orders_backup_real_index = v->cur_real_order_index;
		v->orders_backup_implicit_index = v->cur_implicit_order_index;
		v->unitnumber_backup = 0;
		if (u->unitnumber != 0) {
			v->unitnumber_backup = v->unitnumber;
			v->unitnumber = u->unitnumber;
			u->unitnumber = 0;
		}
```

| 数据 | 动作 | 语义 |
|---|---|---|
| `v->orders` | → `v->orders_backup` | **机车自己的排程进入休眠** |
| `u->orders` | → `v->orders` | **合并列车执行"被挂方"的排程**（指针直搬） |
| `v->cur_real/implicit_order_index` | → `orders_backup_real/implicit_index` | 备份机车排程位置 |
| `v->cur_real/implicit_order_index` | ← `u->cur_real/implicit_order_index` | **继承车组的排程位置**（从等待点继续） |
| 车号 | `unitnumber_backup` 备份 + 互换 | 仅当 `u->unitnumber != 0` |
| `v->current_order` / `dest_tile` | 清空 | 丢掉本 tick 的 `GOTO_COUPLE` |
| `WAIT_COUPLE` | 跳过（`train_cmd.cpp:4962-4965`） | 使下一 tick 进入车组排程的后续订单 |

交接完成后，剥离被挂方的独立身份（**这里已经显式关闭车辆窗口**，是"降级"路径漏掉的对照组）：

```cpp
// src/train_cmd.cpp:4980-4985
			/* We are now part of another train, remove all independent identity. */
			CloseWindowById(WindowClass::VehicleView, u->index);
			CloseWindowById(WindowClass::VehicleOrders, u->index);
			CloseWindowById(WindowClass::VehicleRefit, u->index);
			CloseWindowById(WindowClass::VehicleDetails, u->index);
			CloseWindowById(WindowClass::VehicleTimetable, u->index);
```

⚠️ **注意 `u->orders = nullptr;` 是裸赋值**，没有走 `DeleteVehicleOrders(u)`。而 `DeleteVehicleOrders()` 内部对共享链有专门处理：

```cpp
// src/order_cmd.cpp:3479-3483（DeleteVehicleOrders 的共享分支）
	if (v->IsOrderListShared()) {
		/* Remove ourself from the shared order list. */
		UpdateDeparturesWindowVehicleFilter(v->orders, false);
		v->RemoveFromShared();
		v->orders = nullptr;
	}
```

`IsOrderListShared()` = `orders != nullptr && orders->IsShared()`（`vehicle_base.h:834`）。
→ 若等待方曾用 Ctrl+共享排程，裸搬后：`OrderList::num_vehicles` 不减、`first_shared` 仍指向该车（但其 `orders == nullptr`），共享链被破坏。

---

## 3. 解挂还原：`DecoupleTrain()`

**进入条件**：`v->orders_backup != nullptr`（与 Couple 严格镜像）。

```cpp
// src/train_cmd.cpp:3935-3947
	if (v->orders_backup != nullptr) {
		OrderList *loco_orders = v->orders_backup;
		u->orders = v->orders;
		VehicleOrderID decouple_idx = v->cur_real_order_index;
		v->orders = loco_orders;
		v->orders_backup = nullptr;
		v->cur_real_order_index = v->orders_backup_real_index;
		v->cur_implicit_order_index = v->orders_backup_implicit_index;
```

| 数据 | 动作 |
|---|---|
| `v->orders`（车组排程） | → `u`（还给解出的部分） |
| `v->orders_backup` | → `v->orders` 并置空（**机车恢复自己的排程**） |
| `IncrementRealOrderIndex()`（`train_cmd.cpp:3958`） | **跳过已完成的 `GOTO_COUPLE`**，否则永久卡死 |
| `current_order.Free()`（`train_cmd.cpp:3963`） | 清掉刚执行的 `DECOUPLE`，否则 `ProcessOrders` 不推进 |
| `u` 的定位 | 从 `DECOUPLE` 之后的**下一个 `WAIT_COUPLE`** 恢复（`3972-3988`）；找不到则 `InsertOrder(u, WAIT_COUPLE, 0)` 插到最前 |
| 车号 | `unitnumber_backup != 0` 时互换（`4011-4015`） |
| ★ 清理 | `u->ClearSegmentFront()`（`4029`）——**只清链头 ★，链内更深的 ★ 保留**，便于按边界再拆 |

链内其它 ★ 保留的设计意图（注释原文）：

```cpp
// src/train_cmd.cpp:4025-4029
	/* R3R: the decoupled part is now an independent train, so drop the
	 * segment-front marker on its head. Segment-front markers further down the
	 * chain (when several segments were decoupled at once) are kept so the new
	 * train can be decoupled at those boundaries again. */
	u->ClearSegmentFront();
```

### 3.1 触发门（何时执行 DECOUPLE）
`DECOUPLE` 不带目的地；触发条件是**列车停稳在前一条真实命令（`GOTO_STATION`/`GOTO_DEPOT`/`GOTO_WAYPOINT`）的终点，且 `next == DECOUPLE`**。

---

## 4. 解挂目标与边界选择（"解哪几段"）

`DECOUPLE` 订单在 `flags` 里编码边界（`order_base.h:654-696`）：

- `bit0`：`GetDecouple()`（OrderDecoupleFlags，解挂动作开关）
- `bits1-6`：`GetNumDecouple()`（边界值，0 = auto）
- `bit7`：`GetDecoupleFromHeadBoundary()`（从链头数 vs 从链尾数）

三种语义（`train_cmd.cpp:3792-3808` 注释）：

| 模式 | 含义 |
|---|---|
| `Auto`（value 0） | 释放最后挂上来的那一段 |
| `TailSegments`（value N） | 释放尾部 N 段 |
| `HeadBoundary`（value n） | 在第 n 段与第 n+1 段之间切开，**n 之后的所有段一起释放** |

解析落到 `GetSegmentHeadFromRear()`（`3687`）/ `GetSegmentBoundaryFromHead()`（`3718`）/ `GetDecoupleVehicle()`（`3792+`）。

⚠️ **兜底**：列车没有任何 ★ 时**回退到 OpenTTD 原生逐车启发式**（`3811-3823`）。即"不成段的车厢链拒绝解挂"（记忆 P7 的后半）**尚未实现**；连挂侧倒是实现了：

```cpp
// src/train_cmd.cpp:5104-5110
	/* Target: a car-only formation (zero-power train front: engine bit with a
	 * wagon rail vehicle type), or a plain primary train holding a WAIT_COUPLE
	 * order (the pxp-decouple flavour, which never becomes a formation). P7: a
	 * bare (non-segment) free wagon chain is NOT a valid couple target — it must
	 * first be turned into a segment (waiting formation) via MakeSegment. */
```

`GOTO_COUPLE` 的命中判据在 `GetCouplePosition()`（`train_cmd.cpp:5129`），比较**机车鼻尖 ↔ 车底尾**的中心距。

---

## 5. 其它会搬动排程的路径

### 5.1 换端重排的身份迁移 —— `R3RRelocateFrontIdentity()`

把排程 / 备份 / 各索引 / 车号 / 当前订单 / profit / dispatch_records / name **整体搬到新链头**，旧链头清零：

```cpp
// src/train_cmd.cpp:4512-4525
	to->orders = from->orders;
	from->orders = nullptr;
	to->orders_backup = from->orders_backup;
	from->orders_backup = nullptr;
	to->orders_backup_real_index = from->orders_backup_real_index;
	...
```

### 5.2 段降级 —— `CmdDemoteSegment()`

```cpp
// src/vehicle_cmd.cpp:650-662（wagon-only 分支）
			} else {
				/* One-step demotion of a wagon-only segment: straight back to a
				 * loose free wagon chain (a free wagon chain cannot hold a
				 * schedule or a number). */
				if (seg->orders != nullptr) DeleteVehicleOrders(seg);
				if (seg->unitnumber != 0) Company::Get(seg->owner)->freeunits[VehicleType::Train].ReleaseID(seg->unitnumber);
```

- **含真机车的段**：只撤段边界，保留列车与排程/车号；
- **纯车厢段 / car-only formation**：`DeleteVehicleOrders` + 释放车号 + `R3RDestroyCarOnlyFormation` → **排程被销毁**。

### 5.3 卖车 / 换头（上游逻辑，R3R 需对齐）
- `CmdSellRailWagon`：卖头车时 `new_head->orders = first->orders; new_head->AddToShared(first); DeleteVehicleOrders(first);`（`train_cmd.cpp:2650-2652`）——**这里走的是正规的共享链迁移**，与 Couple 的裸搬形成对比。
- `CmdMoveRailVehicle` 头部身份搬迁：`src_head->orders = src->orders; if (src_head->orders != nullptr) src_head->AddToShared(src);`（`train_cmd.cpp:2524-2525`）。

> **对比结论**：上游原生路径一律 `AddToShared` + `DeleteVehicleOrders`；R3R 的 Couple/Decouple 用的是裸指针直搬（基于"排程独有"假设）。

---

## 6. 已定义但**未接线**的字段

| 字段 | 位置 | 状态 |
|---|---|---|
| `OrderDecoupleOrdersFlags`：`ODOF_KEEP_ORDERS` / `ODOF_KEEP_ORDERS_NO_LOAD` / `ODOF_INHERIT_ORDERS` / `ODOF_WAIT_FOR_COUPLE` | `order_type.h:161-167` | 全仓 `.cpp` **零使用点** |
| `Order::GetDecoupleFirstOrdersType()` / `GetDecoupleSecondOrdersType()` | `order_base.h` | **从未被调用** |
| `OrderList::decouple_first_orders` / `decouple_second_orders` | `order_base.h` | 只在存档读写（`order_sl.cpp`），无设置点 |
| `Order::GetNumCouple()`（"挂几节"，0=不限） | `order_base.h:702` | 只有定义，`.cpp` **零使用点** |

即：**UI 上目前没有任何"解挂后排程怎么分"、"挂几节"的选项**。`ODOF_*` 是上游 JGRPP 遗留结构，R3R 完全用第 2/3 节的 `orders_backup` 硬编码规则替代。

---

## 7. 多次连挂 / 解挂：现状与风险

### ✅ 已能跑通
| 场景 | 说明 |
|---|---|
| 多轮 挂 → 解 → 挂 → 解 | 每轮成对，`DecoupleTrain` 会把 `orders_backup` 置空并恢复，状态干净 |
| 一次挂多段 | 多段链只有链头持 `orders`，交接正常；链内各 ★ 仅作边界标记 |
| 一次解多段 | 多段一起出去，由链头 `u` 统一持有那份排程；链内 ★ 保留可再拆 |
| 连挂目标合法性 | 已实现 P7 前半：裸 free wagon 链不是合法目标，必须先 `MakeSegment` |

### ⚠️ 四个风险点（研究重点）

**① `orders_backup` 只有一层，不支持嵌套连挂**

```cpp
// src/vehicle_base.h:375-379
	OrderList *orders = nullptr;                 ///< Pointer to the order list for this vehicle
	OrderList *orders_backup = nullptr;          ///< R3R: backup of this vehicle's own orders while it executes a coupled consist's schedule (restored on decouple). NOSAVE.
	UnitID unitnumber_backup = 0;                ///< R3R: this vehicle's own unit number while it inherits the consist's number (restored on decouple). NOSAVE.
	VehicleOrderID orders_backup_real_index = INVALID_VEH_ORDER_ID;     ///< R3R: own real-order position while coupled (restored on decouple). NOSAVE.
```

- 触发路径：机车 L 挂车组 A（`L->orders_backup = L_orders`，`L->orders = A_orders`）→ **不解挂**，A 的排程里含 `GOTO_COUPLE` 指向车组 B 的 `WAIT_COUPLE` → 下一 tick `ProcessOrders` → `TrainCoupleHandler` → `Couple` 再次执行 `v->orders_backup = v->orders` → **把 A 的排程写进本该存 L 排程的槽**。
- 后果：**L 自己的排程指针被覆盖丢失**（`OrderList` 对象成孤儿 → 内存泄漏 + 排程丢失）；解挂 B 时 L 恢复的是 A 的排程。
- `TrainCoupleHandler` 调用点：`train_cmd.cpp:9106`（depot 分支）、`9485`（停稳贴合）、`9498`（行驶中）——即**只要合并列车又拿到 `GOTO_COUPLE` 且条件满足，嵌套就会发生**，没有层数保护。

**② `orders_backup` / `unitnumber_backup` / 两个 backup index 全部 `NOSAVE`**

- 已在 `src/saveload/` 全目录检索：**零命中**，即这几个字段根本不参与存档。
- 后果：**在连挂状态下存档 / 读档，备份直接消失**。读档后解挂：`v->orders_backup == nullptr` → 第 3 节的还原块整体跳过 → 解出部分 `u->orders` 保持 `nullptr`（**失去排程**），机车则继续留着车组的排程（车组排程被"吞"了）。
- 多人游戏 + 自动存档下这是**可复现的持续性数据损坏**。

**③ 交接条件与还原条件不对称**

- 交接看 `u->orders != nullptr`；还原看 `v->orders_backup != nullptr`。
- **机车本来无排程 + 车组有排程** 时：`v->orders_backup = nullptr`、车组排程搬给机车；解挂时还原块不执行 → `u` 拿不回排程、也没被插 `WAIT_COUPLE`；机车继续跑那份车组排程。
- 反向组合（机车有排程 + 车组无排程）有专门处理（`train_cmd.cpp:3918-3934` 的 "no hand-over" 注释），**这个方向没有**。

**④ `orders` 裸搬会破坏共享订单链**

- Couple：`u->orders = nullptr;`（`4928`）——不走 `RemoveFromShared()`；
- Decouple：`u->orders = v->orders;`（`3937`）——不走 `AddToShared()`。
- 若任一侧的排程处于共享状态（玩家 Ctrl+共享），会产生：
  - `OrderList::num_vehicles` 计数与实际持有者不一致（`order_cmd.cpp:819-823`）；
  - `first_shared` 可能仍指向一个 `orders == nullptr` 的车 → 其它 `FirstShared()` 调用方拿到失效列表指针。
- R3R 代码注释已声明该前提（"机车 orders 独有、不与他人共享，Couple 的 orders 交接同为指针直搬；若未来出现共享 orders 需先退出共享链"），**但目前没有强制校验**。

**⑤ 解挂侧没有"不成段即拒绝"**
无 ★ 时回退原生逐车切分，会直接切开一条普通列车（原生行为保留，非 R3R 语义）。

---

## 8. 改造建议（供后续功能设计取舍）

| 目标 | 建议 |
|---|---|
| 支持**嵌套连挂** | 把 `orders_backup` 升级为**栈**（如 `std::vector<OrderBackup>` / 固定深度环形），或在"段"上挂一份排程，由段序决定还原顺序；Couple 入栈、Decouple 出栈 |
| 支持**存档** | 给备份结构接存档（需处理 `OrderList` 生命周期与 `IsOrderListShared` 分支）；或改为不依赖备份的设计 |
| 修**不对称** | 把还原条件从 `orders_backup != nullptr` 改为**显式标志位**（"本次交接确实发生过 hand-over"），或让交接时总是登记（哪怕 `nullptr` 也占位） |
| 防**共享链破坏** | Couple/Decouple 的 `orders` 搬迁改为复用 `DeleteVehicleOrders()` / `AddToShared()`（与 `CmdSellRailWagon`、`CmdMoveRailVehicle` 对齐），或在入口 `dbg_assert(!u->IsOrderListShared())` |
| **多次连挂/解挂的语义控制** | 现有 `ODOF_*`（前/后段分别"保留/继承/等待"）与 `GetNumCouple()`（挂几节）都是现成骨架，接 UI + 在 `Couple`/`DecoupleTrain` 里读取即可 |
| **P7 收尾** | 解挂侧补"无 ★ 即拒绝"，与连挂侧 `GetCouplePosition` 的拒绝对齐 |

---

## 9. 关键代码索引

| 主题 | 位置 |
|---|---|
| 备份槽字段（NOSAVE） | `src/vehicle_base.h:375-379` |
| `IsOrderListShared()` | `src/vehicle_base.h:834` |
| `TrainHasEngine()` | `src/train_cmd.cpp:3662-3675` |
| ★ 段头解析 | `src/train_cmd.cpp:3687` / `3718` / `3792-3808` |
| `DecoupleTrain()` 还原块 | `src/train_cmd.cpp:3935-3947`、`3958`、`3963`、`3972-3988`、`4011-4015`、`4025-4029` |
| `R3RRelocateFrontIdentity()` | `src/train_cmd.cpp:4512-4525` |
| `Couple()` hand-over | `src/train_cmd.cpp:4925-4944` |
| Couple 关窗（身份剥离对照） | `src/train_cmd.cpp:4980-4985` |
| P7 连挂目标合法性 | `src/train_cmd.cpp:5104-5110` |
| `GetCouplePosition()` 命中判据 | `src/train_cmd.cpp:5129` |
| `TrainCoupleHandler()` 及调用点 | `src/train_cmd.cpp:5141`、`9106`、`9485`、`9498` |
| 卖车/换头的正规排程迁移 | `src/train_cmd.cpp:2524-2525`、`2650-2652` |
| `CmdDemoteSegment()` 排程销毁 | `src/vehicle_cmd.cpp:650-662` |
| `OrderDecoupleOrdersFlags`（未接线） | `src/order_type.h:161-167` |
| DECOUPLE 边界编码 | `src/order_base.h:654-696`、`680`、`687`、`693` |
| `GetNumCouple()`（未接线） | `src/order_base.h:702` |
| `MakeDecouple/MakeGoToCouple/MakeWaitCouple` | `src/order_base.h:289-291` |
| `DeleteVehicleOrders()` | `src/order_cmd.cpp:3472-3492` |
| `OrderList::RemoveVehicle()` | `src/order_cmd.cpp:819-823` |
| `AddToShared()` / `RemoveFromShared()` | `src/vehicle.cpp:4555` / `4578` |
| `orders_backup` 存档检索 | `src/saveload/`（**零命中**） |

---

## 10. 场景推演：A 挂 BC → 只解 C（AB 走）

这是把"排程归属"裂缝暴露得最清楚的场景：**机车 A 去挂上车组 BC，之后只把尾部 C 解下来，AB 继续跑。**

### 10.1 当前实现的答案

**AB 走 A 的老计划；C 拿走整份车组计划、被钉在 `WAIT_COUPLE` 原地等下一台机车。**

挂车那一刻（`Couple`，`train_cmd.cpp:4925-4965`）：

```
A.orders_backup      = orders_A          // A 的老计划休眠
A.orders             = orders_BC         // 合并列车执行车组的计划
BC头.orders           = nullptr           // 车组头不再持计划
A.cur_real/implicit  = BC 当时的 index    // 继承车组位置（若正停在 WAIT_COUPLE 再 +1 跳过）
```

解挂那一刻（`DecoupleTrain`，`train_cmd.cpp:3935-3993`；此处 `v` = AB、`u` = C）：

```
u.orders = v.orders                    // C ← orders_BC（正在跑的那份）
v.orders = v.orders_backup             // AB ← orders_A（机车老计划）
v.cur_real/implicit = 备份位置 → IncrementRealOrderIndex()   // 跳过已完成的 GOTO_COUPLE
u.cur_real/implicit/timetable = 从 DECOUPLE 之后卷绕找的第一个 WAIT_COUPLE
                               （找不到就在最前面插一条）
```

| 谁 | 拿到什么计划 | 从哪继续 |
|---|---|---|
| **AB**（走） | `orders_A`（机车自己的出勤表） | 那条已完成的 `GOTO_COUPLE` 的**下一条** |
| **C**（留） | `orders_BC`（整份车组计划） | 强制回到 `WAIT_COUPLE`，**原地待挂** |

### 10.2 三个问题

**① B 成了"计划孤儿"（核心矛盾）**

`orders_BC` 是**给 B+C 一体**设计的计划。现在计划随 C 走了（C 却钉在 `WAIT_COUPLE` 拿不动），而 **B 跟着 A 走了，描述"B 该去哪"的订单却在 C 手上**。AB 执行 `orders_A`——那是**机车自己的出勤表**，玩家写它时本不必知道车上会多一节 B。

→ 结果：**"B 该去哪"这段运输在计划层面凭空消失**；`orders_BC` 里被 `DECOUPLE` 分开的那半截也再无执行者，只能靠玩家事先在 `orders_A` 里补写。

**② C 拿到的是一份"用不上的计划"**

C 被强行钉在 `WAIT_COUPLE`，因此 `orders_BC` 里 `DECOUPLE` **之后**的订单对 C 是**死代码**（除非将来又来一台机车挂它，那批订单才由新机车接手）。
隐患：`wait_idx` 是从 `decouple_idx` 往后**卷绕找第一个** `WAIT_COUPLE`（`3975-3978`）；车组计划含多轮循环时可能选到**不是当前站点对应的那一个**。

**③ A 的老计划常常"无单可跑"，甚至绕回去再挂车**

`orders_A` 常见只有一条 `GOTO_COUPLE`。而 `IncrementRealOrderIndex()` **带回绕**（`vehicle_base.h:1052-1072`）→ 越过后绕回 0 → **AB 又跑去挂车**。
日志佐证：09-09 复测"解挂后机车回 38,27 库执行 GOTO_COUPLE 找不到车底 → COUPLE-FAIL 刷屏"，即 AB 回到 `orders_A` 绕回那条已完成的挂车订单 —— **是排程归属问题，不是寻路 bug**。

### 10.3 根因：挂车用车组计划、解挂用机车计划的"混搭"

| 时刻 | 执行谁的排程 |
|---|---|
| 挂车后 | **车组的** `orders_BC` ← 模型"车组路线制" |
| 解挂后（留下的 AB） | **机车的** `orders_A` ← 模型"机车路线制" |
| 解挂后（解出的 C） | **车组的** `orders_BC`，但钉在 `WAIT_COUPLE` |

- **整体挂、整体解**时自洽（解下来的就是原来那个车组，拿回自己的计划天经地义）；
- **部分解**时不成立：车组被切成两半，而**计划只有一份、没有被切分** → 必然有一半成为孤儿。

### 10.4 四种语义模型

| 模型 | 内容 | 评价 |
|---|---|---|
| **1 · 计划跟解出的部分走**（= 现状） | AB → `orders_A`；C → `orders_BC`（待挂） | 适用"机车=调车机、车组=循环拖车单元"；**只在整体解时正确** |
| **2 · 计划跟留下的列车走**（本场景正解） | AB → 继承 `orders_BC`，从 `DECOUPLE` 之后继续；C → 只插一条 `WAIT_COUPLE` | `DECOUPLE` 是写在 `orders_BC` 里的**步骤**，写在它后面的订单本就该由"继续跑的那一方"执行 → 同时解决问题 ① ② |
| **3 · 由 `DECOUPLE` 订单显式声明**（推荐最终形态） | 复用 `ODOF_KEEP_ORDERS` / `ODOF_INHERIT_ORDERS` / `ODOF_WAIT_FOR_COUPLE` + `decouple_first/second_orders`，前后段各自选策略 | 模型 1 / 2 只是它的两个默认组合；字段现成，缺 UI + 读取点 |
| **4 · 段级排程**（最彻底） | `orders` 改为**每段一份**；`Couple` 不搬 orders，只"合并列车同时引用多段计划"，`Decouple` 天然各归各段 | 任意多次挂/解都自洽；改动最大（`OrderList` 生命周期、GUI、时间表、profit 归属全要动） |

### 10.5 `orders_A` 存哪、何时恢复

模型 2 / 3 都需要 AB "跑完继承来的计划后再回家"，即**排程栈**；而现状 `orders_backup` **单层**且"解挂瞬间就还"（`vehicle_base.h:375-379`），只能表达"解挂即回家"。
可简化：**挂车即作废 `orders_A`**（`GOTO_COUPLE` 之后的计划视为已消费，不恢复）——对"机车专职当动力"的玩法足够，且省掉栈。

### 10.6 落地建议

1. **短期（本场景）**：接线 `ODOF_*`，默认组合定为"**前半 INHERIT + 后半 WAIT_FOR_COUPLE**"（= 模型 2）；同时把 `orders_backup` 改成支持暂存（或先做"挂车作废"）。
   落点很小：把 `DecoupleTrain` 的 `3935-3947` 块里两行归属对调 —— "C 拿到 `orders_BC` + 钉 `WAIT_COUPLE`"改为"`v` 保留 `orders_BC`（跳过 `DECOUPLE`）+ `u` 插一条 `WAIT_COUPLE`"。
2. **长期**：若要做"多次连挂/解挂"（嵌套），直接上**段级排程**（模型 4），否则栈深度失控。

---

## 11. 多阶段场景 T8701（两次解挂 + 中途交接）

用户实景（南宁局 T8701 国际联运），比 §10 更能说明问题：**同一次运行里有两次解挂，且两次的"排程归属"要求相反。**

### 11.1 流程

三个"部分"：**一** = 机车（南宁 → 越南同登）；**二** = 国际车底（南宁 → 越南嘉林）；**三** = 国内车底（南宁 → 凭祥）。

南宁站开局：**二、三 已经是两个段组成的一条链**。机车 一 去挂它 → 前进方向顺序 **一-二-三**。

| 阶段 | 动作 |
|---|---|
| ① 南宁 | 一 挂上 [二\|三] → 一-二-三 |
| ② 凭祥 | 解下 **三**（三留在凭祥），一-二 继续 |
| ③ 同登 | 解下 **二**（二留在同登），一 单独 |
| ④ 同登后 | 一 掉头回南宁；二 被在同登等待的机车挂上，继续去嘉林 |

**用户给出的结构性不变式：先解挂的部分总是在后解挂部分的外侧。** 因为解挂永远"剥链尾"，所以**每次解挂解下的都是当前链的最尾段，留下的总是其余全部** —— 前半/后半的判定是确定性的（正好对应 `GetDecoupleVehicle()` 的 TailSegments/Auto 语义）。

### 11.2 推荐排程

`orders_23`（[二\|三] 链持，**必须是这份排程在跑路线**）：

| idx | 订单 | 备注 |
|---|---|---|
| 0 | `WAIT_COUPLE` | 南宁等 一 来挂 |
| 1 | `GOTO_STATION 凭祥` | 一-二-三 一起开 |
| 2 | `DECOUPLE` | 解下最外侧的 **三** |
| 3 | `WAIT_COUPLE` | **三 的**等待点（凭祥） |
| 4 | `GOTO_STATION 同登` | 一-二 继续 |
| 5 | `DECOUPLE` | 解下 **二**（新外侧） |
| 6 | `WAIT_COUPLE` | **二 的**等待点（同登） |
| 7 | `GOTO_STATION 嘉林` | 二 被新机车挂上后继续 |

`orders_1`（机车一）= `[ GOTO_COUPLE(南宁挂车点), GOTO_STATION 南宁 ]`。

**约定**：`DECOUPLE` 后面跟的 `WAIT_COUPLE` 是"**交给解出方**"的等待点；继承该排程的一方要**跳过**它（与 `Couple` 跳过 `WAIT_COUPLE` 同理）。

### 11.3 两次解挂的归属必须**相反**

| 解挂 | 站 | 前半（留下） | 后半（解出） |
|---|---|---|---|
| #1 | 凭祥 | 一-二 → **INHERIT**（继续 `orders_23`，跳过 idx3，去 idx4 同登） | 三 → **WAIT_FOR_COUPLE**（凭祥就地等） |
| #2 | 同登 | 一 → **RESTORE**（拿回 `orders_1` → 回南宁） | 二 → **WAIT_FOR_COUPLE**（同登等新机车） |

**#1 要"留下的一方继承当前排程"，#2 要"留下的一方拿回自己的排程"** → 单向默认规则（现状"挂车用车组的、解挂还机车的"）不可能做出 T8701。**必须由 `DECOUPLE` 订单逐次声明归属 = 模型 3（§10.4）**，模型 1/2 只是它的两个默认组合。

### 11.4 现状四处硬伤（全部会卡住 T8701）

| # | 硬伤 | 代码位置 | 后果 |
|---|---|---|---|
| 1 | `orders_backup` **单层且第一次解挂即消费** | `3942-3947`（`v->orders = loco_orders; v->orders_backup = nullptr;`） | #1 就把 `orders_1` 还给 一-二 并清空备份 → #2 无备份可还，**一 回不了南宁** |
| 2 | **第一次解挂把整份 `orders_23` 发给三** | `3937`（`u->orders = v->orders;`） | 一-二 失去"凭祥→同登"，路线断掉 |
| 3 | 等待点用"**从 `decouple_idx` 往后卷绕找第一个 `WAIT_COUPLE`**" | `3972-3985` | 多等待点排程下规则不成立；一旦 idx3 缺失就会搜到 **idx6（同登）**，把凭祥的三钉到同登。应改为**就地插入**或按 (站, 解挂) 配对 |
| 4 | **无 hand-over 时 `current_order.Free()` / `IncrementRealOrderIndex()` 都不执行** | `3958`、`3963` 均在 `if (v->orders_backup != nullptr)` 内 | "路线全写机车排程、车底链不写排程"的替代写法会出事：解挂后 `cur_real_order_index` 停在原位，触发钩子（`9145` / `9226-9268`，只读 index + 车位）**每 tick 仍成立 → 反复触发 → 二三被级联解掉** |

> 硬伤 4 的机制补充：`DECOUPLE` 钩子在 `ProcessOrders` **之前**、条件是 `cur_speed == 0` 且 `at_order_dest`（`cur_real` 是 GOTO_STATION/DEPOT 且停在对应 tile）且 `cur_real_order_index + 1` 是 DECOUPLE。它是 **(index, tile) 的纯函数**——没人推进 index 就会不断重触发。所以"免排程"写法也不可行，`orders_23` 必须存在。

### 11.5 最小改动清单（落地 T8701）

1. **排程备份改栈**（或"只在声明为 RESTORE 的那次解挂才出栈"）——解决硬伤 1。
2. **解挂归属显式化**：接线 `ODOF_*`（前半/后半各自 KEEP / INHERIT / WAIT_FOR_COUPLE）——解决硬伤 2，并让 #1/#2 能给出相反策略。
3. **等待点改为就地插入**（或按解挂点配对），不再"往后搜第一个"——解决硬伤 3。
4. **把 `current_order.Free()` + 索引推进移出 hand-over 守卫**（无交接时也应消费掉刚执行的 DECOUPLE）——解决硬伤 4，顺带修掉"无交接 + 自身排程含 DECOUPLE ⇒ 级联解挂"。

---

## 12. 挂车优先级值（"挂完之后谁的命令生效"）

### 12.1 用户提出的算法（原话转述）

给每个段一个**值**，挂车时改写它：

- **A 去挂 B** ⇒ `值(B) < 值(A)`（被挂的一方更小）。
- 连挂组合体执行 **值最小** 的那个段的命令。
- 例：`AB` 组合体去挂 `C` ⇒ `C < B < A`；`C` 去挂 `AB` 组合体 ⇒ `B < A < C`。
- 关键例句：`C < B < A` 之后 **B 解挂出去、又回来挂 `AC` 组合体** ⇒ 挂完后 `C < A < B`（B 从"中间"变成"最大"）。

**引入这个值的唯一目的：决定挂车之后该显现谁的调度命令。**

### 12.2 等价形式（已确认）

把"值"看成一条**优先级列表**，**表头 = 最小 = 命令拥有者**，则整条规则可以写成一步：

> **一次挂车 = 被动方列表在前，主动方列表整块接在其后。**
> `new_list = list(被动方) ++ list(主动方)`

四个例句逐一验证（全部吻合）：

| # | 事件 | 主动 / 被动 | 结果列表 | 命令拥有者 |
|---|---|---|---|---|
| 1 | A 挂 B | A / B | `[B] ++ [A] = [B, A]` | B ✓ `B<A` |
| 2 | AB 挂 C | AB / C | `[C] ++ [B, A] = [C, B, A]` | C ✓ `C<B<A` |
| 3 | C 挂 AB | C / AB | `[B, A] ++ [C] = [B, A, C]` | B ✓ `B<A<C` |
| 4 | B 挂 AC | B / AC | `[C, A] ++ [B] = [C, A, B]` | C ✓ `C<A<B` |

**推论（很重要）**：

- 挂车后**命令拥有者 = 被动方列表的表头**；**主动方整块退到末尾，放弃自己的命令**。
- 所以 B 回来挂 AC 时必须听 C 的——正好是用户要的效果。
- 与现状实现**方向一致**：`Couple()` 里 `v(主动机车).orders ← u(被动车组).orders`，本质就是"被挂方赢"（§2）。
- 与现状的**差别**：现状只在两个"整体"之间交换，没有"列表"概念；一旦被动方是**多段链**（如 AC、或 T8701 的 [二\|三]），"表头是谁"就无从得知——**这正是值/列表要补的信息**。

### 12.3 已确认的四个设计决定（2026-09-12）

| # | 问题 | 用户的决定 |
|---|---|---|
| 1 | 值的载体 | **每个段（`Train` 对象）一个值**，不是整个组合体共用一个 |
| 2 | 挂车后各段自己的 orders | **一律保留，只是"不显现"**（不再像现状那样把指针搬走 + `u->orders = nullptr`） |
| 3 | 解挂后值怎么变 | **两部分都重排为 1..n 的连续序号**（保持各自原有相对序）。例：`1,2,3,4` 中解出 3 ⇒ 长链 `1,2,3`（原 1,2,4 压缩），短链 `1`（就是 3） |
| 4 | 解挂后谁接管 | **仍是该部分里值最小的段接管并显现调度**（**不需要** `DECOUPLE` 订单显式声明归属） |

**决定 4 的举例**（用户的题面）：

设 1,2,3,4 依次连挂，值 `1 < 2 < 3 < 4`，当前命令拥有者 = 1。现在 **3 解挂出去**：

- 长链 `[1, 2, 4]` → 重排 → `1 < 2 < 3` → **命令拥有者 = 原 1**
- 短链 `[3]` → 重排 → `1` → **命令拥有者 = 3**

> 所以"解挂后由值最小者接管"在"重排序号"的前提下，等价于一句话：**每部分各自回到自己列表的表头**。

### 12.4 T8701 用这套规则重演（关键结论：不再需要 backup / RESTORE）

前提：**每个段都带自己那条真实排程**（段级排程），连挂后由列表表头（值最小）那条排程**驱动整个组合体**。

| 段 | 自己的 orders |
|---|---|
| 二 | `orders_2` = `[GOTO 凭祥, DECOUPLE, GOTO 同登, DECOUPLE, WAIT_COUPLE, GOTO 嘉林]`（**主排程**，即 §11.2 的 `orders_23`） |
| 三 | `orders_3` = `[WAIT_COUPLE]`（凭祥就地等） |
| 一 | `orders_1` = `[GOTO_COUPLE 南宁挂车点, WAIT_COUPLE, GOTO 南宁]` |

（南宁开局：二、三 已成链，设列表为 `[二, 三]`）

| 阶段 | 事件 | 一-二-三 列表 | 驱动者 | 结果 |
|---|---|---|---|---|
| ① 南宁 | 一 挂 二-三 链（主动=一，被动=链） | `[二, 三] ++ [一] = [二, 三, 一]` | **二** | `orders_2` 跑：南宁→凭祥，三节一起开 ✓ |
| ② 凭祥 | 解下链尾 三 | 重排：长 `[二,一]→[1,2]`；短 `[三]→[1]` | 长链=**二** / 短链=**三** | 一-二 继续 `orders_2` 中 DECOUPLE 之后 → 同登 ✓；三 跑自己的 `orders_3` = 就地等 ✓ |
| ③ 同登 | 解下链尾 二 | 重排：`[一]→[1]`；`[二]→[1]` | 一 自己 / 二 自己 | **一 的 `orders_1` 自动生效** → 掉头回南宁 ✓（**不需要 RESTORE**）；二 跑 `orders_2` 的 `WAIT_COUPLE` → 就地等 ✓ |
| ④ 同登后 | 新机车 挂 二 | `[二] ++ [新机车]` | **二** | `orders_2` 继续 → 嘉林 ✓ |

**结论：§11.3 里"两次解挂归属必须相反"的难题，在"段级 orders + 值最小驱动"下自然消解**——
#1 的"留下方继承后续路线"= 因为留下的链表头仍然是 二；#2 的"留下方拿回 `orders_1`"= 因为一 单独成链后表头就是它自己。
于是 **§11.5 的最小改动清单可以大幅精简**：

| §11.5 原项 | 用值规则后 |
|---|---|
| 1. 备份改栈 | **不需要**（根本不再搬 `orders`） |
| 2. 接线 `ODOF_*` 显式声明归属 | **不需要**（归属由"表头驱动"推出） |
| 3. 等待点改为就地插入 / 配对 | **不需要**（等待点写在解出方自己的排程里，不靠"往后搜"） |
| 4. `current_order.Free()` + 索引推进移出 hand-over 守卫 | **仍然必需**，且范围扩大（见 §12.5） |

### 12.5 这套规则带来的新代价（必须一起设计）

1. **不再搬 `orders` ⇒ 引擎"读排程"的入口要改。** 现状组合体的排程与进度都在"段头/机车"身上（`Couple()` 搬指针就是为了这个）。新模型要求：**由值最小的那个段的 `orders` + `cur_real_order_index` 驱动整个组合体**，所有读点都要重定向。
2. **非驱动段的索引推进。** ① 里 一 执行了 `GOTO_COUPLE`，但当时驱动者是 二 —— 一 自己的 index 必须**照样推进**（否则 ③ 单独成链后还卡在 `GOTO_COUPLE` 上）。这是**硬伤 4 的推广**：索引推进必须与"谁是驱动者"解耦。
3. **解挂时"刚执行完的那个 DECOUPLE"必须被消费**，否则 ② 之后 二 会撞上自己刚执行过的 DECOUPLE → 级联解挂（硬伤 4 原样重现）。
4. **`WAIT_COUPLE` 与"驱动者切换"的时序**：一 在 ① 被挂上后应跳过 二 排程里的 `WAIT_COUPLE`（现状 §2 有专门跳过逻辑），改成驱动者切换后需重新梳理。

### 12.6 三个实现细节（已核查代码，给出结论）

#### (1) 组合体的 `current_order` / `dest_tile` 归谁 —— **归"物理链头"，这是硬事实**

代码事实（不是选择）：

- `Vehicle::current_order`、`Vehicle::dest_tile`、`Vehicle::orders` 都是 **per-`Vehicle`** 字段（`vehicle_base.h:288 / 373 / 375`）；`cur_real/implicit_order_index` 在基类 `BaseConsist`（`base_consist.h:71-73`）。**`Train` 自己没有订单字段。**
- 每 tick 只 tick **链头**：`vehicle.cpp:1684-1686` 遍历 `_tick_train_front_cache`，而该 cache 只收 `Previous() == nullptr` 的车（`vehicle.cpp:1491,1500`）→ 一列多段组合体**只推入一个 `Train`**。
- `Train::Tick()` → `TrainLocoHandler(consist, ...)` → `ProcessOrders(consist)`（`train_cmd.cpp:9305`），`consist == this` = 链头。
- 耦合后 `NormaliseSubtypes` 会清掉非链头车的 `GVSF_FRONT`（`train_cmd.cpp:4804` 注释）→ 段头**天然不参与**订单推进。
- 显示读点同样以链头为准：车辆窗口读 `Vehicle::Get(window_number)->current_order`（`vehicle_gui.cpp:4129,4310`），Orders 窗口读 `this->vehicle->orders`（`order_gui.cpp:1174+`）。

> **推论**：T8701 ① 里物理链头是 **一（机车）**，但"表头/驱动者"是 **二** ——
> **两者并不重合，所以"驱动者"不可能直接等于"被 tick 的那段"。**

因此落地有两条路，必须二选一：

| 路线 | 做法 | 代价 |
|---|---|---|
| **A. 借用（推荐）** | 保持"链头持有生效 orders"不变；链头 `orders` **指向**被选中（值最小）那段自己的 `OrderList`，**原主仍持有它**（不置 `nullptr`）。链头自己的 `orders` 存进 `orders_backup` 休眠 | 同一个 `OrderList` 被两个 `Vehicle` 引用 → 必须走 JGRPP **现成的共享订单骨架**（`IsOrderListShared()` / `AddToShared` / `RemoveFromShared`，`vehicle_base.h:828-834`、`order_cmd.cpp:3479-3483`）。正好把 §0.5 的"裸指针直搬破坏共享链"一并修掉 |
| **B. 真·段级驱动** | 把 `ProcessOrders` / `cur_real_order_index` / `current_order` 全部重定向到"被选中的段"对象上，链头只管物理 | 改动面极大（`TrainLocoHandler` 的 `consist` 参数、所有 `v->cur_real_order_index` 读点、窗口读点） |

**用户 2026-09-12 拍板：走 A。**

它 = "现状机制 + 把 `u->orders = nullptr` 改成""保留原主 + 加入共享链"""，语义正好就是用户答案 2（各段保留自己的 orders，只是不显现）。

约定：

- **值 = 链内优先级序号，从 1 连续**；**表头 = 值最小者 = 该链的"命令拥有者"**。
- **链头（物理 `Previous()==nullptr` 那段，也是唯一被 tick 的那段）= 执行者**；它的 `orders` 借用表头的 `OrderList`，它自己那条存 `orders_backup` 休眠。
- 链头 == 表头时无需借用，直接用自己那条（这就是 T8701 ③ 的"自动 RESTORE"）。
- 挂车：`[被动列表] ++ [主动列表]` 后重排 1..n；解挂：两侧各自压缩重排 1..n。
- 每次解挂 = **剥当前链的物理最尾段**（§12.6(3)）。

#### (2) 段新建时的初值 —— **不写，靠默认值；我们的"值"取 1**

代码事实：新建 `Train`/`Vehicle`（`vehicle.cpp:506-521`、`train.h:236`）、`CmdMakeSegment`（`vehicle_cmd.cpp:530-578`）、`R3RCreateCarOnlyFormation`（`train_cmd.cpp:1731-1744`）**都不主动写订单字段**，全靠类内默认值：

- `current_order` = `OT_NOTHING`（`current_order{}` → `type = 0`，`order_type.h:76-77`）
- `orders` = `nullptr`；`dest_tile` = `INVALID_TILE`；三个 index = `0`
- **没有任何"新建时自动插入 `OT_NOTHING`/`OT_LOADING` 订单"的逻辑**

对"值"的建议：

> **新建段 ⇒ 它自己就是单段链 ⇒ 值 = 1（表头 = 自己）。**
> 并且**挂车、解挂都重排为连续序号**（挂车：`[被动] ++ [主动]` 后重排；解挂：各部分压缩重排）。
> 这样恒有 **"值 = 链内优先级序号，从 1 连续"**，"值最小"≡"序号 1"≡"表头"，实现最简（不需要比较、不需要特判 0）。
> 采 0 或 1 只是偏移，取 **1** 与用户答案 3 的"重排为 1,2,3"表述一致。

#### (3) "先解挂的在外侧" —— **降级为"实际场景不会出现中间解挂"的非约束**

用户 2026-09-12 澄清：那只是举例时为了防例外加的限定，**实际游戏中不会出现"要解挂的段在中间被两边夹住"**。

因此可以安全固化的前提是：

> **每次解挂 = 剥掉当前链的物理最尾那一段**（留下的 = 其余全部）。

这与 §11.1 的结构性不变式、`GetDecoupleVehicle()` 的 TailSegments/Auto 语义一致（`train_cmd.cpp:3772`）。
→ 不需要为"中间解挂"设计值列表的切割算法；**值列表的切割永远是"尾部单段 + 其余"**（见 §12.4 ②③）。

### 12.7 顺带确认的死代码/活代码

- **死代码**（定义但无任何读取点，`src/` 全量搜索确认）：`ODOF_KEEP_ORDERS / ODOF_KEEP_ORDERS_NO_LOAD / ODOF_INHERIT_ORDERS / ODOF_WAIT_FOR_COUPLE`（`order_type.h:161-167`）、四个 `Get/SetDecoupleFirstOrdersType` / `...SecondOrdersType`（`order_base.h:825-842`，仅定义 + 序列化 + 字段）、`Order::GetNumCouple()`（`order_base.h:702`，零调用）。
  → §11.5 第 2 项"接线 `ODOF_*`"**可以直接划掉**，不必再救活它们。
- **活代码**：`orders_backup` / `orders_backup_real_index` / `orders_backup_implicit_index` / `unitnumber_backup`（读写点：`vehicle.cpp:1241-1243` 析构、`train_cmd.cpp:3935-3945` 解挂、`4516-4519` 身份迁移、`4925-4941` 挂车；**`NOSAVE`**）。
  → 走路线 A 时，这套字段从"搬走排程的临时寄存"变成**"链头自己那条休眠排程的寄存处"**，语义更干净，且**不再需要升级成栈**（因为休眠的永远是链头自己那一条，嵌套连挂不再互相覆盖 —— 待验证）。

---

## 13. 待确认清单

1. **嵌套连挂是否在目标场景内**——若走 §12.6(1) 路线 A，`orders_backup` 的语义变成"**链头自己那条休眠排程的寄存处**"（而非"每次挂车临时挪走别人的排程"），单层是否仍够用需要重新验证（关键：是否存在"同一个链头同时休眠两条排程"的情形）。
2. **`NOSAVE` 是否可接受**——否则需接存档（`OrderList` 生命周期 + 共享引用计数）。
3. **是否要接线 `ODOF_*` / `GetNumCouple`**——若要"解挂后前/后段分别保留/继承排程"，这是现成骨架，只差 UI + 读取点。
4. **共享订单链（Ctrl+共享）是否需要在 R3R 流程中被支持**——若不需要，建议加 `dbg_assert` 固化成前提；若需要，必须改走 `AddToShared` / `RemoveFromShared`。
5. ~~**"部分解挂"的排程归属选哪个模型**（§10.4）~~ → **已定为模型 4（段级排程）+ "值最小者驱动"**（§12.3~12.5），不再需要模型 3（`DECOUPLE` 显式声明）。`DecoupleTrain` 的交接块改为"**两侧重排序号 + 各自链头改指向自己的表头**"（§12.6(1) 路线 A）。待定：是否仍保留模型 3 作为可选的逐次覆盖开关。
6. **T8701 多阶段场景是否纳入目标**（§11）——若是，则 §11.5 的四项改动（备份栈 / 归属显式化 / 等待点就地插入 / 推进移出守卫）从"可选"变成"必需"。
7. ~~**挂车优先级值（§12）的四个设计点**~~ → **已于 2026-09-12 确认**（每段一个值 / 各段保留 orders 只是不显现 / 解挂两部分重排 1..n / 仍由值最小者接管驱动）。剩下的是 §12.6 的三个实现细节。

---

## 14. 路线 A 实施方案（已拍板 2026-09-12）

### 14.1 借用不注册为"共享订单链"

路线 A 的"链头借用表头的 `OrderList`"**不走 JGRPP 的 `AddToShared` / `RemoveFromShared`**：

- 那套机制是给"用户显式共享排程"用的，会带上大量 GUI 副作用 —— 车辆窗口/订单窗口显示"共享订单"、`STR_ORDERS_END_OF_SHARED_ORDERS`、`RemoveFromShared` 会关闭或重建订单窗口（`vehicle.cpp:4578-4609`）、`EnableOrderListWindow` 判定等。
- 借用是**引擎内部的临时引用**，不该让用户看到"这列车在共享订单"。
- 代价：**必须自己保证"借来的 list 不被释放"**（原主还持有它）。这是 §14.5 的守卫。

### 14.2 复用现有字段，但**必须**新增一个"是否借用"标记

现状已经有：`orders_backup` / `orders_backup_real_index` / `orders_backup_implicit_index`（`vehicle_base.h:376-379`）。
它们现有的语义就是"链头自己那条排程被寄存"，可以直接复用。

**但不能拿 `orders_backup != nullptr` 当"当前 orders 是借来的"判据**：机车耦合前可能**本来就没有排程**（`orders == nullptr`），此时借用后 `orders_backup` 仍是 `nullptr`，判据失效 —— 会把它当成"没借用"，进而在析构/清空订单时**释放掉原主的排程**。

因此新增一个显式布尔量 `r3r_orders_borrowed`（见 14.3）。

### 14.3 新增字段

`vehicle_base.h`，紧跟 `orders_backup_implicit_index` 之后：

```cpp
uint16_t r3r_priority = 1;  ///< R3R: priority rank of this segment inside its consist (1..n, contiguous).
                            ///< The lowest value is the consist's "command owner": its schedule is the one
                            ///< shown/executed. Rebuilt on couple (passive-first) and on decouple (compacted). NOSAVE.
```

### 14.4 新增工具函数（`train_cmd.cpp`，紧邻 `DecoupleTrain` 之前）

```cpp
/* R3R: 段序 = 链头自身 + 其后所有带 SegmentFront 标记的车（与 R3RFlipChainBySegments 的切分一致） */
static std::vector<Train *> R3RGetSegmentHeads(Train *chain);

/* R3R: 表头 = 段序里 r3r_priority 最小者（值最小 = 命令拥有者） */
static Train *R3RGetPriorityHead(Train *chain);

/* R3R: 按当前段序把 r3r_priority 重排为 1..n（不变相对序） */
static void R3RRenumberPriorities(Train *chain);

/* R3R: 让链头指向表头的排程（借用），并寄存链头自己那条。
 *      链头 == 表头时反过来取回自己那条（这就是 T8701 ③ 的"自动 RESTORE"）。 */
static void R3RSyncDrivingOrders(Train *chain, bool inherit_progress_from_head);
```

`R3RSyncDrivingOrders(chain, ...)` 的行为：

| 情形 | 动作 |
|---|---|
| 链头 == 表头 | 取回自己那条：`orders = orders_backup`（若在借用中），索引同时取回 `orders_backup_real/implicit_index`；`orders_backup = nullptr` |
| 链头 != 表头 | 若尚未在借用中：`orders_backup = orders`、记录索引 → 然后 `orders = 表头->orders`。索引仅当 `inherit_progress_from_head == true`（挂车那一下）才用表头的索引初始化 |

### 14.5 三处改动点

**(a) 挂车 —— `train_cmd.cpp:4925-4951`（`Couple()` 内的 orders 交接块）**

- 保留：`v->orders_backup = v->orders; v->orders = u->orders;`、索引交接、unitnumber 交接。
- **删除核心一行：`u->orders = nullptr;`** ← 路线 A 的本质
- 新增：合并两侧段序（被动在前）→ `R3RRenumberPriorities(合并后的链)` → `R3RSyncDrivingOrders(合并后的链, true)`。

**(b) 解挂 —— `train_cmd.cpp:3935-4015`（`DecoupleTrain` 的 orders 交接块）**

改写为通用规则（代替现在"解出方无条件继承、留下方无条件取回"）：

```
running = v->orders                       // 当前生效排程（可能是借的）
1) v 侧（留下）：R3RRenumberPriorities(v 侧链); R3RSyncDrivingOrders(v 侧链, false)
2) u 侧（解出）：R3RRenumberPriorities(u 侧链); R3RSyncDrivingOrders(u 侧链, false)
3) 进度交接：
   - 若 u->orders == running（u 就是生效排程的主人）⇒ u 继承进度：
        u->cur_real_order_index = (刚执行的 DECOUPLE 的下一条)
   - 否则 u 保持自己那条排程的原进度（不动）—— T8701 ② 的三 走这条
```

> 这条"解出方是否就是生效排程的主人"就是现状 `u->orders = v->orders;` 的**泛化**：
> 现状无条件把生效排程给解出方；新规则只在"解出方本来就是主人"时把**进度**给它。

**(c) 释放守卫 —— `vehicle.cpp:1238-1244` + `order_cmd.cpp:3486`**

`DeleteVehicleOrders()` 在未共享时会直接 `v->orders->FreeChain(...)`（`order_cmd.cpp:3489`）。
借用关系不是共享链，`IsOrderListShared()` 为 false ⇒ **会把原主的排程释放掉，留下悬空指针**。因此：

- `vehicle.cpp` 析构路径：在 `DeleteVehicleOrders(this)` **之前**先归还借用
  （`if (orders_backup != nullptr) { orders = orders_backup; orders_backup = nullptr; }`），
  这样析构释放的是"自己那条"，借来的那条留在原主身上。
- `order_cmd.cpp:3486`：加同样语义的守卫（车辆窗口/订单窗口对借用中的车执行"清空排程"时，只清自己那条）。

### 14.6 风险清单（实施时必须盯）

1. **订单窗口编辑的是"借来的排程"** —— 链头显示/编辑的就是表头那条。语义上是对的（链头正在执行它），但要确认所有订单增删路径都不会误释放它（§14.5(c) 只盖了 `DeleteVehicleOrders`，需逐一核 `OrderList::DeleteOrder` 到空表的路径）。
2. **存档**：`orders_backup*` 是 `NOSAVE`（现状如此）。路线 A 下"借来的 orders"在存档里会变成链头**自己持有一条完整排程**，读档后 `orders_backup` 丢失 ⇒ 解挂时链头无法"取回自己那条"。**这是路线 A 的一个真实缺陷，需要单独决定**（要么把 `orders_backup` 落盘，要么在存档前做一次规范化）。
3. **非驱动段索引冻结**：§12.5 第 2 条仍然成立 —— 借用期间只有链头的索引在推进；靠 §14.5(b) 第 3) 条的"主人继承进度"补回来。

### 14.7 实施状态（2026-09-12，已落地，编译通过）

| 文件 | 改动 |
|---|---|
| `vehicle_base.h` | 新增 `uint16_t r3r_priority = 1`、`bool r3r_orders_borrowed = false`（均 `NOSAVE`），紧跟 `orders_backup_implicit_index` |
| `train_cmd.cpp` | 新增 `R3RGetSegmentHeads` / `R3RGetLowestPriority` / `R3RGetPriorityHead` / `R3RRenumberPriorities` / `R3RMergePriorities` / `R3RSyncDrivingOrders`（紧邻 `GetDecoupleVehicle` 之前） |
| `train_cmd.cpp` `Couple()` | 合并前先抓取两侧段列表；`R3RMergePriorities` + `R3RRenumberPriorities`；**删除 `u->orders = nullptr;`**，改为 `v->orders = couple_owner->orders` + `v->r3r_orders_borrowed = true`；进度继承源由 `u` 改为 `couple_owner` |
| `train_cmd.cpp` `DecoupleTrain()` | 守卫由 `orders_backup != nullptr` 改为 `r3r_orders_borrowed`；**删除 `u->orders = v->orders;`**（仅当 `u->orders == nullptr`，即无自有排程的普通车底，才沿用旧继承）；v 侧的手工还原改由 `R3RSyncDrivingOrders` 决定（成为表头才取回）；两侧 `R3RRenumberPriorities` + `R3RSyncDrivingOrders`；等待点搜索/插入用 `u_inherited_running` 收口 |
| `order_cmd.cpp` `DeleteVehicleOrders()` | 新增归还守卫：借用中先 `orders = orders_backup` 再走原有释放逻辑，避免释放原主的排程 |

验证：`ninja -C build CMakeFiles/openttd_lib.dir/src/{train_cmd,order_cmd}.cpp.obj` → **exit 0，无新增警告**。
全量重建（改了 `vehicle_base.h`，需重编 687 obj）请走 `rebuild_R3R.cmd`。

## 15. KI-92：depot 编辑（拖拽/卖车）也必须在拆合链后重建「排程所有权」（2026-09-17 第 44 轮，已修）

### 15.1 现象与现场

玩家实报：**在车库内手动解耦之后，前面的机车段没有变回自己持有的调度命令，仍保持耦合时刻的调度命令**（并问「机车与列车属不同公司有无关系」）。

现场（`build\R3R_debug.log`）：

```
ARRANGE-IN dh=-1 dst=-1 sh=0 src=9 mc=1      <- 把车底整段拖到空行（拆链）
MAKESEG-CMD veh=9 tile=1766 exec=0/1         <- 分离出的零动力段被自动升级为段
...
SKIP-STOPPED veh=0 order=0 real=17 spd=0 tile=38,27
SKIP-STOPPED veh=9 order=0 real=17 spd=0 tile=38,27
```

`veh=0`（机车，eng 494）与 `veh=9`（车底段头，eng 325）**同为 `real=17`** ⇒ 两条链仍指向**同一份 `OrderList`**，即路线 A 的「借用」没有被解除。

### 15.2 根因

路线 A 的借用状态（`r3r_priority` / `r3r_orders_borrowed` / `orders_backup`）**只在两处维护**：`Couple()`（起借 + `R3RMergePriorities`/`R3RRenumberPriorities`）与 `DecoupleTrain()`（`R3RRenumberPriorities` + `R3RSyncDrivingOrders` 归还）。

而车库里的手动解耦**根本不经 `DecoupleTrain()`** —— 它走 `TrainDepotMoveSegment()`（`depot_gui.cpp`，拖到空行 = `dst == nullptr`）→ `Command<MoveRailVehicle>` → `CmdMoveRailVehicle()` → `ArrangeTrains()` 把链切开。该路径原先**零 R3R 交接**，于是：

1. 机车链头仍 `r3r_orders_borrowed == true`、`orders` 仍是车底的列表 ⇒ 继续跑车组排程（玩家所见）；
2. 它自己的排程还躺在单层的 `orders_backup` 里，只会被下一次耦合覆盖（永久丢失路径存在）；
3. `CmdSellRailWagon()` 若卖掉 **owner 段**（车底），会 `DeleteVehicleOrders(owner)` 真的释放那份列表，而机车仍持有指针 ⇒ **悬垂**（`DeleteVehicleOrders()` 的归还守卫只对「借用者自己被删」生效）。

**与公司无关**：命令层没有跨公司分支，同公司复现路径完全一致。

### 15.3 修复

- 新增静态辅助（`train_cmd.cpp`，定义紧接 `R3RSyncDrivingOrders()` 之后；因 `CmdMoveRailVehicle()` 位于文件更上方，故在同文件更早处加前向声明，未动任何头文件 ⇒ 增量编译合法）：

```cpp
static void R3RSyncChainAfterDepotEdit(Train *chain)
{
	if (chain == nullptr) return;
	R3RRenumberPriorities(chain);
	R3RSyncDrivingOrders(chain, false);
}
```

- 三个调用点：`CmdMoveRailVehicle()` Execute 分支对 `src_head` / `dst_head` 各一次（**放在 Execute 块内**，试算阶段不改状态）；`CmdSellRailWagon()` 在 `NormaliseTrainHead(new_head)` 之后、`delete sell_head` **之前**一次（先归还借用，再让被卖部分释放列表 ⇒ 顺带消掉 15.2 的第 3 条）。

### 15.4 为什么 renumber 先、sync 后，且对「没拆链的编辑」安全

`R3RRenumberPriorities()` 是**压紧编号并保持相对序**（严格 `<`，平局回退物理序），不是按物理序重排：

| 编辑 | renumber 后 | `R3RGetPriorityHead()` | sync 结果 |
|---|---|---|---|
| 拖出车底（只剩机车，原 priority 2） | 机车 → 1 | 机车自己 | `owner == chain` ⇒ **归还 `orders_backup`**（正是玩家要的） |
| 整列在车库列表换行/重排（不拆） | 相对序不变（车底 1、机车 2） | 车底（在后） | `orders == owner->orders` ⇒ 早退，**借用保持**，行为与改前逐位一致 |
| 拖入一整列车拼接（两边原 priority 均 1，平局） | 物理序在前者胜 ⇒ 原链头 1 | 原链头 | `owner == chain` ⇒ 链头驱动**自己**的排程；被拖入列车的排程挂起但**不丢**（再拖开即恢复） |

### 15.5 已知边界（未覆盖 / 待确认）

1. **只修了「拖拽」与「卖车」两条 depot 编辑路径**；直接改段结构的 `CmdMakeSegment()` / `CmdDemoteSegment()`（`vehicle_cmd.cpp`）**未加同步**。降级只撤销假引擎身份与 ★、**不删** `OrderList`，所以不会悬垂；但「降级掉 owner 段后机车应否收回自己的排程」这一语义尚未定义，待复测。
2. **depot 内手动拼接**的语义刻意选最保守解（见 15.4 第三行），与订单耦合的「车组计划优先」不同；若希望「后挂入者优先」，需另立规则并同步改这两处钩子。

### 15.6 编译与核验（第 44 轮）

仅改 `src\train_cmd.cpp` ⇒ 增量合法：`EXIT_CODE=0`、日志末行 `[3/3] Linking CXX executable openttd.exe`、`train_cmd.cpp.obj` @ 20:59:26 > 源码 @ 20:58:09、产物 `build\openttd.exe` @ **2026-09-17 21:00:25**（50 637 824 B）。未改头文件与语言文件（语言包版本沿用 `0xEDC76EB`）。

**待实测**：①拖出车底后机车显示自己的排程、排程面板与后备索引正确；②拖出机车本体同理；③整列重排与改前逐位一致；④卖掉车底后机车不悬垂、不崩。台账条目见 `R3R_KNOWN_ISSUES.md` 的 **KI-92**。
