# 第 223 轮（2026-10-08）玩家五条 issue：取证、归属与推论

- 玩家原话（要点）：*"还请你录入 KI 或寻找现有的 KI，修复的时候优先修复现有的，这轮不上手改，你有疑问的可以来问我"*；第 5 条 *"只要 AI 给出猜想/推论，本轮不写入 KI"*。
- 本轮性质：**纯登记 + 只读取证**。未改任何源码、未构建、未新建任何 `.cmd`。
- 交付物：本报告 + `R3R_KNOWN_ISSUES.md` 末尾「第 223 轮」小节（新增 KI-332 / KI-334 / KI-335，认领 KI-321，关联 KI-316 / KI-323）。

---

## 一、第 1 条「复制多段链时段特质继承有问题」→ **新增 KI-332**

### 1.1 已确认的代码事实（只读）

| 层 | 克隆时是否复制 | 证据 |
| --- | --- | --- |
| 引擎 / 假引擎子类型位 | ✅ | `src\vehicle_cmd.cpp:2206-2212`（`SetEngine/ClearWagon/ClearFreeWagon`，`IsFrontEngine()` 按源镜像） |
| 段边界 ★/⊗ | ✅ | `R3RMirrorSegmentMarkers()` `src\vehicle_cmd.cpp:529-542`（双向镜像：置位**和**清位），调用点 `:2224` + 构建后锁步遍历（KI-223 / KI-270 / 第 179 轮） |
| 停在库内（`VehState::Stopped`） | ✅ | `src\vehicle_cmd.cpp:2226-2240`（KI-223：假引擎链克隆必失败的真因） |
| 真铰接打散 | ✅ | `R3RDearticulateOneGroup()` `src\vehicle_cmd.cpp:461`，第 170 轮 |
| **段号 `unitnumber` / `unitnumber_backup`** | ❌ | `unitnumber` 在 `vehicle_cmd.cpp` 的命中全在 **build / promote / release** 路径（如 `:280-294` 的号池恢复），**克隆循环内零命中** |
| **段名 `name` / `name_backup`** | ❌（只有链头一个名） | `CloneVehicleName()` `:1542`，调用点 `:2479`，只对 `v_front`（链头）做"加序号"复制 |
| **`r3r_priority`** | ❌ | 克隆循环内零命中 |
| **`couple_groups` 掩码** | ❌ | 克隆循环内零命中（掩码只在 `R3RNormaliseChainGroups` / 读档路径写） |
| **`r3r_segment_id` / 段 ID 行** | ❌ | 段行只由 `R3RSettleChainSegments` / `R3RSyncSegmentTraits` 在**读档与提交点**重建；克隆后不会为原链的每个段重建段行 |

### 1.2 结论

克隆是**"物理层忠实复制 + 段身份层全部回落默认值"**：

> 克隆出来的东西"长得像原链的段"（有 ★/⊗、有引擎/假引擎位），但**每一段都是匿名段** —— 段号、段名、挂接分组、优先级、段 ID 行全部丢失，退化为"新建车辆的默认值"。

### 1.3 关联既有 KI（这就是"优先修既有的"落点）

- **KI-129**（复制纯假引擎链恒失败 → 身份重建，已修）：只解决**能不能造出来**。
- **KI-223 / KI-270 / 第 179 轮**（★/⊗ 镜像，已修）：只解决**段边界还在不在**。
- **KI-247**（三特质读写统一走段行，已修）：只解决**读写口径**。
- ⇒ 本条是这四条**都没覆盖的第 4 层：段身份数据本身的复制**。故新增，但修法必须与上述四条同源（落在 `CloneVehicle()` 的锁步遍历里）。

### 1.4 待玩家拍板（影响修法）

克隆品应当继承 (a) 全套（段号 / 段名 / 分组 / 优先级 / 段 ID 行），还是 (b) 只继承"段的划分（已做）+ 段名"，段号重新分配？

### 1.5 现有探针为何看不见

`CLONE-SRC` / `CLONE-DST` / `CLONE-MARKERS` / `R3R-CLONE-GROUP-MISMATCH` 全部只 dump `subtype / ★ / ⊗ / artic`，**不含任何段特质字段** ⇒ 这就是"问题存在但日志一片正常"的原因（复测前建议先扩探针）。

---

## 二、第 2 条「前往车库挂接，应当无论如何直接前往车库，再库内扫描」→ **认领 KI-321（既有）**

### 2.1 现状就是玩家要的语义（第 213 轮已落地）

- `src\train_cmd.cpp:13414-13497`：当 `OT_GOTO_COUPLE` 的目的地是**车库**时，**跳过耦合寻路（CPL）**，改用普通寻路直接驶入库内；到库后再做库内扫描（`:11800-11829`）。探针 `COUPLE-DEPOT-DIRECT`。
- 即：**车库型 = 无条件直驶车库 → 库内扫描判断**，与玩家第 2 条逐字一致。
- **对照（刻意不做）**：站台型**不会**自行驶向目的地 —— KI-323（第 214 轮，按玩家口径 = 设计行为）。

### 2.2 本条要做的只有"复测 + 口径确认"

1. 车库型按 KI-321 原判据复测（至今未实机复测）。
2. 第 2 条是否要求**站台型也先到目的地再扫描**？该口径与 KI-316（第 214 轮改判"原地等待 = 正确行为"）与 KI-323 **正面冲突**，需玩家拍板。
3. 已登记的、与本条同源的缺口（不另开条目）：`COUPLE-DEST-EMPTY`（`src\train_cmd.cpp:11893-11921`）到点无目标时是**原地无限等待、保留订单**（第 112 轮起；KI 条目 `:10089` 与该处注释仍写"满 500 tick 跳过命令"，属**过期描述**，待随口径一并清理）；KI-289 的 `COUPLE-WAIT-HEAL` 够不着 `found=0`。

---

## 三、第 3 条「双头机车挂接时机车错位」→ **新增 KI-334**

### 3.1 已确认的代码事实（只读）

- `R3RReverseChainDirections()`（`src\train_cmd.cpp:8560-8598`）对链上**每一节无条件**执行：
  `direction = ReverseDir(direction)` → `flags.Flip(Flipped)` → `InvalidateNewGRFCacheOfChain()` → `InvalidateImageCacheOfChain()` → 逐节 `UpdateDeltaXY()`。
  **全文不含 `IsMultiheaded` / `IsRearDualheaded` / `other_multiheaded_part` 任何判断** —— 双头两半没有被当成"原子对"处理。
- 对比证据（说明代码库知道双头需特殊对待，而这里没有）：`SetSegmentTailFakeEngine()` `src\vehicle_cmd.cpp:638-660`，其中 `:656` 明确 `if (last->IsRearDualheaded()) return;`。
- 另一个可能接触点：`R3RRespaceChainAfterEdit()`（`:7369` 起）逐对按 `CalcNextVehicleOffset` 的**名义间距**重排；若双头两半之间被当作普通缝，间距会被"对齐"到名义值，而双头对接是**固定刚性**的。

### 3.2 结论（本条为**待取证**，不下死结论）

最可能的机制：**逻辑翻转 / 间距重排路径不识别双头两半的成对关系**，在链头落在双头组内、或段边界切在双头两半之间时，把两半翻成不一致朝向 / 把对接间距当普通缝写坏 ⇒ 玩家看到的"机车错位"。

