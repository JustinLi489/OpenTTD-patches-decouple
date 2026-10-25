# 第 276 轮：站台整格预留的三个结构性问题（玩家三条报告）

日期：2026-10-22　状态：登记 + 初步定性（本轮先落盘，随后取证）

## 一、玩家原话（三条，逐字要点）

1. 「游戏中，一列火车进站时，会把预留铺满整个到发线，然后自己完全出站之后再一次性销毁整条到发线上面的预留。但是，这样子在R3R中会出现一种情况，两列重连动车组站台解挂，分头行动。前车离开之后，后车还在车站内，但是站台上已经没有预留了，可能包括后车脚下。」
2. 「我不知道正常出站是怎么样，但是还是那个动车组分离场景，假如站台距离出站信号机有一段距离，那么前车向信号机铺设预留并且穿过信号机之后，会出现站台到信号机那一段的预留在前车驶过之前被离奇销毁的情况」
3. 「还记得我提过的那个在通过式路点等待的功能吧，符合条件的列车的预留没有在它那一格停止，而是穿过了它」

对应 KI：**KI-427 / KI-428 / KI-429**（均已写入 `R3R_KNOWN_ISSUES.md` 末尾）。

## 二、P1（KI-427）：解挂后前车出站，把整条到发线的位（含后车脚下）一次性清空

### 2.1 机制（引擎事实，已在此前轮次验证）

- 站台/路点的预留是**整格位**：一格一个 bool（`SetRailStationReservation(tile, bool)`，`src/station_map.h`，`_m[t].m6` bit 2），**不按车、不按节、不按路径段记账**。
- 列车进站时会把整条到发线（整个 platform）订满，这是 `ReserveTrackUnderConsist()` 的平台分支 + `SetRailStationPlatformReservation()` 的语义。
- 出站时引擎走 `ClearPathReservation()` → `SetRailStationPlatformReservation(tile, ...)`，**按整条平台清**，不是只清自己走过的那一格。

### 2.2 R3R 特有的放大条件

解挂把一条链拆成两条**彼此独立、但物理上同处一条到发线**的列车：

- 前车（v）带着排程先走，出站 → 引擎清整条平台 ⇒ 后车（u）脚下的位一并消失；
- 后车（u）**补不回来**，因为唯一的脚下重订入口 `ReserveTrackUnderConsist()` 对"是否值得保护"有判据 `R3RIsReservationHolder()`：
  `R3RIsCarOnlyFormation(consist) || (consist->IsPrimaryVehicle() && consist->current_order.IsType(OT_WAIT_COUPLE))`
  解挂后的后半是**带排程的真列车**（既非纯车底、也不在 `OT_WAIT_COUPLE`），⇒ 不是 holder ⇒ 不会有任何人给它重订平台位。

### 2.3 后果

- 后车在站台上"裸奔"：别的车可以订进它脚下的平台格（整格位一旦为 false 就没人保护）；
- R3R 的耦合寻路 `ReserveTrackUnderConsist` 的 `HasReservedTracks` 前置也可能因此判错；
- 玩家观察到的"站台上已经没有预留，包括后车脚下"就是这条链的终态。

### 2.4 修法候选（未拍板）

- **A（最小侵入，推荐先试）**：解挂提交点给解出方补一次"平台保护"——调 `ReserveTrackUnderConsist()` 并在之后每 tick 的 holder 判据里把"在站台上、速度 0、且有排程"也算 holder。风险：扩大 holder 集合会改变 `RESV-NOBOOK`（KI-208）的行为面。
- **B**：在 `SetRailStationPlatformReservation` 的清空路径上加"还有别的车站在该平台上则不整条清"——侵入引擎热路径，属 desync 风险区，**不建议**。
- **C**：解挂时把后车沿平台推一格，让两车不共享同一条到发线——几何侵入大，且玩家场景是长编组，推不动。
- 判据：解挂后前车出站，日志中该平台格仍 `reserved=1` 且审计 `RESV-AUDIT-WATCH` 报 `kind=STATION`，后车能正常出站。

## 三、P2（KI-428）：站台→出站信号机那一段的位，在前车驶过**之前**被销毁

### 3.1 与 P1 的区别

P1 是"前车**离开站台之后**，平台位被清"（引擎整条清）。P2 是"前车**已经越过信号机**、但那段路还没走过"位就没了 ⇒ 时序上更早、位置在站台与信号机之间，不是平台格而是普通铁轨（plain rail）。

### 3.2 猜测根因（待验证，三候选）

- **C-1 `R3RReleaseChainReservations()` 的 doomed 名单口径错**：它按"本链 own_path 记账的格"清，若记账把"预订覆盖"当成"已驶过"，就会把还没走到的格清掉（`src/train_cmd.cpp:11691-11763`）。
- **C-2 `ExtendTrainReservation()` 延伸时释放旧段**：每次向前延伸会把"已驶过"的段放掉，若 `begin_tile` 取值错（例如从新终点而不是当前位置起算），会误放未驶过段（`train_cmd.cpp` 的 `stale-own-bit` 邻近逻辑，:14517 一带）。
- **C-3 解挂提交点的 `R3RResvLedgerForgetMask("decouple")` 把 PATH 行整批删掉**，随后引擎 `ClearPathReservation()` 按"账本无主"把 plain rail 位按地图直接清（这一条会同时解释"穿过信号机后马上没"）。

### 3.3 需要的取数

同一场景的 `build\R3R_debug.log` 中：`RP cur=` 序列（铺到哪一格）、`RESV-GC`（谁以什么 `why` 放的）、`CLEAR-RAIL`/`SET-RAIL`（引擎侧 setter 记录）、解挂前后的 `DECOUPLE-FIRE/DONE` 与 `RESV-REL`。判读关键：被销毁的那几格在 `RESV-GC` 里出现的 `why` 是什么（`decouple` / `free-path` / `resync` / `-notowner`）。

## 四、P3（KI-429）：通过式路点等待，预留穿过了等待格

### 4.1 现象

"通过式路点等待"（列车在路点停车等待，不在路点做装卸）的列车，其预留应当**终止在路点格**，实际却铺过了路点格，继续向下一站延伸。

### 4.2 猜测根因（待验证）

- **C-A 停车点与预留终点用了两个不同的来源**：预留终点由 `current_order` 的 destination 推（路点之后的下一站），而"通过式路点等待"的停车点由专门的路点等待逻辑决定 ⇒ 预留按下一站铺，自然穿过路点。
- **C-B 路点等待在 `ProcessOrders` 才触发**，但 `TrainController`/`TryPathReserve` 在此之前已经把路径铺到下一站；等待触发后没有"回退预留"的动作。
- **C-C 路点格的整格位与 plain rail 记账混用**：预留判定停在立交/侧线格上时，按 `TrackdirToTrack` 只订一条轨，视觉上"穿过"是另一条轨的位。

### 4.3 需要的取数

`RP cur=` 最后几格是否越过路点、`RESV-WATCH SET-STN` 的目标格、以及该列车 `current_order` 类型与 `dest_tile`。

## 五、优先级与顺序

1. **KI-428 优先**（"位在前车驶过之前消失"最容易造成正面冲突/卡死）；
2. 其次 KI-427（后车裸奔，可导致冲突）；
3. KI-429 属行为不符（预留过度），危险度较低但会挡别人。

## 六、上一轮（KI-425 桥隧/道口）构建自证 —— 已确认成功

- `build\R3R_incbuild.done` = `EXIT_CODE=0`
- `build\R3R_incbuild.log` 尾：`[2/3] Building CXX object …src\pbs.cpp.obj` → `[3/3] Linking CXX executable openttd.exe`
- `src\pbs.cpp` @ 2026-10-22 17:18:59 → `build\openttd.exe` @ 2026-10-22 17:21:09
⇒ KI-425 已在 exe 内，本轮及后续读日志的结论按此 exe 有效。

## 七、本轮静态取证结果（行号锚点，已实证）

### 7.1 P1 病灶（铁证）：清平台不看平台上还有没有别的车

`src/train_cmd.cpp:14164-14171`（`ClearPathReservation()` 内部）：

```cpp
} else if (IsRailStationTile(tile)) {
	DiagDirection dir = TrackdirToExitdir(track_dir);
	TileIndex new_tile = TileAddByDiagDir(tile, dir);
	/* If the new tile is not a further tile of the same station, we
	 * clear the reservation for the whole platform. */
	if (!IsCompatibleTrainStationTile(new_tile, tile)) {
		SetRailStationPlatformReservation(tile, ReverseDiagDir(dir), false);
	}
}
```

⇒ 车尾离开站台**最后一格**的那一刻，直接把**整条平台**置 false，函数里没有任何"平台上还有别的车"的判据。`src/station_cmd.cpp:1527-1540`（`FreeTrainStationPlatformReservation`）同理：也是按移动前端/后端各清一次整条平台。

**补得回来吗？可以，但没人调用。** `src/train_cmd.cpp:19101-19106`（`Train::ReserveTrackUnderConsist()` 的平台分支）：

```cpp
if (IsRailStationTile(tile)) {
	bool occupied = IsRailStationPlatformOccupied(tile);
	DiagDirection dir = AxisToDiagDir(GetRailStationAxis(tile));
	SetRailStationPlatformReservation(tile, dir, occupied);
	SetRailStationPlatformReservation(tile, ReverseDiagDir(dir), occupied);
}
```

它按"平台是否被占用"整条重设，所以在后车还站在台上时调用一次就能把位补回 true。问题纯粹是**后车不是 holder**（`R3RIsCarOnlyFormation || (IsPrimaryVehicle && OT_WAIT_COUPLE)`），解挂后的后半是带排程的真列车 ⇒ `TrainLocoHandler` 不会为它调这个函数。

### 7.2 P2 病灶：整体清路径 + 只从车头起重铺

`src/train_cmd.cpp:14184-14224`（`FreeTrainTrackReservation(Train *consist, TileIndex origin, Trackdir orig_td)`）：

- `:14195` 第一句就 `R3RResvLedgerForget(consist->index, "free-path")` —— **把这个链头名下的账本行整批忘掉**，其中包含 yapf `rp()` 钩子铺下的、不属于 `consist->path` 的位（注释 §21.6 明说）。
- `:14221-14224` `tile = origin != INVALID_TILE ? origin : moving_tile`（`moving_tile = moving_front->tile`）⇒ 无参调用时**从车头当前格起算**，沿路径把整条预留放掉；随后 `TryPathReserve(consist, false, first_tile_okay)`（`:4895`）只从当前车头位置重新铺。

⇒ 若调用发生在"车头已过信号机、但车尾与站台之间那段还没驶过"的时刻，则：整条旧路径（含站台→信号机）被清 → 重铺只覆盖车头前方 → **视觉上站台到信号机之间断掉一段，而这列车其实还要驶过它**。这正是玩家报告二描述的形态。待日志确认调用者（解挂提交点 / 出站 `CheckNextTrainTile` / `MarkTrainAsStuck` 后的重试）。

### 7.3 P3 病灶：路点等待只是个状态判断，不拦寻路

`src/train_cmd.cpp:12986-13005`（通过式路点等待的判据）：

- `:12988-12996` 当前订单必须是 `OT_GOTO_COUPLE`，且**前一条真实订单**是 `OT_GOTO_WAYPOINT`；
- `:12998-13001` 车头必须在路点格上、且路点索引等于该订单目的地；
- `:12990` 注释自陈：`cur_real_order_index already points at the GOTO_COUPLE order (the waypoint is switched away as soon as the head enters its tile, see :13656)`。

⇒ **车头一进入路点格，`current_order` 就切换成 GOTO_COUPLE**，而 `GOTO_COUPLE` 是"行进订单"（`:14822-14832` 明确它可被选为目标、`UpdateOrderDest` 会算出新的 `dest_tile`）。于是 `TryPathReserve()` 按**切换后的目标**铺路，预留自然穿过路点继续铺向会合点。等待逻辑（本函数）只回答"我现在是不是在等待"，没有任何"把预留截断在路点格"的动作。

⇒ P3 修法方向（未拍板）：在车头进入路点格、切换到 GOTO_COUPLE 的那一刻，若要等待，应**跳过/推迟** `UpdateOrderDest` + `TryPathReserve`（保持 `dest_tile` = 路点格，或让 `CheckNextTrainTile` 不对该状态延伸），而不是先铺完整条路再靠速度归零停住。

## 八、下一步

1. P1（KI-427）：扩 holder 判据 + 解挂提交点补一次 `ReserveTrackUnderConsist()`（最小侵入，先试）。
2. P2（KI-428）：先取日志确认是谁调 `FreeTrainTrackReservation()`，再决定改调用点还是改"重铺起点"。
3. P3（KI-429）：把"切换成 GOTO_COUPLE"与"是否已到等待点"解耦，等待期不 `UpdateOrderDest`。

## 九、P1（KI-427）本轮已落地（只改 src\train_cmd.cpp，未碰 src\*.h）

**没有走 §八.1 的"扩 holder 判据"**：把 holder 扩成"任何停稳在站台的列车"，等于让每一列在站台装卸的车每 tick 都跑一遍 `ReserveTrackUnderConsist()`，并且会顺带撞上 KI-206 那段 waiter 清扫（`R3RReleaseChainReservations(consist, "waiter")`）——那条清扫对合法路径是有破坏性的，只该对等待者生效。代价与风险都远大于"在释放侧加一个判据"。

**实际改动**（`src/train_cmd.cpp` 的 `ClearPathReservation()` 站台分支，原 `:14164-14171`）：

```cpp
if (!IsCompatibleTrainStationTile(new_tile, tile)) {
    /* R3R (KI-427): 只有本到发线上再没有别的列车时，才整条释放 */
    const Axis station_axis = GetRailStationAxis(tile);
    const DiagDirection axis_fwd = AxisToDiagDir(station_axis);
    const DiagDirection axis_back = ReverseDiagDir(axis_fwd);
    if (IsRailStationPlatformFree(v, tile, axis_fwd) && IsRailStationPlatformFree(v, tile, axis_back)) {
        SetRailStationPlatformReservation(tile, ReverseDiagDir(dir), false);
    }
}
```

四点说明：

1. **判据复用引擎现成的 `IsRailStationPlatformFree()`**（`src/pbs.cpp:999-1020`，声明 `src/pbs.h:138`）。它沿站台轴逐格枚举列车（撞毁车 `IsCrashTesting` 跳过），并用 `First()->index == consist->First()->index` 把**本链整条**排除 —— 语义正是"还有没有别人"，不是"有没有车"。
2. **必须双向各调一次**：`IsRailStationPlatformFree(start, dir)` 只朝 `dir` 一个方向走（内部 `if (GetRailStationAxis(start) != DiagDirToAxis(dir)) return false;`），而车通常停在到发线中间，只查一侧会漏。方向由 `GetRailStationAxis(tile)` 推出来，天然满足它对轴的要求。
3. **不释放 ≠ 泄漏**：真正最后离开的那列车仍会走到同一句，届时判据成立、照旧整条释放，与原生行为逐字相同。停着的后车继续受这条整平台位保护，前车驶离也不会把它清掉。
4. 只动释放侧，**没有**去动 `ReserveTrackUnderConsist()`、没有新增 holder、没有改 `SetRailStationPlatformReservation()` 的语义，也没碰平台订位（`pbs.cpp:1000` 一带）的热路径。

### 9.1 构建自证（第 276 轮）

复用既有 `_tmp_inc_build.cmd`（未新建任何 .cmd），启动方式 `Start-Process -FilePath 'cmd.exe' -ArgumentList '/c','_tmp_inc_build.cmd' -WorkingDirectory 'd:\sourcecode of JGRPP' -WindowStyle Minimized`。

| 判据 | 读数 | 结论 |
| --- | --- | --- |
| `build\R3R_incbuild.done` | `EXIT_CODE=0`（构建前该文件不存在，属本轮新写） | 成功 |
| 日志尾部 | `[2/3] Building CXX object …src\train_cmd.cpp.obj` / `[3/3] Linking CXX executable openttd.exe` | 走到链接 |
| 时间戳 | `src\train_cmd.cpp` 17:32:21 → `train_cmd.cpp.obj` 17:32:59 → `build\openttd.exe` 17:34:25（52 141 056 B） | obj 晚于源码、exe 晚于 obj |
| `read_lints` | `src\train_cmd.cpp` 0 条 | 干净 |

未新增日志字面量（本轮只加注释与判据，没有新探针），故不做 exe 字符串核对 —— 复测靠既有的 `RESV-AUDIT-WATCH … kind=STATION` / `RESV-AUDIT-STRAY` 读数即可判定。

## 十、P2（KI-428）/ P3（KI-429）本轮为什么不动

