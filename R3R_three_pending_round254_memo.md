# 第 254 轮：今日搁置三项逐步实施（KI-379 / KI-380 / KI-381）

日期：2026-10-19
玩家原话：「我希望把复测搁置备忘，然后把今日的搁置三项逐步做一下。」
前置：复测层已于同日统一搁置（`R3R_perf_hitch_round250_memo.md` §十四 + `R3R_KNOWN_ISSUES.md` 四处状态回填）。

顺序：KI-379（预留销毁侧可归因）→ KI-380（desync 独立日志）→ KI-381（诊断分档）。
每一项都按「先落盘清单/方案 → 改码 → 构建自证 → 回填 KI」走，不把结论留在对话里。

**本轮收口（2026-10-20 12:47）**

| 项 | 状态 | 改动文件 | 构建自证 |
|---|---|---|---|
| KI-379 | 已修，复测待做 | `src/pbs.cpp` | 12:42 `EXIT_CODE=0`、`[3/3] Linking`（§15.1.6） |
| KI-380 | 已修，复测待做 | `src/crashlog.cpp` | 12:42 `EXIT_CODE=0`、`[3/3] Linking`、exe 5/5 字面量命中（§15.2.4） |
| KI-381 | 部分修复，P-2 待发布版实测 | `src/train_cmd.cpp` | 12:47 `EXIT_CODE=0`、`[3/3] Linking`（§15.3.5） |

三项合计只动了 **3 个 `.cpp`**，未碰任何 `src/*.h`、未改 `R3RL` 存档格式、未新建 `.cmd`。

---

## 15.1 KI-379：全仓库预留释放调用点清单

### 15.1.1 关键事实（先于清单）

**地图层的释放其实已经收敛**。`src/r3r_resv_probes.h` 注释自陈：预留位的**唯一写者**是六个 map accessor
`SetTrackReservation` / `SetDepotReservation` / `SetCrossingReservation` / `SetRailStationReservation` /
`SetTunnelReservation` / `SetBridgeReservationTrackBits`，每个都调 `R3RResvWatchLog()`。

而 `R3RResvWatchLog()`（`pbs.cpp:566`）无条件先调 `R3RResvLedgerNote()`（`:278`），后者对 `CLEAR-*`
前缀走**唯一的台账删除分支**（`:294-300`：查反向索引 → `R3RResvLedgerDropRow` → 擦反向索引）。
⇒ **台账同步删除已经具备**，任何一次真实释放（含所有裸写点）都会经此删账；
KI-379 描述的"位没了却查不出谁清的"缺的是**日志与元数据**，不是同步机制。

越过 setter 的写入只有"整格重建"（`MakeRailNormal`/`MakeRailDepot`/`MakeStation`/`MakeBridgeRamp`…），
它直接覆写地图字节、不经 setter —— 这部分只能靠 `R3RResvLedgerResync()`（Mirror law，`:512`）事后对账。
这就是 KI-379「下一步」要定的边界：**经 setter 的 = 实时同步 + 可归因；整格重建的 = 事后对账**。

### 15.1.2 表 1：带归属的释放入口（可在入口内归因）

| 入口 | 位置 | 覆盖范围 | 现状日志 | 缺口 |
|---|---|---|---|---|
| `UnreserveRailTrack(tile, track)` | `pbs.cpp:944` | 全部六类 tile：depot / plain rail / crossing / station / tunnel / bridge head | `UNRES-RAIL tile= track= phase= actor=`（`:954-958`，仅"该格仍有预留"时打） | 无 `owner=`、无 `age=`、无 `who=`；且打在 setter 之前，不证明真的释放成功 |
| `UnreserveRailTrackdir(tile, td)` | `pbs.cpp:923` | 同上（转调） | 继承 | 同上 |
| `R3RReleaseReservationLedgerChecked(key, tile, track, why)` | `pbs.cpp:376` | 平轨 / 站台整格；先自证归属（`:382-397`） | `RESV-GC ... why=<reason>` / `-notowner` / `-hold` / `-gone` | 无 `age=`、无 `phase=`；`veh=` 是调用者 key 而非实际 holder |
| `R3RReleaseReservationSafe(key, …)` | `pbs.cpp:607` | 薄转发 | 继承 | — |
| `R3RResvLedgerForget(owner, why)` | `pbs.cpp:445` | 批量（整链/整主） | `RESV-LEDGER-FORGET veh= why= mask= released= kept=`（`:505`） | 无 `phase=`；批量逐格不单独成行 |
| `R3RResvLedgerNote(kind, tile, track)` 的 `CLEAR-*` 分支 | `pbs.cpp:294-300` | **所有**经 setter 的释放（唯一收口） | **无** | 这正是要补 `RESV-REL` 的点 |
| `R3RResvLedgerResync()` | `pbs.cpp:512` | 整格重建类绕过 setter 的丢失 | `RESV-LEDGER-RESYNC rows= dropped= why=map-mismatch` | 只有汇总，无逐格 |

