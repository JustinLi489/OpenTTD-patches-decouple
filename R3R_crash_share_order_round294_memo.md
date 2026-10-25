# 第 294 轮临时分析报告：联机客户端崩溃 — `order_cmd.cpp:1001` 共享订单环「记账 vs 环」脱钩

- 日期：2026-10-24
- 性质：**纯分析**（玩家要求"你先分析"），零源码改动、零构建
- 对应 KI：`R3R_KNOWN_ISSUES.md` 的 **KI-465**（高，根因已做实 / 修法待拍板）、**KI-466**（中，判读口径）

---

## 一、输入与现场

| 项 | 值 |
|---|---|
| crashlog | `C:\Users\冯洁敏\Downloads\crash-20261020T150946Z.log` |
| 断言 | `:11-12` `Assertion failed at line 1001 of D:\sourcecode of JGRPP\src\order_cmd.cpp: this->num_vehicles == check_num_vehicles` ⇒ 打印 `2, 1` |
| 调用上下文 | `:14` `0: CallVehicleTicks -> OnPeriodic: veh: 21` |
| 宿主 | 联机**客户端**（`:27` `Dedicated: no`），exe `r3r-stable-2026-10-16-1-g2fa4018d2-m`（Build 2026-10-18 02:27:55，52 129 536 B） |
| Command Log | `:323` 起全部 `cc: 0, lc: 0`（客户端私有命令，未提交） |

**唯一调用链**：`CallVehicleTicks` → `OnPeriodic(veh 21)` → `CheckOrders()` → `order_cmd.cpp:3611` `v->orders->DebugCheckSanity()`。

⇒ 断言不是"某条命令当场写坏"，而是"**周期自检发现一张早已脱钩的表**"。这决定了修法方向：要修的是**记账维护点**，不是崩溃点。

---

## 二、两个断言点是同一不变式的两侧（不是两个 bug）

```997:1001:src/order_cmd.cpp
		for (const Vehicle *v = this->first_shared; v != nullptr; v = v->NextShared()) {
			++check_num_vehicles;
			assert_msg(v->orders == this, "Vehicle %u is not in this order list", v->index);
		}

		assert_msg(this->num_vehicles == check_num_vehicles, "There are %u vehicles in this order list, but the counter is %u", check_num_vehicles, this->num_vehicles);
```

- `:999` = 「环上说这辆车属于本表，但它的 `orders` 已经指到别处」（**借用窗口形态**）。
- `:1001` = 「表上记着 2 辆，沿环只走得到 1 辆」（**漏摘环形态**，本次现场）。
- 两者共用同一个不变式：**`num_vehicles` 必须等于环上实际可走到的车辆数，且环上每辆车的 `orders` 必须指回本表**。

## 三、两套记账天生可以脱钩

`OrderList` 有**两套互不校验的记账**：

| 记账 | 位置 | 存档 | 维护点 |
|---|---|---|---|
| 计数 `num_vehicles` | `order_base.h:1581`（NOSAVE） | 不存 | `AddVehicle()`（`order_base.h:1762`，`++`）/ `RemoveVehicle()`（`--`） |
| 共享环 `first_shared/previous_shared/next_shared` | `order_base.h:1582`（NOSAVE 指针） | 不存 | `Vehicle::AddToShared()` / `Vehicle::RemoveFromShared()` |

```950:954:src/order_cmd.cpp
void OrderList::RemoveVehicle(Vehicle *v)
{
	--this->num_vehicles;
	this->first_shared = v->NextShared();
}
```

- `RemoveVehicle()` **无下界检查**、**不校验 `v` 是否真在环上**。
- ⇒ 只要有一处路径"只做了其中一套"，计数与环就永久漂移，且**永不自我校正**（没有任何 `RecountVehicles()` 之类的重算）。

## 四、`2, 1` 的确切产生路径（本轮新发现）

### 4.1 前提：路线 A 的"排程借用"故意绕过记账

R3R 的订单表借用（第 44 轮起的路线 A）是**裸改 `v->orders`**：

```
v->orders_backup = v->orders;
v->orders        = u->orders;   // 指向别人的表，不调 AddToShared/RemoveFromShared
```

于是合法地存在一个中间态：

> **`v` 仍登记为原表 `L` 的 `first_shared`、仍挂在 `L` 的环上，但 `v->orders` 已指向借来的表 `B`。**

### 4.2 释放路径踩到它

`DeleteVehicleOrders()`（`order_cmd.cpp:3770-3852`）对 `v` 的处理顺序：

1. 借用归还块：`v->r3r_orders_borrowed` 相关处理；
2. `:3781` 判定 `IsOrderListShared()` —— **看的是 `v->orders`，即借来的表 `B`** ⇒ 为真；
3. ⇒ 只把自己**从 `B` 摘环**；**原表 `L` 完全没被处理**；
4. ⇒ 落到末尾 R3R 兜底：

```3843:3852:src/order_cmd.cpp
	for (OrderList *ol : OrderList::Iterate()) {
		if (ol->GetFirstSharedVehicle() != v) continue;
		if (ol == v->orders || ol == v->orders_backup) continue; // handled above
		r3r_dead_registered.push_back(ol);
	}
	for (OrderList *ol : r3r_dead_registered) { ...; ol->FreeChain(false); }
```

5. `FreeChain(false)` 走**移交分支**（`:690-705`：`R3RFindOrderListReferrer()` 找到活车 `w` ⇒ `first_shared = w` 后 **直接 `return`**）——**不减 `num_vehicles`、不销毁表**。

### 4.3 结果

- 表 `L`：`num_vehicles == 2`（`v` 的那一份从未被减）；
- 环：`[v, w]`，`w->next_shared == nullptr`；
- `v` 一死 ⇒ 环上只走得到 `w` 一辆 ⇒ **计数 2、实测环长 1**。

**实参顺序已核对源码**（本轮取证，纠正"按 %u 顺序反推"的猜测风险）：

```1001:1001:src/order_cmd.cpp
	assert_msg(this->num_vehicles == check_num_vehicles, "{}, {}", this->num_vehicles, check_num_vehicles);
```

⇒ 打印的 `2, 1` 逐位对应 **`num_vehicles = 2`（表上计的）、`check_num_vehicles = 1`（环上走得到的）**，与 4.2 的"`FreeChain` 移交只改 `first_shared`、不减计数"逐字吻合。

> 顺带记录：`RemoveVehicle()`（`:950-954`）无下界保护、也不校验 `v` 是否真在环上 ⇒ 反向漂移（计数被多减、`IsShared()` 提前变 `false` 导致走错分支）在结构上同样可能，属于同族隐患。