### 3.3 关联既有 KI

KI-143（双头后半节误升格死循环，已修）、KI-145 / KI-146 / KI-152（双头 + 段家族）、KI-214（边界框偏移，未修）、KI-98 / KI-106（几何两套尺子）。本条与 KI-214 可能同源（都是"位置没动、几何/朝向被改写"族）。

### 3.4 需要的取证（请玩家丢日志）

双头机车挂接场景的 `build\R3R_debug.log`，重点看：
`FOLD-V` / `FOLD-U` 逐节 dump 里**双头两半的 `dir` / `engType` / `x,y`**、`SWAPV` 行前后两半 direction、以及 `R3RRespaceChainAfterEdit` 的 `RESPACE-AFTER-EDIT skip=/front=`。

---

## 四、第 4 条「解挂后两列几乎同时启动」→ **新增 KI-335（新需求）**

### 4.1 已确认的代码事实（只读）

- `DecoupleTrain()`（`src\train_cmd.cpp:7698` 起）在**同一 tick** 内把两半都放行：
  - 引擎半：`u->SetFrontEngine(); u->vehstatus.Reset(VehState::Stopped);`（`:7719-7722`）
  - 车厢半：`R3RCreateCarOnlyFormation(u)` + `SetFrontEngine()`（`:7728-7731`）
- 两半随后各自走 `TrainLocoHandler` → `ProcessOrders`，**不存在任何"等对面板离开后我再启动"的互锁**。
- 唯一与"缝"有关的既有机制是 `R3RWaitingCoupleSeam()`（`:2240-2245`，配合 `R3RWaitingCoupleAtEnd()` `:2200` 一带），但它只用于**禁止朝接缝方向的反转 / 阻止朝缝预留路**（`:17403`、`:17422`），**不禁止启动**。

### 4.2 实现落点（待玩家拍板后做）

在 `TrainLocoHandler` 的停驻 / 启动判据处（`:17397-17422` 的 `r3r_may_reverse` 与 `LeavingStation` 段，以及 `:12251-12256` 一带的停驻区）加守卫：

> `R3RWaitingCoupleSeam(consist) != R3RSeamEnd::None`（我鼻尖/尾部顶着刚解下的对面板）**且** 对面板仍停着（`cur_speed == 0`）⇒ 本 tick 不启动。

判据原子可直接复用 `R3RWaitingCoupleAtEnd()`，不需要新几何。

### 4.3 复测判据（实现后）

解挂后，后车在前车**完全离开 seam**（速度非 0 且驶离至相邻格）之前 `cur_speed` 恒 0；前车驶离后后车按订单正常启动。

---

## 五、第 5 条（附件日志）——**子分组 A1 / A2 的两列车互相挂接**（新增 KI-336，待拍板）

玩家 2026-10-08 补充口径（原话）：

> *"还有第五条关于那个丢出来日志的，情况如下，有 A 挂接分组和子分组 A1、A2，我把一列车只放在 A1，一列只放在 A2，结果那两列竟然挂接配对了，这不是我期望的。"*
> *"关于第五点，我有初步的猜想了：我把列车 1 放在 A1 组，结果列车 1 就自动进了 A 组；列车 2 放在 A2，结果 2 也自动进了 A 组，于是就配对了。"*

⇒ **玩家的猜想逐字成立**（§5.1），但它只是"两条独立成因"里的第二条；现场日志里真正发生的那次配对，成因是第一条（§5.3）。

### 5.1 猜想核实：放进 A1 会"自动进 A 组" —— **成立，且是第 146 轮的既定设计，不是 bug**

- 存档里每节车只存**显式**掩码（`Vehicle::couple_groups`，`src\vehicle_base.h:387`）。
- 所有"读"统一走 `R3RGetEffectiveCoupleGroupsOfSegment()`（`src\couple_group.cpp:249-252`）= `R3RAddCoupleGroupAncestors()`（`:233-247`）**沿 parent 链逐级并入祖先位**。
- 读点（= 玩家能亲眼看到"它也在 A 组里"的地方）：
  - `src\couple_group_gui.cpp:190`（分组窗口右栏：该组下有哪些段）
  - `src\couple_group_gui.cpp:589`（段的"+ 其它组"列）
  - `src\couple_group.cpp:703`（**组内段计数**）
  - `src\depot_gui.cpp:790` / `:827`、`src\vehicle_gui.cpp:3477` / `:3502`（车队/车库列表的分组列与筛选）
- ⇒ 把车放进 A1 之后：**分组窗口 A 行立刻列出它、A 的段计数 +1、该车"其它组"里出现 A**。这正是第 146 轮玩家要求叁「石头里的列车自动也算滚木里的列车」的落地行为（KI-233），**展示层必须保持**（见 §5.7⑤）。

### 5.2 挂接白名单用的是**同一套含祖先的集合** ⇒ 兄弟组互相"相容"

- `R3RCoupleAllowedIgnoringPair()`（`src\couple_group.cpp:391-392`）取的正是**含祖先**的有效集合：
  `coupler_groups = R3RGetEffectiveCoupleGroupsOfSegment(coupler);` / `target_groups = R3RGetEffectiveCoupleGroupsOfSegment(target);`
- 相容判据 `R3RCoupleGroupMasksCompatible()`：两边非空时 `(a & b) != NONE` 即相容。
- A1 车 × A2 车：`A(A1) = {A1, A} = 0b011`、`A(A2) = {A2, A} = 0b101` ⇒ 交集 `{A} = 0b001 ≠ 0` ⇒ **判相容**。
- ⇒ 玩家猜想成立：两车因为"都被算进 A"，就在 A 这一点上相交，白名单放行。

### 5.3 但现场日志里那次配对，闸门**根本没跑**（成因二 / 更根本的一条）

- 实测计数：整个日志 **`CG-GATE` = 0 行**（`CG-GATE` 只在白名单闸门真正被走到时才打印）。
- 代码依据（KI-306 既定口径）：`R3RCoupleAllowedIgnoringPair()` 的**同公司**分支**只有**在命令显式声明了临时分组（`coupler_declared_temp || target_declared_temp`）时才调用 `R3RCoupleGroupMasksCompatible()`，否则直接 `return true` —— 即「同公司 + 命令上没挂临时分组 ⇒ **完全不看分组**」。
- ⇒ 现场"只放进 A1 / 只放进 A2"这件事对挂接**没有任何约束力**，两车本来就允许配对。**即使把 §5.2 的祖先继承去掉，这次配对依然会发生。**
- 佐证一（跨子组配对确实发生）：`CPL-PAIR act=23 tgt=10` —— `act=23` 归属 A1（`CGRP-NORM-UNION head=23 nseg=1 mask=2`），`tgt=10` 归属 A2（`head=10 nseg=1 mask=4`）。
- 佐证二（祖先继承在计数里确实生效）：`CG-PAINT` 打 `groups=3`，三组段计数 `sel=0 → 6` / `sel=1 → 2` / `sel=2 → 4`；**6 = 2 + 4** ⇒ 组 0（= A）自己没有直属成员，它的 6 个全部来自两个子组，与"祖先继承"逐字吻合。这也是"组 0 = A、组 1 = A1、组 2 = A2"的判据。

### 5.4 机制总结：**两条独立成因，缺一不可**

