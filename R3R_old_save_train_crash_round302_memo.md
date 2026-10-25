# 第 302 轮：老旧存档兼容 —— 「接二连三的列车撞击」+ 崩溃（初步判断）

状态：**取证中 / 尚未改任何源码**。本文件是本轮唯一可信状态载体（结论只认文件）。

## 一、输入（现场）

| 项 | 值 |
|---|---|
| 玩家报告 | 朋友用「R3R v1.0.3」读取一个运行很久的老存档，场景中**接二连三出现列车撞击（train crash）**，随后崩溃 |
| 崩溃日志（最新一次崩溃） | `C:\Users\冯洁敏\Downloads\crash-20261024T105056Z.log`（46 504 B，mtime 2026-10-24 19:29） |
| 崩溃日志（另一份） | `C:\Users\冯洁敏\Downloads\crash-20261020T150946Z.log`（56 399 B，mtime 2026-10-24 21:26 = 下载时间，崩溃时间 10-20） |
| 崩溃日志（更早） | `C:\Users\冯洁敏\Downloads\crash-20261018T144344Z.log`（67 022 B） |
| 玩家自己复跑 | 用**最新内测版**（本工作区 build\openttd.exe）跑同一老存档，**也出现列车撞击** |
| 玩家复跑的调试日志 | `build\R3R_debug.log` 8 141 905 B @ 2026-10-25 14:06:59（内测版探针 ON） |
| 同目录性能日志 | `build\R3R_perf.log` 41 339 470 B @ 14:07:09 |
| 地图 | 4k × 8k（约 3 200 万 tile），玩家明确说这是 debug log 暴涨的原因之一 |
| 录屏 | 朋友发来，玩家仍在研读（**未提供**） |

## 二、已确认事实（有日志锚点）

1. **崩溃不含任何列车栈帧**：`crash-20261024T105056Z.log:10-17` 的 crash reason 是
   `Exception: E1212012` / `Message: Disconnecting road vehicle.`，context 0 = `IndividualRoadVehicleController`
   携带的 veh 是 **39930 / 39929（Road Vehicle）**，front 是 **39928（Road Vehicle 16）**。
   ⇒ 这一次崩溃发生在**公路车辆控制器**里，不是铁路。
2. **这是"第二次崩溃"**：日志第 3 行 `As you loaded an emergency savegame no crash information would ordinarily be generated.`
   ⇒ 会话里**先崩过一次**（那次才是首崩，日志未提供），游戏用紧急存档重启，随后又崩在公路车上。
3. **朋友用的版本**：`r3r-stable-2026-10-16-1-g2fa4018d2-m (2)`，Release ver 0.73.1，Build date Oct 18 2026，
   `Defines:` 末尾 `R3R_PROBES_DEFAULT=0` ⇒ **探针默认关闭的发布版**，与工作区内测版不是同一个二进制。
4. 存档是 JGRPP 老档：`Revision text changed to jgrpp-0.69.2, savegame version 292`（`:269`），
   NewGRF 数量 `last_newgrf_count: 599`（`:110`），期间多次 `GRF config changed`（`:271/:427/:429/:431`）。
5. **车辆池已经是 5 位量级**：road veh index 39928~39930 ⇒ 该存档车辆总数约 4 万。
   任何 O(车辆数) × O(tile 数) 的读档期逻辑在这种档上都会被放大到秒级/分钟级。
6. 崩溃点 **不是** `CheckTrainCollision`、**不是** PBS 断言、**不是** rail_map.h:168 那类预留断言，
   也没有 `dbg_assert` 字样 —— 是显式抛出的异常（E1212012 是 "throw an error object" 的编码）。

## 三、猜测根因（待验证，按可能性排序）

- **G-1（首选）段/★ 标记在旧档里不成闭环 ⇒ 物理重叠 ⇒ 原生 TrainCrashed 连环触发。**
  R3R 的段身份（★ `SegmentFront`、⊗ `SegmentBack`、`r3r_segment_id`、段行）全部是**本 mod 新增**，
  旧档（JGRPP 0.69.2 时代、甚至 R3R 早期版本）里可能：根本没有 ★、或 ★ 数量与铰接单元数不符、
  或 ★ 存在但段行（R3VP/R3SG 稀疏块）缺失。读档时 `R3RRebuildCouplePriorities()` → `R3RSettleChainSegments()`
  会**按现有 ★ 重新推导段**，一旦推导出的段边界与物理链/几何不匹配，
  `ConsistChanged(CCF_SAVELOAD)` 重算出来的 cached 长度/位置就会让相邻车节在像素上重叠，
  原生 `CheckTrainCollision` 判定为撞车 ⇒ 一列车刚修好下一列又撞（"接二连三"）。
- **G-2 读档期迁移链在旧档上产生非法状态。** 读档路径上有三处会**主动改写**车辆：
  ①第 180 轮 KI-273 的 `R3RDearticulateLegacyArticSegment()`（对「带 ★/⊗ 且含真铰接」的链**自动打散**）；
  ②第 301 轮新加的 `R3RReleaseStaleNonSegmentUnitNumbers()`（回收幽灵车号）；
  ③第 161 轮 `R3RBorrowControlTraitsLive(chain, "load")`（借用控制段号/名）。
  三者都只在读档跑一次，在老档上的输入分布与内测档完全不同。尤其 ① 会**改变 chain 上每节车的角色**，
  若与 ②③ 的执行顺序耦合，可能留下"段头已打散、段行还指向旧段"的中间态。
- **G-3 预留体系在旧档上不自洽 ⇒ 两列车被允许进入同一格。**
  旧档会把 `_m[t].m2` bit 8-11 的预留位整份带进来（KI-203/KI-345 一族），
  而 R3R 的内存账本（`_r3r_consist_resv`）是 NOSAVE，读档时为空 ⇒ 车会释放它"以为自己订的"位、
  或反过来不敢释放孤儿位。若某格预留被清掉而两列车都认为"约好在那格交汇"，就是原生撞车。
