# 第 236 轮临时分析报告：挂接目的地扫描的「无条件清点」探针（CPL-SCAN）

**主题**：回答「`CPL-PATHFOUND veh=23 found=0` 长期为 0」到底是"等待链不在场"还是"等待链在场但被闸门拒掉"。
**性质**：只加只读探针，**零行为变更**（未改任何判据 / 运动 / 位置 / 预留）。
**日期**：2026-10-10

---

## 一、输入与采样有效性

| 项 | 值 |
|---|---|
| 日志 | `build\R3R_debug.log`，247 628 B / 3 055 行 / LastWriteTime 2026-10-10 21:31:18 |
| 产生者 | `build\openttd.exe`，51 738 112 B / LastWriteTime 2026-10-10 21:03:46 |

`openttd.exe` 的 51 738 112 B / 21:03:46 与第 234 轮（KI-343 假双头）「构建自证」逐字一致 ⇒ 该日志由**第 234 轮 exe**产生，采样有效。

> 注意与第 235 轮的区分：第 235 轮小节（`R3R_KNOWN_ISSUES.md:10973`）的输入日志是 1 965 行 / 171 606 B / **21:17:02**；本报告分析的是**同一天更晚的第二次运行**（21:31:18），两者不是同一份文件，勿混判。

---

## 二、第 235 轮留下的缺口（本轮的出发点）

第 235 轮复测判定 KI-343 已收口，但日志里同时留下了这条**未定性**的现象：`veh=23` 长时间 `CPL-PATHFOUND found=0`，即 GOTO_COUPLE 寻路反复找不到合法终点。

上一轮（对话内）已用日志把「已确认」与「未确认」分开，结论是：

**已确认（日志字面可查）**

- 全日志 `TPR-BLOCKED` = **0** 条 ⇒ 该时段**不存在**"被其他列车占格阻挡"的形态；此前把它类比 KI-330（解挂后同格误判）**缺乏支持，已收回**。
- `veh=23` 的形态（`TPR-WALK` 共 34 条，末两条在 2466 / 2497）恒为：
  `mf=1092,86 origin=1092,86 td=1 okay=0 exit=1 new=1092,87 reach=16 spd=0 one=0 look=0`
  ⇒ 车**静止**在 `1092,86`；`reach=16`（非 `TRACK_BIT_NONE`）说明**前方可达轨位存在**；`okay=0` 说明当前预留不覆盖 origin。
- 失败持续：`CPL-PATHFOUND veh=23 found=0` 共 **23** 条（行 1674 → 2437）；转折点是 `2501-2503 PFD-ACCEPT kind=waiting tile=1082,136 t=22 ordType=17 wc=1`（等待方车底 22 到位）→ `2504 found=1` → `2506 CPL-BEST cost=19848` → `CPL-BACK` 回溯 → `2548 CPL-RESERVE veh=23 reserved=1 next=1`。
- 同期确有配对抗拒：`1707 / 2357 PFD-REJ-GROUP`（2357 为 `t=10 ordType=17 wc=1 rej=128`）、`1319 CPL-GATE pair-mismatch`、`666/670/706/2679 CPL-GATE target-not-wait`、`2431 RESV-NOBOOK veh=22 tile=1085,136 order=17 reason=parked-waiter`。
- 日志末尾未稳定：`COUPLE-FAIL loco=23 order=16 tx=1112 ty=125` + `CPL-PAIR act=23 tgt=22 dist=38`。

**未确认（不能断言）**

- `found=0` 的**根因**。因为 `PFD-ACCEPT` / `PFD-REJ-*` 都是**条件打印**：只有"探到站台格 → 格上确有车底 → 判据走到对应分支"才出声。于是在 23 次 `found=0` 的区间里，日志只留下 1707、2357 两条 `PFD-*`——**样本不足以区分**"YAPF 根本没探到站台 / 格上没有车底"与"在场但被 GROUP/FIT/TARGET/ORDER 拒掉"。这正是本轮要补的探针。

---

## 三、探针设计（本轮落地）

### 3.1 落点

| 文件 | 位置 | 内容 |
|---|---|---|
| `src/pathfinder/yapf/yapf_destrail.hpp` | 文件头（`class CYapfDestinationRailBase` 之前） | 命名空间级 `inline` census 变量（11 个计数器 + 3 个"最近样本"字段）+ `R3RCoupleScanReset()` + `R3RCoupleScanDump()` |
| 同上 | `CYapfDestinationTrainRailT::PfDetectDestination(TileIndex, Trackdir)` | 四个早退点各自计数：入口 `enter`、非站台/车库 `noplat`、无预留 `nores`、格上无车底 `noconsist`；过了闸门那一刻 `consist` |
| 同上 | 文件内两个 lambda `r3r_dest_reject` / `r3r_dest_accept` | 逐次计数（**在节流 `% 64` 之前**，否则失败现场只剩 1/64 样本）；并记录"最近一次扫到的车底"（格 + 车号 + 结局） |
| `src/pathfinder/yapf/yapf_rail.cpp` | `CYapfTrainRailT::FindNearestCoupleTrain()`：`R3RResvPhaseGuard` 之后 | `R3RCoupleScanReset()` |
| 同上 | `CPL-PATHFOUND` 那一块之后、`if (!path_found)` 之前 | `R3RCoupleScanDump(veh, found, closed, limit)` —— **无条件**，每次寻路恰一行 |

