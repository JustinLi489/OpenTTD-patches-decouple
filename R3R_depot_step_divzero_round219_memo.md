# 第 219 轮（2026-10-06）临时分析报告：非列车车库 `resize.step_width == 0` 导致矩阵命中检测除零

## 零、玩家原话（需求）

> 「三次的现场分别是公路车库（CmdBuildRoadDepot）、船坞（CmdBuildShipDepot）、机场（CmdBuildAirport），都是在车库里造了一辆车之后右键触发。也就是说：非列车车库的 resize 步长为 0，右键命中检测里做了除以它的运算。」

需求＝**公路车库 / 船坞 / 机场三种非列车车库窗口，在库里造出（或已有）一辆车后，在矩阵区左键/右键都会致命失败**。玩家已把根因方向指得很准（步长为 0 + 除零），本轮任务是把它落到具体代码行并修掉。

## 一、结论（先说结果）

玩家的方向**完全正确，但比预想的更严重、且成因在窗口控件树而非车库逻辑**：

- `this->resize.step_width`（窗口级）**对全部车库窗口恒为 0**（含列车车库），成因是车库窗口根容器的 `resize_x` 被 `std::lcm(1, 0)` 拉成 0。
- `this->resize.step_height`（窗口级）**恰好等于单元格高度**，所以纵向没症状 —— 这是"看起来只有宽度出问题"的原因。
- 列车车库之所以没事，是**撞了两条巧合**：`num_columns` 恒为 1（`:401` 初值，`:1820-1824` 只给非列车算列数）+ 命中检测对 Train 走 `xm = x` 特例分支（`:1015-1016`）⇒ `step_width` 一次都没被用到。
- 非列车车库：`num_columns = current_x / resize_x ≥ 2` ⇒ 绘制侧 `WithWidth(0)` 造出**零宽单元格**（退化为负高度 rect，即 KI-135 那条 `GUI-DEGEN-RECT`/贴图不显），命中检测侧 `x / 0` 是**整数除零**（SIGFPE / `STATUS_INTEGER_DIVIDE_BY_ZERO`）⇒ **左右键点在矩阵上都会炸**，不需要"右键"这个前提。
- 正确量是**矩阵控件自己的** `resize_x`/`resize_y`（由 `UpdateWidgetSize(WID_D_MATRIX)` 的 resize 出参权威设定），本文件在 `num_columns`（`:1823`）与 `rows_in_display`（`:953`）**已经**用它，只有绘制与命中这两处误用了窗口级步长 —— **文件内部本来就是自相矛盾的**。

## 二、取证（逐环锚点）

### 2.1 窗口级步长的来源

`src/window.cpp`：
```cpp
this->resize.step_width  = this->nested_root->resize_x;
this->resize.step_height = this->nested_root->resize_y;
```
并且同文件注释自陈：`resize.step_width and/or resize.step_height may be 0, which means no resize is possible.` ⇒ **0 是合法值**，库代码却把它当除数用。

### 2.2 车库窗口的根容器是 `NWidgetVertical`

`src/widget.cpp` `MakeWindowNWidgetTree()`（`:3423-3453`）：首件是 `NWidget(NWID_HORIZONTAL)` 且含 CAPTION + SHADEBOX ⇒
```
root = NWidgetVertical
├── titlebar (NWidgetHorizontal, 定义见 depot_gui.cpp:59-69)
└── shade_stack (NWidgetStacked)
    └── inner (NWidgetVertical) = [矩阵行, 按钮行, 段工具行]
```

### 2.3 两条合并规则不同（这是根因的公式）

`NWidgetHorizontal::SetupSmallestSize`（`widget.cpp:1515-1523`）：
```cpp
this->resize_x = 0;   // smallest non-zero child widget resize step
this->resize_y = 1;   // smallest common child resize step
...
if (child_wid->resize_x > 0) { if (this->resize_x == 0 || this->resize_x > child_wid->resize_x) this->resize_x = child_wid->resize_x; }   // :1575-1577 取"最小正"
this->resize_y = std::lcm(this->resize_y, child_wid->resize_y);                                                                          // :1578 取"最小公倍"
```

`NWidgetVertical::SetupSmallestSize`（`:1705` / `:1762`）：
```cpp
this->resize_x = 1;
...
this->resize_x = std::lcm(this->resize_x, child_wid->resize_x);
```

⇒ **`std::lcm(1, 0) == 0`**：垂直容器只要有一个孩子的 `resize_x == 0`，容器自己的 `resize_x` 就被永久钉成 0。

### 2.4 标题栏把根容器钉成 0

