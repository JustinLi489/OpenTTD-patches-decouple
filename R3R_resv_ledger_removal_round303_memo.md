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

**快照清单（待本轮回填实测值）**：
- [ ] 快照目录（相对路径 + openttd.exe + README.txt 回退步骤）
- [ ] 快照内改动文件数与工作区 `git status` 的改动数一致
- [ ] git tag（指向含全部账本代码的提交）
- [ ] 快照内 exe 的 mtime / 体积 / SHA256
- [ ] 回退命令（`git checkout <tag> -- src`，不用 reset --hard）
- [ ] 陈旧 obj 判据：最新 obj 晚于所有已改 src
