# 第 508 轮：`station_map.h:31` 断言崩溃 —— 根因确证 + 修复

日期：2026-11-25
玩家一句报告：**「station_map.h 31行断言崩溃了」**

---

## 一、断言含义（确证）

`src\station_map.h:29-33`：

```cpp
inline StationID GetStationIndex(TileIndex t)
{
	dbg_assert_tile(IsTileType(t, TileType::Station), t);   // ← 第 31 行
	return static_cast<StationID>(_m[t].m2);
}
```

唯一含义：**对非车站格调了 `GetStationIndex()`**。

## 二、现场定位（日志锚点）

`build\R3R_debug.log`（末次写入 2026-11-25 21:29:13，产生者 = 21:21:30 那个 exe，即第 504/507 轮改动版）尾部：

```
PFX-GEO t=1436,1874 rail=0 rtype=255 tb=0x0 ... stn=1
PFX-GEO t=1437,1874 ... stn=1
PFX-GEO t=1438,1874 ... stn=0     ← 站台末格外侧
PFX-GEO t=1439,1874 ... stn=0     ← 会合点落在这里（普通轨道）
...
PFD-MEET kind=car-only tile=1439,1874 t=18 n=1
PFD-MEET kind=car-only tile=1439,1874 t=18 n=2
SEGSAVE-PARK ...
```

⇒ 崩溃时刻正在做挂接寻路，**会合点 = `1439,1874`，该格 `IsRailStationTile()==0`（普通轨道）**。

## 三、根因（确证，非猜测）

`src\pathfinder\yapf\yapf_costrail.hpp` 第 504 轮新增的"会合点强制断段"（原 `:888-891`）：

```cpp
if (Yapf().R3RMeetActive() && Yapf().PfDetectDestination(cur.tile, cur.td)) {
    end_segment_reason.Set(EndSegmentReason::Station);   // ← 借用 Station 位
    break;
}
```

`EndSegmentReason::Station` 是**已被 upstream 占用的语义位**，同一个函数里有两处消费：

1. `:903` `end_segment_reason.Any(ESRF_POSSIBLE_TARGET)` → `:905 PfDetectDestination(cur.tile, cur.td)` →
   `target_seen = true`（**这是我们想要的：会合点算"找到目标"**）。
2. `:943-951` `Station platform-length penalty` 分支：

```cpp
if (end_segment_reason.Test(EndSegmentReason::Station)) {
    const BaseStation *st = BaseStation::GetByTile(n.GetLastTile());   // ← 944
    dbg_assert(st != nullptr);
    uint platform_length = st->GetPlatformLength(...);
```

`BaseStation::GetByTile(tile)` 内部就是 `BaseStation::Get(GetStationIndex(tile))`。
会合点 `1439,1874` 不是车站格 ⇒ **`station_map.h:31` 断言**。

即：**"会合点断段"复用了 Station 枚举位，把非站台的段末喂给了只接受站台格的站台长度惩罚。**
崩溃与"会合点落在站台外一格"这一新语义天然共存 —— 会合点越到站台外，就越必崩。

## 四、修复（已落地）

`src\pathfinder\yapf\yapf_costrail.hpp:943`：站台长度惩罚增加格类型守卫

```cpp
if (end_segment_reason.Test(EndSegmentReason::Station) && IsRailStationTile(n.GetLastTile())) {
```

- 真站台到达：行为与 upstream 逐字相同（`IsRailStationTile` 恒真）。
- R3R 会合点（非站台格）：跳过站台长度惩罚，但 `EndSegmentReason::Station` 位保留 ⇒
  `:903/:905` 仍判 `target_seen = true`，会合点作为终点依然成立（第 504 轮功能不受影响）。
- 未改 `ESRF_*` 掩码、未新增枚举、未碰 `PfDetectDestination`。

## 五、复核过的不需要改的地方

- `yapf_costrail.hpp:711` 的 `cur.tile_type == TileType::Station && IsRailWaypoint(cur.tile)` —— 有守卫。
- `yapf_destrail.hpp` 内 9 处 `GetStationIndex` / `GetRailStationAxis` —— 全部前置 `IsRailStationTile`。
- `yapf_costrail.hpp:757 follower->is_station` 的站台惩罚 —— 由跟随器的 `HasStationTileRail(new_tile)` 置位，
  与 `new_tile` 同源，不会指向非站台格。
- `train_cmd.cpp:17096 ft.is_station && GetStationIndex(ft.new_tile)` —— 同上，同源。
- 陈旧 obj 假设已排除：`follow_track.hpp`(11-25 18:04) 早于 `train_cmd.cpp.obj`(21:15) / `pbs.cpp.obj`(21:11) /
  `yapf_rail.cpp.obj`(21:02)，上一轮全量已覆盖 ABI 变更（`platform_leap` 成员）。

## 六、构建与状态

- 改动：仅 `src\pathfinder\yapf\yapf_costrail.hpp`（`src\*.h` 家族，且被 yapf 各 TU include）。
- 构建方式：因改的是头文件，按铁律走 `R3R_fullbuild.cmd`（全量），不用手动增量。
- 构建结果：见下方「构建自证」段。
- 状态：**已修（待游戏内复测）**。

## 七、复测判据

1. 同场景（会合点 `1439,1874`，车底 `1424..1437`）挂接时**不再崩**，`station_map.h:31` 断言不再出现。
2. 会合点仍作终点：日志应出现 `PFD-MEET ... tile=1439,1874`，且路径 `CPL-BEST`/`COUPLE-OK` 行为与第 507 轮预期一致。
3. 真站台到达的 YAPF 代价不变：普通列车进站（非 R3R 会合）路径/时隙与上版一致，无"绕远站台"回归。
4. 无新断言（`pbs.cpp:144/145`、`rail_map.h:168`）伴随出现。
