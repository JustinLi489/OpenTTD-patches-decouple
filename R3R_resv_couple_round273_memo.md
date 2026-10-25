# 第 273 轮：第 272 轮四项修复的实机复测失败（先落盘，后取证）

## 一、玩家原话（2026-10-22）

> 「虽然我们的饼画的很漂亮，但是实际做的确实是难以言说，首先是神秘的预留问题，
> 其次是挂车的车还能蠕动，倒是等挂的车卡住了」

三条报告：
1. **神秘预留问题**（幽灵/孤儿轨道预留）——第 240/243 轮修过、仍复现。
2. **挂车的车还能蠕动**——主动挂车方（coupler）仍以低速蹭动，没有按 C4-1′ 停住。
3. **等挂的车卡住**——被动等待方（waitee）反而卡死不动。

## 二、输入日志

- 路径：`D:\temp\codebuddy-dropped-files\db7dc58b-d9e6-4125-b40c-889f91b413c8\R3R_debug.log`
- 规模：**553 887 B / 8 057 行**
- 产生者：待核对（应为第 272 轮 exe `build\openttd.exe @2026-10-22 14:01:13`，
  即含 C4-1′ / C4-3 / C-6 / C-1′ 的内测探针版）

## 三、首轮计数（`Select-String -SimpleMatch` 全日志）

| 探针 | 命中次数 | 判读 |
|---|---|---|
| `CPL-HOLD `（C4-1′ 心跳） | **0** | **C4-1′ 判据一次都没成立** |
| `CPL-HOLD-NOBOOK`（C4-3 门禁） | **0** | **C4-3 门禁一次都没触发** |
| `CPL-NOTREADY`（C-6 未就绪） | **1** | C-6 至少命中过一次 |
| `CPL-PATHBUG`（C-6 真失败） | **0** | 没有"配对已锁 + 闸门拒绝"的签名 |
| `COUPLE-OK` | 1 | 本段只成功挂上 1 次 |
| `COUPLE-FAIL` | **0** | 一条都没有——与"挂车方蠕动、等待方卡住"并存，很反常 |
| `CPL-PAIR` | 4 | 配对层只打印 4 行 |
| `CPL-SKIP` | 1 | 节流跳过 1 次 |
| `RESERVECONSIST` | 24 | 脚下占用保护仍在跑 |
| `SPD-DROP` | 15 | 有 15 次"非零→零"的速度坠落 |
| `REVERSEDIR` | 1 | 一次调向 |
| `CPL-BEST` | **0** | 挂接寻路根本没找到过最佳终点（或探针被抑制） |
| `PFD-REJ*` | **0** | B 层闸门一次都没拒绝过候选 |

## 四、疑点（D-273-x）

- **D-273-1（C4-1′ 完全没触发）**：`CPL-HOLD`/`CPL-HOLD-NOBOOK` 双零命中，说明
  `R3RShouldHoldForCouple()` 在本场景恒为 false。要么判据形状与实际订单布局不符，
  要么它根本没被调用到。**这是本轮第一优先要证的。**

- **D-273-2（`COUPLE-FAIL` 零命中却"挂车方蠕动"）**：上一轮日志里该探针是每 tick 刷的；
  本次一条都没有，说明 `TrainCoupleHandler` 可能**根本没进 u==nullptr 分支**（即配对层
  已找到配对、在走"有目标"的另一支），于是所有"C-6 未就绪/真失败"的日志面都不覆盖它。
  与 D-273-1 同源：判据/探针都假设了另一种订单形态。

- **D-273-3（等待方卡住）**：`CPL-NOTREADY` 只 1 行，且没有 `CPL-BEST`/`PFD-REJ`。
  "卡住"的机制尚未定：是 KI-316 的清预留+stuck，还是 C-6 新加的零速，还是等待方
  自己在 `WAIT_COUPLE` 上永不订路（KI-208 的 `RESV-NOBOOK`）导致挂车方永远走不到？

- **D-273-4（神秘预留）**：本轮日志没有任何 `RESV-AUDIT*` / `RESV-REAP` / `RESV-PURGE`
  命中（**待复核**，本轮只统计了 13 个关键字，未含 RESV-*）。若确实为零，则
  "神秘预留"来自**未被审计覆盖的整格位（站台/路点）或读档带入的存量**，
  而不是本会话新泄漏的 plain rail 位。

## 五、猜测根因（待验证）

- **G-273-A（首选，解释 D-273-1 + D-273-2）**：`R3RShouldHoldForCouple()` 取"上一条订单"
  的写法 `prev_index = cur_real_order_index - 1` 与实际索引语义不符。若车停在路点上时
  `cur_real_order_index` **仍指向路点订单本身**（订单要等列车驶离才推进），则 `prev`
  永远不是 `OT_GOTO_WAYPOINT`，判据恒假；同时也解释了为什么挂车方在**还没有配对**时
  就继续往前蹭（C4-1′ 没拦住）。
  **验证判据**：日志里找到该车的 `ORD-*` / `real=` 读数，确认停车瞬间 `real` 指向
  路点订单（`type=1` 的 waypoint）还是 `type=16`（`OT_GOTO_COUPLE`）。

- **G-273-B（解释 D-273-3）**：等待方被 KI-208 的 `RESV-NOBOOK` 门禁按住（停稳 + 站台格 +
  `WAIT_COUPLE`/car-only ⇒ 拒绝订路），而挂车方的新路线需要穿过它原来占的格位；
  两边互等 ⇒ 双双卡住。挂车方"蠕动"是因为它还在按旧的 `keep_track` 预留 + 逐 tick
  重试向前挤。
  **验证判据**：日志里应见 `RESV-NOBOOK veh=<等待方>`，且该车格位在挂车方 `RP cur=` /
  `CPL-BEST` 的路线上。

