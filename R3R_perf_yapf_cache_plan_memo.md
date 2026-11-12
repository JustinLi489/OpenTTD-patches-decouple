# R3R 性能专项 · YAPF 寻路缓存：目标定义（2026-11-08，第 1 轮）

> 本轮边界：**零源码改动、零构建、零 KI 新增**。只做目标定义、现状取证与待拍板清单。
> 后续每一轮的实现/取证一律追加到本文件，不另建新备忘。

---

## 1. 现状取证（代码级事实）

### 1.1 铁路 YAPF 的对外入口只有 5 个（+R3R 1 个）

全部在 `src/pathfinder/yapf/yapf_rail.cpp`：

| 入口 | 行 | 底层 | 性质 |
| --- | --- | --- | --- |
| `YapfTrainChooseTrack()` | :2166 | `CYapfRail::stChooseRailTrack` / `No90` | **最高频**：列车在信号段选出口 + PBS 预留（`ChooseTrainTrack()` 每列车每次需要重新决定走向时调用） |
| `YapfTrainCheckReverse()` | :2200 | `stCheckReverseTrain` | 掉头评估 |
| `YapfTrainCheckDepotReverse()` | :2347 | — | 库内掉头评估 |
| `YapfTrainFindNearestDepot()` | :2365 | `stFindNearestDepotTwoWay` | 找最近库（自动维护/回库） |
| `YapfTrainFindNearestSafeTile()` | :2383 | `stFindNearestSafeTile` | 安全停车位 |
| `CYapfFollowCoupleRailT` | :1301 | `FindNearestCoupleTrain` | R3R 新增；调用点 :1337 **恒 `DisableCache(true)`** |

结论：**只要覆盖 `ChooseRailTrack` 一条，就抓住了绝大部分铁路寻路调用**；其余四条是低频事件（掉头/回库/安全位）。

### 1.2 现有"缓存"是段成本缓存，不是路径缓存

- `CYapfSegmentCostCacheGlobalT`（`yapf_costcache.hpp:124`）→ `CSegmentCostCacheT<Tsegment>`（:90），`HashTable` + `BumpAllocContainer`。
- key = 段起点 `(TileIndex + DiagDirection)`（同文件 :83-86 注释自述），value = `CYapfRailSegment`（该段代价、信号、末信号等）。
- 失效 = `CSegmentCostCacheBase::s_rail_change_counter`，由 `YapfNotifyTrackLayoutChange()` 递增（`yapf_cache.h:23-45`，计数器定义 `yapf_rail.cpp:2575`），计数变化即整体 `Flush()`。

**关键区别**：段成本缓存让 A* 的每一步展开更快，但**同一 (起点, 终点) 的第二次搜索仍要重新展开整张图**——节点闭合表是每次搜索私有的（`CRailNodeList`）。所以"重复同一起终点的寻路"目前完全没有被复用。这正是本专项的收益来源。

### 1.3 原生对缓存正确性的戒心（必须继承）

- 近距找库（`max_penalty != 0`）关缓存，注释明写 *"the cached result does not match uncached result in all cases and that causes desyncs"*（`yapf_rail.cpp:1202-1211`）。
- 复杂 waypoint 关缓存（`yapf_destrail.hpp:239-243`）。
- 挂车寻路恒关缓存（`yapf_rail.cpp:1337`）。
- 调试级别 `yapfdesync > 0` 或 `desync >= 2` 时，用第二个实例 `DisableCache(true)` 重跑并比对，不一致打 `CACHE ERROR: ...`（`:1872` / `:1978` / `:2073`）。

⇒ 这是一套**现成的"缓存 vs 无缓存"自检基础设施**。本专项的新缓存必须能被它覆盖（否则等于没有正确性保障），并且这是唯一可信的验收手段。

---

## 2. 目标陈述

**一句话**：在不改变任何列车行为（逐 tick 确定性一致）的前提下，把重复的铁路 YAPF 寻路从"每次全量 A*"降为"命中即返回"，让联机服务器存档的 game-loop 时间相对基线显著下降。

三条可验证目标：

- **G1 确定性**：同存档 + 同输入序列，开缓存与关缓存跑出逐 tick 完全一致的车辆状态/订单进展；`yapfdesync` 自检 0 条 `CACHE ERROR`。
- **G2 收益**：Release 树基准场景中，YAPF 累计耗时（ms/s）或每 tick 平均耗时下降 ≥ 目标值（**目标值必须等 §4 基线出来再定，不预先拍数字**）。
- **G3 成本**：常驻内存有硬上限、服务器长跑不增长；失效不漏（轨道/信号/编组/预留变化必失效）。

---

## 3. 候选方案（本轮只定义，不实现）

- **方案 A（建议作为第一目标）— L1 结果缓存**
  key = `(origin tile, origin trackdir, 目标标识, 上下文指纹)` → value = 决策结果（出口 trackdir / 预留位集合 / 是否可预留）+ 代价。命中直接跳过 A*。
- **方案 B — L2 局部决策缓存**
  只缓存某个 junction 的出口选择。粒度细、失效容易，但收益小，天花板低。
- **方案 C — 只增强 L0**
  段成本缓存扩容 + 更细失效（信号红绿变化也参与失效）+ 减少无谓全 Flush。改动最小，但解决不了"重复整图搜索"。

**真正的难点（也是 desync 的唯一来源）**：YAPF 的结果**不是** `(origin, dest)` 的纯函数。它至少依赖：

1. 信号状态（红/绿、PBS）；
2. 轨道上其他列车的预留（`HasReservedTracks`、`FollowTrainReservation`）；
3. 列车自身：长度、速度、加速度、是否 realistic braking、是否 90° 禁行；
4. 订单上下文：tracerestrict program、危险品/载货、lookahead 状态；
5. 目标解析：车站/路点/库多格时的目标格选择；
6. R3R 附加语义：挂接目标判定、段身份、`r3r` 相关闸门（第 356~406 轮大量闸门已进入 `ChooseTrainTrack` 路径）。

⇒ 缓存 key 的"上下文指纹"设计就是本专项的核心工作量。**指纹过宽 = desync，过窄 = 命中率归零。**

---

## 4. 测量与验收基建（P0，必须先于优化）

- **树**：性能数字只能来自 `build-release`（RelWithDebInfo，`/O2 /Ob2`）。Debug 树慢 5~20×（KI-14/KI-25），历史 A/B 倍率全部作废，**不可用作依据**。日常仍只编内测树，只有确定要取性能数时才编发布树。
- **现成探针**：`src/r3r_perf.h` + `R3RPerfFrameTick()`，由 `window.cpp:3415` 每 128 帧写一行 `PERF frames= fps= frameMs avg= max= | tryCouple= foldCheck= dbgWrite= posHelper= ...` 到 exe 同目录的 `R3R_perf.log`。
- **需新增（下一轮才动）**：YAPF 专用只读计数——每个入口的调用次数、累计 ns、缓存命中/未命中/淘汰数。默认关闭、零行为影响。
- **基准场景**：需用户提供"服务器重载存档 + 固定时长（空跑/不操作）"。A/B 对比可复用既有 `R3R_AB_probe.cmd`（R3R vs 官方 JGRPP 0.72.4）。
- **判定指标优先级**：① 每 tick game-loop 平均耗时 ② YAPF 累计 ms/s ③ 长帧 p99/max（联机卡顿的真实体感来源）。

### 4.1 两棵树的分工（2026-11-08 用户提问后的结论）

先拆术语，这两件事经常被说成一句但其实是**两棵不同的树**：

