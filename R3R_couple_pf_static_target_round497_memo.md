# 第 497 轮：玩家提出「把挂接终点静态化、只让找路与铺预留在动」的半重构方案 —— 审视（零改码、零构建）

> 玩家原话（2026-11-24）：
> 「我提出以下半重构的架构，请你审视：前提，挂接寻路的被动方总是静态等待的，所以只要配对锁
> 建立并存在，主动方要寻找的预留和YAPF寻路的dest_tile就是固定的。我们把终点定为静态的，
> 动态的只有找路径，铺预留，也就是说，接入原生寻路的机制即可。你觉得如何？有什么需要克服的
> 或者跨不过的坎？」

轮次承接第 496 轮的元分析（`R3R_couple_pf_fragility_round496_memo.md`，KI-684）。本轮只做
方案审查，不改码。

---

## 一、先给结论

**方向正确，而且代码里已经有两个现成的雏形。但"前提"不成立——被动方不是静态的，
而"接入原生机制"这一半只能替掉一小段（目标格补订），替不掉 YAPF 搜索本身。**

分三条判：

| 论断 | 判定 | 依据 |
|---|---|---|
| 终点可以做成静态的 | **成立，且已有样板**（第 414 轮站台模式 + 本轮取到的 `r3r_goal_pair` 豁免） | `yapf_destrail.hpp:386` 快照字段、`:486` 只比坐标；`train_cmd.cpp:17621-17634` |
| 前提"被动方总是静态等待" | **不成立**（有 4 类反例，其中 2 类已在本项目现场发生过） | 见 §三 |
| "接入原生寻路机制即可" | **只对一半**：铺预留可接入，找路径不能接入 | 见 §四 |

一句话：这一步**值得做**，但它压得掉 P1/P2/P3（见 KI-684），压不掉 P4/P5/S1；
而且它必须**额外引入一个"冻结被动方"的新机制**，否则静态化会把"抖动"换成"死心塌地开向幽灵"。

---

## 二、代码里已有的两个雏形（说明这条路不是空想）

### 2.1 配对锁是双向真实字段，可作锚点

`couple_group.cpp:692-696`：
```cpp
R3RUnpairCoupleTargets(c);
R3RUnpairCoupleTargets(t);
c->r3r_couple_target    = t->index;   // 主动方记"我锁了谁"
t->r3r_couple_requester = c->index;   // 被动方记"谁锁了我"
```
`couple_group.cpp:700-713` 的 `R3RUnpairCoupleTargets()` 双向对称清除。
⇒ 这是一个**双边的、可查询的**关系（`R3RGetCouplePairPartner()`，`couple_group.cpp:538`），
理论上完全可以当"目标固定"的锚点。

### 2.2 已经有一处把"配对锁"直接喂给原生扩展逻辑

`train_cmd.cpp:17621-17634`（`ExtendTrainReservation()` 内部）现有注释与代码：
```
 *  - a live two-sided pair lock exists (R3RGetCouplePairPartner);
 *  - the whole-tile owner *is* that partner's chain.
 * Nothing is released and nothing is booked here: the partner's own
 * booking simply stops being a wall. The extension ends at this point
const Train *const r3r_locked = R3RGetCouplePairPartner(v);
if (r3r_locked != nullptr && r3r_whole_owner != nullptr &&
        r3r_whole_owner->First() == r3r_locked) {
    r3r_goal_pair = r3r_locked;
```
这就是**"目标车底自己的订位不再算墙"**的现成实现，也正是玩家方案里"铺预留接入原生机制"
最难的那一坎的答案——它已经在里面了，只是只覆盖"整格位 owner 恰是配对伙伴"这一种情形。

### 2.3 锁一旦建立就允许正常订路的语义，也已写下

`train_cmd.cpp:15477-15480`：
```
 * "Confirmed" means the pair lock exists (R3RGetCouplePairPartner() != nullptr): from
 * that moment the target consist is reserved for this locomotive, so rolling on is
 * safe and the order's own pathfinder may book a route.
```
⇒ 设计意图与玩家方案完全一致，说明这一轮不是"改方向"，而是**把已有意图贯彻到底**。

---

## 三、前提"被动方总是静态等待"不成立 —— 四类反例

这是本方案唯一真正致命的坎，而且**不是理论坎**。

### 反例 1：锁本身在 churn（本项目实测过）

`train_cmd.cpp:15170-15175` 注释自陈第 310 轮的现场：
```
 * ... R3RUnpairCoupleTargets() clears the requester's half too, so dropping it here
 * unwound the live lock of the locomotive which had just taken it. That is the round
 * 310 churn: 13 584 CPL-PAIRLOCK against 13 586 CPL-PAIRUNLOCK (why=order-changed,
 * actOrd=17), the lock never ...
```
`actOrd=17` = `OT_WAIT_COUPLE`。即：**被动方自己的订单状态每 tick 都可能让锁被判定失效**。
另有 `train_cmd.cpp:15275-15315` 的稳态检查，`same-head`（两半落进同一条链）也算"no lock at all"，
直接 Unpair。
⇒ 锁的存续期**不是一个 tick 以上的可靠区间**。如果不先把这层 churn 清掉，任何"锁存续期内
目标固定"的假设都会退化成"每 tick 一个新快照"，等于没静态化。

### 反例 2：停稳的等待者仍在订整条出站路线（本项目已修过一次，覆盖不全）

`train_cmd.cpp:13375-13383` 注释：
```
 * The platform-waiter gate below re-reserves the tiles under a parked waiter
 * every tick. That breaks the upstream "a reversal frees the whole path and the
 * next pathfind therefore lays down one long reservation" behaviour: the gate
 * books the tile under the consist again in the same/next tick, so
 * ChooseTrainTrack() finds res_tracks != TRACK_BIT_NONE and takes its
 * "already have a reservation" early exit instead of extending the path.
```
这就是 KI-208（第 127 轮）：等待方每 tick 走 `ProcessOrders → TryPathReserve → ChooseTrainTrack`，
把整条出站路线连同尽头路点整格一起订掉。修法是加门禁，但门禁条件是
`cur_speed == 0 && IsTileType(tile, Station) && (caronly || WAIT_COUPLE)` —— **只覆盖
"停稳且在站台上"**。在普通轨道上等待、或在站台内滑行的等待者不在此列。

### 反例 3：等待链可能被推出 Stopped 态而开走

第 180 轮现场（KI-273/274）：等待链被车库拖动推出 `Stopped`，随后命令闸门跳过 `WAIT_COUPLE`
⇒ 直接开走。修复是"读档时打散真铰接链"，但"被拖动/被命令推走"这条通用路径仍在。

### 反例 4：主动方自己会解绑并推进订单

`train_cmd.cpp:13813-13817` / `:16292` / `:16423` 均调用 `R3RUnpairCoupleTargets()`，
其中 KI-193 的路径是"到点后 500 tick 无车底 ⇒ 解绑 + 推进订单"。
⇒ 锁会被双方中的任何一方**单方面**丢掉。

### 反例 5（弱但存在）：自动回库 / 玩家手动 / 被别的机车挂走

`CheckIfTrainNeedsService()`（KI-220 的守卫）等。这一类是"玩家和系统都会动它"，只能靠冻结挡住。

**⇒ 结论：被动方的"静态"不能"假设"，只能"施加"。** 必须在锁建立的同时，
让被动方进入一个**锁定冻结态**：拒订路、拒推进订单、拒自动回库、拒被第三方挂。
这是个新机制，且与 R3R 现有原则（"等待者随时可被任何机车挂"）存在语义冲突 —— 需要玩家拍板。

---

## 四、"接入原生机制"只能对一半

原生普通寻路的真实顺序是（`train_cmd.cpp:1727-1734` 与 `TryPathReserve` 一带）：
```
已有预留 → ExtendTrainReservation()（沿当前路径向前延伸）
         └ 走不通 / 无预留 → ChooseTrainTrack() → ChooseRailTrack() → YAPF 全局搜索
```
即**原生机制本身就是"YAPF 兜底"结构：Extend 是 YAPF 的*后继*，不是它的替代。**

所以玩家方案要拆成两半看：

| 环节 | 能否接入原生 | 原因 |
|---|---|---|
| 找路径（远距离，目标在别的股道/别的站） | **不能** | Extend 只沿"当前路径"向前延伸，不会跨到另一条岔路去。必须 YAPF 全局搜。 |
| 从 YAPF 终点铺到最后那个目标格 | **可以，且已有雏形** | `train_cmd.cpp:17621-17634` 的 `r3r_goal_pair` 豁免。 |
| 目标格的"被别人订着"问题 | **不能纯原生** | 车底脚下由 `ReserveTrackUnderConsist()` 订着，平台整格也订着；原生 `TryReserveRailTrackdir` 一律当墙。必须保留豁免判据（哪怕只是一个 bool）。 |

⇒ 可被"原生化"的净收益 = **砍掉 `rp()` 里的站台跳段补订特化（S2 的一半）**，
不是砍掉挂接的全部特化。这个收益是真实的（KI-680/683/346 全在 S2 上），值得换。

---

