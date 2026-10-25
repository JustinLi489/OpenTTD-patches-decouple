# R3R 第 266 轮：解挂互锁新判据「铺出预留」放行过早（后车抢跑）

## 一、玩家报告（2026-10-21，第 265 轮改动的第一次实机复测）

> 「这回前车没完全留出到安全信号位的距离后车就启动了，不符合我预期。」

日志：`D:\temp\codebuddy-dropped-files\e73b5d3a-1f31-45f5-a8c5-e08adc848bea\R3R_debug.log`

⇒ 第 265 轮的 `TryPathReserve(consist, false, false)` 成功被当成"可以起步"，但这个成功
**并不等于前车已经让出到下一个安全信号位的距离**。这与我上一轮的预判边界逐字吻合：
「若出现『后车先动、前半还压在站台咽喉』，说明判据放得太宽」。

## 二、猜测根因（待验证，先落盘）

- **G-1（首要怀疑，结构性）**：`TryPathReserve` 的语义是"**我这条路径上的每一格路权都能订到**"，
  而 PBS 的路权按格/按轨位互斥。前车刚起步时，它**已经离开的那几格**立刻释放，后车就能把这些格
  连同路径尽头那个安全位一起订下 ⇒ 预留成功 ⇒ 放行。但玩家的要求是前车**让出到下一个安全信号位
  的距离**（即两车之间要留出一个完整的 block/信号区间），这比"后车能订到路权"严格得多。
  ⇒ 新判据必须叠加"前车已经越过某个安全信号位"这一条件，否则永远比玩家预期早。
- **G-2（旧 path 残留）**：`first_tile_okay=false` 只排除"终点==脚下这一格"。若后车旧 path 的
  终点本来就在**自己前方的站台格**（安全位），`train_cmd.cpp:15679` 的
  `origin.okay && moving_front->tile != origin.tile` 直接返回 OK ⇒ 假放行，且这一 tick 根本
  没有重新寻路。
- **G-3（判据对象错）**：`TryPathReserve` 只看"后车自己能不能订到"，完全没有"前车在哪"的信息。
  即便前车还在同一站台的下一个格，只要该格已被前车预留释放（前车已向前移动了一格），就通过。

## 三、取证计划（每组结果立刻回填本文件）

1. 抓 `DECOUPLE-INTERLOCK set/wait/release` 三行，看放行落在第几个 tick、`reason` 是什么、
   `dist` 是多少（`dist < R3R_DECOMP_CLEAR_TILES` 即证明走的是 `reason=reserved`）。
2. 同 tick 前后抓前车的实际位置（`DECOUPLE-DONE` 后的 `tx/ty`、以及后车 `tile`），
   算两车真实间距。
3. 抓 `RESERVECONSIST` / `TRP` 行确认那次预留铺到了哪一格（路径终点是否就是后车自己的站台内）。

## 四、修法方向（待取证后定）

- **A（最贴合玩家原话）**：把放行条件改成"预留成功 **且** 前车已越过下一个安全信号位"。
  安全信号位的判定需要复用引擎的信号查询（`HasSignalOnTrackdir` / `FindTrainExitDir` 一带），
  或者退化成"前车 `seam_car` 已离开站台格 **且** 距离 ≥ 某值"，但那就是回到几何判据。
- **B（折中）**：保留能力判据，但要求后车预留的**第一个格**不再是前车当前所在格的相邻格
  —— 即要求两车间至少隔一个空 block。
- **C（最小改动）**：把"预留成功"降级为**必要条件**，仍要求前车驶离站台格；这样等于回到第 265 轮
  之前的行为，不可接受。

## 五、未确认项

- U-1：玩家说的"安全信号位"是**出站信号机**还是"两车之间的闭塞区间"？
- U-2：日志中后车放行那一 tick，前车 `seam_car` 的 `dist` 具体值。

## 六、取证结论（2026-10-21，日志 5 行全量 + 关键区段）

### 6.1 事故链（`R3R_debug.log:4184-4240`）

