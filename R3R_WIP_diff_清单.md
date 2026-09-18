# R3R 09-10~09-14 未提交工作 差异清单

- 仓库 `d:\sourcecode of JGRPP`；提交 `2db952d332`（2026-09-14 23:49:16）；基线 `f3ebaad870`（09-09 checkpoint）
- 现状：工作区被 `git reset` 回到 `f3ebaad870`，以下改动不在工作区，也不在当前 `build\openttd.exe`（09-15 00:22:35）内
- 规模：18 文件 / +1234 −205（`src/train_cmd.cpp` 独占 +1008 −205；`src/r3r_perf.h` 新增 196 行）
- 完整 diff 附件：`_d_small.txt`（17 文件 791 行）、`_d_train.txt`（train_cmd.cpp 1254 行）

## 结论先说

1. **本批含现场死循环的正解**（§F KI-06 判据收敛）。当前 exe 由回退后源码编译，故 09-15 00:32 日志必然复现死循环。
2. **18 文件闭环依赖，挑着恢复编译不过**：新增的 `src/r3r_perf.h` 被 12 个文件 `#include`，且所有 `fopen("R3R_debug.log","a")` 都换成了 `R3RFopenDbg()`。最小可编译集 = 全部 18 个文件。
3. 恢复后按 KI-15 必须**全量重编**（改过 `src/*.h`）。

## A 探针基建（前置依赖）

- `src/r3r_perf.h`（新增）：`R3RPerfCounters`/`R3RP()`；`R3R_PROBES_DEFAULT`（无定义时按 `_DEBUG` 推导，Debug=1/Release=0）；`R3RDbgOn()`（环境变量 `R3R_DBG=0/1`）；`R3RPerfOn()`（`R3R_PERF`）；`R3RFopenDbg()`（关闭时返回 nullptr）；`R3RDbgWrite()`；`R3RPerfTimer`/`R3RScopeTimer`；`R3RPerfFrameTick()`（每 128 帧 dump）。
- `CMakeLists.txt`：`if(DEFINED R3R_PROBES_DEFAULT) add_definitions(...)`（注释记为 KI-28；缺它 `R3R_release_build.cmd` 的 `[5d]` 闸门报 `EXIT_CODE=94`）。

## B fopen 统一替换（14 文件）

train_cmd.cpp ~40 处、yapf_rail.cpp 18、yapf_destrail.hpp 5、yapf_common.hpp 1、vehicle_cmd.cpp 2、pbs.cpp 1、station_cmd.cpp 1、vehicle_base.h 1；另 7 个文件只加 include。效果：`R3R_DBG=0` 一键静默全部探针。

## C KI-14 探针洪水治理（train_cmd.cpp）

`R3RDbgEdgeTag`（FOLDCHK / FOLDCHK_DIR / RESERVECONSIST / SKIPSTOPPED / TTB / COUPLEFAIL / CPLS0）+ `R3RDbgEdge(tag,key,payload)` 状态未变即吞掉 + `R3RDbgEdgeReset()`。覆盖 FOLDCHK、FOLDCHK-DIR、RESERVECONSIST×2、TTB-PROBE、COUPLE-FAIL、CPL-S0-CHK、SKIP-STOPPED；DEPOT-ARR 的 fopen 移进 `s_dbg_dump_sigs` 变化分支内。实测背景：38 秒 28 838 行 / 2.1 MB，每行一次 fopen/fprintf/fclose。

## D KI-26 帧预算拆解

train_cmd.cpp 新增 static 计时桶：loco / coupleH / plat / resv / edgeGate / ctrl / coll / spd / vp / dump / mov / ord / prv / load / dep / dec / wpr / stk / rev（刻意放 .cpp 内避免动头文件）。`R3RPerfDumpAndReset()` 输出 PERF + PERF-TICK 两行（含 `sub= resid= nl= glTrains=` 与世界快照 chains/vehs/maxChain/segs/artic/waitCouple）。
- `framerate_gui.cpp` 新增 `R3RGetTrainsMs()`（取 `_pf_data[PFE_GL_TRAINS]`，与游戏内 framerates 同源）
- `window.cpp` `UpdateWindows()` 每帧调 `R3RPerfFrameTick(delta_ms)`
- `newgrf_engine.cpp` `PositionHelper()` 加 pos_helper/pos_steps/pos_max（暴露段内计数 O(n²)）