| # | 成因 | 现状代码 | 玩家只放 A1/A2 时是否拦截 |
| --- | --- | --- | --- |
| 1 | **同公司默认不看分组**（KI-306 口径）：命令上未声明临时分组 ⇒ 白名单恒放行 | `src\couple_group.cpp:391-392` 之后的同公司 `return true` | ❌ 不拦（**现场即此情形**） |
| 2 | **白名单使用"含祖先"的有效集合** ⇒ A1/A2 共享父组 A ⇒ 交集非空 | `:249-252` → `:391-392` → `R3RCoupleGroupMasksCompatible` | ❌ 不拦 |

⇒ 要让"子分组互相隔离"成立，**必须同时处理这两条**；只改一条都会留下另一半。

### 5.5 待拍板 ①：挂接白名单用哪套集合？（请玩家选一个）

记 `E(v)` = 显式所属组（**不含**祖先）、`A(v)` = 显式 + 全部祖先。

- **选项 1（最严）**：白名单只比 `E(a) ∩ E(b) != ∅`。
  - A1 × A2 → ∅ ⇒ **拒绝** ✅
  - A1 × A（父组车）→ ∅ ⇒ **也拒绝**（父子之间同样互不相容）
  - 展示/计数仍保留祖先继承（第 146 轮需求不受影响）
- **选项 2（父子和、兄弟隔）**：白名单改比 `(E(a) ∩ A(b)) ∪ (A(a) ∩ E(b)) != ∅`。
  - A1 × A2 → `{A1}∩{A2,A}` = ∅ 且 `{A1,A}∩{A2}` = ∅ ⇒ **拒绝** ✅
  - A × A1 → `{A} ∩ {A1,A}` = {A} ⇒ **允许** ✅（父组/父组的车仍能挂子组的车，保住"滚木管石头"的直觉）
  - A1 × A1、A × A ⇒ **允许** ✅
- **选项 3（维持现状）**：判据不动；只在文档里讲清"同公司必须挂临时分组才生效"，且 A1 × A2 **仍然相容**（共享 A）。
- **选项 4（把闸门默认打开但保留祖先）**：**不解决**本问题（A1 × A2 仍相容），且会回归 KI-195 的"忘分组 ⇒ 谁都挂不上"。**不建议**。

### 5.6 待拍板 ②：持久分组是否也该默认参与判据？（与成因 1 挂钩，独立问题）

即使选定选项 1/2，**只要命令上不声明临时分组，白名单依然完全不跑**（KI-306 口径）。所以还要选：

- **(i) 保持现状**：必须用「临时挂接分组」显式启用；持久分组（A/A1/A2）只承担"跨公司授权 + 展示/筛选"。（KI-195 的既有结论，最安全）
- **(ii) 默认打开**：同公司也一律查持久分组（空掩码按"隐式同组"处理）。缺点是"忘了分组"直接变成"谁都挂不上"—— KI-195 就是因此被否掉的。**不建议**。
- **(iii) 折中**：只有当**双方至少一方有持久分组**时才启用白名单；两边都无分组（空 × 空）仍旧放行。⇒ 给一部分车分了组就自动生效，完全没用分组的存档行为不变。

### 5.7 复测判据（拍板并实现后）

① 只放 A1 的车 × 只放 A2 的车：应出现分组否决日志（`CG-GATE` 家族）且**不产生** `CPL-PAIR` / `COUPLE-OK`；
② A（父组）的车 × A1（子组）的车：**选项 2 应仍可挂、选项 1 应被拒**；
③ 两边都无分组：选 (iii) 时仍可挂（**负判据**，防"忘分组即瘫痪"）；
④ 选 (i) 时：①② 只在命令声明了临时分组时才生效（与 KI-306 现行口径一致）；
⑤ **不得回退**：展示层"放进 A1 ⇒ A 组里也看得到 / 计数含子组"必须保持（第 146 轮 KI-233 需求）。

### 5.8 附：同日志的其它计数与旧推论（原 §5.1 ~ §5.4，保留供后查）

#### 5.8.1 计数（实测）

| 标签 | 次数 | 说明 |
| --- | --- | --- |
| `SKIP-STOPPED` | 307 | 停驻车跳过（`order=17` real=16 一带，WAIT_COUPLE） |
| `NOABSORB-SEG` | 83 | **全部 `n=0`**，`len=6/8/12` |
| `COUPLE-FAIL` | 59 | |
| `COUPLE-WAIT-OFFDEST` | 58 | |
| `stuck=1` | 33 | `MarkTrainAsStuck` |
| `CPL-GATE` | 28 | |
| `DECOUPLE-DONE` / `LOCO-AFTER-DECOUPLE` | 6 组 | 解挂正常发生 |
| `REVERSEDIR` | 5 | 全为 `db=1`，含 `veh=2 @1073,139 order=16`、`veh=11 @1073,139 order=16` |
| `COUPLE-DEST-EMPTY` | 0 | ⇒ **不是**"到点无目标"型 |

#### 5.8.2 关键锚点（抽样原文）

- `veh=2`：`order=16`（GOTO_COUPLE）、`real=3`、`destStn=1`、**`onStnTile=1`**（自己已在目的站台上）→ `CPL-PAIR act=2 tgt=3 dist=14` → `FSCP tile=1079,139 fail=tryReserve`, **`owner=11 otile=1080,140`** → `CPL-SAFE-FAIL` → `COUPLE-FAIL` → `DEPOT-ARR ... stuck=1`。
- `veh=11`：同一格 `1073,139`，`order=16`；`CPL-GATE reject=target-not-wait act=11 tgt=23 aOrd=16 tOrd=1`；随后 `REVERSEDIR veh=11 ... db=1`。
- `veh=23`：`CPL-GATE reject=pair-mismatch act=23 tgt=10 tOrd=17`（**目标合法在等**，但配对锁指向别车）。
- `NOABSORB-SEG` 语义（`src\train_cmd.cpp:6648-6660`）：段停在库内时"段永不吸收库内散车厢"守卫的每 tick 探针，`n` = 该格上 `IsFreeWagon()||IsFrontWagon()` 的数量，边沿去重按 `(段头, n)`。⇒ 83 行全 `n=0` 表示**库内几乎总是没有散车厢**，不是缺陷，而是"库内活动量很大"的指标。

#### 5.8.3 旧推论（当时口径：只给推论、不写 KI；现仍不作结论）

1. **这不是"找不到目标"，而是"找得到、但被另一台正在跑的机车锁住"**：`veh=2` 的耦合寻路（FSCP）在 `1079,139` 撞上 `veh=11` 的预留 ⇒ `CPL-SAFE-FAIL` ⇒ `COUPLE-FAIL` 循环。两台机车**互为对方的预留障碍**。
2. **多机车同区域时"谁挂谁"的仲裁自锁**：`veh=11` 想挂 `veh=23`（但 23 不是等待态 ⇒ `target-not-wait`）；`veh=23` 想挂 `veh=10`（10 确实是等待态 17，却 `pair-mismatch`，其配对锁指向别车）。⇒ 现场至少 3 台机车（2 / 11 / 23）在争抢不同目标，锁与闸门互相否证。
3. **自愈掉头打不破互锁**：`stuck=1` ×33 之后走 `REVERSEDIR db=1`（`:1073,139` 上两台都掉过头），掉头后重新寻路又回到同一冲突 ⇒ 与 KI-330 家族同源，属"同格/相邻格误判 + 自愈策略不适用"。
4. **大量长期等待者占用关键格**：`SKIP-STOPPED` 307 + `COUPLE-WAIT-OFFDEST` 58 ⇒ 现场有大量 `WAIT_COUPLE` 停驻车，正是第 1 点里把别人路堵住的来源。
5. `COUPLE-DEST-EMPTY = 0` ⇒ 与 KI-316/323 的"到点无目标"型**无关**；本条现场属 **KI-288 / KI-289 / KI-330 家族（闸门 + 互锁 + Stuck 自愈）**。
6. `DECOUPLE-DONE` 6 组说明解挂本身正常；若"难绷"发生在这 6 组之后，重点看解挂后**两半是否仍同格**（KI-330 场景）以及随后谁先启动（对照第 4 条）。

