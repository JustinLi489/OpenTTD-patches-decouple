# 第 257 轮：非控制段「区间自动命令」记录（设计论证，不改代码）

状态：**仅论证 + 落盘，本轮不改代码、不构建**（用户 2026-10-20 明确要求「先落盘备忘，再论证，下一轮再改代码」）。

---

## 一、玩家原话

> 我现在要对我那个「班次」的东西进行澄清：我想要一个东西，能够让列车即使自己是非控制段，也能知道自己在非控制期间的时刻表。那就是，在自己是非控制段区间，用自动命令记录区间时刻。我举个例子
>
> A段：a1-a2-c1-等挂-c2-(不耗点的命令)-c3-c4-解挂-a3-a4
> B段：b1-b2-去c1挂A-b3-b4，
>
> 那么，在B跟随A跑一圈之后，B的计划就会变成：
>
> B段：b1-b2-去c1挂A-c2-c3-c4-b3-b4，其中c2-c4这一段是自动命令。
>
> 我的意思是什么呢，就是把B段作为非控制段的时候走过的车站，路点和车库都用自动命令记录下来

另有附带诉求（同一轮一并处理，但下一轮才动代码）：

> R3R版本的时刻表功能有一些问题，目前暴露的体现在自动，分离时刻表和自动填充都按不下去按键，下一轮一并更改

## 二、需求形式化

设两条段 A、B 的订单表（段 = 打 `SegmentFront★` 的区间，见 `train.h` `VehicleRailFlag::SegmentFront`）：

- A：`a1 → a2 → c1 → (WAIT_COUPLE) → c2 → (不耗点命令) → c3 → c4 → (DECOUPLE) → a3 → a4`
- B：`b1 → b2 → (GOTO_COUPLE c1) → b3 → b4`

耦合后链上只有一个「控制段」（`R3RGetControlSegment()` / `r3r_priority` 最小者，见第 152 轮 P1 决策），非控制段的那一份订单表被停进 `orders_backup` 且整个借用期内**不被执行**（`R3RSyncDrivingOrders()` 只在链头 owner==chain 时放行）。

期望：B 作为非控制段被带着跑完一圈后，B 自己的订单表变成

- B：`b1 → b2 → (GOTO_COUPLE c1) → [c2 → (不耗点命令) → c3 → c4] → b3 → b4`

其中方括号内是**自动记录出来的订单**（下称「自动命令」/auto-recorded orders），来源是这一圈里列车实际经过的车站 / 路点 / 车库。

语义要点（从例子反推）：

1. **插入锚点** = B 表里「进入非控制状态」的那条命令之后（本例 `GOTO_COUPLE` 之后），不是表尾、也不是当前执行位置之后（借用期间 cur index 冻结）。
2. **记录对象** = 实际**经过**的车站 / 路点 / 车库，按经过顺序；不含 A 的停靠等待时间语义（是否带 wait time 见 §五 U-3）。
3. **不重复累积**：跑第二圈再经过同一批站点时，必须**替换**上一轮的自动区间，而不是再插一段（否则表会无限增长）。
4. **可辨识**：玩家需要能区分「自己写的命令」和「自动记录的命令」（否则解挂后再跑 B 会把自动段当人工段一起沿用，且无法一键清除）。
5. **目的** = B 将来单独跑（或成为控制段）时，它知道自己在这段路上该停哪些站、时刻表该是什么；即把「跟随期间的路线事实」沉进 B 自己的排程。

## 三、现有机制取证

### 3.1 订单借用层（排程归属）

- 控制段判据：`R3RGetControlSegment(chain)` / `R3RGetPriorityHead(chain)`（`src\train_cmd.cpp`，按 `r3r_priority` 最小）。
- 链头借用控制段的 orders：`Vehicle::r3r_orders_borrowed` + `Vehicle::orders_backup`（`src\vehicle_base.h`）。
- 唯一执行入口：`R3RSyncDrivingOrders(chain, ...)`（`train_cmd.cpp`），只在 `owner == chain` 时让链头跑自己那份表，否则继续借用。
- `orders_backup` 已随存档：`src\sl\couple_group_sl.cpp` 的稀疏块 `R3VP`（第 94 轮加入，含 `orders_backup` / `order_backup_index` / `order_backup_index_i` / `unitnumber_backup` / `name_backup` / `orders_borrowed` / `priority`）。
- 结论：**非控制段的订单表在物理上一直存在、且能被单独改写**（`orders_backup` 指向的就是那张 `OrderList`，仍由 ORDL 存档）。这是本功能可行的地基——不需要新建存储。

### 3.2 「自动命令」这一词在现有代码里的歧义（必须先分清）

现有代码里与「自动」相关的**三个不同东西**，玩家口语可能混用：

| 名称 | 代码 | 语义 | 与本需求关系 |
| --- | --- | --- | --- |
| 自动填充 | `WID_VT_AUTOFILL` → `Commands::AutofillTimetable`，`VehicleFlag::AutofillTimetable` | 按实际运行历史**回填时刻表的等待时间**（不改订单表） | 不是本需求；但它是玩家抱怨按不下去的三个键之一 |
| 自动 | `WID_VT_AUTOMATE` → `VehicleFlag::AutomateTimetable` | 每次跑完自动更新时刻表数值 | 同上 |
| 自动分离 | `WID_VT_AUTO_SEPARATION` → `VehicleFlag::TimetableSeparation` | 与「分离时刻表」相关 | 同上 |