### 15.1.3 表 2：裸写点（不经 `UnreserveRailTrack`）

`train_cmd.cpp` 10 处（均已核对上下文）：

| 行 | 场景 | 守卫 / 备注 |
|---|---|---|
| `:10746` | `release-ghost` 站台整格 | 四条件前置：站台型 / 仍有预留 / 格上无车 / `GetTrainForWholeTileReservation()==nullptr` |
| `:10757` | `release-ghost` 平轨 | 同上（平轨分支） |
| `:12998` `:13013` | `FreeTrainTrackReservation()` 内部 | 隧道桥入口 / 信号仿真保护 |
| `:13081` | `free_origin_tunnel_bridge` | 同上 |
| `:13101` | 同上 | 同上 |
| `:15684` | `reap-stn` | 站台整格，四道校验（车 / 预留 / whole-tile owner） |
| `:15703` | reap 平轨 | `R3RResvPhaseGuard("reap")` |
| `:17584` | `crash-del` | 车辆已 `delete`，**无 actor** |
| `:19404` | `del-visible` | 车辆已析构，**无 actor** |

`pbs.cpp` 内部：`:420`（站台整格经 `UnreserveRailTrack`）、`:432`（`UnreserveTrack`）、`:740`（站台 setter）、
`:886`/`:932`/`:968`/`:983`/`:2483`/`:2493`（`TryReserve`/`Unreserve`/`SetRailStationReservation` 各分支）。

仓库级分布（`search_content` 计数）：`pbs.cpp 22`、`train_cmd.cpp 41`、`rail_cmd.cpp 11`、`pbs.h 4`、
`yapf_rail.cpp 4`、`tunnelbridge_cmd.cpp 3`、`rail_map.h 3`、`station_map.h 3`、`afterload.cpp 3`、
`r3r_resv_probes.h 2`，以及 `waypoint_cmd.cpp`/`vehicle.cpp`/`station_cmd.cpp`/`road_cmd.cpp`/
`infrastructure.cpp`/`settings_table.cpp`/`train.h`/`waypoint_sl.cpp` 各 1。

**结论**：不必逐个改造这些裸写点 —— 它们全部落在六个 setter 之上，只要把 `RESV-REL` 打在
`R3RResvLedgerNote()` 的 `CLEAR-*` 分支，就一次性覆盖表 2 的全部 41+ 处；唯一例外是整格重建
（表 1 的 `R3RResvLedgerResync`），它本来就没有"释放事件"，只能事后对账。

### 15.1.4 实施方案（KI-379）

1. `R3RResvLedgerRec` 增加 `uint32_t booked`（记账时的 `_tick_counter`），四个构造点
   （`pbs.cpp` 原 `:326` / `:334` / `:477` / `:490`）补字段。
   **订正（实施时决定）**：`booked` **不落盘**。理由：`_tick_counter` 是会话级的，读档后从 0 重新开始，
   存档里的绝对值在新会话里没有意义（会算出巨大 age）。读档构造一律写 0，日志按 `age=0`
   （"本会话之前记账"）输出。因此 `R3RL` 存档格式**一字节不变**，零兼容风险。