#### 5.8.4 现场澄清结果（2026-10-08 玩家已答）

- 玩家对第 5 条的澄清 = **子分组 A1 / A2 的两列车互相挂接**（见本报告 §五 开头引文），与上面的"多机车互锁 / 难绷"推论**不是同一件事** ⇒ 上述推论**降级为旁证**，不转 KI，仅作为 KI-288 / KI-289 / KI-330 家族的一份现场样本保留。
- 上述第 5 点（`COUPLE-DEST-EMPTY = 0`）仍然有效：该日志**不是**"到点无目标"型。

---

## 六、待玩家拍板 / 澄清清单（截至 2026-10-08，已更新）

1. ~~**KI-332**：克隆品继承全套段特质还是只继承"段的划分 + 段名"？~~ ⇒ **已拍板**，见 §七.1。
2. ~~**KI-321**：车库型复测，还是把"先到目的地再扫描"推广到站台型？~~ ⇒ **已拍板**，见 §七.2。
3. **KI-334**：**仍待玩家提供**双头机车挂接场景日志（判 `FOLD-V` / `FOLD-U` 双头两半 `dir`）。
4. ~~**KI-335**：解除条件用"离开 seam"还是"离开 N 格"？~~ ⇒ **已拍板**，见 §七.3。
5. **KI-336（第 5 条）**：**新增两问待拍板** —— §5.5（白名单用哪套集合：选项 1 / 2 / 3）与 §5.6（持久分组是否默认参与：i / ii / iii）。

---

## 七、玩家拍板记录（2026-10-08）

### 7.1 KI-332 —— 克隆品的段特质继承口径

- **拍板**：**(a) 全套继承** —— 段名 / 挂接分组 / 优先级 / 段 ID 行**逐段照搬**（最忠实）；
- **唯一例外**：**段的列车编号（`unitnumber` / `unitnumber_backup`）不继承**，克隆品按正常流程另分配新号（即"段号可能与原链冲突"这一顾虑按"不继承"处理）。
- **落地含义**：`CloneVehicle()` 的锁步遍历里要补 `name`（逐段）+ `couple_groups` + `r3r_priority` + 段 ID 行；`unitnumber` **保持现状**（新号），`unitnumber_backup` 一并不复制。
- **待办**：先扩 `CLONE-SRC` / `CLONE-DST` 探针（把段名字段加进去），再改码；复测判据见 KI-332。

### 7.2 KI-321 —— 「无论如何直接前往车库再扫描」的适用范围

- **拍板**：**仅车库型，维持现状** —— 即只**确认**第 213 轮已落地的 M2（`src\train_cmd.cpp:13414-13497` 跳过 CPL 直驶入库 + 库内扫描 `:11800-11829`）；
- **站台型保持不变**：继续按 KI-316 / KI-323 的「**原地等待 = 正确行为**」口径，**不**推广"先到目的地再扫描"。
- **落地含义**：KI-321 **无需再改码**，只需按 §2.2 的三条做复测 + 清理同源过期描述（`COUPLE-DEST-EMPTY` 的注释与 `:10089` 条目仍写"满 `R3R_COUPLE_DEST_IDLE_LIMIT` tick 跳过命令"，实为"原地无限等待、保留订单"）。

### 7.3 KI-335 —— 解挂后"后车等前车离开"的解除条件

- **拍板**：**前车完全离开站台 / 驶离 N 格**（不采用"离开 seam"的最简口径）。
- **落地含义**：需要在 `R3RWaitingCoupleAtEnd()` 之外**新增几何判据**（前车相对站台/相对本车的位置或格数），属新实现；误停风险略高，故实现时必须带**负判据**（正常追尾 / 同站两列车不得被误停，见 KI-335）。
- **顺序**：排在 KI-334 之后（都是挂接/解挂几何族，先拿双头日志再定）。

---

## 八、第 224 轮：KI-336 拍板与实现（挂接分组白名单）

**日期**：2026-10-08　　　**本文属"临时分析报告"**（长期规则要求：明确需求 + 根因 + 修法后立即记入备忘，再动代码）。

### 8.1 玩家需求（原话）

> 第一点，采用选项2，我希望这样舅舅和外甥也不匹配（可以推广到舅公和旁系晚辈这样子）
> 第二点，无论公司都要相同挂接分组，但是，不同公司的两个无分组视作不同分组，相同公司的两个无分组才视作相同分组

即：**需求 ①** = §5.5 的 **选项 2**；**需求 ②** = §5.6 的 **(ii) 同公司一律查持久分组**，外加"无分组"的按公司区分语义。

### 8.2 拍板落地口径

- **① 集合口径 = 选项 2（只认直系）**。设 `E(x)` = 段落上**存储的（显式）**掩码，`A(x)` = `E(x) ∪ 全部祖先`（既有 `R3RAddCoupleGroupAncestors()`）。判据改为
  `match(a,b) = (E(a) ∩ A(b)) ∪ (A(a) ∩ E(b))`，相容 ⟺ `match ≠ ∅`。
  - 同组 `A×A`、父子 `A×A1`（A1 的 parent 是 A）、祖孙 `A×A1a` 都 ∋ 祖先那一项 ⇒ **可挂**；
  - 兄弟子组 **`A1×A2` 拒** —— 两侧只共享祖先 `A`，而 `A ∉ E(A1)、A ∉ E(A2)`；
  - **"舅舅和外甥"拒** —— 外甥 `E={N}`、`A(N)={N,P,G}`；舅舅 `E={U}`、`A(U)={U,G}`（`U` 与 `P` 是兄弟）⇒ 两个交项**都是空集**；
  - **"舅公和旁系晚辈"拒** —— 旁系晚辈 `E={D}`、`A(D)={D,N,P,G}`；舅公 `E={U}`（`U` 只与 `G` 同辈的那一支）⇒ 仍都是空集。
    结构性理由：`match` 的两项分别要求"一方**显式**拥有的组出现在对方的祖先链里"，而旁系关系里**只有共同祖先**进入双方集合，共同祖先不是任何一方的显式元素 ⇒ 恒空。
- **② 启用条件 = 无条件**：撤回 KI-306 的"命令声明了临时分组才启用白名单"；KI-195 的"同公司不设门槛"随之作废（玩家原话"无论公司都要相同挂接分组"）。同公司、跨公司**两条路径都过白名单**。
  - 临时分组（`GetCoupleTempGroup()`，KI-170 / KI-225）语义**不变**：它只是给本方 `E` 多并进一个真实分组位，再走同一判据（不再是"启用开关"）。⇒ 现场口径仍是"声明滚木的机车能挂滚木组（或其父/子组）的车底"，而"声明了分组就只能挂该分组"（KI-306 想治的"设了与没设一样"顺带被治）。
