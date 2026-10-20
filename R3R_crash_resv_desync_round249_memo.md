# R3R 第 249 轮（2026-10-19）：崩溃 C00000FD / 预留销毁无主 / desync 独立日志 / 发布版探针分离

> 状态：**取证与方案设计阶段（本文件先落盘，随后逐项取证与实施）**
> 输入：`C:\Users\冯洁敏\Downloads\crash-20261018T144344Z.log`（709 行）+ 玩家 2026-10-19 四条口头需求
> 规则依据：收到报告先落盘临时备忘 + KI 条目，之后才深挖代码（记忆 26350098 / 68719153）

---

## 零、输入与环境

| 项 | 值 |
|---|---|
| 崩溃时间 | 2026-10-18 14:43:44 (UTC) |
| 版本 | `r3r-stable-2026-10-16-1-g2fa4018d2-m` (2)，Release ver `0.73.1` |
| Build date | `Oct 18 2026 01:36:32` |
| Defines | `... R3R_PROBES_DEFAULT=0`（**发布版，探针在编译期关闭**） |
| 线程 | `Non-main thread (ottd:game)`（模拟线程，非 UI 线程） |
| 地图 | 4096x4096，1920-02-25，3 个客户端（misaka / 瓦片 / 套局，同 company 0） |
| NewGRF | 217 个（中国套件为主），`max_train_length: 64` |
| 网络 | `Network: server`（朋友是主机） |

---

## 一、① 崩溃日志分析

### 1.1 已确认事实（日志原文锚点）

1. **异常码 = 栈溢出**
   `crash-20261018T144344Z.log:8` → `Exception: C00000FD`
   `C00000FD` = `STATUS_STACK_OVERFLOW`（不是 `C0000005` 访问违规，也不是堆损坏 `C0000374`）。

2. **栈顶两帧是 ntdll 堆分配，但不是堆 bug**
   `:32` `[00] ntdll 0x00007FF9EC55DF47 RtlAllocateHeap + 13735`
   `:33` `[01] ntdll 0x00007FF9EC55B44D RtlAllocateHeap + 2733`
   ⇒ 栈已耗尽，进入保护页时仍在执行"分配/写入"动作，异常在 ntdll 抛出。**这是结果，不是原因**。

3. **openttd 模块栈帧呈严格两地址交替（互递归签名）**
   `:41` `[09] openttd 0x00007FF634CBCA69`
   `:42-95` `[10]~[63]` 恒为 `0x00007FF634CDE8CB` ↔ `0x00007FF634CBCF41` 交替，共 54 帧。
   - 交替 = **A 调 B、B 调 A**（或 A→B→A 的偶/奇链），不是单函数自递归（自递归会是同一地址重复）。
   - `[09]` 的 `CBCA69` 与循环里的 `CBCF41` 不同 ⇒ A 有两个返回地址（A 有分支，或 `CBCA69` 是 A 的另一个调用点）。
   - 报告只 dump 64 帧窗口，**真实深度远大于 64**（栈溢出通常需数千层）。

4. **触发点是一条命令，不是 tick 常规路径**
   `:15` `DoCommandP: tile: A37B95 (2965 x 2615), intl_flags: 0x7, company: 0, cmd: 0x31 CmdReverseTrainDirection, payload: 285, false`
   ⇒ 玩家（或 autoreplace/自动逻辑）对 **veh 285** 执行"反转列车方向"，命令执行期间递归爆栈。

5. **模块基址推定**
   `openttd` 帧地址均在 `0x00007FF634xxxxxx` ⇒ 基址约 `0x00007FF634000000`，
   `[02] 0x00007FF634FF2FA4` ⇒ RVA ≈ `0xFF2FA4`；`[09] 0x00007FF634CBCA69` ⇒ RVA ≈ `0xCBCA69`。
   ⇒ **可用 RVA 在 `build-release\openttd.pdb` / `.map` 中反查符号**（见 §1.4 步骤 1）。

6. **与"反向"有关的既有实现范围（待逐一对号）**
   `CmdReverseTrainDirection` → `ReverseTrainDirection()`（`src/train_cmd.cpp`），R3R 在该路径上额外做了：
   段级翻转 `R3RFlipChainBySegments`、假引擎/假双头身份迁移、`ReverseTrainSwapVehicles`、
   `UpdateStatusAfterSwap`、`ConsistChanged(CCF_...)`、`NormaliseSubtypes`、耦合优先级重建、解挂缝更新、预留重算。

### 1.2 疑点（本文件登记，供后续验证/证伪）

- **D-1**：`[02]~[08]` 是固定 7 帧，但日志只给地址无符号；无法判断它们属于 `DoCommandP` 调用链还是崩溃处理路径（`CrashLog`）本身。
  期望：解开符号后应能看到 `CmdReverseTrainDirection` / `ReverseTrainDirection` 之一。
  当前：无符号 ⇒ 存疑。
- **D-2**：`[09]` 与循环起点地址不同（`CBCA69` vs `CBCF41`），若两者是同一函数的不同内联点，则"互递归"判断需改为"单函数→B→单函数"的三段循环。需符号确认。
- **D-3**：`Last sync record type: BEGIN` 与本次崩溃是否相关未知；考虑到同轮要做 desync 日志（问题③），需确认崩溃前是否已存在同步异常（`BEGIN` 只是"最近一条同步记录类型"，**不能**推断 desync）。
- **D-4**：崩溃发生在 **Release 版**（`/O2 /Ob2`），内联会把源码级递归折叠成"地址交替"形态，**不能**指望源码里能肉眼看到 `f(){f()}`；必须靠符号 + 反汇编定位。
- **D-5**：payload 285 的车是否处于 R3R 多段/假双头/真铰接状态未知（崩溃日志不含车辆状态）。若能取到该存档，可直接 `dump_vehicle 285`。

### 1.3 猜测根因（待验证，勿当结论）

> 以下均为假设，验证后必须回来把状态改成「成立 / 证伪」。

- **G-1（主假设）**：`ReverseTrainDirection` 路径上出现**链表遍历互递归**——
  典型形态为 `GetNextUnit()/GetPrevUnit()`（`train.h`，会跳过 artic 与双头后半）与
  `IsDualHeadRear()/IsRearDualheaded()` 或 `ConsistChanged()`/`UpdateStatusAfterSwap()` 之间互相调用，
  当链上出现"自指/成环"（双头两半互指 `omp`、段头★⊗ 叠加、假引擎身份残留）时无终止条件。
  支撑：栈为两地址交替；触发点恰是反向命令（会重建 `First()/Last()` 视角与双头语义）。
  证伪判据：符号化后若循环里没有 `GetNextUnit`/`IsDualHeadRear` 家族，则本假设不成立。
- **G-2**：`R3R` 自身的段/链遍历 lambda 自调用（`R3RFlipChainBySegments` 系列）在"整链倒置 + 多段 + 假引擎"组合下漏 `visited` 上限。
  支撑：同族代码在别处都加了 `R3R_CHAIN_WALK_LIMIT=512` 之类上限（KI-217 教训），反向路径可能未加。
  证伪判据：符号里出现 `R3RFlipChainBySegments` / `ReverseTrainSwapVehicles`。
- **G-3（弱）**：`C00000FD` 也可能来自**超大栈上数组**（单帧几 MB），但两地址交替形态基本排除此可能。
  状态：倾向证伪，保留登记。

### 1.3b 符号化结果（2026-10-19 已完成 ⇒ G-1/G-2 **证伪**）

**基址修正**：真实模块基址 = `0x7FF633F90000`（不是原先推的 `0x7FF634000000`）。
判据=「栈帧必须是 `E8 rel32` 的返回地址且目标落在 .text」，对 10 个帧全帧扫描 64K 步长，唯一解（9/10 帧命中 E8，0 个 FF 间接）。

| 帧 | VA | RVA | 符号 | 源码位置 |
|---|---|---|---|---|
| [02] | 0x7FF634FF2FA4 | 0x1062FA4 | `_malloc_base` | malloc_base.cpp:34 |
| [03] | 0x7FF634405649 | 0x475649 | `BumpAllocContainer<CYapfRailSegment,1024>::New` | arena_alloc.hpp:130 |
| [04] | 0x7FF634418080 | 0x488080 | `NodeList<CYapfRailNode>::FindOpenNode`（内联 AddStartupNode） | nodelist.hpp:154 / yapf_base.hpp:235 |
| [05] | 0x7FF63441F48A | 0x48F48A | `FindPath` | yapf_base.hpp:165 |
| [06] | 0x7FF6344195B8 | 0x4895B8 | `Yapf`（内联 `ChooseRailTrack`） | yapf_rail.cpp:1265 / 1352 |
| [07] | 0x7FF63444643A | 0x4B643A | `stChooseRailTrack` | yapf_rail.cpp:1311 |
| [08] | 0x7FF634440E79 | 0x4B0E79 | `YapfTrainChooseTrack` | yapf_rail.cpp:1502 |
| [09] | 0x7FF634CBCA69 | 0xD2CA69 | `ChooseTrainTrack`（内联 `DoTrainPathfind`） | train_cmd.cpp:14092 / 13153 |
| [10] | 0x7FF634CDE8CB | 0xD4E8CB | **`TryLongReserveChooseTrainTrack`** | train_cmd.cpp:13831 |
| [11] | 0x7FF634CBCF41 | 0xD2CF41 | **`ChooseTrainTrack`** | train_cmd.cpp:14208→14211 |

**根因（已定性）**：`[10] ↔ [11]` 交替 54 帧 = 长预留（LR 信号）分支的**无界互递归**：
- `TryLongReserveChooseTrainTrack()`（:13748）末尾 `:13829` `HasLongReservePbsSignalOnTrackdir(v, ft.new_tile, ..., !long_enough, ...)` 为真时 `:13831` 回调 `ChooseTrainTrack()`;
- `ChooseTrainTrack()` `:14208` 又调 `TryLongReserveChooseTrainTrack()`;
- 二者**都没有深度/访问上限**，每层只前进一格；`long_enough` 恒 false（`IsReservationLookAheadLongEnough` 在 `!UsingRealisticBraking() || lookahead==nullptr` 时返 **true** 会停；但 `reservation_end_position` 不推进 / 或 trace-restrict 程序恒返 `TRPRF_LONG_RESERVE` 时）即无限递归。
- 触发=`CmdReverseTrainDirection`（veh 285）后的预留重算（TryPathReserve → ChooseTrainTrack）；YAPF 帧（[03]~[08]）只是最外层那次寻路仍在栈上，`_malloc_base` 是爆栈瞬间正在分配 YAPF 节点。
- **G-1（GetNextUnit/IsDualHeadRear 互递归）证伪；G-2（R3RFlipChainBySegments 无上限）证伪**。

**复现/取数工具**（可复用）：
`llvm-symbolizer --obj=openttd.exe --relative-address --print-address --functions=short <RVA...>`（MSVC 14.51 自带，LLVM 23；工作目录设 `build-release` 以规避 `--obj` 路径含空格；`--relative-address` 必需，否则一律 `??:0`）。
（临时脚本：`_tmp_symscan2.ps1` 做基址反推；`_tmp_sym_b3.txt` 存符号结果。）

### 1.3c 止血实施（2026-10-19，仅 `src\train_cmd.cpp`，未碰 `.h`）

**改动**（插在 `_long_reserve_disabled` 之后、`TryLongReserveChooseTrainTrack()` 之前）：

1. `static uint _r3r_long_reserve_depth = 0;` + `static const uint R3R_LONG_RESERVE_MAX_DEPTH = 128;`
2. `struct R3RLongReserveDepthGuard`：构造时 `++`（未超限才计），析构时 `--`（RAII，覆盖函数内全部 return 路径）。
3. `TryLongReserveChooseTrainTrack()` 入口在 `_long_reserve_disabled` 之后加门：
   `if (_r3r_long_reserve_depth >= R3R_LONG_RESERVE_MAX_DEPTH) { 打 LR-DEPTH-CAP（同 tick 内 64 节流）; return; }`，紧接 `R3RLongReserveDepthGuard depth_guard;`。

**为什么这样截断是安全的**
- 到达第 N 层时，"到本格为止"的预留已由上一层的 `ChooseTrainTrack`（`train_cmd.cpp:14137 result_flags |= CTTRF_RESERVATION_MADE` 一带）完成；被跳过的只是 `:14208` → `TryLongReserve` → `:13831` 这圈"越过下一个 LR 信号继续预留"的行为。
- 不变量：`_r3r_long_reserve_depth` 因 RAII 恒在函数出口归零，不会跨 tick / 跨车泄漏。
- 纯本地计数、不写游戏状态；"是否截断"只取决于地图与信号配置（同步数据），故不影响 desync。
- 128 的依据：每层只跨 **一个** LR 信号；满长列车（`max_train_length=64`）只需几十层；栈溢出时日志里 54 帧仅是 64 帧 dump 窗口，真实数千层，因此 128 远低于危险值又高于任何合法用量。

**构建自证（build 内测树，探针 ON）**
- 复用 `_tmp_inc_build.cmd`（未新建 .cmd）；`GUARD` 判增量合法；
- `[4/5] Building CXX object ... train_cmd.cpp.obj` → `[5/5] Linking CXX executable openttd.exe`；
- `build\R3R_incbuild.done` = `EXIT_CODE=0`（2026-10-19 14:29:14）；
- 错误计数（`error C|fatal error|FAILED:|build stopped`）= **0**；
- `src\train_cmd.cpp` 14:25:31 → `train_cmd.cpp.obj` 14:26:35 → `build\openttd.exe` 14:28:52（51 889 152 B）；
- `read_lints` 0 条；exe 内可检索 `LR-DEPTH-CAP`（1 处）。

**构建自证（build-release 发布树，玩家实际运行的那棵）**
- 复用 `_tmp_inc_build_rel.cmd`（未新建 .cmd）；
- `[5/5] Linking CXX executable openttd.exe`；
- `build-release\R3R_incbuild.done` = `EXIT_CODE=0`（2026-10-19 14:32:21）；
- 错误计数 = **0**；`src\train_cmd.cpp` 14:25:31 → `train_cmd.cpp.obj` 14:30:53 → `build-release\openttd.exe` 14:32:08（23 140 864 B）；
- 发布版 exe 内检索不到 `LR-DEPTH-CAP` 属**预期**：`R3R_PROBES_DEFAULT=0` 让 `R3RDbgWrite` 的整块与其字面量在编译期消失（也就是 KI-381 想要的那种"探针分离"），止血门禁本身不受影响。

### 1.4b 根本问题追查（2026-10-19，第一组取证，行号=当前源码，已含我新插的 38 行注释）

**事实 1：`TryLongReserveChooseTrainTrack()` 只有两个入口，都在 `TryPathReserve()` 里**
- `train_cmd.cpp:13899`（`TBM_REALISTIC` 分支，先 `VehicleOrderSaver` + `AdvanceOrdersFromVehiclePosition`/`FromLookahead`）、`:13901`（非 realistic 分支）；两处都传 `force_res = true`。
- 也就是说：递归不是"两个函数互相调用"，而是 **`ChooseTrainTrack` → `TryPathReserve` → `TryLongReserveChooseTrainTrack` → `ChooseTrainTrack`** 这一圈；`ChooseTrainTrack` 里调 `TryPathReserve` 的确切行待下一组取证（旧版行号 14208→14211，现源码 14208 已被"extend reservation"循环占用，故走 `TryPathReserve` 的应是该循环之后/`CTTF_NO_LOOKAHEAD_VALIDATE` 相关分支）。

**事实 2：`IsReservationLookAheadLongEnough()`（`:13625-13693`）是纯只读判定，递归期间**不会**随推进而变真**
- 早退为真：`!UsingRealisticBraking() || lookahead == nullptr`（`:13627`）、装货类订单/`OT_WAITING`（`:13629`）、`CTTLASF_STOP_FOUND` 或 lookahead 的 `DepotEnd`（`:13631`）、`reverse_distance` 已够（`:13633`）、aspect-limited 且余量 >24（`:13637`）。
- 主判据（`:13650-13692`）：遍历 `v->lookahead->items` 找信号，用真实制动距离判断 `signal_position + distance <= reservation_end_position`。
- **关键**：它读的全是 `v->lookahead`（上一次 `FillTrainReservationLookAhead()` 的产物）与 `lookahead_state.flags`，**与本次递归已经越过了多少个 LR 信号、已订了多少格完全无关**。⇒ 只要进入递归那一刻 `long_enough == false`，递归中它就一直 false（除非中途 `check_destination_seen()`（`:14202`）置 `CTTLASF_STOP_FOUND`）。

**事实 3：`long_enough == false` ⇒ 每一格 PBS 信号都放行（这就是"无界"的具体机制）**
- `HasLongReservePbsSignalOnTrackdir()`（`:13700-13716`）返回 `default_value`（= `!long_enough`）：无 restrict 程序时直接 `return default_value`（`:13715`）；有 `TRPAUF_LONG_RESERVE` 程序时用程序结果覆盖。
- 因此在 `long_enough == false` 时，沿线**任何**非 no-entry 的 PBS 信号都判"要继续长预留" ⇒ `:13880` 再调 `ChooseTrainTrack` ⇒ 深度 = 从起点到第一个 no-entry 信号 / 程序显式否决 / 订单 STOP_FOUND 的格数，**没有任何上限**。

**事实 4（疑点 REG-249-B）**：`long_enough` 只有在 `UsingRealisticBraking() && lookahead != nullptr` 且上述五条早退都不成立时才可能为 false。
- 若玩家存档**没开** realistic braking（`TBM_REALISTIC`），`long_enough` 恒 `true` ⇒ 递归只能由 trace restrict 程序显式给 `TRPRF_LONG_RESERVE` 触发；连续 54 层意味着 54 个 LR 限制信号，不合理。
- ⇒ 反推崩溃现场**开着** realistic braking 且 `lookahead != nullptr`。**待验证**：需玩家给出该存档的设置或复现日志（`TBM_REALISTIC` / `realistic_braking_aspect_limited`），否则这一段仍是推断。

**事实 5（根因假设 G-3，待验证）**：`CmdReverseTrainDirection` 之后 lookahead 被重建/重置，`reservation_end_position` 相对实际可预留范围**明显偏小**（甚至等于 `current_position`），于是 `long_enough` 在整条线路上恒 false；此时"长预留"把整条线路逐格订完，递归深度 = 线路格数 ⇒ 爆栈。
- 支撑：`IsReservationLookAheadLongEnough` 的判定完全依赖旧 lookahead，而 lookahead 由 `FillTrainReservationLookAhead()` 在 `ChooseTrainTrack` **结束之后**才算（`:14178` 等），所以反向瞬间这次调用里的 lookahead 必然是"上一方向的陈旧值"。
- 判据：若复现时打 `reservation_end_position` / `current_position` / `lookahead_end_position` 在进入 `TryLongReserveChooseTrainTrack` 时的值，G-3 成立则三者基本相同（余量 <=24）。

**治本方向（待定，尚未改码）**
- 方向 A（最小、语义正确）：在递归里维护"本次已推进的距离/已跨 LR 信号数"，`long_enough` 判定改为 `IsReservationLookAheadLongEnough(...) || 已推进 >= AdvanceTrainReservationLookaheadEnd 可延伸上限`。因为上游原意就是"一次订到够长"，用陈旧 lookahead 判断"够长"本身就错位。
- 方向 B：realistic braking 下反向瞬间先 `FillTrainReservationLookAhead()` 刷新再跑长预留（治前因，但触及 lookahead 内部状态机，风险高）。
- 方向 C：维持已落地的 128 层上限 + 把 `LR-DEPTH-CAP` 打点改成记录起点/终点格以取证（不动语义）。

