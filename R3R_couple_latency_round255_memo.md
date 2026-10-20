# R3R 挂接延迟（双方到位 → 开始挂接）取证与优化（第 255 轮）

## 一、问题重定义（玩家 2026-10-20 纠偏）

玩家原话：「我的意思是这个只是优化 arrange train 那边的消耗吗，我想的是为什么每一次双方列车到位之后到成功配对开始执行挂接要那么久」。

⇒ 这不是性能问题，是**行为层的等待时长**问题。上一轮做的 KI-385（把 `R3RReleaseChainReservations()` 的全图 419 万格扫改成遍历 `own_path.tiles`）发生在**挂接判定已经成功、正在提交那一刻**，是一次性单帧成本，**不参与任何排队**，因此与玩家所报的延迟无关。KI-385 保留（它是纯收益且等价），但它不是这个问题的答案。

## 二、已确认的根因锚点

### 2.1 主因：停稳在目的地后，候选搜索每 500 tick 才放行一次

`src\train_cmd.cpp:12204-12206`（`TrainCoupleHandler()` 内，滚动检查之后、候选搜索之前）：

```cpp
if (!r3r_rolling && R3RCoupleOrderDestinationReached(v)) {
    if (((uint32_t)_tick_counter + v->index.base()) % R3R_COUPLE_DEST_IDLE_LIMIT != 0) return false;
}
```

常量：`src\train_cmd.cpp:12052`

```cpp
static const uint32_t R3R_COUPLE_DEST_IDLE_LIMIT = 500; ///< ~15 s at normal game speed.
```

⇒ 一旦机车**已停在 GOTO_COUPLE 的目的地**（`R3RCoupleOrderDestinationReached(v)` 为真）且速度归零（`!r3r_rolling`），候选搜索（含 `CPL-*` 整条寻路与 `TryTrainCouple`）**只在 `(_tick_counter + index) % 500 == 0` 的那一个 tick 执行**，其余 499 个 tick 直接 `return false`。

这是相位门，不是计数器 ⇒ 新到达的机车要等到的下一个 500 网格点，最长 500 tick。

### 2.2 量化（按常速 30 tick/s）

| 情形 | tick | 墙钟（常速） |
|---|---|---|
| 最好（到达瞬间恰好是网格点） | 0 | 0 s |
| 平均 | 250 | ≈ 8.3 s |
| 最坏 | 500 | ≈ 16.7 s |

注释自陈 `~15 s at normal game speed`（`:12052`）。

### 2.3 叠加因素：每次搜索失败都要再等一整窗

- 那唯一一个放行的 tick 里若 `u == nullptr`（没找到候选），下一次尝试要再等 500 tick。
- 若找到候选但合并提交失败，`R3R_COUPLE_COMMIT_FAIL_LIMIT = 4`（`:12086`）——注释自陈「each one a parked-wait window apart, i.e. R3R_COUPLE_DEST_IDLE_LIMIT ticks at minimum」（`:12080-12082`）⇒ **4 次失败 = 约 2000 tick ≈ 67 s**。
- 另有 `R3R_COUPLE_SCAN_RETRY = 8`（空手后 8 tick 冷却，日志 `CPL-SKIP retryIn=`）与 `R3R_PAIR_RESCAN_TICKS = 8`（配对锁重扫），量级远小于 500，非主因。

### 2.4 设计动机（为什么当初加这个门）

`src\train_cmd.cpp:12037-12044` 的注释：不做节流的话，停稳的机车会**每 tick** 跑 depot 扫描 + `TryTrainCouple`，既烧掉列车 tick，又会「沿站台漂移」（`drifting along the platform`）。第 109 轮现场证据：`veh=24` 在 21,9 有 27 次 `CPL-ENTRY`、7 次 `COUPLE-FAIL`、0 次 `CPL-BEST/CPL-RESERVE`，同时 `veh=30` 从 60,30 一路漂到 58,38。

⇒ **这个门是为「无目标的空转」设的，但它的代价被施加到了「目标就在眼前」的情形上**——这是可分离的，也是优化的着力点。

## 三、猜测根因（待验证）

**猜测 1（主，高置信）**：延迟主因 = 500 tick 相位门（2.1）。判据：现场日志中 `COUPLE-OK loco=` 与其前一条 `CPL-*`/`TTB-*` 的时间差应接近 500 的整数倍；`COUPLE-DEST-EMPTY` 的间隔应恒为 500 tick。

**猜测 2（待验证）**：`R3RCoupleOrderDestinationReached(v)` 的判据可能「过早为真」——若车底还在移动、或按 depot/station id 判到点而非格，则机车可能在自己还没真正贴靠时就进入等待窗，把一次本可立即成功的挂接推迟 500 tick。

**猜测 3（待验证）**：到达瞬间若不在网格点上，即便目标就在同一格也必等。这是纯相位效应，与目标状态无关。

## 四、优化候选（未落地，待拍板）

- **方案 A（首选，最小改动）**：给相位门加「脏放行」——当目的地附近/配对目标出现可检测的变化时（例如目的地格上出现新车辆、或 `_r3r_couple_pair` 锁刚被建立），允许立即放行一次搜索，仍保留 500 tick 作为兜底轮询。效果：目标到位的那个 tick 就能挂上，同时保留防漂移。风险：需要一个廉价判定（否则就退回每 tick 全扫，等于撤销这个门）。
- **方案 B**：把 500 改为「前 N 次失败用短间隔（如 10 tick），连续失败后指数退避到 500」。效果：常态 10 tick（≈0.3 s）内挂上，空转仍被抑制。
- **方案 C**：只降低常量（如 500 → 60）。最简单，但会同比放大「无目标空转」的扫描次数与漂移风险。
- **不建议**：直接删掉这个门（第 109 轮现场会复现漂移与空转）。