- **③ 无分组语义（需求 ② 后半）**：`E(a)=E(b)=∅` 时 —— **同公司** ⇒ 同一个隐式组 ⇒ **可挂**；**不同公司** ⇒ **不同组 ⇒ 不可挂**。
  - 同公司之所以要特判：`match` 公式对双空集会给出 ∅，会把"两边都没分组"判死，与玩家口径相反 ⇒ 在 `R3RCoupleGroupMasksCompatible()` 里保留 `a==NONE && b==NONE ⇒ true`（原 `a==b` 分支收窄为"双空"）。
  - 跨公司不需要特判：跨公司走 `R3RCoupleGroupMasksAllowCrossCompany()`，它的匹配集合就是同一个 `match(a,b)`，双空集 ⇒ ∅ ⇒ 直接 `false`，与现状（隐式组不能承载对外授权）一致。

### 8.3 根因（与 §五 的核实一致，此处补"为什么改这三处就够"）

1. **成因 1 = 同公司默认不过闸门**：`R3RCoupleAllowedIgnoringPair()` 的同公司分支只有 `coupler_declared_temp || target_declared_temp` 时才调 `R3RCoupleGroupMasksCompatible()`，否则 `return true`（KI-306）。现场 `CG-GATE` **0 行** ⇒ "只放进 A1 / 只放进 A2"对挂接毫无约束。
2. **成因 2 = 判据用含祖先的有效集合**：`:391-392` 取 `R3RGetEffectiveCoupleGroupsOfSegment()`，`{A1,A} ∩ {A2,A} = {A} ≠ ∅` ⇒ 兄弟子组判相容。
3. 两个比较函数（`R3RCoupleGroupMasksCompatible` / `R3RCoupleGroupMasksAllowCrossCompany`）**全代码库只有本文件两处调用**（`:480`、`:459`），所以改这两个函数的语义 + 调用点传参口径即可，**不影响展示层**（`couple_group_gui.cpp:190/589`、`couple_group.cpp:703`、`depot_gui.cpp:790/827`、`vehicle_gui.cpp:3477/3502` 仍走有效集合，KI-233 第 146 轮的口径**不回退**）。

### 8.4 修法（`src\couple_group.cpp` + `src\couple_group.h` / `src\order_base.h` 文档）

- `couple_group.cpp`：新增文件内 `R3RCoupleGroupMatchingSet(a,b)`（选项 2 的 `match` 集合，内部各补一次祖先），`R3RCoupleGroupMasksCompatible()` 改用它（双空集特判保留），`R3RCoupleGroupMasksAllowCrossCompany()` 把 `a & b` 换成同一 `match`；
- `R3RCoupleAllowedIgnoringPair()`：`:391-392` 由"有效集合"改为**存储集合**（`R3RGetCoupleGroupsOfSegment`）；同公司分支去掉 `coupler_declared_temp || target_declared_temp` 条件，**无条件**判相容并打探针；
- `R3RCoupleGateProbe()`：顺手修掉"表满（64 对）后未知键每次都写一行"的潜在刷屏（现在闸门变成热路径，这个洞必须堵）——表满即静默返回；
- 注释/文档同步：`couple_group.h` 两个函数的 doxygen（现行文本写的是"空集算一个隐式组 / 非空交集即可"，已不成立）、`R3RGetEffectiveCoupleGroupsOfSegment()` 的"跨公司闸门必须用它"一句、`couple_group.cpp` 里 KI-306/KI-195 的整段历史注释。
- **不改**：段上存储的数据、存档格式、UI 展示口径、跨公司"对外开关"（`CGF_ALLOW_OTHERS`）语义、临时分组的数据/UI。

### 8.5 复测判据（替换 KI-336 里按 (i)/(iii) 写的那两条）

1. **只放 A1 × 只放 A2（同公司）** ⇒ 不产生 `CPL-PAIR` / `COUPLE-OK`，且日志出现分组否决（`CG-GATE compatible=0` 与/或到点闸门 `reject=group-mismatch`）；
2. **A × A1（同公司）** ⇒ 可挂（`CG-GATE compatible=1`），一个子组的车挂得上父组的车；
3. **舅舅 × 外甥、舅公 × 旁系晚辈** ⇒ 均拒（同 1 的判据）；
4. **同公司、两边都无分组** ⇒ 仍可挂（负判据，必须不回归）；
5. **跨公司、两边都无分组** ⇒ 拒（负判据）；**跨公司、共享一个"对外"组** ⇒ 可挂（不回归）；
6. **同公司、一边有分组一边没有** ⇒ 拒（这是本轮新增的收紧，属**预期行为变更**，需玩家确认是否符合本意）；
7. **展示层不得回退**：放进 A1 的段仍出现在 A 的列表里、A 的段计数仍含子组（KI-233）；
8. **性能/日志**：闸门进入寻路热路径后，`CG-GATE` 不得刷屏（每对车至多一次、verdict 变化才再写、表满静默）。

### 8.6 风险与未做

- **风险 R-1（最大）**：需求 ② 的"无条件白名单"会让"忘记把车底加进分组"重新变成 KI-195 抱怨的"到了站台找不到挂接目标"（表现为 `CPL-PAIR` 不产生 + `COUPLE-FAIL` / `CPL-PATHFOUND found=0` 反复）。这是玩家明确拍板的口径，但**复测必须专门盯这一路**；若现场不符预期，回退点 = 把同公司分支的条件恢复成 `coupler_declared_temp || target_declared_temp`（一行）。
- **未做**：未做"分组删掉后段上残留位"的新逻辑（沿用既有 sanitise）；未给白名单加新的调试窗口/UI 提示（玩家可能看不出"为什么挂不上"）；`CG-GATE` 只在同公司分支打（跨公司分支仍靠既有 `CPL-GATE` 判读）。
- **构建**：本轮改了 `src\couple_group.h` + `src\order_base.h` ⇒ 护栏判定 `2 header/lang file(s) are newer than the newest object file` → `REMOVED 620 object file(s) - upgrading this build to a FULL rebuild`，走**全量**（692 步，复用既有 `_tmp_inc_build.cmd`，未新建任何 .cmd）。
  - 结果：`[692/692] Linking CXX executable openttd.exe`；`build\R3R_incbuild.done` = `EXIT_CODE=0`；日志里 `error C|fatal error|FAILED:|build stopped` 计数 **0**；`src\couple_group.cpp`/`couple_group.h`/`order_base.h` 均 20:25 → `couple_group.cpp.obj` 20:39 → `build\openttd.exe` 2026-10-08 20:49（51 677 696 B）；`read_lints` 三个文件 **0 条**。
  - 踩坑记录：上一轮用 `Start-Process -ArgumentList '/c','"d:\sourcecode of JGRPP\_tmp_inc_build.cmd"'` 启动**没跑起来**（路径含空格，参数被拆碎，cmd 秒退且不留痕迹），而 `build\R3R_incbuild.*` 三个文件都停留在**前一天 20:32** 的旧内容上，差点被误当成"构建已完成"。正确做法 = `Start-Process -FilePath 'cmd.exe' -ArgumentList '/c','_tmp_inc_build.cmd' -WorkingDirectory 'd:\sourcecode of JGRPP' -WindowStyle Minimized`（参数里不放含空格的路径）。⇒ **判据必须看时间戳，不能只看 done/guard 文件存在与否。**
  - **仍未做**：游戏内复测（`R3R_KNOWN_ISSUES.md` 的 KI-336 复测判据 ①~⑦）。