本需求的「自动命令」是**第 4 个概念**：由 R3R 自动往订单表里插入的 `Order` 对象（带「自动生成」标记）。代码里目前**不存在**这个概念，需要新增标记位与记录逻辑。

### 3.3 时刻表按钮 disabled 的现有判据（待复核）

`src\timetable_gui.cpp`：

- `:666` `SetWidgetDisabledState(WID_VT_AUTOFILL, v->orders == nullptr || v->vehicle_flags.Test(VehicleFlag::AutomateTimetable));`
- `:667` `SetWidgetDisabledState(WID_VT_AUTO_SEPARATION, v->vehicle_flags.Test(VehicleFlag::ScheduledDispatch) || v->HasUnbunchingOrder());`
- `:668` `EnableWidget(WID_VT_AUTOMATE);`
- `:677-687` 另有一个分支把 `WID_VT_AUTOFILL / WID_VT_AUTOMATE / WID_VT_AUTO_SEPARATION` 等整批 `DisableWidget()`（该分支的进入条件待读，见 §五 U-5）。

**疑似根因（待验证）**：`v->orders == nullptr`。R3R 的借用层把非链头车的 `orders` 摘掉（`src->orders = nullptr;` 见 `train_cmd.cpp` 车库停放分支），且 `orders_backup != nullptr` 时活字段恒为空 ⇒ 对这些车（非链头 / 段内非首车 / 停放车）打开时刻表窗口，`v->orders` 为空 ⇒ `WID_VT_AUTOFILL` 被禁用；若 `:677` 分支条件也命中，三个键会一起禁用。**这不一定是 bug，也可能是「窗口打开在错误的车上」**（R3R 订单窗口/时刻表窗口是否应重定向到控制段，见 U-6）。

### 3.4 段行（`R3RSegmentRecord`）是天然的落点

`src\couple_group.h:546-556`：

```cpp
struct R3RSegmentRecord {
    OrderList *orders = nullptr;          ///< 段自己的排程（R3SG 以 REF_ORDERLIST 存档）
    VehicleOrderID real_index = INVALID_VEH_ORDER_ID;
    VehicleOrderID implicit_index = INVALID_VEH_ORDER_ID;
    VehicleOrderID timetable_index = INVALID_VEH_ORDER_ID;
    bool orders_borrowed = false;         ///< 这张表属于链上另一段，本段绝不能释放
    UnitID unitnumber = 0;
    std::string name;
    GroupID group_id = GroupID::Invalid();
    bool in_use = false;                  ///< NOSAVE
};
```

三点直接相关：

1. **订单表归属已有正式宿主**（`orders`），且注释明确「段不是链头时仍能保住自己的排程」——正是 B 段在跟随期间需要的东西。
2. **三个索引**（`real_index` / `implicit_index` / `timetable_index`）已经存在，注释写明「这一段在**没被 tick** 的时候走到哪了」——这就是自动区间的**插入锚点**。
3. `orders_borrowed` 是 `Vehicle::r3r_orders_borrowed` 的段级孪生，用来判「这张表是借来的（不能写）」还是「本段自有（可以写自动记录）」。

⇒ 本功能**不需要新增存储结构**：自动区间 = 在段自有 `OrderList` 的 `real_index + 1` 处插入若干 `Order`，并在段行里记一个「自动区间范围」。

### 3.5 三个时刻表命令的硬校验（第二条诉求的取证）

`src\timetable_cmd.cpp`：

| 命令 | 校验 | 行 |
| --- | --- | --- |
| `CmdSetVehicleOnTime`（重置迟到） | `!IsCompanyBuildableVehicleType(v) \|\| !v->IsPrimaryVehicle() \|\| v->orders == nullptr` → `CMD_ERROR` | `:385` |
| `CmdAutofillTimetable`（自动填充） | 同上，**含 `IsPrimaryVehicle()` 与 `orders == nullptr`** | `:542` |
| `CmdAutomateTimetable`（自动） | `!IsPrimaryVehicle()` → `CMD_ERROR`（不要求 orders） | `:588` |
| `CmdTimetableSeparation`（分离时刻表） | `!IsPrimaryVehicle()` → `CMD_ERROR` | `:626` |

UI 侧 `src\timetable_gui.cpp`：

- `:666` `WID_VT_AUTOFILL` 禁用条件 = `v->orders == nullptr || AutomateTimetable`
- `:667` `WID_VT_AUTO_SEPARATION` 禁用条件 = `ScheduledDispatch || HasUnbunchingOrder()`
- `:668` `WID_VT_AUTOMATE` 在自家车分支**恒 EnableWidget**（所以它的「按不下去」只可能是命令层 `CMD_ERROR` 静默失败）
- `:674-688` 另一分支（`v->owner != _local_company`）把三者一起 `DisableWidget()`

**定性结论（三段，分层）**：