- **G-273-C（解释 D-273-4）**：神秘预留是**整格位（站台/路点）**的孤儿占用（KI-207c 家族），
  审计分支要求"格上无车 + `GetTrainForWholeTileReservation()` 为空"，而等待方正停在
  那个路点上 ⇒ 审计认为"有主"，不 reap；但所有者其实永不离开 ⇒ 显示为神秘预留。
  **验证判据**：`RESV-AUDIT-WATCH ... kind=WAYPOINT reserved=1 owner=vehN`。

## 六、取证批次 1~2（日志锚点逐条）

| 行 | 原文（节选） | 判读 |
|---|---|---|
| 121 | `RESV-AUDIT-ARM tick=2338 interval=2048 due=0 pending=0` | 审计只 **arm** 了一次，全日志再无 `RESV-AUDIT-SUMMARY`/`STRAY`/`REAP` ⇒ **审计面完全不可见**（与 REG-243-A 同族：暂停使 `_tick_counter` 不前进） |
| 126/144/147/150 | `CPL-PAIR tick=2338 act=21 tgt=24 dist=0 actTile=1889,188 tgtTile=1889,188` | 开局 21 与 24 同格配对（dist=0） |
| 163 | `COUPLE-OK loco=21 rear=50 consist=24 co=0 real=1 type=1 tx=1889 ty=188 x=30234 y=3016` | 唯一一次成功挂接 |
| 232 | `CHAIN-ATTRS head=21 n=48 FE=2 SEG=2 pow=57932 wt=2352 len=192 spd=428` | 合并后 48 节、2 段、速度 428 |
| 296 | `CHAIN-ATTRS2 couple head=21 len=192 pow=57932 wt=2352 spd=428 nocab=0 db=0` | 耦合后属性健康 |
| 3993 | `RESV-LEDGER-FORGET veh=21 why=decouple mask=0x3 released=3 kept=14 phase=misc` | 解挂发生（第 1 次） |
| 6070 | `RESV-LEDGER-FORGET veh=24 why=free-path mask=0x1 released=3 kept=14 phase=misc` | 24 侧也有一次带 14 kept 的清理 |
| 7191 | `CPL-ENTRY veh=21 tile=205,274 orderType=16 dontReserve=0` | 21 在 205,274 开始 GOTO_COUPLE |
| 7280~7291 | `RESV-GC veh=21 tile=228,236 … 258,233 why=free-path` + `UNRES-RAIL tile=254,233 phase=gc-stn` | 沿 y=233 一长串预留被 GC |
| 7292 | `RESV-LEDGER-FORGET veh=21 why=free-path mask=0x1 released=95 kept=0 phase=choose-track` | **一次性放掉 95 格**（第 271 轮判据里担心的形态复现） |
| 7293 | `CPL-NOTREADY veh=21 tile=205,274 real=12 spd=0 stuck=0` | **C-6 未就绪分支确实命中过 1 次** |
| 7294 | `SPD-DROP veh=21 tile=205,274 prev=428 order=6 real=11 stuck=0 fp=0 prog=181 wait=0 parked=0 stop=0 track=02 dtile=479486` | 428→0 硬停；当时 `order=6`（`OT_GOTO_WAYPOINT`）、`real=11`，**next tick 才是 real=12 type=16** |
| 7295 | `DEPOT-ARR veh=21 spd=0 real=11(6) curType=6 … destTx=254 destTy=234 … tileEqDest=0` | 停点 205,274 距目的地 254,234 还很远 ⇒ **不是到达路点后被按住**，是半路被按停 |
| 7316 | `CPL-SKIP veh=21 tile=205,273 orderType=16 dontReserve=0 retryIn=6 holdAge=2` | 挂接搜索进入节流（`holdAge=2`） |

**日志尾部终局形态**（末 40 行）：

```
LOCO-MOVE veh=21 tile=248,234 j=192 spd=2 track=01 stuck=0
DEPOTBLK veh=21 tile=248,234 track=01 eng=1 isdepot=0 ontile=0 idmatch=0 pend=0 stopped=0 parked=0 spd=2 destRaw=12 depotIdx=4294967295
TRP veh=24 mf=216,235 real=10 type=1 dest=473353 spd=0 fp=0 look=0 one=0 mstuck=0 fto=0 => ok=0 res=0
EXTEND-BACKOUT veh=24 origin=216,235/8 stop=219,235/8 next=220,235 reason=reserve-fail err=0 owner=-1 whole=0 track=-1 need=0x0 resv=0x0 led=-1
TPR-WALK veh=24 mf=216,235 origin=216,235 td=8 okay=1 exit=2 new=217,235 reach=1 spd=0 one=0 look=0
```

- **"挂车的车还能蠕动"实锤**：`veh=21 j=192 spd=2`——目标速度 192，实际速度 2，**每 tick 被削到 2**，
  且已从 205,274 挪到 248,234（走了约 50 格）。这不是"没停"，是"每 tick 起一下又被按住"。
- **"等挂的车卡住了"实锤**：`veh=24` 每 tick `EXTEND-BACKOUT reason=reserve-fail`，
  `owner=-1 whole=0 need=0x0 resv=0x0` ⇒ **失败时那格既无预留也无主人**，`TRP ok=0 res=0`；
  它要去的 `dest=473353`（type=1 车站）走不动。

## 六之二、取证进度