两条都已定位到"可能的病灶"（§7.2 / §7.3），但**修法会改预留的释放/铺设语义**，在拿到那次调用的日志之前动手风险太高 —— 这个存档最怕的正是"预留断片"（KI-426）与"停放车被顶死"（KI-330 家族）两类回归。

### 10.1 P2 需要的取数（三选一即可定性）

在复现场景（两列重连动车组站台解挂、站台离出站信号机有一段距离）抓 `build\R3R_debug.log`，然后看：

| 判据 | 看的行 | 说明 |
| --- | --- | --- |
| 谁清了那一段 | `RESV-GC ... why=free-path`（`pbs.cpp` 的 checked release）| 出现这行 ⇒ 是 `FreeTrainTrackReservation()` 的账本整批忘（§7.2 候选 A） |
| 同上 | `RESV-GC ... why=decouple-v` / `decouple-u` | 解挂提交点 `R3RReleaseChainReservations()` 释放（候选 B） |
| 同上 | `UNRES-RAIL ... phase=` / `CLEAR-STN` | 引擎逐格释放（候选 C，需看 `phase=` 与 actor=） |
| 那一刻车头在哪 | 同一时刻的 `CPL-*` / `TRP veh=` / `RESERVECONSIST` | 判断"车头是否已过信号机、车尾是否还在站台→信号机段上" |

**只要有这三类行中的任何一类出现在"车头过信号机之后、车尾离开那段之前"，就能立刻定死是哪个调用者，改法随之确定（改调用点 vs 改重铺起点 vs 只把"脚下那几格"排除在整批忘之外）。**

### 10.2 P3 需要的取数

同一份日志里搜 `CPL-HOLD` 与 `CPL-PAIR`：

- 若车头进入路点格时 **`CPL-HOLD ... confirm=0`** 出现过、而预留仍穿过去 ⇒ 是"进入前就铺好了"，修法=进入路点格时把 `dest_tile` 钉在路点格（§7.3 修法方向）。
- 若从头到尾只有 `CPL-PAIR`（配对锁早于到点建立，`confirm=1`）⇒ 等待逻辑认为"已经可以走"，预留穿过是**设计口径**（第 272 轮：配对锁存在即放行），要改的是"等到什么才算 confirm"，需玩家拍板。
- 若两者都没有 ⇒ 说明该车根本没进 `R3RShouldHoldForCouple()` 的判据（订单形状不对：前一条真实订单不是 `OT_GOTO_WAYPOINT`），需核对订单窗口里那两条订单的类型。

⇒ 这三条互不重叠，一条日志即可分流。

---

## 十一、玩家日志分流（2026-10-22，第 276 轮续）

日志＝`build\R3R_debug_276_player.log`（1112 行，第 276 轮 exe）。场景：`GEO settle-decouple-v head=21` 占 267..272,237、`GEO settle-decouple-u head=24` 占 273..278,237，站台 265..278,237 共 14 格。

### 11.1 KI-427 实机确认（通过）

| 行 | 原文要点 | 判定 |
| --- | --- | --- |
| :373-375 | `RESV-GC veh=21 tile=264,237 / 263,237 / 262,237 why=decouple` | 只放掉平台西侧 3 格纯轨（v 半前进方向＝它自己的旧路线） |
| :376 | `why=decouple mask=0x3 released=3 kept=14` | 14 格站台位一格未放 |
| :377-390 | 连续 14 行 `why=decouple-hold` | 逐格：格上还有车（24 站上面）⇒ 拒绝释放 |

⇒ KI-427 的释放侧守卫（`ClearPathReservation()` 里 `IsRailStationPlatformFree()` 双向判据）实机通过，玩家亦确认"问题 1 符合预期"。本轮不追加改动。

### 11.2 KI-428 分流 → 命中候选 A

| 行 | 原文要点 | 说明 |
| --- | --- | --- |
| :824 | `RESV-GC veh=21 why=free-path mask=0x1 released=95 kept=0 phase=choose-track` | 一次 95 位 |
| :800-819 | `... tile=238..258,233 why=free-path` 含 `UNRES-RAIL tile=254,233`、`CLEAR-STN tile=254,233 phase=gc-stn actor=21` | 成批释放里含整格位 |
| :825 | `DEPOT-ARR veh=21 spd=0 real=11(6)`（tx=205 ty=274） | 释放时车早已不在那段路上 |

本日志中 `why=free-path` 是**唯一**成批出现的释放原因（单次 95 位），`decouple-*` 只有 §11.1 的 3+14 格 ⇒ **候选 A（`FreeTrainTrackReservation()` 整批忘账本＋从车头重铺）成立，候选 B 排除。**

机制（`pbs.cpp:267-274` `R3RResvLedgerForgetMask()`）：`free-path` 掩码＝`PATH`(0x1)，只放路线位不动 consist 位；`decouple` 掩码＝`ALL`(0x3)，靠逐格 `-hold` 豁免。日志里靠 `mask=` 字段可直接区分。

疑点 **U-428-A**：本日志的 `free-path` 释放的是 21 的**旧路线**（238..258,233），并非"站台→信号机"段；玩家报的该段消失在本日志对应的是 :373-375 那 3 格纯轨被 `decouple` 放掉、随后 :473 立即重订。⇒ 机制已明确＝解挂提交点会放掉前车前进方向上的纯轨位（语义必要，旧路线已作废），视觉上会闪一帧；若要消除闪烁，修法＝放之前先替 v 预演一次 `TryPathReserve`（观感项，不改正确性）。

### 11.3 KI-429 分流 → "只有 CPL-PAIR"档，且证据把方向反转

| 行 | 原文要点 | 判定 |
| --- | --- | --- |
| :1068 | `TRP veh=24 ... type=17 dest=INVALID ok=0 res=0` | 24 停在路点跑 WAIT_COUPLE，不订路线 |
| :1069 | `RESV-NOBOOK veh=24 tile=265,231 order=17 caronly=0 reason=parked-waiter` | KI-208 闸门拦下（设计如此） |
| :1101 | `RESV-WATCH SET-STN tile=265,234 track=255 phase=controller actor=47 head=21` | 来车链（车 47）订下整格位并直穿 |
| :1111-1112 | `CPL-PAIR actTile=264,234 tgtTile=265,231 dist=4` | 配对锁已建立 |
| 全日志 | `CPL-HOLD`（代码名 `CPL-HOLD-NOBOOK`）0 条 | ⇒ §10.2 第二档 |

⇒ 等待逻辑认为"已可走"（第 272 轮口径：配对锁存在即放行），不是判据没进。

**更强的现场**：全日志**没有任何** `SET-STN tile=265,231` ⇒ 等待车 24 脚下那格没人替它订整格位，而来车能给紧邻的 265,234 订位并继续通过。⇒ 方向可能是反的：不是"等待方预留穿出去"，而是"**等待方脚下没整格位，别人能穿过来**"。

疑点 **U-429-A**（先取证再改码）：`265,231` 是否路点格；`ReserveTrackUnderConsist()` 对路点格走 `SetRailStationPlatformReservation`（打 `SET-STN`）还是 `TryReserveRailTrack`（打 `SET-RAIL`）。两类探针都不见该格 ⇒ 要么未覆盖、要么真没订。取法：给该函数的站台/路点分支补只读探针 `RVC-STN veh= tile= booked|skip-<reason>`，同场景复跑一次即可定死。

### 11.4 本轮刻意不动

KI-428 修法（把"车头已过、车尾未过"的位排除在整批忘账外）＝改预留语义，KI-426/KI-330 回归面大，等 U-428-A 补齐观感判据再动；KI-429 等 U-429-A 取证。

---

## 十二、解挂后"主人"要不要转给后车（回答玩家提问）

玩家原话：「分离后，预留本来是前车的，那现在是要把主人转移给后车吗？」

### 12.1 两层结构

| 层 | 数据结构 | 有无 owner | 依据 |
| --- | --- | --- | --- |
| 引擎地图位 | `SetRailStationPlatformReservation()` / `SetTrackReservation()`，落在 `_m[t].m6`（站台整格位）与 `m2`（轨道位） | **无**，纯布尔 | `station_map.h` / `rail_map.h` |
| R3R 账本 | `_r3r_resv_owner_of_bit` + rows，键＝`R3RResvBitKey(tile,track)`，值含 `booker`（链头 id）＋purpose | **有** | `pbs.cpp:277-292` |

⇒ 引擎层没有对象可"转移"；能变的只有账本行的 booker。

### 12.2 解挂提交点已经在"过户"，方式不是转移

`src/train_cmd.cpp:9485-9503`：先 `R3RReleaseChainReservations(v/u, "decouple")`（掩码 ALL），再 `u->ClearReservationUnderConsist(); u->ReserveTrackUnderConsist();` —— 留下的一半**把自己的脚下位重新订一遍**。重新订位＝新 booker（u 的新链头，本例 24）覆盖旧行，这就是过户，不必新增 transfer API。日志 :377-390 的 14 行 `-hold` 是"旧 booker(21) 想放、因格上有车被拒"的中间态，随后由 9503 那句重订接管。

### 12.3 真实缺口（登记 KI-430）

v 已让出、u 又够不到的**空档格**（本例 265,266）不会被 u 重订，仍记在 21 名下。它们不会被误放（KI-427 守卫），但：①日志/审计 `owner=21` 一直指着已开走的车；②若 21 被卖，`R3RResvLedgerForget(21,"destroy")` 是 ALL 掩码、撞 `-hold` 会留下指向已释放车辆 id 的行（KI-207c「有主但已死」未做）。

可选修法（待拍板，本轮不动）：
- **T1（推荐最小）**：`R3RResvLedgerForget()` 的 `-hold` 分支把行改挂到"格上现任者" `GetFirstVehicleOnTile(tile)->First()`。一句覆盖空档格与死 booker 两种情形。
- T2：解挂提交点显式给 u 到平台尽头的空格补订。范围小但多一处 map 写、需防越界到别人站台。
- T3（不建议）：新增 booker 转移 API，与 T1 等价且绕过 Mirror law 对账。

口径：**平台位不需要"转移主人"，需要的是"格上现任者接管账本行"**；KI-427 的守卫保证"没人站了才放"，两者互补。

### 12.4 待玩家拍板

- Q-276-1：T1"格上现任者接管"是否认可（改 `pbs.cpp` 释放语义，需连带回归 KI-363 的 consist 位存活设计）。
- Q-276-2：:373-375 那 3 格纯轨在解挂瞬间被放掉的预览闪烁，是否按 §11.2 末尾"先预演再放"消除（观感项）。
- Q-276-3：KI-429 是否按 U-429-A 补只读探针后再改。

### 12.5 一句话版答案（2026-10-22 玩家复查「这个问题你还没有回答我」，此处落盘复述）

问：分离后预留本来是前车的，那现在是要把主人转移给后车吗？

答：**要，但引擎层没有"主人"可转，实际做的是"格上现任者重新订位"**。

- 引擎侧（站台整格 bool / 轨道位）**没有 owner**，只有"这一格有没有被订"；所以不存在"把某对象的主人字段改个值"这回事。
- 真正被解挂提交点（`src\train_cmd.cpp:9485-9503`）做的事：`R3RReleaseChainReservations(v/u,"decouple")` 把两半都放一遍 → `u->ClearReservationUnderConsist()` 清 u 的记账 → `u->ReserveTrackUnderConsist()` 让留下的 u **把自己脚下位重新订一遍**。重订＝新 booker（u 的新链头，本例 24）覆盖旧行 ⇒ 等效过户。所以**不需要新增 transfer API**。
- 日志 `:377-390` 那 14 行 `why=decouple-hold` 就是这个中间态：旧 booker(21) 想放、因格上有车被拒；随后 `:9503` 的重订把它接管。
- **唯一没被接管的**：v 已让出、u 又够不到的**空档格**（现场 `265,266`）——不会被 u 重订，行仍记在 21 名下 ⇒ 已在 KI 清单登记为 **KI-430**，推荐修法 **T1**（`R3RResvLedgerForget()` 的 `-hold` 分支把行改挂 `GetFirstVehicleOnTile(tile)->First()`）。

⇒ 一句话：**平台位不需要"转移主人"，需要的是"格上现任者接管账本行"**；现状已覆盖 u 脚下，缺口只在空档格（KI-430）。

---

## 十三、玩家复核「分离思路」逐条确认（2026-10-22，同轮落盘）

### 13.1 玩家模型（原话要点）

> 「2 那个是旧预留啊，是分离之前铺设的预留吗，我怎么感觉它会影响行车的？我先捋一下列车分离的思路，列车分离之后，原先的变孤儿，前车开走，顺便把老预留全部按原生机制销毁。随后后车立刻把预留铺满站台（应该还要往自己脚下铺设，我怕有些神人玩家列车超出站台），是不是这样。」

### 13.2 逐条对错（源码锚点已核）

| 玩家说法 | 判定 | 实情 |
|---|---|---|
| 分离后原先的（预留）变孤儿 | **半对** | 不必等到"变孤儿"：解挂提交点 `src\train_cmd.cpp:9494-9495` 当场用 `ALL` 掩码把两半全放一遍。能放的放（`:373-375` 的 3 格纯轨），因格上有车放不掉的走 `-hold`（`:377-390` 的 14 格站台位）。**真孤儿只有空档格**（v 让出、u 够不到，现场 `265,266`）⇒ KI-430。 |
| 前车开走，顺便把老预留全部按原生机制销毁 | **顺序错** | ①销毁发生在**解挂那一刻**，前车还没动（`9494-9495`）；②前车出站走的是原生 `ClearPathReservation()` → `SetRailStationPlatformReservation()` 整条平台清，**这条已被第 276 轮 KI-427 加了双向 `IsRailStationPlatformFree()` 守卫**：台上还有别的车就不清。 |
| 随后后车立刻把预留铺满站台 | **只对一半** | `:9502-9503` 的 `u->ClearReservationUnderConsist(); u->ReserveTrackUnderConsist();` 确实在解挂同 tick 执行，但它订的是"**本链车节覆盖的格**"，**不铺整条到发线**。铺整条是引擎在 `TryPathReserve` 进站时做的（沿站台轴双向 `SetRailStationPlatformReservation`）。 |
| 还要往自己脚下铺设（怕整车超出站台） | **对，且已实现** | `Train::ReserveTrackUnderConsist()`（`17318` 起）`for (u = this; u; u = u->Next())` 逐节 `TryReserveRailTrack`（`17324-17369`），车节落在站台外的 plain rail 上也照样订 ⇒ 车长超过站台不会裸奔。**该函数本身无 holder 门禁**。 |
| 后车自己保住了整条到发线的位 | **不成立（重要纠偏）** | 解挂后的 u 是带排程真列车、不在 `OT_WAIT_COUPLE` ⇒ `R3RIsReservationHolder(u)` 为假 ⇒ `R3RGcConsistReservations()`（`17305-17315`，每 256 次调用）会把 u 的记账按 `holder-gone` 清掉；而 `R3RReleaseReservationSafe()` 只对 plain rail 真改地图（要求 `IsPlainRailTile`），**站台格只删账本、不动地图** ⇒ 地图位留着、账本变无主。整条平台位真正靠的是 KI-427 的**释放侧守卫**（前车不敢清），不是后车持有。 |

修正后的模型（一句话）：**解挂当场两半全放 → 能放的放掉 → 放不掉的靠"格上有车"豁免活下来 → 后车重订自己覆盖的格（含脚下）→ 那笔账本随后被 GC 判非持有者清掉，但站台地图位因释放侧守卫继续留着。**

### 13.3 Q-276-2 重新定性：那 3 格不是"别人的旧预留"，是**前车自己的旧路线**

- `:373-375` 的 `264,237 / 263,237 / 262,237` 是站台（`265..278,237`）**西侧进站方向**的 plain rail，属于解挂前整链（当时还是一列车）自己铺的 PBS 路线；解挂时作废，`:473` 立刻重订。
- **不影响行车正确性**：放掉的是 v 自己要走的位，重订窗口在同一/下一 tick；即使别的车抢进来，PBS 只会让 v 重新寻路（不会撞车）。可见后果=预览闪一帧 ⇒ 仍是观感项。
- **玩家"会影响行车"的直觉，真正对应的是 KI-428**：`:824 RESV-GC veh=21 why=free-path mask=0x1 released=95 kept=0` —— 21 的**整条旧路线**（`238..258,233`）被一次忘掉 95 位再从车头重铺，这段窗口才是"别的车可以插进来 / 位在前车驶过之前消失"的地方。⇒ 该修的优先级是 KI-428，不是这 3 格。

### 13.4 新疑点 U-427-C（登记，待取数）

