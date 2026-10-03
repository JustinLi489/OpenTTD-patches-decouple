# R3R 第 158 轮备忘：玩家四问（图像分离 / 特质归属 / 车库拖动 / 挂接分组）

- 记录时间：2026-09-29
- 来源：玩家原话四条 —— 壹「日志尾部列车的图像分离的像被狗啃了一样，怀疑是折叠」；贰「列车编号和名称应该属于列车特质、由控制段显现，而目前我看到的是依旧由链头决定」；叁「车库内仅有段参与的拖动应当视作耦合/解耦，我怀疑这里你的判定没收紧」；肆「挂接分组应当怎么处理，是当作列车的特质还是物理属性？就像 AB 挂 AC，耦合后的链应当属于 AB、AC 还是 ABC？」
- 玩家要求：**务必记录进备忘**（本文件）。同时按长期规则在 `R3R_KNOWN_ISSUES.md` 末尾落了「第 158 轮」小节与 KI-253~KI-256。
- 现场日志：`build\R3R_debug.log`（675 303 B / 9 914 行，本轮复测那次）。
- 本轮**未改任何源码**（四问里贰/叁/肆都需要玩家先拍板口径；壹需要先加取数探针）。

---

## 0. 结论速览

| 问 | 玩家的猜测 | 代码/日志结论 | 性质 | 待拍板 |
|---|---|---|---|---|
| 壹 | 是折叠（fold） | **折叠判据无辜**：现场相邻车实测 gap 82~83px，远大于 `gap>8` 阈值。真症状是「段划分只剩 2 段（应有 7 个铰接单元）+ 段内各车坐标完全重合 + 段间 82~83px 大空隙」 | 未修（需先加取数探针） | 无需拍板，但需玩家提供「分离瞬间」的复现场景（耦合？翻转？车库拖动？） |
| 贰 | 编号/名称仍由链头决定 | **完全成立，且是"控制段=链头段"（P1-甲）的必然结果**：`R3RSegmentTraitRow()` 对链头所在段**恒返回 nullptr** ⇒ 控制段的车号/名称/分组永远直读链头车的字段 | 设计口径冲突，非 bug | **要拍板**：甲（维持链头=唯一门面）还是乙（特质跟命令所有者段走） |
| 叁 | 车库"仅段参与"的拖动判定没收紧 | **成立**：判定只有"这辆车在段内"（`TrainDepotGetSegmentFront`），没有"被拖块 == 完整段"、"块跨段"、"拖出整段=解耦 / 拖入别链=耦合"这三种判据；全仓没有任何 `Commands::Couple` / `DecoupleTrain` 调用，耦合语义是**模仿**出来的 | 未修（需收紧口径） | **要拍板**：允许的拖动单位口径 |
| 肆 | （询问语义） | 现状：**按段存、按车落地、耦合时被"等待方控制段掩码覆盖全链"**（不是并集）⇒ AB 挂 AC 的结果 = **看谁是等待方**，另一方分组**被丢弃** | 语义未定 | **要拍板**：并集(ABC) / 段级保持(AB+AC) / 现状覆盖 |

---

## 1. 壹：图像"被狗啃"——不是折叠，是段划分塌缩 + 段内坐标重合

### 1.1 现场（`build\R3R_debug.log` 尾部 dump）

链 head=48，共 21 节，物理链序（`F` 行 = `Next()` 顺序）：

```
48, 49, 50, 21, 22, 23, 18, 19, 20, 15, 16, 17, 12, 13, 14, 9, 10, 11, 6, 7, 8
```

同一批车的像素坐标（`F`/`MV` 行的 x,y）：

