# R3R 第 267 轮：第 266 轮修复的首次实机复测（放行达到预期，预留侧暴露新问题）

## 一、玩家报告（2026-10-21，第 266 轮改动后的第一次复测）

玩家原话：

> 「请看看吧，我觉得可能这边达到预期了，但是预留那边出了问题」

日志：`D:\temp\codebuddy-dropped-files\e759e3d4-dec8-44b5-8981-1ca710510a55\R3R_debug.log`
（**304 129 B**，`LastWriteTime 2026-10-21 18:25:25`）

⇒ 产生者应为第 266 轮刚构建的 `build\openttd.exe`（`2026-10-21 18:12:07`，52 036 096 B）。
采样有效（日志晚于 exe 13 分钟）。

## 二、已取证读数（行号为日志行号，原文摘录）

| 行 | 读数 | 判定 |
|---|---|---|
| `:3982` | `DECOUPLE-FIRE consist=21 tx=267 ty=237 real=5 mode=0 num=0 segs=1 eff=24 plat=48/0` | 同一场景：**48 格站台**，与第 266 轮现场一致 |
| `:4080` | `DECOUPLE-DONE u=24 co=0 real=6 tx=273 ty=237 x=4368 y=3800` | 解出方 = **veh=24**（后车），`co=0` 非纯车底 ⇒ 命中登记条件 |
| `:4149` | `DECOUPLE-INTERLOCK set head=21 follower=24 seam_car=47 tile=272,237` | 互锁登记：前车 21，后车 24，前车链尾 47 @ 272,237 |
| `:4150` | `LOCO-AFTER-DECOUPLE veh=21 curType=0 real=5 idxnext=1 tile=267,237` | 前车 21 仍在 267,237 |
| `:4186` | `DECOUPLE-INTERLOCK wait head=21 follower=24 seam=272,237 seam_car=47 dist=0 res=0 age=0` | **dist=0 / res=0**：能力判据此时不成立，正常等待 |
| `:4206` | `DECOUPLE-INTERLOCK release head=21 follower=24 seam_car=47 dist=7 age=8 reason=reserved` | **放行来自能力判据 `reason=reserved`，不是几何判据** |

### 2.1 结论句（判据侧）

1. **第 266 轮 §八 判据 ③ 正向成立**：全日志首次出现 `release … reason=reserved`
   （第 265 轮实现后全日志 0 条）。放行时刻 `dist=7`、`age=8`（8 tick），远早于
   `dist` 涨到"尾车离开 48 格站台"，说明能力判据确实提前放行、把"白等站台长度"变成了
   "前车让出到下一个安全位即可起步"——**这正是玩家第 265 轮要求的口径**。
2. `DECOUPLE-INTERLOCK` 全日志仅 **3 条**（set/wait/release），无 `timeout`、无重复 `set`
   ⇒ 判据 ⑤（正常场景不出现 timeout）成立；判据 ④ 旁证成立（只有一对车号）。
3. `wait` 与 `release` 之间隔 20 行、`age` 仅 8 tick ⇒ 后车停住时间很短，
   玩家"达到预期"的观感与此一致。

## 三、新疑点（按规则先落盘，未取证完）

玩家指出"预留那边出了问题"。日志尾部可见可疑形态（**尚未定性**）：

- `CLEAR-PATH veh=50 tile=263,237 td=0 phase=controller actor=24`
  —— 被清路径的车是 **veh=50**，而 `actor=24` 是**解出的后车**（DECOUPLE-DONE u=24）。
  即"后车 24 的身份在清别的车（50）的路径"。`veh=50` 与 `veh=24` 的关系、
  以及 `actor=` 的语义（预留主人 vs 路径所有者）**待查**。
- 尾部另有 `CLEAR-PATH veh=47 … actor=21` 与 `LR-TRACE veh=21 depth=1 tile=206,320
  res_end=27921 cur=26643` —— 前车 21 的预留终点与当前节点差 1 278 格，
  该 `res_end/cur` 是否为正常长路径终点、还是异常残留，**待查**。

### 3.1 疑点清单（编号，待逐条关掉）

- **Q-1**：`CLEAR-PATH` 的 `veh=` 与 `actor=` 为何不同车？（是"清除别人的路径"还是
  探针打印错对象）——需看 `src\train_cmd.cpp` 的 CLEAR-PATH 打印点。
- **Q-2**：`veh=50` 是什么身份？（后车 24 解出后又被谁挂了？还是前车 21 的链被拆成 50？）
  需抓 `COUPLE-OK` / `CHAIN-ATTRS` / `GEO settle-*` 行。
- **Q-3**：玩家说的"预留出了问题"具体形态——是**黑色预留预览出现在车不走的轨上**
  （KI-346 家族）、**预留被误清导致停车**、还是**预留泄漏挡别的车**？需玩家一句话确认，
  同时用 `RESV-AUDIT-STRAY` / `RESV-GC` / `RESV-REAP` 计数判。
- **Q-4**：`release reason=reserved` 之后，后车 24 起步时自己的预留是否被
  `R3RReleaseChainReservations()`（解挂/耦合清扫）连带清掉？第 265 轮把
  `TryPathReserve` 放进互锁内部调用，失败回滚由 `ChooseTrainTrack()` 负责，
  但**成功后本 tick 起步**这条路径是否漏了 reserved flag 记账，未知。

## 四、下一步取证计划（每组结果立刻回填本文件）

1. `CLEAR-PATH` 打印点源码（`grep 'CLEAR-PATH'` → `src\train_cmd.cpp` 行号），确认
   `veh=` / `actor=` 各自取的是哪个对象。
2. 抓 `veh=50` 的来历：`COUPLE-OK` / `DECOUPLE-*` / `GEO settle-*` 中出现的 50。
3. 预留审计计数：`RESV-AUDIT-STRAY`、`RESV-GC`、`RESV-REAP`、`RESV-GHOST`、
   `RESERVECONSIST`、`TPR-BLOCKED` 各多少行、首末行号。
4. 若确认是"后车起步后预留被误清"，查 `R3RReleaseChainReservations()` 的调用点是否
   把刚订到的路径当成"别人的"清掉。

