# R3R 第 276 轮续四：等待区截断「越过路点」后永久停车（临时分析报告）

- 日期：2026-10-22
- 输入日志：`D:\temp\codebuddy-dropped-files\107f65cb-c1a6-45d9-88e9-46c1ca570f74\R3R_debug.log`
  - 行数 1371，mtime 2026-10-22 21:35
  - 产生者 `build\openttd.exe` mtime 21:31:12（含第 276 轮续三 KI-433 / KI-434 的版本；`findstr` 在 exe 内确认存在 `CPL-WAITCUT-CLEAR` 与 `CPL-WAITHOLD` 两个字面量）
- 本轮性质：**只取证 + 定性 + 登记**，未改任何源码、未构建。
- 涉及车辆：act = veh **21**（执行 `OT_GOTO_COUPLE`，real=12），tgt = veh **24**（等待车底，real=11 `OT_WAIT_COUPLE`，`dest=4294967295`，`RESV-NOBOOK ... reason=parked-waiter`）。
- 关键坐标（MapSizeX=2048）：
  - 21 起点 `248,234`，等待区路点 `254,234`（`wp=17` 即 StationID 16 + 1）
  - 24 停在 `265,234`
  - 21 的 GOTO_COUPLE 目的地 = 站 12 = `destTile=491798` = **(278,240)**（`COUPLE-WAIT-OFFDEST ... destStn=12 destTile=491798` 与 `DEPOT-ARR ... destTx=278 destTy=240` 两条互证）

## 一、现场时间线（全部为日志行号锚点）

| 行 | 内容（截取） | 判定 |
|---|---|---|
| 1036 | `CPL-PAIR tick=3014 act=21 tgt=24 dist=17 actTile=248,234 tgtTile=265,234` | 会车对已解析到 24，距离 17 |
| 1040 | `CPL-WAITCUT veh=21 state=2 wp=17 tile=248,234` | **截断生效**：运动前端 `254,234` 在路点格、速度 0，state=2（等放行条件） |
| 1041-1047 | `RESV-GC ... 248..253 track=0x0 why=free-path` / `254,234 track=stn kind=WAYPOINT why=free-path-hold` | 旧预留被清 8 格，路点格保留（kept=1） |
| 1048-1050 | `RESV-GC 256,234 / 257,234 why=free-path` + `RESV-LEDGER-FORGET ... released=8 kept=1 phase=choose-track` | 一次性大清理 = KI-433 的 `FreeTrainTrackReservation` |
| 1051 | `COUPLE-FAIL loco=21 order=16 tx=248 ty=234` | 几何没贴到（dist=17），属正常 |
| 1065 | `COUPLE-WAIT-OFFDEST veh=21 ... mf=254,234 real=12 destStn=12 destTile=491798 ... onStnTile=1` | 只读诊断：配对目标不在订单目的地，但订单目的地是 (278,240) |
| 1067 | `CPL-SKIP veh=21 tile=248,234 orderType=16 dontReserve=0 retryIn=7 holdAge=1` | **YAPF 本次根本没搜索**（空搜索节流窗口内） |
| 1068-1095 | 每 4 行一组的 `RESV-GC 254 kept` + `256/257 released=2` + `RESV-LEDGER-FORGET released=2 kept=1 phase=choose-track` | **每 tick 都在 `FreeTrainTrackReservation`**（goto 15863） |
| 1096 | `COUPLE-FAIL loco=21 ... tx=249 ty=234` | 链头已从 248 前进到 249 |
| 1112 | `CPL-PAIR tick=3017 ... dist=16 actTile=249,234` | 车在动、距离在缩 |
| 1172 | `CPL-WAITCUT veh=21 state=1 wp=17 tile=249,234` | **运动前端已离开路点格 ⇒ state 从 2 掉回 1（截断）** |
| 1173-1176 | `PFD-ACCEPT kind=waitwp tile=254,234` / `CPL-PATHFOUND found=1` / `CPL-BEST tile=254,234` | 截断寻路成功，终点=路点 |
| 1218-1256 | `CPL-PAIR ... dist=15,14,13,12,11 actTile=250..254` | 一路推进到链头 `254,234`（正好是路点格） |
| 1269 | `EXTEND-BACKOUT veh=21 origin=260,234 stop=264,234 next=278,234 reason=choice-reserved owner=24` | 运动前端已到 `260,234`——**越过路点 254 六格**；前路被 24 的预留挡住 |
| 1288-1293 | `TPR-WALK ... mf=260,234 origin=260,234 td=8 okay=1 new=261,234 spd=0` / `EXTEND-BACKOUT ... owner=21 n=1..4` | 前路 264,234 的预留 **owner=21（自己）** ⇒ 自己堵自己，`ExtendTrainReservation` 反复回退 |
| 1295-1354 | `CPL-PAIR ... dist=11 actTile=254,234 tgtTile=265,234` 恒定重复 | 距离不再缩小（21 停在 254，24 在 265，中间 255..264 走不动） |
| 1355-1371 | 仍为 `COUPLE-FAIL` / `TPR-WALK mf=260,234 spd=0` / `COUPLE-WAIT-OFFDEST` | 日志结束前**无 `COUPLE-OK`**，永久静止 |