`closed` / `limit` 取自 YAPF 自身的节点统计（`Yapf().nodes.ClosedCount()` / `Yapf().GetMaxSearchNodes()`），用于区分"搜索真穷尽"与"节点上限把搜索截断"。

> 落点修正：`nodes` 是 `CYapfBaseT` 的 `public` 成员，可直接 `Yapf().nodes.ClosedCount()`；但 `max_search_nodes` 是 `protected`，而 follow 类 `CYapfFollowCoupleRailT` **不是** `CYapfBaseT` 的派生类（它是 `Tpf` 的基类），无法直接读 ⇒ 在 `yapf_base.hpp` 的 `CYapfBaseT` 里新增 `public` 只读访问器 `GetMaxSearchNodes()`。首编译失败正是这一点（`error C2039: "max_search_nodes": 不是 … 的成员` 与 `error C2660: R3RCoupleScanDump: 函数不接受 2 个参数`）。

### 3.2 输出格式

```
CPL-SCAN veh=%d found=%d closed=%u limit=%u enter=%u noplat=%u nores=%u noconsist=%u consist=%u rej=%u/%u/%u/%u acc=%u/%u last=%d,%d lastveh=%d why=%s
```

| 字段 | 含义 |
|---|---|
| `closed` / `limit` | YAPF 已扩展节点数 / 节点上限（`limit=0` 表示无限）。`closed == limit != 0` ⇒ 搜索被**截断**，此行的 `found=0` **不能**当作"候选不在场" |
| `enter` | `PfDetectDestination` 调用次数（节点级：节点扩展时一次 + `PfCalcEstimate` 时一次，故约 2× 参与判定的节点数） |
| `noplat` | 早退：不是站台/车库格 |
| `nores` | 早退：站台格但没有预留位且非站台分支 |
| `noconsist` | 早退：格合格但**格上扫不出合法车底** |
| `consist` | 扫到车底、进入三道闸门 |
| `rej=G/F/T/O` | 四道闸门各自的拒绝次数：GROUP / FIT / TARGET / ORDER |
| `acc=car/wait` | 两种接受分支次数：car-only / waiting |
| `last=tx,ty` / `lastveh` | **最近一次**扫到的车底所在格与链头车号（`-1,-1 / -1` = 本次寻路压根没扫到任何车底） |
| `why` | 最近一次样本的结局：`none` / `GROUP` / `FIT` / `TARGET` / `ORDER` / `acc-car` / `acc-wait` |

### 3.3 判读表（把 `found=0` 拆成互斥的四种形态）

| 形态 | `CPL-SCAN` 特征 | 结论 |
|---|---|---|
| A 搜索被截断 | `closed == limit != 0` | 没搜完，本轮结论作废，需先放宽 `max_search_nodes` 再谈候选 |
| B 没探到站台 | `enter > 0`、`noplat` 占绝大多数、`nores=noconsist=consist=0`、`last=-1,-1` | YAPF 的可达节点里**根本没有站台格** ⇒ 问题在可达性/通道，不在候选判据 |
| C 探到站台但格上没车底 | `noconsist > 0`、`consist = 0`、`last=-1,-1` | **等待链不在场** ⇒ 属"上游没把等待方送到位"，与第 234 轮的 `PFD-ACCEPT kind=waiting` 转折点互为印证 |
| D 车底在场但被拒 | `consist > 0`、`rej` 合计 `> 0`、`why != none` | **在场被拒**，且 `why` 直接给出是哪道闸门（GROUP=分组、FIT=装货/货种/车数、TARGET=配对目标、ORDER=订单） |

> 形态 C 与 D 互斥且**穷尽**了"YAPF 已探到站台"的全部可能，因此本轮日志之后不再需要靠推测填空。

### 3.4 已知口径限制（须在判读时记住）

1. **`enter` 是调用次数不是格数**，`PfCalcEstimate` 会重复调用 ⇒ `enter ≈ 2×` 节点数；各分支计数同样按调用次数累计，只作**同一次寻路内的相对比较**。
2. **"最近样本"只保留最后一次**：一次寻路里若既有接受又有拒绝，`last/why` 只反映**最后**那个。要看逐次明细仍需 `PFD-REJ-*`（节流 1/64）与 `PFD-ACCEPT`（无条件）。
3. **只覆盖 `CYapfDestinationTrainRailT`（挂接寻路）**：不覆盖普通 GOTO_STATION/GOTO_DEPOT 寻路。
4. `CPL-SCAN` 与既有 `CPL-PATHFOUND` 同频（每次寻路一行），但 `CPL-ENTRY` 只在进入路径选择前打一次 —— **有 `CPL-ENTRY` 而完全没有 `CPL-SCAN`** 的可能性只存在于 `CYapfCoupleRail*::stFindNearestCoupleTrain` 之前就返回的情形（`FindNearestCoupleTrain` 内部在 reset 与 dump 之间无早退，故只要进入该函数必有 `CPL-SCAN`）。

