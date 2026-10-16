# 第 233 轮：库型挂接「必须整列进库再合链」落地 + 双头机车逻辑翻爆雷根因

- 轮次：233（2026-10-10）
- 玩家原话：「那我需要修的效果就是列车必须完全进库再库内挂接，然后，我记得双头机车的挂接也适配了逻辑反转吧，这次为什么会爆雷」
- 上游：第 232 轮《KI-339 复测》与 KI-340（`R3R_depot_couple_retest_round232_memo.md`）
- 本轮范围：需求 1 落地改码（仅 `src\train_cmd.cpp`）；需求 2 只做实根因与修法清单（不落地，待拍板）

---

## 一、两个需求拆解

| # | 玩家要求 | 性质 | 本轮处置 |
|---|---|---|---|
| 1 | 列车必须**完全进库**再库内挂接 | 改行为 | 已落地（§二） |
| 2 | 双头机车的挂接"也适配了逻辑反转"，为什么这次爆雷 | 问因 | 根因做实（§三），修法待拍板（§五） |

---

## 二、需求 1：库外触碰即挂 → 收敛为「整列进库后合链」

### 2.1 现行代码（唯一能在库外触发 `Couple()` 的地方）

```cpp
15240 static uint CheckTrainCollision(Train *v, Train *moving_front)
15243     if (v->track == TRACK_BIT_DEPOT) {          // ← 判据落在「被碰车」在库里
15255         Train *loco = moving_front->First();
15256         if (loco->current_order.IsType(OT_GOTO_COUPLE) &&
15257                 v->First()->current_order.IsType(OT_WAIT_COUPLE) &&
15258                 loco != v->First() &&
15262                 R3RCouplePairMatches(loco, v->First())) {
15263             int x_diff = v->x_pos - moving_front->x_pos;
15266             if (x_diff * x_diff + y_diff * y_diff <= min_diff * min_diff) {
15288                 Couple(loco, v->First());
15289                 moving_front->cur_speed = 0;
15290                 moving_front->progress = 0;
```

要点：**门禁只看"被碰的那一节车在库内"（`v->track`），完全没有"运动链本身是否已进库"的判据**。于是机车鼻子刚够到库内车身（自己仍在库外普通轨道上、`track != TRACK_BIT_DEPOT`）就被 `Couple()` 并清速 —— 这正是 KI-340 里 `dist=31` 错位拼接几何的来源。

### 2.2 引擎侧的入库语义（证明"整列进库"是一个可判定、可等待的状态）

- `src\rail_cmd.cpp:4847-4883`（`VehicleEnterTile_Rail` depot 分支）：车节**逐节**把 `v->track` 置为 `TRACK_BIT_DEPOT`，并且只有在**最后一节**进入时才 `if (v->GetMovingNext() == nullptr) … VehicleEnterDepot(consist)`。
- 该分支**没有任何"库已被占用"的检查** ⇒ 多列车可以同时待在同一个 1×1 库格（第 232 轮 §八 已用 `Depot` 无车辆字段 + `vehiclelist.cpp:80-97` 扫描式取车库列表证过）。
- ⇒ "整列进库"= 链上每一节 `track == TRACK_BIT_DEPOT`，可用 `R3RChainFullyInDepot()`（`:12505`，第 231 轮为 KI-339 新增）判定，且**不需要把机车钉在库口**。

### 2.3 落地改动（第 233 轮）

1. `CheckTrainCollision` 库内分支门禁追加 `R3RChainFullyInDepot(loco)`：运动链未整列进库时**不再**触碰即挂。
2. 追加一行只读探针 `CPL-DEPOT-NOTIN veh=… head=… tile=…`（新边沿标签 `R3REDGE_CPLDEPOTSKIP`），用于复测时区分"被新判据拦下"与"根本没走到这里"。

### 2.4 效果与协同

- 拦下后机车继续走 `TrainLocoHandler` 的运动块 → 整列进库（`track` 全部变 `TRACK_BIT_DEPOT`）。
- 整列进库后由**库内扫描块**（`:17395`，第 231 轮已带 `R3RChainFullyInDepot(consist)`）走 `TrainCoupleHandler()` 合链 —— 此时两链都躺在同一条库内轨道上、几何对齐，KI-340 的"命中端≠拼接端"错位前提消失。
- 与第 231 轮 KI-339 的闸门口径完全一致（同一函数、同一语义:"在库里"只看 `track`）。

---

## 三、需求 2：双头机车为什么爆雷

### 3.1 先纠正前提：「双头适配」适的是"相邻"，不是"链头身份"

现存唯一的双头专项代码在 `NormaliseDualHeads()`：

