# 路点截断：借用换向机制把「停车位」改到车头（2026-10-23，设计与取证）

## 〇、本轮性质

玩家提出设计需求（不是 bug 报告）：

> 「我还是想要我那个路点截断机制，能不能借助这个路点换向的机制，把停车位改为车头，
> 把速度降到 0（注意，要符合条件的列车这个命令才会被算作停止，其它的都算通过）」

**本轮先取证 + 出方案，未改源码、未构建。**

## 一、需求拆解（三句，逐句要有落点）

- R1「保留路点截断机制」：R3R 现有的"把寻路终点钉在路点、不继续往前铺"必须留着。
- R2「停车位改为车头、速度降到 0」：不要原生 Reverse 的"整列车过线（`reverse_distance = cached_total_length`）"，
  而是**车头抵达路点即停**，`cur_speed` 归零。
- R3「符合条件的列车才算停止，其它算通过」：判定必须落在**列车实例**上，
  **不能用 `OrderWaypointFlag::Reverse`** —— 那是订单级属性，凡是执行该订单的列车都会停/掉头，
  违反 R3。⇒ 标记必须挂在链头（VehicleRailFlag / R3R 段行），不能挂订单。

## 二、方案候选（待取证后定）

### S1：走「停车」通道（不碰 reverse_distance）

让被截断的列车在路点处不推进订单、转 `OT_WAITING`，速度由既有制动模型归零，
预留仍只到路点。判据在列车侧（R3R 标记），与订单无关 ⇒ 天然满足 R3。

- 需要取证：`ShouldStopAtStation()` 对路点/等待时刻表的判据；`TrainEnterStation` 停车流程；
  `GetTrainStopLocation` 能否把停车点放到"车头进格处"而不是 `OrderStopLocation::FarEnd`。

### S2：复用 `reverse_distance` 的「速度剖面」，但终点动作改为停车

`ReversingDistanceTargetSpeed(rd)`（`train_cmd.cpp:18528-18540`）给出"让车在距离 rd-1 处停住"的速度
⇒ 若把 rd 设成"车头到路点的剩余像素"，就能精确停在车头进格处。
然后把 `train_cmd.cpp:18634-18637` 的 `goto reverse_train_direction` 换成"停车（OT_WAITING）"。

- **前提（必须先证实）**：`train_cmd.cpp:14127-14131` 里 `if (reverse_distance > 0) { rd--; return true; }`
  到底在哪个函数、什么条件下执行。若它真的"只要 rd>0 就不动"，S2 根本不可能产生速度剖面，
  **上一轮备忘与 KI 里那句「每 tick 递减 1 并 return true（不寻路不移动）」就可能是误读，必须更正**。

## 三、待验证问题（U）

- U-1：`train_cmd.cpp:14127` 所在函数名与调用条件（决定 S2 可行性）。
- U-2：`reverse_distance` 正常递减的驱动点是 `DecreaseReverseDistance()`（:18520）还是 :14128。
- U-3：`ShouldStopAtStation()` 的内部判据（路点 + stop 标志 / 等待时刻表），决定 S1 需不需要新造通道。
- U-4：`GetTrainStopLocation()` / `OrderStopLocation` 对路点能否给出"车头即停"的 stop 位置。

## 四、取证记录（逐条追加，带行号）

### 4.1 【U-1 已解，且推翻上一轮表述】14127 那段在 `CheckTrainStayInDepot()` 里

`train_cmd.cpp:14095-14098` 该函数开头先 bail（不在库就返回），`:14127-14131` 的
`reverse_distance > 0 ⇒ rd--; return true;` 只作用于**停在车库里的车**。
⇒ 上一轮备忘 `R3R_waypoint_reverse_pathfind_20261023_memo.md:3.3` 与 KI 里
「`TrainLocoHandler` 早退点 / 本 tick 不寻路不移动」是**误读**，已在那边订正。

### 4.2 【U-2 已解】行驶中 rd 的递减点 = `DecreaseReverseDistance()`，由「前进一格」驱动

`train_cmd.cpp:18520` `DecreaseReverseDistance()` 内部 `if (rd > 1) rd--;`
调用点在 `TrainController` 里每跨一格各一处（:19252 / :19287）。
⇒ `reverse_distance` 是「还剩几格/几像素」的**距离计数器**，随真实位移递减，
再配合 `ReversingDistanceTargetSpeed()`（:18528）给出速度剖面 ⇒ 能精确停。
但它天生是「车头 + 整列车长」，与 R2「车头即停」**方向相反**。

### 4.3 【U-3 已解】路点停车的唯一现有判据 = 订单带等待时刻表

`order_cmd.cpp:4806-4816`：

```cpp
bool Order::ShouldStopAtStation(StationID last_station_visited, StationID station, bool waypoint) const
{
	if (waypoint) return this->IsType(OT_GOTO_WAYPOINT) && this->IsWaitTimetabled() && this->dest == station;
	...
}

bool Order::ShouldStopAtStation(const Vehicle *v, StationID station, bool waypoint) const   // :4826
{
	return this->ShouldStopAtStation(v->last_station_visited, station, waypoint);
}
```

⇒ 纯订单级，满足不了 R3（同订单的其它列车会被一起停）。
**但 :4826 是带 `v` 的重载，这就是注入「列车侧标记」的现成钩子。**

### 4.4 【U-4 已解】停车位置现在恒为 `FarEnd` = 车头停在路点格**末端**

