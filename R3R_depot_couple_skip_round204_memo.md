# R3R 第 204 轮：同库「前往挂接」+「等待挂接」两车，静默跳过 GOTO_COUPLE 直接执行下一条命令

- 日期：2026-10-03
- 现场：`build\R3R_debug.log`（716 行，最后写入 2026-10-03 01:05:02）
- 新增 KI：**KI-310**；本轮**未改任何源码**（先取证）

## 一、玩家报告（逐字）

> 两个列车处于同一个车库，分别处于前往该车库挂接和等待挂接的状态，但是处于不同的临时挂接分组，于是，主动挂接的列车没有和被挂的列车耦合，但也不是我期望的等待符合条件的列车，而是直接跳过到下一条命令。

（玩家同时澄清：上一轮"同分组却挂不上"是其**调度命令设置错误**，非本补丁缺陷。）

## 二、现场构成（车库存放 7 条链）

读档 dump 两次逐字一致（L29-210 与 L469-650）。车库 `1,15`（`depotIdx=0`）：

| 链头 | 节数 | 名称 | 说明 |
|---|---|---|---|
| 0 | 6 | T8701/2-南宁-凭祥 | 段1 |
| 6 | 21 | T8701/2-…（嘉林）1 | 段2 |
| 27 | 21 | T8701/2-…（嘉林）2 | |
| 48/51 | 3/3 | DF4D-3058 / D19Er-XXXX | 非等待 |
| **54** | 3 | HXN5B-XXXX | **主动挂接**：`i=0 type=16(GOTO_COUPLE) dest=0`、`i=1 type=6(GOTO_DEPOT) dest=19` |
| 57 | 3 | HXN5B-XXXX2 | `i=0 type=6`、`i=1 type=16` |

锚点（两次 dump 逐字相同）：

```640:641:d:\sourcecode of JGRPP\build\R3R_debug.log
R3RDUMP-CHAIN head=54 ow=0 isai=0 n=3 stopped=1 inDepot=1 spd=0 prio=1 borrow=0 ordCount=2 ordbk=0 curReal=0 curImpl=0 coType=0 realType=16 tile=1,15 tileOw=0 isDepot=1 depotIdx=0
R3RDUMP-ORDER head=54 i=0 type=16 dest=0 isDepot=0
```

## 三、第一次运行（L1-440）：耦合**成功**

1. 车库拖动：`DEPOT-BEFORE-HEAD sel=3 head=6 block=0 tail=5` ⇒ 块 `0..5` 拖到链 6 前 ⇒ 形成 `head=0 / n=27 / nseg=2 / ctrl=6 / borrowed=1`；链头 0 借控制段 6 的排程表 ⇒ L266 `DEPOT-ARR veh=0 real=0(17) curType=17`（等待车底）。
2. L271 `DEPOT-ARR veh=54 real=0(16) curType=16 … destDepot=1 tileEqDest=1`（已到本库）。
3. L272 **`CG-GATE compatible=1 coupler=54 target=0 cdecl=1 tdecl=1 cmask=0x1 tmask=0x1`**（分组闸门放行，**同组**）。
4. L273-291 `CPL-PAIR act=54 tgt=0 dist=0 actTile=1,15 tgtTile=1,15`；L293/294 正确拒绝另两条非等待链（`CPL-GATE reject=target-not-wait tgt=57 / tgt=27 aOrd=16 tOrd=0`）。
5. L305 **`COUPLE-OK loco=54 rear=26`** ⇒ 合并链 30 节；L337 `ORD-AFTER-COUPLE head=54 n=33 real=1 … owner=6` ⇒ 第一次完全成功。

## 四、第二次运行（L441-716）：**静默跳过挂接命令**（= 玩家所报现象）