## 五、事故链复演（行号 4150-4206 + 其后 50 行，原文摘录）

### 5.1 等待期：`origin` 继续跟着前车屁股推进（与第 266 轮 §6.2 逐字一致）

| 行（约） | 读数 | 判定 |
|---|---|---|
| 4180 | `TPR-BLOCKED veh=24 … origin=265,237 okay=0 other=21 otherTile=267,237 otherSpd=9` | 后车"下一个安全停车位"= 前车屁股后一格，被前车压住 |
| 4186 | `DECOUPLE-INTERLOCK wait … dist=0 res=0 age=0` | 第 1 个 tick 能力判据不成立 |
| 4190 | `TPR-BLOCKED … origin=265,237 … otherTile=266,237 otherSpd=16` | 前车前进，`origin` 不动、`otherTile` 推进 |
| 4194 | `TPR-BLOCKED … origin=265,237 … otherTile=265,237 otherSpd=57` | 前车到 265，正好压在 origin 上 |
| 4196 | `TPR-BLOCKED … origin=264,237 … otherTile=264,237 otherSpd=79` | 前车过了 265，`origin` 跟着推进到 264 |
| 4198 | `TPR-BLOCKED … origin=263,237 … otherTile=263,237 otherSpd=80` | 同上，`origin` 继续被"顶"着走 |
| 4206 | `DECOUPLE-INTERLOCK release … dist=7 age=8 reason=reserved` | 8 tick 后订路成功 ⇒ 放行 |

⇒ **能力判据成功的物理含义仍是"前车腾出一格"**，不是"前车让出到信号位"。
本场景能"看起来对"，是因为恰好在站台边界上：

- `RESV-REL who=CLEAR-STN veh=21 tile=265..278,237 track=whole owner=21 age=27`（14 行）
  ⇒ 站台格是 **265..278,237**，前车尾车 47 清到 265 时 `dist=7`（272-7=265）
  恰是**站台最外格、即出站信号机所在位置**。
- 所以 `reason=reserved` 与"尾车离开站台格"的几何判据在这一刻几乎重合 ⇒ 玩家观感"达到预期"。

### 5.2 放行之后的即时动作（问题所在）

`4206` 之后紧接：

```
CLEAR-PATH veh=47 tile=265,237 td=0 phase=controller actor=21
RESV-REL who=CLEAR-STN veh=21 tile=265,237 track=whole owner=21 age=27 phase=controller purposes=0x1
RESV-REL who=CLEAR-STN veh=21 tile=266,237 … （共 265..278 连续 14 格，全部 owner=21）
```

⇒ 前车一离开站台，引擎按"站台整体"把 **265..278 整段 14 格整格预留一次性释放**。
而后车 24 的停车位置是 `DECOUPLE-DONE u=24 tx=273 ty=237`、`TPR-BLOCKED veh=24 tile=273,237`
——**273,237 正落在这 14 格区间内**。

⇒ 即：后车 24 刚靠 `reason=reserved` 拿到路权起步，同一/相邻 tick 里前车的
**站台整段释放**把这段（含后车脚下与前方）的整格预留一起清掉。
`RESV-REL` 打印的 `owner=21` 说明探针认这段的主人是前车，但**后车此刻正站在这些格上**。

### 5.3 结论句（保留原文，§九 有第 267 轮续的订正）

1. **判据侧：第 266 轮 §八 判据 ③ 成立**（首次出现 `release … reason=reserved`，
   `dist=7 age=8`，无 timeout）。玩家"达到预期"的观感与本场景几何重合一致。
2. **但 `reason=reserved` 的语义仍是"前车腾出一格"**（`origin` 逐 tick 被前车"顶着"推进，
   4190→4198 三行铁证）。本场景只是**恰好**在站台边界触发，换一个站台中部/长站台场景
   仍会抢跑。⇒ 第 266 轮"能力判据保留作正向价值"的设计**在这个意义上不成立**。
3. **预留侧问题（玩家所指）**：站台整格预留按"整段站台"释放，粒度与所有权不匹配
   —— 前车离站时把 265..278 一次清光，其中包含后车 24 所在/所需的格。
   后车"订到了路权"与"预留随即被清"在同一时刻发生。
4. `CLEAR-PATH` 的 `actor=` 是**全局诊断变量 `r3r_resv_actor`**（`src\train_cmd.cpp:13797-13799`），
   与 `veh=` 不等属正常，**不是缺陷**（Q-1 关闭）。
5. `veh=50` 是**另一列车**（`:302 GEO settle-couple n=47 veh=50 tile=1889,188`），
   与后车 24 无关（Q-2 关闭）。

## 六、修法方向（待拍板，未落地）

- **A（推荐，最小）**：`CLEAR-STN` / 站台整体释放前用 `GetTrainForWholeTileReservation(tile)`
  过滤——**该格的整格预留已不属于自己（或格上有别的车）就不整段清**，改为只清 plain rail 位。
  与 KI-203/KI-207c 同族，但方向相反：那两次是"漏清"，这次是"多清"。
- **B**：能力判据加"前车已越过出站信号机"这一条（真正的信号位判定），
  而不是靠"订到一格"——即回到第 266 轮 §四 A 方案，代价是要接信号查询。
- **C**：解挂后把后车 24 挪出前车站台预留区间（治前因，改动大、涉及落点算法）。

## 七、Q-3 已确认（2026-10-21，玩家答复）

**玩家原话**（决定性一句，原样保留）：

> 「后车起步后前方没有黑色预留预览，这是我观察到的预留方面的问题。注意：我们对预留系统拉了一坨大的，这里有很多的屎山代码，我们务必谨慎妥善修复！」

⇒ **Q-3 定案 = a**：后车起步后**前方没有黑色预留预览**（不是 b 的"铺到不走的轨"，也不是 c 的"走走停停"）。

### 7.1 这条答复的含义（把 §5.2 的推断收紧为现象）

- 黑色预览 = `rail_map.h` 的 **plain rail 预留位**（`GetRailReservationTrackBits()`，`_m[t].m2` bit 8-11）；
  玩家看不到预览 ⇒ 后车 24 起步方向上的格子里**没有 plain rail 预留位**。