---

## 四、未确认项

- **U-1**：`veh=23` 在 `1092,86` 的 `reach=16` 具体是哪一条 trackbit（`reach` 是位掩码，16 未进一步展开）；本轮未加逐位 dump。
- **U-2**：车底 22 从 `1085,136`（2431 `RESV-NOBOOK`）走到 `1082,136`（2501 `PFD-ACCEPT`）的动因未确认（是否与 `RESV-NOBOOK` 的门禁放开直接相关）。
- **U-3**：`CPL-GATE target-not-wait` 与 `PFD-REJ-GROUP` 在形态 D 里的主次关系未知（需 `CPL-SCAN` 的 `rej` 分布来定）。
- **U-4**：末尾 `tx=1112 ty=125` 的 `COUPLE-FAIL` 与 `1092,86` 那段是否同源。

---

## 五、复测判据

1. **`CPL-SCAN` 每次寻路恰一行**，且与同一 tick 的 `CPL-PATHFOUND` 配成对（`veh` / `found` 逐字一致）。
2. 复现 `veh=23` 类场景（或任意长时间 `found=0`）时，`CPL-SCAN` 的 `why` 必须**不是** `none`，即能判进 §3.3 的 A/B/C/D 之一。
3. **负判据**：正常挂接成功的那次寻路，`acc` 至少一路 `> 0`，且 `why` 为 `acc-car` 或 `acc-wait`。
4. **负判据**：车库/普通轨道场景不产生 `CPL-SCAN`（该类寻路不走 `CYapfDestinationTrainRailT`）。
5. **性能**：`CPL-SCAN` 每寻路一行，日志增幅应与 `CPL-PATHFOUND` 同量级（不得出现刷屏级别的行数增长）。
6. 形态 D 若出现，`rej=G/F/T/O` 的四分量应能与同时段的 `PFD-REJ-*` 行对上（后者是 1/64 抽样）。

---

## 六、构建自证

- **入口**：复用工作区既有 `_tmp_inc_build.cmd`（`Start-Process cmd.exe /c _tmp_inc_build.cmd`，WorkingDirectory = 工作区根）；**未新建任何 `.cmd`**。
- **护栏**（`build\R3R_incbuild.guard.log`）：本轮改了两个头文件，
  ```
  GUARD: 2 header/lang file(s) are newer than the newest object file
  GUARD: newest object = 2026-10-10 22:32:55
  GUARD:   newer: src\pathfinder\yapf\yapf_base.hpp   (2026-10-10 22:37:58)
  GUARD:   newer: src\pathfinder\yapf\yapf_destrail.hpp (2026-10-10 22:37:42)
  GUARD: REMOVED 620 object file(s) - upgrading this build to a FULL rebuild
  ```
  ⇒ 如预期转**全量**（692 步），不是增量。
- **结果**：`[692/692] Linking CXX executable openttd.exe`（日志尾部为 691 → 692 步）。
- **判决**：`build\R3R_incbuild.done` = `EXIT_CODE=0`；日志内 `error C*` / `fatal error` / `FAILED:` / `build stopped` 计数 0。
- **时间戳链**（`obj > 源码` 且 `exe > obj`）：
  | 文件 | 时间 | 大小 |
  |---|---|---|
  | `src\pathfinder\yapf\yapf_destrail.hpp` | 2026-10-10 22:37:42 | 26 425 B |
  | `src\pathfinder\yapf\yapf_rail.cpp` | 2026-10-10 22:37:55 | 79 049 B |
  | `build\CMakeFiles\openttd_lib.dir\src\pathfinder\yapf\yapf_rail.cpp.obj` | 2026-10-10 22:46:19 | — |
  | `build\openttd.exe` | 2026-10-10 23:01:30 | 51 740 672 B |
- **产物自证**：`findstr CPL-SCAN build\openttd.exe` 命中 ⇒ 新探针字面量确在本次 exe 内。
- **`read_lints`**：`yapf_destrail.hpp` / `yapf_rail.cpp` / `yapf_base.hpp` 三文件 0 条。
- **性质**：只读探针，零行为变更；构建只证明"编进去了"，**行为是否如 §3.3 判读表所设想，仍须游戏内复测**（§五 六条判据）。

### 6.1 落盘时的一处施工记录（供复现）

`yapf_base.hpp` 的 `max_search_nodes` 是 `protected`，而 follow 类 `CYapfFollowCoupleRailT` **不是** `CYapfBaseT` 的派生类 ⇒ 首编译报
`error C2039: "max_search_nodes": 不是 "CYapfFollowCoupleRailT" 的成员` + `error C2660: R3RCoupleScanDump: 函数不接受 2 个参数`。
修法：在 `yapf_base.hpp` 的 `CYapfBaseT` 加 `public` 只读访问器 `GetMaxSearchNodes()`；`nodes` 本身已是 `public`，故 `Yapf().nodes.ClosedCount()` 无需包装。
