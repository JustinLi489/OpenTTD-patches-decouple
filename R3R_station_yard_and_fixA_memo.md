# R3R 备忘：KI-118 修法A（`r3r_parked`）与「车站站场」（station yard）

- 日期：2026-09-19（第 68 轮）
- 分支：`feature/decouple`
- 状态：**已全部实现（第 68–69 轮，2026-09-19）并编译验证** —— 「修法A」与「车站站场」P1+P2+P3 均已落地（定稿见 §4，落点见 §4.7），产物 `build\openttd.exe` @ 2026-09-19 21:12:23（620/620 obj 全量重编一致，见 `R3R_KNOWN_ISSUES.md` 末尾「第 69 轮构建验证与状态更新」附节）。本文保留设计依据、数据模型与落点清单，供回归、实测与复盘查阅。
- 剩余待办：仅 §4.8 的**运行时实测**（需人工交互，AI 无法代跑）。

---

## 0. 两件事与结论速览

| 事项 | 工作量 | 是否需全量重编 | 结论 |
| --- | --- | --- | --- |
| KI-118 修法A（独立标志 `r3r_parked`） | **小**（1 个新字段 + 4 处小改 + 1 个新 SLV 版本） | 是（碰 `src/train.h`） | 可立即实现 |
| 车站站场（A场 / B场 / AB共用场） | **大**（新持久化结构 + 订单目的地扩展 + YAPF 代价 + 平台选择 + 存档 + UI） | 是 | 需分期，先行拍板 |

> 用户原话的条件是「如果引入站场和修法A工作量都不大，就两个都改完再编译」。**经验收，站场不满足「工作量不大」**，故本轮先出备忘 + 实现修法A，站场按 §2.10 分期排期。

---

## 1. KI-118 修法A：车库「停放豁免」改用独立标志

### 1.1 问题回顾（摘自 `R3R_KNOWN_ISSUES.md` KI-118）

- `TrainLocoHandler()` 车库挂车块（`src/train_cmd.cpp:10555-10653`）为了让「被 R3R 车库工具停在库里」的列车仍能执行 `GOTO_COUPLE`，采取了两项措施：
  1. 用 `r3r_pending_depot_couple`（`train_cmd.cpp:10570-10574`）绕开「停止列车 bail out」门（`10579`）；
  2. 在每次尝试前**无条件**清掉 `VehState::Stopped` 并把 `orders[cur_real_order_index]` 的 `GOTO_COUPLE` 填进 `current_order`（`10625-10631`、`10640-10641`）。
- 后果：任何停在「`GOTO_COUPLE` 目标车库」内的机车（R3R 停的 / 玩家按停止的 / 新建后从未启动的）都会每 tick 触发挂车。KI-60 宣示的「双方均须启动」在库内退化为三条件。
- 根因：代码无法区分「R3R 工具停的」与「玩家意图停的」——两者共用同一个 `VehState::Stopped`。

### 1.2 修法A 设计（语义）

新增 `Train::r3r_parked`（SAVED）：

> 该标志表示「本车的 `VehState::Stopped` 是 R3R 车库编辑工具自动停的，不是玩家意图」。

豁免（清 `Stopped` + 由 `orders` 按索引取 `GOTO_COUPLE` 填入 `current_order`）**只对 `r3r_parked == true` 的列车开放**；玩家按「停止」或新建未启动的列车 `r3r_parked == false`，走原「停止列车 bail out」路径。

### 1.3 行为矩阵（目标）

| 列车 | `Stopped` | `r3r_parked` | `orders[i]` 是 `GOTO_COUPLE` 且指向本库 | 期望 |
| --- | --- | --- | --- | --- |
| R3R 工具停在库里（`MakeSegment`/`DemoteSegment`） | 1 | **1** | 是 | 豁免 → 就地挂车 |
| 玩家按「停止」的机车 | 1 | 0 | 是/否 | bail out，**不**挂车 |
| 新建后从未启动的机车 | 1 | 0 | 是 | bail out，**不**挂车 |
| 运行中到达目标库执行 `GOTO_COUPLE` | 0 | 0 | 是（`current_order` 已由 `ProcessOrders` 填入） | 正常挂车（走既有 `couple_targets_this_depot` 第二分支） |
| 停在自家库、`GOTO_COUPLE` 指向别处 | 0/1 | 0 | 否 | 行为不变 |

### 1.4 改动清单（可执行）