- 这与 §5.2 看到的 `RESV-REL who=CLEAR-STN veh=21 tile=265..278 track=whole` 是**两个层面**：
  `track=whole` 是**站台整格位**（`_m[t].m6` bit 2），清它本身不直接抹 plain rail 位。
  ⇒ 所以"无预览"的真凶**尚未坐实**，候选两个，见 §7.2（必须分清再动手）。

### 7.2 两个候选（待 §九 取证判别，禁止先改码）

- **候选 α（§5.2 的连带）**：站台整段释放过程中，`CLEAR-STN` 那条路径**不只是**清整格位，
  还顺带把该格 plain rail 位一起 `UnreserveRailTrack()` 掉（`pbs.cpp` 的
  `UnreserveRailTrack()` 在 `TileType::Station && HasStationRail` 时会转成
  `SetRailStationReservation(tile,false)` —— 这一点在 KI-207c 附记里已记录过）。
  若后车 24 的路径预留正是 plain rail 位，就会被一并清掉。
- **候选 β（后车自己没订成）**：`release … reason=reserved` 只是"这一 tick 的
  `TryPathReserve` 返回了 `TPRRF_RESERVATION_OK`"，但 `ChooseTrainTrack()` 随后
  可能重新规划、把刚订的路 `ClearPathReservation()` 掉（`phase=controller` 的
  `CLEAR-PATH veh=47 …` 是本轮已见的大量形态）；或路径只订到"前车屁股后一格"，
  走完那一格就没了 ⇒ 观感也是"前方无预览"。

### 7.3 玩家硬约束（本轮起生效，写进备忘防丢）

> 「我们对预留系统拉了一坨大的，这里有很多的屎山代码，我们务必谨慎妥善修复！」

⇒ 后续改动纪律（自定，供玩家否决）：
1. **一次只改一条路径**，且只改"被证据指认的那一条"，不顺手重构。
2. 动 `ClearPathReservation` / `R3RReleaseChainReservations` / `SetRailStationPlatformReservation`
   任一处前，必须先在备忘列出**全部调用点**与各自的原语义，逐点判定影响。
3. **先加只读探针**把"谁清掉了后车的 plain rail 位"钉死，再改行为；探针不得改变任何行为。
4. 每步都要有**负判据**（正常 PBS 预留、站台等待、路点预留三类不得回退，
   见 KI-203/KI-207c/KI-346 的既有判据）。

## 八、未确认项

- U-1：`veh=50` 已确认为另一列车（见 §5.3-5），无需再查。
- U-2：`actor=` 已确认为全局诊断变量（见 §5.3-4）。
- U-3：玩家观察到的"预留问题"形态 = §七 的 Q-3-a/b/c，**待玩家一句话确认**。
- U-4：后车 24 在 `273,237` 站立期间，其脚下整格预留的实际 owner 是 21 还是 24
  （`RESV-REL` 只打印了释放时的 owner=21，需加 `RESV-AUDIT-WATCH` 关注格才能坐实）。

## 九、第 267 轮续：玩家确认「无黑色预览」后的取证（2026-10-21）

### 9.1 新增读数（`R3R_debug.log`，`release` 行 4206 之后）

```
:4219 TRP veh=24 mf=273,237 real=6 type=1 dest=133161 spd=0 fp=1 look=1 one=0 mstuck=0 fto=0 => ok=1 res=1
:4220 RESV-REL who=CLEAR-STN veh=21 tile=265,237 track=whole owner=21 age=27 phase=controller purposes=0x1
        … 连续 14 行：tile=265..278,237，全部 track=whole / owner=21 / age=27
:4235 LR-TRACE veh=24 depth=1 tile=206,296 td=1 res_end=1671 cur=160 le=1672 reason=9 nodes=1
:4236 CRT    veh=24 order=1 dir=1 origin=260,237 td=0 found=1
:4237 RESV-REL who=CLEAR-STN veh=24 tile=265,237 track=whole owner=24 age=3 phase=controller purposes=0x1
        … 连续 7 行：tile=265..271,237，全部 track=whole / owner=24 / **age=3~6**（逐格递增）
```

**结论句（读数层）**：

1. 后车 24 起步后，自己在 `265..271,237` 上持有的预留是 **`track=whole`（站台整格位）**，
   `owner=24`、`age=3~6` 逐格递增 ⇒ 这是**它前进时逐格清自己刚订的整格位**，属正常收尾，
   不是被别人抢清。
2. 全日志**不存在** plain rail 预留的"设置"探针（`SET-RAIL` 命中 **0** 行；`CLEAR-RAIL`
   只作为 `RESV-REL-PATH who=CLEAR-RAIL veh=21 …` 的 `who=` 取值出现，共 3 行，全是前车 21）
   ⇒ **无法用现有探针证明后车 24 是否订到过任何 plain rail 预留位**。这是取数缺口，不是结论。

### 9.2 机制性事实（解释"看不到黑线"的第一候选，未坐实）

- 站台格上的 PBS 预留走 `TryReserveRailTrack()` 的站台分支 ⇒ `SetRailStationReservation()`
  （`_m[t].m6` bit 2，**整格位**），而不是 plain rail 位（`_m[t].m2` bit 8-11）。
- 玩家看到的**黑色冻结预览**只画 plain rail 位 ⇒ **站台格内本来就不长黑色预览**。
- 后车 24 起步时整列车仍在站台内（`273,237`，站台 265..278），它前方到 265 全是站台格
  ⇒ 这一段"没有黑线"是引擎语义，不是丢预留。
- **但**它走到 260 之后（`CRT origin=260,237`，已在站台外）如果仍无黑线，才是真缺陷。
  ⇒ 见 C-1。

### 9.3 猜测根因（待验证，先落盘）

- **G-1（中）**：`ClearPathReservation()` 的站台分支在"整段站台"释放时，
  会把该段的 plain rail 位一并 `UnreserveRailTrack()`（KI-207c 附记已证
  `UnreserveRailTrack()` 遇 `TileType::Station && HasStationRail` 会转 `SetRailStationReservation(tile,false)`）
  ⇒ 玩家看到"无预览"。
  **待验证判据**：后车 24 路径上的非站台格（如 `264,237 / 263,237`）在起步后应持有 plain rail 位；
  若 `GetRailReservationTrackBits()` 为空而 `TRP res=1` 成立，则成立。