| 车 | tile | x,y | 备注 |
|---|---|---|---|
| 48 | 59,79 | 952,1268 | 链头（真引擎 `loco=48`） |
| 49 | 59,79 | 952,1272 | 展开正常（差 4px） |
| 50 | 59,79 | 952,1275 | 展开正常 |
| 21 | 59,79 | 952,1277 | **与 22、23 完全同坐标** |
| 22 | 59,79 | 952,1277 | 重合 |
| 23 | 59,79 | 952,1277 | 重合 |
| 18 | 60,85 | 968,1360 | **与 23 相隔 83px**，且 19、20 与它重合 |
| 19 | 60,85 | 968,1360 | 重合 |
| 20 | 60,85 | 968,1360 | 重合 |
| 15,16,17 | 60,86 | 968,1382 | 三节重合 |
| 12,13,14 | 60,87 | 968,1404 | 三节重合 |
| 9,10,11 | 60,89 | 968,1426 | 三节重合 |
| 6,7,8 | 60,90 | 968,1448 | 三节重合 |

即：**每三节一坨（坐标完全相同），坨与坨之间留着 82~83px 的空档**；只有链头那三节（48/49/50）是正常展开的。

链条自己的自检探针把这两件事都印出来了：

```
TTB-PROBE fold-geom veh=18 tile=60,84 x=968 y=1359 prev=23 ptile=59,79 px=952 py=1277 ... first=48 mvfront=48 db=0 speed=120
```

—— 相邻两车（`prev=23` 与 `veh=18`）的坐标差 = (16, 82)，也就是 **gap ≈ 82/83px**。

### 1.2 段划分也塌了

`CHAIN-ATTRS head=48 n=21 FE=1 SEG=2 spd=0`。

探针语义已核实（`src/train_cmd.cpp:9378-9387`）：`SEG` = 链上 `IsSegmentFront()` 的车数（**含链头**）。所以这条 21 节的链**全链只有 2 个 ★ ⇒ 只有 2 段**。

而车号规律（48/49/50、21/22/23、18/19/20、15/16/17、12/13/14、9/10/11、6/7/8）说明现场是 **7 个三节铰接单元**（`F1 idx=49 flags=0x2000000`、`F2 idx=50 flags=0x2008000` 即 artic part 标志）。**7 个物理单元却只有 2 段**：段划分与物理单元完全脱节。

### 1.3 定性

1. **折叠（fold）判据不背这个锅**：`R3RCheckChainFoldedDirection` 的判据是 `gap > 8` 或方向点积 > 0 就算折叠；现场 gap 82/83px 远超阈值，探针只是"报告者"，它看到的是"巨大缝隙"，不是"折叠"。
2. 真症状两条：
   - **段划分丢了一半以上**（7 个铰接单元 → 2 段），于是"逐段反转 / 按段重排"用的是错误的段边界；
   - **每一坨车的坐标完全重合**（段内没有按车长展开），**坨与坨之间 82~83px**（应该紧贴）。
3. 该链确实跑过翻转路径：`FLIP-V veh=48 p=49 ... x=968 y=1448 tile=60,90` → `FLIP-DONE head=48 nseg=1 whole=0`（耦合前的整段逻辑翻），随后 `COUPLE-OK loco=48 rear=8 consist=6 co=1 real=14 tx=60 ty=90`、`RESPACE-AFTER-EDIT couple head=48 px=10 stretch=1 visited=20 capped=0`。翻转只改 direction / 链序 / ★，**不改位置**，所以"翻转后的车仍停在原地"是这一现象的天然来源之一，但**不足以解释段内重合**（位置推进应按 `cached_veh_length` 逐节展开）。
4. 与既有条目同族：KI-214（耦合后包围盒/位置错位，现场在 idx23↔idx24 接缝）是本条的轻症版。本轮现场是"整段塌缩 + 跨段断口"的重症版。
5. 需要排除/确认的第三因素：`CHAIN-ATTRS ... len=0 pow=0 wt=0`（该探针打印 `v->gcache.cached_total_length` 等）。**耦合探针可能打在 `ConsistChanged()` 之前，故 `len=0` 尚不能当结论**；但若在 `ConsistChanged()` 之后仍为 0，则"链长为 0"就是位置推进无法展开的直接解释 —— 这一条列为本轮第一项取数任务。

### 1.4 下一步取证方案（下轮直接做，不需拍板）

