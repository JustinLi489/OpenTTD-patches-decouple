# 第 456 轮：玩家新报五项问题（登记与取证）

- 建档时间：2026-11-18
- 输入：玩家一次性报告 5 个问题（原话见 §一）
- 当前生效 exe：`build\openttd.exe` @2026-11-18 00:47:39（53 030 400 B），
  `build\R3R_incbuild.done` = `EXIT_CODE=0`，即第 455 轮续二（调度池比较符扩展 + 上界修正）已编入内测树。
- 本轮状态：**仅登记，未宣布任何根因**。按既有纪律，每条问题的结论必须带
  `文件:行号` 或 `build\R3R_debug.log:行号` 锚点，不得凭阅读印象下判断。

---

## 一、玩家原话（逐字保留，勿改写）

> 向你报告以下问题：
> 1，不知为何，order_cmd.cpp 5911行断言崩溃了
> 2，不知如何，列车分配到的排程全部都有一个"（自动填充）"后缀并且命令颜色显灰
> 3，那个关于改装信息报错的问题依然存在。
> 4，我发现，不知为何，中国火车包的三节铰接式列车（疑似是因为存档再读档），重量，功率和最大牵引力都乘三了。此外，另一个还没有怀疑点的是这些列车装载能力都多了一名旅客
> 5，我再次发现了一个预留在道岔的不安全停车位截止的问题（详见build/R3R_debug.log）

---

## 二、问题登记表

| 编号 | 玩家描述 | 对应 KI | 初判严重度 | 状态 |
|---|---|---|---|---|
| Q-456-1 | `order_cmd.cpp:5911` 断言崩溃 | KI-654（新建） | 高（崩溃） | **已修 + 已编译**（§9.1），复测待做 |
| Q-456-2 | 排程全部带"（自动填充）"后缀且命令灰显 | KI-655（新建） | 中（行为/观感） | **已修 + 已编译**（§6.1），复测待做 |
| Q-456-3 | 改装信息报错仍存在 | KI-653（追加本续） | 中 | 未修，等玩家给报错原文 + GRF 名 + 触发操作 |
| Q-456-4 | 中国火车包三节铰接车读档后 weight/power/max TE ×3、容量 +1 | KI-656（新建） | 高（数据错误） | 部分定性（§9.3），策略待拍板 |
| Q-456-5 | 道岔预留的不安全停车位截止复现 | KI-657（新建） | 高（潜在死锁/穿模） | 部分定性（§8.2/§9.2），REG-456-E 待钉死 |

---

## 三、取证计划（按依赖顺序，非并行盲搜）

### Q-456-1 order_cmd.cpp:5911 断言
- [ ] 读 `src\order_cmd.cpp` 5900-5930 确认断言表达式与所在函数
- [ ] 反查该断言的语义前置条件（谁写、谁读）
- [ ] 找玩家崩溃日志（`crash-*.log`）或 `build\R3R_debug.log` 中对应时间点
- [ ] 判据：能给出「哪个调用路径把不满足不变式的数据带到这里」

### Q-456-2 "（自动填充）"后缀 + 命令灰显
- [ ] 全树搜 `自动填充` / `auto` 相关语言串（`src\lang\*.txt`）定位 STR 编号
- [ ] 定位该后缀的拼接点（订单文本渲染）与触发条件
- [ ] 灰显判定点（`IsOrderGrayed` / auto_ranges / `R3RSkipAutoOrderRange` 一族）
- [ ] 关联线索：第 264 轮续（KI-399/KI-400/KI-401）的 auto_ranges 与"自动填充命令永不执行"，
      以及第 264 轮续的 F 项（auto_ranges 迁移到段链头）——推断这批字段在读档/单段化后
      是否被误当成"全表都是自动填充"。
- [ ] 判据：后缀与灰显必须来自同一份状态，且能指出该状态被错误置位的提交点

### Q-456-3 改装信息报错（KI-653 续）
- [ ] 读 KI-653 现有条目全文（`R3R_KNOWN_ISSUES.md:24668` 起）
- [ ] 复述第 455 轮已排除项，避免重复取证
- [ ] 需要玩家提供：报错弹窗的**完整原文**（截图或日志行）+ 涉事 GRF 名 + 触发的具体操作
- [ ] 判据：报错文本能对应到 `CheckConsistencyOfArticulatedVehicle` 的某个分支

### Q-456-4 铰接车 weight/power/max TE ×3、容量 +1
- [ ] 先定性：是"×3"还是"多算了一个铰接单元的份"（三节车恰好 ×3 属强线索）
- [ ] 查 `ConsistChanged` / `R3RConsistReferenceEngine`（KI-640 第 444/449 轮引入的"代表引擎"口径）
      对 `cached_power` / `cached_max_te` / `cached_weight` 的累加次数
- [ ] 查存档往返路径：`src\sl\vehicle_sl.cpp` 的 `AfterLoadVehiclesPhase2` 是否二次累加
- [ ] 查容量 +1：`cached_cargo_capacity` 与铰接部件货种聚合（第 455 轮 §12.3 的 `pn`/`n` 判读表）
- [ ] 判据：给出"读档前/读档后"两次 dump 的数值差，并能指出累加点被调用两次的证据

### Q-456-5 道岔预留不安全停车位截止
- [ ] 读 `build\R3R_debug.log` 尾部，定位 `PLATSEL-BLOCK` / `DEPOTBLK` / `RP cur=` / `SPLICE` 一族锚点
- [ ] 对照第 240 轮 KI-346（双槽格订错轨）、第 243 轮续十二（审计触发时延）、第 242 轮（停驶不再预留）
- [ ] 判据：能指出"不安全停车位"是被谁订下的（预留 lambda 的哪一路），以及截止判据为何没拦住

---

## 四、疑点登记（REG-456-*）

（发现即登记，不等取证结束）

