# 第 333 轮：`follow_track.hpp:140` 断言崩溃（第 332 轮修复引入，已定位并修）

- 日期：2026-10-27
- 来源：玩家报告「follow_track.hpp 140行断言崩溃了」，使用的 exe = 第 332 轮（`build\openttd.exe` 18:27:31，含 `CPL-FIRST-TD … hops= yapf= ok= best= start= trk=`）。
- 关联：KI-446 续 / KI-504（挂接寻路），本轮条目在 `R3R_KNOWN_ISSUES.md` 文末「第 333 轮」小节；上一轮分析见 `R3R_couple_never_moves_round328_memo.md` §27.29–§27.31（本轮推翻其"首步格 = `pPrev->GetLastTile()`"这一前提）。
- 状态：已实现 + 已编译（复测待做）

## 一、断言位置（事实）

`src\pathfinder\follow_track.hpp` 的 `TrackFollower::Follow(TileIndex old_tile, Trackdir old_td, …)` 入口处：

```cpp
dbg_assert_tile(TrackdirBitsToTrackBits(GetTileTrackdirBits(old_tile, TT(), sub_mode)) & TrackdirToTrackdirBits(old_td),
        old_tile);
```

即：**要求 `old_td` 是 `old_tile` 这一格上真实存在的 trackdir**。跨格的一对必断言（Debug 构建 `DBG_ASSERTS` 全开）。

## 二、根因：YAPF 节点访问器配对错（第 332 轮引入）

`src\pathfinder\yapf\yapf_node_rail.hpp` 里两套访问器属于**两个不同的格**：

| 访问器 | 定义 | 语义 |
|---|---|---|
| `GetTile()` / `GetTrackdir()` | `:85` / `:88`（转发 `key.GetTile()/GetTrackdir()`，`:44` / `:49`） | 节点**自己**那一格与其出向 trackdir，**同格** |
| `GetLastTile()` / `GetLastTrackdir()` | `:157` / `:163`（读 `segment->last_tile / last_td`） | 本节点 **segment 被截断**那一格与其 trackdir，**同格** |

第 332 轮的代码写成：

```cpp
const TileIndex first_tile = pPrev->GetLastTile();   // segment 末端格
const Trackdir yapf_first_td = pPrev->GetTrackdir(); // 节点自己格的 trackdir
…… reach(first_tile, yapf_first_td, 0);              // ← 跨格，引擎一旦下探就断言
```

当 `pPrev` 的 segment 只跨一格时两侧恰好同格（不炸），跨多格时必然把 A 格的 trackdir 拿去 `Follow` B 格 ⇒ `follow_track.hpp:140` 崩溃。日志里 `CPL-BACK pPrev=15,15 td=2 parent=16,14 ptd=0` 打印的正是 `GetLastTile()/GetLastTrackdir()` 那一对，被误当成"首步格的 trackdir"。

对照：本函数**原有**的 `next_trackdir = pPrev->GetTrackdir();`（`yapf_rail.cpp:1042`，本轮未改）用的就是 key 侧，与 `GetTile()` 才是同格 —— 这也是它从来不会触发该断言的原因。

## 三、本轮改动（仅 `src\pathfinder\yapf\yapf_rail.cpp`，未碰任何 `src\*.h`）

1. **配对修正**：`first_tile = pPrev->GetTile()`（不再 `GetLastTile()`），`yapf_first_td = pPrev->GetTrackdir()`。
2. **采纳前自证同格**：新增 `first_tdb = GetTileTrackdirBits(first_tile, TRANSPORT_RAIL, 0)`；`first_td_on_tile = (yapf_first_td != INVALID_TRACKDIR) && HasBit(first_tdb, yapf_first_td)`；`yapf_first_ok = (r3r_hops > 1) && first_td_on_tile && (first_tile != st) && reach(...)`。
3. **两道兜底护栏**（不再依赖调用方自觉）：`reach()` 里 `f2.Follow(cur, cur_td)` 之前、`rp()` 里 `f3.Follow(cur, cur_td)` 之前，各加一次 `cur_td == INVALID_TRACKDIR || !HasBit(GetTileTrackdirBits(cur,…), cur_td) → return false`。⇒ 即使将来有别的调用方喂进跨格对子，也只退化为"这条路走不通"，不会断言崩溃。
4. **探针扩容**：`CPL-FIRST-TD` 追加 `at=%d,%d`（首步 td 所属格）与 `tdb=0x%x`（该格轨道位），可直接读出"配对是否成立 / 首步格是哪一格 / 该格有没有这根轨"。

