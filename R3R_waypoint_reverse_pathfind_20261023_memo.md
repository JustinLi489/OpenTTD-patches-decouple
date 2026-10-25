# 路点订单带 Reverse 时寻路器的表现（2026-10-23，设计咨询取证）

## 〇、本轮性质

玩家问：「路点订单带 reverse 的时候，寻路器又会怎么表现」。

属设计咨询，不是 bug 报告。**本轮未改任何源码、未构建。**

## 一、待验证问题清单

- Q1 `OrderWaypointFlag::Reverse` 会被哪些层读到？（寻路层 / 运动层 / 订单推进层）
- Q2 YAPF 的 `SetDestination()` 是否感知 Reverse？（决定「目的地」是否改变）
- Q3 带 Reverse 的路点，终点格是否从「最近可达格」变成「远端格」？
- Q4 列车到达带 Reverse 的路点后，掉头由谁执行、何时执行？
- Q5 掉头后的下一段寻路是否在同一次寻路里算完（即是否额外多一次寻路）？
- Q6 `YapfTrainCheckReverse()` 与订单 Reverse 是否同一件事？（易混淆点）

## 二、已知事实（上一轮已取证，见 R3R_wait_order_truncation_memo.md §七）

- `train_cmd.cpp:662-663`：`OT_GOTO_WAYPOINT` 且 `GetDestination()==station_id`
  ⇒ `osl = OrderStopLocation::FarEnd`（**停车位置取远端**，普通站台默认 `Middle`）。
- `train_cmd.cpp:1118-1121`：订单带 `OrderWaypointFlag::Reverse`
  ⇒ `limit_advisory_speed(item.start + v->gcache.cached_total_length, 0, item.z_pos)`
  ⇒ 把「车头抵达路点远端 + 整列车长」处限速压到 0；`if (order->IsWaitTimetabled()) last_station_visited = st;`
- `yapf_destrail.hpp:227-240`：`OT_GOTO_WAYPOINT` 用
  `CalcClosestStationTile(dest, moving_front->tile, StationType::RailWaypoint)`。
- `train_cmd.cpp:13988-13991`：普通路点进格即 `ProcessOrders()`。

## 三、取证记录（每条带行号锚点）

### 3.1 寻路层：Reverse **不改变「去哪」**

`yapf_destrail.hpp:227-240` 的 `SetDestination()` 只读「订单类型 + `GetDestination()`」，
**从不读 `GetWaypointFlags()`** ⇒ 带不带 Reverse，终点都是
`CalcClosestStationTile(dest, moving_front->tile, StationType::RailWaypoint)`
（离车最近的可达路点格）。搜索空间、路径选择与普通路点订单完全一致。

### 3.2 寻路层唯一的 Reverse 感知：代价里加一笔「掉头位被占」罚分

`yapf_costrail.hpp:704-747`（多格路点终点处理）：

```
} else if (cur.tile_type == TileType::Station && IsRailWaypoint(cur.tile)) {
	if (v->current_order.IsType(OT_GOTO_WAYPOINT) &&
			GetStationIndex(cur.tile) == v->current_order.GetDestination() &&
			!Waypoint::Get(...)->IsSingleTile()) {
		... 沿空轨 follow 最多 20 格找 IsSafeWaitingPosition ...
		if (td == INVALID_TRACKDIR || !IsSafeWaitingPosition(...) || !IsWaitingPositionFree(...)) {
			extra_cost += Yapf().PfGetSettings().rail_lastred_penalty;
		}
		if (v->current_order.GetWaypointFlags().Test(OrderWaypointFlag::Reverse) && HasStationReservation(cur.tile)) {
			extra_cost += Yapf().PfGetSettings().rail_pbs_station_penalty * 4;
		}
	}
	end_segment_reason.Set(EndSegmentReason::Waypoint);
```

⇒ 带 Reverse 且该路点格**已被人整格预留**（`HasStationReservation`）时，代价额外加
4 倍 `rail_pbs_station_penalty`。语义：掉头要在该格停住整个车长，占着的地方不能选。
结果 = **多格路点时会优先挑没被预留的那一格 / 另一个站台**，但这是软代价，不是硬禁止。

### 3.3 运动层：Reverse 靠 `reverse_distance` 计数器实现「整列车过线才掉头」

`src\station_cmd.cpp:4593-4606`（`VehicleEnterTile_Station`）：

```
if (consist->current_order.IsType(OT_GOTO_WAYPOINT) && consist->current_order.GetDestination() == station_id && consist->current_order.GetWaypointFlags().Test(OrderWaypointFlag::Reverse)) {
	/* Reverse at waypoint. */
	if (consist->reverse_distance == 0) {
		consist->reverse_distance = consist->gcache.cached_total_length;
		if (consist->current_order.IsWaitTimetabled()) {
			... UpdateVehicleTimetable / last_station_visited
			consist->current_order.MakeWaiting();
			consist->current_order.SetNonStopType(ONSF_NO_STOP_AT_ANY_STATION);
			return {};
		}
	}
}
```

⇒ 车头刚进路点格时把 `reverse_distance` 置为**整列车长度**，含义是「还要再走一个车长才掉头」，
即等车尾也通过路点。带等待时刻表时先转 `OT_WAITING`，掉头推迟到等待结束。