| 行 | 读数 | 判定 |
|---|---|---|
| `:4184` | `DECOUPLE-INTERLOCK set head=21 follower=24 seam_car=47 tile=272,237` | 登记成功；`seam_car=47` 是**前车链尾** |
| `:4219` | `TPR-BLOCKED veh=24 tile=273,237 origin=265,237 okay=0 other=21 otherTile=267,237 otherSpd=9` | 后车预留失败：路径终点 `origin=265,237`＝**前车屁股后面那一格**，被前车 21 挡住 |
| `:4221` | `DECOUPLE-INTERLOCK wait … dist=0 res=0 age=0` | **能力判据在第 265 轮实现后一次都没成立过**（`release reason=reserved` 全日志 0 条）|
| `:4224-4239` | `TPR-BLOCKED … otherTile=266,237 → 265,237 → 264,237 → 263,237`、`CLEAR-PATH veh=47 tile=272→271→270→269,237` | 前车一路向左（出站方向）驶离，后车的"安全位"始终贴在前车屁股上（跟着推进），故 `res` 恒 0 |
| `:4240` | `DECOUPLE-INTERLOCK clear head=21 follower=24 seam_car=47 dist=4 gone=0` | **放行来自几何判据 `dist >= R3R_DECOMP_CLEAR_TILES(4)`**，不是能力判据 |

### 6.2 结论句

1. 抢跑的真凶不是第 265 轮新增的能力判据，而是**第 228 轮留下的 `dist >= 4` 快速放行**。
   本次站台 48 格（`DECOUPLE-FIRE … plat=48/0`），前车尾车只走了 4 格（`272,237 → 268,237`），
   两车实际间距 5 格，**仍同处站台区间**，玩家看到的正是"没留出到安全信号位的距离"。
2. **能力判据 `TryPathReserve` 在本场景结构性地不可能成立**：PBS 给后车算出的"下一个安全停车位"
   就是"前车屁股后面那一格"（`origin` 每 tick 跟着前车推进 265→264→263…），于是
   「后车能订到路权」= 「前车屁股露出来一格」，永远比玩家预期早。这条判据单靠方位学无法补救，
   必须叠加"前车已让出真正的信号区间"。
3. ⇒ 修法：**删掉 `dist >= 4`**，几何快速放行只认"前车尾车已驶离站台格"
   （站台末端即出站信号机，尾车离开站台格 = 整列越过出站信号 = 让出整个站台区间）；
   能力判据保留（前车真正让出到信号位时它能提前放行），超时兜底从 900 tick 提到 3600 tick。

## 七、实现落地（第 266 轮）

- `R3R_DECOMP_CLEAR_TILES`（4）**删除**：距离阈值无法表达"安全信号位"，只会抢跑。
- 几何放行条件由 `seam_car == nullptr || !IsRailStationTile(seam_car->tile) || dist >= 4`
  改为 `seam_car == nullptr || !IsRailStationTile(seam_car->tile)`；`dist` 仍计算、仍进日志（只读诊断）。
- `R3R_DECOMP_WAIT_LIMIT` 900 → 3600：长站台（本次 48 格）里前车若在站内临时停车，
  900 tick 会误触发强制放行 ⇒ 再次抢跑。
- 能力判据、节流 `R3R_DECOMP_PROBE_INTERVAL`、判据优先级其余部分**逐字不变**。

## 八、待复测判据

1. 同场景（48 格站台、前车向左出站）：`DECOUPLE-INTERLOCK` 的 `clear` 必须等
   `seam_car` 那辆尾车的 tile **不再是站台格**才出现（日志里 `dist` 会明显 > 4，可能几十格）。
2. 等待期后车必须完全静止（`cur_speed==0`）且不出现在站台格以外；`TPR-BLOCKED veh=24`
   可以照旧刷（那是它在探路），但 **不得**出现 `RESERVECONSIST`/`RESV-AUDIT-STRAY` 增长。
3. 前车真正让出到信号位后，若能力判据先成立，应出现
   `DECOUPLE-INTERLOCK release … reason=reserved`（早于尾车离开站台格）—— 这是新判据的正向价值。
4. 负判据：车库/野外解挂仍不登记；无关列车不出现任何 `DECOUPLE-INTERLOCK` 行。
5. 超时：正常场景不应出现 `DECOUPLE-INTERLOCK timeout`。

