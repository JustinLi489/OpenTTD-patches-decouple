# R3R 第 240 轮（2026-10-11）：KI-345「孤儿走廊」根因做实 + 修复 —— `rp()` 把"直行假想轨"当成"实际进入轨"预留

**性质**：**根因做实 + 修复实现**（改 `src\pathfinder\yapf\yapf_rail.cpp` 一处函数、两个分支；未碰任何 `src\*.h`）。
**承接**：第 237/238/239 轮（KI-345）只做到"能查能清"（审计 + 3 轮年龄后 reap），把 20 格走廊定性为"随存档带入的孤儿预留"，并明确未定来源。本轮把**产生源**做实。

---

## 一、输入与现场

- 现场日志：`D:\temp\codebuddy-dropped-files\87f690dd-cf51-4568-8f5c-82670a6519a0\R3R_debug.log`（含 `CPL-SCAN` ⇒ 产生者 = 第 236 轮 exe ⇒ 采样有效；与第 237~239 轮同一份场景日志）。
- 第 237/238/239 轮已确认的孤儿走廊（20 格，逐轮逐字相同）：
  - `RESV-AUDIT-STRAY … tile=1093,87 track=4 bits=0x10 age=0` … `tile=1112,106 track=4 bits=0x10 age=0`
  - `age` 0→1→2 递增，第 3 次审计当次 `RESV-REAP … track=4 age=3`（L14706–L14725 恰 20 条）。
  - 即：**全部 20 格都是 `track=4`（`TRACK_LEFT`）`bits=0x10`，且格号是 (1093,87) 起每步 x+1,y+1 的一条对角线**。

## 二、证据链（同一格上"实际占轨"与"被预留轨"不一致）

同一段日志里，同一条走廊上有三套独立读数（原文照抄）：

```
L8231 REACH cur=1092,87 td=4 depth=3 trk=4 bits=0x10
L8232 REACH cur=1093,87 td=5 depth=4 trk=5 bits=0x30     <-- 跟随器：本格实际走 RIGHT(td=5)，且本格同时带 LEFT(0x30)
L8233 REACH cur=1093,88 td=4 depth=5 trk=4 bits=0x10
L8234 REACH cur=1094,88 td=5 depth=6 trk=5 bits=0x30
L8248 RP cur=1092,87 td=4 depth=3 trk=4 bits=0x10
L8249 RP cur=1093,87 td=5 depth=4 trk=5 bits=0x30        <-- rp() 同深度同值
L8250 RP cur=1093,88 td=4 depth=5 trk=4 bits=0x10
L8251 RP cur=1094,88 td=5 depth=6 trk=5 bits=0x30
L8272 DEPOTBLK veh=23 tile=1093,87 track=20 eng=1 … spd=112   <-- 车物理上在 RIGHT(0x20)
L8277 DEPOTBLK veh=23 tile=1094,88 track=20 eng=1 … spd=112
```

把上面三套读数与审计行对照：

| 格 | 车实际占轨（DEPOTBLK） | 跟随器判定（REACH/RP td） | 本格轨道位（bits） | 审计报的孤儿 | 一致？ |
|---|---|---|---|---|---|
| 1092,87 | `track=10` LEFT | td=4 → LEFT | 0x10（只有 LEFT） | 无 | ✓ |
| 1093,87 | `track=20` **RIGHT** | td=5 → **RIGHT** | **0x30（LEFT\|RIGHT）** | **`track=4` LEFT** | ✗ ← 孤儿 |
| 1093,88 | `track=10` LEFT | td=4 → LEFT | 0x10（只有 LEFT） | 无 | ✓ |
| 1094,88 | `track=20` **RIGHT** | td=5 → **RIGHT** | 0x30 | **`track=4` LEFT** | ✗ ← 孤儿 |

⇒ **孤儿恰好落在"本格带 LEFT\|RIGHT 两槽、而车只走 RIGHT"的格上**，且**左半边（LEFT）那一槽被预订了却没有任何车**。这正是 KI-345 的 20 格走廊 = 该走廊里所有"双槽格"。

## 三、根因（代码级）

`src\pathfinder\yapf\yapf_rail.cpp` 的 `rp()`（单段挂车路径回溯预留）里，每个成功递归帧除了预订**当前格**的轨道，还要"补预订车**开进去**的那一格的轨道"（注释原话：`the loco needs the track it drives INTO`）。实现是：