## 五、补充取证（2026-10-20）

### 5.1 目的地判据过宽（证实猜测 2 的一半）

`src\train_cmd.cpp:12126-12136`：

```cpp
static bool R3RCoupleOrderDestinationReached(const Train *v)
{
	const Order &order = v->current_order;
	if (!order.IsType(OT_GOTO_COUPLE)) return false;
	const Train *front = v->GetMovingFront();
	const TileIndex tile = (front != nullptr) ? front->tile : v->tile;
	if (order.GetCoupleIsDepot()) {
		return IsRailDepotTile(tile) && GetDepotIndex(tile) == order.GetDestination().ToDepotID();
	}
	return HasStationTileRail(tile) && GetStationIndex(tile) == order.GetDestination().ToStationID();
}
```

⇒ 车站场景下只要车头压在**目的车站任意一格的轨道上**就算「到达」，与车底相距多少格无关。所以「进站即进入 500 tick 等待窗」——即便它离目标还有一整个站台。

### 5.2 `r3r_rolling` 的定义（关键）

`src\train_cmd.cpp:12179`：

```cpp
const bool r3r_rolling = v->cur_speed != 0;
```

⇒ 门只在**停稳**（`cur_speed == 0`）时生效。行驶中的机车**每 tick** 都跑完整搜索与挂接（`:12242-12246`、`:12285-12289` 的注释与分支都明说 rolling 路径常驻）。

### 5.3 最刺眼的一处：折叠修正回滚后的「从停稳重试」正好落在这个门后面

`src\train_cmd.cpp:12405-12410`：

```cpp
	Couple(v, u);
	if (r3r_rolling) {
		/* R3R (KI-106 fix 1) probe: ... merged tells whether the merge
		 * actually committed -- a fold-fix rollback leaves the two chains apart and
		 * the loco is stopped on the exact pixel, retrying from standstill. */
```

注释自己写明：**折叠修正回滚后，两链分离、机车停在精确像素上，然后「从停稳状态重试」**。而停稳状态的重试间隔正是 `R3R_COUPLE_DEST_IDLE_LIMIT = 500`。

⇒ **一次折叠修正失败 ⇒ 等最多 500 tick（≈16.7 s 常速）才重试一次**。`R3R_COUPLE_COMMIT_FAIL_LIMIT = 4` 意味着最坏 4 个窗口 ≈ 2000 tick ≈ 67 s 后放弃订单。

这几乎必然是玩家所报「双方到位后要等很久」的直接来源：机车已经贴到目标、一次尝试因折叠修正回滚，然后就在原地干等 500 tick。

## 六、结论

| 层 | 机制 | 量级 |
|---|---|---|
| 1 | 行驶中每 tick 尝试挂接（rolling 路径） | 0 延迟 |
| 2 | 一旦停稳且车头在目的站任意格 → 500 tick 相位门 | 平均 250 tick ≈ 8.3 s，最坏 500 tick ≈ 16.7 s |
| 3 | 那次尝试若因折叠修正回滚 → 再等一整窗 | 每次失败 +500 tick |
| 4 | 连续 4 次未提交 → 放弃并跳过订单 | ≈ 2000 tick ≈ 67 s |

⇒ 玩家感受到的延迟 = 层 2 × （失败次数）。**KI-385 的全图扫不与这条链路相交**（它在层 3 成功提交之后）。

## 六之二、场景精确澄清（玩家 2026-10-20 第二次纠偏，**推翻 §二 的适用范围**）

### 玩家原话
「A车已经处于等待挂接的状态，B车已经到达机待线处于寻找符合列车前往车站挂接的状态，A和B是符合挂接配对的条件的，但是无论如何，B总是在一段时间之后才开始行驶前去挂接A……而且我的朋友报告，在超大地图中，这个间隔时间会变长。」

### 与 §二 的差别（重要）
§二 描述的 500 tick 相位门判据是 `!r3r_rolling && R3RCoupleOrderDestinationReached(v)`。而本场景中 **B 停在「机待线」，A 在「车站」**，B 的 GOTO_COUPLE 目的地是 A 的车站。因此：

- 若机待线与目的站台**同属一个 StationID**（大站多站台 + 机待线的常见布局）⇒ `HasStationTileRail(tile) && GetStationIndex(tile) == ToStationID` 为**真** ⇒ 500 tick 门**生效** ⇒ 每 500 tick 才允许搜一次 ⇒ 精确解释「总是过一段时间才开动」。
- 若机待线是**独立站/侧线**（不同 StationID）⇒ 门**不生效** ⇒ B 每 tick 都跑 `TrainCoupleHandler` ⇒ 延迟必须由别的机制解释（候选识别门槛、搜索失败重试、或搜索成本）。

**⇒ 这是本轮第一个必须区分的前提，不能二选一硬套。需要向玩家确认机待线是否与目的站台同站，或从行为日志反推。**

### 「超大地图间隔变长」的新线索（决定性）
§二/§六 的量化是**固定 tick 数**（500），与地图尺寸无关，因此**无法解释地图越大越久**。必须找到与地图尺寸正相关的量，候选假设：

