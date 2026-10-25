# 用 OT_WAITING 能否截断预留？（设计可行性）

日期：2026-10-23
问题（玩家原话）：「通过选择性地让列车执行 OT_WAITING 能不能达到截断预留的效果？我主要是想要实现让列车在等待区路点等待。」
性质：设计咨询。本轮未改源码、未构建。

---

## 一、机制事实（代码锚点）

### 1. 预留不是订单层产生的，是寻路层产生的

`TryPathReserve()`（train.h:105 内联 → `TryPathReserveWithResultFlags`，train_cmd.cpp:16285）
是**唯一的订路入口**，两条调用路径：

- 移动分支：`TrainLocoHandler` 的 `j >= adv_spd` → `TrainController()` → 内部 TryPathReserve；
- 订单推进：`ProcessOrders()` / `UpdateOrderDest()`。

一次 TryPathReserve 会把 PBS 预留**一路铺到本次寻路的终点**（订单目的地或安全位），
不是逐格铺。⇒ 「截断」只能作用在**寻路的终点选择**上，订单类型本身不决定预留长度。

### 2. 「停住」不自动等于「不订路」，更不等于「收回已订的」

- 停驶分支（`j == 0 < adv_spd`）根本不进 TrainController，所以**本 tick 不订新路**；
- 但已经铺出去的预留**不会因为停车而缩回**，只能等列车开过去逐格释放，
  或由 KI-345/KI-346 的 GC/审计清理。
  ⇒ 这解释了现场「幽灵预留先发出去、过一会儿才被销毁」。

### 3. 现有两个"拒绝订路"门（都在 TryPathReserve 入口）

- `R3RIsParkedWaiting()`（train_cmd.cpp:16261，调用点 :16327）：覆盖三形态
  - `parked-waiter`：`OT_WAIT_COUPLE`（**KI-208 的 station-tile 限制已在 W-4' 取消**，
    路点格与普通轨道也算，见 :16239-16241 注释）；
  - `parked-caronly`：纯车底编组；
  - `parked-no-target`：GOTO_COUPLE 已到目的地但无配对锁。
  命中 ⇒ 直接 `return TPRRF_NONE`（打 `RESV-NOBOOK`）。
- `R3RShouldHoldForCouple()`（:13069，调用点 :16347）：等待区路点栅栏命中 ⇒ 拒绝订路
  （`CPL-HOLD-NOBOOK`）。

### 4. OT_WAITING 的实际行为

- `:14113-14115` `if (v->current_order.IsType(OT_WAITING)) return true;` ⇒ 列车原地停住。
  这一条与 `OT_WAIT_COUPLE`（:14120-14122）完全同构。
- 但 `OT_WAITING` **不在** `R3RIsParkedWaiting()` 的三形态里 ⇒ 它只挡住移动分支，
  **挡不住 ProcessOrders/UpdateOrderDest 的订路**；
- 且 `OT_WAITING` 是原生"站内待避"订单：自身无目的地，进入/退出由时刻表等待
  （`IsWaitTimetabled` :14110）或装载条件决定，**没有"等某个外部条件就绪再走"的语义**。

## 二、结论

**「OT_WAITING 能不能截断预留」→ 部分能，但不是做这件事的正确工具，而且不够。**

| 想要的效果 | OT_WAITING | OT_WAIT_COUPLE | 等待区路点栅栏（现有） |
|---|---|---|---|
| 车停在原地 | 能（:14113） | 能（:14120） | 能（清速 :21093） |
| 停车后不再订路 | 部分（只挡移动分支） | 能（:16327 waiter 门） | 能（:16347 hold 门） |
| **开往路点途中就把预留截在路点** | **不能** | **不能** | 能（YAPF 终点限制，:13119-13156） |
| 条件满足自动放行 | 不能（只认时间/装载） | 能（配对锁解除） | 能（`R3RGetCouplePairPartner` 非空） |

关键点：用户真正要的「截断预留」发生在**列车还没到路点的时候**（那时订单是
GOTO_WAYPOINT / 下一站，YAPF 已经按终点把整段铺出去了）。此时把订单改成 OT_WAITING
没有用——订单类型改不了这次寻路的终点。

## 三、现成方案（推荐直接用）

R3R 已有「等待区路点」能力（第 276/277 轮，KI-429 / KI-437）：

排程写成 **`GOTO_WAYPOINT(等待区路点)` → `GOTO_COUPLE(目标车底)`**，栅栏判据
（`R3RShouldHoldForCouple` :13069）：

1. 当前订单是 `OT_GOTO_COUPLE`；
2. **前一条真实订单是 `OT_GOTO_WAYPOINT`**（:13085）；
3. 运动前端站在该路点格上（:13087-13090）；
4. 还没有配对锁（:13093）。

⇒ 未到路点：挂接寻路终点被截断为路点（`R3RWaitCutResult` 返回 1）；
⇒ 停在路点上未就绪：返回 2，无合法终点，原地等；
⇒ 配对就绪：返回 0，放行，预留才铺向车底。

