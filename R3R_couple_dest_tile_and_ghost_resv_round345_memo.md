# R3R 第 345 轮：挂接「目的地指向车底格」+「配对车底预留对本车透明」

## 0. 玩家命令（原话，不得改写）

> 现在，我们让车站分支的前往挂接命令指向车底所在格，并让车底的预留对于自己配对的列车视作空气

这是对第 344 轮 KI-511（`CPL-PF-FALLBACK` 兜底）的**继续收窄**：兜底只解决"失败后不钉死"，但没有给普通寻路一个可达终点，也没有解除对方 PBS 预留的阻挡。本轮两条手段正是补这两个前提。

## 1. 两条手段的技术含义

### 手段 A：前往挂接命令（`OT_GOTO_COUPLE`）的 `dest_tile` 指向车底所在格

现状疑点（待 U-1 取数确认）：`OT_GOTO_COUPLE` 的 `dest_tile` 要么是 `INVALID_TILE`，要么还残留着上一条订单的站/路点。两种情况下普通寻路（`DoTrainPathfind`）都没有"车底格"这个终点：

- `dest_tile == INVALID_TILE` ⇒ 普通寻路没有目标，`path_found = false`；
- `dest_tile` 是别的格 ⇒ 普通寻路朝错的方向铺预留（KI-479 的"沿 y=14 西行"疑似同源）。

若 A 落地，`YapfTrainChooseTrack` 的目标函数（`CYapfDestinationTrainRailT`）会以车底格为终点，普通寻路的 back-walk（`FindSafePositionOnNode`，只判几何不逐格订路）就可能算出"到车底前的安全位为止"的合法路径 —— 这正是第 344 轮兜底需要的前置条件。

### 手段 B：配对车底（等待方）的轨道预留，对本车视作空气

现场是交叉预留死结：`FSCP tile=14,15 fail=tryReserve tbits=0x9 res=0x8`，54 占 `TRACK_Y` 而 57 要 `TRACK_X`，PBS 禁止交叉预留。但 54 是**57 自己配对的车底**——它本来就该在原位等着被挂，它的预留不该构成对 57 的阻塞。

B 的语义边界（必须写死，防扩大化）：
- 只在"占位者 == 本车当前配对目标"时放行；
- 只在**挂接寻路/普通寻路为挂接订单铺预留**时放行，不放行实际持锁（即 57 不得真的把 54 的位抢过来）；
- 超时/配对失配时不放行，否则会退化成第 341 轮已证伪的"放宽位级互斥"（会让车走错岔道）。

## 2. 候选落点（待取数确认，未确认不得改码）

| 手段 | 候选落点 | 待确认 |
|---|---|---|
| A | `src\train_cmd.cpp` 的 `ProcessOrders()` 对 `OT_GOTO_COUPLE` 的处理；`SetDestTile()`；`TrainCoupleHandler()` 里配对目标解析 | U-1：当前 `dest_tile` 实际值从哪来；U-2：配对目标（车底）格如何取（`First()->tile`？还是链尾？） |
| B | `src\pbs.cpp` 的 `CheckTrainsOnTrack()`（记忆锚点 986-1011）；`src\pathfinder\yapf\yapf_rail.cpp` 的 `TryReserveRailTrack` 冲突分支 | U-3：预留冲突的判定入口是哪一个（有多个）；U-4：`CYapfCoupleRail` 与 `CYapfRail` 是否共用该入口 |

## 3. 已知风险

- **R-1 手段 A 可能让列车朝车底开进"被占格"**：普通寻路的目标格被 54 占着，`CYapfDestinationTrainRailT` 默认把占位格当障碍 ⇒ 需要 U-2 确认目标函数是否允许"终点被占"（挂接寻路允许，普通寻路不允许）。
- **R-2 手段 B 碰的是 desync 冻结区**：`pbs.cpp` 的 `CheckTrainsOnTrack` 与 `TryReserveRailTrack` 是第 341 轮明确划为"不许放宽位级互斥"的区域。本轮只做"配对关系豁免"，绝不动位级互斥本身。
- **R-3 二者叠加可能出现"两车同时朝对方开"**：A 让 57 以 54 格为终点，B 让 54 的预留透明 ⇒ 若 54 也在朝 57 走，会互相穿透。守卫：只对本车的挂接订单生效，且要求对方 `current_order.IsType(OT_WAIT_COUPLE)`。

