# 第 376 轮：取消自动填充命令，改手动填充（保留"占位不执行"）

日期：2026-10-30　状态：**待玩家拍板，未改源码、未构建**

---

## 0. 玩家原话（2026-10-30）

> 根据我的观察，目前容量这边暂时没有问题了。但是，我想取消自动填充命令这一东西，改为手动填充，也就是不再自动填充，但是保留这种不会被执行，仅用作占位的调度命令，你可以跟我论证或询问细节

拆成三条要求：

- **R1**：KI-540（refit 容量 1/3）实机复测通过 —— 第 375 轮方案 1 有效。
- **R2**：**取消自动填充**。不要再自动往段自己的表里塞命令。
- **R3**：**改为手动填充**，但**保留**"不会被执行、仅用作占位"这种调度命令。

---

## 1. 现状取证（行号锚点，`src\train_cmd.cpp` / `src\couple_group.h`）

### 1.1 自动填充在哪写

- `R3RAutoRecordChainOrders(Train *chain)` 定义在 `src\train_cmd.cpp:7025`。
- 唯一调用点：`TrainLocoHandler()` 入口 `src\train_cmd.cpp:22402`（每 tick、每条前端车一次，函数内部按链头 tile 节流）。
- 注释原文（`:22400-22401`）：「非控制段自己不会 tick，它"经过"的车站/路点/车库只能在这里顺手记下来」。

⇒ 自动填充 = **每 tick 采样链头脚下踩过的目的地，追加进每个非控制段自己的订单表**。

### 1.2 区间记在哪

`src\couple_group.h:586-588`（段行 `R3RSegmentRecord`，KI-395 起 3 个槽）：

```cpp
uint16_t auto_ranges_begin[AUTO_RANGE_SLOTS] = {};      // 每个自动区间的首条订单
uint16_t auto_ranges_count[AUTO_RANGE_SLOTS] = {};      // 该区间的条数；0 = 空槽
uint32_t auto_ranges_signature[AUTO_RANGE_SLOTS] = {};  // 写入时的内容哈希，用来区分"我们写的"与"玩家手改的"
```

另有 KI-388 遗留的单区间 `auto_begin/auto_count/auto_signature`（`:593-595`），只供旧档迁移。

### 1.3 "不执行"靠什么

`R3RSkipAutoRecordedOrders(Train *v)`，`src\train_cmd.cpp:7337`：

- 前置：`own != nullptr && v->GetEffectiveOrders() == own`（只有在跑**自己的**表时才管）。
- 取 `slot = R3RAutoRangeFind(row, idx)`；索引落在区间内 ⇒ `cur_real_order_index = begin + count`（`:7385`），顺带修正 `cur_implicit_order_index` 与时刻表索引（KI-402，`:7396` 起）。
- **关键第 4 条**：`:7362-7373` —— 若 `auto_ranges_signature[slot] == 0` 或 `R3RAutoOrderHash(own, begin, count) != signature`，**直接丢弃该区间**（打 `AUTO-ORD-STALE`），跳过后面的跳过逻辑。

⇒ 现状的"不执行"认证是 **位置（区间）+ 内容哈希**，本质是"我认得自己刚写的那几条"。

### 1.4 玩家痛点的准确来源

第 1.3 节第 4 条 + KI-503 的设计意图是"保护玩家手改的命令不被误跳"。副作用是：

- 自动条目被玩家**编辑过一次** ⇒ 保护立刻失效，那几条**会真的被执行**。
- 而在编辑之前，它们**不会**被执行。

⇒ 同一个位置的命令，行为取决于"玩家动没动过"，这本身就是不稳定的语义。取消自动填充后，这个矛盾自然消失，但"占位不执行"的认证也失去了依据（见 §2）。

---

## 2. 核心矛盾：取消自动填充后，"占位"靠什么认

自动填充把三件事绑在一起：

1. **写入**（谁把命令放进表）
2. **标记**（`auto_ranges` 记住哪几条是自动写的）
3. **执行豁免**（按标记跳过）

取消 1 之后，2 就没有生产者了，3 也就没有依据。所以必须把"认证"从**位置+哈希**换成**显式声明**。

三种粒度（这是需要玩家拍板的核心）：

### 方案 X：订单级标志（逐条可标）

