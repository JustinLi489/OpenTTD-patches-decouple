# 第 412 轮论证：GOTO_STATION「车站自动调节车次」+「条件性分配调度（调度与列车分离）」

**状态**：本轮只论证，**零源码改动、零构建**。待玩家用 §7 的 C-1~C-5 回填后，再决定是否进入实现。
**KI 条目**：`R3R_KNOWN_ISSUES.md` 的「第 412 轮」小节（KI-575~KI-578）。

---

## 〇、玩家原话（口径原文，不得改写）

> 「我希望对 GOTO_STATION 添加两个功能，第一个是在车站自动调节车次（应当一个简单移植就行了），第二个是条件性分配调度（这需要我们做一个调度与列车的分离），我们先论证一下」

---

## 一、口径假设（未确认，§7 待回填）

- **A-1（功能一的语义）**：假设「在车站自动调节车次」= 让**某一个 GOTO_STATION 订单**具备「到站后自动调节本排程各车的班次/间隔」的能力，即把现有的**车级**开关 `VehicleFlag::TimetableSeparation`（`src/base_consist.h:50`）与 `UpdateSeparationOrder()` 的能力**下放到订单级**；「简单移植」指复用现成算法而不新造。
- **A-2（功能二的语义）**：假设「条件性分配调度」= 列车到达某站后，按**条件**被分配/切换到一份**独立于列车存在**的排程（调度）；因此必须先有「调度」这个实体，才有「列车跑哪份调度」这件事可分配。
- **A-3**：两个功能共用同一块地基（见 §2.4），故一起论证、分期落地。

---

## 二、地基事实（全部为代码实证，附锚点）

### 2.1 「调度与列车分离」其实已经做了**一半**

| 事实 | 锚点 |
| --- | --- |
| 读侧已有**间接层**：`GetEffectiveOrders()` = 有 `r3r_follow_orders` 就用它，否则用 `orders` | `src/vehicle_base.h:405`（注释在 `:388`） |
| 全代码 12 处读排程的地方都走它（`GetFirstOrder`/`GetNumOrders`/`GetOrder`…） | `vehicle_base.h:864, 901, 911, 922, 934, 1181, 1194, 1204, 1371, 1377` |
| 段级排程已经有**槽**：`R3RSegmentRecord` 持有 `OrderList *orders` + 三索引 | `src/couple_group.h:547` |
| 借用关系有显式标记：`orders_borrowed` + `Vehicle::r3r_orders_borrowed` | `couple_group.h:551`；`vehicle_base.h` 同名字段 |
| 但**写侧不走间接层**：Couple/Decouple 直接搬 `orders` 指针 + `orders_backup` + `r3r_follow_owner` | KI-04 / KI-527 / KI-526 家族债务的总源 |

⇒ 结论：**分离缺的不是读侧的抽象，缺的是「槽」这个对象的生命周期与进度归属**。这也是为什么 R3R 现在每次「换谁跑这份排程」都要靠 `orders_backup`、`r3r_follow_owner`、`r3r_orders_borrowed` 三件套手工搬指针，进而催生 KI-04（借用期索引冻结）、KI-527（AUTO-ORD anchor 用借来坐标）、KI-526（第 8 个借用入口漏停位）。

### 2.2 「车次/调度」的现成载体已经有了，但归属位置不对

| 事实 | 锚点 |
| --- | --- |
| `DispatchSchedule`（调度表：起止时间、间隔、slot 数组、lateness 策略）定义 | `src/order_base.h:1336` |
| `DispatchSlot`（单个班次槽）定义 | `src/order_base.h:1313` |
| 调度表挂在 **OrderList** 上（即"跟排程走"，不是跟车走） | `order_base.h:1591`（`std::vector<DispatchSchedule> dispatch_schedules`）+ `:1808-1814` |
| **但**「这趟车用第几个班次」存在 **Order** 上：`OrderExtraInfo::dispatch_index`（`Scheduled dispatch index + 1`） | `order_base.h:100` |
| slot 推进与迟延计算 | `DispatchSchedule::UpdateScheduledDispatch()` `src/schdispatch_cmd.cpp:1079`；`UpdateScheduledDispatchToDate()` `:1056` |

**这是功能一真正的硬障碍（也是功能二的动机）**：
`dispatch_index` 在 **Order** 上，而共享排程（Ctrl+共享 / R3R 车组共用表）意味着**同一张表被 N 列车共用 ⇒ N 列车共享同一个 `dispatch_index`**。于是"每列车跑不同车次"目前只能靠两条歪路实现：
1. 每列车各存一份**不共享**的排程（放弃共享排程的所有好处，且 R3R 的段/挂接语义会频繁改写表）；
2. 干脆不用 slot，靠 `lateness` 漂移"A 车自然落后 B 车"（不可控、会被 R3R 的索引冻结放大）。

### 2.3 「条件」机制也已经有了，但只能**跳转**不能**分配**

- `OrderConditionVariable` 已含 `DispatchSlot` / `Unconditionally` 等变量（`order_base.h` 的 `enum class OrderConditionVariable`）。
- `OT_CONDITIONAL` 订单可以用 `GetConditionDispatchScheduleID()` 引用一个调度表 ⇒ **条件性地"跳到某个调度表"在语义上已存在**。
- 但它的动作是「**改变本车订单流的走向**」（改订单/跳过），不是「**给这列车换一份调度**」。所以功能二不是"条件引擎缺失"，而是"**被分配物**（调度）不是实体"。

### 2.4 「自动调节」的现成算法（功能一可**原样移植**的部分）

| 能力 | 锚点 | 可复用度 |
| --- | --- | --- |
| 同排程多车迭代器 `R3RForEachTimetableVehicle(v, func)` | `src/timetable_cmd.cpp:48`（全文件 14 处调用） | **直接复用**，这是"调节谁"的答案 |
| 间隔/迟延均分 `UpdateSeparationOrder(Vehicle*)` | `timetable_cmd.cpp:853`；门在 `:1050`（`if (flag TimetableSeparation) UpdateSeparationOrder(v)`）、`:1253` | **直接复用**核心算法 |
| 出发时刻均分（一次性） `CmdSetTimetableStart(..., timetable_all, start)` | `timetable_cmd.cpp:568`（`timetable_all` = 对所有同排程车按份均分） | **直接复用**公式 |
| 车级开关 `VehicleFlag::TimetableSeparation`，默认值来自 `settings.vehicle.auto_separation_by_default` | `base_consist.h:50`；构造时 `train_cmd.cpp:3428` | 功能一要么**沿用**它、要么**下放**成订单级 |
| 开关的批量设置命令 `CmdTimetableSeparation` | `timetable_cmd.cpp:725`（`:739` 用 `R3RForEachTimetableVehicle` 同步全组） | 新增订单级开关时按同一模式写 |

**三者互斥矩阵（必须尊重，否则会出现静默失效）**：

- `TimetableSeparation` ⟂ `ScheduledDispatch`：`schdispatch_cmd.cpp:74` 直接返回 `STR_ERROR_SEPARATION_MUTUALLY_EXCLUSIVE`。
- `TimetableSeparation` ⟂ Unbunching 订单：`order_cmd.cpp:2673` 返回 `STR_ERROR_UNBUNCHING_NO_UNBUNCHING_AUTO_SEPARATION`。
- `schdispatch_gui.cpp:413` 在 `TimetableSeparation || HasUnbunchingOrder()` 时禁用调度按钮。

---

## 三、功能一论证：GOTO_STATION 车站自动调节车次

### 3.1 三个候选语义

| 候选 | 语义 | 改动面 | 存档 | 风险 | 结论 |
| --- | --- | --- | --- | --- | --- |
| **F1-a** | 订单级「到站自动调节班次间隔」：给 GOTO_STATION 加一个开关；列车在该站停稳时，对本排程全体车执行一次均分（等价于把 `CmdSetTimetableStart(timetable_all)` / `UpdateSeparationOrder` 自动化） | `OrderExtraInfo::xflags` 取一位（无需新字段）+ `MOF_R3R_AUTO_*` 白名单 + `order_gui.cpp` 一个下拉/复选 plane + 到站钩子调 `UpdateSeparationOrder` | **零格式变更**（复用 `xflags` 位） | 低：算法与被调函数都是现成的；需处理与 `TimetableSeparation`/`ScheduledDispatch` 的互斥提示 | **推荐** |
| **F1-b** | 订单级「到站自动推进车次槽」（自动改 `dispatch_index`） | 需要先有「车↔槽」绑定（§2.2），否则改订单会波及同表的所有车 | 需要新字段 | 高：等于提前实现功能二的一半，且会踩共享记账 | 归入功能二 |
| **F1-c** | 到站自动调节「车次号/名称」 | 与 R3R 段身份（`unitnumber_backup`/`name_backup`/控制段）正面冲突 | R3VP/R3SG 已有一套 | 高：会造成第二套 identity 机制 | 不建议 |

### 3.2 F1-a 的最小落地清单（供实现轮直接用）

1. `OrderExtraInfo::xflags`（`order_base.h:101`）取一个尚未使用的位作 `R3R_ORDER_AUTO_SEP`；`order_base.h` 加 `HasR3RAutoSeparation()` / `SetR3RAutoSeparation()`（只改 .h 一处 ⇒ 会触发全量重编，需提前告知）。
2. `order_cmd.cpp` 的 `CmdModifyOrder` 对 `OT_GOTO_STATION` 白名单追加新 `MOF_*`（KI-124 的教训：**白名单漏加 = UI 静默失败**）。
3. `order_gui.cpp`：GOTO_STATION 的参数行加一个复选/下拉（参照第 71 轮 yard 下拉、第 152 轮 couple side 的既有做法）。
4. 钩子：到站停稳处（`TrainEnterStation` 之后的停靠态）判定 `current_order` 带该位 ⇒ 调 `UpdateSeparationOrder(v)`（或一次性均分 `CmdSetTimetableStart` 语义），并打探针 `R3R-AUTOSEP veh= tile= n=`。
5. 互斥校验：若本车带 `ScheduledDispatch`/Unbunching 订单，给出 `STR_ERROR_*_MUTUALLY_EXCLUSIVE` 类错误而不是静默。