1. **搜索失败率随距离上升**：YAPF 上限 `max_search_nodes` 默认 10000（`aystar.h:28`），大地图上 B→A 距离远 ⇒ 单次搜索更易耗尽节点 ⇒ 失败 ⇒ 门在搜索**之前**，失败即再等一整窗 ⇒ 平均等待 ≈ 500 × (1/p)。
2. **每 tick 成本随地图增大**：固定 500 tick 的墙钟时长被拉长（若同时存在全图/全车遍历）。
3. **全车遍历**：`R3REnsureCouplePair()` 的 `Train::Iterate()`（`:11841`）有 `R3R_PAIR_RESCAN_TICKS = 8` 节流，大地图车辆多 ⇒ 单次更贵。
4. **周期性全图审计**：`R3RReservationAudit()`（`:15544`）节流 2048/8192 tick，大地图 419 万格/次 ⇒ 周期性卡顿。

**必须用同一场景在大/小地图上对比 tick 差（游戏内时间），而不是墙钟，否则无法区分 1 与 2。**

### 待验证清单（本轮）
- V-1：B 停稳在机待线、`u == nullptr` 时，`TrainCoupleHandler` 返回到谁、谁负责让 B 最终开动（`TrainLocoHandler` 停驶分支是否完全不寻路）。
- V-2：`ChooseTrainTrack()` 里 `OT_GOTO_COUPLE` 的分支（KI-316「no candidate anywhere, driving to the order destination instead」）触发条件与频率。
- V-3：搜索失败是否真的导致「再等一整窗」（门的 return 在搜索之前，需确认没有旁路）。
- V-4：`max_search_nodes` 是否被 R3R 改过、挂接寻路是否另有节点/距离上限。
- V-5：机待线与目的站的 StationID 关系（需玩家确认布局）。

## 六之三、本轮取证结果（2026-10-20 第二次，B 在机待线场景）

### 已确认 A：B 在机待线时不吃 500 tick 门

`src\train_cmd.cpp:12355-12359` 的注释是作者自己写下的实测——

```cpp
/* R3R (KI-330, U-2): read-only diagnosis. A GOTO_COUPLE loco with no
 * candidate anywhere reaches this branch on every tick while it is still
 * *off* its order destination, so the KI-193 parked-wait gate above never
 * engages: no heartbeat is written and the full map-wide candidate scan
 * runs every tick (the 2026-10-06 report had veh=57 frozen here). */
```

⇒ **B 在机待线（off destination）时，500 tick 门不生效**。§二 / §六 的结论对 B 不适用，那是「A 侧」或「已停进目的站」的情形。

### 已确认 B：B 无候选时被禁止开往目的地，只能原地等

`src\train_cmd.cpp:14355-14362`：

```cpp
if (path_found == INVALID_TRACK) {
	/* R3R (KI-316, round 214): ... has been removed on
	 * the player's ruling of 2026-10-03: a locomotive that finds nothing to
	 * couple onto must NOT set off towards the order destination, it must
	 * keep waiting where it stands -- on the loco siding -- until a consist
	 * turns up. */
```

⇒ **这是玩家自己 2026-10-03 定的规则**。它直接决定了 B 的启动必须依赖「某次重试恰好找到候选」。

### 已确认 C：B 的重试走的是引擎的 Stuck 退避，不是 R3R 自己的节流

`src\train_cmd.cpp:18887-18908`：

```cpp
if (!mode && consist->flags.Test(VehicleRailFlag::Stuck)) {
	++consist->wait_counter;
	...
	bool turn_around = consist->wait_counter % (_settings_game.pf.wait_for_pbs_path * DAY_TICKS) == 0 && _settings_game.pf.reverse_at_signals;
	if (r3r_heal_exhausted) turn_around = false;
	if (!turn_around && consist->wait_counter % _settings_game.pf.path_backoff_interval != 0 && consist->force_proceed == TFP_NONE) return true;
	TryPathReserveResultFlags path_result = TryPathReserveWithResultFlags(consist);
```

`path_backoff_interval` 默认值 **20**（`src\table\settings\pathfinding_settings.ini:125  def = 20`，单位见 `src\settings_type.h:792` 注释 `ticks between checks for a free path`）⇒ 正常应为每 20 tick 重试一次 ≈ 0.67 s。

### ⚠ 矛盾点（本轮尚未解决，禁止当作结论）

20 tick ≈ 0.67 s **无法解释**玩家描述的「很久」与「超大地图更久」。因此必有一个压制因素，候选：

1. **`mode` 为真时整块跳过**（`:18888` 的 `if (!mode && ...)`）——若 B 在机待线以 shunt 模式运行，Stuck 重试**完全不发生**，`wait_counter` 也不递增，直到某个事件把 `mode` 翻回来。这是最可能把 0.67 s 变成数分钟/无穷的开关。
2. **`DoTrainCouplePathfind` 持续返回 INVALID_TRACK**（A 尚未被判为合法候选）：A 必须已是 `OT_WAIT_COUPLE`（`R3RCanCoupleNow`，`:11744`）、未 Stopped、同 couple group、且站在 B 订单指向的站（`R3RCoupleTargetAtOrderStation`，`:11687`）。任一不满足 ⇒ 每 20 tick 白试一次，累计即「很久」。
3. **配对锁竞争**：`R3REnsureCouplePair()` 每 `R3R_PAIR_RESCAN_TICKS = 8` tick 重扫 + KI-182 的进入顺序优先戳（`R3RCoupleEnterTick`，`:6829`）⇒ A 可能先被别的机车锁住，B 排队。
4. **大地图相关**：每次 `DoTrainCouplePathfind` 是 YAPF 搜索（`max_search_nodes` 默认 10000，`aystar.h:28`），B→A 距离远 ⇒ 单次更贵/更易打满 ⇒ tick 变慢 + 失败率上升。这可以解释「超大地图更久」，但与 1/2/3 的贡献需日志分离。

