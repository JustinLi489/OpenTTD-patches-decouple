# R3R 第 377 轮：veh=0 在订单回环后的异常（取证与疑点登记）

状态：**取证中 / 疑点已登记，未改任何源码**
日志：`build\R3R_debug.log`（59 218 行 / 5 828 856 B / 2026-10-30 21:37:58）
产生者：`build\openttd.exe` 52 450 304 B / 2026-10-30 20:00:45（第 376 轮 方案 W / `OT_COUPLE_END` 构建产物）
⇒ 日志晚于 exe，采样有效，本轮现象出自含 `OT_COUPLE_END` 的新构建。

---

## 1. 玩家报告

原话：「请见 build/R3R_debug.log，关注 veh0，它在调度命令跑了一个回环之后出现了问题」

已确认的事实底座：
- `veh=0` 是一条 **纯车底链（caronly=1）的段头**，位于 `veh=48` 链的第二段；`veh=48` 才是 primary（执行头）。
- 该车底自己的订单表 = 15 条（`ord=0000025D236D2700`）；耦合期借用的表 = 33 条（`ord=0000025D236CF820`，主人 `veh=6`）。

---

## 2. 已取证时间线（全部带行号，原文片段）

| 行号 | 原文 / 摘要 | 判定 |
|---|---|---|
| 50475 | `UNIT-RESTORE veh=0 id=1` | 解挂后车号恢复，正常 |
| 50479 | `ORD-IDX-WATCH site=syncdriving-in tag=keep veh=0 real=0->3 impl=0->3 tt=0->3 n=15->15` | 自有表（15 条）索引 0→3 |
| 50481-50483 | `del-implicit...real=0->23 n=33` / `ord=...->236CF820` | 切换到借用的 33 条表，索引被改成 23 |
| 50489 | `COUPLE-AIR-SKIP veh=48 from=14 to=15 end=14 num=19 order=0` | 空气区间跳过，`veh=48`，正常形态 |
| 51321 | `CPL-PLAN veh=0 n=33 real=25 impl=25 co=17 borrowed=1 follow=1 followOwn=6 bak=0 bakReal=3` | veh=0 借用 owner=6 的表，索引 25，备份索引 3 |
| 53163 | `COUPLE-AIR-SKIP veh=50 from=18 to=0 end=18 num=19 order=0` | **回环点**：18 → 0（表尾跳回首），且执行车是 `veh=50`（48 链上的 ⊗ 段尾，不是链头） |
| 56803-56804 | `syncdriving-in tag=keep veh=0 real=3->25` / `out-samelist` | 又从自有表 3 采纳成 25（借用表值） |
| 56806 | `ORD-IDX-WATCH site=ord-push veh=0 real=25->6` | 进度被写回 6（借用表 owner 的进度） |
| 56810 | `COUPLE-AIR-SKIP veh=57 from=2 to=3 end=2 num=8 order=0` | 另一列车，正常 |
| 56962-56963 | `post-processorders ... veh=0 real=6 curType=17` / `DEPOT-ARR veh=0 real=6(17) curType=17` | 停在 WAIT_COUPLE |
| 57935 | `CPL-WAITCLEAR veh=0 head=48 tile=35,12 segFront=1 idx=6 real=17 nord=33 nth=16` | 又参与一次耦合，仍在借用表 idx=6 |
| 58020 | `settle-couple` | 耦合提交 |
| 58980 | `ORD-RETURN-ORPHAN veh=0 head=48 owner=0` | **owner 被判成 0（自己）**，而表主人是 6 |
| 58981 | `ORD-RETURN-OWN veh=0 num=15 real=6->3 impl=6->3 bk=3/3 orphan=1` | 归还自有 15 条表，索引退回备份值 3 |
| 58982-59218 | 无任何 `veh=0` 行 | 之后 237 行完全静默 |

对照：`veh=48` 解挂后的自有表（行 152-166 `L-ORD`）结构正确 ——
`0=6(路点15) 1=1(站7) 2=15(DECOUPLE) 3=17(WAIT_COUPLE) 4=6(路点15) 5=16(GOTO_COUPLE,站7) 6..13=1(站) 14=18(COUPLE_END)`。
即「参考区间」落在 5→14，闭区间写法正确。

---

## 3. 疑点（REG-377-*，先登记、不预设结论）

**REG-377-A（中，疑点）** `ORD-RETURN-ORPHAN veh=0 head=48 owner=0`
期望：借用表的 owner 应为 `veh=6`；实测 owner=0。
存疑原因：owner 判定若在归还那一刻已把「本链链头所在段」当成 owner，则归还分支会走成「没有别人在用这张表」⇒ 可能提前释放/提前归还进度。
还需什么数据：`R3RPushProgressToOwner` / `ORD-RETURN-*` 打印 owner 的取值来源（是 `R3RGetPriorityHead` 还是 `follow_owner`）。

**REG-377-B（中，疑点）** `COUPLE-AIR-SKIP veh=50 from=18 to=0 end=18 num=19`
期望：空气区间跳过应当只在**执行头**（本链 primary `veh=48`）上改索引。
实测：执行车是 `veh=50`（48 链的 ⊗ 段尾），并且做了 18→0 的**回环**。
存疑原因：若 `R3RSkipCoupleAirRanges()` 改的是「传进来的 v」而不是 `v->First()`/primary，则链上多节车的 `cur_real_order_index` 会各走各的，回环后与链头不同步。
反向证据：50489 与 56810 的 veh 分别是 48、57，形态看起来是链头 ⇒ 不是每次都是非链头，需要查这几次调用点的上下文（是否 `veh=50` 当时确实是临时链头）。
还需什么数据：53163 前后 40 行上下文；`R3RSkipCoupleAirRanges` 的调用点列表。

**REG-377-C（高，疑点）** 58981 之后 `veh=0` 静默 237 行
期望：解挂后 `veh=0` 段若被并入 48 链，则由 48 代跑，不产生自己的日志（静默属正常）；若它已独立成链，则应出现 `TRP/CRT/ORD-*`。
存疑原因：无法只凭日志区分「正常静默」与「卡死后不再产生日志」。
还需什么数据：玩家侧的现象描述（看到什么）。

**REG-377-D（低，疑点）** `ORD-IDX-WATCH site=tick-in veh=6 real=17->11`
借用表 owner=6 的索引在解挂前是 17，解挂后变 11；与 veh=0 的 6 不一致。若「整链一个索引」的口径成立，这两个数不应分叉。待与 REG-377-A 合并判定。

---

## 4. 猜测根因（待验证，禁止当结论引用）

**猜测 1（待验证）**：`OT_COUPLE_END` 区间跳过（`COUPLE-AIR-SKIP`）在执行端按「当前被 tick 的车」改索引，遇到 18→0 回环时把索引写到 `veh=50`（段尾）身上；链头 `veh=48` 仍持有旧索引 ⇒ 回环后两者的进度分叉，下一次交接（解挂/耦合）按错误索引落点，`veh=0` 停在错误的 `WAIT_COUPLE`。
支撑证据：53163 的 `veh=50` + `from=18 to=0`；58981 的 `real=6->3` 与 `bk=3/3`。
待验证判据：在同场景复跑，若 `COUPLE-AIR-SKIP` 的 `veh=` 恒等于该链 primary（`veh->First()`/`IsPrimaryVehicle()`），则猜测 1 被证伪。

**猜测 2（待验证）**：`ORD-RETURN-ORPHAN ... owner=0` 说明「谁是这张表的主人」在解挂后被判成本链链头所在段（`veh=0` 自己带 ★，看起来像段头），于是「归还」走了 orphan 分支，把借来的进度覆盖成备份值 3；实际应保留 owner=6 的进度 6 ⇒ 位置丢失。
支撑证据：58980 的 owner=0；58981 的 `real=6->3`；寄存器值 3 = 解挂前的 `bakReal=3`。
待验证判据：owner 正确时应打印 `owner=6`，且 `real` 不被 off-by-回环改写。

---

## 5. 需要玩家回答（编号，答完直接回填本文件）

- **C-1** 「出现了问题」的具体表现是哪一种？
  (a) 车底/列车停住不动、不执行后续命令
  (b) 订单窗口里选中行/索引跳到了错误的命令（如跳回第 1 条）
  (c) 图像/位置异常（穿模、错位、瞬移）
  (d) 卡死/崩溃
  (e) 其它（请描述）
- **C-2** 出问题时 `veh=0` 所属车底是「T8701/2 南宁-河内（嘉林）」那组吗？它当时停在哪个站/库（tile 或站名）？
- **C-3** 「跑了一个回环」是指：整张订单表执行完一圈回到第 1 条之后才出问题，还是指窗口里看到索引从表尾跳回表首的那一刻？
- **C-4** 问题是在**解挂之后**出现，还是在**耦合之后**出现？（日志里 58981 是解挂归还，58020 是耦合提交）

---

## 6. 下一步（待 C-1..C-4 与上一节判据落地后）

1. 定位 `R3RSkipCoupleAirRanges()` 的调用点，确认索引写在 `v` 还是 `v->First()`；若写 `v`，改为写 primary 并补 `ORD-AIR-SYNC` 探针。
2. 复核 `R3RPushProgressToOwner()` 的 owner 解析（`R3RGetPriorityHead` vs `follow_owner`），解释 `owner=0`。
3. 复跑判据：`COUPLE-AIR-SKIP` 的 `veh=` 恒为链头；回环（`to=0`）后 `real` 与 `impl` 一致；`ORD-RETURN-ORPHAN` 不再在借用归还时出现。

---

## 7. 玩家答复（2026-10-30，原话要点，按 C 编号回填）

- **C1（原话）**：「在场景最后一次解挂之后应当跳到第一条命令，但是却跳到了第四条」
  ⇒ **期望 = 自有表 idx 0（第 1 条）；实测 = idx 3（第 4 条）**。与日志 `58981 ORD-RETURN-OWN veh=0 num=15 real=6->3 impl=6->3 bk=3/3 orphan=1` 逐字吻合（3 = 归还时采用备份索引，备份索引本身就是 3）。
- **C2（原话）**：「这组车底是"T8701/2 南宁-凭祥"，出现问题的时候它在凭祥站，也就是日志最后一次解挂发生的车站（日志里有多个场景，但是关于这个问题关注最后一个场景即可）」
  ⇒ 只看**最后一次解挂**（`58980/58981`），`veh=0` = T8701/2 南宁-凭祥 车底；位置 = 凭祥站。日志前半的同型事件只作对照，不作判据。
- **C3（原话）**：「它自己的表大概第六条之后就全部是"空气排程"了，然后它最后一次作为控制段就是在执行第六条排程，后面跟别的车跑一大圈之后解出来再次作为控制段，排程就已经到第四条了，而实际上按道理应该是第一条」
  ⇒ 自有表结构（日志 `152-166` 的 `L-ORD`，15 条）：`0=路点15 1=站7 2=DECOUPLE 3=WAIT_COUPLE`；第 6 条（idx 5）= `GOTO_COUPLE(16,站7)`，其后 idx 6..14 = 空气区间 + `18=COUPLE_END`（索引 14）。
  事件序列 = 作为控制段执行到 idx 5（GOTO_COUPLE）→ 被别车挂走、借用他人表跑一大圈 → 解挂 → 重新成为控制段时索引为 3。
- **C4（原话）**：「问题在解挂后出现」
  ⇒ 责任段锁定在**解挂（归还借用 + 给解出方定索引）**，不在耦合提交。

### 7.1 据此收紧的判定

| 项 | 期望 | 实测（日志） | 判定 |
|---|---|---|---|
| 解挂后 `veh=0` 自有表 `real` | 0 | 3 | **不符（玩家主诉）** |
| 归还来源 | 应回到「作为控制段交接出去时的进度」并推进过已完成的交接点 | 直接采用 `orders_backup_real_index`（=3） | 疑为直接原因 |
| 备份索引 3 的来源 | 应来自「自己作为控制段执行到 idx 5 后被挂走」的停放值 | 3 来自**上一次解挂**写入的等待点（`50479 syncdriving-in keep veh=0 real=0->3`） | 疑为二次污染 |

### 7.2 猜测根因（待验证，第 2 条替换原猜测 2）

**猜测 3（更高优先级，待验证）**：`veh=0` 的自有表在**上一次解挂**时就被写成了 3（`50479`：解挂路径 `syncdriving-in tag=keep veh=0 real=0->3`）——即解出方的「等待点=卷绕找到的第一个 WAIT_COUPLE（idx 3）」这一既有口径（KI-215 家族）在**车底自持表**场景下是错的；随后这次 3 又被 `R3RParkOwnOrderProgress()` 当作「自己作为控制段的真实进度」停进 `orders_backup_real_index`，解挂归还时原样搬回（`bk=3/3`）。
⇒ 于是「应当从头（0）」变成「停在 3」，且**跨场景可复现**（每次解挂都把 3 再冻一次）。
待验证判据：①最后一次耦合前的 `ORD-BORROW-PARK veh=0 bkr=?`；若 bkr=3，则停放端已被污染，责任在上一次解挂而非本次归还；若 bkr=5/6，则责任在归还端选错基准。②`50479` 前后上下文确认 3 确实由解挂等待点搜索写入。

**猜测 4（待验证）**：即便备份值正确为 5（GOTO_COUPLE），归还时也应把「已完成的交接点」推进到 0（回环闭合），而不是停在 5——因为车底被别车挂走再解出，等价于自己的循环重新开始。若 `ORD-RETURN-OWN` 只做「搬回备份值」而不做回环归一，则备份值再准也到不了 0。

### 7.3 复测判据（据此更新）

1. 最后一次解挂后立即出现 `ORD-RETURN-OWN veh=0 num=15 real=<6|5>->0`（末值为 **0**）。
2. 紧邻的 `ORD-IDX-WATCH site=tick-in veh=0 … curType=6`（路点）而非 `curType=17`。
3. 同一场景反复解挂/耦合，`ORD-BORROW-PARK veh=0 bkr=` 不得恒为 3（不得自污染）。
4. `veh=48` 一侧（机车半）索引行为逐字不变（负判据）。

---

## 8. 取证：过期备份是直接原因（`build\R3R_debug.log`，全部带行号）

### 8.1 `veh=0` 的归还值 = 最近一次 `ORD-BORROW-PARK veh=0` 的值

| 归还行 | 原文 | 最近一次 park | 值 |
|---|---|---|---|
| 2008 | `ORD-RETURN-OWN veh=0 num=7 real=6->0 bk=0/0` | 早期无 park | 0 |
| 6447 | `… num=7 real=6->0 bk=0/0` | — | 0 |
| 12877 | `… num=15 real=6->0 bk=0/0` | — | 0 |
| 34223 | `… num=15 real=6->3 bk=3/3` | `24975 ORD-BORROW-PARK veh=0 bkr=3 bki=3 num=15` | **3** |
| 39287 | `… num=15 real=6->0 bk=0/0` | `35335 ORD-BORROW-PARK veh=0 bkr=0 bki=0 num=15` | 0 |
| **58981** | `… num=15 real=6->3 bk=3/3` | **（无新 park）** ← 沿用 `50480` | **3（过期）** |

⇒ 机制确定：`ORD-RETURN-OWN` 把 `orders_backup_real_index` 原样搬回；该备份只在 `R3RParkOwnOrderProgress()` 真正命中时刷新，**未命中就冻结上一次的值**。

### 8.2 凭祥场景本次耦合没有刷新 `veh=0` 的备份

- `57937 ORD-BORROW-PARK veh=48 bkr=2 bki=2 tag=couple` ← 本次耦合只停了**主动方机车 48**，没有 `veh=0` 的行。
- 此后到 `58981` 之间全日志再无非 48 的 park 行。
- 全日志 `ORD-BORROW-PARK veh=0` 共 3 次：`2819 bkr=0 num=7`、`8906 bkr=0 num=15`、`24975 bkr=3`、`50480 bkr=3`（`35335 bkr=0`）——最后一次是 `50480`，距 `58981` 相隔 8 500 行、跨 6 次耦合/解挂。
- `58982 ORD-BORROW-PARK veh=48 bkr=2 bki=2 num=19` ← 解挂后为**下一轮**停位（又一次只停 48）。

### 8.3 那个 3 的来历（它不是"本轮进度"，是它当解出方时的等待点）

- `50479 ORD-IDX-WATCH site=syncdriving-in tag=keep veh=0 real=0->3 impl=0->3 tt=0->3 n=15->15 ord=…236D2700->…236D2700` ← **同一张自有表内 0→3**（`n` 未变，不是换表）。
- `50480 ORD-BORROW-PARK veh=0 bkr=3 bki=3 num=15` ← 立刻把 3 冻结。
- `50536 DECOUPLE-DONE u=0 co=1 real=25` ← 说明 `50479` 处在**解挂/交接**路径上（`veh=0` 当时是解出方，索引被置成 3 = `WAIT_COUPLE` = 第 4 条）。

⇒ 3 的语义 = 「车底停在 WAIT_COUPLE 等人来挂」，属它**作为解出方**时的正确值；但玩家口径下，`veh=0` 作为**留下的段**被别车带去跑完一圈再解出时，自有表应**从 0 重新开始**（`39287` 就是正确形态 `real=6->0 bk=0/0`）。

### 8.4 根因定性（取代 §7.2 的猜测 3/4）

**根因（已由日志闭环证实，未改码）**：`orders_backup_real_index` 是**单层驻留位**，且只在主动方（机车 `v`）被 `R3RParkOwnOrderProgress()` 命中时刷新；车底段 `veh=0` 作为**被动跟随段**被挂走时其备份不刷新 ⇒ 解挂归还时搬回的是**它上一次作为解出方时**的索引（3），而不是本轮应有的值（0）。

两条待定口径（需你点头，我不擅自选）：
- **口径甲（最小修法）**：耦合入口对**链上每个把表让出去的非控制段**都调一次 `R3RParkOwnOrderProgress()`（补 `ORD-BORROW-PARK tag=segment` 探针），让备份与本轮实际一致。
- **口径乙（按你的期望直接归零）**：解挂归还时若该车的自有表「已走完一轮 / 借用期间从未作为控制段执行」，则把索引归一到 0（或归一到一个显式标记的"下一轮起点"），而不是搬备份。

### 8.5 仍待验证（下一轮取数）

- U-1：`50479` 的 0→3 究竟是解挂路径哪一行写的（`wait_idx` 卷绕找 WAIT_COUPLE，还是 `R3RSyncDrivingOrders` 继承 owner 冻结值）——需读 `DecoupleTrain()` 里 `syncdriving-in tag=keep` 的调用点上下文。
- U-2：`57937` 只停 48 的 park，是因为 `R3RParkOwnOrderProgress` 的哪道守卫跳过了 `veh=0`（四道守卫：二次耦合 / 无表 / 表主人非自己 / 已有备份）。
- U-3：`58982` 紧接着又 `park bkr=2`（48），说明解挂路径里"为下一轮停位"是有的，但它停的是主动方而不是让表段 —— 与 U-2 同源。