- 新增 `MOF_R3R_PLACEHOLDER`（`src\order_type.h:353-385` 的 `ModifyOrderFlags`，uint8_t 还有空位，末项 `MOF_END` 在 `:384`）+ `Order` 里 1 bit（例如复用 `Order` 现有 R3R 位段）。
- 执行侧：`R3RSkipAutoRecordedOrders()` 改判"这条订单有没有占位位"，有就前进一条。
- **优点**：最灵活，玩家可以"表里前 3 条照跑、后 2 条只占位"；UI 可加单条开关。
- **缺点**：**要改 `Order` 的存档布局**（`src\sl\order_sl.cpp` 的订单字段 + 存档版本），`Order` 是全局结构、所有订单类型共享 —— 这是三种方案里唯一会碰存档格式的。

### 方案 Y：段级开关（整表占位）

- 段行加 `bool orders_placeholder_only`（`src\couple_group.h` 的 `R3RSegmentRecord`）。
- 执行侧：该段为占位模式时，**整张自己的表都不驱动列车**（更好的落点不是"跳过"，而是 `R3RSettleChainSegments` 选"由谁驱动"时就不选它，索引层面根本不进这张表）。
- **优点**：只动 `R3SG` 段行块（本来就有版本管理，加 1 字节比改 `Order` 安全得多）；语义干净——"这一段的表只作备忘"；顺带把 `auto_ranges` 三个数组整体废弃，代码净减少。
- **缺点**：整表一刀切，不能"部分执行部分占位"。

### 方案 Z：保留区间机制，只把"自动写"改成"手动触发"

- 把 `R3RAutoRecordChainOrders(consist)` 从 `TrainLocoHandler` 挪到一个按钮/命令上，执行侧一行不改。
- **优点**：改动最小。
- **缺点**：**没解决痛点** —— 认证仍然绑在区间 + 哈希上，玩家一编辑保护就失效（§1.4）。只推荐作为 X/Y 的附属"填充辅助"存在，不能单独作为答案。

**我的推荐**：**Y 为骨架 + X 作为可选增强**。理由：

1. 玩家说的"保留这种不会被执行、仅用作占位的调度命令"读起来是**这一类命令**的身份，不是"这一条"——段级开关最小且语义最正。
2. `Order` 是全局热结构，为了一个 R3R 占位位去改存档格式，收益/风险不划算；段行加字节风险低一个量级。
3. 取消自动填充后 `auto_ranges`（3 槽 × 3 字段 + 哈希计算 `R3RAutoOrderHash` + 迁移代码 KI-388）全部可以删，净收益明显。

---

## 3. 待玩家拍板（编号回填，D-x）

- **D-1 粒度**：整段一刀切（方案 Y）／逐条标记（方案 X）／两者都要（Y 打底、X 可选）？
- **D-2 "手动填充"具体指什么**：
  - (a) 玩家用**现有订单窗口**逐条添加，R3R 不再插手 —— 那"占位"只是给这张表打个段级标记；
  - (b) 另外要一个**"按当前实际路线填充"的按钮**（等于把今天的自动填充变成按一下填一次）；
  - (c) 两者都要。
- **D-3 占位表现在还能不能让列车跑**：占位模式下，这张表是"完全无法驱动列车"（即使玩家想跑也不跑），还是"只有主动下命令才能跑"？
- **D-4 UI 表现**：占位命令在订单窗口里是否要灰字 + 后缀（如「（占位·不执行）」）？段级开关放在哪（订单窗口顶部 / 车队列表段行 / 段设置）？
- **D-5 存量数据**：旧档里已有的 `auto_ranges` 记录，读档时①转换成本段占位模式②原样保留一次③直接清除（表里的命令留下但不再享受豁免）？

---

## 3.1 玩家答复（2026-10-30，原话）

> 我想法是：给订单添加一种 ordertype（暂定叫挂接结束），或者给每一个前往挂接命令后面自动搭配一个挂接结束命令。效果是挂接结束命令到回环里面的上一个前往挂接命令之间的所有命令均算作填充凑数的命令，也就是空气。然后，占位命令要灰字。旧档直接把自动填充的命令直接清除（不知是降级还是删除，删除最好）希望这些能够回答你的问题

D-1..D-5 对应答案：

| 编号 | 玩家答复 | 结论 |
|---|---|---|
| **D-1 粒度** | 「挂接结束命令到回环里的上一个前往挂接命令之间的**所有命令**」 | **不用 X（订单 1 bit），不用 Y（段级开关）**。区间由**订单语义**界定 ⇒ 第四种方案，记为 **方案 W** |
| **D-2 手动填充** | 「给每一个前往挂接命令后面自动搭配一个挂接结束命令」 | 玩家手动往 `GOTO_COUPLE` 与 `挂接结束` 之间插命令；R3R 不再自动塞站名，只自动保证那条分隔符存在 |
| **D-3 占位表能否驱动** | 区间内「均算作填充凑数的命令，也就是空气」 | **彻底不执行**，且不限类型（站/路点/库/等待/解挂只要是区间内都算空气） |
| **D-4 UI** | 「占位命令要灰字」 | 空气区间内全部灰字（含 `挂接结束` 自身） |
| **D-5 旧档存量** | 「直接清除（不知是降级还是删除，**删除最好**）」 | 读档时按 `auto_ranges` + 哈希校验**真删除**那些订单，然后清空数组；不做降级保留 |