`train_cmd.cpp:644` `GetTrainStopLocation()`：

- :647-648 路点：`*station_ahead = *station_length = TILE_SIZE`（路点只有 1 格）
- :662-663 `OT_GOTO_WAYPOINT` 且 dest 匹配 ⇒ `osl = OrderStopLocation::FarEnd`
- :665 `overhang = cached_total_length - TILE_SIZE`，必然 > 0
- :718-721 `else if (overhang >= 0) osl = OrderStopLocation::FarEnd;`（二次强制）
- :723 注释「The stop location of the FRONT! of the train」，:736-738 `FarEnd ⇒ stop = station_length`

⇒ 现在是「车头停在格末端」，也就是整列车都已通过路点。
**要「车头即停」= 改走 `NearEnd`（stop 取 0），而不是 FarEnd。**
同一套 osl 逻辑在 `:906-909 / :965-968` 还有**第二份拷贝**（另一支停靠预测函数），必须一起改，否则改一半。

## 五、对上一轮结论的更正（已执行）

`R3R_waypoint_reverse_pathfind_20261023_memo.md` §3.3 与 `R3R_KNOWN_ISSUES.md`
「路点订单带 Reverse」小节里那句「`14127-14131` 是 TrainLocoHandler 早退点、本 tick 不寻路不移动」
**作废**：它在 `CheckTrainStayInDepot()`，只管库里。行驶中的行为由 4.2 的距离计数器给出。

## 六、方案（结论：能做，但「借谁」要改口）

用户原设想「借 Reverse 的速度剖面」。取证结论：**该借的不是 `reverse_distance`，而是「路点也能停车」那条处理链**。
理由：`reverse_distance` 的语义天生是「车头 + 整列车长」（4.2），与「停车位 = 车头」正好相反；
而「路点作为停车目标」这条链（`IsWaitTimetabled()` 走通的那条）本来就把车停在路点格上、并把速度归零。

### 设计（三处改动，全部在列车侧判定，不碰订单标志）

**A. 判据：给「带 v 的重载」加列车侧标记**（`order_cmd.cpp:4826`）

```cpp
bool Order::ShouldStopAtStation(const Vehicle *v, StationID station, bool waypoint) const
{
	if (waypoint && this->IsType(OT_GOTO_WAYPOINT) && this->dest == station && R3RTrainStopsAtWaypoint(v)) return true;
	return this->ShouldStopAtStation(v->last_station_visited, station, waypoint);
}
```

一句话同时买到三个效果（都是既有代码）：
- `CheckNextTrainTile` `:13980` ⇒ `return`，**不再做边缘续订**（截断的直接强化）；
- `AdvanceOrdersFromLookahead` `:15211` ⇒ 按「要停」处理，不提前推进订单；
- `VehicleEnterTile_Station` `:4615` ⇒ 进停车流程。

**R3 由此自动满足**：标记挂在列车（链头/R3R 段行）上，同一订单的其它列车只要没这个标记，
`IsWaitTimetabled()` 为假 ⇒ 照旧 `return false` ⇒ **算通过**。

**B. 停车位 = 车头**（`train_cmd.cpp:662-663` + `:718-721`，以及第二份 `:908-909` + `:965-968`）

```cpp
osl = R3RTrainStopsAtWaypoint(front) ? OrderStopLocation::NearEnd : OrderStopLocation::FarEnd;
```
并且 :718-721 的 `overhang >= 0 ⇒ FarEnd` 必须对带标记的列车**放行**，
否则「列车长于 1 格」永远把 NearEnd 覆盖成 FarEnd，改了也不生效（`overhang` 对路点恒 > 0）。

**C. 速度降到 0**：**不需要新代码**。停车流程走到 stop 位置时 `TrainEnterStation` 自己把
`cur_speed` 归零并转 `OT_LOADING` / `OT_WAITING`。R2 的「降到 0」是这条链的既有结果。

### 与「路点截断」的关系

截断负责「预留只到路点、不往后铺」；本方案负责「到了就真停、且停在车头」。
两者互补：截断决定**算到哪**，本方案决定**停在哪**。

## 七、实现前必须确认（U-5 / U-6，未取证）

- **U-6（最关键）**：`TrainEnterStation` 对「路点 + 停车」的订单动作是 `MakeWaiting()` 还是
  `ProcessOrders()` 直接推进？若是后者，车停下来但订单已跳到下一条，`CheckNextTrainTile` 的
  截断窗口就只剩几格 ⇒ 「停住等放行」的效果不成立。**这一条决定方案是否可用，必须先看。**
- **U-5**：R3R 现有「路点截断」的实现位置与标记载体（在哪打、怎么清），
  以及新的 `R3RTrainStopsAtWaypoint()` 应复用哪个字段。上一轮备忘提到 KI-437「栅栏格只当终点」，
  但本轮未取证该条目的实现。
- ~~U-7：`NearEnd` 会让车尾仍压在路点前的格上（路点仅 1 格）……~~ ⇒ **已证伪，见 §9.5**：
  `NearEnd` 的 `stop = cached_total_length` 是「**车尾对齐格起点**」，会把车头推到路点格**之后**；
  且路点 `overhang > 0` 会把它强制覆盖回 `FarEnd`。⇒ 不能用 `NearEnd`，必须新造「车头即停」的 stop 值。

## 八、玩家口径回填（2026-10-23，U-5 / U-6 已答）

