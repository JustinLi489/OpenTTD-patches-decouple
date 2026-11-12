# 第 425 轮：无主排程编辑器改用普通列车订单窗口控件树（REG-424-B）

> 落盘时间：2026-11-12（紧接着第 424 轮 KI-611「GUI 还是老样子」）。
> 本文件是本轮唯一可信状态载体；对话内容被压缩后按本文件恢复，不得重跑取证。

## 一、玩家意图与本轮口径

- 第 424 轮玩家原话：「什么东西，为什么我看到的无主排程编辑的 GUI 还是老样子」
  ⇒ 说明第 424 轮「在旧手写树里塞一个参数行」的做法不满足预期。
- 第 425 轮拍板口径（REG-424-B）：**不再维护手写控件树**，无主排程编辑器直接复用普通列车订单窗口
  的 `_nested_orders_train_widgets`（`src\order_gui.cpp`），做到与订单窗口**逐像素一致**。
- 无主排程没有真实 `Vehicle`，因此：**依赖车辆才能工作的控件一律「置灰（禁用）」而不是隐藏**，
  保证行高/列宽稳定，切换选中订单时窗口不需要 `ReInit()`。

## 二、已落地改动（`src\order_gui.cpp`，只改这一个 .cpp，未碰 `src\*.h`）

### 2.1 控件树替换（第 1 步，已完成）

- 删除原来的 `_nested_r3r_schedule_editor_widgets` 定义，位置改为说明注释
  （关键词：`R3R (第 425 轮, REG-424-B)`）。
- 窗口描述符 `_r3r_schedule_editor_desc` 的 tree 参数改为 `_nested_orders_train_widgets`，
  初始宽度 396 → **384**（与订单窗口一致），高度 160 不变。

### 2.2 控件 ID 机械改名（已完成）

原先的 `WID_R3RSE_*` 全部映射到 `WID_O_*`（`src\widgets\order_widget.h` 中确认存在）：

| 原 ID | 现 ID | 说明 |
|---|---|---|
| `WID_R3RSE_CAPTION` | `WID_O_CAPTION` | 标题栏 |
| `WID_R3RSE_ORDER_LIST` | `WID_O_ORDER_LIST` | 订单列表（含 6 行最小高、resize 1/1） |
| `WID_R3RSE_SCROLLBAR` | `WID_O_SCROLLBAR` | 垂直滚动条 |
| `WID_R3RSE_LOAD` | `WID_O_FULL_LOAD` | 装满（左上 plane 0） |
| `WID_R3RSE_UNLOAD` | `WID_O_UNLOAD` | 卸货（中上 plane 0） |
| `WID_R3RSE_NON_STOP` | `WID_O_NON_STOP` | 非停 |
| `WID_R3RSE_DEPOT_ACTION` | `WID_O_DEPOT_ACTION` | 车库动作（中上 plane 1） |
| `WID_R3RSE_YARD` | `WID_O_R3R_YARD` | 目的地站场（yard plane 1） |
| `WID_R3RSE_SEPARATION` | `WID_O_R3R_SEPARATION` | 间隔/等派（yard plane 1 右侧） |
| `WID_R3RSE_COUPLE_SIDE` | `WID_O_COUPLE_SIDE` | 挂接侧（yard plane 3） |
| `WID_R3RSE_COUPLE_TEMP_GROUP` | `WID_O_COUPLE_TEMP_GROUP` | 临时挂接分组（yard plane 2/3/4） |
| `WID_R3RSE_DECOUPLE_BOUNDARY` | `WID_O_REFIT_DROPDOWN` | **复用右上 plane 1 的 refit 下拉框当解挂边界选择器**（与订单窗口 `OT_DECOUPLE` 分支完全同构） |
| `WID_R3RSE_ADD` | `WID_O_GOTO` | 添加订单（站台下拉列表） |
| `WID_R3RSE_DELETE` | `WID_O_DELETE` | 删除（`WID_O_SEL_BOTTOM_MIDDLE` plane 0） |
| `WID_R3RSE_DUPLICATE` / `_MOVE_UP` / `_MOVE_DOWN` | `WID_O_MGMT_BTN` | **合并成管理下拉菜单**（见 §五 待办 T1，尚未落地） |