## 六之四、实机日志实测（2026-10-20 第三次，玩家提交 R3R_debug.log）

### 日志量与 tick 跨度

文件 `D:\temp\codebuddy-dropped-files\95866110-2269-46b3-a8ae-5f13d8b0f21c\R3R_debug.log`，74 674 B / 865 行。tick 锚点（`RESV-AUDIT-ARM/DUE/WAIT/SUMMARY` 的 `tick=`）：

- L3 `tick=1417`、L90 `tick=1792`、L147 `tick=1920`、L153/163 `tick=2048`、L239 `tick=3072`、L302 `tick=4096`、L456 `tick=5120`、L853/863 `tick=6144`。
- 全日志覆盖 **tick 1417 → 6144（4727 tick）**，平均 **≈2.58 tick/行**（397 行对应 1024 tick，L456→L853）。

### B 车（veh=0）这一轮的完整行为链（行号为权威锚点）

| 行 | 探针 | 读数 | 判定 |
|---|---|---|---|
| L593 | `CPL-PAIR act=0 tgt=3 dist=5 actTile=200,378 tgtTile=201,374` | B 与 A 相距 **5 tile** | 已配对，未贴合 |
| L594 | `DEPOTBLK veh=0 ... spd=59` | 仍在滑行 | — |
| L626 | `REVERSEDIR veh=0 tile=200,378 dir=3 order=16 spd=0 nv=3 rev=0 stuck=0 db=0` | **掉头** | 不是等待 |
| L636 | `TPR-WALK veh=0 mf=200,378 origin=200,378 td=9 okay=0 ... spd=0` | 寻路首次失败 | — |
| L637 | `CPL-ENTRY veh=0 tile=200,378 orderType=16 dontReserve=0` | 进入挂接寻路 | — |
| L639-641 | `PFD-ACCEPT kind=car-only tile=201,370 t=3 ordType=17 wc=1` | A 被判为合法候选 | 候选识别**没卡** |
| L642 | `CPL-PATHFOUND veh=0 found=1` | 找到路径 | — |
| L643 | `CPL-SCAN veh=0 found=1 closed=3 limit=10000 enter=9 noplat=3 ...` | **只展开 3 个节点** | 小场景成本极低 |
| L645 | `CPL-BEST veh=0 tile=201,370 td=9 cost=6442` | — | — |
| L681 | `CPL-RESERVE veh=0 reserved=1 next=9` | 挂接路径预留成功 | — |
| L682 | `TRP veh=0 mf=200,378 real=2 type=16 dest=189641 spd=0 fp=0 look=1 one=0 mstuck=0 fto=1 => ok=1 res=1` | 普通预留也成功 | **B 在这一 tick 被放行** |
| L683 | `COUPLE-FAIL loco=0 order=16 tx=200 ty=378` | 未挂上（还差 5 格） | — |
| L688 | `COUPLE-WAIT-OFFDEST veh=0 tile=200,378 mf=200,378 real=2 isDepot=0 destDepot=0 **destStn=0 destTile=189641** onDepotTile=0 **onStnTile=1**` | **车在站台格上，但订单目的地站不是当前站** | 见下节 |
| L693 | `LOCO-MOVE veh=0 tile=200,378 j=218 spd=61` | 开始行驶 | — |
| L737 | `COUPLE-PREFLIGHT head=0 last=2 fold=0 dirfold=0 gap=3 ... spd=92 db=1` | 已贴到 gap=3 | — |
| L742 | `COUPLE-OK loco=0 rear=8 consist=3 co=1 real=4 type=2` | **挂接成功** | — |

⇒ **L593 到位 → L742 挂上 ≈ 149 行 ≈ 380 tick ≈ 13 秒（常速 30 tick/s）**。这段时间里 B **一直在掉头、寻路、预留、行驶**（L626/L636/L637-681/L693 都有动作），**不存在"原地干等 500 tick"的形态**。500 tick 门在这一轮**从未生效**。

### 决定性读数：B 处于 off-destination，与 `:12355-12359` 注释完全对应

- L688 明确 `destStn=0` 而 `onStnTile=1`，`destTile=189641`。
- `189641` 按 2048 宽推算 = **y=92, x=1225**；B 实际在 **(200,378)**，A 在 **(201,374)**。
- ⇒ **B 的 `OT_GOTO_COUPLE` 订单目的地是 (1225,92) 的车站，而真正要挂的 A 就在它旁边 5 格**。`R3RCoupleOrderDestinationReached()`（`:12126-12136`，判据 `GetStationIndex(tile) == ToStationID`）因此**恒为 false** ⇒ 500 tick 相位门**永不生效** ⇒ B 每 tick 都跑 `TrainCoupleHandler` + 候选搜索。
- 这正是 `:12355-12359` 作者注释描述的分支：「a GOTO_COUPLE loco with no candidate anywhere reaches this branch on every tick while it is still *off* its order destination, so the KI-193 parked-wait gate above never engages … the full map-wide candidate scan runs every tick」。

