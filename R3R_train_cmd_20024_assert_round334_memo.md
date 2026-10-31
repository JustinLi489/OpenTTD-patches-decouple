# 第 334 轮：`train_cmd.cpp:20024` 断言崩溃取证（停机）

> 状态：**取证完成第一段（dump 已读）／根因=猜测（待验证）／未改任何源码、未构建**
> 现场 dump：`C:\Users\冯洁敏\Documents\OpenTTD\crash-20261027T105727Z.log`（2026-10-27 18:58:31，14 349 B）

## 一、断言原文（dump 第 11 行逐字）

```
Message:    Assertion failed at line 20024 of D:\sourcecode of JGRPP\src\train_cmd.cpp: chosen_track & (bits | GetReservedTrackbits(gp.new_tile))
```

源码（`src\train_cmd.cpp:20015-20024`，`prev == nullptr` 分支 = 链头/机车自身在进入新格）：

```cpp
TrackBits chosen_track;
if (prev == nullptr) {
    ChooseTrainTrackResult result = ChooseTrainTrack(first, gp.new_tile, enterdir, bits, CTTF_MARK_STUCK | CTTF_NON_LOOKAHEAD);
    assert(IsValidTrack(result.track));
    chosen_track = TrackToTrackBits(result.track);
    reverse_at_signal = (result.ctt_flags & CTTRF_REVERSE_AT_SIGNAL);
    dbg_assert_msg_tile(chosen_track & (bits | GetReservedTrackbits(gp.new_tile)), gp.new_tile, ...);
}
```

语义：`ChooseTrainTrack` 返回的 `result.track` 必须满足 —— **该轨在新格上要么"从进入方向可达"（`bits`），要么"该格已被预留"（`GetReservedTrackbits`）**。断言炸 = 返回的轨两者都不满足。

## 二、dump 读数（全部为原文）

- `tile: 78E (14 x 15), type: 10 (Railway), height: 01, data: 00 0000 00 01 09 00 00 0000, 0x4, 0x9, 0x0`
  ⇒ 崩溃格 `gp.new_tile = 78E = (14,15)`，普通铁轨。
- `Within context`：
  - `TrainController: veh: 59: (Train [N/A], c:0, st:E, vs:D, vf:B, vcf:ld, gvf:, tf:ME, trk: 0x04, [tile: 78F (15 x 15), type: 10 (Railway)…])`
    ⇒ `veh 59`：`tf:ME` = ArticGroup**M**ember + SegmentBack⊗，`trk: 0x04`（`TRACK_UPPER`），位于 `(15,15)`。
  - `front: 57: (Train 7 (HXN5B-XXXX2), st:FE, vs:D, vf:TpBa, vcf:zd, gvf:, tf:qHS, trk: 0x08, [tile: 70F (15 x 14), …])`
    ⇒ 链头 `veh 57`：`tf:qHS` = ArticGroup**H**ead + SegmentFront★，`trk: 0x08`（`TRACK_LOWER`），位于 `(15,14)`。
  - `CallVehicleTicks: veh: 57` ⇒ 本 tick 被 tick 的是链头 57，内部把 `TrainController` 调到 59。
- 栈：`TrainController +2745 (train_cmd.cpp:20024)` ← `TrainLocoHandler +17681 (train_cmd.cpp:22473)` ← `Train::Tick (22620)`。
- 版本：`r3r-pre-ledger-removal-2026-10-25-m (2)`，**`Build date: Oct 27 2026 15:17:42`**，`Defines: DBG_ASSERTS …`。
- 游戏时间 `1920-06-04 (48,0)`，load 于 `1920-05-19`，1119 state ticks 前（10:55:34 载入，10:57:27 崩）。

## 三、读数推出的几何事实

1. 列车向西进入 `(14,15)`：`veh 59` 在 `(15,15)`（`78F`），新格 `(14,15)`（`78E`）在它正西 ⇒ `enterdir = DIR_W`。
2. `veh 59` 是**链中**车（`SegmentBack⊗` + `ArticGroupMember`），`veh 57` 是**链头**（★ + 真引擎 `st:FE`、`Train 7`）。二者不在同一格（`15,15` vs `15,14`）⇒ 这条链此时横跨两格以上，且链头在北。
3. `trk 0x04`（UPPER）与 `trk 0x08`（LOWER）**不是同一个 track**，也不是同一方向族 ⇒ 该链相邻两节车的 `track` 字段不一致（同一格内不可能同时是 UPPER 与 LOWER，但它们在**不同格**，故本身不算矛盾）。