## 九、代码改动（第 266 轮，仅 src\train_cmd.cpp，未碰任何 src\*.h）

锚点均为改动后行号：

1. `src\train_cmd.cpp:2291-2295` 函数头注释：几何放行条件由"驶离 N 格"改为"前车尾车
   离开站台格（= 越过出站信号机的位置）"，并注明这是玩家第 266 轮口径。
2. `src\train_cmd.cpp:2302-2308`：删除 `static const uint R3R_DECOMP_CLEAR_TILES = 4;`，
   `R3R_DECOMP_WAIT_LIMIT` 900 → **3600**（长站台 48 格 + 站内临时停车时 900 tick 会误超时）。
3. `src\train_cmd.cpp:2395 一带`：放行判据由
   `seam_car == nullptr || !IsRailStationTile(seam_car->tile) || dist >= R3R_DECOMP_CLEAR_TILES`
   改为 `seam_car == nullptr || !IsRailStationTile(seam_car->tile)`；`dist` 仍计算、仍进
   `DECOUPLE-INTERLOCK clear/wait/release` 日志（纯只读诊断，不再是判据）。
4. 能力判据（`TryPathReserve(consist,false,false)` + `TPRRF_RESERVATION_OK`）、节流
   `R3R_DECOMP_PROBE_INTERVAL=4`、`R3RDecoupleInterlockWait()` 其余分支**逐字未动**。
5. 剪枝循环（`:2335` 附近）跟着 `R3R_DECOMP_WAIT_LIMIT` 一起放宽到 3600，属同一常量，无独立改动。

## 十、构建自证（第 266 轮）

- 首次尝试：`_tmp_inc_build.cmd` 的护栏前置检查发现 `build\openttd.exe` 正在运行
  （`tasklist` 有 `openttd.exe` PID 23684），脚本按既有约定直接写
  `build\R3R_incbuild.done = EXIT_CODE=97`（18:05:46）并退出，**未开始编译**。
  同时 `build\R3R_incbuild.guard.log` = `GUARD: incremental is safe (no header/lang file
  is newer than the newest object)` ⇒ 本轮只改 `src\train_cmd.cpp`，护栏判定为合法增量。
- 待玩家关闭游戏后重跑，结果见 §十一。

## 十一、构建自证（第 266 轮，2026-10-21 18:12 完成）

- 前置：`Get-Process openttd` 空（游戏已关闭），占用拦下的 `EXIT_CODE=97`（18:05:46）不再阻挡。
- 启动方式：复用既有入口 `Start-Process cmd.exe -ArgumentList '/c','_tmp_inc_build.cmd'
  -WorkingDirectory 'd:\sourcecode of JGRPP' -WindowStyle Minimized`（**未新建任何 .cmd**）。
- `build\R3R_incbuild.guard.log` = `GUARD: incremental is safe (no header/lang file is newer
  than the newest object)` ⇒ 本轮改动全在 `src\train_cmd.cpp`，增量合法。
- 日志尾部：`[2/3] Building CXX object CMakeFiles\openttd_lib.dir\src\train_cmd.cpp.obj`
  → `[3/3] Linking CXX executable openttd.exe`（Linking 恰 1 次）。
- `build\R3R_incbuild.done` = `EXIT_CODE=0`（18:12:14，13 B）。
- 错误计数：`error C` / `fatal error` / `FAILED:` / `build stopped` 在日志中命中 **0** 条。
- 时间戳链：`src\train_cmd.cpp` 18:05:28 → `train_cmd.cpp.obj` 18:10:32 →
  `build\openttd.exe` **18:12:07（52 036 096 B）**，obj 晚于源码、exe 晚于 obj。
- `read_lints(src\train_cmd.cpp)` = 0 条。
- 本轮未新增日志字符串（`DECOUPLE-INTERLOCK` 三行标签沿用第 228 轮字面量），故不做 exe 串核对。
- 发布树 `build-release` **未编译**，仍停留在旧版本（仅内测树推进到第 266 轮）。
- 状态＝已实现 + 已编译，游戏内复测待做；判据见 §八（核心第 1 条：`clear` 行必须出现在
  `seam_car` 那辆尾车**离开站台格之后**，`dist` 会明显大于 4）。

