# 第 272 轮（2026-10-21）：预留复核 + 挂接寻路 `why=GROUP` / 平行股道

## 一、本轮输入

- 玩家原话：「@build\R3R_debug.log 我难以查看游戏内的预留行为，请你查看。还有，现在列车的挂接寻路又出了一点问题，不符合我预期」
- 日志：`build\R3R_debug.log` 7940 行 / 544 324 B / 2026-10-21 23:05:51
- exe：`build\openttd.exe` 52 036 096 B / 22:55:33 ⇒ **含第 271 轮续的 18097 断言修复**，采样有效。

## 二、预留侧：日志里到底发生了什么

`Select-String` 计数（全日志）：`RESV-LEDGER-FORGET` **2**、`RESV-NOBOOK` 1、`CPL-SKIP` 1、
`CPL-PATHFOUND` 2、`CPL-SCAN` 2、`COUPLE-OK` 1、`SPD-DROP` 14、`LOCO-MOVE` 20。

### 2.1 只有两条账本销毁，且都不在第 271 轮那条通道上

```
:3987  RESV-LEDGER-FORGET veh=21 why=decouple mask=0x3 released=3  kept=14 phase=misc
:6088  RESV-LEDGER-FORGET veh=24 why=free-path mask=0x1 released=3 kept=14 phase=misc
```

- 两条都是 `phase=misc`（没有 choose-track 相位），`:6088` 早于唯一的 `CPL-SKIP`（`:7775`）1600 行
  ⇒ **第 271 轮的判据未被推翻**：节流跳过不再拆预留。
- `:3987 why=decouple` 是解挂提交点的正常释放。
- `:6088 why=free-path`（veh=24，等待车底侧）只撤 3 位、留 14 位，属"部分撤销"：
  调用点唯一 = `FreeTrainTrackReservation()` 开头的 `R3RResvLedgerForget(consist->index, "free-path")`
  （`src\train_cmd.cpp:14137`）。这条通道仍然醒目，但本次量级只有 3 位，**不是**"整段预留被清空"。
- 本轮**没有**出现第 271 轮那种 `released=85~95 kept=0` 的大规模清空。

### 2.2 其余预留动作都是正常尾清 / 等待者清扫

| 探针 | 条数 | 判读 |
| --- | --- | --- |
| `CLEAR-PATH veh=21 tile=257..264,233 td=8 phase=controller actor=47` | 8 | 机车走过后逐格释放**已走过的**格，正常 |
| `CLEAR-PATH veh=24 tile=259..264,234 actor=50` | 4 | 同上（actor 为该 tick 执行清理的链） |
| `RESV-GHOST waiter veh=24 tile=279..281,234 freed=3` | 3+1 汇总 | 第 127 轮 waiter 清扫，正常 |
| `RESV-NOBOOK veh=24 tile=265,234 order=17 caronly=0 reason=parked-waiter` | 1 | 第 127 轮门禁生效：停稳的等待者不再订整条出站路线 |
| `RESV-REL who=CLEAR-STN veh=21 tile=... track=whole owner=21` | 3 组（1888,188 / 1843..1856,191 / 1317..1330,221 / 791..793,237） | 站台整格预留随出站释放，owner=自己，正常 |
| `RESV-GC veh=24 tile=273..278,237 track=stn kind=STATION why=moved-gone` | **24**（每格 4 行重复） | **仍未修的泄漏通道**：4 节车 × 4 次重复报同一格 ⇒ 记账里同一格存了 4 条（见疑点 D-1） |

**给玩家的"预留行为"结论**：这一局里没有"铺满整段又被神秘销毁"的形态了；
`RESV-LEDGER-FORGET` 只剩解挂与一次 3 位的 free-path。剩下的可见异常只有
`why=moved-gone` 的站台整格预留——它每格被记 4 次、逐格清掉，属**无害但重复记账**。

## 三、挂接侧：`why=GROUP` 其实是"还没锁定配对"

```
:7752  CPL-ENTRY veh=21 tile=253,233 orderType=16
:7754  CPL-PATHFOUND veh=21 found=0
:7755  CPL-SCAN tick=3005 veh=21 found=0 … noconsist=67 consist=3 rej=3/0/0/0 acc=0/0
        last=278,234 lastveh=24 why=GROUP
:7775  CPL-SKIP veh=21 tile=254,233 orderType=16 retryIn=5 holdAge=3
```

`why=GROUP` 的计数点在 `src\pathfinder\yapf\yapf_destrail.hpp:549-552`：

```cpp
if (!R3RCoupleAllowed(Train::From(Yapf().GetVehicle()), t)) { r3r_dest_reject(0, tile, t); ... }
```

而 `R3RCoupleAllowed()`（`src\couple_group.cpp:597-608`）**第一道闸门是配对锁**，不是分组：

```cpp
if (!R3RCouplePairMatches(coupler, target)) return false;   // ← 未锁定时从这里返回
return R3RCoupleAllowedIgnoringPair(coupler, target);       // ← 分组白名单在这里
```

⇒ **`rej=…/why=GROUP` 把"配对锁未建立"也算成 GROUP**，现场读数看起来像"分组把候选拒了"，实际是
tick 3005 时机车与 veh=24 尚未互相锁定。**这是探针归因缺陷**（KI-404）。

佐证：从 tick 3019 开始 `CPL-PAIR act=21 tgt=24` 成串出现（说明锁定已建立，且目标就是 veh=24），
`splice/分组` 再没拦它：

```
:7889  CPL-PAIR tick=3019 act=21 tgt=24 dist=7 actTile=259,233 tgtTile=265,234
:7939  CPL-PAIR tick=3031 act=21 tgt=24 dist=1 actTile=265,233 tgtTile=265,234
:7940  LOCO-MOVE veh=21 tile=265,233 j=202 spd=30 track=01 stuck=0    ← 日志到此结束
```

`dist` = **曼哈顿格距**（259,233→265,234 差 6+1 = 7，吻合）。

### 3.1 真正的"不符合预期"：机车到了车底的**平行股道**

- 等待车底（veh=24..50，24 节）在 **y=234** 一整排：
  `RESERVECONSIST veh=24 tile=265,234 track=0x1` … `veh=50 tile=270,234 track=0x1`
  （`track=0x1` = TRACK_X，东西向）。
- 机车 21 在 **y=233** 同样东西向：`LOCO-MOVE veh=21 tile=265,233 track=01`。
- 两者相差 1 格且**平行**：车底头 veh=24 在 265,234，机车到 265,233 就 `dist=1` 了，
  但机车是"停在/开在车底旁边那一股道"，不是"迎头对接"。
- 该格机车也进不去：`RESERVECONSIST veh=24..50 tile=265..270,234 ok=0 resNow=1`
  ⇒ 车底脚下 24 格全部由**车底自己**预留（正常），机车的 PBS 不会让它开进去。

⇒ 若 y=233 与 y=234 两条股道在 265 附近没有连通/道岔，机车就会沿 265,233 → 266..270,233
与整条车底擦肩而过，永远 `dist=1` 挂不上。**这是本轮"挂接寻路不符合预期"的最高嫌疑**（KI-405）。

疑点 D-2：为什么 GOTO_COUPLE 的目的地会把机车引到 **y=233** 这股道？
待查 `PfDetectDestination` 的站台扫描（`yapf_destrail.hpp:430-478`，沿轴 `st += ±delta`
找等待车底）是否在**当前股道**上就判"到达"。

## 四、疑点与猜测根因（按规则先落盘，待验证）

- **D-1（疑点，预留记账重复）**：`RESV-GC … why=moved-gone` 每格连打 4 行 ⇒ 账本里同一
  (tile, track) 被登记 4 次（4 节车各登记一次？）。期望 1 行/格。待查
  `ReserveTrackUnderConsist()` 的记账去重。
- **D-2（疑点，股道）**：机车为何在 y=233 而不是 y=234；两条股道是否在站台内连通。
- **D-3（猜测根因，待验证）**：`CPL-PAIR` 只看几何格距，不看两车是否同轴/连通，
  于是"相邻股道"也会一路报 `dist=1` 让状态机以为"就快挂上了"。
- **D-4（猜测根因，待验证）**：`PfDetectDestination` 在站台段上把同站台**任意股道**都算作
  候选车底所在（第 93 轮 KI-163 修的是 `reach()/rp()` 的轴向盲扫，此处是 destrail 的另一段扫描），
  于是 GOTO_COUPLE 在平行股道上就判"到点"，机车不再换股。

## 五、结论

1. **预留侧**：第 271 轮的修复在本局生效（无大规模清空、无 `phase=choose-track` 的 free-path）；
   剩下的 `why=moved-gone` 是重复记账的站台整格位，量小、可后续收口。
2. **挂接侧**：`why=GROUP` 不是分组真拒，而是"配对锁未建立"被误计（探针缺陷，行为本身正常）；
   真正的问题是机车与目标车底**平行相邻而不共线**，PBS 下又进不去车底占的那 24 格。

## 五之二、补充取证（同日，源码 + 日志交叉）

### 5.2.1 `PfDetectDestination` 的扫描只沿**同一车站轴**，不会跨股道

`src\pathfinder\yapf_destrail.hpp:450-479`：`t == nullptr` 时才沿
`TileOffsByAxis(GetRailStationAxis(tile))` 双向前进，且每步要求
`IsCompatibleTrainStationTile(st, tile)`（同车站 + 同轴 + 相邻）。⇒ y=233 那股若与
y=234 不同行，**扫描根本走不过去**，`D-2/D-4` 的"平行股道被当成到达"**证伪**。
真正决定 y=233 的是 YAPF 的**路径**，不是目的地集合。

### 5.2.2 `why=GROUP` 的直接原因：`found=0`，即目的地集合为空

```
:7211  CPL-PATHFOUND veh=21 found=0
:7212  CPL-SCAN tick=2979 veh=21 found=0 ... noconsist=83 consist=0 rej=0/0/0/0
:7754  CPL-PATHFOUND veh=21 found=0
:7755  CPL-SCAN tick=3005 veh=21 found=0 ... noconsist=67 consist=3 rej=3/0/0/0
       acc=0/0 last=278,234 lastveh=24 why=GROUP
```

⇒ tick 3005 扫到 3 次候选（都是 veh=24），**3 次全被 `R3RCoupleAllowed()` 拒**；
`acc=0/0` ⇒ 没有任何一格被判为合法目的地 ⇒ `found=0` ⇒ 不预留、不换股。

### 5.2.3 排除项：不是分组白名单真拒

全日志 `CG-GATE` **零命中**（`R3RCoupleGateProbe` 只在
`R3RCoupleAllowedIgnoringPair()` 走到末尾分组比较时才打，`couple_group.cpp:508`）。
⇒ 调用在**到达那一行之前就 return 了**，最可能是外层
`R3RCouplePairMatches()`（`couple_group.cpp:606`）不成立。

### 5.2.4 配对锁每 tick 都在（重）选，但候选要先过"订单指定站"

`R3REnsureCouplePair()`（`src\train_cmd.cpp:12814`）由
`TrainLocoHandler` 每 tick 调用（`:19618`，depot 补锁在 `:19694`），扫描条件依次是
`t->First()==t` → `R3RIsCoupleTarget(t)` → `!Stopped` →
**`R3RCoupleTargetAtOrderStation(coupler, t)`（`:12891`）** →
未被别人锁 → `R3RCoupleAllowedIgnoringPair()`，全过才 `R3RPairCoupleTargets()`。

`CPL-PAIR` 是 `R3RDbgEdge` 边沿打印（`:12935`），只在**新建立/更换配对**时出现，
且 128 帧窗口抑制 ⇒ 不能据此断言"tick 3019 才建立配对"。

### 5.2.5 追加疑点

- **D-5（猜测根因，待验证）**：`R3REnsureCouplePair` 的扫描把 veh=24 挡在
  `R3RCoupleTargetAtOrderStation()`（`:12891`；订单 destRaw=12，见
  `DEPOTBLK veh=21 ... destRaw=12 depotIdx=4294967295`），而 **YAPF 的
  `R3RCoupleAllowedIgnoringPair()` 又是另一套目的地判定（KI-165 的"第一半许可"）**
  ⇒ 两层判据不一致时，配对锁永远建不起来，YAPF 的目的地集合恒空 ⇒ `found=0`。
  待核：veh=24 所在 station index 是否 == 12；两处目的地判定的差异。
