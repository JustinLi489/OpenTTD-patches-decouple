# R3R 排程池 —— 排程与列车解耦（第 417 轮）落地备忘

编写时间：2026-11-10。承接 `R3R_dispatch_pool_round416_memo.md`（第 416 轮车站调度池）。

## 1. 玩家四条需求（原话要点）

1. 调度计划表（排程）要像池子一样管理，像路签一样「表与列车独立但有相互作用」：
   - 公司「列车管理」界面，并列于「路签」「挂接分组」，新增「列车排程管理」入口；
   - 进去能看到本存档的**排程池**，可以创建**无主排程**；
   - 列车上原本依附的排程转为**自动创建的有主排程**；
   - 列车本身也能改自己的排程指针指向哪张表。
2. 分配调度池同理：既能直接引用既有的（依附于列车的）排程，也能直接使用无主排程。
3. 改排程指针必须**语义明确**：必须清楚「改的是哪个指针」——
   - `Vehicle::orders`（列车持有的排程指针）；
   - 车站调度池条目里的排程引用（条目 → 表，不复制表）。
4. **撤销第 416 轮的禁止性校验**：不再禁止「含 R3R 命令（解挂/挂接/等待挂接）的排程」进入车站分配池；任何样子的排程都能入池，非崩溃性后果由玩家承担，只保留防崩溃/防未定义行为的硬校验。

## 2. 数据模型结论（这一轮拍板）

**「无主排程」= 一张 `first_shared == nullptr` 的 `OrderList`**，本体仍在 `_orderlist_pool` 里，因此：

- 存档不需要新块：`Save_ORDL()` 遍历整个池（含空表），空的无主表也会被写出；读回来时默认构造 `OrderList(OrderListID)` 的 `first_shared` 就是 `nullptr`。
- 「有主 / 无主」不需要字段标记，读 `GetFirstSharedVehicle() == nullptr` 即可。
- 代价：全库对 `GetFirstSharedVehicle()` 的无条件解引用必须逐个加空守卫（第 416 轮正是因此才没做独立表）。本轮逐一核对并加守卫，清单见 §4。

## 3. 三条新命令（`CommandType::OtherManagement`）

| 命令 | 数据 | 语义 |
| --- | --- | --- |
| `CreateR3RLooseSchedule` | 无 | 新建一张空的无主排程，放进排程池 |
| `DeleteR3RLooseSchedule` | `uint32_t ol_index` | 只允许删无主表（有主表由列车生命周期管） |
| `AssignR3RSchedule` | `uint32_t veh_index, uint32_t ol_index` | **只改目标列车的 `orders` 指针**指向该表；不复制表内容；已有其它列车共用该表时成为共享成员 |

`AssignR3RSchedule` 的守卫（防崩溃口径）：
- 目标必须是有效主车（`IsPrimaryVehicle()`），正在借用（`r3r_orders_borrowed`）时拒绝并报错 —— 借用态改指针会破坏 `orders_backup` 单层停放，属结构性风险，不做「智能处理」，直接拒绝并提示。
- `orders_backup != nullptr`（有停放表）时也拒绝，理由同上（单层槽位）。
- 共享登记走原生 `AddToShared`/`RemoveFromShared` 口径，不手搓共享环。

## 4. `GetFirstSharedVehicle()` 空守卫清单（防崩溃硬校验）

| 位置 | 处理 |
| --- | --- |
| `order_cmd.cpp` `OrderList::Initialize()` | 允许 `v == nullptr`：只重算计数/时长，不注册终点 |
| `order_cmd.cpp` `OrderList::FreeChain()` | 无主表从未注册过终点 ⇒ 跳过 `UnregisterOrderDestination`（否则会打乱站点引用计数） |
| `station_gui.cpp` `can_vehicle_use_facility` | 无主表直接跳过（它本来 `GetNumVehicles()==0` 不计数） |
| `sl/waypoint_sl.cpp` / `sl/station_sl.cpp` 旧档迁移 | 加空守卫 |
| `order_serialisation.cpp` 导出 | 无主表输出 `error` 字段而不是解引用 |
| `departures_gui.cpp` / `vehiclelist_func.h` | 只从**车辆的** `orders` 进，无主表到不了，不改 |
| `afterload.cpp` JOKERPP 两处 | 循环条件本就 `w != nullptr`，不改 |
| `order_cmd.cpp` 各 Insert/DeleteOrder | 只经车辆调用，不改 |

## 5. 界面