1. L441-650 **重读同一存档**（dump 逐字相同，54 仍 `curReal=0 / realType=16`）。
2. L651-700 **同样的拖动**（唯一差别 `sel=0`）：`DEPOT-BEFORE-HEAD sel=0 head=6 block=0 tail=5` ⇒ 同样 `head=0 / n=27 / nseg=2 / ctrl=6 / borrowed=1`、`GEO-END settle-depot-edit head=0 n=27`。
3. L701-716 只跑两个 tick：
   - **L706 `DEPOT-ARR veh=54 … real=1(6) curType=6 … destTx=16 destTy=14 destDepot=0 tileEqDest=0`** ⇒ `cur_real_order_index` 已由 0 变 1，**跳过 `i=0` 的 `OT_GOTO_COUPLE`**，直接执行 `i=1` 的 `OT_GOTO_DEPOT`（→ depot 19 = `16,14`）。
   - L714 `CRT veh=54 order=6 dir=5 origin=1,15 td=8 found=1`、L715 `TRP veh=54 … real=1 type=6 dest=1808` ⇒ 真开走。
4. **第二次全程无任何耦合探针**：无 `CG-GATE`、无 `CPL-PAIR`、无 `CPL-GATE`、无 `COUPLE-OK`，也无 `ARTIC-SKIP-R3R` / `COUPLE-DEST-EMPTY` ⇒ 挂接命令是**无声消失**的。

## 五、与玩家描述的出入（待玩家确认）

- **吻合**：L706/714/715 与"没有耦合……而是直接跳过到下一条命令"逐字对应。
- **出入**：玩家说"处于**不同**的临时挂接分组"，但全日志唯一 `CG-GATE` 行（L272）是 `cmask=0x1 tmask=0x1`（**同组**），且第二次运行完全没走到分组闸门。⇒ 需确认：(a) 是否有更新的日志/新场景；(b) "不同分组"是实测还是推测。

## 六、根因候选（未定论）

`cur_real_order_index` 在 **dump(=0)** 与 **ProcessOrders(=1)** 之间被改写；其间只发生**车库拖动（L651-700）**。候选：

1. **车库编辑提交点误及非本次拖动的链**：`R3RSyncChainAfterDepotEdit()`（`src\train_cmd.cpp` 约 :5455）内 `R3RRenumberPriorities` / `R3RSyncDrivingOrders(chain,false)` / `R3RSettleChainSegments`，若套用到独立且非借用（`borrow=0`）的链 54 且 owner 判定出错，会把别人的索引写进 54。
2. **同进程内第二次读档的会话级静态残留**：已确认 `s_dbg_dump_sigs`（:16784）会造成"链头 0 第二次无 `DEPOT-ARR`"这类**日志缺失**；须区分"日志缺失"与"逻辑未执行"。
3. **`R3RSkipCoupleOrdersForRealArtic()`（:16408）静默路径**：它会把 `OT_GOTO_COUPLE` 索引推到下一条非 R3R 命令（:16446），**但两个分支都写 `ARTIC-SKIP-R3R`**；日志 0 条 ⇒ 暂不成立（除非探针被抑制）。

> 注意：`R3RSkipCoupleOrdersForRealArtic` 只跳**主动**命令；`OT_WAIT_COUPLE` 是被动订单，语义上**不应**被跳（第 180 轮 KI-274 已记录同一设计口径问题）。

## 七、下一步

1. 读 `R3RSyncChainAfterDepotEdit()`（约 :5960-6070）与 `CmdMoveRailVehicle()` 提交点，确认拖动是否/如何触及 54。
2. 读 `ProcessOrders` 的 `:16580-16740`，确认 `real=1` 是否在进入 ProcessOrders 前成立。
3. 向玩家索取"不同分组"的新日志（或确认本日志即现场）。

## 八、复测判据（定位后细化）

1. 同存档、同拖动重复运行 N 次，54 **每次**都必须尝试挂接（出现 `CG-GATE`/`CPL-PAIR`）或至少 `ARTIC-SKIP-R3R`，不得静默 `real=1`。
2. 54 的 `cur_real_order_index` 不得在无 `COUPLE-OK`/`ARTIC-SKIP-R3R`/`COUPLE-DEST-EMPTY` 的情况下自 0 变 1。
3. 分组不同时，主动车应**原地等待**（`COUPLE-DEST-EMPTY … waiting in place, order kept`），不得跳命令。