1. 新增/扩充探针（只加打印，不改行为）：
   - 逐车 `cached_veh_length / cached_total_length / IsArticulatedPart / IsSegmentFront / IsSegmentBack / r3r_segment_id`；
   - `RESPACE-AFTER-EDIT` 前后各 dump 一次每节 `(tile, x, y)` 与"期望间距"；
   - `FLIP-DONE` / `R3RFlipChainBySegments` 出口 dump 每段段头/段尾的 `(tile, x, y)`；
   - `COUPLE-OK` 之后（**确认在 `ConsistChanged()` 之后**）再打一次 `CHAIN-ATTRS`。
2. 判据（用于判定修好没有）：段数 == 物理铰接单元数（本场景 7）；段内相邻车间距 ≈ 各自车长；段间紧贴（≤ 2px）；`CHAIN-ATTRS len != 0`。
3. 请玩家补一份"分离瞬间"的日志（关键：分离是**耦合后立刻**发生，还是**车库拖动后**、还是**翻转后**发生）——现有日志只能证明"分离已经存在"，不能唯一定位是哪个提交点造的。

---

## 2. 贰：车号 / 名称（/分组）"依旧由链头决定"——成立，且是甲口径的必然结果

### 2.1 代码级根因（已逐行核实）

`src/couple_group.cpp`：

- `R3RSegmentTraitRow(v)`（:923-928）：
  ```cpp
  const Vehicle *const head = v->First();
  if (head->r3r_segment_id == v->r3r_segment_id) return nullptr;   // 链头所在段 = 控制段 ⇒ 没有行
  return R3RSegmentGet(v->r3r_segment_id);
  ```
- `R3RSegmentUnitNumber(v)`（:930-937）：行有值用行，否则 **`return v->First()->unitnumber;`**（回退链头）。
- `R3RSegmentName(v)`（:939-953）：行有值用行，否则返回**车自己的名字**（注释明确：隐藏段没名字就该没名字，回退链头会让子行变成上一行的副本）。
- `R3RSegmentGroupID(v)`（:955-962）：行有值用行，否则 **`return v->First()->group_id;`**（回退链头）。
- `R3RSegmentStoreTraits(v)`（:964-...）注释原文：*"Traits are written for the control segment only: it is the one the player edits (everything the GUI offers lands on the chain head), and the one whose row is purely a mirror of the car."*

⇒ **"控制段"在代码里就是链头所在段**：`R3RGetControlSegment()`（`src/train_cmd.cpp:4385`）从 `chain->Previous()` 向上找 ★，链头没有 Previous，**恒返回链头**；`R3RSyncSegmentTraits()`（:4696）把链头车的活字段写进控制段行。

⇒ 因此"由控制段显现" ≡ "由链头显现"，**玩家观察到的现象与第 152 轮 P1=甲 的口径完全一致**——这不是漏改，而是甲口径的定义。

### 2.2 另一个锚：命令所有者（最低优先级段）

`R3RGetLowestPriority(segs)`（`src/train_cmd.cpp:4353`）按 `r3r_priority` 最小挑"命令所有者"。现场 8/8 提交点全是两个锚分裂：

```
INVAR-CTRL tag=couple head=48 nseg=2 ctrl=48 ctrl_pri=2 owner=21 owner_pri=1 borrowed=1
```

即：**特质锚 = 链头 48，排程/命令锚 = 段头 21**。玩家在界面上看到的车号/名称来自 48（链头），而列车实际跑的表属于 21（owner）——两套东西分家在一条链上各行其是。这就是"编号/名称看起来由链头决定"的最直接证据。

### 2.3 仍然直读链头字段的读出点（第 155/156 轮只覆盖了主干）

已走段行的（OK）：`vehicle_gui.cpp:398/400/2443/2446-2447/2518`、`vehiclelist.cpp:187/190`、`group_gui.cpp:784`、`strings.cpp:2367-2369`。

仍直读链头/自身字段的（问题点，需收尾）：