### 1.4c 根本问题定性（2026-10-19，第二组取证）—— 循环闭合点已找到

**事实 6：递归闭合点 = `ChooseTrainTrack()` 尾部 `:14253-14257`**
```cpp
if (_settings_game.vehicle.train_braking_model == TBM_REALISTIC) {
    FillTrainReservationLookAhead(consist);
    if (consist->lookahead != nullptr) lookahead_state.order_items_start = (uint)consist->lookahead->items.size();
}
TryLongReserveChooseTrainTrack(consist, res_dest.tile, res_dest.trackdir, (flags & CTTF_FORCE_RES), lookahead_state);
```
这就是符号化栈里 `[11] ChooseTrainTrack`（旧行号 14208→14211）调用 `[10] TryLongReserveChooseTrainTrack` 的位置。**完整环**：
`ChooseTrainTrack`(:14257) → `TryLongReserveChooseTrainTrack`(:13878/13880) → `ChooseTrainTrack`(:13880)。

**事实 7：递归只在"下一格真有 PBS 信号"时发生，每层前进一格**
- `:13702` `if (HasPbsSignalOnTrackdir(tile, trackdir))`、`:13703` no-entry 直接 false；
- `:13704-13713` 仅当信号是 restricted 且程序用了 `TRPAUF_LONG_RESERVE` 才用程序结果；
- `:13715` 否则 `return default_value`，而调用点传 `!long_enough`（`:13878`）与 `:13813 bool long_reserve = !long_enough;`。
- ⇒ `long_enough == false` 时：**沿线每一个 PBS 信号格都判"继续长预留"**，深度 = 连续 PBS 信号格数（或环上无限个）。

**事实 8：`long_enough` 长期为 false 的机制（这才是根本）**
- 主判据 `:13685-13690`：沿 `v->lookahead->items` 找信号，要求 `signal_position + 真实制动距离 <= reservation_end_position` 才 true。
- 只要"预留末端还不够远"（被别的车/信号挡住、或已订到线路尽头、或环上已订满），制动距离永远无法被覆盖 ⇒ **恒 false**。
- `:14254` 每层确实重算 `FillTrainReservationLookAhead()`，但**重算的结果可能完全不变**（预订无法再延伸时）⇒ 递归状态毫无变化 ⇒ 继续递归只是原地绕圈。

**根因结论（G-4，已定性）**：长预留的终止条件寄希望于"订得越多、`reservation_end_position` 越远、`long_enough` 变 true"，但**没有任何"预留不再延伸"的检测**。一旦遇到「信号连续 + 预订末端无法推进」（典型：环线/三角线/站场折返构成的信号闭环，或线路尽头/前方被占），递归沿环或沿信号链无限进行 ⇒ 栈溢出 `C00000FD`。触发点 `CmdReverseTrainDirection`（veh 285）只是"整条路线重新预留"的一次性入口，不是原因。
- 这也解释了为什么 `R3R_LONG_RESERVE_MAX_DEPTH=128` 能止血却治不了本：正常语义下递归本来就该在"无进展"时停，靠层数硬截会误伤长信号链。

**治本方案（T-1 本轮落地）**
- **T-1 环检测**：在 `_r3r_long_reserve_depth` 旁边加 `static TileIndex _r3r_long_reserve_visited[128]`，`R3RLongReserveDepthGuard` 构造时把本层 `tile` 写进 `slot = depth++`；`TryLongReserveChooseTrainTrack()` 入口线性扫已访问栈，命中 ⇒ 打 `LR-CYCLE veh= tile= td= depth=`（同 tick 64 节流）并 `return`。
  - 语义零影响：正常长预留沿单向前进，绝不会回到已访问格；回访只可能是环。
  - 代价：每层最多 128 次 `TileIndex` 比较（可忽略），静态内存 512 B。
- **T-2（本轮落地）无进展检测**：递归前比较 `reservation_end_position`，本层没推进就不递归。覆盖"环 + 长信号链"两种无限动因，因为两者共同特征都是"再走一层，预留末端还是原地"。
- **T-3（未做）** `IsReservationLookAheadLongEnough` 增加"预留已到尽头"早退（要改上游判定函数本身，影响面大于 T-2，留作后备）。

**T-1 落地内容（2026-10-19）**：`src\train_cmd.cpp` 在 `_r3r_long_reserve_depth` 旁新增 `static TileIndex _r3r_long_reserve_visited[R3R_LONG_RESERVE_MAX_DEPTH]`；`R3RLongReserveDepthGuard` 构造函数改为 `Guard(TileIndex tile)`，`slot = _r3r_long_reserve_depth` 时把 tile 写入 visited 再 `++`；新增 `static bool R3RLongReserveCycleDetected(TileIndex)`；`TryLongReserveChooseTrainTrack()` 入口在深度上限门之后加环检测，命中打 `LR-CYCLE veh= tile= td= depth=`（同 tick 64 节流）并 `return`。未改任何 `.h`。

**T-2 落地内容（2026-10-19）**：`ChooseTrainTrack()` 尾部（`train_cmd.cpp:14283` 一带，`if (result_flags & CTTRF_RESERVATION_MADE)` 块内）新增 `bool r3r_extend_long_reserve = true;`；realistic 分支在 `FillTrainReservationLookAhead(consist)` **之前**采样 `prev_res_end = consist->lookahead->reservation_end_position`（无 lookahead 时不判），**之后**若 `reservation_end_position <= prev_res_end` ⇒ `r3r_extend_long_reserve = false` 并打 `LR-NO-PROGRESS veh= tile= td= end= prev= depth=`（同 tick 64 节流）；末尾的 `TryLongReserveChooseTrainTrack(...)` 改为 `if (r3r_extend_long_reserve) { ... }`。
- 语义：`reservation_end_position` 是"本次重建 lookahead 后预留末端的真实位置"。它没有前进，说明这一层既没把预留推远、也不可能让 `IsReservationLookAheadLongEnough()` 变真 ⇒ 再递归一层的收益恒为零，正是 G-4 描述的死循环形态。
- 边界：非 realistic 分支（`!UsingRealisticBraking()` 时 `IsReservationLookAheadLongEnough` 恒 true，递归只能由 restrict 程序显式 `TRPRF_LONG_RESERVE` 触发）**不加此判据**——那里没有 `lookahead` 可比，且链长天然有限；该分支仍由 T-1 环检测 + 128 层上限兜底。
- 保守性：只在 `had_lookahead`（进本层前已有 lookahead）时判，避免"首次创建 lookahead"被误判成无进展。

**构建自证（第 2 次，含 T-1）**
- 内测树 `_tmp_inc_build.cmd`：`[2/3] train_cmd.cpp.obj` → `[3/3] Linking`，`build\R3R_incbuild.done=EXIT_CODE=0`（2026-10-19 14:37:41），`build\openttd.exe` 51 889 152 B @14:37:28。
- 发布树 `_tmp_inc_build_rel.cmd`：`[3/3] Linking`，`build-release\R3R_incbuild.done=EXIT_CODE=0`（14:39:31），`build-release\openttd.exe` 23 140 864 B @14:39:19；仅有既存的 `warning C4189`（探针局部变量在发布版被优化掉）。
- `read_lints` 0 条。

**构建自证（第 3 次，含 T-2，仅内测树）**
- 玩家口径（2026-10-19）：「平时就只编译探针内测版本，发布版本是我说要再编译的」。故本轮**只编 `build`**，`build-release` 保持第 2 次那版（含 T-1、不含 T-2），发布树待玩家明示后再编。
- 内测树 `_tmp_inc_build.cmd`：`[1/5]` 版本生成 → `[2/3] Building ... train_cmd.cpp.obj` → `[3/3] Linking CXX executable openttd.exe`；`build\R3R_incbuild.done=EXIT_CODE=0`（2026-10-19 14:43:54）；`src\train_cmd.cpp` @14:41:53 → `train_cmd.cpp.obj` @14:42:34 → `build\openttd.exe` 51 889 152 B @14:43:50（大小与第 2 次相同属 PE 节对齐，obj 与 exe 时间戳均已刷新）。
- `read_lints` 0 条。

**待验证（U）**
- U-A：现场到底是**环**还是**长信号链** → 复测时看 `LR-CYCLE` / `LR-NO-PROGRESS` 谁出现：`LR-CYCLE` 出现=环（T-1 命中）；只出现 `LR-NO-PROGRESS`=长信号链或预订被挡（T-2 命中）；两者都不出现却仍崩 ⇒ 两条都不是本因，须回到 1.4 反汇编复核。
- U-C（T-2 的风险点，需复测确认）：是否存在"本层确实没推进、但下一层换了 res_dest 起点后又能推进"的合法场景？若正常线路出现 `LR-NO-PROGRESS` 且伴随"该预留却没预留"的停摆，说明判据过严，需退回 T-1 单条 + 改 T-3。
- U-B：玩家是否开 realistic braking（REG-249-B，仍无证据；若没开则 `long_enough` 恒 true，递归只能由 restrict 程序触发，需另找动机）。**2026-10-19 已由代码反证为该开启**：见 1.4d。

### 1.4d 场景画像（2026-10-19，玩家确认「无存档、只有朋友在场」后的纯日志反推）

**前提**：崩溃日志 `C:\Users\冯洁敏\Downloads\crash-20261018T144344Z.log`（709 行）是唯一证据，现场无法重放。下列画像全部从**源码判据 + 日志既有事实**反推，分「硬前置（代码可证）／强推断／猜测」三档，并给替代取证途径。

**硬前置（不满足则这条路径根本不会递归，可用于排除朋友的场景）**

1. **真实制动必须开**。`IsReservationLookAheadLongEnough()` 首行 `:13627 if (!v->UsingRealisticBraking() || v->lookahead == nullptr) return true;`，`UsingRealisticBraking()`=`train.h:384` 的 `tcache.cached_tflags & TCF_RL_BRAKING`（由 `_settings_game.vehicle.train_braking_model == TBM_REALISTIC` 缓存）。`long_enough` 为 true ⇒ `default_value = !long_enough` 为 false ⇒ `HasLongReservePbsSignalOnTrackdir()`（`:13700`）返回 false ⇒ 不递归。另一个入口 `TryLongReserveChooseTrainTrackFromReservationEnd()`（`:13914`）在 `:14042` 直接把 `default_value` 写成 `train_braking_model == TBM_REALISTIC`，非 realistic 同样恒 false。⇒ **既然崩了，朋友的存档必然是 realistic braking 开启**。这条同时把登记项 U-B 从"未知"降级为"已确定"。
2. **递归只发生在"预留成功但仍不够长"的分支**。`:14028-14034` 若 `ExtendTrainReservation()` 返回 `INVALID_TILE`（订不出去）⇒ `MarkTrainAsStuck()` + **直接 `return`，不递归**。所以现场每一层都是**成功订出了一点**，只是没订到"够长"。这修正了最初"预订被挡住导致原地绕圈"的猜测：被挡死的形态根本进不到递归。
3. **每层只前进一格**。`:13907-13910` 递归调用是 `ft.Follow(tile, td)` 后传 `ft.new_tile`，`ChooseTrainTrack()` 重入点前移 **1 格**；`:14042` 的 long_reserve 判据也只看"res_dest 的下一格"有没有 PBS 信号。⇒ 递归深度 ≈ 跨过的**连续 PBS 信号格数**。
4. **那串信号必须是 PBS 且非 no-entry**。`:13702 HasPbsSignalOnTrackdir(tile, trackdir)` 且 `:13703 IsNoEntrySignal(tile, Track)` 为 false；restricted 信号若启用了 `TRPAUF_LONG_RESERVE` 程序则由程序决定（`:13704-13713`）。⇒ 现场是**普通路径信号（含 combo/exit）**，不是被标成 no-entry 的入口信号。

**强推断**

5. **现场是"每 1 格一个 PBS 信号"的实测密度，而不是稀疏信号**。因为一层只跨 1 格，要做到千层递归就必须有千格连续信号；稀疏信号（每 10 格一个）根本攒不出这个深度。JGRPP 里能产生"连续每格都有信号"的现实设施只有三种：带信号模拟的**长隧道/长桥**（`CheckLongReservePbsTunnelBridgeOnTrackdir` 是另一条 long reserve 来源，`:14037`）、**大编组场/枢纽的多股道**（站场里每格布信号是常规做法）、以及**环线**（绕圈可以突破任何有限长度）。
6. **列车在该处速度不低**。`long_enough` 的主判据要求"某个信号的位置 + 该信号限速下的真实制动距离 ≤ 预留末端"（`:13685-13690`，`GetRealisticBrakingDistanceForSpeed`）；预留末端每层只前移 1 格，而制动距离随速度平方增长。⇒ 速度越高、信号限速越高，就越难"够长"，递归越深。低速调车不会触发。
7. **另一条旁证：`realistic_braking_aspect_limited` 分支未命中**。`:13637-13639` 若 `realistic_braking_aspect_limited == TRBALM_ON` 且 `reservation_end_position > current_position + 24` 就直接判"够长"（24 px = 1.5 格）。既然递归发生了，说明现场要么该设置不是 ON，要么**预留末端距车不到 1.5 格**——即"看得见的信号很近、但制动距离远大于它"。
8. **爆栈深度是千级，不是 128 级**。崩溃版本 `r3r-stable-2026-10-16-1-g2fa4018d2-m`（Build Oct 18 01:36:32）**不含**任何 R3R 防护（T-0 的 128 层上限是本轮 10-19 14:29 才编进去的）。模拟线程栈上限 MB 级、每层 `ChooseTrainTrack`+YAPF 帧数百字节 ⇒ 实际递归深度在**千层量级**（推算，未实测，登记为 U-D）。⇒ 朋友那条路径**至少一千格连续 PBS 信号**，或是一个可以无限绕的环。

**猜测（未验证，按可能性排序）**

- **G-5a 环闭合（可能性最高）**：三角线/灯泡线/站场两端连通形成的信号闭环，列车换端后从一个口进去，沿环绕圈，每层前进 1 格、预留也顺利前进（所以 `long_enough` 永远差一点），绕够上千层才爆栈。与 1.4c 的 G-4 一致。判据：内测版跑该区域应打 `LR-CYCLE`。
- **G-5b 超长带信号隧道/桥 + 一端被挡**：跨海大桥/长隧道在 JGRPP 里可逐格模拟信号，一条数百上千格的桥就是一条天然信号链，尽头若是站台占用或红灯，`long_enough` 永假。判据：内测版应打 `LR-NO-PROGRESS`，且崩溃 tile 附近是隧道/桥入口。
- **G-5c 巨型编组场**：几十上百股道、每股逐格信号，配合 `CTTF_FORCE_RES` 强制预留。可以攒深度但很难到千层，可能性最低。

**替代取证途径（不需要原存档，按性价比排序）**

1. **让朋友翻自动存档**（最有效）：OpenTTD 默认每 5/10/15 分钟自动存档，路径 `%USERPROFILE%\Documents\OpenTTD\save\autosave\autosave*.sav`（JGRPP 同目录结构）。崩溃前最近的那一个基本就是同一片区域。拿到后用内测版只做一件事——**把该列车换端一次**，日志里出现 `LR-CYCLE` 还是 `LR-NO-PROGRESS` 就能直接定形态；出现 `LR-DEPTH-CAP` 说明两者都没覆盖。
2. **问朋友 4 个问题**（不需要任何文件）：① 那片是不是三角线/灯泡线/环形场（车能绕回原处）？② 那里信号是不是密到"每格一个"（编组场/枢纽股道）？③ 有没有很长的桥或隧道？④ 换端那一刻，车是不是刚从站台/库出来、或刚挂/解挂完？——四个问题分别对应 G-5a/b/c 与触发时机。
3. **构造最小复现**（朋友没档也能验，10 分钟内）：新建地图 → 设置里开**真实制动** → 造一条 40 格直线股道，**每格放一个 PBS 信号（combo/exit，绝不要 no-entry）** → 两端接站台并各停一列车占住 → 放一列高速机车（200 km/h 级）在中间 → 换端。内测版预期：若出现 `LR-DEPTH-CAP depth=128`，说明天然形态就是"长链"型；把这条直线首尾相接成环再试一次，预期转为 `LR-CYCLE`。这两个结果分别等价于 G-5b 与 G-5a。

**待验证（U）**

- U-D：真实递归深度（千层级)属推算，须以最小复现场的 `LR-DEPTH-CAP depth=` 实测值校准；若实测只有 130 层就崩，说明模拟线程栈比预估小得多，则 G-5a 的"环"权重更高。

### 1.4e 玩家复跑取证（2026-10-19 15:27，无原存档、自建场景）

**输入**：`build\R3R_debug.log` 188 401 B / LastWriteTime 2026-10-19 15:27:40；`Downloads` 下仍只有旧的 `crash-20261018T144344Z.log`（13:25:51），**本次未崩溃**。

**读数（全部带行号）**

- `LR-CYCLE|LR-NO-PROGRESS|LR-DEPTH-CAP` 三串 **0 命中** ⇒ 三层防护一条都没触发。
- 有两次换端：`:649 REVERSEDIR veh=3 tile=323,164 dir=3 order=1 spd=0 nv=15 rev=1 stuck=0 db=0`、`:2461 REVERSEDIR veh=3 tile=323,178 ... rev=1`，另有 `:2226 REVERSEDIR veh=3 tile=323,164 ... rev=0 stuck=1 db=1`。**坐标 323,164 / 323,178 ≠ 崩溃现场 2965,2615**，地图尺度也不同（后者是大图）。
- `TRP veh=3`：`:179/:203/:235 mf=323,116 => ok=0 res=0` → `:271 look=1 => ok=1 res=1`；`:737 mf=323,160 fto=1 => ok=0 res=0`，随后 `:779/:931/:1083/.../:2151` 共 12 条 `mf=323,160 => ok=0 res=0`（原地订不出去）；`:2259 mf=323,164 look=1 fto=1 => ok=1 res=1`；`:2551/:2608 mf=323,175 => ok=0 res=0`。
- 日志尾部一整串 `UNRES-RAIL tile=323,1xx track=1 phase=extend-backout actor=3`，从 168 一路退到 124 以下 ⇒ 这条线**确实在逐格做预留延伸/回退**，长度 ≥ 45 格，属于"长链"形态，但**没有形成递归**。

**结论**