⇒ **§二/§六 的 500 tick 结论在本场景完全不适用**（已第三次修正，前两次都套错）。

### 疑点 REG-255-A（必须登记）：`RESERVECONSIST veh=3..8 ok=0 resNow=1`

L615-620 与 L694-699 两组，逐车一行：

```
RESERVECONSIST veh=3 tile=201,374 track=0x2 fb=0 bits=0x2 ok=0 resNow=1
RESERVECONSIST veh=4 tile=201,374 track=0x2 fb=0 bits=0x2 ok=0 resNow=1
RESERVECONSIST veh=5 tile=201,373 track=0x2 fb=0 bits=0x2 ok=0 resNow=1
RESERVECONSIST veh=6 tile=201,373 track=0x2 fb=0 bits=0x2 ok=0 resNow=1
RESERVECONSIST veh=7 tile=201,373 track=0x2 fb=0 bits=0x2 ok=0 resNow=1
RESERVECONSIST veh=8 tile=201,373 track=0x2 fb=0 bits=0x2 ok=0 resNow=1
```

A 的 6 节车底脚下轨道**已处于预留状态**（`resNow=1`），但它自己的 `ReserveTrackUnderConsist` **每次都失败**（`ok=0`）。`veh=3` 的 tile `201,374` 正是 `CPL-PAIR tgtTile=201,374`。

两种可能，**未定**：
1. 这是 A 自己合法的 PBS 预留（它也在等/在跑），`ok=0` 只是"已订过不重复订"的正常幂等返回 ⇒ 无害。
2. 这是**幽灵预留**（KI-203/KI-345 同族），占位者不是 A 本身 ⇒ 会干扰挂接寻路"跟随等待列车预留"（`:14300-14306` 注释：couple pathfinder 依赖 `the waiting train's reservation`）。

**待验证判据**：L615 附近是否伴随 `RESV-GHOST` / `RESV-AUDIT-STRAY`（本日志 `strayTiles=0`，倾向可能 1，但 `RESERVECONSIST` 的 `resNow` 来源尚未确认）。

### 「超大地图间隔更长」的机制归因（**不再是猜测，有代码依据 + 日志量化**）

| 机制 | 代码依据 | 与地图尺寸的关系 |
|---|---|---|
| **① 每 tick 候选扫描（主因）** | `:12242-12246`、`:12285-12289` 在 off-destination 停稳时每 tick 调用；`:12355-12359` 注释自陈「runs every tick」 | B→A 距离越远，YAPF 展开节点越多。本日志 `CPL-SCAN closed=3 enter=9`（小场景）；大地图 A 远则 `closed` 数量级上升 |
| **② tick 变慢拉长墙钟** | `R3RReservationAudit()` 每 2048/8192 tick 全图扫（`:15544`，本日志 L90/L100/L302/L853 可见 `RESV-AUDIT-SUMMARY ... tick=`） | 419 万格/次，大地图单次更慢 ⇒ 同样 380 tick 的墙钟被拉长 |
| **③ 每次失败再走一轮** | `R3R_COUPLE_SCAN_RETRY = 8`、`COMMIT_FAIL_LIMIT = 4` | 与地图无关，但次数会被①②放大 |

⇒ **「大地图更久」= ①的单次成本 × ②的tick膨胀**，不是某个固定 tick 常量随地图变化。这与 §六之二 的假设 1/2 一致，现由日志坐实①，②有周期探针佐证。

## 七、待办

1. 取证 `R3RCoupleOrderDestinationReached()` 的实现（判断它何时为真）。
2. 取证 `r3r_rolling` 的来源（是否要求 `cur_speed == 0`）。
3. 确认「配对锁」与「候选出现」是否有现成的廉价信号可作脏放行来源。
4. 与玩家确认取舍（等待时长 vs 空转开销）后再落地。

## 八、方案（2026-10-20，待玩家评估；**未落地**）

### 8.0 根因口径（三次修正后的最终版）

1. **500 tick 相位门不是「大地图更久」的解释**。它只对「停稳 + `R3RCoupleOrderDestinationReached()` 为真」生效；本场景 B 在机待线/站台格但 `off-destination`（L688 `destStn=0 onStnTile=1 destTile=189641`），门全程未参与。L593→L742 ≈380 tick 内 B 一直在掉头/寻路/预留/行驶，无「原地干等」形态。
2. **与地图规模正相关的量只有两条**：
   - **R1 单次候选搜索成本**：`DoTrainCouplePathfind` 是 YAPF，无 `max_cost`、节点上限 10000（`aystar.h:28`），范围 = 从 B 出发的可达路网 ⇒ 距离越远单次越贵、越易打满。
   - **R2 tick 膨胀**：`R3RReservationAudit()` 每 2048/8192 tick 全图扫 419 万格；`R3RPurgeStrayReservations()` 同量级。同样的 tick 数在大地图上墙钟更长。
3. **最大不确定性（必须先量化）**：本场景 `CPL-SCAN closed=3 enter=9`，R1 在小场景几乎零成本。R1 在玩家的大地图场景里是否真的变重，**尚无实测**。若 R1 贡献很小，则优化 YAPF 预算是白做，真正的杠杆全在 R2。

### 8.1 判别实验（决定后续方案取舍，零行为变更）

- 记两个量：`T_tick` = `CPL-PAIR`（双方到位）→ `COUPLE-OK` 的 tick 差；`W` = 同一区间墙钟。
- **大图 vs 小图同场景各跑一次**：
  - `T_tick` 明显变大 ⇒ R1/失败重试为主要矛盾 ⇒ 优先 P3、P2。
  - `T_tick` 基本不变而 `W` 变大 ⇒ tick 膨胀为主要矛盾 ⇒ 优先 P1。