- **G-2（中）**：`reason=reserved` 放行后 `ChooseTrainTrack()` 在下一 tick 重算并
  `ClearPathReservation()` 旧路，新路只订到"前车屁股后一格"⇒ 前方始终只有 1 格预留，
  视觉上等于没有预览。
  **待验证判据**：`CRT veh=24` 出现频率应 ≈ 每 tick 一次；`res_end` 节点号应始终只比 `cur` 大 1~2。

### 9.4 需要的取数（只读探针，未实施，等玩家点头）

按 §7.3 的谨慎纪律，先只加探针：

- **P-1** `RESV-SET-RAIL veh=%d tile=x,y track=%d whole=%d phase=%s`：
  在 R3R 自己的预留记账点（`Train::ReserveTrackUnderConsist()` 与
  `TryReserveRailTrack()` 的 R3R 包装，若无可放 `R3RResvPhaseGuard` 附近）打印**每一次成功订到的位**，
  含"整格 / plain rail"标志 ⇒ 直接回答"24 有没有 plain rail 位"。
- **P-2** `RESV-DUMP veh=24`：复用既有 `RESV-AUDIT-WATCH` 机制，把 `264,237 / 263,237 / 262,237`
  三格加入关注列表，每次审计打印 `reserved=` 与 `owner=`。
- 两条都只读、不改行为；`R3R_PROBES=0` 时零开销。

### 9.5 C-1（需玩家一句话，落盘防丢）

> 你看到"没有黑色预览"时，后车位置是在**站台格范围内**（站台红色区域的格子上），
> 还是已经开到**站台外的普通轨道**上？

- 若只在站台格内 ⇒ 大概率是 §9.2 的引擎语义（站台段本来不画黑线），属观感问题，
  可考虑"给站台段也画预览"或先不做。
- 若站台外普通轨道也没有 ⇒ 是真缺陷，按 §9.3 的 G-1/G-2 加 P-1/P-2 探针钉死后再改。

## 十、C-1 答复 + 形态彻底改写（2026-10-21 第二次，玩家原话保留）

**玩家原话**（决定性，原样保留）：

> 「是这样，站台和出站信号灯还隔了一些距离，然后前车离开站台没离开信号灯的时候后车就启动了，
> 然后就没有向站台铺预留，也没有向出站信号机铺预留，但是后车是全速启动的」

### 10.1 拆成四条硬事实

1. **站台最外格与出站信号机之间还有一段普通轨道** ⇒ 「前车离开站台」**不等于**「前车越过出站信号机」。
2. 前车**才离开站台、还没越过信号机**，后车就启动了 ⇒ 放行发生在**前车仍占用同一个信号 block 内**的时刻。
3. 后车启动后**既没向站台铺预留，也没向出站信号机铺预留**（玩家看到的黑色预览两处都没有）。
4. 后车**全速启动**，不是慢慢蹭。

⇒ 这**不是**"后车预留被谁清掉"的问题（§9 那一路的 α/β/G-1/G-2 全部降级为次要），
而是：**互锁在后车尚未拿到任何预留、前车尚未出 block 的情况下，就让后车全速跑起来。**
在 PBS 下这就是「无预留行驶」，属于可以撞车的真缺陷，**严重度应高于观感**。

### 10.2 疑点登记（按 2026-10-11「有疑点立刻落盘」规则）

- **D-1（矛盾）**：无预留却全速。正常 `TrainController` 在找不到可用预留/下一格时会刹车
  （`cur_speed` 应归零或 `Stuck`），不可能"全速"。
  可能：①放行路径里直接把 `cur_speed`/`subspeed` 写上去（或没清）；②后车此时判定为"非 PBS 段"
  （站台格整格位缺失时 `HasReservedTracks` 为假，走非 PBS 分支不查预留）；③放行时把
  `current_order` 推到了 `OT_NOTHING`，`TrainController` 认为"无目标"而不订路却照跑。
- **D-2（疑点）**：`DECOUPLE-INTERLOCK release` 的 `dist=` 阈值是**格数常量**
  （第 228 轮记录 `dist=4` 跨过即放行），与「站台最外格 ↔ 出站信号机」的实际间距**无关**。
  只要间距小于该常量，就必然在信号机之前放行。
- **G-3（猜测根因，待验证）**：互锁的 release 判据里**没有任何"是否已越过出站信号机"或
  "两车是否已分处不同 block"的检查**，只有几何距离 / `reason=reserved`。
  ⇒ 这是 §5.3-2 已证实的「`reason=reserved` 只是前车腾出一格」的**行为学后果**，
  现在有了肉眼可复现的表现形式。

### 10.3 C-2（需玩家一句话，落盘防丢）

> 站台最外格到那个出站信号机之间，隔了大概几格？（1~2 格 / 3~5 格 / 6 格以上）

用途：判断是否只要把 release 阈值抬到"至少跨过信号机"就能挡住，还是必须真的接信号查询。
（无论答案如何，G-3 的修法方向都是"接信号/block 判定"，这个问题的答案只决定
「最小补丁能否先扛住」。）

### 10.4 下一步（本轮不动手改行为）

读 `src\train_cmd.cpp` 里 `DECOUPLE-INTERLOCK` 的 `wait`/`release` 判据代码，
把现有判据（`dist` 阈值、`reason=reserved`、`gone=`）逐条列出来，并标出
**放行后是否对后车做了任何速度/订单侧的动作**（对应 D-1）。
拿到代码事实后再提修法，按 §7.3 纪律：先只读探针，再一次只改一条路径。

## 十一、代码级根因（2026-10-21 第 3 组工具，行号锚点）

`src\train_cmd.cpp` 的 `R3RDecoupleInterlockWait()`（:2364 起）实际有三条放行动作：