- [x] 日志规模 + 13 关键字计数（§三）
- [x] 定位实际订单形态（G-273-A）⇒ §六 行 7293/7294/7295/7316，判定：G-273-A **成立**
- [x] 挂车方/等待方车号与位置（G-273-B）⇒ veh=21 机车（挂车方）、veh=24 纯车底 24 节（等待方）
- [x] `RESV-GC … why=free-path-gone` 全家族检索 ⇒ 全日志仅 4 条，全在 y=235（§7.3）
- [ ] RESV-* 全家族计数（D-273-4）
- [ ] COUPLE-OK 上下文（唯一成功那次的形状）

## 七、结论（第 273 轮，2026-10-22）

### 7.1 三条现象归到两个根因（高置信，可直接落地）

| 现象 | 根因 | 铁证（日志行号） |
|---|---|---|
| ② 挂车方蠕动 | **C-6「配对未就绪」硬停作用在"前瞻订单"上** | `:7293 CPL-NOTREADY veh=21 real=12 spd=0` vs `:7294 SPD-DROP veh=21 prev=428 order=6 real=11`、`:7295 DEPOT-ARR destTx=254 destTy=234 tileEqDest=0` |
| ③ 等待方卡住 + ① 神秘预留（**同源**） | **ledger 释放路径无法释放隧道/桥（`TileType::TunnelBridge`）⇒ 在位残留、账本已空** | `:7695/7696/7699/7700 RESV-GC veh=24 tile=220,235 / 223,235 / 226,235 / 230,235 track=0x0 why=free-path-gone` + `:7766…8053 EXTEND-BACKOUT veh=24 stop=219,235 next=220,235 reason=reserve-fail owner=-1 led=-1`（40+ 次） |

### 7.2 根因 R-273-1：C-6 判据看错订单层（解释现象②）

- `ChooseTrainTrack()` 内 `orders.AdvanceOrdersFromVehiclePosition()` 会把 `current_order` 推进到"下一条"。当前订单是 `OT_GOTO_WAYPOINT(6)`（通过式路点）时，函数体内看到的 `current_order` 已经是下一条 `OT_GOTO_COUPLE(16)` ⇒ 日志里同一瞬间 `CPL-NOTREADY real=12 / type=16` 而 `SPD-DROP order=6 real=11`（`:7293` vs `:7294` 相差 1 行、同一 tick）。
- C-6 的 `!R3RHasCouplePair()` 硬停分支用的就是这个**前瞻态**，于是在车离路点 `254,233` 还有约 50 格（`:7295 tileEqDest=0`）时把 `cur_speed` 428→0。
- 挂接扫描随后进入节流（`:7316 CPL-SKIP … retryIn=6 holdAge=2`），每 ~8 tick 才真扫一次 ⇒ 每 8 tick 归零一次 ⇒ 玩家看到的"蠕动"（日志尾 `LOCO-MOVE veh=21 tile=248,234 j=192 spd=2`：目标 192、实际 2）。
- **修法 K-273-1（待拍板）**：在 `ChooseTrainTrack()` 入口、任何 `AdvanceOrdersFromVehiclePosition()` 之前采样 `const bool r3r_entry_couple = consist->current_order.IsType(OT_GOTO_COUPLE);`；C-6 硬停仅在 `r3r_entry_couple` 为真时生效，前瞻态命中时按 `CPL-SKIP` 同款放行（保留 `keep_track`、不动速度、不释放预留）。

### 7.3 根因 R-273-2：ledger 释放不覆盖隧道/桥（解释现象①+③）

- `R3RReleaseReservationLedgerChecked()`（`pbs.cpp:540-554`）判"这格还是我的"用
  `GetTileType(tile)==TileType::Railway && IsPlainRailTile(tile) && HasTrack(tile,track) && HasBit(GetRailReservationTrackBits(tile),track)`。
  隧道/桥是 `TileType::TunnelBridge` ⇒ 恒假 ⇒ 落到 `:551 R3RResvLedgerDropBit()`：**只删账本行、不调 `UnreserveRailTrack()`**。这与该函数 `:480-486` 注释自称必须避免的"留下 ownerless 位（KI-363/364）"逐字相反。
- 现场签名：`why=free-path-gone` 全日志**仅 4 条**，都在 y=235 的 `220/223/226/230`；而 `221,222` 与 `227,228,229` **完全没有账本行**（被跟随器整段跳过）⇒ `220↔223`、`226↔230` 是两座隧道/桥的两端。
- 后果链：这两笔是 yapf `rp()` 订的、不在 `consist->path` 上 ⇒ `FreeTrainTrackReservation` 的路径清扫够不到；ledger 又不释放 ⇒ 位上残留、账本已空（`owner=-1 led=-1`）⇒ `ExtendTrainReservation` 走到 `220,235` 时 `TryReserveRailTrackdir` 失败（`TryReserveRailTrack` 对 TunnelBridge 要求 `!HasTunnelReservation` / `TryReserveRailBridgeHead` 成功）⇒ `reason=reserve-fail` 永久循环（`train_cmd.cpp:14607`）。
- 且 KI-361「自家链已订即算成功」豁免（`pbs.cpp:924`）门禁是 `IsPlainRailTile(tile)` ⇒ 隧道/桥**拿不到豁免，无法自愈**。
- **修法 K-273-2（待拍板）**：
  - (a) `R3RReleaseReservationLedgerChecked()` 增加非 plain-rail 分支：`TileType::TunnelBridge`（`GetTunnelBridgeTransportType==TRANSPORT_RAIL` 且 `HasTunnelReservation`/`HasBridgeReservation`/`IsTrackAcrossTunnelBridge` 成立）、`TileType::Road && IsLevelCrossing`、`TileType::Railway && IsRailDepot`、以及**带信号的 Railway**（`IsPlainRail` 而非 `IsPlainRailTile`），一律走 `UnreserveRailTrack()`——该函数（`pbs.cpp:1058-1124`）已完整覆盖 TunnelBridge / 道口 / 站台 / 车厂 / plain rail。
  - (b) KI-361 豁免把 `IsPlainRailTile(tile)` 放宽为"该格确实存着我这一位"（`GetReservedTrackbits(tile)` + `R3RTrainForLedgerBit`/`GetTrainForWholeTileReservation` 判归属），使隧道/桥/道口/站台也能"已订即成功、顺手点绿"。
  - (c) 审计面（`R3RReservationAudit`）补 TunnelBridge/道口/车厂扫描，否则同族问题永远看不见（`RESV-AUDIT-ARM` 全日志仅 `:121` 一条、无 `SUMMARY/STRAY/REAP`）。

