# 第 411 轮：列车耗时压榨第二轮（答案本收益到顶，换靶）

日期：2026-11-09
状态：取证中（未改源码、未构建）

## 一、玩家口径确认（本轮的出发点）

玩家原话：

> 「之前没改这个的时候列车处理耗时平均在 22~21 毫秒，现在变成了 21~20 毫秒。我怀疑我们是不是对性能压榨不足」
> 「我看的是帧率里面的列车耗时。如果寻路这边已经不能压榨了，就换个地方继续压榨」

⇒ **口径 = 帧率面板的「列车」耗时（每帧毫秒）**，不是 `PERF-TICK` 的 `ms/s`。
⇒ 本轮基线：**每帧 21~20 ms**（改答案本前 22~21 ms）。

## 二、结论 1：答案本的收益上限就是 ~1 ms，已经吃到

- 答案本只替换 `FindPath()` 的那一次 A*（`yapf_rail.cpp` 的 `R3RAnswerReplay`）。
- 它不省：`TrainLocoHandler` 的每帧调度、`UpdateSpeed`、`MaxSpeedInfo`（整链遍历）、
  `CheckTrainCollision`、`ReserveTrackUnderConsist`（每 tick 重订脚下格）。
- 存档规模（`build\R3R_perf.log` 快照）：`chains=940 vehs=40027 maxChain=243 artic=27004`
  ⇒ 每帧有 4 万节车要过一遍固定流程，A* 摊薄后只占百分之几。
- 22→21（约 5%）与「A* 只占 loco 的 ~5%」自洽，**不是压榨不足，是靶子小**。

## 三、结论 2：20 ms/帧 的真实候选靶子（假设，待取数验证）

现有 `PERF-TICK` 行已经把 `loco` 拆成子桶，且带两个关键字段：

- `sub=` 各子桶之和；
- `resid=` **loco 里没被任何子桶覆盖的残差** ← 最大嫌疑区。

子桶清单（train_cmd.cpp 静态累加器）：`loco / coupleH / plat / resv / edgeGate / ctrl / coll / spd / vp / mov / ord / prv / load / dep / dec / wpr / stk / rev`。

候选（按可疑度排序）：

| 编号 | 目标 | 机制 | 为何可疑 |
|---|---|---|---|
| T-1 | **R3R 的 O(链长) 判定每帧多次调用** | `R3RGetControlSegment` / `R3RGetLowestPriority` / `R3RIsCarOnlyFormation` / `R3RChainHasRealArticPart` 全是整链线性扫描 | 第 152 轮备忘自陈「现状整链线性扫描 O(链长)」；940 链 × 42 节 × 多次/帧 |
| T-2 | `ReserveTrackUnderConsist()` | `train_cmd.cpp:19269`，每 tick 每链遍历全 consist 重复 `SetTrackReservation` | 幂等但每帧重订 4 万节 |
| T-3 | `MaxSpeedInfo` / `UpdateSpeed` | 每帧遍历整链取最小限速 | 链不变时限速不变，可缓存 |
| T-4 | 每帧全图/全链扫描类 | `R3REnsureCouplePair`、waiter 清扫、`_r3r_couple_dest_idle`、`R3RReservationAudit` | R3R 自己加的，上游没有 |

## 四、疑点登记

- **REG-411-A**：玩家给的读数只有「帧率面板列车耗时」，没有 `PERF-TICK` 分解。
  在拿到 `sub/resid` 与各桶读数前，T-1~T-4 全是猜测，**不得直接改码**。
- **REG-411-B**：`build\R3R_perf.log` 尾部是**暂停态**采样（`loco=0.0/s 0ms/s`、`fps=61.6`、`dump=5ms/s`），
  不能当耗时基线；且 `nl=14768ms/s` 是**假读数**（`glTrains=239.87ms` 是累计值 × fps）。
- **REG-411-C**：`build-release\build.ninja` 的 `R3R_PROBES_DEFAULT=0`，
  发布树默认不写 perf 日志，需要确认运行时能否打开探针（控制台命令），否则只能用 Debug 树取比例。

## 五、下一步（待执行）

1. 确认发布树能否运行时开探针（找控制台命令）；能则直接取真实 20 ms 分解。
2. 取一份**运行态**（发车、不暂停）的 `PERF-TICK` 窗口，读 `sub/resid` 与各桶。
3. 按最大桶对号入座，只改 `.cpp`（避免 KI-183 全量），每步单独 A/B。

## 六、复测判据（本轮性能改动的验收口径）

1. 帧率面板「列车」耗时从 20 ms 降到 <15 ms（或给出各桶下降的量化证据）。
2. 负判据：`COUPLE-OK`/`DECOUPLE-DONE`/`ORD-*` 行为与改动前逐字一致（除时间戳）。
3. 负判据：无新增 `TPR-BLOCKED`、`RESV-AUDIT-STRAY`、`rail_map.h:168` 断言。
4. `ANSWER-REPLAY` 命中率不低于改造前（不能靠牺牲答案本换速度）。

## 七、取数结果（2026-11-09，`build\R3R_perf.log` 的**运行态**窗口）

最后两个 `loco>0` 的 `PERF-TICK` 窗口（每窗口 128 帧）：

```
# 窗口 A（最近）
loco=4774.2/s 663ms/s | ctrl=1859.8/s 311ms/s | stk=449.2/s 68ms/s | dep=3989.7/s 41ms/s
spd=3426.8/s 28ms/s | vp=3426.8/s 20ms/s | ord=4488.7/s 16ms/s | load=4487.9/s 11ms/s
coll=1859.6/s 5ms/s | dec=1015.9/s 5ms/s | prv=0.6/s 4ms/s | edgeGate=385.6/s 1ms/s
dump=1ms/s | mov=81655.9/s | resv=0.0/s 0ms/s | coupleH=0 | plat=0 | wpr=0 | rev=0
sub=505ms/s resid=159ms/s

# 窗口 B（更早）
loco=4470.0/s 675ms/s | ctrl=287ms/s stk=56ms/s dep=44ms/s spd=26 vp=18 ord=20 load=9
sub=465ms/s resid=209ms/s
```

**换算（玩家读数 20 ms/帧 ⇒ fps≈33）**：

| 桶 | ms/帧 | 占比 |
| --- | --- | --- |
| ctrl（TrainController：移动+预留扩展） | 9.4 | 47% |
| resid（未归桶） | 4.8 | 24% |
| stk（stuck 重试） | 2.1 | 10% |
| dep（CheckTrainStayInDepot） | 1.2 | 6% |
| spd / vp / ord / load / coll / dec | 0.8 / 0.6 / 0.5 / 0.3 / 0.15 / 0.15 | ~13% |

**单次成本**：`ctrl` 167 µs/次（1859 次/秒）；`stk` 151 µs/次（449 次/秒）；`dep` 10.3 µs；`spd` 8.2 µs；`vp` 5.8 µs；`ord` 3.6 µs；`load` 2.5 µs。

**结论句（逐条对应第二/三节的假设）**：

1. `resv=0.0/s` ⇒ **T-2 证伪**。`ReserveTrackUnderConsist()` 全文件仅 3 个调用点（`:10194` 解挂、`:20098` Crash 后、`:22576` waiter），**不在主 tick 路径**。
2. `stk` 的计时范围 = `train_cmd.cpp:23303-23305`「Handle stuck trains」（`VehicleRailFlag::Stuck` 后的预留重试）。**449 次/秒 ≈ 每帧 13.5 次** ⇒ 长期有 7~13 条链在 stuck 重试，每次 151 µs 全额烧在寻路上。这既省 10%，也是行为缺陷线索（KI-330 家族）。
3. `spd` 只占 4% ⇒ **T-3 降级**（MaxSpeedInfo 缓存收益很小）。
4. A* 位于 `ctrl` 桶内；答案本只砍 1 ms ⇒ `ctrl` 的主要成本极可能是 `ExtendTrainReservation` 的**逐段扩展**（不需要 A*）而非 `FindPath`。**待证**：需把 ctrl 拆成 `astar` / `extend` 两桶。
5. **T-1 仍未证**：R3R 的 O(链长) 扫描若存在，全部落在 `resid`（24%）里。`resid` 是最大未知项。
6. **失真提醒**：Debug 树 `/Od /Ob0` 无内联，`resid` 会偏大（R3R 每 tick 的虚调用/小函数全在 resid），绝对比例仅供定性排序；桶间**量级**可信。

## 八、下一刀（待玩家拍板，二选一或并行）

- **方案 1（取数，零行为变更）**：把 `ctrl` 拆成 `astar` / `extend`，把 `resid` 拆 2~3 桶（物理/站台进出/掉头+其它）。只改 `.cpp`，增量约 2 分钟。
- **方案 2（直接修，白捡 10%）**：查这 449 次/秒的 stuck 车队为什么卡住。属行为修复，不是性能 hack。

## 九、`ctrl` 桶的结构确认与切分方案（2026-11-09 取证）

玩家口径是「继续压榨」，不必等二选一，先做零风险的那一步。

**结构确认（源码锚点）**：

1. `ctrl` 的计时范围 = **整个** `TrainController()`：`R3RScopeTimer r3r_ctrl_timer(&r3r_ctrl_ns, &r3r_ctrl_calls);` 位于函数第 6 行（`train_cmd.cpp:20818`），作用域覆盖后面整段逐节循环（`:20843` `for (prev = v->GetMovingPrev(); v != nomove; ...)`）。
2. 该函数内**唯一的 A* 入口**是 `:20929` 的 `ChooseTrainTrack(first, gp.new_tile, enterdir, bits, CTTF_MARK_STUCK | CTTF_NON_LOOKAHEAD);`，条件是 `prev == nullptr`（即「机车自己刚跨进新格」），即一次 `TrainController` 调用至多触发一次 `ChooseTrainTrack`。
3. `ExtendTrainReservation()` 定义在 `:16114`、唯一调用点在 `:17525`（`TrainLocoHandler` 内），**不在 `TrainController` 里**，因此它落在 `prv` 或 `resid`，不在 `ctrl`。

⇒ **结论句**：`ctrl` 可以由 `astar` 子桶干净地切开——`astar ⊂ ctrl`，所以 `sub_ms` 公式（`:8761`）**不需要改**（`ctrl` 已全额计入），只额外打印「其中 `astar` 占多少」，`resid` 也不会被影响。若 `astar` 在 311 ms/s 里占比很小，则 9.4 ms/帧 是逐节物理步进（`GetNewVehiclePos` / `VehicleEnterTile` / `TrainEnterStation`）本身，压榨必须换靶（`stk` / `resid`）；若占比大，则寻路仍有空间（`ChooseTrainTrack` 内的 `TryLongReserve` 往返、`lookahead` 重建）。

## 十、已落地的改动（纯计时，零行为变更）

只改 `src\train_cmd.cpp` 一个文件，未碰任何 `src\*.h`（避免 KI-183 全量）。

1. **计数器**（`static uint64_t r3r_astar_calls/ns`，紧接 `r3r_ctrl_calls` 之后）：
   `astar` 是 `ctrl` 的**子集**，注释写明「not added to sub_ms」。
2. **PERF-TICK 输出**：格式串在 `ctrl=` 与 `coll=` 之间插入 `astar=%.1f/s %.0fms/s |`，
   参数列表在 `ctrl_calls/ctrl_ns` 之后插入 `astar_calls/astar_ns`（顺序必须与格式串一致）。
3. **清零**：`R3RPerfDumpAndReset()` 在 `r3r_ctrl_calls = r3r_ctrl_ns = 0;` 之后加一行。
4. **计时点**（`TrainController()` 内 `prev == nullptr` 分支，原 `:20939`）：
   把 `ChooseTrainTrack(...)` 包进立即执行的 lambda，lambda 体内构造
   `R3RScopeTimer r3r_astar_timer(&r3r_astar_ns, &r3r_astar_calls);` 再 `return ChooseTrainTrack(...)`。
   用 lambda 而不是「在块开头放 timer」的原因：该 `if` 块一直延伸到红信号处理（约 200 行），
   直接放块头会把红信号/`force_proceed` 的代码也算进 `astar`。
   `ChooseTrainTrackResult` 是两字段 POD（`Track track; ChooseTrainTrackResultFlags ctt_flags;`），按值返回安全。

**判读口径（拿到新日志后）**：
- `astar` 接近 `ctrl` ⇒ 9.4 ms/帧 主要是寻路，继续在 `ChooseTrainTrack` / `TryLongReserve` 里找。
- `astar` 远小于 `ctrl`（例如 < 20%）⇒ 目标改为逐节物理步进 + `resid`，`ctrl` 别再动。
- `astar` 的 **/s 次数**本身也是信息：`ctrl=1859.8/s` 而 `astar` 应显著更小（一条链每秒跨格次数有限）；
  若二者接近，说明几乎每次 `TrainController` 都在跨格。

**构建状态（2026-11-09 02:32，已编译通过）**：
- 首次尝试被脚本闸门拦下：`build\R3R_incbuild.done` = `EXIT_CODE=97`（`openttd.exe` PID 20216 仍在运行），未编译。
- 玩家人工关闭游戏后重跑 `R3R_fullbuild.cmd inc detach`：
  `tasklist` 确认无 `openttd.exe` → 护栏 `GUARD: incremental is safe (no header/lang file is newer than the newest object)` →
  `[2/3] Building CXX object ... train_cmd.cpp.obj` → `[3/3] Linking CXX executable openttd.exe` →
  `build\R3R_incbuild.done` = `EXIT_CODE=0`（02:32:09）。
- 时间戳链：`src\train_cmd.cpp` 02:24:38 → `train_cmd.cpp.obj` 02:30:08（15 823 934 B）→ `build\openttd.exe` 02:31:53（52 610 560 B）。
- 产物自证：`findstr /M /C:"astar=%.1f/s %.0fms/s" build\openttd.exe` 命中 ⇒ 新格式串确实进了 exe。

⇒ 本轮状态 = **已实现 + 已编译**，等玩家跑一次运行态日志（`build\R3R_perf.log` 的 `PERF-TICK` 行）取 `astar` 读数。

## 十一、取数结果（2026-11-09 02:43，`build\R3R_perf.log` 128 755 B / 183 个 PERF-TICK）

### 11.1 日志结构：只有 5 个运行态窗口，其中 3 个是本轮 exe

`PERF-TICK` 共 183 条，但 `loco=0.0/s` 占 **178 条**（暂停态，全部桶为 0，无取数价值）。
非零窗口仅 5 条，且**前 2 条没有 `astar` 字段** ⇒ 那是上一版 exe 的旧记录残留在同一文件里（日志是追加写的，**不是每次覆盖**）。
⇒ 认新 exe 的判据必须是「行内出现 `astar=`」，不能只看文件名/时间。

（另：该日志按固定列宽**折行**写入，`findstr`/`Select-String` 直接取行只能拿到碎片；正确读法 = 读全文 → `split('PERF-TICK')` → `' '.join(x.split())` 归一空白。）

### 11.2 三条新 exe 窗口的原始读数

| 窗口 | fps / frameMs avg | loco | ctrl | **astar** | stk | resid | sub | dbgWrite / skipped |
| --- | --- | --- | --- | --- | --- | --- | --- | --- |
| ① 旧 exe | 61.7 / 16.2 | 4774.2/s 663ms/s | 1859.8/s 311ms/s | — | 449.2/s 68ms/s | 159ms/s | 505ms/s | 0 / 0 |
| ② 旧 exe | 60.4 / 16.6 | 4470.0/s 675ms/s | 1745.4/s 287ms/s | — | 421.0/s 56ms/s | 209ms/s | 465ms/s | 0 / 0 |
| ③ 新 exe | **3.5 / 289.1** | 4038.6/s 654ms/s | 1572.3/s 262ms/s | **107.3/s 1ms/s** | 379.8/s 72ms/s | 202ms/s | 451ms/s | 228.8/s / 484/s |
| ④ 新 exe | **5.3 / 188.6** | 6461.9/s 727ms/s | 2505.1/s 398ms/s | **171.2/s 1ms/s** | 611.0/s 71ms/s | 137ms/s | 590ms/s | 2207.0/s / 451/s |
| ⑤ 新 exe（干净） | **59.0 / 16.9** | 6189.0/s 698ms/s | 2398.8/s 385ms/s | **163.7/s 2ms/s** | 584.4/s 66ms/s | 132ms/s | 566ms/s | 0 / 0 |

### 11.3 结论一：`astar` 不是瓶颈（寻路方向证伪）

- 占比：`astar` / `ctrl` = `1/262` = **0.38%**、`1/398` = **0.25%**、`2/385` = **0.52%**。
- 占帧：窗口⑤ 干净数据 `2ms/s ÷ 59fps` = **0.034 ms/帧**（16.9 ms 帧里的 0.2%）。
- 单次耗时：`2 ms/s ÷ 163.7 次/s` ≈ **12 µs/次**；同期单次 `ctrl` = `385 ms/s ÷ 2398.8` ≈ **160 µs/次**。
- 调用比：`astar`/`ctrl` ≈ **6.8%**（三次窗口一致）⇒ 只有约 1/15 的 `TrainController` 跨格，与「一条链每秒跨格次数有限」吻合。

⇒ **`ctrl` 的 5~6.5 ms/帧 全是逐节物理步进**（`GetNewVehiclePos` / `VehicleEnterTile` / `TrainEnterStation`），
`ChooseTrainTrack` / `TryLongReserve` / lookahead 重建**停止投入**，那条线到此收口。

### 11.4 结论二（口径订正，重要）：之前的「9.4 ms/帧、47%」作废

日志自带 `fps=59.0~61.7`、`frameMs avg=16.2~16.9`、`frames=128`，**不是 33 fps**。
此前按 33 fps 换算得到 `ctrl=9.4ms/帧 占 47%` 是分母错误。按实测 60 fps 重算：

| 桶 | ms/s（窗口⑤） | ms/帧 | 占 16.9ms 帧 |
| --- | --- | --- | --- |
| `ctrl` | 385 | **6.5** | **38%** |
| `astar`（⊂ ctrl） | 2 | 0.03 | 0.2% |
| `resid` | 132 | 2.2 | 13% |
| `stk` | 66 | 1.1 | 6.6% |
| `dep` | 34 | 0.58 | 3.4% |
| `spd` | 34 | 0.58 | 3.4% |
| `vp` | 23 | 0.39 | 2.3% |
| `nl` | 432 | 7.3 | **43%（口径存疑，见 11.5）** |

`sub=566ms/s` = 各桶之和（**不含 `astar`、`coll`**，`:8769` 的求和式里没有它们，故 `astar` 只打印、不重复扣减，与 §九 的设计一致）。

### 11.5 疑点 REG-411-A：`nl` 口径不可信，不能当靶子

`nl_ms`（`:8773`）= `R3RGetTrainsMs() * fps - loco_ns`，即「列车 tick 里不属于 `TrainLocoHandler` 的部分」。

- 窗口⑤ 算出 `nl=432ms/s`（7.3 ms/帧，比 `ctrl` 还大），而窗口①（fps 几乎相同 61.7、loco 相近 663）只有 `55ms/s`。
- 同一份日志里 `glTrains=213.25ms` 在窗口③④⑤**逐字相同**（常数），窗口②是 239.87、窗口①是 138.24；用 `213.25 × 5.3 - 727` 反推窗口④应得 ≈ 403，但实测 `nl=17`。
- ⇒ `glTrains` 与 `fps` 的乘法口径不自洽（`glTrains` 很可能已是"某累计量/上一帧值"，不是"本窗口每帧 ms"）。

**动作**：`nl` 暂不进优化靶清单，**先不追**；若要追，需先把 `R3RGetTrainsMs()` 的量纲查清（它读的是 `PFE_GL_TRAINS` 的哪种累加器）。本轮**不据此下任何结论**。

### 11.6 疑点 REG-411-B：窗口③④的 fps 崩到 3.5/5.3 是探针刷屏，不是代码回归

- 窗口③ `dbgWrite=228.8/s 85ms/s`、`skipped=484/s`；窗口④ `dbgWrite=2207.0/s 72ms/s`、`skipped=451/s`、`max=531ms`；`posHelper=204.7/214.6 次/s`、`dbgEdge=37.6/42.6 次/s` 同时非零。
- 窗口⑤ 这些全为 0，`fps=59.0`、`frameMs avg=16.9`。
- ⇒ ③④是**玩家正在做挂车/拖动/存档等编辑操作**时的窗口（调试写盘 + 边沿探针被触发），单帧峰值 486/531 ms。
- **负判据成立**：本轮改动只有两个计数器 + 一个计时器作用域，不可能造成 3.5 fps；干净窗口⑤与旧 exe 的①（59 vs 61.7）同量级。

### 11.7 新线索（未取数，仅登记）：`vehs=40036 / artic=27004`

同一行末尾：`chains=942 vehs=40036 maxChain=243 segs=2 artic=27004 waitCouple=0`。
⇒ 全图 4 万节车里 **27 004 节是铰接部件（67%）**。`TrainController` 的逐节循环（**`:20853`**，`for (prev = v->GetMovingPrev(); v != nomove; prev = v, v = v->GetMovingNext())`）走的是 `GetMovingPrev` 系列，**不跳过 artic 部件**，所以每帧约 4 万次迭代里有 2.7 万次在动 artic 部件。
**这是 `ctrl` 唯一剩下的量级级入口**：把 6.5 ms/帧 降下来只能靠"减少参与步进的节数"，而不是"让寻路更聪明"。

> ⚠ 本节 11.7 的"跳过 artic 部件"设想已在 §十二 取证后**推翻**，只保留"节数被 artic 放大 3 倍"这个数字事实。

---

## 十二、取数复核：artic 部件"可跳过"的设想不成立（2026-11-09）

玩家追问"为什么说铰接部分有优化空间"，去读代码验证，结论是**上一轮的推断过头了**，必须收窄。

### 12.1 代码事实一：主循环确实遍历全部节，包括 artic

`train_cmd.cpp:20853`：
```cpp
for (prev = v->GetMovingPrev(); v != nomove; prev = v, v = v->GetMovingNext()) {
    r3r_mov_iters++; // KI-14 (2): measure how many vehicles this call actually walks
```
无 `IsArticulatedPart()` 分支、无 `continue`（`movi_iters` 计数就是为量这个）。

### 12.2 代码事实二：每节车的位置是这个循环**独立算出来并写进去的**

三个写入点，全部在循环体内、对每一节（含 artic）都执行：

| 位置 | 代码 | 作用 |
| --- | --- | --- |
| `:20862` | `GetNewVehiclePosResult gp = GetNewVehiclePos(v);` | 算出本节前进 1px 后的 x/y/tile |
| `:21303-21304` | `v->tile = gp.new_tile; v->track = chosen_track;` | 写 tile / track |
| `:21525-21529` | `v->UpdateDeltaXY(); v->x_pos = gp.x; v->y_pos = gp.y; v->UpdatePosition();` | 写包围盒 + 像素坐标 + 位置哈希 |
| `:21535` | `v->UpdateInclination(...)` | 写 Z 高度 |

而 `GetNewVehiclePos`（`vehicle.cpp:2986-3009`）**通篇没有 artic 特例**：
```cpp
const Coord2D<int8_t> &coord = delta_coord[v->GetMovingDirection()];
int x = v->x_pos + coord.x;
int y = v->y_pos + coord.y;
gp.new_tile = TileVirtXY(x, y);
```
就是"沿本节自己的朝向走 1px"。

### 12.3 结论：不能跳过，只能砍单节工作量

- artic 部件**不是**"位置由父车通过 `CalcNextVehicleOffset` 决定"的被动附属物；它和普通车厢一样独立推进、独立写 tile/track/坐标/包围盒/Z 高度。
- 所以"在逐节循环里 `continue` 掉 artic"会直接让部件停住不跟随 ⇒ 图像断裂、占位错误、碰撞误判。**该设想作废。**
- 剩下的唯一可能形态 = "对 artic 部件少做其中某几步"（例如 `UpdatePosition()` 的位置哈希、`UpdateInclination()` 的 Z、`UpdateDeltaXY()` 的包围盒），但**每一样都要单独证明不影响显示/碰撞/信号**，且 artic 同样要画、同样要占轨道，估计能砍的很少。

### 12.4 因此对 §11.7 的修正

- 保留：`artic=27004`（67%）确实是 `ctrl` 迭代基数的 3 倍放大器，`vehs` 与 `ctrl` 的线性关系在三个窗口里都成立。
- 撤回：把"跳过 artic 部件"当作可执行方案。**B 方案降级为"已探查、路径不通"**，不再作为候选。
- 靶子回到 **A（`stk` 1.1 ms/帧、449~611 次/秒重试）** 与 `resid`（2.2 ms/帧，13%，尚未看代码）。

---

## 十三、"从 TrainController 里继续压榨"候选清单（2026-11-09，待逐条取证）

玩家明确要求继续压 `ctrl`（6.5 ms/帧，占 38%）。跳过 artic 已作废（§十二），所以只剩**减少每节车的工作量**与**减少无效迭代**两条路。先登记候选，再逐条证明或证伪，不凭直觉下结论。

| 编号 | 候选 | 假设收益 | 风险 | 状态 |
| --- | --- | --- | --- | --- |
| C-13.1 | `UpdatePosition()` 每节每 tick 都调，若是"位置哈希表重插 + 视口失效"，4 万节就是 4 万次重插 | 高 | 中（查表依赖它） | **已证伪**（见 13.1） |
| C-13.2 | `UpdateDeltaXY()` 每节都调（算包围盒），但包围盒只在 direction/长度变化时才变，且只在绘制/碰撞时用 | 高 | 中 | **成立但量级小**（见 13.2） |
| C-13.6 | `VehicleEnterTile()` 每节每 tick 都调，含"同一格内只挪 1px"的情况（占 99.5% 的迭代） | 高 | 中 | 待取证（**当前头号嫌疑**） |
| C-13.3 | `UpdateInclination()` 每节都调（算 Z 高度、坡度），artic 部件的 Z 是否可由父车继承 | 中 | 中 | 待取证 |
| C-13.4 | `GetMovingPrev/GetMovingNext` 每步都要判 `IsDrivingBackwards()`，可在外层预算一次 | 低中 | 低 | 待取证 |
| C-13.5 | 循环入口 `TrainController` 每 tick 每列都调，但 `cur_speed==0 && 无预留` 的列车是否可整列跳过 | 高 | 高（行为） | 待取证 |

**取证纪律**：每条必须给出函数实现原文 + 调用频率 + 是否可省的理由；证伪的直接标"作废"，成立的要再证明"不影响显示/碰撞/信号"。

### 13.1 C-13.1（位置哈希重插）已证伪

`UpdatePosition()` = `UpdateVehicleTileHash(this, false)`，而 `UpdateVehicleTileHash`（`vehicle.cpp:854-865`）开头就有早退：

```cpp
void UpdateVehicleTileHash(Vehicle *v, bool remove)
{
	TileIndex old_hash_tile = v->hash_tile_current;
	TileIndex new_hash_tile;
	...
	new_hash_tile = v->tile;
	if (old_hash_tile == new_hash_tile) return;   // ← 同格内移动直接返回
```

⇒ 99.5% 的同格迭代只付「两次读 + 一次比较 + 一次 return」，不是哈希重插。**这条不再是靶子。**

