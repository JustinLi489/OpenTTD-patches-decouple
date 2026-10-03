# R3R 第 190 轮临时分析报告 —— 第 188 轮修复（KI-288 / KI-289）实机复测核验

- 日期：2026-10-01
- 本轮性质：**纯核验，未改任何源码**（玩家指示「算了，不用改了，看看探针，如果没问题这一轮就可以结案了」）
- 核验对象：`D:\sourcecode of JGRPP\build\R3R_debug.log`（12741 行 / 837 064 B，mtime 2026-10-01 21:25）
- 生产该日志的 exe：`build\openttd.exe` @ 2026-10-01 20:55:12（51 566 080 B）＝第 188 轮构建产物（含 KI-288 / KI-289）
- 对应 KI 条目：`R3R_KNOWN_ISSUES.md` 的「第 190 轮」小节
- ⚠ **2026-10-01 同轮更正（玩家质疑后）**：本报告第二、三、四节的初版结论有两处**错**，已就地标注并在**第六节**给出更正：①KI-289 的「未通过」作废（现场 6 次掉头是玩家手动，判据①无从判定）；②KI-292 的「配对锁冻结在旧链 24 上」作废（锁是**从未建立**，真因是候选被 KI-288 的目的地判据在配对扫描阶段筛掉）。**以第六节为准。**

---

## 一、核验前置：排除「改了源码没编进 exe」

| 检查项 | 结果 |
| --- | --- |
| exe 内 `COUPLE-WAIT-HEAL` | 命中 |
| exe 内 `COUPLE-WAIT-NOHEAL` | 命中 |
| exe 内 `pair-mismatch` | 命中 |
| 日志内 `COUPLE-WAIT-HEAL` / `COUPLE-WAIT-NOHEAL` | **各 0 次** |

⇒ 探针代码确实编译进 exe，运行时却从未触发。这不是「没编进去」，而是**没走到**。

---

## 二、KI-289 复测：**未通过** ⚠️【本结论已作废 —— 见第六节。掉头是玩家手动，不是引擎自愈】

### 2.1 判据 vs 实测

第 188 轮写的判据①：同场景等待期内 `REVERSEDIR ... rev=0 stuck=1` 最多 **2 次**，之后应出现 `COUPLE-WAIT-NOHEAL veh=26 ... heals=3`，机车停在原地。
判据②：每次自愈前应有 `COUPLE-WAIT-HEAL veh=26 ... n=1/2 limit=2`。

实测（`findstr /n /c:"REVERSEDIR veh=26"`，共 6 行）：

```
11752:REVERSEDIR veh=26 tile=4,11 dir=1 order=16 spd=0 nv=3 rev=0 stuck=1 db=0
12021:REVERSEDIR veh=26 tile=4,11 dir=1 order=16 spd=0 nv=3 rev=0 stuck=1 db=1
12198:REVERSEDIR veh=26 tile=4,11 dir=1 order=16 spd=0 nv=3 rev=0 stuck=1 db=0
12246:REVERSEDIR veh=26 tile=4,11 dir=1 order=16 spd=0 nv=3 rev=0 stuck=1 db=1
12373:REVERSEDIR veh=26 tile=4,11 dir=1 order=16 spd=0 nv=3 rev=0 stuck=1 db=0
12443:REVERSEDIR veh=26 tile=4,11 dir=1 order=16 spd=0 nv=3 rev=0 stuck=1 db=1
```

- 掉头 **6 次**（判据上限 2 次），`db` 在 0/1 间来回翻 ⇒ 引擎自愈在原地把机车反复掉头。
- `COUPLE-WAIT-HEAL` / `COUPLE-WAIT-NOHEAL` **0 次** ⇒ 判据②也不满足。

### 2.2 为什么额度一次都没用上（逻辑链）

KI-289 分两半：生产者打标记，消费者在「Handle stuck trains」里限流。

生产者（`src\train_cmd.cpp:11384-11455`）：