### 7.4 玩家答复（2026-10-22，按编号回填）

> 本节为玩家原话回填，**编号对应上一版三问，问法不改**，只填答案与判定。

- **U-273-1（y=235 上 `220~223` 与 `226~230` 是隧道还是桥）**
  - **玩家原话**：「都是桥(两段都是跨河/跨路桥)」。
  - **判定：R-273-2 成立，对象＝桥（非隧道）**。`220↔223` 跨 4 格（中间 221/222）、`226↔230` 跨 5 格（中间 227/228/229）；桥在**地图上只有两端格**是 `TileType::TunnelBridge`，中间是桥面不占地图格 ⇒ 与「中间格没有任何账本行」逐字吻合，本疑点关闭。
  - **对修法的影响**：K-273-2 (a) 的 `TileType::TunnelBridge` 分支保持不变（`UnreserveRailTrack` 内部按 `IsBridgeTile` 分发到桥头释放）；复测时要看的是 **桥两端格成对出现/成对释放**（`HasBridgeReservation` / `TryReserveRailBridgeHead`），而不是隧道字段。
- **U-273-2（`CLEAR-PATH veh=24 … actor=50` 里的 `actor=50` 是谁）**
  - **玩家原话**：「不知道这个actor是谁，但是50号车是属于veh24的，而目前全图就两列车」。
  - **判定：释放者是自己，不是第三方抢位**。`actor=50` 是 `veh=24` 这条链的**车节**，全图只有 `veh=21`（挂车方机车）与 `veh=24`（等待车底）两列车 ⇒ 不存在"别人的预留被我们清掉"。
  - **由此产生的新疑点**：这批位横跨 `x=80…256`（远超 `veh=24` 停在 `216,235` 的脚下几格），却挂着 `actor=50` 这个**节号**；而 `RESV-GC veh=24 …` 打的是**链头号**。⇒ 订/放两侧的账本 key 可能不是同一辆车（详见 §7.5 的 **D-273-4**）。
- **U-273-3（复跑时是否长时间暂停）**
  - **玩家原话**：「忘记了，但是这个暂停主要是想要看什么，审计吗，如果是我可能得麻烦你改探针了」。
  - **判定：REG-243-A 仍不可判**（日志里没有 tick 证据）。**并直接回答玩家"暂停是为了看什么"**：暂停只影响一件事——审计臂 `R3RReservationAudit()` 按 `_tick_counter` 每 **2048 tick** 跑一次（未暂停约 1.2 分钟），暂停时计数器不前进 ⇒ 本次日志只留下 `:121 RESV-AUDIT-ARM` 一条，没有 `SUMMARY/STRAY/REAP`，所以"审计面看不见"不能当结论。
  - **给玩家的两条选项（需拍板）**：
    - (i) 复跑时**不暂停**、连续跑 ≥ 5 分钟（≈ 8192 tick），审计自然出 4 条 SUMMARY；
    - (ii) 我加一个**控制台命令**（如 `r3r_resv_audit`）当场跑一次全图审计并立刻打印 ⇒ 不必靠"暂停多久"来凑节拍。代价：需要给 `R3RReservationAudit` 开一个外部入口，大概率要动 `src\pbs.h` 或 `src\train_cmd.h` ⇒ **触发全量重编（约 20 分钟）**。建议 (ii)，但等玩家点头再做。
- **现象①补充（玩家这一轮的细化，原话）**：「第一个是有列车预留超过路点但是又在岔道停止，第二个是有预留被神秘销毁」。
  - **判定：现象① 拆成 ①a 与 ①b，两者是同一批位的两种显形**：
    - **①a「预留越过路点、车却停在岔道前」** = R-273-2 的下游。`EXTEND-BACKOUT veh=24 origin=216,235 stop=219,235 next=220,235 reason=reserve-fail`（`:7766…8053`，40+ 次）：预留面想继续向前推（越过路点、往桥头去），桥头格订不上 ⇒ 回退一格并停车。玩家看到的"超过路点"的那一段，正是 yapf `rp()` 补订、**不在 `consist->path` 上**的位。
    - **①b「预留被神秘销毁」** = `CLEAR-PATH veh=24 … actor=50`（`:7296…7331`，x=80…256 逐格释放）。能释放的（plain rail、x=80…256）被一路清掉 ⇒ 视觉上"啪一下没了"；释放不掉的（桥头，y=235 的 `220/223/226/230`）留在地图上 ⇒ 视觉上"神秘预留"。
    - ⇒ **①a 与 ①b 不是两个 bug，而是同一个"预留面越界 + 释放面不覆盖桥"的两面**：清得掉的部分显形成"神秘销毁"，清不掉的部分显形成"神秘预留"。

