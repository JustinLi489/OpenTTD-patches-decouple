# R3R 第 241 轮临时分析报告：列车「卡住」时状态行不显示「等待空余轨道」的定性

- 日期：2026-10-11
- 主题：状态行（VehicleView / 车辆列表）显示「{速度} - 正前往 {站}」而不是「等待空余轨道」
- 性质：**只读取证 + 定性，未改任何源码，未构建**
- 玩家原话：「我也不怎么确定，我就说吧，列车卡在那里的时候，没有显示等待空余轨道，而是显示速度为0和前往车站。你可以继续确认」

---

## 一、结论（先说结果）

**这不是显示（GUI）逻辑的 bug。** 状态行的 `Stuck` 判据排在全部订单类型分支**之前**；
玩家能看到「速度为 0 + 正前往车站」，只有一种解释：

> 当时该车的 `VehicleRailFlag::Stuck` 位是 **0**，即引擎根本没有把它判定为「卡住（等待空余轨道）」。

而 R3R 场景下「车停着不动但不算 Stuck」有三条**结构性**路径（都在 `train_cmd.cpp`），
其中第 1 条（库内）在代码注释里就被明确写成「库里永远是安全格，不可能 stuck」。

---

## 二、GUI 侧证据（显示优先级没问题）

`src/vehicle_gui.cpp` 的状态行分支顺序（节选）：

```4663:4665:src/vehicle_gui.cpp
} else if (v->type == VehicleType::Train && Train::From(v)->flags.Test(VehicleRailFlag::Stuck) && !v->current_order.IsType(OT_LOADING) && !mouse_over_start_stop) {
    append(Train::From(v)->flags.Test(VehicleRailFlag::WaitingRestriction) ? STR_VEHICLE_STATUS_TRAIN_STUCK_WAIT_RESTRICTION : STR_VEHICLE_STATUS_TRAIN_STUCK);
```

- 4663 是 `Stuck` 分支，**位于** 4674 起的 `switch (v->current_order.GetType())` **之前**。
- 紧随其后（未到达该分支时）才是订单类型分支：

```4729:4740:src/vehicle_gui.cpp
case OT_GOTO_COUPLE:
    SetDParam(0, ...);
    if (GetCoupleIsDepot()) {
        append(GetVehicleStatusForDepot(...)); /* STR_VEHICLE_STATUS_HEADING_FOR_DEPOT_VEL */
    } else {
        append(STR_VEHICLE_STATUS_HEADING_FOR_STATION_VEL);
    }
    break;
```

对应语言串（`src/lang/simplified_chinese.txt`）：

```4610:4616:src/lang/simplified_chinese.txt
STR_VEHICLE_STATUS_TRAIN_STUCK                                  :{ORANGE}等待空余轨道
STR_VEHICLE_STATUS_HEADING_FOR_STATION_VEL                      :{LTBLUE}{1:VELOCITY} - 正前往 {0:STATION}
STR_VEHICLE_STATUS_HEADING_FOR_DEPOT_VEL                        :{ORANGE}{1:VELOCITY} - 正前往 {0:DEPOT}
```

⇒ 「速度为 0 + 正前往车站」= 走 `OT_GOTO_COUPLE` 非车库分支（或 `OT_GOTO_STATION`/`OT_DECOUPLE`），
**前提是 `Stuck == false`**。

### 附：一个会骗人的细节

4663 的条件里有 `!mouse_over_start_stop`。**鼠标悬停在「开始/停止」按钮上时**，即使列车是 Stuck 的，
也会跳过 Stuck 分支、落到订单分支显示「正前往 …」（并把文字染成对应颜色，见 4770-4774）。
如果玩家是「把鼠标停在按钮上读状态行」，那也会看到「正前往」。这一条属干扰项，需排除。

---

## 三、Stuck 置位/清除路径（`train_cmd.cpp`）

| 行号 | 位置 | 行为 |
|---|---|---|
| 3794 | `MarkTrainAsStuck()` 定义 | 置 Stuck + 清速度 + 刷新 `WID_VV_START_STOP` |
| 4460-4464 | `CheckNextTrainTile()` 开头 | **`moving_front->track == TRACK_BIT_DEPOT` ⇒ 无条件 `Reset(Stuck)` 并 return**（注释：Can't be stuck here as inside a depot is always a safe tile） |
| ~4506 | 同上（wormhole：隧道/桥） | Reset Stuck |
| 4538 | 同上（PBS 段预留失败） | `MarkTrainAsStuck(consist)` |
| 4540-4543 | 同上（**非 PBS 段**） | `Reset(Stuck); wait_counter = 0;`（注释：A train not inside a PBS block can't be stuck） |
| 12687 | 出库路径 | `MarkTrainAsStuck(v)` |
| 13674 / 13719 / 13844 / 13877 / 13899 / 13945 / 13955 / 14207 | `TryPathReserveWithResultFlags()` 内各失败出口 | 视 `mark_as_stuck` 参数置 Stuck |
| 14196 | 同上，**预留成功** | `Reset(Stuck)` + `wait_counter = 0` |
| 17249-17288 | `TrainLocoHandler()` 站台「等待者」块 | 仅 `ReserveTrackUnderConsist()` + 逐格预留，**从不 MarkTrainAsStuck** |
| 17330 | 同上，玩家手动停车早退 | `vehstatus.Test(Stopped) && cur_speed == 0 && 非 depot-couple` ⇒ `return true` |
| 17419-17494 | 同上，**库内 GOTO_COUPLE 等待/重试块** | 每 tick `TrainCoupleHandler()`，并强制 `consist->cur_speed = 0; consist->progress = 0;`（17476-17477），**从不 MarkTrainAsStuck** |
| 17584 | `DEPOT-ARR` 探针 | 字段中含 `stuck=%d`（可直接取证） |
| 17966 起 | 「Handle stuck trains」块 | 仅在**已经 Stuck**时递增 `wait_counter` / 决定掉头 |

