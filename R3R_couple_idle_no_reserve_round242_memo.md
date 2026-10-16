# R3R 第 242 轮：GOTO_COUPLE 机车停在普通轨道后永不重试预留（KI-348，探针轮）

- 日期：2026-10-11
- 轮次：242
- 状态：**只加只读探针 + 定性**（未改任何判据/行为，未碰 `src/*.h`）
- 关联：KI-347（第 241 轮，状态行不显示「等待空余轨道」/「正前往…」的口径）、KI-345/KI-346（孤儿预留，第 240 轮）
- 输入：玩家报告「列车停在原地不动、状态行不显示『等待空余轨道』」+ 现场日志 `build\R3R_debug.log`（3572 行）

---

## 一、本轮输入与诉求

玩家原话（本轮）：

> 嘶，如果你认为还要搜索一会儿，那么我认为性价比更高的选择是加探针。还有，记得写备忘。

⇒ 停止继续盲搜代码，改为**加只读探针取证**；并且必须产出一份临时分析报告（本文件）+ KI 清单条目（`R3R_KNOWN_ISSUES.md` 的 KI-348）。

现场：机车 `veh=23` 停在 `1112,125`，状态行不动、不显示等待轨道，日志尾部持续刷 `COUPLE-FAIL`。

---

## 二、现场取证（日志锚点，逐行原文）

`build\R3R_debug.log`（3572 行）；尾部约 133 行（3440~3572）是稳定刷屏段，**每约 19 行一组**，逐字重复：

```
CPL-PAIR act=23 tgt=22 dist=38 actTile=1112,125 tgtTile=1085,136
DEPOTBLK veh=23 tile=1112,125 track=02 eng=1 isdepot=0 ontile=0 idmatch=0 pend=0 stopped=0 parked=0 spd=0 destRaw=1 depotIdx=4294967295
COUPLE-FAIL loco=23 order=16 tx=1112 ty=125 x=17800 y=2007
LOCO-ORD ...（7 行）
COUPLE-WAIT-OFFDEST veh=23 tile=1112,125 mf=1112,128 real=6 isDepot=0 destDepot=0 destStn=1 destTile=287807 onDepotTile=0 onStnTile=0
RESERVECONSIST veh=22 tile=1085,136 track=0x1 fb=0 bits=0x1 ok=0 resNow=1（×8 节）
```

关键读数：

| 项 | 值 | 含义 |
|---|---|---|
| `DEPOT-ARR veh=23` | `spd=0 real=6(16) curType=16 stuck=0 tileDepot=0 tx=1112 ty=125 destTx=1087 destTy=140 destDepot=0 destResv=0 tileEqDest=0 depotDir=-1 enterDir=1` | 速度 0；`Stuck` 标志 **0**；当前订单 16 = `OT_GOTO_COUPLE`；目的地参考格 1087,140；车不在库内 |
| `DEPOTBLK veh=23` | `track=02 eng=1 isdepot=0 ontile=0 stopped=0 parked=0 spd=0 destRaw=1 depotIdx=4294967295` | 车在**普通轨道**（`track=02` = `TRACK_BIT_Y`），非库、非站台格；`stopped=0`（未被 `VehState::Stopped` 早退） |
| `COUPLE-WAIT-OFFDEST veh=23` | `destStn=1 destTile=287807 onDepotTile=0 onStnTile=0` | 订单目的地是**车站 1**；`dest_tile = 287807`（地图 2048 宽下 = 1087,140，与 `DEPOT-ARR` 的 `destTx/destTy` 一致）；`onStnTile=0` ⇒ 车**不在**该车站的站台上 ⇒ `R3RCoupleOrderDestinationReached()` 恒假 |
| `CPL-PAIR act=23 tgt=22` | `dist=38 actTile=1112,125 tgtTile=1085,136` | 配对表里已认到目标 `veh=22`（在 1085,136），但**相距 38px**，远未到 `TrainCoupleHandler` 的几何命中范围 |
| `TRP veh=23` | 全日志**最后一条在 2812**，`ok=1 res=1`（预留成功） | 2812 之后直到 3572 **没有任何** `TRP` / `TPR-WALK` / `TPR-BLOCKED` 行 ⇒ 该车**再未尝试过预留路径** |

排除项：

- **不是** `Stopped` 早退：尾部无 `SKIP-STOPPED veh=23`（`DEPOTBLK` 亦报 `stopped=0`）。
- **不是** 预留被拒：尾部**没有任何**预留尝试行（既无成功也无失败），问题出在「根本没去试」。
- **不是** 站台/库内特例：车在普通轨道（`track=02`），非库、非站台格。

---

## 三、代码路径逐段对照（`src/train_cmd.cpp`）

`TrainLocoHandler(Train *consist, bool mode)` 的骨架（行号为当前树）：

1. **停驶闸门**（约 17330）：`VehState::Stopped && cur_speed == 0` ⇒ `SKIP-STOPPED` + `return true`。
   → 现场 `stopped=0`，**不适用**。
2. **车库耦合块**（约 17419~17494）：`IsRailDepotTile(consist->tile)` 才进。
   → 现场 `isdepot=0`，**不适用**。