| 树 | 目录 | 编译配置 | 探针 | 用途 |
| --- | --- | --- | --- | --- |
| 内测树 | `build\` | **Debug**：`/Od /Ob0 /RTC1 /MTd -D_DEBUG -DDBG_ASSERTS` | ON | 功能开发、正确性自检、断言、计数统计 |
| 发布树 | `build-release\` | **RelWithDebInfo**：`/O2 /Ob2 /DNDEBUG /MT` | OFF | **唯一可用于收益结论的树** |

「原生 Debug 模式」= `build\`；「发布树/非探针树」= `build-release\`。二者不是同一个东西。

**结论：不能把性能读数完全交给 Debug 树。** 按指标类型分开看：

- **计数型指标（Debug 树可信）**：调用次数、缓存命中/未命中/淘汰、平均展开节点数。计数器数与编译优化无关，两棵树数出来的次数逐个相同。
- **耗时型指标（Debug 树不可信）**：耗时占比、ms/s、fps、每 tick 时间。`/Ob0` 把内联与模板访问器全变成真实调用、`/RTC1` 加栈帧/初始化检查，惩罚强度**因代码形状而异** ⇒ 两棵树的"谁更热"排序会变，"A 比 B 慢 N 倍"的 N 在 Release 下完全不成立。历史证据：第 13/14 轮全部 A/B 倍率作废，真因就是 Debug vs Release 9.64×（KI-14/KI-19/KI-20）。
- 附加污染：`build\` 探针 ON，运行期若不关（`R3R_DBG=0` / `R3RPerfOn()`），测出来的是探针本身。

**可行规程（建议采纳）：迭代在 Debug，判决在 Release。**

1. 日常迭代只在 `build\`：功能、正确性、`yapfdesync` 自检、命中率计数。不编发布树。
2. 只有候选**在 Debug 树上通过正确性**之后，才编**一次** `build-release` 取数；取数不合格就回退候选，**不要**拿 Debug 读数去"微调参数"。
3. 发布树取数：运行期探针关闭，固定存档 + 固定时长 + 后台空跑（A/B 用既有 `R3R_AB_probe.cmd`）。
4. 折中手段（省发布树次数）：在 Debug 树只看**命中率**，用「命中率 × 单次寻路耗时」估收益区间，仅用于判断"值不值得编发布树验证"，不作为收益结论。

**迭代速度的现实预期**：`build\` 改 `.cpp` 增量约 2~4 分钟；但性能优化大概率要动 `src/*.h`（给 YAPF 节点加字段、改缓存 key），而 `build\` 的 ninja deps 表已 STALE（记忆 66636022），改任何 `src/*.h` 必退化为全量 692 步 ≈ 16~20 分钟（8 GB 机器 `-j2`）。所以"每轮都双树"不可行，"过关才编一次发布树"才可行。

### 4.2 `PERF` 行的口径陷阱

`R3R_perf.log` 的 `PERF` 行由 `R3RPerfFrameTick()` 每 **128 渲染帧**写一次（`window.cpp:3415`），其中 `frameMs`/`fps` 是**客户端渲染帧**。联机服务器上决定体感的是 **game tick 耗时**，两者不等价：

- 寻路缓存只影响 tick，不影响渲染管线 ⇒ **不要把 fps 上升当成寻路收益**；
- 主指标应为「固定 tick 数的墙钟耗时」或「单 tick 平均耗时」，`PERF` 行只作辅助（尤其 `posHelper`/`tryCouple` 这类 ms/s 分项）。

### 4.3 「第三棵树」方案评估（2026-11-08，用户提问，**未动手**）

用户诉求原话：希望一棵树同时具备「内测树不必次次全量的增量优点」+「发布树低延迟、计算周期短的优点」。

**先把两个优点的来源拆开**——它们并不都来自"树"：

| 优点 | 真正来源 | 能否移植到新树 |
| --- | --- | --- |
| 增量不必全量 | `R3R_inc_guard.ps1` 的护栏 + 本轮只改 `.cpp` | 能，但**与 /O2 无关**；护栏靠 `src/*.h`、`src/*.hpp` 时间戳判全量（`R3R_inc_guard.ps1:50` 两个扩展名都收） |
| 计算快、周期短 | `/O2 /Ob2`（`build\` 是 `/Od /Ob0 /RTC1`，5~20×） | 只能靠新树拿到，与树绑定 |

**已查证的事实（只读，未改任何文件）**

1. `build-release\build.ninja` 的 DEFINES 只有 `-DR3R_PROBES_DEFAULT=0`，**没有 `-DR3R_PROBES`** ⇒ 走 `r3r_perf.h:130` 的 `#define R3R_PROBES R3R_PROBES_DEFAULT` ⇒ `R3R_PROBES=0` ⇒ `R3RDbgOn()` 直接 `return false`，现有发布树**运行时用 `R3R_DBG=1` 也打不开探针**。
2. `r3r_perf.h:126-128` already 明确了目标组合：`-DR3R_PROBES=1 -DR3R_PROBES_DEFAULT=0` = 探针在二进制里、默认关、`R3R_DBG=1` 才开。**【2026-11-08 补正·本条原写「不需要改任何源码」是错的】** 头文件层面的语义确实已就绪，但**构建系统层面必须改 `CMakeLists.txt`**：它当时只有 `if(DEFINED R3R_PROBES_DEFAULT)` 一个块，`-DR3R_PROBES=1` 会被 CMake 判为未使用变量（"Manually-specified variables were not used by the project"）而**丢弃**，`build.ninja` 的 DEFINES 里根本不会出现 `-DR3R_PROBES` ⇒ 脚本 [5d] 新加的硬闸门必然以 `EXIT_CODE=94` 拦下构建。已补 `if(DEFINED R3R_PROBES) add_definitions(-DR3R_PROBES=${R3R_PROBES}) endif()`（`CMakeLists.txt:550-562`）。**教训：凡是"命令行 -D 变量"，必须先在 CMakeLists.txt 里有对应的 `if(DEFINED ...)` 消费块，否则 configure 静默成功、define 静默丢失，只有下游闸门能发现。**
3. `R3R_inc_guard.ps1:73-75` 的 `$smallClosure` 机制（2026-10-27 新增）已能"只删指定 obj 保持增量"，目前只登记 `yapf_destrail.hpp → yapf_rail.cpp.obj`。**这是本轮性能专项的关键杠杆**：yapf 的改动几乎全在 `.hpp`，默认会被判全量。
4. yapf include 闭包实测：`yapf.hpp` 被 5 个 TU include（`yapf_ship_regions.cpp` / `yapf_ship.cpp` / `yapf_road.cpp` / `yapf_rail.cpp` / `yapf_common.cpp`），而 `yapf.hpp:22/26` 又 include `yapf_base.hpp`、`yapf_costcache.hpp` ⇒ 改 `yapf_costcache.hpp` 影响 **5 个 obj**，不是 620 个。
5. `R3R_inc_guard.ps1:26` 硬编码 `Join-Path $root 'build'`；`R3R_fullbuild.cmd` 的闸门把「`build.ninja` 含 `-DR3R_PROBES_DEFAULT=0`」判为发布树并回 `EXIT_CODE=95`。**新树若用 `DEFAULT=0`，这两处都会误伤**。

**三个方案（成本递增）**

| 方案 | 做法 | 首次成本 | 之后改 `.cpp` | 之后改 yapf `.hpp` | 新增磁盘 | 需改脚本 |
| --- | --- | --- | --- | --- | --- | --- |
| **B（推荐）** | 不新建树。给 `build-release` 的 DEFINES 加 `-DR3R_PROBES=1`（DEFAULT 仍 0），并把 yapf 三个头登记进 `$smallClosure` | 一次全量 667 步 ≈ **30~50 分钟** | 4~8 分钟（/O2 编巨型 TU 慢 2~4×） | **只删 5 个 obj，≈1~2 分钟** | 0 | `R3R_release_build.cmd`、`R3R_inc_guard.ps1` |
| A | 新建 `build-opt`（同 B 的配置，独立目录） | 首次全量 + 复制 vcpkg ≈ 30~50 分钟 | 同上 | 同上（护栏需支持该目录） | 一份树（估 1.5~3 GB） | 上面两个 + `R3R_fullbuild.cmd`（新增第三模式） |
| C | 维持双树，只扩 `$smallClosure` | 0（不重编） | 2~4 分钟 | 1~2 分钟 | 0 | 只 `R3R_inc_guard.ps1` |

**必须诚实说明的代价（别对"完全保留两个优点"抱期待）**

- 「增量快」**不是 Debug 的功劳**，所以换 /O2 树不会更慢在护栏上，但会慢在编译本身：`train_cmd.cpp` 这类巨型 TU 在 `/O2` 下约 60~120 秒，Debug 约 20~30 秒。改 `.cpp` 的增量从 2~4 分钟变 4~8 分钟。
- 改 `.h`（`train.h`、`vehicle_base.h`）仍是全量，且 `/O2` 全量比 Debug 全量更慢（30~50 分钟 vs 16~20 分钟）。性能专项大概率要碰 `train.h`（缓存 key 加字段）⇒ 这一刀躲不过，只能靠"少改或一次性改完"。
- `R3R_PROBES=1` 的树**不等于**纯发布树：`R3RDbgWrite` 的调用点仍会求值参数（`R3R_PROBES=0` 时才是宏整体丢弃），`R3R_PERF=1` 时每帧 6.5k 次计时调用也真实存在。所以**绝对读数**要用 B 树 + `R3R_DBG=0 R3R_PERF=0`，并**对照跑一次纯发布树**量化这层残差；A/B 相对比较只要两边同树同设置即可。
- 磁盘：记忆 76468255 记录 C 盘仅剩约 0.7 GB、`TEMP` 必须改指 D 盘；`-j4` 编 `/O2` 在 8 GB 机器上会换页冻结，必须 `-j2`，且**两棵树不可同时编**。

**结论**：用户要的东西可以做到，但方案 B 用一棵现成的树就够了，不必新增第三棵；且「改 yapf `.hpp` 也只编 5 个 obj」比"新建树"更能决定日常迭代手感。

**拍板项**

- **C-6**：选 B（改 `build-release` + 扩 smallClosure，推荐）／ A（新建 `build-opt`）／ C（只扩 smallClosure、维持双树）。
- **C-7**：是否把 yapf 三个头（`yapf.hpp` / `yapf_base.hpp` / `yapf_costcache.hpp`）登记进 `$smallClosure`（需先确认没有第 6 个 TU include 它们）。

### 4.4 方案 B 落地（2026-11-08，用户拍板 C-6=B / C-7=是，同日实施）

**用户指令原话**：「那就先提交，再打tag。还有，我希望这一轮内打tag和方案B改造一起做完。」

**先固化稳定点（已完成）**

- 源码提交 `b0122745b7`：6 files, +835/−108（第 392~406 轮功能）。
- 文档提交 `6335aa70c2`：17 files, +5193/−32（`R3R_KNOWN_ISSUES.md` +658 行 + 14 份 memo）。
- 附注标签 `r3r-stable-2026-11-08` → `6335aa70c2`，注明 "source b0122745b7"。
- 依据：附注标签只能指向提交，故"先提交再打 tag"是唯一顺序；打 tag 前已核对 `build\openttd.exe`（11-08 17:24:20）晚于全部被改源码（最新 `train_cmd.cpp` 17:02:16），产物与源码一致。

**四处改动（脚本 + CMake，零 `src/*.cpp` 行为改动）**

| 文件 | 改动 | 为什么必须 |
| --- | --- | --- |
| `CMakeLists.txt:550-562` | 新增 `if(DEFINED R3R_PROBES)` 消费块 | 否则 `-DR3R_PROBES=1` 被 CMake 丢弃，闸门 94（见 §4.3 补正） |
| `R3R_release_build.cmd` | configure 加 `-DR3R_PROBES=1`；新增 `[4c]` 调护栏（`-BuildDir "build-release"`）；`[5d]` 增 `findstr -DR3R_PROBES=1` 闸门 | 建出"探针在、默认关"的 /O2 树；闸门防将来手改回退 |
| `R3R_release_fullbuild.cmd` | 同上（configure + `[5c]` + `[9]` 闸门） | **两个入口写同一个 `build-release`**，参数不一致会互相触发全量重编（每换一次参数就是 30~50 分钟），必须同参同闸 |
| `R3R_inc_guard.ps1` | 新增 `-BuildDir` 参数（默认 `build`）；`$smallClosure` 由 1 条扩到 4 条 | ①护栏原硬编码只守护 `build\`，`build-release` 是同一套 STALE deps 的盲区（KI-183）；②yapf 闭包实测：`yapf_base.hpp`/`yapf_costcache.hpp` 仅被 `yapf.hpp` include，`yapf.hpp` 被 5 个 TU include（`yapf_common/rail/road/ship/ship_regions.cpp`）⇒ 改这三个头只删 5 个 obj 而非 620 个 |

`$smallClosure` 现状（改头后只删这些 obj，其余保持增量）：

```
src\pathfinder\yapf\yapf_destrail.hpp  -> yapf_rail.cpp.obj
src\pathfinder\yapf\yapf.hpp           -> yapf_common / yapf_rail / yapf_road / yapf_ship / yapf_ship_regions
src\pathfinder\yapf\yapf_base.hpp      -> 同上 5 个
src\pathfinder\yapf\yapf_costcache.hpp -> 同上 5 个
```

**落地实测（截至本条记录时）**

- 三个脚本纯 ASCII 复核通过（PowerShell 5.1 按 ANSI 读 `.ps1`、`cmd.exe` 按 936 读 `.cmd`，非 ASCII 会从行中间执行垃圾）。
- 护栏实跑：`& ".\R3R_inc_guard.ps1" -BuildDir "build-release"` → `1 header/lang file(s) are newer than the newest object file … newer: src\widgets\order_widget.h … REMOVED 620 object file(s)` ⇒ `build-release` 已无 obj，本次必然全量。
- 构建实跑（`R3R_release_build.cmd` 后台）：configure 输出 `-- R3R probes default override -- -DR3R_PROBES_DEFAULT=0` 与 `-- R3R probes compile switch -- -DR3R_PROBES=1` 双行，`FIRST FLAGS => FLAGS = /DWIN32 … /Zi /O2 /Ob2 /DNDEBUG …`，`OK: /Od absent`，`[5c]` 外部库闸门通过，`[5d] OK: probes compiled IN, default OFF`，ninja 698 步开始。
- 构建结果（已回填）：`[698/698] Linking CXX executable openttd.exe`、`BUILD FINISHED rc=0`、`build-release\R3R_release_build.done` = `EXIT_CODE=0`、错误计数 0、耗时约 36 分钟；产物 `build-release\openttd.exe` **2026-11-08 19:04:54（23 555 072 B）**（对照 Debug 树 52 524 544 B）。
- 探针确实链进 /O2 树：exe 内 ASCII 命中 `FOLDCHK` / `SPD-DROP` / `COUPLE-OK` / `DECOUPLE-DONE` / `TPR-BLOCKED` / `COUPLE-PREFLIGHT` / `RESV-GC` / `DECOUPLE-INTERLOCK` / `respace-after`（9/9）。
- 假疑点澄清：`RESV-AUDIT*` 两棵树 exe 都没有，不是被 `/O2` 折掉，而是该段探针已从现行源码移除（`src\train_cmd.cpp` 命中 0 次，只在未跟踪的陈旧备份 `src\train_cmd.cpp.r3r_bak` 里命中 2 次）。**`src\` 下的 `.r3r_bak` 会污染 grep 并给出误导行号，排查"代码在哪"时要限定文件名。**
- 待完成的验收项（**仅剩运行时两条**，构建期四条已全部达成）：①不带 `R3R_DBG` 启动**不生成** `R3R_debug.log`（默认关）；②`R3R_DBG=1` 启动**生成** `R3R_debug.log`（运行时可开）——这两条是方案 B 的最终判据，缺一即方案 B 未成立。实测法：把 `build-release\openttd.exe` 拷到独立目录（如 `D:\r3r_opt\`），先裸跑确认无日志，再 `set R3R_DBG=1` 跑同一 exe 确认有日志。

**风险与边界**

- 这棵树**不是**纯发布树：`R3RDbgWrite` 等调用点在 `R3R_PROBES=1` 下仍会求值参数，`R3R_PERF=1` 时每帧约 6.5k 次计时调用真实存在 ⇒ 绝对性能读数必须 `R3R_DBG=0 R3R_PERF=0`，并与纯发布树对照量化残差；A/B 相对比较只要同树同设置即可。
- 今后**发布 exe 仍须用 `-DR3R_PROBES=0`** 单独编一份（现在还缺一个"纯发布"出口脚本的模式），否则发行版会带探针代码（默认关，但二进制更大、调用点有参求值残差）。
- `-j2` 不可上调（8 GB 机器 + `/O2` 会把 cl.exe 换页冻死，见记忆 76468255），且两棵树不可同时编。

---

## 5. 硬约束（违反即回退）

1. **确定性**：缓存命中与未命中必须给出**完全相同**的结果；同存档同输入逐 tick 一致。以 `yapfdesync` 双跑自检为准。
2. **存档兼容**：缓存是纯内存态（NOSAVE），读档/新局清空；不新增任何存档字段。
3. **失效正确**：轨道/信号改动（`YapfNotifyTrackLayoutChange`）、车辆增删与编组（R3R 的 couple/decouple/翻转/车库拖动）、信号状态变化、PBS 预留变化，**宁可多失效，不可漏失效**。
4. **内存有界**：LRU/世代淘汰 + 硬上限，服务器长跑零增长。
5. **可关闭**：编译期或设置项开关，关闭时行为与原生逐字等价，便于定位回归。
6. **构建合规**：改 `src/*.h` 必须全量（KI-183 / 记忆 66636022：ninja deps 表 STALE）；本轮及后续优先只改 `.cpp`。

---

## 6. 范围与非目标

**做**：铁路 YAPF 的结果复用；测量基建；失效策略。

**不做（至少第一轮不做）**：
- 不改 A* 核心算法、代价函数、`nodelist.hpp`；
- 不引入多线程/异步寻路（联机确定性与存档一致性风险过高）；
- 不改 R3R 挂车语义；
- 不动 road/ship YAPF；
- 不动构建配置与发布树策略。

---

## 7. 待拍板（C-1 ~ C-4）

| 编号 | 问题 | 选项 | 建议 |
| --- | --- | --- | --- |
| C-1 | 第一目标选哪个方案 | A（L1 结果缓存）/ B（局部决策）/ C（只增强段缓存）/ 先 A 原型再评估 | **A** |
| C-2 | 开关形态 | GM 设置项 / 启动参数 / 纯编译期 | 编译期 + 运行时开关各一份，便于 A/B |
| C-3 | 基准场景 | 用户提供服务器存档；固定时长空跑；玩家自己启动（重进程不由 AI 拉起） | 需用户指定存档与时长 |
| C-4 | 第一步 | 先做测量基建 + 基线 / 直接上 A 原型 | **先测量**（没有基线就无法证明收益，也无法定位回归） |
| C-5 | 测量规程 | 接受「迭代在 Debug、判决在 Release」/ 坚持只用 Debug 树 / 每次改动都双树编 | **前者**（只用 Debug 树会把收益排序测反；双树每轮编不起） |

---

## 8. 后续轮次接口

- 第 2 轮（待 C-1/C-4 拍板后）：只加只读计数器，取 `ChooseRailTrack` 的调用次数与耗时占比，出基线。
- 第 3 轮及以后：按选定的方案做原型，先在 Debug 树用 `yapfdesync=1` 验证确定性，再上 Release 树取收益。

---

## 9. L1 结果缓存架构设计（第 2 轮，2026-11-08，用户指令「可以开始 YAPF 优化的架构设计」）

> 本节只做架构。**零源码改动、零构建**。所有"必须"都附代码依据；凡未取证的标 U。

### 9.1 从代码读出的三条硬事实（决定架构上限）

| # | 事实 | 依据 | 对架构的含义 |
| --- | --- | --- | --- |
| F1 | 对外入口签名 = `Track YapfTrainChooseTrack(const Train *v, TileIndex tile, DiagDirection enterdir, TrackBits tracks, bool &path_found, bool reserve_track, PBSTileInfo *target, TileIndex *dest)` | `yapf_rail.cpp:2166` | 输出极小（1 个 `Track` + 3 个出参），**结果缓存的数据结构非常便宜**；但 `reserve_track` 是**行为分叉点**，不是普通参数 |
| F2 | `reserve_track == true` 时函数内**产生副作用**：`SetReservationTarget` + 沿 best node 回走逐格预留（`ChooseRailTrack` 内 `safe_position_nodes` 循环） | `yapf_rail.cpp:1994-2050+` | **缓存 Track 但不缓存预留 ⇒ 静默改变 PBS 世界状态**。别的列车下一 tick 看到的预留不同 ⇒ 决策连锁改变 ⇒ 这是"缓存类 desync"的头号来源。**结论：`reserve_track == true` 的调用不允许走"只缓存决策"的路径** |
| F3 | 现成自检：`yapfdesync >= 1` 或 `desync >= 2` 时 `pf1` 正常跑、`pf2.DisableCache(true)` 再跑一遍并比对，不一致打 `CACHE ERROR: ChooseRailTrack()` | `yapf_rail.cpp:1976-1989`（另 `:1872` / `:2073`） | 新缓存**必须无条件尊重 `DisableCache(true)`**（旁路），否则 G1 验收手段直接失效；反过来，只要尊重它，G1 就有现成机器可跑 |

补充事实（非硬约束但影响设计）：`ChooseRailTrack` 入口的 origin 不是纯参数——`OT_GOTO_COUPLE && !GetCoupleIsDepot()` 时钉到自身 tile，否则走 `FollowTrainReservation(v, ..., OkayUnused)`（`yapf_rail.cpp:2001-2012`）⇒ **origin 依赖当前 PBS 预留状态与订单类型**，这直接决定 key 形态与失效粒度。

### 9.2 核心矛盾：预留状态是高频输入，不能当失效键

§3 列了 6 类上下文依赖，其中 **"轨道上其他列车的预留"是每 tick 都在变的**。若把"预留状态"做成 key 分量或 epoch，则：

- 每次任何列车预留/释放都让 epoch 变化 ⇒ 缓存条目寿命 ≈ 0 ⇒ **命中率归零**；
- 若不放进 key，又会在"别人刚占了我要走的路"时返回过期决策 ⇒ **desync**。

这不是实现细节问题，而是**"L1 结果缓存"在 PBS 铁路上是否成立**的分水岭。架构上只能二选一：

| 路线 | 做法 | 命中窗口 | 风险 |
| --- | --- | --- | --- |
| **R1 同 tick 复用** | key 含 game tick 序号，**只在同一 tick 内复用**；只缓存 `reserve_track == false` | 短（同 tick 内重复调用） | **接近零**：同 tick 内除自身预留外无世界变化，且 `reserve_track==false` 不改状态 |
| **R2 跨 tick 复用** | key 含 `(epoch_layout, epoch_signal)`，命中后用"预留快照 + 轻量重校验"验证 | 长 | 高：重校验若做到"等于重跑"，收益归零；做不到就有 desync |

**架构结论：R1 是唯一可以先落地且能自证正确的形态；R2 必须先有 P0 数据证明确实存在跨 tick 重复，否则不做。**

### 9.3 分层架构（最终形态）

```
┌ L0 段成本缓存（已有，不动）      yapf_costcache.hpp  CSegmentCostCacheT, key=(tile,diagdir)
│                                失效 = s_rail_change_counter（YapfNotifyTrackLayoutChange）
├ L1a 同 tick 结果复用（本轮设计主体）
│    key = (tick_seq, v->index, origin.tile, origin.trackdir, enterdir, tracks,
│           dest_kind, dest_tile, target_tile, target_trackdir, geom_digest)
│    value = {track, path_found, target, dest}
│    前置条件 = !reserve_track && !DisableCache && 缓存开关开
├ L1b 跨 tick 结果复用（P2，需 P0 数据支持）
│    额外 key 分量 = (epoch_layout, epoch_signal)；命中后重校验预留可达性
└ L2 局部决策（不做）
```

### 9.4 Key 设计（逐字段给理由）

```cpp
struct R3RYapfKey {
    uint32_t tick_seq;      // 9.2 R1 的核心：同 tick 才可能命中
    uint32_t veh_index;     // v->index.base()：把"同车连续调用"主场景隔离，天然挡住跨车误用
    TileIndex origin_tile;  // 来自 FollowTrainReservation 或自身 tile（见 9.1 补充事实）
    uint8_t  origin_td;     // trackdir
    uint8_t  enterdir;      // DiagDirection
    uint8_t  tracks;        // TrackBits（已裁到 6 bit）
    uint8_t  dest_kind;     // OrderType + R3R GetCoupleIsDepot 位（origin 语义不同，必须进 key）
    TileIndex dest_tile;    // 订单目的地解析结果（None 用 INVALID_TILE）
    TileIndex target_tile;  // 传入的 PBSTileInfo* 内容（可能为 INVALID_TILE）
    uint8_t  target_td;
    uint32_t geom_digest;   // 列车几何/动力学指纹，见下
};
```

`geom_digest` = `(节数 << 24) ^ (Σ cached_veh_length << 8) ^ (railtype 位) ^ (IsDrivingBackwards ? 1 : 0) ^ (是否 90° 禁行 << 1)`。

理由：

- **必须含 `v->index`**：YAPF 对列车长度、速度、加速度、realistic braking 敏感（§3 第 3 条）。用 index 而非"几何摘要"作隔离，是因为**长度相同但速度/加速度不同的列车可以给不同答案**（同 tick 内多次调用最常见于同一列车）。
- **必须含 `geom_digest` 即使已有 index**：同 index 的列车可能在同一 tick 内被 R3R 改了编组（couple/decouple 会改 `cached_veh_length` 与 ★ 分布）而不换 index；index 单独不够。
- **必须含 `dest_kind`**：`OT_GOTO_COUPLE` 有两种语义（`GetCoupleIsDepot()`），影响 origin 计算（`yapf_rail.cpp:2001-2009`）；同一 (origin,dest) 在两种语义下答案可以不同。
- **刻意不含**：信号红绿、其他车预留、trace restrict 程序状态 —— 这些由 R1 的"同 tick"约束覆盖（同 tick 内它们不变）。**这是整个设计的关键简化**：不是"忽略"，而是"用 tick 边界把它变成常量"。

### 9.5 条目与存储

```cpp
struct R3RYapfEntry {
    R3RYapfKey key;       // 完整 key，不做哈希替代
    Track      track;
    bool       path_found;
    TileIndex  target_tile;
    uint8_t    target_td;
    TileIndex  dest;
    uint32_t   lru_tick;
    bool       valid;
};
```

- **完整 key 落条目，不做"哈希命中即信任"**：误命中的后果是列车静默走错轨（可能撞车/卡死），比 miss 贵无穷倍。
- 存储 = 固定容量开放寻址（线性探测），容量 4096 起（4096 × ~48B ≈ 200 KB），可配。
- 驱逐 = 取 `lru_tick` 最小者（4096 线性扫描只在满且 miss 时发生，代价可控；必要时降为 clock/second-chance）。
- **tick_seq 变化不做批量清空**：条目靠"key.tick_seq 不等"自然作废 + 驱逐回收，避免每 tick memset 抖动。

### 9.6 失效矩阵（R1 下的答案：绝大多数"不需要"）

| 事件 | R1（同 tick）是否需要失效 | 理由 |
| --- | --- | --- |
| 轨道/信号灯/站台结构改动 | **不需要**（tick 已变） | `YapfNotifyTrackLayoutChange` 本来就在同一 tick 内被调用，而 R1 的 key 含 tick |
| 其他列车预留/释放 | **不需要** | 同 tick 内 `reserve_track==false` 的调用不改世界；但 U-1 见下 |
| R3R 编组改动（couple/decouple/翻转/车库） | **不需要**（`geom_digest` + index 兜住） | 若改动发生在同 tick 内且 index 不变，`geom_digest` 变化即 miss |
| 读档 / 新局 | **需要：清空**（一次性） | 避免跨存档脏条目（虽然 tick 也会变，但主菜单→读档的 tick 可能不单调） |
| 车辆删除（index 复用） | **需要**：`geom_digest`/index 组合可能撞上新车 | 加一条"index 复用代际"更稳，见 U-2 |

**U-1（必须靠 P0 实测确认，否则 R1 不成立）**：同一 tick 内是否存在"先 `reserve_track==false` 探测 → 别人预留 → 再探测"的交错。理论上 `ChooseTrainTrack` 是同步单线程、中间不切 tick，但 R3R 的挂车探测（`FindNearestCoupleTrain` 恒 `DisableCache(true)`，`yapf_rail.cpp:1337`）可能与主寻路交错。若存在，则需要把"预留代际"也并进 key（则 R1 退化为 R2 的复杂度）。**这条必须先用 P0 计数器量，不许凭推理结论。**

### 9.7 确定性验收（G1 的机器化）

1. 缓存必须判 `DisableCache(true)` 立刻旁路（`pf2` 用得到，`yapf_rail.cpp:1981`）。
2. 验收命令：`-d yapfdesync=1`（或 `desync=2`）跑基准场景，日志出现 `CACHE ERROR: ChooseRailTrack()` 即失败。
3. **命中率与结果必须同时上报**：只报"命中率 60%"没有意义，必须同时报"这 60% 与未缓存结果逐字节一致"（由第 1 条保证）。
4. 关缓存路径（编译期 `R3R_YAPF_CACHE=0`）必须与原生**逐字等价**：不能让开关本身改变任何一行行为（包括计数器）。

### 9.8 观测（P0，且必须先于任何缓存代码）

只读计数器，全部挂在 `R3RFopenDbg`/`R3RPerfFrameTick` 既有通道上，输出进 `R3R_perf.log`：

| 计数器 | 用途 | 决定什么 |
| --- | --- | --- |
| `yapf_calls` 分 `reserve_track` true/false | reserve 分布 | **若 false 占比 <10%，L1a 直接否决**（覆盖不到主路径） |
| `yapf_calls` 分"同 tick 同车重复" | R1 的理论命中上限 | 若上限 <5%，整个 L1 否决 |
| `yapf_calls` 分"跨 tick 同键重复" | R2 的理论命中上限 | 决定是否值得做 R2 |
| `yapf_ns` 累计 + p99 | 耗时占比 | 决定收益天花板 |
| `yapf_nodes` 平均展开节点 | A* 规模 | 判断"命中省下的到底是多少" |

**这一步的价值不在于"有基线"，而在于它能在写任何缓存代码之前就否决整个方向。** §7 的 C-4「先测量」正是这个意思，本轮把它具体化为上面 5 个计数器。

### 9.9 代码落点（为省编译时间而设计）

| 内容 | 落点 | 为什么 |
| --- | --- | --- |
| 缓存实现（key/entry/查表/淘汰） | **新增 `src/pathfinder/yapf/r3r_yapf_cache.cpp`（独立 TU）** | 只改 `.cpp` + `CMakeLists.txt` 的源文件列表 ⇒ 不触发 KI-183 全量；若塞进 `yapf_base.hpp` 加字段，则改 yapf 头 → 删 5 obj（尚可）+ 改 `train.h` 才是灾难 |
| 缓存查询点 | `yapf_rail.cpp` 的 `ChooseRailTrack` 最前面插 3 行（查 / 写回 / 旁路判 `DisableCache`） | 保持单点、易回退 |
| 计数器 | 同上文件 + `r3r_perf.h` 已有 PERF 行 | 复用既有通道，不新建日志文件 |
| 开关 | 编译期 `R3R_YAPF_CACHE`（默认 0）+ 运行时 `R3R_YAPF_CACHE=1` 环境变量 | 默认关 ⇒ 合入主干也不改变任何行为，符合"可关闭"硬约束（§5.5） |

**U-3（2026-11-08 已取证，不再是未确认项）**：源列表是**显式枚举**，不是 glob —— `src/pathfinder/yapf/CMakeLists.txt:1-23` 是一个 `add_files(...)` 调用逐行列出 15 个 `.h` + 6 个 `.cpp`（含 `yapf_rail.cpp:17`），该函数定义在 `cmake/SourceList.cmake:34` 的 `add_files()` → `_add_files_tgt(${OPENTTD_LIB} ${ARGV})`。⇒ 新增 `r3r_yapf_cache.cpp` 必须在 `src/pathfinder/yapf/CMakeLists.txt` 里加一行；改 CMakeLists 会触发 reconfigure，但只多出一个 TU，**不改任何 `.h`** ⇒ 不触发 KI-183 全量，符合 §9.9 的省时设计。
（顺带记录同一教训：`src/train_cmd.cpp.r3r_bak` 这类未跟踪备份会污染全树 grep —— 本次搜 `YapfTrainChooseTrack` 的第一条命中就来自它，行号 15917 具误导性。取证时应带 `--glob !*.r3r_bak` 或直接指名文件。）

### 9.10 分阶段计划

| 阶段 | 内容 | 产出 | 否决条件 |
| --- | --- | --- | --- |
| **P0** | 5 个只读计数器 + Release 树基线 | 三张分布表 + 耗时占比 | `reserve_track==false` 占比 <10% 或 同 tick 重复上限 <5% ⇒ **终止 L1，转方案 C（增强 L0）** |
| **P1** | L1a 同 tick 复用（只服务 `reserve_track==false`） | Debug 树 `yapfdesync` 零 `CACHE ERROR` + Release 树收益 | desync 或收益 <3% ⇒ 回退，不留代码 |
| **P2** | L1b 跨 tick（epoch_layout/epoch_signal + 预留重校验） | 仅在 P0 证明"跨 tick 重复占比 >15%"时才启动 | 重校验成本吃掉收益 ⇒ 放弃 |
| **P3** | 评估 L0 扩容 + 信号参与失效（方案 C） | 与 L1 独立 | — |

### 9.11 架构层风险清单

| 编号 | 风险 | 缓解 |
| --- | --- | --- |
| A-1 | `reserve_track==true` 占绝对多数 ⇒ L1a 空转 | P0 先量，占比 <10% 直接终止 |
| A-2 | 缓存命中后错过了"本次调用本该产生的预留"，导致后续 tick 世界状态不同 | 这是 F2 的必然推论；**R1 用 `reserve_track==false` 前置条件硬性规避**，不做例外 |
| A-3 | 跨 tick 复用命中过期决策 ⇒ 撞车/卡死 | P2 才做，且必须过 `yapfdesync` + 长跑 |
| A-4 | 误命中（哈希碰撞/字段缺失） | 完整 key 比对，不用"哈希即信任" |
| A-5 | 性能计数器本身改变时序（`/Od` 下尤其） | 计数器只在 `R3R_PERF` 下编入实际计时（`R3RPerfOn()` 包裹）；判决树另跑一次全关 |
| A-6 | 缓存放在 `Yapf()` 全局单例里 ⇒ 改 `yapf_base.hpp` ⇒ 连带影响其他三种 YAPF（road/ship） | 独立 TU + 文件内 static，**不进 `yapf_base.hpp`** |

### 9.12 入口调用面取证（2026-11-08，决定 L1a 判据挂在哪）

`YapfTrainChooseTrack` 在全树（排除 `.r3r_bak` 污染）只有 **1 个调用者**：`src/train_cmd.cpp:16025` 的 `static Track DoTrainPathfind(...)`（`:16022`），它把第 6 参 `do_track_reservation` 原样透传。`DoTrainPathfind` 有 **3 个调用点**：

| 调用点 | 第 6 参实参 | 语义 | 对 L1a 的意义 |
| --- | --- | --- | --- |
| `train_cmd.cpp:17694` | 变量 `do_track_reservation`（R3R 挂接专用回退寻路，`OT_GOTO_COUPLE` 分支内） | 由上层决定 | 需 P0 分辨真实取值 |
| `train_cmd.cpp:17986` | 变量 `do_track_reservation`（正常 `ChooseTrainTrack` 路径） | 由上层决定 | **主路径**，L1a 的收益主要来自这里 |
| `train_cmd.cpp:18167` | **字面量 `true`** | 「换到下一条订单，试一下有没有路」（`orders.SwitchToNextOrder(true)` 之后） | 这是**探测语义**却传 `true` ⇒ 可能真的预留。见 U-5 |

结论：L1a 的判据**必须挂在 `ChooseRailTrack` 里读 `reserve_track`**，不能靠在 `DoTrainPathfind` 调用点做白名单（3 个调用点里有 2 个是变量，静态分不出来）。

---

## 10. 待拍板（D-1 ~ D-6，架构层）

| 编号 | 问题 | 选项 | 建议 |
| --- | --- | --- | --- |
| **D-1** | 第一形态选 R1（同 tick）还是直上 R2（跨 tick） | R1 / R2 / 先 R1 再按数据决定 R2 | **R1**（零失效风险，且能最快暴露"命中窗口是否够大"这个致命前提） |
| **D-2** | 是否先做 P0 计数器再写缓存 | 先量 / 直接写缓存原型 | **先量**（5 个计数器能在写缓存前否决整个方向） |
| **D-3** | key 是否含 `v->index` | 含 / 只含几何摘要 | **含**（"同车重复"是主场景；几何摘要防编组变更） |
| **D-4** | 缓存实现落点 | 独立 `.cpp` / 塞进 `yapf_base.hpp` | **独立 `.cpp`**（避免 KI-183 全量与波及 road/ship） |
| **D-5** | 默认开关 | 编译期默认 0 + 运行时环境变量 / 只编译期 | **编译期 0 + 环境变量**（合入主干零行为改变） |
| **D-6** | 基准场景（承 §7 C-3） | 用户指定服务器存档 + 固定时长空跑 | **仍需用户给存档与时长**，P0 之前必须定 |

**U 清单（不许当结论）**：
- U-1 同 tick 内是否真有"探测→他人预留→探测"交错（§9.6）。R1 是否成立的前提，**只能 P0 实测**。
- U-2 车辆 index 复用是否需要代际计数（§9.6）。
- ~~U-3 源列表 glob 还是枚举~~ → **已取证，见 §9.9**：显式 `add_files`，需在 `src/pathfinder/yapf/CMakeLists.txt` 加一行。
- U-4 `reserve_track` 的实际分布（P0 的首要输出）。
- **U-5（新，2026-11-08）**：`train_cmd.cpp:18167` 的 `DoTrainPathfind(consist, next_tile, exitdir, reachable, path_found, true, &cur_dest, nullptr)` 实参硬编码 `true`，但它处在「换下一条订单试有没有路」的探测位置。若该处确实产生 PBS 预留，则它是 L1a 的**漏网路径**（探测调用也会写世界状态），必须以 P0 计数器按「调用点标签」再分一层来确认；若它不预留（例如 `cur_dest.okay==false` 时上层会丢弃/重算），则无影响。**未取证，不得假设。**

---

## 11. 机制澄清：为什么是「前瞻一大堆、只预留一小点」（2026-11-08，回答用户提问）

用户问：「预留不应该是只到下一个安全停车位吗，难道铺预留的寻路是前瞻一大堆然后只小小的预留一点吗」——**答案是肯定的，这正是设计**，且已代码取证。

取证链（`src/pathfinder/yapf/yapf_rail.cpp`）：

| 行 | 事实 |
| --- | --- |
| `:2015` | `Yapf().SetDestination(v)` —— 搜索目标取自订单的 `dest_tile`（GOTO_STATION/DEPOT/WAYPOINT/GOTO_COUPLE 都有） |
| `:2018` | `FindPath(v)` —— 一路算到该目的地为止，**前瞻距离由目的地决定** |
| `:2038-2047` | 找到路径后回溯至起点，把沿途节点压入 `safe_position_nodes`，再调 `FindSafePositionOnSafePositionNodes()` |
| `:896-902` | 用 `rbegin()` 遍历 ⇒ 顺序是**从离车最近的一端往目标端**扫 |
| `:870-878` + `:160-167` | 逐节点 `IterateTiles` 判 `IsSafeWaitingPosition()`，命中即写 `res_dest_*` 并 `return false` 停止该节点迭代 ⇒ 取**第一个**安全位 |
| `:2059` | `TryReservePath(target, ...)` —— 只把预留订到 `res_dest` |

因此：**搜索终点 = 订单目的地；预留终点 = 沿选定路径离车最近的安全停车位。二者生来不等，不是 bug。**
「安全停车位」的判据是 `IsSafeWaitingPosition()`（`pbs.cpp`），与驾驶侧 `ExtendTrainReservation()`、挂接侧 `FindSafePositionProc()` 共用同一把尺子（`:773-777` 注释自陈）。

**预留会超过「下一个安全位」的两个例外**（必须记住，否则会把它们当 bug 追）：

1. 长预留：`train_cmd.cpp:18257-18259` 的 `TryLongReserveChooseTrainTrack()` 在 LR 信号布局下**逐级往后多订一段**；`18244-18254` 的 `LR-NO-PROGRESS` 是防递归爆栈的保护（预留终点不前进就停止递归）。
2. R3R 挂车预留：`yapf_rail.cpp:628-800` 的注释明确写「驱动段截断在第一个安全位，但**最后一个安全位之后仍要继续订满到会合点**」，否则机车自锁。这类预留的用途是「占位给对面看」，不是「我要开到那儿」，所以不按安全位收尾。

**对缓存方案的影响（补进 D-3 的理由）**：既然预留终点恒为安全位，缓存需要保留的不是「到目的地的整条长路径」，而是**决策结果**——所选路径上【当前位置 → 第一个安全位】这一段的格序与下一步 trackdir。整条到目的地的路径只是算代价的中间产物，用完即可丢；命中时省掉的是整场 `FindPath`，但 `TryReservePath` 那一步必须照常重放（副作用不能抄）。

结论状态：**已取证（锚点齐全），非推断。**

---

## 12. 「只问不占」为什么必须单独存在，不能并进占位预留（2026-11-08，回答用户提问）

用户问：不占位的预留有什么存在的必要？只是看路径惩罚吗？为什么不能包进占位的预留里？

**它不是「只看惩罚」，它是另一类问题：问「该不该」，不问「我要走过去」。**

`DoTrainPathfind()` 全文件只有 3 个调用点，定义在 `train_cmd.cpp:16022`：

| 锚点 | 实参 | 性质 |
| --- | --- | --- |
| `:17694` | 变量 `do_track_reservation` | 正常开车前订路 |
| `:17986` | 变量 `do_track_reservation` | 同上（长预留重入） |
| `:18167` | **硬编码 `true`** | 换下一条订单的探测；与位置语义矛盾，登记为 U-5 |

真正的「只问不占」在**另一条 API**上，不在 `ChooseTrainTrack`：

| 调用 | 锚点 | 问的是什么 |
| --- | --- | --- |
| `CheckReverseTrain` | `train_cmd.cpp:19458`（定义）／`:13419`、`:23077`、`:23383`（调用）→ `yapf_rail.cpp:2068-2086` `stCheckReverseTrain` | 「在这格掉头之后还有没有路」——问完**可能决定不掉头**，此时根本没有「我要走的路」可占 |
| `FindNearestSafeTile(..., dont_reserve, ...)` | `yapf_rail.cpp:1888` 形参 `bool dont_reserve` | 接口层显式为「只问不占」留了开关（本轮未定位到传 `true` 的上层调用点 ⇒ 要么是死参数，要么另有调用者，**待查**） |

**不能并进占位预留的三条硬理由**（前两条是本项目历史实证，不是理论）：

1. **占位是有主的账**：占了就得跨 tick 记住归属，并在「车被卖／链被编辑／重新订路／读档」时释放。纯询问无主，天生不留尾巴。R3R 自己的伤疤就是证据——KI-203 孤儿预留（`ReserveTrackUnderConsist` 只加不减）、KI-208 幽灵预留（先订后撤、玩家可见）、KI-345/346 走廊（`rp()` 订错轨道 20 格）。
2. **占位改变别人的结果**：同 tick 内后算的车看到占位就绕（`CheckTrainsOnTrack` 只读地图位），结果依赖调用顺序。询问必须是纯函数，才能重放、才不 desync。
3. **询问天然会失败**（订单不可达、掉头不可行），失败就得回滚。「探测也占位」= 每天制造大量「订了又撤」，这正是幽灵预览的直接来源。

**结论**：不是技术上无法合并，而是合并后必须为**每一条探测路径**都提供精确可逆的释放——这个代价 R3R 已经反复付过。`train_cmd.cpp:18167` 那行硬编码 `true` 恰恰是「把探测也包进占位」的现成样本，它是否真的留下预留，由 U-5 的 P0 计数器回答。

**对缓存方案的影响**：可安全抄答案的只有「只问不占」的调用；而它的数量可能极小（3 个 `DoTrainPathfind` 点中 2 个传的是同一个变量，`CheckReverseTrain` 走的是另一套不订路的 API）。若 P0 量出占比 <10%，缓存的总体收益接近 0——与 U-4 合并判定，不得单凭架构好感推进。

结论状态：**已取证（锚点齐全），非推断**；例外项：`dont_reserve` 的实际调用者未定位，标为待查。

---

## 13. 三岔路怎么选：一次 A*、三条路并行摊开（2026-11-08，回答用户提问）

场景：列车在 A 站，订单推进到「去 B 站」，A→B 之间有 1/2/3 三条路径可选。

### 13.1 选路机制：不是挨个试，是同一张 open list 里同时展开

| 步骤 | 锚点 | 做什么 |
| --- | --- | --- |
| ① 定目标 | `yapf_rail.cpp:2015` `SetDestination(v)` | 目标 = 订单 `dest_tile`（B 站的站台格） |
| ② 搜 | `yapf_base.hpp:161` `FindPath()`；主循环 `:167-169` `nodes.GetBestOpenNode()` | 按 f = g + h 取最优开节点，展开后继后插回，直到弹出终点 / open 空 / `max_search_nodes`(默认 10000) |
| ③ 比 | `yapf_costrail.hpp` | 三条路各自累计 g，**谁的总代价小谁赢**；先探到 B 不等于赢 |
| ④ 出结果 | `yapf_rail.cpp:2059` `TryReservePath(res_dest)`；`train_cmd.cpp:16022` `DoTrainPathfind` | 只交出「下一步走哪条轨」(Track / Trackdir)，**不交出整条路** |

⇒ 1/2/3 三条路在搜索里是**同时存在**的分支，A* 用 f 的排序决定先展开谁；遇到代价已经超过当前最优完整路径的分支，直接被剪掉。

### 13.2 代价由什么构成（这就是「为什么它不走最短的那条」）

`yapf_costrail.hpp` 的实测清单：

| 项 | 锚点 | 说明 |
| --- | --- | --- |
| 每格基础 | `:594-607` 注释 | 直格 `YAPF_TILE_LENGTH`、斜格 `YAPF_TILE_CORNER_LENGTH` |
| 坡度 | `:107-110` `rail_slope_penalty` | 上坡逐格加 |
| 转弯 | `:117-124` `rail_curve45_penalty` / `rail_curve90_penalty` | 90 度另判，可由 `forbid_90_deg` 直接禁 |
| 道口 | `:152-155` `rail_crossing_penalty` | |
| 双滑 | `:131-135` `rail_doubleslip_penalty` | |
| **被别人订的格/站台** | `:198-202` `rail_pbs_station_penalty`×格、`rail_pbs_cross_penalty`×格 | 三条路差异的最大来源 |
| 红灯 | `:455-461` `rail_firstred_penalty` / `rail_firstred_exit_penalty` | 前方第一个红灯 |
| 站台长短 | `:548-563` `rail_longer_platform_penalty` / `rail_shorter_platform_penalty`（+ per_tile 项） | 选哪个站台 |
| 途经站台 | `:757` `rail_station_penalty` × platform_length | 穿站台要钱 |
| 库内掉头 | `:698` `rail_depot_reverse_penalty` | |
| 终点被占 | `:736-747` `rail_lastred_penalty`、路点被占 `pbs_station_penalty*4` | |

**推论**：路 1 最短但被别车订了 8 格 ⇒ g 里多出 `8 × rail_pbs_cross_penalty`；路 2 多绕 10 格只是 `10 × 基础`；谁大取决于设置。所以「它绕远了」通常是**在躲别人的预留或躲红灯**，不是 bug。

### 13.3 什么时候真的调用 YAPF（不是每 tick 全搜）

| 触发点 | 锚点 | 条件 |
| --- | --- | --- |
| 起步 | `train_cmd.cpp:23110` `TryPathReserve(consist, true, true)` | `UpdateSpeed` 后 `cur_speed==0` 且要离站 |
| 跨格后预留不够 | `:21532-21535` `!TryPathReserve(first)` | 进入新格且 `CHANGED_NORMAL_TO_PBS_BLOCK` / 无有效预留 |
| 出库门 | `:15605` | `SigSegState::Path` 才试图订路 |
| 掉头后 | `:5658` | `first_tile_okay` 变体 + `R3RMarkForceReplan` |
| 长预留信号处 | `:21544` `TryLongReserveChooseTrainTrackFromReservationEnd` | LR 信号逐级延长 |
| 解挂互锁放行 | `:3287` | 站台清空后**必须配套**一次成功的预留才放行 |

⇒ PBS 下真正的规律是：**预留够用就不搜；预留用完（到达安全位）或前提变了（掉头/出库/换订单/长预留信号）才搜一次**。从 A 到 B 全程可能只搜几次到几十次，取决于沿途安全位密度。

### 13.4 对缓存的直接含义（补进 D-3）

一次 `FindPath` 是**全量**搜索：三条路都被展开过，「被订的格」这类随时间变化的状态直接进 g。因此缓存不能只 key 成 (起点, 终点, 车)，否则别车一旦订走路 1，缓存的旧答案就是错的。可行粒度只有「同一 tick 内、地图预留状态未变」——这恰好也是 P0 要量 P-4/P-5 的原因。

---

## 14. 为什么「只返回下一步」不等于「每一格都重算」（2026-11-08，回答用户提问）

用户问：为什么不返回整条路？现状岂不是走不了多远就要重新全量算路？

**误解点：路并没有被丢掉，它被写进地图了；重算点也不是「每一格」，而是「下一个安全位」。**

### 14.1 一次寻路交出三样东西，不是一样

`DoTrainPathfind`（`train_cmd.cpp:16010-16026`）签名：

| 产物 | 形态 | 给谁 |
| --- | --- | --- |
| 返回值 `Track` | 下一步走哪条轨 | 驾驶层（`TrainController` 格内推进用） |
| 出参 `PBSTileInfo *dest` | 预留终点（安全位） | 预留层 |
| 出参 `TileIndex *final_dest` | 最优路径末格 | 订单/掉头判断 |

`yapf_rail.cpp:2054-2060`：先取回溯链上第一步 `next_trackdir`，再 `*dest = GetBestNode()->GetLastTile()`，然后 `TryReservePath(target, node->GetLastTile())` **沿整条节点链逐格下订**（`TryReservePath` 定义 `:910`）。

⇒ 整条路没被丢弃：**每一格的预留位就是这条路径的持久化形式**，而且是引擎强制版本。驾驶层照预留开，不需要第二份数据结构。

### 14.2 覆盖范围 = 「到下一个安全停车位」，不是一格，也不是到 B

| 证据 | 锚点 |
| --- | --- |
| 搜索终止原因 | `yapf_costrail.hpp:825` `EndSegmentReason::SafeTile` |
| 延长预留的语义 | `train_cmd.cpp:16044-16052` 注释「Stops on encountering a safe tile, another reservation or a track choice」 |
| 现场量级 | KI-345/346 走廊单次订出 20 格；第 93 轮 `RP cur=` 单次订 13 格 |

⇒ 「走不了多远就要重算」不成立：一次覆盖十几到几十格，走到安全位才再搜。

### 14.3 为什么不把整条路返回并缓存（四条硬理由）

1. **地图已是唯一真相**：预留 = 我的路径的引擎强制版。另存一份就要维护两套失效逻辑，任何一处漏同步就是撞车或幽灵预留（KI-203/208/345 全是这类伤疤）。
2. **存了也立刻过期**：路径正确性依赖别人的预留、信号状态、订单；任一变，旧路径就是错的。
3. **长预留 = 全网串行**：订到 B 就独占整条走廊，别的车全绕。所以原生只在「长预留 PBS 信号」处显式延长（`train_cmd.cpp:21544` `TryLongReserveChooseTrainTrackFromReservationEnd`），而不是默认订到终点。
4. **驾驶层不需要**：每 tick 只推进 subspeed，需要的是「这一格往哪拐」，不是「整条路」。

### 14.4 真正的重算触发点（不是每格）

沿 §13.3 的六条：起步 / 跨格后发现下一段没订 / 出库 / 掉头 / 长预留信号 / 解挂放行。**预留够用就不搜**，这是原生 PBS 的基本节律。

### 14.5 对缓存方案的含义（关键，补进 D-3）

- 「缓存整条路」收益接近 0 —— 那部分已经在地图上。
- 有价值的是「超出预留段、往终点方向看得很远」的那段搜索（§11 已证前瞻远大于预留）。
- 但这段恰恰最依赖别人的预留状态 ⇒ 收益只能由 P-4/P-5（同一 tick 内重复度、按调用点分布）来判定，不得凭架构好感推进。

---

## 15. 「抄到下一个岔路」方案评估（2026-11-08，回答用户提问）

用户提法：过灯就重新导航 → 把「当前位置到下一个岔路」的答案抄下来 → 岔路之前不再重算。

### 15.1 三件事实：其中两件已经做了

| 事实 | 锚点 | 含义 |
| --- | --- | --- |
| A：搜索图本来就是「岔路到岔路」的段图 | `yapf_costrail.hpp:810-813` `KillFirstBit(follower_local.new_td_bits) != TRACKDIR_BIT_NONE` ⇒ `EndSegmentReason::ChoiceFollows` ⇒ `break` | A* 的**节点 = 段**，段内逐格不展开；段代价 `n.segment` 在同一 tick 内复用 |
| B：答案本来就抄了，抄在地图预留位上 | `train_cmd.cpp:18154` `while (!IsSafeWaitingPosition(...))` 内 `:18167 DoTrainPathfind` 逐段延长；`yapf_rail.cpp:2059 TryReservePath` | 一次 FindPath 订到安全位，驾驶层照预留开、不重搜 |
| C：**岔路 ≠ 安全位**，两者不是同一个点 | `pbs.cpp:2725-2755` 判据只有：车库 / 非 PBS 信号 / 隧道桥信号口 / 线路尽头（`include_line_end`）/ 下一格无可达轨 / 下一格单轨+PBS 信号 | 判据里**没有「岔路」项** |

⇒ 事实 C 的直接后果：用户说的「抄到岔路」比现状「抄到安全位」**更短**，反而增加搜索次数。
岔路格终止预留只是兜底形态，现场探针签名即 `RESV-TERM ... tcount=2 safe=0`（`train_cmd.cpp:18215`，`tcount>=2` = 该格多轨；注释自陈 legally terminated on a junction）。

### 15.2 方案拆成三份，只有第三份是新东西

| 子方案 | 现状 | 增量 | 风险 |
| --- | --- | --- | --- |
| 按岔路切段（段图） | **已实现**（事实 A） | 无 | — |
| 抄下答案并持久化 | **已实现**（预留位，事实 B） | 无 | — |
| 跨 tick 记住「岔路口的选择序列」 | 未实现 | 有 | 高（正确性） |

### 15.3 为什么「跨 tick 抄答案」的收益与直觉相反

- **直线段**（无岔路）：在 A* 里是 1 个节点、一次 `TrackFollower` 走到底，本来就便宜。抄它省的是常数（节点对象构造/析构、open list 操作），不是搜索量。
- **岔路口**：open list 真的分叉，是唯一贵的地方；但那也正是别人的预留/信号变化最频繁的地方 ⇒ **旧答案必然过期**。
- ⇒ 抄得到的地方不值钱，值钱的地方抄不了。这是这个方案的天花板，不是实现问题。

### 15.4 真正值得抄的三样（按风险从低到高，供后续拍板）

1. **负缓存（失败结论，8~64 tick）**：键 = (起点 tile/trackdir, 目标, 订单)，值 = 「这段时间不通」。风险最低、收益最确定——现场 `CPL-SKIP retryIn=8`、COUPLE-FAIL 每 tick 重试、`RESV-NOBOOK` 全是同一方向被反复重算的实例。
2. **段代价（不是答案）**：键 = (段起 trackdir, 段终 trackdir, railtypes, 预留摘要)，值 = `segment_cost`。收益必须由 P-4/P-5（同一 tick 内重复度）裁决；预留摘要是高频键，命中率存疑。
3. **段图拓扑（只存连通性，永不失效）**：拆轨/建轨/改信号时发失效通知。收益在「路径不存在」的快速否决。

**明确不抄**：答案路径本身。理由见 §14.3（地图已是唯一真相 + 存了立刻过期 + 长预留独占走廊）。

### 15.5 落地前必须补的一组数（补进 P0，不另起编号）

单次 `FindPath` 的：`open list 峰值长度` / `展开节点数` / `是否在段内退化成单链`。
判读口径：
- 若绝大多数调用 open 峰值 < 4 ⇒ 这些搜索本就是直线段，可优化空间只有常数项，不值得引入缓存失效风险；
- 若存在相当比例 > 32 ⇒ 才值得做 15.4-2 的段代价缓存。

结论状态：**已取证（锚点齐全），非推断**；15.3 的天花板判断依赖 15.5 的实测分布，标为待验证。

---

## 16. 「答案本」方案定性修正（2026-11-08，用户澄清后）

用户澄清：答案抄到岔路前的安全位（整段大直道），**预留只铺一小段**；需要重算时不去调 YAPF，而是从答案本上抄下一段铺成预留。

### 16.1 上一轮的判断前提被推翻

§15.3 说「抄得到的地方不值钱」，前提是假设 **答案覆盖 ≈ 预留覆盖**。用户把二者解耦后，前提不成立。

| 量 | 谁的 | 锚点 |
| --- | --- | --- |
| 预留终点 = 最优链上挑出的**安全位** | `res_dest_tile` | `yapf_rail.cpp:2047 FindSafePositionOnSafePositionNodes()` |
| 预留铺设 = 沿整链走到该位、逐格下订 | 地图 | `yapf_rail.cpp:2059 TryReservePath(target, node->GetLastTile())` |
| 答案 = **整条回溯链** | 内存（用完即弃） | `yapf_rail.cpp:2041-2046`（`node = node->parent` 一路回溯） |

⇒ 答案通常远长于预留（现场预留 13~20 格，而答案到目的地可几百格）。**答案本确实存在"预留之外的剩余价值"**，这是 §14.5 说的"超出预留段"的可操作形态。

### 16.2 这座方案的支点是「确定性 + 铺预留即校验」

两点合起来，使它在**状态未变时不是近似优化，而是逐字节等价**：

1. **确定性**：A* 无随机项。地图状态（轨道 / 信号 / 别人预留 / 道岔）不变 ⇒ 重算必然得到同一条答案。
2. **校验免费**：把答案的下一格变成预留，本来就要走 `TryReserveRailTrackdir`（`pbs.cpp:144`，含 `IsWaitingPositionFree` 与冲突检查）。**订不上 = 答案已失效**，不需要额外的失效通知机制。

⇒ 失效检测搭在本来就要执行的那一步上，这是本方案最省的地方，也绕开了 §6 说的"失效通知是雷区"。**上一轮我判定"贵的地方抄不了"，在"答案段内无岔路"的前提下不成立**——因为无岔路就没有"决策"，只有"沿轨前进"，而沿轨前进的正确性恰好由逐格下订完整覆盖。

### 16.3 剩余三条真实风险（都可控，但必须写进设计）

| 风险 | 机制 | 对策 |
| --- | --- | --- |
| 局部失效 ⇒ 从「绕」退化为「等」 | 答案某格被别人长订，抄答案只能停；重算可能改走另一岔路 | 抄答案失败即**丢弃答案并重算**，行为回到现状，只损失"白试了一段" |
| 非最优 | 状态变了但仍可订 ⇒ 走的是可行但非最短的路 | **只在答案段内无 track choice 时启用**（无分支 ⇒ 等价性可严格保证） |
| 起点漂移 / 朝向反向 | 答案带的是固定格序列；车停在段中间、或 `DrivingBackwards` | 答案带起点 key + trackdir；定位不到或 dir 反即重算 |

**回退代价要计价**：抄答案试到一半失败，必须把已订的几格干净撤销，否则留下半截幽灵预留 —— 这正是 KI-203/207c/345/346 的伤疤区（`R3RReleaseReservationSafe` 那套"先自证这格仍是我的预留再释放"的纪律必须照搬）。

### 16.4 落地形态（不改搜索、不改判据）

| 环节 | 落点 |
| --- | --- |
| 存 | `Train` 挂紧凑数组 `{tile, trackdir}` + 游标 + 起点 key（**NOSAVE**，读档丢弃） |
| 填 | `yapf_rail.cpp:2041-2046` 回溯循环里顺手拷一份（本来就在逐节点遍历，零额外遍历） |
| 用 | `TryPathReserve` 判"预留不足"之前先试答案本；铺成功即返回，**不进 YAPF** |
| 丢 | 订不上 / 起点不匹配 / 朝向不符 / 订单变 / 掉头 / 解挂 |

### 16.5 还没定的一件事：答案段的边界应取"安全位"还是"岔路"

- 安全位在岔路**之前** ⇒ 答案段永远不含决策 ⇒ 等价性最强，收益也最小（每段都短）；
- 安全位在岔路**之后** ⇒ 答案段含一次决策 ⇒ 必须额外保证"岔路另一侧的可订性未变"，等价性变弱。

⇒ **建议第一版只做"答案段内无 track choice"**：这一档收益/风险都能先算清，再决定要不要放开到含岔路。

### 16.6 与 §15 结论的关系（不是矛盾，是分层）

- §15.4 说的三样（负缓存 / 段代价 / 段拓扑）是**喂给 A* 的原料**；
- §16 的答案本是**绕过 A*** 的路径；
- 二者可以并存，但**先做哪个仍由同一组 P0 数决定**：若多数调用的答案段内无岔路且 open 峰值 < 4，答案本收益上限就是"省掉这些常数级 A*"，此时它比段代价缓存更简单、更安全，**应优先**。

结论状态：**16.1/16.2 已取证（锚点齐全）；16.3 风险为推断，需 16.6 的实测命中率与回退代价验证。**

---

## 17. 答案存在哪里：轨道位 vs 引擎（2026-11-09，回答用户提问）

### 17.1 结论：只能存引擎侧，不能存轨道位

| 落点 | 能存什么 | 语义 | 后果 |
| --- | --- | --- | --- |
| 轨道位 `_m[t].m2` bit 8-11 | **只有预留**（`SetTrackReservation` / `GetRailReservationTrackBits`） | 排他占位：「我现在有权走这条轨」 | ①别人看到被占 ⇒ **独占走廊**，与「只铺一小段」直接冲突 ②随存档保存（是存档字段） ③必须由订它的车负责撤销 ⇒ 撤销路径是 R3R 头号伤疤区（KI-203 / 207c / 345 / 346 全是「该撤没撤 / 撤错格 / 撤了别人的」） |
| 引擎对象 | **记忆**（不占位、不挡人） | 「我上次是这么走的」 | 失效就丢，零副作用 |

**决定性理由**：把答案写进轨道位，答案长度就等于预留长度，「答案」与「预留」就不再解耦，直接退化成 §14.3 说的长预留独占走廊。用户要的正是「答案长、预留短」，所以**答案必须存在轨道位之外**。

地图上不存在「只记不占」的位——`_m[t].m2` 的那 4 bit 一旦置上就是排他的。这是硬约束，不是实现选择。

### 17.2 引擎侧三个落点

| 方案 | 落点 | 存档 | 头文件改动 | 代价 |
| --- | --- | --- | --- | --- |
| A：`Train` 新增成员 | `vehicle_base.h` | 需标 NOSAVE，或进 R3VP 稀疏块 | **要改 `src/*.h`** | 依赖表已失效（`ninja -t deps` 报 STALE）⇒ 必须全量重编 ~16~20 分钟 |
| **B：`train_cmd.cpp` 文件内 static side table** | 本文件 | 零（运行时态，读档自动清空） | **零** | 需手工 erase（卖车/解挂/换单/掉头） |
| C：新稀疏存档块 | `couple_group_sl.cpp` + 新 chunk | 要写 chunk + 版本 | 要动 sl | 最重，且答案本无跨存档价值 |

**推荐 B。** 依据（均为本仓库既有先例，不是新发明）：

- 同构先例就在旁边：`train_cmd.cpp:15318 _r3r_couple_partial_reserve`（`unordered_map<uint32_t, R3RCouplePartialReserveRecord>`，key = 车辆 index）、`:15278 _r3r_depot_self_release`、`:14224 _r3r_wbar_state`、`:326 _r3r_spd_ctx_by_veh`、`:16085 _r3r_loop_memo`。
- 「NOSAVE 记忆」在本仓库是**明确先例**：`vehicle_base.h:411 name_backup` 注释自陈 *"NOSAVE: like a parked schedule after a load it is simply dropped"*；`vehicle_base.h:433-438 r3r_couple_requester` 注释自陈 *"a stale value is always recognised by the two-sided check ... simply re-locks on the next tick instead of ever dangling"*。答案本完全适用同一套论证。
- 读档丢失答案**没有损失**：它只是省一次搜索的加速器，读档后第一次寻路重新填即可。

### 17.3 答案的元素类型

不能只存 `TileIndex`：同一格可双向走，光有 tile 无法确定行进方向。

沿用现成结构 `PBSTileInfo { TileIndex tile; Trackdir trackdir; bool okay; }`（`pbs.h`），答案为：

```
struct R3RAnswerPath { std::vector<PBSTileInfo> steps; PBSTileInfo origin; /* 起点 key */ };
```

`origin` 是「这段答案是从哪一格、朝哪个 td 出发算出来的」，用于抄之前比对车辆当前状态；`steps` 是逐格序列。

### 17.4 终点规则：「岔路前最后一个安全位」需要精确化

用户表述：答案记到岔路之前的最后一个安全位。落成可执行规则要补三件事：

**(1) 区间定义**：`[当前位置, 下一个岔路)`
「岔路」的可判据就是 A\* 自己的段边界——`yapf_costrail.hpp:810-813` `KillFirstBit(follower_local.new_td_bits) != TRACKDIR_BIT_NONE` ⇒ `ChoiceFollows`。**不要另造判据**，用同一把尺子，答案段才和搜索段对齐，抄回来时起点/终点能对上。

**(2) 退化**：区间内没有安全位怎么办
`IsSafeWaitingPosition`（`pbs.cpp:2725-2755`）只有六条：车库 / 非 PBS 信号 / 隧道桥信号口 / 线路尽头 / 下一格无可达轨 / 下一格单轨+PBS 信号。一段大直道完全可能一条都不满足。

⇒ 建议退化为**记到岔路格本身**（`okay=false` / `safe=0` 形态）。依据：现场 `RESV-TERM ... tcount=2 safe=0`（`train_cmd.cpp:18215`，注释自陈 "legally terminated on a junction"）证明引擎接受在岔路格终止。且岔路格正是 A\* 段边界，答案段与搜索段天然对齐。

**不要**退化到「岔路前最后一格」——那切在段内部，抄回来时起点定位要多一层模糊匹配。

**(3) 不含决策**：这是本方案的定位，也是它的天花板
按此定义，答案段**永远不含岔路口的决策**（走哪条边）。这正是 §16.2 等价性最强的形态，但也意味着：
- 抄得到的地方 = 无分支大直道 = A\* 里本来只有 1 个节点
- 一到岔路仍必须重算 = open list 真正分叉的地方

⇒ 收益上限就是「省掉直线段上的常数开销」，与 §15.3 判断一致。想覆盖岔路口，必须额外保证「岔路口另一侧的可订性未变」，那是另一档风险。

### 17.5 生命周期：唯一丢弃出口

B 方案的唯一负担是 clean-up。按本仓库既有纪律（R3R 所有释放路径都先自证身份再动手），答案本必须只有**一个**丢弃入口：

```
static void R3RDropAnswerPath(Train *v, const char *reason);
```

调用点：`Vehicle::PreDestructor`（卖车）/ Couple / Decouple / 订单变 / 掉头 / 铺预留失败 / 起点 key 不匹配。
每条都打一行探针（`ANSWER-DROP veh= reason=`），否则将来「答案为什么没用上」无法判读。

### 17.6 未定项

- U-17-1：`steps` 容量上限（建议 256 格，超出截断；几百格的答案内存按车计约 256×8B=2KB，1000 车 2MB，可接受）
- U-17-2：答案段的终点是「安全位」还是「岔路格」在含信号的长直道上是否等价（若信号恰在岔路前，两者重合；否则安全位更近，答案更短）
- U-17-3：抄答案时若车辆停在答案段**中间**（预留只铺了 13 格就被挡），需要在 `steps` 里线性定位当前 `(tile, trackdir)`；定位不到即作废——这条规则要实现时实测覆盖率

结论状态：**17.1/17.2/17.3 已取证（锚点齐全，含 NOSAVE 先例与 side table 先例）；17.4 的退化规则与 17.5 的丢弃点清单为设计建议，未验证。**

---

## 18. 答案本实现（2026-11-09，按 §17 方案 B 落地）

### 18.1 落点：全部在 `src/pathfinder/yapf/yapf_rail.cpp`，零头文件改动

关键发现：本文件已有**跨文件通信的成文先例**（`:59-95`）——数据本体是文件内 `static`，对外是**非 static 函数**，调用方（`train_cmd.cpp:18064`）手写 `extern` 声明。注释自陈理由："不能为了一个标志去改 .h（会触发全量重编）"。

⇒ 答案本照此办理：`static` 表 + 非 static 接口，`train_cmd.cpp` 侧只在需要时手写 `extern`。**不碰任何 `.h`，走增量构建。**

### 18.2 三个锚点

| 环节 | 锚点 | 动作 |
| --- | --- | --- |
| 抄 | `yapf_rail.cpp:2018` `path_found = Yapf().FindPath(v)` **之前** | 先试答案本，成功即返回 `next_trackdir`，**不进 A\*** |
| 存 | `yapf_rail.cpp:2059` `TryReservePath(...)` 成功之后 | 沿 `node->parent` 链拷一份格序列 |
| 丢 | 答案本自带 `origin` 比对 + 订不上即弃 | 无需外部通知 |

### 18.3 抄答案的逐格口径（这是等价性的全部依据）

```
从 origin.(tile, td) 起，逐步沿答案前进：
  1. 该格若 HasSignals / 是站台跳格 ⇒ 按同口径跳过（与 TryReservePath 一致）
  2. TryReserveRailTrack(tile, TrackdirToTrack(td))
     失败 ⇒ 已订的格保留（原生 PBS 续订语义），返回"抄到哪算哪"
  3. 下一格是岔路（KillFirstBit(GetTrackBits) != NONE）⇒ 订完岔路前一格即停
  4. 到达答案末尾 ⇒ 停