- 灰掉的（点不动）：`WID_VT_AUTOFILL` ⇒ 唯一可疑输入是 `v->orders == nullptr`。R3R 的借用层会让**非链头车的 `orders` 恒为空**（排程在 `orders_backup` 或段行里），车库停放分支还显式 `src->orders = nullptr;`（`train_cmd.cpp:3507`）。若玩家在**非链头/非控制段**那辆车上开时刻表窗口，正好命中。
- 可点但无反应：`WID_VT_AUTOMATE` / `WID_VT_AUTO_SEPARATION` ⇒ 命令层要求 `IsPrimaryVehicle()`，非链头车必 `CMD_ERROR`，界面只会「没反应」。
- 三者同时全灰的另一种可能：窗口打开的车不属于本机公司（`:674`），或玩家看的是别人的车队。

⇒ 这条**不是单一 bug，而是「R3R 段结构 × 原版『一辆车=一个 primary vehicle』假设」的错配**：时刻表窗口/命令全按「primary vehicle 拥有唯一时刻表」设计，而 R3R 把一个列车拆成多段、只有链头是 primary、且非控制段的 orders 不在活字段上。修法方向见 §4.4。

## 四、设计论证（候选方案与权衡）

### 4.1 方案比较

| 方案 | 做法 | 优点 | 缺点 |
| --- | --- | --- | --- |
| **A. 段行 + 真订单（推荐）** | 在非控制段的段自有 `OrderList` 里、`real_index+1` 处插入真实 `Order`；段行记自动区间范围 | 玩家看得见、能编辑、能被既有订单/时刻表系统原样消费；无新存储 | 需要可靠的「自动/人工」区分手段；插入会使索引位移，需同步三个 index |
| B. 只记不插（影子表） | 另存一份「经过记录」，不写订单表 | 零风险 | 不满足「计划里出现 c2-c4」的要求 |
| C. 给 `Order` 加 flag 位 | 用 `Order::flags`（`uint16_t`，`order_base.h:138`）里未用位标记 | 最直接 | 需先确认位是否已满；改 `Order` 结构影响存档（`Order` 是 `SLE_...` 逐字段写的） |

推荐 **A**，但「自动/人工」区分不靠 `Order::flags`，而靠**段行里的区间元数据**（`auto_begin` / `auto_count`）：

```cpp
/* 追加到 R3RSegmentRecord（第 152 轮结构，向后兼容：缺字段取默认 0） */
VehicleOrderID auto_begin = INVALID_VEH_ORDER_ID;  ///< 自动区间在 orders 里的首索引
uint16_t auto_count = 0;                            ///< 自动区间长度（0 = 没有自动记录）
```

理由：`Order::flags` 的 16 位承担 load/unload、depot action、non-stop、timetabled、full-load、unbunching 等，确认剩余位需要逐个核对并存档版本迁移；区间元数据在段行里是**天然成对**的（一段只有一段自动区间），且已被 R3SG 存档机制覆盖，不需要碰 `Order` 的存档布局。

### 4.2 记录钩子（「实际经过」而非「订单目标」）

| 目标 | 现有钩子 | 说明 |
| --- | --- | --- |
| 车站 | `TrainEnterStation(Train *v, StationID)`（`train_cmd.cpp`，station 分支） | 已有 R3R 探针与折返逻辑；`v` 是链头 |
| 路点 | 同一函数的 waypoint 分支（`~6161`） | 与车站共用入口，可按 `IsRailWaypointTile` 区分 |
| 车库 | `TrainEnterDepot(Train *v, TileIndex)` | 进库即一条 `OT_GOTO_DEPOT` |

每处判定「哪个段经过了」的做法（候选）：

- **段头法**：遍历链上各段，取段头（`IsSegmentFront()`）车的 tile，若该 tile 属于本站/本路点/本库，则记该段一条。
- 优点：简单、确定；与「段」语义一致（段的行程 = 段头的行程）。
- 缺点：超长列车可能只有后段在站台内 ⇒ 段头不在站台格时漏记。可用「段内任一节车在该站 tile 集合内」取代，但要多一次链遍历。

记录时机必须**只对非控制段**生效：`R3RGetControlSegment(chain) != 段`。控制段自己的表本来就在跑，不需要记录（它的订单就是事实）。

### 4.3 插入、替换与索引维护

1. **锚点**：段行 `real_index`（借用期冻结值）就是「最后一次作为控制段执行到哪」。自动区间插在 `real_index + 1`。
   - 例：B 表 `b1 b2 GOTO_COUPLE b3 b4`，冻结在 `GOTO_COUPLE`（index 2）⇒ 插在 index 3 ⇒ `b1 b2 GOTO_COUPLE [c2 c3 c4] b3 b4`，与玩家例子逐字一致。
2. **替换而非追加**：每次**进入**非控制状态（耦合提交点）时，若 `auto_count > 0`，先删掉 `[auto_begin, auto_begin+auto_count)` 并同步三个 index；再开始新一轮记录。这样第二圈不会无限增长。
3. **索引位移**：插入/删除都要同步 `real_index` / `implicit_index` / `timetable_index`（既有 `OrderList` 的插入/删除操作是否自动维护这些？需查 `InsertOrder` / `DeleteOrder` 的语义，见 U-9）。
4. **记录期间表不能被跑**：非控制段不 tick，所以记录不会与执行冲突；但若该段在记录中途**变成控制段**（解挂后 B 单独跑），必须立刻停止记录并封闭区间。

### 4.4 第二条诉求（时刻表按钮）的修法方向

三个层次，按风险从低到高：

