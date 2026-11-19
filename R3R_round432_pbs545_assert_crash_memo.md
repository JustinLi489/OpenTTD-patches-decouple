# 第 432 轮：pbs.cpp:545 断言崩溃（挂车寻路平台预留错轨）

**性质**：**R3R 自身回归，但非第 431 轮引入**；引入点 = 第 402 轮 KI-571（站台预留从"整格位"改为"逐格 track"）。
**状态**：根因已做实（2026-11-13）；等待玩家批准后改码。

---

## 一、现场（`crash-20261113T023908Z.log`，20 775 B）

```
Crash at: 2026-11-13 02:39:08 (UTC)
Version:     r3r-stable-2026-11-13-m (2)      ← "-m" = 内测树（探针版）
Build date:  Nov 13 2026 03:34:18             ← 即第 431 轮空指针修复后编出的 build\openttd.exe
Defines: ... DBG_ASSERTS ... WITH_ASSERT ...
Assertion failed at line 545 of src\pbs.cpp:
  (TrackdirBitsToTrackBits(GetTileTrackdirBits(tile, TRANSPORT_RAIL, 0)) & TrackToTrackBits(track)) != 0
tile: FE6B (107 x 127), type: 50 (Station), height: 02, data: 60 0000 50 00 01 86 00 0000, 2, 5, 20
CallVehicleTicks: veh 0 (Train, st:FE, trk: 0x80, tile EC6A (106x118) Railway)
```

栈（自下往上，只留 R3R 相干帧）：

```
TrainLocoHandler                 train_cmd.cpp:23144
R3REnsureCouplePair              train_cmd.cpp:14415
TryPathReserveWithResultFlags    train_cmd.cpp:19110
R3RTryPathReserveCore            train_cmd.cpp:19695
ChooseTrainTrack                 train_cmd.cpp:17984
DoTrainCouplePathfind            train_cmd.cpp:16363
YapfTrainCoupleTrack             yapf_rail.cpp:2773
stFindNearestCoupleTrain         yapf_rail.cpp:1534
FindNearestCoupleTrain           yapf_rail.cpp:2001
R3RReservePathSequence           yapf_rail.cpp:771
ReserveSingleTrack               yapf_rail.cpp:650
ReserveRailStationPlatform       yapf_rail.cpp:624   ← 这里传了本格不存在的 track
TryReserveRailTrack              pbs.cpp:544        ← 断言在此
```

崩溃点是「**对 107,127 这个站台格，预留了一条该格 trackdir bits 里并不存在的轨道**」。断言本身是防御性的（`pbs.cpp:544`），真正出错的是调用方 `ReserveRailStationPlatform`。

## 二、与第 431 轮非段检测的关系（初步结论）

**无关**。第 431 轮只改了一处：

```cpp
/* src\train_cmd.cpp R3RChainAllWagonsSegmented */
v = (n != nullptr) ? Train::From(n) : nullptr;   // 原来 Train::From(v->Next()) 会崩
```

这是纯遍历判断（`R3RSkipCoupleOrdersForRealArtic` 的闸门），**不写地图、不预留、不碰 track**，且崩溃栈里根本没有该函数。⇒ 第 431 轮不是本次崩溃的成因。

**真正的成因（已定位到代码）**：`ReserveRailStationPlatform`（`yapf_rail.cpp:606-634`）沿站台**整段**逐格预留，但**整段共用同一个 `td`**：

```cpp
bool ReserveRailStationPlatform(TileIndex &tile, DiagDirection dir, Trackdir td)
{
    do {
        if (!TryReserveRailTrack(tile, TrackdirToTrack(td), false)) return false;   // ← 崩溃点 624
        tile = TileAdd(tile, diff);
    } while (IsCompatibleTrainStationTile(tile, start) && tile != this->origin_tile);
}
```

`td` 是**进入站台那一格**的 trackdir，`TrackdirToTrack(td)` 由它算出的轨道被**套用到整段平台的每一格**。而 `IsCompatibleTrainStationTile()` 只保证「同站、同轴、轨型兼容」，**不保证每一格都有那条轨道**（站台两端接不同 track 的弯道时，中间格/末端的实际轨道可以是另一条）。走到那种格就必然触发 `pbs.cpp:544` 的断言。