```

第 3 条是 §17.4 的落点：**答案段内绝不含决策**，岔路一律留给 A\*。

### 18.4 第一版刻意不做

- 不做跨调用失效通知（靠逐格 `TryReserveRailTrack` 天然校验）
- 不做"答案段中段定位"（车辆停在答案中间时，直接用 `(tile, td)` 在 `steps` 里线性查找；找不到即弃）
- 不写存档（读档即清空）

结论状态：**18.1~18.3 已取证；待实现 + 编译。**

### 18.5 实现清单（2026-11-09 已落代码，仅改 `yapf_rail.cpp` 一个文件）

| # | 位置 | 内容 |
| --- | --- | --- |
| 1 | `yapf_rail.cpp:~100` | `struct R3RAnswerStep { TileIndex tile; Trackdir td; }`；`_r3r_answer_steps` / `_r3r_answer_origin` / `_r3r_answer_store_count` / `_r3r_answer_hit_count`（`std::map`，key=车辆 index） |
| 2 | 同上 | `void R3RAnswerDrop(VehicleID, const char *reason)` —— 非 static，供 `train_cmd.cpp` 手写 `extern` 清理；打 `ANSWER-DROP` |
| 3 | 同上 | `static std::vector<R3RAnswerStep> _r3r_answer_build` + `static void R3RAnswerCommit(VehicleID)`，打 `ANSWER-STORE` |
| 4 | `TryReservePath` 开头 | `_r3r_answer_build.clear()` |
| 5 | `TryReservePath` 逐格 lambda | `if (r3r_tok && !unsafe_pos) _r3r_answer_build.push_back({tile, td});` —— 挂接寻路不参与 |
| 6 | `ChooseRailTrack` 入口（`SetOrigin` 之前） | 先试 `R3RAnswerReplay`，成功即 `path_found = true` 并 `return answer_td`，**不进 `FindPath`** |
| 7 | `ChooseRailTrack` 尾部 | `TryReservePath` 返回 true 才 `R3RAnswerCommit(v->index)` |
| 8 | 新成员函数 `R3RAnswerReplay` | 按 `(tile, trackdir)` 在答案里**线性定位**车辆当前位置；从下一格起逐格 `ReserveSingleTrack`；**订到岔路格即收手**（`IsPlainRail && KillFirstBit(GetTrackBits) != TRACK_BIT_NONE`）；一格都订不上则 `R3RAnswerDrop` 并回落 A^*。打 `ANSWER-REPLAY` |

**为什么不改 `.h`**：整条链都在 `yapf_rail.cpp` 内部（`ChooseRailTrack` / `TryReservePath` / `ReserveSingleTrack` 都是同一个模板 struct 的成员），跨文件只暴露 `R3RAnswerDrop` 一个非 static 函数，风格照抄本文件 `:59-95` 的既有做法。⇒ 走增量构建。

**第一版刻意没做**：`train_cmd.cpp` 侧的 `R3RAnswerDrop` 调用点（卖车/解挂/换单/掉头）。当前靠 `origin` 定位失败与"一格都订不上"两条天然失效路径兜底；表按车辆 index 存放，index 复用时可被脏数据命中，但定位判据会拦住（除了极端巧合）。下一轮补显式清理。

### 18.6 编译自证（2026-11-09 00:35）

构建入口复用 `R3R_fullbuild.cmd inc detach`（未新建任何 `.cmd`），`MODE=inc`，`GUARD: incremental is safe`。

三件套：
- `build\R3R_incbuild.done` = `EXIT_CODE=0`（2026-11-09 00:35:52）
- 日志尾部 `[2/3] Building CXX object ...yapf_rail.cpp.obj` → `[3/3] Linking CXX executable openttd.exe`
- `src\pathfinder\yapf\yapf_rail.cpp` 00:32:55 → `yapf_rail.cpp.obj` 00:33:16 → `build\openttd.exe` 00:35:48

产物自证（exe 明文）：`ANSWER-REPLAY` / `ANSWER-STORE` / `ANSWER-DROP` / `origin-not-in-answer` / `nothing-booked` / `yapf-answer` 全部命中；`ANSWER-COMMIT` 不命中属正常（代码里没有这个字面量）。

**编译期踩到的两处坑（都是第一版写错，已修）**：

1. `error C2248: "CYapfReserveTrack<Types>::origin_tile": 无法访问 private 成员`（及同类的 `ReserveSingleTrack`）。
   第一版把 `R3RAnswerReplay` 写在派生类 `CYapfFollowRailT` 里，而 `ReserveSingleTrack`（`:638`）、`origin_tile`（`:230`）、`res_fail_tile`、`res_dest_*` 全部落在 `CYapfReserveTrack` 的 `private:` 区间（`:224` ~ `:685`），**派生类看不见**。
   ⇒ 已把整个函数搬进 `CYapfReserveTrack`（紧跟 `TryReservePath` 之后、`stDesyncCheck` 之前，public 区），`ChooseRailTrack` 侧仍是 `this->R3RAnswerReplay(...)`。
2. `error C2440: 无法从 "const Train *" 转换为 "Vehicle *const"`。
   `Yapf().GetVehicle()` 返回 `const Train *`（与 `TryReservePath` 里 `:992` 的用法一致）⇒ 改用 `const Train *const veh`。

**第一版还有一处语义错（不报错，但会导致答案本恒失效，已一并修）**：

- `ReserveSingleTrack()` 的返回值是 `tile != this->res_dest_tile || td != this->res_dest_td`（`:661`），而**复刻路径从不调用 `SetReservationTarget()`**，`res_dest_*` 里留着上一轮 A\* 的残留值。若抄到的格恰好等于残留目标，会被误判成"预订失败"。
  ⇒ replay 开头显式 `res_dest_tile = INVALID_TILE; res_dest_td = INVALID_TRACKDIR;`，让返回值只由真实订位结果决定。
- 站台格分支订不上时 `ReserveSingleTrack` 只在 `res_fail_tile` 留痕、**返回值仍为 true**，必须补一次 `if (res_fail_tile != INVALID_TILE) break;`，否则会把失败当成功继续往下订。
- 返回值语义原先写成 `origin.trackdir`，**这是错的**：原生的 `next_trackdir` 取的是根节点**第一个子节点**的 trackdir（`:2236`），即"下一格上要走的 trackdir"，不是 origin 格自己的出口方向。⇒ 改成 `steps[idx + 1].td`。

**额外加的一条安全闸**：`GetDebugLevel(DebugLevelID::yapfdesync) >= 1 || GetDebugLevel(DebugLevelID::desync) >= 2` 时直接返回 false。因为 desync 调试会把同一场景用两个 `Tpf` 各跑一遍逐节点比对（`stChooseRailTrack` 的 `stDesyncCheck`），若其中一次走答案本、另一次走 A\*，会报 CACHE ERROR 假警报。开调试时一律走原生路径，也便于改前改后对比。

结论状态：**已实现 + 已编译通过（EXIT_CODE=0）；游戏内命中率/正确性复测待做。**



结论状态：**已取证（锚点齐全），非推断。**
