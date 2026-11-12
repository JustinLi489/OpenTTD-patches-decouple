# 第 426 轮：无主排程编辑器与订单窗口的两处行为不齐（REG-424-B 续）

> 落盘时间：2026-11-12，紧接第 425 轮「整树复用 `_nested_orders_train_widgets`」之后。
> 本文件是本轮唯一可信状态载体；上下文被压缩后按本文件恢复，不得重跑取证。
> **先落盘再取证/改码**（用户长期规则：收到报告第一时间写备忘 + KI）。

## 一、玩家原话（2026-11-12）

> 无主排程的编辑窗口还是有一些和普通的编辑窗口不一样：
> 1，选择"前往"时，假如点击"前往"，就是普通的GOTO类型命令，并且点击地图上的车站设施就可以前往，
> 而不是点进去出现一个下拉框，叫你在下拉框选车站，假如点旁边的下拉框，就出来一堆什么
> "前往最近的车库""前往挂接"之类的东西。目前无主排程编辑窗口没有实现。
> 2，现有的有主排程编辑者是可以选中已有的调度命令然后给它编辑属性的，但是无主排程的编辑窗口
> 压根就选不中已有的调度命令。
> 这些不符合我"向既有对齐"的需求

## 二、两个问题拆解（口径 REG-426-A：完全照抄订单窗口的交互）

### 问题 1：`WID_O_GOTO`（前往）按钮的两种点击语义未实现

订单窗口的真实交互（**待逐一核实行号后回填 §三**）：

- **点按钮主体** ⇒ 进入"点地图选目标"模式（`SelectOrderCmd` / `OrderClick_Goto`，进入
  `this->goto_type` 状态），此时**点击地图上的车站/车站设施**就新建一条 `OT_GOTO_STATION`
  命令，点车库就是 `OT_GOTO_DEPOT`，点路点就是 `OT_GOTO_WAYPOINT`。
- **点旁边的小箭头**（`NWID_BUTTON_DROPDOWN` 的下拉部分）⇒ 弹出 "前往最近的车库 / 前往挂接 /
  前往等待挂接 / 前往解挂 …" 那一类固定条目。

无主编辑器第 425 轮落地时把 `WID_O_GOTO` **当普通按钮直接弹下拉**（或根本没接地图点击），
与订单窗口不一致，必须按 `NWidgetButtonDropdown` 的原生分支重写：
`OnClick` 里只有 `widget == WID_O_GOTO && this->IsWidgetLowered(WID_O_GOTO)` 之外的按压区
才进 `ShowDropDownMenu`，主体按压走 `SelectOrderCmd`。

### 问题 2：列表里的既有命令「选不中」

有主窗口点列表行 ⇒ `OrderClick_OrderList()` ⇒ 设 `this->selected` + `SetDirty()`，
之后参数行/按钮状态全部按该条命令刷新。

无主编辑器选不中，可能原因（**待验证，勿直接当结论**）：

- G-1：`R3RScheduleEditorWindow::OnClick` 里没有 `WID_O_ORDER_LIST` 分支（第 425 轮重写时
  只处理了按钮类 widget，列表点击落空）；
- G-2：列表分支存在但写入的不是同一个 `this->selected` 字段，或没调 `SetDirty()`；
- G-3：`GetNumOrders()` 恒 0 / `OrderList` 视图函数接错，导致命中行后取不到订单；
- G-4：`WID_O_ORDER_LIST` 在 `UpdateWidgetSize` / `OnPaint` 里没有走订单窗口的绘制函数
  （行高、点击带回的 index 换算不一致）。

## 三、取证（已回填，行号锚定 `src\order_gui.cpp`）

### 3.1 无主编辑器（`struct R3RScheduleEditorWindow`，始于 :5492）