### 8.1 U-6 玩家原话与要点

> 「U6，我记得我们的 GOTOCOUPLE 命令本身就能够等待，所以可以直接 processorder 到挂接的命令去等待
> （可以先查证，论证）」

⇒ **不改 `TrainEnterStation` 的停车语义，也不新造"停在路点"状态**：
停车后照原样 `ProcessOrders()` 推进订单，把"停住"这件事交给**下一条命令自己的等待语义**
（`OT_GOTO_COUPLE` / `OT_WAIT_COUPLE`）。要求本轮先查证论证，不是先改码。

### 8.2 猜测依据（待验证，登记为 §九 的 V 系列）

- KI-348（第 242 轮）实测形态：`OT_GOTO_COUPLE` 且 `cur_speed == 0` 时 `TrainLocoHandler` 恒走停驶分支
  （`j < adv_spd`），该分支只做 `CheckTrainCollision` + `TrainCoupleHandler`，**不寻路不预留**
  ⇒ 车原地不动等着被挂。这正是玩家说的"命令本身就能够等待"。**已由 V-2（§9.2）与 W 系列旁证支持。**
- ~~KI-193（第 109 轮）的反例风险：500 tick 后自动推进订单~~ ⇒ **已证伪，见 §9.3**：
  现行代码里 `COUPLE-DEST-EMPTY` 只是心跳（`train_cmd.cpp:13389-13392` / `:13757` 明写
  `waiting in place, order kept`），`R3R_COUPLE_DEST_IDLE_LIMIT` 只控制打日志节流，**不跳单**。
- ~~`OT_WAIT_COUPLE` 作为"停住等放行"的载体比 GOTO_COUPLE 更稳~~ ⇒ **不再需要**：
  停车动作由 `TrainEnterStation` 的路点分支（§9.1）完成，等待动作由 `OT_GOTO_COUPLE` +
  停驶分支（§9.2 / KI-348）完成，两者拼起来就是"停在路点格上原地等"，无需换成 `OT_WAIT_COUPLE`。
  （若将来发现 `OT_GOTO_COUPLE` 的重试寻路会推车，再回退到这一条。）

### 8.3 U-5 玩家原话

> 「U5，我不太清楚那些函数，可能就是按照你推荐的来」

⇒ 按本备忘 §六 的方案：新判据用列车侧标记，`R3RTrainStopsAtWaypoint(v)` 复用 R3R 现有截断
（KI-437）的标记载体，不新造第二套。具体字段待 §九 取证后定。

### 8.4 U-6 论证结论（2026-10-23 取证后）：玩家判断成立，但只解决「等」，不解决「停在哪」

**结论：`OT_GOTO_COUPLE` 确实能永久等待，可以承担"停住等放行"的角色；但它管不住"车停在哪一格"。**

三段论据（全部有行号，详见 §9.1~9.4）：

1. **能等**：`ProcessOrders()` 对 `OT_GOTO_COUPLE` 只做 `UpdateOrderDest()` 就 `return false`
   （`order_cmd.cpp:4693-4703`），订单不会被自行推进；目的地无车底时只有 `COUPLE-DEST-EMPTY`
   心跳，`order kept`（`train_cmd.cpp:13389-13392`）。
2. **不动**：车停稳后 `cur_speed == 0` ⇒ `TrainLocoHandler` 走停驶分支（KI-348），
   该分支不寻路、不预留 ⇒ 原地等，且 `TrainCoupleHandler` 仍在里面跑 ⇒ 挂上就自动继续。
3. **不解决停车位置**：`OT_GOTO_COUPLE` 的停车位置是"车底所在站的站台"，
   `TrainEnterStation` 对它只限速到 20 不停止（`train_cmd.cpp:16861-16863`）。
   要"停在路点格且车头即停"，仍必须让**路点订单自己算停止**（§9.4 的 `ShouldStopAtStation`）
   并把 stop 改成车头即停（§9.5）。

⇒ 因此正确的组合是：

```
路点订单：ShouldStopAtStation 为真（本车判据） ⇒ VehicleEnterTile_Station 算 stop ⇒ TrainEnterStation
          ⇒ MakeWaiting + cur_speed = 0（§9.1，现成代码）  = 【停】
下一条：  订单推进到 OT_GOTO_COUPLE ⇒ ProcessOrders 不推进 + 停驶分支不寻路  = 【等】
放行：    栅栏条件满足 ⇒ R3RShouldCutCoupleAtWaitPoint() 返回 0 + 挂上 ⇒ 自动继续  = 【走】
```

三件事各用既有的一个机制，**不需要新造状态机**。玩家 U-6 的直觉把最难的一段（"停住不走"）
直接落到现有代码上，成立。

## 九、本轮取证任务（V 系列）

- **V-1**：`TrainEnterStation()` 对「路点 + 应停车」的订单动作，是 `MakeWaiting()` 还是直接 `ProcessOrders()`
  （决定 8.1 的口径落在哪个分支）。
- **V-2**：`ProcessOrders()` / `SwitchToNextOrder()` 对 `OT_GOTO_COUPLE`、`OT_WAIT_COUPLE` 是否推进、
  推进条件是什么（确认"推进到挂接命令后能停住"）。
- **V-3**：`OT_GOTO_COUPLE` 无目标时 500 tick 自动跳过（KI-193）的确切代码位置，评估是否影响本方案。
- **V-4**：R3R 现有路点截断（KI-437「栅栏格只当终点」）的实现函数与标记载体，供 `R3RTrainStopsAtWaypoint()` 复用。