## 四、与第 333 轮崩溃的关系（关键）

| | 第 333 轮（前两份 dump） | 本轮（新 dump） |
|---|---|---|
| 断言点 | `follow_track.hpp:140`（`Follow(old_tile, old_td)` 的 td 不在该格） | `train_cmd.cpp:20024`（`ChooseTrainTrack` 返回的 track 不在 bits/预留内） |
| 崩溃格 | `16,14`(Station) / `15,15`(Railway) | `14,15`(Railway) |
| 车 | `veh 57`(front, ★H) / `veh 59`(ME) | 同一对车 `57` / `59` |
| exe | Build `15:17:42` | Build `15:17:42`（**同一个 exe**） |

⇒ 同一 exe、同一列车、相邻几格内先后触发两条不同的"轨道不自洽"断言。两条断言都只表达同一件事：**R3R 喂给引擎的 `(tile, track/trackdir)` 组合与该格地图形状不符**。

## 五、猜测根因（待验证，未实证）

- **G-1（首选）**：`ChooseTrainTrack` 的返回路径（YAPF `rp()`/`reach()` 或 R3R 的 `CPL-*` 采纳分支）在"终点/首步"处把**相邻格或站台跳格**的 track 当成本格 track 返回。`bits` 只含本格从 `enterdir` 可达的轨位，`GetReservedTrackbits` 只含本格预留位，两者都不含"隔壁格的轨" ⇒ 正好触发该断言。
- **G-2**：YAPF 内部认为"已预留"（节点 key 里有 `reservation` 语义）但地图上本格预留位实际不在（例如前面被 `R3RReleaseChainReservations`/`RESV-GC` 释放，或释放发生在 YAPF 搜索之后、断言之前），于是 `chosen_track` 落在 `bits|reserved` 之外。
- **G-3**：`GetVehicleTrackdir()/track` 与 `direction` 不对齐（第 333 轮 U-6 那条），使 `bits`（`DiagdirReachesTrackdirs(enterdir)` 与 `v->track` 的 90° 限制共同作用，`:20002-20006` 用 `FindFirstTrack(v->track)` 裁轨）被裁得过窄，YAPF 却按完整轨位选轨 ⇒ 选出被裁掉的轨。
  - 该支有额外旁证：崩溃格 `(14,15)` 的 `data` 行给出 `0x4, 0x9, 0x0`（第三行是 `bits/reserved` 打印），但断言宏只打印表达式文本，**三个实参的真实数值没有进 dump**，无法直接判 G-3。

## 六、待验证判据（下一组动作）

1. `build\R3R_debug.log` 在 18:58 前后的 `CPL-*` / `CRT-*` / `RESV-*` 行：读 `CPL-RET … ret=0x…`、`CPL-BEST tile=… td=…`、`CRT veh=… origin=… td=… found=…`，确认返回的 td 属于哪一格。
2. 对 `veh 57/59` 执行 `dump_vehicle`：核 `direction` 与 `trk` 是否自相矛盾（`0x04`/`0x08` 与方向是否匹配），判定 G-3。
3. 读 `ChooseTrainTrack` 包装（`train_cmd.cpp` 内 R3R 版）与 `yapf_rail.cpp` 的返回点，找是否有一条路径能把"非本格 track"塞进 `result.track`，判定 G-1。
4. 判定后再决定修法：优先"返回前自证"（`HasBit(bits, result.track) || HasBit(GetReservedTrackbits(tile), result.track)` 不成立就当作 `found=0` 处理），而不是放宽断言。

## 七、疑点登记

- **U-7**：三份 dump（两次 `follow_track.hpp:140` + 一次 `train_cmd.cpp:20024`）都落在 `veh 57/59` 这一对车上，且都是 `Build 15:17:42`。⇒ 该列车在存档里的 `track`/`direction`/段标记大概率已经处于自相矛盾状态（R3R 段编辑的残留），而不是三条互不相关的 bug。待验证：读档后立即 `dump_vehicle 57` / `59`。
- **U-8**：`veh 59` 的 `c:0` 且 `Train [N/A]`（无名），`veh 57` 是 `Train 7 (HXN5B-XXXX2)` ⇒ 59 是车底/段中车，符合"段头 57 + 段尾 59"的段结构；但 59 带 `SegmentBack⊗` 说明它是段尾 ⇒ 该段只有 57/58/59 三节左右。判据：`GEO settle-*` 的 `segid` 与节数。
- **U-9**：`vf:B`（veh 59）与 `vf:TpBa`（veh 57）的 flags 组合是否合法（`B` 位对应的 flag 待核），若 59 带"倒行"而 57 不带，整链驱动方向可能不一致 ⇒ 与 G-3 同源。