---

## 九、第 225 轮：KI-332 实现（克隆链逐段继承段特质）

**日期**：2026-10-08　　　**本文属"临时分析报告"**（长期规则：明确需求 + 根因 + 修法后**立即**记入备忘，再动代码；本次先写本节、再改码）。

### 9.1 玩家需求（原话）

> 当我们复制一条多段组成的链的时候，段的特质的继承会出现一些问题

### 9.2 拍板口径（§7.1 的落地映射）

- **(a) 全套继承**：段名 / 挂接分组 / 优先级 / 段 ID 行 **逐段照搬**；
- **唯一例外**：`unitnumber` / `unitnumber_backup` **不继承**，克隆品按正常流程另分配新号。

### 9.3 代码事实与根因（为什么"物理层忠实、身份层全丢"）

- `CloneVehicle()`（`src\vehicle_cmd.cpp`）的锁步遍历（src/dst 各用 `GetNextVehicle()` 对齐）只镜像四样东西：引擎/假引擎位、段边界 ★/⊗（`R3RMirrorSegmentMarkers()`）、`VehState::Stopped`、真铰接打散（`R3RDearticulateOneGroup()`）。
- `r3r_segment_id` 是**段特质行的主键**，只在 `R3RSettleChainSegments()` / `R3RSyncSegmentTraits()`（**读档与提交点**）里分配/重建。克隆路径不走这两个函数 ⇒ 克隆品**一个段 ID 都没有** ⇒ 没有行可写 ⇒ 段名/分组**无处安放**。
- `CloneVehicleName()`（`:1542`，调用点 `:2479`）只处理**链头**（源链头名 + 序号），段名逐段保持默认值。
- ⇒ 根因 = 「克隆 = 物理层忠实复制 + 段身份层全部回落默认值」，缺的是**第 4 层：段身份数据本身的复制**（KI-129 只解决"能不能造出来"、KI-223/第 179 轮只解决"★/⊗ 还在不在"、KI-247 只解决"读写口径"，三层都不覆盖它）。

### 9.4 修法（三个文件、五处落点，全部新增，不动既有判定逻辑）

1. **`src\couple_group.cpp`** 新增公开函数 `R3RCopyClonedSegmentTraits(const Train *src_chain, Train *dst_chain)`（原型声明在 `src\couple_group.h`，紧邻 `R3RSegmentStoreTraits`）——锁步遍历两条链，逐车照搬：
   - `name_backup` / `group_id_backup`（**停放副本**）。这是关键：提交点 `R3RBorrowControlTraits()` 优先读停放副本，**只写段行会被下一次 `R3RSettleChainSegments()` 冲掉**；
   - 活字段 `name`（隐藏段段头照搬自己的名；**链头不搬名**——链头的名字由 `CloneVehicleName()` 接管，且活字段在借用层生效时是"控制段那一套"）与 `group_id`；
   - 段头（`d == dst_chain || d->IsSegmentFront()`）额外照搬 `couple_groups`（取 `R3RGetCoupleGroupsOfSegment(s)`，与源段同一口径）与 `r3r_priority`，并把 `R3RSegmentSectionName/GroupID(s)` 写进克隆品**该段自己的行**（`R3RSegmentGet(d->r3r_segment_id)`，分组 `DEFAULT_GROUP` 归一成 `Invalid`）；
   - `unitnumber` / `unitnumber_backup` **一个都不碰**（拍板例外）；探针 `CLONE-TRAITS dst= n=`。
2. **`src\train_cmd.cpp`** 新增**非 static** 入口 `void R3RSettleClonedChain(Train *chain)`（值 = `R3RSettleChainSegments(chain, "clone")`，紧跟其定义，只有一处调用需求故不碰头文件）；同时把 `settle-clone` 补进 `R3RSettleWorthDumping()` 的标签映射与 `R3RSettleChainSegments()` 的 `@param tag` 文档。
3. **`src\vehicle_cmd.cpp`** 顶部手写前向声明 `void R3RSettleClonedChain(Train *chain);`（跨文件调用；本轮唯一需要动头文件的地方是第 1 条的声明）。
4. **`src\vehicle_cmd.cpp` 的 `CloneVehicle()`**：在"锁步遍历 + `ConsistChanged(CCF_ARRANGE)`"之后插一段（无条件、带车类型守卫）：
   `R3RSettleClonedChain(w_head)` → `R3RCopyClonedSegmentTraits(v_head, w_head)` → `R3RSettleClonedChain(w_head)` → `R3RDbgCloneTraits(v_head, w_head)`。
   **为什么先 settle 后复制再 settle**：第一次 settle 给克隆品**发段 ID 建行**（否则无处可写）；复制之后再 settle 一次，让行内容按"提交点同款"从复制后的状态推导（含控制段的借用镜像、隐藏段从停放副本回填）。
5. **新增只读探针 `R3RDbgCloneTraits()`**（放在 `R3RDbgCloneMarkers()` 之后）：每个段一行
   `CLONE-TRAIT seg= src= dst= prio=-> grp=-> cgrp=0x->0x name=""->""`。
   **为什么不用既有 `CLONE-SRC`/`CLONE-DST`**：那两个 dump 挂在 `r3r_identity_rebuilt` 分支内（只有"源链带打散铰接组"时才打），而本轮最典型的**单段**场景（一列有名字、有分组的车）压根没有 ★/⊗，会被闸门屏蔽；故按 §7.1 的待办"把段名字段加进去"，改为**每次克隆都打**（探测开时），原 dump 一字不动。

**关于"段 ID 行逐段照搬"的口径澄清**：**不能字面共享 ID**——ID 是段的唯一标识，两条链共享同一行会互相覆盖。实现取"**给克隆品分配新 ID，把源段行的内容照搬进新行**"，语义上等价于"这一段的整套特质跟过去了"，与拍板 (a) 一致。

### 9.5 复测判据（KI-332 判据的落地化）

1. 克隆一条 3 段链 ⇒ `CLONE-TRAIT` 至少 3 行，每行 `name=` 左右一致、`grp=` 一致、`prio=` 一致、`cgrp=` 一致；
2. `CLONE-TRAITS dst= n=` 的 `n` = 克隆节数；
3. **负判据**：克隆品的段号与新分配号一致（**故意不同**于源，不得把源号搬过来）；
4. 克隆后把它开到站台/车库做一次解挂/耦合，段名与分组**仍在**（没被下一次 settle 冲掉）；
5. 单段带名带分组的车克隆 ⇒ 名字仍带序号（`CloneVehicleName()` 不回退）、分组也跟着走；
6. **负判据**：无分组的普通列车克隆 ⇒ `cgrp=0x0->0x0`、`name=""->""`，不误伤。

### 9.6 风险与未做