1. **UI 判据放宽（最小改动）**：`SetButtonDisabledStates()` 里把 `v->orders == nullptr` 改成 `v->orders == nullptr && R3RSegmentOrdersOf(v) == nullptr`（即段行/备份里也没有表才算没有），并在 `:674` 之外补一个「R3R 段车」分支：允许段头车操作，但命令目标重定向到控制段/链头。
2. **命令层重定向**：`CmdAutofillTimetable` / `CmdAutomateTimetable` / `CmdTimetableSeparation` 在 `!IsPrimaryVehicle()` 时改用 `R3RGetOwnershipVehicle(v)`（段 → 链头/控制段）再执行。这需要命令层能安全地改成「作用于链头」，且**语义要想清楚**：改的到底是控制段的时刻表还是本段的？按用户「班次」诉求，应该是**本段自己的** ⇒ 但原版命令只认 `v->orders`，需要改成「本段订单表」。
3. **结构性方案**：让段也能作为 primary（触及引擎核心 `IsPrimaryVehicle()` 的十几处使用点，**不推荐**）。

推荐先做 1+2 的「只读/重定向」组合：窗口显示本段自己的订单表（从段行取），按钮命令作用到本段行；只有当本段没有订单表时才退化到控制段。

> **本节已被 §4.8 取代（2026-10-20 第二轮）**：玩家已确认「能让玩家预览时刻表的不都是牛逼的控制段的表吗」，即窗口展示的本来就是控制段的表 ⇒ **不改窗口显示对象**；真正的死因是命令层 `!IsPrimaryVehicle()`，修法收敛为「解析到订单表所有者 + `VehicleFlag` 双写」，详见 §4.8。

### 4.5 与既有机制的交互（必须逐条确认）

- **DECOUPLE / INHERIT**：解挂时 B 拿回自己的表（含自动区间）。若 `ODOF_INHERIT_ORDERS` 让解出方继承整份表，自动区间会跟着走——是否符合预期需拍板（U-10）。
- **`R3RSyncDrivingOrders()`**：成为控制段后，B 的表被执行；自动区间里的 `GOTO_STATION` 会真的停靠。这正是用户要的，但**必须有「不耗点」选项**（用户例子明确写「(不耗点的命令)」）。
- **`R3RPushProgressToOwner()`**：借用期进度回写只写三个 index，不写字表 ⇒ 与本功能不冲突。
- **存档**：段行已有 R3SG 块；新增两个字段需确认 R3SG 的版本化写法（U-11）。`orders_backup` 已在 R3VP。
- **联机 / desync**：记录发生在 `TrainEnterStation`（游戏逻辑 tick 内），是确定性路径；只要插入/删除都走同一逻辑、不依赖 UI 状态，就不会 desync。

### 4.6 参考实现草图（下一轮照此落地）

```cpp
/* 段行新增字段（couple_group.h，R3RSegmentRecord 尾部） */
VehicleOrderID auto_begin = INVALID_VEH_ORDER_ID;
uint16_t auto_count = 0;

/* 提交点（耦合/车库编辑/解挂）调用：进入非控制状态时清空上一轮记录 */
void R3RBeginAutoRecord(Train *chain, const Train *segment_head);

/* 三个到点钩子调用：把「本段经过了 X」追加进该段自己的 OrderList */
void R3RRecordAutoOrder(Train *chain, const Train *segment_head, Order &&order);

/* 提交/读档时收尾：封闭区间，标记 auto_begin/auto_count */
void R3RFinaliseAutoRecord(Train *chain);
```

### 4.7 玩家拍板（2026-10-20 第二轮答复，直接覆盖 §五 的 U-3/U-4/U-6/U-10/U-12）

| 编号 | 玩家原话要点 | 已确定口径 |
| --- | --- | --- |
| U-3 / U-7 | 「列车领取路签这些都不算耗点的操作，只有前往车站/车库/路点这些需要列车物理移动的才算耗点操作，噢，还有，等待挂接和前往挂接不算耗点操作」 | **耗点命令 = `OT_GOTO_STATION` / `OT_GOTO_WAYPOINT` / `OT_GOTO_DEPOT`**；`OT_WAIT_COUPLE` / `OT_GOTO_COUPLE` 与路签（order slot）类一律**不耗点**。⇒ 自动命令只记这三类，新插入订单 `wait_time = 0`（不带等待时间、不产生停靠等待）。 |
| 记录粒度 | 「记录的车站是经过而非仅仅停靠」 | 钩子必须抓**路径经过**：经过不停靠的车站/路点、列车开进开出的车库都记。 |
| U-4 | 「第二圈就按照替换上一轮自动区间来，不过，如果这一轮和上一轮自动区间一样，就不替换」 | 替换前做**逐条等价比较**（类型 + 目标 id/tile 序列全等）⇒ 相同则**整段跳过**（不删不插、不动索引、不重置时刻表）；不同才删旧插新。 |
| 跨公司 | 「跨公司就和同公司一样」 | 不按公司过滤：车站、路点、车库照记，命令目标原样写入 B 的表。 |
| U-10 | 「解挂之后自动的区间就跟着 B 走到下一圈的这个位置啊。然后，我记得 R3R 没有什么 INHERIT_ORDERS、让 B 拿走整份表啊」 | 自动区间**永久属于 B**，位置固定在锚点（`GOTO_COUPLE` 之后）；解挂后保留，下一圈跑到该位置就执行。`ODOF_INHERIT_ORDERS` 等枚举在 R3R 里**零使用点（未接线）** ⇒ 该交互项作废。 |
| U-6 / U-12 | 「我看到的是不灰但是按下没反应，点的就是普通的车啊，能让玩家预览时刻表的不都是牛逼的控制段的表吗」 | 不灰 = 走自家车分支；**无反应 = 命令层 `CMD_ERROR` 静默失败**。时刻表窗口本就展示控制段的表 ⇒ 修法改为「命令作用对象统一到实际订单表」，不重定向窗口。具体失败点按 KI-389 第 2 轮取证（`orders == nullptr` / `IsPrimaryVehicle()` / flag 被覆写，三选一）。 |
| 环境 | 「虽然你看到一个 openttd 进程，但是这个不是 build 文件夹里面的那个 exe，不必杀死」 | 构建前**不必**杀 `openttd.exe`；遇到「openttd.exe is running」闸门先核对路径是否为 `build\openttd.exe`，别误杀玩家正在用的副本。 |