## 八、日志读数（`build\R3R_debug.log` 尾部，崩溃前最后一次 ChooseTrainTrack）

```
CPL-FIRST-TD st=15,14 hops=2 yapf=2 at=15,15 tdb=0x15 ok=1 best=2 start=15,15 trk=2
CPL-TRACE-RESULT st=15,14 best=2
RP cur=15,15 td=2 depth=0 trk=2 bits=0x15
RP cur=14,15 td=0 depth=1 trk=0 bits=0x9
RP cur=13,15 td=0 depth=2 trk=0 bits=0x1
CPL-RESERVE veh=57 reserved=1 next=2
CPL-RET veh=57 tick=126650 tile=14,15 enterdir=0 tracks=0x9 res=0x0 ret=0x4 why=nores-final spd=30 real=1
```
（`tick=` 前一条为 `CPL-RET veh=57 tick=125551 tile=15,14 enterdir=0 tracks=0x9 res=0x0 ret=0x8 why=nores-final spd=5`）

- `tracks=0x9`（`TRACK_BIT_X|TRACK_BIT_LOWER`）= TrainController 传给 `ChooseTrainTrack` 的可达轨位；`res=0x0` = 该格无预留。
- `ret=0x4`（`TRACK_UPPER`）⇒ 与 `tracks|res` 交集为 0 ⇒ 精确对上 `20024` 的表达式。
- `RP cur=14,15 td=0 trk=0 bits=0x9` ⇒ 该格 trackdirbits 只有 `X_NE(bit0)` 与 `Y_SE(bit3)`，**压根没有 UPPER 轨**；而 `RP cur=15,15 … bits=0x15` 含 `UPPER_NE(bit4)`。⇒ `ret=0x4` 是**隔壁格 (15,15) 的轨**。

## 九、根因判定（已定位到赋值点，不再是猜测）

`best_track` 在 `why=nores-final`（`src\train_cmd.cpp:17682`）之前只有四处可赋值：

| 行 | 赋值 | 能否解释现场 |
|---|---|---|
| 17212 | `best_track = track;`（单轨快速路径） | 否：`tracks=0x9` 是双轨 |
| **17443-17445** | **`if (path_found != INVALID_TRACK) best_track = path_found;`**（`DoTrainCouplePathfind(consist, true)` 的返回值） | **是：无任何"属于本格 tracks"校验** |
| 17677 | `if (new_tile == tile && HasTrack(DiagdirReachesTracks(enterdir), next_track)) best_track = next_track;` | 否：`14,15` 上没有 UPPER 轨，`HasTrack` 会挡下 |
| 17723 | lost 分支 | 否：`why` 不是 `lost` |

⇒ **根因 = `train_cmd.cpp:17443-17445`：R3R 把「couple 寻路的首步轨」无条件当作「本格 `ChooseTrainTrack` 的返回值」**。couple 寻路是从机车自身/上一格（`15,15`）出发的，而 `ChooseTrainTrack` 的 `tile` 是 `PrepareVehicleEnteringTile` 之后的新格（`14,15`）——两者不是同一格时，A 格的 `TRACK_UPPER` 就被塞进 B 格的结果里。

这与第 332/333 轮 `follow_track.hpp:140` 的**病根同源**（同一列 `veh 57/59`、同一 exe、相邻几格），只是出口不同：一个把跨格 trackdir 喂进 `Follow`，一个把跨格 track 喂回 `TrainController`。

## 十、修法（待拍板，本轮未落地）

