# R3R depot 段/散链 UI 备忘（第 17 轮，2026-09-15）

> 范围：本批任务的 **任务 2（链状态标记列）**、**任务 3/4（拖动段整段高亮）**、**KI-42（错误文案清理）**、**任务 5（清单登记）**。
> 关联清单条目：`R3R_KNOWN_ISSUES.md` §4-6、KI-42（已修）、KI-43 / KI-44（未验证）。
> 关联记忆：51063123（段两端假引擎 / ★ 语义）、50765346（free wagon ↔ segment 两级模型）、66636022 + KI-15（改 `src/*.h` 必须全量重编）。

---

## 0. 结论速览

| 项 | 状态 | 产物 |
| --- | --- | --- |
| 任务 2：depot 列车行首「链状态」标记列 | 已实现、编译通过，**未实测** | `depot_gui.cpp` + `lang/english.txt` + `lang/simplified_chinese.txt` |
| 任务 3/4：拖动段内任意车厢 → 整段统一高亮 | 已实现、编译通过，**未实测** | `depot_gui.cpp` + `train_gui.cpp` |
| KI-42：depot 错误文案硬编码 | **已修** | `depot_gui.cpp`（6 处） |
| 构建 | `R3R_quickcheck.cmd` ×2 = `NINJA_EXIT=0`；`_tmp_inc_build.cmd` 链接 = `NINJA_EXIT=0`；`read_lints` 0 诊断 | `build\openttd.exe` @ 2026-09-15 21:15:18，50 480 640 B |

**本轮未触碰任何 `src/*.h`**，因此不触发 KI-15（改头文件后必须全量重编），后续仍可按 `.cpp` 增量构建。

---

## 1. 概念基线（不要重新发明）

- R3R 两级模型：**散链（free wagon chain / loose chain）** ↔ **段（segment）**。
- 段的中段标记 = `Train::flags` 的 `VehicleRailFlag::SegmentFront`（★）；段尾标记 = `VehicleRailFlag::SegmentBack`。
  - `Couple()` 提交点给被挂链链头打 ★、给其链尾打 `SegmentBack`（`train_cmd.cpp` ~5790 区）；
  - depot 的「设为段 / 取消段」命令走 `vehicle_cmd.cpp` 的 `CmdMakeSegment` / `CmdDemoteSegment`（并可能给段尾假引擎化）。
- 段边界**可以不完整**：旧存档 / 异常路径可能只有 ★ 没有 `SegmentBack`。所有"段尾"推导都必须**兜底**为「下一段 ★ 之前的最后一辆，或链尾」——depot 侧既有实现是 `TrainDepotDetachSegment` 内联的那个 `while`，本轮把它抽成 `TrainDepotGetSegmentTail()` 复用。
- depot 拖动语义（本轮之前就正确，别改）：`TrainDepotMoveVehicle` 内部先取 `TrainDepotGetSegmentFront()`，命中段则整段搬运；`_cursor.vehchain` 只是**光标/高亮**的"整链"提示位。
  - `DepotClick`：`this->sel = v->index;`（`v` = 点击到的车或其 artic 组首）
  - `_cursor.vehchain = _ctrl_pressed || seg_front != nullptr;`

---

## 2. 任务 2：链状态标记列

**落点（`src/depot_gui.cpp`）**

| 位置 | 内容 |
| --- | --- |
| `DepotWindow::tag_width`（~L395） | 新增成员；**非列车车库为 0**，整列自动消失 |
| `DrawChainStateTag()`（~L485） | 沿 `Next()` 数 `IsSegmentFront()`：`0` → `STR_DEPOT_CHAIN_LOOSE`；否则 `STR_DEPOT_CHAIN_SEGMENTS`（`{NUM}` = 段数）。绘制在行首 tag 矩形内 |
| `DrawVehicleInDepot()` | 调用 `DrawChainStateTag(u, r, rtl)`（~L537）；`text`（~L512）/`image`（~L513、~L591）/`flag`（~L562）四处 `Rect` 全部 `Indent(tag_width, rtl)` |
| `GetVehicleFromDepotWndPt()` | `xm < tag_width` → 视为点中该车辆本体（~L703）；`xm -= tag_width`（~L704）；counter 判定减 `tag_width`（~L729）；`x -= tag_width + header_width`（~L735） |
| `OnResize()` | `tag_width` 由两条字符串的 bounding box 较大值 + `hsep_normal` 求得（~L874）；`base_width` 加 `tag_width`（~L889）；`hscroll->SetCapacity()` 减 `tag_width`（~L1418） |

**语言文件**：`src/lang/english.txt` / `simplified_chinese.txt` **末尾追加**：

```
STR_DEPOT_CHAIN_LOOSE     :Loose chain            / 散链
STR_DEPOT_CHAIN_SEGMENTS  :Segments: {NUM}        / 段：{NUM}
```