```cpp
2660     /* R3R: inside a segment the two halves of a dual head must stay next to
2661      * each other. Upstream moves the rear half behind the last wagon that
2662      * follows the front half, … (see KI-145). */
2668     if (R3RIsInsideSegment(t)) {
2670         const bool moved = t->Next() != other;
2671         if (moved) { RemoveFromConsist(other); InsertInConsist(t, other); }
```

它保证的是**段内两半相邻**（链序/几何），属 KI-145。它**完全不涉及"链头身份能不能落在后半"**。
另两处也自陈不看双头：`R3RReverseChainDirections()`（`:8787` 一带，逐节取反 direction）与 `R3RRespaceChainAfterEdit()`（`:7725`）的注释（`:9142-9160`）明说"不认 `IsMultiheaded()`/`IsRearDualheaded()`/`other_multiheaded_part`"。

### 3.2 爆破点：`NormaliseSubtypes()` 的硬断言

```cpp
2696 static void NormaliseSubtypes(Train *chain)
2702     assert(chain->Previous() == nullptr);
2705     if (chain->IsWagon()) {
2706         chain->SetFreeWagon();
2707     } else {
2708         assert(chain->IsEngine());   // ← 双头后半在这里必炸
2709         chain->SetFrontEngine();
2710     }
```

双头机车的**后半**按定义（`src\ground_vehicle.hpp:445`）`IsRearDualheaded() == IsMultiheaded() && !IsEngine()`，且它也不是 wagon：

| 车 | `IsMultiheaded()` | `IsEngine()` | `IsWagon()` | `NormaliseSubtypes` 走哪支 |
|---|---|---|---|---|
| 前半 65 | 1 | 1 | 0 | else → `assert(IsEngine())` 通过，置 FRONT ✔ |
| 后半 66 | 1 | **0** | 0 | else → **`assert(chain->IsEngine())` 失败** ✘ |

⇒ **逻辑翻一旦把链头定到后半 66，`R3RRelocateFrontIdentity(v, 66)` → `NormaliseSubtypes(66)` 在 debug 下直接断言崩溃；在 release（断言关）下会造出一条"链头既不是引擎也不是桶车、没有 FRONT 位"的破链。**

因此翻转路径的那个守卫 **不是疏漏，是被这个断言逼出来的硬约束**：

```cpp
10122   if (v_flip_head != v && !v_flip_head->IsEngine()) {      // 候选2
10131       R3RDbgWrite("FOLDCHK-SKIP COUPLE-FLIP-V v_head_not_engine head=%d -> fall through to candidate 3\n", …);
10132       try_candidate3 = true;
10178   if (head != v && !head->IsEngine()) {                    // 候选3
10192       R3RDbgWrite("FOLDCHK-SKIP COUPLE-FLIP-BOTH v_head_not_engine head=%d -> give up this tick\n", …);
10193       return false;
```

### 3.3 现场日志闭环（`build\R3R_debug.log`，第 231 轮 exe @18:32:26 产物）

```
 1604 CPL-HIT site=depot loco=65 v=63 dx=-7 dy=0 maxd=7 overlap=1 spd=0 dir=5   ← 命中：运动车头 65 ↔ 库内车节 63，7px
 1610 SPLICE-GAP-REJECT COUPLE prev=66 x=15459 y=2139 head=61 x=15428 y=2136 exp=8 dist=31  ← 拼接：运动链尾 66 ↔ 等待链头 61，31px
      FOLDCHK-SKIP COUPLE-FLIP-V  v_head_not_engine head=66 -> fall through to candidate 3
      FOLDCHK-SKIP COUPLE-FLIP-BOTH v_head_not_engine head=66 -> give up this tick   （每 tick 重复）
```

- `loco=65` 与 `head=66` 就是同一台双头机车的两半（DUALHEAD 探针：`mh=1 rdh=1 omp` 互指）。
- `dist=31 ≈ 4 节 × 8px = 一整条短机车链长`：命中端（头 65）与拼接端（尾 66）角色错位 —— 即 KI-340。
- 唯一能改观的双翻候选（翻转后拼接端从尾 66 变成头 65，`dist` 会从 31 掉到 ≈7，正好落在 `exp=8` 容差内）**恰恰被 `v_head_not_engine` 挡下**。
- 逐 tick 重复 ⇒ 玩家感知的"爆雷/卡死"。

### 3.4 为什么物理换端没这个问题

引擎自己的换端 `ReverseTrainSwapVehicles()`（`:3955`）→ `ReverseTrainSwapVeh()`（`:3911`）**只 `std::swap` 位置/朝向/tile/gv_flags，从不重排 `Next()` 链序**（`:3928-3935`）⇒ 链头**对象**不变，双头天然安全。
只有 R3R 的**逻辑翻**（`R3RFlipChainBySegments`，重排链序 + 迁移段头/★/primary 身份）才会把后半顶到链头 —— 那条路上的子类型/身份语义**从未为双头适配过**。