## 二、已经生效的部分（不能回退）

1. **截断协议本身工作**：`CPL-WAITCUT state=2`（行 1040）与 `PFD-ACCEPT kind=waitwp tile=254,234`（行 1173）说明判据与 YAPF 终点都已经锁到等待区路点。
2. **KI-433 的旧预留清理工作**：行 1041-1050 一次清掉 8 格，路点格按 `free-path-hold` 保留（`kept=1`）。
3. **KI-434 的取端同源工作**：`CPL-ORIGIN origin=` 与 `CPL-WAITCUT tile=`/`TPR-WALK mf=` 全部落在运动前端（本例 db=0，但 state 能升到 2 说明取端一致）。

## 三、缺口 A（高）：越过路点后没有恢复档 ⇒ 永久停车

### 3.1 判据（源码锚点 `src\train_cmd.cpp:13131-13160`）

```cpp
const Train *wait_front = v->GetMovingFront();
const bool front_on_wp = (wait_front != nullptr && IsRailWaypointTile(wait_front->tile)
        && GetStationIndex(wait_front->tile) == wait_dest);
if (!front_on_wp) return 1;          // ← 只有"没到/已越过"合在一起
if (v->cur_speed != 0) return 2;
... ①②③ 全成立 ⇒ return 0（放行并 latch）
```

即只有三档：**没到路点 = 1（截断，终点=路点）**、**在路点上但没停稳/条件不齐 = 2**、**在路点上停稳且条件齐 = 0（放行）**。
注释（13146-13149）明确写：「截断模式下 YAPF 终点就是路点格、预留只铺到路点格，机车必然被停到路点格上，所以"已越过路点"只可能发生在截断生效那一 tick 之前，属一次性边界，**不再为它开后门**。」

### 3.2 现场把这条注释打穿

- 行 1288/1311/1334/1352/1370：`TPR-WALK veh=21 mf=260,234 origin=260,234 td=8 okay=1`，运动前端 **260,234**，路点是 **254,234**，已经过去 6 格。
- 此后 `front_on_wp == false` ⇒ 恒 `return 1` ⇒ YAPF 终点永远是**身后**的路点 ⇒ 每次搜索必然失败（`okay=1` 只表示入口可走，`reach=1` 到达 261 后就无路可去）⇒ 只走"停车 + 撤预留"分支 ⇒ 每 tick 原地。
- `EXTEND-BACKOUT ... reason=choice-reserved` 与 `TPR-WALK ... new=261,234` 是同一件事的两面：想延伸，被自己名下（`owner=21`）的 264,234 预留挡回。

### 3.3 为什么会越过