- **D-6（疑点，读数矛盾）**：tick 3005 记 `last=278,234 lastveh=24`，而尾部
  `RESERVECONSIST veh=24 tile=265,234`，中间还有
  `RESV-GC veh=24 tile=273..278,237 why=moved-gone` ⇒ **veh=24 在这段日志里确实换过位置**
  （278,237 一带 → 265,234）。等待车底为什么会动，未解释。

## 六、结论与待取证

### 6.1 决定性读数：机车走完的是一条**沿 y=233 的 25 格直线**

```
:7536..7937  CLEAR-PATH veh=21 tile=240,233 td=8 phase=controller actor=47
             … 241,233 … 242,233 … （逐格 +1）… 264,233
:7940 尾部  CPL-PAIR tick=3031 dist=1 actTile=265,233 tgtTile=265,234
```

`CLEAR-PATH phase=controller` 是**车走过后释放脚下格**（正常尾清），⇒ 机车确实
**沿 y=233 一路直行到 x=264**，而等待车底 24 节停在 **y=234 的 265..270**。
两条股道**平行、相差 1 格**，`CPL-PAIR dist` 是曼哈顿格距（`|Δx|+|Δy|`），
所以 `dist` 从 6 降到 1 —— 完全不能说明"接近了可挂位置"。**D-3 成立**。

### 6.2 `why=GROUP` 不是分组白名单（D-5 收敛）

> 编号更正（2026-10-22）：本文中"KI-404"指 `R3R_KNOWN_ISSUES.md` 现 **KI-410**（`why=GROUP` 标签压缩），
> "KI-405"指现 **KI-411**（`found=0` 保留旧预留）。原编号与第 264/265 轮撞号，已改。

- `CG-GATE` 全日志**零命中** ⇒ 同公司分组比较那一行（`couple_group.cpp:508`）从未执行。
- 该函数在它之前有两个出口：`couple_group.cpp:402/403/409`（目的地不匹配）与
  `:503-504`（**跨公司**分支，直接 `return R3RCoupleGroupMasksAllowCrossCompany(...)`，
  同样不打 `CG-GATE`），加上外层 `R3RCouplePairMatches()`（`:606`）不成立也归 `why=GROUP`。
- ⇒ `why=GROUP` 这一个标签**把四种完全不同的拒绝压成一种**（配对锁未建立 / 目的地不匹配 /
  跨公司 / 分组不兼容），本轮**无法从日志判定** tick 3005 那 3 次拒绝究竟属于哪一种。
  **这是探针缺陷（KI-404），必须补细分。**

### 6.3 推测根因（D-7，待验证）：解挂后机车带着**旧路径预留**继续跑

时间线（行号）：

```
:3987  RESV-LEDGER-FORGET veh=21 why=decouple mask=0x3 released=3 kept=14 phase=misc
:4028  GEO settle-decouple-u n=0 veh=24 tile=273,237      ← 解出方在 273,237
:7211  CPL-PATHFOUND veh=21 found=0
:7755  CPL-SCAN tick=3005 veh=21 found=0 … consist=3 rej=3/0/0/0 why=GROUP last=278,234
:7775  CPL-SKIP veh=21 tile=254,233 orderType=16 retryIn=5 holdAge=3
:7940  尾部 dist=1，日志结束（无 COUPLE-OK / COUPLE-FAIL）
```

- 解挂清扫 `released=3 kept=14` ⇒ **地图上仍有 14 格旧预留没放**（账本只放了 3）。
- `ChooseTrainTrack()` 在 `found=0` 时按第 271 轮的两条 early-return **保留预留继续行驶**
  （`train_cmd.cpp:15465` 节流分支、`:15480` `cur_speed != 0` 分支）。
- ⇒ 机车端着**解挂前的普通到站路径**沿 y=233 走完，与 y=234 的车底擦肩；
  直到日志结束都没再为挂接铺路（`CPL-SKIP retryIn=5` 的节流也不清不改）。

⇒ 与玩家口径（`train_cmd.cpp:15484-15488`「找不到可挂车底就必须原地等，不能开向订单目的地」）
**直接冲突**：这一轮保住的"旧预留"正是它不该走的那条路。

### 6.4 待拍板（请按编号回答）

- **C-1**：`found=0` 且 `cur_speed != 0` 时，是否改为"**只在当前预留确实是挂接路径时才保留**，
  否则清预留 + 停车原地等"（= 局部回退第 271 轮的 15480 分支）？
- **C-2**：解挂时对**机车侧**是否直接 `FreeTrainTrackReservation()` 全清（当前 `released=3 kept=14`）？
- **C-3**：`why=GROUP` 是否补成 `pair / dest / company / group` 四种（纯探针，零行为变更）？

### 6.5 仍未取证

1. `CPL-PAIR dist` 只看几何格距、不看同轴/连通 —— 已确认为设计如此，是否需要改显示口径。
2. 玩家侧确认：日志截断之后机车是"继续往前开走"还是"停住"。
3. `veh=24` 为何从 273,237 移到 265,234（D-6）—— 若它自己带着 GOTO 订单在跑，则
   `R3RIsCoupleTarget()` 的 `OT_WAIT_COUPLE` 前置会让配对锁迟迟建不起来（同一根因的另一面）。

### 6.6 本轮补充取证（第 272 轮收尾，行号锚点）

1. **`why=GROUP` 的 3 次拒绝已被判明为「配对锁未建立」**（D-5 由此收敛，不必再查目的地判定差异）：
   `:7755 CPL-SCAN tick=3005 … rej=3/0/0/0 acc=0/0 why=GROUP` 发生在 **tick 3005**，
   而 `CPL-PAIR` 这一对（act=21 / tgt=24）第一次出现是在 **tick 3019**（`:7834` 一带，
   `dist=7`）。⇒ tick 3005 时 `R3RCouplePairMatches()` 必然为假，被 `couple_group.cpp:606`
   直接记成 `why=GROUP`，与分组白名单、跨公司、目的地都无关。
   反向自证：`CG-GATE` 全日志零命中，且若真走到 `:508` 分组比较，`CG-GATE` 必打行。
2. **配对锁为何晚到**：`CPL-PAIR` 首次成形于 veh=24 **停稳在 265,234 之后**
   （`:7814 DEPOT-ARR veh=24 … destTx=265 destTy=231` 紧跟其后）。⇒ 车底在跑动中
   `R3REnsureCouplePair()` 不认它，只有停稳落到站台格才建锁。这是配对锁的
   "目标必须已到位"时序，属设计，但它与「机车早已端着旧预留跑到跟前」叠加，结果是
   **锁建立得比机车到达更晚**，YAPF 那一次 `found=0` 就成了最后一次判决。
3. **D-6 结案（veh=24 确实在动，不是我读错）**：解挂后 veh=24 持**自己的 17 条订单表**
   （`:93-110` 段：`i=6 type=1 dest=19` … `i=10 dest=12`、`i=11 type=17 WAIT_COUPLE`），
   `DECOUPLE-DONE u=24 real=6` 后从 273,237 出发跑完整套行程（西行至 41,73 一带、
   再回 depot、最后到 265,234），`real` 一路推进到 11 执行 WAIT_COUPLE。
   ⇒ 它**不是**原地等人，而是「先跑完自己的活儿再回来等」；`last=278,234`（tick 3005）
   与 `tile=265,234`（tick 3031）的差值是它在西行/回站途中的正常位移。
4. **尾部几何（决定性）**：`:7911` 起 `RESERVECONSIST veh=24 tile=265,234 / 266,234 …`
   一整排 24 节停在 **y=234**，track 均为 `0x1 = TRACK_X`；机车 `:7937 trk=01 spd=30`
   在 **y=233**。两条平行东西向股道差 1 格，**没有同格、没有道岔** ⇒ 即便配对锁已建立，
   机车这一侧也不可能在 265,233 与 265,234 挂上。
   `dist=1` 只是曼哈顿差，不代表"可挂距离"（D-3 再次成立）。

⇒ 结论不变但更硬：**KI-405 的表现是「机车上一条与目标股道平行的旧预留被完整保留」**，
而非"挂接寻路算错目的地"。玩家看到的"擦身而过"由三件事叠加：
旧预留不清（`:15480` early-return）+ 配对锁晚建（目标停稳才建）+ 平行股道几何。

### 6.7 玩家口供纠正（2026-10-22）：两组动车组合体 + 通过式路点（根因方向改写）

**玩家原话要点**
1. 「这不是机车挂车底的机辆模式，而是**两组动车组分头行动后来又合体**的模式。」
   ⇒ 21 与 24 同属动车组，不存在"机车 / 车底"的角色差；此前备忘里的"机车/车底"用词作废，
   一律改称"主动组 / 等待组"。
2. 「后车的相关前往挂接命令**前面**，是前往路点命令。而这个路点……是一个**通过式路点**，
   列车的预留路径理论上可以穿过它，也可以在那一格停止。」
3. 「玩家的本意是让列车**在路点等待**被挂列车到位再启动去挂接，但是事实不符合预期。」
4. 「至于车底登记太晚和两条平行股道，我只知道列车**去了一条错误的到发线**，却不知道为什么。」

**⇒ 新的根因方向 D-8（待验证，取代/补充 D-7）**

- 「在路点等」**不是路点自身的语义**：通过式（non-stop）waypoint 的本义就是不减速穿过，
  想停必须靠别的机制。玩家期望的"等"只能由 R3R 的挂接等待链（`CPL-SKIP retryIn` /
  `WAIT_COUPLE`）提供。
- 若 R3R 的等待判决落在 **GOTO_COUPLE 阶段**（此刻列车已穿过路点、已驶入其后的轨道段），
  而 `found=0` 又走 KI-411 的 early-return（保留旧预留继续行驶），列车就会
  **开过通往目标股道的最后一个道岔**；一旦越过，它在物理上再也无法回到 y=234，
  只能沿 y=233 直行到底 = 玩家看到的"去了一条错误的到发线"。
- 配对锁晚建（等待组仍在跑自己的 17 条订单，`tick 3019` 停稳才建锁）
  使 `tick 3005` 那次 `found=0` 成为**最后一次判决**，恰好卡在"已过路点、未到道岔/已在道岔后"的窗口里。

**修法方向调整（C-1 不推翻，但重心变化）**

- **C-1′（代替原 C-1）**：`found=0` 时的判据不只看"预留是不是挂接路径"，还要看
  **"是否已经越过最后一个能通往目标格的岔口"**；未越过 ⇒ 停车原地等；已越过 ⇒ 至少不能继续加速驶离。
- **C-4（新增）**：把"在路点等"做成显式语义 —— 当 GOTO_COUPLE 的前一条是
  **通过式 GOTO_WAYPOINT** 且配对/目标未到位时，允许在该路点格停车等待
  （等价于把该路点当 `WAIT_COUPLE` 的锚点），而不是穿过后再判。

**下一组取证项**
- V-1：日志里 21 / 24 的订单 dump 是否真有一条 `type=6`（`OT_GOTO_WAYPOINT`）在 `type=16` 之前，
  其坐标、`dest`、以及是否带 non-stop（通过式）标志。
- V-2：y=233 与 y=234 之间的道岔位置（`CLEAR-PATH` 序列里找 track 变化），
  以及列车越过该道岔的 tick 与当时 `current_order` 的阶段。
- V-3：`CLEAR-PATH veh=21 起始 240,233` 是否正是该路点格或其紧邻后继格。

### 6.8 实证：订单表确认"路点 → 挂接"，且 21 是沿错误到发线直行（行号锚点）

1. **订单表确认（玩家口供逐字对上）**：日志开头的 `R3RDUMP-ORDER`（`:84-110`）显示
   - `:91 head=21 i=7 type=6 dest=16`（`OT_GOTO_WAYPOINT`，路点 16）
   - `:92 head=21 i=8 type=16 dest=12`（`OT_GOTO_COUPLE`，车站 12）
   ⇒ 主动组 21 的挂接命令**前面确实是一条路点命令**，且**不是**反转路点（同表还有 i=3 `type=6 dest=17`，属上一轮循环）。
   等待组 24：`:101 i=7 type=6 dest=20`、`:105 i=11 type=17`（`OT_WAIT_COUPLE`）⇒ 24 才是真正的等待方。
