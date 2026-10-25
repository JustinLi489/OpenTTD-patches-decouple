# 第 296 轮：分端日志 + 探针收放（2026-10-25）

玩家原话（本轮任务边界）：

> 那真的有点可惜了，需要你调整好探针（加必要的，屏蔽不必要的），再编一个 exe，然后把它分别放进
> srv 和 cli 文件夹，我才能两端跑日志，不然你知道的，现有的 R3R_debug.log 会相互写入
> （你还是看看既有日志能不能看吧）

本轮**只改探针 + 日志出口**，不改任何列车/耦合/克隆行为。

---

## 一、既有日志能不能看：部分能看，但恰好缺了三条问题各自的判据

样本：`build\R3R_debug.log`，4.9 MB / 41 658 行（2026-10-24 23:46 产生）。

### 1.1 行数分布（按行首 token 统计）

| 标签 | 行数 | 占比 | 有用性 |
|---|---:|---:|---|
| `SPDD` | 19 291 | 46.3% | 无（每车每 tick 速度 dump） |
| `GRF-WEIGHT` | 8 381 | 20.1% | 无（每次 NewGRF 重量查询） |
| `CARGOWEIGHT-VEH` | 6 144 | 14.7% | 无（每次货物重量重算） |
| `GEO` | 1 848 | 4.4% | 低（提交点整链几何 dump，需要时才要） |
| `SEGFE-VEH` | 1 826 | 4.4% | 低（逐车段头探针） |
| 其余（含以下有用项） | 4 168 | 10.0% | — |

前五项合计 **37 490 行 = 90.0%**，且**全部走裸 `fprintf` 出口**（`R3RFopenDbg`），
不受标签白名单控制 —— 所以"过滤"以前只过滤了那 10%。

有用项实测行数：`CPL-PAIR` 411、`RESV-WATCH` 106、`CRT` 119、`DEPOT-ARR` 59、`TRP` 30、
`COUPLE-OK` **5**、`CPL-GATE` **6**、`SEGLOAD-` 6、`ORD-` 5、`UNRES-RAIL` 3。

### 1.2 三条问题各自的判据缺口

| 问题 | 需要的行 | 既有日志 | 结论 |
|---|---|---|---|
| ①挂接太慢 | `CPL-PAIR`→`CPL-HIT`→`COUPLE-OK` 三段耗时 | `CPL-PAIR` 411、`COUPLE-OK` 5、**`CPL-HIT` 0** | 只能看到"试了很多次、成了 5 次"，看不到每次卡在哪一段 |
| ②一对完全不挂 | `CPL-GATE reject=` 分类计数 | **仅 6 行**（128 帧边沿窗口），且 `COUPLE-FAIL` **0 行** | 能定性（`pair-mismatch` / `target-not-wait`），不能定量 |
| ③克隆得 5、6 号 | `UNIT-NEW id= ... free_next=` | **`UNIT-NEW` 0 行** | **完全不可判** |

`UNIT-NEW` 为 0 的原因不是没触发，而是 `src\vehicle_cmd.cpp:251` 用的是
`R3RDbgWrite("UNIT-NEW ...")`，而 `R3RDbgTagAllowed()` 的白名单（第 274 轮为排查预留而设）
只放行 `RESV*` / `UNRES*` / `EXTEND*` / `LOOP*` / `R3R-RES*` —— `UNIT-*` 被 `:295` 的
`return` 吞掉。这是本轮必须修的第一件事。

### 1.3 双写实证（支持玩家的抱怨）

同一 tick 同一 payload 的 `CPL-PAIR` 连续出现两遍：

```
CPL-PAIR tick=2422 act=21 tgt=24
CPL-PAIR tick=2422 act=51 tgt=24
CPL-PAIR tick=2422 act=99 tgt=75
CPL-PAIR tick=2422 act=21 tgt=123
CPL-PAIR tick=2422 act=21 tgt=24      <- 与第一行逐字相同
CPL-PAIR tick=2422 act=51 tgt=24
...
```

`CPL-PAIR` 走 `R3RDbgEdge` 边沿触发（同 key 同 payload 只打一次），因此**两遍只能来自两个进程**
（srv 与 cli 各自内存状态独立）。⇒ 玩家说的"相互写入"成立。

---

## 二、本轮改动清单

### 2.1 `src\r3r_perf.h`（新增两个开关）