### 3.3 功能一必须同时回答的口径问题

- 调节目标：均分**到达时刻**、均分**发车间隔**、还是保持**固定 headway**？（三种在 `UpdateSeparationOrder` 里只差迟延基准）
- 触发时机：**每次到站**、**仅该站**、还是**离站时**？
- 「车次」是否等于「一趟车的实例」（而不是 slot 号）？若是，功能一与功能二的边界要重新划。

---

## 四、功能二论证：条件性分配调度 = 调度与列车分离

### 4.1 为什么"必须"分离（三条硬理由，均有代码证据）

1. **被分配物不是实体**：调度表挂在 `OrderList`（`order_base.h:1591`），而 `OrderList` 的所有权在**车**身上（`Vehicle::orders` / `orders_backup` / 共享记账 `first_shared`/`num_vehicles`）。没有车就没有表 ⇒ 无法"把调度 X 发给列车 A"。
2. **车次归属位置错**：`dispatch_index` 在 Order 上（`order_base.h:100`）⇒ 共享排程的 N 车共享一个 slot，无法逐车分配（§2.2）。
3. **进度归属错**：权威进度是**借用期链头的活索引**（`r3r_follow_owner` + `ORD-PUSH` 补账，第 133 轮 KI-215b）。分配/切换调度时，进度必须跟着"调度实例"走，否则每次切换都要补一次账，KI-04/527 家族会成倍增长。

### 4.2 提议模型（三期）

**一期：`R3RSchedule` 实体 + 显式绑定（不自动分配）**

```text
R3RSchedule {                    // 独立池，稀疏块 R3SC（CH_SPARSE_TABLE）
    uint16_t   id;
    OrderList *orders;           // 本体仍由 ORDL 写出，这里只存 REF_ORDERLIST
    uint16_t   dispatch_schedule; // 指向 orders->dispatch_schedules 的下标（0 = 无）
    uint16_t   dispatch_slot;     // 「车次」= 车↔槽绑定，从 Order 上搬到实体上
    TinyString name;              // 可选：给玩家看的调度名/车次名
    /* 进度：cur_real / cur_implicit / cur_timetable + timetable_start */
};
Vehicle::r3r_schedule_id        // 新字段；0 = 无调度（走现状路径）
```

- 读侧：`GetEffectiveOrders()`（`vehicle_base.h:405`）内部改为「有 `r3r_schedule_id` 就取实体的 orders，否则回退现状」⇒ **全部 12 个读点零改动**。
- 写侧：新增 `R3RBindSchedule(Vehicle*, R3RScheduleID)`（命令），取代"搬指针"式的 `orders_backup` 三件套。
- 存档：新块 R3SC + `Vehicle` 一个 uint16 字段；旧档读入时**就地生成"一车一调度"**（保持行为逐字不变），不做迁移期语义变更。

**二期：条件求值 + 到站自动分配**

- 条件集（确定性求值，禁止读 UI/tick 之外的易变状态）：在站货种/装载率、在站车辆数、本调度上一趟的返回时刻（lateness）、段/挂接状态、是否已有同调度列车占用、公司资金。
- 分配点：只允许在「停稳 + 站台格 + 订单边界」求值（与 REG-405-A 的会合点同类约束），分配动作走**命令**（可回放、可回滚），并在分配时强制 `current_order.Free()` + 三索引重置 + `timetable_start` 归位。
- 探针：`R3R-ASSIGN veh= from= to= cond=... result=ok|reject reason=`。

**三期：借用层退役**

- `orders_backup` / `r3r_orders_borrowed` / `r3r_follow_orders` / `r3r_follow_owner` 退化为"实体引用 + 旧档兼容读"，KI-04 / KI-527 / KI-526 一次性收口。

### 4.3 交互矩阵（每格都是必须写进设计表的硬约束）

| 交互 | 该怎么做 |
| --- | --- |
| Ctrl+共享排程 | 共享的是 `OrderList`，不是 `R3RSchedule`；一份调度可被 N 车引用 ⇒ 引用计数必须覆盖到实体（否则 `FreeChain` 提前销毁 ⇒ 复现 KI-93 崩溃家族） |
| 挂接 / 解挂 | 「半个调度属于哪一半」必须先定：建议**调度跟控制段走**（与第 152 轮 P1=甲「控制段==链头所在段」同源），解挂时按段边界判定是否拆成两个调度 |
| 段（★/⊗） | `R3RSegmentRecord::orders`（`couple_group.h:547`）与实体二选一：建议实体是**权威**，段行退化为缓存 |
| 车库拖动 / 卖车 | 引用计数 -1；为 0 时是否销毁要明确（"调度常驻池"就不销毁） |
| 克隆 | 克隆品必须**新建调度实例**（不能共享进度），与第 225 轮 KI-332 的"新段 ID + 复制行内容"同款做法 |
| 读档 | 指针阶段 `REF_ORDERLIST` 恢复；顺序必须早于 `AfterLoadVehiclesPhase2` 的排程重建 |
| 答案本（第 408~411 轮） | 答案本按"排程 + 起点"缓存路径；调度换了要能失效（否则会拿旧调度路径复跑） |
| 条件订单 `OT_CONDITIONAL` | 与功能二语义重叠，需明确「条件跳转」与「条件分配」谁优先，避免两套条件引擎 |

### 4.4 五个主要风险

1. **热切换排程会打断索引一致性**：`cur_real`/`cur_implicit`/`cur_timetable` 与 `current_order` 三者必须原子切换，否则触发既有 `TT-DESYNC` 家族断言；对策=只在停稳+订单边界切换。
2. **共享记账**：`OrderList::first_shared`/`num_vehicles` 不知道实体的存在 ⇒ 必须先扩引用计数（或让实体成为 `OrderList` 的"第一引用者"）。
3. **存档迁移**：旧档"进度在车上"，新档"进度在实体上" ⇒ 必须做一次性就地生成，且**不得**改变既有存档格式的语义（ORDL 原样）。
4. **借用层退役的连带影响**：R3R 现有大量探针/断言/补账逻辑（`ORD-*`、`SEGTR-*`、`AUTO-ORD-*`）都假设借用层存在，三期要同步改写，工作量被低估的风险高。
5. **命令/条件回放**：条件求值若读"当前 tick"以外的可变状态，会引入 desync；且分配必须走命令（不能在 tick 里直接改状态）。

---

## 五、本轮刻意不做

- 不动 ORDL / ORDLS 存档格式，不动 `dispatch_index` 现有语义。
- 不动 desync 冻结区（`CheckTrainsOnTrack` / pbs 路径）与答案本缓存键。
- 不动 R3R 借用层（属三期）。
- 不在本轮写任何代码、不构建。

---

## 六、判据（供实现轮验收，按现有格式）

**F1-a（功能一）**：① 带标记的车到站后出现一次 `R3R-AUTOSEP`，同排程 N 车的 `timetable_start` 间隔趋于均分；② 未带标记的排程行为逐字不变（负判据）；③ 带 `ScheduledDispatch`/Unbunching 的车给出互斥错误而非静默；④ 存档往返后标记仍在（`xflags` 已存档）。

**一期（功能二）**：① 旧档读入后 `R3SC` 每车一条、`GetEffectiveOrders()` 返回值与改前逐字相同；② 显式 `R3RBindSchedule` 换调度后，`cur_real` 从新表的位置开始而不是 0；③ 挂接/解挂/克隆/卖车后引用计数无泄漏（`R3SC` 条目 = 活车引用数）；④ 负判据：未使用调度的存档，行为与二进制输出（日志）逐字不变。

---

## 七、待玩家回填（按编号答，答完直接回填本节）

- **C-1** ✅ **已回填（见 §8.1）**：「车次」= **①班次/发车间隔（时刻表）**。玩家 2026-11-10 订正"游戏原文应该是『自动调节班次』"。
- **C-2** ✅ **已回填（见 §8.1）**：按"班次"解读 ⇒ 调节对象是时刻表间隔；具体**目标/时机**拆到 §8.6 的 C-6/C-7 继续问。
- **C-3**：功能二的**条件输入**（可多选）：货种或装载率 / 在站车辆数 / lateness / 段-挂接状态 / 公司资金 / **本车属性（可靠性/极速/功率/牵引力）** / 其它；条件满足时的**动作**：① 换整份排程 ② 只改后续某几条订单 ③ 只换车次槽 ④ 从调度池领一份新调度并替换旧调度。
- **C-4**：一份池内调度**同时被多列车引用**（N 车 1 调度）？还是"发出去就独占、跑完归还"？—— 与"每份表最多分配几份"直接相乘，决定池条目要不要引用计数。
- **C-5**：池的生命周期归谁（车站 / 车队 / 公司）；车站被拆/改名后池怎么办。

---

## 八、第 412 轮续（2026-11-10）：玩家订正 + 功能二六条要求的逐条落地分析

