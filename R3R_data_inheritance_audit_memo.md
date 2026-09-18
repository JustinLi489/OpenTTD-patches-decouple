# R3R 数据继承审计备忘（任务 1）

> 2026-09-15 第 16 轮。范围：**设为段 / 降级段 / 耦合 / 解挂 / 换端重排** 前后，
> 列车各项数据的继承与守恒。审计方式：逐函数精读（无子代理），未跑运行时取数。

---

## 一、结论速览

| 数据 | 设段前 → 设段后 | 降级 | 耦合 | 解挂 | 判定 |
|---|---|---|---|---|---|
| 重量 | 逐车烘焙份额，总和不变 | 清 override 复原 | 两链相加 | 各归其主 | **守恒** |
| 功率 | 同上（含 multihead 折半） | 同上 | 同上 | 同上 | **守恒**（+段的尾部假引擎 +1 hp，见 2.1） |
| 最高速度 | 全组同一个 `max_speed_override`（取 min 语义） | 清 override | 取合并链 min | 各归其主 | **守恒** |
| 运行费用 | `GetNextVehicle()` 走语义层**仍跳过组成员** | 同上 | 合并链一次计费，总额不变 | 解出部分自立门户，总额不变 | **守恒** |
| 列车价值 | artic 件 `value == 0`，组成员保持 0 | 同上 | 各车 `value` 原样 | 原样 | **守恒** |
| 可靠性 / breakdown | 属于「链头对象」；本组操作不移交链头 | — | 换端重排时会换到另一台引擎（见 2.2） | — | **守恒（有注解）** |
| 载重容量 / 货物 | 只改 subtype，`cargo`/`cargo_cap` 未动 | 只改 subtype | 各自保留 | 各自保留 | **守恒** |
| 服役相关 | `service_interval` 随身份迁移，`date_of_last_service` 不迁（见 2.2） | — | — | — | **有缺口** |
| 当年利润 | — | — | `v.profit_this_year += u.profit_this_year`，u 清零 | 各留各的 | **守恒**（见 2.3） |

---

## 二、关键机制（为什么守恒）

### 2.0 语义角色层是守恒的地基

`DearticulateChainWithSnapshot`（`vehicle_cmd.cpp:389-442`）把一个真 artic 组炸成
多台真实车辆，同时：

- 父车 `SetArticGroupHead()` + `weight_override = w_q+w_r` / `power_override = p_q+p_r` /
  `max_speed_override = 组速度`；
- 每个部件 `ClearArticulatedPart()`（**只清 subtype 位，不置 `ArticGroupMember` 的替代逻辑**）
  + `SetArticGroupMember()` + `weight_override = w_q` / `power_override = p_q` /
  `max_speed_override = 组速度`。

关键点在于**哪些谓词走 subtype 位、哪些走语义层**，二者分工明确：

| 判定点 | 依据 | 后果 |
|---|---|---|
| `Vehicle::GetNextVehicle()`（→ 运行费用、列车价值、容量聚合的口径） | `HasArticulatedPart()` = **语义层**（`Next()->IsArticGroupMember()`，`vehicle_base.h:1184`，注释明确写了「de-articulated groups are traversed as one unit」） | 拆开后**仍被当作一个整体跳过** → 运行费用/价值/容量**不会重复计 N 次** |
| `GroundVehicle::CargoChanged()` 的 artic 分支（`ground_vehicle.cpp:114`） | `u->IsArticulatedPart()` = **subtype 位** | 拆开后走「普通车」分支 → 吃自己的 `weight_override` |
| `Vehicle::GetEnginePartsCount()`（`vehicle_base.h:1200`，注释明确） | `v->IsArticulatedPart()` = **subtype 位** | 拆开后恒为 1 → 不再把父车重量再摊 N 份 |
| `Train::GetPower()` / `GetWeightWithoutCargo()` | override 优先，其次才看 subtype | 拆开后读烘焙值 |
| `GetTrainVehicleMaxSpeed()`（`train_cmd.cpp:259`） | override 优先 | 拆开后全组读同一个组速度（max speed 是**取 min 的上限**，不是可加量，故全给同一个值正确） |

**重量逐车对账**（父车原重 `W`、组内车辆数 `n`）：设段前 `CargoChanged` 给父车
`W/n + W%n`、每个部件 `W/n`，合计 `W`；设段后父车 `weight_override = W/n + W%n`
且 `GetEnginePartsCount()==1` → 父车仍是 `W/n + W%n`，部件各 `W/n`，合计 `W`。**逐车
完全一致**（这也解释了为什么 `weight_override` 要把余数放在父车上）。

### 2.1 唯一的刻意偏差：段的尾部假引擎 +1 hp

`SetSegmentTailFakeEngine`（`vehicle_cmd.cpp:491-505`）把段尾车厢 `SetEngine()`。
`GetPower()`（`train.h:497-499`）对「subtype=engine 但引擎记录是车厢」的车强制
`power = max(power, 1)`。于是：

- 段由**普通车厢链**升级而来：设段后比设段前多 1 hp（段尾贡献）。
- 段由**真 artic 组**拆解而来：段尾带 `power_override`（烘焙份额），**不**走这条 clamp，
  不产生增量。

属**已文档化**的刻意偏差（为了让 0 马力引擎不触发「无动力自动停车」并把窗口显示成
1 而不是 0），量级 1 hp，不影响平衡，不修。