| 项 | 行号 | 原文片段 / 读数 | 判定 |
| --- | --- | --- | --- |
| 构造 | :5698-5709 | `this->vtype = VehicleType::Train;` + `this->vscroll = this->GetScrollbar(WID_O_SCROLLBAR);` | 只有 `Train`，无 `goto_type` 之类状态字段 |
| `UpdateWidgetSize` | :5721-5729 | `size.height = std::max<uint>(size.height, GetCharacterHeight(FontSize::Normal) * 6);` / `resize.width = 1;` / **`resize.height = 1;`** / `fill.height = 1;` | **问题 2 的真根因（见 3.3）** |
| `OnResize` | :5731-5734 | `this->vscroll->SetCapacityFromWidget(this, WID_O_ORDER_LIST, framerect.top + framerect.bottom);` | 容量按 widget 总高算，与行高无关 ⇒ 滚动条本身正确 |
| `OnPaint` | :5736-5742 | `vscroll->SetCount(...)` → `UpdateButtonStates()` → `UpdateParameterWidgets()` → `DrawWidgets()` | 参数行每帧刷新，T4 不需要动 |
| `DrawWidget` | :5744-5767 | `const int line_height = GetCharacterHeight(FontSize::Normal) + WidgetDimensions::scaled.vsep_normal;`，逐行 `DrawOrderString(nullptr, order, i, y, i == this->selected, false, ir.left, middle, ir.right, this->vtype);` | 绘制侧行高 = charHeight + vsep，**与点击侧使用的 1px 行高不一致** |
| `OnClick` | :5769-5778 | **已有** `case WID_O_ORDER_LIST`：`const int row = this->vscroll->GetScrolledRowFromWidget(pt.y, this, WID_O_ORDER_LIST, WidgetDimensions::scaled.framerect.top); this->selected = (row != INT_MAX && row < this->GetNumOrders()) ? row : -1; this->UpdateButtonStates(); this->SetDirty();` | 分支**不缺**，G-1/G-2 否定 |
| `OnClick` `WID_O_GOTO` | :5883-5885 | `case WID_O_GOTO: this->ShowAddOrderDropDown(); break;` | **问题 1 根因：主体/箭头不分，直接弹车站列表** |
| `ShowAddOrderDropDown` | :5684-5695 | 遍历 `Station::Iterate()`，`if (!st->facilities.Any(StationFacility::Train)) continue;` 后 `ShowDropDownMenu(this, list, -1, WID_O_GOTO, ...)` | 这就是玩家说的"叫你下拉框选车站" |
| `OnDropdownSelect` `WID_O_GOTO` | :5918-5932 | `const StationID station = StationID(index);` → `MakeGoToWaypoint` / `MakeGoToStation` + `SetStopLocation` → `DoCommandP<Commands::InsertR3RLooseOrder>(...)` | 下拉项语义 = 车站 ID，改下拉内容时必须同步改这里 |

### 3.2 有主订单窗口（`OrdersWindow`，对照基准）

| 项 | 行号 | 原文片段 / 读数 |
| --- | --- | --- |
| `OnClick` `WID_O_GOTO` | :3811 起 | 用 `this->GetWidget<NWidgetLeaf>(widget)->ButtonHit(pt)` 区分**主体 / 箭头**：主体走 `OrderClick_Goto(OPOS_GOTO)`（已处于该状态则 `ResetObjectToPlace()`），箭头走 `ShowDropDownMenu`（含"前往最近的车库/前往挂接/等待挂接/解挂/条件跳转/共享/插到车辆…"） |
| `OrderClick_Goto(type)` | :2119 起 | `SetObjectToPlaceWnd(ANIMCURSOR_PICKSTATION, PAL_NONE, HT_RECT | HT_VEHICLE, this)` 一类的选目标态，并设 `this->goto_type` |
| `OnPlaceObject` | :4799 起 | `OPOS_GOTO` 分支用 `GetOrderCmdFromTile(...)` 把鼠标下的 tile 转成 `Order`（车站/车库/路点），再 `InsertNewOrder` |
| `GetOrderCmdFromTile` | :1700 起 | tile ⇒ `Order`：车库 `MakeGoToDepot`、路点 `MakeGoToWaypoint`、车站 `MakeGoToStation` |
| 按钮按下态 | :3228 | `SetWidgetLoweredState(WID_O_GOTO, this->goto_type != OPOS_NONE && this->goto_type != OPOS_COND_VIA);` |

### 3.3 问题 2 的根因（已做实）

`Window::GetScrolledRowFromWidget(pt, this, WID_O_ORDER_LIST, top)` 的 `line_height` 参数缺省为负值，
按文档语义「**负值 ⇒ 用该 widget 的 `resize_y`**」。而无主编辑器在 `UpdateWidgetSize`（:5727）把
`resize.height` 写成 **1**，于是：

```
row = (pt.y - rect.top - top) / 1 + vscroll->GetPosition()
```

点击落在第 3 行时 `row` 已是几十，`row < GetNumOrders()` 几乎恒假 ⇒ `selected = -1` ⇒
"压根选不中"。绘制侧用的是 `charHeight + vsep_normal`（:5758），所以**视觉正常但命中全错**，
这也解释了玩家"能看见列表、就是选不中"的描述。

