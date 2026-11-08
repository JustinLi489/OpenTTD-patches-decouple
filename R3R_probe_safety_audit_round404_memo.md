# 第 404 轮：探针安全审计（站台格 / 隧道桥格 / 非铁路格 / 无效 trackdir）

日期：2026-11-08
触发：玩家要求「在添加探针之后、启动编译之前，检查探针是否考虑周全」，理由是历史上多次因探针未处理站台格、隧道/桥梁格而崩溃（KI-570 才刚在 `rail_map.h:40` 炸过）。
范围：第 404 轮新加/改动的三处探针（全在 `src\train_cmd.cpp`，本轮**未改任何行为代码**）。
结论：**发现 1 处会导致断言的缺陷（REG-404-A），已修；其余 12 项检查通过。**

---

## 一、审计对象（第 404 轮改动清单）

| # | 探针 | 位置 | 用途 |
|---|------|------|------|
| 1 | `RESV-END` 扩字段 | `src\train_cmd.cpp:19377-19418`（`TryPathReserveWithResultFlags` 预留成功后） | REG-403-A：打 `movdir`/`ahead`/`fwd`，看推进方向与 `endTile` 方位 |
| 2 | `LOCO-MOVE` 扩字段 | `src\train_cmd.cpp:23507-23536`（`TrainLocoHandler` 移动分支入口） | REG-403-A：同一组字段，停车后完全不出现即为「从未进移动分支」的正面证据 |
| 3 | `CPL-NEXT`（新） | `src\train_cmd.cpp:23552-23584`（`GOTO_COUPLE` 移动分支，每 32 tick 一行） | REG-403-B：查「车头下一格」是不是站台格、整格预留归谁 |

---

## 二、逐项安全检查

### 2.1 `GetTileTrackdirBits(tile, TRANSPORT_RAIL, 0)`（#1、#2 使用）

- 调用前均有 `IsValidTile(...)` 判断（#2 写成 `IsValidTile(r3r_la_tile) ? (uint)GetTileTrackdirBits(...) : 0u`）。
- **非铁路格（水面/田野/树林/房屋）**：内部走 `GetTileTrackStatus()`，该函数对 `TileType` 全枚举有分支、其余走 `default: return 0`，是引擎里被 A* 探索直接对任意格调用的公共入口 ⇒ 安全。
- **站台格**：`MP_STATION` + `HasStationRail` 分支。
- **隧道/桥梁格**：`MP_TUNNELBRIDGE` 分支。
- 判定：**通过**（不需要额外门禁）。

### 2.2 `TileIndexDiffCByDir(consist->GetMovingDirection())`（#1、#2 使用）

- `GetMovingDirection()` 返回 `Direction`（0..3）；`TileIndexDiffCByDir` 只查表，无断言。
- 判定：**通过**。注意语义：`db=1` 时它返回**运动方向**（与 `direction` 相反），这正是 REG-403-A 想看的量，不是缺陷。

### 2.3 `IsPlainRailTile` / `IsRailStationTile` / `GetTrackBits` / `GetRailReservationTrackBits` / `HasStationReservation`

- `IsPlainRailTile`、`IsRailStationTile` 本身是 `IsTileType(...)` 的复合谓词，对任意格安全。
- 后三者的合法前置是「铁路格 / 站台格」，越界会命中 `rail_map.h:40` 与 `rail_map.h:168` 两条断言（KI-570 的现场）。
- 代码里三处均已有短路守卫：`r3r_nb_stn && HasStationReservation(...)`、`r3r_nb_plain ? GetTrackBits(...) : 0u`、`r3r_nb_plain ? GetRailReservationTrackBits(...) : 0u`。
- 判定：**通过**（守卫保留，不得改成无条件求值 —— KI-570 的 `IsPlainRail(ftile)` 就是这么炸的）。

### 2.4 `TrackdirToExitdir(moving_front->GetVehicleTrackdir())` —— **REG-404-A，已修**