改名落地方式：对 `order_gui.cpp` 做字面量 `-replace`（UTF-8 无 BOM 往返），已用
`findstr /n /c:"WID_R3RSE_"` 复核，只剩 §五 的 3 处 `case` 标签（5874 / 5880 / 5888）。

### 2.3 `UpdateParameterWidgets()` 重写（已完成，约 :5539-5657）

新实现要点（顺序即代码顺序）：

1. **固定平面**（每帧无条件设置）：
   - `WID_O_SEL_TOP_ROW_GROUNDVEHICLE` → `DP_GROUNDVEHICLE_ROW_NORMAL`(0)（永不走条件单行）
   - `WID_O_SEL_OCCUPANCY` → `SZSP_NONE`（无占用列）
   - `WID_O_SEL_NON_STOP` → 0（非停按钮始终可见，按订单类型再决定是否禁用）
   - `WID_O_SEL_MGMT` → 0（`WID_O_MGMT_BTN`，不用 `MGMT_LIST_BTN`）
   - `WID_O_SEL_BOTTOM_MIDDLE` → 0（删除，不用“停止共享”）
   - `WID_O_SEL_SHARED` → 0（共享订单列表按钮，置灰）
2. **需要车辆 ⇒ 永远禁用**：`WID_O_TIMETABLE_VIEW`、`WID_O_OCCUPANCY_TOGGLE`、`WID_O_SKIP`、
   `WID_O_SHARED_ORDER_LIST`（+ `WID_O_REFIT`、`WID_O_REFIT_DROPDOWN` 默认禁用，按类型再放开）。
3. 参数类按钮**先全部禁用**，再按选中订单类型开放：
   `WID_O_FULL_LOAD / UNLOAD / NON_STOP / REFIT / DEPOT_ACTION / REFIT_DROPDOWN / REVERSE /
   COUPLE_SIDE / COUPLE_TEMP_GROUP / R3R_YARD / R3R_SEPARATION`。
4. 类型分支（左侧 `left` / 中间 `middle` / 右侧 `right` / 站场槽 `yard` 四个平面变量）：
   - `OT_GOTO_STATION`：left=LOAD、right=REFIT、yard=`DP_YARD_DROPDOWN`；放开 FULL_LOAD/UNLOAD
     （`no_stop` 目标站不停时仍禁用，与订单窗口同口径）、NON_STOP、YARD、SEPARATION；
     回显 lower 状态（装满/卸货/非停/间隔+等派 OR）。
   - `OT_GOTO_DEPOT`：left=`DP_LEFT_REFIT`（**置灰**，因为需要车辆）、middle=`DP_MIDDLE_SERVICE`；
     放开 DEPOT_ACTION、NON_STOP。
   - `OT_GOTO_WAYPOINT`：left=`DP_LEFT_REVERSE`；放开 REVERSE
     （回显 `order->GetWaypointFlags().Test(OrderWaypointFlag::Reverse)`，与
     `order_gui.cpp:2988` 同源）、NON_STOP。
   - `OT_GOTO_COUPLE`：yard=`order->GetCoupleIsDepot() ? DP_YARD_COUPLE_DEPOT : DP_YARD_COUPLE`；
     放开 COUPLE_TEMP_GROUP，目标为车库时再放开 COUPLE_SIDE 并回显 `OCS_REAR`。
   - `OT_WAIT_COUPLE`：yard=`DP_YARD_WAIT_COUPLE`；放开 COUPLE_TEMP_GROUP。
   - `OT_DECOUPLE`：right=`DP_RIGHT_REFIT`；放开并回显 `WID_O_REFIT_DROPDOWN`
     （`GetDecoupleBoundaryMode() != DecoupleBoundaryMode::Auto`）。
   - 其它/无选中：四槽保持默认（LOAD/UNLOAD/EMPTY/YARD_EMPTY），全部禁用。
