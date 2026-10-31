# 第 328 轮：配对目标仍是障碍（问题 1 未解决）+ 玩家追问「之前能挂上现在不能」

日期：2026-10-26
玩家提供日志：`D:\temp\codebuddy-dropped-files\ffe4362a-93fb-403d-bd70-942c012a939a\R3R_debug.log`（233 671 B / 1 698 行）

## 一、玩家原话

> 问题仍然没有解决，因此，我需要追问：为什么之前同样的场景列车就能挂上，现在又不能了

上一轮拍板（原话）：「探针可以加，然后把自己配对的列车视作空气也可以加（也就是D+日志）」
⇒ 第 327 轮已落地修法 D（`TryPathReserveWithResultFlags()` 的 veto 分支追加配对豁免）+ `TPR-EXIT` 探针，并已编译（EXIT_CODE=0）。**本轮日志证明 D 不是本次死锁的落点。**

## 二、读数（行号即日志行号，原文摘录）

### 2.1 关键事实：真正"挂上了"的是 veh=54，不是 veh=57

| 行 | 原文（截断） |
|---|---|
| 332 | `CPL-PAIRLOCK tick=11532 why=scan act=54 actHead=54 actTile=1,15 actSpd=0 actOrd=16 ... actPairT=0 actPairRR=1048575 tgt=0 tgtHead=0 tgtTile=1,15 tgtSpd=0 tgtOrd=17 ... tgtPairRR=54` |
| 452 | `COUPLE-OK loco=54 rear=226 consist=0 co=1 real=1 type=1 tx=1 ty=15 x=20 y=248` |
| 484 | `ORD-AFTER-COUPLE head=54 n=33 real=1 impl=1 tt=1 borrowed=1 owner=6 u_has_orders=1 own_first=54 u_follow=1` |
| 1207 | `DECOUPLE-DONE u=0 co=1 real=3 tx=12 ty=15 x=196 y=244` |

⇒ 本会话的挂接成功发生在 **tick≈11532，veh=54 挂上 veh=0（T8701/2 车底，33 节，u=0 是链头）**；
随后 **1207 行解挂**，把 veh=0 留在 **12,15**。

### 2.2 之后 veh=57 出发去挂 veh=0，但它**一步没走**

| 行 | 原文（截断） |
|---|---|
| 1315 | `CPL-PAIRLOCK tick=12428 why=scan act=57 actHead=57 actTile=16,14 actSpd=0 actOrd=16 actLen=11 actPairT=0 tgt=0 tgtHead=0 tgtTile=12,15 tgtSpd=0 tgtOrd=17 tgtLen=107 tgtPairRR=57` |
| 1313 | `TPR-EXIT veh=57 state=reach0 pair=0 tile=16,14 spd=0 other=57 otherTile=16,14 otherOrder=16 origin=16,14 okay=1` |
| 1410 | `TPR-EXIT veh=57 state=ok pair=0 tile=16,14 spd=0 other=57 otherTile=16,14 otherOrder=16 origin=16,14 okay=1` |
| 598/691/786/881/975/1080/1412 | `COUPLE-FAIL loco=57 order=16 tx=16 ty=14 x=271 y=232`（→ 1412 行 x=270） |

⇒ 配对锁**是成立的**（`actPairT=0` / `tgtPairRR=57`），但机车 veh=57 全程停在 **16,14**、`spd=0`，
7 次 `COUPLE-FAIL` 期间像素 x 仅 271→270（≈1px 抖动），**从未接近 12,15**。

### 2.3 `TPR-EXIT` 的 pair 位全程为 0 ⇒ 修法 D 从未被触发

8 条 `TPR-EXIT` 里 `pair=1` **一条都没有**；唯一的 veto 是 1286 行：
`TPR-EXIT veh=54 state=veto pair=0 tile=12,15 spd=2 other=0` —— 那是 **veh=54**（挂上后又解挂、再去别处）在 12,15 被挡，且 pair=0（54 与 0 的配对已随解挂解除）。
⇒ **本次死锁与"配对目标被当作实体障碍"无关**，修法 D 不是本轮病因。

## 三、疑点（先落盘，待验）

- **疑点 Y-1（探针自身缺陷，必须修）**：`TPR-EXIT` 里"阻挡车"字段无法区分「没有阻挡车」与「阻挡车链头就是 veh=0」。
  原因：上一轮实现写成 `other_head = (other_chain != 0) ? Train::From(other)->First() : nullptr`，
  而 `other_chain` 为 0 时既可能是 nullptr、也可能真的是 veh=0（本次现场的等待车底链头就是 0）。
  证据：1286 行 `state=veto ... other=0 otherTile=65535,65535`，但 veto 分支**只可能**在有 `other_train` 时进入，
  故该行 `otherTile=65535,65535` 是探针把 veh=0 误判为"无"的痕迹。⇒ 下一版必须把"是否有 other"与"other 的 id"分开打印。

- **疑点 Y-2（本轮真病因，待验证的猜测根因）**：`cur_speed == 0` 的 GOTO_COUPLE 机车进不了寻路/移动分支，
  与第 242 轮 KI-348 记录的「第 4 条结构性路径」同形：
  `TrainLocoHandler` 先 `UpdateSpeed()` 得 `j`，`j < adv_spd`（速度为 0 时恒真）⇒ 只走停驶分支，
  该分支对 GOTO_COUPLE 只做 `CheckTrainCollision() || TrainCoupleHandler()`（命中判据 + 候选搜索，**不寻路、不预留**），
  真正调 `TrainCheckIfLineEnds()` / `TrainController()` 的移动分支永不执行 ⇒ 距离 4 格里永远撞不上。
  待验证判据：日志中应**完全没有** `LOCO-MOVE veh=57`（第 242 轮探针），且 `CPL-S0-CHK` 每 tick 出现。

- **疑点 Y-3**：veh=57 为何会在 16,14 把速度变成 0 并停住（是它自己到点停车、还是被 12,15 的 veh=0 的预留位/信号挡住后刹停）。
  待验证：`SPD-DROP veh=57` 的上一行 + 停车前最后一条 `REACH/RP/CRT` 的 origin。

- **疑点 Y-4（回答玩家"为什么之前能挂上"）**：之前那次能挂上的是 **veh=54**，它在 tick 11532 时 **spd=0 但停在 1,15、
  目标在 1,15（同格）**——同格时 `CheckTrainCollision()` 的命中判据天然成立，不需要移动。
  本次 veh=57 与目标相距 4 格（16,14 vs 12,15），**必须移动才能挂上**，于是暴露了"速度为 0 就不寻路"的死锁。
  ⇒ 这解释了"同样场景之前能挂上、现在不能"：差别不在配对判定，而在**这次需要跨格接近**。
  待验证：对照日志确认 veh=54 命中时 `dist`/`CPL-PAIR dist=` 是否为 0。

## 四、下一步（按优先级）

1. 取证 Y-2（`LOCO-MOVE`/`CPL-S0-CHK`/`SPD-DROP` 是否出现）→ 决定是否按 KI-348 修"停驶分支也要为 GOTO_COUPLE 寻路"。
2. 取证 Y-3（速度归零的提交点）。
3. 修 Y-1 探针缺陷（一行改动，让下一份日志可判读）。
4. 若 Y-2 成立，修法候选：在停驶分支对 `OT_GOTO_COUPLE` 补一次寻路尝试（`TrainCheckIfLineEnds`），
   或对"配对锁成立且目标在射程内"的机车放行最小起步速度。**需玩家拍板后再改行为。**

## 五、状态

- 修法 D（配对豁免）：已实现、已编译，**本次日志证明未被触发 ⇒ 不是本轮病因，保留不撤**。
- 本轮问题：**未解决**，已定位到新方向（停驶机车不寻路），待取证确认后落地。

## 六、第 3 组取证：它其实动了（`:1398`~`:1424`、`:1694`~`:1698`）

- `:1398 CPL-PAIRSCAN tick=12455 actSpd=5 bestDist=5`（已恢复速度）
- `:1400 LOCO-MOVE veh=57 tile=16,14 j=194 spd=5 db=0 dtile=1932`（**首次进移动分支**）
- `:1401 LINEND veh=57 tile=16,14 x=15 sig=0 near=1 spd=5 brk=15`（前方线端）
- `:1402 REVERSEDIR veh=57 tile=16,14 dir=5 order=16 spd=0 rev=0` → `:1406 REVERSEDONE db=1 mvfront=59`（站台尽头自动掉头、改倒车）
- `:1413-1418 LOCO-ORD 0=6 / 1=16 / 2=6 / 3=6 / 4=16 / 5=2`
- `:1419 COUPLE-WAIT-OFFDEST veh=57 real=1 destTile=1932 onStnTile=1`
- `:1421-1424 CPLSPD spd=0→2→3→4 realbrk=1 db=1`（原地重新加速）
- `:1497/1599/1636/1649/1682/1698 LOCO-MOVE` 15,14 → 14,14 → **13,14**，`spd=30`
- `:1648/1680/1697 CPL-PAIRSCAN actTile=14,14/14,14/13,14 bestDist=3/3/**2**`

⇒ **不是永久死锁**：卡约 300 tick（7 次 `COUPLE-FAIL`）后靠一次「站台尽头自动掉头」重获速度，30 km/h 开到 13,14（bestDist=2）；**日志在即将挂上时截止，尚无 `COUPLE-OK`**。

## 七、定论：为什么"之前能挂、现在不能"

tile index 反算（地图宽 128，三处互证）：`1932 = 15*128+12 = (12,15)`、`1808 = 14*128+16 = (16,14)`。

- **上次成功＝同格挂接**：`:371 CPL-PAIR tick=11532 act=54 tgt=0 **dist=0** actTile=1,15 tgtTile=1,15` ＋ `:452 COUPLE-OK loco=54 rear=226`。`dist=0` ⇒ 命中判据天然成立，不需移动/寻路/移动分支。
- **这次＝跨格挂接**：`:1357 CPL-PAIR tick=12428 act=57 tgt=0 **dist=5** actTile=16,14 tgtTile=12,15`。必须跨格接近。
- 而切到 GOTO_COUPLE 的瞬间它正停着：`:593 SPD-DROP veh=57 tile=16,14 prev=5 order=9 real=0` → `:597 CPL-S0-CHK order=16 real=1`，`cur_speed` 已 0 ⇒ `j < adv_spd` 恒真 ⇒ 只走停驶分支（`:597/690/785/880/974/1079 CPL-S0-CHK` ＋ 7 次 `COUPLE-FAIL` ＋ `:1642 CPL-PATHFOUND found=0`）；**`LOCO-MOVE veh=57` 在 `:1400` 前一条都没有**。

⇒ **差别不在配对判定，而在"这次需要跨格接近"**；同格时任何判据都成立，跨格才暴露 KI-348 那条结构性路径（速度为 0 就不寻路）。

## 八、修法候选（**待拍板，本轮未改行为**）

- **B1（最小）**：停驶分支里若订单是 `OT_GOTO_COUPLE`、配对锁成立、与目标距离 > 命中距离 ⇒ `SetForceReserveOnce()` + 最小起步速度，进一次移动分支。
- **B2（结构）**：配对扫描成功时把 `current_order` 的 `dest_tile` 钉到车底所在格并清 `Stopped`/`force_stop`（本次 `dtile` 由 1808＝脚下 变 1932＝车底格 正是这条路径）。
- **B3（零风险）**：只加 `CPL-IDLE-START` 探针（停驶＋配对锁成立＋距离>命中），供下份日志判 B1 的触发面。
- **历史约束**：KI-265（第 173 轮）记过「修法 3 治前因（清 cur_speed／下刹车）会让机车停在挂车距离外再也挂不上，须先拿数据」⇒ 动速度的方案必须先有 B3 数据。

## 九、需要玩家确认的一句话

日志在 `:1698`（veh=57 到 13,14、bestDist=2）截止：**之后它挂上了吗？**
挂上 ⇒ 本题降级为"延误约 300 tick"，修 B1/B2 消除延误；没挂 ⇒ 还有第二道坎，需 `:1698` 之后的日志。

## 十、状态（更新）

- 修法 D：已实现已编译，**本次 8 条 `TPR-EXIT` 里 `pair=1` 为 0 ⇒ 未被触发，不是本轮病因，保留不撤**。
- 探针缺陷 Y-1（`other_chain==0` 与 nullptr 混淆，见 1286 行 `other=0 otherTile=65535,65535`）本轮已定位、**待修**。
- 本轮性质改判：**"停驶分支死等约 300 tick 后才靠自动掉头恢复"**，修法待拍板。

## 十一、第 4 组取证（读源码）：速度是谁清零的 —— **R3R 自己的 KI-499 门**

前九节把速度归零归因于"原生停车"，**这一节推翻了该归因**。源码落点：

- `src\train_cmd.cpp:22229-22242`（`TrainLocoHandler` 内，早于 `j < adv_spd` 判定）：

```cpp
if (R3RShouldWaitForCouplePair(consist)) {
    ... CPL-HOLD-NATIVE 探针（每车 512 tick 一行）...
    consist->cur_speed = 0;
    consist->subspeed = 0;
    j = 0;                       // ⇒ 下一步 j < adv_spd 恒真 ⇒ 必走停驶分支
} else {
    j = consist->UpdateSpeed(max_speed_info);
}
```

- 判据 `src\train_cmd.cpp:14267-14273`（第 321 轮 KI-499「配对锁建立之前不许起步」）：

```cpp
if (v == nullptr || !v->IsFrontEngine()) return false;
if (!v->current_order.IsType(OT_GOTO_COUPLE)) return false;
if (v->track == TRACK_BIT_DEPOT) return false;      // 库内例外
return R3RGetCouplePairPartner(v) == nullptr;        // 无锁 ⇒ 按住
```

⇒ **速度归零是 R3R 主动写的**，不是原生刹车；停驶分支不寻路不是"凑巧"，是这道门的必然结果。

### 11.1 与日志时间线对账（结论：门按设计工作）

| 行 | 事件 | 解读 |
|---|---|---|
| `:593` | `SPD-DROP veh=57 tile=16,14 prev=5 order=9 real=0` | 归零发生在 **order=9（implicit）**、real=0；此时订单还不是 GOTO_COUPLE ⇒ 这道门**没参与**（原生站台出发归零） |
| `:597` 起 7 次 | `CPL-S0-CHK veh=57 order=16` ＋ `COUPLE-FAIL` | 已切单到 GOTO_COUPLE(16)、real=1，**锁尚未建立** ⇒ 门按住，每 tick 只做碰撞/挂接判定 |
| `:1315` | `CPL-PAIRLOCK tick=12428 act=57 … actPairT=0 … tgtPairRR=57` | 锁建立（tick=12428） |
| `:1398` | `CPL-PAIRSCAN tick=12455 actSpd=5 bestDist=5` | 建锁后 **27 tick** 速度恢复到 5 ⇒ 门已放行，与注释「锁一建立判据立刻为假」逐字吻合 |

⇒ 七次 `COUPLE-FAIL` **全部落在"锁未建立"窗口内**，是 KI-499 的预期行为，不是寻路故障。

### 11.2 修正后的因果链（取代 §七 的表述）

1. 之前 veh=54 能挂上＝**同格**（`:371 dist=0`）：无锁期间停驶分支的 `CheckTrainCollision()` 直接命中，不需要移动。
2. 本次 veh=57 跨 5 格（`:1357 dist=5`）：无锁期间被按住 ⇒ 不能跨格接近 ⇒ 只能等锁。
3. 锁在 tick=12428 才建立 ⇒ 按住窗口约 300 tick ⇒ 玩家看到"不动"。
4. 建锁后 27 tick 恢复、掉头、30 km/h 追到 bestDist=2（`:1698`）。

⇒ **真问题从"停驶分支不寻路"改判为"配对锁建立得太晚"**。

- **疑点 Y-5（新，最高优先）**：为什么配对锁要等到 tick=12428 才建立？切到 GOTO_COUPLE 后 `R3REnsureCouplePair()` 应在更早位置运行，建锁本应只差几 tick。待取数：`:597`~`:1315` 之间全部 `CPL-PAIRSCAN` 的 `state=` 序列（是否为 `no-candidate` 反复扫描退避），以及 `R3REnsureCouplePair()` 的调用点与扫描节流常量。
- **修法方向随之改变**：B1（停驶分支补寻路）会与 KI-499 的「无锁不许起步」硬冲突（无锁还允许起步等于废掉 KI-499），**不建议**；正解应落在"缩短无锁窗口"（B2 的孪生问题：锁建立时机 / 扫描节流 / 退避），或对「配对锁已建立但速度仍为 0」补一次最小起步。

## 十二、第 5 组取证：`state=none` 的真正含义 —— **根本没有合格的等待车底**

读 `CPL-PAIRSCAN` 行时最大的陷阱：`s_chain / s_target / s_stop / s_dest / s_lock / s_ign` **全是"被该闸门拒掉的数量"**，不是"通过的数量"（源码 `src\train_cmd.cpp:14082/14086/14087/14097/14104/14118` 每处都是 `++r3r_s_xxx; continue;`）。据此重新解读现场：

```
:609 [R3R] CPL-PAIRSCAN tick=11996 act=57 state=none ord=16 destStn=0 actTile=16,14 actSpd=0
     pool=4 s_chain=55 s_target=4 s_stop=0 s_dest=0 s_lock=0 s_ign=0 s_ignd=0
     best=-1 bestDist=0 paired=0 wait=8
     near=27 nearDist=16 nearWhy=not-target nearTile=1,15 nearOrd=0 nearStop=1
```

- `s_chain=55`：55 个车不是独立链头（或与本链同头）被跳过；
- `pool=4`：真正参与评选的**不同链链头只有 4 个**；
- `s_target=4`：**这 4 个全部**倒在 `if (!R3RIsCoupleTarget(t))`（`:14086`）；
- `s_stop/s_dest/s_lock/s_ign/s_ignd` 全 0 ⇒ 后面的"Stopped / 目的地不符 / 被别人锁住 / 分组或授权不合"**一个都没走到**。

⇒ **`state=none` 不是"找到了但被拒"，而是"池子里没有任何一节车处在可被挂接的等待状态"**。`nearWhy=not-target`＋`nearOrd=0` 是最直接的旁证：离得最近的候选 veh=27 当时**订单类型还是 0（无订单）**、停在库内（`nearTile=1,15`、`nearStop=1`）。

### 12.1 候选是怎么"长出来"的

- `:629 tick=12003 DEPOT-ARR veh=27 spd=0 real=0(17) curType=17 stuck=0 tileDepot=1 tx=1 ty=15` ⇒ veh=27 在 tick=12003 才取得 `OT_WAIT_COUPLE(17)`。
- `:1357 CPL-PAIR tick=12428 act=57 tgt=0 dist=5 actTile=16,14 tgtTile=12,15` ⇒ 到 tick=12428 才有第一对锁（tgt=veh 0）。
- ⇒ 11996 → 12428 的 **432 tick 全部花在"等某个车底进入 WAIT_COUPLE"**，这期间每次重扫都是 `state=none`，每次 `state=none` 都让 `_r3r_pair_verdict=false`（`:14136`），进而让 KI-499 的门（`:22229`）继续把 `cur_speed` 摁在 0。

### 12.2 这就是「之前能挂上、现在不能」的最终解释

| | 之前（veh=54） | 本次（veh=57） |
|---|---|---|
| 距离 | `:371 CPL-PAIR … dist=0`（**同格**） | `:1357 CPL-PAIR … dist=5`（跨 5 格） |
| 车底状态 | 早已 `WAIT_COUPLE`，锁立刻建立 | 车底还在跑自己的订单，**432 tick 后才进 WAIT_COUPLE** |
| 无锁期间 | 停驶分支的 `CheckTrainCollision()` 同格直接命中 ⇒ `:452 COUPLE-OK` | 停驶分支跨格打不到 ⇒ 每 tick `COUPLE-FAIL` |

⇒ **不是回归、不是寻路坏了**。同一套 KI-499 规则在"车底先就位"时无感，在"机车先到、车底后到"时表现为长时间原地不动。差别只在**朝谁等**，旧实现允许它带速度滑到目的地干等，第 321 轮之后改为原地等。

### 12.3 修法重估（第 5 组之后）

- **B1（停驶分支补寻路＋给速度）确认作废**：无锁期间放行＝废掉 KI-499 与第 318 轮的 `parked-no-pair` 门，会把"滑到目的地干等"重新引回来。
- **B4 的目标改写**：不是"缩短扫描退避"，而是**在无锁窗口里允许列车"向将要出现的等待点自走一步"**，或在锁建立的一瞬立刻给最小起步（B5）——两者都必须以"有锁"为前提，故 **B5 是唯一零冲突方案**。
- **B6（新，最可能被玩家接受）**：把"无锁按住"从"写死 `cur_speed=0`"改为"**不写速度、只禁止订路**"——即保留 `R3RShouldWaitForCouplePair` 对 `TryPathReserve` 的否决（第 318 轮的原意），但不再清零速度，让机车靠惯性/既有路线滑向等待点附近（这正是第 321 轮之前的行为）。风险＝KI-265 的"滑过头停在挂接距离外"，故需与"车底位置已知时钉 `dest_tile`"（B2）配套。

## 十三、玩家口径修正（2026-10-27）：**确为回归**，回归点＝第 321 轮 KI-499 门

玩家原话：「啊，我说的上次，之前，不是指这个场景的上次，而是指，几天甚至十几天之前，这个同样的场景就没这个问题」。

⇒ §12.2 的「非回归」结论**作废**。这不是"同一规则在两种时序下的表现差异"，而是**昨天新装的一道门把一条一直走得通的路堵死了**。

时间线（据 `R3R_KNOWN_ISSUES.md:18053-18065`）：

| 轮次 | 日期 | 做法 | 结果 |
|---|---|---|---|
| 第 318 轮 | 2026-10-26 | `parked-no-pair` 只否决 `TryPathReserve` | 还能靠惯性滑行（玩家仍报非法滑行） |
| 第 319/320 轮 | 2026-10-26 | `max_speed = 0` 压制 | **物理上压不住**（`min_speed overrules the maximum speed`），现场蠕行 `actSpd=2` |
| **第 321 轮** | **2026-10-26** | `TrainLocoHandler()` 内 `cur_speed = subspeed = 0; j = 0;` | **回归点**：整个移动分支被跳过 |
| 第 328 轮 | 2026-10-27 | 同一场景 | 跨 5 格挂不上、原地 432 tick |

⇒「十几天前没这个问题」的机制解释：那时没有这道门，机车在无锁期间仍走正常移动分支（`UpdateSpeed()` + `TrainCheckIfLineEnds` / `TrainController`），会自己订路、自己开，跨 5 格只是"多开几 tick"；车底晚 432 tick 才进 `WAIT_COUPLE` 也无所谓，因为机车早已在途中/已经挨在旁边。现在这道门把移动分支整段跳过 ⇒ 跨格场景下 `CheckTrainCollision()` 永远打不到 ⇒ 只能等锁，等锁期间一动不动。

⇒ 定性：**跨格挂接从"能"变"不能"，是第 321 轮引入的行为回归**，不是存档差异、不是环境差异、也不是寻路故障。根子是两个诉求硬冲突：**"无锁不许动"（第 319/320 玩家报非法滑行）⇔ "必须开过去才挂得上"**；第 321 轮把门装得太粗，把"乱滑"与"该走"一起按死。

⇒ 修法结论随之简化：**B6 就是"恢复第 321 轮之前"，即玩家记忆里"十几天前能挂上"的那条路径**，不是新设计。B5 只是补丁（不解决 432 tick 干等）。

## 十三·补、玩家二次质疑（2026-10-27）：不是「不走」，是「走错」

玩家原话：「但是，现在的场景不是列车该走的不走，而是走错了啊，配对自己都说成功了，应该没有问题啊」。

⇒ 上一节 §十三 的定性只解释了一半。玩家指出**锁已建立之后**的现象是"走错方向/走错地方"，不是"纹丝不动"。这两件事必须分开：

| 阶段 | 现象 | 已定性的原因 |
|---|---|---|
| 锁建立前（tick 11996→12428 一带，`state=none` 刷屏、7 次 `COUPLE-FAIL`） | 一步不动 | 第 321 轮 KI-499 门 `cur_speed=0` ⇒ 停驶分支不寻路 |
| **锁建立后（`CPL-PAIRLOCK` / `CPL-PAIR` 之后）** | **能动了，但朝错的目标开** | **本轮新疑点 REG-328-A** |

### REG-328-A（疑点，待取证）

**期望**：配对锁 `CPL-PAIR act=99 tgt=75` 成立 ⇒ 机车 `dest_tile` 应指向 `tgt=75` 所在格，放行后朝车底开。
**实测**：玩家肉眼看是走错方向；日志需取"锁建立后第一段预订路线 `RP cur=`/`CPL-TRACE` 终点"与 `tgt` 车底实际 tile 对账。

### 猜测根因（待验证）

**配对锁只"点名"、不"指路"。** `R3RPairCoupleTargets()` 只写 `r3r_couple_target` / `r3r_couple_requester` 两个字段，不写 `dest_tile`；而机车寻路读的 `dest_tile` 仍来自 `OT_GOTO_COUPLE` 订单自身（原目的地可能是车站/车库，或旧订单残留）。⇒ 锁一建立、KI-499 门松开、移动分支恢复执行，机车就按**订单那个旧目的地**订路 ⇒ 表现为"配对成功了却开向别处"。

与第 328 轮备选修法的对应关系：**这正是 B2「配对扫描成功即钉 `dest_tile` 到车底格」从没实现所留下的缺口**。B6 只解决"该动时能动"，B2 才是解决"动对方向"。⇒ 若本条成立，正确修法顺序是 **B2 + B6**，单做 B6 会让"能动了但朝错方向走"变成常态。

### 取证结果（第 1 组，2026-10-27，`build\R3R_debug.log`）

```
:332  CPL-PAIRLOCK tick=11532 why=scan act=54 actTile=1,15  tgt=0 tgtTile=1,15   ← 上一场景（同格，能挂）
:371  CPL-PAIR    tick=11532 act=54 tgt=0 dist=0
:445  CPL-PAIRUNLOCK tick=11946 why=coupled act=54 …           ← 挂上了
:1272 CPL-PAIRUNLOCK tick=12421 why=decouple act=54 actTile=12,15 …  ← 解挂后机车退到 12,15
:1315 CPL-PAIRLOCK tick=12428 why=scan act=57 actTile=16,14 tgt=0 tgtTile=12,15  ← 本次锁建立
:1357 CPL-PAIR    tick=12428 act=57 tgt=0 dist=5 actTile=16,14 tgtTile=12,15
:1505 CPL-PATHFOUND veh=57 found=1
:1508 CPL-BEST veh=57 tile=5,15 td=0 cost=10242     ← ★ 寻路目标 = 5,15，不是车底所在 12,15
:1642 CPL-PATHFOUND veh=57 found=0
```

⇒ **「走错」的实证成立**：锁 `:1315` 明确点名 `tgtTile=12,15`（车底就在那里，`tgtOrd=17` 即 `OT_WAIT_COUPLE` 原地等），但紧接着 `:1508 CPL-BEST tile=5,15` 把寻路目标定在 **5,15** —— 比车底再往西 7 格；机车 16,14 朝 5,15 开，会**从 12,15 旁边越过去**，这正是玩家看到的"配对成功了却开向别处"。

⇒ 与 §十三 的分工进一步明确：锁建立前用 KI-499 门按死（不动）；锁建立后放行，但放行方向由 `CPL-BEST` 决定，而它给的是 5,15。**所以"挂不上"有两个独立缺陷叠加，不是一个。**

### REG-328-A 状态：成立（待确认 CPL-BEST 语义）

已确认：期望目标 12,15（`tgtTile`）≠ 实际寻路目标 5,15（`CPL-BEST`）。
待确认：`CPL-BEST` 打印的 `tile` 是"YAPF 目的地 / 订单 dest_tile"还是"候选车底安全位置"；`td=0` 的含义（`td=0` 若为 trackdir，则 5,15 大概率来自订单 `dest_tile` 而非车底）。
判读影响：若 `CPL-BEST` = YAPF 目的地，则根因就是"寻路用订单 dest、不用车底实际 tile"，修法=B2；若 `CPL-BEST` = 车底侧安全位，则 5,15 说明候选扫描选错了车（扫到的不是 12,15 那个车底），修法在 `R3REnsureCouplePair`/候选接受集。

### 第 2 组取证（2026-10-27）：5,15 极可能是**同一条车底的车尾**

```
:124  R3RDUMP-CHAIN head=0 ow=0 isai=0 n=6 stopped=1 inDepot=1 spd=0 prio=1 borrow=0
      ordCount=6 ordbk=0 curReal=0 curImpl=0 coType=0 realType=6 tile=1,15 tileOw=0 isDepot=1
:598/:691/:786/:881/:975/:1080  COUPLE-FAIL loco=57 order=16 tx=16 ty=14 x=271 y=232   ← 锁建立前，一动没动
:1412                             COUPLE-FAIL loco=57 order=16 tx=16 ty=14 x=270 y=232   ← 锁建立后只挪了 1px
:1642                             CPL-PATHFOUND veh=57 found=0
```

数值对账：`CPL-PAIRLOCK :1315` 给 `tgtLen=107`（像素），一格 16px ⇒ 107/16 ≈ **6.7 格 ≈ 7 格**；车底链头在 `12,15`，往西 7 格正是 **`5,15`**，与 `CPL-BEST tile=5,15`（`:1508`）逐格吻合。

⇒ **新判读（取代"寻路用订单 dest"那条猜测）**：配对锁没有认错车，`5,15` 就是这条 107px 车底的**另一端（车尾）**。R3R 的挂接判定是「机车鼻尖 ↔ 车底 `Last()`」（记忆 79051681 记的 `GetCouplePosition` 语义），车底链头 `12,15` 是**车头、朝向机车**，头对头接不上，必须绕到车尾 `5,15` 那一头才能贴 —— 玩家看到的就是"配对成功了却掉头绕过整条车底往别处开"。

这也解释了"擦肩而过"：机车沿 y=14 往西开、车底躺在 y=15，经过 `12,15` 时那里是车头不是车尾，`GetCouplePosition` 必然不命中。

## 十三·补二、玩家新观察（2026-10-27）：机车实际走到 **5,14**，不是 5,15

玩家原话：「啊，机车不就走偏了，本来去（5，15）的现在变成去（5，14）了吗」。

⇒ 这补上了 §十三·补 留下的最大缺口：`CPL-BEST tile=5,15`（`:1508`）是寻路给的目标格，**机车实际落在同 x、邻 y 的 5,14**，差一格。⇒ 「走错」的精确形态 = **差一格没顶上去**，不是"绕过车底去另一头"（此前 §十三·补 的"绕到车尾"判读据此降级为**不成立/至少不完整**）。

### REG-328-B（疑点，待取证）

**期望**：目标格 `5,15` 上的车底被机车鼻尖顶到 ⇒ 命中挂接。
**实测**：机车到了 `5,14`（相邻股道），与 `5,15` 差一格。
**为什么存疑**：`5,15` 若正被车底自己占着（车底 107px 从 12,15 铺到 5,15），机车**物理上不可能停进 5,15**，只能停在旁边格伸鼻尖过去；而 5,15 在 y=15、机车在 y=14，**两条股道之间若没有可走通的扳道/站台内接，鼻尖永远够不到**。⇒ 极可能不是"停车位置差一格"，而是**寻路认了 5,15 这个终点、却把车引到一条够不到它的平行股道**。

三个候选成因（待数据判读）：
- (a) YAPF 终点判据按 tile 而非按 track/可达面：`5,15` 与 `5,14` 在 tile 邻域上算"就到了"，但股道不同 ⇒ 停车判定成立而挂接判定不成立。
- (b) 车底把 `5,15` 占着 ⇒ 终点格被占用，YAPF 回退到相邻格 `5,14` 当安全位（`CheckSafePositionOnNode` 家族），这是**设计上就够不到**的落点。
- (c) 真正的目标本来就该是"鼻尖贴到车底某一端"，而 `5,15` 只是车节所在的格，不是可停留的格。

### 需要的数据（下一组取证）

1. `build\R3R_debug.log` 里 `5,15` / `5, 15` / `5,14` / `5, 14` 的全部出现行（判断 5,15 上是否是车节、5,14 上是什么）。
2. 车底 `veh=0` 在 tick 12428 前后的逐节几何（`R3RDUMP-CHAIN head=0` 或 `GEO ...` 逐节 tile），确认车底是否真的铺到 5,15。
3. `COUPLE-FAIL` 的随后几行（`tx/ty` 是机车自己的格）：若末条变成 `tx=5 ty=14` ⇒ 机车确实停在 5,14，实证玩家观察。

## 十三·补三、玩家第三次澄清（2026-10-27，关键）：机车**没到过** 5,14，只是**正沿通往 5,14 的股道移动**

玩家原话：「啊，实际上，机车并没有真正抵达（5，14），但是那条股道通往（5，14），机车在挪动肯定会到的」。

⇒ 这句话纠正了 §十三·补二 的记录口径，而且把问题重新定性了：

1. **「一步不动」不再是现象**：锁建立后机车确实在挪（日志 `:1412 x=270` 相对 `:598 x=271`，也是移动的痕迹，只是极慢）。⇒ 第 321 轮 KI-499 门「停驶分支不寻路」只能解释锁建立前的完全静止，**不能解释锁建立后的\"走错/走不快\"**。
2. **真正的问题变成"股道对不对"，不是"到没到 5,14"**：机车在 `y=14` 股道上、沿 x 递减方向朝 `(5,14)` 推进；而车底（tgt=0，107px ≈ 7 格）躺在 `y=15` 股道上，从 `12,15` 铺到 `5,15`。
   ⇒ 两条**平行股道**：机车走 `y=14`，车底在 `y=15`，x 对齐（5 与 5）但 y 差一格。
3. **REG-328-B 因此升格为主疑点**：目标格 `5,15` 与机车可停留格 `5,14` 属于不同股道。若这两条股道在站内**没有可通行的换轨连接**，机车开到 `(5,14)` 后鼻尖永远够不到 `(5,15)` 的车尾 —— 表现为"开到了、就在旁边、就是挂不上"。
4. **反向可能性也要留**：若 `y=14` 与 `y=15` 之间在某个 x 处有转辙/站台横道可通，则机车理应能在那里并入 `y=15`；此时"走不到 5,15"就是**寻路没把换轨点上算进路径**（YAPF 终点判定按 tile、不按 track 连通性），修法落在 `CPL-BEST` 的终点语义上。

### 待取证判据（下一组，优先第 1 条）

1. **`RP cur=` / `CPL-TRACE` 逐格序列**里，机车预订路线是从头到尾都在 `y=14`（`trk` 一直是 TRACK_X 一类），还是中途出现过换轨（`trk` 变 `TRACK_Y` / `LEFT` / `RIGHT`、或 td 变化）⇒ 直接判"股道不连通"还是"连通但没算进去"。
2. 车底逐节 `tile` 是否**全部** `y=15`（若无任何一节落在 `y=14`，则"平行股道"结论坐实）。
3. `CPL-BEST tile=5,15` 的 `td=0` 语义：若 `td` 是 trackdir 且为 `TRACKDIR_X` 一类，说明寻路认为从 `x` 方向就能到 `5,15`，但机车实际在 `y=14` ⇒ 终点 tile 与到达 track 不匹配。

### 第 3 组取证（2026-10-27）：`RP cur=` 逐格序列 —— **股道是连通的，"够不到"假设被证伪**

`build\R3R_debug.log` 里 `RP cur=` 共 24 行，分三次回溯（每次一个候选方向）：