## 九、第 205 轮：探针补强（2026-10-03）

按玩家指示"直接添加探针"，不改任何行为逻辑，只铺观测网以定位第六节的三个候选根因。

### 9.1 哨兵函数

新增文件内静态哨兵（仅 `src\train_cmd.cpp`，未碰任何 `src\*.h`）：

```cpp
static void R3RWatchOrderIndex(const Train *t, const char *site, const char *tag = nullptr)
```

- 定义位置：`src\train_cmd.cpp:1898`（`R3RChainHasRealArticPart()` 之后、`DearticulateChainWithSnapshot` 前向声明之前）。
- 按 `t->index.base()` 维护**进程静态**快照表 `static std::unordered_map<uint32_t, R3RIdxSnap> seen;`。
- 快照字段：`real / impl / tt / ol(OrderList*) / count / artic / valid`。
- **边沿触发**：仅当任一字段相对上次快照变化（含"进程首次见到该车"）才写一行；连续多 tick 不变**不刷屏**。
- 输出格式：

```
ORD-IDX-WATCH site=%s tag=%s veh=%d real=%d->%d impl=%d->%d tt=%d->%d n=%d->%d artic=%d->%d ord=%p->%p front=%d spd=%d parked=%d
```

`-1->X` 前缀 = 进程首次见到该车。

- **纯只读**：不改字段、不调转移函数。
- 快照表**跨读档保留**（刻意）：使"同进程内第二次读档后索引不一致"（第六节候选②的会话级残留）以 `...->...` 形式直接暴露。

### 9.2 十个调用点（成对取样，缩小索引变更窗口）

| # | site | 行 | 位置 |
|---|---|---|---|
| 1 | `settle-in` | :5351 | `R3RSettleChainSegments()` 入口 |
| 2 | `settle-out` | :5433 | 同上末尾（`R3RLogSegmentTraits` 之后） |
| 3 | `ord-push` | :5472 | `R3RPushProgressToOwner()` 写回之前 |
| 4 | `ord-push-owner` | :5473 | 同上、写回 owner 之后 |
| 5 | `depot-edit-in` | :6097 | `R3RSyncChainAfterDepotEdit()` 入口 |
| 6 | `depot-edit-out` | :6223 | 同上末尾 |
| 7 | `loco-entry` | :16681 | TrainLocoHandler：`r3r_pending_depot_couple` 计算后、Stopped 闸门之前 |
| 8 | `post-skipgate` | :16726 | `R3RSkipCoupleOrdersForRealArtic()` 之后 |
| 9 | `post-depotblk` | :16816 | 车库挂车块 + `NormalizeTrainVehInDepot` 之后、DECOUPLE 之前 |
| 10 | `post-processorders` | :17103 | `ProcessOrders()` 之后 |

### 9.3 取数判读（对应第六节三个候选）

对 `ORD-IDX-WATCH veh=54` 逐行读取，`real=0->1` 那一行的 `site` 即"改索引的现场"：

- **候选①**（车库编辑提交点误及链 54）：命中区间 `depot-edit-in` → `depot-edit-out`。
- **候选③**（`R3RSkipCoupleOrdersForRealArtic` 静默路径）/ 引擎侧：命中区间 `loco-entry` → `post-skipgate` 或 `post-depotblk`。
- **候选②**（读档/静态残留）：第一个 tick 即 `-1->1`（进程首次见到就是 1），说明索引在读档后本就为 1，与拖动无关。

### 9.4 构建自证

- 复用既有 `_tmp_inc_build.cmd`（未新建任何 `.cmd`）；`GUARD: incremental is safe`。
- `[3/3] Linking CXX executable openttd.exe`；`build\R3R_incbuild.done=EXIT_CODE=0`；`read_lints` 0。
- `src\train_cmd.cpp` → `train_cmd.cpp.obj` → `build\openttd.exe` 时间戳单调。
- `findstr /C:"ORD-IDX-WATCH" build\openttd.exe` 命中（`EXE-HIT-ORD-IDX-WATCH`）。