- **R-1**：链头活字段 `group_id` 被照搬后，紧随其后的 `AddVehicleToGroup`（`:2412` 一带，把克隆品加进源链的分组）目标是同一个组 ⇒ 幂等，无风险。
- **R-2**：`R3RSettleClonedChain()` 对**单段链**同样会跑一遍（ID 分配器空转 + 探针）。语义与"解挂/车库编辑提交点"一致，无新增崩溃面，但探测开时每次克隆都会多出 `settle-clone` 的 GEO dump。
- **未做（本轮）**：游戏内复测（判据见 9.5）；`CLONE-TRAIT` 只 dump 了 **section 版**（本段自己的名/组），没有额外 dump 链头"活字段"那一份——若复测发现"链头显示的名字不对"，需要再加一列；`R3R_KNOWN_ISSUES.md` 的 KI-332 状态更新与「未做」清单（本轮结束一并改）。

### 9.7 构建自证（第 225 轮）

- 复用既有 `_tmp_inc_build.cmd`（**未新建任何 .cmd**）。
- **护栏判全量**：`build\R3R_incbuild.guard.log` = `REMOVED 620 object file(s) - upgrading this build to a FULL rebuild`（`src\couple_group.h` 21:34 晚于当时最新 obj）——与本轮改动范围（1 个头文件 + 3 个 .cpp）一致，属预期，非异常。
- `build\R3R_incbuild.done` = **`EXIT_CODE=0`**；日志尾部 `[692/692] Linking CXX executable openttd.exe`；`error C[0-9]` / `fatal error` / `FAILED:` / `build stopped` 计数 **0**。
- 时间戳链：`src\couple_group.cpp` / `couple_group.h` / `train_cmd.cpp` 21:34 → obj 21:51 / 21:58；`src\vehicle_cmd.cpp` 21:39 → obj 21:58；`build\openttd.exe` **2026-10-08 22:01（51 680 768 B）**，晚于全部所改源码。
- `read_lints`（`vehicle_cmd.cpp` / `couple_group.cpp` / `couple_group.h` / `train_cmd.cpp`）**0 条**。
- 产物自证：`openttd.exe` 内 **4/4** 命中新字面量 `CLONE-TRAITS dst=` / `CLONE-TRAIT seg=` / `settle-clone` / `R3RSettleClonedChain`。

**结论**：KI-332 的修法已实现 + 已编译通过，**游戏内复测待做**（判据见 9.5）。2026-10-10 复测的取证见 §十（判据 ①②③ 通过）。

---

## 十、第 226 轮（2026-10-10）：KI-333 克隆多段链时"隐藏段名字"不与原链去重（已修 + 实机复测通过）

### 10.1 玩家报告与定位

- 第 225 轮（KI-332）落地 `R3RCopyClonedSegmentTraits()` 后，克隆品的每一段都**逐字照搬**源段的名字；改名只有 `CloneVehicleName()` 一条路径、**只作用于链头**（`:1542`，调用点 `:2479`/`:2653`）⇒ **隐藏段沿用源段原名**，克隆品与原链在隐藏段上重名。
- 现场（玩家 2026-10-08 复测）：克隆一条两段链后，链头已变成 `关西机场快速02`，第二段仍显示 `纪州路快速01`（与原车第二段同名）。
- ⇒ 这是 KI-332「段身份数据照搬」带入的**重名副作用**：照搬是对的，缺的是"照搬之后再逐段唯一化"。

### 10.2 修法（仅 `src\vehicle_cmd.cpp`，未碰任何 `src\*.h`，未新增语言串）

1. `:1580` 新增 `template <typename IsUnique> static std::string R3RIncrementedName(std::string name, IsUnique is_unique)`：把原 `CloneVehicleName()` 的核心（"不以数字结尾则补 ` 1`，再自增末位数字直至 `is_unique()` 通过"；`isdigit` 的 `unsigned char` 强转与空串守卫照旧）抽出来，供链头与隐藏段共用。
2. `:1631` `CloneVehicleName()` 退化为薄壳：`R3RIncrementedName(名字, IsUniqueVehicleName)` 后写 `dst->name` ⇒ **链头行为逐字不变**。
3. `:1672` 新增 `static bool R3RSegmentNameInUse(const std::string &name)`：候选项是否已被占用 —— 遍历 `Vehicle::Iterate()` 比 `v->name` 与 `v->name_backup`，再遍历段行池（`R3RSegmentGet(1..R3RSegmentPoolSize())`）比 `row->name`。
4. `:1708` 新增 `static void R3RUniquifyClonedSegmentNames(const Vehicle *dst_chain)`：对 `R3RSegmentHiddenHeads(dst_chain)` 的每一个隐藏段，取"**停放副本优先、否则活字段**"的旧名（与隐藏段段特质同口径；链头只有停放副本算自己的），跑 `R3RIncrementedName(旧名, !R3RSegmentNameInUse)`，把新名**写回源用的那个字段**（parked ⇒ `car->name_backup`，否则 `car->name`），使随后 settle 派生的段行拿到新名。
5. `:2655` `CloneVehicle()` 里 `CloneVehicleName()`（`:2653`）之后追加：`if (列车) { R3RUniquifyClonedSegmentNames(w_front); R3RSettleClonedChain(Train::From(w_front)); }`。

**顺序不变式**：链头改名 → 隐藏段改名 → **再 settle 一次重推段行**（改名必须在 settle 之前，否则行会被旧名覆盖）。

**为什么不在 `R3RCopyClonedSegmentTraits()` 里改名**：改名要查"名字是否已被占用"，而链头的新名（`CloneVehicleName()` 的产物）必须先进入候选命名空间，否则隐藏段可能生成一个与链头**新名**相同（而链头那时还叫旧名）的候选；放在 `CloneVehicleName()` 之后可保证两者互斥。

### 10.3 构建自证（第 226 轮）

- 复用既有 `_tmp_inc_build.cmd`（**未新建任何 .cmd**）；本轮只改 1 个 `.cpp`。
- `build\R3R_incbuild.guard.log` = `GUARD: incremental is safe (no header/lang file is newer than the newest object)` ⇒ 增量合规（无"REMOVED 620"）。
- `build\R3R_incbuild.done` = **`EXIT_CODE=0`**（2026-10-10 07:58:51）。
- 时间戳链：`src\vehicle_cmd.cpp` **07:56:31** → `vehicle_cmd.cpp.obj` **07:57:29** → `build\openttd.exe` **2026-10-10 07:58:47（51 680 768 B）**，晚于所改源码。
- `read_lints`（`vehicle_cmd.cpp`）**0 条**。

### 10.4 实机复测（玩家 2026-10-10 08:05 日志：613 行 / 57 322 B，跑的就是上面那个 exe）

会话顺序：读档（`SEGLOAD-PARK` / `SEGLOAD-ROW`）→ 库内把原两段链整理好（`SEGTR-SNAP tag=depot-edit head=23 nseg=2`，seg9 `关西机场快速01` / seg10 `纪州路快速01`）→ **点克隆**（`UNIT-NEW` 逐节、每节一条 `settle-depot-edit`）→ 克隆收尾 → 随后玩家在库里继续拆分/重组克隆品。

克隆收尾的完整链路（行号为日志行号）：

