# JGRPP 0.72.4 崩溃取证备忘（Follow/GetTileType 非法格索引）

> 输入：`C:\Users\冯洁敏\Downloads\错误报告.docx`
> 提取文本：`_tmp_crash_report.txt`；对照源码：`_tmp_train_0724.cpp`（0.72.4 的 train_cmd.cpp）、`_tmp_ft_0724.txt`（0.72.4 的 follow_track.hpp）
> 工作区 `D:\sourcecode of JGRPP` 为 0.73.1 系魔改树，仅作行号/结构对照，**不是**玩家运行的版本。

## 一、崩溃现场（已确证）

调用栈（外层→内层）：

```
[04] CheckNextTrainTile      /src/train_cmd.cpp:3508
[03] ChooseTrainTrack        /src/train_cmd.cpp:4807
[02] TryLongReserveChooseTrainTrack  /src/train_cmd.cpp:4529
[01] ChooseTrainTrack        /src/train_cmd.cpp:4004   ← 内联 ExtendTrainReservation
[00] CFollowTrackT::Follow   /src/pathfinder/follow_track.hpp:239
     (inlined FollowTileExit / follow_track.hpp:145)
```

- 崩溃类型：`C0000005` 读取越界（不是断言、不是除零）。
- 关键寄存器：`RAX = 0xFFFFFFFF`，与 `INVALID_TILE` 同值。
- 崩溃指令字节 `0F B6 11 C1 FA 04 83 FA 05 ...` = `movzx edx, byte ptr [rcx]; shr edx,4; cmp edx,5`
  → 读取 `Tile.type` 字段低 4 位（bit 4..7）后与 `5`（Station）/`1`（Railway）比较。
  → 对应 `IsTileType(t, Station)` / `HasStationTileRail(t)` 的**内联展开**，且读指针已是野指针。
- 地图 16384×8192，Win10 19045，GCC 13 win32。

## 二、源码锚点（0.72.4 逐行核对结果）

| 位置 | 内容 | 判定 |
|---|---|---|
| `follow_track.hpp:145` | `Follow()` 末尾 `return this->FollowTileExit();` | 崩溃入口 |
| `follow_track.hpp:239` | `this->new_tile = TileAddByDiagDir(this->old_tile, this->exitdir);` | 非法格在此产生 |
| 紧随其后 | `if (IsRailTT() && HasStationTileRail(this->new_tile))` | 崩溃读点 |
| `train_cmd.cpp:3998` | `ExtendTrainReservation(v, origin, ...)` 定义 | — |
| `train_cmd.cpp:4004` | 函数体起手 `ft.Follow(tile, cur_td)`（`tile = origin.tile`） | 崩溃调用点 |
| `train_cmd.cpp:4639` | `origin = FollowTrainReservation(consist, nullptr, OkayUnused)` | origin 来源 |
| `train_cmd.cpp:4642-4643` | `if (do_track_reservation) res_dest = ExtendTrainReservation(consist, origin, ...)` | **缺 `origin.tile == INVALID_TILE` 守卫** |

## 三、根因链（高置信度推断）

1. `FollowTrainReservation(..., OkayUnused)` 在本情形返回 `INVALID_TILE`（原因待定，见 §四）。
2. 0.72.4 的 `ChooseTrainTrack` 拿到 origin 后**没有校验**，直接进 `ExtendTrainReservation`。
3. `ExtendTrainReservation` 第一步 `ft.Follow(INVALID_TILE, cur_td)`。
4. `FollowTileExit` 用 `TileAddByDiagDir`/`TileAddWrap` 推算 `new_tile`：`INVALID_TILE` 的 `TileX/TileY` 必然越界 ⇒ **返回 `INVALID_TILE`（保持 0xFFFFFFFF）**。
5. `HasStationTileRail(0xFFFFFFFF)` ⇒ `_m[0xFFFFFFFF]` ⇒ 读地址 ≈ 基址 + 34 GB ⇒ 段错误。
   与 `RAX = 0xFFFFFFFF` 完全吻合（该值正是被传入的 tile 索引）。