### 9.5 复测步骤

1. 用新 exe 复跑同一存档、同一拖动（尽量复现第二次运行的 `sel=0` 路径）。
2. 抓 `ORD-IDX-WATCH` 全部行，按 `veh=54` 过滤，按行序排。
3. 找 `real=0->1` 首次出现的那一行的 `site`，对照 9.3 判定候选。

## 十、第 206 轮：按玩家澄清补探针（"54 没有被车库拖动过"）

### 10.1 玩家澄清与本轮策略

玩家明确：主诉列车 **54 并没有被车库拖动**。⇒ 第六节候选①（"拖动提交点误及链条 54"）不再是首要嫌疑；但"拖动命令执行期写坏了库内**第三条**链"仍未被排除——第 205 轮的 `depot-edit-in/out` 只观察**被传入的那条链**（拖动涉及的 src/dst），对同库第三条链是盲区。

因此本轮不预判方向，而是把观测网加密到两类此前无覆盖的窗口：

- **(a) tick 之外**（命令队列、读档末、其它子系统）：靠 `tick-in` / `tick-out` 成对取样切分。若 54 的 `real=0->1` 出现在"上一 tick 的 `tick-out`(=0) → 本 tick 的 `tick-in`(=1)"形态，即证明写者在 tick 之外；若 `tick-in`(=0) → 某站点(=1)，则写者在 handler 内且被站点直接点名。
- **(b) 此前完全无站点的 R3R 机制**：借用层 `R3RSyncDrivingOrders()`（专门把 owner 的冻结索引回灌给链头，KI-04/KI-215b 的源头）、真铰接订单闸门入口、库内两处自动分支、以及"跳命令"命令本体 `CmdSkipToOrder()`。

### 10.2 哨兵本身的两处修订（重要）

1. **按 (站点, 车号) 分桶**。原实现 `static std::unordered_map<uint32_t, R3RIdxSnap> seen;` 只按车号分桶 ⇒ 所有站点共用一份快照：A 站点打过一行后，B 站点看到相同的值就**静默不打**，于是"日志里没有 B 站点"无法区分"B 站点没被执行"与"B 站点执行了但值没变"。现在键改为 `(site 字面量地址 << 32) | veh`（`src\train_cmd.cpp:1921-1922` 一带），每个站点对每条链各自保留"首次行 + 各自的边沿行"。
2. **新增非 static 薄包装** `void R3RWatchOrderIndexSite(const Train*, const char*, const char*)`（`:1960`），供 `order_cmd.cpp` 用**手写声明**调用（`src\order_cmd.cpp:49`），从而本轮**不碰任何 `src\*.h`**，保持 KI-183 增量构建合规。

### 10.3 本轮新增站点（16 处调用，全部只读）

| site | 行 | 位置与用途 |
|---|---|---|
| `load-rebuild` | :5656 | `R3RRebuildCouplePriorities()` 入口（**只被读档调用**）⇒ 区分"读档时已被改"与"读档后（tick 之间）才被改" |
| `syncdriving-in` | :5847 | `R3RSyncDrivingOrders()` 入口，tag 记 `inherit`/`keep` |
| `syncdriving-out-owner` | :5852 | owner==chain（归还借用）出口 |
| `syncdriving-out-nolist` | :5893 | owner 无表可跑出口 |
| `syncdriving-out-samelist` | :5909 | 已在同一张表、只采纳进度的出口（**本身是写点**） |
| `syncdriving-out-adopted` | :5943 | 真的切换并采纳 owner 索引的出口（**本身是写点**） |
| `commitfail-skip-in/out` | :11967 / :11984 | 耦合提交连续失败后的**自动跳命令**路径（本文件第二条"静默推索引"路径） |
| `depotstay-in` | :12111 | `CheckTrainStayInDepot()` 入口（库内列车路径基线；**无此行即证明该链未走此路径**） |
| `skipgate-in` | :16541 | `R3RSkipCoupleOrdersForRealArtic()` **无条件**入口行（证明"是否进过闸门"，含非真铰接链） |
| `pre-processorders` | :17147 | `ProcessOrders()` 之前，与既有 `post-processorders`(:17160) 配对 |
| `tick-in` | :17570 | `Train::Tick()`：`current_order_time++` 之后、第一次 `TrainLocoHandler` 之前 |
| `tick-out-leftchain` | :17582 | 第二次 handler 之前已不再是链头（身份被迁走） |
| `tick-out` | :17589 | 第二次 `TrainLocoHandler` 之后（仅在链头仍存活时打点；返回值语义与原来逐字相同） |
| `svc-in-depot` | :17612 | `CheckIfTrainNeedsService()` 的库内自动检修分支 |
| `cmd-skip-in` / `cmd-skip-out` | order_cmd.cpp :1894 / :1923 | `CmdSkipToOrder()`（**"跳命令"命令本体**）执行体前后 |

