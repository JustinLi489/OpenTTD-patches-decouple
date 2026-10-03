# R3R 临时分析报告 — 订单索引哨兵复测（第 208 轮 / 2026-10-03）

本轮**未改任何源码**。玩家三问：①KI-310「挂车命令被静默跳过」能否结案；②这套改动动了多少 OpenTTD 原生代码（会不会把屎山改坏）；③探针能否在编译发行版时关掉。

现场：`build\R3R_debug.log`（371 行，含第 207 轮新哨兵的会话），由 `build\openttd.exe`（51 634 176 B，2026-10-03 04:40，含 KI-312 的 16 个订单编辑哨兵）产生。

---

## 一、KI-310 结案判据：黑箱窗已关闭

### 1.1 订单编辑段（L232-239，veh=54）逐行

| 行 | site | real | impl | tt | n |
|---|---|---|---|---|---|
| 232 | `cmd-delete-in` | -1→0 | -1→0 | -1→0 | -1→2 |
| 233 | `ordlist-delete-in` | 0 | 0 | 0 | 2 |
| 234 | `ordlist-delete-out` | 0 | 0 | 0 | **2→1** |
| 235 | `cmd-delete-out` | 0 | 0 | 0→65535 | 1 |
| 236 | `ordlist-insert-in` | 0 | 0 | 65535 | 1 |
| 237 | `ordlist-insert-out` | 0 | 0 | 65535 | **1→2** |
| 238 | `cmd-skip-in` | **-1→1** | -1→1 | 65535 | 2 |
| 239 | `cmd-skip-out` | **1→0** | 1→0 | 65535 | 2 |

结论：

1. **删一条 + 插一条整个过程中 `real` 恒为 0**，没有出现 `0->1`。KI-312 复测判据 ① 所描述的签名（`ordlist-insert-in/out` 夹住 `real=0->1`）**未出现**——因为本次插入位置在索引之后（n 1→2，未触发上游 `InsertOrder()` 的 `if (sel_ord <= cur_real_order_index) ++`）。
2. 全日志 `real=1` 只出现在 `cmd-skip-in`（进入 `CmdSkipToOrder` 前已由该命令语义写为 1），随后 `cmd-skip-out` 回到 0 —— 这是**玩家手动"跳过命令"**，与 KI-310 第 207 轮结论一致。
3. 除 `cmd-skip` 外，**没有任何 site 出现 `0->1`** ⇒ KI-312 判据 ② 成立：订单编辑命令不再有黑箱写点，R3R 无隐藏写索引路径。
4. 哨兵分桶行为符合第 206 轮设计（每个 site 各自一条首行），8 行连排即为 8 个 site 的首次取样。

⇒ **KI-310 可结案（非缺陷，根因＝玩家重设调度触发上游 `InsertOrder()` 的索引后移语义）**。玩家 2026-10-03 自述「重设唯二调度里面的第一条，导致索引跳到了第二条」与代码语义逐字吻合。

### 1.2 本次会话暴露的**另一个**现象：为什么"挂不上"

与 KI-310 无关，是同一场景的新数据点：

```
332:CG-GATE compatible=0 coupler=54 target=0 cdecl=0 tdecl=1 cmask=0x0 tmask=0x1
359:CPL-GATE reject=target-not-wait site=depot act=54 tgt=57 aOrd=16 tOrd=0 aStop=0 tStop=1 grp=0
360:CPL-GATE reject=target-not-wait site=depot act=54 tgt=27 aOrd=16 tOrd=0 aStop=0 tStop=1 grp=0
361:CPL-GATE reject=pair-mismatch  site=depot act=54 tgt=0  aOrd=16 tOrd=17 aStop=0 tStop=0 grp=0
362:COUPLE-FAIL loco=54 order=16 tx=1 ty=15 x=20 y=248
365:COUPLE-DEST-EMPTY loco=54 tile=1,15 real=0 num=2 next=1 (waiting in place, order kept)
```

三条拒绝各有其因：