> 玩家原话（本轮，不得改写）：
> 「等一下，第一点可能因为我的笔误导致了你的理解错误。游戏原文应该是"自动调节班次"。（对应C1，C2）
> 再说说条件性分配调度命令的一些细节：触发条件性分配调度命令需要列车在GOTO_STATION命令处选择"等待分配命令"的调度属性，同时，在该分配车站放入供分配的调度计划（每个表限定好最多分配几份）。执行这个之后，列车把正在执行的表和排程进度的指针按条件挪到供执行的表上面，也就是新的借用（你应该还记得自身表，指向表，自身排程进度和借用排程进度，如果必要，需要创建第五和第六项表属性和排程进度）。执行调度分配命令的本车的各种属性，例如可靠性，极速，功率，最大牵引力……我还希望支持像可编程信号灯那样添加条件系统（if,else,elif）。
> 这一轮继续明确要求和论证」

### 8.1 C-1 / C-2 回填（口径订正，§一 A-1 措辞作废）

| 项 | 结论 | 影响 |
| --- | --- | --- |
| C-1「车次」 | = **班次 / 发车间隔（时刻表）** | 功能一 = 备忘 §3.2 的 **F1-a**，**不是** F1-b（推进 `dispatch_index`）也不是 F1-c（改车次号） |
| 连带 | `OrderExtraInfo::dispatch_index`（`order_base.h:100`）与功能一**无关** | KI-577 降级为"仅功能二相关"；KI-578（车次口径未定）**收口作废** |
| 功能一定案 | 车级 `VehicleFlag::TimetableSeparation`（`src/base_consist.h:50`）+ `UpdateSeparationOrder()`（`src/timetable_cmd.cpp:853`）**下放到订单级**；触发钩子 = 到站停稳 | KI-575 定案 F1-a，存档零格式变更 |

### 8.2 功能二六条要求的逐条落地分析

**（1）触发方式 = GOTO_STATION 上的订单级标记**

现成位空间实测：`OrderExtraInfo::xflags`（`order_base.h:101`）8 位**已被占用 0/1/2-3/4/5-7**（`order_base.h:62-67` 的注释），其中 **bits 5-7 = `GetRoadVehTravelDirection()`，只对公路车有意义**（`order_base.h:1117-1124`，配合 `SetRoadVehTravelDirection`）。
⇒ 对 **Train 的 GOTO_STATION** 取 **bit 5** 作 `R3ROF_WAIT_DISPATCH`（"等待分配命令"）**不冲突、存档零格式变更**，与功能一同一手法。
⇒ 必须同步：`CmdModifyOrder` 对 `OT_GOTO_STATION` 的 `MOF_*` 白名单（KI-124 教训：漏加即 UI 静默失败）+ `order_gui.cpp` 一个 plane + 存档字段沿用 `xflags`。

**（2）分配车站上的「供分配的调度计划池」+ 每表「最多分配几份」**

- 存储落点：`Station` 已有**成熟先例** —— `R3RStationYard`（`station_base.h:939-953`）+ 独立 chunk `SYRD`（`src/sl/station_yard_sl.cpp`，**不动上游共享的 STNN 布局**，`station_base.h:936-938` 的注释自陈）。
  ⇒ 池走同一条路：新 chunk（推荐，`R3DP`）或 SYRD 升版；**绝不动 STNN**。
- "最多分配几份" = 池条目的**配额计数器**：`issued`（已发出）/ `limit`（上限）。它同时也是 C-4（是否共享）的实现载体：`limit>1 && 允许共享` ⇒ 同一份表被 N 车同时跑（= 原生共享排程语义）；`limit=1` ⇒ 独占，跑完归还。

**（3）执行动作 = 把「正在执行的表 + 进度指针」按条件挪到「供执行的表」上（新的借用，不是复制）**

这条把功能二**锁定为借用层行动**，不是"改 `orders` 指针"。玩家列的四项与现有字段的对应（`vehicle_base.h:377-416`）：

| 玩家口径 | 现有字段 | 现状与缺口 |
| --- | --- | --- |
| 自身表 | `orders`（`:377`） | 第 294 轮 KI-465 修法 D 后，借用期**不再覆盖**自身表 |
| 指向表 | `r3r_follow_orders`（`:394`）+ `r3r_follow_owner`（`:395`） | 读侧经 `GetEffectiveOrders()`（`:405`）已全量走它 |
| 自身排程进度 | `orders_backup_real_index`（`:413`）/ `orders_backup_implicit_index`（`:414`） | **只有两项**：`cur_timetable_order_index` 没有停放副本 |
| 借用排程进度 | `cur_real_order_index` / `cur_implicit_order_index` / `cur_timetable_order_index` | 活索引；借用期由链头 tick，靠 `ORD-PUSH` 回写 owner（KI-215b） |

⇒ **现有四项够一次分配，但不够玩家要的形态**，两个硬缺口：
- **缺口 a（第五项）**：`cur_timetable_order_index` 无停放位 ⇒ 一旦列车有时刻表进度，"分配后归还"必丢时刻表位置。**必须新增 `orders_backup_timetable_index`**（落 `R3VP` 稀疏块，`src/sl/couple_group_sl.cpp`，不碰车辆表布局）。
- **缺口 b（第六项）**：现借用是**单层**指针（`r3r_follow_orders` 一个）⇒ 若被分配到的表自己也在被别人借用（链式/池条目被指定 owner 车），无法表达"返回谁"。⇒ 要么禁链（分配只允许指向"自身表"，即池表必须是叶子），要么升级为**借用栈**。

**（4）本轮最重要的新结论：「表的主人」必须能是「车站池条目」（第三态）**

现有借用全是**车→车**关系，owner 由 `r3r_priority` 最小者决定（`vehicle_base.h:415` 注释 + `R3RRebuildCouplePriorities()`），进度靠 `R3RPushProgressToOwner()` 写回 owner **车**（KI-215b，`train_cmd.cpp`）。
但池里的表**没有 owner 车** ⇒ 下列三处现有机制全部无法表达它：
1. `R3RRebuildCouplePriorities()`（按链上优先级选 owner）——选不出"车站"；
2. `R3RPushProgressToOwner()`（写回 `VehicleID`）——`r3r_follow_owner` 是 `VehicleID`，装不下"车站+槽号"；
3. `OrderList::FreeChain()`/共享记账（`first_shared`/`num_vehicles`）——池表的引用者可能是 N 辆不同车 + 车站本身。

⇒ 这就是玩家说的"第五和第六项表属性和排程进度"的**真正实质**：不是给车再加两个字段，而是**给"表的主人"增加第三种类型**。最小实现：`r3r_follow_owner` 之外加 `r3r_follow_pool`（车站 ID + 池槽号 + 池内进度副本），`R3RPushProgressToOwner` 在 owner 无效时回写池条目。

**（5）「本车属性（可靠性/极速/功率/最大牵引力）」——语义两解，必须拍板（C-8）**

- α：**作为条件输入**（按本车属性选不同调度）；
- β：**作为调度的期望值**（这份调度期望多快/功率多大的车，跑不动的车不接）；
- γ：**作为分配记录**（写进池/日志/统计，不参与判定）。
三解的实现代价差一个量级（α/β 要读 `GroundVehicleCache`/`Engine` 属性并新增条件变量；γ 只是探针），**未拍板前不写码**。

**（6）「像可编程信号灯那样 if / elif / else」——现成范式已存在，但只有 if**

| 事实 | 锚点 |
| --- | --- |
| 可编程信号灯有 `SignalProgram`（指令数组）/ `SignalInstruction` / `SignalVM` | `src/programmable_signals.h:33` / `:65` / `:26` |
| 有 `SignalIf` 指令 | `programmable_signals.h:309` |
| 条件基类 `SignalCondition` + `SignalSimpleCondition`/`SignalVariableCondition`/`SignalStateCondition`/`SignalSlotCondition`/`SignalCounterCondition` + `SignalComparator` | `programmable_signals.h:133/154/174/190/201/219/236/161` |
| 订单侧已有 `OT_CONDITIONAL` + `OrderConditionVariable`（含 `DispatchSlot`/`Unconditionally`） | `order_base.h`（见 §2.3） |

⇒ 结论：**elif/else 在信号灯里是"用跳转手工拼出来的"**，没有原生块结构。所以正确做法不是照抄 `SignalIf`，而是：
- **结构层**：在订单流里定义"块"（`IF` / `ELSEIF` / `ELSE` / `ENDIF` 四种标记，或一条 `IF` 订单携带 `jump_if_false` 偏移）；
- **求值层**：复用/扩展 `OrderConditionVariable`（订单侧已有，能读车辆上下文），而不是引入 `SignalVM`（那是信号灯专用状态机）；
- **UI 层**：可编程信号灯已有"程序编辑器"GUI，可作交互参照（`src/programmable_signals_gui.cpp`）。

### 8.3 存档影响（初评）

| 新增物 | 落盘方式 | 是否改共享表 |
| --- | --- | --- |
| 订单位「等待分配」 | `xflags` bit 5（火车） | 否 |
| 池（车队列表 + 配额 + 池内进度） | 新 chunk `R3DP`（照 SYRD 先例） | 否 |
| `orders_backup_timetable_index` | `R3VP` 稀疏块加一字段 | 否 |
| `r3r_follow_pool`（第三态主人） | `R3VP` 加字段 | 否 |
| if/elif/else 块 | 若复用 `OT_CONDITIONAL` 的跳转偏移则零新增；若加新 `OrderType` 则要动 `ORDL`（**风险高，不推荐**） | 视方案 |

### 8.4 分期（更新，替换 §4.2 的四期表述）

- **一期**（功能一，可立即实现）：订单位 + 到站钩子调 `UpdateSeparationOrder`。零存档变更。
- **二期**（功能二最小闭环）：订单位「等待分配」+ 车站池 chunk + **无条件**领取/归还 + 池条目进度 + 缺口 a/b。**不碰条件系统**。
- **三期**（条件 DSL）：`if / elif / else` 块 + 条件变量（本车属性 / 装载 / 时间 / lateness）。
- **四期**（可选，退役）：`R3RSchedule` 实体化，`orders_backup`/`r3r_follow_*` 三件套退役（KI-04/526/527 收口）。

