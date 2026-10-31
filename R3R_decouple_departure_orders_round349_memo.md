# R3R 解挂后脱离 + 排程归属 + 排程自动插入（第 349 轮，2026-10-28）

## 一、玩家原话（本轮任务定义）

> 「不错，这个挂接寻路的问题暂时得到了解决，接下来，我希望向解挂后的脱离和排程，以及排程的自动插入进攻」

拆成三条战线：

| 代号 | 战线 | 一句话 |
|---|---|---|
| **A** | 解挂后的**脱离** | 解出方与留下方分开后，各自要能自己开走（不互相挡、不赖在原格、不原地僵死） |
| **B** | 解挂后的**排程归属** | 解出方拿哪张表、留下的拿哪张表、索引停在哪一条 |
| **C** | 排程的**自动插入** | 列车跑一趟后，把实际走法记成订单（auto_ranges / AUTO-ORD 那一套） |

## 二、前置结论（挂接寻路，已收口，不再动）

第 347~348 轮：`yapf_rail.cpp` 的安全位判据加了「第三人就是询路车自己」豁免 ⇒ `LOCO-MOVE veh=57 tile=15,14 track=08`（LOWER，正确地走了岔道），全日志 `FSCP fail=other ... other=57` 与 `CPL-SAFE-FAIL` 归 0；同一轮修掉「挂接机车 lookahead 丢失 ⇒ 速度被 `advisory_max_speed = 30` 兜底」的 30 km/h 平台（KI-519，`RESV-LA-REBUILD` 探针）。

## 三、现状盘点（代码事实，带锚点）

### B 战线：排程归属

1. **T8701 需要的双向归属开关至今没有接线** —— 搜索 `ODOF_` 全 `src/*.cpp` 只有 **1 条命中且是注释**：
   ```
   src\train_cmd.cpp:9800:  * 字段（唯一写入口是已删的 MOF_DECOUPLE_ORDERS 命令），字段恒为 ODOF_KEEP_ORDERS ⇒ 解析器
   ```
   ⇒ `GetDecoupleFirstOrdersType()` / `GetDecoupleSecondOrdersType()` 仍零调用点，DECOUPLE 订单**无法逐次声明**「留下的继承 / 解出的等待」。第 143 轮拍过的「接线 ODOF_*，默认前半 INHERIT + 后半 WAIT_FOR_COUPLE」仍是未做项。

2. **当前生效口径（route A + KI-180 + KI-215 + KI-215b）**：
   - `src\train_cmd.cpp:9952-9962`：解挂两半**绝不共用一张表**（不再把正在跑的表别名给解出方）；
   - `:9963` `u_inherited_running = (u->orders == nullptr)`；
   - `:9991` `OrderList *const driving_orders = v->GetEffectiveOrders();`（修法 D：跟随表）；
   - `:9996-9997` 先 `R3RRenumberPriorities(v/u)` 重排优先级，再让链头重新指向各自的命令所有者（v 成为 owner 就归还 = T8701 #2 restore，否则继续跑 owner 的表 = T8701 #1 inherit）；
   - `:7430 R3RPushProgressToOwner()`（KI-215b）：借用结束时把链头跑到的索引写回 owner，防止「冻结索引被喂回、订单往回跳」。
   - 解出方等待点：从 `decouple_idx` 之后卷绕找 `WAIT_COUPLE`；找不到就**就地插入**（KI-215 已把闸门从 `u->orders == v->orders` 改成比 `driving_orders`，修复「解出方是链中段时永远找不到等待点」）。

3. **物理脱离**：`KI-335` 的 `DECOUPLE-INTERLOCK`（第 228 轮，已实机复测通过：日志 `set head=23 follower=22 seam=1086,139` → `wait dist=0` → `clear dist=4 gone=0`）只保护**站台格上、非纯车底**的跟随方等到前车离开接缝 4 格再起步；车库/野外解挂不登记（`:2302 !IsRailStationTile` 与 `:2304 R3RIsCarOnlyFormation` 两条都不打日志的早退，第 228 轮记为未覆盖）。

### C 战线：排程自动插入

4. **记录侧已经在跑**（`R3RAutoRecordChainOrders`，定义 `src\train_cmd.cpp:6922`，调用点 `:21873`），落点是段行的 `auto_ranges_begin/count/signature[AUTO_RANGE_SLOTS]`（存档在 `src\sl\couple_group_sl.cpp:299-301`）。
5. **第 264 轮拍板的 C-2 白名单已落地**：`:6910 AUTO-ORD-DROP kind=%s dest=%u seg=%u head=%d reason=not-in-source-range leg=[%u,%u) n=%u`（目的地不在控制段对应区间就丢弃）。
6. **执行侧跳过已存在**：`static uint16_t R3RSkipAutoRecordedOrders(Train *v)`（`:7201`，调用点 `:7367` 与 `:21877`），带 `AUTO-ORD-SKIP ... order=%u`、`AUTO-ORD-STALE`、`AUTO-ORD-TRUNC from= to= tag=`；`order_gui.cpp:1021-1023` 用同一区间判定灰字显示。
   ⇒ 第 264 轮记忆里写的「F（执行侧跳过凭据）未做」**已不成立**，需按代码实际重判。
7. **尚未确认（本轮要做）**：`auto_ranges` 在「解挂单段化」后是否还在（第 264 轮日志 `SEGTR-SNAP tag=decouple-v ... auto n=0 first=[0,0)` 是解挂清空的铁证）；`:7201` 的跳过是否受 `v->orders == own` 限制（第 264 轮 §八 记的「F 要放宽 v->orders==own」）。

### 同族已知缺陷（会与 A/B/C 相互咬）

| KI | 内容 | 对 A/B/C 的影响 |
|---|---|---|
| KI-180 | 解挂两半共用一张表 ⇒ 悬垂 / 串窗 | B 的地基，已修 |
| KI-215 / 215b | 解出方索引往回跳（`real=18` → `real=15`） | B，已修 |
| KI-330 | `CheckTrainsOnTrack` 把同格静止纯车底判成阻塞 ⇒ 机车 `MarkTrainAsStuck` | A 的核心嫌疑（解挂 8/8 紧跟 `TPR-BLOCKED`，`otherTile == mf`） |
| KI-399 / KI-400 / KI-401 | 自动插入区间在解挂单段化后丢失 / 记录内容缺陷 | C |
| KI-518 | `CPL-SAFE-FAIL` 前移到站台内候选格 | 属挂接，已豁免 |

## 四、需要玩家拍板的问题（答完请按编号回填，勿重问）

- **Q1（范围）**：A 战线「脱离」指哪一种？
  - Q1-a 解出方在前车还压着接缝时**不能开走**（防追尾/防抢岔）——已有 `DECOUPLE-INTERLOCK`，需扩到非站台/纯车底；
  - Q1-b 解出方**不该开走**却开走了（抢信号、乱跑）；
  - Q1-c 解出方**根本开不动**（`TPR-BLOCKED` 同格纯车底，KI-330）；
  - Q1-d 两者像素/位置贴在一起分不开（视觉脱离）。
