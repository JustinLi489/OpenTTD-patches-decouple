# 第 433 轮：挂接分组耦合、段/车厢寿命（max_age）、可靠性与年龄的继承

**日期**：2026-11-13
**状态**：取证中（三条都已落盘，未改码）

---

## 一、玩家报告（原话要点）

1. **挂接分组**：段 A 属于挂接分组 1 和 2，段 B 属于挂接分组 1 和 3；在车库内拖动把它们耦合之后，**两个段都变成了属于 1+2+3**。玩家判定"肯定不对"，但正确口径自己也记不清了，要我给结论。
2. **纯车底的段寿命显示 0**（玩家记得"实际为上限 65535"），但玩家隐约记得**纯车厢也是有使用寿命的**。
3. **执行挂接和解挂时，机车的使用寿命会从 30 年变成 0 年**。

**总要求（本轮硬指标）**：彻底查清耦合（couple）与解耦（decouple）的**年龄（age）、年限（max_age）、可靠性（reliability / reliability_spd_dec）**继承；**每个段乃至每个车厢的这些数据，不得因任何 `ArrangeTrains()` 而改变**。

---

## 二、初步取证锚点（已知事实）

### 2.1 寿命字段的真实分布

`max_age` 全仓赋值点（`src/**/*.cpp`）：

| 位置 | 赋值 | 说明 |
|---|---|---|
| `train_cmd.cpp:3418` | `v->max_age = e->GetLifeLengthInDays()` | `CmdBuildRailVehicle`（机车/动车） |
| `train_cmd.cpp:24683` | `v->max_age = e->GetLifeLengthInDays()` | 另一处建造/替换路径（待确认） |
| `roadveh_cmd.cpp:323` / `ship_cmd.cpp:1109` / `aircraft_cmd.cpp:349` | 同上 | 其它载具 |
| `articulated_vehicles.cpp:491` | `v->max_age = CalTime::DateDelta{0}` | **铰接部件显式清零** |

**关键**：`CmdBuildRailWagon`（车厢建造）**不在**上述列表里 ⇒ 车厢的 `max_age` 保持默认值（0）。`vehicle_gui.cpp:132` 的既有注释已自陈此事：「*Wagons have no life length (max_age stays 0), and R3R's fake engines …*」，`vehicle_gui.cpp:136` 用 `if (u->max_age.base() == 0) continue;` 主动跳过它们。

⇒ **报告 2 的"纯车底的段寿命 = 0"就是 `max_age == 0`**，且这不是 R3R 引入的，而是上下游一致的既有形态：假引擎（`MakeConsist` 给的 `FrontEngine`）原型仍是 Wagon，`max_age` 从未被赋值。

### 2.2 可靠性/寿命的"搬运"嫌疑点

`CopyVehicleConfigAndStatistics` 只有 3 个真实调用点：`autoreplace_cmd.cpp:605`、`train_cmd.cpp:4524`、`train_cmd.cpp:4789`（后两个在车库编辑路径）。定义在 `vehicle_base.h:1004`，内部会调 `BaseConsist::CopyConsistPropertiesFrom`。
R3R 在两处注释里自陈过这个函数会搬内容（`train_cmd.cpp:11684`、`11708`、`11734` 的 `to->CopyVehicleConfigAndStatistics(from);`）。
⇒ **报告 3（机车寿命 30→0）的头号嫌疑**：某条身份迁移/链头迁移路径把"假引擎（Wagon，max_age=0）"的统计量搬到了机车头上，或反之。

---

## 三、猜测根因（待验证）

- **G-1（挂接分组并集）**：`R3RNormaliseChainGroups` 的历史实现是"取控制段掩码 → 清全链各段头 → 重填"，即**覆盖**；现在观察到 1+2+3 的**并集**，说明要么（a）覆盖方向选的是"合并后的并集"（某轮把 OR 加进去了），要么（b）读侧 `R3RGetEffectiveCoupleGroupsOfSegment` / `R3RAddCoupleGroupAncestors` 把父组与并集展开成看起来像并集，要么（c）车库拖动走的是 `R3RSyncChainAfterDepotEdit` 的另一条归一化分支。**待验证**：读 `couple_group.cpp` 的归一化与读侧展开。
- **G-2（纯车底寿命 0）**：`max_age=0` 是 Wagon 原型的常态（§2.1），本项可能**不是缺陷**而是"车厢本就没有寿命"；玩家记忆中的"纯车厢也有寿命"需对照原生 OpenTTD/JGRPP 的车厢行为确认（原生 wagon 的 `max_age` 是否真的为 0 —— 若原生为 0，则"车厢有寿命"是误会；若原生非 0，则是我方建造路径漏赋值）。
- **G-3（机车寿命 30→0）**：`CopyVehicleConfigAndStatistics`（或 `CopyConsistPropertiesFrom`）在身份迁移时把 `max_age`/`age`/`reliability` 从假引擎（Wagon，全 0）搬到机车。