1. **U-B 结案**：`C:\Users\冯洁敏\Documents\OpenTTD\openttd.cfg:240 train_braking_model = 1`（`TBM_REALISTIC`）、`:241 realistic_braking_aspect_limited = 1`（`TRBALM_ON`）。⇒ 真实制动是开着的，代码上"必须开才能递归"的硬前置成立，之前悬着的 U-B 关闭。（注：`_settings_game` 随存档走，cfg 只是默认值，但本次玩家自建场景同样会继承 cfg ⇒ 对本次运行有效。）
2. **`aspect_limited=1` 让崩溃画像再收窄一层**：`:13637-13639` 的条件是 `realistic_braking_aspect_limited == TRBALM_ON` 且 `reservation_end_position > current_position + 24`（24 px = 1.5 格）就**直接判"够长"、不递归**。既然崩溃真的发生了，崩溃现场的 `reservation_end_position` 必然**紧贴车头（≤1.5 格）**。⇒ 现场不是"长预留一路顺畅地递归很远"，而是**预留被死死压在车头前方不到一格半、同时前方又是连续 LR 信号**的极端形态。这与 1.4d 的"每层都预留成功但永远不够长"完全自洽，也与 T-2（无进展检测）的判据对得上。
3. **本次场景没触发的原因（判定）**：`mf=323,160` 连续 12 次 `ok=0 res=0` 说明该处是"完全订不出去"，而 `:14028-14034` 规定 `ExtendTrainReservation()` 返 `INVALID_TILE` 时直接 `return` 不递归。另两次 `ok=1 res=1`（323,164 / 323,116）预留末端大概率已推过 1.5 格 ⇒ 被 `:13637` 早退成"够长"。⇒ 这是**"两头的边界都不落在触发窗口里"的巧合**，不能作为"BUG 不存在"的证据，也不能作为 T-1/T-2 有效的证据。
4. **`UNRES-RAIL ... phase=extend-backout` 是本轮新观察点**（疑点 REG-249-C）：长链上反复"延伸再回退"说明长预留确实在被构建，但每 tick 重建、末端没锁住；若现场是这种"每 tick 全链回退重订"，正是崩溃前的形态。需确认 `R3RExtendReservation` 的 backout 是否与本条递归有关（待下轮取证）。

**待验证（U）**

- U-E：玩家自建场景的**信号密度**（是否每格一个 PBS LR 信号）与是否有**闭环**——本次日志无信号类探针，判不了。这是"没触发"最可能的原因，必须先补数据再谈复跑价值。
- U-F：崩溃现场 `reservation_end_position` 是否真的恒为 `current+≤24`（由结论 2 反推，尚无正面读数）。

### 1.4f 新增 `LR-TRACE` 只读探针（2026-10-19，按 1.4e 的"缺数据"结论补）

**为什么加**：1.4e 判不了"玩家自建线路上长预留递归到底跑了几层"，因为 T-1/T-2 的日志只在**异常**时打（环 / 不推进 / 撞上限），正常浅层递归完全静默。于是"没日志"既可能是"没递归"也可能是"递归了 3 层就正常结束"，两者无法区分，复跑等于盲跑。

**实现（仅 `src\train_cmd.cpp`，零行为变更）**：在 `_r3r_long_reserve_visited[]` 之后新增 `_r3r_lr_trace_max_depth/_tile/_td/_train` 与 `_r3r_lr_trace_last_tick`；`TryLongReserveChooseTrainTrack()` 在构造 `R3RLongReserveDepthGuard` **之前**记录本层深度与格（只写静态变量，不 return、不改变预留）；`~R3RLongReserveDepthGuard()` 在深度回到 0（整条递归结束）且 `max_depth >= 1` 时打一行、随后清零：

```
LR-TRACE veh=%d depth=%u tile=%d,%d td=%d res_end=%d cur=%d
```

`res_end`/`cur` 取自 `v->lookahead`（`reservation_end_position` / `current_position`，单位 px），无 lookahead 时打 -1。同 tick 最多一行。

**判读表（下一轮复跑直接照这个读）**

| 现象 | 结论 |
|---|---|
| 无 `LR-TRACE` | 该线路上**根本没有 LR 信号链**（`TryLongReserveChooseTrainTrack` 一次都没进）⇒ 场景不具备触发条件，与 BUG 无关 |
| `depth=1~5` | 信号链很短，正常结束 ⇒ 场景太短，需要更密的信号或更长的链 |
| `depth` 稳定在 20~60 且 `res_end-cur` 很小 | **正是崩溃形态的前身**（预留末端紧贴车头、靠递归一格格摸） |
| `depth` 单调增长到 100+ 并出现 `LR-DEPTH-CAP` | 已撞防护，形态确认，可据此反推现场长度 |

**注意**：`depth` 只在"整条递归收工时"报一次最大值，所以一次换端操作应产生**恰好一行**（同 tick 内多次入口合并）。若同一 tick 出现多行，说明存在多条独立递归入口，需另记。

### 1.4g 玩家第二次复跑（2026-10-19 15:27 日志）+ 场景口述 —— 未触发的真因锁定为「换端时速度为 0」

**玩家口述场景（原话摘要）**：南北向长直轨，超过 40 格（估计 60 格）；每一格都有一个朝南、可向南发出预留、能双向通过的 PBS 路径信号；两端各一个车站、各停一列孤立机车；中间是一列最高时速 200 km/h 的动车组（末端有 cab）；让动车组出来、加速到 200 km/h 后**让它停车**（当时南端拦阻机车还没到位）→ 之后执行换端。

**决定性读数（`build\R3R_debug.log`，本轮 exe 已含 T-0/T-1/T-2 三层防护）**

```
:649  REVERSEDIR veh=3 tile=323,164 dir=3 order=1 spd=0 nv=15 rev=1 stuck=0 db=0
:2226 REVERSEDIR veh=3 tile=323,164 dir=3 order=1 spd=0 nv=15 rev=0 stuck=1 db=1
:2461 REVERSEDIR veh=3 tile=323,178 dir=3 order=1 spd=0 nv=15 rev=1 stuck=0 db=0
```

三次换端 **`spd` 全为 0**，与玩家"先停车再换端"的口述逐字吻合。

**判定**：`IsReservationLookAheadLongEnough()`（`:13627` 起）在 `cur_speed == 0` 时真实制动距离为 0（`GetRealisticBrakingDistanceForSpeed`），且 `realistic_braking_aspect_limited == TRBALM_ON` 的 `:13637` 分支只要末端超过 `current+24` 即判"够长" ⇒ **静止换端使 `long_enough` 首次即为 true，`default_value = !long_enough` 为 false，`HasLongReservePbsSignalOnTrackdir()` 返假 ⇒ 一次递归都不发生**。这是全日志 0 条 `LR-*`、0 条 `LR-TRACE` 的唯一原因，与信号密度、轨道长度、有无环**无关**。

**对 1.4d 的三处修正**

1. 强推断 5「必须千格连续信号」**下调**：玩家的 60 格直线 + 每格一信号已满足密度条件；所需深度由速度与制动距离决定，60 格量级就可能够，不必千格。
2. 猜测 G-5a（环）/G-5b（长桥隧）/G-5c（巨型编组场）**一律不作为证据**：玩家明确说是**直线**，既非环也非桥隧 ⇒ 崩溃现场大概率同族（长直轨 + 每格信号 + 两端车站），"环"的权重显著下调。
3. 1.4d 的硬前置 ②③④ 与强推断 ⑥（速度不低）全部成立，其中 **⑥ 是本次唯一未被满足的一条**，其余条件玩家场景已逐条满足。

**下一步（唯一正确的复现动作）**：**在 200 km/h 行驶中直接换端**（点换向按钮 / 带 reverse 的订单），绝不要先停车。新编的 `LR-TRACE` 探针会打出 `depth=` 与 `res_end-cur`，据此判定 60 格直线能否达到崩溃级深度。

### 1.4h 构建自证（2026-10-19 15:45，`LR-TRACE` 探针版）

- 复用既有 `_tmp_inc_build.cmd`（未新建任何 .cmd）；日志 `[2/3] Building CXX object ... train_cmd.cpp.obj` → `[3/3] Linking CXX executable openttd.exe`。
- `build\R3R_incbuild.done` = `EXIT_CODE=0`。
- `build\openttd.exe` @2026-10-19 15:45:07（51 889 152 B），晚于 `src\train_cmd.cpp` 本轮改动。
- `findstr /C:"LR-TRACE veh=" build\openttd.exe` 命中（LRTRACE_OK）⇒ 探针确实编入。
- 发布树 `build-release` **未编**（按 2026-10-19 口径只编内测探针树）。

### 1.4i 代码重读：递归的真实驱动条件 + 对 §1.4g 结论的修正（2026-10-19）

**重读范围**：`:13625-13693`（`IsReservationLookAheadLongEnough`）、`:13700-13719`（`HasLongReservePbsSignalOnTrackdir`）、`:13838-13954`（`TryLongReserveChooseTrainTrack`）、`:14000-14056`（`ChooseTrainTrack` 预留失败支）、`:14317-14355`（`ChooseTrainTrack` 尾部再入点 + T-2 门禁）。

**递归链的精确形态（据此可判场景参数）**

1. 每层的唯一再入点在 `:14352-14353`，前提是 `result_flags & CTTRF_RESERVATION_MADE`（`:14317`）**且** T-2 判定 `reservation_end_position` 相对上一层**有增长**（`:14339` `<= prev_res_end` 即停，打 `LR-NO-PROGRESS`）。⇒ **每层必须真的把预留向前推进一格**，否则递归当场断掉。
2. `TryLongReserveChooseTrainTrack` 继续往下探的条件是 `:13950`：`Follow` 成功 **且** 下一格 `HasLongReservePbsSignalOnTrackdir(..., !long_enough, ...)` 为真。普通 PBS 信号（非 no-entry、无 TRP 覆盖）直接返回 `default_value = !long_enough`。⇒ **下一格有普通 PBS 信号 + 当前判"不够长" ⇒ 继续递归**，与"前方有没有车"无关。
3. 因此递归深度 ≈ **从换端点起连续"预留能推进 + 下一格有 PBS 信号"的格数**。

**结论 A：挡路机车不但不需要，而且有害（这是本轮最重要的实操结论）**

- 若下一格被另一列车占住：`TryReserveSafeTrack` 失败 ⇒ `FreeTrainTrackReservation` + `MarkTrainAsStuck` + `result_flags &= ~CTTRF_RESERVATION_MADE`（`:14307-14313`）⇒ 尾部再入点 `:14352` 不进 ⇒ **整条递归在阻挡点立即终止**。
- 玩家的场景是"两端车站各停一列孤立机车"，等于给递归的**两头各加了一个天花板**：60 格扣掉两端站台、两台机车、以及列车本体长度，实际可递归格数很可能不到 40 ⇒ 达不到崩溃现场的 54 层。这与"跑完没崩、也没有 `LR-DEPTH-CAP`"完全吻合。
- ⇒ 正确场景 = **把两端机车移走（或删掉），长轨两端完全空旷**。

**结论 B：环状不是必要条件，且有一个可用的判别特征**

- 环的作用只是让"连续 PBS 信号链"在小地图上无限长；直轨只要连续信号格数 > 崩溃深度（54 层）就等效，建议 **150 格以上**。
- 但 T-1（`R3RLongReserveCycleDetected`，`:13853`）会在绕回本轮回已走过的格时停止并打 `LR-CYCLE` ⇒ **环状场景在探针版会命中 `LR-CYCLE`，直轨不会**。这正好可以反向判定玩家原来的场景是不是环。

**修正 §1.4g 的错误结论（重要）**

§1.4g 写的是"换端时 `spd=0` ⇒ 制动距离 0 ⇒ `long_enough` 首次即为 true ⇒ 递归 0 层"。**该推断不成立**：

- `:13627` 只要求 `UsingRealisticBraking()` 且 `lookahead != nullptr`，**没有速度门**；
- `:13629` 的早退只看 `IsAnyLoadingType()` / `OT_WAITING`（现场 `REVERSEDIR ... order=1` 是 `OT_GOTO_STATION`，不是等待订单）；
- `:13637` 与 `:13669` 两个分支只看**位置**（`reservation_end_position` / `item.start` 相对 `current_position`），不看速度；
- 唯一与速度有关的是 `:13688` 的 `GetRealisticBrakingDistanceForSpeed`，但它只在 `found_signal`（`item.start > current_position + 24`）成立时才参与；而"每格一个信号"时第一个信号恰在 16px 处 ≤ 24px ⇒ `found_signal` 恒为 false ⇒ 走 `:13692 return false`，与速度无关。

⇒ **停车换端同样会递归**。这次没崩的真正原因按结论 A 推断是"两端机车掐断了递归"，而非速度。该推断标记为**猜测根因（待验证）**，验证物 = 去掉机车后 `LR-TRACE depth=` 是否跃升。

**§1.4h 探针可读性补充**：`LR-TRACE` 的 `depth` 就是本轮递归最大层数；若同一次换端出现 `depth=0` 不打印（`max_depth>=1` 才打），所以**完全没有 `LR-TRACE`** 仍表示"该线路一次 `TryLongReserveChooseTrainTrack` 都没进"（信号不是 LR PBS，或 `long_enough` 一开始就为真）。

### 1.4j 第三次复跑（2026-10-19 17:18，巨大环 + 每格一信号 + 达速调头）—— 递归深度只有 2，未复现

**玩家口述**：造了一个巨大的环，环上除间距为 1 的信号灯外**没有任何别的东西**（无车站、无路点），列车放出达速后调头。

**日志**（`build\R3R_debug.log`，71 444 B，17:18:43）

```
:414 LR-TRACE veh=0 depth=2 tile=351,558 td=1 res_end=1282 cur=1066
:417 LR-TRACE veh=0 depth=1 tile=351,558 td=1 res_end=1282 cur=1076
:418 LR-TRACE veh=0 depth=2 tile=351,559 td=1 res_end=1298 cur=1082
:421 LR-TRACE veh=0 depth=1 tile=351,559 td=1 res_end=1298 cur=1092
...  （逐格 351,560 / 561 / … / 572，res_end 每次 +16=1 格）
:470 LR-TRACE veh=0 depth=2 tile=351,572 td=1 res_end=1506 cur=1290
:474 REVERSEDIR veh=0 tile=351,559 dir=3 order=0 spd=0 nv=15 rev=1 stuck=0 db=0
```

**读数结论（三条）**

1. `LR-TRACE` 存在 ⇒ 递归链**确实被执行**，环 + 每格信号的形态是对的（这次不存在"信号不是 LR 型"的问题）。
2. **`depth` 最大值只有 2**，而崩溃现场是 **54 层交替帧**。`depth=1` / `depth=2` 交替出现，说明每一轮递归**在第一层递归之内就收敛**，完全够不到爆栈所需的量级。
3. `res_end - cur` 恒为 **216 px（13.5 格）**，`res_end` 每层 +16 px（正好一格）⇒ 预留确实在一格一格往前推，但**推一格就立刻被判定为"够长"**。
4. `REVERSEDIR ... spd=0 ... order=0`：调头时速度又是 0，且**订单是 `OT_NOTHING`（order=0）**——列车没有订单在环上滑行/停车。

**判定：本次未复现，卡点是 `IsReservationLookAheadLongEnough()` 在第二层就返回 true**

`long_enough == true` ⇒ `:13950` 的 `default_value = !long_enough` 为 false ⇒ 普通 PBS 信号直接返回 false ⇒ 递归停。能让它早早返回 true 的候选分支（按可能性排序）：

- **`:13637`**（`lookahead_end_position <= reservation_end_position && aspect_limited == TRBALM_ON && res_end > cur+24`）——**最可能**。每格一个信号时 `lookahead_end` 被"信号只能显示下一格"压得很近，而 `res_end` 已超出 `cur+216px`，条件极易成立。这解释了"每格信号反而让递归变浅"的直觉矛盾。
- `:13631`（`CTTLASF_STOP_FOUND` 或 depot end）——环上若有任何一处红灯/停车点，flag 一置就全局返回 true。
- `:13629`（`IsAnyLoadingType/OT_WAITING`）——`order=0` 是 `OT_NOTHING`，**排除**。
- `:13685-13689`（`found_signal` + 制动距离）——只在 `aspect_limited != ON` 时才是主判据。

**⇒ 猜测根因（待验证）**：崩溃现场的 `realistic_braking_aspect_limited` 很可能是 **OFF**，或者信号间距**不是 1 格**（信号越密，`lookahead_end` 越近，越容易"够长"；信号越稀，`lookahead_end` 远超 `res_end`，递归才会一路走下去）。本轮先查配置实际值，再决定是加"返回原因"探针还是让玩家改设置。

### 1.4k 配置核对 + `reason` 探针落地（2026-10-19 17:25）

**（1）1.4j 的猜测「`aspect_limited` 可能是 OFF」已证伪**

读 `Documents\OpenTTD\openttd.cfg`：

```
train_braking_model             = 1     (真实制动)
realistic_braking_aspect_limited = 1    (TRBALM_ON，受限显示开启)
track_edit_ignores_realistic_braking = false
train_acc_braking_percent       = 100
```

⇒ `:13643-13647`（原 1.4e/1.4j 里记作 `:13637`，本轮加了 reason 探针后行号后移）这个分支**条件方向是对的、也确实在起作用**：`realistic_braking_aspect_limited == TRBALM_ON` 成立，只要 `lookahead_end_position <= reservation_end_position` 且 `res_end > cur+24`，`long_enough` 立刻 true。这与现场 `res_end - cur = 216 px` 完全吻合（远大于 24），所以**唯一还在起作用的前半句只能是 `lookahead_end <= res_end`**——即"每格一个信号把可视间距压到预留终点之内"。

**（2）疑点登记 REG-249-C（有疑点）**

`LR-TRACE` 里 `depth=2` 与 `depth=1` **逐格交替**出现（`:414/:417`、`:418/:421` …），而不是"一次深递归 + 一次浅递归"。按 `R3RLongReserveDepthGuard` 的语义（`/` 处 `_r3r_lr_trace_max_depth` 在 depth 归零时清零），这说明**同一 tick 内 `TryLongReserveChooseTrainTrack` 被反复进入多次、每次只走 1~2 层**，而不是一次调用走多层。若真如此，崩溃现场的 54 层必须是"单次调用内"的 54 层，两者形态不同 ⇒ **复现场景还缺一个让单次调用连续递进的要素**（现场景每层推进一格后，下一层就因 lookahead 受限而收敛）。

**（3）探针增强（只读，零行为变更；仅 `src\train_cmd.cpp`）**

`IsReservationLookAheadLongEnough()` 内新增两个文件内静态量，并在每个 `return true` / 最终 `return false` 前打上原因码：

```cpp
static uint _r3r_le_reason = 0;          // 1..9 = 命中的提前返回分支，0 = 走到最后 return false
static int  _r3r_le_lookahead_end = -1;  // 仅 reason=5 时填 lookahead_end_position
```

| reason | 分支（当前行号） | 含义 |
|---|---|---|
| 1 | `:13633` | 不是真实制动 / 无 lookahead |
| 2 | `:13635` | 装货中或 `OT_WAITING` |
| 3 | `:13637` | `CTTLASF_STOP_FOUND` 或 DepotEnd |
| 4 | `:13640` | `reverse_distance>=1` 且预留已越过反向点 |
| **5** | **`:13645`** | **`lookahead_end <= res_end && TRBALM_ON && res_end > cur+24`（本轮重点怀疑对象）** |
| 6 | `:13662` | `TRLIT_REVERSE` 且预留够长 |
| 7 | `:13665` | `TRLIT_STATION` + `CTTLASF_REVERSE_FOUND` |
| 8 | `:13691` | `AdvanceTrainReservationLookaheadEnd(cur+24) <= res_end` |
| 9 | `:13700` | `found_signal` + 该信号处制动距离够长 |
| 0 | `:13703` | 以上全不满足 ⇒ 返回 false（**只有 false 才会继续递归**） |

`LR-TRACE` 行尾追加 `le=` 与 `reason=`：