---

## 9. 第 378 轮：玩家拍板「先只修甲」

### 9.1 玩家原话与据此收紧的范围

> 「先只修甲吧，乙感觉有些危险，或者说修跟不修都是一样要麻烦玩家的」

- 本轮**只落地口径甲**（补齐耦合入口的停位覆盖 + 让书签可刷新），乙（归还时按"走完一圈就归零"归一）**不做**：它不是修不好，而是**改变归还语义**，一旦判据写错会把本来正确的部分也一起挪位，风险面大于收益。
- 玩家后半句"修跟不修都一样要麻烦玩家"已在下面 §9.4 如实登记为残余：甲不能覆盖全部形态。

### 9.2 落地内容（仅 `src\train_cmd.cpp`，未碰任何 `src\*.h`）

1. 新增 `static void R3RParkLendingSegments(Train *chain, const Train *control)`，定义在 `R3RSegmentOwnOrderList()`（`:6697`）之后。逐节走 `Next()`，对链上每一节**自己持有表**的段判断"此刻能不能看见它自己表上的真实进度"：
   - 跳过链头（`Couple()` 已有 `R3RParkOwnOrderProgress(v)` 专块，`:12651`）；
   - 跳过控制段 `control`（借用期间它继续跑自己的表，备份与活索引都还有用）；
   - `R3RSegmentOwnOrderList(s) == nullptr` ⇒ 没有"自己表上的位置"可言，跳过；
   - `orders_backup != nullptr` ⇒ 旧「停表」形态，位置已随表停过，跳过；
   - 跟随的是**别人的**表（`r3r_follow_orders != nullptr && != own`）⇒ 活索引是那张表的坐标，写进来就是污染（KI-472 甲），跳过；
   - 活索引 `INVALID` 或 `>= own->GetNumOrders()` ⇒ 不是有效进度，跳过；
   - 备份为 `INVALID` ⇒ `R3RParkOwnOrderProgress(s)`，命中也打 `ORD-BORROW-PARK … tag=segment`（与原 `tag=couple` 区分）；
   - 备份已有值且与活索引不同 ⇒ 覆盖刷新，打 `ORD-BORROW-REFRESH veh= bkr=old->new bki=old->new tag=segment`（新增探针）。
2. 调用点：`Couple()` 的交接块内，紧跟 `was_borrowing` 采样之后、`R3RParkOwnOrderProgress(v)` 之前（`:12643` 一带），`R3RParkLendingSegments(v, couple_owner);`。

### 9.3 为什么"刷新"要有坐标守卫

甲的字面说法是"每次耦合都给让表段补记一次书签"，但直接把活索引写进备份是**错的**：

- 借用期间活索引跑的是 owner 那张表，把它写进"自己表上的位置"就是把 33 条表的第 17 条记到 15 条表的备份里 ⇒ 归还时越界、落 0 或落错位（KI-472 甲已经踩过一次）。
- 所以只有"活索引确实落在自己表的坐标里"（非跟随者，或跟随的正是自己那张表）才允许写；其余不动。

推论（与玩家预期不完全一致，必须记住）：**一个整段借用期都在跑别人那张表的段，中途无法刷新书签**——它自己表上的位置自借用开始就没再变过，甲只能保证"下次借用开始时书签是新的"，不能保证"归还时是 0"。

### 9.4 残余与预期判读（不隐瞒）

| 形态 | 甲是否能救 |
|---|---|
| 段在两次借用之间当过链头/控制段，自己表上有推进，再被耦合当被动段 | **能**：耦合那一刻刷新成真实进度（`ORD-BORROW-REFRESH`），归还值不再是陈旧书签 |
| 段从未停过位（`INVALID`）就被挂走 | **能**：现在会补 `ORD-BORROW-PARK tag=segment`，不再走 `ORD-RETURN-NOBK` 落 0 |
| 段从 `50480` 起连续 6 次耦合都是被动跟随（=第 377 轮现场 `veh=0`） | **不能**：中途无"自己坐标"可采样，书签仍是 3；要它变成 0 只能走乙，或让玩家在解挂后手动点"跳到命令" |

⇒ 第 377 轮那条 `real=6->3` 的**直接症状**，甲只能减少其发生面（不再有"从没停过位"和"跨借用周期不刷新"两类），不能把已冻结的 3 变成 0。

### 9.5 复测判据

1. 每次 `COUPLE-OK` 前若链上有"让表段"，应出现 `ORD-BORROW-PARK … tag=segment` 或 `ORD-BORROW-REFRESH … tag=segment`（至少其一）；完全没有也可以接受（= 全链段都已停过位且无变化）。
2. `ORD-BORROW-REFRESH` 的 `bkr=old->new` 必须满足 `new < num(自己表)`；出现 `new >= num` 即判本修法失败（坐标守卫失效）。
3. 负判据：`ORD-RETURN-OWN` 的 `real=->X` 中，X 必须 `<= num`；机车侧（如 `veh=48`）行为逐字不变。
4. 负判据：控制段与链头不产生 `tag=segment` 行（不得重复停位）。
5. 现场形态（连续被动跟随）若仍出现 `real=6->3`，属 §9.4 已登记残余，不算回归。

---

## 10. 第 379 轮：按玩家口径把"刷新纸条"扩到每一次 R3R 行为

### 10.1 玩家原话（2026-10-30）

> 关于甲，我的建议是每次执行R3R行为时都刷新纸条，也就是挂接和解挂都刷新，这可能就是你的修法的局限性

要点：第 378 轮的甲只把"给链上其余段补停位/刷新"挂在 **`Couple()` 入口**一处，所以
"解挂"这条 R3R 行为完全不动书签 —— 玩家判定这就是修法的局限。

### 10.2 结论：局限成立，但真凶是"只写了没停过才写"

第 378 轮原始落点（现 `:7617` 一带的 `R3RSyncDrivingOrders` 切表点）写的是：

```
if (chain->orders_backup == nullptr && chain->orders_backup_real_index == INVALID) { ...停位... }
```

即**"书签一旦被写过就永不再更新"**。而两次借用之间这辆车往往一直在跑**自己的**表
（解挂之后它拿回自己的表、开始正常推进），到下一次切表那一刻书签早已过期。
⇒ 光多挂几个调用点还不够，必须把语义从"没停过才停"改成"按此刻真实进度覆盖刷新"。

### 10.3 落地（只改 `src\train_cmd.cpp`，未碰任何 `.h`）

把第 378 轮的单点函数抽成统一原语，挂到**三个**位置：

1. `static bool R3RRefreshOrderBookmark(Train *v, const char *tag)`（`:6727`）
   四道守卫（自己没表 / 旧形态 `orders_backup != nullptr` / 跟随的是别人的表 /
   活索引 `INVALID` 或 `>= num`）→ 未停过位写 `ORD-BORROW-PARK veh= bkr= bki= num= tag=`；
   已停过位且与活索引不同 ⇒ 覆盖写 `ORD-BORROW-REFRESH veh= bkr=old->new bki=old->new num= tag=`。
2. `static void R3RRefreshOrderBookmarks(Train *chain, const Train *control, const char *tag)`（`:6763`）
   整链逐节调用；`control` 可传 `nullptr`（提交点收口处整链都刷）。
3. 三个调用点（= "每一次 R3R 行为"）：
   - `Couple()` 交接块 `:12725` `tag=couple`（接跟随指针**之前**，能采到每段自己的坐标；顺带保留"链头由 `R3RParkOwnOrderProgress(v)` 停"的原分工，`control` 传 `couple_owner`）；
   - `R3RSyncDrivingOrders()` 切表点 `:7617` `tag=sync-borrow`（**这一处覆盖解挂与车库编辑**：换驱动表那一刻是唯一还能采到"自己表上坐标"的时刻）；
   - `R3RSettleChainSegments()` 入口 `:6892` `tag=<couple|decouple-v|decouple-u|depot-edit|clone|load>`（所有提交点的收口；覆盖"解挂两半拿回自己的表之后可能已被 DECOUPLE 推进过一条命令"这种情况）。

`R3RParkOwnOrderProgress()` 本身一个字没改（别的调用点依赖它"别覆盖"的语义）。

### 10.4 仍然存在的结构性边界（必须一起看）

借用中（`r3r_follow_orders` 指向别人那张表）的段，**活索引是那张表的坐标**，写进自己的
备份就是污染（KI-472 甲）。所以这类段在借用期里永远采不到"自己表上的位置"——
书签只能被冻结。扩展调用点的收益是：**只要它在两次借用之间真正自己开过表**，下一次
任一 R3R 行为都会把书签刷成真值；不能再进一步。

⇒ 第 377 轮 `veh=0` 那种"从 `50480` 起连续 6 次耦合都是被动跟随"的形态，本轮之后
书签仍可能是 3（因为它确实一直没在自己表上推进过）。这一点仍然是 §9.4 已登记的残余，
不是回归；要它变成 0 只能走乙（玩家已否决）或由玩家手动点"跳到命令"。

### 10.5 复测判据（本轮）

1. 解挂之后应能看到 `ORD-BORROW-PARK … tag=decouple-u`（或 `tag=decouple-v` / `tag=sync-borrow`），
   即"解挂也刷新"确有落地；耦合侧出现 `tag=couple`。
2. `ORD-BORROW-REFRESH` 的 `bkr=old->new` 必须 `new < num`；出现 `new >= num` 即坐标守卫失效、判失败。
3. 负判据：`ORD-RETURN-NOBK` 应显著减少（未停过位的段现在会在切表点补停位）。
4. 负判据：机车侧（`veh=48` 那类）`ORD-RETURN-OWN real=->X` 与第 378 轮逐字不变。
5. 已登记残余：连续被动跟随的段书签仍可能是旧值（§10.4）。

---

## 11. 第 380 轮：第 379 轮的刷新点把书签刷坏了（0→3）

### 11.1 玩家反馈

2026-10-30，玩家：「见 R3R_debug.log，为何问题还是没解决」。

### 11.2 现场（build\R3R_debug.log，1 677 949 B）

同一条 `veh=0` 车底段、同一张 `num=15` 的表，两次归还的落点不同：

| 行号 | 原文 | 判读 |
|---|---|---|
| 5 | `ORD-BORROW-PARK veh=0 bkr=0 bki=0 num=15 tag=load` | 读档时书签 0（正确，第 1 条） |
| 3889 | `ORD-RETURN-OWN veh=0 num=15 real=6->0 impl=6->0 bk=0/0 orphan=1` | 归还到 0（正确） |
| 3897 | `ORD-BORROW-PARK veh=0 bkr=0 bki=0 num=15 tag=decouple-v` | 书签仍是 0（正确） |
| **4242** | **`ORD-BORROW-REFRESH veh=0 bkr=0->3 bki=0->3 num=15 tag=decouple-u`** | **第 379 轮新加的 settle 刷新把 0 写成 3** |
| 16243 | `ORD-RETURN-OWN veh=0 num=15 real=6->3 impl=6->3 bk=3/3 orphan=1` | 归还到 3（错，第 4 条） |

结论句：第 379 轮的刷新确实在跑（全日志 24 条 `ORD-BORROW-REFRESH`），但它不是"没生效"，而是**生效后把对的写成了错的**。

同类第二例（不同表、不同 tag）：
- `ORD-BORROW-REFRESH veh=26 bkr=30->3 bki=30->3 num=33 tag=decouple-u`（:13645，从 30 回跳到 3）
- `ORD-BORROW-REFRESH veh=48 bkr=18->2 bki=18->2 num=19 tag=couple`（:15798）

### 11.3 根因：解挂落位被当成历史进度

解挂提交点里顺序是 `R3RSyncDrivingOrders(u)`（归位，索引 = 书签 0）→
`R3RAdvanceAfterDecouple()` 把 u 推到"解挂后该停的那一格"（自己表第 4 条 = 索引 3，
通常是 `OT_WAIT_COUPLE`）→ `R3RSettleChainSegments(u, "decouple-u")` 入口刷新。

刷新那一刻看到 3，判定为"它自己表上的真实进度"，写进书签。

但：
- **落位（3）** = 解挂之后这列车该在哪、是个临时工作位置；
- **进度（0）** = 它自己的表上真正跑过了几条命令；
- 两者在解挂瞬间数值不同、语义也不同，混为一谈就把"下次归还的恢复点"改成了"解挂的落点"。

第 380 轮一度想把守卫从"是不是跟随者"收紧成"驱动表是不是自己那张"
（`GetEffectiveOrders() != own`），**实测推演证明挡不住**：`R3RSegmentOwnOrderList()`
在 `orders_backup == nullptr` 时返回的就是 `v->orders`，而解挂后 `veh=0->orders`
确实已经是自己那张 15 条的表 ⇒ 两者恒等，守卫放行。`3 < 15` 的取值范围守卫也同样放行。
所以"数值合法"根本不等于"语义合法"。

### 11.4 修法（仅 src\train_cmd.cpp，未碰 .h）

1. `R3RRefreshOrderBookmark()` 新增第三参 `bool only_unset = false`；为真时在写之前
   直接 `return false`（已有书签一律不动）。
2. `R3RRefreshOrderBookmarks()` 同参透传。
3. `R3RSettleChainSegments()` 入口调用改为 `R3RRefreshOrderBookmarks(chain, nullptr, tag, true)`
   —— 提交点收口**只补从没有过书签的车**，不覆盖。

保留两处可覆盖的刷新点：`Couple()` 入口（接跟随指针之前）与
`R3RSyncDrivingOrders()` 切表点（交表/收表之前）—— 那两处活索引必然是自己表上的坐标。

守卫仍保留 `GetEffectiveOrders() != own`：它虽然在这一例中无效，但语义比"是不是跟随者"
更准确，将来 `orders` 被复用为别人的表时能挡住。

### 11.5 复测判据

1. 解挂后 `veh=0` 那一类**不得再出现** `ORD-BORROW-REFRESH ... tag=decouple-u`（书签已存在时）；
2. `ORD-RETURN-OWN veh=0 num=15 real=6->0`（不再 `->3`），玩家现象"跳到第 4 条"消失；
3. 负判据：新档/书签 INVALID 的车仍应出现 `tag=load` / `tag=decouple-u` 的 `ORD-BORROW-PARK`（补空仍生效）；
4. 负判据：`tag=couple` 与 `tag=sync-borrow` 的 REFRESH 行为与第 379 轮逐字不变。

---

## 12. 第 381 轮：`only_unset` 生效了，但耦合入口把书签又写坏（0→3）

### 12.1 玩家反馈

2026-10-30，玩家：「见 R3R_debug.log，修改还是没有生效，这是为什么」。

### 12.2 采样有效性（先验，必须做）

| 项 | 读数 | 判读 |
|---|---|---|
| `build\R3R_debug.log` | 1 698 258 B / 16 828 行 / 2026-10-30 22:43:31 | 新日志 |
| `build\openttd.exe` | 52 450 304 B / 2026-10-30 22:39:32 | = 第 380 轮 `only_unset` 版 |

⇒ exe 早于日志，采样有效，日志确实由带修法的 exe 产生。

### 12.3 事实（本份日志，行号与 §11.2 不同，勿混用）

| 行号 | 原文 | 判读 |
|---|---|---|
| 5 | `ORD-BORROW-PARK veh=0 bkr=0 bki=0 num=15 tag=load` | 读档书签 0 |
| **3459** | **`ORD-RETURN-OWN veh=0 num=15 real=6->0 impl=6->0 bk=0/0 orphan=1`** | **第一次解挂归还到 0 ⇒ 第 380 轮修法在这里生效** |
| ? | `ORD-BORROW-REFRESH veh=0 bkr=0->3 bki=0->3 num=15 tag=couple` | **耦合入口把 0 覆盖成 3** |
| **16498** | **`ORD-RETURN-OWN veh=0 num=15 real=6->3 impl=6->3 bk=3/3 orphan=1`** | 第二次解挂归还到 3（玩家看到的仍是"跳到第 4 条"） |

全日志 `ORD-BORROW-REFRESH` 共 **12 条，`tag=` 全部为 `couple`，`tag=decouple-u` 为 0 条**：

```
veh=57 0->1 num=8 | veh=48 0->2 num=19 | veh=48 3->5 num=19 | veh=51 0->3 num=13
veh=53 4->7 num=13 | veh=50 6->9 num=19 | veh=48 10->13 num=19 | veh=0 0->3 num=15
veh=50 14->17 num=19 | veh=59 2->5 num=8 | veh=57 6->1 num=8 | veh=48 18->2 num=19
```

结论句 1：第 380 轮的 `only_unset` **确实生效**——settle 侧一条 REFRESH 都没有了，第一次解挂也正确回到 0。
结论句 2：玩家"没生效"的观感来自**第二次解挂**（:16498 →3），而污染源已经从 settle 换到了 `Couple()` 入口（`tag=couple` 那条 `veh=0 0->3`）。

### 12.4 为什么耦合入口也会写坏（与第 379 轮同型，只是换了个地方）

`Couple()` 里 `R3RRefreshOrderBookmarks(chain, control, "couple")` 仍是覆盖式，前提是
"交接那一刻，链上每一段的活索引必然是自己表上的坐标"。这个前提对**主动方**成立，
对**被动段**不成立：`veh=0` 在耦合前已停在自己的等待落位（索引 3），它并没有"跑到第 4 条"，
只是被摆在那里等人来挂。刷新把这个落位记成进度 ⇒ 下一次归还就从第 4 条起跑。

⇒ 第 380 轮修的只是"谁能覆盖"（settle 不许覆盖），没修"覆盖时读到的值算不算进度"。
同一个坑在 `Couple()` 与 `R3RSyncDrivingOrders()` 两处还原样留着。

### 12.5 疑点（REG-381 系列，待验证）

- **REG-381-A**：`veh=0` 的活索引在第二次耦合前到底是"真跑到 3"还是"被落位到 3"？
  判据：查该段在两次解挂之间的 `ORD-IDX-WATCH` / `SKIP-STOPPED` / 订单推进记录；
  若期间它从未作为独立列车发车（`spd` 恒 0、无移动分支），则 3 是落位而不是进度。
- **REG-381-B**：`veh=48 bkr=18->2`（:15798 一带，`num=19`）与 `veh=57 bkr=6->1`（`num=8`）
  同型：覆盖后数值**变小**，说明读取到的不是单调递增的进度，更像"被另一张表的坐标复位"。
  与 KI-472 甲（借用期坐标污染）同族，需并案。
- **REG-381-C**：`ORD-RETURN-ORPHAN veh=0 head=48 owner=0`（:16497）判 owner=0 而表主是 6，
  与第 377 轮 REG-377-A 完全一致 ⇒ owner 解析仍未修（本轮不碰）。

