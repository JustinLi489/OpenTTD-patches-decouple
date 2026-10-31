# R3R 第 354 轮：veh57-59 跳过 5 号排程 + veh48-50 TrainController 异常

## 〇、玩家报告（原话，2026-10-28）

> 见 build/R3R_debug.log，veh57-59 没执行完 5 号排程（可能是排程索引 4）就跳过了它，
> 而此时，veh48-50 的 traincontroller 也出了问题。

## 一、采样有效性

| 项 | 读数 |
|---|---|
| `build\R3R_debug.log` | 496 352 B / 3 258 行，2026-10-28 23:10:21 |
| `build\openttd.exe` | 52 344 832 B，2026-10-28 22:37:55 |

exe 时间戳 = 第 353 轮修复版（KI-526 补 `Couple()` 停位 + KI-527 改 `AUTO-ORD` anchor 判据）。
⇒ 本日志是**第 353 轮修复后的首次实机复测**，KI-526/KI-527 是否生效需在本轮一并核读。

## 二、待取证（本轮先登记，随后回填）

1. veh57-59 段：`ORD-AFTER-COUPLE` / `ORD-RETURN-OWN` / `AUTO-ORD` / `SKIP-STOPPED` /
   `AUTO-ORD-SKIP` 在该段上的读数，判断"跳过 5 号排程（索引 4）"是：
   - (a) 归还侧落 0 后索引推进越过了索引 4；还是
   - (b) `AUTO-ORD-SKIP` 的自动区间把索引 4 吞掉；还是
   - (c) 段单段化（`SEGID-KEEP` / `SEGTR-SNAP ... nseg=1`）时机不匹配。
2. veh48-50 段：`LOCO-MOVE` / `SPD-DROP` / `TRP` / `CRT` / `CPL-*` / `RESERVECONSIST` 读数，
   判断 TrainController 出了什么问题（无预留 / 无移动分支 / 预留在错误方向）。
3. 两条现象的时间相关性：是否同一 tick 区间、是否同一次解挂/耦合引发。

## 三、复测判据（第 354 轮）

①veh57-59 段必须逐条执行到索引 4 的完成态再前进；②veh48-50 的 `LOCO-MOVE` 与 `TRP` 必须持续出现且方向与
目的地一致；③KI-526/KI-527 判据不回退（`ORD-RETURN-NOBK` 恒 0、`ORD-RETURN-OWN` 的 `real` 不再变 0）。

## 四、取证回填（2026-10-28，首轮）

### 4.0 两个口径更正（先说清，避免后面误读）

| 探针 | 真口径 | 源码锚点 |
|---|---|---|
| `DEPOT-ARR veh=.. real=%d(%d)` | 第二个数**不是**"上一次索引"，而是**当前 real 索引处那条订单的类型** | `src\train_cmd.cpp:22624-22636` |
| `DEPOT-ARR curType=%d` | `current_order.GetType()`（活订单，可能尚未随索引刷新） | `src\train_cmd.cpp:22627` |
| `CPLSPD realbrk=%d` | `UsingRealisticBraking()`，**不是**"排程索引坏了" | `src\train_cmd.cpp:1574-1584` |

⇒ `real=5(15)` 读作「real 停在索引 5，索引 5 上是一条 `OT_DECOUPLE`（15）」，与 `DECOUPLE-FIRE real=5` 自洽。

### 4.1 veh=57 时间线（`build\R3R_debug.log` 行号）

| 行 | 探针要点 | 判读 |
|---|---|---|
| :353/:354 | `ordlist-insert-in/out veh=57 real=4->4 n=33 ord=…3FAC0->…892E40 front=1 spd=100` | 借用开始：表指针从自己那张切到 owner 那张（n=33） |
| :355 | `AUTO-ORD seg=4 head=57 slot=0 … anchor=2 real=4 own=0 borrowed=1 n=7` | 仍 anchor(2)≠real(4)，KI-527 判据③未达成（见 D-2） |
| :772-982 | `LINEND veh=57 tile=35,12 … spd=52→5` | 正常进站减速 |
| :983 | `ORD-IDX-WATCH site=del-implicit-in veh=57 real=3->4 impl=3->4 tt=1->4 n=33` | 删隐式订单，索引 3→4 |
| :985 | `SPD-DROP veh=57 tile=35,12 prev=5 order=3 real=4 … dtile=923` | 停稳，real=4，活订单类型 3 |
| :988 | `DEPOT-ARR veh=57 spd=0 real=5(15) curType=4 … tx=35 ty=12 destTx=27 destTy=7 tileEqDest=0` | **real 已是 5**，且车停在 35,12 而非目的地 27,7 |
| :989 | `DECOUPLE-FIRE consist=57 tx=35 ty=12 real=5 mode=2 num=1 segs=2 plat=30/0` | 解挂闸门在索引 5 上触发 |
| :994 | `TDTRY ok=1 v=57 u=0 … borrowed=1 u_eng=0 v_eng=1 u_next=1` | v=机车侧、u=纯车底侧 |
| :997 | `ORD-RETURN-OWN veh=57 num=7 real=5->1 impl=5->1 bk=1/1 orphan=0` | 归还把自己的表拿回来，但**落点是 1**（不是停位快照 5，也不像 0） |
| :1004/:1011 | `settle-in/out tag=decouple-v veh=57 real=4->2 / 4->3 n=33->7` | 提交点把索引收到 3 |
| :1005 | `SEGID-KEEP tag=decouple-v head=57 id=4 nseg=1 reason=auto-range` | 单段化（auto_range 保段 ID） |
| :1008 | `AUTO-ORD-SKIP seg=4 head=57 slot=0 from=2 to=3 iv=[2,3) order=0` | 自动填充区间 [2,3) 被跳过 |
| :1121 | `LOCO-AFTER-DECOUPLE veh=57 curType=0 real=3 idxnext=6` | 解挂后停索引 3 |
| :1136/:1137 | `TPR-BLOCKED … other=0 otherTile=35,12 caronly=1` → `TPR-SEAM-EXEMPT` | 第 221 轮解挂同格豁免生效，未误判阻塞 |
| :1141-1146 | `CRT veh=57 order=6 origin=36,12 td=8 found=1` → `TRP veh=57 real=3 type=6 ok=1 res=1` | 之后正常订路开出 |
| :1941 | `loco-entry veh=57 real=3->4 spd=60` | 索引再推进到 4 |

### 4.2 疑点 D-1（**已成立**：索引 4 在未到达目的地的情况下被跨掉）

- `:988` 时机：`real=5`、`destTx/destTy=27,7`、`tx/ty=35,12`、`tileEqDest=0` ⇒ 索引 4 的订单**没有到达其终点**就已不再占据 real。
- `:985`（real=4）与 `:988`（real=5）之间**零 `ORD-IDX-WATCH`** ⇒ 4→5 这次写入没有经过任何被 watch 的站点（`del-implicit` / `syncdriving` / `ord-push` / `settle`），是一次裸赋值/裸 `IncrementRealOrderIndex()`。
- 待定：索引 4 的订单类型未知（日志无订单表 dump）。`curType=4` 只是活订单类型，且它带 `dest=27,7`，说明活订单可能还没随索引刷新。

### 4.3 猜测根因（待验证）G-1：`R3RSkipUnfireableDecoupleOrder()` 把 4 推成 5

- 依据：`src\train_cmd.cpp:9850-9867` 是唯一"不看是否到达、无条件 `IncrementRealOrderIndex()`"的循环，且它**直接改索引、不打 ORD-IDX-WATCH**。
- 成立条件：索引 4 本身必须是 `OT_DECOUPLE`。若索引 4 是站点/车库类订单，此猜测即被证伪。
- 验证判据：拿到 veh57 的订单表第 5 条类型；或临时加一行 `DECOUPLE-SKIP-TRY` 探针（`i`/`real`/`type`）。

### 4.4 猜测根因（待验证）G-2：索引 4 的站点订单被"通过站点"语义消费

- 依据：`:983 del-implicit` 说明索引 3/4 一带确实走过隐式订单删除逻辑（列车未停稳即"通过"站点）。
- 若索引 4 也是站点订单而列车只是路过，则 real 4→5 属隐式通过 —— 玩家的观感正是"没执行完就跳过"。
- 与 G-1 互斥，二者以索引 4 的订单类型三者择一。

### 4.5 疑点 D-2（已成立）：KI-527 判据③（`AUTO-ORD` 的 anchor == 该段自己的表进度）仍未达成

`:355 AUTO-ORD seg=4 head=57 … anchor=2 real=4 own=0 borrowed=1 n=7` —— anchor 仍取 2，而 real=4。
第 353 轮把 `base` 改成 `own_is_real ? own : num`（`src\train_cmd.cpp:7124` 一带），此处 `own=0 borrowed=1` ⇒ 应走 `base=num`；但打印出来的 anchor 仍是 2，需要复核 anchor 的取值点是否就是那个 `base`（也可能 anchor 打的是 `cur_real_order_index` 的另一种换算）。**这是回归项，必须在下一轮定位。**

### 4.6 veh=48-50（TrainController）读数

- `CPLSPD veh=48`（第 322 轮 GOTO_COUPLE 限速分解探针）连续读数：
  `tick=13741 spd=27 strict=5 aend=5 aitem=5 adv=5 gap=0 decel2=240 nocab=0 revd=0 spr=0 sig=0 ordmax=65535 realbrk=1 db=0`
  → 每 tick 掉 3/2/1，到 `tick=13746 spd=5` 后**钉死在 5**（tick 13746→13790 全是 `spd=5`）；
  同段 `gap` 由 `0` → `-1` → `-2`（预留终点已经落到车当前位置之前），`db` 在 tick 13745 由 0 变 1。
- `:3197 DEPOTBLK veh=48 tile=38,11 track=01 eng=1 isdepot=0 ontile=0 idmatch=0 pend=0 stopped=0 parked=0 spd=5 destRaw=2 depotIdx=4294967295`
- `:3230 LOCO-MOVE veh=48 tick=13789 tile=38,11 j=193 spd=5 track=01 dir=5 db=1 stuck=0 dtile=1571`
- `:3231 RESV-EXT-PRE veh=48 tile=37,11 enterdir=0 tracks=0x1 reserved=0x0 doRes=0 real=2 spd=5`
- `:3257/:3258 POSCONSIST veh=48 eng=500 before=0 after=2 flip=0 consec=0 head=48`

判读：**移动分支仍在跑**（`LOCO-MOVE` 有行、`j=193`），不是第 242 轮那种"根本进不了移动分支"；
问题是它把速度压到 5 后 `gap<0`（预留已用尽/终点在身后）继续以 5 爬行且不再向前订 —— 典型"接近挂车目标时预留终点与车身重叠"的形态（与 KI-202/KI-232 一族同源，未定性）。

### 4.7 疑点 D-3（待取证）：两条现象是否同源

veh=57 解挂在 tick≈13412（`:989` 所在 CPL-PAIRSCAN 块 tick=13412），veh=48 的 `CPLSPD` 读数在 tick 13741+，相隔约 330 tick（同一会话、同一解挂事件之后）。
是否同一次解挂引发（veh=48 去挂 veh=57 解出的 `u=0`）**待确认**：`CPL-PAIRSCAN act=48 ord=16 destStn=2 actTile=42,10` 显示 veh48 的意图确实是"去挂车"。

## 五、请玩家确认（按编号回填，避免重问）

- C-1 veh57（车号 57 那台机车）的订单表第 1~7 条分别是什么？重点是**第 5 条（索引 4）**的类型与目的地（到站/车库/路点/解挂/等待挂接）、是否有装卸或满载等待。
- C-2 解挂那一刻 veh57 停在哪个车站/路点（游戏内坐标 35,12 附近）？那条命令要求它在该站做什么？
- C-3 veh48-50 那列的目标是不是"去挂 veh57 解出来的车底"？
- C-4 你观察到的 "traincontroller 出问题" 具体表现：原地抖动 / 慢慢爬 / 报"正在前往"不动 / 卡在 5 km/h？

## 六、玩家四条答复回填（2026-10-29，玩家原话要点）

### 6.1 C-1 / C-2：veh57 的订单表（玩家列出 6 条，自称 7 张）

| 用户编号 | 内容 | 推得索引 |
|---|---|---|
| 1 | 前往路点 1 | 0 |
| 2 | 前往车站 1 挂接 | 1 |
| 3 | 前往车站 2（**自动填充**） | 2 |
| 4 | 前往路点 2 | 3 |
| 5 | 前往路点 3 | 4 |
| 6 | 前往车站 2 挂接 | 5 |
| （7） | **玩家未列**（“7 张排程”只给 6 条）⇒ 缺失一条，疑为尾部 `OT_DECOUPLE` | 6 ? |

玩家原话：「veh57 的 7 张排程分别是（**编号仅供区别！**）：前往路点 1，前往车站 1 挂接，前往车站 2（自动填充），前往路点 2，前往路点 3，前往车站 2 挂接。」
玩家原话：「解挂那一刻 veh57 在**车站 2**，正**前往路点 2**」⇒ 解挂发生时的**语义位置 = 索引 3（路点 2）**，不是索引 5。

### 6.2 三条推论（当轮定稿，后续取证以此为准）

- **推论 R-1：猜测根因 G-2（“索引 4 是站点订单被 del-implicit 消费”）——证伪。**
  玩家明确「5 号排程 = 索引 4 = 前往路点 3」，是路点不是车站。
  路点订单**不参与**“通过站点 / del-implicit”语义，故 `:983 del-implicit-in real=3->4` 不可能是在消费索引 4 本身；
  `del-implicit` 只能消费它当时真正在处理的那条（站点类）订单。
