# 若现在删除预留账本会发生什么（第 303 轮，2026-10-25）

**性质**：删除影响面推演。零源码改动、零构建。承接 `R3R_resv_ledger_disposable_round302_memo.md`。

## 零、玩家问题原话

> 「如果现在把账本系统删除了，会发生什么」

## 一、先界定"账本"到底是哪块代码（实测）

全仓引用 `_r3r_consist_resv` / `R3RResvRec` / `R3RReconcileConsistReservation` / `R3RGcConsistReservations` / `R3RIsReservationHolder` 的位置**只有 train_cmd.cpp 一处簇**（:17903–18740）：

| 组件 | 位置 | 作用 | 删掉的后果 |
|---|---|---|---|
| `struct R3RResvRec` / `_r3r_consist_resv` | :17916 / :17921 | `unordered_map<index, vector<{tile,track}>>` | 失去"跨格知道自己订过哪些位" |
| `R3RIsReservationHolder` | :17933 | 谁能持有预留（car-only / WAIT_COUPLE 且 primary） | GC 判据消失 |
| `R3RReconcileConsistReservation` | :18576 | 每 tick 增量对账：fresh 里没有的旧位→释放 | **只加不减**（退化为 KI-203 之前） |
| `R3RGcConsistReservations` | :18605（每 256 tick，:18720） | 持有者已消失则整条释放 | 无人回收 |
| `Train::ClearReservationUnderConsist` | :18724 | 按账本**跨 tile** 释放 | 退化为"只清调用那一刻脚下那几格" |
| `R3RReleaseReservationSafe` | pbs.cpp:848（`pbs.h:108`） | 实际释放动作，带自证（格型/track/位仍在） | **不在账本内，可保留** |

结论：账本是一个**薄封装层**，不是机制主干。删它不需要碰 pbs 的释放器、不需要碰审计、不需要碰存档（NOSAVE，无版本迁移）。

## 二、删了**不会**发生什么（先消除恐惧）

1. **不会崩、不会断言**：账本不碰地图写入，`R3RReleaseReservationSafe` 的四道自证仍在（`rail_map.h:168` 那类断言不会因删账本而回来）。
2. **不会读档报错 / 不需要版本迁移**：账本 NOSAVE，删掉等于"读档后本来也没有的东西不再假装有"。
3. **不会 desync**：账本从不进命令流，各客户端本来就不一致。
4. **不会误伤别人的位**：账本删掉后没有任何"按陈旧记录改图"的可能，误伤面反而收窄。

## 三、删了**会**发生什么（按严重度）

**① 车"被搬走而不是开走"的旧位重新变成永久残留（高，即 KI-203 复现）**
账本唯一的实战价值是 `ReconcileConsistReservation(...,"moved")`（:18719）：车节 tile 变了，把"上一 tick 记下、这一 tick 不在 fresh 里"的格释放掉。删掉后 `ReserveTrackUnderConsist` 回到"只加不减"——这正是 KI-203 的原始形态（第 110 轮实测：逻辑翻转/拼接/车库拖走之后旧格预留"再没人走到"）。

**② `ClearReservationUnderConsist` 退化（中高）**
唯一调用点是解挂 :10033。删账本后它只能清"当下脚踩的几格"，而解挂瞬间链已重排 ⇒ 解挂侧的泄漏会回升。

**③ 兜底从"秒级"变成"分钟级或人工"（中）**
剩下的兜底各有延迟：`R3RReservationAudit`（:18145）= 每 2048 tick + 3 次确认（脏标记档 256 tick，仍需约 768 tick）；`r3r_resv_purge` 控制台命令（pbs.cpp:2749）= 人工。玩家肉眼可见"铁轨上有黑色预留、别的车过不去"的窗口会变长。

**④ 收窄但不归零**：不删的部分仍能兜住大部分
- `R3RReleaseChainReservations`（:12198）——**不依赖账本**，按"释放前权威归属"扫地图，5 个调用点：decouple :10025/:10026、couple :12521、couple-settled :12628、waiter :21187。覆盖所有编组提交点（含 plain rail + 站台/路点整格）。
- 原生释放（车离开格 / 路径作废 / 撞车 / 删车 / 拆轨）。
- `RESV-NOBOOK`（等待车底不订出站路线）。

⇒ 缺口只剩"**不经上述提交点**的搬运"：逻辑翻转/整链倒置（tile 不变但 direction 变）、车库拖拽的非提交路径、瞬移/克隆。

## 四、三种删法的取舍

| 方案 | 做法 | 结果 | 建议 |
|---|---|---|---|
| A 直接删 | 删掉 :17916–18740 那一簇 + 两个调用点 | 回到 KI-203 之前，泄漏靠审计/命令兜 | **不推荐**（等于用"可丢弃"换回"会漏"） |
| B 删账本 + 补齐 Flush | 删簇，同时把 `R3RReleaseChainReservations` 挂到翻转/倒置/车库编辑/瞬移全部搬车点（第 301 轮铁律 2） | 事务语义，无泄漏、无账本 | **推荐终态**，工作量集中在"找齐搬车点" |
| C 缩到命令内 WAL | 账本只在单个命令内有效，命令结束即清空 | 中间态，改动最小，仍需 Flush | 过渡可用 |

**B 的必要条件**：翻转/倒置必须先 Flush 再 Re-reserve（否则"tile 不变、track 变了"的情形既清不掉也订不上）。

## 五、复测判据（若走 B）

1. 逻辑翻转/整链倒置后，该链脚下各格预留数与链节数一致（不多不少）；
2. 车库拖拽/摘段/降段后不留无主位（`RESV-AUDIT-STRAY` 增量为 0）；
3. 解挂两半各自脚下的位都还在（不可过度释放）；
4. 负判据：另一列车合法 PBS 路径中间格不得被清（清别人=拆路）。

## 六、待拍板

- **C-7** 走 A / B / C？（建议 B，A 只在"想立刻把代码量砍半、接受回归"时选）
- **C-8** 若走 B，翻转/倒置是"Flush + 重订脚下"还是"逐格 Re-reserve"？（建议前者，简单且不误伤同格另一轨）

## 七、当初为什么引入账本（2026-10-25 回溯，附代码原文）

**一句话**：为了给"只加不减"的 `ReserveTrackUnderConsist()` 补一个**跨格/跨 tick 的销账点**——否则车一挪，旧位就再没人走到，预留位永久残留。

代码自陈（`src\train_cmd.cpp:17901-17915`，原文）：

> `ReserveTrackUnderConsist()` only ever *adds* track reservations, and the only matching release (`ClearReservationUnderConsist()`) walks the tiles the consist occupies *at the moment it is called*. Whenever the consist has moved in the meantime, or a single vehicle carries a stale tile for one tick (logical flip, splice, depot drag), the tile that was really reserved is never visited again and its reservation bits stay set forever: a black blob of reserved track in the middle of empty line, which the signalling system then treats as an obstacle. Player report 2026-09-25 -- "black reservation preview while the train's next order is a plain GOTO_STATION, followed it to tile 60,16" ... The table below records what we reserved per consist (chain head index), so we can release precisely those bits and never somebody else's.

对应的三个诱因（当时实测）：
1. **配对释放只认"当下脚下"**：`ClearReservationUnderConsist()` 按调用那一刻的车节 tile 遍历，链一旦重排（翻转/拼接/车库拖拽），旧格不在遍历集合里。
2. **订路是每 tick 重订的**：`ReserveTrackUnderConsist` 在平台等待门/tick 里反复调用，`fresh` 集合每 tick 不同，差异部分没人负责退回。
3. **当时没有 Flush 概念**：链编辑提交点（couple/decouple/depot-edit）尚未接 `R3RReleaseChainReservations` 之外的地图级清扫，尤其"逻辑翻转 tile 不变、track 变"这一类根本没有对应释放路径。