```cpp
if (u == nullptr) {
    if (r3r_rolling) return false;          // 移动循环里的正常态，不标记
    _r3r_couple_no_target[v->index] = 1;    // :11401 —— 无条件打标记
    ... COUPLE-FAIL 探针（:11411 之后，边沿触发） ...
    if (R3RCoupleOrderDestinationReached(v)) { ... return false; }   // 到点干等
    else _r3r_couple_dest_idle.erase(v->index);
    return false;
}
_r3r_couple_no_target.erase(v->index);      // :11463 候选出现才清
```

消费者（`:16867` 起「Handle stuck trains」）：`r3r_wait_no_target = _r3r_couple_no_target.count(consist->index) != 0`，只有它为真才把 `turn_around` 置假、才打 HEAL / NOHEAL。

判定：`COUPLE-FAIL loco=26` 本日志出现 **7 次**（打点在 `:11401` 之后）⇒ 标记**确实被立起**；本现场 `u` 恒为 `nullptr`（候选 23 每 tick 都被闸门拒），订单恒为 16 ⇒ 标记不会被清。⇒ `_r3r_couple_no_target[26]` **全程存在** ⇒ 若走到 16914 的 `ReverseTrainDirection()`，**必然**先打 HEAL 或 NOHEAL。

HEAL / NOHEAL 皆 0 次 ⇒ **16914 那个调用点从未执行过掉头** ⇒ 现场的 6 次掉头来自别处，最可能是：

- `:16715` 的 `r3r_may_reverse = ProcessOrders(consist) && CheckReverseTrain(consist) && R3RWaitingCoupleSeam(...) != R3RSeamEnd::Back` → `ReverseTrainDirection()` + `return true`（提前返回，**根本走不到 16867 的卡死处理块**）；
- 或 `:15917` `CheckReverseTrain()` 内部的「太靠近格末，立即掉头」。

旁证：掉头那一刻 `stuck=1` 为真，而 16914 的前置门是 `if (!mode && consist->flags.Test(VehicleRailFlag::Stuck))`——车确实是 Stuck 的，却丝毫没触发该块的日志 ⇒ 更加支持「掉头发生在这个块之外」。

### 2.3 未确认项

- **U-1**：6 次掉头究竟落在 `:16715` 还是 `:15917`。需下一轮给 `ReverseTrainDirection()` 加一个「调用点编号」入参（或调用栈探针）才能定死。**但「KI-289 无效」这一结论不依赖 U-1**——HEAL / NOHEAL 为 0 已经是独立且充分的证据。

---

## 三、KI-288 复测：**无法判定**（判据①属偶然通过）⚠️【本结论已作废 —— 见第六节：KI-288 判据①**通过且已生效**，`dest-mismatch` 0 次正是「它在候选筛选阶段就把 23 拒掉」的表现】

### 3.1 表面上通过的部分

第 188 轮的现场错挂是 `10184 COUPLE-OK loco=26 rear=0 consist=23 co=1 real=28 type=7 tx=4 ty=11`——**命令指车库 1,11，实际挂在车站 4,11**。

本次日志的全部 `COUPLE-OK` 中最后一条 26 的记录是：

```
COUPLE-OK loco=26 rear=0 consist=23 co=1 real=25 type=1 tx=24 ty=9
```

**没有任何 `COUPLE-OK ... tx=4 ty=11`** ⇒ 「命令指车库却挂在车站」这一现象**没有复发**。

### 3.2 但不能归功于 KI-288 的新分支

KI-288 新增的拒绝在 `couple_group.cpp` 的 `R3RCoupleAllowedIgnoringPair()` 车库分支（`IsRailStationTile(target->tile) ⇒ return false`），它在探针里的标签由 `R3RCoupleTargetAtOrderStation()` 决定，即 **`reject=dest-mismatch`**。

全日志 `reject=dest-mismatch` **0 次**。

原因在闸门判据顺序（`src\train_cmd.cpp:10872-10895`）：

```
1 active-not-goto
2 target-not-segment
3 target-not-wait
4 active-stopped
5 target-stopped
6 pair-mismatch          ← KI-182 配对锁，最外层
7 R3RCoupleAllowedIgnoringPair  → group-mismatch / dest-mismatch   ← KI-288 在这里
```

