# R3R 第 159 轮备忘（2026-09-29）

本轮来源：玩家对第 158 轮遗留三个口径 + KI-253 时机的四条答复（q-0/q-1/q-2/q-3）。
本文是**临时工作备忘**，落定后条目并入 `R3R_KNOWN_ISSUES.md` 末尾「第 159 轮」小节。

---

## 一、四条拍板（原文口径）

### q-0（贰）= 控制段的定义改用「命令所有者段」
> 乙：控制段=命令所有者段（特质与排程跟命令所有者段走（字面实现「由控制段显现」）。
> 代价：链头不再是唯一门面，`R3RSegmentTraitRow` 的「控制段无行」约定要重写，链头需借用所有者段的 orders/特质。

- 现状（甲）：`R3RGetControlSegment(chain)` = **链头所在段**；第 152 轮 P1=甲的"强制不变式（重排 `r3r_priority` 让链头段恒为最低）"**尚未接线**，落地清单 ② 仍挂着。
- 新口径（乙）：`R3RGetControlSegment(chain)` = **`R3RGetPriorityHead(chain)`（`r3r_priority` 最小的段头 = 命令所有者）**。
  不再强制重排优先级；改为让"链头段的特质"退位、"所有者段的特质"上位。
- 连带：链头作为引擎唯一 tick 的载体（`ProcessOrders` / 车辆列表主行）必须**借用**所有者段的三特质
  （`unitnumber` / `name` / `group_id`），与既有排程借用层（`r3r_orders_borrowed` / `orders_backup`）同构。

### q-1（叁）= 不把车库拖动限定死，而是收紧「判定为挂接」的条件
> 我不希望把车库拖动限定死，我要求你收紧判断，严格的执行"双方没有非成段的部分才判定为挂接"，
> 或者我们这样，我们限定段不能和散车厢在同一条链，保证了有段参与的车库内拖动只可能是段和段之间。

### q-2（肆）= 挂接分组耦合时改**并集（OR）**
> (1) 并集 ABC（耦合时改成 OR（现在 `R3RNormaliseChainGroups` 是「取控制段掩码→清空每段头→重填」即覆盖），
> 广播各段头。代价：跨公司授权变宽、段级差异消失。

### q-3（壹）= KI-253 图像分离的出现时机
> 耦合后（挂上车底后立刻出现），具体情况可以看 `r3r_debug.log` 最后一次耦合场景（务必写入临时备忘）。

→ 取证见第二节。

---

## 二、KI-253 取证：`build\R3R_debug.log` 最后一次耦合场景

日志末尾的耦合是 **`veh=48`（机车）挂上等车底 `veh=6`**（前一次同型耦合是 `veh=50`，行 3030~3139，
症状同款，互为对照）。

### 1. 耦合本身（行 4213 ~ 4736）
| 行 | 内容 |
|---|---|
| 4213 | `FOLDCHK COUPLE n=21 worst_gap=76 A idx=48 y=1448 B idx=6 y=1526 dist=78` |
| 4246 | `FLIP-DONE head=21 nseg=1 whole=1`（尝试 2：逻辑翻等待车底，整链倒置） |
| 4254 | `FOLDCHK COUPLE-FLIP-U n=21 worst_gap=20 A idx=20 y=1468 B idx=15 y=1490 dist=22` ← **已出现 20px 假缝** |
| 4262/4270 | `FLIP-DONE head=48 nseg=1 whole=0` + `FOLDCHK COUPLE-FLIP-V ... worst_gap=69` |
| 4278/4281/4290 | 候选 3 双翻 → `FOLDCHK COUPLE-FLIP-BOTH ... worst_gap=20 A idx=20 y=1468 B idx=15 y=1490 dist=22` |
| 4735 | `RESPACE-AFTER-EDIT couple head=48 px=10 stretch=1 visited=20 capped=0` |
| 4736 | `COUPLE-OK loco=48 rear=8 consist=6 co=1 real=14 type=1 tx=60 ty=90 x=968 y=1447` |

⇒ 四个候选**全部没通过折叠判据**（最好的候选也留 `worst_gap=20`），最后由"最后一招拼接"接受；
图像分离正是这次**带缝接受**的直接后果，所以玩家看到的时机 = **挂上车底后立刻**。