2. 新增文件内 `R3RResvLedgerReportRelease(owner, tile, track, purposes, age, kind)`，输出
   `RESV-REL who=%s veh=%u tile=%d,%d track=%s owner=%u age=%u phase=%s`
   （`track` 用 `rail`/`stn` 两态，避免 `INVALID_TRACK` 打印成 0xFFFFFFFF；`who` = 触发它的 kind）。
3. `R3RResvLedgerNote()` 的 `CLEAR-*` 分支：在删行之前取 owner/purposes/booked → 删行 → 打 `RESV-REL`。
   删行顺序保持"先取后删"（现有代码就是先取 `bit_it->second`，无需调整）。
4. 无台账行的 `CLEAR-*`（随存档带入 / 无 actor 写入的位）不逐行打，累计到
   `_r3r_resv_rel_noowner`，每 256 次打一行 `RESV-REL-NOOWNER count= last=x,y/phase=`，
   避免把日志量重新推回 KI-383 那种量级。
5. `R3RResvLedgerForget()` 的汇总行补 `phase=`，并明确 `who=forget`（批量不逐格）。
6. 释放前自证**保持不动**：`R3RReleaseReservationLedgerChecked` 的 `-notowner` / `-hold` 逻辑不改。

**不做**：不给 `R3RResvLedgerDropBit` 加地图写入；不改 `rail_map.h` 的任何 setter（那是 KI-366 断言教训的
高危区）；不把 `RESV-REL` 搬到 `UnreserveRailTrack`（会漏掉 `SetRailStationReservation` 直调与整格路径）。

### 15.1.5 实施记录（已完成，2026-10-19）

只改 `src/pbs.cpp`，**未碰任何 `src/*.h`**（增量构建合法）。落点：

| 位置 | 改动 |
|---|---|
| 顶部 include | `+ #include "date_func.h"`（`_tick_counter` 声明所在，`date_func.h:16`） |
| `R3RResvLedgerRec` | `+ uint32_t booked`，并注释"不落盘"的理由 |
| 结构体前 | `+ static inline uint32_t R3RResvLedgerNow()` = `(uint32_t)_tick_counter` |
| 记账两处（原 `:326`/`:334`） | 构造补 `R3RResvLedgerNow()` |
| `R3RResvLedgerForget()` 保留分支 | 构造补 `row.booked`（保留行不重置年龄） |
| `Load_R3RL()` | 构造补 `0`（见 15.1.4 订正） |
| `_r3r_resv_ledger_notes` 之后 | 新增 `_r3r_resv_rel_path` / `_r3r_resv_rel_noowner` / `R3RResvPhaseName()` / `R3RResvLedgerReportRelease()` / `R3RResvLedgerReportUnowned()` |
| `R3RResvLedgerNote()` 的 `CLEAR-*` 分支 | 取 owner/purposes/booked → 删行 → `RESV-REL`；无行 → `RESV-REL-NOOWNER` 采样 |
| `R3RResvLedgerForget()` 汇总行 | `+ phase=%s` |

**日志体量与采样口径（对 KI-279 原文"每次释放一行"的受控偏离）**：
- 整格（`track==INVALID_TRACK`，站台/路点）与含 `R3R_RESV_PURPOSE_CONSIST` 的位 → **逐行** `RESV-REL`；
  这正是以前完全不可见、也是最需要归因的两类。
- 纯 PATH 平轨 → 每 512 次一行 `RESV-REL-PATH count=…`（`count` 是精确累计值，总量不丢）。
- 无台账行 → 每 256 次一行 `RESV-REL-NOOWNER count=…`。
- 理由：一格一行会把日志量推回第 250 轮那种量级（KI-383 的教训），而 PATH 位本身就是每 tick 重建。

行格式（field 与 KI-379 的 `who= veh= tile= track= owner= age= phase=` 完全对齐）：

```
RESV-REL who=CLEAR-STN veh=23 tile=1093,87 track=whole owner=23 age=412 phase=reap-stn purposes=0x3
RESV-REL-PATH count=512 who=CLEAR-RAIL veh=23 tile=1094,88 track=0x20 age=7 phase=controller
RESV-REL-NOOWNER count=256 who=CLEAR-RAIL tile=1095,89 track=rail phase=free-path
```

