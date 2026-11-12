# R3R 答案本（性能优化初版）崩溃取证与修复 — 第 409 轮

日期：2026-11-09
触发：玩家「这个性能优化的初步版本触发了崩溃，希望你修复一下」
现场产物：`build\openttd.exe`（2026-11-09 00:35:48 链接，含第 408 轮答案本）

---

## 一、崩溃现场（原文锚点，非推断）

崩溃日志：`C:\Users\冯洁敏\Documents\OpenTTD\crash-20261108T164103Z.log`
（时间 `2026-11-08 16:41:03 UTC` = 本地 `2026-11-09 00:41:03`，即第 408 轮 exe 链接完成后 5 分钟）

```
:11  Exception:  C0000005
:13  Fault type: 0 (read)
:14  Fault addr: 0000000000000038
:18  Within context: 0: CallVehicleTicks: veh: 16524: (Train 36, c:5, st:FE, ...)
:29  Build date:  Nov  9 2026 00:28:12
```

```
:33  [00] openttd  0x...3BEB PoolID<VehicleIDTag>::base + 11 (src\core\pool_id_type.hpp:73)
:34  [01] openttd  0x...E9E5 CYapfReserveTrack<...>::R3RAnswerReplay + 133 (src\pathfinder\yapf\yapf_rail.cpp:1212)
:35  [02] openttd  0x...35EF CYapfFollowRailT<...>::ChooseRailTrack + 399 (src\pathfinder\yapf\yapf_rail.cpp:2188)
:36  [03] openttd  0x...087B CYapfFollowRailT<...>::stChooseRailTrack + 219 (yapf_rail.cpp:2149)
:37  [04] openttd  0x...A3B0 YapfTrainChooseTrack + 112 (yapf_rail.cpp:2352)
:38  [05] openttd  0x...C9EA DoTrainPathfind + 122 (train_cmd.cpp:16026)
```

## 二、根因（一句话）

`R3RAnswerReplay()` 第一行取 `Yapf().GetVehicle()`，但该指针**在 `ChooseRailTrack` 里此刻仍是 nullptr**，
于是 `veh->index.base()` 解引用 nullptr + 0x38（`Vehicle::index` 的偏移）⇒ C0000005。

### 为什么是 nullptr —— 机制链

1. `CYapfBaseT::vehicle` 成员**只在** `FindPath(v)` 里赋值：
   `src\pathfinder\yapf\yapf_base.hpp:163` → `this->vehicle = v;`
2. `CYapfReserveTrack::TryReservePath()` 里能安全用 `Yapf().GetVehicle()`
   （`yapf_rail.cpp:992`、`:647`）是因为它**永远在 `FindPath` 之后**跑。
3. 第 408 轮把答案本回放插在 `ChooseRailTrack` 的 `SetOrigin(...)` 之后、`SetDestination(v)`（`:2196`）
   与 `FindPath(v)`（`:2199`）**之前** ⇒ 此时 `this->vehicle` 从未被本次调用写过，
   首次寻路时是构造初值 nullptr ⇒ 崩。

即：不是逻辑错、不是索引越界，是**取错了 vehicle 来源**——明明 `ChooseRailTrack(const Train *v, ...)`
手上就有 `v`，却绕去问一个还没被赋值的成员。

## 三、连带隐患（同一个坑的第二处）

`ReserveSingleTrack()`（`yapf_rail.cpp:638`）内部第 647 行也写死：
```cpp
if (!TryReserveRailTrackdir(Yapf().GetVehicle(), tile, td)) {
```
回放路径会调用它 ⇒ **即使把 `R3RAnswerReplay` 的首行改掉，一旦走到非站台格仍会以 nullptr 调
`TryReserveRailTrackdir`**，同样崩（可能在 `pbs.cpp` 内部更晚才炸，栈更难读）。
所以必须让 `ReserveSingleTrack` 也能拿到调用者显式传入的车。

## 四、修法（最小侵入，不复制分支逻辑）