这正是「让列车在等待区路点等待」的实现，且是**双向 PBS 式精准拦停**。

## 四、若要"与挂接无关的通用路点等待"

现有栅栏与 `OT_GOTO_COUPLE` 强绑定（:13072）。要通用化需要仿写两处，缺一不可：

1. **YAPF 终点限制**：把 `R3RWaitCutResult` 的截断口径从"仅 GOTO_COUPLE"放宽到
   "任意订单前一条是 GOTO_WAYPOINT 且该路点被标记为等待区"（给 Order 加个标志位，
   或复用路径点选项）；
2. **拒绝订路 + 清速**：把 `R3RShouldHoldForCouple` 同结构的判据挂到
   `TryPathReserveWithResultFlags`（:16347 处）与 `TrainLocoHandler` 清速处（:21093）。

只做 2 不做 1 ⇒ 车到了路点会停，但**开过去的路上预留已经铺过头**（就是当前现象）；
只做 1 不做 2 ⇒ 停在路点后会由 ProcessOrders 重新订出去。

## 五、未确认项

- U-1：`OT_WAITING` 能否被安全地"手工塞入"正在跑的订单链（订单机状态、时刻表联动、
  `HandleWaiting` 的前置）——未取数，不建议这么做。
- U-2：把 `OT_WAITING` 追加进 `R3RIsParkedWaiting()` 的形态表是否能挡住 ProcessOrders
  侧的订路——未验证；但即便能，也解决不了"途中已铺出去"的问题（见 §二）。

---

## 六、引擎寻路/订单粒度的事实（2026-10-23 取证）

问题：「A站→B站→C站，寻路器是否先只寻 A-B、在 B 截断，等执行完去 B 的命令后才寻 B-C？」

### 6.1 订单粒度：是，一次寻路只为一个订单

`src\pathfinder\yapf\yapf_destrail.hpp:220` `SetDestination(const Train *v)` 按
`v->current_order.GetType()` 分支给出终点：

- `OT_GOTO_WAYPOINT` / `OT_GOTO_STATION`：`CalcClosestStationTile(v->current_order.GetDestination().ToStationID(),
  v->GetMovingFront()->tile, StationType::Rail|RailWaypoint)`（:227-240）
  ⇒ 终点是**该订单站/路点的最近可达格**，不是"下一个订单"。
- `OT_GOTO_DEPOT`：库分支（:244 起），或 `ODATFB_NEAREST_DEPOT` 走 `any_depot`。
- 其它类型（`OT_GOTO_COUPLE` 等）各走自己的分支。

⇒ YAPF 里没有任何"跨订单预取"逻辑：到 C 的路径不会在 A→B 那次寻路里出现。

### 6.2 但订单推进时机是「进格」，不是「停稳」

`src\train_cmd.cpp:13990`（`TrainCheckIfLineEnds` 一族）：

```
if (IsRailWaypointTile(moving_front->tile) && GetStationIndex(moving_front->tile) == consist->current_order.GetDestination()) ProcessOrders(consist);
```

⇒ 路点订单在运动前端**刚进入路点格**就推进订单；车站订单同理由 `TrainEnterStation` 触发。
此时列车通常还在滑行（`cur_speed != 0`），下一 tick 移动分支就会为新订单（B→C）
调 `TryPathReserve`。**这就是"车还没停稳、预留已经越过 B 铺向 C"的直接原因**，
也是 C4-1' 必须清 `cur_speed`（:21093）而不只是改订单类型的原因。

### 6.3 预留目标 ≠ 订单终点的另一层：安全等待位

`src\pathfinder\yapf\yapf_rail.cpp:95-110`：

```
TileIndex res_dest_tile;   ///< The reservation target tile
TileIndex res_fail_tile;   ///< The tile where the reservation failed
bool FindSafePositionProc(TileIndex tile, Trackdir td)
{
    if (IsSafeWaitingPosition(Yapf().GetVehicle(), tile, td, true, !TrackFollower::Allow90degTurns())) { ... return false; }
}
```

配套 `SetReservationTarget()`（:397）。⇒ "能停在哪"由 `IsSafeWaitingPosition()` 单独判定，
与订单类型无关；订单只回答"要去哪"。

### 6.4 结论

可截断的位置只有两个层面：

1. **去哪**：`SetDestination()` 算出的 `dest_tile`（改这里 = 把寻路终点强行改成等待区路点）；
2. **能不能走**：`TryPathReserveWithResultFlags()` 入口拒绝订路（R3R 现有两道门的做法）。

订单类型（`OT_WAITING` / `OT_WAIT_COUPLE`）**不属于这两层中的任何一层**，它只影响
`ProcessOrders` 是否推进与 `TrainLocoHandler` 是否进移动分支（见 §一.4 / §二）。

---