- **Q2（优先级）**：A/B/C 三条先做哪条？我建议顺序 **A（脱离）→ B（归属）→ C（自动插入）**，因为 B 的验证依赖"A 之后两半各自能独立跑"，C 又依赖 B 的表归属正确。
- **Q3（B 的口径）**：DECOUPLE 订单是否按第 143 轮原案接线 `ODOF_KEEP_ORDERS / INHERIT_ORDERS / WAIT_FOR_COUPLE`（默认「留下的继承、解出的等待」），还是维持现在这套自动判定？
- **Q4（场景）**：本轮要按 T8701（两次解挂 + 中途交接，南宁—凭祥—同登—嘉林）验收吗？是的话请给一份从开局到第二次解挂的日志，我按 `DECOUPLE-FIRE / DECOUPLE-DONE / ORD-AFTER-COUPLE / LOCO-AFTER-DECOUPLE` 四行做时间线。
- **Q5（C 的口径）**：自动插入的最终形态是「记录 + 灰字 + **执行时跳过**」（第 264 轮拍板），还是「记录后由玩家一键转成真订单」？

## 五、下一步（不依赖 Q 的部分，可先做）

1. 取数：`src\train_cmd.cpp:9990-10120` 的解挂归属全流程 + `:7201-7260` 的跳过函数，逐行核对「表归属」与「auto_ranges 生命周期」，产出 B/C 两战的准确现状图。
2. 只读探针（不改行为）：在解挂提交点后打一行 `DEC-SPLIT head= leaving= own_head= own_leaving= idx_head= idx_leaving= auto_head= auto_leaving=`，把"谁拿哪张表、停在哪一条、自动区间还剩几段"一次说清，避免后续每轮都要靠猜。

## 六、第 349 轮取证结论（代码事实，逐行核对完毕）

### 6.1 B 战线：归属是「自动判定」，且只有一条推进规则

`static void R3RAdvanceAfterDecouple(Train *half, OrderList *driving_orders, VehicleOrderID decouple_idx)`（`src\train_cmd.cpp:9753-9796`）的全部逻辑就两支：

| 条件（`:9758`） | 索引落点 | 语义 |
|---|---|---|
| `half->GetEffectiveOrders() == driving_orders` | `(decouple_idx + 1) % GetNumOrders()`（`:9761-9763`） | 这半**接着跑**刚在跑的那张表 ⇒ 跳过刚执行的那条 DECOUPLE |
| 否则（这半拿回了自己停放的表） | 只有 `GetOrder(cur_real_order_index)` 是 `OT_GOTO_COUPLE` 时 `+1`（`:9767-9789`，KI-215d） | 这半**回去跑自己的表**，顺便跨过已完成的挂车命令 |

两支之后统一：`:9792 R3RSkipUnfireableDecoupleOrder(half)`、`:9793 cur_timetable_order_index = cur_real_order_index`、`:9795 InvalidateVehicleOrder`。

**结论 B-1**：`R3RAdvanceAfterDecouple` 是纯粹的**结果适配器**，它自己不决定归属——归属在调用点由 `R3RRenumberPriorities(v/u)` → `R3RReelectControlSegment` → `R3RSyncDrivingOrders(half,false)`（`:9996-9997`）决定：这半的控制段仍是别人 ⇒ 继续借别人的表（inherit）；控制段变成自己 ⇒ 归还借用（restore）。玩家无法在 DECOUPLE 订单上声明"这一次留谁"，因为策略字段是死的。

**结论 B-2（第 143 轮原案确认仍未做）**：`src\train_cmd.cpp:9798-9802` 的注释自陈 `R3RApplyDecoupleOrdersStrategy()` **已随 UI 接线一并删除**，读的正是 `decouple_first_orders / decouple_second_orders`，且因为唯一写入口（`MOF_DECOUPLE_ORDERS` 命令）已删，字段**恒为 `ODOF_KEEP_ORDERS`**，"删除后解挂行为与删除前逐字段等价"。⇒ 接线 ODOF 不是"修 bug"，而是"新增能力"（要给回写入口 + UI），工作量比修 bug 大，需明确拍板。

### 6.2 C 战线：跳过只对「自己跑自己的表」生效，借用期恒不跳过（真缺口，可定位可修）

`static uint16_t R3RSkipAutoRecordedOrders(Train *v)`（`src\train_cmd.cpp:7201-7260`）第 **7205-7208** 行：

```cpp
OrderList *const own = R3RSegmentOwnOrderList(v);
/* 修法 D: "是不是我在跑的表"必须看有效表；orders 恒指向自己的表，借用期
 * 直接比较它会让自动记录区间在借用期间被当成自己的排程继续执行。 */
if (own == nullptr || v->GetEffectiveOrders() != own) return 0;
```

**结论 C-1**：借用期（跟随者/非控制段）`GetEffectiveOrders()` 是 owner 的表、`own` 是自己（常为 `orders_backup`）的表 ⇒ 两者不等 ⇒ **函数在第二行就 return 0**，自动区间一次都不会被跳过。第 264 轮记的「F 要放宽 `v->orders == own`」在现代码里就是这个 `:7208`。

**结论 C-2（新发现，不能简单放宽）**：同一函数后续全部按 `own` 计量——`:7219 own->GetNumOrders()`、`:7227 R3RAutoOrderHash(own, begin, count)`、`:7249` 改索引。而 `begin/count/signature` 是**记录时按记录者自己的表**算出来的（`R3RAutoRecordChainOrders` 用 `ol = R3RSegmentOwnOrderList(seg)`）。若跟随者跑的是 owner 的表，`own` 的编号空间与记录的编号空间**不是同一个**，直接放宽会跳错命令（比不跳更危险）。
⇒ 正解不是放宽守卫，而是"**用有效表 + 找到该有效表的段行**"：区间必须来自"持有这张表的那个段"的 row，区间内的偏移也必须按这张表计数。需要一个"由 OrderList* 反查段 row"的入口（现有 `R3RSegmentOwnOrderList` 是车→表，方向相反）。

**结论 C-3**：`auto_ranges` 的生命周期仍未确认。写 `auto_ranges_*` 的地方只有：记录 `:7065-7067`、stale 清零 `:7234-7236`、`R3RAutoFinaliseChainOrders` 的按段裁剪。若解挂后段行被回收/重分配（新 `r3r_segment_id`），新行是默认值 ⇒ 区间归零（与第 264 轮 `SEGTR-SNAP ... auto n=0 first=[0,0)` 吻合）。本轮未取到"段 ID 是否在解挂后发生变化"的实证 ⇒ 登记为**疑点 REG-349-A**，需 `DEC-SPLIT` 探针在各半 `R3RSettleChainSegments` 之后打 `segid/auto n/first` 才能定性。

### 6.3 A 战线：两道豁免已落地，「脱离」剩下的口子是「非站台解挂」

- `KI-330` 的同格豁免**已在代码里**：`src\train_cmd.cpp:18990` `else if (other_train->tile == consist->tile && consist->cur_speed == 0)` 走豁免（探针 `TPR-SEAM-EXEMPT`，`:18997`），注释 `:18949-18965` 说明"被自己刚解挂的车底压在同格"不再判永久阻塞；
- 配对豁免也在：`:18982-18989` `tpr_pair_hit`（`RESV-LA-REBUILD` 那一轮的 57 号机车场景），注释 `:18966-18981`；
- `KI-335 DECOUPLE-INTERLOCK` 的登记口有两条**不打日志的早退**：`src\train_cmd.cpp:2909 !IsRailStationTile(follower->tile)`、`:2911 R3RIsCarOnlyFormation(follower)`。⇒ **车库解挂、野外解挂、纯车底被解出，全部不登记**，日志上完全无痕（第 228 轮已记为"未覆盖"，本轮复核仍成立）。

**结论 A-1**：「解挂后开不动」在站台+机车侧已有两道豁免兜住；剩余风险集中在"**车库/野外解挂**"与"**解出方是纯车底**"两类，且这两类**没有任何日志可判**，必须先加探针才能分清是"不该走"还是"走不动"。

### 6.4 附：C 战线的上游阻塞仍待拍板