### 7.5 答复对根因/修法的影响 + 新疑点（D-273-4/5）

- **R-273-2 收敛**：从"隧道/桥二选一"收敛为**桥**。修法 (a) 的代码位置与分支不变，仅把校验对象写成桥（`IsBridgeTile` / `HasBridgeReservation` / `TryReserveRailBridgeHead`）。复测判据 #4 补充为「桥两端格必须成对释放／成对豁免，不再出现单端残留」。
- **D-273-4（新疑点，高，待取证）**：账本 key 与真实所有者可能错配。
  - 期望：`R3RReserveTrackUnderConsist()` / yapf `rp()` 记账用的 key == 释放/GC 查表用的 key == 同一辆链头。
  - 实测矛盾：`RESV-GC veh=24 tile=220,235 … why=free-path-gone` 用**链头 24**；`CLEAR-PATH veh=24 … actor=50` 用**节号 50**；而 `x=80…256` 这一批位的量级远超 `veh=24` 脚下的几格。
  - 为什么存疑：若订侧记在节号、放侧按链头查（或反之），"自己订的位自己找不到"本身就能独立造成"预留被神秘销毁"与等待方永久卡死，与 R-273-2 叠加后现象会更像"随机乱来"。
  - 还需的数据：`src\train_cmd.cpp` 的 `R3RReserveTrackUnderConsist()` 与 `src\pbs.cpp` 的 `rp()` 记账时传的 `key`（是 `consist->index` 还是当前遍历节 `u->index`），以及 `R3RReleaseChainReservations()`/`R3RGcConsistReservations()` 查表用的 key。
- **D-273-5（新疑点，中，待取证）**：「预留超过路点」是合法 PBS 行为（点到下一安全等待位）还是 `rp()` 越界补订？
  - 判法：在 `CPL-TRACE`/`RP cur=` 序列里找**最后一格**与路点格 `254,233` 的空间关系；若最后一格越过路点且 `TryPathReserve` 的 `origin` 也在路点之后，属正常；若路点之前就已停（`stop=219,235`）却仍有位在其右侧，属 `rp()` 越界，与 KI-346/KI-345 同族。
- **审计可见性（U-273-3 的下游）**：无论选 (i) 还是 (ii)，**KI-415 修好前审计臂必须能看见桥/道口/车厂**，否则修完也无法验证（K-273-2 (c) 保持必做）。

### 7.6 审计到底在查什么 / 我需要哪个读数（回答玩家 2026-10-22 提问）

> 玩家原话：「这个审计是看什么的，你又需要看什么」。此节即答复存档。

- **它查什么（`R3RReservationAudit(tag)`，`src\train_cmd.cpp`）**：
  1. 扫全图**带轨道预留位的格子**（plain rail 的 `GetRailReservationTrackBits`）；
  2. 对每一位反查"谁订的"——`GetTrainForReservation(tile,track)` / `GetTrainForWholeTileReservation(tile)`；
  3. 打三行：`RESV-AUDIT-SUMMARY <tag> reservedTiles=N owned=K strayTiles=M`、`RESV-AUDIT-STRAY tile=x,y track=…`（无主位点名）、`RESV-AUDIT-OWNER … veh=<号>`；
  4. 连续 `R3R_RESV_REAP_AUDITS=3` 次仍无主 ⇒ `RESV-REAP` 真释放（兜底清理）。
- **它不查什么（别指望它）**：不判断"这条预留在路线上合不合理"、不核对账本 key 对不对、不告诉你预留为什么订歪；它只回答一个二值问题——**这个地图位现在有没有车认领**。
- **我需要的就三个数**：
  - `reservedTiles`（全图预留位总量，做基线）；
  - `strayTiles`（无主位数量）+ `RESV-AUDIT-STRAY` 点名的格坐标（**修复前应当正好点名 `220,235` / `223,235` / `226,235` / `230,235` 四个桥头格；修复后归零**）；
  - `RESV-AUDIT-OWNER` 行里的 `veh=` 号（若该位查到的主人是 50 而我的账本 key 是 24，就是 **KI-416** 账本 key 错配的直接铁证）。
- **为什么这次读不到**：2048 tick/次 × 3 次 ≈ 6144 tick 未暂停才 reap；本日志只有 `:121 RESV-AUDIT-ARM` 一条，说明节拍没走够（暂停或时长不足），所以"审计面看不见"不能当结论。
- **三种取数方式（待玩家拍板）**：
  - (i) 复跑不暂停、连续 ≥5 分钟；
  - (ii) 我加控制台命令 `r3r_resv_audit` 当场跑一次并打印（需开外部入口，大概率动 `.h` ⇒ 全量重编约 20 分钟）；
  - (iii) 折中：把审计改成**事件触发**（读档后首 tick + 首次出现 `EXTEND-BACKOUT reason=reserve-fail` 时各跑一次），纯 `train_cmd.cpp` 改动、走增量、无需玩家配合。

### 7.7 新需求：预留"非原生"产生/销毁一律打日志（玩家 2026-10-22 提出）

> **玩家原话**：「能不能这样，当预留以非上游原生OPENTTD的方式产生或销毁的时候，打印日志。当然，这可能会导致日志暴涨，可能需要清探针」

- **需求解析**：把"预留位变更"按两个正交维度分类，只记录 **{产生, 销毁} × {非原生}** 这 2 类：
  - **原生** = 上游 JGRPP 本来就有的路径（`TryPathReserve` 的 yapf 走 yapf-tracerestrict / `FreeTrainTrackReservation` / `ClearPathReservation` / `TryReserveRailTrack` / `UnreserveRailTrack` 的内部实现）；
  - **非原生** = R3R 自己加的路径（链编辑清扫、GC 账本对账、审计 reap、purge、yapf `rp()` 的 R3R 补订/越界补订、耦合/解挂/车库编辑提交点）。
  - 目的：把"神秘的预留 / 神秘的销毁"（①a/①b）从"靠事后审计猜"变成"发生的那一刻就点名到调用点"。