- **REG-456-A**：Q-456-4 的"×3"与"三节铰接"数值巧合，需区分三种可能：
  ①真的算了三遍；②算了一遍但每节权重翻倍；③显示侧重复计入。
  期望值 = `CalcNextVehicleOffset` 口径的单份属性；实测待取。
- **REG-456-B**：Q-456-4 的"容量 +1"与"×3"可能同源（同一累加循环），也可能独立
  （货种聚合的 `front=nullptr` 口径）。需分别取数，不得合并结论。

---

## 四·二 取证进展（每条自带锚点，随取随写）

### P-456-1（Q-456-1 断言）已定位断言原文

- 锚点 `src\order_cmd.cpp:5911`：
  ```cpp
  assert(v->cur_implicit_order_index == 0 || v->cur_implicit_order_index < v->GetNumOrders());
  ```
- 宿主是 `ProcessOrders()`（订单推进主循环，紧随其后 `:5912 v->UpdateRealOrderIndex()`）。
- 现场取证探针已存在（第 297 轮 KI-472，`:5891-5908`）：条件 `GetNumOrders() > 0 &&
  cur_implicit_order_index >= GetNumOrders()` 时调 `R3RWatchOrderIndexSite(Train::From(v),
  "process-orders-bad", "assert-4909")`（注意标签仍写 `assert-4909`，是历史标签未随行号更新）。
  ⇒ **判定方法**：只要日志里有 `process-orders-bad` / `assert-4909` 行，就拿到越界值与当时订单数。
- 历史同类现场（注释 `:5901`）：`crash-20261025T032645Z.log` —— 崩溃车 veh 99 是**克隆品**、
  停在车库格（`trk 0x80`）。⇒ 克隆品 + 车库 是强线索，本轮优先复核克隆路径是否写越界索引。
- 仍缺：本轮崩溃的 `crash-*.log` 或对应时段的 `build\R3R_debug.log`（需玩家提供）。

**P-456-1b 现场检查结果（2026-11-18）：**
- 工作区根目录 **无 `crash-*.log` 文件**（`Get-ChildItem -Filter 'crash-*.log'` 空结果）。
- `build\R3R_debug.log` 存在（2026-11-18 20:42:30，368 746 B），
  搜 `process-orders-bad|assert-4909` **0 命中**。
- **探针盲区（REG-456-C，重要）**：`:5905` 的条件是
  `if (r3r_oob_n > 0 && (int)v->cur_implicit_order_index >= r3r_oob_n)`。
  当 `GetNumOrders() == 0` 而 `cur_implicit_order_index != 0` 时，断言 `:5911` **仍然会崩**
  （`idx == 0 || idx < 0` 为假），但探针因 `r3r_oob_n > 0` 为假**一行都不打**
  ⇒ 「日志里没有 process-orders-bad」**不能**排除这条路径。需把条件改成
  `if ((int)v->cur_implicit_order_index >= r3r_oob_n)`（num=0 时 `>=0` 恒真，自然覆盖）。

### P-456-2（Q-456-2 自动填充后缀）判据链已完全打通

- 语言串：`STR_ORDER_R3R_AUTO_FILLED` = `english.txt:5084` `{SPACE}(auto-filled)` /
  `simplified_chinese.txt:5050` `{SPACE}（自动填充）`。
- 唯一使用点：`src\order_gui.cpp:1697`
  `if (air_range && !order->IsType(OT_COUPLE_END)) AppendStringInPlace(line, STR_ORDER_R3R_AUTO_FILLED);`
- 灰显与后缀**同源**：`src\order_gui.cpp:1086` `const bool air_range = !timetable && R3RIsCoupleAirOrderRow(v, list, order_index);`
  → `:1089-1090` `if (order->IsType(OT_IMPLICIT) || air_range) colour = Grey;`
  ⇒ 玩家报的「后缀 + 灰显」是同一个 `air_range` 的两种表现，不是两个 bug。
- `R3RIsCoupleAirOrderRow()`（`order_gui.cpp:1022-1029`）取 `v->GetEffectiveOrders()`（借用期即控制段表），
  调 `R3RIsCoupleAirOrder(ol, index)`。
- 真判据 `src\order_base.h:1973-1990`：
  ```cpp
  if (self->IsType(OT_COUPLE_END)) return true;
  for (uint step = 1; step < num; step++) {
      const Order *const o = list->GetOrderAt((index + num - step) % num);
      if (o == nullptr) return false;
      if (o->IsType(OT_GOTO_COUPLE)) return true;
      if (o->IsType(OT_COUPLE_END)) return false;
  }
  ```
  语义 = 「从本行向前回绕，先碰到 `OT_GOTO_COUPLE` 就在区间内，先碰到 `OT_COUPLE_END` 就在区间外」。
- **G-456-1 强化（升为强候选，待实测）**：若表里有 `OT_GOTO_COUPLE` 而**缺少配对的
  `OT_COUPLE_END`**，则 GOTO_COUPLE 之后的所有行 + 环回它之前的行全部判真
  ⇒ 表现正是「几乎整张表灰显 + 带（自动填充）」，只有 `OT_GOTO_COUPLE` 那一行例外。
  这与玩家描述逐字吻合。反向情形（只有 COUPLE_END 没有 GOTO_COUPLE）只会把那一行标灰，不会全表。
- 待查：`OT_COUPLE_END` 在哪些路径被**自动插入**（插 GOTO_COUPLE 时是否补结束标记）、
  以及读档/排程池复制/克隆是否丢掉结束标记。

### P-456-2b 结束标记的补齐函数只有 3 个调用点（缺口已定位到枚举面）

- 补齐函数 `R3REnsureCoupleEndOrders(Vehicle *v)`：`src\order_cmd.cpp:1959-2010+`，
  入口 `OrderList *const list = v->GetEffectiveOrders();`，语义 = 「每个 GOTO_COUPLE 若在环上
  第一次遇到边界之前没有 COUPLE_END，就在 i+1 处插一个」。
  **注意它的入口是 `Vehicle *v`**（靠 `v->GetEffectiveOrders()` 取表），对**没有车辆持有的
  ownerless 排程（排程池条目）根本调不到**。
