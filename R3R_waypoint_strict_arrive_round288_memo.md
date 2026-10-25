# R3R 第 288 轮：等待区路点「严格判据」落地后不完成排程（取证中）

## 一、玩家原话（2026-10-23）

> 于是列车不选择停好停稳，选择了不完成排程（狗头）

现场日志：`D:\temp\codebuddy-dropped-files\6d33abd9-c9ee-4011-a85a-2430eeda3abb\R3R_debug.log`
（产生者应为 build\openttd.exe @2026-10-23 20:13:27，即第 287 轮「严格判据」版）

## 二、现象（玩家表述）

第 287 轮把 `src\station_cmd.cpp` 的到达判据改成
`r3r_head_stop ? (x >= stop && consist->cur_speed == 0) : (x == stop)` 之后：

- 好的一面：不再「高速进格被硬刹 + 订单被抢跑推成 16」（第 287 轮要堵的就是这个）；
- 坏的一面：**订单不完成了** —— 车既没有判到达，也没有推进到 `OT_GOTO_COUPLE`，
  于是整条排程卡在路点这一条订单上。

## 三、猜测根因（待验证，先落盘）

- **G-288-1（首选）**：车确实停稳了，但停稳位置不满足兜底的车位判据。
  `R3RAdvanceStoppedWaitWaypointOrder()` 除了 `R3RTrainStopsAtWaypoint()` + `cur_speed == 0`，
  还要求「运动前端仍在 `IsRailWaypointTile` 且 station_index == `current_order.GetDestination()`」。
  若 look-ahead 的 0 速目标停在**路点格之前**（`x < stop` 的同格早段或前一格），
  则 `VehicleEnterTile_Station()` 的严格判据永不成立（它就是靠「进格那一 tick」判的），
  兜底的车位判据也不成立 ⇒ 无人推进订单。
  验证：日志里 `WP-NOARRIVE` 与 `WP-STOP-ADVANCE` 双双零命中，且车停住的 tile ≠ 路点 tile。
- **G-288-2**：车没真正停稳（速度在 0 与非 0 之间抖动，或停住又被 look-ahead 推一下），
  兜底每 tick 都看到 `cur_speed != 0`。
  验证：有 `WP-NOARRIVE`（说明进格时非零）但无 `WP-STOP-ADVANCE`，且 `SPDD` 里能看到 0 ↔ 非 0 往复。
- **G-288-3**：兜底调用点没走到。`R3RAdvanceStoppedWaitWaypointOrder()` 在
  `TrainLocoHandler` 的停驶分支（`j < adv_spd` 内 `cur_speed == 0`）里被调用；
  若 `j >= adv_spd`（例如 adv_spd 被算成 0），停驶分支不执行。
  验证：日志里完全看不到该分支的伴生探针（`SKIP-STOPPED` / `SetLastSpeed` 附近）。
- **G-288-4**：兜底要求「运动前端仍在 `IsRailWaypointTile`」，但车头已越过路点格
  （`x > 15` 进入下一格）⇒ 同样不推进。验证：车头 tile 比路点 tile 靠前。

## 四、复测/取证判据

1. `WP-NOARRIVE` 与 `WP-STOP-ADVANCE` 各自的命中数（双零 ⇒ G-288-1/3/4；只 NOARRIVE ⇒ G-288-2/4）。
2. 停稳那几 tick：车头 tile、`x`、`cur_speed`、`current_order` 类型。
3. 有没有 `SPDD ... to=0`（谁把速度归零：look-ahead 自然刹停还是别的钳制）。
4. 负判据：普通站台停靠不受影响。

## 五、取证结论（2026-10-23 20:17 日志，1537 行 / 234 682 B，产生者=第 287 轮 exe）

### 5.1 计数（`Select-String` 实测）

| 标签 | 命中 |
| --- | --- |
| `WP-NOARRIVE` | **15** |
| `WP-STOP-ADVANCE` | **0** |
| `COUPLE-FAIL` | **0** |
| `SPDD` | 1269 |