## 四、构建自证（第 333 轮）

- 复用工作区既有 `_tmp_inc_build.cmd`（未新建任何 `.cmd`），`Start-Process cmd /c _tmp_inc_build.cmd -WorkingDirectory 'd:\sourcecode of JGRPP' -WindowStyle Minimized`。
- 结果见下方「回填」段（`build\R3R_incbuild.guard.log` / `.done` / `build\openttd.exe` 三个时间戳必须都是本轮）。

## 五、复测判据（4 条）

1. **崩溃消失**：同场景不再出现 `follow_track.hpp:140` 断言（也不再出现任何 `dbg_assert_tile` 于 `Follow` 入口）。
2. **判据可从探针读出**：`CPL-FIRST-TD … at=x,y tdb=0x… ok=?` 中 `at=` 必须是 `yapf=` 那根轨真实所在的格，且 `tdb` 包含 `yapf` 对应的位（`ok=1` 时成立；`ok=0` 时也应满足 `first_td_on_tile=1`，失败只会来自 `hops<=1` / `first_tile==st` / `reach()` 下探）。
3. **行为**：若 `ok=1`，`start=` 应等于 `at=`，`CPL-RET` 的 `ret=` 应随之变化（不再恒为 `0x1`/`TRACK_X`）；若 `ok=0` 且 `first_tile==st`，说明第 332 轮的"格位错位"前提不成立，本路径与改动前逐字一致（见下节 U-1）。
4. **负判据**：单跳（`hops=1`）与普通直行线路的 `CPL-FIRST-TD`/`COUPLE-OK` 与第 331 轮逐字相同，无新增 `REVERSEDIR … force_swap=1`。

## 六、仍未确认

- **U-1（第 332 轮的核心假设，本轮仍未被证实）**：`pPrev->GetTile()` 是否真的 ≠ `st`。现场 `st=16,14`（`CPL-TRACE`）与 `parent=16,14`（`CPL-BACK` 的 `GetLastTile()`）同格，若 `GetTile()` 也落在 `16,14`，则 `first_tile != st` 恒假、`yapf` 分支永不采纳，第 332 轮的"格位错位"结论需要重新论证（下一步读 `CPL-FIRST-TD … at=` 实测即可判定）。
- **U-2**：`15,15` 上是否真有 `TRACK_UPPER`（`td=2`）。本轮 `tdb=` 字段可直接回答。
- **U-3**：断言崩溃的确切调用点（`reach` 首次调用 / `rp` 首次调用 / `reach` 递归）本机无 crash dump 侧证据，是从"本轮唯一新增的跨格对子"反推；护栏已覆盖三处，故不影响修复。

## 七、回填（构建自证，2026-10-27 18:42）

| 项 | 值 |
|---|---|
| 入口 | `cmd /c _tmp_inc_build.cmd`（复用既有脚本，未新建任何 `.cmd`） |
| 护栏 | `GUARD: incremental is safe (no header/lang file is newer than the newest object)` |
| 编译 | `[2/3] Building CXX object …\src\pathfinder\yapf\yapf_rail.cpp.obj` → `[3/3] Linking CXX executable openttd.exe` |
| 判决 | `build\R3R_incbuild.done` = `EXIT_CODE=0`（18:42） |
| 错误计数 | `error C` / `fatal error` / `FAILED:` / `build stopped` = **0** |
| 时间链 | `src\pathfinder\yapf\yapf_rail.cpp` 18:37 → `yapf_rail.cpp.obj` 18:41 → `build\openttd.exe` 18:42（52 322 304 B，晚于源码与 obj） |
| 静态检查 | `read_lints` = 0 条 |
| 字面量 | exe 内 `CPL-FIRST-TD st=` 命中（新增 `at=` / `tdb=` 字段随该格式串进二进制） |