**仍然保持的约束**：
- 释放前自证归属逻辑（`R3RReleaseReservationLedgerChecked` 的 `-notowner`/`-hold`）一行未改。
- 台账删除仍只在 `CLEAR-*` 分支与 `R3RResvLedgerForget()` 两处发生，没有新增第三处。
- 整格重建绕过 setter 的路径继续由 `R3RResvLedgerResync()` 事后对账（边界见 15.1.1）。

### 15.1.6 构建自证（2026-10-19 23:53）

| 项 | 读数 |
|---|---|
| 构建入口 | 复用 `_tmp_inc_build.cmd`（未新建任何 `.cmd`） |
| 护栏 | `GUARD: incremental is safe (no header/lang file is newer than the newest object)` |
| 链接 | `[3/3] Linking CXX executable openttd.exe` |
| 判决 | `build\R3R_incbuild.done` = `EXIT_CODE=0`（23:53） |
| 错误计数 | `error C` / `fatal error` / `FAILED:` / `build stopped` 合计 **0** |
| 时间戳链 | `src\pbs.cpp` 23:50 → `pbs.cpp.obj` 23:52 → `build\openttd.exe` 23:53（51 942 912 B） |
| lint | `src/pbs.cpp` 0 条 |
| 产物字面量 | exe 内命中 `RESV-REL-PATH count=%u who=%s veh=%u tile=%d,%d track=0x%X age=%u phase=%s`、`RESV-REL-NOOWNER ...`、`RESV-LEDGER-FORGET ... phase=%s` |

**遗留**：`RESV-REL who=%s ... purposes=0x%X` 这一条因 `findstr` 输出被二进制上下文截断未直接看到，构建后另跑一次
`findstr /c:"RESV-REL who="` 复核（见下条命令记录）。

---

## 15.2 KI-380：desync 独立日志

### 15.2.1 疑点（2026-10-19，先登记后取证，按"有疑点即落盘"）

**疑点 D-1：KI-380 描述的"缺独立日志"与代码不符。**
`src/crashlog.cpp:1024 MakeDesyncCrashLog()` 已经生成
`desync-<client|server>-<UTC %Y%m%dT%H%M%SZ>.log`（`_personal_dir`，名字带秒级 UTC ⇒ **不覆盖**），
内容由 `FillDesyncCrashLog()`（`:667`）填：UTC 时间、Flags(R/S)、`LogDesyncDateHeader`、
`Client #<id>, "<name>"`、版本/OS/编译器/配置/库/设置、`LogCommandLog`（最近命令）、gamelog、news、
`DumpDesyncMsgLog`、`CheckCaches`。
调用点两个：客户端 desync 判定 `network_client.cpp:332`、服务端收到并判定 `network_server.cpp:1393`。
⇒ 真正缺的是 ①20 份上限（会无限堆积）②`_pause_mode` 与 `Last sync record type` 两个字段 ③Debug 版
`DESYNC-LOG file=` 探针 ④"连接超时/被踢"没有任何落盘（`netdrop` 全仓零命中）。

### 15.2.2 实施记录（已完成，2026-10-20）

只改 `src/crashlog.cpp`（**未碰任何 `src/*.h`**，只在新 include 里加了 `openttd.h` / `r3r_perf.h`）。