### 2. 耦合前的等待车底是健康的（行 4265 `A3-ARR-BEFORE-U head=6`）
```
6 p=-1 n=7 | 7 | 8 | 9 | ... | 21 p=20 n=22 | 22 | 23 p=22 n=-1
```
18 节车（= 6 个三节铰接单元：{6,7,8} {9,10,11} {12,13,14} {15,16,17} {18,19,20} {21,22,23}），
`dir=3`、y 单调 `1526 → 1457`、节间距 ~5px —— **无重叠、无假缝**。

### 3. 耦合后立刻出现分离（行 4834~4854，`F%d` 物理链序 dump，列车正以 spd=21 北行）
```
F0  idx=48 y=1446   F1 idx=49 y=1450   F2 idx=50 y=1453
F3  idx=21 y=1455   F4 idx=22 y=1455   F5 idx=23 y=1455   ← 三节挤在同一点（完全重叠）
F6  idx=18 y=1476   F7 idx=19 y=1472   F8 idx=20 y=1467   ← 单元内 4~5px 正常
F9  idx=15 y=1489 …（23→18 之间跳 +21px）
…  每单元内部正常，**单元与单元之间恒跳 +21~+22px**（20→15、17→12、14→9、11→6 同款）
F18 idx=6  y=1526   F19 idx=7  y=1521   F20 idx=8  y=1516
```
- `flags=0x4000010`（`SegmentFlipped | 0x10`）铺满被挂车底每一节，`idx=21` 另带 `SegmentFront(0x2)`
  ⇒ 这些车都经过"折叠修正的逻辑翻转"。
- 行 4740 `CPL idx=21 … len=2 gap=-5 nom=5`：判据自身已量到**负缝（重叠）**。
- 结构塌缩：行 4789 `CHAIN-ATTRS head=48 n=21 FE=1 SEG=2`，行 4790/4795 `SEG idx=48` / `SEG idx=21`
  ⇒ 21 节只剩 2 个 ★；被挂的 6 个铰接单元被并成 1 段（`SEGTRAIT-SYNC-SEC tag=couple head=48 seg=3`）。
- 行 4796 `INVAR-CTRL tag=couple head=48 nseg=2 ctrl=48 ctrl_pri=2 owner=21 owner_pri=1 borrowed=1`
  —— **本轮的 q-0 现场样本**：链头 48 是控制段（甲），真正的命令所有者是 21。

### 4. 与本轮改动的关系
- `贰乙` 换口径后，同一现场应变成"控制段 = 21 那一段"，链头 48 借用 21 段的特质；
  `INVAR-CTRL` 这类"链头 vs 所有者分裂"的探针要改写（分裂是常态而非异常）。
- KI-253 本身（几何假缝/重叠）**不在本轮三条改动之内**，本轮只登记时机证据；
  真因候选：整链倒置翻转只倒**单元次序**、不反转**单元内部**（记忆：artic 块整体搬移），
  于是被挂链在"单元序倒置"后与保留位置的几何互相矛盾（单元内链序与整列行进方向相反、
  单元接缝被顶开 21~22px），`RESPACE-AFTER-EDIT` 的 `stretch` 又跳过了 artic 部件，
  第一单元 {21,22,23} 因此整组压在同一个像素上。

---

## 三、落地清单（本轮顺序：肆 → 叁 → 贰）

1. **肆**：`R3RNormaliseChainGroups`（`train_cmd.cpp`）由"取控制段掩码 → 清空每段头 → 重填"改为
   **取全链段头掩码并集（OR）→ 清空每段头 → 用并集重填**；读档侧 `R3RNormaliseChainGroupsAfterLoad`
   （`couple_group.cpp`）同步改 OR，保证读档后不变式一致。
2. **叁**：车库拖动/命令层收紧"判定为挂接"——只有**双方都不含非成段（散车厢）部分**时才算挂接；
   有段参与就必须段↔段。
3. **贰乙**：`R3RGetControlSegment` 改指命令所有者段；新增三特质借用（链头持有所有者段特质、
   自身特质停进自己的段行/备份）；`R3RSegmentTraitRow` 的"无行"判据重写为"本车即链头"；
   子行枚举由"非链头段"改为"非控制段"；`R3RSettleChainSegments` 里挂上借用步。

## 四、落地结果与锚点（全部已实现 + 已编译）