`R3R_KNOWN_ISSUES.md:13251 KI-399`（非控制段在借用期没有自己的 `OrderList` ⇒ 自动记录被静默放弃）、`:13275 KI-400`（记录只落在 `orders_backup`，订单窗口看不见）状态仍为「待拍板」。二者在结构上先于 C-1/C-2：若借用期根本没有自己的表，那么"记录"这一步就没有落点，放宽跳过也无数据可跳。

### 6.5 本轮判定汇总

| 战线 | 现状 | 可动工与否 |
|---|---|---|
| A | 站台侧已兜住（KI-330 + KI-335 复测通过）；车库/野外/纯车底侧无登记、无日志 | 需 Q1 指明现象；可先加 `DEC-SPLIT`/seam 登记探针 |
| B | 归属=优先级+控制段自动决定；`ODOF_*` 死字段，逐次声明能力不存在 | 需 Q3 拍板（要不要给回写入口 + UI） |
| C | 记录与灰字已在；执行跳过只覆盖"跑自己的表"，借用期恒 0（`:7208`），且不能简单放宽（C-2） | 可动工，但顺序上被 KI-399/400 卡住 |

## 七、本轮续报（玩家 2026-10-28 第二次发言）

玩家原话：

> 「见R3R_debug.log，那就是我报告的一些问题」

即：**本轮的"问题清单"不再用文字描述，而是以 `build\R3R_debug.log` 的现场读数为准**。
⇒ 处理方式：先扫日志定位异常行（探针原文 + 行号），按"疑点/猜测根因"即刻登记（含期望值/实测值），
再与 §六 的三战线现状图对齐，判定属于 A / B / C 哪一条。

取证落点（本节点待回填）：

- 采样有效性：日志行数 / 字节数 / mtime / 产生者 `build\openttd.exe` 的 mtime 与字节数；
- A 战线判据：`DECOUPLE-FIRE` / `DECOUPLE-DONE` / `DECOUPLE-INTERLOCK set|wait|clear` / `TPR-BLOCKED` / `TPR-SEAM-EXEMPT` / `MarkTrainAsStuck`；
- B 战线判据：`DECOUPLE-ADV` / `ORD-AFTER-COUPLE` / `ORD-PUSH` / `LOCO-AFTER-DECOUPLE` / `R3RSyncDrivingOrders` 相关；
- C 战线判据：`AUTO-ORD` / `AUTO-ORD-DROP` / `AUTO-ORD-SKIP` / `AUTO-ORD-STALE` / `AUTO-ORD-TRUNC` / `SEGTR-SNAP ... auto n=`。

## 八、现场取证（2026-10-28 16:47 日志，逐行锚点）

**采样有效性**：`build\R3R_debug.log` = 4 445 968 B @2026-10-28 16:47；产生者 `build\openttd.exe` = 52 344 832 B @16:42（时间早于日志 ⇒ 有效采样；该 exe 即第 348 轮 KI-519 版，含 `RESV-LA-REBUILD`）。
场景确认为 T8701 同族：车辆名 `T8701/2-南宁-凭祥`、`T8701/2-南宁-河内（嘉林）1`（日志 :3/:10）。

**本次会话共两次解挂**（`DECOUPLE-FIRE` 全日志仅 2 条）：

| # | 行号 | 现场 | 解出 | 留下 |
|---|---|---|---|---|
| 1 | :1144 | `DECOUPLE-FIRE consist=54 tx=12 ty=15 real=2 mode=2 num=1 segs=2 eff=0 plat=30/0` | `u=0`（27 节车底，33 条表） | `v=54`（机车，拿回自己 4 条表） |
| 2 | :2662 | `DECOUPLE-FIRE consist=57 tx=35 ty=12 real=5 mode=2 num=1 segs=2 eff=0 plat=30/0` | `u=0`（同一条车底） | `v=57`（机车） |

### 8.1 A 战线：解挂后「同格互判阻塞」每次都发生，豁免与否取决于残余速度（REAL BUG）

解挂 1 的完整序列（`:1280-1294`）：

```
1280: ORD-IDX-WATCH site=depotstay-in veh=54 real=1->1 ... n=33->4 ord=...F0->...56F0
1281: ORD-IDX-WATCH site=loco-entry  ... spd=2
1287: TPR-BLOCKED veh=54 tile=12,15 spd=2 mf=12,15 origin=12,15 okay=1 td=8
        other=0 otherTile=12,15 otherMf=12,15 otherSpd=0 otherOrder=0 otherFirst=0 caronly=1
1288: TPR-EXIT veh=54 state=veto pair=0 ...
1289: TRP veh=54 ... spd=0 fp=0 look=1 one=1 mstuck=1 fto=1 => ok=0 res=0
1290: DEPOT-ARR veh=54 spd=0 real=1(6) curType=6 stuck=1 ...
1291: CRT veh=54 order=6 dir=5 origin=14,15 td=8 found=1
```

解挂 2 的同位置序列（`:2816-2817`）：

```
2816: TPR-BLOCKED veh=57 tile=35,12 spd=0 mf=35,12 origin=27,12 okay=1 ... other=0 otherTile=35,12 ... caronly=1
2817: TPR-SEAM-EXEMPT veh=57 tile=35,12 spd=0 other=0 origin=27,12 okay=1 td=0 otherOrder=0 caronly=1
```

**结论 A-2（真缺陷，可定位）**：`CheckTrainsOnTrack` 的同格豁免（`src\train_cmd.cpp:18990`）硬条件之一是 `consist->cur_speed == 0`。
- 解挂 2 时 `spd=0` ⇒ 命中豁免，打 `TPR-SEAM-EXEMPT`，正常走 `ChooseTrainTrack`；
- 解挂 1 时 `spd=2`（刚解挂、残余速度未清）⇒ **不命中豁免** ⇒ 走 `tpr_note(1,…)` + `MarkTrainAsStuck`（`:19006-19008`）⇒ `TRP … mstuck=1 => ok=0 res=0`、`DEPOT-ARR … stuck=1`，要等下一 tick 的 `CRT … found=1` 才自愈。

⇒ 同一物理状态（被自己刚解出的车底压在同格）因 2 km/h 残余速度被判成"永久阻塞"，并给机车打上 Stuck 标记。这是"解挂后的脱离"里唯一一条有日志实证的机制缺陷。

### 8.2 B 战线：解挂归属结果正确，但 `wait_idx` 两次都是 65535（疑点）

```
1203: DECOUPLE-JUMP fire_real=2 dec_idx=2 wait_idx=65535 step=-1 v=54 u=0 n_u=33
1204-1207: JUMP-ORD 2 type=15(DECOUPLE) / 3 type=17(WAIT_COUPLE) / 4 type=1 / 5 type=15
1208: DECOUPLE-ADV dec_idx=2 v_real=1 u_real=3
1209: DECOUPLE-DONE u=0 co=1 real=3
1275: LOCO-AFTER-DECOUPLE veh=54 curType=0 real=1 idxnext=6
1276-1279: L-ORD 0 type=16 dest=0 / 1 type=6 dest=19 / 2 type=2 / 3 type=1
```

```
2721: DECOUPLE-JUMP fire_real=5 dec_idx=5 wait_idx=65535 step=-1 v=57 u=0 n_u=33
2726: DECOUPLE-ADV dec_idx=5 v_real=0 u_real=6
```

**结论 B-3（归属正确）**：留下方 `v` 拿回自己的 4 条表（`ORD-IDX-WATCH … n=33->4`，`:1280`；`L-ORD 0..3` = GOTO_COUPLE→GOTO_DEPOT→…），解出方 `u=0` 保住 33 条表并落到 `real=3`（= `JUMP-ORD 3 type=17` 的 WAIT_COUPLE）。这正是 T8701 #1 期望的「机车 RESTORE、解出车底 WAIT_FOR_COUPLE」。

