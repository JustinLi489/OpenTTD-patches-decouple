# 第 275 轮：隧道 / 桥 / 道口的预留位释放（KI-422 落地）+ 「断开的预留」新报告登记

日期：2026-10-22　状态：修法中（本轮先落盘，再改码，再构建）

## 一、玩家指令与本轮范围

玩家原话要点：
1. 「那么现在你认为是桥隧需要修复吗，那就先修吧。」⇒ 本轮执行 KI-422 的修法：把隧道 / 桥 / 道入 `R3RReleaseReservationLedgerChecked()`。
2. 「我在路上发现了好多断开的预留啊，这就是预留的一大坨屎山」⇒ 新报告，登记为 **KI-426（待定性）**，本轮不改（先修桥隧），分析见 §五。

## 二、现状取证（本轮实测行号）

`src/pbs.cpp` 的 `R3RReleaseReservationLedgerChecked()`（:488-555）只有 **两个** 分支：

- `:511-538` `TileType::Station && HasStationRail`：整格位，读 `GetStationReservationTrackBits()`，释放 `UnreserveRailTrack(tile, FindFirstTrack(rail_bits))`，三道判据（无位 ⇒ `-gone` 删行；有车或 walk 到主人 ⇒ `-hold` 保留；否则释放）。
- `:540-554` `TileType::Railway && IsPlainRailTile`：按 `(tile, track)` 精确释放。
- 其余一切（**隧道、桥、道口、depot**）落到 `:549-554` 的兜底：只 `R3RResvLedgerDropBit()` 删账本行 + 打 `RESV-GC ... why=<原因>-gone`，**地图上的位一个字节都不动**。

`src/pbs.cpp` 的 `UnreserveRailTrack()`（:1190-1261）本来就**完整支持**这几类：

- `:1224-1230` `TileType::Road && IsLevelCrossing` → `SetCrossingReservation(tile, false)` + `UpdateLevelCrossing` + dirty
- `:1232-1237` `TileType::Station && HasStationRail` → `SetRailStationReservation(tile, false)`
- `:1239-1256` `TileType::TunnelBridge && TRANSPORT_RAIL` → 隧道 `SetTunnelReservation(false)`；桥 `UnreserveRailBridgeHeadTrack(tile, t)`；附带出口信号态与 dirty

⇒ 缺的不是引擎能力，而是 R3R 的释放函数没把这几类格送进去（与 §9.8 的归因一致：存量缺口，非台账回归）。

配套读取函数（本轮确认存在）：
- `src/tunnelbridge_map.h:185` `GetTunnelBridgeReservationTrackBits(tile)`：隧道返回 `DiagDirToDiagTrackBits(GetTunnelBridgeDirection(t))`，桥返回 `GetBridgeReservationTrackBits(t)`。
- `src/road_map.h:477` `HasCrossingReservation(tile)`。
- `src/bridge_map.h:455` `UnreserveRailBridgeHeadTrack(tile, t)` 的 `assert(HasBit(GetCustomBridgeHeadTrackBits(tile), t))` ⇒ 桥必须传该桥头真实存在的那条轨，不能传 `INVALID_TRACK`。
- `UnreserveRailTrack()` 入口 `assert_msg_tile(... GetTileTrackdirBits(tile, TRANSPORT_RAIL, 0) & TrackToTrackBits(t) ...)` ⇒ 传进去的 `t` 必须是该格 `GetTileTrackStatus` 认的轨，否则 Debug 版（DBG_ASSERTS + R3R_PROBES）当场断言崩溃——这是 KI-370（`rail_map.h:168`）的同类雷，必须避开。

## 三、修法（只改 src/pbs.cpp，不动任何 src/*.h）

在 Station 分支之后、plain-rail 分支之前插两个分支，结构与 Station 分支同构：