### 8.5 §五「不做清单」追加

- 不在二期做条件求值（把"分配"与"判断"解耦，先保证分配/归还可逆）。
- 不引入 `SignalVM`/信号灯指令集（上下文不同，且它无 elif/else 块）。
- 不改 `STNN`、不改 `ORDL` 的既有字段含义（新东西一律走 `xflags` 位与独立 chunk）。

### 8.6 新待回填（编号继续，答完直接回填本节）

- **C-6**：功能一的**调节目标**：① 均分**到达**时刻 ② 均分**发车间隔** ③ 固定 headway（指定值）④ 两者都要（先固定 headway 再均分）。
- **C-7**：功能一的**触发时机**：① 每次到该站停稳 ② 只在该站停稳（别的站不管——本来就是订单级，需确认是否"只在这一条订单生效"）③ 离站时生效（影响下一趟）。
- **C-8**：C-8 = §8.2(5) 的三解 α/β/γ，「本车属性」到底是条件输入、调度的期望值、还是仅记录。
- **C-9**：池里一份调度**跑完怎么处理**：① 归还池（配额-1 复位）② 消耗掉（一次性，用完从池里删除）③ 留在车上不归还（列车继续跑它，池只发一次）。
- **C-10**：分配时**原表怎么办**：① 保留在车上（`orders` 不动，只换 `r3r_follow_orders`）② 一并归还池（换出来）③ 按条件决定。
- **C-11**：if/elif/else 的**条件作用域**：① 只在「等待分配」这一条订单上判断一次 ② 可以在订单流任意位置做分支（等于通用订单脚本）。
- **C-12**：池的**归属与可见性**：① 公司级公共 ② 车站级（同站多公司共享？）③ 车队级。

### 8.7 勘误与关键补充（第二轮取证，2026-11-10）

§8.2 里有两处**估计过高/过低**，实测更正如下（结论以本表为准，正文不改以免丢失推理痕迹）：

| 项 | §8.2 原写法 | 实测更正 | 锚点 |
| --- | --- | --- | --- |
| if / elif / else | "`SignalIf` 只有 if，无 else/elif（需确认）" | **原生有 if/else**：`SignalIf` 持有 `condition` / `if_true` / `if_false` / `after` 四个指针，并配 `PseudoInstruction` 伪指令（枚举 `SignalOpcode` 的 Else/Endif）负责"跳出 then 块"与"GUI 插入点"；**elif = else 块内再嵌一个 if**（所以"支持 elif"= 允许嵌套，不需要新指令） | `programmable_signals.h:309`、`:368-371`、`:322-348`、`:311-321`（注释自陈 If-Else/If-Endif） |
| 「本车属性」作条件输入 | "α 解要新增条件变量，代价高，三解差一个量级" | **已有约六成**：`OrderConditionVariable` 现成含 `Reliability`(1) / `MaxReliability`(7) / `MaxSpeed`(2) / `Age`(3) / `RemainingLifetime`(6) / `LoadPercentage`(0) / `Percent`(11) / `CounterValue`(16) / `TimeDate`(17) / `Timetable`(18) / `DispatchSlot`(19) / `SlotOccupancy`(12) / `VehicleInSlot`(13) / `VehicleInSlotGroup`(21) / `DrivingBackwards`(22) / `Cargo*` 系列；**玩家点名的四项里"可靠性""极速"已存在**，**只缺「功率」「最大牵引力」** | `src/order_type.h:297-323` |
| 时刻表进度无备份位 | "缺 `cur_timetable_order_index` 的停放副本" | **成立，且位置更正**：该字段不在 `vehicle_base.h`，而是 **`base_consist.h:73`**（consist 级，随 `BaseConsist::CopyConsistPropertiesFrom` 复制）；备份侧只有 `orders_backup_real_index` / `orders_backup_implicit_index`（`vehicle_base.h:413-414`），全库无 `orders_backup_timetable*` | `base_consist.h:73`、`vehicle_base.h:413-414` |
| xflags bit 5 对火车空闲 | "bits 5-7 只对公路车有意义" | **成立**：`GetRoadVehTravelDirection()` 的全部使用点都带 `VehicleType::Road` 判定 —— `order_gui.cpp:1098`/`:1154`、`order_serialisation.cpp:242`、`roadveh_cmd.cpp:1614`、`yapf_road.cpp:282` ⇒ Train 的 GOTO_STATION 上 bits 5-7 恒被忽略，取 bit 5 作 `R3ROF_WAIT_DISPATCH` 安全 | 同左 |
| 「第三态主人」论证 | "owner 装不下车站" | **成立**：`R3RPushProgressToOwner()`（`train_cmd.cpp:7213-7235`）的 owner 来自 `R3RGetPriorityHead(chain)`（返回 `Train *`），并用 `owner->GetEffectiveOrders() != driven` 做同一张表校验 ⇒ 池表（无 owner 车）永远匹配不上，进度无处写回 | `train_cmd.cpp:7213/7220/7221/7233-7235` |

---

## 十一、一期实现（第 413 轮，2026-11-10）

玩家最新指令原话：「是的，C9就是比如表A 限 3 辆、表B 限 2 辆。如果明确要求了，就开工吧，给我一个初步成果」

### 11.1 结论：一期的载体选 `OrderExtraInfo::xflags` bit 5，零存档格式变更

理由（已在 §8.7 末行取证）：`GetRoadVehTravelDirection()`（bits 5-7）的全部使用点都带 `VehicleType::Road` 判定；订单表不跨车型共享，所以列车 `GOTO_STATION` 上的 bit 5 是干净空位。`SLE_CONDVAR_X(OrderExtraInfo, xflags, ..., XSLFI_TIMETABLE_EXTRA)` 已经在 `src/sl/order_sl.cpp:142` 序列化，**不需要新 chunk、不需要动 `ORDL`/`STNN`**。

### 11.2 语义落点：标记写在 Order 上 ⇒ 天然排程级

`v->Orders()` 返回的是 **effective** 订单表（借用跟随者拿到的是控制段那张），所以"排程里存在任一带标记的 `GOTO_STATION`"这一判据对整条排程的所有车同时成立，不需要像 `CmdTimetableSeparation` 那样显式遍历广播（`timetable_cmd.cpp:746-753` 的 `R3RForEachTimetableVehicle` 只是给"车级 flag"用的）。

`SeparationActive` 仍然只是入场券：本实现沿用原生机制 —— 打开标记时清掉同表所有主车的 `SeparationActive`，之后每辆车自己第一次到站/离站时由 `UpdateSeparationOrder` 首行置位。`PopulateSeparationState` 与状态机**一字未改**。

### 11.3 代码改动清单（逐文件）

1. `src/order_type.h`：`MOF_R3R_STATION_SEPARATION` 加在 `MOF_COUPLE_TEMP_GROUP` 之后、`MOF_END` 之前。
2. `src/order_base.h`：`xflags` 注释补 bit 5；`HasR3RStationSeparation()` / `SetR3RStationSeparation(bool)`（`AssignBit(GetXFlagsRef(), 5, ...)`，带幂等守卫）。
3. `src/order_cmd.cpp`：
   - `OT_GOTO_STATION` 的 `MOF_*` 白名单追加新枚举（**KI-124 的坑，漏了就是 UI 静默失败**）；
   - 新增文件级 `R3RSeparationConflictOnOrders(const Vehicle *v_start)`：按 `GetEffectiveOrders()` 找同表全部主车，查 `VehicleFlag::ScheduledDispatch` 与 `HasUnbunchingOrder()`；
   - 预检 case：`Train` + `OT_GOTO_STATION` + `data<=1`，冲突返回 `STR_ERROR_SEPARATION_MUTUALLY_EXCLUSIVE`；
   - 执行 case：`SetR3RStationSeparation(data != 0)` → 清同表 `SeparationActive` → `SetTimetableWindowsDirty(v, STWDF_ORDERS)`。
4. `src/timetable_cmd.cpp`：新增 `R3ROrdersHaveStationSeparation(const Vehicle*)`（`type != Train` 直接 false，防 bit 5 在公路车上的方向语义误判）；`:1250` 门禁 `TimetableStarted && (车级 flag || R3ROrdersHaveStationSeparation(v))`。
5. `src/widgets/order_widget.h`：`WID_O_R3R_SEPARATION`。
6. `src/order_gui.cpp`：plane 1 拆成 30px 场下拉 + 30px 切换按钮（顶行总宽仍 60px，见 KI-262）；`OnClick` 取反发命令；`OT_GOTO_STATION` 回显 `SetWidgetLoweredState`。
7. `src/lang/english.txt`、`simplified_chinese.txt`：`STR_ORDER_R3R_SEPARATION` / `_TOOLTIP`。

### 11.4 与二期接口的一致性检查

- C-9 坑位制（每份表各自 `limit`）与本期无冲突：一期只提供"排程级分离开关"，不含任何池/配额字段。
- C-11 条件写车站 ⇒ 二期在车站池条目上求值，本期不引入任何列车侧条件字段。
- 一期**不碰** `r3r_follow_orders`、`r3r_priority`、`orders_backup*`，故 KI-526/KI-527/KI-04 的行为不变。

### 11.5 待复测判据

见 `R3R_KNOWN_ISSUES.md` 的 KI-575「一期落地清单」末尾 7 条。核心两条：①同一排程 N 辆车，任一车打标记 ⇒ **N 辆车全部**参与均分；②公路/船/飞机看不到按钮、命令层也拒。

### 11.6 未做

时间表窗口的分离按钮不反映订单标记（只在订单窗口显示按下态）；`CmdTimetableSeparation` 不反向检查标记；繁中串未加（该文件既有 R3R 串本就缺）。