2. **21 此刻已在执行 GOTO_COUPLE，不是"旧到站预留"**：`:7911 DEPOTBLK veh=21 tile=264,233
   track=01 eng=1 isdepot=0 … spd=30 **destRaw=12**` ⇒ 当前订单目的地就是车站 12（`type=16`）。
   ⇒ 对 D-7 的修正：早期判断"它端的是解挂前的旧到站预留"**不准确**，它确实在跑挂接订单，
   但**跑在错误的到发线上**。
3. **等待组 24 已到位并在等**：`:7872 ORD-IDX-WATCH veh=24 real=10->11` → `:7878 TRP veh=24
   mf=270,234 real=11 type=17 spd=0` → `:7879 RESV-NOBOOK veh=24 tile=265,234 order=17 … parked-waiter`
   ⇒ 24 在 265..270,234 停稳等挂（`real=11` 正是 `WAIT_COUPLE`）。
4. **配对锁太晚，而 21 早已判过 `found=0`**：`:7889 CPL-PAIR tick=3019 act=21 tgt=24 dist=7
   actTile=259,233` —— 配锁第一次建立时 21 已到 259,233（离 265,233 只剩 6 格）；
   而挂接寻路最后一次是 `:7754 CPL-PATHFOUND veh=21 found=0`（tick 3005），
   `:7775 CPL-SKIP retryIn=5 holdAge=3` 之后**再没有寻路**。⇒ 锁建立时已经来不及重新规划。
5. **21 全程 30 速直行、未停未转线**：`:7883-7940` 连续 `CLEAR-PATH veh=21 tile=257..265,233 td=8`
   + `LOCO-MOVE veh=21 tile=…,233 j≈200 spd=30 track=01 stuck=0`，最后 `:7939 CPL-PAIR tick=3031
   dist=1 actTile=265,233 tgtTile=265,234` 与 24 并排。⇒ 没有"在路点停车"这一步发生。
6. **两股道几何**：24 整列 `track=0x1`（`TRACK_X`）在 y=234（`:7912-7935`），21 在 y=233 同样
   `track=01`（`:7937`）⇒ 两条平行东西向直线、间距 1 格；`dist` 只算曼哈顿距离，
   `dist=1` 不代表可挂。

**⇒ 修正后的根因链（G-1，待验证最后一环）**

| 环节 | 事实 | 后果 |
|---|---|---|
| ① 路点是通过式 | 订单 `type=6`（无反转），玩家本意"在路点等" | 路点本身不产生停车；R3R 的"等"只认 `WAIT_COUPLE`(17) |
| ② 配对锁晚建 | 等待组 24 先跑完自己的 17 条订单，`tick 3019` 停稳才成 `WAIT_COUPLE` | `tick 3005` 的 `found=0` 成了最后一次挂接判决 |
| ③ 挂接寻路失败后退化 | `found=0` ⇒ 只能按 `GOTO_COUPLE` 的目的地（**车站 12**）走普通到站寻路 | 站台 12 有多条到发线，选了空的那条 **y=233** |
| ④ 越过岔口后不可逆 | 进入 y=233 后一路向东，无道岔可回 y=234 | 锁建立时已在 259,233，只能与 24 并排擦过 |

**待验证（U-1/U-2）**
- U-1：路点 16 的 tile 是否在 y=234（早期 `:6644 DEPOT-ARR veh=21 real=11 type=6 … destTx=254
  destTy=234` 提示有一条 type=6 的目标格在 254,234，但那行借的是别的订单表，需确认）。
- U-2：车站 12 的站台是否同时含 y=233 与 y=234 两条到发线（若含，③ 的"选空闲股道"成立）。

### 6.9 玩家口径拍板（2026-10-22）：四种等待点 + 「寻路失败」语义分层

**P-1 等待点白名单**（玩家原话）：「列车应当可以在**尽头式路点、通过式路点、车站和车库内**
等待挂接目标。」
⇒ 等待挂接**不是**路点/车站的附属语义，而是 R3R 自己的一等能力：任何"能让列车合法停稳的格"
都应当能当 `WAIT_COUPLE` 的锚点。落地含义：
- **通过式路点**：订单的 non-stop 只影响"正常到站要不要减速停"，**不影响挂接等待**；
  目标未到位时应当在此格停住等。
- **尽头式路点**：已有反转语义，等待时**不得**触发多余掉头（等 ≠ 到站）。
- **车站**：站台任意股道都能等；但必须有办法判断"这条股道能不能与目标相接"
  （本轮 y=233 / y=234 就是反例：能等在这条股道上，却永远挂不上旁边那条）。
- **车库**：库格上等待现在被 `RESV-NOBOOK reason=parked-waiter`（KI-208）与 depot 分支
  特殊对待，需要纳入同一套等待判定，而不是各自例外。

**P-2 失败语义分层**（玩家原话）：「挂接寻路失败应当作为一个游戏出 bug 时候的回滚，
而列车长时间找不到配对目标不算做需要回滚的场景。」
⇒ 两类必须彻底分开，现状是把它们混成了一条路（`found=0` ⇒ 保留旧预留继续开）：

| 类别 | 判据 | 正确处置 |
|---|---|---|
| **真失败（异常，属 bug）** | 自身几何/状态自相矛盾：目标就在紧邻格却算不出路径、路径穿越自身、预留与自身冲突、`ConsistChanged` 后状态不一致 | 回滚到已知良好状态 + 报错（**这才是"回滚"该待的地方**） |
| **未就绪（正常）** | 配对目标还在跑自己的订单、未停稳、未进 `WAIT_COUPLE`、尚未登记为可挂对象 | **原地等待 + 低频重试**；不回滚、不清成无路、**更不得保留旧路线继续行驶** |

⇒ 现状违背 P-2：`found=0` 一律走"保留旧预留继续行驶"（`train_cmd.cpp:15465/15480` 的
early-return），于是"未就绪"被兑现成了"顺着旧路线跑掉"（本轮 y=233 直行 25 格）。

**P-3 玩家疑问**：「我不知道为何会挂接寻路失败。」
⇒ 待查清单（下一组取证）：
- **Q-1**：`found=0` 的判定点在哪个函数、打点在哪、前置过滤逐条是什么
  （候选车是否必须已 `Stopped` / `OT_WAIT_COUPLE` / 同站台 / 同轨 / 已配对）。
- **Q-2**：`found=0` 之后有没有"未就绪"的专门分支（预期：没有，这是要新建的 C-6）。

**修法清单（随口径更新）**
- **C-4**（扩写）：把"在路点等"扩成"在四类等待点等"——尽头路点 / 通过式路点 / 车站 / 车库。
- **C-6**（新增，直击 P-2）：把 `found=0` 拆成"未就绪"与"真失败"两条路；
  未就绪 ⇒ 原地等 + 低频重试（复用 KB 的 `CPL-SKIP retryIn` 机制），且**必须清掉/不保留**
  那条与挂接无关的旧预留；真失败 ⇒ 打 `CPL-PATHBUG` 类探针 + 回滚 + 停车报错。
- **C-1′**：未越过最后一个通往目标的岔口 ⇒ 停车原地等（与 C-6 互为保险）。
- **C-5**：`found=0` 时禁止退化到"普通去车站"寻路（本轮错误到发线的直接成因）。

### 6.10 Q-1/Q-2 取数结果：「挂接寻路失败」到底是什么（带行号）

**Q-1 打点与三种成因**：`CPL-PATHFOUND found=N` 打在 `src/pathfinder/yapf/yapf_rail.cpp:825-841`
（`YapfTrainCoupleTrack()` 内 `Yapf().FindPath(v)` 之后）。返回 `INVALID_TRACK` 有**三种完全不同的**
成因，共用一个返回值：

| # | 成因 | 代码位置 | 是否"失败" |
|---|---|---|---|
| 1 | **节流跳过**（上次空搜索后 8 tick 内不搜） | `yapf_rail.cpp:1777-1801`（`R3RCoupleScanHold`）→ `:1854-1864` 直接 `return INVALID_TRACK` | **不是**，只是"本轮不搜" |
| 2 | **真的扫不到合法目标** | YAPF 全图搜完无候选（`PFD-ACCEPT` 零命中） | **不是 bug**，是"未就绪" |
| 3 | **有候选但被闸门拒** | `PFD-REJ-GROUP / FIT / TARGET / ORDER` | 视具体闸门而定 |

本轮 tick 3005 属于第 2 类的"未就绪"：等待组 24 当时还在跑自己的 17 条订单，
`tick 3014` 才 `ORD-IDX-WATCH veh=24 real=10->11` 进 `WAIT_COUPLE`（`:7872`）。

**Q-1b 上层处置分支（`src/train_cmd.cpp:15426-15506`，这才是关键）**：

| 分支 | 行号 | 行为 |
|---|---|---|
| `path_found != INVALID_TRACK` | `:15436` | 用挂接寻路的结果当 `best_track` |
| 节流跳过（`R3RCoupleSearchSkipped`） | `:15465` | **保持现状**返回，不撤预留、不标 stuck |
| **`cur_speed != 0`（车还在动）** | **`:15480`** | **保持现状**返回，"让它走完手上的预留" ← **本轮漏网点** |
| 停稳 `cur_speed == 0` | `:15482-15506` | `FreeTrainTrackReservation` + `MarkTrainAsStuck` + 原地等 |

`:15482-15502` 的注释逐字写着玩家 2026-10-03 的口径：
「a locomotive that finds nothing to couple onto must NOT set off towards the order destination,
it must keep waiting where it stands…The pathfinder is re-run on every retry (TrainCoupleHandler),
so the rendezvous happens by itself as soon as a legal target appears.」

⇒ **P-2 的口径其实已经在代码里了**，只适用于"停稳的车"。问题是本轮 21 以 30 速行驶，
走的是 `:15480` 那条"保持现状"分支，而它手上的预留**是旧路线**（不是挂接路径），
于是"保持现状" = "顺着旧路线开到底"。**修法就落在这里：`:15480` 必须先判"这条预留通不通到挂接点"。**

**Q-2 P-1 四类等待点的现状核查**：

| 地点 | 能不能"停住" | 能不能"等" | 缺口 |
|---|---|---|---|
| 车站（任意股道） | 能（普通到站） | 能（`cur_speed==0` 后进 wait gate） | **不能判断"这条股道能否与目标相接"**（本轮 y=233/y=234） |
| 车库 | 能（入库） | 能（另有 `r3r_couple_depot` 专支） | 需纳入同一套判定（P-1） |
| 尽头式路点 | 能（`OrderWaypointFlag::Reverse` 会触发掉头） | 能 | 等待时**不得**触发多余掉头 |
| **通过式路点** | **不能** ← **唯一硬缺口** | 不能（停不下来就进不了 wait gate） | 需 C-4 |

**通过式路点为什么停不住**（`train_cmd.cpp:1078-1084`）：
`Order::ShouldStopAtStation(v, st, /*waypoint*/true)` 对 waypoint 的语义是
「non-stop ⇒ 不停车」，于是制动判据 `limit_advisory_speed()` 不把路点当停车点
（`:1081-1083` 只在 `OrderWaypointFlag::Reverse` 时才限速到 0），
`TrainController` 到点后直接推进订单（`:14779-14790`）。
⇒ 想"在通过式路点等"，必须**由 R3R 自己的判据**在到点时刻拒绝推进并刹车。

### 6.11 Q-1 完整答复：`found=0` 的两层准入链逐条（P-3 的正面回答）

P-3 的答案不是"某一条判据太严"，而是**两层判据不同步**：配对锁由 A 层建立，
B 层却**硬要求**配对已存在。A 层还要求候选**已经是** `OT_WAIT_COUPLE`。

| 层 | 函数/位置 | 使用的准入函数 | 是否要求"已成对" |
|---|---|---|---|
| **A 配对锁选择** | `train_cmd.cpp:12864-12918`（`TrainCoupleHandler` 尾段池扫描，8 tick 节流 `R3R_PAIR_RESCAN_TICKS=8` `:12858`） | `R3RCoupleAllowedIgnoringPair()` | **不要求**（正是它负责建锁 `:12924`） |
| **B 挂接寻路目的地** | `yapf_destrail.hpp:417-601`（`PfDetectDestination`） | `R3RCoupleAllowed()` = `R3RCouplePairMatches()` **且** IgnoringPair（`couple_group.cpp:597-608`、`:528-535`） | **硬要求**（`c->r3r_couple_target==t->index && t->r3r_couple_requester==c->index`） |

⇒ 因此 **B 层不可能"先发现候选、再促成配对"**：没有锁就一律 `PFD-REJ-GROUP`。
`PFD-REJ-GROUP` 这个名字因此是误导——它绝大多数时候报的是**配对锁不匹配**，
而不是挂接分组不兼容（分组判据在同一个函数里，排在配对锁之后）。