- `:999` 形态：借用窗口内（`orders` 已搬走、环上还没摘）若恰逢自检，就在 `:999` 用两个指针值崩掉。两条路径**同源**（都是"两套记账只维护了一套"）。

## 五、为什么单机长期测不出来

1. 必须**至少两辆车共享同一张订单表**（`Ctrl` 共享订单 / R3R 借用）才有 `num_vehicles > 1`；单机主线每车独占一张表（count=1、环长 1）永不满足不变式。
2. 自检本身是**极度稀疏的门控**（见第六节）：20 天一次、只查链头、只在客户端本地设置允许时。
3. NOSAVE 本地指针不参与 desync 哈希 ⇒ 只有单端 assert 一种暴露形式。

## 六、KI-466：`CheckOrders()` 的判读口径（联机排错陷阱）

```3549:3565:src/order_cmd.cpp
（五重门控）
```

| # | 门控 | 含义 / 陷阱 |
|---|---|---|
| 1 | `order_review_system == Off` → return（`:3552`） | `_settings_client` = **本机客户端设置，不同步** |
| 2 | `Crashed` → 跳过（`:3555`） | — |
| 3 | `ExcludeStopped && Stopped` → 跳过（`:3558`） | 同为客户端设置；`Stopped` 车不进检查 |
| 4 | `FirstShared() != v` → 跳过（`:3561`） | 只检查共享链**头**，环上其余车辆不查 |
| 5 | `owner == _local_company && day_counter % 20 == 0 && !GVSF_VIRTUAL`（`:3565`） | 只查自己的车、20 天一次 |

**三个直接推论**：

1. **专用服务器永不触发** —— dedicated 的 `_local_company` 是旁观，`:3565` 恒假；旁观客户端、非车主的客户端同样恒假。⇒ **"服务端没崩"是结构性的，不能当作"没有 desync"的证据**。
2. **同一局两个客户端会一个崩一个不崩** —— 取决于各自 `order_review_system` 与各自操纵的车。⇒ 排查时**不能**用"他没崩"排除 bug。
3. **发现时延最大 20 天且只查链头那张表** ⇒ 本次 `2, 1` 很可能早已存在，崩溃只是"第一次被检查到"。

**为什么这类损坏永远不报 desync**：`num_vehicles / first_shared / previous_shared / next_shared` 全是 NOSAVE 的**本地对象指针**，每端各自分配、地址不可能跨端相等 ⇒ 天然不参与 desync 哈希；能跨端同步的只有语义内容（订单本体、车辆位置）。⇒「共享环记账坏了」在联机中**只能是单端 assert 或后续行为异常**。

## 七、排除项（触发者取证）

Command Log（`:323` 及之前 256 条）末段只有：

- `CmdReverseTrainDirection 75`
- `CmdAutomateTimetable 51 / 75`
- `CmdBulkChangeTimetable 51 / 75`
- 两条 `CmdMassStartStopVehicle`

**没有** R3R 的 couple / decouple / 车库编辑 / 卖车命令，而被检查的是 **veh 21**。

- `CmdAutomateTimetable` 只写 `automate_timetable` 标志（`timetable_cmd.cpp:645-651`），不碰 `orders` 指针；
- `CmdBulkChangeTimetable` 只改时刻表数值；
- ⇒ 两者**基本可排除**为触发者。损坏早于最近约 2 个游戏日，或来自命令之外的路径（读档、AI、另一客户端）。

## 八、未确认（U 项）

- **U-1**：崩溃客户端的 `order_review_system` 取值（crashlog 不含客户端设置）。若为 `ExcludeStopped`，则"该车原先 `Stopped` 被跳过、被 `CmdMassStartStopVehicle` 启动后才进入检查"是"为什么此刻崩"的一种解释（`:323` 前两条正是 `CmdMassStartStopVehicle`）。
- **U-2**：`num_vehicles` 是 NOSAVE，读档靠 `OrderList::Initialize()`（`order_cmd.cpp:607-633`：先从 `first_shared` 反向走 `PreviousShared` 把 `first_shared` 推到最前，再正向走 `NextShared` 累加）重算 ⇒ **读档本身不会产生脱钩**，故排除"读档直接造成"；仍待核对 R3R 借用表（`orders_backup` 走 `R3VP` 稀疏块）与 `Initialize()` 的先后次序。
- **U-3**：该客户端是"读档快照入局"还是"全程在场"未知；若入局读档，还需确认入局瞬间是否已有漂移（判据：入局后第一个提交点调 `DebugCheckSanity()`）。
- **U-4**：`:999` 那一次崩溃的 crashlog 未取得（玩家只提供了 `:1001` 这份），两种形态的覆盖度需一并验证。

## 九、修法候选（待玩家拍板，本轮未改码）

- **A（推荐，治标且自愈，纯 `.cpp` ⇒ 增量安全）**
  新增 `OrderList::RecountVehicles()`：沿 `first_shared → NextShared()` 走一遍并把结果赋给 `num_vehicles`（**环是唯一事实来源**），调用点：
  1. `FreeChain()` 移交分支 `return` 前（`:696-701`）—— 修本次 `2, 1`；
  2. `DeleteVehicleOrders()` 兜底循环 `FreeChain(false)` 之后（`:3851`）—— 兜住同族路径。
  另建议 `RemoveVehicle()` 加下界保护（`if (this->num_vehicles > 0) --`）与"只从真实环上摘"的断言。
- **B（治本，R3R 侧）** 借用/停放不再裸改 `orders`，改走"注册共享"。改动面覆盖 Couple / DecoupleTrain / 车库编辑 / 克隆 / 读档**五条边**（长期记忆 ID 35644846 风险点④的预判）。
- **C（探针，最低风险先行）** 在五个提交点直接调 `orders->DebugCheckSanity()` 并把 `num_vehicles` / 实测环长写 `R3R_debug.log`，把发现时延从 20 天降到 1 个提交点，用于定位"哪条边先坏"。

> 建议顺序：**C 先落（只加只读探针）→ 复现一次拿到"哪条边"的证据 → 再决定 A 只兜底还是 A+B 一起做。** 若玩家只想要"不再崩"，A 单做即可（自愈式，不改变语义）。

## 十、复测 / 验证判据（修法 A 落地后）