⇒ 严格判据在工作（15 次拒绝“到达”），但兜底推进**一次都没跑**，也从没进入挂车阶段。

### 5.2 15 行 `WP-NOARRIVE` 全文（`R3R_debug.log:716` 起）

```
WP-NOARRIVE veh=21 tile=254,233 x=1 stop=1 spd=53
WP-NOARRIVE veh=21 tile=254,233 x=2 stop=1 spd=51
...（每 tick x+1、spd 约 -2）...
WP-NOARRIVE veh=21 tile=254,233 x=14 stop=1 spd=15
WP-NOARRIVE veh=21 tile=254,233 x=15 stop=1 spd=15
```

**关键读数（`:716` 同一批同 tick 的 SPDD）**：
`SPDD veh=21 ... tile=248,233 x=3971 db=1 order=6 real=11 ...`
⇒ `VehicleEnterTile_Station()` 收到的 `tile=254,233` 是**运动前端那节车厢正在进入的路点格**，
而链头 `consist->tile` 还在 `248,233`（差 6 格 = 约 12 节 × 8px）。
所以探针里的 `spd` 打的是**整列** `consist->cur_speed`，`x` 是那节车在格内的偏移。链长 ≈ 6 格。

### 5.3 车的实际行为：不是“不选停好”，是**根本停不下来然后冲过去**

- 进格 `x=1` 时 `spd=53`，在格内 15 tick 单调降到 `spd=15`，**从未到过 0**；
- `x=15` 之后出格，速度**不再下降**，继续沿轨道前进；
- 日志尾部（`:1490-1537`）：`veh=21 tile=269,235 → 276,235`、`spd=208 → 253`、`order=6 real=11 stuck=0`、`dtile=479486` 恒定
  ⇒ 车已冲过路点 254,233 约 22 格、一路加速到 253，订单始终停在 `OT_GOTO_WAYPOINT`。

### 5.4 判定

- **G-288-1 / G-288-2 合并成立**：车**从未停稳**（整段轨迹里 `cur_speed` 最小只到 15），
  所以严格判据 `x >= stop && cur_speed == 0` 永远为假 —— 这是判据**按设计**的结果；
  兜底 `R3RAdvanceStoppedWaitWaypointOrder()` 也因此一次都没跑（`WP-STOP-ADVANCE=0`）。
- **G-288-3 / G-288-4 排除**：不是因为车停错了格、也不是因为停驶分支没走到，而是**压根没停**。
- **真根因（新，D-288-1）**：第 286 轮那条“原生 0 速目标”在实际运行中**没有把车在路点前刹住**：
  从 `spd=65` 一路到进格只剩 `spd=53` 是极缓慢的自然减速，进格后每 tick 只掉 2~3，
  说明 advisory 的 0 速目标要么位置太近（`stop=1` = 格入口），要么根本没覆盖到路点。
  ⇒ 这不是“判据写错”，而是“**给了严格判据却没有任何机制把车送到停车标上**”。

### 5.5 与玩家两条口径的关系

- R287-P1（严格判据）= 已生效，且**必须保留**；它正是逼出这个真问题的工具。
- R287-P2（拒绝减速钳制）= 仍然遵守：正确修法不是把 `(stop-x)*20-15` 加回路点，
  而是让**原生 look-ahead 的 0 速目标**按正常站台停车那样从远处生效，由 realistic braking 自然刹停。

### 5.6 待办（下一组取证）

1. 读 `train_cmd.cpp` 的 `PredictStationStoppingLocation()` 与 `GetTrainStopLocation()`
   的 `r3r_head_stop` 分支，确认 0 速目标的 `limit_advisory_speed()` 位置与生效条件；
2. 确认 look-ahead 在车距路点 6 格时是否已包含该站 item（若没有 ⇒ 目标出现太晚，53 刹不住）；
3. 若确认目标只在格内生效：修法候选 =
   (a) 把 `GetTrainStopLocation()` 对 `r3r_head_stop` 的返回值由 `1` 改为**格末**（如 `TILE_SIZE - 1`），
       让停车标真正落在格末、advisory 在全格范围内持续把目标压到 0；
   (b) 修 look-ahead 让路点站更早进入 item 列表。
   **两者都不引入任何速度钳制**（仍由原生刹车执行）。