- **推论 R-2：G-1（`R3RSkipUnfireableDecoupleOrder` 把 4 推成 5）—— 与 R-1 互斥同时，出现新的硬矛盾（见 D-4）。**
- **推论 R-3（关键）：`:988 DEPOT-ARR real=5(15)` 与玩家订单表**无法同时成立**。**
  玩家说索引 5 = 「前往车站 2 挂接」（类型应为 `OT_GOTO_COUPLE`=16 或 `OT_WAIT_COUPLE`=17），
  而 `:988` 打印「索引 5 上那条订单的类型 = **15 = `OT_DECOUPLE`**」。
  ⇒ 必有一项不成立，候选三选一：
  - **(a)** 我上一轮对 `real=%d(%d)` 的“口径更正”是错的（括号里不是该索引的订单类型）；
  - **(b)** 打印用的是 **owner 表（n=33）坐标**下的索引 5，不是玩家那张 7 条表的索引 5 ——
    借用期 `v->orders` 指向 owner 表，若探针走 `v->orders->GetOrderAt(real)` 就必然读到 owner 表的第 6 条；
  - **(c)** 玩家记错或订单表在此后被改过。
  **本轮第一优先取证 = 判定 (a)/(b)。** 若不判死，(R-1)(R-2) 全部建立在错坐标上，可能整段推翻。

### 6.3 C-3 / C-4：veh48（挂车方）

- 玩家原话：「veh48-50 当时就是**去挂 veh57 解出来的车底**」⇒ 与 `CPL-PAIRSCAN act=48 ord=16 destStn=2 actTile=42,10` 吻合，配对关系成立（D-3 关闭：两条现象**同源**，同一次解挂引发）。
- 玩家原话（问题表现，两条独立）：
  1. 「去挂接的时候，**预留没一次性铺到下一个安全停车位**」；
  2. 「**半路上它还偏离预留走错岔道了**」。

### 6.4 猜测根因 G-3（待验证）：挂车寻路的预留轨迹与实际行车道岔不一致（KI-346 同族残留）

- 支撑：玩家描述 2（“偏离预留走错岔道”）＝预留订在岔道 A、TrackFollower 实际走岔道 B。
  第 240 轮 KI-346 修的正是同一机理（单段挂车回溯预留 `rp()` 里进入轨写成
  `TrackdirToTrack(ReverseTrackdir(cur_td))`，恒等于当前轨，换槽双轨格必订错），但那只修了
  **`rp()` 两处**；「安全停车位（`FindSafeCouplePosition`）」与「挂车 YAPF back-walk」两条支路是否同病**未取数**。
- 支撑：玩家描述 1（“没一次性铺到下一安全停车位”）＝`CPL-SAFE`/`CPL-BEST` 求出的安全位与 `RP` 实际铺出的格数不匹配
  （预留中途 return / 递归帧 `cur==tg` 提前返回不订自己 ⇒ 终点格缺失，KI-346 备注里已提到这一点）。
- 待验证判据：
  - ① `CPL-SAFE tile=` 与 `CPL-RES destTile=` 是否同一格；
  - ② `RP cur=` 序列是否连续、终点是否等于 `CPL-BEST tile=`；
  - ③ veh48 实际经过的 tile 序列（`LOCO-MOVE`/`GEO`）与 `RP cur=` 序列逐格比对，找出分岔那一格；
  - ④ 分岔格是否为双槽格（`bits=0x30`）。

### 6.5 待补登记

- 疑点 D-4：见 6.2 R-3（`real=5(15)` 与订单表冲突）。
- 疑点 D-5：玩家“7 张排程”只列 6 条，缺一条；若缺失的是尾部 `OT_DECOUPLE`，则索引 6 = DECOUPLE，
  而日志显示索引 5 = DECOUPLE（同一冲突的另一面）。
- 疑点 D-6：解挂发生在「正前往路点 2」（索引 3）时，若 DECOUPLE 在索引 5/6，则**解挂闸门提前触发**本身就是第二个独立缺陷
  （`DECOUPLE-FIRE` 的条件是“停稳在前一条命令终点 + next 是 DECOUPLE”，与“正前往”矛盾）。

## 七、第二轮取证：口径纠正 + 新猜测根因 G-1′（2026-10-29）

### 7.1 枚举钉死（`src\order_type.h:76-94`，实测输出）

`3=OT_LOADING`、`4=OT_LEAVESTATION`、`5=OT_DUMMY`、`6=OT_GOTO_WAYPOINT`、`7=OT_CONDITIONAL`、
`8=OT_IMPLICIT`、`15=OT_DECOUPLE`、`16=OT_GOTO_COUPLE`、`17=OT_WAIT_COUPLE`。

- **纠正**：上一轮把 `curType=4` 读成“表里第 5 条订单的类型”是**错的**。
  `curType` = `current_order.GetType()`（`train_cmd.cpp:22627`），而 4 = `OT_LEAVESTATION`
  是引擎的**运行时伪订单**（列车停稳/离站态），表里根本没有这条订单。
- `:985 order=3` = **`OT_LOADING`（正在装货）** ⇒ 与“停在车站 2”自洽，不是路点。
- `:1141 CRT order=6` = **`OT_GOTO_WAYPOINT`** ⇒ 与玩家“前往路点”自洽。
- `(15)` 仍是真·表内订单类型：`train_cmd.cpp:22626` 取
  `consist->GetOrder(consist->cur_real_order_index)->GetType()`。

### 7.2 疑点 D-4 关闭：(b) 成立 —— `real` 是 **owner 表坐标**

`DEPOT-ARR` 的括号取自 `consist->GetOrder(real)`，而 `Train::GetOrder()` 走
`GetEffectiveOrders()` ⇒ **借用期指向 owner 表（n=33）**。
⇒ `real=5(15)` = **owner 表第 6 条是 `OT_DECOUPLE`**，不是玩家那张 7 条表的第 6 条（“前往车站 2 挂接”）。
⇒ 玩家订单表与日志**不矛盾**，是两张不同的表。

### 7.3 猜测根因 G-1′（强，待最终验证）：切表/还表时 `cur_real_order_index` 从不换算，两套坐标被当成一套

证据链全在同一辆 veh=57 上：

| 行 | 原文要点 | 判读 |
|---|---|---|
| `:353` | `ORD-IDX-WATCH site=ordlist-insert-in veh=57 real=4->4 impl=4->4 tt=4->4 n=33 ord=…3FAC0->…892E40 front=1` | 切到 owner 表（n=33）那一刻 **real 保持 4 不变**。玩家表索引 4 =「前往路点 3」；owner 表索引 4 = 完全另一条订单 ⇒ **坐标未换算** |
| 借用期 | real 在 owner 表坐标上 4 → 5 | 推进全部发生在外表 |
| `:988` | `real=5(15)` owner 表第 6 条 = `OT_DECOUPLE` | 解挂闸门（只认“当前 real 就是解挂”，`train_cmd.cpp:22641-22647`）在**玩家表其实还停在索引 3（路点 2）**时触发 ⇒ 与玩家 C-2 原话逐字吻合 |
| `:997` | `ORD-RETURN-OWN veh=57 num=7 real=5->1 bk=1/1` | 归还时突然改用**自己表坐标**，5 换成停位 1 |

⇒ **索引 2 / 3 / 4（自动填充、路点 2、路点 3）在这条路径上从未执行就被丢弃**，
正是玩家所说的“没执行完 5 号排程（索引 4）就跳过了它”。

**结论（本轮定稿方向）**：第 352/353 轮的 KI-525 / KI-526 修的是“**停位值与停位宿主**”，
没有修“**索引语义**”。停位侧与归还侧各用一套坐标，中间没有任何换算点；
`real` 是单值，跨界时既不随表切换换算，也不带“我在哪张表”的标记。

- 对照物：`R3RSkipUnfireableDecoupleOrder()`（`train_cmd.cpp:9850-9870`）整函数只 `break` 在非
  `OT_DECOUPLE` 上，**不可能**把路点订单 4 推成 5 ⇒ **G-1 证伪**；
  `R3RAdvanceAfterDecouple()`（`:9898-9941`）只在解挂**之后**跑，与解挂前的 4→5 无关。
- 剩余待验：`ORD-BORROW-PARK veh=57` 那一行的 `bkr=` / `bki=` 是否为 4（若为 1，说明停位取自另一处、
  连停位都不在借用起点采样）。

### 7.4 veh48 的 G-3 细化为两条独立子缺陷

- **G-3a 预留没铺到“下一个安全停车位”**：`CPL-SAFE` / `CPL-BEST` 求出的安全位与 `RP` 实际铺出的格数不匹配
  （KI-346 备注已自陈“递归帧 `cur==tg` 提前 return 且不订自己 ⇒ 终点格缺失”）。
- **G-3b 半路偏离预留走错岔道**：预留订下的轨 ≠ `TrackFollower` 实走轨（KI-346 同族；第 240 轮只修了
  `rp()` 两处，`FindSafeCouplePosition` 与挂车 YAPF back-walk 两条支路**未取数**）。
- 判据（下一轮取数）：① `CPL-SAFE tile=` 是否等于 `CPL-RES destTile=`；② `RP cur=` 序列是否连续到终点格；
  ③ veh48 实走 tile 序列与 `RP cur=` 逐格比对，找出分岔格；④ 分岔格是否 `bits=0x30` 双槽格。

（以下取证结果按顺序回填——第三轮起续接）

## 八、第三/四轮取证（2026-10-29 01:44 采样）

### 8.0 采样身份（先钉死，后续所有行号只对本采样有效）

| 项 | 读数 |
| --- | --- |
| `build\R3R_debug.log` | `3483` 行 / `514805` B / mtime `2026-10-29 01:44:21` |
| `build\openttd.exe` | `52344832` B / mtime `2026-10-28 22:37:55`（＝第 353 轮 KI-526/527 修复版） |

**REG-354-B（疑点·已定性）**：本备忘 §四/§七 里引用的 `:988 DECOUPLE-FIRE` 这类行号属于**同一场景的另一次采样**
（行号整体偏移：旧 `:988` = 新 `:1216`）。原因是 `CPL-PAIRSCAN` / `CPLSPD` 这类**按帧**探针行数随帧率浮动，
两份采样的行号不可互换。⇒ 下文一律用「本采样行号 + 原文片段」双锚定；引用 §四/§七 旧行号时必须重新定位。

### 8.1 veh57 自有排程的硬证据（`COUPLE-FAIL` 后的 `LOCO-ORD` 逐条 dump，`:2381`-`:2387`）

```
0  type=6  (OT_GOTO_WAYPOINT)  前往路点 1
1  type=16 (OT_GOTO_COUPLE)    前往车站 1 挂接
2  type=1  (OT_GOTO_STATION)   前往车站 2（自动填充）
3  type=6                       前往路点 2
4  type=6                       前往路点 3
5  type=16                      前往车站 2 挂接
6  type=2  (OT_GOTO_DEPOT)      回库
```

三条硬结论：

1. **玩家只列了 6 条，第 7 条是“回库”（`OT_GOTO_DEPOT`）**，不是解挂。
   ⇒ 上一轮“第 7 条 = 尾部 `OT_DECOUPLE`”的推断**证伪**（D-4 猜想链彻底关闭）。
2. **veh57 自有表里根本没有 `OT_DECOUPLE`。** ⇒ `:1217` 的 `DECOUPLE-FIRE real=5` 执行的那条
   `OT_DECOUPLE` **只可能来自借来的 owner 表**（n=33，owner=6）。
   ⇒ “veh57 自己的排程被跳过”在**借用期**并不成立：借用期跑的是别人的表，自有索引全程冻结。
3. 玩家口述“5 号排程”＝自有表索引 `4`（路点 3），与 `LOCO-ORD 4 type=6` 逐字吻合。

### 8.2 停位来源（KI-526 判据）：`ORD-BORROW-PARK` **全日志 0 命中**

- `findstr "ORD-BORROW-PARK ORD-RETURN-NOBK ORD-RETURN-OWN"` 三模式只命中 **1 行**：
  `:1225 ORD-RETURN-OWN veh=57 num=7 real=5->1 bk=1/1`。
- ⇒ 第 353 轮新加的那个停位点（`Couple()` 里跟随指针三行之前）**在本场景一次都没走到**
  （四道守卫之一跳过），停位值来自别处；**停位值 = 1**，不是 4。
- 借用起点 `:607 ordlist-insert-in veh=57 real=-1->4 ... n=-1->33 front=1`
  ⇒ 加入 33 条表时被赋值 `4`，此时自有索引已冻结，两者是**两套坐标**（G-1′ 继续成立）。
- 【疑点 REG-354-E】“停位=1”与“冻结时自有进度应为 2（车站 1 挂接已做完）”不一致，需要一个裁定点，
  当前无探针覆盖⇒无法判定是“停在了已完成的那条”还是“停位取自另一处”。

### 8.3 解挂后自有表的落点（“跳排程”的真正现场）

| 行号 | 原文片段 | 判定 |
| --- | --- | --- |
| `:1232` | `settle-in ... real=4->2 n=33->7` | 切回自有表（7 条） |
| `:1236` | `AUTO-ORD-SKIP from=2 to=3 iv=[2,3)` | 索引 2（自动填充的车站 2）被跳过＝**第 264 轮口径“自动填充永不执行”** |
| `:1239` | `settle-out ... real=4->3` | 落点定为 3 |
| `:1349` | `LOCO-AFTER-DECOUPLE veh=57 curType=0 real=3 idxnext=6` | 从**路点 2**（索引 3）继续 |

⇒ 归还 1 → settle 直接推到 3：**索引 1（车站 1 挂接）与索引 2（自动填充）都没有执行**。
索引 2 的跳过有据（设计），**索引 1 的跳过没有任何探针记录裁决过程** ⇒ **REG-354-C（疑点）**：
无法判定“跳过已完成的车站 1 挂接”是落点修复的正确行为还是又一次无换算的坐标平移。

### 8.4 解挂后的第二次 4→5