**关键限定（本条回溯的结论）**：账本引入时的目标是"**销账**"，不是"**户口**"。它要回答的唯一问题是"这条链订过哪些 `{tile,track}`"，不回答"这个位现在归谁"（后者由 `GetTrainForReservation` / `GetTrainForWholeTileReservation` 从地图+拓扑查）。这也解释了第 303 轮为什么判定它"薄"：一旦把 Flush 铺到全部搬车点（方案 B），"销账"这件事就由提交点直接完成，账本的历史使命即告结束。

**由此得到的验收口径（替代"感觉差不多就删"）**：账本能删的充要条件是——日志里 `R3RReconcileConsistReservation` 的 `why="moved"` 类释放计数在实测场景下长期为 0，且 `RESV-AUDIT-STRAY` 增量为 0。前者证"新泄漏不再产生"，后者证"没有别的路径在偷偷依赖它"。

## 八、【重要更正】"账本"是两套，不是一个（2026-10-25 玩家追问时核对出来）

前面第 303 轮把"账本"当成一套讲，**这是错的**。核对代码后确认是两代、并存、语义不同：

| | 第一代：consist 账本 | 第二代：位级归属账本 |
|---|---|---|
| 位置 | `src\train_cmd.cpp:17916-17921` `_r3r_consist_resv` | `src\pbs.cpp:155-177` `_r3r_resv_by_owner` + `_r3r_resv_owner_of_bit` |
| 引入 | 第 110 轮 KI-203（2026-09-25） | 第 245 轮 KI-369/KI-371（2026-10-14 前后） |
| key | 车 index（调用者/链头） | bit key = `(tile<<3)|slot`，slot 7 = 整格位（`:183-189`） |
| 回答什么 | **我订过哪些 `{tile,track}`**（销账用） | **这一位归谁**（归属用，带 `purposes` 位掩码 PATH/CONSIST，`:170-174`） |
| 是否存档 | **否**（纯内存，读档即空） | **是**，`R3RL` 稀疏块（`src\sl\couple_group_sl.cpp:411`，`CH_SPARSE_TABLE`；实现 `pbs.cpp:853-894`） |
| 谁依赖它 | 只有 `R3RReconcileConsistReservation`(:18576)/`R3RGcConsistReservations`(:18605)/`ClearReservationUnderConsist`(:18724) 三个函数 | **整个释放/审计/回收链路**：`R3RReleaseReservationLedgerChecked`(pbs.cpp:488)、`R3RResvLedgerOwner`(:431)、`R3RResvLedgerResync`、reap 路径 |

⇒ **上一轮"删账本不会 desync、无需迁移"的结论只对第一代成立**。第二代已在存档里、且是判断"这位是谁的"的唯一依据，删它等于删存档块 + 让两端重新靠 walk 猜归属 = REG-243-K 那条 desync 原路返回。**"删账本"这件事必须先确认删哪一套。**

## 九、第二代账本引入后暴露出的问题（全部是代码注释里自陈的，按轮次）

1. **KI-362（自己造出来的抖动）**：早期 `R3RResvLedgerForget()` 释放 owner 名下**所有**位，包括车脚下那几位；下一 tick `ReserveTrackUnderConsist()` 又订回来 ⇒ 每 tick 订/放整列车。修法=引入 `purposes` 位掩码 + Release law（pbs.cpp:122-133。
2. **REG-243-K（放错别人的位）**：早期 `R3RReleaseReservationSafe()` 只问地图"这有位吗"，从不问账本"这归谁" ⇒ 账本漂移时释放别人的位。修法=`R3RReleaseReservationLedgerChecked` 先查 `R3RResvLedgerOwner()`（pbs.cpp:488-494）。
3. **KI-363/364（留下无主 stray）**：释放时"位没放成（车站着不让放）却把行删了" ⇒ 位还在但没人认领 ⇒ 得靠 reap 去扫。修法=Mirror law（pbs.cpp:134-142）。
4. **账本必然漂移**：`MakeStation()` 重建 tile、`SetRailStationPlatformReservation()` 整台清位这类写入者**绕开 setter**，账本不知道 ⇒ 必须周期性 `R3RResvLedgerResync()` 拿地图校准（pbs.cpp:139-142、`:356-359` 的 `R3R_LEDGER_RESYNC_NOTES` 节奏）。**这条等于账本自己承认"我不能自证，得定期回头看地图"。**
5. **NOSAVE → desync → 只好进存档（第 245 轮）**：注释原文（pbs.cpp:856-861）——a client that loads a game with an empty ledger answers "no owner" for every bit on the map and falls back to the walk... Two clients then reap different bits and desync (REG-243-K)。**这是账本从"临时簿记"升级为"存档格式一部分"的那一刻，也是这次改动真正变大的地方。**
6. **存档带来的新约束**：`booked` 时间戳不能存档（读档 tick_counter 归零），R3RL 读回来的行 booked=0，需要 `age=0` 特判语义（pbs.cpp:159-166）；新增 `R3RL` 块、表头命名、稀疏表编码、旧档无块=空账本的兼容面（`:870-875`）。
7. **purpose 分类靠字符串名单**：`R3RResvLedgerPurpose()` 里硬写 `{"consist","clear-consist","waiter-platform"}`（pbs.cpp:241-244），新增 phase 忘了登记就误分类；作者自己写"misclassification is safe in one direction only"（`:233-239`）——即这是一个**只能靠人不忘**的隐式耦合。
8. **多轨格只记第一条轨**：under-book（pbs.cpp:117-120），账本天生不完整，只能保证"晚放"，不能保证"全知"。
9. **两套账本并行**：consist 账本按车 index、位级账本按 bit key，两套 key、两套生命周期（一个每 tick 对账、一个存档+resync），语义重叠却互不知情 ⇒ 复杂度不是加法。
10. **热路径开销**：`ReserveTrackUnderConsist()` 每次调用都建 `std::vector<R3RResvRec> r3r_fresh` 再与旧表逐项比对（train_cmd.cpp:18623、:18580-18588）；地图每个 setter 都要走 `R3RResvWatchLog`（含 `kind[0]=='C'` 字符串前缀判定，pbs.cpp:363-366）。

**归纳成一句**：账本最初只是"帮我记住我订过什么，好还回去"的便条；为了修便条自己的错，长出了 purpose 掩码、两条法则（Release/Mirror）、resync 校准、存档块和一套 phase 登记纪律。它已经不是便条，而是**第二真相源**——而第二真相源与地图不一致时的每一个分叉，都曾经是一个 bug（上面 1/2/3/4）。

## 十、由此浮出的第三条路：不是"删账本"，是"合并两套账本"（方案雏形，待验证）

既然第二代账本的正向表 `_r3r_resv_by_owner`（owner → `vector<{tile,track,purposes,booked}>`）**本身就是**"某车订了哪些位"，而 `purposes` 里已经有 `R3R_RESV_PURPOSE_CONSIST = 0x02`（pbs.cpp:172）——那么第一代 `_r3r_consist_resv` 记录的"车脚下那些格"，完完全全是第二代里 `purposes & CONSIST` 的**子集视图**。