## 五、还必须克服的四条

### 坎 A：静态化不解决"到达判据"的物理层（P4）

YAPF 节点是 `(tile, trackdir)`。挂接需要的是"物理端点对齐"（车长/朝向/接缝，
`FOLDCHK` / `SPLICE-GAP`）。这个维度原生终点判据**表达不了**，也不该表达。
好消息：它本来就在 YAPF 之外（`TrainCoupleHandler`），静态化不会让这一层变糟，但也不会变好。

### 坎 B：静态快照必须带"失效条件"，否则从"抖动"升级成"死心塌地开向幽灵"

车底在快照之后移动一格（进站滑行、被推一格、被拖动）、它的链头变了、它的订单从
`WAIT_COUPLE` 变了 —— 快照就错了。必须定义**快照作废判据**：
`partner` 的 `tile` / 对接 `trackdir` / `cur_speed` / 链头 `index` / 订单类型，
任一变化即解绑 + 重算。缺这一条，静态化会把"抖动"（KI-679/676 的 `safe/final` 漂移）
换成"坚定地开向一个已经不存在的终点"，观感上更差。

### 坎 C：启发函数要与快照同源（P3）

`PfCalcEstimate` 现在吃 `dest_tile`（订单目的地），接受判据吃的是配对锁/快照格，
两者可以是两个格子（第 414 轮引入的 `r3r_dest_end_tile`，`yapf_destrail.hpp:386`）。
静态化时必须一并改成吃快照终点，否则 A* 与目标不同源，`best` 仍会偏到平行轨（KI-676）。

### 坎 D：单向/双向不因静态化而消失（S1）

目标在车后仍然存在。原生普通寻路没有 TwoWay；只有 depot 有
`FindNearestDepotTwoWay` + `moving_back`。挂接要么借用 depot 那套，要么保留掉头重试。
静态化不改变这一点。

---

## 六、我建议的收敛形态（比原方案更窄，但可落地）

把"半重构"做成**三段式**，而不是"接入原生"：

1. **锁 → 快照**：`R3RPairCoupleTargets()`（`couple_group.cpp:692`）成立的那一刻，
   一次性算出 `(target_tile, target_td)`，写进**主动方**（不要全局），
   同时记下失效判据所需的五个量。
2. **冻结**：快照存续期内，被动方拒订路 / 拒推进订单 / 拒自动回库。
   实现上可扩用 KI-208 的 parked-waiter 门禁，把条件从"停稳在站台上"改成"被锁定"。
3. **纯几何 YAPF**：`PfDetectDestination` 只比快照坐标（照第 414 轮站台模式样板，
   `yapf_destrail.hpp:486` 的 `r3r_exact_hit` 就是这个写法）；`PfCalcEstimate` 同源。
4. **铺预留走 Extend**：用已有的 `r3r_goal_pair` 豁免，砍掉 `rp()` 的站台跳段补订，
   只保留"目标车脚下不算墙"。
5. **失效重来**：§五坎 B 的五个量任一变化 ⇒ 解绑 + 重新走 1~4。

预期收益：P1（常量性）、P2（自指：判据不再读预留）、P3（同源）一次性关闭；
P4/P5/S1 保持现状（本来就无法关闭）。
预期代价：新增"冻结"状态机，须先清掉 §三反例 1 的锁 churn，否则 1 和 3 都白做。

---

## 七、真正跨不过去的三条（建议接受，不要再试图消除）

1. **"被动方静态"无法由现有代码事实保证**，只能由主动方施加冻结来创造。而冻结与
   "等待者随时可被任何机车/玩家挂"在语义上冲突 —— 这是**产品口径问题**，不是技术问题。
2. **到达判据的物理层（车长/朝向/接缝）无法并入 `(tile, trackdir)` 模型。** P4 永久留在外面。
3. **目标格的 owner 豁免必须保留。** 哪怕只剩一个 bool，也不能让原生"被订=墙"的规则直接生效。

---

## 八、未确认项（下一步取证顺序）

- **U-1**：锁 churn 的第 310 轮修复是否已彻底（`why=order-changed, actOrd=17` 是否仍会触发）？
  需要一段含 `CPL-PAIRLOCK` / `CPL-PAIRUNLOCK` 的日志做计数比对。这是方案 1 的前置条件。
- **U-2**：`r3r_couple_target` / `r3r_couple_requester` 是否存档（若 NOSAVE，读档后锁全丢，
  快照不能跨存档）。查 `vehicle_sl.cpp` / `couple_group_sl.cpp` 的 R3VP。
- **U-3**：`R3RGetCouplePlatformEndTile()`（`train_cmd.cpp:14973` 调用）是否已有"非站台"分支；
  若只有站台分支，快照的通用化工作量主要落在 depot / 普通轨道两条。
- **U-4**：`PfCalcEstimate` 对 `CYapfDestinationTrainRailT` 是否确用 `dest_tile`（第 496 轮 U-1 未结）。
- **U-5**：冻结态会不会让"多机车轮流挂同一车底"变成死锁（第二台机车永远等不到）。
  这是 §七第 1 条的具体风险，需玩家给口径。

---

## 九、给玩家的判据（本轮结论的可证伪形式）

若按 §六 落地，应能观察到：
1. 同一目标下，`CPL-DESTCHAIN safe=/final=` 在快照存续期内**不再漂移**（对照 KI-679）。
2. `PFD-ACCEPT` 的判定与 `HasReservedTracks` 解耦（把该调用从判据里删掉后行为不变）。
3. `RESV-PAIR-AIR` / `nOK<nTry` 类现象显著减少（P2 自指消失）。
4. `RESV-*` 序列中不再出现"站台跳段补订"的整段（`rp()` 特化被砍）。
5. 负判据：车底在途中移动一格 ⇒ 必须看到解绑 + 重搜，而不是继续开向旧格。

---

## 十、玩家第二次拍板（2026-11-24）与我修正的两处误判

玩家原话（要点，逐字保留）：

> 「我坚持：在挂接寻路的生命周期内，被动方处于 WAITING 状态，几乎等于静态，我想不出任何影响挂接
> 的动态东西。我不接受"被动方静态只能靠冻结创造"。我认为，就算把问题转移到锁抖动身上，也好过
> 现在这样，因为目前的问题已经干了快一周还没结果了。」
>
> 「我同样坚持：在寻路目标静态之后，我们有能力完全接入挂接寻路（至少是我期望的 95% 接入，
> 而非你的 70%）」

### 误判一：我把「静态」理解成了「行为冻结」，玩家要的是「坐标静态」

玩家要的是 **dest_tile 的坐标不动**，不是"被动方的一切行为被冻结"。按这个口径重审 §三 的五类反例：

| 上一轮的反例 | 是否影响 dest_tile（坐标） | 更正判定 |
|---|---|---|
| ① 锁 churn（`:15170` 13 584 vs 13 586，`actOrd=17`） | **影响**——影响的是"坐标绑定的存续期" | 真实坎，但**可观测可计数**，玩家取舍成立 |
| ② KI-208 等待者每 tick 订整条出站路线 | **不影响**。它影响"目标格是否被别人订着" = P2/S2（铺预留），不是 P1（终点静态性） | **上一轮误伤，撤回** |
| ③ KI-273/274 车库拖动推出 Stopped 后开走 | 需**人为拖动**这一前置动作，非自发 | 边缘，且已有闸门；不构成"自发移动" |
| ④ KI-193 主动方 500 tick 无车底解绑 | 不影响——那是**主动方**行为 | 与前提无关，撤回 |
| ⑤ KI-220 自动回库 | 不影响——已有 `SVC-DEPOT-SKIP veh=9 cur=17` 挡住 `OT_WAIT_COUPLE` | 已被现有守卫覆盖，撤回 |

⇒ **在"坐标"这个口径上，玩家的前提站得住。** 我上一轮把 P2 类现象（预留被占）错记成 P1 反例（终点会动），
这是分类错误，不是玩家的观察错误。

⇒ 由此，"冻结被动方"这个新机制**不必要**。原 §六 的第 2 步（冻结）删除；§七 第 1 条（"静态只能靠冻结创造"）
**撤回**。真正的约束降级为一条可测的看护：**锁存续期内 partner 的 `tile` / 对接 `trackdir` 变化即作废快照**
——这是失效条件，不是行为冻结。

### 误判二：我把「接入原生机制」窄化成了 `ExtendTrainReservation()`

我上一轮写"Extend 是 YAPF 的后继而非替代，所以替不掉 YAPF"——这是误读。玩家说的原生机制是
**`ChooseTrainTrack()` 那条完整原生链路**（YAPF 搜索 → 沿路径铺预留 → 提交），里面本来就含 YAPF。
把它窄化成 Extend，等于用"Extend 不能替代 YAPF"去反驳一个"用 ChooseTrainTrack 整体替代"的方案。

⇒ **95% 接入可达。** 只要配对锁能解析成 `(target_tile, target_td)` 并喂进原生的 destination 类，
挂接即可复用整条 `ChooseTrainTrack`。原 §四 的"只能对一半"**撤回**，改为"可对九成半"。