- **G-4 公路车崩溃与撞车可能有因果，也可能完全独立。** 可能路径：连环撞车 → 存档被写坏（紧急存档）
  → 重启后路网/车库索引不一致 → 公路车在"不该 disconnecting"的状态下被 disconnect ⇒ 抛异常。
  也可能是**独立**的老 bug（`IndividualRoadVehicleController` 的 disconnecting 分支）。
  **这条必须先定性，否则修错方向。**
- **G-5 日志暴涨的直接原因**（不是 bug，是开销）：`R3RReservationAudit()` 是**遍历全图**找无主预留，
  4k×8k ≈ 3 200 万 tile 每次全扫；`RESV-AUDIT-SUMMARY`/`RESV-PURGE` 一次就是几万行。
  叠加探针 ON + 车辆 4 万，`R3R_debug.log` 涨到 GB 级完全可预期。**需要确认玩家那份 8 MB 是被谁刷大的。**

## 四、需要向玩家/朋友确认的信息（编号回填）

- **Q-1** 首崩（导致"加载 emergency savegame"的那次）的 crash log 有没有？文件名时间戳应早于 10-24 10:50。
  现在的 10-20/10-24 两份里，10-20 那份是不是首崩？
- **Q-2** 撞车是**读档后立刻**发生，还是跑了一段时间（几分钟/一天游戏时间）后开始？
  撞的是**同一列车反复**还是**全图多点**？撞车前那些列车是否刚经过站台挂车/解挂/车库编辑？
- **Q-3** 那个老存档**之前有没有被 R3R 版本保存过**（即已经是 R3R 档），还是纯 JGRPP 老档第一次进 R3R？
  （这一条决定"段标记缺失"是否可能。）
- **Q-4** 玩家复跑（内测版）时 `build\R3R_debug.log` 里，撞车前后有没有出现
  `LEGACY-ARTIC-DEARTIC` / `UNIT-RELEASE` / `UNIT-DROP` / `SEGLOAD-PARK` / `GRP-RESTORE-SEG-LOAD` / `INVARIANT-BROKEN`？
- **Q-5** 朋友那份是**发布版**（探针 OFF）：撞车时游戏内有没有弹"列车相撞"消息？撞车与"两列车重叠"是否同时可见？
- **Q-6** 录屏里能否看到：撞击的列车是否**首尾相接/叠在同一格**（几何重叠）还是**对向相撞**（信号/PBS 问题）？
- **Q-7** 是否会**必然**复现（读档即撞），还是随机？如果是必然，能否提供一个**读档后只跑几十秒就撞**的最小存档？

## 五、取证计划（下几步，按顺序）

1. 读 `build\R3R_debug.log` 里与撞车有关的行（`CRASH`/`TrainCrashed`/`check_train_collision`/`INVARIANT-BROKEN`/
   `RESPACE-AFTER-EDIT`/`GEO respace`/`LEGACY-ARTIC`/`SEGLOAD`），确认撞车时链上段数与几何。
2. 读 crash-20261020 那份，对比是否也是 road vehicle（若是，则 G-4 独立 bug 概率上升）。
3. 在源码里确认 `"Disconnecting road vehicle."` 的抛出条件与触发点。
4. 若日志证实"段数 ≠ 铰接单元数"，则按 KI-253 家族处理（几何塌缩 → 重叠 → 撞车）。

## 六、本轮明确未做

- 未改任何源码、未构建。
- 未拿到录屏与首崩日志，G-1~G-4 全是待验证假设。

## 七、首轮只读取证（2026-10-25）

### 7.1 崩溃语义（已定）

- 抛出点确认：`src\roadveh_cmd.cpp:1758` `if (!v->IsFrontEngine()) FatalError("Disconnecting road vehicle.");`
  触发条件 = `IndividualRoadVehicleController` 算出 `dir == INVALID_TRACKDIR`。
- 方向来源：`src\roadveh_cmd.cpp:1361 FollowPreviousRoadVehicle(v, prev, tile, entry_dir, already_reversed)`；
  其第一条判据 `:1363 if (prev->tile == v->tile && !already_reversed) return _road_reverse_table[entry_dir];`
  ⇒ **只有当 `v->tile != prev->tile` 时才会往下走**，才会走到 `:1381 return INVALID_TRACKDIR`。
- 崩溃现场 tile 读数（`crash-20261024T105056Z.log:16`）：
  - 39930（后节，`front: 39928`）@ tile `0x78DB65` = (7013, 966)
  - 39929、39928（前车，`Road Vehicle 16`, `st:F`）@ tile `0x7CDB65` = (7013, 998)
  - 差值 `0x40000` ÷ 地图宽 8192 = **32 行** ⇒ 同一链上"后节"与前车相距 32 格。
- 判读：**这不是铁路撞车的直接栈**，而是公路车**链上 tile 关系已不一致**（后节停在一个陈旧坐标）导致的 fatal。
  另一要点：39930 的 `vs:D`（含 Hidden）、`vf:` 为空 ⇒ 形态像"正在删除/已隐藏的后节"，即**卖车/崩溃中间态**。

### 7.2 内测版日志统计（只读，`build\R3R_debug.log`）

| 指标 | 值 |
|---|---|
| 总行数 | 88 277 |
| `RESPACE-AFTER-EDIT` | **0** |
| `RESV-AUDIT` | **1**（仅 SUMMARY） |
| `INVARIANT-BROKEN` | **0** |

**结论句（重要）**：`R3RRespaceChainAfterEdit()` 的唯一调用点是耦合提交点，`R3RSettleChainSegments` 的探针
也没出现 ⇒ 本次会话**没有发生任何挂车/解挂/车库编辑类链编辑**，也没有"提交后仍折叠"的不变量报告。
⇒ **撞车不能用"链编辑提交点把几何搞坏"来解释**（这是过去几轮的常客，本轮排除）。
⇒ 撞车只能来自：读档期推导、运行期预留/碰撞判据、或与 R3R 无关的老档自身问题。

### 7.3 8 万行的来源仍未定位

- 只有 1 行审计、0 行 respace ⇒ 刷屏主体是别的前缀（候选：`RESV-GHOST` / `RESV-PURGE` / `CPL-*` / `TRP` / `GEO` / `DEPOT-*`）。
- 8 MB / 88 277 行 ≈ 92 B/行，属"日志偏大"但未到 GB 级；需确认玩家说的"暴涨"是否指**持续增长不停**。