**疑点 REG-349-B（待验证）**：`wait_idx=65535 step=-1` 两次都出现，而链表里 `idx3`/`idx6` 明确是 `OT_WAIT_COUPLE`（`:1205`、U-ORD 3/6/…）。若 65535 表示"没找到"，则与"u_real 落在 3/6 上"矛盾；若它只是"本次无需插入"的哨兵，则日志文案会把正常路径伪装成异常。⇒ 需核对 `DECOUPLE-JUMP` 打印点的 `wait_idx` 来源与 `step` 语义（`src\train_cmd.cpp` 内 `DECOUPLE-JUMP` 探针附近），判定是探针文案问题还是查找逻辑真的漏找。

### 8.3 C 战线：区间编号空间错位 + 白名单大面积丢弃（两个真缺陷）

**（1）记录表与执行表不是同一张（§六 C-2 的机制已核清；"索引错位"表述需更正）**

> **更正（核对源码后）**：`R3RAutoRecordChainOrders` 在 `:7028` 已有 KI-503 修法
> `const bool follows_other_plan = seg->IsR3RFollower() || seg->GetEffectiveOrders() != ol;`
> ⇒ 跟随者一律 `anchor = num`（追加到**自己**表尾），**不会**把 33 条表的索引写进 4 条表。
> 且 `:6952-6971` 已按 KI-399 懒建 `orders_backup`（探针 `AUTO-ORD-NEWLIST`）⇒ KI-399 **已修**，
> 备忘录 §六 6.4 与 KI-520 里"KI-399 待拍板"的写法应改判为"已修（代码内已落地）"。
> 真正剩下的矛盾是：**记录写进段自己的表（`n=4` / `n=7`），执行却跑所有者那张表（`n=33`）**
> ⇒ 记录、灰字、执行跳过三者在借用期全部落空。这才是 C 战线的核心，且它与 B 战线是同一件事的两面。

```
768: AUTO-ORD seg=3 head=54 slot=0 kind=station dest=0 iv=[3,4) cursor=1 anchor=3 real=1 cur=waypoint own=0 borrowed=1 n=4
806: AUTO-ORD seg=1 head=0  slot=0 kind=station dest=0 iv=[6,7) cursor=1 anchor=6 real=0 cur=waypoint own=0 borrowed=1 n=7
2346: AUTO-ORD seg=4 head=57 slot=0 kind=station dest=2 iv=[6,7) cursor=1 anchor=6 real=4 cur=other own=0 borrowed=1 n=7
2372: AUTO-ORD seg=1 head=0  slot=0 kind=station dest=2 iv=[6,7) cursor=1 anchor=7 real=3 cur=other own=0 borrowed=1 n=7
2280: AUTO-ORD-SKIP seg=3 head=54 slot=0 from=3 to=0 iv=[3,4) order=5
```

- 记录时 `own=0 borrowed=1` ⇒ 写进 `orders_backup`（KI-399/400 现场复现，玩家在订单窗口看不见）；
- `n=4` / `n=7` 是**记录者自己那张表**的条数，而同一时刻真正在跑的是 33 条的驱动表（`ORD-AFTER-COUPLE … n=33`、`ORD-IDX-WATCH … n=33->4`）；
- `:2280` 的 `AUTO-ORD-SKIP … from=3 to=0 iv=[3,4)` 又确实按 4 条表跳过成功 ⇒ 说明"跳过"只在 `GetEffectiveOrders()==own` 的那一瞬生效，一旦回到借用（33 条表）该区间既对不上号也不会被查。

**（2）白名单大面积丢弃**

```
298: AUTO-ORD-DROP kind=depot   dest=0 seg=1 head=0  reason=not-in-source-range leg=[1,2)  n=33
574: AUTO-ORD-DROP kind=depot   dest=0 seg=3 head=54 reason=not-in-source-range leg=[1,2)  n=33
1297: AUTO-ORD-DROP kind=station dest=0 seg=1 head=0  reason=not-in-source-range leg=[4,5)  n=33
2219/2220/2261: AUTO-ORD-DROP kind=station dest=0 seg=4|1 head=57 reason=not-in-source-range leg=[4,5)  n=33
2827: AUTO-ORD-DROP kind=station dest=2 seg=1 head=0  reason=not-in-source-range leg=[7,10) n=33
```

同一 `dest` 在前后几行既出现 `DROP`（不在源区间）又被 `AUTO-ORD` 记录（`:768 dest=0 iv=[3,4)`），说明 `leg`（控制段对应区间）的算法与采样点系统性地错位在 ±1~3 条上 ⇒ C-2 白名单现在把"该记的"也丢掉了。

**（3）auto_ranges 在解挂后并非全清（推翻第 264 轮的"清空"结论）**

```
1200: SEGTR-SNAP   i=1 id=1 front=0 star=1 ctrl=0 | live u=2 n="T8701/2-南宁-河内（嘉林）1" | bk u=1 n="T8701/2-南宁-凭祥" | row use u=1 n="T8701/2-南宁-凭祥" g=65534 | auto n=1 first=[6,1)
1201: SEGTR-SNAP   i=2 id=2 front=6 star=1 ctrl=1 | live u=0 n="" | bk u=2 n="T8701/2-南宁-河内（嘉林）1" | row use u=2 n="…嘉林1" | auto n=0 first=[0,0)
```

`auto n=1 first=[6,1)` = `begin=6, count=1`（区间 `[6,7)`），即解挂后 `seg id=1` 的区间**存活**；只有 `seg id=2` 归零。
⇒ 第 264 轮记的 `SEGTR-SNAP tag=decouple-v … auto n=0` 不是"必然清空"，而是"按段分别存活/归零"；REG-349-A 从"是否清空"改判为"为什么只有部分段存活"。

### 8.4 与本轮三战线的对位

| 玩家可感知现象 | 归属 | 代码位置 | 性质 |
|---|---|---|---|
| 解挂后机车"卡一下"、界面显示卡住 | A | `CheckTrainsOnTrack` 同格豁免 `cur_speed == 0`（`:18990`） | 真缺陷，有日志实证（`:1287-1290` spd=2） |
| 解挂后订单窗口看不到自动插入 | C | `own=0 borrowed=1` 只写 `orders_backup` | KI-399/400，结构性问题 |
| 自动插入"该记的没记上" | C | `not-in-source-range` 白名单（`leg` 错位） | 真缺陷，`:298/:574/:1297/:2219/:2820` 多条 |
| 自动插入的区间跳错/不跳 | C | 区间按 `own`（4 条）编号、执行按驱动表（33 条） | 真缺陷，C-2 实锤 |
| 解挂后谁拿哪张表 | B | — | **行为正确**（`:1275-1279` + `:1209`） |
| `wait_idx=65535` | B | `DECOUPLE-JUMP` 探针/查找 | 疑点，待核代码 |

## 九、本轮落地（A 战线）

### 9.1 改动（唯一一处，`src\train_cmd.cpp:18990`）

```cpp
/* 改前 */
} else if (other_train->tile == consist->tile && consist->cur_speed == 0) {
/* 改后 */
} else if (other_train->tile == consist->tile) {
```

去掉 `consist->cur_speed == 0`，并把理由写进紧随其后的注释（11 行）。

### 9.2 为什么这样改是安全的