## 4. 复测判据（草案）

1. **正判据 1**：`CPL-ENTRY` 之后 `dest_tile` == 配对车底格（新增/复用探针打印），普通寻路 `found=1`。
2. **正判据 2**：`FSCP ... fail=tryReserve` 不再出现（B 生效），或出现新的豁免探针。
3. **负判据 A**：非挂接订单的 `dest_tile` 逐字不变。
4. **负判据 B**：不配对的两列车之间，PBS 预留照旧互斥（`TPR-BLOCKED` 形态不变）。
5. **负判据 C**：54 自身行为不变（它仍停着等挂，不因 B 而移动）。
6. **回归**：KI-509 / KI-359 P1 / KI-357 / KI-511 兜底判据不回退。

## 5. 取证记录（每条自带行号，勿重跑）

### R1（2026-10-28 第 1 组）：挂接订单的 `dest_tile` 恒为空

`grep SetDestTile src\train_cmd.cpp` 共 9 处，**全部**是 `SetDestTile(INVALID_TILE)`：

| 行号 | 上下文 |
|---|---|
| 2704 | `R3RClearStaleWaitMarker()` 清 `OT_WAIT_COUPLE` |
| 7277 | 跳过 R3R 订单 |
| 10044 | 解挂提交点（v 侧） |
| 10133 | 解挂提交点（u 侧） |
| 13211 | 耦合提交点清残留 `GOTO_COUPLE` |
| 15268 | 挂接提交失败跳单 |
| 21760 / 21773 | `ARTIC-SKIP-R3R` 跳单 |

⇒ **手段 A 的现状确认**：R3R 从不给 `OT_GOTO_COUPLE` 写目的地格。该订单从 `ProcessOrders` 装载之后 `dest_tile` 一直是 `INVALID_TILE`，所以普通寻路（`DoTrainPathfind`）拿不到"车底格"这个终点。**这是第 344 轮兜底 `found=0` 的直接原因之一。**

### R2（第 2 组）：预留接口的签名差异（手段 B 的关键）

- `pbs.cpp:476` `bool TryReserveRailTrack(TileIndex tile, Track track, bool trigger_stations)` —— **没有 `const Train *v` 参数**，函数内部无从知道"谁在订"，因此无法在此处判定配对关系。
- `pbs.h:88` `bool TryReserveRailTrackdir(const Train *v, TileIndex tile, Trackdir td, bool trigger_stations)` —— **有 `v`**。这是手段 B 唯一有"主体身份"的入口。
- `pbs.cpp:1303` `static void CheckTrainsOnTrack(FindTrainOnTrackInfo &info, TileIndex tile)` —— 同样**没有 self**，且 `info.best` 恒取最低索引车头（注释明写 `ALWAYS return the lowest ID (anti-desync!)`，`:1324-1325`）。调用点 6 处（`FollowTrainReservation` 的 `:1398/:1407/:1413`、`GetTrainForReservation` 的 `:1862/:1874/:1892`）。

⇒ **手段 B 必须落在 `TryReserveRailTrackdir(const Train *v, ...)` 一侧**（或在 `TryReserveRailTrack` 增加可选 `const Train *self` 参数），**不能改 `CheckTrainsOnTrack`**：后者是反 desync 冻结区，且它回答的是"谁停在这格"，不是"这格能不能订"。

### R3（第 3 组）：**R1 结论作废** —— 手段 A 其实已存在（KI-446）

R1 只 grep 了 `src\train_cmd.cpp`；真正给 `OT_GOTO_COUPLE` 写目的地的是 `src\order_cmd.cpp` 的 `UpdateOrderDest()`：