- **491** `CLONE-MARKERS src=23 dst=61 cars=16 srcSF=2 dstSF=2 srcSB=2 dstSB=2 diff=0`
- **492** `SEGID-SYNC tag=clone head=61 nseg=2`；**493** `INVAR-CTRL tag=clone head=61 nseg=2 ctrl=61 ctrl_pri=1 head_pri=1 borrowed=0`
- **494-509** `GEO settle-clone n=0..15`（segid 11 = veh 61-68、segid 13 = veh 69-76）；**510** `GEO-END settle-clone head=61 n=16 capped=0`
- **511-513** 第 1 次 `SEGTR-SNAP tag=clone`（名字/分组仍为空、gid=65534 —— settle 时改名还没跑）
- **514** `CLONE-TRAITS dst=61 n=16`（`R3RCopyClonedSegmentTraits()` 汇总 ⇒ KI-332 判据 ② 成立）
- **515-537** 第 2 次 settle（`SEGID-SYNC` / `SEGTRAIT-SYNC` / `INVAR-CTRL` / GEO / SEGTR-SNAP，名字仍空）
- **538** `CLONE-TRAIT seg=0 src=23 dst=61 prio=1->1 grp=1->1 cgrp=0x2->0x2 name="关西机场快速01"->""`
- **539** `CLONE-TRAIT seg=1 src=22 dst=69 prio=2->2 grp=1->1 cgrp=0x2->0x2 name="纪州路快速01"->"纪州路快速01"`
- **561** `GEO-END settle-clone head=61 n=16 capped=0`；**562-564** **第 3 次 settle** 的 `SEGTR-SNAP tag=clone head=61 nseg=2 ctrl=61 borrowed=0`：
  - `i=1 id=11 front=61 star=1 ctrl=1 | live u=7 n="关西机场快速02" gid=1 | bk u=0 n="" gbk=-1 | row use u=7 n="关西机场快速02" g=1`
  - `i=2 id=13 front=69 star=1 ctrl=0 | live u=0 n="" gid=1 | bk u=0 n="纪州路快速02" gbk=1 | row use u=0 n="纪州路快速02" g=1`
- **565** `ORD-IDX-WATCH site=settle-out tag=clone veh=61 real=0->0`；**566 起** 20 条 `SEGTR-SNAP tag=depot-edit`（玩家拖动克隆品：nseg 4→…→1，末条 `CGRP-NORM-UNION head=61 nseg=1 mask=2`）。

**判读**：

1. **修复生效**：链头段（segid 11）落到 `关西机场快速02`，隐藏段（segid 13，front=69）落到 `纪州路快速02`，与原车 `…01` 不再重名；`row use` 列与"停放副本/活字段"一致 ⇒ **段行也拿到新名**（第 3 次 settle 的目的达成）。玩家结论 = "目前尚未发现问题"。
2. `CLONE-TRAIT` 的 `seg=0 … name="关西机场快速01"->""` 是**设计内的中间态**：该 dump 在 `CloneVehicleName()` **之前**打（探针 `:2578`、改名 `:2653`），此刻链头活字段还没换、其段名按"活字段优先"读成空。**判据必须看 `SEGTR-SNAP tag=clone`（第 3 次 settle）**，不能用 `CLONE-TRAIT` 的 `seg=0` 行判"链头没改名"。
3. 全日志 **0 条** `INVARIANT-BROKEN` / `MISMATCH` / `assert` / `error` / `crash` / `REFUSE` / `REJECT` 行。
4. KI-332 判据 ①（`CLONE-TRAIT` 每段 prio/grp/cgrp 左右一致：`1->1`/`2->2`、`grp=1->1`、`cgrp=0x2->0x2`）、②（`n=16` = 节数）、③（克隆品段号 11/13 为新分配，非源段 9/10）本次**通过**。

### 10.5 待补 / 未做（第 226 轮）

- `R3RUniquifyClonedSegmentNames()` **自身不打探针**，验证只能依赖克隆收尾那次 `SEGTR-SNAP tag=clone`；若将来去掉那次 settle 就失去判据（需一并补探针）。
- KI-332 判据 ④（克隆后开到站台做一次**解挂/耦合**，段名与分组仍在）本次**未覆盖**：日志里的后续 20 条 `tag=depot-edit` 是**库内拖动**，只能作"经车库编辑不丢名/组"的旁证（末条被并成 `nseg=1`、`CGRP-NORM-UNION mask=2`）。
- KI-332 判据 ⑤（单段带名带分组克隆）与 ⑥（负判据：无分组普通车 `cgrp=0x0->0x0`、`name=""->""`）本次未覆盖（源链两段都带名字与分组）。
- 未处理"克隆品名字与**别的列车**重名"的相反情形（`R3RSegmentNameInUse()` 已把全车池纳入候选检查，属已覆盖，但无实测样本）。

---

## 十一、第 227 轮（2026-10-10）：克隆隐藏段排程 + 编号勘误

### 11.1 本条与「五问」的关系

玩家 2026-10-08 报的五问已分别落在 KI-332（§九）、KI-321（§七）、KI-334/`KI-337`（§十一，克隆隐藏段排程）、KI-335（§四）、KI-336（§八）。其中"克隆多段链"这一族在 §九（KI-332 段特质）、§十（KI-333 段名去重）之后又暴露出**第三块**——**隐藏段自己的排程（`orders_backup`）没被克隆**，即本轮 `KI-337`。

### 11.2 编号勘误（重要）

- 本条首次登记时误用了 **`KI-334`**，而该号已被**第 223 轮 第 3 条**（双头机车挂接时机车错位，`R3R_KNOWN_ISSUES.md:10529`）占用。
- 正名后：**克隆隐藏段排程 = `KI-337`**；`KI-334` 仍专指双头机车。
- 已同步修正：`R3R_KNOWN_ISSUES.md` 的「第 227 轮」小节标题与状态、`R3R_clone_segment_orders_round227_memo.md` 的表头与 §七。
- 待办提醒：五问清单本身**没有**`KI-337` 这一号，后续引用勿再写 `KI-334`。

### 11.3 实机复测：通过

- 日志 `D:\temp\codebuddy-dropped-files\f97696fa-24a5-46d5-91ae-cc10e7f1565d\R3R_debug.log`（08:29:39，613 行 / 57 439 B），exe = `build\openttd.exe` 08:26:10。
- 关键三行：`513 CLONE-ORD dst=69 src=23 n=7 … borrowed=1 bk=0000019871302EF0`、`584 … veh=69 … n=-1->7`（失败轮为 `n=-1->-1 ord=0`）、`606 … veh=23 … n=-1->7`（源未受影响）。
- 判据 ①②③⑥ 通过，④⑤⑦ 无样本（详见 `R3R_clone_segment_orders_round227_memo.md` §七）。

### 11.4 五问当前状态一览（2026-10-10 更新）

| 序 | 条目 | 状态 |
| --- | --- | --- |
| 1 | KI-332 克隆段特质 | 已修（第 225 轮）+ 复测通过（§十） |
| 2 | KI-321 车库挂接直驶 | 已实现（第 213 轮）+ 玩家拍板"仅车库型维持现状"⇒ 无需改码，只剩**清理过期描述** |
| 3 | KI-334 双头机车错位 | **未修、证据待补**（无该场景日志）⇒ 本轮补**只读探针** |
| 4 | KI-335 解挂启动互锁 | **未实现**（玩家已拍板口径：前车完全离开站台 / 驶离 N 格） |
| 5 | KI-336 A1/A2 互挂 | 已修（第 224 轮）+ 待复测 |
| 追 | **KI-337** 克隆隐藏段排程 | **已修 + 复测通过**（§十一） |