| 行号 | 原文片段 |
| --- | --- |
| `:2371` | `LINEND veh=57 tile=42,13 spd=5 brk=15 clamp=0 len=2` |
| `:2372` | `del-implicit-in veh=57 real=3->4 ... n=7->7` |
| `:2373` | `del-implicit-out veh=57 tag=deleted real=-1->4 n=-1->7` |
| `:2374` | `SPD-DROP veh=57 tile=42,13 prev=5 order=9 real=4` |
| `:2375` | `DEPOT-ARR veh=57 real=4(6) curType=9 tx=42 ty=13 destTx=21 destTy=7 tileEqDest=0` |
| `:2379` | `CPL-S0-CHK veh=57 real=5 order=16` |
| `:2388` | `COUPLE-WAIT-OFFDEST veh=57 tile=42,13 destStn=2 destTile=923 onStnTile=1` |

**口径纠正（重要，作废 §七 的一段证据链）**：`DEPOT-ARR` 的 `destTx/destTy/tileEqDest` 取自
`consist->dest_tile`（探针在 `train_cmd.cpp:22596-22615`），而该字段此时由 `current_order` 决定；
`curType=9`＝**伪订单**，不是表内那条 `OT_GOTO_WAYPOINT` 的目的地。
⇒ **`tileEqDest=0` 不能作为“索引 4 未到达”的证据**；KI-529 原写的核心证据链作废（详见 §八.6）。

现状：veh57 永久停在 `42,13` 等挂接（`:3034`/`:3035`/`:3044` 同形反复，`COUPLE-WAIT-OFFDEST` off-dest）。

### 8.5 veh48：G-3a / G-3b 均已做实

`build\_r354_scan3.txt`（`veh=48|act=48` 且剔除 `PAIRSCAN`，共 476 行）读出：

| 行号 | 原文片段 | 判定 |
| --- | --- | --- |
| `:3092` | `RESV-EXT-PRE veh=48 tile=38,11 enterdir=0 tracks=0x9 reserved=0x8 doRes=0` | 38,11 需 0x9 |
| `:3093` | `RESV-FOREIGN-RES veh=48 tile=38,11 res=0x8 tracks=0x9 spd=87 real=2` | **别的列车占着 0x8** |
| `:3006` | `CPL-RESERVE veh=48 reserved=1 next=3 path=6 booked=6` | 第一次订下 6 格 |
| `:3359` | `CPL-RESERVE veh=48 reserved=1 next=2 path=5 booked=5` | 第二次只剩 5 格 |
| `:3360` | `RESV-CPL-XTILE veh=48 tile=38,11 tracks=0x9 resv=0x8 ret=0x2 accept=0x9` | 冲突复核 |
| `:3361` | `CPL-RET veh=48 tile=38,11 ... ret=0x1 why=nores-final spd=87 real=2` | **38,11 没订下来，路径在此截断** |
| `:3367` | `LOCO-MOVE veh=48 tile=39,11 spd=85 track=04 dir=6` | 仍然出发 |
| `:3393` | `LOCO-MOVE veh=48 tile=38,11 spd=74 track=01 dir=5` | **开进未订下的格** |
| `:3413` | `RESV-LA-REBUILD veh=48 tile=38,11 spd=64 order=16 own=1 look=1 end=2` | look-ahead 只剩 **2 格** |
| `:3414` | `CPLSPD ... strict=15 gap=1` | 开始刹 |
| `:3426` | `CPLSPD ... strict=5 gap=0` | 归零 |
| `:3475` | `CPLSPD veh=48 tick=13773 tile=38,11 spd=5 strict=5 gap=-1` | **gap 转负、5 km/h 僵死** |

- **G-3a 成立**：`CPL-BEST tile=27,12 cost=12352`（`:3119`）给出的安全位，`CPL-RESERVE` 只铺到
  `36,12` 一带就被 foreign res 截断 ⇒ 预留没铺到下一个安全停车位。
- **G-3b 成立**：`CPL-RET why=nores-final` 之后**没有任何“未订满就不许出发”的门禁**，机车照走，
  驶入未预留格；到达后 look-ahead 只重建出 2 格，随即 `gap: 1→0→-1`、速度钉在 5 km/h 永久停住（KI-530 形态复现）。
- 【疑点 REG-354-D】`RESV-FOREIGN-RES` 不打 owner 车号 ⇒ 无法判定 0x8 属于哪列车。若它属于 veh57 那条
  “停在 42,13 off-dest 等挂接”的链（或 veh54 链），则两车互为**死锁对**（一个等挂接不动、一个等它让路）。
  需要探针补 owner。

### 8.6 本轮结论

| 编号 | 内容 | 状态 |
| --- | --- | --- |
| R354-1 | **路径预留失败后无出发门禁**：`CPL-RET why=nores-final` 仍照走 → 驶入未订格 → look-ahead 只剩 2 格 → `gap` 转负、5 km/h 僵死 | 已做实（G-3a+G-3b），待改码 |
| R354-2 | **借入/归还索引语义不换算**（G-1′）：停位 1 vs 借入 4，且 `ORD-BORROW-PARK` 未命中 | 继续成立 |
| R354-3 | 解挂后 `settle` 把 1 直接推到 3 无探针 | REG-354-C，待补探针 |
| R354-4 | KI-529 原证据链（`tileEqDest=0`）**作废** | 需改用“路点身份判别”重新取数 |

**R354-1 修法候选**（下一轮择一）：
(a) `CPL-RESERVE` 截断/失败 ⇒ 本次不出发（清 `cur_speed`/`subspeed`，保持等待态）；
(b) 截断后立即改以“已订到的最后一格”为临时目标重新寻路；
(c) look-ahead 失败时允许借 foreign 预留格的另一条空闲轨（此处 `tracks=0x9` 中 `track 0x1` 空闲）。

### 8.7 下一轮取数清单

1. REG-354-D：给 `RESV-FOREIGN-RES` 加 owner（或临时打印 `GetTrainForReservation` 结果）。
2. REG-354-C：给 settle 的“落点裁决”加一行日志（起点索引 / 跳过原因 / 终点索引）。
3. KI-529 重取：判别 `42,13` 是路点格还是站台格（`IsRailWaypointTile` + 站点/路点 ID），
   并把 `destTx/destTy` 改成取**表内 real 那条订单**的目的地。
4. REG-354-E：停位=1 的裁定点归属。

## 九、玩家口径纠正（2026-10-29，第二轮；两条都推翻本节前文）

### 9.1 玩家原话要点（权威口径，取代一切推断）

> 「42，13是路点2的格子，惊喜吧。」
> 「veh48你也分析错了，它正常的等待直到自己能够完全占有前去挂接才从"等待空余轨道"的状态中启动过来，
> 但是，它明明能一次到位，却偏偏愚蠢的在一个非安全停车位的道岔那里结束预留。然后，后面它走完预留，
> 在一个非安全停车位的道岔区又铺设了一次预留到自己的目的地（要挂的车底），然后到了另一个道岔，拐上了错误的路。」

### 9.2 结论 A：KI-529 定论 —— 索引 4 确系"未到达即被跨掉"

- 玩家已答 §8.7 第 3 条：**`42,13` 是路点 2 的格子**。
- 因此 §4.2 的 D-1（已成立）与 §8.3/§8.4 的两次推进**都是真跳单**：
  - 车停在**路点 2**（订单 3 的目的地，已执行完），`cur_real=4` 指向**订单 4 = 前往路点 3**；
  - 车没有离开 `42,13` 就出现 `CPL-S0-CHK real=5`（`:2379`），下一 tick `ORD-IDX-WATCH site=loco-entry real=4->5`（`:2400` 附近）。
- 于是 §4.2 那条"`tileEqDest=0` 不能当证据"的作废口径**只对 `DEPOT-ARR` 那一行有效**，不再影响总判定：
  到达判据所比对的 `dest_tile` 就是路点 2 的格，而 `real` 已经是路点 3 的订单 ⇒ **判定用了上一个订单的目的地**。
- 新猜测根因 **G-4（强，待验证）**：`del-implicit` 把 `real` 3→4 时**只改索引、不重算派生状态**
  （`current_order` / `dest_tile` / `last_station_visited`），下一个到达判定用陈旧 `dest_tile`（= 路点 2 = 车所在格）
  ⇒ `at_order_dest` 恒真 ⇒ 再推一格 4→5。
  - 与 KI-527（`AUTO-ORD` anchor）、KI-399（N1 锚点）**同族**：索引被外部改写后派生状态未同步。
  - 待验证判据：在 `del-implicit` 出口与 `ProcessOrders` 的到达判定处各打一行 `dest_tile`，
    若 `del-implicit` 之后 `dest_tile` 仍是路点 2 而 `real==4` ⇒ 成立。

### 9.3 结论 B：KI-531 根因**改写**（前文 G-3a/G-3b 的框定错误，作废）

我在 §8.5 把症状框成"预留**失败**（被 foreign res 截断）后没有出发门禁"。玩家的现场口径否掉了这个框定：

- 事实 1：veh48 是**正常等到能完全占有才启动**的 ⇒ 出发时的预留是**成功**的，不存在"带着半截预留强行出发"。
- 事实 2：它**能一次到位**，却在**非安全停车位的道岔**结束预留。
- 事实 3：走完这段后，**又在另一个非安全位道岔区订了第二次预留**奔目的地，结果在**第三个道岔拐错路**。

代码锚点（`src\train_cmd.cpp`，本轮实读）：

| 行 | 内容 | 意义 |
|---|---|---|
| `:18277` | `while (!IsSafeWaitingPosition(consist, res_dest.tile, res_dest.trackdir, true, pf.forbid_90_deg, true))` | 预留终点不安全就逐格延伸，直到找到安全停车位 |
| `:18287` | `orders.SwitchToNextOrder(true)` | 延伸过程中会**切换订单视图**再寻路 |
| `:18291-18303` | `DoTrainPathfind(...)`；`cur_dest.tile != INVALID_TILE` 但 `!res_dest.okay` ⇒ 释放 + `break` | 找到路但订不下来 ⇒ 放弃 |
| `:18306-18314` | `/* No order or no safe position found, try any position. */ if (!TryReserveSafeTrack(consist, res_dest.tile, res_dest.trackdir, true)) {...} break;` | **兜底分支：明知 `res_dest` 不是安全位，仍在这一格下单**；成功则 `CTTRF_RESERVATION_MADE` 保留 |

⇒ **"在非安全停车位的道岔上结束预留"是这条兜底分支的显式设计**（上游注释即"try any position"）。
对普通到站停车无害（下一 tick 会继续延伸），对 R3R 挂车链致命：预留合法终止在道岔 ⇒ 车开完后
以**道岔上的 trackdir** 作为下一次 `origin` 重新寻路 ⇒ 岔道选择由第二次寻路的 `best_track` 单独决定，
与第一次"延伸时假定的那条岔"不同源 ⇒ 第三个道岔拐错（玩家事实 3）。

新猜测根因 **G-5（强，待验证）**：`TryReserveSafeTrack` 兜底分支在**挂车目标（`OT_GOTO_COUPLE`）**下
应视为失败（释放 + `mark_stuck` + 保留"等待空余轨道"），而不是"订在非安全位当作成功"。
支撑：玩家明示"它正常的等待直到能完全占有才启动"——兜底分支恰好绕过这条纪律。

### 9.4 修法候选（待拍板，未落地）

- **F-1（最小改动，首选验证）**：`:18307` 兜底分支在 `current_order.IsType(OT_GOTO_COUPLE)`
  （或 `r3r_pending_depot_couple` / `R3RIsCoupleTarget` 语境）时**不调用** `TryReserveSafeTrack`，
  直接走失败路径（`FreeTrainTrackReservation` + `MarkTrainAsStuck` + 清 `CTTRF_RESERVATION_MADE`）。
  ⇒ 车停在原地继续等，直到能一次订到安全位；与玩家"正常等待"的期望一致。
- **F-2（治根）**：延伸失败时**不要把 `res_dest` 当作终点**，而是回退到"已订下的最后一格"再判安全；
  若该格仍不安全则整段释放。风险：可能使部分窄道场景永久无法出发（需 `mark_stuck` 兜底）。
- **F-3（禁止分段）**：`OT_GOTO_COUPLE` 语境下禁止"预留→走完→再预留"的分段，第二次寻路必须以
  第一次的**同一 trackdir 序列**为约束（等价于引入"路签"），成本最高。

### 9.5 待验证 / 待取数（下一轮，按优先级）

1. **V-1（验 G-4）**：`del-implicit` 出口打 `real` + `current_order.type` + `dest_tile`；
   `ProcessOrders` 到达判定处打同一组。判据：`real==4` 且 `dest_tile==路点2` ⇒ G-4 成立。
2. **V-2（验 G-5）**：在 `:18306` 兜底分支入口打一行
   `RESV-UNSAFE-TAIL veh= order= tile= td= safe=0 extended=0`。
   判据：veh48 出现该行且 `tile` 为道岔（`GetTrackBits().Count() > 1`）⇒ G-5 成立。
3. **V-3**：打 `CPL-BEST`（寻路目标）与本次 `res_dest`（`TryPathReserve` 实订终点）两行到同一 tick，
   量化"明明能一次到位"的距离（预期 `27,12` vs `38,11`）。
4. **V-4**：`GetTileTrackBits(38,11).Count()` 与 `IsSafeWaitingPosition(38,11,td)` 的直接读数，
   确认 38,11 是道岔且判为不安全。

### 9.6 玩家二次确认 + 拍板（2026-10-29）

玩家原话（按四步重述 veh48，作为本轮唯一事实基准）：

> 「车正常等待，直到能完全占有才启动，所以出发时的预留是成功的；但这次成功的预留终止在一个不安全的位置，也就是道岔；
> 走完这段后，它又在那个道岔订了第二次预留；最后在第三个道岔拐错方向。」

