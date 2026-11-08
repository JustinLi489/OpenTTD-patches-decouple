# 第 392 轮：玩家三条体验优化（2026-11-05）

## 零、玩家原话（逐字保留）

> 我又休息了一阵子。并为你带来了三个我希望优化的地方：
> 1，机车在车库，车底在车站，机车的命令是去车站挂接，那么机车就会一直显示等待空余轨道不去挂接，这不符合预期
> 2，关于一些R3R命令在调度计划表里面的图形显示，我发现，等待挂接，前往挂接命令的下面，有一大堆不需要的属性栏，什么不停车，什么满载后离开，导致我预览该命令的临时挂接分组十分困难。
> 3，关于挂接分组的管理页面，第一个是父分组折叠的时候子分组应当隐藏，第二个是关于这里面段的显示我希望有列车的预览，就像普通列车分组一样

## 一、问题拆解与初步判断（标注状态）

| # | 现象 | 性质 | 关联既有条目 | 当前状态 |
|---|---|---|---|---|
| 1 | 库内机车执行 GOTO_COUPLE 去车站挂接，一直显示"等待空余轨道"（原版 stuck 文案）且不出发 | 行为缺陷（高） | KI-242（第 242 轮）第 4 条结构性路径、KI-347、KI-330、KI-208 门禁 | 待取证 |
| 2 | OT_WAIT_COUPLE / OT_GOTO_COUPLE 订单面板下方仍显示 GOTO_STATION 的通用属性栏（不停车、满载后离开等），把「临时挂接分组」挤出可视区 | GUI 布局（中） | KI-262（第 162 轮，订单窗口宽度/EqualSize） | 待取证 |
| 3a | 挂接分组窗口里父分组折叠时，子分组仍显示 | GUI 逻辑（中） | 第 146 轮父子组（`parent` 成员关系） | 待取证 |
| 3b | 分组内的"段"列表只有文本 `#n（m 节）`，希望有列车缩略图预览 | GUI 功能（中） | 第 155 轮段子行、group_gui 车辆列表绘制 | 待取证 |

## 二、初步猜测根因（待验证，按铁律先落盘）

### 猜测 G1（问题 1）

- **G1-a**：`TrainLocoHandler` 的停驶分支（`if (j < adv_spd)`）对 OT_GOTO_COUPLE 只做几何命中/配对搜索，**不调用 TrainController / TryPathReserve**；而机车在库内 `cur_speed==0` 且 `TRACK_BIT_DEPOT`，只有移动分支才会出库。⇒ 永不出库，`VehicleRailFlag::Stuck` 被置位后界面显示"等待空余轨道"。这与第 242 轮已证实的第 4 条结构性路径同族，但那条是"停在普通轨道"，本条是"停在库内"。
- **G1-b**：库内出库预留失败（`MarkTrainAsStuck`）来自 `TPR-BLOCKED`（KI-330）或库位占用（KI-339 复测里"一 depot 一列车"的前提）。
- **G1-c**：与第 208 轮 `RESV-NOBOOK` 门禁无关（该门禁要求 `IsTileType(tile, Station)`，库格被排除），除非判定条件被后续轮次放宽。

### 猜测 G2（问题 2）

- **G2-a**：`order_gui.cpp` 的订单底部面板按 `OT_GOTO_STATION` 的控件组合无条件铺开（`WID_O_NON_STOP`、`WID_O_FULL_LOAD`、`WID_O_UNLOAD`、`WID_O_TRANSFER`、`WID_O_NO_UNLOAD` 等），`OT_WAIT_COUPLE`/`OT_GOTO_COUPLE` 分支没有把这些控件的显示区收回，于是既占高度又出现在预览里。
- **G2-b**：「临时挂接分组」`WID_O_COUPLE_TEMP_GROUP` 被排在被挤下去的位置（第 143 轮加入 WAIT_COUPLE 支持时只补了 plane 5 的显示条件，未调整行序）。

### 猜测 G3（问题 3）