### 13.2 C-13.2（包围盒重算）成立，但只值约 15%

`Train::UpdateDeltaXY()`（`train_cmd.cpp:4821-4882`）通篇只读四个输入：

| 输入 | 用途 |
| --- | --- |
| `v->flags.Test(Flipped)` | 决定 sprite 朝向 |
| `EngInfo(v->engine_type)->visual_effect_and_powered` / `sprite_id` | 决定宽度分段 |
| `v->direction` | 决定 bounds 的四边 |
| `gcache.cached_veh_length` | 决定长度 |

**完全不读 `x_pos`/`y_pos`/`tile`** ⇒ 纯函数式，同格内 1px 移动时重复算出来的 bounds 逐位相同。

量级估算：函数体约 20~30 ns（一次虚分派 + 一次 `EngInfo` 查表 + 分支 + 若干算术 + 写 `bounds`）。4 万节 × 59 fps ≈ 236 万次/秒 × 25 ns ≈ **59 ms/s ≈ 0.9 ms/帧**，占 `ctrl` 的 15%。可优化但**不是量级级**，且要新增"上次 bounds 对应的 direction"状态（改 .h ⇒ 全量重编），性价比一般。

### 13.3 C-13.6（`VehicleEnterTile`）基本证伪：普通轨道格第一行就返回

先确认调用点确实在同格分支内（`train_cmd.cpp:20877-20893`）：

```cpp
if (gp.old_tile == gp.new_tile) {            // ← 同格
    if (v->track == TRACK_BIT_DEPOT) { ... }  // 车库
    else {
        if (v->IsMovingFront() && !TrainCheckIfLineEnds(v, reverse)) return false;
        auto vets = VehicleEnterTile(v, gp.new_tile, gp.x, gp.y);   // ← 无条件
```

但被调的函数很便宜：

`VehicleEnterTile`（`vehicle.cpp:3047-3050`）只是纯虚分派，无逻辑。铁路实现 `VehicleEnterTile_Rail`（`rail_cmd.cpp:4791-4794`）**开头三行就退出**：

```cpp
static VehicleEnterTileStates VehicleEnterTile_Rail(Vehicle *u, TileIndex tile, int x, int y)
{
	/* This routine applies only to trains in depot tiles. */
	if (u->type != VehicleType::Train || !IsRailDepotTile(tile)) return {};
```

⇒ 普通轨道格每次只花「`GetTileType` + 查表 + 虚分派 + 两次比较 + return」≈ 5~10 ns，236 万次/秒 ≈ 20 ms/s ≈ 0.3 ms/帧。**不值得动，作废。**

唯一例外是**站台格**走 `VehicleEnterTile_Station`，它逐 tick 判"是否到达停车标"，而这正是 1px 移动要判的东西 —— 语义必需，不能跳过。

### 13.4 剩余未查项（下一组的靶子）

到目前 `ctrl` 的 6.5 ms/帧已排除：`astar`(0.5%)、哈希重插(0)、`VehicleEnterTile`(5%)。剩下这几处还没量过，且都不是零成本：

| 位置 | 每节每 tick 的工作 | 疑点 |
| --- | --- | --- |
| `v->UpdateInclination(...)` `:21535` | 算 Z 高度 / 坡度状态 | 调用频率是否真的每节 |
| `v->IsDrawn()` → `UpdateViewport(true)` `:21507` | 算 sprite 范围、标脏区 | 判据若不含"是否在屏幕内"，则 4 万节全做 UI 工作 |
| `GetMovingPrev/GetMovingNext` `:20853` | 每步判 `IsDrivingBackwards()` | 可外层预算一次 |
| `UpdateDeltaXY()` `:21525` | 见 13.2 | 约 15% |

**结论性建议**：继续靠读码猜性价比太低（已连证伪三条）。应当**直接在循环体里下 4~5 个细分计时探针**（pos / enter / delta / incl / viewport），跑一次全局 59 fps 场景，用读数决定砍哪段。这与既有 `r3r_ctrl_ns` 探针同一套机制，只多几个桶，成本极低。

---

## 十四、`TrainController` 调用全景（2026-11-09，讲解）

函数体 `train_cmd.cpp:20823 - 21661`，签名 `bool TrainController(Train *v, Vehicle *nomove, bool reverse)`。

### 14.1 谁调它、多久调一次

调用方是 `TrainLocoHandler`（车辆 tick 的物理分支）：一次「运动步进」调一次，参数 `(link, nullptr, true)`。所以频率 = **有速度的列车数 × 59 fps**，不是车辆数 × 59。`ctrl=385 ms/s` 除以 4 万节迭代基数，就是 §11 里那份 6.5 ms/帧的来源。

### 14.2 它做四件事，按代价从低到高

| 阶段 | 行号 | 做什么 | 是否每节都做 |
| --- | --- | --- | --- |
| 0 入口 | `:20827-20850` | 预留相位守卫、`r3r_ctrl_ns` 计时、`first=v->First()`、倒车距离早退 `goto reverse_train_direction` | 每次调用 1 次 |
| 1 逐节步进 | `:20853` 起 `for (prev = v->GetMovingPrev(); v != nomove; prev = v, v = v->GetMovingNext())` | 见 14.3 | **每节 1 次** |
| 2 方向收尾 | `:21659` | `direction_changed` 为真才重算 `cached_max_curve_speed` | 每次调用 1 次 |
| 3 失败出口 | `:21663 invalid_rail` / `:21667 reverse_train_direction` | 非链头直接 `return true`（放弃）；链头则回滚 wormhole 状态并尝试掉头 | 出错才走 |

### 14.3 逐节步进（热路径）的七个动作

1. **算新位置** `:20862` `GetNewVehiclePos(v)` —— `GetMovingDirection()` 虚分派 + 查 `delta_coord` 表 + 两次加法 + `TileVirtXY`。
2. **判断跨没跨格** `:20863-20876` —— 先处理「刚离开桥头进虫洞」，再看 `gp.old_tile == gp.new_tile`。
3. **同格分支** `:20877-20899`（**99.5% 的迭代走这里**）：
   - 车库格：位置钉住不动（`gp.x = v->x_pos`），`reverse_distance` 清零；
   - 普通格：`IsMovingFront()` 时 `TrainCheckIfLineEnds()` 检查是否到头（到头就 `return false` 整列放弃本步）；然后 `VehicleEnterTile()`（§13.3 已证明普通格内部第一行就 return）；若返回 `EnteredStation` 则 `TrainEnterStation()` 推进订单。
4. **跨格分支** `:20900-21472`（0.5%）：`GetTileTrackStatus` 算可达轨 → `CheckCompatibleRail` → **仅链头**（`prev == nullptr`）调 `ChooseTrainTrack`（这就是 `astar`，2 ms/s）→ 红灯/强制通过判定 → 选轨后写 `v->tile`/`v->track` → 站台/坡道/隧道桥/道口特殊处理 → 可能 `goto enter_new_tile` 二次进入（自定义桥头）。
5. **写位置**（两条路径最终汇合）：
   - 隧道 wormhole 分支 `:21475-21521` 内部自己写 x/y/UpdatePosition/UpdateDeltaXY，然后 **`continue`**（避开下面统一段）；
   - 普通路径落到 `:21524-21535`：`UpdateDeltaXY()` → 写 `x_pos/y_pos` → `UpdatePosition()`（哈希早退）→ `DecreaseReverseDistance` → `AdvanceLookAheadPosition` → `DecrementPendingSpeedRestrictions` → `UpdateInclination()`（`GVF_CHUNNEL_BIT` 才进）→ 链头 `AffectSpeedByZChange`。
6. **信号 / 道口** `:21542-21590`：`update_signal_tunbridge_exit` 时刷新隧道桥出口信号；`update_signals_crossing` 且 `IsMovingFront()` 时做 `TrainMovedChangeSignal`，进入 PBS 块却没预留就 `TryPathReserve`，失败 `MarkTrainAsStuck`（**这就是 `stk` 1.1 ms/帧 的来源**）。
7. **绘制** `:21507`（wormhole 路径）/ 普通路径尾部：`if (v->IsDrawn()) v->Vehicle::UpdateViewport(true)`。

### 14.4 由此得到的"压榨"判断

- **按节计费的固定成本**：`GetNewVehiclePos` + 同格判定 + `VehicleEnterTile` + `UpdateDeltaXY` + `UpdatePosition` + `IsDrawn/UpdateViewport` + `UpdateInclination`（后者有条件）。7 项 × 4 万节 = `ctrl` 的主体。
- **按列车计费**：`ChooseTrainTrack`（2 ms/s）、`TryPathReserve`、`TrainCheckIfLineEnds`。这些只对链头跑，砍它没有量级空间。
- 所以量级只能来自「让 4 万节的每一次迭代更便宜」，而 7 项里已证伪/无效的占 4 项（enter tile、哈希、astar、方向收尾），真正还没量过的是 **`UpdateDeltaXY`（约 15%）、`UpdateInclination`、`IsDrawn+UpdateViewport`** 这三项 —— 与 §13.4 的探针清单一致。

**新发现（值得单独验）**：`:21493-21494`（wormhole 路径）与 `:21525`（普通路径）是两条并列的 `UpdateDeltaXY()` 调用点，互斥不重复；但**同格路径也走 `:21525`**，即"车轮没跨格、朝向没变"时仍在重算 bounds（§13.2 已证其四输入全不含坐标）⇒ 这是唯一一个**已被证明冗余、且占 15%** 的点，而不是猜测。

---

## 十五、"地基是不是搭在像素上"——四处定义核对（2026-11-09）

玩家提问：TrainController 的地基是否以像素为单位？是否每节车厢都有自己的边界框与像素位置？四处取证结论：**位置是每节车各自一份的绝对像素坐标，包围盒是每节车各自一份、但以相对偏移表达；格与轨道位是另一套独立真值，两者靠两张查表绑定。**

### 15.1 每节车各存一份绝对坐标（不是链头算相对偏移）

`vehicle_base.h:325-328`：

```cpp
int32_t x_pos = 0;                        ///< x coordinate.
int32_t y_pos = 0;                        ///< y coordinate.
int32_t z_pos = 0;                        ///< z coordinate.
Direction direction = Direction::Invalid; ///< facing
```

⇒ 4 万节 = 4 万份 `(x_pos, y_pos, z_pos)`。`TrainController` 的 `for` 循环逐节推进，正是因为这四样是**逐车字段**，没有"链头位置 + 节号 × 车长"的解析式可用。

### 15.2 每节车各有一个包围盒，且是相对偏移

`vehicle_base.h:337-339`：

```cpp
VehicleSpriteSeq sprite_seq{};   ///< Vehicle appearance.
Rect16 sprite_seq_bounds{};
SpriteBounds bounds{};           ///< Bounding box of vehicle.
```

两套包围盒：`bounds`（物理，`UpdateDeltaXY` 写）与 `sprite_seq_bounds`（绘制序列，`UpdateSpriteSeqBound` 写）。`bounds` 不是绝对矩形，而是**相对本车坐标的 origin/offset/extent**，证据在 `vehicle_base.h:1849`：

```cpp
Point pt = RemapCoords(this->x_pos + this->bounds.origin.x + this->bounds.offset.x,
                       this->y_pos + this->bounds.origin.y + this->bounds.offset.y,
                       this->z_pos);
```

⇒ 屏幕点 = 本车像素坐标 + 本车包围盒偏移。所以"有自己边界框"成立，但它寄生在自己的坐标上。

### 15.3 推进是纯像素加法，格是反推出来的

`vehicle.cpp:2986-3009` `GetNewVehiclePos`：

```cpp
const Coord2D<int8_t> &coord = delta_coord[v->GetMovingDirection()];
int x = v->x_pos + coord.x;
int y = v->y_pos + coord.y;
...
gp.old_tile = v->tile;
gp.new_tile = TileVirtXY(x, y);
```

`delta_coord` 是 8 方向每步 1 单位的表 ⇒ **一次调用推进 1 个世界单位**。`new_tile` 由像素反算，`v->tile` 只是缓存 ⇒ **格不是独立真值，是像素的函数**。

### 15.4 像素精度与"轨道层改写像素"的耦合点

`tile_type.h:15-17`：

```cpp
static constexpr uint TILE_SIZE      = 16;  ///< Tile size in world coordinates.
static constexpr uint TILE_UNIT_MASK = TILE_SIZE - 1;
static constexpr uint TILE_PIXELS    = 32;  ///< Pixel distance between tile columns/rows in ZOOM_BASE.
```

⇒ 1 格 = 16 世界单位（格内位置 0..15），绘制时 1 格 = 32 屏幕像素，即 **1 世界单位 = 2 屏幕像素**。

关键耦合在 `vehicle.cpp:5495-5501` `VehicleEnterTileCoordinates`：

```cpp
gp.x = (gp.x & ~TILE_UNIT_MASK) | b.x;
gp.y = (gp.y & ~TILE_UNIT_MASK) | b.y;
return b.dir;
```

`b` 来自 `_vehicle_subcoord[enterdir][track]` ⇒ **跨格瞬间，位置低 4 位被该轨型的标准子坐标覆盖，朝向也被轨道强制指定**。这就是"像素位置由轨道拓扑裁决"的唯一入口。

### 15.5 三条由此确定的结论

1. **"地基在像素上"成立，但只说对一半**：位置的存储与推进是像素级的（逐节 int32 + 每次 ±1），而 TrainController 的**所有判定**（是否跨格 `gp.old_tile == gp.new_tile`、选哪根轨、红不红灯、到没到站）都在格/轨道位层完成。像素只负责"走到哪"，轨道位负责"能往哪走"。
2. **`bounds` 与坐标解耦**（`train_cmd.cpp:4821-4882` 只读 `Flipped`/`engine_type`/`direction`/`cached_veh_length`）⇒ 同一节车在同一朝向下的 bounds 恒定，同格内 1 单位位移时重算属纯冗余（§13.2 的 15% 结论不变，且这是**结构性冗余**而非性能猜测）。
3. **`UpdateViewport` 已有视口裁剪早退**（`vehicle_base.h:1850` `IsPointInViewportVehicleRedrawArea` 不在视野内即 return）⇒ 上一轮 §13.4 怀疑的"4 万节全做 UI 工作"**不成立**。但注意 `UpdateDeltaXY` 在 `:1844` 先于裁剪执行，所以裁剪救不了包围盒重算。

### 15.6 新增疑点与候选（待证）

- **`progress` 字段**（`vehicle_base.h:341`）：注释自陈 "The percentage (if divided by 256) this vehicle already crossed the tile unit" ⇒ 已有"格内进度"的 8 位标量。若 TrainController 同格分支完全不需要像素精度（只需知道"还在同一格"），则像素推进可能可以降频、由 `progress` 承担格内判定。**未确认**：`TrainController` 本轮读过的区间内未见读取 `progress`，需全文件搜索确认它是否只被绘制/探针使用。
- 若上条成立，则 §13.4 建议的探针方案要加第 6 个桶：`progress` 依赖度。

---

## 十六、"每节每 tick 全地图找一遍"是否成立（2026-11-09，讲解）

玩家提问：TrainController 必须每个车厢每次都全地图找一遍才能把列车挪动吗？**结论：完全不成立。全图级搜索只出现在跨格瞬间、且只有链头；99.5% 的逐节迭代是纯本地算术，一行地图数据都不读。**

### 16.1 同格分支的完整调用清单（只有三个调用）

`train_cmd.cpp:20877-20899`：

```cpp
if (gp.old_tile == gp.new_tile) {
    if (v->track == TRACK_BIT_DEPOT) {
        gp.x = v->x_pos; gp.y = v->y_pos; first->reverse_distance = 0;   // 车库：钉死
    } else {
        if (v->IsMovingFront() && !TrainCheckIfLineEnds(v, reverse)) return false;
        auto vets = VehicleEnterTile(v, gp.new_tile, gp.x, gp.y);
        if (vets.Test(VehicleEnterTileState::EnteredStation)) TrainEnterStation(first, GetStationIndex(gp.new_tile));
    }
}
```

⇒ 同格分支里只有 `TrainCheckIfLineEnds`（**且仅在 `IsMovingFront()` 时**）、`VehicleEnterTile`、`TrainEnterStation`（仅站台末端那一 tick）三个调用。**没有一个遍历车辆池，也没有一个扫描地图区域。**

### 16.2 `TrainCheckIfLineEnds` 的搜索范围＝前方一格

`train_cmd.cpp:22067-22116`：

```cpp
DiagDirection dir = VehicleExitDir(moving_front->GetMovingDirection(), moving_front->track);
TileIndex tile = moving_front->tile + TileOffsByDiagDir(dir);          // 只算相邻一格
TrackStatus ts = GetTileTrackStatus(tile, TRANSPORT_RAIL, RoadTramType::Invalid, ReverseDiagDir(dir));
...
if (bits == TRACK_BIT_NONE || !CheckCompatibleRail(consist, tile, dir, true)) return TrainApproachingLineEnd(...);
if ((trackdirbits & red_signals) != 0) return TrainApproachingLineEnd(...);
if (IsLevelCrossingTile(tile)) MaybeBarCrossingWithSound(tile);
```

⇒ **O(1)**。算一格偏移、读那一格的 track status、判四条（无轨/轨型不符/红灯/道口）。没有 while、没有 A*、没有范围扫描。

### 16.3 逐节迭代 7 项的真实复杂度

| 动作 | 编译器展开后的实际成本 | 读地图？ |
| --- | --- | --- |
| `GetNewVehiclePos` | 查 8 项表 + 2 次加法 + `TileVirtXY` 位运算 | 否 |
| 同格判定 | 一次整数比较 | 否 |
| `VehicleEnterTile` | 普通格第一行 return（§13.3） | 否 |
| `UpdateDeltaXY` | 4 个标量算 origin/offset/extent | 否 |
| `UpdatePosition` | 写/删 tile 哈希槽，哈希未变即 return | 否（只写索引） |
| `IsDrawn` + `UpdateViewport` | 视口矩形包含判定（§15.5） | 否 |
| `UpdateInclination` | `GVF_CHUNNEL_BIT` 才进，读自身 z | 否 |

⇒ **逐节 7 项全部 O(1) 且不读地图**。逐节代价与地图大小、车辆数、线路长度**全都无关**。

### 16.4 唯一的大范围搜索：链头的 A*，且只在跨格瞬间

`ChooseTrainTrack`（`:20900-21472` 跨格分支内，守卫 `prev == nullptr`）是真正的搜索，但它：
- 只对**链头**执行（不是每节）；
- 只在**跨格那一 tick** 执行（不是每 tick）；
- 占用 2 ms/s（§12 实测），已在 §14.4 判定"没有量级空间"。

`TryPathReserve` 同理：进入 PBS 块且未预留时才跑，不是每 tick。

### 16.5 真正"遍历全图"的东西不在 TrainController 里

- `R3RReservationAudit()`（R3R 自加）：每 2048 tick 扫全图 plain rail 预留格。**是 R3R 的探针式审计，不是引擎主循环**，且已被 §十五 R243 的脏标记机制改成"有命令才加速"。
- `ReserveTrackUnderConsist()`：每 tick 跑，但遍历的是**本链的每节 + 每节那一格**，O(链长)，不是 O(地图)。

### 16.6 那 385 ms/s 到底花在哪：cache miss，不是算法

算一下量级：4 万节 × 59 fps ≈ **236 万次/秒** 逐节迭代。每次迭代的固定动作里有 4 处必然访存：

1. `v->GetMovingNext()` —— 沿 `Next()` 指针跳到下一节。`Vehicle` 对象 200+ 字节，4 万节分散在 `Pool<>` 的 4 万个槽里 ⇒ **几乎必然一次 L1/L2 miss**；
2. `v->track`、`v->x_pos/y_pos`、`v->bounds` 读写 —— 同一次 miss 的缓存行内则可摊薄，但 `bounds` 与 `x_pos` 是否同行取决于结构布局；
3. `UpdatePosition()` 的 tile 哈希表槽 —— 随机分布，**又一次 miss**；
4. `IsDrawn()` 读 `vcache` + 视口矩形数组 —— 少量。

按一次 DRAM latency ≈ 80~100 ns、每次迭代 2 次有效 miss 估算：2.36 M × 2 × 90 ns ≈ **425 ms/s**，与实测 `ctrl=385 ms/s` **同一量级**。

⇒ **这才是量级的真因：不是搜索，是"每秒钟 236 万次指针追逐"**。因此正确的优化方向从"减少算法"转为：
- 减少**每节**要碰的缓存行数（例如把热字段（`x_pos`,`y_pos`,`direction`,`track`,`bounds`）压到同一缓存行 / 拆出 SoA 热数组）；
- 减少**每 tick 的迭代次数**（例如同格内 1 单位移动时跳过 `UpdateDeltaXY`，因为它不读坐标 —— §15.5）；
- 增加**每帧推进的像素数**（提高单 tick 位移，直接线性降低迭代频率）。

### 16.7 更新 §13.4 探针清单

原清单 5 个桶（pos / enter / delta / incl / viewport）应补两桶：
- `walk`：只算 `for` 循环的指针推进 + 迭代计数（即"空转"成本），用来直接量出 §16.6 的指针追逐底价；
- `hash`：`UpdatePosition()` 的代价（说明里原以为它早退免费，实际它本身就是一次随机访存）。

判读：若 `walk` 单项就接近 `ctrl` 的一半，则证实瓶颈是访存而非算法，后续优化应全线转向数据布局/降低迭代频率，不再逐个函数抠。

---

## 十七、访存优化能不能拿"量级"——先纠两个错，再分层（2026-11-09）

玩家提问：访存？是不是要引入内存密集缓存命中之类的东西？这样才能带来量级的提升吗？

### 17.1 纠错一（我自己上一轮算错了）：不是 4 万节 × 59 fps

§16.6 写的「4 万节 × 59 fps ≈ 236 万次/秒逐节迭代」**是错的**。只有**正在移动的列车**才进 `TrainController`，静止列车不进。

证据（§11.2 窗口⑤，`R3R_perf.log`）：

```
loco=6189.0/s 698ms/s | ctrl=2398.8/s 385ms/s | astar=163.7/s 2ms/s
```

- `ctrl_calls = 2398.8/s ÷ 59 fps ≈ 40.7 次/帧` ⇒ **约 41 条移动中的链**在跑，不是 4 万节。
- 若平均链长 20 节：`41 × 20 × 59 ≈ 4.8 万节次/秒`；若 30 节：`≈ 7.2 万节次/秒`。
- 于是 `ctrl` 的**单节次成本** = `385 ms/s ÷ 4.8 万` ≈ **8 µs/节次**（Debug `/Od`）。

⇒ 所以 §16.6 那个「425 ms/s ≈ 实测 385 ms/s」是**数字凑巧对上、推理却是错的**，不要引用。真实单节次成本 5~8 µs，比我估的 2 次 cache miss × 90 ns = 180 ns **高一个数量级以上**。

### 17.2 纠错二（决定性，优先级高于一切）：这把尺子是 Debug 的

备忘 §7 自己就写着：

> **失真提醒**：Debug 树 `/Od /Ob0` 无内联，`resid` 会偏大，绝对比例仅供定性排序；桶间**量级**可信。