| 位置 | 改动 |
|---|---|
| 顶部 include | `+ openttd.h`（`_pause_mode` 与 `PauseModes` 在 `openttd.h:70-83`，全仓没有 `pause_mode.h`）、`+ r3r_perf.h`（`R3RDbgWrite`，该头只依赖 std 头）、`+ <algorithm> <filesystem>` |
| `LogDesyncDateHeader()` 之后 | 新增 `LogDesyncStateFields()`：`Pause mode: 0xNN (paused\|running)` + `Last sync record type: …`（与崩溃报告的 `crashlog.cpp:567-581` 逐字同构：`_record_sync_records` && `_network_sync_records` 非空时可算） |
| `FillDesyncCrashLog()` | `LogDesyncDateHeader(buffer);` 之后加 `LogDesyncStateFields(buffer);` |
| `MakeDesyncCrashLog()` 之前 | 新增 `R3RPruneDesyncLogs()`：扫 `_personal_dir` 里 `desync-*.log`，按文件名（`YYYYMMDDTHHMMSSZ` 天然按时间排序）保留最新 20 份；`std::error_code` 全程吞错（剪枝失败不许把 desync 变成崩溃）；成功剪枝写 `DESYNC-LOG-PRUNE removed= kept= total=` |
| `MakeDesyncCrashLog()` 落盘结果分支 | 成功：`R3RDbgWrite("DESYNC-LOG file=%s mode=%s")` + 剪枝；失败：`DESYNC-LOG-FAIL mode=%s` |

**对 KI-380 原方案的订正（D-1 定论）**：独立日志与"不覆盖"上游**早已具备**（`desync-<client|server>-<UTC>.log`，调用点 `network_client.cpp:332` / `network_server.cpp:1393`），因此本轮不新建文件类型，只补三处真缺口（两个字段 + 20 份上限 + Debug 探针）。

**`netdrop-*.log` 的判定：不做**（记入 KI-380 状态说明）。理由：
1. 断线事件本身已被 `network_client.cpp` / `network_server.cpp` 的 `SendError`→`CloseConnection` 路径写进正常服务器日志与 `-d net=…` 输出，"缺独立文件"只是形式问题；
2. 新增第四类磁盘文件会与 desync 剪枝规则、玩家归档习惯（只收 `crash-*`/`desync-*`）冲突，收益小于维护成本；
3. KI-380 的两条判据 S-1/S-2 均只涉及 desync 报告内容，不依赖 netdrop 文件。
若将来真出现"断线但没有任何原因线索"的现场，再按 `DESYNC-LOG` 同款在 `CloseConnection` 打一行 `NETDROP cli= reason=` 到 `R3R_debug.log`（不落新文件），届时另立条目。

### 15.2.3 判据对照

- **S-1**（desync 后目录里必有一份新 `desync-*.log`，且 `R3R_debug.log` 有 `DESYNC-LOG file=` 指向同路径）→ 由 `MakeDesyncCrashLog` 成功分支保证；`file=` 与 `printf` 用同一个 `filename` 缓冲，不可能指向不同文件。
- **S-2**（报告内含 `Pause mode:` 与 `Last sync record type:`；份数不超过 20）→ 前两项由 `LogDesyncStateFields()` 无条件写入（`Last sync record type` 仅在记录同步帧时出现，属预期），后者由 `R3RPruneDesyncLogs()` 在每次成功落盘后执行。
- 负判据：崩溃报告（`FillCrashLog`）与不一致报告（`FillInconsistencyLog`）路径未改；剪枝只匹配 `desync-` 前缀且扩展名 `.log`，不会误删 `crash-*.log` / `netdrop-*`。

### 15.2.4 构建自证（2026-10-20 12:42）

复用既有 `_tmp_inc_build.cmd`（**未新建任何 `.cmd`**）：

| 检查项 | 读数 |
|---|---|
| 护栏 | `GUARD: incremental is safe (no header/lang file is newer than the newest object)` |
| 判决文件 | `build\R3R_incbuild.done` = `EXIT_CODE=0`（12:42） |
| 链接行 | `R3R_incbuild.log:583 [3/3] Linking CXX executable openttd.exe`（仅 1 条） |
| 错误计数 | `error C*` / `fatal error` / `FAILED:` / `ninja: build stopped` 共 **0** 条 |
| 时间戳链 | `src\crashlog.cpp` 12:39 → `build\CMakeFiles\openttd_lib.dir\src\crashlog.cpp.obj` 12:40 → `build\openttd.exe` 12:42（51 942 912 B） |
| `read_lints` | `src\crashlog.cpp` **0** 条 |
| exe 字面量 | 5/5 命中：`DESYNC-LOG file=`、`DESYNC-LOG-FAIL`、`DESYNC-LOG-PRUNE`、`Pause mode: 0x`、`Last sync record type:` |