```
LR-TRACE veh=%d depth=%u tile=%d,%d td=%d res_end=%d cur=%d le=%d reason=%u
```

于是下一次复跑可直接读出"到底哪一条把递归掐断"，不用再猜。

**（4）构建自证（内测探针版）**

- 复用既有 `_tmp_inc_build.cmd`（**未新建任何 .cmd**）；`build\R3R_incbuild.guard.log` = incremental is safe。
- 日志：`[2/3] Building CXX object ... train_cmd.cpp.obj` → `[3/3] Linking CXX executable openttd.exe`；`build\R3R_incbuild.done` = `EXIT_CODE=0`。
- 时间戳：`src\train_cmd.cpp` 17:22:04 → `train_cmd.cpp.obj` 17:23:07 → `build\openttd.exe` 17:24:39（51 891 712 B）。
- `read_lints` 0 条。

**（5）待复测动作（下一步唯一要做的事）**

用新 `build\openttd.exe` 重跑**同一个环**（每格一信号、达速后调头），取回：

```
LR-TRACE ... res_end=... cur=... le=... reason=...
```

判读规则：

- `reason=5` ⇒ 确认是"信号密 ⇒ lookahead 受限"；下一步改为**稀疏信号**（间距 ≥ 10 格）再跑，让 `lookahead_end` 远超 `res_end`，看 depth 是否随之增大。
- `reason=3` ⇒ 环上某处被判定"前方停车/车库尽头"，需去掉该类结构（例如环内的双向信号被读成 stop）。
- `reason=8` ⇒ 是 `AdvanceTrainReservationLookaheadEnd(cur+24)` 那一支；说明 lookahead 已能前瞻 1.5 格即满足，需看 `res_end` 的增长速率。
- `reason=9` ⇒ 找到了 `found_signal` 且制动距离够长（说明 `aspect_limited` 实际未按 ON 走，需复核 `_settings_game` 的取值时机）。
- `reason=0` 但 `depth` 仍只有 1~2 ⇒ `long_enough` 是 false，掐断原因在别处（`default_value`、`_long_reserve_disabled`、或下一格不是 LR 信号），要转查 `HasLongReservePbsSignalOnTrackdir`。

### 1.4l 玩家猜测「环上有一处岔口接车库」——与代码机制吻合（2026-10-19 17:45）

**猜测**：复现场景的环不是完全闭合的，中间有一处可以连到车库（depot）。玩家怀疑正是这处连接打断了（阻止了）"无限递归"。

**代码核对（三条，全部逐字命中）**

1. `src/pbs.cpp:1469-1473`（前瞻推进循环 `AdvanceReservationLookAhead` 内）：
   ```cpp
   /* Depot tile? Can't continue. */
   if (IsRailDepotTile(tile)) {
       if (lookahead != nullptr) lookahead->flags.Set(TrainReservationLookAheadFlag::DepotEnd);
       break;
   }
   ```
   ⇒ **库格是前瞻的硬墙**：前瞻一走到库格就停在那里，并置 `DepotEnd`。

2. `src/pbs.cpp:1475`：`if (HasBlockSignalOnTrackdir(tile, trackdir)) break;`
   ⇒ 岔口处若挂了**非 PBS 信号**（普通 block signal），前瞻同样当场终止。两条路都堵。

3. `src/train_cmd.cpp:13637`：
   ```cpp
   if (HasBit(lookahead_state.flags, CTTLASF_STOP_FOUND) || v->lookahead->flags.Test(TrainReservationLookAheadFlag::DepotEnd)) { _r3r_le_reason = 3; return true; }
   ```
   ⇒ 命中即判「预留已够长」并 `return true`，**递归立即停止**（`return false` 才是继续递归的唯一出口）。

**反向推论（这条才是关键）**

如果环是**完美闭合**的，前瞻永远走不到库格，它会一直绕下去，直到撞上 `src/pbs.cpp:1467` 的 loop 检测：

```cpp
if (tile == start_tile && trackdir == start_trackdir) break;
```

⇒ `lookahead_end` 会停在**绕满一整圈**的位置，远大于 `res_end`（现场只 13.5 格）⇒ reason 5/8/9 全部不成立 ⇒ `return false` ⇒ **递归一路走下去**，这才可能堆到崩溃现场的 54 层。

⇒ 结论：**一处岔口就把「无限闭环前瞻」降级成「有限前瞻」**，完全能解释本轮 `depth=1~2`。要复现 54 层，场景必须是**无岔口的闭合环**（无库、无站、无路点、无侧线）。

**判据（等新日志）**

- `reason=3` ⇒ 你的怀疑成立，掐断点就是库格/岔口信号。
- `reason=5` ⇒ 岔口不是主因，主因仍是"每格一信号把前瞻压短"。
- 改成闭合环后出现 `LR-CYCLE`（护栏拦截）⇒ **形态已复现**（无限递归成立、护栏生效），这本身即高价值证据。

**状态**：REG-249-D 已登记，未验证（等日志）。

### 1.4m 拆掉车库后「轨道上调向」→ 游戏卡死无崩溃报告（2026-10-19 17:37）

**玩家动作**：按 §1.4l 的建议拆掉通往车库的岔口，然后让列车**在轨道上调向**（reverse），不再用库内调向。

**现象**：游戏卡死很久，迟迟没有写崩溃报告。

**现场读数（本次取证，全部为只读命令）**

| 项 | 读数 | 判定 |
|---|---|---|
| 进程 | `openttd.exe` PID 21272 存活，Mem 116 172 K | 没崩、没退出 |
| Responding | `False` | 窗口无响应 |
| CPU | 210.8 s（17:32:36 启动） | **在烧 CPU**，不是死锁等待 |
| 日志体积 | 29 697 B，LastWriteTime `17:37:24` 之后不再增长 | **纯 CPU 空转、零日志**，排除 KI-217 式刷屏 |

**日志末尾锚点（`build\R3R_debug.log`，逐字）**

```
LR-TRACE veh=0 depth=1 tile=246,371 td=1 res_end=1074 cur=868 le=1075 reason=9
LR-TRACE veh=0 depth=2 tile=246,372 td=1 res_end=1090 cur=874 le=1091 reason=9
SPD-DROP veh=0 tile=246,360 prev=1 order=0 real=0 stuck=0 fp=0 prog=79 wait=0 parked=0 stop=0 track=02 dtile=4294967295
REVERSEDIR veh=0 tile=246,360 dir=3 order=0 spd=0 nv=15 rev=1 stuck=0 db=0
  RF idx=0..14  x=3944 y=5775..5721  dir=3 trk=0x2        (15 节，竖向排列)
RESV-GC ×17  tile=246,357..246,373 track=0x1 why=free-path
RESV-LEDGER-FORGET veh=0 why=free-path mask=0x1 released=17 kept=0
REVERSEDONE db=1 mvfront=14
  RA idx=0..14  与 RF 逐行相同
TPR-WALK veh=0 mf=246,357 origin=246,357 td=9 okay=0 exit=3 new=246,356 reach=2 spd=0 one=0 look=0
<— 到此为零输出，之后永久静默
```

**判定**

1. 调向本身**成功**：`REVERSEDONE db=1 mvfront=14`，15 节 RA dump 与 RF 逐行一致（位置未动、方向统一），`RESV-GC ... released=17` 说明旧预留被正确回收 —— 换端路径没有崩。
2. 卡死点在**调向后第一次预留**：`TPR-WALK ... okay=0`（预留失败）之后，本应出现的 `CRT ...`（ChooseTrainTrack 结果）**全无** ⇒ 卡在 `TryPathReserve` 内部的 walk 之后、`ChooseTrainTrack` 返回之前，而该区段**没有任何日志**。
3. 与 REG-249-D 的关系：拆掉车库 = 环闭合 ⇒ 前瞻能一路绕到 `src/pbs.cpp:1467` 的 `tile==start_tile && trackdir==start_trackdir` 才停 ⇒ `lookahead_end` 变成整圈 ⇒ `reason 5/8/9` 条件不再成立 ⇒ `IsReservationLookAheadLongEnough` 返回 **false** ⇒ 递归不再被"够长"截断。**形态上正是崩溃现场所需的那个条件**。所以这次卡死很可能就是"无限递归"真的出现了，只不过表现为 CPU 死循环，而不是立刻栈溢出（`LR-DEPTH-CAP=128` 截断了**深度**，但没截断**总工作量**：每层仍会尝试环上多个分支）。

**疑点 REG-249-E（不自洽处，必须记下）**

卡死前最后两条 `LR-TRACE` 仍是 `reason=9`（"找到信号 + 制动距离够长" ⇒ 本应 `return true` 截断递归），且调向瞬间 `le=1075` 只比 `res_end=1074` 大 **1**，`le=1091` vs `res_end=1090` 也大 **1** —— 也就是说**掉头瞬间的 lookahead 极短（只 1 px 富余），根本不是"绕了整圈"**。⇒ 「环闭合导致前瞻绕整圈」与「实测 lookahead 只多 1px」互相矛盾，两者不能同时为真。可能解释：①调向瞬间 lookahead 还没重建（沿用旧值）；②闭环 loop-break 触发点其实很近；③`le` 的语义不是"前瞻尽头"。**未判定，需代码核对 `le` 取值点。**

**未确认**

- U-1 是否真是递归/寻路爆炸（缺栈证据；无栈则无法区分"递归爆炸"与"YAPF 分支爆炸"）。
- U-2 零输出是否只是日志缓冲未 flush（但 17:37:24 后连一行都没有，且此前每 tick 都有输出，倾向不是缓冲）。
- U-3 `okay=0` 之后为何不 return 而是陷入长计算（`TPR-WALK` 只是打印，之后继续走 ChooseTrainTrack）。

**下一步（待办）**

1. 进程仍活着 ⇒ 可考虑抓 minidump 取栈再杀（最快定性）；否则直接杀，改用探针定位。
2. 探针方案：在 `ChooseTrainTrack` 递归入口/出口加**成对心跳**（带 tick 与累计次数），或直接加**单 tick 总调用预算**，超限打 `LR-BUDGET-HIT` 并返回 false —— 既是定位手段，也是用户要的"别卡死"护栏。
3. 复测时同时保留"是否拆车库"这一变量，明确区分它与"每格信号"各自的贡献。

### 1.4n 「同根因、双出口」及其推论（2026-10-19 18:0x）

**推论 P-1（本次卡死与 10-18 崩溃共享上游、分裂下游）**

- 共同上游：`IsReservationLookAheadLongEnough()` 是否被截断。库格（`pbs.cpp:1469` DepotEnd）/ 非 PBS 信号（`pbs.cpp:1475`）/ 环检测（`pbs.cpp:1467`）三者任一命中 ⇒ `long_enough=true`（reason 3/5/9）⇒ 递归 `return`。三者全不命中 ⇒ `return false` ⇒ 递归继续。
- 分裂下游：
  - **崩溃现场**（10-18）：递归一路到底，`_r3r_long_reserve_depth` 到 **54**，物理栈爆 ⇒ C00000FD，进程死，有 crash log 有栈。= **空间**耗尽，一次到底。
  - **本次卡死**（10-19）：`Responding=False`、CPU 210 s、日志停在 `TPR-WALK`、**一个 tick 没跑完**、无 crash。= **时间/工作量**耗尽，进程活，无栈。

**推论 P-2（护栏漏洞：深度有界 ≠ 工作量有界）**

`R3R_LONG_RESERVE_MAX_DEPTH = 128`（`train_cmd.cpp:13779`）只封**纵向**。每层 `ChooseTrainTrack` 内部还有多信号重试 / YAPF 寻路（横向宽度不受限）。故「深度已被 128 截断」**不能**推出「总耗时可控」：128 × 每层横向分支即可把单 tick 拖到分钟级。⇒ 护栏必须按**资源预算**（节点数 / 时间）设计，而不是按**调用形态**（深度）设计。

**新疑点 REG-249-F（本轮实验与崩溃现场不能互推）**

- 事实 A：10-18 崩溃现场有车库（订单 GOTO_DEPOT 42,28），却出现 54 层单次连续递归。
- 事实 B：按 §1.4l，库格是前瞻硬墙 ⇒ 前瞻若经过库格应得 `reason=3`、`return true`、递归不深。
- 事实 C：本轮拆掉车库岔口后不但没变好，反而卡死（即拆掉打断点 = 让前瞻变长 = 更糟）。
- 三者并置 ⇒ **崩溃现场那条前瞻路径上应当没有库格**（车库只在支线/订单目的地，不在前瞻走向上），否则推不出 54 层。即：10-18 现场与本轮现场的**拓扑不同**，本轮的"拆库"实验**不能**直接迁移去解释 10-18 的 54 层。
- 待验证：KI-378 现场的线路拓扑（前瞻方向上是否有库格/普通信号），需要现场截图或 `LR-TRACE` 的 `tile=` 序列反推。

**推论 P-3（失败形态的取证价值排序）**

崩 > 卡。崩溃留 crash log + 栈（可直接定位函数）；卡死零栈零日志（`LR-TRACE` 退栈才打，卡住时恰好不打）⇒ 卡死必须靠**预算探针**（超限时主动打 `LR-BUDGET`）才能定位。这也是下一步必须先加预算护栏的原因，而非可选项。

### 1.4o 单次预留总预算护栏（已实现 + 已编译，2026-10-19 18:05）

**动机**：§1.4n 推论 P-2 —— `R3R_LONG_RESERVE_MAX_DEPTH=128` 只封纵向深度，未封每层横向分支（每层 `ChooseTrainTrack` 跑一次完整 YAPF + 多信号重试），故"深度已截断"推不出"单 tick 可返回"。§1.4m 的卡死即为此（CPU 210 s、单 tick 未完成、零日志）。

**改动（仅 `src\train_cmd.cpp`，未碰任何 `src\*.h`）**

1. `:13781-13797` 新增 `static uint _r3r_long_reserve_nodes = 0;` + `static const uint R3R_LONG_RESERVE_MAX_NODES = 1024;`（含 18 行注释说明"深度有界 ≠ 工作量有界"）。
2. `TryLongReserveChooseTrainTrack()` 入口（`if (_long_reserve_disabled) return;` 之后、原来的深度检查之前）插入：
   - `if (_r3r_long_reserve_depth == 0) _r3r_long_reserve_nodes = 0;`（顶层入口兜底清零）
   - `if (_r3r_long_reserve_nodes >= R3R_LONG_RESERVE_MAX_NODES)` ⇒ 64 tick 节流打 `LR-BUDGET veh= tile= td= depth= nodes= limit=` 后 `return`（语义与深度上限一致：只跳过"越过下一个 LR 信号继续订"，已订部分保留）
   - `++_r3r_long_reserve_nodes;`
3. `R3RLongReserveDepthGuard::~` 末尾（原 `if (depth == 0 && max_depth >= 1)` 块之后）加 `if (_r3r_long_reserve_depth == 0) _r3r_long_reserve_nodes = 0;` —— 保证正常退栈、深度上限、环路检测、预算超限**四种出口**都会在回到 depth 0 时清零，计数口径=一次顶层预留尝试。
4. `LR-TRACE` 行尾追加 `nodes=%u`（取 `_r3r_long_reserve_nodes`），用于回答"正常场景一次预留到底用多少节点"。

**为什么 1024**：合法长预留每层跨一个 LR 信号，满编列车也只需几十个节点；1024 给足 20 倍余量。真正的病态现场（§1.4m）是 CPU 210 s 仍未返回，节点数已远超此值。若实测正常场景 `LR-TRACE ... nodes=` 逼近 1024，再上调。