**A 层逐条（`train_cmd.cpp:12864-12918`）**：

| # | 行 | 判据 | 不满足的后果 |
|---|---|---|---|
| A1 | `:12865` | 不是自己 | 跳过 |
| A2 | `:12876` | `t->First()==t` 且与 coupler 不同链 | 跳过，并顺手 `R3RClearStaleWaitMarker(t)` |
| A3 | `:12880` | `R3RIsCoupleTarget(t)`（`train_cmd.cpp:2111-2146`：`IsSegmentFront()`★ + `First()==t` + `!R3RChainHasRealArticPart()` + `current_order.IsType(OT_WAIT_COUPLE)`） | 跳过 |
| A4 | `:12881` | **`!t->vehstatus.Test(VehState::Stopped)`**（候选不得处于 Stopped） | 跳过 ← **疑点 REG-272-C** |
| A5 | `:12891` | `R3RCoupleTargetAtOrderStation(coupler,t)`（KI-319，订单目的地/股道站匹配） | 跳过 |
| A6 | `:12896-12910` | 已被别人锁 ⇒ 比 `R3RCoupleEnterTick()`，早者赢（`CPL-PAIR-STEAL`） | 跳过 |
| A7 | `:12911` | `R3RCoupleAllowedIgnoringPair()`：目的地（`couple_group.cpp:384-411`）+ 分组白名单（`:434` 起，`R3RCoupleGroupMasksCompatible`）+ 跨公司 | 跳过 |
| A8 | `:12913-12917` | 取曼哈顿距离最近者 | — |

**B 层逐条（`yapf_destrail.hpp:417-601`，对应 census 计数）**：

| # | 行 | 判据 | census 计数 / 探针 |
|---|---|---|---|
| B1 | `:424` | 只算站台格或车库格 | `noplat` |
| B2 | `:435-439` | 有预留，或本身就是站台格 | `nores` |
| B3 | `:443-448` | `GetTrainForReservation()`；排除"自己这种正在 `GOTO_COUPLE` 的主动方" | — |
| B4 | `:456-478` | 站台沿轴扫；只认 `tr->IsFrontEngine() && R3RIsCoupleTarget(first)` | `noconsist` |
| B5 | `:549` | `R3RCoupleAllowed()` ← **配对锁 + 目的地 + 分组** | `PFD-REJ-GROUP`（why=0） |
| B6 | `:555` | `TrainFitStation()` | `PFD-REJ-FIT`（why=1） |
| B7 | `:566` | `R3RIsCoupleTarget()` | `PFD-REJ-TARGET`（why=2） |
| B8 | `:573` / `:590` | car-only 直接放行；否则须 `IsPrimaryVehicle && OT_WAIT_COUPLE` 且 `CheckOrderLoad && CheckOrderCargoType && CheckNumberOfWagons` | `PFD-REJ-ORDER`（why=3） / `PFD-ACCEPT kind=waiting` |

**⇒ 本轮 tick 3005 的 `found=0` 归因（正面回答 P-3）**：
1. 24 在 tick 3014 才 `ORD-IDX-WATCH real=10->11` 进 `WAIT_COUPLE`（`:7872`），故 tick 3005 时
   A3 直接不成立 ⇒ A 层扫不出 `best` ⇒ **没有配对锁**；
2. 即便 B 层此刻扫到 24，B4（`R3RIsCoupleTarget` 为假）与 B5（无锁）两道都会拒 ⇒ `found=0`；
3. 全日志首条 `CPL-PAIR` 出现在 tick 3019（`:7889`）——**晚于**该次寻路 14 tick。

⇒ 结论：这**不是寻路 bug，是"未就绪"**，与 P-2 的分类完全吻合。真正的问题是**时序+处置**：
A 层 8 tick 节流、B 层立即要求成对 ⇒ 中间窗口内 `found=0` 被 `:15480` 兑现成"保持现状"
（顺旧预留跑掉）。这正是"找到了点却停不下来"的机制。

**C-6 因此可以写成一个纯函数判据**（不猜、不看距离）：
- `found=0` 且 `!R3RHasCouplePair(consist)` 或 `partner` 不满足 A3/A4 ⇒ **未就绪**
  ⇒ 停车原地等（**禁止保留旧路线**），复用 `CPL-SKIP retryIn` 低频重试；
- `found=0` 且配对已成立（`R3RHasCouplePair` 真、候选在场、闸门全过）⇒ **真失败**
  ⇒ `CPL-PATHBUG` 探针 + 回滚 + 报错。
- C-5 独立成立：`found=0` 任何情况下都不得退化到"普通到站"寻路。

**新增疑点（按规则就地登记）**
- **REG-272-C（疑点，待验证）**：A4 `:12881` 把 `VehState::Stopped` 的候选**排除**在候选集外。
  玩家口径 P-1 要求"可以在车站内等待被挂"，而玩家让列车"停在这里等"最自然的手段就是
  Stop 命令（会置 `VS_STOPPED`）。若等待组被手动 Stop，它将**永远**不会成为 A 层候选。
  期望值＝Stopped 的 `OT_WAIT_COUPLE` 链仍是合法候选；实测值＝被 `continue` 掉。
  待验证数据：让等待组在站台手动 Stop 后，日志应出现 `CPL-PAIR` 缺失 + `PFD-REJ-GROUP`
  持续 + `found=0`。若成立，A4 与 P-1 直接冲突，必须改。

### 6.12 玩家拍板（2026-10-22 第二轮）：REG-272-C 关闭；C-4 定为正解

**拍板①（原话）**：「手动停止的列车不应该参与挂接作业。」
⇒ **REG-272-C 关闭，A4 `train_cmd.cpp:12881` 的 `!vehstatus.Test(VehState::Stopped)` 是正确的，
不是与 P-1 冲突，而是 P-1 的另一半**：P-1 说的是"地点白名单"（哪里可以等），
这条说的是"车辆状态白名单"（什么状态的车才配被挂）。二者不矛盾：
- 在站台/路点/车库**正常停稳**（跑完自己的订单、`OT_WAIT_COUPLE`、无 `VS_STOPPED`）⇒ 合法等待方；
- 被玩家**手动 Stop**（置 `VS_STOPPED`）⇒ 明确排除，不参与挂接作业。
结论：`VehState::Stopped` 这个过滤条件要**保留并显式写进注释/文档**（现状无注释，容易被下轮误当 bug 删掉）。
B 层寻路侧没有单独判 `Stopped`，但 A 层不建锁 ⇒ B 层 `PFD-REJ-GROUP` 必然拒，间接已覆盖；
**不额外在 B 层加同判据**，避免"同一个口径两处实现"分叉。

**拍板②（原话）**：「让通过式路点成为能够等待挂接配对的地方是正解。」
⇒ **C-4 采纳为正解并升级为必修**：通过式路点（`type=6` 无 `Reverse`）必须能充当
`GOTO_COUPLE` 的"前哨等待点"——后车在路点停住等前车/等待组就位，就绪后自动起步去挂。

**这带来一条新的硬约束（与拍板①联动，务必写死）**：
- **D-A 等待态不得复用 `VehState::Stopped`。** 既然手动 Stop 的车不参与挂接（拍板①），
  那么"在路点等待挂接"就**不能**用 Stop 命令/`VS_STOPPED` 实现，否则等待者会被自己的闸门排除。
  C-4 必须是 R3R 独立实现的一个"挂接等待态"（自持制动 + 订单推进门禁），全程不置 `VS_STOPPED`、
  不误标 `Stuck`。
- **D-B 判据只许有一个实现。** 需要新增一个纯函数（暂名 `R3RShouldHoldForCouple(v)`），
  **至少两个调用点必须共用它**：
  (a) 制动侧——让 `TrainController`/`ShouldStopAtStation()` 的限速把该路点当成停车点；
  (b) 推进侧——到点处理（`train_cmd.cpp:14779-14790` 一带）问同一函数，**返回"等"就拒绝推进
      `cur_real_order_index`**。
  只做 (a) 会"停了但订单已推进"（下一 tick 直接开去挂接点，等于没等）；
  只做 (b) 会"订单没推进但没刹车"（越冲越远，就是本轮 21 跑掉的原形态）。
- **D-C 等待期间必须可恢复。** 每 tick（或沿用 8 tick 节流）复查 `R3RHasCouplePair()` /
  候选 A3/A4 条件；一旦就绪立刻放行，不许留下"永久等待"状态；也不许在此期间撤掉自己脚下的
  预留（否则到点停不住）。

**C-4 与 C-1′/C-5/C-6 的关系（修订后）**：
| 编号 | 内容 | 与拍板的对应 |
|---|---|---|
| **C-4** | 通过式路点/尽头路点/车站/车库四类地点都能等（实现为 D-A/D-B/D-C 的独立等待态） | 拍板②升为必修 |
| C-6 | `found=0` 拆"未就绪"（原地等）/ "真失败"（回滚+报错），判据 `!R3RHasCouplePair()` | 拍板①让 A3/A4 成为"未就绪"的合法组成 |
| C-1′ | 未越过最后一个通往目标的岔口 ⇒ 停车等 | 路点等住的加强保险 |
| C-5 | `found=0` 禁止退化成普通到站寻路 | 否则路点等待会被"普通到站"覆盖 |

### 6.13 C-4 精确落点（已取证，3 处，必须共用同一纯函数）

代码取证：`train_cmd.cpp:1076-1092`（`ApplyLookAheadItem` 的 `TRLIT_STATION`）与
`train_cmd.cpp:14751-14792`（`AdvanceOrdersFromVehiclePosition`）。

| # | 位置 | 现状（原文要点） | C-4 改法 |
|---|---|---|---|
| **C4-1 制动** | `:1078-1084` | `if (order->ShouldStopAtStation(..., is_waypoint)) limit_advisory_speed(item.start + PredictStationStoppingLocation(...), 0, ...)`；否则 `else if (OT_GOTO_WAYPOINT && dest==st && Reverse)` 才限速到 0，位置用 `item.start + v->gcache.cached_total_length` | 新增第三个分支：`else if (R3RShouldHoldForCouple(v, order, st))` ⇒ 用**与 Reverse 分支完全相同**的位置表达式 `item.start + v->gcache.cached_total_length` 限速到 0；**不写** `last_station_visited = st`（没有真正经过） |
| **C4-2 规划期推进** | `:1085-1091` | `if (order->IsBaseStationOrder() && order->GetDestination()==st && v->GetNumOrders()>0) { current_order_index++; AdvanceOrderIndex(...); order = v->GetOrder(current_order_index); ... }` —— **无条件推进**，所以通过式路点在制动规划阶段订单指针就已经跳到下一条 | 条件追加 `&& !R3RShouldHoldForCouple(v, order, st)`。**不做这一步，C4-1 等于白做**：车停了，但 lookahead 已按"下一条=GOTO_COUPLE"去限速/算路径 |
| **C4-3 实际推进** | `:14779-14790` | 到点（base-station 或 `moving_front->tile == v->dest_tile`）后 `SwitchToNextOrder(true)`；注意 `:14776` 已对 `OT_GOTO_DEPOT` / `OT_GOTO_COUPLE` **直接 return**（R3R 既有注释：挂接订单必须保持到挂接真正完成） | 在 `:14788-14789` 的 `SwitchToNextOrder(true)` 之前加同一门禁；`OT_GOTO_COUPLE` 那条 `:14776` **不动** |

**三个必须写死的实现要点**：
1. **不许复用 `OrderWaypointFlag::Reverse`**。`Reverse` 的语义是"到点掉头"（`:1081` 分支后续会走
   换端），拿它实现"等待挂接"会把列车掉个头。C-4 必须是**独立分支 + 独立判据**。
2. **C4-1 与 C4-2/C4-3 必须共用 `R3RShouldHoldForCouple()`**，否则出现两种半成品：
   只 C4-1 ⇒ "停了但订单已推进"（下一 tick 直接开去挂接点）；
   只 C4-3 ⇒ "订单没推进但没刹车"（就是本轮 21 以 `spd=30` 冲过去、`found=0` 也照跑的形态）。
3. **判据内容**＝ `!R3RHasCouplePair(consist)` 或等待方不满足 A3（`OT_WAIT_COUPLE`）/ A4（无
   `VS_STOPPED`）；并沿用 8 tick 节流复查，就绪立即放行（D-C）。**不置 `VS_STOPPED`**（D-A）。