```
# 路线 1
1537  16,14 td=0  depth=0
1538  15,14 td=0  depth=1
1539  14,14 td=0  depth=2
1540  13,14 td=0  depth=3
1541   5,14 td=0  depth=4    ← ★ 玩家看到的那一格
1542   4,14 td=0  depth=5
1543   3,14 td=0  depth=6
1544   2,14 td=3  depth=7  trk=3 bits=0x8
1545   2,15 td=2  depth=8  trk=2 bits=0x5   ← ★ y=14 → y=15 换轨（斜轨）
1546   1,15 td=0  depth=9
# 路线 2
1547  14,14 td=12 depth=2  trk=4
1548  14,13 td=13 depth=3  trk=5           ← 绕到 y=13
1549  13,13 … 1550 5,13 … 1551 4,13 … 1552 3,13 …
1553   3,14 td=2  depth=8  trk=2 bits=0x5   ← y=13 回 y=14
1554   2,14 … 1555 2,15 … 1556 1,15         ← 再下到 y=15
# 路线 3
1557  15,14 td=3  depth=1  trk=3 bits=0x29
1558  15,15 td=2  depth=2  trk=2 bits=0x15  ← ★ x=15 处就有 y=14→y=15 换轨
1559  14,15 td=0  depth=3
1560  13,15 td=0  depth=4
```

三条读数各自确凿：

1. **`(5,14)` 确实在寻路给出的路线上**（`:1541`），与玩家原话「那条股道通往（5，14）」逐字吻合 ⇒ 机车不是在乱走，它在执行一条被判为合法的路线。
2. **两条股道连通，且有两处换轨点**：`(15,14)→(15,15)`（`:1557-1558`，就在机车起点旁边）、`(2,14)→(2,15)`（`:1544-1545`，西端）。⇒ §十三·补三 第 3 条"平行股道永远够不到"的假设**证伪**，不必再查"有没有转辙"。
3. **路线 3 才是正确形态**：`15,15 → 14,15 → 13,15`，即从 x=15 直接下到车底那条股道，向东/向西贴着车底走。而路线 1/2 却选择**沿 y=14 平行股道越过整条车底（13→5 整段跳跃）、到最西端 x=2 才下到 y=15，最后停在 1,15**。

### 新的定性：不是"走偏"，是"绕远"——而且绕远可能是规则要求的

- 车底长 107px ≈ 7 格，`tgtTile=12,15`（`:1315`），若 12,15 是车底**东端**，则车底铺 `12,15 … 5,15`，西端是 `5,15`。
- R3R 挂接判定是「机车鼻尖 ↔ 车底 `Last()`」，即只能贴**尾端**（记忆 79051681）。
- 若车头在 12,15、车尾在 5,15 ⇒ 机车必须从**西侧**接近 5,15 ⇒ 跑到 `5,14` 一带再西绕到 `1,15`、再折回车尾，**这与路线 1/2 的走向完全一致**。
- ⇒ 玩家看到的"走偏"= **规则强制绕到车尾**，不是寻路算错；只是 11 格绕行 + 极慢推进，观感就是"开去了不该去的地方"。

### REG-328-B 状态：证伪（平行股道不连通的说法不成立）

## 十四、玩家第四次澄清（2026-10-27，决定性）：车底头就在 `12,15`，且**头贴、尾贴都可行**

玩家原话：「车底头靠近（12，15），但是，你说的规定问题不成立，因为之前（几天前的exe）在别的场景，我就已经试过了，从头和尾贴上去都是可行的」。

⇒ 两条关键裁决，直接改写结论：

1. **`12,15` = 车底的头**（东端），车底铺 `12,15 … 5,15`，`5,15` 是尾（西端）。
2. **"R3R 只允许贴车底尾端 `Last()`"这条口径被我误用** —— 玩家在几天前的 exe 上实测过**头、尾都能贴**。
   ⇒ §十三·补三 的"绕远是规则必然、属口径问题"**撤回**，判读无效。

### REG-328-A 状态改为：**真 bug（终点舍近求远）**

- 就近可贴点：`13,15`（紧挨车底头 `12,15` 的东侧一格）—— 路线 3（`:1557-1560`：`15,14 → 15,15 → 14,15 → 13,15`）**已经算出来了，而且只差一格就到**。
- 实际采用的终点：`CPL-BEST tile=5,15`（`:1508`）＝ 车底**尾端**，需沿 `y=14` 越过整条车底、从最西端 `x=2` 拐下 `y=15`、再折回 —— 11 格绕行，最终停 `1,15`（`:1546`）。
- ⇒ 机车不是"走偏/够不到/被规则逼着绕"，而是**寻路在"贴头（近）"与"贴尾（远）"之间选错了优先级 / 根本没把贴头算成合法终点**。

### 第 4 组取证（2026-10-27）：`CPL-BEST` 的打印点与"选端"代码（`src\pathfinder\yapf\yapf_rail.cpp:916-966`）

关键代码（`reach()` 内，YAPF 判定到达后处理）:

```916:966:src/pathfinder/yapf/yapf_rail.cpp
   Train *h = ct->First();          // 车底头
   Train *t = ct->Last();           // 车底尾
   ... for (tr : VehiclesOnTile(pNode->GetLastTile())) if (tr->First() == h) best_on_consist = true;
   if (best_on_consist) {
       consist_head_tile = pNode->GetLastTile();      // YAPF 停在车底"头格"上
   } else if (DistanceManhattan(v->tile, t->tile) < DistanceManhattan(v->tile, h->tile)) {
       consist_head_tile = t->tile;                   // 尾端更近 ⇒ 选尾
   } else {
       consist_head_tile = h->tile;                   // 否则选头
   }
   ... fprintf("CPL-BEST veh=%d tile=%d,%d td=%d cost=%d", ..., pNode->GetLastTile(), ...);
```

两条必须分清的事实：

1. **`CPL-BEST tile=5,15` 打印的是 `pNode->GetLastTile()`**，即 **YAPF 自己搜出来的终点格**，不是 `consist_head_tile`（后者由 `CPL-HEAD` 单独打印）。
   ⇒ 所以"选 5,15"不是这里选端逻辑造成的，而是 **YAPF 的搜索/到达判据把 5,15 当成了目标**。
2. 这里的选端（`DistanceManhattan` 取近端）**只用于决定"车底哪一端是拼接端"**，并且注释（KI-200，`:924-935`）明说：只有 YAPF 停在**空站台格**时才用距离判近端；停在车底格上时以 YAPF 的落点为准。

### 由长度重新校准几何（前面几节的"车底铺到 5,15"是错的）

`tgtLen=107px` ≈ 6.7 格 ⇒ 车底从 `12,15`（头，玩家确认）往西铺 **7 节到 `6,15`**（尾）。于是：

- `13,15` = **车头外侧一格**（贴头停车位）—— 合法空位，路线 3（`:1557-1560`）正好在它上面终止。
- `5,15` = **车尾外侧一格**（贴尾停车位）—— 也合法空位，`CPL-BEST` 就是它。
- ⇒ `5,15` 与 `13,15` 是**对称的一对贴车位**，两者都空；机车在 `16,14`，到 `13,15` 只有 4 格、到 `5,15` 要绕 11 格。

### 由此把问题收敛成一句话

**两个合法贴车位里，A\* 的最优解理应是近的 `13,15`，但 YAPF 给出的 best 是远的 `5,15`。**
⇒ 唯一可能的解释只剩两类，二选一（下一步取证）：

- **(甲) `13,15` 从未被当作目标**：目标候选只按"车底尾端外侧"或"车底格"生成，没有"头端外侧"这一项；
- **(乙) `13,15` 被生成过但被否决**：例如该格被别的列车/信号占、或该格的 trackdir 进不来、或被 `CheckSafePositionOnNode` 判不安全（注释 KI-200 提到的正是这一类）。

注意路线 3 的出现（`15,14→15,15→14,15→13,15`）说明**YAPF 确实在 `13,15` 这一带上回溯过**，偏向 (乙) 或"候选生成对了但择优/提交时被替换"。

### 新的定性：问题在"终点候选生成或排序"，不在"路径"

`RP cur=` 三条路线都是**合法可达**的，差别只在终点落在车底哪一端。因此排查落点从"路径/换轨/够不到"整体转向：

- 候选是怎么产生的（是否只把 `First()` 那一端当候选，还是两端都产生）；
- 若两端都产生，`CPL-BEST` 的择优键是什么（cost？距离？顺序？），为何 11 格绕行的远端反而胜出；
- `13,15` 这个"贴尾位"是否在候选生成时就被 `CheckSafePositionOnNode` 一类判据剔除。

### 第 5 组取证（2026-10-27，决定性）：`CPL-HEAD best=5,15 head=5,15 on=1`

```
build\R3R_debug.log:1507   CPL-HEAD best=5,15 head=5,15 on=1
```

这一行的分量：

- `on=1` 就是代码里的 `best_on_consist`，判据是
  `for (tr : VehiclesOnTile(pNode->GetLastTile())) if (tr->First() == h) best_on_consist = true;`
  ⇒ **`5,15` 这一格上停着的车节，正是车底链的 `First()`**，即 `ct->First()->tile == 5,15`。
- `best == head` ⇒ 走的是 `consist_head_tile = pNode->GetLastTile()` 分支（`:936-937`），**曼哈顿选近端那段代码根本没执行**。
- ⇒ 问题的正确问法不是"就近的 `13,15` 为何没被选"，而是：

**`5,15` 是车底链的 `First()`（链序头），`12,15` 是 `Last()`（链序尾）。**
YAPF 的目标判定只认**链头 `First()` 那一端**，路径终点只能落在 `5,15`；`13,15`（贴 `Last()` 侧外侧的合法停车位）**从未进入候选**。机车因此必须绕到 `First()` 那一端 —— 这就是"舍近求远"的全部原因。

### 与玩家口述的对应关系（需一次口径确认）

- 玩家说「车底头靠近 `12,15`」—— 指**物理/运营意义上的车头端**（朝向 `12,15` 那一侧）。
- R3R 的 `First()` 是**链序头**，日志实证它在 `5,15`。
- ⇒ 两者相反：链序头在西端 `5,15`，玩家眼里的车头在东端 `12,15`。

### 最终定性（REG-328-A）

**不是"够不到"、不是"规则只允许贴尾"、也不是"选端选错"，而是：挂接目标候选只生成车底 `First()` 一端，`Last()` 一侧的合法贴车位从未被考虑。**

- 玩家实测「头、尾都能贴」⇒ `Last()` 侧本来就可挂 ⇒ 候选集合先天少一半，机车只能被引向 `First()` 端，路程远 2 倍以上，观感就是"往反方向开"。
- 修法方向（未实施、未拍板）：
  - **(甲)** 候选同时给出 `First()` 与 `Last()` 两端的贴车格，让 A\* 的 cost 自然择优（近端胜出）；
  - **(乙)** 至少把 `Last()` 端补进候选，并让 `reach()` 里那段 `DistanceManhattan` 选近端（`:938-941`）真正可达 —— 目前它是死路，永远进不去。

### 尚未确认

- U-1（口径）：玩家说的"车底头"是否确指物理车头（`12,15` 端），而 `First()` 在 `5,15` 只是链序方向相反。若确认，则"贴哪端"必须按几何端取候选，不能按链序端。
- U-2：`yapf_destrail.hpp` 中生成该候选的确切函数（`AddDestinationCandidate` / `YapfTrainAddDest` 一族）尚未读过，"只认 `First()`"目前由 `CPL-HEAD on=1` 反推。
- U-3：`First()` 为何落在西端 `5,15`（解挂时链序未按运营方向重排？）。

### 十四、玩家第五次表态（2026-10-27，设计口径）：**两端都能挂才是正确，单端可挂是缺陷**

玩家原话：「无论链头在哪边，两端都能挂接不应该才是正确的吗，为什么一端不能挂了」。

⇒ 这是一条**设计口径拍板**，不是现象报告。含义：

1. 「链头在哪边」不该影响能否挂接 —— 挂接能力属于**车底两端**，与链序方向无关。
2. 当前实现是"只有 `First()` 那一端能挂"⇒ 被玩家判定为**缺陷**（该修），而非"R3R 的规则"。
3. 因此修法目标明确为：**让 `Last()` 一端也成为合法挂接端**，两端对称；选哪端由路径代价/距离决定，而不是由链序决定。
4. 由此回看 §十三「规则强制绕到车尾」的推测彻底作废；§十四 前文的"候选只生成 `First()` 一端"就是缺陷本体。

### 仍需跨段取证的点（回答"为什么会只认一端"）

- U-2 升级为必读：`yapf_destrail.hpp` 里车底目标的**候选生成**为什么只产出 `First()` 侧；是硬编码 `->First()`，还是两端都产出但 `Last()` 侧被别的判据否掉。

### 第 6 组取证（2026-10-27）：读 `yapf_destrail.hpp:478-580` —— **修正我自己"只认 `First()` 一端"的说法**

关键代码（挂接目的地判定，逐节点调用）：

```478:544:src/pathfinder/yapf/yapf_destrail.hpp
   /* Only platform or depot tiles qualify. */
   if (!IsRailStationTile(tile) && !IsRailDepotTile(tile)) { ++_r3r_cpl_scan_noplat; return false; }
   ...
   Train *t = nullptr;
   if (has_res) { t = GetTrainForReservation(tile, TrackdirToTrack(td)); ... }
   if (t == nullptr && IsRailStationTile(tile)) {
       /* 沿站台轴正反扫，找停在平台上的等待车底 */
       for (...) { for (Train *tr : VehiclesOnTile<VehicleType::Train>(st)) {
           Train *first = tr->First();
           if (tr->IsFrontEngine() && R3RIsCoupleTarget(first)) { t = first; break; }
       } }
   }
   if (t == nullptr) { ++_r3r_cpl_scan_noconsist; return false; }   // ← 该格无车底 ⇒ 非目的地
   ++_r3r_cpl_scan_consist;
   t = t->First();                                                  // ← 只是把目标归一为链头车对象
```

读完之后必须修正上一节的定性：

1. **目的地判定是"逐格"的，不是"只认链序头"**：搜索走到某个格 `tile` 时，判断的是**这一格上（或同平台回扫到的某格上）有没有合法等待车底**；没有就 `noconsist` 返回 false。
   ⇒ 车底链覆盖的**任何一格**（`12,15 … 5,15` 里任意一格）都**可以**成为目的地，不限于 `First()` 所在格。
2. `t = t->First()` 只是把目标**对象**归一成链头车，方便后续 `R3RIsCoupleTarget` / 分组 / 装货判据，**并不限定落点格子**。
3. ⇒ 所以"只有 `First()` 端能挂"**不是代码硬编码的规则**，`CPL-HEAD on=1` 只说明这次恰好停在 `First()` 所在格，不能反推"另一端被禁止"。

### 修正后的机制解释：不是"链序单端"，而是"只有车头能开进车底那串格"

- 目的地 = **车底占据的格**；`13,15`（车底外侧空格）**不是**目的地 ⇒ 机车不能"停在旁边贴上去"，必须把车头**开进车底那串格中的一格**。
- 车底是一串连续占用的格（`12,15..5,15`），车头只能从**最东端（`12,15`）**或**最西端（`5,15`）**这两格之一进入，中间的格子两端都被车底自己堵住。
  ⇒ 于是"两端都能挂"在实现上退化为"**从哪一端能开进去，就只能在那一端挂**"，观感正是玩家说的"一端能挂、一端不能挂"。
- 这一端的可达性取决于路径/换轨/占用，而不是链序：本次实测可达端落在 `5,15`（`CPL-BEST tile=5,15`），近端 `12,15` 反而没被选中。
  ⇒ 因此**真正待查的下一个问题**是：为什么 `15,14→15,15` 那条"就在起点旁边"的短通道没把 `12,15` 变成更优目的地（`cost` 更小），而让 `5,15`（绕 11 格）胜出。

## 十五、玩家第六次澄清（2026-10-27）：上面那套"只能从两端侵入"的解释**不成立**

玩家原话：「虽然一边需要侵入车底格，但是问题是，前面几天的exe就可以，而且，我们不是说了这个成配对的等挂列车对于自己配对的列车算是空气吗」。

两条硬约束，直接否掉 §十四 的机制解释：

1. **回归证据**：几天前的 exe 在别的场景里**两端都挂成功过**（同一套代码逻辑）。⇒ "只能从某一端进"不是这条路径的固有性质，而是**近期退化**。
2. **口径事实（早已拍板）**：**成对的等待车底，对"和自己配对的机车"而言是"空气"** —— 即不构成物理阻挡、不占碰撞体积。
   ⇒ 那么机车完全可以**把车头直接开进车底任意一格**（含 `12,15`，甚至中间格），不存在"只能从两端进入"的几何限制。
   ⇒ §十四 那句"车底是一串连续占用格，只能从两端进"**作废**（它把车底当成了实体墙，与"空气"口径矛盾）。

### 由此把结论改回：这是**退化 bug**，不是设计退化

- "空气"口径成立 ⇒ YAPF 对 `12,15` 这条路**本来就该畅通**，`13,15`/`12,15` 的 cost 明显小于 `5,15`（约 4 格 vs 11 格）。
- 但日志 `CPL-BEST tile=5,15` 说明 A\* 最终仍把最优解定在远端。
- ⇒ 待查点收敛为：**既然车底对配对机车是空气，`12,15` 一端为什么在 YAPF 眼里变成"不可达/高代价"**。三种可能（下一步逐一排除）：
  - **(a)** "空气"豁免只做在**碰撞判定**里，没做进 **YAPF 的 tile 可通行/安全位判定** ⇒ 寻路层仍把车底格当障碍，只有 `First()` 那格因某种特例可进；
  - **(b)** `12,15` 端被**另一列车**（非配对目标，比如刚解挂的另一半/第三人）实际占用 ⇒ 与"空气"无关；
  - **(c)** 近端路径上有**预留/信号约束**（另一列车的 reserv 覆盖 `15,15`/`14,15`/`13,15`），迫使 A\* 走远端。

### 第 7 组取证（2026-10-27，决定性）：**近端 13,15 / 12,15 已经被算出并预订成功**

`build\R3R_debug.log` 原文（行号为文件内偏移，跨行显示是工具折行，不是真换行）：

```
1534  REACH cur=14,15 td=0 depth=3 trk=0 bits=0x9
1535  REACH cur=13,15 td=0 depth=4 trk=0 bits=0x1
1559  RP cur=14,15 td=0 depth=3 trk=0 bits=0x9
1560  RP cur=13,15 td=0 depth=4 trk=0 bits=0x1
1561  RP-BOOK dir veh=57 tile=13,15 td=0 trk=0 bits=0x1 reserv=plain before=0x0 book=1
1562  RESV-WATCH SET-RAIL tile=13,15 track=0 phase=yapf-couple actor=57 head=57
1563  RP-BOOK plat veh=57 tile=12,15 td=-1 trk=0 bits=0x1 resv=stn before=0x1 book=1
1570  RP-BOOK dir veh=57 tile=14,15 td=0 trk=0 bits=0x9 reserv=plain before=0x0 book=1
1571  RP-BOOK dir veh=57 tile=13,15 td=0 trk=0 bits=0x1 reserv=plain before=0x1 book=1
```

**这组读数的分量（又一次推翻我上一节的定性）**：

1. `veh=57`（配对机车，`act=57`、`head=57`）**真的把路径算到了 `13,15`**（`REACH`+`RP` 链条 `15,14 → 15,15 → 14,15 → 13,15`），并且 **`RP-BOOK` / `SET-RAIL` 都已经把这格真正预订下来**（`before=0x0 → book=1`，随后第二次 `before=0x1` 说明预订已存在）。
2. `RP-BOOK plat ... tile=12,15 ... resv=stn` ⇒ **车底所在格 `12,15` 也被纳入预订范围**（站台整格位）。
3. ⇒ **"候选只生成 `First()` 一端"、"近端从未进入候选"两个说法全部作废**。近端不但进了候选，还进了预留。
4. 而 `CPL-BEST tile=5,15`（`:1507`）出现在这组**之前** ⇒ 时间序是：先按远端 `5,15` 尝试，**之后**才改走/追加了近端 `13,15` 的路径。

### 修正后的定性（REG-328-A 第三版）

- 端点选择**不是**单端硬编码：两端都出现过，近端还被成功预订。
- 因此问题**不在"选哪端"，而在"订完之后为什么没挂成"**：`:1571` 之后再无 `COUPLE-OK`，最终落到 `:1642 CPL-PATHFOUND found=0`。
- 这与玩家「前几天的 exe 两端都能挂」**完全相容**：退化发生在**预留成功之后**（提交/命中/清扫），不是寻路端点。

### 下一步唯一待查（范围已收窄到 70 行日志）

- `:1571` → `:1642` 之间：`13,15`/`12,15` 的预订是被谁清掉的（找 `CLEAR-RAIL`/`CLEAR-STN`/`RP-BOOK ... book=0`），以及 `CPL-PATHFOUND found=0` 之前的最后一次尝试用的是哪个端点。

## 十六、玩家第七次澄清（2026-10-27，决定性）：**预订没被清，是机车不按预订走**

玩家原话：「13,15 和 12,15 的预订没有被清除，它们现在还呆在那里，只是机车不按照预留走了」。

这条把问题性质彻底钉死，并同时否掉我上面三个候选中的两个：

1. **(a) 寻路不可达** —— 否。`RP-BOOK` 已成功，`13,15`/`12,15` 都是可达的。
2. **(b) 被别的车占用** —— 否。若被占，预订不可能成功（`book=1`）。
3. **(c) 预订被清** —— 否。玩家实测**预留还在图上**，本会话结束后仍残留 ⇒ 不是「订了又被扔」。
4. ⇒ **唯一剩下的形态：预留成功且保留，但机车（veh=57）没有沿这条预留行驶。**

### 新的定性（REG-328-A 第四版）

- 缺陷环节从「寻路/端点选择」和「预留清扫」**移到了「机车执行已预留路径」**。
- 这与玩家前几天的 exe 能两端成功完全相容：那只 exe 的执行环节正常。
- 也与"配对车底对机车是空气"相容：空气口径本来就让车底格可进、可订 —— 现状恰恰是**订了却不走**。

### 直接因果候选（按可能性排序，待日志判）

- **E-1：预留终点与机车实际行驶目标不一致** —— `rp()` 订的路径止于 `13,15`（车底外侧），而机车 `dest_tile` / `current_order` 指向别处，`TrainController` 到某点就停/不动，于是预留成为"死订"。
- **E-2：机车根本没进入移动分支** —— 即第 242 轮 KI-348 那条结构性路径：`cur_speed==0 ⇒ j=0 < adv_spd` 恒走停驶分支，永远不进 `TrainController`，预留永远不被消费（本次若 `actSpd=0` 且无 `LOCO-MOVE veh=57`，即命中）。
- **E-3：路径被标记为不可行驶（`stuck` / `reverse` / `force_proceed`）** —— 例如 `R3R-RES-ONCE grant ... force_proceed` 之后机车被要求换向，但换向没执行。

### 判据（一眼可判 E-1/E-2/E-3）

- 若日志中 `veh=57` 在 `:1561` 之后**完全没有** `LOCO-MOVE veh=57` 且 `cur_speed==0` ⇒ **E-2**（停车分支，预留永不被消费）。
- 若有 `LOCO-MOVE` 但只走一两格就停 ⇒ **E-1/E-3**。
- 若出现 `SPD-DROP veh=57` 紧跟 `LOCO-MOVE veh=57` 之后 ⇒ 说明是刹车到 0 而非从未启动。

### 待取证（范围极小）

- `build\R3R_debug.log` 中全部 `veh=57` 的行（`:1490` 之后），只要看有没有 `LOCO-MOVE` / `SPD-DROP` / `TPR-EXIT ... state=` / `REVERSEDIR veh=57`。

### 第 8 组取证（2026-10-27，实锤 E-1）：**预留走 y=15，机车走 y=14 —— 两条不同股道**

`build\R3R_debug.log` 中 `veh=57` 的原文（折行是工具显示，不是真换行）：

```
1563  RP-BOOK plat veh=57 tile=12,15 td=-1 trk=0 bits=0x1 resv=stn book=1
1571  RP-BOOK dir  veh=57 tile=14,15 td=0  trk=0 bits=0x9  before=0x1 book=1
1572  RP-BOOK dir  veh=57 tile=15,15 td=2  trk=2 bits=0x15 before=0x0 book=1
1578  CPL-RESERVE veh=57 reserved=1 next=0
1599  LOCO-MOVE veh=57 tile=15,14 j=210 spd=30 track=01 dir=5 db=1 stuck=0 dtile=1932
1636  LOCO-MOVE veh=57 tile=15,14 j=206 spd=30 track=01 dir=5 db=1 stuck=0 dtile=1932
1642  CPL-PATHFOUND veh=57 found=0
1649  LOCO-MOVE veh=57 tile=14,14 j=196 spd=30 track=01 dir=5 db=1 stuck=0 dtile=1932
1682  LOCO-MOVE veh=57 tile=14,14 j=204 spd=30 track=01 dir=5 db=1 stuck=0 dtile=1932
1698  LOCO-MOVE veh=57 tile=13,14 j=204 spd=30 track=01 dir=5 db=1 stuck=0 dtile=1932
```

**读数把两套几何摆开了：**

| | 股道 | 逐格 |
|---|---|---|
| 挂接 YAPF 预留（`rp()` 订的） | **y=15** | `15,14 → 15,15 → 14,15 → 13,15 → 12,15` |
| 机车实际行驶（`LOCO-MOVE`） | **y=14** | `15,14 → 14,14 → 13,14 → …` 继续向西 |

- 两条路只在起点格 `15,14` 相交，之后**立刻分叉**：YAPF 南下 `15,15`，机车直行 `14,14`。
- 机车 `db=1`（倒车）、`dir=5`、`track=01`（TRACK_X）、`spd=30`、`dtile=1932` —— 它**一直在动，不是卡住**。
- 因此 `13,15`/`12,15` 的预订变成**画面上的死预订**（玩家所见），机车永远走不到。

### 结论（REG-328-A 第五版，已实锤）

> **缺陷 = 挂接 YAPF 算出的路径没有被交给机车执行。**
> 机车仍按自己的行驶目标（`dtile=1932`）与自己的预留跑，两者在 `15,14` 分叉，于是挂接预留成为孤儿。

- 这**不是**寻路 bug（路径算对了、订上了），**不是**预留清扫 bug（预留还在），**不是**端点禁制（`13,15`/`12,15` 都进了候选与预留）。
- 与「前几天的 exe 能两端都挂」完全相容：那只 exe 里机车会按挂接路径走（或两条路恰好重合）；现在分叉了。
- 与「配对车底是空气」相容：空气口径让 `12,15` 可订 —— 恰恰是"订了却不走"。

### 下一步（唯一）

- 查 `R3R-RES-ONCE grant ... tile=12,15` 与 `CPL-RESERVE reserved=1` 之后，**谁负责把这条路径变成机车的行驶意图**（`dest_tile` / `current_order` / `TrainController` 的 `force_proceed` / `R3R` 的 couple 专用行进），以及为什么 `db=1` 下机车选了 `y=14` 而 YAPF 选了 `y=15`（**`db=1` 参照系** 是最大嫌疑，参见 KI-242 的 `GetMovingDirection` 修正）。

---

# 十七、结论总账（防丢索引 / 上下文压缩后可由此恢复）

> 玩家 2026-10-27 明确要求：「可以查，但是我希望我们这么多推导的结论别弄丢了」。
> 本节是**唯一权威状态**：任何结论若要推翻，必须在此处改写，不得只写在对话里。

## 17.1 已确认事实（铁证，不会被推翻）

| 编号 | 事实 | 证据锚点 |
|---|---|---|
| F-1 | 真正第一次挂上的是 **veh=54**，不是 veh=57 | `R3R_debug.log` `COUPLE-OK loco=54` |
| F-2 | veh=57 之后**确实动过**，不是一直静止 | `:1398`~`:1424`、`:1694`~`:1698` |
| F-3 | 日志里出现过 `state=none`，并伴随候选不足 | `CPL-SCAN ... why=none` |
| F-4 | 车底头在 **12,15**；两端（头贴/尾贴）在物理上都可行 | 玩家第四次澄清 |
| F-5 | 平行股道 **y=14 与 y=15** 之间是连通的（`15,14→15,15` 有连接） | `REACH cur=14,15` / `REACH cur=13,15` |
| F-6 | 近端 **13,15 / 12,15 已被 A\* 算出并 `RP-BOOK` 预订成功**（`before=0x0→book=1`、`resv=stn`） | `:1561`、`:1562`、`:1563` |
| F-7 | 这些预订**没有被清除**，会话结束后仍留在图上 | 玩家第七次澄清（实地观察） |
| F-8 | 机车实际行驶在 **y=14**（`15,14→14,14→13,14`），预留却在 **y=15** | `:1599`、`:1649`、`:1698` 的 `LOCO-MOVE` |
| F-9 | 机车是**倒车**状态 `db=1`、`dir=5`、`track=01`、`spd=30`、`dtile=1932` | 同上 |
| F-10 | `CPL-PATHFOUND found=0` 反复出现，`CPL-SCAN ... why=none` | `:1642`、`:1643` |
| F-11 | 同一套代码在几天前的 exe 上**两端都挂成功过** | 玩家第五次澄清 |

## 17.2 定性演进（七次修正，按时间序）

| 序 | 定性 | 被什么推翻 |
|---|---|---|
| ① | 机车**一步没走**（速度被清零） | §六 取证：它动过（F-2） |
| ② | 是 **KI-499 门**（第 321 轮）在停车 | §十二：门按设计工作，根因是**没有合格等待车底** |
| ③ | **走错方向** / 绕到 `5,15` 远端 | §十四：`5,15` 极可能是同一车底的车尾（后被长度校准修正） |
| ④ | 平行股道**不连通**，够不到 | §十三·补三：`RP cur=` 逐格证明连通（F-5） |
| ⑤ | **只认 `First()` 一端**（链序单端） | §十五：近端也进了候选与预留（F-6） |
| ⑥ | 车底是连续占用格，**只能从两端侵入** | 与「配对车底对配对机车是空气」口径矛盾，撤回 |
| ⑦ | 订了又被**清除** | 玩家第七次澄清：预订还在（F-7） |

## 17.3 当前有效结论（REG-328-A 第五版）

> **缺陷 = 挂接 YAPF 算出的路径没有被交给机车执行。**
> 机车仍按自己的行驶目标（`dtile=1932`）和自己的预留跑在 `y=14`，与挂接路径（`y=15`）在起点格 `15,14` 之后立即分叉 ⇒ `13,15`/`12,15` 的预订成为**画面上的死预订**。

- 排除：寻路不可达（F-6 已订上）、被别的车占（`book=1` 成功）、预订被清（F-7）、端点禁制（F-6）。
- 相容：F-11（旧 exe 两条路重合或会被接管）；「空气」口径（空气只保证**可订**，不保证**会走**）。

## 17.4 仍待判的三条（E-1 已实锤，E-2/E-3 未排除）

- **E-1（已实锤）**：预留终点 ≠ 机车实际行驶目标，两套几何在 `15,14` 分叉。
- **E-2**：`cur_speed==0 ⇒ j=0 < adv_spd` 停驶分支（第 242 轮 KI-348 形态）—— 本次 `LOCO-MOVE` 有输出且 `spd=30`，**基本排除**，但需确认停车瞬间。
- **E-3**：路径被标 `stuck`/要求换向但没执行 —— `stuck=0`，**大概率排除**。

## 17.5 唯一未解问题

`CPL-RESERVE reserved=1` 之后，**这条路径本应由谁接管成机车的行驶意图**，以及为什么 `db=1` 下机车选 `y=14`、YAPF 选 `y=15`。

## 17.6 已作废的表述（不要再引用）

<!-- 第 9 组取证落点在下方 17.7 -->

## 17.7 第 9 组取证（2026-10-27，读源码）：挂接寻路的返回链路已定位

三处探针的**确切落点**（`src\pathfinder\yapf\yapf_rail.cpp`）：

| 日志行 | 源码位置 | 含义 |
|---|---|---|
| `CPL-ENTRY veh= tile= orderType= dontReserve=` | `:1987-1995` | 进入挂接寻路的统一入口（`dont_reserve` 参数） |
| `CPL-PATHFOUND found=` | `:873-879` | `Yapf().FindPath(v)` 的返回值 |
| `CPL-RESERVE reserved= next=` | `:1247-1257` | `TryReservePath(nullptr, pNode->GetLastTile(), true)` + `next_trackdir` |

**返回链路（关键）**：

```
CYapfCoupleRail::stFindNearestCoupleTrain(v, dont_reserve)      // yapf_rail.cpp:802-808
  → CYapfTrainRailT::FindNearestCoupleTrain(v, dont_reserve)    // :810 起
      → CPL-PATHFOUND / CPL-SCAN                                 // :873-885
      → TryReservePath(...) → CPL-RESERVE reserved=              // :1248-1256
      → return reserved ? next_trackdir : INVALID_TRACKDIR       // :1257
  → 外层 :1996-2002
      Trackdir ret = ...stFindNearestCoupleTrain(...)
      return (ret != INVALID_TRACKDIR) ? TrackdirToTrack(ret) : INVALID_TRACK
```

**由此得到的确定结论**：

1. 日志 `CPL-RESERVE veh=57 reserved=1 next=0` ⇒ `next_trackdir == 0` ⇒ 外层返回 `TrackdirToTrack(0)`，即**一个合法的 Track**，不是 `INVALID_TRACK`。
   ⇒ 上层拿到的是"有路可走、请走 TRACK_X"这个答复，**不是失败**。
2. 而机车实际 `LOCO-MOVE ... track=01 dir=5` 也是 `TRACK_X` 家族 ⇒ **返回的 Track 与机车实际走的轨属同一类**，说明上层确实用了这个返回值（或机车自己算出同类的轨），但**具体哪一条岔路（y=15 还是 y=14）在执行阶段被换掉了**。
3. ⇒ 缺口精确定位在 **`:2002` 的返回值被谁消费** 这一层：`TrackdirToTrack(ret)` 交给 `TryPathReserve` 之后的 `ChooseTrainTrack` / `TrainController`，那里有没有可能再跑一次普通寻路、从而覆盖掉挂接寻路选出的股道。

**下一步（唯一，未做）**：确认 `:1996-2002` 所在函数的**函数名**与它的调用者（预备搜索 `YapfTrainFindNearestTrain` 零命中，说明该包装使用了别的名字，须读 `:1900-1945` 的函数头）。

## 17.8 第 10 组取证（2026-10-27，补完 17.7 的缺口 + 两条重要校正）

### 校正 C-1：`Direction` 是 **8 方向**，`dir=5` 合法（撤销"非法值"猜测）

`src\direction_type.h:24-36`：

```
enum class Direction : uint8_t {
    N = 0, NE = 1, E = 2, SE = 3, S = 4, SW = 5, W = 6, NW = 7, End, Invalid = 0xFF
};
```

⇒ `LOCO-MOVE ... dir=5` = **`Direction::SW`（西南）**，是合法值。
⇒ 之前任何"`dir=5` 越界/被写坏"的推断**作废**，不要再当线索。

### 校正 C-2：`LOCO-MOVE` 的三个字段口径（打点源码已确认）

`src\train_cmd.cpp:22318-22323`：

```
fprintf("LOCO-MOVE veh=%d tile=%d,%d j=%d spd=%d track=%02X dir=%d db=%d stuck=%d dtile=%u\n",
    ..., (int)consist->cur_speed, (unsigned)consist->track,
    (int)consist->direction, (int)(consist->IsDrivingBackwards() ? 1 : 0),
    ..., (unsigned)consist->dest_tile.base());
```