5. 末尾统一 `SetDisplayedPlane(left/middle/right/yard)`。

### 2.4 `UpdateButtonStates()` 重写（已完成，约 :5659-5666）

```cpp
const VehicleOrderID count = this->GetNumOrders();
const bool has_selection = (this->selected >= 0 && this->selected < count);
this->SetWidgetDisabledState(WID_O_DELETE, !has_selection);
this->SetWidgetDisabledState(WID_O_MGMT_BTN, count == 0);   // 原三按钮的禁用逻辑合并
```

## 三、平面索引依据（`order_gui.cpp` 内 `enum DisplayPane` :1892-1927）

> **编译坑（第 425 轮已踩，务必记住）**：`enum DisplayPane` 是 `struct OrdersWindow` 的
> **私有成员**（第 1875 行 `struct OrdersWindow` 后的 `private:` 区），`R3RScheduleEditorWindow`
> 只是复用它的控件树、**不是**它的派生类，因此代码里**不能写 `DP_*` 名字**
> （否则 `error C2065: “DP_LEFT_LOAD”: 未声明的标识符` 一次报 14 条）。
> 正确做法：写**裸平面序号**（0/1/2/3/4）并在旁边注释语义，见 §2.3 的落地代码。


- `DP_GROUNDVEHICLE_ROW_NORMAL = 0`
- `WID_O_SEL_TOP_LEFT`：`DP_LEFT_LOAD 0 / DP_LEFT_REFIT 1 / DP_LEFT_REVERSE 2`
- `WID_O_SEL_TOP_MIDDLE`：`DP_MIDDLE_UNLOAD 0 / DP_MIDDLE_SERVICE 1`
- `WID_O_SEL_TOP_RIGHT`：`DP_RIGHT_EMPTY 0 / DP_RIGHT_REFIT 1`
- `WID_O_SEL_TOP_YARD`：`DP_YARD_EMPTY 0 / DP_YARD_DROPDOWN 1 / DP_YARD_COUPLE 2 /
  DP_YARD_COUPLE_DEPOT 3 / DP_YARD_WAIT_COUPLE 4`

对照来源：普通订单窗口 `UpdateButtonState()`（`order_gui.cpp` 约 :2870-3210，其中
`OT_GOTO_STATION` :2941、`OT_GOTO_WAYPOINT` :2975-2992、`OT_GOTO_DEPOT` :2994、
couple/wait_couple/decouple :2890-2935、`default` :3182）。

## 四、为什么不用 `SZSP_NONE` 隐藏不重要槽位

订单窗口对 waypoint / couple / decouple 的部分槽位用 `SZSP_NONE` 整个摘掉（行宽随之变化，
它靠 `layout_changed` → `ReInit()` 处理）。无主编辑器按 REG-424-B 口径**一律保留槽位并置灰**：
所有候选平面宽度相同（`EqualSize` 容器内每个子项 60px，yard 槽四个 plane 也是 60px），
所以切换选中不会改变布局，`ReInit()` 一次都不需要。

## 五、剩余待办（按顺序）

- **T1（已完成）**：`OnClick()` 中 3 处残留 case
  `WID_R3RSE_DUPLICATE` / `_MOVE_UP` / `_MOVE_DOWN` 已合并为单个 `case WID_O_MGMT_BTN:`，
  弹 `ShowDropDownList()`（`DDSF_SHARED`），三项按 `has_selection` / 首条 / 末条分别 `masked`。