- 全树调用点仅 3 处：
  1. `src\sl\vehicle_sl.cpp:561`（读档，`part_of_load` 分支，按车辆迭代）
  2. `src\order_cmd.cpp:2192`（订单删除/修改命令末尾）
  3. `src\order_cmd.cpp:2372`（订单移动命令末尾）
- ⇒ **结构性缺口**：排程池里的 ownerless 排程在被"分配"给列车时、以及排程复制/克隆路径，
  都不经过这三处，因此可能带着「有 GOTO_COUPLE、无 COUPLE_END」的表进入车辆
  ⇒ 按 `R3RIsCoupleAirOrder()` 的回绕逻辑，除 GOTO_COUPLE 那一行外**整表**被判 air range
  ⇒ 灰显 + （自动填充）。这与玩家「分配到的排程全部带后缀」的措辞（"分配"= 从池里指定）高度一致。
- **下一步待查（A-456-8）**：排程池"分配/复制"给列车的命令入口
  （`CmdR3RSet...Schedule` / `AssignOrderList` 一族）是否调用 `R3REnsureCoupleEndOrders`；
  以及自动填充（AUTO-ORD）是否走 `InsertOrder(v,...)`（`order_cmd.cpp:1818` 注释声称
  "insert 时总会带结束标记"）。

### P-456-2c **根因已定位（强证据，非推测）：ownerless 排程插入不补 `OT_COUPLE_END`**

- 车辆侧走 `InsertOrder(Vehicle *v, Order &&new_o, VehicleOrderID sel_ord)`
  （`src\order_cmd.cpp:1817`），第一句就是：
  ```cpp
  const bool add_couple_end = new_o.IsType(OT_GOTO_COUPLE);
  ```
  并在 `:1919-1921` 插入结束标记 ⇒ 车辆订单窗口插入挂车命令**永远**成对。
- **排程池侧走的是另一条路**：`CmdR3RInsertLooseOrder()`（`src\order_cmd.cpp:4430-4449`）：
  ```cpp
  Order new_order{};
  MemberPtrsTie(new_order, Order::GetCmdRefFields()) = data.new_order;
  if (flags.Test(DoCommandFlag::Execute)) {
      list->InsertOrderAt(std::move(new_order), insert_at);   // <== 无任何 COUPLE_END 补齐
      InvalidateWindowClassesData(WindowClass::R3RSchedule);
      ...
  }
  ```
  函数头注释（`:4417-4424`）明确说「Deliberately *does not* run PreInsertOrderCheck()」，
  但**没有**提到结束标记；实际也没补。
- \(:4556-4558\) 同族：另一处 `list->InsertOrderAt(std::move(new_order), sel_ord + 1)`，
  同样位于 ownerless 排程编辑路径（同样带 `InvalidateWindowClassesData(WindowClass::R3RSchedule)`）。
- **因果链（与玩家描述逐字吻合）**：
  1. 玩家在排程池编辑器里给排程插入「挂车（GOTO_COUPLE）」；
  2. 表里只有 `OT_GOTO_COUPLE`，**没有** `OT_COUPLE_END`；
  3. `R3RIsCoupleAirOrder()`（`order_base.h:1973`）从任何一行向前回绕，只要先碰到
     `OT_GOTO_COUPLE` 就判真 ⇒ 除 GOTO_COUPLE 自己那一行外，**整表**判为 air range；
  4. `order_gui.cpp:1086` 灰显 + `:1697` 追加 `STR_ORDER_R3R_AUTO_FILLED`；
  5. 该排程"分配"给列车后，车辆订单窗口显示同一张表 ⇒ 玩家看到「全部带（自动填充）且全灰」。
- **修复方向（待落地）**：
  - 把 `R3REnsureCoupleEndOrders(Vehicle*)` 的列表级逻辑抽成
    `R3REnsureCoupleEndOrdersInList(OrderList *list)`，车辆版改为薄包装；
  - `CmdR3RInsertLooseOrder()` 与 `:4558` 那条在 `InsertOrderAt` 之后调用列表级版本；
  - 兜底：ownerless 排程被**分配**给列车时（`CmdR3RSet...`/`AssignOrderList`）也跑一次。
- **待复核**：`order_cmd.cpp:2519-2521`、`:4012` 两处 `InsertOrderAt` 落在哪条路径
  （若也属 loose/复制路径需一并补）。

### P-456-4（Q-456-4 铰接车 ×3 / 容量 +1）取证进展

- 字段定义：`src\train.h:288-294`
  ```cpp
  uint16_t weight_override = UINT16_MAX;
  uint16_t power_override = UINT16_MAX;
  uint16_t max_speed_override = UINT16_MAX;
  ```
- 读取点（override 优先，取代原型）：
  `train.h:496-515 GetWeightWithoutCargo()`、`train.h:569+ GetPower()`、
  `train_cmd.cpp:25954-25961`（货重）——三处都是 `if (override != UINT16_MAX) 用 override，
  else if (!IsArticulatedPart()) 用 RailVehInfo`。逻辑本身守恒、无重复计算。
- **已确认 override 有存档**：`src\sl\vehicle_sl.cpp:209-211`
  `SLE_CONDVAR(Train, weight_override/power_override/max_speed_override, SLE_UINT16,
  SLV_R3R_ARTIC_OVERRIDE, SL_MAX_VERSION)`，且 `saveload_common.h:438` 的
  `SLV_R3R_ARTIC_OVERRIDE` 在 `SL_MAX_VERSION`(:441) **之前** ⇒ 新档会写出。