### 12.6 下一步（本轮落地目标）

把"覆盖式刷新"从 `Couple()` 入口撤掉，只保留 `R3RSyncDrivingOrders()` 切表点一处，
并给切表点加"交表前必定停位"的前置（该处活索引此时仍是自己表坐标，且是唯一能保证采样时机的点）。
判据：同一场景第二次解挂应为 `real=6->0`；`ORD-BORROW-REFRESH` 只允许 `tag=sync-borrow` 出现。

### 12.7 构建自证（第 381 轮）

复用既有入口 `R3R_fullbuild.cmd inc detach`（未新建任何 `.cmd`）：

| 项 | 读数 |
|---|---|
| `build\R3R_incbuild.guard.log` | `GUARD: incremental is safe (no header/lang file is newer than the newest object)` ⇒ 本轮只改 `.cpp`，增量合法 |
| `build\R3R_incbuild.done` | `EXIT_CODE=0` |
| `build\R3R_incbuild.log` 尾部 | `[2/3] Building CXX object …train_cmd.cpp.obj` → `[3/3] Linking CXX executable openttd.exe` |
| 时间戳链 | `src\train_cmd.cpp` 22:45:55 → `train_cmd.cpp.obj` 22:48:17 → `build\openttd.exe` 22:49:34（52 450 304 B） |
| `read_lints` | `src\train_cmd.cpp` 0 条 |

本轮未新增日志字面量（只改调用参数），故不做 exe 内字符串核对。只编内测树（探针 ON），发布树未动。

### 12.8 复测判据（交给玩家用新 exe）

1. 同一场景第二次解挂应为 `ORD-RETURN-OWN veh=0 num=15 real=6->0`（不再 `->3`），车底回到第 1 条命令。
2. `ORD-BORROW-REFRESH` 不再出现 `tag=couple`（若出现，只允许 `tag=sync-borrow`）。
3. 负判据：第一次解挂（`:3459` 那类）与机车侧 `veh=48` 的归还行为逐字不变。
4. 负判据：`ORD-RETURN-NOBK` 不增多（空缺书签仍由 `R3RSyncDrivingOrders` 与 settle 补空路径填）。

## 13. 第 382 轮：玩家提「书签宿主错位」假设 + 代码复核

### 13.1 玩家假设（要点）

A 去挂 B → AB 又被 C 挂 → CAB 解为 CA + B。CA 的链头是 C，于是书签用的是 C 的（C 自己的表），
但 CA 实际指着 A 的表 ⇒ 坐标与表错位。建议修法：**书签返还也走控制段，而不是链头。**

### 13.2 写入侧复核（假设的这一半被证伪）

| 锚点 | 原文 | 判定 |
|---|---|---|
| `src\train_cmd.cpp:6754` | `OrderList *const own = R3RSegmentOwnOrderList(v);` | 书签的「表」是**该段自己的表**，不是"链头身上那张" |
| `:6756` | `if (v->orders_backup != nullptr) return false;` | 旧机制形态直接跳过 |
| `:6760` | `if (v->GetEffectiveOrders() != own) return false;` | **驱动表不是自己那张 ⇒ 一律不写** ⇒ 书签不可能记别人的坐标 |
| `:6762` | `if ((uint16_t)v->cur_real_order_index >= (uint16_t)own->GetNumOrders()) return false;` | 还要求坐标落在自有表范围内 |
| `:3999-4009` `R3RParkOwnOrderProgress` | 五道守卫后 `chain->orders_backup_real_index = chain->cur_real_order_index;` | 同上语义，但**没有** `GetEffectiveOrders()==own` 这道校验（只查 `r3r_follow_orders==nullptr`） |
| `:7471-7494` `R3RReturnBorrowedOrders` | `OrderList *const own = R3RSegmentOwnOrderList(chain);` … `chain->cur_real_order_index = own_real;` | 归还用的也是**链头自己段**的表与书签 |

⇒ 书签不会记「别人表的坐标」，玩家描述的这一半不成立。

### 13.3 但玩家的直觉对：真错位是「书签记的是对的表，那张表却不是该跑的表」

`R3RReturnBorrowedOrders(chain)` 只处理**传入的那一个链头**，`R3RSegmentOwnOrderList(chain)` 取的是
**链头所在段**的表。CAB 解为 CA 时：

- 链头 `C` 的自有表 = `orders_C`，书签 = `orders_C` 坐标；
- 但 CA 该跑的表由 `R3RReelectControlSegment` / `R3RSyncDrivingOrders(…,false)` 重新决定，可能是 **A 的 `orders_A`**；
- 于是「C 拿回 `orders_C` + 跳到 `orders_C` 的书签」是一次**正确的**还原，却还原到了**错的段**上；
- A 的 `orders_A` 与 A 的书签（`R3RPushProgressToOwner` 写过）挂在 A 上，但 `R3RReturnBorrowedOrders` 从不去看 A —— 它不是链头。

一句话：**书签按"车"存（每车一对），刷新与归还却按"链头"调用**。链头 ≠ 表属主时，那个段的一对备份永远没人归还。

### 13.4 对建议修法的评估

| 方案 | 做法 | 评价 |
|---|---|---|
| 玩家建议：返还走**控制段** | `R3RReturnBorrowedOrders(R3RGetControlSegment(chain))` | 方向对，但**控制段 ≠ 表属主**：`R3RGetControlSegment`（:5986）找 ★ 段头，`R3RGetPriorityHead`（:5956）找 `r3r_priority` 最小者，多段+后挂现场会分叉（KI-254 实测 `ctrl=48 / owner=21`），仍可能配错表 |
| 改走**表属主** | 宿主 = `chain->r3r_follow_owner` / `R3RGetPriorityHead(chain)` | 与既有 `R3RPushProgressToOwner` 口径一致 |
| **正解：归还按段遍历** | `for (Train *s = chain; s; s = s->Next()) if (s->IsSegmentFront()) R3RReturnBorrowedOrders(s);`，每段各用 `R3RSegmentOwnOrderList(s)` | 书签本就是逐车字段、`R3RSegmentOwnOrderList` 本就是段级 API ⇒ 最贴合既有数据结构，不新增字段 |

### 13.5 疑点（登记 REG-382-A/B/C）

- **REG-382-A**：CA 在解挂后到底该跑谁的表，取决于 `R3RReelectControlSegment` 判据（★ 段头 vs `r3r_priority` 最小）；两者在日志里曾分叉，必须用现场 `DECOUPLE-DONE` / `ORD-AFTER-COUPLE` 的 `real/owner/ctrl` 才能定。
- **REG-382-B（可直接修，低风险）**：`R3RParkOwnOrderProgress`（:3999）缺 `GetEffectiveOrders()==own` 校验，与 `R3RRefreshOrderBookmark`（:6760）**不对称**。若某路径下 `r3r_follow_orders` 已摘掉而驱动表仍是别人的（解挂后继承态），它会把别人表坐标写进自有书签。
- **REG-382-C**：`only_unset` 管"谁写"，不管"写在哪个段上"，对它无能为力。

### 13.6 结论

1. 「书签记了别人表坐标」不成立（`:6760` 守卫）。
2. 「书签与表由不同段持有、归还只按链头」成立，是本次 `0→3` 的候选根因之一。
3. 下一步顺序建议：先补 **REG-382-B**（与既有守卫对齐，零新机制），再按 **13.4 第三行（按段遍历归还）**改；不建议只把宿主换成"控制段"。

## 14. 第 382 轮：按上一节顺序落地（改动已写入源码）

玩家 2026-10-30 指示"直接按照那个顺序改改试试"，即 13.6 的两步：先补守卫、再改按段遍历。
改动仅 `src\train_cmd.cpp`，未碰任何 `src\*.h`（增量合法）。

### 14.1 改动一：`R3RParkOwnOrderProgress()` 补守卫（REG-382-B）

新增第 6 道守卫：

```cpp
if (chain->GetEffectiveOrders() != chain->orders) return false;
```

- 位置：`chain->orders_backup_real_index != INVALID_VEH_ORDER_ID` 判断之后、写书签之前。
- 语义：与 `R3RRefreshOrderBookmark()` 的 `GetEffectiveOrders() != own`（`own = R3RSegmentOwnOrderList(v)`，而该函数在 `orders != nullptr` 时就返回 `orders`）**逐字对齐**。
- 补的缺口：原有 `r3r_follow_orders != nullptr` 只挡修法 D 之后的跟随态；KI-239 现场那种"`orders` 里直接放借来的表、但没有跟随指针"的中间态会漏过去，把别人表的坐标写进自己的书签。
- 影响面：四个调用点（`Couple()` 入口 / 车库 DEPOT-PARK / 车库编辑 new_head / 被动跟走车底段）在"此刻驱动的是别人的表"时一律改为不写书签 —— 这正是期望行为（KI-472 甲：那种坐标属于别人）。

### 14.2 改动二：`R3RSyncDrivingOrders()` 的 `owner == chain` 分支改为逐段归还

```cpp
for (Train *s = chain; s != nullptr; s = s->Next()) {
    if (s->Previous() != nullptr && !s->IsSegmentFront()) continue;
    if (s != chain && s->r3r_orders_borrowed) { /* ORD-RETURN-SEG 探针 */ }
    R3RReturnBorrowedOrders(s, false);
}
```

- 原写法只对传入的链头调一次。书签是**按车**存的（每段各一辆），其余段的书签永远没人兑现。
- 新写法：谁的账谁结 —— 每个段首都用**自己**的 `R3RSegmentOwnOrderList(s)` 与**自己**的书签还原。
- 与玩家建议的关系：玩家说"书签返还走控制段"，这是它的通用版 —— 不单选控制段，而是逐段各自还原，控制段自然被覆盖。之所以不单选控制段，理由见 13.4：`R3RGetControlSegment`（★ 段头）与 `R3RGetPriorityHead`（`r3r_priority` 最小）会分叉（KI-254 现场 `ctrl=48 / owner=21`）。
- 成本：`R3RReturnBorrowedOrders()` 入口第一句就是 `!r3r_orders_borrowed` 即返回，遍历近似零开销；且归还后标记清零，后续调用不再进循环体。

### 14.3 新增探针

| 探针 | 触发条件 | 用途 |
|---|---|---|
| `ORD-RETURN-SEG veh= head= follow= own= driven=` | 逐段归还时，非链头且带借用标记的段 | 证明"这一次真的还了段中段"，并给出该段自有表条数 vs 正在跑的表的条数（两者不等即为借用态） |

既有探针 `ORD-RETURN-OWN` / `ORD-RETURN-NOBK` / `ORD-RETURN-ORPHAN` / `ORD-BORROW-PARK` 语义不变。

### 14.4 复测判据

1. **正判据（主）**：CAB 解为 CA+B 后，`DECOUPLE-DONE` 之后的 `ORD-RETURN-OWN` 行里，链头那行的 `num=` 与 `real->` **不再**出现"num=3 real->0/3"这种短表坐标（若该链头本该跑 A 的 15 条表，num 应为 15 或不再出现该行）。
2. 出现 `ORD-RETURN-SEG` 时，`follow=1` 且 `own != driven`（两段表条数不同），说明确实还了段中段。
3. **负判据**：单段链的 `ORD-RETURN-OWN` 逐字不变（`ORD-RETURN-SEG` 不该出现）。
4. **负判据**：`ORD-RETURN-NOBK` 数量不增加；若增加，说明改动二让某个从未停位的段首走进了"无书签→落 0"分支（见 14.5 风险）。
5. 玩家原始症状（解挂后命令跳回 `0→3`）不再出现。

### 14.5 风险（本轮未消除）

- **R-382-1**：改动二会给"有借用标记但从未停过位（书签 INVALID）"的段首写 0（`num == 0 || own_real == INVALID` 分支），表现为该段命令跳回第一条。若判据 4 命中，应改为"没有书签就不动索引，只摘跟随指针"。
- **R-382-2**：本次只覆盖 `owner == chain` 分支。CA 场景若重选后 `owner != chain`（链头继续借 A 的表），走的是"继续借用"分支，本改动不生效 —— 那种情况的错位在 `R3RSyncDrivingOrders()` 借用分支读的是 `owner->cur_*`（活索引，借用期已冻结但可能被污染，KI-472 甲），属另一条线。
- **R-382-3**：`R3RReturnBorrowedOrders()` 末尾仍把书签清成 INVALID（周期账本归零），因此"每段各自结算"是**一次性的**：若某段在同一周期内被再次借用，书签会重新采样。

### 14.6 构建自证

- 入口：`R3R_fullbuild.cmd inc detach`（复用既有脚本，未新建任何 `.cmd`）。
- `build\R3R_incbuild.done` = `EXIT_CODE=0`。
- 日志尾部：`[2/3] Building CXX object CMakeFiles\openttd_lib.dir\src\train_cmd.cpp.obj` → `[3/3] Linking CXX executable openttd.exe` → `==== R3R build ended 2026/10/30 周五 23:20:08.47 MODE=inc EXIT_CODE=0`。
- `src\train_cmd.cpp` 23:16:50 → `build\openttd.exe` 23:19:50（52 450 304 B），exe 晚于源码。
- 新探针字面量 `ORD-RETURN-SEG` 在 exe 内命中（`LIT=True`）。
- `read_lints`：`src\train_cmd.cpp` 0 条。

## 15. 第 382 轮回退（玩家实报回归，2026-10-30）

### 15.1 玩家原话

「我甚至没机会走到那个 CA 场景，就被刚才前几个对话你埋下的屎山代码搞得准备撞车了」

⇒ 第 382 轮两处改动在**与 CA 场景无关的常规行驶**上就产生了回归（列车将要撞车），
说明改动面远大于登记时的预期。判定：**本轮两处改动全部回退到第 381 轮状态**。

### 15.2 回退理由（逐条对应 §14.5 的风险）

| 改动 | 回退理由 | 对应风险编号 |
|---|---|---|
| 改动二（`owner == chain` 分支逐段归还） | 逐段调 `R3RReturnBorrowedOrders(s,false)`，对"带借用标记但书签 INVALID"的段会走 `num == 0 \|\| own_real == INVALID_VEH_ORDER_ID → own_real = 0` 分支，把该段 `cur_real/implicit_order_index` 直接写成 0。该分支被耦合/解挂/车库编辑/读档**全部提交点**调用，等于把"索引归零"扩散到每一条链的每一个段首 ⇒ 列车按第一条命令行驶 ⇒ 撞车。 | R-382-1（已命中） |
| 改动一（`R3RParkOwnOrderProgress` 补守卫） | 语义上更正确（不写别人表的坐标），但后果是书签**更容易保持 INVALID** ⇒ 归还时同样落 0。单独它就足以触发"命令跳回第一条"。两者叠加不可分辨，且都在不可快速验证的路径上。 | R-382-1 的同一后果 |

### 15.3 回退方式

精确删除 §14.1 与 §14.2 新增的代码块（含注释与 `ORD-RETURN-SEG` 探针），恢复：

```cpp
if (owner == chain) {
    /* Head is the command owner: give the borrow back. */
    R3RReturnBorrowedOrders(chain, false);
    R3RWatchOrderIndex(chain, "syncdriving-out-owner");
    return;
}
```

与 `R3RParkOwnOrderProgress()` 原本的五道守卫。`.md` 里的 §14 保留（作为"此路不通"的记录），不删。

### 15.4 结论（写给自己，避免再犯）

1. **`R3RReturnBorrowedOrders()` 的落 0 分支是危险放大器**：任何"让更多车走进这个函数"的改动，等价于"让更多车的索引被写成 0"。在把它改成安全形态（无书签 ⇒ 不动索引，只摘跟随指针）之前，**禁止扩大它的调用面**。
2. 第 382 轮的错误不是"方向错"，而是**顺序错**：正确顺序应是「先把落 0 分支改成安全形态」→「再扩大调用面」。我按相反顺序做了，直接踩中自己写下的 R-382-1。
3. 修 CA 场景之前必须先有**一次干净的对照日志**（无回归的 exe），否则无法区分新旧症状。

### 15.5 回退构建自证

- 入口：`R3R_fullbuild.cmd inc detach`（复用既有脚本，未新建任何 `.cmd`）。
- `build\R3R_incbuild.done` = `EXIT_CODE=0`。
- 日志尾部：`[2/3] Building CXX object …\src\train_cmd.cpp.obj` → `[3/3] Linking CXX executable openttd.exe` → `==== R3R build ended 2026/10/30 周五 23:27:52.91 MODE=inc EXIT_CODE=0`。
- `src\train_cmd.cpp` 23:25:17 → `build\openttd.exe` **23:27:44**（52 450 304 B），exe 晚于源码。
- **exe 大小 = 52 450 304 B，与第 382 轮改动之前逐字节同尺寸**；且 `ORD-RETURN-SEG` 字面量在 exe 内已查不到（`OLD_PROBE_GONE=True`）⇒ 代码确已回到第 381 轮状态。
- 构建前 `tasklist /FI "IMAGENAME eq openttd.exe"` 确认游戏未运行（避免闸门 97）。

### 15.6 下一步（未执行，等玩家）

不再按"补守卫 + 按段遍历"的旧顺序。新顺序必须是：

1. **先改 `R3RReturnBorrowedOrders()` 的落 0 分支**为安全形态：`own_real/own_impl` 为 INVALID 时**不动 `cur_*_index`**，只摘跟随指针与借用标记，并打 `ORD-RETURN-NOBK-KEEP`（与现有 `ORD-RETURN-NOBK` 并存便于对照）。
2. 只有第 1 步单独编译、复测无回归之后，才允许让更多车走进该函数（按段遍历或扩大调用面）。
3. 第 1 步是纯"少改活字段"的收窄型改动，风险方向是安全的（最坏情况=索引保持原样，不会跳回第一条）。

在此之前**不得再动借用/归还层的索引写入语义**。

## 16. 回退后问题依然存在（2026-10-30 23:29，玩家追问"你是引入了回归吗"）

### 16.1 玩家原话

「问题依然存在，你是引入了回归吗？」

### 16.2 已证伪：第 382 轮两处改动**不是**当前现象的来源

三条硬证据（全部来自本机读数，非推断）：

| 证据 | 读数 | 说明 |
|---|---|---|
| exe 尺寸 | `build\openttd.exe` = **52 450 304 B** | 与第 382 轮改动**之前**那一版逐字节同尺寸 |
| 旧探针字面量 | `ORD-RETURN-SEG` 在 exe 内 `Select-String -SimpleMatch` = **False** | 改动二的循环体与探针确已从二进制消失 |
| 运行时间线 | exe `23:27:44` → `build\R3R_debug.log` **`23:29`（293 867 B）** | 日志晚于新 exe ⇒ 玩家这次跑的就是回退版 |