- **G3-a**：分组窗口左栏 `RebuildGroups()` 递归展开全部子组，没有持久化的折叠位图（也可能有折叠按钮但只影响展开箭头、不过滤子项）。
- **G3-b**：右栏段列表由 `couple_group_gui.cpp` 自绘文本行，未走 `vehicle_gui` 的 `DrawVehicleImage`/`VehicleList`，所以没有缩略图。

## 三、取证计划（按序执行，每条取证后立刻回填本节）

1. 读 `TrainLocoHandler` 停驶分支与移动分支的 GOTO_COUPLE 处理；读 `MarkTrainAsStuck` 调用点。
2. 读 `order_gui.cpp` 中 OT_WAIT_COUPLE / OT_GOTO_COUPLE 的 plane 与控件显示条件。
3. 读 `couple_group_gui.cpp` 左栏构建与右栏段列表绘制。
4. 读 `group_gui.cpp` 车辆列表缩略图绘制可复用函数。

## 四、取证记录（逐条追加）

### R392-A 问题 2 根因已实证（`src/order_gui.cpp:2758-2766`）

```cpp
case OT_GOTO_COUPLE:
case OT_WAIT_COUPLE:
    this->DisableWidget(WID_O_NON_STOP);
    this->DisableWidget(WID_O_FULL_LOAD);
    this->DisableWidget(WID_O_UNLOAD);
    this->DisableWidget(WID_O_REFIT_DROPDOWN);
    this->DisableWidget(WID_O_REFIT);
    this->EnableWidget(WID_O_MGMT_BTN);
    break;
```

判定：
- 该分支**只有 `DisableWidget`，没有 `SetDisplayedPlane`**。`DisableWidget` 只让按钮变灰，控件**仍占位、仍可见**。
- 相邻的 `OT_GOTO_STATION`（2768-2788）、`OT_GOTO_WAYPOINT`（2790-2805）都成套设置了 `train_row_sel / left_sel / middle_sel / right_sel` 的 plane；挂接两条命令漏了。
- 于是四个槽位保持**上一个被选中订单**的 plane（典型是 `DP_LEFT_LOAD` = 满载后离开、`DP_MIDDLE_UNLOAD` = 卸载、`WID_O_NON_STOP` = 不停车），玩家看到的就是"一堆灰掉但仍在的不相关属性栏"。
- 「临时挂接分组」在顶行的 `WID_O_SEL_TOP_YARD` 槽位（plane `DP_YARD_WAIT_COUPLE` / `DP_YARD_COUPLE`，第 143 轮加入），被这些残留控件挤在同一行，所以"预览十分困难"。

修复方向（待落地）：在 `OT_GOTO_COUPLE` / `OT_WAIT_COUPLE` 分支补 plane 设置——不适用的三个槽位（left/middle/right）统一置 `SZSP_NONE`，`train_row_sel` 置 `DP_GROUNDVEHICLE_ROW_NORMAL`，并把 `WID_O_NON_STOP` 一并隐藏而不是仅置灰。

### R392-B 问题 1 取证（`src/train_cmd.cpp`）

已确认的代码事实：

1. `TrainLocoHandler` 的速度计算块（`:23110-23127`）：`R3RShouldWaitForCouplePair(consist)` 为真时把 `cur_speed/subspeed` 清零并令 `j=0`，于是落进原生停驶分支。
2. 停驶分支（`:23139-23182`）对 OT_GOTO_COUPLE **只做** `CheckTrainCollision(moving_front) || TrainCoupleHandler(consist)`，**不调用 TrainController / TryPathReserve**。⇒ 一旦被按住，本 tick 既不动也不订路。这是第 242 轮 KI-348 已证实的结构性路径。
3. `R3RShouldWaitForCouplePair()`（`:14182-14188`）有 `if (v->track == TRACK_BIT_DEPOT) return false;`，注释原文「刻意排除库内：库里 GOTO_COUPLE 的机车必须能开出来去汇合」。⇒ **库内状态本身不是死锁原因**，机车在库里会正常加速、出库。
4. 库内/库外两条"触碰即挂接"捷径都要过配对锁：`CheckTrainCollision` 的库内分支（`:19461-19490`，KI-341）要求 `R3RCouplePairMatches(loco, v->First())`，且要求 `R3RChainFullyInDepot(loco)`；不满足则 `return 0`（本 tick 不挂、也不报错）。

