# R3R 下一步手册（2026-09-12 23:30 生成）

> 用途：对话记录丢了，这份手册是"我是谁、干到哪、你该敲什么"的唯一依据。
> 配套清单：`R3R_KNOWN_ISSUES.md`（长期规则要求的已知问题总表）。
> 复测数据：`D:\r3r_probe\ki14_result.txt`、`build\R3R_perf.log`、`build\R3R_debug.log`。

---

## 0. 一句话现状（更新于 09-13 第 9 轮）

KI-14 的 **第 ① 项（探针洪水）已修**；但卡顿元凶已用游戏内 `fps` 锁定为 **`GL train ticks`（列车 tick）**，不是日志：

- 第 5 轮用户实测：`Game loop = 168.05 ms`，其中 **`GL train ticks = 135.53 ms`（占 80 %）**，`Drawing 7.14` / `Video 2.33` / `Sound 0.04` 全正常 → **元凶 = 列车仿真 tick**。
- 与"所有 R3R 探针都是 0"自洽：`tryCouple` / `foldCheck` / `dumpChain` / `posHelper` 都不在 `TrainLocoHandler` 主干上。
- 代码审查结论：`TrainLocoHandler` 内 R3R 注入点**只有一处** —— 站台等待车（`R3RIsCarOnlyFormation` 或 `primary + OT_WAIT_COUPLE`）停稳时**每 tick 整站台重预留**（`train_cmd.cpp` ~9500：`ReserveTrackUnderConsist()` + 沿站台轴双向逐 tile `TryReserveRailTrack`），且 `ReserveTrackUnderConsist()` 内部每车每 tick 还调 `R3RTrackBitsProbe` / `R3RDbgEdge`。
- **第 6 轮（本轮）已落地**：① 帧预算分解计数器接好 —— PERF 日志新增 `PERF-TICK` 行，把列车 tick 拆成 `loco`（整个 `TrainLocoHandler`）/ `coupleH` / `plat`（站台重预留闸门）/ `resv`（`ReserveTrackUnderConsist`）/ `edgeGate` 五个桶；② 给 tick 路径上 4 处裸探针补闸门（`COUPLE-FAIL`+`RESCHECK`、`DEPOT-ARR`、`CPL-S0-CHK`），使 `fopen` 只在边沿时发生。编译 `NINJA_EXIT=0`，obj/exe 时间戳链干净（`src` 01:32:06 → `obj` 01:32:36 → `exe` 01:32:38）。
- **第 7 轮（09-13 复测判读）—— KI-14 结案定性**：`D:\r3r_probe\ki14_result.txt` + `build\R3R_perf.log` 的 `PERF-TICK` 给了决定性数据。卡顿窗口（fps 10.0→10.4）内 `loco = 364→598 ms/s`、`plat = 14→32 ms/s`、`resv = 13→29 ms/s`、`edgeGate = 3→10 ms/s`、**`coupleH = 0.0/s`**；事件一结束（fps 62.0）`loco = 0.0/s`。即 **R3R 自己只占 ≈ 70 ms/s（12 %），`TrainCoupleHandler` 根本不在 tick 路径上**，剩下 **~520 ms/s（88 %）在未插桩的原生 `TrainLocoHandler` 主干**。
- 世界规模是关键背景：`chains=911 / vehs=42 555 / maxChain=126 / 链均 46.7 节 / artic=28 534 / waitCouple=128`。`loco = 18 438 calls/s`（≈554 个前车每 tick 各一次，单车均 32 µs）→ **R3R 合并出的超长链会把每 tick 所有 O(链长) 的原生遍历整体放大**，这是唯一还站得住的归因路径。
- **第 7 轮已落地**：为验证它，把原生 O(链长) 的 4 处遍历也接上分桶（`ctrl` / `coll` / `spd` / `vp`，另加 `dump`），`PERF-TICK` 行扩展。编译 `NINJA_EXIT=0`。
- **第 8 轮（09-13 判读新数据）—— 结案**：加上 `ctrl/coll/spd/vp` 后，`PERF-TICK` 把 593 ms/s 拆完了 —— **`ctrl`（`TrainController`）325–340 ms/s = 55 %，单项最大，88 µs/次调用**；`spd 43 + vp 37`（原生整链遍历）= 13 %；R3R 自己的桶 `plat 43 + resv 39 + edgeGate 10` = **92 ms/s（15 %）**；`coll` 6 ms/s、`coupleH` 恒 0；余量 ~90 ms/s（15 %）在 `TrainLocoHandler` 其余部分。
- 结构原因：`TrainController` 是**每车一迭代**的移动循环（`GetNewVehiclePos`/`VehicleEnterTile`/`TrainMovedChangeSignal`），成本 ∝ 移动车数 × 链长；本存档 `vehs/chains` = **46.7 车/链**、`maxChain=126`、42 555 车 → 原生逐车成本被放大。
- **定性**：这不是 R3R 探针缺陷，也不是站台重预留问题，而是**原生逐车仿真 × 超大存档规模**（R3R 合并超长链使单车链变长，放大了逐车/整链遍历）。R3R 侧理论可省上限 ≈ 92 ms/s（10.3 fps → ~12 fps）。
- **第 9 轮（09-13 判读含 `mov=` 的数据）—— 终局结案**：三个窗口（fps 9.2 / 17.9 / 61.7）的 **`mov/ctrl` = 49.6 / 48.7 / 48.6 节/次**（全存档链均 = 42 555/911 = 46.7，移动中的链略长 4–6 %）→ 每次 `TrainController` 调用都走整条链；**每车成本 1 919 / 1 892 / 1 856 ns，跨窗口（ctrl 总量差 2.4 倍）恒定** → 90–95 µs/次不是固定调用开销，就是 `48.7 车 × 1.9 µs`；对照纯遍历 `spd`（0.10 µs/车）/ `vp`（0.19 µs/车）→ **移动步是纯指针遍历的 10–18 倍，属真实工作量**（`GetNewVehiclePos` + `VehicleEnterTile` + `TrainMovedChangeSignal` + 平交道口/信号），不是 bug。
- **完整拆账（fps 17.9 窗口，分母 596 ms/s）**：`ctrl` **52.5 %**、`TrainLocoHandler` 其余（订单/路径预留/信号尾）24.7 %、`spd+vp` 12.6 %、**R3R 三桶 `plat+resv+edgeGate` 合计 55 ms/s ≈ 9 %**、`coll` 0.8 %。
- **结论**：成本 = 移动车数 × 链长 × 1.9 µs，属**原生逐车移动设计 × 超大存档规模（42 555 车 / 911 链 / maxChain 126）**；R3R 合并超长链只是把"每次移动的车数"从常规几节放大到 48.7 节，不增加总车步数。**KI-14 结案，不再插桩。**
- **次级发现（非列车 tick）**：fps 9.2 窗口里 `loco` 只折合 34 ms/帧、占 108.6 ms 帧预算的 32 %，另 **68 % 不在列车 tick**（与第 4 轮"硬换页/内存"嫌疑一致）→ 若还要冲帧率，下一步该查内存/换页与绘制，而不是列车仿真。
- 判读规则见第 3.E 节。