1. 复现路径（两车 `Ctrl` 共享订单 → 挂上/解挂/卖车其中一辆 → 快进 20 天）不再出现 `:999` / `:1001`。
2. 日志中 `RECOUNT`（修法 A 的探针）出现时，前后的 `num_vehicles` 与实测环长一致。
3. 负判据：单车独占订单表（未共享）在任何操作后 `num_vehicles` 恒 1、环长 1。
4. 负判据：真共享链（2 车 `Ctrl` 共享、不做 R3R 操作）卖其中一辆后，剩下一辆仍能正常跑车、且 `num_vehicles == 1`。
5. `IsShared()` 语义不退化：共享链卖到只剩 1 辆时必须变回 `false`（否则 `DeleteVehicleOrders` 会走错分支）。

## 十一、未做

- 未加任何代码、未构建（玩家本轮只要分析）。
- 未取得 `:999` 形态的 crashlog。
- 未核对 R3R 借用表 `orders_backup`（`R3VP` 稀疏块）与 `Initialize()` 的先后次序（U-2 的剩余部分）。

## 十二、自检归属核实（2026-10-24，`git blame` 实证）

**结论：`DebugCheckSanity()` 及其全部断言、全部调用点，都是原生 / JGRPP 上游的，R3R 一行都没碰过。**

| 位置 | 归属 | 证据 |
| --- | --- | --- |
| `OrderList::DebugCheckSanity()` 函数体 | 原生 | `rubidium 2009-01-03 f3167966290` 建立 |
| `assert_msg(v->orders == this)`（`:999`） | JGRPP 上游 | `Jonathan G Rennison 2024-10-06 3b10c59af81` |
| `assert_msg(this->num_vehicles == check_num_vehicles)`（`:1001`） | JGRPP 上游 | 同上 |
| 整个函数外层 `#ifdef WITH_ASSERT`（`:970` / `:1006`） | JGRPP 上游 | `3b10c59af81` |
| `cachecheck.cpp:621`（全表遍历） | 原生 | `rubidium 2009` |
| `afterload.cpp:4188`（**读档后**全表遍历） | JGRPP 上游 | `Jonathan G Rennison` |
| `order_cmd.cpp:3611`（`CheckOrders` 内） | 原生 | `rubidium 2004-05-08 2f4e9df0` |
| `CheckOrders()` 五重门控（`:3549-3565`） | 原生/上游 | 全部 ≤ 2016 上游提交 |

⇒ 按玩家拍板口径（原生 ⇒ 档二根治），本轮转入档二设计。

## 十三、档二可行性判定（本轮关键发现：场景 1 结构性不可行）

原生共享模型的不变式是：**每张活着的 `OrderList`，至少有一个 `orders` 指向它的已注册持有者**。
`orders_backup`（R3R 把"自己那张表"停放在车子字段里）恰好违反它，而违反后**无法用 `AddToShared/RemoveFromShared` 修复**，必须分两种情况：

### 场景 2：own 表还有别的成员（`num_vehicles >= 2`）

即本次崩溃现场（日志 `2, 1` 说明这张表原本就登记了两辆车）。

- 借出：`chain->RemoveFromShared()` 从 own 摘掉（**own 仍有别人，合法**）→ `chain->orders = donor->orders; chain->AddToShared(donor);`（`num_vehicles` 正确 +1）。
- 归还：`chain->RemoveFromShared()` 从 donor 摘掉 → `chain->orders = own; chain->AddToShared(own->GetFirstSharedVehicle());`（合法）。
- **完全自洽，档二在这一场景成立。**

### 场景 1：own 表独占（`num_vehicles == 1`，单机绝大多数情况）

- 若借出时把 chain 摘掉：`RemoveVehicle()` 使 `num_vehicles = 0`、`first_shared = nullptr`，而 `OrderList::FreeChain()` 第一行就是 `this->GetFirstSharedVehicle()->type` ⇒ **空表直接崩**（原生里"0 辆的表"是不存在的状态）。
- 若借出不摘：永远是「`first_shared == chain` 但 `chain->orders != own`」⇒ **必然触发 `:999`**；而且 `cachecheck.cpp:621`（`WITH_ASSERT` 全表）与 `afterload.cpp:4188`（**读档后**全表）都会遍历到这张被 `orders_backup` 引用的停放表。

⇒ **档二在场景 1 结构性不可行**，除非让 `orders_backup` 不再是 `OrderList*`。

### 两条真·根治路线

- **B1（推荐，中等改动）= 扩展不变式，而不是绕过自检**
  1. 借用/归还改走原生注册（场景 2 全自洽，`num_vehicles` 与环由引擎维护）；
  2. `OrderList` 增加 `bool r3r_parked`（NOSAVE，不进存档）；
  3. 场景 1 的 own 表在借用期间置 `r3r_parked = true`；
  4. `DebugCheckSanity()` 对 parked 表**只校验订单计数、跳过共享环校验**（即跳过 `:997-1001` 那几行），**非 parked 表照旧全查**；
  5. `FreeChain()` 对 parked 表走专用分支（直接销毁，不做 `GetFirstSharedVehicle()->type`）。
  ⇒ 合法停放不再误报；真损坏（非 parked）仍然崩，自检的"用处"完整保留。
- **B2（彻底，大改）= 消灭停放表**
  `orders_backup` 由 `OrderList*` 改为**订单数据副本**（`std::vector<Order>` + 调度 + 三个索引）：借用时 `FreeChain` 销毁 own 表，归还时 `OrderList::Create()` 重建。停放表从此不存在，原生不变式完整成立，`:999`/`:1001` 的所有 R3R 来源消失。
  代价：`R3VP` 存档格式变更 + 五条边（Couple / Decouple / 车库 / 克隆 / 读档）+ 旧档迁移；且会丢掉"own 表原本是 Ctrl 共享链"的成员关系（需额外记录成员列表）。
- **A（兜底，不根治）**：`RecountVehicles()` + `RemoveVehicle()` 下界保护，只保证不再打印 `2, 1`。

### 本轮建议

**B1 优先**：它让"场景 2"真正满足原生不变式（崩溃现场这条路被根治），同时用**显式 parked 标记**把"场景 1 的合法停放"从自检里排除——这是扩展不变式，不是削弱自检。B2 可作为后续轮次（需要改动存档格式）。

未做：`AddToShared/RemoveFromShared` 之外是否还有第三条注册路径；`OrderList` 池的迭代是否真的覆盖"只被 `orders_backup` 引用"的表（`KI-169` 记录说是覆盖的，本轮未复核）。

## 十四、玩家方案 P（2026-10-24 玩家提出）：诚实 `orders` + 跟随指针

### 14.1 玩家原话要点

