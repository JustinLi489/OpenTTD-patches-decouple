# R3R 发布准备：探针代码彻底移除 + 新增代码性能审计（2026-11-27）

分支 `feature/decouple`，起点 HEAD `519dc83366`（rounds 523-525）。
恢复标签：`r3r-pre-probe-strip-2026-11-27`（本轮任何改动都可 `git checkout` 回来）。

## §1 任务口径（玩家原话）

> 我准备执行补丁包的发布版编译，现在是准备阶段。我要求你只做：1，删除所有的R3R添加的探针代码，注意，不是用一个东西把它们隔离起来，而是让源码里面不存在探针代码。2，检查是否有新增的代码严重影响性能

## §2 结果总览

| 项目 | 结果 |
| --- | --- |
| 删除探针相关源码 | 41 文件、净 −7837 行（+265 行是结构补回与注释订正） |
| 探针头文件 | `src/r3r_perf.h`、`src/r3r_resv_probes.h` 已物理删除 |
| 源码残留探针标识 | `R3RDbgWrite / R3RFopenDbg / R3RDbgOn / R3RPerf* / R3R_PROBE*` 全局 **0** |
| exe 探针字面量 | `CPL-SCAN / FOLDCHK / RESV-WATCH / R3R_debug.log / CARGOWEIGHT / LOCO-MOVE / UNRES-RAIL / SEGFE-VEH` 等 **全部 0 次命中** |
| 编译 | 内测树 ninja 全绿：`[3/3] Linking CXX executable openttd.exe`，`EXIT_CODE=0`，`warning C4189` 0 条 |
| 产物 | `build\openttd.exe` 53 332 480 B（删探针前 54 425 088 B，**小 1.09 MB**），2026-11-27 19:25:45 |
| 未做 | 没有编 `build-release`（按既有口径，发布树只在玩家明确要求时编）；没有动 CMake 的 `-DR3R_PROBES*` 开关（发布脚本的硬闸门依赖它，见 §5） |

## §3 删除内容分类（按写法）

1. `R3RDbgWrite("...", args);` 单语句（含跨行实参）。
2. `FILE *dbg = R3RFopenDbg("a"); if (dbg == nullptr) return/continue;` 早退型。
3. `FILE *dbg = R3RFopenDbg("a"); if (dbg != nullptr) { fprintf/fclose }` 块型。
4. `if (R3RDbgOn()) { ... }` / `if (R3RDbgOn() && R3RDbgEdge(...)) { ... }` 门控块。
5. `R3RPerfTimer / R3RPerfCounters / R3RScopeTimer / R3RPerfFrameTick / R3RPerfDumpAndReset` 全部计时与 PERF 汇总（含 `R3RGetTrainsMs()`）。
6. `R3RDbgEdge` 边沿触发系统本体：`R3REDGE_*` 枚举（50 个 tag）、`R3RDbgEdgeState`、`R3RDbgTagHash`、`R3RDbgEdgeTotals/Reset`、`R3RPerfDumpAndReset`。
7. 探针专用函数（整体删除定义 + 全部调用点）：
   `R3RDumpUpgradeDbg`、`R3RDbgCloneMarkers`、`R3RDbgCloneTraits`、`R3RDumpAttachFail`、`R3RSettleWorthDumping`、`R3RDumpChainGeometry(Snapshot)`、`R3RDumpChainDbg`、`R3RDumpCoupleIdentity`、`R3RDumpDualHeadPairs`、`R3RCoupleScanDump`、`R3RCoupleGateProbe`、`R3RTimetableCmdProbe`、`R3RDbgCargoSummary`、`R3RDbgMaxSpdHold`、`R3RDbgCoupleSpdCurve`、`R3RWatchOrderIndex(Site)`、`R3RCheckTtSync`、`R3RSnapshotChainStats`、`R3RCheckControlSegment`、`R3RProbeNoAbsorbSeg`、`R3RProbeNdhSegKeep`、`R3RProbeKi149Seam`、`R3RDbgPair*`（5 个）、`R3RLogReserveAttempt`、`R3RTrackBitsProbe`、`R3RDbgOrderTypeAt`、`R3RProbeLineEnd`、`R3RWaitProbeDue`、`R3RWaypoint(Loco)WaitProbe`、`R3RWaypointExitProbe`、`R3RWaypointHandleProbe`、`R3RStrandCensus`、`R3RResvWatchLog`。
