# 第 427 轮：无主排程编辑器 GUI 与有主订单窗口的「控件/下拉项对齐」+ 路径预览

- 日期：2026-11-12
- 来源：玩家五条报告（原话见 §一）
- 上一轮：`R3R_round426_ownerless_align_goto_select_memo.md`（T3 行高 / T1 前往双语义 / T2 无车辆 tile→order，已编译 EXIT_CODE=0 @22:09:41）
- 状态：**取证中**（本轮先落盘，再取证，再改码）

---

## 一、玩家原话（五条，逐字保留）

> 1，无主排程的编辑窗口右侧有一处镂空
> 2，我说那个"前往"按钮旁边的下拉框有XXX这些，结果你就真只给我一些。有主排程的下拉框有的无主排程都要有啊， 又什么"条件式跳过命令""出发看板经由""路签操作"你别省略啊。
> 3，"前往"按钮的很左边那个"管理命令"按钮也是同理，你省略了很多项（既有的几项还是英文）
> 4，无主排程编辑的那个显示命令负载率的按钮应该是可以不要的
> 5，JGRPP添加的路径预览（也就是在各个命令目的地头顶连线）我希望无主排程也有

**口径要点（从原话提炼，供改码时遵守）**：
- P-1：**有主订单窗口的每一项下拉项，无主编辑器都必须有**（不是"够用就行"的子集）。上一轮我以"无车辆锚点做不到"为由砍掉 `ODDI_GO_TO_NEAREST_DEPOT`，玩家不接受这个理由，必须补回或给出等价物。
- P-2：「管理命令」下拉同理，且**不允许出现英文**（上一轮我在无主编辑器里用的是自己拼的英文/未翻译串）。
- P-3：负载率按钮**删除**。
- P-4：JGRPP 的路径预览（订单目的地连线）**无主编辑器也要有**。

---

## 二、已知锚点（第一组取证）

`src\order_gui.cpp`：

- `enum OrderDropDownID`（`:607-622`）= **有主窗口「前往」下拉的全部 14 项**：
  `ODDI_GO_TO` / `ODDI_GO_TO_NEAREST_DEPOT` / `ODDI_CONDITIONAL` / `ODDI_SHARE` /
  `ODDI_INSERT_FROM_VEHICLE` / `ODDI_TRY_ACQUIRE_SLOT` / `ODDI_RELEASE_SLOT` /
  `ODDI_RELEASE_SLOT_GROUP` / `ODDI_CHANGE_COUNTER` / `ODDI_LABEL_TEXT` /
  `ODDI_LABEL_DEPARTURES_VIA` / `ODDI_DECOUPLE` / `ODDI_GOTO_COUPLE` / `ODDI_WAIT_COUPLE`
  → 对照玩家点名的三项：条件式跳过命令=`ODDI_CONDITIONAL`、出发看板经由=`ODDI_LABEL_DEPARTURES_VIA`、路签操作=`ODDI_TRY_ACQUIRE_SLOT`/`ODDI_RELEASE_SLOT`/`ODDI_RELEASE_SLOT_GROUP`/`ODDI_CHANGE_COUNTER`。
  **上一轮我只实现了 `ODDI_GO_TO` / `ODDI_DECOUPLE` / `ODDI_GOTO_COUPLE` / `ODDI_WAIT_COUPLE` 4 项 ⇒ 缺 10 项（含我主动砍掉的 `ODDI_GO_TO_NEAREST_DEPOT`）。**
- `_order_manage_list_dropdown[]`（`:624`）= 有主窗口「管理命令」下拉的字符串表（**项数与内容待取证**）。
- `R3RScheduleEditorWindow` 起始 `:5492`（第 426 轮锚点，可能因本轮改动位移）。

---

## 三、五条问题的初判（**猜测根因，待验证**）