**期望**：解挂后 u 名下 14 格站台位应持续有主。
**实测**：日志里既无 `holder-gone` 释放行，也无 `RESV-AUDIT-STRAY … kind=STATION` 对该 14 格的报告。
**存疑**：`R3RGcConsistReservations()` 每 256 次调用应触发一次，届时 u 的账本条目会被清；若真被清，地图位应表现为"无主站台位"，审计应在下一轮报 `STRAY kind=STATION`。
**还缺什么**：一段跨越 ≥512 次 `ReserveTrackUnderConsist` 调用的日志（或让审计快速档生效），看该 14 格是否出现在 `RESV-AUDIT-STRAY`；以及 `R3RReleaseReservationSafe` 对 `TileType::Station` 的实际分支（是"只删账本"还是"走 GC 站台分支"）。**在拿到数据前不改 GC 判据**——放宽 holder 会重开 KI-206 的口子。

---

## 十四、玩家 2026-10-22 三条答复与处置口径（原话要点 + 待办）

### 14.1 玩家原话要点

> 「1 已经符合预期，但假若有小瑕疵可以修复。2 我有个怎么修的想法，就是我们学着挂接给解挂后两列车也赋予一次铺预留的能力。然后就是，我想知道这个能力遇到连续的 R3R 动作会不会出问题，如果有就得修。3，证据不足吗，那就加探针吧」

### 14.2 逐条处置

| 编号 | 玩家口径 | 落地含义 |
|---|---|---|
| **Q-276-1** | 认可现状，**允许修小瑕疵** | KI-430（空档格账本仍挂已走车辆）按 T1 修（`R3RResvLedgerForget()` 的 `-hold` 分支改挂 `GetFirstVehicleOnTile(tile)->First()`）。属"小瑕疵"，改 `pbs.cpp` 记账归属，不动释放/铺设语义 |
| **Q-276-2** | **方案改为"仿照挂接，解挂后两列车各获得一次铺预留的能力"** | 即解挂提交点不再只 `u->ReserveTrackUnderConsist()`，**v 侧也补一次**（现状 v 侧只 `R3RReleaseChainReservations(v,"decouple")`，路权全靠下一 tick `TryPathReserve` 重铺 ⇒ 存在"空预留窗口"与预览闪烁）。**附加要求：必须分析该能力遇到连续 R3R 动作（解挂→再解挂 / 解挂→挂接 / 解挂→车库拖动 / 解挂→换端）是否出问题，有问题必须修** |
| **Q-276-3** | 证据不足就加探针 | KI-429 先补只读探针 `RVC-STN veh= tile= booked\|skip-<原因>`（`ReserveTrackUnderConsist()` 的站台/路点分支 + `TryPathReserve` 铺整条分支各一处），零行为变更，复跑取数后再定修法 |

### 14.3 Q-276-2 待回答的连续动作风险清单（本轮必须逐条核实）

1. **双记账**：两半车节覆盖格重叠时（解挂瞬间两半可同 tile），`ReserveTrackUnderConsist()` 的记账判据是 `if (ok || HasReservedTracks(u->tile, bits))`（KI-203），`HasReservedTracks` 不区分归属 ⇒ 第二个调用者可能把**别人的位**登记成自己的行 ⇒ GC/释放时误放。
2. **连续解挂**：解挂后立刻再解挂（多段车底一次解多节），提交点连续两次 `R3RReleaseChainReservations` + 两次重订，账本 key 随链头身份变化，可能出现孤儿行。
3. **解挂→挂接**：挂接提交点自己的预留写法与解挂新加的那次是否冲突（同格双边登记）。
4. **与 KI-208 门禁的关系**：`TryPathReserve` 对"停稳+站台+holder"有 `RESV-NOBOOK parked-waiter` 门禁；但 `ReserveTrackUnderConsist()` 不受该门禁约束，需确认不会与门禁语义打架。
5. **解挂→车库拖动**：`CmdMoveRailVehicle` 会改链顺序，账本按 index 记，拖动后同一 index 换了车节位置 ⇒ 陈旧行。

### 14.4 取证结论（2026-10-22，源码实读，★=与玩家前提不符）

**★F-1（2026-10-22 已订正，订正内容见 §15）`ReserveTrackUnderConsist()` 的调用点清单。** 全 `train_cmd.cpp` 仅 4 个调用点：`:9503`（`DecoupleTrain()` 内，u 侧，**这一对函数的用途就是"让等待被挂的车底占住轨道当障碍"，即玩家说的"挂接时期引入的函数"**）、`:17473`（崩溃后重订）、`:19728`（TrainLocoHandler 的平台等待门，每 tick）、以及 `:9502` 同处的 `ClearReservationUnderConsist`。**`Couple()`/`TryTrainCouple()` 函数体内确实没有直接调用**——但这只说明调用点位置，不改变"该函数为挂接服务"的来历；原文"挂接提交点并没有这次调用"一句**作废**。

**F-2 holder 判据 = `R3RIsCarOnlyFormation || (IsPrimaryVehicle && current_order.IsType(OT_WAIT_COUPLE))`**（`train_cmd.cpp:16631` 附近）。解挂后的两半是**带排程真列车**且不在 WAIT_COUPLE ⇒ **两半都不是 holder**。

**F-3 GC 每 256 tick 清一次非 holder 的账**（`R3R_CONSIST_RESV_GC_TICKS = 256`，`train_cmd.cpp:16629`；`R3RGcConsistReservations()` `:17305-17315`：`R3RIsReservationHolder(owner)` 为假 ⇒ 逐格 `R3RReleaseReservationSafe(..., "holder-gone")` 并 erase）。

**★F-4 释放守卫不对称：站台格"有车就 hold"，plain rail 没有这道守卫。** `pbs.cpp:511-528` 站台分支：`GetFirstVehicleOnTile(tile, Train) != nullptr || GetTrainForWholeTileReservation(tile) != nullptr` ⇒ 打 `-hold` 并 `return false`（不放）。而 plain rail 分支只校验 `IsPlainRailTile && HasBit(GetRailReservationTrackBits)`（KI-110 的"严格自证"），**不查格上有没有车**。

**⇒ 结论（Q-276-2 必须先解决的问题）**：若照原样在解挂提交点给 v 补一次 `v->ReserveTrackUnderConsist()`，v 会以 `v->index` 为 key 记进 `_r3r_consist_resv`（`:17391` `R3RReconcileConsistReservation(this->index.base(), ...)`）；因 F-2，v 不是 holder；256 tick 内必被 F-3 以 `holder-gone` 释放，而 **v 站在 plain rail 上时 F-4 会真把位放掉** ⇒ 前车裸奔，比现状更糟。现状 u 侧没炸，只是因为它记的是**站台格**、恰好撞上 F-4 的 `-hold`。

**⇒ 修法必须两步，顺序不能反：**
- **步 1（= Q-276-1 的 T1 扩展）**：把"格上有车就 hold"这道守卫从站台分支推广到 plain rail 分支，并在 hold 时把账本行**改挂给格上现任者**（`GetFirstVehicleOnTile(tile)->First()`）。这一步同时解决 KI-430 的"行挂在已走车辆名下"与 F-3/F-4 的误放。
- **步 2（= Q-276-2 的玩家方案）**：再在解挂提交点补 `v->ReserveTrackUnderConsist()`。此时即使 256 tick 后 GC 判 v 非 holder，`-hold` + 改挂会让位留在"格上现任者"名下，不会真放。

**连续 R3R 动作风险（对玩家问题的正面回答）**：

| 连续动作 | 风险 | 现状是否已有防护 |
|---|---|---|
| 解挂 → 立刻再解挂（多段车底连解） | 两次 `R3RReleaseChainReservations` + 两次重订，`_r3r_consist_resv` 的 key 随链头身份变，可能留下无人 reconcile 的行 | 有部分防护：`:495-508` 的 `-notowner` 拒绝跨链释放；但旧 per-consist 表与 pbs 账本是两套 key（`this->index` vs 链头），差异靠 `own_head` 兜底（`:500-503`） |
| 解挂 → 立刻挂接 | 挂接后 u 变 holder，`:19728` 会重订；若步 2 已给 v 记账，v 已并入新链、key 失效 ⇒ 行残留 | 靠 F-3 的 GC `holder-gone` 清，但同样受 F-4 影响 |
| 解挂 → 车库拖动 | `CmdMoveRailVehicle` 改链序，key 是车辆 index 不随链序变，行仍指向原车辆 ⇒ 一般安全；但 `ClearReservationUnderConsist` 按 `this->index` 清，若拖动后链头变了则清不全 | 已有 `R3RSyncChainAfterDepotEdit` 的 settle，但未清预留账本 |
| 解挂 → 换端/逻辑翻转 | 逻辑翻转只改 direction 与链序、不改位置，key 不变 ⇒ 安全 | 无额外需要 |

⇒ 连续动作里**唯一必须修的**就是 F-3+F-4 那条（非 holder 的 plain rail 位被 GC 真放），其余靠既有 `-notowner` 与 GC 兜底。

---

## 十五、玩家 2026-10-22 纠正原话（口径以本条为最终版）

> 「等一下，我说的赋予铺预留的能力是指借用既有的在挂接时期引入的函数，让这个函数给解挂分离后的两列车赋予铺预留能力，你别搞错了」

### 15.1 纠正后的正确口径

- **不新建函数、不改挂接逻辑**，复用挂接时期为"被挂链路权不丢"而引入的那一对：
  - `Train::ClearReservationUnderConsist()`（`src\train_cmd.cpp:17396` 定义）
  - `Train::ReserveTrackUnderConsist()`（`src\train_cmd.cpp:17318` 定义，KI-203 记账 + KI-110 逐节 `TryReserveRailTrack`）
- **玩家要的是"给 A、B 两列车各一次"**，即解挂提交点对 v 与 u 分别调这一对（或至少让两半都保有"铺自己覆盖格"的能力），而不是新增机制。
- 我上一轮的表述"挂接侧没有这个调用、能力来自身份"属于**把"函数是挂接时期引入的"误读成"挂接提交点在调它"**，已作废。

### 15.2 已核实（2026-10-22 源码实读）

**位置确认**：`src\train_cmd.cpp:9485-9503` 位于 **`DecoupleTrain()`** 内（该函数在 `TryTrainCouple()` `:10794` / `Couple()` `:11809` 之前，属解挂提交点），不在挂接侧。注释原文 `:9485-9493` 自陈：拆链后两半再也不会走合并链那条路线，故先 `R3RReleaseChainReservations(v/u,"decouple")`（`:9494-9495`）清掉旧位，然后 `:9497-9503` 才调 `u->ClearReservationUnderConsist(); u->ReserveTrackUnderConsist();`。

**该函数的来历（即玩家说的"挂接时期引入"）**：`ReserveTrackUnderConsist()` 的目的是**让等待被挂的车底占住轨道当障碍物**，好让挂接机车能寻路到它（`:9497-9500` 注释逐字："the waiting consist must hold its path reservation (occupy the track) so the signalling system treats it as an obstacle: other trains must not run through it, and the coupling locomotive can pathfind to its reserved track"）。它服务的是**挂接**，只是调用点落在解挂提交点（解出的车底正好是"下一个等待被挂者"）。⇒ **玩家口径正确，§14.4 的 F-1 中"挂接提交点并没有这次调用"一句作废**（那句只说明 `Couple()/TryTrainCouple` 内没有直接调用，与函数来历无关）。

**现状精确描述**：
- **u（解出的那半）**：已调一次 —— `:9502-9503`。
- **v（留下的机车半）**：**没有调**，注释 `:9492-9493` 明写 "the locomotive books its route on the next pathfind"。
- ⇒ 玩家方案落地 = 把 `:9502-9503` 这两行扩成 **u、v 各一次**（不新建函数、不改挂接逻辑）。

**前置必修项不变**：F-3（GC 每 256 tick 把非 holder 的账本按 `holder-gone` 释放，而解挂后两半都是带排程真列车 ⇒ 都不是 holder）+ F-4（站台格有车才 `-hold`，plain rail 无"格上有车"守卫 ⇒ 会真把位放掉）。这一对着重就是"占住轨道当障碍"，一旦被 GC 放掉则占位失效，比不补更危险 ⇒ **先改 F-4 的守卫（即 Q-276-1 的 T1 扩展），再补 v**。

---

## 十六、玩家 2026-10-22 第二次澄清：这个函数到底是什么（口径最高优先级）

> 「等一下，我说说这个函数是什么吧，当一列车挂接另一列车之后，新的车头很可能超过出站信号灯（常为PBS），而挂接完成之后，新组成的一列车就被赋予一次自主不依靠信号灯铺预留的能力，随后，在列车铺设了一次预留之后，这个能力就会失效」

### 16.1 据此确立的函数语义（这条覆盖 §15 里"占住轨道当障碍"的解释）

`ReserveTrackUnderConsist()` 是**一次性自主铺预留授权（force-reserve-once）**，不是"持续占位"：

1. **触发场景**：挂接完成后，合并链的**新车头很可能已经越过（顶着）出站信号灯**（多为 PBS 信号）。此时信号系统不会给它授权（灯是红的，或者它站在信号灯后方但已被判"该区间无授权"），列车会永远卡住不走。
2. **授予**：挂接成功的那一刻，给合并链授予一次"不依靠信号灯自行铺预留"的能力。
3. **消费即失效**：它铺出**一次**预留之后，能力自动失效（一次性，不是每 tick 补订）。
4. 因此它天然是"提交点的一锤子买卖"，而不是 `:19728` 那种每 tick 的门 —— 这也解释了为什么它必须写在 `Couple()`/`DecoupleTrain()` 的提交点里。

⇒ 与 `VehicleRailFlag::ForceReserveOnce`（bit27）极可能是同一机制的两侧，需核实（见 16.2）。

### 16.2 已核实（2026-10-22 源码实读）——玩家说的就是 KI-179 的 `ForceReserveOnce`

**结论：玩家描述的正是 `VehicleRailFlag::ForceReserveOnce`（`src\train.h:55`，KI-179）。§15.1 把它解释成 `ReserveTrackUnderConsist()`（占住轨道当障碍）是错的，该解释作废。**

证据链（四处互证）：

| 环节 | 位置 | 原文/行为 |
|---|---|---|
| 位定义 | `src\train.h:55` | `ForceReserveOnce = 27, ///< R3R (KI-179): one-shot "reserve independently of the signals" ability, granted to the merged train when a coupling completes. Consumed by the first successful path reservation (whether or not it needed the red-signal bypass), kept while every reservation attempt fails` |
| 访问器 | `src\train.h:265-269` | `HasForceReserveOnce()` / `SetForceReserveOnce()` / `ClearForceReserveOnce()` |
| **授予点（全文件唯一）** | `src\train_cmd.cpp:12420` | `v->SetForceReserveOnce();` —— 位于 **`Couple()` 提交点**，紧接 `ORD-AFTER-COUPLE` 快照（`:12394-12411`）之后、KI-199 的 couple-autoreserve（`:12439`）之前；注释 `:12413-12419` 逐字即玩家所述："the two halves were reserved separately before the coupling, so the joined train can end up with **its front just past a signal it never reserved**; this lets it make one reservation which ignores that signal" |
| **消费点** | `src\train_cmd.cpp:16000-16012` | `R3ROneShotReserveGuard` 析构里 `if (armed) { if (reserved) { 打 R3R-RES-ONCE used; ClearForceReserveOnce(); } else { 保留 } }` —— 即"铺成功一次就失效，全失败则留着下次再试" |
| **绕过机制** | `src\train_cmd.cpp:16046` | `if (r3r_one_shot.armed && consist->force_proceed == TFP_NONE) consist->force_proceed = TFP_SIGNAL;` —— 这就是"不依靠信号灯" |

**关键结论**：

1. `SetForceReserveOnce()` 在 `Couple()` 里**有**，在 `DecoupleTrain()` 里**一次都没有**（全文件仅 `:12420` 一个 Set）。
2. 玩家方案落地 = **在 `DecoupleTrain()` 提交点给 v、u 各调一次 `SetForceReserveOnce()`**，不新建函数、不改 `Couple()`。
3. **§15.2 / §14.4 里"必须先改 F-4 守卫"的前置条件在此口径下不再成立**：`ForceReserveOnce` 只是一个 flag（随 `Train::flags` 存档），不落 `_r3r_consist_resv` 账本、不经 `R3RGcConsistReservations()`、也不订地图位 ⇒ 没有"被 GC 拆掉"的问题。F-3/F-4 只对 `ReserveTrackUnderConsist()` 那一对成立，属另一条机制（可用，但不是本轮方案）。
4. 仍需注意 KI-219（`:16018-16032`）：绕过标志只应"换来一次预留"，不能在整程里被带着跑；现有 guard 已在 `reserved` 时把 `force_proceed` 复位回 `TFP_NONE`，解挂点新置的 flag 会自然复用这套收尾逻辑，不需要额外改。
5. 需要防的一个新问题：解挂后 u 的排程是 `OT_WAIT_COUPLE`（原地等人挂、不前进；且 `:19728` 平台等待门已经每 tick 给它订 `ReserveTrackUnderConsist`），给它置 `ForceReserveOnce` 会怎样需确认——若它一直不去铺路线，flag 会长期挂着（不是错误，但会让后续挂接时的行为带一个"免红灯"资格，需评估是否该只给 v）。