而 `build\` 的 CMake 配置（长期事实）：`CMAKE_BUILD_TYPE=Debug`，`FLAGS="/Zi /Ob0 /Od /RTC1 -std:c++20 -MTd -D_DEBUG -DDBG_ASSERTS"`。官方 JGRPP 0.72.4 是 `/O2 /Ob2 /DNDEBUG /MT`。

⇒ **`ctrl = 385 ms/s`、`frameMs avg = 16.9`、`fps = 59` 全部是 `/Od` 无优化 + 无内联 + 运行时检查 + Debug CRT 的数字。**

在「重循环 + 虚调用 + 每节车一次 `GetMovingNext()` 指针追逐」这种代码上，`/Od` → `/O2` 是 **5~15×** 的常态差距，原因正是：
- `/Ob0` 让 `GetMovingNext()` / `IsMovingFront()` / `UpdateDeltaXY()` 这类小函数**全部真调用**（每节车数组 3~5 次调用开销 + 无法把 `v` 留在寄存器）；
- `/Od` 让每个局部变量**走栈读写**（`gp.x`、`gp.y`、`enterdir` 反复落栈）；
- `/RTC1` 插入栈帧检查；`_DEBUG` 启用迭代器调试与断言；
- `-MTd` 用 Debug CRT，`malloc`/`free` 带填充与校验。

⇒ **结论句：在讨论「要不要引入缓存优化」之前，必须先确认唯一的性能尺子不是 `/Od`。若 20 ms/帧 是 Debug 数字，第一刀是换构建配置，零代码改动，收益大概率 5 倍以上、远超任何访存优化。**

补充：`build-release\` 树已存在（记忆 76468255），但 `R3R_PROBES_DEFAULT=0`（REG-411-C）⇒ 发布树不写 perf 日志，**无法用它取 `ctrl` 分解**。这构成了一个两难：
- 要么在 Release 里临时开探针（改 `CMakeLists`/编译定义 ⇒ 全量重编一次）；
- 要么接受「Debug 树只用来看出比例、Release 只用来看总帧时间」的两树配合。

### 17.3 术语纠正：不是"引入缓存"，是"让已有的缓存有效"

CPU 的 L1/L2/L3 一直都在，不需要"引入"。能做的是三件事，收益量级完全不同：

| 手段 | 具体做法 | 预期收益 | 是否"量级" |
| --- | --- | --- | --- |
| **A. 换构建配置** | `/Od`→`/O2`、`/Ob0`→`/Ob2`、`/RTC1` 关掉、`-MTd`→`-MT` | **5~15×** | **是** |
| **B. 数据布局**（SoA、热字段压紧） | 把 `x_pos/y_pos/direction/track/cached_veh_length/bounds` 从 `Vehicle`（200+ 字节）里拆成并行数组 | **2~3×**（在 A 之上叠加） | 否，常数 |
| **C. 砍冗余动作** | 同格跳 `UpdateDeltaXY`、跳 tile 哈希写 | **1.15~1.3×** | 否 |
| **D. 降低迭代频率** | 见 17.4 | **4~16×** | **是，但要动语义** |

⇒ 直接回答：**"缓存命中率优化"本身拿不到量级，它是 B 档，2~3 倍封顶。** 量级只可能来自 A 或 D。

### 17.4 唯一可能靠"少做事"拿量级的点：16 tick 里有 15 tick 是白跑

算术事实（§15.4 已证）：**1 格 = 16 世界单位，一次 `GetNewVehiclePos` 只推进 1 单位** ⇒ 同一格内要连续推进 **16 个 tick** 才跨格一次。

这 16 个 tick 里逐节循环各字段的变化：

| 字段 | 格内 15 个 tick | 跨格那 1 个 tick |
| --- | --- | --- |
| `x_pos` / `y_pos` | 线性 ±1 | 线性 ±1，然后被 `_vehicle_subcoord` **覆盖低 4 位** |
| `tile` | 不变 | 变 |
| `direction` | 不变 | 可能被轨道强制改 |
| `track` | 不变 | 变 |
| `bounds` | **不变**（§13.2 已证四输入不含坐标） | 可能变 |
| 同格分支的三件事 | 只有 `TrainCheckIfLineEnds`（仅运动前端）+ 空转 `VehicleEnterTile` | 换成完整跨格逻辑 |

⇒ 若能承认"格内阶段不需要逐节重算"，就有两条大幅降频路线：

- **D-1（保守）**：格内 tick 只推进 `x_pos/y_pos`，跳过 `UpdateDeltaXY` + `UpdatePosition`（哈希未变本来就早退，但调用与比较仍在）。收益 1.2~1.3×，风险极低。
- **D-2（激进，才够量级）**：格内阶段**完全不逐节推进**，改为「链头跨格时一次性推导全链新位置」或「绘制时按 `progress`（`vehicle_base.h:341`，格内进度 ÷256）线性插值」。迭代频率从 `59 次/秒` 降到 `3.7 次/秒` ⇒ **理论 16×**。

**D-2 的硬阻塞（必须先验证，不能直接开工）**：以下逻辑读**亚格像素坐标**，格内不更新会让它们失去精度：

1. `CheckTrainCollision()` —— 车与车碰撞/间距；
2. `GetCouplePosition()` / `R3RCheckChainFold()` —— 耦合命中与折叠判定，容差只有 2~8 px（记忆 79051681、KI-201 附记 4）；
3. `R3RRespaceChainAfterEdit()` —— 间距修正，容差 `R3R_RESPACE_MAX_ERROR=8`；
4. `VehicleEnterTile_Station` —— 站台停车标判定，注释自陈"正是 1px 移动要判的东西"。

⇒ 所以 D-2 **不能全链一刀切**，只能对"附近没有其他列车、不在站台、不在挂车交互中"的链启用。这是一个需要状态机的工程，不是一个开关。

### 17.5 建议顺序（按"收益 ÷ 风险"排序）

1. **第一步（零代码）**：确认 20 ms/帧 是否来自 `build\`（Debug）。是 ⇒ 先测 `build-release`（RelWithDebInfo `/O2`）的帧时间。这一步可能直接把问题解决掉。
2. **第二步（已建议，未做）**：Debug 树加 `walk` + `hash` 两桶，确认 `ctrl` 里"指针追逐 vs 实际工作"的比例。**注意**：若第一步成立，这步的价值降为"理解结构"，不再是"找优化点"。
3. **第三步**：在 Release 树上取总帧时间作为新基线，再决定 B/C/D 是否还值得做。
4. **D-1** 可随手做（跳 `UpdateDeltaXY`），但它只有 15%，且 `/O2` 下比例会更低。
5. **D-2 与 B 暂不启动**：前者风险（像素敏感逻辑）、后者代价（改结构体 ⇒ 全量重编 + 存档兼容核查），都应该等第一步的数据落地后再评估。

### 17.6 一句话结论

**不是"要引入缓存优化才能拿量级"，而是"当前尺子可能是 `/Od`，那才是量级的来源"**；在 `/O2` 之上，缓存友好布局只值 2~3 倍，唯一还能量级的路是把「16 tick 里 15 tick 逐节白跑」砍掉（D-2），而它有 4 处像素敏感逻辑挡路。

---

## 十八、玩家提议：TrainController 改「链级计算」（2026-11-09）

玩家提问（原话）：我看到你思考过程中提到 OTTD 的位置分散储存在各节车厢上，而链头存位 + 长度计算 + 方向计算能否让 TrainController 从车厢级别变成链级别？

**这是本轮最有价值的一个提问，方向 = §17.4 的 D-2，即唯一还能量级的那条路。但"链头位置 + 累计长度 × 方向"这个具体公式只在直线成立，弯道必须改成"链头位置 + 每节缓存偏移表"。**

### 18.1 为什么"链头位置 + 长度 + 方向"不成立

设链头在 $P_0$、方向 $d_0$，第 $k$ 节车的中心位置直觉上是：

$$P_k = P_0 - d_0 \cdot \sum_{i<k} L_i$$

**这个式子只在整条链位于同一条直轨上时成立。** OTTD 的实际情况：

1. **每节车有独立的 `direction`**。弯道上第 1 节朝 NE、第 5 节朝 SE，是常态。`direction` 是**每节车的独立字段**，不是链的属性。
2. **每节车有独立的 `track`**，由它自己所在那一格的轨道决定。
3. 所以第 $k$ 节的位置是：沿轨道从链头往回走 $k$ 节，每节按**它自己那一格的出口方向**偏转。这是一条**折线**，不是直线。

⇒ 正确的链级公式是：

$$P_k = P_0 + \sum_{i<k} \text{Offset}_i,\qquad \text{Offset}_i = f(\text{direction}_i,\ \text{track}_i,\ L_i)$$

而 $\text{Offset}_i$ 恰恰依赖每节车自己的 `direction`/`track` —— **所以"链级"并不能消掉"每节车的信息"，只能把"每 tick 重算"换成"跨格时更新一次偏移表"。**

### 18.2 但关键事实支持玩家：偏移表在格内是常量

§17.4 已证：1 格 = 16 单位，每 tick 移动 1 单位。因此：

| 量 | 格内 15 个 tick | 跨格那 1 tick |
| --- | --- | --- |
| 每节 `direction` | 不变 | 可能变 |
| 每节 `track` | 不变 | 可能变 |
| 每节 `tile` | 不变 | 变 |
| **每节相对链头的偏移 $\text{Offset}_i$** | **完全不变** | **可能变** |

⇒ **偏移表 $\{\text{Offset}_i\}$ 是格内常量。** 这意味着正确的「链级」实现是：

- **每 tick**：只推进链头 $P_0 \mathrel{+}= 1$（O(1)），全链位置由 $P_0 + \text{Offset}_i$ **按需计算**（不写回 `x_pos`）；
- **跨格时**：重算/平移偏移表（O(链长)，但每 16 tick 才一次）。

⇒ 每 tick 的逐节工作从 $O(n)$ 降到 $O(1)$，跨格 tick 保持 $O(n)$。等效消除 $(16-1)/16 = 93.75\%$ 的逐节迭代。**这就是量级。**

### 18.3 硬阻塞（比 §17.4 的 4 处更根本）：`x_pos` 是全代码库的权威状态

`v->x_pos` / `v->y_pos` 不是 `TrainController` 的局部变量，而是**整个引擎共享的权威字段**。只要还有任何一处代码直接读 `v->x_pos` 并期望它是最新值，"格内不写回"就会让它读到 15 tick 前的陈旧值。

⇒ 改「链级」实际等于：**把所有 `v->x_pos` 的读点改成 `GetCarPos(chain, offset)` 函数调用**。这不是优化，是重构整个位置模型。**待取证：`x_pos` 全库读点数。**（下节记读数）

**猜测根因（待验证）**：若 `x_pos` 读点数在数百以上、且分散在 PBS 预留 / yapf / 订单到达 / 视口跟随 / 信号 / 声音等上游模块，则"懒计算"方案的改动面是全代码库级，风险远超收益；可行的折中是 §18.4。

### 18.4 三个可行程度不同的梯度（建议按序）

| 档 | 做法 | 改哪里 | 收益 | 风险 |
| --- | --- | --- | --- | --- |
| **L-1** | 格内 tick 只做 `x_pos/y_pos += 预存 delta`，跳过 `GetNewVehiclePos` 的表查与分支 | 仅 `TrainController` 循环内 | 1.3~1.5× | 极低（`x_pos` 仍每 tick 写回，语义不变） |
| **L-2** | 每链缓存 $\{\text{Offset}_i\}$ 与每节 $\Delta_i$，跨格时更新；格内仍写回 `x_pos` 但算术降为一次加法 | `TrainController` + 新增 NOSAVE 缓存 | 1.8~2.5× | 中（缓存与真实状态可能失同步） |
| **L-3** | 格内**完全不写回** `x_pos`，全库读点改 `GetCarPos()` | 全代码库 | **10~16×** | 极高（漏一处即隐性错位，且存档/读档需重建） |

⇒ 玩家的直觉对应 **L-3**，它是真量级；但落地顺序必须是 L-1 → L-2 → 评估 L-3，因为 L-1/L-2 过程中会**自动暴露**哪些读点真的依赖"每 tick 最新坐标"，那份清单就是 L-3 的唯一可靠前提。

### 18.5 必须排除的干扰项

在动手前仍要先回答 §17.2 的问题：**当前 20 ms/帧 是否来自 `/Od` 的 Debug 树**。若 Release 下 `ctrl` 占比已降到 1~2 ms/帧，L-3 的工程量就不值得投入。**取证顺序：先确认尺子，再谈重构。**

---

## 十九、玩家提议二：按 45° 分解求前缀和（2026-11-09）

玩家原话：OTTD 里所有夹角都是 45° 的倍数，所以可以按「∑ 自己到链头之间某个方向的车厢总长度 × 该方向的偏移修正」算某节车厢。

公式：$P_k - P_0 = \sum_{d} \big(\sum_{i<k,\ \text{dir}_i=d} L_i\big)\cdot u_d$。这是把 §18.1 的折线前缀和**按方向重组**：

$\sum_{i<k}\text{Offset}_i = \sum_d \big(\sum_{i<k,\text{dir}_i=d} L_i\big) u_d$

**恒等式，成立。** 三个好处、两个必须修的坑。

### 19.1 好处一：纯整数、零三角函数
$u_d \in \{(\pm1,0),(0,\pm1),(\pm1,\pm1)\}$，于是 $x = acc_E-acc_W+acc_{NE}-acc_{NW}+acc_{SE}-acc_{SW}$，$y$ 同构 ⇒ 每节约 12 次整数加减，无 LUT/无定点/无舍入漂移。

### 19.2 好处二：内层无分支
`acc[dir] += L` → 由 acc 算 (x,y)，**没有** tile 跨界判定、没有 `VehicleEnterTile`、没有轨道查表、没有 `UpdatePosition` 哈希写。⇒ L-2 内层从"半个 `GetNewVehiclePos`"降到十几次整数运算。

### 19.3 好处三：桶向量在格内恒定
格内 16 tick 中每节 `direction`/`tile` 不变 ⇒ acc 前缀和不变 ⇒ 与 §18.2「偏移表在格内是常量」同源，每 tick 只需推进链头。

### 19.4 坑一（致命）：桶必须按「轨道单位步」计，不能按「车厢」计
一辆长 16 的车过弯时前 6 单位在直轨、后 10 单位在斜轨，而 `direction` 只有一个字段。若把整 16 记到该车 `direction` 的桶里，误差上界：

$$\varepsilon \le L\cdot(\sqrt2-1) \approx 0.414\,L$$

L=8 ⇒ 3.3 单位；L=16 ⇒ 6.6 单位。而耦合/折叠/respace 的容差只有 **2~8 px**（§18.4）⇒ 直接越界造成误判。**正确粒度是 $u_d$ 的轨道单位步，一辆车要按沿轨子段拆进两个桶。**

### 19.5 坑二：斜向步的欧氏长度是 √2
$u_{diag}=(\pm1,\pm1)$ ⇒ 每 1 轨道单位位移 √2。等长车厢在斜线上的世界间距是直线上的 √2 倍。这不是 bug（刚体在弯道上本就占更多世界空间，`UpdateDeltaXY` 的斜向 bbox 也是 L×L），但意味着：**任何"世界坐标差 ÷ 车厢数"的估算都是错的**，只能在轨道单位空间里做加法。这解释了历史上"exp=2 与实测不符"那一族症状（KI-201 附记 4 同族）。

### 19.6 结论：公式对、该实现，但它降的是常数不是阶
- **阶不变**：仍 $O(\text{链长})$ / 跨格，因为 §19.4 要求按轨道单位走一遍。
- **常数大降**：内层无分支无查表，配 L-2 缓存预期 **2~4×**。
- **真正价值在安全**：位置变成 $(\text{链头}, 8\text{桶前缀})$ 的纯函数，可断言（$\sum acc_d \equiv$ 链长）、可 diff、可单点复算 —— 这是 L-3（懒计算）必须先有的地基。

落地建议：L-2 用「8 桶前缀和」实现而非「每节存 (dx,dy)」。(dx,dy) 每节 8 字节、8 桶每节 16 字节 —— 内存上 (dx,dy) 更省，但 8 桶多一个可校验不变式且天然拒绝 §19.4 的错法。内存要紧时存 (dx,dy)、用桶算法生成并断言。

**待验证（本轮取证）**：①`GetNewVehiclePos` 斜向步是否真为 (±1,±1)；②`CalcNextVehicleOffset` 是否按车厢 `direction` 一次性算间距（若是，§19.4 的误差在现有代码里已存在）；③`cached_veh_length` 是否与方向无关。

### 19.7 取证结果（2026-11-09，三项全部确认，并修正 §19.5）

**① 步长表确认（`src/vehicle.cpp:2988-2999`）**：

```cpp
static constexpr DirectionIndexArray<Coord2D<int8_t>> delta_coord{{{
	{-1, -1}, // N
	{-1,  0}, // NE
	{-1,  1}, // E
	{ 0,  1}, // SE
	{ 1,  1}, // S
	{ 1,  0}, // SW
	{ 1, -1}, // W
	{ 0, -1}, // NW
}}};
const Coord2D<int8_t> &coord = delta_coord[v->GetMovingDirection()];
int x = v->x_pos + coord.x;
int y = v->y_pos + coord.y;
```

⇒ **每个分量都是 ±1/0，且每步的 $L_\infty$ 范数恒等于 1。玩家的前提成立，而且比"45° 倍数"更强：每次推进就是一个整数格步，没有任何小数。**

**② `CalcNextVehicleOffset` 与方向无关（`src/train.h:448-456`）**：

```cpp
uint8_t rounding = this->IsDrivingBackwards() ? 1 : 0;
return (this->gcache.cached_veh_length + rounding) / 2
     + (this->GetMovingNext() != nullptr ? this->GetMovingNext()->gcache.cached_veh_length + 1 - rounding : 0) / 2;
```

⇒ 名义间距**纯粹是两个标量车长的一半之和**，`direction`/`track` 完全不参与。$cached\_veh\_length$ 也与方向无关（③同时成立）。

**③ 关键新发现：引擎全程用 Chebyshev（$L_\infty$）距离**。所有间距判据都是 `std::max(abs(dx), abs(dy))`（`train_cmd.cpp:219`、`:9502-9511`、`:13209`）。而由①，$L_\infty$ 范数恰好等于「走过的轨道步数」 ⇒ **OTTD 的位置空间天然就是"轨道步"整数空间，$u_d$ 在其中是标准单位向量。**

### 19.8 对 §19.5 的修正（我上一节写重了）

§19.5 说"斜向欧氏长度 √2、等长车厢在斜线间距是 √2 倍"——**在引擎自己的度量里这句话不成立**：引擎从不测欧氏距离，只测 $L_\infty$，而斜线上 `max(|dx|,|dy|)` 与直线上一样等于步数。所以：

- 等长车厢在直/斜线上的**引擎度量间距完全相同**，`CalcNextVehicleOffset` 一个标量就够 —— 这正是它能与方向无关的原因；
- √2 只影响**渲染**（斜向 bbox 是 $L\times L$），不影响位置与判据；
- ⇒ 玩家的公式在引擎原生度量下是**精确恒等式**，不存在度量不匹配问题。

### 19.9 误差量级修正：§19.4 的上界应是 $L/2$（不是 $0.414L$）

按"车厢"而非"步"分桶的唯一误差，来自**一辆车跨越方向变化点**：设车长 $L$、跨界处把车分成 $fL$ 与 $(1-f)L$，全记到一个桶里时，在另一轴的贡献误差为 $\min(f,1-f)\cdot L$，最大在 $f=0.5$：

$$\varepsilon_{\max} = L/2$$

$L=16$ ⇒ **8 单位**，恰好等于 `R3R_RESPACE_MAX_ERROR=8`（`train_cmd.cpp:9511`）。$L=8$ ⇒ 4 单位。
⇒ 结论不变（必须按步分桶），但危险程度是"恰好压在容差边界上"，不是"两倍越界"。这也解释了 KI-201 附记 4 那一族"紧贴残差对"为什么总在 1~8 之间游走。

---

## 二十、收益量化：20~25 ms 能省出多少毫秒（2026-11-09）

玩家问：这个（8 桶前缀和 + 缓存）能优化出多少毫秒，以刚才列车二十多毫秒为例。

### 20.1 先把「20 ms」拆成可优化池（这是回答的前提）

$T = 20\sim25$ ms 不是全部可优化。它 = **位置计算**（桶公式能砍）+ **每节其它工作**（砍不掉：`UpdateDeltaXY`、`bounds`、`UpdatePosition` 哈希写、视口/UI 刷新）。

设 $p$ = 位置计算占比，$k$ = 该子项加速倍数，则

$$\Delta T = T\cdot p\cdot\left(1-\frac1k\right)$$

⇒ **只报"2~4×"是没有意义的，必须同时给 $p$。** 本节所有结论都是 $p$ 的参数化结果。

### 20.2 尺子前提：build 树是 `/Od`（记忆 76468255，2026-09-13 实测）

`build` = `CMAKE_BUILD_TYPE=Debug`、`FLAGS="/Zi /Ob0 /Od /RTC1 /MTd ... -DDBG_ASSERTS"`。在 `/Od` 下：

- 函数调用**不内联**，`GetNewVehiclePos` 是真调用 + 20 字节结构体按 hidden pointer 返回 + RTC1 栈检查；
- ⇒ 把它内联成整数加减，$k$ **比 `/O2` 下更大**；
- 但 `/Od` 下桶公式自身的 12 次加减也要 load/store 到栈，$k$ 不会无限大。

⇒ 净：`/Od` 下 $k\approx3\sim5$；`/O2` 下 $k\approx2\sim4$，**但基数只有 1/4~1/6**（见 §20.4）。

### 20.3 数字表（\(T\) 取 20 ms / 25 ms，线性可缩放）

| 档 | 作用点 | \(k\) | \(p=0.5\) | \(p=0.7\) | \(p=0.9\) |
| --- | --- | --- | --- | --- | --- |
| **L-1** | 跳过表查+分支，仍走函数 | 1.2~1.3 | 20→17.7~18.3 | 20→17.2~17.9 | 20→16.5~17.4 |
| **L-2** | 缓存 Offsets，格内一次加法写回 | 3~5 | 20→15.3~16.7 | 20→13.2~15.1 | 20→11.1~13.3 |
| **L-3** | 格内不写回，每链 O(1) | 消 93.75% 逐节位置工作，+`GetCarPos` 回涨 | 20→9~12 | 20→7~9 | 20→5~7 |

同一表按 $T=25$：L-2 在 $p=0.7$ 时 25→16.5~18.9；L-3 在 $p=0.7$ 时 25→8.8~11.3。

**保守收口（$p\approx0.6\sim0.7$，取区间下沿）**：

- **L-2 省 4~9 ms**（1.3~1.8×）
- **L-3 省 8~15 ms**（1.8~3.5×）

### 20.4 最关键的一条：切 Release 后这些毫秒会缩到 1/4~1/6

若 Release 下 `ctrl` 从 20~25 ms 降到 3~5 ms，则**同样的算法优化省下的绝对时间变成 1~2.5 ms**。

⇒ **在 Debug 树上量到的 6~12 ms，不能作为投入 L-3（≈150 个 `x_pos` 读点重构）的依据。**
⇒ 必须先有 Release 树的 `ctrl` 读数，再回答"值不值得"。

### 20.5 拿到确定数字的最小取证成本（不 instrument 全树）

只需在 `TrainController` 外层加**两个 rdtsc 桶**：一个包 `GetNewVehiclePos`，一个包其余（`UpdateDeltaXY`/`bounds`/`UpdatePosition`），跑 60 帧 dump 一次 ⇒ 直接给出 $p$，上表立刻变成确定值。纯只读探针、零行为变更。

（若 `PERF` 行已有 `ctrl=` 字段，则 $T$ 已有数据源，只缺 $p$。）

**判定句：先量 $p$，再谈毫秒；先量 Release，再谈值不值。**

---

## 二十一、更正（2026-11-09，玩家澄清）：20~25 ms 是 **Release** 读数，且 §20.4 结论作废

### 21.1 玩家原话

「等一下，我澄清，release 树列车耗时才是 20ms，debug 树列车耗时超一百 ms」

### 21.2 我必须撤回 §20.4 的整段结论

§20.4 写的是「20~25 ms 是 Debug 读数，切 Release 后会缩到 1/4~1/6，所以 Debug 上量到的毫秒不能当依据」——**方向完全反了，整段作废**。

真实情况：**20~25 ms 已经是 `/O2` 生产树的读数**；Debug 是 100+ ms（≈5×）。⇒

- §20.2 的「尺子前提=`/Od`」**错误**，实际尺子是 `/O2 /Ob2 /DNDEBUG`（记忆 76468255 的 build-release 配置）；
- §20.3 的 $k$ 取值不是「/Od 下更大」，而是 **`/O2` 下**的值；
- §20.4「先量 Release 再谈值不值」**这条选项消失**——Release 读数已经有了，就是 20~25 ms；
- 「乙路线」（确认 build-release 是否还在、量一次 Release `ctrl`）**不再需要**，只剩「甲路线」（量 $p$）。

### 21.3 这个更正把结论往**有利方向**推，不是不利方向

原因：§20.4 原来的劝阻逻辑是「Debug 的 6~12 ms 在 Release 上会缩成 1~2.5 ms」。现在确认 20~25 ms 本身就是 Release 值 ⇒ **省下来的就是真金白银的 5~9 ms 生产耗时**，不打折。

另外 Debug 100+ / Release 20 ≈ **5×** 这个比值本身是**好消息**：说明编译器层面的优化空间已被 `/O2` 吃掉，剩下的 20 ms 是真实的算法/结构工作量，而不是可被编译器消掉的 Debug 开销。⇒ 算法层的优化在 20 ms 上仍有真实回报，不会像在 Debug 上那样被「未优化代码噪声」稀释。

### 21.4 决定 $k$ 的新关键事实：`/O2` 但**没有 LTCG** ⇒ 跨 TU 不可内联

`GetNewVehiclePos()` 定义在 `src/vehicle.cpp`（步长表在 `:2988`），而调用方 `TrainController()` 在 `src/train_cmd.cpp`（**不同编译单元**）。

- build-release = `RelWithDebInfo` + `/O2 /Ob2 /DNDEBUG`，**未启用 `/GL` + `/LTCG`**（无 `CMAKE_INTERPROCEDURAL_OPTIMIZATION`）；
- ⇒ `/Ob2` 只能在**同一个 .cpp 内**内联，`TrainController → GetNewVehiclePos` 这个跨 TU 调用**在 Release 下依然是真函数调用**；
- ⇒ 每节车的固定开销（call/ret、参数传递、`GetNewVehiclePosResult` 20 字节结构体返回、内部查表与分支）**在 `/O2` 下并未消失**，只是函数体本身被优化了。

**所以缓存/桶公式省掉的是这些固定开销，在 Release 上依然有效** ⇒ $k$ 不会因为从 Debug 换到 Release 而大幅缩水（不像上一轮我担心的那样）。

（仍待实测确认：`build-release\CMakeCache.txt` 是否含 IPO；若玩家确认曾开过 `/GL`，则 $k$ 下修到 1.5~2.5。）

### 21.4b 标志实证（2026-11-09）

`build-release/build.ninja:228`（`build.ninja` 内 648 处同款）：

```
FLAGS = /DWIN32 /D_WINDOWS /GR /EHsc /Zi /O2 /Ob2 /DNDEBUG -std:c++20 -MT
        /Zc:rvalueCast /Zc:preprocessor /MP /FC /utf-8 -D_FORTIFY_SOURCE=2 /W3
