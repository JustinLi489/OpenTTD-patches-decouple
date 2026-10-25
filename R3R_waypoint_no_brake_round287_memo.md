# 第 287 轮：等待区路点「预留拦住了、车没拦住」

## 一、玩家报告（原话）

> 「列车还是没在路点刹停……虽然列车的预留拦下来了，但是车没有拦下来」

现场日志：`D:\temp\codebuddy-dropped-files\695775fc-c4a6-4cde-adb8-76becb94c125\R3R_debug.log`
规模 190 077 B / 1 276 行 / 2026-10-23 19:57:19（产生者应为第 286 轮 exe `build\openttd.exe` @19:52:50）。

## 二、首轮读数（已确认）

| 行号 | 原文（节选） | 判定 |
| --- | --- | --- |
| :1151 | `SPDD veh=21 from=30 to=0 dt=7 ctx=stop-train cfrom=30 cto=0 ... tile=253,233 x=4050 db=1 order=16 real=12 stuck=1` | 速度 **30 → 0 一次性归零**，`ctx=stop-train` 即「靠硬刹收尾」，不是按 look-ahead 逐步减速停车；此时 `order=16`（`OT_GOTO_COUPLE`）⇒ **路点订单已经完成**，车不是停在路点订单上 |
| :1148? | `TRP veh=21 mf=259,233 real=12 type=16 dest=491798 spd=0 ... => ok=0 res=0` | 停住后预留拿不到（`ok=0 res=0`），`mf=259,233` 与车头 `253,233` 不同格 |
| 尾部 | `COUPLE-FAIL loco=21 order=16 tx=253 ty=233 x=4050 y=3736` + `COUPLE-WAIT-OFFDEST veh=21 tile=253,233 mf=259,233 real=12 isDepot=0 destDepot=0 destStn=12 destTile=491798 onDepotTile=0 onStnTile=0` | 停在「不是目的地所在站台」的位置上反复失败 |

- `WP-STOP-ADVANCE` **零命中** ⇒ 第 286 轮新增的兜底推进函数 `R3RAdvanceStoppedWaitWaypointOrder()` **一次都没跑**（它只在 `cur_speed==0` 且订单独占 `OT_GOTO_WAYPOINT` 时才推进）。
- 全日志 `COUPLE-OK` 零命中、`CPL-S0-CHK` 零命中。

## 三、由读数得出的结构性结论

1. **第 286 轮的 via 判据补丁没有生效在真正的推进点上。** 订单仍然在车头进格那一瞬从 `OT_GOTO_WAYPOINT` 变成了 `OT_GOTO_COUPLE`，但**不是**走 `ProcessOrders()` 的 via 分支（那条已被 `cur_speed != 0` 拦住），而是走**原生到达处理** `VehicleEnterTile_Station()` → `TrainEnterStation()` 的 waypoint 分支（`MakeWaiting()` + `cur_speed = 0`）。该分支：
   - 不受 `ProcessOrders()` 判据约束；
   - 一到 `x >= stop`（`r3r_head_stop` 形态）就触发，`MakeWaiting()` 会**直接把 `cur_speed` 清零** ⇒ 这正是 `ctx=stop-train` 的「一次性 30 → 0」。
2. **车没被 look-ahead 减速的真正原因**：`ctx=stop-train` 说明刹车来自 R3R/引擎的停站硬置零，而不是 `TRLIT_STATION` 的 0 速目标。⇒ look-ahead 里那条「在路点格末以 0 速停住」的项**没有产生**（或产生了但 `ApplyAdvisory` 没被采用）。

## 四、猜测根因（待验证）

- **G-287-1（高可能）**：`GetTrainStopLocation()` 的 `r3r_head_stop` 形态只在「`R3RTrainStopsAtWaypoint(v)` 为真」时成立，而该判据要求 `v->current_order.IsType(OT_GOTO_WAYPOINT)`；一旦订单被子系统提前推到 `OT_GOTO_COUPLE`（哪怕只有一 tick），0 速目标就消失 ⇒ 与第 285/286 同一族。
- **G-287-2（中可能）**：`TrainEnterStation()` waypoint 分支的 `MakeWaiting()` 在**车头刚进格**就执行，使 `cur_speed` 归零，先于任何 look-ahead 减速完成 ⇒ 观感是"硬刹"，即使 look-ahead 已经减速过。
- **G-287-3（待查）**：`ok=0 res=0` 与 `COUPLE-WAIT-OFFDEST onStnTile=0` 说明停住的位置**不是目的地站台格**（车头 253,233 vs 运动前端 259,233、`destTile=491798`），即"停错地方"而不是"停得太晚"。

## 五、待验证项

- U-287-1：`order=16` 是在哪一 tick、由哪个调用点从 15/1 推进到 16 的（需 `ProcessOrders`/`TrainEnterStation`/兜底三者的可区分探针）。
- U-287-2：路点订单是否带时间表等待（`IsWaitTimetabled()`），决定是否走 `EnteredStation` 原生路径。
- U-287-3：`cur_speed` 在 `x >= stop` 之前是否曾按 look-ahead 逐步下降（日志尾部只见 30 → 0，需更早的速度序列）。
- U-287-4：`destTile=491798` 对应的实际 (x,y) 与 253,233 / 259,233 的关系。