- **技术难点（必须先讲清）**：所有预留位最终都落到同一批 setter（`rail_map.h` 的 `SetTrackReservation`、`station_map.h` 的 `SetRailStationReservation`、`bridge_map.h` 的 `SetBridgeReservation`/`SetTunnelReservation`），**R3R 的非原生释放也是走同一个 setter** ⇒ 光在 setter 层埋点无法区分调用者，必须额外携带"谁在改"的上下文。
- **两个落地方案（待拍板 C-273-4）**：
  - **方案 A（setter 层 + 上下文标志）**：加一个进程级 `static bool _r3r_resv_native_ctx`（或一个 tag 全局），在原生入口（`TryPathReserve` / `FreeTrainTrackReservation` / `ClearPathReservation` 入口）置 true、退出清 false；四个 setter 内判断 `!_r3r_resv_native_ctx` ⇒ 打 `RESV-NONNATIVE op=set|clear site=<tag> tile=x,y track=n`。
    - 优点：**自动全覆盖**，以后新加的非原生路径也不用逐个补埋点（正是玩家要的"凡是非原生就打印"）。
    - 代价：动 `src\rail_map.h`/`station_map.h`/`bridge_map.h`/`tunnel_map.h` ⇒ **触发全量重编约 20 分钟**；且 setter 是热路径（每格每次预留都调），即便有 `if` 也要确认开销可接受（内联 bool 判断，实测影响应可忽略）。
  - **方案 B（调用点显式埋点）**：只在 R3R 自己那 ~8 个非原生调用点各打一行带 `site=` 的日志，setter 不动。
    - 优点：纯 `.cpp`，走增量（约 2 分钟），零热路径开销，日志天然带业务语义（`site=gc-ledger` / `site=audit-reap` / `site=chain-clear` / `site=rp-extend`）。
    - 代价：**新增非原生路径必须记得补埋点**（漏一个就又回到"神秘"）；且无法覆盖"我们不知道存在"的第三方改动。
- **两方案共同的配套（必须一起做，否则日志必暴涨 —— 玩家已预见）**：
  - **开关**：`R3R_RESV_TRACE`（默认 0/1 可配），关掉即零日志；
  - **节流**：按 `(site, tile, track, op)` 做**首现 + 计数**（`RESV-NONNATIVE … n=1` → 同类后续只累加，收尾打 `RESV-NONNATIVE-SUM site=… op=… n=…`），复用既有 `R3RDbgEdge` 边沿/计数思路；
  - **白名单**：可由玩家指定只关心某几格（如桥头 `220,235/223,235/226,235/230,235` + `x=80…256,235`），其余一律只计数不逐行；
  - **清探针**：先统计现有 `build\R3R_debug.log` 各标签行数，砍掉刷屏大户（预期候选：`RESERVECONSIST ok=0 resNow=1` 每 tick 每车 8 行、`EXTEND-BACKOUT` 40+、`CLEAR-PATH` 逐格、`CPL-PAIR/COUPLE-FAIL` 每 tick），把日志预算让给新探针。
- **我的推荐**：**A + 节流 + 白名单**。理由：玩家的原话是"**以非原生方式**产生或销毁就打印"——只有 A 能保证"不漏"，B 依赖我预先想全所有路径，而 KI-415/416/417 恰好证明"我以为已经覆盖的路径"并不全。若坚持不改 `.h` 走增量，则退 B 并接受漏报。
- **待拍板**：
  - **C-273-4**：选 **A（全覆盖，全量重编 ~20 min）** 还是 **B（增量 2 min，可能漏报）**？
  - **C-273-5**：日志策略选 **首现+计数**（推荐）还是**逐条全量**（仅在明确复查单格时用）？白名单要不要先限定在 y=235 与 `x=80…256`？
  - **C-273-6**：清探针授权——是否允许我按统计结果**直接删改**既有刷屏探针（会改变后续日志的行数与格式，老日志的 grep 脚本可能失效）？

### 7.8 取证结论：这套基础设施**已经存在**（§7.7 的 A/B 两方案作废）

> 2026-10-22 取证。**§7.7 里"方案 A 需要动 4 个头文件 + 全量重编"的判断是错的**，原因见下；本节取代之。

**证据 1：`src\r3r_resv_probes.h`（全文 57 行）就是为这个目的写的**

- 头注释自陈："Rather than instrumenting the ~20 call sites of the reservation setters (and missing the ones added later), the probes sit inside the six map accessors that own the reservation bits -- the setters below are the ONLY writers of those bits, so **the trace is complete by construction**"。
- 6 个 setter 全覆盖：`SetTrackReservation`（plain rail）、`SetDepotReservation`（车库）、`SetCrossingReservation`（道口）、`SetRailStationReservation`（站台/路点）、`SetTunnelReservation`（隧道）、`SetBridgeReservationTrackBits`（**桥头**）。
- 统一出口 `R3RResvWatchLog(kind, tile, track)`（`src\pbs.cpp:678-694`），kind 形如 `SET-RAIL` / `CLEAR-STN` / `CHG-BRIDGE`，并带 `phase=` 与 `actor=` / `head=`。
- ⇒ 我在 §7.7 提议的"方案 A（setter 层 + 上下文标志）"**已经实现**（KI-207，2026-09-25），不需要新写，更不需要动头文件。