1. 真阻塞的几何形态是 `otherTile != consist->tile`（另一列车占住我预留前路的尽头，见同函数 `:18962-18964` 的既有注释）；同格不可能是"前路被占"。
2. 豁免**不授予通行权**：落进豁免分支只是不 `return TPRRF_NONE`，后续 `ChooseTrainTrack` / `TryReserveRailTrack` 照常执行，PBS 互斥仍决定能否真正进入那一格（`:18978-18981` 原文）。
3. 该分支只做日志与"不阻塞"，不写位置、不改速度、不打方向，属于纯判据放宽。

### 9.3 构建自证（第 349 轮）

| 项 | 值 |
|---|---|
| 构建入口 | 复用既有 `build\_r3r_build.cmd`（未新建任何 `.cmd`） |
| 退出码 | `CMAKE_EXIT=0` |
| 链接 | 日志命中 `[3/3] Linking CXX executable openttd.exe` |
| 编译错误 | 日志内 `error C` / `FAILED:` 零命中 |
| 时间戳 | `src\train_cmd.cpp` 17:18 → `build\openttd.exe` 17:20（52 344 832 B） |
| 静态检查 | `read_lints` 0 条 |

本轮未新增日志字面量，故不做 exe 字面量核对（`TPR-SEAM-EXEMPT` 为既有串）。

### 9.4 复测判据（4 条）

1. 解挂瞬间**不再**出现 `TRP veh=NN … mstuck=1 => ok=0 res=0` 紧跟在 `TPR-BLOCKED` 之后；
2. 同现场应改为出现 `TPR-SEAM-EXEMPT veh=54 tile=12,15 spd=2`（spd 非 0 也豁免，这正是本轮要看到的差异）；
3. 解挂后不再出现 `DEPOT-ARR … stuck=1`，机车当 tick 直接 `CRT … found=1`；
4. 负判据：另一列车停在**我预留前路的尽头格**（`otherTile != 我`）时仍必须 `ok=0 res=0`，不得移动。

## 十、仍未动（B / C，需口径）

| 编号 | 内容 | 卡在哪 |
|---|---|---|
| B-1 | 解挂归属只能由"优先级 + 控制段"自动决定，`ODOF_*` 是死字段（`:9798-9802` 自陈） | 要不要给 DECOUPLE 恢复"前半/后半排程策略"的 UI + 回写入口 |
| B-2 | `DECOUPLE-JUMP wait_idx=65535 step=-1` 两次都出现，但结果落在 `OT_WAIT_COUPLE` | 先判是探针文案还是查找漏找（只读核对即可） |
| C-1 | 记录写自己的表（`n=4`/`n=7`），执行跑所有者的表（`n=33`）⇒ 自动插入在借用期整体落空 | 与 B-1 同源：段在借用期没有自己的"位置"，记录无处生效 |
| C-2 | `AUTO-ORD-DROP … reason=not-in-source-range`（`leg=[1,2)` / `[4,5)` / `[7,10)`）大面积丢弃 | 需先定"采样格 → 命令"的匹配口径（`R3RAutoOrderForTile` + `R3RAutoOrderSame`） |
| C-3 | 灰字看不见（`own=0 borrowed=1` ⇒ 只写 `orders_backup`） | KI-400，需拍板"是否让订单窗口显示 orders_backup" |

**建议顺序**：A（已落地）→ C-2（纯匹配口径，可独立修）→ B-1/C-1（同源，需拍板）→ C-3（显示层，最后）。

## 十一、玩家拍板（2026-10-28 第三次发言）：排程归属不许动

**玩家原话要点**：「解挂的『前半/后半排程策略』是一个既有的十分成熟的东西，采用了控制段系统来决定排程，不需要也不能改。」

⇒ 口径落地（此后不得再回头问 Q3）：

- **B-1 关闭（永久）**：不接线 `ODOF_KEEP_ORDERS / ODOF_INHERIT_ORDERS / ODOF_WAIT_FOR_COUPLE`，不恢复 `decouple_first_orders / decouple_second_orders` 的回写入口，不给 DECOUPLE 订单加「前半/后半排程策略」UI。`src\train_cmd.cpp:9798-9802` 那句「`R3RApplyDecoupleOrdersStrategy()` 已删除且行为等价」是本项目的**定案**，不是待补的洞。`ODOF_*` 保持零使用点。
- **控制段系统 = 排程唯一权威**：解挂前后半的排程归属一律由「重排优先级 → 选控制段 → `R3RSyncDrivingOrders`」决定，`R3RGetControlSegment()` 是唯一答案来源。任何新功能都必须**适配**它，不得新增第二套归属规则。
- **C-1 重新定性（关键）**：原先写成「记录写自己的表（`n=4`/`n=7`）、执行跑所有者的表（`n=33`）⇒ 缺陷」，**定性错误**。执行跑控制段表是**正确且不可改**的设计；错的是自动插入的记录侧——它把区间记在了「段自己的表」上，那是一张在借用期根本不参与执行的表。⇒ C-1 从「归属缺陷」改判为「**记录侧没有对准控制段**」，修法方向由「改归属」变为「让记录/灰字/跳过三者都以控制段表为编号空间」。
- **不变量（供后续轮次引用）**：借用期任何「自动插入」记录落点必须满足 `record_order_list == segment->GetEffectiveOrders()`，否则该记录在借用期不可执行、不可显示、不可跳过。
- **C-3（KI-400）口径随之确定**：灰字要显示的是「控制段表里的区间」，不是 `orders_backup` 里的区间 ⇒ 不再需要「让订单窗口显示 orders_backup」这个拍板项。

**仍未答**：Q1（脱离的四种含义）、Q2（三战线顺序）——但两者都不阻塞 C-2。

## 十二、玩家 2026-10-28 第四次发言：三条新报告（先落盘，后取证）

**玩家原话（逐条保留）**：

1. 「第一个是自动插入的命令位置错误了，应该是在最后执行的前往挂接命令后面，而不是整个排程最后面。」
2. 「第二是（可能）解挂后调度命令跳到了刚才自动插入的命令后面，但是自动插入命令的位置本身就是错误的。」
3. 「第三是，无论如何，解挂后，应当限制两列车的脱离方向，而这里没显现出来，因为列车一解挂就撞毁了。」

据此新开三条（避开既有 A/B/C 编号，避免与上一轮结论混淆）：

### D-1 自动插入命令的锚点错误（玩家第 1 条，严重度 高）

- **玩家口径（定性依据，优先级高于任何旧实现注释）**：自动插入的命令必须落在「**最后执行的那条 `GOTO_COUPLE`（前往挂接）命令之后**」，不是整份排程的末尾。
- 现状猜测（待验证）：锚点取自「整表长度 `num`」或「末尾追加」，故解挂记录被放到列表最后。
- 我方旧写法里已有反面证据：第 349 轮日志 `AUTO-ORD … anchor==real`，且 KI-503 的修法给出 `follows_other_plan ⇒ anchor = num` —— **这条"末位锚点"正是玩家现在否定的行为**，需重新审视：`num` 只是"借用别的计划时不要把索引写在别人表上"的权宜，不能当作插入位置的语义。
- 待验证判据：插入后 `cur_real_order_index` 应紧接 `GOTO_COUPLE` 的 `real+1`；表内顺序应为 `[…, GOTO_COUPLE, 自动插入项, …后续原命令]`，而不是 `[…, 原命令…, 自动插入项]`。

**取证结论（2026-10-28，D-1 石锤）**：