### 7.4 待验证假设（更新后的排序）

- **H-1（升为第一）预留体系在超大地图上被 R3R 自己清坏。** `R3RReservationAudit()` / `R3RPurgeStrayReservations()`
  靠 `GetTrainForReservation()/GetTrainForWholeTileReservation()` 判"有主/无主"；4 万车 + 3 200 万 tile 时，
  一旦某类合法预留被判成无主而 `RESV-REAP` 释放，另一列车就会被放行进入同一格 ⇒ 原生撞车（且是"接二连三"）。
  取证点：日志里 `RESV-AUDIT-SUMMARY` 的 `strayTiles=`、`RESV-REAP`、`RESV-GHOST`、`RESV-PURGE` 行数与 tile。
- **H-2 旧档段身份半残 ⇒ 长度/位置重叠。** 若存档曾被 R3R 早期版本保存过（★ 有但段行 R3VP/R3SG 缺，或反之），
  读档 settle 推导的段边界与物理链不符 ⇒ `cached_veh_length` 与真实几何错位 ⇒ 列车自我/相互重叠 ⇒ 撞。
- **H-3 `CheckTrainCollision` 的 R3R 边界改动（KI-106 把 `>=` 改成 `>` 一族）在原生布局上误判。**
  老档里大量列车是原生紧贴布局，恰好落在 1px 边界 ⇒ 一读档就接连判撞。需复核当前 `CheckTrainCollision` 的 R3R 改动面。
- **H-4 公路车崩溃是撞车/紧急存档的下游**：撞车→存档中间态→重启后链上 tile 不一致→`FatalError`。
  也可能是独立老 bug，需对比 10-20 那份崩溃日志是否同为 road vehicle。

---

## 八、玩家答复 Q1~Q6（2026-10-25，原话要点，逐条落盘）

- **Q1 → 只有一份崩溃日志**：朋友只发了 `crash-20261024T105056Z.log` 这一份，**没有首崩日志**。
  ⇒ 007.4 的 H-4「用 10-20 那份对比」作废（那两份是更早日期的，但不是本次事故的首崩，不能当同一事故链）。
- **Q2 → 读档后不到一分钟**就接二连三出现事故。⇒ **时间窗 < 60 秒**，这是本轮最有约束力的量化条件。
- **Q3 → 决定性事实：这就是 JGRPP 0.73.0 原生存档，用 R3R 读取的。**
  （R3R 本体版本 0.73.1，见 crash log `:23 Release ver: 0.73.1`）
  ⇒ 存档里**一条 R3R 数据都没有**：没有★、没有⊗、没有 `r3r_segment_id`、没有 R3VP/R3SG/R3SY 任何稀疏块。
  ⇒ H-2（段身份"半残"）**改写**：不是半残，而是"**从零一次性给全图 4 万车的原生链建段**"。
  ⇒ 且跨小版本读档（0.73.0 档 → 0.73.1 引擎）本身走原生 afterload 迁移。
- **Q4 → 玩家没看日志**（一贯把日志丢给 AI）。⇒ 需要玩家重新产出一份，见 §九。
- **Q5 → v1.0.3 = 2026-10-18 编译的无探针发行版**（对应 crash log 的 `Build date: Oct 18 2026`，`R3R_PROBES_DEFAULT=0`）；
  不必麻烦朋友，**玩家已把朋友的存档要过来了**。
- **Q6 → 观察清晰的一例是列车追尾，且被追尾列车脚下的预留为"无"**，玩家怀疑"审计乱搞"。

## 九、由 Q1~Q6 收紧后的假设表（本轮结论）

### 9.1 时间窗把嫌疑面砍掉一大半

`R3R_RESV_REAP_AUDITS = 3` 次审计才 reap；审计节拍 = `_r3r_resv_audit_due ? 256 : 2048` tick。
- 稳态 2048 tick/次 ⇒ 3 次 = 6144 tick ≈ **3 分钟以上**（1× 速度 33 tick/s） ⇒ **60 秒内不可能靠稳态审计 reap 出事故**。
- 若读档/命令把 `_r3r_resv_audit_due` 置位 ⇒ 3 次 = 768 tick ≈ **23 秒** ⇒ **落在 60 秒窗口内，无法排除**。
⇒ **REG-302-B（新疑点）：`_r3r_resv_audit_due` 在读档路径上是否被置位？** 若被置位，玩家的"审计乱搞"怀疑就成立；
  若没被置位，纯审计 reap 在 60 秒内根本跑不完 3 次，"审计"这条要降级为次要。
- 反之，**读档期一次性动作**（`R3RPurgeStrayReservations()`、`R3RRebuildCouplePriorities()`/`R3RSettleChainSegments(chain,"load")`）
  在第 0 秒就生效 ⇒ **与 60 秒窗口完全吻合**，优先级应高于 reap。

### 9.2 H-5（新增，并列首选）：撞车判据本身在原生布局上误判

R3R 动过 `CheckTrainCollision` 的边界比较（KI-106 记：`-1` 与把 `>=` 改成 `>`）。
原生 0.73.0 档里全部列车是**原生容差下合法**的紧贴布局；判据一收紧，读档后每列车一移动就判撞
⇒ 完全符合「**一读档就发生**」+「**接二连三**」+「**不依赖任何 R3R 功能**」+「**原生档首次进 R3R 就中招**」四个特征。
⇒ 本条必须**优先静态复核**（纯读代码，不必跑游戏）。

### 9.3 Q6 的观察可以两种解释，必须区分

被追尾列车"脚下预留为无"：
- 解释 α（玩家假设）：R3R 的 purge/audit **把它的合法预留清掉** ⇒ 后车获准进入同一格 ⇒ 追尾。
- 解释 β（正常行为）：**停稳的列车本来就会释放脚下预留**（`cur_speed==0` / Stopped），
  追尾是别的判据问题；"预留为无"只是停车的正常表现，甚至可能是**撞车之后**产生的观感。