### 9.1 V-1：`TrainEnterStation()` 对「路点到达」= `MakeWaiting()` + 清速（已取证）

`src\train_cmd.cpp:16817-16829`（`Waypoint::IsExpected(bst)` 分支）：

```cpp
16819  UpdateVehicleTimetable(consist, true);
16820  consist->last_station_visited = station;
16821  consist->force_proceed = TFP_NONE;
16823  consist->current_order.MakeWaiting();                        // → OT_WAITING
16824  consist->current_order.SetNonStopType(ONSF_NO_STOP_AT_ANY_STATION);
16825  R3RSpdCtx(consist, "wp-arrive", consist->cur_speed, 0);       // R3R 自己的速度探针
16826  consist->cur_speed = 0;
16827  consist->UpdateTrainSpeedAdaptationLimit(0);
16828  return;                                                        // 不进 BeginLoading()
```

⇒ 路点到达**已经**是「MakeWaiting + 速度清零」，现成代码，不需要新增「停在路点」状态。
⚠ 但这段只在 `ShouldStopAtStation()` 为真时才会被调用（见 9.4），所以「**谁能停**」由 `ShouldStopAtStation` 决定。

对照两支：

- `:16811` `if (consist->current_order.IsType(OT_WAIT_COUPLE)) return;` —— 完全不动。
- `:16850-16866` `OT_GOTO_COUPLE` 到达只把 `cur_speed` 限到 20（`:16861-16863`）后 `return`，**不停止**。

### 9.2 V-2：`ProcessOrders()` 对挂接类订单不推进 ⇒「推进到挂接命令就能停住」成立

`src\order_cmd.cpp:4667-4703`：

- `OT_LOADING / OT_LOADING_ADVANCE / OT_WAITING` ⇒ `return false`（`:4675-4682`）；
- `OT_DECOUPLE / OT_WAIT_COUPLE` ⇒ `return false`（`:4688-4691`）；
- `OT_GOTO_COUPLE` ⇒ 仅 `UpdateOrderDest()` 后 `return false`（`:4693-4703`）。

⇒ 订单停在 `OT_GOTO_COUPLE` 后**永不自行推进**，只有挂上（`Couple()`）才前进。

### 9.3 V-3：KI-193「500 tick 自动跳单」已废除 ⇒ §8.2 的反例风险作废

`src\train_cmd.cpp:13389-13392`（注释）与 `:13757`（日志格式串）：

```
COUPLE-DEST-EMPTY is therefore no longer an "order completed" marker but the
heartbeat of a loco waiting in place; ...
COUPLE-DEST-EMPTY loco=%d ... (waiting in place, order kept)
```

⇒ 现在它只是**心跳**：订单保留、车原地等。**§8.2「500 tick 后自动推进（KI-193）」这条反例不成立，作废。**

`R3R_COUPLE_DEST_IDLE_LIMIT = 500`（`:13392`）现在只控制心跳节流，不控制跳单。

同族旁证：`:21090-21103` 的 `hold-for-couple` 分支（`R3RShouldHoldForCouple`）直接 `cur_speed = 0; subspeed = 0`，并自己有 500 tick 心跳——它就是「等车底」的停车载体之一。

### 9.4 V-4：「该不该在这里停」的唯一闸门 = `Order::ShouldStopAtStation(const Vehicle *v, ...)`；R3R 载体 = `R3RShouldCutCoupleAtWaitPoint()`

**(a) 闸门**

- `src\station_cmd.cpp:4615`：`VehicleEnterTile_Station()` 先问
  `ShouldStopAtStation(consist, station_id, IsRailWaypoint(tile))`；为假 ⇒ 直接返回、不停车。
- `src\order_cmd.cpp:4806`（路点分支，**不带 v**）：
  `if (waypoint) return IsType(OT_GOTO_WAYPOINT) && IsWaitTimetabled() && dest == station;`
- `src\order_cmd.cpp:4826`（**带 v** 的重载）：`return this->ShouldStopAtStation(v->last_station_visited, station, waypoint);`

⇒ 带 v 的重载就是「**按车区分**」的注入点，正好满足 R3「同一订单只有该列车算停止」。
调用点：`station_cmd.cpp:4615`、`train_cmd.cpp:1258`（进站减速）、`train_cmd.cpp:16071/16128/20920`、`vehicle.cpp:4422`。

**(b) R3R 既有截断载体（KI-437 / F-437-D，第 276~277 轮）**

- `src\train_cmd.cpp:13111-13116`：

  ```cpp
  struct R3RWaitBarrierState { VehicleOrderID order_index; bool seen_on_wp; bool overshoot; };
  static std::unordered_map<uint32_t, R3RWaitBarrierState> _r3r_wbar_state;
  ```

  进程内状态、**不入档**（`:13109` 明说读档丢失最坏只是多拦一次）。
- `src\train_cmd.cpp:13157` `int R3RShouldCutCoupleAtWaitPoint(const Train *v, uint32_t *wait_wp)`，
  返回语义见 `:13154-13155`：`0 = 不截断 / 1 = 截断（本次寻路只许以等待区路点作终点）/ 2 = 本车已站在等待区路点上但还没放行（留原地等）`。
- `front_on_wp` 判据 `:13192-13194`（`IsRailWaypointTile` + `GetStationIndex == wait_dest`）；
  `overshoot` 永久失效 `:13205-13213`。