| 位置 | 读的是 | 影响 |
|---|---|---|
| `vehicle_gui.cpp:334-341` `GetUnitNumberDigits()` | `v->unitnumber` | 列表车号列宽按链头算 |
| `vehicle_gui.cpp:375-380` | `v->unitnumber` | 同上（:400 已用段行，两处不一致） |
| `vehicle_gui.cpp:1793` `VehicleNumberSorter` | `a->unitnumber` | 按车号排序用的是链头号 |
| `depot_gui.cpp:810` | `v->unitnumber` | 车库格里的车号是链头号 |
| `depot_gui.cpp:1217` | `GetUnitNumberDigits(this->vehicle_list)` | 车库列宽同上 |
| `couple_group_gui.cpp:584` | `t->unitnumber` | **挂接分组编辑器**的段列表用链头号（该处本就逐段列，却没用段行） |
| `departures_gui.cpp:215-216` | `v->unitnumber` | 离站板宽度种子 |
| `departures_gui.cpp:224-225` | `v->group_id` | 离站板收集分组 |
| `departures_gui.cpp:1381/1385` | `d->vehicle->group_id` | 离站板显示分组 |
| `group_cmd.cpp`（GroupStatistics 一类） | 链头 `group_id` | 分组统计把整链算在链头分组下 |

另外一处**语义不对称**（会造成"隐藏段子行显示链头号/组"）：`R3RSegmentUnitNumber` / `R3RSegmentGroupID` 在段行为空时回退**链头**，而 `R3RSegmentName` 回退**自身**。

### 2.4 待拍板（本轮最重要的口径问题）

- **甲（第 152 轮已拍、未接线）**：强制"控制段 == 链头段"（四个提交点末尾重排 `r3r_priority`，让链头段成为最低优先级）。效果：两个锚合一，特质与排程都归链头段 —— **玩家观察到的现象不会变**（仍是"看起来由链头决定"，因为定义上就是链头）。代价：会改变"耦合后跑谁的表"（T8701 的 INHERIT、KI-215b 的进度回写、`R3RMergePriorities` 的被动优先语义）。
- **乙**：控制段 = 优先级最小段（命令所有者）。效果：车号/名称/分组跟着**命令所有者段**走 —— 这才是"列车特质属于列车、由控制段显现"的字面实现；链头不再是唯一门面（一条链的名字可能挂在链中段的段头上，UI 需要段子行承载）。代价：UI 观感变化大，且要重写 `R3RSegmentTraitRow` 的"控制段无行"约定（改成"每段都有行，链头段的行不再是隐式镜像"）。

**需要玩家明确回答**：你要的"由控制段显现"，是甲（控制段=链头段，本质仍是链头）还是乙（控制段=命令所有者段，显示跟着排程走）？

---

## 3. 叁：车库内"仅段参与"的拖动 —— 判定确实没收紧（成立）

### 3.1 现状代码路径

1. `depot_gui.cpp:378` `TrainDepotMoveVehicle()`：
   - `:410` 判段只看 `TrainDepotGetSegmentFront(v)`（`:160-179`）——**只回答"这辆车落在某个段内"**，不回答"被拖的块是否刚好等于一个完整段"；
   - 是段 ⇒ `:412` `TrainDepotMoveSegment()`（`:247-265`）：`TrainDepotDetachSegment()`（`:222`，按 `TrainDepotGetSegmentTail` `:189` 摘掉整段）→ 以 `MoveChain` 搬段本身 → 落空行且非机车时补发 `MakeSegment`；
   - 不是段 ⇒ 普通 `MoveRailVehicle`。