1. **首选（最小、对症）**：17443 处加同格必要条件 —— 只有当 `path_found` 属于本轮 `tile` 的可达集合时才采纳（`HasTrack(tracks | GetReservedTrackbits(tile), path_found)`，与 `20024` 的判据完全同构）；不满足则视作 `INVALID_TRACK`，落回 17446 之后的既有处置（`keep_track = FindFirstTrack(tracks)` / KI-316 撤预留+`MarkTrainAsStuck`）。
2. **治根**：让 `DoTrainCouplePathfind` 明确"返回的是本格首步轨还是 origin 格首步轨"，并在跨格调用时用 `GetLastTile()/GetLastTrackdir()` 与本轮 `tile` 做同格断言（与第 333 轮在 `reach()`/`rp()` 加的两道护栏同款）。
3. 不建议放宽 `20024` 断言：它的语义（返回轨必须可达或已预留）正是本 bug 的判据。

## 十一、待验证

- 用 `build\openttd.exe`（18:42 版）复跑同一存档，看 `CPL-RET … ret=` 是否仍出现"不属于 `tracks`"的值；若 17443 修好后重新出现，说明还有第二处（`DoTrainPathfind` 侧）。
- `DoTrainCouplePathfind` 内部是否使用了 `GetLastTile()`（若它确实从"上一格"取起点，则 17443 的返回值天然跨格，修法 1 可完全兜住）。

## 十二、玩家口径：预留是"边移动边铺"（2026-10-27，与根因的关系）

玩家指出：机车不是一次性把整条预留铺好再走，而是缓慢移动、每跨一格才给自己脚下那一格铺预留。问这与断言是否有关。

**代码实证（`src\train_cmd.cpp:20176-20181`）**：`TrainController` 在 `prev == nullptr`（链头/机车自身进入新格）分支里 `ChooseTrainTrack(first, gp.new_tile, enterdir, bits, …)` 选轨；选完立刻 `TryReserveRailTrack(gp.new_tile, TrackBitsToTrack(chosen_track), false)`（:20181）——**只订"即将进入的这一格"的这一条轨**。下一次跨格再问一次、再订一格。所以玩家描述逐字正确：这是逐格预订，不是整条预订。

因此：

1. **它解释了 `res=0x0`**。断言发生在"选轨刚返回、还没执行 :20181"的瞬间，`gp.new_tile=(14,15)` 此刻确实一位预留都没有；`CPL-RET … res=0x0` 是正常的时序读数，不是"预留被谁偷了"。
2. **它不是根因，也不改变根因判定**。错的不是"格还没铺到"，而是"返回值取的是另一格的轨"：`ret=0x4`(UPPER) 在 `(14,15)` 上根本不存在（该格 trackdirbits 只有 `X_NE`/`Y_SE`，即 `0x9`），即便这一格已经被预留，预留里也只可能是 `X` 或 `LOWER`，断言照样成立。⇒ **不能靠"把预留一次性铺满"来绕过 20024**，那是掩盖而不是修复。
3. **它是"为什么这个 bug 每格都会咬"的放大器**。因为每跨一格就重新调一次 `ChooseTrainTrack`，而 couple 寻路的起点恒为机车所在/上一格（`CPL-FIRST-TD at=15,15 tdb=0x15 best=2`），只要机车处于"车尾还没离开旧格、车头已进新格"的跨格状态（本例列车跨 `15,14`/`15,15`/`14,15` 三格，是常态），返回值就必然跨格 ⇒ 每个跨格 tick 都是一次错配机会。这也解释了为什么同一列 `veh 57/59` 会在相邻几格内连续触发 `follow_track.hpp:140` 与 `train_cmd.cpp:20024` 两条断言。
4. **对修法的含义不变**：修法 1（17443 采纳前做同格校验 `HasTrack(tracks | GetReservedTrackbits(tile), path_found)`，不合格就当 `INVALID_TRACK` 走既有 stuck 处置）与修法 2（`DoTrainCouplePathfind` 明确返回哪一格的轨 + 同格断言）都不受影响；前者正是把"跨格答案"挡在入口，后者是让它不再产生。

**结论**：有关（解释了读数时序、跨格常态、触发频率），但不是根因；根因仍是"跨格轨被当成新格的答案"。

## 十三、归属判定：原生 OTTD 的锅还是 R3R 的地鼠？（2026-10-27）

**判定：R3R 引入的地鼠。** 上游的契约（`ChooseTrainTrack` 必须返回**本格 `tile`** 上可走的轨）从未变过，而且上游自己在两条采纳路径上都做了校验；R3R 新增了一条绕开校验的旁路。

**证据 A —— 出问题的那三行是 R3R 代码**（`src\train_cmd.cpp:17434-17445`）：