### 0.1 第 10 轮（09-13 追问判读：`segs=0` + 谁在跑列车 tick）

- **口径先核实**（`train_cmd.cpp:4072-4084`）：`segs` 统计的是**带 ★ `IsSegmentFront()` 的车辆数**；`waitCouple` 统计 `current_order.IsType(OT_WAIT_COUPLE)` 的车辆数。日志 `chains=911 vehs=42555 maxChain=126 segs=0 artic=28534 waitCouple=122` → **这个存档 911 条链里没有任何一节段头，即全是"非成段链"**；其中 122 辆车（≈122 条链头）挂着 WAIT_COUPLE 在等被挂。
- **谁在跑列车 tick**（`train_cmd.cpp:10058-10088` `Train::Tick()`）：只有 `IsFrontEngine()` 才进；且**每 tick 调用 `TrainLocoHandler` 两次**（10067 `mode=false` + 10078 `mode=true`，原生结构）。而 `R3RCreateCarOnlyFormation()`（1733-1746）对车厢链头做 `SetEngine()+ClearWagon()+SetFrontEngine()` → **车厢链被提升为 FrontEngine，因此成为"列车"、每 tick 必然进入列车 tick 循环**。
- **定量交叉验证**（`loco calls ÷ fps ÷ 2`，即"每帧跑了几条链"）：窗口 60 = 9354.7/5.2/2 ≈ **899 条**、窗口 62 = 16704.5/9.2/2 ≈ **908 条** ≈ `chains=911` → **全部链每帧都在跑列车 tick**。而 `ctrl calls ÷ fps ÷ 911`：窗口 60 = 1858.5/5.2/911 ≈ **39 %**、窗口 62 ≈ **40 %** → 只有四成链在移动，**六成链静止但照样每帧跑 2 次 handler**。
- **新量化点（非移动链的固定成本）**：`(loco − ctrl − spd − vp) ÷ (loco_calls − ctrl_calls)` = 窗口 60 `96/7496.2` = **12.8 µs/次**、窗口 62 `172/13306.4` = **12.9 µs/次**、窗口 64 `91/5653.5` = **16.1 µs/次** → **12.8–16.1 µs/次，跨窗口恒定**；折合窗口 62 约 **172 ms/s（占 sim 29 %）** 是"静止链"的每帧例行开销（含整链 `spd`/`vp` 遍历、`ProcessOrders`、站台预留闸门）。
- **结论修正（重要）**：第 9 轮的"R3R 侧只剩 9 %"说的是 **R3R 自己的计时桶**（`plat+resv+edgeGate`）；但 R3R 的**结构性贡献**——把 911 条车厢链变成列车对象，使原生代码每 tick 为它们跑 2 次 `TrainLocoHandler` + 整链 `spd`/`vp` 遍历——是**记在原生桶里的**，量级 **91–247 ms/s（占 sim 15–44 %）**，远大于那 9 %。原生 OpenTTD 里散车厢（free wagon）不是列车，这些开销根本不存在。
- **另一条同源修正**：**"非成段"本身不收费**——段（★）只影响身份/几何/预留，不会让 tick 变快或变慢；`segs=0` 意味着段机制在本存档既不是原因也不是解药。真正决定成本的是两个量：**(a) 有多少条链被包成列车（决定 tick 次数）**、**(b) 有多少节车在移动（决定车步数，1.9 µs/步）**。
- **待验（需你拍板再动）**：加一个按 `R3RIsCarOnlyFormation()` 分类的计时探针，把 `loco`/`spd`/`vp` 再拆成"car-only 等待链"与"真机车列车"两列，才能定量"静止等待链"的真实可省上限；在那之前任何"跳过静止链 tick"的改法都可能破坏信号/站台预留语义，不擅动。

---

## 1. 背景：怎么走到这里的

| 轮次 | 时间 | 做了什么 | 结论 |
|---|---|---|---|
| 第 1 轮 | 09-12 白天 | 加 `R3RDbgEdge` 边沿触发闸门（`train_cmd.cpp`，覆盖 `FOLDCHK` / `FOLDCHK-DIR` / `RESERVECONSIST` / `SKIP-STOPPED` / `TTB-PROBE`，每状态每 128 帧窗口最多一行；PERF 行新增 `dbgEdge=` / `skipped=`） | 编译通过 |
| 第 2 轮 | 09-12 22:53 | 首次复测 | 闸门见效，但发现 **exe 是混合陈旧 obj**（615 obj 里 444 个早于 `vehicle_base.h` 09-12 01:21）→ 帧率数据作废（KI-15） |
| 重编 | 09-12 23:16:19 | **全量重编**（`rebuild_R3R.cmd`） | 615/615 obj 晚于头文件，`openttd.exe` 50 222 080 B |
| 第 3 轮 | 09-12 23:16:55 | 干净 exe 复测 120 s | 见下方数据，探针问题确诊已修 |
| 第 4 轮 | 09-13 判读 | 重新分析 25 530 行日志 + 复查全部探针站点 | 卡顿段内**所有 R3R 探针为 0** → 探针抓不到元凶，改查未插桩路径 |
| 第 5 轮 | 09-13 用户实测 | 游戏内 `fps` 命令（PFE 逐项） | **元凶 = `GL train ticks` 135.53 ms（占 game loop 80 %）**，绘制/视频/声音正常 |
| 第 6 轮 | 09-13 本轮 | 接线 `PERF-TICK` 帧预算分解 + 给 4 处 tick 路径裸探针补闸门 | `NINJA_EXIT=0`，零 lint；待一次复测取 `PERF-TICK` 数据 |
| 第 7 轮 | 09-13 用户复测 | 判读 `ki14_result.txt` + `PERF-TICK` | **R3R 仅占 70 ms/s（12 %），`coupleH=0`；520 ms/s 在原生 tick** → 再给原生 O(链长) 遍历接 4 个桶（`ctrl`/`coll`/`spd`/`vp`），`NINJA_EXIT=0` |
| 第 8 轮 | 09-13 用户复测 | 判读含 `ctrl/coll/spd/vp` 的 `PERF-TICK` | **`ctrl` 325–340 ms/s = 55 %（88 µs/次）是主因**；R3R 桶仅 92 ms/s（15 %），spd+vp 80 ms/s（13 %），余量 90 ms/s；判为"原生逐车移动 × 存档规模"→ 加 `mov=` 迭代计数定量每车成本，`NINJA_EXIT=0` |
| 第 9 轮 | 09-13 用户复测 | 判读含 `mov=` 的 `PERF-TICK` | **`mov/ctrl` = 49.6/48.7/48.6 节/次、每车 1 856–1 919 ns 恒定** → 90–95 µs/次 = `48.7 车 × 1.9 µs`，非固定开销；R3R 三桶降至 **9 %**；**终局定性 = 原生逐车移动 × 存档规模，KI-14 结案** |

