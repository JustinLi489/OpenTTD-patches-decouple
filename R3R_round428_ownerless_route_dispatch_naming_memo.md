# 第 428 轮：无主排程路径预览 / 车站调度池去重 / 排程命名全存档唯一

时间：2026-11-12 / 2026-11-13　状态：三问全部已实现，待构建自证 + 游戏内复测

## 零、玩家原话（逐字）

1. 「无主排程的路径预览（也就是目的地头顶的连线），不知是你没有接线，还是我没法触发，总之，请你检查。」
2. 「车站用于分配调度的调度池，在往里面添加调度时有以下问题：我只需要按调度添加，而非按列车添加，因为我发现两列共享排度的列车会让调度池添加的下拉框显示两次同一个调度。而此时，无主的排程却压根不显示。」
3. 「排程命名应当全存档禁止重复（对于现有的重复的排程命名，只要不崩就行）」

## 一、问题定性（先登记，后取证）

### P1 无主排程的路径预览（KI-619 复报）

- 上一轮（第 427 轮）只在 `R3R_round427_ownerless_gui_parity_memo.md` §七 写了落地设计，**未改任何代码**
  ⇒ 现状是「没有接线」而不是「玩家没触发」。本轮按该设计实现。
- 设计要点（移植自 §七）：`ViewportRouteOverlay` 增 `route_orders`/`route_vtype` 成员；
  `PrepareRoutePaths/Steps(nullptr)` 时改用它们；`ViewportPrepareVehicleRoute()` 在
  `GetVehicleFromWindow(_focused_window) == nullptr` 且注册的无主编辑器窗口 == 焦点窗口时走新入口；
  `R3RScheduleEditorWindow` 构造注册、析构注销；`Order::GetLocation(v, airport)` 的两处 `v->type`
  改成 `v != nullptr && ...` 以实现 nullptr 安全（无主排程恒为列车）。
- 待验证：`PrepareRouteSteps` 是否读 `cur_*_index` 做高亮（无主模式没有"当前命令"，需退化为普通色）。

### P2 车站调度池：按调度添加、去重、无主排程不显示

- 玩家口径（关键）：**下拉框以「调度（排程）」为单位列出，一辆列车一个条目是错的**；
  两列车共享同一份排程时，下拉里出现两条一模一样的调度 ⇒ 需按排程去重。
- 「无主的排程压根不显示」⇒ 调度池的候选来源目前只遍历车辆（`Vehicle::Iterate`），
  没有遍历松散排程池（`OrderList::Iterate` / R3R 松散排程容器），需补。
- 取证点待定：`src\schdispatch.h`（车站调度池数据结构）、`station_base.h`、
  以及添加调度的 GUI（`station_gui.cpp` 的 Dispatch 页 / `order_gui.cpp`）。

### P3 排程命名全存档唯一

- 玩家口径：命名**全存档唯一**；存量重复不崩即可（不做强制改名/不阻断读档）。
- 需要：命令层校验重名（`CmdR3RSetOrderListName` 之类）⇒ 重名直接返回错误；
  存档读入时不做唯一性强制（容忍旧档重复）；查找/引用用 ID 不用名字。

## 二、待取证清单

- [ ] `OrderList` 的名称字段与设置命令（名称命令名、错误串）
- [ ] 松散排程的容器（无主排程如何枚举：`OrderList::Iterate` 是否覆盖无主表）
- [ ] 车站调度池的候选枚举代码位置与当前去重口径
- [ ] `PrepareRouteSteps` 是否使用当前命令索引

## 三、复测判据

1. 打开无主排程编辑器（未选中任何列车），地图上出现目的地连线与目的地头顶的序号框；
   焦点离开该窗口后连线消失；`gui.show_vehicle_route*` 三项设置照旧生效。
2. 两列车共享同一份排程时，车站调度池的「添加调度」下拉**只出现一条**该排程；
   无主排程也出现在候选里。
3. 给两份排程起同一个名字 ⇒ 第二个命令被拒绝（有错误提示），存档内名字不重复；
   旧档里的重复名字能正常读入且不崩。

## 四、取证与实现记录

### 4.1 取证结论（回填 §二）

- 排程（OrderList）没有独立的「松散排程容器」——**一张 OrderList 就是一"份排程"**，
  放在 OrderList 对象池里，`OrderList::Iterate()` 覆盖全部（含 `first_shared == nullptr`
  的无主表）。池下标记为 `ol->index.base()`。
- `OrderList::GetIfValid(OrderListID(idx))` 对已释放槽位返回 `nullptr`（`Pool::GetRaw`
  解引用前已置空），可以安全当"表还在不在"的判据。
- 名称字段 = `OrderList::r3r_schedule_name`，读写走 `GetR3RScheduleName()` /
  `SetR3RScheduleName()`；设置命令 = `CmdR3RRenameSchedule(flags, ol_index, name)`。
- 车站调度池条目旧结构 = `{host: VehicleID, limit, issued}`，候选枚举在
  `station_gui.cpp` 的 `WID_SD_ADD` 分支里遍历 `Vehicle::Iterate()` ⇒ 玩家报的
  「两列共享同表 ⇒ 下拉出现两条」「无主排程不显示」都是这个原因。
- 无主排程路径预览：上一轮只写了设计，`ViewportRouteOverlay` 里确实**只有**
  `GetVehicleFromWindow(_focused_window)` 一条入口（无 `route_orders` 字段）。

### 4.2 P1 无主排程路径预览（已实现）

- `src\viewport.cpp`：`ViewportRouteOverlay` 新增 `route_orders`(`const OrderList *`) 与
  `route_vtype`(`VehicleType`)；`PrepareRoutePaths()` / `PrepareRouteSteps()` 在
  `route_orders != nullptr` 时改按该表出图（不再依赖 `Vehicle *`），`PrepareRouteSteps`
  在没有"当前命令索引"时退化为普通色（无主表没有 cur_*_index）。