- `track=` = `consist->track`（`TrackBits`，十六进制）⇒ `01` = `TRACK_X`。
- `dir=` = `consist->direction`（8 方向 `Direction`）⇒ `5` = `SW`。
- `dtile=` = `consist->dest_tile`（TileIndex 整数）⇒ `1932` 是目的地**没有变**。
- **该打点在移动分支 `else` 里**（`:22302` 之后），且注释明写：「the *movement* branch is the only place that ever calls `TrainCheckIfLineEnds()`/`TrainController()`, i.e. **the only place that can reserve a path**」。
  ⇒ 机车确实在移动分支里跑，**E-2（停驶分支）正式排除**。

### 补完 17.7：包装函数名已确认为 `YapfTrainCoupleTrack`

```
YapfTrack YapfTrainCoupleTrack(const Train *v, bool dont_reserve);   // yapf_rail.cpp:1996-2002
  → 由 DoTrainCouplePathfind(v, do_track_reservation) 调用
     （传 !do_track_reservation 作为 dont_reserve）
  → 返回 TrackdirToTrack(ret)，ret == INVALID_TRACKDIR 时为 INVALID_TRACK
```

### 新疑点 D-1（必须优先钉死，否则无法判定"机车走反了"）

`CPL-RESERVE veh=57 reserved=1 next=0`：

- 若 `next` 是 **pathfinder 选出的首步 trackdir**，则 `0` = `TRACKDIR_X_NE`，即 **`TRACK_X`**；
- 而 YAPF 预留下来的路径首步是 `15,14 → 15,15`（**向南**，属 `TRACK_Y` 家族）；
- 两者**按轨道类型就不一致**。

⇒ 只有两种可能，必须二选一，否则"路线分叉"的判定没有地基：

- **(D-1a)** `next` 打的不是"首步轨道"，而是别的量（例如 `pNode->GetLastTrackdir()` 或终点端轨道）⇒ 我的"分叉"走廊判断要重做；
- **(D-1b)** pathfinder 的节点枚举与 `TryReservePath` 的铺轨顺序**互为反向**（从终点往起点铺），于是 `next=0` 是**终点端**的轨道，路径本体仍是 `15,14→15,15→…→12,15`。

### 待取证（下一组，唯一）

- 读 `yapf_rail.cpp:1240-1260`（`CPL-RESERVE` 的 `next_trackdir` 到底取的是哪个表达式），以及 `:1000-1110` 里 `rp()` 的递归方向（是 `cur → tg` 还是 `tg → cur`）。
- 同时确认 `15,14` 的 `bits`：日志 `REACH cur=15,14` 一节里的 `bits=`，判断这格到底是 `TRACK_X` 单轨还是含 `TRACK_Y` 的交叉格 —— 若含 `TRACK_Y`，"南下到 `15,15`"与"直行 `14,14`"才是真岔路；若不含，则 `15,15` 根本不是从 `15,14` 可达，§十六 的"分叉"结论要整体重写。

## 17.9 第 11 组取证（2026-10-27）：**D-1 破案 —— 返回值与预留路径不同源**

### 事实 A：`CPL-RESERVE` 的 `next=` 就是 `next_trackdir`（源码复核）

`src\pathfinder\yapf\yapf_rail.cpp:1247-1258`：

```
if (!dont_reserve) {
    bool reserved = this->TryReservePath(nullptr, pNode->GetLastTile(), true);
    fprintf(dbg, "CPL-RESERVE veh=%d reserved=%d next=%d\n", ..., (int)next_trackdir);
    return reserved ? next_trackdir : INVALID_TRACKDIR;
}
```

⇒ `next=0` 就是 `next_trackdir == 0`，返回 `TrackdirToTrack(0)` = **`TRACK_X`**。

### 事实 B：`rp()` 铺的路径首步是另一个方向

`yapf_rail.cpp:1237`：`rp(st, best_td, 0);` —— **预留用的是 `best_td`，不是 `next_trackdir`**。

日志逐格读数：

| 日志行 | 原文 | 判读 |
|---|---|---|
| `:1513` | `REACH cur=15,14 td=0 depth=1 trk=0 bits=0x29` | 起点格候选一 = `TRACK_X` |
| `:1532` | `REACH cur=15,14 td=3 depth=1 trk=3 bits=0x29` | 起点格候选二 = `TRACK_LOWER` |
| `:1574` | `RP-BOOK dir veh=57 tile=15,14 td=3 trk=3 bits=0x29 resv=plain before=0x0 book=1` | **实际铺的是 `td=3`** |
| `:1577` | `RP-BOOK dir veh=57 tile=15,14 td=3 trk=3 bits=0x29 resv=plain before=0x8 book=1` | 同上，订到 bit 3 |
| `:1572` | `RP-BOOK dir veh=57 tile=15,15 td=2 trk=2 bits=0x15 before=0x0 book=1` | 下一格 `TRACK_UPPER` |
| `:1571` | `RP-BOOK dir veh=57 tile=14,15 td=0 trk=0 bits=0x9 before=0x1 book=1` | 再下一格 |
| `:1563` | `RP-BOOK plat veh=57 tile=12,15 ... resv=stn book=1` | 终点站台 |

⇒ **`best_td = 3`（`TRACK_LOWER`，南下 y=15），而 `next_trackdir = 0`（`TRACK_X`，直行 y=14）。两者不是同一个值。**

### 事实 C：`15,14` 与 `15,15` 确实连通（`bits` 互逆配对）

- `15,14`：`bits=0x29` = `TRACK_X`(bit0) | `TRACK_LOWER`(bit3) | `TRACK_RIGHT`(bit5)
- `15,15`：`bits=0x15` = `TRACK_X`(bit0) | `TRACK_UPPER`(bit2) | `TRACK_LEFT`(bit4)

`TRACK_LOWER` 与 `TRACK_UPPER` 互为反向轨道 ⇒ `15,14 --LOWER--> 15,15` 合法。**§F-5（连通）成立，§17.8 里"若不含 Y 则不可达"的担心排除。**

### 结论（REG-328-A **第六版**，已实锤到变量级）

> **`YapfTrainCoupleTrack()` 返回给上层的轨道与它自己预留的路径不是同一条。**
> 预留走 `best_td = 3`（`TRACK_LOWER` → y=15 → 车底），返回值却是 `next_trackdir = 0`（`TRACK_X` → y=14）。
> 机车忠实执行了返回值（`consist->track = 0x01 = TRACK_X`），于是它走 y=14，而 y=15 的预留无人消费，成为玩家看到的"还在但不用的预订"。

**这推翻 §17.3 的措辞**：不是"机车不按 YAPF 路径走"，而是**YAPF 交给机车的那条轨，本来就不是它预留的那条**。

- `D-1a` 成立：`next` 确实不是"预留路径首步"。
- `D-1b`（铺轨顺序反向）**否定**：`rp()` 由 `best_td` 驱动，`15,15` 在日志里先出现只是回溯顺序，不代表方向。

### 下一步（唯一，未做）

- 读 `yapf_rail.cpp:1100-1246`，找出 `next_trackdir` 的赋值语句，回答：为什么它在 `best_td = 3` 的情况下仍是 `0`？三种可能：
  - **N-1** 它在 `reach()` 里被赋成"从 origin 出发的第一个 trackdir"，而 origin 的 `vehTd=0`（`CPL-ORIGIN ... td=0 vehTd=0`）被直接沿用；
  - **N-2** 它在循环里被后到的候选覆盖（先 3 后 0，最后一次写入胜出）；
  - **N-3** 两者语义本就不同（`next_trackdir` 是"离开 origin 的方向"，`best_td` 是"进入 target 的方向"），但 `rp()` 误把它当同源。

## 17.10 第 12 组取证（2026-10-27）：**根因钉死 —— `best_td` 属于起点格 `16,14`，被当成机车所在格 `15,14` 的选轨结果**

### 铁证（`build\R3R_debug.log`）—— 先更正 §十六 的一处错误

```
1500  CPL-ORIGIN veh=57 origin=16,14 td=0 vehDir=5 vehTd=0 dontReserve=0
1510  CPL-BACK  pPrev=15,15 td=2 parent=16,14
1509  CPL-BACK  pPrev=5,15  td=0 parent=15,15
1511  CPL-TRACE st=16,14 tdb=0x101
1536  CPL-TRACE-RESULT st=16,14 best=0
1574  RP-BOOK dir veh=57 tile=15,14 td=3 trk=3 bits=0x29 resv=plain before=0x0 book=1
1572  RP-BOOK dir veh=57 tile=15,15 td=2 trk=2 bits=0x15 before=0x0 book=1
1578  CPL-RESERVE veh=57 reserved=1 next=0
1599  LOCO-MOVE veh=57 tile=15,14 j=210 spd=30 track=01 dir=5 db=1 dtile=1932
```

**更正**：`origin` 是 **`16,14`**，不是 §十六 / §17.8 里写的 `15,14`。`15,14` 是**机车实际所在格**（`LOCO-MOVE tile=15,14`）与 `CPL-ENTRY` 的入参格。两者相差一格，这一格之差就是全部问题。

### 两套几何在同一格上选了不同的轨

| 来源 | 起点 | 路径 | 在 `15,14` 上用的轨 |
|---|---|---|---|
| **A\* 节点链**（`CPL-BACK`） | `16,14` | `16,14 → 15,15 → 5,15` | **不经过 `15,14`** |
| **`reach()`/`rp()` 实际铺轨** | `16,14` | `16,14 → 15,14 → 15,15 → 14,15 → 13,15 → 12,15` | **`td=3` = `TRACK_LOWER`** |
| **返回给上层的值** | `st=16,14` | — | `TrackdirToTrack(best_td=0)` = **`TRACK_X`** |
| **机车实际行驶** | `15,14` | `15,14 → 14,14 → 13,14` | **`TRACK_X`（忠实执行了返回值）** |

`CPL-TRACE st=16,14 tdb=0x101` = trackdir `0` 与 `8` 两个出口：
- `td=0` → 走 `15,14`（长路，`reach()` 先试、返回 true、`best_td=0`、`rp()` 照它铺）；
- `td=8` → 走 `15,15`（短路，A\* 的 `CPL-BACK` 走的是这条）。

`reach()` 在 `:1068-1071` 是「按 bit 从低到高、**第一条能到车底的胜出**」⇒ 先试 `td=0` 就成功，于是 `best_td=0`、`rp()` 铺 `td=0` 那条长路。

### 结论（REG-328-A **第七版**，最终）

> **`YapfTrainCoupleTrack()` 的返回值是「YAPF 起点格 `16,14` 的出发 trackdir」，调用方却把它当「机车所在格 `15,14` 的选轨结果」。**
> 两者差一格：`16,14` 的 `trackdir 0` 在 `15,14` 上解释为 `TRACK_X`（向西到 `14,14`），而 `rp()` 按 `td=0` 铺出来的却是 `15,14` 的 `TRACK_LOWER`（向南到 `15,15`）。
> ⇒ 机车走 `y=14`，预留躺 `y=15`，`13,15`/`12,15` 的预订永远不会被消费（玩家所见）。

**三件事同时被解释**：
1. 预留还在、且位置合法 —— `rp()` 确实订成功（`book=1`）。
2. 机车在动、方向正常 —— 它忠实执行了返回值 `TRACK_X`。
3. 前几天的 exe 能挂上 —— 若当时 `origin == 机车所在格`（差零格），返回值与预留就落在同一格，不会分叉。

### 修法方向（未实施，待玩家拍板）

- **修法 A（最小）**：`YapfTrainCoupleTrack()` 返回时不要直接给 `TrackdirToTrack(best_td)`，而是用 `st` 与调用方 origin 的关系换算；或在 `:1079` 处要求 **`st == origin`**，不等时把 `st` 换成 `GetVehicleOriginTile()` / `v->tile` 后重跑 `reach()`。
- **修法 B（治根）**：`reach()` 的搜索起点与 A\* 的 `pNode` 链起点必须一致（都用 `v->tile` 或都用 `pNode->GetLastTile()`），使「选轨」「铺轨」「返回」三者同源。
- **修法 C（兜底）**：`rp()` 铺完之后，用 `rp` 实际走过的第一步覆盖 `next_trackdir`（`rp` 递归里已知 `cur_td`），使返回值恒等于预留首步。

> 修法 C 与 A/B 不冲突，且能在不改 `reach()` 语义的前提下立刻消除"返回值 ≠ 预留"这一族缺陷；建议 A+C 一起做。

### 仍待确认（下一组）

- `origin` 为什么是 `16,14` 而机车在 `15,14`：`CPL-ORIGIN` 打的是 `pNode->GetLastTile()` 还是 `v->tile`？若打的是前者，则"差一格"是 YAPF 节点的正常语义（起点节点末格=车头已跨入的格），修法 A 里就不能简单要求 `st == origin`，而要在**返回时用 origin 重算首步**。
- 读 `yapf_rail.cpp:940-980`（`consist_head_tile` / `SetReservationTarget` 上方）确认 `origin` 与 `st` 的来源差异。

## 17.11 第 13 组取证（2026-10-27）：**§17.10 的「差一格」结论作废 —— 真缺陷是「只返回首格 Track，却预留整条跨轨路径」**

### 先纠正 §17.10 的一处硬错误

`yapf_rail.cpp:817` 与 `:821-823`：

```
PBSTileInfo origin = PBSTileInfo(v->tile, v->GetVehicleTrackdir(), false);
fprintf(dbg, "CPL-ORIGIN veh=%d origin=%d,%d td=%d vehDir=%d vehTd=%d ...\n",
        ..., (int)TileX(origin.tile), (int)TileY(origin.tile), (int)origin.trackdir, ...);
```

而 `CPL-ENTRY`（`:1990-1992`）打的是 `TileX(v->tile)`。

⇒ **`CPL-ORIGIN origin=` 与 `CPL-ENTRY tile=` 打的是同一个量（`v->tile`），恒等。**
§17.10「origin=16,14 而机车在 15,14，差一格」**作废**。真相是日志里**有两次不同的寻路调用**：

| 调用 | 日志行 | `v->tile` | 内容 |
|---|---|---|---|
| 第 1 次 | `:1500` `CPL-ORIGIN origin=16,14` | **16,14** | `:1511 CPL-TRACE st=16,14` → `:1536 best=0` → `:1578 CPL-RESERVE next=0` |
| 第 2 次 | `:1640 CPL-ENTRY tile=15,14` + `:1641 CPL-ORIGIN origin=15,14` | **15,14** | 机车已西行一格，重新搜 |

⇒ 第 1 次寻路时机车确实在 `16,14`，返回值 `TRACK_X` 与 `st` **同一格、无错位**。

### 真缺陷：返回值表达不了「路径中段换轨」

第 1 次寻路的实际数据：

```
:1511  CPL-TRACE st=16,14 tdb=0x101          ← 16,14 有两个出口：trackdir 0 与 8
:1536  CPL-TRACE-RESULT st=16,14 best=0      ← reach() 取低 bit 先试，td=0 成功
:1578  CPL-RESERVE veh=57 reserved=1 next=0  ← 返回 TrackdirToTrack(0) = TRACK_X
:1570  RP-BOOK dir tile=16,14 td=0 ...
:1574  RP-BOOK dir tile=15,14 td=3 trk=3     ← ★ rp() 在 15,14 换成了 TRACK_LOWER
:1572  RP-BOOK dir tile=15,15 td=2 trk=2
:1571  RP-BOOK dir tile=14,15 td=0 trk=0
:1563  RP-BOOK plat tile=12,15 ... resv=stn
:1599  LOCO-MOVE veh=57 tile=15,14 track=01  ← ★ 机车仍走 TRACK_X
```

`rp()` 从 `16,14` 走 `td=0` 到 `15,14`，TrackFollower 在 `15,14` 给出**两个**继续方向（`td=0` 与 `td=3`，见 `:1538` / `:1557` 的同格两条 `RP cur=15,14`）：

- `td=0` → `14,14`（直行，`TRACK_X`）
- `td=3` → `15,15`（南下，`TRACK_LOWER`）

`rp()` 试 `td=0` 到不了车底，改试 `td=3` 成功 ⇒ **预留走的是「在 `15,14` 从 `TRACK_X` 换到 `TRACK_LOWER`」的路径**。

而 `YapfTrainCoupleTrack()`（`:1996-2002`）只返回

```
Trackdir ret = ...stFindNearestCoupleTrain(v, dont_reserve);
return (ret != INVALID_TRACKDIR) ? TrackdirToTrack(ret) : INVALID_TRACK;
```

⇒ **`Track` 是一个「当前格轨道类型」，不是「一条路径」。** 它丢掉了 `rp()` 已经知道的全部转折信息。

### 结论（REG-328-A **第八版，最终**）

> **`YapfTrainCoupleTrack()` 预留了一整条可跨多种 `Track` 的路径，却只把「首格轨道」`TrackdirToTrack(best_td)` 交给执行层。执行层（`ChooseTrainTrack`/`TrainController`）每格重新独立决策、从不读取 YAPF 的路径，于是路径中段一旦换轨就必然分叉。**
>
> 本次现场：返回值 `TRACK_X`（`16,14` 出发正确）→ 机车 `16,14 → 15,14 → 14,14` 直行；
> 预留路径在 `15,14` 换 `TRACK_LOWER` → `15,14 → 15,15 → 14,15 → 12,15`。
> ⇒ `15,15`/`14,15`/`13,15`/`12,15` 的预订从铺下那一刻起就**不可能被这辆车走到**（玩家所见「预订还在但没人用」）。

**这一版同时解释了三件事，且不再需要任何「差一格」「机车不听话」的假设**：

1. 预留合法且存在（`book=1`、`before=0x0`）—— `rp()` 照 `reach()` 的路径铺，路径本身确实通到车底。
2. 机车正常行驶—— 它只收到「走 `TRACK_X`」，沿 `TRACK_X` 直行是**完全正确**的执行。
3. 旧 exe 能挂上—— 那些场景整条耦合路径**全程同一种 `Track`**，返回值足以覆盖，分叉不暴露。

### 修法方向（据第八版重排）

- **修法 C（首选，改返回值语义）**：`YapfTrainCoupleTrack()` 不能只给 `Track`。要么在 `rp()` 里记录**完整 trackdir 序列**并交给执行层（`ChooseTrainTrack` 按序列选），要么至少保证「返回值与 `rp()` 首步逐字一致」——现在 `best_td` 与 `rp()` 首步确实一致，**但这一步一致不解决问题**，因为路径在第 2 格就换轨了。
- **修法 D（最小可行）**：**不要在 `15,14` 这类换轨格上让 `rp()` 继续铺**，而是要求 `reach()`/`rp()` 只沿**与 `best_td` 同 `Track` 的连通段**搜索；跨 `Track` 时放弃并返回 `INVALID_TRACK`（让上层等下一格再搜）。代价是耦合路径变短、可能多次重搜；收益是「预留 ⊂ 返回值可表达的集合」，永不再产生孤儿预订。
- **修法 E（治根）**：couple 寻路改成「到车底为止的强制路径」而非「建议首格」，即把整条 trackdir 序列存进 `PBSTileInfo` 或 `Train` 的临时字段，让 `ChooseTrainTrack` 优先消费。
- **修法 A/B（§17.10 遗留）已作废**：origin 与 st 同格一致，无需重跑 `reach()`。

### 下一步（唯一，未做）

- 读 `DoTrainCouplePathfind()` 的**调用点与返回值消费**（`train_cmd.cpp`）：确认上层拿到 `Track` 后是否真的只用于「当前格选轨」，以及是否存在「路径续段」的既有字段（若有，修法 C 就有现成载体）。

## 17.12 第 14 组取证（2026-10-27）：**消费点实锤 —— 返回值只被赋给 `best_track`，整条路径无任何字段承载**

### 唯一消费点：`src\train_cmd.cpp:17386-17394`（`ChooseTrainTrack` 内）

```cpp
/* R3R: always reserve the couple path - the loco needs a reservation
 * to move, and it must extend right up to the consist. ... */
Track path_found = DoTrainCouplePathfind(consist, true);
/* R3R: the couple pathfinder starts from the loco itself and always
 * returns a valid departure direction, so use it regardless of where
 * ExtendTrainReservation ended up ... */
if (path_found != INVALID_TRACK) {
    best_track = path_found;      // ← 唯一用途：当前格选哪根轨
}
```

⇒ **`DoTrainCouplePathfind()` 的返回值只写进 `best_track`（一个 `Track`），随后作为 `ChooseTrainTrackResult.track` 返回给 `TrainController`。整条 YAPF 路径没有进入任何字段（`PBSTileInfo dest` 用的是 `ExtendTrainReservation` 的结果，不是 couple 寻路的结果）。**

注释本身写明设计意图是 **"a valid departure direction"（出发方向）**，而 `rp()`（`:1082-1236`）却按**整条路径**逐格 `TryReserveRailTrackdir`。**两侧语义从一开始就不匹配。**

### 最后一环：`15,14` 的 `TRACK_X` 从未被订

`build\R3R_debug.log` 中 `tile=15,14` 的全部 `RP-BOOK` 只有两条：

```
:1574  RP-BOOK dir veh=57 tile=15,14 td=3 trk=3 bits=0x29 resv=plain before=0x0 book=1
:1577  RP-BOOK dir veh=57 tile=15,14 td=3 trk=3 bits=0x29 resv=plain before=0x8 book=1
```

两条都是 `td=3` = `TRACK_LOWER`。而 `:1538` / `:1557` 的 `RP cur=15,14 td=0 depth=1` 只是**递归帧入口探针**（`rp()` 在判据之前无条件打，见 `:1085-1088`），那次 `td=0` 分支最终**没到 `reserve_dir_if_present`**。

⇒ **`15,14` 的 `TRACK_X` 没有预留**；`16,14` 的 `TRACK_X` 有（`:1570 RP-BOOK ... tile=16,14 td=0`）。

### 结论（REG-328-A **第九版，终局**）

> 1. `reach()` 找到一条 `16,14 →(X) 15,14 →(LOWER) 15,15 →(UPPER) 14,15 →(X) 12,15` 的路，`best_td = 0`。
> 2. `rp()` 按这条路逐格订：`16,14` 的 `TRACK_X`、`15,14` 的 `TRACK_LOWER`、`15,15` 的 `TRACK_UPPER`、`14,15` 的 `TRACK_X`、`12,15` 站台整格。
> 3. 返回值 `TrackdirToTrack(0) = TRACK_X` → `best_track`（唯一消费点）。
> 4. 机车照 `best_track` 走：`16,14 →(TRACK_X，已订) 15,14`。
> 5. 进入 `15,14` 后，**该格的 `TRACK_X` 没有预留**，YAPF 铺的是同格的 `TRACK_LOWER` ⇒ 机车在这个岔口按普通逻辑继续 `TRACK_X` → `14,14`（该格亦无预留）。
> 6. `15,14`(LOWER) / `15,15` / `14,15` / `12,15` 的预订从此**不可能被任何车走到** ⇒ 玩家看到的「预订还在、位置也合理、就是没人走」。

**根因一句话**：**「预留按整条路径铺，执行只按首格轨道」——`rp()` 与 `YapfTrainCoupleTrack()` 的返回值粒度不一致，路径一旦在中段换 `Track` 就必然分叉。**

### §17.2 定性表的第 ⑧ 次修正

| 序 | 定性 | 被什么推翻 |
|---|---|---|
| ⑦ | 订了又被**清除** | 玩家第七次澄清（F-7） |
| ⑧ | 返回值的起点格与机车所在格**差一格**（§17.10） | `yapf_rail.cpp:817/821` 证 `origin.tile == v->tile`，两次寻路是不同 tick（§17.11） |
| ⑨ | **返回值粒度 = 单格 `Track`，预留粒度 = 整条跨轨路径**（当前有效） | — |

### 修法（按第九版定稿）

| 编号 | 内容 | 代价 | 评价 |
|---|---|---|---|
| **D** | `reach()`/`rp()` 只沿**与 `best_td` 同 `Track`** 的连通段搜索；需换 `Track` 就返回 `INVALID_TRACK`，让上层下一格再搜 | 耦合路径变短、可能多次重搜 | **最小且安全**：保证「预留 ⊂ 返回值可表达」，不再产生孤儿预订 |
| **C** | `rp()` 记录完整 trackdir 序列，`ChooseTrainTrack` 按序列逐格选轨 | 需新增载体（`Train` 临时字段或扩 `ChooseTrainTrackResult`） | 治根，但动执行层，回归面大 |
| **E** | 把 couple 路径并入 PBS 机制（写 `dest` + 让 `FollowTrainReservation` 推进），废弃手工 `rp()` | 改动最大 | 长期正解 |
| ~~A/B~~ | 用 `v->tile` 重跑 `reach()` / 统一 `st` 与 `origin` | — | **作废**：两者本就同格 |

### 仍待确认（下一组）

- `15,14` 的 `TRACK_X` 无预留却被机车驶入，是否触发过 `TPR-BLOCKED`/`stuck`？日志尾部 `:1642/:1643` 的 `CPL-SCAN why=none` 与 `CPL-PATHFOUND found=0` 是否就是「第二次寻路时车已站在未预留格上」的后果 —— 这决定修法 D 需不需要同时处理「已驶入无预留格」的善后。

## 17.13 第 15 组取证（2026-10-27）：**驶入无预留格不触发阻塞、不 mark_stuck；该格预留是 controller 后补的**

### 读数（`build\R3R_debug.log`）

| 行 | 原文（截断） | 判读 |
|---|---|---|
| 1599 | `LOCO-MOVE veh=57 tile=15,14 j=210 spd=30 track=01 dir=5 db=1 stuck=0 dtile=1932` | 机车在 `15,14`，`track=0x01`＝**TRACK_X**，`spd=30` 在动 |
| 1636 | `LOCO-MOVE veh=57 tile=15,14 j=206 spd=30 track=01 dir=5 db=1 stuck=0` | 仍在 `15,14`，仍走 TRACK_X（未转向 LOWER） |
| 1639 | `RESV-EXT-PRE veh=57 tile=14,14 enterdir=0 tracks=0x11 reserved=0x0 doRes=0 real=1 spd=30` | **`14,14` 此时 `reserved=0x0`（无预留）**，机车照常要进 |
| 1644 | `RESV-WATCH SET-RAIL tile=14,14 track=0 phase=controller actor=59 head=57` | 该格的 `TRACK_X` 是 **controller 阶段、actor=59（同一链上第 3 节）、head=57** 后补订的，`phase` 不是 `yapf-couple` ⇒ **不是 couple 寻路铺的** |
| 1649 | `LOCO-MOVE veh=57 tile=14,14 j=196 spd=30 track=01 dir=5 db=1 stuck=0` | 已进入 `14,14` 并继续 TRACK_X |

### 全文检索（`TPR-BLOCKED` / `stuck=1` / `RESV-WATCH SET-RAIL tile=1[34],14`）

```
:1285 TPR-BLOCKED veh=54 tile=12,15 spd=2 ... other=0 otherTile=12,15 ...
:1287 TRP veh=54 ... mstuck=1 fto=1 => ok=0 res=0
:1288 DEPOT-ARR veh=54 ... stuck=1 ...
:1303 SPD-DROP veh=54 ... stuck=1 ...
:1304 STUCK veh=54 tile=12,15 state=1 stuck=1 ...
:1644 RESV-WATCH SET-RAIL tile=14,14 track=0 phase=controller actor=59 head=57
:391  RESV-WATCH SET-RAIL tile=13,14 track=0 phase=choose-track actor=57 head=57
```

⇒ **`veh=57` 全日志 0 条 `TPR-BLOCKED`、0 条 `stuck=1`**；唯二的 `TPR-BLOCKED`/`stuck` 都属于 `veh=54`（tick 1285~1304，解挂后）。
⇒ `13,14`（`:391`）与 `14,14`（`:1644`）的 `TRACK_X` 都被 `controller` / `choose-track` 阶段订上了，**唯独 `15,14` 的 `TRACK_X` 没有任何一条 `SET-RAIL`**（与 §17.12「`15,14` 只有 `td=3` 两条 RP-BOOK」互证）。

### 结论（新增，不改动第九版主因）

1. **修法 D 不需要额外善后「已驶入无预留格」**：`spd=30` 已出发的车不会被 PBS 拦下，`stuck` 只在 `ChooseTrainTrack` 返回失败时置位（`:1285` 的 `veh=54` 是 `spd=2` 起步阶段才被拦），所以「车开在没预留的轨上」本身不产生卡死、不产生 `TPR-BLOCKED`。
2. `14,14` 之所以后来有预留，是 `controller`（`ReserveTrackUnderConsist` 一族，`actor=59` 是链内车节）补的 ⇒ **孤儿预订（`15,14` LOWER / `15,15` UPPER / `14,15` X）与「车走过的地方都有预留」这个表面现象并存**，正是玩家看到「预订还在、位置合理、就是没人走」的完整解释。
3. `:1642/:1643 found=0`（`closed=5 nodes=18 enter=13`) 的原因**仍未定性**，下一组查 `CPL-SCAN` 的 `noplat/nores/noconsist/why` 语义，重点验证假设 **H-15a：第二次寻路从 `15,14` 出发，而 `15,14` 的 `TRACK_X`（= `origin.td` 对应的轨）无预留 ⇒ YAPF 视起点为不可出发 ⇒ `found=0`**。

## 17.14 第 16 组取证（2026-10-27）：**`found=0` 的性质 = 搜索只闭合 5 个节点就穷尽，`consist=0`（车底从未被扫到）**

### `CPL-SCAN` 各字段定义（`src\pathfinder\yapf\yapf_destrail.hpp`）

| 字段 | 定义（行号） | 语义 |
|---|---|---|
| `enter` | `:35` | `PfDetectDestination` 调用次数（**节点级**，含 `PfCalcEstimate` 的重复访问） |
| `nodes` | `:78` 打印，`= closed + enter` | 搜索规模 |
| `noplat` | `:36` | 早退：**不是站台/车库格** |
| `nores` | `:37`、`:493-496` | 早退：**非站台格**且该格无本车预留位 |
| `noconsist` | `:38`、`:537-541` | 早退：格合格但**格上扫不出合法车底** |
| `consist` | `:39`、`:543` | 扫到车底、进入三道闸门 |
| `why` | `:45`、`:71-73` | 最近一次走到闸门的结果；`-1` 打 `none` |

### 对 `:1643 CPL-SCAN tick=12627 veh=57 found=0 closed=5 limit=10000 nodes=18 enter=13 noplat=1 nores=6 noconsist=6 consist=0 rej=0/0/0/0 acc=0/0 accwp=0 last=-1,-1 lastveh=-1 why=none` 的判读

- `closed=5 < limit=10000` ⇒ **不是被节点上限截断，是真的穷尽了可达空间**（文件头 `:63-66` 明确写了这条判读口径）。
- `consist=0` 且 `lastveh=-1`、`why=none` ⇒ **本次搜索一次都没扫到任何车底**，`12,15` 上的 veh=0 根本没被探到。
- 分布：`enter=13` = `noplat(1) + nores(6) + noconsist(6) + consist(0)` ⇒ 13 次判定全部落在「非站台无预留格(6) / 站台格无车底(6) / 非站台(1)」，一个车底都没碰。
- ⇒ **`found=0` 不是「配对被拒」「分组被拒」「目的地被拒」**（`rej=0/0/0/0`），而是**搜索在 `15,14` 附近就已经走不出去**（只闭合 5 个节点）。

### 结论（H-15a 修正为 H-15b，仍待一组确认）

- H-15a「起点格无预留 ⇒ YAPF 直接拒」**方向正确但表述不准**：YAPF 并未把起点自身判死（它是从 `origin` 无条件展开的），而是**从 `15,14` 只能展开极少的后继**（`closed=5`），且这些后继全是「无预留的普通轨」⇒ 无法继续延伸 PBS 路径 ⇒ 到不了站台格上的车底。
- H-15b（待验）：**`15,14` 的 `TRACK_X` 没有预留，导致机车从这一刻起行驶在「无预留轨」上，YAPF 的 PBS 展开在下一步就失去合法性，couple 寻路从此恒 `found=0`。** 这与 §17.13「`14,14` 的预留是 controller / `ReserveTrackUnderConsist` 后补的」并不矛盾——那是**车节保护**（整链脚下格），不是**路径预留**，两者是不同的位（`RESV-WATCH ... phase=controller` vs `phase=yapf-couple`）。

## 17.15 第 17 组取证（2026-10-27）：**日志尾部 = 车其实在走、`bestDist` 4→3→2 递减、且一路在「无预留轨」上**

### 尾部读数（`:1678-1698`，日志到此结束）

| 行 | 原文（截断） |
|---|---|
| 1680 | `CPL-PAIRSCAN tick=12692 act=57 ... actTile=14,14 actSpd=30 ... best=0 bestDist=3 ...` |
| 1682 | `LOCO-MOVE veh=57 tile=14,14 j=204 spd=30 track=01 dir=5 db=1 stuck=0 dtile=1932` |
| 1694 | `RESV-EXT-PRE veh=57 tile=13,14 enterdir=0 tracks=0x1 reserved=0x0 doRes=0 real=1 spd=30` |
| 1697 | `CPL-PAIRSCAN tick=12736 act=57 ... actTile=13,14 actSpd=30 ... best=0 bestDist=2 ...` |
| 1698 | `LOCO-MOVE veh=57 tile=13,14 j=204 spd=30 track=01 dir=5 db=1 stuck=0 dtile=1932` |

⇒ **`15,14 → 14,14 → 13,14`、`spd=30` 恒定、`stuck=0`、`bestDist` 4→3→2 递减**：机车在**正常行驶且确实在接近配对目标**（veh=0 @ `12,15`），它走的每一格 `reserved=0x0`（`13,14` 也无预留）⇒ H-15b 的「一路驾驶在无预留轨上」得到直接证实。

### 完整机制链（REG-328-A 第十版，链条闭合）

1. `rp()` 按 `best_td` 的回溯路径**逐格换轨**预留：`16,14`(X) / `15,14`(LOWER) / `15,15`(UPPER) / `14,15`(X) / `12,15`(站台)。
2. 返回值只有一个 `Track = TrackdirToTrack(best_td) = X`（`yapf_rail.cpp:1990-2002`），唯一消费点 `train_cmd.cpp:17386-17394` → `best_track`。
3. 执行层只按 `best_track` 选当前格轨道 ⇒ 机车在 `15,14` 走 X（`14,14` 方向），**不是**预留的 LOWER。
4. ⇒ `15,14`(LOWER) / `15,15` / `14,15` / `12,15` 的预订**永久无人消费**（孤儿，玩家看到的「预订还在、位置合理、就是没人走」）。
5. ⇒ 机车自 `15,14` 起**每一步都在无预留轨上**（`:1694` `reserved=0x0`），`ExtendTrainReservation` 恒 `doRes=0`，couple 寻路 `found=0`（`:1642/:1643`，`consist=0`）。
6. 一旦它接近站台、需要在岔口/入站格取得预留而 `ChooseTrainTrack` 失败，就会 `spd=0` 并落入「停驶分支不再寻路」（KI-348 的结构性路径）⇒ **永久不动**，同时旧预订仍挂在轨道上。玩家现象完全解释。

### 修法 D 的精确形态（据本组读数补全，待拍板）

`rp()` 只沿 **与 `best_td` 同 `Track`** 的前缀推进并预留，遇到需要换 `Track` 的下一格就**停止**（该格不订、递归不再深入）；`YapfTrainCoupleTrack()` 仍返回 `TrackdirToTrack(best_td)`。
- 现场效果：本次预留 = `16,14`(X) + `15,14`(X)，`best_track = X` ⇒ 机车进入 `15,14` 时该格**已预留**；下一 tick 从 `15,14` 重新寻路也能继续展开，逐步推进到 `12,15`，不再产生孤儿预订、不再驶入无预留轨。
- 必须在截断处用**进入该格的 track**（`reserve_if_present(f3.new_tile, TrackdirToTrack(nd))`，即 KI-346 已修的那句）把前缀末格订上，否则车进不去。

### 疑点（按「有疑点立刻落盘」规则登记）