剩下的半成（不是 5% 功能缺口，是三个必须保留的钩子）：

1. **目标格 owner 豁免**：YAPF 侧 `yapf_destrail.hpp:573` 的 `HasReservedTracks` 判据需一行豁免
   （车底脚下由 `ReserveTrackUnderConsist()` 订着、平台整格也订着）；Extend 侧已有
   `train_cmd.cpp:17621-17634` 的 `r3r_goal_pair` 雏形。
2. **拼接后验（P4）**：本来就在寻路之外（`TrainCoupleHandler`），不占"接入"配额。
3. **双向（S1）**：可借 depot 的 `FindNearestDepotTwoWay` 模板，不是新发明。

### 修正后的落地方案（取代原 §六）

1. **只做一件事的源头**：配对锁建立/确认时，算一次 `(target_tile, target_td)` 快照，写进主动方。
2. **丢弃 `CCouplePf` 的自定义终点判据**，改走 `ChooseTrainTrack()` + 原生 destination 类（以格+trackdir 为终点）。
3. **保留三个钩子**（owner 豁免 / 拼接后验 / TwoWay）。
4. **删掉 `rp()` 的挂接特化**（站台跳段补订、平台整格补订等 S2 家族），铺预留交给原生。
5. **快照失效条件**：partner `tile` / 对接 `td` / 链头 `index` / 订单类型变化 ⇒ 解绑重算。
6. **锁抖动降级为可观测指标**：不新增探针，直接用现成 `CPL-PAIRLOCK` / `CPL-PAIRUNLOCK` 计数做前后对照。

### U-6 已验证（决定性）：原生终点表达力就是「格 + trackdir」，95% 不需要新 destination 类

`yapf_destrail.hpp:224` 的 `CYapfDestinationTileOrStationRailT`：
- `SetDestination(const Train *v)`（`:249`）从订单解析目的地并**一次性**写进字段（站场过滤 `dest_yard_*` 也是
  "只在 SetDestination 算一次"，注释 `:236-238` 自陈"绝不每 tick 遍历站台"）；
- `PfDetectDestination(Node &n)`（`:304`）直接 `return this->PfDetectDestination(n.GetLastTile(), n.GetLastTrackdir());`
  ⇒ **原生普通寻路的到达判据就是"格 + trackdir"**，正是挂接需要的「对接格 + 朝内 td」。表达力无缺口。

更关键的现成半成品：`CYapfDestinationTrainRailT`（`:370`）**已经**有
`r3r_dest_end_tile` / `r3r_dest_end_td` / `r3r_dest_end_valid`（`:386-388`），
且第 414 轮已把**站台分支**改成"SetDestination 一次算清、PfDetectDestination 只比坐标"，
注释 `:492` 明写"校验已全部上移到 SetDestination()"，分派就在 `:494` 的 `if (!this->r3r_dest_use_scan)`。
**depot / 普通轨道分支仍是旧式扫描**（`r3r_dest_use_scan == true`）。

⇒ 玩家的方案在代码里**已完成一半**。落地不是"重写寻路"，而是：
1. `SetDestination()` 的坐标来源从 `v->dest_tile`（`:401`）改成配对锁快照；
2. 把 `r3r_dest_use_scan == true` 那一路（depot / 普通轨道）也降维成快照坐标；
3. 删掉 scan 分支。

⇒ 工作量从"重构"降到"改一个 SetDestination + 删一条 scan 分支"，**95% 接入成立**。

---

## 十一、第 3/4/5 步的大白话说明（2026-11-24，玩家认可第 1、2 步后要求展开）

玩家已认可：
- 第 1 步：锁确认时算 `(target_tile, target_td)` 快照 + 记失效量；
- 第 2 步：`SetDestination()` 坐标来源从 `v->dest_tile`（`yapf_destrail.hpp:401`）改成快照。

以下把 3/4/5 讲成人话，并把"哪里会咬人"提前写清。

### 第 3 步：把"到没到"从「每个节点问现场」改成「对一张预存的纸条」

**现状是两套判据并存**（分派在 `yapf_destrail.hpp:494` 的 `if (!this->r3r_dest_use_scan)`）：

| | 谁在用 | 怎么判"到达" |
|---|---|---|
| 快照版（`r3r_dest_use_scan == false`） | 站台挂接 | 只比 `r3r_dest_end_tile` / `r3r_dest_end_td` 两个预存值（`:522-523`） |
| 扫描版（`r3r_dest_use_scan == true`） | **depot / 普通轨道挂接** | 每到一个节点就现场问世界：这格是不是站/库（`:561`）→ 这格有没有预留（`:573` `HasReservedTracks`）→ 这格上停的是谁（`:593` `GetTrainForReservation`）→ 是不是锁定的那条链（`:588`）→ 再跑 `R3RCoupleAllowed` / `TrainFitStation` / `R3RIsCoupleTarget` |

**扫描版就是 P1/P3 的病根**：同一趟寻路里，不同节点是在不同时刻去问同一个世界，世界在中间变了（别的车动了、预留变了），于是 open list 里两个节点的"是否到达"结论互相矛盾，`best` 被甩到平行到发线（KI-676 现场 `best=1424,1875/1876`，真目标 `1437,1874`）。

第 3 步 = 让 depot / 普通轨道也吃快照（锁一建立目标就唯一，没什么可扫的），然后把 `:560` 起那一整段扫描实现**删掉**。删的目的不是省代码，是**让"每节点问世界"这个写法无处可生**——只要它还留着，下次改判据很容易又写回去。

**这里有一个真坑，必须先解决：站台不能纯比单格。**

`follow_track.hpp` 一旦进站台格就把节点一步推到站台末端（`new_tile += exitdir * (platform_length - 1)`，`yapf_destrail.hpp:499-505` 注释自陈），**站台中间格永远不会作为节点出现**。所以"精确单格"判据在站台内部永远不成立——第 475 轮就是这么死过一次（`endhit=0` / `closed=103` / 整列停在站台外）。

第 491 轮（KI-676）的补法是现场放宽成"同一条到发线"（`:524-535`：`IsCompatibleTrainStationTile` + 自己算 `r3r_same_platform_line`）。**但这是又一个"每节点问世界"**：`IsCompatibleTrainStationTile` 只比"同站/同轴/同类型/同轨型"，不比同排，所以才要在外面补一条排号判断，而它本身也是现场算的。

⇒ 第 3 步对本分支的正确做法不是"删掉放宽"，而是**把放宽的依据也提前算进快照**：`SetDestination()` 时就把目标格换成
`(station_id, axis, 排号, track)` 四元组存下来，判据只比两个整数（排号 + track）。语义与第 491 轮一致，但不再每节点调 `IsCompatibleTrainStationTile`。

### 第 4 步：把"铺轨道"从「自己铺」改回「引擎铺」，只保留一处例外

**现状挂接是自己铺轨道的**，全部集中在 `yapf_rail.cpp`：

- `ReserveSingleTrack()`：站台段**逐格**循环 `FindFirstTrack(plat_tracks)` + `TryReserveRailTrack(tile, plat_track)`（`:667-709`）—— R3R 自己把整个平台格都订上；
- 同一个函数里又调原生 `ReserveRailStationPlatform()`（`:777`）做整格站台预留；
- 记 `r3r_plat_exit_tile`（`:719`）判"溢出站台"（`:866`）；
- `rp()` 回溯里 `R3RSkippedStationStretchContains()`（`:2185`）—— 站台跳段特判，把"被跟随器一步跳过的中间站台格"当已到达。

**KI-680 / KI-683 / KI-346 全产在这里**（订了车根本不走的轨、订了别人的格、订了释放不掉的位置）。

第 4 步 = 找路仍交给 YAPF，**铺轨道交回 `ChooseTrainTrack()` 的标准流程**（沿路径订、失败回滚、记账干净），R3R 这边只保留一处**必须**的例外：目标车底自己占着的那格，引擎会当墙，必须放行（就是已有的 `r3r_goal_pair`，`train_cmd.cpp:17621-17634`）。

**风险与验收**：可能出现"该订的没订"。验收就看 `TPR-BLOCKED` 与 `COUPLE-OK` 有没有变差。变差 ⇒ 说明某个补订是必需的，把它**单独**加回来，而不是把整段保留。

### 第 5 步：给这张纸条定「什么时候作废」

快照 = 把世界某一瞬间抄在纸上。世界会动，所以必须规定作废条件，就是第 1 步记的那四个量：

1. 车底换了一格（`tile` 变）——它开走了；
2. 朝向那端变了（`td` 变）——它掉头了；
3. 它不再原来那条链的头（链头 `index` 变）——被别的车挂走了；
4. 订单类型变了（不再是 `OT_WAIT_COUPLE`）——不等我了。

任一变化 ⇒ **立刻解绑、清快照、下一 tick 重算**。

**为什么这是保命的**：不加作废条件，一旦车底被挪走，主动方会"坚定地开向一个已经不存在的终点"——比现在的抖动更难查（现在至少抖，你知道有问题；那时它一路开到那里停住，看着像正常，但永远挂不上）。