## 六、玩家三条口径（2026-10-23，原话要点）

1. 「停车标这个毕竟是一个相对位置」——不要把它当成绝对像素/格末，改的时候必须按相对量算。
2. 「我希望是线性减速，因为我观察到原生游戏中似乎就是这样的」——即质疑「到 15 就不能再降」。
3. 「兜底基本没必要，反正不符合条件的列车甚至不会把路点当成安全停车位」——撤掉兜底分支。

## 七、复核取证（2026-10-23，对照口径 1/2）

### 7.1 「15」不是减速模型的下限，是「已越过目标点」的地板

- `src\train_cmd.cpp:81` `REALISTIC_BRAKING_MIN_SPEED = 5` ⇒ 模型真正的地板是 **5**（`:1144`/`:1192` 两处 `speed_sqr <= 25 ⇒ return 5`）。
  所以「速度到 15 就再也不能线性减速」这个说法**不成立**，模型本身可以一路按 v² 减到 5。
- 真正的 15 出现在 `LimitSpeedFromLookAhead()`（`:1198-1216`）：
  ```cpp
  if (position <= current_position) {                 // :1200  已经越过这个 0 速目标
      max_speed = std::min(max_speed, std::max(15, end_speed));   // :1201
  }
  ```
  ⇒ 车头一旦越过 0 速目标点，上限就被抬到 **15**，之后再来多少条 0 速目标都压不下去（min 只会取更小值，但每条都先被抬成 15）。
  现场「进格 x=1 时 53 → 格内单调降到 15 → x=15 仍 15 → 出格加速到 253」逐字对应这条：**减速是真在减，只是在 15 处被地板卡住**。

### 7.2 「停车标」现在有**两套互不一致**的相对位置（真 bug）

| 取值处 | 路径 | `r3r_head_stop` 时的返回值 |
| --- | --- | --- |
| `GetTrainStopLocation()` `:853-854` | 仅 `lookahead == nullptr`（`:1375` 的旧路径，`:1383` 唯一调用点） | **`return 1;`**（车头中心停在格入口内 1/16） |
| `PredictStationStoppingLocation()` `:1008-1106` | realistic braking + look-ahead 实际在走的路径（`:1238`） | **没有 `return 1` 覆盖**，落到 `osl = FarEnd` ⇒ `stop = station_length`（`:1099-1101`）= **格末**，再 `+ adjust`，不减半车长（`:1104-1105`） |

⇒ 287 轮我以为「停车标 = 1」是现场生效值，实际 look-ahead 用的是格末；两套语义并存，谁生效取决于有没有 look-ahead。
口径 1 因此成立：**必须先把这两处统一成同一个相对量计算，再谈慢不慢**。

### 7.3 判据的物理边界（解释为何「出格即永久失效」）

`x` 是**格内偏移**（0..15）。`VehicleEnterTile_Station()` 只在「进格那一 tick」按 `x >= stop && cur_speed == 0` 判；
- `stop = 1` ⇒ 车在 x=1..15 任一时刻速度为 0 才算到达；现场最小只到 15 ⇒ 15 次 `WP-NOARRIVE` 后出格；
- 出格后 `x` 归 0、`tile` 已不是路点格 ⇒ 该判据**不再被调用**，永久失效。
- 注意 `stop` 必须落在 0..15；写成 `TILE_SIZE`(16) 会让 `x >= 16` 在格内永不成立，反而造出新的死判据。

### 7.4 新疑点（待验证）