---

## 四、正确口径（待玩家确认，我给出建议）

### 4.1 挂接分组应该怎样？

历史口径（第 158 轮 KI-256 已列为待拍板项，三选一）：

1. **并集 ABC** —— 把挂接分组当"整列车属性"；
2. **段级保持 AB + AC** —— 把挂接分组当"段的属性"（当前数据模型本来就是按段存、按段读）；
3. 维持"覆盖"（现状旧逻辑）。

**建议选 2（段级保持）**，理由是：数据模型已经是"段头 carrier + 按段读写"（`couple_group.cpp` 的段头 carrier、`R3RSegmentGet/Store` 的段行），选 1 要额外把并集广播到每个段、等于放弃段级语义；选 3 会丢信息（B 的 3 被 A 的 2 覆盖）。**若玩家要"整列车属性"语义则选 1，我按 1 实现（OR 归一化）。**

> 需要玩家一句话拍板：**1 还是 2**。

### 4.2 寿命/可靠性口径

按玩家本轮硬指标：**任何 `ArrangeTrains()`（耦合、解耦、车库拖动、逻辑翻转、克隆）都不得改动任一节车的 `age` / `max_age` / `reliability` / `reliability_spd_dec`**。
唯一例外：`autoreplace` 这种**语义上就是换车**的路径（新车的统计量按新引擎重置），以及原生 `ConsistChanged` 不改这些字段这一事实。

---

## 五、复测判据（草案）

1. 段 A(1+2) + 段 B(1+3) 车库拖动耦合后：按口径 2 应保持 A=1+2、B=1+3；按口径 1 应两段都=1+2+3（现状）。
2. 耦合前后、解耦前后逐节 dump 的 `age`/`max_age`/`reliability`/`reliability_spd_dec` **逐字相同**（除被换掉的引擎）。
3. 机车的寿命显示在挂/解全程不变（不得出现 30→0）。
4. 纯车底段的寿命显示与原生车厢行为一致（若原生为 0，则显示 0 是正确行为，本条转为"确认口径"）。

---

## 六、未确认

- U-1：玩家现场的存档/日志（本轮三条都还没有日志锚点，需玩家提供或指令我读哪个日志）。
- U-2：`max_age` 为 0 的假引擎在 `vehicle_gui.cpp:136` 被跳过排序，是否有其它只读点会因 0 而误判（如 `vehicle.cpp:196` 的 renew 判定 `this->age - this->max_age < 30*months`）。
- U-3：`CalTime::DateDelta` 语义下 `max_age = 0` 与 `65535` 的关系（玩家记忆中提到的"上限 65535"是否指 `uint16_t` 溢出形态）。

---

## 七、玩家澄清（2026-11-13，对报告 2 的定性）

> 玩家原话要点：**"纯车底段的寿命不应该是 0 或者 65535，应该是 15 或者 30 这样的值。"**

⇒ 报告 2 的诉求不是"解释为什么是 0"，而是**要求车厢有真实使用寿命**。G-2 的"车厢本就没有寿命、显示 0 是正确行为"被玩家否掉，按缺陷处理。

### 7.1 取证：车厢确实有寿命，只是建造路径从不写到车上（G-2 成立为新根因）

| 事实 | 锚点 | 结论 |
|---|---|---|
| 车厢引擎在默认表里**有 lifelength** | `src\table\engines.h:130` `MW(1827, 20, 20, 50, CT_PASSENGERS, …)` // Passenger Carriage；`:131` Mail Van、`:132` Coal Truck 同形 | 宏 `MW(a,b,c,d,…)`（`engines.h:52`）第 3 参 = `CalTime::YearDelta{c}` = **lifelength = 20 年** |
| 寿命换算成天 | `engine.cpp:511-515` `Engine::GetLifeLengthInDays()` = `(info.lifelength + extend_vehicle_life) * DAYS_IN_LEAP_YEAR` | 默认 20 年 ≈ 7320 天 |
| 机车的建造路径**有**赋值 | `train_cmd.cpp:3418` `v->max_age = e->GetLifeLengthInDays();` | 机车寿命 30 年（base_life）就是这样来的 |
| 车厢建造路径**没有**赋值 | `train_cmd.cpp:2107-2168` `CmdBuildRailWagon()` 全文：`build_year`（:2154）有、`max_age` 无 | ⇒ 车厢 `max_age` 恒 0 |
| 铰接部件被**显式清零** | `articulated_vehicles.cpp:491` `v->max_age = CalTime::DateDelta{0};` | 铰接车厢同样 0 |
| 双头后节**没有**复制寿命 | `train_cmd.cpp:3348-3352` 只复制 `reliability` / `reliability_spd_dec` / `date_of_last_service` / `build_year`，**缺 `max_age`** | 双头后半 `max_age` 也是 0（原生缺陷） |