```cpp
// order_cmd.cpp:4601-4612
static TileIndex R3RGetCoupleOrderDestination(const Vehicle *v)
{
	if (v->type != VehicleType::Train) return INVALID_TILE;
	const Train *const tgt = R3RGetCouplePairPartner(Train::From(v));
	if (tgt == nullptr) return INVALID_TILE;
	const TileIndex target_tile = tgt->tile;
	if (!IsRailStationTile(target_tile)) return INVALID_TILE;   // ← 过窄
	return target_tile;
}

// order_cmd.cpp:4691-4703
case OT_GOTO_COUPLE:
	...
	const TileIndex station_dest = v->GetOrderStationLocation(order->GetDestination().ToStationID());
	/* R3R (KI-446): keep the travel target on the platform of the paired consist,
	 * so that the PBS destination and the couple pathfinder agree. */
	const TileIndex couple_dest = (station_dest != INVALID_TILE) ? R3RGetCoupleOrderDestination(v) : INVALID_TILE;
	v->SetDestTile(couple_dest != INVALID_TILE ? couple_dest : station_dest);
	return true;
```

⇒ **手段 A 已在（KI-446）**，本轮不是"新建"而是**放宽两处过早返回 + 补实时刷新**：
1. `:4609` `!IsRailStationTile(target_tile)` ⇒ 车底停在普通轨道 / 车库 / 路点时 A 直接失效（现场 1112,125 正是普通轨道）；
2. `:4700` `station_dest != INVALID_TILE` 前置条件 ⇒ 订单站解析失败时连配对都不查；
3. `dest_tile` 只在订单切换时刷新 ⇒ 等待期间车底若挪格就会变陈（U-5 待确认调用频率）。

### R4（第 3 组）：预留入口归属

- 挂接寻路的逐格订路在 `src\pathfinder\yapf\yapf_rail.cpp:279`：`TryReserveRailTrack(tile, TrackdirToTrack(td))` —— **走的无主体版本**（这就是 `FSCP fail=tryReserve` 那一行）。
- 普通寻路 back-walk 在 `yapf_rail.cpp:463`：`TryReserveRailTrackdir(Yapf().GetVehicle(), tile, td)` —— 有主体。
- `pbs.cpp:428` 的 `TryReserveRailTrackdir` 只是"`TryReserveRailTrack` + 点灯"的薄壳；真正拦截位级冲突的是 `pbs.cpp:484` 的 `TryReserveTrack(tile, track)`（rail_map.h）。
- ⇒ 唯一能同时覆盖两条寻路器、且**不必改 yapf_rail.cpp** 的落点 = `pbs.cpp:476` `TryReserveRailTrack()` 本体；主体身份用已有的全局 `r3r_resv_actor`（`pbs.h:43`，`R3RResvPhaseGuard` `pbs.h:63-73` 可设，非 `#if R3R_PROBES` 包裹）在 `DoTrainCouplePathfind()` 外层套一次即可拿到"谁在订"。

### R5（第 4 组）：**手段 B 的落点确认 = `pbs.cpp` 单文件，零头文件改动**

- `src\pathfinder\yapf\yapf_rail.cpp:922-926` `FindNearestCoupleTrain()` 开头已有
  `R3RResvPhaseGuard r3r_phase("yapf-couple", R3RResvActorID(v));`
  ⇒ **挂接寻路期间全局 `r3r_resv_actor` 恒等于"正在挂接的机车"**，FSCP 的 `:279` 与 `rp()` 的逐格订路都在这个 guard 作用域内。**不需要改 yapf_rail.cpp，也不需要新头文件**。