1. **`src/train.h`** — `struct Train`（`:158` 起）内新增字段（放在 R3R `weight_override` 组附近 `:214-216`）：
   ```cpp
   /* R3R KI-118 fix A: set when this train was auto-stopped in a depot by R3R's
    * own depot editing tools (R3RStopChainInDepot via MakeSegment/DemoteSegment).
    * The in-depot GOTO_COUPLE exemption in TrainLocoHandler is granted only to a
    * train carrying this flag, so a train the player stopped -- or a freshly
    * bought, never-started train -- no longer auto-couples inside its target
    * depot. Cleared when the player starts/stops the train manually, and when
    * the couple it was parked for succeeds. */
   bool r3r_parked = false;
   ```

2. **`src/sl/saveload_common.h`** — 在 `SLV_R3R_ARTIC_OVERRIDE`（`:438`）之后新增版本号，并更新 `SAVEGAME_VERSION`（`:467`）：
   ```cpp
   SLV_R3R_PARKED,   ///< R3R: Train::r3r_parked (KI-118 fix A).
   ```
   ```cpp
   static constexpr SaveLoadVersion SAVEGAME_VERSION = SLV_R3R_PARKED;
   ```
   > 注意：`SLV_R3R_ARTIC_OVERRIDE` 已是现存版本号，若直接复用会让同版本号的旧 R3R 存档布局错位，**必须**新开一个版本号。

3. **`src/sl/vehicle_sl.cpp`** — `_vehicle_train_sl_desc`（`:203-212`）末尾追加：
   ```cpp
   SLE_CONDVAR(Train, r3r_parked, SLE_BOOL, SLV_R3R_PARKED, SL_MAX_VERSION),
   ```
   （`SLE_BOOL` 已确认在 `src/sl` 各表中可用。）

   > 排查中发现的既存问题（未修，不属本轮范围）：`Train::weight_override / power_override / max_speed_override` 只登记在 `src/saveload/vehicle_sl.cpp` 的 `_vehicle_train_sl_desc`，而该文件整体位于 `namespace upstream_sl`、只在 `_sl_upstream_mode`（读原生 OpenTTD 存档）时使用；当前 JGRPP 格式表 `src/sl/vehicle_sl.cpp` 的 `_train_desc` 里没有这三个字段 ⇒ 它们在正常 R3R 存档中不会被持久化。

4. **`src/vehicle_cmd.cpp` `R3RStopChainInDepot()`（`:328-343`）** — 在置 `Stopped` 处同时置标志：
   ```cpp
   t->StopSeparation();
   t->vehstatus.Set(VehState::Stopped);
   t->r3r_parked = true;   // R3R KI-118 fix A
   ```
   > 函数开头的 `if (t->vehstatus.Test(VehState::Stopped)) return;` **保留不动**：列车本来就停着时，其 `Stopped` 是玩家意图，不该被贴上「R3R 停的」标签。

5. **`src/train_cmd.cpp`**
   - `r3r_pending_depot_couple` 定义（`:10571`）加入 `consist->r3r_parked`：
     ```cpp
     const bool r3r_pending_depot_couple = consist->IsEngine() && consist->r3r_parked && r3r_pend != nullptr &&
             r3r_pend->IsType(OT_GOTO_COUPLE) && r3r_pend->GetCoupleIsDepot() && ...;
     ```
   - 挂车成功分支（`:10643-10646`）清标志：
     ```cpp
     if (r3r_coupled) {
         consist->cur_speed = 0;
         consist->progress = 0;
         consist->r3r_parked = false;   // R3R KI-118 fix A
     }
     ```

6. **`src/vehicle_cmd.cpp` `CmdStartStopVehicle()` execute 分支（`:1224` 起）** — 玩家显式 start/stop 视为「接管」，清标志：
   ```cpp
   if (v->type == VehicleType::Train) Train::From(v)->r3r_parked = false;   // R3R KI-118 fix A
   ```
   放在 `v->vehstatus.Flip(VehState::Stopped);`（`:1229`）附近。

### 1.5 为什么选择「存档」（而不是 NOSAVE）

- `VehState::Stopped` 本身是存档字段。若 `r3r_parked` 不存档，则「工具停在库里 → 存档 → 读档」后该列车会丢失豁免能力：它 `Stopped=1`、不跑 `ProcessOrders`、`current_order` 恒为 `OT_NOTHING`，于是**永远挂不上车**（回归）。
- 反正修法A 已经必须碰 `src/train.h` ⇒ 必须全量重编，多一个 SLV 版本与一行 `SLE_CONDVAR` 不增加成本。