### 4.8 KI-389 命令层取证结论（2026-10-20 第二轮，已定性到单一谓词）

**取证原文（本次实读，非推断）**

- `src\timetable_gui.cpp:666`：`SetWidgetDisabledState(WID_VT_AUTOFILL, v->orders == nullptr || v->vehicle_flags.Test(VehicleFlag::AutomateTimetable));`
- `src\timetable_gui.cpp:667`：`SetWidgetDisabledState(WID_VT_AUTO_SEPARATION, v->vehicle_flags.Test(VehicleFlag::ScheduledDispatch) || v->HasUnbunchingOrder());`
- `src\timetable_gui.cpp:668`：`this->EnableWidget(WID_VT_AUTOMATE);` ⇒ **「自动」键在自家车分支恒可点**。
- `src\timetable_gui.cpp:675-687`（`v->owner != _local_company` 分支）：`DisableWidget(WID_VT_AUTOFILL / WID_VT_AUTOMATE / WID_VT_AUTO_SEPARATION)` 三键整批禁用 ⇒ 灰 = 非本公司车，与玩家「不灰」互斥，玩家必走自家车分支。
- `src\timetable_cmd.cpp:542`（`CmdAutofillTimetable`）：`if (v == nullptr || !IsCompanyBuildableVehicleType(v) || !v->IsPrimaryVehicle() || v->orders == nullptr) return CMD_ERROR;`
- `src\timetable_cmd.cpp:588`（`CmdAutomateTimetable`）：`if (v == nullptr || !IsCompanyBuildableVehicleType(v) || !v->IsPrimaryVehicle()) return CMD_ERROR;`
- `src\timetable_cmd.cpp:626`（`CmdTimetableSeparation`）：同上，也只查 `IsPrimaryVehicle()`。
- `src\train.h:325`：`bool IsPrimaryVehicle() const override { return this->IsFrontEngine() || this->IsFrontWagon(); }`（该谓词实质就是 `GVSF_FRONT` 位）。

**逻辑演绎（关键：不是三选一，而是已被排除到一条）**

1. 「自动」与「分离时刻表」两个命令**只**校验 `IsPrimaryVehicle()`（外加 `v != nullptr` / 车辆类型 / `CheckOwnership` 本公司）。
2. 玩家报告这两个键「不灰但按下去没反应」，且三个键同时如此 ⇒ `CheckOwnership` 通过（否则 GUI 会整批 Disable）⇒ 唯一能同时卡死三个键的共同谓词是 **`v->IsPrimaryVehicle() == false`**。
3. 因此 `CmdAutofillTimetable` 的 `v->orders == nullptr` 与「flag 被 tick 覆写」两条候选**不再是主因**（它们无法解释「自动」「分离」两键无反应）。⇒ 之前的「三选一」收敛为单点：**时刻表窗口持有的 `v` 不是前端车（既非 `IsFrontEngine()` 也非 `IsFrontWagon()`）**。

**为什么会拿到一个非前端车？** R3R 的结构性后果，候选按可能性排序：

- **H-a（最可能）段内车/被吸收车底的车节**：R3R 耦合后把被挂车底链头的假引擎身份撤掉（`R3RDestroyCarOnlyFormation` / `ClearSegmentTailFakeEngine` 一带的 `ClearEngine()`），该车于是 `IsFrontEngine()==false`；它在段行里仍可能有自己的 `OrderList`（`R3RSegmentRecord::orders`），于是「有表 ⇒ 自动填充不灰」+「非前端 ⇒ 三键全部 CMD_ERROR」两个现象同时成立，与玩家描述逐字吻合。
- **H-b 窗口从地图/子行点开时持的是链中的某一节**：`ShowTimetableWindow(const Vehicle *v)`（`timetable_gui.cpp:1377`）直接把传入的 `v->index` 当窗口编号，没有任何 `v->First()` 归一；而地图点击命中车节、车队列表的隐性段子行（KI-247，`vehicle_gui.cpp:381-392`）都可能把非链头车传进来。玩家「点的就是普通的车」正是这个形态。
- **H-c 控制段本身不是前端车**：若某次提交点把 `GVSF_FRONT` 移到了别的车上（逻辑翻/车库编辑/读档迁移），连「控制段」都会按不动——这条能同时解释玩家「能让玩家预览时刻表的不都是牛逼的控制段的表吗」的疑问。