**实现前需确认 1 条**：`Order::IsBaseStationOrder()` 是否包含 `OT_GOTO_WAYPOINT`。
从 `:1081` 与 `:1085` 的组合推断应包含（否则 Reverse 路点永远不推进订单、`last_station_visited`
的 `IsWaitTimetabled` 分支也无意义），但 C4-2 的正确性依赖它，改码前必须 grep 定义确认。

### 6.14 待拍板清单（玩家直接回编号即可，或回「全按建议」）

> 玩家 2026-10-22：「需要我拍板什么你直接说吧，我备忘里面没找到 C6 什么的」
> ⇒ 本节是**唯一权威清单**，之前 C-6/C-5/C-1′ 只散落在对话与表格里，确实不便查。
> **C-4 已拍板为必修（§6.12 拍板②），不在待拍板之列。**

#### C-6　`found=0` 要不要分成「未就绪」和「真失败」

- **现状**：`found=0` 只按速度二分（`train_cmd.cpp:15465` 节流 / `:15480` `cur_speed!=0`
  ⇒ 保持现状顺旧预留继续跑 / `:15482-15506` 停稳 ⇒ 原地等）。
- **问题**：配对还没建立（"未就绪"）时若车还在动，会被 `:15480` 兑现成**保持现状跑掉**，
  这正是本轮 21 沿 y=233 直行、`found=0` 五次仍照跑的形态。
- **选项**：
  - **C-6a（建议）**：拆两态。`found=0 && !R3RHasCouplePair(consist)`（或 partner 不满足 A3
    `OT_WAIT_COUPLE` / A4 无 `VS_STOPPED`）⇒ **未就绪**：一律停车等 + 撤旧预留 + 8 tick 低频重试，
    打 `CPL-NOTREADY`；`found=0 && 配对已成立` ⇒ **真失败**：`CPL-PATHBUG` + 回滚 + 退订单/报错。
  - C-6b（保守）：只加 `CPL-NOTREADY` 探针，不改行为，先取数据。
- **为什么建议 a**：现场已多次出现"未就绪被兑现成开走"，且判据是纯函数（查配对锁 + A3/A4），
  不需要猜距离或看日志。

#### C-5　`OT_GOTO_COUPLE` 期间 `found=0` 允许不允许「退化成普通到站寻路」

- **现状**：允许（寻路失败后按普通到站目的地处理），于是车会开去站台而不是停着等挂接。
- **选项**：
  - **C-5a（建议）**：**一律禁止**。挂接订单期间 `found=0` 只有两种归宿——等（C-6a 的未就绪）
    或报错（真失败），**永不改写成站台寻路目标**。
  - C-5b：允许退化（现状）。
- **为什么建议 a**：这是 **C-4 生效的前提**。C-4 让车在路点/站台停住等，但如果 `found=0` 仍能
  退化成普通到站，车迟早会被"普通到站"的目标带走，路点等待等于白设。

#### C-1′　越过最后一个通往目标的岔口后怎么办

- **现状**：无此判据；车已经过了岔口仍会继续往目标方向冲（物理上再也不可能挂上）。
- **选项**：
  - **C-1′a（建议）**：一旦越过最后一个通向目标/等待方的岔口 ⇒ 停车等（不再往前跑），
    打 `CPL-PASSED-JUNCTION`，等配对就绪/目标移动后重新评断。
  - C-1′b：只记日志，不改行为。
- **为什么建议 a**：它是 C-4 的"加强保险"——路点等待处理的是"还没到"，C-1′ 处理的是"已经过了"。

#### 拍板后的落地顺序（建议）

1. **C-3/D-B 先落地**：抽 `R3RShouldHoldForCouple()` 纯函数（三个调用点共用，§6.13）；
2. **C-5a** 加在 `found=0` 出口，作为 C-6 的前置；
3. **C-6a** 拆两态；
4. **C-1′a** 补岔口判据；
5. **C-4 三处**（C4-1/C4-2/C4-3，§6.13）最后接上，因为它依赖前三步的判据函数。
6. 全部只改 `train_cmd.cpp`（+ 可能 `traincmd` 相关），不碰 `src\*.h`，走 `_tmp_inc_build.cmd` 增量。

### 6.15 玩家第四轮口径（2026-10-22）与逐条答复

**玩家原话（四条，逐字）**：
1. 「C4，是需要玩家手动操作吗，手动操作之后是不是预留就先得在路点终止一下再继续（口径确认）」
2. 「C6，我确实想不出怎么算做真失败，不过拆分未就绪和真失败总是好的，这个我也得确认一下口径（确认什么叫真失败）」
3. 「C5，我希望在真失败的时候还是能回滚的，但是我们得在真失败那里严格收口」
4. 「C1，它预想中理论上不可能发生，所以只加日志就好了」

#### 答 C4：**不需要任何手动操作**；**预留必须终止在路点**——这条是对的，而且是硬性的

- **不需要手动操作**。判据全自动（`R3RShouldHoldForCouple()`），全程**不置 `VS_STOPPED`**
  ——否则等待者会被 A4（`train_cmd.cpp:12881`，见 §6.12 拍板①）当成"手动停止的车"排除掉。
  玩家手动 Stop 的语义是"这列车不参与挂接"，与"R3R 自动等挂接"是相反的两件事，不能混用。
- **"预留先得在路点终止一下再继续"= 正确，且这正是 C4-2 不能省的唯一理由**：
  `:1085-1091` 是**无条件** `current_order_index++`，所以 lookahead 一过路点就把"下一条"算成
  `GOTO_COUPLE`，接着 `ApplyLookAheadItem` 按**挂接点**算路径 ⇒ `TryPathReserve` 沿 lookahead
  把预留铺**过**路点、直抵挂接点。此时车在路点停住、预留却在挂接点：
  ①"等待"从未真正成立（预留已经跨过去了）；②那条穿过去的预留可能压着别人的位；
  ③一旦就绪放行，车是顺着旧预留直接冲，没有"重新评断"的机会。
  **拦掉 C4-2 之后**，lookahead 的下一条仍是路点自身 ⇒ C4-1 在路点限速 0 ⇒ 预留**自然终止在路点格**。
- 因此写死一条不变式：**等待态下"预留终点 == 等待点格"；就绪放行后才允许向前重新铺。**
  与 KI-208 的 `RESV-NOBOOK` 同型（等待者不许重订超出等待点的路线）。
- **实现顺序被这条约束固定为 C4-2 → C4-1 → C4-3**：先拦住推进，再谈限速；顺序反了，
  限速会作用在"已经推进后的下一条订单"上，等于给挂接点限速，反而让车停在半路。

#### 答 C6：真失败了 = **A 层说能挂、B 层说不能挂**（R3R 自己两层判据打架）

玩家"想不出怎么算真失败"，是因为把它当成了"世界状态"问题。它其实是**内部一致性**问题。
R3R 选目标和寻路是两套判据：

| 层 | 位置 | 判据 |
|---|---|---|
| **A 层（选目标、建锁）** | `R3RScanCoupleTargets` `:12845` / `:12881` / `:12891` / `:12911` | `R3RIsCoupleTarget` + 非 `VS_STOPPED` + `R3RCoupleTargetAtOrderStation` + `R3RCoupleAllowedIgnoringPair` ⇒ `R3RPairCoupleTargets()` 建锁 |
| **B 层（寻路，`found=0` 的来源）** | `src\pathfinder\yapf\yapf_destrail.hpp` | `R3RCoupleAllowed`（**含 pair 锁**）+ `TrainFitStation` + `R3RIsCoupleTarget` + 装货/货种/车数 |

于是 `found=0` 可以精确三分，判据全部是现成函数（`couple_group.h:283/289`）：

| 情形 | 判据 | 性质 |
|---|---|---|
| **未就绪** | `!R3RHasCouplePair(consist)`（`R3RGetCouplePairPartner()` 为空） | 世界还没准备好，**不是错误** |
| **暂时不可达** | partner 非空，但 B 层没有明文否决（无 `PFD-REJ-*`）⇒ 路被堵/信号不放行 | 世界暂时不允许，**不是错误** |
| **真失败** | partner 非空（A 层已锁）**且** B 层对同一目标明文拒绝（`PFD-REJ-GROUP/FIT/TARGET/ORDER`） | **R3R 两层判据不一致 = 代码缺陷** |

三种情形的**动作完全相同**（撤预留 + 停住 + 保留订单 + 等），**唯一区别是打不打报错行**：

| 情形 | 预留 | 订单 | stuck | 日志 |
|---|---|---|---|---|
| 未就绪 | 撤（`:15504` 现状） | 保留 | 标记 | 既有 `COUPLE-FAIL` / `COUPLE-DEST-EMPTY` 心跳，**不加新行** |
| 暂时不可达 | 撤 | 保留 | 标记 | 同上，**不加新行** |
| **真失败** | 撤 | 保留（**不回滚**） | 标记 | 新增 `CPL-PATHBUG alock=<partner> found=0 rej=<原因> passed=<0/1>`，一次性抑制防刷屏 |

这样设计的价值：**即使"真失败"判据误判，最坏后果只是多打一行日志**，绝不会让列车做出
危险动作（撤预留/停住/等本来就是所有情形的共同反应）。这就是"严格收口"该有的形态。

#### 答 C5：**现状已经禁止退化了**，要确认的只是"回滚"指哪一种

先纠一处事实（避免重复决策）：`found=0` 时"退化成普通到站寻路"的 fallback
**已于 2026-10-03 玩家拍板后删除**，代码 `:15482-15506` 的注释逐字记着这件事。所以 C-5 **不需要新决策**。
（唯一例外 `:15511` 的 `r3r_couple_depot`：库内挂车仍允许普通寻路进库，合理，不动。）

"真失败时还能回滚"的"回滚"有两种，请指定：

- **R-a 预留回滚（建议＝是）**：撤掉自己铺的预留，回到"没有预留"的干净态。
  现状 `:15504 FreeTrainTrackReservation()` + `:15505 MarkTrainAsStuck()` 已在做，就是玩家要的东西。
- **R-b 订单回滚（建议＝否）**：把 `current_order` 退回上一条 / 跳过 `GOTO_COUPLE`。
  **不要做**：订单一回滚，列车就失去挂接意图，接着去执行更早的订单（下一张多半是 `GOTO_STATION`），
  这在效果上就是**变相**退化成普通到站——正是 C-5 要禁的事；还会重新引入 KI-193/KI-215 的索引回跳。

建议落定：**C-5 = 真失败时只做 R-a；订单锁住不动（保留 `GOTO_COUPLE`，沿用既有 500 tick 的
`COUPLE-DEST-EMPTY` 心跳）**，并把"R-b 明确不做"写进 `:15482` 那段注释。

#### 答 C1′：按玩家口径 = **C-1′b（只加日志）**

不新增机制，把"是否已越过最后岔口"降级为**真失败日志的一个字段** `passed=0/1`
（判据：目标格是否仍在自己前方——`v->dest_tile` 与 `v->tile` 的相对方向对比 `v->direction`；
更省事的版本只打 `dest=x,y dist=%u`，事后人工判读）。既然"理论上不可能发生"，不值得为它写独立判据。

#### 落地顺序（修订，已锁定）

1. **C4-2** 拦 lookahead 推进（`:1085-1091`）——按 C4 的答复，它必须第一个做；
2. **C4-1** 路点限速 0（`:1078-1084`）；
3. **C4-3** 实际推进门禁（`:14788`）；
4. **C-6 三态拆分**（`:15465` / `:15480` / `:15506` 三个出口 + `R3RHasCouplePair`）；
5. **C-1′ 日志字段**（并入第 4 步的真失败行，不单独立项）；
6. **C-5 不改行为**，只补注释写死"R-b 明确不做"。

### 6.16 玩家第五轮口径（2026-10-22）：C-4 不得误伤普通路点 + C-5 定案

**玩家原话（两条，逐字）**：
1. 「C4我的担心是普通的一个用路点指定路径会不会也被拦停，我期望的是不会」
2. 「C5，既然如此，那就直接保持现状不退化了」

#### 答 C-4：**不会**。判据不认"这是路点"，只认"这列车正在等挂接"

C-4 的判据是 `R3RShouldHoldForCouple(v, order, st)`（§6.12 D-B），落地为**三道串联闸门**，
任一不成立就完全走原代码、一行不改：