### 1.6 待复测点

1. 库里用「设为段 / 降为段」停下的列车，若其 `orders` 含指向本库的 `GOTO_COUPLE`，仍能自动挂上。
2. 新建机车（`Stopped=1`）+ 一条指向本库的 `GOTO_COUPLE` ⇒ **不再**自动吸附。
3. 玩家按「停止」的机车（`Stopped=1`）在目标库内 ⇒ 不挂车；按「启动」后恢复正常。
4. 「工具停的列车 → 存档 → 读档 → 自动挂车」仍成功。
5. 运行中到达目标库执行 `GOTO_COUPLE` 的机车行为无回归。

---

## 2. 车站站场（station yard）

### 2.1 需求（用户原话转写）

- 一个车站可分为 **A场 / B场 / AB共用场**。
- A场与B场**只有寻路目的地不同**，其余一切属性（名字、货物、接驳、统计、基建归属）**完全共用**。
- AB共用场是**独立寻路目的地**；前往 A/B 场的列车**也可以**寻路到 AB 共用场，但承受**巨大寻路惩罚**。
- 惩罚的定性要求：**前往 A 场的车只有在 A 场满了的时候才会寻路到 AB 共用场**（不量化，只要效果正确）。

### 2.2 现状：为什么原生/当前 R3R 不支持

- 车站的铁路部分只有一个外接矩形 `BaseStation::train_station`（`src/base_station_base.h:91`），**没有**任何「平台组 / 场」的持久化结构。
- 「平台」（platform）是**运行时现算**的：靠 `IsCompatibleTrainStationTile()`（`src/station_map.h:567-574`）从某个 tile 沿轴向外扫，得到连续段。没有平台列表，也没有平台 id。
- 订单目的地是 `Order::dest`（`DestinationID`，`src/order_base.h:136`），`OT_GOTO_STATION` 语义 = `StationID`，**没有**第二维度。
- 寻路目标：`CYapfDestinationTileOrStationRailT`（`src/pathfinder/yapf/yapf_destrail.hpp:121-226`），`SetDestination()`（`:140`）读 `current_order.GetDestination().ToStationID()`；`PfDetectDestination()`（`:183-196`）把「落在该站任意 tile」都算到达。
- 「空闲平台计数」目前只有 `GetFreeStationPlatforms(StationID)`（`src/order_cmd.cpp:3693-3718`），遍历平台端点 + `GetStationReservationTrackBits`，且**仅被条件订单使用**。

⇒ 结论：**必须新增「场」这一维度**（持久化结构 + 订单字段 + 寻路代价 + 选择逻辑 + UI），不是改几行能解决的。

### 2.3 数据模型（建议）

在 `Station`（`src/station_base.h:891`）上新增：

```cpp
struct StationYard {
    uint8_t  id = 0;                  ///< 1..N，0 保留给「无场（整站）」。
    std::string name;                 ///< 可选，默认 "A"/"B"/"共用"。
    bool     is_shared = false;       ///< true = AB 共用场（对 A/B 目的地只作兜底，带巨大惩罚）。
    std::vector<TileIndex> tiles;     ///< 属于本场的铁路 tile（必须是本车站的铁路 tile）。
};
std::vector<StationYard> yards;       ///< R3R: 车站内的寻路场地划分；空 = 现状（整站单目的地）。
```

不变量：
- `yards` 为空 ⇒ 行为与现在**完全一致**（零回归）。
- 同一 tile **不得**同时属于两个场；未列入任何场的 tile 视为「自由平台」（可被任意场目的地使用，也可禁止——见 Q3）。
- 场只在**铁路**车站上有意义。

### 2.4 订单目的地扩展

- `Order::dest` 语义不变（仍是 `StationID`）。
- 新增 `uint8_t r3r_yard = 0;`（`src/order_base.h` `struct Order`，`:135-139` 附近），仅对 `OT_GOTO_STATION` 有意义；0 = 整站。
- 存档：`GetOrderDescription()`（`src/sl/order_sl.cpp:174-202`）是**三处共用**的 desc（全局 `ORDR`、`Vehicle::current_order`、`REF_ORDER`），加一行 `NSL("r3r_yard", SLE_CONDVAR(Order, r3r_yard, SLE_UINT8, SLV_R3R_YARD, SL_MAX_VERSION))` 即可（实现时需核对三处确实共用同一 desc）。
- 语义：
  - `r3r_yard == 0`：整站（现状）。
  - `r3r_yard == k`：目的场 = 场 k；兜底 = 该站的 shared 场。