**修法方向（下一轮落地，先探针后改）**

1. **先加只读探针**（`CmdAutofillTimetable` / `CmdAutomateTimetable` / `CmdTimetableSeparation` 入口各一行，命中早退时也打）：`TT-CMD <cmd> veh= front= frontwagon= orders=%p owner= first= prioHead= segid= ` ⇒ 一条日志即可判定 H-a/H-b/H-c。
2. **命令作用对象归一**：三命令在 `!v->IsPrimaryVehicle()` 时不再直接 `CMD_ERROR`，而是解析到「实际持有/展示这张订单表的那辆车」——优先 `R3RGetPriorityHead(chain)`（控制段），退化 `v->First()`；`VehicleFlag` 仍要写在**玩家点击的那辆车**上（否则按钮不高亮），所以实现上是「解析目标 + 同时写 v 与其 primary」。
3. **`v->orders == nullptr` 的处理**：对 R3R 段车，`orders` 可能停在段行/`orders_backup`（`src\train_cmd.cpp:6186-6193` 的借用停放、`:3507` 的车库停放分支）⇒ 判据改为「`v->orders` 或段行 `orders` 任一非空即通过」，否则 `CmdAutofillTimetable` 依旧会静默失败。
4. **不做**：不把段提升成 primary（`IsPrimaryVehicle()` 有十几处使用点，风险过高）；不改时刻表窗口的显示对象（玩家已确认它展示的本来就是控制段的表）。

**结论句**：KI-389 已从「三选一」收敛到 `!IsPrimaryVehicle()` 单点；下一轮先加 `TT-CMD` 探针定性是 H-a/H-b/H-c，再按「解析到订单表所有者 + flag 双写」实现。

### 4.9 TT-CMD 探针已实现（第 257 轮，零行为变更）

改动只在 `src\timetable_cmd.cpp`：

- 顶部补 `#include "train.h"`（要读 `Train::IsFrontEngine()` / `IsFrontWagon()`）。
- 新增文件内静态 `R3RTimetableCmdProbe(const char *cmd, const Vehicle *v, VehicleID veh)`，整函数被 `if (!R3RDbgOn()) return;` 包裹（`R3R_PROBES=0` 时零开销），输出一行：
  `TT-CMD <cmd> veh= found= type= primary= front= frontwagon= orders=%p owner= first= segid= prio= borrowed=`
- 四个命令的 `Vehicle *v = Vehicle::GetIfValid(veh);` 之后**无条件**调用（`cmd` = `on-time` / `autofill` / `automate` / `separation`），即成功点击也会打一行 ⇒「日志里连 TT-CMD 都没有」可以判定命令根本没被派发。
- 判读口径：
  - `found=0` ⇒ 窗口号是失效车辆（不是 H-a/b/c，另立条目）。
  - `front=1 frontwagon=0` ⇒ 是正链头，`!IsPrimaryVehicle()` 只能来自别处（H-c 的变体）。
  - `front=0 frontwagon=1` ⇒ 段内非段首车厢（H-a）。
  - `front=0 frontwagon=0` ⇒ 既非引擎也非前车（H-b，窗口开在中间车厢上）。
  - `segid=65535` ⇒ 该车没有段 ID（未 settle 或旧档），可与 H-a 交叉验证。

构建：只改 `src\timetable_cmd.cpp`，护栏 `GUARD: incremental is safe`；第一次 `timetable_cmd.cpp(410): error C2440`（`Owner` 是强类型包装，需 `.base()`），改成 `(int)v->owner.base()` 后 `[3/3] Linking`、`EXIT_CODE=0`（0:15:49）、errs=0、`timetable_cmd.cpp` 0:14:27 → obj 0:14:43 → `build\openttd.exe` 2026-10-21 0:15:45（52 039 680 B）、`read_lints` 0、exe 命中 `TT-CMD %s veh=`。状态＝探针已加 + 已编译，等玩家提供 3 次点击的 `TT-CMD` 行。

## 五、历史未确认项（被 §4.7 关闭者仅留编号）