### 11.7 构建自证（2026-11-10）

入口：`R3R_fullbuild.cmd inc detach`（复用既有入口，未新建任何 `.cmd`）。护栏判定：`REMOVED 624 object file(s) - upgrading this build to a FULL rebuild`（因为动了 `order_type.h` / `order_base.h` / `widgets/order_widget.h` / 两个 lang txt）。

第一次全量 **失败**：`script_event.cpp.obj` 的 `FAILED:` + `build\generated\script\api\script_window.hpp(2329): error C2001: 常量中有换行符`。根因是 widget 头注释里写了英文双引号 —— 见 KI-584。改成纯英文注释后重跑。

第二次全量通过：

| 判据 | 读数 |
| --- | --- |
| `build\R3R_incbuild.done` | `EXIT_CODE=0` |
| 步数 | `[697/697]` |
| 日志错误计数 | `error C` / `fatal error` / `FAILED:` / `build stopped` = **0** |
| 链接 | `Linking CXX executable openttd.exe` 1 次 |
| 产物 | `build\openttd.exe` @ 2026-11-10 02:26:00，52 618 752 B |
| obj 时间戳 | `order_gui.cpp` 02:09:25→obj 02:17:56；`order_cmd.cpp` 01:49:41→obj 02:17:54；`timetable_cmd.cpp` 01:49:24→obj 02:21:48 |
| 语言串 | `build\generated\table\strings.h` 含 `STR_ORDER_R3R_SEPARATION = 0xC7F`、`STR_ORDER_R3R_SEPARATION_TOOLTIP = 0xC80` |
| lint | 三文件 0 条 |

### 11.8 构建期补的一处崩溃防护（必须记）

`order_gui.cpp` 最初写成 `this->SetWidgetLoweredState(WID_O_R3R_SEPARATION, ...)`。查 `window_gui.h:514-517` 发现该 inline 函数**不判空**（直接 `GetWidget<NWidgetCore>(...)->SetLowered()`），而同一个文件 `order_gui.cpp:2667-2675` 的注释已经写明「A hidden widget is not registered in the window lookup」并用 `GetWidget` 判空保护非停站按钮。

我们的按钮只存在于 `WID_O_SEL_TOP_YARD` 的 plane 1，在两种情况下不在查找表里：①车队列表多选行模式（`row_sel != nullptr`，走 `WID_O_SEL_TOP_ROW` 的 groundvehicle 行）；②非列车车辆（plane 恒为 `DP_YARD_EMPTY`）。⇒ 改为：

```cpp
if (NWidgetCore *sep_btn = this->GetWidget<NWidgetCore>(WID_O_R3R_SEPARATION); sep_btn != nullptr) {
    sep_btn->SetLowered(order->HasR3RStationSeparation());
}
```

教训：**订单窗口里凡是加进 `NWID_SELECTION` 的 widget，回显一律先 `GetWidget` 判空**，不要用 `SetWidgetLoweredState` / `SetWidgetDisabledState` 的简写。

---

## 十二、玩家首轮反馈（第 414 轮，2026-11-10）：按钮按不下去

玩家原话：「目前是只做了车站的均匀班次是吧，我向你报告，这个调节班次按钮按不下去」。

先回答前半句：**是的，一期只做车站**。落点是"订单表里任意一条 `GOTO_STATION` 带标记 ⇒ 整份排程的所有车参与均分"，池/坑位/每表限额（C-9）/条件（C-11）/回环归还都属二期，一行未动。

### 12.1 已排除项（读码，非猜测）

- 命令链路三段齐全：白名单 `order_cmd.cpp:2579`、预检 `:2995-3005`、执行 `:3113-3132`。
- 标记不会写丢：`GetXFlagsRef()` 自带 `CheckExtraInfoAlloced()`（`order_base.h:166-170`），`SetR3RStationSeparation` 的守卫只在"值已相同且 extra 非空"时提前返回（`:681-685`）。
- 控件确实属于 `OT_GOTO_STATION` 的 plane 1（`order_gui.cpp:2683-2697` 的 `DP_YARD_DROPDOWN`）。
- 回显代码确实在 `case OT_GOTO_STATION:` 内、`break` 之前（`:2818-2826`）。

### 12.2 三个候选（按可能性排序，均未验证）

| 编号 | 候选 | 表现 | 验证手段 |
| --- | --- | --- | --- |
| C-1 | 点击没进 `OnClick`（命中区被挤 / widget 被禁用） | 点下去连凹陷都没有 | `SEP-BTN-CLICK` 探针是否存在 |
| C-2 | 被 `R3RSeparationConflictOnOrders()` 拒（同表有 `ScheduledDispatch` / `HasUnbunchingOrder()`） | 有凹陷、状态不变、可能弹互斥提示 | `SEP-CMD-REJECT` 探针 |
| C-3 | 命令成功但回显被 `ReInit()` 吃掉 | 有凹陷、下一帧又弹回 | `SEP-CMD-DONE` 有、按钮仍不凹 |

### 12.3 本轮动作

只加只读探针（不改行为），请玩家复跑一次；判读规则写进 KI-585。

### 12.4 一个必须承认的设计缺陷（顺带登记）

即便按钮能按，**当前放在 `GOTO_STATION` 顶行的位置也不理想**：它和"目的地场"下拉框挤同一个 60px 槽，两个控件各 30px，`调节班次` 文字（中文 4 字）在 30px 内必然被裁。若探针证实是命中区问题，正解是把该按钮挪出 `WID_O_SEL_TOP_YARD`（例如放到第二行管理区或它自己的槽），而不是继续在 60px 里挤。这条等 C-1 的结论再定。

**由更正推出的成本重估（重要）**

1. **C-8 选 α（本车属性作条件输入）的成本从"整套变量"降到"2 个枚举值 + 2 个求值分支"**：
   - 新增 `Power` / `MaxTractiveEffort` 两个 `OrderConditionVariable`（追加在 `End` 之前，`order_type.h:322`）；
   - 求值处读 `GroundVehicleCache`（`gcache.cached_power` / `cached_max_te`，列车在 `ConsistChanged()` 后可用）；
   - 枚举是 `uint8_t`（`order_type.h:297`）且有 22 个已用值 ⇒ 空间充足；`OrderCondition` 的 variable 落盘宽度需在实现轮核对（若为 `SLE_UINT8` 则追加值向后兼容）。
   - ⇒ α 与 β 的成本差因此不再"一个量级"，**β（调度的期望值/门槛）仍明显更贵**（要给池条目加期望字段 + "跑不动就不接"的拒绝语义 + UI）。
2. **if/elif/else 的结构层可以直接照 `SignalIf` 抄树形（condition/if_true/if_false/after + 两个伪指令）**，比"在订单流里线性插 IF/ELSEIF/ENDIF 标记"更接近玩家说的"像可编程信号灯"；但**求值上下文必须换成订单/车辆**（不能挂 `SignalVM`）。
3. **功能二最小闭环的必做项收窄为三件**：
   - 池的存储与配额（新 chunk，照 `SYRD` 先例）；
   - **池条目持有进度**（因为 `R3RPushProgressToOwner` 只认 `Train *`）—— 这就是玩家说的"第五、第六项"的实质落点；
   - `orders_backup_timetable_index`（`R3VP` 加一字段）。

### 8.8 仍未取证（下一轮实现前必须确认）

- `OrderCondition` 的存档字段宽度与 `Evaluate()` 求值位置（决定新增变量与 if 块的落盘方式）。
- `SYRD` chunk 的版本/注册方式（决定新池 chunk 的样板量）。
- 池表被引用时 `OrderList::first_shared` / `num_vehicles` 的记账归属（池是否是"伪车辆"成员）。
- 列车 `orders_backup` 与池条目进度同时存在时的优先级（谁是真"自身进度"）。

---

## 九、第 412 轮续二（2026-11-10）：C-6/C-7 定案 + C-8~C-12 改写为玩法语言

> 玩家原话（本轮，不得改写）：
> 「C6,C7照抄原生的"自动调节班次"功能即可。若无法照抄再问我
> 剩余的这些你表达得更像是技术人员内部的决策，而非详细清晰的选项，我难以决断」

### 9.1 原生「自动调节班次」到底是什么（照抄基准，已取证）

| 事实 | 锚点 |
| --- | --- |
| 算法只做一件事：**改本车的 `lateness_counter`（晚点计数）**，不改时刻表、不改出发时刻、不改订单 | `timetable_cmd.cpp:853-902` |
| 收集对象 = **同一条排程**（`GetEffectiveOrders()` 相同）且 `SeparationActive` 的所有车，按累计进度排序 | `PopulateSeparationState` `:783-851`（准入在 `:801`） |
| **少于 2 辆车直接返回**（什么都不发生） | `:858` |
| 计算式 = 前车累计 + 后车累计 − 2×本车累计，首尾做环回补偿 | `:886`、`:888-892` |
| 结果先取一半，再按公司设置 `auto_timetable_separation_rate`（平滑率 %）与旧值混合 | `:895-898` |
| 触发点 ①：自动化时刻表**首次启动**时（初始化，非每站） | `:1050` |
| 触发点 ②：`UpdateVehicleTimetable` 收尾，即**每次到站/离站** | `:1253` |
| 到达与离站的分流：该订单"没填时间"且 `travelling==true` 时早退 ⇒ 实际**主要在离站那一刻生效**，订单填了时间则到达时也生效一次 | `:1246` |

### 9.2 结论：能照抄，唯一改动是一行门禁