| 新增 | 作用 |
|---|---|
| `R3RDbgFileName()` | 日志文件名，默认 `"R3R_debug.log"`，可用环境变量 `R3R_DBG_FILE` 覆盖（`<64` 字节）。`R3RFopenDbg()` 与 `R3RDbgWrite()` 两个出口都改用它 |
| `R3RDbgBulkOn()` | 批量探针总闸，默认 **OFF**，`R3R_DBG_BULK=1` 打开。`R3R_DBG=0` 仍然压倒一切 |

### 2.2 白名单（`R3RDbgTagAllowed()`）扩到三条问题够用为止

新增放行前缀（每条都注明用途，且全部是事件级或调用点已节流的标签）：

- 耦合：`COUPLE` / `CPL` / `FOLD` / `SPLICE` / `ARTIC` / `SKIP-`
- 克隆号：`UNIT` / `CLONE`
- 排程与共享表：`ORD` / `CTRL` / `AUTO-` / `OWNER` / `NAME`
- 门槛与段簿记：`RESERVE` / `DECOUPLE` / `SPD` / `DUALHEAD` / `TTB` / `DEPOT` /
  `SEGID` / `SEGROW` / `LEGACY` / `CGRP` / `GRP` / `R3R-`

**刻意不放行**：`POSCONSIST`、`DEARTIC`、`SEGTR`、`GEO`、`SEGFE`、`SPDD`、`GRF-WEIGHT`、
`CARGOWEIGHT`。前三个走裸出口、量小（各自 200~340 行），留着；后五个走裸出口、量巨大，见下。

### 2.3 五个批量探针改受 `R3RDbgBulkOn()` 控制（默认关）

| 站点 | 标签 | 既有行数 |
|---|---|---:|
| `train_cmd.cpp`（`TrainLocoHandler` 内） | `SPDD` | 19 291 |
| `newgrf_engine.cpp`（`PROP_TRAIN_WEIGHT`） | `GRF-WEIGHT` | 8 381 |
| `ground_vehicle.cpp`（`UpdateCargoWeight` 附近） | `CARGOWEIGHT-VEH` | 6 144 |
| `train_cmd.cpp`（`R3RDumpChainGeometry` 入口与快照 dump） | `GEO` / `GEO-END` | 1 848 + 87 |
| `train_cmd.cpp`（`SEGFE-VEH`） | `SEGFE-VEH` | 1 826 |

预计改造后同场景日志从 41 658 行降到 **约 4 000~5 000 行**（剩下的是 `CPL-*` / `COUPLE-*` /
`RESV-*` / `CRT` / `DEPOT-ARR` 这类真事件），再叠上本轮新放行的 `UNIT-*` / `CPL-*` / `ORD-*`。

### 2.4 三处"必要的新字段"（只读，直接服务 KI-467/468/470）

1. `CPL-GATE` 行追加 `tOwn=` `tShared=` `tFollow=` `aFollow=` `aShared=`，并把
   `tShared/tFollow/aFollow/aShared` 编进边沿触发的 payload ——
   这样"同一对被共享调度改变了表关系"会**重新打一行**，不会被 128 帧窗口吃掉。
2. `ORD-AFTER-COUPLE` 行追加 `own_shared=` `own_first=` `u_shared=` `u_follow=` ——
   合并后链头自己的表是否还在原生共享环里（`own_first` 是环上另一辆车），
   这是 KI-470 的直接判据。
3. `ORD-XFER keep` 行追加 `own_shared=` `own_first=` `owner_shared=` ——
   第二次耦合（自己保留原表、只挂跟随指针）时的同一组读数。

### 2.5 两端日志隔离（部署方式）

日志名是**相对工作目录**的，所以：

- 各目录各放一份 exe，**以该目录为工作目录启动** ⇒ 天然各写各的
  （`build\srv\R3R_debug.log` 与 `build\cli\R3R_debug.log`，`R3R_perf.log` 同理）。
- 若必须从同一目录启动两端，则分别设 `R3R_DBG_FILE=srv.log` / `R3R_DBG_FILE=cli.log`。