---

## 2. 第 3 轮实测数据（对照用）

存档 `D:\r3r_probe\_net7.sav`（`chains=911 vehs=42555 maxChain=126 artic=28534`），脚本 `_tmp_ki14_verify.ps1`，120 s 计量窗口。

| 指标 | 旧（洪水，22:11） | 干净 exe（23:16:55） |
|---|---|---|
| `R3R_debug.log` 体积 | 2 109 528 B / 38 s = **55 KB/s** | 1 170 080 B / 120 s = **9 751 B/s** |
| 行数 | 28 838 | 16 857 |
| RESERVECONSIST | 17 118 | 3 426 |
| DEPOT-ARR / 其缩进行 | 389 / 6 310 | **642 / 10 858（占 65%）** |
| CRT | 95 | 1 280 |
| UNRESERVE-STATION | — | 238 |
| 探针写入 | 峰值 **1.71 MB/s** | **0.01–0.04 MB/s**，5–20 ms/s |
| 闸门去重 | 无闸门 | edge 25–95/s vs skipped 1 343–8 347/s |
| 活跃段 fps | 24.9（读档期，chains=0） | 11.5 → 5.8 → 5.7 → 5.9 → 5.7 → 16.0 |
| 静止段 fps | 61.8–62.0 | **61.2 / 61.4 / 61.3** |
| 运行后空闲内存 | — | 4 413 MB（**无换页**） |

PERF 尾段原文（干净 exe）：

```
PERF frames=128 fps=5.8  frameMs avg=172.8 max=278 | dbgWrite=25.3/s 5ms/s 0.01MB/s | posHelper=24.7/s steps=617/s maxSteps=47 0ms/s | dbgEdge=25.3/s skipped=4760/s | chains=911 vehs=42555 waitCouple=119
PERF frames=128 fps=16.0 frameMs avg=62.4  max=379 | dbgWrite=95.2/s 20ms/s 0.01MB/s | dbgEdge=95.2/s skipped=6552/s | chains=911 vehs=42555 waitCouple=128
PERF frames=128 fps=61.2 frameMs avg=16.3  max=18  | dbgWrite=0.0/s 0ms/s 0.00MB/s  | dbgEdge=0.0/s skipped=0/s     | chains=911 vehs=42555 waitCouple=128
```

判读：掉帧段的 `dbgWrite` 只有 5 ms/s、日志 0.01 MB/s → **探针不可能是 172 ms/帧的原因**；同一窗口结束后（`waitCouple` 涨到 128 后稳定）立刻 61.2 fps。

---

## 3. 你要敲的命令（按需取用）

### A. 只改了 `.cpp` → 增量编译（不用删 obj）

```cmd
call "D:\VISUAL STUDIO\MAIN PACK\VC\Auxiliary\Build\vcvars64.bat"
"D:\gcc new\mingw64\bin\ninja.exe" -C "d:\sourcecode of JGRPP\build" -j 4 openttd
echo %ERRORLEVEL%
```

现成脚本（等价，日志写 `build\R3R_inc_build.log`）：

```
d:\sourcecode of JGRPP\_tmp_inc_build.cmd
```

> `ERRORLEVEL=0` 才算成功；`LNK1168` = 游戏还开着，先关 `openttd.exe`。

### B. 改了任意 `src/*.h` → 必须先删全部 obj 再按 A 编（KI-15 陷阱）

```cmd
powershell -NoProfile -Command "Get-ChildItem 'd:\sourcecode of JGRPP\build' -Recurse -Filter *.obj -ErrorAction SilentlyContinue | Remove-Item -Force"
```

一键脚本（关掉 CodeBuddy 后运行，约 25–40 分钟）：`d:\sourcecode of JGRPP\rebuild_R3R.cmd`

编译后核验（obj 必须全部晚于所改头文件）：

```powershell
powershell -NoProfile -Command "$h=(Get-Item 'd:\sourcecode of JGRPP\src\vehicle_base.h').LastWriteTime; $o=Get-ChildItem 'd:\sourcecode of JGRPP\build' -Recurse -Filter *.obj; Write-Host ('objs=' + @($o).Count + ' 早于头文件=' + @($o | Where-Object {$_.LastWriteTime -le $h}).Count)"
```