订单级版本 = 在 `:1253` 调 `UpdateSeparationOrder(v)` **之前**加一句判断：只有"刚刚结束的那条订单"（`v->cur_timetable_order_index`）带「自动调节班次」标记时才调用。
算法本体、平滑率、`SeparationActive`、`<2` 早退、`PopulateSeparationState` **全部原样不动** ⇒ 这是**真移植**，不是重写。
⇒ **C-6 定案**：目标 = 均分同一排程多车的间隔（靠晚点计数平滑，不直接改时刻表）。
⇒ **C-7 定案**：时机 = 到站/离站，沿用原生的 `:1246` 分流规则（不另立规则）。

### 9.3 【已被玩家订正，C-13 取消】原前提错误：自动调节班次本来就是**排程级**属性

**玩家 2026-11-10 原话**：「Q1，只要是共享自有表的，它们都会一起算入自动调节班次，因此你似乎直接搞错了前提。这个原生就已经有解决方案了」。

原分析错在把门禁写成"刚刚结束的那条订单是否带标记"（车 × 站），于是推出"没经过该站的车永不置位"。原生根本不是这个形状：

| 事实 | 锚点 |
| --- | --- |
| 开关命令一执行，就作用到**整份排程上的所有车**（含借用跟随者） | `CmdTimetableSeparation` → `R3RForEachTimetableVehicle(v, ...)`（`timetable_cmd.cpp:746-753`） |
| `SeparationActive` 只是**入场券**，不是开关：命令执行时全部清掉（`:752`），此后每辆车**自己**在首次到站/离站时置位 | 清：`timetable_cmd.cpp:752`；置位：`UpdateSeparationOrder` 首行 `:855` |
| 收集组时过滤 `SeparationActive`，只是为了跳过"还没跑过一站"的车 | `PopulateSeparationState` `:801` |

⇒ 只要把订单级标记定义为「**排程里存在一条带标记的 `GOTO_STATION` ⇒ 整份排程开启自动调节**」，共享该排程的每一辆车在 `:1250` 的门禁上都会成立，各自到站即置位、全部算入同一个对齐组。原生语义无需任何新机制，**C-13 不是选项，取消**。
⇒ 顺带订正 §9.2 的措辞：`:1253` 前的门禁判的是"**这份排程要不要自动调节**"（排程级、与当前是哪一站无关），不是"刚结束那条订单带不带标记"。

### 9.4 C-8~C-12 改写为玩法语言（**§8.6 的旧技术表述作废，以本节为准**）

| 编号 | 你要决定的事（一句话） | 选项（玩法说法） | 建议 |
| --- | --- | --- | --- |
| **C-8** | 条件里能不能用"这辆车本身的性能"？ | **A** 能当条件用：如"本车可靠性 > 80% 就跑 A 班表，否则跑 B 班表"（车好跑快线、车差跑慢线）<br>**B** 班表自带门槛：每份班表写"需要 120km/h 以上的车"，不达标的车不接、去等下一份<br>**C** 只记录不判断：分配时把本车参数显示出来给你看 | **A** |
| **C-9** | 池里的班表被领走之后怎么办？ | **A** 归还：跑完回到池里，还能再发给下一列车（循环班表）<br>**B** 消耗：一份只发一次，发出去就归那列车（派车单）<br>**C** 不收回也不删除：池只管发（等于复制一份给车） | **A** |
| **C-10** | 换班表时，车上原来那份怎么办？ | **A** 留着：跑完新班表能换回来（可逆）<br>**B** 交回池：旧班表还回车站，车上不保留<br>**C** 每条分配命令自己选 | **A** |
| **C-11** | "如果…就…否则…"能写在哪？ | **A** 只在"等待分配"那一站判断一次：到站分流、选完就走（简单）<br>**B** 整条班表任意位置都能写（等于给列车写脚本，更灵活但更难维护） | **A**（一期）/B（三期） |
| **C-12** | 谁有资格来领这份班表？ | **A** 全公司共用（统一调度中心）<br>**B** 只有放它的那个车站能领（该站自己的班表）<br>**C** 只有指定车队能领（专线专车） | **B** |

推荐列的依据只有两条，不掺技术细节：
1. **能不能换回来**（C-9/C-10 选 A）—— 你要求的是"借用"，借用天然要能还；选 B/C 就变成一次性消耗，跟"跑完回来"冲突。
2. **一期能不能做完**（C-11 选 A）—— B 要额外解决"这条指令要跳到第几条"的维护问题，会拖慢第一期。

### 9.5 Q1/Q2 已全部答复（2026-11-10）⇒ 见 §十

- **Q1（= C-13）**：已由玩家订正，**C-13 取消**（自动调节班次是排程级属性，原生已有解）⇒ §9.3。
- **Q2（C-8~C-12）**：已按玩法语言作答 ⇒ §10.2 定案表。

本节不再有待答问题。

---

## 十、玩家口径回填（2026-11-10）：Q1 + C-8~C-12 全部定案

### 10.1 玩家原话（不得改写）

> 「Q1，只要是共享自有表的，它们都会一起算入自动调节班次，因此你似乎直接搞错了前提。这个原生就已经有解决方案了
> C8，能用"这辆车本身的性能"判断能否分配
> C9，这些表就像占坑位一样，只有固定数量的车能够在分配这些表
> C10，这些分配的表跑完一个回环，就应当交回池子里。
> C11，实际上，这些什么"如果极速达XXX"的分配条件，是写在车站上面的，而不是列车上。比如说，车站里面写：表A的分配条件是列车极速达160，表B的分配条件是最大牵引力达多少千牛。然后一台机车来这个车站领取分配命令，就按照这个规则来。C12应当就清晰了」

### 10.2 定案表

| 编号 | 定案 | 玩法表述 |
| --- | --- | --- |
| **Q1 / C-13** | **取消** | 自动调节班次是**排程级**开关：排程里有一条 GOTO_STATION 带标记，共享这份排程的车**全部**一起算入对齐（原生 `R3RForEachTimetableVehicle` 就是这个语义）。§9.3 |
| **C-8** | **A（本车性能参与判断）** | 判断对象 = **来领表的这辆车自己的性能**（极速、最大牵引力、可靠性…）。车达标才发，不达标不发。 |
| **C-9** | **坑位制（新，非 A/B/C）** | 每份表有**固定坑位数**：最多只有 N 辆车能同时持有这份表。坑满即不再发，后来的车继续等/按条件分流到别的表。 |
| **C-10** | **跑完一个回环后交回池** | 不是每条命令就还、也不是永不还：车把这份表**跑完一整圈（回环）**，即交回池子，坑位释放，可再发给下一辆车。 |
| **C-11** | **条件写在车站上（新表述）** | 「表A 需要极速 ≥160」「表B 需要最大牵引力 ≥ X kN」这些条件，写成**该车站池里每份表的进入条件**；列车到站申领时按规则匹配一次。既不在列车上，也不是班表任意位置。 |
| **C-12** | **B（该站自己的池）** | 由 C-11 直接推出：条件与表都属于某个车站 ⇒ 只有放它的那个车站能发。不做全公司共享池、不做车队专线池。 |

### 10.3 由上述定案新产生的实现项（尚未取证/实现，登记在此）

| 项 | 说明 | 待办 |
| --- | --- | --- |
| P-1 条件变量缺项 | `OrderConditionVariable`（`order_type.h:297-323`）已有 Reliability/MaxSpeed 等，**缺 Power / MaxTractiveEffort**；玩家 C-11 的例子正是"极速 + 最大牵引力" | 新增两个变量（`End` 之前插入，5 位字段上限 31、现用 22，容量够）；复用同一套求值函数 |
| P-2 池条目要带配额 | `issued`（已发几辆）/ `limit`（坑位数）；`issued >= limit` ⇒ 不发 | 池 chunk 字段，随 KI-579 |
| P-3 回环判据 | "跑完一个回环"需落成判据：候选 = `cur_real_order_index` 回绕（最后一条 → 0）那一刻 | 取证 `IncrementRealOrderIndex`（`vehicle_base.h`）回绕点，再定安放位置 |
| P-4 条件宿主 ≠ 列车 | 分配条件存在**车站池条目**上，不由列车携带 ⇒ 列车侧只需要"到站申请 + 交回"，不需要存条件 | 与 KI-581 的 `R3RProjectedOF_WAIT_DISPATCH` 位配合 |
| P-5 一期仍可先做 | 功能一（排程级自动调节）不依赖 P-1~P-4，仍按 §9.2 一行门禁落地 | 一期零存档格式变更 |

---

## 十三、第 414 轮收口：按钮点不动的真因是"handler 放错了 switch"

### 13.1 结论（一行）

第 412 轮把 `case WID_O_R3R_SEPARATION:` 写进了 **`OnDropdownSelect()`**（`order_gui.cpp:4346`，紧邻 `WID_O_R3R_YARD` 的下拉分支），而该控件是 `WWT_TEXTBTN`，点击只走 **`OrderWindow::OnClick()`**（`order_gui.cpp:3473` 起的 `switch (widget)`）。`OnClick` 里没有这个 case ⇒ 落到 `default` 静默返回 ⇒ 命令从未 `Command<Commands::ModifyOrder>::Post()` ⇒ 表现为"按不下去、无提示、无回显变化"。

### 13.2 为什么 §12.2 的三个候选全落空

C-1/C-2/C-3 全都假设"点击已经进入 `OnClick`"，只是后续某一层出问题（命中区 / 互斥拒绝 / 回显刷新）。真实故障点在这三层**之前**的分发入口，因此：

- 加探针（`SEP-BTN-CLICK` 等）可以定位它，但**不必**——静态一读就能看到 case 挂在错误的 handler 上，且 `OnDropdownSelect` 对纯按钮永远不可能被调用。
- 教训（写入 KI-585）：排查"按钮没反应"要按 `控件树 → OnClick/OnDropdownSelect 归属 → 命令 Post → 预检 → 执行 → 回显` 的完整顺序走，**分发归属必须列为检查面**；只看控件在不在显示 plane 里是不够的。