### 10.4 判读口径（针对"没有被拖动"的形态）

对 `ORD-IDX-WATCH veh=54` 逐行读，找 `real=0->1` 那一行的 `site`：

- `tick-out`(=0) → `tick-in`(=1)：**写者在 tick 之外**（命令队列 / 读档末 / 其它子系统）。若是命令，`cmd-skip-in/out` 会直接点名；若两者都无行，则写者在没有站点的命令或子系统里，用 `load-rebuild` 是否已为 1 进一步区分"读档 vs 读档后"。
- `tick-in`(=0) → `syncdriving-*`：命中借用层回灌（KI-04 / KI-215b 同族）。
- `tick-in`(=0) → `loco-entry`(=0) → `skipgate-in` …：命中真铰接闸门，此时 `artic=` 字段可直接读出该链是否真铰接。
- 落在 `settle-*` / `depot-edit-*` 区间：命中 R3R 提交点（即第七节待办 1）。
- 首行即 `-1->1`：写者在"进程首次见到该车"之前（读档结束时已是 1），与拖动、命令**均无关**（第六节候选②）。

### 10.5 构建自证（2026-10-03 04:11）

- 复用既有 `_tmp_inc_build.cmd`（未新建任何 `.cmd`）；`GUARD: incremental is safe (no header/lang file is newer than the newest object)`。
- `[4/4] Linking CXX executable openttd.exe`；`build\R3R_incbuild.done=EXIT_CODE=0`；`read_lints` 两文件 0 条。
- `src\train_cmd.cpp`(04:00:24) → `train_cmd.cpp.obj`(04:02:32)；`src\order_cmd.cpp`(03:59:04) → `order_cmd.cpp.obj`(04:02:32)；`build\openttd.exe` 04:11:07（51 634 176 B）。
- 9 条新字面量在 exe 内全部命中：`load-rebuild` / `syncdriving-in` / `tick-out-leftchain` / `cmd-skip-in` / `depotstay-in` / `skipgate-in` / `commitfail-skip-out` / `svc-in-depot` / `pre-processorders`。

### 10.6 未确认项

- 玩家澄清后，"第二次运行 54 的 `real=0->1` 究竟由谁写"仍然**未定论**；本轮只加密观测网，未改任何行为。
- 仍待玩家用本 exe 复跑并交回 `ORD-IDX-WATCH` 全部行（按 `veh=54` 过滤）。
- 第八节复测判据 1-3 不变，另加判据 4：54 的 `real` 变化必须**能归属到某个站点**，不得出现"两个相邻站点之间没有中间站点却已变化"的黑箱窗；若出现，按该窗两端站点继续加密。

## 十一、第 207 轮：根因（玩家补充）＝上游 `InsertOrder()` 的"插入点 ≤ 当前索引则当前索引 +1"，不是静默跳过

### 11.1 玩家原话（2026-10-03）

> 我在重设调度之后，忘记调整当前调度命令了！这会导致排程本身就在下一条命令（我重设唯二调度里面的第一条，导致索引跳到了第二条，而我还以为它在第一条！）