```cpp
Track entry_track = TrackdirToTrack(ReverseTrackdir(cur_td));
TrackBits tile_tracks = TrackdirBitsToTrackBits(GetTileTrackdirBits(f3.new_tile, TRANSPORT_RAIL, 0));
if (tile_tracks & TrackToTrackBits(entry_track)) TryReserveRailTrack(f3.new_tile, entry_track);
```

两点致命：

1. **`ReverseTrackdir` 在这里完全没起作用**：`ReverseTrackdir(LEFT_S) = LEFT_N`，而 `TrackdirToTrack(LEFT_N) == TrackdirToTrack(LEFT_S) == TRACK_LEFT`。⇒ 该表达式恒等于 `TrackdirToTrack(cur_td)`，语义是**"假设下一格用的轨道和当前格是同一个 Track"**。
2. 这个假设**只在"下一格与当前格轨道相同（直行）"时成立**。一旦连接处换槽（下一格实际走的是另一个 Track），它就会去订**另一条轨**。而守卫 `tile_tracks & TrackToTrackBits(entry_track)` 用的是**该格的全部轨道位**，双槽格（`0x30`）必然包含 `LEFT` ⇒ **守不住**，于是照订不误。

代入 L8248/L8249：

- `cur=1092,87`，`cur_td = TRACKDIR_LEFT_S(4)` ⇒ `entry_track = TrackdirToTrack(ReverseTrackdir(4)) = TrackdirToTrack(TRACKDIR_LEFT_N(10)) = TRACK_LEFT(4)`。
- `f3.new_tile = 1093,87`，`bits = 0x30`，`0x30 & TrackToTrackBits(TRACK_LEFT)=0x10` ≠ 0 ⇒ **`TryReserveRailTrack(1093,87, TRACK_LEFT)`**。
- 而车在 1093,87 上走的是 `RIGHT`（L8232/L8272 双证）⇒ 该 `LEFT` 位**没有主人**：`GetTrainForReservation` 回溯不到任何车，车的 PBS 释放（按自己的路径轨道）永远不会碰到它 ⇒ 审计判孤儿、只有 GC 能清。

**对称验证（同一机制的另一半）**：`cur=1093,87`（`cur_td=5` RIGHT_S）⇒ `entry_track = TrackdirToTrack(RIGHT_N(11)) = TRACK_RIGHT`；下一格 1093,88 的 `bits=0x10` **不含** RIGHT ⇒ `0x10 & 0x20 == 0` ⇒ **不订**。这解释了为什么 1093,88 **不在**孤儿名单里 —— 20 格名单 = 走廊里的"双槽格"（即车走 RIGHT 的那些格），一个不多一个不少。

**这是"只泄漏、永不自愈"的账**：`rp()` 预订的是原始地图预留位，没有任何所有权记录；正常路径的释放要么按车自己的 trackdir 走（碰不到这条轨），要么按整格/站台（不适用于 plain rail）⇒ 位一直挂着，直到 KI-345 的 GC 在 3 次审计后 reap，或玩家拆轨重建。**玩家可见后果**：①铁轨上出现**车根本不走的那条轨**的黑色预留预览（本槽位还常被同一格上其它轨型/另一条线用，会**挡住别的车**）；②预留跨会话随存档保存（`_m[t].m2` bit 8-11），故第 125/126 轮"幽灵预留先出现再被回收"的观感问题的一个**持续来源**就在这里。

## 四、修法（仅 `src\pathfinder\yapf\yapf_rail.cpp`，两处同构）

把"进入轨"从"猜"改成**跟随器已经算好的实际轨道**：单比特分支用 `FindFirstTrackdir(tdb3)`（即即将递归传下去的 `nd`），多比特循环用 `(Trackdir)FindFirstBit(tb)`；并用既有的 `reserve_if_present()` 收口（它自带"该格确有该轨"的校验，且只会订该轨）。

- 单比特分支：`if (rp(f3.new_tile, FindFirstTrackdir(tdb3), depth + 1))` → 改为先取 `const Trackdir nd = FindFirstTrackdir(tdb3);`，进入轨改用 `reserve_if_present(f3.new_tile, TrackdirToTrack(nd))`。
- 多比特分支：循环体把原先内联的 `(Trackdir)FindFirstBit(tb)` 提为 `const Trackdir nd = (Trackdir)FindFirstBit(tb);`，进入轨同样改用 `reserve_if_present(f3.new_tile, TrackdirToTrack(nd))`。
- 当前格那一笔 `reserve_if_present(cur, TrackdirToTrack(cur_td))` **不动**（它本来就是对的：Track 与方向无关）。
- **量级不变**：`TrackdirToTrack(nd)` 与旧式在"下一格与当前格同 Track"（绝大多数轨道布局）下**逐字相同**，只有换槽连接才不同 ⇒ 不是新行为，是**去掉一个错误分支**。
- **为什么不会误伤**：`nd` 来自 `TrackFollower`，它已经按车辆自己的 `RailTypes` 过滤过 ⇒ 订的正是"车能站、且真的站"的那条轨；`reserve_if_present` 的存在性校验恒成立（`nd` 本就是该格的 trackdir）。
- **保留"进入格"这笔订的理由不变**：递归帧在 `cur == tg` 时**提前 return true 且不预订自己**，所以车头终点格（`tg`）的轨道只能靠这一笔补上。