| 闸门 | 条件 | 不成立时的行为 |
|---|---|---|
| **G0 订单上下文** | 该路点订单的**紧邻下一条**是 `OT_GOTO_COUPLE` | 普通指路路点（下一单是车站/车库/另一个路点）⇒ **根本不进新逻辑** |
| ~~**G1 在挂接作业中**~~ | ~~`v->current_order` 是 `OT_GOTO_COUPLE`~~ | **此写法是错的，已作废（见 6.17）** |
| **G2 未就绪** | `!R3RHasCouplePair(consist)` 或等待方不满足 A3/A4 | **已就绪 ⇒ 不停**，直接通过去挂 |

⇒ 三种"普通路点"行为逐字不变：
1. **纯指路路点**（`路点 → GOTO_STATION`）：G0 为假 ⇒ 原样穿过、原样掉头；
2. **路径链中的路点**（`路点A → 路点B → GOTO_COUPLE`）：路点A 的紧邻下一条是路点B 而非
   `GOTO_COUPLE` ⇒ 不拦；**只有紧邻 `GOTO_COUPLE` 的那一个**（路点B）可以停。
   玩家 §6.7 原话「前往挂接命令**前面**是前往路点命令」，指的正是紧邻这一条，不多不少；
3. **反向路点**（带 `OrderWaypointFlag::Reverse`）：仍走 `:1081` 既有 Reverse 分支（掉头），
   C-4 **绝不复用 Reverse**（§6.13 要点 1），两条分支互不干扰。

G2 另外保证"能通过就通过"：配对/目标一旦就绪，该路点就是纯通过式——不减速、不停车、不掉头。

**代码佐证（2026-10-22 取证，证明玩家的担心在代码层面是真实的）**：
`order_base.h:351-354` 的 `IsBaseStationOrder()` = `OT_IMPLICIT || OT_GOTO_STATION || OT_GOTO_WAYPOINT`，
而 `train_cmd.cpp:1085` 的推进条件正是 `order->IsBaseStationOrder() && order->GetDestination() == st`
⇒ **不加 G0 闸门时，C4-2 会拦掉全部路点订单的推进**（不只是"等待路点"）。
同族的 `:14779`、`:15626`、`:14788`（`OT_WAITING` + `SwitchToNextOrder(true)`）同理，每处都必须先过 G0。

**负判据（比正判据更重要，必须进复测清单）**：普通指路路点**不得**出现 `CPL-HOLD` 行，
且其 `TrainController` 到点行为与旧 exe 逐字相同。这是防 C-4 反噬的守门条件。

#### 答 C-5：定案 ＝ **维持现状，零改动**

「既然如此，那就直接保持现状不退化了」⇒ C-5 由"待拍板"改为**已定案、不实现**：

- `found=0` 退化成普通到站寻路的 fallback **早已在 2026-10-03 删除**（`:15482-15506` 注释为证），
  现状就是"禁止退化"；
- **R-a（撤预留）与 R-b（订单回滚）都不做**，`:15504-15506` 逐字不动；
- 后续任何一轮**不得**以"体验优化"为名重新引入退化路径。

#### 落地顺序（最终，锁定）

1. **C4-2** 拦 lookahead 推进（`:1085-1091`，内含 G0/G1/G2）；
2. **C4-1** 路点限速 0（`:1078-1084`）；
3. **C4-3** 实际推进门禁（`:14788`）；
4. **C-6** 三态拆分（`:15465` / `:15480` / `:15506`）；
5. **C-1′** 日志字段（并入第 4 步，不单独立项）；
6. **C-5 零改动**（只在 `:15482` 注释里写死「R-a/R-b 均不做」）。

### 6.17 **玩家纠正：G1 判据错了**（2026-10-22 第五轮），并牵出三处连锁死锁

**玩家原话（逐字）**：
> 「等等，列车current order=GOTO_WAYPOINT的时候就应该被拦停了呀，G1判据有问题吧」

**判定：玩家完全正确，且问题比"判据写错"更严重——按原 G1 落地会得到一个必然死锁的 C4。**

G1 原文写的是「`v->current_order` 是 `OT_GOTO_COUPLE`」，但等待中的列车此刻的
`current_order` **恰恰是 `OT_GOTO_WAYPOINT`**（它正停在那条通过式路点上）。
于是原 G1 与 C4 的目的正好相反：C4 要拦的就是 `GOTO_WAYPOINT` 态，而 G1 却把它排除在外。

#### 代码取证：不只 C4 自己的判据，**配对锁的维护入口也卡在同一个条件上**

`train_cmd.cpp:12814 R3REnsureCouplePair()`（每 tick 由 `TrainLocoHandler()` 调用，负责
建立/维护"已有耦合目标"锁）：

```12820:12829:src/train_cmd.cpp
	/* Not executing a GOTO_COUPLE order any more: whatever lock this chain
	 * still holds is stale, ... */
	if (!coupler->current_order.IsType(OT_GOTO_COUPLE)) {
		R3RUnpairCoupleTargets(coupler);
		_r3r_pair_scan_tick.erase(coupler->index.base());
		_r3r_couple_enter_tick.erase(coupler->index.base());
		return;
	}
```

⇒ 列车停在等待路点上时，**每 tick 都会主动解掉配对锁并直接 return**，连全池扫描（`:12864`）
都进不去。后果链：

1. C4 拦住 lookahead 推进 ⇒ `current_order` 停成 `OT_GOTO_WAYPOINT`；
2. `R3REnsureCouplePair()` 因 `:12825` 每 tick 解锁 ⇒ **锁永远是空的**；
3. C4 的"就绪判定"（哪怕改成 `R3RHasCouplePair()`）恒读为"未就绪" ⇒ **永远不放行 = 死锁**。

这与第 1 条互为因果：原 G1 想用 `current_order` 判"在不在挂接作业"，而"不在"恰恰是 C4 制造的状态。

#### 连锁第三处：A 层"目的地规则"也会读错站

`:12882-12884`（KI-319，round 212）在选目标前用 **`coupler` 的当前订单目的地**判断
"候选车底是否停在本次挂接要去的站"。等待态下该订单是**路点**，而车底停在**挂接站**
⇒ 两者不等 ⇒ 合法车底被判成"停在别的站" ⇒ 同样永远配不上目标。

#### 修正后的闸门（C-4 定稿，替代 §6.16 那张表）

| 闸门 | 条件 | 说明 |
|---|---|---|
| **G0 订单上下文** | `current_order.IsType(OT_GOTO_WAYPOINT)` **且** 紧邻下一条是 `OT_GOTO_COUPLE` | 判"这是通过式等待路点"，**不看 `current_order` 是否为 `GOTO_COUPLE`** |
| **G1 就绪** | `R3RHasCouplePair(consist)` 为真 ⇒ **不停，直接通过** | 就绪即纯通过式 |
| **G2 未就绪** | 无配对锁 ⇒ 限速 0 停在路点格 | 预留终点必须止于该格 |

#### 随之必改的两处（否则 C4 上线即死锁，与 C4 同一提交）

- **C4-0a**：`R3REnsureCouplePair()` 的入口条件放宽为
  「`OT_GOTO_COUPLE` **或** 通过式等待路点（G0 成立）」——等待态必须照常建锁/维护锁；
  解锁分支保留，但语义精确化为「既不是 `GOTO_COUPLE`、也不是它的等待路点 ⇒ 解锁」。
- **C4-0b**：A 层目的地校验（`:12882` 一带）与 B 层寻路侧的订单校验，在等待态下改用
  **下一条订单（`OT_GOTO_COUPLE`）的目的地**，而不是 `current_order` 的目的地。
- **C4-0c（新，待拍板）**：等待超时策略。若"下一条 `GOTO_COUPLE`"的目标车底根本不存在，
  配对永远不会就绪 ⇒ 列车被永久钉在路点上。建议＝超时 **N tick（默认 500，与既有
  `COUPLE-DEST-EMPTY` 心跳同量级）后打 `CPL-HOLD-TIMEOUT` 并放行**，让既有的
  `COUPLE-DEST-EMPTY`/`COUPLE-FAIL` 逻辑接手（订单不动，仍不退化为普通到站）；
  另一选项＝保持停住并持续心跳。**待玩家拍板 C-7**。

⇒ 落地顺序修订为：**C4-0a/C4-0b → C4-2 → C4-1 → C4-3 → C-6 → C-1′**。
C4-0a/0b 不先做，C4-2 一上就是死锁。

### 6.18 玩家第六轮（2026-10-22）：C-7 定案 + 质疑 C4-0a/0b 是否必要

**玩家原话（逐字）**：
> 「C7是什么，C4-0c吗，我选择保持停住。然后，C4-0a为什么需要入口放宽到通过式等待路点啊，
> 列车到位路点等待了不就当前命令跳到GOTO_COUPLE了吗，C4-0b同理」

#### C-7 定案 ＝ C4-0c ＝ **保持停住，不超时放行**

- 等待超时**不做**：配对永远不就绪时就一直停在路点格，持续心跳（`CPL-HOLD`，按秒节流）；
- 因此**不得**引入 `CPL-HOLD-TIMEOUT` 这条放行分支；
- ⇒ 与 KI-193（`COUPLE-DEST-EMPTY` 500 tick 后自动跳过订单）存在**语义冲突，见 6.19 的 C-9**。

#### C-8（待验证疑点）：玩家质疑 C4-0a/0b 是否根本不需要

玩家的机制反问很关键：**到达路点后订单不就推进到 `OT_GOTO_COUPLE` 了吗？**
若为真，则我上一轮写的 C4-0a（配对锁入口放宽）与 C4-0b（目的地改看下一条）**都是多余的**，
而且更严重：**C4-2「拦 lookahead 推进」这个设计方向本身就是错的**——它会人为制造
`current_order == OT_GOTO_WAYPOINT` 这个非自然状态（自然流程里它一闪过就被推进掉）。

两种设计对比（待代码确认哪一种是自然流程）：

| 方案 | 路点格上的 `current_order` | 配对锁/目的地校验 | 是否要 C4-0a/0b |
|---|---|---|---|
| **甲：拦推进**（我上一轮的写法） | `OT_GOTO_WAYPOINT`（人为冻结） | 全部判据失效 ⇒ 死锁 | 必须改 0a/0b |
| **乙：到点即推进、推进后停住**（玩家口径） | `OT_GOTO_COUPLE` | 全部判据天然正确 | **不需要**，只需"未就绪不许开始行驶/预留" |

⇒ 待验证判据：`train_cmd.cpp:1078-1095` 区域，waypoint 到点后是否无条件
`SwitchToNextOrder(true)` 使 `current_order` 变成下一条（`OT_GOTO_COUPLE`）。
若成立 ⇒ 采方案乙，**撤掉 C4-0a/0b 与 C4-2**，把 C4 收敛成「在 `TrainCoupleHandler` 之前
加一道"未就绪则限速 0、不预留、不推进"的门」。

### 6.19 取证结论 1：`:1085-1091` 根本不是真实订单推进（我上一轮的 C4-2 落点是错的）

`train_cmd.cpp:1064-1065` 的函数签名逐字为：

```1064:1091:src/train_cmd.cpp
static void ApplyLookAheadItem(const Train *v, const TrainReservationLookAheadItem &item, int &max_speed, int &advisory_max_speed,
		VehicleOrderID &current_order_index, const Order *&order, StationID &last_station_visited, const TrainDecelerationStats &stats, int current_position)
...
			if (order->IsBaseStationOrder() && order->GetDestination() == st && v->GetNumOrders() > 0) {
				current_order_index++;
				AdvanceOrderIndex(v, current_order_index);
				order = v->GetOrder(current_order_index);
```

⇒ 被推进的 `current_order_index` / `order` 是 **lookahead 的局部副本**（引用参数，只服务于
"按下一单的最高速度限制前瞻速度"），与 `v->current_order` **无关**。
真实订单推进由 `ProcessOrders()` 在**车辆实际到点**时执行 `SwitchToNextOrder()`。

**这一条同时证明两件事**：
1. 玩家 C-4 的担心（普通路点被误拦）在 `:1085` 处**不存在**——那里改不改都改不到车辆订单；
   我上一轮把 C4-2 定在 `:1085-1091` 是**找错了落点**。
2. C4-0a/0b 的立论前提（"C4 把 `current_order` 冻结成 `OT_GOTO_WAYPOINT`"）也**不成立**，
   因为 C4 若按自然流程只需让车**停在**路点格，订单该推进还是推进。