### C. 复测大存档（**先关掉 CodeBuddy 再跑**，8 GB 内存不够同时在）

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File "d:\sourcecode of JGRPP\_tmp_ki14_verify.ps1"
```

跑完把 `D:\r3r_probe\ki14_result.txt` 全文发我判读（旧日志会自动归档为 `R3R_debug.log.before_ki14_*.log`，不会丢）。

### D. 判读标准

| 现象 | 结论 |
|---|---|
| 活跃窗口 fps ≥ 60，`dbgWrite` ≤ 0.05 MB/s | 正常 |
| 活跃窗口 fps < 30，但 `dbgWrite` < 0.05 MB/s | **仿真本身**（耦合/寻路）→ 要给 `TryTrainCouple` / `FindNearestCoupleTrain` 加 ms 计时，不是压日志 |
| 静止窗口 fps < 60，探针全 0 | 查混合 obj（见 B 的核验命令） |

### E. 读 `PERF-TICK`（第 6 轮新增，专治 135 ms）

每 128 帧（低帧率下约 22 s）会在 `build\R3R_perf.log` 追加一行。第 7 轮格式（示例，每桶为 `调用/s 毫秒/秒`）：

```
PERF-TICK loco=18438.5/s 598ms/s | coupleH=0.0/s 0ms/s | plat=365.2/s 31ms/s | resv=365.2/s 28ms/s | edgeGate=15004.3/s 10ms/s | ctrl=.../s ...ms/s | coll=.../s ...ms/s | spd=.../s ...ms/s | vp=.../s ...ms/s | dump=...ms/s | mov=.../s
```

> **09-13 已确认的基线**（同一个卡顿场景）：`loco ≈ 364→598 ms/s`、`plat 14→32`、`resv 13→29`、`edgeGate 3→10`、`coupleH = 0`；事件结束（fps 62）后 `loco = 0`。
> 即 R3R 自己的桶合计只占 `loco` 的 **~12 %**、`coupleH` 恒 0 → 剩下 88 % 要靠 `ctrl`/`coll`/`spd`/`vp` 定论。
> 提醒：`per_s` 是按 128 帧窗口归一化的，低帧率下窗口 ≈ 22 s，短事件（如 20 s）只会落在 1–2 行里。
>
> **第 8 轮实测（卡顿窗口稳态）**：`loco 593 | ctrl 325 | plat 43 | resv 39 | spd 43 | vp 37 | edgeGate 10 | coll 6 | coupleH 0`（ms/s）→ `ctrl` 占 55 %。
>
> **第 9 轮实测（含 `mov=`，`build\R3R_perf.log` 第 60/62/64 行，对应 fps 9.2 / 17.9 / 61.7）**：
>
> | 窗口 | loco | ctrl（calls/s） | µs/次 | mov/s | **mov/ctrl** | **ctrl÷mov** |
> |---|---|---|---|---|---|---|
> | fps 9.2 | 316 | 177（1 858.5） | 95.2 | 92 233.8 | **49.6 节** | **1 919 ns/车** |
> | fps 17.9 | 560 | 313（3 398.1） | 92.1 | 165 450.0 | **48.7 节** | **1 892 ns/车** |
> | fps 61.7 | 252 | 130（1 441.3） | 90.2 | 70 024.6 | **48.6 节** | **1 856 ns/车** |
>
> 判读：`mov/ctrl` ≈ 链均 46.7 → 每次调用走整条链；**每车成本跨窗口恒定** → 90–95 µs/次 = `48.7 车 × 1.9 µs` 的乘积，**不是固定调用开销**（结论：车多 × 链长，收工）。基线对照：`spd` 5.1 µs/次、`vp` 4.2 µs/次 → 0.10–0.19 µs/车。
> `mov / ctrl` = 每次 `TrainController` 调用实际走的**车辆数**（= 该次移动链的长度）；`ctrl ms/s ÷ mov` = **每车每步的纳秒成本**。若每车成本与正常单车一致（~1–2 µs），说明纯粹是"车多 × 链长"，R3R 无可优化；若明显偏高，再往 `TrainController` 内部（信号更新 `TrainMovedChangeSignal` / `TryPathReserve`）细分。

| 看到什么 | 结论 / 下一步 |
|---|---|
| `ctrl` 占 `loco` 大头 | **已实测发生（55 %）**：花在 `TrainController` 的逐车移动循环 → 原生仿真成本，KI-14 从"R3R 缺陷"转为"超长链/大存档放大原生开销"的规模问题；再看 `mov/ctrl` 确认单车成本 |
| `spd` + `vp` 占大头 | `GetCurrentMaxSpeedInfoAndUpdate` / `UpdateSpeed` / `UpdateViewport` 这类**整链遍历**被 `maxChain=126` 放大 → 可做"链结构变化时才重算"，但会改行为，须用户拍板 |
| `coll` 占大头 | `CheckTrainCollision` 在超长链上逐车扫 → 同上，属规模放大 |
| `plat` + `resv` 合计占 `loco` 大部分 | 站台重预留是元凶 → 做"按需重预留"改造（改行为，须用户拍板） |
| `edgeGate` 意外很大 | 闸门自身（`unordered_map` 查找）过热 → 给热路径 `R3RTrackBitsProbe` 换无哈希判断 |
| `coupleH` > 0 | 每个移动步都调 `TrainCoupleHandler`（`train_cmd.cpp` ~9941）→ 调用点前先判 `OT_GOTO_COUPLE` |
| `loco ≈ 0` 但帧率仍低 | 卡顿不在列车 tick → 看 `PERF` 行其它字段 / 绘制 / 视频 |
| `dump` 很大 | `R3RPerfDumpAndReset` 自身在 42 555 车里做快照 → 降低采样频率 |

---

## 4. 下一步待办（按优先级）

1. ~~**【我做】** 给残余刷屏源补闸门~~ **已完成（第 6 轮）**：`DEPOT-ARR` 的 `fopen` 已移进签名闸门内；`COUPLE-FAIL` + `RESCHECK`（原每 tick／每移动步双 `fopen`）合并进 `R3REDGE_COUPLEFAIL` 边沿闸门；`CPL-S0-CHK` 改走 `R3RDbgWrite` + `R3REDGE_CPLS0`。`CRT`（10.7/s）、`UNRESERVE-STATION`（2/s）、`VIS-MISS`（当前日志未见）经频率评估属冷路径（syscall <1 ms/s），本轮**有意不动**。
2. ~~**【你跑】** 复测取 `PERF-TICK`~~ **已完成（第 7 轮）**：结论 = R3R 自己的桶合计 ≈ 70 ms/s（占 `loco` 的 12 %），`coupleH` 恒 0 → **探针与平台重预留都洗清**。
3. ~~**【你跑】** 跑第 7 轮 exe 取 `ctrl/coll/spd/vp`~~ **已完成（第 8 轮）**：`ctrl` 55 %（88 µs/次）是主因，R3R 桶仅 15 %，`coupleH` 恒 0。
4. ~~**【你跑，最后一轮取数】** 跑第 8 轮 exe 取 `mov=`~~ **已完成（第 9 轮）**：`mov/ctrl` = 48.6–49.6 节/次、每车 1 856–1 919 ns 恒定 → **判为"车多 × 链长"的规模问题，KI-14 收工**，不再插桩。
5. **【待你拍板，可选，会改行为】** R3R 侧唯一可省的是自己的 **9 %**（`plat 25 + resv 23 + edgeGate 7` ≈ 55 ms/s）：把站台重预留改成"按需"（只在预约丢失时补一次、热路径去 `unordered_map` 查找）。注意上限有限（约 +8 % 帧率），需你先点头。
6. **【待拍板，方向性】** 若要再压帧率，唯一有量级空间的是"每次移动的车数"（`mov/ctrl = 48.7`）：要么限制单链最大长度（`maxChain` 现 126），要么在"合并收益（运营语义）"与"帧率"之间取舍 —— 这属产品决策，我不会擅自改。
7. **【新线索，非列车 tick】** fps 9.2 窗口里列车 tick 只占帧预算的 32 %，另 68 % 在别处（内存/换页/绘制嫌疑）→ 值得先用游戏内 `fps` 命令对该窗口逐项复看，再决定是否继续追帧率。
8. **【待实现，KI-17】** `dbgWrite` 计费不完整（多数裸 `fopen` 未走 `R3RDbgWrite`）→ "文件体积"仍是可信指标。`vehicle_base.h` 的 `ADVANCE` 探针同类，且改头文件触发 KI-15 全量重编 → 推迟到下次必须改头文件时一并处理。
9. **【历史未验】** KI-10（newGRF 段内图像）、KI-11（逻辑翻 v 多场景）仍缺交互回归；KI-05 解挂触发点待复测。

---

## 5. 环境备忘（避免重复踩坑）

- 编译环境：必须先 `call "D:\VISUAL STUDIO\MAIN PACK\VC\Auxiliary\Build\vcvars64.bat"`，否则 `cl` 找不到 `stdint.h`。
- Ninja 路径：`D:\gcc new\mingw64\bin\ninja.exe`。
- 8 GB 内存：CodeBuddy 约 3.7 GB，与大存档/编译不能共存 → 跑游戏和全量编译前先关 CodeBuddy。
- 改 `src/lang/*.txt` 后若启动报 "No available language packs"，强制重编 `strings.cpp`（见 KI-16）。
- C 盘曾只剩 8.7 MB，页面文件在系统盘；复测前确认 C 盘有余量，否则数据不可比。

---

## 6. 09-14 三步计划（🔴 进行中；用户已拍板"写入备忘后逐个动手"）

背景见 KI-26：Release 繁忙窗口 `fps=27 frameMs=37.0 loco=458 ms/s(16.96 ms/帧, 占帧 45.8%)`，其中 `未插桩余量 = 225 ms/s = 8.33 ms/帧（占 loco 49%）` 是黑箱；探针自身 `dbgWrite 24 + edgeGate 4 + dump 1 = 28 ms/s = 1.08 ms/帧`。要回答"能否降到 1.1×"，必须先拿到"无探针下限"并切开 8.33。

### 步骤 1（🔴 本轮做）：总开关 + 无探针基线
- 改动：`src/r3r_perf.h` 增加运行时总开关 `R3RDbgOn()`（读环境变量 `R3R_DBG`，**缺省=开**，即不设变量时行为与现在完全一致）与 `R3RFopenDbg(mode)`（关时返回 nullptr）；`R3RDbgWrite` 开头早退。
- 把裸 `fopen("R3R_debug.log", ...)`（KI-17 的 61 处热路径：`train_cmd.cpp` 35、`yapf_rail.cpp` 21、`yapf_destrail.hpp` 5）统一换成 `R3RFopenDbg("a")`——现有 `if (dbg != nullptr)` 守卫天然兼容，关时零写入且连 `fopen` 都不发生。
- **验收判据**：设 `R3R_DBG=0` 启动后，`R3R_debug.log` 大小在两分钟内不增长（`R3R_perf.log` 仍在写、`dbgWrite` 计为 0）；同窗口对比 `R3R_DBG=1`，`loco`/`frameMs` 的差值即"日志 I/O 真实代价"。
- 预期：若 8.33 ms/帧 中有一半以上来自日志 I/O，则繁忙窗口立刻回到 1.5× 量级；若差值 <1 ms/帧，则 8.33 真的是计算量，必须进步骤 2。

- 落地状态（09-14 01:24）：`r3r_perf.h` 已加 `R3RDbgOn()`（读 `R3R_DBG`，缺省开）与 `R3RFopenDbg(mode)`（关时返回 nullptr），`R3RDbgWrite` 首行早退；`train_cmd.cpp` 34 处、`yapf_rail.cpp` 21 处、`yapf_destrail.hpp` 5 处已改走 `R3RFopenDbg("a")`；`window.cpp`/`newgrf_engine.cpp` 已强制重编（头文件依赖跟踪在本机失效，见 KI-15）；`build-release/openttd.exe` = 22 668 800 B @ 09-14 01:24:40，二进制内含 `R3R_DBG` 字面量，编译零错误（仅 train_cmd.cpp:3701 既有 C4267 警告）。
- 仍未 gating 的 9 处冷路径：`vehicle_base.h`（ADVANCE，仅 GOTO_COUPLE 索引推进时触发）、`vehicle.cpp`、`vehicle_cmd.cpp`×2、`station_cmd.cpp`、`sl/vehicle_sl.cpp`×2、`pbs.cpp`、`yapf_common.hpp`。与 §4 第 1 条同判：低频、syscall <1 ms/s，本轮有意不动；若步骤 1 的差值反常偏小，再回头补。

### 步骤 2（🟡 代码已落地 09-14 01:39，待你跑出数）：切开 `loco` 的 8.33 ms/帧
在 `TrainLocoHandler` 内补了 **4 个**子桶（同受 `R3RDbgOn()` 管辖，关时零成本）。原计划的 3 个里 `percar` 无法用 RAII 包住"其余全部"，故按可命名的实际调用点拆成两个，避免共享调用计数导致口径含混：
1. `ord`：`ProcessOrders` + `CheckReverseTrain`，三个调用点（9780 进站/9898 出站/9997 通过站点）；
2. `prv`：`UpdateSignalsOnSegment` + `TryPathReserve`（整块包住，一次 tick 记一次）；
3. `load`：`consist->HandleLoading(mode)`；
4. `dep`：`CheckTrainStayInDepot`。
PERF-TICK 行新增 `ord= prv= load= dep=` 与 `sub=`（上述子桶之和）`resid=`（`loco` 减去 `sub` 的残差）。
**口径说明**：`resv` 不参与 `sub`——它是 `plat` 的内层循环，重复相减会低估残差；`coll` 同理嵌在 `ctrl` 里。`ctrl` 在换端辅助函数（3133/3163/3192）里也会跑，故 `sub` 可能略微超出 `loco`，**`resid` 是"仍未解释"的上界**，判读时按上界看。
验收：`loco ≥ sub`（`resid ≥ 0`），且 `resid` 能把 8.33 ms/帧 压到 1 ms/帧 以内。

### 步骤 3（🟡 代码已落地 09-14 01:39，待你跑出数）：口径 A 的真值
原先 `D:\r3r_probe\AB\data_R3R_win.txt` 里那行 `GL trains` 来自 **Debug exe（`build\`）的 88.07 ms/帧**，与 Release 的 A/B 不同源，口径 A 因此悬空。
改为让 exe 自己把游戏内同源数字写进探针日志：`framerate_gui.cpp` 新增 `double R3RGetTrainsMs()`（读 `_pf_data[PFE_GL_TRAINS]`，取与 `framerates` 控制台命令相同的近期窗口均值 `NUM_FRAMERATE_POINTS/4`；无样本时返回 -1.0），`train_cmd.cpp` 的 PERF-TICK 行新增 `glTrains=%.2fms`。
这样每个 128 帧窗口自带"vanilla 同款列车行"，口径 A 的 1.1× 判据不再需要 Debug 构建或对帧率窗口截图 OCR。
判读：`glTrains` 与 `loco` 之差 = 我们没包住的那些原生列车 tick（`vehicle.cpp` 1683 的 `PerformanceMeasurer framerate(PFE_GL_TRAINS)` 覆盖面比 `TrainLocoHandler` 更大）。

> 踩坑提醒：`R3R_AB_probe.ps1` 只取"最后一个 running 窗口"，本次取到 46.9 fps 轻载窗口，比同日志最重的 27 fps 乐观 1.42×；三次验收必须**钉住同一窗口**（或统一用最重窗口）才可比。

### 一次性测试法（09-14 起，替代"改一小步测一次"）
三步代码已全部落地在**同一次编译**里（`build-release\openttd.exe` = 22 670 336 B @ 09-14 01:39:53），只需**两次运行**：
1. **A 组＝探针全关**（步骤 1）：新开 cmd → `cd /d "D:\sourcecode of JGRPP"` → `set R3R_DBG=0` → `R3R_AB_probe.cmd` → 菜单 **[1] 采集 R3R**，等它自己跑完。
2. **B 组＝探针全开**（步骤 2/3）：**再新开一个 cmd**（不带 `R3R_DBG`）→ `R3R_AB_probe.cmd` → 菜单 **[1] 采集 R3R** → 跑完按 **[3] 生成报告**。
两次必须同存档、同时长、同样操作节奏。
产出对照：`build-release\R3R_perf.log` ＝ 最后一次（B），`D:\r3r_probe\AB\R3R_perf_prev.log` ＝ 前一次（A）——**先跑 A 再跑 B**，两份就都在。
- 步骤 1 判据：A 组 `dbgWrite=0`、`edgeGate=0`；同窗口 `loco`(B) − `loco`(A) ＝ 日志 I/O ＋ 边沿闸门的真实代价（B 侧繁忙窗口为 `dbgWrite=24 ms/s`、`edgeGate=4`）。
- 步骤 2 判据：B 组出现 `ord= prv= load= dep= sub= resid=`，且 `resid` 从 8.33 ms/帧 落到 1 ms/帧 以内即算切开（按 KI-27 读作上界）。
- 步骤 3 判据：B 组 `glTrains=` 有值（≠ -1.00），与同窗口 `loco` 相除即得口径 A 的同源倍率。

### 09-14 02:02 实测结果（A/B 已跑完）
用户实跑顺序与上文相反：**先跑的那次没带 `R3R_DBG`**（1:55:31，探针 ON）→ `D:\r3r_probe\AB\R3R_perf_prev.log` ＝ 探针 ON，`build-release\R3R_perf.log`（2:02:16）＝ `R3R_DBG=0`。判定依据：ON 组 `dbgWrite=35…344/s`，OFF 组恒为 `0.0/s`。
同存档同负载（`mov/s` 差 1%，`chains=911 vehs=42555 artic=28534 waitCouple=130` 全同），重载窗口对照（fps 49.7 / 50.9）：

| 项 | 探针 ON | `R3R_DBG=0` | 差 |
|---|---|---|---|
| `glTrains` | 16.63 ms/帧 | 11.47 ms/帧 | **−5.16** |
| `loco` | 511 ms/s | 357 ms/s | −154 ms/s（−3.0 ms/帧）|
| `dbgWrite` 桶 | 45 ms/s 0.04MB/s | 0 | 只解释了 0.88 ms/帧 |
| `plat` | 53 ms/s | 9 ms/s | −44 |
| `resid` | 192 ms/s | 136 ms/s | −56 |
| `ctrl`/`vp`/`spd` | 143/58/46 | 113/46/38 | −30/−12/−8 |

结论：**探针真实代价 ≈5.2 ms/帧（19.6 ms 帧的 26%），`dbgWrite` 桶少报了 4 倍**——`dbg==nullptr` 时被跳过的"拼装日志内容"循环（整链快照之类）本就算在包围桶的计时里（KI-17 已记）。
步骤 2 的切分（OFF 组重载窗口 fps=50.9）：`loco=7.01` ＝ `ctrl 2.22` ＋ `vp 0.90` ＋ `spd 0.75` ＋ `plat 0.18` ＋ `dep 0.16` ＋ `ord 0.10` ＋ `load 0.04` ＋ `prv 0.00`（`sub=4.34`）＋ **`resid 2.67`**。验收"`resid` < 1 ms/帧"未达。
步骤 3：OFF 组 `glTrains=11.47 ms/帧`，对 09-13 的 vanilla 0.72.4 基线 9.14 ms/帧 ⇒ 口径 A ≈ **1.25×**；要摸到 1.1× 还需再砍 ≈1.4 ms/帧。

### 下一轮（三件小事，按性价比排序）
1. **把总开关默认值反过来**（改成 `R3R_DBG=1` 才开）：现在 exe 默认探针 ON，等于白送 5 ms/帧＝26% 帧预算；仅此一条就把 trains 阶段 16.63 → 11.47 ms/帧。
2. **给 `resid` 加子桶**：`resid=2.67 ms/帧` 已是 `ctrl` 之后第二大项，先插 `stat`（`TrainEnterStation` 及其内部），再看 `TrainPhysics`/`UpdateStatus` 这类每车循环。
3. **把 `R3RScopeTimer` 也纳入总开关**（当前无条件运行 ≈6.5k 次/帧 ≈ 0.3~0.7 ms/帧），并给 9 处裸 `fopen` 补 `R3RDbgOn()` 守卫（冷路径，属账目卫生）。
顺带：跑一次菜单 **[2] 采集 JGRPP**，拿同机同存档的 vanilla `glTrains` 基线，口径 A 的 1.1× 判据才完全同源。

### 09-14 第 2/3 条落地（代码已改，等新 exe 实测）
用户 09-14 03:44 把第 1 条改判成"**按构建树分角色**"：**`build\` = 内测版，探针 ON**（日常就用它测）；**`build-release\` = 发行版，探针 OFF**（**只在发新 release 时**才编）。实现方式=**按构建类型自动推导**，不再依赖手传 flag：`src/r3r_perf.h` 里 `#ifndef R3R_PROBES_DEFAULT` 时 `#if defined(_DEBUG)` ⇒ 1、否则 ⇒ 0（显式 `-DR3R_PROBES_DEFAULT=<n>` 优先）。实测 `build\build.ninja` 的 FLAGS 含 `-MTd … -D_DEBUG` ⇒ ON；`build-release` 是 `/MD` RelWithDebInfo ⇒ OFF。运行时 `R3R_DBG=1` 仍可强行打开。`R3R_release_build.cmd` 另加显式 `-DR3R_PROBES_DEFAULT=0` + `[5d]` 硬闸门（`build.ninja` 里查不到该 define 即 `EXIT_CODE=94`，在建之前拦下）作双保险；`build\` 侧反向闸门写在 `R3R_fullrebuild.cmd` 里（查到 `-DR3R_PROBES_DEFAULT=0` 就拒绝，内测版必须留探针）。第 2、3 条已实现：

1. **两层开关**（`src/r3r_perf.h`）：`R3RDbgOn()` 管 `R3R_debug.log` 的一切写入（+ 编译期默认值 `R3R_PROBES_DEFAULT`），新增 `R3RPerfOn()` 管计时器与桶，`R3R_PERF=0/1` 可单独覆盖，缺省跟随 `R3R_DBG`。`R3RScopeTimer`（train_cmd.cpp）与 `R3RPerfTimer`（r3r_perf.h）按 `on` 快照走，关掉时连 `QueryPerformanceCounter` 都不取。
   - A/B 想拿"无日志写入"的干净分布，用 **`R3R_DBG=0 R3R_PERF=1`**；只写 `R3R_DBG=0` 会让 PERF-TICK 所有桶为 0，AB 脚本退回 fallback 窗口并打中文警告（预期行为，不是 bug）。
2. **`resid` 再切四刀**：`dec`（DECOUPLE 闸门整块，含 DEPOT-ARR 快照）、`wpr`（waypoint 到达掉头回退）、`stk`（卡死车路径）、`rev`（`Reversing` 标志掉头）；并把原先漏减的 `coupleH`、`edgeGate` 计入 `sub`。
3. **新增 `nl=` 字段**：`nl = glTrains − loco`，即列车 tick 里不在 `TrainLocoHandler` 内的那部分 —— 11.47 ms/帧 里剩下的 ≈4.46 ms/帧 应该落在这里，下一轮的靶子就是它。
4. **9 处裸 `fopen` 全部收编**（KI-17 闭合），并给 7 个文件补 `#include "r3r_perf.h"`；linter 0 error。

⚠️ 本轮改了头文件（`r3r_perf.h` / `vehicle_base.h` / `yapf_common.hpp`）：按记忆 66636022，本机 ninja 对头文件改动不触发重编，**必须删光 `build-release\*.obj` 全量重编**（已删，09-14 已后台启动，-j2，预计 25–50 分钟；日志 `build-release\R3R_release_build.log`，完成标志看 `R3R_release_build.done` 里的 `EXIT_CODE`）。Debug 的 `build\` 本轮**没动**（它仍是 09-13 02:10:35 的 50 222 080 B 老 exe，不含 09-14 的两层开关 / 四子桶 / `nl` / fopen 收编），要用它当最新内测版需同样删 obj 全量重编，入口见下面新增的 `R3R_fullrebuild.cmd`。
- ✅ **构建已完成（09-14 03:05:17）**：`NINJA_EXIT=0` / `EXIT_CODE=0`，`build-release\openttd.exe` = 22 674 432 B；`build.ninja` 的 DEFINES 含 `WITH_ZLIB/WITH_LIBLZMA/WITH_ZSTD/WITH_LZO/WITH_PNG/WITH_OPUSFILE`（KI-25 闸门通过），exe 内含 `R3R_DBG`、`R3R_PERF`、`dec=`、`sub=…resid=`、`nl=%.0fms/s glTrains=` 字面量 —— 新开关与四子桶已进二进制，可以开始取组 C/组 D。
- ✅ **AB 工具已对齐（09-14）**：`R3R_AB_probe.ps1` 的 `parseR3R` tick 白名单加入 `dec/wpr/stk/rev/sub/resid/nl`，`report` 段新增这四项的输出行（该 ps1 仍为纯 ASCII，`R3R_fix_gbk.ps1` 复核 `ASCII-OK`）。

### 09-14 03:44 收口：两棵构建树的角色 + 双击重编入口（KI-28）

**分工（用户拍板）**：`build\` = **内测版**（Debug，探针 **ON**）＝ 日常拿来测的那份；`build-release\` = **发行版**（RelWithDebInfo + `/O2`，探针 **OFF**）＝ **只在发新 release 时才编**（用户原话："发布新 release 再编译 build-release 里面的"）。两棵树**都必须全量重编**（本机头文件依赖跟踪失效，KI-15），所以统一了一个入口：

- **`R3R_fullrebuild.cmd`**（纯 ASCII，双击即用；**跑之前先关掉 CodeBuddy**，KI-24）：双击出菜单 `[1] build-release（发行，探针 OFF，默认按回车即选）` / `[2] build（内测，探针 ON）` / `[0] 取消`，也支持命令行 `R3R_fullrebuild.cmd release` / `R3R_fullrebuild.cmd debug`。脚本流程 = 打印可用物理内存与 `openttd.exe` 占用警告（KI-24 / LNK1168）→ `pause` 等你确认 → `del /s /q` 目标树全部 `*.obj` → 查探针闸门（内测树查到 `-DR3R_PROBES_DEFAULT=0` 就拒绝）→ 组装/构建 → 回显 `EXIT_CODE`。发行分支转调 `R3R_release_build.cmd`（configure + vcpkg 库闸门 + `[5d]` 探针闸门 + `ninja -j2` + LTCG 链接，输出进 `build-release\R3R_release_build.log`）；内测分支自己 `call vcvars64.bat` + `ninja -C build -j2 openttd`（TEMP 指到 D 盘），输出直接显示在窗口。
- ⚠️ **修正上面 272 条**：`build-release\openttd.exe`（22 674 432 B @ 09-14 03:05:17）是在**加 `-DR3R_PROBES_DEFAULT=0` 之前**编的，**探针是 ON 的** —— 它的价值是"新开关 + 四子桶已进二进制的实证"，**不是合格发行版**。发新 release 前必须双击 `R3R_fullrebuild.cmd` 选 [1] 重编，并用 `build.ninja` 的 DEFINES 复核 `-DR3R_PROBES_DEFAULT=0`。
- ✅ **改动已验证（09-14 03:44）**：用新 `r3r_perf.h` 单独编了一个 release 目标文件（`build-release/CMakeFiles/openttd_lib.dir/src/pbs.cpp.obj`，**只编不链接**，`build-release\openttd.exe` 未被触碰），`NINJA_EXIT=0` ⇒ 头文件语法无误。顺带再次坐实 KI-15：cl 输出的仍是中文 `注意: 包含文件:`，ninja 的 `deps=msvc` 拿不到任何依赖。
- **日常改 .cpp 的增量编译仍由 AI 代劳**（ninja 会发现 .cpp 变新、只重编该 TU + 链接，快）；**只要动了 `src\*.h` 就必须走 `R3R_fullrebuild.cmd` 全量重编**，不许增量。

**新 exe 出来后要取的两组数**（都走 AB 工具菜单 [1]）：
- 组 C：`R3R_DBG=0 R3R_PERF=1` → 干净分布，看 `resid` 被四个新桶切掉多少、`nl` 报出 handler 外多少；
- 组 D：`R3R_DBG=1` → 探针全开，与组 C 相减即新桶在有日志写入时的抬升（对照 5.2 ms/帧）。

### 09-14 收口②：发行版重编被自身闸门拦死 → CMake 未引用 `R3R_PROBES_DEFAULT`（KI-29）

`R3R_release_build.cmd` 09-14 04:02 的发行版全量重编 **`EXIT_CODE=94` 失败**，日志末段原文：

> `[5d]` HARD GATE: build.ninja does not carry -DR3R_PROBES_DEFAULT=0

根因：脚本 `[4]` 传了 `-DR3R_PROBES_DEFAULT=0`，但 **`CMakeLists.txt` 从头到尾没有引用该变量**，configure 只打了一句警告就继续：

> CMake Warning: Manually-specified variables were not used by the project: R3R_PROBES_DEFAULT

⇒ 该 define 从未进入 `build.ninja` 的 `DEFINES` ⇒ KI-28 的 `[5d]` 双保险闸门在建之前就把自己拦死了。闸门本身没做错（它确实挡住了"探针 ON 的坏发行版"），但没有这次 CMake 侧修补，**发行版就永远编不出来**；故 `build-release\openttd.exe` 至今仍是 09-14 03:05:17 那份"探针 ON"的实证 exe（见上面 280 条）。

**修复（09-14，`CMakeLists.txt`）**：在 `OPTION_NO_TAGGED_PTRS` 之后、`enable_testing()` 之前新增

```cmake
if(DEFINED R3R_PROBES_DEFAULT)
    add_definitions(-DR3R_PROBES_DEFAULT=${R3R_PROBES_DEFAULT})
    message(STATUS "R3R probes default override -- -DR3R_PROBES_DEFAULT=${R3R_PROBES_DEFAULT}")
endif()
```

`${R3R_PROBES_DEFAULT}` 的引用让 CMake 认定变量已被使用（警告消失），并写入目录属性 `COMPILE_DEFINITIONS`（→ `build.ninja` 的 `DEFINES = …`，与 `WITH_*` 同一行）。

**验证（09-14，只 configure 不编译）**：用与脚本 `[4]` 完全一致的参数重跑 `build-release` configure ⇒ 输出 `-- R3R probes default override -- -DR3R_PROBES_DEFAULT=0`；`findstr /C:"-DR3R_PROBES_DEFAULT=0" build-release\build.ninja` ⇒ `GATE_5D=OK`；`build\build.ninja` 不含该串（内测树反向闸门 OK，探针保持 ON）。

**下一步（需用户执行，AI 无法代跑：全量编译须先关 CodeBuddy，KI-24）**：

1. 关掉 CodeBuddy → 双击 **`R3R_fullrebuild.cmd` 选 [1]**（发行树全量重编；`CMakeLists.txt` 变更按 KI-15 必须全量，不许增量）→ 等 `build-release\R3R_release_build.done` 里出现 `EXIT_CODE=0`。
2. 想拿最新内测版：再双击选 **[2]**（`build\` 全量重编，把 09-14 的两层开关 / 四子桶 / `nl` / fopen 收编带进二进制）。
3. 新 exe 出来后按上面"两组数"取组 C / 组 D；顺带菜单 [2] 重采同机 JGRPP vanilla 基线（09-13 那份 9.14 ms/帧 与本次非同次运行，不可直接比）。

### 09-14 收口③：性能工作搁置，转入功能缺陷（用户拍板）

用户 09-14 拍板：R3R : JGRPP **总处理时长比**已从约 **2 : 1** 降到 **25 : 18**（≈1.39×），**性能降级为「低」并暂时搁置**，转去修功能缺陷。决策依据、口径前提、搁置边界与遗留资产见 `R3R_KNOWN_ISSUES.md` 顶部「决策记录」。

- **不再推进**：上面"两组数"里 `组 C / 组 D / nl / 同机 vanilla 基线"这一串——它们是为判定"1.1× 是否可达"服务的；既然搁置，暂不取数、暂不再动 tick 路径。
- **转入功能**（按严重度排序）：**KI-06**（artic 端点角色错位 → "机车鼻对车底尾挂车失败"每 tick 死循环，高）、**KI-05**（解挂后 `COUPLE-FAIL` 刷屏，高）、**KI-01/KI-02**（借用排程全 NOSAVE / `orders_backup` 单层无栈，丢数据风险）。
- **封存提醒**：性能支线改动（`r3r_perf.h` 两层开关 / 四子桶 / `nl` / 9 处 `fopen` 收编）**＋ KI-29 的 `CMakeLists.txt` 修补**都还没经过一次成功的全量重编；而且改过 `src/*.h` ⇒ 下次任何构建都必须走 `R3R_fullrebuild.cmd` 全量（KI-15）。若想保住这批改动的实证价值，先跑一次 `[1]`/`[2]`；若直接转功能，也建议先 commit 锁住。
- **日常提醒**：功能测试请带 `R3R_DBG=0`，否则内测树（探针默认 ON）会白吃 ≈5.2 ms/帧（KI-17/KI-26），干扰任何与帧率/延迟相关的观察。

---

## 7. 09-16 第 19 轮收口：挂接分组（功能支线，步骤 4/5/6 已完成）

**这一轮做的不是性能，是功能**：新建「挂接分组」体系（独立于现有列车分组），作为挂接的**硬约束白名单**——同组才允许挂接；不同组即使贴上，也**视同「没有等待挂接的列车」**（原地等待、可继续找别的候选、不刷屏、不死循环）。

| 项 | 落点 |
|---|---|
| 设计备忘 | `R3R_couple_group_design_memo.md`（第 18 轮立设计，§12 第 18 轮记录 / **§13 第 19 轮记录**） |
| 代码新增 | `src/couple_group_type.h`、`couple_group.h/.cpp`、`couple_group_cmd.h/.cpp`、`couple_group_gui.h/.cpp`、`src/sl/couple_group_sl.cpp`、`src/widgets/couple_group_widget.h` |
| 硬约束接线 | `R3RCoupleAllowed` 5 处：`yapf_destrail.hpp:434`（E1）、`yapf_rail.cpp:166`（E2）、`train_cmd.cpp:5878`（E4）、`train_cmd.cpp:5955` / `5988`（E5/E6）；E3/E7 确认被覆盖，无需改 |
| 可见性 | 车辆详情窗口两行（挂接分组 / 命令归属 第 k 段 / 共 N 段）、depot 链状态列第二行（组名 + k/N）、分组窗口归属标记；共用查询 `R3RGetChainScheduleOwner()` |
| 存档 | `CGPP`（组池表）+ `CGVR`（稀疏表：仅有组且组有效的段头车），车辆表布局零改动 |
| 已知问题 | KI-43（depot 标记列未实测）、KI-45（本体，代码完成待实测）、KI-46（跨公司接口预留）、KI-48（存档）、**KI-49（步骤 6 可见性未实测）**、**KI-50（depot「段：N」口径改为含链头段）** |

**构建（已由 AI 完成）**：`R3R_fullrebuild.cmd debug` 全量重编，691 步，00:43:07 → 01:15，`EXIT_CODE=0`；`build\openttd.exe` = **50 609 152 B @ 2026-09-16 01:14:40**。本轮动过 `src/*.h`（`vehicle_base.h` / `couple_group.h`）⇒ 必须全量（KI-15），现已完成，**混合对象风险已消除**。

**待你做的只剩实测（步骤 7）**，清单在 `R3R_KNOWN_ISSUES.md` §4-7 第 5 点，摘要：
1. **老存档零回归**（最重要）：未指派任何组的存档，挂接 / 解挂 / 车辆窗口 / depot 显示应与改动前完全一致；
2. 两条车底分入**不同**组后贴在一起 → 不挂接、不刷屏、机车继续找同组候选或原地等待；
3. 分入**同**组后 → 正常挂接；
4. 连挂后 depot 两行数字与车辆窗口「第 k 段 / 共 N 段」是否一致；
5. 组改名 / 删除后两处显示是否同步（已知：车辆窗口改名变长需重开）；
6. 非列车车库与 RTL 布局无位移。

> 游戏内建议带 `R3R_DBG=0`（内测树探针默认 ON）；若要看 depot 两行是否被行高截断，属 KI-43/KI-49 的目测项。