### 2.2 换端重排：可靠性 / 年龄 / 服役的真实缺口

`R3RRelocateFrontIdentity(from, to)`（`train_cmd.cpp:5122-5165`）只在
「多节真引擎链逻辑翻 v，链头离开原机车对象」时触发，它调用官方
`CopyVehicleConfigAndStatistics`（`vehicle_base.h:891-914`）迁移：

`orders` / `orders_backup*` / `cur_*_order_index` / `unitnumber` /
`current_order` / `dest_tile` / `profit_this_year` / `profit_last_year` /
`profit_lifetime` / `current_loading_time` / 三个 timetable 标志 / **`service_interval`**。

**没有**迁移：

- `reliability` / `reliability_spd_dec`
- `breakdown_ctr` / `breakdown_chance` / `breakdowns_since_last_service`
- `age` / `max_age` / `build_year` / `date_of_last_service`
- `value`（不需要迁：按对象守恒，链头换人后总和不变）

后果：

1. **可靠性**：breakdown 判据读 `v->First()->breakdown_chance_factor`
   （`vehicle.cpp:2350`），而 `breakdown_chance_factor` 由 `ConsistChanged`
   在链头上重算（`train_cmd.cpp:1388-1392`，`cached_num_engines` 也只数
   「非组员 && (引擎||multihead)」，设段前后**完全一致**）。链头换了物理引擎
   后，列车读的是**新链头自己那台引擎**的 `reliability` —— 从「这台引擎的属性」
   角度是对的，但不是「原链头那台的」，属语义选择而非丢失。
2. **服役间隔不匹配（真缺口）**：`service_interval` 被复制过去，而
   `date_of_last_service` 没复制 → 新链头拿**自己的上次服役日期**配**原链头的间隔**。
   `NeedsServicing()` 于是可能立刻成立或长期不成立，服役周期出现一次跳变。
   严重度低（只影响一次进场时机），但确实是「继承不完整」。

### 2.3 换端重排残留 `profit_this_year`（低）

`CopyVehicleConfigAndStatistics` 只把 `profit_this_year/last_year` **复制**到新链头，
不把 `from` 清零，也没有 `profit_this_year = 0`。因此旧链头对象（现在是链内普通引擎）
仍留着一份当年利润。

- **当前无实际危害**：年度结算 `VehiclesYearlyLoop`（`vehicle.cpp:4783-4803`）
  只遍历 `Vehicle::IterateFrontOnly()` + `IsPrimaryVehicle()`，`from` 已不是链头
  → 既不显示也不结转到 `profit_lifetime`。
- **隐患**：若该对象日后因另一次换端重排**重新成为链头**，会把这份陈旧利润又复制
  一次 → 该车的 `profit_this_year` 虚高（金额上不影响公司账，只影响车辆窗口统计）。
  修法（待拍板）：迁移后 `from->profit_this_year = 0; from->profit_last_year = 0;`。

### 2.4 耦合 / 解挂的利润与身份

- `Couple`（`train_cmd.cpp:5760-5763`）：`v->profit_this_year += u->profit_this_year;`
  `v->profit_last_year += u->profit_last_year;` 然后 u 两项清零 → **守恒**。
- `Couple` 同时 `SetTrainGroupID(u, DEFAULT_GROUP)` + `GroupStatistics::CountVehicle(u, -1)`，
  链尾 `NormaliseTrainHead(v)` → `v->ConsistChanged(CCF_ARRANGE)` + `UpdateTrainGroupID(v)`
  → 合并链的重量/功率/速度/费用缓存**重算**，group 归链头。
- 解挂：解出部分（真引擎段）**自立门户**成为独立 primary，其运行费用本来就是它自己的
  那部分；留下的部分由 `ConsistChanged` 重算。总额不变。

### 2.5 容量与货物：不动 subtype 之外的东西

`DearticulateChainWithSnapshot` / `RearticulateChain` 只动 subtype 位、
`ArticGroupHead/Member` 标志与三个 override，**不碰** `cargo`、`cargo_cap`、
`cargo_type`、refit 状态。`CargoChanged` 对每辆车（含 artic 件）都加
`GetCargoWeight()`，故总载重与总容量在设段/降级前后一致。

---

## 三、结论

用户点名的五项（重量、功率、运行费用、可靠性、列车价值）在
**设段 / 降级 / 耦合 / 解挂**四种操作下**全部守恒**：

- 重量 / 功率 / 最高速度：靠 `DearticulateChainWithSnapshot` 的逐车烘焙 +
  `GetEnginePartsCount()` 保持走 subtype 位实现；
- 运行费用 / 列车价值：靠 `GetNextVehicle()` 走**语义角色层**、把拆开的组仍当一个整体
  跳过实现（这是 R3R 最关键的守恒设计）；
- 可靠性：不进聚合，属于链头；本组四种操作都不换链头物理对象。

两个遗留缺口（均登记 KI）：

- **KI-40**：换端重排复制 `service_interval` 但不复制 `date_of_last_service`
  （以及 reliability/max_age/build_year 一律不迁）→ 服役时机跳变一次。低。
- **KI-41**：换端重排后旧链头对象残留 `profit_this_year/last_year`，未清零。低。

另有一个**刻意偏差**（不修，已文档化）：普通车厢链升级为段时，段尾假引擎贡献 +1 hp。