### 13.3 落地（仅 `src\order_gui.cpp`，未碰 `src\*.h`、未加语言串）

1. `OnClick()` 新增 `case WID_O_R3R_SEPARATION:`（现 `:3763-3772`）：取当前选中订单，判 `nullptr` 与 `OT_GOTO_STATION`，然后 `this->ModifyOrder(sel, MOF_R3R_STATION_SEPARATION, order->HasR3RStationSeparation() ? 0 : 1)`。范式与 `MOF_COUNTER_VALUE`（`:4217`）、`MOF_DECOUPLE_BOUNDARY`（`:4247`）一致（成员版签名 `:1979-1982`）。
2. 删除 `OnDropdownSelect()` 里那份死代码 case（避免下次再被误读成"已经实现了"）。

### 13.4 复测判据

1. 列车车站订单点「调节班次」⇒ 立即凹下；再点 ⇒ 弹起。
2. 切到别的订单再切回来 ⇒ 仍保持凹（标记在订单上，不是窗口临时状态）。
3. 同表其它车站订单不被这一次点击改动（标记逐条设，但均分按整表生效）。
4. 若该车启用了原生班次调度 / Unbunching ⇒ 应弹互斥错误（预期拒绝，不是本 bug）。

### 13.5 仍遗留（低，未修）

按钮与"目的地场"下拉平分 60px 槽位（各 30px），中文四字「调节班次」在 30px 内必然裁字（与 KI-262 的"顶行 5×60=300px 不得撑宽"冲突）。正解是移出 `WID_O_SEL_TOP_YARD`（第二行管理区或独立槽），不是继续压缩。本轮只保证可点击，宽度不动；若玩家复测后抱怨文字看不清，再按此方向改。

### 13.6 构建自证

增量构建入口复用 `R3R_fullbuild.cmd inc detach`（未新建任何 `.cmd`）：

| 项 | 实测 |
| --- | --- |
| 护栏 | `build\R3R_incbuild.guard.log` 末行 = `GUARD: incremental is safe (no header/lang file is newer than the newest object)` |
| 步数/链接 | 日志含 `[3/3] Linking CXX executable openttd.exe`（Linking 1 次） |
| 错误 | `error C*` / `fatal error` / `FAILED:` / `build stopped` 命中 0 条 |
| 判决 | `build\R3R_incbuild.done` = `EXIT_CODE=0`，2026-11-10 02:46:57 |
| 时间戳链 | `src\order_gui.cpp` 02:43:25 → `build\CMakeFiles\openttd_lib.dir\src\order_gui.cpp.obj` 02:45:13 → `build\openttd.exe` 02:46:43（52 618 752 B） |
| lint | `read_lints` 0 条 |

本轮未新增日志字面量与语言串，故不做 exe 字面量核对；发布树 `build-release` **未编**（按口径日常只编内测树）。

---

## 十四、功能二 · 二阶段（最小闭环）开工判定与待拍板项（第 415 轮，2026-11-10）

### 14.1 玩家输入与原话

> 「不错，这个按钮正常。我们是否可以进入功能二的二阶段了」

**结论：可以，无阻塞残留。** 一期（功能一：车站订单的排程级均分，KI-575）已闭环 —— 实现完成、`order_gui.cpp` 的 handler 归属修好（KI-585）、实机复测按钮正常。二阶段不依赖任何一期未完成项（§11.4 已核：一期不碰 `r3r_follow_orders` / `r3r_priority` / `orders_backup*`）。

### 14.2 二阶段范围（严格按 §8.4，不加不减）

| 项 | 内容 | 出处 |
| --- | --- | --- |
| ① 订单位「等待分配」 | 列车 `GOTO_STATION` 上 `xflags` bit 5 = `R3ROF_WAIT_DISPATCH`（与一期 bit 5 复用同位置，但**一期已有 `MOF_R3R_STATION_SEPARATION` 也占 bit 5** ⇒ 见 D-0） | §8.3(1)、§8.7 末行 |
| ② 车站池 chunk | 新稀疏块（建议 `R3DP`），键 = `StationID`，值是"池条目数组"：`{OrderList*, issued, limit}`；**不改 `STNN`、不改 `ORDL`** | §8.5 |
| ③ 无条件领取 | 到站停稳 + 站台格 + 该订单带 WAIT_DISPATCH ⇒ 取池里第一条 `issued < limit` 的表，走借用（`r3r_follow_orders` + `orders_backup*`） | §8.3(3)、第 294 轮 KI-465 口径 |
| ④ 无条件归还 | 回环跑完 ⇒ 交回池、`issued--`、清借用、恢复自身表与进度 | §10.2 C-10 |
| ⑤ 池条目进度（缺口 a） | 新增 `orders_backup_timetable_index`（落 `R3VP` 稀疏块，不碰车辆表布局） | §8.3(3) 缺口 a |
| ⑥ 缺口 b | **二期选"禁链"**：池表必须是叶子（不得指向别的表），不做借用栈 | §8.3(3) 缺口 b |
| **不做** | 条件求值/DSL（= 三期）、`R3RSchedule` 实体化 + 三件套退役（= 四期）、全公司共享池、车队专线池 | §8.4、§10.2 C-12 |

### 14.3 开工前必须拍板（D-0~D-6，均附推荐值）

| 编号 | 问题 | 推荐 | 影响面 |
| --- | --- | --- | --- |
| **D-0** | **位冲突**：一期「调节班次」已占 `xflags` bit 5，二期「等待分配」若也用 bit 5 会互相踩 | 二期改用 **bit 4**（现被"某个已占用组"覆盖？需核对 `order_base.h:62-67` 的位图再定，最稳是新增 `MOF_R3R_WAIT_DISPATCH` 并改用独立的**空闲位**，或把两个标记合成"1 位 + 语义由订单类型定"三者选一） | 命令层白名单、GUI 回显、存档 |
| **D-1** | 池存哪 | 新稀疏 chunk `R3DP`（`CH_SPARSE_TABLE`，键 `StationID`），**绝不改 `STNN`** | 存档兼容 |
| **D-2** | 池里的"表"用什么 | 复用现成 `OrderList`（`OrderList::Iterate` 已覆盖"只被引用"的表），池只持指针 + `issued/limit`，不新建实体 | 与四期解耦 |
| **D-3** | 领取时机 | 停稳（`cur_speed==0`）+ `IsRailStationTile(tile)` + 订单是 WAIT_DISPATCH + 站匹配 ⇒ 取第一条有空坑位的表；取不到就原地等（**不得**记 `Stuck`） | 与 KI-347/KI-348 同族约束 |
| **D-4** | 归还判据（P-3） | **已拍板（见 §14.7）："跑完分配表最后一条命令 ⇒ 指针回自身表的下一条"**，不是整轮回环 | 决定是否会早还/漏还 |
| **D-5** | `limit` 默认与上限 | **已拍板：默认 1，上限 256**（字段用 `uint16_t`，勿用 `uint8_t`） | 与 C-9 坑位制一致 |
| **D-6** | 池管理 GUI 放哪 | **已拍板：车站窗口，站场选项旁边**（不放订单窗口，顶行 5×60=300px 已满，KI-262） | GUI 工作量 + 车站窗口宽度 |

> D-0 是唯一有"必须先取证"性质的一项（位图要逐位核对 `order_base.h:62-67`）。其余 6 项给默认值即可开工。

### 14.4 落地顺序（建议，S1→S7）

1. **S1** 取证 `order_base.h:62-67` 位图 + `OrderConditionVariable`/`xflags` 现状，定 D-0 的用位 → 落 KI 条目。
2. **S2** 池 chunk `R3DP`：结构体 + `SlVec`/`CH_SPARSE_TABLE` 注册 + `R3RHasDispatchPool()` + 指针阶段（`OrderList*` 走 `SlObjectPtrOrNullFiltered`），读档后对"只被池引用"的表补 `OrderList::Initialize`（同 KI-169 的 R3VP 做法）。
3. **S3** 订单位 + 命令：`MOF_R3R_WAIT_DISPATCH` 白名单 + 预检（仅列车 + `OT_GOTO_STATION`）+ 执行（置位并 `SetTimetableWindowsDirty`）。
4. **S4** 领取/归还：`TrainDispatchHandler()`（与 `TrainCoupleHandler` 同层，只在停稳分支调）+ 借用（`R3RBindDispatch` / `R3RUnbindDispatch`）+ 缺口 a 的 `orders_backup_timetable_index`（R3VP 新字段）+ 缺口 b 的"池表必须叶子"校验。**归还细节按 §14.7 D-4**：入池表从 `real=0` 起、池表末条完成即还、归还后 `real = 备份索引 + 1`（并 `current_order.Free()` / `SetDestTile(INVALID_TILE)`，否则重触发同一条 WAIT_DISPATCH）。
5. **S5** 复测判据探针（只读）：`DISP-POOL take/return`、`DISP-POOL full station=`、`DISP-POOL skip reason=`。
6. **S6** 车站窗口池管理 GUI（S5 后可独立收尾，不阻塞 1~5）。
7. **S7** 存档往返 + 与耦合/解挂/克隆/卖车的交叉校验（`issued` 计数不得泄漏）。

### 14.5 代价与风险（必须先让玩家知道）