## E 路线A：段优先权 + 排程借用（核心功能）

数据模型（`vehicle_base.h`，均 NOSAVE）：`uint16_t r3r_priority = 1`（段在列车内排名，最小者=命令属主）、`bool r3r_orders_borrowed = false`。

新增（train_cmd.cpp ~3768）：`R3RGetSegmentHeads` / `R3RGetLowestPriority` / `R3RGetPriorityHead` / `R3RMergePriorities` / `R3RRenumberPriorities` / `R3RRebuildCouplePriorities`（导出）/ `R3RSyncDrivingOrders`。

- `Couple()`：合并前先抓 passive/active 段表 → 合并优先权 → 求 `couple_owner`；**删掉 `u->orders = nullptr`**，改为链头**借用**属主排程（`v->orders = couple_owner->orders; v->r3r_orders_borrowed = true;`），属主始终保留自己的指针。
- `DecoupleTrain()`：交接判据 `orders_backup != nullptr` → `r3r_orders_borrowed`；解出的 u 仅在自己无排程时才继承（`u_inherited_running`）；WAIT_COUPLE 插前逻辑只对该情形生效；两侧各自 renumber + sync。
- `train.h` 声明；`sl/vehicle_sl.cpp` 读档后 `if (part_of_load) R3RRebuildCouplePriorities(t);`
- `order_cmd.cpp DeleteVehicleOrders()`：先归还借用再删自己的，避免释放属主排程留下悬空指针。

> 解决 T8701 模型2/3 的三个硬伤（orders_backup 单层、计划孤儿、整份车组排程被发走），也是 KI-02/KI-05 的修复。

## F KI-06 判据收敛 —— 死循环正解

```cpp
// 旧（当前工作区仍是这个）
return R3RCheckChainFold(head, tag) > 8 || R3RCheckChainFoldedDirection(head, tag);

// 新（2db952d332）
const int worst_gap = R3RCheckChainFold(head, tag);      // 只为出 FOLDCHK 诊断行
if (R3RCheckChainFoldedDirection(head, tag)) return true; // 只有方向能否决
if (worst_gap > 8) R3RDbgWrite("FOLDCHK-ACCEPT %s worst_gap=%d residual gap (chain still closing up), not a fold\n", ...);
return false;
```

注释写明的两条理由：(a) artic part 渲染在父车精确 x/y，合法一对报 `dist=0` 对名义 `exp=5`，任何"更近=穿插"判据在每列铰接车上误报；(b) `ArrangeTrains()` 只重连链、不重算位置，刚逻辑拼接的一对保留拼接前间距，旧 `gap>8` 把该瞬态当折叠 → 每 tick 全候选回滚 → 死循环。

## G 崩溃/窗口防御

- `train_cmd.cpp R3RDestroyCarOnlyFormation()`：补 `CloseWindowById` × 5（VehicleView/Orders/Refit/Details/Timetable）——09-10 朋友崩溃根因
- `vehicle.cpp PreDestructor()`：无条件 `CloseWindowById(VehicleView, index)`
- `viewport.cpp UpdateNextViewportPosition()`：跟随车辆已释放 → `CancelFollow`
- `window.cpp HandleViewportScroll()`：veh 为空 → `CancelFollow`

## H 临时诊断（建议恢复后再删）

- `sl/vehicle_sl.cpp`：`LOADCENSUS-ENTER/LEAVE` + `LOADCENSUS-PHASE2END`（**无条件遍历全部车辆写日志**）
- `vehicle.cpp ShowVisualEffect()`：`VIS-MISS` 现场 dump