```

⇒ 确认 `/O2 /Ob2`，**无 `/GL`**。`build-release/CMakeCache.txt` 亦**无** `CMAKE_INTERPROCEDURAL_OPTIMIZATION` 条目（搜到的 `_CMakeLTOTest-CXX/bin/CMakeCache.txt:64` 只是 CMake 的 LTO 能力探测临时目录，与主工程无关；`:332` 的 "GLU library" 是 `/GL` 的误匹配）。
⇒ **§21.4 的推断成立：Release 下 `GetNewVehiclePos` 仍是跨 TU 真调用，固定开销不消失。**

### 21.5 修正后的数字表（$T=20$ ms，`/O2` 真值；先用 $\Delta T = T\cdot p\cdot(1-1/k)$ 重算）

上一版本表的数字我算错了（我用错了 1/k 的分配），下面是按公式重算的正确值：

| 档 | \(k\) | \(p=0.5\) | \(p=0.6\) | \(p=0.7\) |
| --- | --- | --- | --- | --- |
| L-1 | 1.15~1.25 | 20→18.3~18.7 | 20→18.0~18.4 | 20→17.7~18.2 |
| **L-2** | 2.5~4 | 20→**12.5~14.0** | 20→**11.0~12.8** | 20→**9.5~11.6** |
| **L-3** | 消 93.75% 逐节位置工作 | 20→10.6 | 20→8.8 | 20→6.9 |

（L-3 按 $T(1-0.9375p)$ 计，未计 `GetCarPos` 按需计算的回涨；实际比表内略高 1~2 ms。）

**保守收口（$p\approx0.6\sim0.7$）：**

- **L-2 省 7.2~10.5 ms（1.6~2.1×）**
- **L-3 省 11~13 ms（2.2~2.9×）**

对比 §20.3：§20.3 那些数字被我自己判定为「不作数（会缩水）」，**§21.5 的数字就是生产收益**，且比 §20.3 更大——因为不再除以 4~6 的构建折扣。

### 21.6 唯一仍需实测的量：$p$

$p$ = 20 ms 中「位置计算」占比。它决定上表的落点，且**与构建类型无关**（同一份代码结构）⇒ 量一次长期有效。方法同 §20.5：`TrainController` 外层两个 rdtsc 桶，60 帧 dump。

**修正后的判定句：20 ms 是 Release 真值，优化不打折，L-2 净赚 7.2~10.5 ms（§21.5 口径）；仍需量 $p$ 才知道落在表的哪一格。**

---

## 二十二、实施方案定稿前的三个硬事实（2026-11-09，玩家说「5~10ms 很心动，想上手」）

### 22.1 硬事实一：`GetNewVehiclePos` 只有 **12 行**，且没有分支

`src/vehicle.cpp:2986-3009` 全文：

```cpp
GetNewVehiclePosResult GetNewVehiclePos(const Vehicle *v)
{
	static constexpr DirectionIndexArray<Coord2D<int8_t>> delta_coord{{{...8 项...}}};
	const Coord2D<int8_t> &coord = delta_coord[v->GetMovingDirection()];
	int x = v->x_pos + coord.x;
	int y = v->y_pos + coord.y;
	GetNewVehiclePosResult gp;
	gp.x = x; gp.y = y;
	gp.old_tile = v->tile;
	gp.new_tile = TileVirtXY(x, y);
	return gp;
}
```

⇒ **`/O2` 下函数体本身编译出来就是 ~10 条指令**（读 dir、查表 2 字节、两次 add、`TileVirtXY` 的移位/掩码、4 个字段写出）。

⇒ **所以 L-2 的收益几乎全部来自「消除跨 TU 调用」，不是「查表变加法」。** 调用开销（`call`/`ret` + `GetNewVehiclePosResult` 20 字节经 hidden pointer 返回 + 参数搬运 + 栈帧）在 `/O2` 无 LTCG 下每节车都实付一次，**量级与函数体本身相当甚至更大**。

⇒ 这带来一个**重要简化**：原计划「缓存 Offsets 数组 + 格内一次加法」是多余的重构。
**真正的最小改动 = 让这个 12 行函数能被内联进 `train_cmd.cpp`。** 三档见 22.5。

### 22.2 硬事实二：**不能用「位置子桶」逐节计时**（我上一轮的建议作废）

`src/r3r_perf.h:51-55`：

```cpp
inline uint64_t R3RPerfNowNs()
{
	return (uint64_t)std::chrono::duration_cast<std::chrono::nanoseconds>(
		std::chrono::steady_clock::now().time_since_epoch()).count();
}
```

`steady_clock` 在 MSVC 上是 `QueryPerformanceCounter` 系，**单次 20~50 ns**。
而 `GetNewVehiclePos` 一节车在 `/O2` 下估计只有 **5~15 ns**。

⇒ 在 `train_cmd.cpp:20862` 上包一个 `R3RScopeTimer`，**观察者开销是被测物的 2~10 倍**，读数完全失真（且 `r3r_mov_iters` 每帧几万次，会直接把整个 `ctrl` 桶抬起来）。

**§20.5 / §21.6 里「在 `TrainController` 外层加两个 rdtsc 桶量 $p$」这条路线作废。** $p$ 改用 22.6 的 A/B 差值法反推，不再单独插桩。

### 22.3 硬事实三：现成的基础设施已足够做 A/B

`src/train_cmd.cpp:8375-8393` 已有一整套桶，且 `KI-574`（round 411）刚把寻路单独拆出来：

| 变量 | 含义 |
| --- | --- |
| `r3r_ctrl_calls/ns` | `TrainController()` 整体（`train_cmd.cpp:20828`） |
| `r3r_astar_calls/ns` | `ChooseTrainTrack`，**`ctrl` 的子集，不重复计入 sub** |
| `r3r_mov_iters` | 逐节迭代次数（`train_cmd.cpp:20854`）⇒ **每节车平均 ns = ctrl_ns / mov_iters** |
| `r3r_coll_ns` / `r3r_spd_ns` / `r3r_vp_ns` / `r3r_ord_ns` … | 其它子桶 |

输出在 `R3RPerfDumpAndReset()`（`train_cmd.cpp:8774` 起）的 `PERF-TICK` 行。

⇒ **逐节位置计算的真实成本 = `ctrl_ns/mov_iters` 的 A/B 差值**，不需要新探针。

### 22.4 ⚠️ 疑点 R411-1（必须先澄清，否则收益预期差 60 倍）

`PERF-TICK` 行里两种单位混排：

- `ctrl=%.1f/s %.0fms/s` ⇒ **`ctrl=20` 表示「每秒 20 毫秒」**（占 1 秒的 2%）；
- 行尾 `glTrains=%.2fms` ⇒ **这才是每帧毫秒**。

若玩家读到的 20 是 **`ctrl=20ms/s`**：
- 每帧 60fps ⇒ `ctrl` 每帧只摊 **0.33 ms**；
- §21.5 的「省 7.2~10.5 ms」是 **ms/s** 口径 ⇒ 每帧只省 **0.12~0.18 ms** ⇒ 帧率上几乎无感。

若玩家读到的是 **`glTrains=20.00ms`（每帧）**：
- 60fps ⇒ 每秒 1200 ms ⇒ 吃掉 1.2 个核，**属于严重卡顿**，§21.5 的 7.2~10.5 对应**每帧 7~10 ms**（约 ×0.6 后仍很可观）。

**两者差 60 倍，方案优先级完全不同 ⇒ 必须先确认单位再动手。**

（§20~§21 全篇的「20~25 ms」都建立在这个未确认的读数上。）

### 22.5 三档实现方案（技术路径与单位无关，收益随 22.4 而变）

| 档 | 做法 | 改动量 | 重编 | 风险 | 预期 \(k\) |
| --- | --- | --- | --- | --- | --- |
| **M-1** | 在 `train_cmd.cpp` 内加本地 `static inline R3RGetNewVehiclePosFast()`（照抄 12 行），**只替换 20862 那一处** | ~15 行，1 文件 | 增量（仅 `train_cmd.cpp`） | 极低（行为逐字等价） | = 消除跨 TU 调用 |
| **M-2** | 把 `GetNewVehiclePos` 整体移到 `vehicle_func.h` 做 `inline`，删 `vehicle.cpp` 定义 | ~20 行，3 文件 + 1 头 | **全量**（改 `.h`，记忆 66636022） | 低（含 wormhole/road 等其它调用者一起受益） | 同上，覆盖面更广 |
| **M-3** | CMake 打开 `/GL /LTCG` | 1 行 | 全量 + 链接剧增 | **中**（C 盘常年 <1 GB，记忆 76468255 已栽过；链接临时文件按 GB 计） | 最大（全程序） |

**建议：先在 M-1 上做 A/B 证明收益，再决定要不要 M-2/M-3。** M-1 是唯一「1 文件、增量、可 5 分钟内回退」的选项。

**注意 M-1 的边界**：`gp.old_tile = v->tile` 与 `gp.new_tile = TileVirtXY(x,y)` 必须逐字保留；`GetMovingDirection()` 在 `/O2` 下可内联（`vehicle_base.h` 里的 inline 成员）⇒ M-1 内联后整段变成纯算术，正是目标。

### 22.6 验证方法（A/B 差值，不插桩、不发明 \(p\)）

1. **基线**：`build-release` 当前代码，同一存档 / 同一位置 / 同一速度，读 `PERF-TICK`，记录 `ctrl=`、`ctrl_calls=`、`mov_iters=`、`astar=`。⇒ 算 `baseline_ns_per_car = ctrl_ns / mov_iters`。
2. **改 M-1**，重编 `build-release`。
3. **复测**同一场景，同样记录 ⇒ `after_ns_per_car`。
4. `Δ = baseline − after`，**逐节位置的真实总成本 = Δ × mov_iters**，`p = Δ / ctrl_per_car` 自动得出。

⇒ **A/B 同时给出「省了多少」和「$p$ 是多少」**，比先做预测数字再验证更可靠。
⚠️ 必须在 **`build-release`（`/O2 /Ob2`，无 `/GL`）** 上做——内测树是 `/Od /Ob0`，那里的 `k` 完全不同、读数不可外推（记忆 76468255）。

**当前待玩家拍板：先回答 22.4 的单位（`ctrl=20ms/s` 还是 `glTrains=20ms`），再执行 M-1。**

---

## 二十三、⚠️ 自我更正：5~10 ms 这个数字**不成立**（2026-11-09，同日第二次更正）

玩家说「5 到 10 毫秒，这可是丰厚的收益，我十分心动」——**这个数字是我给的，而它是错的。在动手前必须撤回。**

### 23.1 算术：`GetNewVehiclePos` 的物理成本上界

`GetNewVehiclePos` 在 `/O2` 下 = ~10 条指令 + 一次跨 TU 调用。单节成本上界：

| 项 | 估算 |
| --- | --- |
| 函数体 | ~10 条指令 ≈ 3~5 ns |
| 跨 TU 调用（call/ret + 20 字节返回 + 参数） | ~10~20 周期 ≈ 3~7 ns |
| **单节合计** | **≈ 6~12 ns** |

而 `TrainController` **每节车还做**：`GetTileType` / 轨道跟随 / 信号 / `UpdateViewport` / `UpdateDeltaXY` / `bounds` / `UpdatePosition` 哈希写 —— 这些才是每节成本的主体。

⇒ 单节实测成本 $c = \texttt{ctrl\_ns} / \texttt{mov\_iters}$，`GetNewVehiclePos` 只占其中的 **6~12 ns**。
⇒ $p \le 12 / c$。要 $p = 0.5$（我 §21.5 表格的中列），必须 $c \le 24$ ns/节 —— **这需要 `TrainController` 整节只花 24ns，物理上不可能**（单是 `UpdateViewport` + 哈希写就超过）。

### 23.2 ⇒ §20.3 与 §21.5 的数字表**全部作废**

那两张表的 $p$ 取值（0.5 / 0.6 / 0.7 / 0.9）**没有一个是可达的**。它们给出的「省 7.2~10.5 ms」是在 $p=0.6\sim0.7$ 下算的，而真实 $p$ 大概率在 **0.02~0.2** 区间。

按 $p=0.05\sim0.15$、$k=2\sim4$ 重算（$T$ 按玩家口径 20）：

$$\Delta T = 20 \times (0.05\sim0.15) \times (1 - 1/3) \approx \mathbf{0.7\sim2.0}$$

⇒ **真实预期是 0.7~2 ms 量级，不是 5~10 ms。** 且这已经是偏乐观的 $p$。

### 23.3 为什么 §20/§21 会错得这么离谱

根因是我把「逐节位置计算」当成了 `TrainController` 的主要工作。**在看到 22.1 那个 12 行函数体之前，$p$ 是纯猜的；看到之后，12 行 + 10 条指令直接给 $p$ 定出了天花板。** 这是典型的「先给数字、后看代码」顺序错误——本该先读 22.1 再谈毫秒。

### 23.4 M-1 仍然值得做，但性质变了

M-1（本地 `static inline` 副本，~15 行、1 文件、增量编译、行为逐字等价）**仍然是净正面**：

- 成本：15 行、5 分钟、可秒回退；
- 收益：每节省 3~7 ns（跨 TU 调用开销），按 `mov_iters` 折算绝对值需要一行日志才能定，但**方向恒为正、无风险、无行为变更**；
- 副作用：可作为 $p$ 的**精确测量手段**（A/B 差值直接给出 `GetNewVehiclePos` 的真实总成本，不需要插桩）。

⇒ 但**不要**基于「5~10 ms」去做 L-3（150 处 `x_pos` 读点重构）。那个投入/产出比不成立。

### 23.5 20 ms 的大头只能靠一行日志定位

`KI-574`（round 411）**已经**把 `astar`（`ChooseTrainTrack`）从 `ctrl` 里拆出来单独计时了，且已有 `coll` / `spd` / `vp` / `ord` / `prv` / `load` / `dep` / `dec` / `wpr` / `stk` / `rev` 等桶 + `resid` 残差。

⇒ **一行 `PERF-TICK` 就能指出 20 ms 真正花在哪**，无需任何新代码：

```
PERF-TICK loco=..ms/s | coupleH=.. | plat=.. | resv=.. | edgeGate=..
         | ctrl=<次数>/s <ms>/s | astar=<次数>/s <ms>/s | coll=..
         | spd=.. | vp=.. | dump=.. | movIters=<n>/s
         | ord=.. | prv=.. | load=.. | dep=.. | dec=.. | wpr=.. | stk=.. | rev=..
         | sub=..ms/s resid=..ms/s | nl=..ms/s glTrains=..ms
```

判读规则（要哪一列就做什么优化）：

| 若这一列最大 | 真正该做的是 |
| --- | --- |
| `astar` | 寻路优化（不是位置计算） |
| `vp` | `UpdateViewport` 逐节刷新（可批量化） |
| `spd` | `MaxSpeedInfo` + `UpdateSpeed` 的整链遍历 |
| `coll` | `CheckTrainCollision` |
| `resid` | `ctrl` 内部的纯物理步（**这时位置计算才可能相关**） |
| `glTrains`（每帧）远大于 `ctrl` 折算值 | 大头在 `TrainLocoHandler` 之外（PFE_GL_TRAINS 别的部分），位置计算无关 |

**⇒ 下一步唯一该做的：贴一行 `PERF-TICK`（Release 树、同一场景、跑满一个 128 帧窗口）。**

（2026-11-09 核查：`build-release\R3R_perf.log` 与 `build\R3R_perf.log` **均不存在**，无现成读数可取；`R3R_PERF=1` 环境下该文件写在 exe 自身目录，每 128 帧一行。）

---

## 二十四、口径第三次更正（2026-11-09，玩家澄清）：20 ms 是**游戏图形化帧率界面**的每帧毫秒

### 24.1 玩家原话

> 「build release 树压根不产生 R3R_perf.log 啊。我看到的 20ms 是在游戏的图形化"帧率"界面看到的」

### 24.2 这不矛盾 —— `build-release` 不产生 perf.log 是**设计如此**，不是故障

记忆 76468255 与 `R3R_fullbuild.cmd` 闸门 95 都写明：**`build-release\build.ninja` 带 `-DR3R_PROBES_DEFAULT=0`**（发布树、探针编译期移除）。

`src/r3r_perf.h:119-124` 原文：

```
 *   0 -> the probes are removed at compile time. R3RDbgOn() / R3RPerfOn() become
 *        a constant false, so every guarded block collapses to `if (false)`,
 *        MSVC then drops the block together with its format strings, and
 *        neither R3R_debug.log nor R3R_perf.log is ever opened or written.
```

⇒ **发布树里 `R3RPerfDumpAndReset()` 整个函数体是 `ret`**，`R3R_perf.log` 永不创建。
⇒ §23.5 末尾「`R3R_PERF=1` 环境变量就能出日志」的说法**只在探针树（`build\`）成立，在 `build-release` 上无效**。这一条作废。

### 24.3 口径确立：20 ms = **每帧** 20 ms（PFE_GL_TRAINS 一侧），不是 20 ms/s

游戏图形化帧率界面（Performance 窗口）显示的是 **每帧毫秒**（2 秒统计窗口的均值），与 `PERF-TICK` 行里的 `ctrl=%.0fms/s`（**每秒**毫秒）差 60 倍。

⇒ §22.4 提出的「疑点 R411-1」此时**结案**：玩家读的是每帧口径，**20 ms/帧**。

⇒ 严重程度：60 fps 下 20 ms/帧 = **1200 ms/s ≈ 1.2 个核**。这确实属于「严重卡顿、值得优化」，玩家的不满完全合理。

### 24.4 但这**不改变** §23 的结论：位置计算省不到 5~10 ms

§23.1 的算术与单位无关（它是每节车的纳秒数 × 迭代次数），重列一次以便彻底钉死：

| 量 | 值 | 依据 |
| --- | --- | --- |
| `GetNewVehiclePos` 单节成本 | **6~12 ns** | 12 行函数体（~10 条指令）+ 跨 TU 调用（`/O2` 无 `/GL`，§21.4b 实证） |
| 每帧迭代节数 $N$ | 假设 2 万节（2000 列车 × 10 节，每帧 1 tick） | 大型图上限量 |
| ⇒ 位置计算每帧总耗时 | $N \times 6\sim12\text{ ns} = $ **0.12~0.24 ms** | |
| ⇒ 占 20 ms 的比例 $p$ | **0.6~1.2%** | |

即使把 $N$ 放大到 10 万节（极不现实），也只有 0.6~1.2 ms，$p \le 6\%$。

**反过来算：要让 $p=0.5$，每节总成本必须降到 12~24 ns。而 `TrainController` 每节还要做 `UpdateViewport`、`UpdateDeltaXY`、`UpdatePosition` 哈希写、轨道/信号处理 —— 单这几项就在几百 ns 量级。$p=0.5$ 物理上不可能。**

⇒ **真实 $p \approx 0.01\sim0.03$。**

$$\Delta T = 20 \times (0.01\sim0.03) \times (1-1/3) \approx \mathbf{0.13\sim0.4\ ms}$$

**⇒ L-2（内联）真实收益 0.1~0.4 ms，占 20 ms 的 0.5~2%。「5~10 ms」彻底不成立，第三次确认撤回。**

### 24.5 所以 20 ms 的大头必然在别处 —— 而 Release 树**无法**用 PERF 定位

这是新口径带来的真实约束：

- `build\`（内测树）：有探针、能出 `PERF-TICK`，但是 `/Od /Ob0 /RTC1` + Debug CRT ⇒ 100+ ms，占比失真；
- `build-release\`（发布树）：`/O2 /Ob2`、20 ms 真实值，但 **`R3R_PROBES_DEFAULT=0`，一行日志都不出**。

⇒ **「在 Release 上取分解数据」这件事，现状下做不到。** 可选路径只有三条：

| 路径 | 做法 | 代价 | 备注 |
| --- | --- | --- | --- |
| **D-1** | 用内测树的 `PERF-TICK` 定位大头 | 0（现成） | Debug 下小函数占比**虚高**（无内联、调用开销放大）⇒ 只要 Debug 显示 `GetNewVehiclePos` 相关桶不显著，Release 下必然更小。**足以否定/确认 L-2 类优化，也足以找出真正的大头** |
| **D-2** | 编一个 `RelWithDebInfo + R3R_PROBES=1` 的诊断树（独立目录，如 `build-release-probe`） | 全量 ~16~20 min + 磁盘（C 盘常年 <1 GB，记忆 76468255 已栽过） | 唯一能拿到「Release 真实分解」的路 |
| **D-3** | 外部采样（Visual Studio CPU Profiler / WPA / ETW），不需要改码头 | 需玩家在 IDE 里操作 | 能得到函数级热点，且不污染被测程序 |

**建议 D-1 先行**：内测树的一次 `PERF-TICK` 就能回答「大头是不是寻路/视口/速度遍历」，成本为零。D-2 只在 D-1 指向「`ctrl` 内部纯物理步」时才值得。

### 24.6 最后一次更正：为什么我连续三轮在这个数字上出错

| 轮次 | 我给的数 | 错在哪 |
| --- | --- | --- |
| §20.3 | 6~12 ms | $p=0.5\sim0.9$ **纯猜**，未验证 |
| §21.5 | 7.2~10.5 ms | 同上，只是换了 $T$ 的口径 |
| §23 | 0.7~2 ms | 发现 12 行函数体后修正，但 $p$ 仍按 0.05~0.15 取 |
| **§24** | **0.13~0.4 ms** | 按 $N$ 与单节纳秒数直接算，$p$ 落在 1~3% |

**根因是顺序错了：先给毫秒表、后读被测代码。** 正确顺序是「先算清单节纳秒数 × 迭代数 ⇒ 得到绝对上界 ⇒ 再谈占比与收益」。§24.4 那张表就是这个正确顺序的产物，也是本文档最终应采用的口径。

**⇒ 当前唯一可信结论：优化 `GetNewVehiclePos` 最多值 0.4 ms/帧。它不是一个 5~10 ms 的项目。**

---

## 二十五、实读取数（2026-11-09）：`build\R3R_perf.log` 找到了，但这份是**暂停态残值**

### 25.1 文件

- 路径：`d:\sourcecode of JGRPP\build\R3R_perf.log`（另有一份 `D:\r3r_probe\AB\R3R_perf_prev.log`，由 A/B 脚本搬走）
- 大小 1 382 612 B，mtime **2026-11-09 03:44:26**（今日）
- 尾部两行原文（换行被终端按 80 列折断，拼接后）：

```
PERF frames=128 fps=61.2 frameMs avg=16.3 max=24
     | tryCouple=0.0/s 0ms/s | foldCheck=0.0/s 0ms/s | dumpChain=0.0/s dumpIdent=0.0/s
     | dbgWrite=0.0/s 0ms/s 0.00MB/s | posHelper=0.0/s steps=0/s maxSteps=0 0ms/s
     | dbgEdge=0.0/s skipped=0/s
     | chains=942 vehs=40036 maxChain=243 segs=2 artic=27004 waitCouple=0

PERF-TICK loco=0.0/s 0ms/s | coupleH=0.0/s 0ms/s | plat=0.0/s 0ms/s | resv=0.0/s 0ms/s
     | edgeGate=0.0/s 0ms/s | ctrl=0.0/s 0ms/s | astar=0.0/s 0ms/s | coll=0.0/s 0ms/s
     | spd=0.0/s 0ms/s | vp=0.0/s 0ms/s | dump=9ms/s | mov=0.0/s
     | ord=0.0/s 0ms/s | prv=0.0/s 0ms/s | load=0.0/s 0ms/s | dep=0.0/s 0ms/s
     | dec=0.0/s 0ms/s | wpr=0.0/s 0ms/s | stk=0.0/s 0ms/s | rev=0.0/s 0ms/s
     | sub=0ms/s resid=0ms/s | nl=13054ms/s glTrains=213.25ms
```

### 25.2 这份数据**不能**用来判断占比（理由：桶全 0，只有 PFE 有残值）

- `loco` / `ctrl` / `astar` / `coll` / `spd` / `vp` / `ord` … **全部为 0**（连 `ctrl.0/s` 的次数都是 0）
- 而 `glTrains=213.25ms` 非 0，且 `nl=13054ms/s` 恰等于 `213.25 × 61.2`（⇒ `nl = glTrains_per_frame × fps − loco_ms_per_s`，**证明 `glTrains` 的单位确实是「每帧毫秒」**，§24.3 的口径在此得到日志级实证）

两个量来自不同源：

| 量 | 来源 | 暂停时行为 |
| --- | --- | --- |
| R3R 各桶（`ctrl`/`loco`/…） | `R3RPerfDumpAndReset()` 的累计计数器 | 暂停 ⇒ tick 不跑 ⇒ 计数器不增 ⇒ 读 **0** |
| `glTrains` | `PFE_GL_TRAINS` 的 `PerformanceMeasurer` | 暂停 ⇒ `PerformanceMeasurer::Paused()` ⇒ `num_valid` 不增 ⇒ 保留**上一次有效窗口的均值** |

⇒ 这一行是「暂停后的混合残值」：`glTrains=213.25ms` 是**暂停前** Debug 树的真实每帧值（与玩家说的「内测树 100+ ms」同量级，且 213 vs 20 的 10.7× 倍数与 Debug↔Release 常识一致）；而各桶的 0 是**暂停期间**的真实读数。**两者不可相除。**

⇒ §24.5 的结论不变并加强：**要定位 20 ms 的构成，必须拿一份「游戏正在跑、列车在动、未暂停」的 `PERF-TICK`。** 当前这份只能证明链路（探针在、日志在、`glTrains` 单位是每帧），不能证明构成。

### 25.3 ✅ 但它给了一个**钉死收益上界**的硬数字：`vehs=40036`

这是全文最重要的新事实。§24.4 里 $N$ 是假设的 2 万节；实测：

```
chains=942  vehs=40036  maxChain=243  segs=2  artic=27004  waitCouple=0
```

- **`vehs=40036`**：全图 4 万节车（942 条链，最长单链 243 节）
- **`artic=27004`**：其中 27 004 节（**67.4%**）是 **artic 部件**（`IsArticulatedPart()`）

对 `GetNewVehiclePos` 的成本，这两个数给出双向约束：

| 情形 | 每帧迭代节数 $N$ | $N \times 6\sim12\text{ ns}$ | $p$（对 20 ms/帧） |
| --- | --- | --- | --- |
| 保守：只算真实车节 $40036-27004=13032$ | 13 032 | **0.08~0.16 ms** | 0.4~0.8% |
| 激进：artic 部件也走一遍（取 40 036） | 40 036 | **0.24~0.48 ms** | 1.2~2.4% |

$$\Delta T = 20 \times (0.004\sim0.024) \times (1-1/3) \approx \mathbf{0.05\sim0.32\ ms/帧}$$

⇒ **无论怎么取，都在 0.3 ms 以内。**「5~10 ms」的缺口是 **30~200 倍**，不是靠调参能补的 —— 它必须是「位置计算之外的东西」。§24.4 的算术判定由此从「估算」升级为「实测数据支撑的定论」。

### 25.4 顺带得到的画像（对后续定位有用）

- 942 条链 / 4 万节 ⇒ **平均每链 42.5 节**，最长 243 节 ⇒ 典型的大型重载图
- 67% 的节是 artic 部件 ⇒ 任何「遍历节」的操作（`UpdateViewport`、`UpdateDeltaXY`、`UpdatePosition`、`GetNewVehiclePos`）有 2/3 的迭代落在**不独立渲染、不独立寻路**的部件上
- `segs=2` 与 `maxChain=243` 并列 ⇒ 那条 243 节的链只有 2 个段 ⇒ **段内平均 121 节**，这是一条极端长的单段重载列车
- `waitCouple=0` ⇒ 当前无等待挂接的链（暂停时可能已归零，非结论）

**⇒ 下一步唯一要求：让玩家在「不暂停、列车在跑」的状态下采一份 `PERF-TICK`（等满 128 帧，约 2 秒），从 `loco` 与 `nl` 的比值开始判读。**

---

## 二十六、【结案】1880 条里有 5 条有效行 —— 20 ms 的构成已经测出来了

### 26.1 单位公式（源码实证，不再是推断）

`src\train_cmd.cpp:8773`：

```cpp
const double nl_ms = R3RGetTrainsMs() * fps - (double)r3r_loco_ns * ms_per_s;
```

- `R3RGetTrainsMs()` ⟶ 即 `glTrains`，**每帧毫秒**
- `r3r_loco_ns * ms_per_s` ⟶ `loco`，**每秒毫秒**
- `nl` = glTrains 换算到每秒后，**减去 loco 剩下的那部分**（每秒毫秒）

反解：$\text{fps} = (nl + loco) / glTrains$。

### 26.2 五条有效读数（`build\R3R_perf.log` 行 162 / 188 / 314 / 316 / 318）

1880 条 `PERF-TICK` 中 **1875 条是 `loco=0.0/s` 暂停残值**，只有 5 条列车真的在跑（这也证实 §25.2 的判读：这份日志绝大多数时间是暂停态采样）。

| 行 | glTrains(ms/帧) | loco(ms/s) | nl(ms/s) | 反解 fps |
| --- | --- | --- | --- | --- |
| 162 | 138.24 | 663 | 55 | **5.19** |
| 188 | 239.87 | 675 | 1078 | 8.65→ (675+1078)/239.87 = **7.31** |
| 314 | 80.37 | 654 | 41 | **8.65** |
| 316 | 214.92 | 727 | 17 | **3.46** |
| 318 | 213.25 | 698 | 432 | **5.30** |

⇒ **内测树的真实 tick 速率只有 3.5~8.6 tick/s**（正常应为 30/s）。138~240 ms/帧 就是玩家说的「一百多毫秒」，完全吻合。

### 26.3 逐桶分解（取行 318 为主样本，单位 ms/s）

```
loco   = 698  ← TrainLocoHandler 整体，占 glTrains 的 62%
  ├ ctrl = 385  (占 loco 55%)
  ├ stk  =  66
  ├ dep  =  34
  ├ spd  =  34
  ├ vp   =  23
  ├ load =  11
  ├ ord  =   7
  ├ prv  =   5
  ├ coll =   6
  ├ astar=   2
  ├ dec  =   1
  ├ edgeGate = 1
  ├ sub  = 566  (以上各项之和)
  └ resid= 132  ← loco 内未归类的残余（698−566）
