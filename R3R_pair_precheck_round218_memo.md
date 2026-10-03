# R3R 第 218 轮临时分析报告

## 主题：把可行性验证提前到「挂接配对」阶段 —— 评估与落点

- **日期**：2026-10-03
- **状态**：**取证 + 方案评估完成 · 未改任何源码 · 未构建**
- **玩家原话**：「我们能不能在进行挂接配对的时候就验证挂接可行性，如果不行就拒绝配对」
- **与前序的关系**：第 217 轮（`R3R_arrange_anti_clip_round217_memo.md`，KI-326）建议的闸门在**提交点**（`CheckTrainAttachment` 之前）。本轮玩家把闸门**再往上游推一格**，落到**配对建立点**。两者是**串联的两道**，不是替代关系。
- **取证范围**：`src/train_cmd.cpp`（本轮只读，未改一个字符）。

---

## 一、结论先说

1. **能，而且代码里已经有一半了。** 配对期的"可行性预检"有现成先例，且有两处现成落点（第 212 轮 KI-319 的 `R3RCoupleTargetAtOrderStation()` 就是同一手法）。
2. **最有价值的发现：现有配对期闸门存在一处真实不对称缺口，而它恰好就是第 172 轮穿模的前因。** 补上它，用户这个方案能**单独消灭一个已确认的根因**。
3. **但配对期只能做「静态」预检**（与位置无关的三项）。折叠方向与拼接点间距**必须在提交点那关拦**，因为配对时机车还在路上，几何尚不存在。
4. **必须同时设计"拒绝后的出口"**，否则从"穿模"退化成"机车在站台乱跑"。

---

## 二、「配对」在代码里的确切位置

### 2.1 唯一建立点

```
R3REnsureCouplePair(Train *v)            train_cmd.cpp:11374
```

- 每 tick 由 `TrainLocoHandler()` 调用一次。
- 函数头注释自陈：**"This function is what establishes and maintains the lock."**
- 配套数据是 **KI-182 的一对一耦合目标锁**：`Vehicle::r3r_couple_target`（机车侧）/ `r3r_couple_requester`（等待车底侧），解除走 `R3RUnpairCoupleTargets()`。

### 2.2 候选筛选链（本轮取证的核心）

`R3REnsureCouplePair()` 内部，先做稳态短路（仍锁定且 partner 仍合格 ⇒ 直接 return），否则**先解除旧配对**，再全池重扫，对每个候选 `t` 依次过以下闸门：

| 序 | 判据 | 说明 |
| --- | --- | --- |
| 1 | `t->First() != t \|\| t->First() == coupler->First()` | 非独立链头 / 自家链 ⇒ heal + continue |
| 2 | `!R3RIsCoupleTarget(t)` | **★ + 独立链头 + `OT_WAIT_COUPLE` + 真铰接否决（见 §三）** |
| 3 | `t->vehstatus.Test(VehState::Stopped)` | 停驻车不是候选 |
| 4 | `!R3RCoupleTargetAtOrderStation(coupler, t)` | **KI-319（第 212 轮）—— 把"目的地规则"提前到配对扫描** |
| 5 | locker 检查 | 记录 `CPL-PAIR-STEAL`（被别的机车锁定） |
| 6 | `!R3RCoupleAllowedIgnoringPair(coupler, t)` | 分组 / 公司边界 / 目的地 |
| 7 | 取 `DistanceManhattan` **最近**者 ⇒ `best` | |
| 8 | `R3RPairCoupleTargets(coupler, best)` | **真正落锁**（`:11484`） |

**⇒ 落点就是第 6 与第 7 之间（"第 6.5 步"），与第 4 步同构。**

### 2.3 极强先例：第 212 轮 KI-319

`R3RCoupleTargetAtOrderStation()`（候选链第 4 步）做的事和玩家现在提议的**完全同构**：把一条原本只在到达时生效的规则，**提前到配对扫描里**。它当时的动机（写在注释里）是：

> 否则会给一个"永远拿不到"的候选打「已被别人锁定」的假报告（第 210 轮就误读成 contention）。