| # | 位置 | 改动 |
| --- | --- | --- |
| 1 | `yapf_rail.cpp:638` `ReserveSingleTrack` | 形参加 `const Train *reserver = nullptr`；`:647` 改 `TryReserveRailTrackdir(reserver != nullptr ? reserver : Yapf().GetVehicle(), tile, td)`。原生调用点一行不改，行为逐字节不变 |
| 2 | `yapf_rail.cpp:1205` `R3RAnswerReplay` | 首参加 `const Train *v`；删掉 `const Train *const veh = Yapf().GetVehicle();`，内部全部改用 `v`；`ReserveSingleTrack(..., v)` 显式传车 |
| 3 | `yapf_rail.cpp:2188` 调用点 | `this->R3RAnswerReplay(v, origin, target, dest, &answer_td)` |

**刻意不选的两条路**：
- 在回放前补一句 `Yapf().SetVehicle(v)`：`CYapfBaseT` 没有这个公开 API（只有 `FindPath` 会写），
  硬加等于改 `yapf_base.hpp`（头文件）⇒ 触发全量重编 16~20 分钟，且给模板基类开口子。
- 在回放里复制一份 `ReserveSingleTrack`（站台 `ReserveRailStationPlatform` + 路点随机化/动画）：
  两份实现必然分叉，将来改预留规则会漏改一处。

## 五、教训（写死在这里，防止再犯）

**在 `FindPath` 之前不许调用任何依赖 `Yapf().GetVehicle()` 的成员。**
判断某个 YAPF 成员函数能不能在回放路径用，先看它体内有没有 `Yapf().GetVehicle()`。
当前已知依赖者：`ReserveSingleTrack`（`:647`）、`TryReservePath`（`:992`）。

## 六、复测判据

| # | 判据 |
| --- | --- |
| ① | 同一存档同一操作不再崩；日志无新 `crash-*.log` |
| ② | 正常行车出现 `ANSWER-REPLAY ... booked=N`（N>=1），说明回放真的在跑而不是全程回落 A\* |
| ③ | 负判据：回放成功后列车轨迹与改前一致（不出现原地抖动/反向） |
| ④ | 负判据：`ANSWER-DROP reason=nothing-booked` 不应刷屏（刷屏说明回放路径一直在失败重试） |
| ⑤ | 站台场景（`IsRailStationTile`）也要覆盖一次——本次修复的第 2 处隐患在那里 |

## 七、状态

- [x] 崩溃根因定位（栈 + 机制，双证）
- [x] 修法确定（上表三条）
- [x] 代码落地（`yapf_rail.cpp` 三处：`ReserveSingleTrack` 签名、`R3RAnswerReplay` 签名与首行、`ChooseRailTrack` 调用点）
- [x] 增量编译 `EXIT_CODE=0`
- [ ] 玩家实机复测

## 八、构建自证（2026-11-09 00:49）

- 入口：`R3R_fullbuild.cmd inc detach`（复用既有入口，未新建任何 `.cmd`）
- guard：`GUARD: incremental is safe (no header/lang file is newer than the newest object)`
- 日志：`[2/3] Building CXX object ...yapf_rail.cpp.obj` → `[3/3] Linking CXX executable openttd.exe`
- 判决：`build\R3R_incbuild.done` = `EXIT_CODE=0`（2026-11-09 00:49:11）
- 时间戳：`yapf_rail.cpp` 00:47:02 → `yapf_rail.cpp.obj` 00:47:49 → `build\openttd.exe` 00:49:02（52 524 544 B）
- `read_lints` 0 条
- 本轮未新增日志字面量（只加了 `assert`），故无 exe 串核对项

## 九、修复后的完整调用链（供后人复核）

```
CYapfFollowRailT::ChooseRailTrack(const Train *v, ...)      yapf_rail.cpp:2176
  ├─ origin = FollowTrainReservation(v, ...)  /  PBSTileInfo(v->tile, ...)
  ├─ if (reserve_track) R3RAnswerReplay(v, origin, target, dest, &answer_td)   ← 传入 v
  │    ├─ veh = v                              （不再问 Yapf().GetVehicle()）
  │    ├─ ReserveSingleTrack(tile, td, veh)    ← 第三次传参到底
  │    │    └─ TryReserveRailTrackdir(veh, tile, td)
  │    └─ 成功 → path_found=true; return answer_td
  ├─ Yapf().SetDestination(v)                  :2196
  └─ Yapf().FindPath(v)                        :2199   ← 只有走到这里 this->vehicle 才有值
```