### 7.2 结论

**车厢（含假引擎、含铰接部件、含双头后节）的 `max_age = 0` 是"建造时漏赋值"造成的，而引擎数据里本来就有 15/20/30 年这样的寿命值。** 与 R3R 的 `ArrangeTrains()` 无关，但确实让"段寿命"失去意义，故按玩家要求修复。

### 7.3 修法（本轮落地）

1. `CmdBuildRailWagon()`：建造车厢时写入 `v->max_age = e->GetLifeLengthInDays();`（`train_cmd.cpp:2159`）。
2. `AddArticulatedParts()`：**列车**的铰接部件改为跟随自己引擎的寿命；其它载具类型（road/ship/aircraft）的铰接部件仍保持 0（它们的可见性/primary 判定与列车完全不同，不动）（`articulated_vehicles.cpp:495`）。
3. `AddRearEngineToMultiheadedTrain()`：双头后节补 `u->max_age = v->max_age;`（`train_cmd.cpp:3358`）。
4. 老存档迁移（`sl/vehicle_sl.cpp`，`AfterLoadVehiclesPhase2()` 内、`XSLFI_TEMPLATE_REPLACEMENT` 虚拟车清理之前，仅 `part_of_load`）：遍历每条链的每一节，`max_age == 0` 且引擎有效且引擎寿命非 0 时补上引擎寿命；只填 0、不改已有值（幂等）。范围比原计划更宽（不筛 `!IsEngine()`/铰接部件）——因为**只填 0**，对正常车是空操作，对漏赋值的任何一节都有效。命中打 `R3R-STATS-MIGRATE-LOAD units=N`。
5. KI-629 的"不能变"硬指标：在 `ArrangeTrains()` 加**统计量不变式守卫**（进入时快照 `age`/`max_age`/`reliability`/`reliability_spd_dec`，出口核对；发现漂移打 `R3R-STATS-DRIFT` 并还原）。实现见 §7.6。

> **标签前缀更正**：计划里写的 `STATS-DRIFT` / `STATS-MIGRATE-LOAD` 一律加 `R3R-` 前缀。原因是 `r3r_perf.h:322` 的 `R3RDbgTagAllowed()` 白名单只放行 `RESV*/UNRES*/EXTEND*/LOOP*/R3R-RES*/RESERVE*/DECOUPLE*/SPD*/WP*/DUALHEAD*/TTB*/DEPOT*/SEGID*/SEGROW*/LEGACY*/CGRP*/GRP*/R3R-*`，裸 `STATS-` 会被**静默丢弃**（第 274/296/370 轮已三次踩坑，0 条 ≠ 没触发）。日志里的正确串是 `R3R-STATS-DRIFT` 与 `R3R-STATS-MIGRATE-LOAD`。

### 7.4 KI-629 的机制判定（补 §2.2）

`base_consist.cpp:22-76` `CopyConsistPropertiesFrom()` 只搬 `name`/订单/时间表/`vehicle_flags`/`dispatch_records`，**不碰** `age`/`max_age`/`reliability`。`ArriveTraining` 里 `CopyVehicleConfigAndStatistics` 的三个调用点（`train_cmd.cpp:4524/4789`、`autoreplace_cmd.cpp:605`）都不修改这四个字段。

⇒ **代码层面不存在"编排把机车寿命改成 0"的路径**。玩家看到的 30→0，最可能是**显示对象变成了 `max_age = 0` 的车**：
- 假引擎段头（原型 Wagon，`max_age = 0`）——车辆详情窗口 `vehicle_gui.cpp:3790` 直接读 `v->max_age`，于是显示"0 年"；
- 或双头后半节（`AddRearEngineToMultiheadedTrain` 漏复制 `max_age`）。

本轮的 1、2、3 三项修复会把这些 0 全部消除（假引擎 → 车厢寿命 20 年；双头后半 → 跟随机车 30 年），因此"30→0"现象应一并消失；守卫探针用于兜住任何残留路径。

### 7.5 复测判据（替换 §五 的对应条目）

1. 新建车厢（含普通车厢、铰接部件、双头后节）`max_age` 非 0，详情窗口/车队列表显示 15~30 年量级的寿命。
2. 纯车底段（假引擎段头）显示该车厢引擎的寿命，不再显示 0 年。
3. 机车挂/解全程寿命显示恒为原值（30 年），不得出现 0。
4. 日志中 `R3R-STATS-DRIFT` 恒 0 条；若出现，说明还有未知路径改写这四个字段（需读日志定位）。
5. 负判据：车厢仍**不会**被自动更换（`Vehicle::NeedsAutorenewing()` 对非引擎返回 false，见 `vehicle.cpp:198-199`），车库/列表中车厢行为除"寿命/年龄"显示外无变化。