## 五、构建自证（第 240 轮）

复用既有 `_tmp_inc_build.cmd`（未新建任何 `.cmd`）；只改了一个 `.cpp`，未碰任何 `src\*.h` / `lang\*.txt`。

- `build\R3R_incbuild.guard.log` = `GUARD: incremental is safe (no header/lang file is newer than the newest object)` ⇒ 增量合法。
- `build\R3R_incbuild.log` = `[2/3] Building CXX object ...yapf_rail.cpp.obj` → `[3/3] Linking CXX executable openttd.exe`；`Linking` 命中 1 次。
- `build\R3R_incbuild.done` = `EXIT_CODE=0`。
- 错误计数：`error C*` / `fatal error` / `FAILED:` / `build stopped` 合计 **0**。
- 时间戳链（均为 2026-10-11）：`src\pathfinder\yapf\yapf_rail.cpp` @00:27:56 → `build\CMakeFiles\openttd_lib.dir\src\pathfinder\yapf\yapf_rail.cpp.obj` @00:29:04 → `build\openttd.exe` @00:30:13（51 740 672 B，晚于全部源码）。
- `read_lints`（yapf_rail.cpp）：0 条。
- 本轮未新增日志字符串，不做 exe 字面量核对（改动是既有表达式的替换，无新字面量）。

## 六、复测判据

1. **正判据（主）**：同一场景复跑，`RESV-AUDIT-STRAY` **不再出现 `1093,87..1112,106` 这条 20 格 `track=4`**（理想为 0 条；至少该走廊归 0），`RESV-AUDIT-SUMMARY` 的 `strayTiles` 明显下降（第 237/238/239 轮恒为 20）。
2. **正判据（辅）**：耦合寻路仍能成功 —— `CPL-BEST` / `CPL-TRACE-RESULT` / `RP cur=` 序列与修复前**逐字相同**（本修复只改"订哪条轨"，不改路径选择），`COUPLE-OK` 照常出现。
3. **负判据 1**：直行/普通单轨线路上的预留不变 —— 修复前后在同一段直轨上 `RESV-AUDIT` 的 `owned` 计数应一致；不得出现"该订的没订"（表现为车在路上被自己的预留挡住、`TPR-BLOCKED` / `res` 归 0）。
4. **负判据 2**：无 `rail_map.h:168` / `pbs.cpp:144/145` 断言（`TryReserveRailTrack` 只在该格确有该轨时调用，且由 `reserve_if_present` 保证）。
5. **旁证**：拆掉 `RESV-AUDIT-STRAY` 的既有 20 格后，`RESV-REAP` 不再为这条走廊触发（没有源就没有账）；KI-345 的 GC 仍作为兜底保留。

## 七、未确认项

- **U-1**：现场那 20 格是"双槽格"（`LEFT|RIGHT = 0x30`）——同一格为什么同时存在两条不同槽位的轨道（双轨型/双线在同一格叠放）？与修复无关（修复不依赖槽位拓扑，只依赖"跟随器说了哪条"），但若后续想解释"为什么这条线上一格能有两条轨"仍需单独取数（建议方向：该格 `HasRailType`/`GetRailType` 与 `TRACK_BIT_RT_1/RT_2` 的对应，`src\track_type.h:58-59`）。
- **U-2**：第 237 轮记录的走廊是"读档首帧即存在" ⇒ 这批位是**上一次会话**（第 236 轮 exe 之前）由同一 bug 留下的；本轮修的是"以后不再产生"，**旧档里已有的孤儿位仍需靠 KI-345 的 GC / `r3r_resv_purge` 清一次**（不追溯）。
- **U-3**：`rp()` 的平台段补订循环（`is_station` 分支）里的 `plat_track = AxisToTrack(axis)` 是否也有同族"猜轨道"问题，本轮未取数、未改（该支路订的是站台自身的轴轨道，语义上比本处强，但值得后续单独核对）。