- 消费点（`src\pathfinder\yapf\yapf_rail.cpp`）：
  - `:37` 手写前向声明 `int R3RShouldCutCoupleAtWaitPoint(const Train *v, uint32_t *wait_wp);`（`:34-36` 注释写明返回语义、并说明"为免改 .h 触发全量重编"才手写）；
  - `:843` `const int r3r_wait_cut = R3RShouldCutCoupleAtWaitPoint(v, &r3r_cut_wp);`（每 tick 重算）；
  - `:863-866` state 2 ⇒ `return INVALID_TRACKDIR;`（"已经停在等待区路点上、只是还没放行：本次寻路没有合法终点"，防止 YAPF 全图搜）；
  - `:869` `Yapf().SetCoupleWaitCut(r3r_wait_cut == 1, r3r_cut_wp);`（state 1 换终点为等待区路点）。
- `train_cmd.cpp:15844` 在进入截断的第一 tick 清掉整条旧预留。

⇒ **U-5 结论（已在下文 9.4c 更正）**：~~`R3RTrainStopsAtWaypoint(v)` 直接实现为
`R3RShouldCutCoupleAtWaitPoint(v, nullptr) == 2`~~
进格那一瞬运动前端必在路点格、且尚未放行 ⇒ 返回 2；已放行（配对就绪）或已越过 ⇒ 0 ⇒ 不停。
该函数有副作用（写 `seen_on_wp` / `overshoot`），但它对 `!IsType(OT_GOTO_COUPLE)` 立即 `return 0`（`:13161`），
不会误清这堵墙；副作用本身也只是把「front 真在路点格上」这个事实记下来。

### 9.4c 【更正 9.4(b)】不能复用 `R3RShouldCutCoupleAtWaitPoint()` 做停车判据——它有一道订单类型闸门

`src\train_cmd.cpp:13161`：

```cpp
if (!v->current_order.IsType(OT_GOTO_COUPLE)) return 0;   // :13161
```

而 `ShouldStopAtStation()` 被问的那一刻，车的 `current_order` 是 **`OT_GOTO_WAYPOINT`**（正在执行去栅栏路点那条命令），
不是 `OT_GOTO_COUPLE`（`OT_GOTO_COUPLE` 是**下一条**）。所以该函数在那个时刻**恒返回 0**
⇒ 若拿它当 `R3RTrainStopsAtWaypoint()`，判据永远为假、车永远不会停。**9.4(b) 的实现建议作废。**

正确判据（纯读、零状态、零副作用，建议新写一个只读函数，不复用截断那套 map）：

```cpp
/* 本车在这一趟的「栅栏路点」上该停车：当前订单是 GOTO_WAYPOINT，
 * 且下一条真实订单是 GOTO_COUPLE。这正是 R3RShouldCutCoupleAtWaitPoint() 里
 * :13173-13177 的反向判据（那里看 order_index-1，这里看 order_index+1）。 */
static bool R3RTrainStopsAtWaypoint(const Train *v)
{
    if (v == nullptr || !v->IsFrontEngine()) return false;
    if (!v->current_order.IsType(OT_GOTO_WAYPOINT)) return false;
    const VehicleOrderID num = v->GetNumOrders();
    if (num == 0) return false;
    const VehicleOrderID next = (v->cur_real_order_index + 1 == num)
            ? 0 : static_cast<VehicleOrderID>(v->cur_real_order_index + 1);
    const Order *o = v->GetOrder(next);
    return o != nullptr && o->IsType(OT_GOTO_COUPLE);
}
```

要点：

1. **天然按车区分**——同一路点订单，只有「下一条是 GOTO_COUPLE」那列会算停止，其它列车照旧通过，
   满足 R3「只有符合条件的列车该订单才算停止」。
2. **与 KI-437 口径同源**——栅栏路点的定义本来就是「GOTO_COUPLE 的前一条真实订单是 GOTO_WAYPOINT」
   （`:13145-13146` / `:13177`），这里只是从另一侧问同一个问题。
3. **不需要 `_r3r_wbar_state`**，因此躲开 W-1（`MakeWaiting()` 后 `current_order` 变 OT_WAITING、
   `:13165` 的 erase 是否误清墙）与 W-3（高频调用 + 副作用）两个坑。
4. 停车后 `MakeWaiting()` 把 `current_order` 改成 `OT_WAITING` ⇒ 本判据立刻变假 ⇒
   `ShouldStopAtStation` 回落到原规则 ⇒ **不会永久停在该路点**（这一点正好是我们要的：
   停住之后由 `OT_GOTO_COUPLE` + 停驶分支负责继续等，见 8.4）。

### 9.5 【新更正】`NearEnd` ≠ 车头即停；上一轮 B 方案原写法作废

`src\train_cmd.cpp:974-975`（`PredictStationStoppingLocation`）与 `:728-730`（`GetTrainStopLocation`）：

```cpp
case OrderStopLocation::NearEnd: stop = v->gcache.cached_total_length; break;
```

⇒ `NearEnd` 的真实语义是「**车尾对齐站台起点**」（车头在站内一整列车长处），**不是**「车头即停」。

而且两处都紧跟同一段覆盖：

```cpp
} else if (overhang >= 0) {
    osl = OrderStopLocation::FarEnd;      // :965-968 / :718-721
}
```