⇒ G-5 的证据链钉成四段，探针必须逐段可计数：
① `reserve #1 made=1` 但 `res_dest` 非安全位（= `:18306` 兜底命中）；
② 走完 → 第二次 `TryPathReserve` 的 `origin` 就在那个道岔上；
③ `reserve #2` 同样落在非安全位或选岔与第一次不同源；
④ 第三处道岔 exit 选择与预留表不一致。

**玩家拍板**：「两个都先执行探针取证」⇒ 本轮只加只读探针（V-1 + V-2/V-3），**不改任何行为**，不落 F-1/F-2/F-3。

探针落点定稿：
- `RESV-UNSAFE-TAIL`：`:18306` 兜底分支入口（含 `GetTrackBits().Count()`，把"是不是道岔"写进行里）；
- `RESV-TERM`：`:18317` 起 `if (result_flags & CTTRF_RESERVATION_MADE)` 块内（预留最终终止点 + `IsSafeWaitingPosition` 自证 + `resv#` 计数）；
- `RESV-ORIGIN`：`R3RTryPathReserveCore` 入口（第二次预留的起点是否就在道岔上）；
- `ORD-DEST-CHK`：`del-implicit` 出口 + 到达判定处（V-1，`real/curType/dest_tile/at_dest`）。

### 9.7 探针落地（2026-10-29，**已实现，零行为变更**）

玩家拍板"两个都先执行探针取证"⇒ 本轮只加只读探针，不落 F-1/F-2/F-3，不改任何判据。

| # | 探针 | 落点（`src\train_cmd.cpp`，行号为改前锚点） | 判读 |
|---|---|---|---|
| P-1 | `ORD-IDX-WATCH` **扩容** | `R3RWatchOrderIndexSite()` 的 `:2520` 那一行，行尾追加 `curType= dest= dtX= dtY= vtile=` | 一次覆盖全部站点（含 `del-implicit-in/out`、`loco-entry`、`skipgate-in`）⇒ G-4 判据变成"`real==4` 但 `dest` 仍是路点2" |
| P-2 | `RESV-UNSAFE-TAIL` | `:18306` 兜底分支入口前（`try any position` 注释处），同时置 `r3r_unsafe_tail=true` | 出现即证明"延伸失败、走了不安全兜底"；`tcount>=2` 即道岔；`ext=0` 表示没延伸成功 |
| P-3 | `RESV-TERM` | `:18317` `if (result_flags & CTTRF_RESERVATION_MADE)` 块首 | 每次**成功**预留的终止点自证：`safe=` 是当场重算的 `IsSafeWaitingPosition`；`safe=0 && tail=1` 即 G-5 第一段；同一 veh 相邻两条 `RESV-TERM` 的 `vtile` 对比即"第二次预留是否从那个道岔起订" |

实现细节：`tcount` 用 `GetTileTrackBits(tile, TRANSPORT_RAIL, 0).Count()`（**故意不用** `GetTrackBits()`——后者在 `rail_map.h:168` 断言 `IsPlainRailTile`，站台/路点格会崩）；`safe` 用与 `:18277` while 完全相同的六参 `IsSafeWaitingPosition`，避免两把尺子。

**复测判据（veh48 侧，逐段可计数）**
1. 出征前必须出现 `RESV-TERM ... safe=1`（第一次预留成功且终止在安全位）— **若为 `safe=0` 则 G-5 第一段坐实**；
2. 若见 `RESV-UNSAFE-TAIL`，其 `tile` 应等于随后 `RESV-TERM` 的 `tile`（兜底点=终止点）；
3. 同一 veh 的**下一条** `RESV-TERM` 的 `vtile` 应等于上一条的 `tile`（第二次预留从道岔起订，G-5 第二段）；
4. 第三处道岔拐错：用 `LOCO-MOVE ... track=` 与上一条 `RESV-TERM` 的 `td` 交叉比对，若实际 track ≠ 预留轨 ⇒ 岔道选择与预留不同源（G-5 第三段）。

**复测判据（veh57 侧，V-1）**
5. `ORD-IDX-WATCH site=del-implicit-out` 处 `real=4` 且 `dtX,dtY` 仍等于路点2 的格、`vtile` 也在路点2 ⇒ G-4 成立（陈旧 `dest_tile`）；
6. 若 `dtX,dtY` 已经是路点3 的格而 `real` 仍被推成 5 ⇒ G-4 证伪，改查 `at_order_dest` 的容差/伪订单分支。

**构建**：`R3R_fullbuild.cmd inc detach`（复用既有入口，未新建 .cmd）。结果待回填本节。

### 9.8 构建失败记录（2026-10-29，历史，已由 §9.9 收口）

- 第 1 次 `inc detach`：**失败**，`build\R3R_incbuild.done` = `EXIT_CODE=1`，
  `train_cmd.cpp(18328)` 与 `(18354)` 均 `error C3861: "GetTileTrackBits": 找不到标识符`
  ⇒ 该 API 不在 `train_cmd.cpp` 的包含链里（`GetTileTrackBits` 属 `tile_map.h` 家族，本文件未 include）。
- 修法（已落码，未编译）：`tcount` 改 `(IsPlainRailTile(t) ? (int)GetTrackBits(t).Count() : -2)`，
  `-2` = 非 plain rail（站台/路点，**不能**调 `GetTrackBits`，见 `rail_map.h:168` 的 `IsPlainRailTile` 断言）。
  该写法只在 38,11 这类 plain-rail 道岔上返回 2，语义与预期一致。
- `read_lints` 两次均 0 条。
- 第 2 次 `inc detach` 启动时**被用户取消** ⇒ 当前 `build\openttd.exe` 仍是 2026-10-28 22:37:55 的旧版，
  **探针尚未进入 exe**，不得据此判读任何 G-4/G-5。
- 待办：用户同意后重新 `R3R_fullbuild.cmd inc detach`，成功三件套 =
  `done` 内含 `EXIT_CODE=0` + 日志尾 `[n/n] Linking CXX executable openttd.exe` +
  `build\openttd.exe` 的 mtime 晚于 `src\train_cmd.cpp`。

### 9.9 构建成功（2026-10-29 终局，**探针已进 exe**）

用户澄清"构建不是他取消的"（上一轮 `^C` 属环境问题），重新启动后成功：

| 判据 | 读数 |
|---|---|
| `build\R3R_incbuild.done` | `EXIT_CODE=0`，mtime **2026-10-29 02:26:35** |
| 日志尾 | `[3/3] Linking CXX executable openttd.exe`、`==== R3R build ended 02:26:42 MODE=inc EXIT_CODE=0` |
| `build\openttd.exe` | mtime **2026-10-29 02:26:35**（晚于 `src\train_cmd.cpp`） |
| exe 字面量 | `RESV-UNSAFE-TAIL veh=` 命中、`RESV-TERM veh=` 命中（findstr /M 各返回 exe） |
| `read_lints` | `src\train_cmd.cpp` 0 条 |

**三次编译错（记下来省下次）**
1. `error C3861: "GetTileTrackBits": 找不到标识符`（`:18328`/`:18354`）
   ⇒ 该 API 不在 `train_cmd.cpp` 的包含链里，**别用**。
2. `error C2228: ".Count 的左边必须有类/结构/联合"`（`:18328`）
   ⇒ 本树 `TrackBits` 是裸位集（`rail_map.h:170` 是 `(TrackBits)GB(...)`，C 风格强转），
   **没有** `.Count()` 成员；统计位数必须 `CountBits((uint)bits)`（`bitmath_func.hpp`）。
3. 最终写法（两处一致）：
   `(IsPlainRailTile(res_dest.tile) ? (int)CountBits((uint)GetTrackBits(res_dest.tile)) : -2)`
   —— `-2` 表示非 plain rail（站台/路点），**绝不能**对它调 `GetTrackBits`（`rail_map.h:169` 断言）。

⇒ **现在可以复跑取证了**，判据见 §9.7 的 6 条。

---

## 十、第 355 轮：新日志判读（2026-10-29 02:29:29）

### 10.1 采样有效性

| 项 | 读数 |
|---|---|
| `build\R3R_debug.log` | 550 271 B，mtime `2026-10-29 02:29:29` |
| `build\openttd.exe` | mtime `2026-10-29 02:26:35`，52 344 832 B = **探针版**（§9.9） |
| 结论 | exe 早于日志 ⇒ 本轮日志由探针版产生，**判读有效** |

### 10.2 关键标签计数（`Select-String | Group-Object Pattern`）

| 标签 | 次数 | 判定 |
|---|---|---|
| `CPL-PAIRSCAN` | **1392** | 无节流刷屏（见 10.4 疑点 REG-355-B） |
| `COUPLE-FAIL` | 11 | 三台机车反复找不到候选 |
| `LOCO-MOVE veh=48` | 11 | veh48 **确实在跑**（进过移动分支） |
| `CLEAR-STN` | 2 | — |
| `TPR-BLOCKED` | 1 | veh57 被**同格**车底阻塞 |
| `CPL-GATE` | 1 | `reject=target-not-wait` |
| `RESV-TERM veh=` | **1** | 全场唯一一次成功预留 |
| `RESV-UNSAFE-TAIL veh=` | **0** | **G-5 第一段未复现**（无兜底预留） |
| `RESV-GHOST` | 0 | 无幽灵清扫 |
| `RESV-NOBOOK` | 0 | 无停驻等待者订路 |

### 10.3 三条硬读数（原文逐字）

```
:1408  TPR-BLOCKED veh=57 tile=35,12 spd=2 mf=35,12 origin=35,12 okay=1 td=8
                    other=0 otherTile=35,12 otherMf=35,12 otherSpd=0 otherOrder=0
                    otherFirst=0 caronly=1
:1420  RESV-TERM veh=57 real=3 type=6 tile=42,13 td=8 tcount=-2 safe=1
                    tail=0 spd=2 vtile=35,12
:1583  [R3R] CPL-GATE reject=target-not-wait site=depot act=54 tgt=27 aOrd=16
                    tOrd=0 aStop=0 tStop=1 grp=0 tOwn=1 tShared=1 tFollow=0
```

逐条判读：

1. **`:1420` 是全场唯一成功预留**：`veh=57`、订单 `type=6`（`OT_GOTO_WAYPOINT`）、
   终点 `42,13`、`tcount=-2` ⇒ 该格**不是 plain rail**（路点/站台格，探针里 `-2` 的语义就是这个，
   见 §9.7），`safe=1`、`tail=0` ⇒ **终止点是安全位、没走兜底**。
   ⇒ **G-5 判定：本会话未复现**（无 `RESV-UNSAFE-TAIL`，无道岔终止，无 `safe=0`）。
   注意 `tcount=-2` 不能当"道岔"读，它是"非 plain rail"占位值。
2. **`:1408` 是 KI-330 同族复发**：`other=0`（解出的纯车底）、`otherTile == tile == mf`（同格 35,12）、
   `otherOrder=0`（`OT_NOTHING`）、`otherSpd=0`、`otherFirst=0`（已是独立列车）、`caronly=1`；
   而 `origin=35,12 okay=1` ⇒ **自己的预留终点是安全位、订路本身健康**，失败纯属同格车误判。
   ⇒ 与第 221 轮记忆里 KI-330 的形态逐字同构（`FollowTrainReservation` →
   `CheckTrainsOnTrack` 不排除自身 ⇒ `MarkTrainAsStuck`）。第 221 轮拟的"解挂缝同格豁免"修法**仍在待拍板状态，未落地**。
3. **`:1583` 指向一条更硬的结构问题**：`act=54` 想挂 `tgt=27`，
   `aOrd=16`(`OT_GOTO_COUPLE`) 而 `tOrd=0`(`OT_NOTHING`) ⇒ 等待车底 `veh27` 的 `current_order`
   **不是 `OT_WAIT_COUPLE(17)`**，被闸门按 `target-not-wait` 拒。
   与 `CPL-PAIRSCAN ... pool=0 ... best=-1 ... near=-1`（尾部 1392 行全是这个）互相印证：
   **候选池恒空**，三台机车（48/54/57）永远挂不上。

### 10.4 疑点与猜测根因（按 2026-10-11 规则即时登记）

- **REG-355-A（猜测根因，待验证）**：`veh27`（纯车底链，`n=33`、`spd=0`、`parked=0`、
  `ORD-IDX-WATCH site=load-rebuild curType=0`、`vtile=1,15`）读档后 `current_order` 停在
  `OT_NOTHING`，而 `veh54` 恰好也在 `1,15`（尾部 `act=54 actTile=1,15`）⇒ 两者同格且车底无等待订单。
  待验证：`veh27` 是否本应带 `OT_WAIT_COUPLE`（即"解出方/车底读档后丢失等待订单"），
  还是它本来就该由 `GOTO_COUPLE` 的一方主动插单。取证口：`WAIT_COUPLE` / `DECOUPLE` / `SETTLE` 里 `veh=27` 的行。
- **REG-355-B（测量缺陷，确定）**：`CPL-PAIRSCAN` **1392 条 / 550 KB，每 tick 每 act 一行**，
  完全没有 `R3RDbgEdge` 节流。第 135 轮曾因无节流探针把日志刷到 1.1 GB（KI-217）。
  ⇒ 该探针必须加边沿/窗口抑制，否则复测时日志会再次膨胀到不可读。
- **REG-355-C（疑点）**：`COUPLE-FAIL loco=48 order=16 tx=42 ty=10` 连续 6 次
  （`:276/:348/:451/:594/:786/:967`）而 `LOCO-MOVE veh=48` 显示它一直在动
  （`42,10 → 40,10 → 39,10 → 39,11 → 38,11`，`db=1` 倒车），
  `dest` 恒为 `tile(27,7)`（`dest=923`）。⇒ 它能动但**挂不上**，`tx/ty` 打的是自己的格
  （见记忆 42642809 的探针口径），不能读成"目标在 42,10"。