3. **`ProcessOrders(consist)`**（约 17784）：每 tick 都跑。`OT_GOTO_COUPLE` 只在**挂上**后才推进索引 ⇒ 未挂上时索引原地不动。
4. **订单调向**（约 17792）：`r3r_may_reverse` 为真 ⇒ `cur_speed = 0` + `ReverseTrainDirection()` + `return true`。**这是 `cur_speed` 被归零的候选点之一**，但日志尾部无 `REVERSEDIR veh=23`，暂排除。
5. **速度推进**（约 18072）：`j = consist->UpdateSpeed(max_speed_info);`
6. **停驶分支**（约 18084）：`if (j < adv_spd) { ... }`
   - `cur_speed == 0` 时，`UpdateSpeed` 返回 `j = 0`，而 `adv_spd >= 1` ⇒ **`j < adv_spd` 恒真**。
   - 该分支内对 `OT_GOTO_COUPLE` 只做：`CPL-S0-CHK` 探针 + `CheckTrainCollision(moving_front) || TrainCoupleHandler(consist)`。
   - **`TrainCoupleHandler()` 只做几何命中/候选搜索，不调用任何寻路或预留。**
7. **移动分支**（约 18108，`else`）：`consist->SetLastSpeed()` + `CheckNextTrainTile` 前的 `TrainCheckIfLineEnds()` + **`TrainController()`** —— 引擎真正的「预留路径 / 加速 / 判前方可通行」全部在**这里**。

⇒ 结论链：

> `cur_speed == 0` ⇒ 每 tick 都满足 `j < adv_spd` ⇒ 每 tick 只跑第 6 步 ⇒ **`TrainController` 永远不再执行** ⇒ **永远不会再尝试预留路径**。而 `Stuck` 标志（引擎用来表达「被卡住/等待空余轨道」的唯一状态位）**只在预留失败 `mark_as_stuck=true` 时被置位**（见 `R3RLogReserveAttempt` 上游与 `VehicleRailFlag::Stuck`），既然从没再试预留，`Stuck` 恒为 0。
>
> 这就是 KI-347 的另一半：**界面不显示「等待空余轨道」不是 GUI bug，而是引擎根本没有进入「等待」这个状态**——它自我认定为「正在正常前往目的地」（`Stuck==0`）。

剩下的唯一未知是：**是谁、在哪一 tick 把 `cur_speed` 归零的？** 这决定修法（是「预留成功但终点=原地」、是「r3r_may_reverse 兜圈」、还是「移动分支里刹车到 0 后再无出口」）。这正是本轮要加探针回答的问题。

---

## 四、根因推断（三个候选，待探针分辨）

- **候选 A（最可能）：最后一次预留（2812，`ok=1 res=1`）给出的路径终点就落在 1112,125 附近**，车走完/走到安全停止位后 `cur_speed` 自然降到 0；此后没有任何触发点让它重新预留 ⇒ 静止。判别：`SPD-DROP` 行之前紧邻的日志应是预留/行尾相关（`TRP`/`CRT`），而不是 `REVERSEDIR`。
- **候选 B：`r3r_may_reverse` 每 tick 掉头** ⇒ `cur_speed=0` + `return true`。但日志尾部无 `REVERSEDIR veh=23` 行可佐证，且 `REVERSEDIR` 探针是无条件 `fprintf`（不进 edge 闸门）⇒ 若发生必然留痕。倾向排除，但由 `SPD-DROP` 兜底。
- **候选 C：移动分支里 `TrainController` 因前方不可通行刹车到 0**（PBS 前有车 / 信号红），随后因上述死锁再无法评估。判别：`SPD-DROP` 前一 tick 应有 `LOCO-MOVE` 行且 `spd>0`。

---

## 五、本轮新增探针（只读，零行为变更）

全部落在 `src/train_cmd.cpp`，**不新增 `src/*.h` 任何字段**（避免 KI-183 全量重编；本轮也因此保持增量构建合法）。

### P1 `SPD-DROP`（新增边沿枚举 `R3REDGE_SPDDROP`）

- 落点：`TrainLocoHandler()` 入口（`R3RWatchOrderIndex(consist, "loco-entry")` 之后）。
- 机制：文件内静态 `std::unordered_map<uint32_t, uint16_t>`（key = `consist->index.base()`）记住上一 tick 的 `cur_speed`；当「上次非零、本次为零」时打一行并 `erase`，非零时写入。map 规模 = 运动中列车数，可忽略。
- 输出：`SPD-DROP veh= tile= prev= order= real= stuck= fp= prog= wait= parked= stopped= track= dtile=`
  - `prev` = 归零前最后一次观察到的速度；
  - `stuck/fp(force_proceed)/prog(progress)/wait(wait_counter)/parked/stopped/track/dtile(dest_tile)` = 归零瞬间的完整上下文。
- 用途：与上一行日志配对 ⇒ 直接指出**谁**把速度归零。

### P2 `LOCO-IDLE`（升级既有 `CPL-S0-CHK`）

- 落点：停驶分支内既有的 `CPL-S0-CHK` 探针处。
- 改动：保留原名与前缀（不破坏既有判据），**扩充字段**并把新增位并入 edge payload：
  `stuck` / `force_proceed` / `progress` / `wait_counter` / `parked` / `dest_tile`。