- 烘焙点：`src\vehicle_cmd.cpp:528-593 R3RDearticulateOneGroup()`
  `w_q = total_weight / n_total; w_r = total_weight % n_total;`
  组头 `weight_override = w_q + w_r`、成员 `= w_q`（功率同构）；`max_speed_override = 全组同速`。
  ⇒ 打散**一次**时统计严格守恒。
- **缺口 1（强候选，G-456-3）**：该函数**完全没有处理 `cargo_capacity`**
  （全函数只碰 weight/power/max_speed 三个 override，`:549-566`）。
  真 artic 组里部件容量由父车统一报，打散后每节按自己的 `cargo_capacity` 独立计入
  ⇒ 总容量 = 父车那份 + N 个部件各自那份 ⇒ **只增不减**，与玩家「多了一名旅客」方向一致。
- **缺口 2（强候选，G-456-4）**：若 override 在读档后变成 `UINT16_MAX`（未写/未读/被清），
  则每节回落到 `RailVehInfo(engine_type)->weight` 与 `GetPower()` 原型值。
  真 artic 车的 GRF 常把整组重量/功率填在**每一节**的原型记录上（因为部件本来不贡献），
  于是「每节都报全量」= **恰好 ×3**，与玩家「三节车 ×3」逐字吻合。
  需验证 override 在读档后是否真的非 `UINT16_MAX`。
- **现成探针（无需改码即可取数）**：
  - `DEARTIC-SPLIT head= n= wtot= wq= wr= ptot= pq= wagon=`（`vehicle_cmd.cpp:575`）
  - `DEARTIC-PART head= i= idx= art= gm= gh= eng= ov= rw= w= cargo=`（`:580`）
    ⇒ `ov` = 烘焙值、`rw` = 原型 weight，两者相等即「烘焙没生效/回落到原型」。
  - `CARGOWEIGHT-VEH head= idx= eng= art= gm= gh= ov= ew= pc= cw= acc=`（`ground_vehicle.cpp:151`）
    ⇒ `pc` = 该组被摊的部件数。
- **日志核查结果（2026-11-18 场次）**：
  - `DEARTIC-|CARGOWEIGHT-VEH` **0 命中** ⇒ 本会话**没有发生过** R3R 打散烘焙，
    也没有走 `CARGOWEIGHT-VEH` 那条链。
  - `CHAIN-ATTRS head=120 n=39 FE=2 SEG=2 pow=3741 wt=3600 len=8 spd=100`（`:3069`）
    / `CHAIN-ATTRS2 couple head=120 len=151 pow=3742 wt=22220 spd=100`（`:3125`）
    ⇒ 有链级属性汇总探针，但缺「单车原型值」对照，**无法据此判定 ×3**。
  - ⇒ 结论：**本场日志不能证伪也不能证实 Q-456-4**，该问题必须靠 `dump_vehicle`
    或车窗读数（车号 + 三节各自的 `wt/pw`）来定性。
- **判据（给玩家，二选一即可）**：
  1. 控制台 `dump_vehicle <链头车号>`，看 `tf:` 是否带 `H`（ArticGroupHead）/`M`（ArticGroupMember）：
     带 ⇒ 是 R3R 打散的假铰接组，重点查 override 是否读档丢失；
     不带 ⇒ 是真 GRF 铰接组，重点查 `ConsistChanged` 的部件跳过逻辑。
  2. 列车详情窗口「重量/功率/牵引力」三格数值 + 该车 `engine_type` 名字。

### P-456-5（Q-456-5 道岔不安全停车位截止）日志取证：**本轮日志未复现该签名**

日志 `build\R3R_debug.log`（2026-11-18 20:42:30，368 746 B）。第 354 轮 V-2 已预置专用探针：

- `RESV-UNSAFE-TAIL veh= real= type= tile= td= tcount= safe=0 vtile= spd= ext=0`
  （`src\train_cmd.cpp:19018`）——「延伸找安全位失败、落到 try-any fallback」的唯一签名。
  **统计结果：0 条。**
- `RESV-TERM ... tcount= safe= tail= spd= vtile=`（`train_cmd.cpp:19044`）—— 预留实际终点。
  抽样 12 条**全部 `safe=1`、`tcount=-2`**（`tcount=-2` 表示终点不是 plain rail：站台/路点/道口/桥隧）。
  **`tcount=[3-9]`（多轨道=道岔格）统计结果：0 条。**
- 结论：**本轮日志里没有任何一次预留把终点落在道岔格上**，也没有触发 unsafe fallback。
  玩家报的"道岔不安全停车位截止"若确实发生在本场，必然走的是另一条路径或另一个判据
  （`IsSafeWaitingPosition` 对某格**误判为安全**，此时 `safe=1`，探针不会报警）。
- **待确认（需玩家）**：具体是哪一格道岔、哪列车（车号）、当时列车是停着还是通过、
  以及"截止"的表现（道岔被黑预留占住？列车停在道岔上？下一段预留从此格重新发起？）。
- 旁证：`CPLRES ... unsafe=0` / `RPFAIL ... unsafe=0` 全为 0，与上述一致。

### P-456-5b 读数误读的自我纠正（保留记录以防复发）

- 初次读日志时曾把 `RESV-TERM veh=6 ... tcount=-2 safe=1 tail=0 spd=77 vtile=218,137`
  误读为 `tcount=77`。真因是 PowerShell `Format-List` 对长行做了折行，
  字段 `spd=77` 与 `tcount=-2` 被拆到不同显示行。
  ⇒ **REG-456-D 撤回**：不存在 `tcount=77` 的异常格。
- **纪律**：读日志一律用 `Select-String` 取**整行原文**（`ForEach-Object { $_.Line }` 或直接 `Get-Content`），
  绝不以 `Format-List`/`Format-Table` 的折行结果判读字段。

## 五、猜测根因（待验证，形成即落盘）