---

## 十七、玩家 2026-10-22 拍板：`v` 和 `u` 都给（口径定稿）

> 「v和u都给，毕竟你铺设了一次预留这个能力就没了。我现在只是担心连续的R3R行为会不会导致这个能力出什么问题」

### 17.1 定稿口径

- **落地位置**：`DecoupleTrain()` 的 `src\train_cmd.cpp:9502-9503` 那两行旁边，扩成
  `v->SetForceReserveOnce(); u->SetForceReserveOnce();`
- **不新建函数、不改 `Couple()`、不碰 `ReserveTrackUnderConsist()` 那一对**（§16 已改正：那是另一条机制）。
- 玩家对"u 会挂着不消费"的回答是**接受**：能力本来就是一次性，铺一次就没了，挂一会儿不是问题。

### 17.2 玩家提出的新问题：连续 R3R 行为会不会让这个一次性能力出问题

风险清单（R1~R6，逐条待核实/已判）——**结论先行：真正的坑不是"多次置位"，而是 `force_proceed` 与 flag 跨解挂存活这两条**（见 R2/R3），必须在本轮一起处理。

| 编号 | 风险 | 判定 | 处置 |
|---|---|---|---|
| R1 | `SetForceReserveOnce()` 若置在"非链头车"上则完全无效（`HasForceReserveOnce()` 只读链头 flags） | **待核实**：`9502` 之后 `DecoupleTrain` 是否还会改链头（`NormaliseTrainHead` / 车号互换 / 索引重排） | 必须核实落点是否在"链头终值"之后 |
| R2 | `force_proceed` 被置成 `TFP_SIGNAL`（`:16046`）后，若该 tick 预留失败（guard 保留分支 `:16013-16016`），随后被解挂 ⇒ 绕过标志留在解出的半边上，会一路闯红灯（KI-219 现场形态） | **成立**（`DecoupleTrain` 内无任何 `force_proceed` 归一） | 解挂提交点应把两半的 `force_proceed` 归一到 `TFP_NONE`（除非玩家原本强制过 `TFP_STUCK`） |
| R3 | 重复挂/解：`ForceReserveOnce` 位可能残留在"已不再是链头"的车节上，之后某次解挂让它变成链头 ⇒ 带一个来历不明的免红灯资格 | **成立但不致命** | 解挂提交点对两半先 `ClearForceReserveOnce()` 再 `SetForceReserveOnce()`（幂等重置，避免语义漂移） |
| R4 | 同一 tick 内"解挂置位 → TrainLocoHandler 快照 armed"是否会漏 | **不成立**：命令阶段先于 tick 阶段，同 tick 生效 | 无需处理 |
| R5 | 反复挂接（A 挂 B、再挂 C）时，合并链头每次都会被 `Couple():12420` 重新置位，旧位若残留在别处不影响链头读取 | **不成立**（读点唯一在链头） | 无需处理，靠 R3 的幂等重置收口 |
| R6 | u 是 `OT_WAIT_COUPLE`，不前进 ⇒ 不消费 flag；`:19728` 平台等待门每 tick 调 `ReserveTrackUnderConsist()` 但**不经过 `TryPathReserve`**，故不会误把能力消费掉 | **已判**：能力会攒着，等它真开走/被挂上才用 | 玩家已接受，不处理 |

### 17.3 核实结果（2026-10-22 源码实读，两处已定）

**R1 → 落点安全，已定**：`DecoupleTrain()` 在 `src\train_cmd.cpp:9539` 以 `return u;` 结束；`:9502-9503` 之后只剩 `MarkDirty`（:9510-9511）、车库窗口失效（:9517-9518）、`R3RUnpairCoupleTargets`（:9525-9526）、`R3RRecordDecoupleSeam`（:9537），**没有任何链头变更**（`NormaliseTrainHead`/车号互换都在更早处执行完）。⇒ `SetForceReserveOnce()` 直接加在 `:9503` 之后即落在两个链头终值上，R1 排除。

**R2 → 成立，已定（必修）**：`force_proceed = TFP_...` 的写入点全文件只有 7 条（`:4967` 换头/卖头、`:16033-16034` guard 复位、`:16045-16046` guard 置位、`:16430`/`:16453` 到站、`:18769` 离站、`:20560` 手动停车），**`DecoupleTrain()`（9420-9539）区间内 0 条** ⇒ 解挂确实不归一 `force_proceed`，KI-219 形态在这里可复现。

**R3 → 成立，处置已定**：`ClearForceReserveOnce()` 是逐车字段，只清链头清不掉残留位 ⇒ 处置=**先逐节清两条链，再给两个链头置位**（幂等重置），并只在"确有残留位且 `force_proceed==TFP_SIGNAL`"时归零，以免吃掉玩家强制的 `TFP_STUCK`。

**R7（新增，比 R2/R3 更要紧，必须告知玩家）→ 这个能力救不了"解挂后同格卡住"**：
`ForceReserveOnce` 的全部作用就是把 `force_proceed` 置成 `TFP_SIGNAL`（`:16046`），即**只绕过信号（红灯）判据**。而 221 轮已坐实的 KI-330 卡点是另一条路径：`TrainLocoHandler` → `FollowTrainReservation`（`:13947`）→ `pbs.cpp` `CheckTrainsOnTrack` 命中同格另一列车 ⇒ `MarkTrainAsStuck` + `return TPRRF_NONE`，**发生在 `ChooseTrainTrack` 之前，与 `force_proceed` 无关**。221 轮日志证据：解挂后 `TPR-BLOCKED` 8/8 紧跟 `DECOUPLE-DONE` 且 `otherTile == mf`。⇒ **解挂后两半同格是常态，此时置不置 `ForceReserveOnce` 都一样卡**；这一条必须靠 KI-330 的修法（同格豁免 / 解挂分离到不同格）解决，与本轮改动正交。

### 17.4 落地形态（已写入源码）

在 `src\train_cmd.cpp:9503` 之后插入（`u->ReserveTrackUnderConsist();` 之后、`v->MarkDirty();` 之前）：

1. 采样 `v_had_stale` / `u_had_stale`；
2. 沿 `v`、`u` 两条链逐节 `ClearForceReserveOnce()`；
3. 若 `*_had_stale && force_proceed == TFP_SIGNAL` ⇒ 归 `TFP_NONE`；
4. `v->SetForceReserveOnce(); u->SetForceReserveOnce();`

复测判据（待玩家）：①解挂后日志出现 `R3R-RES-ONCE grant head=<v>`/`<u>` 各一次（**注**：当前 grant 探针只写在 `Couple()` 的 `:12421-12428`，解挂处未加 probe，若需成对对照应补一行）；②`R3R-RES-ONCE used` 随之出现（能力被消费）③不再出现"解挂后半边永不动 + `one=1` 长挂"④同格场景若仍 `TPR-BLOCKED`，属 KI-330 未修，不算本改动失败。

---

## 十八、玩家 2026-10-22 提醒：先把 KI-429 的探针加齐再构建

> 「等一下，你不是说还要为另外一个问题加探针吗，请你加好探针再构建吧」

### 18.1 本轮要加的两个探针（都已落地，零行为变更）

| 探针 | 目的 | 落点 |
|---|---|---|
| `R3R-RES-ONCE grant head=… side=v\|u` | 解挂侧的授予**从未有过探针**（`SetForceReserveOnce` 只在 `Couple()` 里带 grant 行），不加就无法把"解挂后不动"区分为「没授予」还是「授予了没消费」 | `DecoupleTrain()` 提交点，紧接 `v/u->SetForceReserveOnce()` 之后，各一行（带 `stale=` 记录本条前置清理是否命中） |
| `RVC-STN veh= tile= kind=track= bits= fb= ok= resNow= whole=` | **KI-429 / U-429-A 的判据**：`265,231` 上等待车脚下那格全日志无 `SET-STN`，要先分辨它是路点还是站台、`ReserveTrackUnderConsist()` 到底订上了什么（站台格走的是**整格位** `HasStationReservation`，不是 per-track 位，所以旧的 `ok`/`resNow` 读数无法区分「订了整格」与「什么都没订」） | `Train::ReserveTrackUnderConsist()` 主分支（`TryReserveRailTrack` 之后），仅对 `IsRailStationTile \|\| IsRailWaypointTile` 的格生效 |

### 18.2 防刷屏与互不遮蔽

- `RVC-STN` 用**独立枚举标签** `R3REDGE_RVCSTN`（插在 `R3REDGE_WALKLOOP` 之后、`R3REDGE_COUNT` 之前），且 key 的 bit63 置 1 与同函数的 `RESERVECONSIST`（tag 相同）彻底隔开，两条探针不会互相把对方的 payload 顶掉。
- payload 把 `kind/bits/fallback/ok/resNow/whole` 全部编进去 ⇒ 只有**状态变化**才出一行；停驻候车不会重演 KI-14 那次「38 秒 28838 行」。

### 18.3 判读口径（复跑后）

1. `RVC-STN … kind=waypoint` 且 `whole=0` ⇒ 路点格**根本没订上整格位**（U-429-A 的"方向是反的"成立：不是等待方预留穿出去，是等待方脚下没人占）。
2. `whole=1` 但仍有车穿过 ⇒ 问题在**释放**侧（谁把它放了），转 KI-345/GC 方向。
3. `kind=station` 与 `kind=waypoint` 两行对比 ⇒ 决定要不要按 §11.3 给 `ReserveTrackUnderConsist()` 补站台/路点专门分支。

### 18.4 构建自证（2026-10-22 18:45，第二轮才成功）

| 判据 | 读数 | 判定 |
|---|---|---|
| `build\R3R_incbuild.done` | `EXIT_CODE=0`，18:45:41 | 通过 |
| 日志尾部 | `[3/3] Linking CXX executable openttd.exe`，`findstr "error C"` 零命中 | 通过 |
| `src\train_cmd.cpp` | 18:38:45 | — |
| `build\CMakeFiles\openttd_lib.dir\src\train_cmd.cpp.obj` | 18:45:35（**晚于源码**） | 通过 |
| `build\openttd.exe` | 18:45:39，52 141 056 B（晚于 obj） | 通过 |
| `read_lints src\train_cmd.cpp` | 0 条 | 通过 |
| exe 字面量 | `RVC-STN veh=` / `kind=%s track=0x%X` / `side=u force_proceed=` / `stale=%d` **4/4 命中** | 通过 |

### 18.5 本轮踩到的坑（务必记住）

**第一轮构建（18:40:03 的 exe）是残缺的**：`EXIT_CODE=0`、`[3/3] Linking`、done 齐全，但 **`train_cmd.cpp.obj` 是 18:37:22，比 `src\train_cmd.cpp`(18:38:45) 还旧** ⇒ 那一轮 ninja 在最后一次编辑落盘之前就扫完了 mtime，只编进了枚举 + `RVC-STN`，**漏掉了解挂侧 grant 探针**。当时的实测就是 `RVC-STN` 命中、`side=u`/`stale=%d` 全部不命中——若只看 done/EXIT_CODE 会直接把这个残次 exe 当成"已构建"交付。

**铁律补充**：判"构建完成"不能只看 `EXIT_CODE=0` + `[n/n] Linking`，必须**逐文件核对 obj mtime > 对应 .cpp mtime**；多处在同一批编辑里时，`obj` 只覆盖到它扫描那一刻的版本。更稳的做法是**编辑全部落盘后间隔几秒再启动构建**，或构建完成后用 `side=`/`stale=` 这类新增字面量做产物自证（本轮的 RVC-STN 与 grant 探针正好互为对照，暴露了这个问题）。

---

## 十九、第 277 轮复测取数（2026-10-22 18:59 日志）

**输入**：`D:\temp\codebuddy-dropped-files\b7be0b1d-49ae-45cd-883d-6ee58ddfe884\R3R_debug.log`
大小 91 959 B / **1118 行** / mtime 2026-10-22 18:59:02，由本轮 18:45:39 的 exe（含 `side=`/`stale=`/`RVC-STN`）产生 ⇒ 采样与本轮改动严格对应。

### 19.1 原始读数（探针命中计数）

| 探针 | 命中 | 判读 |
|---|---|---|
| `R3R-RES-ONCE` | **7** | 见下表逐行 |
| `DECOUPLE-DONE` | 1 | 本会话只解挂一次 |
| `TPR-BLOCKED` | **0** | 本轮未出现 KI-330 同格阻塞形态 |
| `RVC-STN` | **0** | 本次没有候车车底停在站台/路点格上经过 `ReserveTrackUnderConsist()` ⇒ **U-429-A 的 265,231 场景未复现** |

### 19.2 七条 `R3R-RES-ONCE` 逐行（这是本轮改动的直接判据）

| 行 | 原文 | 判读 |
|---|---|---|
| 186 | `grant head=21 force_proceed=1 tile=1889,188` | **挂车侧**（Couple 旧探针，无 `side=`）授予 |
| 257 | `used head=21 force_proceed=1 tile=1889,188` | 挂车侧能力被消费 |
| 456 | `grant head=21 side=v force_proceed=0 tile=267,237 stale=0` | **解挂侧 `v`（留下的机车 21）授予**，`stale=0` ⇒ 无残留位、未触发归一 |
| 457 | `grant head=24 side=u force_proceed=1 tile=273,237 stale=0` | **解挂侧 `u`（解出的车底 24）授予** |
| 473 | `used head=21 force_proceed=2 tile=267,237` | `v` 消费成功（`force_proceed=2`=被临时嵌成 TFP_SIGNAL，正是"绕红灯"生效的形态） |
| 474 | `clear head=21 tile=267,237` | `v` 消费后位被清 |
| 478 | `used head=24 force_proceed=1 tile=273,237` | `u` 也消费成功 ⇒ **两半解挂后都自己铺出了路线，不再有"半边永不动"** |

**结论句**：解挂侧 `SetForceReserveOnce()` 的成对授予**按设计工作**，`v`/`u` 各自在解挂后第一次寻路时消费掉能力（`:473`/`:478`），KI-179 解挂分支的主判据 **通过**。

### 19.3 遗留疑点（登记，待取证）

| 编号 | 疑点 | 期望 / 实测 | 还需什么 |
|---|---|---|---|
| REG-277-A | `u` 授予时就是 `force_proceed=1`（`stale=0` 表示我的归零逻辑没动它），而 `v` 消费时才变成 `2` | 若 `1=TFP_STUCK` 则解出方带着"卡住"标志落地，需确认 `u` 之后的 `used` 是否把它复位 | 枚举 `TFP_NONE/TFP_STUCK/TFP_SIGNAL` 的真实取值 |
| REG-277-B | `clear` 只有 `head=21` 一行，`head=24` 无对应 `clear` | 若 `clear` 探针只写在某条分支，则 `u` 的位可能仍挂着（是否会污染后续挂接） | `clear` 探针的落点条件 |
| REG-277-C | `RVC-STN=0` | 本次候车/解出车底脚下一格不是站台或路点（`u` 在 273,237），故探针未走；不是探针失效（exe 已自证 4/4 命中字面量） | 玩家复现 265,231 那种"路点上等人"场景再取 |

### 19.4 解挂两侧的实际行为（读日志 436~510 行）

| 行 | 原文 | 判读 |
|---|---|---|
| 437 | `DECOUPLE-ADV dec_idx=5 v_real=5 u_real=6` | 解挂点索引推进正常 |
| 438 | `DECOUPLE-DONE u=24 co=0 real=6 tx=273 ty=237` | 解出方 24 落在 273,237，非 car-only（`co=0`）⇒ 命中登记条件 |
| 456/457 | grant `side=v` / `side=u` | 两半各自授予 |
| **473~475** | `used head=21` → `clear head=21` → `TRP veh=21 mf=267,237 ... spd=8 ok=1 res=1` | **机车 21 解挂后同 tick 就铺出路并开动（spd=8）** |
| **478~480** | `used head=24` → `TRP veh=24 mf=273,237 ... ok=1 res=1` → `CRT veh=24 order=1 dir=1 origin=260,237 found=1` | **解出的车底 24 也立刻铺路开动** |
| 500 | `CRT veh=21 ... origin=206,355` | 21 已经开到远处，确认真的走了（不是原地打转） |