- **D-328-17a**：为什么第一次从 `16,14` 搜索时 `15,14`/`15,15`/`14,15` 都**尚未预留**却能找到路径（`found=1`、`closed` 远大于 5），而第二次从 `15,14` 出发只闭合 5 个节点就穷尽（`found=0`）？若 YAPF 在 PBS 模式下要求进入格已预留，第一次不可能成功；若允许未预留，第二次也不该失败。中间必有一个额外差异（`12,15` 此刻 **已被 `RESERVECONSIST` 以 `veh=0` 身份占位 `resNow=1`**，而第一次搜索时该位尚未建立？或有信号/道岔状态差异）。**待验**：取第一次成功那次的 `CPL-SCAN` 行（`:1520` 附近）对比 `noplat/nores/noconsist/consist`，即可判定差异在「车底格预留位」还是「起点格预留」。
- 该疑点**不影响修法 D 的必要性**：无论哪一侧，D 都消除「预留路径 ≠ 执行路径」这条已被证明可致死的链路。

## 17.16 第 18 组取证（2026-10-27）：**`rp()` 订的是「进入轨」，`best_track` 给的是「出去轨」——这是「以前也拐弯却没事」的答案**

### 读数（`build\R3R_debug.log`，本组新增）

| 行 | 原文（截断） | 判读 |
|---|---|---|
| 1499/1505 | `CPL-ENTRY veh=57 tile=16,14` / `CPL-PATHFOUND veh=57 found=1` | 第一次寻路起点 `16,14`，成功 |
| 1536-1546 | `RP cur=16,14 td=0 depth=0` → `15,14 td=0` → `14,14 td=0` → `13,14 td=0` → `5,14` → `4,14` → `3,14` → `2,14 td=3` → `2,15 td=2` → `1,15` | 搜索树里**确实存在一条「X 直行」前缀**（`16,14→15,14→14,14→13,14`，td 全为 0） |
| 1547-1556 | `14,14 td=12` → `14,13 td=13` → … → `1,15` | 另一条支路（斜向），同样走到 1,15 |
| 1557-1560 | `15,14 td=3` → `15,15 td=2` → `14,15 td=0` → `13,15 td=0` | 第三条支路，**这一条才是真正订到站台的**（见下行） |
| 1561/1563-1569 | `RP-BOOK dir 13,15 td=0 book=1`、`RP-BOOK plat 12,15 … 6,15 book=1` | 只有到站台的那条支路被订 |
| 1570-1577 | `RP-BOOK 14,15 td=0`、`13,15 td=0`、`15,15 td=2`、`15,14 td=3`、`16,14 td=0` | 回溯逐层订，**`15,14` 订的是 `td=3`(LOWER)** |
| 1578 | `CPL-RESERVE veh=57 reserved=1 next=0` | 返回值 `next_trackdir=0`（X） |

- 关键：`RP-BOOK` 里**从来没有 `14,14` / `13,14`**（那两条支路走到 `1,15` 后返回 false，不订），也**从来没有 `15,14 td=0`**。
- ⇒ `rp()` 的语义是「**沿成功路径、在每格订下我进来时走的那根轨**」；`best_track` 的语义是「**起点格该往外走的那根轨**」。**在直行格二者同轨，在换轨拐弯格二者不同轨。**
- ⇒ 本次 `16,14`(X) 进 `15,14`，成功路径却在 `15,14` 转向 LOWER ⇒ `rp()` 只订 LOWER，而执行层在 `16,14` 拿到 X 直行入 `15,14`（该格 X 未订）。

### 对「以前也要拐弯，为什么没问题」的结论

**拐弯本身从来不是问题；问题是「拐弯格上，进入轨 ≠ 出去轨」这件事是否被 `rp()` 覆盖到。**

- 每跨一格 `ChooseTrainTrack` 都会**重新跑一次 YAPF**（origin 变成新格，`best_td` 重算），所以执行层并不需要「整条路径序列」——它只需要**当前格能再算出一次成功路径**。
- 旧场景里，要么整条路径同轨直行（拐弯发生在相邻格、本格进入轨=出去轨），要么距离短（`veh=54` 那次 `COUPLE-OK` 前后 `dist=0`，预留一格格数极少），于是第二格重新寻路仍然 `found=1`，`best_track` 每格都对，看不出问题。
- 本次的致命点**不是**「拐弯走错」而是「拐到 `15,14` 之后**下一次寻路 `found=0`**（`:1642/:1643`）」，`best_track` 从此不再刷新，只能沿用上次的 X 直行 ⇒ 才一路开到 `14,14`、`13,14`。
- 而 `found=0` 的直接原因是 `nores`：`:1643 enter=13 = noplat(1)+nores(6)+noconsist(6)`，从 `15,14` 沿 X 出去的第一格 `14,14` 此时 `reserved=0x0`（`:1639 RESV-EXT-PRE … reserved=0x0 doRes=0`）⇒ 该格被判「无本车预留位」⇒ 展开即止，`closed=5` 穷尽、`consist=0`（一次都没扫到车底）。

### 修法 D 的形态据此再校正（重要）

D 不是「沿 `best_td` 的路径截断」——那条路径在 `15,14` 就转 LOWER 了，截断只会订 `16,14` 一格，`15,14` 仍然没订。正确形态是：

> **`rp()` 沿「td 保持不变」的同轨直行前缀推进**（正是 `RP cur=1537-1541` 那条 `16,14→15,14→14,14→13,14`），逐格按该轨订下去，遇到必须换 trackdir 的格就停；`best_track` 仍取 `TrackdirToTrack(best_td)`。

这样订出的格子 = 执行层真正会走的格子（同一根轨、直行），「进入轨=出去轨」天然成立，`14,14` 也会被订上 ⇒ 下一格重搜不再 `nores`。

### 疑点（新增）

- **D-328-17b**：日志里**没有 `veh=54` 那次的 `CPL-*` / `RP-BOOK`**（`CPL-ORIGIN` 全文仅 veh=57 两条，`COUPLE-OK loco=54` 在 `:452`），无法逐字对比「以前拐弯那次」的 `best_td` 与订轨。上述「以前没事」的解释是由机制推出的，**尚未用旧日志实证**。若玩家能提供含成功挂接的旧日志（带 `CPL-*` 且未被 `R3RDbgEdge` 抑制），可一眼定论。
- 另注：`RP cur=1547-1556` 那条斜向支路说明 `rp()` 会遍历搜索树的多个分支再回溯订轨，**「订的格」可能多于「唯一路径」**；这也是 D 需要「只订直行前缀」而非「订所有分支」的理由。

## 18. 玩家拍板（2026-10-27）：走修法 E，按 E1→E4 循序渐进，本轮先落地 E1（=D）

玩家原话：「我是要按照修法 E 准备对挂接寻路进行改为正常 PBS 逻辑的，不过就按照你推荐的循序渐进吧」。

### 18.1 已确认的路线与顺序（本次拍板，不再讨论）

| 步 | 内容 | 本轮 |
|---|---|---|
| **E1 = D** | `rp()` 只订「td 不变的同轨直行前缀」，遇换 trackdir 即停；返回值 `best_track` 不变 | **本轮落地** |
| E2 | `rp()` 的订轨动作改调 PBS 的 `ExtendTrainReservation`，目标判定仍留 couple 自己 | 待 E1 实机验证后 |
| E3 | 目标格被占特例 + 停稳后预留保持/续期 | 待 E2 后 |
| E4 | 删 couple 专用 YAPF 分支与 `rp()`，完全走 PBS | 最后 |

### 18.2 E1 的正确性前提（必须先确认，否则 D 不成立）

D 之所以成立，依赖于「第二格重跑 YAPF 时，只要**目标格**仍有预留就能 found=1」，即 `nores` 判据是**目标格**判据、不是**路径中间格**判据。若 `nores` 要求路径上每一格都有本车预留，则「只订前缀」必然导致第二格再次 `nores`，D 无效，必须直接在 E2 上做。

→ 本节结论待 §18.3 用代码原文确认后回填（`yapf_destrail.hpp:485-500`）。

### 18.3 前提确认（代码原文）：`nores` 不是路径中间格判据，但它否掉了 §17.16 的 D 形态

`yapf_destrail.hpp:491-496` 原文：

```
TrackdirBits tdb = TrackdirToTrackdirBits(td);
bool has_res = HasReservedTracks(tile, TrackdirBitsToTrackBits(tdb));
if (!has_res && !IsRailStationTile(tile)) { ++_r3r_cpl_scan_nores; return false; }
```

- `tile` 是**被探测的目标格**（该函数是目标格过滤器），故 §18.2 的前提成立：中间格无预留不会导致 `nores`。
- 但 `:493` 带 `!IsRailStationTile(tile)` 豁免 ⇒ **站台格永远不会 `nores`**；`nores` 只可能由**车库格**产生。而现场 `:1643 enter=13 = noplat(1)+nores(6)+noconsist(6)`、目标车底在站台 `5,15` ⇒ **`found=0` 的主因不是 `nores`，是 `noconsist`（有预留却找不到车底）**。§17.16 把 `14,14 reserved=0x0` 当成 found=0 的原因，**该推断作废**。

### 18.4 §17.16 的「只订同轨直行前缀」形态作废，正确形态 = 「拐弯格补订进入轨」

重新读 `rp()` 原文（`yapf_rail.cpp:1182-1234`）后确认：`rp()` 回收时订的两处**都是「出去轨」**——

| 代码 | 订的是 | 语义 |
|---|---|---|
| `:1188` / `:1214` `reserve_dir_if_present(cur, cur_td)` | `cur` 格的 `TrackdirToTrack(cur_td)` | 从 `cur` **出去**走哪根轨 |
| `:1204` / `:1230` `reserve_dir_if_present(f3.new_tile, nd)` | `new_tile` 格的 `TrackdirToTrack(nd)`，`nd ∈ f3.new_td_bits` | 从 `new_tile` **出去**走哪根轨 |

⇒ **「订出去轨」本身是对的**（PBS 语义就是"我要从这格沿这根轨出去"）。缺的是**另一根**：车从 `16,14` 沿 X 驶入 `15,14` 时，占的是 `15,14` 的 **X 轨**；而 `rp()` 只给 `15,14` 订了 **LOWER**（`:1574 RP-BOOK 15,14 td=3`）。于是车到达 `15,14` 后重跑 YAPF，起点 trackdir 是 X、而 X 无预留 ⇒ 展开立即枯竭（`enter=13`）⇒ `found=0`；此后 `best_track` 不再刷新，沿用 X 直行开出预留区。

**这也解释了"以前直行没事"**：直行格「进入轨 ≡ 出去轨」，订一根即覆盖两根；**拐弯格必须订两根**，而现在只订一根。

⇒ **E1 正确形态**：在 `:1204` / `:1230` 处，除 `nd`（出去轨）外**补订 `TrackdirToTrack(f3.new_td)`（进入轨）**；两者同轨时去重（同轨只订一次，避免多余 `TryReserveRailTrackdir` 调用）。`cur` 格自身的进入轨由上一层负责，首格 `st` 由 `ReserveTrackUnderConsist` 保护，均不需新增。

此形态比「只订直行前缀」更小、更准：**不改订什么格，只补一根轨**。

→ 待确认：`TrackFollower` 是否暴露「进入 new_tile 的 trackdir」成员（`new_td`）。

### 18.5 进入轨的算法（`follow_track.hpp` 无 `new_td`，用 `exitdir` 反推）与 E1 最终形态

`follow_track.hpp:41-46` 只有 `old_td / new_tile / new_td_bits / exitdir`，**没有**「进入 new_tile 的 trackdir」成员。但可反推：

- `Follow(cur, cur_td)` 内部先算 `exitdir = TrackdirToExitdir(cur_td)`（离开 `cur` 的方向），再把 `new_td_bits &= DiagdirReachesTrackdirs(exitdir)`（`:147`）——即 new_tile 上**沿 exitdir 继续**的 trackdir 集合。
- ⇒ 进入 new_tile 的 trackdir = `TrackBitsToTrackdirBits(TrackToTrackBits(TrackdirToTrack(cur_td))) & DiagdirReachesTrackdirs(f3.exitdir)` 取首个。
  - 同槽直连时它就是「cur 那根轨在 new_tile 上的同向 trackdir」；
  - 换槽时该轨在 new_tile 上不存在 ⇒ 交集为空 ⇒ **不订**（`reserve_dir_if_present` 另有 `bits & TrackToTrackBits(...)` 校验，双重保险）。

**E1 落地形态（最终）**：在 `yapf_rail.cpp:1203-1205` 与 `:1229-1231` 两个 `reserve_dir_if_present(f3.new_tile, nd)` 之后，补订上述进入轨；**两者同轨时跳过**（`TrackdirToTrack(entry_td) != TrackdirToTrack(nd)`）⇒ 直行格行为与改动前**逐字相同**，只有拐弯格多订一根。

**为什么这能修好 KI-504**（机制闭环）：
1. 现在 `15,14` 只订了 LOWER（`:1574`），X 没订；
2. 车从 `16,14` 沿 X 驶入 `15,14`，下一 tick 在 `15,14` 重跑 `ChooseTrainTrack`：起点在 X 轨、X 无预留，`MaskReservedTracks` 又把本车已预留的 LOWER 剔掉（`follow_track.hpp:195-201`）⇒ 可展开方向枯竭，`enter=13`、`found=0`；
3. `best_track` 停止刷新，沿用上次的 X ⇒ 车继续直行开出 `14,14`、`13,14`，与「预留串永远无人消费」逐字吻合；
4. E1 之后 `15,14` 同时有 X（进入）+ LOWER（出去），起点可展开、路径续算成功 ⇒ `found=1`，`best_track` 逐格刷新。

**同时否掉的三条旧假设**（避免后续重复取证）：
- ~~「D 应只订 td 不变的同轨直行前缀」~~ → 作废（那会把车引到 `2,14` 更远）；
- ~~「`14,14 reserved=0x0` 导致 `nores` 才 found=0」~~ → 作废（`nores` 只对车库格生效）；
- ~~「预留粒度 ≠ 返回值粒度」（KI-504 原表述）~~ → 表述不准确：`rp()` 订的**出去轨**与 `best_track` 同为"出去"语义、是自洽的；真正缺的是**拐弯格的进入轨**。

### 18.6 构建自证（第 329 轮 E1）

复用既有 `_tmp_inc_build.cmd`（未新建任何 `.cmd`）。注意：**`cd /d ... && _tmp_inc_build.cmd` 直接调用会返回 `exitCode=0` 但脚本根本不执行、无任何输出**（`build\R3R_incbuild.done` 的 mtime 仍停在上一次 2026-10-26 23:46:43）——必须用 `Start-Process -FilePath 'cmd.exe' -ArgumentList '/c','_tmp_inc_build.cmd' -WorkingDirectory 'd:\sourcecode of JGRPP' -WindowStyle Minimized -Wait`。

| 判据 | 读数 |
|---|---|
| `build\R3R_incbuild.log` | `[2/3] Building CXX object ...yapf_rail.cpp.obj` → `[3/3] Linking CXX executable openttd.exe` |
| `build\R3R_incbuild.done` | `EXIT_CODE=0`（2026-10-27 13:40:59） |
| obj | `build\CMakeFiles\openttd_lib.dir\src\pathfinder\yapf\yapf_rail.cpp.obj` @13:39:05，晚于源码 @13:37:09 |
| exe | `build\openttd.exe` @2026-10-27 13:40:52（52 317 696 B），晚于全部源码 |
| `read_lints` | 0 条 |

仅改 `src\pathfinder\yapf\yapf_rail.cpp` 一个 `.cpp`，未碰任何 `src\*.h`、未改语言串 ⇒ 增量合法、无需全量。

### 18.7 复测判据（5 条）

1. **正判据**：`RP-BOOK dir veh=57 tile=15,14 td=*` 出现**两根**（`trk=1` 的 X + `trk=8` 的 LOWER），即拐弯格进入轨被订。
2. **主判据**：机车拐进 `15,14` 之后**不再 `found=0`** —— 应当出现 `CPL-PATHFOUND found=1` 且 `best_track` 继续刷新；`COUPLE-OK loco=57` 出现。
3. **负判据（直行不变）**：全直行场景的 `RP-BOOK` 行数与 `trk` 值与改动前**逐字相同**（同轨跳过逻辑生效，不多订）。
4. **负判据（无新孤儿）**：不出现新的 `RESV-AUDIT-STRAY` / `RESV-REAP`（补订只订该格真实存在的轨，`reserve_dir_if_present` 自带校验）。
5. **回归**：其它挂接场景（车库内、路点等待、双头）挂接成功率不下降。

若第 2 条仍 `found=0`，则说明「进入轨」不是唯一缺口，直接进 E2（把 `rp()` 的订轨动作换 PBS `ExtendTrainReservation`）。

---

# 十九、第 329 轮实机复测：E1 **已生效但无效**，根因第二次修正 = 「自己订的路把自己挡住了」

日志 `D:\temp\codebuddy-dropped-files\0752004c-8c1e-43c9-9459-697775b4a82d\R3R_debug.log`（231 914 B，2026-10-27 13:55:14），产生者 `build\openttd.exe`（第 329 轮 E1，13:40:52）⇒ **采样有效，就是 E1 版**。

## 19.1 判据 1「拐弯格补订进入轨」= 通过（E1 代码确实执行）

```
:1574 RP-BOOK dir veh=57 tile=15,14 td=3 trk=3 bits=0x29 resv=plain before=0x0 book=1
:1577 RP-BOOK dir veh=57 tile=15,14 td=3 trk=3 bits=0x29 resv=plain before=0x8 book=1
:1578 RP-BOOK dir veh=57 tile=15,14 td=0 trk=0 bits=0x29 resv=plain before=0x8 book=1 SPLIT
```

`:1578` 就是我新加的进入轨补订（`trk=0` = X，`before=0x8` = 该格原已有 LOWER，`SPLIT` = 两根不同轨并存的既有日志分支）。⇒ `15,14` 现在**同时持有 X + LOWER**，E1 行为与设计一致。

## 19.2 判据 2「不再 found=0」= **不通过**

```
:1640 RESV-EXT-PRE veh=57 tile=14,14 enterdir=0 tracks=0x11 reserved=0x0 doRes=0
:1641 CPL-ENTRY veh=57 tile=15,14 orderType=16 dontReserve=0
:1642 CPL-ORIGIN veh=57 origin=15,14 td=0 vehDir=5 vehTd=0 dontReserve=0
:1643 CPL-PATHFOUND veh=57 found=0
:1644 CPL-SCAN tick=12627 veh=57 found=0 closed=5 limit=10000 nodes=18 enter=13 noplat=1 nores=6 noconsist=6 consist=0 ...
```

与 E1 之前**逐字同形**（`enter=13 / noplat=1 / nores=6 / noconsist=6`）。车随后 `:1632 LOCO-MOVE veh=57 tile=15,14 track=01 spd=30` → `:1649 CPL-PAIRSCAN actTile=14,14 actSpd=30` → 日志尾 `LOCO-MOVE veh=57 tile=14,14 spd=30`，**照旧沿 X 直行开出预留区**。⇒ 「进入轨缺失」是**真缺陷但不是 found=0 的原因**，§18.5 的机制闭环**证伪**。

## 19.3 根因第二次修正：`CanEnterNewTile` **不豁免本车自己的预留**

`follow_track.hpp:195-201` 原文：

```
TrackBits reserved = GetReservedTrackbits(this->new_tile);
this->new_td_bits &= ~TrackBitsToTrackdirBits(reserved);      // 已预留的轨整根剔除
for (Track t : SetTrackBitIterator(TrackdirBitsToTrackBits(this->new_td_bits))) {
    if (TracksOverlap(reserved | TrackToTrackBits(t))) this->new_td_bits &= ~TrackToTrackdirBits(t);
}
```

**没有任何 `IsReservationFor(tile, td, this->veh)` 豁免**——预留轨对**所有人**（含车主自己）都不可通行。

于是 `rp()` 预先订下的整条路径，在下一 tick 变成**自己的路障**：

| 车在 `15,14` 的可选方向 | 下一格 | 该格预留 | 结果 |
|---|---|---|---|
| 沿 LOWER（正确路线，通往车底站台 `12,15`） | `15,15` | **本车已订**（`:1572`/`:1575`） | 被 mask ⇒ 不可进 |
| 沿 X（错路，走到 `1,15` 死胡同） | `14,14` | `0x0`（`:1640`） | 可进，但到不了车底 |

⇒ 唯一可走的方向恰好是错的那条 ⇒ `enter=13` 展开枯竭 ⇒ `found=0` ⇒ 方向不再刷新 ⇒ 车沿 X 直行。**`noconsist=6` 的 6 个格正是 `rp()` 订下的 `15,15 / 14,15 / 13,15` 等「有主预留但无车底」的格**（`noplat=1` 才是唯一非站台/非车库被拒格）。

**为什么第一次寻路能 found=1**：第一次跑时这些都还没订。**订完就再也跑不动第二格**——这是 couple 路径用「寻路器」实现 PBS 的结构性冲突：寻路器把「已预留」一律视为障碍，而 couple 路径恰恰要一边订一边继续找。

## 19.4 由此否掉的第三条假设 + E1b 形态

- ~~「补订拐弯格的进入轨即可修好」~~ → **证伪**（19.2）。
- ~~「只订 td 不变的同轨直行前缀」~~ → 同样无效：直行前缀订下的格在下一 tick 一样被 mask（只是那格恰好是车要走的方向，然而 `15,14` 的下一个直行格 `14,14` 本来就没订，问题不在前缀内）。

**E1b（本轮实际该做的）**：`rp()` **不订任何中间路径轨**，只保留 `rp_last_tile` 记录（供 `SetReservationTarget`）。理由：

1. 中间格一律不订 ⇒ 下一 tick 无自我 mask ⇒ `15,15 / 14,15 / 13,15` 全部可进 ⇒ `found=1`，方向逐格刷新；
2. 车底所在格的保护**不依赖 `rp()`**——由 `ReserveTrackUnderConsist()` 每 tick 负责（日志 `:1599-1625 RESERVECONSIST ... resNow=1` 证明车底整格预留已存在）；
3. 起点格 `st` 的出去轨也不必由 `rp()` 订（同样由 RESERVECONSIST 覆盖），故可一并去掉；
4. 这正是「正常 PBS 逻辑」的最小一步：**couple 路径只做「方向决策」，预留交给 PBS 的既有机制**，与 E2/E4 方向一致，且比 E2 更早暴露「中间格无预留、他车可插入」这一层（若出现，即 E3 要治的目标格被占特例）。

**代价（须在复测判据里盯）**：中间格无预留 ⇒ 他车可能插入路径 ⇒ 机车可能重算绕行或短暂卡顿。这是 E1b 的已知代价，不是回归。

## 19.5 下一轮复测判据（4 条）

1. 正判据：`CPL-PATHFOUND veh=57 found=1`，且 `:1632`、`:1633` 一带出现 `best_track` 逐格刷新（不再恒为 X）；
2. 正判据：机车进 `15,14` 后**拐向 `15,15`**，最终 `COUPLE-OK`；
3. 负判据：`CPL-SCAN` 的 `noconsist` 归 0（不再有「本车订下却没人走的格」）；
4. 负判据：不再出现车沿 X 一路开到 `14,14 / 13,14` 的 `LOCO-MOVE` 序列；`RESV-AUDIT-STRAY` 不新增。

- ❌「只能从两端侵入」——与「空气」口径矛盾。
- ❌「平行股道不连通」——F-5 已证伪。
- ❌「只认 `First()` 一端」——F-6 已证伪。
- ❌「预订被清除」——F-7 已证伪。
- ❌「机车一步没走」——F-2 已证伪。
- ⚠️「`5,15` 是车底另一端」——长度校准后被削弱，`5,15` 与 `12,15` 相距 7 格而车底长度不足，此说法仅为可能，未证实。

### 待取证（下一组）

- 找"空气"豁免的确切落点：`CheckTrainCollision` 一族里对 `R3RCouplePairMatches` / `OT_WAIT_COUPLE` 的跳过，**并确认它是否被 YAPF 的 `YapfTrainFollowTile` / `CheckSafePositionOnNode` 复用**。
- 从日志核 `12,15..13,15` 一带当日是否有别的车/预留（`RESERVECONSIST` / `RESV-*` 覆盖该格）。

### 与玩家口径的对照结论

- 玩家的主张「两端都应能挂」**在设计上成立，且当前实现并非排斥某一端**；它排斥的是"停在车底外侧"这种贴法。
- 所以缺陷不是"某端被禁"，而是（a）**必须侵入车底格**才能挂（导致只能从某一端进）、（b）**A\* 择优把近端丢了**。
- 修法因此有两个方向（性质不同，需拍板）：
  - **P1**：把"车底两端外侧空格（`13,15` / `5,15` 若为空格）"也纳入目的地判定 ⇒ 真正的"停在旁边就能挂"，两端天然对称，且近端必然胜出；
  - **P2**：保持"必须进车底格"，只查清 `12,15` 为何没赢（换轨/占用/代价），修择优。


### 仍是疑点的部分（待新日志）

1. `:1412` 之后机车只挪了 1px（x 271→270）且 `:1642 CPL-PATHFOUND found=0` ⇒ **它并没有真的跑完这 11 格去 5,15**，更像又被按住/无路可订。要新日志才能分清"走错方向"与"走两步又停"。
2. `5,15` 是车尾这一结论目前靠长度算术吻合 + `GetCouplePosition` 语义，**尚无逐节 dump 直接证据**；判据=新日志里 `R3RDUMP-CHAIN head=0` 出现在 tick 12428 附近时含 `tile=5,15` 的车节。
3. 若确认是"绕到车尾"，则玩家直觉（就在眼前为什么不挂）与 R3R 端头规则冲突，属**口径问题**而非实现对错；此时修法不是 B2/B6，而是"允许头对头挂接"或"优先选最近可挂那一端"。

## 十四、本轮进度与阻塞

- 已完成：§十一 的源码落点与日志对账（推翻"原生停车"归因，定位到 KI-499 门）。
- 阻塞：后续取数（`:597`~`:1315` 之间的 `CPL-PAIRSCAN state=` 序列）时玩家取消了命令，未取到。
- 未改任何源码、未构建。

## 20. E1b 已落地（2026-10-27 14:00 前后，已编译通过，实机复测待做）

### 20.1 改了什么（仅 `src\pathfinder\yapf\yapf_rail.cpp`，无头文件改动）

按 §19.4 实施「`rp()` 不再订任何中间路径轨」：

1. 删掉 `rp()` 内**全部** `reserve_dir_if_present(cur, cur_td)`（当前格）调用，共 4 处；
2. 删掉站台跳段分支里的平台格补订循环（`plat_track` / `TileOffsByDiagDir` 那一段），该分支现在只 `return true`；
3. 删掉两处 `reserve_dir_if_present(f3.new_tile, nd)` 与 E1 新加的「进入轨」补订块（`entry_tds` / `entry_td`）；
4. 三个 helper 一并删除：`r3r_log_book`（`RP-BOOK` 探针）、`reserve_if_present`、`reserve_dir_if_present`；
5. 保留：递归结构、`depth > 512` 上限、`rp_last_tile` 记录（供 `SetReservationTarget`）、`R3RSkippedStationStretchContains` 的到达判定、`RP cur=` 逐帧探针。

⇒ `rp()` 现在是**纯只读**的路径回溯：只用来求出「真实出发方向 + 目标格」，不再改地图预留。

### 20.2 构建自证

- 复用既有 `_tmp_inc_build.cmd`（未新建 `.cmd`），启动方式 `Start-Process cmd.exe -ArgumentList '/c','_tmp_inc_build.cmd' -WorkingDirectory ... -Wait`；
- 日志：`[3/3] Linking CXX executable openttd.exe`，无 `error C` / `FAILED:`；
- `build\R3R_incbuild.done` = `EXIT_CODE=0`（14:02:09）；
- 时间戳：`src\pathfinder\yapf\yapf_rail.cpp` 13:59:28 → `build\openttd.exe` 2026-10-27 14:01:51；
- `read_lints` 0 条；
- exe 字面量：`RP-BOOK=False`（helper 已删，删除生效）、`RP cur==True`、`CPL-SCAN=True`。

### 20.3 判读口径变更（重要）

`RP-BOOK` 探针随 helper 一起删除了，所以 **§19.5 判据里凡是依赖 `RP-BOOK` 的读法全部作废**（原判据 1「拐弯格出现两根轨」不再可读，属预期）。E1b 之后只看：

- `CPL-PATHFOUND ... found=`；
- `CPL-SCAN ... enter= / nores= / noconsist= / consist=`；
- `LOCO-MOVE veh=57 tile= / track=`（车实际走哪根轨）；
- `COUPLE-OK / COUPLE-FAIL`。

理由：E1b 的假设是「不再订位 ⇒ 不再自我 mask ⇒ `found` 应为 1」，这条**不需要**订位日志即可判定；订位日志反而会误导（本来就不该有订位）。

### 20.4 复测判据（4 条，与 §19.5 同口径）

1. `CPL-PATHFOUND veh=57 found=1`，且 `best_track` 逐格刷新（不再恒为 X）；
2. 机车在 `15,14` **拐向 `15,15`**，最终 `COUPLE-OK loco=57`；
3. `CPL-SCAN` 的 `noconsist` 归 0（不再有「订了没人走」的格）；
4. 负判据：不再出现沿 X 到 `14,14 / 13,14` 的 `LOCO-MOVE` 序列；`RESV-AUDIT-STRAY` 不新增。

### 20.5 若仍 `found=0`（下一步 E2 的入口条件）

说明「自我 mask」不是唯一因素，按 §18 的路线进 **E2**：`rp()` 彻底改调 PBS `ExtendTrainReservation`（由引擎自己边找边订、自己识别自己的预留），couple 寻路的 yapf 分支只负责「找目标格」。届时需先确认 `ExtendTrainReservation` 在小半径/岔道上的 `Trackdir` 语义与 `SetReservationTarget` 的衔接点（`train.h` 相关接口）。

### 20.6 已知代价（写清，避免下一轮当回归）

中间格无预留 ⇒ 他车可能插入这段路，机车可能重算绕行或短暂停等；`ReserveTrackUnderConsist()` 仍在（日志 `RESERVECONSIST ... resNow=1` 实证），所以**车底格与脚下格仍有保护**。这正是 E3 要治的问题，E1b 把它提前暴露出来是刻意的。

## 21. 玩家对 E1b 的复测反馈（2026-10-27，E1b 实机无效 + 新怀疑方向）

### 21.1 玩家原话（不改写）

> 「现在列车还是走错道岔，我怀疑跟路点分支的非法滑行有关系」

⇒ 两条信息：
1. **E1b 也没修好**：机车仍走错道岔（即 `15,14` 仍沿 X 出去，未拐向 `15,15`）。这与 §20.5 的入口条件一致 —— 「自我 mask」**不是**（唯一）因素，E1/E1b 两轮都打在同一个错误假设上。
2. **玩家提出新怀疑方向**：「路点分支的非法滑行」。这条此前**从未被本备忘的任一条根因假设覆盖**，属新增线索。

### 21.2 疑点登记（按「有疑点/猜测根因」即刻落盘规则）

- **疑点 D-1（新增，待验证）**：机车在做 couple 寻路时，其**实际运动**可能不是由寻路的 `next_trackdir` 驱动，而是走了「路点分支」的既有滑行逻辑（例如 `ProcessOrders` / `TrainCheckIfLineEnds` / waypoint 到达处理里对 `OT_GOTO_WAYPOINT` 的分支），该分支沿**当前轨道方向**继续滑行而不重新选轨 ⇒ 表现为「无论 yapf 选了什么，车都沿 X 直行」。若成立，则前 3 轮全部改在 `rp()` 上都是**改错了层**（改的是寻路侧，病在运动/订单侧）。
  - 支撑（间接）：E1b 之后 `rp()` 已**完全不订任何中间格**，下一 tick 的 `CanEnterNewTile` 不可能再被本车预留 mask，若仍是同一现象，说明「mask」这条因果链不成立或不完整。
  - 待验证判据见 §21.4。
- **猜测根因 D-2（待验证）**：`noconsist=6` 中那 6 格可能就是「路点/站台跳段」被 yapf 一步跳过（`TrackFollower::tiles_skipped`）的格；`R3RSkippedStationStretchContains()` 只覆盖站台轴向跳段，**不含路点**（路点不是 `IsRailStationTile`），若现场路径实际穿过路点，则该跳过判定会漏，导致 `rp_last_tile` 与真实到达点错位。

### 21.3 本轮取证纪律

- 本轮**不再**在没有新日志的情况下继续推断 `rp()` 内部细节（前 3 轮教训：§18.2 / §19.3 各被实测证伪一次）。
- 需要玩家提供 **E1b 版 exe（`build\openttd.exe` @2026-10-27 14:01:51）产生的新 `R3R_debug.log`**，且必须包含机车在岔道口前后的连续行。

### 21.4 下次取数要看的行（判据）

1. `RP cur=` 序列：机车经过 `15,14` 时最后一个 `RP cur=` 是哪一格、`td=` 多少（这决定 `rp_last_tile` 与 `SetReservationTarget` 的终点）。
2. `CPL-PATHFOUND / CPL-SCAN`：`found=` 与 `enter/nores/noconsist/consist`；**重点看 `noconsist` 是否仍为 6**（若 E1b 后仍恒 6，说明那 6 格与 `rp()` 的订位无关，D-2 成立概率上升）。
3. `LOCO-MOVE veh= tile= track=`：机车在岔道口的**实际** `track=` 与 `dir=`，与同 tick 的 `next_trackdir` 对比。
4. `TRP / TPR-*`（`TryPathReserve` 一族）：看机车是否有**自己的普通 PBS 预留**、终点在哪格 —— 若机车根本没有预留却仍在移动，即「非法滑行」的机器可读形态。
5. 路点相关：`SKIP-STOPPED`、`GOTO_WAYPOINT`、`CRT`（`ChooseTrainTrack`）在岔道口附近是否出现；路点格坐标（便于确认是否在路径上）。
6. `SPD-DROP` / `LOCO-IDLE`（第 242 轮探针，若仍在）：判断车是被刹车还是被滑行带动。

## 22. 决定性发现：病不在 `rp()`，在 `ChooseTrainTrack()` 的「已有预留就早退」（2026-10-27 E1c）

### 22.1 代码事实（带行号）

`src\train_cmd.cpp`：

- `:17048` `TrackBits res_tracks = (TrackBits)(GetReservedTrackbits(tile) & DiagdirReachesTracks(enterdir));`
- `:17075-17078`
  ```cpp
  const bool r3r_couple_stuck_retry = consist->current_order.IsType(OT_GOTO_COUPLE) &&
          !consist->current_order.GetCoupleIsDepot() && consist->cur_speed == 0;
  if (res_tracks != TRACK_BIT_NONE && !r3r_couple_stuck_retry) return { FindFirstTrack(res_tracks), result_flags };
  ```
  ⇒ **只要该格已有本车预留、且车在动（`cur_speed != 0`），`ChooseTrainTrack()` 一次 yapf 都不跑**，直接把 `FindFirstTrack(res_tracks)` 当答案返回。
- `src\track_type.h:19-29`：`TRACK_X = 0` 是最低位 ⇒ `FindFirstTrack()` 在 `TRACK_X|TRACK_LOWER` 上**恒返回 TRACK_X**。
- `:19903-19905` `TrainController()` 唯一真正消费该返回值的路径：`chosen_track = TrackToTrackBits(result.track)`。
- `:20180` `chosen_dir = VehicleEnterTileCoordinates(gp, enterdir, TrackBitsToTrack(chosen_track));`
- `:20238` `v->track = chosen_track;`、`:20251-20254` `v->SetMovingDirection(chosen_dir);`
- `:19841` 若 `gp.old_tile == gp.new_tile`（已在本格内）**不重新选轨**，拐向只在「跨格那一瞬」定死。