- U-1 记录钩子应落在哪：`TrainEnterStation()` / 路点到达分支 / `TrainEnterDepot()`，还是统一的 `ProcessOrders()` 推进点？需要「实际经过」而不是「订单目标」。
- U-2 插入锚点如何稳定定位：借用期间 B 的 `cur_real_order_index` 冻结在哪一条？是否需要在耦合提交点快照「进入点索引」并存档（否则读档后无法找回插入位置）。
- U-3 自动命令是否带 `wait time` / 停靠类型：玩家例子写的是「(不耗点的命令)」，暗示自动命令应**默认不耗点**（`OT_GOTO_STATION` 带 `OrderFlag::NoUnload`? 还是仅 `wait_time = 0`），需与「不耗点命令」的确切含义对齐（见 U-7）。
- U-4 第二轮经过同一批站点时如何替换：清空上一段自动命令需要能识别边界（首/尾标记或连续区间）。
- U-5 `timetable_gui.cpp:677` 那个整批 Disable 分支的进入条件是什么。
- U-6 R3R 下「打开某车的时刻表窗口」是否应该自动重定向到控制段（与订单窗口一致）；若不重定向，非控制段的时刻表显示的是「借用中的控制段表」而非自己的表，玩家会看到别人的时刻表。
- U-7 玩家「不耗点的命令」的确切所指（`OT_NOTHING`? 条件订单? `OrderFlag::NoLoad`?）。
- U-8 自动命令是否参与 `AutofillTimetable` 的时间统计（若参与，则 B 单独跑时时刻表可直接填充，符合「知道自己的时刻表」）。
- U-9 `OrderList::InsertOrder` / `DeleteOrder` 是否自动维护 `cur_real_order_index` / `cur_implicit_order_index` / `cur_timetable_order_index`（决定插入后是否需要手工修正）。
- U-10 解挂时 `ODOF_INHERIT_ORDERS` 与自动区间的交互（继承方是否带走自动记录）。
- U-11 R3SG 段行块的版本化方式与新增字段的向后兼容写法。
- U-12 时刻表按钮到底是「灰」还是「可点无反应」，需要玩家提供截图/复现步骤（决定是修 UI 判据还是命令层）。

## 六、下一轮落地清单（初稿，待论证收敛）

1. `Order` **不加**标记位（`flags` 16 位已占满、`Order` 逐字段存档需版本迁移）：段行新增 `auto_begin` / `auto_count` 即可界定区间（§4.1）。R3SG 字段表在 `src\sl\couple_group_sl.cpp:276-285`，追加两行 + 版本化（U-11）。
2. 非控制段区间记录器：在**链头 tick** 时判「链上各非控制段是否应记录当前位置」，按段把经过的站点/路点/车库追加到该段自己的 `OrderList`（段行 `orders`；段行已有 `orders` 与三个索引，无需新存储），锚点 = 段行 `real_index + 1`。
3. 去重/替换：每轮进入非控制状态时先做**逐条等价比较**，相同则整段跳过（不动索引、不重置时刻表），不同才删旧插新。
4. UI：自动命令在订单窗口与时刻表窗口的可辨识呈现（颜色/图标/后缀）——可与 5 一起做。
5. **KI-389（已定性，见 §4.8）**：先加只读探针 `TT-CMD`（三命令入口，含早退路径）定性 H-a/H-b/H-c；再实现「`!IsPrimaryVehicle()` 时解析到 `R3RGetPriorityHead(chain)` / `v->First()` + `VehicleFlag` 双写」；`v->orders == nullptr` 判据放宽为「`v->orders` 或段行 `orders` 任一非空」。**不**提升段为 primary、**不**改窗口显示对象。

## 七、复测判据（初稿）

- ① 用户例子场景：B 跑完一圈后订单表恰为 `b1 b2 GOTO_COUPLE c2 (不耗点) c3 c4 b3 b4`，且 c2~c4 带自动标记。
- ② 第二圈后表长不增长（替换而非追加）。
- ③ 解挂后 B 单独跑，按自动记录区间停靠，不产生 `COUPLE-FAIL` 循环。
- ④ 负判据：单段链 / 全程都是控制段的列车，订单表逐字不变。
- ⑤ 存档往返：自动标记与自动区间都保留；旧档读入无自动标记、行为不变。
- ⑥ 时刻表三键（自动 / 自动填充 / 分离时刻表）在「链头、控制段、段内车节」上都能按下并立即生效（按钮变高亮 / 相应 `VehicleFlag` 置位）；负判据：非本公司车仍整批灰；日志出现 `TT-CMD` 且 `primary=0 -> 解析 prioHead=N` 的路径，不再出现无日志的静默早退。

---

## 八、实现进度（2026-10-20 第三轮，边做边落盘）

### 8.1 已落地（编译前）

| 文件 | 改动 | 状态 |
| --- | --- | --- |
| `src\couple_group.h` | `R3RSegmentRecord` 在 `group_id` 后、`in_use` 前新增 `auto_begin`(`uint16_t`)、`auto_count`(`uint16_t`)、`auto_signature`(`uint32_t`)，附区间语义注释 | 已写 |
| `src\sl\couple_group_sl.cpp` | `_r3r_segment_desc` 在 `group_id` 后追加三条 `NSL(...)`（UINT16/UINT16/UINT32），旧档读 0 | 已写 |

### 8.2 关键取证结论（本次实读，带锚点）

- **非控制段的表仍在段头车自己手上（Route A）**：`src\train_cmd.cpp:11113-11146` 的交接块注释明写 “the merged head BORROWS the command owner's schedule instead of stealing it -- the owner keeps its own pointer (no `u->orders = nullptr`)”。⇒ 取「本段自有表」的判据 = `seg->r3r_orders_borrowed ? seg->orders_backup : seg->orders`；两者皆空时回退段行 `R3RSegmentGet(id)->orders`。
- **隐藏段的段行订单表故意不填**：`R3RSyncHiddenSegmentTraits()` 只写 unit/name/group（其注释：借用规则尚未改，隐藏段的 `orders` 留空）。⇒ 段行 `orders` 不能作为记录落点的唯一来源，必须以段头车字段为准。
- **控制段判据**：`R3RGetControlSegment(chain)`（按 `r3r_priority` 最小）。
- **段行指针不可跨 Alloc 持有**：`R3RSegmentGet` 返回的指针在下次 `R3RSegmentAlloc/GetOrCreate` 后可能失效（vector 扩容）。