**结论句（本轮主判据 4/4 通过）**：①grant 成对 ②两半都出现 `used` ③两半都 `TRP ok=1 res=1` 且真的移动 ④全日志 `TPR-BLOCKED=0`。**"解挂后有一半永不动"未复现。**

### 19.5 新问题（本轮暴露，与解挂修复正交）

日志尾部 tick 3048~3083（第 1080~1118 行）显示：解出的车底 **24 停在 `265,231` 等挂**（`U-ORD 0 type=17` = `OT_WAIT_COUPLE`，`CPL-PAIR ... tgtTile=265,231` 全程不变），机车 **21 回头去挂它**，但：

```
CPL-PAIR tick=3079 act=21 tgt=24 dist=3 actTile=265,234 tgtTile=265,231   ← 最近点
CPL-PAIR tick=3081 act=21 tgt=24 dist=4 actTile=266,234 tgtTile=265,231   ← 又变远
CPL-PAIR tick=3083 act=21 tgt=24 dist=5 actTile=267,234 tgtTile=265,231   ← 继续东行
```

⇒ **21 在距 24 仅 3px 的 265,234 与它擦肩而过、继续向东开走，挂车失败。**

同时寻路侧的证据指向"目标格取错"：

```
CPL-HEAD best=278,231 head=265,231 on=0
PFD-ACCEPT kind=waiting tile=278,231 t=24 ordType=17 wc=1   （重复 3 行）
RESV-WATCH SET-STN tile=265,234 track=255 phase=controller actor=47 head=21
```

| 编号 | 疑点 | 期望 / 实测 |
|---|---|---|
| REG-277-D | 挂接寻路的最佳格是 **278,231**，而等待车真实所在是 **265,231**（`CPL-HEAD ... on=0` 明确标出两者不等） | 期望 `best == head`；实测差 13 格 ⇒ 21 朝 278,231 走，265,231 只是路过 |
| REG-277-E | `PFD-ACCEPT kind=waiting tile=278,231 t=24` 里 278,231 是什么格？（24 的 `dest_tile`？站台合法停车位？） | 若是 `dest_tile` 而非 `u->tile`，则 GOTO_COUPLE 的候选目标从一开始就错了 |
| REG-277-F | `21` 在 265,234 订了**站台整格位**（`SET-STN ... actor=47 head=21`），而 24 在 265,231 —— 这正是 U-429-A 的"等待者脚下无人订"形态在本场景的复现 | 但 `RVC-STN=0`：24 所在 265,231 未被 `ReserveTrackUnderConsist()` 走过 ⇒ 需先确认 265,231 的格类型（是否路点） |

### 19.6 对 19.5 的定性更正（同日，读日志 405~426 行后）

| 读数 | 原文 |
|---|---|
| 405 | `GEO settle-decouple-u n=0 veh=24 tile=273,237 ... starF=1 ... clen=2` |
| 426（截取） | `n=21 veh=48 tile=278,237 ... clen=2` |

⇒ 解出的 `u` 是一条 **22 节**车底（`n=0..21`，链头 `24`@273,237、链尾 `48`@278,237；中间 45~47 是 artic part 不占 n）。

因此：

- `PFD-ACCEPT kind=waiting tile=278,231 t=24` **不是取错格** —— YAPF 扫到站台 `278,231` 时该格上确实压着这条链的**链尾那一段**，`t` 报的是链头对象 `24`；
- `CPL-HEAD best=278,231 head=265,231 on=0` 的含义是"寻路终点（=能贴上去的那一端）在 278,231，而链头在 265,231"，两者不等是**长链的几何必然**，不是 bug。

⇒ **REG-277-D / REG-277-E 撤销**（原判"目标格取错"不成立）。

**尾部 `dist` 先减后增也不是挂车失败**：21 从 `265,234` 继续东行（266,234 → 267,234）去 `278,231` 找链尾，日志在 `tick=3083` 处**截断于途中**（距目标还有 11 格），全程只有 `CPL-PAIR`/`CPL-SKIP`/`CPL-SAFE-FAIL`，**没有 `COUPLE-OK` 也没有 `COUPLE-FAIL`**。⇒ 这一段**不可判**，需继续录到出现 `COUPLE-OK loco=21 rear=…` 或 `COUPLE-FAIL` 为止。

**REG-277-F 保留但改了推断**：`RVC-STN=0` 且 `24` 明确是 `ordType=17`(WAIT_COUPLE) 链头（`PFD-ACCEPT ... ordType=17 wc=1`），说明 `:19728` 平台等待门确实在跑；探针没出声的最可能原因是 **`265,231` 不是 station/waypoint 格**（同日志 `SET-STN` 只出现在 `265,234`，`265,231` 一次没有）⇒ **U-429-A 的"等待者脚下缺整格位"很可能根本不成立**（普通轨道格本来就只有 per-track 位，没有整格位可言）。要坐实需下一次复测时确认该格类型。

**下一步（未做）**：①等玩家把日志录到 `COUPLE-OK`/`COUPLE-FAIL`，判定 21 是否真的挂上；②若仍挂不上，再看 `CPL-SAFE-FAIL`（本次唯一一次在 `tick=3050 tile=258,233 fail=tryReserve`，`FSCP-RES ... oend=257,234` 显示被**自己的预留终点**挡住）与 `FSCP-REACH-NONE`。

## 20 玩家澄清：挂接失败是「预留穿路点」的后果（2026-10-22，定性纠偏）

### 20.1 玩家原话要点

- 前一句：「挂接失败了，挂接的车站台股道都走错了」。
- 本句：「你是在处理那个预留从路点穿过的那个问题吧，而我所说的挂接失败，是这个带来的预留紊乱造成的后果」。

⇒ **不把「21 挂 24 失败 / 21 跑到 265,234 那条股道」当独立问题**，它是 **KI-429（预留从路点/站台格穿过）** 的下游症状。本轮只追 KI-429，挂接失败作为**后果证据**并入 KI-429。

### 20.2 §19.6 的两条结论作废

| §19.6 原结论 | 处置 |
|---|---|
| 「`CPL-HEAD best=278,231 head=265,231` 两者不等是长链的**几何必然**，不是 bug」 | **作废**。24 节车底从 `265,231` 横跨到 `278,231` 占满一条股道，机车从西侧 `252,234` 驶来，运营上唯一合理可贴端是**西端（链头侧）**；把 best 定到东端 278,231 迫使机车沿**紧邻的 265,234 股道**整条东行 —— 这**就是玩家看到的"股道走错"**，且到东端仍未拼上（截断前无 `COUPLE-OK`）。 |
| 「尾部 `dist` 先减后增**不是**挂车失败」 | **作废**。玩家已确认失败；`dist` 3→4→5 且继续东行、全程无 `COUPLE-OK`/`COUPLE-FAIL`，就是失败形态。 |

⇒ **REG-277-D / REG-277-E 恢复**（原判"目标端点/目标格取错"重新成立），不再按"几何必然"放过。

### 20.3 因果链（猜测根因，待验证）

> **【2026-10-22 二次纠正：本节 H-20-1 / H-20-2 / H-20-3 全部作废——主体搞反了，等待区是给 B（机车）设的，不是给 A（车底）设的。正确场景见 §21。以下原文保留供追溯，请勿据此改码。】**

- **H-20-1**：等待车底 `24` 停稳（`order=17` WAIT_COUPLE；KI-208 闸门 `RESV-NOBOOK veh=24 tile=265,231 order=17 caronly=0 reason=parked-waiter`）⇒ 停驶分支不进 `TrainController`（KI-348 口径）⇒ 其脚下 `265,231` **从未订整格位**（全日志 `SET-STN tile=265,231` **0 条**）。
- **H-20-2**：来车 `21` 的寻路/预留因此**看不见** `24` 的占位，可以直接给紧邻股道订下整格位并穿过：`RESV-WATCH SET-STN tile=265,234 track=255 phase=controller actor=47 head=21` —— 这是 KI-429 现象本体（预留穿过去而不是停在等待格）。
- **H-20-3**：两侧叠加 ⇒ YAPF 把 `278,231`（链尾端）当唯一合法落点（`CPL-BEST veh=21 tile=278,231 td=8 cost=16939` / `CPL-HEAD best=278,231 head=265,231 on=0` / `PFD-ACCEPT kind=waiting tile=278,231 t=24`），而路径本身不受占位约束 ⇒ 21 沿 `265,234 → 266,234 → 267,234` 东行绕行 ⇒ **股道走错 → 挂接失败**。

### 20.4 待验证判据

- **V-1**：复跑同场景若出现 `SET-STN tile=265,231`（等待车脚下有位）⇒ H-20-1 证伪。
- **V-2**：若 `CPL-BEST` 落在 `265,231` 链头侧且 21 就地贴上成交 ⇒ H-20-3 成立。
- **V-3**：`RVC-STN veh= tile= booked|skip-<reason>` 探针一旦出声，即可定死路点/站台格走哪个订位 API（U-429-A 结案）。

**行为未改**，仍等 Q-276-3 拍板（见 KI-429）。

## 21 二次纠正：等待区是给 **B（机车）** 用的，不是给 A（车底）（2026-10-22）

### 21.1 玩家原话（逐条）

1.「A应当先到位，等B来挂」
2.「可是A到位之后B才允许从很远的车站执行前去挂车的排程，这浪费时间」
3.「于是玩家在挂接作业车站附近用路点设置了一个等待区」
4.「设想中，B应当预留到等待区的路点就截止预留，一直等到A到位才能把预留延伸到A在的站台」
5.「但是现在实际上是B的预留超过了等待区然后，也不在信号灯前面截止，而是在一个不可能是安全停车位的道岔那里截止了」
6.「而你理解的可能是A使用等待区」

⇒ **本条直接否掉 §20 的全部归因**：§20 把「停稳不订位」的 A 当成穿越主体，方向搞反了。玩家报的 KI-429 原始描述（"符合条件的列车的预留没有在它那一格停止，而是穿过了它"）本来就是 **B 的视角**。

### 21.2 场景重建（正确版）

| 角色 | 身份 | 行为 |
|---|---|---|
| **A** | 车底（被动方） | 先到位，停在站台/交接点等挂 |
| **B** | 机车（主动方） | 从远处车站发车前来挂 A |
| **等待区** | 玩家自建路点（作业站附近） | 语义＝B 到这里停下，**预留到此为止** |

- **期望**：B 的预留 → 止于等待区路点格（且落点必须是**合法安全停车位**，通常路点前后有信号机）；A 到位后才允许把预留延伸进 A 所在站台。
- **实况**：B 的预留 → **越过等待区路点** → **也不在信号机前截断** → 断在一个**道岔（分叉）**上。玩家强调"那是不可能的安全停车位"。

### 21.3 §20 三条假设作废

| 原假设 | 处置 |
|---|---|
| H-20-1「等待车底 24 停稳不订位」 | 事实本身仍成立（`RESV-NOBOOK veh=24` 是 KI-208 设计），但 **A 不是穿越主体**，与本问题无因果关系 ⇒ 剔除出因果链。 |
| H-20-2「来车 21 给紧邻股道订位并穿过」 | **作废**。21 是 B（要找 A 的那一方）；它订整格位是正常寻路，问题在于它**不该铺到这里**。 |
| H-20-3「可贴端被判到东端导致绕行」 | **降级**：可能只是"预留越界"的下游表现，不再是主因主线；挂接失败仍是后果，但归因改挂 KI-429 的"预留越界"。 |

### 21.4 新假设（待验证，先取证再改码）

- **H-21-1（预留终点来源）**：B 接近等待区路点时，`current_order` 已经切到 `GOTO_COUPLE`（`src\train_cmd.cpp:13656` 换单，注释自陈"一进路点格 index 就指向 GOTO_COUPLE"），`UpdateOrderDest` 算出的 `dest_tile` 是 **A 的站台/邻格** ⇒ `TryPathReserve()` 按这个目标铺 ⇒ 预留天然越过等待区。**等待逻辑只回答"现在能不能走"，没有任何"把预留截在路点格"的动作**（此条与 KI-429 已有静态取证一致）。
- **H-21-2（放行口径过松）**：第 272 轮口径"配对锁存在即放行"⇒ A 未到位时 B 也照常全程序预留（对应日志现象"只有 `CPL-PAIR`、无 `CPL-HOLD`"）。
- **H-21-3（截断点没有合法性校验）**：路径在需要截断时退化为"最后一段可铺轨"，而**道岔不是合法停车位**（停上去会同时堵两条线）。如果断点格 `GetTrackBits` 含 ≥2 条轨 ⇒ 说明截断点选择只看了"轨道存在"，没看"能不能合法停车"。
- **H-21-4（可能的实现缺口）**：等待区路点的"停车点"由 R3R 等待逻辑单独决定，而"预留终点"由 pathfinder 按 `dest_tile` 决定，两者**从来没有对齐过**（C-A 的老假设在这一版里重新成立，但要按 B 的链路重新取证）。

### 21.5 取数清单（需要玩家提供 / 日志要查）

**需要玩家给：**
- **P1** B 是哪一列（车号 / 从哪个站发出 / 大致发车时刻）
- **P2** 等待区路点的坐标（或截图，可 OCR 读数）
- **P3** 断裂道岔的坐标（"预留断在那儿"的那格）
- **P4** A 停在哪（站台格坐标）

**日志按 B 的车号查这四样：**
1. `TRP veh=B … type=? dest=?` + `current_order` 类型（是否已 `GOTO_COUPLE`）→ 判 H-21-1
2. B 的 `RP cur=…` 末几格 / `origin=` / `CPL-BEST` / `PFD-ACCEPT` 落点 → 预留实际终点
3. 等待区路点格上有没有 `SET-STN` / `SET-RAIL`（B 连路点格都没订？）→ 判"穿过去"是"没停"还是"停了但订过头"
4. 断点格的 `bits=` 是否含多条轨（真道岔）→ 判 H-21-3

**判据：**
- **V-21-1**：B 的预留终点 == A 的站台/邻格 ⇒ H-21-1 成立。
- **V-21-2**：断点格 `bits` 为多轨（道岔）⇒ H-21-3 成立。
- **V-21-3**：B 的路点格完全无 `SET-STN`/`SET-RAIL` ⇒ "没在路点停"；有订位但继续往后订 ⇒ "停了但没截断"，两种修法不同。

**行为未改、未构建**。等 P1~P4 后按 B 的车号取数，再定修法。

### 21.6 P1~P4 回填（2026-10-22，玩家原话）

> 「前去挂接的B是veh21，等待区路点在（254，231-234），（257-260，231，235）是一片复杂的道岔区，预留在那里断开，而在寻路紊乱之前，A在很远处」

| 编号 | 回填内容 |
|---|---|
| **P1** | **B = veh 21**（前去挂接的机车） |
| **P2** | 等待区路点 = **(254,231) ~ (254,234)**，一列 4 格纵向路点区，x 恒 254 |
| **P3** | 预留断开处 = 道岔区 **(257..260, 231)** 与 **(257..260, 235)** 一带（玩家原话"复杂道岔区"） |
| **P4** | **关键**：寻路紊乱**之前 A 在很远处** ⇒ A 尚未到站台/交接点 |

**P4 的直接作用（排除一项解释）**：既然 A 还没到位，就**排除**「B 因 A 已到位而提前把预留延伸进站台」这条解释。B 的预留越界与截断点选择**就是本问题本体**，不是"提前执行了后续订单"。

**几何关系（并入 21.4 的假设体系）**：B 自西侧驶来（此前日志 `CPL-PAIR` 起点 `actTile=252,234`）⇒ 路径上依次经过 `252,234` →（**等待区 `254,231..234`**）→（**道岔区 `257..260,231/235`**）→ 才到 `265,234` 一带。即 **B 是先穿过等待区、再在道岔区断掉**，与玩家原话顺序逐字一致。

**新增假设 H-21-5（待验）**：B 的路点格在 `x=254`、纵向 4 格，而 B 实际走的是 `y=234` 这一条 ⇒ 若等待区里只有 `254,234` 是 B 该停的那一格，其余 3 格是给别方向用的，则"停在等待区"要求"在正确的那一格按信号机/站台语义截断"。道岔区 `257..260` 才是 B 真正被截断的地方，说明截断是**按"到更远处才断"**发生的 ⇒ 支持 H-21-3（截断点只问"轨道能不能铺"，不问"是不是合法停车位"）。

**下一步取数（按 veh=21）**：`TRP veh=21` 的 `dest`/`order`、`CPL-ENTRY/CPL-ORIGIN/CPL-BEST/CPL-PAIR` 逐行、`RP cur=` 序列是否跨过 `x=254`、是否进入 `257..260`，以及断开那一格的 `bits=`。