- **T2（已完成）**：`OnDropdownSelect()` 新增 `case WID_O_MGMT_BTN:`，index 0/1/2 分别发
  `Commands::DuplicateR3RLooseOrder` / `MoveR3RLooseOrder(sel, sel-1, 1)` /
  `MoveR3RLooseOrder(sel, sel+1, 1)`（命令体逐行搬自原 OnClick 三处，语义未改；
  成功移动后同步 `this->selected` 并 `SetDirty()`）。
- **T3（已完成）**：新增 `GetWidgetString()` 重写，`WID_O_CAPTION` ⇒
  `STR_R3R_SCHEDULE_EDITOR_CAPTION`（原树无 caption 串，不重写会导致标题空白）。
- **T4**（未做）：构造函数里 `CreateNestedTree()` 之后补一次 `UpdateParameterWidgets()`（当前只在
  `OnPaint()` 里调，首帧前窗口尺寸可能按 plane 0 算过）。判断：`OnPaint()` 在
  `DrawWidgets()` 之前调用，首帧即正确，暂不修改以免引入构造期重入。
- **T5**：`WID_O_SEL_MGMT` / `WID_O_SEL_BOTTOM_MIDDLE` / `WID_O_SEL_SHARED` 等新出现的
  选择器是否需要 `UpdateWidgetSize` 特殊处理 —— 目前 `UpdateWidgetSize` 只处理
  `WID_O_ORDER_LIST`（6 行最小高 + resize/fill），先保持不动，编译后看实际尺寸。
- **T6（清理，可延后）**：`src\widgets\order_widget.h` 里 `enum R3RScheduleEditorWidgets`
  （原 `WID_R3RSE_*` 定义）已无任何引用，成为死代码。删它要改 `.h` ⇒ 触发全量重编
  （16~20 分钟），故**本轮不删**，留待下次必须改头文件时一并清理。

## 六、未确认 / 风险

- **U-1（已解决）**：`R3RScheduleEditorWindow` 原本**没有**重写 `GetWidgetString`（第 424 轮旧树里标题是
  `WID_R3RSE_CAPTION` + `STR_R3R_SCHEDULE_EDITOR_CAPTION`）；本轮已加
  `std::string GetWidgetString(WidgetID, StringID) const override`，`WID_O_CAPTION` 返回
  `STR_R3R_SCHEDULE_EDITOR_CAPTION`，其余回退 `Window::GetWidgetString`。

## 七、构建自证（2026-11-12）

- 入口：`R3R_fullbuild.cmd inc detach`（复用既有入口，未新建任何 `.cmd`）。
- 第 1 次构建 **EXIT_CODE=1**：`order_gui.cpp` 报 14 条 `C2065: "DP_*": 未声明的标识符`
  ——根因见 §三 的编译坑（`DisplayPane` 是 `OrdersWindow` 私有枚举）。改法是全部换成裸序号 +
  语义注释，`DP_` 前缀在 `R3RScheduleEditorWindow` 内**零出现**（`findstr /c:"DP_"` 只剩
  `OrdersWindow` 自己的合法用法）。
- 第 2 次构建：`build\R3R_incbuild.done` = `EXIT_CODE=0`；日志 `[3/3] Linking CXX executable
  openttd.exe`（全日志唯一次 Linking）；错误计数 0。
- 时间戳：`src\order_gui.cpp` 21:35:42 → `order_gui.cpp.obj` 21:36:42 →
  `build\openttd.exe` 21:37:47（52 842 496 B）⇒ exe 晚于源码，obj 晚于 .cpp。
- `read_lints` 对 `src\order_gui.cpp` 报 0 条。

## 八、游戏内复测判据

1. 双击无主排程打开编辑器：窗口标题显示 `STR_R3R_SCHEDULE_EDITOR_CAPTION`（不是空白/缺参数），
   整体排布与普通列车订单窗口**逐像素一致**（同一行高、同一按钮宽度、同一参数行位置）。
