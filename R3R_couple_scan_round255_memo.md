# 第 255 轮：挂接寻路/配对是否存在全图搜索（性能）

- 日期：2026-10-20
- 玩家报告原话：「挂接寻路/配对是不是有一个要用到全图搜索的，这个可能会导致列车挂接等很久，能不能优化一下它」
- KI 条目：`R3R_KNOWN_ISSUES.md` 的 **KI-385**
- 状态：取证完成，**未改任何源码**

---

## 一、结论先说

玩家的猜测**部分成立，但对象错了**：

- **配对（找"该挂谁"）不是全图搜索**——它走的是「本车附近 tile 的车辆哈希链」+「本车预留链回溯」，范围是 2~9 格。
- **但挂接路径上确实有全图 tile 扫描**，共两处：
  1. `R3RReleaseChainReservations()` —— **全图 419 万格**，在**每次挂接/解挂/waiter 提交点**都跑一次（这是"挂接瞬间卡一下"的头号嫌疑）；
  2. `R3RReservationAudit()` —— 全图扫描，但已节流到 2048（内测）/8192（发布）tick 一次，属既有设计。
- 另外还有一个**真正的全车辆遍历**：配对锁 `R3REnsureCouplePair()` 里的 `Train::Iterate()`，8 tick 节流。

"挂接等很久"的**最可能直接原因不是搜索成本，而是三处等待型节流**（见 §四），其中 `R3R_COUPLE_DEST_IDLE_LIMIT = 500` 让停在目的地上的待挂机车**最长约 500 tick 才重试一次候选搜索**。

---

## 二、取证读数（含行号，可复核）

### 2.1 配对层：不用全车辆遍历

车库分支走「本车格 + 库出口格」两格的哈希链：

```
src/train_cmd.cpp:12195-12198
TileIndex tiles[2] = { v->tile, v->tile + TileOffsByDiagDir(GetRailDepotDirection(v->tile)) };
for (TileIndex tile : tiles) {
    Vehicle *first = GetFirstVehicleOnTile(tile, VehicleType::Train);
```

明线分支走「回溯本车预留链」：

```
src/train_cmd.cpp:12222   u = GetCouplePosition(v, reverse);
src/train_cmd.cpp:11933   FollowTrainReservation(v, &other_vehicle);   // GetCouplePosition 内
```

贴靠兜底走 3×3 邻格：

```
src/train_cmd.cpp:12239-12243   3x3 邻格 + GetFirstVehicleOnTile / HashTileNext
```

⇒ 判定：**配对搜索范围 = 常数级（2 / 链长 / 9）**，与地图大小无关。

### 2.2 配对锁：全车辆遍历，但有 8 tick 节流

```
src/train_cmd.cpp:11841   for (Train *t : Train::Iterate()) {     // R3REnsureCouplePair 内（定义 :11791）
src/train_cmd.cpp:11835-11837
static const uint64_t R3R_PAIR_RESCAN_TICKS = 8;
auto scan_it = _r3r_pair_scan_tick.find(coupler->index.base());
if (scan_it != ... && _tick_counter - scan_it->second < R3R_PAIR_RESCAN_TICKS) return;
```

调用点：`train_cmd.cpp:18279`、`18355`（`TrainLocoHandler` 内，每 tick 每链头一次入口，但整段被 8 tick 节流）。

⇒ 判定：O(车辆数) / 8 tick / GOTO_COUPLE 链头。**这是唯一与"全图规模"沾边的配对成本**，但 8 tick 节流后量级可接受；待优化。

### 2.3 YAPF 挂接寻路：从机车出发，上限 10000 节点

```
src/pathfinder/yapf/yapf_rail.cpp:813,824
PBSTileInfo origin = PBSTileInfo(v->tile, v->GetVehicleTrackdir(), false);
Yapf().SetOrigin(origin.tile, origin.trackdir);
```

- 节点上限：`max_search_nodes`，默认 `AYSTAR_DEF_MAX_SEARCH_NODES = 10000`（`src/pathfinder/aystar.h:28`），设置范围 500~1000000（`src/table/settings/pathfinding_settings.ini:131-136`）；截断在 `yapf_base.hpp:180`。
- **无代价上限**：`SetMaxCost()` 只用于"找最近车库"（`yapf_rail.cpp:700`）；挂接用的 `CYapfCoupleRail`（`yapf_rail.cpp:1493-1494`）不设 ⇒ `max_cost = 0` = 不限。
- 目的地探测每节点只认站台/车库格：`yapf_destrail.hpp:413-419`；站台补扫受站台长度约束（`:452-454`）。

⇒ 判定：搜索范围 = **从机车出发的可达路网**，最多 10000 节点，**不是全图**；但没有代价上限意味着"目标很远时会把 10000 节点吃满"。

### 2.4 全图 tile 扫描清单

| 位置 | 属哪条路径 | 频率 |
|---|---|---|
| `src/train_cmd.cpp:10662`（函数定义 `:10589`） | `R3RReleaseChainReservations()` 提交点预留清扫 | **每次提交**：`:10893`(couple)、`:11000`(couple-settled)、`:8484-8485`(decouple)、`:18178`(waiter) |
| `src/train_cmd.cpp:15544`（定义 `:15487`） | `R3RReservationAudit()` | 2048（内测）/ 8192（发布）tick |
| `src/pbs.cpp:2544`（定义 `:2534`） | `R3RPurgeStrayReservations()`（控制台 `r3r_resv_purge` + 审计共用） | 手动 / 审计内 |