| 行 | 条件 | 动作 | 是否订路 |
|---|---|---|---|
| :2376-2379 | 后车自己已不在站台格 | erase + `return false` | **否** |
| :2387-2390 | 前车（`seam.head`）已不存在 | erase + `return false` | **否** |
| **:2405-2411** | **`seam_car` 被卖 或 其 `tile` 已不是 `IsRailStationTile`** | **`DECOUPLE-INTERLOCK clear … gone=` + erase + `return false`** | **否** |
| :2413-2419 | `age >= 3600` | `timeout` + erase | 否 |
| :2440-2449 | 每 1/4 tick 调 `TryPathReserve(consist)` 且返回 `TPRRF_RESERVATION_OK` | `release … reason=reserved` + erase | **是** |

### 11.1 玩家现象与代码逐字对位

- **「前车离开站台没离开信号灯时后车就启动了」** ⇒ 命中的正是 **:2405** 这条几何分支：
  唯一条件是 `!IsRailStationTile(seam_car->tile)`，**只要前车尾车（`kept->Last()`，
  本场景 = 47）的 `tile` 不再是站台格**就放行，完全不看它是否已越过出站信号机、
  也不看两车是否已分处不同信号 block。
- **「没有向站台铺预留，也没有向出站信号机铺预留」** ⇒ 该分支 **不调用 `TryPathReserve`**
  （唯一会订路的是 :2441），后车是在**零预留**状态下被放行的。
- **「后车全速启动」** ⇒ 同样由此解释：没有任何订路/失败刹车过程，后车直接进入
  `TrainLocoHandler` 的移动分支；它脚下是站台整格位（`_m[t].m6` bit2）被判为"有路权"，
  于是直接加速。
- `dist` 自第 265 轮起**不再参与判据**（:2402-2404 注释自陈"dist 仍计算并进日志，纯只读诊断"）。

### 11.2 为什么第 266 轮的假设会成立又失效

`:2293-2296` 的函数头注释自己写着：

> `platform exit counts (that is where the exit signal sits); a bare "N tiles away from the seam"`
> `threshold lets the follower start while the leader is still on the platform, which the player`
> `explicitly rejected;`

即：第 266 轮默认 **"站台末端 = 出站信号机位置"**，把"驶离 N 格"换成"驶离站台格"。
玩家现场证明该前提为假（**站台与出站信号机之间还有一段普通轨道**）
⇒ 这一条判据**从一开始就不够**，不是"多清了一格"的粒度问题。

### 11.3 结论（G-3 成立，坐实）

**G-3 成立（已由代码坐实，不再是猜测）**：`R3RDecoupleInterlockWait()` 的几何快速放行
只检查「前车尾车是否还在站台格上」，没有「是否越过出站信号机 / 是否已分处不同 block」，
也没有要求后车先成功预留 ⇒ 前车一离开站台格，后车就在零预留状态下全速起步。
这是 §9 的 α/β/G-1/G-2 之外、且优先级更高的**真根因**。

### 11.4 修法 A′（最小、只改 :2405 一条分支，待玩家点头）

1. `seam_car == nullptr`（前车被卖/身份被重建）⇒ **保留直接放行**（前车没了，没有让行对象）。
2. `seam_car` 存在但 `!IsRailStationTile(seam_car->tile)` ⇒ **不再直接放行**，
   改为**落进 probe 分支**（:2440 那套 `TryPathReserve`），预留成功才 `release`，
   失败继续等，由 :2413 的 3600 tick timeout 兜底。
3. 日志区分 `reason=leader-left-platform`（新，表示几何已满足但仍在等预留）
   与 `reason=reserved`（既有）与 `clear … gone=1`（前车消失）。
4. 预期效果：后车**永远带着路权起步**（不会再有"零预留全速"）；前车真正出 block 后，
   下一 tick 的 `TryPathReserve` 就会成功 ⇒ 起步延迟最多 `R3R_DECOMP_PROBE_INTERVAL`(4) tick。

**为什么不选 B/C**：
- B（直接查 `IsPbsSignal` / block 归属）：语义最正，但要接信号 API、跨 block 判定，
  改动面远大于一条分支，违反 §7.3「一次只改一条被证据指认的路径」。
- C（治前因：解挂落点避开站台）：改动大且本场景无解（后车本来就在站台里被解出）。

**负判据（改动后必须仍成立）**：
- 前车正常驶离并越过信号机后，后车 ≤4 tick 内 `release reason=reserved` 起步（不出现"等太久"）；
- 前车被卖掉（`gone=1`）时照旧立即放行；
- 非解挂场景（正常跟车、两列车共用一个站台）完全不受影响（`_r3r_decomp_seams` 里没有该车）。

## 十二、玩家提问：为什么不直接"两车脱离后都一起 TryPathReserve"（2026-10-21）

**玩家原话**：

> 「等等，那为什么不直接两车脱离之后都一起 trypathreserve」

### 12.1 关键事实：**已经在 try 了**，而且就是这个设计

`src\train_cmd.cpp:2438-2449`（第 265 轮落地）：

```cpp
const uint age = _tick_counter - seam.tick;
int r3r_res = -1;
if (age < R3R_DECOMP_PROBE_INTERVAL || (age % R3R_DECOMP_PROBE_INTERVAL) == 0) {
    r3r_res = (int)TryPathReserve(consist, false, false);
    if ((r3r_res & (int)TPRRF_RESERVATION_OK) != 0) {
        R3RDbgWrite("DECOUPLE-INTERLOCK release ... reason=reserved\n", ...);
        _r3r_decomp_seams.erase(it);
        return false;          // 订到路才放行
    }
}
```

⇒ 后车在等待期里**每 1~4 tick 就调一次 `TryPathReserve`**，成功才放行。
所以"脱离后一起 try"不是没做，而是**已经被做了**——问题在于 `:2405` 那条几何分支
**抢在它前面 `return false`**，把后车在"零预留"状态下直接放行了（§11.1）。
A′ 的本质就是把这条抢跑分支并回 probe，**不是新增机制**。

### 12.2 那"两车都主动 try"为什么不选