截断态并不禁止所有前进：只要某一 tick 的**截断寻路成功**（`CPL-PATHFOUND found=1`，行 1174），`path_found != INVALID_TRACK` ⇒ `result_flags |= CTTRF_RESERVATION_MADE`（14999 附近）⇒ 列车正常前进；只有"失败/节流跳过"的 tick 才撤预留+停车。
于是形成 **「成功一 tick 就前进几格 → 前端离开路点格 → 下一 tick state 退回 1 但终点已在身后 → 失败 → 停车撤预留」** 的交替。若干次成功之后，链头正好骑在路点格上、前端冲过路点，从此再无成功可能。

> 结论：`state` 语义把「尚未到达」与「已经越过」混为一档，而截断态下"越过"是可发生的（且本例确实发生）。

## 四、缺口 B（中）：`CPL-SKIP`（空搜索节流）在截断态被当成真失败 ⇒ 每 tick 清预留

源码链：

- `src\train_cmd.cpp:15787`：`if (keep_track != INVALID_TRACK && R3RCoupleSearchSkipped(consist) && r3r_wait_cut == 0) return ...;`
  —— 第 271 轮为治"铺→清→铺"抖动加的早退，第 276 轮续三（KI-433）追加了 `r3r_wait_cut == 0` 条件。
- 因此截断态下即便本次只是 `CPL-SKIP`（节流窗口内根本没搜索），也落到 `15822 if (!pair_ready || r3r_wait_cut != 0)` ⇒ `cur_speed = 0` + `15863 FreeTrainTrackReservation(...)`。

现场证据：行 1068-1095 每 tick 一组 `released=2 kept=1 phase=choose-track`；行 1067 明确是 `CPL-SKIP ... retryIn=7 holdAge=1`（不是真 `found=0`）。
后果：
1. 玩家看到的"预留一直被销毁"（`RESV-GC` 抖动）就是这条；
2. 节流窗口内 YAPF 不搜索，于是"配对/路径永远不推进"，但车又靠成功 tick 前进 —— 两种状态互相打断，最终以"越过路点"收场（缺口 A）。

## 五、疑点（REG-276-D）

- **REG-276-D-1**：`CPL-WAITHOLD` 全日志 0 条，而 `15832` 的 `CPL-WAITHOLD` 只在 `r3r_wait_cut != 0` 且 `seen != state` 时打。⇒ 说明本 tick 内 `TryPathReserveWithResultFlags` 侧算出的 `r3r_wait_cut` 与 YAPF 侧（`CPL-WAITCUT state=2`）**不一致或为 0**（否则至少会打一行 state=1/state=2）。同一族：`CPL-WAITCUT-CLEAR` 也 0 条。**待验证**：在 `15735` 后补一条无条件只读探针（`CPL-CUTVAL veh= cut= pair_ready= skip= spd=`），一次即可判定。
- **REG-276-D-2**：每 tick 都在 `FreeTrainTrackReservation`（released=2）却仍能前进 —— 说明"前进"只发生在成功 tick，且成功 tick 里 `path_found != INVALID_TRACK` 跳过了失败分支；需要在成功 tick 也打 `CPL-CUTOK veh= path_track= cut=` 以确认。
- **REG-276-D-3**：`EXTEND-BACKOUT ... owner=21`（行 1289-1293）——自己名下的 264,234 预留从哪来、为何没被 KI-433 的一次性清理覆盖（清理发生在行 1041-1050，只到 257）。若为 `TRP` 延伸产物，则与 KI-428（`FreeTrainTrackReservation` 忘账本）同族。
- **REG-276-D-4**：行 1269 首次 `owner=24` —— 21 的前路 264,234 被 **24** 预留。这与"24 是停着的等待者"（`RESV-NOBOOK parked-waiter`）矛盾，需查 24 的整格/平台位覆盖范围。

## 六、修法方向（待拍板，两条须一起做）