## 七、A站 → B路点 → C站 是不是分两次寻路（2026-10-23 取证）

结论：**是，仍然是每订单一次**；但路点这一跳的「推进时机」比站台更早，
而且第二段（B→C）不是被 `ProcessOrders` 发起的一次性寻路，而是被
`CheckNextTrainTile()` 的**边缘续订**发起的。

### 7.1 关键函数：`CheckNextTrainTile`（不是 TrainEnterStation）

`src\train_cmd.cpp:13966`：

```
static void CheckNextTrainTile(Train *moving_front)
{
	/* Don't do any look-ahead if path_backoff_interval is 255. */
	if (_settings_game.pf.path_backoff_interval == 255) return;
	if (moving_front->track == TRACK_BIT_DEPOT) return;
	Train *consist = moving_front->First();
	/* Exit if we are currently in a waiting order */
	if (consist->current_order.IsType(OT_WAITING)) return;
	/* Exit if we are on a station tile and are going to stop. */
	if (HasStationTileRail(moving_front->tile) && consist->current_order.ShouldStopAtStation(consist, GetStationIndex(moving_front->tile), IsRailWaypoint(moving_front->tile))) return;

	switch (consist->current_order.GetType()) {
		...
		case OT_GOTO_WAYPOINT:
			/* If we reached our waypoint, make sure we see that. */
			if (IsRailWaypointTile(moving_front->tile) && GetStationIndex(moving_front->tile) == consist->current_order.GetDestination()) ProcessOrders(consist);
			break;                                                  // :13988-13991
	}
	Trackdir td = moving_front->GetVehicleTrackdir();
	...
	if (!HasReservedTracks(ft.new_tile, TrackdirBitsToTrackBits(ft.new_td_bits))) {
		/* Next tile is not reserved. */                            // :14012 起才是续订
```

⇒ 路点订单的 `ProcessOrders()` 是在**运动前端刚进入路点格**时被调用的，
注释自陈同一口径（:13077-13080「the waypoint is switched away as soon as the
head enters its tile, see :13656」）。

### 7.2 站台 vs 路点的差异（三行对比）

| 项 | A站→B**站** | A站→B**路点** |
|---|---|---|
| 是否停车 | `ShouldStopAtStation()` 真 ⇒ :13980 `return`，不 look-ahead，必须停稳装货 | 普通路点 `ShouldStopAtStation()` 假 ⇒ 落到 :13988 分支，**进格即推进订单** |
| 何时发起 B→C 寻路 | 停稳后 `ProcessOrders` → 下一 tick 移动分支 `TrainController` → `TryPathReserve` | **车还压在 B 格、仍在滑行**时 `CheckNextTrainTile` 就续订（若下一步预留不覆盖） |
| 寻路终点 | `CalcClosestStationTile(dest, moving_front->tile, StationType::Rail)` | 同函数但 `StationType::RailWaypoint`（`yapf_destrail.hpp:238-240`） |

### 7.3 路点什么时候才真的停车

`src\train_cmd.cpp:1118-1121`（速度限制 look-ahead）：

```
} else if (order->IsType(OT_GOTO_WAYPOINT) && order->GetDestination() == st && order->GetWaypointFlags().Test(OrderWaypointFlag::Reverse)) {
	limit_advisory_speed(item.start + v->gcache.cached_total_length, 0, item.z_pos);
	if (order->IsWaitTimetabled() last_station_visited = st;
```

⇒ 只有路点订单带 **Reverse**（或带 stop/等待时刻表）时才把限速压到 0；
普通路点是"路过即过"，不停车、不装货。

### 7.4 多格路点的额外分支

- `yapf_destrail.hpp:227-236`：`OT_GOTO_WAYPOINT` 且 `!IsSingleTile()`
  ⇒ `Yapf().DisableCache(true)`（complex waypoint 的 look ahead 会污染缓存）。
- `yapf_costrail.hpp:704-715`：路点作为终点时按「最后一个信号为红」处理，
  以免"终点前有信号"导致寻路失败。

### 7.5 对上轮 §二 的补充修正（重要）

`OT_WAITING` 的真实作用点被找到了：**它确实能挡住 `CheckNextTrainTile` 的
边缘续订**（:13977 `if (consist->current_order.IsType(OT_WAITING)) return;`）。

但这仍然**不等于**能截断预留：

1. 它挡的是「车压最后一块预留格时向前延伸」这一步；
2. 它挡不住 `ProcessOrders` / `UpdateOrderDest` 发起的订路（上一轮 §二 的口径不变）；
3. 它挡不住"进入该订单**之前**就已经铺到寻路终点的那一段"。

⇒ 结论修正为：`OT_WAITING` 能阻止「边缘续订」这一条通路，但不能阻止
「订单切换时的主动订路」和「已有预留」这两条通路。上一轮 §二 的
"只由 :14113 让车停住"表述偏窄，实际它还参与 :13977。