- 代码锚点：`src\train_cmd.cpp:7028-7037`
  ```
  const bool follows_other_plan = seg->IsR3RFollower() || seg->GetEffectiveOrders() != ol;
  if (follows_other_plan) { anchor = num; }            // num = ol->GetNumOrders() (:6980)
  else if (seg->cur_real_order_index != INVALID_VEH_ORDER_ID) {
      anchor = seg->cur_real_order_index;
      if (here->IsType(OT_GOTO_COUPLE)) anchor = anchor + 1;
  }
  ```
  ⇒ **借用期（`follows_other_plan`）直接把锚点钉在表尾**，这正是玩家说的"整个排程最后面"。
- 该分支的注释（`:7020-7027`，KI-503 写的）自己承认这个行为：「its recorded history simply grows at the tail of its own list」。**这条注释与新拍板冲突，必须改写**。
- 日志实证（`build\R3R_debug.log`，417 210 B @2026-10-28 20:54:40，本轮新档）：`:766 AUTO-ORD seg=3 head=54 slot=0 kind=station dest=0 iv=[3,4) cursor=1 anchor=3 real=1 cur=waypoint own=0 borrowed=1 n=4`；`:803 AUTO-ORD seg=1 head=0 … iv=[6,7) anchor=6 real=0 n=7`；`:2258 seg=4 head=57 … anchor=6 real=4 n=7`；`:2284 seg=1 head=0 … anchor=7 real=3 n=7`。四例 anchor 全部落在「自己表的最后一条」，与"末尾"一致。
- 修法方向（据此确定）：借用期锚点改为「自己表里**最后一条 `OT_GOTO_COUPLE` 的下一项**」。首选数据源 `seg->orders_backup_real_index`（`:6968` 懒建时显式置 `INVALID_VEH_ORDER_ID`；由 `Couple()` 停放时写入，借用期冻结不动，正是"进入借用时自己走到哪"），在 `ol` 内向前找最后一个 `OT_GOTO_COUPLE`，`anchor = 该索引 + 1`；找不到再退回 `own`（`INVALID` ⇒ 0）。
- 注意：`anchor` 的语义是"区间起点（`auto_ranges_begin`，`:7065`）"，插入点由区间决定，不是直接 `InsertOrder(anchor)`；改 anchor 后必须同步 `cursor` 语义（`:7073-7080` 的"新一轮回卷"逻辑），否则会重复追加。

**实现（2026-10-28，仅 `src\train_cmd.cpp`，未碰 `.h`）**：

```cpp
if (follows_other_plan) {
    const VehicleOrderID own = seg->orders_backup_real_index;
    const VehicleOrderID base = (own != INVALID_VEH_ORDER_ID && own <= num) ? own : num;
    anchor = base;
    for (VehicleOrderID i = 0; i < num && i <= base; i++) {
        const Order *const o = ol->GetOrderAt(i);
        if (o != nullptr && o->IsType(OT_GOTO_COUPLE)) anchor = (VehicleOrderID)(i + 1);
    }
}
```

- 上面 KI-503 那段大注释里"its recorded history simply grows at the tail of its own list"已删除并改写（那条与新拍板冲突），保留"不能用别人表的索引"这半条。
- 取值理由：`orders_backup_real_index` 是"自己被停放时走到哪"（`Couple()` 经 `:8001` 写入，借用期冻结）；扫 `i <= base` 使"`base` 处恰好是一条还没执行的 GOTO_COUPLE"也落到它后面，与非借用分支 `anchor + 1` 同义。
- 回退安全：懒建新表（`orders_backup_real_index == INVALID`）时 `base = num = 0`，落点 0，与旧行为一致；老存档没有该字段有效值时同样退到 `num`，最坏回到改动前的行为，不会把记录插到玩家命令中间（KI-503 的教训不会复发）。

### D-2 解挂后指令跳到了自动插入项之后（玩家第 2 条，依赖 D-1）

- 玩家表述带"（可能）"，但机制自洽：插入位置在末尾 ⇒ 解挂后按"下一项"推进时必然落到插入项之后。
- **结论先行：D-2 不单独修**，先把 D-1 的锚点改正，再看 `cur_real` 是否仍越位；若仍越位，才是 `DecoupleTrain` 的索引推进（`IncrementRealOrderIndex` / `R3RSkipUnfireableDecoupleOrder`）问题。

### D-3 解挂后没有限制两列车脱离方向 ⇒ 一解挂就撞毁（玩家第 3 条，严重度 高=毁车）

- **玩家口径**：「无论如何」都要限制脱离方向 —— 即这是硬要求，不是可选优化。
- 现状：解挂提交点（`DecoupleTrain` 末尾）只做两半的段/索引/预留清理，**没有任何"两半朝相反方向驶离"的约束**；两半解挂后物理上仍贴在同一格/相邻格，各自 `TrainLocoHandler` 下一 tick 独立起步，若都朝对方一侧 ⇒ `CheckTrainCollision` 命中 ⇒ 撞毁。
- 待取数据（下一组工具调用就去拿）：①`build` 下是否有本轮 `crash-*.log`；②`R3R_debug.log` 在两条 `DECOUPLE-DONE`（`:1209` 一带、`:2727` 一带）之后 20~40 行内，两半的 `direction` / `cur_speed` / `CRT` 行，看是否同向。
- 候选修法（待验证后再选，不预设立场）：
  - 甲：解挂提交点强制两半 `direction` 相反（能立即脱离的几何状态），并在下一 tick 前禁止朝对方一侧启动；
  - 乙：解挂后对后车（解出方）置 `Stopped`/零速一小段，且不订路，等前车离开该格再放行（与 KI-335 的 `DECOUPLE-INTERLOCK` 同族机制，已有实现可复用）；
  - 丙：解挂提交点校验两半的"下一步去向"（各自 `current_order` 的首段路径方向），冲突时把后车延后一 tick 起步。
- **注意**：乙 与既有 `DECOUPLE-INTERLOCK`（KI-335，已实机复测通过）是同一思路，优先复用它而不是新造约束。

**取证结论（2026-10-28，D-3 现场）**：

- 全盘无 `crash-*.log` ⇒ 是**游戏内碰撞/损毁**，不是进程崩溃，不涉及反 desync 冻结区。
- `DECOUPLE-INTERLOCK` 在本轮全日志**零命中**（连同 `DECOUPLE-SEG-SKIP`），而两次 `DECOUPLE-DONE` 都成立：`:1113 DECOUPLE-DONE u=0 co=1 real=3 tx=12 ty=15 x=196 y=248`、`:2567 DECOUPLE-DONE u=57 co=1 real=5 tx=12 ty=15 x=196 y=248`。
  - ⇒ 既有互锁（KI-335）**根本没接管**。调用点 `R3RRecordDecoupleSeam(v, u)`（`:10444`）是无条件的，但函数体内 `if (R3RIsCarOnlyFormation(follower)) return;`（`:2911` 一带，两道早退都不打日志）把 follower=纯车底全挡掉。两次解出的 `u=0`/`u=57` 都是 `co=1`（纯车底），故一次都没登记。
  - 这正是 KI-335 里记为「判据⑥未覆盖」的那条缝：当时理由是"纯车底链恒被 P7 停驻门按住，不必再锁一层"。**玩家第 3 条把这条假设证伪**：车底确实不动，但**留下的机车半会立刻起步**，互锁的对象搞反了——该锁的不是"纯车底自己乱跑"，而是"机车不要朝刚解下的车底那一侧开"。
- 几何（`GEO settle-decouple-u`）：解出侧 27 节（`GEO-END … head=0 n=27 capped=0`），veh 0 在 `12,15 x=196`，往后 `veh17 7,15 x=126` … `veh26 5,15 x=91`，全链 `dir=5 db=0` ⇒ 车底沿 **x 递减（向西）** 铺开；`DECOUPLE-FIRE consist=54` 与 `u=0` 同在 `tx=12 ty=15`。
  ⇒ 机车半（54）与车底链头（0）**同格**，机车一旦朝西侧起步就直接压进车底。这与 A 战线那条"同格豁免"是同一个几何前提：A 只解决了"别误判为永久阻塞"，**没有解决"真的会撞"**。