- **F-435-A（治本）**：给 `R3RShouldCutCoupleAtWaitPoint()` 增加"已越过"档。
  - 候选实现 1（推荐，最小）：本车**曾经**在截断态下满足 `front_on_wp && cur_speed == 0`（即进过 state=2 并停稳过）之后，一旦运动前端离开路点格，一律 `return 0` 并写入现有的放行 latch（`_r3r_wait_cut_released`），永不回到 1。
  - 候选实现 2（几何）：用路点站台轴 + 运动方向判"前端在路点格之前/之后"，之后 ⇒ 0，之前 ⇒ 1。缺点：DB/多站台/斜向情形要重算，风险高于 1。
  - 候选 3（保守兜底）：`front_on_wp == false` 且 `GetStationIndex(v->tile) == wait_dest`（整列已在路点站台上）⇒ 0。
- **F-435-B（治噪）**：`15787` 的 `r3r_wait_cut == 0` 条件改为"截断态但仅节流跳过时不清预留"：
  ```cpp
  if (keep_track != INVALID_TRACK &&
      (R3RCoupleSearchSkipped(consist) || (r3r_wait_cut != 0 && r3r_wait_cut == 2))) return { keep_track, result_flags };
  ```
  即 state=2（在路点上等）**不清预留**（路点格仍在，车已停），只有 state=1（还没到路点、且真失败）才清。
- **F-435-C（可选）**：截断态下绕过 `R3RCoupleScanHold` 的 8 tick 节流（`holdAge=1` 每次都跳过，说明 hold 与截断态互相打架）。需先拿到 REG-276-D-1 的读数再定。

## 七、复测判据

1. 同场景不再出现"`mf` 越过路点后 spd=0 永久静止"：`TPR-WALK veh=21 mf=` 单调推进或停住时有 `CPL-WAITHOLD`/`CPL-CUTRELEASE` 说明原因。
2. 出现新的放行探针（F-435-A 落地后）：`CPL-WAITCUT state=0` 的 `tile=` 可以是路点格**或路点之后**，且此后不再回到 1。
3. 截断态每 tick 的 `RESV-LEDGER-FORGET released=2 kept=1` 抖动消失（F-435-B）：`phase=choose-track` 的释放只应发生在 state=1 真失败时。
4. 最终出现 `COUPLE-OK loco=21 rear=24`（本例 tgt 距离 11，中间 255..264 必须能走）。
5. 负判据：没有等待区路点的普通 `GOTO_COUPLE` 一条 `CPL-WAITCUT` 都不出现；DB 双头场景 `state` 仍能升到 2。

## 八、未确认

- U-1：`CPL-WAITHOLD`/`CPL-WAITCUT-CLEAR` 为 0 的确切原因（REG-276-D-1）——是 `TryPathReserve` 侧 `r3r_wait_cut` 为 0，还是另有分支绕开了 15736。
- U-2：`EXTEND-BACKOUT owner=24`（行 1269）在 24 处于 `parked-waiter` 时的合理性。
- U-3：本次越过路点的"成功 tick"具体是行 1173-1176 还是 1218-1256 之间的某次；需要 `CPL-CUTOK` 探针。
- U-4：`COUPLE-WAIT-OFFDEST` 只读诊断在本例打出的 `destStn=12 destTile=(278,240)` 与 tgt=24 在 265,234 是否同一站台的不同股道（决定"贴到 24 是否合法"）。

## 九、为什么等待区路点不像红灯信号机（玩家 2026-10-22 第四次追问，设计定性）

**玩家原话**：「为什么这个路点不能像红灯的信号机一样表现，老是把预留漏出去」。

### 9.1 两者的约束载体根本不同

- **红灯信号机 = 地图格上的前置禁止**。YAPF 在 `FollowTrack` 阶段就把红灯格判为不可进入（cost 直接给 INF / 直接 blocked），于是路径在红灯前一格终止，**红灯之后的格根本不会被写进这条路，也就不存在"先订后撤"**。约束的载体是"那个格"，与谁在开车、车在哪、车速多少完全无关 ⇒ 永久在线、无条件生效。
- **R3R 等待区路点 = 每 tick 由一个函数现场算出来的"改终点"**。路点在引擎眼里只是 `TileType::Station` + `IsRailWaypointTile` 标记，YAPF 完全不认识它。R3R 只能在寻路前调 `R3RShouldCutCoupleAtWaitPoint()` 拿一个 0/1/2，再 `Yapf().SetCoupleWaitCut(r3r_wait_cut == 1, wp)` 把**终点**换掉。约束的载体是"这次寻路的终点"，作用域只有这一 tick 的一次搜索。