- `src\pbs.h:63-73` `R3RResvPhaseGuard` 与 `:40-43` 的 `r3r_resv_phase / r3r_resv_actor` **不在 `#if R3R_PROBES` 内**（`:44` 的 `#if` 只包 `R3RIsWatchedResvTile`）⇒ 探针关掉时仍可读，行为不依赖调试开关。
- 配对关系是**显式存档字段**：`couple_group.cpp:528-553` `R3RCouplePairMatches()` / `R3RGetCouplePairPartner()` 读 `r3r_couple_target` + `r3r_couple_requester` 双向互指；`:563-578` `R3RPairCoupleTargets()` 建立。⇒ B 的判据是确定性游戏状态，不引入随机/时序 ⇒ 不违反 desync 冻结约束。
- 占位者查询工具现成：`pbs.h:263` `Train *GetTrainForReservation(TileIndex, Track)`（普通轨道）；`pbs.h:271` `Train *GetTrainForWholeTileReservation(TileIndex)`（站台/路点整格位）。
- ⇒ **B 的落点 = `pbs.cpp:476 TryReserveRailTrack()` 开头**（所有 tile 类型唯一汇聚点，覆盖 `CYapfCoupleRail` 与 `CYapfRail` 两条寻路器、覆盖 rp() 与 FSCP 两处订路）：满足「actor 是 OT_GOTO_COUPLE 机车 + 有配对伙伴 + 该格预留的**全部**占位者都是这个伙伴」时直接 `return true`，**不写地图**（位仍归车底，车底开走时自然释放）。
  - 只覆盖 `TileType::Railway` 的 plain rail 与 `TileType::Station` 的 `HasStationRail` 格；车库 / 道口 / 隧道桥一律不动（车库有专有分支与 KI-511 的 depot 守卫）。

### R6（第 4 组）：**手段 A 的落点确认**

- 刷新时机没问题：`order_cmd.cpp:4850-4860` `ProcessOrders()` 的 `OT_GOTO_COUPLE` 分支**每 tick** 调一次 `UpdateOrderDest(v, &v->current_order)` ⇒ `dest_tile` 每个 tick 都会重算，车底挪格后不会长期变陈（U-5 已消解）。
- 缺口只有两处（`order_cmd.cpp:4601-4612`）：`!IsRailStationTile(target_tile)` 与 `:4700` 的 `station_dest != INVALID_TILE` 前置。
- ⇒ A 的落点 = 只改 `R3RGetCoupleOrderDestination()` 与 `:4691-4703` 的赋值：把"必须在站台上"放宽成"车底所在的铁轨格（普通轨道 / 站台 / 路点都算）"，并把 `station_dest` 从"先决条件"降级成"没有配对伙伴时的回退"；`GetCoupleIsDepot()` 的车库分支保持原样（玩家口径是「车站分支」）。

## 6. 落地记录（2026-10-28，只改 2 个 .cpp，未碰任何 `src\*.h`）

### A：`src\order_cmd.cpp`

1. `R3RGetCoupleOrderDestination()`（`:4601`）把 `if (!IsRailStationTile(target_tile)) return INVALID_TILE;`
   换成 `IsValidTile` + `GetTileType ∈ {Railway, Station, TunnelBridge}` 的白名单 ⇒ 车底停在普通轨道 / 路点 / 站台上都能给出目标格，车库仍走 `GetCoupleIsDepot()` 分支。
2. `UpdateOrderDest()` 的 `OT_GOTO_COUPLE` 分支（`:4703` 附近）把 `station_dest` 从**先决条件**降级成**回退**：
   ```cpp
   const TileIndex couple_dest = R3RGetCoupleOrderDestination(v);
   const TileIndex station_dest = v->GetOrderStationLocation(order->GetDestination().ToStationID());
   v->SetDestTile(couple_dest != INVALID_TILE ? couple_dest : station_dest);
   ```

### B：`src\pbs.cpp`

1. 新增 `#include "couple_group.h"`。
2. 新增文件内 `static bool R3RReservationHeldByCouplePartner(TileIndex tile)`（在 `TryReserveRailTrack` 之前）：
   - 取 `r3r_resv_actor`（`Vehicle::GetIfValid` + `type == Train`）→ `First()` 的 `current_order.IsType(OT_GOTO_COUPLE)`；不满足即 `false`；
   - `R3RGetCouplePairPartner(head)` 必须给出**双向活锁**的伙伴，否则 `false`；
   - plain rail：`GetRailReservationTrackBits` 的**每一个**已订位都必须由 `GetTrainForReservation(tile, t)` 追溯到同一个伙伴，任何一位查不到主人或主人不是伙伴即 `false`；
   - station/waypoint：`HasStationRail && HasStationReservation` 且 `GetTrainForWholeTileReservation(tile)->First() == partner`；
   - 其余 tile 类型（车库/道口/隧道桥/非 rail 站台）一律 `false`。