- **必须全量重编**：会动 `src/*.h`（新 chunk id、新 `ModifyOrderFlags`、`Vehicle` 不新增字段但 `R3VP` 加字段）⇒ 本项目依赖表已失效（记忆 66636022），护栏会 `REMOVED 620 obj` 转全量，**约 16~20 分钟**。
- **存档**：新增 chunk ⇒ 老档读入时"池为空"，行为逐字不变（负判据可测）；新档给老 exe 读会被拒（chunk 未知），这是所有 R3R 新 chunk 的既有代价。
- **借用层风险**：二期复用 `r3r_follow_orders` ⇒ 与 KI-04/KI-526/KI-527 同源风险。必须先确认"池的 owner 不是车"这一第三态不会被 `R3RRebuildCouplePriorities()` / `R3RPushProgressToOwner()` 误判（§8.7 已论证现有实现无法表达，故二期要给这两处加"池表 owner"分支，**这是二期最容易埋坑的一处**）。
- **不碰条件系统**：二期"无条件领取"= 谁先到谁先领；条件（极速/牵引力）留三期，但 **D-0 与 P-1 的变量缺项（Power/MaxTractiveEffort）可在二期顺手补结构位**，避免三期再动一次存档。

### 14.6 复测判据（二期交付时用）

1. 一列车站订单打 WAIT_DISPATCH ⇒ 到站后出现 `DISP-POOL take`，列车改跑池表的第 0 条（不是自身的第 0 条）。
2. 池 `limit=1` 时两列车先后到站 ⇒ 第一列领走，第二列 `DISP-POOL full` 且原地等，**不得**显示"卡住"、不得乱开。
3. 第一列跑完回环 ⇒ `DISP-POOL return`，坑位释放，第二列随即领到。
4. 归还后列车自身表 `real == 分配点索引 + 1`（例：3→4），`implicit` / `cur_timetable_order_index` 按备份恢复后同样前推一条（缺口 a 生效）；**不是**停在 3（会重触发）、也**不是**回 0。
5. 负判据：没打 WAIT_DISPATCH 的订单、没建池的车站 ⇒ 行为与本期 exe 逐字不变（日志无 `DISP-POOL`）。
6. 存档往返：池条目、`issued`、被领走的借用关系全部保持；且 `issued` 数 == 实际持有该表的活车数（无泄漏）。

### 14.7 玩家拍板（2026-11-10，原话保留）

> 「坑位默认1，上限256（最好不要太限制玩家，我只是觉得256刚刚好）」
> 「池管理界面放车站窗口，站场选项的旁边」
> 「比如说，列车执行了自己的命令3，然后因为命令3分配了一个新的表，于是它开始从idx=0执行分配的表，然后跑完分配的表的最后一条命令，再把命令指回自己表的命令4。我的意思就这样」

**D-5 坑位**：默认 **1**，可填 **1~256**。⇒ 池条目字段 `limit` / `issued` 一律 `uint16_t`（`uint8_t` 装不下 256）；`limit==0` 不解释成"不限"，按非法值钳到 1（避免"不限"在后面对账时破坏 `issued<=limit` 不变式）。
**D-6 界面位置**：车站窗口、**站场选项旁边**。⇒ S6 时以 `station_gui.cpp` 的 `_nested_station_yard_widgets` 那一行为锚点新增控件；**必须同时核对车站窗口最小宽度**（该行已有 shared/workshop/locate/rename/delete + resizebox，再塞按钮会撑宽，做法同 KI-262：新增前先量 total_min_width，超了就改成第二行或下拉入口）。
**D-4 归还语义（本轮最重要的口径，替换 §10.2 C-10 的"跑完一个回环"表述）**：

```text
列车自身表:      [1] [2] [3: GOTO_STATION + WAIT_DISPATCH] [4] [5] ...
                                  │ 到站停稳
                                  ├─ 领到池表 P（issued: 0→1）
                                  ▼
              自身表 real 停在 3（备进 orders_backup_real_index=3）
              实际执行 池表 P：[P0] [P1] ... [Pn-1]，从 P0 开始（cur_real=0）
                                  │ Pn-1 执行完
                                  ▼
              归还：issued 1→0，撤借用，自身表 real ← 4（= 分配点索引 + 1）
```

要点四条：
1. **分配点 = 自身表里那条带 WAIT_DISPATCH 的 `OT_GOTO_STATION` 的索引**（例中的 3），不是"到站"这个事件本身。
2. **进入池表从 `real = 0` 开始**（不是接着自身表进度、也不是池表上次被跑到哪）。池表若要"接着上次的进度"，那是三期/四期的"池条目进度"话题，二期**每次领取都从 0**。
3. **归还触发 = 池表最后一条命令执行完**。判定的**机制落点**是"池表 `real` 即将从 `num-1` 推进出去的那一瞬"（引擎内部正是 `IncrementRealOrderIndex` 的回绕点，`vehicle_base.h`），在那里**拦截并归还**，不让它真的从 0 重跑一整圈。⇒ 与 §10.2 C-10 的"跑完一个回环（整圈回到 0 再还）"不同：那个要等一圈，这个在末条结束即还；也不是"回到放表车站"。
4. **归还后指针落点 = 自身表 `备份索引 + 1`**（例中的 4），且**必须跳过占用判定**（`current_order.Free()` + `SetDestTile(INVALID_TILE)` + 索引重置），否则会重触发同一条 WAIT_DISPATCH 形成死循环（同 §10.2 硬伤 4 的机制）。

⇒ 对实现的影响：`orders_backup_real_index` 存的正是"分配点 3"，归还时写 `3+1=4`，语义与现有借用层天然对齐（第 294 轮 KI-465 口径不变）；`cur_timetable_order_index` 的停放副本（缺口 a）同理，归还时按备份恢复而不是回 0。

**池条目数上限** → 改为**设置项**（见 §14.8）。

### 14.8 第 416 轮开工口径（2026-11-10，玩家拍板 + 实现层定案）

> 玩家原话：「单个车站单个站场的池条目数上限如果不对性能造成太大影响，建议设置里玩家自己调，最大值可以激进一点，256 / 然后，其它的请你全量重编」

**E-1 容量设置项**：`_settings_game.order.r3r_dispatch_pool_capacity`，`SLE_UINT16`，默认 **256**，范围 **1~256**。语义 = **一个车站的一个场最多能登记多少份「供分配的调度计划」**。注意与 D-5 的「坑位（limit）」区分：
- **容量 capacity** = 池里能放多少**条目**（表），全局设置，防 UI/存档膨胀；
- **坑位 limit** = 一条条目能同时发给多少**列车**（1~256/条）。
性能：容量只影响读取/UI 遍历上限，判定是 O(条目数)，256 条目 * 每 tick 一次领用尝试可忽略 ⇒ 不构成性能问题。

**E-2 池条目的载体 = 「调度模板车」(host) + 配额**，不是独立 `OrderList`。这是本轮最重要的实现层定案，理由（取证见 §14.8.1）：
- `OrderList::first_shared` 是 `Vehicle*`，全库有 9 处无条件解引用 `GetFirstSharedVehicle()->`（`order_cmd.cpp:742/743/925/953` 的 `GetType()/GetOwner()`、`departures_gui.cpp:325/464`、`waypoint_sl.cpp:144`、`station_sl.cpp:62`、`afterload.cpp:4402`）。造一张"无主表"会让这 9 处任意一处空指针崩溃（KI-169a 已经踩过一次同族问题，当时的修法就是给无主表 `OrderList::Initialize(v)`）。
- 让池条目**引用一辆活车的自身表**，`first_shared` 恒非空 ⇒ 零第三态、零 `OrderList` 生命周期管理、借用层（`r3r_follow_orders`/`r3r_follow_owner`/ORD-PUSH）完全复用。
- 存档只需 `VehicleID` + 两个 `uint16_t` ⇒ 塞进既有 **SYRD** word 流（升 v6），**不新增 chunk、不动 `STNN`/`ORDL`**。
- 语义：池条目 = 「本场把 T 号车的这张排程当作可分配的班次」，T 车就是模板（不应自己再跑同一张表）。

**E-3 领取/归还（与 §14.7 D-4 一致）**：
- 领取：自身表当前订单 `OT_GOTO_STATION` + `WAIT_DISPATCH` + 站/场匹配 + 停稳 ⇒ `R3RParkOwnOrderProgress(v)`（存备份索引）→ `r3r_follow_orders = host->orders`、`r3r_follow_owner = host`、`r3r_orders_borrowed = true`、**新增 `r3r_follow_pool = true`**（落 R3VP）→ 三个活索引 **全部置 0**（每次领取从 0 开始，不接池表上次进度）。
- 归还：池表 `cur_real_order_index` 从 `num-1` **回绕到 0 的那一瞬**（引擎 `IncrementRealOrderIndex`）⇒ 拦截：`issued--`、清借用三字段、`cur_real = 备份索引 + 1`（同理 implicit/timetable 按备份 +1）。
- **进度回写必须排除池借用**：`R3RPushProgressToOwner()` 遇到 `chain->r3r_follow_pool` 直接 return，否则会把借用者进度写进模板车（KI-215b 同源风险）。

**E-4 入口**：车站窗口新增「调度池」页（S6，站场选项旁边），「添加」= 下拉列出**订单中含本站的列车**（`Vehicle::Iterate` + `IsPrimaryVehicle` + Train + 扫表），选中即把该车的表登记进当前场池；另有「删除」「坑位 -/+」。

**E-5 已确认的可复用事实**（本轮取证，供后续轮次直接引用，不必重查）：
- `Save_ORDL`（`src/sl/order_sl.cpp:569`）遍历 `OrderList::Iterate()` ⇒ **全池**都落盘，包含只被 `orders_backup`/池引用的表。
- `OrderList::Create(std::move(orders), v)` 的第二参决定 `first_shared`（`vehicle.cpp:4893`、`couple_group.cpp:1404`）。
- SYRD 的载体是 `std::vector<TileIndex>` 的 32 位 word 流（`src/sl/station_yard_sl.cpp`，v5 MAGIC `0xFFFFFF04`），加字段只需升 MAGIC 并保持向后可读。