### 10.5 本轮结论

- **G-5（成功预留终止在道岔）在本会话未复现**，判据 1~4 全部无法成立（无 `RESV-UNSAFE-TAIL`、`safe=0` 零条）。
- **G-4（veh57 陈旧 dest 被误判到达）在本会话也未复现**：`veh57` 的 `dest` 恒 `27,7`，
  真实卡点是 `:1408` 的同格阻塞 + `:1583` 的候选池空。
- 本会话真正的两个卡点：①KI-330 同格阻塞（`:1408`，修法待拍板）；②等待车底 `current_order=OT_NOTHING`（`:1583`，新）。
- 另有一条必须立刻修的测量缺陷：`CPL-PAIRSCAN` 无节流（REG-355-B）。

### 10.6 REG-355-A 的**决定性证据**（原文锚点，2026-10-29 补）

`findstr /N` 命中 **11 条**，逐字相同：

```
:259  SKIP-STOPPED veh=27 order=0 real=17 spd=0 tile=1,15 parked=0 front=1 nord=33 idx=0 tt=0
:344  （同形）
:447  :590  :782  :963  :1618  :2168  :2358  :2774  :3532  （全部同形）
```

判读（三个字段同名不同义，必须分清）：

| 字段 | 值 | 含义 |
|---|---|---|
| `order` | `0` | **`current_order` 的类型** = `OT_NOTHING` |
| `real` | `17` | **有效订单表里索引 `idx=0` 那一条的类型** = `OT_WAIT_COUPLE` |
| `idx` | `0` | 当前真实订单索引 |

⇒ `veh27` 的**排程里确实有 `WAIT_COUPLE`**（`real=17`，`nord=33` 条表），
但它的 `current_order` 仍是 `OT_NOTHING`（`order=0`）——**订单从未被加载进活动字段**。
因此 `:1583` 的 `CPL-GATE reject=target-not-wait ... tOrd=0` 不是"车底没写等待订单"，
而是"**写了但没加载**"。

与代码注释互相印证：`src\train_cmd.cpp` 的 `SKIP-STOPPED` 门（约 `:22410-22435`）
在 `spd==0` 时**直接 `return true`**，跳过了后续的订单处理/加载路径；而
`train_cmd.cpp:10443-10468`（解挂后 `InvalidateVehicleTickCaches()`）的注释正是在记这同一种病：
「否则 WAIT_COUPLE 永不加载 ⇒ `reject=target-not-wait` 永远」。

⇒ **REG-355-A 状态：猜测根因已由日志做实（`real=17` vs `order=0`）**，
剩余待验项只剩"为何读档/解挂后活动订单没同步"（是 `SKIP-STOPPED` 早退导致，
还是缓存失效点没覆盖 `veh27` 这条链，二者可再分一次探针）。
修法方向（**未落地，待拍板**）：`SKIP-STOPPED` 门对 `OT_WAIT_COUPLE`/`OT_GOTO_COUPLE`
放行（只跳"真开车"的处理，不跳订单加载），或在该门之前补一次
`current_order` 与 `GetOrder(idx)` 的同步。

---

## 十一、探针字典（玩家 2026-10-29 提问「这些探针代表什么」，逐条对齐代码）

字段表全部按打印点原文核对，行号为当前源码位置。

| 探针 | 打印点 | 它回答的问题 |
|---|---|---|
| `SKIP-STOPPED` | `train_cmd.cpp:22428`（门在 `:22410` 一带） | 「这列停着的车，本 tick 为什么不处理命令」 |
| `TPR-BLOCKED` | `:19172` | 「这次订路为什么被别车挡下」 |
| `RESV-TERM` | `:18350` | 「一次**成功**的预留，终点落在哪、安不安全」 |
| `RESV-UNSAFE-TAIL` | 同一函数内（`r3r_unsafe_tail` 分支） | 「走了兜底分支、把终点钉在不安全位」（G-5 签名） |
| `CPL-GATE` | `:13921` | 「挂接闸门否决了谁、理由是什么」 |
| `CPL-PAIRSCAN` | `:14235` | 「挂接候选池的漏斗：每一道筛子吃掉几个」 |
| `COUPLE-FAIL` | 寻路失败点 | 「找不到候选车底」 |
| `LOCO-MOVE` | 移动分支入口 | 「真的进了『能开车』的分支」（证明车在动） |
| `CLEAR-STN` | 整格预留释放点 | 「站台/路点整格预留被清掉」 |

### 11.1 `SKIP-STOPPED`（本轮最关键的一条）

门语义：`cur_speed == 0` 且不在库内挂接流程 ⇒ 直接 `return true`，**整 tick 不处理订单**。

字段含义（`train_cmd.cpp:22428`）：

| 字段 | 含义 |
|---|---|
| `order` | `current_order.GetType()`，**此刻活动命令的类型** |
| `real` | 有效订单表第 `idx` 条的**真实类型**（`r->GetType()`，无表为 -1） |
| `spd` | `cur_speed` |
| `tile` | 列车所在格 |
| `parked` | `r3r_parked`（被车库工具停放） |
| `front` | `IsFrontEngine()`（非链头永远开不走） |
| `nord` | 有效订单条数（-1 = 没有任何排程） |
| `idx` | `cur_real_order_index`（真实订单索引） |
| `tt` | `cur_timetable_order_index`（时间表索引） |

⇒ 本轮 `veh=27 order=0 real=17 spd=0 tile=1,15 parked=0 front=1 nord=33 idx=0 tt=0` 的读法：
**它有 33 条排程、当前真实那条是 17（WAIT_COUPLE）、它在链头、没被停放、也不在库里，但活动命令是 0（OT_NOTHING）** ⇒ 订单没被加载。

### 11.2 `TPR-BLOCKED`（`:19172`）

触发点：`FollowTrainReservation()` → `CheckTrainsOnTrack()` 命中"别的列车在我路径上"。

| 字段 | 含义 |
|---|---|
| `veh` / `tile` / `spd` | 我（被挡的链头）/ 我所在格 / 我的速度 |
| `mf` | 我的**运动车头**所在格（倒车时≠`tile`） |
| `origin` / `okay` / `td` | 我这次预留的**终点**、是否合法、终点 trackdir |
| `other` | 判定为障碍的那列车的链头车号 |
| `otherTile` / `otherMf` | 它所在格 / 它的运动车头格 |
| `otherSpd` | 它的速度（0 = 停着） |
| `otherOrder` | 它的活动命令类型 |
| `otherFirst` | `other->First()->index`（=0 表示它自己就是链头） |
| `caronly` | 它是否纯车底（解挂出来的那半截） |

⇒ 判"真挡路"还是"误判"只看一条：**`otherTile == tile`（同格）就是误判**（KI-330 型）；`otherTile == origin` 且 ≠ 我的格，才是真的有人占着我的终点。

### 11.3 `RESV-TERM`（`:18350`）与 `RESV-UNSAFE-TAIL`

| 字段 | 含义 |
|---|---|
| `real` / `type` | 真实订单索引 / 活动命令类型 |
| `tile` / `td` | 预留**终点格** / 终点 trackdir |
| `tcount` | 终点格的轨道位数量；**`-2` = 该格不是 plain rail（站台/路点），不是"道岔 2 轨"** |
| `safe` | 独立重算的"这是不是安全停车位"，健康必须是 1 |
| `tail` | 是否走了兜底分支（`try any position`） |
| `spd` / `vtile` | 当时速度 / 列车所在格 |

`RESV-UNSAFE-TAIL` 只在兜底分支下打；它与 `safe=0` 同时出现才是 G-5 的签名。

### 11.4 `CPL-GATE`（`:13921`）

`reason` 是结论，`site` 是哪个调用点。字段：`act/tgt` 主动/被动车号，`aOrd/tOrd` 双方活动命令类型，
`aStop/tStop` 双方停驶旗标，`grp` = `R3RCoupleAllowed()` 的结果，`tOwn/tShared/tFollow/aFollow/aShared` = 双方
有没有表 / 是否原生共享排程 / 是否跟随借用。

⇒ `reject=target-not-wait tOrd=0` 读法：**对方的活动命令是 OT_NOTHING，不是 WAIT_COUPLE** ⇒ 闸门认为它不是等待者。

### 11.5 `CPL-PAIRSCAN`（`:14235`）

| 字段 | 含义 |
|---|---|
| `state` | `backoff`/`none`/`refused`/`locked`/`steady` |
| `act` / `ord` / `actTile` / `actSpd` | 主动侧车号 / 命令 / 所在格 / 速度 |
| `pool` | 池里参与筛选的候选数 |
| `s_chain` `s_target` `s_stop` `s_dest` `s_lock` `s_ign` `s_ignd` | **各道筛子各吃掉几个**（不在链上 / 不是挂接目标 / 还在跑 / 目的地不符 / 已被别人锁 / 同组忽略 / 忽略目的地） |
| `best` / `bestDist` / `paired` / `wait` | 留下来的最佳候选、距离、是否已配对、等待计数 |
| `near*` | 距主动侧最近的**被拒**候选快照（车号/距离/为什么被拒/格/命令/停驶旗标） |

⇒ 本轮 `pool=0 … best=-1 … near=-1` 读法：**第一道筛子之前就没有候选**，不是"被某道闸门拒掉"，所以问题在候选池为空（上游），不在筛选逻辑。

### 11.6 其余三条

- `COUPLE-FAIL loco=… order=… tx=… ty=…`：寻路没找到候选。**`tx/ty` 打的是机车自己的格**（既有口径），
  不能读成"目标在 42,10"（REG-355-C）。
- `LOCO-MOVE veh=… tile=… track=… db=…`：确实进了移动分支（能订路/能开车）。它出现说明"停驶分支"不是唯一路径。
- `CLEAR-STN tile=… phase=… actor=… head=…`：整格预留（站台/路点）被释放的记录，用来追幽灵预留。

### 11.7 判读口诀

1. 车不动 ⇒ 先看有没有 `LOCO-MOVE`：有 ⇒ 进了移动分支；没有 ⇒ 卡在停驶分支（`SKIP-STOPPED`）。
2. 挂不上 ⇒ 先看 `CPL-PAIRSCAN` 的 `pool`：`pool=0` ⇒ 池空（上游）；`pool>0` 且 `best=-1` ⇒ 被闸门吃了（看 `s_*` 与 `nearWhy`）。
3. 被拒 ⇒ 看 `CPL-GATE` 的 `reason`；`target-not-wait` 一定回到对方 `tOrd` 与 `SKIP-STOPPED` 的 `order/real` 对比。
4. 挡路 ⇒ 看 `TPR-BLOCKED` 的 `otherTile` 是否等于 `tile`：同格 = 误判，异格 = 真挡。

---

## 十二、本轮探针现象的综合判读（玩家 2026-10-29「那么你观察的探针现象意味着什么」）

### 12.1 一句话结论

本轮日志里所有「空 / 0 / 找不到」的读数，**不是四件独立故障，而是同一件事的四层回声**：
`current_order` 没有被加载（停在 `OT_NOTHING`），于是下游每个读它的闸门都如实报「我没有看到等待者」。

### 12.2 四个 `0` 的谱系（关键：别把它们当四件事分别修）

| 读数 | 出现处 | 它是谁 | 性质 |
|---|---|---|---|
| `order=0` | `SKIP-STOPPED veh=27` ×11 | **病源**：`current_order` 的类型 | 真因 |
| `tOrd=0` | `CPL-GATE reject=target-not-wait` | 被挂方 27 的活动订单 | 病源的**回声 1** |
| `otherOrder=0` | `TPR-BLOCKED ... other=0 caronly=1` | 同格那根纯车底的活动订单 | 病源的**回声 2**（另一列车） |
| `pool=0 near=-1` | `CPL-PAIRSCAN` ×1392 | 候选池 | 病源的**回声 3**（= 回声 1 的后果） |

⇒ 判读次序必须是：**先认病源（`order=0` vs `real=17`），再认回声**。
若按回声去改（例如放宽 `CPL-GATE` 的 `target-not-wait`、或在 YAPF 里加豁免），会把一个「状态没同步」的
缺陷伪装成「判据太严」，并在其它所有依赖 `current_order` 的地方留下同类假阴。

### 12.3 排除了三个「良性无辜解释」

`SKIP-STOPPED` 同一行里的三个字段就是用来排除的，本轮读数把三条都否掉了：

- `front=1` ⇒ 它不是「非链头、本来就走不了」；
- `parked=0` ⇒ 不是「被车库工具停放」；
- `nord=33` ⇒ 不是「没有排程」（33 条）。`idx=0` ⇒ 也不是「索引越界找不到条目」。

⇒ 剩下唯一可能：**条目在、索引对、车是链头、没被停放，但没人把它装载进 `current_order`**。
这不是数据错，是**流程没走到**。

### 12.4 `order=0` 与 `spd=0` 的因果方向（决定修法该加在哪）

现场是 `spd=0` + `order=0` 同时成立。两种可能：

- (i) `spd=0` 使 `SKIP-STOPPED` 早退 ⇒ 订单加载整段不执行 ⇒ `order` 留 `0`（**先停后空**）；
- (ii) 订单本就空 ⇒ 车无从起步 ⇒ `spd=0`（**先空后停**）。

`train_cmd.cpp:22410` 的门在 `spd==0` 时 `return true`，路径上不给订单加载机会 ⇒ (i) 与代码一致；
(ii) 需要一个「谁把 `current_order` 清空且不再装载」的上游，本轮日志无此证据（`:10443-10468` 的注释把
这种清空点指向解挂后缓存失效，但 veh27 本轮没有解挂记录）。
⇒ 记为**倾向 (i)，待一条探针定案**（在 `SKIP-STOPPED` 之前打 `current_order` vs `GetOrder(idx)` 对比）。