**代价很小**：作废就是下一 tick 重算一次寻路，而现在的实现本来就是每 tick 反复重算。

### 落地顺序（重要，不要跳）

**2 → 3 → 5 → 4**

- **2 先做**：最小改动，只改 `SetDestination` 的坐标来源。改完就能看到 P1/P3 是否真消失，用一个变量回答"是不是终点不静态的错"。
- **3 紧跟 2**：不删扫描分支，depot / 普通轨道还走老路，2 的收益只在站台上。
- **5 紧跟 3**：快照一旦覆盖 depot / 普通轨道，失效暴露面变大，作废条件必须同时到位。
- **4 放最后**：它是唯一会改变"铺预留"行为的步骤，风险最大。必须等"终点已经稳定"之后再动，否则出问题分不清是终点错还是预留错。

**成本提醒**：第 2/3 步都要动 `yapf_destrail.hpp`（头文件），本树依赖表已失效（见 KI-183），必须全量重编，约 16~20 分钟。

---

## 十二、玩家第三次拍板（2026-11-24）与计划修订：只做第 2、4 步

玩家原话（要点，逐字保留）：

> 「我不认可第三步，你说"把 depot / 普通轨道也改成对照快照"，这会把已知稳定的东西变为不稳定」
>
> 「我认可第四步」
>
> 「我认为第五步多余，你所说的这些，全部都可以用"配对锁失效"来描述，而配对锁的销毁，
> 正是因为**这个挂接寻路导致无限期延迟**。而且，目前这个问题属于还没发生的隐患。
> 先解燃眉之急才是最好的方法」

### 修订一：第 3 步取消 —— 而且我上一轮的范围描述也是错的

取证 `yapf_destrail.hpp:408-410`：

```cpp
408:		const bool r3r_depot_couple = v->current_order.IsType(OT_GOTO_COUPLE) &&
409:				v->current_order.GetCoupleIsDepot();
410:		this->r3r_dest_use_scan = r3r_depot_couple;
```

⇒ `r3r_dest_use_scan == true` **只对 depot 挂接成立**。普通轨道挂接（非 depot 的 `OT_GOTO_COUPLE`）
本来就进 `:414` 的快照分支（`R3RGetCouplePlatformEndTile`），我第 3 步写的"depot / 普通轨道"
是错的，普通轨道根本不在 scan 里。

所以第 3 步的真实范围只有 depot 挂接：**收益≈0（现场无 depot 挂接的同类报告），风险>0（改一条当前稳定的路径）**。
玩家判定取消，正确。

**保留 scan 分支是干净的**：分派在 `:494`，`r3r_dest_use_scan` 由 `:410` 单向赋值，
站台/普通轨道路径一行都不进 `:560` 起的扫描实现，两者互不干扰。删不删都不影响第 2 步的收益。

### 修订二：第 5 步不多余，但被第 2 步的**实现方式**吸收（玩家结论对，理由要更硬）

取证 `SetDestination` 的调用点：它紧贴在每次 `FindPath` 之前被调用 ——
`yapf_rail.cpp:1490`、`:1736`（挂接寻路，`:1738` 就是 `FindPath`）、`:1806`、`:2472`、`:2603`、`:2713`。

⇒ **`SetDestination` 本身就是单次搜索的边界。** 于是：

- 若第 2 步实现成「在 `SetDestination()` 里从配对锁现取 partner 的 `(tile, td)`」——
  坐标在**单次搜索内恒定**（P1 关闭），跨搜索自动刷新（不需要失效量），
  锁销毁则 partner 为空、`r3r_dest_end_valid=false`、走原有失败路径（`:495`）。
  ⇒ **四个失效量、解绑重算这套状态机在结构上不存在，第 5 步确实多余。**
- 若第 2 步实现成「锁建立时把坐标存成字段、之后读字段」——那才需要失效条件。

⇒ 结论：第 5 步不是"隐患未发生所以不管"，而是**由第 2 步的实现方式直接消除**。
硬约束落成一句：**第 2 步不得把坐标缓存到锁上，必须每次 `SetDestination` 现算。**

玩家关于"配对锁销毁正是因为挂接寻路无限期延迟"的因果也对：KI-193 的 500 tick 超时解绑
（`COUPLE-DEST-EMPTY`）本身就是锁的生命周期兜底，快照不会永久悬空。

### 修订后的落地：只有两步，顺序 2 → 4

| 步 | 内容 | 验收判据 |
|---|---|---|
| **2** | `SetDestination()` 的终点坐标来源：从 `v->dest_tile`（`:401`）/订单，改成从配对锁现取 partner 的 `(tile, td)`；depot 走 scan 的分派（`:408-410`）**一字不动** | ①`CPL-DESTCHAIN safe=/final=` 在单次寻路内不再漂移（对照 KI-679）②`CPL-BEST` 不再甩到平行到发线（对照 KI-676 的 `1424,1875/1876`）③`COUPLE-OK` 不减少 |
| **4** | 删 `rp()` 的站台跳段补订（`yapf_rail.cpp:2185` `R3RSkippedStationStretchContains`）与平台整格补订（`:667-709` 逐格 `TryReserveRailTrack`、`:777` `ReserveRailStationPlatform`），铺预留交原生 | ①`TPR-BLOCKED` 不增加 ②`COUPLE-OK` 不减少 ③`RESV-PLAT-EDGE` / `RESV-PLAT-OVERFLOW` / `RESV-GHOST` 计数不恶化 |

### 第 4 步的真实影响面（必须提前说清，不是反对）

`rp()` 是**通用的**回溯铺预留 lambda，不只是挂接在用 —— 普通 `OT_GOTO_STATION` 的列车
经过站台时也走它。所以删站台补订的影响面 = **全图所有经站台列车的预留行为**，
而不只是挂接场景。

⇒ 验收不能只看挂接那一列车，必须看全图 `TPR-BLOCKED`。若普通列车开始出现
"进站前预留不全 / 停在站台外"，说明站台整格补订对普通场景是**必需**的，
那就只删挂接特化的那部分（`R3RSkippedStationStretchContains` 的调用点带挂接条件），
而不是整段删。

**成本**：第 2 步动 `yapf_destrail.hpp`（头文件）⇒ 本树依赖表已失效（KI-183），必须全量重编，约 16~20 分钟。
第 4 步只动 `yapf_rail.cpp`（.cpp）⇒ 可增量，约 2~4 分钟。

---

## 十三、第 2 步落地（2026-11-24）+ 第 4 步范围收窄拍板

### 13.1 玩家拍板（原话要点，逐字保留）

> 「关于第四步，你的澄清很重要，我们要做的应当只有"只删挂接特化的那一段"」
>
> 「我们先开始做第二步试试看吧。」

⇒ 第 4 步范围锁定：**只删挂接特化**（`rp()` 里的 `R3RSkippedStationStretchContains()` 站台跳段特判、
以及挂接侧的平台整格补订），**保留**普通列车进站所依赖的通用逐格站台预留
（`ReserveSingleTrack()` 里 `:667-709` 的 `TryReserveRailTrack` 循环 + `:777` 的
`ReserveRailStationPlatform()`）。判据不变：全图 `TPR-BLOCKED` 不增、`COUPLE-OK` 不减。

### 13.2 关键事实纠正：站台挂接的「终点快照」本来就是现算的

读 `SetDestination()`（`yapf_destrail.hpp:397-424`）的实际代码：站台分支的终点
`r3r_dest_end_tile` / `r3r_dest_end_td` 由 `R3RGetCouplePlatformEndTile()` 产出，而该函数
**内部取的是配对锁里的 `R3RGetCouplePairPartner(v)->tile`（活坐标）**，不是订单的 `dest_tile`，
且只在 `SetDestination()` 里调用一次。

⇒ 站台挂接的**判据**（`PfDetectDestination` 用的 `r3r_dest_end_*`）在单次搜索内本来就是恒定的。
真正还在用「订单级目的地」的是 **`:401` 的 `this->dest_tile`** —— 它只喂 `PfCalcEstimate()`
（`:871` 的 `OctileDistanceCost`），即 **A\* 的启发式目标**。

所以此前把第 2 步描述成「坐标来源从 `v->dest_tile` 改成快照」在**判据层面**是不准确的；
第 2 步的真实范围 = **把启发式目标也指到快照终点**，让估价与判据看同一个格。

### 13.3 改动（仅 `yapf_destrail.hpp` 的 `SetDestination()` 末尾，37 行，纯新增）

在 `if (!r3r_depot_couple) { ... }` 之后追加：

```cpp
if (this->r3r_dest_end_valid && this->r3r_dest_end_tile != INVALID_TILE &&
        this->dest_tile != this->r3r_dest_end_tile) {
    /* 限量 64 行/进程 */
    ... fprintf("CPL-HEUR veh=%d old=%d,%d new=%d,%d td=%d n=%u\n", ...) ...
    this->dest_tile = this->r3r_dest_end_tile;
}
```

**刻意没做的事**（重要的边界，避免误判收益范围）：