- `track_func.h:396`：`TrackdirToExitdir()` 里有 `dbg_assert(IsValidTrackdirForRoadVehicle(trackdir))` ⇒ 传 `INVALID_TRACKDIR` **必断言崩溃**。
- `Train::GetVehicleTrackdir()` 在 `vehstatus.Test(VehState::Crashed)` 时返回 `INVALID_TRACKDIR`（其余分支都是有效值）。
- 本文件已有两处同族防护先例，说明这是已知风险：
  - `src\train_cmd.cpp:2818-2819`：`/* R3R (KI-108 family): TrackdirToExitdir() asserts on INVALID_TRACKDIR. */ if (unlikely(end_td == INVALID_TRACKDIR)) return false;`
  - `src\train_cmd.cpp:5636-5639`：注释写明 "skip the trackdir dependent checks if we do not have one (crashed vehicle)"。
- **原探针写法（会炸）**：`const DiagDirection r3r_nb_exit = TrackdirToExitdir(moving_front->GetVehicleTrackdir());`
- **已修写法**：
  ```cpp
  const Trackdir r3r_nb_td = moving_front->GetVehicleTrackdir();
  const bool r3r_nb_td_ok = (r3r_nb_td != INVALID_TRACKDIR);
  const DiagDirection r3r_nb_exit = r3r_nb_td_ok ? TrackdirToExitdir(r3r_nb_td) : DiagDirection::Invalid;
  const TileIndex r3r_nb_to = r3r_nb_td_ok ? TileAdd(moving_front->tile, TileOffsByDiagDir(r3r_nb_exit)) : INVALID_TILE;
  const bool r3r_nb_ok = r3r_nb_td_ok && IsValidTile(r3r_nb_to);
  ```
  日志同步追加 `tdOk=%d`，使「拿不到 trackdir」与「下一格无效」在日志里可区分。

### 2.5 `GetTrainForWholeTileReservation(tile)` 对任意格类型

- 实现（`src\pbs.cpp`，`GetTrainForWholeTileReservation`）：
  1. `if (!IsValidTile(tile)) return nullptr;`
  2. `GetFirstVehicleOnTile(tile, VehicleType::Train)` —— tile→vehicle 哈希查表，任意格安全；
  3. `GetReservedTrackbits(tile)` —— `switch (GetTileType(t))` + `default: return TRACK_BIT_NONE`，任意格安全；
  4. 非 NONE 才 `GetTileRailType()`（无铁路的格返回 `INVALID_RAILTYPE`）→ 才 `R3RFollowReservationToOwner()`。
- 判定：**不会崩**；但本轮仍把调用收窄到「预留唯一可能的四种载体」：
  ```cpp
  const bool r3r_nb_rail = r3r_nb_ok &&
          (r3r_nb_type == TileType::Railway || r3r_nb_type == TileType::Road ||
           r3r_nb_type == TileType::Station || r3r_nb_type == TileType::TunnelBridge);
  const Train *const r3r_nb_own = r3r_nb_rail ? GetTrainForWholeTileReservation(r3r_nb_to) : nullptr;
  ```
  理由：省一次哈希查询、语义直白（水面/田野上不存在整格预留），并让 `wholeOwner=` 只在这四种格上有意义。

### 2.6 `TileAdd(tile, TileOffsByDiagDir(exitdir))` 的地图边界

- `TileOffsByDiagDir` 查表；`TileAdd` 是 `TileIndex(t.base() + offs.base())`，**不做环绕、不访问地图**。
- 边界/负向溢出时得到的索引由紧随的 `IsValidTile()` 判假，后续 `GetTileType`/`IsRailStationTile` 等全在 `r3r_nb_ok ?` 之内 ⇒ 安全。
- 判定：**通过**。

### 2.7 隧道/桥梁格的「下一格」语义（不崩，但会失真）

- 车在隧道/桥梁内时，`moving_front->tile` 只是**洞口/桥头**格，外推一格得到的是洞外，**不是**车辆真正进入的下一格。
- 这不造成崩溃，但会让 `CPL-NEXT` 的 `to=` 读起来像结论。已补 `worm=%d`（`moving_front->track & TRACK_BIT_WORMHOLE`），读日志的人可自行剔除这类行。
- 判定：**通过（语义已标注）**。

