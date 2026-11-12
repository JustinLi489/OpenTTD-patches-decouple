# R3R 车站调度分离 —— 调度池（第 416 轮）落地备忘

编写时间：2026-11-10。对应 KI：KI-586。

## 1. 本轮目标（玩家口径）

1. 坑位（同一份班次可同时发给几列车）默认 1，上限 256，**放到设置里让玩家自己调**。
2. 池管理界面放在车站 → 站场窗口，「场」下拉旁边。
3. 领取语义：列车跑完自己的命令 3（带「等待分配」标记的到站命令）后领到池表，从 `idx = 0` 开始跑，跑完池表最后一条命令后把命令指回自己表的命令 4。
4. 其余改动全量重编。

## 2. 数据模型（沿用 E-2，不建独立 OrderList）

`Station::R3RStationYard` 增加：

```cpp
struct R3RDispatchPoolEntry {
    VehicleID host = VehicleID::Invalid(); ///< 模板车（表本体挂在它的 orders 上）
    uint16_t limit = 1;                    ///< 坑位：同时可领用的列车数
    uint16_t issued = 0;                   ///< 当前已发出的份数
};
std::vector<R3RDispatchPoolEntry> pool;
```

为什么不用 `OrderList`：`OrderList::first_shared` 是 `Vehicle*`，全库 9 处 `GetFirstSharedVehicle()` 无条件解引用，无主表必崩（KI-169a 同族）。

区分两个容量概念：

| 概念 | 含义 | 出处 | 上限 |
| --- | --- | --- | --- |
| 池条目数 | 一个场里有几份班次 | `order.r3r_dispatch_pool_capacity`（设置，默认/最大 256） | 256 |
| 坑位 `limit` | 一份班次同时给几列车 | 每条条目自己的字段 | `capacity`（同时硬钳 1..256） |

## 3. 命令层

`command_type.h` / `station_cmd.h` / `station_cmd.cpp` 新增三条，全部 `CommandType::OtherManagement`：

| 命令 | 数据 | 说明 |
| --- | --- | --- |
| `AddR3RDispatchPoolEntry` | `StationID, uint16_t yard, uint32_t host_index, uint16_t limit` | 校验 `R3RDispatchHostUsable`（活车 / 有自己的表 / 表内无 `OT_DECOUPLE`、`OT_GOTO_COUPLE`、`OT_WAIT_COUPLE`） |
| `RemoveR3RDispatchPoolEntry` | `StationID, uint16_t yard, uint16_t index` | 已发出的份数随条目丢弃，借用方跑完静默归还 |
| `SetR3RDispatchPoolLimit` | `StationID, uint16_t yard, uint16_t index, uint16_t limit` | 只影响后续发牌，不打断在跑的 |

三条命令执行后 `InvalidateWindowData(WindowClass::StationDispatch, st->index, 0)`。

## 4. 订单标记（等待分配）

- `order_base.h`：xflags bit6 + `HasR3RWaitDispatch()` / `SetR3RWaitDispatch(bool)`。
- `order_type.h`：`MOF_R3R_WAIT_DISPATCH`（`MOF_END` 之前）。
- `order_cmd.cpp`：`OT_GOTO_STATION` 白名单加入该 flag；预检分支（Train + 站订单 + `data <= 1` + 冲突检查）；执行分支 `order->SetR3RWaitDispatch(data != 0)` + `SetTimetableWindowsDirty`。
- 订单窗口：`WID_O_R3R_SEPARATION` 由 `WWT_TEXTBTN` 改为 `NWID_BUTTON_DROPDOWN`，**主区仍直接切换「自动调节班次」**（保持第 415 轮玩家验收过的手感），箭头展开带勾选的两项菜单（0 = 自动调节班次，1 = 等待分配）。宽度仍是 30px，不动 `WID_O_SEL_TOP_YARD` 的 60px 槽位（KI-262）。

> 勾选态用 `MakeDropDownListCheckedItem`，不是 `ShowDropDownMenu` 的位掩码 —— 本 fork 的 `DropdownWindow` 只用 `selected_result` 做高亮（`dropdown.cpp` 内 `item->result == this->selected_result`），传位掩码不会有勾。

## 5. 运行期

- `train_cmd.cpp`：`TrainDispatchHandler()` 在停稳 + `OT_GOTO_STATION` + `HasR3RWaitDispatch()` + 真停在该站时，用 `R3RGetYardOfTile` 找场 → `R3RDispatchPoolPick` 挑条目 → 无坑位则原地等（节流日志 `DISP-POOL-NOSLOT`）→ 成功则 `Acquire` + `R3RBeginBorrow(..., true)` + 备份 `cur_timetable_order_index` + `r3r_follow_pool = true` + 三个索引清零 + `current_order.Free()`。
- `R3RReturnDispatchPool()`：池表末条跑完、索引将回绕那一瞬归还 —— 清标志 → `R3RReturnBorrowedOrders(v, false)` → `cur_real_order_index = orders_backup_real_index + 1`（越界回 0）→ 时刻表索引沿用备份 → `Free()` + `SetDestTile(INVALID_TILE)` + `InvalidateVehicleOrder` → 遍历 `Station::Iterate()` 调 `Release`。
- `vehicle_base.h`：`SkipToNextRealOrderIndex()` / `IncrementImplicitOrderIndex()` 首行拦 `r3r_follow_pool` 并直接返回（避免绕过归还判定）。
- `R3RPushProgressToOwner()` 开头对 `r3r_follow_pool` 直接 return（防池表坐标写回模板车，KI-215b 同族）。