现场候选 23 每次都在第 6 步被 `pair-mismatch` 挡下 ⇒ KI-288 的两个函数**一次都没被执行到**。

⇒ 结论：**KI-288 在本次日志里既未被证真也未被证伪**；「没错挂」的真实原因是「压根没挂上」（见第四节）。第 188 轮的判据①若只看现象会误判为「通过」，必须在条目里注明这一层。

---

## 四、新发现：KI-292 —— 配对锁冻结在旧链，导致解挂后永远挂不回 ⚠️【「冻结」一词已作废：锁是从未建立 —— 见第六节】

### 4.1 现场时间线

| 行号 | 内容 | 含义 |
| --- | --- | --- |
| 10202 / 10229 / 10297 / 10581 | `CPL-PAIR act=24 tgt=23 dist=4 actTile=20,9 tgtTile=24,9` | veh=24 把 veh=23 锁为自己的耦合目标 |
| 10583 / 10608 / 10622 / 10623 | `... dist=3/2/1 ...` | 24 一路接近 23 |
| **10628 / 10654** | `CPL-PAIR act=24 tgt=23 dist=0 actTile=24,9 tgtTile=24,9` | 24 与 23 同格（最后一行为 10654） |
| **11159** | `DECOUPLE-FIRE consist=26 tx=4 ty=11` | 26 在车站 4,11 解挂 |
| 11164 / 11200 | `TDTRY v=26 u=23 v_head=26 u_head=23` / `DECOUPLE-DONE u=23 co=1 real=27 tx=4 ty=11` | **veh=23 被解出**，停在同格 8 px 外 |
| 11740 → 12567 | `CPL-GATE reject=pair-mismatch site=geo|scan act=26 tgt=23 aOrd=16 tOrd=17 grp=0` × 958 | 26 想挂 23，每 tick 被配对锁拒 |
| 11752 / 12021 / 12198 / 12246 / 12373 / 12443 | `REVERSEDIR veh=26 ... stuck=1` × 6 | 引擎自愈把 26 反复掉头 |

### 4.2 决定性证据

```
findstr /c:"reject=pair-mismatch" R3R_debug.log        → 958 行
findstr /c:"reject=pair-mismatch" R3R_debug.log | find /c "tOrd=17"   → 958
findstr /c:"reject=pair-mismatch" R3R_debug.log | findstr /v "act=26 tgt=23"   → （无输出）
```

⇒ 958 次拒绝**全部**是 `act=26 tgt=23`，且目标 `tOrd=17`＝`OT_WAIT_COUPLE`（合法等待车底）、`aOrd=16`＝`OT_GOTO_COUPLE`（合法挂车机车）。

```
findstr /n /c:"CPL-PAIR" R3R_debug.log
```

`CPL-PAIR` 全表中：

- **没有** `act=26 tgt=23` 任何一行（26 从未取得 23 的锁）；
- `tgt=23` 的最后一行是 `10654 CPL-PAIR act=24 tgt=23 dist=0`；
- 10654 之后**再没有任何涉及 `tgt=23` 的 `CPL-PAIR`**——既没有 24 的「释放」，也没有新主人的「接管」。

⇒ **veh=23 的配对锁从 10654 起冻结指向 veh=24，直到日志结束（约 2000 行 / 数分钟）都没有更新。** ⚠️【本行结论作废 —— 见第六节：挂接成功（10768）与解挂（11159）都会清锁；10654 之后没有 `CPL-PAIR` 是因为**新锁从未建立**，不是因为旧锁没被释放】

### 4.3 根因（初判）⚠️【本节整段作废 —— 见第六节】