### 22.2 结论

「走错岔道」的因果链是：

> 岔道格（如 `15,14`，`TRACK_X|TRACK_LOWER`）上有本车预留 ⇒ `:17078` 早退 ⇒ 返回最低位 `TRACK_X` ⇒ `v->track = TRACK_X` ⇒ 车沿 X 直行到 `14,14`，**与 yapf 选了什么、`rp()` 订了什么完全无关**。

这同时解释了为什么 E1（补订进入轨）与 E1b（完全不订）两次都「生效但无效」：两次改的都在 yapf 侧，而 yapf 在行驶中的挂接机车上**根本没被调用**。

也解释了玩家「路点分支的非法滑行」的直觉是对的：车是按「预留位最低轨」滑行，而不是按寻路结果走。

第 289 轮续五（KI-457）已经发现同一个早退，但只对「`cur_speed == 0` 的停住挂接车」放行；**行驶中的车没有放行** ⇒ 本现场正好落在未覆盖的一半。

### 22.3 E1c 改法（最小、对症）

把放行条件从「挂接单 + 非车库 + 已停住」扩到「挂接单 + 非车库 + **该格预留不足以唯一确定方向**（`KillFirstBit(res_tracks) != TRACK_BIT_NONE`）」：

- 单轨可达时仍走早退（省一次寻路，行为逐字不变）；
- 双轨及以上（正是岔道格）⇒ 跑 yapf ⇒ 拿到 best path 第一跳 ⇒ 拐对方向。

### 22.4 复测判据

1. 岔道格（`15,14`）这次 `CPL-PATHFOUND`/`CRT` 必须出现（证明 yapf 真被调用），且 `best_track` 为该拐弯方向；
2. `LOCO-MOVE veh=57 tile=15,14 track=` 不再是 `01`（X），而是 LOWER 那条；
3. 之后应出现 `15,15` 方向的移动与 `COUPLE-OK loco=57`；
4. 负判据：单轨格的 `crT`/`RESV-EXT-PRE` 频次不变（不因多跑 yapf 而暴涨），且不新增 `TPR-BLOCKED`。

## 23. E1c 已落地（2026-10-27）

### 23.1 改动（仅 `src\train_cmd.cpp`，未碰任何 `src\*.h`）

`ChooseTrainTrack()` 在 `:17075` 那段旁边新增第二半放行条件：

```cpp
const bool r3r_couple_split_res = consist->current_order.IsType(OT_GOTO_COUPLE) &&
        !consist->current_order.GetCoupleIsDepot() &&
        KillFirstBit(res_tracks) != TRACK_BIT_NONE;
...
if (res_tracks != TRACK_BIT_NONE && !r3r_couple_stuck_retry && !r3r_couple_split_res) return { FindFirstTrack(res_tracks), result_flags };
```

- `r3r_couple_stuck_retry`（第 289 轮续五 / KI-457，`cur_speed == 0`）**原样保留**；
- 新增项只在「挂接单 + 非车库 + `res_tracks` 有两个及以上 bit」时生效 ⇒ 单轨格早退逐字不变，非挂接订单一字不动；
- 配套只读探针 `RESV-SPLIT-PASS veh= tile= enterdir= res= tracks= spd= real=`（`R3RDbgOn()` 守卫 + 按 veh 的 sig 去重，sig = `res_tracks | tile | cur_speed`）。标签带 `RESV-` 前缀以通过 `R3RDbgTagAllowed()`（否则会被静默丢弃）。

### 23.2 构建自证

- 复用 `_tmp_inc_build.cmd`（未新建任何 `.cmd`）；构建前 `Get-Process openttd` 无输出 ⇒ 未运行。
- `build\R3R_incbuild.guard.log`：`GUARD: incremental is safe`。
- 日志尾：`[2/4] yapf_rail.cpp.obj` → `[3/4] train_cmd.cpp.obj` → `[4/4] Linking CXX executable openttd.exe`（yapf_rail.cpp 是 E1b 那次因 exe 被占用而未链接进 exe 的改动，本轮一并入库）。
- `build\R3R_incbuild.done` = `EXIT_CODE=0`（14:15:31）。
- 时间戳：`src\train_cmd.cpp` 14:13:29 → `build\openttd.exe` 14:15:28（52 310 528 B）。
- `read_lints(train_cmd.cpp)` = 0 条。
- exe 字面量核对命中：`RESV-SPLIT-PASS veh=%d tile=%d,%d enterdir=%d res=0x%X tracks=0x%X spd=%d real=%d`。

状态＝已实现 + 已编译，游戏内复测待做（判据同 §22.4）。

### 23.3 若 E1c 仍无效的下一层

按玩家线索继续查「路点分支的非法滑行」（`CheckNextTrainTile` 的 `OT_GOTO_WAYPOINT` 分支 `:15261-15264` 与 `CFollowTrackRail` 的 `tiles_skipped` 路点跳段），以及「结果 `result.track` 除 `TrainController` 之外的 4 个调用点都被丢弃」这条事实（`ChooseTrainTrack` 只在 `:19903` 一处被真正消费，其余 `:15295/:16948/:16974/:18512` 只借它订位）。

## 24. E1c 实机复测：证伪 + 新怀疑（2026-10-27 14:21 日志）

输入 `D:\temp\codebuddy-dropped-files\3512d960-291e-4388-b12f-455959242d9a\R3R_debug.log`（1630 行 / 227 370 B / mtime 14:21:24）。`RP-BOOK` 0 命中 ⇒ 产物 ≥ E1b。`RESV-SPLIT-PASS` **0 命中**，且两处 `RESV-EXT-PRE` 的 `reserved=0x0` ⇒ `res_tracks == TRACK_BIT_NONE` ⇒ `:17078` 早退不触发、E1c 条件不成立 ⇒ **本现场 E1b 与 E1c 行为逐字等价**，E1c 判据①实证失败。

关键读数：

```
1497 LOCO-MOVE veh=57 tile=16,14 spd=5 track=01 dir=5 db=1 dtile=1932
1498 RESV-EXT-PRE veh=57 tile=15,14 enterdir=0 tracks=0x9 reserved=0x0 doRes=0
1501 CPL-WAITCUT veh=57 state=0 wp=20 tile=16,14
1505 CPL-PATHFOUND veh=57 found=1
1506 CPL-SCAN ... found=1 nodes=17 nores=0 noconsist=6 consist=3 last=5,15 why=acc-car
1508 CPL-BEST veh=57 tile=5,15 td=0 cost=10242
1509 CPL-BACK pPrev=5,15  td=0 parent=15,15
1510 CPL-BACK pPrev=15,15 td=2 parent=16,14   <-- 正确路径 16,14→15,15→5,15
1511 CPL-TRACE st=16,14 tdb=0x101
1512 REACH cur=16,14 td=0 depth=0 trk=0 bits=0x1
1513 REACH cur=15,14 td=0 depth=1 trk=0 bits=0x29   <-- 岔道格仍 trk=0(X)
1514 REACH cur=14,14 td=0 depth=2 trk=0 bits=0x11   <-- 已直行过岔道
1583 LOCO-MOVE veh=57 tile=15,14 spd=30 track=01 dir=5 db=1
1624 RESV-EXT-PRE veh=57 tile=14,14 enterdir=0 tracks=0x11 reserved=0x0
1627 CPL-PATHFOUND veh=57 found=0
1628 CPL-SCAN ... found=0 nores=6 noconsist=6 consist=0 why=none
```

事实 1：同一次尝试存在两套矛盾路径 —— `CPL-BACK` 要 `15,15`，`REACH` 走 `15,14→14,14→13,14`，车实际 `track=01` 跟 `REACH` 一致。
事实 2：`reserved=0x0` ⇒ E1b 之后岔道格上本车一根轨都没订。
事实 3：从 `15,14` 再找 `found=0`（`nores=6 noconsist=6`）⇒ 进了 X 轨就再也找不到车底，挂接彻底失败。
事实 4：`db=1`、`dir=5` 全程不变，而 `enterdir=0`（X_NE）⇒ 车是在**倒着向东北**开。

怀疑 A（主）：挂接寻路算对了，但运动侧选轨/订位消费的不是它（`REACH` 全 X）——`DoTrainCouplePathfind` 的 best path 只用于 `SetReservationTarget`，而 `TrainController → ChooseTrainTrack → TracePath` 走普通 yapf 目标（`dtile=1932`）。
怀疑 B：E1b 的「`rp()` 全不订位」把中间格预留清成 0，PBS 不再约束岔道方向 ⇒ 方向可能反了，应改成"精确订到 best path 每一格"。
怀疑 C（玩家线索）：`wp=20` 路点在挂接路径上，`REACH` 连续 4 格 `trk=0` 无转折，与"路点跳段/沿 X 盲滑"吻合（待验证 `wp=20` 是等待点序号还是路点 tile）。
怀疑 D：`db=1` 下 `direction` 与 `GetMovingDirection()` 混用（KI-242 同族），可能把 `15,15` 判在"后方"从而拒走。

下一步（只加探针）：①`:20238` 前打 `TRKSET veh= tile= chosen= src=trace|yapf|early`；②TracePath 递归里打 `TRCHK cur= td= want_cur= want_td= hit=` 与挂接 best path 对照；③复核 `db=1` 时选轨参照系。

### 24.5 疑点纠正：`REACH` 不是"实走路径"（2026-10-27 读码确认）

读 `src\pathfinder\yapf\yapf_rail.cpp:1011-1081` 后确认：`REACH` 探针（`:1027`）打在 `reach()` 里，而 `reach()` 是一个**独立的深度优先泛洪**，作用只是"从起点 `st` 找一个能走到车底头 `tg` 的**出发 trackdir** `best_td`"（`:1022` `Trackdir best_td`、`:1069-1072` 第一个 `reach()` 返回 true 的 td 即 `best_td`）。它与 `CPL-BACK` 的 parent 链（挂接 best path）**没有任何关系**。

因此 `:1512-1515 REACH 16,14→15,14→14,14→13,14` 只是 DFS 第一个分支（`td=0` = `TRACKDIR_X_NE` 向东）的**探索顺序**，`depth` 单调增 0/1/2/3 正是"一路直行没有回退"的形态，**不能证明车实际走了 X**。§24.3「事实 1（路径分叉）」的表述撤回——两套路径不是"打架"，`REACH` 根本没参与选轨。

仍然成立的硬事实（与探针语义无关）：

- `:1497` 车在 `16,14`、`:1583` 车在 `15,14`、`:1624` 车在 `14,14`，且 `:1583` 的 `track=01`（`TRACK_X`）；即**车确实从 15,14 直行进了 14,14**；
- `:1498`/`:1624` 两处 `reserved=0x0`（E1c 条件不成立）；
- `:1627-1628` 从 `15,14` 再找 `found=0 nores=6 noconsist=6`（进了 X 轨就回不了头）。

**新的取数目标**（本轮要加的两支探针）：

1. `CRTRET` —— `ChooseTrainTrack()` **统一出口**打印实际返回值（含早退分支与 yapf 分支），只在"挂接单且该格 ≥2 根轨"时打、不节流。这是唯一能区分"早退给的 X"与"yapf 给的 X"的读数。
2. `TRKSET` —— `src\train_cmd.cpp:20262` 前打印 `chosen_track` / `enterdir` / `gp.old_tile` / `gp.new_tile` / `v->track`（旧），确认 `v->track = chosen_track` 写入的是不是 `CRTRET` 给的那个值（排除 `chosen_track &= bits` 或 wagon 分支改写）。

配套：`CPL-TRACE-RESULT`（`yapf_rail.cpp:1076`）已在日志里，复测时直接读 `best=` 判断 DFS 出发点方向是否选错。

## 25. 第 329 轮 E1d：两支只读探针落地（2026-10-27）

玩家授权原话：「可以加探针，游戏进程已关闭」。改动仅 `src\train_cmd.cpp`，未碰任何 `src\*.h` ⇒ 增量合法。

### 25.1 实际探针名（与 §24.5 规划不同，原因是日志标签白名单）

`R3RDbgTagAllowed()`（`src\r3r_perf.h:338-360`）只放行前缀 `RESV-` / `UNRES-` / `EXTEND-` / `LOOP-` / `R3R-RES-` / `RES-` / `RP` / `DEPOT` / `BOOK` / `SPLIT` / `RPATH` / `PRB-` / `RPHIT` / `CRT` / `CPL` / `CTRL` / `CGRP` / `R3R-`，**其余标签包括 `CRTRET` / `TRKSET` 会被静默丢弃（日志里看不到 ≠ 没触发）**。故本轮改名：

| 规划名 | 实际标签 | 语义 |
|---|---|---|
| `CRTRET` | **`CPL-RET`** | `ChooseTrainTrack()` 统一出口的实际返回值 |
| `TRKSET` | **`CPL-TRKSET`** | `v->track = chosen_track;` 写入点 |

以后新增任何探针前必须先查这张白名单。

### 25.2 `CPL-RET`（`ChooseTrainTrack()` 统一出口）

在 `ChooseTrainTrackResultFlags result_flags = CTTRF_NONE;` 之后定义 lambda `r3r_chosen_ret(ret_track, ret_flags, why)`，函数内**全部 14 个 return 点**都改写成 `return r3r_chosen_ret(..., "<why>")`，`<why>` 取值：

`early-haverserved` / `single-restrict-wait` / `single-track-nores` / `resfail-extend` / `res-ok-brief` / `cpl-search-skip` / `cpl-moving-keep` / `cpl-wait-state2` / `cpl-fail` / `nores-final` / `cpl-partial-res` / `resfail-notokay` / `lost` / `final`。

打印条件：`R3RDbgOn() && consist->current_order.IsType(OT_GOTO_COUPLE) && KillFirstBit(tracks) != TRACK_BIT_NONE`（只在挂接单的岔道格打），按 `veh` 记一行，签名 `ret_bits|res|enterdir|tracks` 变化才重打（不节流、不丢点）。

字段：`veh tile enterdir tracks res ret why spd real`，`ret=0xFF` 表示返回 `INVALID_TRACK`，`res` = `GetReservedTrackbits(tile) & DiagdirReachesTracks(enterdir)`。

判读：`:1498`/`:1624` 现场应各出现一行 —— 若 `why=early-haverserved` 则早退给的 X（说明 `res_tracks` 非空，与 E1c 结论冲突需重审）；若 `why=resfail-extend` / `res-ok-brief` / `cpl-*` 则 yapf 分支给的 X；若 `ret=0x1`（`TRACK_X`）+ `why=final` 则是 `best_track` 本身选了 X。

### 25.3 `CPL-TRKSET`（写入点，`:20288` 前）

字段：`veh old=x,y new=x,y enterdir chosen=0xX vtrk=0xX tdir spd real db order`，条件同上（挂接单 + `KillFirstBit(chosen_track) != TRACK_BIT_NONE`），签名去重。

判读：`chosen` 与 `CPL-RET` 的 `ret` 不一致 ⇒ 中间被改写；一致但 `tdir` 与 `chosen` 不匹配 ⇒ `v->track` 与 `v->trackdir` 脱钩（`db=1` 时的 `TRACKDIR` 镜像问题，KI-242 同族）。

### 25.4 构建自证

- 复用 `_tmp_inc_build.cmd`（未新建任何 `.cmd`）；`build\R3R_incbuild.guard.log` = `GUARD: incremental is safe (no header/lang file is newer than the newest object)` ⇒ 走增量。
- 首次编译失败（`error C2228: ".base" 的左边必须有类/结构/联合`，`TrackBits` 是普通枚举无 `.base()`；`error C2737: const 对象必须初始化` 于 `:20294 ts_sig`）⇒ 把 `chosen_track.base()` / `(v->track & TRACK_BIT_MASK).base()` / `TrackToTrackBits(ret_track).base()` 三处改成直接强转 `(uint32_t)`；重编通过。
- 日志尾：`[2/3] Building CXX object ...train_cmd.cpp.obj` → `[3/3] Linking CXX executable openttd.exe`。
- `build\R3R_incbuild.done` = `EXIT_CODE=0`。
- 时间戳：`src\train_cmd.cpp` 14:39:21 → `train_cmd.cpp.obj` 14:39:42 → `build\openttd.exe` **2026-10-27 14:40:50（52 310 528 B）**。
- `read_lints(train_cmd.cpp)` = 0 条。
- exe 字面量核对：`findstr /m /c:CPL-TRKSET`、`/c:CPL-RET` 均命中 `build\openttd.exe`。

状态＝已实现 + 已编译，游戏内复测待做。

### 25.5 待复测判读清单

1. `CPL-RET` 是否在 `16,14`（进入岔道格前）与 `15,14` 各出现一行？`why=` 是哪一个？
2. `CPL-RET ret=` 是否为 `0x1`（`TRACK_X`）？
3. `CPL-TRKSET chosen=` 与 `CPL-RET ret=` 是否一致？
4. 若两者都是 `X` ⇒ 选轨本身选了 X（`best_track`），下一步查 `TracePath` 的 follow/目标点（`dtile=1932` 方向）；
5. 若 `CPL-RET` 给的是 `0x2`（`TRACK_Y`）而 `CPL-TRKSET chosen=0x1` ⇒ `chosen_track` 被改写，查 `:20262-20288` 之间的分支（`chosen_track &= bits`、wagon/origin 特殊处理）。

## 26. 第 329 轮 E1d 实机复测：**抓到真凶**（2026-10-27 14:44 日志）

输入 `D:\temp\codebuddy-dropped-files\ec844410-aac8-4a84-9d36-7f0e28483c3a\R3R_debug.log`（1683 行 / 231 822 B / mtime 14:44:35）⇒ 产生者就是 E1d 版 exe（`CPL-RET` 命中，旧版 exe 无此串）。

### 26.1 两支探针的实际读数

`CPL-RET` 只 2 行（`CPL-TRKSET` **0 行**，原因见 §26.4）：

```
1562: CPL-RET veh=57 tile=15,14 enterdir=0 tracks=0x9  res=0x0 ret=0x1 why=nores-final     spd=5  real=1
1630: CPL-RET veh=57 tile=14,14 enterdir=0 tracks=0x11 res=0x0 ret=0x1 why=cpl-moving-keep spd=30 real=1
```

紧接着 1635：`LOCO-MOVE veh=57 tile=14,14 j=196 spd=30 track=01 dir=5 db=1 stuck=0 dtile=1932` ⇒ 车实际确实沿 `track=01`(X) 走。

### 26.2 决定性对照：挂接寻路订的是 LOWER，返回的却是 X

同一 tick、同一格 `15,14`，同一份日志里紧邻的两组读数：

```
1557: RP cur=15,14 td=3 depth=1 trk=3 bits=0x29     <-- 挂接寻路 rp() 订的是 TRACK_LOWER(td=3)
1558: RP cur=15,15 td=2 depth=2 trk=2 bits=0x15
1559: RP cur=14,15 td=0 depth=3 trk=0 bits=0x9
1560: RP cur=13,15 td=0 depth=4 trk=0 bits=0x1
1561: CPL-RESERVE veh=57 reserved=1 next=0          <-- 挂接寻路自报成功
1562: CPL-RET veh=57 tile=15,14 ... ret=0x1 why=nores-final   <-- 运动侧选轨返回 TRACK_X
```

⇒ **挂接寻路（`DoTrainCouplePathfind` + `rp()`）方向完全正确**（`15,14` 上取 LOWER → `15,15` → `14,15` → `13,15`，与 §24 的 `CPL-BACK 15,15 parent=16,14` 完全一致）；**错的是同一格里另一套寻路——运动侧的普通 yapf 目标寻路，它返回了 `TRACK_X`**。两套寻路在同格给出相反结论，且运动侧的那一套被执行。

这把 §24 的「怀疑 A」由推断升为**实证**：`DoTrainCouplePathfind` 的 best path 只喂给 `SetReservationTarget`（预留），**选轨完全不看它**；`ChooseTrainTrack` 里的 `best_track` 来自普通 yapf（目标 `dtile=1932`），两者从未对齐。

### 26.3 `why=nores-final` 的含义（关键新读数）

`nores-final` 是本轮新加的出口之一，位置在函数后段、`res_dest.okay` 快速出口之后、注释「A path was found, but could not be reserved.」之前：

```cpp
if (!do_track_reservation) return r3r_chosen_ret(best_track, result_flags, "nores-final");
```

命中它 ⇒ ①`best_track` 已经由前面的普通 yapf 选轨算出（不是早退、不是 `FindFirstTrack(tracks)` 兜底）；②`do_track_reservation == false`，即**这一格根本没走"订位"路径**——`res=0x0` 与此互证（该格一根轨都没订）。所以这既不是「早退给了 X」（E1c 假设，已证伪），也不是「订位失败退回 X」，而是**纯选轨结果就是 X**。

### 26.4 `CPL-TRKSET` 为什么 0 命中（我方探针条件写错了，不是没触发）

写入点条件写的是 `KillFirstBit(chosen_track) != TRACK_BIT_NONE`，但 `chosen_track` 在那里是**单根轨**的 bit 集（由选定的 trackdir 反推），`KillFirstBit` 后恒为 `NONE` ⇒ 条件恒假。正确条件应看「这一格的轨道位」（该格是否岔道）：`IsPlainRailTile(gp.new_tile) && KillFirstBit(GetTrackBits(gp.new_tile)) != TRACK_BIT_NONE`。

不过本条不影响结论：`ret=0x1` + `LOCO-MOVE track=01` 已经证明写入的就是 X，写回链无需再查。若要补证，下轮改条件即可。

### 26.5 结论（KI-504 的因果链，现已闭合到"两套寻路结论相反"这一环）

1. 车 57 在 `16,14` 处进入 `15,14`（岔道格 `bits=0x9` = X|LOWER）。
2. 挂接寻路算对并预留了 `LOWER`（`RP cur=15,14 td=3`），`CPL-RESERVE reserved=1`。
3. 同一 tick 的普通 yapf 选轨返回 `TRACK_X`（`CPL-RET ret=0x1 why=nores-final`）。
4. 车沿 X 直行到 `14,14`，`15,15`/`14,15`/`13,15` 那串 LOWER 预留从此无人消费（`res=0x0`），且车在 X 上无法回头 ⇒ 从 `15,14` 再找 `found=0 noconsist=6`（§24 的 `:1627-1628`）。

### 26.6 下一步（两条待拍板方向，本轮先不动码）

- **方向 1（治标、最小面）**：当「挂接单 + 该格恰有挂接寻路订下的轨」时，`ChooseTrainTrack` 允许用挂接 best path 的首个 trackdir 覆盖 `best_track`。需要先确认 `DoTrainCouplePathfind` 的结果是否能在 `ChooseTrainTrack` 里取到（`SetReservationTarget` 的落点/是否留下可读字段）。
- **方向 2（治根）**：查普通 yapf 为什么在挂接单上仍以 `dtile=1932` 为目标（而不是等待车底所在格 `5,15`/`12,15`）——若目标本身是错的，则所有选轨都不可信；`dtile=1932` 的语义仍需玩家/代码确认。

**待玩家确认**：`dtile=1932` 对应的 tile 坐标（`1932 & 0xFF` = x=140? 该字段是打包值还是 tile index，需按 `SetDestTile` 口径核对），以及"你希望挂接机车是**先按挂接路径开到车底跟前**，还是允许它用普通 yapf 自由选道只要最终能挂上"。

### 26.7 `dtile=1932` 的语义已查明（2026-10-27 追加，只读取证）

探针打的是 `consist->dest_tile.base()`，而 `Vehicle::SetDestTile()`（`vehicle_base.h:1035`）是**裸赋 `TileIndex`，无任何打包** ⇒ `1932` 就是货真价实的 tile index（坐标 = `x = 1932 % MapSizeX()`，`y = 1932 / MapSizeX()`；本会话地图尺寸未取，故坐标待定，但必然是某个**站台格**，见下）。

写点唯一：`order_cmd.cpp:4691-4703` 的 `OT_GOTO_COUPLE` 分支：

```cpp
const TileIndex station_dest = v->GetOrderStationLocation(order->GetDestination().ToStationID());
/* R3R (KI-446): keep the travel target on the platform of the paired consist,
 * so that the PBS destination and the couple pathfinder agree. */
const TileIndex couple_dest = (station_dest != INVALID_TILE) ? R3RGetCoupleOrderDestination(v) : INVALID_TILE;
v->SetDestTile(couple_dest != INVALID_TILE ? couple_dest : station_dest);
```

`R3RGetCoupleOrderDestination()`（`order_cmd.cpp:4601-4612`）= 取配对车（`R3RGetCouplePairPartner`）的 `tile`，且要求 `IsRailStationTile`，否则退回 `station_dest`（`GetOrderStationLocation`，站台的**锚点格**）。

⇒ **两个结论**：
1. **KI-446 只对齐了「目标格」，没对齐「进入轨」（trackdir）**。它的注释自陈意图是 "so that the PBS destination and the couple pathfinder agree"，实测证明两者仍不一致：站台格在多格站台上从哪一支轨进、进第几格，普通 yapf 与 `DoTrainCouplePathfind` 各算各的。
2. 所以方向 2 的「目标本身是错的」**初步不成立**——`1932` 是配对车所在/订单站台格，方向大体正确；真正的分歧在**同一站台内选哪一支轨**（`15,14` 的 `bits=0x9` = X|LOWER，普通 yapf 取 X、挂接寻路取 LOWER）。这反而**加强了方向 1**（用挂接 best path 的首个 trackdir 覆盖 `best_track`）的必要性。

## 27. 第 330 轮：按方向 3 取证（2026-10-27，玩家已授权「先按照你推荐的方向取证」）

### 27.1 本轮取证计划（先落盘，再取证）

玩家选取了我推荐的方向 3 = §17.16「修法 D 校正形态」：让 `rp()` 只沿「td 不变的同轨直行前缀」逐格预留，遇换 trackdir 即截断（而不是沿 best_td 的成功路径回溯到 LOWER 绕行支路）。

**定案前必须先回答的疑点（编号 Q-330，决定"截断在哪一格"）**：

- **Q-330-1**：同一份日志里为什么出现**两条不同的 RP 链**——`§17.16` 记的 `:1537-1541 RP cur=16,14→15,14→14,14→13,14`（X 直行）与 `§26.2` 记的 `:1557-1560 RP cur=15,14 td=3→15,15→14,15→13,15`（LOWER 绕行）？是**两次不同 `DoTrainCouplePathfind` 调用**（不同 `best_td`），还是**同一次调用内的两个分支/两次递归**？
  - 判据：看两段 RP 之间是否夹着新的 `CPL-ENTRY` / `CPL-PATHFOUND` / `CPL-BEST` / `CPL-TRACE-RESULT` 行；看 `RP` 行的缩进/depth 字段是否连续。
  - 影响：若两次独立寻路，则"截断"只需按每次自己的 best_td 前缀做；若同一次内两分支，则说明 `rp()` 本身有重复预订/分支回退缺陷，修法形态要再定。
- **Q-330-2**：`rp()` 的进入轨计算是否已经修正为使用跟随器的实际 trackdir（KI-346 修复 `reserve_if_present(f3.new_tile, TrackdirToTrack(nd))`）。若已修，则"同轨直行前缀"的截断点判据可直接用 `TrackdirToTrack(nd) != TrackdirToTrack(cur_td)`。
- **Q-330-3**：`YapfTrainCoupleTrack()` 返回 `TrackdirToTrack(best_td)`，而 `best_td=0`（`CPL-TRACE-RESULT best=0`）在 `16,14` 是 X ⇒ 与运动侧同向。第二次从 `15,14` 出发时 `best_td` 是否变？（若 `found=0` 则不变，`best_track` 沿用上一次的值。）

### 27.2 取证结论区（边取证边回填，每条自带行号锚点）

**结论 27-A（Q-330-1 已答，并推翻 §17.16 的「两条不同 RP 链」表述）**：`REACH` 与 `RP` 是**一一对应**的两组行，同一 tick 同一棵树。

| REACH | RP | 读数 |
|---|---|---|
| `1512 REACH cur=16,14 td=0 depth=0 trk=0 bits=0x1` | `1537 RP cur=16,14 td=0 depth=0 trk=0` | 根 |
| `1513 REACH cur=15,14 td=0 depth=1 trk=0 bits=0x29` | `1538 RP cur=15,14 td=0 depth=1 trk=0` | X 分支 |
| `1514 REACH cur=14,14 td=0 depth=2 trk=0 bits=0x11` | `1539 RP cur=14,14 td=0 depth=2 trk=0` | X 继续 |
| `1515~1521`（`13,14`→`5,14`→`4,14`→`3,14`→`2,14 td=3`→`2,15 td=2`→`1,15`，depth 3..9） | `1540~1546` | X 分支走到底 |
| `1522 REACH cur=14,14 td=12 depth=2 trk=4 bits=0x11` | `1547 RP cur=14,14 td=12 depth=2 trk=4` | 第二条支路（从 15,14 拐出） |
| `1523~1531`（`14,13 td=13`→`13,13`→…→`1,15`，depth 3..11） | `1548~1556` | 第二条支路走到底 |
| `1532 REACH cur=15,14 td=3 depth=1 trk=3 bits=0x29` | `1557 RP cur=15,14 td=3 depth=1 trk=3` | **LOWER 分支** |
| `1533~1535`（`15,15 td=2`→`14,15`→`13,15`，depth 2..4） | `1558~1560` | LOWER 分支走到底 |

⇒ **`rp()` 不是"沿 best path 回溯订一条链"，而是"每个被展开的搜索节点都订一次"**（三支全订：X 直行、第二条支路、LOWER）。§17.16 写的「`rp()` 沿回溯链逐格换轨订到了 LOWER 绕行支路」与 §26.2 写的「rp() 订的是 LOWER」**都只看到三支中的一支**，实际是三支全订。

⇒ **Q-330-1 答案 = 既非两次独立寻路、也非两分支回退，而是同一次 `reach()` 展开过程中的逐节点遍历**。这意味着修法 D 的形态必须改写（§27.6）。

**结论 27-B（`rp()` 已是只读，`CPL-RET res=0x0` 完全正常）**：`yapf_rail.cpp:1091-1101` 注释自陈：

> `/* R3R (KI-504, E1b): rp() is read-only - it deliberately books NOTHING. Booking from inside the pathfinder turns the loco's own reservation into its next obstacle: TrackFollower::CanEnterNewTile() removes every already-reserved track from the enterable set and does not exempt the booking train itself (follow_track.hpp) ... Live case: 15,14 had TRACK_X booked, so the loco could no longer go straight on into 15,15 and was pushed onto the other branch. */`

⇒ E1b 已把「在寻路里订位」这条自我阻挡的路堵住。`rp()` 现在只走一遍求 `rp_last_tile`，再 `SetReservationTarget(pNode, rp_last_tile, pNode->GetLastTrackdir())`（`:1132-1134`）。所以 §26.2「rp() 订了 LOWER」**表述错误**：LOWER 那支只是 DFS 走过的分支，且不订任何位。

**结论 27-C（方向 2 彻底排除：`dtile=1932` 就是等待车底所在格 (12,15)）**：
- 日志坐标范围（`51 SKIP-STOPPED ... tile=62,117`）说明地图 ≥128 宽；按 128 宽反算：`1932 = 15 × 128 + 12` ⇒ `x=12, y=15`。
- 与 `1564 CPL-PAIRMAP tick=12556 role=target v=0 vHead=0 vTile=12,15 vOrd=17` 逐字吻合 ⇒ **普通 yapf 的目标就是等待车底 veh=0 所在的 `12,15`**，不是"远处站台"。
- ⇒ §26.6 的**方向 2 作废**（目标本身没错）。`R3RGetCoupleOrderDestination()` 工作正常。

**结论 27-D（真正的分歧点被精确定位到"第二次 ChooseTrainTrack"，即 `15,14` 这一格）**：
- 第一次决策在 `16,14`：`1498 RESV-EXT-PRE veh=57 tile=15,14 enterdir=0 tracks=0x9 reserved=0x0 doRes=0`、`1500 CPL-ORIGIN origin=16,14 td=0`、挂接寻路 `1536 CPL-TRACE-RESULT st=16,14 best=0` ⇒ 根节点的 departure trackdir = **0（X）**，运动侧也走 X ⇒ **第一次决策两边一致**，机车正确驶入 `15,14`（`1584 LOCO-MOVE veh=57 tile=15,14 ... track=01`）。
- 第二次决策在 `15,14`：`1562 CPL-RET veh=57 tile=15,14 enterdir=0 tracks=0x9 res=0x0 ret=0x1 why=nores-final` ⇒ 可进入轨只有 `TRACK_X(0x1)` 与 `TRACK_LOWER(0x8)`，运动侧取 **X**。
- 而挂接路径要求的是 **LOWER**：`1532 REACH cur=15,14 td=3 depth=1 trk=3`（td=3 即 `Y_SE`，`TrackdirToTrack(3)=TRACK_Y` 的南半=LOWER）→ `1533 15,15 td=2` → `1534 14,15 td=0` → `1535 13,15 td=0` → 之后靠 `:1048 R3RSkippedStationStretchContains()` 站台跳格判到 `tg=5,15` 并返回 true（所以日志里**没有** `REACH cur=5,15` 行）。
- ⇒ **「挂接能到、普通 yapf 到不了」的分岔就在 `15,14` 选 X 还是选 LOWER**。X 支路（`1538 15,14 td=0 → 1539 14,14 td=0 → 1540 13,14 td=0 → 1541 5,14`，一次站台跳 8 格）最终走到 `1,15`，**背离 `12,15`**。

**结论 27-E（§26.2 的解读必须订正：`CPL-RET` 的 `tile=` 是「前方目标格」，不是决策所在格）**：
- 配对证据：`1498 RESV-EXT-PRE veh=57 tile=15,14 enterdir=0 tracks=0x9 reserved=0x0 doRes=0 **spd=5**` 与 `1562 CPL-RET veh=57 tile=15,14 enterdir=0 tracks=0x9 res=0x0 ret=0x1 why=nores-final **spd=5**` —— 同 tick、同 `spd=5`，`tile=15,14` 是 `gp.new_tile`（下一格）。所以 `1562` 的决策发生在 **`16,14`**，`ret=0x1`(X) **是正确的第一步**。
- 第二次：`1630 CPL-RET veh=57 tile=14,14 enterdir=0 tracks=0x11 res=0x0 ret=0x1 why=cpl-moving-keep **spd=30**` ⇒ 决策发生在 **`15,14`**（当前格），下一格 `14,14`，仍然返回 **X**。**这里才是错的那一步**：应选 `LOWER`/Y 方向（通往 `15,15`→`14,15`→`13,15`→站台跳到 `12,15`）。
- ⇒ §26.2 表格「在 15,14 决策选 X」碰巧结论对、但证据引用错（引的是 16,14 那次）。`1561 CPL-RESERVE ... next=0` 同样是 16,14 那次。

**结论 27-F（真凶锁定：`best_track` 被「上一次起点的挂接寻路返回值」覆盖，且行驶中不再重算）**：
- `train_cmd.cpp:17437-17445`：
  ```cpp
  Track path_found = DoTrainCouplePathfind(consist, true);
  /* ... starts from the loco itself and always returns a valid departure
   * direction, so use it regardless of where ExtendTrainReservation ended up */
  if (path_found != INVALID_TRACK) { best_track = path_found; }
  ```
  `DoTrainCouplePathfind` 的返回值 = `TrackdirToTrack(next_trackdir)`，而 `next_trackdir` 在 `yapf_rail.cpp:1080-1081` 被设为 `best_td`，`best_td` 是 `reach(st, td, 0)` 在**起点格 `st`** 的 trackdir 循环里第一个能到 `tg` 的值（`:1069-1072`）。⇒ 它是**「从起点走出的第一根轨」**，天然只对起点格有效。