### 11.2 日志硬证据（`build\R3R_debug.log`，381 行，由第 206 轮 exe 产出）

veh=54 的全部 `ORD-IDX-WATCH` 行：

| 行 | site | real |
|---|---|---|
| 289 | `cmd-skip-in` | `-1->1` |
| 290 | `cmd-skip-out` | `-1->0` |
| 316 起（本 tick 内） | `tick-in` / `loco-entry` / `skipgate-in` / `post-skipgate` / `post-depotblk` / `pre-processorders` / `post-processorders` / `depotstay-in` / `tick-out` | 一律 `-1->0` |

读档段（`load-rebuild` / `settle-in` / `settle-out` / `syncdriving-*`）对 veh=54 一律 `-1->0`；`depot-edit-in/out` 两行只打 veh=0（玩家拖的是 0 号链）。

⇒ **54 的 real 在 289 行之前就已经是 1**，而把它写成 1 的那一步**没有任何站点行**（黑箱窗），恰好落在"读档之后、第一次 tick 之前"的命令区。

### 11.3 `cmd-skip-in/out` 两行的确切语义：不是"跳过"，是"跳回"

打点位置：`src\order_cmd.cpp:1894`（在 `v->cur_real_order_index = sel_ord` 赋值**之前**）与 `:1923`（之后）。

- `in` 读到 1 ⇒ **进入这条命令时索引已经是 1**（"跳过"早在这次命令之前就完成了）；
- `out` 读到 0 ⇒ `sel_ord == 0` ⇒ 这次 `CmdSkipToOrder(54, 0)` 的作用是**把当前命令指回第一条**。

⇒ 日志里"索引跳到第二条"与"指回第一条"两步之中，只有第二步有探针；第一步（0→1）落在**没有站点的订单编辑命令**里，正是第十节判据 4 描述的黑箱窗。

### 11.4 代码依据（上游既有语义，R3R 未改）

`src\order_cmd.cpp:1632-1670` 的 `InsertOrder()`（上游原文）：

```cpp
/* If there is added an order before the current one, we need to update the selected order. ... */
if (sel_ord <= u->cur_real_order_index) {
    uint cur = u->cur_real_order_index + 1;
    if (cur < u->GetNumOrders()) u->cur_real_order_index = cur;
}
if (sel_ord <= u->cur_implicit_order_index) { /* 同样 +1 */ }
```

⇒ **在"当前命令位置或它之前"插入命令时，当前命令索引 +1**，语义是"当前命令仍是原来那一条，只是它后移了一位"。玩家以为"我新设的第一条就是当前命令"，于是观感变成"索引自己跳到了第二条"。

### 11.5 根因定性

- **第 204 轮"静默跳过 GOTO_COUPLE"极可能同源**：不是 R3R/引擎在无日志处改索引，而是"重设/插入调度第一条"这条订单编辑命令触发了上述上游语义，把当前命令从第 1 条推到第 2 条；`GOTO_COUPLE` 从未被执行，玩家看到列车直接去跑 `GOTO_DEPOT`。
- 第 207 轮日志里**没有任何"无来源的静默写"证据**：唯一一次 `real=1` 可由订单编辑解释，且随后被 `CmdSkipToOrder(sel_ord=0)` 显式指回 0；还原后行为完全符合期望——veh=54 停在 `1,15` 反复 `COUPLE-FAIL` + `COUPLE-DEST-EMPTY ... (waiting in place, order kept)`，即"原地等待符合条件的车底"。
- **待玩家确认的唯一一环**：289 行那次 `CmdSkipToOrder(veh=54, sel_ord=0)` 是否就是他"调整当前调度命令"的那次点击（在订单窗口点第 1 条命令即会走这条命令）。

### 11.6 下一步（收口）