- 入口：列车列表窗口「列车管理」下拉新增 `ADI_R3R_SCHEDULE_MGMT`（「列车排程管理…」），与「路签」「挂接分组」并列。
- 窗口 `R3RSchedulePoolWindow`（`WindowClass::R3RSchedule`）：本存档**全部**排程（有主 + 无主）一张列表，行文案：
  - 有主：`{车名}: N 条命令`（持有者）；
  - 无主：`无主排程 #k: N 条命令`（k = 列表内无主序号）。
  - 目标列车当前指针指向的那张表用 `*` 前缀标出（这就是「哪个指针」的可见化）。
- 按钮：「新建无主排程」「删除选中排程」「让目标列车指向选中排程」。
- 底部一行常驻说明文字：明确写出「本窗口改的是 `Vehicle::orders` 指针与排程池条目，不复制表内容」。

## 6. 待办 / 风险

- 无主排程没有公司归属（`OrderList` 不存 owner），窗口对多公司是全局可见的；本轮不做多公司隔离，登记为已知边界。
- 第 416 轮那条「池表必须是叶子（禁 R3R 命令）」按需求 4 撤销 —— 循环借用/自引用导致的死循环由玩家承担，代码侧只保证不崩。

## 7. 本轮落地（2026-11-10）

### 7.1 新增文件（4 个）

| 文件 | 内容 |
| --- | --- |
| `src/r3r_schedule_gui.cpp` | `R3RSchedulePoolWindow` 全部实现 + `ShowR3RScheduleWindow()` |
| `src/r3r_schedule_gui.h` | 窗口类声明 + 打开函数 |
| `src/widgets/r3r_schedule_widget.h` | `_nested_r3r_schedule_widgets`（`WID_SCHED_CAPTION` / `_LIST` / `_SCROLLBAR` / `_HINT` / `_TARGET_SEL` / `_NEW` / `_DELETE` / `_ASSIGN`） |
| 本文档 | 交付要求 |

构建接入：`src/CMakeLists.txt` 在 `querystring_gui.h` 与 `rail.cpp` 之间插 `r3r_schedule_gui.cpp` / `.h`；`src/widgets/CMakeLists.txt` 在 `plans_widget.h` 与 `rail_widget.h` 之间插 `r3r_schedule_widget.h`（两处都是显式源列表，不是 glob ⇒ 必须手加，否则链不上）。

### 7.2 命令层（`src/order_cmd.cpp` / `order_cmd.h`）

- `CmdR3RCreateLooseSchedule`：`copy_from` 为 0 ⇒ 空表；否则把源表命令深拷贝进新表。新表 `first_shared = nullptr`、`num_vehicles = 0`，即「无主排程」。**创建前补 `OrderList::CanAllocateItem()` 闸门**（`Pool::GetNew` 在 debug 下断言曾问过容量，缺了会直接 assert），失败回 `STR_ERROR_NO_MORE_SPACE_FOR_ORDERS`。
- `CmdR3RDeleteLooseSchedule`：仅当 `GetFirstSharedVehicle() == nullptr` 且 `GetNumVehicles() == 0` 才允许，否则 `CMD_ERROR`（有主表一律不许从池里删）。
- `CmdR3RAssignSchedule`：把 `v->orders` 重指向 `list`，**不复制任何命令**。守卫链：
  1. `IsValidID` + `list->GetNumOrders() != 0`；
  2. `IsCompanyBuildableVehicleType(v)` + `v->IsPrimaryVehicle()`；
  3. `CheckOwnership(v->owner)`；
  4. 列车/航空器这类「表主人」类型匹配（`first_shared->type != v->type` ⇒ `CMD_ERROR`）；
  5. `IsR3RFollower()` / `orders_backup != nullptr` / `r3r_orders_borrowed` 一律拒绝（借用态与停放态的指针有第二份账，不能直接改）；
  6. 旧表是共享链（`GetNumVehicles() > 1` 或 `PreviousShared()/NextShared()` 非空）走 `RemoveFromShared()`，否则 `RemoveVehicle()`；
  7. 新表入链：已有主人 ⇒ `v->AddToShared(first_shared)`；无主 ⇒ `v->orders = list; list->Initialize(v);`（这一步同时把终点引用计数注册上，与 `Initialize(nullptr)` 的「不注册」严格互补）。
- 新增文件内静态 `R3RMakeOrderListOwnerless()`：旧表被摘成无主（`first_shared == nullptr && num_vehicles == 0`）时**逐条 `UnregisterOrderDestination`**。否则每次「A 的排程被抢走」都会给站点/路点漏一个引用计数（`FreeChain` 对无主表按设计不反注册，因为无主表从未注册过）。
- 成功后 `v->current_order.Free()` + `DeleteOrderWarnings(v)` + `InvalidateWindowClassesData(WindowClass::DepartureBoard)`。

