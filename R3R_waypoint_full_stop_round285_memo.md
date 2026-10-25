# 第 285 轮 临时分析报告：列车在等待区路点未完全刹停

- 玩家报告原话：「我检查了一下，列车没有在路点处完全刹停，这是什么回事」
- 现场日志：`D:\temp\codebuddy-dropped-files\3f1f256b-ba95-4b24-8573-da7c00125ae8\R3R_debug.log`
  - 189 293 B / 1 260 行 / 2026-10-23 19:06:37
  - 产生者 `build\openttd.exe` 2026-10-23 19:02:53（52 275 712 B）⇒ 晚于日志的 exe 版本，采样有效
- KI 条目：`R3R_KNOWN_ISSUES.md` 的 KI-453（第 285 轮小节）

---

## 一、结论先行

**R3R 的「路点拦停」只有两层，且两层都假设"车头还没到那一格"：**

| 层 | 代码位置 | 作用 |
| --- | --- | --- |
| 寻路层 | `R3RShouldCutCoupleAtWaitPoint()`（`src\train_cmd.cpp`）返回 1/2 | 截断路径到路点格（1）／宣布无合法终点（2） |
| look-ahead 层 | `ApplyLookAheadItem()` 的 `TRLIT_STATION` 分支 + `r3r_head_stop` / `R3RWaitCoupleHeadStopPending()` | 给**尚未到达**的站点项一个 `advisory 0` 目标 |

而原生语义下，`OT_GOTO_WAYPOINT` 订单在**运动前端一进入路点格**就判完成并推进（KI-448 已记录）。两个事件在同一 tick 撞车：

1. 前端进格 ⇒ `TrainEnterStation()` 推进订单 `6 → 16`（`OT_GOTO_COUPLE`）；
2. 下一秒 `current_order` 已不是 waypoint ⇒ `R3RTrainStopsAtWaypoint()` 恒 `false`；
3. 前端所在格不再是 look-ahead 里的"未来站点项"（`station_ahead==0`）⇒ KI-448 加的补偿拿不到任何可用项 ⇒ **0 速目标消失**；
4. 列车按自身加速度曲线在路点格内**重新加速**；
5. 前端开出格 ⇒ `bs.overshoot = true` ⇒ 栅栏**永久失效**；
6. 最终只有 `MarkTrainAsStuck()` 的 `ctx=stop-train` 把它硬刹到 0（位置已越过路点 5 格）。

⇒ **不是"刹车不够狠"，是"目标生命周期错了"**：减速段完全正常（55 → 19），坏在最后一刻目标被撤。

---

## 二、现场读数（`veh=21`）

`veh=21` 是 db=1 的倒车链 ⇒ **运动前端 = 链尾（Last()）**，比车头（`v->tile`）领先约 6 格。等待区路点 = `254,233`，订单目的地 `dest=491798`。

### 2.1 订单提前推进（决定性）

| 锚点 | 原文 | 判读 |
| --- | --- | --- |
| `SPDD veh=21` @ `tile=248,233 x≈3970` | `order=6 → 16`（`real=11 → 12`），此时 `spd=55` | 前端 x≈3970+96=4066，正落在路点格 `254,233`（x 4064~4080）内 ⇒ **进格即完成** |
| `CPL-ENTRY veh=21 tile=249,233 orderType=16` | 挂接寻路入口已在 `OT_GOTO_COUPLE` | 与上一条互证 |

> KI-448 的注释已在源码里写了这条机制（`train_cmd.cpp` 约 L690 注释块），本轮是**首次在速度曲线上抓到它的后果**。

### 2.2 速度曲线（`SPDD veh=21 from=… to=…`）

```
55 → 54 → 53 → 52 → 51 → … → 19    @ head tile=248,233 x=3982   （前端已到路点格末附近）
19 → 20 → 21 → 22 → … → 30          @ head 249,233 → 253,233     （前端 255,233 → 259,233，已越点）
30 →  0  dt=8 ctx=stop-train        @ head 253,233               （Stuck 硬刹，最终停点）
```

- 减速段 = `limit_advisory_speed(item.start + PredictStationStoppingLocation(...), 0, ...)` 正常生效；
- **回升段**是核心异常：目标被撤后没有任何东西阻止加速；
- 终局 `ctx=stop-train` ⇒ 停住靠的是"卡死刹车"，不是"到点停靠"。

### 2.3 栅栏状态

- `CPL-WAITCUT veh=21 state=0 wp=17 tile=249,233`（全日志**仅 1 条**）
  - `state` = `R3RShouldCutCoupleAtWaitPoint()` 返回值：`0`=不截断/已放行、`1`=截断到路点、`2`=站路点未放行；
  - 打印点 `src\pathfinder\yapf\yapf_rail.cpp:854`（`FindNearestCoupleTrain()` 入口，状态变化才写）。