- 需要的只读探针（P0，仅 `src\train_cmd.cpp`）：
  - `CPL-PAIR` 行补 `tick=`；
  - `CPL-SCAN` 行补 `tick=` 与 `nodes=`（closed+enter）；
  - `RESV-AUDIT-SUMMARY` 补 `scanned=` 与 `us=`（单次审计耗时）。
- 判据：同一场景前后两版日志里，`T_tick`、`CPL-SCAN nodes`、`audit us` 三个数可直接对比。

### 8.2 方案清单（按建议实施顺序）

**P1 —— 审计台账化（治 R2，对全部大地图玩家普适）**
- P1-1（主选）：`R3RReservationAudit()` 改遍历预留台账（`_r3r_resv_owner_of_bit` / `R3RL` 正向表），只对**在位预留**逐条验主；台账为空（旧档）时退化为 P1-2。
- P1-2（兜底）：分段扫描，每 tick 固定预算（如 16384 格），419 万格 ≈ 256 tick 走完，单帧成本封顶；保留每 8192 tick 一次的全量兜底以捕获台账漂移。
- 收益：单帧成本 O(地图) → O(在位预留数)（千级）。
- 风险：台账漏记 ⇒ 漏审漏 reap（有全量兜底）；需确认读档重建路径（`Load_R3RL` 已有）。
- 判据：`RESV-AUDIT-SUMMARY` 的 `us` 降一个数量级以上；`strayTiles` 与 reap 结果逐条不变；单帧最长停顿不再随地图尺寸增长。

**P2 —— 候选搜索「确定性退避 + 事件唤醒」（治 R1 与「过一会才开动」）**
- P2-1 唤醒源（全部由游戏状态派生，读档可重建，**不得用本地计数器跨档累计**）：
  a) 新车辆进入 B 当前格邻域/订单站（走格哈希链，O(1)）；
  b) 某链从非 `WAIT_COUPLE` 变为 `WAIT_COUPLE`（`R3REnsureCouplePair` 已在遍历，可顺手置代次）；
  c) 配对锁建立/变更；
  d) B 的 `current_order` 变更。
- P2-2 节流：`off-destination` 停稳且上次搜索 `found=0` ⇒ 退避 8→20→40→…→250（封顶），用 `(_tick_counter + index) % interval` 判定相位（确定性）；任一唤醒源置脏 ⇒ 下一 tick 立即放行并重置退避。
- P2-3 「找不到候选」与「找到但提交失败」分开计退避；提交失败仍受 `COMMIT_FAIL_LIMIT=4` 约束，但不吃「空手」的长退避。
- 风险：唤醒源漏覆盖 ⇒ 延迟退回 500 tick 量级（需保留周期性兜底轮询）；停稳时不再每 tick 尝试，**漂移风险反而下降**（第 109 轮的漂移正是每 tick 全扫引起）。
- 判据：`CPL-SCAN` 出现频率从每 tick 降到退避节奏；`T_tick` 不增（目标一出现即开动）；`CPL-PAIR→首次 CPL-ENTRY` ≤ 1 tick。

**P3 —— YAPF 分级预算（治 R1 单次成本）**
- 首档 `max_search_nodes=1500`，失败升 5000→10000（档位由链状态确定性派生）；**升档不算一次失败重试**。
- 收益：大地图常态搜索不再一上来吃满 10000 节点。
- 风险：远目标需多档，若与 P2 退避叠加会拉长总时长 ⇒ 必须「升档当 tick 立即重试」；`max_cost` 仍不设（避免漏掉远目标）。
- 判据：`CPL-SCAN limit=` 出现 1500/5000/10000 三档；近目标仍一档命中；`COUPLE-OK` 不减少。

**P4 —— 收紧 500 tick 门判据（只治 on-destination 场景）**
- 判据从「车头在目的站任意格」改为「车头在目的站 **且** 与配对目标距离 > K 格 **且** 无候选」；已贴靠（`CPL-PAIR dist<=K`）时立即放行，500 tick 仅作无目标兜底。
- 收益：消除「已进站/已贴靠却被门拦 8~16 s」。
- 风险：第 109 轮漂移复现（`veh=30` 沿站台漂）⇒ 前置「停稳 + 已有配对」降低风险。
- 判据：`CPL-PAIR dist<=K` 后下一 tick 即 `CPL-ENTRY`；无配对时仍按 500 tick 轮询；无新增漂移。

### 8.3 明确不做
- 删除 500 tick 门、把常量全局改小（漂移 + 空转，第 109 轮现场）。
- 挂接路径缓存复用（失效判据复杂、desync 风险高）。
- 改 `CheckTrainsOnTrack`/PBS 反 desync 冻结区（第 221 轮结论）。
- 任何「随地图尺寸调整 tick 常量」的做法（同一存档跨机器会行为不一致 ⇒ desync）。

### 8.4 推荐组合
`P0（量化，1 轮）→ P1-1 + P2（主力）→ P3 → P4（按 P0 读数决定是否做）`。
若 P0 显示 `T_tick` 与地图无关而 `W` 显著变大，则 P3/P4 降级为可选，集中做 P1。

## 九、玩家拍板（2026-10-20）与本轮落地范围

### 9.1 拍板结果