- 修法选择修正：**不能只照搬 KI-335 的等待语义**（它的等待者是纯车底、等对方离开；这里的危险方是机车）。D-3 需要的是提交点对**两半各自的下一段去向**做一次冲突检查，冲突时按玩家口径"限制脱离方向"——让后起步的一方（通常是要驶离的那一半）先等，而不是两边同时起步。

---

## 十三、第 351 轮：D-3 通过 + 两个新缺陷（玩家 2026-10-28 第五次发言）

### 13.1 玩家原话（决定性口径，原样保留）

> 「我向你报告，解挂后立即撞毁不再发生，但是，解挂后有一方排程出现问题，排程可能丢失进度，因为变回了命令1。还有就是，自动填充命令的插入点可能往前推了一个前往挂接命令」

三条判定：
- **D-3（KI-523）= 实测通过**：解挂后立即撞毁不再发生 ⇒ 第 350 轮的 `R3ROtherChainAheadOfMovingFront()` + `TPR-SEAM-DIR-BLOCK` 生效。KI-523 状态改「已修（第 350 轮，实机复测通过 2026-10-28）」。
- **新缺陷 N-1（KI-524，高）**：解挂后有一半排程丢进度，`cur_real` 变回 0（玩家看到的"命令 1"）。
- **新缺陷 N-2（KI-521 未修好，高）**：自动插入的落点又"往前推了一个前往挂接命令"——即锚点比正确位置小 1，或被插到了 `GOTO_COUPLE` 之前。

### 13.2 采样有效性

`build\R3R_debug.log` 464 933 B / 2026-10-28 **21:16:37**，产生者 `build\openttd.exe` 21:13:22（52 344 832 B）= 含 D-1（KI-521 修法）+ D-3（KI-523 修法）的版本 ⇒ 采样有效，现象由本轮新代码产生。

### 13.3 N-1 取证（排程丢进度）

**铁证三行**（`ORD-RETURN-OWN` 是排程取回的唯一出口）：

- `build\R3R_debug.log:1152` `ORD-RETURN-OWN veh=54 num=4 real=2->0 impl=2->0 bk=65535/65535 orphan=0`
- `build\R3R_debug.log:2603` `ORD-RETURN-OWN veh=57 num=7 real=5->0 bk=65535/65535 orphan=0`
- `build\R3R_debug.log:2734` `ORD-IDX-WATCH site=depotstay-in veh=57 real=4->0 … n=33->7`

读法：`real`（`cur_real_order_index`）在取回那一刻被写成 **0**，而取回前是 2 / 5 / 4 ⇒ 玩家看到"变回命令 1"。`bk=65535/65535` = `orders_backup_real_index` 与 `orders_backup_implicit_index` 都是 `INVALID_VEH_ORDER_ID`（=65535）⇒ **这辆车从来没有被停放（park）过**，备份索引不存在。

**机制**（`src\train_cmd.cpp` 的 `R3RSegmentReturnOrders()`，约 `:7855-7870`）：

```cpp
const VehicleOrderID own_real = chain->orders_backup_real_index;
if (num == 0 || own_real == INVALID_VEH_ORDER_ID || own_real >= num) own_real = 0;  // ← 归零
```

即"备份索引不可用 ⇒ 当作从第一条开始"。凡是走过 `GetEffectiveOrders()` 借用期、但*没有*在 `ORD-BORROW-PARK` 那一刻停放索引的车，取回时一律归 0。

**停放为什么漏**：`ORD-BORROW-PARK` 是唯一写 `orders_backup_*_index` 的地方（`:8022-8029`），条件是 `chain->orders_backup == nullptr && chain->orders_backup_real_index == INVALID`——这是"借端"条件。全日志 `ORD-BORROW-PARK` **只有 1 条**（`:252 ORD-BORROW-PARK veh=0 bkr=0 bki=0 num=6`），而被取回的有 54/57 两辆 ⇒ 绝大多数借用者从未停放（走的是"表留在 `orders` 里、`r3r_follow_orders` 跟随"的另一条路径，或停放点被更早的懒建 `orders_backup`（KI-399 的 `AUTO-ORD-NEWLIST`）占掉条件而跳过）。

**待判定（REG-351-A）**：54/57 为何 `orders_backup` 非空（`orphan=0` 说明表有归属）但两个索引都是 `INVALID`。两种可能：(a) `AUTO-ORD-NEWLIST` 懒建的 `orders_backup` 抢在 `ORD-BORROW-PARK` 之前把 `orders_backup != nullptr` 置真 ⇒ 停放条件整块被跳过（**最可能**，因为 `R3RAutoRecordChainOrders` 在 `:21873` 每 tick 都会被调，而挂车是事件）；(b) 另有一条路径直接把 `orders` 换成控制段的表、不经停放。

### 13.4 N-2 取证（锚点比正确位置小 1 / 落到表首）

`AUTO-ORD` 是记录侧的唯一输出，`anchor=` 就是落点：

- `build\R3R_debug.log:755` `AUTO-ORD seg=3 head=54 slot=0 … iv=[1,2) anchor=1 real=1 n=4`
- `build\R3R_debug.log:792` `AUTO-ORD seg=1 head=0 … iv=[0,1) anchor=0 real=0 n=7`
- `build\R3R_debug.log:2286` `AUTO-ORD seg=4 head=57 … iv=[5,6) anchor=5 real=4 n=7`
- `build\R3R_debug.log:2312` `AUTO-ORD seg=1 head=0 … anchor=0 real=3 n=7`

三组判读：

1. `anchor=0` 且 `head=0` 的自有表（`log:2661-2693` 的 `U-ORD` 序列：17/1/15/17/1/15/17…，`17=OT_WAIT_COUPLE`、`1=OT_GOTO_STATION`、`15=OT_DECOUPLE`）**整张表一条 `type=16`（`OT_GOTO_COUPLE`）都没有** ⇒ 第 350 轮的锚点循环 `for (i=0..base) if(ol->GetOrderAt(i)->IsType(OT_GOTO_COUPLE)) anchor=i+1;` 一次都不命中，`anchor` 停在 `base`——而 `base` 来自 `orders_backup_real_index`，它 = `INVALID`（同上）⇒ `base = num`？不，`log:792` 是 `anchor=0`，说明走的是 `own==INVALID ⇒ base = num` 之外的路径，或 `num` 本身取自另一张表。

2. 更关键的是：**"最后执行的那条 `GOTO_COUPLE`"根本不在本段自有表里**。机车 0 的 7 条自有表里没有 `type=16`，而它此刻跑的是控制段的 33 条表（`log:2346 n=33`）——玩家口径的"最后执行的前往挂接命令"必然指**正在跑的那张表**上的那条。锚点只在 `ol`（本段自有表）里找 ⇒ 结构上找不到 ⇒ 落点退化。

3. `anchor=1 real=1`（`:755`）与 `anchor=5 real=4`（`:2286`）两种形态并存 ⇒ "往前推了一个 `GOTO_COUPLE`"的观感来源：当 `base` 落在 `GOTO_COUPLE` 那一项上时，`anchor = i+1` 是"它后面"；但若 `base` 比该 `GOTO_COUPLE` 小 1（备份索引是挂车那一刻的冻结值、之后没刷新），循环到 `base` 就停 ⇒ `anchor` 停在该 `GOTO_COUPLE` **之前**，正是玩家说的"往前推了一个前往挂接命令"。