- `wp=17` 有值 ⇒ 该次调用**越过了** `prev->IsType(OT_GOTO_WAYPOINT)` 判据、在 `13331` 行赋值 ⇒ 走的是完整分支。
- 全日志 `CPL-PAIR` **0 命中**，`CPL-SCAN … found=0 … why=GROUP`（**挂接分组不匹配**） ⇒ `pair_ready`（要求 `R3RGetCouplePairPartner(v)!=nullptr` 且车底已停稳在站台格）**不成立**。
- 由「`state=0` + `wp=17` + `pair_ready=false`」反推 ⇒ 只能落在 **overshoot 分支**（`train_cmd.cpp:13365`）。

---

## 三、疑点（按规则落盘）

### Q-285-1（探针被静默，优先级最高）

`WBAR-OVERSHOOT`（`train_cmd.cpp:13360`）与 `WBAR-RELEASE`（`:13425`）都走 `R3RDbgWrite`，其 tag 被 `R3RDbgTagAllowed()`（`src\r3r_perf.h`）过滤——默认白名单只放 `RESV*` / `UNRES*` / `EXTEND` / `LOOP` / `R3R-RES`。

⇒ 全日志 `WBAR` **0 命中是"看不见"，不是"没触发"**。第 284 轮 D-4 专门加的 `WBAR-RELEASE ... pair=` 证据同样被吃掉。

**这条必须先修（零行为变更），否则下一轮还要靠反推。**

### Q-285-2

`state=0` 那行仍打印 `wp=17`，说明它不是在早期 return（`13305/13307/13319/13328`）退出的。

### Q-285-3

终点为 head `253,233` / 前端 `259,233` ⇒ **越过路点 5 格**；`TRP veh=21 … ok=0 res=0 spd=0` ⇒ 停住后不再有任何预留尝试，与 KI-452 的自锁同源，但**自锁发生在越点之后**，不是"没刹住"的原因。

---

## 四、修法候选（未落地）

### F-285-3（建议先做，零行为变更）

把 `WBAR-` 前缀加进 `R3RDbgTagAllowed()` 的默认白名单（或这两行改走 `R3RFopenDbg` 直写），让 `WBAR-OVERSHOOT` / `WBAR-RELEASE` 可见。
收益：下一轮能**一句话**分清"是 overshoot 还是 pair_ready"，不再翻案。

### F-285-1（首选修法）

给「车头已在等待区路点格内且尚未放行」补一层**直接制动**：

- 判据（任一成立）：`R3RWaitCoupleHeadStopPending()` 为真；或「前端在路点格内 + prev 订单是等待区路点 + `R3RShouldCutCoupleAtWaitPoint()!=0`」；
- 动作：把该 tick 的目标速度钳到 0（软版本的 `ctx=stop-train`），使车在格内**停住**而不是重新加速；
- 放行条件不变（仍由 overshoot / pair_ready 决定），避免永久钉死。

### F-285-2（治根，风险最大）

把路点订单的完成判据从"前端进格"改到"前端到达格末/停车标"（触及 `TrainEnterStation()` 原生 waypoint 分支与 `ShouldStopAtStation(..., waypoint=true)` 语义）。
不改的话，"进格即完成"的原生行为会**持续**与 R3R 的拦停设计打架，F-285-1 只是补丁。

---

## 五、复测判据（F-285-1 落地后）

1. 复跑同场景，`SPDD veh=21` 速度**单调减到 0 且不再回升**（不得出现 19→30 段）；
2. 停住位置在前端 `254,233` 格内（不越过 `255,233`），且 `stuck=0`（不得靠 `ctx=stop-train` 收尾）；
3. 放行后仍能 `CPL-PATHFOUND found=1` 并完成挂接（不因新增钳速把车永久钉死在路点）；
4. 负判据：普通（非等待区）`OT_GOTO_WAYPOINT` 通过式路点逐字不变，无减速/停车；
5. 负判据：站台正常停靠（`ShouldStopAtStation` 路径）速度曲线逐字不变。

---

## 六、未确认项

- U-285-1：本轮无法证实 overshoot 与 pair_ready 哪一个先发生（Q-285-1 导致）。
- U-285-2：`CPL-SCAN why=GROUP` 的哪个分组不匹配未取数（不影响本条结论，属挂接分组配置面）。
- U-285-3：越点后 `TRP ok=0` 的自锁与 KI-452 的自锁是否为同一份修复覆盖，待 F-284-1 落地后一并复测。

---

## 七、第 286 轮：玩家拍板与落地（F-285-2 折中版）

### 7.1 玩家原话（2026-10-23）

> 「我选择治根的一个折中方法，对于**符合条件需要在路点刹停的列车**，路点订单完成判据改到『到达格末/停车标』」