本轮交付：同一个 `build\openttd.exe` 复制到 `build\srv\` 与 `build\cli\`。

---

## 三、构建自证

复用既有 `_tmp_inc_build.cmd`（未新建任何 `.cmd`）。因改动 `src\r3r_perf.h`，
`R3R_inc_guard.ps1` 应判"header newer than newest object" ⇒ 转全量（约 692 步）。

> 待回填：`build\R3R_incbuild.done` 的 `EXIT_CODE`、日志尾部 `[n/n] Linking`、
> `build\openttd.exe` 时间戳与大小、错误计数、`read_lints`。

### 3.1 首次全量构建其实失败了（"已完成"是误判）

玩家说"全量重编已经完成"，实际 `build\R3R_incbuild.done` = `EXIT_CODE=1`（00:30），
`build\openttd.exe` 仍是 10-24 23:28:54 的旧件。两批编译错误，都是新探针里 API 用错：

| 批次 | 位置 | 错误 | 修法 |
|---|---|---|---|
| 一（5 处） | `train_cmd.cpp:12866/:13251/:13254` 等 | `C2039: "IsOrderListShared" 不是 "OrderList" 的成员` | `IsOrderListShared()` 属 `Vehicle`（`vehicle_base.h:893`）；`OrderList` 的谓词是 `IsShared()`（`order_base.h:1742`）。`xxx->orders->IsOrderListShared()` → `xxx->IsOrderListShared()` |
| 二（2 处） | `train_cmd.cpp:12601/:12865` | `C2248: "OrderList::first_shared" 无法访问 private 成员` | 改用公开访问器 `Vehicle::FirstShared()` |

### 3.2 最终构建结果

- guard = `GUARD: incremental is safe`（`r3r_perf.h` 已在前一次全量编入，只剩 `train_cmd.cpp` 重编）
- 日志尾部 = `[24/24] Linking CXX executable openttd.exe`
- `build\R3R_incbuild.done` = `EXIT_CODE=0`（00:42）
- `error C*` / `fatal error` / `FAILED:` / `build stopped` 计数 = 0
- `src\train_cmd.cpp` 00:39 → `build\openttd.exe` **00:41，52 325 376 B**
- `read_lints(train_cmd.cpp)` = 0
- exe 字面量（findstr 对二进制不可靠，改用 .NET Latin-1 读字节 + `Contains`）：`UNIT-NEW` True、`own_shared=%d own_first=%d` True、`tShared=%d tFollow=%d` True

### 3.3 部署

`build\openttd.exe` 已复制到 `build\srv\openttd.exe` 与 `build\cli\openttd.exe`，
两处均为 00:41 / 52 325 376 B（此前是 10-17 00:00 的 51 882 496 B 旧件）。复制前已确认无进程占用。

---

## 四、复测判据

1. `build\srv\R3R_debug.log` 与 `build\cli\R3R_debug.log` 各自独立、都非空，且同一 tick 的
   `CPL-PAIR` **不再出现逐字重复的两遍**（同一进程内边沿触发不会重复）。
2. 日志总量显著下降：同规模会话应 ≪ 5 MB；`grep -c SPDD`、`GRF-WEIGHT`、`CARGOWEIGHT-VEH`
   在未设 `R3R_DBG_BULK=1` 时应为 **0**。
3. 克隆两列车后，日志出现 **恰好 2 行** `UNIT-NEW id=<3> / id=<4>`（判 KI-469）；
   若 4 行（3,4,5,6）＝号被分配两次；若 2 行但 id=5,6 且 `free_next` 克隆前已是 5 ＝历史漂移。
4. 挂接过程中能看到 `CPL-PAIR` → `CPL-HIT` → `COUPLE-OK` 三段（KI-467 的分段耗时前提）。
5. 不挂的那一对能看到**稳定的** `CPL-GATE reject=...` 且带 `tShared/tFollow/aShared/aFollow`，
   能直接判"是不是共享表导致 `tOrd` 与 `aOrd` 永远不同步"（KI-470）。
6. 负判据：普通单车（无共享、无克隆）挂接行为逐字不变；`R3R_DBG_BULK=1` 时五个批量探针仍能打开。

---

## 五、未做 / 待玩家拍板

- KI-470 的处理口径仍未拍板（A 整环改跟随 / B 先退共享再跟随 / C 拒绝耦合），本轮不动行为。
- 未做"`CPL-HIT` 是否存在"的代码确认补齐 —— 若第 4 条复测发现 `CPL-HIT` 仍为 0，
  说明命中阶段确实从不触发，下一轮再加命中侧探针。