⇒ **这条路已经走通过一次，配对的扫描位置被验证为可靠的预检点。** 玩家的提议不是新架构，而是**把这条既有做法从 1 条闸门扩到 N 条**。

---

## 三、★ 关键发现：现有配对期闸门的不对称缺口

### 3.1 配对期只检查了"候选"，没检查"机车自己"

```2093:2096:src/train_cmd.cpp
	 * 从这里开始（yapf 的目标探测与回溯、TrainCoupleHandler 的到达闸门都走本函数）
	 * 寻路就再也瞄不到它，机车自然也就不会对着它做 GOTO_COUPLE。 */
	if (R3RChainHasRealArticPart(t)) return false;
	return t->current_order.IsType(OT_WAIT_COUPLE);
```

`R3RIsCoupleTarget(const Train *t)`（定义 `:2062`）的**唯一参数就是候选 `t`**。它对真铰接的否决只覆盖**候选方**。

而提交点的闸门（第 170 轮，`TryTrainCouple()` 折叠修正入口的 `FOLDCHK-REFUSE-REAL-ARTIC`，含 `R3RRefreshChainCaches` + 两侧 `ConsistChanged` + `return false`）判的是：

> **任一参与方**含真 artic part ⇒ 拒绝。

### 3.2 不对称造成的后果 = 第 172 轮穿模生成链的入口

`v` = 机车（`coupler`）自己含真 artic part 时：

| 环节 | 行为 | 结果 |
| --- | --- | --- |
| 配对期 | `R3RIsCoupleTarget(t)` 只看 `t`，`t` 是干净车底 ⇒ **通过** | 配对成功，机车把这一列设为寻路目的地 |
| 行驶 | 机车开过去 | 接近 |
| 到达 | 命中即重叠 1px（`CheckTrainCollision` 挂接分支的 `-1`，KI-106） | **已进入对方像素范围** |
| 提交尝试 | `FOLDCHK-REFUSE-REAL-ARTIC`（任一参与方）**拒绝** | 不合并 |
| 重试 | 每 tick 再来，机车**每失败一次北推 1px**（第 172 轮现场 `worst_gap` 7→2） | **重叠由 1px 涨到若干 px** |
| 提交 | `gap=2` 恰好满足 `FOLDCHK-DIR-OVERRIDE` 前提 ⇒ 真折叠被丢弃 ⇒ 提交 | **穿模** |

⇒ **配对期闸门在"谁含真铰接"这个维度上比提交点闸门宽一格，中间那段空档就是穿模生成链的第①②步。**

**⇒ 用户的方案正好补上这一格。** 这是本轮最重要的结论：**它不是一个"锦上添花"的预检，而是能单独消灭第 172 轮根因②的针对性修复。**

---

## 四、配对期能查什么、不能查什么（必须分清）

配对发生时机车**刚开始执行 `GOTO_COUPLE`**（注释：`A locomotive which starts executing a GOTO_COUPLE order immediately locks onto exactly one waiting segment`），此时机车可能远在若干格之外，**两车根本还没相邻**。因此可用信息只有"两条链各自的静态属性"。

### 4.1 能在配对期预检（静态、与位置无关）

| # | 判据 | 覆盖的失败原因 | 代价 |
| --- | --- | --- | --- |
| **S-1** | **两侧都不得含真 artic**：`R3RChainHasRealArticPart(coupler) \|\| R3RChainHasRealArticPart(t)` ⇒ 拒 | **第 172 轮根因②**（`FOLDCHK-REFUSE-REAL-ARTIC` 死循环 + 北推制造 `gap=2`） | 极低，`R3RChainHasRealArticPart()`（`:1870`）现成、纯只读 |
| **S-2** | **合并后总长 ≤ 上限**：`coupler->gcache.cached_total_length + t->gcache.cached_total_length` 对 `_settings_game.vehicle.max_train_length * TILE_SIZE` | `CheckTrainAttachment()` 的 `allowed_len < 0` 分支（**attach 失败原因中唯一能静态预判的一类**） | 低 |
| **S-3** | （可选）段数 / 分组掩码等其它静态项 | 视需要 | 低 |