- 用途：确认停车稳态的**全部**状态位，尤其 `stuck==0`、`force_proceed==0`、`progress` 是否为 0。

### P3 `LOCO-MOVE`（新增边沿枚举 `R3REDGE_LOCOMOVE`）

- 落点：移动分支（约 18108 `else`）入口。
- 机制：`R3RDbgEdge(R3REDGE_LOCOMOVE, key=veh, payload=tile | spd<<32 | j<<48)`。
- 用途：**负判据的正面证据**——停车期间若完全不出现 `LOCO-MOVE veh=23`，即证明列车从未进入移动分支（也就从未调 `TrainController`/预留）。

### P4（不做，本轮刻意省略）

`TrainCoupleHandler` 的 `u == nullptr` 早退处的候选搜索输入（`GetCouplePosition` / 9 格扫描）已有 `COUPLE-FAIL` / `COUPLE-WAIT-OFFDEST` 覆盖，且它是**症状**不是根因；本轮不扩此面，避免日志进一步膨胀。

---

## 六、未确认项

- **U-1**：`cur_speed` 归零的确切提交点（候选 A/B/C 三选一）——由 P1 回答。
- **U-2**：`progress` 是否残留非零值（若是，`j` 可能不为 0，需重新审视「`j < adv_spd` 恒真」的推断）——由 P2 回答。
- **U-3**：2812 那次 `ok=1 res=1` 的预留终点到底落在哪一格（是否就是 1112,125）——需在 `TRP` 行补 `mf` 的 track/tile；本轮不扩。
- **U-4**：`dest_tile=1087,140`（车站 1）与配对目标 `veh=22 @1085,136` 的关系（是否同一车站两端），影响后续修法是否要「先驶到站台再命中」。
- **U-5**：`veh=22` 的 `RESERVECONSIST ... ok=0 resNow=1` 每 tick ×8 是否为噪声（自身预留重试）——与本题无关，仅登记。

---

## 七、复测判据

1. 复跑同场景（机车 23 停在 1112,125、GOTO_COUPLE 到车站 1）应出现**恰好一行** `SPD-DROP veh=23`，其 `prev>0`，且该行紧邻的上一行能唯一锁定归零调用点（候选 A：预留相关；候选 B：`REVERSEDIR`；候选 C：`LOCO-MOVE`）。
2. 停车期（`SPD-DROP` 之后直到日志结束）应出现 `LOCO-IDLE veh=23 ... stuck=0 fp=0`（至少一行），且**完全不出现** `LOCO-MOVE veh=23`。
3. 停车期不应出现 `TRP veh=23` / `TPR-WALK veh=23`（与现状一致，作为对照）。
4. 负判据：正常行驶的列车（例如 `veh=22`、其它在跑的机车）应正常交替出现 `LOCO-MOVE`，且**不应**出现无意义的 `SPD-DROP`（正常到站停车会各出现一次 `SPD-DROP`，属预期；判据是**数量级**与「停车后不再有 `LOCO-MOVE`」的形成对比）。
5. 负判据：健康场景（列车正常加速、正常挂车）行为逐字不变，`CPL-S0-CHK` 原有字段仍在（只多了新字段）。

---

## 八、构建自证

复用 `_tmp_inc_build.cmd`（**未新建任何 `.cmd`**），按既有约定后台启动：
`Start-Process cmd.exe -ArgumentList '/c','_tmp_inc_build.cmd' -WorkingDirectory 'd:\sourcecode of JGRPP' -WindowStyle Minimized`。
构建前 `tasklist /FI "IMAGENAME eq openttd.exe"` = 无匹配（未占用链接）。

- **护栏**：`build\R3R_incbuild.guard.log` = `GUARD: incremental is safe (no header/lang file is newer than the newest object)` ⇒ 本轮只改 `.cpp`，增量合法。
- **日志尾部**：`[1/5]` FindVersion/rev.cpp 重生成（always-run 边，正常）→ `[2/3] Building CXX object ...\src\train_cmd.cpp.obj` → `[3/3] Linking CXX executable openttd.exe`，无 `error C*` / `fatal error` / `FAILED:` / `build stopped`。
- **`build\R3R_incbuild.done`**：`EXIT_CODE=0`（2026-10-11 01:41:22）。
- **时间戳链**：`src\train_cmd.cpp` @ **01:38:11** → `build\CMakeFiles\openttd_lib.dir\src\train_cmd.cpp.obj` @ **01:39:17** → `build\openttd.exe` @ **01:41:13**（51 740 672 B）⇒ obj 晚于源码、exe 晚于 obj。
- **`read_lints`**：`src\train_cmd.cpp` = **0 条**。
- **exe 字面量核对**：`findstr` 命中 `SPD-DROP veh=`、`LOCO-MOVE veh=`、`stuck=%d fp=%d prog=%d`（3/3）⇒ 三条探针确已进二进制（注意：C++ 代码字面量可检索；语言串不可，本轮无语言串改动）。

结论：**探针已实现 + 已编译，游戏内复测待做**（判据见 §七）。

---

## 九、给玩家的取数指引（复跑时只看这几行）