> "我们现在不是已经实现了段的识别吗，虽然你处于耦合状态的非控制段，但是别人也能认出你。那你就把一份假的指针放到自己原来的调度计划上面，然后给你自己的真调度计划指针打一个标记：我不是这份排程的真主人，我只是因为跟随别人所以指向你。"

### 14.2 为什么这一招恰好解开 §十三 的结构性死结

§十三 的死结是：**原生不变式要求"每张活表至少有一个 `orders` 指向它的持有者"，而 `orders_backup` 违反它**。

方案 P 把方向反过来——**不是把车搬走，而是让车原地不动、只多一个"跟随"字段**：

| 项 | 现状（路线 A 借用） | 方案 P |
| --- | --- | --- |
| 自己的表 own | `first_shared = v`，但 `v->orders != own` ⇒ **`:999` 必炸** | `v->orders == own` 始终成立 ⇒ 自洽 |
| donor 表 | `v` 在用但未登记 ⇒ 计数失真 | `v` 根本没登记进去 ⇒ `num=1`、环长 1 ⇒ 自洽 |
| 车死时 | 只从"借来的表"摘名，own 表没人管 ⇒ **死车挂名额（本次 `2,1`）** | 照 `v->orders` 正常销毁 own ⇒ **坑本身消失** |
| `orders_backup` | 必须长期持有一张"无主活表" | **可以整个退役**（自己表一直在自己名下） |
| 读档 | own 表被 `ORDL` 写出但无主 ⇒ `Initialize()` 无法重建 | 无需重建，读档天然正确 |
| `R3RFindOrderListReferrer` / `FreeChain` 移交兜底 | 必需（KI-93 的命根子） | 可降级为防御性断言 |

⇒ 方案 P 是**唯一能让原生不变式完整成立、且不动存档主格式**的路线。它比 §十三 的 B1（给停放表开豁免口）更干净：B1 是"承认异常并豁免"，P 是"根本不产生异常"。

### 14.3 工程要点：必须把两类访问分开

| 类别 | 语义 | 应读 |
| --- | --- | --- |
| **执行读取** | 我现在该跑哪条命令、到站判定、寻路目的地、GUI 显示 | **有效表**（跟随中 = 被跟随的表） |
| **所有权登记** | `DeleteVehicleOrders`、`FreeChain`、`AddToShared/RemoveFromShared`、`IsOrderListShared`、`Initialize`、`ORDL` 写出 | **`v->orders` 本体**（自己的表） |

新增内容（示意）：

```cpp
// vehicle_base.h 的 Vehicle
OrderList *r3r_follow_orders = nullptr;   // 非空 = 我在跟随这张表（我不是它的主人）
VehicleID  r3r_follow_owner  = INVALID_VEHICLE; // 跟随谁（可空，仅诊断用）

OrderList *GetEffectiveOrders() const { return this->r3r_follow_orders != nullptr ? this->r3r_follow_orders : this->orders; }
bool       IsR3RFollower() const      { return this->r3r_follow_orders != nullptr; }
```

- 借用：`v->r3r_follow_orders = donor->orders; v->r3r_follow_owner = donor->index;`（**不碰 `v->orders`**）
- 归还：`v->r3r_follow_orders = nullptr;`（**不碰 `v->orders`**）
- 执行侧：把 `v->orders->GetOrderAt(...)` 一类改成 `v->GetEffectiveOrders()->...`
- 索引：`cur_real_order_index` 等本来就存在车上，语义不变，无需搬迁
- 存档：`R3VP` 稀疏块里的 `orders_backup` 换成 `r3r_follow_orders`（**字段类型同为 `REF_ORDERLIST`，块内替换，不升版本**；旧档 `orders_backup` 读入时迁移为 `r3r_follow_orders`，同时保证 `v->orders` 已被 ORDL 正确恢复）
- 迁移期：`orders_backup` 保留为只读兼容字段，等确认旧档都能读通再删

### 14.4 与既有方案的对比

| 方案 | 是否满足原生不变式 | 改存档格式 | 改执行路径 | 迁 GUI | 工程量 |
| --- | --- | --- | --- | --- | --- |
| A（重算 num） | 否（掩盖） | 否 | 否 | 否 | 极小 |
| B1（parked 豁免） | 部分（豁免） | 否 | 否 | 需过滤 | 中 |
| B2（orders_backup 数据化） | 是 | **是** | 否 | 否 | 大 |
| **P（玩家方案）** | **是** | 否（块内加字段） | **是（约 241 处解引用读取，需逐条分拣）** | 否（显示的就是有效表，天然正确） | 中偏大 |

### 14.5 风险与待验证项

- **R-1（决定工作量，2026-10-24 已统计）**：`->orders\b` 全库约 600 处（含赋值/判空/传参，属所有权语义，**不改**）；真正的**解引用读取** `->orders->` 约 **241 处**，分布：
  - `schdispatch_cmd.cpp` 47、`order_cmd.cpp` 34、`departures.cpp` 28、`order_gui.cpp` 20、`schdispatch_gui.cpp` 17、`linkgraph/refresh.cpp` 17、`timetable_gui.cpp` 16、`vehicle_base.h` 10、`vehicle.cpp` 10、`timetable_cmd.cpp` 9、`script/api/script_order.cpp` 8、`sl/vehicle_sl.cpp` 5、`viewport.cpp` 4、`vehicle_gui.cpp` 4、`train_cmd.cpp` 3，其余 ≤2（`order_dest_func.h` / `order_serialisation.cpp` / `order_backup.cpp` / `tracerestrict.cpp` / `afterload.cpp` / `script_vehicle.cpp`）。
  - 分拣口径：R3R 自有模块（`schdispatch_*` 64 + `departures.cpp` 28 = 92 处）语义全是"这辆车现在在跑什么" ⇒ **必须**改成有效表；`order_cmd.cpp` 34 处多为订单编辑（改动要写进真表，属混合读写）⇒ 逐条判定；GUI 显示类（`order_gui`/`timetable_gui`/`viewport`/`vehicle_gui`）⇒ 全改有效表。
  - 现状**没有任何**"有效订单表"访问器（全库只有存档描述 `GetOrderListDescription`），`v->orders->` 直接解引用是唯一写法 ⇒ 必须新增 `GetEffectiveOrders()` 并逐点替换。