### 12.5 `pool=0` 的意义：问题在漏斗上游，不在筛子

`CPL-PAIRSCAN` 的 `s_chain/s_target/s_stop/s_dest/s_lock/s_ign/s_ignd` 各代表一道筛子。
本轮 `pool=0` 且 `near=-1`（连被拒候选都没有）⇒ **第一道筛子之前池子就是空的**。
所以「全图找不到等待车底」这个结论是**候选池为空**造成的，不是筛选条件太苛刻。
⇒ 任何「放宽筛选条件」的修法对本轮现象**必然无效**（这是本轮最容易走错的岔路）。

### 12.6 `TPR-BLOCKED` 的意义与它的独立性

`:1408` 的 `otherTile == tile == mf`（同格）+ `origin ... okay=1`（自己的预留终点合法）⇒ 本条是
**独立的第二缺陷**（同格误判，KI-330 家族），不能靠修 `current_order` 解决。

但它有一个**可验证的耦合**：`otherOrder=0` 说明同格那根纯车底的活动订单也是空的
⇒ 与 12.2 的「回声 2」同源。**判据**：修好订单加载后重跑，此处的 `otherOrder` 应变成 `17`；
若真变成 `17`，则「同格豁免」修法仍需要（因为豁免判的是格子与速度，不是订单类型），
但 `otherSpd=0` 这条解释会从「订单空导致停车」改成「它本来就是等待者、合法停着」——
**这恰好说明同格豁免是正确方向而非补丁**（等待者停在机车脚下是正常状态）。

### 12.7 测量层面的含义（不是游戏缺陷，但影响后续所有判读）

`CPL-PAIRSCAN` 1392 行占 550 KB 日志的绝大部分，而真正有价值的低频事件（`RESV-TERM` 仅 1 条、
`RESV-UNSAFE-TAIL` 0 条、`TPR-BLOCKED` 仅 1 条）被挤在噪声里。
⇒ **本轮「某探针 0 条」的结论可信度依赖「它没被吃掉的机制」**：`TPR-BLOCKED`/`RESV-TERM` 走各自的
边沿标签，不受 `PAIRSCAN` 影响，故「0 条 = 未触发」成立；但 `CPL-PAIRSCAN` 自身的数值统计
（`pool` 是否曾非 0）在刷屏下难以人工核对，必须先修节流再取统计量。

### 12.8 判读纪律（本轮教训）

1. 见到 `0`/空，先问「是**真的没有**，还是**读的人没看到**」——本轮四个 0 全是后者。
2. 同名字段跨探针比对（`order` vs `tOrd` vs `aOrd` vs `real`）必须逐一对齐语义，`order` 与 `real`
   在同一行里就不同义。
3. 一条探针的字段设计要能**排除良性解释**（`front/parked/nord/idx` 四个字段的价值就在这里）。

---

## 十四、第 355 轮定案（玩家 2026-10-29 回日志，build\R3R_debug.log）

### 14.1 §13.2 的猜测根因【证伪】

| 判据 | 期望（若猜测成立） | 实测 | 结论 |
|---|---|---|---|
| `wclr` | >0 | **0**（16/16 行） | **`R3RClearStaleWaitMarker` 从未碰过这两条链** |
| `CPL-WAITCLEAR` | 至少若干条 | **0 条** | 同上，清理路径完全没参与 |
| `blocked` | 不该无限增长 | **1→1647 单调增长**（veh=27） | 停驶门永久早退，属实 |

⇒ 13.2 的猜测（等待标记被扫描分支误清）**不成立**。上一轮登记的疑点 D-1（27 是链头不会被 `:14331` 清）方向正确，只是我把它当成了"对不上的地方"，实际上它就是答案的一半。

### 14.2 【已作废】原「段头 vs 链头」推断 — 我判错了，如实留档

本节初版断言「`veh=0` 是链头、`veh=27` 是它的段头，判据落到段头上」，
并被当作"同一 tick 的两次互斥读数"。**该推断完全错误**，作废原因逐条列明：

| 初版依据 | 为什么站不住 |
|---|---|
| `:591` 与 `:723` 是"同一 tick 两次读数" | `:591` 是 `act=48 tgt=0`，`:723` 是 `act=54 tgt=27` —— **两台不同机车、两个不同目标**；且 `CPL-GATE` 行本身不带 `tick=`，无法证明同 tick |
| `n=33`（`:607`）与 `nord=33`（`:259`）相同 ⇒ 同一条链 | `nord` 是 `GetNumOrders()`（**订单数**，不是链长）；两侧**各有一张 33 条的订单表**，数值相同纯属巧合，不构成同链证据 |
| `veh=0` 与 `veh=27` 同链 | `:585` 的 `veh=0` 在 `35,12`，`:259` 的 `veh=27` 在 `1,15`；`:123` 的 `head=27 tail=47` ⇒ 27 那条链是 27…47，**不含 0** |

### 14.3 真实根因（订正版）：27 自己就是链头，`current_order` 从未被加载

**三条独立铁证，全部来自 `build\R3R_debug.log`**：

- `:123` `R3RDUMP idx=27 et=372 sub=09 dir=5 tile=1,15 … FE=1 … SEGF=1 DB=0 spd=0 | prev=-1 next=28 head=27 tail=47 ord=1 co=0`
  ⇒ `prev=-1`（**链头**）、`FE=1`（前端引擎）、`SEGF=1`（同时是段头）、链区间 `27…47`。
- `:156` `R3RDUMP-CHAIN head=27 ow=0 isai=0 n=21 stopped=1 inDepot=1 spd=0 prio=1 borrow=0 ordCount=33 ordbk=0 curReal=0 curImpl=0 coType=0 realType=17 tile=1,15 tileOw=0 isDepot=1 depotIdx=0`
  ⇒ `ordbk=0`（**不是借用排程**）、`curReal=0`、`realType=17`（第 0 条就是 `OT_WAIT_COUPLE`）、
  但 `coType=0`（`current_order` 是 `OT_NOTHING`）、`stopped=1`、`inDepot=1`、`spd=0`。
- `:259` `SKIP-STOPPED veh=27 order=0 real=17 spd=0 tile=1,15 parked=0 front=1 nord=33 idx=0 tt=0 blocked=1 wclr=0`
  ⇒ 每 tick 被停驶门挡回（`blocked` 1→100→277→1005→1167→1325→1485→**1647**，`:318/:345/:649/:1159/:1281/:1566/:2193`）。
- 旁证 `:257/:258/:260` `ORD-IDX-WATCH site=tick-in / loco-entry / tick-out veh=27 … curType=0 dest=4294967295`
  ⇒ 只有 tick-in / loco-entry / tick-out 三个采样点，**没有 `pre-processorders` / `post-processorders`**
  ⇒ `ProcessOrders()` 在这一整段里**从未执行**。

⇒ **机制（猜测 (a) 成立）**：`current_order` 逐车字段，只在处理订单时被写入。
`veh=27` 是 `Stopped && cur_speed==0` 的库内链头，被 `SKIP-STOPPED` 门每 tick 早退 ⇒
`current_order` 从读档起就停在 `OT_NOTHING`（`:38` 的 `ORD-IDX-WATCH site=load-rebuild` 也显示 `curType=0`）⇒
任何依赖 `current_order.IsType(OT_WAIT_COUPLE)` 的判据（`R3RIsCoupleTarget` 经 `R3RCanCoupleNow`）恒假 ⇒
`CPL-GATE reject=target-not-wait site=depot act=54 tgt=27 … tOrd=0` ⇒ `pool=0` ⇒ 54 永远挂不上。

**注意与"清理误清"无关**：`wclr=0`（16/16 行）、`CPL-WAITCLEAR` 0 条 ⇒ `R3RClearStaleWaitMarker` 从未参与（§14.1 仍成立）。

### 14.4 `blocked=1647` 的定性（订正）

初版说"27 是段头，被挡属良性"——**因为 27 是链头，这个结论反过来**：
链头被永久挡在停驶门里正是病征本身（订单永不加载）。但它不是独立 bug，只是 §14.3 的下游表现：
**病根在"库内 `Stopped` 的 `WAIT_COUPLE` 链头被停驶门整 tick 跳过"**，修好订单加载，`blocked` 自然不再单调增长。

`veh=51`（`real=6`=`OT_GOTO_WAYPOINT`、`nord=11`、`tile=62,117`）仍未定性（Q-355-2）：它可能是另一条**正常停驻**的车
（不需要 tick），也可能是同族受害者。

### 14.5 修法（待拍板，未落地）

按根因，候选三选一（前两条针对早退，第三条针对判据）：

- **A-1（最小，改门）**：`SKIP-STOPPED` 门对"订单索引指向 `OT_WAIT_COUPLE`/`OT_GOTO_COUPLE`"的链头**放行**，
  只跳过"开车/寻路"处理、不跳过订单加载。风险=放行让 `WAIT_COUPLE` 链有被推进的可能（第 180 轮 KI-274 教训）。
- **A-2（治根，改同步）**：在停驶门前补一次 `current_order ← GetOrder(cur_real_order_index)` 同步
  （仅当 `current_order` 与索引不符时），使 `current_order` 始终反映真实订单。风险=多一处写 `current_order` 的点，
  需确认不与 `ProcessOrders` 的既有赋值打架。
- **A-3（只在判据侧兜底，不推荐）**：`R3RIsCoupleTarget` 在 `tOrd==0` 时回退读 `GetOrder(cur_real_order_index)`。
  这会让"候选是否在等待"的判定依赖实时索引而非 `current_order`，语义分叉更多。

**待核**：`SKIP-STOPPED` 门的实际条件行号（含"库内挂接"豁免为何对 27 未生效——`parked=0`、`inDepot=1`）。

### 14.6 本轮刻意不做

- 不改行为（未动 `SKIP-STOPPED` 门 / `R3RIsCoupleTarget`），因为修法涉及"停驻链头是否该处理订单"的口径，
  与第 180 轮 KI-274（跳 `WAIT_COUPLE` = 命令列车出发）直接冲突，需玩家拍板后一次改到位。
- 不删 `blocked`/`wclr` 探针（下次复测要用它们确认"没有回归"）。

### 14.7 遗留疑点

- **Q-355-1**（已解决，保留记录）：`site=depot` 调用点实际传的是 `w->First()`，`tgt=27` 即链头，
  所以**不存在"两条路径取链头方式不一致"**——初版的 Q-355-1 是我误读日志造成的伪问题。
- **Q-355-2**：`veh=51` 那条链（`nord=11`、`real=6`、`tile=62,117`）是段头还是链头？是正常停驻还是同族受害？
  需一条 `R3RDUMP idx=51` 或 `CPL-PLAN` 读数。
- **Q-355-3（新）**：`veh=0`（`35,12`、`caronly=1`、`co=17`、`borrowed=1`、`followOwn=6`）为何 `current_order=17` 正常、
  而 `veh=27`（同样库内 `Stopped`、同样 `WAIT_COUPLE`）却停在 0？差别在 `follow=1`/`borrowed=1`
  ——**跟随/借用路径的车辆会绕过停驶门**？若成立，则 27 的修法可参照"借用链"的处理方式，A-1/A-2 的具体形态要据此调整。

### 14.8 代码锚点补全（Q-355-1 结案 + 停驶门 + 现成先例）

**Q-355-1 = 伪问题，结案**：`site=depot` 调用点 `src\train_cmd.cpp:15167` 写的是
`if (R3RCanCoupleNow(v, w->First(), "depot"))`，`why=scan` 调用点 `:15224` 写的是
`if (!R3RCanCoupleNow(v, w->First(), "scan")) continue;` —— **两条路径都传链头**，取链头方式一致。
`tgt=27` 就是 `w->First()`，即 27 确实是链头（与 §14.3 的 `:123` `prev=-1` 吻合）。
初版"两条枚举路径不一致"的结论作废。

**停驶门本体**：`src\train_cmd.cpp:22429`

```cpp
if (!r3r_pending_depot_couple && consist->vehstatus.Test(VehState::Stopped) && consist->cur_speed == 0) {
    ... 探针 ...
    return true;          // ← 整 tick 直接返回，ProcessOrders() 一次都不跑
}
```

- 27 的输入：`Stopped=1`、`spd=0`、`r3r_parked=0`（日志 `parked=0`）、`r3r_pending_depot_couple=0`
  （它不持 `GOTO_COUPLE`，持的是 `WAIT_COUPLE`）⇒ **四条件全部命中** ⇒ `return true`。
- 反向对照：`veh=0`（`co=17` 正常）之所以没被挡，是它 `tStopd=0`（`:591` `tgtStopd=0`）——**没被标 Stopped**，
  所以它照常跑 `ProcessOrders` 把 `current_order` 载成 17。⇒ Q-355-3 的答案：
  **不是"借用/跟随绕过门"，而是"Stopped 标志的有无"**（27 被标停，0 没有）。

**现成先例（修法可抄）**：`:22547-22565` 对**主动方**做过同一件事——
`r3r_pending_depot_couple` 为真时（车被 R3R 车库工具停住、`current_order` 停在 `OT_NOTHING`），
代码先 `StopSeparation()` 清 Stopped、再 `consist->current_order = *r3r_pend;` materialise 订单，
注释自陈目的是"否则 `R3RCanCoupleNow()` 会用 `active-not-goto`/`active-stopped` 拒掉唯一合法候选"。
**同一疾病在主动方已治、在被动方（27 这条）没治** ⇒ KI-532 本质是"先例覆盖不全"。

**修法（按风险从低到高，待拍板）**