⇒ 口径收窄为：**只**改「等待区路点」（下一条是 `OT_GOTO_COUPLE`，判据函数 `R3RTrainStopsAtWaypoint()`）；普通通过式路点、站台停靠全部逐字不变。

### 7.2 取证补正：真凶在 `order_cmd.cpp`，不在 look-ahead

第 285 轮把矛头指向 look-ahead 层（`AdvanceOrdersFromVehiclePosition()` 里 `SwitchToNextOrder(true)`）。本轮核代码发现**更早的那一刀在 `src\order_cmd.cpp` 的 `ProcessOrders()`**：

```
/* Check if we've reached a 'via' destination. */
if (((OT_GOTO_STATION && NO_STOP_AT_DESTINATION) ||
     (OT_GOTO_WAYPOINT && (!IsWaitTimetabled() || type != Train))) &&
    IsTileType(moving_front->tile, TileType::Station) &&
    current_order.GetDestination() == GetStationIndex(moving_front->tile)) {
    ... IncrementImplicitOrderIndex(); ...
}
```

等待区路点通常没有时间表等待（`IsWaitTimetabled()==false`）⇒ 车头一进格，`ProcessOrders()`（每 tick 在 `TrainLocoHandler` 开头被调用）就把订单推到 `OT_GOTO_COUPLE`，这才是第 285 轮日志里「`order=6 → order=16` 而 spd 仍 55」的那一刀。look-ahead 层的两刀是**同一语义的第二、第三道闸**，必须一起堵，否则任意一道先放过都会让 0 速目标消失。

### 7.3 落地（4 处，2 个 `.cpp`）

| # | 文件 / 函数 | 改动 |
| --- | --- | --- |
| 1 | `order_cmd.cpp` `ProcessOrders()` | via 判据追加 `!(r3r_wait_waypoint && v->cur_speed != 0)`；`r3r_wait_waypoint = type==Train && R3RTrainStopsAtWaypoint(...)`；补前向声明 |
| 2 | `train_cmd.cpp` `AdvanceOrdersFromVehiclePosition()` | 等待区路点只 `CTTLASF_STOP_FOUND` + `return`，不 `SwitchToNextOrder()` |
| 3 | `train_cmd.cpp` `AdvanceOrdersFromLookahead()` | `GetDestination()==st && !R3RTrainStopsAtWaypoint(v)` 才 `SwitchToNextOrder(true)` |
| 4 | `train_cmd.cpp` 新 `R3RAdvanceStoppedWaitWaypointOrder()` + 停驶分支调用 | 停稳（`cur_speed==0`）且前端仍在路点格时兜底推进一步订单，探针 `WP-STOP-ADVANCE`（直写文件，绕开 tag 白名单） |

正常路径仍然复用原生：停格末 → `EnteredStation` → `TrainEnterStation()` waypoint 分支 `MakeWaiting()` → `HandleWaiting` 推进订单。兜底只覆盖「车头已越过停车标、`EnteredStation` 不会再来」的死锁形态。

### 7.4 待复测判据（与 KI-453/第 286 轮小节同）

1. 速度单调减到 0 不再回升（无 19→30 段）；
2. 停在前端 `254,233` 格内，`stuck=0`，无 `ctx=stop-train` 收尾；
3. 放行后 `CPL-PATHFOUND found=1` → `COUPLE-OK`（不永久钉死）；
4. 出现 `WP-STOP-ADVANCE idx=A->B type=16`；
5. 负判据：普通通过式路点不停车、无 `WP-STOP-ADVANCE`；站台停靠曲线不变。

### 7.5 构建自证（2026-10-23）

- 入口：既有 `_tmp_inc_build.cmd`（未新建任何脚本）；护栏 `GUARD: incremental is safe`（只改两个 `.cpp`，未碰 `src\*.h` / `lang\*.txt`）。
- 日志：`[2/4] train_cmd.cpp.obj` → `[3/4] order_cmd.cpp.obj` → `[4/4] Linking CXX executable openttd.exe`；`build\R3R_incbuild.done` = `EXIT_CODE=0`。
- 时间戳：`src\order_cmd.cpp` 19:47:53、`src\train_cmd.cpp` 19:48:24 → 两个 obj 19:51:19 → `build\openttd.exe` 19:52:50（52 275 712 B）。
- `read_lints` 两文件 0 条；exe 命中 `WP-STOP-ADVANCE`。

### 7.6 本轮遗留

- F-285-3（`WBAR-` 白名单）仍未做，本轮新探针一律走 `R3RFopenDbg` 规避。
- KI-452 的 F-284-1（挂接寻路前先释放本链旧预留）仍未落地，与「越过路点后 `TRP ok=0` 自锁」同源，需一并复测。