### 4.2 **不能**在配对期查（动态、依赖位置与姿态）

| # | 判据 | 为什么不行 | 应该在哪查 |
| --- | --- | --- | --- |
| **D-1** | 折叠方向 `R3RCheckChainFoldedDirection()` | 需要两车**实际相邻** + 各自 `direction`。配对时机车还在路上，`direction` 会随掉头/绕站台变化 | **提交点**（第 172 轮 `COUPLE-REFUSE-STILL-FOLDED`） |
| **D-2** | 拼接点间距 `\|dist − nominal\| ≤ 8` | 位置尚不存在 | **提交点**（第 217 轮 KI-326 建议的几何预检） |
| **D-3** | **NewGRF 跨链 attach 回调**（`ATTACH-FAIL cb=0x%X`） | 回调需要"**合并后的链**"才能问 | **提交点**（见 §六 的诚实说明） |

⇒ **两道闸门串联：配对期拦"静态不可能"，提交点拦"动态排不好"。绝不能只留一道。**

---

## 五、必须一起设计的副作用：拒绝之后怎么办

### 5.1 无出口会退化成"机车在站台乱跑"

`best == nullptr` 时现有行为只是**记 scan tick、每 8 tick 重扫一次**。若全部候选被静态闸门拒掉 ⇒ 永远 `best == nullptr` ⇒ 每 8 tick 全池扫描 + **机车无目的地**。

这个症状**有历史先例**：KI-195（第 109 轮）记录过同族问题 —— 候选被剔出目的地集合 ⇒ 无预留 ⇒ 沿站台乱跑。

⇒ **配对期拒绝必须配出口。**

### 5.2 出口方案：把 M3 的挂点上移

- 现有 **M3**：`TrainCoupleHandler()` 里的提交失败计数，`R3R_COUPLE_COMMIT_FAIL_LIMIT = 4`（约 1 分钟），满额 ⇒ `R3RUnpairCoupleTargets(v)` + 按 `CmdSkipToOrder` 范式推进订单。**玩家 2026-10-03 已明确拍板保留 M3。**
- **问题：M3 挂在"提交失败"上，不挂在"配对拒绝"上。** 一旦配对期就开始拒，就永远走不到提交，M3 永远不会触发。
- ⇒ **两个选择**（建议 (a)）：
  - **(a) 新增"配对拒绝计数"**：同一 `(coupler, 目标)` 连续被静态闸门拒 N 次（同 4 次量级）⇒ 直接走 M3 那套出口（解配对 + 跳过订单）。保留 M3 原职责不变。
  - (b) 把 M3 的入口整体上移到配对层。改动更大，且会同时改变"提交失败"路径的既有语义（第 172 轮刚验证过，不宜再动）。

### 5.3 选最近候选的逻辑天然正确

被拒的候选 `continue` 掉即可，第 7 步的 `DistanceManhattan` 最近者**自动取次近**，不需要额外改动。

---

## 六、诚实的边界：能不能"预演到能挂上"

玩家提议的可能期望是"配对时就把最终能否挂上验清楚"。要如实说明两点：

1. **NewGRF 的跨链 attach 回调无法在配对期问。** 要预演只能"**临时合并 → 跑 `CheckTrainAttachment(head)` → 回滚**"。
2. **而这条回滚路径已知有坑。** 第 170/216 轮已确认：`RestoreTrainBackup()` **只还原链序**、`R3RUndoLogicalFlip()` **只还原方向与角色位**，**两者都不还原像素位置**（这也是第 217 轮报告 §四 方案 4 的唯一难点）。
   ⇒ **在配对期（机车还在路上、每 8 tick 才扫一次）做"临时合并 + 回滚"是不划算的**：代价高、且回滚不完整会造成新的错位。
3. ⇒ **建议：配对期只做 S-1/S-2 静态三项；动态几何与 NewGRF 回调全部留给提交点** —— 提交点本来就是"临时合并 + 失败回滚"的既有机制（`TryTrainCouple` 四候选 + 逐候选自撤销），放在那里最安全。

---

## 七、推荐落地