- **B-1（推荐，只读判据，零写入）**：`R3RIsCoupleTarget()`（`:2698-2713`）的等待判据加一条回退——

  ```cpp
  if (t->cur_speed != 0) return false;
  if (t->current_order.IsType(OT_WAIT_COUPLE)) return true;
  /* current_order 可能因停驶门（:22429）从未加载；有效表里的当前条才是权威。 */
  const Order *o = (t->GetEffectiveOrders() != nullptr) ? t->GetOrder(t->cur_real_order_index) : nullptr;
  return o != nullptr && o->IsType(OT_WAIT_COUPLE);
  ```

  优点：不写任何字段、不改 tick 行为、不碰第 180 轮 KI-274 的"跳 `WAIT_COUPLE` = 命令出发"风险面。
  缺点：`current_order` 依旧长期为空，其它依赖它的逻辑（若有）仍看不到该状态。

- **B-2（治根，抄 `:22555` 先例）**：在 `:22429` 门之前对"链头 + 有效表当前条是 `WAIT_COUPLE`"者 materialise
  `current_order`/`dest_tile`（不 return 前移，仍 `return true`）。
  风险：一旦 `current_order` 非空，`CheckTrainStayInDepot`/`IsDrivingBackwards` 等会看到新状态，
  可能让 27 类车开始"自己找路"（KI-274 同族），**必须在复测里专门盯**。

- **B-3（改门，最激进）**：门对"有效表当前条是 `WAIT_COUPLE`/`GOTO_COUPLE`"者放行，让它真正跑 `ProcessOrders`。
  风险最大（等于让停驻等待链每 tick 走完整订单处理）。

**我的建议**：先落 B-1（最小、可回退、能立刻解掉 `pool=0`），把 B-2 作为后续"让 `current_order` 恒正确"的治根项另行排期；
B-3 不建议。

**待核**：27 为何被标 `Stopped`（玩家手动停？解挂后残留？读档带入？）——若是**残留**，则治根应在设置 Stopped 的路径上加条件
（`WAIT_COUPLE` 链头不该被标停），而不是在判据里兜底。日志里 `:259` 起就是 `spd=0`，没有"跑起来后被停"的痕迹，
倾向"读档带入的 Stopped"。**这一条需玩家确认该车是否曾手动停放。**

---

## 十五、第 356 轮：主诉对象纠正（veh27 分析整体作废）+ veh48 道岔预留取证计划

### 15.1 玩家纠正（原话要点，2026-10-29）

> "你为什么要关注 veh27 啊，我记得我当时主诉的是：关于 veh48，是这样的：车正常等待，直到能完全占有才启动，
> 所以出发时的预留是成功的；但这次成功的预留终止在一个不安全的位置，也就是道岔；走完这段后，它又在那个道岔
> 订了第二次预留；最后在第三个道岔拐错方向。我建议两个都先执行探针取证。veh27 它就应该停在车库里，
> 因为我压根就没有启动过它，你倒好，跑偏目标了，浪费我时间（和）token。"

**结论：§十四（含 14.2~14.8）关于 veh27 的全部根因推断作废。** 理由不是"证据读错"，而是**问题对象选错**：

- veh27 停在 `1,15` 车库、玩家从未启动它 ⇒ `Stopped=1`、`spd=0`、**`ProcessOrders()` 不跑**、
  `current_order` 停在 `OT_NOTHING`，全部是**设计内的正确行为**。
- §14.3 把"链头 `current_order=0`"当成缺陷、§14.5 提出 A-1/A-2/A-3 三种"放行/补写订单"的修法、
  §14.8 的 B-1/B-2/B-3 —— **全部是把正常现象当病治，一律废弃，不得落地**。
- KI-532 的 `CPL-GATE reject=target-not-wait site=depot act=54 tgt=27` 也就**不是误拒**：
  判据正确拒绝了"一台停在库里、从未启动的车底"。

### 15.2 仍然成立的残余疑点（不是 bug，是待解释）

`act=54` 为什么要以 `tgt=27` 作为候选？若 54 真正该挂的是另一条**正在等待**的车底，那问题在**主动方的目标解析**
（`CPL-PLAN side=tgt` / 候选枚举 / `dest_tile` 匹配），与 27 的订单状态无关。此项降级为"待取证"，不再作为 KI-532 主体。

### 15.3 主诉重述（veh48 道岔链，三个环节，逐条独立取证）

| 编号 | 玩家描述 | 需要的数据 | 现有探针能否覆盖 |
|---|---|---|---|
| R48-1 | 正常等待，直到"能完全占有"才启动 ⇒ 出发时预留**成功** | 该次 `TryPathReserve` 的返回值、预订格数、是否 `ForceReserveOnce` | 仅 `TRP ... ok=1 res=1` 一类汇总行；**无"整条路径 + 终点格"输出** |
| R48-2 | 这条成功预留**终止在不安全位置（道岔）** | 预留**终点格**的 `TrackBits`、是否有分叉、终点是否停在道岔内 | **无**（`RP cur=` 只逐格 dump 到 `tg`，不打终点格的轨道拓扑） |
| R48-3 | 走完这段后，**又在该道岔订了第二次预留** | 第二次预留的**起点格/起点 trackdir**、与上一次终点是否同格 | **无**（无"预留起点"独立探针） |
| R48-4 | 最后在**第三个道岔拐错方向** | 沿途每个道岔格的 `trackdir` 选择与实际走向、`ChooseTrainTrack` 的候选/选中 | 部分：`CPL-BEST/CPL-TRACE` 只覆盖挂车寻路；普通 PBS 无"每道岔选择"记录 |

### 15.4 本轮日志对 veh48 的实测（重要：数据不足，不可下结论）

`build\R3R_debug.log` 中 `veh=48` **全部**有效行（刷屏行已排除）只有：

```
:45/:48/:51   ORD-IDX-WATCH site=load-rebuild|settle-in|settle-out veh=48 real=-1->2 impl=-1->2
              tt=-1->2 n=-1->14 curType=16 dest=923 dtX=27 dtY=7 vtile=42,10
:267          DEPOTBLK veh=48 tile=42,10 track=01 eng=1 isdepot=0 stopped=0 parked=0 spd=0 destRaw=2
:271          DEPOT-ARR veh=48 spd=0 real=2(16) curType=16 tx=42 ty=10 destTx=27 destTy=7
:273          CPLSPD   veh=48 tick=12941 tile=42,10 spd=0
:275          CPL-S0-CHK veh=48 tile=42,10 real=2 order=16 fp=0 prog=2 wait=0
:291          COUPLE-WAIT-OFFDEST veh=48 tile=42,10 mf=42,10 real=2 destTile=923
:316/:317     CPL-HOLD tick=12952 veh=48 tile=42,10 real=2 num=14 spd=0
```

**并集检索**：在 `veh=48` 的全部行里，
`TRP veh=48` **0 条**、`RESV` **0 条**、`LINEND` **0 条**、`REVERSEDIR` **0 条**、
`CRT veh=48` **0 条**、含 `ok=1` 的 **0 条**。

**但这不等于"没出发"** —— 附加检索 `LOCO-MOVE`／`loco=48` 拿到真实轨迹（`build\R3R_debug.log`）：

```
:656  LOCO-MOVE veh=48 tick=13445 tile=42,10 j=192 spd=5  track=01 dir=5 db=0 stuck=0 dtile=1571
:1164 LOCO-MOVE veh=48 tick=13524 tile=42,10 j=217 spd=40 track=01 dir=5 db=1 stuck=0 dtile=1571
:1217 LOCO-MOVE veh=48 tick=13551 tile=41,10 j=215 spd=73 track=01 dir=5 db=1
:1267 LOCO-MOVE veh=48 tick=13589 tile=40,10 j=199 spd=38 track=01 dir=5 db=1
:1290 LOCO-MOVE veh=48 tick=13607 tile=40,10 j=197 spd=22 track=01 dir=5 db=1
:1586 LOCO-MOVE veh=48 tick=13687 tile=40,10 j=267 spd=27 track=01 dir=5 db=1
:1873 LOCO-MOVE veh=48 tick=13714 tile=39,10 j=302 spd=71 track=08 dir=6 db=1
:2169 LOCO-MOVE veh=48 tick=13731 tile=39,11 j=221 spd=85 track=04 dir=6 db=1
:2172 LOCO-MOVE veh=48 tick=13744 tile=38,11 j=206 spd=72 track=01 dir=5 db=1
:2213 LOCO-MOVE veh=48 tick=13772 tile=38,11 j=193 spd=5  track=01 dir=5 db=1
```

**读数结论（本次日志能确定的全部事实）**

| 项 | 读数 | 说明 |
|---|---|---|
| 起点 | `42,10`，`spd=5→40` | 从库里**倒车**（`db=1`）驶出，符合"等能完全占有才启动" |
| 路径 | `42,10 → 41,10 → 40,10 → 39,10 → 39,11 → 38,11` | 6 格；`39,10` 处换 track（`08→04`，即斜向），是**唯一可见的道岔/弯道** |
| 目标 | `dtile=1571` = `(35,12)`（`12*128+35`，地图宽 128）；订单 `dest=923` = `(27,7)` | 挂车目标在 (35,12)，与订单到站 (27,7) 不同 |
| 终止 | 最后一条 `tick=13772 tile=38,11 spd=5`，之后无 `LOCO-MOVE` | 车在 `(38,11)` 停下（距挂车目标还差 3 格） |

⇒ **本日志只覆盖到"车开到 (38,11) 停下"为止**，玩家描述的 R48-2/R48-3/R48-4（预留终点在道岔、二次预留、
第三个道岔拐错）**没有对应日志**，因为现有探针只在移动分支按"每跨一格"打 `LOCO-MOVE`，
**不记录预留路径、不记录预留终点格、不记录道岔处的 trackdir 选择**。
⇒ 仍须按 §15.5 挂探针重取，但**不能再断言"从未出发"**（该错误结论已在本轮即时撤回）。

### 15.5 探针设计（待实现；只读、零行为变更）

按 R48-1~R48-4 逐环节落点，全部集中在 `src\train_cmd.cpp`（不改 `.h`）：

- **P-48-1 `RESV-PATH-DUMP`**（预留成功后一次性）：
  在 PBS 预留成功返回处，逐格输出 `tile / trackdir / track / TrackBits / is_junction(bits 非单轨)`,
  末行 `RESV-PATH-END tile= track= bits= junction=0|1 distToDest=` ⇒ 直接回答 R48-2 "终点是否停在道岔"。
- **P-48-2 `RESV-BEGIN`**（每次预留尝试入口，含失败）：
  `veh / tile / trackdir / spd / cur_real / order_type / ForceReserveOnce / prevEnd(上一次预留终点格)`
  ⇒ 回答 R48-3 "第二次预留是不是就在上一次终点的道岔上"。
- **P-48-3 `JUNC-CHOOSE`**（每个"多轨格"通过时一行，节流按 (tile,trackdir) 边沿）：
  `veh / tile / bits / chosen_trackdir / alt_trackdirs / moving_dir / db`
  ⇒ 回答 R48-4 "第三个道岔为何拐错方向"。
- **P-48-4（可选）`RESV-HOLD-LEN`**：`ok=1` 时输出预订格数与"终点距目的地格数"，用于判断"是否提前终止"。

**共同约束**：全部走 `R3RDbgWrite` + `R3RDbgOn()` 门；新枚举标签插在 `R3REDGE_COUNT` 之前；
不改任何判据/位移/预留逻辑；`R3R_PROBES=0` 时零开销。

### 15.6 复测判据（给玩家跑图用）

1. 出现 `RESV-PATH-END ... junction=0|1`，且能看出成功预留的终点格；
2. `RESV-BEGIN` 的 `prevEnd` 与上一次 `RESV-PATH-END` 的 `tile` **同格** ⇒ 证实"在道岔上二次预留"；
3. `JUNC-CHOOSE` 在第三个道岔处能看出 `chosen_trackdir` 与玩家目视拐错的分支一致；
4. 负判据：正常直行线路不产生 `JUNC-CHOOSE`（无多轨格），日志体积不失控（每车每格一次，边沿节流）；
5. 负判据：预留行为逐字不变（`COUPLE-OK`/`CPL-*` 序列与现有版本一致）。

### 15.7 状态

- 已定：**veh27 侧全部作废**（§十四 保留但标注失效，供追溯）。
- 已实现（第 357 轮）：§15.5 的三条探针已落地并编译，见 §十六。
- 下一步：玩家用新 exe 复跑同一场景，按 §15.6 判据取数。

---

## 十六、第 357 轮：探针落地（只读、零行为变更）

**改动范围**：仅 `src\train_cmd.cpp`（未碰任何 `src\*.h`，因此护栏可判增量安全）。
全部探针走 `R3RDbgOn()` 门 + `R3RDbgEdge` 签名去重/`static std::map` 去重，`R3R_PROBES=0` 时零开销。

### 16.1 新增枚举（插在 `R3REDGE_COUNT` 之前）

```
R3REDGE_RESVPROBE,    ///< RESV-BEGIN / RESV-END 共用标签位
R3REDGE_JUNCCHOOSE,   ///< JUNC-CHOOSE（本轮实际未用该枚举，改走 CPL-RET 同款 static map）
```

保留 `R3REDGE_JUNCCHOOSE` 定义但 `JUNC-CHOOSE` 行本身用 `std::map<uint32_t,uint64_t>` 去重
（与既有 `CPL-RET` 完全同法，避免几百格时 `R3RDbgEdge` 的全局 map 膨胀）。

### 16.2 `LOCO-MOVE` 扩字段（原探针 `train_cmd.cpp` 移动分支）

追加：`look= endTile=x,y endTd= endPos= curPos= endBits=%04X`。

- 数据源：`consist->lookahead->reservation_end_tile / reservation_end_trackdir /
  reservation_end_position / current_position`（`TrainReservationLookAhead`，lookahead 为空时 `look=0`、`endTile=-1`）。