### 2.5 寻路代价与到达判定

- **目的场判定**：`PfDetectDestination()`（`yapf_destrail.hpp:183-196`）目前只判「tile 属于本车站」。建议保持「到达」宽判（落到站内即到达），把场地约束放在**代价**与**最终平台选择**两处，避免破坏既有到站/进站流程。
- **代价惩罚**：`PfCalcCost()`（`src/pathfinder/yapf/yapf_costrail.hpp:573`）的车站分支（`:752-759` 站内 pass-through；`:924-935` 目标车站的平台长度/扣回）加入：
  - tile/平台 ∉ 目的场 且 ∉ 兜底共用场 ⇒ `+YAPF_INFINITE_PENALTY` 量级（等同禁止，仅当目的场满时才发现无解）；
  - tile/平台 ∈ 兜底共用场 ⇒ `+YAPF_INFINITE_PENALTY` 量级但**有限**（例如 `4 * YAPF_INFINITE_PENALTY` 的量级），确保「目的场有空 ⇒ 绝不选共用场」。
  - 常量参考：`YAPF_TILE_LENGTH = 100`、`YAPF_INFINITE_PENALTY = 100000`（`src/pathfinder/pathfinder_type.h:17,28`）。
- **为什么是「发现无解再兜底」而不是简单加权**：YAPF 的代价是局部累加，只要目的场存在**任意**可达平台，其代价必然远低于任何带 `YAPF_INFINITE_PENALTY` 的路径，因此「满了才去共用场」自然成立。真正要额外处理的是**目的场全满**（此时到目的场的路径代价同样带惩罚、路径不存在），需要在 `ChooseRailTrack`（`src/pathfinder/yapf/yapf_rail.cpp:1170`）失败时**降级**重试「目的场 + 共用场」并集，或干脆在代价模型里把「目的场已满」当作允许降级（推荐后者：把惩罚移进「平台的占用/预留代价」，与 `rail_pbs_station_penalty` 同层）。
- **平台选择**：`TrainEnterStation()`（`src/train_cmd.cpp:8426`）与 R3R platform-waiter gate（`:10521-10553`）需按场过滤候选平台。

### 2.6 「场满」判定

- 复用/扩展 `GetFreeStationPlatforms()`（`src/order_cmd.cpp:3693-3718`）为「按场」版本：统计某场内的平台中，`GetStationReservationTrackBits()` 表明仍可预留的平台数。
- 原子判定可用 `IsRailStationPlatformFree()`（`src/pbs.h:22` / `pbs.cpp:90`）与 `HasStationReservation()`（`src/station_map.h:582-586`）。
- 「满」的定义需拍板（见 Q2）。

### 2.7 存档

- 站场定义：**新增独立 R3R chunk**（如 `STYD`），每车站一条 `yards` 列表，范式照抄 `src/sl/couple_group_sl.cpp` 的 `CGVR`（`NSL(...)` 表 + 独立 chunk，**不动**上游 `STNN` 的布局）。
- 版本门：`SlXvFeatureTest(XSLFI_R3R_STATION_YARD, 0, 1)`，并按 §1.4-2 的方式新增 `SLV_R3R_YARD` 版本号。
- 订单字段走 `GetOrderDescription()`（§2.4）。

### 2.8 UI（工作量大头）

1. **车站窗口**：新增「站场」管理页 —— 把平台（或 tile）划入 A/B/共用场；重命名；删除场。
2. **订单窗口**：`OT_GOTO_STATION` 加一个「场」下拉（默认「整站」）；共享排程下需考虑一致性。
3. **车站列表 / 详情**：显示场名（可选）。
4. 中文/英文字符串（`src/lang/*.txt`），注意改语言文件后的 `strings.h` 重编坑（记忆 52814982）。

### 2.9 与现有 R3R 机制的交互风险

- **platform reservation（`SetRailStationPlatformReservation`）**：按场过滤时不要把共用场的预留算进目的场。
- **`loading_vehicles` / `Vehicle::LeaveStation()`**：不受场影响（车站仍是同一个），但要确认 `last_station_visited` 语义不被波及。
- **`CalcClosestStationTile()`**：`dest_tile` 由车站算出，若按场目的地需要落到该场内，需要按场改写（注意别影响寻路器的「到达」判定）。
- **`GetFreeStationPlatforms` 只服务条件订单**：若站场也用它，需评估性能（每 tick 调用点）。

### 2.10 分期与工作量