路点只有 1 格 ⇒ `overhang = 车长 - 16 > 0` 恒成立 ⇒ **无论 osl 设成什么都强制 FarEnd**。
再加上 `station_cmd.cpp:4625` 的 `stop + station_ahead - TILE_SIZE >= station_length` 检查（路点化简为 `stop >= 16`），
把 stop 调大还会被直接判成「不停车」。

⇒ 「车头即停」必须是**新语义**：在一格路点上把 `stop` 取成「车头中心刚进格」（约 `1 + half_veh_len`），
并且**显式跳过 `overhang >= 0 ⇒ FarEnd`**。
⇒ 落点由「改成 NearEnd」改为「路点分支里按本车判据直接给车头即停的 stop」，两份拷贝
（`GetTrainStopLocation:662-663`+`:718-721`、`PredictStationStoppingLocation:908-909`+`:965-968`）必须同步。

### 9.6 【新发现】减速剖面还有第三个入口，用的是**不带 v** 的重载

`src\train_cmd.cpp:1115-1116`（`ApplyLookAheadItem` 的 `TRLIT_STATION` 分支）：

```cpp
if (order->ShouldStopAtStation(last_station_visited, st, Waypoint::GetIfValid(st) != nullptr)) {
    limit_advisory_speed(item.start + PredictStationStoppingLocation(v, order, item.end - item.start, st), 0, item.z_pos);
```

⇒ 若只改带 v 的重载，**look-ahead 减速剖面不会认为路点要停**：车会全速冲进路点格，
靠 `TrainEnterStation` 的 `cur_speed = 0` 硬停（即玩家说的「撞停」观感）。
⇒ 若要「平滑减速到 0」，这一处必须改成带 v 的重载（`order->ShouldStopAtStation(v, st, ...)`），
并让 `PredictStationStoppingLocation` 返回同一个「车头即停」位置。

### 9.7 修正后的落地清单（A′/B′/C′）

- **A′（谁停）**：`src\order_cmd.cpp:4826` 的带 v 重载追加
  `waypoint && R3RTrainStopsAtWaypoint(v) ⇒ true`（实现放 `train_cmd.cpp`，见 9.4）。
- **B′（停哪）**：`GetTrainStopLocation`（`train_cmd.cpp:662-663` 与 `:718-721`）与
  `PredictStationStoppingLocation`（`:908-909` 与 `:965-968`）在同一个「本车该停」判据下改走**车头即停**的 stop，
  并跳过 `overhang>=0 ⇒ FarEnd` 覆盖；两份必须同步。
- **B2（减速）**：`train_cmd.cpp:1115` 改带 v 的重载，否则没有提前减速。
- **C′（谁放行）**：**零改动**。放行仍由 `R3RShouldCutCoupleAtWaitPoint()` 返回 0 自动发生
  （配对就绪 / overshoot / 订单已变）；停车后 `HandleWaiting` 会把订单推进到 `OT_GOTO_COUPLE`，
  而该订单在 `ProcessOrders` 里不推进（9.2），车在停驶分支（KI-348）原地等
  —— 这正是玩家 U-6 说的「GOTO_COUPLE 本身能等待」。

### 9.8 未确认项结算（2026-10-23 取证后）

- **W-1（已规避）**：`MakeWaiting()` 后 `current_order` 变 `OT_WAITING`、`:13165` 的 erase 是否误清墙——
  自 9.4c 改用**纯读**判据后不再读 `_r3r_wbar_state`，本项对本方案失效，保留为 KI-437 自身的问题。
- **W-2（已取数，结论：`x == stop` 不可靠，必须改成「越过即停」）**：见 §9.9。
- **W-3（已规避）**：新判据 `R3RTrainStopsAtWaypoint()` 是纯读、零副作用的（只读 `current_order` /
  `cur_real_order_index` / `GetOrder(next)`），高频调用自洽。
- **W-4（保留观察）**：`train_cmd.cpp:16820` 把 `last_station_visited` 设成路点 ID。新判据不查该字段，
  但 `ShouldStopAtStation` 的兜底（`order_cmd.cpp:4826` → 不带 v 的 `:4806`）会查，故只影响**未命中新判据**的车，
  按原行为走；若复测发现读档后路点等待异常再回来看。

### 9.9 W-2 取证结论：「车头即停」的 stop 值与 `x == stop` 的命中问题

**① stop 的真实语义（`src\train_cmd.cpp:723-745`）**

```cpp
/* The stop location of the FRONT! of the train */
int stop;
switch (osl) {
    case OrderStopLocation::NearEnd:  stop = front->gcache.cached_total_length; break;
    case OrderStopLocation::Middle:   stop = *station_length - (*station_length - front->gcache.cached_total_length) / 2; break;
    case OrderStopLocation::FarEnd:
    case OrderStopLocation::Through:  stop = *station_length; break;
}
uint8_t rounding = v->IsDrivingBackwards() ? 2 : 1;
int result = stop - ((v->gcache.cached_veh_length + rounding) / 2) + adjust;   // :744-745
```

⇒ 返回值 `result` 就是**车头中心在格内的目标 x**（`VehicleEnterTile_Station` 拿它和 `x` 比）。
- 路点 1 格：`station_ahead = station_length = TILE_SIZE = 16`（`:647-648`），`FarEnd ⇒ stop = 16`
  ⇒ 车长 8 的车 `result = 16 - 4 = 12`（车头中心停在格内 12，车头前端贴格末端）——这就是原生「路点通过式」的停车位。