### 21.7 取数完成：玩家三点全部命中（`build\R3R_debug.log`，91959 B @2026-10-22 18:58:54）

> 说明：本轮用的是**最新**现场日志 `build\R3R_debug.log`（18:58:54），不是上一轮的 `R3R_debug_276_player.log`（17:45:32）；后者里 `254,23x` 零命中。

**A. 预留确实越过等待区（路点 254,234 只是被路过）**

| 行 | 原文要点 |
|---|---|
| `:1076-1080` | `CPL-PAIR actTile=248,234 → 249,234 → 250,234 → 251,234 → 252,234`（自西向东逼近） |
| `:1081-1082` | `CPL-ENTRY veh=21 tile=252,234 orderType=16` / `CPL-ORIGIN origin=252,234 td=8 vehDir=1` |
| `:1098-1099` | `CPL-PAIR … actTile=254,234` → `CPL-SKIP veh=21 tile=254,234 orderType=16 retryIn=3 holdAge=5`（**正好在等待区路点格上跳过**） |
| `:1100` | `RESV-REL who=CLEAR-STN veh=21 tile=254,234 track=whole owner=21 age=75 phase=controller` ⇒ 该格**曾被 21 自己订过整格位**，此刻被释放 |
| `:1101-1110` | `CPL-PAIR actTile=255,234 → 256,234 → 257,234 → 258,234 → 259,234 → 260,234 → 261,234` ⇒ **穿过等待区继续东行** |

⇒ **结论**：路点格 `254,234` 从来没被当成"终点"，只是被顺手订了整格位又在 75 tick 后释放；预留一路铺过。

**B. 不在信号机前截止，而不是"提前延伸到 A"**

| 行 | 原文要点 |
|---|---|
| `:1083-1085` | `PFD-ACCEPT kind=waiting tile=278,231 t=24 ordType=17 wc=1` ×3 ⇒ 寻路落点是车底的**东端 278,231** |
| `:1087-1089` | `CPL-SCAN … last=278,231 lastveh=24 why=acc-wait` / `CPL-HEAD best=278,231 head=265,231 on=0` / `CPL-BEST veh=21 tile=278,231 td=8 cost=16939` |
| `:1107` | `RESV-WATCH SET-STN tile=265,234 track=255 phase=controller actor=47 head=21` ⇒ 紧邻 A 股道被订下整格位并通过 |

⇒ **结论**：B 的预留目标被定成 `278,231`（远端），不是 `265,231`（近端头），也不是 `254,234`（等待区路点）。**预留越界的第一个原因＝落点取远端**（与 H-20-3 同族，但主体换到 B 自己身上）。

**C. 断点＝道岔格 `258,233`，且断在自己留下的位子上**

| 行 | 原文要点 |
|---|---|
| `:1090-1092` | `CPL-BACK pPrev=278,231 parent=259,232` → `pPrev=259,232 parent=258,233` → `pPrev=258,233 parent=257,234`（由东向西回溯） |
| `:1093` | `FSCP tile=258,233 fail=tryReserve td=11 want=3 type=1 **tbits=0x19 res=0x1** plain=1 depot=0 st=0` ⇒ **三轨道岔格**（0x19 = X｜LOWER｜LEFT），该格**已有一条轨被预留**（res=0x1＝TRACK_X） |
| `:1094` | `FSCP-RES tile=258,233 tk=0 owner=21 otile=252,234 … ord=16 … **oend=257,234 oendtd=8**` ⇒ 持有者是 **21 自己**，而它自己的预留终点是 **257,234** |
| `:1095` | `FSCP-REACH-NONE tile=258,233 (no chain reaches this tile along a contiguous reservation)` ⇒ 沿连续预留**回溯不到**这一格 |
| `:1096` | `CPL-SAFE-FAIL veh=21 tile=258,233` ⇒ 安全位置检查失败，挂接寻路就此作废 |

⇒ **结论**：预留的**实际断点＝`258,233`（三轨道岔格）**，与玩家"在不可能安全停车的道岔那里截止"**逐字吻合**。断点成因有两层，必须分开验：
- **H-21-3 成立（截断点不校验合法停车位）**：断点是"回溯到能铺就停在上一格（257,234）"，没有任何"这里能不能停车"的判断；`257,234`/`258,233` 都在道岔区里。
- **H-21-7（新，待验）**：`258,233` 的 `res=0x1` 归 21 自己，却 `REACH-NONE`（不是沿本链连续预留到达）⇒ 疑似 21 **早前一次预留遗留的孤儿位**（KI-346 双槽格同族），它反过来卡住了自己这次的 `tryReserve`。

**D. A 的位置（更正玩家口述与日志的差异，需确认）**

`:1073-1075` `TRP veh=24 … real=11 type=17 dest=INVALID ok=0 res=0` / `RESV-NOBOOK veh=24 tile=265,231 order=17 caronly=0 reason=parked-waiter` / `DEPOT-ARR veh=24 … tx=265 ty=231 tileEqDest=0` ⇒ 这份日志里 A **已经**停在 `265,231` 处于等待态。玩家说"紊乱之前 A 在很远处"，可能指时间轴更早（B 从远处发车时），与本节 A 已到位不冲突；若玩家指的是**另一份日志/另一次事件**，需另给。

### 21.8 【已被 §21.9 作废】三条因果分叉（原待拍板，玩家已驳回其中两条）

1. **落点取远端**（`:1089 tile=278,231` vs `head=265,231 on=0`）：B 为贴远端而绕过整个道岔区，预留自然铺过等待区。→ 修"可贴端优先机车来向一侧"。
2. **截断不校验合法停车位**（`:1093-1096` 停在道岔）：修"截断点必须是信号机前的安全位，否则拒绝铺"。
3. **孤儿位挡自己**（`:1094 res=0x1 + :1095 REACH-NONE`）：修"预留记账连续性"，与 KI-346/KI-345 同族。

**行为未改、未构建**。等玩家指定先修哪条（1 最快见效；2 是玩家最初提的规则本体；3 需先加只读探针区分"旧残留"与"本次新订"）。

## 21.16 续三：玩家新现场（不加探针）——预留根本没收住 + 两车均 DB 双头

### 21.16.1 玩家原话（2026-10-22，逐字要点）

> 「预留还是没有被截断，而且我确认它是在前车到位甚至进咽喉区之前超出去的。而且，这两个车是处于两头都有引擎的DB状态」

要点：①不是"进路点后不制动"那么温和，而是**预留本身在触发截断之后仍然铺出了路点**；②超出发生在**前车到位之前、甚至前车进咽喉区之前** ⇒ 不可能是"前车停稳后放行"造成的延伸，只能是**旧预留没被收缩**；③两车都是 DB（两头有引擎）状态 ⇒ 判据取端与寻路起点必须按 DB 复核。

### 21.16.2 源码取证（本轮已读，行号为准）

1. `src\vehicle_base.h:482`
   ```cpp
   Vehicle *GetMovingFront() const { return this->IsDrivingBackwards() ? this->Last() : this->First(); }
   ```
   `IsDrivingBackwards()` 是**整车统一**的标志（由链头 flags 决定，见 `ConsistChanged` 的传播）。
   ⇒ DB 双头 + db=1 时，`GetMovingFront()` 返回 **Last()（双头后半，`IsRearDualheaded()` 那节）**，它的 `tile` 通常**不是** `v->tile`。

2. `src\train_cmd.cpp:13151-13155`（截断判据）
   ```cpp
   const Train *wait_front = v->GetMovingFront();
   const bool front_on_wp = (wait_front != nullptr && IsRailWaypointTile(wait_front->tile)
           && GetStationIndex(wait_front->tile) == wait_dest);
   if (!front_on_wp) return 1;
   if (v->cur_speed != 0) return 2;
   ```
   ⇒ 判据用**运动前端**的格，而日志 `CPL-ORIGIN` 打的是 `v->tile`。DB 下二者不是同一节，日志会给出"已经在路点格却仍 state=1"的假象（口径不同，不是数据矛盾）。

3. `src\pathfinder\yapf\yapf_rail.cpp:818`（寻路起点）
   ```cpp
   PBSTileInfo origin = PBSTileInfo(v->tile, v->GetVehicleTrackdir(), false);
   ```
   ⇒ 起点用 **`v->tile`（链头）**，与判据用的运动前端**不同源**。DB 且 db=1 时，起点在链头、判据在链尾，两者最多相差整列车长 ⇒ 截断终点（路点）相对起点可能在"身后"，YAPF 必然找不到路。

4. `src\pathfinder\yapf\yapf_rail.cpp:862-867`（state==2 早退）
   ```cpp
   if (r3r_wait_cut == 2) {
       return INVALID_TRACKDIR;
   }
   ```
   ⇒ **只返回失败，完全不动预留**。

5. `src\pathfinder\yapf\yapf_rail.cpp:887`（无路径早退）
   ```cpp
   if (!path_found) return INVALID_TRACKDIR;
   ```
   ⇒ 同样**不撤销旧预留**。

6. `src\train_cmd.cpp:18825`（引擎释放唯一自然出口）
   ```cpp
   if (v->GetMovingNext() == nullptr && !(v->track & TRACK_BIT_WORMHOLE)) ClearPathReservation(v, v->tile, v->GetVehicleTrackdir(), true);
   ```
   ⇒ 预留是**车尾离开某格时**才按格清；对"车还没走到"的前方格，没有任何"收缩"机制。

### 21.16.3 结论：两个独立缺口（缺一都不会出现玩家现场）

- **缺口 A（主因，与 DB 无关也会发生）**：截断状态下 YAPF 一旦"以起点找不到路点"（起点已在路点格 → `nodes=1 cost=0`；或起点已越过路点；或判据/起点错位导致路点在身后），代码走 `return INVALID_TRACKDIR`，**旧预留原封不动**。列车按旧预留继续行驶 ⇒ 预留看上去"从来没被截断"，而且必然在"前车到位之前"就铺在外面。
- **缺口 B（DB 双头放大/必然化缺口 A）**：判据用 `GetMovingFront()->tile`，寻路起点用 `v->tile` + `GetVehicleTrackdir()`。DB + db=1 时二者分处链的两端：
  - 判据恒为 `front_on_wp == false`（链尾不在路点格，链头在）⇒ state 永远停在 1，**`state==2` 的制动档永远不会进入**；
  - 起点在链头，路点在链尾一侧 ⇒ 截断终点相对起点"在身后"，YAPF 直接失败 ⇒ 命中缺口 A，旧预留永不被收回。

### 21.16.4 修法（本轮落地，最小改动、不加探针）

1. **起点与判据同源**（治 B）：`FindNearestCoupleTrain()` 的 origin 改为运动前端
   `const Train *front = v->GetMovingFront();`，`PBSTileInfo(front->tile, front->GetVehicleTrackdir(), false)`。
   - db=0 时 `GetMovingFront()==First()==v`（链头调用方保证），**逐字不变**；
   - db=1 时起点落在真实车头，截断终点与判据方向一致。
2. **截断态无路可走 ⇒ 主动收预留并停车**（治 A）：新增 `void R3RHoldAtWaitPoint(Train *v)`（train_cmd.cpp，非 static，yapf_rail.cpp 手写前向声明调用）：
   - `FreeTrainTrackReservation(v)` 撤掉本车整条路径预留；
   - `v->cur_speed = 0`（PBS 无前路，下一 tick `TrainCheckIfLineEnds` 亦会拦住；这里直接归零保证本 tick 不再前进）；
   - 打一行 `CPL-WAITHOLD veh=%d tile=%d,%d speed=%d why=%s`（`no-path` / `on-wp`），边沿节流按 (veh,why)。
   - 调用点：`FindNearestCoupleTrain()` 的 `r3r_wait_cut == 2` 早退处（why=on-wp）与 `!path_found` 且 `this->couple_wait_cut`（why=no-path）。
3. **不动**：`R3RShouldCutCoupleAtWaitPoint` 的放行口径（①②③）、非截断路径、`CheckTrainsOnTrack`（desync 冻结区）。

### 21.16.5 复测判据（在 21.14.5 六条之上新增）

7. 截断生效后，若机车起点已越过路点（或就在路点格），日志必须出现 `CPL-WAITHOLD ... why=no-path|on-wp`，且 `actTile` **不再递增**（列车停住）；
8. `CPL-BEST` 之后不再出现"`cost` 正常但 `RESV-REL track=whole` 后列车继续加速"的组合；
9. DB 双头场景：`CPL-ORIGIN origin=` 必须等于该车**运动前端**的格（与 `CPL-WAITCUT tile=` 同源），且 `CPL-WAITCUT state` 能升到 2（证明判据取端已修）；
10. 负判据：无等待区路点时 `CPL-WAITHOLD` 一条都不出现；非 DB 单头车逐字行为不变。

### 21.16.6 未确认（留档）

- U-1：玩家现场"前车进咽喉区之前就超出"的那段预留，是**哪一次**订下的（state=0 时？还是本会话更早的 `TryReservePath`）——需 `CPL-RESERVE` 与 `RP cur=` 的时序对齐，本轮按缺口 A 的通用机制修，不依赖单次归因。
- U-2：`GetVehicleTrackdir()` 在 db=1 时返回反向 trackdir，本修法把它与运动前端配对使用；若 `front->GetVehicleTrackdir()` 与 `front->tile` 在双头后半上不自洽，需再复核（判据 9 可直接暴露）。

> 上面第 1、3 条已被玩家驳回，只有第 2 条留下。见 §21.9。

### 21.9 玩家口径拍板（2026-10-22，唯一根因收敛）

> 玩家原话：「是的，现在这个场景A已经处于等待挂接的状态，在（265，231）。然后，我不知道机车绕道是什么东西，我认为没有绕道，纯粹是预留没有被正确被路点截断。然后，就是那个遗留占位，我认为这只是预留没有被正确被路点截断的副作用」

| 编号 | 拍板内容 | 对我的分析的处置 |
|---|---|---|
| **P-21-1** | **A 已处于等待挂接状态，位于 `(265,231)`** | 关闭上文 D 项"A 位置差异"疑点；`RESV-NOBOOK veh=24 reason=parked-waiter` 与玩家描述一致，不再要求另给日志。 |
| **P-21-2** | **不存在"绕道"**，纯粹是预留没有被路点正确截断 | **撤回 §21.7 B 的"落点取远端＝第一层原因"**。`CPL-BEST tile=278,231` 只是"寻路目标就是 A 车底"的**正常表现**（A 车底占据 `265..278,231` 段，`head=265,231 on=0` 是车底两端点），**不能**解释成 B 故意绕行，也**不是**独立缺陷。 |
| **P-21-3** | `258,233` 的**遗留占位只是副作用** | **撤回 §21.7 C 的 H-21-7**（"孤儿位反卡自己"不作独立根因）。`res=0x1 + FSCP-REACH-NONE` 只是"预留被铺成一长串、到道岔后记账链断裂"的**症状读数**，不需单独修记账。 |

#### 唯一根因（收敛后）

**B 的预留没有被"等待区路点"截断。**

支撑读数（同一件事的不同侧面，不是多个缺陷）：

| 现象 | 读数锚点 | 说明 |
|---|---|---|
| 路过路点不停 | `:1098-1099` `CPL-PAIR actTile=254,234` → `CPL-SKIP veh=21 tile=254,234` | 到了等待区那一格，只被当成一次"跳过" |
| 订了路点格又放掉 | `:1100` `RESV-REL who=CLEAR-STN veh=21 tile=254,234 track=whole owner=21 age=75` | 它**订过**路点整格位，但语义是"路过"而非"停住" |
| 继续往 A 铺 | `:1101-1110` `actTile=255,234 → … → 261,234`；`:1107 SET-STN 265,234` | 越界后一路铺到紧邻 A 的股道 |
| 断在道岔 | `:1093-1096` `FSCP tile=258,233 fail=tryReserve tbits=0x19 res=0x1` → `REACH-NONE` → `CPL-SAFE-FAIL` | 截断点**恰好**落在三轨道岔格，且**没有任何合法停车位校验** |

⇒ 即：**"能不能走"（等待语义）与"预留铺到哪"（寻路终点）是两套逻辑，路点只参与前者，从不参与后者**（H-21-1/H-21-4 成立；H-21-3 保留为"截断点缺少合法停车位校验"这一属性）。

#### 修法骨架（待玩家点头后落地，先不动代码）