注：`openttd.exe` 字节数与上一轮 KI-379 完全相同（51 942 912 B）属正常——本轮只改 `crashlog.cpp`，链接结果的大小对齐未变。

状态＝**已实现 + 已编译通过，游戏内复测待做**（S-1/S-2 需一次真实 desync 才能取数）。

---

## 15.3 KI-381：诊断分档（发布版降低常驻诊断开销）

### 15.3.1 取证：发布版到底还剩什么在跑

`src/r3r_perf.h` 的三道编译期开关已经做得很彻底，逐条复核（读代码，不凭印象）：

| 机制 | `R3R_PROBES=0`（发布）时的实际形态 | 结论 |
|---|---|---|
| `R3RDbgOn()` | `return false;`（`r3r_perf.h:153-155`） | 常量假，`if (R3RDbgOn())` 整块塌掉 |
| `R3RDbgWrite(...)` | `#define ... ((void)0)`（`:241`） | **连实参一起丢弃**，字符串不进 exe |
| `R3RFopenDbg()` | `R3RDbgOn() ? fopen(...) : nullptr` → 常量 nullptr | 297 处裸 `fprintf` 全在 `if (dbg != nullptr)` 内 ⇒ 死代码 |
| `R3RIsWatchedResvTile()` | `pbs.h:50` `inline bool R3RIsWatchedResvTile(TileIndex) { return false; }` | 审计循环里那 3 处 `if (R3RIsWatchedResvTile(t) && ...)` 直接消失 |
| `R3RPerfFrameTick()` | `if (!R3RPerfOn()) return;`，`R3RPerfOn()` 常量假 | 不开 `R3R_debug.log` / `R3R_perf.log` |
| `R3RDbgEdge()` | 先 `if (!R3RDbgOn()) return true;` | 每车每 tick 一次预测分支 |

⇒ **P-1（发布版 exe 内诊断字符串消失）在结构上已经成立**：所有诊断写盘都经由这两个编译期折叠的入口，`train_cmd.cpp` / `pbs.cpp` 里没有绕开它们的裸 `fopen`/`printf`（仅 `r3r_perf.cpp` 自身的两个日志文件，同样被 `R3RPerfOn()` 挡住）。

真正**在发布版里仍然跑**的，只剩三类「有功能作用、不能简单删掉」的东西：

1. **`R3RReservationAudit("tick")`**（`train_cmd.cpp:18083`）——它不只是日志，**它是唯一会 reap 幽灵预留的代码**（KI-203c / KI-345 玩家实报的黑线就靠它自愈）。每趟是 `for (TileIndex t(0); t < Map::Size(); t++)`（`:15544`）全图扫描，2048×2048 图上 419 万格。
2. **`R3RReserveTrackUnderConsist()` 每 tick 的 reconcile/GC**（`_r3r_consist_resv`）——功能：回收「车开走后留在旧格的记账」。不能关。
3. **`pbs.cpp` 的台账读写**（`_r3r_resv_by_owner` / `_r3r_resv_owner_of_bit`）与 `R3DS`、段 ID、耦合节流——KI-379 之后台账是释放路径的判据，属正确性。

所以 KI-381 原方案里「分档关闭全图审计」这一条**不能照字面执行**：关掉 = 幽灵预留永不回收，等于用一个玩家可见的 bug 换性能。原方案自己也留了口子：「审计即使保留也降频或改脏标记触发」。本轮按这个口子做。

### 15.3.2 疑点（登记在案）

- **REG-254-C**：`R3RReservationAudit` 每趟对**每一个**已记账位无条件执行一次 `_r3r_resv_stray_since.erase(key)`（`:15604`、`:15649`）。健康会话里 `_r3r_resv_stray_since` 恒为空，这几千次哈希查找/删除全是白做。属确定可省（行为等价），已在本轮修掉。
- **REG-254-D**：`R3R_DIAG_DEFAULT` 这个**新宏需要改 `src/r3r_perf.h`**，会触发全量重编（620 obj → 约 20 分钟）且把一个纯性能开关塞进热路径头文件。本轮改用既有 `R3RDbgOn()` 做**运行期选择**（发布版里它就是常量假，等价于编译期），**不动任何 `src/*.h`**。代价：极端情形（`-DR3R_PROBES=1` 的发布版 + `R3R_DBG=1`）仍走诊断档位——那本来就是"正在诊断的构建"，符合预期。