- **R-7（存档成本，2026-10-24 已核实：几乎为零）**：`R3VP` 块的描述表 `_r3r_park_desc`（`src/sl/couple_group_sl.cpp:129-150`）走 `SlTableHeader()` / `SlTableHeaderOrRiff()`，是**表头驱动、按字段名匹配**的格式（不是固定版本号）。⇒ 直接加一行 `NSL("follow_orders", SLE_REF(Vehicle, r3r_follow_orders, REF_ORDERLIST))` 即可：旧档无该字段则取默认 `nullptr`，`Ptrs_R3VP()` 自动做指针修正，`R3RHasParkState()` 加一条判据，**无需升存档版本、无需迁移代码**。`orders_backup` 字段保留作旧档兼容（方案 P 下不再写入）。
- **R-2**：`current_order` 是每 tick 从表里解析的缓存，若某些路径只读 `current_order` 不读表，则跟随切换的当帧可能出现"旧命令"闪现一帧（可接受，需在 Couple/Decouple 提交点强制刷新 `current_order`）。
- **R-3**：`OrderList::GetFirstSharedVehicle()` 的调用者们对"链头跟随中"这一状态的假设（如 `FirstShared() != v` 的 `CheckOrders` 早退）—— 跟随者不是共享链成员，`FirstShared()` 返回自己，行为等价于"独占表"，应当更安全，需复核。
- **R-4**：`R3RSyncDrivingOrders` / `R3RPushProgressToOwner` / `R3RBorrowControlTraitsLive` 这套"借用期索引冻结/回灌"逻辑，在方案 P 下仍然需要（索引仍只由链头推进），但不再有"写回不属于自己的表"的语义冲突 —— 需逐条复核。
- **R-5**：`DeleteVehicleOrders` 里 KI-93 的 `r3r_borrow_is_shared` 例外与 `R3RFindOrderListReferrer` 兜底，在方案 P 下落为死代码，应保留但改注释，不删（防止旧档中间态）。
- **R-6**：车库拖动/克隆/读档五条边都要改成"只设跟随、不动 orders"，工作分五批，每批可独立编译验证。

### 14.6 落地顺序（建议）

1. **第一批（不带行为）**：加字段 + 访问器 + `R3VP` 字段替换 + 旧档迁移（`orders_backup` → `r3r_follow_orders`），构建通过后先验证"读旧档后跟随关系不丢"。
2. **第二批**：Couple / DecoupleTrain 两条主边改成"设跟随、不动 orders"，其余边暂留旧逻辑（旧逻辑仍会裸改 orders，需在入口断言拦截，避免两种机制并存）。
3. **第三批**：车库编辑 / 克隆 / 读档三条边。
4. **第四批**：把 KI-93 的兜底降级为断言，观察一个发布周期后再删。

**关键纪律**：第 2~3 批期间**绝不能两种机制并存于同一条边**，否则 `orders` 与 `r3r_follow_orders` 会各说各话。

## 十五、四步落地清单（玩家 2026-10-24 拍板：先落盘这四步，代码全部改完再一次性重编）

> 执行纪律：**四步全部改完之前不编译**（中途编译会因半套改动产生噪声错误）；四步改完统一走 `_tmp_inc_build.cmd`，改 `vehicle_base.h` 必然被护栏判全量（约 692 步 / 20 分钟）。

### 步骤 1：数据层 + 访问器 + 存档（不改任何行为）

**1.1 `src/vehicle_base.h`（`Vehicle`）**

```cpp
/* R3R (KI-465 修法 D): "跟随指针"。非空 = 本车在执行这张表，但它不是本车自己的表。
 * 本车自己的表始终由 this->orders 诚实持有 —— 原生不变式（每张活表至少有一个 orders
 * 指向它的已注册持有者）因此完整成立，DebugCheckSanity() 的 :999/:1001 都不可能由
 * 借用触发。执行读取走 GetEffectiveOrders()，所有权/销毁/存档仍走 orders 本体。 */
OrderList *r3r_follow_orders = nullptr;
VehicleID  r3r_follow_owner  = INVALID_VEHICLE;

OrderList *GetEffectiveOrders() const { return this->r3r_follow_orders != nullptr ? this->r3r_follow_orders : this->orders; }
bool       IsR3RFollower() const      { return this->r3r_follow_orders != nullptr; }
```

- 放在 `OrderList *orders = nullptr;` 附近（同一段，保持可读）。
- `OrderList` 在此处已是完整类型（`order_base.h` 已 include），指针成员无布局风险。
- `VehicleID` 默认构造就是 `INVALID_VEHICLE`，但显式写便于阅读。
- **不动** `orders_backup` / `orders_backup_real_index` / `orders_backup_implicit_index`（保留作兼容与回退）。

**1.2 `src/sl/couple_group_sl.cpp`（`_r3r_park_desc`）**

```cpp
NSL("follow_orders", SLE_REF(Vehicle, r3r_follow_orders, REF_ORDERLIST)),
NSL("follow_owner",  SLE_VAR(Vehicle, r3r_follow_owner, SLE_UINT32)),
```

- 该块走 `SlTableHeader()` / `SlTableHeaderOrRiff()`，**按字段名匹配** ⇒ 旧档无这两字段自动取默认 `nullptr` / `INVALID_VEHICLE`，**无需升版本、无需迁移代码**。
- `Ptrs_R3VP()` 已用 `SlObjectPtrOrNullFiltered(v, slt)` ⇒ `REF_ORDERLIST` 指针自动修正，无需新代码。
- `R3RHasParkState()` 追加 `|| v->r3r_follow_orders != nullptr || v->r3r_follow_owner != INVALID_VEHICLE`，否则跟随关系不写出。
- 存档写出顺序：`ORDL` 写表本体 → `R3VP` 写跟随指针 → `Ptrs_R3VP()` 修正。跟随目标表一定在 ORDL 里（它是别人的活表），安全。

**步骤 1 验收**：能编译；读旧档跟随字段为空；新档往返跟随字段不丢。

### 步骤 2：执行读取点替换（`->orders->` → `->GetEffectiveOrders()->`）

**2.1 口径**