#### ⇒ 方案乙成立，C4 收敛为单点改动（待第 2 组取证确认推进时机后定稿）

`ApplyLookAheadItem` 的 waypoint 分支（`:1078` 的 `ShouldStopAtStation` 与 `:1081` 的 Reverse 分支）
本来就是"限速到 0"的正规位置。C4 只需**新增一个并列分支**：G0 成立且配对未就绪 ⇒
`limit_advisory_speed(item.start + v->gcache.cached_total_length, 0, item.z_pos)`（与 `:1082` 同构）。

于是：
- 车减速停在路点格；订单照常推进到 `OT_GOTO_COUPLE`；
- `R3REnsureCouplePair()` 入口天然满足 ⇒ **C4-0a 撤销**；
- A 层目的地校验拿到的是 `GOTO_COUPLE` 的目的地 ⇒ **C4-0b 撤销**；
- 就绪后限速 0 自动消失 ⇒ 无需任何"放行"代码。

#### C-9（**新冲突，必须先解决**）：C-7「保持停住」与 KI-193 的 500 tick 自动跳单

KI-193（`train_cmd.cpp` 的 `R3R_COUPLE_DEST_IDLE_LIMIT=500` / `COUPLE-DEST-EMPTY`）在
`TrainCoupleHandler` 的 `u == nullptr` 分支里，**目的地没有任何车底时 500 tick 后主动推进订单**
（按 `CmdSkipToOrder` 范式 `current_order.Free()` + 索引前进）。

玩家 C-7 选「保持停住」⇒ 若同时保留 KI-193，则"停住"最多 500 tick 就被自己的跳单逻辑放走，
C-7 形同虚设。两者必须二选一或划边界，**待玩家拍板 C-10**：

- **C-10 甲**：C4 的停住只等"配对锁"（目标存在但未锁/未到位），**保留** KI-193 的
  "目的地根本没有车底 ⇒ 500 tick 跳单"（= 承认"永远等不到"的情况仍会走掉）；
- **C-10 乙**：C4 的停住覆盖到"目的地无车底"，即**取消** KI-193 的自动跳单，
  列车永久停在路点并持续心跳（完全贯彻"保持停住"，但一条写错的订单会把列车永久钉死）。

建议＝**甲**（"保持停住"指的是"等到就绪"，而不是"对错误订单永不放弃"）。

### 6.20 取证结论 2：**玩家完全正确**，C4-0a / C4-0b 撤销（已从落地清单删除）

#### 证据链（两行代码闭环）

**① 车头进入路点格 ⇒ 立即 `ProcessOrders()`**

```13656:13659:src/train_cmd.cpp
		case OT_GOTO_WAYPOINT:
			/* If we reached our waypoint, make sure we see that. */
			if (IsRailWaypointTile(moving_front->tile) && GetStationIndex(moving_front->tile) == consist->current_order.GetDestination()) ProcessOrders(consist);
			break;
```
判据是 `moving_front->tile`（车头所在格），**不要求停稳、不要求速度 0** ⇒ 车是一边开一边推进的，
这正是现状"路点只是路过"的机制来源。

**② `ProcessOrders()` 里 `OT_GOTO_COUPLE` 是合法推进目标，且直接写进 `current_order`**

```14719:14728:src/train_cmd.cpp
				case OT_GOTO_WAYPOINT:
				case OT_GOTO_COUPLE:
					/* R3R: GOTO_COUPLE is a travel order. It must be selectable ... */
					this->v->current_order = *order;
					return UpdateOrderDest(this->v, order, 0, true);
```

⇒ **车一到路点格，`v->current_order` 就是 `OT_GOTO_COUPLE`**，正是玩家说的那样。

#### 因此

| 我上一轮的项 | 结论 |
|---|---|
| **C4-0a**（配对锁入口放宽到等待路点） | **撤销**。`current_order` 已是 `GOTO_COUPLE`，`R3REnsureCouplePair()`（`:12825`）的入口天然满足。 |
| **C4-0b**（等待态目的地改看下一条） | **撤销**。`current_order` 就是那条 `GOTO_COUPLE`，目的地天然正确。 |
| **C4-2**（拦 `:1085-1091` 的 lookahead 推进） | **撤销**。那里推进的是 lookahead 局部副本（§6.19），与订单无关，改它毫无意义。 |

#### 修正后的 C4（方案乙，定稿口径）

判据 `R3RShouldHoldForCouple(v)` 改为：

1. `v->current_order.IsType(OT_GOTO_COUPLE)`；
2. **前一单是路点且脚下就是该路点**（"通过式等待路点"，即原 G0 的真实形态：
   `GetOrder(cur_real_order_index - 1)` 为 `OT_GOTO_WAYPOINT` 且 `GetDestination()` == 当前车头所在路点站）；
3. `!R3RHasCouplePair(consist)`（未就绪）。

动作＝**限速 0 + 不订路**（车停在路点格）。

**关键技术点（新发现，必须落进实现）**：不能只在路点订单的 lookahead 里加 `limit 0`——
因为订单推进到 `GOTO_COUPLE` 之后 lookahead 立即按挂接点重算，那一条限速项随之消失，
车会重新起步开走。**"停住"必须挂在 `GOTO_COUPLE` 的状态判据上（上表 1~3），持续生效到就绪为止。**

#### 落地清单（最终版，替换 §6.17/6.18 的两版）

1. **C4-1′** 判据 `R3RShouldHoldForCouple()`（三条，见上）+ 限速 0（唯一新增判据）；
2. **C4-3** 实际推进/订路门禁（`:14788` 一带，需在实现前复核该行现状）；
3. **C-6** 三态拆分（`:15465` / `:15480` / `:15506`）+ **C-1′** 日志字段；
4. **C-5** 零改动；**C-7** 保持停住（不做超时放行）；**C-10** 待拍板。

### 6.21 玩家第七轮：**C-10 定案 ＝ 甲**（KI-193 只保留"目的地无车底"这一种放行）

**玩家原话（逐字）**：
> 「KI-193又是什么啊，是配对了但是目的地没有车底吗，那就可以选甲」

#### C-10 定案（铁律口径）

| 情形 | C4 是否拦停 | 由谁收尾 |
|---|---|---|
| 配对**已建立**（`R3RHasCouplePair()` 真） | 不拦 | 正常进入挂接（`TrainCoupleHandler`） |
| 配对**未建立**，但目标车底**在池中可达**（只是还没锁/还没到） | **拦停**（限速 0，无限等） | C4 自己，等就绪后放行 |
| **目的地根本没有任何车底** | **不拦**（或拦满 500 tick 后放行） | **KI-193**：`COUPLE-DEST-EMPTY` → `R3RUnpairCoupleTargets` → 按 `CmdSkipToOrder` 范式推进订单 |

⇒ **甲**：保留 KI-193 的既有语义，C4 的"保持停住"只覆盖上表第 2 行，**不覆盖第 3 行**。
即「保持停住」= 等到就绪，不是对写错的订单永不放弃。KI-193 的代码**一字不改**。

### 6.22 **重要纠正：KI-193 根本不是"500 tick 跳单"**（上一版 C-10 的冲突前提不成立）

玩家追问「KI-193 又是什么啊，是配对了但是目的地没有车底吗」。取证结果比记忆里的旧版本**已经变了**，
上一轮 C-10 写成"与 KI-193 的 500 tick 自动跳单冲突"是**基于过期记忆的误判**，此处作废。

#### KI-193 现在是两个分支，只有第二个会跳单

**分支一：`COUPLE-DEST-EMPTY`（`train_cmd.cpp:13406-13430`）＝ 原地等，订单保留**

```13406:13416:src/train_cmd.cpp
		/* R3R (KI-193 revised, round 112): standstill on the GOTO_COUPLE destination with
		 * nothing to couple onto -- WAIT IN PLACE. The order is deliberately NOT advanced
		 * (player rule: "no consist to couple onto => wait where you are"); skipping it
		 * would silently give up the rendezvous. ...
		if (R3RCoupleOrderDestinationReached(v)) {
			_r3r_couple_fail_news_shown.erase(v->index);
			/* The pair lock may point at a consist that is not there any more; keeping it
			 * would block the next, legitimate coupling target that shows up later. */
			R3RUnpairCoupleTargets(v);
```

尾部 `return false;`（`:13430`）—— **不推进订单**，只写一行心跳（每 `R3R_COUPLE_DEST_IDLE_LIMIT=500`
tick 一次，由 `:13257-13258` 的 `(tick + index) % 500` 相位门控），并顺手清掉"指向已消失车底"的陈旧配对锁。

**分支二：`COUPLE-COMMIT-FAIL`（`:13509-13545`）＝ 真正会跳单的那种**

触发条件是"**可挂车底就在脚下、`R3RCanCoupleNow()` 已通过、`Couple()` 跑过，但合并不上**"
（折叠回滚 / NewGRF `can attach wagon` 否决 / 铰接死锁），连挂 4 个等待窗口（约一分钟）后
`R3R_COUPLE_COMMIT_FAIL_LIMIT` 才放弃并推进订单。代码注释逐字写明这是"给坏挂接的安全阀，
不是正常的放弃方式"，且**只对"停稳且在目的地格上"的机车计数**。

#### ⇒ 回答玩家的三个问题

| 玩家问 | 答 |
|---|---|
| 是"配对了但目的地没有车底"吗？ | **不是"配对成功"**，是到目的地后 `u == nullptr`（一个可挂候选都没找到，`R3RCanCoupleNow()` 全否）。配对锁此时会主动清掉。 |
| 它会跳单吗？ | **主分支不会**，订单原样保留、原地等。只有"车底在现场但挂不上"的 fallback 分支才跳单。 |
| C-10 选甲有无冲突？ | **无冲突，甲天然成立**，KI-193 代码零改动。 |

#### 对 C4 的两条实质性影响（新增，必须进实现）

1. **C4 拦停后 `COUPLE-DEST-EMPTY` 永远打不出来**：`R3RCoupleOrderDestinationReached()`（`:13179-13189`，
   按 depot/station id 判，不按 tile）对"停在前一站路点"的车恒为假 ⇒ 玩家会失去"它在等车底"的唯一反馈。
   ⇒ C4 必须自带心跳 `CPL-HOLD`（已含在 C-6/C-1′ 的日志项里），否则表现为"车莫名停住、日志无任何线索"。
2. **C4 拦停的车会每 tick 进 handler**：`:13257` 的 500 tick 节流只对"到点"的车上生效，
   off-destination 的车走 `r3r_off_dest` 分支（`:13320-13325`）。性能由两道既有节流兜住
   （3x3 触扫 `R3R_COUPLE_SCAN_PHASE=8`、全图 YAPF 空搜退避 `R3RCoupleScanHold`），
   即 KI-330 U-2 的形态，可接受；但日志侧要给 C4 的等待单独限流，不要复用 `COUPLE-WAIT-OFFDEST`。

### 6.23 开工（第七轮尾）：**待拍板项已清零**

玩家原话：「没有要我拍板的了吗，如果没有就可以开工了」

四项口径全部定案，无悬挂项：

| 编号 | 定案 | 出处 |
|---|---|---|
| **C-7**＝C4-0c | 保持停住，不做超时放行 | §6.18 |
| **C-10** | **甲**：KI-193 的"目的地无车底"仍走原名分支（且确认它本来就是原地等、不跳单，代码零改动） | §6.21 / §6.22 |
| **C4-0a/0b/C4-2** | 全部撤销（玩家机制反问正确） | §6.20 |
| **C4 收敛形态** | 单判据 `R3RShouldHoldForCouple()` + 限速 0 + 不订路 | §6.20 |

**落地顺序（最终，不再变）**：
1. **C4-3**（订路门禁，落点待复核）→ 2. **C4-1′**（判据 + 限速 0）→ 3. **C-6** 三态拆分 + **C-1′** 日志 → 4. **C-5** 零改动。

**取证进度**：
- 已完成：C4 判据形态、`:1085` 真相、KI-193 真实形态。
- 待取证（本轮第 1 组）：`TrainLocoHandler` 停驶分支里 `TrainCoupleHandler(consist)` 的调用点（第 242 轮记录为 `CPL-S0-CHK` 探针所在处），
  确认 C4-1′ 的门是插在"停驶分支"还是"`TrainCoupleHandler` 入口"。

### 6.24 取证完成：C4-1′ 的落点与三个必需 API 已确认