- **G-456-1（Q-456-2，待验证）**：`auto_ranges` 在解挂单段化时被清空 / 或被读档迁移写成
  覆盖整表，导致"整表都是自动填充"。支撑：第 264 轮续 §13.1 已实测记录
  `SEGTR-SNAP tag=decouple-v ... auto n=0 first=[0,0)`；与此相反的"全表自动"尚未取数。
  验证判据：在日志/车窗取 `auto n=` 与 `first=[a,b)`，若 `n == 表长度` 即成立。
- **G-456-2（Q-456-4，待验证）**：`ConsistChanged()` 在读档后对同一链被调用两次，
  而属性累加未先清零（`cached_power/weight/max_te` 直接 `+=`）。
  验证判据：读档后单次调用前后打印同一车 `gcache.*`，若翻倍即成立。

---

## 六、落地（2026-11-18，仅 src\order_cmd.cpp，未碰任何 src\*.h ⇒ 增量合法）

### 6.1 P-456-2 已落地（KI-655）

改动四处：

1. `:1949-2029` 新增 `static void R3REnsureCoupleEndOrdersInList(OrderList *list, Vehicle *v)`：
   把原 `R3REnsureCoupleEndOrders(Vehicle*)` 的列表级逻辑整体搬过来，全部读改走
   `list->GetNumOrders()/GetOrderAt()/Orders()`；`v` 只用于窗口失效
   （`v != nullptr` ⇒ 车辆窗口类 + 出发板；`nullptr` ⇒ `WindowClass::R3RSchedule` +
   `R3RScheduleEditor`）。插入结束标记后的条件订单跳转索引修正改用插入后的
   `new_num`（原来是 `v->GetNumOrders()`，等价）。
2. `:2031-2039` 车辆版变成薄包装：`if (v == nullptr) return;` +
   `R3REnsureCoupleEndOrdersInList(v->GetEffectiveOrders(), v);`
3. `:4454-4475` `CmdR3RInsertLooseOrder()` 在 `InsertOrderAt` 之后调用列表级版本
   （注释点明 InsertOrder 的自动补齐在 ownerless 路径上不存在）。
4. `:4580-4591` `CmdR3RDuplicateLooseOrder()` 同样补调用（复制一条挂车命令也会
   造出未关闭块）。
5. `:4709-4722` 兜底：`CmdR3RAssignSchedule()` 的 Execute 分支在 `v->orders = list;`
   **之前**调用一次，保证旧 exe 编出来的坏表在被 adopt 时修好，且修好后才
   `list->Initialize(v)`（否则新插的标记不会被登记为目的地）。

刻意**没有**动的两处：`CmdR3RDeleteLooseOrder()` / `CmdR3RMoveLooseOrder()`
——它们与车辆侧 `CmdDeleteOrder()/CmdMoveOrder()` 一致都不跑补齐（玩家手删标记
属显式意图，自动重插会反复"变回来"）。若复测显示删/移也能造出全表灰，再按同法收口。

### 6.2 P-456-1 探针盲区已补（KI-654 / REG-456-C）

`order_cmd.cpp:5934-5945`：条件由
`if (r3r_oob_n > 0 && (int)v->cur_implicit_order_index >= r3r_oob_n)`
改为 `if ((int)v->cur_implicit_order_index >= r3r_oob_n)`。
即"表已空但索引仍指向旧顺序"这一形态现在也会打 `process-orders-bad/assert-4909` 现场
（旧条件恰好把它排除掉，这正是崩溃时日志 0 命中的原因）。

### 6.3 P-456-4 / P-456-5 仍未落地（等数据，不改码）

- P-456-4：`R3RDearticulateOneGroup()` 只烘焙 weight/power/max_speed（`vehicle_cmd.cpp:528-593`），
  确实不碰容量；但容量在 OpenTTD 里是**逐车 `cargo_capacity` 沿链求和**，打散并不改变
  链上成员，故"缺口 1"目前只是候选、**不能据此改码**（改错方向会重复计容）。
  需 `dump_vehicle <链头>` 看 `tf:` 是否带 `H`/`M` + 三格读数先定性。
- P-456-5：本轮日志 `RESV-UNSAFE-TAIL` 0 条、`RESV-TERM tcount=[3-9]` 0 条，
  签名未复现；`safe=1` 的误判路径探针看不出来。需玩家给具体格/车号/表现。
  **（2026-11-18 夜补：玩家指定 veh=120，新日志尾部已取到"同格再订一次"签名，
  见 §8.2；结论指向"预留 origin 远离车体"而非"停在道岔格"。）**

## 七、复测与验收

见 `R3R_KNOWN_ISSUES.md` 的 KI-654 / KI-655 / KI-656 / KI-657 复测判据；构建自证见 §9.4。

---

## 八、断言闭环 + veh120 取证（2026-11-18 夜，结论段）

### 8.1 KI-653 / P-456-1：crash log 已闭环"是什么"，代码层已闭环"哪里写的"

崩溃日志 = `C:\Users\冯洁敏\Documents\OpenTTD\crash-20261117T170248Z.log`
（398 行，mtime 2026-11-18 1:02:51；exe `r3r-stable-2026-11-13-m (2)`，Build date `Nov 18 2026 00:25:27`）。

- 断言原文（`:5911`，即该二进制的行号；当前源码在 `:5952`，因为本轮在断言前插了
  `:5928-5949` 的探针块）：
  `v->cur_implicit_order_index == 0 || v->cur_implicit_order_index < v->GetNumOrders()`。
- 栈：`ProcessOrders (order_cmd.cpp:5911)` ← `TrainLocoHandler (train_cmd.cpp:24144)`
  ← `Train::Tick (train_cmd.cpp:24907)` ← `CallVehicleTicks (vehicle.cpp:1723)`。