| 目录/文件 | 处理 | 理由 |
| --- | --- | --- |
| `src/sl/*.cpp`（`vehicle_sl.cpp` 5、`order_sl.cpp`、`afterload.cpp` 1） | **不改**（保留 `->orders`/`->orders->`） | 存档必须写/读 `orders` 本体，改成有效表会把 donor 指针写进 own 字段，直接破坏不变式 |
| `src/order_backup.cpp`（2） | **逐条判定**，默认不改 | 备份机制操作的是本体 |
| `src/schdispatch_cmd.cpp`(47)、`departures.cpp`(28)、`schdispatch_gui.cpp`(17) | **全改** | R3R 派车/出发板全部是"这车现在在跑什么" |
| `order_cmd.cpp`(34) | **全改，但先逐条看**：`DeleteVehicleOrders` / `FreeChain` 附近用本体 | 订单编辑作用于有效表，销毁作用于本体 |
| `order_gui.cpp`(20)、`timetable_gui.cpp`(16)、`timetable_cmd.cpp`(9)、`viewport.cpp`(4)、`vehicle_gui.cpp`(4) | **全改** | 界面显示/编辑的是正在执行的表 |
| `linkgraph/refresh.cpp`(17) | **全改** | 货物去向按实际执行的订单 |
| `script/api/script_order.cpp`(8)、`script_vehicle.cpp`(1) | **全改** | 脚本看到的是实际执行的表 |
| `vehicle.cpp`(10)、`vehicle_base.h`(10) | **逐条判定** | `vehicle.cpp` 多为本体（`DeleteVehicleOrders` 等）；`vehicle_base.h` 多为内联读取（`GetOrder*`），需改 |
| 其余 ≤3 处（`train_cmd.cpp` 3、`tracerestrict.cpp` 1、`order_dest_func.h` 2、`order_serialisation.cpp` 2） | **逐条判定** | `order_dest_func.h` 是寻路目的地 ⇒ 改；`order_serialisation.cpp` 是文本导出 ⇒ 改 |

**2.2 机器辅助 + 人工复核**

1. 先按文件跑批量替换（只替换 `->orders->`，不动 `->orders`、`->orders =`、`->orders ==`、`->orders !=`）。
2. 排除 `src/sl/` 整个目录与 `src/order_backup.cpp`。
3. 逐文件 `git diff` 复核：凡替换后出现 `GetEffectiveOrders()` 的位置，问一句"这里是执行还是所有权"。
4. `const Vehicle *` 上下文：`GetEffectiveOrders()` 声明为 `const` 成员返回非 const `OrderList *`，与原生 `v->orders` 在 const 上下文的行为一致，无需额外 const 重载。

**2.3 已知需要额外处理的两处**

- `CurrentOrder` 缓存：`Couple`/`Decouple` 提交点后调用 `v->current_order.Free()` 一类已有刷新，确保切换跟随目标的那一帧不会闪旧命令。
- `IsOrderListShared()` / `FirstShared()` 相关判断：跟随者不是共享链成员，`FirstShared()` 返回自己（行为等价独占表），比现状更安全，无需改。

**步骤 2 验收**：编译 0 错误；`grep -c "->orders->" src/` 只剩 sl/ 与白名单文件。

### 步骤 3：五条边切换（借用/归还只设跟随、不动 `orders`）

| 边 | 位置 | 改法 |
| --- | --- | --- |
| 挂车 `Couple()` | `train_cmd.cpp`（`v->orders = couple_owner->orders` 一带，约 `:12484`） | 删 `v->orders` 赋值 → `v->r3r_follow_orders = couple_owner->GetEffectiveOrders(); v->r3r_follow_owner = couple_owner->index;`；`orders_backup` 不再写（保留旧字段不填） |
| 解挂 `DecoupleTrain()` | `train_cmd.cpp`（`v->orders = v->orders_backup` 一带） | 改为 `v->r3r_follow_orders = nullptr; v->r3r_follow_owner = INVALID_VEHICLE;`；**不再需要**"交出自己表 / 拿回自己的表"，因为 `orders` 从未离开自己 |
| 车库拖动 | `R3RSyncChainAfterDepotEdit()` 及其调用边 | 同上：只设/清跟随 |
| 克隆 | `CloneVehicle()` | 克隆品跟随关系复制或置空（建议置空：克隆品拿到新表更符合直觉，需在验收里确认） |
| 读档 | `AfterLoadVehiclesPhase2` / `R3RRebuildCouplePriorities()` | 跟随字段由 `Ptrs_R3VP()` 恢复；`orders_backup` 旧档迁移：`r3r_follow_orders == nullptr && orders_backup != nullptr` 时**不迁移**（旧档语义里 own 表已被 donor 顶替，无法自动还原），只清 `orders_backup` 避免误用 |

**关键**：步骤 3 完成后，`orders_backup` 变成"只读兼容字段"，`R3RSyncDrivingOrders` 里"把索引写回别人表"的语义冲突消失，但要复核它是否还在读 `orders_backup` 判 `borrowed`。

**步骤 3 验收**：挂车/解挂各跑一次，`v->orders` 全程不变（日志打 `ORDER-HONEST veh= order=`）；`r3r_orders_borrowed` 与 `IsR3RFollower()` 一致。

### 步骤 4：旧兜底降级 + 统一编译 + 验收

- `DeleteVehicleOrders()` 的 `r3r_borrow_is_shared` 例外与 `R3RFindOrderListReferrer()` **保留不删**（旧档中间态仍需自愈），只改注释标注"方案 D 下为死代码"。
- `OrderList::FreeChain` 的移交分支同样保留，加注释。
- 统一编译（`_tmp_inc_build.cmd`，全量约 20 分钟），三件套自证。
- 实机验收判据：
  1. 挂车后 `v->orders` 仍指向自己的表（日志/`dump_vehicle` 可证）；
  2. 不再出现 `num_vehicles` 与实际不一致导致的 `:999`/`:1001`；
  3. 挂车后列车确实执行 donor 的表（到站/订单推进正确）；
  4. 解挂后排程正确回到自己（每半拿各自的）；
  5. 死车不再残留"自己那张没人管"的表；
  6. 存档往返跟随关系不丢；
  7. 负判据：无跟随的普通列车行为逐字不变。

**未覆盖/风险**：`R-2`（`current_order` 一帧闪旧）、`R-4`（借用期索引冻结逻辑仍需保留）、`R-6`（克隆是否复制跟随待定）。

## 十六、执行进度（2026-10-24，四步改码中；纪律=全部改完才编译）