- **做 P0 + P1 + P2**（P3/P4 本轮不做，等 P0 读数）。
- **P1 走「花名册」**（预留台账驱动），不用纯分段扫描做主力。
- **P2 的退避封顶取值**：玩家原话「我都不知道为什么他会出现在这里，估计是之前留下的屎山代码，后面我是期望清理的（最好写入备忘里，说明这是待办）」。

### 9.2 口径澄清（重要，避免误读成"沿用旧常量"）

玩家把两个不同东西混在一起了，备忘必须写清：

1. **现行 500 tick 相位门**（`R3R_COUPLE_DEST_IDLE_LIMIT=500`，第 109 轮 KI-193 引入）——这是**存量机制**，也是玩家口中的"屎山"，本轮**不动它**（清理留待办 9.3）。
2. **P2 的退避封顶**——这是**本轮新引入的参数**，与上面 500 无关。本轮不沿用 250/500 这种随手值，改为**单一命名常量**（`R3R_COUPLE_RETRY_BACKOFF_MAX`），并且退避序列由移位派生，不再散落魔法数。取值按"目标出现即可在 1 tick 内响应"的判据反推，与"最多省多少空转"解耦。

### 9.3 待办：清理遗留 phase/idle 魔法数（玩家明确要求写入备忘）

状态：**待办，未做**。清理对象（相互独立，可分批）：

| 位置 | 现状 | 建议 |
|---|---|---|
| `TrainCoupleHandler` 到点相位门 | `R3R_COUPLE_DEST_IDLE_LIMIT=500`（约 16 s 才允许一次搜索） | 与 P2 退避统一到同一套"事件唤醒 + 退避"框架，门可删除；删除前先确认 `R3RCoupleOrderDestinationReached()` 的唯一用途是否只剩此门 |
| 审计周期 | `R3R_RESV_AUDIT_TICKS=2048` / `_AUDITS=3` / 脏标记 256 三档 | 台账化（P1）落地后，稳态周期可上调（全量兜底 ≠ 主路径），常量归一到一处 |
| 清理残留预留 | `R3RPurgeStrayReservations` 全图扫 | 与 P1 同源：能走台账就不要再全图扫 |
| `COMMIT_FAIL_LIMIT=4` 等散落重试常量 | 与"空手退避"混用 | P2-3 已把两种失败分开计数，后续可归一 |

**清理原则**（写进备忘，防止将来乱改）：不允许"按地图尺寸调整 tick 常量"（跨机器行为不一致 ⇒ desync）；任何周期/封顶值必须走 `(_tick_counter + index) % interval` 确定性相位；删除存量门之前必须先补"事件唤醒"覆盖，否则会从"空转"变成"永远不动"。

### 9.4 本轮实施清单

- **P0（只读）**：`CPL-PAIR` 补 `tick=`；`CPL-SCAN` 补 `tick=` 与 `nodes=`（closed+enter）；`RESV-AUDIT-SUMMARY` 补 `scanned=` 与 `us=`。
- **P1**：审计改遍历预留台账（花名册），台账为空/旧档退化；单 tick 定额分段兜底，禁止单帧全图扫。
- **P2**：`off-destination` 停稳且 `found=0` ⇒ 移位退避，封顶单一常量；唤醒源（新车上站/目标变 WAIT_COUPLE/配对锁变更/订单变更）置脏即下一 tick 放行。
- 判据见 §8.2 各项，另加 P0 的判别实验口径（同场景大小图各跑一次，比 `T_tick` 与 `W`）。

---

## 十、落地记录（P2 收尾，2026-10-20）

### 10.1 实施形态（最终）

| 项 | 落点 | 形态 |
|---|---|---|
| P2 相位门 | `src\train_cmd.cpp` `TrainCoupleHandler()` | `const bool r3r_scan_due = (((uint32_t)_tick_counter + v->index.base()) % R3R_COUPLE_SCAN_PHASE) == 0;` 声明在 `bool reverse = false;` 之前（开放轨道分支与贴靠扫描都要看见它） |
| P2 只闸加宽扫描 | 同上，3×3 贴靠兜底 | `if (u == nullptr && !r3r_rolling && (!r3r_off_dest || r3r_scan_due))`，`r3r_off_dest = !R3RCoupleOrderDestinationReached(v)` |
| P2 常量 | `src\train_cmd.cpp:12060-12096` | `static const uint32_t R3R_COUPLE_SCAN_PHASE = 8;`（由初版 `R3R_COUPLE_RETRY_BACKOFF_MAX = 256` 改名并改值，理由见 10.2） |
| KI-385 | `R3RReleaseChainReservations()` | 已改为 `for (TileIndex t : own_path.tiles)`，全图 4 194 304 格外壳删除 |
| P0 | `CPL-PAIR`/`CPL-SCAN`/`RESV-AUDIT-SUMMARY` | `tick=`、`nodes=`、`scanned=`、`us=` 已补 |
| P1 | `R3RReservationAudit()` + 新 `R3RResvSweepSlice()` | 审计走台账花名册（`R3RResvLedgerForEachBit` + sort/unique）；无台账的幽灵位由每 tick 定额 8 192 格的环形扫掠兜底 |

### 10.2 与 §9.4 原案的三处偏离（都有实测/结构理由，不是偷懒）