```cpp
/* R3R: always reserve the couple path - the loco needs a reservation
 * to move, and it must extend right up to the consist. ... */
Track path_found = DoTrainCouplePathfind(consist, true);
/* R3R: the couple pathfinder starts from the loco itself and always
 * returns a valid departure direction, so use it regardless of where
 * ExtendTrainReservation ended up (res_dest.tile != tile ...) */
if (path_found != INVALID_TRACK) {
    best_track = path_found;
}
```

- 注释自带 `R3R:` 前缀，整段是 R3R 为「让挂在途的机车总有预留」加的；
- `DoTrainCouplePathfind`（`:15927-15930`）只是 `YapfTrainCoupleTrack(v, !do_track_reservation)` 的 R3R 包装，服务 `OT_GOTO_COUPLE` 这一 R3R 自定义订单，原生代码里不存在这条调用链；
- 它的起点是**机车自身**（memo §九/§八：`CPL-ORIGIN origin=58,54`、`CPL-FIRST-TD at=15,15`），不是本函数的 `tile`。注释里那句 "the couple pathfinder starts from the loco itself" 就是自陈。

**证据 B —— 上游两条采纳路径都带同格/可达校验，R3R 这条一条都没带**：

```17672:17677:src/train_cmd.cpp
		TileIndex new_tile = res_dest.tile;

		Track next_track = DoTrainPathfind(consist, new_tile, dest_enterdir, tracks, path_found, do_track_reservation, &res_dest, &final_dest);
		...
		if (new_tile == tile && HasTrack(DiagdirReachesTracks(enterdir), next_track)) best_track = next_track;
```

上游的采纳条件是 `new_tile == tile`（**寻路起点就是本格**）**且** `HasTrack(DiagdirReachesTracks(enterdir), next_track)`（**该轨在本格从进入方向可达**）——正是 `20024` 断言判据的前半。另一条快速路径 `best_track = track`（`:17212` 一带）本来就是本格入参 `tracks` 里的位，天然同格。

⇒ 三条路径对比：上游两条 = 有门；R3R 新增的一条 = 无门、且参照系是另一格。断言不是"新增的严格性"，而是**上游一直有的契约被 R3R 旁路绕过之后，由 `dbg_assert` 把这个违反抓了现行**。

**为什么这类地鼠容易出现（R3R 模式问题）**：R3R 的挂车/解挂/折叠修正大量走"以机车为起点重新寻路"的旁路，而引擎侧 `ChooseTrainTrack` 的入参是"已 PrepareVehicleEnteringTile 的新格"。两套参照系每次跨格就差一格，R3R 又习惯"无条件采纳自己的寻路结果"（因为不采纳车就不动，见注释 "Without this the loco never moves"）⇒ 校验被当成可选。同类先例：第 333 轮 `follow_track.hpp:140`（跨格 trackdir 喂进 `Follow`）也是同一模式。

**修法据此收敛**（与上游同构，不是新发明）：
1. `:17443` 的采纳条件补成 `path_found != INVALID_TRACK && HasTrack(tracks | GetReservedTrackbits(tile), path_found)` —— 等价于上游 `:17677` 的可达判断，与 `20024` 判据同构；
2. 更强的版本是同时要求 `origin.tile == tile`（对应上游 `new_tile == tile`），但因为 couple 寻路本就应该从机车格起步、跨格是常态，用轨位判据比用格判据更贴合意图；
3. 不满足时按 `INVALID_TRACK` 落回 `:17455`/KI-316 的既有处置，不新增行为分支。

## 十四、修法落地（2026-10-27，第 335 轮）

**改动：仅 `src\train_cmd.cpp`（`:17434` 起，新增 46 行，未碰任何 `.h`）。**

```cpp
		Track path_found = DoTrainCouplePathfind(consist, true);
		if (path_found != INVALID_TRACK) {
			const TrackBits r3r_accept_bits = (TrackBits)(tracks | (GetReservedTrackbits(tile) & DiagdirReachesTracks(enterdir)));
			if (HasTrack(r3r_accept_bits, path_found)) {
				best_track = path_found;
			} else {
				... RESV-CPL-XTILE 探针（签名去重）...
				const Track r3r_same_tile = FindFirstTrack(tracks);
				best_track = (r3r_same_tile != INVALID_TRACK) ? r3r_same_tile : best_track;
			}
		}
```