- **「车头即停」= `result ≈ 1`**，即 `stop ≈ (cached_veh_length + rounding) / 2 + 1`。
- `:4625` 的守卫 `stop + station_ahead - TILE_SIZE >= station_length` 在路点上化简为 **`stop >= 16`**，
  `≈ half+1` 远小于 16 ⇒ 不会被判成「不停车」。而 `NearEnd` 的 `stop = cached_total_length`（整列车长）
  必 >= 16 ⇒ 直接被判「不停车」⇒ **再次确认 NearEnd 不可用**（9.5 成立）。

**② `x == stop` 精确命中不可达（W-2 的实证依据）**

`src\station_cmd.cpp:4629-4661`：

```cpp
x &= 0xF; if (DiagDirToAxis(dir) != Axis::X) std::swap(x, y);
if (y == TILE_SIZE / 2) {
    if (dir != DiagDirection::SE && dir != DiagDirection::SW) x = TILE_SIZE - 1 - x;   // 归一成「从格入口起算」
    stop &= TILE_SIZE - 1;
    if (x == stop) { ... return VehicleEnterTileState::EnteredStation; }
    else if (x < stop) { ... cur_speed = min(cur_speed, max(0, (stop - x) * 20 - 15)); }
}
```

- `VehicleEnterTile_Station()` 由 `VehicleEnterTile()` 在**车头中心进入新格的那一 tick** 调用一次
  （`train_cmd.cpp:18677-18684` / `:19006-19094`，两处都是 `vets.Test(EnteredStation) ⇒ TrainEnterStation(...)`），
  之后的 tick 车头中心还在同一格，**不会再进这个函数**。
- 进格那一 tick 的 `x` 初值 = 该 tick 的位移余量（速度越大越深）。若 `stop = 1` 而 `x` 初值已是 5/10，
  则 `x == stop` 与 `x < stop` **双双不成立** ⇒ 该 tick 什么也不做 ⇒ 车继续滑行，此后再无评估机会 ⇒ **永不触发 EnteredStation**。
- 即使 `x < stop` 成立，减速公式给的是 `(stop-x)*20-15`，`x=0, stop=1 ⇒ spd=5`，下一 tick `x=5 > stop`
  ⇒ 同样跨过。⇒ **`==` 对小车头停点是结构性不可命中的**。

**③ 修法（B′ 附带一条）**

在 `:4637` 把比较改为「带标记列车用 `x >= stop`」：

```cpp
const bool r3r_head_stop = R3RTrainStopsAtWaypoint(consist);
if (r3r_head_stop ? (x >= stop) : (x == stop)) { ... EnteredStation ... }
```

效果：进格那一 tick 若已越过目标点，立刻按停车处理（`TrainEnterStation` 会把 `cur_speed` 清 0），
若还没到则走原有 `x < stop` 渐进减速，下一 tick 必越过 ⇒ 停在格内 0~5px 处。
原生列车（`r3r_head_stop == false`）逐字不变。
`UsingRealisticBraking() && cur_speed > 15` 的 overshoot 分支（`:4638-4651`）**保持原样**：
它表示「真的停不住」，本就该判不停车；B2 的 look-ahead 改动负责让车在进格前把速度降下来，避免落到该分支。

**④ 与 9.5 的关系**：9.5 说的「跳过 `overhang >= 0 ⇒ FarEnd`」只解决 `osl` 被覆盖；
本节说的 stop 公式与 `x >= stop` 是同一件事的另一半，缺任一半都停不下来。

## 五、对上一轮结论的更正义务

若 U-1 证实上一轮 `14127-14131` 的表述有误，必须同时更正：
`R3R_waypoint_reverse_pathfind_20261023_memo.md:3.3` 与 `R3R_KNOWN_ISSUES.md` 对应小节。

## 十、源码落地与构建自证（2026-10-23，已实现）

### 10.1 落地清单（按 §9.7 的 A′/B′/B2/B3/C′，C′ 零改动）