⇒ **判据：撞车前那一瞬，被追尾列车是否在移动（`cur_speed>0`）、是否 `Stuck`、是否 `WAIT_COUPLE`。**
  若它在移动却有整条前方路径预留 ⇒ 支持 α；若它停稳 ⇒ β 优先，H-5 升为第一。

### 9.4 新的疑点登记

- **REG-302-A**：崩溃栈（road vehicle，`vs:D`/`vf:` 空/后节 tile 偏离 32 格）与铁路撞车**没有直接栈关系**。
  原生档首次进 R3R 时，`AfterLoadVehiclesPhase2` 是否对**非 Train** 车辆做过任何改写？若没有，这个崩溃就只能归为
  "撞车后紧急存档的中间态"或独立老 bug；**不能**把它当撞车主证据。
- **REG-302-B**：`_r3r_resv_audit_due` 在读档路径是否被置位（决定"审计乱搞"能否在 60 秒内发生）。
- **REG-302-C**：`R3RPurgeStrayReservations()` 的 owner 判据（`GetTrainForReservation` / `GetTrainForWholeTileReservation`）
  在**原生 0.73.0 存档带入的预留位**上是否判得准。判不准 ⇒ 读档即清合法预留 ⇒ 与 Q6 的 α 完全对应。

### 9.5 下一步取证顺序（纯静态，不启动游戏）

1. 复核 `CheckTrainCollision` 里 R3R 改了哪几处比较（H-5）。
2. 复核 `R3RPurgeStrayReservations()` 的**调用时机**（是否 part_of_load / 首个 tick）与 owner 判据。
3. 复核 `_r3r_resv_audit_due` 的置位点（REG-302-B）。
4. 上述确定后，再谈让玩家跑一次带探针的读档（存档已在手）。

## 十、第 302 轮续：基线证据（v1.0.3 落后于整轮预留重写）+ 日志暴涨定性（2026-10-25）

### 10.1 决定性证据：朋友的 exe 与当前树相差 50+ 轮，落后的正好是预留子系统

`git log`（工作区实测，按时间）：

| 提交 | 时间 | 含义 |
|---|---|---|
| `94bcd67019` | 2026-10-18 02:03 | `2026-10-18 stable checkpoint (rounds 219-248)` |
| `77571065e1` | 2026-10-18 02:05 | `release memo for v1.0.3` |
| `c41ca0f227` | 2026-10-20 16:04 | `temporary rollback checkpoint (rounds 249-255)` |
| （未提交工作区） | 2026-10-21 ~ 10-25 | 第 256 ~ 300 轮 |

`git --no-pager diff --stat 77571065e1 -- src/train_cmd.cpp src/pbs.cpp src/pathfinder/yapf/yapf_rail.cpp`：

```
 src/pathfinder/yapf/yapf_rail.cpp |  132 +-
 src/pbs.cpp                       |  357 +++-
 src/train_cmd.cpp                 | 4146 +++++++++++++++++++++++++++++++++----
 3 files changed, 4230 insertions(+), 405 deletions(-)
```

⇒ v1.0.3 之后 `train_cmd.cpp` 改写约 3 750 行、`pbs.cpp` +357、`yapf_rail.cpp` +132，
改动主体正是**预留账本（R3RResvLedger）/ 释放法则（Release law）/ 站台整段释放校验 / 清扫与审计闸门**。
⇒ **第一条纪律：v1.0.3 上的"预留被乱清"不能用来指认当前树。**

### 10.2 报告症状与已修条目逐字同族

- 第 266 轮 **KI-406**：解挂互锁几何放行过早 ⇒「后车抢跑」。
- 第 267 轮 **KI-407**：站台**整段整格**预留释放不校验"该格是否已有别的车/别人的预留" ⇒ 后车刚拿到路权就同时被清空。
- 第 276 轮 **KI-427**（`src/train_cmd.cpp:15066-15091`，代码注释原文）：
  > 前车出站时执行这一句，就把仍然停在站台上的后车那条预留（含后车脚下）一并销毁了 …… 于是"站台上已经没有预留"。

玩家原话「被追尾列车脚下的预留为无」＝ KI-407 / KI-427 的原症状。**v1.0.3（10-18）里 KI-427 的
`IsRailStationPlatformFree()` 双向校验尚不存在**（该段代码标注为 2026-10-22 第 276 轮）。

### 10.3 §9.1 的时间闸门结论必须修正（REG-302-D）

实测常量（当前树）：

- `R3R_RESV_SWEEP_TILES = 8192`（`train_cmd.cpp:18101`）
- `R3R_RESV_REAP_TICKS = 3 * 2048 = 6144`（`:18127`）
- `R3R_RESV_SWEEP_REAP_TICKS = 3 * 2048 = 6144`（`:18415`）
- `R3R_RESV_AUDIT_TICKS = R3RDbgOn() ? 2048 : 8192`（`:21016`）

这些闸门全部按**游戏 tick** 计时：

- 1× 速度（约 30 tick/s）下 6144 tick ≈ 205 s ⇒ 60 秒窗口内 reap 不成立；
- 但**快进 N 倍就把墙钟缩短 N 倍**（大图 Release 版快进 5~10× 很常见）⇒ 6144 tick 可在 40~70 秒内走完，
  **正好落在玩家报的窗口内**。

⇒ 「60 秒内不可能靠 reap 出事」这条**只在 1× 速度下成立**，不能当排除性论证。登记 **REG-302-D**。

### 10.4 内测日志暴涨来源（已定位，KI-478）

`build\R3R_debug.log`（88 277 行 / 8.1 MB）行前缀统计（Top 14）：

| 前缀 | 行数 | 性质 |
|---|---|---|
| `RESV-GC` | 30 069 | 预占台账 GC 释放 |
| `ORD-IDX-WATCH` | 12 196 | 订单索引 |
| `RESV-REL` | 11 450 | 预留释放（CLEAR-STN / CLEAR-RAIL） |
| `CRT` | 5 851 | 选道 |
| `TT-CHK-BREAK` | 5 429 | 时间表校验 |
| `RESV-LEDGER-FORGET` | 4 731 | 账本整掩码注销 |
| `LINEND` / `TRP` / `TPR-WALK` / `SPD-DROP` / `EXTEND-BACKOUT` / `RESV-WATCH-SUM` | 3 108 / 2 628 / 2 242 / 1 661 / 1 454 / 1 376 | |