- 第二次决策（当前格 `15,14`）走的是 `why=cpl-moving-keep` 分支，**直接返回 `best_track`，不重算挂接路径** ⇒ 把上一次在 `16,14` 算出的 X 原样搬用 ⇒ 机车在 `15,14` 直行到 `14,14`，从此离开 `y=15` 行，再也回不到 `12,15`。
- 旁证：`17599` 的条件 `res_dest.tile != INVALID_TILE && !res_dest.okay && (!OT_GOTO_COUPLE || r3r_couple_depot)` 对挂接单非 depot **恒假** ⇒ `DoTrainPathfind`（会按 `dest_tile=12,15` 正确寻路的那个）**根本不被调用** ⇒ 挂接单完全依赖上面这个只对起点有效的返回值。

### 27.3 第二次搜索为什么 found=0（27-G）

**结论 27-G（决定性：第二次挂接寻路本身失败，方向 3 的「截断」救不了这个现场）**：

两次 `DoTrainCouplePathfind` 的 census 对照（日志锚点）：

| | 第一次（起点 `16,14`） | 第二次（起点 `15,14`） |
|---|---|---|
| 行 | `1505/1506` | `1628/1629` |
| `CPL-PATHFOUND` | `found=1` | **`found=0`** |
| `CPL-SCAN` | `closed=3 nodes=17 enter=14 noplat=5 nores=0 noconsist=6 consist=3 rej=0/0/0/0 acc=3/0 why=acc-car last=5,15 lastveh=0` | `closed=5 nodes=18 enter=13 noplat=1 **nores=6 noconsist=6 consist=0** rej=0/0/0/0 acc=0/0 why=none` |

⇒ 第二次搜索**一次也没扫到合法车底**（`consist=0`，`acc=0`，`lastveh=-1`）。因此：

1. `train_cmd.cpp:17437-17445` 的 `if (path_found != INVALID_TRACK) best_track = path_found;` **不执行**，`best_track` 原样保留第一次的 `X`；
2. 随后 `:17506 why=cpl-moving-keep` 直接把它返回（`1630 CPL-RET ... ret=0x1 why=cpl-moving-keep spd=30`）；
3. **即使把 `rp()` 改成"只沿 td 不变的同轨直行前缀预订"（方向 3 / §17.16 修法 D），这个现场也不会有任何改变**——第二次根本没有 `best_td`、没有 RP 链、没有可截断的路径。方向 3 只能影响"能达成但不该订那么远"的形态，无法影响"达成不了"的形态。

**机制（为什么从 `15,14` 出发就到不了车底）**：

- 起点由 `yapf_rail.cpp:817` 钉死为 `PBSTileInfo origin(v->tile, v->GetVehicleTrackdir(), false)` —— **单一 trackdir = 车辆当前朝向**（`1627 CPL-ORIGIN origin=15,14 td=0 vehDir=5 vehTd=0`，第一次同理 `1500 origin=16,14 td=0`）。
- YAPF 的起点节点**不会在起点格换轨**：它只能沿 `td=0`（X）继续。`15,14` 是道岔格（`bits=0x9` = X|LOWER），可通往车底的方向恰恰是 **LOWER**（第一次的成功路径就是靠"在 `15,14` 这个**中间节点**上由 `TrackFollower` 展开换轨"实现的：`1509 CPL-BACK pPrev=5,15 td=0 parent=15,15`、`1510 pPrev=15,15 td=2 parent=16,14`，而 `1532 REACH cur=15,14 td=3 depth=1` 正是那次中间节点的 LOWER 分支）。
- 一旦 `15,14` 变成**起点格**，这一格就退化成一维出口（只有 X），LOWER 分支从搜索树里消失 ⇒ 搜索永远留在 `y=14` 行 ⇒ `noconsist=6` 全是 y=14 行站台格上"没有车底"的早退（车底在 `y=15` 行，`PFD-ACCEPT ... tile=5,15 t=0`）。
- `noplat=1` 与 `nores=6` 的差异同源：第二次树更窄（只在 y=14 行 + 车库格上打转），第一次树能行经 y=15 行平台。

**疑点 REG-330-A（登记，不影响上面的判定）**：第一次 `closed=3` 却 `enter=14 / nodes=17`，第二次 `closed=5` 却 `enter=13 / nodes=18`；`closed` 与 `enter` 不呈单调关系（`enter` 含 `PfCalcEstimate` 的重复访问，见 `yapf_destrail.hpp:32-34` 注释）。⇒ **不能**用 `closed` 大小判断"搜索广度"，本节的"第二次树更窄"只用 `noplat/noconsist/consist` 三个语义计数支撑。待验证：若将来要在日志里区分"起点单 td 限制"与"剪枝"，需要新增"被探过的站台格坐标"探针（当前 `CPL-SCAN` 只有汇总计数）。

**疑点 REG-330-B（登记）**：`1505` 的第一次 `found=1` 与 `1509/1510` 的 best 链 `16,14 → 15,15 → 5,15` 存在一处尚未解释的地方——`1512 REACH cur=16,14 td=0 bits=0x1` 显示根节点只报告了 `X`，但 best 链的第一步却是 `Y_SE`（`15,15 td=2` 反向即从 `16,14` 走 `td=3`）。四种可能：(a) `REACH` 的 `bits` 字段不是该格全部轨道位，而是 `TrackFollower` 的"可进入位"；(b) 根节点的 `TrackFollower` 允许在起点格拐弯（与 27-G 的"起点不换轨"矛盾，若成立则 27-G 的机制需改写）；(c) `CPL-BACK` 的 `parent` 打印的是 `pNode->parent` 的 `GetLastTile()`，而跳跃节点的 `last tile` 语义与"上一格"不同；(d) `tdb=0x101`（`1511 CPL-TRACE st=16,14 tdb=0x101`）里 `0x100` 这个非常规位说明 `16,14` 的文件里还有一个非 `td` 位。**这条直接决定 27-G 的修法口径，必须先解决再动码**：若 (b) 成立，则"起点不换轨"不成立，第二次 `found=0` 就另有原因（更可能是目标格侧被判不可接受），此时方向 3 的截断口径要重新定。

**27-G 对三个方向的影响（据此重排优先级）**：

- **方向 1（用挂接 best path 首 trackdir 覆盖 `best_track`）**：对第二次决策无效——第二次 `found=0` 时没有 best path 可覆盖，仍会沿用陈旧 `best_track`。⇒ 单纯"覆盖"不够，必须同时处理 `found=0`。
- **方向 2（普通 yapf 目标错）**：27-C 已排除目标格错误，但 27-F 的旁证表明 `DoTrainPathfind` 从未被调用，所以"普通 yapf 会选什么"在本现场**没有任何实测数据**。⇒ 需要一个只读取证：临时让挂接单在非 depot 情形也跑一次 `DoTrainPathfind` 并 dump 它选的 track（不改变返回值），才能判断"改用普通 yapf 的 dest_tile 目标"是否真的能走对。
- **方向 3（`rp()` 截断）**：本现场不适用（27-G 第 3 点）。它不是错的，只是**修的是另一类现场**（能到目标但订得太远的 §17.16 形态）。

**下一步待拍板项（C-330）**：

- **C-330-1**：是否先做 REG-330-B 的只读取证（在 `reach()` 里补打根节点 `TrackFollower` 实际展开的 children，或直接补 `bits` 字段的取值来源），以确认"起点格能不能换轨"？
- **C-330-2**：若确认"起点单 td 导致找不到车底"，修法选择：
  - (i) 挂接寻路改用**多 trackdir 起点**（把起点格的其它可进入 trackdir 也作为搜索根，即 `PBSTileInfo` 换成能表达多 td 的形式）；
  - (ii) 在 `found=0` 且 `cur_speed!=0` 时**不沿用陈旧 `best_track`**，改为调用普通 `DoTrainPathfind`（`dest_tile` 目标）拿一个方向，或直接返回"本 tick 不预留/减速"让下一 tick 重新决策；
  - (iii) 同时做 (i)+(ii)。

**推论 27-H（登记，待验证）：(ii) 单独做很可能也治不好**。普通 `DoTrainPathfind` 同样走 `Yapf().SetOrigin(...)` 的**单一 trackdir 起点**（`yapf_rail.cpp:817` 同一行构造 `PBSTileInfo`），所以从 `15,14` 出发按 `dest_tile=12,15` 寻路时，若起点格不能换轨，它同样只能沿 X 走（→ `14,14`→`13,14`→`5,14`→…→`1,15`，即第一次 `REACH` 的 X 支路），一样到不了 `12,15` ⇒ 结果退化成"本 tick 无路径"，列车停在 `15,14` 不动 = 玩家报的"永远不动"。⇒ **治根大概率是 (i)：让挂接寻路在起点格按"每个可离开的 trackdir"各起一次搜索**（当前只用了 `v->GetVehicleTrackdir()` 一个）。这一条必须再取一次证（`15,14` 的其它出口在普通寻路里是否真的可用），否则 (i) 可能只是把失败从"选错方向"变成"还是选不到"。

### 27.4 第 330 轮取证探针（玩家拍板 C-330-1：「先加只读探针吧」）

**探针设计（全部只读，不改任何行为）**：

| 编号 | 文件 | 位置 | 日志串 | 回答什么问题 |
|---|---|---|---|---|
| P1 | `yapf_rail.cpp` | `CPL-ORIGIN`(`:818-826`) 之后 | `CPL-ORIGIN-TD` / `CPL-ORIGIN-FOLLOW` | 起点格的 `GetTileTrackdirBits` **原值**（不再经 `TrackdirBitsToTrackBits` 压缩）、每个 trackdir 各走一步的落点/站台位/跳格数 ⇒ 直接判定 `16,14` 的 `tdb=0x101` 是两个什么 td、`15,14` 到底有没有 LOWER 出口 |
| P2 | `yapf_rail.cpp` | `CPL-BACK`(`:993`) | 追加 `ptd=%d` | best 链上**父节点**的 trackdir（原来只打父节点的 tile）⇒ 判定 `5,15 ← 15,15 ← 16,14` 这条链的每一步是否同轨、是否发生"起点格换轨"（REG-330-B） |
| P3 | `yapf_destrail.hpp` | noconsist 早退(`:537-542`) | `PFD-NOCONSIST tile=x,y td=%d n=%u` | 每次挂接搜索里"探到的站台/车库格但没有车底"那 6 次分别在哪些坐标 ⇒ 判定第二次搜索的树是否真的只留在 `y=14` 行（27-G 的直接证据） |

**刻意不做**：不在 `FindNearestCoupleTrain` 里用"另一个 trackdir 重跑一次 `Yapf().FindPath()`"做对照试验——`yapf_rail.cpp:980` 的 `SetReservationTarget()` 在 `dont_reserve==true` 时**仍然执行**，重跑会把列车预留目标写成 ALT 搜索的结果，属行为污染，不是只读。改用 P1（地图事实）+ P3（搜索树落点）间接判定同一问题。

**限流**：P3 每次扫描最多 8 行 + 全局最多 240 行（`_r3r_cpl_scan_nocdump` / `_r3r_cpl_scan_nocdump_total`，前者在 `R3RCoupleScanReset()` 清零）；P1/P2 天然每 tick 各一行/每节点一行。

**判读表（复测时按此填）**：

- 若 `CPL-ORIGIN-FOLLOW` 在 `15,14` 上出现 `td=3`（LOWER）且 `ok=1 nt=15,15 st=1` ⇒ 地图上 LOWER 出口**存在且通向 y=15 站台**，则第二次 `found=0` 只能归因于"YAPF 从单一 origin td 出发、起点格不换轨"（27-G 成立，修法 (i)）；
- 若 `15,14` 根本没有 LOWER 出口（或 follow 失败）⇒ 27-G 的机制错误，`15,14` 必须靠别的路径（例如先直行再折返）到达车底，修法要改成"起点格逐 td 搜索"以外的口径；
- 若 `PFD-NOCONSIST` 的 6 个坐标全部落在 `y=14`（如 `14,14 / 13,14 / 5,14` 一带）⇒ 第二次搜索树确实没进 `y=15` 行，27-G 得直接证据；若出现 `y=15` 的坐标却不 accept ⇒ 问题在"站台扫描/闸门"而不是"能不能走到"；
- 若 P2 出现 `ptd` 与 `pPrev td` 不同轨（例如 `pPrev=15,15 td=2` 而 `ptd=0`）⇒ 该步是换轨，REG-330-B 的 (b) 假设成立（起点/中间节点可换轨），27-G 需改写。

### 27.5 探针落点与构建成本（2026-10-27，玩家问「加探针能不能避免全量重编」）

**结论：能，取决于探针写进 `.cpp` 还是 `.hpp/.h`。**

- 写 `.cpp`：护栏 `R3R_inc_guard.ps1` 只看 `src` 下的 `.h/.hpp` 与 `lang/*.txt`，`.cpp` 不在监控集合 ⇒ 判增量合法，ninja 只重编那一个 obj + 链接（约 1~2 分钟）。
- 写 `.hpp/.h`：旧护栏无条件判「头文件比最新 obj 新 ⇒ 混合 ABI ⇒ 删全部 620 个 obj」⇒ 全量 692 步（`-j2` 约 16~25 分钟）。本次 P3 必须在 `.hpp`（`PfDetectDestination` 定义在 `yapf_destrail.hpp`，noconsist 早退的上下文只在那里）。

**已做的改造（复用既有脚本，未新建）**：`R3R_inc_guard.ps1` 新增 `$smallClosure` 白名单（纯 ASCII），当前一条：

| 头文件 | 需要重编的 obj | 依据 |
|---|---|---|
| `src\pathfinder\yapf\yapf_destrail.hpp` | `yapf_rail.cpp.obj` | include 闭包核实全树只有 `src/pathfinder/yapf/yapf_rail.cpp:16`（`CMakeLists.txt` 只是列文件，不产生第二 TU） |

命中白名单时护栏只删该 obj 并 `exit 0`（打 `GUARD: small-closure header ... the rest stay valid`），其余 obj 全部保留。**维护纪律**：往白名单加条目必须先核 include 闭包（`rg -n 'include "<name>"' src`）；若日后有第二个 `.cpp` 开始包含同一头，必须在同一次改动里把该 obj 补进值列表，否则那个 obj 会用旧头链进新 exe（混合 ABI，KI-183 的坑）。

**本轮实际状况（重要）**：护栏在这次改造之前已经跑过一次——`build\R3R_incbuild.guard.log`（2026-10-27 15:12:07）记 `REMOVED 620 object file(s)`，随后构建被取消 ⇒ 当前 `build` 树 **0 个 obj**。所以此刻无论护栏怎么判，下一轮构建都必然是全量 692 步；白名单对**再下一次**（obj 重建之后）改 `.hpp` 才生效。

**后续取数原则**：能用 `.cpp` 表达的探针不写进头文件；同一批探针若必须落在头文件里，先确认闭包规模再决定是登记白名单还是接受全量。

**构建自证（2026-10-27，探针版 exe）**：

1. 第一次以前台 `cmd /c _tmp_inc_build.cmd` 起，跑到 `[383/692] … cheat_sl.cpp.obj` 时**被工具调用返回杀掉**（日志尾行 `ninja: build stopped: interrupted by user.`，`build\R3R_incbuild.done` 不存在）。⇒ 教训：本环境前台跑构建会被连带终止进程树，**构建一律走 `Start-Process -FilePath 'cmd.exe' -ArgumentList '/c','_tmp_inc_build.cmd' -WorkingDirectory … -WindowStyle Minimized` 后台独立进程**，否则白跑。
2. 后台续跑时护栏输出 `GUARD: incremental is safe (no header/lang file is newer than the newest object)`——**不是**白名单命中，而是第一次中断前已经把 `yapf_rail.cpp.obj` 重编出来（15:25:06 > `yapf_destrail.hpp` 15:10:53），该 TU 已按新头编译，判增量合法。
3. 结果：`[310/310] Linking CXX executable openttd.exe`（310 = 692 − 已完成的 382 步）、`build\R3R_incbuild.done` = `EXIT_CODE=0`、日志内 `error C|fatal error|FAILED:|build stopped` 计数 **0**、`build\openttd.exe` @ **2026-10-27 15:46:34**（52 319 232 B）晚于全部源码。
4. 产物自证 4/4 命中：`CPL-ORIGIN-TD`、`CPL-ORIGIN-FOLLOW`、`PFD-NOCONSIST`、`ptd=%d`（`findstr /M`，均返回 exe 路径）。
5. 小坑：本环境 shell 是 PowerShell，`findstr … >nul` 会被解析成 `Out-File nul` 报 `NotSupportedException`——二进制串自证一律用 `findstr /M /C:"…"`（只回显文件名，无乱码、无需重定向）。




### 27.6 复测判读（2026-10-27 15:52 日志）

**采样**：`D:\temp\codebuddy-dropped-files\e4f43b3f-…\R3R_debug.log`，233 454 B，mtime 15:52:47；产生者 `build\openttd.exe` @15:46:34（探针版）。命中 `CPL-ORIGIN-TD`×2、`CPL-ORIGIN-FOLLOW`×8、`PFD-NOCONSIST`×12、`ptd=`×2。

两次决策（原始行号来自 `findstr /N`）：

| tick | 起点 | spd | 结果 | 关键差异 |
|---|---|---|---|---|
| 12551 | `16,14` | 5 | `found=1` `CPL-BEST tile=5,15 td=0 cost=10242` | `nores=0` `acc=3/0` `last=5,15 why=acc-car` |
| 12627 | `15,14` | 30 | `found=0` | `nores=6` `acc=0/0` `last=-1,-1 why=none` |

#### 27.6.1 先纠一个探针自身的 bug：P1 的 `ok` 列全是假阴性

- `yapf_rail.cpp:845` 写的是 `TrackFollower fx(v, Yapf().GetCompatibleRailTypes())`。
- `GetCompatibleRailTypes()`（`yapf_destrail.hpp:118-121`）返回的 `compatible_railtypes` **只在 `SetDestination()`（`yapf_destrail.hpp:107-111`）里赋值**，而 `Yapf().SetDestination(v)` 在 `yapf_rail.cpp:900`——**晚于**探针所在的 `834-856`。
- ⇒ 探针时刻 railtypes 是空集 ⇒ `follow_track.hpp:358-364` 的 `!railtypes.Test(rail_type)` 恒真（`err=EC_RAIL_ROAD_TYPE`）⇒ **8/8 全 `ok=0` 与地图无关**，不能据此判定"无出口"。
- 下轮修法：改用 `v->compatible_railtypes`（挂接寻路不 `override_rail_type`，两者等价），并且**无论 ok 都打印 `new_tile`**，追加 `err=`（区分 `EC_RESERVED / EC_RAIL_ROAD_TYPE / EC_NO_WAY / EC_90DEG`）。
- 本轮替代判据：`nbits=`（`new_td_bits`，已按 exitdir 掩码）非零 ⇒ 该方向已进入下一格且可达方向非空；真无出口会得到 `nbits=0`（或 `EC_NO_WAY`）。

#### 27.6.2 判读表第 1 条：`15,14` 的 LOWER 出口**存在**（27-G 的地图前提成立）

- `1637 CPL-ORIGIN-TD veh=57 tile=15,14 tdb=0x2929 tbits=0x29`
  `tdb=0x2929` = td0(0x1)+td3(0x8)+td5(0x20)+td8(0x100)+td11(0x800)+td13(0x2000)；`tbits=0x29` = TRACK_X(0x1)+TRACK_LOWER(0x8)+TRACK_RIGHT(0x20) ⇒ `15,14` 是 X+LOWER+RIGHT 三轨分岔格。
- `1639 CPL-ORIGIN-FOLLOW veh=57 td=3 trk=3 ok=0 … nbits=0x14`：`td=3` 正是 **TRACK_LOWER**，`nbits=0x14`（= td2+td4）非空 ⇒ 进得去、下一格有可达方向。
- 与第一次决策的 `CPL-TRACE` 一致（`REACH cur=15,14 td=3 depth=1 trk=3 bits=0x29` → `REACH cur=15,15 td=2 depth=2 trk=2 bits=0x15`）。
- ⇒ **`15,14` 的 LOWER 出口在地图上存在且通向 `y=15`**，判读表第 1 条落在"存在"分支。

#### 27.6.3 判读表第 2 条（REG-330-B）结案：`tdb=0x101` 与 `bits=0x1` 只是格式差

- `1501 CPL-ORIGIN-TD veh=57 tile=16,14 tdb=0x101 tbits=0x1`：`0x101` = td0+td8 = 同一根 `TRACK_X` 的两个方向；`CPL-TRACE st=16,14 tdb=0x101` 之后是 `REACH cur=16,14 td=0 depth=0 trk=0 bits=0x1`。
- `TrackdirBitsToTrackBits(0x101) == 0x1` ⇒ 两处读数自洽，**不存在"REACH 只认一根轨、实际两根"的矛盾**。REG-330-B 关闭（不是换轨证据）。

#### 27.6.4 判读表第 3 条：第二次搜索树**没有**困在 `y=14` 行

- 第二次 `1644-1649 PFD-NOCONSIST`：`5,14 / 5,14 / 5,13 / 5,13 / 5,14 / 5,13`（`res=0`）。
- 第一次 `1505-1512`：`5,14 / 5,14 / 5,13 / 5,13 /` **`PFD-ACCEPT kind=car-only tile=5,15`** `/ 5,16`。
- 车底 `veh=26` 就在 `5,15`（`RESERVECONSIST veh=26 tile=5,15 … resNow=1` 全程存在）⇒ 两次搜索都推进到了 **x=5 列**（离车底只差一格），第二次甚至到 `5,13`。
- ⇒ **"第二次困在 y=14 行"不成立**；真实形态是"到了 x=5 却 accept 不到 `5,15`"。判读表第 3 条落在"问题在站台扫描/闸门，而不是能不能走到"一侧。

#### 27.6.5 判读表第 4 条（P2 的 `ptd`）：父子链只有 2 跳，与 `CPL-TRACE` 的逐格重放不是同一棵树

- `1518 CPL-BACK pPrev=5,15 td=0 parent=15,15 ptd=2`
- `1519 CPL-BACK pPrev=15,15 td=2 parent=16,14 ptd=0`
- 整个日志只有这 2 行 `CPL-BACK` ⇒ 最优节点的父链是 **`16,14` → `15,15` → `5,15`** 三个节点（`16,14` 是 origin，parent 为空，循环退出）。
- 而 `CPL-TRACE`/`REACH` 是逐格重放（`16,14` → `15,14` → `14,14` → … → `2,14` → `2,15` → `1,15`），两者**结构不同**。
- **疑点 REG-330-D（待核对，不据此下结论）**：`15,15` → `5,15` 跨 10 格却成父子，最可能是 `TrackFollower` 的"站台跳跃"（`follow_track.hpp` 的 `new_tile += step * (length-1)`）导致一步跨完整段站台 ⇒ `GetLastTile()` 就是跳跃后的格。若成立，则第一次的真实路径是 **`16,14` --X--> `15,15` 站台头 --站台跳跃--> `5,15`**，根本不经 `15,14`。需核对 `CPL-BACK` 的 `GetLastTile()`/`GetLastTrackdir()` 与 `reach()` 重放的语义差，再定 REG-330-B/D 的结论。

#### 27.6.6 本轮唯一真正的因果差异：`nores` 0 → 6（新疑点 REG-330-C）

- `1515 CPL-SCAN … nores=0 noconsist=6 consist=3 acc=3/0 … why=acc-car`（found=1）
- `1651 CPL-SCAN … nores=6 noconsist=6 consist=0 acc=0/0 … why=none`（found=0）
- `nores` = 因轨道已被预留而被 `MaskReservedTracks()` 屏蔽的节点数（`follow_track.hpp:179-207`）。第二次多出 6 个被 mask 的节点，且 `consist/acc` 全归零 ⇒ **第二次找不到车底的直接伴随是"预留 mask 把通往 `5,15` 的节点剪掉了"**，而不是"起点单一 td 不换轨"。
- 而 `5,15` 的预留位从第一次决策起就一直被占（`RESERVECONSIST veh=25/26 tile=5,15 … ok=0 resNow=1`，每 tick 多次）。E1b 已让 `rp()` 只读、不再订位（`yapf_rail.cpp:1046-1101` 注释自陈），所以这 6 个 mask **不是本次挂接寻路自己刚订的**，候选来源：①等待车底自己的 `ReserveTrackUnderConsist()`（KI-206/KI-127 机制）；②机车自己的 PBS 路径预留。
- **REG-330-C（猜测根因，待验证）**：`MaskReservedTracks()` 对"等待车底自己的预留"或"本车自己的预留"缺少自豁免，导致第二次挂接寻路剪掉通往目标的入口、`found=0`，进而 `best_track` 沿用陈旧值（`train_cmd.cpp:17437-17445`）。验证判据：在 `MaskReservedTracks` 命中处加只读探针打 `tile/td/reason/owner`，看这 6 个 mask 的归属是谁。

#### 27.6.7 本轮**没有**复现"机车停住不动"

- `1652 CPL-RET veh=57 tile=14,14 enterdir=0 tracks=0x11 res=0x0 ret=0x1 why=cpl-moving-keep spd=30`：第二次决策时 `spd=30`，走 `cpl-moving-keep` 分支，本次 `found=0` **不改变**机车行为（继续按上一次的 X 方向开）。
- 日志在 `tick=12627` 附近结束，机车正沿 `y=14` 从 `16,14` 一路向 x 递减（`CPL-RET` 的 `tile` 从 `15,14` 变 `14,14`），即**正在接近车底**。
- ⇒ 玩家报的"永远不动"需在"机车停稳后再决策"的时刻取数：`spd=0` 时 `why=` 不会是 `cpl-moving-keep`，而 `found=0` 会让 `best_track` 停在旧值（`why=nores-final` 之类）。下一轮复测请让机车停住（或解挂后重新起步）再抓日志。

#### 27.6.8 结论与下一步

| 判读项 | 结论 | 状态 |
|---|---|---|
| `15,14` 有无 LOWER 出口 | 有（`tdb` 含 td3，`nbits=0x14` 非空） | 已定死 |
| `tdb=0x101` vs `bits=0x1` | 同一根 X 轨的两方向，格式差 | REG-330-B 关闭 |
| 第二次是否困在 `y=14` | 否，已到 x=5 列，只差 `5,15` | 已定死 |
| `CPL-BACK` 的 `ptd` | 只有 2 跳，与逐格重放不同树；疑为站台跳跃 | REG-330-D 待核对 |
| 第二次 `found=0` 的成因 | `nores` 0→6（预留 mask 剪掉入口） | REG-330-C 待验证（新猜测根因） |
| 27-G（起点单一 td 不换轨） | 地图前提成立，但**不是**本轮 found=0 的直接原因 | 降级为"可能仍存在，非主因" |
| 探针 `ok`/`nt` 列 | 假阴性（railtypes 未初始化） | 探针 bug，下轮修 |

**下轮探针（仍未改代码）**：①P1 换 `v->compatible_railtypes` + 无论 ok 都打 `new_tile` + 打 `err=`；②在 `MaskReservedTracks()` 命中处打 `MASK-RESV tile/td/owner/reason`；③`CPL-BACK` 补 `skip=`（`pPrev->GetLastTile()` 与父节点之间的距离）以确认站台跳跃。

### 27.7 机车 veh=57 从配对成功到日志截断的速度曲线（同一份 15:52 日志）

数据源：`CPLSPD veh=57`（48 条，`n=1..48`，末条 tick=12520）、`LOCO-MOVE veh=57`（7 条，无 tick 字段）、`SPD-DROP veh=57`（2 条）。速度单位即 `cur_speed`（内部 1/2 单位）。

| tick | spd | strict | gap | 备注 |
|---|---|---|---|---|
| 11996 | 0 | 5 | -1 | 配对前，格 `16,14`（`595`） |
| 12428 | 2 | 5 | -1 | **`CPL-PAIR tick=12428 act=57 tgt=0 dist=5 actTile=16,14 tgtTile=12,15`（`1355`）配对成功** |
| 12429 | 3 | 5 | -1 | |
| 12430 | 4 | 5 | -1 | |
| 12430 | 5 | 5 | -1 | 跑到 5 后维持 |
| 12461 | **0** | **31** | **4** | **`SPD-DROP veh=57 prev=5 order=16 real=1 track=01 dtile=1932 dt=33`（`1419`）**：同一帧 order 切到 `OT_GOTO_COUPLE`、`db 0→1`、`strict 5→31` |
| 12461 | 2 | 31 | 4 | |
| 12462 | 3 | 31 | 4 | |
| 12463 | 4 | 31 | 4 | |
| 12463 | 5 | 31 | 4 | |
| 12464 | 6 | 31 | 4 | |
| 12464→12484 | 6→17 | 31→15 | 4→11 | 单调爬升（`n=11..37`，中间明细未逐条取；全日志 `SPD-DROP veh=57` 仅 2 条，段内无二次归零） |
| 12484 | 17 | 15 | 11 | 峰值，开始收油（`1452`） |
| 12485 | 15 | 15 | 11 | |
| 12491 | 15 | **5** | **0** | strict 由 15 回 5，`gap=0`（到点判定边界） |
| 12492 | 13 | 5 | 0 | |
| 12492 | 11 | 5 | 0 | |
| 12493 | 9 | 5 | 0 | |
| 12493 | 8 | 5 | 0 | |
| 12494 | 7 | 5 | 0 | |
| 12494 | 6 | 5 | 0 | |
| 12495 | 5 | 5 | 0 | 刹到 5 |
| 12520 | 5 | 5 | -1 | **最后一条 `CPLSPD`（`n=48`，`1463`），此后该探针不再产出** |
| ≥12549 | **30** | — | — | 只有 `LOCO-MOVE`：`15,14 j=210 spd=30`（`1593`）、`15,14 j=200 spd=30`（`1629`）、`14,14 j=196`/`j=198 spd=30`（`1657`/`1691`）、`13,14 j=204 spd=30`（`1707`），全程 `track=01 dir=5 db=1` |

**判读**：
1. 曲线形态是「起步 2→5（tick 12428-12430）→ 强制归零掉头（12461）→ 0→17 爬升（12461-12484）→ 17→5 制动（12485-12495）→ 5 平台（12495-12520）→ 30 巡航（12520 之后）」，**没有任何"卡死/速度恒 0"的段落**，与本轮结论"未复现停住不动"一致。
2. **疑点 REG-330-E（已登记，待验证）**：`12461` 的归零不是刹车，而是**订单切换 + 掉头**的副作用——同一帧 `order 9→16(OT_GOTO_COUPLE)`、`db 0→1`、`strict 5→31`（限速被整体放开）、`gap -1→4`。即配对成功（12428）到真正进入 `GOTO_COUPLE` 阶段之间隔了 33 tick（`dt=33`），期间虽已在 `16,14` 却先按旧订单跑。需核对 `train_cmd.cpp` 中 `ReverseTrainDirection`/订单推进是否在同一帧内清 `cur_speed`，以及 `strict` 31 的来源。
3. **疑点 REG-330-F（已登记，待验证）**：`gap` 走 `+4 → +11 → 0 → -1`，即**先冲过头再制动**（`gap` 从 11 归 0 时反而把 `strict` 从 15 压到 5，`gap` 变 -1 后仍以 5 保持 25 tick）；随后 12520 之后又以 30 全速朝 `tgt=12,15` 方向（`16,14→13,14`）前进。需核对 `gap` 的定义（到挂接点的剩余距离？）与 `strict` 的来源（`aend/aitem/adv` 三者同值，疑为 `UpdateSpeed` 的限速输入）。
4. 采样缺口：`CPLSPD` 在 `n=48`（tick 12520）后停止输出，`12520→≥12549` 的 5→30 过程无逐帧数据，**不能据此断言"速度突跳"**；`LOCO-MOVE` 的 `j=` 值（193/210/200/196/198/204）不是 tick 差，同一格出现两条说明其触发条件不是"跨格"。

### 27.8 玩家肉眼观察（2026-10-27）与日志对照

**玩家原话**：「列车启动后，以正常加速度曲线加速又减速到这一格另一端（0-曲线上-曲线下-15-5），随后出格走错岔道，速度立即变为 30 平台。」

**逐句对照（日志侧全部吻合，无一处冲突）**：

| 玩家描述 | 日志锚点 | 判定 |
|---|---|---|
| 「0」 | `595: CPLSPD veh=57 tick=11996 spd=0` | 吻合（配对前静止） |
| 「曲线上」 | `1357-1360: tick=12428 spd=2/3/4/5`；`1420-1425: tick=12461 spd=0/2/3/4/5/6` | 吻合（两次起步；第二次一直爬到 `1452: tick=12484 spd=17`） |
| 「曲线下」「15」 | `1453: tick=12485 spd=15` → `1455-1461: spd=13/11/9/8/7/6/5` | 吻合（峰值 17 后 11 tick 刹到 5） |
| 「5」 | `1463: tick=12520 spd=5`（`CPLSPD` 最后一条，`n=48`） | 吻合 |
| 「出格走错岔道」 | `LOCO-MOVE` 六条：`16,14 j=194 spd=5`（`1497`）→ `15,14 j=210 spd=30`（`1593`）→ `15,14 j=200 spd=30`（`1629`）→ `14,14 j=196`（`1657`）→ `14,14 j=198`（`1691`）→ `13,14 j=204`（`1707`）；**y 恒为 14、x 从 16 递减** | 吻合：机车出的格是 `16,14`，出去后沿 `y=14` 向西直行，而配对目标 `tgtTile=12,15`（`1355`）在 `y=15` ⇒ **未走 LOWER 出口（`td=3`，即 `16,14→15,15` 方向），走了 X 出口（`16,14→15,14`）** |
| 「速度立即变为 30 平台」 | 出格后**第一条** `LOCO-MOVE` 的 `spd` 已是 `30`，其后 5 条全 `30`；`CPLSPD` 在 `tick=12520` 后停止输出 | 平台成立（30 恒定）；**「立即」未证实**——5→30 的中间过程位于采样缺口内，无法判定是渐变还是突跳 |

**由此新增两条疑点**（按规则登记，勿与 REG-330-C/D 混）：

- **REG-330-G（现象，待定位）「出格后走错岔道」**：`16,14` 有三类出口可用——X（`td=0/6`，去 `15,14`）、LOWER（`td=3`，去 `15,15`）、RIGHT（`td=6`？见 `1637: tdb=0x2929`）。日志显示实际走了 X，而目标 `12,15` 需先下到 `y=15`。**与 REG-330-C（`nores=6` 剪掉入口）可能是同一根因**，但要在 `ChooseTrainTrack`/`TrackFollower` 出口处取 `why=` 才能定死。待验证判据：出格那一 tick 的 `CPL-*` 或 `TRP-*` 行里 `chosen_td=0/6`（X）还是 `3`（LOWER）。
- **REG-330-H（猜测根因，待验证）「5→30 的速度来源」**：`CPLSPD` 在 `12520` 之后不再输出，`strict` 最后一次读数是 `5`（`12495-12520`），但出格后立刻以 `30` 巡航。两种可能：(a) 机车进入了另一条限速 `≥30` 的轨道区段（`strict` 由 5 放开到 31 级），速度按加速度渐变上去，只是没有采样；(b) 某处直接把 `cur_speed` 置成限速值（`TrainController` 之外的路径）。**若为 (b) 则属新缺陷**（不走加速度曲线＝违反玩家观测的"正常"预期）。待验证判据：在 `TrainController` 的限速出口与 `cur_speed` 写入点加只读探针，或在 `LOCO-MOVE` 行补 `tick=` 让跨格间隔可算（一格 16px，`spd=5` 约 6 tick/格、`spd=30` 约 1 tick/格，**若两格间隔 ≤3 tick 即可证实是跳变而非渐变**）。

**追加取证（同一份日志，`CPL-ENTRY/CPL-ORIGIN-TD/CPL-BEST/CPL-RET` 全量 8 行，共 2 次决策）**：