### 7.3 窗口行为

- 列表：`OrderList::Iterate()` 全池，过滤「无主」或「主人是列车」（`IsListable()`）——本窗口暂时只服务列车，飞机/道路车辆的排程不列，避免跨类型分配。
- 行文案：`STR_R3R_SCHEDULE_ROW_OWNED` / `_ROW_OWNED_SHARED`（`{RAW_STRING}` 传预渲染车名 + 命令数 + 共享车辆数）、`_ROW_LOOSE`。目标列车当前指向的那张表加 `"* "` 前缀。
- 目标列车：`WID_SCHED_TARGET_SEL` 下拉，选项 = 「无列车」+ 本公司全部列车；入口传入 `vehicle_sel` 时预选那一列。
- 按钮：`NEW`（弹 `STR_R3R_SCHEDULE_NEW_SEL` 选源表 → 无源表时直接建空表）、`DELETE`（`ShowQuery` 二次确认，标题 `STR_R3R_SCHEDULE_DELETE_TITLE`）、`ASSIGN`（未选表或目标为「无列车」时 `CanEdit()` 置灰）。
- 底部常驻 `_HINT`：明说「只改指针，不复制」。
- 行高取 `NWidgetBase::resize_y`（KI-262 家族：车库/列表容器 `resize.step_*` 为 0 的坑，不能用窗口级 step），滚动用 `Scrollbar::SetCapacityFromWidget` + `GetScrolledRowFromWidget`（无效值 `Scrollbar::npos`）。
- 颜色统一走 `TextColour::FromString`（`TC_FROMSTRING` 已是废弃写法）。
- 单例：`ShowR3RScheduleWindow()` 找不到就新建并 `return`，找到就走 `SetCompanyAndTarget()` 复用窗口 + `BringWindowToFrontById`（避免同一窗口重复实例导致的两份缓存）。

### 7.4 入口

- `src/vehicle_gui_base.h`：`VehicleListIdentifier` 的 action-dropdown 枚举加 `ADI_R3R_SCHEDULE_MGMT`。
- `src/vehicle_gui.cpp` / `src/group_gui.cpp`：下拉列表只在 `VehicleType::Train` 时追加 `STR_VEHICLE_LIST_MANAGE_SCHEDULES`（与「管理挂接分组」并列），两个 `case` 都调 `ShowR3RScheduleWindow(this->owner, this->vehicle_sel)`。**两处 switch 都要写**，少一处就是 `NOT_REACHED()` 崩溃。
- 语言：`english.txt` + `simplified_chinese.txt` 各 21 条 `STR_R3R_SCHEDULE_*` + `STR_VEHICLE_LIST_MANAGE_SCHEDULES`（中英对齐，实测 `MISSING:` 为空）。

### 7.5 已知边界（本轮不做）

1. 无主排程不存 owner，跨公司全局可见（窗口按 `this->owner` 过滤目标列车，但池条目本身不分公司）。
2. 无主排程仍可被删除后其命令里的站点/路点引用随之消失，符合预期；但没有「谁在用这份表」的反向统计（只有 `num_vehicles`）。
3. 分配只对「主车」（`IsPrimaryVehicle()`）开放；多节列车请点链头那一行。
4. 借用态（`r3r_orders_borrowed` / `orders_backup`）的车一律拒绝分配 —— 需要先解挂/解耦把账还清。
5. 池表循环引用（表 A 的 R3R 命令再指回表 A）不拦，按需求 4 由玩家负责，只保证不崩。

## 8. 构建自证（第 417 轮，2026-11-10）

### 8.1 编译错误与修法（连续 4 批）