**第一步（最小、最有价值）**：在 `R3REnsureCouplePair()` 的候选循环里，紧邻 `R3RCoupleTargetAtOrderStation()`（第 4 步）之后加 **S-1**：

```cpp
/* 与 R3RIsCoupleTarget() 的真铰接门禁对称：那边只看候选 t，
 * 这边补上"机车自己也不能含真 artic"——提交点的
 * FOLDCHK-REFUSE-REAL-ARTIC 是任一参与方含真 artic 就拒，
 * 不对称会让机车开过去顶死（第 172 轮根因②）。 */
if (R3RChainHasRealArticPart(coupler)) continue;   /* 或并入统一预检函数 */
```

- 现成函数、纯只读、零位置风险。
- **直接消灭**第 172 轮根因②：机车不再会为了一个注定被拒的挂接而靠上去，`worst_gap` 7→2 的"北推 1px"前因消失。

**第二步**：加 **S-2**（合并后总长上限），顺带覆盖 attach 失败里唯一可静态预判的一类。

**第三步**：加 §5.2(a) 的**配对拒绝计数 + 出口**，与第一步同批上线（否则第一步会把"穿模"变成"僵持/乱跑"，比原来更糟）。

**第四步**：提交点闸门按第 217 轮 KI-326 / 第 172 轮 `COUPLE-REFUSE-STILL-FOLDED` 保留并补齐，形成"静态 + 动态"双重闸门。

**统一原则（沿用第 217 轮）**：**绝不留下"半修"状态** —— 每次挂接的结果只允许是「几何连续」或「没挂上」两种。

---

## 八、复测判据

1. **新增拒绝标签出现后，不得再出现 `FOLDCHK-REFUSE-REAL-ARTIC`**（关键判据：说明机车不再对着注定被拒的目标靠上去）。
2. **同一场景 `RESPACE-BADCHAIN` / `unfixed>0` 应消失**（第 172 轮形态的穿模提交从日志消失）。
3. **正常挂接不得被误伤**：健康紧贴同向挂车仍须 `COUPLE-OK`；两侧都无真铰接时新闸门必须一字不放行（即不产生新拒绝行）。
4. **拒绝计数满额后必须走出口**：出现 M3 同族的跳过订单日志，机车执行下一条命令，**不得永久 `best == nullptr` 空转**。
5. **原生真铰接列车（无 ★/⊗）行为不变**：它本来就进不了配对（`R3RIsCoupleTarget` 已拒），故新闸门对它是空操作。

---

## 九、未确认项

- **U-1**：现场日志里 `FOLDCHK-REFUSE-REAL-ARTIC` 的 `v`（机车）到底是"机车自己含真 artic"还是"车底含"？这决定 S-1 是否真的命中第 172 轮根因②。本轮**未统计**（现场日志停在挂接发生之前，与第 217 轮 U-1 同源）。
- **U-2**：`gcache.cached_total_length` 在配对扫描时是否一定是最新值（是否可能在 `ConsistChanged` 之前读到旧值）。需要确认配对扫描与 `ConsistChanged` 的时序关系，否则 S-2 可能误判。
- **U-3**：配对拒绝计数的键该用什么 —— `(coupler, best)` 车号对，还是只 `coupler`？若目标会在多次重扫间变化，只记 `coupler` 更稳（避免计数被分散到不同目标上永远不满额）。
- **U-4**：`R3RCoupleTargetAtOrderStation()` 是否已经隐含覆盖了一部分 S-1（需要读它的实现确认是否调用链上会走到真铰接判定）。若是，落点可与之合并。

---

## 十、本轮未做

- 未改任何源码，未构建。
- 未统计 `FOLDCHK-REFUSE-REAL-ARTIC` 现场出现次数与参与方（U-1）。
- 未验证 `gcache.cached_total_length` 在配对扫描点的时序（U-2）。
- 未核对 `R3RCoupleTargetAtOrderStation()` 与 S-1 的重叠（U-4）。
- 第 217 轮 KI-326（提交点几何预检）状态不变，仍待拍板；本轮新增 KI-327（配对期静态预检）与 KI-328（配对拒绝出口）。