- KI-182 的锁是**逐车 NOSAVE 标志**，维护入口是每 tick 的 `R3REnsureCouplePair(v)`，以 `v->First()`（链头）为单位建立 / 撤销（`R3RUnpairCoupleTargets(coupler)`）。
- veh=23 在 10654 时是「另一条链上的等待段头」，被 24 锁住。
- 此后 23 经历**解挂 + 重新编链**（成为 26 链的段头，`INVAR-CTRL ... head=23`），链身份与位置全变，但 24 侧没有任何事件促使 `R3RUnpairCoupleTargets(24)` 执行（24 已不再执行 GOTO_COUPLE / 已不在同一位置）⇒ **锁残留**。
- 26 扫到 23 时取 `locker = R3RGetCouplePairPartner(23) = 24`，因 `R3RCouplePairOutranks(26, 24)` 为假（24 的 enter tick 更早，语义见日志 `CPL-PAIR-STEAL ... myEnter= / hisEnter=`）而 `continue` ⇒ 26 的锁恒为空 ⇒ `R3RCouplePairMatches(26, 23)` 恒假 ⇒ 958 次 `pair-mismatch`。

### 4.4 与 KI-289 的关系（为什么两条叠在一起）

- KI-289 只处理「引擎自愈掉头」这一条路（而且实测根本没走到）；
- KI-292 是「**合法候选被陈旧锁挡在闸门外**」——闸门/锁生命周期问题。
- 两者互不覆盖：锁不解决 ⇒ 挂不上；挂不上 ⇒ 车停着 ⇒ `Stuck` ⇒ 掉头 ⇒ 几何变化 ⇒ 继续挂不上。**KI-292 优先级更高**，修掉它，KI-289 的场景在本日志里压根不会出现。

### 4.5 修法方向（未拍板，下一轮）

- (a) **锁的存活校验**：`R3REnsureCouplePair` 扫到某候选被 locker 占用时，先校验 locker 是否仍「在执行 GOTO_COUPLE 且与该候选取向一致」；不成立即 `R3RUnpairCoupleTargets(locker)` 后重扫。把「抢锁」从「比 enter tick」放宽到「先验活」。
- (b) **事件级清理**：在 `DecoupleTrain()`、耦合提交点、`R3RSettleChainSegments()` 三处链编辑提交点，对「被移动 / 被解出的段头」调用一次 `R3RUnpairCoupleTargets()`（与 KI-263 用 settle 钩子做身份迁移同构）。
- (c) **兜底复核**：`R3RCouplePairMatches()` 增加「锁成立需双方当前都持 GOTO_COUPLE / WAIT_COUPLE 且几何上仍可能相会」。

### 4.6 复测判据

1. 同场景解挂后应出现 `CPL-PAIR act=26 tgt=23`（26 取得锁），且 `pair-mismatch` 不再刷屏；
2. 随后 `COUPLE-OK loco=26 rear=23 ... tx=4 ty=11` 正常完成——**此处才是 KI-288 的 `dest-mismatch` 该出现的地方**，正好两条一起验；
3. 不再出现 `REVERSEDIR veh=26 ... stuck=1`；
4. 其它车的抢锁行为不回退（`CPL-PAIR-STEAL act=48 ... from=27` 的 act=48 / 27 场景）。

---

## 五、本轮结论 ⚠️【本节结论部分作废 —— 以第六节为准】

- **本轮不能结案**。KI-289 判据明确不满足（6 次 vs ≤2 次，HEAL / NOHEAL 0 次 ⇒ 抑制点未覆盖真实掉头路径）；KI-288 未被真正验证（`dest-mismatch` 0 次）。
- 新增 **KI-292**（配对锁冻结）是本次「解挂后卡死 + 反复掉头」的**主因**。
- 本轮**未改任何源码**，也未产生新的构建；所有结论均可在上述日志行号上逐字复核。
- ⚠️ 以上三条中，前两条已被第六节推翻（掉头是玩家手动 ⇒ KI-289 无从判定；KI-288 其实已生效；「冻结」实为「从未建立」）。「主因」的**现象层**（解挂后机车钉死在站台挂不上）仍成立，但**归因**改成 KI-288 的候选筛选 + 闸门判据顺序遮盖。

---

## 六、同轮更正（2026-10-01，玩家质疑后重核）

### 6.1 玩家提出的三点（全部成立）

1. 「可是 23 和 24 刚刚才解挂啊」——质疑「配对锁冻结在旧链」这个前提；
2. 「那个反复掉头是我的手动行为」（即日志末尾 **6 次 `REVERSEDIR veh=26`**）；
3. 「你又是怎么看到它们的锁一直被冻结的」——要求给出观测依据。