- 现场：`0: CallVehicleTicks: veh: 3: (Train 2, c:0, st:FE, vss:Ds, vf:FTpa, vcf:zd, gvf:s,
  tf:qLKHSO, trk: 0x01, [tile: 1CCB5 (181 x 230), type: 50 (Station), height: 01 ...])`
  - `tf:` 里的 `H`/`S`/`O` = `ArticGroupHead` / `SegmentFront` / `ForceReserveOnce`
    ⇒ 该车是**去铰接组的段头**，且正带一次性预留旗标；
  - `vss:Ds` 停驻、`trk: 0x01` = 站台格上的 plain rail `TRACK_BIT_X`。
- 判读：`== 0` 那一半会直接放行，所以命中时的实际形态必然是
  **`cur_implicit_order_index != 0` 且 `>= GetNumOrders()`**（不是"空表 + 索引 0"）。
- 该场次的 `build\R3R_debug.log` 已被后续场次覆盖（现文件 2026-11-18 21:01:48），
  ⇒ `n` 与索引的具体数值**不可回取**；但写入点已在代码层锁定（见下）。

**根因（已做实，代码级）**

1. `src\train_cmd.cpp:7812-7818`（`R3RReturnBorrowedOrders()` 的 `orders_backup != nullptr`
   分支）是整条链路里**唯一一处直接赋值、且完全不校验上界**的隐式索引写入：
   ```cpp
   chain->orders = chain->orders_backup;
   chain->cur_real_order_index = chain->orders_backup_real_index;
   chain->cur_implicit_order_index = chain->orders_backup_implicit_index;  // 无界
   chain->cur_timetable_order_index = chain->cur_real_order_index;
   ```
2. 同一函数上方 `:7788-7802` 的 `own` 分支**有**归一（`:7792-7793` 对 `num == 0` /
   `INVALID_VEH_ORDER_ID` / `>= num` 一律拉 0）⇒ 两个分支语义不对称，`orders_backup`
   这一支漏了同一套校验。
3. `DeleteUnreachedImplicitOrders()`（`src\vehicle.cpp:3466` 起）**救不了**：
   入口 `const Order *order = this->GetOrder(this->cur_implicit_order_index);`
   在越界时返回 `nullptr` ⇒ `while (order != nullptr)` 一次都不进 ⇒ 陈旧值原样留存，
   一直到 `ProcessOrders` 撞断言。（对比：`GVF_SUPPRESS_IMPLICIT_ORDERS` 分支是直接把
   `cur_implicit_order_index` 拉回 `cur_real_order_index`，那条路是安全的。）
4. 语义上本来就该校验：停放的 `orders_backup_*` 记的是**停放那一刻那张表**的坐标，
   停放期间表会被删单/自动填充/池交接改短，而 `R3VP` 连索引一起存档
   ⇒ 跨会话归还同样可能越界。
5. 同场新日志的旁证：`ORD-IDX-WATCH site=pre-processorders veh=120 real=2->1 impl=2->1
   tt=2->1 n=5->12 ord=00000289E13ED3E0->00000289E13EE1B0`
   ⇒ **一次 ProcessOrders 调用之内真的会换表**（借/还机制就在这个窗口里跑），
   所以"表长变了、索引没跟着归一"不是理论可能。

**修法（本轮落地，仅 `src\train_cmd.cpp`）**：在 `:7813` 取回 `orders_backup` 之后、
赋值之前，按**恢复出来的那张表**的 `GetNumOrders()` 归一 real / impl（timetable 跟随 real），
与 `:7792-7793` 完全同构；真正发生裁剪时打 `ORD-RETURN-BK-CLAMP`（幂等、无裁剪不打印）。

### 8.2 P-456-5 / veh=120：新日志里出现"同格再订一次"签名（玩家指定车号）

日志 `build\R3R_debug.log`（2026-11-18 21:01:48，406 656 B），尾部即 veh=120 的停驻循环：

```
:3141 RESV-TERM  veh=120 real=1 type=1 tile=218,138 td=1 tcount=-2 safe=1 tail=0 spd=0 vtile=211,116
:3142 RESV-BEGIN veh=120 tick=43970 tile=211,116 mf=211,116 td=8 spd=0 real=1 order=1 one=1 look=1 prevEnd=218,138 prevEndPos=368
```

- **`RESV-BEGIN.prevEnd` 与上一条 `RESV-TERM.tile` 同格（218,138）** ⇒ 正是代码注释里
  那条 R48-3 判据（"下一次 BEGIN 的 prevEnd == 上一次 TERM 的终点 ⇒ 在该格又订了一次"）。
- 同一次调用里的配套读数：
  - `CRT veh=120 order=1 dir=5 origin=218,122 td=5 found=1`
  - `CPLRES veh=120 tick=43970 origin=218,122 destTile=218,138 destTd=1 node=218,138
    root=0 unsafe=0 nTry=7 nOK=7 empty=0`
  - `ANSWER-STORE veh=120 n=7 from=218,126 to=218,124 dest=221,129 nth=1`
  - `TPR-EXIT veh=120 state=ok pair=0 tile=211,116 spd=0 ... origin=218,138 okay=1`
  - `TRP veh=120 mf=211,116 real=1 type=1 dest=66269 spd=0 fp=0 look=1 one=0 mstuck=1
    fto=0 => ok=1 res=1`
  - `R3R-RES-ONCE used head=120 force_proceed=2 tile=211,116` / `R3R-RES-ONCE clear head=120`
- 判读：订出去的是 `218,122 → 218,138` 这条竖线共 7 格（`nTry=7 nOK=7`），而**车体在
  `211,116`、订单目的地是 `221,129`**；`unsafe=0` 说明 `IsSafeWaitingPosition` 全程判"安全"，
  所以 `RESV-UNSAFE-TAIL` 永远不会出现在这个形态里（与 §3 的"误判为安全"分支吻合）。
  结果就是玩家看到的：**预留画在一段车走不到/停不住的位置并"截止"在那里，车原地不动**
  （`ok=1 res=1` 但 `spd=0`，且 `mstuck=1`）。