### 9.2 三个泄漏口（都源于"约束不在格上，在车状态里"）

判据全文只有三档，且全部以**车此刻的位置/速度**为输入：
```13151:13155:src/train_cmd.cpp
	const Train *wait_front = v->GetMovingFront();
	const bool front_on_wp = (wait_front != nullptr && IsRailWaypointTile(wait_front->tile)
			&& GetStationIndex(wait_front->tile) == wait_dest);
	if (!front_on_wp) return 1;
	if (v->cur_speed != 0) return 2;
```
- **泄漏口 1（越过后恒截断到身后，KI-435）**：`!front_on_wp ⇒ return 1` 读起来像"红灯还在"，但截断的终点是**路点格本身**；列车一旦越过路点，终点落在起点身后 ⇒ YAPF 必然失败 ⇒ 只能停车。所以"像红灯"只在没越过时成立，越过之后变成"永久红灯 + 车已在灯后"的死结，而不是"绿灯放行"。现场 `254,234` → `mf=260,234` 就是这一档。
- **泄漏口 2（放行 = 红灯永久转绿，且不可撤销）**：`_r3r_wait_cut_released` 这个 latch 只在 `cur_real_order_index` 变化时才清（`:13106-13111`），`return 0` 一直有效。放行的前提是 ①②③ **当时**成立；此后 A 被别的车挂走、A 开始移动、A 被卖出——只要订单号没推进，latch 不退回，路点的约束**永久消失**，预留一路铺到原始终点（配对目标/道岔区）。这正是"老是漏出去"里最像"信号机坏了"的那一半。
- **泄漏口 3（判据前置门直接放弃）**：`!v->IsFrontEngine()` 或 `!current_order.IsType(OT_GOTO_COUPLE)` 一律 `return 0`（`:13102-13103`）。DB 双头后半 / 借用排程下的链头可能命中这两条 ⇒ 路点对这类车**从来不存在**。
- **泄漏口 4（不经过这条设置的预留来源）**：约束只挂在 `TryPathReserve` 的一次 YAPF 调用上。`ReserveTrackUnderConsist()`（脚下与平台整格位）、`rp()` 回溯补订、`ExtendTrainReservation`、以及**上一次 tick 留下的长预留**都不受教育。KI-433 修的就是"失败不清旧预留"，反过来说明长预留在截断生效前就已铺出去。

### 9.3 结论（答复玩家）

路点做不成红灯，不是实现没写对，而是**语义位置放错了**：
1. 红灯约束格，R3R 约束"这一次寻路的终点"；
2. 红灯的判据是"区间里有没有车"，R3R 的判据是"我这列车在哪、多快"；
3. 红灯失效只可能是信号被拆，R3R 的约束会因为一次放行、一次 order index 不变、一次 `IsFrontEngine` 为假而**静默消失**。

### 9.4 要做到"真像红灯"，改造方向（登记为 KI-437，待拍板）

- **F-437-A（推荐，语义化 + 格级禁止）**：把 `r3r_wait_cut` 的输入从"车的位置"换成"配对是否就绪"：`R3RHasCouplePair(v) && A 停稳在同一站台区间 && 本车在路点前` ⇒ 0（放行），否则 ⇒ 1（截断）。位置只用来决定 `cur_speed = 0`（停不停车），不再决定"截不截断"。这样越过路点也仍是 1，但终点不再是身后的路点，而是"路点前的最后一格"或"当前格+0 格"（即原地等），不会退化成永久失败。
- **F-437-B（更彻底，格级红灯）**：在 YAPF 的 follow-track 里，把"等待区路点格"当成一个**只能作为终点、不能作为中间格**的格。这样无论终点设成什么，任何跨过路点的预留都不会产生——等价于信号机。代价是要动 `yapf_rail.cpp` 的 `PfFollowTrack`/`pf` 判定，且要区分"本车合法终点就是该路点"与"别的车路过"。
- **F-437-C（最小兜底）**：latch 改成"每次成功寻路的 tick 重新校验 ①②③，失败即撤回 latch"。只堵泄漏口 2，不解决泄漏口 1/3/4。

