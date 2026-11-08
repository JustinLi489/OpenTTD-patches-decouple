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
2. `r3r_perf.h:126-128` already 明确了目标组合：`-DR3R_PROBES=1 -DR3R_PROBES_DEFAULT=0` = 探针在二进制里、默认关、`R3R_DBG=1` 才开。**不需要改任何源码**即可建出这棵树。
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