1. 打开 `build\R3R_debug.log`（或让探针输出目录里的日志），搜 `SPD-DROP veh=23` —— 期望**恰好一行**，看它**上一行**是什么：这一对就是「谁把 23 号机车停下的」的直接答案。
2. 搜 `LOCO-MOVE veh=23` —— 期望**只在停车之前出现过**，`SPD-DROP` 之后**一次都不再有**（这就是"永远不再尝试预留"的正面证据）。
3. 搜 `CPL-S0-CHK veh=23` —— 期望至少一行且 `stuck=0 fp=0`；若 `prog` 非 0 请一并回报（会推翻本报告 §三 的部分推断，见 U-2）。
4. 顺手搜 `TRP veh=23` / `TPR-WALK veh=23` 的最后一行时间位置，应与 `SPD-DROP` 相邻（对照现状：当前日志最后一条在 2812）。
5. 负判据：没事的车（例如 22 号）应正常出现 `LOCO-MOVE`；正常到站停车各出现一次 `SPD-DROP` —— 若**所有**列车都刷 `SPD-DROP`，说明检测逻辑写反了，请立刻反馈。

---

## 十、第 243 轮：按「新触发时机」（有疑点 / 猜测的根因即刻落盘）复盘

> 起因：用户 2026-10-11 要求把「**有疑点**」「**猜测的根因**」列为**写入临时备忘的触发时机**（不等取证结束、不等结论确定）。本节即按该规则**先落盘、再继续**。

### 10.1 本轮新增现场证据（`build\R3R_debug.log`，**5651 行**，第 242 轮 exe 产出，含 `SPD-DROP`）

| 探针 | 命中数 | 关键读数（原文） |
| --- | --- | --- |
| `SPD-DROP veh=23` | 9 | 第 **4239** 行：`SPD-DROP veh=23 tile=1112,125 prev=27 order=16 real=6 stuck=0 fp=0 prog=255 wait=0 parked=0 stop=0 track=02 dtile=287807` |
| `LOCO-MOVE veh=23` | **78** | 末 8 行全同形：`LOCO-MOVE veh=23 tile=1112,125 j=256 spd=2 track=02 stuck=0`（4300/4320/4340/4611/4782/5092/5267/5530） |
| `CPL-S0-CHK veh=23` | 269 | 停驶分支持续刷 |
| `TRP veh=23` | 23 | 末条 = 第 **3392** 行 `TRP veh=23 mf=1092,86 real=6 type=16 dest=287807 spd=0 fp=0 look=1 one=0 mstuck=0 fto=0 => ok=1 res=1` |
| `REVERSEDIR veh=23` | **0** | 无掉头 |
| 日志尾部 | — | `CRT veh=11 order=6 dir=5 origin=1096,139 td=0 found=1` / `CRT veh=2 ... 1092,139 td=8 found=1` 交替 |

### 10.2 疑点（D 系列）

- **D-1（方法论，重要）**：KI-348 的核心论据「`TRP` 末条在 2812，此后 760 行无任何预留尝试 ⇒ 再未尝试预留」**不成立**。`src\train_cmd.cpp:14017` 显示 `TRP` 打印被边沿闸门压住：
  `if (!one_shot && !R3RDbgEdge(R3REDGE_TRP, (uint64_t)consist->index.base(), payload)) return;`
  `payload = res | order_type<<8 | dest_tile<<16 | (cur_speed!=0)<<48` ⇒ **只要这四项不变就不再打印**；veh=23 停驻状态恒定（order=16 / dest=287807 / res 不变）⇒ 天然被抑制。**「日志没有」≠「没发生」**。
- **D-2**：KI-348 断言「`cur_speed == 0` ⇒ 恒走停驶分支」被实测反例推翻：`LOCO-MOVE veh=23 … spd=2 j=256`（78 行，含末段 5530）。`j = UpdateSpeed()` 返回 256，而 `adv_spd = GetAdvanceDistance()`（`:18116-18117`）⇒ `j >= adv_spd` ⇒ **走的是移动分支**。
- **D-3**：基准不一致。KI-348 引的是旧日志（3572 行、"TRP 末条 2812"），本份 5651 行、含第 242 轮新探针 ⇒ TRP 末条实为 **3392**。
- **D-4**：`CPL-S0-CHK`（269，停驶分支）与 `LOCO-MOVE`（78，移动分支）**交替出现**，与「永久停在停驶分支」互斥。
- **D-5**：`stuck=0` 恒真但车确实原地踏步 ⇒ 「Stuck=0 = 状态显示错」需重审：引擎设计上「红灯等待」本就不置 Stuck。
- **D-6（待补）**：3392 之后预留尝试是否仍发生，现有探针无法判定（被闸门压住）⇒ 需一次无抑制计数才能定论。

### 10.3 猜测的根因（H 系列）