两处主循环都是：

```cpp
for (TileIndex t(0); t < Map::Size(); t++) {
```

2048×2048 图 = **4 194 304 格**。

⇒ 判定：**这就是玩家说的"全图搜索"**。但它不在"配对"里，在**提交点**。

### 2.5 死代码（不构成成本，登记以免后人误判）

`src/pbs.cpp:861` `IsRailStationPlatformFree()` 内有 `Vehicle::Iterate()`，全仓**无任何调用点**（仅 `src/pbs.h:120` 声明）。

---

## 三、疑点登记

- **REG-255-A（疑点）**：`R3RReleaseChainReservations()` 是**全图扫描**，却服务于"释放这条链的预留"。链的预留格必然在**车辆自身 tile 的子集**里，用全图扫描找它是 4 个数量级的浪费。期望实现 = 遍历链上各节的 `tile` 及其邻格；实测实现 = `t 从 0 到 Map::Size()`。→ 待确认（见 §五 待验证项 V-1）。
- **REG-255-B（疑点）**：`R3R_COUPLE_DEST_IDLE_LIMIT = 500` 的注释口径是"停在目的地上的待挂机车每 500 tick 才跑一次候选搜索"。500 tick 在玩家感知上是很长的一段（按 30 tick/s 约 16 秒；暂停/加速下感受不同）。若玩家报告的"等很久"是**挂接迟迟不开始**而不是"卡顿一下"，主因就是这个常量，而不是任何搜索算法。
- **REG-255-C（疑点）**：YAPF 挂接无 `max_cost`，目标远时吃满 10000 节点。但这是"单次寻路成本"，不影响"何时开始挂接"。
- **REG-255-D（猜测根因，待验证）**：玩家主观感受的"等很久"很可能由**三件事叠加**：①`DEST_IDLE_LIMIT=500` 的重试间隔；②`SCAN_RETRY=8` 空手后的冷却；③提交瞬间的 `R3RReleaseChainReservations()` 全图扫描造成的单帧卡顿（观感上像"卡住不动"）。三者需要日志区分，不能靠猜。

---

## 四、优化候选（按收益/风险排序，**均未实施**）

### O-1（首选，纯机械优化）`R3RReleaseChainReservations` 去掉全图扫描

- 现状：全图 419 万格，只为找"这条链订过的位"。
- 改法：改为遍历**链上每一节的 `tile` 及其 8 邻格**（或每节的 `tile + TileOffsByDiagDir(direction)`），对每个候选格做与现在**逐字相同**的四道校验后释放。
- 风险：必须保证不漏——当前实现是"全图兜底"，改成"局部枚举"后若某格不在枚举范围内就会漏（表现为幽灵预留回归 KI-203c/KI-345）。**需要先用探针证明"链的预留格恒 ⊆ 链节 tile 邻域"**，否则不做。
- 判据：同一场景改前改后 `RESV-GHOST`/`RESV-GC` 释放的格集合**逐格相同**。

### O-2（低风险）配对锁 `Train::Iterate()` 加"同站台/同车库"预筛

- 现状：每 8 tick 全车辆扫一遍。
- 改法：先用 `coupler->tile` 的站点/车库 id 做粗筛，或复用 `Station::loading_vehicles` 这类现成列表。**注意**：`GetCouplePosition` 的目标可能不在站台上（明线等待），故预筛必须"站台内查列表 + 站台外回退全扫"，否则会漏配。
- 风险：漏配 = 挂接失效，属高后果。仅在 O-1 之后评估。

### O-3（口径问题，需玩家拍板）放宽 `R3R_COUPLE_DEST_IDLE_LIMIT`

- 现状 500 tick。若玩家感受是"等到不耐烦"，可降到 128/256。
- 代价：该常量本身是为 KI-193（目的地无车底时每 tick 全图重扫刷屏）加的节流；调小会提高重扫频率。
- **必须先有日志证据**证明"等很久"发生在候选搜索之前、且是因为这个节流，才能动它。

### O-4（不建议）给 YAPF 挂接加 `max_cost`

- 会让"目标较远但确实可达"的挂接直接失败，属用功能换性能，不划算。

---

## 五、待验证项（本轮未取证完）

- **V-1**：`R3RReleaseChainReservations()` 里那个全图循环实际在找什么、能否用链节 tile 邻域替代（需读 `:10589-10690` 全文）。
- **V-2**：`R3RReleaseChainReservations` 的 waiter 调用点 `:18178` 是否每 tick 都进（若每 tick 全图扫，那就是**卡顿主因**，优先级最高）。
- **V-3**：玩家现场日志里"待挂机车从到达到挂上"的 tick 差，用来定量判断是 `DEST_IDLE_LIMIT` 还是别的原因。
- **V-4**：`R3R_PAIR_RESCAN_TICKS=8` 下 `Train::Iterate()` 的实际占比（需 perf 取样，非本轮范围）。

---

## 六、下一步

1. 先做 V-2（一条命令即可定性：全图扫描是否每 tick 进）。
2. 再决定是否动 O-1。
3. O-3 需玩家确认"等很久"的具体形态（迟迟不开始 / 开始后卡顿 / 两者都有）。