`depot_gui.cpp:59-69` 标题栏的子件：`WWT_CLOSEBOX`、`NWID_SELECTION`(rename `WWT_PUSHIMGBTN`)、`WWT_CAPTION`、`WWT_LOCATION`、`WWT_SHADEBOX`、`WWT_DEFSIZEBOX`、`WWT_STICKYBOX` —— **无一带 `SetResize`，即 `resize_x` 全为 0** ⇒ 标题栏自身 `resize_x` 停在初值 0（"最小正"取不到任何正值）⇒ 根 `NWidgetVertical` 的 `resize_x = lcm(1, 标题栏 0, 正文…) = 0`。

**这条对列车/公路/船/机场四种车库完全一样**（`_nested_train_depot_widgets` 与其余三个共用同一段标题栏 `:59-69`）⇒ `step_width == 0` 是**四种车库共有的固有状态**，不是非列车特有。

### 2.5 为什么 `step_height` 反而是对的

根是**垂直**容器，`resize_y` 走"最小正孩子步长"；矩阵行容器 `A1`(`:71-76`) 的 `resize_y = min 正 {MATRIX.resize_y, HSCROLLSELECTION…} = MATRIX.resize_y = 单元格高度`，而按钮行/B/C 行的 `resize_y` 都是 0（它们的子件 `SetResize(1, 0)`）⇒ 最小正值取到矩阵的单元格高度 ⇒ `step_height == cell height`。

矩阵 `resize_x/resize_y` 的权威来源是 `UpdateWidgetSize(WID_D_MATRIX)` 的 resize 出参（`depot_gui.cpp:1273-1286`）：
```cpp
int base_width = this->count_width + this->header_width + this->tag_width + padding.width;
resize.height = std::max<uint>(this->cell_size.height, min_height + padding.height);
if (this->type == VehicleType::Train) {
    resize.width = 1;
    ...
} else {
    resize.width = base_width + this->cell_size.extend_left + this->cell_size.extend_right;  // ← 单元格宽
    ...
}
fill.width = resize.width;
fill.height = resize.height;
```

### 2.6 为什么列车车库至今没炸（两条巧合）

1. `num_columns` 成员初值 1（`:401`），且 `OnResize()`（`:1816-1825`）**只给非列车**算列数：`this->num_columns = nwi->current_x / nwi->resize_x;`。
2. 绘制 `if (this->num_columns != 1) ir = ir.WithWidth(this->resize.step_width, rtl);`（`:929`）⇒ 列车跳过宽度设置。
3. 命中检测 `if (this->type == VehicleType::Train) { xm = x; } else { xt = x / this->resize.step_width; … }`（`:1015-1020`）⇒ 列车跳过除零。
4. 纵向 `ym = (y - matrix_widget->pos_y) % this->resize.step_height;`（`:1022`）对列车也执行，但 `step_height` 恰好非 0（见 2.5）⇒ 不炸。

⇒ 列车车库不是"做对了"，而是**恰好一次都没走到那两行**。

### 2.7 退化 rect 的算术（与 KI-135 现场吻合）

`src/core/geometry_type.hpp`：
- `WithWidth(0, false)` = `WithX(left, left - 1)` ⇒ 宽 0；
- `WithHeight(0)` = `WithY(top, top - 1)` ⇒ 高 0；
- 随后 `ir.Shrink(WidgetDimensions::scaled.framerect)`（`{2,1,2,1}`）⇒ 宽高进一步变负 ⇒ `Draw*Image` 拿到负尺寸 rect（KI-135 的 `GUI-DEGEN-RECT depot type=%d cell=%d,%d,%d,%d` 探针就是为它加的）。

## 三、修法

**唯一原则：矩阵单元格几何量一律取矩阵控件自己的 `resize_x`/`resize_y`**（与既有 `:953` `rows_display`、`:1823` `num_columns`、`:1344` `vscroll->SetCount` 口径统一），彻底不再碰窗口级 `resize.step_*`。

改动点（全部在 `src/depot_gui.cpp`，**纯 .cpp ⇒ 增量构建合法**）：

| 行 | 现状 | 改为 |
|---|---|---|
| `:928` | `Rect ir = r.WithHeight(this->resize.step_height);` | `wid->resize_y` |
| `:929` | `ir.WithWidth(this->resize.step_width, rtl)` | `wid->resize_x` |
| `:957` | `ir.Translate(0, this->resize.step_height)` | `wid->resize_y` |
| `:963` | `cell.Translate(rtl ? -(int)this->resize.step_width : (int)this->resize.step_width, 0)` | `wid->resize_x` |
| `:970` | `ir.Translate(0, this->resize.step_height)` | `wid->resize_y` |
| `:1018-1019` | `x / this->resize.step_width`、`x % this->resize.step_width` | `matrix_widget->resize_x` |
| `:1022` | `(y - matrix_widget->pos_y) % this->resize.step_height` | `matrix_widget->resize_y` |