- **H-1（证据已很强、待玩家侧确认）**：veh=23 被**信号红灯**挡在 `1112,125`。
  - `progress == 255` 是红灯分支的**专属标记**：全文件仅两处赋值（`:16094` / `:16099`，均在 `HasSignalOnTrackdir(...)` 判红 `if` 内，注释 "…so speed stays 0"）；9 条 `SPD-DROP veh=23` 中**只有 4239 那条 `prog=255`**，其余为 10/6/20/2（普通减速/到站）。
  - 4239 那条 `wait=0`，而红灯分支紧随 `if (!_settings_game.pf.reverse_at_signals || ++first->wait_counter < …) return false;`（`:16095`/`:16100`）⇒ `wait` 恒 0 ⇔ **`reverse_at_signals == false`**（`++wait_counter` 被短路）⇒ **永不自动掉头、永不放弃**。
  - `REVERSEDIR veh=23` = 0 行 ⇒ 无掉头。
  - 待验证：① `1112,125` 前方格确有红灯；② 设置里 `reverse_at_signals == false`。
- **H-2（未验证）**：红灯永不转绿 = 前方格被长期占用/预留冲突（疑与尾部 `veh=2`@1092,139、`veh=11`@1096,139 的 `CRT … found=1` 空转、`RESV-AUDIT-STRAY 1093,87..1112,106`（KI-346 判据①仍未过）同源）。待验证：取前方格占用者/预留 owner。
- **H-3（未验证）**：`progress == 255` 使 `j` 恰好达到 `adv_spd`（读数 256）⇒ 车每 2~3 tick **正好走一格**，随即再撞同一红灯被清零 ⇒ 宏观静止、微观抖动（解释两分支交替）。

### 10.4 对 KI-348 的更正

KI-348 的「**结构性根因**：`cur_speed == 0` ⇒ 恒走停驶分支 ⇒ `TrainController()` 永不执行 ⇒ 永不重试预留」**已被 D-1/D-2 证伪**，整体改写为 H-1 口径：

> **新结论（待验证）**：机车**每 tick 都在跑移动分支 / `TrainController()`**，但因前方**红灯**（`progress=255`）被反复清零而原地踏步；`Stuck` 不置位属引擎设计；永久钉死的真正机制是 **`reverse_at_signals == false` 让红灯分支永不放弃**，而非"从不尝试预留"。

受影响：原判据 ②（"停车期完全不出现 `LOCO-MOVE veh=23`"）**已被现场反证**（78 行存在）⇒ 作废；③（"不应出现 TRP"）因闸门抑制不可用 ⇒ 作废。

### 10.5 下一步最小取证（不扩大读码面）

1. 取 `1112,125` **前方一格**的信号 / 占用 / 预留 owner（一条只读 dump 或截图即可）。
2. 若确认「红灯 + `reverse_at_signals=false`」成立：修法方向 = 给 GOTO_COUPLE 的等待者在"红灯前停够 N tick"后提供 R3R 侧兜底（自行掉头 / 跳过 / 重选配对），**不动引擎全局红灯语义**。
3. 若要判定 D-6，需加一条**不受闸门**的预留尝试计数（下轮再做）。

### 10.6 第 243 轮续：H-2 取证结果（新增疑点 D-7 / D-8）

**取证（同上日志）**：
- 撞灯前最后运动状态（第 4226-4227 行，与 4239 的 SPD-DROP 相隔 12 行）：
  `4226 CPL-PAIR act=23 tgt=22 dist=39 actTile=1112,124 tgtTile=1085,136`
  `4227 LOCO-MOVE veh=23 tile=1112,124 j=194 spd=112 track=02 stuck=0`
  ⇒ veh=23 以 **spd=112 高速**通过 `1112,124`，在下一格 `1112,125` 急停（4239 行 `prev=27 prog=255`）。**典型"红灯急停"形态**（先减到 27 再被清零）。
- **正前方格 `1112,126` 在全日志零出现**（`1112,124|1112,126` 只命中 4226/4227，且都是 124）⇒ 现有探针**完全没有覆盖** veh=23 正前方那一格 ⇒ H-2 **无法从日志判定**。

- **D-7（新疑点）**：`RESV-AUDIT` 在全日志**只出现 1 组**（第 183-190 行，日志开头、`age=0`），内容仍是 KI-346 那条 20 格走廊：
  `RESV-AUDIT-STRAY … tile=1106,100 track=4 bits=0x10 age=0` … `tile=1112,106 track=4 bits=0x10 age=0`
  `RESV-AUDIT-SUMMARY tick reservedTiles=58 owned=38 strayTiles=20 reaped=0 pendingStray=20`
  ⇒ ①KI-346 判据①（该走廊应消失）**仍未通过**；②但它是**读档带入的存量**（位于日志开头、age=0），而 KI-346 修的是"不再新产生"⇒ 判据①表述应修正为「不再新产生 + 存量由 GC / `r3r_resv_purge` 清掉」。
- **D-8（新疑点 · 纯猜测未取证）**：孤儿走廊末端 `1112,106` 与 veh=23 所在 `1112,125` **同在 x=1112 的纵向线上**（相差 19 格），而 veh=23 的 `track=02` 正是该线方向 ⇒ 怀疑这条线（x=1112）上的双槽格存在**系统性错误预留**，veh=23 正前方格可能有同类残留（本次审计只列了 y≤106 那一段，未覆盖 y=120~130）。
  - 待验证判据：对 `1112,120..1112,130` 跑一次预留扫描（或 `r3r_resv_purge` 预览），看是否有 `track=4` 孤儿位。