修法（R-2）：`resize.height` 改成与绘制一致的行高，
`DrawWidget` 的 `line_height` 改为取 `this->GetWidget<NWidgetBase>(WID_O_ORDER_LIST)->resize_y`
（保证命中与绘制共用同一几何）。

## 四、根因结论（取代原猜测 S-1/S-2）

- **R-1（问题 1，成立）**：`OnClick` 的 `WID_O_GOTO` 只做了 `ShowAddOrderDropDown()`。
  缺三样东西：①`NWidgetLeaf::ButtonHit(pt)` 的主体/箭头分流；②选目标态状态字段（等价
  `goto_type`）+ `SetObjectToPlaceWnd`；③`OnPlaceObject` / `OnPlaceObjectAbort` 回调，
  把鼠标下的 tile 转成 `Order` 并 `InsertR3RLooseOrder`。
- **R-2（问题 2，成立）**：不是"没搬 `WID_O_ORDER_LIST` 分支"（分支在 :5772 好好的），
  而是 `UpdateWidgetSize` 把 `resize.height` 写成 `1`，使 `GetScrolledRowFromWidget` 的
  **命中行高 = 1px**，任何点击都换算成远超 `GetNumOrders()` 的行号。
- 原 S-2（"漏搬列表点击"）**证伪**；原 G-1/G-2/G-3 **证伪**；G-4（行高/换算不一致）**成立且是唯一原因**。

## 五、改码清单（按依赖顺序）

- **T3（先做，风险最低）**：`UpdateWidgetSize` 的 `resize.height` 改成
  `GetCharacterHeight(FontSize::Normal) + WidgetDimensions::scaled.vsep_normal`；
  `DrawWidget` 的 `line_height` 改取 widget 的 `resize_y`，让绘制/命中同源。
- **T1**：新增成员 `bool placing_goto`（+ `bool placing_goto_couple`），
  `OnClick` 的 `WID_O_GOTO` 用 `ButtonHit(pt)` 分流：主体 = 切换选目标态
  （`SetObjectToPlaceWnd` + 按钮按下态），箭头 = 弹"前往最近的车库/前往挂接/等待挂接/解挂"下拉。
- **T2**：新增 `OnPlaceObject(pt, tile)`（`placing_goto` ⇒ `GetLooseOrderCmdFromTile(vtype, company, tile)`
  ⇒ `InsertR3RLooseOrder`；`placing_goto_couple` ⇒ 车站/车库 `MakeGoToCouple`）与
  `OnPlaceObjectAbort()`（清状态 + 抬起按钮）。无主排程没有车辆，所以不能直接用
  `GetOrderCmdFromTile(v, tile)`，需按 `vtype`/`GetCompany()` 复制一份 vehicle-less 版本。
- **T4**：`OnPaint` 已每帧 `UpdateParameterWidgets()`，无需改动；只需在 `OnPlaceObject`
  成功插入后 `SetDirty()`。
- `OnDropdownSelect` 的 `WID_O_GOTO` 分支改为按 `ODDI_*` 语义处理（原来按 `StationID`），
  玩家点箭头后选"前往最近的车库"等条目时插入对应命令。

## 六、构建与复测判据（已回填，2026-11-12）

### 6.1 落地结果（仅 `src\order_gui.cpp`，未碰任何 `src\*.h`、未改 lang）

按 §五 的依赖顺序 T3 → T1 → T2 全部落地：

- **T3（行高）**：`UpdateWidgetSize()` 的 `WID_O_ORDER_LIST` 分支改
  `resize.height = GetCharacterHeight(FontSize::Normal) + WidgetDimensions::scaled.vsep_normal;`
  （先前为 `1`），并改成 `size.height = max(size.height, resize.height * 6)`（先算行高、再取 6 行最小高，顺序不能反）。
  `DrawWidget()` 的 `line_height` 改取 `this->GetWidget<NWidgetBase>(WID_O_ORDER_LIST)->resize_y`（下限 1），
  绘制与命中同源。