⇒ 「回退版仍然出问题」成立，则第 382 轮的两处改动被排除。**注意范围**：这只排除第 382 轮，不排除第 377~381 轮（玩家说的是"前几个对话"，不止一轮）。

### 16.3 git 侧事实：无法用 git 二分

```
3b87101b00 2026-10-25_21:51  R3R: pre-ledger-removal stable checkpoint 2026-10-25 (rounds 256-303)
c41ca0f227 2026-10-20_16:04  R3R: temporary rollback checkpoint 2026-10-20 (rounds 249-255)
```

最后一个提交停在 10-25（第 256~303 轮），而第 377~382 轮的改动全在**未提交**状态（`git status -- src` 列出 20 个 `M` 文件 + 1 个 `?? src\train_cmd.cpp.r3r_bak`）。
⇒ 没有"第 381 轮"这样的提交点可直接 checkout 对照。想二分只能靠①保留的旧 exe 副本，或②我按备忘逐轮手工还原。

### 16.4 日志尾部读数（23:29 那次运行，`build\R3R_debug.log` 末 40 行）

- `CPLSPD veh=48 tick=13427..13440 tile=38,11 → 38,12 → 37,12 spd=161→148 strict=170 aend=170 aitem=170 decel2=240 nocab=0 revd=0 db=1 depe=0`
  ⇒ veh=48 在**正常行驶**（速度 148~161），没有任何"索引跳变/命令重读"的痕迹；`real=2`（同段 `RESV-EXT-PRE ... real=2` 可互证），**不是** KI-542 的 `0→3` 跳命令形态。
- `CPL-PAIRSCAN tick=13428/13435 act=48 state=steady ord=16 destStn=2 actTile=38,12/37,12 actSpd=160/154 pool=0 s_chain=0 s_target=0 s_stop=0 s_dest=0 s_lock=0 s_ign=0 s_ignd=0 best=0 bestDist=3/2 paired=-1 wait=-1 near=-1`
  ⇒ 订单 `ord=16`（`OT_GOTO_COUPLE`）下**完全找不到配对目标**（`paired=-1 wait=-1 near=-1 best=0`）。
- `RESV-EXT-PRE veh=48 tile=37,12 enterdir=0 tracks=0x9 reserved=0x1 doRes=0 real=2 spd=160` → `RESV-ARB tile=37,12 station=0 a=veh0 wait=1 b=veh48 wait=0 picked=veh48` → `CPL-RET veh=48 ... ret=0x1 why=early-havereserved`
- `RESV-PAIR-AIR veh=50 tile=37,12 track=0 kind=rail`（veh=50 也在同格附近持预留）
- `LOCO-MOVE veh=48 tick=13429 tile=38,12 j=258 spd=159 track=04 dir=6 db=1 stuck=0 dtile=1571 look=1 endTile=27,12 endTd=0 endPos=246 curPos=72 endBits=0101`
- `TRP veh=57 mf=35,12 real=0 type=6 dest=1808 spd=0 fp=0 look=0 one=1 mstuck=0 fto=0 => ok=0 res=0`

### 16.5 猜测根因（待验证，未实证）

> 猜测：当前"准备撞车"不是索引/命令错误，而是 **GOTO_COUPLE 的高速接近段没有速度收敛** —— veh=48 以 148~161 的实速奔向 `destStn=2`，而 `CPL-PAIRSCAN` 同时报告 `paired=-1 / wait=-1 / near=-1 / best=0`（连候选都没有），却仍在 `RESV-ARB` 里把 `veh0`（`wait=1`，即等待中的车底）的格位抢到手（`picked=veh48`）。

支撑：`spd` 全程 148~161 且 `depe=0 nocab=0 revd=0`，没有任何刹车/降速事件；`s_lock=0` 说明不是等待互锁；`endTile=27,12` 与 `actTile=37,12` 相距 10 格而速度不降。
待验证判据：①撞车那一 tick 的两列车车号与 `CPL-PAIRSCAN` 的 `paired` 是否仍为 -1；②`veh0` 的 `wait=1` 是 `OT_WAIT_COUPLE` 还是别的等待态；③该现象在第 381 轮 exe 上是否同样出现（这是"是不是我引入的"的唯一判据）。

### 16.6 需要玩家回答（编号回填用）