要点与两处**偏离上文修法草案**的修正：

1. **判据与调用方断言逐字同构**：`bits | GetReservedTrackbits(new_tile)`（原 `:20024` 断言）对到本函数就是 `tracks | GetReservedTrackbits(tile)`；另加 `DiagdirReachesTracks(enterdir)` 掩码，与同文件 `:17031/:17074/:17722` 的既有写法一致（`GetReservedTrackbits(tile)` 在本函数入口就已被无条件调用，不新增断言面）。
2. **不合格时不再降级为 `INVALID_TRACK`**（草案第 3 条作废）：那条会落进 KI-316 处置 `FreeTrainTrackReservation` + `MarkTrainAsStuck`。而跨格是每 tick 都会发生的常态（§十二），一旦降级就是 `:17427` 那种"铺→清→铺"抖车。改为退回本格既有可走位 `FindFirstTrack(tracks)`（与 KI-316 失败返回同一口径 `:17660`），`path_found` 仍记为有效 ⇒ 不进失败分支、预留与 Stuck 都不动。
3. **探针标签 `RESV-CPL-XTILE`**：必须落在 `R3RDbgTagAllowed()` 的 `RESV-/UNRES-/EXTEND-/LOOP-/R3R-RES-` 白名单内，否则被静默丢弃（第 289 轮踩过）。

**行号偏移**：本次新增 46 行使后续代码整体后移，原 `train_cmd.cpp:20024`（断言）现为 **`:20064`**，原 `:20181`（逐格 `TryReserveRailTrack`）现为 **`:20221`**。本备忘前文所有 `20024` 引用按语义不变，实际行号见上。

**构建自证（2026-10-27，增量）**：复用既有 `_tmp_inc_build.cmd`（未新建任何 `.cmd`）；`build\R3R_incbuild.guard.log` = `GUARD: incremental is safe (no header/lang file is newer than the newest object)`；日志尾部 `[3/3] Linking CXX executable openttd.exe`（`[2/3]` 为 `openttd_lib.dir\src\train_cmd.cpp.obj`）；`build\R3R_incbuild.done` = `EXIT_CODE=0`；时间戳链 `src\train_cmd.cpp` 19:18:33 → `train_cmd.cpp.obj` 19:19:42 → `build\openttd.exe` 19:21:52（52 322 304 B）；`read_lints` 0 条。
**待实测（3 条）**：①同场景不再出现 `Assertion failed at train_cmd.cpp:20064`（原 20024）；②出现 `RESV-CPL-XTILE veh=… ret=0x… accept=0x…` 且同车同签名不重复刷；③负判据：正常同格挂车 `COUPLE-OK` 照旧、`CPL-RET why=choice-reserved` 频率不变。

## 十五、"为何只铺脚下预留"——初步取证（待续）

**结论句（已可确认的部分）**：`TrainController` 只在**进入新格的瞬间**（`prev == nullptr`）才需要一次选轨，随后只对那一格补订一条轨：

```20057:20064:src/train_cmd.cpp
				if (prev == nullptr) {
					/* Currently the locomotive is active. Determine which one of the
					 * available tracks to choose */
					ChooseTrainTrackResult result = ChooseTrainTrack(first, gp.new_tile, enterdir, bits, CTTF_MARK_STUCK | CTTF_NON_LOOKAHEAD);
					assert(IsValidTrack(result.track));
					chosen_track = TrackToTrackBits(result.track);
```

```20221:20222:src/train_cmd.cpp
						R3RTrackBitsProbe("reserve-chosen", chosen_track, (int)v->index.base(), gp.new_tile);
						TryReserveRailTrack(gp.new_tile, TrackBitsToTrack(chosen_track), false);
```

⇒ **逐格补订是上游设计，不是 bug**：整条 PBS 预留本来就由 `ChooseTrainTrack` 内部的 `ExtendTrainReservation()`（`:17237`）或 R3R 的 `DoTrainCouplePathfind()`（`:17437`）一次铺出，`TryReserveRailTrack(gp.new_tile, …)` 只是"进入这一格时把这条轨补进账本"。

**因此真正的疑点变成一个**：为什么这对挂接机车**没有**走 `ExtendTrainReservation`/`DoTrainCouplePathfind` 的整条铺设，而是始终落到逐格补订？