| 落点 | 文件:行 | 内容 |
| --- | --- | --- |
| 判据定义 | `train_cmd.cpp:645` | `bool R3RTrainStopsAtWaypoint(const Train *v)`：`IsFrontEngine()` + `current_order.IsType(OT_GOTO_WAYPOINT)` + `GetNumOrders()>0` + `GetOrder((cur_real_order_index+1) % num)->IsType(OT_GOTO_COUPLE)`。**纯读、零状态**，不碰 `_r3r_wbar_state`（规避 W-1/W-3）。 |
| look-ahead 判据 | `train_cmd.cpp:672` | `static bool R3RWaypointOrderStopsForCouple(const Train *v, const Order *order)`：`order == &v->current_order` 时委托前者，否则在订单表里线性定位该 order 再看其后一条。用于 look-ahead（`order` 可能不是当前订单）。 |
| 停车位（实时） | `train_cmd.cpp:701` / `:773` / `:803` | `GetTrainStopLocation()`：`r3r_head_stop = IsRailWaypoint(tile) && R3RTrainStopsAtWaypoint(front)`；`overhang >= 0 && !r3r_head_stop`（不再被 FarEnd 覆盖）；计算完 `result` 后 `if (r3r_head_stop) return 1;`。 |
| 停车位（预测） | `train_cmd.cpp:963` / `:1025` / 返回值处 | `PredictStationStoppingLocation()`：`r3r_head_stop = order->IsType(OT_GOTO_WAYPOINT) && order->GetDestination() == dest && R3RWaypointOrderStopsForCouple(v, order)`；同样跳过 FarEnd 覆盖并返回 `1`。 |
| 提前减速（B2） | `train_cmd.cpp:1175-1176` | look-ahead `case TRLIT_STATION`：追加 `R3RWaypointOrderStopsForCouple(v, order) && order->GetDestination() == st`，条件变为 `if (order->ShouldStopAtStation(...) || r3r_head_stop)`。 |
| 是否该停（A′） | `order_cmd.cpp:4827` / `:4834` | 前置声明 + 带 v 重载首行：`if (waypoint && v->type == VehicleType::Train && this == &v->current_order && R3RTrainStopsAtWaypoint(static_cast<const Train *>(v))) return true;`。`this == &v->current_order` 保证**只影响该列车**，同订单表的其它车照常通过。 |
| 进格判定（B3） | `station_cmd.cpp:4587` / `:4643-4645` | 前置声明 + `r3r_head_stop = IsRailWaypoint(tile) && R3RTrainStopsAtWaypoint(consist)`；比较改为 `r3r_head_stop ? (x >= stop) : (x == stop)`；overshoot 分支加 `!r3r_head_stop` 守护。 |
| 放行（C′） | — | **零改动**：停车后 `HandleWaiting()` 把订单推进到 `OT_GOTO_COUPLE`，`R3RShouldCutCoupleAtWaitPoint()` 因 `current_order` 不再是 `OT_GOTO_COUPLE` 返回 0 自动放行。 |

### 10.2 与原厂的隔离

所有新行为都被 `r3r_head_stop`（且都带 `IsRailWaypoint(tile)` / `IsType(OT_GOTO_WAYPOINT)`）收窄：
不满足判据的列车在 `GetTrainStopLocation` / `PredictStationStoppingLocation` / `ShouldStopAtStation` / `VehicleEnterTile_Station` 四条路径上逐字走原逻辑。

### 10.3 过程中修掉的两个问题

1. **函数定义缺失**：首轮编辑只写了调用点与跨文件声明，`train_cmd.cpp` 里没有函数体 ⇒ 本次补上 `:645` 与 `:672` 两个定义（否则编译必失败）。
2. **C4018 有符号/无符号不匹配**：`cur_real_order_index + 1 < num` 触发 warning，改为先 `const uint next_index = static_cast<uint>(cur_real_order_index) + 1;` 再比较，warning 归零。

### 10.4 构建自证（内测树 `build\`，复用 `_tmp_inc_build.cmd`，未新建任何 .cmd）

- 护栏：`build\R3R_incbuild.guard.log` = `GUARD: incremental is safe (no header/lang file is newer than the newest object)` ⇒ 增量合法（只改 .cpp）。
- 三次后续增量（补定义 / 修 warning / 补 overhang 守护），最终一次：`[2/3] Building CXX ... train_cmd.cpp.obj` → `[3/3] Linking CXX executable openttd.exe`。
- `build\R3R_incbuild.done` = `EXIT_CODE=0`（13:25:29）；日志内 `warning C` / `error C` / `fatal error` / `FAILED:` / `build stopped` 计数 **0**。
- 时间戳链：`src\train_cmd.cpp` 13:23:44 → `train_cmd.cpp.obj` 13:24:15 → `build\openttd.exe` 13:25:25（52 272 640 B）。
- `read_lints`：`train_cmd.cpp` / `station_cmd.cpp` / `order_cmd.cpp` 均 0 条。
- 本轮未新增日志字符串，故不做 exe 字面量核对。

### 10.5 复测判据（游戏内）

1. **正判据**：`OT_GOTO_WAYPOINT` + 下一条 `OT_GOTO_COUPLE` 的列车，车头中心停在路点格内 x≈1 处（整列车基本在路点格之后/之前，车头压格），速度归零；日志/界面上该车订单随后变为 `OT_GOTO_COUPLE`。
2. **负判据 1（同订单其它车）**：同一订单表的另一列车（或复制订单后单改一辆的状态）必须照常**通过**路点不停车 —— 这是 `this == &v->current_order` 的作用。
3. **负判据 2（普通路点）**：下一条不是 `OT_GOTO_COUPLE` 的路点，停车位与原来一致（车头停在格末端，`result=12`），不停车、不减速。
4. **负判据 3（普通车站）**：车站停靠位置与减速剖面逐字不变（`r3r_head_stop` 恒假）。
5. **减速平滑**：接近路点时速度应提前下降（B2 生效），不应出现「全速冲进路点格后靠 `cur_speed=0` 硬停」或落到 overshoot 分支。
6. **放行**：停住后不卡死，订单推进到 `OT_GOTO_COUPLE` 并进入原地等待（KI-348 停驶分支）。

### 10.6 未确认

- U-7：`result == 1` 时 `VehicleEnterTile_Station` 里 `x >= stop` 命中那一刻，后续停车距离/`cur_speed` 是否足够把车停在格内（还是继续滑出一格）—— 只有游戏内日志能定。
- U-8：`PredictStationStoppingLocation` 返回 `1` 后，`LimitSpeedFromLookAhead()` 的 advisory speed 在极短距离下是否会让车抖动（进格前反复加减速）。
- W-4 仍保留：`train_cmd.cpp` 里把 `last_station_visited` 设成路点 ID 的那处未改。