**构建自证**：复用既有 `_tmp_inc_build.cmd`（未新建任何 `.cmd`），仅内测树 `build\`（按规则未编发布树）。
- `build\R3R_incbuild.done` = `EXIT_CODE=0`（18:05:26）
- 日志尾 `[2/3] Building CXX object ... train_cmd.cpp.obj` → `[3/3] Linking CXX executable openttd.exe`，无 error C / FAILED
- `src\train_cmd.cpp` 18:03:44 → `train_cmd.cpp.obj` 18:04:21 → `build\openttd.exe` 18:05:18（51 891 712 B）
- `read_lints` 0 条
- exe 字面量命中 `LR-BUDGET veh=`、`nodes=%u limit=%u`

**复测判据（下次进游戏）**

1. 同场景（拆掉车库岔口 + 轨道上调向）**不再卡死**：单 tick 能返回，界面可操作。
2. 若确实命中病态，日志必须出现 `LR-BUDGET veh=0 ... depth=? nodes=1024 limit=1024`，且**同一行立即暴露爆炸所在的 `tile=`/`td=`/`depth=`**。
3. 正常场景不出现 `LR-BUDGET`；`LR-TRACE ... nodes=` 应在几十量级（若 >300，需记录并评估是否上调上限）。
4. 负判据：正常长预留（无环路、每格信号少）路线仍能订满，列车照常通行，不出现"预留变短导致到站前停车"。

**未做**：未覆盖"单次 `ChooseTrainTrack` 内 YAPF 自身爆炸"（预算只在递归节点上计数；若第一层 YAPF 就卡死，仍抓不到）；未给 `ChooseTrainTrack` 加入/出成对心跳。

### 1.4p 18:14 卡死复现：预算护栏未命中，递归规模正常（2026-10-19 18:2x）

**日志身份**：`D:\temp\codebuddy-dropped-files\bb7ec18f-...\R3R_debug.log`，446 行 / 32 846 B / mtime 18:14:18。产生者应为 `build\openttd.exe` @18:05:18（含 §1.4o 的 `LR-BUDGET` 护栏）⇒ 本日志是**带护栏版本**的现场，读数有效。

**读数（逐条带锚点）**

| 计数 | 值 | 判定 |
|---|---|---|
| `LR-BUDGET` | **0** | 预算 1024 **未触发**，本次卡死不是"单次预留节点爆炸" |
| `LR-TRACE` | **130 行** | 递归**反复正常退栈**（该行只在 depth 回到 0 时打印） |
| `LR-TRACE` depth | max **13** / avg **1.59** | 递归深度很小（对比 10-18 崩溃现场 depth≈54） |
| `LR-TRACE` nodes | max **13** / avg **1.59** | 正常量级只有个位数 ⇒ **1024 的上限有约 78 倍余量，无误伤风险** |
| `LR-DEPTH-CAP` | 0 | 未撞 128 深度上限 |
| `LR-CYCLE` | 0 | 未撞环路检测 |
| `TPR-WALK` | **2 行** | 只有两次预留尝试 |

**关键锚点原文**

- `build\R3R_debug.log:392` `LR-TRACE veh=0 depth=2 tile=122,516 td=1 res_end=1250 cur=1034 le=1251 reason=9 nodes=2` —— 最后一条正常退栈的预留，depth 只有 2。
- `:395` `SPD-DROP veh=0 tile=122,503 prev=1 order=0 real=0 stuck=0 fp=0 prog=145 wait=0 parked=0 stop=0 track=02 dtile=4294967295` —— 速度 1→0；`order=0` 是 `OT_NOTHING`；`dtile=4294967295`=`INVALID_TILE`（**无目的地**）。
- `:400-445` `REVERSEDONE db=1 mvfront=14` 后跟 `RA idx=0..14` 逐节 dump：`x=1960` 一列，`tile=122,500..122,503`，`dir=3`，`trk=0x2` —— 一列 **15 节**列车刚完成换端，竖直停放。
- `:446`（**最后一行**）`TPR-WALK veh=0 mf=122,500 origin=122,500 td=9 okay=0 exit=3 new=122,499 reach=2 spd=0 one=0 look=0`

**结论句（当前证据可支撑的）**

1. 本次卡死**不是**递归爆炸：深度 13、节点 13、130 次正常退栈，预算与深度两道护栏都没被碰到（`LR-BUDGET`/`LR-DEPTH-CAP`/`LR-CYCLE` 全 0）。
2. 卡死点在**最后一条 `TPR-WALK` 之后**，即 `ChooseTrainTrack()` 及其内部 YAPF。这恰好落在我 §1.4o 末尾"未覆盖"的那一格：预算只在 `TryLongReserveChooseTrainTrack()` 入口计数，**单次 `ChooseTrainTrack`/YAPF 内部爆炸计数不到**。
3. 触发形态高度可疑：`spd=0` + `order=OT_NOTHING` + `dtile=INVALID_TILE` + 刚 `REVERSEDONE`（换端）⇒ 疑似"换端后无订单目标仍走 `TryPathReserve`→`ChooseTrainTrack`"，与记忆里"`ReverseTrainDirection` 尾部 `TryPathReserve` 在 `current_order` 为 `OT_NOTHING` 时会乱 reserve"的旧观察同族。
4. `reason=9` 是末段所有 `LR-TRACE` 的一致截断原因，需查 `_r3r_le_reason` 编码表确认其含义（未知，待验证）。

**待验证（优先级序）**

- V-1：`reason=9` 在 `_r3r_le_reason` 编码里代表什么（库格/非 PBS/环检测/前瞻达长）。
- V-2：`TPR-WALK` 各字段定义，尤其 `okay=0 exit=3`、`reach=2`、`look=0`、`one=0`。
- V-3：`ChooseTrainTrack()` 在 `spd=0 && order=OT_NOTHING` 时是否存在无出口的循环（与 YAPF 的 `max_nodes` 无关的那一类）。
- V-4：`REVERSEDONE` 之后为何立刻再 `TryPathReserve`——是 `ReverseTrainDirection` 尾部调用，还是下个 tick 的 `ProcessOrders`。
- V-5：为什么 15 节车只订到 122,499 就 `okay=0`（`exit=3` 方向是否有墙/信号）。

**V-1 已结案（`train_cmd.cpp:13700`）**：`reason=9` 是 `IsReservationLookAheadLongEnough()` 的**正常**出口——找到信号且 `signal_position + 制动距离 <= reservation_end_position` ⇒ 前瞻已足够长 ⇒ `return true`。故末段 10 条 `LR-TRACE` 全是正常终止，不是异常截断。

**调用链已锁定（带行号）**

- `train_cmd.cpp:4575` `ReverseTrainDirection()` 尾部 `TryPathReserve(consist, false, first_tile_okay)` ⇒ 与日志 `REVERSEDONE` → `TPR-WALK` 的顺序逐字吻合，证明本次预留是**换端触发的**。
- `train_cmd.cpp:14839` `ChooseTrainTrack(consist, 122,499, DIAGDIR_NW, TRACK_BIT_Y, CTTF_FORCE_RES | CTTF_MARK_STUCK)`。
- `ChooseTrainTrack` 实现在 `:14043`；**函数内没有 `for(;;)` 主循环**（`:14351 continue` / `:14371 break` 属于 `:14320` 起的"订单-目标重试"块，且 `:14370` 是无条件 `break`）⇒ 卡死不是"`ChooseTrainTrack` 自己在打转"。
- 进入后的第一处重活：`:14126` `ExtendTrainReservation(consist, origin, &tracks, &dest_enterdir, temporary_slot_state)`（定义 `:13177`）。

**猜测根因（待验证，按可能性排序）**

- **G-1（最高）** `ExtendTrainReservation()`（`:13177` / 调用 `:14126`）：其语义是"沿轨道尽可能延伸，遇安全格 / 其它预留 / 轨道分岔才停"。若现场是一段**无信号、无道岔、无车站的闭环**，`CFollowTrackRail::Follow()` 会一直成功、永不 `break`；而该函数**一行日志都不打** ⇒ 与"最后一行 `TPR-WALK` 之后彻底静默 + 单 tick 不返回 + CPU 满载"完全吻合。
- **G-2** `FillTrainReservationLookAhead()`（`:14152` / `:14306` / `:14392`）：REALISTIC 制动模型下按订单/信号逐项计算，`spd=0` 且无订单时是否成环待验。
- **G-3** `VehicleOrderSaver::AdvanceOrdersFromVehiclePosition()`（`:14171`）：订单链成环会死循环；但日志 `order=0`（`OT_NOTHING`）使其可能性降低。
- **G-4** YAPF（`:14269` `DoTrainPathfind`）：通常有节点预算会退出，可能性最低。

**区分判据（下一轮取数，一刀切开）**

在 `:14126` 前后各加一行心跳（`EXTEND-ENTER veh= tile= td=` / `EXTEND-LEAVE n= tiles=`），在 `:14152` 前后加 `FILLLA-ENTER/LEAVE`。**谁没有 LEAVE 谁就是卡点**，不必再猜。

**修复方向（初步，与 `LR-BUDGET` 同一思路：资源预算而非调用形态）**

给 `ExtendTrainReservation()` 加"单次延伸预算"：按 `tile + trackdir` 去重（或简单计数，如 512 格），超限即当作 `okay=false` / `INVALID_TILE` 返回，交给既有失败分支 `MarkTrainAsStuck()` + `FreeTrainTrackReservation()` 收尾。正常线路（有信号/岔口/车站就会停）永远碰不到上限，故零行为变更风险。

### 1.4q 上限方案审阅材料（2026-10-19，供用户拍板，未改码）

**已确认的循环结构（`src\train_cmd.cpp`）**

- `:13207-13209` `tile = origin.tile; cur_td = origin.trackdir; while (ft.Follow(tile, cur_td)) {`
- **循环内全部退出点**只有 6 个，且都必须由"线路特征"触发：
  | 行号 | 条件 | 出口 |
  |---|---|---|
  | `:13212` | `HasOnewaySignalBlockingTrackdir` | `break` → backout |
  | `:13217` | 90 度转弯被禁 | `break` → backout |
  | `:13229-13284` | 岔口/车站/库 + 该格有他人预留 | `break` → backout |
  | `:13296` | 岔口/车站/库 + 无预留 | `return PBSTileInfo(new_tile, old_td, **false**)` |
  | `:13302-13308` | `IsSafeWaitingPosition` 且订位成功 | `return PBSTileInfo(tile, cur_td, **true**)` |
  | `:13337` | tracerestrict `WAIT_AT_PBS` | `break` → backout |
  | `:13342` | `!TryReserveRailTrackdir` | `break` → backout |
- `:13345-13348` `ft.err == EC_OWNER || EC_NO_WAY` ⇒ `return PBSTileInfo(ft.old_tile, ft.old_td, true)`（线路正常尽头）
- `:13350-13398` 其余 break 一律进 backout：`:13369` 打 `EXTEND-BACKOUT`，`:13380-13393` 从 `origin` 重走到 `stopped` 逐格 `UnreserveRailTrackdir`，最后 `return PBSTileInfo()`（`INVALID_TILE`）。

**为什么闭环必死**：一段**无信号、无道岔、无车站/库**的环形轨道上，`ft.Follow` 永远成功、`ft.err` 恒 `NONE`、`KillFirstBit(new_td_bits)` 恒 `NONE`、`target_seen` 恒假、`IsSafeWaitingPosition` 恒假 ⇒ 6 个出口**一个都不会命中**，`tile` 沿环循环更新，`while` 永不退出。这与现场"最后一行 `TPR-WALK` 后彻底静默 + 单 tick 不返回"完全一致。

---

**候选 1（推荐）：总步数上限**

```cpp
uint r3r_extend_steps = 0;
static const uint R3R_EXTEND_MAX_STEPS = 512;
while (ft.Follow(tile, cur_td)) {
    if (++r3r_extend_steps > R3R_EXTEND_MAX_STEPS) { r3r_backout_reason = "max-steps"; break; }
    ...
}
```

- **计数对象**：本次延伸尝试中 `ft.Follow()` 成功的次数（= 走过的格数）。局部变量，不跨调用、不入存档。
- **上限值**：512 格。
- **超限行为**：`break` ⇒ 走**完全既有的** backout 路径（`:13369` 日志 + `:13380` 逐格释放 + `:13398` 返回 `INVALID_TILE`）。不新增分支语义，只多一个 `reason=max-steps`。
- **下游效果**：`ChooseTrainTrack` 的 `:14293` `res_dest.tile == INVALID_TILE` 分支 ⇒ `TryReserveSafeTrack` 兜底 ⇒ 失败则 `FreeTrainTrackReservation` + `MarkTrainAsStuck`。
- **成本**：每格一次 `++uint32`，可忽略。
- **误伤风险**：正常线路每格必遇安全位/信号/车站/岔口，OpenTTD 里连续无中断直线极少超过 100 格；512 有 5 倍余量。
- **可回退性**：改常量即可，无存档影响、无 ABI 影响。
- **副作用（需拍板）**：会 `MarkTrainAsStuck`，该列车显示"卡住"；且下一 tick 仍会重试（再次跑满 512 步），但**不会再冻结游戏**。

**候选 2：路径去重（精确识别绕圈）**

- 记录 `(tile, trackdir)`，重复出现即判定绕圈并 `break`。
- 优点：能区分"合法超长直线"与"绕圈"，也能抓"来回振荡"。
- 缺点：需固定小表（如 `uint64_t[256]` + 线性扫描）或哈希；闭环时 512 次迭代 × 平均 256 次比较 ≈ 13 万次比较，可接受但比候选 1 复杂一个量级；`std::unordered_set` 每格分配，热路径不可接受。

**候选 3：不加上限，只加心跳（最小风险）**

- 仅在 `:13209` 前后打 `EXTEND-ENTER` / `EXTEND-LEAVE steps=`。
- 优点：零行为变更，能给出"到底绕了多少格、从哪一格开始绕"的铁证。
- 缺点：**本次现场仍会卡死**（只是有日志），需再跑一次复现才能拿到数据。

**建议**：心跳与候选 1 一起上（一次构建既止损又取证）；若用户希望最小步，则先上候选 3，按日志确认后再上候选 1。

**待用户拍板的 3 点**

1. 上限值 512 是否接受（可调 256 / 1024）。
2. 超限后接受"标记卡住"（候选 1，最稳），还是要求"保留已订路径、当作岔口交给 YAPF"（需把 `break` 改成 `return PBSTileInfo(tile, cur_td, false)`，路径更可能被另一处逻辑继续消费，但每 tick 仍会重跑 512 步）。
3. 是否接受超限日志 `EXTEND-LIMIT veh= tile= steps= limit=`（建议 64 tick 节流，避免刷屏）。

### 1.4r 补充：候选 2-lite「绕回 origin 即判闭环」（2026-10-19，成本≈0）

上面候选 2 的完整去重表太贵；但本次性质是**纯闭环**，可用最省的判据：

```cpp
/* 放在 while 内部，紧跟 ++steps 之后 */
if (r3r_extend_steps > 1 && ft.new_tile == origin.tile) { r3r_backout_reason = "loop-origin"; break; }
```

- **成本**：每格 1 次 `TileIndex` 等值比较（4 字节比较），可忽略。
- **精度**：无道岔/无站台的一般轨道上 `ft.Follow` 逐格前进，绕满一圈必然回到 `origin.tile` ⇒ 精确抓闭环；**不会误伤合法长直线**（直线永远回不到 origin）。
- **盲区**：`ft.Follow` 在站台格会一步跳过 N 格（`tiles_skipped`），若环上恰有站台且跳过区间跨过 origin，则可能漏判 ⇒ 仍需候选 1 兜底。
- **误伤面**：真·环形线路且环上无任何信号/岔口时，本就会被判定无法找到等待位（走满一圈没有任何出口），故 `break` 是正确结论；环上有信号/岔口时第一圈内就会先命中 `:13212`/`:13296` 等出口，走不到这里。
- **为什么仍要保留步数上限**：环可能极大（地图级大环数千格），"走到绕回为止"本身仍可能耗掉可观的单 tick 时间；512 步上限是时间成本的上界，loop-origin 是语义上的精确判据，两者互补。

**影响汇总表（候选 1 + 心跳）**

| 维度 | 结论 |
|---|---|
| 存档格式 | 无影响（局部变量，不入档） |
| 正常线路行为 | 无影响（正常线路远达不到 512 步，出口条件与判定顺序完全不变） |
| 长直线（>512 格无任何中断） | 理论上会被截断；实际 OpenTTD 地图边长上限 4096，且 512 格内必遇车站/信号/岔口/安全位 |
| 超限后表现 | 列车被 `MarkTrainAsStuck`（界面显示"卡住"），下一 tick 重试仍会跑满 512 步再失败，但**游戏不再冻结** |
| 性能 | 最坏情况每次预留多 512 次循环（单 tick 数十万次操作，微秒级），可忽略 |
| 可回退 | 改 `R3R_EXTEND_MAX_STEPS` 常量即可；也可整体 `#if` 关掉 |
| 风险点 | 若现场其实不是闭环而是"合法但极长的延伸"，512 会把它误判为失败 ⇒ 用 `EXTEND-LEAVE steps=` 心跳读数可立刻分辨（steps 恰好 512/513 且位置在地图边缘 ⇒ 疑似长直线；steps 小得多且反复出现同一 tile ⇒ 闭环） |

### 1.4s 误伤面复核与上限值修正（2026-10-19，用户质疑"512 会误伤谁"后重算）

**§1.4q 里"512 有 5 倍余量、误伤风险低"这句结论过窄，现修正。**

**误伤的精确判据（四个条件同时成立，连续 N 格）**

要走到上限，路径上连续 N 格必须**同时**满足：

1. 没有任何能命中 `IsSafeWaitingPosition()` 的条件（`src\pbs.cpp:2692-2748`）：
   - 无车库格 `IsRailDepotTile`（`:2694`）
   - 无普通信号 `HasBlockSignalOnTrackdir`（`:2697`）
   - 无隧道桥信号模拟入口（`:2699`）
   - 不是线路尽头（`:2707-2710`，`include_line_end=true` 时尽头会返回 true）
   - 下一格无 PBS 信号 `HasPbsSignalOnTrackdir`（`:2720`）
2. 没有任何岔口：`KillFirstBit(ft.new_td_bits) == TRACKDIR_BIT_NONE` 恒成立（`:13222` 不进入）
3. 没有任何车站/库/路点：`target_seen` 恒假（同上）
4. 没有单向信号阻断（`:13212`）、没有 tracerestrict `WAIT_AT_PBS`（`:13337`）、没有订位失败（`:13342`）

⇒ 等价说法：**"从 origin 出发连续 N 格，当前行进方向上没有任何信号、没有任何道岔、没有任何车站/库/路点"**。

**这不"神秘"，而且现实存在**

- 最常见：**单列车专用长支线**——玩家只跑一列车，全程不设信号（没必要），两端各一个车站，中间几十到上千格纯轨道。完全合法、完全正常。
- 其次：景区/观光环线、厂矿专线、地图边缘的孤立支线。
- 反向确认：这种线路上现有行为是"一路延伸预留到线路尽头 → `:13345` `EC_NO_WAY` 返回 `okay=true` → 列车正常开"。所以 512 截断**确实会把它打断**，是真实误伤，而不是理论风险。

**误伤后的实际后果（比 §1.4q 写的好一点，但仍不可接受）**

- 超限 `break` → backout 释放本次订的所有位 → `:13398` 返回 `INVALID_TILE`
- `ChooseTrainTrack:14293` 走兜底 `TryReserveSafeTrack(path_end)`（`:14296`）；成功仍能 `CTTRF_RESERVATION_MADE` 让车动，失败才 `:14308-14309` 释放 + `MarkTrainAsStuck`
- 所以后果是"**可能卡住、可能只能挪一格**"，不稳定，玩家看到的就是"这条线原来好好的，现在列车不动了"

**候选 2 方案对"是否也软着陆"的核对（修正 §1.4q 第 2 问的错误选项）**

§1.4q 曾把"超限时 `return PBSTileInfo(tile, cur_td, false)`"当作更软的处置。核对 `:14280-14290` 后**该说法错误**：

```cpp
if (res_dest.tile != INVALID_TILE && !res_dest.okay) {
    if (mark_stuck) MarkTrainAsStuck(consist);
    FreeTrainTrackReservation(consist, origin.tile, origin.trackdir);
    return { best_track, result_flags };
}
```

`okay=false` 会**跳过** `TryReserveSafeTrack` 兜底，直接释放 + 标记卡住。⇒ **backout（INVALID_TILE）反而是两种处置里误伤更小的那个**，因为它多一层兜底。§1.4q 第 2 问的 (b) 选项应予废弃。

**结论：上限值与判据一起改**

| 项 | §1.4q 原案 | 修正后 | 理由 |
|---|---|---|---|
| 主判据 | 无 | **绕回 `origin.tile` 即 break**（`loop-origin`） | 精确抓闭环，**零误伤长直线**（直线永远回不到 origin） |
| 步数上限 | 512 | **4096** | 只作"loop-origin 漏判"（站台 `tiles_skipped` 跨过 origin）与"来回振荡"的兜底；4096 ≈ 地图边长上限，一条纯直线路径要到 4096 格必须整图贯穿且无任何设施，实用上不存在 |
| 超限处置 | backout / 返回 false | **只保留 backout** | 见上：`okay=false` 会直接卡住，更糟 |
| 性能 | — | 每格 1 次 `TileIndex` 比较 + 1 次 `uint` 自增；最坏 4096 格 ≈ 数百微秒 | 可接受；正常线路几十格就出口 |

修正后的误伤面：**只剩"连续 4096 格无信号/无道岔/无车站的长直线"**。地图边长上限 4096（对角约 8190 步），要凑出 4096 格无设施路径需整张地图只铺一条线——实际不可能。**误伤面从"常见单列车支线"收缩到"理论构造"。**

**待用户拍板（替代 §1.4q 的三点）**

1. 主判据用 `loop-origin` + 步数兜底 4096（推荐），还是只要其中一条？
2. 若用户仍希望保留 512 这种小值，则必须接受"单列车无信号支线会被打断"，请明确。
3. 超限/闭环日志仍建议 `EXTEND-LIMIT veh= tile= steps= limit= reason=loop-origin|max-steps`，64 tick 节流。

### 1.4t 超限之后的完整链路：预留到底会变成什么样（2026-10-19，答用户追问）

**先纠正一个前提**：`ExtendTrainReservation` 走不到尽头时才会超限。`:13345` 的
`if (ft.err == EC_OWNER || ft.err == EC_NO_WAY) return PBSTileInfo(ft.old_tile, ft.old_td, true);`
排在 backout **之前**。所以"600 格的合法长直线"在**未加上限**时是"走到尽头 → 正常返回 okay=true → 列车照开"；只有加了上限且上限 < 600 时，才会在第 N 格被 `break` 掉。误伤的确切成因就是这个。

**超限 break 之后的六步（全部是既有代码，无新语义）**

| 步 | 行号 | 动作 | 对预留的影响 |
|---|---|---|---|
| 1 | `:13369` | `EXTEND-BACKOUT` 日志（reason 会变成 `max-steps` / `loop-origin`） | 只读 |
| 2 | `:13376-13377` | `tile = origin.tile; cur_td = origin.trackdir;` 回到起点 | — |
| 3 | `:13380-13393` | 从 origin **重走一遍** `ft.Follow`，逐格 `UnreserveRailTrackdir(tile, cur_td)`，直到 `stopped`（= `ft.old_tile`，即第 N 格） | **本次新订的 N 格全部释放，一位不留**；`origin` 自己的位不动 |
| 4 | `:13395` | `PopFromChangeStackRevertTemporaryChanges` | tracerestrict 临时槽回滚 |
| 5 | `:13398` | `return PBSTileInfo();`（`INVALID_TILE` + `okay=false`） | 交给 `ChooseTrainTrack` |
| 6 | `:14293-14311` | `path_end = FollowTrainReservation(...)` → `TryReserveSafeTrack(path_end)`；成功则 `CTTRF_RESERVATION_MADE`，失败则 `FreeTrainTrackReservation(origin)` + `MarkTrainAsStuck` | 见下 |

**结局 A：兜底成功**（`TryReserveSafeTrack` → `YapfTrainFindNearestSafeTile`，`:13411-13414`）

- 但此时 `path_end` 已经退回到 `origin` 附近（第 3 步刚把 N 格清空），YAPF 只能从 origin 往前找。
- 而在"神人轨道"上，**前方 N 格内按定义一个安全格都没有**（那正是它超限的原因）⇒ 找不到。
- 所以这条路在本次场景里基本走不通。它会成功只在"origin 附近恰好有别的岔路通向安全格"时。

**结局 B：兜底失败（本场景的实际结局）**

```
FreeTrainTrackReservation(consist, origin.tile, origin.trackdir);  // :14308
MarkTrainAsStuck(consist);                                        // :14309
```

- 列车**原地停住**，`VehicleRailFlag::Stuck` 置位，界面显示"卡住/等待空余轨道"。
- **图上一条预留黑线都不剩**（第 3 步已清光，兜底也没订上）。
- 原本"一路预留到尽头"的合法状态**永远不会再产生**——因为每次重试都是同样的 N 格截断。

**次生问题 1：每 tick 重做 2N 格**

- 重试一次 = 正向 `ft.Follow` N 次 + backout 再 `ft.Follow` + `UnreserveRailTrackdir` N 次 = **约 2N 格**。
- 512 ⇒ 约 1000 格/tick；4096 ⇒ 约 8000 格/tick（每格含 `CFollowTrackRail::Follow` 的地图读取与 trackdir 位运算，粗估 50~200 ns ⇒ 0.4~1.6 ms）。一帧 16 ms 预算，单列车就吃掉 3~10%；多列车同时走这条路会明显拖帧。
- 是否真的每 tick 重试取决于它落在 `TrainLocoHandler` 的哪个分支（`cur_speed==0` 时走停驶分支，见 KI-348 第 242 轮的结论）⇒ 不必假定"必然每 tick"，但**只要它重试就是 2N**。这就是`4096` 比 `512` 贵的地方。

**次生问题 2：backout 中途 break ⇒ 残留孤儿预留**

- `:13381` `if (!ft.Follow(tile, cur_td)) break;` —— backout 是**重新走一遍**，不是按账本释放。
- 正常情况同 tick 内路径未变，重走必成功。但一旦重走在中途失败（地图被改、`tiles_skipped` 因跳格差异对不上、某格状态在同 tick 内变化），后面尚未释放的位就**永久留下**，且没有任何账本记得它们 ⇒ KI-345/KI-346 那一类"孤儿预留走廊"。
- 上限越大，这段"重走"越长，撞上这种窗口的机会越多。这是 `4096` 的第二个代价（`512` 相对安全）。

**次生问题 3：backout 与 loop-origin 的相互作用**

- 闭环时 `loop-origin` 在第 k 格（k = 环长）break，`stopped = ft.old_tile` 是环的最后一格；backout 从 origin 重走到那一格，**整圈都释放**。语义正确。
- 但如果环上某站台 `tiles_skipped` 一步跨过 `origin.tile`，`loop-origin` 漏判 ⇒ 退化到 `max-steps` 截断，此时 `stopped` 落在环中间，backout 只释放到那里——**剩下的半圈仍是本次订下的位，但已被释放了一半**，图上会出现"半个环有黑线、半个环没有"的怪异状态。这解释了为什么两者都要保留：`loop-origin` 保证整圈一次清干净。

**结论**：超限不会留下"半截合法预留"，它要么**全清 + 卡住**，要么（兜底凑巧成功）**只剩 origin 附近一小格**。真正的代价不是"预留错乱"，而是：①列车停死不再前进；②每 tick 重复 2N 格白跑；③backout 越长，孤儿残留窗口越大。⇒ 上限值的选择实质是**"误伤面"与"每 tick 成本 / 残留风险"的权衡**，`loop-origin` 让绝大多数闭环在环长（远小于上限）处就精确退出，从而把上限实际触发率压到接近零。

### 1.4u 归属取证：超限/长等待是 R3R 引入还是上游自带（2026-10-19，答用户追问）

**结论：循环本体与"无上限"都是上游自带的；512/4096 截断至今尚未实现，只是本轮方案；R3R 只在本轮新增了一处会让这个循环走得更远的旁路。**

| 项 | 归属 | 证据 |
|---|---|---|
| `ExtendTrainReservation()` 函数本体 | **上游 OpenTTD** | `git log -S` 首现 `0b38276d5a (svn r13959) -Codechange [YAPP]: Try to extend a train reservation as far as possible...（michi_cc）`；JGRPP 直接继承 |
| `while (ft.Follow(tile, cur_td))` 无步数上限 | **上游原样** | `git show 0b38276d5a:src/train_cmd.cpp` 中同名函数即为 `while (ft.Follow(tile, cur_td))`，体内无 `steps`/`visited`/任何上限；当前树 `train_cmd.cpp:13209` 与之一致（只多出 R3R 的日志赋值） |
| 闭环无限循环（`loop-origin` 要解决的那个） | **上游固有** | 上游既无 visited 集也无上限 ⇒ 只要轨道拓扑成环、沿途无安全位/无他人预留，就会一直转 |
| 长直线一路预留到尽头 | **上游行为**（非缺陷） | `train_cmd.cpp:13345-13348` `EC_OWNER || EC_NO_WAY ⇒ return PBSTileInfo(ft.old_tile, ft.old_td, true)` 是上游原有分支，走不到尽头才进 backout |
| `EXTEND-LIMIT` / `max_steps` / `loop-origin` 截断 | **未实现**（本轮方案） | 全 `src` 搜这三个字面量 **0 命中**；当前 `:13209` 仍是裸 `while`，`:13376-13393` backout 也仍是"从 origin 重走一遍"的无限循环 |
| 「清掉自己的陈旧预留位后继续循环」 | **R3R 新增（本轮，续四十七）** | `train_cmd.cpp:13275-13285`：blame 位属于本链且 `R3RReleaseReservationLedgerChecked()` 成功 ⇒ **不 break**，落下去继续 while。上游此处是 `if (HasReservedTracks(...)) break;` **无条件 break**。⇒ 这是唯一一处 R3R 把"该停的点"改成"清掉继续走"，它本身不制造无限，但会把这个循环推得更远、更容易撞到环 |

**副作用核对**：`:13380` 的 backout `while (tile != stopped || cur_td != stopped_td)` 也是上游原样（同样无上限）⇒ 上限截断方案若只加正向而漏掉 backout，则 `break` 之后 backout 仍会走满 N 格（不是无限，但有界不设防）。两个循环都要加上限才完整。

**一句话**：上游给了"会一直走"的循环，R3R 在本轮又给它加了一条"遇到自己的陈旧位就清掉继续"的续命路径；而 512/4096 是把上游的"一直走"改成"走 N 格就退"的新规则，属于**我们主动引入的行为变更**，不是修上游 bug（上游并不认为那是 bug，因为没有上限才是它设计的一部分：一路预留到尽头）。

### 1.4v 更正：用户实机对照推翻 1.4u 的结论（2026-10-19）

**用户实测（权威）：几乎相同的环形场景在 JGRPP 0.73.0 / 0.73.2 上都不崩、不卡死；同一场景在 R3R 上崩/卡。⇒ 1.4u 里"闭环无限是上游固有"这句必须撤回。**

**撤回后的正确画像**：上游循环"没有显式上限"≠"在环上无界"。上游用**预留位本身当 visited 集**：
- `0b38276d5a` 版本循环体内 `if (!TryReserveRailTrack(tile, TrackdirToTrack(cur_td))) break;` —— 绕回已订格时 TryReserve 必失败 ⇒ 天然在上游环处终止（最多一圈）。
- 只在 `target_seen || KillFirstBit(ft.m_new_td_bits) != NONE`（分叉/目标格）处才额外查 `HasReservedTracks(...)` 并**无条件 break**。

**猜测根因（待验证，优先级从高到低）**：
- **猜测 A（最可能）**：R3R 把"自己的位"变成可重复预订 / 不触发 break。候选落点：`TryReserveRailTrackdir()`（R3R 版，`train_cmd.cpp:13342`）或预留记账 ledger 让 `TryReserveRailTrack` 对本人已押位返回成功 ⇒ 上游那个"预留位=visited"的自终止机制失效 ⇒ 环上无限绕。
- **猜测 B**：R3R 使 `IsSafeWaitingPosition()` 对含假引擎/段的链恒为假 ⇒ 环上永远不 `return PBSTileInfo(tile, cur_td, true)`，只能靠 TryReserve 失败退出；一旦 A 同时成立就无限。
- **猜测 C**：`STALE-OWN-BIT`（`:13275-13285`）清位后 `continue` 绕环 —— **注意此分支只在"分叉/目标格"才走到**，纯直线环到不了，故单独不足以解释本次现象，但会在带道岔的环上叠加。

**待验证判据**：①`TryReserveRailTrackdir` 对"本车已押的同一 track"返回值；②上游 `TryReserveRailTrack` 同参数下返回 false；③环场景日志里是否出现 `STALE-OWN-BIT`（若无则该分支无关）；④`IsSafeWaitingPosition` 在环上满格时是否恒假。

**归属结论（暂定）**：512/4096 不是"给上游补上限"，而是**给 R3R 自己破坏掉的自终止机制打补丁**；因此正确修法优先是**恢复"本车已押位不得重复预订"**（或把 visited 显式化），上限只作兜底。

### 1.4w 猜测 A 已证伪：`ExtendTrainReservation` 不是无限源（2026-10-19）

`src/rail_map.h:267-277`：

```cpp
inline bool TryReserveTrack(TileIndex tile, Track t)
{
	dbg_assert_tile(HasTrack(tile, t), tile);
	TrackBits bits = TrackToTrackBits(t);
	TrackBits res = GetRailReservationTrackBits(tile);
	if ((res & bits) != TRACK_BIT_NONE) return false;  // already reserved
	res |= bits;
	if (TracksOverlap(res)) return false;              // crossing reservation present
	SetTrackReservation(tile, res);
	return true;
}
```

- **对已押位无条件 return false，没有任何 R3R 的 owner/ledger 放行** ⇒ 环上二次到达同格 → `TryReserveRailTrackdir` 失败 → `train_cmd.cpp:13342` `reserve-fail` break。上游与 R3R 在此**逐字同构**，上游"预留位=visited 集"的自终止机制在 R3R 里**完好**。
- 推论：`ExtendTrainReservation` 的正向 `while` 与 `:13380` backout 在**纯环上都有界**（≤ 环长 + 一圈回滚）。⇒ 1.4v 猜测 A、B 均不成立；512/4096 加在这两处**治不了环上卡死**。

**候选重新排序（R3R 独有、待取数）**：
- **D（新增，最可能解释"卡顿像卡死"）**：探针 I/O 放大。`rail_map.h:250-254` 的 `R3RResvWatchLog` + `pbs.cpp:948-957` 的 `UNRES-RAIL` 每次改预留都写文件；环场景每 tick 预留/回滚整圈 ⇒ 每 tick 数百~数千行 `build\R3R_debug.log`。与 KI-135（日志刷到 1.1GB 后冻结）**同族**。发布/探针 OFF 版不会卡 ⇒ 可解释"JGRPP 不卡"。
- **E**：`yapf_rail.cpp:1022` 的 `rp()`（**R3R 独有**的挂车预留递归，`if (depth > 512) return false;`）。环上每次命中都递归到 512 层，每层 `TrackFollower` + 预留 → 单 tick 开销极大；上限保证不死循环，但可造成明显卡顿。
- **F**：`FollowTrainReservation()`（pbs.cpp）若有 R3R 的 ledger/owner 改动破坏"遇到本车即停"，会真无限。**未取证**。
- **G**：R3R 订单/段状态机（`ProcessOrders` 的 GOTO_COUPLE/WAIT_COUPLE 环路）而非轨道预留。

**下一步唯一需要的数据**：①是 crash（有 `crash-*.log`/RIP）还是纯卡顿；②卡时 `build\R3R_debug.log` 的体积/增长速度；③用的 exe 是探针版（build\）还是发布版（build-release\）。三者任一即可把 D/E/F/G 收敛到一条。

### 1.4x 澄清「两个 512」，并按"卡死不崩"重定向（2026-10-19）

**用户确认**：R3R 是**卡死、没崩溃**；同一环形掉头场景在原生 JGRPP 0.73.0/0.73.2 上**无事发生**。

**必须先分清两个 512，否则归属判断会串线**：

| 512 | 位置 | 状态 | 针对什么 | 归属 |
|---|---|---|---|---|
| `R3R_CHAIN_WALK_LIMIT = 512` | `src\train_cmd.cpp`（KI-217） | **已实现并编译进 exe** | `GetMovingNext/GetMovingFront` 在 DrivingBackwards 混合链上呈周期 2 死循环 → 一次触发打 19M 行日志（`build\R3R_debug.log` 从 209 KB 刷到 1.1 GB）→ 游戏冻结、不崩溃 | **R3R 独有**（见下） |
| `ExtendTrainReservation` / backout 步数上限（512 正向 / 4096 之类） | `src\train_cmd.cpp:13209` 与 `:13380` | **未实现（只是本轮方案）** | 环上预留无界绕圈 | 上游循环本体；但**已被 1.4w 证伪为纯环无界**（`TryReserveTrack` 对已押位恒 false ⇒ 一圈必 break） |

**因此对用户这句话的准确回答**：
- 我上一轮说的 512/4096（`ExtendTrainReservation`）确实是针对"卡死不崩的环形无限循环"设计的，但你场景的卡死**很可能不是它**（1.4w 已证伪纯环无界）。
- R3R 里**真正已经落地、且确实针对"卡死不崩 + 日志暴涨"**的是 `R3R_CHAIN_WALK_LIMIT=512`（KI-217，第 135 轮）。它有明确的 R3R 独有根因：R3R 的 couple/折叠/车库/翻转提交点直接重链接而不调 `Train::ConsistChanged()` ⇒ 链上 `VehicleFlag::DrivingBackwards` 一半 1 一半 0 ⇒ `GetMovingFront()/GetMovingNext()` 选出周期 2 的环（现场 MV dump `24,25,26,23,26,23,…`）⇒ 该循环**没有上限**，一次触发就打印 19M 行、日志 1.1 GB、游戏冻结。JGRPP 根本没有这条重链接路径，`ConsistChanged()` 恒把链头 db 复制全链 ⇒ **同等环形掉头在 JGRPP 上不可能出现 db 混合，所以"无事发生"完全自洽**。

**判定（待日志体积佐证）**：若卡死时 `build\R3R_debug.log` 体积暴涨（几百 MB~GB 级）、且尾部是 `MV`/`F`/`RESPACE`/`GEO` 之类逐车 dump 刷屏 ⇒ 就是 KI-217 家族（R3R 独有），不是 `ExtendTrainReservation`。

**仍可能的分支**：KI-217 只在 `R3RRespaceChainAfterEdit` 主循环与两个 dump 循环加了 visited<512；**其它**同样沿 `GetMovingNext/GetMovingFront` 步进的循环若未加上限，环形掉头仍可无限 ⇒ 需逐一核对（R3RDumpChainGeometry、R3RCheckChainFoldedDirection、R3RSettleChainSegments、R3RFlipChainBySegments、R3RNormaliseMixedChainDrivingBackwards…）。

### 1.4y 取证：512 的实际覆盖范围与"环形掉头"的剩余无界循环（2026-10-19）

`R3R_CHAIN_WALK_LIMIT = 512` 定义于 `train_cmd.cpp:7340`（注释自陈："the cap only exists so a corrupted link/flag can never turn a walk into an endless loop (see KI-217)"）。实际只用在 **9 处**：

| 行 | 用途 | 是否受限 |
|---|---|---|
| `:5508` | `R3RSettleWorthDumping` 的 `Next()` 遍历（写死字面量 512，因常量在文件后段） | ✅ |
| `:7450` `:7458` | `R3RNormaliseMixedChainDrivingBackwards` 的 db 扫描/修写（`Next()`） | ✅ |
| `:7522` | `R3RDumpChainGeometry`（`Next()`） | ✅ |
| `:7643` | `R3RRespaceChainAfterEdit` 的 `geo_before` 快照（`Next()`） | ✅ |
| `:7670` | `R3RRespaceChainAfterEdit` 主步进循环（`GetMovingNext()`，KI-217 的原现场） | ✅ |
| `:9253` | `DUALHEAD` dump（`Next()`） | ✅ |
| `:16913` `:16927` | `F` / `MV` 两个 dump 循环（`Next()` / `GetMovingNext()`，19M 行现场） | ✅ |

**仍无上限的 `GetMovingNext()`/`GetMovingFront()` 循环（同一份 `train_cmd.cpp`）**：`:215`（`IterateFrontOnly` 逐链）、`:644`/`:651`（站台 overhang）、`:678`、`:893`/`:901`（`GetTrainStopLocation`）、`:1152`（lookahead trim）、`:4256`（`TrainController` 出库）、`:4457`（`REVERSEDONE` 的 `Next()` dump）。

⇒ **结论**：512 只堵住了"诊断打印 + respace"这条 KI-217 原路；**运行时** 的 `GetMovingNext()` 循环（尤其 `:651`/`:901`/`:4256`）在 db 混合链上仍是裸循环。环形掉头只要在 `ReverseTrainDirection`（`ConsistChanged` 之前/之后）留下 db 不一致，就会在下一个 tick 卡在这些循环里 —— **卡死、不崩溃、无日志暴涨**，与用户描述完全吻合，且 JGRPP 因 `ConsistChanged()` 恒把链头 db 复制全链而不可能复现。

**判定**：用户场景的归属 = **R3R 引入**（不是上游）；但落点**不是** `ExtendTrainReservation`，而是"R3R 换端/重链接未统一 db"这一 KI-217 家族的**未封口部分**。修复优先级：①找出环形掉头路径中哪个提交点漏了 db 归一（`R3RNormaliseMixedChainDrivingBackwards` 有 4 个调用点需逐个核对是否覆盖 `ReverseTrainDirection`/`ForceFlipReverse`）；②给 `:651`/`:901`/`:4256` 三类运行时循环加 `R3R_CHAIN_WALK_LIMIT` 兜底（成本为零，正常链远小于 512）。

### 1.4z 用户澄清：他说的 512 是「预留步数上限」，且本次卡死**无日志暴涨**（2026-10-19，权威）

**用户原话要点**：
- 「我指的 512 限流就是那个会导致如果有连续 512 格什么都没有的轨道，就会列车对着空旷的轨道卡死的那个东西，也就是今天下午我们在讨论的东西。」
- 「游戏卡死，**并没有出现什么日志暴涨**。」

**⇒ 三条必须立刻执行的更正**：

1. **1.4x / 1.4y 的"KI-217 家族"定性作废**。KI-217 的现场签名是 `R3R_debug.log` 从 209 KB 刷到 1.1 GB；本次**无日志暴涨** ⇒ 不是 dump 循环、不是 `R3RResvWatchLog` I/O 放大（候选 D 排除）、也不是 `GetMovingNext` 周期 2 的 19M 行打印。
2. **1.4w 的"ExtendTrainReservation 有界、不用管"结论必须重审**。用户描述的正是那个循环的**步数上限**：走到 512 格仍无车（环上或长直线上）就 `break`，预留没走完 ⇒ 后续判据认为预留不完整/被挡 ⇒ 列车停在空旷轨道前不动。**这不是"会不会无限"的问题，而是"上限本身制造卡死"的问题** ⇒ 我上轮论证的方向偏了。
3. 用户所指对象与 `R3R_CHAIN_WALK_LIMIT` **无关**，那一节继续保留只作对照，不再用于解释本次现象。

**待补全的「512 清单」**（本轮必须逐条列出，避免再绕）：见 §1.4z-表。

#### 512 清单（初版，待代码核对后定稿）

| # | 名称/位置 | 值 | 触发后果 | 状态 |
|---|---|---|---|---|
| ① | `ExtendTrainReservation` 正向预留步数上限（`train_cmd.cpp` ≈13209 一带） | 512（待核） | 连续 512 格无车 ⇒ 提前 `break` ⇒ 预留不完整 ⇒ **列车对着空旷轨道卡死** | **用户所指**；需核实是否上游固有 |
| ② | 同上，回退/backout 段（≈13380） | 4096（待核） | 回退上限 | 待核 |
| ③ | `R3R_CHAIN_WALK_LIMIT`（`:7340`） | 512 | 只影响 dump/respace 遍历，**不参与运动决策** | 已实现（KI-217） |
| ④ | `yapf_rail.cpp:1022` `rp()` 递归 `depth > 512` | 512 | 挂车预留递归上限，超限 `return false`（挂车失败，非列车卡死） | 已实现（R3R 独有） |
| ⑤ | （待搜）其它 `512`/`4096` 常量 | ? | ? | 待核 |

**下一步取证（唯一目标）**：定位 ① 的真实数值与代码原文，并确认它是上游 JGRPP 就有、还是 R3R 加的；若上游也有，则解释"为什么 JGRPP 不卡"必须落在**进入该循环的前置条件**（预留起点/长度/环上 target_seen）差异上，而不是循环本身。

#### 1.4z-定稿：代码核实结果 —— 用户所指的那个 512 **在代码里不存在**（2026-10-19）

**核实方法**：`search_content` 全 `src\*.cpp` 搜 `\b(512|4096)\b`，再逐处看上下文。

| 512 出现处 | 原文 | 与本次讨论的关系 |
|---|---|---|
| `train_cmd.cpp:5508` | `for (... n++) { ... }` 循环条件 `n < 512`（`R3RSettleWorthDumping` 的链遍历，字面量） | 只决定"这条链要不要打 dump"，**不参与运动** |
| `train_cmd.cpp:7340` | `static const int R3R_CHAIN_WALK_LIMIT = 512;`（注释自陈见 KI-217） | 只用于 dump / respace 遍历，**不参与运动** |
| `train_cmd.cpp:19940` | `(15 * (512 + speed) / 512)` | 牵引阻力公式常数，无关 |
| `vehicle_cmd.cpp:425` / `:470` | 克隆时锁步遍历 `n < 512` | 克隆用，无关 |
| `yapf_rail.cpp:970` / `:1054` | `if (depth > 512) return false;`（`reach()` 与 `rp()` 两个挂车预览递归） | 只影响**挂车预览/预留**，超限返回 false（挂车失败），不导致列车运动卡死 |
| `vehicle.cpp:1663` / `tree_cmd.cpp:604` / `fileio.cpp:525` 等 | tick/噪声/TarHeader 尺寸 | 全部无关 |

**关键否定结论**：`ExtendTrainReservation()` 的主体是

```13209:13300:src/train_cmd.cpp
	while (ft.Follow(tile, cur_td)) {
		...
		tile = ft.new_tile;
		cur_td = FindFirstTrackdir(ft.new_td_bits);
```

**整个循环没有任何步数计数器、没有 512、没有 4096、没有 `break` 上限**。函数内 `break` 只来自：单向信号（:13212）、90° 禁转（:13217）、choice/target 且已被预留（:13284）、`IsSafeWaitingPosition` 但预留失败（:13305）。⇒ **"连续 512 格空旷轨道就放弃预留"这件事在当前代码里不存在，它是上一轮我提出的修复方案，不是现状。**

**⇒ 该方案作废理由（用户的批评成立且是致命的）**：给这个循环加"最多走 512 格"的上限后，在**长直线/大环**上会出现"轨道完全空旷、预留却在中途被掐断"，下一 tick 判据认为预留不完整 ⇒ 列车对着空旷轨道停住。**等于用一个新的卡死换掉一个未证实的卡死**。

**⇒ 本次"环形掉头卡死、无日志暴涨"的候选，重新按"R3R 独有 + 不刷日志"排序**：

| 候选 | 机制 | 是否符合"无日志暴涨" | 状态 |
|---|---|---|---|
| **H（最高）** | db 混合 ⇒ `GetMovingNext()` 周期 2 ⇒ 踩进**纯计算**的运行时循环（`train_cmd.cpp:651` 站台 overhang、`:901` stop-location、`:4256` `TrainController` 出库）。这些循环**一行日志都不打** | ✅ 完全符合（死循环但静默） | 推测，待验 |
| I | `R3RFlipChainBySegments` / `R3RReassignArticGroupRoles` / `R3RRespaceChainAfterEdit` 在环形/混合链上内部 while 成环 | ✅（除 respace 的 summary 行） | 推测，待验 |
| J | 订单状态机（GOTO_COUPLE/WAIT_COUPLE/DECOUPLE 互相回绕）每 tick 重试 | ✅（只打少量 COUPLE-FAIL） | 推测，待验 |
| K | `ExtendTrainReservation` 真无限 | ✅ | **已证伪**（TryReserveRailTrackdir 对已押位必失败） |
| L | 探针 I/O 放大 | ❌ 与"无日志暴涨"矛盾 | **排除** |
| M | KI-217 dump 循环 19M 行 | ❌ 同上 | **排除** |

**判据（唯一还需要的一句话）**：R3R 卡死时是**整个游戏无响应**（画面冻结、点不动、CPU 单核 100%）还是**游戏照常运行、只有那列车不动**？
- 整个无响应 ⇒ H 或 I（死循环）；
- 只有车不动 ⇒ 不是死循环，是"预留/寻路每 tick 失败"（J 或预留判据），修法完全不同。

### 1.4aa 新现场锚点：2026-10-19 18:14 那次日志（用户给定，权威）

**用户原话**：「你可以注意到今日 18:14 的那个日志，是的，那一次日志没有暴涨，游戏也只是卡死。」

**由此锁定的两条硬事实**：
1. **日志没有暴涨** ⇒ 排除全部 dump 刷屏路径：KI-217 的 `F`/`MV` dump 19M 行（`:16913`/`:16927`）、`R3RRespaceChainAfterEdit` 主循环（`:7670`）、`R3RDumpChainGeometry`（`:7522`）、`REACH`/`RP` 深度 ≤15 的 fprintf（`yapf_rail.cpp:964-968`/`:1024-1028`）、探针 I/O 放大（候选 D/L）。
2. **游戏"只是卡死"** ⇒ 引擎仍在跑（只是无响应/不推进），不是崩溃退出 ⇒ 死循环或超长正运算，不是 assert/crash。

**⇒ 候选收敛到 H / I**（db 混合踩进纯计算循环 `:651`/`:901`/`:4256`，或 R3R 段/翻链内部 while 自缠），二者都不打日志。

**待核（本轮）**：把 18:14 那份日志的体积、行数、最后若干行取出来，作为"卡死时尾部是什么形态"的唯一实证；若尾部停在某个探针行（如 `GEO`/`FOLDCHK`/`COUPLE-*`/`TRP`），即可直接指认卡死前一瞬的调用面。

#### 1.4ab 实证：18:14 日志尾部 = `TPR-WALK veh=0`，之后零输出（决定性）

**文件**：`build\R3R_debug.log`，**32 846 B / 446 行**，`LastWriteTime = 2026-10-19 18:14:00`。⇒ 确认**没有暴涨**（同期 `R3R_perf.log` 285 673 B @18:13:59）。

**日志最后 6 行（逐字）**：

```
RESV-LEDGER-FORGET veh=0 why=free-path mask=0x1 released=17 kept=0
REVERSEDONE db=1 mvfront=14
  RA idx=0 x=1960 y=8063 tile=122,503 dir=3 trk=0x2
  ...
  RA idx=14 x=1960 y=8009 tile=122,500 dir=3 trk=0x2
TPR-WALK veh=0 mf=122,500 origin=122,500 td=9 okay=0 exit=3 new=122,499 reach=2 spd=0 one=0 look=0
```

**三条硬结论**：
1. **卡死点 = `TPR-WALK` 之后的调用面**。`TPR-WALK` 是该探针家族在本 tick 的最后一条输出，此后 0 行 ⇒ 游戏冻结在"预留/寻路"这一段，不是 dump、不是日志 I/O。
2. **来源是掉头**：紧邻上方 `REVERSEDIR veh=0 … rev=1 spd=0` → `REVERSEDONE db=1 mvfront=14`。掉头后整链 **db=1**（DrivingBackwards），移动前端 = idx14（原链尾，位于 `tile=122,500`，与 `mf=122,500` 一致）。即 **db 是一致的**，不是 KI-217 的混合 db ⇒ 更佐证"不是 217 家族"。
3. `okay=0`：`origin=122,500` 被判为**不是安全等待位**（或预留未成功），随后本应进入 `TryReserveSafePath`/`ChooseTrainTrack`（YAPF）继续；卡死就发生在这之后。

**下一步唯一取证**：定位 `TPR-WALK` 探针在 `train_cmd.cpp` 的行号与其紧邻的下游代码（`TryPathReserve` 尾部 → `TryReserveSafePath` → `ChooseTrainTrack` → YAPF），确认该路径上存在"无日志、可无界"的循环或递归。

#### 1.4ac 根因锁定：`TryLongReserveChooseTrainTrack()` 递归爆炸（代码注释已自陈同一事件）

**`TPR-WALK` 探针位置 = `train_cmd.cpp:14821-14835`**，其**正下方就是** `ChooseTrainTrack(...)`（`:14839`）。

而 `:13766-13798` 的注释**逐字描述了本次事件**：

> "…a trace restrict program which keeps returning TRPRF_LONG_RESERVE, a reservation end which does not advance, or simply a run of LR signals longer than the lookahead window — recurses until the stack is exhausted. The crash report shows exactly this shape: 54 alternating ChooseTrainTrack / TryLongReserveChooseTrainTrack frames (crash-20261018T144344Z.log, triggered by CmdReverseTrainDirection on veh 285)."
>
> "**That is exactly the 2026-10-19 hang: TPR-WALK was the last line written, the recursion never unwound (LR-TRACE only prints at depth 0), and the process sat at 100% CPU with a frozen window.**"

**机制（与 18:14 日志完全吻合）**：
1. `TryPathReserve` 走到 `:14839` `ChooseTrainTrack` → 结果带 `TRPRF_LONG_RESERVE`（信号要求继续往前预留）；
2. `ChooseTrainTrack` 内部回调 `TryLongReserveChooseTrainTrack()` → 又调 `ChooseTrainTrack()` → 递归；
3. 每一层都跑**完整 YAPF 搜索**，可向多个信号分支扇出；
4. `LR-TRACE` 只在 **depth 0** 打印（日志里全是 `depth=1`/`depth=2`，`cur` 每次 +16 ≈ 一格 = 8px×2？说明每层都在推进但**预留终点 res_end 一直跟着走**，`reason=9` 恒不变 ⇒ 永远满足 "再往前" 条件）；
5. 递归栈被 `R3R_LONG_RESERVE_MAX_DEPTH=128` 封住（防崩），但 **128 层 × YAPF 扇出** 仍能在单个 tick 内烧掉分钟级 CPU ⇒ **画面冻结、100% CPU、日志停在 `TPR-WALK`、零新增行**。

**现有防线与缺口**：
- `R3R_LONG_RESERVE_MAX_DEPTH = 128`（`:13779`）⇒ 只防栈溢出（08-18 的 crash）；
- `R3R_LONG_RESERVE_MAX_NODES = 1024`（`:13797`）+ `_r3r_long_reserve_visited[]`（`:13804`）⇒ 本轮为 hang 新加，**必须核实是否真的在递归入口递增/检查**（否则是死代码）。

**⇒ 18:14 卡死 = 该递归未能收敛，不是 db 混合、不是预留无界绕环、不是 `ExtendTrainReservation`。**

#### 1.4ad **上面这条已被证伪**：递归爆炸不成立（2026-10-19，决定性）

**证据（`build\R3R_debug.log`，446 行）**：

| 探针 | 命中数 | 含义 |
|---|---|---|
| `LR-BUDGET` | **0** | 从未触及 1024 节点预算 |
| `LR-DEPTH-CAP` | **0** | 递归从未到 128 层 |
| `LR-CYCLE` | **0** | 从未检测到成环 |
| `LR-NO-PROGRESS` | **0** | `res_end` 一直在前进 |
| `LR-TRACE`（全部） | 若干，**最大 `depth=2 nodes=2`** | 递归只有 1~2 层 |

最后 5 条 LR-TRACE 逐字：

```
LR-TRACE veh=0 depth=2 tile=122,514 td=1 res_end=1218 cur=1002 le=1219 reason=9 nodes=2
LR-TRACE veh=0 depth=1 tile=122,514 td=1 res_end=1218 cur=1012 le=1219 reason=9 nodes=1
LR-TRACE veh=0 depth=2 tile=122,515 td=1 res_end=1234 cur=1018 le=1235 reason=9 nodes=2
LR-TRACE veh=0 depth=1 tile=122,515 td=1 res_end=1234 cur=1028 le=1235 reason=9 nodes=1
LR-TRACE veh=0 depth=2 tile=122,516 td=1 res_end=1250 cur=1034 le=1251 reason=9 nodes=2
```

⇒ **深度 2、节点 2、每 tick 都在推进**（`res_end` 1218→1234→1250，每次 +16 = 一格），这是**健康的长预留**，不是病态递归。

**⇒ 同时可确认 exe 是修复版**：`src\train_cmd.cpp` 18:03:44 → `train_cmd.cpp.obj` 18:04:21 → `build\openttd.exe` 18:05:18（51 891 712 B），而日志 18:14:00 ⇒ 18:14 那次跑的是**已含** depth 128 / nodes 1024 / cycle 三道防线的 exe，且它们全部没被触发。

**⇒ 卡死点重新收敛为唯一一处**：`TPR-WALK`（`:14821`）紧接 `:14839` 的

```14838:14839:src/train_cmd.cpp
	if (reachable != TRACK_BIT_NONE) {
		ChooseTrainTrackResult result = ChooseTrainTrack(consist, new_tile, exitdir, reachable, CTTF_FORCE_RES | (mark_as_stuck ? CTTF_MARK_STUCK : CTTF_NONE));
```

`TPR-WALK` 实测字段：`mf=122,500 origin=122,500 td=9 okay=0 exit=3 new=122,499 reach=2 spd=0 one=0 look=0`。
`look=0` ⇒ `consist->lookahead == nullptr`（所以不是 `FillTrainReservationLookAhead`）；`reach=2` 非空 ⇒ **确实进入了 `ChooseTrainTrack`**，此后零输出。

**剩余嫌疑（按可能性）**：
1. `ChooseTrainTrack` 内部对每个 reachable trackdir 迭代调用 YAPF，`CTTF_FORCE_RES` 下若 `CTTRF_LONG_RESERVE` 反复成立，存在"再试一遍"的循环（需读函数体确认）；
2. 单次 YAPF 搜索在环形 + 信号网络上达到 `max_search_nodes` 上限的极端耗时；
3. `pbs.cpp` 的 `FollowTrainReservation` / `CheckTrainsOnTrack` 在 `spd=0`、`origin.okay=0` 时进入的回溯循环。

**下一步**：读 `ChooseTrainTrack` 函数体，找"可无界且不打日志"的循环。

#### 1.4ae `ChooseTrainTrack` 内唯一"无日志可循环"的点 = `:14333` 的 `while (!IsSafeWaitingPosition(...))`

函数体 `:14043-14430` 逐段核对结果（全部**不打日志**）：

| 行 | 结构 | 是否有界 |
|---|---|---|
| `:14122` | `FollowTrainReservation()`（pbs.cpp 沿预留回溯） | 依赖预留图，理论上若预留互相指向可成环 |
| `:14126` | `ExtendTrainReservation()` | 有界（`TryReserveRailTrack` 对已押位恒 false ⇒ 一圈必 break） |
| `:14221` | `DoTrainCouplePathfind()`（YAPF） | 受 `max_search_nodes` |
| `:14269` / `:14346` | `DoTrainPathfind()`（YAPF，`while` 内每轮一次） | 受 `max_search_nodes`，但**外层可能反复调** |
| `:14333` | `while (!IsSafeWaitingPosition(...)) { ... if (res_dest.okay) continue; ... }` | **取决于 `orders.SwitchToNextOrder(true)` 是否回绕** |
| `:14409` | `TryLongReserveChooseTrainTrack()` | 已被 1024/128 封住（本次 0 命中） |

**`:14333` 的失控形态**（与现场完全吻合）：`origin.okay=0`、`spd=0`，`ExtendTrainReservation` 返回一个**能订得上、但不是安全等待位**的 `res_dest`；循环体每轮 `DoTrainPathfind` **跑一次完整 YAPF**，若 `cur_dest.okay` 为真就 `continue`。只要 `SwitchToNextOrder()` 能一直返回 true（订单回绕 / 循环订单），这个 `while` 就是**单 tick 内的无限循环 + 每轮一场 YAPF** ⇒ 日志零输出、CPU 单核满、窗口冻结。

**待核（最后一问）**：`VehicleOrderSaver::SwitchToNextOrder()` 在订单表末尾是返回 `false` 还是回绕到第一条？若回绕 ⇒ 本条即为最终根因；若返回 false ⇒ 转查 `:14122 FollowTrainReservation` 的预留图成环。

### 1.4 取证计划（按顺序，每组取证完立刻回填本文件）

1. 检查 `build-release\` 下是否存在 `openttd.pdb` / `openttd.map`；若有，用 `dia2dump` / `llvm-symbolizer` / `cvdump` 反查
   `RVA 0xCBCA69 / 0xCBCF41 / 0xCDE8CB / 0xCBCA69 / 0xFF2FA4 / 0x405649 / 0x418080 / 0x41F48A / 0x4195B8 / 0x44643A / 0x440E79`
   （RVA = 地址 − 0x00007FF634000000，需先用 `dumpbin /headers` 确认 ImageBase）。
2. 若 pdb 不可用：用「同源码 + 同编译参数」本机重编一份带 map 的 exe（**不新建 .cmd**，复用 `R3R_release_fullbuild.cmd` 的 map 开关），或用 `dumpbin /disasm` 对 `0xCBCA69` 附近反汇编找 `call` 目标。
3. 确认递归双方后，在源码中定位"缺终止条件"的循环，加 `visited` 上限 + 断言探针（只在 Debug 探针版生效）。
4. 顺带检查 `ReverseTrainDirection` 路径上所有 `while (v = v->Next())` 类循环是否都有 `R3R_CHAIN_WALK_LIMIT`。

---

## 二、② 预留"销毁无主"

### 2.1 现状（已知）

- **产生侧有台账**：`R3RL` 块入档（第 245 轮 KI-371），`_r3r_resv_by_owner` / `_r3r_resv_owner_of_bit` 记录"哪位给哪格哪轨订了位"。
- **销毁侧无归属**：释放入口分散且不带 owner 信息——
  `UnreserveRailTrack()`（`pbs.cpp`）、`SetRailStationReservation(tile,false)`、
  `R3RReleaseChainReservations()`、`R3RReleaseReservationSafe()`、`R3RReconcileConsistReservation()`、
  `R3RPurgeStrayReservations()`、`R3RResvPhaseGuard()` 各条路径。
  ⇒ 台账只能在"事后对账"发现位消失，**不知道是谁、以什么理由释放的**；出现"位被谁清掉导致车卡住/幽灵位不清"时无法归因。

### 2.2 目标

1. 所有**改变地图预留位**的写操作集中到单一函数，并在其中做台账**同步删除**（消除"位没了但台账还在"）。
2. 每次释放写一行结构化日志：`RESV-REL who=<原因> veh=<释放者> tile=x,y track=n owner=<原台账归属> age=<tick> phase=<...>`。
3. 保留"释放前自证这格现在仍是我的预留"（rail_map.h:168 断言教训，记忆 87560799）。

### 2.3 待取证（下一步）

- 全仓库 `SetTrackReservation|SetRailStationReservation|UnreserveRailTrack|UnreserveTrack` 调用点清单（含 `pbs.cpp` / `rail_map.h` / `station_map.h` / `train_cmd.cpp` / `yapf_rail.cpp`）。
- 台账结构体与入档格式（`R3RL` 的 chunk 定义、`_r3r_resv_owner_of_bit` 的位编码）。
- 是否存在"引擎原生释放"路径本就在 R3R 控制之外（如 PBS 走路释放 `FreeTrainTrackReservation`）——这部分只能"事后对账"，需在方案里写清边界。

---

## 三、③ desync 独立日志

### 3.1 已知（第 247 轮 KI-377 取证结论）

- `-d desync=N` 有专门文件 `commands-out.log`，路径 `Subdirectory::Autosave`（典型 `%APPDATA%\OpenTTD\save\autosave\`），
  **以 `"wb"` 打开、每次启动覆盖**（`src/debug.cpp:118-123`）⇒ 天然不能满足"每次同步错误一份独立日志"。
- `-d command=N` 输出到 **stderr**，GUI 启动时直接丢。
- 命令环形缓冲在内存：`src/command.cpp` 的 `_command_log`（256 条）+ `_command_log_aux`（32 条），`AppendCommandLogEntry()` 无条件记录。

### 3.2 用户需求

「每次同步错误生成一份独立日志」——用于把"desync 导致被踢"和"垃圾网络/邀请码服务器"区分开。

### 3.3 候选方案（待定稿）

在 desync 判定点（`src/network/network_command.cpp` 的 sync 检查 / `NetworkSyncLog` 一带）新建一个**事件型文件日志**：

- 文件名：`desync-<UTC时间戳>-cli<clientid>.log`，落点 `Subdirectory::Autosave`（与 `commands-out.log` 同目录，便于一起取走）。
- 内容：帧号、本地/远端 sync 校验、client id、公司、`_pause_mode`、最近 256 条命令（复用 `LogCommandLog` 的格式化），以及 `Last sync record type`。
- 一次会话内每个 desync 事件一份新文件（不覆盖），并设上限（如 20 份）防刷盘。
- 同时在 `R3R_debug.log` 写一行 `DESYNC-LOG file=...`（Debug 探针版才写）。

### 3.4 待明确

- 「异常卡出去」还有一类是**连接超时/被主机踢**（非 desync）⇒ 需在 `NetworkClientSocket` 断开点也写一份 `netdrop-*.log`（原因码 + 最后 N 条命令），否则无法与 desync 区分。此项需玩家确认是否需要。

---

## 四、④ 发布版探针分离与性能

### 4.1 现状

- `R3R_PROBES_DEFAULT=0` 已在发布版 Defines 中，日志**字符串与 `if (R3RDbgOn())` 块**理论上被编译期消除。
- 但存在**非探针但调试性质**的常驻开销，这些在 Release 里照跑：
  - `R3RReservationAudit()` 全图 plain-rail 预留扫描（每 `R3R_RESV_AUDIT_TICKS` tick）；
  - `_r3r_consist_resv` 的 reconcile / GC；
  - `R3rResvPhaseGuard`、台账查询（`_r3r_resv_owner_of_bit` 可能在热路径）；
  - 各类 `std::unordered_map` 静态容器与每 tick 快照（如 `SPD-DROP` 的 map —— 已有 `if (R3RDbgOn())` 包裹）。
- ⇒ 用户"似乎没有特意做探针分离"的判断**方向正确**：探针确实关了，但**审计/台账/对账**没关。

### 4.2 方案方向

引入编译期开关 `R3R_DIAG_DEFAULT`（发布版 0 / 内测 1），把"只用于定位、不影响正确性"的部分分档：
- 必须保留（影响正确性/防 desync）：`R3RL` 台账读写、`R3DS` 解挂缝、段 ID、耦合节流。
- 可关闭（纯诊断）：全图审计扫描、GC 的日志输出、边沿状态表、per-tick 快照。
- 审计扫描建议即使保留也**降频**（如 2048 → 8192 tick），或改为仅在"脏标记"时跑（第 243 轮续十二已做过一次脏标记降频）。

### 4.3 待取证

- 统计发布版热路径上还剩下哪些 `R3R` 调用（用 `R3R_PROBES_DEFAULT=0` 预处理后看哪些块被保留）。
- 定量：在 Debug 版用 `R3R_PERF` 取 `audit/reconcile` 的 tick 占比，决定降频幅度。

---

## 五、执行顺序（本轮）

1. **（已完成）** 崩溃日志事实层取证 + 本备忘落盘 + KI-378~381 登记。
2. 崩溃栈符号化（§1.4 步骤 1）→ 结论回填 §1.3。
3. 预留释放侧调用点清单（§2.3）→ 方案定稿 → 实施。
4. desync 独立日志（§3.3）→ 实施。
5. 探针/诊断分档（§4.2）→ 实施。
6. 构建（复用既有 `_tmp_inc_build.cmd` / `R3R_release_fullbuild.cmd`，**不新建 .cmd**）→ 自证回填。

---

## 六、复测判据（草案，实施后细化）

| # | 判据 |
|---|---|
| C-1 | 同场景（veh 285 反向）不再 `C00000FD`；若仍能触发，日志出现新增的 `WALK-LIMIT` 类探针行 |
| C-2 | 反向操作后链序单调、`First()/Last()` 自洽，`CHAIN-ATTRS` 无环 |
| R-1 | 每次预留释放都有 `RESV-REL`，且台账中该位已删除（`RESV-AUDIT-SUMMARY strayTiles` 稳态为 0） |
| R-2 | 释放后 `rail_map.h:168` 断言零命中 |
| S-1 | 人为制造 desync 时，autosave 目录出现**新的一份** `desync-*.log`，内容含帧号与最近命令 |
| S-2 | 无 desync 的会话不产生该文件（零误报） |
| P-1 | 发布版 exe 内 `R3R_DIAG` 相关字符串消失（编译期删除自证） |
| P-2 | 同存档 A/B：发布版帧率不低于第 248 轮发布版（差距 < 3%） |

---

## 七、未确认项汇总

- U-1：崩溃栈 `[02]~[08]` 与 `[09]~[63]` 的符号（**阻塞 C-1 的根因定性**）。
- U-2：veh 285 当时的车辆状态（链长/段数/是否假双头/是否真铰接）——需存档或 Debug 版复现。
- U-3：`R3RL` 台账是否已覆盖"引擎原生释放"路径（`FreeTrainTrackReservation` 等）。
- U-4：`commands-out.log` 的实际绝对落点（KI-377 U-A-1 仍待实测）。
- U-5：玩家所说"性能损失"的具体体感来源（是否有卡顿点、是否与审计同 tick 相关）。

---

## 1.4af `SwitchToNextOrder()` 语义核实 —— `:14333` 的 while 是**有界**的（假说证伪）

**取证**（`src/train_cmd.cpp`）：
- `:13489-13538` `VehicleOrderSaver::SwitchToNextOrder(bool skip_first)`：`if (GetNumOrders() == 0) return false;` → `do { if (cur_real_order_index >= GetNumOrders()) cur_real_order_index = 0; ... } while (cur_real_order_index != old_index && depth < GetNumOrders());`
- `:13509-13519` 命中 `OT_GOTO_STATION / OT_GOTO_WAYPOINT / OT_GOTO_DEPOT / OT_GOTO_COUPLE` 时 `return UpdateOrderDest(...)`（真值）；其余（含 `OT_CONDITIONAL` 处理）`++cur_real_order_index; depth++;` 继续；**整数回绕受 `depth < GetNumOrders()` 与 `!= old_index` 双重限制**。

**结论**：`:13538` 之后是 `return false;`（本函数无其他出口），即**订单表末尾不回绕成无限循环**，最多绕一圈就返回 false。
⇒ `:14343 if (orders.SwitchToNextOrder(true))` 的 `else` 分支会 break 跳出 `:14333` 的 while。**`:14333` 每轮必消耗一次订单推进，总轮数 ≤ GetNumOrders()，故该 while 不是单 tick 卡死点。假说证伪（与本备忘 1.4ad 对递归爆炸的证伪同性质）。**

**顺带**：`:14333` 循环体每轮执行一次完整 `DoTrainPathfind`（`:14346`），订单多时会慢，但**不是无限**。

**下一目标**（仍未排除的"无日志、可无界"结构，按可疑度排序）：
1. `:14122 FollowTrainReservation()`（`src/pbs.cpp`）——沿已预留轨道前走；**若预留图本身是闭合环（环形掉头现场）且实现无步数上限，则单 tick 死循环**。与现场 `REVERSEDONE db=1` + 环形掉头 + 日志最后一行 TPR-WALK 之后零输出高度吻合。
2. `:14126 ExtendTrainReservation()`（`:13209` 起）——`while (ft.Follow(...))` 无计数器；若闭环上"自己的预留"被当作可继续（`TryReserveRailTrack` 对自占位返回 true），可能绕环不止。
3. 两者都不成立时，再查 `DoTrainPathfind`/YAPF 的 R3R 闭包（`yapf_rail.cpp`）。

---

## 1.4ag **根因定稿**：`ExtendTrainReservation()` 在闭合环上无限循环（KI-361 引入的回归）

### 一、排除前两个候选（都有界）
- `FollowTrainReservation`（`src/pbs.cpp`）自带环路检测：`bool first_loop = true;` + 回到起点 `(tile, start_td)` 即 break。且现场 `RESV-LEDGER-FORGET veh=0 why=free-path mask=0x1 released=17 kept=0` 说明预留**刚被整体释放**、`look=0`（无 lookahead）⇒ 它第一步就返回，不可能是挂点。
- `ExtendTrainReservation` 的**回滚循环**（`:13380 while (tile != stopped || cur_td != stopped_td)`）沿前进路径倒着走回 `stopped`，而 `stopped` 必在环上 ⇒ 有界。

### 二、真凶：主循环 `:13209 while (ft.Follow(tile, cur_td))` 在本场景**没有终止条件**
逐条对照（`src/train_cmd.cpp`）：
| 退出条件 | 本场景是否成立 |
|---|---|
| `:13210-13213` 单向信号挡路 | 环形掉头段**无信号** ⇒ 不成立 |
| `:13215-13218` 90° 禁转致无后继 | 环上是正常曲线 ⇒ 不成立 |
| `:13222` `target_seen`（站/库/路点/非 plain rail）或岔路口（`KillFirstBit(new_td_bits) != NONE`） | 纯闭环无站无库无路点，环上每格单后继 ⇒ 不成立 |
| `:13302` `IsSafeWaitingPosition()`（安全等待位＝**信号**前的位） | 无信号 ⇒ 恒 false |
| `:13342` `TryReserveRailTrackdir()` **失败** ⇒ `reason="reserve-fail"; break;` | **以前成立、现在不成立**（见下） |

### 三、为它为什么"以前有界、现在无界"——KI-361
- 底层 `TryReserveTrack()`（`src/rail_map.h:267-277`）对已订位返回 **false**：
  ```cpp
  TrackBits res = GetRailReservationTrackBits(tile);
  if ((res & bits) != TRACK_BIT_NONE) return false;  // already reserved
  ```
  ⇒ 走完第一圈回到起点时，`:13342` 必然 false ⇒ `break` ⇒ 一圈后回滚、`MarkTrainAsStuck`。**这就是玩家最早报的"列车环形掉头后对着空旷轨道停住"的机制**（本备忘前几轮把它误归到"512 上限"，见 1.4z 的自我纠正）。
- **但** 2026-10-16 的 KI-361 在 `TryReserveRailTrackdir()`（`src/pbs.cpp:810-816`）加了一条：
  ```cpp
  if (!success && v != nullptr && IsPlainRailTile(tile)) {
      const Train *const booker = R3RTrainForLedgerBit(tile, track);
      if (booker != nullptr && booker->index == v->First()->index &&
              HasBit(GetRailReservationTrackBits(tile), track)) {
          success = true;   // 当成"已订成功"
      }
  }
  ```
  ⇒ 第二圈每一格都变成"成功"（那些位是第一圈自己订的、台账 owner 就是自己）⇒ `:13342` 永不 break ⇒ **`while (ft.Follow(...))` 绕环无限**。
  KI-361 的本意是修挂车寻路 `rp()` 的 track 级/ trackdir 级重复预订，代价是顺手拆掉了 `ExtendTrainReservation` 原来借"重复预订必然失败"实现的**隐式一圈上界**。

### 四、与现场逐条吻合
1. 日志最后一行 = `TPR-WALK ... okay=0 exit=3 new=122,499 reach=2 spd=0`（`:14821` 打印），紧接 `:14839 ChooseTrainTrack(...CTTF_FORCE_RES...)` → `:14126 res_dest = ExtendTrainReservation(...)`（`do_track_reservation=true`、`origin.okay=0` 故必然进主循环）。
2. 循环体内**没有任何日志**（只有 break/return 后才打 `EXTEND-BACKOUT`）⇒ 日志在 TPR-WALK 之后**零输出**、文件不暴涨（446 行、32 846 B，18:14:00）。
3. 单线程无限循环 = 窗口冻结、进程不退、单核 100%，与"游戏只是卡死"逐字吻合。
4. 与 KI-217（db 混合刷屏）无关（已于 1.4ac/1.4ad 用 `LR-*` 计数 0 排除）。

### 五、修法（本轮落地）
1. **环闭合检测（主防，O(1) 内存）**：在 `:13299-13300` 推进到下一格后，若 `tile == origin.tile`（走到出发格 ⇒ 绕回一圈，合法路径不可能重复经过同一格），即刻**交棒**给路径规划器而不是"失败"：
   `return PBSTileInfo(tile, cur_td, false);`（`okay=false` = "路有效但非安全等待位"，与 `:13294-13296` 的岔路口分支同构；已订的一圈保留），并写探针 `LOOP-CLOSED veh= tile= td= steps=`。
   —— 为什么不 `break`：`break` 会走回滚 + `MarkTrainAsStuck`（`:14129`），那正是玩家抱怨的"对着空旷轨道卡死"。交棒后由 `DoTrainPathfind` 按订单目的地继续，能走就走、不能走才落到既有的失败路径。
2. **步数兜底（不可能误伤合法路径）**：`tiles_skipped`（隧道/桥/长站台跟随器一步跨多格）会让走线跳过 `origin.tile` 从而漏检，故加计数器，上限取 `2 * Map::Size()`：合法扩展每格至多 2 条 track 且不会重复预订，**任何合法路径都不可能超过 2×地图格数**，所以该上限只可能被环触发，不会像"512 格上限"那样把长直线提前掐断（用户已否掉的方案，不再采用）。
3. 不做：不给 `ExtendTrainReservation` 加"格数上限"类常量（512/4096），不撤 KI-361 的 own-bit-success（它修的是另一个真实问题，撤了会复发）。

**待验证判据**（下次复跑）：①不再整机冻结、日志继续增长；②出现 `LOOP-CLOSED veh=0`（或 `steps` 达上限的 `WALK-CAP`）恰一次而非每 tick；③该列车随即出现 `CRT`/`RESV` 重订并驶离，或明确 `Stuck`（而不是冻死）；④无 `rail_map.h:168` / `pbs.cpp` 断言。

### 六、构建自证（第 249 轮，2026-10-19）
- 复用既有 `_tmp_inc_build.cmd`（**未新建任何 `.cmd`**）：`GUARD: incremental is safe (no header/lang file is newer than the newest object)`。
- `build\R3R_incbuild.log:520` = `[3/3] Linking CXX executable openttd.exe`；无 `error C` / `FAILED:`。
- `build\R3R_incbuild.done` = `EXIT_CODE=0`（@19:26:46 第一次 / @19:32:2x 第二次，两次都是警告级 0 错误）。
- 时间戳链（**最终版**，第二次增量把 `return` 的 trackdir 从 `cur_td` 改成 `ft.old_td` 以与岔路口分支同构）：`src\train_cmd.cpp` 19:28:10 → `build\CMakeFiles\openttd_lib.dir\src\train_cmd.cpp.obj` 19:29:20 → `build\openttd.exe` 19:32:23（51 891 712 B，`[3/3] Linking`、`EXIT_CODE=0`）。
- `read_lints` `src\train_cmd.cpp` = 0 条；exe 字面量命中 `LOOP-CLOSED`。
- 本轮只编内测（探针 ON）树，**发布树 `build-release` 未动**，停留在第 248 轮那一版（不含 KI-382）。

### 七、改动清单（可审计）
`src\train_cmd.cpp` 两处：
1. `R3REDGE_*` 枚举在 `R3REDGE_REDSIG` 之后新增 `R3REDGE_WALKLOOP`（在 `R3REDGE_COUNT` 之前）。
2. `ExtendTrainReservation()`：循环前声明 `uint32_t r3r_walk_steps = 0; const uint32_t r3r_walk_limit = Map::Size() * 2;` + 大段说明注释；循环内 `cur_td = FindFirstTrackdir(ft.new_td_bits);` 之后插入环闭合/步数判定，命中时设出参并 `return PBSTileInfo(tile, cur_td, false)` + `LOOP-CLOSED` 探针。

未改：`pbs.cpp`（KI-361 的 own-bit-success 保留）、任何 `src\*.h`、任何语言文件。