### 3.5 结论（回答玩家的问题）

> "双头机车的挂接也适配了逻辑反转"——**只对了一半**：适配的是"段内两半必须相邻"（KI-145），没有被适配的是"**翻转后链头落到后半时的身份提升**"。后者被 `NormaliseSubtypes()` 的 `assert(chain->IsEngine())` 从根上排除，于是 `v_head_not_engine` 在双头链上**必然**命中：候选2 让位、候选3 放弃、逐 tick 重试 ⇒ 爆雷。这次不是"适配失效"，而是**这条路从来没有被适配**，第 232 轮 KI-340 的错位几何第一次把它顶到了台面上。

---

## 四、需求 1 与需求 2 的关系

KI-340 的错位几何只在"**库外贴边拼接**"时出现（命中端=车头、拼接端=车尾，恒定差一整条机车）。
需求 1 把合链推迟到"整列进库、两链同轨对齐"之后 ⇒ **该几何前提消失**，双头链大概率直接走候选0（直拼）成功，根本不进翻转候选。因此本轮先落地需求 1 是**低风险且可能同时解决现场**的顺序。

---

## 五、修法清单（需求 2，待拍板，本轮未落地）

| 方案 | 内容 | 风险 |
|---|---|---|
| **C1** | `NormaliseSubtypes()` 接受后半：`else if (chain->IsEngine() \|\| chain->IsRearDualheaded())` + 删/放宽 `assert` | 置 FRONT 后 **`IsFrontEngine()==1` 与 `IsRearDualheaded()==1` 同时成立**（后者只看 `IsMultiheaded() && !IsEngine()`）⇒ 语义自相矛盾，须同步复核 `IsRearDualheaded()` 全部使用点 |
| **C2** | 两处守卫 `!…->IsEngine()` 放宽为"`IsEngine() \|\| IsRearDualheaded()`"（与引擎自己的 `CanLeadTrain()` 口径一致，后者明确接受 `IsRearDualheaded()`） | 必须与 C1 一起做，否则断言照炸 |
| **C3**（保守） | 含双头机车的链**拒绝"整链倒置"式逻辑翻**，只允许链头仍落引擎的翻法，并显式打日志 | 不改子类型语义，但双头链在错位几何下仍然挂不上（治不了现场，只把"静默爆雷"变成"有报告"） |

推荐次序：**先观需求 1 复测**（若现场已通，C1/C2 优先级下降）；若仍需 C1/C2，则必须与 `IsRearDualheaded()` 语义复核同一轮完成。

---

## 六、未确认项

- U-1 整列进库后，库内扫描块（`:17395`）与 `TrainCoupleHandler()` 是否对双头链走直拼（候选0）成功 —— 需复测日志的 `COUPLE-OK` / `COUPLE-FLIP-*` 判定。
- U-2 被新判据拦下时机车是否**确实**会自己开进库（第 231 轮 KI-339 已给同类证据，但本轮是"库内已有 WAIT_COUPLE 链"的占用态，需实测）。
- U-3 `IsRearDualheaded()` 的全部使用点（C1/C2 的前置调研）未做。
- U-4 本轮现场的双头链两半在链序上本就隔 2 节（`sep=2 adj=0`，KI-334）—— 与 C1/C2 无关的独立存量问题。

---

## 七、复测判据

1. **正判据（核心）**：库型挂接不再出现"库外贴边即挂"。失败场景日志应出现 `CPL-DEPOT-NOTIN …`，且**不再**出现该 tick 的 `CPL-HIT site=depot` + `Couple()`。
2. **正判据**：随后机车整列进库（`track` 全部 `80`），再出现 `COUPLE-OK loco=<机车> …`。
3. **正判据**：`SPLICE-GAP-REJECT … dist=31` 与 `FOLDCHK-SKIP … v_head_not_engine` 的逐 tick 重复消失。
4. **负判据**：站台型挂接（非库）不受影响，行为逐字不变（本分支只在 `v->track == TRACK_BIT_DEPOT` 内）。
5. **负判据**：正常库型挂接（机车自己开进空库挂等待链）行为不变 —— 该场景整列进库后本就满足新判据。
6. **负判据**：`R3RChainFullyInDepot()` 只看 `track`，多格贯通式库跨格车节照旧视为在库内，无回归。

---

## 八、构建自证

见 `R3R_KNOWN_ISSUES.md` 第 233 轮小节的「构建自证」块（复用既有 `_tmp_inc_build.cmd`，未新建任何 `.cmd`）。