1. **识别"这是等待区路点"**：B 的 `current_order` 已是 `GOTO_COUPLE`（`src\train_cmd.cpp:13656` 换单）且配对目标是 A；路径经过的路点格应被视为**预留上限**，而不是普通路径格。
2. **截断点必须是合法安全停车位**：优先取路点格本身；若路点格不可停（道岔/多轨格），取路点前最近的安全停车位（信号机前），**绝不允许**落在 `tbits` 含 ≥2 条轨的道岔上。
3. **截断后禁止向 A 延伸**：直到配对条件成立（A 已停稳在站台）才允许重算一次到 A 的预留。
4. **验证判据**：①B 的 `RP cur=`/`CPL-BEST` 末端止于路点一带，**不出现**对 `265,234`/`278,231` 的预订；②不再出现 `FSCP tile=258,233` / `CPL-SAFE-FAIL`；③A 到位后才出现一次延伸到 `265,231` 的重算；④等待期间 `CPL-PAIR` 仍能正常心跳（配对识别不受影响）。

#### Q-276-4 已拍板（2026-10-22，玩家原话「我拍板，Q-276-4选123全部」）

**放行条件 = ① && ② && ③ 全部同时成立**，缺一不延伸预留：

| 编号 | 条件 | 语义 |
|---|---|---|
| ① | `A->cur_speed == 0 && IsRailStationTile(A->tile)` | A 已实打实停稳在站台上（不是滑行经过、不是停在道岔/正线） |
| ② | `R3RIsCoupleTarget(A)` 为真 | A 仍是合法的等待挂接待配对者（订单未被跳过/未被别人抢挂） |
| ③ | A 车底链**完全停稳在站台区间内**（沿 A 链逐节：`cur_speed==0` 且该节 `tile` 落在同一站台的站台格上） | 防止"头在站台、尾在正线/道岔"的半截状态被当成到位（本场景 A 是 24 号 17 条订单的长车底，链长 14 节，单看链头会误判） |

⇒ 实现上把这三条合成一个判据函数（建议名 `R3RWaitPointReleaseOk(const Train *target)`），**只在放行点调用一次**；不成立就保持路点截断、不重算到 A 的预留。三条全选意味着"宁可多等，也不许提前把预留伸到 A 身上"，与玩家 §21.9 的根因口径一致。

### 21.10 落地设计（代码已读，2026-10-22）

#### 已确认的代码事实（供后续不再重查）

| 位置 | 事实 |
|---|---|
| `src\pathfinder\yapf\yapf_destrail.hpp:417-426` | `PfDetectDestination()` 开头只认站台/车库格，其余**一律 return false**（路点格在这里被直接判掉 ⇒ 路点从来不可能成为寻路终点）。这是"路点不参与预留终点"的**代码级铁证**。 |
| `yapf_destrail.hpp:441-486` | 之后通过 `HasReservedTracks`/`GetTrainForReservation`/站台回扫找等待车底，三道闸门 `R3RCoupleAllowed` / fit / `R3RIsCoupleTarget`。 |
| `src\pathfinder\yapf\yapf_rail.cpp:848-921` | 挂接寻路成功后把落点改成 `consist_head_tile`（车底近端），再 `SetReservationTarget(pNode, res_target_tile, td)`。现场 `CPL-BEST tile=278,231` / `CPL-HEAD head=265,231 on=0` 就是这一段的读数。 |
| `yapf_rail.cpp:928-950` | 回溯路径**逐节点** `CheckSafePositionOnNode()`，任一节点不过即 `CPL-SAFE-FAIL` 并放弃（现场 `:1096`）。 |
| `yapf_rail.cpp:1024-1110` | `rp()` 递归铺预留，用 `tg = consist_head_tile`，路径上遇 depot 即 break；**没有任何路点语义**。 |

#### 修法（选定：在 destination 判定处认路点，改动最小且不动铺路算法）

1. **`yapf_destrail.hpp` 的 `PfDetectDestination()`**：在"只认站台/车库"早退**之前**插入路点分支——
   `if (this->couple_wait_cut && IsRailWaypointTile(tile) && 该格只有一条轨) { accept; return true; }`
   ⇒ YAPF 直接以路点为终点，`best node` 落在路点上 ⇒ 回溯只走路点之前那段、`rp()` 铺到路点即止 ⇒ **自然满足"截断点必须是合法停车位"（路点本身就是停车格），且永远不会落在三轨道岔上**。
2. **`yapf_destrail.hpp` 加一个成员** `bool couple_wait_cut = false;`（默认 false ⇒ 非挂接寻路逐字不变）。
3. **`yapf_rail.cpp` 的 `FindNearestCoupleTrain()` 在 `SetDestination()` 之前**算一次并写进该成员：

   `couple_wait_cut = !B_on_wait_waypoint || !R3RWaitPointReleaseOk(A)`

   其中 `B_on_wait_waypoint = IsRailWaypointTile(v->tile) || IsRailWaypointTile(v->Last()->tile)`；
   `R3RWaitPointReleaseOk(A)` = §21.9 的 ①&&②&&③。
   - B 未到路点 ⇒ 恒截断（先到路点，哪怕 A 已到位）；
   - B 已在路点 + A 未满足 ①②③ ⇒ 继续截断（原地等，origin==dest ⇒ 返回 INVALID_TRACKDIR，车停住）；
   - B 已在路点 + A 满足 ①②③ ⇒ 不截断，直达 A 正常挂接。
4. **A 的取法**：`v->current_order` 为 `OT_GOTO_COUPLE` ⇒ 取目的地 `StationID`，遍历该站台格上的列车，取 `R3RIsCoupleTarget(first)` 的链头；逐条判 ①②③。为免每节点扫站台，**只在 `SetDestination` 之前算一次**（每 tick 每车一次，可接受）。
5. **探针**：新增 `CPL-WAITCUT veh= onwp= rel= cut= wptile=x,y`（每车每状态一行，用边沿闸门），供复测判读。

#### 风险与边界

- 改 `yapf_destrail.hpp` 属头文件改动，护栏可能判 `REMOVED 620 obj` → 全量 692 步（约 20 分钟），预计是唯一代价。
- 只对"路径上真的存在路点"的车生效；无路点线路逐字不变。
- `B_on_wait_waypoint` 只看首/尾两节；长车底若中间节压在路点上则视为"未到"，最坏是多等一次重算，不会死锁（车在路点上时首或尾必在路点格上，因为路点格 ≤ 车长时首尾至少一节覆盖）。
- 不追溯存量：已在半路的 B 在下一次寻路即生效。

**下一步**：读 `yapf_destrail.hpp` 类成员区（200-340）确认可加成员的位置与 `SetDestination` 调用链，然后落码。

### 21.11 落地实现（2026-10-22，已改码 + 已编译；实机复测待做）

§21.10 的骨架有三处必须在落码时收紧，否则会从"预留过长"直接变成"永远挂不上"：

1. **没有等待区路点就绝不截断**（最关键）。§21.10-3 的 `!B_on_wait_waypoint || ...` 会让"线路上根本没有路点"的挂接也进入截断模式，而截断模式的终点只有路点 ⇒ YAPF 连一个终点都找不到 ⇒ 机车永远走不到车底（T8701、库内挂车、站台直挂全部退化）。
   因此截断的**前提**改为：`GOTO_COUPLE` 的前一条真实订单必须是 `OT_GOTO_WAYPOINT`（与 `R3RShouldHoldForCouple` 同一口径，`cur_real_order_index - 1`，索引 0 时回绕到末尾），取该订单的 `GetDestination()` 作为等待区路点。没有这条订单 ⇒ 返回 0（旧行为逐字不变）。
2. **终点必须点名那条路点**。"任一路点都接受"会被最小代价落点选到**身后**的路点（线路上往往不止一个路点），把机车往回引。故 `SetCoupleWaitCut(bool, uint32_t wp)` 的 wp = `DestinationID + 1`（0 = 不限定），`PfDetectDestination()` 里比对 `GetStationIndex(tile).base() + 1`。
3. **放行必须带 latch**。放行那一 tick 之后机车就离开路点格，若每 tick 重判"是否站在路点上"会立刻又截断，而截断终点此时已在身后 ⇒ 可能倒车回路点。用 `static std::unordered_map<uint32_t, VehicleOrderID> _r3r_wait_cut_released`（key = 链头 index，value = 放行时的 `cur_real_order_index`）钉住；订单推进（`cur_real_order_index` 变化）才让它失效并重判。进程内状态、不入档；读档丢失 latch 的后果只是"可能退回路点再放行一次"（自愈），不会卡死。

落点（三个文件，均未碰 `src\*.h` 之外的接口；`_r3r_wait_cut_released` 在 train_cmd.cpp 内）：

- `src\pathfinder\yapf\yapf_destrail.hpp`：新增成员 `bool couple_wait_cut` + `uint32_t couple_wait_cut_wp` + `SetCoupleWaitCut(bool, uint32_t)`；`PfDetectDestination()` 最前面插截断分支（只认点名路点；计入 `accwp` 与 `why=6`=`cut-wp`；一次扫描只打一行 `PFD-ACCEPT kind=waitwp tile=x,y`）。
- `src\pathfinder\yapf\yapf_rail.cpp`：手写前向声明 `int R3RShouldCutCoupleAtWaitPoint(const Train *v, uint32_t *wait_wp);`；`FindNearestCoupleTrain()` 在 `SetDestination()` 之前取值并按 3 态处理 —— `2`（已在路点、未放行）直接 `return INVALID_TRACKDIR`（**不跑 YAPF**：否则每 tick 为一条自己脚下的路点全图搜索一次）；`1` 打开截断；`0` 不截断。探针 `CPL-WAITCUT veh= state= wp= tile= x,y`，边沿触发（值变化才写）。
- `src\train_cmd.cpp`：`R3RShouldCutCoupleAtWaitPoint()`（非 static，跨文件经手写前向声明调用）实现 3 态 + ①②③ 放行判据 + latch。逐节遍历用 `GetNextUnit()`（const 可用）；③ 用 `GetStationIndex()` + `GetRailStationAxis()` 比对，**刻意不用** `IsCompatibleTrainStationTile()`（它含 `IsStationTileBlocked()`，而等待中的车底自己正压着这些格 ⇒ 判定恒假）。

**未做**：实机复测。

#### 21.11.1 构建自证（2026-10-22）

复用工作区既有 `_tmp_inc_build.cmd`（**未新建任何 `.cmd`**）。本轮改了 `src\pathfinder\yapf\yapf_destrail.hpp`，KI-183 护栏判 `REMOVED 620 object file(s) - upgrading this build to a FULL rebuild` ⇒ 走**全量 692 步**（约 40 分钟）。

| 判据 | 实测 |
|---|---|
| 完成标记 | `build\R3R_incbuild.done` = `EXIT_CODE=0` |
| 链接行 | `[692/692] Linking CXX executable openttd.exe`（另有 `[22/693] strgen.exe`、`[23/693] settingsgen.exe`，全日志 Linking 共 3 次） |
| 错误计数 | `error C` / `fatal error` / `FAILED:` / `build stopped` = **0** |
| 源码 → obj | `yapf_destrail.hpp` 19:58 / `yapf_rail.cpp` 19:59 → `yapf_rail.cpp.obj` **20:10**；`train_cmd.cpp` 19:59 → `train_cmd.cpp.obj` **20:22** |
| exe | `build\openttd.exe` **2026-10-22 20:25**，52 166 144 B（晚于全部源码与 obj） |
| lint | 三个改动文件 `read_lints` = **0** 条 |
| exe 字面量 | 3/3 命中：`CPL-WAITCUT veh=%d state=%d wp=%u tile=%d,%d`、`PFD-ACCEPT kind=waitwp tile=%d,%d`、`CPL-SCAN … accwp=%u … why=%s`（含 `cut-wp`） |

**结论**：本轮代码已进入 `build\openttd.exe`，可直接用它复跑 §21 场景；因是 Debug 内测树（探针 ON），发布树 `build-release` **未**编译，仍停在旧版（不含 KI-429 修复）。

### 21.12 第二次实机复测取证（2026-10-22 第 276 轮续，零源码改动）

**输入**：玩家新日志（945 行，20:36），产生者 `build\openttd.exe` 20:25（含 KI-429）。场景同 §21：B=veh 21 执行 `GOTO_COUPLE` 去挂 A=veh 24（A 停在 `265,234` 起、链到 `278,234`），B 的等待区路点 = id 17、格 `254,233`。

#### 21.12.1 截断第一次是**对的**

| 日志行 | 原文 | 判定 |
|---|---|---|
| 727-729 | `CPL-ENTRY veh=21 tile=205,274 orderType=16` / `CPL-WAITCUT veh=21 state=1 wp=17 tile=205,274` | 截断模式打开，点名 wp=17 |
| 730 | `PFD-ACCEPT kind=waitwp tile=254,233` | 终点收在路点格 |
| 732 | `CPL-SCAN … accwp=3 … last=254,233 why=cut-wp` | 只接了路点 |
| 733-737 | `CPL-BEST veh=21 tile=254,233 td=8 cost=22758` → BACK `254,233→237,233→230,234→212,252→205,274` | 回溯止于路点，**没有**穿到 258,233 道岔 |
| 738-739 | `CPL-TRACE st=205,274 tdb=0x202` / `REACH cur=205,274 …` | 正常铺路 |
| 910-911 | `RESV-WATCH SET-RAIL tile=264,234 … actor=21 head=21` / `CPL-RESERVE veh=21 reserved=1 next=8` | 预留止于 A 前格 264,234 |

⇒ 判据②（不再出现 `FSCP tile=258,233` / `CPL-SAFE-FAIL`）在这一段**通过**；KI-429 的截断机制本身有效。

#### 21.12.2 缺陷一：放行提前了一整格（KI-431）

```
835: CPL-PAIR tick=3030 act=21 tgt=24 dist=13 actTile=253,233 tgtTile=265,234
837: CPL-ORIGIN veh=21 origin=253,233 td=8 vehDir=1 vehTd=8
838: CPL-WAITCUT veh=21 state=0 wp=17 tile=253,233   ← 提前放行
839: PFD-ACCEPT kind=waiting tile=278,234 t=24 ordType=17 wc=1
844: CPL-HEAD best=278,234 head=265,234 on=0
845: CPL-BEST veh=21 tile=278,234 td=8 cost=16442
846-849: CPL-BACK 278,234→259,234→258,233→254,233→253,233   ← 又经过道岔格
```

机制（`src\train_cmd.cpp` 的 `R3RShouldCutCoupleAtWaitPoint()`，on_wait_wp 段）：判"是否已到等待区路点"用的是 `for (w = v; w != nullptr; w = w->GetNextUnit())` **逐节**遍历，任意一节 `tile` 落在路点格即为真。veh 21 不是单节机车（本场景链长 >1），其第二节已压在 `254,233`，而**机车本体（链头）还在 `253,233`** ⇒ on_wait_wp 成立 + ①②③（A 停稳/是挂接目标/整链在站台）成立 ⇒ latch 设置、返回 0。

与 §21.10 设计口径不一致：设计只允许"**链头 / 尾节 / 运动车头**"三处，落码时写成了"任意节"。多节链里"任意节"几乎恒成立（列车越长越早），等于把截断提前解除。

后果：放行那一 tick 起 YAPF 的端点恢复成 A 的远端 `278,234`，回溯链重新经过 `258,233` 道岔 —— 玩家看到的"又绕回去"就是这一行 `state=0` 造成的。

#### 21.12.3 缺陷二：`CPL-HEAD` 解析到自己（KI-432，直接导致挂不上）

下一 tick（3032）B 已进路点格 `254,233`：

```
913: CPL-ENTRY veh=21 tile=254,233 orderType=16
919: CPL-SCAN tick=3032 … last=278,234 lastveh=24 why=acc-wait
920: CPL-HEAD best=278,234 head=254,233 on=0     ← head 变成 B 自己脚下
925: CPL-TRACE st=254,233 tdb=0x101
926: REACH cur=254,233 td=0 depth=0 …            ← st == tg，立即成功
927: CPL-TRACE-RESULT st=254,233 best=0
929: CPL-RESERVE veh=21 reserved=1 next=0         ← 预留退化成 1 格
931: RESV-REL who=CLEAR-STN veh=21 tile=254,233 track=whole owner=21 age=57 phase=controller
```

根因在 `src\pathfinder\yapf\yapf_rail.cpp:897-933`：

```cpp
Train *ct = GetTrainForReservation(pNode->GetLastTile(), TrackdirToTrack(pNode->GetLastTrackdir()));
if (ct == nullptr) { for (Train *tr : VehiclesOnTile<...>(pNode->GetLastTile())) if (tr->IsFrontEngine()) { ct = tr; break; } }
if (ct != nullptr) { Train *h = ct->First(); Train *t = ct->Last(); ... }
```