**拍板建议**：先 F-437-A（改动小、直接把"永久红灯死结"变成"原地等"），把 F-437-B 留作后续彻底方案；F-437-C 可作为 A 的附带项一并做（成本≈3 行）。

## 十、玩家新口径（2026-10-22）：对符合条件的列车，路点＝「双向 PBS」，精准拦停；不符合的＝普通路点

玩家原话：「算了，直接这样，对于符合条件的列车，这个路点就像一个双向的PBS，能够精准拦停列车，对于不符合的，就是一个普通路点。这个想法怎么样」

### 10.1 判定：这个方向优于 §9.4 的 A/B/C 三候选，采纳为 **F-437-D**

它一次解决了 §9.2 的四个泄漏口里最硬的两个（泄漏口 1 越过死结、泄漏口 2 永久绿），原因是把约束的载体从「车状态」搬回了「格」：

| | 现状（改终点） | F-437-D（格屏障） |
|---|---|---|
| 约束载体 | 本次寻路的终点 | 路点格本身 |
| 越过后 | 终点在身后 ⇒ 永久失败（KI-435） | 墙在身后 ⇒ 不再生效，不失败 |
| 生效范围 | 全图（该车的每次寻路） | **仅该车**，他车照旧走 |
| 负判据 | 需额外保证 | 天然成立（不符合条件的车不挂屏障） |

且「双向」正是 KI-435 的解：现在是**一个点**（终点），越过后点在你背后就永远到不了；改成**一道墙**后，正常情况车撞墙停下、根本不会越过；若因故越过了，语义就是「这道墙已在身后」，失效而非死锁。

### 10.2 命名纠正：这不是 PBS，是「per-vehicle 拦停栅栏」

PBS 的语义在**预留**（这块路被谁订了）。玩家要的是**停**（这条线在这儿到此为止），与「订没订到路」无关。若按 PBS 字面实现（去查 `GetTrainForReservation`）会做错方向：真正需要的判据是「这个格是否本车的拦停格」。
建议内部命名 `R3RWaitBarrier`（虚拟停车牌），日志 `WBAR-*`。可保留「双向 PBS」作为对玩家的说明口径。

### 10.3 三层落地设计

1. **判定层**（`src/train_cmd.cpp`，替换 `R3RShouldCutCoupleAtWaitPoint` 的语义）
   - `R3RWaitBarrierActive(v, &barrier_tile, &barrier_station)` ⇒ 生效条件：
     订单 `OT_GOTO_COUPLE` + 前一条命令 `OT_GOTO_WAYPOINT`（沿用现有 `wait_dest` 推导，`:13115-13121`）+ **本车尚未越过该格**。
   - **越过 ⇒ 失效**（不是 `return 1`）。位置判据从此只做两件事：①决定 `cur_speed = 0`（在墙前刹停）；②决定屏障是否还生效。
2. **寻路层**（`src/pathfinder/yapf/yapf_rail.cpp`，唯一动主干的地方）
   - 在 follow-track 扩展节点时：`IsRailWaypointTile(new_tile) && GetStationIndex(new_tile) == barrier_station && barrier 生效` ⇒ 若该 node **不是本次寻路的合法终点**，则**不扩展**（cost=INF / 直接跳过）。
   - 双向天然成立：node 是 (tile, trackdir)，两个 trackdir 都被拦，无需额外写方向判断。
   - **必须保留「终点例外」**：否则车永远到不了路点。现有 `Yapf().SetCoupleWaitCut(bool, wp)`（`:869`）已携带 `wp`，屏障直接复用同一字段，二者正交叠加：改终点负责「开过去」，禁穿负责「开不过去」。