> 结论一句话：**不是渲染/存档问题，是“轨道预约追踪拿到了非法格索引（INVALID_TILE），并且 0.72.4 在这条路径上缺少非法值守卫，直接拿它去索引地图数组”。**

## 四、未确认项（决定“怎么救存档”）

- **U-1**：`FollowTrainReservation` 为何返回 `INVALID_TILE`。候选：
  - a) 列车处于**部分入库/车库边缘**，链上 tile 与 track 位不一致（上游 #15662 / #15680 「Train reversing when partially in a depot」）；
  - b) 车库整格预留位（`GetDepotReservationTrackBits`）与链头实际位置不符，走 `FollowReservation` 失败返回非法；
  - c) 隧道桥/站台整格预留（JGRPP 自有扩展）在 `FollowReservation` 中提前 break。
- **U-2**：崩溃前玩家最后操作（是否在车库拖车/整列换向/刚出库）。
- **U-3**：崩溃是否每 tick 必现（同一列车同一位置）——决定能否“载入后立刻暂停 + 卖车”自救。
- **U-4**：上游 0.72.4 之后是否已有该守卫修复（`origin.tile == INVALID_TILE` 早退）。**待 git 核对**。

## 五、止血方向（待 U-1~U-4 定稿后细化）

1. 立刻复制 `Documents\OpenTTD\save\autosave\` 全部文件到别处（只读备份，不覆盖）。
2. 载入后第一时间按暂停，避免 tick 推进到崩溃帧。
3. 定位问题列车（崩溃栈来自 `CheckNextTrainTile`，即“正在找下一段路”的那列车），整列拖回车库卖掉落库重建。
4. 若崩溃在载入后数秒内必现：改用 autosave 回退到崩溃前 1~2 个自动存档点，避开该状态。
5. 版本层面：升级到包含该守卫的 JGRPP 版本（待 U-4 确认版本号）。

## 六、下一步取证清单

- [ ] `git log` 核对 0.72.4 之后 `ExtendTrainReservation` / `FollowTrainReservation` 是否加了 `INVALID_TILE` 守卫（给出首个含修复的版本号）。
- [ ] 核对上游 #15662 / #15680 是否在 0.72.4 之前/之后（决定“车库换向”是否可作为解释）。
- [ ] 从崩溃日志尾部（寄存器/栈之外的段落）找是否有车辆 ID、tile 坐标线索。

## 七、列车已定位（2026-10-18，证据确证）

崩溃日志 `_tmp_crash_report.txt` 第 17-18 行（寄存器/调用栈之前的 "Crash context args" 段）直接给出了
被 tick 的车辆对象，原文：

```
17:  0: TrainController: veh: 71083: (Train 76 (CJ-C1), c:5, st:FE, vs:HSD, vf:FTs, vcf:R, gvf:,
      tf:J, trk: 0x80, [tile: 1FCD6D0 (5840 x 2035), type: 10 (Railway), height: 01,
      data: 05 07A5 00 01 C2 00 00 0006]), veh: 71085: (Train [N/A], ... 同一 tile, front: 71083 ...), veh: nullptr