## 六、第二轮取证（2026-10-23，日志逐行锚点；**结论修订**）

关键新读数（`build\R3R_debug.log` 同一份 1 276 行日志）：

| 行号 | 原文（节选） | 判定 |
| --- | --- | --- |
| :716 | `SPDD veh=21 from=54 to=0 dt=0 ctx=wp-arrive cfrom=53 cto=0 ... tile=248,233 x=3971 order=9 real=11` | **`ctx=wp-arrive`**：零速来自 `TrainEnterStation()` 的 waypoint 分支（`MakeWaiting()` + `cur_speed = 0`），不是 look-ahead。`order=9` = `OT_WAITING` ⇒ 到达处理**确实触发了**，且是 53→0 的硬置零。`real=11` = 路点订单本身。 |
| :733~:759 一带 | `SPDD ... ctx=wp-arrive cage=1 ... order=16 real=12`，速度序列 20 → 21 → 22 → … → 35 | `OT_WAITING` 在**下一 tick 就被 `ProcessOrders()` 推成 `OT_GOTO_COUPLE`（real=12）**，随后车从 0 重新加速 ⇒ **"停"只存在一个 tick**，观感就是"没刹停"。 |
| :829 | `CPL-ENTRY veh=21 tile=249,233 orderType=16 dontReserve=0` | 进挂接寻路时订单**早就是 16**，路点订单已完成。 |
| :831 | `CPL-WAITCUT veh=21 state=0 wp=17 tile=249,233` | 等待区截断（wait cut）是在**订单已成 16 之后**才做的 ⇒ 截断发生在车已开过路点之后。 |
| :329 / :840 | `RESV-WATCH SET-STN tile=254,233 track=255 phase=yapf-resv actor=21 head=21` / `RESV-GC veh=21 tile=254,233 track=stn kind=WAYPOINT why=free-path-hold` | 路点格 = **254,233**。进站前的 yapf 预留**一路订过了路点**（:836~:844 释放 250..258,233 共 9 格），说明"在路点前把预留截断"**并没有在进站前发生**。 |
| :1150 | `COUPLE-WAIT-OFFDEST veh=21 tile=253,233 mf=259,233 real=12 ... destStn=12 ... onStnTile=0` | 车头已到 259,233（越过路点 254,233），`consist->tile`=253,233 是**车尾**；停在了"目的地站台之外"。 |

⇒ **机制链（已实证，取代第三节的猜测）**：

1. 进站前 yapf 预留照常订到路点之外（`SET-STN 254,233` + `SET-RAIL 255..258,233`），**没有任何"路点前截断"**；
2. 车头以 53 的速度压进路点格 254,233，`VehicleEnterTile_Station()` 因 `r3r_head_stop ? (x >= stop) : (x == stop)` 走 `x >= stop` 分支 ⇒ 立即 `return EnteredStation`（该分支的"太快"保护 `!r3r_head_stop && ...` 对等待区路点**被显式跳过**）；
3. `TrainEnterStation()` waypoint 分支 `MakeWaiting()` → `order` 变 `OT_WAITING`、`cur_speed = 0`（53→0 硬刹，`ctx=wp-arrive`）；
4. 下一 tick `ProcessOrders()` 把 `OT_WAITING` 直接推成 `OT_GOTO_COUPLE`（waypoint 无时间表等待 ⇒ 0 tick 等待）；
5. 订单已是 16 ⇒ `R3RTrainStopsAtWaypoint()` 变假 ⇒ 后续不再有"停稳才算到"的约束，车重新加速；
6. `TrainCoupleHandler()` 此时才做 `CPL-WAITCUT`（:831，tile=249,233 车尾），并释放其余预留（:834~:845），但车已越点；
7. 挂接目标被 **`why=GROUP`** 否决（:833/:1130，候选 `last=278,234 lastveh=24`，veh24 是 `type=17 OT_WAIT_COUPLE` 的等待车底），`COUPLE-FAIL` 反复；
8. 车以 30 的余速继续前移（:1122 `LOCO-MOVE veh=21 tile=253,233 j=207 spd=30`），最终停在 253,233、预留全空 ⇒ `STUCK state=1 stuck=1`、`TRP ok=0 res=0`。

⇒ **两个独立缺陷**：

- **D-287-1（本轮主线，玩家报的）**：等待区路点的"到达"判据仍是"车头进格"（`x >= stop`），并且该形态**跳过了"太快"保护**；到达后 `OT_WAITING` 立即被推成 `OT_GOTO_COUPLE`，于是"必须刹停"退化为"硬刹一 tick 再加速"。同时进站前的预留**没有**在路点前截断（`SET-STN/ SET-RAIL 255..258,233`），look-ahead 没有 0 速目标 ⇒ 不会逐步减速。
- **D-287-2（旁证，独立）**：`CPL-SCAN ... why=GROUP` ⇒ 挂接候选被挂接分组闸门否决，即使停稳也挂不上。需单独定性（不改本轮主线判据）。