- **Q-288-A（高）**：`limit_advisory_speed()` 写的是 `advisory_max_speed`，它只有在
  `lookahead->flags.Test(TrainReservationLookAheadFlag::ApplyAdvisory)` 为真时才会并进 `max_speed`（`:1462-1464`）。
  若路点这一趟 `ApplyAdvisory` 没有置位，TRLIT_STATION 的 0 速目标**根本不参与限速**，
  现场那点微弱的 65→53 减速就只能来自 `:1452` 的 `reservation_end_position`（预留终点 0 速）。
  待验证：`ChooseTrainTrack` 里 `CTTLASF_STOP_FOUND` / `ApplyAdvisory` 的置位条件，以及路点是否被算作「stop found」。
- **Q-288-B（中）**：`PredictStationStoppingLocation` 的 `r3r_head_stop` 判据用 `R3RWaypointOrderStopsForCouple(v, order)`，
  而 `GetTrainStopLocation` 用 `R3RTrainStopsAtWaypoint(front)`（`:752`）+ `osl` 分支（`:768`），两套判据函数不同，
  存在「一处认、一处不认」的错位可能。

## 八、本轮修法口径（第 288 轮已落地，见 §九）

1. **统一停车标**（对应玩家口径 1）：让 `PredictStationStoppingLocation()` 与 `GetTrainStopLocation()`
   共用同一段 `r3r_head_stop` 相对位置计算，不再一处 `return 1`、一处 FarEnd。
   具体相对量（`1` = 格入口内 1/16，还是 `FarEnd - 半车长` = 车头中心停在格末内侧）需按「车头压进路点格」的语义定一个，不许写死第二套。
2. **允许真减速到停**（对应玩家口径 2）：停车目标的 0 速在地板 `max(15, end_speed)` 处不应把车抬到 15；
   停车类目标（`end_speed == 0` 且必须停）越过目标点后应继续压到 `REALISTIC_BRAKING_MIN_SPEED`(5) 再由到达归零，
   这样玩家看到的就是从远处一路减速、进格后收尾、停住，而不是「卡在 15 冲过去」。
3. **撤掉兜底**（对应玩家口径 3）：`R3RAdvanceStoppedWaitWaypointOrder()` 相关分支不再作为到达补充路径。
4. 仍然**不加**任何格内速度钳制（R287-P2 不变）。

## 九、第 288 轮落地记录（玩家批准「好，按照这个试试吧」）

### 9.1 停车标统一为格内相对量（口径 1）

新增单一来源（`src/train_cmd.cpp`，插在 `GetTrainStopLocation()` 注释块之前）：

```cpp
static int R3RWaitWaypointStopOffset(const Train *v)
{
    const int rounding = v->IsDrivingBackwards() ? 2 : 1;
    const int rel = (int)TILE_SIZE - ((v->gcache.cached_veh_length + rounding) / 2);
    return Clamp(rel, 0, (int)TILE_SIZE - 1);
}
```

- `GetTrainStopLocation()`：原来等待区路点写死 `return 1`（车头中心压进格内 1/16）改为
  `return R3RWaitWaypointStopOffset(v)`。
- `PredictStationStoppingLocation()`（realistic braking + look-ahead 实际使用的那条路径）：
  原来落到 FarEnd（格末），现在在 `adjust--` 之前加同源返回 `if (r3r_head_stop) return R3RWaitWaypointStopOffset(v);`。
- 这样「减速目标点」与「到达判据」用的是同一个数：车头中心停在格末内侧半个车身处。
  `Clamp(..., 0, TILE_SIZE-1)` 是硬要求 —— 写成 `TILE_SIZE`(16) 会让 `x >= 16` 在格内永不成立，
  造出永不推进的死判据。

### 9.2 收尾允许压到 5（口径 2）

`LimitSpeedFromLookAhead()`（`src/train_cmd.cpp:1231`）的「目标已在身后」分支：

```cpp
const int floor_speed = (end_speed == 0) ? REALISTIC_BRAKING_MIN_SPEED : 15;
max_speed = std::min(max_speed, std::max(floor_speed, end_speed));
```

- 只对**停车类目标**（`end_speed == 0`）解除 15 地板，改为压到 `REALISTIC_BRAKING_MIN_SPEED`(5)；
  非停车目标（`end_speed > 0`）逐字保留原生 `max(15, end_speed)`。