- `ViewportPrepareVehicleRoute()`：`GetVehicleFromWindow(_focused_window) == nullptr` 且焦点
  窗口正是注册的无主编辑器窗口时，走 `route_orders` 入口。
- `r3r_schedule_gui.cpp` 的 `R3RScheduleEditorWindow` 构造时注册、析构时注销。
- `Order::GetLocation(v, airport)` 的两处 `v->type` 改为 `v != nullptr && ...`，
  使 `nullptr` 安全（无主模式恒按列车处理）。

### 4.3 P2 调度池条目改为「按排程」（已实现，KI-621）

结构（`src\station_base.h`）：

```
struct R3RDispatchPoolEntry {
    VehicleID host = VehicleID::Invalid();   ///< 模板车缓存（显示用；无主排程为 Invalid）
    uint32_t  schedule = R3R_DISPATCH_NO_SCHEDULE;  ///< OrderList 池下标（条目身份）
    uint16_t  limit = 1;
    uint16_t  issued = 0;
};
```

API 签名全部改为按排程：`R3RDispatchPoolAdd(yard, schedule, limit)`、
`R3RDispatchPoolPick(yard, except_schedule, uint32_t &out_schedule)`、
`R3RDispatchPoolAcquire(schedule)` / `Release(schedule)`。

- `src\station_cmd.cpp:2281` `R3RDispatchEntryScheduleIndex(e)`：`e.schedule` 有效就用它；
  旧档条目（`R3R_DISPATCH_NO_SCHEDULE`）回退 `host->orders->index.base()`，这就是旧档
  兼容路径，**不需要**迁移代码。
- `:2298` `R3RDispatchScheduleUsable(schedule, out_orders)`：`OrderList::GetIfValid` +
  非空表。按第 417 轮拍板，**不**再检查"表里有没有解挂/挂接命令"。
- `:2366` `Add`：按 `schedule` 去重；`host` 由 `ol->GetFirstSharedVehicle()` 推导
  （无主表 ⇒ Invalid，条目照样能进池）。
- `:2410` `Pick` / `:2428` `Acquire` / `:2443` `Release`：一律按
  `R3RDispatchEntryScheduleIndex(e)` 比较；`Acquire` 改为"按场顺序找第一个还有坑位的场"，
  旧写法遇到第一个满场就直接失败。
- `:2757` `CmdR3RAddDispatchPoolEntry(flags, station, yard, schedule, limit)`：用
  `R3RDispatchScheduleUsable` 校验，归属用 `orders->GetR3RScheduleOwner()` +
  `R3RScheduleAccessibleBy`（公开排程允许外公司登记）。
- `src\train_cmd.cpp` 借用端：`Pick` 取 `schedule` ⇒ `OrderList::GetIfValid` 取表 ⇒
  `Acquire(schedule)`；`except_schedule` 用 `v->GetEffectiveOrders()` 的池下标。
  归还端：先取 `borrowed->index.base()` 作 `pool_schedule`，再逐站 `Release(pool_schedule)`
  —— 旧代码只在 `host != Invalid` 时归还，**无主排程会漏还坑位**。
- `src\station_gui.cpp`：新增 `R3RDispatchEntryScheduleGui(e)`（`e.schedule` → 旧档回退
  `host->orders`）；`R3RDispatchScheduleLabel(const OrderList*)`（无名时：有首车用
  `STR_VEHICLE_NAME`，无主表显示「未命名排程 #池下标」，表没了显示
  `STR_R3R_DISPATCH_POOL_ENTRY_GONE`）；`WID_SD_ADD` 候选改为遍历 `OrderList::Iterate()`，
  下拉值 = `ol->index.base()`，按 `e.schedule` 去重。列表**不再**过滤失效条目
  （排程被释放后条目仍在池里占着 limit/issued，隐藏起来就再也删不掉）。
- `src\sl\station_yard_sl.cpp`：SYRD 升 **v8**（`SYRD_V8_MAGIC = 0xFFFFFF07`），池条目
  3 字 → 4 字（末字 = OrderList 池下标 + 1，0 = 未记录）；`R3RUnpackPool(in, p, has_schedule)`
  兼容 v2~v7。解码阶段**只做数值钳制**，不查车辆池/排程池（加载顺序不保证）。

### 4.4 P3 排程命名全存档唯一（已实现）

- `src\order_cmd.cpp:4701-4706`：`CmdR3RRenameSchedule` 在存取权限校验之后，
  名称非空时遍历 `OrderList::Iterate()` 找同名（区分大小写，与显示一致），命中即
  `return CommandCost(STR_ERROR_R3R_SCHEDULE_NAME_TAKEN)`。空名例外（= 自动标签，
  允许多份）。
- 校验写在命令的 check 阶段（`CommandCost` 早返回且无副作用），因此 GUI 的
  `Post(STR_ERROR_CAN_T_DO_THIS, ...)` 会把具体错误串弹出来，无需改 GUI。
- 读档不做唯一性强制（旧档存量重名照常读入，不崩、不自动改名），符合玩家口径。

### 4.5 构建

- 第一次后台构建 `EXIT_CODE=1`：`station_gui.cpp(3613): error C2065:
  "STR_R3R_DISPATCH_POOL_ENTRY_GONE": 未声明的标识符` —— 我误以为该串已存在。
  已在 `english.txt` / `simplified_chinese.txt` 的 `STR_R3R_DISPATCH_POOL_ENTRY` 后面
  补上该串，其余错误 0 条。
- 语言表变更 ⇒ 护栏会判全量，重新构建中（见 §五 自证）。