⇒ **预留释放类（`RESV-GC` + `RESV-REL` + `RESV-LEDGER-FORGET` = 46 250 行，占 52%）** 是暴涨主因，
即"预留订了又放"的高频 churn（与第 278 轮 KI-437 观察同族），不是普通运行噪声。

### 10.5 结论（本轮）

1. **不在 v1.0.3 上取证**：它缺 KI-406 / KI-407 / KI-427，且预留子系统在 10-20 之后被整体重写。
2. 下一步＝**用当前树出一个新发布版**（`build-release`），把朋友的同一份存档读进去复跑；
   若仍复现，再按 `SPD-DROP` / `RESV-*` 取证（此时需要探针包）。
3. 崩溃日志维持 REG-302-A 定性（公路车 `FatalError("Disconnecting road vehicle.")`，且是**第二次崩溃**，
   首崩信息缺失），不作为铁路撞车主证据。
4. H-5（`CheckTrainCollision` 误判）**初读偏排除**：R3R 在该函数里的改动以**放宽**为主（接缝免撞早退），
   未见"让原生紧贴布局更容易判撞"的收紧；但仅一轮初读、未与原生版本逐行比对 ⇒ 保留为待复核，不结案。

## 十一、根因锁定：v1.0.3 的「整条到发线无条件释放」（2026-10-25）

> **【更正 §10.2】** 上一节写"KI-427 的 `IsRailStationPlatformFree()` 双向校验尚不存在"——**错了**。该判据在
> v1.0.3 里**早就存在**（`src/pbs.cpp:756` 定义 + `src/pbs.h:120` 声明），但**零调用点**（死代码）。
> KI-427 修的不是判据，而是**把这条既有却从未被调用的判据接进释放路径**。本节的证据以此为准。

### 11.1 证据 1：v1.0.3 里该判据是死代码

```
$ git --no-pager grep -n "IsRailStationPlatformFree" 77571065e1 -- src
77571065e1:src/pbs.cpp:756:bool IsRailStationPlatformFree(const Train *v, TileIndex start, DiagDirection dir)
77571065e1:src/pbs.h:120:bool IsRailStationPlatformFree(const Train *v, TileIndex start, DiagDirection dir);
```

只有定义与声明，**没有任何调用**。当前树同名判据有 4 处调用（`src/pbs.cpp:999` 定义；
`src/train_cmd.cpp:2621`/`:2622` 的 `R3RPlatformClearForConsist()`、`:15088` 的释放闸门）。

### 11.2 证据 2：同一段代码在 v1.0.3 与当前树的逐字对照

v1.0.3（`77571065e1:src/train_cmd.cpp:13003-13009`，`ClearPathReservation()` 的站台分支）：

```cpp
} else if (IsRailStationTile(tile)) {
    DiagDirection dir = TrackdirToExitdir(track_dir);
    TileIndex new_tile = TileAddByDiagDir(tile, dir);
    /* If the new tile is not a further tile of the same station, we
     * clear the reservation for the whole platform. */
    if (!IsCompatibleTrainStationTile(new_tile, tile)) {
        SetRailStationPlatformReservation(tile, ReverseDiagDir(dir), false);   // ← 无条件整条清
    }
}
```

当前树（`src/train_cmd.cpp:15060-15091`）：

```cpp
        if (!IsCompatibleTrainStationTile(new_tile, tile)) {
            /* R3R (KI-427, 第 276 轮 ...): 整条到发线的释放
             * 只在"本到发线上除了本链之外没有别的列车"时才做。 */
            const Axis station_axis = GetRailStationAxis(tile);
            const DiagDirection axis_fwd = AxisToDiagDir(station_axis);
            const DiagDirection axis_back = ReverseDiagDir(axis_fwd);
            if (IsRailStationPlatformFree(v, tile, axis_fwd) && IsRailStationPlatformFree(v, tile, axis_back)) {
                SetRailStationPlatformReservation(tile, ReverseDiagDir(dir), false);
            }
        }
```

⇒ **差别只有一道闸门**；v1.0.3 没有。

### 11.3 因果链（对得上玩家 Q6 的每一个字）

1. `SetRailStationPlatformReservation(start, dir, false)` 沿站台轴把**整条到发线**的站台预留位清掉
   （`src/pbs.cpp:731` 起逐格 `IsCompatibleTrainStationTile` 走到底）。
2. 触发条件只是"释放路径里有一格是站台末端格"（`!IsCompatibleTrainStationTile(new_tile, tile)`），
   **不校验该到发线上还有没有别的列车**。上游之所以敢这么写，是因为上游假定"一条到发线同时只服务一列车"
   （进站时由 `ReserveTrackUnderConsist()` 整条订满，直到最后一节车离开才一次性全清）。
3. 该假设在 R3R 下被打破：v1.0.3 的 `ReserveTrackUnderConsist()` 在
   `77571065e1:src/train_cmd.cpp:4574` 也是**无条件**写 `SetRailStationPlatformReservation(..., true)`，
   不看归属 ⇒ 停稳链/等待链/解挂后的后车都会在站台上"占着"整条到发线。
4. 于是任意一列车离站（或释放路径扫到该站台末端格）就把**另一列车脚下的预留一起销毁**
   ⇒ 被追尾列车"脚下的预留为无"（玩家原话，逐字对应）。
5. 该到发线在预留层面变为空 ⇒ 原本被 PBS 挡住的后车获准进入 ⇒ **追尾**。
6. 一次进站/出站的动作在全图**每个繁忙站台**独立发生 ⇒ 读档后几十秒内"**接二连三**"。

对照反例（说明这不是引擎的普遍写法）：v1.0.3 撞毁车删除路径
（`77571065e1:src/train_cmd.cpp:17290-17299`）在写整格位之前先算
`bool occupied = IsRailStationPlatformOccupied(tile);` 再双向写 ⇒ **有归属校验**。
唯独释放路径没有。