- **H-2 判定状态 = 未定论**。要往前走只有两条路：①玩家给 `1112,125` 前方的截图（看信号灯颜色 + 是否有车停着）；②加一条**只读**探针打印该格的信号状态与预留 owner（需改码 + 增量构建）。

### 10.7 玩家现场拓扑（2026-10-11，本轮关键新证据）

玩家原话：「1112,125~129 的横向轨道，列车横卧在 125-128（头靠 128），129 有一格红的单向路径信号机，但黑色预留一直往前延申，126 是一个红的区间信号灯，从 125 开始一直往前都有黑色的显示」。

| 事实 | 对应读数 | 意义 |
| --- | --- | --- |
| 车占 `1112,125~128`，**车头在 128** | 探针 `tile=1112,125`（自身格） | 车确实停在此段 |
| `129` = 红的**单向路径信号机** | `prog=255` + `wait=0`（红灯分支**专属**标记） | **H-1 获目视独立佐证**：确实停在红灯前 |
| 125 起黑色预留**一路往前延伸** | `reservedTiles=58 owned=38 strayTiles=20` | 前方格**确实被预留**（不是"没预留"）⇒ 与 KI-348「再未尝试预留」正面冲突 |
| `126` = 红的区间信号灯 | 车自身占格 | 占格信号对后方显红属正常，非阻塞源 |

### 10.8 新疑点（D-9 ~ D-11）

- **D-9（口径冲突 · 待澄清）**：玩家称 125~129 为"**横向**轨道"，而探针全程 `track=02`（TRACK_BIT_Y = 南北向），且车占 125→128 明显沿 **y** 排列。**不影响红灯结论**，但需玩家确认这串轨道在屏幕上水平还是垂直（决定"前方是哪一格"）。
- **D-10（关键）**：129 之后的预留**有主**（`strayTiles=20` 那条走廊的 y≤106，不含 y≥129），但车过不去 ⇒ **有主预留 + 信号红 = 疑似死锁**。与 KI-346 的"孤儿走廊"（1093,87..1112,106，另端同在 x=1112 线上）是**两个不同问题**，不可混为一谈。
- **D-11**：若前方格是 **veh=23 自己**预留的，按 PBS 语义信号机应放行"已预约路径通过信号机"；仍显红只有两种可能：①前方预留**不属于** veh=23；②该信号机是**单向**且方向朝向来车（永远拒绝）。二者修法完全不同，**必须取证区分**。

### 10.9 新增/更新猜测根因

- **H-1（状态升级：有目视佐证，基本坐实）**：veh=23 停在 `1112,129` 的红灯前，被引擎"红灯前原地踏步"语义反复清零。
- **H-4（猜测根因 · 未验证）**：前方 (1112,129+) 的黑色预留**属于另一列车**，而该车自己也不前进（疑为 `veh=2`@1092,139 / `veh=11`@1096,139 的 `CRT … found=1` 空转，或 `veh=22`@1085,136）⇒ **互相等待死锁**；`reverse_at_signals=false` 让 veh=23 永不放弃。判据：取 1112,129 及更前方的预留 owner。
- **H-5（猜测根因 · 未验证）**：129 的单向路径信号机方向与来车相反，列车被永久拒绝（与预留无关）。判据：玩家看清红灯朝向 / 该格信号探针。

### 10.10 第 243 轮续二：H-1 **完全坐实**（定位到代码分支）+ H-4 **证伪**

**取证 A（x=1112 线上有无其它列车）**：全日志除 veh=23 自己外，x=1112 只出现 1 行 `RESV-AUDIT-STRAY tile=1112,106 track=4`（孤儿预留，非列车）⇒ **前方 129 及更远处没有任何列车** ⇒ **H-4 证伪**。

**取证 B（`CPL-PAIR` 逐格轨迹）**：
`3924: 1112,106 dist=57` → `4062: 1112,123 dist=40` → `4226: 1112,124 dist=39` → `4228: 1112,125 dist=38`，此后 4257/4278/4298/4609/4780/5090/5265/5528 **全部停在 `1112,125 dist=38`**。
- ⇒ veh=23 是沿 **x=1112 的 y 轴**由北向南行驶（y 递增），`track=02`(TRACK_BIT_Y) **完全正确**；**D-9 归因 = 玩家按屏幕视觉称"横向"，引擎坐标是 y 轴，无矛盾**（也印证列车 125→128 是沿 y 排列）。
- ⇒ `CPL-PAIR` 的 `actTile` = 链头格 = **1112,125** ⇒ **车头朝 129 侧**（玩家说的"头靠 128"应是车尾端）。