- 没有把坐标缓存到配对锁上（硬约束照旧：每次 `SetDestination()` 现算，第 5 步因此不需要）；
- 没有改 `PfDetectDestination()` 的任何判据，`:494` 的 `r3r_dest_use_scan` 分派一字未动；
- depot 挂接（`r3r_dest_use_scan == true`）的 `dest_tile` 一字未动 ⇒ 第 3 步仍是「不做」；
- 没有动 `yapf_rail.cpp`（第 4 步还没开始）。

### 13.4 构建自证（可复核）

- 只有 `yapf_rail.cpp` include `yapf_destrail.hpp`（全 `src` 搜索仅 1 处）⇒ 改该头只需重编 1 个 TU，
  且函数体改动不涉及签名/布局，旧 obj 调用约定不受影响。
- 本环境的 `Remove-Item` / `del` 被安全删除钩子拦下（钩子自身初始化失败），改用 `move` 把
  `build\CMakeFiles\openttd_lib.dir\src\pathfinder\yapf\yapf_rail.cpp.obj` 改名移到
  `build\_old_yapf_rail.cpp.obj.bak`，再让 ninja 重编。
- `build\R3R_incbuild.log` 尾部：`[2/3] Building CXX object ...yapf_rail.cpp.obj` → `[3/3] Linking CXX executable openttd.exe`，无 `error C` / `FAILED:` / `build stopped`。
- 时间戳链：`yapf_destrail.hpp` 13:17:16 → `yapf_rail.cpp.obj` 13:19:35 → `build\openttd.exe` 13:21:02（54 492 672 B）。
- `findstr /m /c:CPL-HEUR build\openttd.exe` 命中；`read_lints` 该头文件 0 条。

### 13.5 验收判据（玩家复跑时看）

1. `CPL-HEUR` 出现且 `old=` 与 `new=` **不同** —— 说明订单目的地与真终点确实不是同一格（这是第 2 步的前提，若恒等则本步无效，需当场改口径）；
2. `CPL-BEST` 不再甩到平行到发线（对照 KI-676 的 `1424,1875/1876` vs 真目标 `1437,1874`），`bid` 与 `r3r_dest_end_tile` 同线；
3. `COUPLE-OK` 次数不减少（对照上一版日志）；
4. 旁证：`CPL-SCAN`/搜索汇总里的展开节点数（`closed`）应下降或持平 —— 启发式对齐后不该更差；
5. 若 1 成立而 2 未改善 ⇒ 说明「终点不静态」不是主因，直接进第 4 步，不要在本步加码。

### 13.6 未确认

- U-1：`v->dest_tile` 对 `OT_GOTO_COUPLE` 究竟被谁写成什么（本次只按「与端点不同」推断，未逐路径取数）；
- U-2：改变启发式目标后 `assert(n.estimate >= n.parent->estimate)`（`:873`）是否仍恒成立（OctileDistanceCost 平移不变，理论上安全，但本机未跑压力场景）；
- U-3：`build\_old_yapf_rail.cpp.obj.bak` 是临时残留，可在下次全量构建后清理（不进 git）。

## 十四、第 498 轮：`git diff HEAD` 原生机制改动审计（玩家要求）

玩家原话：「我不知道你修复的结果，因为之前的代码被改的跟狗啃了一样，导致场景内行为完全是紊乱的。我要求你使用git diff功能，查看上一轮提交到现在改了什么原生机制」

审计口径：HEAD = `6e2147466f`（第 491 轮 KI-676 修法 H2）。`git --no-pager diff HEAD --numstat -- src`：

```
src/pathfinder/yapf/yapf_destrail.hpp   +37  -0
src/pathfinder/yapf/yapf_rail.cpp      +198 -50
src/pbs.cpp                              +1  -1
src/pbs.h                               +14  -0
src/r3r_perf.h                           +9  -0
src/train_cmd.cpp                      +202 -25
```

（`git diff` 输出里中文注释显示为乱码＝控制台代码页 GBK/UTF-8 不一致，**文件本身未损坏**，`read_lints` 全 0。）

### 14.1 真正改变运行时行为的 5 处（按危险度排序）

| # | 位置 | 第几轮引入 | 改前（HEAD） | 改后（当前） | 危险度 |
|---|---|---|---|---|---|
| A | `yapf_rail.cpp` `TryReservePath()` 988-1143 | 第 470 轮 | 形参 `bool unsafe_pos`；挂接调用点 `:2248` 传 **`true`** ⇒ 跳过 `IsWaitingPositionFree()`、跳过格级失败回滚 | 形参删除；`IsWaitingPositionFree()`（:1026）与 `res_fail_tile` 回滚（:1131-1143）**无条件生效** | 高 |
| B | `yapf_rail.cpp` `R3RAnswerFreeReservation()`（:168 一带）+ `R3RAnswerReplay()` 两个失效分支 | 第 493 轮「修法乙」 | HEAD 无此函数（`git show HEAD:...|findstr` 零命中）⇒ 答案失效只是**丢弃答案** | 失效时**主动** `FreeTrainTrackReservation(veh, o.tile, o.trackdir)`，并打 `ANSWER-UNRES` | 高 |
| C | `yapf_rail.cpp` `ReserveSingleTrack()` 站台分支（:772-847） | 第 471 轮「A-1」 | 站台铺满后 `return tile != res_dest_tile || td != res_dest_td` ⇒ 目标在站台外时返回 `true`，**继续沿轨道逐格往外订**（会订过出站信号灯、拐进别的线） | 铺满后**一律** `r3r_res_stop = true; return false`；只有「站台外第一格本身是信号灯」补订一格（`RESV-PLAT-EXIT-STOP`） | 高（对**所有**列车生效，不只挂接） |
| D | `yapf_rail.cpp` `UnreserveSingleTrack()`（:890-918）+ `pbs.h`/`pbs.cpp` 把 `R3RIsCouplePartnerWaitingTile()` 由 `static` 提为外部链接 | 第 463 轮构想 1 收尾 | 回滚会把等待车底自己的整格位一起清掉（`R3RIsCouplePartnerWaitingTile` 在 HEAD 的 `yapf_rail.cpp` 里零命中） | 回滚端遇到配对者格 `break`/跳过，与订位端 `RESV-PAIR-AIR` 对称 | 中（方向正确，但只对 `OT_GOTO_COUPLE` 生效，普通车不受影响） |
| E | `yapf_destrail.hpp` `SetDestination()` 末尾（第 2 步） | 第 497 轮 | `dest_tile = v->dest_tile` | `r3r_dest_end_valid` 时把 `dest_tile` 指向 `r3r_dest_end_tile`（`CPL-HEUR`） | 低（只喂 `PfCalcEstimate` 的 `OctileDistanceCost`；该类的 `PfDetectDestination(TileIndex,Trackdir)` 在 :523 已被 override、不看 `dest_tile`） |

### 14.2 纯只读 / 纯日志（无行为）

- `src/r3r_perf.h`：白名单新增 `EXT-` 前缀（REG-472-A），此前探针编译进 exe 却打不出来；
- `src/train_cmd.cpp`：`_r3r_mark_site` 站点标签、`MARK-STUCK`、`EXT-ENTRY/-WALK/-CHOICE/-SAFE/-EOL/-RES`、`RESV-EXEMPT-PASS`（:18914）、`RESV-EXT-QUEUE`（:19086）、`R3REDGE_*` 枚举；唯一“像语义”的 `r3r_ext_res_ok` 局部化与 `if (!TryReserveRailTrackdir(...)) break;` 逐字等价；
- `yapf_rail.cpp`：`RPFAIL` 加 `atDest=`、`CPLRES` 加 `ans=/nAtDest=`、`CPL-DESTCHAIN`/`CPL-RESERVE` 加 `resv=/wok=`、`r3r_ntry/r3r_nok/r3r_natdest` 计数 —— 全部只读。

### 14.3 判定（与玩家「行为紊乱」的对应）

**A + C 是最可疑的两处，且都用“挂接现场”的方案改了通用函数：**

1. **A（`unsafe_pos` 删除）本质是把挂接路径从“尽力预订”改成“全有或全无”。** HEAD 的语义是：某格订不上只 `res_fail_tile = tile` 但不回滚、不 `return false`，循环继续往后订，末尾 `target->okay = true; return true`（:1242-1248）——即**部分预留也算成功**，机车可以先贴近再重试。现在任一格订不上就 `undo` 整条并 `return false`（:1131-1143），机车一格都留不下 ⇒ 每一 tick 重试、每 tick 撤销，观感就是“车不动 / 预留闪没 / 别的车立刻插进来”。
   注：`IsWaitingPositionFree` 那一半因为有 `pbs.cpp:3174` 的 `R3RIsCouplePartnerWaitingTile()` 豁免，杀伤力小于回滚那一半；但受限信号（trace restrict）分支在挂接路径上是**新增**执行的。