### 7.6 实现落地与守卫细节（第 433 轮，已编译）

**A. 统计量不变式守卫（`train_cmd.cpp`，紧邻 `ArrangeTrains()` 之前定义）**

```cpp
struct R3RStatSnap { int32_t age; int32_t max_age; uint16_t reliability; uint16_t reliability_spd_dec; };
static void R3RSnapshotChainStats(const Train *head, std::map<uint32_t, R3RStatSnap> &out);
static void R3RVerifyChainStats(const char *tag, Train *head_a, Train *head_b,
        const std::map<uint32_t, R3RStatSnap> &before);
```

- 快照键 = `v->index.base()`（不用指针，链搬家后对象地址不变但用 index 与日志口径一致；`std::map` 而非 `unordered_map`，与本 TU 既有探针一致）。
- `ArrangeTrains()` 入口：`if (R3RDbgOn())` 时对 `*src_head`、`*dst_head` 各快照一次（`emplace` 天然去重，两链原来是同一条也不会重复）。
- 出口：`NormaliseDualHeads(*src_head)` / `NormaliseDualHeads(*dst_head)` 之后核对**两个新链头**下的每一节。
- 为什么在 `ArrangeTrains()` 而不是 `ConsistChanged()`：前者是「车在链之间搬家」的唯一入口（couple/decouple/车库拖动/卖车都过它），而 `ConsistChanged()` 本身合法地重算 `cached_*` 且会被大量无关路径调用。
- 漂移处理：打 `R3R-STATS-DRIFT arrange veh= age=a->b maxage=a->b rel=a->b dec=a->b eng=N` 后**当场还原**四字段。还原只在探针开启时生效（整段在 `if (R3RDbgOn())` 语义下），probe-free 版与上游行为逐字一致。
- 边界：只核对「搬动前后仍在这两条链上」的车；被搬到别处的车不追溯（本函数不会把车搬去第三个链，故覆盖完整）。

**B. 读档迁移的取舍**

- 范围收在 `Train::IterateFrontOnly()`：只处理列车，不碰 road/ship/aircraft。
- 只填 `max_age == 0` 的格子，因此对已经正确的新档是纯空操作；`part_of_load` 为假（NewGRF 重载）时不跑，避免在 GRF 重载期改车。
- 引擎寿命 `GetLifeLengthInDays()` 为 0（虚拟/特殊原型）时跳过，不写 0。

**C. 剩余风险**

- `R3R-STATS-DRIFT` 若真的开火，说明存在 `ArrangeTrains()` 之外的写入点（例如 `autoreplace` 的换引擎路径、`Vehicle::HandleAutoreplace`），届时按日志里的 `veh=`/`eng=` 定位即可；本轮只把「编排」这条嫌疑堵死。
- 迁移不区分「玩家故意把 `max_age` 设 0」的畸形档（本作没有这样的合法路径，`max_age = 0` 只可能来自漏赋值）。

### 7.7 构建自证（第 433 轮）

- 入口：`R3R_fullbuild.cmd inc detach`（复用既有脚本，未新建任何 `.cmd`）。
- 第一次链接前编译失败：`src\train_cmd.cpp(3963): error C2440: 转换丢失限定符` —— `const Vehicle *next = u->Next();` 的结果喂给 `Train::From()`，而 `From(const Vehicle*)` 返回 `const Train*`，赋给 `Train *u` 非法。改成 `Vehicle *next = u->Next();`（非 const 链遍历不要先降 const）后通过。
- 第二次：`build\R3R_incbuild.done` = `EXIT_CODE=0`（2026-11-13 22:14:45），日志尾 `[3/3] Linking CXX executable openttd.exe`，无 `error C` / `FAILED:`。
- 产物：`build\openttd.exe` @2026-11-13 22:14:39（52 867 072 B）晚于 `src\train_cmd.cpp`(22:12:36) 与 `src\sl\vehicle_sl.cpp`(22:09:35)。
- `read_lints`：`train_cmd.cpp` / `articulated_vehicles.cpp` / `sl\vehicle_sl.cpp` / `couple_group.cpp` 共 0 条。
- exe 字面量 4/4 命中：`R3R-STATS-DRIFT`、`R3R-STATS-MIGRATE-LOAD`、`CGRP-NORM-KEEP`、`CGRP-NORM-KEEP-LOAD`。
- 状态：**已实现 + 已编译**，游戏内复测待做（判据见 §7.5 与 KI-627/628/629 各条的复测判据）。