**取证 C（红灯分支代码 `src\train_cmd.cpp:16074-16116`）——H-1 定位到具体分支**：
- `:16087` `IsNoEntrySignal(...)` ⇒ `goto reverse_train_direction`（会掉头）；现场 `REVERSEDIR veh=23` = **0** ⇒ **不是 NoEntry 信号**。
- `:16091` `if (!HasSignalOnTrackdir(gp.new_tile, ReverseTrackdir(i)))` = 该格**只在一侧有信号 = 单向信号** ⇒ `:16092-16094` `cur_speed=0; subspeed=0; progress=255` ⇒ `:16095` `if (!_settings_game.pf.reverse_at_signals || ++first->wait_counter < _settings_game.pf.wait_oneway_signal * DAY_TICKS * 2) return false;`
- ⇒ 与玩家目视「**129 有一格红的单向路径信号机**」**逐字吻合**，并解释了 `wait=0` 恒不变（`!reverse_at_signals` 短路 ⇒ `++wait_counter` 永不执行）。
- ⇒ **H-1 完全坐实**：veh=23 停在单向信号前，被 `:16092-16094` 反复清零、被 `:16095` 因 `reverse_at_signals == false` 立刻返回 ⇒ **永久原地踏步、永不掉头、永不放弃**。这是「引擎等待语义 + 一个不该长期为红的单向信号」共同作用，**不是"从未尝试预留"**。

### 10.11 新猜测根因

- **H-6（猜测根因 · 未验证 · 当前最可能的真凶）**：`1112,129` 前方 block **被预留**（现场已证无车）⇒ 单向信号永远红。候选来源：①**孤儿预留**（KI-345/KI-346 家族；`RESV-AUDIT` 只在日志开头跑过一组，y≥120 区间**从未被审计**）；②某列车预订该 block 后离开却未释放（`reservedTiles=58 owned=38`）。
  - **待验证判据（唯一有效）**：打印 `1112,129` 之后第一格的 `GetReservedTrackbits` 与预留 owner（需加只读探针），或由玩家在游戏内查该格。
- **H-5（单向信号方向设反）**：**仍未排除**，但优先级低于 H-6；判据 = 玩家看清 129 红灯朝向 / 该格信号属性。

### 10.12 第 243 轮续三：全日志逐行复核（**推翻了 §10.10 的两处措辞**）

**取证 D（`LOCO-MOVE` 全程在场 ⇒ 移动分支一直在跑）**：
- `LOCO-MOVE veh=23 tile=1112,125 j=256 spd=2 track=02 stuck=0` 在停车后**反复出现**（4259/4280/4300/4320/4340/4611/4782/… 共 8+ 次），且 `spd=2`、`j=256`。
- ⇒ `TrainLocoHandler` 的**移动分支（`j >= adv_spd`，约 :18108）每 tick 都在执行**，`TrainController` → `:16074` 红灯分支**真的被走到** ⇒ 第 242 轮 KI-348 的「恒走停驶分支、从不预留/从不进移动分支」**彻底作废**（与 §10.10 的 H-1 定位不冲突，反而是它的前提）。
- ⇒ 同时证明 `cur_speed` 不是恒 0：每 tick 被 `:16092-16094` 清零后，下一 tick 的 `UpdateSpeed()` 又把它抬到 2，再被打回 ⇒ **`wait=0` + `spd=2` 的"原地踏步"稳态**。

**取证 E（`SPD-DROP` 直接锁定分支）**：
`4239:SPD-DROP veh=23 tile=1112,125 prev=27 order=16 real=6 stuck=0 fp=0 prog=255 wait=0 parked=0 stop=0 track=02 dtile=287807`
- `prog=255` = 正是 `:16094`（单向）或 `:16099`（双向）写的哨兵值 ⇒ **红灯分支的"原地踏步"被逐字证实**。
- `fp=0` = `force_proceed == TFP_NONE` ⇒ 没有"强制通过"介入。
- `wait=0` 且全程恒 0 ⇒ `:16095` 的 `!reverse_at_signals` 短路成立（`++first->wait_counter` 永不执行）。

**取证 F（`CRT` 的出处，用于排除"另一个寻路器在转圈"的误读）**：
- `CRT` 唯一产生点是 `src\pathfinder\yapf\yapf_rail.cpp:1327-1334`，即在 `ChooseRailTrack()`（`stChooseRailTrack` 内层）`Yapf().FindPath()` 之后无条件 fprintf。
- ⇒ 日志尾部被 **veh=2（origin=1092,139 td=8）与 veh=11（origin=1096,139 td=0）** 的 `CRT … found=1` 交替刷屏，**不是** veh=23 的活动；且两者 `found=1`（找到路径）⇒ **不是死锁、不是找路失败**，只是"每帧重算一次"的正常代价。
- ⇒ 这**削弱**了 §10.9 的 H-4 表述（"veh=2/veh=11 疑为互相等待"）：CRT found=1 说明它们各自都有路，刷屏只是没有缓存/节流，**不应再作为阻塞源候选**。

**取证 G（`DEPOTBLK` / `CPL-PAIR` 的几何收口）**：
- `DEPOTBLK veh=23 tile=1112,125 track=02 eng=1 isdepot=0 ontile=0 idmatch=0 pend=0 stopped=0 parked=0 spd=0 …` ⇒ 车在**普通轨道**、非库、非停放、`Stuck==0`。
- `COUPLE-WAIT-OFFDEST veh=23 tile=1112,125 mf=1112,128 real=6 isDepot=0 destDepot=0 destStn=1 destTile=287807 onDepotTile=0 onStnTile=0`
  ⇒ `destTile=287807` 解码（`MapLogX=11`）为 **1087,140**（车站 1），与配对目标 `veh=22 @1085,136` 的空间关系 = 曼哈顿距离 38（= 日志里的 `dist=38`，即 `dist` 是曼哈顿距离）。