该函数本体的最近一次改动是**第 402 轮（KI-571）**：

```cpp
/* R3R (第 402 轮, KI-571): 原来这里是硬判 `HasStationReservation(tile)`
 * ⇒ 只要整格位被任何人占着就整段失败。 ... 改走同一个入口后：空格照旧订上；
 * 伙伴/自己的位当空气继续推进 ... */
```

⇒ **老代码走不到断言**：硬判 `HasStationReservation(tile)` 一遇到被占的格就整段 `return false`，循环提前结束；改成逐格 `TryReserveRailTrack` 后，循环会把**每一格**都走完并逐格断言「这格有这条轨」，于是「整段共用入口 track」这个一直存在的隐含假设第一次被真的检查出来。

**归属**：是 R3R 自己的回归，**但不是第 431 轮**（非段检测）引起的，是**第 402 轮 KI-571 的站台整段预留改动**把这条路径放开后才暴露的。「之前都没有」= 第 402 轮之前玩家没有跑到「挂车寻路选中穿过站台、且站台某格没有入口那条轨」的组合。

### 2.1 git 实证（2026-11-13）

```
git show b0122745b7^:src/pathfinder/yapf/yapf_rail.cpp
  bool ReserveRailStationPlatform(TileIndex &tile, DiagDirection dir)   ← 老签名：无 td
  {
      do {
          if (HasStationReservation(tile)) return false;
          SetRailStationReservation(tile, true);                        ← 整格位，与 track 无关
          ...
      } while (IsCompatibleTrainStationTile(tile, start) && tile != this->origin_tile);
```

`b0122745b7` = "R3R: rounds 392-406 checkpoint 2026-11-08（couple station skip …）"。
⇒ 第 402 轮之前站台预留是**整格位**、签名里**根本没有 trackdir**，结构上不可能触发 `pbs.cpp:544`。
⇒ 「整段共用入口 track」这个错误假设是第 402 轮**新造**的，不是老代码里的遗留。

### 2.2 决定修法成本的关键事实

`pbs.cpp` 的 `TileType::Station` 分支（`pbs.cpp:602-620`）：

```cpp
case TileType::Station:
    if (HasStationRail(tile) && !HasStationReservation(tile)) {
        SetRailStationReservation(tile, true);   // 整格位，完全没用 track
        ...
        return true;
    }
    if (R3RReservationHeldByCouplePartner(tile)) { ... return true; }   // 空气
```

`track` 参数在 Station 分支**一次都没被读**，只被 544 行断言检查。
⇒ 那个 `TrackdirToTrack(td)` **纯粹是为了喂断言而塞进来的错误值**；改成「该格真实存在的轨道」对站台预留语义**零影响**，不改变任何 map 状态。

## 三、疑点登记

- **疑点 A（已解）**：`ReserveRailStationPlatform` 不是新函数，其行为变更点是第 402 轮 KI-571（`HasStationReservation` 硬判 → 逐格 `TryReserveRailTrack`），该轮注释即写在本函数内（`yapf_rail.cpp:612-623`）。已由 git 复核签名差异（见 §2.1）。
- **疑点 A2（待验证，不影响修法）**：崩溃格 `107,127` 是否真的是「站台整段的非入口格」、以及该格实际有哪些轨道 —— 需在同存档上用 `R3R_DBG=1` 复现并看 `TrackdirToTrack(td)` 与该格 trackdir bits 的差异。修法按「读该格实际轨道」实施后，无论该格是哪条轨都不会再断言。
- **疑点 B（待验证）**：`R3REnsureCouplePair`（train_cmd.cpp:14415）由哪一轮引入、是否本轮新增调用频率。崩溃栈里它是源头帧。
- **疑点 C**：`trk: 0x80`（TRACK_BIT_DEPOT）出现在 veh 0 的 context 里，说明该车当时处于/刚离开车库态；107,127 与 106,118 相距较远，需确认这两者关系。
- **疑点 D**：断言栈里没有任何「预留记账/GC」帧（`R3RReleaseReservationSafe` / `R3RGcConsistReservations`），所以不是第 110 轮那类陈旧预留清理误伤。