- `endBits` = `GetTileTrackStatus(endTile, TRANSPORT_RAIL, 0)` 原始位图。
- 用途：**每跨一格**给出"当前这条预留的终点格"，于是 R48-2 可逐格观察
  "终点格是不是停在道岔上"，R48-3 可观察"同一格的终点被新预留改写"。

### 16.3 `RESV-BEGIN`（`TryPathReserveWithResultFlags()` 入口，FI-208 早退门**之前**）

字段：`veh tick tile=x,y mf=x,y td spd real order one look prevEnd=x,y prevEndPos`。

- `prevEnd` = **进入本函数时** lookahead 的旧终点（即上一次预留留下的终点）。
- 放在 KI-208 早退门之前 ⇒ **被门拒掉的尝试也会留痕**，解决"日志没有 ≠ 没发生"。
- 签名去重 payload = `tile | trackdir<<24 | order_type<<32 | look<<40 | spd!=0<<41`。

### 16.4 `RESV-END`（同函数成功出口，`FillTrainReservationLookAhead()` **之后**）

字段：`veh tick from=x,y endTile=x,y endTd endPos curPos endBits=%04X junction real spd`。

- 必须在 `FillTrainReservationLookAhead()` 之后才有值（前半段只订轨、不建前瞻）。
- `junction` = `KillFirstBit(TrackdirBitsToTrackBits(endBits & 0xFFFF)) != TRACK_BIT_NONE`，即"该格至少两根轨"。
- lookahead 为空（非真实制动模型）时 `endTile=-1` 仍打一行，避免把"没有"误读成"没跑过"。

### 16.5 `JUNC-CHOOSE`（`ChooseTrainTrack()` 的唯一出口 lambda `r3r_chosen_ret` 内）

字段：`veh tick tile=x,y enterdir tracks=0xX res=0xX ret=0xX why order spd real db`。

- 与既有 `CPL-RET` 的差别：**不限定 `OT_GOTO_COUPLE`、不限定多轨格**（CPL-RET 是两条件的交集，
  跑偏第四环时可能根本不在交集内）。
- `res` 用 `IsPlainRailTile(tile) ? GetReservedTrackbits(tile) : TRACK_BIT_NONE` 守卫 ——
  `GetReservedTrackbits()` 内部对非平原轨有 `dbg_assert_tile(IsPlainRailTile())`，
  探针不得制造新崩溃面（第 110 轮附记 7b 教训）。
- 无上限增长风险可控：`static std::map<uint32_t,uint64_t>` 按车号，与 CPL-RET 同规模。

### 16.6 判读方法（给玩家）

1. 先看 `RESV-BEGIN veh=48` 是否出现。
   - **有** ⇒ veh48 的预留确实走 `TryPathReserveWithResultFlags`，直接读 `RESV-END.endTile/endBits/junction`；
   - **没有** ⇒ 它的预留走的是 couple 寻路（`yapf_rail.cpp` 的 `CPL-RESERVE`）/ 或其他低层 `TryReserveRailTrack`，
     下一轮再补那边（本轮先定性"走哪条路"，不重复猜）。
2. `RESV-END ... junction=1` ⇒ R48-2「成功预留终止在道岔」得到实证。
3. 第二条 `RESV-BEGIN` 的 `prevEnd` == 前一条 `RESV-END` 的 `endTile`（同格）⇒ R48-3「在该道岔二次预留」得到实证。
4. 第三个道岔处 `JUNC-CHOOSE` 的 `tracks`（可选项）、`res`（已被占）、`ret`（实际给的）三者对照玩家目视轨迹
   ⇒ R48-4「拐错方向」是"选项不够"还是"选项有却选错"。
5. 负判据：`LOCO-MOVE` 新增字段不改变原有字段；`COUPLE-OK` / `CPL-*` 序列与上一版逐字一致。

### 16.7 构建自证（已通过）

入口：`R3R_fullbuild.cmd inc detach`（复用既有入口，未新建任何 `.cmd`）；`build\R3R_incbuild.guard.log` = `GUARD: incremental is safe`。

**首两次失败（如实记录）**：

1. `train_cmd.cpp(19518): error C2446: 无法将 'int' 转换为 'TrackStatus'` + `C27737 "r3r_re_bits": 必须初始化 const 对象`
2. `train_cmd.cpp(23467): error C2440: 无法从 'TrackStatus' 转换为 'uint'` + `C27737 "r3r_la_bits"`

根因：`TrackStatus` 不是数值类型，而是 `struct { TrackdirBits trackdirs; TrackdirBits signals; }`（`src\track_type.h:134`），
`GetTileTrackStatus()` 返回它，既不能与 `0` 比较也不能直接强转。改为 `GetTileTrackdirBits(tile, TRANSPORT_RAIL, 0)`
（`src\tile_track_func.h:34`，返回 `TrackdirBits` 枚举，可 `(uint)` 直接转）后通过。**教训：JGRPP 里取"某格有哪些轨/轨向"
应当用 `GetTileTrackdirBits()`，`GetTileTrackStatus()` 只在确实需要 `.signals` 时才用。**

**最终结果**：

| 判据 | 实测 |
| --- | --- |
| `build\R3R_incbuild.done` | `EXIT_CODE=0`（2026-10-29 15:09:51） |
| 日志尾部 | `[3/3] Linking CXX executable openttd.exe` |
| 源 → obj → exe | `src\train_cmd.cpp` 15:07:28 → `train_cmd.cpp.obj` 15:07:54 → `build\openttd.exe` 15:09:41（52 344 832 B） |
| `read_lints` | 0 条 |
| exe 字面量核对 | `RESV-BEGIN veh=` / `RESV-END veh=` / `JUNC-CHOOSE veh=` / `endBits=%04X` 四项全 True |

只改了 `src\train_cmd.cpp`，未碰任何 `src\*.h` 与 `lang\*.txt`。

### 16.8 交付给玩家的复跑要求

用 `build\openttd.exe`（52 344 832 B，2026-10-29 15:09:41）复跑同一场景，
把 `build\R3R_debug.log` 交回；按 §16.6 的 5 步判读。
重点只看三样：①`RESV-BEGIN veh=48` 有没有；②`RESV-END ... endBits/junction`；
③第三个道岔的 `JUNC-CHOOSE tracks/res/ret`。

---

## 十三、第 355 轮：猜测根因 + 探针定案方案（玩家 2026-10-29 授权改探针）

### 13.1 为什么上一轮定不了根因（诚实交代，不是"探针太多"）

两个缺口的性质不同：

- **缺口 1（真缺读数）**：`SKIP-STOPPED` 只说"现在 `order=0` / `real=17`"，说不出**这两个读数的先后**
  （先停后空 / 先空后停）。而这决定了修法该加在"停驶门"还是"清理等待标记"。**一条对照读数即可定案。**
- **缺口 2（信噪比）**：`CPL-PAIRSCAN` 每 tick 一行（1392 行/550 KB），把 `CPL-WAITCLEAR`、
  `RESV-TERM` 这类低频真信号挤到看不见。这属于"探针自身坏了"，**必须修，否则下次还是看不见**。

⇒ 结论：不是"探针太多定不了"，而是"**探针的形态不对**"——一条真缺的对照 + 一条必须节流的刷屏。

### 13.2 猜测根因（待验证）：等待标记被"清理"后，停驶门让它永远回不来

代码锚点（当前源码）：

1. `src\train_cmd.cpp:2732` `R3RClearStaleWaitMarker(Train *w)`
   - 判据只有**一句**：`w == nullptr || !w->current_order.IsType(OT_WAIT_COUPLE) → return`；
   - 命中即 `w->current_order.Free()` + `SetDestTile(INVALID_TILE)`（`:2736-2737`）。
2. 两个调用点：
   - `:12998`（`Couple()` 合并成功后清掉其余段头的陈旧标记）——**语义正确**；
   - `:14331-14334`（`CPL-PAIRSCAN` 的候选池筛选分支 `t->First() != t || t->First() == coupler->First()`）
     ——**这个函数在"只读扫描"里被调用**，即扫描本身会改写车辆状态。
3. `TrainLocoHandler` 的停驶门 `:22408`：`Stopped && cur_speed == 0 && !库内挂接` ⇒ `return true`，
   **整 tick 不执行订单处理**，因此 `current_order == OT_NOTHING` 的链**没有任何机会重新加载**。
4. `parked=0` ⇒ 该链不是被 R3R 车库工具停的（`:22408` 的豁免 `r3r_pending_depot_couple` 要求 `r3r_parked`，
   `:22394`），所以豁免不生效。

⇒ **猜测链条**：等待车底的 `WAIT_COUPLE` 在扫描/合并路径里被 `R3RClearStaleWaitMarker` 清成 `OT_NOTHING`
→ 该车 `spd=0` 原地不动 → `SKIP-STOPPED` 永久挡回 → `current_order` 再也不会被加载
→ 之后所有 `CPL-GATE` 判 `tOrd=0`（target-not-wait）、所有扫描 `pool=0`。

### 13.3 对不上的地方（如实登记为疑点，先不定案）

- **疑点 D-1**：`:14331` 的分支条件是 `t->First() != t`（它不是链头）**或** `t->First() == coupler->First()`（与主动方同链）。
  但本轮 `CPL-GATE reject=target-not-wait tgt=27`，而 27 `front=1`（自己就是链头）。
  ⇒ 若 27 是链头且与主动方不同链，**这个分支不会清它**，猜测链在 27 身上不成立。
  需要靠新增的 `CPL-WAITCLEAR` 的 `tag` + 车号来判：**日志里到底有没有 27**。
- **疑点 D-2**：`Stopped` 在两个可疑位置之外还有别的写入者（`Vehicle::SetStopVehicle`、车库进出、
  玩家"停车"按钮），本轮 `SKIP-STOPPED` 没有打印 `Stopped` 的**来源与时刻**，无法排除"玩家/老存档带入"。

### 13.4 本轮探针改动（只读，零行为变更，仅 `src\train_cmd.cpp`，不碰 `*.h` 以免全量）

| # | 位置 | 改动 | 定案什么问题 |
|---|---|---|---|
| P1 | `R3RClearStaleWaitMarker()` 签名 | 加 `const char *tag` 参数；两处调用点传 `"couple-merge"` / `"scan"` | 哪个调用点在清、清的是谁 |
| P2 | 同上，`CPL-WAITCLEAR` 行 | 追加 `tag head idx real nord` 与 `first==head` 判据 | D-1：27 是否被清、被哪个点清 |
| P3 | 同上，成功清理时 | 记入静态表 `_r3r_waitclear_count[veh]` | 供 P4 交叉核对 |
| P4 | `SKIP-STOPPED` 行（`:22428`） | 追加 `blocked`（连续被本门挡回的 tick 数，静态表）与 `wclr`（`CPL-WAITCLEAR` 命中次数） | **先后顺序**：若 `wclr>0` ⇒ 先空后停（标记被清，主因在清理）；若 `wclr=0` 且 `blocked` 很大 ⇒ 先停后空（问题在停驶门本身） |
| P5 | `CPL-PAIRSCAN` 去重 payload（`:14229-14231`） | **把 `wait` 从 payload 里去掉**（正文仍打印） | 修掉缺口 2：`wait` 每 tick 变导致去重恒不命中 |

P4 是**本轮唯一的定案探针**：它把"两个读数谁先"变成一个可读的数字。

### 13.5 复测判据（下次日志一次判完）

1. `CPL-PAIRSCAN` 行数应从 1 392 量级降到**几十行**，且 `CPL-WAITCLEAR` / `RESV-TERM` 清晰可见。
2. `SKIP-STOPPED veh=27 … wclr=? blocked=?`：
   - `wclr>0` ⇒ 猜测根因成立（13.2），修法落在**收窄 `R3RClearStaleWaitMarker` 判据**（只在真正的合并提交点清，
     扫描分支改为只读、或要求 `t` 不在等待态才清）；
   - `wclr=0` 且 `blocked≥100` ⇒ 猜测证伪，修法落在**停驶门**（允许 `OT_NOTHING + 可执行 real order` 的链通过一次以加载订单）。
3. 负判据：正常停站/库内挂接的链 `wclr=0`、`blocked` 不应无限增长。
4. `CPL-WAITCLEAR` 若出现，其 `tag` 必须能解释为 `couple-merge`（合法）或 `scan`（需审查）。

### 13.6 本轮刻意不做

- 不改任何**行为**（不放宽停驶门、不收紧清理判据）——必须先看 P4 的读数再决定改哪一头，
  否则会把"状态被误清"伪装成"闸门太严"（与 §12.2 同一教训）。
- 不动 `src\*.h`，避免触发全量重编。

### 13.7 构建自证（2026-10-29）

- 入口：`R3R_fullbuild.cmd inc detach`（复用既有入口，未新建任何 `.cmd`）；构建前 `tasklist` 确认 `openttd.exe` 未运行。
- `build\R3R_incbuild.done` = `EXIT_CODE=0`；日志有 `[3/3] Linking CXX executable openttd.exe`。
- 时间戳链：`src\train_cmd.cpp` 2:42:07 → `train_cmd.cpp.obj` 2:43:09 → `build\openttd.exe` 2:44:30（52 344 832 B）。
- `read_lints` 0 条（`src\train_cmd.cpp`）。
- 产物字面量：exe 内命中 `CPL-WAITCLEAR … tag=%s isHead=%d idx=%d real=%d nord=%d nth=%u` 与
  `SKIP-STOPPED … blocked=%u wclr=%u`。**注意** `CPL-PAIRSCAN` 去 wait 只改 payload，不改格式串，故不能靠字面量核对。

### 13.8 状态

已实现 + 已编译，**待玩家跑一次现场日志**（AI 不自行启动游戏）。拿到日志后按 §13.5 四条判据一次定案：
`wclr>0` ⇒ 修清理判据；`wclr=0 && blocked≥100` ⇒ 修停驶门。