- 上一 tick（3030）放行后，`TryReservePath()` 按 YAPF 节点把路径铺到了 best 节点 `278,234`，该格的账本/预留归 **21 自己**；
- 本 tick 在 `278,234` 解析"等在这里的是谁"时，`GetTrainForReservation()` 先命中自己的预留 ⇒ `ct = 21`；
- `h = ct->First() = 21`（`tile = 254,233`），`t = 21->Last()`（链尾，距离远）⇒ `DistanceManhattan(v->tile, h->tile) == 0` ⇒ 选 `h->tile` ⇒ `consist_head_tile = 254,233`；
- `tg == st` ⇒ `reach()` 第一条就 `true` ⇒ `best_td = 0` ⇒ 只订 1 格，随后整格位被 controller 释放。

**缺的守卫**：`GetCouplePosition()` 里早有 `other_vehicle->First()->index == v->index` 的自我排除，这段新解析（KI-200 起）没有同一守卫，`ct` 也不校验 `ct->First() != v->First()`。这是本轮放行策略改变后被**新暴露**的路径：截断期 best 停在路点、278,234 上没有自己的预留，所以第一次复测不会遇到；一旦放行到远端，下一 tick 必自指。

**结果**：全日志唯一的 `COUPLE-OK` 是更早的 `:120`（另一场景 1889,188）。本次从 3033 到日志结束（tick 3057，`CPL-PAIR dist=1 actTile=265,233 tgtTile=265,234`）**没有** `COUPLE-OK`，即 §21 判据③未达成；机车沿 3030 那次铺出的旧路径开过站台前缘，错过了挂接窗口。

#### 21.12.4 判据判定表（第二次复测）

| 判据 | 结果 | 依据 |
|---|---|---|
| ① 截断期止于路点 | **部分通过** | 3030 前全对（`:730-737`、`:910`）；但 3030 就 `state=0`（`:838`），比"整列停在路点"早一格 |
| ② 不再铺进道岔区 | **截断期通过 / 放行后失败** | 截断期无 `FSCP 258,233`；放行后 `:846-849` BACK 链重新经过 `258,233`（此时属旧行为） |
| ③ 放行后铺到 A 并 `COUPLE-OK` | **未达成** | 3032 起 head 自指 ⇒ 预留 1 格 ⇒ 至 3057 无 `COUPLE-OK` |
| ④ 无反例场景 | 未覆盖 | 本次日志只有这一个挂接场景 |
| ⑤ 无每 tick 全图搜索 | 通过 | `CPL-WAITCUT` 全日志 **2 行**（729/838）；`CPL-SCAN nodes` = 16/21/25 |

#### 21.12.5 修正方向（待拍板，未改码）

- **F-431（收紧放行判据）**：on_wait_wp 从"任意节"收紧为"**链头**或**运动前端**（`GetMovingFront()`/`GetMovingDirection()` 对齐的那一端）落在路点格"。尾节只用来判"整列都进了等待区"，不能单独触发放行。可选更严：还要求 `v->cur_speed == 0`（此刻 B 仍在滑行，`vehDir=1`）。
- **F-432（解析排除自己）**：`ct` 命中后先排 `ct->First() == v->First()`（以及 `ct == v`），命中就丢弃并回落到 `VehiclesOnTile()` 扫描 / 整站台找"非本链"的 `IsFrontEngine` 车；若最终仍找不到外链，则 `consist_head_tile = INVALID_TILE`，让 `res_target_tile` 回落到 `pNode->GetLastTile()`，**不要**用自己脚下当 head（当前会退化成零长度预留）。
- 备选（治标不改判定）：放行那一 tick 不清截断模式，而是把截断目标从"路点格"改成"A 的近端格"，让 YAPF 一步到位、不产生 278,234 这一格的自有预留。
- 复测判据（下一轮）：⑦`CPL-WAITCUT state=0` 出现时 `tile=` 必须是路点格本身（`254,233`），不得是前一格；⑧`CPL-HEAD head=` 不得等于 `CPL-ENTRY tile=` 自身；⑨放行后 `CPL-RESERVE next` 非 0、`REACH` 的 `st != tg`；⑩最终出现 `COUPLE-OK loco=21`。

### 21.13 修正落地（2026-10-22 第 276 轮续二，已改码 + 已编译）

玩家拍板："把到站判定收紧到停稳吧，然后找错人也一起修复"。两条同时落地。

#### 21.13.1 F-431：到站判据改成四档（`src\train_cmd.cpp`）

`R3RShouldCutCoupleAtWaitPoint()` 原来用 `for (w = v; ...; w = w->GetNextUnit())` 逐节扫，**任意一节**压在路点格即算到达。改为：

| 档 | 条件 | 返回 |
|---|---|---|
| 0 | 一节都没压到路点格 | `1`（继续截断，终点＝路点格） |
| 1 | 运动前端已越过路点（前端不在路点格、但链上有节在） | 直接进 ①②③ 判放行，**不再往回截断** |
| 2 | 运动前端就在路点格，但 `v->cur_speed != 0` | `2`（留在路点上等，不跑 YAPF） |
| 3 | 运动前端在路点格且已停稳 | 判 ①②③，全成立返回 `0`（放行） |

- 运动前端取 `GetMovingFront()`（`IsDrivingBackwards() ? Last() : First()`），倒车时与几何一致。
- 档 1 是刻意保留的兜底：列车速度高时可能一两个 tick 就穿过路点格，若继续按"在路点格"判截断，YAPF 终点会落在身后，机车会掉头。前端一旦离格即自动放行（也就自动绕开档 2 的等待）。
- 档 2 返回 `INVALID_TRACKDIR` 时 YAPF 一次都不跑（沿用 KI-429 的第 2 态），不会每 tick 全图搜索。
- 函数头注释同步改写，注明旧口径的现场（链头在 `253,233`、后节已压 `254,233` ⇒ 提前 `state=0`）。

#### 21.13.2 F-432：解析"谁在等"时排除本车（`src\pathfinder\yapf\yapf_rail.cpp`）

`:897` 一带：

```cpp
const VehicleID self_first = v->First()->index;
Train *ct = GetTrainForReservation(pNode->GetLastTile(), TrackdirToTrack(pNode->GetLastTrackdir()));
if (ct != nullptr && ct->First()->index == self_first) ct = nullptr;      // 新增
if (ct == nullptr) {
    for (Train *tr : VehiclesOnTile<VehicleType::Train>(pNode->GetLastTile())) {
        if (tr->IsFrontEngine() && tr->First()->index != self_first) { ct = tr; break; }  // 新增排除
    }
}
```

- 命中自己就丢弃，回落到"该格上真正停着的别的链"；现场 `278,234` 上确有 veh 24（`PFD-ACCEPT kind=waiting tile=278,234 t=24`），所以回落能拿到正确链头。
- 若该格上没有任何外链，`consist_head_tile` 保持 `INVALID_TILE`，下游 `:967`（`res_target_tile`）与 `:1008`（`tg`）自动退回 `pNode->GetLastTile()` —— 不会再出现"耦合端＝自己脚下 ⇒ `st == tg` ⇒ 预留 0 格"。

#### 21.13.3 关于"这两个共同造成了预留没被截断"——要分开说

**不完全对，两件事要分开归因**：

- **"预留没被截断"（铺到 `278,234`、重新经过 `258,233`）的唯一成因是 KI-431。** 只修 KI-431 就能恢复截断；
- **KI-432 不是"没截断"的原因，它是 KI-431 放行的副作用**：一旦放行，`TryReservePath()` 把路径铺到 best 节点 `278,234`，那格的预留**记在自己名下**；下一 tick 才会把自己认成等待车底，于是预留退化成 1 格并被 `CLEAR-STN` 释放。所以 KI-432 是**"没挂上"的直接原因**，且只在"放行后 best 落在自己铺过的格上"时才被触发。
- **但两条必须一起修**：KI-431 的修复只是把放行推迟到停稳；放行之后照样会走到 `278,234` 自指那条路 ⇒ 只修 KI-431，车停稳、放行、挂接还是失败。

#### 21.13.4 构建自证

| 项 | 值 |
|---|---|
| 脚本 | 复用 `_tmp_inc_build.cmd`（**未**新建任何 `.cmd`） |
| 护栏 | `GUARD: incremental is safe (no header/lang file is newer than the newest object)` |
| 链接 | `[4/4] Linking CXX executable openttd.exe` |
| 判决 | `build\R3R_incbuild.done` = `EXIT_CODE=0` |
| obj | `src\train_cmd.cpp` 20:51:09 → `train_cmd.cpp.obj` 20:52:41；`src\pathfinder\yapf\yapf_rail.cpp` 20:51:02 → `yapf_rail.cpp.obj` 20:52:39 |
| exe | `build\openttd.exe` **2026-10-22 20:54:02** |
| lint | 两文件 0 条 |
| 发布树 | `build-release` 未编译，仍为旧版（不含 KI-429 / KI-431 / KI-432） |

#### 21.13.5 待复测（用 20:54 的 exe）

⑦ `CPL-WAITCUT state=0` 的 `tile=` 必须是路点格 `254,233` 本身，不得是前一格；
⑧ 应能看到 `state=2`（运动前端在路点格、还没停稳的那几 tick）；
⑨ `CPL-HEAD head=` 不得等于 `CPL-ENTRY tile=` 自身；
⑩ 放行后 `CPL-RESERVE next` 非 0、`REACH` 的 `st != tg`；
⑪ 最终出现 `COUPLE-OK loco=21`；
⑫ 负判据：没有 `OT_GOTO_WAYPOINT` 前序订单的挂接（`CPL-WAITCUT` 根本不出现）行为逐字不变。

### 21.14 续一版复测失败与修正（2026-10-22 第 276 轮续三）

玩家拖入新日志：`D:\temp\codebuddy-dropped-files\a2996ab4-…\R3R_debug.log`（959 行），报"跟没修之前行为一样"。**结论：玩家判断正确，KI-432 生效、KI-431 没生效。**

#### 21.14.1 现场读数（行号为该日志）

| 行 | 原文要点 | 判读 |
|---|---|---|
| 827 | `TRP veh=24 … tile=265,234` | 等待车底链头在 265,234 |
| 838 | `CPL-WAITCUT veh=21 state=0 wp=17 tile=253,233` | **放行时链头还在路点前一格 253,233** |
| 843 | `CPL-BEST … 278,234` | 放行后立刻重铺到目标车底远端 |
| 844 | `CPL-HEAD best=278,234 head=265,234 on=0` | **KI-432 生效**：head 解析成 265,234（真正的等待车底），不再是自己 |
| 947 | `CPL-TRACE-RESULT st=254,233 best=8` | 起点仍是 254,233 |
| 953 | `CPL-RESERVE next=8` | 订了 8 格 |
| 955 | `RESV-REL who=CLEAR-STN veh=21 tile=254,233 track=whole owner=21` | 整格位随即被释放 |
| 959 | 尾部停在 `258,233`（三轨道岔格） | 与修前同形：卡在道岔上 |

#### 21.14.2 根因：续一版自己加的"越过兜底"档把手改废了

续一版写成：

```cpp
if (!any_on_wp) return 1;
if (front_on_wp && v->cur_speed != 0) return 2;   // ← 只有 front_on_wp 为真时才拦
```

`front_on_wp == false` 时**没有 return 1**，而是继续走 ①②③。现场恰好是"链头 253,233（未到路点）+ 后节已压 254,233（`any_on_wp` 真）"，于是绕过停稳条件直接放行。

**认知错误**：把"链上有节在路点格"当成"运动前端已越过路点"的证据。实际上它有两种完全相反的含义——
- 前端已越过（列车很长，后节还在路点格上）；
- 前端还没到（后节先压线）。
两者在几何上无法区分，因此**绝不能作为放行依据**。

#### 21.14.3 修正（已改码 + 已编译）

```cpp
const Train *wait_front = v->GetMovingFront();
const bool front_on_wp = (wait_front != nullptr && IsRailWaypointTile(wait_front->tile)
        && GetStationIndex(wait_front->tile) == wait_dest);
if (!front_on_wp) return 1;      // 前端不在路点格：一律截断，后节压线不算
if (v->cur_speed != 0) return 2; // 前端在路点格但没停稳：留在路点上等
```

- 删掉 `any_on_wp` 逐节扫描（不再有"任意节"语义）与"越过即放行"档；
- 函数头注释同时记下两代错误判据，避免第三次重犯；
- "已越过路点"不再开后门：截断模式下预留只铺到路点格，机车必然被停到路点格上，越过只可能发生在截断生效那一 tick 之前，属一次性边界。

#### 21.14.4 构建自证

| 项 | 值 |
|---|---|
| 脚本 | 复用 `_tmp_inc_build.cmd`（未新建 .cmd） |
| 链接 | `[3/3] Linking CXX executable openttd.exe` |
| 判决 | `build\R3R_incbuild.done` = `EXIT_CODE=0` |
| 时序 | `src\train_cmd.cpp` 21:01:45 → `train_cmd.cpp.obj` 21:02:28 → `build\openttd.exe` **2026-10-22 21:03:41** |
| lint | 0 条 |

#### 21.14.5 复测判据（用 21:03:41 的 exe）

① `CPL-WAITCUT state=0` 的 `tile=` **必须是路点格 254,233**，不得再是 253,233；
② 应出现若干 `state=2`（前端已在路点格、还在滑行那几 tick）；
③ 放行前不得出现 `CPL-BEST … 278,234`（终点不得提前回到目标车底远端）；
④ `CPL-HEAD head=265,234 on=0` 保持不变（KI-432 不许回退）；
⑤ 最终 `COUPLE-OK loco=21`；
⑥ 负判据：无 `OT_GOTO_WAYPOINT` 前序的挂接行为逐字不变。

### 21.15 续二版复测：截断生效了，但"进路点格后不制动"（2026-10-22 第 276 轮续四）

玩家新日志：`D:\temp\codebuddy-dropped-files\723b2009-…\R3R_debug.log`（77 536 B，21:07:54，晚于 21:03:41 的 exe）。玩家判断："修复看起来生效了，行为变了，但还得修"——**判断正确，而且这次截断确实生效了**。

#### 21.15.1 现场读数

| 行 | 原文要点 | 判读 |
|---|---|---|
| 834/835 | `CPL-PAIR tick=3030 … actTile=253,233` → `CPL-ENTRY tile=253,233` | 机车还在路点前一格 |
| 840 | `CPL-BEST veh=21 tile=254,233 td=8 cost=400` | **截断生效**：终点＝路点格，不再是 278,234 |
| 861/864 | `CPL-TRACE-RESULT st=253,233 best=8` / `CPL-RESERVE next=8` | 预留只到 254,233 |
| 865 | `CPL-PAIR tick=3032 actTile=254,233` | 车头进入路点格 |
| 866/867 | `CPL-ENTRY tile=254,233` / `origin=254,233` | 起点已在路点格 |
| 870 | `CPL-SCAN … closed=0 nodes=1 … why=cut-wp` | 起点即终点，**空路径** |
| 871 | `CPL-BEST veh=21 tile=254,233 cost=0` | 0 成本，等于没有约束 |
| 872 | `RESV-REL who=CLEAR-STN veh=21 tile=254,233 track=whole` | 整格预留随即释放 |
| 873–883 | `actTile` 254→262 递增，`dist` 11→4 | **列车越过路点继续开向 265,234** |

`CPL-WAITCUT` 全文只有 1 行（728，另一趟 `origin=205,274`，`state=1`），866 那次**没有新行**且边沿触发 ⇒ 当时仍是 `state=1`，即 `front_on_wp == false`。

#### 21.15.2 两个待验证的怀疑点

1. **`front_on_wp` 恒为假**：`CPL-ORIGIN` 报 `v->tile = 254,233`（已在路点格），但状态仍是 1 ⇒ `GetMovingFront()` 在这条多节链上取到的节**不是** `v->tile` 那一节。若成立，则 `state=2`（制动档）永远不会进入，判据形同虚设。
2. **`state=1` 在"起点＝路点"时退化成零约束**：YAPF 返回 0 段路径（`nodes=1`、`cost=0`），既没产生新预留、也没撤销旧预留，列车于是照原速穿过路点。

#### 21.15.3 下一步（先取证再改码）

- 读 `Vehicle::GetMovingFront()` / `IsDrivingBackwards()` 的实际定义，确认多节链下它返回哪一节；
- 读 `CPL-RESERVE`（`yapf_rail.cpp:1216` 一带）与调用侧对 `INVALID_TRACKDIR` 的处理，确认"空路径"是否会保留旧预留；
- 把"制动"落到**撤销/收缩本车路径预留**上，而不是只靠 YAPF 终点。
- 修完仍按 21.14.5 的六条判据复测，另增：⑦进入路点格后 `actTile` 不得再递增（必须停住）。