## 6. 存档

- `sl/couple_group_sl.cpp`：R3VP 稀疏块加 `order_backup_index_t`（`orders_backup_timetable_index`）与 `follow_pool`；`R3RHasParkState()` 相应扩判据。
- `sl/station_yard_sl.cpp`：SYRD 升 **v6**（MAGIC `0xFFFFFF05`）。每组在场名之后、tile 数之前多一个「池块」：

```
word  条目数 P
P 组 × 3 word：模板车序号 + 1（0 = 无效）、limit、issued
```

读档约束：**解码阶段不做任何车辆存在性校验**（SYRD 与 VEHS 的加载顺序无保证），只做纯数值钳制（`limit` 钳 1..256、`issued` 钳到 `limit`、条目数钳 4096）。失效条目留给运行期 `Prune/Count/Pick` 兜底。v2~v5 老档读回空池。

## 7. 界面

`station_widget.h`：`StationYardWidgets` 加 `WID_SY_DISPATCH`；新增 `StationDispatchWidgets`（`WID_SD_CAPTION / YARD_SEL / LIST / SCROLLBAR / ADD / DELETE / SLOTS_DOWN / SLOTS_UP`）。

`station_gui.cpp`：
- 站场窗口第一行「场」下拉之后加「调度池」按钮（60×12，行宽 90+60+60+60=270 < 既有 330），无有效场时禁用。
- 新增 `StationDispatchWindow` + `_station_dispatch_desc`（`WindowClass::StationDispatch`，ini key `r3r_station_dispatch`，340×200）：
  - 列表一行 = `{VEHICLE}: N 条命令, 坑位 used/limit`；
  - 「添加」下拉遍历全图列车，排除已在池里的与不能当模板的；
  - 「删除」走 `ShowQuery` 确认；
  - 「-」/「+」改坑位（钳 1..256）。
- 行距取自 `WID_SD_LIST` 控件自身的 `resize_y`（KI-262 同族：兄弟滚动条 `resize_y = 0` 会把容器步长拉成 0，所有行叠在顶部）。

语言串新增：`STR_R3R_DISPATCH_POOL_BUTTON(_TOOLTIP)`、`_YARD_SEL`、`_YARD_NONE`、`_LIST_TOOLTIP`、`_ADD_SEL`、`_ADD_EMPTY`、`_DELETE_CONFIRM`、`_CAPTION_STATION`。

`STR_R3R_DISPATCH_POOL_ENTRY` 保持 `{RAW_STRING}: {NUM} ...`（**不能**改成 `{VEHICLE}`：本 fork 的 strgen 在含复数标记 `{P}` 的串里遇到 `{VEHICLE}` 会 FATAL「Command 'VEHICLE' has no (default) plural position」，把整个 `lang/afrikaans.txt` 编译打挂）。GUI 侧先用 `GetString(STR_VEHICLE_NAME, id)` 取车名再拼。

## 8. 已知风险 / 待观察

1. **`issued` 泄漏**：模板车被卖掉/解挂时不归还 `issued`，坑位会一直占着（`Prune` 只在 `issued > limit` 时清零）。运行期 `Prune` 会剔除失效 host，但已发出份数不会自动回收。
2. **SYRD v6 不再可被旧 exe 读**（老引擎读到未知 MAGIC 会整块丢弃并报存档不兼容）。玩家如要回退版本，需先备份。
3. 池窗口直接读 `st->r3r_yards[y-1].pool`（公有成员），没有走只读访问器；与站场窗口读法一致。
4. `TrainDispatchHandler` 只在列车**停稳**分支调用，行驶中不会领表；到站未停稳那一帧不会误领。

## 9. 构建自证（2026-11-10 04:25）

- 命令：`R3R_fullbuild.cmd inc detach`（改了 `src/*.h`，guard 判定 `REMOVED 620 object file(s) - upgrading this build to a FULL rebuild`）。
- 结果：`build\R3R_incbuild.done` = `EXIT_CODE=0`；日志尾 `[27/27] Linking CXX executable openttd.exe`；`build\openttd.exe` mtime `2026-11-10 04:24:52` 晚于最新源码 `04:19:45`；obj 620 个；exe 52,697,088 字节。
- 中途踩到并已修的四个编译/构建错误（按出现顺序）：
  1. `lang/simplified_chinese.txt` 用了 `{NEWLINE}` —— 本 fork 无此命令，改为空格连接；
  2. `STR_R3R_DISPATCH_POOL_ENTRY` 用 `{VEHICLE}` + `{P}` → strgen FATAL，回退 `{RAW_STRING}`；
  3. `station_widget.h` 的 `///<` 注释里写了「第 416 轮」→ doc 生成把中文截断成换行，`script_window.hpp` 语法崩（KI-584 同族，控件注释必须纯 ASCII）；
  4. `OrderList` 没有 `GetOrder()`（改 `GetOrderAt(static_cast<VehicleOrderID>(i))`）、`MakeDropDownListStringItem` 需 rvalue、`ShowErrorMessage` 两参数须为 `EncodedString`、`train_cmd.cpp` 缺 `<set>`、`DestinationID` 需 `.ToStationID()`。