## 四、修法（已实施，2026-11-13）

玩家 2026-11-13 回「按你推荐的来改吧」⇒ 治根 + 治标一并落地，**只改 `src\pathfinder\yapf\yapf_rail.cpp` 一个 .cpp**（未碰任何 `src\*.h`）。

1. **治根（零语义影响）**：`ReserveRailStationPlatform` 的 track 不再用入口格的 `TrackdirToTrack(td)`，改为**该格实际存在的轨道**（`yapf_rail.cpp:624-651`）：

   ```cpp
   const TrackBits plat_tracks = TrackdirBitsToTrackBits(GetTileTrackdirBits(tile, ::TransportType::TRANSPORT_RAIL, 0));
   if (plat_tracks == TRACK_BIT_NONE) return false;   // 站台格理论必有轨，防御性
   const Track plat_track = FindFirstTrack(plat_tracks);
   ...
   if (!TryReserveRailTrack(tile, plat_track, false)) return false;
   ```

   因为 Station 分支不读 `track`（见 §2.2），这只是让 545 行的断言拿到一个必然成立的值；`td` 形参保留，仅用于探针比较。
2. **治标（已叠加）**：新增只读探针 `RESV-PLAT-TRACK tile=x,y entry_track=N actual_track=M`，只在「入口 track ≠ 该格实际 track」时打一行（走本文件既有的 `R3RFopenDbg("a")`，探针关闭时恒不打开文件）。用途=给疑点 A2 取数 + 统计这种站台有多大面积，为将来判断要不要改成逐格独立 track 语义留数据。
3. **刻意不做**：不退回 `HasStationReservation` 硬判（会复活第 402 轮 KI-571 修掉的「伙伴占位导致整段订不上」问题）；不改 `pbs.cpp:545` 的断言强度（它现在真的能抓到越权预留，应当保留）。

## 五、复测判据（草案）

1. 同存档同位置不再触发 `pbs.cpp:545` 断言。
2. 挂车寻路的站台预留仍能覆盖平台（不出现「该订没订」→ `TPR-BLOCKED` / 预留成功但车不走）。
3. 普通（非挂车）寻路的站台预留逐字不变。
4. 负判据：真正对该格无该轨的调用仍被断言/探针拦下（不改防御强度）。

## 六、构建与复测

> 2026-11-13 第一段：按玩家"先跟我说一下"的要求**只做定性、未改码、未编译**。
> 2026-11-13 第二段：玩家回「按你推荐的来改吧」⇒ 已改码 + 内测增量编译（见下）。

- [x] 定位 `ReserveRailStationPlatform` 引入轮次（疑点 A）→ 第 402 轮 KI-571（git `b0122745b7`，签名新增 `td`；改前为整格位、与 track 无关）
- [x] 修法定稿 + 改码（`yapf_rail.cpp` §四）
- [ ] 定位 `R3REnsureCouplePair` 引入轮次（疑点 B）
- [x] 编译自证（见 §七）：EXIT_CODE=0、[3/3] Linking、exe 10:48:32、errcount=0、exe 命中 `RESV-PLAT-TRACK`
- [ ] 游戏内复测

## 七、构建自证（已回填，2026-11-13）

- 改文件：`src\pathfinder\yapf\yapf_rail.cpp`（唯一；未碰 `src\*.h`）
- `read_lints`：0 条
- 编译入口：`R3R_fullbuild.cmd inc detach`（复用既有入口，未新建任何 .cmd）；编译前 `tasklist` 确认 `openttd.exe` 未运行
- 护栏：增量（仅 1 个 .cpp，无头/语言文件更新）
- `[3/3] Linking CXX executable openttd.exe`
- `build\R3R_incbuild.done` = `EXIT_CODE=0`（2026-11-13 10:48:39）
- 错误计数：`error C* / fatal error / FAILED: / build stopped` = **0**
- 时间戳链：`src\pathfinder\yapf\yapf_rail.cpp` 10:45:28 → `yapf_rail.cpp.obj` 10:46:41 → `build\openttd.exe` **10:48:32（52 867 072 B）**
- 产物自证：exe 字节流命中 `RESV-PLAT-TRACK`
- 状态：**已修 + 已编译通过，游戏内复测待做**
