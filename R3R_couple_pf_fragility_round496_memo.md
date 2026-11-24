# 第 496 轮：为什么「挂接寻路已经几乎等于普通寻路」却仍然不断冒出雷霆问题（元分析，零改码）

> 玩家原话（2026-11-24）：
> 「明明挂接寻路几乎和普通寻路一模一样了，为什么挂接寻路总是相比起普通寻路，冒出来一堆雷霆问题」

本轮**零源码改动、零构建**。目标不是再修一个具体 bug，而是回答"为什么这一类问题反复出现"，
并给出可审计的代码级判据——以后每冒出一个新问题，先归类到本 memo §3 的某一条前提上，
再决定修法。

---

## 一、先确认"几乎一模一样"这句话的事实基础

两套寻路确实共享同一个 YAPF 搜索骨架：同一个 `CYapfBaseT` 节点展开、同一个
`CFollowTrackT`（`follow_track.hpp`）、同一个 open/closed 表、同一个 `PfCalcEstimate`、
同一个 `CYapfFollowRailT` 入口。差异被刻意压缩到 **两个类** 里：

| 层 | 普通行车寻路 | 挂接寻路（GOTO_COUPLE） |
|---|---|---|
| 目标策略类 | `CYapfDestinationTileOrStationRailT`（`yapf_destrail.hpp:224`） | `CYapfDestinationTrainRailT`（`yapf_destrail.hpp:370`） |
| 终点判定 | `PfDetectDestination(tile, td)`（`:310`） | `PfDetectDestination(tile, td)`（`:486`） |
| 预留回溯 | 不经过 YAPF 回溯（由 `ExtendTrainReservation` / PBS 沿路径订） | 由 YAPF 自己的 `rp()` lambda 逐节点订（`yapf_rail.cpp:2071` 起） |
| 入口 | `ChooseRailTrack` 单 origin | `FindNearestCoupleTrain`（`yapf_rail.cpp:1597`）单 origin + `DisableCache(true)`（`:1593`） |

所以玩家的观察是**成立的**：骨架一样，差异集中、可数。问题在于——**差异集中的那两处，
恰好是 YAPF 仅有的两个"整段算法都建立在它之上"的前提**。

---

## 二、代码级取证：两种终点判据的语义完全不同

### 2.1 普通寻路的终点 = 地图的静态属性（纯函数）

`yapf_destrail.hpp:310` 的判据只有三类信息：站台 ID、轨道位、trackdir：

- `HasStationTileRail(tile)` / `GetStationIndex(tile) == dest_station_id` /
  `GetRailStationTrack(tile) == TrackdirToTrack(td)`；
- 否则 `tile == dest_tile && HasTrackdir(dest_trackdirs, td)`。

这三个量在**一次搜索内必然恒定、跨 tick 也恒定**（除非玩家拆建）。因此该函数满足：

1. 幂等（同一 (tile, td) 永远同结果）；
2. 无副作用（只读地图）；
3. 与"谁停在哪、谁订了哪"完全无关。

### 2.2 挂接寻路的终点 = 另一个实体的运行时状态（非纯函数）

`yapf_destrail.hpp:486` 的同一个函数，除站台快照分支外，还要读：

- `:573` `HasReservedTracks(tile, TrackdirBitsToTrackBits(tdb))` —— **地图上的 PBS 预留位**；
- `:588` `R3RGetCouplePairPartner(Train::From(Yapf().GetVehicle()))` —— **配对锁**；
- `:593` `GetTrainForReservation(tile, TrackdirToTrack(td))` —— **预留归属者**，随后还要求
  `R3RCoupleAllowed(...)` / `TrainFitStation(...)` / `R3RIsCoupleTarget(...)`（命令层验收判据）
  同时成立。

这三个量**每 tick 都可能变**，而且**会被寻路自己写**——`rp()` 订位写的就是这两个量读的那份状态。

### 2.3 关键观察：同一份代码里两种范式并存

第 414 轮已经把**站台模式**改造成了快照式：`SetDestination(v)`（`:397`）里调
`R3RGetCouplePlatformEndTile()`（`:415`）把终点固化成 `r3r_dest_end_tile`
（字段声明 `:386`），`PfDetectDestination` 之后**只比坐标**（`:522` `r3r_exact_hit`，
`:526-532` 同排 + `IsCompatibleTrainStationTile`），并明确注释"校验已全部上移到
SetDestination"（`:492`）。

也就是说：**代码里已经存在一个"正确范式"的样本，而其余分支仍停留在旧范式**。
这解释了为什么症状总是出现在"车底不在站台上"（depot / 普通轨道 / 半路）的场合，
而站台挂接相对稳定。

---

## 三、结论：被打破的是 YAPF 的五条隐含前提，不是某个实现失误