- **tgt=57**：其排程首条本就**不是** WAIT_COUPLE（`R3RDUMP` 命令表 i0 `type=6` GOTO_DEPOT），所以 `tOrd=0` + `tStop=1` ⇒ 拒绝合理。
- **tgt=27**：其排程 i0 **是** `type=17`（WAIT_COUPLE），且与 54 同在库 `1,15`，但 L312/L339… 每帧 `SKIP-STOPPED veh=27 order=0 real=17 spd=0 tile=1,15 parked=0` —— 被玩家**手动停止**（`vehstatus.Stopped`）⇒ `TrainLocoHandler` 在 Stopped 闸门就 return，`current_order` 永远停留在 `OT_NOTHING` ⇒ 挂接闸门读到 `tOrd=0`，判"它不是在等挂" ⇒ **永久不可作为挂接目标**。（对照组：同库的 veh 0 未被停止，L308 `DEPOT-ARR veh=0 real=0(17) curType=17` 正常加载 WAIT_COUPLE。）
- **tgt=0**（拖动合并链，27 节，链头 `OT_WAIT_COUPLE` 已加载）：它是**唯一有效候选**，被 `CG-GATE` 白名单否决——`tdecl=1`（等待命令上声明了「临时挂接分组」）、`tmask=0x1`（声明的是 0 号组），而 `cdecl=0`、`cmask=0x0`（54 既不在任何组里，其 `GOTO_COUPLE` 也没声明分组）⇒ `compatible=0` ⇒ 不建配对锁 ⇒ `CPL-GATE pair-mismatch`。

**关键对照（第 204 轮同一场景）**：那次成功挂接为 `CG-GATE compatible=1 coupler=54 target=0 cmask=0x1 tmask=0x1`（`cmask=0x1`）。本次 `cmask=0x0` ⇒ **veh 54 的 `GOTO_COUPLE` 上的「临时挂接分组」在重设调度后丢失了**（玩家本次是"删一条 + 插一条"：L232-237 的 `ordlist-delete-*` / `ordlist-insert-*`；订单窗口新建命令默认不带临时挂接分组）。这不是 R3R 的 bug，是**编辑后的数据**问题；修法是重新给 54 的 `GOTO_COUPLE` 设上同样的临时挂接分组（或去掉等待命令上的分组）。

---

## 二、改动触达原生代码的范围（本轮实测数字）

`git --no-pager diff --shortstat`（相对上次提交 `r3r-stable-2026-09-29`）：

```
28 files changed, 7841 insertions(+), 349 deletions(-)
```

改动文件清单（28 个，其中 `couple_group*` 为 R3R 新文件，其余均为**原生文件**）：

```
R3R_KNOWN_ISSUES.md
src/couple_group.cpp  src/couple_group.h  src/couple_group_type.h   ← R3R 新增
src/depot_gui.cpp  src/group_cmd.cpp  src/group_gui.cpp
src/order_base.h  src/order_cmd.cpp  src/order_gui.cpp  src/order_type.h
src/sl/couple_group_sl.cpp  src/sl/extended_ver_sl.cpp  src/sl/extended_ver_sl.h
src/sl/order_sl.cpp  src/sl/saveload.cpp
src/station_cmd.cpp  src/station_gui.cpp  src/strings.cpp
src/train_cmd.cpp  src/vehicle.cpp  src/vehicle_base.h  src/vehicle_cmd.cpp
src/vehicle_gui.cpp  src/vehicle_gui_base.h  src/vehiclelist.cpp
src/lang/english.txt  src/lang/simplified_chinese.txt
```

口径：**确实大面积动了原生代码**（+7841/−349 的形态说明以"插入新函数/新分支/新字段"为主，删除极少），并且更早的 `r3r-stable-2026-09-29` 提交是一次 52 files / +12166 −618 的量级。分两类：

- **A 类（纯探针/脚手架，可编译期关闭）**：`R3RDbgWrite()` + `R3RDbgOn()` 门禁；`R3R_PROBES=0` 时 `R3RDbgOn()` 变常量 false、`R3RDbgWrite(...)` 成 `((void)0)`，块内字符串与调用参数一起消失。
- **B 类（真行为改动，关不掉，是功能本体）**：如 `train_cmd.cpp` 的耦合/解挂/车库编辑提交点、`couple_group.cpp` 的分组语义、`order_*` 的临时挂接分组与解挂边界、`sl/*` 的新存档块，以及更早轮次的 `newgrf_engine.cpp`（position/length in consist 改段内计数）、`pbs.cpp`（预留归属查询重构 + 新增整格位查询）、`rail_map.h`/`bridge_map.h`/`tunnel_map.h` 等。这些分支全部挂在 R3R 特有前提（★/⊗ 段标记、couple group、R3R 专属命令）上，普通列车/普通订单走原生路径。