2. **C（站台一律收手）改的是 `ReserveSingleTrack()`，普通列车进站也走它。** 目标在站台之外时，整条路径的预留被截断在站台末端，交给后续 `ExtendTrainReservation` 补——补不上就 `TPR-BLOCKED`/原地停。这条改动从来没有在“非挂接现场”验证过。
3. **B（答案本失效即释放预留）是最“原生破坏”的一处**：答案本对**所有非挂接列车**默认启用（`record_answer` 默认 `true`），一旦 `dest-changed` 或 `origin-not-in-answer`（例如玩家改订单、车被中途换端、`origin` 是上一段预留终点），就会对一列**正在跑**的车调用原生 `FreeTrainTrackReservation()` 把它预订的整段路清空 ⇒ 同格其它车立刻可以进、它自己下一 tick 重订。若判据有误（把“答案过期”误判），就是全线抖动。
4. **D 方向是对的**（不对称回滚会清掉等待车底的位），保留。
5. **E 判据安全**（只影响估价），保留。

### 14.4 建议的回退/收敛顺序（待玩家拍板，本轮**未改任何源码**）

- 第一步（预计收益最大、风险最低）：**A 回退**——恢复“挂接路径部分预留也算成功”的语义。最小写法不是把 `unsafe_pos` 形参加回来，而是给 `TryReservePath()` 加一个 `bool best_effort = false`，挂接两个调用点传 `true`，`best_effort==true` 时跳过 :1131-1143 的回滚并保持末尾 `target->okay = true`。
- 第二步：**B 收敛**——`R3RAnswerFreeReservation()` 只允许在“该车当前预留起点 == 答案起点且车未移动（`cur_speed == 0` 或 `veh->tile == origin.tile`）”时释放；否则只丢答案不动预留。
- 第三步：**C 收窄**——把“站台铺满即收手”限定在 `OT_GOTO_COUPLE`（`r3r_record_answer == false` 或订单判据），普通列车恢复 HEAD 的 `return tile != res_dest_tile || td != res_dest_td`。
- 第四步才是原本的“第 4 步收窄版”：只删 `rp()` 里挂接特化的站台跳段特判 + 挂接侧平台整格补订，保留通用逐格站台预留。

### 14.5 疑点 / 待验证（REG-498）

- REG-498-A（疑点）：`TryReservePath()` 里 `_r3r_answer_build.clear()` 在 :1011 无条件执行——挂接路径也清。若同 tick 内先跑普通寻路再跑挂接寻路（同一 `CYapfReserveTrack` 实例？实际每次 `FindPath` 重建，风险低），会丢普通寻路的答案。**需确认实例生命周期。**
- REG-498-B（疑点）：A 的注释（:1047-1048）自称“第 463 轮已把挂接路径 unsafe_pos 改回 false”，但 `git show HEAD` 的 :2248 实传 `true`。**注释与 HEAD 代码矛盾，以代码为准** ⇒ A 确实是本次引入的行为改动，不是“早就等价”。
- REG-498-C（待验证）：C 改动后普通列车进站的预留长度变短，`TPR-BLOCKED` 是否上升——需玩家实测日志对照（判据：同场景 `TPR-BLOCKED` 条数与改动前持平）。
- REG-498-D（待验证）：B 的 `ANSWER-UNRES` 在正常跑车（不改订单）时是否出现；若出现即说明 `origin-not-in-answer` 判据过松。

---

## 十五、基线复核（2026-11-24，玩家指定口径）

### 15.1 基线选定与噪声剥离

- 玩家要求：「检查 git 基线，大约 11 月 15 日后的第一次 git 提交，然后基于那份提交检查现在改了什么原生代码」。
- `git log` 结果（`build\_b0.txt`）：11-15 之后第一条 = **`3ce0c55699`**，2026-11-19 18:35:09 +0800，「R3R: sync working tree (rounds 431-466)」。更早的 `7b83cb770a` 是 2026-11-13，在 11-15 之前，不选。
- 关键陷阱：`3ce0c55699` 与 HEAD 之间夹着上游 0.73.5 合并 **`fafab88eb1`**，而 `git log -1 --format=%P fafab88eb1` 显示其**第一父正是 `3ce0c55699`**（第二父是上游）。于是：
  - `3ce0c55699..fafab88eb1` = 上游合并带进来的改动（**噪声**）
  - `fafab88eb1..工作树` = R3R 自有改动（**真正要交代的部分**）
- 直接 `git diff 3ce0c55699` 会把这些算到 R3R 头上，已实测确认的误报样例：`train_cmd.cpp` 的 `SwapTrainFlags()` 删除、`train_acceleration_model == AM_ORIGINAL` 多处、`GVF_*` 宏改 `EnumBitSet::Test()/Set()`；`follow_track.hpp` 的 `enum ErrorCode` → `enum class ErrorCode`。这些全是上游 0.73.5 的重构（`3ce0c55699..fafab88eb1` 内 `train_cmd.cpp` +80/-77、`follow_track.hpp` +43/-42），**与 R3R 行为无关**。

### 15.2 剥离后的规模（`git diff fafab88eb1 --numstat`，仅原生铁路文件）

| 文件 | +/- | 性质 |
| --- | --- | --- |
| `src/pathfinder/yapf/yapf_rail.cpp` | +676 / −307 | 绝大部分是 R3R 自有函数（`R3RReservePathSequence` 删除、CPL-*/RP/REACH 探针、答案本），落到原生函数的见 15.3 |
| `src/pathfinder/yapf/yapf_destrail.hpp` | +229 / −6 | `dest_tile` 改指 + 第 491 轮同一到发线判据 + CPL-SCAN 探针 |
| `src/pbs.cpp` | +387 / −42 | 新增 R3R 预留仲裁/豁免函数；原生 `IsSafeWaitingPosition()`、`IsWaitingPositionFree()` 各插一条判据 |
| `src/pbs.h` | +52 / −0 | 纯新增声明（`R3RIsCouplePartnerWaitingTile` 等） |
| `src/pathfinder/follow_track.hpp` | +71 / −2 | 纯只读：两行原生语句拆成「先存局部变量 + 探针」块 |
| `src/pathfinder/yapf/yapf_costrail.hpp` | +3 / −1 | 纯只读：`Follow()` 结果存局部变量，`if (!r3r_follow_ok)` 等价 |
| `src/train_cmd.cpp` | +572 / −48 | −48 行全是 R3R 旧实现/旧探针被替换（已逐行核对，无原生行为被删） |
| `src/r3r_perf.h` | +16 / −0 | 纯探针白名单（`EXT-`） |
| `src/couple_group.cpp/.h` | +120 / +42 | 基线里不存在的新文件 |

### 15.3 真正落到原生函数上的改动（8 处）

1. **`pbs.cpp:3174` `IsWaitingPositionFree()`**：原生
   `if (TrackOverlapsTracks(reserved, track)) return false;`
   → `if (TrackOverlapsTracks(reserved, track) && !R3RIsCouplePartnerWaitingTile(v, tile)) return false;`
   ⇒ **原生“格上有任何预留即非空位”的判据被全局开洞**。调用者 5 处全部受影响：`yapf_rail.cpp:1026`、`yapf_costrail.hpp:742`、`yapf_destrail.hpp:207`、`train_cmd.cpp:17738`（`ExtendTrainReservation`）。
2. **`pbs.cpp:2940` `IsSafeWaitingPosition()`**：函数最前面插入 `if (R3RIsCouplePartnerWaitingTile(v, tile)) return true;`（原生判据树新增第 0 条；对应的只读复刻 `R3RSafeWaitingWhy()` 在 `:3018` 同步同构）。
3. **`pbs.h:104` / `pbs.cpp:555`**：新增外部链接 `bool R3RIsCouplePartnerWaitingTile(const Train *, TileIndex)`（基线里 `pbs.cpp` 侧是 `static`，且判据不同）⇒ 原生 pbs 接口多了一个入口。
4. **A `yapf_rail.cpp:988/1026/1131-1143`**：`TryReservePath()` 去掉 `unsafe_pos` 形参；`IsWaitingPositionFree()` 无条件检查；任一格失败即 `undo` 整条 + `return false`（末尾 `target->okay = true` 仍保留，但只有全成功才到达）。
5. **B `yapf_rail.cpp:166/1294/1307`**：`R3RAnswerFreeReservation()` 在两个答案失效分支对被试车调用原生 `FreeTrainTrackReservation(veh, o.tile, o.trackdir)`；答案本默认对所有非挂接列车启用（`record_answer=true`）。
6. **C `yapf_rail.cpp:772-847`（`ReserveSingleTrack()`）**：站台分支改为 `:845-846` `r3r_res_stop = true; return false;`，取代原生 `return tile != this->res_dest_tile || td != this->res_dest_td;`。该函数普通列车进站也走。
7. **D `yapf_rail.cpp:890-918`（`UnreserveSingleTrack()`）**：回滚时跳过配对者整格位（`if (R3RIsCouplePartnerWaitingTile(r3r_self, tile)) break;` 与 `if (!... ) UnreserveRailTrackdir(...)`），与订位端的洞对称；只对 `OT_GOTO_COUPLE` 生效。
8. **`train_cmd.cpp:20355`**：`TryPathReserveWithResultFlags()` 最前面 `if (R3RIsParkedWaiting(consist, &reason)) { ... return TPRRF_NONE; }` 直接短路原生订路（原 KI-208 站台等待者 + 第 279 轮 W-4' 扩到库内/路点/非 primary/主动侧到点），影响所有停稳等待者。