另加两道**结构性护栏**（不是替代上面，是防回归）：

1. `GetVehicleFromDepotWndPt()` 开头：`if (matrix_widget->resize_x == 0 || matrix_widget->resize_y == 0) return Error;`（任何未来布局再退化为 0 时，**降级为"点空"而不是除零崩**）。
2. `OnResize()`：`num_columns = std::max<uint>(1, nwi->current_x / nwi->resize_x);` —— 矩阵比一个单元格还窄时 `num_columns` 会变 0，而 `:1344` 的 `CeilDiv(size, num_columns)` 是**另一处除零**（现存隐患，本轮顺手堵掉）。

**为什么不改控件树（把标题栏加 `SetResize`）**：那会改窗口可 resize 语义与默认尺寸，波及全部四种车库与 `resize.step_height` 的现有正确取值，风险远大于收益；且 `resize.step_*` 在库语义里本就是"窗口缩放手感"参数，**本就不该被当作单元格尺寸**用。用矩阵自身的 `resize_x/resize_y` 才是本文件其余各处已有的正确口径。

## 四、复测判据

1. **公路车库**：造一辆车 → 在矩阵上左键点车（应选中/拖拽）、右键点车（应弹动作菜单）**均不崩**；图标正常显示、列排布正确。
2. **船坞**：同上（船坞为 3 列）。
3. **机场车库**：同上（机场为 3 列）。
4. **列车车库**：横向滚动、纵向分隔线、逐节拖拽**行为逐像素不变**（本轮对 Train 路径只是把相等的值换了个来源：`step_height` == `wid->resize_y`，`step_width` 未参与）。
5. **日志**：不再出现 `GUI-DEGEN-RECT depot type=1|2|4 cell=…,0,…`（宽 0）形态；空车库点击也正常（原来空库右键同样会除零 —— 现亦修好）。
6. 窗口拉大拉小后重复 1~5（走 `OnResize` 重算 `num_columns`）。

## 五、未确认项

- **U-1**：玩家三次现场是否**空库右键也崩**（按代码应崩）。若玩家印象是"必须先造一辆车才崩"，那是点击位置差异：空库时玩家点的位置未必落在矩阵内。不影响修法。
- **U-2**：`cell_size.extend_left/extend_right` 参与 `resize.width`，故非列车单元格宽 > 单位号+表头+标签列宽之和；`xm` 的语义区间（`xm < tag_width` / `<= header_width` 的落点判定，`:1105` 一带）在本修复后**首次真正生效**，需玩家实测三处落点是否顺手（属体验项，非正确性）。
- **U-3**：`num_columns == 1` 时绘制**仍会**执行 `WithWidth(resize_x)`?? —— 否：`:929` 有 `if (this->num_columns != 1)` 守卫，保持原样不动，避免改变单列外观。

## 六、本轮性质与构建自证

**已定位根因 + 已实现 + 已编译通过，游戏内复测待做。** 仅改 `src/depot_gui.cpp`（未碰任何 `src/*.h`、未碰 `lang/*.txt`），走既有 `_tmp_inc_build.cmd`（**未新建任何 `.cmd`**）。

构建结果（三件套齐全）：

| 项 | 值 |
|---|---|
| 护栏 | `GUARD: newest object = 2026-10-03 19:17:30` / `newer: src\pbs.h (2026-10-03 21:25:14)` ⇒ `REMOVED 620 object file(s) - upgrading this build to a FULL rebuild`（因**上一轮遗留的 `pbs.h` 比全部 obj 新**，与本轮改动无关；本轮改动本身是纯 `.cpp`，本可增量） |
| 日志尾 | `[693/693] Linking CXX executable openttd.exe` |
| 判定 | `build\R3R_incbuild.done` = `EXIT_CODE=0` |
| 错误 | `error C\d+/fatal error/FAILED:/build stopped` 计数 = **0** |
| 时间戳链 | `src\depot_gui.cpp` 18:31:36 → `depot_gui.cpp.obj` 18:48:08 → `build\openttd.exe` 18:59:29（51 671 552 B） |
| 静态检查 | `read_lints` 该文件 **0 条** |
| 残留核查 | 该文件内 `resize.step_width` / `resize.step_height` 命中数 = **0**（已全部换源） |

（构建前已确认 `openttd.exe` 未在运行，避免 LNK1168。）