## 3.2 方案 W 的规则化定义（待实现口径）

### 3.2.1 新增订单类型

`OT_COUPLE_END`（中文「挂接结束」，英文暂定 `Couple end`）。纯标记，无目的地、无动作。

### 3.2.2 空气区间的判定（唯一权威定义）

对订单表中的**每一条 `OT_COUPLE_END`**：

1. 沿 **Previous 方向（模回环）** 往回扫，找最近的 `OT_GOTO_COUPLE`。
2. 若途中**先遇到另一条 `OT_COUPLE_END`** ⇒ 本条判定为**孤儿**，不产生区间（且自身仍按"标记"处理：灰字、不执行，但不吞别人）。
3. 否则区间 = `(GOTO_COUPLE, 本条 COUPLE_END]`，**模回环取值**（跨 0 索引时分成两段）。

**为什么需要第 2 条孤儿规则**：表里若只有一条 `GOTO_COUPLE`，其余 `COUPLE_END` 往回扫会绕一整圈咬上它，把整张表都判成空气。孤儿规则把这种绕圈挡掉。

**为什么用"往回扫"而不是"往前扫"**：与玩家原话一致（"回环里面的上一个"），且能天然处理玩家把命令插在 `GOTO_COUPLE` 前面的情况（那种情形不属于任何区间）。

### 3.2.3 执行侧

- 索引落在空气区间 ⇒ 跳过整段，落到 `COUPLE_END` 的下一条（`COUPLE_END` 自身也跳过）。
- 取代 `R3RSkipAutoRecordedOrders()`（`train_cmd.cpp:7337`）的整段哈希逻辑；`AUTO-ORD-STALE`（KI-503）随之消失——**再没有"玩家改过就失效"这件事**，语义变成纯粹的"位置决定"。

### 3.2.4 自动补齐 `COUPLE_END`

`R3REnsureCoupleEndOrders(OrderList *)`：扫全表，凡 `OT_GOTO_COUPLE` 的下一条不是 `OT_COUPLE_END` ⇒ 就地插入一条。调用点覆盖：读档、`R3RSettleChainSegments` 各提交点、订单增删改命令之后。

### 3.2.5 旧档清除（玩家口径：删除）

读档时对每条带 `auto_ranges` 的段行：哈希匹配（`R3RAutoOrderHash` 与 `auto_ranges_signature` 相等）才 `DeleteOrder` 那一段；不匹配说明玩家手改过，保留。删完把 `auto_ranges_begin/count/signature` 全清零。随后跑 `R3REnsureCoupleEndOrders` 补分隔符。

### 3.2.6 可以整体删除的东西

`R3RAutoRecordChainOrders()`（`:7025` 起约 300 行）、`R3RAutoOrderOnControlLeg()`（`:6971`）、`R3RAutoRecord*` 系列、`R3RAutoOrderHash`、`R3RAutoRangeFind`、`AUTO_RANGE_SLOTS`、`R3RSkipAutoRecordedOrders()`、段行的 `auto_ranges_*` 九字段（`couple_group.h:586-595`，含 KI-388 遗留三字段）。**净删代码量目测 400~500 行**。

### 3.2.7 存档影响

- `Order` 结构布局**不变**（`OrderType` 是 `uint8_t` 字段，新值不增宽度）⇒ 不改 `order_sl.cpp` 的字段表。
- 但新值在**旧 exe** 上会落到 `default` 分支（未知类型）⇒ 需在 `order_sl.cpp` 或读档后加兼容处理；新 exe 读旧档不受影响（旧档里没有该值）。
- 段行 `auto_ranges_*` 删除 ⇒ `R3SG` 块要么升版本，要么保留字段但不再使用。**倾向保留字段不读写**（零存档风险），只把逻辑删掉。

## 3.3 仍需玩家确认的两点（R-1/R-2，已给默认值）