| # | 现象 | 猜测根因（待验证） | 待取证 |
|---|---|---|---|
| 1 | 右侧有一处镂空 | 无主编辑器复用 `_nested_orders_train_widgets`，其中某个控件（可能是「订单负载率」相关或某个 dropdown 的 plane 位）在无主模式下 `SetDisplayFlag(false)` 了但窗口宽度/位置仍按它算；或复用树上某控件在无主模式的 `UpdateWidgetSize` 里没给最小尺寸 ⇒ 画成空白格 | 需取无主编辑器构造时对 widget 的显示/隐藏操作清单 + 右侧控件树的实际排布 |
| 2 | 「前往」下拉只给了 4 项 | 上轮 `ShowGotoDropDown()` 手工只 push 4 项（自陈"无车辆锚点做不到 `ODDI_GO_TO_NEAREST_DEPOT`"） | 需取有主 `ShowDropDownList` 的完整项构造代码 |
| 3 | 「管理命令」下拉缺项 + 英文 | 上轮无主编辑器该按钮分支是自写的（未复用 `_order_manage_list_dropdown`），且用的字符串是 C++ 硬编码英文 | 需取无主 `WID_O_MANAGE_LIST` 分支 vs 有主同名分支 |
| 4 | 负载率按钮该删 | 该按钮来自复用控件树（有主窗口的 `WID_O_LOAD`？还是 R3R 自加？） | 需确认该按钮的 widget 名与有主语义（若能显示负载率则也许不该删，但玩家要求删 ⇒ 无主模式下隐藏） |
| 5 | 路径预览缺失 | JGRPP 的订单目的地连线由 `OrdersWindow` 的某个 `OnPaint`/overlay 钩子按 `vehicle->orders` 驱动；无主窗口没有 Vehicle ⇒ 没有入口 | 需定位 JGRPP 路径预览的实现函数与数据来源 |

---

## 四、取证记录（按时间追加）

### 4.1 第一组

- `src\order_gui.cpp:607-622` `enum OrderDropDownID` 共 14 项（上文已列全文）。
- `src\order_gui.cpp:624` `_order_manage_list_dropdown[]` 存在。
- 结论：**问题 2 已确认成立** —— 有主 14 项，无主上轮只有 4 项。

### 4.2 第二组：「管理命令」按钮（问题 3）

**有主窗口有两个不同的"管理"按钮，别混**：
- `WID_O_MGMT_LIST_BTN`（`:3805`）—— **列表级**下拉，用 `_order_manage_list_dropdown[]`（`:624`）共 **6 项**：
  `REVERSE`（反转顺序）/ `APPEND_REVERSED`（追加反转）/ `EXPORT`（导出到剪贴板）/ `IMPORT_REPLACE` / `IMPORT_APPEND` / `IMPORT_APPEND_REVERSED`。
- `WID_O_MGMT_BTN`（`:3818-3880` 一带）—— **单条命令级**下拉，项是动态构造的（复制命令、改跳转目标、停车位置近/中/远/通过、道路车辆方向、颜色、插入方式…），内容随当前订单类型变化。

**无主编辑器现状**：`WID_O_MGMT_BTN` 只有一个分支（`:6055` 起），硬编码 3 项 `DUPLICATE` / `MOVE_UP` / `MOVE_DOWN`；`WID_O_MGMT_LIST_BTN` **在无主窗口里根本没有 OnClick 分支**（`search_content` 只有 `:3805` 一处）。

**问题 3 的"既有的几项还是英文"已定性（不再猜）**：无主侧用的 `STR_R3R_SCHEDULE_EDITOR_DUPLICATE` / `_MOVE_UP` / `_MOVE_DOWN` **只存在于 `src\lang\english.txt:4094-4099`，`src\lang\simplified_chinese.txt` 里一条都没有**（`search_content` 0 命中）⇒ 中文环境下 fallback 英文。同一批 `STR_R3R_SCHEDULE_EDITOR_*`（`english.txt:4087-4099`，共 13 条：CAPTION/LIST_TOOLTIP/EMPTY/ADD/ADD_TOOLTIP/DELETE/DELETE_TOOLTIP/DUPLICATE/…/MOVE_DOWN_TOOLTIP）全部缺中文。

### 4.3 第三组：JGRPP 路径预览（问题 5）—— 已定位实现，但**强绑定 Vehicle**