- 这是「线性减速」的最后一段：到达前的减速完全由 `else if (end_speed < max_speed)` 的
  刹车距离计算负责，这一段原生就是线性的；15 只卡在「已越过目标」那一步。
- 仍然**不加**任何格内速度钳制：本处只改限速上限，不写位置/速度。

### 9.3 到达判据只留位置，撤掉兜底（口径 3）

`src/station_cmd.cpp` 的 `VehicleEnterTile_Station()`：

```cpp
const bool r3r_arrived = r3r_head_stop ? (x >= stop) : (x == stop);
```

- 去掉第 287 轮的 `&& consist->cur_speed == 0`：那是自指死锁 —— 减速地板是 5、越过目标点后
  还被抬到 15，速度永远到不了 0，于是永远「未到达」，现场 WP-NOARRIVE ×15 后带 15 冲出格。
- 删掉 `else if (r3r_head_stop && x >= stop) { WP-NOARRIVE ... }` 探针分支（`x >= stop` 时必先命中
  `r3r_arrived`，逻辑上不可达）。
- 删掉 `R3RAdvanceStoppedWaitWaypointOrder()` 定义与 `TrainLocoHandler` 停驶分支里的调用，
  以及 `ChooseTrainTrack` 里指向它的注释。玩家原话：「兜底基本没必要，反正不符合条件的列车
  甚至不会把路点当成安全停车位。」
- 到达后的归零不靠新代码：`VehicleEnterTile_Station()` 返回 `EnteredStation` →
  `TrainEnterStation()` 的路点分支（`train_cmd.cpp:17139-17143`）`MakeWaiting()` + `cur_speed = 0`
  + `UpdateTrainSpeedAdaptationLimit(0)`，与原生站台停靠同一条路径。

### 9.4 Q-288-A 的结论（疑点解除）

`ApplyAdvisory` 只是「下一帧强制重算一次 advisory」的刷新标记（设置变更/读档时置位，
`UpdateSpeed()` 里 `cur_speed <= strict_max_speed` 时清零），不是 advisory 生效的必要条件：
`advisory_max_speed` 由 `DoUpdateSpeed(..., strict_max_speed, advisory_max_speed, realistic)`
单独作为参数消费（`train_cmd.cpp:17081+`）。因此路点的 0 速目标不依赖 `ApplyAdvisory`。

### 9.5 构建自证（第 288 轮）

复用既有 `_tmp_inc_build.cmd`（未新建任何 `.cmd`）；`tasklist` 确认 `openttd.exe` 未运行；
`build\R3R_incbuild.guard.log` = `GUARD: incremental is safe (no header/lang file is newer than the newest object)`；
日志尾部 `[4/4] Linking CXX executable openttd.exe`；`build\R3R_incbuild.done` = `EXIT_CODE=0`；
时间戳链 `src\station_cmd.cpp` 20:39:12 / `src\train_cmd.cpp` 20:40:26 → 两个 obj 均 20:41:37 →
`build\openttd.exe` **2026-10-23 20:42:59（52 275 712 B）**；`read_lints` 两文件 0 条。
本轮无新增日志字面量（`WP-NOARRIVE`、`WP-STOP-ADVANCE` 反而被删除），故不做 exe 串核对。

### 9.6 待复测判据

1. 等待区路点：车头从远处一路减速（不再卡 15），进格后收尾到 5，走到格末内侧半个车身处
   判到达、`TrainEnterStation()` 归零并 `MakeWaiting`，订单推进到 GOTO_COUPLE。
2. 不再出现 `WP-NOARRIVE`（探针已删）；日志里应能看到停稳后订单推进（原来的 WP-STOP-ADVANCE 也不再有）。
3. 负判据：普通站台停靠、普通通过式路点（下一条不是 GOTO_COUPLE）的停车标、减速、到达行为逐字不变。
4. 负判据：非停车目标（限速牌/让行/信号限速）的「已越过目标」仍按 15 地板。