- **R-1（默认按「是」执行）**：空气区间内**一律**空气，包括 `OT_WAIT_COUPLE`、`OT_DECOUPLE`、`OT_GOTO_DEPOT` —— 只认位置不认类型。理由：玩家原话是「所有命令」。**风险**：若某条排程把 `WAIT_COUPLE` 放在 `GOTO_COUPLE` 与 `COUPLE_END` 之间，它会被吞掉（但自动补齐逻辑会把 `COUPLE_END` 紧贴在 `GOTO_COUPLE` 后面，所以默认情况下区间是空的，只有玩家主动插命令才会产生空气）。
- **R-2（默认按「补」执行）**：缺 `COUPLE_END` 时自动补在 `GOTO_COUPLE` 的下一条，而不是"没标记就不跳过"。理由：玩家的语义依赖这条分隔符存在，缺了就退化成"全部真执行"，不安全。

## 4. 影响面预判（落地前先摆清楚）

- 删自动填充 ⇒ `R3RAutoRecordChainOrders()`（`:7025` 起约 300 行）+ `R3RAutoOrderOnControlLeg`（`:6971`）+ `R3RAutoOrderHash` + `R3RAutoRangeFind` + `AUTO_RANGE_SLOTS` 相关全部退场。
- `R3RSkipAutoRecordedOrders()`（`:7337`）按 D-1 改写判据（或整体删除，若改为"选表阶段就不选占位表"）。
- `R3RSegmentRecord` 加字段 ⇒ `R3SG` 段行块可能要升版本（`src\sl\couple_group_sl.cpp`）。
- 订单窗口 / 时间表窗口的灰字渲染点需要同步（KI-400 相关 UI 点）。
- **不碰** `Order` 存档格式（除非 D-1 选 X）。

---

## 5. 未做

本轮零源码改动、零构建。等 D-1..D-5 答复后按选定方案落地。

---

## 6. 方案 W 落地（实现轮，2026-10-30）

按 D-1..D-5 拍板结果落地「新增 `OT_COUPLE_END` + 区间空气化」。玩家未再提新口径，全部按 §3 定义执行。

### 6.1 落点清单

| 层 | 文件 | 内容 |
|---|---|---|
| 枚举 | `src\order_type.h` | `OT_COUPLE_END = 18`（`OT_END` 变 19）；`GetOrderTypeName()` 的名字表同步补到 19 项（`order_cmd.cpp:5324`，`static_assert(lengthof(names) == OT_END)` 是硬闸门） |
| 订单语义 | `src\order_base.h` | `Order::MakeCoupleEnd()`；`HasNoTimetableTimes()` 纳入 `OT_COUPLE_END`（结束标记没有旅行/等待时间，`IsCompletelyTimetabled()` 因此恒真）；`R3RIsCoupleAirOrder(list, index)` 为区间判定的**唯一权威实现** |
| 区间判定 | `src\order_base.h` | 对每条 `OT_COUPLE_END` 沿 Previous 方向模回环回扫找最近的 `OT_GOTO_COUPLE`，先遇到另一条 `COUPLE_END` 即判孤儿（不产生区间）；区间 = `(GOTO_COUPLE, 本条 COUPLE_END]` |
| 自动补齐 | `src\order_cmd.cpp` | `R3REnsureCoupleEndOrders(Vehicle *)`；`InsertOrder()` 插 `GOTO_COUPLE` 时自动追加标记；读档（`src\sl\vehicle_sl.cpp`）与删/移订单后各调一次 |
| 执行侧 | `src\train_cmd.cpp` | `R3RSkipCoupleAirRanges()` 取代旧的哈希式豁免：索引落进空气区间即跳到 `COUPLE_END` 的下一条，`COUPLE_END` 自身也跳过；`AUTO-ORD-STALE` 与 `auto_ranges_*` 九字段全部退场 |
| 时刻表 | `src\order_cmd.cpp` / `src\timetable_gui.cpp` | `IsCompleteTimetable()` 跳过空气项；`FillTimetableTable()` 对空气项放行「缺时间」但仍累加显式填写的时间；`BuildTimetableWarnings()` 不再为空气项报缺时刻表/停站类型；`no_set_waiting_orders` 跳过空气项 ⇒ 占位上的等待时间不能充当自动间隔所需的真实停站等待 |
| 订单窗口 | `src\order_gui.cpp` | 空气区间内灰字 + `STR_ORDER_R3R_AUTO_FILLED` 后缀；`OT_COUPLE_END` 增显示分支（`STR_ORDER_R3R_COUPLE_END`）；该行的控件状态切到 `DP_ROW_EMPTY` / `DP_GROUNDVEHICLE_ROW_EMPTY` 并禁用 `WID_O_NON_STOP` / `FULL_LOAD` / `UNLOAD` / `REFIT` / `REFIT_DROPDOWN`，防止把标记行改成命令 |
| 语言 | `src\lang\{english,simplified_chinese}.txt` | `STR_ORDER_R3R_COUPLE_END`（`（参考结束）`） |