实现全在 `src\viewport.cpp`：
- `struct ViewportRouteOverlay`（`:416`）：持有 `RouteStepsMap route_steps` + `std::vector<DrawnPathRouteTileLine> route_paths`（`:395` 的 `{from_tile,to_tile,order_conditional}`）。
- 唯一数据入口 `ViewportPrepareVehicleRoute()`（`:2974`）：
  ```cpp
  const Vehicle *focused_veh = GetVehicleFromWindow(_focused_window);
  _vp_focused_window_route_overlay.PrepareRouteAndMarkDirtyIfChanged(focused_veh);
  for (auto &it : _vp_fixed_route_overlays) { ... PrepareRouteAndMarkDirtyIfChanged(v); }
  ```
- `GetVehicleFromWindow(const Window *w)`（`:2620`）按 `w->window_class` / `w->window_number` 反查 Vehicle。
- 绘制：`ViewportDrawVehicleRoutePath()`（`:2802`，连线）与 `ViewportDrawVehicleRouteSteps()`（`:3013`，目的地头顶的步骤框），在 `:4370-4373` 处按 `gui.show_vehicle_route_mode != 0 && gui.show_vehicle_route_steps` 调用。
- 固定叠加层 API：`AddFixedViewportRoutePath(VehicleID)`（`:5045`）/ `RemoveFixedViewportRoutePath`（`:5051`）/ `ChangeFixedViewportRoutePath`（`:5062`），都是 **VehicleID** 口径。
- 相关设置：`gui.show_vehicle_route_mode` / `gui.show_vehicle_route` / `gui.show_vehicle_route_steps`。

⇒ **问题 5 的坏消息**：整条链路以 `const Vehicle *` 为唯一输入，无主排程只有 `OrderList*`。要么在 `ViewportRouteOverlay` 上加一条「按 OrderList 准备」的重载（要另找路径计算的起点：目前起点是 `v->tile`），要么给无主编辑器指定"锚点车辆"。**待取证**：`PrepareRoute(const Vehicle*)` / `PrepareRouteSteps(const Vehicle*)` 内部到底读了 veh 的哪些字段（tile / orders / cur_real_order_index / type / IsGroundVehicle…），决定能否用 `OrderList* + TileIndex + VehicleType` 三个参数替代。

### 4.4 第四组：路径预览的内部依赖（决定可行性）

`ViewportRouteOverlay::PrepareRouteSteps(const Vehicle *veh)`（`viewport.cpp:2932`）只读四样：
`veh->GetNumOrders()`、`veh->GetEffectiveOrders()->GetRouteOverlayColour()`、`veh->Orders()`、`order->GetLocation(veh, veh->type == VehicleType::Aircraft)`。
`PrepareRoutePaths(const Vehicle *veh)`（`:2702`）额外读一个 `from_tile`（车当前位置）作为连线起点。

⇒ **好消息**：`route_steps`（目的地上方步骤框）只需要 `OrderList*` + `VehicleType`，**完全不需要位置**；
`route_paths`（连线）需要一个起点 tile，无主排程没有车，可退化为「以第一条订单的目的地为起点」或干脆按订单顺序首尾相接。
⇒ **唯一硬依赖**是 `Order::GetLocation(const Vehicle *v, bool aircraft)`，它签名吃 `Vehicle*`（`OT_IMPLICIT` 等隐式订单要用车位置）。**待取证**：`GetLocation` 对 `OT_IMPLICIT` 之外的类型是否完全不用 `v`。

**改动面评估（重要）**：支持问题 5 必须动 `src\viewport.cpp` + `src\viewport_func.h`（新增 API）⇒ 命中 KI-183 护栏（改 `.h` 触发全量重编 692 步，约 16~20 分钟）。

### 4.5 第五组：待查

- 问题 1「右侧镂空」与问题 4「负载率按钮」疑似同源（都在右侧 pane），尚未取证。

---

## 五、改码清单（已落地部分）

### 5.1 问题 2/3：下拉项与文字编辑（`src\widgets\order_widget.h` + `src\order_gui.cpp`）