挂接寻路并没有"写错"，它是把一个**单机静态最短路问题**的算法，套到了**多智能体协调问题**上。
YAPF 骨架隐含五条前提，挂接打破了全部五条：

**P1 终点常量性**（同一次搜索内恒定，跨 tick 稳定）
→ 破坏：配对锁、预留归属、车底位置逐 tick 变。
→ 表现为：`CPL-DESTCHAIN` 的 `safe/final` 与 origin 无关地漂移或恒定（KI-679 / REG-494-A）。

**P2 判据无自指**（判据读的状态不被搜索写）
→ 破坏：判据读预留位，`rp()` 写预留位。自己订的位会遮住伙伴（`r3r_masked_by_self`），
自己会成为 `GetTrainForReservation` 的返回者。
→ 表现为：`RESV-PAIR-AIR` 段落判可达、`destEq=0` 而 `ok=1`。

**P3 启发函数与接受判据同源**（`PfCalcEstimate` 用的 `dest_tile` 就是终点）
→ 破坏：接受判据是"配对锁给的格 + 同排"，`dest_tile` 只是订单目的地的大致位置；
第 414 轮引入的 `r3r_dest_end_tile` 与 `dest_tile` 甚至可以是两个不同的格子。
→ 表现为：A* 退化成贪心、open 表被抽空后 `found=0`，或 `best` 落在平行轨上。

**P4 到达即成功**（进入终点格集即任务完成）
→ 破坏：挂接的成功条件是**物理端点接触**（车长、朝向、`SPLICE-GAP-REJECT`、`FOLDCHK`、
`cached_veh_length` 与包围盒两套几何）。节点踩到格 ≠ 能拼上。
→ 表现为：`COUPLE-REFUSE-STILL-FOLDED`、拼接面 8px 缝、双头 `sep=2`。

**P5 单一决策主体**
→ 破坏：机车在跑 `GOTO_COUPLE`，车底同时在自己跑订单（可能正在进站、可能被别人锁、
可能正在离开）。终点是一个**会自己移动的 agent**，而不是地图上的坐标。
→ 表现为：`TrainCoupleHandler` 每 tick 重试、`COUPLE-DEST-EMPTY`、`CPL-SCAN` 六类 reject。

另外还有两条挂接独有的结构缺口（不属于 YAPF 前提，但同样只能出现在挂接上）：

**S1 单向单 origin**：`FindNearestCoupleTrain` 只有一个 `origin`（`yapf_rail.cpp:1604`），
没有 depot 那套 `FindNearestDepotTwoWay`。目标落到车后 ⇒ 朝前那片 open 表被抽空 ⇒
`found=0` 每 tick 复发。代码注释已自陈此点（`:1753-1768`，第 472 轮 KI-675）。

**S2 预留回溯是 R3R 独有的第三条实现**：普通寻路不经过 YAPF 回溯订位；挂接经过，
于是 `rp()` 里所有"特化"（站台跳段补订、平台整格、轴向扫描、`TrackdirToTrack` 推导）
都成了**普通寻路不存在的新失败面**（KI-346 / KI-680 / KI-683 全在这条路径上）。

---

## 四、为什么"总是"挂接出问题：失败面是乘性的，且被重试放大

普通寻路只有一条链路：`目标(静态) → 搜索 → 预留`。出错 = 车不走 / 走错，一个问题。

挂接是四段串联，任一环失败都表现为"挂不上"：

```
目标解析(配对锁/车底在场/分组/装货/车数)   ← 6 类 reject，独立失败面
   → 搜索(单向 + 启发不同源)              ← found=0 有 3 种成因
      → 回溯预留(站台跳段/R3R 特化)        ← 铺错线、幽灵预留、部分预留放行
         → 拼接(几何/朝向/端点角色错位)    ← FOLDCHK / SPLICE-GAP / 双头
```

四段串联 ⇒ 观察到的故障率近似**乘性叠加**；而普通寻路只有一段。
更糟的是挂接失败后 `ProcessOrders` 不推进 `GOTO_COUPLE`（KI-193 历史问题），
**每 tick 重试**把一次性噪声放大成刷屏与永久卡死——同样的逻辑错误，普通寻路只是一次
"原地停住"，挂接会变成 1GB 日志级别的现场。

这就是"雷霆问题"的来源：不是挂接寻路比普通寻路更差，而是
**它的每一处不确定都被重试循环与四段串联同时放大**。

---

## 五、现有 KI 全部落在这个分类里（可复核性检验）

如果本 memo 的结论成立，那么历史上所有挂接独有问题都应能归到上面 7 条之一：