⇒ 第一代账本不需要"删 + 补 Flush"，可以"**改为查第二代的视图**"：
- `R3RReconcileConsistReservation()` 不再维护自己的 map，而是取 `_r3r_resv_by_owner[chain_head]` 里 `purpose & CONSIST` 的行与 `fresh` 求差；
- 差值释放仍走 `R3RReleaseReservationSafe`（→`R3RReleaseReservationLedgerChecked`，不越权）；
- 收益：**少一套 key、少一套生命周期**；并且第一代的"销账便条"顺带获得存档支持（读档后不空，但那时 consist 位由 `ReserveTrackUnderConsist` 重建，影响有限）。

**待验证项（真要做才查）**：
- V-1 第一代 key 是"调用者 index"（`R3RResvActorID(this)` 语义），第二代 owner 是 `R3RResvLedgerOwnerOf()`（链头 index，pbs.cpp:191-200）——两者在多段链/车底上是否恒等，必须在 `ReserveTrackUnderConsist` 的调用上下文里核对一遍，否则求差会漏行。
- V-2 第一代只在"`ok || HasReservedTracks`"时记账（train_cmd.cpp:18667），第二代的行由地图 setter 记账；同一格可能出现"第一代有行、第二代无行"（来自旧档、或绕开 setter 的写入），需定义回退策略（保守做法：第二代无行时退回"按脚下清"，即今天的行为）。
- V-3 合并后 `R3RGcConsistReservations()` 的 "holder-gone" 语义由谁来跑（第二代有没有等价回收），不能两边都留着。

## 十一、R3R 到底绕开了原生 OTTD 的哪条预留销毁机制（2026-10-25 取证）

**先给原生销毁机制的三条腿（全部由"运动/位置变化"驱动）**

| 原生销毁入口 | 触发条件 | 代码锚点 |
|---|---|---|
| `ClearPathReservation(v, tile, trackdir, ...)` | 清单格及其沿 trackdir 的前方一段（plain rail / 站台整台 / 隧道桥） | `src\train_cmd.cpp:15003`，分支 `:15032`（隧道桥）、`:15060`（站台） |
| 同上，位置更新时逐节调用 | **"最后一节离开该格"**：`v->GetMovingNext() == nullptr && !(v->track & TRACK_BIT_WORMHOLE)` | `:19964`（注释原文 "Clear any track reservation when the last vehicle leaves the tile"） |
| `FreeTrainTrackReservation(v)` / 崩溃 | 到达/停稳放弃路径、车辆被毁 | `:18759`（`if (!Stuck) FreeTrainTrackReservation`）+ `:18760-18762` 逐节清 |

⇒ 原生**只订"车要往前走的那条路"**，所以它的销毁只需要回答一个问题："车离开这格了吗？"。车不动，就没有任何销毁事件。

**R3R 造出了"车不动却持有预留"这个原生不存在的状态，共四条路径：**

**A. 静止的等待车底被要求持预留（最直接的一条）**
`src\train_cmd.cpp:21185-21197`（平台等待者分支）：先 `R3RReleaseChainReservations(consist, "waiter")` 清、再 `consist->ReserveTrackUnderConsist()`（:21191）订**车脚下**，还要沿站台轴把整段订下去（:21195-21197 `for (t = consist->tile; IsCompatibleTrainStationTile(...); t -= delta) TryReserveRailTrack(t, track)`）。注释自陈动机：等待车底必须"occupy the track"才能被信号系统当障碍、挂车寻路才能找到它（`:10028-10032`）。
它不动 ⇒ `:19964` 永不触发；它不崩溃 ⇒ `:18761` 永不触发；它的路线预留甚至被 `RESV-NOBOOK` 门禁主动拒绝（`:17189-17200`）⇒ `FreeTrainTrackReservation` 无物可清。**三条腿全部落空。**

**B. 逻辑翻转：`tile` 不变、`track` 变**
`R3RReverseChainDirections` / `R3RFlipChainBySegments` 只改 `direction` 与各节 `track`，**不改 tile、不产生走格**。原生的销毁判据基于"离开某格"（`gp.new_tile != v->tile`），这里位置一格没动，而旧 `track` 上的预留位是**另一条轨的位**，任何"按当前 track 遍历"的清理都点不到它。这是原生世界里根本不存在的变化类型。

**C. 物理链序重排：车"搬家"但不"移动"**
couple / decouple / depot-drag 直接重链接（`SetNext`/`ArrangeTrains`），车节 tile 不变，但它的 `Next()/Previous()` 变了。`ClearReservationUnderConsist()`（`:18724-18742`）只按**当前这条链的成员**遍历（`:18739`），被搬去别的链的车节不在集合里；而新链也没有理由去清"它以前在旧链上订的位"。全文件 `ClearReservationUnderConsist()` 只有**一个**调用点（`:10033`，DECOUPLE 提交点），而 `ReserveTrackUnderConsist()` 有三个（`:10034`、`:18801`、`:21191`），其中 `:21191` 是**每 tick**。

**D. 订/清频率根本不对等**
订＝每 tick（等待门 `:21191`）；清＝事件级（解挂 `:10033`）。所以即便 C 不发生，光"A"就足够制造泄漏：每 tick 用一个**新的** `fresh` 集合覆盖旧的，差集无人负责退回。这正是 KI-203 注释里那句 "only ever *adds*" 的字面含义。

**结论（一句话）**：R3R 没有"覆盖"或"关掉"任何原生销毁函数，而是**造出了一类原生销毁机制永远等不到的预留——车不动、格不变、链却变了**。原生的销毁是"空间事件驱动"，这类预留只有"时间维度"才能发现（上一 tick 订过、这一 tick 不订了），所以第一代账本出现了：**它给这类预留伪造了一个"离开事件"**。

⇒ 这也解释了为什么"补 Flush"（方案 B）是账本的正当替代：Flush 把"伪造离开事件"从"每 tick 差集"改成"在链被改写的那一刻显式记账"，时机更准、不需要历史。

## 十二、【玩家拍板 2026-10-25】一代二代一起删；删之前先做版本快照

**玩家原话要点**："既然一切的开始就是这四点，我应该有能力设计出不靠账本绕开这个的东西，我认为我们可以执行账本有关的删除，一代二代一起删。但是，在执行账本的删除之前，我们要执行版本快照，或者说是标记稳定可回退版本。"

⇒ 决策口径（后续不许再反复）：
1. **目标态 = 两套账本全删**（`_r3r_consist_resv` 与 `_r3r_resv_by_owner`/`_r3r_resv_owner_of_bit`），不采用 §十 的"合并两套账本"路线（该节降级为备选，仅当"不靠账本"方案做不出来时回看）。
2. **删除的前提 = 先有一份可回退落点**：源码快照目录 + 能跑的 exe + git tag。快照必须在删任何一行之前完成并自证。
3. 删除的正确形态由玩家自行设计（针对 §十一 的 A/B/C/D 四点各自给出"不靠账本也能销毁"的机制）；AI 的职责是先把快照做扎实、再按玩家设计逐点落地。
4. **禁止"边删边试"**：删除动作只在快照 tag 存在且可 checkout 之后才开始。