### 2.8 `moving_front` 是否可能为空

- `consist->GetMovingFront()` = `IsDrivingBackwards() ? Last() : First()`，只要 `consist` 链有效就非空；本处 `consist` 由 `Train::From(v)->First()` 取得、已在 `TrainLocoHandler` 主循环内。
- 判定：**通过**。

### 2.9 `(uint16_t)trackdir << 24` 与 `tile.base()` 是否位重叠（#1 的 payload 打包）

- `tile.base()` 上界远小于 `2^22`（地图最大 4096×4096 量级），`<<24` 起算不重叠；`trackdir=0xFF` 时打包成 `0xFF000000`，仍不覆盖低 24 位。
- 判定：**通过**。

### 2.10 探针所属分支的进入条件

- `CPL-NEXT` 在 `GOTO_COUPLE` 的**移动分支**内（`j >= adv_spd`），停车时本来就不执行 —— 这与 REG-403-B 要判「停车后从不进移动分支」正好互相印证，不是缺陷。
- 判定：**通过**。

### 2.11 频率 / 日志体积

- `CPL-NEXT` 每 `(tick + index) % 32 == 0` 一行；`LOCO-MOVE`、`RESV-END` 均带既有节流。相对 KI-217 的 19M 行刷屏事件（无上限循环）不构成风险。
- 判定：**通过**。

### 2.12 新字段的类型

- `DiagDirection::Invalid` 存在（本文件 `:5633` 已在用）；`TileType::Void` 存在（`tile_type.h`）；`Trackdir`/`INVALID_TRACKDIR` 本文件多处使用。
- 判定：**通过**（`read_lints` 0 条）。

---

## 三、修复摘要（REG-404-A）

- 文件：`src\train_cmd.cpp`（仅此一处，未碰 `.h`）
- 位置：`CPL-NEXT` 探针（`TrainLocoHandler` 的 `OT_GOTO_COUPLE` 移动分支）
- 改动：`TrackdirToExitdir()` 前置 `INVALID_TRACKDIR` 判定；无效时 `to=INVALID_TILE`、跳过全部铁路查询；日志追加 `tdOk=`、`worm=`、`rail=`。
- 行为影响：**零**（纯探针）。
- 口径纪律（写进 KI）：**凡在 R3R 探针里调用 `TrackdirToExitdir()` / `ReverseTrackdir()` 之外的 trackdir 派生函数，必须先确认 `GetVehicleTrackdir() != INVALID_TRACKDIR`**，与 `:2819`、`:5639` 两处既有先例一致。

---

## 四、未确认项

- **U-404-1**：`Train::GetVehicleTrackdir()` 的 `Crashed` 分支在实机上是否真的会在 `TrainLocoHandler` 里出现（crashed 车辆通常已不进主循环）。本轮按「宁可多判一次」的保守口径加守卫，不依赖该分支是否可达。
- **U-404-2**：`CPL-NEXT` 的 32 tick 节流是否够密（`RESV-END`/`LOCO-MOVE` 在关键 tick 都有行，但 `CPL-NEXT` 可能错过那一 tick）。若复测仍判不出 REG-403-A/B，下一步改为「状态变化即打」（沿用 `R3RDbgEdge` 的边沿触发）。
- **U-404-3**：`worm=1` 的行其 `to=` 语义无效，但本轮未改「改用 `TrackFollower` 求真实下一格」的做法 —— 需要时再补。

---

## 五、下一步

1. 增量编译：`R3R_fullbuild.cmd inc detach`（仅改 `.cpp`，护栏应判 incremental is safe）。
2. 构建自证：`build\R3R_incbuild.done` = `EXIT_CODE=0`、日志尾部 `[n/n] Linking CXX executable openttd.exe`、`train_cmd.cpp.obj` 与 `build\openttd.exe` 的 mtime 晚于 `src\train_cmd.cpp`。
3. 复测判据见 KI 清单「第 404 轮」小节（含「探针不再导致断言」这条负判据）。