| 项 | 状态 | 落点 | 数量/说明 |
| --- | --- | --- | --- |
| 1.1 字段 | **已改** | `src/vehicle_base.h`（`OrderList *orders` 之后） | 新增 `r3r_follow_orders` / `r3r_follow_owner` |
| 1.1b 访问器 | **已改** | 同上（字段紧邻） | `GetEffectiveOrders()`（const，返回非 const `OrderList *`）/ `IsR3RFollower()` |
| 1.2 存档 | **已改** | `src/sl/couple_group_sl.cpp::_r3r_park_desc` | `NSL("follow_orders", SLE_REF(...REF_ORDERLIST))` + `NSL("follow_owner", SLE_VAR(...SLE_UINT32))` |
| 1.2b `R3RHasParkState` | **待改** | 同文件 | 追两条判据，否则跟随关系不写出 |
| 2 批量替换（纯读取批） | **已改 16 文件 / 176 处** | `schdispatch_cmd.cpp` 47、`departures.cpp` 28、`order_gui.cpp` 20、`schdispatch_gui.cpp` 17、`linkgraph/refresh.cpp` 17、`timetable_gui.cpp` 16、`timetable_cmd.cpp` 9、`script/api/script_order.cpp` 8、`viewport.cpp` 4、`vehicle_gui.cpp` 4、`order_dest_func.h` 2、`order_serialisation.cpp` 2、`tracerestrict.cpp` 1、`script/api/script_vehicle.cpp` 1 | 口径=`->orders->` → `->GetEffectiveOrders()->`，`replace_all` |
| 2 批量替换（剩余 57 处） | **待改** | `vehicle.cpp` 10、`vehicle_base.h` 10、`order_cmd.cpp` 34、`train_cmd.cpp` 3 | **必须逐条判定**（含所有权语义：销毁/共享登记/借用归还） |
| 2 排除 | **确认不改** | `src/sl/*`（`vehicle_sl.cpp` 5、`order_sl.cpp`、`afterload.cpp` 1）、`src/order_backup.cpp` 2 | 存档/备份必须用 `orders` 本体 |
| 3 五条边 | **待改** | `Couple()` / `DecoupleTrain()` / `R3RSyncChainAfterDepotEdit()` / `CloneVehicle()` / 读档 | 只设/清 `r3r_follow_orders`，不动 `orders` |
| 4 兜底降级 + 编译 | **待做** | `order_cmd.cpp` 注释 + `_tmp_inc_build.cmd` | 改 `vehicle_base.h` ⇒ 护栏必判全量（约 692 步） |

**已确认的替换安全性**：`->orders->` 不会误命中 `->orders_backup->`（`->orders` 后接 `_` 而非 `-`），也不会命中 `->orders =` / `->orders ==` / `->orders !=` / `->orders != nullptr` 这些所有权写法。⇒ 批量替换天然只覆盖"解引用读取"，所有权路径不受影响。

## 十七、实施收口（2026-10-24）

### 17.1 四步落地结果

| 项 | 状态 | 落点 | 说明 |
| --- | --- | --- | --- |
| 1.1 字段 | 完成 | `src/vehicle_base.h`（`orders` 之后） | `OrderList *r3r_follow_orders = nullptr;`、`VehicleID r3r_follow_owner = VehicleID::Invalid();` |
| 1.1b 访问器 | 完成 | 同上 | `GetEffectiveOrders()`、`IsR3RFollower()`；`GetNumOrders/GetNumManualOrders/GetOrder/GetIndexOfOrder/Orders()` 全部已走 effective |
| 1.2 存档 | 完成 | `src/sl/couple_group_sl.cpp` | R3VP 增 `follow_orders`(REF_ORDERLIST) / `follow_owner`(SLE_UINT32)，`R3RHasParkState()` 同步加两条判据 |
| 1.2b 读档重建 | 完成 | `src/train_cmd.cpp::R3RRebuildCouplePriorities` | 有 `r3r_follow_orders` 时按 `r3r_follow_owner` 重建 owner/priority（老档走原 route A 推断，二者互不干扰） |
| 2 批量读取替换 | 完成 | 16 文件 / 176 处 + 逐条判定的 57 处 | 见 17.2 |
| 3 五条边 | 完成 | `Couple()` / `DecoupleTrain()` / `R3RSyncChainAfterDepotEdit()` / 卖车换头 / 读档 | 只设/清跟随，不再交换 `orders` 指针 |
| 4 兜底 | 完成 | `order_cmd.cpp` 的 `R3RFindOrderListReferrer` + `OrderList::FreeChain` | 见 17.3 |

### 17.2 逐条判定后的落点（只列有语义变化的）

- **`Couple()`**：`v` 不再 `v->orders = u->orders`；改为 `v->r3r_follow_orders = u->GetEffectiveOrders(); v->r3r_follow_owner = ...; v->r3r_orders_borrowed = true;`，`v` 自己的表留在 `v->orders`（KD-02 口径：表不被搬走，只是"谁在执行它"变了）。
- **`DecoupleTrain()`**：两半各自 `R3RSyncDrivingOrders(..., false)` + `R3RReturnBorrowedOrders()`，解出的那半若本来无表则按原逻辑插入 `WAIT_COUPLE`（门禁用 `GetEffectiveOrders()`，不会因"跟随者自己的空表"误判成有排程）。
- **`R3RSyncDrivingOrders()`**：借用期索引继承/回写改读写 `r3r_follow_orders` 指向的表；`R3RPushProgressToOwner()` 的"同一张表"判据改为 effective 相等（跟随者与 owner 现在必然 effective 相等）。
- **`R3RSyncChainAfterDepotEdit()`**：拖动块的排程交接改为"搬跟随指针"，只有真正 `orders` 持有者才搬 `orders` + `AddToShared`。
- **卖车/换头**（`train_cmd.cpp` ~4303）：链头是跟随者时把跟随指针交给 `new_head`，再 `DeleteVehicleOrders(first)`，不会再把被跟随表误当自己的表释放。
- **车辆被吸收**：跟随者被并入别链时先 `orders = nullptr` 再 `FreeChain(false)`，避免留下"无车引用但已分配"的表（ORDL 存档后 `Initialize()` 永不执行那族 bug 的直接来源）。
- **`src/linkgraph/refresh.cpp`**：分段运费刷新的"是否换了排程"判据改为比较 `GetEffectiveOrders()`。
- **`src/sl/vehicle_sl.cpp` R3RDUMP**：`ordCount` 与 `L-ORD` 明细改从 `GetEffectiveOrders()` 输出（跟随者自身 `orders` 是闲置表）。

### 17.3 收口审计中新发现并修掉的缺陷