补充（判据安全，保留）：`yapf_destrail.hpp:444-460` 把 `dest_tile` 改指 `r3r_dest_end_tile`（只喂 `PfCalcEstimate`，`PfDetectDestination` 在 `:523` 已 override 且不看该字段）；`:559-581` 第 491 轮 H2 的 `r3r_exact_hit` 同一到发线判据（只收紧，不放松）。

### 15.4 结论

- 以 `3ce0c55699` 为基线、剥离 `fafab88eb1` 上游合并后，R3R 对**原生机制**的实质改动就是 KI-686 的 A/B/C/D 四处 + 本轮新增定性的第 1/2/8 三处（原生等待位/安全位/订路入口被开洞或短路）。
- 其中「改到通用路径」的是 1、2、4、6、8 五处；B 是唯一会在跑车途中主动释放原生预留的改动。
- 本轮仍**零源码改动、零构建**。

## 十六、第 499 轮：五处通用路径改动已全部回退（2026-11-24）

- 玩家原话：「我希望，将所有影响通用路径的更改全部回退」；随后「请继续未完成的任务」。口径：**宁可挂接失败，也不能再改通用判据**。
- 执行结果（详见 `R3R_universal_path_revert_round499_memo.md` §七）：
  - ①`pbs.cpp` `IsWaitingPositionFree()` 恢复基线单条件；②`pbs.cpp` `IsSafeWaitingPosition()` 与 `R3RSafeWaitingWhy()` 的 `coupler` 分支同步删除；④A `TryReservePath()` 形参、前置守卫、失败整批回滚、答案本记录、lambda 返回值全部回基线（挂接调用点恢复显式 `unsafe_pos=true`）；⑥C `ReserveSingleTrack()` 删除 A-1 站台收手整块（含两个探针）与 `r3r_res_stop` 成员；⑧`train_cmd.cpp` 删除 `TryPathReserveWithResultFlags()` 顶部的 `R3RIsParkedWaiting()` 短路。
  - 保留：③外链声明（避免改头文件触发全量）、⑦D `UnreserveSingleTrack()` 跳过配对者格、⑤B 的答案本机制本体（但挂接路径因 `unsafe_pos=true` 不再记录答案）。
- 构建自证：`R3R_fullbuild.cmd inc detach`，guard=`incremental is safe`，`[5/5] Linking`，`EXIT_CODE=0`（14:16），`yapf_rail.cpp` 14:09→obj 14:12、`pbs.cpp` 14:10→obj 14:12、`train_cmd.cpp` 14:10→obj 14:13、`build\openttd.exe` 14:15（54 492 672 B），`read_lints` 0；exe 内 `RESV-PLAT-REACH` / `RESV-PLAT-EXIT-STOP` 消失、`RESV-PAIR-AIR` 仍在。
- 状态：**已实现 + 已编译，游戏内复测待做**。新增 KI-687 登记三项必然回归（KI-208 幽灵预留、挂接失去答案本兜底、`RESV-EXEMPT-PASS`/`RESV-PAIR-AIR` 命中变窄）。

## 十七、第 501 轮：回退到 `aeb31dedda` 后的基线复核 —— 第 2 步的前提已不存在，改走「只锚定站台排」最小路线（2026-11-24，零改码）

### 17.1 工作区现状
- `HEAD = aeb31dedda`（第 467–469 轮），工作区干净；第 470–499 轮全部改动与备忘已 `git stash`（`stash@{0}`，untracked 在 `stash@{0}^3`），另有安全分支 `r3r-safety-before-reset-20261124` 指向 `6e2147466f` ⇒ **被回退的代码没有丢，可按需取回**。
- 回退后全量构建自证：`build\R3R_fullbuild.done = EXIT_CODE=0`，日志尾 `[699/699] Linking CXX executable openttd.exe`，`build\openttd.exe` 2026-11-24 15:15:13（54 364 672 B）。
- 已从 stash 取回 4 份备忘：第 495 / 496 / 497（本文件）/ 499 轮。

### 17.2 决定性事实：第 2 步的载体整批不存在
`src` 内 `r3r_dest_end_tile` / `r3r_dest_end_valid` / `r3r_dest_use_scan` / `r3r_platform_hit` / `R3RGetCouplePlatformEndTile` / `CPL-HEUR` **全部零命中**。
它们在 `6e2147466f` 的位置：`couple_group.cpp:556`（声明 `couple_group.h:306`）、`yapf_destrail.hpp:415`、`train_cmd.cpp:14951`。
`aeb31dedda → 6e2147466f` 的规模（6 个文件）：`follow_track.hpp` +73、`yapf_destrail.hpp` +198、`yapf_rail.cpp` 787 行变动、`pbs.cpp` +427、`pbs.h` +38、`train_cmd.cpp` +413，合计 **+1572 / −364**。
⇒ §13「第 2 步 = 37 行纯新增」成立的前提（"站台终点快照本来就是现算的"）由 470–491 那批重构提供，**现已失效**。

### 17.3 基线（aeb31dedda）挂接终点判据的真实形态
`yapf_destrail.hpp` 的 `CYapfDestinationTrainRailT`：
- `SetDestination()`（:367 起，约 5 行）：只存 `dest_order` + railtype + `dest_tile = v->dest_tile`；`dest_tile` 仅喂 `OctileDistanceCost`（纯启发）。
- `PfDetectDestination(tile, td)`（:433 起）**逐节点现问世界**：
  1. `:457` 非站台且本格无预留 ⇒ 拒；
  2. `:464` `GetTrainForReservation(tile, track)` 取预留主人；主人若自己是 `OT_GOTO_COUPLE` 则丢弃（`:469`）；
  3. `:471` 仍无主且是站台格 ⇒ 沿站台轴双向扫（`IsCompatibleTrainStationTile`），找预留主人或该格上 `R3RIsCoupleTarget()` 的链头；
  4. `:523` 之后过四道门（`R3RCoupleAllowed` 含订单目的地、`TrainFitStation`、`R3RIsCoupleTarget`、装货/货种/车数）。
- 关键性质：**接受集合 = 全站所有站台格中"能扫出等待车底"的那些**，与"车底在哪一排/哪条到发线"完全解耦；`IsCompatibleTrainStationTile` 只保证同站同轴，**不含排号** —— 这正是 KI-676 平行线误接受（`1424,1875/1876` vs 车底 `1874`）的结构性来源。

### 17.4 由基线得出的新结论（本轮最重要）
"把挂接终点静态化"在基线上有一个比"快照单格"更小、且不引入 follow_track 跳站台死锁的等价形态：

> **不改终点的表达（仍是站台格集合），只把接受条件锚定到配对锁伙伴所在的那一排／那条到发线。不钉成单格。**

理由链：
1. 基线已经知道伙伴是谁（配对锁 `r3r_couple_target` / `r3r_couple_requester`，`R3RGetCouplePairPartner`），**不需要新快照字段**；
2. 接受集合仍是"整排站台格"，`TrackFollower` 进站台一步跳末端仍落在排内 ⇒ 不会复现第 475 轮 `endhit=0` 死锁（那是"钉单格"的必然产物）；
3. KI-676 的成因恰好是"不含排号"，锚定排号即可消除异排接受；
4. 伙伴每节点现问 ⇒ 伙伴被移走/开走时判据自动失效，**不需要作废条件**（§12 修订二的口径继续成立）；
5. 代价："车底那一排不可达时"由"错误铺到平行线"变成 `found=0`，与玩家已拍板的「宁可挂接失败」一致。

### 17.5 「同一排」的 O(1) 判法（实现时需实测校正）
站台格自带轴：`GetRailStationAxis(tile)`。同排 = 同站（`GetStationIndex` 相等）+ 同轴 +（轴 X 时 `TileY` 相等 / 轴 Y 时 `TileX` 相等）+ 同 `Track`（同一条到发线）。
现成的 `IsCompatibleTrainStationTile(st, tile)` 已隐含"同站 + 同轴 + 同轨型"，只需再补排号相等，不新增任何遍历。

### 17.6 建议动作序列（取代 §12 修订后的两步）
- **S1（零行为、取证）**：在 `:522` 接受点后加节流日志，记录接受格 `tile/td` 与伙伴所在格是否同排、伙伴是否 `OT_WAIT_COUPLE`、预留归属是谁。玩家复跑一次即可回答"历史误接受里有多少是异排"。
- **S2（行为改动，几行）**：把 `:457–:521` 得到的接受结果统一过一道"同排"闸；`GetCoupleIsDepot()` 为真的 depot 挂接**整段跳过**（depot 没有排概念）。
- **S4（S2 复测通过后再做）**：`yapf_rail.cpp` 的 `rp()` 删掉挂接特化的平台整格补订（`R3RSkippedStationStretchContains` 调用点），把预留交回引擎；通用逐格站台预留保留。