**证据 2：销毁侧其实**全量**在打日志，刷屏大户就是它**

- `build\R3R_debug.log` 共 **8057 行**，标签频次 TOP：

| 行数 | 标签 | 位置 | 语义 |
|---|---|---|---|
| **3207** | `UNRES-RAIL` | `src\pbs.cpp:1068-1072` | `UnreserveRailTrack()` 内层，条件 `GetReservedTrackbits(tile) != TRACK_BIT_NONE`，**无条件全量打**，带 `phase=` + `actor=` |
| **3190** | `CLEAR-PATH` | `src\train_cmd.cpp:14113-14117` | `ClearPathReservation()` 入口，同一条件、同一批事件，**带 `veh=` + `td=`** |
| 398 | `LR-TRACE` | — | 路径 |
| 194 | `ORD-IDX-WATCH` | — | 订单索引 |
| 190 / 152 | `RESV-GC` / `RESV-REL` | `pbs.cpp` | R3R 账本释放 |
| 52 / 24 / 23 | `EXTEND-BACKOUT` / `RESERVECONSIST` / `TRP` | — | ①a 现象的既有证据 |

- ⇒ **`CLEAR-PATH` 与 `UNRES-RAIL` 是同一个释放事件的"入口层"与"setter 层"两行**，相加 6397 行 = **占日志 79.4%**，纯重复。这是"日志暴涨"的真正来源，也是清探针的第一刀（砍掉任一即可省约 39.6%）。

**证据 3：真实的两个缺口（这才是需要改的东西）**

1. **产生侧只对白名单格打印**：`R3RResvWatchLog` 的打印门禁是 `if (!R3RIsWatchedResvTile(tile)) return;`（`pbs.cpp:684`）⇒ 桥头 `220/223/226/230,235`、`x=80…256,235` 这些**不在 watch list 的格，`SET-*` 事件完全静默**。这正是 KI-415 里"桥头预留怎么来的查不到"的另一半原因。
2. **没有"原生/非原生"的判据**：`phase=` 字段已有，但 `r3r_resv_phase` 默认值就是 `"misc"`（`pbs.cpp:54`），R3R 自己的路径各自 `R3RResvPhaseGuard` 具名（`consist` / `clear-consist` / `waiter-platform` / `gc-stn` / `purge` …）⇒ **"`phase != "misc"` ⇒ 非原生"这个判据天然成立**，不需要额外埋点、不需要改任何 setter 签名。

**结论：改动清单（全部在 `.cpp`，可走增量构建约 2 分钟，无需全量重编）**

| # | 位置 | 改动 | 收益 |
|---|---|---|---|
| 1 | `pbs.cpp:684` | 门禁由 `if (!watched) return;` 改为 `watched || phase != "misc"` 时打印，加总开关 `R3R_RESV_TRACE_NONNATIVE`（默认开） | 实现玩家要的"非原生产生也打印" |
| 2 | `train_cmd.cpp:14113` 或 `pbs.cpp:1068` | 二选一删除：推荐**保留 `UNRES-RAIL`**（最内层、不可漏、带 track），`CLEAR-PATH` 降为仅 `phase=="misc"` 时打或直接删 | 日志立减约 39.6% |
| 3 | `pbs.cpp:678` | 按 `(kind, tile, track, phase)` **首现 + 计数**：首现打 `RESV-WATCH …`，后续只累加，收尾打 `RESV-WATCH-SUM` | 把新增量压到常数级 |
| — | 开关 | `R3R_RESV_TRACE_NONNATIVE`（0=仅白名单，1=非原生全打） | 玩家可自行收敛 |

- **不需要**：新增 setter 埋点、改 `r3r_resv_probes.h`、改任何 `src\*.h`。
- **仍待拍板**：**C-273-4 作废**（不再是 A/B 二选一）；新增 **C-273-7**：删 `CLEAR-PATH` 还是删 `UNRES-RAIL`？（推荐保 `UNRES-RAIL`）**C-273-8**：节流用"首现+计数"还是"每 tick 每格最多 1 行"？

### 7.9 玩家拍板（2026-10-22）：预留按推荐做 + 其它探针暂时禁用

> **玩家原话**：「我们收敛探针，预留方面，可以按照你推荐的来，而其它探针，可以暂时禁用，注释或删除」

**决议（按推荐=我的推荐项全部采纳）**：

- **C-273-7 = 删 `CLEAR-PATH`**（`src\train_cmd.cpp:14113-14117`），保 `UNRES-RAIL`（`pbs.cpp:1068`，setter 层、覆盖桥/隧道/站台且带 track）。省 3190 行 ≈ 39.6%。
- **C-273-8 = 首现 + 计数**（按 `(kind, tile, track, phase)` 边沿化，收尾打 `RESV-WATCH-SUM`）。
- **C-273-4 作废**（基础设施已存在，纯 `.cpp` 增量）。
- **门禁放宽**：`R3RResvWatchLog()` 的 `if (!R3RIsWatchedResvTile(tile)) return;`（`pbs.cpp:684`）改为「白名单格 **或** `phase != "misc"`（非原生）」即打印，并加总开关 `R3R_RESV_TRACE_NONNATIVE`。
- **其它探针 = 暂时禁用（可恢复）**：玩家授权"注释或删除"。

**执行顺序（本轮）**：①先定"其它探针"的禁用实现方式（是否所有探针都经 `R3RDbgWrite()` 统一出口 ⇒ 若统一，则一处按标签过滤即可全禁，零侵入、可随时恢复，远优于逐处注释数百行）；②改预留侧 4 处；③增量构建；④回填构建自证。