推论（**待现场日志验证**）：
- "等待空余轨道"= `VehicleRailFlag::Stuck` 已置位，即**寻路/预留返回 TPRRF_NONE**，不是"没进移动分支"。因此机车确实尝试过寻路但失败。
- 失败位置只可能两处：①出库后到车站的 PBS 预留被 `FollowTrainReservation`/`CheckTrainsOnTrack` 判阻塞（KI-330 同族）；②YAPF couple 目标（车底旁 `FindSafeCouplePosition` 安全位）不存在——**车底本身就停在站台上**时，若该站台找不到空安全位，就会永久失败。
- 若成立，则本缺陷的语义问题在于：玩家期望"机车去车站挂接"，而实现要求"车底旁边必须还有一块空地给机车停"。

需要的现场证据（**需玩家跑日志，AI 不自行开游戏**）：
1. 用内测版 `build\openttd.exe`（探针 ON）复现，产生 `R3R_debug.log`。
2. 关注行：`CPL-DEPOT-NOTIN`、`CPL-PAIR`/`PAIRSCAN`、`TRP`、`TPR-BLOCKED`、`CPL-BEST`/`CPL-PATHFOUND`、`COUPLE-FAIL`、`CPL-S0-CHK`（含 stuck=）、`SPD-DROP`、`LOCO-MOVE`。
3. 判读：`LOCO-MOVE` 是否出现（区分"从没进移动分支" vs "进了但预留失败"）；`TRP` 的 res 是否恒 0；`CPL-PATHFOUND found=` 是否为 0。

### R392-C 问题 3a 根因已实证（`src/couple_group_gui.cpp` 的 `RebuildGroups()`）

折叠位本身**早就存在**：`this->folded_groups`（`std::vector<CoupleGroupID>`），点击折叠三角走 `ToggleGroupFold` → `folded_groups.push_back/erase` → `RebuildGroups()`；递归 `push_children` 里也确实有

```cpp
if (has_children && !is_folded(id)) {          /* ← 折叠时不再展开子组 */
    for (const CoupleGroupID cand : visible_groups) {
        if (R3RGetCoupleGroupParent(cand) == id) push_children(cand, depth + 1);
    }
}
```

**但递归结束后还有一段"兜底循环"**，把"没有出现在 groups 里的可见组"补进列表：

```cpp
for (const CoupleGroupID id : visible_groups) {
    if (std::find(groups.begin(), groups.end(), id) == groups.end()) {
        groups.push_back(id); depths.push_back(0); ... has_children=false;
    }
}
```

`push_children` 被折叠跳过的**子组与孙子组同样不在 `groups` 里** ⇒ 兜底循环把它们原样加回列表底部，而且 `depth=0`。⇒ 玩家点折叠三角后子分组不但没隐藏，还从缩进位置**跳到列表末尾顶格显示**，观感上就是"折叠无效"。

修复：兜底循环前先判断该组是否被某个**可见且已折叠的祖先**遮住（沿 `R3RGetCoupleGroupParent` 上溯，带步数上限防旧档环），是则跳过。

### R392-D 问题 2 控件树事实（`src/order_gui.cpp:4861-4900`）

顶行结构（`WID_O_SEL_TOP_ROW_GROUNDVEHICLE` 的 plane 0）：