| # | 问题 | 依据 |
|---|---|---|
| P1 | **后车这一 tick 必然失败**：两车仍在同一 signal block，`TryPathReserve` 走 `CheckTrainsOnTrack` 必然把前车判为占用者 | `train_cmd.cpp` 的 `FollowTrainReservation` → `pbs.cpp` `CheckTrainsOnTrack`；KI-330 第 221 轮已证该函数"恒取 index 最小链头"，前车 index 更小 ⇒ 恒被判占用 |
| P2 | **失败要付出语义代价**：若 `mark_as_stuck=true`，后车被打上 `VehicleRailFlag::Stuck` ⇒ 界面显示"等待空余轨道"，而它其实只是正常让行 | 现实现已刻意传 `mark_as_stuck=false`（`:2441` 第三参，注释 `:2430-2431` 自陈理由） |
| P3 | **不能每 tick 全路径 YAPF**：`TryPathReserve` 是完整寻路，3600 tick 等待期若每 tick 跑一遍，代价不可接受 | 第 265 轮为此引入 `R3R_DECOMP_PROBE_INTERVAL=4`（`:2309-2311`） |
| P4 | **前车不需要替它 try**：前车继续跑自己的订单，`TrainLocoHandler` 的移动分支本来就会为它 `TryPathReserve`；由互锁再替它订一次是重复寻路，且会与它自己的路径争抢 block 判定 | `TrainLocoHandler` 移动分支 |

### 12.3 结论：A′ ≡「两车都 try，但只以后车的结果为准」

- 前车：**照常自己 try**（不干预，本来就在跑）。
- 后车：**在等待期里 try**（`:2440` 已有），**成功才放行**——A′ 只是把 `:2405` 从"绕过 probe 直接放行"改成"落进 probe"。
- 因此玩家提的"一起 try"与 A′ 的差别只有一条：**前车那一侧我们不额外插手**。
  这样既满足"订到路才走"，又避免 P1~P4 的四项代价。

### 12.4 一句话答复

> 已经在 try 了（每 1~4 tick 一次，成功才放行）；真正的问题是那条"前车尾车离开站台格就放行"
> 的几何分支抢在 try 之前把后车放走了。修法不是新增"一起 try"，而是**把这条抢跑分支并回 try**。

## 十三、A′ 落地（2026-10-21，玩家拍板「行，那就按A'修吧」）

**只改 `src\train_cmd.cpp` 的 `R3RDecoupleInterlockWait()`，未碰任何 `src\*.h`。**

### 13.1 改动前（`git` 可回退的原始形态）

```c
if (seam_car == nullptr || !IsRailStationTile(seam_car->tile)) {
    R3RDbgWrite("DECOUPLE-INTERLOCK clear ... dist=%u gone=%d\n", ..., seam_car == nullptr ? 1 : 0);
    _r3r_decomp_seams.erase(it);
    return false;              // ← 零预留放行
}
```

### 13.2 改动后

1. **拆成两条**：
   - `seam_car == nullptr` ⇒ 打 `clear … gone=1` + `erase` + `return false`（前车被卖/身份重建，
     没有让行对象，与旧行为一致）；
   - `seam_car` 存在但 `!IsRailStationTile(seam_car->tile)` ⇒ 只置局部量
     `const bool leader_left = true`，**不再 return**，继续往下走 `TryPathReserve()`。
2. **release 判据唯一**：`:2453` 起 `if (leader_left || age < 4 || age % 4 == 0)` ⇒
   `TryPathReserve(consist, false, false)`，只有 `TPRRF_RESERVATION_OK` 才 `erase + return false`。
   - 几何已满足后改为**每 tick** 试探（该窗口通常几十 tick，前车一越过出站信号机、
     两车分处不同 block，后车当 tick 就能拿到预留，不被 4 tick 节流拖慢）。
3. **日志**（便于复测判读）：
   - `DECOUPLE-INTERLOCK release … left=%d reason=%s`，`reason` 二值：
     `leader-left-platform`（几何已满足、靠预留放行）｜`reserved`（几何未满足、靠预留放行）；
   - `DECOUPLE-INTERLOCK wait … left=%d` 追加 `left=`；
   - `clear … gone=1` 不再有 `gone=0` 形态（`gone=0` 已不存在，凡 `gone=` 出现即前车消失）。
4. **timeout 位置不变**（`R3R_DECOMP_WAIT_LIMIT=3600`），现在它也覆盖"几何已满足但预留
   一直拿不到"的情况 —— 这是唯一可能让后车多等一会儿的新代价，属兜底。
5. 函数头注释 + probe 段注释一并改写（旧注释自陈第 266 轮假设"站台末端 = 出站信号机"，
   该假设已被玩家现场证伪，必须留档避免后人再加回几何捷径）。

### 13.3 负判据（复测时必须同时看）

| # | 判据 | 期望 |
|---|---|---|
| N1 | 前车驶离站台但仍在信号机前 | 日志出现 `wait … left=1 res=<非OK>` 或 `release … left=1 reason=leader-left-platform`，**不得**出现"无预留即起步" |
| N2 | 前车越过出站信号机 | ≤1~4 tick 内 `release … reason=leader-left-platform`，后车起步且带预留 |
| N3 | 前车被卖掉 | `clear … gone=1` 后立即放行（不得多等） |
| N4 | 非解挂场景（普通跟车/两列车共用站台） | `_r3r_decomp_seams` 无条目 ⇒ 完全不受影响 |
| N5 | 车库/野外解挂 | `R3RRecordDecoupleSeam()` 早退 ⇒ 无 set 行 |
| N6 | 性能 | `leader_left` 窗口内每 tick 一次 YAPF，窗口结束即停；不得出现持续 YAPF 拖慢 |

### 13.4 构建自证（2026-10-21 18:53，已完成）

玩家关掉游戏后重跑既有入口 `_tmp_inc_build.cmd`（未新建任何 .cmd）：