18:  1: CallVehicleTicks: veh: 71083: (Train 76 (CJ-C1), ... 同上)
```

读数判定：

| 项 | 值 | 含义 |
|---|---|---|
| Vehicle ID | **71083** | 崩溃时正在执行 `TrainController` 的那节车（车头） |
| 车名/车型 | Train 76 / `CJ-C1` | China Set Trains 的 CJ-C1 |
| 同一列车另一节 | 71085 | 与 71083 同 tile，`front: 71083` ⇒ 只有 2 节的短编组 |
| 位置 | tile 0x1FCD6D0 = **(5840, 2035)** | 两节**都在同一格** |
| 轨道位 | `trk: 0x80` | `TRACK_BIT_DEPOT` ⇒ **整列车停在车库格内** |
| 状态 | `st:FE`, `vf:FTs` | 前端引擎（Front Engine）+ 已停止标记 |

⇒ U-1 的候选 **a/b 升为首选**：列车完全在车库格内，崩溃发生在 `CheckNextTrainTile`
（出库方向找下一段路）→ `ChooseTrainTrack` → `ExtendTrainReservation(origin)` 这条链上，
即 **车库格上的 `FollowReservation` 给出了非法 origin**（车库整格预留位与链头 trackdir 不匹配）。
候选 c（站台整格预留）与本案现场不符，降级。

⇒ 需要观察的列车就是 **Train 76 (`CJ-C1`)，veh 71083**，观察窗口 = 它从 (5840,2035) 车库出库的那几秒。

## 八、玩家现场报告（2026-10-18）：出库瞬间崩，库内无他车，从未 arrange train

玩家原话三条事实：

1. **崩溃时刻 = 列车出库的一瞬间**；
2. 该车库格**只有它一列车**（无第二列车、无同格等待者）；
3. 这列车**最近在车库里从未被 arrange train**（没有被拖拽/重排/重新拼挂过）。

⇒ 三条对应地**排除**了我此前列的候选：

| 候选 | 状态 |
|---|---|
| 车库整格预留位被**别的车**占着 | **排除**（库内只有它） |
| 库内被拖过/掉头/解挂重挂导致链头 trackdir 与整格预留位不一致 | **排除**（从未 arrange train） |
| 列车在库内、正常出库时 `FollowReservation` 在 depot 格上返回 INVALID_TILE | **唯一剩余** |

收窄后的机理假设（待源码核实）：车库里整列车在 `TRACK_BIT_DEPOT`（0x80）上，出库寻路时
`ChooseTrainTrack` 取 `origin = FollowTrainReservation(cur_tile, cur_trackdir)`，
而 depot 格的 reservation 语义是"整格"（`GetDepotReservationTrackBits`），
`FollowReservation` 对 depot 格的 trackdir/预留位匹配判定失败即返回 `INVALID_TILE`，
随后 `ExtendTrainReservation` 不校验就 `ft.Follow(INVALID_TILE, ...)` ⇒ 崩溃。

⇒ 下一步取证（源码，非游戏内）：`pbs.cpp` 的 `FollowReservation` 对 `IsRailDepotTile` 的处理分支、
`GetDepotReservationTrackBits` 的置位时机（进库/出库/ReserveTrackUnderConsist），
以及 `train_cmd.cpp` 里 `CheckNextTrainTile → ChooseTrainTrack` 这一段 origin 的来源与是否可空。
⇒ 游戏内已不需要再观察别的列车；只需确认"单列车、纯出库、无编辑"即可复现。

## 九、根因确证（2026-10-18）：0.72.4 上游已知 bug，已在 0.73.x 修复

### 9.1 玩家版本

`_tmp_crash_report.txt:22-24`：

```
OpenTTD version:
 Version:     jgrpp-0.72.4 (0)
 Release ver: 0.72.4