| 阶段 | 内容 | 估计 | 备注 |
| --- | --- | --- | --- |
| P1 | 数据结构 + 存档（`STYD` + `SLV_R3R_YARD`）+ 订单字段 + `GetFreeStationPlatforms(by yard)` | 中 | 无 UI，可用命令/测试代码建场 |
| P2 | YAPF 代价 + 平台选择 + 到达判定（含「满→兜底」） | 中 | 站场真正的行为核心 |
| P3 | UI（车站窗口建场 + 订单选场）+ lang | 大 | 交互设计需用户参与 |

**整体：中-大（数人日量级），不属于「工作量不大」。**

### 2.11 需要拍板的问题

- **Q1**：场如何**物理定义**？① 玩家手选平台/tile 划入（最灵活，需要 UI）；② 每个平台自动成为一个场（简单，但 A/B 语义受限）；③ 用**站牌/路点**等既有物件标记（无新 UI，但语义别扭）。
- **Q2**：「A 场满」的判据？① 该场全部平台被**预留/占用**；② 该场存在列车**停留**即视为满；③ 两者组合。
- **Q3**：AB 共用场是 ① 显式的一组平台；还是 ② 「车站内未被划入 A/B 的其余平台」？
- **Q4**：未划入任何场的 tile 行为？（禁止 / 任意场可用）
- **Q5**：是否**只对火车**生效（公路/船/飞机不做）？
- **Q6**：惩罚量级是否接受「`YAPF_INFINITE_PENALTY` 的固定倍数」这种不量化实现？

---

## 3. 本轮结论

1. **修法A 已具备可执行清单**（§1.4），可与站场任一阶段**合并为同一次全量重编**，避免重复支付 25–40 分钟的构建成本。
2. **站场显著超出「工作量不大」**：它需要新的持久化维度 + 订单字段 + 寻路代价 + 平台选择 + UI，建议按 §2.10 分期；P1/P2 可先做（不带 UI），P3 需用户参与交互设计。
3. 站场实现前请先回答 §2.11 的 Q1–Q6。

---

## 4. 拍板结果与实现定稿（2026-09-19）

### 4.1 用户对 §2.11 的回答

- **Q1（物理定义）= ①**：玩家在**车站窗口**里手选平台/tile 划入 A场/B场/共用场（需要新 UI）。
- **Q3（共用场）= ①**：共用场是**显式指定的一组平台**，和 A/B 一样由玩家划入（不是「其余平台」）。
- **Q2（满判定）= ①**：某场「满」= 该场内**全部平台**都被预留/占用（`GetStationReservationTrackBits` / `HasStationReservation` 逐平台判，与现有预留机制一致）。
- **新增强约束（性能）**：**绝不允许列车在大老远就每 tick 疯狂遍历站台**。⇒ 所有「场/平台」扫描**只允许发生在「列车真正选平台的那一刻」一次**。
- **范围 = P1+P2+P3 一起做**（含建场 UI、订单选场、双语字符串），交付即完整可用。

### 4.2 关键实现决策（相对 §2.5/§2.7 的修订）

| 项 | 原稿建议 | **定稿** | 理由 |
| --- | --- | --- | --- |
| 场地约束落在哪 | YAPF `PfCalcCost` 加 `YAPF_INFINITE_PENALTY` | **`PfDetectDestination` 过滤**（不改代价） | 代价法要调参、会波及「站内 pass-through」代价；过滤法零调参、且天然满足「选平台那一刻算一次」的性能约束 |
| 订单字段 | `Order::r3r_yard` 新字段 + 新 SLV 版本 | **复用 `xdata2` 第 1–2 位**（bit0 已被 `SetReverseAtStation` 占用，`order_base.h:584`） | 主串已开先例（`GetCoupleSlot()` 走 `xdata`）；**免改存档格式、免新 SLV 版本、免动 ORDR desc** |
| 站场定义存档 | 新 chunk `STYD` | **新 chunk `SYRD`**（照抄 `couple_group_sl.cpp` 的 `CGVR` 范式） | 独立 chunk ⇒ **完全不碰上游 STNN 布局**，也**不需要 SLV 版本号** |
| tile 编码 | `TileIndex \| yard<<24` | **按场各存一个 `std::vector<TileIndex>`**（`SLE_VARVEC(..., SLE_UINT32)`） | 24 位放不下 XL map（`XSLFI_EXTRA_LARGE_MAP` 可到 8192²）；三个具名 vector 最稳，且 `SL_VARVEC` 对 `vector<uint32_t>` 是已验证路径 |