**快照清单（2026-10-25 21:50 完成，实测值）**：
- [x] 快照目录：`build\r3r_pre_ledger_removal_20261025\`（`src\` 全树 1824 文件 / 111,716,087 B + `openttd.exe` + `README.txt`）
- [x] 与工作区一致：快照是"工作区实际状态"整树复制，且同内容已进入提交 `3b87101b00`
- [x] git tag：`r3r-pre-ledger-removal-2026-10-25` → `3b87101b00eaf37d0d483c6c425acda28f48da34`（= HEAD，已用 `git rev-list -n 1` 校验，不用 `^{commit}` 写法——本环境 shell 会吃掉 `^`）
- [x] exe：52,326,400 B / 2026-10-25 21:49:26 / SHA256 `4004720B807D02720DF89E91E3E08775F128A0D872697A4CFB3B299D7CF7E8BC`
- [x] 回退命令：`git checkout r3r-pre-ledger-removal-2026-10-25 -- src`（**不用** `reset --hard`）；纯目录回退 = `robocopy build\r3r_pre_ledger_removal_20261025\src src /E`
- [x] 陈旧 obj 判据：构建前发现 `train_cmd.cpp`(14:55:48) 新于 `exe`(13:51:48) ⇒ 已先跑 `_tmp_inc_build.cmd` 补齐（`[3/3] Linking`、`EXIT_CODE=0`、obj 21:47:36 → exe 21:49:26），快照里的 exe 与源码严格对应

**踩到的两个坑（下次直接避开）**：
1. `git tag -a <tag> -m '...'` 的内联消息在本环境被 shell 拆坏，导致 tag 打到了**上一次提交** `c41ca0f227` 而不是 HEAD。正确做法：消息写进文件用 `-F`。已 `git tag -d` 重建并校验。
2. `git rev-parse <tag>^{commit}` 里的 `^` 被吃掉，命令退化成乱码参数。校验 tag 落点一律用 `git rev-list -n 1 <tag>`。

**提交范围（本轮）**：`src/` 全部改动 + `R3R_KNOWN_ISSUES.md` + 全部 `R3R_*.md` 备忘 + `_tmp_inc_build.cmd`，共 91 个文件。**未入库**（沿用 09-29 先例）：`*.zip`、`*.log`、`*.bak`、`R3R_archive_*`、根目录探针 txt、`INCREMENTAL`、畸形名 ` R3R_release_build.cmd`。

## 十三、【玩家拍板 2026-10-25】第二代账本存档块处置 = **B**

**玩家原话**："第二代的处理方法，我选择 B"

⇒ 口径（同轮落盘，不得再反转）：
- `R3RL` 存档块按 **B** 处置：**不再写出、旧档读到即跳过**。不采用 A（只停写、保留读逻辑），不采用 C（读档先用它清孤儿）。
- 已知代价（玩家已知情并接受）：旧档里若残留孤儿位，失去"账本指认归属"这条线索。兜底 = 现有 `R3RReservationAudit()`（按 `GetTrainForReservation()` / `GetTrainForWholeTileReservation()` 判 owner，nullptr 即 stray，连 3 次 reap），**不依赖账本**，所以 B 不会留下永久无主位。

### B 的硬约束（2026-10-25 取证，动手前必须遵守）
**不能直接删掉 `src\sl\couple_group_sl.cpp:411` 那一行注册。**

```
src\sl\couple_group_sl.cpp:411
    { 'R3RL', Save_R3RL, Load_R3RL, nullptr, nullptr, CH_SPARSE_TABLE },   // R3R reservation ledger chunk
```
```
src\sl\saveload.cpp:3089-3092
    const ChunkHandler *ch = SlFindChunkHandler(id);
    if (ch == nullptr) {
        SlErrorCorruptFmt("Unknown chunk type: {}", ChunkIDDumper()(id));
    }