⇒ 第二、四节的初版结论作废；本节取代之。

### 6.2 更正一：掉头是玩家手动 ⇒ KI-289 本轮无从判定

- 手动掉头走 `CmdReverseTrainDirection` → `ReverseTrainDirection()`，是**命令**路径；KI-289 的抑制点在 `train_cmd.cpp:16914` 的「Handle stuck trains」块内，是 **tick 自愈**路径。**手动掉头本来就不经过抑制点。**
- ⇒ `COUPLE-WAIT-HEAL` / `COUPLE-WAIT-NOHEAL` 各 **0 次**是**预期行为**，**不能**当作「判据①不通过」。
- ⇒ **KI-289 本轮 = 无法判定**（要判定它需要一个「机车原地等挂、玩家全程不干预」的现场）；状态回到「已修·待复测」。

### 6.3 更正二：「锁冻结」是推断而不是观测，而我推错了

- **先把观测能力的边界说清楚**：代码里**没有任何探针打印 `r3r_couple_target` / `r3r_couple_requester`**。我能看到「锁的状态」的唯一途径是闸门那行的 `reject=pair-mismatch`（`train_cmd.cpp:10882` 一带）。而 `R3RCouplePairMatches()` 要求**两侧锁互相指向**，所以这个标签只能说明「不匹配」，**无法区分**下面三种情况：
  1. 两边都没有锁；
  2. 只有半边锁；
  3. 锁指向别的车。
- 这三者在日志里长得**一模一样**。第四节的「冻结」是我把「不匹配」过度解读成了「陈旧锁残留」。
- **反证一（结构上不可能）**：解挂会主动清锁。`DecoupleTrain()` 在 `train_cmd.cpp:7821-7822` 对 v、u 各调一次 `R3RUnpairCoupleTargets()`；`R3REnsureCouplePair()` 也会在「链头已不再执行 `OT_GOTO_COUPLE`」（`:10953-10957`）或「原配对不再合格」（`:10979`）时撤锁。⇒ 一次解挂之后锁还能残留数分钟，结构上不成立。
- **反证二（计数检索，这次是真的取证）**：
  - `R3RPairCoupleTargets()` 全文件**只有 `:11042` 一处调用**，紧跟 `:11056` 打 `CPL-PAIR` ⇒ 它是锁**唯一**的建立点；
  - `R3RDbgEdge()`（`:6144-6165`）只在「**同一个 (tag,key) 且 payload 未变**」时抑制 ⇒ **首次出现的 `(act,tgt)` 必定打印**；
  - 计数检索结果：**全日志 0 条 `CPL-PAIR act=26`**（`findstr /c:"CPL-PAIR act=26"` = 0 行），而 24 / 27 / 48 / 50 都有若干条。
  - **补充（更强的一条）**：全日志共 **155 条 `CPL-PAIR`**，而 **`10654 CPL-PAIR act=24 tgt=23 dist=0 actTile=24,9` 是最后一条** —— `10655..12741` 区间内**一条 `CPL-PAIR` 都没有**。也就是说：从 10768 那次挂接之后，**整个场景再没有任何一辆车成功建立过配对锁**。这与「解挂之后 26 反复扫描而始终无锁」完全吻合。
  - ⇒ **26 在解挂后从未取得任何锁**，不是「锁被别人占着不放」。
  - **诚实附带说明（避免把这条证据说过头）**：10768 那次挂接的 coupler 链头是 **24**（`CPL-PAIR act=24 tgt=23` 一路 dist 4→0，到位后挂接），26 只是**折叠修正把链头翻过去之后**的新链头（`FLIP-DONE head=26`）。所以「没有 `act=26`」这件事本身有两层原因叠加：①26 从未当过接近阶段的 coupler；②解挂后它想当 coupler 却一直筛不到候选。真正有判别力的是后一条 —— 即 10654 之后再无任何 `CPL-PAIR`。