```
1499: CPL-ENTRY      veh=57 tile=16,14 orderType=16 dontReserve=0
1501: CPL-ORIGIN-TD  veh=57 tile=16,14 tdb=0x101 tbits=0x1      ← 只有 X 轨（td0/td8）
1517: CPL-BEST       veh=57 tile=5,15  td=0 cost=10242          ← 最佳终点是 5,15，不是 12,15
1571: CPL-RET        veh=57 tile=15,14 enterdir=0 tracks=0x9 res=0x0 ret=0x1 why=nores-final spd=5 real=1
1635: CPL-ENTRY      veh=57 tile=15,14 orderType=16 dontReserve=0
1637: CPL-ORIGIN-TD  veh=57 tile=15,14 tdb=0x2929 tbits=0x29     ← X+LOWER+RIGHT 三向
1652: CPL-RET        veh=57 tile=14,14 enterdir=0 tracks=0x11 res=0x0 ret=0x1 why=cpl-moving-keep spd=30 real=1
```

**由此修正 REG-330-G 的形态判定（重要）**：`16,14` 的 `tbits=0x1` 说明该格**物理上只有 X 向轨道，压根没有 LOWER 出口** ⇒ 「走错岔道」这个说法在 `16,14` 不成立，往 `15,14` 是**唯一可走方向**。真正的偏差有两处，且都在更上层：

- **REG-330-G′（现象，成立）**：`CPL-BEST` 选中的终点是 **`5,15`**（`1517`），而 `CPL-PAIR` 打印的配对目标是 **`12,15`**（`1355`）。两者相距 7 格且不在同一行 ⇒ 机车沿 `y=14` 一路向西找的是 `5,15` 那个 car-only 车底（`PFD-ACCEPT kind=car-only tile=5,15`、`RESERVECONSIST veh=26`），**不是配对的 `veh=0@12,15`**。所以"走错路"的实质是**寻路终点与配对对象不是同一个东西**，需核对 `GetCoupleSlot` / `R3RIsCoupleTarget` 在 `tgt=0` 时为何把 `5,15` 判成可挂对象。
- **REG-330-G″（现象，成立）**：`15,14` 有 `tbits=0x29`（X+LOWER+RIGHT）三向出口，第一次决策在此格以 `why=nores-final` 收尾（`1571`），第二次决策从这里继续以 `why=cpl-moving-keep` 走到 `14,14`（`1652`）——**始终没走 LOWER（td=3，去 `15,15`）**，而 `15,15` 才是通往 `tgtTile=12,15` 方向的下一行。与 REG-330-C（`nores=6` 剪枝）指向同一处。

### 27.9 探针落地（2026-10-27 16:14 exe）：`CPL-RET` / `LOCO-MOVE` 补 `tick=`

**动机**：§27.8 的 REG-330-H 卡在「5→30 是渐变还是硬置」上，唯一缺的是时间轴——`CPL-RET`/`CPL-ENTRY`/`CPL-BEST` 三行原本都没有 `tick=`，`LOCO-MOVE` 也没有，只能靠行号相邻猜间隔。

**改动（仅 `src\train_cmd.cpp`，未碰任何 `src\*.h`、未改语言串、零行为变更）**：

1. `ChooseTrainTrack()` 内的 `r3r_chosen_ret` lambda（`CPL-RET` 唯一出口探针）：格式串 `CPL-RET veh=%d tile=...` 改成 `CPL-RET veh=%d tick=%u tile=...`，参数表插入 `(uint)_tick_counter`。**去重签名 `crtr_sig` 不动**（仍由 `ret_bits/res/enterdir/tracks` 组成）⇒ 同一决策状态不会因为 tick 变化而重复打行。
2. 移动分支（`TrainLocoHandler` 的 else 支，`LOCO-MOVE` 唯一产出点）：格式串加 `tick=%u`，参数插 `(uint)_tick_counter`。**边沿去重键不动**（`R3REDGE_LOCOMOVE` + `(veh, tile)`）⇒ 仍是一格一行。

`_tick_counter` 声明在 `src\date_func.h:16`（`extern uint64_t _tick_counter;`），`train_cmd.cpp:74` 已 include，本文件内已有 56 处用法，无需新增头文件。

**构建自证**：

| 项 | 读数 |
|---|---|
| 构建入口 | `_tmp_inc_build.cmd`（复用，未新建 `.cmd`） |
| 护栏 | incremental is safe（只改 `.cpp`） |
| 步骤 | `[2/3] Building CXX object ...\src\train_cmd.cpp.obj` → `[3/3] Linking CXX executable openttd.exe` |
| `build\R3R_incbuild.done` | `EXIT_CODE=0`（16:14:48） |
| 时间链 | `src\train_cmd.cpp` 16:12:49 → `train_cmd.cpp.obj` 16:13:49 → `build\openttd.exe` 16:14:40 |
| exe | 49.9 MB，命中 `CPL-RET veh=%d tick=%u`、`LOCO-MOVE veh=%d tick=%u` |
| `read_lints` | 0 |

**复测判读（拿到新日志后按此读，不用再猜）**：

1. `CPL-RET` 的 `tick` 应能把两次决策钉在同一时间轴上：`15,14` 那条 `why=nores-final spd=5` 与 `14,14` 那条 `why=cpl-moving-keep spd=30` 的 tick 差，就是「速度放开到 30」所花的真实时长。
2. `LOCO-MOVE` 逐格 tick 差直接给出每格耗时：`16,14→15,14` 若 **≤3 tick** ⇒ 速度在出格瞬间已接近 30（跳变）；若 **≥6 tick** ⇒ 出格时仍是 5 的速度，30 是在下一格之内爬上去的（渐变）。
3. 反向校验：`spd=30` 那几格的 tick 差应接近 1（30 的内部单位 = 约 15px/tick，16px 一格）；若实测仍是大间隔，说明 `spd` 字段与真实位移不同步，需另查 `cur_speed` 的写入点。
4. 若 `CPL-RET` 在 `tick=12520` 之后**一条都不出现**，则说明整段「30 巡航」期间 `ChooseTrainTrack` 的岔道格分支没被走到（`KillFirstBit(tracks)==TRACK_BIT_NONE`），与 REG-330-G″ 的「始终没走 LOWER」互为印证。

### 27.10 新日志判读（tick 探针首测，2026-10-27）

**输入**：`D:\temp\codebuddy-dropped-files\521dd73c-5220-4936-973c-31b341c4690a\R3R_debug.log`，227 544 B / 1631 行；产生者必为本轮新编的探针版（两条探针都带 `tick=`，与 §27.9 的构建产物一致）。

#### 27.10.1 时间轴（全部带日志行号）

| 日志行 | tick | 标签 | 读数 |
|---|---|---|---|
| 1383 | 12460 | `LOCO-MOVE` | `tile=16,14 j=194 spd=5 db=0` |
| 1463 | 12491 | `LOCO-MOVE` | `tile=16,14 j=194 spd=15 db=1` |
| 1543-1546 | ~12551 | `RP cur=` | `15,14 td=3` → `15,15 td=2` → `14,15 td=0` → `13,15 td=0`（**走 LOWER**） |
| 1548 | 12551 | `CPL-RET` | `tile=15,14 why=nores-final spd=5 real=1` |
| 1549 | 12551 | `RESV-WATCH` | `SET-RAIL tile=15,14 track=0 phase=controller`（**订的是 X**） |
| 1550 | 12556 | `CPL-PAIRMAP` | `role=target v=0 vTile=12,15 vOrd=17 vLen=107` |
| 1551 | 12556 | `CPL-PAIRMAP` | `role=requester v=57 vTile=16,14 vSpd=12` |
| 1582 | 12568 | `CPL-PAIRSCAN` | `actTile=16,14 actSpd=30` |
| 1584 | 12570 | `LOCO-MOVE` | `tile=16,14 j=212 spd=30 db=1` |
| 1600 | 12597 | `CPL-PAIRSCAN` | `actTile=15,14 actSpd=30` |
| 1603 | 12601 | `LOCO-MOVE` | `tile=15,14 j=210 spd=30` |
| 1611 | 12627 | `RESV-EXT-PRE` | `tile=14,14 tracks=0x11 reserved=0x0 spd=30` |
| 1614 | 12627 | `CPL-ORIGIN-TD` | `tile=15,14 tdb=0x2929 tbits=0x29` |
| 1615-1620 | 12627 | `CPL-ORIGIN-FOLLOW` | 6 个方向 **全部 `ok=0`**（`nbits=0x1001/0x14/0x14/0x100/0x100/0x1001`，`st=0/0/0/1/1/0`） |
| 1621-1626 | 12627 | `PFD-NOCONSIST` | `tile=5,14` / `5,13` 共 6 次 |
| 1627 | 12627 | `CPL-PATHFOUND` | `found=0` |
| 1628 | 12627 | `CPL-SCAN` | `found=0 closed=5 nodes=18 enter=13 noplat=1 nores=6 noconsist=6 consist=0` |
| 1629 | 12627 | `CPL-RET` | `tile=14,14 why=cpl-moving-keep spd=30 real=1` |

#### 27.10.2 判定 1：5→30 是**加速渐变**，不是硬置 ⇒ **REG-330-H 关闭（证伪「硬置」分支）**

`12551 spd=5` → `12556 spd=12` → `12568 spd=30`：**17 tick 内 5→12→30**，与 `LINEND veh=54`（`1596-1610`：58→56→54→51→49→46→44→41→38→35→31→27，每 tick 降 2~3）的量级一致，属正常加减速曲线，**不是**把 `cur_speed` 直接写成限速值。⇒ 玩家「立即变为 30」是**观感**（17 tick ≈ 0.3 秒），不是代码级的硬置。

#### 27.10.3 判定 2：`LOCO-MOVE` 的 tick 差**不能**当每格耗时（§27.9 判据 2 作废）

实测三次出行的 tick 差：`12460→12491`=31、`12491→12570`=79、`12570→12601`=31。而 `spd=30` 时一格（16 px）应约 1 tick ⇒ 79/31 都远大于物理耗时。原因是 `LOCO-MOVE` 走 `R3RDbgEdge(R3REDGE_LOCOMOVE, ...)`，**该闸门按 128 tick 窗口抑制**，只有窗口打开的那一 tick 才会真正写字。⇒ 这条探针只能证明「某 tick 车在哪格、速度多少」，**不能算格间隔**；要量耗时只能看 `CPL-PAIRSCAN`（每约 29 tick 一条、不受抑制）或另加不带抑制的探针。

#### 27.10.4 判定 3：挂接寻路走 LOWER，运动侧订/走 X（REG-330-G″ 由「没走 LOWER」升级为「两套路径分歧」）

- 挂接寻路（`RP cur=`）回溯路径明确是 LOWER：`15,14 td=3` → `15,15` → `14,15` → `13,15` → … → `1,15`，与 `tgtTile=12,15`（y=15 行）一致；
- 但同一 tick 的实际预留是 `RESV-WATCH SET-RAIL tile=15,14 track=0`（**TRACK_X**，即向西去 `14,14`），运动侧 `12601`、`12627` 也确实是 `15,14`→`14,14`；
- ⇒ **挂接寻路算出的路径与列车实际走的路径不是同一条**，且分歧恰好发生在 `15,14` 这个三向格（`tbits=0x29` = X+LOWER+RIGHT）。这与第 93 轮 KI-163（`rp()` 与运动侧不一致）是同族形态，但本次是「`rp()` 要 LOWER、`controller` 订 X」，方向相反。

#### 27.10.5 判定 4：`CPL-RET` 的 `tile=` 是**决策格/预留起点**，不是车所在格（读法修正，重要）

`1548 CPL-RET tile=15,14` 与 `1551 CPL-PAIRMAP vTile=16,14`（同 tick 附近）并存，且 `1584 LOCO-MOVE tile=16,14` 证明车此时仍在 `16,14`。⇒ `CPL-RET` 的 `tile` 来自 `ChooseTrainTrack()` 的入参（下一格/预留起点），**此前把它当车所在格读过的地方（含 §27.8 的对照表）需按此修正**；要判车的位置只认 `LOCO-MOVE` / `CPL-PAIRMAP` / `CPL-PAIRSCAN` 的 `actTile`。

#### 27.10.6 新疑点 REG-330-I（待验证）：`CPL-ORIGIN-FOLLOW` 六方向全 `ok=0`

`1615-1620` 对 `15,14` 的 6 个 trackdir 逐一试跟随，**无一 `ok=1`**，`nt=-1,-1`（连下一格都没算出来）。后续 `PFD-NOCONSIST×6`、`CPL-PATHFOUND found=0`、`CPL-SCAN nores=6 noconsist=6` 全部由此派生。两种可能：
(a) 该格的下一格全被别的车/预留占死（`res=0` 但可能有车）；
(b) `TrackFollower` 从车头当前位置出发判定，而车头此刻正处于格边界/曲线中，`ok=0` 是「几何上还没对上」的正常形态，真正的拒绝在更后面。
**判据**：`CPL-ORIGIN-FOLLOW` 需补 `next tile` 的占用信息（或 `PFD-*` 的拒绝原因分类），否则 `ok=0` 无法归因。

### 27.11 玩家口径（2026-10-27）：`15,15` 当时被「刚解挂下来的另一台机车」占用 ⇒ 改判 §27.10.4

**玩家原话**：「你说（15，15）当时为什么不视作合法目标吗，哦，那是当时另一个机车刚刚解挂下来占用了路径」

**这条口径直接推翻 §27.10.4 的「两套路径分歧 = bug」判定**：

| 项 | §27.10.4 原判 | 按玩家口径改判 |
|---|---|---|
| `15,15` 不可进 | 判据出问题（`CPL-ORIGIN-FOLLOW` 全 `ok=0` 归因不明） | **正确**：格上有车（刚解挂的另一台机车）⇒ 车占用检查生效 |
| `RP` 要 LOWER 而 `RESV-WATCH` 订 X | 寻路与控制器「各走一条路」= 结构性缺陷 | **不是分歧**：LOWER 被堵 ⇒ 控制器只能订 X，是被堵后的替代路径 |
| `CPL-PATHFOUND found=0` | 寻路整体失败 | 仍成立，但**失败原因是合理的**（目标方向被占） |

**缺陷因此收窄为一条（新编号 REG-330-J，待验证）**：

> 寻路失败（`found=0`）后**没有任何「原地等待 / 稍后重试」机制**，列车直接落回普通运动逻辑，沿 X 一路向西（`15,14`→`14,14`→…）**永久离开车底所在方向**；而 LOWER 那台占路机车只要开走，路径本来就通了。

这条同时把 REG-330-G″ 的「始终没走 LOWER」解释干净：**不是判据不肯走，是当时走不了，而且错过之后不会再来一次**。

**待验证判据（REG-330-J）**：

1. 日志里应存在「解挂 → 占用 `15,15`」的痕迹：`DECOUPLE-DONE` 的 `tx/ty` 落在 `15,15` 或其邻格，或 `GEO settle-decouple-u` 段内出现 `15,15`；
2. 该解挂的时间戳应**早于** `tick=12551`（`CPL-RET why=nores-final`）；
3. 若该占路机车在 `tick>12627` 之后离开，则「等待即可通过」成立 ⇒ 修法方向=失败后保号重试/短暂停车，而非改寻路判据。

### 27.12 撤回 §27.10.4「两套路径分歧」，钉死正确路径与失败阶段

**（一）我自己读错了，判定 3 撤回**

§27.10.1 表里把 `RP cur=15,14 td=3` 与 `RESV-WATCH tile=15,14 track=0` 当成「寻路要 LOWER / 控制器订 X」两条路。实测两行把话说清了：

| 日志行 | 原文 | 含义 |
|---|---|---|
| 1519 | `REACH cur=15,15 td=2 depth=2 trk=2 bits=0x15` | **15,15** 这格 bits=0x15（X\|UPPER\|LOWER），搜索在第 2 层到达它 |
| 1544 | `RP cur=15,15 td=2 depth=2 trk=2 bits=0x15` | 预留函数在 **15,15** 订的是 LOWER(trk=2) |
| 1549 | `RESV-WATCH SET-RAIL tile=15,14 track=0` | 在 **15,14** 订的是 X(track=0) |

⇒ 订 X 的是 `15,14` 这一格、订 LOWER 的是 `15,15` 那一格，是**同一路径上相邻两格各自订自己那段轨道**，不是两条路径。**「两套路径分歧」不成立，撤回。**

**（二）决定性反证：`15,14` 根本没有 LOWER 轨道位**

`1614 CPL-ORIGIN-TD tile=15,14 tdb=0x2929 tbits=0x29` —— `0x29` = X(1) | UPPER(8) | RIGHT(32)，**不含 LOWER(16)**。所以物理上 `15,14` 无法南下到 `15,15`，**它本来就不该出现在去 y=15 行的路径上**。

**（三）正确路径（按轨道位推出来）**

`16,14` 走 **LOWER** → `16,15`（`15,14` 的 tbits 无 LOWER，所以只能从 `16,14` 南下）→ 沿 X 西行 `16,15` → `15,15` → `14,15` → … → `5,15`（`GEO settle-decouple-u n=26 veh=26 tile=5,15 starB=1` 是车底链尾 `5,15`）。

**（四）机车实际走的**：`16,14` → `15,14` → `14,14`（X 西行，走 y=14 行）。

⇒ REG-330-G″ 成立且表述更精确：**该下没下（`16,14` 的 LOWER），反而沿 X 走到了 y=14 行**。

**（五）与玩家口径完全兼容，且把失败阶段钉死了**

玩家说 `15,15` 被刚解挂的机车占用。两个读数因此不再打架：

| 阶段 | 读数 | 结论 |
|---|---|---|
| 搜索（YAPF） | `REACH`/`RP`/`CPL-BACK` 都在 `15,15` 有 depth=2 节点 | **搜索成功了**，路径到 `5,15` |
| 预留 | `1548 CPL-RET why=nores-final spd=5` | **预留失败**（`nores-final` = 最终没订上，不是没找到路） |

⇒ `nores-final` 与「`15,15` 被占」正好对上：寻路认为可行、真正下单被别的车占着。**所以这不是寻路 bug，是「预留失败后没有任何等待/重试，列车直接落回普通运动逻辑走 X 离开」**（= §27.11 的 REG-330-J）。

**（六）顺带修正 §27.10 的表述**：27.10.1 表里「RP 走 LOWER（15,14 td=3 → 15,15）」应读作「RP 在 15,15 订 LOWER」，`15,14 td=3` 是另一个节点行，两者不是一条链的两端。

### 27.13 关键纠正（2026-10-27）：`nores-final` 不是「预留失败」，是「本次调用不订路」

**代码事实（两行定死）**：

```17016:17616:src/train_cmd.cpp
bool do_track_reservation = _settings_game.pf.reserve_paths || (flags & CTTF_FORCE_RES);
...
/* No track reservation requested -> finished. */
if (!do_track_reservation) return r3r_chosen_ret(best_track, result_flags, "nores-final");
```

⇒ `nores-final` 的唯一含义是 **`do_track_reservation == false`**：这条调用**没有订路资格**，函数连预留都不尝试，直接返回选出的 `best_track`。

**因此 §27.12 判定（五）撤回**：`1548 CPL-RET why=nores-final` 不能读作「下单被别的车挡住」。它跟「`15,15` 被刚解挂的机车占用」**没有因果关系**——那一路压根没预订。

玩家口径仍然成立，但作用点必须改：**「`15,15` 被占」影响的是挂接寻路本身**（`1627 CPL-PATHFOUND found=0`、`1628 CPL-SCAN nores=6 noconsist=6`，tick=12627），不是预留。

**待查 P-1**：该存档 `pf.reserve_paths` 是 on 还是 off。判据=`RESV-EXT-PRE ... doRes=%d`（17065-17070 新字段）；本次日志那一行是**旧格式无 doRes** ⇒ 产生者 exe 早于该探针，需新日志才能定。

### 27.14 short-stuck 的真实效果（代码锚点，供拍板）

1. `MarkTrainAsStuck`（`src/train_cmd.cpp` 约 4750 起）：**立即 `cur_speed = 0`、`subspeed = 0`**（硬停，不是减速）+ 置 `VehicleRailFlag::Stuck` / `WaitingRestriction` + `wait_counter = 0`；
2. UI：车辆窗口启停控件变「等待空余轨道」——语义正确，玩家能看出是"在等"而不是"坏了"；
3. `TrainLocoHandler` 的 stuck 分支（约 22150）：**每 `path_backoff_interval` tick 才重试一次 `TryPathReserve`**，其余 tick 直接 `return`（不寻路、不推进订单）；
4. **副作用 A（最大风险）**：`wait_counter % (wait_for_pbs_path * DAY_TICKS) == 0 && reverse_at_signals` ⇒ 等满 `wait_for_pbs_path` 天会**掉头**。挂接场景掉头 = 彻底跑反方向，比现在走 X 更糟；
5. **副作用 B**：stuck 期间订单不推进、不重算；占路车永不离开就永久停（但"停着"仍优于"跑错方向"）；
6. 恢复：任一次成功预留会 `Reset(wait_counter)` 并清 `Stuck`。

⇒ 裸用 `MarkTrainAsStuck` 不可接受（副作用 A），正确形态是「**有限次原地等 + 定期重跑挂接寻路 + 抑制掉头**」，与 KI-289 已有的 `r3r_heals` 抑制掉头同路。

### 27.15 玩家拍板（2026-10-27）：按 short-stuck 修，原地保号等 N tick 再重试

**玩家原话**：「那就按你说的：'预留失败时按 short-stuck 处理，即原地保号、等待 N tick 再重试，而不是落回普通运动逻辑'来修吧」

**拍板内容（口径，不再变动）**：

1. 触发条件 = **预留失败**（下单没订上），**不得**落回普通运动逻辑直接驶离；
2. 处理方式 = **原地停住 + 保号 + 定时重试**，重试成功即恢复行驶；
3. **不得改 YAPF 判据**：`15,15` 有车就是不合法，判据正确；
4. 明确排除：不得走「等满 `wait_for_pbs_path` 天就掉头」那条既有分支（§27.14 副作用 A）。

**落盘时必须先解决的冲突（P-2，见 §27.13）**：

`nores-final`（`src/train_cmd.cpp:17616`）的出口条件是 `if (!do_track_reservation)`，而
`do_track_reservation = _settings_game.pf.reserve_paths || (flags & CTTF_FORCE_RES)`（`:17016`）。

⇒ 若 `pf.reserve_paths == false`，则 `nores-final` **不是预留失败**，而是「本次调用没有订路资格」，此时它返回 `best_track` 属正常上游行为，**不能在这里做 short-stuck**（否则全线普通寻路都会被拦成停车）。

⇒ 因此修法的**前置动作**：必须先确认 `1548 CPL-RET why=nores-final` 那次调用的 `do_track_reservation` 取值；若为 false，则真正失配的地方在**调用侧**（传了不带 `CTTF_FORCE_RES` 的 flags），修法应落在调用侧而不是 `17616`。

### 27.16 构建自证（第 330 轮，2026-10-27）

复用既有 `_tmp_inc_build.cmd`（未新建任何 `.cmd`）。第一次因 `openttd.exe` 正在运行（PID 7572）被闸门拦下，`build\R3R_incbuild.done` = `EXIT_CODE=97`，且该情形**只写 done 不写 log**（log 里是上一轮的 `[3/3] Linking` 旧内容），极易误判为"已构建"——本次通过时间戳识别：`train_cmd.cpp` 16:59:23 新于 `train_cmd.cpp.obj` 16:13:49。玩家关闭游戏后重跑：

| 判据 | 实测 |
|---|---|
| 完成标记 | `build\R3R_incbuild.done` = `EXIT_CODE=0` |
| 日志尾部 | `[2/3] Building CXX object ...train_cmd.cpp.obj` → `[3/3] Linking CXX executable openttd.exe` |
| 时间戳链 | `src\train_cmd.cpp` 16:59:23 → `train_cmd.cpp.obj` 17:13:23 → `build\openttd.exe` 17:14:38 |
| obj 体积 | 15 005 916 B → 15 123 720 B（确认重编，非陈旧 obj） |
| 产物自证 | exe 内 `findstr` 命中 `CPL-NOOWNRESV` / `cpl-moving-keep` / `reason=no-own-reservation` |
| `read_lints` | 0 条 |

**状态 = 已实现 + 已编译，游戏内复测待做**。复测判据见 §27.15 与 KI 条目（4 条）：①同场景出现 `CPL-NOOWNRESV` 且机车停在原格不再西行；②占路车离开后自动恢复并挂上；③`REVERSEDIR ... stuck=1` 不超过 KI-289 允许的两次；④其它 GOTO_COUPLE 正常行驶场景不出现 `CPL-NOOWNRESV`。

### 27.17 第一次实机复测：接线失败，判据恒真（2026-10-27）

**玩家原话**：「机车还是走错道岔，说明这个short stuck相关的接线有问题」——**成立，且已做实到具体一行**。

**采样有效性**：`build\R3R_debug.log` 1 710 行 / 233 846 B / 17:18:27，产生者 `build\openttd.exe` 52 319 232 B / 17:14:38 = 第 330 轮 E1i 版 ⇒ 是新代码产生的日志。

**四条现场锚点（逐字）**：

| 行 | 内容 |
|---|---|
| `:1622` | `CPL-PATHFOUND veh=57 found=0` ← **挂接寻路本身失败** |
| `:1610`~`:1615` | `CPL-ORIGIN-FOLLOW veh=57 td=0/3/5/8/11/13 ... ok=0 nt=-1,-1` ← 六个方向**全部跟不出去** |
| `:1606` | `RESV-EXT-PRE veh=57 tile=14,14 enterdir=0 tracks=0x11 reserved=0x0 doRes=0 real=1 spd=30` ← 该格 `reserved=0x0`，**没有预留** |
| `:1624` | `CPL-RET veh=57 tick=12627 tile=14,14 enterdir=0 tracks=0x11 res=0x0 ret=0x1 why=cpl-moving-keep spd=30 real=1` ← 仍然走 `cpl-moving-keep`，交回 `ret=0x1`（TRACK_X，继续沿 y=14 西行） |
| 全日志 | `CPL-NOOWNRESV` **0 条** |

`LOCO-MOVE` 轨迹：`:1579 tile=16,14` → `:1598 tile=15,14` → `:1662 tile=14,14`，`dtile=1932`（=`(140,7)`，按 256 宽地图换算），全程 `track=01 X 方向`，即一路沿 y=14 向西，**始终没南下进 y=15 那一行**——与玩家「走错道岔」逐字吻合。

**根因（接线错误，一句话）**：我把「有没有自己的预留」判断成了 `FollowTrainReservation(...).tile != INVALID_TILE`，但**这个函数永远不会返回 `INVALID_TILE`**。

- `src\pbs.h:229` 自己写明：`OkayUnused, ///< 'okay' return value is not used`——它只影响 `res.okay`，不影响 `res.tile`；
- `src\pbs.cpp:1392`：车库无预留也照样 `return PBSTileInfo(tile, trackdir, false)`；
- `src\pbs.cpp:1395`：`FollowReservation(...)` 从 `moving_front->tile` 起跟随，**没有预留时就停在起点**，返回起点而非无效值。

⇒ `own_resv.tile != INVALID_TILE` **恒为真** ⇒ 我加的那道必要条件等于不存在，代码与修复前完全等价。这就是「接线有问题」的确切含义，也解释了为什么探针一条都打不出来（探针只在判据为假时才写）。

**修法方向（待落地，不再用 `FollowTrainReservation` 判有无预留）**：真正的病根不是「有预留没预留」，而是 `why=cpl-moving-keep` 这条早退**没有校验本次挂接寻路是否成功**。现场 `found=0` + 六个方向 `ok=0` 已经说明「没有任何可跟随的路」，此时把 `keep_track` 交回去必然是按 `best_track` 瞎开。正确判据应取二者之一（下一轮二选一并取证）：

- A：本次 `CPL-PATHFOUND` 的 `found` 为真（即挂接寻路确实找到路径）才允许 keep；
- B：用 `HasReservedTracks(front_tile, TrackToTrackBits(keep_track))` 之类**真正查地图预留位**的判据（`pbs.h` 里 `GetRailReservationTrackBits/HasReservedTracks` 才是地面真值）。

**待验证**：`bestDist` 逐帧 5→4→3（`:1577/:1595/:1661`）在递减，需确认它是「到目标距离」还是「到某候选距离」，若是前者则说明车并非走远而是走偏（差一行），不影响本判定。

### 27.18 修法落地 + 构建自证（第 330 轮续，2026-10-27）

**实际采用的判据与 §27.17 列的 A/B 都不同**，原因读码后才清楚：整段代码（`src\train_cmd.cpp:17446` 起）**本来就包在 `if (path_found == INVALID_TRACK)` 里**，走到这条早退时挂接寻路必然已经是 `found=0`，所以 §27.17 的选项 A（"本次 found 为真才 keep"）恒假、不可用；选项 B 要查地图预留位，需处理站台整格位与 `rail_map.h:168` 的 `IsPlainRailTile` 断言，风险大。

改用第三条：**跟随到的尽头是否真的比车头更远**。

```cpp
const Train *const r3r_own_front = consist->GetMovingFront();
const PBSTileInfo own_resv = FollowTrainReservation(consist, nullptr, FollowTrainReservationFlag::IgnoreLookahead);
const bool has_own_resv = r3r_own_front != nullptr && own_resv.tile != r3r_own_front->tile;
if (has_own_resv) return r3r_chosen_ret(keep_track, result_flags, "cpl-moving-keep");
```

- 无预留时 `FollowReservation` 停在起点 ⇒ `tile == 车头格` ⇒ 判"没有"；有预留则至少前进一格 ⇒ 不等 ⇒ 照旧 keep。
- 显式 `IgnoreLookahead`：真实制动模型（`TBM_REALISTIC`）下 `lookahead->reservation_end_tile` 是"计划终点"而非"已订下的路"，不忽略会让判据再次退化成恒真（与本次翻车的错误同源）。
- 探针补 `front=x,y end=x,y`：下次一眼就能看出 `front==end`（无预留的直接证据）。

**构建自证**（复用既有 `_tmp_inc_build.cmd`，未新建任何 `.cmd`）：

| 判据 | 实测 |
|---|---|
| 完成标记 | `build\R3R_incbuild.done` = `EXIT_CODE=0` |
| 日志尾部 | `[2/3] Building CXX object ...train_cmd.cpp.obj` → `[3/3] Linking CXX executable openttd.exe` |
| 时间戳链 | `src\train_cmd.cpp` 17:21:17 → `train_cmd.cpp.obj` 17:24:36（15 124 092 B）→ `build\openttd.exe` 17:26:32（52 322 304 B） |
| 产物自证 | exe 内 `findstr` 命中 `CPL-NOOWNRESV` / `reason=no-own-reservation` / `cpl-moving-keep` / `end=%d,%d` |
| `read_lints` | 0 条 |

**状态 = 已实现 + 已编译，游戏内复测待做**。

**复测判据（4 条，与 §27.15 的旧判据合并后以此为准）**：

1. 同场景出现 `CPL-NOOWNRESV ... front=14,14 end=14,14`（`front == end` 即"无预留"的直接证据），机车停在原格不再沿 y=14 西行；
2. 占路车离开后自动恢复并挂上（`COUPLE-OK`）；
3. `REVERSEDIR ... stuck=1` 不超过 KI-289 允许的两次；
4. 负判据：其它 `GOTO_COUPLE` 正常行驶场景**不出现** `CPL-NOOWNRESV`。

### 27.19 第二次实机复测：判据生效了，但暴露出更上游的两处失败（2026-10-27）

**采样有效性**：`D:\temp\...\R3R_debug.log` 1 884 行 / 248 845 B / 17:32:30，产生者 `build\openttd.exe` 52 322 304 B / 17:26:32 = 本轮新判据版 ⇒ 是**新代码**产生的日志。

**玩家两条观察的判读**：

1. 「还是走错道岔」——**已消失**。日志里 `CPL-RET ... why=cpl-fail`（上一版是 `why=cpl-moving-keep`），`CPL-NOOWNRESV` 仍 0 条，但那是**因为新判据直接放行到下一页**（`cpl-fail` 分支比 NOOWNRESV 更早），不是探针失效。轨迹停在 `tile=15,14 / mf=14,14`，不再沿 y=14 西行。
2. 「预留也不铺了，似乎没调用 trypathreserve」——**不成立**。日志明确有 `:1702 TRP veh=57 mf=14,14 real=1 type=16 dest=1932 spd=0 ... => ok=0 res=0`、`:1870` 同形。`TRP` 就是 `TryPathReserve` 的出口探针，它被调用且**返回失败**。「预留不铺」是结果，不是没调用。

**现场锚点（逐字）**：

| 行 | 内容 | 含义 |
|---|---|---|
| `:1655` | `SPD-DROP veh=57 tile=15,14 prev=30 ... stuck=1` | 速度从 30 归零 |
| `:1656` | `STUCK veh=57 tile=15,14 state=1 stuck=1 spd=0 order=16` | 被 `MarkTrainAsStuck` |
| `:1680` | `TPR-WALK veh=57 mf=14,14 origin=14,14 td=0 okay=0 exit=0 new=13,14 reach=1` | `reachable=TRACK_BIT_X`，**不是** reach0 早退 |
| `:1681` | `TPR-EXIT veh=57 state=choose pair=0 other=57 otherTile=15,14 ...` | `other` 是**自己**，被 `18474` 的 `!= consist->index` 正确跳过，未误判阻塞 |
| `:1682` | `RESV-EXT-PRE veh=57 tile=13,14 enterdir=0 tracks=0x1 reserved=0x0 doRes=1` | 下一格 13,14 **无人预留**、本车**有订路资格** |
| `:1700` | `UNRES-RAIL tile=13,14 track=0 phase=choose-track actor=57` | 13,14 的预留被撤（KI-316 处置） |
| `:1702/:1870` | `TRP ... => ok=0 res=0` | 订路失败（`REACH` 走完 `ChooseTrainTrack` 仍无 `CTTRF_RESERVATION_MADE`） |
| `:1686`~`:1691` | `CPL-ORIGIN-FOLLOW veh=57 td=0/3/5/8/11/13 ok=0 nt=-1,-1 nbits=0x1001/0x14/0x14/0x100/0x100/0x1001` | **挂接寻路起点六向全跟不出去** |
| `:1698` | `CPL-PATHFOUND veh=57 found=0` | 必然是 0 |
| 全日志 | `TPR-BLOCKED` 0 条 | 不是「别的车占着我的路」 |

**两处未解失败（登记为猜测根因，待验证）**：

- **G-1（挂接寻路，与 PBS 改造无关）**：`CPL-ORIGIN-FOLLOW` 调的是 `TrackFollower::Follow()`（`src\pathfinder\yapf\yapf_rail.cpp:845-846`），六个 trackdir 全返回 `false`，而 `nbits` 非零（下一格确有可用轨道）⇒ 拒绝发生在**跟随器内部**（rail type 兼容性 / 90 度弯限制 / 自身占位），不是 YAPF 代价、也不是 PBS 预留。挂接寻路是**只读搜索**（`DoTrainCouplePathfind` → `yapf_destrail`），它本来就不订路，所以「挂接寻路没完成 PBS 改造」**不是**本次失败的原因；真正原因是它连第一步都走不出去。
- **G-2（普通订路）**：`13,14` 无预留、`doRes=1`、`reachable=0x1` 非空，`ChooseTrainTrack` 却返回无预留 ⇒ 失败点在 `ChooseTrainTrack` 内部（信号 / YAPF / 90 度 / 该格已被自身某节占用）。当前无该函数内部的出口探针，无法再细分。

**结论**：本轮判据把「走错道岔」换成了「原地卡住」，方向正确（符合玩家口径「预留失败按 short-stuck 处理」），但整条链的上游（挂接寻路 found=0、普通订路 ok=0）仍未通，所以表现为永不恢复。**下一步不是再改判据，而是把 G-1/G-2 坐实。**