3. 只在**原本就会失败**的两处接入（不改变"能订就订"的正常路径）：
   - plain rail：`TryReserveTrack()` 返回 `false` 之后追加豁免，命中即 `return true`，并打
     `RESV-PAIR-AIR veh= tile=x,y track= kind=rail`；
   - station：`HasStationRail && !HasStationReservation` 分支未命中的情况下追加同一豁免，kind=`stn|wpt`。
   - **不写地图**：位仍归车底，由车底自己释放；本车只是"被允许穿过"。

### 与 §3 风险的对照

- **R-1**：A 只改 `dest_tile`（普通寻路与 PBS 目的地），不改挂接寻路自身的目标选择（`FindNearestCoupleTrain` 仍自己找车底）⇒ 老的"目标格被占导致没停稳"那条路径不受影响。
- **R-2**：`CheckTrainsOnTrack` 与位级互斥一行未动；B 是"失败后按配对关系豁免"，且判据全部来自存档字段（`r3r_couple_target/requester`）。
- **R-3**：**刻意不额外要求对方 `OT_WAIT_COUPLE`**。理由：纯车底（car-only formation）是 `R3RIsCoupleTarget` 的合法目标（KI-194 `PFD-ACCEPT kind=car-only|waiting`），它的 `current_order` 常常不是 `WAIT_COUPLE`，加这道门会把本场景直接排除；而"双向活锁"本身已经保证对方不会朝本车开（纯车底无订单、等待方只有 `WAIT_COUPLE`/`OT_NOTHING`）。

## 7. 构建自证（2026-10-28）

- 复用 `_tmp_inc_build.cmd`（未新建 `.cmd`），后台 `Start-Process cmd /c _tmp_inc_build.cmd`。
- 第一次启动时机器上那份日志其实是上一轮残留（`done` 02:25:49、只编了 `train_cmd.cpp`），而源码 mtime 是 02:33:31/02:34:10 ⇒ **判据必须看时间戳**，随即重跑。
- 重跑结果：`GUARD: incremental is safe`；`[2/4] pbs.cpp.obj`、`[3/4] order_cmd.cpp.obj`、`[4/4] Linking CXX executable openttd.exe`；`build\R3R_incbuild.done` = `EXIT_CODE=0`（02:41:38）。
- 时间戳：`src\pbs.cpp` 02:34:10 → `pbs.cpp.obj` 02:39:38（3 137 387 B）；`src\order_cmd.cpp` 02:33:31 → `order_cmd.cpp.obj` 02:39:38（5 737 083 B）；`build\openttd.exe` **02:41:14（52 344 832 B）**，晚于两个 obj。
- 错误计数 0；`read_lints` 两文件 0 条；exe 内字面量 `RESV-PAIR-AIR` 命中（按 28591 编码读字节检索）。

## 8. 状态

- 2026-10-28：取证完成（R1–R6），**A/B 均已落地并编译通过**（A = `src\order_cmd.cpp`，B = `src\pbs.cpp`，均为 .cpp ⇒ 增量合法）。KI 条目已写入 `R3R_KNOWN_ISSUES.md` 末尾「第 345 轮」小节（KI-512 = A，KI-513 = B，含构建自证）。
- **游戏内复测待做**，判据见 §4 与两条 KI 的复测判据：正判据=订单目的地=车底所在格 + 出现 `RESV-PAIR-AIR` 且 `FSCP fail=tryReserve` 消失 → `COUPLE-OK`；负判据=非配对列车占位仍 `fail=tryReserve`、车库/道口/隧道桥逐字不变、KI-341 走错岔道不复现、KI-509/KI-359 P1/KI-357/KI-511 不回退。
- 重大修正：§2 的候选落点 A 应为 `src\order_cmd.cpp` 的 `R3RGetCoupleOrderDestination()` / `UpdateOrderDest()`（不是 `train_cmd.cpp`）；§1 的"现状疑点"由 R3 结论取代。