### 8.3 记录算法（本轮实现口径）

0. 只对 **非控制段** 生效（`R3RGetControlSegment(chain) != seg_head`）；控制段自己的表就是事实，不写。
1. **钩子** = `TrainLocoHandler` 每 tick 调一次；先按「段头车 tile 是否变化」节流（`unordered_map<segment_id, TileIndex>`，NOSAVE），tile 变了才处理该段。
2. **取目标**：段头车当前 tile → 路点（先判 `IsRailWaypointTile`）/ 车站（`IsRailStationTile`）/ 车库（`IsRailDepotTile`），各生成 `OT_GOTO_WAYPOINT / OT_GOTO_STATION / OT_GOTO_DEPOT`，`wait_time = 0`（不耗点）。
3. **锚点** = 段头车 `cur_real_order_index + 1`（冻结值；`INVALID` 时取 0）。只在区间为空（`auto_count == 0`）时写入 `auto_begin`。
4. **去重** = 与区间最后一条比较（同类型同目标即跳过）⇒ 停靠多格站台不会写多条。
5. **轮次**（`unordered_map<segment_id, uint16_t> cursor`，NOSAVE）：
   - 每次提交点 settle 到「本段为非控制段」时把 cursor 置 0（新一轮，重头逐条比对）；
   - 记录时与区间第 `cursor` 条比较：相同 ⇒ `cursor++` 不写；不同 ⇒ 从 `auto_begin + cursor` 起删掉区间剩余，再追加本条，`auto_count = cursor + 1`，`cursor++`；
   - 轮末截断放在同一 tick 检查：若 `cursor > 0 && cursor < auto_count` 且本轮确实跑过（有匹配或写入），说明本轮站点比上一轮少 ⇒ 截断到 `cursor`。
6. **玩家手改保护**：`auto_signature` = 区间内容哈希，定稿时写入；记录前若实测哈希 != 存档值 ⇒ 视为玩家手改，清空 `auto_*`（**不删订单**）并停止本段自动记录。
7. **段释放**（`R3RSegmentFree`）时清 `auto_*`；克隆/读档沿用 settle 路径重新判定。

### 8.4 构建自证（2026-10-21 0:10 完成）

- 构建入口：复用 `_tmp_inc_build.cmd`（**未新建任何 .cmd**）。
- 第一轮：`src\couple_group.h`（2026-10-20 23:30:23）新于最新 obj（18:26:58），护栏打 `GUARD: REMOVED 620 object file(s) - upgrading this build to a FULL rebuild` ⇒ 全量。在 `[668/692]` 失败：
  `src\train_cmd.cpp(5722): error C2664: 无法将参数 1 从“const Order”转换为“Order &&”`（`order_base.h(1707)` 声明 `void OrderList::InsertOrderAt(Order &&new_order, VehicleOrderID index)`）。
- 修法（只改 `src\train_cmd.cpp`，不触发再全量）：`R3RAutoOrderAppend()` 内 `ol->InsertOrderAt(Order(o), (VehicleOrderID)at);`，注释说明「InsertOrderAt 只收右值，传拷贝以免污染调用方的 `want`」。
- 第二轮：`[26/26] Linking CXX executable openttd.exe`；`build\R3R_incbuild.done` = `EXIT_CODE=0`（0:10:46）；`error C`/`fatal error`/`FAILED:`/`build stopped` 计数 0。
- 时间戳：`src\train_cmd.cpp` 0:05:14 → `train_cmd.cpp.obj` 0:05:39；`src\sl\couple_group_sl.cpp` 23:30:27 → `couple_group_sl.cpp.obj` 23:50:40；`build\openttd.exe` @2026-10-21 0:10:29（52 039 680 B）晚于全部源码。
- `read_lints`：`train_cmd.cpp` / `couple_group.h` / `sl\couple_group_sl.cpp` 均 0 条。
- exe 字面量核对：`AUTO-ORD seg=` / `AUTO-ORD-DROP` / `AUTO-ORD-TRUNC` / `auto_signature` 各命中 1 处。
- 顺带改动：`_tmp_inc_build.cmd` 的「游戏在跑就退出 97」闸门由「任何 `openttd.exe`」改为「只拦本树 `build\openttd.exe`」——原因是玩家当时跑的是 `D:\R3R-OpenTTD-0.73.1-win64\openttd.exe`，不该阻断内测构建。
- 发布树 `build-release` **未编**（按 2026-10-19 口径：日常只编内测树）。

### 8.5 状态与复测

状态＝**已实现 + 已编译**，游戏内复测待做。6 条复测判据见 `R3R_KNOWN_ISSUES.md` 的「第 257 轮实现」小节末（① B 段表出现 `c2(不耗点) c3 c4` 且不停车装卸 ② 同序列第二圈不增长不重置时刻表 ③ 改线出现 `AUTO-ORD-TRUNC` ④ 存档往返 `SEGTR-SNAP auto b/c/sig` 一致 ⑤ 控制段/单段链负判据 ⑥ 手改后 `AUTO-ORD-DROP` 且玩家订单保留）。

未做：游戏内复测；KI-389（时刻表三键「不灰但无反应」）按下一轮加 `TT-CMD` 只读探针定性。