⇒ **N-2 的修法与 N-1 同源**：二者都由「备份索引 `INVALID` / 陈旧」+「锚点只在自有表里找」造成。

### 13.5 修法定向（**尚未落地，代码未改**）

- **修 A（N-1）**：`R3RSegmentReturnOrders()` 在 `orders_backup_real_index == INVALID` 时**不得硬归 0**。改为"从借用期在控制段表上所到的位置折算"：优先用 `chain->cur_real_order_index`（借用期由 `R3RSyncDrivingOrders` 继承控制段值，是真实进度）夹到 `[0, num-1]`；若它也无效才退 0。
- **修 B（N-2）**：`R3RAutoRecordChainOrders()` 的锚点改为**在正在跑的那张表里找**最后一个 `OT_GOTO_COUPLE`，再映射回本段自有表的落点；映射失败（自有表没有该命令）时**不退 0**，退到"本段自有表里最后一个已执行项之后"，最坏退 `num`。
- 顺带：`ORD-BORROW-PARK` 的停放条件放宽（`orders_backup != nullptr` 但索引仍 `INVALID` 时也补写索引），修 REG-351-A 的 (a)。

### 13.6 REG-351-A 收窄：`bk=INVALID` 的两条代码级候选（2026-10-28 补查）

先排除一条：`orders_backup_real_index` **不是** NOSAVE 字段，`src\vehicle_base.h:413` 注释明写 "SAVED by the R3VP chunk"，`src\sl\couple_group_sl.cpp:132` 也有 `NSL("order_backup_index", SLE_VAR(Vehicle, orders_backup_real_index, ...))` ⇒ 读档本身不会凭空清零。所以 `INVALID` 只能来自下面两处（全文件把该字段写成 `INVALID` 的只有 5 处）：

**候选① 读档重建清除（`R3RRebuildCouplePriorities()`，`src\train_cmd.cpp:7722-7725`）**
```cpp
chain->r3r_orders_borrowed = (owner != chain);
chain->orders_backup = nullptr;
chain->orders_backup_real_index = INVALID_VEH_ORDER_ID;
chain->orders_backup_implicit_index = INVALID_VEH_ORDER_ID;
```
该函数在读档时对每个链头跑一次（入口在 `:7669 orders_backup != nullptr` 与 `:7685 r3r_follow_orders != nullptr` 两条**保态**分支之后 return），所以只有"两个保态依据都为空"的链才会落到这里；而这一行**无条件**把借用标记重算成 `(owner != chain)`——即便重算结果是"仍在借用"，备份索引也已经没了。**成立条件**：存档时该链 `r3r_follow_orders` 为空/未被 R3VP 指针阶段恢复，但 `orders` 与某段同表 ⇒ 读回后 borrowed=true 且 bk=INVALID ⇒ 归还时归 0。与玩家"反复读档测同一个档"的用法吻合，**当前最可疑**。

**候选② 运行期懒建抢先（`AUTO-ORD-NEWLIST`，`src\train_cmd.cpp:6967-6973`）**
```cpp
seg->orders_backup = OrderList::Create(nullptr, seg);
seg->orders_backup_real_index = INVALID_VEH_ORDER_ID;   // ← 主动清
seg->orders_backup_implicit_index = INVALID_VEH_ORDER_ID;
seg->r3r_orders_borrowed = true;
```
一旦某段被这样懒建出 `orders_backup`，`:8022` 的停放条件 `chain->orders_backup == nullptr && ...` 就**永久不成立** ⇒ 该段从此不会再记录"自己在原表上的位置"，归还时必然 `INVALID`。**反证**：54/57 归还有 `num=4`/`num=7`（说明 `R3RSegmentOwnOrderList()` 返回的是真表，不是懒建表），日志里 `AUTO-ORD-NEWLIST` 是否命中 54/57 需查 → 若未命中则候选②对这两辆车不成立，只对中段成立。

**判定数据（下次采样要的）**：①该车 `AUTO-ORD-NEWLIST` 是否出现；②读档后的 `load-raw` 与 `SEGSAVE-PARK` 两行里该车的 `bk` 值（存档时是多少）；③`:7722` 那一路是否命中（现无探针，需补一行 `ORD-REBUILD-CLEAR`）。

### 13.7 两条候选**全部证伪**，真因更简单（2026-10-28，玩家否定 + 代码/日志双证）

**玩家否定候选①**：原话「读档重建时被清除是不可能的，因为我读档的时候没有任何段处于耦合态」。成立——`:7685` 的 `r3r_follow_orders != nullptr` 与 `:7669` 的 `orders_backup != nullptr` 两条保态分支要求"存档时确实是借用态"，无耦合态的存档两条都为空，而 `:7722` 那一路把 `borrowed` 重算成 `(owner != chain)` 时 `owner` 就是链自己 ⇒ 清完仍是"非借用"，对归还毫无影响。

**候选②（运行期懒建）同样证伪，两条独立证据**：
1. `build\R3R_debug.log` 全文件 `AUTO-ORD-NEWLIST`、`AUTO-ORD-NOLIST` **零命中**（`ORD-BORROW*` 亦只有 `:252 ORD-BORROW-PARK veh=0 bkr=0 bki=0 num=6` 这一条）。
2. 结构上也不可能：`R3RSegmentOwnOrderList()`（`src\train_cmd.cpp:6685-6689`）是
   ```cpp
   if (seg->orders != nullptr) return seg->orders;
   return seg->orders_backup;
   ```
   **优先 `orders`**；而懒建的前提是 `:6952 if (ol == nullptr)`。54/57 归还时 `num=4` / `num=7` 正是从它们自己的 `orders` 表读出来的 ⇒ 它们在 AUTO-ORD 记录时 `ol != nullptr`，永远进不到懒建分支。

**真因（单一、局部）**：`orders_backup_real_index` 的 `INVALID` **不是"被谁清掉"，而是它的初始值——这辆车从来没有 park 过**。park 只在 `R3RSyncDrivingOrders()`（`:8022`）里"从自己的表切到别人的表"那一刻发生，而按 `:8012` 的注释，链节本来就在 owner 表上时会提前 return 并**保持进度**。没 park ⇒ 不需要恢复 ⇒ 当前位置 `cur_real_order_index` 就是它自己的坐标。

但 `R3RSegmentReturnOrders()` 把"没备份"当成了"位置未知"，`:7859` 直接落 0：
```cpp
if (num == 0 || own_real == INVALID_VEH_ORDER_ID || own_real >= num) own_real = 0;
```
`:7847` 的注释自陈 "备份缺失或已越界……则落 0 —— 宁可从头跑"，就是这个"宁可从头跑"在 54/57 身上丢了合法进度。日志里 `real=2` / `real=5` / `real=4` 是**被丢弃的那个有效值**（num 分别 4 / 7 / 7，全部在界内）。

**修法（比 §13.5 的三层方案小得多）**：`INVALID` 不再落 0，改为"保留现值、仅越界时夹紧"：
```cpp
VehicleOrderID own_real = chain->orders_backup_real_index;
if (own_real == INVALID_VEH_ORDER_ID) own_real = chain->cur_real_order_index;
if (num == 0) { own_real = 0; own_impl = 0; } else { if (own_real >= num) own_real = num - 1; ... }
```
理由：真越界（借用期跑的是 owner 的长表）仍被夹住，第 299 轮/KI-472 要防的断言不会回来；而没有 park 的情形恰好说明"当前值就是自有表坐标"，保留即正解。同理处理 `orders_backup_implicit_index`。