注：首次用 `Start-Process … -WindowStyle Minimized` 启动的那次**没有真正执行**（`build\openttd.exe` 仍是上一轮 18:27 的产物、`done`/`log` 时间戳未变），随后改用前台 `cmd /c _tmp_inc_build.cmd` 一次成功。⇒ 判据仍必须看时间戳，不能只看 `.done` 里的 `EXIT_CODE=0`（旧文件里同样是 0）。

## 八、崩溃 dump 实证（2026-10-27 补记，本机找到了 U-3 缺失的现场）

崩溃日志目录：`C:\Users\冯洁敏\Documents\OpenTTD\`（不在工作区，也不在 `%APPDATA%\OpenTTD\crash`），本轮找到同一次崩溃的两份：

| 文件 | 本地时间 | 大小 |
|---|---|---|
| `crash-20261027T100208Z.log` | 2026-10-27 18:02:08 | 2 066 B |
| `crash-20261027T103907Z.log` | 2026-10-27 18:39:07 | 1 973 B |

**关键：两份 dump 的 `ExeVersion` 都写着 `Build date: Oct 27 15:17:42`** —— 即 `r3r-pre-ledger-removal-2026-10-25-m (2)` 的 **15:17 那次构建**，既不是第 332 轮的 18:27，也不是第 333 轮的 18:42。旁证：`build\R3R_debug.log` 最后写入 18:34:30，早于 `build\openttd.exe` 的 18:42:50 ⇒ **第 333 轮的 exe 到目前为止一次都没被游戏运行过**，玩家报的崩溃发生在旧 exe 上。

### 8.1 断言原文（dump 第 11 行，逐字）

```
Assertion failed at line 140 of D:\sourcecode of JGRPP\src\pathfinder\follow_track.hpp:
[&]() { if (this->IsTram() && GetSingleTramBit(this->old_tile) != DiagDirection::Invalid) return true;
 const uint sub_mode = (IsRoadTT() && this->veh != nullptr) ? to_underlying(this->IsTram() ? RoadTramType::Tram : RoadTramType::Road) : 0;
 const TrackdirBits old_tile_valid_dirs = GetTileTrackdirBits(this->old_tile, TT(), sub_mode);
 return (old_tile_valid_dirs & TrackdirToTrackdirBits(this->old_td)) != TRACKDIR_BIT_NONE; }()
```

`dbg_assert_tile(expr, tile)` → `assert_tile(expr, tile)` → `assert_tile_error(line, file, expr, tile)`（`src\stdafx.h:381-392`），dump 里的 `tile:` 行就是该宏的第二实参，本处即 `this->old_tile`。**所以 dump 的 `tile:` 就是崩溃时的 `Follow(old_tile, old_td)` 里的 `old_tile`。**

### 8.2 两份 dump 的差异（唯一差异）

| | 崩溃 A（18:02） | 崩溃 B（18:39） |
|---|---|---|
| `tile:` | `710 (16 x 14), type: 50 (Station)` | `78F (15 x 15), type: 10 (Railway)` |
| 其余 | 完全相同 | 完全相同 |

两份的 `Within context` 逐字相同：

```
0: TrainController: veh: 59: (Train [N/A], c:0, st:E, vs:D, vf:B, vcf:ld, gvf:, tf:ME, trk: 0x01,
     [tile: 710 (16 x 14), type: 50 (Station)…], front: 57: (Train 7 (HXN5B-XXXX2), st:FE, vs:D, vf:TpBa,
     vcf:zd, gvf:, tf:qHS, trk: 0x01, [tile: 710 (16 x 14), type: 50 (Station)…]))