- `order_widget.h`：有主那批 `OrderWidgets` 里新增 `WID_O_TEXT_LABEL`（文本标签「文字」按钮）与
  `WID_O_DEPARTURE_VIA_TYPE`（发车板标签「经由类型」下拉）；新增 `WID_O_SEL_OCCUPANCY_TOGGLE`
  （包住 `WID_O_OCCUPANCY_TOGGLE` 的 `NWID_SELECTION`，供无主窗口整块隐藏）。
- `order_gui.cpp` 的 `R3RScheduleEditorWindow`：
  - 新增 `QUERY_LABEL_TEXT` 查询常量 + `ModifySelectedOrderText(mof, text)`（走
    `Command<Commands::ModifyR3RLooseOrder>::Post(..., mof, 0, {}, text)`，第 6 参即文本，不需要车辆锚点）。
  - `OnClick`：`WID_O_TEXT_LABEL` → 校验 `IsType(OT_LABEL)` 后 `ShowQueryString(order->GetLabelText(), ...)`；
    `WID_O_DEPARTURE_VIA_TYPE` → 两项下拉（显示/移除经由，映射 `OLST_DEPARTURES_VIA` /
    `OLST_DEPARTURES_REMOVE_VIA`，`DDSF_SHARED`）。
  - 下拉选择处理新增 `WID_O_DEPARTURE_VIA_TYPE` → `ModifySelectedOrder(MOF_DEPARTURES_SUBTYPE, index)`。
  - `OnQueryTextFinished`：`QUERY_LABEL_TEXT` 只有「取消」保留旧文本，**空串是合法值**（清空标签文字）。
  - `UpdateWidgetStates`：`case OT_LABEL:` 按 `GetLabelSubType()==OLST_TEXT` 互斥——文本标签禁用经由下拉、
    发车板标签禁用文字按钮；无选中/非标的订单时两者都禁用。
- 无主 `ShowGotoDropDown()` 已补齐 14 项（`ODDI_SHARE` / `ODDI_INSERT_FROM_VEHICLE` 因需要车辆锚点置灰）。
- 无主「管理命令」下拉改用 `STR_ORDER_*` 中文串，不再自拼英文。

### 5.2 问题 4：负载率按钮（KI-618）

`R3RScheduleEditorWindow` 构造函数里在 `FinishInitNested()` 之前
`GetWidget<NWidgetStacked>(WID_O_SEL_OCCUPANCY_TOGGLE)->SetDisplayedPlane(SZSP_NONE);`
⇒ 无主窗口连按钮带列一起消失，不再残留空白。

### 5.3 顺带修掉的 bug

`MOF_COLOUR` 的下拉选择判据原为 `index >= 0x100 && index < 0x200`，而「默认颜色」项索引是
`0x100 + to_underlying(Colours::Invalid)`，正好落在排他上界外 ⇒ 选「默认颜色」无反应。改为
`index <= 0x100 + to_underlying(Colours::Invalid)`。

### 5.4 问题 5：路径预览（未落地，见 §七）

## 六、构建与复测判据

### 6.1 构建坑（本轮实锤，已登记 KI-620）

第一次构建 `EXIT_CODE=1`，报错在**生成文件** `build\generated\script\api\script_window.hpp`：

```
script_window.hpp(2375): error C2059: 语法错误:“常数”
...
```

根因：`src\script\api\...` 的 widget 枚举生成器（脚本 API）**把注释里的非 ASCII 字符替换成换行**。
我在 `order_widget.h` 写的 `WID_O_SEL_OCCUPANCY_TOGGLE, ///< R3R (第 427 轮, KI-618): ...`
被生成为

```
WID_O_SEL_OCCUPANCY_TOGGLE ... ///< R3R (
 427 
            = to_underlying(::OrderWidgets::),
```

即 `第` / `轮` 各换行一次，把注释后半截挤成代码 ⇒ 整个 `::OrderWidgets` 枚举解析失败，后续全部重定义。
⇒ **`src\widgets\*.h` 里的注释一律只能写 ASCII**（`///<` 行注释尤其致命）。已把本文件三处中文注释
（`round 427` / `round 421` / `round 424`）改成英文后重新构建。

### 6.2 复测判据（问题 2/3/4）