### 11.4 时间窗（修正 §9.1 与 §10.3 的口径）

v1.0.3 的常量（`77571065e1:src/train_cmd.cpp`）：

- `R3R_RESV_AUDIT_TICKS = 2048`（`:17736`，**平坦值、无 `R3RDbgOn()` 闸门**）
- `R3R_RESV_AUDIT_TICKS_SOON = 256`（`:17737`，脏标记触发）
- `R3R_RESV_REAP_TICKS = 3 * 2048`（`:15202`）

⇒ 稳态 3 次审计 = 6144 tick ≈ 205 s（1×），**快进 5× ≈ 41 s、10× ≈ 20 s 落在 60 s 窗口内**。
但**主因不依赖审计**：§11.3 的机制在第一次有车离站的时刻就能触发。
⇒ 结论：审计降级为"次因/放大器"，**主因＝§11.3 的无条件整条释放**。

### 11.5 结论与复测判据

1. **根因（已定性，代码级）**：v1.0.3 = 10-18 检查点，`ClearPathReservation()` 的站台分支缺
   "该到发线是否还有别人"这道闸门（第 276 轮 KI-427 才补上），导致"一车离站 → 清整条到发线 →
   另一列车脚下预留消失 → 后车获准进入 → 追尾"。
2. **不需要再在 v1.0.3 上取证**；用当前树出 `build-release`，同一存档复跑。
3. 复测判据：①读档后 60 秒内无追尾；②若仍复现，日志里查 `RESV-GC ... track=stn` 与
   `CLEAR-STN phase=<非 release-ghost>` 的配对，以及被撞车在撞前一刻 `cur_speed>0` 却有整条前方预留；
   ③负判据：单车独占站台的普通出站仍能整条释放（不出现站台预留泄漏累积，`RESV-AUDIT-STRAY kind=STATION` 不增长）。

### 11.6 因果链闭合：「撞车判据是纯几何，避让 100% 依赖预留位」（2026-10-25）

补上 §11.5 遗留的唯一推断环节——**为什么"清掉前车预留"会变成"追尾"而不是"后车停下等"**。

- `CheckTrainCollision(Train *v, Train *moving_front)`（当前树 `train_cmd.cpp:18782` 定义、`:19042` 聚合入口）
  **全程没有任何预留位查询**。它做的只有：仓库特例 `:18785`、跨公司开关 `:18859`、自称自碰撞豁免
  `:18866-18872`、粗筛哈希 `:18881-18882`、平方距离判据 `:18885/:18892`、高度差 `:18902`。
  ⇒ **"两列车不会重叠"从来不是撞车判定保证的**，而是靠"另一列车占着的格带着预留位 ⇒ YAPF 不敢订"保证的。
- 因此 §11.3 的"清掉别人脚下的预留"不是"少了一道保险"，而是**直接删掉了唯一的障碍标记**：
  后车 YAPF 认为该到发线空闲 → `rp()`/`TryReserveRailTrackdir()` 订进去 → 车头开进站台 →
  `CheckTrainCollision` 看到像素重叠 → `TrainCrashed()`（`:19030-19031`）。
  这就是"追尾碰撞"而非"后车停车等待"的机制来源。
- 另证玩家原话「被追尾列车**脚下的**预留为无」：被撞那列正停在到发线上，它脚下那几格恰恰被前车离站时
  一次整条清掉。

**新登记的残余缺口（REG-302-E）**：`IsRailStationPlatformFree()`（`pbs.cpp:999-1017`）的判据是
逐格 `Vehicle::Iterate()` + `t->tile == tile`（跳过撞毁车、跳过 `First()` 相同的本链），
即它只保护"**物理上正停在这条到发线上的车**"，**不保护"已订下这条到发线、车还没开进来"的列车**。
后者在 R3R 的 `ReserveTrackUnderConsist()`（v1.0.3 `:4574` 无条件写整格位 `true`）之后确有发生空间
⇒ 已登记为 REG-302-E，**未修**，主因（KI-477）不依赖它。

### 11.7 版本映射核实（2026-10-25，修正"修复在哪一版"）

| 提交 | 日期 | 内容 |
|---|---|---|
| `94bcd67019` | 2026-10-18 02:03:29 | stable checkpoint（rounds 219-248） |
| `77571065e1` | 2026-10-18 02:05:54 | v1.0.3 release memo（即朋友那版，= 219-248 基线） |
| `c41ca0f227` | 2026-10-20 16:04:32 | temporary rollback checkpoint（rounds 249-255）= **HEAD** |
| 工作树 | 10-20 之后 | rounds 256~302，**未提交** |

- `git log -S "IsRailStationPlatformFree" -- src/train_cmd.cpp` ⇒ **零命中**：第 276 轮那道站台闸门**从未进入任何提交**
  （该函数本身 2026-08-28 `e30c8b0f1b` 就存在于 `pbs.cpp`，只是一直没被释放路径调用）。
- `git status --short -- src/train_cmd.cpp src/pbs.cpp src/pbs.h` ⇒ 三者皆 ` M`（已修改未提交）。
- ⇒ 两个后果：①朋友那版（219-248）与 HEAD（249-255）**都没有**该闸门，所以"落后"的判定成立且比原先估计更彻底；
  ②**出发布版必须直接从当前工作树编译**，任何 `git checkout` / `git stash` / reset 回退都会把这 47 个轮次的修复一起丢掉。

### 11.8 【更正 §11.3 / §11.5】现场清晰样本在**咽喉区**，不是站台（2026-10-25，玩家原话）

> 玩家原话：「额，我观察到的追尾不仅发生在站台上啊，包括我观察清晰的那一个例子就是一个列车
> **在车站咽喉区等待**，结果**脚下的预留被神秘清除**了，于是后车就和它撞上了」。

**这条直接推翻"站台整条到发线无条件释放"作为唯一/主要路径**：
- `ClearPathReservation()` 的站台分支只在**当前格与下一格不兼容**（即车即将离开这条到发线）时才触发，
  且只清 `IsCompatibleTrainStationTile()` 连续的那一段；**咽喉区是普通轨道格（plain rail），
  走的是 else 分支，完全不经 `SetRailStationPlatformReservation()`**。