1. `TileType::TunnelBridge && GetTunnelBridgeTransportType(tile) == TRANSPORT_RAIL`
   - `bits = GetTunnelBridgeReservationTrackBits(tile)`；`NONE` ⇒ 删行 + `-gone(tb)` + return true
   - `GetFirstVehicleOnTile(tile, VehicleType::Train) != nullptr` ⇒ `-hold(tb)` + return false（隧道里的车其 `tile` 就是入口格，桥上的车 tile 就是桥格，所以这条判据对两类都成立）
   - 释放：`t = (track != INVALID_TRACK && HasBit(bits, track)) ? track : FindFirstTrack(bits)`，`R3RResvPhaseGuard("gc-tb")` 包住 `UnreserveRailTrack(tile, t)`
2. `TileType::Road && IsLevelCrossing(tile)`
   - `!HasCrossingReservation(tile)` ⇒ 删行 + `-gone(cr)` + return true
   - 格上有车 ⇒ `-hold(cr)` + return false
   - 释放：`t` 取道口那条铁路轨（`GetCrossingRailTrack(tile)`），`R3RResvPhaseGuard("gc-cr")` 包住 `UnreserveRailTrack(tile, t)`

刻意**不做**：
- 不动 `TryReserveRailTrackdir()` 的 `IsPlainRailTile` 守卫（KI-370 防断言）。
- 不动 `depot`：库位预留的语义是"库被占用"，R3R 有专门的库内逻辑，贸然释放会破坏库位判定，单独立项评估（登记为本轮疑点 REG-275-A）。

## 四、构建自证

（待回填：EXIT_CODE / Linking 行 / obj 与 exe 时间戳）

## 五、新登记：KI-426「路上大量断开的预留」

玩家原话：「我在路上发现了好多断开的预留啊，这就是预留的一大坨屎山」。

- **状态**：待定性（本轮未改，未取证日志）。
- **形态理解**：视觉上是**一段一段互不相连**的预留高亮，而不是一条连续到目的地的路径。这与单条孤儿位（KI-345/346 的走廊、KI-422 的死位）不同：更像"多条互不相接的残段"。
- **猜测根因（待验证，三个候选，互相可区分）**：
  - C-1 **链编辑清了中段、留了两头**：`R3RReleaseChainReservations()` 只释放"属于本链 own_path 且在 doomed 名单里"的格，路径被切断后两侧仍各自保留（`train_cmd.cpp:11691-11763`）。
  - C-2 **多列车各自的合法 PBS 预留被误读成断段**：不同链的预留首尾相接但不连续（中间隔着别人占用的格），审计 `RESV-AUDIT-SUMMARY` 的 `owned` 计数不会报错，视觉上却是碎段。
  - C-3 **KI-346 的错轨位（换槽格订了不走的那条轨）** 与正确位交替出现 ⇒ 视觉上"断一格"。判据看 `RESV-AUDIT-STRAY ... track=` 是否与 `RESV-WATCH SET-RAIL` 的 track 交替。
- **需要的取数**：①玩家现场日志（`build\R3R_debug.log`）里 `RESV-AUDIT-SUMMARY` 的 `reservedTiles/owned/strayTiles` 三个读数；②`RESV-PURGE` / `RESV-REAP` 行；③最好有一张截图（或玩家描述断段的位置：同一车站附近 / 跨隧道 / 道口处）。
- **复测判据（修完后）**：`RESV-AUDIT-STRAY` 的 strayTiles 归 0，且同一走廊不再出现"有主但互不相接"的多段。

## 六、疑点 REG-275-A（待评估，不影响本轮）

`depot` 格（`TileType::Railway && IsRailDepot`）同样落在 `-gone` 兜底里，`UnreserveRailTrack()` 也有 `SetDepotReservation(tile, false)` 分支（`pbs.cpp:1213-1216`），但库位"有车在内"的语义更强（车在库里时 tile 就是库格），且 R3R 库内编辑有自己的路径处理。是否需要把 depot 也接进同一个分支，需要一次"库里编组后库位预留是否残留"的现场读数再决定。