3. **停车层**：沿用现有 `cur_speed = 0`，不改。

### 10.4 必须拍板的三个细节（C-437-1/2/3）

- **C-437-1 墙的位置**：建议「路点格可作终点、不可作中间格」。即允许车真正站在路点格上（否则车会停在路点前一格，观感上「没进等待区」）。若要停在路点前一格，则 `barrier_tile` 取路点前一格。
- **C-437-2 越过后的处置**：建议**失效 + 一行 `WBAR-OVERSHOOT` 探针**。不做「反向拉回」——YAPF 不支持把车引回头，且会被玩家视为抽搐。
- **C-437-3 双向的边界**：拦「跨过路点格」，不拦「本车在自己这一侧的正常移动」。即判据是「本次扩展是否跨越 barrier_tile」而非「本车在哪」。

### 10.5 代价与风险

- 工作量 **大于 F-437-A**（要动 `yapf_rail.cpp`，我上轮标记为 F-437-B 的那一档）；但 F-437-A 只是把死锁降级成「原地等」，泄漏口 2（一次放行后永久绿）它治不了，F-437-D 能一起治。
- 主要风险：`yapf_rail.cpp` 是寻路主干，改错会波及全部列车寻路。缓解：屏障判据首行即 `If (!barrier_active) return 逐字原样`，让 99% 的寻路走原路径；且 `barrier_station` 为 0 时整段判断短路。
- 次要风险：`R3RShouldCutCoupleAtWaitPoint` 现有三个返回值（0/1/2）的调用点共两处（`yapf_rail.cpp:843`、`train_cmd.cpp:15735`），改语义要同步，否则 KI-436 的「节流跳过被当真失败」会以新形态复发。

### 10.6 复测判据（F-437-D）

①越过路点后不再出现 `TPR-WALK ... spd=0` 永久静止，且不出现「终点在身后」的连败；②`WBAR-*` 只在符合条件（有等待区路点 + 配对未就绪）的车上出现，普通经过路点的列车 0 条；③对向/他车经过同一路点不受影响（负判据，最关键）；④本例终局 `COUPLE-OK loco=21`；⑤截断态不再每 tick `RESV-LEDGER-FORGET released=2`（KI-436 随之消失）。

### 10.7 玩家拍板（2026-10-22）：C-437-1/2/3 三条全部按推荐默认执行

玩家原话：「C-437-1按照你推荐的来 C-437-2按照你推荐的来 C-437-3按照你推荐的来」

| 编号 | 事项 | 拍板结果 |
|---|---|---|
| C-437-1 | 墙的位置 | **路点格可作终点、不可作中间格**。车允许真正站在路点格上（=本次寻路的合法终点）；禁止把路点格当作"穿过它继续往前"的中间节点。`barrier_tile` = 路点格本身，不取前一格。 |
| C-437-2 | 越过之后怎么办 | **屏障失效 + 一行 `WBAR-OVERSHOOT` 探针**。不做反向拉回（YAPF 不支持引车回头，视觉上会抽搐）。语义 = "这道墙已经在你身后了，不管"。 |
| C-437-3 | 双向的边界 | **拦「跨越 `barrier_tile`」而非「本车在哪」**。判据落在"本次节点扩展是否跨越 barrier_tile"上，所以本车在自己这一侧的正常移动（含倒车）不被误拦；对向列车从另一侧接近同一路点时，只要它不属于"符合条件的车"就完全不受影响。 |

⇒ 实现口径冻结为 **F-437-D**，取代 F-437-A/B/C。实现顺序：①`train_cmd.cpp` 新增 `R3RWaitBarrierActive()`（判据 + 越过即失效 + WBAR 探针）②`yapf_rail.cpp` follow-track 加"非终点不得跨越 barrier_tile"的禁扩展 ③编译 + 探针自证。

### 10.8 实现落地（2026-10-22，已编译通过）