#### ① 配对锁的访问器＝`R3RGetCouplePairPartner(coupler)`

不存在 `R3RHasCouplePair()`（我前几版备忘里写的名字是凭记忆编的，**不存在**）。
真正的访问器在 `:12841` 被使用：

```12839:12845:src/train_cmd.cpp
	/* Still locked onto a consist which still qualifies: nothing to do. This is
	 * the steady state, so it must stay cheap -- no pool walk at all. */
	Train *partner = R3RGetCouplePairPartner(coupler);
```

⇒ "未就绪" ＝ `R3RGetCouplePairPartner(Train::From(v->First())) == nullptr`。

#### ② `R3REnsureCouplePair(consist)` 在 handler 早期（`:19618`），早于速度计算（`:20315`）

```19614:19618:src/train_cmd.cpp
	/* R3R (KI-182): from the moment the "go to and couple" order starts running
	 * the locomotive locks onto exactly one waiting consist, and both sides mark
	 * that lock ("已有耦合目标"); the couple pathfinder then only ever resolves
	 * that one consist. */
	R3REnsureCouplePair(consist);
```

⇒ **在 `:20318` 之前拦停不会造成死锁**：本 tick 的配对锁已经建立/维护完毕，
C4 读到的就是最新锁状态。（这是上一轮的疑虑，已排除。）

#### ③ 插入点＝`:20315-20318` 的 `MaxSpeedInfo` / `UpdateSpeed` 之间

```20311:20319:src/train_cmd.cpp
	int j;
	{
		/* KI-14 (2): both of these walk the whole consist every tick. */
		R3RScopeTimer r3r_spd_timer(&r3r_spd_ns, &r3r_spd_calls);
		Train::MaxSpeedInfo max_speed_info = consist->GetCurrentMaxSpeedInfoAndUpdate();

		if (!mode) consist->ShowVisualEffect(std::min(max_speed_info.strict_max_speed, max_speed_info.advisory_max_speed));
		j = consist->UpdateSpeed(max_speed_info);
	}
```

C4 的动作落在这里，**一次拦停同时实现三件事**：
1. 把 `strict_max_speed` / `advisory_max_speed` 压 0（视觉/加速都不再往上走）；
2. `cur_speed = 0; subspeed = 0;`（保证 `j == 0 < adv_spd`，同 tick 就进停驶分支）；
3. 因为 `j < adv_spd`，**永远不进移动分支**（`:20385` 之后的 `TrainCheckIfLineEnds` / `TrainController`）
   ⇒ **"不订路"（C4-3）自动达成，不需要单独改 `:14788`**。

#### ④ 判据函数签名用非 const `Train *`（避免 const_cast 到处飞）

`R3REnsureCouplePair(Train *)` 同款。判据 2 的"前一单"取 `cur_real_order_index - 1`（卷绕），
车头格取 `GetMovingFront()->tile`（与 `:13656` 的推进判据同一来源，保证语义一致）。

#### ⑤ 已知代价（先记录，后续优化）

`cur_speed = 0` 是**硬刹**：列车头刚进路点格那一 tick 会瞬停，观感突兀。
彻底做法是在 `ApplyLookAheadItem`（`:1078-1084`）给"下一单是 `GOTO_COUPLE` 的通过式路点"
也加一条 `limit_advisory_speed(..., 0, ...)` 让它提前平滑减速；但那里 `v` 是 `const Train *`，
需要 `const_cast`。⇒ 记为 **C4-1″（观感，次优先）**，本轮先做功能正确的 C4-1′。

### 6.25 **C4-1′ 已落地**（`src/train_cmd.cpp`，已改未编译）

**新增判据函数** `R3RShouldHoldForCouple(const Train *v)`（插在 `R3REnsureCouplePair` 之后、`GetCouplePosition` 之前，约 `:12951`）：

```cpp
static bool R3RShouldHoldForCouple(const Train *v)
{
	if (v == nullptr || !v->IsFrontEngine()) return false;
	if (!v->current_order.IsType(OT_GOTO_COUPLE)) return false;

	const VehicleOrderID num_orders = v->GetNumOrders();
	if (num_orders == 0) return false;

	const VehicleOrderID prev_index = (v->cur_real_order_index == 0)
			? static_cast<VehicleOrderID>(num_orders - 1)
			: static_cast<VehicleOrderID>(v->cur_real_order_index - 1);
	const Order *prev = v->GetOrder(prev_index);
	if (prev == nullptr || !prev->IsType(OT_GOTO_WAYPOINT)) return false;

	const Vehicle *front = v->GetMovingFront();
	const TileIndex head_tile = (front != nullptr) ? front->tile : v->tile;
	if (!IsRailWaypointTile(head_tile)) return false;
	if (GetStationIndex(head_tile) != prev->GetDestination()) return false;

	return R3RGetCouplePairPartner(const_cast<Train *>(Train::From(v->First()))) == nullptr;
}
```

**拦截点**（`TrainLocoHandler`，`MaxSpeedInfo` 之后、`UpdateSpeed` 之前，约 `:20315`）：
压 `strict/advisory_max_speed = 0` + `cur_speed = subspeed = 0` + 每 500 tick 一行
`CPL-HOLD tick= veh= tile=x,y real= num= spd=`。

**API 校正（重要）**：`R3RGetCouplePairPartner(const Train *v)` 声明在 `src/couple_group.h:282`、
定义在 `src/couple_group.cpp:537`，**接受 const**（上一版备忘里写成 `R3RHasCouplePair()` 是错的，该名不存在）。

### 6.26 **C4-3 的新结论：仍需要一道门，落点应为 `TryPathReserveWithResultFlags`**

`j == 0 < adv_spd` 只保证**不进移动分支**（`TrainController` / `TrainCheckIfLineEnds` 不跑），
但**不保证 `ProcessOrders()` 不订路**：订单从"路点"切到 `GOTO_COUPLE` 那一 tick，
`UpdateOrderDest()` 会按新目的地调订路。若 C4-3 只靠 `j < adv_spd`，第一 tick 就会
从路点向挂接站订出一条完整路线（= 玩家要避免的"提前订路"）。

⇒ C4-3 的落点是 KI-208 已在用的统一订路入口 `TryPathReserveWithResultFlags()`
（`train_cmd.cpp:10292` 附近，全部门禁都堆在那里，含 `RESV-NOBOOK`），在里面并列加一条：
`R3RShouldHoldForCouple(consist)` ⇒ 记 `TPRRF_NONE` + 节流日志 `CPL-HOLD-NOBOOK` 后 return。
因该函数在 `:12951` **之前**，`R3RShouldHoldForCouple` 需要前置声明。

### 6.27 **C-6 三态拆分 + C-1′ 日志字段已落地**

落地位置：`src/train_cmd.cpp` 的 `ChooseTrainTrack()` 内 found=0 处（约 `:15536`），
以及 `src/pathfinder/yapf/yapf_rail.cpp`（新增"上一次真实搜索被 B 层拒绝了哪些闸门"的记录）。

**为什么要跨文件**：真失败判据需要 `PFD-REJ-GROUP/FIT/TARGET/ORDER` 的计数，那四个计数器是
`yapf_destrail.hpp:40` 的 `_r3r_cpl_scan_rej[4]`（inline 全局），`train_cmd.cpp` 看不到。
故在 `R3RCoupleScanState`（`yapf_rail.cpp`，文件内结构体，**不碰任何 .h**）加一字段
`uint32_t last_rej_mask`，在 `R3RCoupleScanResult()` 里按 `found` 写入（found ⇒ 0，空结果 ⇒
四位打包），并导出命名空间级函数 `uint32_t R3RCoupleSearchRejectMask(const Train *v)`；
`train_cmd.cpp` 在既有 `bool R3RCoupleSearchSkipped(const Train *v);` 手写声明旁并列声明它
（同样是为了不碰 .h）。

**三态判定与动作**（与 §6.15 表格一致，唯一刻意保留的偏差写在第 3 条）：

| 情形 | 判据 | 动作 | 日志 |
|---|---|---|---|
| 未就绪 | `!R3RHasCouplePair(consist)` | **撤预留 + 停住**（`cur_speed=subspeed=0`）+ `MarkTrainAsStuck` + 保留订单 | `CPL-NOTREADY`（500 tick 相位心跳） |
| 暂时不可达 | 配对已锁，`rej mask == 0`，且车还在动 | 保持现状（走完手上预留，第 271 轮口径） | 无新行 |
| 真失败 | 配对已锁，`rej mask != 0`，且车已停 | 与"暂时不可达"（停住时）同——KI-316 处置 | `CPL-PATHBUG`（同一签名只报一次） |

1. **未就绪一律停车**：玩家 2026-10-22 报的"沿 y=233 直线开出 21 格"就是这一态被旧的
   "行驶中保持现状"分支放过。现在 `pair_ready == false` 时先零速、再走共同处置（撤预留 +
   标 stuck + 原地等），下一 tick 若仍没配对就继续按住在原地。
2. **C-5 = 零改动**：三种情形都**不动订单**（不回滚、不改 `cur_order_index`、不报错给玩家）；
   `:15559` 那段 KI-316 注释保留，并把"R-b（回滚订单）明确不做"写进新注释。
3. **刻意保留的唯一偏差**：`pair_ready == true` 且**车还在动**时仍返回 `keep_track`（不清预留），
   因为此时车已经在开往自己锁定的车底，撤预留只会在半路急刹、下一 tick 又照铺。它一旦停下
   （`cur_speed == 0`）就立刻落入上表第 3 行的共同处置。理由与第 271 轮一致，且 §6.15 的
   "最坏后果只是多打一行日志"仍成立。

**日志字段（C-1′）**：

```
CPL-NOTREADY veh=23 tile=1112,125 real=2 spd=0 stuck=1
CPL-PATHBUG  veh=23 alock=22 found=0 rej=0x4 dest=1087,140 dist=6 spd=0
```

`rej=0x4` 即 bit2 = TARGET（`PFD-REJ-TARGET`）；`alock` = 配对锁定的那一半车号（-1=无）。
`dist` 取切比雪夫格距（`dest` 为 `consist->dest_tile`）。**真失败只报一次**：静态
`std::map<int, uint32_t> r3r_couple_pathbug_mask` 按签名（rej mask）比较，签名变化才再报。

## 七、构建自证（第 272 轮）

- 改动文件：`src/train_cmd.cpp`（C4-1′ 判据 + 限速 0、C4-3 订路门禁、C-6/C-1′ 三态拆分）、
  `src/pathfinder/yapf/yapf_rail.cpp`（rej mask 记录 + 导出）。**未碰任何 `src/*.h`**。
- 构建入口：复用既有 `_tmp_inc_build.cmd`（**未新建任何 .cmd**）。
- 结果见下方 §7.1（构建完成后回填）。

### 7.1 构建结果（2026-10-22 14:01，通过）

- **第一次构建失败**：`src\train_cmd.cpp(15589): error C3861: "HasBit": 找不到接受 2 个参数的匹配重载`——
  `consist->flags` 是 `EnumBitSet`，不能用 `HasBit(flags, …)`，须用 `flags.Test(VehicleRailFlag::Stuck)`。
  同一轮 `yapf_rail.cpp` 已编过（`last_rej_mask` + `R3RCoupleSearchRejectMask()` 无语法问题）。
- **第二次构建**：`GUARD` 判增量合法；`[2/3] Building CXX object …train_cmd.cpp.obj` →
  `[3/3] Linking CXX executable openttd.exe`；`build\R3R_incbuild.done = EXIT_CODE=0`（14:01:24）；
  `error C|fatal error|FAILED:|build stopped` 计数 **0**。
- 时间戳链：`src\train_cmd.cpp` 13:58:38 → `train_cmd.cpp.obj` 13:59:28；
  `src\pathfinder\yapf\yapf_rail.cpp` 13:43:32 → `yapf_rail.cpp.obj` 13:50:48；
  `build\openttd.exe` **2026-10-22 14:01:13 / 52 036 096 B**（晚于全部改动源码）。
- `read_lints`（两文件）= **0** 条。
- exe 字面量 4/4 命中：`CPL-NOTREADY`、`CPL-PATHBUG`、`CPL-HOLD-NOBOOK`、`CPL-HOLD tick`。
- 说明：本轮只编**内测探针树** `build\`（按用户 2026-10-19 口径），`build-release` 未动，
  发布版仍停留在不含第 272 轮四项修复的旧版。