- `tcount=-2` 只表示 `218,138` **不是 plain rail**（站台/路点/道口/桥隧），不是"轨道数"
  ⇒ 与 §P-456-5b 的误读纠正一致；道岔格（`tcount >= 3`）本轮仍 0 条。
- **疑点 REG-456-E（新，待验证）**：`CRT/CPLRES` 的 `origin=218,122` 既不是车体 tile
  （`211,116`）也不是订单目的地（`221,129`），却与 `RESV-TERM` 终点同 x（218）
  ⇒ 需确认 `CPLRES` 的 origin 语义是否为"从当前预留的远端续订"。若是，则"离车 22 格的预留"
  本身就是该现象的成因，而不是"留了不安全的格"。
  验证判据：把 `CPLRES` 的 origin 来源在代码里钉死，并加一行 `RESV-ORIGIN` 打印
  `consist->tile` / `mf` / `origin` 三者；或在同场景看是否恒有 `origin != consist->tile`。

### 8.3 本轮仍未做

- `RESV-UNSAFE-TAIL` 在新日志里的条数（`218,138` 形态按判读恒不出现，但需核数字）。
- P-456-4（容量三倍）仍按玩家口径「读数确定是三倍，具体数据无意义」⇒ 只做代码层解释，
  不再做数值取证（`R3RDearticulateOneGroup()` 只烘焙 weight/power/max_speed，
  `cargo_capacity` 逐车沿链求和、打散不改成员 ⇒ 三倍不是打散直接造成的，
  重点转 `ConsistChanged()` 重复累加 / 读档双次调用，见 G-456-2）。

以上两条已在本轮补完，见第九节。

## 九、收尾落地（2026-11-18 夜）

### 9.1 P-456-1 / KI-654 已修：归还借用时补索引归一（只改 `src\train_cmd.cpp`）

（编号订正：断言崩溃 = **KI-654**；KI-653 是 P-456-3 的"改装信息报错"，两码事。
源码注释里原先写成 KI-653，已一并订正。）

- 现场：最新 `crash-*.log` 断言 `cur_implicit_order_index < GetNumOrders()` 失败
  （该日志第 5911 行），栈 `ProcessOrders ← TrainLocoHandler ← Train::Tick ← CallVehicleTicks`，
  涉事车 `veh 3`（`tf:qLKHSO`、`trk 0x01`）。
- 全流程**唯一**无界写入点 = `R3RReturnBorrowedOrders()`（`src\train_cmd.cpp:8057`）的
  `chain->orders_backup != nullptr` 分支：原第 8183 行直接把 `orders_backup_implicit_index`
  写进活字段，而紧邻的 `own` 分支（`:8157-8158`）早就有
  `num == 0 || INVALID_VEH_ORDER_ID || >= num ⇒ 0` 的归一。
- `DeleteUnreachedImplicitOrders()`（`src\vehicle.cpp:3455-3499`）救不了：入口
  `GetOrder(cur_implicit_order_index)` 越界即返回 nullptr ⇒ `while (order != nullptr)`
  整段不进入 ⇒ 不做任何纠正。越界值一路带到 `ProcessOrders` 的断言。
- 修法（已落地）：取回后按**恢复出来那张表**的 `GetNumOrders()` 归一 real/impl
  （timetable 跟 real），真发生裁剪时打
  `ORD-RETURN-BK-CLAMP veh=%d num=%u real=%d->%d impl=%d->%d`。
  与 `own` 分支逐字同构：`num == 0` 时二者都归 0（与既有行为一致，不引入新语义）。
  原 KI-472 的"只读探针"注释同时改写为已修说明，并把真实风险点（Delete 救不了）写清。

### 9.2 P-456-5 补数：`RESV-UNSAFE-TAIL` 计数 = 0

- 用 `Select-String -SimpleMatch 'RESV-UNSAFE-TAIL'` 统计 `build\R3R_debug.log`：**0 条**。
- 与 §8.2 判读一致：`unsafe=0` + `IsSafeWaitingPosition` 全程判"安全" ⇒ 根本不走 unsafe-tail
  兜底分支。veh=120 的问题是**预留终点本身停不住/走不到**，不是"落入不安全位被追认"。
- 口径保持：该探针只统计"判定不安全后仍写进尾部"的那条兜底路径，因此它恒 0 并不能证明
  "没有错预留"，只能证明"错预留不经过这个分支"。

### 9.3 P-456-4 代码层解释：三倍只能来自「override 层与组身份失配」，容量则是 CB36 逐车重算

读代码结论（本轮未改相关代码；weight/power/TE 与容量是两条不同的机制，分开写）。

**(A) weight / power / max TE 三倍 —— override 层**

1. 打散时不是"改数据"，而是把整组统计量**均分烘焙**到每节：
   `R3RDearticulateOneGroup()`（`src\vehicle_cmd.cpp:549-566`）——
   组头拿 `w_q + 余数 / p_q + 余数`，每个成员拿 `w_q / p_q`，三节都写同一个
   `max_speed_override`。耗用端优先读 override，且**在引擎/车厢身份门禁之前**
   （`src\train.h:500-506`、`src\train_cmd.cpp:25974-25980`）：
   `if (weight_override != UINT16_MAX) weight += weight_override; else if (!IsArticulatedPart()) …`
   ⇒ 只要 override 在，总重/总功率守恒，**不会**三倍。
2. override **是存档字段**：`src\saveload\vehicle_sl.cpp:209-211`
   （`weight_override / power_override / max_speed_override`，起始版本
   `SLV_R3R_ARTIC_OVERRIDE`）⇒ 排除"读档丢 override 导致×3"这条最直觉的解释。
3. 唯一能把总量放大成 N 倍的路径 = **override 被清回 `UINT16_MAX`，而成员仍是真车**：
   `R3RRearticulateOneGroup()`（`src\vehicle_cmd.cpp:713-715`）正是那个清除点；
   若"判定为重新铰接"但部件并没有真正变成 artic part（或组元数对不上），
   每节就回落成各自引擎记录的 weight/power，GRF 给每个部件声明整组值时即 N 倍。