| 批次 | 报错 | 根因 | 修法 |
| --- | --- | --- | --- |
| 1 | `order_cmd.cpp(4164) error C3861: "return_cmd_error" 找不到标识符` | 该翻译单元没有 `return_cmd_error` 宏（它是 `command_func.h` 的条件宏，这里未生效） | 三处改 `return CommandCost(STR_ERROR_...)` |
| 2 | `order_cmd.cpp(4166/4198/4256) error C2678: 没有找到接受“DoCommandFlag”类型的左操作数的运算符(或没有可接受的转换)` | 三条新命令形参误写成 `DoCommandFlag`（`enum class`，无 `operator&`、无 `.Test`） | 形参改 `DoCommandFlags`（与 `CmdInsertOrder` / `CmdSetRouteOverlayColour` 一致，`DEF_CMD_TUPLE_NT` 展开的 `CommandProc` 本身也是复数类型） |
| 3 | `order_serialisation.cpp(537) error C2065: "vt" 未声明的标识符` | 为支持无主排程把 `VehicleType vt` 挪进了 `if (veh != nullptr)` 作用域，后面 `if (veh == nullptr)` 分支仍用它 | 提到 if 外并默认 `VehicleType::Train`，命中主人时再覆盖 |
| 4 | `r3r_schedule_gui.h(20) error C2146: 语法错误: 缺少“)”(在标识符“company”的前面)`<br>`r3r_schedule_gui.cpp(31) fatal error C1083: 无法打开包括文件: "misc_gui.h"`<br>`r3r_schedule_gui.cpp(77) error C2065: "INVALID_COMPANY" 未声明的标识符`<br>`r3r_schedule_gui.cpp(597/690/693) error C2027: 使用了未定义类型 "CommandTraits<...>"` | ①`CompanyID` 需 `company_type.h`；②本 fork 没有 `misc_gui.h`，`ShowQuery` / `ShowQueryString` 声明在 `textbuf_gui.h`；③本 fork 无 `INVALID_COMPANY`，用 `CompanyID::Invalid()`；④`CommandTraits<Commands::X>` 特化由 `order_cmd.h` 的 `DEF_CMD_TUPLE_NT` 生成，窗口文件漏 include | 四件事逐条修：加 `#include "company_type.h"`（`.h`）、`misc_gui.h` 换 `textbuf_gui.h`、`INVALID_COMPANY` 换 `CompanyID::Invalid()`、加 `#include "order_cmd.h"` |

### 8.2 三件套核对（通过）

- 护栏：因第 3 批修了 `r3r_schedule_gui.h`，判重编；最后一次构建 `[101/101] Linking CXX executable openttd.exe`。
- `build\R3R_incbuild.done` = `EXIT_CODE=0`（2026-11-10 10:41）。
- 日志 `error C` / `fatal error` / `FAILED:` / `build stopped` 计数 **0**。
- 时间戳：`src\r3r_schedule_gui.cpp` → obj 晚于源码；`order_cmd.cpp` / `order_serialisation.cpp` obj 均晚于对应 `.cpp`；`build\openttd.exe` @ 2026-11-10 10:41:46（52 738 048 B）晚于全部源码。
- 产物自证：exe 内命中 `r3r_schedule_pool`（ini key）与 `R3RSchedulePoolWindow`；`build\generated\table\strings.h` 含 `STR_R3R_SCHEDULE_CAPTION = 0xC9B`。
- `read_lints`：`order_cmd.cpp` / `order_serialisation.cpp` / `r3r_schedule_gui.cpp` / `r3r_schedule_gui.h` 全 0 条。
- 发布树（`build-release`）**本轮未编**，停留在第 416 轮版本，不含排程池窗口。

### 8.3 本轮新增的接口事实（下轮省事）

1. 本 fork 无 `misc_gui.h`；`ShowQuery` / `ShowQueryString` 在 `textbuf_gui.h`。
2. 本 fork 无 `INVALID_COMPANY`；用 `CompanyID::Invalid()`（`PoolID` 静态成员）。
3. `CommandTraits<Commands::X>` 只在包含了声明该命令的头（`*_cmd.h`）时才可见，GUI 侧调 `Command<>::Post` 必须 include 对应 `*_cmd.h`。
4. `DoCommandFlag` 是 `enum class`，命令形参一律 `DoCommandFlags`（复数）才支持 `flags.Test(...)`。

### 8.4 待实机复测判据（6 条）

1. 车队列表 / 分组列表的「管理排程」→ 打开池窗口，能看到本存档全部列车排程 + 无主排程，行文案命令数正确。
2. 目标列车下拉选中某列 ⇒ 其 `v->orders` 指向的那一行带 `*` 前缀。
3. 「新建无主排程」建出空表后行数 +1，且该表没有任何车指向它（`*` 不出现）。
4. 「让目标列车指向选中排程」后：该列跑的是新表命令；**原表命令一条不少**（仍是池里的一行，未被复制/清空）——这是"只改指针"的核心判据。
5. 「删除选中排程」只对无主表生效；对有主表操作应弹错误而不是删掉。
6. 反复分配 + 存档往返：不崩、不出现 `first_shared` 空解引用（`GetFirstSharedVehicle()` 空守卫），目标列车读档后仍指向同一张表。