**当前头号嫌疑（猜测根因，待验证）**：`:17074` 起那条"该格已有本车预留 ⇒ 直接早退"的捷径。它一旦命中就 `return`，`ExtendTrainReservation`（`:17237`）与 `DoTrainCouplePathfind`（`:17437`）**都不会被执行**，于是预留不再向前延伸，只剩 `TrainController` 的逐格补订 —— 与玩家观察完全吻合。

**待验证**：
- 读 `:17074-17236` 确认早退的完整条件与 `why=` 标签，以及 `:17472`/`:17525` 那几条早退是否同类；
- 现场日志里对应 `CPL-RET ... why=` 的取值（`choice-reserved` / `cpl-search-skip` / `cpl-moving-keep` / `cpl-wait-state2`）哪一种占多数，即"只有脚下"发生在哪条出口。

### 15.1 已取证：出口是 `early-haverserved`（2026-10-27）

```17101:17129:src/train_cmd.cpp
	const bool r3r_couple_stuck_retry = consist->current_order.IsType(OT_GOTO_COUPLE) &&
			!consist->current_order.GetCoupleIsDepot() && consist->cur_speed == 0;
	...
	const bool r3r_couple_split_res = consist->current_order.IsType(OT_GOTO_COUPLE) &&
			!consist->current_order.GetCoupleIsDepot() &&
			KillFirstBit(res_tracks) != TRACK_BIT_NONE;
	...
	/* Do we have a suitable reserved track? */
	if (res_tracks != TRACK_BIT_NONE && !r3r_couple_stuck_retry && !r3r_couple_split_res) return r3r_chosen_ret(FindFirstTrack(res_tracks), result_flags, "early-haverserved");
```

**完整机制（三段合成，缺一不可）**：

1. **早退吃掉整条铺设**。`res_tracks` = 本格已有本车预留 ∩ 从 `enterdir` 可达。挂接机车在**单轨格**（`KillFirstBit(res_tracks) == NONE`）且**正在行驶**（`cur_speed != 0` ⇒ `r3r_couple_stuck_retry` 为假）时，`:17129` 直接 `return FindFirstTrack(res_tracks)`，**`ExtendTrainReservation()`（`:17237`）与 `DoTrainCouplePathfind()`（`:17437`）都不执行**，`result_flags` 也不含 `CTTRF_RESERVATION_MADE`。
2. **调用方只补订一格**。回到 `TrainController`，`:20221` 无条件 `TryReserveRailTrack(gp.new_tile, TrackBitsToTrack(chosen_track), false)` —— 只订"即将进入的这一格"。
3. **自我维持**。第 2 步订下的正是"下一格"，于是下个 tick 在第 2 步那个格子上，`res_tracks` 又非空 ⇒ 又命中第 1 步 ⇒ 永远只有**一格**富余。

⇒ **"只铺脚下"不是设计，也不是预留泄漏，而是"已有预留就早退"这条上游捷径与"逐格补订"合成出来的稳定不动点。** 它只在**单轨格 + 行驶中**成立：

- 岔道格（预留两 bit）已被 `r3r_couple_split_res`（KI-504，第 329 轮）放行到 yapf，会铺整条；
- 停住的车（`cur_speed == 0`）已被 `r3r_couple_stuck_retry`（KI-457，第 289 轮续五）放行，会铺整条；
- 非挂接订单照旧走这一条（对它们语义正确：YAPF/PBS 已在外层铺好）。

**这也解释了两个既有现象**：① `RESV-EXT-DEST` 在行驶中的挂接机车上恒为零（`:17237` 之前就 return 了）；② 该格一旦被清空预留（如 §十四 修复前断言崩溃后的 `FreeTrainTrackReservation`），机车立刻从 `early-haverserved` 变成走 yapf 整条铺，日志会突然出现一批 `CPL-ENTRY`/`CPL-BEST` —— 即"预留忽长忽短"的观感来源。

**若要修"只铺脚下"**（本轮未改，等拍板）：最小改法是给 `:17129` 的早退再加一条"挂接机车且 `do_track_reservation` 时不吃捷径"（即 `!consist->current_order.IsType(OT_GOTO_COUPLE) || GetCoupleIsDepot() || do_track_reservation == false` 才 return），代价是每个跨格 tick 都重跑一次 couple 寻路（YAPF 全图搜索，性能与 KI-14 帧率风险需先测）。