1. **补哨兵（唯一开口）**：KI-311 的观测网覆盖了 tick 前后、R3R 各提交点、读档与 `CmdSkipToOrder`，但**没有覆盖订单编辑命令本体**。建议在 `CmdInsertOrder` / `CmdDeleteOrder` / `CmdMoveOrder` / `CmdModifyOrder` / `CmdClearOrderList` / `CmdReverseOrder` 的 Execute 体前后各加一对 `cmd-insert-in/out` 之类的站点（与 `cmd-skip-in/out` 同构）。判据：若 54 的 `0->1` 落在 `cmd-insert-in/out` 之间，KI-310 即可结案。
2. **玩家侧一次干净复现**：重设调度后先看订单窗口里"当前命令"高亮在第几条、**先抓一次日志**（此时应只有插入、没有 `cmd-skip` 行），再点第一条命令。
3. **产品口径（可选）**：这是上游既有语义，**不建议改索引规则**；若要改善观感，方向是把订单窗口的"当前命令"标记做得更醒目（引擎本就高亮，玩家容易忽略）。

### 11.7 第 207 轮追加证据：`real=1` **不是**存档带入，本会话内确有一次无站点的订单编辑

玩家 2026-10-03 原话：「我确定，刚才有一次命令跳过就是我手动操作的，但是不确定是哪一行的那个」。

- **全日志只有一次 `cmd-skip-*`**：行 289/290（veh=54）。任何"点订单 / 跳命令"都必然留这两行 ⇒ 玩家手动的那一次 **就是 289/290**，其语义是**把当前命令从第 2 条指回第 1 条**（`out` 时 `real=0`，即 `sel_ord=0`）。
- **54 的读档值确为 0**：`load-rebuild`(:36) / `settle-in tag=load`(:39) / `settle-out tag=load`(:42) 三处全部 `real=-1->0` ⇒ 排除"存档带入 / 读档残留"（KI-311 §10.4 判据的候选②被否）。
- **手动 dump 时刻也是 0**：行 221 `R3RDUMP-CHAIN head=54 ow=0 isai=0 n=3 stopped=1 inDepot=1 spd=0 prio=1 borrow=0 ordCount=2 ordbk=0 curReal=0 curImpl=0 coType=0 realType=16 tile=1,15 …`，紧跟的命令表为 `i=0 type=16`(GOTO_COUPLE dest=0)、`i=1 type=6`(GOTO_DEPOT dest=19=38,27,见 :222-223) ⇒ **dump 那一刻当前命令确实停在第一条**。
- **`0->1` 的窗口被夹死**：它必然发生在行 231（`=== R3RDUMP-END ===`）与行 289（`cmd-skip-in`）之间；而这段日志里只有 0 号链的车库拖动（:232 `DEPOT-BEFORE-HEAD-RANK head=6 tmax=1 amin=3`、:233 `DEPOT-BEFORE-HEAD sel=3 head=6 block=0 tail=5`、:234/:238 `ARRANGE-IN dh=0 dst=5 sh=6 src=6 mc=1`、:242-248 `DEPOT-PARK-SHARED veh=6` / `DEPOT-XFER-ID depot-edit head=0 ctrl=6`、:283 `GEO-END settle-depot-edit head=0 n=27`），**veh=54 一字未参与**。
- ⇒ 结论：`0->1` 的写者只能是**订单编辑命令**（无站点覆盖，即 KI-312 的黑箱窗），与玩家自述"重设唯二调度里面的第一条"吻合；本次现场的两个动作（`0->1` 重设、`1->0` 指回）**都是玩家的订单操作**。在观测网覆盖范围内，R3R/引擎没有任何"无来源静默写"。

**给玩家的答案**：要认的就是 **行 289 / 290**（`ORD-IDX-WATCH site=cmd-skip-in` / `cmd-skip-out`，veh=54）——日志里唯一一次跳命令命令本体，且它`out=0` 说明你点的是**第 1 条命令（GOTO_COUPLE）**，是把当前命令**拉回**第一条，而不是跳过它。

### 11.8 第 207 轮落地：KI-312 订单编辑哨兵（已实现 + 已编译 + 已入 exe）