- **旧推理的来龙去脉（可复核，也是我的错误来源）**：`CPL-PAIR act=24 tgt=23` 最后一行是 `10654 dist=0`，此后 `tgt=23` 再无 `CPL-PAIR`。我把「之后没有 `tgt=23`」读成了「没人释放/没人接管」，其实那条锁在 **10768 挂接成功**时就随 `R3RUnpairCoupleTargets()` 一起清了（挂接成功与解挂同源），此后 23 就是 26 链的一部分，直到 **11159** 才被解出。

### 6.4 更正三：真名是「锁从未建立」，真因在**配对扫描阶段**

- 26 解挂后订单索引归 0：**`OT_GOTO_COUPLE`，而且是车库型**（`11280 DEPOT-ARR veh=26 real=0(16) curType=16 destTx=1 destTy=11 destDepot=1` 指车库 1,11）。
- 23 被解出后停在同一格 **4,11**、持 `OT_WAIT_COUPLE`（`11299 DEPOT-ARR veh=23 ... curType=17 tx=4 ty=11`）。
- `R3REnsureCouplePair()` 每 **8 tick**（`:10986 R3R_PAIR_RESCAN_TICKS = 8`）重扫候选池，筛选条件是 `R3RCoupleAllowedIgnoringPair(coupler, t)`（`:11029`）；**不合格就静默 `continue`，一个字节的日志都不打** —— 这就是「什么都看不见」的根源。
- KI-288 恰好把目的地判据也加进了这个筛选器：`if (IsRailStationTile(target->tile)) return false;`（`couple_group.cpp:321` 一带，车库型订单分支）。而 **4,11 是站台格** ⇒ 23 被筛掉 ⇒ `best == nullptr` ⇒ `:11038` 直接返回 ⇒ **根本不调用 `R3RPairCoupleTargets()`**。
- 闸门里 `pair-mismatch`（KI-182，`:10882`）排在 `dest-mismatch`（KI-288）**之前** ⇒ 958 行日志只能报 `pair-mismatch`，把真因**永久遮住**。
- ⇒ 所以 `reject=dest-mismatch` **0 次不是**「KI-288 没被执行」，而恰恰是「**它在更早的筛选阶段就已经把候选拒掉了**」。**KI-288 的判据①（无 `COUPLE-OK ... tx=4 ty=11`）本轮通过且已生效**，代价是把机车钉死（KI-292）。

### 6.5 一条独立旁证

- `11438 PFD-REJ-GROUP tile=4,11 t=23 ordType=17 wc=1 rej=896`（`pathfinder/yapf/yapf_destrail.hpp`）：挂车寻路的**目标测试**同样把 23 拒了（`rej` 计数器与 `R3RCoupleAllowed*` 判据同源）。这与「候选在筛选阶段就被拒」互相印证，也解释了 `11283 CPL-PATHFOUND veh=26 found=0`（找不到挂车目标 ⇒ 零预留 ⇒ 不移动）。

### 6.6 更正后的复测判据（取代 4.6）

1. 出现 `CPL-PAIR-SKIP ... act=26 tgt=23 why=dest`（先补这行观测，把「靠推断」换成「看得见」）；
2. `pair-mismatch` 不再刷屏；
3. 26 不再原地卡死：要么出现 `COUPLE-OK`，要么按 KI-193 放宽后推进订单、离开站台；
4. 其它车的抢锁行为不回退（`act=48` / `act=27` 的 `CPL-PAIR-STEAL` 场景）；
5. KI-288 判据①保持通过（无 `COUPLE-OK ... tx=4 ty=11`）。

### 6.7 本轮教训（写进流程）

- **别把「标签」当「状态」**：`pair-mismatch` 只说明「不匹配」，不足以推断「陈旧锁残留」。凡是要断言某个内部状态（锁、角色位、段 ID）必须先确认「有没有探针能直接看到它」；看不到就只能写「未确认项」，不能写结论。
- **闸门判据的顺序会制造假象**：外层判据（`pair-mismatch`）会遮住内层真因（`dest-mismatch`）。给筛选器补一条 `CPL-PAIR-SKIP why=` 是零风险、收益最大的一步。