1. 无主编辑器选中文本标签订单：文字按钮可用、点开是查询框，改完列表与订单窗口一致；清空后标签无文字。
2. 选中发车板标签订单：经由下拉可用，两项切得动，列表/地图表现与有主窗口一致。
3. 两种标签互斥：文本标签时经由下拉灰、发车板标签时文字按钮灰；选中非标签订单两者都灰。
4. 「前往」下拉 14 项俱在（`ODDI_SHARE`/`ODDI_INSERT_FROM_VEHICLE` 置灰可接受，属无车辆语义限制）。
5. 「管理命令」下拉全中文。
6. 右侧原「负载率」按钮与列彻底消失，无镂空（问题 1 一并复看）。
7. 颜色下拉「默认颜色」项可选中。

## 七、问题 5（路径预览）落地设计

### 7.1 依赖取证（已确认）

- `ViewportRouteOverlay::PrepareRouteSteps(const Vehicle *veh)`（`viewport.cpp:2932`）只读：
  `veh->GetNumOrders()`、`veh->GetEffectiveOrders()->GetRouteOverlayColour()`、`veh->Orders()`、
  `order->GetLocation(veh, veh->type == VehicleType::Aircraft)`。
- `PrepareRoutePaths(const Vehicle *veh)`（`:2702`）额外读连线起点 `veh->tile` / `veh->direction`。
- `PrepareRoutePathsConditionalOrder(...)` 递归时读 `veh->GetEffectiveOrders()` / `veh->GetOrder(idx)`。
- `Order::GetLocation(const Vehicle *v, bool airport)`（`order_cmd.cpp:1122`）只在两个分支碰 `v`：
  `if (airport && v->type == Aircraft)` 与 `OT_GOTO_DEPOT` 的 `v->type == Aircraft`。
  ⇒ 只要把这两处改成 `v != nullptr && v->type == ...`，`v == nullptr` 就是安全的（松散排程恒为列车）。

### 7.2 方案（尽量不新增 .h 依赖）

1. `ViewportRouteOverlay` 增两个成员 `const OrderList *route_orders` / `VehicleType route_vtype`；
   三个 `Prepare*` 入口在 `veh == nullptr` 时改用这两个成员（`veh != nullptr` 时优先车，且把
   `route_orders` 清空，避免上一次无主绘制残留）。
2. 新增 `PrepareOrderListRouteAndMarkDirtyIfChanged(const OrderList *, VehicleType)`：与车辆版同款
   （比较 colour / force_change 后分别准备 paths 与 steps）。
3. `ViewportPrepareVehicleRoute()`：`focused_veh == nullptr` 且注册的无主排程窗口 == `_focused_window`
   时走新入口，否则维持原行为。⇒ 焦点离开编辑器即自动消失，无需 OnBlur 钩子。
4. 注册接口放在 `viewport.cpp`，在 `order_gui.cpp` 用 `extern` 手写声明（**不改 .h** ⇒ 下一轮构建仍是
   增量；OpenTTD 有 `extern void ViewportPrepareVehicleRoute();` 先例）。
   `R3RScheduleEditorWindow` 构造末尾注册 `(this, this->schedule, this->vtype)`；
   析构里 `if 注册窗口 == this` 时清空。
5. 连线起点：无主排程没有车 ⇒ `route_paths` 退化为「从第一条订单目的地起，按订单顺序首尾相接」；
   `route_steps`（目的地头顶的步骤框）与有主完全一致。
6. 是否显示沿用既有设置（`gui.show_vehicle_route_mode` / `_show_vehicle_route` / `_show_vehicle_route_steps`），
   不新增开关。

### 7.3 未确认项

- U-1：`route_steps` 的编号是否按 `veh->cur_implicit_order_index` 着色（若按当前订单高亮，无主模式需要
  一个「当前订单」= 选中订单，否则全部按普通色）。取证点：`PrepareRouteSteps` 内是否读 `cur_*_index`。
- U-2：`PrepareRoutePaths` 的 `from_tile` 用哪个位置最直观（第一条订单目的地 / 编辑器上次视野中心）。