---

## 四、为什么「卡住」却不是 Stuck —— 三条路径

### 路径 1（最可能）：库内 GOTO_COUPLE 等挂接目标

- `CheckNextTrainTile()` 开头（4460-4464）对 `track == TRACK_BIT_DEPOT` **无条件清 Stuck**。
- 库内 GOTO_COUPLE 机车每 tick 走 17419-17494 的重试块，只把速度钉成 0（17476-17477），
  既不置 Stuck，即使被别处置了也会在下一 tick 被 4460 清掉。
- ⇒ 状态行按 `current_order` 渲染：`GetCoupleIsDepot()` 为真显示橙字「正前往 {车库}」，
  否则显示蓝字「正前往 {车站}」= **玩家看到的现象**。
- 这正是 KI-339/KI-340 现场（机车在库里反复 SPLICE-GAP-REJECT / FOLDCHK-SKIP 每 tick 重试）时的显示。

### 路径 2：非 PBS 段列车

4540-4543：非 PBS 段一律清 Stuck。若机车停在非 PBS 区段等挂接，同样不会显示「等待空余轨道」。

### 路径 3：站台「等待者」（不是「前往车站」）

17249-17288 的 waiter 块（`R3RIsCarOnlyFormation` 或 `IsPrimaryVehicle() && current_order.IsType(OT_WAIT_COUPLE)`）
停稳在站台格时，每 tick 只做站台预留，不置 Stuck。
但这类车 `current_order` 是 `OT_WAIT_COUPLE` ⇒ 显示的是「等待时间表」分支，**不是**玩家看到的「前往车站」。
故可排除。

---

## 五、复核用的取证方法

下次复现时直接读 `build\R3R_debug.log`：

1. `DEPOT-ARR ... stuck=%d ...`（17584 探针）—— 直接给出该车当时的 Stuck 位；
2. `TRP veh=… ok=… res=… mstuck=…` —— 同上；
3. 若 `stuck=0` 而车 `cur_speed=0` 不动 ⇒ 确认属路径 1/3 的「等待」，不是引擎认的「卡住」；
4. 若确实 `stuck=1` 却显示「前往车站」⇒ 那才是真 bug（检查 `mouse_over_start_stop` 与 4663 前置分支），需再次上报。

---

## 六、要不要改？（待玩家拍板，本轮未动任何代码）

- **方案 A（推荐，语义正确）**：新增一个专用状态串（例如「等待挂接目标」/`STR_VEHICLE_STATUS_TRAIN_WAITING_COUPLE`），
  在 `OT_GOTO_COUPLE` 且**已停稳（`cur_speed == 0`）且本 tick 未找到可挂目标**时显示，替代「正前往 …」。
  改动落点：`vehicle_gui.cpp` 的 `OT_GOTO_COUPLE` 分支 + `lang/english.txt` / `lang/simplified_chinese.txt` 新增串。
  （新增语言串 ⇒ 需重生成 `strings.h`，属全量/近全量重编。）
- **方案 B（不推荐）**：让这条等待路径也 `MarkTrainAsStuck()`。
  后果：显示「等待空余轨道」与事实不符，且会触发引擎的掉头自愈逻辑（17966 块 / `wait_counter` 递增、
  可能掉头或强制通行），有把车开走的风险。

---

## 七、未确认项

- **U-1**：玩家当时看到的到底是「正前往 {车站}」还是「正前往 {车库}」？两者分别对应
  `GetCoupleIsDepot()` 的真/假，能区分是库内等待还是站台等待。
- **U-2**：玩家观察时鼠标是否悬停在「开始/停止」按钮上（`mouse_over_start_stop` 干扰项）。
- **U-3**：该车当时的 `current_order` 究竟是 `OT_GOTO_COUPLE` 还是 `OT_GOTO_STATION`
  （若是 `OT_GOTO_STATION` 且 Stuck 未置位，则属另一类问题，需另立条目）。
- **U-4**：现场日志 `build\R3R_debug.log` 中该车的 `stuck=` 读数尚未取得。