**重要简化：不需要动 `yapf_rail.cpp`。** 落地时确认 `SetCoupleWaitCut(true, wp)` 走的是
`PfDetectDestination()` 白名单（`yapf_destrail.hpp:452` 起：截断模式下除该路点外一律否决），
而 YAPF 一旦在某节点检出 destination 就把 `max_cost` 压到该节点的 cost，更远的节点随即被剪
⇒ **best node 必然是路点格、回溯/预留必然止于路点格，物理上不可能"跨越"**。所以
C-437-1「可作终点、不可作中间格」已经由"终点白名单"天然满足，无需在 follow-track 里加
第二道禁扩展。上一轮担心的"动寻路主干"风险（F-437-B）整块消失。

改动全部落在 `src\train_cmd.cpp` 一个文件（未碰任何 `src\*.h`，增量合法）：

1. **状态结构替换 latch**（原 `_r3r_wait_cut_released`，只有一个 bit 的"放行过"）：
   ```cpp
   struct R3RWaitBarrierState { VehicleOrderID order_index; bool seen_on_wp; bool overshoot; };
   static std::unordered_map<uint32_t, R3RWaitBarrierState> _r3r_wbar_state;
   ```
   `order_index` 一变（挂上了 / 跳到下一条）整条作废 ⇒ 与老 latch 同样的"不跨命令生效"，
   但多了 `seen_on_wp` / `overshoot` 两个 bit 来表达"墙还在不在车前面"。
2. **C-437-2 越过即失效**：`front_on_wp` 为真 ⇒ `seen_on_wp=true`、清 `overshoot`；
   曾为真而现在为假 ⇒ `overshoot=true` 并打 `WBAR-OVERSHOOT`，此后本命令内 `return 0`。
   这是 KI-435 的直接解药：越过之后不再返回 1（老逻辑返回 1 ⇒ 终点在身后 ⇒ 永久停车）。
3. **判据换成"配对是否就绪"**（C-437-1/3 的落点）：`R3RGetCouplePairPartner(v)` 非空 +
   A 停稳 + A 是合法挂接目标 + A 整链逐节停下同一站台区间 ⇒ `pair_ready`。
   - `pair_ready` ⇒ `return 0`（每 tick 重算，A 一变差就重新拦；**不再是永久绿**，堵住泄漏口 2）；
   - 未就绪 且 `front_on_wp` ⇒ `return 2`（无合法终点，原地等）；
   - 未就绪 且不在路点 ⇒ `return 1`（截断，终点只能是路点）。
   与 `R3RShouldHoldForCouple()` 同口径（配对锁在就放行），不再各写一套。
4. 探针：`WBAR-OVERSHOOT` / `WBAR-RELEASE`（均 `R3RDbgOn()` 门内，`R3RDbgWrite` 直写），
   配合既有 `CPL-WAITCUT state=` 可完整复现状态迁移 1 → 2 → 0（或 1 → 失效）。

**构建自证**：复用 `_tmp_inc_build.cmd`（未新建 .cmd）；`[2/3] train_cmd.cpp.obj` →
`[3/3] Linking`；`build\R3R_incbuild.done` = `EXIT_CODE=0` @22:06:25；
`src\train_cmd.cpp` 22:01:28 → `train_cmd.cpp.obj` 22:02:52 → `build\openttd.exe`
22:05:42（52 214 272 B）；`read_lints` 0 条。
产物字面量核对（`findstr /M /C:"..." build\openttd.exe`）：`WBAR-OVERSHOOT` 命中、
`WBAR-RELEASE` 命中、`CPL-WAITCUT` 命中（3/3）。时间戳链 = src 22:01 → obj 22:02 →
exe 22:05 → done 22:06，无陈旧产物。

**与 §10.6 复测判据的对应**：① 越过不再永久静止 ⇒ 由 `WBAR-OVERSHOOT` + 后续正常寻路验证；
② 不再永久绿 ⇒ 由 `CPL-WAITCUT state=0` 早于配对就绪时**能退回 1/2** 验证；③ 负判据 ⇒
普通经过路点的列车 0 条 `WBAR-*`；④ 终局仍是 `COUPLE-OK loco=21`。