`src\train_cmd.cpp:14127-14131`（**更正：这不在 `TrainLocoHandler`，而在 `CheckTrainStayInDepot()`，
函数开头 :14095-14098 先判「不在库里就 bail」，故只影响停在车库里的列车**）：

```
if (v->reverse_distance > 0) {
	v->reverse_distance--;
	if (v->reverse_distance == 0) SetWindowWidgetDirty(...);
	return true;          // 库里这节车本 tick 不处理
}
```

⇒ **上一轮把这里写成「TrainLocoHandler 早退点 / 出车路上也不动」是误读**，见
`R3R_waypoint_stop_head_round_20261023_memo.md` §五。行驶中 `reverse_distance` 的真正递减点是
`DecreaseReverseDistance()`，由 `TrainController` 每前进一格调用（:19252 / :19287），且 `rd > 1` 才递减。

`src\train_cmd.cpp:18528-18540` `ReversingDistanceTargetSpeed()`：按剩余 `reverse_distance`
反算目标速度（真实制动 = `GetRealisticBrakingSpeedForDistance(stats, rd-1, 0, 0)`；
原版 = `(rd-1)*10-5` 或 `(rd-1)*5/2`）⇒ 让车**正好走完车长时降到 15 以下**。
同一上限还作用于：`train_cmd.cpp:1315-1317`（advisory max speed）、
`vehicle.cpp:4424`（速度更新不再加速）。

**唯一真正掉头的点** `src\train_cmd.cpp:18634-18637`：

```
if (reverse && first->reverse_distance == 1 && (first->cur_speed <= 15 || !first->UsingRealisticBraking())) {
	/* Train is not moving too fast and reversing distance has been reached */
	goto reverse_train_direction;
}
```

### 3.4 预留在 Reverse 上的硬要求：必须够一个车长

`src\train_cmd.cpp:15253-15255`（真实制动 look-ahead）：

```
if (v->reverse_distance >= 1) {
	if (v->lookahead->reservation_end_position >= v->lookahead->current_position + v->reverse_distance - 1) { _r3r_le_reason = 4; return true; }
}
```

⇒ 预留终点必须比当前位置多出 `reverse_distance-1`，否则按 line end 处理。
即：**掉头订单的预留必须包含整列车长度**。

### 3.5 订单推进

- `train_cmd.cpp:13988-13991`：路点进格即 `ProcessOrders()`（普通路点路径）。
- `train_cmd.cpp:15214-15225`（`AdvanceOrdersFromLookahead`）：带 Reverse 的路点只额外
  打 `CTTLASF_REVERSE_FOUND` + `state.reverse_dest`（供真实制动提前限速），
  `SwitchToNextOrder` 仍按 `GetDestination() == st` 推进。
- `train_cmd.cpp:16073-16077`（另一处 lookahead）同理，只置 `CTTLASF_REVERSE_FOUND`。

## 四、结论：带 Reverse 时寻路器的完整表现

1. **寻路仍是「一次一订单」**，终点仍是最近可达路点格；Reverse 不产生第二段预取。
2. **Reverse 只在两处影响寻路**：
   - 代价：目标格已被整格预留 ⇒ +4×`rail_pbs_station_penalty`（偏好空的站台）；
   - 约束：真实制动下预留长度必须覆盖 `reverse_distance`（≈车长）。
3. **Reverse 把「进站即推进」改成「进站后还要走完车长才换端」**：
   `reverse_distance` 从车长递减到 1，速度被压到 ≤15，然后 `TrainController` 走
   `reverse_train_direction` 真正换端。换端后订单已推进，下 tick 才为下一订单重新寻路
   ⇒ **天然不存在「滑行中就已铺出下一段预留」的问题**（与普通路点的差异正在这里）。
4. **对 R3R 的直接含义**：等待区路点若是带 Reverse 的订单，截断（把终点钉在该路点）
   必须让预留长度 ≥ 车长，否则 `15253-15255` 判 line end，车会停在半路 / 反复重试；
   而 KI-437 那套「栅栏格只当终点」的做法在 Reverse 场景下需要额外确认预留长度。

## 五、疑点与未取证项

- U-1（**待验证**）：`train_cmd.cpp:13980` 的守卫
  `HasStationTileRail(...) && ShouldStopAtStation(consist, st, IsRailWaypoint(tile))` 对
  「带 Reverse 但无 stop/等待」的路点订单返回真还是假？
  为真 ⇒ `CheckNextTrainTile` 直接 return，不做边缘续订；为假 ⇒ 落到 `13988` 分支推进订单。
  本轮**未取证 `ShouldStopAtStation` 的内部判据**，故「带 Reverse 的路点是否也走 13990 那条
  ProcessOrders」不作断言。
- U-2（**待验证**）：`YapfTrainCheckReverse()`（`yapf_rail.cpp`）与订单 `OrderWaypointFlag::Reverse`
  是否有关联。本轮零取证，不排除「同名不同物」——前者是路径末端的换向判定，后者是订单属性。
- U-3：`OT_WAITING` 撞上 `reverse_distance>0` 时的先后次序（14113-14115 在 14127 之前
  ⇒ OT_WAITING 直接 return，`reverse_distance` 不递减）。带等待时刻表的路点掉头是否会因此
  卡住，未取证。