```
NWID_HORIZONTAL [EqualSize]
  WID_O_NON_STOP        ← 直接孩子，不是 selection，只能变灰不能隐藏
  WID_O_SEL_TOP_LEFT    ← selection（FULL_LOAD / REFIT / REVERSE）
  WID_O_SEL_TOP_MIDDLE  ← selection（UNLOAD / DEPOT_ACTION）
  WID_O_SEL_TOP_RIGHT   ← selection（空 panel / REFIT_DROPDOWN）
  WID_O_SEL_TOP_YARD    ← R3R 场/临时挂接分组下拉
```

- `plane 6 (DP_GROUNDVEHICLE_ROW_EMPTY)` 是一个**不含 YARD 槽**的空 panel，直接用它会把「临时挂接分组」下拉一起弄没，所以不能用。
- 结论：left/middle/right 三个 selection 置 `SZSP_NONE` 即可隐藏；`WID_O_NON_STOP` 要隐藏必须新增一层 selection（`WID_O_SEL_NON_STOP`：plane 0 = 按钮、plane 1 = 空 panel），因此本轮改动会碰 `src/widgets/order_widget.h`（触发全量重编）。

### R392-E 问题 3b 可行性（缩略图）

- `DrawVehicleImage(const Vehicle *, const Rect &, VehicleID selection, EngineImageType, int skip)` 已公开（`vehicle_gui.h`），`EIT_IN_LIST` 可用。
- `DrawTrainImage()` 的逐节循环条件是 `px < max_width` 且第 k 节需 `px - width < max_width`，因此**把 rect 宽度设成"本段各节显示宽度之和"就恰好只画本段**，不会串到下一段。
- 段内总宽度用 `Train::GetDisplayImageWidth(&offset)` 逐节累加（`DrawTrainImage` 内部用的同一个函数）。
- 行高：`UpdateWidgetSize` 里两栏共用 `GetCharacterHeight(Normal) + 2`；要提高行高必须**两栏同步**（`Window::resize.step_height` 取根容器的最小正孩子，绘制又用窗口级 `resize.step_height`），否则滚动与点击错位。

## 五、落地状态

### 5.1 问题 3a（临时挂接分组窗口折叠三角无效）——已改，待编译

文件：`src/couple_group_gui.cpp`，函数 `RebuildGroups()`。

根因（见 R392-C）：递归构建结束后有一段"兜底循环"，把 `push_children()` 因
`is_folded()` 而跳过的子组重新追加进列表且 `depth = 0`，于是折叠后子组既没被隐藏、
又跳到列表末尾顶格显示，看起来就是"点三角没反应"。

修法：在兜底循环前加 `hidden_by_folded_ancestor()` lambda，沿 `R3RGetCoupleGroupParent()`
向上溯源，只要途中遇到「可见且已折叠」的祖先就跳过该组（步数上限取
`visible_groups.size()` 防旧档成环）。

### 5.2 问题 3b（右栏段列表无缩略图、行太矮）——已改，待编译

文件：`src/couple_group_gui.cpp`。

- 行高（`UpdateWidgetSize`）：两栏（组列表 / 段列表）统一改成
  `max(字符高 + 2, ScaleGUITrad(GetVehicleHeight(Train)) + 2)`，保证与窗口级
  `resize.step_height` 一致（否则滚动与点击命中会错位）。
- 缩略图（`DrawWidget` 的 `WID_CG_SEGMENTS` 分支）：按节累加宽度直到下一个 `IsSegmentFront`
  或半行宽上限，然后用 `DrawVehicleImage(t, thumb, VehicleID::Invalid(), EIT_IN_LIST, 0)` 只画本段，
  之后把文字矩形 `Indent()` 掉缩略图宽度再 `DrawString()`。RTL 由
  `_current_text_dir == TD_RTL` 决定缩进方向。
- 新增 `#include "vehicle_gui.h"`。

### 5.3 问题 2（顶行控件在不适用订单类型下仍显示）——已改，待编译

文件：`src/widgets/order_widget.h`、`src/order_gui.cpp`。