### 4.3 数据模型（`src/station_base.h`，`struct Station`）

```cpp
/* R3R station yard (站场). 场选择器取值：0 = 整站(无场)，1 = A场，2 = B场，3 = 共用场。 */
std::vector<TileIndex> r3r_yard_a{};      ///< R3R: 划入 A场的铁路 tile
std::vector<TileIndex> r3r_yard_b{};      ///< R3R: 划入 B场
std::vector<TileIndex> r3r_yard_shared{}; ///< R3R: 划入 AB 共用场
```

成员/自由函数（声明在 `station_base.h`，定义在 `station_cmd.cpp`）：

- `std::vector<TileIndex> &Station::R3RYardTiles(uint8_t yard)` / const 版
- `uint8_t Station::R3RGetYardOfTile(TileIndex) const`（0 = 未划入，**二分查找**，向量恒保持 sorted+unique）
- `void Station::R3RSetTileYard(TileIndex, uint8_t yard)`（先从三场摘除，再插入并按场排序去重）
- `void Station::R3RRemoveTileFromYards(TileIndex)`（车站 tile 被拆时修剪）
- `bool Station::R3RHasAnyYard() const`
- `void R3RCollectPlatformTiles(TileIndex, std::vector<TileIndex> &out)`（沿 `GetRailStationAxis` + `IsCompatibleTrainStationTile` 双向扫出整条平台）
- `bool R3RStationYardIsFull(const Station *, uint8_t yard)`（**一次性**逐平台判预留/占用）

不变量：三场皆空 ⇒ 行为与现状**逐字节一致**（零回归）；同一 tile 不同时属于两场；只对铁路车站有意义。

### 4.4 订单侧（`src/order_base.h`）

```cpp
/** R3R：GOTO_STATION 的目的场（0=整站）。占用 xdata2 位 1..2；位 0 是 SetReverseAtStation。 */
inline uint8_t GetR3RYard() const { return (uint8_t)GB(this->GetXData2Low(), 1, 2); }
inline void    SetR3RYard(uint8_t yard) { SB(this->GetXData2Ref(), 1, 2, yard); }
```

已核对：`xdata2` 在 `OT_GOTO_STATION` 上**仅** bit0 被 R3R 站台调向占用，其余位只被 `OT_CONDITIONAL` 的条件（站点/调度槽）使用 ⇒ 位 1–2 安全。

### 4.5 YAPF（`src/pathfinder/yapf/yapf_destrail.hpp`，`CYapfDestinationTileOrStationRailT`）

- 新增成员：`uint8_t dest_yard_sel; std::vector<TileIndex> dest_yard_tiles; bool dest_yard_active;`
- `SetDestination()`（`:140`）在 `OT_GOTO_STATION` 分支后**一次性**算出允许 tile 集：
  - `GetR3RYard() == 0` 或车站无任何场 ⇒ `dest_yard_active = false`（完全走原生）。
  - 否则 `allowed = tiles(场)`；若 `R3RStationYardIsFull(st, 场)` ⇒ `allowed ∪= tiles(共用场)`。
  - 排序去重、写入 `dest_yard_tiles`，`dest_yard_active = true`。
- `PfDetectDestination(tile, td)`（`:183`）在原有「属于本车站 + track 匹配」通过后，追加
  `if (dest_yard_active && !std::binary_search(...)) return false;`
- **性能**：`SetDestination` 每次寻路调用一次（非每 tick、非每节点）；`PfDetectDestination` 每节点只做一次二分查找。**不存在**「大老远每 tick 遍历站台」。
- **无死锁**：占用/预留**不**参与到达判定（与原版一致），列车照常进站排队；`allowed` 永不为空（场无 tile 时退回原生）。

### 4.6 UI

- **车站窗口**：新增 `WID_SV_R3R_YARDS` 按钮 → 打开新窗口 `StationYardWindow`（`station_gui.cpp`）。窗口按**平台**列表展示本铁路车站的所有平台，每行一组按钮 `[A场][B场][共用场][移出]` + `[定位]`（`ScrollMainWindowToTile` 把主视窗移到该平台）。点击即写入 `Station::R3RSetTileYard`。列表/高亮方案避免做地图 overlay，风险最低。
- **订单窗口**：`OT_GOTO_STATION` 行增加「场」下拉（`整站/A场/B场/共用场`），走既有 `OrderList` 的 `MOF_*` 下拉机制。
- **双语**：`src/lang/english.txt` 与 `src/lang/simplified_chinese.txt` 加字符串；改语言 txt 后须强制重编 `strings.cpp`（记忆 52814982）。