---

## 三、发行版探针开关：结论＝**基本能关，但有漏网的**

### 3.1 机制与实证

- 编译期两把开关：`R3R_PROBES_DEFAULT`（未显式指定时按 `_DEBUG` 推导：内测树 1、发行树 0；`R3R_release_build.cmd` 显式传 `-DR3R_PROBES_DEFAULT=0`，脚本内另有"build.ninja 里找不到该定义即中止"的硬闸门）→ `R3R_PROBES`（`src\r3r_perf.h:99-131`）。
- 运行期覆盖：`R3R_DBG` / `R3R_PERF`（发行版仍可用 `R3R_DBG=1` 现场开探针）。
- 实证（`findstr` 条件运算符写法，避免 `%ERRORLEVEL%` 解析期展开的老坑）：发行版 exe（`build-release\openttd.exe`，2026-09-20 13:53，22 755 328 B）内 **搜不到 `FOLDCHK`** ⇒ 旧探针的代码与字符串确实没进发行版。

### 3.2 漏网：第 205~207 轮的订单索引哨兵没有套门禁

- `src\train_cmd.cpp:1898 R3RWatchOrderIndex()`：函数体**没有** `if (!R3RDbgOn()) return;`，第一件事就是算 key、查写 `static std::unordered_map<uint64_t, R3RIdxSnap>`、并调 `R3RChainHasRealArticPart(t)`；末尾的 `R3RDbgWrite(...)`（:1931）在 `R3R_PROBES=0` 下变 `((void)0)`，**只丢日志与格式串，前面的记账与全链扫描留下**。
- 调用点**未加 `#if R3R_PROBES`**（`src\train_cmd.cpp:16733` 裸调用 `R3RWatchOrderIndex(consist, "loco-entry");`）⇒ 该批哨兵完全靠运行期 `R3RDbgOn()` 生效，而未加早退门。**（2026-10-03 第 209 轮更正）** 本行原文写"`src\order_cmd.cpp` 与 `src\train_cmd.cpp` 内 `#if R3R_PROBES` 出现次数均为 0"，该数字取自 `search_content(outputMode="count")`，而本环境**该模式会误报 0**（对同一行 `R3RDbgWrite("ORD-IDX-WATCH …`，`src\train_cmd.cpp:1931`，它报 0 匹配、`content` 模式命中 1 处）。改用 `content` 模式复核实测：`src\train_cmd.cpp` 为 **1 处**（:1695-1706，KI-144/145 探针宏的声明块，位置在哨兵之前且不含 :1898/:16733/:1931），`src\order_cmd.cpp` 为 **0 处**。⇒ 结论（第 205~207 轮哨兵未受编译开关保护，见 KI-315）**不变**，仅计数表述按实测更正。
- 规模：全树 26 个调用点（10 个在 `TrainLocoHandler` 每列车每 tick 路径上，含 `loco-entry`/`post-processorders`），订单编辑命令再加 6 对。发行版表现为"无日志、无字符串、但有每 tick 每列的 map 记账 + 铰接扫描"。
- 一行修法（未实施）：把 `if (!R3RDbgOn()) return;` 作为 `R3RWatchOrderIndex()` 的第一条语句；顺带也修掉 `R3R_DBG=0`（探针运行期关闭）基准下的这笔开销。

---

## 四、待玩家拍板 / 待办

1. **KI-313（低，待拍板）**：重设调度（删+插）后 `GOTO_COUPLE` 的「临时挂接分组」丢失 ⇒ 白名单拒挂。是否需要"复制/编辑命令时保留临时挂接分组"？现状属期望行为（新命令默认无分组）。
2. **KI-314（中，待拍板）**：被**手动停止**（`vehstatus.Stopped`）的等待车底永远不能被挂（`current_order` 恒 `OT_NOTHING`）。是否放宽为"链头排程当前是 WAIT_COUPLE 也算目标"？
3. **KI-315（低~中，待修）**：订单索引哨兵未受编译开关保护（见 §3.2）。
4. 可选：对 §二 的 B 类改动做一次"原生行为影响面"专项审计（逐文件列出"改动是否在无 R3R 前提下不可达"），供将来跟上上游版本时评估合并成本。