- ⇒ §11.3 的机制至多解释"站台上被追尾"的那几起；玩家看得最清楚的那一起（咽喉区等待车）
  **必须另有释放路径**。KI-477 的状态由「根因」降为「站台型追尾的根因之一」。

**猜测根因（待验证）——`FreeTrainTrackReservation()` 的 plain rail 逐格释放不看归属，且用的是"当前朝向"**：
1. 引擎释放普通轨道预留的路径是逐格 `UnreserveRailTrack(tile, TrackdirToTrack(td))`，
   而 `UnreserveRailTrack()` **无条件清位**（不校验该位是谁订的，也不校验格上有没有车）。
2. 起始 `td` 来自 `GetVehicleTrackdir()`，即**由车当前 `direction` 推出**。
3. R3R 的"逻辑翻/换端"（`R3RReverseChainDirections` / `R3RFlipChainBySegments`）**只改 `direction` 与链序，
   不动 `x_pos/y_pos/tile`** ⇒ 翻转后 `GetVehicleTrackdir()` 指向的是**反方向那一侧**，
   于是这一步回溯释放会沿着**车根本没走过的那一支**去 `UnreserveRailTrack`。
4. 咽喉区正是"多支线汇入、相邻格分属不同车"的地方 ⇒ 那些格/轨很可能正是**另一列等待车的预留**，
   被无条件清掉 ⇒ 该车"脚下的预留神秘消失" ⇒ 后车 YAPF 认为空闲、订进来 ⇒ 撞（§11.6 已证撞车判定纯几何）。

**为什么"神秘"**：清位者不在被清车旁边、也不在同一条到发线上，日志里两件事（翻转/换端 与 预留消失）
相隔很远，只有把 `UnreserveRailTrack` 的无归属语义与 `direction` 被改这两件事拼起来才连得上。

**待验证判据**（下一组取证即查）：
- V-1 当前树 `Train::ClearPathReservation()` 的**非站台分支**是否也是逐格 `UnreserveRailTrack`、是否校验归属。
- V-2 `FreeTrainTrackReservation()` 的起始 `td` 是否取 `GetVehicleTrackdir()`（即受 `direction` 影响）。
- V-3 R3R 的翻转/解挂/车库编辑提交点里，是否存在"先改 direction/链序，再调用释放"的顺序 ⇒ 错向释放。
- V-4 现场日志：咽喉区那列被撞车的格位能否在 `RESV-REL` / `CLEAR-RAIL` 一类行里找到**非本人**的 actor。

### 11.9 V-1 / V-2 取证结果：plain rail 释放确实「逐格无归属」+「朝向驱动」；新增两条路径（2026-10-25）

**V-1 成立（代码锚点 `src/train_cmd.cpp:15092-15095`）**：`ClearPathReservation()` 的其他格分支就是
```cpp
} else {
    /* Any other tile */
    UnreserveRailTrack(tile, TrackdirToTrack(track_dir));
}
```
`UnreserveRailTrack()` 只按参数清位，**既不校验该 track 位是谁订的，也不校验格上有没有车**。
站台分支（`:15060-15091`，KI-427 闸门在 `:15088`）与咽喉区无关 ⇒ 玩家看得清的那一起必须由 else 分支解释。

**V-2 成立（`src/train_cmd.cpp:15141-15143`）**：
```cpp
TileIndex tile = origin != INVALID_TILE ? origin : moving_tile;
Trackdir  td = orig_td != INVALID_TRACKDIR ? orig_td : moving_front->GetVehicleTrackdir();
```
没有 `orig_td` 时基准方向就是**当前朝向**推出的 trackdir ⇒ R3R 只改 `direction` 不改位置的翻转，
会让"释放方向"与"车实际走过的方向"脱钩（§11.8 猜测第 2/3 条成立）。

**新增关键发现（比"错向"更普遍的一条）：整条释放循环从头到尾不比对归属（`:15169-15210`）**
```cpp
CFollowTrackRail ft(consist, consist->GetIndirectCompatibleRailTypes());
while (ft.Follow(tile, td)) {
    tile = ft.new_tile;
    TrackdirBits bits = ft.new_td_bits & TrackBitsToTrackdirBits(GetReservedTrackbits(tile));
    td = RemoveFirstTrackdir(&bits);
    ...
    if (free_tile || (...)) ClearPathReservation(consist, tile, td);   // :15210
```
- 判据只有「**这一格上有没有预留`GetReservedTrackbits(tile)`**」，**没有**「这个预留是不是 `consist` 订的」
  （引擎里本可用 `GetTrainForReservation(tile, track)` 比对，`pbs.cpp` 里到处都是）。
- 唯一的归属检查在**起始格**（`:15155`「Don't free reservation if it's not ours」，语义是"起始格该轨已被别人订 ⇒ 直接放弃"），
  **一旦起步成功，后面每一格都不再校验**。
- ⇒ 只要释放者自己的 `consist->path` 在某一格中断（预留已被别处清掉/被 YAPF 放弃/被 R3R 截断），
  而该格**恰好有另一列车（等待车）的预留位**且 `CFollowTrackRail` 能沿同一条轨走过去，
  `:15210` 就会替那列车把位清掉 —— 清位者连"我在清别人的预留"都不知道，日志上也不会留痕（**完全符合"神秘"**）。
- 咽喉区正是"多支汇入/相邻格分属不同车"的地方，是这条路径唯一容易命中的地形。

**调用者清单（`FreeTrainTrackReservation(` 全库检索结果，只列会作用在"等待中列车"附近的）**：