1. **索引调整环选错（真实缺陷）**：`InsertOrder` / `DeleteOrder` / `MoveOrders` 原写成"`eff_orders->first_shared != nullptr` 才用被跟随表的环，否则退回 `v->FirstShared()`"。当被跟随表是**非共享表**（`first_shared == nullptr`，或 FollowChain 未登记）时，跟随者会被落进**自己那张闲置表的环**里调整一次，随后又按 `follower` 补调一次 ⇒ `cur_real_order_index` 被 ±2。现改为：`follower` 时环恒取 `eff_orders->first_shared`（可为空），空则跳过环、只补调 `v` 一次；`adjust_indices` 内改成"effective 不符就跳过"而不是硬 assert，并给环遍历加 1000 步护栏（防损坏环死循环）。
2. **`INVALID_VEHICLE` 不存在**：本代码库是 `VehicleID::Invalid()`（`PoolID<VehicleIDTag>`），`vehicle_base.h` 及 `train_cmd.cpp` / `order_cmd.cpp` / `order_backup.cpp` / `couple_group_sl.cpp` 共 13 处已全部改正（本文 §一/§二 的设计示例里写 `INVALID_VEHICLE` 只是伪代码，实际代码一律 `VehicleID::Invalid()`）。首轮全量构建停 122/692、二轮停 581/692，都是这条 `error C2065`。
3. **`OrderList::first_shared` 是 private**：`order_base.h` 的 `first_shared` 落在 private 段，外部只能走公开的 `GetFirstSharedVehicle()`。`InsertOrder` / `DeleteOrder` / `MoveOrders` 三处环起点因此改为 `eff_orders->GetFirstSharedVehicle()`（`error C2248`）。注意 `OrderList` 自身成员函数（如 `FreeChain()`）内继续用 `this->first_shared` 是合法的。
4. **`replace_all` 漏替**：同一轮里对 `r3r_follow_owner = INVALID_VEHICLE;` 用 `replace_all=true` 只改到了部分行，`const_cast<Train *>(u)->` / `v->` / `src->` / `chain->` 四个前缀变体仍残留 ⇒ 编译期才暴露。**教训：批量替换后必须用 `search_content` 复核残留计数归零，不能假定 replace_all 已覆盖全部变体。**
5. **保持不动（逐条判定结论）**：
   - `R3RSegmentOwnOrderList()` 仍返回**自己的** `orders`（先 `orders` 后 `orders_backup`）——它的用途是"这段自己的排程"，跟随者自己的闲置表正是要找的东西，调用点（自动记录区间 `R3RAutoRecordChainOrders`）另有 `eff == own` 闸门。
   - `train_cmd.cpp` ~7289 的"链头无表 ⇒ 从别的段找同指针 owner"是**老档 route A 的推断**，只在 `r3r_follow_orders == nullptr` 时可达，保留。
   - `u_inherited_running = (u->orders == nullptr)` 保持——它只与 `u->GetEffectiveOrders() == nullptr` 组成合取门禁使用，语义上是"u 本来就没有自己的表"。
   - `src/sl/*` 与 `order_backup.cpp` 的 `orders` 读写（存档/备份）继续用本体指针。

### 17.4 构建自证（2026-10-24）

改了 `src/vehicle_base.h` ⇒ 按 KI-183 护栏必须全量。共三轮，前两轮都被编译错误拦下，第三轮收口：

| 轮次 | 时长/进度 | 结果 | 原因 |
| --- | --- | --- | --- |
| 1 | 22:55:09 启动，122/692 停 | `EXIT_CODE=1` | 5 条 `error C2065/C3861`：`INVALID_VEHICLE` 不存在（见 17.3-2） |
| 2 | 22:59:00 启动，581/692 停 | `EXIT_CODE=1` | 5 条 `error C2248`：`first_shared` 是 private（见 17.3-3） |
| 3 | 23:20:31 增量补齐 + 23:26:00 重编 train_cmd | `EXIT_CODE=0` | `[26/26] Linking CXX executable openttd.exe`，`error C*/fatal error/FAILED:/build stopped` 计数 0 |

第三轮为什么是增量而不是全量：第二轮已把 581 个 obj 按**修正后的**头文件重编完，`vehicle_base.h`(22:58:33) 早于最新 obj，护栏正确判 `GUARD: incremental is safe`；只剩尾部 111 步 + `order_cmd.cpp` / `train_cmd.cpp` 重编 + 链接。

时间戳核对（全部成立，产物不陈旧）：

- `src/train_cmd.cpp` 23:25:46 → `train_cmd.cpp.obj` 23:26:19 → `build\openttd.exe` 23:28:54（52 320 768 B）
- `src/order_cmd.cpp` 23:20:05 → `order_cmd.cpp.obj` 23:20:50 → 同一 exe
- `src/vehicle_base.h` 22:58:33 早于全部 obj ⇒ 头文件改动确实进入了每个 obj
- `build\R3R_incbuild.done` 23:29:09 = `EXIT_CODE=0`
- `read_lints`：`order_cmd.cpp` / `train_cmd.cpp` / `vehicle_base.h` / `linkgraph\refresh.cpp` / `sl\vehicle_sl.cpp` / `sl\couple_group_sl.cpp` / `couple_group.cpp` / `order_backup.cpp` 全部 0 条

状态＝**已实现 + 已编译**，游戏内复测待做。本轮没有新增日志字面量（`GetEffectiveOrders` 是函数名，不进 exe 明文），故不做 exe 串核对。

#### 17.4.1 事后独立复核（2026-10-24 23:29 之后，纯只读）

用与构建脚本无关的只读命令重验一遍，全部成立：

| 项 | 实测 | 判定 |
| --- | --- | --- |
| `build\R3R_incbuild.done` | 内容 `EXIT_CODE=0`，mtime 23:29:09 | 通过 |
| 日志尾 | `[26/26] Linking CXX executable openttd.exe` | 通过 |
| 日志错误 | `error C*/fatal error/FAILED:/ninja: error/build stopped` 命中 **0 行** | 通过 |
| `R3R_incbuild.guard.log` | `GUARD: incremental is safe (no header/lang file is newer than the newest object)` | 通过（第 3 轮合法增量） |
| `build\openttd.exe` | 23:28:54，52 320 768 B | 晚于全部源码/obj |
| 最新源码 | `train_cmd.cpp` 23:25:46（其后无更晚的 src 文件） | 早于 exe |
| 最旧 obj | `script_accounting.cpp.obj` 22:59:59 | **晚于** `vehicle_base.h` 22:58:33 |
| 关键 obj | `train_cmd.cpp.obj` 23:26:19 / `order_cmd.cpp.obj` 23:20:50 / `couple_group_sl.cpp.obj` 23:09:49 / `vehicle_sl.cpp.obj` 23:11:20 / `refresh.cpp.obj` 23:04:17 | 均晚于各自 .cpp |
| obj 总数 | `build\` 下 620 个 | 与护栏 `REMOVED 620 object file(s)` 逐数吻合 ⇒ 第二轮失败未留下缺失 obj |
| `read_lints` | 6 个文件 0 条 | 通过 |

结论：本轮产物**无陈旧 obj、无残留编译错误**，构建自证收口。剩下的唯一未完成项是游戏内复测（7 条判据见 KI-465）。