| 判据 | 实测 |
|---|---|
| 护栏 | `build\R3R_incbuild.guard.log` = `GUARD: incremental is safe (no header/lang file is newer than the newest object)`（18:51:12） |
| 编译/链接 | `[2/3] Building CXX object … train_cmd.cpp.obj` → `[3/3] Linking CXX executable openttd.exe` |
| 完成标记 | `build\R3R_incbuild.done` = `EXIT_CODE=0`（18:53:28） |
| 错误计数 | `error C` / `fatal error` / `FAILED:` / `build stopped` 命中 **0** 行 |
| 时间戳链 | `src\train_cmd.cpp` 18:48:06 → `train_cmd.cpp.obj` 18:51:58 → `build\openttd.exe` 18:53:15（52 036 096 B） |
| read_lints | 0 条 |
| exe 字面量 | `leader-left-platform` / `reserved` / `DECOUPLE-INTERLOCK release head=%d follower=%d seam_car=%d dist=%u age=%u left=%d reason=%s` 全部命中 |

⇒ 状态：**已实现 + 已编译，游戏内复测待做**。复测按 §13.3 的 N1~N6 判读。

## 十四、A′ 首次实机复测：**未达预期**（2026-10-21 19:05 日志）

### 14.1 采样有效性

`build\R3R_debug.log` = 2026-10-21 **19:05:21** / 598 458 B；产生者 `build\openttd.exe` =
2026-10-21 **18:53:15** / 52 036 096 B（A′ 版）⇒ 日志晚于 exe，**采样有效**。

### 14.2 现场原文（两次解挂同形，行号为 `build\R3R_debug.log`）

```
4173:DECOUPLE-INTERLOCK set head=21 follower=24 seam_car=47 tile=272,237
4210:DECOUPLE-INTERLOCK wait  head=21 follower=24 seam=272,237 seam_car=47 dist=0 res=0 age=0
4234:DECOUPLE-INTERLOCK release head=21 follower=24 seam_car=47 dist=7 age=8 reason=reserved
8411:DECOUPLE-INTERLOCK set head=21 follower=24 seam_car=47 tile=272,237
8448:DECOUPLE-INTERLOCK wait  … dist=0 res=0 age=0 left=0
8470:DECOUPLE-INTERLOCK release … dist=7 age=8 left=0 reason=reserved
```

- 两次都是 **`wait` 只出现 1 次（age=0），8 tick 后就 `release`**；
- `left=0` ⇒ 放行瞬间 `seam_car`(47) **仍在站台格上**；
- `reason=reserved` ⇒ 放行来自 `TryPathReserve` 成功，不是几何（几何捷径已在 A′ 删除，符合代码预期）。

### 14.3 前车尾车在哪（判 `left=0` 是否可信）

```
4216:CLEAR-PATH veh=47 tile=272,237 td=0 phase=controller actor=21
4236:CLEAR-PATH veh=47 tile=265,237 td=0 phase=controller actor=21
4237-4249:RESV-REL who=CLEAR-STN veh=21 tile=265,237 … 277,237 track=whole owner=21 …
```

⇒ `265..277,237` **13 格全部是站台整格预留**（`track=whole` + `CLEAR-STN`），
尾车 47 停在 `265,237` ⇒ **确实还在站台上**，`left=0` 读数可信，A′ 的几何部分工作正常。

### 14.4 根因（本次已实证）

`4233:TRP veh=24 mf=273,237 real=6 type=1 dest=133161 spd=0 fp=1 look=1 one=0 mstuck=0 fto=0 => ok=1 res=1`
—— 后车 24 的预留**成功**了。为什么成功？

```
4208:TPR-BLOCKED veh=24 … origin=265,237 okay=0 … other=21 otherSpd=9
4218:TPR-BLOCKED veh=24 … origin=265,237 okay=0 … other=21 otherTile=265,237 otherSpd=57
```

后车的预留目标 `origin` 就是 **265,237 = 前车尾车当前所在格**。前车逐格 `CLEAR-PATH`
（`4216/4219/4223/4227/4228/4230/4231/4236`）把自己走过的格交还，后车于是能把路
一路铺到「前车屁股后面那一格」并拿到 `TPRRF_RESERVATION_OK`。

**⇒ 结论：`TryPathReserve` 成功 ≠ 前车已离开。PBS 允许「跟车预留」（铺到前车后方），
这种成功在解挂互锁的场景里恒成立，因此以它为唯一放行判据等价于没有任何门槛。**
这就是 A′ 为什么看起来"完全没生效"——代码改了、`reason` 也确实变成了 `reserved`，
但 `reserved` 这个条件在"后车与前车同向、前车正在驶离"时 8 tick 内必然满足。

### 14.5 疑点（REG-267-A，已登记）

| # | 疑点 | 期望 | 实测 | 还需什么 |
|---|---|---|---|---|
| R-1 | 解挂当刻 `res=0`，8 tick 后 `res=1`，中间前车只走了 7 格 | 若两车仍在同一 signal block，后车预留应持续失败 | 8 tick 就成功 | 需要 `PBS` block id 对比（前车尾格 vs 后车头格），确认两者是否真的已在不同 block |
| R-2 | `release` 后后车动作 | 若确实已分处不同 block，后车起步无害 | 玩家观感/截图（本次未取） | 后车起步瞬间是否有黑色预留预览、是否紧随前车出站 |
| R-3 | 第二次解挂（8411 起）与第一次逐字同形（同 8 tick、同 dist=7） | 偶然 | 两次完全一致 | 说明是**结构性**行为，不是时序巧合 |

### 14.6 候选修法（待玩家拍板，编号 C-1/C-2/C-3）

- **C-1（推荐，语义 = 玩家原话「前车越过出站信号机」）**：判「后车车头 → 前车尾车」之间
  是否**存在一个出站信号机**。从 `consist->tile` 沿 `GetMovingDirection(consist)` 逐格前进
  （上限 ~32 格），中途遇到 `IsPbsSignal(tile, dir)` ⇒ 前车已在信号机另一侧 ⇒ 放行；
  先撞到 `seam_car->tile` ⇒ 两车之间无信号机 ⇒ 继续等（仍受 `R3R_DECOMP_WAIT_LIMIT` 兜底）。
  优点：直接编码玩家语义；缺点：需要正确处理车站/车库/隧道格与信号机朝向。
- **C-2（最省事）**：判 `seam_car->tile` 与 `consist->tile` 是否处于不同 PBS block
  （若引擎侧有现成 API 就直接用）。语义与 C-1 近似，实现更抽象，但 PBS block 的取法要先查证。