- ⇒ 列车**朝向与目标一致**（继续沿 y+ 才可能绕到 1087,140），**不存在"该掉头却没掉头"**；玩家"头靠 128"与 `mf=1112,128` **完全一致**（§10.10 取证 B 说"头靠 128 应是车尾端"**是错的**，探针 `mf` 就是 moving front = 车头所在格 = 128）。

**结论（本轮更新）**：
- H-1 **仍然坐实**，分支定位**从"扣分项"升级为"逐字命中"**（`prog=255` + `fp=0` + `wait=0` 三读数同时落在 `:16091-16095`）。
- §10.10 的两处措辞**作废**：①"mover 分支从不执行"（实际每 tick 执行）；②"头靠 128 是车尾端"（实际 128 就是车头）。
- **H-6 依旧是最可能的真凶且仍未验证**：红灯 = 129 格未被预约（PBS 语义下路径信号在"其后方已订"时才绿）或该 block 被他人订住。唯一有效判据不变 = **打印 129 及其后一格的 `GetReservedTrackbits` + 预留 owner**。

### 10.13 本轮探针落地（只读，`src\train_cmd.cpp`）

在 `:16075` 进入红灯分支后插入一次性（`R3RDbgEdge` 去重）只读 dump，标签 `REDSIG`，落点选在 `:16077`（`Trackdir i = FindFirstTrackdir(trackdirbits);`）之后：
- 主行：`veh/old/new/enterdir/chosen_track/i` + `ts.trackdirs/ts.signals/reachable_trackdirs/red_signals`。
- 信号行：`new_tile` 的 `GetPresentSignals/GetSignalStates` + 逐 track 的 `GetSignalType/IsOnewaySignal`。
- 预留行：`new_tile` 的 `GetReservedTrackbits`，以及**沿实际出口方向 `TrackdirToExitdir(i)` 的下一格**（= 129 之后列车真正要开进去的那一格，与引擎 `:16101-16102` 的出口口径一致）的 `GetReservedTrackbits` 与**逐 track 的 `GetTrainForReservation` / `GetR3RReservationOwnerNearby` owner**。
- 车辆行：`new_tile` 与下一格上是否有 `VehicleType::Train`（`HasVehicleOnTile` 只读）。
- 节流：边界枚举新增 `R3REDGE_REDSIG`（插在 `R3REDGE_LOCOMOVE` 之后、`R3REDGE_COUNT` 之前），key = `(veh << 32) ^ new_tile.base()`，payload = `(new_resv << 32) ^ beyond_resv` ⇒ 同一 (车,格) 只在**预留图景变化**时再打一行，不刷屏。
⇒ 判据：owner=`veh=2/11/22` ⇒ H-6 的"他人订住"成立；owner=`none` 且 bits 非 0 ⇒ **孤儿预留**（KI-345/346 家族，且证明 y≥120 区间确实漏审）；bits=0 ⇒ 退回 **H-5（单向信号方向/属性问题）**。

### 10.14 第 243 轮续三：构建自证（2026-10-11）

- 复用既有 `_tmp_inc_build.cmd`（未新建任何 .cmd）。
- 首跑因 `openttd.exe`（PID 14512）占用被脚本护栏拦下：`build\R3R_incbuild.done = EXIT_CODE=97`、exe mtime 停在 01:41:13（未链接）。
- 玩家退出游戏后重跑：`build\R3R_incbuild.guard.log` = `GUARD: incremental is safe (no header/lang file is newer than the newest object)`。
- 日志尾：`[2/3] Building CXX object CMakeFiles\openttd_lib.dir\src\train_cmd.cpp.obj` → `[3/3] Linking CXX executable openttd.exe`（Linking 命中 1 次）。
- `build\R3R_incbuild.done = EXIT_CODE=0`（2026-10-11 02:21:59）；`error C* / fatal error / FAILED: / build stopped` 计数 = **0**。
- 时间戳链：`src\train_cmd.cpp` @02:17:42 → `train_cmd.cpp.obj` @02:19:43 → `build\openttd.exe` @02:21:41（**51 772 416 B**，晚于全部源码）。
- `read_lints` 该文件 **0 条**。
- exe 字面量自证 **6/6 命中**：`REDSIG veh=`、`REDSIG   tdbits=`、`REDSIG   new present=`、`REDSIG     track=`、`REDSIG   beyond=`、`REDSIG     beyond track=`。
- 状态 = **已实现 + 已编译**，游戏内复测待做。
- 复测判据：①出现 `REDSIG veh=23` 行；②其 `beyond=1112,129 … resv=` 非 0 时逐 track `owner=`/`near=` 给出真凶车号（⇒ H-6 成立）；③`resv=0` 且 `new present/states` 正常 ⇒ 退回 H-5（单向信号属性/朝向）；④同一 (车,格) 不刷屏（`R3REDGE_REDSIG` 去重生效）；⑤负判据：其它列车正常通过红灯段时行为逐字不变（探针在 `if (R3RDbgOn())` 内、纯只读）。