**待补记**：`R3RDbgWrite` 是否为全部探针的唯一出口（决定禁用方案）。

### 7.10 实施定稿（取证后确定，2026-10-22）

**取证结论：`R3RDbgWrite()` 是全部探针的唯一写盘出口**（`src\r3r_perf.h:224-242`，`inline`；`R3R_PROBES=0` 时展开为 `((void)0)` 宏）。它内部只有总开关 `if (!R3RDbgOn()) return;`（`r3r_perf.h:143-156`，受环境变量 `R3R_DBG` 控制）。
⇒ **禁用"其它探针"不需要逐个注释几百处调用点**，只要在这个唯一出口按"标签"过滤一次即可，且可用环境变量随时恢复、无需重编。

**最终改动（3 个文件）**

| # | 文件 | 改动 |
|---|---|---|
| 1 | `src\r3r_perf.h` | 新增 `R3RDbgTagAllowed(const char *fmt)`：取 fmt 的第一个空白前 token，**默认只放行 `RESV*` / `UNRES-...` 预留侧标签**，其余一律丢弃；环境变量 `R3R_DBG_TAGS=all` 一键恢复全部探针。`R3RDbgWrite()` 内 `R3RDbgOn()` 之后调用它。补 `#include <cstring>`。 |
| 2 | `src\pbs.cpp` `R3RResvWatchLog()` | ①门禁由 `if (!watched) return;` 放宽为"白名单格 **或** phase 非 `misc`（非原生）"；②白名单格**保持原格式每变必打**（不破坏既有 grep）；③非白名单的非原生事件走**首现+计数**：首现打原格式一行，其后每第 64 次打 `RESV-WATCH-REP kind= tile= track= phase= n=`。 |
| 3 | `src\train_cmd.cpp:14113-14117` | **删除 `CLEAR-PATH` 探针块**（与 `UNRES-RAIL` 同一事件重复），原位留一行注释指向本节。省 3190 行 ≈ 全日志 39.6%。 |

**代价**：改 `src\r3r_perf.h` ⇒ KI-183 护栏必判 `REMOVED 620 obj` ⇒ **全量重编约 20 分钟**（无法避免：`R3RDbgWrite` 是 inline，改其实现必须重编全部引用者，否则行为不一致）。与预留侧两处 `.cpp` 改动合并到同一次构建，不额外多编。

**恢复方式**（无需重编）：`set R3R_DBG_TAGS=all` 或在启动器里加该环境变量，即可让所有旧探针（`FOLDCHK` / `CPL-*` / `EXTEND-BACKOUT` / `SPD-DROP` / `TRP` …）重新输出。

**判据**：①默认启动时日志只剩 `RESV-*` / `UNRES-RAIL` 系列；②`R3R_DBG_TAGS=all` 时旧标签全部回来；③桥头 `220/223/226/230,235` 出现 `RESV-WATCH SET-BRIDGE/CHG-BRIDGE … phase=<非 misc>`（此前完全静默）；④`CLEAR-PATH` 全日志 0 条、`UNRES-RAIL` 条数不变。

**构建自证（已完成，2026-10-22）**：三处改动落盘后启动 `_tmp_inc_build.cmd`。`build\R3R_incbuild.guard.log` 判定：
`GUARD: 1 header/lang file(s) are newer than the newest object file` / `newest object = 2026-10-22 13:59:28` / `newer: src\r3r_perf.h (2026-10-22 14:58:50)` / `REMOVED 620 object file(s) - upgrading this build to a FULL rebuild`
⇒ 如「代价」所预判，走**全量 692 步**。结果：

- `build\R3R_incbuild.log` 尾行 `[692/692] Linking CXX executable openttd.exe`；`build\R3R_incbuild.done` = `EXIT_CODE=0`
- 错误计数 `error C* / fatal error / FAILED: / build stopped` = **0**
- `build\openttd.exe` = 2026-10-22 15:24:18（52 089 344 B）
- 时间戳链：`src\r3r_perf.h` 14:58:50 < `src\pbs.cpp` 14:58:40 → `pbs.cpp.obj` 15:17:14；`src\train_cmd.cpp` 14:58:58 → `train_cmd.cpp.obj` 15:20:27 ⇒ obj 均晚于源码
- `read_lints` 三文件 0 条
- exe 字面量核对：`RESV-WATCH-REP` = 1、`RESV-WATCH %s tile=` = 2（白名单分支 + 首现分支各一份）、**`CLEAR-PATH` = 0（确认已从产物中删除）**

**交付复测 4 条**（玩家用本 exe）：①默认启动日志只剩 `RESV-*` / `UNRES-RAIL`；②`set R3R_DBG_TAGS=all` 后 `FOLDCHK`/`CPL-*`/`EXTEND-BACKOUT`/`SPD-DROP`/`TRP` 等旧标签全部回来；③桥头 `220/223/226/230,235` 出现 `RESV-WATCH SET-BRIDGE/CHG-BRIDGE … phase=<非 misc>`；④全日志 `CLEAR-PATH` 0 条、`UNRES-RAIL` 条数与本轮基线（3207）量级一致。


## 八、复测判据（本轮修好后）

1. 路点 + `GOTO_COUPLE` 布局下必须出现 `CPL-HOLD` 心跳，且机车 `spd=0` 停在路点格上。
2. 配对建立后挂车方起步、`COUPLE-OK` 成立。
3. 等待方不得因挂车方挤占而永久卡死。
4. 全日志不再出现无主整格位/plain rail 预留（`RESV-AUDIT-STRAY` 归零）。