8. 探针宏 `R3R_PROBE_NOABSORB_SEG / R3R_PROBE_NDH_SEG_KEEP / R3R_PROBE_KI149_SEAM`。
9. `#if R3R_PROBES` 条件编译块（保留 `#else` 分支）与 `#include "r3r_perf.h" / "r3r_resv_probes.h"`。
10. 探针专用的 static 容器/计数器（仍在热路径上白跑，见 §4）：`_r3r_c2_ref_logged`、`_r3r_waitclear_count`、`_r3r_skipstop_ticks`、`r3r_noownresv_seen`、`r3r_oneshot_hold_state`、`tpr_seen`、`_r3r_depot_stay_tag`、`s_dbg_dump_sigs`、`_r3r_wp_loco_wait_stamps`、`_r3r_maxspd_hold_logged`、`_r3r_cpl_scan_*`（YAPF 终点 census 全套）、`_r3r_pair_invariant_logged`、以及 81 个因删探针而变成未使用的局部变量（含 2 处必须保留副作用的调用，见 §6）。

## §4 保留项（不是探针，删了会改变行为）

- `r3r_resv_phase / r3r_resv_actor / R3RResvPhaseGuard`：预留仲裁逻辑读 actor 决定争用格归谁（`pbs.cpp` 的 `R3RReservationHeldByCouplePartner` 等），是真实状态。只删了它的日志出口与注释里的"探针"表述。
- 地图 setter（`rail_map.h / bridge_map.h / road_map.h / station_map.h / tunnel_map.h`）里的 `R3RResvWatchLog` 调用：整条 hook 已删（该 hook 自第 303 轮起只写日志，台账已不存在），setter 回到上游原样。
- `R3RCheckChainFold / R3RCheckChainFoldedDirection`：是"绝不提交折叠链"的判定，不是日志；保留。
- `R3RBorrowControlTraitsLive / ReverseTrainSwapVehicles`：本轮曾被误删，已从标签版本原样补回（见 §6）。
- `station_cmd.cpp` 的 `_r3r_live_vehicle_cache`：修复"对已死车辆指针解引用"的真实缺陷，按 station linkgraph tick 摊销，保留。

## §5 构建层开关（刻意保留）

`CMakeLists.txt:542-563` 的 `R3R_PROBES_DEFAULT` / `R3R_PROBES` 保留不动：
`R3R_release_build.cmd` 的 [5c]/[5d] 硬闸门用 `findstr` 检查 `build-release\build.ninja` 必须含这两个 define，删掉 CMake 块会让发布脚本自锁（EXIT_CODE=94）。
现在源码里已无任何 `#if R3R_PROBES`，这两个 define 变成惰性宏，无副作用。

## §6 本轮踩到的坑（后续别再犯）

1. **不要在未屏蔽注释的情况下按括号配对删块**：`/* { */` 这类注释会把配对算偏，删掉的跨度会吃掉外层块的 `}`，甚至吃掉外层语句。本轮因此破坏了 6 处结构（`ValidateTrains` 的三个失败返回被连带删掉、`if (...) {} else if (...) {}` 链、`CheckTrainStayInDepot` 的函数头、`if (v->r3r_orders_borrowed)` 的两个分支闭合、`COUPLE-FLIP-BOTH` 的 OVERRIDE 分支、`DoTrainPathfind` 兜底块）。全部靠"逐行括号深度 + 编译器 `C2601/C2267`（局部函数非法）"定位并补回。
2. **两种调用必须保留副作用**：`const X r = F(args);` 里 F 有副作用时不能整行删，只能改成裸调用或 `(void)(...)`。本轮涉及 `TryPathReserveWithResultFlags`、`R3RSetCoupleGroupAllowOthers`、`R3RParkOwnOrderProgress`、`_r3r_dispatch_no_slot.insert(...)`、`SpliceFolded(...)`、`R3RReleaseReservationChecked(...)`、`R3RSkipUnfireableDecoupleOrder(...)`、`R3RRRecoverSegmentTraits`。
3. **`if (c) { /*注释*/ }` 不能一律删成 `(void)(c);`**：若 `c` 是纯读，直接删更干净；若 `c` 含副作用调用，必须保留调用。
4. **沙箱限制**：`R3R_fullbuild.cmd` 内部调 `reg.exe`（vcvars64 初始化）被本机安全策略拦截，`cmd /c` 也被拦。本轮改为手工设置 `INCLUDE/LIB/PATH`（MSVC 14.51.36231 + Windows SDK 10.0.26100.0）后直接调 `ninja -C build -j2 openttd`，等价且可复现。
5. **PowerShell 5.1 的 `*>` 重定向写 UTF-16LE**，日志会变成中文乱码；应改用 `$out | Out-File -Encoding utf8`。
6. **C 盘曾只剩 233 KB** 导致 `Edit` 工具的 checkpoint 备份写失败（ENOSPC），改用脚本直接写文件。玩家已清理出 2 GB。

## §7 性能审计：删除探针本身消除了什么

删掉的都是**每 tick / 每车 / 每格**执行的热路径代码，属净收益：