2. `depot_gui.cpp:390` / `:1619` `TrainDepotDropSplitsSegment()`（`:205-212`）：只拒绝"落点在段内部"，**不校验被拖块与段边界是否对齐**。
3. 命令层 `CmdMoveRailVehicle`（`train_cmd.cpp`）只做位置合法性（拒绝插入段内部），**没有耦合/解耦分支**；`R3RSyncChainAfterDepotEdit()`（`:5266`）负责并链后的收尾：`R3RSyncDrivingOrders()` 排程借用、车号取回/补号（约 `:5286-5361`）、名字返还、`R3RNormaliseChainGroups()`（`:5374` 调用，定义 `:5423`）、提交点 settle（`:5380`）。
4. 段的"耦合语义"是**模仿**出来的：`★/⊗` 标记搬运（`train_cmd.cpp:3096-3114`，注释自陈"复刻 Couple() 的标记"）、三特质停放（`:2967-3085`）；优先级也仿耦合的"目标被动"（`TrainDepotRankDropTargetFirst` `depot_gui.cpp:287` 给被拖侧 `priority += target_max+1`）。
5. 全仓搜索：拖动路径里**没有任何** `Commands::Couple` / `DecoupleTrain` 调用（只有注释提到）。

### 3.2 缺口清单（建议收紧的判据）

| # | 情形 | 期望 | 现状 |
|---|---|---|---|
| 1 | 拖出整段（★..⊗ 或 ★..链尾）到**空行** | 视作**解耦**（段从原链解出、特质取回、段 ID 保留/回收、排程归属按解耦规则处理） | 只做 `MoveChain` + 可能补发 `MakeSegment`，收尾靠"并/拆链"通用逻辑 |
| 2 | 把段拖进**另一条链**（落点在链内/头/尾） | 视作**耦合**（走 Couple() 的排程继承、等待点跳过、优先级、分组归一化） | 只靠优先级 + 标记模仿，未走 Couple |
| 3 | 被拖块**劈开一个段**（块首非 ★ 或块尾非段尾） | 拒绝（或自动扩为整段） | depot 侧会扩为整段，但命令层**无"块边界必须与段边界对齐"的校验** |
| 4 | 被拖块**跨越两个段**（首在段 A、尾在段 B） | 拒绝 | 不拒绝 |

### 3.3 待拍板

"仅段参与"的可拖动单位口径：是否限定为 **①整段 ②整条链 ③散车厢链（不成段）** 三种，其余（劈段、跨段、段内部分）一律拒绝？如确认，本轮即可在 `CmdMoveRailVehicle` 加统一校验（拒绝式），并在 depot 侧把"整段/整链"两条路径显式分流成"解耦语义 / 耦合语义"。

---

## 4. 肆：挂接分组是"列车特质"还是"物理属性"

### 4.1 现状（代码事实）

| 维度 | 现状 | 位置 |
|---|---|---|
| 存储 | **按车**存（`Vehicle::couple_groups` 64 位掩码，CGVR 稀疏只存非 0 的车） | `vehicle_base.h:387`、`sl/couple_group_sl.cpp` |
| 语义 | **按段**承载（只有段头是 carrier） | `couple_group.cpp:55` `R3RIsCoupleGroupCarrier` |
| 读取 | 段内**并集**（`R3RCollectCoupleGroupsOfSegment`）再展开**父组继承**（`R3RAddCoupleGroupAncestors`，parent 链 OR） | `couple_group.cpp:233-251` |
| 耦合时 | **覆盖，不是并集**：取"控制段（=最低优先级段）"的掩码，清空链内**每个段头**的掩码后逐位重填 | `R3RNormaliseChainGroups()` `train_cmd.cpp:5423`（取 owner `:5460`、清 `:5464`、填 `:5467`），调用点 `:9475`（Couple 成功）、`:5374`（车库编辑）、`:6909/:6911`；读档同样做一次 `couple_group.cpp:777-801` |
| 谁是 owner | `R3RMergePriorities(passive, active)`（`train_cmd.cpp:4938`，主动侧 `+= passive.size()`）⇒ **等待/被动方优先级更低** ⇒ owner = **等待方**控制段 | `train_cmd.cpp:9120-9122` |
| 旧"交集才可挂" | 已废除（`R3RCoupleGroupMasksCompatible` 仅剩定义，无调用点）⇒ 分组现在只用于**跨公司授权** | `couple_group.cpp:121-146` |

### 4.2 对"AB 挂 AC"的直接回答