- `order_widget.h` 新增 `WID_O_SEL_NON_STOP`（放在 `WID_O_SEL_TOP_RIGHT` 之后）。
- `order_gui.cpp` 顶行控件树：把 `WID_O_NON_STOP` 包进新的 `NWID_SELECTION`，
  这样它才有"隐藏"这种状态（原来是 `EqualSize` 容器的直接孩子，只能变灰）。
- `OrderWindow::UpdateButtonState()` 新增两个局部工具：
  - `set_top_plane(sel, plane)`：切换 plane 并记录是否跨越"隐藏边界"；
  - `show_non_stop(bool)`：切换 plane 并安全清 lowered（隐藏时不再调
    `RaiseWidget(WID_O_NON_STOP)`）。
- 各分支的新可见性：
  | 订单类型 | 不越站 | 左槽 | 中槽 | 右槽 |
  |---|---|---|---|---|
  | 空行 / default / `OT_COUPLE_END` | 隐藏 | 保持原样 | 保持原样 | 保持原样 |
  | `OT_DECOUPLE` | 隐藏 | 隐藏 | 隐藏 | 段边界下拉 |
  | `OT_GOTO_COUPLE` / `OT_WAIT_COUPLE` | 隐藏 | 隐藏 | 隐藏 | 隐藏 |
  | `OT_GOTO_STATION` | 显示 | 满载 | 卸载 | 改造 |
  | `OT_GOTO_WAYPOINT` | 显示 | 掉头 | 隐藏 | 空 |
  | `OT_GOTO_DEPOT` | 显示 | 改造 | 服务 | 空 |
- 隐藏用 `SZSP_NONE`。已核实 `NWidgetStacked::FillWidgetLookup()` 会先经
  `NWidgetContainer::FillWidgetLookup()` 注册自身与全部孩子，所以
  `GetWidget<NWidgetStacked>(WID_O_SEL_TOP_*)` 在隐藏后**依然有效**（不会退化成 nullptr，
  不会出现"隐藏后永远回不来"）。`EqualSize` 容器里隐藏孩子的 `fill_x == 0`，
  不会被提升到 `longest`，因此宽度会真正回收给剩余槽位。
- 布局重算：`AssignSizePosition()` 只在窗口 ReInit / resize 时执行，因此
  `UpdateButtonState()` 末尾加了 `if (layout_changed) this->ReInit();`。
  `OrderWindow` 没有 `OnInit()` override，`ReInit()` 不会重置 `selected_order` /
  `goto_type` 等业务状态。

### 5.4 待验证

1. 三个文件 `read_lints` 均为 0 条。
2. 因改了 `src/widgets/order_widget.h`，按 KI-183 规则必须走全量重编
   （护栏应判 `REMOVED 620 object file(s)`）。
3. 游戏内判据见第六节。

### 5.5 首次全量构建失败与修复（2026-11-05）

`build\R3R_fullbuild.done` = `EXIT_CODE=1`，日志停在 `[479/698]`，共 3 条 error，全部在
`src\couple_group_gui.cpp`：

- `:503` `error C3861: "ScaleGUITrad": 找不到标识符`
- `:503` `error C2664: _Ty std::max<int>(std::initializer_list<_Ty>)`（这是上一条的连锁误报：
  第一个实参解析失败后编译器把参数表当 initializer_list 匹配）
- `:599` `error C3861: "ScaleGUITrad": 找不到标识符`

根因：3b 新增的行高/缩略图代码用了 `ScaleGUITrad()`，它定义在 `src\zoom_func.h`（:117），
而 `couple_group_gui.cpp` 原来没有 include 该头（它此前只用到 `GetVehicleHeight`，在
`vehicle_gui.h` 里）。

修法：`src\couple_group_gui.cpp` include 区在 `window_gui.h` 与 `core/geometry_func.hpp` 之间
插入 `#include "zoom_func.h"`。`read_lints` 三个文件均为 0 条，重新以
`R3R_fullbuild.cmd inc detach` 增量续编（护栏判 `incremental is safe`，剩余 221 步）。