- **T1（前往按钮双语义）**：新增成员 `bool placing_goto` / `bool placing_goto_couple`；
  `OnClick()` 的 `case WID_O_GOTO` 用 `GetWidget<NWidgetLeaf>(widget)->ButtonHit(pt)` 分流
  （主体 `TogglePlacingGoto()`、箭头 `ShowGotoDropDown()`）；新增
  `CancelPlacingGoto()` / `StartPlacingGoto(bool couple)` / `TogglePlacingGoto()` /
  `ShowGotoDropDown()` / `InsertFixedOrder(const Order&)`。
  `UpdateButtonStates()` 末尾统一刷新按下态（`SetWidgetLoweredState(WID_O_GOTO, placing_goto || placing_goto_couple)`），
  覆盖选点成功 / ESC / `ResetObjectToPlace()` 全部退出路径。
- **T2（无车辆 tile→order）**：新增文件内静态
  `GetLooseOrderCmdFromTile(VehicleType vtype, CompanyID company, TileIndex tile)`（`order_gui.cpp:1802`），
  复制 `GetOrderCmdFromTile()` 全逻辑，仅把车辆成员换成 `vtype`/`company` 参数，并跳过
  「道路车库 vs 车辆兼容道路类型」校验（无车辆可判；无主表本无道路车辆）。
  配合新增 `OnPlaceObject(pt, tile)` / `OnPlaceObjectAbort()`，插入统一走
  `DoCommandP<Commands::InsertR3RLooseOrder>(InsertLooseOrderCmdData(schedule.base(), insert_pos, order), STR_ERROR_CAN_T_DO_THIS)`。

### 6.2 与 §五 原文的偏差（如实登记）

1. **下拉项不含「前往最近的车库」**：该语义（`OrderClick_GoToDepot(..., NEAREST)`）依赖
   `Vehicle` 当前所在格做最近车库搜索，无主排程没有车辆锚点，无法实现。`ShowGotoDropDown()`
   因此只保留 4 项：`ODDI_GO_TO`（前往站点/路点/车库）、`ODDI_DECOUPLE`（解挂）、
   `ODDI_GOTO_COUPLE`（前往挂接）、`ODDI_WAIT_COUPLE`（等待挂接）。
2. **原 `ShowAddOrderDropDown()`（列出全部自有车站）已被删除**（`search_content` 全仓 0 引用），
   其功能由「主体点地图选站」+「箭头下拉固定类型」取代。

### 6.3 构建自证（内测树，2026-11-12）

复用既有入口 `R3R_fullbuild.cmd inc detach`（**未新建任何 `.cmd`**）：

- `build\R3R_incbuild.guard.log` = `GUARD: incremental is safe (no header/lang file is newer than the newest object)`；
- `build\R3R_incbuild.done` = **`EXIT_CODE=0`**；
- 日志 `[3/3] Linking CXX executable openttd.exe`（`[2/3]` 为重编 `order_gui.cpp.obj`）；
  `error C` / `fatal error` / `FAILED:` / `build stopped` 计数 **0**；
- 时间戳链：`src\order_gui.cpp` 22:06:51 → `order_gui.cpp.obj` 22:07:42 → `build\openttd.exe`
  **2026-11-12 22:09:41（52 845 568 B）**（旧 exe 52 842 496 B，增量 +3 072 B）；
- `read_lints` 对 `src\order_gui.cpp` **0 条**；
- 发布树 `build-release` 未重编（按口径只在玩家明确要求时编）。

### 6.4 复测判据

1. 点「前往」**主体** ⇒ 按钮按下态、光标变选目标态、日志无报错；点地图车站/路点/车库/工业
   ⇒ 新增一条前往该处的命令（`quick_goto` 开启时自动退出选区，关闭时保持选区可连续添加）。
2. 点「前往」**右侧小箭头** ⇒ 弹出 4 项下拉（前往 / 解挂 / 前往挂接 / 等待挂接）；
   选「解挂」「等待挂接」立即插入对应固定命令；选「前往挂接」进入挂接选区，
   点车站/车库插入 `MakeGoToCouple`（车库型 `depot_target=true`），点其它格无动作且不退出。
3. 按 ESC / 右键取消 ⇒ 按钮抬起、选区退出、不再插入。
4. 点列表**任意一行** ⇒ 该行高亮、底部参数行随之切换（这是问题 2 的正面判据；
   行高 = 字高 + `vsep_normal`，与绘制逐像素一致）。
5. 列表滚动/翻页后点击仍命中正确行；`WID_O_ORDER_LIST` 最小高仍为 6 行。
6. **负判据**：有主订单窗口（`OrdersWindow`）的「前往」按钮点击语义、下拉项、行高逐字不变。
7. **复测必须用 22:09:41 的 `build\openttd.exe`**（21:37:47 及更早的 exe 不含本轮三处改动）。