- AB 与 AC 是两条链各自的挂接分组。耦合后现在的行为是：**由"等待方"决定**——等待方的控制段掩码被广播到合并链的**每一个段头**，挂车方（主动侧）的分组**被丢弃**。
- 结果既不是"AB 和 AC 并存"（段级保持），也不是并集 ABC，而是"**取一方 + 全链统一**"——即**存储与语义都被压成了链级单一份**（这正是"链级组"概念的残留）。
- 附带后果：因为归一化会**清空再重填每个段头**，耦合瞬间**段级分组差异被抹平**（`R3RClearCoupleGroupsOfSegment` `couple_group.cpp:272`）。所以现在实际上是"物理上按车、语义上按段、行为上按链"的三层不一致。

### 4.3 三个候选口径（请玩家拍板）

| 口径 | 语义 | 优点 | 代价 |
|---|---|---|---|
| **(1) 并集（ABC）** | 耦合时两侧掩码 OR，广播给所有段头 | 不丢信息；实现最简 | 跨公司授权会因并集而变宽（不相关的组也变"允许"）；段级差异消失 |
| **(2) 段级保持（AB 的段留 AB、AC 的段留 AC）** | 取消归一化，各段保自己的 | 语义最干净（分组 == 段特质，与车号/名称/排程同层）；跨公司授权可按段判定 | UI 上一条链各段分组不同，必须靠段子行显示；`R3RCoupleAllowed` 的跨公司判定要明确"看哪一段" |
| **(3) 现状（覆盖，等方优先）** | 保持链级单一份 | 实现零改动 | 丢信息；与"分组属于段"的设计直接冲突 |

**建议**：若目标是"分组属于段"（与 P3=B 的段身份体系一致）⇒ 选 (2)；若目标是"分组是整列车的属性（授权/白名单按整链走）"⇒ 选 (1) 并显式写成"并集"，同时把 `R3RNormaliseChainGroups` 改成 OR 而不是"清空重填"。

---

## 5. 待拍板清单（一次性回答即可）

1. **贰**：控制段口径 = 甲（控制段恒等于链头段，本质仍由链头决定）还是 乙（控制段 = 命令所有者段，特质跟排程走）？
2. **叁**：车库可拖动单位是否限定为 {整段, 整条链, 散车厢链}，其余（劈段/跨段/段内部分）一律拒绝？拖动"整段"是否按"解耦语义"、拖入别链按"耦合语义"走同一套代码？
3. **肆**：挂接分组归谁 = (1) 并集 ABC / (2) 段级保持 AB+AC / (3) 维持现状覆盖？
4. **壹**（不需口径，只需场景）：图像分离是**耦合后**、**车库拖动后**、还是**翻转后**发生的？能否补一份"分离瞬间"的日志？

---

## 6. 涉及的关键位置索引（本轮核实过行号）

- 特质读：`couple_group.cpp:923`（TraitRow，控制段返回 nullptr）、`:930`（UnitNumber，空行回退链头）、`:939`（Name，空行回退自身）、`:955`（GroupID，空行回退链头）、`:964`（StoreTraits 只写控制段）
- 控制段/优先级：`train_cmd.cpp:4385`（R3RGetControlSegment，恒返回链头）、`:4353`（R3RGetLowestPriority）、`:4696`（R3RSyncSegmentTraits）、`:4938`（R3RMergePriorities）
- 分组归一化：`train_cmd.cpp:5423`（定义）、`:5374`（车库编辑调用）、`:9475`（Couple 调用）、`couple_group.cpp:272/777-801`
- 车库拖动：`depot_gui.cpp:160`（GetSegmentFront）、`:189`（GetSegmentTail）、`:205`（DropSplitsSegment）、`:222`（DetachSegment）、`:247`（MoveSegment）、`:287`（RankDropTargetFirst）、`:378`（MoveVehicle）、`:1619/:1650`（拖动落点）
- 车库收尾：`train_cmd.cpp:5266`（R3RSyncChainAfterDepotEdit）、`:3122/:3123/:3238`（调用点）
- 探针：`train_cmd.cpp:9366-9387`（CHAIN-ATTRS，`SEG` = IsSegmentFront 计数）