玩家 2026-10-03 回复「好，请你动手」⇒ 按 KI-312 的建议给订单编辑命令补上成对只读哨兵，把 §11.7 里那段「无站点的 `0->1`」窗口彻底封掉。

**实现（只动 `src\order_cmd.cpp`，未碰任何 `src\*.h` ⇒ KI-183 增量合规）**

- 新增文件内匿名命名空间 RAII 包裹 `struct R3ROrderIdxWatch`（:61，定义在 `safeguards.h` 之后、`static_assert(sizeof(DestinationID)…)` 之前）：构造打 `site_in`，**析构**打 `site_out`。选择 RAII 而非手写 in/out 的理由是 `CmdModifyOrder` / `CmdBulkOrder` 体内有多条 `return CMD_ERROR;` 提前返回路径，手写 out 必漏；RAII 让全部 return 路径天然成对。构造时判类型，非 `VehicleType::Train` 自动跳过，绝不误读其它车型。
- **底层出口 2 对**（推荐方案里没列到、但覆盖度最高的一层）：
  - `OrderList::InsertOrderAt()`（:838）→ `ordlist-insert-in` / `ordlist-insert-out`；
  - `OrderList::DeleteOrderAt()`（:867）→ `ordlist-delete-in` / `ordlist-delete-out`；
  - 车主取 `this->GetFirstSharedVehicle()`。这一层覆盖了 `CmdInsertOrdersFromVehicle` 直调 `InsertOrderAt`（绕开 `InsertOrder()` 包装的那些路径）、`CmdMassChangeOrder`、`CmdBulkOrder` 内部批量，以及 R3R 自己在 Couple/Decouple 里插 `WAIT_COUPLE` 的写入。
- **命令层 6 对**：`DecloneOrder`（=订单窗口"清空订单列表"，也被 `CmdDeleteOrder` 越界分支调用）→ `cmd-declone-in/out`；`CmdDeleteOrder` → `cmd-delete-in/out`；`CmdMoveOrder`（拖动排序）→ `cmd-move-in/out`；`CmdReverseOrderList` 的 Reverse 分支 → `cmd-reverse-in/out`；`CmdModifyOrder` → `cmd-modify-in/out`；`CmdBulkOrder` → `cmd-bulk-in/out`。
- 合计 **8 对 / 16 个 site**，与既有 `cmd-skip-in/out` 同构；全部纯只读，不改任何索引语义。

**构建自证**：复用 `_tmp_inc_build.cmd`（未新建任何 `.cmd`）；`build\R3R_incbuild.guard.log` = `GUARD: incremental is safe`；`build\R3R_incbuild.done` = `EXIT_CODE=0`；`src\order_cmd.cpp` @04:36 → `order_cmd.cpp.obj` @04:38 → `build\openttd.exe` @2026-10-03 04:40（51 634 176 B）；日志 `error C` / `fatal error` / `FAILED:` / `build stopped` 计数 0；`read_lints` 0；exe 内命中 `ordlist-insert-in` / `ordlist-delete-out` / `cmd-move-in` / `cmd-reverse-in` / `cmd-modify-in` / `cmd-bulk-in` / `cmd-declone-in`。

**复测判据（这四条一过，KI-310 即可正式结案）**

1. 复现一次"重设调度"：日志里应出现 `ordlist-insert-in/out`（或 `cmd-bulk-in/out`）夹住 `real=0->1`，且 `veh` 与该链头一致；
2. 同一个 `0->1` 不再出现在任何其它 site —— §11.7 的黑箱窗关闭；
3. 随后点第一条命令仍是 `cmd-skip-in`（`1`）→ `cmd-skip-out`（`0`），既有行为不回退；
4. 未做订单编辑的会话里，这 16 个新 site 一行都不出现（无噪声、不影响日志体积）。

**未做（有意）**：不再在 `OrderList::MoveOrders()` 或底层 `InsertOrder()`/`DeleteOrder()` 包装函数处重复打点（命令层 + `InsertOrderAt`/`DeleteOrderAt` 已覆盖全部编辑路径，重复只会让日志出现两层同义行）；不改动任何索引语义。