- **C-3（波形方案）**：A′ 回滚，恢复第 266 轮几何判据并加"尾车再驶离 M 格"。
  **不建议**——第 266 轮已否掉纯距离阈值，且现场站台外那段普通轨道的长度不固定。

### 14.7 一句话结论

> A′ 的几何部分（删除"离开站台格就放行"）是对的，但**换成 `TryPathReserve` 当判据是错的**：
> PBS 允许跟车预留，前车正在驶离时后车 8 tick 内就能"预留成功到前车屁股"，于是后车照样
> 在前车还压在站台上时起步。真正需要的是「两车之间已经隔着一个出站信号机」这条几何/信号判据。

---

## 十五、C-1 落地实现 + 构建自证（2026-10-21 第三次）

### 15.1 拍板与落点

玩家选 **C-1**（= KI-407 修法 B）。仅改 `src\train_cmd.cpp`，未碰任何 `src\*.h`，
未新增日志字符串以外的产物。

### 15.2 新增判定函数 `R3RSeamSignalState()`（`train_cmd.cpp:2375-2403`）

```cpp
static int R3RSeamSignalState(const Train *consist, const Train *seam_car, uint *steps_out)
```

- 起点 `consist->tile`、方向 `consist->GetVehicleTrackdir()`，跟随器
  `CFollowTrackRail ft(consist, consist->GetIndirectCompatibleRailTypes())`。
- 循环上限 32 格，每格顺序：
  1. `tile == seam_car->tile` ⇒ 返回 **0**（先撞上前车尾车 ⇒ 两车之间无信号机）；
  2. `ft.Follow(tile, td)` 失败 ⇒ 返回 **2**（跟丢）；
  3. `tile = ft.new_tile; td = RemoveFirstTrackdir(&ft.new_td_bits);`
  4. `IsTileType(tile, TileType::Railway) && HasPbsSignalOnTrackdir(tile, td)` ⇒ 返回 **1**。
- 无有效 trackdir（库内等）⇒ 返回 **2**；超过 32 格 ⇒ 返回 **2**。
- 纯只读：不订路、不改 `path`、不改地图/车辆状态。

`HasPbsSignalOnTrackdir(tile, td)` 的语义已核实（`src\rail_map.h:640` 定义、
`src\train_cmd.cpp:17864-17870` 的 `red_signals` 用法）：`td` = 在本格上的行进方向，
命中的是**本格入口侧、面向该行进方向**的那面 PBS 信号机 ⇒ 与"后车会撞上的那面"一致。

### 15.3 判据接线（`train_cmd.cpp:2520-2539`）

```cpp
r3r_res = (int)TryPathReserve(consist, false, false);
if ((r3r_res & (int)TPRRF_RESERVATION_OK) != 0) {
    const int sig_state = R3RSeamSignalState(consist, seam_car, &sig_steps);
    if (sig_state != 0) { … erase(it); return false; }   // 放行
    … 每 16 tick 打 hold reason=no-signal-between  // 继续等，条目保留
}
```

- 放行 = **预留成功 AND `sig_state != 0`**（AND，不是 OR）。
- `leader_left` 降级为**只解除 4 tick 探测节流**，不再是放行理由。
- `seam_car` 失效 ⇒ `gone=1` 直接放行；`R3R_DECOMP_WAIT_LIMIT` 超时兜底不变。
- 新日志字段：`release … left=%d sig=%d steps=%u reason=signal-cleared|leader-gone`、
  `hold … reason=no-signal-between`（每 16 tick 一行，防刷屏）。

### 15.4 玩家提问留档："和直接判定前车是否越过信号有什么不同"

| 维度 | 本实现（C-1） | 直接判定"前车越过某面信号机" |
|---|---|---|
| 判据主体 | 后车路径上、前车之前是否有 PBS 信号机 | 前车尾车相对某面**特定**信号机的位置 |
| 记忆 | 不记信号机身份，每 tick 现场扫 | 解挂时须记下 tile+td，且要存档 |
| 前车倒回 | 关系重新成立 ⇒ 重新 hold | 一次性置位后不回退 |
| 信号机拆改/加装 | 自动生效 | 记录失效 |
| 多面信号机 | 任意一面都算 | 需定义"第几面" |
| 前车进库/岔走 | `Follow` 跟丢 ⇒ 视为已让开 | 无定义，易永久卡死 |
| 开销 | ≤32 次 `Follow`（仅预留成功后） | O(1) 比较 |

### 15.5 已知弱点（未修，登记）

1. 分岔格 `RemoveFirstTrackdir()` 只取第一支 ⇒ 支路选错会跟丢并返回 2（误放行）。
2. 只认 PBS 信号机；两车之间若只有普通区间信号机，会继续等，由 timeout 兜底。

### 15.6 构建自证（复用 `_tmp_inc_build.cmd`，未新建 .cmd）

| 项 | 读数 |
|---|---|
| 护栏 | `GUARD: incremental is safe` |
| 编译/链接 | `[2/3] train_cmd.cpp.obj` → `[3/3] Linking CXX executable openttd.exe` |
| `build\R3R_incbuild.done` | `EXIT_CODE=0`（2026-10-21 19:19:41） |
| 错误计数 | `error C` / `fatal error` / `FAILED:` / `build stopped` = **0** |
| 时间戳链 | `train_cmd.cpp` 19:12:57 → `train_cmd.cpp.obj` 19:18:19 → `build\openttd.exe` 19:19:34（52 036 096 B） |
| `read_lints` | 0 |
| exe 字面量 | `no-signal-between` / `signal-cleared` / `leader-gone` 3/3 命中 |

### 15.7 复测判据（5 条）

1. 前车越过出站信号机之前：只有 `hold … reason=no-signal-between`，无 `release`。
2. 越过之后：`release … reason=signal-cleared`，且 `steps=` 为实际扫描格数（>0）。
3. `seam_car` 消失：`gone=1` / `reason=leader-gone`。
4. 负判据：车库/野外解挂不登记 seam ⇒ 行为一字不变；正常 PBS 预留、站台等待、路点预留不得回退。
5. `timeout` 仍为兜底，不得成为常态放行路径。