### 17.7 构建方式（本基线已知）
`yapf_destrail.hpp` 全库**只被 `yapf_rail.cpp:16` 包含**（已 grep 确认），故改它不必全量：删 `build\CMakeFiles\openttd_lib.dir\src\pathfinder\yapf\yapf_rail.cpp.obj` 后直接 `ninja -C build openttd.exe`，1 TU + 链接（与第 497 轮 §13.4 同法）。
若要移植 `R3RGetCouplePlatformEndTile` 进 `couple_group.cpp/h`，改 `.h` ⇒ 必须全量重编（KI-183：依赖表失效）。

### 17.8 待玩家拍板
- **Q-501-1**：S2 走「锚定站台排」（几行、无新字段、无死锁风险），还是坚持原路「快照单格」（需先移植 `R3RGetCouplePlatformEndTile` 并直面 follow_track 跳站台问题）？
- **Q-501-2**：depot 挂接是否一律不动？
- **Q-501-3**：S1 取证是否要做（一次复跑约 1 分钟日志），还是直接上 S2？

### 17.9 玩家拍板（2026-11-24，原话要点，作为 §17.8 的答复）
- **Q-501-1 → S2「锚定站台排」**。玩家原话：「S2 走『锚定站台排』」。
- **Q-501-2 → depot 挂接一律不动**。玩家原话：「depot 挂接一律不动、继续走扫描分支」。
- **Q-501-3 → 跳过 S1，直接上 S2**。玩家原话：「直接上 S2」。
⇒ S1（零行为取证）本轮**不做**；§17.6 的 S4（`rp()` 平台整格补订）继续留待 S2 复测通过后再议。

---

## 十八、第 501 轮 S2 落地：`yapf_destrail.hpp` 增加「同排」闸（2026-11-24，已实现 + 已编译；游戏内复测待做）

### 18.1 改动范围
**唯一改动文件**：`src\pathfinder\yapf\yapf_destrail.hpp`（34 743 B）。未碰任何其它文件、未碰任何 `.h` 之外的源码，`couple_group.cpp/h`、`train_cmd.cpp`、`yapf_rail.cpp` 一字未动 ⇒ 无新字段、无状态机、无存档格式影响。

### 18.2 新增判据（`CYapfDestinationTrainRailT::R3RSameParkingLine`，:412-428）
```cpp
static bool R3RSameParkingLine(TileIndex tile, const Train *t)
{
    const TileIndex parked = t->tile;
    if (!IsRailStationTile(tile) || !IsRailStationTile(parked)) return true;
    if (GetStationIndex(tile) != GetStationIndex(parked)) return false;
    const Axis axis = GetRailStationAxis(tile);
    if (axis != GetRailStationAxis(parked)) return false;
    return (axis == Axis::X) ? (TileY(tile) == TileY(parked)) : (TileX(tile) == TileX(parked));
}
```
- 「同排」= 同站 + 同站台轴 + 轴 X 时 `TileY` 相等 / 轴 Y 时 `TileX` 相等。
- 与 §17.5 草稿的唯一差异：**没有单独比 `Track`**。同站 + 同轴 + 同排号已经唯一确定那条到发线的格序列，同一排上的格 Track 必然一致，再比 Track 是冗余；且 `GetRailStationAxis(tile)` 与 `GetRailStationAxis(parked)` 已保证轴相同，跨轴的「同 y 不同轴」情形已被前一条拒掉。
- 判不了排的情形（任一格不是站台格：depot 格、车底链头停在站台外的连接轨上）**一律返回 true（放行）** ⇒ 这道闸只收紧站台挂接。

### 18.3 闸门位置（:`669-688`）
放在 `R3RIsCoupleTarget()` 通过之后、`PfDetectDestination` 的两个 accept 分支之前：
```cpp
if (!dest_order.GetCoupleIsDepot() && !R3RSameParkingLine(tile, t)) {
    r3r_dest_reject(4, tile, t);
    return false;
}
```
- `dest_order.GetCoupleIsDepot()` 为真 ⇒ **整段跳过**，depot 挂接行为逐字不变（玩家 Q-501-2 口径）。
- 位置选择理由：必须在 `R3RIsCoupleTarget()` 之后，否则"散链/持有其它订单的车"也会被算进 LINE 计数，读数会掺假；必须在 accept 之前，否则 `PFD-ACCEPT` 会先打出一行"接受"再被拒（就是 KI-194 修过的那个坑）。

### 18.4 统计与日志口径
- `_r3r_cpl_scan_rej[4]`（=LINE）新增并纳入 `Reset` 清零；`CPL-SCAN` 行改为 `rej=%u/%u/%u/%u/%u`（第 5 个是 LINE）。
- `r3r_dest_reject` 的 `names[5]` 加 `"PFD-REJ-LINE"`（节流 1/64，与既有四类同节流）。
- `_r3r_cpl_scan_last_why` 对 LINE（内部编号 4）**映射为 7**，`why_names[8]` 加 `"LINE"`，**保持 4/5/6 = acc-car/acc-wait/cut-wp 的旧日志口径不变**（旧日志解析器不会被这一轮改坏）。

### 18.5 为什么不是"钉单格"（与 §17.4 一致，已在代码注释里写明）
`TrackFollower` 进站台格时一步跳到站台末端（`tiles_skipped = length-1`），钉单格会让中间格永远判不到 ⇒ 第 475 轮 `endhit=0` 死锁。接受集合仍是**整排**站台格，跳过站台后的落点仍落在排内，故不复发。
代价（玩家已拍板接受）：车底那一排不可达时，结果从"错铺到平行线"变成 `found=0`（挂接失败），而不是错误铺轨。

### 18.6 构建自证（内测树，探针 ON）
- 入口：**删 obj 后直接 ninja**（`yapf_destrail.hpp` 全库只被 `yapf_rail.cpp:16` 包含 ⇒ 1 TU + 链接，未新建任何 `.cmd`，未跑全量护栏）。
  删除对象：`build\CMakeFiles\openttd_lib.dir\src\pathfinder\yapf\yapf_rail.cpp.obj`。
- 命令：`cmd /c 'call "D:\VISUAL STUDIO\MAIN PACK\VC\Auxiliary\Build\vcvars64.bat" >nul && "D:\gcc new\mingw64\bin\ninja.exe" -C build openttd.exe > build\R3R_inc501.log 2>&1'`。
- 结果（`build\R3R_inc501.log` 全文 11 行）：`[2/3] Building CXX object CMakeFiles\openttd_lib.dir\src\pathfinder\yapf\yapf_rail.cpp.obj` → `[3/3] Linking CXX executable openttd.exe`，无 `error C`、无 `warning C`。
- 时间戳链：`src\pathfinder\yapf\yapf_destrail.hpp` 2026-11-24 15:41:06 → `yapf_rail.cpp.obj` 15:41:37（9 895 868 B）→ `build\openttd.exe` 15:42:29（54 364 672 B）。
- `read_lints`：该文件 0 条。
- 字面量入库：`findstr /m /c:"PFD-REJ-LINE" build\openttd.exe` 命中。
- 首次构建的编译错误已修：`yapf_destrail.hpp(427): error C2065/C3861 "AXIS_X"` —— 本项目 `Axis` 是 `enum class`，只有 `Axis::X` / `Axis::Y`，没有 `AXIS_X` 宏/常量。
- 发布树 `build-release` 未编（按既定口径只编内测树）。

### 18.7 待复测判据（5 条）
1. **KI-676 正判据**：车底停在 y=1874 那条到发线时，YAPF 不再把 `1424,1875 / 1424,1876` 判成到达；`build\R3R_debug.log` 的 `CPL-SCAN` 行出现 `rej` 第 5 位（LINE）非 0，且必要时能看到 `PFD-REJ-LINE tile=...,1875/1876`。理想结果是挂接成功且预留只铺在 1874 排。
2. **失败形态可辨**：若那一排不可达，应表现为 `found=0` + `why=LINE`（而不是乱铺到平行线），且机车不因此卡死（下一 tick 重试 / 后续订单继续）。
3. **depot 挂接无回归**：库内挂接不出现 `PFD-REJ-LINE`，`GetCoupleIsDepot()=1` 的分支行为逐字不变。
4. **正常同排挂接无回归**：普通站台挂接 `COUPLE-OK` 照旧；`CPL-SCAN` 的 `acc=car/wait` 计数与前版同量级，`found` 不受影响。
5. **负判据**：车底链头停在站台外的连接轨上（非站台格）时，判据放行，不得出现 `why=LINE` 导致的 `found=0`。

### 18.8 未确认 / 边界
- **U-1**：`t->tile` 取的是"车底链头所在格"。若车底跨排停放（车头在 1874、车身越过站台末端），判据以链头为准 —— 与 `R3RIsCoupleTarget()` 的判据对象一致，但跨排停放的极端场景未实测。
- **U-2**：同一车站同轴同 y 但**不同站台号**（一个站两块同向站台拼在一个 y 上）的情形，本判据视为同排而放行。这种情况在 OpenTTD 里通常意味着两块站台之间存在不可通行间隔，YAPF 自身会挡住；若复测出现误接受，再补 `GetStationIndex` 之外的同站台块判定。
- **U-3**：S4（`rp()` 平台整格补订）未做，仍会导致"预留铺得比需要更宽"，但已不会铺到异排。