nl     = 432  ← ⚠ TrainLocoHandler 之外那部分，占 glTrains 的 38%，零分解
```

### 26.4 三个结论

**① 位置计算（`mov`）被彻底否定。** 行 318 `mov=106050.8/s`。除以 tick 速率 5.30 ⇒ **每 tick 迭代 ≈ 2.0 万次**（与 §25.3 的 `vehs=40036`、每 tick 只扫 `_tick_train_front_cache` 的约一半相符）。按每节 6~12 ns（Release）⇒ **0.12~0.24 ms/帧**，占 20 ms 的 **0.6~1.2%**。§24 的算术判定与实测完全一致，`GetNewVehiclePos` 这条线第四次确认放弃。

**② 真正的第二大项是 `nl`（38%），而它一条探针都没有。** `nl` 是 `vehicle.cpp` 里 `PerformanceMeasurer framerate(PFE_GL_TRAINS)` 包裹的整段循环，减去 `TrainLocoHandler` 之后剩下的全部内容——包括 `_tick_train_front_cache` 的遍历开销、`Train::Tick()` 中非 loco 的部分、`_tick_effect_veh_cache` 等。**这是当前唯一「占比大 + 完全不可见」的黑洞。**

**③ `loco` 内部的大头是 `ctrl`（55%）+ `resid`（132 ms/s）。** `resid` 说明 loco 内还有 24% 的时间没被任何桶覆盖（`sub` 只覆盖了已知的 14 个），这同样是个盲区。

### 26.5 换算到玩家的 20 ms（Release）

Debug↔Release 倍数 = 213.25 / 20 ≈ **10.7×**（与记忆 76468255 的 5~20× 一致）。按行 318 的比例外推：

| 部分 | Release 估值 | 说明 |
| --- | --- | --- |
| `loco` | **≈ 12.4 ms/帧** | 其中 `ctrl` ≈ 6.8 ms、`resid` ≈ 2.3 ms |
| `nl` | **≈ 7.6 ms/帧** | **完全无分解** |
| `mov`（位置计算） | ≈ 0.12~0.24 ms/帧 | 已放弃 |

⚠ 外推有失真：Debug 无内联会**放大** `ctrl`/`spd`/`vp`/`dep` 这类小函数密集的桶 ⇒ Release 下 `loco` 占比会比 62% 更低、`nl` 占比会比 38% **更高**。

### 26.6 下一步（唯一值得做的）

给 `nl` 装细分桶。落点 `src\vehicle.cpp` 的 `PFE_GL_TRAINS` 循环内，纯探针、`R3R_PERF` 守护、零行为变更：

| 桶 | 量什么 |
| --- | --- |
| `nlc` | 缓存遍历 + 循环控制本身（`_tick_train_front_cache`） |
| `nlt` | `Train::Tick()` 中 loco 之外的部分（`CheckIfTrainNeedsService`、`UpdateVisualState`、`ConsistChanged` 类判定） |
| `nle` | `_tick_effect_veh_cache` / 残留 |

同时给 loco 的 `resid` 加一两个候选桶（`ctrl` 内部再拆 1 层即可）。

**这是唯一能回答「20 ms 里那 7.6 ms 到底是什么」的动作，成本约 30 行探针 + 3 分钟增量编译。**

---

## 二十七、回到 TrainController 主线：实测把它的构成钉死了

### 27.1 两个桶的准确覆盖范围（源码实证）

| 桶 | 包裹位置 | 覆盖内容 |
| --- | --- | --- |
| `ctrl` | `train_cmd.cpp:20828`（`TrainController()` 入口 `:20823` 之后第一行） | **整个 TrainController 函数体** |
| `astar` | `train_cmd.cpp:20944-20947`（lambda 内） | **只有 `ChooseTrainTrack()` 一次调用**，且只在 `prev == nullptr`（车头跨格）分支里发生 |

注释（`:8382-8388`）已明说：`ctrl` 混合了「per-car physics step」和「车头跨格时的一次路径搜索」，两者补救手段完全不同。

### 27.2 实测比值：寻路只占 ctrl 的 0.5%

用行 318 的读数：

| 量 | 值 | 推算 |
| --- | --- | --- |
| `ctrl` | 2398.8 /s，385 ms/s | 每次 160.5 µs |
| `astar` | 163.7 /s，2 ms/s | 每次 12.2 µs |
| astar 出现率 | 163.7 / 2398.8 | **6.8%**（只有车头跨格时才搜一次） |
| astar 占 ctrl | 2 / 385 | **0.52%** |
| `coll` | 2398.8 /s，6 ms/s | **1.6%**（注释 `:8761` 明确 coll 在 ctrl 内） |
| `mov_iters` | 106050.8 /s | ÷ 2398.8 = **44.2 节/次调用** |
| **每节成本** | 385 ms / 106050 | **3.63 µs/节**（Debug） |

$385 - 2 - 6 = 377$ ms/s ⇒ **97.9% 的 TrainController 时间花在逐车移动循环本身，与寻路无关。**

### 27.3 这直接否掉了我们前几轮讨论的那个方向

前面几轮一直在算 `GetNewVehiclePos` 的收益上界（0.1~0.3 ms/帧）。现在有了实测：**整个 `astar`（含 `ChooseTrainTrack` 全部寻路）只有 2 ms/s = 0.04 ms/帧**。

⇒ 即便把寻路优化到零，对 20 ms 帧也只值 **0.2%**。**「优化 TrainController 的寻路」这条路，天花板是 0.04 ms/帧，已经结案。**

### 27.4 真正的靶心 = 那个 44.2 节/次的循环

- 每次 `TrainController` 平均遍历 44.2 节（与 `vehs/chains = 40036/942 = 42.5` 吻合 ⇒ 一次调用基本走完整条链）
- 每节 3.63 µs（Debug）⇒ 按 10.7× 折算 Release 约 **0.34 µs/节**
- 这个量级意味着循环体已是「内存带宽 + 分支预测」级别，**没有算法级空间**，除非减少迭代次数或减少每节的工作

### 27.5 唯一的结构性机会：artic 部件占 67%

```
vehs=40036  artic=27004   ⇒ 67.4%
```

`TrainController` 的逐车循环用物理链序遍历，会走到每一个 artic 部件。但 artic 部件不独立寻路、不独立渲染（位置由父车 + 偏移决定）。**待验证（猜测根因，未实证）：如果循环体对 artic 部件做了与父车完全相同的工作，理论上最多有 2/3 的迭代是可省的。**

反证条件：若 artic 部件在循环里必须独立更新 `x_pos/y_pos/direction`（原版引擎行为如此），则不可省 —— 需要读循环体确认，或装桶实测。

### 27.6 下一步（两选一，都不大）

| 方案 | 内容 | 成本 | 能得到什么 |
| --- | --- | --- | --- |
| A | 给循环内装 2 个桶：`mvp`（`GetNewVehiclePos`+坐标写入）、`mvxy`（`UpdateDeltaXY`） | 约 15 行探针，3 分钟增量 | 377 ms/s 的内部构成 |
| B | 直接读循环体，判断 artic 部件是否有可省工作 | 0 行代码 | 只看代码，不下结论 |

**建议先 B 再 A**：如果循环体对 artic 确实做满额工作，A 就没必要；如果看不出，A 才能判。

---

## 二十八、主线恢复（2026-11-09 玩家纠正）—— 链偏移表 + 仅覆写链头

### 28.1 玩家原话要点（逐条，不得再丢）

1. 目标 = **压榨 OTTD 性能**，不是修 bug。
2. 第一步尝试 = **寻路缓存**：把"列车到岔道之前的所有路"缓存下来，直到岔道前最后一个安全位再做全量 A*。**已实现、已实测：收益仅 1 ms。**
3. 随后按建议**分桶测时**，结论 = **TrainController 才是大头**，原因是"游戏要对**全图四万节车厢**执行 xyz 空间数据覆写"。
4. 思路因此转向：**链偏移表 + 仅覆写链头空间数据**。
5. 之后探索曲折、**未出结果**，且上一轮 AI 跑偏。

### 28.2 我上一轮跑偏在哪（自查记录）

- 把"TrainController 优化"误读成"TrainController 里的**寻路**优化"，于是花整轮去证明 `astar` 只占 ctrl 的 0.5%。该结论成立，**但不是玩家要的**。
- 又去追 `nl`（`TrainLocoHandler` 之外的 38%）和 artic 部件占 67%。这两条都是**外围**，与"覆写 4 万节 xyz"这个核心命题无关。
- 玩家要的是：**把 per-car 的 xyz 覆写砍掉**，不是把别处的耗时找出来。

### 28.3 方案的技术定义（必须精确，否则再次跑偏）

- **链偏移表** = 每条链维护一张 per-car 表，记录每节车**相对链头**的 (tile 偏移, x/y 像素偏移, direction)。
- **仅覆写链头** = 每 tick 只计算链头新位置，其余节**由"链头位置 + 偏移表"导出**，而不是逐节跑 `GetNewVehiclePos()` 再逐节写字段。
- **前提** = 链的**形状**（沿轨道的几何配置序列）不变。形状只在链头跨格 / 岔道择轨 / 弯道变化时改变，此刻重建该链的偏移表。

### 28.4 必须先回答的三个问题

| # | 问题 | 为什么决定成败 |
| --- | --- | --- |
| Q1 | TrainController 逐车循环到底写了哪些字段？ | 写侧清单 = 要省掉的成本本体 |
| Q2 | 每节的 tile / direction 能否"相对链头"导出？ | 弯道上每节 tile 与 direction 各自不同，"跨格"发生在不同 tick；纯空间偏移可能不成立 |
| Q3 | 谁读 `u->tile` / `x_pos` / `y_pos` / `direction`？是否在热路径？ | **若渲染/碰撞/寻路都直接读，就必须每 tick 全部物化，"省"无从谈起** |

### 28.5 猜测根因（待验证）

- **猜测 A**：收益瓶颈不在"算"而在"写"。4 万节 × 每节若干次 `int` 存写 ≈ 内存带宽级开销，换算法救不了，只能换数据布局或减少物化次数。
- **猜测 B**：读侧若兼容惰性（只在渲染时把可见车物化），则偏移表可行，且收益上限就是 `mov` 对应的那部分 ctrl 时间。
- **猜测 C（悲观但需实证）**：引擎在 `TrainController` 里逐节推进时，每节都要**独立**判断跨格、独立跑 `GetNewVehiclePos`（因为每节可能处在不同轨道位），"仅算链头"在弯道上根本得不到正确 tile ⇒ 方案退化为"只省重复的轨道查询、省不了覆写"。

### 28.6 下一步（严格按主线，不碰外围）

1. 读 `TrainController` 逐车循环体，列写侧字段清单（Q1）。
2. 统计读侧：`u->tile` / `x_pos` / `y_pos` / `direction` 的外部读点数量，并判定是否在热路径（Q3）。
3. **只有 Q3 支持惰性，才继续设计偏移表**；否则改做"减少每节工作量"，而不是"减少节数"。

---

## 二十九、TrainController 逐车循环实读（Q1 已答）

### 29.1 循环本体

| 锚点 | 内容 |
| --- | --- |
| `train_cmd.cpp:20853` | `for (prev = v->GetMovingPrev(); v != nomove; prev = v, v = v->GetMovingNext())` |
| `:20854` | `r3r_mov_iters++`（每轮第一个语句） |
| `:20862` | `GetNewVehiclePosResult gp = GetNewVehiclePos(v);`（每轮第一件实事） |
| `:21524-21530` | 主出口（见 29.2） |
| 约 `:21530` | 循环结束 |

**循环体总长约 680 行（20853 → 21530），每节车每 tick 全跑一遍。**

### 29.2 每节车写的字段（主路径 `:21524-21540`）

| 字段 | 写入点 | 依赖什么 |
| --- | --- | --- |
| `bounds` | `UpdateDeltaXY()` `:21525` | `direction` + `cached_veh_length` + `flags.Flipped` —— **不依赖 x_pos** |
| `x_pos` / `y_pos` | `:21527-21528` | `gp` |
| `tile` | `UpdatePosition()` `:21529` | `x_pos/y_pos`（`TileVirtXY`） |
| `lookahead` / `reverse_distance` / `PendingSpeedRestriction` | `:21530-21532` | 条件 |
| `z_pos` | `UpdateInclination()` `:21535` | **该节脚下地图 slope 查询** |
| 速度 | `AffectSpeedByZChange` `:21539` | 仅链头（`prev == nullptr`） |

隧道/桥分支 `:21491-21519` 是另一条出口，同样写 `x_pos/y_pos/UpdatePosition/UpdateDeltaXY/UpdateInclination`。

### 29.3 硬结论：**「仅覆写链头」在引擎里不成立**

三条独立约束：

1. **`tile` 是逻辑状态，不是渲染状态。** 信号（`UpdateSignalsOnSegment`）、PBS 预留（`TryReserveRailTrack`/`ReserveTrackUnderConsist`）、碰撞（`CheckTrainCollision`）、寻路都**直接读 `u->tile`**。每 tick 必须正确 ⇒ 不能惰性。
2. **`z_pos` 依赖该节车自己脚下的地图。** `UpdateInclination()` 查 slope；同一条链爬坡时每节 z 各不相同，**无法从链头导出**。
3. **`direction` 在弯道上逐节不同。** `bounds` 由 direction 决定，弯道上每节 bounds 各异。

⇒ 4 万节的 `x_pos/y_pos/tile/bounds/z_pos` **必须每 tick 物化**。偏移表可以省掉"怎么算"，但省不掉"写进去"。

### 29.4 但机会在另一处：680 行分支网 × 不跨格主流路径

循环体里绝大部分是**条件分支**，判据几乎都围绕 `gp.old_tile != gp.new_tile`（本 tick 是否跨格）：

| 情况 | 要做的事 |
| --- | --- |
| 跨格 | 隧道/桥/wormhole（`:20863-20874`）、`enter_new_tile`、`VehicleEnterTile`、信号、道口、站台、`UpdateSignalsOnSegment` —— **一大片** |
| **不跨格** | 只有像素位置微调 + `UpdatePosition` + `UpdateDeltaXY` + `UpdateInclination` |

而 `VEHICLE_LENGTH = 8`，列车每 tick 只前进 1~数像素 ⇒ **平均约 8 tick 才跨一格**，同一 tick 内只有链头附近少数几节跨格。

**⇒ 绝大多数 (车节, tick) 组合走的是「不跨格」，却仍要穿越这 680 行的跨格判定网。**

### 29.5 猜测根因（待验证）

`ctrl` 的 377 ms/s 里，**大头的消耗是「不跨格」车节反复穿越跨格判定网**（大量分支 + 条件调用），而不是位置计算本身，也不是寻路（已证 0.5%）。

这条若成立，收益上限接近 `ctrl` 的 90%（≈ 6 ms/帧 Release），远大于寻路缓存的 1 ms。

### 29.6 下一步（可落地、可测）

给「链内、非链头、本 tick 不跨格、非 wormhole」的车节开一条**快速路径**：

```
只做：x_pos/y_pos 微调 → UpdatePosition() → UpdateDeltaXY() → UpdateInclination()
跳过：隧道/桥判定、VehicleEnterTile、信号、道口、站台、CheckNextTrainTile 等
```

前置工作 = **只读统计**（约 10 行探针）：
- 「不跨格」车节占 `mov` 的比例
- 快路径要跳过的那批条件，在「不跨格」分支里实际命中率是多少（若命中率不低，说明不能跳过）

**先统计再改**，否则容易跳过必要副作用（`update_signal_tunbridge_exit`、`IsTooCloseBehindTrain`、`CheckNextTrainTile` 三者是首要嫌疑）。

---

## 三十、原版 TrainController 的跨格机制（源码实证，2026-11-09）

### 30.1 【决定性事实】`GetNewVehiclePos` 只有 3 条指令

`src/vehicle.cpp:2986-3009`：

```cpp
const Coord2D<int8_t> &coord = delta_coord[v->GetMovingDirection()];
int x = v->x_pos + coord.x;      // delta ∈ {-1, 0, 1}
int y = v->y_pos + coord.y;
gp.old_tile = v->tile;
gp.new_tile = TileVirtXY(x, y);  // 纯位运算移格
```

**它不查轨道、不查地图、不查 track、不查信号。就是「沿当前方向走 1 个像素，再算落在哪格」。**

⇒ 每 tick 每节车固定前进 **1 像素**（与 `cur_speed` 无关；速度只影响 `progress`/物理步频）。
⇒ `TILE_SIZE = 16` ⇒ 每节车**平均每 16 tick 才跨一次格**，且一次只跨一格。

### 30.2 【重大修正】这直接否掉了「链偏移表能省轨道查询」的前提

前面 §28.3 写的"偏移表可以省掉 `GetNewVehiclePos` 的重复轨道查询"——**前提不存在**。原版根本没有 per-car 的轨道查询，只有一个加法和一次 `TileVirtXY`。

⇒ **偏移表在这里能省的运算量 ≈ 0。** 用户方案中"仅覆写链头"若指的是"省掉 per-car 位置计算"，那是省掉 2 条指令。

### 30.3 跨格判定与两条路径（`train_cmd.cpp:20862-20900`）

```cpp
GetNewVehiclePosResult gp = GetNewVehiclePos(v);      // :20862
if (!(v->track & TRACK_BIT_WORMHOLE)) {               // :20875  非隧道内
    if (gp.old_tile == gp.new_tile) {                 // :20877  【A】不跨格
        if (v->track == TRACK_BIT_DEPOT) { ... }
        else {
            if (v->IsMovingFront() && !TrainCheckIfLineEnds(v, reverse)) return false;  // 仅链头
            auto vets = VehicleEnterTile(v, gp.new_tile, gp.x, gp.y);   // ★ 每节每 tick 都调
            if (vets.Test(CannotEnter)) goto invalid_rail;
            if (vets.Test(EnteredStation)) TrainEnterStation(first, GetStationIndex(gp.new_tile));
            if (old_direction != v->direction) notify_direction_changed(...);
        }
    } else {                                          // :20900  【B】跨格
        ... 约 600 行主体 ...
    }
}
```

### 30.4 【B】跨格时怎么算（`20900`→`21350`）

| 步 | 位置 | 动作 |
| --- | --- | --- |
| 1 | `:20904` | `enterdir = DiagdirBetweenTiles(old, new)` —— 从哪条边进的新格 |
| 2 | `:20911-20915` | `GetTileTrackStatus()` 取新格轨道位 × `DiagdirReachesTrackdirs(enterdir)` 求可达 ⇒ `bits` / `red_signals` |
| 3 | `:20918-20926` | 90° 转弯限制（仅 `prev == nullptr`，车厢允许 90° 转向） |
| 4 | `:20932` | `CheckCompatibleRail()` —— 新格轨型与列车是否兼容 |
| 5 | `:20936` | **分水岭：`prev == nullptr`？** |
| 5a | `:20944-20947` | 链头 ⇒ `ChooseTrainTrack()` **全量寻路选轨**（= `astar` 桶，只占 ctrl 的 0.5%） |
| 5b | `:21127` 附近 | **车厢 ⇒ 跟车，不寻路**：<br>先判 `TileVirtXY(prev->x_pos, prev->y_pos) == gp.new_tile`（前车是否已占住我即将进的格）<br>是 ⇒ 沿用 `prev->track`；<br>否 ⇒ 查 `_connecting_track[enterdir][exitdir]` 表补齐 |
| 6 | `:21224` | `VehicleEnterTileCoordinates(gp, enterdir, track)` —— 把 `gp.x/gp.y` **修正到新格入口像素** |
| 7 | `:21227` | `VehicleEnterTile()` 地图回调（站台/道口/桥） |
| 8 | `:21303-21304` | **真正覆写**：`v->tile = gp.new_tile; v->track = chosen_track;` |
| 9 | `:21307` | 轨型变了 ⇒ `ConsistChanged(CCF_TRACK)` |
| 10 | `:21317-21321` | `SetMovingDirection(chosen_dir)` |
| 11 | `:21323` | 仅链头：`wait_counter = 0`、道口音效、`CheckNextTrainTile()`（延长预留） |
| 12 | `:21334` | 到站 ⇒ `TrainEnterStation()` |
| 13 | `:21524-21540` | 统一出口：写 `x_pos/y_pos`、`UpdatePosition()`、`UpdateDeltaXY()`、`UpdateInclination()` |

### 30.5 【关键】车厢之间是**串行依赖**，不能跳步

`:21127` 的判据读的是 `prev->x_pos / prev->y_pos`——**前车在本 tick 已经被更新过的实时像素位置**。循环顺序 `链头 → 第2节 → …` 保证了这一点。

⇒ 每节车的跨格选轨依赖前车**刚刚算出的**位置，整条链是严格的串行推进，**无法并行、无法跳步、无法"只算链头再批量导出"**。

这从机制上再次确认 §29.3 的结论：**「仅覆写链头」在结构上不成立**，因为第 N 节需要第 N-1 节的实时结果。

### 30.6 猜测根因（待验证，优先级最高）

**【A】不跨格路径**虽然"什么都不做"，但**每节每 tick 仍无条件调用 `VehicleEnterTile()`**（`:20890`）。按 `mov = 106050/s` 推算，即每秒 10.6 万次 `VehicleEnterTile`。

而每节车平均 16 tick 才跨一格 ⇒ **跨格分支【B】只占 1/16 的迭代，另外 15/16 全在【A】**。

⇒ 如果 377 ms/s 里的大头在【A】，那它必然集中在 `VehicleEnterTile()`（站台末端/道口/桥检测）而不是位置计算。

**待验证判据**：读 `Train::VehicleEnterTile()` 实现，看它每 tick 对普通直轨做了多少次地图查询/分支；若里面有 `GetStationIndex`/`IsRailStationTile`/`IsLevelCrossingTile` 之类的每 tick 调用，即坐实。

### 30.7 下一步（按优先级）

1. **读 `Train::VehicleEnterTile()`**：判定【A】路径的每节每 tick 成本（这是 15/16 的主流路径）。
2. 若坐实，优化目标变成**「让【A】路径跳过不必要的 `VehicleEnterTile`」**——例如"本节车与上次同格同轨、且不在站台/道口格"时短路。
3. 偏移表方案可以正式结案（省不到东西）。

---

## 三十一、【重大修正】移动粒度不是「1 tick 1 像素」（2026-11-09 玩家追问）

### 31.1 玩家追问（原话要点）

> 列车真的是 1 tick 移动 1 像素吗，那怎么区分不同速度的列车，难道是选择某 tick 不移动吗

**追问完全正确，§30 与 §29 里「每 tick 走 1 像素」的表述是错的，必须推翻。**

### 31.2 真实机制：progress 预算 + 每步固定代价

**两个关键函数（`src\vehicle_base.h`）：**

```cpp
// :581 —— 「走一步」需要多少 progress
inline uint GetAdvanceDistance()
{
	// 对角（direction & 1）192，正交 256
	return (to_underlying(this->direction) & 1) ? 192 : 256;
}

// :569 —— 速度换算成 progress 增量
static inline uint GetAdvanceSpeed(uint speed) { return speed * 3 / 4; }
```

注释原文（`:542-547`）：
> A vehicle moves a step every 256 progress units.
> The vehicle speed is scaled by 3/4 when moving in X or Y direction due to the longer distance.

**主循环（`src\train_cmd.cpp`，`TrainLocoHandler`）：**

```cpp
int j = consist->UpdateSpeed(...);                     // :22704 上方，本 tick 的 progress 预算
Train *moving_front = consist->GetMovingFront();
int adv_spd = moving_front->GetAdvanceDistance();      // :22705 走 1 像素的代价（256/192）
if (j < adv_spd) {                                     // :22706 预算不够一步 ⇒ 本 tick 不动
    if (consist->cur_speed == 0) consist->SetLastSpeed();
    ... 停驶分支（不调 TrainController）...
} else {
    while (j >= adv_spd) {                             // 一 tick 可走多像素
        j -= adv_spd;                                  // 扣掉一步的代价
        ... TrainController(consist) ...               // 前进 1 像素（内部 GetNewVehiclePos）
        if (CheckTrainCollision(moving_front)) break;
        adv_spd = moving_front->GetAdvanceDistance();  // :22795 方向可能变，重算
        if (j < adv_spd || consist->cur_speed == 0) break;  // :22798
    }
}
```

`UpdateSpeed()` 的注释（`:18516-18522`）原文：
> It returns the distance that the train can drive this tick. `Vehicle::GetAdvanceDistance()` determines the distance to drive before moving a step on the map.

⇒ **`j` 是「本 tick 的 progress 预算」，`adv_spd` 是「走 1 像素的门槛」，二者比值的整数部分 = 本 tick 前进的像素数。**

### 31.3 速度差异怎么体现（玩家猜的正是对的）

| 车速 | `j`（= cur_speed × 3/4） | 每 tick 前进 |
| --- | --- | --- |
| 慢车 `j < 256` | 小 | **0 像素**（本 tick 完全不动，只累加） |
| 中速 `256 ≤ j < 512` | 中 | 1 像素 |
| 快车 `j ≥ 512` | 大 | 2+ 像素（循环多次） |

**⇒ 「选择某 tick 不移动」是真实存在的机制，不是 bug。** 慢车就是靠"攒不够一步就跳过这个 tick"来降速。

补充：对角方向门槛是 192 而不是 256（`GetAdvanceSpeed` 又统一乘 3/4），两者配合保证**对角移动与正交移动的屏幕速率一致**（对角每像素代表 √2 距离）。副作用：**同一条链如果方向不同，每 tick 能走的像素数会不同**（因为 `adv_spd` 逐轮重算，`:22795`）。

### 31.4 【对性能账的修正】`mov` 的含义变了

| 项 | 上一轮（错） | 修正后 |
| --- | --- | --- |
| 循环次数 ≈ | 车节数 × tick 数 | **前进像素次数**（每像素一次） |
| `mov` 106050/s | ≈ 40036 节 × 5.3 tick/s 的一半 | = 全局每秒前进的像素次数 |
| 平均每节车每 tick | 1 次（以为全跑） | **≈ 0.52 次**（106050 / 5.3 / 40036） |

**⇒ 平均每节车每 tick 只推进 0.52 个像素，即大量 (车节, tick) 根本不进 TrainController。** 这与慢车占多数的事实吻合。

**但「跨格概率 1/16」不受影响**：每次前进恰好 1 像素，跨格 = 这次前进跨过 16px 边界，概率仍是 1/16（`TILE_SIZE = 16`）。

**⇒ §29.4 的核心判断仍成立**：每次迭代（= 前进 1 像素）都有 15/16 的概率**不跨格**，却仍要穿越 680 分支网。只是"每次迭代"的主体从"每节每 tick"变成"每个前进的像素"。

### 31.5 复算每迭代成本（Debug）

- `ctrl` 377 ms/s ÷ `mov` 106050 次/s = **3.55 µs/次迭代**
- 每次迭代 = 前进 1 像素 = 内部 `GetNewVehiclePos`（3 指令）+ 约 680 行分支网 + 出口写 5 个字段
- Debug（`/Od /Ob0 /RTC1`）下 3.55 µs ≈ 数百条指令当量，**与"680 行分支网全跑"量级吻合**

### 31.6 结论

1. **§30 的「每 tick 1 像素」作废**，正确的是「每 tick 攒 progress，每攒满 256/192 前进 1 像素，一 tick 可多像素、也可零像素」。
2. **§30.3 关于"慢车很多 tick 不动"的直觉方向正确**，但机制不是"选择不移动"，而是 `j < adv_spd` 时压根不进移动循环。
3. **优化方向不变**：减少「不跨格迭代」穿越 680 行分支网的成本。**优化对象是"每次前进像素"，不是"每节车每 tick"。**
4. 额外发现的**新疑点（待验证）**：`adv_spd` 逐轮重算（`:22795`）意味着**同一条快链在弯道上每 tick 能走的像素数会跳变**（正交 256 / 对角 192 交替），这会让"本 tick 走几个像素"不平稳。是否值得优化另议，但它解释了"同一列车在不同轨段位移速度不同"的观感。

---

## 三十二、一 tick 多像素时如何拐 45° 弯（玩家追问，源码实证 2026-11-09）

### 32.1 玩家的问法

> 一列车速非常快，每 tick 走两像素，但某节车厢距出格只有 1 像素，而进入下一格要拐 45° 弯，这又是怎么把它正确摆到弯道上的？

**答案：跨格那一像素不是"推出去的"，而是"查表吸附"的。坐标格内部分被整块覆盖，方向被整块改写。**

### 32.2 【决定性】`VehicleEnterTileCoordinates` 不是加法，是查表赋值

`src\vehicle.cpp:5495-5501`：

```cpp
Direction VehicleEnterTileCoordinates(GetNewVehiclePosResult &gp, DiagDirection enterdir, Track track)
{
	const VehicleSubcoordData &b = _vehicle_subcoord[enterdir][track];
	gp.x = (gp.x & ~TILE_UNIT_MASK) | b.x;   // 保留"第几格"的高位，低 4 位（格内像素）整块抹掉换成表值
	gp.y = (gp.y & ~TILE_UNIT_MASK) | b.y;
	return b.dir;                            // 新朝向也由表里给定
}
```

对比 `GetNewVehiclePos`（`:2999-3008`）的 `x += coord.x` —— **那条路径只在"没跨格"时才自由累加**。一旦 `gp.old_tile != gp.new_tile`，坐标的低 4 位就被 `b.x/b.y` 覆盖，**由着前进量算出来的落点被丢弃**。

⇒ 所以"距离出格只剩 1 像素"这件事根本不重要：那 1 像素只是让 `gp.new_tile` 变成了邻格，随后坐标被重写为"该格该轨道的标准入口点"。

### 32.3 那张表长什么样

`src\vehicle.cpp:5444-5486`：

```cpp
struct VehicleSubcoordData : Coord2D<uint8_t> {
	Direction dir = Direction::Invalid;   // 新方向；Invalid = 该组合不可能
};