1: CallVehicleTicks: veh: 57: (…)
```

即：`veh 59` = `tf:ME`（ArticGroup**M**ember + SegmentBack⊗）、`veh 57` = `tf:q**HS**`（ArticGroup**H**ead + SegmentFront★），两车同在车站格 `16,14`、`trk: 0x01`（`TRACK_X`）；游戏时间 `1920-06-03 (23,0)`，load 后 1020 state ticks。

### 8.3 两个候选根因（都能解释 A，只有两种能解释 B）

1. **跨格对子（第 332 轮那条）**：现场 `CPL-BACK pPrev=15,15 td=2 parent=16,14 ptd=0` 里 `GetLastTile()=15,15`，把 `GetTrackdir()`（属另一格）拿去 `Follow(15,15, …)` ⇒ **正好解释崩溃 B**（`tile: 15,15`）。
2. **`old_td == INVALID_TRACKDIR`**：`INVALID_TRACKDIR` 在**任何**格都不合法，于是"哪一格先被喂进去就在哪一格崩" ⇒ **可同时解释 A（16,14）与 B（15,15）**，也解释了"同一 exe、同一存档、同一 tick 却崩在不同格"这个反常现象。
   - 机制可行性已核：`Train::GetVehicleTrackdir()` 走 `TrackDirectionToTrackdir(FindFirstTrack(track), direction)`，而该函数是查表 `_track_direction_to_trackdir[track][dir]`（`src\track_func.h:456-462`，**不 assert**，非法组合直接返回 `INVALID_TRACKDIR`）。现场两车都是 `trk: 0x01`（`TRACK_X`，只容 NE/SW 两向），若 `direction` 是南北向就返回 `INVALID_TRACKDIR`（`direction` 与 `track` 不对齐 → R3R 段编辑/翻转后的典型残留）。

⇒ 结论：**崩溃 B 与第 332 轮的 `GetLastTile()+GetTrackdir()` 错配完全吻合，第 333 轮的修法覆盖它；但崩溃 A 提示可能还存在"INVALID trackdir"这一支，且它未必落在 `rp()`/`reach()` 内**（`ChooseTrainTrack` 自己的 `ft.Follow(...)`：`train_cmd.cpp:16972`、`:17285`，以及 `yapf_rail.cpp` 的 `rev_node` 两处 `follower.Follow(GetLastTile(), ReverseTrackdir(GetLastTrackdir()))` 都不在第 333 轮加的两道护栏里）。

### 8.4 疑点登记（新增，待验证）

- **U-4**：同一 exe、同一存档、同一 tick、同一车号，两次 dump 的 `old_tile` 却是 `16,14` 与 `15,15` 两个不同值 ⇒ 崩溃点不是唯一固定位置（或该 exe 同时存在两条不同错配调用）。待验证：用第 333 轮 exe 复跑，读 `CPL-FIRST-TD … at=… tdb=…`。
- **U-5**：`Build date 15:17:42` 到底对应哪一轮构建未核对；既有文档写"崩溃 exe = 第 332 轮产物（18:27:31）"与 dump **不符**，本节已更正（崩溃的是 15:17 那版）。不影响结论（333 的护栏对两条错配都适用）。
- **U-6**：`GetVehicleTrackdir()` 是否真的返回 `INVALID_TRACKDIR`（若成立，则 `ChooseTrainTrack` 入口那一对 `Follow(consist->tile, td)` 会直接断言，与 `rp()/reach()` 无关）。待验证：`CPL-ORIGIN-TD`/`CRT`/`RESV-EXT-DEST` 行里的 `td=` 是否为 8（`INVALID_TRACKDIR`），或对 `veh 57/59` 执行 `dump_vehicle` 看 `direction` 与 `trk` 是否自相矛盾。

### 8.5 下一步（未做，需玩家/日志）

1. 用 18:42 的 exe 复跑同一存档：若不再崩 ⇒ 崩溃 B 是主因，第 333 轮即收口。
2. 若仍崩，先看 dump 的 `tile:`：等于车自己的格（`16,14`）⇒ U-6 成立，需在 `ChooseTrainTrack` 的 `ft.Follow` 前也加同格守卫（或先修 `direction`/`track` 不对齐的源头）；等于相邻格 ⇒ 还剩别的跨格调用点。
3. 加固顺序（待拍板，尚未落地）：`yapf_rail.cpp` 的两处 `rev_node` `Follow` 加同格守卫 → `train_cmd.cpp:16972/17285` 加同格守卫。二者都要"先自证再 Follow"，失败只退化为"此路不通"，不改任何合法路径行为。