### 6.2 本节修订：§3.2.4 的补齐判据（实现期发现的规格自相矛盾）

§3.2.4 原文写「凡 `OT_GOTO_COUPLE` 的**下一条**不是 `OT_COUPLE_END` ⇒ 就地插入一条」。按字面实现会与本方案自身语义冲突：玩家一旦往区间里插命令，标记被挤到第 3 条起，此时"下一条不是标记"成立 ⇒ 补齐逻辑会**再插一条紧贴 `GOTO_COUPLE` 的新标记**，`(新标记, 旧标记]` 全部变回活命令，空气区间永远无法存在。原 §3.3 R-2 的「缺了就退化成全部真执行」担忧由此变成**每次编辑都发生**。

实现改为：**往前扫描**到下一个 `OT_GOTO_COUPLE` 或表尾，先遇到 `COUPLE_END` 即视为已闭合、一字不动；只有整段确实没有标记时才在 `GOTO_COUPLE` 后就地插入（此时区间为空，等价于"该段没有参考命令"）。同时插入位置由 `(i + 1) % num` 改为 `i + 1`（不再绕回表首）。

### 6.3 边界与已知取舍

- **删标记 = 拆区间**：玩家删掉 `COUPLE_END` 后按 §6.2 补在 `GOTO_COUPLE` 后面，原区间内命令重新变活。与"删掉 `GOTO_COUPLE` 也会让区间消失"同向，属可接受语义，未做额外确认弹窗。
- **孤儿标记不追删**：删掉 `GOTO_COUPLE` 后留下的 `COUPLE_END` 显示为"（参考结束）"但无区间可闭，执行侧无影响（只是列表噪声）。未做自动清理，避免删订单时反向调整索引。
- **标记不再追加"(自动填充)"后缀**：D-4 要求标记**灰字**（`R3RIsCoupleAirOrder()` 对标记自身返回真，颜色跟随），但它不是"参考命令"，故后缀只加给区间内的真实命令行。
- **订单 JSON 导出**：`order_enums_to_json.h` 未收录 R3R 三个订单类型（`OT_DECOUPLE`/`OT_GOTO_COUPLE`/`OT_WAIT_COUPLE` 本就缺），`NLOHMANN_JSON_SERIALIZE_ENUM` 对未知值回落到首项 ⇒ 导出为 `null`，不抛异常、不崩溃。属既有缺口，本轮未扩。

### 6.4 构建自证（2026-10-30）

入口＝既有 `R3R_fullbuild.cmd inc detach`（未新建任何 `.cmd`）；护栏因 `src\couple_group.h`(19:17:09)、`src\order_type.h`(18:51:24) 及两个 `lang\*.txt` 新于最新 obj，判 `REMOVED 620 object file(s) - upgrading this build to a FULL rebuild` ⇒ 全量 702 步。

- `build\R3R_incbuild.done` = `EXIT_CODE=0`（20:01:03）
- 日志尾部 `[702/702] Linking CXX executable openttd.exe`，`error C` / `fatal error` / `FAILED:` / `build stopped` 计数 **0**
- `build\openttd.exe` @20:00:45（52 450 304 B）；`order_gui.cpp` 19:33:14 → `order_gui.cpp.obj` 19:54:22 → exe 20:00:45（obj 晚于源、exe 晚于 obj）
- `build\generated\table\strings.h`：`STR_ORDER_R3R_AUTO_FILLED = 0xF1F`、`STR_ORDER_R3R_COUPLE_END = 0xF20`

### 6.5 待玩家实机复测（6 条）

1. 订单窗口插入 `前往挂接` 后，紧跟出现「（参考结束）」行，且区间内命令灰字 +「(自动填充)」后缀。
2. 往区间里插/删命令后**不会**多出第二条「（参考结束）」（§6.2 的核心判据）。
3. 区间内缺时刻表时间不报警告、不阻断到发时间预测；显式填的时间仍计入总时长。
4. 区间内写等待时间不会让自动间隔判定为"已有真实停站等待"。
5. 列车实际运行跳过整段区间（不驶向区间内的目的地）。
6. 载入 KI-541 之前的旧存档：段行 `auto_ranges_*` 键被丢弃、读档后每段自动补出结束标记，`AUTO-ORD-STALE`/`AUTO-ORD-DROP` 日志不再出现。