2. 在列表里上下切换选中不同订单类型（站台/车库/路点/挂车/等待挂车/解挂）：
   参数行随类型切换，但窗口**尺寸不变、无闪烁、无重排**。
3. 需要车辆的控件（装载/卸货/装满/车库动作/间隔/挂接分组/改装下拉等）**可见但灰**，点击无反应。
4. 管理下拉（原 MGMT 按钮）三项可用性：无选中 ⇒ 三项全灰；选中首条 ⇒ 上移灰；
   选中末条 ⇒ 下移灰；选中中间 ⇒ 三项全亮。
5. 复制 / 上移 / 下移三条命令行为与第 424 轮一致（复制在选中项后插入一条同参数订单；
   上移/下移成功后选中高亮跟随移动）。
6. 订单列表滚动条、6 行最小高度、"前往"按钮新增订单流程正常。

## 九、未做 / 遗留

- T4/T5/T6 见 §五。
- `WID_O_TIMETABLE_VIEW` / `WID_O_OCCUPANCY_TOGGLE` / `WID_O_SKIP` / `WID_O_SHARED_ORDER_LIST`
  永久置灰（无主排程没有时间表/占用/共享语义），未提供替代入口。
- 复测未做：以上 6 条需玩家用 21:37:47 版 `build\openttd.exe` 验证。
- **U-2**：`WID_O_GOTO` 在订单窗口里是 `NWID_BUTTON_DROPDOWN`，主区点击也会开列表；
  无主编辑器把「添加订单」挂到它上面，行为上等价，但按钮宽度/文案（“前往”）语义不够贴切，
  本轮先不引入新语言串。
- **U-3**：`WID_O_SEL_SHARED` 有两个 plane（共享订单列表 / 加入车队分组），都依赖车辆；
  本轮固定 plane 0 + 禁用。若后续觉得这块灰按钮太扎眼，可改 `SZSP_NONE`（会改变行宽，
  需要自己处理 `ReInit`，故本轮不做）。
- **U-4**：`WID_O_TIMETABLE_VIEW` / `WID_O_OCCUPANCY_TOGGLE` 在标题栏，禁用后可点性为 0，
  但订单窗口里它们是 `NWID_BUTTON`（无 dropdown 箭头），视觉上仍是灰按钮 —— 符合口径。

## 七、构建与复测判据

构建（按口径只编内测树）：

```
powershell -NoProfile -Command "Start-Process -FilePath 'd:\sourcecode of JGRPP\R3R_fullbuild.cmd' -ArgumentList 'inc detach' -WorkingDirectory 'd:\sourcecode of JGRPP' -WindowStyle Minimized"
```

判据（三件套，必须看时间戳）：
① `build\R3R_incbuild.done` 内容 `EXIT_CODE=0`；
② `build\R3R_incbuild.log` 尾部 `[n/n] Linking CXX executable openttd.exe`；
③ `build\openttd.exe` mtime 晚于 `src\order_gui.cpp`，且 `order_gui.cpp.obj` 也晚于它。
（改的是 `.h` 的话护栏会判全量，本轮只改 `.cpp`，应走增量。）

游戏内复测：
1. 双击无主排程打开编辑器 ⇒ 窗口外观/宽度/按钮位置与普通列车订单窗口逐像素一致。
2. 选中「前往站台」⇒ 左上「装满」、中上「卸货」、右上（refit 下拉，灰）、站场下拉、间隔菜单
   均出现且可用；`WID_O_SKIP`、`WID_O_TIMETABLE_VIEW`、占用列开关、共享订单列表为灰。
3. 选中「前往车库」⇒ 左上 refit（灰）、中上「服务」可用。
4. 选中「前往路点」⇒ 左上「掉头」可用且能点亮。
5. 选中「解挂」⇒ 右上下拉可用，能选自动/尾段/头边界。
6. 切换选中订单时窗口**不重排、不闪动**（这是本轮置灰策略的核心验收点）。