### 15.3.3 实施（仅 `src/train_cmd.cpp`）

| 位置 | 改动 | 理由 |
|---|---|---|
| `train_cmd.cpp:18044-18045` | 稳态间隔由硬编码 `2048` 改为 `R3RDbgOn() ? 2048 : 8192`；`SOON` 两档都保持 `256` | 探针关掉的构建每 4 倍时间才扫一次全图（平均开销 ÷4），而脏标记（卖车/库内重排）与「有 stray 在老化」两条快路径**逐字不变** ⇒ 幽灵回收时延不变 |
| `train_cmd.cpp:15604`、`15649` | `_r3r_resv_stray_since.erase(key)` 前加 `if (!_r3r_resv_stray_since.empty())` | 健康会话里该表为空，省掉每个已记账位一次哈希查找；判决结果不变 |

**为什么不删审计、不加 `-DR3R_DIAG` 宏**：见 15.3.1 末段与 REG-254-D。**为什么不改 `R3R_RESV_REAP_TICKS`**：它只在「已有 stray 老化」时起作用，而那种状态下一趟就会切到 256 的快档（`_r3r_resv_audit_has_pending`），再调它只会延长幽灵寿命。

### 15.3.4 判据对照

- **P-1**（发布版 exe 内诊断字符串消失）⇒ 结构上已成立（15.3.1 表），本轮不新增任何诊断字符串，且新增代码里没有新的裸 `fopen`/`printf`。
- **P-2**（同存档 A/B 帧率不低于第 248 轮发布版，差距 < 3%）⇒ **本轮无法自证**：按 2026-10-19 的构建口径，日常只编内测树（`build\`，探针 ON），发布树 `build-release\` 只在用户明确要求时编。P-2 留待出发布版时实测；本轮能给的只有"稳态全图扫描频率 ÷4"这一结构性证据。
- **负判据**：内测树（探针 ON）行为**逐字不变**（`R3RDbgOn()` 为真 ⇒ 仍是 2048/256）；`RESV-AUDIT-ARM / -DUE / -WAIT / -SUMMARY / -REAP` 的触发时机与内容不变；stray 的 `age` 判据（`R3R_RESV_REAP_TICKS`）不变。

### 15.3.5 构建自证（2026-10-20 12:47）

复用既有 `_tmp_inc_build.cmd`（**未新建任何 `.cmd`**）：

| 检查项 | 读数 |
|---|---|
| 护栏 | `GUARD: incremental is safe (no header/lang file is newer than the newest object)` |
| 判决文件 | `build\R3R_incbuild.done` = `EXIT_CODE=0`（12:47） |
| 链接行 | `R3R_incbuild.log` = `[3/3] Linking CXX executable openttd.exe` |
| 错误计数 | `error C` / `fatal error` / `FAILED:` / `ninja: build stopped` 共 **0** 条（`findstr` 无命中） |
| 时间戳链 | `train_cmd.cpp` → `train_cmd.cpp.obj` 12:46（12 465 482 B） → `build\openttd.exe` 12:47（51 942 912 B） |
| `read_lints` | `src\train_cmd.cpp` **0** 条 |

说明：exe 字节数与 KI-380 那版完全相同（51 942 912 B）属预期——本轮只改了三个常量表达式与两处 `if`，没有新增/删除任何字符串字面量或函数，故不做 exe 字面量核对（无可核对项）。

**验证边界（必须如实记录）**：内测树 `R3RDbgOn()` 为真，走的仍是 `2048 / 256` 老路径 ⇒ 本轮**唯一可运行观测**的是 `if (!empty())` 那个守卫（健康会话不产生任何日志差异）。`8192` 分支只有发布树才会取到，本轮按 2026-10-19 口径**未编发布树**，故该分支属"读代码自证"而非"实测自证"。