| KI / 现象 | 归属 |
|---|---|
| KI-675 单向单 origin、目标在车后 found=0 | S1 |
| KI-676 `best` 被甩到平行线、safe/final 常量漂移 | P1 + P3 |
| KI-678 断头线 / KI-679 `safe/final` 与 origin 无关 | P1 + P3 |
| KI-680 `nOK<nTry` 仍 `ok=1` | P2 + S2 |
| KI-682 「不按预留走」 | P2（读自己写的预留） |
| KI-683 预留溢出站台观感 | S2（`rp()` 特化） |
| KI-265 / 附记 4 拼接面 8px、方向判据双罚 | P4 |
| KI-334 双头 `sep=2`、KI-346 错轨道预订 | P4 / S2 |
| KI-193 `COUPLE-DEST-EMPTY` 每 tick 全图重扫 | P5 |
| `CPL-SCAN` 六类 reject（KI-344） | P5（目标阶段独立失败面） |

七条覆盖全部已知挂接独有问题，没有一条需要"挂接寻路实现有 bug"这种解释。
反向检验也成立：**普通寻路同类问题只有 P1/P3 被触及时才会出现**
（第 495 轮 veh=75 的 `safe/final=1457,1873` 恒定 + 岔口换线，正是答案本让 P1 在普通侧失效）。

---

## 六、可操作落点（建议，本轮未改码）

按"消除前提破坏"排序，而不是按症状排序：

1. **把目标解析从搜索里彻底移出（P1/P2 根修）**
   照第 414 轮站台模式的样板，把 depot / 普通轨道 / 半路三条分支也改成
   `SetDestination()` 里一次性解析出 `(target_tile, target_td)`，写进快照字段，
   `PfDetectDestination` 只做坐标比较，**不再读 `HasReservedTracks` /
   `GetTrainForReservation` / 配对锁**。
   代价：目标必须在 `SetDestination` 那一刻确实在场；不在场就干脆 `found=0` 并
   归到"目标不在场"类（这正是我们想要的可判别性）。

2. **启发函数改吃快照终点（P3）**
   `PfCalcEstimate` 用 `r3r_dest_end_tile` 而不是 `dest_tile`，否则 A* 与接受判据不同源，
   `best` 会系统性偏到平行轨。

3. **双向搜索（S1）**
   仿 `FindNearestDepotTwoWay`，用 `moving_back` 端再搜一次，`rev=1` 时改走"掉头"语义，
   而不是每 tick 从同一 origin 重抽一次 open 表。

4. **把"目标不在场"和"找不到路"分开计费（P5）**
   前者不该重试寻路（应推进/挂起订单，KI-193 已有 `COUPLE-DEST-EMPTY` 机制），
   后者才重试。这样日志量与卡死都能立刻降一个量级。

5. **`PfDetectDestination` 保持无副作用纪律**
   目前里面还有探针计数（`_r3r_cpl_scan_enter`，`yapf_destrail.hpp:35`）与节流静态
   （`r3r_dest_rej_seen`）。不影响返回值，但破坏了"判据函数可重入"的可读性；
   新增判据一律不得在其中写状态（历史教训：曾在里面直接调命令层校验）。

---

## 七、未确认项

- **U-1**：`PfCalcEstimate` 对 `CYapfDestinationTrainRailT` 是否真的用 `dest_tile`
  （本次只 grep 到字段与注释，未逐行读 `PfCalcEstimate`），需下一次取证确认后
  才能把 §6.2 定为必修。
- **U-2**：depot 模式的终点判定是否已有独立快照分支（`R3RGetCoupleDepotEndTile` 是否存在），
  若已有则 §6.1 的工作量只落在"普通轨道/半路"一条分支。
- **U-3**：`TrainFitStation` 在 `PfDetectDestination` 内的调用是否可能改动车辆/站台缓存
  （`gcache`/`station`），若有写操作则是 P2 的加强版。
- **U-4**：本 memo 未做日志统计，未量化"四段串联"各自的占比；若要做，用现成的
  `CPL-SCAN`（六类 reject 计数）对一段日志做一次汇总即可，不需要新探针。

---

## 八、本 memo 的用法（复测/判据）

以后新增一条挂接问题，第一句话必须是：

- 它属于 P1 / P2 / P3 / P4 / P5 / S1 / S2 中的哪一条？
- 若是 S2（`rp()` 特化），必须先证明普通寻路不走该代码路径；
- 若是 P4，必须给出像素/车长读数（`FOLDCHK` / `SPLICE-GAP` / `RESPACE`），
  因为 P4 的所有症状都能用几何量证伪；
- 若归不到任何一条，说明是**新前提被打破**，应先把新前提写进本 memo §3 再动代码。

反面纪律：**不要再以"挂接寻路和普通寻路一样，所以应该表现一样"作为推理前提**。
两者算法一样、问题类不同；"表现一样"这个期望本身就是错的。
