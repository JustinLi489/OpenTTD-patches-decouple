# R3R 第 401 轮：`rail_map.h:40` 断言崩溃 —— 站台格进了"失败探针"的 plain-rail 判定

> 触发时间：2026-11-08（本轮 exe `build\openttd.exe` @03:17:46，含第 400 轮 R-400-1）
> 玩家原话：「rail_map.h 40行断言崩溃了」
> 严重度：**高**（崩游戏，100% 复现于"站台格预订失败"那一刻）

---

## 一、断言是什么

```40:41:src/rail_map.h
	dbg_assert_tile(IsTileType(t, TileType::Railway), t);
	return static_cast<RailTileType>(GB(_m[t].m5, 6, 2));
```

`GetRailTileType(TileIndex t)` 的**入口先决条件**：t 必须 `TileType::Railway`。
`IsPlainRail(t)` 第一句就是 `GetRailTileType(t)`（`rail_map.h:51-55`），所以任何对**非铁路格**调 `IsPlainRail()` 的地方，都会在这里断言，而不是在 `rail_map.h:168` 的 `IsPlainRailTile` 那个更出名的断言处。

站台格的 `TileType` 是 `TileType::Station`（`IsRailStationTile` 为真），**必然**踩这条。

## 二、根因（高置信，由本轮改动直接引入）

第 400 轮 R-400-1 让 `r3r_resv_path` 里**第一次出现站台格**（补压的 `tg`，见 `R3R_couple_station_skip_round400_memo.md` §六）。于是 `R3RReservePathSequence()` 的第二遍循环里，`res_fail_tile` 也可能第一次指向站台格。

而失败分支的探针**没有**先自证 plain rail：

```680:690:src/pathfinder/yapf/yapf_rail.cpp
						const TileIndex ftile = this->res_fail_tile;
						const Track want = TrackdirToTrack(stdir);
						Train *owner = IsRailStationTile(ftile)
								? GetTrainForWholeTileReservationArbitrated(ftile)
								: GetTrainForReservationArbitrated(ftile, want);
						if (owner == nullptr && IsRailStationTile(ftile)) owner = GetTrainForReservationArbitrated(ftile, want);
						const bool selfheld = (owner != nullptr && (owner == coupler || owner->First() == coupler->First()));
						/* 位读数必须先自证 plain rail: GetTrackBits/GetReservedTrackbits
						 * 在非 plain rail 上是 dbg_assert(KI-207c 附记 7b 的教训)。 */
						const bool plain = IsPlainRail(ftile);
```

`:682` 明明已经按 `IsRailStationTile(ftile)` 分了岔（说明作者知道 ftile 可能是站台格），但 `:689` 的 `IsPlainRail(ftile)` 是**无条件求值**——它不是"位读数"，是"位读数的前置"，所以没有被上面那句注释里的守卫覆盖。`ftile` = 站台格 ⇒ `GetRailTileType` ⇒ **`rail_map.h:40` 断言**。

同族第二处（同一颗子弹的另一个枪口，踩的是 `rail_map.h:168`）：

```381:385:src/pathfinder/yapf/yapf_rail.cpp
					fprintf(dbg, "FSCP tile=%d,%d fail=tryReserve td=%d want=%d type=%d tbits=0x%X res=0x%X plain=%d depot=%d st=%d self=%d\n",
						(int)TileX(tile), (int)TileY(tile), (int)td, (int)TrackdirToTrack(td),
						(int)GetTileType(tile), (unsigned)GetTrackBits(tile), (unsigned)GetReservedTrackbits(tile),
						(int)IsPlainRail(tile), (int)IsRailDepotTile(tile), (int)IsRailStationTile(tile),
						(int)self_held);
```

`GetTrackBits` / `GetReservedTrackbits` / `IsPlainRail` 三个都无条件求值，而同一行已经在打印 `st=`——同样知道自己可能是站台格。这个点在订单型寻路（`CheckSafePositionOnNode`）上，历史现场没踩到只是运气。

### 为什么"崩溃"而不是"打一行怪日志"

断言在 `fprintf` **之前**（`:689` 早于 `:690`），所以日志里**不会**留下该条 `CPL-RES-FAIL`；崩溃前最后一条只会是 `CPL-RESERVE` 或更早的 `CPL-*`。
=> **判据**：`build\R3R_debug.log` 末尾若停在 `CPL-RESERVE ...`（或任意 `CPL-*`）且其后无 `CPL-RES-FAIL`，即与本文一致。

### 旁证结论（顺带把 U-400-1 做实）

能走到这条失败分支 ⇒ 补压的站台格 `tg` **确实订不上**。最可能是平台已被等待车底整格预订（`ReserveRailStationPlatform` 首格即失败）。这与第 400 轮备忘 §6.4 预判的"风险点"完全吻合：改动不回归、但也不修好，卡在"目标车底占着站台整格位"这一案。待日志确认。

## 三、修法（R-401-1）

只改探针的**求值守卫**，不改任何行为：

1. `:689` → `const bool plain = IsTileType(ftile, TileType::Railway) && IsPlainRail(ftile);`
2. `:381-385`（FSCP）→ 先算 `const bool plain = IsTileType(tile, TileType::Railway) && IsPlainRail(tile);`，`GetTrackBits`/`GetReservedTrackbits` 改成 `plain ? ... : -1`，`plain=` 打同一个值。

口径：`IsPlainRail()` 的合法前置是"铁路格"，不是"我知道它是站台/车库"。凡是把 `IsPlainRail` 用在**来源不受控**的格子上（`res_fail_tile` 这种由失败点回填的），必须先 `IsTileType(t, TileType::Railway)` 短路。这条要作为长期纪律写进 KI（与 KI-207c 附记 7b 是同一条纪律的两个断言面）。

**不做**：不给 `GetRailTileType` 加兜底（那是全引擎的地基断言，改了会掩盖真 bug）。

## 四、复测判据

1. 同场景不再崩溃；日志出现 `CPL-RES-FAIL ... type=5 st=1 plain=0 tbits=-1 res=-1`（`TileType::Station`=5）。
2. `CPL-RES-PARTIAL` 随后出现（`booked>0`），机车停在平台前一格，不越权。
3. 负判据：非站台线路的 `CPL-RES-FAIL` 逐字不变（`plain=1`、tbits/res 照旧）。
4. 负判据：`FSCP` 在普通铁路上逐字不变。
5. 不再出现任何 `rail_map.h:40` / `rail_map.h:168` 断言。

## 五、构建自证

- 入口复用既有 `R3R_fullbuild.cmd inc detach`（未新建任何脚本）；构建前 `tasklist` 确认 `openttd.exe` 未运行。
- 护栏：`build\R3R_incbuild.guard.log` = `GUARD: incremental is safe (no header/lang file is newer than the newest object)` ⇒ 合法增量（本轮只改 `.cpp`）。
- 日志：`[2/3] Building CXX object ...\yapf_rail.cpp.obj` → `[3/3] Linking CXX executable openttd.exe`；`build\R3R_incbuild.done` = `EXIT_CODE=0`（2026/11/08 14:03:52）。
- 时间戳：`src\pathfinder\yapf\yapf_rail.cpp` 14:00:35 → `yapf_rail.cpp.obj` 14:02:24（8 964 340 B）→ `build\openttd.exe` 14:03:45（52 488 192 B）。
- `read_lints`：该文件 0 条。
- 本轮未新增日志字面量（只改字段求值方式），故不做 exe 串核对。