按 §三 的顺序 **肆 → 叁 → 贰乙** 落地；条目已并入 `R3R_KNOWN_ISSUES.md` 末尾「第 159 轮」小节，
分别登记为 **KI-258（肆）**、**KI-259（叁）**、**KI-260（贰乙）**，并已回填 KI-254 / KI-255 / KI-256 的状态为"已修（第 159 轮）"。

| 项 | 文件与锚点 |
|---|---|
| 肆 | `train_cmd.cpp:5515` `R3RNormaliseChainGroups()`（OR 累积 → 广播各非空段头；探针 `CGRP-NORM-UNION :5563`）、`couple_group.cpp:755` `R3RNormaliseChainGroupsAfterLoad()`（探针 `CGRP-NORM-LOAD :816` / 汇总 `GRP-NORM-LOAD-SUM :825`） |
| 叁 | `depot_gui.cpp:224` `TrainDepotChainHasLoosePart()`、`:242` `TrainDepotChainHasSegment()`、`:258` `TrainDepotSideIsLoose()`、`:280` `TrainDepotRefuseSegLooseMix()`；唯一漏斗 `TrainDepotMoveVehicle()` `:486`（探针 `DEPOT-SEG-MIX-SKIP :306`） |
| 贰乙 | `train_cmd.cpp:4392` `R3RGetControlSegment()` → `R3RGetPriorityHead()`；`:4415` `R3RCheckControlSegment()` 探针改写（`INVAR-CTRL :4431`）；`:4702` 新增 `R3RBorrowControlTraits()`（`CTRL-PARK :4733`）；`:4768` `R3RSyncSegmentTraits()` 特质来源改判；`:4898` `R3RSyncHiddenSegmentTraits()` 跳过链头段；`:4967` `R3RSettleChainSegments()` 挂上借用步；`couple_group.cpp:936` 新增 `R3RSegmentControlHead()`、`:968` `R3RSegmentTraitRow()` 判据重写、`:1097` `R3RSegmentReconcileRows()` 加 `r3r_orders_borrowed` 守卫、`:1165` `R3RSegmentHiddenHeads()` 改按"非控制段"枚举 |

叁 的实际判据（与 §三 行文的细微差别，以此为准）：`depot_gui.cpp:295-309` 先要求**至少一侧有段**，再要求**两侧整链都不沾散车厢**才放行 —— 即"从含散车厢的长链里拖出一个健康整段"也会被拒（`drag_loose` / `dst_loose` 量的是整链）。命令层 `CmdMoveRailVehicle` 未加同款校验（`TrainDepotMoveVehicle()` 是玩家可达的唯一漏斗）。

## 五、构建自证

复用既有 `_tmp_inc_build.cmd`（**未新建任何 `.cmd`**）：

- 护栏：`GUARD: incremental is safe`（未碰 `src/*.h` 与 `lang/*.txt`，增量合法）。
- 判决：`build\R3R_incbuild.done` = `EXIT_CODE=0`；日志无 `error C*` / `fatal error` / `FAILED:` / `build stopped`。
- 产物与时间戳（三者均早于 exe）：

  | 文件 | 源 mtime | obj mtime |
  |---|---|---|
  | `src\depot_gui.cpp` | 2026-09-29 18:37:37 | 19:13:01 |
  | `src\couple_group.cpp` | 2026-09-29 18:51:54 | 19:12:06 |
  | `src\train_cmd.cpp` | 2026-09-29 19:27:40 | 19:31:46 |

  `build\openttd.exe` @2026-09-29 **19:31:51**（51 534 336 B）。
- `read_lints`：`train_cmd.cpp` / `couple_group.cpp` / `depot_gui.cpp` 共 0 条诊断。
- 产物自证（`findstr` 命中 exe）：`CTRL-PARK`、`INVAR-CTRL`、`CGRP-NORM-UNION`、`CGRP-NORM-LOAD`、
  `GRP-NORM-LOAD-SUM`、`DEPOT-SEG-MIX-SKIP`、`SEGROW-RECONCILE`。
- 第一批（KI-257 + KI-253 探针）的构建自证见 `R3R_KNOWN_ISSUES.md` 第 159 轮小节内 KI-253 条目末段。
- **状态 = 已实现 + 已编译，游戏内复测待做**；复测判据见 `R3R_KNOWN_ISSUES.md` 第 159 轮小节 KI-258/259/260 各自的"未做 / 边界（等复测）"。