> 追加在末尾是为了不动既有 StringID（避免存档 / 布局位移），但**改了 `lang/*.txt` 就必须重编 `strings.cpp`**（KI-16 / 记忆 52814982）：本轮的做法是先 `touch src/strings.cpp` 再构建，否则 exe 内语言包版本号与新生成的 `.lng` 不匹配，启动报 *No available language packs*。

**已知语义待确认（→ KI-43）**：`segments == 0` 会对「单机车」「造出来的散链」一律显示「散链」。按 R3R 两级模型这是自洽的（单机车本来就不是段），但是否符合玩家直觉需实测拍板；若要区分，可在 `num_segments == 0 && !head->IsFreeWagon()` 时另显一条文案。

---

## 3. 任务 3/4：拖动段内任意车厢 → 整段统一高亮

**问题**：拖动是整段搬（`TrainDepotMoveVehicle`），但高亮帧只从**被抓的那节车厢**画到**链尾**（`_cursor.vehchain` 语义 = 直到 `Next() == nullptr`），与真实行为不一致，玩家无法判断自己拖的是整段还是单节。

**设计取舍（重要，下一轮别推翻）**

1. **`this->sel` 保持"被抓的那节车厢"不动**。
   曾考虑把 `this->sel` 直接改成段首 ★，但那会让 `OnDragDrop` / `OnMouseDrag` 里基于 `sel` 的比较（"落点是否就是自己"）失配：把段内第 3 节拖回自己原本位置时，`result.wagon->Previous()`（= 段内第 2 节）≠ 段首，会绕过 `if (v == wagon) return;` guard 走进 `TrainDepotMoveSegment(seg_front, 段内车厢)` —— 自引用移动。因此改为**只把"段首 index"传给绘制层**。
2. **段首 index 只作为 `DrawTrainImage` 的 `selection` 传入**（`depot_gui.cpp` ~L526-527）：`selection` 在 `DrawTrainImage` 中仅用于高亮帧与落点预览宽度，不影响任何拖动逻辑。
3. **段的右边界**：`train_gui.cpp` 新增 `GetSegmentTail()`（~L64），规则与 `TrainDepotDetachSegment` 完全一致（`IsSegmentBack` → 缺失则退到「下一段 ★ 之前」→ 再退到链尾）。`DrawTrainImage` 用 `sel_head->IsSegmentFront() && _cursor.vehchain` 判定"这次拖的是段"，并用 `sel_frame_done` 在到达段尾后停止扩展高亮帧（~L148-149、~L174-180）；`HighlightDragPosition()` 的预览宽度同样以段尾封顶（~L89-93）。
4. **必须同步修 `OnCTRLStateChange`**（~L1429）：它的 `_cursor.vehchain = _ctrl_pressed;` 会把"段拖动"降回单节（用户按下/松开一下 ctrl 就出现高亮跳变），改为 `_ctrl_pressed || GetDraggedSegmentFront() != nullptr`。`DepotClick` 同样用该表达式（~L776）。
5. `CountDraggedLength()`（~L469-470）按**整段**求和，否则拖动时行末 count 列会把段后面的车厢也算进去。

**副作用（有意保留）**：`DrawTrainImage` 的"段尾截断"对**所有**调用方生效，包括 `vehicle_gui.cpp` 车辆列表与 `tbtr_template_gui_create.cpp` 虚拟列车窗口。即那些窗口若恰好以某个 ★ 车为 `selection` 且 `_cursor.vehchain` 为真，也会只框住该段。这符合 R3R「段是单位」的语义，但**未实测**（→ KI-44）。

---

## 4. KI-42：错误文案硬编码

`src/depot_gui.cpp` 中 6 处 `STR_ERROR_CAN_T_BUY_TRAIN + to_underlying((*begin)->type)` / `+ to_underlying(v->type)` 全部改为 JGRPP 的 `GetCmdBuildVehMsg(*begin)` / `GetCmdBuildVehMsg(v)`（`vehicle_func.h`，按 `VehicleType` 显式映射，不依赖枚举顺序与字符串相邻性）。落在 clone 与 buy 两条路径（~L1177-1244）。

---

## 5. 复测清单（交用户）

1. depot 里 **散链 / 单段 / 多段** 三种列车行首标记是否正确；`段：{NUM}` 的 `NUM` 是否为 ★ 数量。
2. 抓**段内第 2、3 节车厢**拖动：高亮帧是否覆盖**整段**（而非只剩后半截/延伸到链尾）。
3. 拖动到别处时，落点灰色预览宽度是否 = 整段宽度；行末 count 列是否只加整段长度。
4. 拖动期间**按一下再松开 ctrl**：段拖动跨度不应缩回单节。
5. **回归**：拖动散链车厢（无 ★）行为不变——不按 ctrl 只搬单节、按 ctrl 搬整链。
6. **回归**：公路 / 船 / 飞机车库行内容不得出现位移（`tag_width == 0`）。
7. 中英文各看一遍宽度是否够（尤其 `段：100` 这类三位数）。RTL 语言理论走 `rtl` 分支，未验证。