```
⇒ 删掉注册行后，**任何含 R3RL 的旧档在读档期直接报 `Unknown chunk type` 而读不进去**。即 B 的"读档跳过旧块"不是白送的，必须显式实现。两种形态：

- **B1 空壳 handler**：保留注册行；`Load_R3RL()` 改成只消费数据不落库（`CH_SPARSE_TABLE` 必须由 load proc 消费，`load_proc = nullptr` 不安全）；`Save_R3RL()` 不再被调用或写空表。缺点=留一段死代码。
- **B2 走可丢弃通道**：把 `R3RL` 加进 `SlXvIsChunkDiscardable(id)`（`saveload.cpp:3086` 与 `:3118` 两处已支持：命中则 `ch = nullptr` 并 `SlSkipArray()` 跳过），随后注册行与 `Save_R3RL`/`Load_R3RL` 全部删除 ⇒ 真正"彻底删除"。**倾向 B2**，待确认该函数能否容纳自定义 chunk id。

### 13.1 B2 可行性取证（2026-10-25，已确认可行）

**取证锚点**
- `src/sl/extended_ver_sl.cpp:532-540` `SlXvIsChunkDiscardable(uint32_t id)` 是纯白名单查表（遍历 `_sl_xv_discardable_chunk_ids`），**没有"只允许内置 id"的守卫** ⇒ 自定义 chunk id 可直接吃下。
- 该名单的 populate 途径只有三处：`extended_ver_sl.cpp:408/409`（Joker）、`:429/430`（ChillPP）、`:507/523`（SpringPP），以及 `:731-738` 的 SLXI "unknown feature" 丢弃列表。
- `src/sl/saveload.cpp:3086` 与 `:3118` 两处调用点**都已存在**（`SlLoadChunks` 与 `SlLoadCheckChunks`），命中即 `ch = nullptr`。
- `src/sl/saveload.cpp:2877-2891` `SlLoadCheckChunk(nullptr, id)` 实现：`ch == nullptr` ⇒ `Debug(sl,1,"Discarding chunk ...")` 后按 block mode 走 `SlSkipArray()`/跳过长度 ⇒ **确实只是跳过，不解引用 handler**。
- `R3RL` 全部引用面：仅 `src/sl/couple_group_sl.cpp:21-25`（前向声明）+ `:411`（注册行）；实现体 `Save_R3RL`/`Load_R3RL` 在 `src/pbs.cpp`。**不在任何 SLXI feature 名单里** ⇒ 删除注册行不会影响 SLXI 读写。

**结论：B2 落地只需一行特判，不改任何存档格式、不加 handler**
```cpp
bool SlXvIsChunkDiscardable(uint32_t id)
{
    /* R3R (第 303 轮): the reservation ledger chunk is gone; old savegames
     * still carry it, so discard it on load instead of failing on an
     * unregistered chunk type. */
    if (id == 'R3RL') return true;
    ...
}
```
⇒ 新档不再写出 `R3RL`（注册行删掉后 `SlSaveChunks()` 遍历 `ChunkHandlers()` 就再也看不到它），旧档读到 `R3RL` 走 discardable 通道静默跳过。**这是真正"彻底删除"**：`Save_R3RL`/`Load_R3RL` 两个函数体、注册行、前向声明全可删，只剩这一行 id 特判（不是账本代码，是存档兼容垫片）。

**B2 的两个提醒（落地时不要踩）**
1. `'R3RL'` 是四字符字面量，风格与 `couple_group_sl.cpp:411` 的 `{ 'R3RL', ... }` 一致；MSVC 会出多字符字面量警告但整棵树已有大量同款，不新增噪音。
2. 删除前向声明（`couple_group_sl.cpp:21-25`）时**不要连带删掉 `R3DS` 的那一组**（`:27-31`，那是解挂缝合/孤儿年龄块，本轮不动）。

### 13.2 【玩家 2026-10-25 拍板】第一代不必玩家拍板；执行顺序 = 先删账本，替代机制之后再说

**玩家原话**："第一代账本也要我拍板吗，你刚才一次性说出来嘛，然后那个不靠账本的替代机制，能否在删除账本之后再说"

⇒ 两条口径（同轮落盘，不得反转）：

1. **第一代 `_r3r_consist_resv` 的删除方式不需要玩家拍板**，由本 AI 直接定案并执行。理由（可验证）：它是**纯内存 NOSAVE**（`train_cmd.cpp:17921` `static std::unordered_map<uint32_t, std::vector<R3RResvRec>> _r3r_consist_resv;`），不进存档、无格式迁移、无 desync 面，删除不涉及任何玩家可感知的口径选择 —— 与第二代（进存档 `R3RL`，删法会决定旧档能不能读）性质完全不同，**只有第二代才需要玩家拍板，且已拍板为 B/B2**。
2. **执行顺序 = 先删账本（第一代 + 第二代），后谈 §十一 A/B/C/D 的"不靠账本"替代机制。** 玩家已知情接受由此产生的时间窗。

### 13.3 第一代删除范围（AI 定案，动手那轮逐点核对引用面）

**删净（不留死代码）**：`R3RResvRec` 结构、`_r3r_consist_resv`、`R3RIsReservationHolder()`(:17933)、`R3RReconcileConsistReservation()`(:18576)、`R3RGcConsistReservations()`(:18605)、`R3RReleaseReservationSafe()`（`pbs.h:108` 声明 / `pbs.cpp:848` 定义，调用点仅 :18586/:18611/:18735 三处，全属第一代路径）、`ReserveTrackUnderConsist()` 末尾的 reconcile+GC 两行(:18719/:18720)、`ClearReservationUnderConsist()` 里"先按记账跨 tile 释放"那一段(:18732-18737)。

**保留**：`ClearReservationUnderConsist()` 原有的"按当前车节 tile 清理"逻辑（那是上游语义，不是账本）。

**注**：删 `R3RReleaseReservationSafe` 要动 `pbs.h` ⇒ 按 KI-183 护栏必走全量（692 步）。若想避免全量，可保留该函数（它有独立的自证式安全校验，留着不吃亏），但**不留调用者**即为死代码 —— 动手时按"能一次全量就一次全量"的原则处理，不为了省 16 分钟留半截。

**风险明示（不是拍板项，是必须记录的代价）**：删完第一代，KI-203 类"只加不减"的残留预留会立刻**重新出现**（账本是当时唯一的跨格销账点）。这是玩家"先删、替代机制后议"这一顺序的必然结果，按第 303 轮 §十一定性属**已知情接受**。可观测判据：`R3RReservationAudit()` 的 `RESV-AUDIT-STRAY` / `RESV-REAP` 计数上升、黑色预留块重回空线 —— **属预期回归，不是新 bug，不要误登记为新 KI**。

### 13.4 执行计划（玩家 2026-10-25「开始删除吧」后定案，分两步、每步可编译）

**第 1 步：删第二代（`R3RL` + `_r3r_resv_by_owner`/`_r3r_resv_owner_of_bit`）**
- `src/sl/extended_ver_sl.cpp` 的 `SlXvIsChunkDiscardable()` 加 `if (id == 'R3RL') return true;`
- `src/sl/couple_group_sl.cpp` 删 `:24-25` 前向声明 + `:411` 注册行（**不动 `:27-31` 的 `R3DS`**）
- `src/pbs.cpp` 删账本全套：`R3RResvLedgerNow`/`R3RResvLedgerRec`/`R3RResvPurpose`/两张 map/`R3RResvBitKey`/`R3RResvLedgerActorHead`/`DropRow`/`DropBit`/`Purpose`/`ForgetMask`/`Resync`/`Note`/`Owner`/`ForEachBit`/`ReportRelease`/`ReportUnowned`/`R3RReleaseReservationLedgerChecked`/`Save_R3RL`/`Load_R3RL`；**保留** `R3RFollowReservationToOwner()`、`GetTrainForWholeTileReservation()`、`R3RResvWatchLog()` 的探针日志（只把其中对 `R3RResvLedgerNote()` 的调用删掉 ⇒ 六个 map setter（`rail_map.h:252/315`、`station_map.h:599`、`road_map.h:494`、`bridge_map.h:419`、`tunnel_map.h:92`）**一行都不用改**）
- `src/pbs.h` 删 `:95-108` 与 `:110-126`（`R3R_RESV_NO_OWNER`、Note、Owner、Forget、`R3RReleaseReservationSafe`、BitVisitor、ForEachBit）
- `src/vehicle.cpp:1251-1254` 删整块；`src/train_cmd.cpp` 的 `:9506/:9507/:11424/:12526/:15115` 删行；`:15236-15238` 的 `r3r_owned` 回退为恒 `true`（=账本引入前的原始行为，属"回退"不是新机制）
- **审计 roster 必须回退为扫地图**：`train_cmd.cpp:18220` 的 `R3RResvLedgerForEachBit(R3RResvRosterCollect, &sink)` 是第 255 轮为性能改成"走账本名册"的；账本删了就没有名册 ⇒ 恢复成账本引入前的**逐格扫地图**实现（`R3RReservationAudit()` 里既有的 scan 分支还在，见 :18463 注释）。**这是回退到旧实现，不是本轮发明的新替代机制**，口径上不违反玩家"替代机制之后再说"。

**第 2 步：删第一代**（§13.3 清单，纯 .cpp 增量）。

**验收**：`EXIT_CODE=0` + `[n/n] Linking` + exe mtime 晚于源码 + `read_lints` 干净 + exe 内 `R3RL`/`R3RResvLedgerForget`/`R3RResvLedgerOwner` 字面量消失。

### 仍未定（等玩家设计，不是本 AI 能代决的）
1. ~~第一代（`_r3r_consist_resv` 内存账本）的删除方式~~ ⇒ **已定案（§13.2/§13.3），无需玩家拍板**；
2. §十一 A/B/C/D 四点各自的"不靠账本"替代机制 —— **按玩家指示，账本删完再谈**。
⇒ B 只是**存档口径**，删代码主体仍是这两条。**在拿到 1/2 之前不动源码。**

---

## 十四、删除落地记录（2026-10-25，玩家「开始删除吧」后执行）

### 14.1 已删净（两代一次删完，不再分两步）

**第二代（存档账本 `R3RL` + 两张 map）**
- `src/pbs.cpp`：`R3RResvLedgerNow/Rec/Purpose`、`_r3r_resv_by_owner`、`_r3r_resv_owner_of_bit`、`R3RResvBitKey`、`R3RResvLedgerActorHead`、`DropRow/DropBit/Purpose/ForgetMask/Resync/Note/Owner/ForEachBit/ReportRelease/ReportUnowned`、`R3RTrainForLedgerBit`、`R3RResvLedgerForget`、`Save_R3RL`/`Load_R3RL`、`R3RResvLedgerSaveRec`/`_r3r_resv_ledger_desc` 全删；`R3RReleaseReservationLedgerChecked` → 改名 `R3RReleaseReservationChecked`（去掉"先问账本"段，保留站台/隧道桥/平交道/普通轨道四类防御式释放）。
- `src/pbs.h`：删 `R3R_RESV_NO_OWNER`、`R3RResvLedgerNote/Owner/Forget`、`R3RReleaseReservationSafe`、`R3RResvBitVisitor`、`R3RResvLedgerForEachBit`。
- `src/sl/couple_group_sl.cpp`：删 `Save_R3RL/Load_R3RL` 前向声明与 handler 注册（`R3DS` 原样保留）。
- `src/sl/extended_ver_sl.cpp`：`SlXvIsChunkDiscardable()` 开头 `if (id == 'R3RL') return true;` ⇒ 旧档带 R3RL 时**丢弃**而非报未知区块。
- `src/vehicle.cpp`：析构路径的 `R3RResvLedgerForget(..., "destroy")` 整块删除。

**第一代（纯内存 `_r3r_consist_resv`）**
- `src/train_cmd.cpp`：`R3RResvRec`、`_r3r_consist_resv`、`R3R_CONSIST_RESV_GC_TICKS`、`R3RIsReservationHolder()`、`R3RResvCollectAllPathTiles()`（其唯一调用者 `bit_on_some_path` 随账本判据删除后已无用户）、`R3RReconcileConsistReservation()`、`R3RGcConsistReservations()` 全删；`ReserveTrackUnderConsist()` 里的 `r3r_fresh` 收集（3 处 push_back）+ 末尾 reconcile/GC 两行删除；`ClearReservationUnderConsist()` 里"先按记账跨 tile 释放"整段删除，**保留**其原有的"按当前车节 tile 清理"。
- `R3RReleaseReservationSafe()` 的三个调用点（`moved`/`holder-gone`/`cleared`）随上述函数一起消失，函数本身在 pbs.cpp/pbs.h 中的定义与声明也已不存在。

### 14.2 与 §13.4 的一处偏差（记明，等玩家一句话即可改回）

§13.4 第 1 步写"`:15236-15238` 的 `r3r_owned` 回退为恒 `true`"。实际落地改为**纯几何归属判断**：

```cpp
const Train *r3r_owner = GetTrainForReservation(tile, r3r_track);
r3r_owned = (r3r_owner == nullptr || r3r_owner == consist->First());
```

理由：恒 `true`（= 无条件 `ClearPathReservation`）会把 KI-479 原样带回来——"本链 path 在某格断掉，而该格同轨上是**别的车**的预留（咽喉区等待车脚下），被顺手清掉 ⇒ 后车认为空闲、订进来、纯几何相撞"。那是崩溃级；泄漏只是观感级且已有审计/命令兜底。`GetTrainForReservation()` 是引擎原生函数（非账本），不属于"新替代机制"。它"退一步"的地方是：本链自己的位若被几何回溯答成邻车，就漏放 ⇒ 残留上升，属 §13.3 已声明的预期回归。**玩家若要求严格按备忘改回恒 `true`，改一行即可。**

### 14.3 审计回退（按 §13.4 定案）

`R3RReservationAudit()` 的 roster 走账本名册改为**整图扫描**（四类位全收：普通轨道 `GetReservedTrackbits`、站台/路点 `HasStationReservation`、隧道桥 `GetTunnelBridgeReservationTrackBits`、平交道 `HasCrossingReservation`）。同时删掉两条依附账本的判据：
- 站台分支：`lowner` / `ledger_dead` / `why=dead`（只留 `owner == nullptr` ⇒ `why=walk`）；
- 普通轨道分支：`lowner` / `ledger_dead` / `ledger_absent`（`why=norow` 一并消失）。

⇒ 已知能力损失：**"记账者已死但几何仍答得出人"的孤儿位不再被抓**（KI-359/KI-363 B' 那条判据），以及"无账本行且不在任何 path 上"的位（KI-363 B'）。两者都登记在 KI-LEDGER-303F 的预期回归里，不新建 KI。

`R3RResvSweepSlice()` 的分片扫图保留，只把 `rowed` 判据去掉（现为 `!occupied && !walked`）。

`R3RReservationAudit()` 末尾的 `R3RResvLedgerForget(VehicleID::Invalid(), "resync")`（账本-地图镜像对齐）删除。

### 14.4 构建与验收

- 改动含 `src/pbs.h` ⇒ 按 KI-183 必经**全量**（护栏应报 `REMOVED 620 object file(s)`）。
- 构建启动 2026-10-25 22:15:33（`_tmp_inc_build.cmd`，未新建任何 .cmd）。**结果待回填**（见 §14.5）。
- 验收项：`build\R3R_incbuild.done` = `EXIT_CODE=0`、日志尾 `[n/n] Linking CXX executable openttd.exe`、`build\openttd.exe` mtime 晚于 22:15、`read_lints` 0 条、exe 内 `R3RL`/`R3RResvLedgerForget`/`R3RResvLedgerOwner`/`_r3r_consist_resv` 字面量消失、`RESV-AUDIT-STRAY`/`RESV-REAP` 仍在（审计还活着）。
- 回归口径：只要"进游戏跑一段不崩、预留仍能订能放"，即视为删除成功；幽灵预留回升**不算**失败。

### 14.5 构建结果（2026-10-25 22:39 核对，通过）

- 构建启动 22:15:33，完成 22:36:41；`build\R3R_incbuild.done` = `EXIT_CODE=0`。
- 因改 `src\pbs.h`，护栏判全量：日志尾 `[693/693] Linking CXX executable openttd.exe`；`error C` / `fatal error` / `FAILED:` / `build stopped` 计数 **0**。
- `build\openttd.exe` = 52,228,608 B @ 22:36:29，晚于最新源码 `src\train_cmd.cpp` @ 22:15:09。
- exe 字面量核对：`RESV-AUDIT-STRAY` yes、`RESV-REAP` yes（审计仍在工作）；`R3RResvLedgerForget` / `R3RResvLedgerOwner` **no**（账本符号已消失）。`R3RReleaseReservationChecked` 搜不到属正常——函数名不进二进制文本段（只有字符串字面量进），判定以编译通过为准。
- 结论：**第 1 步（第二代）+ 第 2 步（第一代）均已落地并编译通过**，回归游戏内实测未做（按 §14.4 口径，只要"能订能放不崩"即视为删除成功，幽灵预留回升不算失败）。

---

## 十五、删完之后要玩家设计的四个问题（按 §十一 A/B/C/D 编号，2026-10-25 22:40 落盘）

**共同背景**：原生销毁机制只有三条腿，全部由"运动/位置变化"驱动：①`ClearPathReservation()`（清单格 + 前方一段）；②位置更新时"最后一节离开该格"（`train_cmd.cpp:19964`）；③到站/停稳放弃路径、车辆被毁（`:18759` 起）。R3R 造出的是"车不动、格不变、链却变了"的预留 —— 三条腿都等不到它。账本的作用是**给这类预留伪造一个"离开事件"**；现在账本删了，需要玩家给每一类重新设计销毁时机。

**A-1　静止的等待车底（平台等待者）**
- 现状：`train_cmd.cpp` 平台等待者分支先 `R3RReleaseChainReservations(consist,"waiter")` 清，再 `ReserveTrackUnderConsist()` 订**脚下**，并沿站台轴把整段订下去；每 tick 都做。它不动 ⇒ 永不触发原生销毁。
- 要问：脚下这块 + 站台整段的预留，**该由谁在什么时候放下**？（候选：①挂车成功那一刻连带放下；②车底离开站台格时放下（需要位置事件以外的触发点）；③干脆不订站台整段，只订脚下；④保留现状的"每 tick 先清后订"并接受残留）

**B-1　逻辑翻转（tile 不变、track 变）**
- 现状：`R3RReverseChainDirections` / `R3RFlipChainBySegments` 只改 `direction` 与各节 `track`，不动 tile，旧 track 上的位任何"按当前 track 遍历"的清理都点不到。
- 要问：翻转前后要不要**显式按旧 track 释放一次**（翻转前先记下每节 `track`，翻完按旧值释放），还是认为翻转只发生在车已停稳、位本来就不该存在？

**C-1　物理链序重排（couple / decouple / depot 拖动）**
- 现状：`ClearReservationUnderConsist()` 只按**当前链成员**遍历，只有一个调用点（DECOUPLE 提交点）；被搬走的车节不在集合里，新链也没理由清它旧链上的位。
- 要问：链改写（挂车/解挂/车库拖动）那一刻，要不要统一做一次"按改动前的成员集合释放"（改动前快照成员 + 各自 track，改完按快照放）？还是接受这部分残留交给审计？

**D-1　订/清频率不对等**
- 现状：订＝每 tick；清＝事件级（历史上一代账本用"差集"补上）。删了账本后，"上一 tick 订过、这一 tick 不订了"的差额无人负责。
- 要问：这条要不要**独立修**（即在 `ReserveTrackUnderConsist()` 内部按"上一 tick 的集合 − 本 tick 的集合"释放），还是把 A/B/C 三条解决后**允许 D 继续泄漏**、只靠审计（2048 tick + 3 次确认）兜底？

**兜底现状（无需玩家设计，已具备）**：`R3RReservationAudit()` 每 2048 tick 全图扫一次，`owner==nullptr` 的位经 3 次确认后 `RESV-REAP` 释放；`R3RResvSweepSlice()` 每 tick 扫一小片做早期发现。两者都不依赖账本。

---

## 十六、【玩家拍板 2026-10-25 22:5x】A 项口径 + B/C/D 转为反问

**A-1 玩家定案（原话要点）**："关于静止的等待车底，我们让它仅向自己所在站台和自己脚下发出预留。"
⇒ 口径：等待车底只订 **①自己脚下（`ReserveTrackUnderConsist()` 的车节 tile）** + **②自己所在站台（整格/平台位）**；**不再**沿站台轴把整段订下去（取消 `train_cmd.cpp` 平台等待者分支里 `for (t = consist->tile; IsCompatibleTrainStationTile(...); t -= delta) TryReserveRailTrack(t, track)` 那条"沿轴整段"循环）。等价于"预留范围 = 车实际占据的位置"，不再向前/向侧扩张。
**待确认（实施前一句话）**：A-1 只回答"订哪些位"，没回答"什么时候放"。见下方 Q-A2。

**B/C/D 未答复，玩家改为反问（原话）**：
- B："列车逻辑翻转了会对它脚下的预留产生什么影响吗"
- C："R3R行为对于预留来说像是把原先的列车销毁了重新造吗，这会造成什么影响吗"
- D："这个会造成什么后果吗"
⇒ 这三条 AI 必须用代码取证回答（不是让玩家设计）。回答结论回填本节 §16.1~§16.3。

**Q-A2（仍需玩家一句话）**：等待车底脚下 + 站台的这部分预留，**什么时候放**？
- ①挂车成功那一刻连带放下（推荐：与 `R3RReleaseChainReservations(consist,"waiter")` 同点，语义最干净）
- ②每 tick 先清后订（现状形态，会短暂闪黑格）
- ③只在车底被销毁/解挂时放

### 16.1 B 的取证（逻辑翻转 vs 脚下预留）—— 结论：**脚下那几格基本不受影响，受影响的是"前方那段"**

已核实事实：
- 预留位存在地图 `_m[t].m2` bit8-11，**按 Track 记录**，不按车、不按 trackdir。
- 逻辑翻转的实体（`R3RFlipChainBySegments` / `R3RReverseChainDirections`，`train_cmd.cpp:10380-11300` 区段）**只改 `direction`、链序（Next/Previous）、★/⊗、假引擎与身份**；全文件 40 处 `->track =`（建车/离开车库/wormhole/移动）**没有一处在翻转区段** ⇒ **不改 `track`、不改 `tile`、不改地图位**。
- 释放侧坐标系：`ClearReservationUnderConsist()`（`train_cmd.cpp:18452`）逐节 `ClearPathReservation(u, u->tile, u->GetVehicleTrackdir(), ...)`；`GetVehicleTrackdir()`（`:22104`）对普通轨道返回 `TrackDirectionToTrackdir(FindFirstTrack(this->track), 行进方向)`，而 `ClearPathReservation()`（`:14968`）最终 `UnreserveRailTrack(tile, TrackdirToTrack(track_dir))`（`:15020`）。
- 碎片化关键点：`TrackdirToTrack(反向 trackdir) == 同一条 Track`（`ReverseTrackdir` 不换 Track，这正是 KI-346 的坑）。

⇒ 影响分三类：
1. **位不会消失、不会变主人**：地图一个字节都没动。
2. **单轨格（绝大多数正线）**：翻转后 `GetVehicleTrackdir()` 只是反向的 trackdir，`TrackdirToTrack()` 仍是同一条轨 ⇒ 逐节释放照旧命中 ⇒ **脚下预留可以正常放下**。
3. **双槽格（道岔 `TRACK_BIT_LEFT|RIGHT`、交叉 `TRACK_BIT_CROSS`、双轨位）**：`TrackDirectionToTrackdir(track_bits, 新方向)` 可能选出**另一条** Track ⇒ 释放打到隔壁轨上（要么空转、要么放掉别人的位）。
4. **真正会坏的是"前方那段"**：`FreeTrainTrackReservation(consist)`（`:18476` 是 Crash 里的用法；`Train::Crash` 之外由改轨/换端/失败回滚等调用）从 `GetMovingFront()` 出发沿预留 walk，**遇到第一格没有预留位就 return**。翻转把"运动前端"从链的这一头换到那一头 ⇒ walk 起点跑到队伍另一端，那边若没有预留位就立刻停 ⇒ **原来从旧前端铺出去的长路径整段收不回来**。这解释的正是旧备忘 §十一 B 记的"点不到"，机制不是"track 变了"，而是"**walker 的起点换了**"。
5. 附带：`consist->lookahead` 是 per-consist 的缓存视图，翻转/重链不动它，它继续描述旧路径，直到下次 `FreeTrainTrackReservation` 里 `lookahead.reset()`。

### 16.2 C 的取证（"销毁重造"类比）—— 结论：**不成立，而且比销毁重造更漏**

- 真·销毁有释放入口：`Train::Crash()`（`:18468-18479`）先 `FreeTrainTrackReservation()`，再逐节 `ClearPathReservation()`。
- 但**普通删除不走它**：`Vehicle::PreDestructor()`（`vehicle.cpp:1159` 一带）里**没有任何** `FreeTrainTrackReservation` / `ClearPathReservation`（`vehicle.cpp` 全文零命中），`CmdSellRailWagon()`（`train_cmd.cpp:4344`）也不调 `ClearReservationUnderConsist` / `R3RReleaseChainReservations`（全文件调用点只有 `:10013/:10014/:10021/:12491/:12593/:20903`，全在解挂/挂车/等待分支）⇒ **卖车本来就不清脚下预留**，跟 R3R 无关。
- R3R 的重链（couple/decouple/翻转/车库拖动）**不 delete、不 create、不改 tile、不改 track**，只改链指针与方向 ⇒ 它连 Crash 那条释放入口都不经过。所以对"预留"来说它不是"销毁重造"，而是"**车主换了身份证和朝向，地图上的位没人认领**"。
- 真正接近"销毁重造"的是 `R3RDestroyCarOnlyFormation()` / `DearticulateChainWithSnapshot()`（拆真假铰接组时真删/真建车对象），那才是对象层面的重造。

⇒ 结论：**不要指望"删了账本之后，销毁/重链会顺手把位清掉"**——原生只有"撞毁"这一条会清，其余全靠各提交点显式调用。

### 16.3 D 的取证（订/清频率不对等的后果）

- 现状：订 = 每 tick（`:20907 ReserveTrackUnderConsist()` + `:20911-20916` 两个沿站台轴的双向循环，每 tick 全订一遍）；清 = 事件级（解挂 `:10013/10014`、挂车 `:12491/12593`、等待首 tick `:20903`）+ 位置更新（最后一节离开该格，`:19964` 一带）。
- "上一 tick 订过、这一 tick 不订了"的差额**无人负责**（历史上一代账本用 reconcile 差集补上，现已删）⇒ 幽灵位。
- 后果按严重度：
  1. **挡信号/挡车（最痛）**：信号系统把 `HasReservedTracks()` 当占用 ⇒ 别的车订不进来 ⇒ `MarkTrainAsStuck`、死锁（KI-330 那类"机车卡住"常与此叠加）。
  2. **跨存档延续**：位是地图字段，随 `.sav` 保存 ⇒ 读档后照样在。
  3. **审计兜底变慢**：2048 tick × 3 次确认（约 6144 未暂停 tick），且现在审计是全图扫描（第 303 轮已回退）。
  4. **可能永久化**：账本删除后，"记账者已死"判据没了；只要 `GetTrainForReservation()` 还能把该位答给**任何一辆**车（哪怕只是路过/同格），审计就永远判"有主"、reap 不掉（KI-LEDGER-303F 已登记的预期回归）。
  5. 观感：黑色预留预览。

### 16.4 A 项落点的现状与歧义（**实施前必须问清**）

现状 `train_cmd.cpp:20896-20916`（等待车底分支，每 tick）：
```cpp
consist->ReserveTrackUnderConsist();                              // ①脚下各节
const Axis axis = GetRailStationAxis(consist->tile);
const TileIndexDiff delta = TileOffsByAxis(axis);
const Track track = AxisToTrack(axis);
for (TileIndex t = consist->tile; IsCompatibleTrainStationTile(t, consist->tile); t -= delta) TryReserveRailTrack(t, track); // ②整条到发线(往一端)
for (TileIndex t = consist->tile; IsCompatibleTrainStationTile(t, consist->tile); t += delta) TryReserveRailTrack(t, track); // ③整条到发线(往另一端)
```

玩家口径"仅向自己所在站台和自己脚下发出预留"有两种读法：
- **读法甲（≈现状）**："自己所在站台" = 整条到发线 ⇒ **代码几乎不用改**，A 项等于确认现状。
- **读法乙**："自己所在站台" = 车**实际压着的那几格站台** ⇒ 删掉 ②③ 两个循环，只留 ①（脚下各节）+ 车体覆盖到的站台格。
⇒ 需玩家一句话选甲/乙。

---

## 十七、【玩家拍板 2026-10-25 23:0x】A=甲（零改动）、B=认可现状、C/D=搁置

**玩家原话**
- A：「向脚下和自己所在整条到发线发出预留」
- B：「我想逻辑反转应该只发生在耦合场景吧，而耦合场景一般都是一车停稳一车预留向站台吧，那这样应该没有什么问题」
- C、D：「居然 A 落地几乎不要别的机制？那太好了，可以暂时搁置」

### 17.1 A 项结论 = 读法甲 = 现状，**零改动**

现状 `train_cmd.cpp:20896-20916` 与玩家口径逐字对应：①`consist->ReserveTrackUnderConsist()`（脚下各节）；②③沿站台轴双向到两端（`IsCompatibleTrainStationTile(t, consist->tile)` 为界、`Track = AxisToTrack(axis)` 单轨）。

**Q-A2（何时放）随之定案 = 保持现状**：等待首 tick `R3RReleaseChainReservations(consist,"waiter")` + `consist->lookahead.reset()` 一次（`_r3r_waiter_purged` 保证每等待 episode 只一次），之后每 tick 只加不减。
理由是"差集≈0"：
- 订的集合 = 车**真正占着**的格（脚下）+ 自己那条到发线；
- 车停着不动 ⇒ 本 tick 集合 ≈ 上 tick 集合 ⇒ 没有"上一 tick 订过、这一 tick 不订了"的差额；
- 唯一"多订"是站台两端车没走到的格，那**正是**玩家认可的"整条到发线"语义，不算泄漏。
⇒ A 项**不改一行代码**。

### 17.2 B 项结论 = 玩家判断成立，**无需新机制**

**实测调用点全集**（`R3RFlipChainBySegments\(|R3RReverseChainDirections\(`，全仓 11 命中）：
- `R3RReverseChainDirections` 定义 `:10401`；调用 `:10606`（`R3RFlipChainBySegments` 内部）、`:12555`（`Couple()` 里对 `merged_first` 补翻）。
- `R3RFlipChainBySegments` 定义 `:10983`；调用 `:11746`（候选 1 翻 u）、`:11781`（候选 2 翻 v）、`:11843`+`:11846`（候选 3 复合）。
⇒ **全部落在 `TryTrainCouple` / `Couple` 两个函数体内**，与玩家"逻辑反转只发生在耦合场景"一致；全仓无其它搬车点（force 换端路径已按 2026-09 拍板判弃用）。

**为什么耦合场景确实基本无事**（三条独立理由）：
1. 翻的是**停在站台上的等待车底 u**，它脚下是 `AxisToTrack(axis)` **单轨** ⇒ 反向 trackdir 的 `TrackdirToTrack()` 仍是同一条 Track ⇒ 逐节释放照旧命中（§16.1 第 2 点）；双槽格才可能打错，见下。
2. u **没有"前方路径预留"**（`RESV-NOBOOK` 门禁主动拒绝它订路）⇒ §16.1 第 4 点那个"walker 起点换了、长路径收不回"的坑在 u 身上**不存在**。
3. 有前方预留的是机车链 v（移动方），它走物理 SNAP / 物理翻，且提交点已有 `R3RReleaseChainReservations(v,"couple")`（`:12521`）与 `"couple-settled"`（`:12628`）兜住。
⇒ B 项**不做代码改动**。

**唯一残留（低，观察项，不修）**：若被翻的链恰站在**双槽格**（道岔 `TRACK_BIT_LEFT|RIGHT`、交叉 `TRACK_BIT_CROSS`）上，反向 trackdir 可能落到隔壁轨 ⇒ 释放打空 / 打到别人的位。登记 `KI-LEDGER-303G`。站台与正线都是单轨格，耦合点一般不会落在道岔中心。

### 17.3 C / D 项 = **已搁置**（玩家拍板）

- C（"R3R 对预留像不像销毁重造"）：§16.2 的结论（真销毁只有 Crash/破产会清，普通卖车原生就不清；重链连 Crash 都不走）**保留备用**，玩家判定搁置。
- D（订/清频率不对等）：§16.3 的五条后果保留；A 走甲后差集≈0 ⇒ 搁置成立。
- 复活条件（写清，免得将来重新取证）：若 `RESV-AUDIT-STRAY` 计数明显上升，按 §16.3 五条与 §16.2 的"只有 Crash 会清"直接排查，不必重做取证。

### 17.4 本轮零改动声明

- 本轮**无源码改动** ⇒ **不构建**；`build\openttd.exe` 仍是 2026-10-25 22:36:29 那版（两代账本已删净，52,228,608 B）。
- 交付物：本节 + `R3R_KNOWN_ISSUES.md` 的「第 303 轮续」小节。
- 结论一句话：**删账本之后不需要任何新机制；A/B/C/D 四点全部收敛为"现状即可 / 不适用 / 已搁置"。**