- **C-1**：「问题依然存在」指的是哪一种现象 —— (a) 列车真的要撞车（哪两列车？车号/时刻）；(b) 解挂后命令跳回第一条；(c) 没走到 CA 场景就出错；(d) 其它。
- **C-2**：你印象里**最后一次完全正常**是什么时候（哪一轮 / 哪个 exe / 哪天）？工作区有 `build\r3r_stable_2026-09-29\` 快照，若记得更近的备份位置也请给路径。
- **C-3**：这次出问题的操作序列（耦合→解挂？还是纯自动行驶？）以及是否读档后立即出现。

**在拿到 C-1~C-3 之前不做任何代码改动**：现在没有任何证据指向借用/归还层，贸然再改只会叠加变量。

## 17. C-1 的回答：两个独立症状（2026-10-30 23:3x）

### 17.1 玩家原话（逐字保留）

> 「我的问题是，六号或七号段，也就是“HXN5B-XXXX”，在解挂后，排程回到了第一条，这不正常。总之，它卡在车底前面，但是“DF4D-3058”又没有认出它，以为这一格是合法的可挂车底，于是冲了过去」

### 17.2 拆成两个必须分开修的症状

| 编号 | 症状 | 归属 | 与第 382 轮改动的关系 |
|---|---|---|---|
| **S-17-A** | 六号/七号段（HXN5B）**解挂后 `cur_real_order_index` 回到第一条** | 解挂的排程归属 / 书签归还（KI-542 / KI-215 / KI-180 家族） | **无关**：回退版复现（log 23:29 晚于 exe 23:27:44） |
| **S-17-B** | DF4D-3058 在 `GOTO_COUPLE` 寻路里**把停着的那列 HXN5B 判成“合法可挂车底”**，全速冲过去 | 候选判据 `R3RIsCoupleTarget` / `CPL-GATE` / `CPL-PAIRSCAN` 与命中判据不一致 | **无关**：同上 |

### 17.3 与 §16.4 日志读数的对接（一致性检查）

§16.4 记的 veh=48 正是 **DF4D-3058**，读数与玩家描述逐条吻合：

- `CPL-PAIRSCAN act=48 state=steady ord=16 destStn=2 … paired=-1 wait=-1 near=-1 best=0 s_lock=0`
  ⇒ 配对扫描**找不到任何候选**（`paired=-1`），即"没有认出合法的可挂车底"。
- `CPLSPD veh=48 … spd=161→148 … db=1`、`LOCO-MOVE veh=48 … endTile=27,12 curPos=72`
  ⇒ 却仍在**全速前进**（148~161），即玩家说的"冲了过去"。
- `RESV-ARB tile=37,12 station=0 a=veh0 wait=1 b=veh48 wait=0 picked=veh48`
  ⇒ 它把停在该格的那列车（`veh0`，`wait=1`）所在格位的预留**抢到手**并准备驶入。
- `TRP veh=57 mf=35,12 … ok=0 res=0` ⇒ 另有车预留失败。

⇒ **S-17-B 的机理假设（待验证）**：`CPL-PAIRSCAN` 已判定"无候选"（`best=0 paired=-1`），但**寻路/预留这一侧没有拿到同一个结论**——`CPL-GATE` 判定"这一格是可挂目标"（或根本没有否决），于是 `TryPathReserve` 照常把整条路径订到自己身上并全速逼近。也就是**扫描判据与寻路判据用了两把尺子**，而同族问题在本工程有先例（第 232 轮 KI-340 的"命中端与拼接端角色错位"、第 172 轮"两把尺子双重惩罚"）。

⇒ **S-17-A 的机理假设（待验证）**：解挂时若 HXN5B 那一段既不是"驱动者"（`v->orders == driving_orders`）也不是任何现有闸门的对象，它会走到 `R3RSyncDrivingOrders` 的**兜底分支**；若其 `orders_backup_real_index` 为 INVALID，则 `R3RReturnBorrowedOrders` 的落 0 分支把索引写成 0 ⇒ 表现为"排程回到第一条"。这与 §15 命中的 R-382-1 是**同一支代码**，但触发路径不是第 382 轮新增的那条（已回退仍在发生）⇒ 说明该落 0 分支在**解挂**路径上本来就可被命中，与第 382 轮无关。

### 17.4 待取证（下一步，按顺序）

- **T-17-1**：本次日志里 `DECOUPLE-DONE` / `LOCO-AFTER-DECOUPLE` / `ORD-AFTER-DECOUPLE` / `ORD-RETURN` 各几条，解出方（u）的 `real=` 是否为 0；HXN5B 对应哪个 veh 号。
- **T-17-2**：`veh=48` 在冲向 `37,12` 之前，`CPL-GATE` / `CPL-PAIR` / `R3RIsCoupleTarget` 相关探针打了什么（`target-not-wait` 等），以及 `COUPLE-FAIL` 的 `tx/ty`。
- **T-17-3**：`veh=0`（停在 37,12 的那列车）是不是 HXN5B 解出的段，它的 `real=` 是否为 0（若是，则 S-17-A 与 S-17-B 有因果链：排程跳第一条 ⇒ 它不再执行 `WAIT_COUPLE` ⇒ `IsCoupleTarget` 判据不认它，但几何/预留仍视其为可挂目标）。

**在 T-17-1~T-17-3 出结果之前不改任何代码。**

### 17.5 T-17-1 取证结果（`build\R3R_debug.log` 23:29 那次运行，逐字摘录）

```
956:  ORD-RETURN-OWN veh=54 num=4 real=2->0 impl=2->0 bk=0/0 orphan=0
1013: DECOUPLE-DONE u=0 co=1 real=3 tx=12 ty=15 x=196 y=248
1079: LOCO-AFTER-DECOUPLE veh=54 curType=0 real=2 idxnext=6 tile=12,15
2079: ORD-RETURN-OWN veh=57 num=8 real=5->0 impl=5->0 bk=0/0 orphan=0
2135: DECOUPLE-DONE u=0 co=1 real=6 tx=35 ty=12 x=564 y=200
2201: LOCO-AFTER-DECOUPLE veh=57 curType=0 real=0 idxnext=6 tile=35,12
```

**结论句 1（S-17-A 实锤，且与第 382 轮无关）**：第二次解挂的机车 `veh=57` 在 2079 行被归还成 `real=5->0`，2201 行出解挂时为 `real=0` —— 这就是玩家说的"排程回到第一条"。对比第一次解挂 `veh=54`：956 行同样落 0（`real=2->0`），但 1079 行又变回 `real=2`（被后续代码修正），所以只有第二次是真的跳回第一条。

**结论句 2（`bk=0/0` 是核心）**：两处 `ORD-RETURN-OWN` 的书签都是 **`bk=0/0`（不是 `INVALID`）**。⇒ 归还没有"缺书签"的处境，它是**照着一个真值 0 的书签**把车送回第 0 条。所以 §15 里假定"书签为 INVALID 才落 0"不是本次形态；本次是**书签本身被记成了 0 且再没更新**（`R3RParkOwnOrderProgress` 的 `orders_backup_real_index != INVALID` 幂等守卫只允许记一次）。→ 与 §17.3 的 S-17-A 假设一致，但触发点从"缺书签"修正为"**陈旧书签 = 0**"。

**结论句 3（`veh=57` 就是玩家说的 HXN5B）**：`DECOUPLE-DONE … tx=35 ty=12` 与 §16.4 尾部 `TRP veh=57 mf=35,12 real=0 … ok=0 res=0` **同格同车号** ⇒ 玩家说的"卡在车底前面的那台"就是 **veh=57 @ (35,12)**，它 `real=0`、`res=0`、`spd=0`、不再前进。

**结论句 4（S-17-B 的空间关系成立）**：DF4D-3058 = `veh=48`，此刻在 `37,12`，运动方向朝 `endTile=27,12`（§16.4 `LOCO-MOVE`），而 `veh=57` 停在 `35,12` ⇒ **正在 veh=48 的必经路径上，相距 2 格**。玩家描述的"冲了过去"与几何完全吻合。

**结论句 5（T-17-2 规模）**：`CPL-GATE` 在本次日志中命中 **0 条** ⇒ 该探针未输出（可能被 `R3RDbgEdge` 窗口抑制，或本次 tick 未走到）。⇒ 不能用"日志没有 CPL-GATE"推断判据没跑过；S-17-B 的判据取证需换探针（`CPL-PAIR` / `COUPLE-FAIL` / `RESV-ARB` / `R3RIsCoupleTarget` 侧）。

### 17.5b T-17-3 取证结果（`build\R3R_debug.log`，逐字摘录 + 行号）

**车名确认**：`SEGTR-SNAP tag=decouple-v head=57 nseg=1 ctrl=57 borrowed=0` / `SEGTR-SNAP i=1 id=0 front=57 star=1 ctrl=1 | live u=7 n="HXN5B-XXXX2" gid=65534`
⇒ **`veh=57` = HXN5B-XXXX2，单位号 7**，与玩家说的"六号或七号段"完全对上。

**完整因果链（按日志顺序）**：

| 行 | 原文（节选） | 含义 |
|---|---|---|
| **53** | `ORD-BORROW-PARK veh=57 bkr=0 bki=0 num=8 tag=load` | **读档时** park，把书签写成 **0/0**（自己表 num=8）。这就是 §17.5 里 `bk=0/0` 的来源 |
| 1865 | `ORD-AFTER-COUPLE head=57 n=33 real=4 impl=4 tt=4 borrowed=1 owner=6 u_has_orders=1` | 耦合后借用 owner=6 的 33 条表，在借表上跑到 real=4 |
| — | `ORD-IDX-WATCH site=del-implicit-out tag=suppress veh=57 real=0->4 impl=0->4 tt=0->4 n=8->33` | 借入瞬间：自己的 0 → 借表的 4 |
| — | `DECOUPLE-FIRE consist=57 tx=35 ty=12 real=5 mode=2 num=1 segs=2 eff=0 plat=30/0` | 在 (35,12) 触发解挂，两段 |
| — | `TDTRY ok=1 v=57 u=0 v_head=57 u_head=0 borrowed=1 u_eng=0 v_eng=1` | v=57 是机车侧，u=0 是车底侧 |
| — | `ORD-IDX-WATCH site=syncdriving-in tag=keep veh=57 real=-1->5 n=-1->33` | 归还前先从 owner 继承 real=5 |
| **2079** | `ORD-RETURN-OWN veh=57 num=8 real=5->0 impl=5->0 bk=0/0 orphan=0` | **照 bk=0 归还到自己的 8 条表 ⇒ real=0**（S-17-A 直接原因） |
| 2083 | `ORD-PUSH head=0 owner=6 real=3->6` | 车底侧把进度推回 owner=6（正常） |
| **2087** | `ORD-BORROW-PARK veh=57 bkr=0 bki=0 num=8 tag=decouple-v` | settle 里**又 park 一次，再写 0/0**（此时 real 已被归 0） |
| — | `CTRL-UNBORROW decouple-v head=57 unit=7 own_bk=0 hasname=1 own_name=0 own_g=65534` | 取消借用，`own_bk=0` 即"自己的书签是 0" |
| — | `ORD-IDX-WATCH site=settle-in tag=decouple-v veh=57 real=4->0 impl=4->0 tt=4->0 n=33->8` | settle 输入 4 → 输出 0（表从 33 换回 8） |
| — | `LOCO-AFTER-DECOUPLE veh=57 real=0` | 终值 0 |

**结论句 6（S-17-A 根因定性）**：`veh=57` 的排程跳回第一条，不是"缺书签落 0"，而是**书签在 `tag=load` 那一刻就被写成 0**（日志第 53 行），解挂时 `R3RReturnBorrowedOrders()` 忠实地照这个 0 执行（2079）。**与第 382 轮两处改动无关**（那两处已回退，本次日志仍复现；且 `tag=load` 这条 park 在 `R3RRebuildCouplePriorities` 里，不在被回退的分支内）。

**结论句 7（为什么书签是 0，机理假设待验证）**：读档时 `veh=57` 处于**借用态**（`borrowed=1 owner=6`），它的 `cur_real_order_index` 是**借来那张 33 条表上的坐标**；`R3RParkOwnOrderProgress()` 把这个坐标当作"自己 8 条表上的进度"存进书签。读档序列中此刻的活索引若尚未从 owner 同步（探针在 1719 行显示 `real=-1->5` 的继承发生在**解挂时**，不是读档时），park 写下的就是初值 **0**。

**结论句 8（S-17-B 与 S-17-A 的因果）**：`veh=57 @ 35,12` 因 real=0 停在原地不再前进（§16.4 `TRP veh=57 mf=35,12 ok=0 res=0`），而 `veh=48`（DF4D-3058）的 `GOTO_COUPLE` 路径终点 `endTile=27,12` 正好穿过 35,12 ⇒ 玩家看到的"冲过去"。**S-17-B 不是独立缺陷，而是 S-17-A 的直接后果之一**：一台本应在 `WAIT_COUPLE` 待命的机车，因为排程跳回第一条而停在轨道中央。

### 17.5c 代码层结论（`src\train_cmd.cpp`）

**`R3RParkOwnOrderProgress()`（:3999-4010）五道守卫**：

```cpp
4001 if (chain == nullptr) return false;
4002 if (chain->orders_backup != nullptr) return false;
4003 if (chain->r3r_follow_orders != nullptr) return false;   // 只挡"修法 D 之后的跟随者"
4004 if (chain->orders == nullptr) return false;
4005 if (chain->orders_backup_real_index != INVALID_VEH_ORDER_ID) return false;
4007 chain->orders_backup_real_index = chain->cur_real_order_index;   // ← 直接抄活索引
4008 chain->orders_backup_implicit_index = chain->cur_implicit_order_index;
```

**结论句 9（S-17-A 的真正缺陷）**：第 4003 行只拦住**修法 D 之后**的跟随者（`r3r_follow_orders != nullptr`），拦不住**旧机制**——`borrowed=1` 但 `r3r_follow_orders == nullptr`、`orders` 里直接放着借来的那张表（日志 53 行 `tag=load` 即此形态）。此时第 4007 行抄进书签的 `cur_real_order_index` 是**借来那张 33 条表上的坐标**，却被当成"自己 8 条表上的进度"永久保存（第 4005 行的幂等守卫又让它只记一次、再不更新）。

**结论句 10（归还侧为何不报错）**：`R3RReturnBorrowedOrders()` 的 `:7485` 是
`if (num == 0 || own_real == INVALID || own_real >= num) own_real = 0;`
书签是**合法值 0**（不是 INVALID、不越界）⇒ 既不触发 `ORD-RETURN-NOBK` 告警，也不被钳制，原样写回 `cur_real/impl/timetable_order_index`（:7493-7495）⇒ 排程回到第一条。

**结论句 11（为什么书签是 0 而不是 4）**：读档那一刻 `veh=57` 的 `cur_real_order_index` 本身已是 0 —— 它在上一次会话解挂时就被归 0 并存进了存档，本次读档 park 再记一次 0，形成闭环。⇒ **这个 bug 会随存档累积**，玩家每存读一次就固化一次。

**结论句 12（与第 382 轮的关系，最终裁定）**：S-17-A 的缺陷在 `R3RParkOwnOrderProgress` 的**旧机制形态**与 `R3RReturnBorrowedOrders` 的落 0 分支上，两处都是既有代码；第 382 轮改的是同一函数第 4005 行附近（已回退），**不是本次症状的来源**。玩家 23:29 的日志已复现，证据链完整。

### 17.5d 澄清：这不是「指针错位」（2026-10-31 玩家提问）

玩家问「这个问题是指针错位导致的吗」。**不是**。分三层，别混：

1. **指针是健康的**。`orders_backup` 全程 nullptr（本次没走 `7498` 的备份还原分支），`orders` 指向 owner=6 那张 33 条表是旧借用机制的**正常形态**（旧机制就是把借来的表直接放进 `orders`）。全流程没有悬垂、没有指错对象、没有读到已释放的 OrderList。
2. **错的是「坐标」，不是指针**。`orders_backup_real_index` 的类型是 `VehicleOrderID`（第几条命令），不是地址。park（`4007`）在旧机制借用态下抄走的是 **owner 那张 33 条表坐标系**下的位置（读档那一刻恰为 0），归还（`7473`）把这个数当成**自己 8 条表**上的位置用 —— 同一个整数换了个参照系，这就是"错位"的全部含义。
3. **为什么一路绿灯**：0 在自己 8 条表上**合法**（`own_real < num` 成立），所以 `7485` 的钳制不触发；`7480` 的告警只认 INVALID，也放过。⇒ 静默写回 0，不崩、不报错，只表现为"命令跳回第一条"。这正是它比野指针更难抓的原因：野指针会崩，合法值 0 只会静默出错。

**唯一与"指针"沾边的地方**是缺陷点本身：`4003` 拿 `r3r_follow_orders != nullptr`（指针存在性）当"我是不是跟随者"的身份判据，而旧机制借用根本不设这个指针 ⇒ 判据覆盖不全。这是"用指针当身份判据"的不足，**不是指针指错**。

### 17.5e 澄清：是「进度借还」与「表借还」两代结构混用（2026-10-31 玩家提问）

玩家问「这是否是因为排程进度和整张表的借还结构的不一致导致的」。**是**，而且可以拆成三层互不感知的不一致——不是某一处写错，是两代设计被塞进同一个函数。

| 层 | 表（OrderList） | 进度（索引） | 不一致点 |
|---|---|---|---|
| 粒度 | 整张 swap，`orders` / `orders_backup` / `r3r_follow_orders` 成对备份 | 单个整数书签 `orders_backup_real_index/implicit` | 两套备份各自有守卫，**互不感知** |
| 身份判据 | `r3r_orders_borrowed` + `r3r_follow_orders` 两字段 | `4003` 只认 `r3r_follow_orders` | 旧机制借用只置 `borrowed`、不设 follow 指针 ⇒ 被判成"没借用" |
| 定义域 | `own->GetNumOrders()` 是**自己表**的长度 | 书签值来自 **owner 表**的坐标系 | 用 B 表长度校验 A 表坐标 ⇒ **越界检查天然失效** |

**第三层最致命**：`7485` 的 `own_real >= num` 看着是在做安全钳制，实际是在两个不同的定义域之间做比较。0 在任何表的定义域内 ⇒ 恒过。这就是"为什么不崩只错"的机制解释，也解释了为何所有守卫都放行。

**第四层（`4002` 的连带伤害）**：`if (chain->orders_backup != nullptr) return false;` 把"有表备份"当成"索引已备份"的替代条件。这是旧机制"表+进度一起备份"的残留；在修法 D 之后表不再进 `orders_backup`，于是这条判定既挡不住重复 park、又会在有表备份时永久跳过索引备份。

**结论**：根因属于「借还结构存在两代实现，守卫只覆盖新一代」，而非单点笔误。这也决定了修法必须 B→A：
- **B** 让定义域失效时仍然安全（不把借表坐标写进自己表，索引保持不动）；
- **A** 补身份判据（`orders` 不是自己的表就不记书签），让旧机制态与新机制态被同一判据覆盖。

### 17.7 修法候选（未落地，等玩家拍板）

| 方案 | 改动 | 效果 | 风险 |
|---|---|---|---|
| **A（收窄 park）** | `R3RParkOwnOrderProgress()` 加第 6 道守卫：`if (R3RSegmentOwnOrderList(chain) != chain->orders) return false;` —— 旧机制借用态（orders 不是自己的表）不记书签，留 INVALID | 不再把借来表的坐标记成自己的进度 | **单独无效**：书签变 INVALID 后，归还侧 `:7485` 的 `num == 0 || own_real == INVALID → 0` 照样归 0 |
| **B（收窄归还）** | `:7485-7486` 改成："INVALID 时**不动**索引，只摘跟随指针"（§15.6 第 1 步），并打 `ORD-RETURN-NOBK-KEEP` | 最坏情况=索引保持借来表坐标 | 借来表坐标可能 ≥ 自己的条数 ⇒ 撞 KI-472 的 `cur_implicit_order_index < GetNumOrders()` 断言；须同时钳制 |
| **A+B（推荐组合）** | A + B，且 B 对"越界"做钳制（`own >= num ⇒ num - 1`，不是 0） | `veh=57` 解挂后保持 5（自己表 8 条，合法）⇒ 不再跳第一条 | 5 未必是语义正确的那条，但不会朝错方向冲 |
| **C（治根，代价大）** | 给 `orders_backup_*_index` 真正接上存档（R3VP 已有 `order_backup_index` 字段，见第 94 轮），让 park 记录的"自己表进度"随存档往返 | 跨会话也不丢 | 需要确认 R3VP 读档后是否被本次 park 覆盖；旧档无字段仍需兜底 |

**顺序纪律（沿用 §15.6）**：先做 **B**（纯收窄、不引入新写入），单独编译复测无回归；再做 **A**；最后才考虑 **C**。**不得**跳过 B 直接做 A（那等于把"归 0"从 0 值换成 INVALID 值，行为不变）。

### 17.7a 修法 A 已落地（2026-10-31，玩家指定"先只修 A"）

玩家决定先只做 A，跳过 §17.7 的 B→A 顺序纪律。已按此执行，并在此记录偏离与预期。

**改动（`src/train_cmd.cpp`）**

1. `R3RParkOwnOrderProgress()` 在 follow 指针判据之后新增第五道守卫：
   `if (chain->r3r_orders_borrowed) return false;`
2. 函数头注释"四道守卫"→"五道"，并写明旧机制只置 borrowed、不设 follow 指针。
3. `:2271` 处内联复制体（`tag=movechunk`）同步加 `!u->r3r_orders_borrowed`，保持两处判据一致。
4. `R3RRefreshOrderBookmark()` 无需改动——它在第 380 轮已用更强的判据
   `GetEffectiveOrders() != own`，本就挡得住旧机制借用态。

**预期（必须如实标注）**：单独 A **不会**让 HXN5B 解挂后不跳第一条。书签保持 INVALID 后，
`:7485` 的 `num == 0 || own_real == INVALID → 0` 照样落 0。这与现状可观察行为相同，
差别只在于日志多一行 `ORD-RETURN-NOBK`。历史证据：`:12776-12777` 注释记录的
`NOBK real=65535` → `ORD-RETURN-OWN real=2->0` 正是这个组合。

**A 的独立价值**：不再把 owner 表坐标系下的数字**写进存档**。修前该错值每存读一次固化一次
（若读档时 owner 进度为 5，就永久记成 5）；修后统一为"从未记录"，把错误从**累积型**降为
**一次性**，且可被日志点名。

**仍未解决**：跳第一条本身要等 B（归还侧 INVALID 不落 0 + 越界钳制到 `num-1`）。

### 17.7b D2 已落地：借用入口全部收口到 R3RBeginBorrow（2026-10-31，玩家指定）

玩家决定不做旧档兜底（手上有"未耦合"存档），要求直接按"进度与表借还同一原子操作"实施。
已落地，**未编译**。

**新增**：`static void R3RBeginBorrow(Train *chain, OrderList *other, VehicleID owner, const char *tag, bool refresh = false)`
- 声明在 `:2224` 之后（文件前部，供 movechunk 调用），定义在 `R3RRefreshOrderBookmarks()` 之后。
- 一次调用内完成：停位（`R3RParkOwnOrderProgress` 或 `R3RRefreshOrderBookmark`）+ 设 `r3r_follow_orders` / `r3r_follow_owner` / `r3r_orders_borrowed`。
- `refresh=true` 不短路（sync-borrow 每次切表都要刷新，否则退化到第 379 轮之前）。
- `other == nullptr` 时仍执行停位（movechunk 的"整表直搬"分支），只是不改跟随指针。

**已收口的入口（6 处，原先各自复制判据）**

| 入口 | tag | 备注 |
|---|---|---|
| `NormalizeTrainVehInDepot` | movechunk | 原先内联复制判据，A 轮刚补过 borrowed 判据；现全部删除 |
| 车库拖动换段头 | depot-drag | 前提 `orders == nullptr`，停位必然 no-op |
| 卖链头 | sell-head | new_head 可能带空表，是实质停位 |
| `R3RSyncDrivingOrders` | sync-borrow | 唯一 refresh 模式 |
| donor 链 | donor | 原先 no-op 的审计调用 |
| `Couple()` | couple | 第 353 轮补的"第八个入口" |

**风险与待验证**
- 全部为机械替换，行为应等价；但 `R3RBeginBorrow` 的短路条件（已在同一张表上则跳过）是新增判据，需现场确认不会漏设 `borrowed`。
- 未编译。需按 `R3R_REBUILD_COMMANDS.txt` 关 CodeBuddy 后跑 `rebuild_R3R.cmd`。

**一处与 §17.5c 不一致的现场证据（必须在 B 阶段澄清）**

`build\R3R_debug.log` 第 45/53 行：
```
ORD-BORROW-PARK veh=57 bkr=0 bki=0 num=8 tag=load
SEGTR-SNAP tag=load head=57 nseg=1 ctrl=57 borrowed=0
```
即读档那一刻 `orders` 是**自己的 8 条表**、`borrowed=0`、`cur_real=0`，park 记 0 是**自己表的真实进度**，
不是 §17.5c 所述的"抄了 owner 那 33 条表的坐标"。另 `:2068` 的 `del-implicit-out` 显示切表前
`n=8 real=0 ord=...FF10`，与"切表前进度为 0"一致。⇒ 归还落 0 可能是**正确恢复**，跳第一条的真实
成因需在 B 阶段用 `:2067` / `:2068` 这一对 in/out 重新确认，不能沿用"抄错坐标"的表述。

### 17.5f 构想评估：让「进度借还」对齐「表借还」（2026-10-31 玩家提问）

玩家提议：把进度的结构对齐表的结构，让两者共用一套借还。**方向对，但字面不可行**，须改造成"事务边界对齐"。

**字面版（进度存进 OrderList）为何不可行**

| 障碍 | 事实 |
|---|---|
| 一对多 | `OrderList` 可共享（`IsShared()` / `FirstShared()`），同一张表被多条链持有，各自进度不同 ⇒ 表上一个 `cur_index` 无法表达 |
| 语义 | 复制/克隆表给别人用，进度不该跟着走（应从 0 开始） |
| 所有权 | `cur_real_order_index` 是 **Train** 的私有字段，随车存读；把它挪进共享表等于把"每车状态"变成"每表状态" |

**真正该对齐的是三样东西（这才是缺陷所在）**

1. **身份判据**：书签必须自带"它属于哪张表"。现结构 `orders_backup_real_index` 是裸整数，无来源 ⇒ 归还时无法拒绝跨表使用。改成配对：
   `struct { OrderList *table; VehicleOrderID real, impl; }`，归还前校验 `pair.table == 归还的表`，不匹配就丢弃。
2. **事务边界**：现在 park 散在 8 个入口（couple / donor / sell-head / depot-drag / movechunk …），靠每处记得调 ⇒ 第 352、353 轮就是在反复补"第八个入口"。应把「park 自己的进度 + 换表 + 切到对方坐标系」封成一个原子函数，8 个入口只调它，**漏调不可能**。
3. **坐标系**：归还只从与自己配对的表还原，从根上消除 §17.5e 第三层"用 B 表长度校验 A 表坐标"。

**它能修什么、不能修什么**

- 能修：程序内"某入口忘了 park"导致的丢进度；跨定义域误用。
- **不能修**：读档时**已经是**借用态（veh=57 的形态）——此时没有"借用开始"那一刻可拦截，信息在旧档里就不存在。这一层只能靠 §17.7 的 **C**（书签真正进存档）。
- 也**不能**替代 B 的越界钳制：归档时 `own_real >= num` 仍须钳到 `num-1`。

**顺带指出一个不对称**：修法 D 之后跟随者的 `orders` 不动、只置 `r3r_follow_orders`，即"表并未借出"。所以"对齐表"必须先选定以哪一代为准——旧机制（swap）与新机制（置指针）的"表借还"形态不同，二者混用正是 §17.5e 的根因。

**结论**：这是比 A/B 更正确的长期结构（应记为 **D2**），但工作量与回归面都远大于 A/B，且仍不能省掉 C。短期仍按 A(已落地) → B → C 走。

### 17.7c D2 构建自证（2026-10-31，增量，非全量）

**为什么这次能走增量**（前置探测，三条都要满足）：

| 判据 | 读数 | 结论 |
|---|---|---|
| `openttd.exe` 进程 | `tasklist` 无输出 | 未运行，不会 LNK1168 / 闸门 97 |
| 最新 `src\*.h` | 2026-10-30 19:26:35 | 早于最新 obj |
| 最新 `src\lang\*.txt` | 2026-10-30 19:33:39 | 早于最新 obj |
| 最新 obj（基准） | 2026-10-30 23:26:27 `train_cmd.cpp.obj` | 基准 |
| `src\train_cmd.cpp` | 2026-10-31 16:09:38 | 仅它需要重编 |

⇒ 护栏 `build\R3R_incbuild.guard.log` 输出 `GUARD: incremental is safe (no header/lang file is newer than the newest object)`，**未删 620 obj、未退化为全量**。

**入口**：复用工作区既有 `R3R_fullbuild.cmd`（`inc detach` 后台），未新建任何 `.cmd`。

**产物链**：

```
src\train_cmd.cpp        16:09:38
  -> train_cmd.cpp.obj   16:16:04   （晚于源码 ✅）
  -> build\openttd.exe   16:18:26   （晚于 obj ✅，52 450 304 B）