| 锚点 | 上下文 | 对被撞车的影响 |
|---|---|---|
| `train_cmd.cpp:15104` | 定义；`:15115` 先 `R3RResvLedgerForget(consist->index,"free-path")` 整条忘记账本，`:15117` `lookahead.reset()` | — |
| `train_cmd.cpp:5073` | 预留重建前置清（"Clear path reservation in front if train is not stuck"，`:5072` 有三个 `no_*_unreserve` 开关） | 本人 |
| `train_cmd.cpp:16628` | `CPL-WAITCUT-CLEAR`（第 276 轮续三，KI-433）：等待区路点截断激活时按 `(veh, cur_real_order_index)` **清一次旧预留** | 本人（前方），**不解释"脚下"** |
| `train_cmd.cpp:16763 / 16828 / 16848 / 16900 / 16910` | `ChooseTrainTrack()` 各失败出口：`FreeTrainTrackReservation(consist, origin.tile, origin.trackdir)` + 可选 `MarkTrainAsStuck` | 本人；**注意这些点每 tick 都可能触发**，是"预留铺→清→铺"抖车的来源 |
| `train_cmd.cpp:18717` | `TrainCrashed()` 里 `if (!Stuck) FreeTrainTrackReservation(this)` | 事故后 |
| `train_cmd.cpp:22437` | `DeleteVisibleTrain()` | 删车 |
| `tunnelbridge_cmd.cpp:557 / 1378 / 1512`、`station_cmd.cpp:1546`、`infrastructure.cpp:210`、`settings_table.cpp:586` | 拆轨/隧道桥/站台改动/共享基建/刹车模型切换 | 正确取 `GetTrainForReservation()` 后调，属正当释放 |

**结论（本轮定性）**：
1. 咽喉区"脚下预留被清"**不需要 R3R 的翻转**也能发生 —— 引擎本体的 `FreeTrainTrackReservation()`
   在 plain rail 上就是"谁在位就清谁"，任何一列车重建预留时都会扫到别人的位。R3R 只改 `direction` 不改位置
   的翻转（V-2/V-3）**额外**让释放起点朝错方向，是放大器而非必要条件。
2. 这也解释了玩家"追尾不只发生在站台"：站台分支（KI-477）是一条，plain rail 无归属释放是另一条，两者都要修。
3. 未验证：V-3 的具体提交点顺序（R3R 翻转/解挂/车库编辑里是否"先改 direction 再释放"）、
   V-4 的现场 actor 证据（日志里 `RESV-LEDGER-FORGET ... why=free-path` 的 `released=` 计数可作反推：
   若某车一次 `free-path` 释放的格数**远大于**它自己的 `path` 长度，就是吃到了别人的位）。

**下一条待验判据（V-5～V-7）**：
- V-5 `R3RResvLedgerForget()` 返回的/日志里的 `released` 是否能与 `consist->path` 长度比对（`pbs.cpp:251-` 的 release law）。
- V-6 是否需要给 `:15210` 前面加归属守卫（修法候选）——**已实现，见 §11.9.1（首版写错，已改）**。
- V-7 R3R 翻转提交点调用释放的顺序（`R3RReverseChainDirections` / `R3RFlipChainBySegments` / `R3RSyncChainAfterDepotEdit` 附近有无释放调用）。

### 11.9.1 自我证伪：第一版守卫用了 `GetTrainForReservation()`，这是我这一轮写下的**低级错误**（2026-10-25）

**错在哪（三处，一处比一处致命）**：

1. **顺序错**：我把守卫放在 `:15210` 的循环里，而 `FreeTrainTrackReservation()` 的**第一句**（`:15115`）就是
   `R3RResvLedgerForget(consist->index, "free-path")` —— 等循环跑到我的守卫时，**本链 path 位的账本行已经被撤空了**。
   于是"这个位是谁订的"对本链自己的位永远只能退回***几何回溯***去猜。
2. **谓词错（致命）**：`GetTrainForReservation()`（`pbs.cpp:465` 的 `R3RTrainForLedgerBit` 先问账本、再退
   `R3RFollowReservationToOwner()`）回答的是"**这条预留串的尽头站着谁**"，不是"**这一位是谁订的**"。咽喉区两列车的
   预留首尾相接（正是本 bug 的地形），它会把**本链自己的位**答成邻车 ⇒ `r3r_owned=false` ⇒ **本链的位永远放不掉**。
   我等于把"吃掉邻车的位"换成了"自己的位永不归还"，而后者正是 KI-345/346 花好几轮才清干净的孤儿位/自堵形态。
3. **§11.9 里的自相矛盾**：我在同一节既写"`:15115` 先整条忘记账本，所以这条误清在账本上留不下痕迹"，又拿账本做归属
   判据。而且"**整条**"这个词也错了——释放法 `pbs.cpp:267-274`（`R3RResvLedgerForgetMask`）把 `free-path` 掩到
   **PATH-only**，`consist` 位的行**不会**被忘（KI-362 的保护），所以更准确的说法是"本链 path 行被撤、consist 行还在"。

**改法（已落代码，仅 `src/train_cmd.cpp`）**：谓词换成**只问账本**的 `R3RResvLedgerOwner(tile, track)`（`pbs.h:101`，
账本口径 = 链头 index，见 `pbs.cpp:194-200` 的 `R3RResvLedgerActorHead()`）。判据
`owner == R3R_RESV_NO_OWNER || owner == consist->First()->index.base()` 才放行：
- 本链 path 位：行刚被 `:15115` 撤掉 ⇒ `NO_OWNER` ⇒ **照旧释放**（原行为逐字保留）；
- 邻车（咽喉区等待车）的位：行还在 ⇒ owner 是别人的链头 ⇒ **跳过**（这就是要堵的那一手）；
- 老档带入 / 引擎自写的无账位：`NO_OWNER` ⇒ 照旧释放（不制造新的泄漏面）。
探针 `RESV-FOREIGN-SKIP veh=… tile=… td=… track=… owner=%u`（边沿触发，同一 (清位者,格) 一条）。

**教训（写给自己）**：本函数是"先撤账、再逐格放"的结构，任何在循环里"问账本"的判据都必须先确认**那条账是不是刚被自己撤掉**；
同理，凡是要判"这一位归谁"，只能用账本，**不能用 `GetTrainForReservation()` 的几何回溯**（那一族的语义是"谁站在尽头"）。
此条同时补充 V-2：`:15141-15143` 的 `td` 仍取 `GetVehicleTrackdir()`（受 `direction` 影响），是否另需修正见 V-3。