**下一步取证计划（不需要玩家跑游戏即可做一半）**：

1. 读 `src\pathfinder\follow_track.hpp`，列出 `Follow()` 的每个 `return false`，逐条对照现场（重点：`compatible_railtypes` 与 `_settings_game.pf.forbid_90_deg`）；
2. 查 `yapf_rail.cpp` 里挂接寻路的 `Yapf().GetCompatibleRailTypes()` 初始化是否为空集；
3. 给 `ChooseTrainTrack` 加出口探针（`CTTRF_*` 各分支各一行），一轮即可定位 G-2；
4. 若 G-1/G-2 都与 90 度弯有关，则 `15,14` 那个 `X|LOWER|RIGHT` 三岔格的配置就是根，需要调整站场或放开限制。

### 27.20 撤回 §27.19 的「走错道岔已消失」——我判错了（2026-10-27）

**玩家当场纠正**：「我都跟你说了它走错道岔了，你怎么得出相反的结论的」。**玩家对，我错**。

**我的错误类型**：拿「一个探针字符串消失」替代了「现象是否消失」。我只确认 `why=cpl-moving-keep` 不再出现、尾部 `LOCO-MOVE` 停在 `15,14`，就宣布走错道岔已消除；**没有把 `LOCO-MOVE` 的整条轨迹拉出来看位移**。日志里机车确实沿 y=14 向西北走了两格：

| 行 | tick | tile | spd | 说明 |
|---|---|---|---|---|
| `:1367` | 12460 | `16,14` | 5 | 起点 |
| `:1435` | 12472 | `16,14` | 23 | 加速 |
| `:1491` | 12551 | `16,14` | 5 | 刹车到 5 |
| `:1565` | 12551 | — | — | **`CPL-RET ... ret=0x1 why=nores-final spd=5`** |
| `:1587` | 12601 | `15,14` | 30 | **跨格，重新加速到 30** |
| `:1627` | 12627 | `15,14` | 30 | 继续 |
| `:1646` | 12627 | `14,14` | 0 | **`why=cpl-fail` 才停车** |

车底在 y=15 那条线上（`RESERVECONSIST veh=0..26 tile=5,15..12,15`），车沿 y=14 一路西行、从未南下 ⇒ **这正是玩家说的走错道岔**。

**真凶不是我这轮改的判据，而是排在它前面的 `nores-final` 出口**：

- `:1565 why=nores-final spd=5 ret=0x1`：该出口语义是「本次调用**根本没有订路资格**」（`do_track_reservation == false`），它连预留都不尝试就把 `best_track` 交回去 ⇒ 列车按 best_track 自行选道；
- 放行之后 `:1587` 立刻 `spd=5 → 30` 并跨格，位移发生；
- 直到下一 tick `:1646` 才落到我新改的 `cpl-fail`（KI-316 撤预留 + `MarkTrainAsStuck`）把车刹住。

⇒ 我的判据**只拦住了第二次及以后**，第一次已经被 `nores-final` 放出去走了两格。**顺序错了**：拦截点必须前移到 `nores-final`。

**同时否掉 E1h 的旧结论**：E1h 判「`nores-final` 的条件是 `!do_track_reservation`，可能全模式正常、不是缺陷」。现场证明：在挂接等待场景下**存在 `doRes=0` 的调用**（否则打不出 `nores-final`），而它一旦发生就必然走错道岔。E1h 那条判断**作废**。

**改正后的结论**：
1. 走错道岔**仍然存在**，§27.19 第 1 条判读、KI-479 的「已生效/已消除」表述全部撤回；
2. 判据方向（不许落回普通运动逻辑）没错，但**落点错了**——要改的是 `nores-final` 出口，不是后面那条 keep 分支；
3. 下一步待查两点：**P-1** 该 `doRes=0` 调用来自哪个调用点（`TrainCheckIfLineEnds` 未带 `CTTF_FORCE_RES`？还是 `pf.reserve_paths=false`）；**P-2** 挂接等待中的机车是否应在 `doRes=0` 时直接原地等（与玩家 2026-10-27 口径一致：预留失败就 short-stuck，不落回普通运动）。

### 27.21 `pf.reserve_paths` 到底是什么设置（2026-10-27，玩家问「设置选项叫什么」）

**代码键名**：`pf.reserve_paths` / `_settings_game.pf.reserve_paths`。

**定义**（`src\table\settings\pathfinding_settings.ini:115-119`）：

```
[SDT_BOOL]
var      = pf.reserve_paths
from     = SLV_100
def      = false
cat      = SC_EXPERT
```

**关键结论：它不是 GUI 可见选项。** 该条目**没有** `str =` / `strhelp =` / `patxname =`（对比同文件 `:83-88` 的 `pf.reverse_at_signals` 三项齐全），同类还有 `pf.wait_for_pbs_path`(:106)、`pf.path_backoff_interval`(:121)、`pf.wait_oneway_signal`(:90)、`pf.wait_twoway_signal`(:98) —— 全是 OpenTTD 1.0 之前「全局 PBS 开关」时代的遗留项，只用于**读旧存档时兼容**（`src\table\settings_compat.h:333-335` 的 `SLCX_VAR`），现代版本不再在设置界面暴露。

**现代版本 PBS 由什么开启**：**信号类型**（路径信号 Path signal / 单向路径信号 One-way PBS），不是这个开关。所以玩家在设置里找不到它属正常，存档里它的值通常是 `false`。

**运行时的两个来源**（`src\train_cmd.cpp:17016`）：

```17016:src/train_cmd.cpp
	bool do_track_reservation = _settings_game.pf.reserve_paths || (flags & CTTF_FORCE_RES);
```

⇒ 现代档里恒等于 `(flags & CTTF_FORCE_RES)`。**`nores-final` 出现等价于「这次 ChooseTrainTrack 调用没带 CTTF_FORCE_RES」**，不是「玩家没开某个设置」。

**因此 P-1 的追查方向修正**：不要再问玩家开没开设置，直接找**同一个车头在同一局面下被以两种 flags 调用**的两个调用点（一个带 `CTTF_FORCE_RES` → `RESV-EXT-PRE doRes=1`；一个不带 → `tick=12551 nores-final doRes=0`），那才是放走列车的入口。

### 27.22 找到唯一真正决定「进哪根轨」的调用点：它压根不带 FORCE_RES（2026-10-27）

**决定性锚点**（`src\train_cmd.cpp:20020`，`prev == nullptr` 即本车是主动车、正在决定进入下一格走哪根轨时）：

```20020:20022:src/train_cmd.cpp
ChooseTrainTrackResult result = ChooseTrainTrack(first, gp.new_tile, enterdir, bits, CTTF_MARK_STUCK | CTTF_NON_LOOKAHEAD);
assert(IsValidTrack(result.track));
chosen_track = TrackToTrackBits(result.track);
```

flags 只有 `CTTF_MARK_STUCK | CTTF_NON_LOOKAHEAD`，**没有 `CTTF_FORCE_RES`**。结合 `:17016`：

```17016:src/train_cmd.cpp
bool do_track_reservation = _settings_game.pf.reserve_paths || (flags & CTTF_FORCE_RES);
```

而 `pf.reserve_paths` 是隐藏遗留项、现代档恒 `false`（§27.21）⇒ **这个主决策点的 `do_track_reservation` 恒为 `false`** ⇒ 必然一路走到 `:17682` 的 `nores-final`，把 `best_track` 交回来，立刻在 `:20022` 写进 `chosen_track`。

**这坐实了三件事**：

1. `nores-final` **不是**「前瞻订路失败」（我此前一瞬的怀疑作废）——它就是**列车运动的决策出口**，返回值直接决定列车进哪根轨。§27.20 的因果方向是对的。
2. `do_track_reservation == false` 不是「玩家没开某个设置」，而是**主决策点在设计上就从不请求整体预留**：它只挑一根轨，真正的预留由紧随其后的 `:20181 TryReserveRailTrack(gp.new_tile, TrackBitsToTrack(chosen_track), false)` 单独做一格。这与日志 `res=0x0 ret=0x1` 完全一致——`res` 探的是 `:17031` 那一刻的预留位（此时当然还没有），订路发生在 `:20181`。
3. 因此「挂接寻路（CPL）已经把路线订到车底」这件事，**对列车进哪根轨毫无约束力**：CPL 的成果只影响 `lookahead`/制动，主决策点仍然会在 `do_track_reservation==false` 下用 `best_track` 自行选道。这就是走错道岔的结构性原因。

**待查（下一步唯一要回答的问题）**：`do_track_reservation == false` 时 `best_track` 到底是谁给的？

按 `:17665` 的条件：

```17665:17667:src/train_cmd.cpp
if (res_dest.tile != INVALID_TILE && !res_dest.okay && (!consist->current_order.IsType(OT_GOTO_COUPLE) || r3r_couple_depot)) {
```

挂接单（`OT_GOTO_COUPLE`）且不在车库 ⇒ 该条件为**假** ⇒ 里面那次 `DoTrainPathfind(...)`（`:17674`，唯一能给 YAPF 轨道的地方）**被跳过** ⇒ `best_track` 只能来自更早的挂接寻路块。而日志给的是 `ret=0x1`（`TRACK_X`，直行），正确应为 `LOWER`（南下）——说明挂接块的赋值也是一路「取第一根轨 / 取直行」的兜底，根本没带挂接寻路的结果。

### 27.23 走错道岔的完整因果链（2026-10-27，已闭环到代码行）

**`best_track` 的唯一正当来源**是挂接寻路，`:17437-17445`：

```17437:17445:src/train_cmd.cpp
		Track path_found = DoTrainCouplePathfind(consist, true);
		if (path_found != INVALID_TRACK) {
			best_track = path_found;
		}
```

日志对应 `CPL-PATHFOUND veh=57 found=0`（`:1737` 附近）⇒ `path_found == INVALID_TRACK` ⇒ **这次没有给 `best_track` 赋值**。

于是 `best_track` 落进 `:17446 if (path_found == INVALID_TRACK)` 的兜底分支（注释自陈「多轨岔口时它仍是 INVALID_TRACK」），而返回值必须是有效轨（调用方 `:20021 assert(IsValidTrack(result.track))`），所以兜底只能是 `FindFirstTrack(tracks)` 之类。

**完整链条**（每一步都有行号）：

| 步 | 位置 | 事实 |
|---|---|---|
| 1 | `:17386` | 挂接单（`OT_GOTO_COUPLE`）且非车库 ⇒ 进挂接块 |
| 2 | `:17437` | `DoTrainCouplePathfind` 被调用 |
| 3 | 日志 `CPL-PATHFOUND … found=0` | 挂接寻路**失败**，`path_found = INVALID_TRACK` |
| 4 | `:17444` | 条件不成立，`best_track` **保持 INVALID_TRACK** |
| 5 | `:17446` 兜底分支 | 为了满足 `assert(IsValidTrack)`，塞进一根「随手可得的轨」 |
| 6 | `:17016` | `do_track_reservation = false`（调用点没带 `CTTF_FORCE_RES`） |
| 7 | `:17682` | `return r3r_chosen_ret(best_track, …, "nores-final")` ⇒ 日志 `ret=0x1` |
| 8 | `:20022` | `chosen_track = TrackToTrackBits(result.track)` = `TRACK_X` |
| 9 | `:20181` | `TryReserveRailTrack(new_tile, X, false)` — 只订这一格 |
| 10 | 日志 `LOCO-MOVE` | 车沿 X 直行，从 `16,14` → `15,14` → `14,14`，永不南下 |

`tracks=0x9` = `TRACK_X(0x1) | TRACK_BIT_LOWER(0x8)`，`FindFirstTrack` 取 bit0 = `X` ⇒ **永远是直行**。这就是「每次都往同一个错方向跑」的机制性原因，与地图布局无关。

**结论：根因不在 `nores-final` 本身，而在它上游的兜底 —— 「挂接寻路失败时，为了给出一个有效 Track，随手塞了 `FindFirstTrack`」。** `nores-final` 只是忠实地把这份伪造结果交了出去。

**修法约束（三条，必须同时满足）**：

1. `ChooseTrainTrack` 的返回值**必须**是有效 Track（`assert` 在 debug 下生效），所以不能靠返回 `INVALID_TRACK` 表达「原地等」；
2. 但「原地等」在调用方 `TrainController` 里是**已有表达**的 —— `:20252 if (chosen_track == TRACK_BIT_NONE)` 分支就是「车停在原地、不进入新格」（TTB-PROBE `fold-geom` 就在那里打，注释说得很清楚：*"the TRACK_BIT_NONE handling just below already leaves the vehicle where it is"*）；
3. 所以正确做法是：**挂接单 + 挂接寻路失败 + 配对已就绪时，让 `ChooseTrainTrack` 以「无轨」的方式收尾**（例如置 `result_flags` 标记、或在 `:17446` 处 `MarkTrainAsStuck` + 撤预留 + 返回 caller 能识别为「别动」的值），由 `:20020` 的调用点把 `chosen_track` 置 `TRACK_BIT_NONE`，而不是让列车按 `FindFirstTrack` 开走。

**下一步**：读 `:17446-17560` 的兜底实现，确认它现在具体塞的是什么（`FindFirstTrack(tracks)`？`FindFirstTrack(bits)`？），以及有没有现成的「停车」标记可用。

### 27.24 兜底值的原文：`FindFirstTrack(tracks)`，且**所有**出口都把它交回去（2026-10-27）

```17446:17455:src/train_cmd.cpp
		if (path_found == INVALID_TRACK) {
			/* … 遇到多轨岔口（tracks 有多个位）时它仍是 INVALID_TRACK —— 于是
			 * "节流跳过"或"行驶中"这两条路一提前返回就把 INVALID_TRACK 递给 assert。
			 * 回退口径与下面 KI-316 那条失败返回完全一致（FindFirstTrack(tracks)）… */
			const Track keep_track = (best_track != INVALID_TRACK) ? best_track : FindFirstTrack(tracks);
```

日志 `tracks=0x9` ⇒ `FindFirstTrack(0x9)` = `TRACK_X`（bit0）⇒ 恒为「直行」。**这就是「每次都往同一个错方向跑」的机制性原因**，与地图布局无关。

**更关键的一点：`keep_track` 在下方三条出口全部被原样交回调用方**：

| 出口 | 行 | 返回值 |
|---|---|---|
| 节流跳过 | `:17472` | `keep_track`（`cpl-search-skip`） |
| 行驶中有自己预留 | `:17553` | `keep_track`（`cpl-moving-keep`） |
| 无自己预留 ⇒ KI-316 处置 | 后续 | **仍是 `keep_track`** |
| 无任何处置 ⇒ 落到末尾 | `:17682` | `best_track`（此时 = `keep_track`）⇒ `nores-final` |

⇒ **即使走了 KI-316 的「撤预留 + `MarkTrainAsStuck` 原地等」，返回给调用方的仍然是那根 `X`，列车照样进入下一格。** `MarkTrainAsStuck` 只设置 Stuck 标志位（影响显示/服务判定），**不阻止 `chosen_track` 被写进 `v->track`**。这就是为什么「原地等」的意图写了三轮、现场却一直开走。

**修法落点（结论）**：问题不在「哪一个 return」，而在**返回值类型根本没有表达「别动」的能力**。必须新增一条标志（如 `CTTRF_STOP_HERE`）并让调用方 `:20020-20022` 在收到该标志时把 `chosen_track` 置 `TRACK_BIT_NONE`，从而落进已有的「车停在原地、不进入新格」分支（`:20252`，其注释明写 *"the TRACK_BIT_NONE handling just below already leaves the vehicle where it is"*）。

判据：修好后同场景应满足
1. 日志出现新的 `cpl-noownresv-stop`（或等价 why）而**不再**出现 `why=nores-final ret=0x1` 把车放走；
2. `LOCO-MOVE veh=57` 在 `16,14` 之后**不再**出现 `15,14`/`14,14`（车原地不动）；
3. 占路的车一离开，下一轮 `CPL-PATHFOUND found=1` ⇒ `COUPLE-OK` 正常；
4. 负判据：非挂接单、车库内挂接、以及真有自己预留的行驶中机车，行为逐字不变。

### 27.25 **§27.23 的 found=0 假设作废**：挂接寻路其实成功，是它自己给出了直行（2026-10-27，铁证）

用 `findstr` 抓 `build\R3R_debug.log`（248 845 B，2026-10-27 17:32:12）：

```
:1508  CPL-PATHFOUND veh=57 found=1
:1565  CPL-RET veh=57 tick=12551 tile=15,14 enterdir=0 tracks=0x9 res=0x0 ret=0x1 why=nores-final spd=5 real=1
:1644  CPL-PATHFOUND veh=57 found=0
:1646  CPL-RET veh=57 tick=12627 tile=14,14 enterdir=0 tracks=0x11 res=0x0 ret=0x1 why=cpl-fail spd=0 real=1
```

**配对关系是 `found=1 → nores-final`、`found=0 → cpl-fail`**，与代码结构逐字吻合：

- `found == INVALID_TRACK` ⇒ 进 `:17446` 块，块内每条出口都是 `cpl-*`（`cpl-search-skip` / `cpl-moving-keep` / `cpl-fail` / `cpl-wait-state2`），**不可能**落到 `:17682` 的 `nores-final`；
- `found != INVALID_TRACK` ⇒ `:17444 best_track = path_found` ⇒ `:17662` 置 `CTTRF_RESERVATION_MADE` ⇒ `:17665` 因「GOTO_COUPLE 且非车库」为假而跳过 `DoTrainPathfind` ⇒ `:17682` 直接 `return best_track` ⇒ 这就是 `nores-final`。

**⇒ 结论作废与替换**：§27.23 表里第 3~5 步（「`found=0` ⇒ `best_track` 保持 INVALID ⇒ 兜底 `FindFirstTrack(tracks)` = `X`」）**是错的**。真实情况是：

> **`DoTrainCouplePathfind()` 在 found=1 时返回的第一步轨道本身就是 `TRACK_X`（直行），而正确路线应当是 `TRACK_BIT_LOWER`（南下）。**

`FindFirstTrack` 只是 `found=0` 时另一条路径的兜底（`:1646 cpl-fail` 那一次，返回 `FindFirstTrack(0x11)` = `0x1`，同样是 X，但那次 spd=0 车没动，无可见后果）。

**因此修法方向随之改变**：不是「补一条让列车停下的通道」（§27.24 的 `CTTRF_STOP_HERE` 方案），而是**查 `DoTrainCouplePathfind` 为什么在 found=1 时把第一步指向直行**。可能的分支：

1. 返回值语义是「目标格上的进入轨道」而非「当前格出发轨道」，被 `ChooseTrainTrack` 当作 `new_tile` 的落点轨使用 ⇒ 语义错位；
2. YAPF 挂接寻路的路径本身起点不是车头所在格（如从 `origin`/预留末端起算），第一步属于别的格；
3. 寻路确实选了 X 方向的路线（cost 计算把南下判得更贵）。

**下一步唯一要回答的问题**：`DoTrainCouplePathfind()` 返回的 Track 是在哪个 Tile 上、朝向如何算出来的。

### 27.26 根因做实：`reach()` 的贪婪 DFS 覆盖了 YAPF 的最优首步（2026-10-27，已改）

**调用链**：`ChooseTrainTrack` `:17437` → `DoTrainCouplePathfind(consist, true)` → `YapfTrainCoupleTrack(v, false)` → `CYapfCoupleRail::FindNearestCoupleTrain()`。

**YAPF 本身是对的**。`yapf_rail.cpp:1040`：

```1040:1040:src/pathfinder/yapf/yapf_rail.cpp
		next_trackdir = pPrev->GetTrackdir();
```

`pPrev` 是 `while (pNode->parent != nullptr)` 走完后的「origin 的子节点」，即路径第一段 ⇒ `pPrev->GetTrackdir()` 就是起点格的正确出向（本案应为南下 `LOWER`）。

**但紧接着这个结果被无条件覆盖**（`:1041`）：

```1041:1102:src/pathfinder/yapf/yapf_rail.cpp
	if (pNode->parent == nullptr && pPrev != nullptr) {
		...
		for (; tdb != TRACKDIR_BIT_NONE; tdb = KillFirstBit(tdb)) {
			Trackdir td = (Trackdir)FindFirstBit(tdb);
			if (reach(st, td, 0)) { best_td = td; break; }
		}
		...
		if (best_td != INVALID_TRACKDIR) {
			next_trackdir = best_td;
```

三件事凑成这个 bug：

1. `while` 结束后 `pNode` **必然**是 origin（`parent == nullptr`），所以 `if (pNode->parent == nullptr && pPrev != nullptr)` **恒真**——每次挂接寻路都会跑这段；
2. `reach()` 是**贪婪深度优先**：`for` 按 trackdir 的 bit 顺序遍历，`if (reach(st, td, 0))` 一旦某方向「能到」就 `break`，**既不比代价也不比长度**；
3. 岔口格 `tracks=0x9` 里 `TRACK_X` 的 bit 排在 `TRACK_BIT_LOWER` 之前 ⇒ 只要「直行能绕远路到达目标」，`best_td` 恒为 `X` ⇒ `next_trackdir = X` ⇒ `TrackdirToTrack` → `TRACK_X`。

日志闭环（`build\R3R_debug.log`）：

| 行 | 内容 | 含义 |
|---|---|---|
| `:1508` | `CPL-PATHFOUND veh=57 found=1` | YAPF 找到路径 |
| `:1565` | `CPL-RET veh=57 tick=12551 tile=15,14 tracks=0x9 ... ret=0x1 why=nores-final` | 交回去的是直行 |

⇒ `found=1` 说明不是"没找到路"，是"找到的路被这段贪婪扫描改判成直行"。这与 §27.25 的实测完全一致，也解释了三轮修复为何都没治到：此前所有改动都在 `train_cmd.cpp` 里找"谁放走了车"，而改判发生在 `yapf_rail.cpp` 内。

**改法（最小、行为只在原判错误时变化）**：先给 YAPF 的首步一个机会——用同一个 `reach()` 验证它能否到达 `tg`，能就保留；只有它不可达（即 `pPrev` 已是目标节点、其 trackdir 属于目标格，正是原注释要覆盖的场景）才回落到贪婪扫描。

- 正常情形（YAPF 首步正确且可达）⇒ 保留 YAPF，本次修复生效；
- 原注释场景（单段挂接，`pPrev` 的 trackdir 属于目标格）⇒ `reach(st, td)` 的 `Follow(st, td)` 会因该 trackdir 不在起点格而失败 ⇒ 回落到原扫描，行为逐字不变。

新增只读探针 `CPL-FIRST-TD st=x,y yapf=<td> ok=<0|1> best=<td>`，每次走到该分支一行，用于复测自证「取的是 YAPF 首步还是扫描结果」。

### 27.27 构建自证（第 331 轮，2026-10-27）

改动文件：仅 `src\pathfinder\yapf\yapf_rail.cpp`（未碰任何 `src\*.h`，增量合规）。

| 项 | 值 |
|---|---|
| 护栏 | `GUARD: incremental is safe (no header/lang file is newer than the newest object)` |
| 编译 | `[2/3] Building CXX object CMakeFiles\openttd_lib.dir\src\pathfinder\yapf\yapf_rail.cpp.obj` |
| 链接 | `[3/3] Linking CXX executable openttd.exe` |
| 判决 | `build\R3R_incbuild.done` = `EXIT_CODE=0`（2026/10/27 17:59:33） |
| 错误计数 | `error C*` / `fatal error` / `FAILED:` 共 **0** |
| obj | `yapf_rail.cpp.obj` 2026/10/27 17:57，8 628 501 B |
| exe | `build\openttd.exe` 2026/10/27 17:58:47，52 322 304 B |
| 字面量自证 | exe 内命中 `CPL-FIRST-TD st=%d,%d yapf=%d ok=%d best=%d` |
| read_lints | 0 条 |

**复测判据（4 条）**：

1. **正判据**：同场景日志出现 `CPL-FIRST-TD st=16,14 yapf=<南下td> ok=1 best=<同值>`，且随后的 `CPL-RET ... ret=0x8`（`TRACK_BIT_LOWER`）或其它南下轨道，**不再**是 `ret=0x1`。
2. **行为判据**：`LOCO-MOVE veh=57` 在 `16,14` 之后应出现 `16,15` / `15,15` 等南下格，**不再**出现 `15,14`→`14,14` 的直行序列。
3. **收尾判据**：机车到达目标邻格后 `CPL-PATHFOUND found=1` ⇒ `COUPLE-OK`（不再停在 `nores-final`）。
4. **负判据**：非岔口单轨路线、以及原注释场景（`pPrev` 已是目标节点）的挂接，`CPL-FIRST-TD` 应出现 `ok=0` 并回落到扫描，行为与改动前逐字不变；普通到站/预留不受影响（本次未动 `train_cmd.cpp` 任何逻辑）。

**边界**：本次只改「多分支格上首步方向的选择」，不改 `reach()` 的深度优先策略本身、不改 `rp()` 的预留行为、不改 `DoTrainCouplePathfind` 的返回值语义。若复测判据 1 仍出现 `ret=0x1`，则说明 YAPF 本身的 cost 就把直行判为最优（§27.25 分支 3），下一步要查 `YapfTrainCoupleTrack` 的 cost 计算。

### 27.28 再前一层：YAPF 的 origin 方向被钉成车辆当前 trackdir（2026-10-27）

对照 KI「第 328 轮」小节的既有取证：

```
:1500 CPL-ORIGIN origin=16,14 td=0 vehDir=5 vehTd=0
:1511 CPL-TRACE st=16,14 tdb=0x101
:1536 CPL-TRACE-RESULT best=0
:1570 RP-BOOK 16,14 td=0
:1577 RP-BOOK 15,14 td=3 trk=3 bits=0x29      ← TRACK_LOWER（绕行第一跳在下一格才换轨）
:1572 RP-BOOK 15,15 td=2 bits=0x15
:1571 RP-BOOK 14,15 td=0 bits=0x9
```

**关键推论**：`vehTd=0`(= `origin` 的 `td`) 且 `CPL-TRACE-RESULT best=0` ⇒ **YAPF 自己选出的第一步就是 `td=0`（直行）**，`reach()` 只是忠实复现它，§27.26 说的「reach 覆盖 YAPF」在此现场**不成立**。

换言之：`reach(16,14, td=0)` 返回 true 是**真的**——16,14 --X--> 15,14 --LOWER--> 15,15 --...--> 12,15 是一条合法的绕行路线。它只是又长又蠢。

**真正的问题在起点方向**：YAPF 的 origin 节点带着车辆当前 trackdir（`vehTd=0`），第一步只允许沿该方向继续 ⇒ 想要南下就必须在 `16,14` 本格转向，而 origin 不给这个机会 ⇒ 只能沿直行绕远路 ⇒ 首步恒为 `TRACK_X`。

这与 `ChooseTrainTrack` 的预期直接冲突：它拿到的 `tracks=0x9`（X 与 LOWER 都允许），期待 YAPF 告诉它「本格该走哪根轨」，而 YAPF 返回的却是「沿当前方向走出的那根轨」。

⇒ **§27.26 的改动因此不修本 bug**（现场会打出 `CPL-FIRST-TD ... ok=1 best=0`，与改动前同值）。它本身无害且让逻辑更正确（不再让贪婪扫描改判 YAPF 的结果），暂时保留，但不作为本 bug 的修复。

**下一步唯一的问题**：`FindNearestCoupleTrain()` 里 origin 是怎么建出来的、有没有「允许本格转向」的开关（OpenTTD YAPF 里 origin 的 trackdir 集合由 `pf.SetOrigin` / `AddStartNode` 决定）。

**REG-330-H 补锚点**：`CPL-RET` 自带 `spd=`，且两次决策恰好夹住跳变——`1571` 在 `15,14` 记 `spd=5`，`1593`（`LOCO-MOVE`）已在 `15,14` 记 `spd=30`，中间仅 22 行日志；`1652` 在 `14,14` 记 `spd=30` 且 `why=cpl-moving-keep`（速度已高，走"保持移动"分支）。⇒ **跳变点锁定在「第一次决策失败（`nores-final`）之后、进入 `15,14` 之前」**。`CPL-RET/CPL-ENTRY/CPL-BEST` 三行都**没有 `tick=` 字段**，故仍算不出精确 tick 间隔；要判定"渐变 vs 硬置"，最省的办法是给 `CPL-RET` 与 `LOCO-MOVE` 各补一个 `tick=`（纯 `.cpp`，走增量）。

### 27.29 **§27.28 的「origin 方向被钉死」判错了**：真凶是「拿 st 的 trackdir 当 new_tile 的轨」（2026-10-27，第 332 轮）

第 331 轮 exe（含 `CPL-FIRST-TD`）新采的日志把上一节的结论直接推翻。原文（`build\R3R_debug.log`，已规整空白）：

```
1421 CPL-ORIGIN veh=57 origin=16,14 td=0 vehDir=5 vehTd=0
1422 CPL-ORIGIN-TD veh=57 tile=16,14 tdb=0x101 tbits=0x1
1423 CPL-ORIGIN-FOLLOW veh=57 td=0 trk=0 ok=0 nt=-1,-1 nbits=0x9 st=0 skip=0
1424 CPL-ORIGIN-FOLLOW veh=57 td=8 trk=0 ok=0 nt=-1,-1 nbits=0x0 st=0 skip=0
1436 CPL-SCAN tick=12551 veh=57 found=1 closed=3 … last=5,15 why=acc-car
1437 CPL-HEAD best=5,15 head=5,15 on=1
1438 CPL-BEST veh=57 tile=5,15 td=0 cost=10242
1439 CPL-BACK pPrev=5,15  td=0 parent=15,15 ptd=2
1440 CPL-BACK pPrev=15,15 td=2 parent=16,14 ptd=0
1441 CPL-TRACE st=16,14 tdb=0x101
```

**三点硬事实**：

1. **YAPF 的首步根本不在 `16,14` 上，而在 `15,15` 上**。回溯链是 `16,14(p td=0) → 15,15(td=2) → 5,15`，末次 `pPrev=15,15 td=2` ⇒ `next_trackdir`(:1040) = `td=2` = `TRACKDIR_UPPER_E`，`TrackdirToTrack(2)` = **`TRACK_UPPER`(0x4)**。也就是说 YAPF **根本没有要求机车在 `16,14` 本格转向**（`td=0` = X 沿 SW 出去正好是 `15,15`），§27.28 的「origin 钉死方向 ⇒ 只能直行绕远」整条推理**不成立**。
2. `st = pNode->GetLastTile()` = **`16,14`（机车自己站的那一格）**，`GetTileTrackdirBits(16,14) = 0x101` ⇒ 只有 X 的两向（`td=0`/`td=8`）。贪婪 `reach()` 从这个集合里只能选出 `td=0`，`TrackdirToTrack(0)` = **`TRACK_X`(0x1)**。
3. 因此真正的错位是**格位错位**：`FindNearestCoupleTrain()` 交回的是「`st` 上的轨」，而 `ChooseTrainTrack` 把它当「`gp.new_tile` 上的轨」用（`train_cmd.cpp` 的 `chosen_track = TrackToTrackBits(result.track)`，并与 `gp.new_tile` 的 `bits` 做 `dbg_assert`）。`st(16,14)` 与 `new_tile(15,15)` 是两张不同的轨道布局，`TRACK_X` 恰好在两张格上都存在 ⇒ 断言不响、列车按"直行"通过岔口。

**为什么第 331 轮白改**：`yapf_first_ok = reach(st, yapf_first_td, 0)` 里 `yapf_first_td=2` **不属于** `st=16,14`，`TrackFollower::Follow(16,14, 2)` 立即失败 ⇒ `ok=0` ⇒ 仍然回落到贪婪扫描。这正是 §27.27 之后日志里看不到任何行为变化的机械原因（`CPL-FIRST-TD` 也印证：`st=16,14 yapf=2 ok=?`，`ok` 必为 0）。

**本轮改法（仅 `src\pathfinder\yapf\yapf_rail.cpp`，未碰 `.h`）**：

- 回溯循环加 `uint r3r_hops` 计数（`pPrev` 最终是否等于目标节点即 `r3r_hops == 1`）。
- 采纳判据改为**按首步格校验**并只在多跳时采纳：
  `yapf_first_ok = (r3r_hops > 1) && td != INVALID && (first_tile != st) && reach(first_tile, yapf_first_td, 0)`；
  采纳时 `best_td = yapf_first_td; best_start = first_tile;`（新增变量，默认 `= st`）。
- `rp()` 的起点由写死 `st` 改为 `best_start` —— 否则拿 `first_tile` 的 trackdir 从 `st` 起走必失败，预留终点会算不出来。
- 单跳路径（`pPrev` 就是目标节点，其 trackdir 是车底站台方向、背向机车）**原样保留**原来的贪婪扫描，`best_start` 仍为 `st` ⇒ 该分支行为逐字不变。
- 探针 `CPL-FIRST-TD` 扩成 `st=x,y hops=N yapf=T ok=K best=B start=x,y trk=R`，一眼能看出采纳了谁、属于哪一格、换算成哪根轨。

### 27.30 构建自证（第 332 轮，2026-10-27）

| 项 | 值 |
|---|---|
| 构建入口 | 复用 `_tmp_inc_build.cmd`（未新建任何 `.cmd`） |
| 护栏 | `build\R3R_incbuild.guard.log` 本轮 18:24:51 重写（81 B），走增量 |
| 编译 | `[2/3] Building CXX object …\yapf_rail.cpp.obj` → `[3/3] Linking CXX executable openttd.exe` |
| 判决 | `build\R3R_incbuild.done` = `EXIT_CODE=0`（18:28:00） |
| 错误计数 | `error C` / `fatal error` / `FAILED:` / `build stopped` = 0 |
| 产物 | `build\openttd.exe` 2026-10-27 18:27:31，52 322 304 B（晚于源码改动） |
| lint | `read_lints` 该文件 0 条 |
| 字面量 | `findstr /c:"hops=%u" build\openttd.exe` → HIT |

### 27.31 复测判据（第 332 轮，4 条）

1. **正判据（核心）**：同一挂接场景出现 `CPL-FIRST-TD … hops=2 yapf=2 ok=1 best=2 start=15,15 trk=4`，且随后的 `CPL-RET` 变成 `tracks=… ret=0x4`（`TRACK_UPPER`），不再是 `ret=0x1`。
2. **正判据（行为）**：机车在 `16,14` 进入 `15,15` 后走 UPPER 分支（`15,15` 的 `td=2`），而不再沿 X 直行；`LOCO-MOVE` 的 `tile` 序列随之改变，且不再出现"每次都在同一岔口走同一条"。
3. **负判据（单跳不回归）**：机车与车底紧邻（`r3r_hops == 1`）时 `CPL-FIRST-TD` 显示 `hops=1 … ok=0`，`best` 与第 331 轮以前逐字同值；`COUPLE-OK` 仍照常出现。
4. **负判据（不引入掉头）**：`CPL-FIRST-TD` 的 `start` 恒等于 `CPL-BACK` 末行的 `pPrev` 格；日志不出现 `REVERSEDIR … force_swap=1` 之类新增掉头记录，`vehDir` 不变。

**仍未确认**：U-1 `CPL-ORIGIN-FOLLOW td=0/8 全 ok=0` 说明「从 `16,14` 沿 X 出发」的两次跟随都失败，而 `reach(16,14,0)` 却返回 true —— 两者用的是同一个 `TrackFollower`，矛盾点尚未解释（可能是 `CPL-ORIGIN-FOLLOW` 探针的参数/时机与 `reach()` 不同，属探针问题而非行为问题）。U-2 `15,15` 上是否确有 `TRACK_UPPER`（`CPL-FIRST-TD` 的 `trk=4` 与 `dbg_assert` 会直接证实/否认）。