```

`_tmp_crash_report.txt:318`：`Revision text changed to jgrpp-0.72.4, savegame version 292`。

### 9.2 上游修复提交（证据确证）

```
$ git log -1 --format="%H | %ai | %an | %s" 23ab347212
23ab34721295d4f1d37284bf4b613e661c8e4f6f | 2026-07-23 17:36:24 +0100 |
Jonathan G Rennison | Fix incorrect handling of vehicles in depots in CheckNextTrainTile
```

提交正文：`Merge error in 6b8d03dc`（6b8d03dc = "Feature: Trains with an engine on the rear
drive backwards when reversing"）。diff（`git show 23ab347212 -- src/train_cmd.cpp`）：

```c
 static void CheckNextTrainTile(Train *moving_front)
 {
 	/* Don't do any look-ahead if path_backoff_interval is 255. */
 	if (_settings_game.pf.path_backoff_interval == 255) return;

+	/* Exit if we are inside a depot. */
+	if (moving_front->track == TRACK_BIT_DEPOT) return;
+
 	Train *consist = moving_front->First();
```

### 9.3 版本比对

| 对象 | 日期 | 是否含该 3 行 |
|---|---|---|
| `jgrpp-0.72.4` tag 提交 `ed98bf4582` | **2026-06-19** | **否** |
| 修复提交 `23ab347212` | **2026-07-23** | — |
| `git tag --contains 23ab347212` | — | **jgrpp-0.73.1 / 0.73.2 / 0.73.3**（无 0.72.5） |

⇒ 玩家所用 0.72.4 **确定不含**该早退；工作区 R3R 树 `src/train_cmd.cpp:12534-12535` 已含该段
（`/* Exit if we are inside a depot. */` + `if (moving_front->track == TRACK_BIT_DEPOT) return;`）。

### 9.4 完整因果链（与崩溃日志逐项对齐）

1. 崩溃 dump：`veh 71083 (Train 76 CJ-C1)`，`trk: 0x80` = `TRACK_BIT_DEPOT`，tile (5840,2035)
   ⇒ 列车**仍在车库格内**（玩家："出库的一瞬间"）。
2. 0.72.4 的 `CheckNextTrainTile` **没有** depot 早退 ⇒ 继续执行：
   - `HasStationTileRail(depot_tile)` = false（车库不是站台，跳过 3508 前的停站判断）；
   - 订单不是"回本库"（是出库去别处）⇒ `OT_GOTO_DEPOT && tile==dest_tile` 不成立；
   - `td = moving_front->GetVehicleTrackdir()` 在 depot 上返回**通往库外**的 trackdir；
   - `CFollowTrackRail ft(consist); ft.Follow(tile, td)` 成功，`ft.new_tile` = 库外第一格，且未预留
     ⇒ 官方 `train_cmd.cpp:3508` 调用 `ChooseTrainTrack(consist, ft.new_tile, ...)`（栈 `[04]`）。
3. `ChooseTrainTrack` 内先取 `origin = FollowTrainReservation(v)`，起点是**车自己所在的 depot 格**；
   depot 格的预留是"整格"语义（`GetDepotReservationTrackBits`），与车头 trackdir 不匹配
   ⇒ `FollowReservation` 返回 `INVALID_TILE`（`0xFFFFFFFF`）。
4. `ExtendTrainReservation`（内联于 `ChooseTrainTrack:4004`）**不校验** origin 就
   `ft.Follow(INVALID_TILE, ...)` ⇒ `FollowTileExit` 用非法格推算 ⇒ 仍是 `0xFFFFFFFF` ⇒
   `HasStationTileRail(0xFFFFFFFF)` 读 `_m[0xFFFFFFFF]` ⇒ `C0000005`，`RAX=0xFFFFFFFF`（与日志寄存器一致）。

⇒ **结论定性：这是 jgrpp-0.72.4 的上游已知 bug（0.72.4 缺 3 行 depot 早退），与玩家的操作、
车库状态、存档内容全部无关**。玩家三条现场事实（出库瞬间 / 库内无他车 / 从未 arrange train）
正是该 bug 的**必然表现**：只要列车在库内且订单要出库，每 tick 都必崩。

### 9.5 处置

- **正解**：升级到 jgrpp-0.73.1 / 0.73.2 / 0.73.3（含 `23ab347212`）。
- 若必须留在 0.72.x：只能自行给 `src/train_cmd.cpp` 的 `CheckNextTrainTile` 补那 3 行后重编。
- 用工作区 R3R 版代跑**不合规**：R3R 基线是 0.73.x，另存后存档版本高于 0.72.4，0.72.4 将拒绝读取。