4. 另一条同族路径 = 快照本身已被放大：`GetPowerSnapshot()/GetWeightSnapshot()`
   是沿链把每节的 `GetPower()/GetWeight()` 求和，若上一次打散/重组已把总量顶成 3×，
   再打散就会把 3× **烘焙进**新的均分值，于是三倍"跟着链走"。
- **判别办法（现成探针，不需要新代码）**：`CARGOWEIGHT-VEH`（`src\ground_vehicle.cpp:151-155`）
  已逐节打 `ov= / ew= / pc= / cw=`。若玩家报三倍时三节都是 `ov=65535` ⇒ 命中第 3/4 条；
  若 `ov` 是正常份额（如 `ov=12`）而界面仍显示三倍 ⇒ 三倍在**读取端聚合**
  （`cached_total_weight` / TE 汇总 / 段级统计重复计数），与 override 无关。

**(B) 容量 +1 / 三倍 —— 容量根本没被烘焙**

1. `R3RDearticulateOneGroup()` **从不写 `cargo_cap`**；容量每次 `ConsistChanged()` 逐节重算：
   `src\train_cmd.cpp:614` `uint16_t new_cap = e_u->DetermineCapacity(u);`
   → `Engine::DetermineCapacity()` → `GetVehicleProperty(u, PROP_TRAIN_CARGO_CAPACITY, …, u)`
   = NewGRF CB36，**实参就是这一节车**，回调能看见 artic 角色 / `gcache.first_engine` /
   consist 内位置（0x40·0x41），而打散改掉的正是这层身份。
2. 既有护栏只拦"回落成 0"（`IsArticGroupMember() && !IsArticGroupHead() && new_cap == 0`），
   **不拦"涨上去"**。
3. 显示端不去重：`R3RConsistWholeConsistCapacity`（`src\train_gui.cpp:610-635`）是
   `for (u = v; u; u = u->Next()) item->capacity += u->cargo_cap;` ⇒ 各带一份就各算一份。

**结论**：两条都是"打散把 GRF 的整组语义摊到 N 节"的后果，落在 override 层（重量/功率/TE）
和 CB36 层（容量）两处，**不是打散写坏了成员数据**。要压回去需要在打散时对非组头成员
把 `cargo_cap` 显式置 0 / 对 CB36 结果设上限，属策略选择，本轮未做，待玩家拍板；
先用 `CARGOWEIGHT-VEH` 的 `ov=` 区分"override 失配"还是"读取端重复计数"。

### 9.4 构建自证（2026-11-18 21:25，MODE=inc，完成三件套）

入口 = `R3R_fullbuild.cmd inc detach`（复用既有脚本，未新建任何 `.cmd`）；构建前 `tasklist` 已确认
`openttd.exe` 未运行。两次增量（第一次吸收 `order_cmd.cpp`，第二次吸收 `train_cmd.cpp` 里的
KI 编号注释订正），最终产物读数为：

| 项 | 读数 |
|---|---|
| `build\R3R_incbuild.done` | `EXIT_CODE=0` |
| `build\R3R_incbuild.log` 尾部 | `[2/3] Building CXX object ...\src\train_cmd.cpp.obj` → `[3/3] Linking CXX executable openttd.exe`，`R3R build ended 2026/11/18 21:25:30.90 MODE=inc` |
| `src\order_cmd.cpp` | `2026-11-18 20:52:07`，266 325 B |
| `build\CMakeFiles\openttd_lib.dir\src\order_cmd.cpp.obj` | `2026-11-18 20:54:10`，5 994 508 B（晚于源码 ✓） |
| `src\train_cmd.cpp` | `2026-11-18 21:20:25`，1 380 930 B |
| `build\CMakeFiles\openttd_lib.dir\src\train_cmd.cpp.obj` | `2026-11-18 21:22:24`，16 476 546 B（晚于源码 ✓） |
| `build\openttd.exe` | `2026-11-18 21:25:18`，53 030 400 B（晚于两个 obj ✓） |
| `read_lints` | 两文件 0 条 |
| exe 字面量 | 命中 `ORD-RETURN-BK-CLAMP` |

注：`R3REnsureCoupleEndOrdersInList` 是文件内 `static`，exe 内搜不到符号属正常（可能被内联），
不能据此判定"没编进去"；判定以 `order_cmd.cpp.obj` 时间戳晚于源码为准。

### 9.5 本轮交付状态一览（截至 2026-11-18 21:25）

| 问题 | KI | 状态 |
|---|---|---|
| P-456-1 断言崩溃 | KI-654 | **已修 + 已编译**，游戏内复测待做（> 后被第 457 轮 KI-658 取代：同一断言的池层入口，见 `R3R_round457_assert5952_memo.md`） |
| P-456-2 全表（自动填充）/灰显 | KI-655 | **已修 + 已编译**，游戏内复测待做 |
| P-456-3 改装信息报错 | KI-653 | **部分定性（第 458 轮）**：OCR 已取到弹窗原文（`中国火车 0.4` / `DF3`），触发点钉死在造车命令内；H-653-A（KI-641 保留部件容量致 `carries_more`）待 `BUY-REFIT … more=` 日志判定。见 `R3R_round458_three_answers_memo.md` §一 |
| P-456-4 ×3 / 容量 +1 | KI-656 | **已定性（非显示问题）+ 玩家批准修法 + 已落地**：`Train::ConsistChanged()` 容量分支对「打散组非组头成员」强制归 0（组头已承担时），保留 KI-641 兜底；见第 458 轮 |
| P-456-5 veh=120 不安全停车位 | KI-657 | 部分定性（同格再订 + `CPLRES origin` 语义未钉死，REG-456-E）；第 458 轮改写为大白话三选一（推荐 (a) 只加只读探针钉死 origin 语义） |