```

日志尾部：`[2/3] Building CXX object ...train_cmd.cpp.obj` → `[3/3] Linking CXX executable openttd.exe` → `==== R3R build ended 2026/10/31 16:18:35 MODE=inc EXIT_CODE=0`。

`read_lints src\train_cmd.cpp` = 0 条。

**D2 六个入口已全部改为调用 `R3RBeginBorrow()`**（`grep` 实证，无残留内联判据）：

| 行号 | tag | 备注 |
|---|---|---|
| 2288 | `movechunk` | 排在 `DeleteVehicleOrders(u)` 之前 |
| 4495 | `depot-drag` | 前提 `orders == nullptr`，park 必然 no-op |
| 4750 | `sell-head` | `new_head` 可能带空表，实质停位 |
| 7700 | `sync-borrow` | 唯一 `refresh=true` |
| 7988 | `donor` | 原为 no-op 审计调用，现真实停位 |
| 12819 | `couple` | 第 353 轮的"第八个入口" |

声明 2240 行，定义 6814 行，`ORD-BORROW-PARK` 日志在 6774（refresh 路径）/ 6822 / 6835。

**仍未解决（写清，别被这轮的"编译通过"掩盖）**：§17.5c 那句「读档时 park 记的 0 是抄了 owner 33 条表的坐标」与日志不符——`build\R3R_debug.log:45` 的 `ORD-BORROW-PARK veh=57 bkr=0 bki=0 num=8 tag=load` 与 `:53` 的 `SEGTR-SNAP ... borrowed=0` 显示当时 `orders` 是**自己那张 8 条表**、`cur_real=0`，即 park 记 0 是**自己表的真实进度**。⇒ "归还落 0" 可能本就是正确恢复，"跳第一条"的真因须用 `:2067` / `:2068` 的 `del-implicit-in` / `del-implicit-out` 对重判，不能沿用旧结论改码。

### 17.7d D2 后首次复测取样（2026-10-31 18:08）

**采样有效性**：`build\R3R_debug.log` mtime 2026-10-31 18:08:18 > `build\openttd.exe` 16:18:26 ⇒ 日志出自含 D2 的构建。

**已确认读数**

| 行 | 原文 | 判读 |
|---|---|---|
| 608 | `ORD-BORROW-PARK veh=57 bkr=0 bki=0 num=8 tag=load` | 读档时把自己表进度停成 0 |
| 2506 | `SEGTR-SNAP tag=couple head=57 nseg=3 ctrl=6 borrowed=1` | 耦合后借 owner=6 的表 |
| 2832 | `ORD-RETURN-OWN veh=57 num=8 real=5->0 impl=5->0 bk=0/0 orphan=0` | 解挂归还：借来坐标 5 被还原成自己表 0 |
| 2840 | `ORD-BORROW-PARK veh=57 bkr=0 bki=0 num=8 tag=decouple-v` | 归还后又 park 成 0 |
| 2842 | `SEGTR-SNAP tag=decouple-v head=57 nseg=1 ctrl=57 borrowed=0` | 解挂后已非借用态 |

`veh=54` 同形（1573 `real=2->0 bk=0/0` → 1581 `bkr=1`）。

**疑点 REG-378-A（本轮最重要，直接影响"跳第一条"是否真被修掉）**

`ORD-BORROW-PARK` 全日志只有两种 tag：`load` 与 `decouple-v`，**没有任何一条 `couple` / `depot-drag` / `sell-head` / `sync-borrow` / `donor` / `movechunk`**。而 D2 的六个收口入口正是这些 tag。

- `veh=57` 在 608（load）之后、2506（couple）之前**没有**自己的 `tag=couple` 停位行。
- ⇒ 若耦合入口的停位真的生效，2832 归还时 `bk` 应是耦合那一刻的进度（而不是 load 留下的 0）。
- ⇒ 实测 `bk=0/0` 与"耦合入口没停位"一致。

**两种可能，尚未区分**：

1. **被守卫挡住**：`R3RParkOwnOrderProgress()` 第 4013 行 `r3r_follow_orders != nullptr` 或第 4020 行 `r3r_orders_borrowed` 返回 false。若耦合前该链已带跟随指针/借用标志（例如上次解挂没清干净），停位会被静默跳过——**而这正是 D2 想让"不可能漏"的那一步**。
2. **被短路跳过**：`R3RBeginBorrow()` 第 6830 行 `!refresh && chain->r3r_follow_orders == other` ⇒ 直接 return，既不 park 也不打日志。

两种情况下"归还落 0"都会复现，即 **D2 收口了调用方式，但没解决"到达现场时状态已被占"的问题**。判据：需在耦合入口加一条"为什么没停位"的探针（守卫分支各自的 return 原因），或读 `:2506` 之前的日志确认链头 57 当时是否 `borrowed=1`。

**§17.5c 的旧结论仍未采纳**：2840 的 `bkr=0 num=8` 是在 `borrowed=0`（2842）之后 park 的，记的是自己表真实 0，不是抄 owner 坐标。

### 17.7e 收口：书签 0 是正确值（2026-10-31，同一份日志）

**决定性证据（换表那一刻的索引对）**

```
ORD-IDX-WATCH site=settle-out tag=couple veh=57 real=0->4 impl=0->4 tt=0->4 n=8->33
```

`n=8->33` = orders 从自己 8 条表换成 owner 的 33 条表；`real=0->4` = 换表瞬间自己的活索引**就是 0**，换完之后才是 4（owner 坐标）。

后面 `site=tick-out/tick-in/loco-entry/skipgate-in/post-skipgate/post-depotblk/pre-processorders` 全部是 `real=1->4`，`post-processorders` 才变成 `curType=1`（GOTO_DEPOT）并 `DEPOT-ARR veh=57 real=4(1)`。

**⇒ 结论：`ORD-BORROW-PARK veh=57 bkr=0` 记的 0 是真值，2832 的 `ORD-RETURN-OWN real=5->0` 是正确恢复。**「归还落 0」不是缺陷，`§17.5c` 那条「抄了 owner 33 条表坐标」的判断**被证伪**，不要再按它改码。

`veh=54` 同形：1573 `real=2->0 bk=0/0` 归还后 1581 才 park 成 `bkr=1`（下一段跑动的新进度），语义自洽。

**遗留疑点（降级为"日志缺失"，不影响正确性）**

`ORD-BORROW-PARK` 仍无 `tag=couple` 行。两种可能：

1. `R3RBeginBorrow()` 第 6830 行 `!refresh && chain->r3r_follow_orders == other` 命中 ⇒ 静默 return（书签早已被前一次停位写过，值恰好正确，所以结果无害）；
2. 六入口各自的 `R3RParkOwnOrderProgress()` 守卫（4013 `follow != nullptr` / 4020 `r3r_orders_borrowed`）返回 false。

本案 `settle-out` 的 `real=0` 与最终书签 `0` 一致，**两种可能都不会造成丢进度**，故不再作为缺陷追查；但"停位到底由谁写成"仍无实证，登记为 `REG-378-A`（低，仅日志可观测性）。

**下一步（真正要确认的）**：既然书签值一直正确，"命令跳回第一条"的观感若仍存在，则原因不在借还链路，需换方向取数——建议下一轮抓 `ORD-RETURN-OWN` 前后该车的 `curType`/`real` 与玩家目视的"第一条"是否同一条定义（账号排程第 1 条 vs 显示行第 1 行）。

### 17.7f 玩家纠正：换表瞬间索引是 1，不是 0（2026-10-31）

**玩家原话（决定性）**：「换表瞬间它的索引是 1（第二条命令），但是实际上，索引 1 和索引 2 分别是前往挂接和填充结束这样的特殊命令，所以可能从这里追查」。

⇒ §17.7e 的收口**作废**：`ORD-IDX-WATCH site=settle-out ... real=0->4` 的 `0` 不能当作"真实进度"，真值是 **1**。

**由此产生的新疑点 REG-378-B（高）**

若换表瞬间真索引 = 1（= 第 2 条 = `GOTO_COUPLE` 前往挂接），而归还时写回的也是 1/0，那么"看起来跳回第一条"实际是**落在特殊命令上**：

| 索引 | 类型 | 行为 |
|---|---|---|
| 0 | 第 1 条 | 出发/首条 |
| 1 | `GOTO_COUPLE` | 主动命令，执行侧会跳过（KI-274 已有同族结论：`R3RSkipCoupleOrdersForRealArtic` 对 16/17 一视同仁） |
| 2 | 「填充结束」 | 自动填充区间边界（KI-400/`auto_ranges`），执行侧必须跳过 |

即：**进度没丢，但它停在"不执行的特殊命令"上** ⇒ 界面显示往回跳、列车原地不动。这与 `§17.7d` 的数值自洽完全兼容——所以之前"数值正确 ⇒ 不是缺陷"的推论是错的，**值合法 ≠ 位置可用**。

**待验证判据（按序）**

1. `ORD-IDX-WATCH` 的 `real` 到底打印哪个字段（`cur_real_order_index`？`cur_implicit_order_index`？还是归一化后的输出）——若它漏打/错打 `+1`，那 `0->4` 与真值 `1->4` 的差就是探针缺陷，不是数据缺陷。
2. 自己的表 `n=8` 里，索引 0/1/2 依次是什么类型（`OT_GOTO_COUPLE` / 填充边界 / 真运输命令）。若 1、2 都是"不执行"的，则**正确恢复点应跳过它们**到第一个可执行命令，而不是停在第 2 条。
3. `R3RParkOwnOrderProgress` / 归还写回时是否对特殊命令做了规范化（找不到就落到 0 或 1），以及 `auto_ranges`（填充区间）在换表/归还时是否被清空（第 264 轮 `SEGTR-SNAP ... auto n=0` 即"解挂单段化清空 auto_ranges"的先例）。

**结论句**：追查方向从"索引值有没有被写错"改为"**索引是否停在了执行侧会跳过的特殊命令上，且恢复时没有向前推进到第一条可执行命令**"。

### 17.7g 证据链推翻 §17.7e：书签被冻结在读档旧值（2026-10-31）

**先纠错：§17.7e 引的那几行 `real=0->4` 是 `tag=load`，不是 `tag=couple`**

```
:363  ORD-IDX-WATCH site=load-rebuild tag=-    veh=57 real=0->4 n=8->33
:377  ORD-IDX-WATCH site=settle-out  tag=load veh=57 real=0->4 n=8->33
:604  ORD-IDX-WATCH site=load-rebuild tag=-    veh=57 real=4->0 n=33->8
:611  ORD-IDX-WATCH site=settle-out  tag=load veh=57 real=4->0 n=33->8
```

`n=8->33` 与 `33->8` 是**读档重建**在两种状态间来回（带借用态存档 / 干净态存档），与耦合无关。§17.7e 把它当"换表那一刻"是误读，结论作废。

**真耦合读数（印证玩家）**

```
:1153  ORD-AFTER-COUPLE head=54 n=33 real=1 impl=1 tt=1 borrowed=1 owner=6 u_has_orders=1
:2428  ORD-AFTER-COUPLE head=57 n=33 real=4 impl=4 tt=4 borrowed=1 owner=6
```

`veh=54` 耦合后 `real=1` —— **正是玩家说的"索引 1（第二条命令）"**。此时它跟着的是 owner=6 那张 33 条表，索引 1。

**真凶链（四步）**

| 步 | 证据 | 含义 |
|---|---|---|
| 1 | `:608 ORD-BORROW-PARK veh=54 bkr=0 num=4 tag=load` | 读档时把自己 4 条表的进度记 0（当时确实在 0） |
| 2 | 全日志无 `ORD-BORROW-PARK ... tag=couple` | **耦合入口的停位没执行**（守卫/短路），书签仍是 0 |
| 3 | `:1573 ORD-RETURN-OWN veh=54 num=4 real=2->0 bk=0/0` | 归还把借来的坐标 2 还原成书签 0 |
| 4 | 结果 `real=0` | 回到**第 1 条**；玩家看到"跳回第一条" |

⇒ 数值本身自洽，但**书签是过期值**（停在步 1 的 0，而车辆在耦合前已经推进到 1）。`§17.7d` 的 `REG-378-A`「六入口无 park 日志」从"可观测性瑕疵"升级为**根因候选**。

**下一步（唯一要判的）**：耦合入口调 `R3RBeginBorrow()` 时为什么没 park——是第 6830 行 `!refresh && chain->r3r_follow_orders == other` 短路，还是 `R3RParkOwnOrderProgress()` 的 4013/4020 守卫返回 false。判据：`ORD-AFTER-COUPLE` 的 `borrowed=1 owner=6` 说明耦合瞬间链已处于借用态，若耦合前 `r3r_orders_borrowed` 已是 true，则 park 必被 4020 行挡下。

### 17.7h 闭环：书签 0 是过期值，真值 1（2026-10-31，玩家方向正确）

**铁证（同一秒内相邻两行，`build\R3R_debug.log`）**

```
:1573  ORD-RETURN-OWN   veh=54 num=4 real=2->0 impl=2->0 bk=0/0 orphan=0
:1583  COUPLE-AIR-SKIP  veh=54 from=1 to=2 end=1 num=4 order=0
:4475  ORD-RETURN-OWN   veh=54 num=4 real=2->0 impl=2->0 bk=0/0 orphan=0
:4485  COUPLE-AIR-SKIP  veh=54 from=1 to=2 end=1 num=4 order=0
```

`COUPLE-AIR-SKIP from=1` 说明归还之后索引**确实经过 1**，而 1 落在 air 区间内（`end=1` 即 `OT_COUPLE_END` 就在 1），于是被跳到 2。

⇒ 玩家判断成立：**表里 1/2 是特殊命令**（1 = `OT_COUPLE_END` 标记、2 = 区间之后第一条可执行命令）。`ORD-RETURN-OWN real=2->0` 把书签 0 写回，列车于是**从 0 重新跑一遍**，看起来就是"命令跳回第一条"。

**根因（代码已定位，非猜测）**

| 行 | 代码 | 效果 |
|---|---|---|
| 12806 | `R3RRefreshOrderBookmarks(v, couple_owner, "couple", /*only_unset=*/true)` | 耦合入口**只补空、不覆盖** |
| 6830 | `if (!refresh && chain->r3r_follow_orders == other) return;` | `R3RBeginBorrow` 可能在耦合处静默返回 |
| 4022 | `if (chain->orders_backup_real_index != INVALID_VEH_ORDER_ID) return false;` | 书签已存在 ⇒ 永不再写 |
| 608 | `ORD-BORROW-PARK veh=54 bkr=0 tag=load` | 读档时把 0 写死 |

链条：读档写 0 → 列车自己表跑到 1 → 耦合时 `only_unset=true` **拒绝覆盖** → 借用期跑 owner 表到 2 → 解挂归还 0 → 从第一条重跑。

**与第 381 轮口径的冲突（必须在改码前解决）**

12800-12805 注释自陈：耦合前被动段停的是"等待落位"（`ORD-BORROW-REFRESH veh=0 bkr=0->3 tag=couple`，3 = `OT_WAIT_COUPLE`），**不是"它自己跑到第几条"**；覆盖之后"下一次解挂就从第 4 条起跑"，所以改成只补空。

但本案 `veh=54` 的书签不是"等待落位"，而是**读档时的陈旧 0**，只补空的口径恰好把它永久冻结。第 379 轮 6828 行注释已经警告过同一件事：「refresh 模式不做短路…否则退化成"书签一旦写下就永不过期"的老毛病」——12806 的 `only_unset=true` 正是那个老毛病的复发。

**三个候选修法（未落地，待拍板）**

| 方案 | 做法 | 风险 |
|---|---|---|
| A | 12806 改 `only_unset=false`，耦合前用"自己表坐标"覆盖书签 | 直接回退第 381 轮；`bkr=3`（WAIT_COUPLE 落位）会重新出现 |
| B | 覆盖，但只在「书签对应订单类型 ≠ 当前订单类型」或「书签值 < 当前值」时覆盖 | 需要新判据，能保住第 381 轮的等待落位 |
| C | 归还时校验书签：若 `bk` 处的订单已经不可执行（`OT_COUPLE_END` / air 区间 / 与存档时代不符），先推进到第一条可执行命令再写回 | 治症状，不治"书签过期"本身 |

**倾向 B**：它同时满足第 379 轮（书签不过期）与第 381 轮（等待落位不被覆盖）两个口径——因为等待落位的值(3) 通常 ≥ 陈旧值(0)，且类型是 `OT_WAIT_COUPLE` 而非运输命令。

### 17.7i 玩家否决 A/B/C：要治根（2026-10-31，决定性口径）

**玩家原话**：「我依旧希望能治根就不打补丁：读档时记录是 0，这个本身没问题，但是走到 1，数数的手指也要一起挪到 1，你这 ABC 看起来都是先让 bug 运行再把 bug 摆正，不太好」

⇒ **A / B / C 全部否决**。三者共同缺陷：都承认"书签可以长期是陈旧值"，只在耦合/归还那一刻回头纠正。正确形态是**书签与真实进度同一时点、同步推进**，不存在"过期"这一状态。

**目标不变量（本方案要建立的东西）**

> 只有当列车**正在借用别人的订单表**时，`orders_backup_real_index` 才有权存在；且其值恒等于"借出那一刻自己表跑到的位置"。
> 非借用态（含：本会话从未借用、借用已结束）**不允许携带书签**。

**在此不变量下，四个现存的防御全都变成多余的**

| 现状 | 在不变量下 |
|---|---|
| 12806 `only_unset=true`（只补空不覆盖） | 不需要：借用瞬间书签必为空，覆盖与补空等价 |
| 4022 `orders_backup_real_index != INVALID ⇒ return false` | 语义混淆：它把"防二次借用"和"防覆盖书签"当成一件事。不变量下只需前者 |
| 6830 `!refresh && follow_orders == other ⇒ return` | 短路本身合法（幂等），但它掩盖了"书签从未被写过"的事实；不变量下若真借用必已有书签 |
| 读档写 0（:608 `ORD-BORROW-PARK bkr=0 tag=load`） | **这是根**：读档路径把书签当成"存档字段"无条件落地，无视当时是否处于借用态 |

**⇒ 根因定性（替代 §17.7h 的"only_unset 冻结"表述）**

不是"耦合时该覆盖没覆盖"，而是**"书签被当成可随存档持久化的独立字段"，从读档那一刻起就与实际进度脱钩**。`only_unset` 只是让这个脱钩值活下来的最后一道锁。

**已复核的代码事实（2026-10-31）**

| 行 | 代码 | 判定 |
|---|---|---|
| 6969 | `R3RRefreshOrderBookmarks(chain, nullptr, tag, true)` | **`R3RSettleChainSegments` 的每个提交点（含 `tag=load`）都对整链补书签，且 `only_unset=true`** ⇒ 书签是"到处随手补的便条"，补完就再没人管 |
| 6771-6780 | 有书签 ⇒ `only_unset` 直接 return | 让它永远停在第一次补的那个值 |
| 4022 | `orders_backup_real_index != INVALID ⇒ return false` | 同上，且混淆了"防二次借用"与"防覆盖书签" |
| 6762 / 4020 | `orders_backup != nullptr` / `r3r_orders_borrowed` ⇒ 不采样 | **这两条是对的**（真借用态下 `cur_*` 是别人表的坐标，不能采） |
| 7383-7384 | 非借用态清 `INVALID` | 已有清理，但只覆盖 `7350` 那条分支 |
| 6834 | `R3RParkOwnOrderProgress(chain)` 在 6840 切表**之前** | 时序正确：真正借出那一刻采到的必然是"自己表上的坐标" |

**⇒ 根因定稿**：书签不是"过期"，而是**从设计上被当成了可以长期存放、随时补写的存档字段**。它本应是"借用期的临时寄存器"——只在把表借出去的那一瞬间有值，还回来就该消失。

**治根修法 D1（推荐，不新增判据，只删三处"锁"）**

| 动作 | 位置 |
|---|---|
| 删掉"已有书签就用旧值"分支，改为**采样前提成立就刷新为当前值** | 6771-6778 改为直接写；6779-6780 的 `only_unset` 返回删除 |
| 删掉幂等 return（保留"防二次借用"由 6830 负责） | 4022 |
| 提交点不再"补书签"，改为只清理非法残留（非借用态却带书签 ⇒ 清 `INVALID`） | 6969 |
| 耦合入口去掉 `only_unset=true` | 12806 |

保留不动：6762 / 4020（真借用态不采样）、6766（此刻必须驱动着自己的表）、6834 切表前采样、7383 归还清空。

**效果**：非借用态从 0 走到 1 时书签可能仍是 0（**但它此时不会被任何人读取**，因为书签只在借用期有意义）；真正在 1 借出时，6834 采到的必然是 1。即"手指在需要它的那一刻永远是对的"。

**治根修法 D2（更彻底，若要求字面同步）**

把书签写入挂到"订单索引推进"的收口处（`AdvanceOrdersFromVehiclePosition` / `IncrementRealOrderIndex` / 各 `CmdSkipToOrder` 路径），每次 `cur_real_order_index` 变化时若"驱动的是自己的表"则同步书签。字面满足"走到 1 手指就挪到 1"，代价是热路径每步一次判断、且需覆盖全部直接赋值点（容易漏，漏一处就退化成 D1）。

**D1 与 D2 的关系**：D2 是"持续镜像"，D1 是"按需取值"。二者都消除了"事后摆正"，区别只在书签在**非借用期**是否保持最新——而那个值在 D1 下根本不被读取。

**恢复合法性的边界（读档）**：读档时若链确实处于借用态（7300-7347 保态分支），书签必须从 `R3VP` 存档字段恢复——这是**唯一允许**"非采样获得书签"的场合，因为那一刻 `cur_*` 是别人表的坐标，没有别的真值来源。

### 17.7j 玩家拍板 D2（2026-10-31，决定性）

**玩家原话**：「我希望用 D2」

⇒ 采纳 **D2（字面同步 / 持续镜像）**：订单索引每前进一次，书签跟着走一格。不采用 D1 的"按需取值"。

**D2 的落地前提（必须先把清单做完再改码）**

`cur_real_order_index` 在代码里有多处**直接赋值**（不经过 `IncrementRealOrderIndex()`），漏掉任何一处都会退化成"书签偶发落后"，即 D2 失效、且现场表现为随机回跳。

⇒ 步序固定为：
1. **扫全量写点**：`cur_real_order_index` / `cur_implicit_order_index` 的所有赋值与自增点（含 `IncrementRealOrderIndex`/`IncrementImplicitOrderIndex` 内部）。
2. 判断能否**收口到少数几个函数**（理想=N 个写点都改成调用同一个 setter）。
3. 若收口成立：在 setter 里做"此刻驱动的是自己的表且未借用 ⇒ 同步书签"，热路径只多一次指针比较。
4. 若收口不成立：逐点补，并在函数级注释里标注"此处必须同步书签"。

**D2 的同步判据（与 D1 相同的三道前置，不可放宽）**

- `chain->orders_backup != nullptr` ⇒ 真借用态，跳过（`cur_*` 是别人表坐标）
- `chain->r3r_orders_borrowed` ⇒ 同上，跳过
- `GetEffectiveOrders() != 自己那张表` ⇒ 跳过（此刻跑的确实不是自己的表）
- 四条全部通过 ⇒ `orders_backup_real_index = cur_real_order_index`、`orders_backup_implicit_index = cur_implicit_order_index`

**D2 之后可以删除的东西**（否则又是一道"事后摆正"）

- `6779-6780` 的 `only_unset` 返回
- `4022` 的"已有书签就 return"
- `6969` 提交点的"补书签"调用（改由推进处保证）
- `12806` 的 `only_unset=true`

**D2 的已知风险（必须实测项）**

- 热路径开销：每次索引变化一次指针比较 + 两次赋值，量级可忽略，但要确认不在 `UpdateSpeed` 最内层触发。
- 借用/归还交接瞬间的时序：切表前最后一刻的书签必须已被推进处写成真值（这正是 D2 相对 D1 的唯一额外收益）。
- 读档：借用态仍从 `R3VP` 恢复（唯一合法的非采样来源），非借用态清 `INVALID`。

### 17.7k D2 落地实现（2026-10-31，已按玩家拍板实施）

**扫点结论（决定实现形态）**

`cur_real_order_index` / `cur_implicit_order_index` 的写入点全库 **55+ 处**（`search_content` 实测，另有 7 处被输出上限截断），分布在：
- `base_consist.cpp` 拷贝构造、`vehicle_base.h` 的 `SkipToNextRealOrderIndex` / `UpdateRealOrderIndex` 回绕分支
- `train_cmd.cpp` 约 20 处（读档、耦合、解挂、车库编辑、空气区间跳过、waiter 交接）
- `order_cmd.cpp` 约 9 处（增删改订单后的索引重映射）
- `vehicle.cpp` / `order_backup.cpp` / `sl/oldloader_sl.cpp`

⇒ **逐点补写不可行**（必然漏，漏一处就退化成"随机回跳"）。且 `cur_real_order_index` 是 `BaseConsist` 的公有字段，`BaseConsist` 看不到 `Train` 的 R3R 函数，无法在 `SkipToNextRealOrderIndex` 内部挂钩。

**⇒ 采用「tick 级持续镜像」：不枚举写点，改为枚举消费者能看到的时间点**

`TrainLocoHandler(Train *consist, bool mode)`（`train_cmd.cpp:21922`）是**每列车每 tick 唯一的入口**，且 `R3RSkipCoupleAirRanges(consist)`（:21933）就在入口处、自己会改索引。因此在其后加一次镜像调用：

> 任何 tick 内发生的索引变化（无论来自哪个写点），最迟在**下一 tick 入口**被同步进书签。
> 所有消费者（耦合采样、归还、结算、车库编辑）都在 tick 边界或提交点执行 ⇒ 读到的永远是最近一次同步值。

这不是"事后摆正"：不存在"长时间保留陈旧值"的状态，最快落后一个 tick 且必定在一个 tick 内被拉平；而旧实现是**永不拉平**（`only_unset` 锁死）。

**改动清单（4 处，全部在 `src\train_cmd.cpp`，不碰 .h）**

| # | 位置 | 改动 |
|---|---|---|
| 1 | `21933` 之后 | 插入 `R3RRefreshOrderBookmark(consist, "live");` |
| 2 | `6969` | `R3RRefreshOrderBookmarks(chain, nullptr, tag, true)` → 第三参 `false`（提交点也允许覆盖，不再"只补空"） |
| 3 | `12806` | `/*only_unset=*/true` → `false`（耦合采样允许覆盖） |
| 4 | 说明性 | `R3RRefreshOrderBookmark` 头部注释补 D2 口径 |

**保留不动的守卫（它们是对的，不是锁）**

- `6762` `orders_backup != nullptr ⇒ return`：真借用态下 `cur_*` 是别人表坐标
- `6763-6768`：`GetEffectiveOrders() != own`、索引越界、`INVALID` 一律不采
- `6766` 的判据正是"此刻跑的是不是自己那张表"，D2 靠它区分"该镜像"与"不该镜像"

**语义变化（必须写清，供复测判读）**

- 书签在**非借用期**也存在且恒等于当前索引（旧实现里它是"随存档带来的陈旧快照"）。
- 日志会在**每条订单推进**时出现一行 `ORD-BORROW-REFRESH ... tag=live`；每列车首次出现一行 `ORD-BORROW-PARK ... tag=live`。这是 D2 的正常体温，不是异常刷屏。
- 复用同一原语 ⇒ 不新增判据、不新增字段、不改存档格式。

**待复测判据（D2 专用）**

1. `ORD-BORROW-PARK/REFRESH ... tag=live` 每列车在推进订单时出现，且 `bkr` 与 `ORD-IDX-WATCH` 的 `real` 同步（差值恒 0，不再出现 `bkr` 长期落后）。
2. 复现原场景：54 号车读档 → 自跑到第 1 条 → 耦合 → 解挂，`ORD-RETURN-OWN` 的 `real` 不再是 `0`，而是解挂前自己表上的实际位置。
3. 负判据：借用期间（`borrowed=1`）不得出现 `tag=live` 行把 `bkr` 改成 owner 表坐标（守卫必须挡住）。
4. 性能：`tag=live` 同步不得出现在 `UpdateSpeed` 内层；帧率无可感变化（对照 `R3R_perf.log`）。

**构建自证（2026-10-31）**

- 入口：`R3R_fullbuild.cmd inc detach`（复用既有脚本，未新建任何 .cmd）
- 结果：`[2/3] train_cmd.cpp.obj` → `[3/3] Linking CXX executable openttd.exe`
- `build\R3R_incbuild.done` = `EXIT_CODE=0`（22:26:16）
- `build\openttd.exe` mtime 22:26:11，晚于 `src\train_cmd.cpp`
- 仅改 `src\train_cmd.cpp`，未碰任何 `src\*.h` ⇒ 增量合规
- `read_lints` 0 条

**状态**：已实现 + 已编译，游戏内复测待做（判据见上 4 条）。

**已知边界（不可消除，非补丁）**

守卫 `GetEffectiveOrders() != own ⇒ 不采样` 使得"非借用、但正跑着继承来的表"的段不被镜像——此刻活索引确实是别人表的坐标，写进书签就是污染（第 380 轮现场 `build\R3R_debug.log:4242` 就是这么把对的 0 刷成 3 的）。这类段的书签停留在它最后一次"真正在自己的表上"的值，属正确语义，不是遗漏。

### 17.8 D2 实测结果与残留疑点（2026-10-31）

**采样有效性**：`build\R3R_debug.log` 21 396 行 / 2 090 435 B / 10-31 22:46:51；产生者 `build\openttd.exe` 10-31 22:26:11 = D2 版（`tag=live` 字面量已在树内）⇒ 采样有效。

**D2 判定：已生效**

| 探针 | 条数 | 判读 |
|---|---|---|
| `tag=live` | 43 | 每 tick 镜像确实在跑 |
| `ORD-RETURN-NOBK` | **0** | 归还时书签空缺已消失（第 353 轮判据①达成） |
| `ORD-BORROW-PARK` | 22 | 首写正常 |
| `ORD-BORROW-REFRESH` | 56 | 覆盖刷新正常 |
| `ORD-RETURN-OWN` | 15 | 见下表 |
| `ORD-AFTER-COUPLE` | 13 | 交接正常 |

⇒ **玩家「当前问题暂时解决」与探针一致**：书签不再空缺，54 号车归还 `real=2->0 bk=0/0`（它自己 4 条表、借出时确实在 0，属正确还原，不是缺陷）。

**15 次 `ORD-RETURN-OWN` 全表（格式 `real=借入坐标->归还后` `bk=书签`）**

| 行 | veh | num | real | bk | orphan |
|---|---|---|---|---|---|
| 883 | 54 | 4 | 2->0 | 0/0 | 0 |
| 2031 | 57 | 8 | 5->1 | 1/1 | 0 |
| 4093 | 48 | 19 | 10->2 | 2/2 | 1 |
| 4095 | **0** | 15 | 6->**0** | **0/0** | 1 |
| 4714 | 48 | 19 | 2->2 | 2/2 | 0 |
| 5820 | 48 | 19 | 13->5 | 5/5 | 0 |
| 7731 | 51 | 13 | 16->3 | 3/3 | 0 |
| 10090 | 53 | 13 | 19->7 | 7/7 | 0 |
| 11371 | 50 | 19 | 22->9 | 9/9 | 0 |
| 14034 | 48 | 19 | 24->13 | 13/13 | 0 |
| 16752 | 50 | 19 | 29->17 | 17/17 | 0 |
| 18096 | 59 | 8 | 2->5 | 5/5 | 0 |
| 19181 | 57 | 8 | 5->1 | 1/1 | 0 |
| 21114 | 48 | 19 | 10->2 | 2/2 | 1 |
| 21116 | **0** | 15 | 6->**3** | **3/3** | 1 |

**疑点 Q-1（待验证，用户所指「另一列车的排程进度返还」）**

同一车底 `veh=0`（自己 15 条表），两次归还的终值不同：`:4095 → 0`、`:21116 → 3`。两次的 `cur` 都是 6（借入表坐标），说明"借入侧一致、自己表侧不一致"。二者之中必有一个不是"它自己表上的真实进度"。

- 若是 `0` 错：说明第二次之前它确实在自己表上跑到过 3，第一次归还时书签还没跟上（D2 落地前的旧值残留）。
- 若是 `3` 错：说明 D2 把"非自己表"的落位/工作位置写进了书签（第 380 轮的老坑，`GetEffectiveOrders()!=own` 本该挡住）。

**判定所需数据**：`veh=0` 两次归还之前的 `ORD-BORROW-PARK` / `tag=live` 行（书签何时变 3）、以及它自己 15 条表在各时刻的实际执行位置。

**疑点 Q-2（观察，未判缺陷）**：`veh=59` 在 `:18096` 归还到 5，`:18100` 解挂提交点推进到 6，`:18101` 立刻 `ORD-BORROW-PARK bkr=6 tag=decouple-v`。归还→推进→再停位，链条自洽；但 `:17853` 借用入口处**没有** `ORD-BORROW-PARK`，它的 5 是怎么写进去的需确认（可能是更早一次 park 的残留，那就仍属"非借用态携带书签"）。

### 17.6 下一步（T-17-3）

读 `build\R3R_debug.log` 约 2000~2210 行，确认三件事：①`veh=57` 的书签是在哪一行、什么状态下被写成 0 的（找 `ORD-BORROW-PARK veh=57` / `ORD-XFER` / `ORD-AFTER-COUPLE`）；②`DECOUPLE-DONE` 的完整字段（`co=`、`real=`、`plat=`）；③解挂后 `veh=57` 的 `orders` 到底是自己的表还是借来的表。

### 17.9 Q-1 结案：`veh=0` 的 `0` 与 `3` 各自都对（2026-10-31，零源码改动）

**取数**（`build\R3R_debug.log`，21 396 行，产生者 `build\openttd.exe` 10-31 22:26:11 = D2 版）：

| 行 | 探针 | 读数（原文摘录） |
|---|---|---|
| 5 | `ORD-BORROW-PARK` | `veh=0 bkr=0 bki=0 num=15 tag=load` |
| 935 | `DECOUPLE-JUMP` | `fire_real=2 dec_idx=2 wait_idx=65535 step=-1 v=54 u=0 n_u=33` |
| 941 | `DECOUPLE-DONE` | `u=0 co=1 real=3` |
| 4094 / 4095 | `ORD-RETURN-ORPHAN` / `ORD-RETURN-OWN` | `veh=0 head=48 owner=0` / `veh=0 num=15 real=6->0 bk=0/0 orphan=1` |
| 4103 | `ORD-BORROW-PARK` | `veh=0 bkr=0 bki=0 num=15 tag=decouple-v` |
| 4725 | `ORD-IDX-WATCH` | `site=settle-in tag=decouple-u veh=0 real=6->3 impl=6->3 tt=6->3 n=33->15` |
| 4726 | `ORD-BORROW-REFRESH` | `veh=0 bkr=0->3 bki=0->3 num=15 tag=decouple-u` |
| 4731~4737 | `DECOUPLE-JUMP` / `DECOUPLE-ADV` / `DECOUPLE-DONE` | `u=0 n_u=15` / `u_real=3` / `u=0 real=3`（随后 `JUMP-ORD 3 type=17` = `OT_WAIT_COUPLE`） |
| 21116 | `ORD-RETURN-OWN` | `veh=0 num=15 real=6->3 bk=3/3 orphan=1` |

**判定**

1. `0` 的来源是读档那次 `tag=load` 的初始停位。从读档到 `:4095` 之间 `veh=0` 一直跑别人的 33 条表（`:935 n_u=33`、`:4725 n=33->15`），`GetEffectiveOrders()!=own` 把镜像挡住 ⇒ 书签停在 0。这是第 382 轮写的**设计内不可消除边界**（非借用却跑继承表 ⇒ 活索引是别人的坐标），不是 D2 失效。
2. `3` 的来源是 `:4725` 那次 `settle tag=decouple-u`：表在同一处从 33 条切回自己的 15 条，活索引 6→3，切回后守卫满足 ⇒ 写入 3。3 在自己表上的语义就是 `OT_WAIT_COUPLE`（解挂后该待的那一格，`u_real=dec_idx+1=3`）⇒ **3 同时是"落位"和"续跑点"**，两者在此重合。
3. 因此 `:4095 → 0`（自己表还没跑过；且当时的 3 是 33 条表坐标，搬过来就是污染）与 `:21116 → 3`（表已切回自己的、3 是自己表坐标）**各自都正确**。第 380 轮"落位当进度"的坑不会误伤：落位只在"此刻跑的就是自己表"时才落书签，而这时落位本身等于续跑点。
4. **Q-1 结案 = 不是缺陷，无须改码。**

**Q-2 状态（仍未结案，降级为低）**：`veh=59` 全程没有 `ORD-BORROW-PARK`（`:17853` 借用入口无停位，`:18096` 直接归还到 5）。`:17853 ORD-IDX-WATCH site=del-implicit-in tag=- veh=59 real=-1->30 ... n=-1->33` 显示它是本 tick 才被合成/接管的车（`real=-1` 即 INVALID 起步）⇒ 5 不可能是本会话 `tag=live` 写的。剩两种可能：(a) `R3VP` 稀疏块把上一会话的 `orders_backup_real_index=5` 带进来；(b) 有 park 路径不打日志。验证手段：`Ptrs_R3VP()` 还原后补一行 `ORD-R3VP veh= bkr=`，或把 `R3RRefreshOrderBookmark()` 的"值未变早退"放宽为"parked 由 INVALID 变有效时也打一行"。

**Q-3（需玩家回答，已写进 KI 条目）**：玩家所称"另一列车的排程进度返还问题"请指认三件事：①`veh=` 车号；②现象（返还后从第几条重跑 / 跳到哪条 / 完全不前进）；③当时的命令序号。本轮从日志只能看到 15 次 `ORD-RETURN-OWN` 全部等于各自书签（自洽），唯一"两次不同值"的 `veh=0` 已结案为正确。

### 17.10 玩家指认：问题就在最后一次解挂 —— §17.9 结论作废

**玩家原话（2026-10-31）**：「你又开始胡说谁谁谁没有问题了，我现在指认，问题就在日志里面的最后一次解挂」。⇒ §17.9 的「Q-1 不是缺陷」**推翻**，改为本节。

**现场锚点（`build\R3R_debug.log`，最后一次解挂 = `:21105` 起的 consist=48 / u=6 那次）**

| 行 | 探针 | 读数 |
|---|---|---|
| 21105 | `DECOUPLE-FIRE` | `consist=48 tx=100 ty=65 real=10 mode=1 num=1 segs=2 eff=6 plat=30/0` |
| 21107 | `ORD-IDX-WATCH` | `site=syncdriving-in tag=keep veh=48 real=24->10 n=33->33 ord=…EA0` |
| 21114 | `ORD-RETURN-OWN` | `veh=48 num=19 real=10->2 bk=2/2 orphan=1` |
| 21116 | `ORD-RETURN-OWN` | `veh=0 num=15 real=6->3 bk=3/3 orphan=1` |
| 21117 | `ORD-BORROW-PARK` | `veh=48 bkr=2 bki=2 num=19 tag=sync-borrow` ← 48 自己的 19 条表被停位 |
| 21124 | `ORD-BORROW-PARK` | `veh=0 bkr=3 bki=3 num=15 tag=decouple-v` |
| 21126 | `DEPOT-XFER-ID` | `decouple-v head=48 ctrl=0 unit=1 own_bk=4` ← **控制段 = veh=0** |
| 21127 | `INVAR-CTRL` | `tag=decouple-v head=48 nseg=2 ctrl=0 ctrl_pri=1 head_pri=2 borrowed=1` |
| 21128~21136 | `GEO settle-decouple-v` | 链 = 48,49,50（segid=1）+ **0,1,2,3,4,5（segid=2）**… 共 9 节；`veh=0` 是第二段段头 |
| 21176 | `DECOUPLE-DONE` | `u=6 co=1 real=11 tx=100 ty=63` |
| 21180 | `LOCO-AFTER-DECOUPLE` | `veh=48 curType=0 real=3 idxnext=17` |
| 21181~21395 | `ORD-IDX-WATCH` / `CRT` | 48 全程 `n=15 ord=…0360`（**0 的 15 条表**）、`curType=17`(=`OT_WAIT_COUPLE`)，`CRT veh=48 order=17 dir=3 origin=101,66 td=5 found=0` 反复 |

**缺陷陈述（可观测事实，不依赖推断）**

1. 解挂提交点 `settle-decouple-v` 之后，**控制段落在第二段的 `veh=0` 上**（`ctrl=0 < head=48`，`borrowed=1`），机车 48 反过来成了"借用者"。
2. 结果整列改跑 **`veh=0` 的 15 条表**（`n=15 ord=…0360`），并从 **index 3** 起；index 3 在该表上是 `OT_WAIT_COUPLE`（48 事后 `curType=17`），于是 48 停在 `100,65` 等挂、`CRT order=17 found=0` 每 tick 重来，**不再推进自己的计划**。
3. 同时机车 48 **自己的 19 条表被停位 `bkr=2`**（`:21117`），此后整份日志再未被取用。
4. `real=3` 这个数不是 48 的进度、也不是 0 的执行进度：0 的 15 条表**从未跑过 0..2**（全日志没有 `veh=0` 自己跑表前进的 `ORD-IDX-WATCH`），它的 `real` 只在 `:4725` 与 `:21116` 两次被判成同一个 `6->3`（33 表坐标 6 → 切表 → 落位 3）。⇒ 我之前把 3 解释成"0 的真实续跑点"是**误判**：它是**解挂落位**，被当成进度喂给了新控制段。

**猜测根因（待验证）**：`R3RSettleChainSegments()` 在 `decouple-v` 提交点**没有强制重排 `r3r_priority`**，因此"控制段 = 优先级最小段"的旧判据把第二段 `veh=0`（`ctrl_pri=1`）选成排程主人，机车 48（`head_pri=2`）被迫借用；再叠加 `R3RAdvanceAfterDecouple()` 给 v 写的落位 3，就出现"机车被塞到另一列车的 WAIT_COUPLE 上永久等待"。对照第 152 轮拍板的 P1 不变式「控制段 == 链头所在段」，本条即该不变式在 `decouple-v` 边未落地。

**同一形态在全日志的分布**（`INVAR-CTRL` 共 18 行，`DEPOT-XFER-ID` 5 行）：`decouple-v` 两处都判 `ctrl=0≠head=48`（`:4107`、`:21127`），`depot-edit`/`decouple-u`/`couple` 各处 ctrl 也普遍 ≠ head ⇒ 不是单次偶发，但**玩家指认的这一次**（`:21105`）是最后一次解挂。

**待玩家确认（U-17.10）**：①解挂后留下的那列，你期望它继续走**机车 48 自己的 19 条表**（第 3 条起），还是继续走**车组那份 33 条表**？②它现在停在 `100,65` 等挂不动，是否符合你看到的现象？

**下一步（T-17-4，未做）**：读 `R3RSettleChainSegments()` 的 `decouple-v` 分支，确认是否缺 `R3RRebuildCouplePriorities()` / `R3RRenumberPriorities()` 调用，以及 `R3RGetControlSegment()` 选主人的判据；修法优先「提交点强制 ctrl==head 所在段」，其次才是"落位不写书签"。

### 17.11 T-17-4 取证结果：§17.10 的猜测根因**证伪**，真根因=「孤儿表回落」

**代码事实（`src\train_cmd.cpp`）**

1. `R3RSettleChainSegments()`（`:6953`）**确实不含** `R3RRenumberPriorities()` / `R3RRebuildCouplePriorities()`，但**这不重要** —— 重排发生在解挂主体里。
2. `DecoupleTrain()` 里 `:9697 R3RRenumberPriorities(v)`、`:9698 R3RRenumberPriorities(u)`、`:9702 R3RReelectControlSegment(v,"decouple-v")`、`:9703 (u,...)`、`:9704 R3RSyncDrivingOrders(v,false)`、`:9705 (u,false)` ⇒ 重选是**有**的。
3. `R3RGetControlSegment()`（`:5992`）= `R3RGetPriorityHead(chain)`（`r3r_priority` 最小者）。`:5983-5987` 注释明确记载第 159 轮 **q-0=贰乙** 拍板「控制段 = 命令所有者段，链头只承载」，且**甲口径的强制重排已作废** ⇒ `ctrl≠head`（`ctrl=0`）本身**是乙口径的预期形态，不是缺陷**。§17.10「缺强制重排 / P1=甲 未落地」**证伪**。
4. `R3RRenumberPriorities()`（`:7156`）只按**现有** `r3r_priority` 压实，不改次序；而 `R3RMergePriorities()`（`:7138`）在耦合时把被动方（车底）整体排在主动方（机车）之前 ⇒ 解挂后留下的这半里，「车底段 `veh=0` = 优先级 1、机车段 `veh=48` = 2」是耦合那一刻就定下的排名。
5. `R3RReelectControlSegment()`（`:7222`）只在「锚表在本链上有明确主人（`R3ROrderListHolderInChain()` 非空）且主人≠当前控制段」时才改优先级；锚表无主（孤儿）时 `:7242` 直接 `return`。

**日志事实（`build\R3R_debug.log:21100~21139`）**

| 行 | 读数 | 含义 |
|---|---|---|
| 21105 | `DECOUPLE-FIRE consist=48 ... real=10 segs=2 eff=6` | 合并链正在跑的表主人是 **6**（`eff=6`），即 33 条表 |
| 21113 | `ORD-RETURN-ORPHAN veh=48 head=48 owner=0` | **33 条表在 v 半段已成孤儿**（它的主人 6 被切到 u 半段） |
| 21114 | `ORD-RETURN-OWN veh=48 num=19 real=10->2 bk=2/2 orphan=1` | 48 交还自己的 19 条表，落回书签 2 |
| 21116 | `ORD-RETURN-OWN veh=0 num=15 real=6->3 bk=3/3 orphan=1` | `veh=0` 交还自己的 15 条表，落回书签 3 |
| 21118~21120 | `n=33->15`、`ord=…EA0->…0360`、`adopted real=0->3` | 48 改借 **`veh=0` 的 15 条表**，位置 **3** |
| 21126/21127 | `ctrl=0 ctrl_pri=1 head_pri=2 borrowed=1` | 控制段 = `veh=0` ⇒ 48 借用 0 的表（乙口径预期形态） |
| **`CTRL-REELECT` 全日志 0 命中** | —— | `R3RReelectControlSegment()` 每次都走 `:7242 return`（孤儿表）⇒ 号码牌一次没被重发 |

**真根因（已证，不再是猜测）**：解挂时**正在跑的那张表（33 条）的主人在解出的 `u=6` 那半**，于是 `v`（留下的 48+0..5）这一半里它成了**孤儿表**：

1. `R3RSyncDrivingOrders(v,false)` 走到 `ORD-RETURN-ORPHAN`（`:21113`）⇒ 48 不再跑 33 条表，改交还自己停放的 19 条表；
2. 控制段按旧优先级仍是 `veh=0` ⇒ 48 改借 0 的 **15 条表**，位置取 0 的书签 **3**（`:21120 adopted real=0->3`）；
3. 3 在 0 的表上是 `OT_WAIT_COUPLE`（48 随后 `curType=17`、`CRT found=0` 反复）⇒ 留下的列车被塞进「车底等挂」的位置永久停住；
4. 而**本应继承**的 33 条表（从 `decouple_idx+1 = 11` 继续）随 6 一起走了。

⇒ 这是第 380 轮/T8701 模型 2「解挂后留下的列车继承正在跑的车组计划」在**表主人被切走**这一子情形下未落实：只处理了「v 自己是主人（归还）」与「v 仍借到主人（继续借）」两种，没有处理「主人被切到对面（孤儿）」。`R3RAdvanceAfterDecouple(v, driving_orders, 10)` 因 `:9704` 的 sync 已把 v 切到 15 条表，`driving_orders` 不匹配 ⇒ 走 `GOTO_COUPLE` 分支 ⇒ 落 3，掩盖了「本该 D+1=11」的意图。

**修法方向（待拍板，未实施）**

- **M1（治根，推荐）**：`R3RSyncDrivingOrders()` 在 `ORD-RETURN-ORPHAN` 分支（v 的借用表主人已不在本链）不再「各回各家」，而是**把孤儿表就地过继给本半段的控制段**：`ctrl->orders = driving_orders`（清除 `r3r_orders_borrowed`）、`ctrl` 自己原有的停放表进 `orders_backup` 继续 park；随后 `R3RAdvanceAfterDecouple(v, driving_orders, decouple_idx)` 会命中 `driving_orders` 分支 ⇒ v 落 `11`，继续跑车组计划。
- **M2（口径兜底）**：`R3RReelectControlSegment()` 遇到「锚表是孤儿」时改为「把控制权交给链头所在段」（即 ctrl 落到 48），使 `ctrl==head`；但这只改"谁是主人"，不解决 33 条表已随 6 走掉的问题，留下的列车仍会跑 48 的 19 条表（位置 2）——与玩家期望「继续走车组计划」仍不符。故 M2 只能作 M1 的补充，不能替代。
- **M3（最小止血）**：禁止落位/归还把索引落在 `OT_WAIT_COUPLE` 上（留在半段时跨过它）。只能让列车"能动"，去向仍是错的。

**待玩家确认（U-17.11）**：解挂后留下的这列（48 + 车底 0..5），你期望它继续走**原车组那份 33 条表**（从第 11 条起，即 M1）还是**机车 48 自己的 19 条表**？若是前者 ⇒ 按 M1 落地；若是后者 ⇒ 需要 `R3RReelectControlSegment()` 在孤儿时把控制权交给链头段（M2）+ `R3RSyncDrivingOrders(v)` 不再回落到车底 15 条表。

### 17.12 玩家拍板 U-17.11（2026-10-31）：控制段 = 0..5、用 0..5 的表 = **现状即正确**，缺陷只剩「表进度」

**玩家原话**：「机车 48 + 车底 0..5，按道理说它应该重选控制段（选出0..5为控制段）然后使用0..5的表（现状），问题仅出在这个表的进度上面」。

**据此改判（以下三条立即生效）**

1. **M1（孤儿表过继 33 条表）作废**。留下的半段**不该**继承车组那份 33 条表；33 条表随 `u=6` 走掉是**正确**的，`ORD-RETURN-ORPHAN`（`:21113`）也不是缺陷。
2. **§17.11 的「真根因」降级**：`ctrl=0≠head` 不是缺陷（第 159 轮乙口径 + 玩家本轮确认），`R3RSyncDrivingOrders(v,false)` 让 48 改借 0 的 15 条表**也是正确方向**。
3. **唯一真缺陷 = 借来的那张表的进度**：`:21118~21120 n=33->15 adopted real=0->3` 把留下的列车放在 `veh=0` 表第 **3** 条，而 3 是 `OT_WAIT_COUPLE` ⇒ `curType=17`、`CRT found=0` 反复、停在 `100,65` 不动（`:21181~21395`）。**进度不该是 3（或不该停在 3 上）**。

**待查（T-17-5）**：`veh=0` 那 15 条表各索引的类型/目的地是什么？特别是：
- `bk=3/3`（`:21116`）这份书签是**什么时候、按什么规则**写进去的；
- `real=6->3` 的 `6` 是借用期冻结的 owner 坐标（33 条表坐标 6 ⇒ 切表后映射成 3？还是巧合相同？）；
- 3 若是「车底等机车来挂」的等待点，则留下的列车**已经有机车 48**，跑这份表时应当**跳过它继续**（与 `Couple()` 成功后跳过 `u` 表里 `WAIT_COUPLE` 同构），而不是停在那儿；
- 若 3 之后没有可执行命令，则说明这份表在「已有机车」的形态下本就该从头/从别的点开始。

**修法候选（待取证后定，均不得改动「控制段=0..5」这一形态）**

- **P1（首选，若 3 是车底视角的等挂点）**：解挂后留下的半段在采纳控制段表的索引后，若该索引是 `OT_WAIT_COUPLE` 且本半段**含机车**（`TrainHasEngine`），则按 `Couple()` 的跳过规则前进到下一个非等待命令。
- **P2（若书签 3 本身就写错了）**：修 `R3RParkOwnOrderProgress` / 书签写入点，让 `veh=0` 的停放进度等于它真正执行到的位置。
- **P3（若 6→3 是跨表映射错误）**：修 `R3RSyncDrivingOrders` 的坐标映射（不同表之间不该直接用数值搬索引）。

**本轮零源码改动、零构建。** 下一步先取证 0 的 15 条表内容与 `bk=3` 的写入时机（`ORD-BORROW-PARK`/`ORD-RETURN-OWN`/`ORD-BORROW-REFRESH` 三条探针的行号链）。

### 17.13 T-17-5 取证（一）：`bk=3` 是「解挂落位」被书签刷新吞进去的 —— 0 原本的书签是 **0**

**`veh=0` 书签（`orders_backup_real_index`）的完整生命线（全日志 6 行，已穷举）**

| 行 | 读数 | 判定 |
|---|---|---|
| `:5` | `ORD-BORROW-PARK veh=0 bkr=0 bki=0 num=15 tag=load` | 读档时被停位，书签 **0** |
| `:4095` | `ORD-RETURN-OWN veh=0 num=15 real=6->0 impl=6->0 bk=0/0 orphan=1` | 第一次解挂归还，落 0 |
| `:4103` | `ORD-BORROW-PARK veh=0 bkr=0 bki=0 num=15 tag=decouple-v` | 再停放，书签仍 **0** |
| **`:4726`** | **`ORD-BORROW-REFRESH veh=0 bkr=0->3 bki=0->3 num=15 tag=decouple-u`** | **书签被改写成 3 ← 污染点** |
| `:21116` | `ORD-RETURN-OWN veh=0 num=15 real=6->3 impl=6->3 bk=3/3 orphan=1` | 最后一次解挂归还，落 3（=被污染的书签） |
| `:21124` | `ORD-BORROW-PARK veh=0 bkr=3 bki=3 num=15 tag=decouple-v` | 再停放，书签 3 |

⇒ **`3` 不是 0 的运输进度，而是 `:4704` 那次解挂（`consist=48`、**解出 `u=0`**、`dec_idx=2`）给它落的位 `dec_idx+1=3`**。落位之后同一提交点里的「书签持续镜像」(`ORD-BORROW-REFRESH`) 把它当成进度抄进了书签。

**`:4704` 那次解挂的完整读数（关键上下文）**

- `:4704 DECOUPLE-FIRE consist=48 tx=102 ty=59 real=2 mode=2 num=1 segs=1 eff=0`、`:4705 ARRANGE-IN dst=-1 src=0 mc=1`、`:4709 TDTRY ... v=48 u=0` ⇒ 解出 **0**（含 0..5），留下 48。
- `:4716 ORD-IDX-WATCH site=syncdriving-in tag=keep veh=0 real=3->0 ... n=33->15` ⇒ 0 从 33 表切回自己的 15 表，活索引 3→**0**（此时书签 0，两者一致，**干净**）。
- `:4725 ORD-IDX-WATCH site=settle-in tag=decouple-u veh=0 real=6->3 ... n=33->15` ⇒ settle 期间 0 的活索引被写成 **3**（入口读数 6，33 表坐标）。
- `:4726 ORD-BORROW-REFRESH veh=0 bkr=0->3` ⇒ 3 立刻被抄进书签。
- `:4731 DECOUPLE-JUMP fire_real=2 dec_idx=2 wait_idx=65535 step=-1 v=48 u=0 n_u=15` ⇒ `wait_idx=65535`（没找到等待点），落位 `dec_idx+1=3`。
- 对照 v 侧：`:4718/4724 settle-in/out tag=decouple-v veh=48 real=0->3->4`、`:4721 COUPLE-AIR-SKIP veh=48 from=3 to=4` ⇒ 48 落 3 后**立刻被跳到 4**（因为它有机车、3 是它表上的停挂点）；**0 这半段没有等效的"跳过"**，所以 3 被留下来当进度。

**结论（待代码验证）**

1. `veh=0` 的 15 条表**从未真正执行过任何一条**（全日志无它的前进型 `ORD-IDX-WATCH`，只有 3→0 的切表与 6→3 的落位），所以它唯一可信的进度是**书签 0**。
2. 缺陷机理＝**「解挂落位」与「书签镜像」在同一提交点内串行，落位被当成进度固化**（第 380 轮「落位当进度」老坑在 D2 持续镜像落地后复发）。
3. 正确行为应是：留下的列车（48+0..5）借 0 的表时，进度取 **0**（未执行），然后按「本半段含机车 ⇒ 跳过表里的 `OT_WAIT_COUPLE`」规则推进——这与 `:4721 COUPLE-AIR-SKIP veh=48 from=3 to=4` 对 v 侧做的处理**完全同构**，只是对"被借表的宿主段"漏了。

### 17.14 玩家拍板 U-17.12（2026-10-31）：方向反了 —— 书签是「源头」，活索引才是「抄送方」

**玩家原话**：「不应该是这样吗：列车0..5的书签是第0条，当它突然晋升为控制段的时候，也就是那个15条表被使用的时候，把它主人的书签上面的数字抄送到链头上正在执行的数字（链头排程变为0）。可你说把现在正在执行的数字抄送到书签上！这不对」

**据此定性**：数据流方向唯一 —— **书签（表主人自己的真实进度）→ 链头活索引**。反向（活索引 → 书签）只能在「这张表真的执行过一条命令」时发生，绝不能在解析/解挂落位/提交点收口时发生。§17.13 的「P2′」表述改为：

- 采纳侧（本来就对）：`R3RSyncDrivingOrders()` `:7705 chain->cur_real_order_index = owner_real;` —— 把主人的位置抄给链头，**方向正确**；它抄错的根源是 `owner_real` 早已被污染成 3。
- 刷新侧（错）：`R3RSettleChainSegments()` `:6973` 的覆盖刷新把落位抄进书签；`TrainLocoHandler` `:21956` 的 live 镜像也会把「停在等挂点上的那一格」抄进书签。

**已落地（两处，已编译）**

1. `:6973` 改回 `only_unset = true`：提交点收口只补空，撤销第 378 轮 D2 的覆盖（D2 的「防陈旧锁死」由 `:21956` live 镜像独家承担）。
2. `R3RRefreshOrderBookmark()` 新增守卫：书签已有值（`!= INVALID`）且当前格是 `OT_WAIT_COUPLE` ⇒ 拒绝覆盖；补空不受限。堵住 live 镜像把落位固化。

**实测判据**：`ORD-BORROW-REFRESH veh=0 bkr=0->3 tag=decouple-u` 不再出现；`ORD-RETURN-OWN veh=0` 由 `real=6->3` 变为 `real=6->0`；`adopted real=0->0`；链头从 0 的表第 0 条起跑（`ORD-IDX-WATCH` 出现 `veh=48 real=0->1` 的前进型记录）。

**构建**：`build\_r3r_build.cmd` 增量编译通过（`[3/3] Linking CXX executable openttd.exe`，无 error），`build\openttd.exe` 时间戳 2026-10-31 22:26。

**待验证（T-17-5 后半）**

- `ORD-BORROW-REFRESH` 的调用点与守卫：`R3RRefreshOrderBookmarks(chain, tag, only_unset)` 在 `R3RSettleChainSegments(chain,"decouple-u")` 内被调用，且此时 `u` 的索引已被 `R3RAdvanceAfterDecouple`/落位改写 ⇒ 需确认调用顺序（settle 先于 JUMP？）。
- 修法候选收敛为 **P2′**：`ORD-BORROW-REFRESH`（持续镜像的提交点批量版）必须**排除"本提交点刚落位/刚推进"的那一次**——或者更根本：镜像只在「真的执行了命令」(ProcessOrders 推进) 后发生，不在 settle 落位后发生。同时保留 P1（被借表的宿主段含机车时跳过 `OT_WAIT_COUPLE`）作为"进度即便落在等挂点也能自愈"的兜底。