## 七、下一步（本轮计划）

1. 只读取证：找 `OT_GOTO_WAYPOINT → OT_GOTO_COUPLE` 的**唯一**提交点（读 `VehicleEnterTile_Station()` 的 `EnteredStation` 返回条件与 `TrainEnterStation()` waypoint 分支）。
2. 若确认为 `TrainEnterStation()` 路径：把「到达判据」从 `x >= stop`（进格即到）改为「已停稳」——即在该分支入口加 `cur_speed == 0` 前置，或在 `VehicleEnterTile_Station()` 里对 `r3r_head_stop` 追加「到停车标」判定。
3. 负判据不变：普通通过式路点、站台正常停靠逐字不变。

## 八、玩家口径（2026-10-23，第 287 轮拍板；原话）

> “到达”不能等于“车头进格”，依旧符合条件的才按照这种严格判据，然后，我明确，减速钳制我不喜欢，不希望加进游戏

⇒ 本轮修法被收窄成两条，取代第六节末尾的“两个独立缺陷”并列处理：

- **R287-P1（严格判据，仅限“符合条件的”）**：只有“等待区路点”（判据 = 第 286 轮的
  `R3RTrainStopsAtWaypoint()`：当前订单是 `OT_GOTO_WAYPOINT` 且下一条是 `OT_GOTO_COUPLE`）
  才使用严格判据——“真正停在格末停车标上”才算到达。**普通站台停靠、普通通过式路点
  逐字不变**（仍是原生 `x == stop`）。
- **R287-P2（拒绝减速钳制）**：不得把原生那段 `spd = max(0, (stop - x) * 20 - 15)`
  扩展到等待区路点，也不得删掉它的 `cur_speed > 30` 早退；**不新增任何自造减速/钳制**。
  减速只能来自原生 look-ahead 刹车（`PredictStationStoppingLocation()` + `limit_advisory_speed(..., 0, ...)`
  给出的 0 速目标，第 286 轮通道，保持不变）。

⇒ 落地形态（`src/station_cmd.cpp` 的 `VehicleEnterTile_Station()` 唯一改动）：

```
r3r_arrived = r3r_head_stop ? (x >= stop && consist->cur_speed == 0) : (x == stop)
```

即“以非零速度越过停车标”既不判到达、也不在这里做任何减速动作（落空 `return {}`），
车继续由 look-ahead 的 0 速目标刹停；停稳后订单由
`R3RAdvanceStoppedWaitWaypointOrder()`（`train_cmd.cpp:21498`，停驶分支）推进。

判据要求（复测）：①严格路径下不得再出现 `SPDD ... ctx=wp-arrive from=5x to=0`
（高速进格被硬刹）②应出现 `WP-NOARRIVE`（本次新增只读探针，记录“越过停车标但未停稳”）
③停稳后出现 `WP-STOP-ADVANCE`④普通站台停靠/普通通过式路点日志与行为逐字不变。

## 九、实现落地（第 287 轮，已实现 + 已编译）

只改一个文件：`src/station_cmd.cpp` 的 `VehicleEnterTile_Station()`（未碰任何 `src/*.h`）：

1. `:4654-4656` 到达判据由 `r3r_head_stop ? (x >= stop) : (x == stop)` 改为
   `r3r_head_stop ? (x >= stop && consist->cur_speed == 0) : (x == stop)`，并抽成
   `const bool r3r_arrived`。⇒ **严格判据只对“等待区路点”生效**，普通站台/普通路点逐字不变。
2. `:4672-4685` 新增只读探针分支 `else if (r3r_head_stop && x >= stop)`：越过停车标但未停稳时
   打 `WP-NOARRIVE veh= tile= x= stop= spd=` 并 `return {}`。**该分支不写任何状态、
   不做任何减速**（玩家 R287-P2 明确拒绝钳制）。仅在 `t == consist->GetMovingFront()`
   时记录，避免逐节刷屏。
3. `:4686-4694` 原生钳制段 `spd = max(0, (stop - x) * 20 - 15)` 与其 `cur_speed > 30` 早退
   **一字未动**，也未把它扩展到等待区路点。

构建自证（复用 `_tmp_inc_build.cmd`，未新建任何 `.cmd`）：
`R3R_incbuild.guard.log` = `GUARD: incremental is safe`；
日志尾部 `[2/3] Building CXX object ...station_cmd.cpp.obj` → `[3/3] Linking CXX executable openttd.exe`；
`build\R3R_incbuild.done` = `EXIT_CODE=0`；
`src\station_cmd.cpp` 20:11:10 → obj 20:11:59 → `build\openttd.exe` 2026-10-23 20:13:27（52 275 712 B）；
`read_lints` 0 条；exe 内 `findstr /M /C:"WP-NOARRIVE"` 命中。

状态＝已实现 + 已编译，**游戏内复测待做**。