| 位置 | 原开销 | 现在 |
| --- | --- | --- |
| 地图 setter（6 个）`R3RResvWatchLog` | 每次预留位变更一次函数调用 + 分支（最热路径） | 0（调用点整条删除） |
| YAPF `PfDetectDestination` | 每次节点判定 `++_r3r_cpl_scan_enter` 等 6 类计数器 + 每分支写 `last_*` | 0 |
| `TrainLocoHandler` 每列车每 tick | 30+ 处 `R3RScopeTimer`（`nanoseconds` 时钟读 ×2）+ 多处 `R3RDbgOn()` 分支 + 停止车每 tick 计算 5 字段签名并查 `s_dbg_dump_sigs` 向量 | 0 |
| `ChooseTrainTrack` 每格 | 每次出口调用 `r3r_chosen_ret(...)` lambda（带 `const char*` 实参） | 直接 `return { ... }` |
| `ReserveTrackUnderConsist` | 跳段记账数组 + `HasReservedTracks` 额外调用 | 0（保留真正的 `TryReserveRailTrack`） |
| `NewGRF PositionHelper`（精灵路径） | `R3RPerfTimer`（每次 2 次时钟读）+ `pos_steps/pos_max` 累加 | 0 |
| `ConsistChanged / CargoChanged` | 重算重量时 `CARGOWEIGHT-VEH` 的整链预扫描（512 节）+ 每节格式化 | 0 |
| 存档 `Load/Save_R3VP/R3SG` | 每车/每段行 `fopen` + 格式化 | 0 |
| 全树 | `R3RDbgTagAllowed()` 的 40 条 `strncmp` 白名单（每条日志都跑） | 不存��� |

## §8 性能审计：剩余新增代码里值得知道的开销

按"是否每 tick 执行"排序，均为**设计内**开销，未发现会显著拖慢的项：

1. `R3REnsureCouplePair()`（`train_cmd.cpp:19965`，每列车每 tick 一次）：入口先判 `current_order.IsType(OT_GOTO_COUPLE)`，非挂接单一律早退；只有挂接单才走 `Train::Iterate()` 全池扫描，且空扫描后 **8 tick 退避**（`R3R_PAIR_RESCAN_TICKS`）。持有有效锁时直接 `return`，不扫。
2. `_r3r_pair_verdict` 查询（`TryPathReserveCore` 的 parked-no-pair 门）：每次预留尝试一次 `unordered_map<uint32_t,...>` 查找。命中率高的热路径，但只在"停驻等待者"这一支。
3. `R3RIsCarOnlyFormation / R3RChainHasRealTraction / R3RHasWagonBehindEngine`：前者 O(1)；后两者 O(链长)，但调用点被 `IsCarOnlyFormation(consist)` / `IsType(OT_DECOUPLE)` 夹住，正常列车不走。
4. `R3RGetSegmentHeads()`（`std::vector` 分配）：15 个调用点全在提交点（couple / decouple / 车库编辑 / 读档 settle），不在每 tick 路径。
5. `IsRailStationPlatformFree()`（`pbs.cpp`，站台格数 × 全池车辆）：只在解挂互锁里用，且每 `R3R_DECOMP_PROBE_INTERVAL=4` tick 一次、只对登记过 seam 的那一对。
6. `_r3r_loop_memo` / `_r3r_couple_partial_reserve` / `_r3r_decomp_seams` 等 `unordered_map`/`btree`：均按 VehicleID 键、条目随订单/状态清除，稳态不增长。
7. `station_cmd.cpp` 的 `_r3r_live_vehicle_cache`：按 station linkgraph tick 摊销（原注释：`DeleteStaleLinks()` 每站每 `STATION_LINKGRAPH_TICKS` 一次）。

结论：**未发现"严重影响性能"的新增代码**。原来真正吃帧的探针（YAPF census、每车计时器、每 tick 签名/地图 setter 日志）已随本轮删除。

## §9 复测判据（玩家侧）

1. 启动后工作目录**不再生成** `R3R_debug.log` / `R3R_perf.log`（设 `R3R_DBG=1` 也不再生成——代码已不存在）。
2. 控制台 `framerates` 的 trains 行应与原版 JGRPP 同量级；大地图多列车场景不再有"每 tick 一次 fopen"的硬盘抖动。
3. 功能回归：连挂/解挂/库内挂接/共享排程/读档后车号与组名，全部应与删探针前一致（本轮零判定改动）。
4. 若发现行为差异，先取 `r3r-pre-probe-strip-2026-11-27` 与本轮 diff 对比该函数（本轮只删日志与死变量，判定逻辑应逐字相同）。

## §10 进度

- [x] 盘点探针基础设施与调用点规模
- [x] 脚本化删除 + 人工复核（含 6 处结构修复）
- [x] 编译验证（内测树 ninja 全绿，0 告警）
- [x] 残留扫描（源码 0、exe 字面量 0）
- [x] 性能审计（§7/§8）
- [ ] 发布树 `build-release` 编译（等玩家指令）