### 4.7 落点清单（实现顺序）

1. `src/station_base.h`（数据 + 声明）
2. `src/station_cmd.cpp`（实现 + 车站 tile 变动时修剪）
3. `src/sl/station_yard_sl.cpp`（新，`SYRD` chunk）+ `src/sl/CMakeLists.txt` + `src/sl/saveload.cpp` 注册
4. `src/order_base.h`（`GetR3RYard/SetR3RYard`）
5. `src/pathfinder/yapf/yapf_destrail.hpp`（过滤）
6. `src/widgets/station_widget.h` + `src/station_gui.cpp`（建场 UI）
7. `src/order_gui.cpp`（选场 UI）
8. `src/lang/*.txt`（字符串）
9. 全量重编（**改动了 `src/*.h` ⇒ 必须删全部 `*.obj` 全量重编**，记忆 66636022）

### 4.8 已知风险 / 待复测（实现时同步登记 `R3R_KNOWN_ISSUES.md`）

- 场 tile 在车站扩建/拆除后的修剪（必须无悬垂 tile，否则 `PfDetectDestination` 误判）。
- 场全满 → 回落共用场 → 共用场也满时的排队行为（预期与原版一致：站外 PBS 等待）。
- 未划入任何场的平台**不**被带场订单使用（需在 UI 上说明）。
- 订单换类型（GOTO_STATION → 其他）时 `SetR3RYard` 位残留：`MakeOrder`/`AssignOrder` 会重置 xdata2，需实测确认。
- 共享排程（`IsOrderListShared`）下选场一致性。

---

## 5. 第 70 轮修订：动态 N 场（**取代 §4 的固定三场**）

§4.3 / §4.6 / §4.7 里所有「A场 / B场 / 共用场」的固定三项表述**已作废**，实现改为「每车站动态 N 场」。
§2.11 的拍板结论（Q1=玩家在车站窗口手选平台划入、Q3=共享场是显式指定的一组平台、Q2=场满=场内平台全被占用、性能上「只允许在选平台那一刻扫一次」）**全部保留不变**，只是「场」的数量不再固定为 3。

### 5.1 数据模型（实际落地：`src/station_base.h`，`struct Station`）

```cpp
struct R3RStationYard {
    std::vector<TileIndex> tiles; ///< 本场的铁路车站 tile（升序、无重复）
    bool shared = false;          ///< 共享场：任何场停满时都可回落至此
};
std::vector<R3RStationYard> r3r_yards{};   ///< 场的列表（有效场 ID = 下标 + 1）

static constexpr uint16_t R3R_YARD_NONE = 0;     ///< 整站（不限定场）
static constexpr uint16_t R3R_MAX_YARDS = 65534; ///< 场数量上限（订单 16 位存储）
static constexpr uint16_t R3R_YARD_A = 1, R3R_YARD_B = 2, R3R_YARD_SHARED = 3; ///< 仅 v1 兼容
```

- **场 ID 稳定性（硬约束）**：场**只追加、从不删除**；「删场」= `R3RClearYard()` 清空内容而**不移除下标**，保证已有订单引用的场 ID 永不失配。`R3RAddYard()` 返回新场 ID 或 `R3R_YARD_NONE`（满）。
- 接口（声明 `station_base.h` / 实现 `station_cmd.cpp`）：`R3RNumYards()`、`R3RYardTiles(yard)`、`R3RAddYard(shared)`、`R3RClearYard(yard)`、`R3RIsYardShared(yard)`、`R3RSetYardShared(yard, shared)`、`R3RGetYardOfTile(tile)`（二分）、`R3RSetTileYard(tile, yard)`、`R3RRemoveTileFromYards(tile)`、`R3RPruneYardTiles()`、`R3RStationYardIsFull(st, yard)`、`R3RCollectYardDestinationTiles(st, yard, out)`。
- 旧成员 `r3r_yard_a` / `r3r_yard_b` / `r3r_yard_shared` 保留（仅为不改 `SYRD` 字段布局），语义见 5.2。

### 5.2 存档：`SYRD` v2（同一 chunk、同一字段布局）