1. **没有实现"事件唤醒"，退化为纯相位门**。理由：相位取 8 tick（≈0.27 s），恰好等于 `R3RCoupleScanHold` 的空转重试间隔 ⇒ 贴靠扫描永远不会比"真正会让机车重新动起来的那次寻路"更慢；再叠加唤醒源只会增加一个必须跨机同步的脏标记（又一次 desync 面），收益为零。
2. **常量 256 → 8，并改名 `SCAN_PHASE`**。初版取 256（≈8.5 s）是按"退避封顶"想的，但这条扫描的典型触发形态是**机车停在离车底差几个像素处**（会合的常见结局）。8.5 s 的隐形等待在玩家眼里就是"挂车又坏了"，而 8 tick 的代价是每 8 tick 只做一次 9 格哈希查询——本来也不贵。名字里的 `BACKOFF` 一并去掉：这里**没有**退避（不随失败次数增长），叫 backoff 会误导后来者去加计数。
3. **没有加"未到点且非相位就提前 return false"**。原计划有，读完分支后判定为有害且无收益：
   - 真正的全图候选搜索**不在** `TrainCoupleHandler()` 里，而在 `YapfTrainCoupleTrack()`，且已被 `R3RCoupleScanHold()` 节流到空手后 8 tick —— 旧注释（"the full map-wide candidate scan runs every tick"）是错的，本轮已在两处订正；
   - 提前 return 会跳过 `_r3r_couple_no_target[v->index] = 1` 与目的地心跳记账，而 stuck 自愈正依赖这份记账（只写不删 ⇒ 少写会改变自愈触发节奏）；
   - 剩下的尾部工作全是常数级赋值/边沿日志，省不出可测量的时间。

### 10.3 口径订正（重要，防止下一轮再被旧注释带偏）

`TrainCoupleHandler()` 里两处注释原文分别声称"off-destination 时全图候选搜索每 tick 都跑"，实为**错误**：
- 全图量级的候选搜索只有 `YapfTrainCoupleTrack()`/`FindNearestCoupleTrain()`，它的入口 `CPL-SCAN` 由 `R3RCoupleScanHold()` 在空手后节流 8 tick（`yapf_rail.cpp`），不是每 tick；
- `TrainCoupleHandler()` 自身每 tick 做的是：库格出口/本格哈希链两格（常数级）、`FollowTrainReservation`（沿本车预留走，链长级）、3×3 贴靠扫描（9 格）——即"常数级 + 链长"，与地图尺寸无关。
⇒ 因此"大地图更慢"的成因不是这里的遍历规模，而是 **(a) tick 本身变慢（P1 的审计全图扫描是最大一项）；(b) 大地图上 A↔B 距离变大，同一段 tick 的路径搜索/跟随成本上升**。两处注释已改写为这个口径。

### 10.4 构建自证

两轮，均复用既有 `_tmp_inc_build.cmd`（**未新建任何 .cmd**）：

| 轮次 | 触发 | 结果 | 产物 |
|---|---|---|---|
| 第一轮（含 P0/P1/P2 初版 + KI-385） | 上一轮改了 `src\pbs.h` ⇒ 护栏判 REMOVED 620 obj 转**全量** | `[692/692] Linking CXX executable openttd.exe`、`build\R3R_incbuild.done` = `EXIT_CODE=0`、`error C*`/`fatal error`/`FAILED:`/`build stopped` 计数 **0** | `build\openttd.exe` @ 2026-10-20 14:38:09（52 003 328 B） |
| 第二轮（P2 收尾：改名 + 8 tick + 去掉遮蔽声明 + 注释改写） | 护栏 `GUARD: incremental is safe` | `[3/3] Linking CXX executable openttd.exe`、`EXIT_CODE=0`、错误计数 **0** | `src\train_cmd.cpp` 14:46:44 → `train_cmd.cpp.obj` 14:47:20 → `build\openttd.exe` @ 2026-10-20 14:47:26（52 003 328 B） |

`read_lints` = 0 条。本轮未新增日志字面量（仅改常量值与注释），故不做 exe 串核对。

### 10.5 待复测判据（玩家侧）

1. **负判据（不回归）**：机车滚动过程中贴靠/命中行为逐字不变——rolling 时本就不跑 3×3 扫描（KI-106 fix 1），本改动不触碰该分支。
2. **正判据（省掉 7/8 的扫描）**：off-destination 停稳的 GOTO_COUPLE 机车，日志中 `CPL-PAIR` 心跳间隔由 1 tick 变为**成簇出现**（每 8 tick 一簇，簇内仍逐 tick），且 `COUPLE-WAIT-OFFDEST` 仍按边沿输出、不消失。
3. **延迟判据**：机车停稳在"差几像素"位置时，从停稳到 `COUPLE-OK` 的 tick 差 ≤ 8（≈0.27 s）；若实测出现明显 >8 tick 的干等，说明门的位置伤到了会合路径，需回退这条门。
4. **到点场景不变**：停在订单目的地（`R3RCoupleOrderDestinationReached()` 为真）的机车完全不受影响，仍按 `R3R_COUPLE_DEST_IDLE_LIMIT = 500` 的既有节奏（本次刻意不在 500 门之上再叠一个不对齐的 8 tick 相位）。
5. **确定性**：两台机器同存档，`COUPLE-OK` 发生的 tick 一致（相位只由 `_tick_counter` 与 `v->index.base()` 决定，无本地状态）。
6. **P1/KI-385 侧**：`RESV-AUDIT-SUMMARY` 新出现 `scanned=`（≈台账款，数千级）与 `us=`；挂接/解挂提交点不再出现全图扫（墙钟应随地图增大而不变）。