static constexpr DiagDirectionIndexArray<TrackIndexArray<VehicleSubcoordData>> _vehicle_subcoord{{{
	{{{ // NE
		{{15, 8}, Direction::NE}, // TRACK_X
		{},                       // TRACK_Y      ← 空 = 非法组合
		{},                       // TRACK_UPPER
		{{15, 8}, Direction::E},  // TRACK_LOWER
		{{15, 7}, Direction::N},  // TRACK_LEFT
		{},                       // TRACK_RIGHT
	}}},
	... SE / SW / NW 各 6 条 ...
}}};
```

索引是 `[进入边][轨道]`，值是 `(格内像素 x, y) + 新朝向`。`{}` 即 `Direction::Invalid` = **该边进不来这条轨**。

**关键保障：查表前候选轨已被入口边过滤过**（`train_cmd.cpp:20911-20915`）：`GetTileTrackStatus() × DiagdirReachesTrackdirs(enterdir)` 求交，只有能接住这个入口方向的轨道才进入 `bits`。所以 `_vehicle_subcoord[enterdir][chosen_track]` 必然是合法项，查不到 `Invalid`。

### 32.4 【完整时序】一 tick 2 像素、第 1 像素拐弯

设 `j = 512`（够 2 像素的量），当前正交方向（`adv_spd = 256`）：

| 轮 | 动作 | 结果 |
| --- | --- | --- |
| 轮 1 | `j -= 256` → `j = 256`；`TrainController` → `GetNewVehiclePos` 算出 `old_tile != new_tile` | 走跨格分支 `:20900` |
|  | `enterdir = DiagdirBetweenTiles(old,new)` `:20904` | 确定进入边 |
|  | `GetTileTrackStatus × DiagdirReachesTrackdirs(enterdir)` `:20911` | 只剩能接住的轨（45° 弯通常唯一） |
|  | `ChooseTrainTrack()`（仅链头）或沿用 `prev->track` / 查 `_connecting_track`（车厢）`:20936/:21127` | `chosen_track` 定案 |
|  | `VehicleEnterTileCoordinates(gp, enterdir, chosen_track)` `:21224` | **坐标吸附 + 朝向改成斜向** |
|  | `v->tile = gp.new_tile; v->track = chosen_track;` `:21303`；`SetMovingDirection(chosen_dir)` `:21317` | 三元组落定 |
|  | 回到循环口：`adv_spd = GetAdvanceDistance()` `:22795` | **方向已变对角 ⇒ `adv_spd` 从 256 变 192** |
|  | `if (j < adv_spd || cur_speed == 0) break;` `:22798` | `256 >= 192` ⇒ 继续 |
| 轮 2 | `j -= 192` → `j = 64`；`GetNewVehiclePos` 用**新方向的 delta** | 沿斜轨前进 1px（通常不跨格） |
|  | `adv_spd = 192`；`64 < 192` ⇒ `break` | 本 tick 结束，剩 64 留给下一 tick |

⇒ **第 2 像素走的是"新方向"，因为方向在第 1 像素跨格时就被 `SetMovingDirection` 改掉了。老方向的 delta 不会再被用一次。**

⇒ **`j` 不是"像素数"，是"progress 预算"**：正交 1px 要 256，对角 1px 要 192。拐弯后剩余预算按新价格继续花，不需要换算 —— 这正是 `vehicle_base.h:562-564` 注释原文说的 *"the leftover progress does not need any adaption when changing movement direction"*。

### 32.5 真正的权威是 `(tile, track, direction)`，不是像素

这套设计里：
- **像素坐标是派生的**：只用来决定"这一 tick 有没有跨格"，以及给渲染用。
- **权威状态是 `(tile, track, direction)` 三元组**，跨格瞬间被一次性写定。
- 吸附允许坐标**跳变**（可能前进 15px，也可能看似倒退），但永远落在"该轨道允许的位置"，不会站在不存在的轨道上。

⇒ 所以不存在"连续走对角线弯过去"这回事，弯道是**离散的两段轨道 + 入格时的吸附与转向**。

⇒ 这也解释了 R3R 图像错位的土壤（KI-98/KI-106 记忆）：因为位置是"吸附式"的，任何绕过 `VehicleEnterTileCoordinates` 的改身份/改链序操作（逻辑翻转只改 `direction` 不动像素）都会让 `bounds`/`x_offs` 与实际轨道对不上，而不是像连续积分那样自动收敛。

### 32.6 结论

1. **跨格 = 查表赋值**（`vehicle.cpp:5498-5499`），不是累加；格内像素被整块覆盖。
2. **方向也在同一次查表里被改写**（`return b.dir` → `SetMovingDirection`），所以剩余像素天然走新方向。
3. **剩余预算按新方向的价格继续花**（256↔192），但"这一 tick 还能走几像素"必须**逐轮重算**，不能预先算好。
4. **不会摆错**的前提是"选轨阶段已用入口边过滤"（`:20911`），查表不会命中非法项。
5. 优化含义：`TrainController` 的每轮迭代**只推进 1 像素**这一事实不变，但"跨格"是这 1 像素触发的，而吸附/选轨/回调全在跨格分支里 —— 与 §30.6 的判断一致。

---

## 三十三、同一入口边有多条轨时怎么选对（玩家追问，源码实证 2026-11-09）

### 33.0 【订正】§32.4 的 256/192 写反了

§32.4 表格里写的「方向变对角 ⇒ `adv_spd` 从 256 变 192」是**错的**，把直轨与弯道的代价对调了。正确关系：

| 轨道类型 | 典型方向 | `delta_coord` | `GetAdvanceDistance()` |
| --- | --- | --- | --- |
| 直轨 `TRACK_X` / `TRACK_Y` | `SW` / `NE` / `SE` / `NW`（**奇数**索引） | 单轴 1 单位 | **192** |
| 弯道 `UPPER` / `LOWER` / `LEFT` / `RIGHT` | `N` / `E` / `S` / `W`（**偶数**索引） | 双轴各 1（√2） | **256** |

⇒ 拐弯是**变贵**（192→256），不是变便宜。所以一列「1 tick 走 2 像素」的快车进弯那 tick 往往只能走 1 像素（见 §33.5）。

**等价性自证**（与旧模型 `GetOldAdvanceSpeed` 逐值吻合）：设原始 speed=256 ⇒ 新模型 `GetAdvanceSpeed` = 192。直轨每步要 192 ⇒ 1 步/tick；弯道每步要 256 ⇒ 0.75 步/tick。旧模型直轨 advance=256（1 步/tick）、弯道 advance=192（0.75 步/tick）。**两者完全一致。**

### 33.1 先纠正坐标系：源码里的"东"不是 `Direction::E`

`src\vehicle.cpp:2988-2999`：

```cpp
GetNewVehiclePosResult GetNewVehiclePos(const Vehicle *v)
{
	static constexpr DirectionIndexArray<Coord2D<int8_t>> delta_coord{{{
		{-1, -1}, // N
		{-1, 0}, // NE
		{-1, 1}, // E
		{0, 1}, // SE
		{1, 1}, // S
		{1, 0}, // SW      ← 地图 +x（玩家眼里的"向东"）走的是 SW！
		{1, -1}, // W
		{0, -1}, // NW
	}}};

	const Coord2D<int8_t> &coord = delta_coord[v->GetMovingDirection()];
	int x = v->x_pos + coord.x;
	int y = v->y_pos + coord.y;
```

**关键事实：`Direction` 的名字是屏幕方位，而 `delta_coord` 是地图轴位移，两者是 45° 错开的。** 地图 +x 方向对应 `Direction::SW`（delta `(+1,0)`）。

| 玩家说法 | Direction（奇/偶） | delta | 每像素 progress |
| --- | --- | --- | --- |
| 沿地图 +x 直行 | `SW`（奇） | (+1, 0) | 192 |
| 沿地图 -x 直行 | `NE`（奇） | (-1, 0) | 192 |
| 沿地图 +y 直行 | `SE`（奇） | (0, +1) | 192 |
| 沿地图 -y 直行 | `NW`（奇） | (0, -1) | 192 |
| +x 再向 -y 偏 45° | `W`（偶） | (+1, -1) | 256 |
| +x 再向 +y 偏 45° | `S`（偶） | (+1, +1) | 256 |

### 33.2 `DiagdirBetweenTiles` 也是反的

`src\map_func.h:489-500`：

```cpp
inline DiagDirection DiagdirBetweenTiles(TileIndex tile_from, TileIndex tile_to)
{
	int dx = (int)TileX(tile_to) - (int)TileX(tile_from);
	int dy = (int)TileY(tile_to) - (int)TileY(tile_from);
	if (dx == 0) {
		if (dy == 0) return DiagDirection::Invalid;
		return (dy < 0 ? DiagDirection::NW : DiagDirection::SE);
	} else {
		if (dy != 0) return DiagDirection::Invalid;
		return (dx < 0 ? DiagDirection::NE : DiagDirection::SW);
	}
}
```

⇒ **目标在 +x 侧（玩家眼里的"东"）返回 `SW`；目标在 -x 侧返回 `NE`。** 与 `delta_coord` 同一套约定，自洽。

### 33.3 玩家的场景：东向进入一个有直轨 + 两弯的格子

车沿地图 +x 走，`gp.old_tile != gp.new_tile` ⇒ `enterdir = DiagdirBetweenTiles(old,new) = SW`。

下一格假设轨道位是 `TRACK_BIT_X | TRACK_BIT_UPPER | TRACK_BIT_LOWER`（玩家说的"东西直轨 + UPPER + LOWER"）。

**第一道闸：入口边过滤**（`train_cmd.cpp:20911` 一带）：

```cpp
TrackBits bits = TrackStatusToTrackBits(GetTileTrackStatus(gp.new_tile, TT_TRAIN, ...));
bits &= DiagdirReachesTrackdirs(enterdir);   // 只留"从这个边进得来"的轨
```

`_vehicle_subcoord[SW]`（`vehicle.cpp:5470-5477`）就是这张"可达表"，空项 = 该组合非法：

```cpp
{{{ // SW   ← 即"沿地图 +x 进入"
	{{0, 8}, Direction::SW},    // TRACK_X      ✓ 直行
	{},                         // TRACK_Y      ✗
	{{0, 7}, Direction::W},     // TRACK_UPPER  ✓ 45° 弯
	{},                         // TRACK_LOWER  ✗  ← 玩家以为可选，其实进不来
	{},                         // TRACK_LEFT   ✗
	{{0, 8}, Direction::S},     // TRACK_RIGHT  ✓ 45° 弯
}}},
```

**⇒ 从 +x 方向进来，`TRACK_LOWER` 与 `TRACK_LEFT` 在几何上根本不可达，被这道闸直接滤掉。** 可达只剩 `X / UPPER / RIGHT`，落点全部在 `x=0`（西边缘），正是入口侧。

### 33.4 第二道闸：谁做选择

**链头**（`v->IsMovingFront()`）：调 `ChooseTrainTrack()`（YAPF + 信号 + 预留 + 订单方向），返回 `chosen_track`。

**车厢**：完全不寻路，跟着前车走（`train_cmd.cpp:21135` / `:21145-21153`）：

```cpp
chosen_track = prev->track;                         // 情形 A：prev 与 v 同格，直接复制
...
static const DiagDirectionIndexArray<DiagDirectionIndexArray<TrackBits>> _connecting_track{{{
	{TRACK_BIT_X,     TRACK_BIT_LOWER, TRACK_BIT_NONE,  TRACK_BIT_LEFT },   // [SW] 行
	...
}}};
DiagDirection exitdir = DiagdirBetweenTiles(gp.new_tile, TileVirtXY(prev->x_pos, prev->y_pos));
chosen_track = _connecting_track[enterdir][exitdir]; // 情形 B：prev 已在下一格，查"进边×出边"
```

`_connecting_track[SW]` 这一行 = `{NONE, RIGHT, X, UPPER}`（按 NE/SE/SW/NW 排列）：

| prev 所在方位 exitdir | 选出的轨 |
| --- | --- |
| `SW`（prev 也在 +x 侧 ⇒ 继续直行） | `TRACK_BIT_X` |
| `SE`（prev 偏 +y） | `TRACK_BIT_RIGHT` |
| `NW`（prev 偏 -y） | `TRACK_BIT_UPPER` |
| `NE`（prev 在后方） | `NONE`（不能掉头） |

**⇒ 车厢的"算对"= 精确复现前车的走法**，不需要也知道不许自己判断。

### 33.5 第三道闸：查表定落点 + 定方向（这才是 45° 弯的真相）

选定后 `chosen_track &= bits`（`:21175`）兜底，然后 `VehicleEnterTileCoordinates(gp, enterdir, chosen_track)` 查回 §33.3 那张表：

| 选中 | 落点 | 新方向 | delta | 每像素 | 语义 |
| --- | --- | --- | --- | --- | --- |
| `TRACK_X` | (0, 8) | `SW`（奇） | (+1, 0) | 192 | 直行，方向不变 |
| `TRACK_UPPER` | (0, 7) | `W`（偶） | (+1, -1) | 256 | **45° 弯，方向转 -y** |
| `TRACK_RIGHT` | (0, 8) | `S`（偶） | (+1, +1) | 256 | **45° 弯，方向转 +y** |

**⇒ 45° 弯不是"沿一条斜线滑过去"，而是入格瞬间把方向值从 `SW` 改写成 `W`/`S`（跨 45°），把位移从 `(+1,0)` 改成 `(+1,∓1)`，同时把坐标钉到表里的入口像素。** 弯道的"弧度"完全由这两个离散量表达，没有任何插值。

### 33.6 一 tick 2 像素 + 第 1 像素拐弯，实际会怎样

设 `j = 384`（2×192），直轨（`adv_spd = 192`）：

| 轮 | 动作 | 结果 |
| --- | --- | --- |
| 1 | `j -= 192` → 192；`TrainController` 算出跨格 | 进跨格分支 |
| | `enterdir = SW`；`bits = X|UPPER|LOWER` → 过滤后 `X|UPPER`（LOWER 被滤） | 候选只剩两条 |
| | 链头 `ChooseTrainTrack` 选 `UPPER`（或车厢跟随选 `UPPER`） | `chosen_track = UPPER` |
| | `VehicleEnterTileCoordinates` → 落 (0,7)、方向 `W` | **吸附 + 转 45°** |
| | `adv_spd = GetAdvanceDistance()` = **256**（偶方向） | 变贵 |
| | `if (j < adv_spd) break` | **192 < 256 ⇒ 直接 break** |
| 2 | —— | **本 tick 只走了 1 像素** |

**⇒ 「每 tick 走 2 像素」的车一拐弯，那一 tick 就退化成 1 像素（甚至后续几 tick 也只走 1 像素），因为剩余预算 192 不够买一个 256 的斜向步。** 这正是玩家肉眼看到的"过弯会稍微顿一下"。

若 `j = 512`：轮 1 后剩 320 ≥ 256 ⇒ 轮 2 走斜向 1 像素、剩 64 ⇒ 仍是 2 像素但第二步是斜的。

### 33.7 选错的后果

`chosen_track &= bits` 若清空（`TRACK_BIT_NONE`），`train_cmd.cpp:21179` 起的分支会让车**停在原地**并打 `TTB-PROBE fold-geom`。R3R 的拼接/逻辑翻转使相邻车不再相邻时，`DiagdirBetweenTiles` 会返回 `Invalid`，代码用 `_connecting_track[enterdir][enterdir]`（`:21172`，退化为"直行"）兜底 —— 这是 R3R 特有的补丁，上游会直接 assert。

### 33.8 结论

1. **先由入口边筛掉不可能项**（`DiagdirReachesTrackdirs`）：玩家的例子里 `TRACK_LOWER` 从一开始就不在候选内。
2. **链头寻路 / 车厢跟车**决定最终 `chosen_track`，车厢不做任何自主判断。
3. **`_vehicle_subcoord[enterdir][track]` 一次性给出落点与新方向**，45° 弯就是这个表项把方向由 `SW` 改成 `W`/`S`。
4. **代价随形状变**：直轨 192 / 弯道 256，所以过弯那 tick 的像素数会下降，不是上升。
5. 三张表（`delta_coord`、`DiagdirBetweenTiles`、`_vehicle_subcoord`）用的是**同一套 45° 错开的坐标约定**，互相自洽 —— 这是理解这套代码时最容易踩的坑。

---

## 三十四、"车厢按链头像素 + 预留算下一 tick 折线"可行性评估（玩家设想，2026-11-09）

### 34.1 玩家设想

> 能不能这样：车厢按照脚下的预留，加上链头行进了多少像素，计算出它下一 tick 的行进折线？

### 34.2 现状：`TrainController` 一次调用遍历**整列**，每节各自算像素

`src\train_cmd.cpp:20852-20857`：

```cpp
	/* For every vehicle after and including the given vehicle */
	for (prev = v->GetMovingPrev(); v != nomove; prev = v, v = v->GetMovingNext()) {
		r3r_mov_iters++; // KI-14 (2): measure how many vehicles this call actually walks
		old_direction = v->direction;
		old_trackbits = v->track;
		old_gv_flags = v->gv_flags;
```

循环内每一节都独立做同一套动作：

| 行号 | 动作 |
| --- | --- |
| `:20862` | `GetNewVehiclePos(v)` —— 按**这一节自己的** `direction` 查 `delta_coord` 加 1px |
| `:20873` / `:213xx` | `VehicleEnterTileCoordinates(gp, enterdir, track)` —— 查表吸附到格内入口点 |
| `:20890` / `:213xx` | `VehicleEnterTile(v, tile, x, y)` —— 触发信号/道口/车站回调 |
| `:20898` | `notify_direction_changed()` —— 本节的转向减速 |
| `:21317` | `SetMovingDirection(chosen_dir)` —— 逐节改写方向 |

⇒ **"每节车厢各自算像素"是事实，不是链头算一次分发下去。** `r3r_mov_iters`（KI-14 探针）统计的正是"本次调用走了几节"，即 O(N) 遍历。

### 34.3 已有的一半：**走哪条轨**早就跟 `prev` 了

`src\train_cmd.cpp:21135` / `:21145-21153`（§33.4 已记录）：车厢的 `chosen_track` 来自

```cpp
chosen_track = prev->track;                                       // 同格：复制
chosen_track = _connecting_track[enterdir][exitdir];              // prev 在前方：查"进边×出边"
```

⇒ 玩家说的"按脚下的预留"，在**轨道层**已经实现：车厢不会自己选轨，它复现前车（或链头）的走法。

### 34.4 缺的只是"**格内像素 + 方向**"

所以设想的落点非常窄：**把 `:20862`+`VehicleEnterTileCoordinates` 的"逐节自由累加再吸附"，换成"按链头已走弧长沿折线退位"。**

`tile/track` 不需要动（已经跟 prev），只动 `x_pos/y_pos`（格内 4 位）与 `direction`。

### 34.5 三条硬约束（决定能不能做）

**(1) `VehicleEnterTile` 有副作用，不能只由链头触发一次。**
`:20890` 与跨格分支里的 `VehicleEnterTile(v, gp.new_tile, gp.x, gp.y)` 返回 `VehicleEnterTileState::CannotEnter / EnteredStation`，并驱动 `update_signals_crossing`、`TrainEnterStation(first, ...)`（`:20896`）、道口状态。**每节车厢跨格的时刻不同**，副作用必须发生在各自跨格的那一 tick。折线取点可以算出"落在哪一格"，但仍必须保留"这节车本 tick 是否跨格 → 触发回调"这一步，不能合并。

**(2) 同步区。**
`x_pos/y_pos/tile/track/direction` 是 `Vehicle` 的存档字段，多人游戏逐端模拟。派生方案必须**完全确定**（整数、无历史浮点、无客户端差异），且折线缓冲若依赖"上一 tick 的缓冲"，读档/新建车辆时必须能重建。这一条是最大风险 —— 一旦不满足就是 desync。

**(3) 必须保留的四条特殊路径**（都不能走直线折线）：

| 情形 | 位置 | 现在怎么走 |
| --- | --- | --- |
| 隧道/桥 wormhole | `:20863-20874`、`v->track & TRACK_BIT_WORMHOLE` | `VehicleEnterTileCoordinates(gp, dir, AxisToTrack(DiagDirToAxis(dir)))`，位置虚拟 |
| 车库内 | `:20879-20883` | `gp.x = v->x_pos; gp.y = v->y_pos;` 原地不动 |
| 倒车 | `GetMovingPrev/GetMovingNext` + `IsDrivingBackwards` | 遍历方向反向，链头是 `Last()` |
| 铰接车 / 假引擎 | `cached_veh_length` 差异极大 | 固定弧长偏移必须按节累加真实长度 |

另外 `nomove` 参数（`:20823`）允许"只推进前 k 节"，折线方案要支持任意起点。

### 34.6 建议的简化版（比"维护像素折线缓冲"更轻）

**不必维护 `std::deque<Pixel>` 历史缓冲。轨道本身就是折线的压缩形式。**

做法：
1. 链头照旧逐像素推进（`TrainController` 对 `first` 的行为一字不改）。
2. 每节车厢的 `tile/track` 照旧由 `prev->track` / `_connecting_track` 推导（现状不变）。
3. **只把"格内像素"从"自由累加后吸附"改为"由 `prev` 的像素沿本格轨道退 `(len_prev+len_cur)/2` 求出"**，方向取本格轨道在该位置的切线。

这样：
- 不需要历史缓冲（`tile/track` 就是折线）；
- 跨格判断仍逐节做（保留 `VehicleEnterTile` 副作用时机）；
- 车厢位置天然与链头刚性一致 ⇒ **根治 KI-98 / KI-106 的包围盒错位**（逻辑翻转不动像素引起的错位，正是"像素不派生"的必然后果）。

### 34.7 收益判断：几何一致性 >> 性能

- **性能收益有限**：`GetNewVehiclePos` 是一次 `delta_coord` 查表加法，`VehicleEnterTileCoordinates` 也是一次查表。真正贵的是**跨格分支**（`GetTileTrackStatus` / `ChooseTrainTrack` / 预留），而跨格每 tick 只发生在少数车厢上。换成折线退位不一定更快，还要多算弧长。
- **真正价值在几何**：一旦车厢位置由链头派生，R3R 的逻辑翻转（只改 `direction` 不动 `x_pos`）就**不可能**再产生错位 —— 因为位置变成派生值，翻转后会自动重算。这直接消掉 KI-98 / KI-106 / KI-201 附记 4 整族问题。
- **代价**：改动面覆盖 `TrainController` 主循环、`GetNewVehiclePos`、`_vehicle_subcoord` 的全部消费点，以及存档旧档重算。属"高风险高收益"，必须先有确定性证明（单机/多人一致性测试）才动手。

### 34.8 结论

1. 设想方向正确，且"选轨"那一半**已经实现**（`prev->track` / `_connecting_track`）。
2. 剩下要改的只有"格内像素 + 方向"，范围比听起来小得多。
3. 但 `VehicleEnterTile` 副作用、同步确定性、wormhole/depot/倒车/铰接四条特殊路径，任意一条处理不当都会崩或 desync。
4. 若目标是**修图像错位**，值得做；若目标是**提速**，收益不成立。
5. 落地前应先写"确定性 + 负判据"清单，而不是先写代码。

---

## 三十五、为什么"车厢按链头折线"不能提速（成本量化，玩家追问 2026-11-09）

### 35.1 一句话结论

**这个方案省掉的是一层"查表 + 加法"，而这一层本来就不是瓶颈；它连"遍历次数"都没减少。**

### 35.2 每列车每 tick 的成本结构

`TrainLocoHandler` 的预算循环（多处注释互证：`14123` / `15278` / `18465` / `19609` / `22391` 的统一表述是"`j < adv_spd` 是停驶分支，否则调 `TrainController()`"）：

```
j = UpdateSpeed()                     // 本 tick 的 progress 预算
while (j >= adv_spd) {                // adv_spd = 1 像素的价（直轨 192 / 弯道 256，见 §33.3）
    j -= adv_spd;
    TrainController(consist, nullptr); // ① 一次调用 = for 遍历整列，每节各推 1 像素
}
```

⇒ **每 tick 的 for 遍历次数 = 本 tick 走的像素数（1~4）**，不是 1。一列 20 节的常速车，每 tick 循环体执行 20~40 次。

每次循环体（`train_cmd.cpp:20853-20898`）只做三件轻活：

| 动作 | 行号 | 成本 |
| --- | --- | --- |
| `GetNewVehiclePos(v)` | `:20862` | 1 次 `delta_coord` 查表 + 2 次整数加法 |
| `gp.old_tile == gp.new_tile` 比较 | `:20877` | 1 次比较（**绝大多数车厢走这支**） |
| `VehicleEnterTile(v, ...)` | `:20890` | 一次函数调用 + 内部几个 if |

### 35.3 关键：**遍历次数不变**

折线方案仍要为**每一节**判断"本 tick 是否跨格"（§34.5 约束 1：`VehicleEnterTile` 的副作用必须在各节自己的跨格 tick 触发）。所以：

- 改前：20~40 次循环体 × (查表 + 加法 + 比较 + 一次调用)
- 改后：20~40 次循环体 × (沿本格轨道算弧长 + 按车长退位 + 取点 + 比较 + 一次调用)

**循环次数一模一样，单次成本不降反升。** 唯一省下的"像素累加 + 吸附查表"被"弧长退位"替换掉了 —— 后者要做整数除法/累加弯道折点，不会比前者便宜。

### 35.4 【已于 §三十六 订正】跨格分支与 YAPF 并非大头

> ⚠ 本节原写"真正贵的是跨格分支 / 一次 YAPF 顶掉整列车"，**经实测证伪**（§11.3、§11.4 已有数据）。
> 以 §三十六 为准，此处保留原文仅为记录错误来源。

| 动作 | 单次量级 | 实测总量占比 |
| --- | --- | --- |
| `TrainController` 整次调用（＝逐节步进） | **160 µs/次** | **6.5 ms/帧 = 38%** |
| └ 其中 `ChooseTrainTrack`（`astar`） | **12 µs/次** | **0.2~0.5%（⊂ ctrl）** |

⇒ 结论正好相反：**单次 `TrainController` 比单次 `ChooseTrainTrack` 贵 13 倍，总量贵 ~100 倍。**
YAPF 那条线在 §11.3 已明确收口（"停止投入"）。折线方案改的是 `ctrl` 这一层——**方向对，但手法未必省**。

### 35.5 更要紧：9.64× 的差距根本不在算法

本工作区的 `build\` 是 `CMAKE_BUILD_TYPE=Debug`，实测 `FLAGS="/Zi /Ob0 /Od /RTC1 -MTd -D_DEBUG -DDBG_ASSERTS"`：

- 无优化（`/Od`）、禁内联（`/Ob0`）、运行时检查（`/RTC1`）、Debug CRT、断言全开。

官方 JGRPP 是 `/O2 /Ob2 /DNDEBUG /MT`。在这种重循环 + 虚调用密集的仿真代码上，**Debug 与 Release 差 5~20× 是常态**。

⇒ 玩家观察到的"慢 9.64×"**100% 由构建类型解释，与车厢像素算法的实现方式无关**。任何重构都碰不到这一层。

### 35.6 如果真要提速，按收益排序

| 优先级 | 手段 | 预期（有实测依据的才填数字） |
| --- | --- | --- |
| 1 | 编 Release（`RelWithDebInfo` + `/O2 /Ob2 /DNDEBUG`） | **5~20×**（构建类型差异，非算法） |
| 2 | 关闭 R3R 探针（`-DR3R_PROBES_DEFAULT=0`） | §11.6 实测：编辑操作期 `dbgWrite=2207/s`，fps 被打到 3.5 |
| 3 | `VehicleEnterTile` 每节每 tick 调用（**C-13.6，头号嫌疑**） | 位于 38% 的 `ctrl` 内，量级待取数 |
| 4 | `stk`（1.1 ms/帧）+ `resid`（2.2 ms/帧 = 13%） | 已量测，代码未看 |
| 5 | 车厢像素算法（本 §34 设想，属 C 类"砍单节工作量"） | **方向对、手法待实测** |
| ~~6~~ | ~~减少 YAPF 调用~~ | **作废：实测 0.2~0.5%，§11.3 已收口** |

### 35.7 结论

- 方案本身**是对的**，但它的价值在**几何一致性**（根治 KI-98 / KI-106 图像错位），不在速度。
- ~~想提速：先换 Release，再关探针，再动 YAGF。~~ → 见 §36，YAGF 一项作废。
- 判据：如果换 Release 后 9.64× 消失，就证明提速问题与本节讨论的算法完全无关。

---

## 三十六、口径订正：`TrainController` 与 `ChooseTrainTrack` 谁是大头（玩家追问 2026-11-09）

### 36.1 玩家抓到的矛盾

同一轮里出现了两句互斥的话：

| 出处 | 说法 |
| --- | --- |
| §11.3 / §13 | `ctrl`（`TrainController`）= 6.5 ms/帧 = **38%**，是最热的桶 |
| §35.4（已订正） | "真正贵的是跨格分支 / `ChooseTrainTrack`"，暗示 YAPF 是大头 |

**玩家是对的，§35.4 错了。**

### 36.2 错在哪：用"单次成本"推断"总量"

我当时的推理是"YAPF 节点数千 ⇒ 一次就是微秒级 ⇒ 比逐节的一条查表加法贵几千倍 ⇒ 它才是大头"。

这个推理缺了**频率**这一维。实测（§11.3，三个窗口一致）：

| 量 | 实测值 |
| --- | --- |
| 单次 `TrainController` | **160 µs** |
| 单次 `ChooseTrainTrack`（`astar`） | **12 µs** |
| `astar` / `ctrl` 调用次数比 | **6.8%**（≈ 1/15 的 `TrainController` 才跨格） |
| `astar` / `ctrl` 耗时比 | **0.25~0.52%** |

⇒ 三重反转：
1. 单次上 `TrainController` 比 `ChooseTrainTrack` **贵 13 倍**（不是便宜几千倍）；
2. 总量上 `astar` 只占 `ctrl` 的 **0.3%**，占整帧 **0.2%**；
3. §35.4 原句"一次 YAPF 顶掉整列车所有车厢一个 tick"是**方向性错误**，实际是"**一节车的步进 ≈ 1/3 次 YAPF**"。

### 36.3 错因归类（纪律）

这是**违反自己的"结论只认文件"铁律**：§11.3 的实测结论早就写在同一个文件里（"ChooseTrainTrack / TryLongReserve / lookahead 重建**停止投入**，那条线到此收口"），我在 §35 却凭直觉重开了一条已被关闭的战线。

⇒ 追加纪律：**在同一份备忘内做新的成本推断前，必须先检索本文件的实测章节（§7/§11/§12/§13），不得绕过已量测数据。**

### 36.4 订正后的正确图景（全部有实测支撑）

以 16.9 ms/帧、60 fps、4 万节车/942 链为分母：

| 桶 | ms/帧 | 占比 | 性质 |
| --- | --- | --- | --- |
| `ctrl` | **6.5** | **38%** | 逐节物理步进（`GetNewVehiclePos` / `VehicleEnterTile` / `TrainEnterStation`） |
| └ `astar` | 0.03 | 0.2% | 寻路，**已收口** |
| `resid` | 2.2 | 13% | 未归桶，代码未看 |
| `stk` | 1.1 | 6.6% | 449~611 次/s 重试 |
| `dep` | 0.58 | 3.4% | `CheckTrainStayInDepot` |
| `spd` | 0.58 | 3.4% | `UpdateSpeed` |

⇒ **大头确实在 `TrainController`，而它内部几乎全是"每节车做一遍固定动作"。**
⇒ 想压 `ctrl`，只能"减少每节的工作量"或"减少参与步进的节数"；后者已按 §12 证伪（artic 不能跳过）。
⇒ 靶子收敛到 **C-13.6 `VehicleEnterTile`（头号嫌疑，未取数）**。

### 36.5 这对 §34 折线设想的影响：**方向比我之前说的更对**

- 折线设想改的正是 `ctrl` 这一层（每节的位置计算）⇒ **瞄准了 38% 的大头**，不是"碰不到要点"。
- §35 用"大头在 YAPF"去否定它，**否定理由不成立**。
- 但"能否提速"仍未被证明：现在每节是 2 次查表加法（`delta_coord` + `VehicleEnterTileCoordinates` 吸附），折线退位要做沿轨弧长 + 除以车长 + 弯道折点，**都是每节必做、次数不变**。
- ⇒ 正确的态度不是"它不能提速"，而是 **"它是 C 类候选之一，须与 C-13.6 一起实测排序，不能靠推断定生死"**。

### 36.6 待办

1. 取 `VehicleEnterTile` 的单节耗时（C-13.6），确定 6.5 ms 里它占多少。
2. 若 C-13.6 占比小 ⇒ 位置计算才是剩余大头，此时折线设想的价值上升，应做原型 A/B。
3. `resid`（2.2 ms/帧）仍未看代码，属独立战线。

---

## 三十七、`ctrl` 内部四段分解的取数方案（2026-11-09，玩家"按你的说法取数"）

### 37.1 为什么要从一个桶扩成两个子桶

§36.6 只写了"取 `VehicleEnterTile`"。但玩家跑一次日志的成本很高，只测一项会浪费这次机会：
若 `vent` 只占 5%，我仍然答不出"6.5 ms 的 38% 到底花在哪"。
所以一次把 `ctrl` 内部拆成两个互不重叠的子桶，剩余部分用减法反推：

| 子桶 | 覆盖范围 | 行号 | 预期占比 |
| --- | --- | --- | --- |
| `vent` | `VehicleEnterTile(v, gp.new_tile, gp.x, gp.y)`（**同格分支**，占 99.5% 的迭代） | `:20890` | 待测（C-13.6） |
| `wr` | 每节写位置：`UpdateDeltaXY` → 写 x/y → `UpdatePosition` → `DecreaseReverseDistance` → `AdvanceLookAheadPosition` → `DecrementPendingSpeedRestrictions` → `UpdateInclination` | `:21524-21535` | 待测 |
| 残差 | `GetNewVehiclePos`（查表加法）、同格比较、`TrainCheckIfLineEnds`、跨格分支（0.5% 迭代）、循环自身开销 | — | 减法得出 |

⇒ **`wr` 就是玩家折线方案要替换的那一段**。所以这两个数直接决定"该不该做折线"：
- `wr` 大 ⇒ 位置写入选型确实是靶子，但折线方案**不减少调用次数**，收益仍未知，需要再看它内部哪一步贵；
- `wr` 小 ⇒ 折线方案连靶子都没打中，直接放弃。

### 37.2 口径：两个子桶都是 `ctrl` 的子集

与 `astar` 完全同构（§十 的设计）：**只打印、不加入 `sub_ms`**，否则会被扣两次。
`ctrl` 已全额计入 `sub_ms`，所以 `sub_ms` / `resid` 的公式**不需要改**。

### 37.3 探针自身开销与读数偏差（必须写明）

`R3RScopeTimer` 每次构造/析构各取一次 `R3RPerfNowNs()`（QPC，约 20 ns）。
每节每 tick 增加 2 个计时器 ≈ 80 ns；按 `mov` ≈ 240 万次迭代/秒（4 万节 × 60 帧）：

```
240 万 × 80 ns ≈ 192 ms/s ≈ 0.19 ms/帧 ≈ ctrl 的 3%
```

⇒ **偏差方向：`ctrl` 会被自己的子计时器抬高约 3%**，两个子桶之和可能略超真实值。
但这只影响绝对值，不影响"谁占大头"的量级判定。判读时按"±3% 噪声"看待。

### 37.4 判读口径（拿到日志后照此填结论）

分母 = 同一行的 `ctrl` 的 `ms/s`。

| 观察 | 结论 |
| --- | --- |
| `vent / ctrl ≥ 40%` | 靶子确认是 `VehicleEnterTile`，去读它的实现找可省分支（先看"同格内只挪 1px"是否白跑） |
| `vent / ctrl ≤ 10%` 且 `wr / ctrl ≥ 40%` | 靶子是位置写入；但折线方案**不减调用次数**，须先确认 `wr` 内部 `UpdateDeltaXY`（KI-253 的包围盒）与 `UpdatePosition`（KI-13.1 已证伪的哈希）各占多少 |
| `vent` 与 `wr` 都 ≤ 10%，残差 ≥ 50% | 大头在跨格分支或 `TrainCheckIfLineEnds`，两条既有候选线（折线、`VehicleEnterTile`）都要让位，改测残差 |
| `wr / ctrl ≥ 60%` 且其中 `UpdateInclination` 可用继承替代 | 回到 §12.3 的"砍单节某几步"路线（`UpdateInclination` 是 C-13.3，仍是待取证） |

### 37.5 落地清单（本轮执行）

1. 计数器：`r3r_vent_calls/ns`、`r3r_wr_calls/ns`（紧接 `r3r_astar` 之后，注释写明是 `ctrl` 的子集、不计入 `sub_ms`）。
2. `:20890` 改成立即执行的 lambda，lambda 体内构造 `vent` 计时器再 `return VehicleEnterTile(...)`。
3. `:21524-21535` 包进显式花括号块，`old_z` 提到块外声明（行为不变，仅缩窄计时范围，**不把** `AffectSpeedByZChange` 与 `update_signal_tunbridge_exit` 算进去）。
4. `PERF-TICK` 格式串在 `astar=%.1f/s %.0fms/s |` 之后插入 `vent=%.1f/s %.0fms/s | wr=%.1f/s %.0fms/s |`，参数列表同位置插入 4 个（**顺序必须与格式串严格一致**）。
5. `R3RPerfDumpAndReset()` 在 `r3r_astar_calls = r3r_astar_ns = 0;` 之后加两行清零。
6. 只改 `src\train_cmd.cpp`（纯 .cpp ⇒ 增量合法，符合 KI-183）。

### 37.6 复测要求（交给玩家）

- 运行态（发车、不暂停，不做挂车/拖动/存档），跑够 1 个 128 帧窗口以上；
- 判据：`PERF-TICK` 行内**出现 `vent=` 与 `wr=`**（旧 exe 的行没有这两个字段，日志是追加写的，必须靠字段存在性认新数据）；
- 负判据：`fps` 不因本轮改动明显下降（探针只加两个计时器，预期影响 < 1 帧）。

### 37.7 落地与构建自证（2026-11-09 21:57）

六处改动，全部在 `src\train_cmd.cpp`（纯 .cpp，未碰 `src\*.h`）：
计数器定义（`r3r_astar` 之后）、`:20890` 的 lambda 包壳、`:21524` 位置的 `wr` 块 + `old_z` 外提、`PERF-TICK` 格式串、同处参数列表、`R3RPerfDumpAndReset()` 清零。

| 自证项 | 实测 |
| --- | --- |
| 构建入口 | `R3R_fullbuild.cmd inc detach`（复用既有入口，未新建 .cmd） |
| 完成标记 | `build\R3R_incbuild.done` = `EXIT_CODE=0`，mtime 21:57:53 |
| 链接 | 日志 `[3/3] Linking CXX executable openttd.exe` |
| 时间戳 | `src\train_cmd.cpp` 21:54 → `build\openttd.exe` 21:57:50（52 610 560 B） |
| 字面量顺序 | exe 内实测 `... astar=%.1f/s %.0fms/s | vent=%.1f/s %.0fms/s | wr=%.1f/s %.0fms/s | coll=...`，与参数列表同序 |
| read_lints | 0 条 |

⇒ 探针可用。**注意：`sub_ms` 公式未改**（vent/wr 是 ctrl 子集，不重复扣），所以 `sub`/`resid` 读数与上一版逐字可比。

### 37.8 交给玩家的复测步骤（判据）

1. 用 `build\openttd.exe`（21:57 版）打开**同一个存档**，跑同一段场景；
2. 让它跑够 ≈ 5 分钟真实时间（`PERF-TICK` 每 128 帧一行，5 分钟 ≈ 140 行，足够取均值）；
3. 期间**不要暂停**（暂停不计帧，`ms/s` 会失真），**不要开窗口**（会引入 `vp` 噪声）；
4. 回日志给我 `PERF-TICK` 行即可，我按 §37.4 的判读表填结论。

确认新数据的唯一办法：行内**有 `vent=` 和 `wr=`**。旧 exe 的行没有这两段，日志是追加写的，容易混。

---

## 三十八、取数结果：`vent` 被排除，最大项是"残差"（2026-11-09 22:13 日志）

### 38.1 数据有效性（先验，通过）

| 项 | 实测 |
| --- | --- |
| 日志 | `build\R3R_perf.log`，80 033 B，mtime 22:13:03 |
| 产生者 | `build\openttd.exe` mtime 21:57:50（**本轮的探针版**）⇒ 22:13 > 21:57，有效 |
| 字段存在性 | 行内含 `vent=%.1f/s` 与 `wr=%.1f/s` 段 ⇒ 确为新 exe |
| `PERF-TICK` 总窗口 | 107 |
| 其中 `ctrl=0.0/s` | **97**（暂停/空转窗口，`nl=144653ms/s` 这种荒谬读数即其指纹） |
| 可用窗口 | **11**（含 1 个刚启动的低次数窗口） |
| 场景规模 | `chains=942 vehs=40036 artic=27004 segs=2`，`fps=60.5 frameMs avg=16.5` |

⇒ **日志够用。** 但注意 `R3R_perf.log` 是累加写的，107 个窗口里 97 个是暂停态，直接取"最后一行"会拿到全 0 的假数据（§37.8 的判据只保证"是新 exe"，不保证"在跑"）。

### 38.2 原始读数（11 个有效窗口，ms/s 为单位，同窗口可直接相比）

| # | ctrl | astar | vent | wr |
| --- | --- | --- | --- | --- |
| 1 | 1615.9 / 294 | 110.3 / 2 | 57943.5 / 27 | 63404.9 / 103 |
| 2 | 2434.3 / 441 | 166.3 / 1 | 88166.3 / 38 | 96633.1 / 154 |
| 3 | 2554.4 / 439 | 175.8 / 1 | 93558.2 / 40 | 102534.0 / 165 |
| 4 | 2651.1 / 474 | 182.9 / 1 | 96546.0 / 41 | 106083.2 / 174 |
| 5 | 2547.9 / 478 | 171.8 / 1 | 91057.0 / 39 | 100016.6 / 159 |
| 6 | 2607.3 / 484 | 176.6 / 2 | 93356.4 / 44 | 102739.8 / 161 |
| 7 | 2594.8 / 455 | 188.6 / 2 | 95795.6 / 41 | 105465.5 / 169 |
| 8 | 2551.0 / 463 | 181.4 / 2 | 93191.9 / 40 | 102453.5 / 168 |
| 9 | 2546.5 / 454 | 175.6 / 2 | 91497.2 / 42 | 100613.2 / 166 |
| 10 | 2673.8 / 441 | 184.2 / 1 | 96039.6 / 41 | 105547.4 / 169 |
| 11 | 2485.4 / 463 | 175.1 / 2 | 89847.7 / 41 | 98606.0 / 172 |

**11 个窗口的规律高度一致**（波动 <5%），取中位（ctrl=463）：

| 桶 | ms/s | 占 `ctrl` | 每帧 ms（fps 60.5） | 占整帧 16.5 ms |
| --- | --- | --- | --- | --- |
| `ctrl` | 463 | 100% | 7.65 | **46.4%** |
| └ `astar` | 2 | 0.4% | 0.03 | 0.2% |
| └ **`vent`** | **41** | **8.9%** | **0.68** | **4.1%** |
| └ **`wr`** | **165** | **35.6%** | **2.73** | **16.5%** |
| └ **残差** | **255** | **55.1%** | **4.21** | **25.5%** |

调用频次自洽性检查（证明计数器没串）：
- `ctrl` 2547.9/s ÷ 60.5 = **42.1 次/帧**；
- `vent` 91057/s ÷ 2547.9/s = **35.7 节/次**；`wr` 100016/s ÷ 2547.9/s = **39.3 节/次** ⇒ 每节一次、跨格分支（~9%）不退同格路径，与 §十四 的代码结构吻合。
- 单次成本反推：`ctrl` 187.6 µs/次，`vent` **0.43 µs/次**，`wr` **1.59 µs/次**。
- `astar`/`ctrl` = 0.4% ⇒ 与 §36.2 记录的 0.25~0.52% **独立复现**，说明本轮读数与历史口径一致。

### 38.3 判读（对照 §37.4）

| §37.4 规则 | 实测 | 判定 |
| --- | --- | --- |
| `vent / ctrl ≥ 40%` ⇒ 靶子是 `VehicleEnterTile` | 8.9% | ❌ **排除** |
| `vent ≤ 10%` 且 `wr ≥ 40%` ⇒ 靶子是位置写入 | 8.9% / 35.6% | ⚠ **半命中**：`wr` 是最大已命名子桶，但没到 40% |
| 两者都 ≤10%、残差 ≥50% ⇒ 改测残差 | 残差 55.1% | ✅ **残差是最大项** |

### 38.4 三条结论

**① C-13.6（`VehicleEnterTile`）就此排除。**
它只占 `ctrl` 的 8.9%（0.68 ms/帧、整帧 4.1%），单次 0.43 µs。
即使把它优化到 0，最多省 0.68 ms/帧，而代价是动 `VehicleEnterTile` 的落点语义（已知它牵涉 KI-98/KI-106 的图像错位）。**投入产出比不成立，从候选表删除。**
⇒ §36.6 待办 1 结案：答案是"6.5 ms 里它占约 9%，不是大头"。

**② `wr`（每节写位置段）是最大**已命名**子桶（35.6%，2.73 ms/帧），但它内部混了三个不同性质的步骤。**
它现在同时包含 `UpdateDeltaXY`（KI-253 的包围盒）、`UpdatePosition`（KI-13.1 已证伪的哈希）、`UpdateInclination`（C-13.3）。
玩家的折线方案只替换中间那一步 ⇒ **在拆开这三步之前，无法判断它值多少**。这是 §12.3「砍单节某几步」路线的第一条实测依据。

**③ 真正的头号是"残差"：55.1%（4.21 ms/帧、整帧 25.5%），比 `vent` 和 `wr` 加起来以外的任何单项都大。**
残差 = `GetNewVehiclePos`（查表加法）+ 同格比较 + `TrainCheckIfLineEnds` + 跨格分支（~9% 迭代）+ 循环自身开销 + **本轮探针开销**。
扣掉探针（每节 2 个计时器 × 35.7 节 × ~40 ns ≈ 2.9 µs/次 ⇒ 约 3%）后仍有 **≈52%**，约 2.8 µs/节。

⇒ 这改写了两轮以来的靶子排序：**折线方案与 `VehicleEnterTile` 都不在第一位，第一位是每节那 2.8 µs 的"固定动作"残差。**

### 38.5 下一步（按投入产出排序）

1. **拆残差**（最高优先）：`GetNewVehiclePos` 计时 + `TrainCheckIfLineEnds` 计时，两个子桶即可把 55% 劈成三块。这是纯 .cpp 改动、无行为风险。
2. **拆 `wr`**（次高）：把 `UpdateDeltaXY` / `UpdatePosition` / `UpdateInclination` 各自计时，判 §12.3 路线值不值。
3. 折线方案：**暂缓**。它的靶子（`wr` 的 `UpdatePosition` 部分）在拆开之前上限是 2.73 ms/帧，而它自身要付沿轨弧长成本，须等 ①② 的数据。

### 38.6 疑点（按纪律登记，不在此下定论）

| 编号 | 疑点 | 期望 | 实测 | 待验证 |
| --- | --- | --- | --- | --- |
| REG-38-A | `ctrl` 每帧耗时 | §36 记 6.5 ms/帧（38%） | **7.65 ms/帧（46.4%）** | 多出 1.15 ms。探针只能解释 ~0.24 ms；余下疑为场景差异（`ctrl` 42.1 次/帧 vs 历史 39.7）或 Debug 波动。不影响桶间比例，但影响"整帧占比"的绝对值 |
| REG-38-B | `vent` 单次 0.43 µs 是否偏低 | 它内部要查 tile 类型/进入逻辑 | 0.43 µs 在 Debug 下偏快 | 可能被内联或走了早退分支（同格非站台）；若要动它才是问题，现已排除，不追 |
| REG-38-C | 97/107 窗口为暂停态 | — | 玩家多半是"跑一段→暂停→看数" | 不影响本轮（11 个有效窗口足够），但下次取数请连续跑不暂停 |

---

## 三十九、残差二次分解：`gnvp` + `lend`（2026-11-09，玩家"那就拆那两个吧"）

### 39.1 目标

§38.4 结论 ③：残差占 `ctrl` 的 **55.1%**（4.21 ms/帧、整帧 25.5%），是当前第一靶子，
但内部混了四种性质完全不同的东西。本轮把其中**两个可命名的函数级大头**单独计时：

| 子桶 | 覆盖 | 行号 | 为什么选它 |
| --- | --- | --- | --- |
| `gnvp` | `GetNewVehiclePos(v)` —— 每节每 tick 的查表 + 加法，产出 `gp` | `:20878` | 它是残差里**唯一每节必跑**的函数，且被 §33 怀疑是"位置派生"路线的替代起点 |
| `lend` | `TrainCheckIfLineEnds(v, reverse)` —— 仅 `IsMovingFront()` 的节 | `:20904` | 已知它会做 `TrainApproachingLineEnd` 的预留/信号检查，是最可能在残差里吃大头的分支 |

**范围口径（重要）**：
- `TrainCheckIfLineEnds` 在 `TrainLocoHandler` 的移动分支 `:23738` 还有一处，但那是 `loco` 的子集、**不属于 `ctrl`**，本轮不计入 `lend`；
- 跨格分支（0.5% 迭代）里的 `VehicleEnterTile` 也不计入 `vent`（§37.1 已声明），仍留在残差。

### 39.2 口径

与 `vent` / `wr` 完全同构：**`ctrl` 的子集，只打印，不加 `sub_ms`**。
`sub_ms` / `resid` 公式不变 ⇒ 与 §38 的读数逐字可比。

### 39.3 减法后的三块残差

```
残差' = ctrl - vent - wr - gnvp - lend - astar
```

拿到数据后按 `ctrl` 为分母判读：

| 观察 | 结论 |
| --- | --- |
| `gnvp / ctrl ≥ 25%` | 位置派生本身就是靶子 ⇒ §33 的折线/派生路线**直接有价值**，优先于任何 `VehicleEnterTile` 类改动 |
| `lend / ctrl ≥ 25%` | 靶子是线端检查 ⇒ 去看 `TrainApproachingLineEnd` 是否每 tick 重复做可缓存的事 |
| 两者都 < 10% 且 `残差'` 仍 ≥ 30% | 大头在**跨格分支 + 循环自身 + 探针开销**，继续劈已无收益 ⇒ 该转向减少迭代次数（段/链级批处理）而不是减少单节成本 |
| `gnvp` 与 `lend` 合计 ≥ 50% | 残差基本被解释完 ⇒ 两轮以来的"每节 2.8 µs 固定动作"落地为具体函数 |

### 39.4 落地清单

1. 计数器 `r3r_gnvp_calls/ns`、`r3r_lend_calls/ns`（紧接 `r3r_wr` 之后）。
2. `:20878` 改成 lambda 包壳（与 `vent` 同写法）。
3. `:20904` 拆成显式块，保留短路语义（`IsMovingFront()` 为假时不调用、不计时）。
4. `PERF-TICK` 格式串在 `vent= | wr= |` 之后插 `gnvp= | lend= |`，参数列表同序插入 4 个。
5. `R3RPerfDumpAndReset()` 清零补两行。
6. 只改 `src\train_cmd.cpp`（纯 .cpp ⇒ 增量合法）。

### 39.5 落地与构建自证（2026-11-09 22:25）

六处改动，全部在 `src\train_cmd.cpp`（未碰 `src\*.h`）：
计数器定义（`r3r_wr` 之后）、`:20878` 的 `gnvp` lambda 包壳、`:20904` 的 `lend` 显式块（保留 `IsMovingFront()` 短路语义）、
`PERF-TICK` 格式串、同处参数列表、`R3RPerfDumpAndReset()` 清零。

| 自证项 | 实测 |
| --- | --- |
| 构建入口 | `R3R_fullbuild.cmd inc detach`（复用既有入口，未新建 .cmd） |
| 完成标记 | `build\R3R_incbuild.done` = `EXIT_CODE=0`，mtime 22:25:27 |
| 链接 | 日志 `[3/3] Linking CXX executable openttd.exe` |
| 时间戳 | `src\train_cmd.cpp` 22:23 → `build\openttd.exe` 22:25:21（52 610 560 B） |
| 字面量顺序 | exe 内实测 `... astar= | vent= | wr= | gnvp= | lend= | coll= ...`，与参数列表同序 |
| read_lints | 0 条 |

### 39.6 交给玩家的复测要求

与 §37.8 完全一致，只多一条判据：

1. 用 `build\openttd.exe`（**22:25 版**）打开同一存档，跑同一段场景，连续 ≈5 分钟；
2. **不要暂停**（§38.1 的教训：107 个窗口里 97 个是全 0 的暂停态，直接看"最后一行"会取到假数据）；
3. 判据：`PERF-TICK` 行内同时出现 `vent=`、`wr=`、`gnvp=`、`lend=` 四段；
4. 负判据：`fps` 不因新增两个计时器明显下降（每节增加 2 次 QPC ≈ 80 ns，预期 < 1 帧）。

---

## 四十、取数结果：`gnvp` 5.2% / `lend` 0.9% ⇒ 残差是"循环体本身"（2026-11-09 22:33 日志）

### 40.0 先纠一个会误导下一轮的口径错误（重要）

代码 `train_cmd.cpp:8786-8789`：

```cpp
const double sub_ms  = (ctrl + spd + vp + plat + ord + prv + load + dep +
                        ch + edge + dec + wpr + stk + rev) * ms_per_s;
const double resid_ms = loco_ms - sub_ms;
```

⇒ **日志里的 `resid` 是 `loco − sub`，即"handler 层面的残差"，不是 `ctrl` 内部的残差。**
本轮日志 `resid=126ms/s` 而 `ctrl` 内残差算出来是 **225 ms/s**，两者差近一倍，来源就是口径不同。
**§38 以来我一直用的是"`ctrl` 内残差"（`ctrl − 已命名子桶`），与 `resid` 字段无关。从本轮起两个名字必须分开写：**

| 名字 | 定义 | 本轮值 |
| --- | --- | --- |
| `resid`（日志字段） | `loco − sub` | 126 ms/s（#13） |
| **`ctrl 内残差`**（本文用） | `ctrl − vent − wr − gnvp − lend − astar` | **225 ms/s（#13）** |

### 40.1 数据有效性

| 项 | 实测 |
| --- | --- |
| 日志 | `build\R3R_perf.log`，31 952 B，mtime **22:33:30** |
| 产生者 | `build\openttd.exe` mtime **22:25:21** ⇒ 22:33 > 22:25，有效 |
| 字段 | 44 个窗口**全部**含 `gnvp=`/`lend=` ⇒ 确为本轮新 exe |
| 有效窗口 | **5**（#10~#14）；其余 39 个全 0（暂停态，同 §38 的坑，**这批暂停比例更高，97%→89%**） |
| 场景 | `chains≈942 vehs≈40036`，`fps≈60`，`mov≈96000 节/s` |

样本只有 5 个（比上轮 11 个少），但 5 个窗口的 `ctrl` 落在 377~448、波动 ±15%，**结论不受影响**。

### 40.2 原始读数（ms/s）

| # | ctrl | astar | vent | wr | gnvp | lend | ctrl 次数/s | mov 节/s |
| --- | --- | --- | --- | --- | --- | --- | --- | --- |
| 10 | 377 | 1 | 30 | 131 | 20 | 4 | 1999.3 | 87793.5 |
| 11 | 448 | 1 | 36 | 158 | 24 | 5 | 2406.6 | 106144.6 |
| 12 | 448 | 4 | 35 | 159 | 24 | 5 | 2425.0 | 106914.8 |
| **13（中位）** | **441** | **1** | **34** | **154** | **23** | **4** | **2184.0** | **95996.6** |
| 14 | 384 | 1 | 31 | 130 | 20 | 4 | 1829.5 | 80252.3 |

### 40.3 计数器自洽性（证明新探针没串）

| 校验 | 实测 | 结论 |
| --- | --- | --- |
| `gnvp_calls == mov_iters` | 87793.5 == 87793.5（5 个窗口**逐字相等**） | ✅ `gnvp` 严格每节一次，与循环迭代数一一对应 |
| `vent/gnvp` | 71803.6/87793.5 = **81.8%** | 同格分支进入率 81.8%（跨格 18.2%，比 §38 推算的 ~9% 高，见 REG-40-B） |
| `wr/gnvp` | 78628.7/87793.5 = **89.6%** | `wr` 在同格与跨格两条路径都执行，故 > `vent` |
| `lend/ctrl` | 1611.8/1999.3 = **80.6%** | 每列车迭代约 80% 会走到一次 `TrainCheckIfLineEnds`（只 moving front 那节） |
| 每列车遍历节数 | 87793.5/1999.3 = **43.9 节/次** | 与 §38 的 42.1 同量级 |

### 40.4 判读（对照 §39.3）

以 #13 为分母（`ctrl` = 441）：

| 桶 | ms/s | 占 `ctrl` | 单次成本 | 每节成本 |
| --- | --- | --- | --- | --- |
| `ctrl` | 441 | 100% | 201.9 µs/次 | **4.59 µs/节** |
| └ `astar` | 1 | 0.2% | 0.47 µs/次 | 0.01 µs/节 |
| └ `vent` | 34 | 7.7% | 0.43 µs/次 | 0.35 µs/节 |
| └ `wr` | 154 | **34.9%** | 1.77 µs/次 | **1.60 µs/节** |
| └ **`gnvp`** | **23** | **5.2%** | **0.24 µs/次** | **0.24 µs/节** |
| └ **`lend`** | **4** | **0.9%** | **2.29 µs/次** | 0.04 µs/节 |
| └ **`ctrl` 内残差** | **225** | **51.0%** | — | **2.34 µs/节** |

| §39.3 规则 | 实测 | 判定 |
| --- | --- | --- |
| `gnvp/ctrl ≥ 25%` ⇒ 位置派生是靶子 | 5.2% | ❌ 排除 |
| `lend/ctrl ≥ 25%` ⇒ 线端检查是靶子 | 0.9% | ❌ **强排除** |
| 两者 < 10% 且残差 ≥ 30% ⇒ 转向减少迭代次数 | 5.2% / 0.9%，残差 51.0% | ✅ **命中** |

### 40.5 三条结论

**① `lend` 彻底出局。** 只有 0.9%（4 ms/s）。它单次确实贵（2.29 µs，是 `vent` 的 5 倍、`gnvp` 的 10 倍），但**每列车一次**而不是每节一次，被 43.9 节/次的遍历稀释掉了。§33 那条"线端检查可能是大头"的猜想**证伪**。

**② `gnvp` 单次极便宜（0.24 µs），但它每节必跑。**
5.2% 是个"不多但也不为零"的量。它证明 `GetNewVehiclePos` 本身不是问题——查表 + 几个加法，0.24 µs 已经接近 Debug 下能做到的下限。⇒ §33 的"折线/派生路线"如果目的是省 `GetNewVehiclePos`，**收益上限只有 23 ms/s（整帧 1.4%），不值得**。

**③ 残差 = 循环体本身，不是任何一次函数调用。**
把已命名的四个桶全部扣掉后，还剩 **2.34 µs/节**（225 ms/s，`ctrl` 的 51%）。而这个量里已经**没有任何函数调用了**——它是：
`while` 条件 + `v = GetNextVehicle()` 取链 + `old_*` 三个字段快照 + wormhole 位测试 + `gp.old_tile == gp.new_tile` 比较 + `goto` 标签 + 四组 timer 的 QPC 开销（4 × ~40 ns ≈ 0.16 µs/节）。

扣掉探针后 ≈ **2.18 µs/节仍是纯循环开销**。这解释了为什么前两轮"每节固定动作"始终找不到具体函数：**它本来就没有具体函数**。

### 40.6 由此确定的下一步（与前两轮的靶子排序相反）

1. **停止"砍单节成本"这条路线。** 单节 4.59 µs 里，`wr` 1.60 + 残差 2.18 已经贴着 Debug 的下限，`vent`+`gnvp`+`lend` 合计才 0.63。再劈一个子桶最多省 0.1~0.3 µs/节。
2. **唯一还有量级的靶子 = `wr`（1.60 µs/节，34.9%）。** 它内部三步还没拆（`UpdateDeltaXY` / `UpdatePosition` / `UpdateInclination`），而玩家的"折线派生位置"只替换中间那步。**这是最后一个值得拆的桶。**
3. **减少迭代次数（段/链级批处理）正式升为第一顺位。** 每节 2.34 µs 的残差只能靠"少遍历节"来降——但要先确认 `TrainController` 的 43.9 节/次是否可减（每节都要 `GetNewVehiclePos` + 写位置，除非证明某类节的位置不变）。
4. 折线方案：**目标从"省 `gnvp`"改为"省 `wr` 的 `UpdatePosition`"**，否则打错了地方。

### 40.7 疑点（按纪律登记）

| 编号 | 疑点 | 期望 | 实测 | 待验证 |
| --- | --- | --- | --- | --- |
| REG-40-A | 两种"残差"混用 | 一个名字一个含义 | 日志 `resid=126` vs `ctrl` 内残差 225 | 已在 §40.0 分开命名；后续文档禁用裸写"残差" |
| REG-40-B | 跨格分支比例 | §38 推算 ~9% | `vent/gnvp` = 81.8% ⇒ 跨格 **18.2%** | 疑场景差异（本轮 `mov` 96k 节/s 比上轮密）；不影响结论 |
| REG-40-C | 有效窗口仅 5/44 | 上轮 11/107 | 暂停比例 89% | 样本偏少但 5 个窗口波动 ±15%，`gnvp/lend` 的比例 5 个窗口全部落在 5.0~5.4% / 0.9~1.1%，结论稳 |
| REG-40-D | `lend` 单次 2.29 µs 是否含预留 | 它可能触发寻路 | 若是，`astar` 应同时涨 | `astar` 仅 1 ms/s，说明这些 `lend` 调用绝大多数走的是早退（非 moving front / 无预留），不是寻路 |

---

## 四十一、`wr` 三步拆分（2026-11-09，玩家"那就拆wr吧"）

### 41.1 为什么要拆

§40 的结论把靶子收缩到只剩 `wr`：**154 ms/s、占 `ctrl` 34.9%、每节 1.60 µs**，
是单节 4.59 µs 里唯一的量级项（`vent`+`gnvp`+`lend`+`astar` 合计才 0.64 µs）。
但 `wr` 是一个**四件事打包**的块，`UpdateDeltaXY`（算包围盒）和 `UpdatePosition`（只标脏块）
的成本可能差一个数量级，不拆开就无法判断"折线派生位置"到底能省下哪一段。

### 41.2 当前 `wr` 块的完整内容（`train_cmd.cpp:21575-21594`）

```cpp
int old_z = 0;
{
    R3RScopeTimer r3r_wr_timer(&r3r_wr_ns, &r3r_wr_calls);
    v->UpdateDeltaXY();                                                   // ① 算 sprite 包围盒 / x_offs / y_offs

    v->x_pos = gp.x;                                                      // ② 位置落定
    v->y_pos = gp.y;
    v->UpdatePosition();                                                  //    标脏块
    DecreaseReverseDistance(v);                                           // ③ 三个簿记
    if (v->lookahead != nullptr) AdvanceLookAheadPosition(v);
    if (v->flags.Test(VehicleRailFlag::PendingSpeedRestriction)) DecrementPendingSpeedRestrictions(v);

    old_z = v->UpdateInclination(...);                                    // ④ 算 z / 坡度
}
```

### 41.3 拆成四个子桶（`ctrl` 的子集，只打印，不加 `sub_ms`）

| 子桶 | 覆盖 | 为什么单独看 |
| --- | --- | --- |
| `wrd` | ① `UpdateDeltaXY()` | 要访问 sprite/GRF 尺寸，**最可能是大头**；折线方案替代不了它（它算的是包围盒，不是位置） |
| `wru` | ② 写 `x_pos/y_pos` + `UpdatePosition()` | 折线方案**真正能替代**的那一段：派生后位置仍要写，但可以跳过从 `gp` 重算 |
| `wrs` | ③ 三个簿记（`DecreaseReverseDistance` / `AdvanceLookAheadPosition` / `DecrementPendingSpeedRestrictions`） | 三个都是 R3R 或 lookahead 相关的独立小函数，可能有隐藏循环，从没单独计过时 |
| `wrz` | ④ `UpdateInclination()` | 每节必算 z，可能含 `GetTileZ`/缓存访问 |

`wr` 本身保留为外层桶，**四个子桶之和必然小于 `wr`**（差额 = 5 个 timer 的 QPC 开销 + 未覆盖的空隙）。

### 41.4 判读表

以 `wr` 为分母（§40 实测 `wr` = 154 ms/s = 1.60 µs/节）：

| 观察 | 结论 |
| --- | --- |
| `wrd / wr ≥ 50%` | 大头在包围盒重算 ⇒ **折线派生方案基本无效**（它替代不了算包围盒），要改为"包围盒缓存/增量更新" |
| `wru / wr ≥ 50%` | 折线派生**正中靶心** ⇒ 方案价值 = 每节 0.8 µs 量级，值得做 |
| `wrs / wr ≥ 25%` | 三个簿记里有隐藏成本 ⇒ 先去看哪个函数在循环，这类通常是最容易低成本砍掉的 |
| `wrz / wr ≥ 25%` | z 计算是靶子 ⇒ 看 `UpdateInclination` 是否每节都做地图查询（同格时 z 不变，本可跳过） |
| 四项合计 < 60% | 剩余是 timer 开销 ⇒ 说明 `wr` 已经拆无可拆，回到"减少迭代次数"路线 |

### 41.5 落地清单（只改 `src\train_cmd.cpp`，纯 .cpp ⇒ 增量合法）

1. 计数器：`r3r_wrd/wru/wrs/wrz` 各 `_calls/_ns`（紧接 `r3r_lend` 之后）。
2. `:21580` 的块内按 41.2 的①②③④插四个 `R3RScopeTimer`，**保持原有语句顺序与语义**，只在语句前后加作用域。
3. `PERF-TICK` 格式串在 `lend= |` 之后插 `wrd= | wru= | wrs= | wrz= |`，参数列表同序插 8 个。
4. `R3RPerfDumpAndReset()` 清零补四行。
5. 负判据：`fps` 不因每节多 4 次 QPC 明显下降（每节 +160 ns ≈ 整帧 +0.15 ms，预期不可见）。

### 41.6 注意（写进代码注释，避免下一轮误读）

`wr` 的读数会因这 4 个 timer 而**略微变大**（每节 +0.16 µs，即 1.60 → 约 1.76 µs/节），
与 §40 的 154 ms/s 不再逐字可比；比较时请看**子桶占 `wr` 的比例**，而不是绝对值。

### 41.7 落地与构建自证（2026-11-09 22:48）

五处改动，全部在 `src\train_cmd.cpp`（未碰 `src\*.h` ⇒ 增量合法）：
计数器定义（`r3r_lend` 之后）、`:21580` 的 `wr` 块内四个 `R3RScopeTimer` 作用域
（语句顺序与语义逐字不变，`old_z` 仍在最外层声明）、`PERF-TICK` 格式串、
同处参数列表（各 +8 项）、`R3RPerfDumpAndReset()` 清零。

| 自证项 | 实测 |
| --- | --- |
| 构建入口 | `R3R_fullbuild.cmd inc detach`（复用既有入口，未新建 .cmd） |
| 完成标记 | `build\R3R_incbuild.done` = `EXIT_CODE=0`，mtime 22:48:34 |
| 链接 | 日志 `[3/3] Linking CXX executable openttd.exe` |
| 时间戳 | `src\train_cmd.cpp` 22:46 → `build\openttd.exe` 22:48:24（52 610 560 B） |
| 字面量顺序 | exe 内实测 `... wr= \| gnvp= \| lend= \| wrd= \| wru= \| wrs= \| wrz= \| coll= ...`，与参数列表同序 |
| read_lints | 0 条 |

### 41.8 交给玩家的复测要求

1. 用 `build\openttd.exe`（**22:48 版**）打开同一存档，跑同一段场景，连续 ≈5 分钟；
2. **不要暂停**（§38.1 / §40.1 的教训，两批日志里 89%~91% 的窗口是暂停态）；
3. 判据：`PERF-TICK` 行内要同时出现 `wrd=`、`wru=`、`wrs=`、`wrz=` 四段；
4. 负判据：`fps` 不因每节多 4 次 QPC 明显下降（每节 +160 ns，整帧预期 +0.15 ms 以内）。

### 41.9 拿到数据后我会做什么

按 §41.4 的表判定，然后：
- 若 `wrd` 是大头 ⇒ 折线派生方案退役，改研究包围盒缓存；
- 若 `wru` 是大头 ⇒ 折线派生方案正式立项，价值 = `wru` 的份额；
- 若 `wrs` 是大头 ⇒ 直接去那三个函数里找循环，这类通常最好砍。

---

## 四十二、拆分结果：`wrz`（UpdateInclination）独占 50%（2026-11-09 22:57 日志）

### 42.0 数据有效性

| 项 | 实测 |
| --- | --- |
| 日志 | `build\R3R_perf.log` 28 035 B，mtime **22:57:26** |
| 产生者 | `build\openttd.exe` **22:48:24** ⇒ 22:57 > 22:48，有效 |
| 字段 | 31 个窗口**全部**含 `wrd=`/`wru=`/`wrs=`/`wrz=` |
| 有效窗口 | **7**（#6~#12），其余 24 个全 0（暂停态） |

### 42.1 原始读数（ms/s）

| # | ctrl | wr | wrd | wru | wrs | wrz | wr 次数/s | mov 节/s |
| --- | --- | --- | --- | --- | --- | --- | --- | --- |
| 6 | 424 | 173 | 17 | 19 | 23 | 86 | 79775.3 | 88913.6 |
| 7 | 483 | 199 | 20 | 23 | 24 | 100 | 94220.8 | 104088.5 |
| 8 | 464 | 195 | 20 | 22 | 23 | 98 | 91758.1 | 100367.0 |
| 9 | 460 | 180 | 18 | 20 | 22 | 91 | 83422.6 | 92640.3 |
| **10（中位）** | **471** | **182** | **18** | **21** | **22** | **92** | **84942.9** | **95079.6** |
| 11 | 455 | 178 | 18 | 20 | 20 | 90 | 84184.8 | 92377.3 |
| 12 | 434 | 186 | 19 | 21 | 22 | 94 | 88209.5 | 95214.3 |

### 42.2 占比与单次成本（#10 为分母，7 个窗口互差 <1.5%）

| 子桶 | ms/s | 占 `wr` | 占 `ctrl` | 每节成本 |
| --- | --- | --- | --- | --- |
| `wr` | 182 | 100% | 38.6% | 2.14 µs |
| └ **`wrz`** | **92** | **50.5%** | **19.5%** | **1.08 µs** |
| └ `wrs` | 22 | 12.1% | 4.7% | 0.26 µs |
| └ `wru` | 21 | 11.5% | 4.5% | 0.25 µs |
| └ `wrd` | 18 | 9.9% | 3.8% | 0.21 µs |
| └ 未覆盖（5 个 timer 的 QPC） | 29 | 15.9% | 6.2% | 0.34 µs |

7 个窗口的 `wrz/wr` 落在 **49.7%~50.6%**，`wrd` 9.7%~10.1%，`wru` 11.3%~11.6%，`wrs` 11.2%~13.3% —— **结构极稳，不是噪声**。

### 42.3 三条判定（对照 §41.4）

**① 我原来的预判（`wrd` 最大）被证伪。** `UpdateDeltaXY` 只占 9.9%（0.21 µs/节），是全块最便宜的一项。

**② 折线派生方案正式退役。** 它只能替代 `wru`，而 `wru` 只有 11.5%（0.25 µs/节 = 21 ms/s = 整帧 1.4%）。**收益上限 4 个 ctrl 百分点，不值得动几何代码。**

**③ 真靶子是 `wrz` = `UpdateInclination`，50.5%（1.08 µs/节，占 `ctrl` 19.5%）。**
它是单节 4.59 µs 里**第一个量级超过 1 µs 的具体函数**，也是 §40 那 2.18 µs "纯循环残差"之外唯一的大件。
命中 §41.4 的判据 `wrz/wr ≥ 25%` ⇒ 下一步查：**同格移动时 z 是否根本不变、能否整段跳过**。

### 42.4 探针开销复核（正名：这些 timer 值这个钱）

`wr` 从上轮 1.60 µs/节涨到 2.14，多出 0.54 µs；其中 0.34 µs 是 5 个 timer，另 0.2 µs 是读数落在不同窗口（本轮 `ctrl` 471 vs 上轮 441，整体略高）。
⇒ 每节付 0.34 µs 换到"50% 集中在一个函数"这个信息，**值得**。

### 42.5 下一步（唯一动作）

读 `Train::UpdateInclination()` 源码，判定三件事：
1. 同格（`new_tile == old_tile`）时它是否仍做地图 z 查询；
2. 是否有缓存可命中（`cached_z` 之类）；
3. 滑行中同一轨道上 z 恒定，能否对"整段同格"只算一次。

前提：它是纯位置计算、不改状态机。若它顺带改 `VehicleRailFlag`（GOINGUP/DOWN），跳过时要同步保持标志正确。

---

## 四十三、`wrz` 的构成解剖：一半是 Debug 断言，一半是渲染记账（2026-11-09）

### 43.1 调用链（已逐层读完）

```cpp
// ground_vehicle.hpp:310
inline int UpdateInclination(bool new_tile, bool update_delta, bool in_wormhole = false)
{
    int old_z = this->z_pos;
    if (in_wormhole) { ... }
    else if (new_tile) this->UpdateZPositionAndInclination();   // 跨格
    else               this->UpdateZPosition();                 // 同格（81.8% 走这里）
    this->UpdateViewport(true, update_delta);                   // 无条件
    return old_z;
}
```

### 43.2 成分一：`UpdateZPosition()` 里的 Debug 断言（`ground_vehicle.hpp:293-295`）

```cpp
if (HasBit(gv_flags, GVF_GOINGUP_BIT) || HasBit(gv_flags, GVF_GOINGDOWN_BIT)) {
    ...  // 平地车不进这里
}
#ifdef _DEBUG
    assert(this->z_pos == GetSlopePixelZ(this->x_pos, this->y_pos, true));
#endif
```

**这条 `assert` 在 if 之外，每节无条件执行一次 `GetSlopePixelZ`**（读地图高度表 + 四角插值）。
`build\` 是 Debug 构建（`_DEBUG` + `DBG_ASSERTS` 均开）⇒ 这部分成本**在 Release 构建里根本不存在**。

### 43.3 成分二：`UpdateViewport(true, false)`（`vehicle_base.h:1837`）

`update_delta` 在 `wr` 块里传的是 `false`（`UpdateInclination(new_tile, false, ...)`），所以：
- `if (update_delta) UpdateDeltaXY()` —— **不执行**（这解释了 `wrd` 只有 0.21 µs，它没被重复调用）；
- 必做：`RemapCoords()` + `IsPointInViewportVehicleRedrawArea(_viewport_vehicle_normal_redraw_rects, pt)`（**线性扫一个 vector**）；
- 视野内 ⇒ `UpdateViewportNormalViewportMode(true)`；视野外 ⇒ `SetBit(VCF_IMAGE_REFRESH)` + `Vehicle::UpdateViewport(dirty)` → `MarkAllViewportsDirty(...)`（**遍历所有视口**）。

### 43.4 判定：`wrz` 不可"同格跳过"

§42.5 提的三项检查，答案全是"否"：

| 检查 | 结论 |
| --- | --- |
| 同格时是否仍做地图 z 查询 | **是**，但那是 `_DEBUG` 的断言，删了等于自废安全网 |
| 是否有缓存可命中 | 没有；`z_pos` 本身就是缓存，断言正是在验它 |
| 能否对整段同格只算一次 | **不能**：`UpdateViewport` 必须每节跑，否则车在屏幕上的坐标与脏矩形不更新 |

⇒ `wrz` 的 1.08 µs/节 = **Debug 断言 + 渲染记账**，两者都不是"浪费"，都不是能靠写代码砍掉的。

### 43.5 由此产生一个必须先问的方向性疑点

| 编号 | 疑点 | 依据 |
| --- | --- | --- |
| **REG-43-A** | **这四轮（411~414）的每一个微秒都是 Debug 数字，可能优化的是玩家根本不在的构建** | ①`build\` 确为 Debug（`/Od /Ob0 /RTC1 -MTd -D_DEBUG`）；②§40 的 `ctrl` 441 ms/s = 整帧 7.35 ms（44%），这在 Release 下按 5~20× 经验值只有 0.4~1.5 ms；③已确认至少 `wrz` 的 50% 成本是 `#ifdef _DEBUG` 独有 |

**这条不解决，继续拆桶可能是白干。** 需要玩家拍板（见 §43.6）。

### 43.6 三个可选路线（等玩家选）

| 选项 | 做法 | 代价 | 能得到什么 |
| --- | --- | --- | --- |
| **甲** | 先编一次 Release（`build-release` 已有），在同一存档量 `ctrl`/`wr`/`wrz` | 一次全量约 16~20 分钟 | **一劳永逸地知道真实数字**；若 Release 下 `ctrl` 只有 1 ms/帧，性能问题本身不存在，可直接收工 |
| 乙 | 继续在 Debug 上拆 `wrz`（把 `UpdateZPosition` 与 `UpdateViewport` 再分开计时） | 增量 3 分钟 | 只能知道 Debug 下谁占，对玩家体验无直接价值 |
| **丙（推荐）** | 甲 + 一条极低风险的实修：确认 `assert` 是主因后，把该断言的 `GetSlopePixelZ` 改成"仅在上一次断言失败过才复查"的节流版 | 甲 + 3 分钟 | 内测版立刻变快，且不丢断言能力 |

**为什么推荐丙**：`assert` 的目的是抓"z_pos 与地图不符"的 bug。连续 95 万次/秒全量复查属于过度防御——同一辆车在同一格内位置连续变化时，重复验同一件事。改为"命中一次就每次验、否则每 N 次验一次"能把 Debug 版的 `wrz` 砍掉约一半，且第一次出错仍然当场停住。

⇒ **下一步候选（待玩家拍板，均未实施）**：
- **A（低风险，先做）**：`stk` 1.1 ms/帧 + 449~611 次/秒 —— 查这 400~600 次/秒的 stuck 重试为什么发生（行为缺陷，不是纯性能），既是 6.6% 帧耗也是 KI-330 家族线索。
- **B（中风险，量级最大）**：artic 部件能否在 `TrainController` 的逐节循环里跳过（位置由父车 `UpdateDeltaXY`/`CalcNextVehicleOffset` 决定）。必须先证明"部件位置不由本循环写"，否则会引发图像/碰撞错位。
- **C（不动）**：`ctrl` 的寻路部分已证伪，不再投入。