- **v1**：三场分别直接存进 `yard_a` / `yard_b` / `yard_shared`；订单场 ID 0/1/2/3 = 整站/A/B/共享。
- **v2**：全部场数据编码进 `yard_a`，`yard_b` / `yard_shared` 恒空。编码（每项一个 32 位 word）：
  `word0 = #SYRD_V2_MAGIC`（大于任何合法 tile 序号）、`word1 = 场数 N`（低 16 位有效），之后 N 组 `[共享标志][tile 数 M][M 个 tile]`。
- 读档：`yard_a` 以 MAGIC 开头 ⇒ v2 解码；否则按 v1 迁移 —— **A→场1、B→场2、共用→场3(shared)，恒等映射**，旧档里订单引用的场 ID 继续有效。
- 保存时 `r3r_yard_a` 兼作编码缓冲，`SlObjectSaveFiltered` 之后立即 `clear()` 释放。
- 兼容细节：订单侧 `GetR3RYard()` 先读 `xdata2` **高 16 位**（新编码），为 0 时回落到 `xdata2` 低半的 **bits 1..2**（v1 编码）；`SetR3RYard()` 会先清掉那段 legacy 位，避免回落读到陈旧值。

### 5.3 其余落点（相对 §4.7 的差异）

| 文件 | 变化 |
| --- | --- |
| `src/station_cmd.h` | `SetR3RStationYard` → `CmdDataT<StationID, TileIndex, uint16_t>`；`SetR3RStationYardShared` → `CmdDataT<StationID, uint16_t, bool>`（yard 由 2 位升 16 位） |
| `src/pathfinder/yapf/yapf_destrail.hpp` | `dest_yard_sel` 升 `uint16_t`，允许 tile 集改由 `R3RCollectYardDestinationTiles()` 收集；`PfDetectDestination` 仍只做一次 `std::binary_search`（性能约束不变） |
| `src/order_gui.cpp` | 场下拉**按目的地站动态生成**（整站 + 该站当前所有场），标签 `STR_ORDER_R3R_YARD_NAMED(_SHARED)` |
| `src/station_gui.cpp` | 站场管理窗口改为 N 场：`WID_SY_YARD_SEL` 选场 + `WID_SY_NEW_YARD` 追加空场 + `WID_SY_TOGGLE_SHARED` 共享开关 + `WID_SY_ASSIGN` 把选中平台划入/移出该场 + `WID_SY_LOCATE`；平台列表显示每行所属场号 |
| `src/lang/*.txt` | `STR_R3R_YARD_NAME_N` `[Yard {NUM}]` / `_N_SHARED` / `STR_ORDER_R3R_YARD_NAMED` 等按编号的动态文案（中英同步） |
| `src/sl/station_yard_sl.cpp` | **新增文件**（已加入 `src/sl/CMakeLists.txt`），承载 v2 编解码 + v1 迁移 |

### 5.4 本轮编译收口与踩到的坑（详见 `R3R_KNOWN_ISSUES.md` 第 70 轮）

- 首次全量重编在 `station_yard_sl.cpp` 中断（`[427/707]`）：①`Station::R3RStationYard` 是嵌套类型，自由函数里必须全限定；②`TileIndex` 是强类型，**没有**到 `uint32_t` 的隐式转换，`in[p++] & 0xFFFF` / `uint32_t m = in[p++]` 均不合法，须 `.base()`。
- 续编又暴露 `order->GetDestination()` 是 `DestinationID`（不是 `StationID`），`order_cmd.cpp(2471)` / `order_gui.cpp(805)` 须写 `.ToStationID()`。
- 最终 `build\R3R_resume.done = EXIT_CODE=0`，`build\openttd.exe` @ 2026-09-19 22:51:53（50 779 136 B），620/620 obj 全晚于最新头文件（无 KI-15 混合布局）。

### 5.5 待复测（口径已从「三场」改为「N 场」）

1. 建场 → 订单下拉能列到场；选场后列车只停该场平台；未划入任何场的平台不被带场订单使用。
2. 场满 → 回落共享场；共享场也满时的排队行为（预期与原生一致：站外 PBS 等待）。
3. **老档迁移**：无 `SYRD` 的旧档 + v1 三场 `SYRD` 旧档，加载后场 1/2/3 与原 A/B/共用一一对应，且这些旧档里的订单场 ID 仍指向同一个场。
4. **存读往返**：游戏内建场/划平台 → 存档 → 读档，场数据与订单引用一致（**本轮完全未测**）。
5. 车站扩建/拆除后场 tile 修剪无悬垂；订单换类型（GOTO_STATION → 其他）时 `SetR3RYard` 位残留已确认被重置。
6. 共享排程（`IsOrderListShared`）下选场一致性。

