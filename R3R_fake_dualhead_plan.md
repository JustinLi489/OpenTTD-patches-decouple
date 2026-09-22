# R3R「假双头」方案（玩家 2026-09-21 拍板，待实现）

## 0. 玩家口径（原文）

> 双头机车的后端机车无法成为引擎，而段要求头尾均为引擎或假引擎，所以我的想法是把双头拆成单头，
> 再赋一个假双头的 flag 来绕开双头机车的后端机车无法成为引擎。

即：**段的两端必须是引擎（或假引擎）** 与 **双头机车后半节不能被打成引擎** 两条约束互斥。
玩家给的解法不是「放弃段尾假引擎」，而是**解耦这对约束**：把双头「拆成单头」（两半各自具备引擎身份），
再用一个**假双头旗标**把「这两半其实是一台双头机车」这一事实显式记下来，供所有真正需要它的判定使用。

## 1. 现状（本轮开工前的调查结论）

- 段尾假引擎机制＝`SetSegmentTailFakeEngine()`（`src/train_cmd.cpp`），其内部对 `IsRearDualheaded()`
  早退——这是第 78 轮 KI-143 特意加的，因为上游 `NormaliseDualHeads()` 依赖「后半节 `!IsEngine()`」
  才能把它搬回前半节身边。
- 双头的「两半」关系存在 `other_multiheaded_part`（双向指针），语义由 `IsMultiheaded()` +
  「谁有引擎位」派生：`IsRearDualheaded()` ＝ `IsMultiheaded() && !IsEngine()`。
- 因此「拆成单头 + 假双头旗标」的实质＝**让后半节也有引擎位**，同时**引入一个新判据取代
  `IsRearDualheaded()` 里那个「无引擎位」的推断**，并把所有依赖它的地方换成新判据。

## 2. 设计

### 2.1 新旗标

- 在 `src/train.h` 的 `VehicleRailFlag` 枚举里新增一位：`FakeDualHead`（当前用到 27 位之前的空位，
  实测该枚举是 `EnumBitSet<VehicleRailFlag, uint32_t>` ⇒ 字段宽度不变、**存档格式不变**，
  但改 `src/train.h` ⇒ 本仓规矩：**必须删全部 `*.obj` 全量重编**（KI-15 / 记忆 66636022）。
- 语义：`FakeDualHead` 打在**后半节**上，含义＝「我是双头机车的后半节，引擎位是人工授的」。

### 2.2 新判据（单一真源）

```
IsDualHeadRear()  ==  IsMultiheaded() && (!IsEngine() || HasBit(FakeDualHead))
```

- 所有 `IsRearDualheaded()` 的调用点改为该判据。**清点结论（见 3.1）显示 20 处语义完全一致，
  可整体替换为 `IsDualHeadRear()`**；只需保留 `ground_vehicle.hpp:445` 的原定义与
  `tbtr_template_vehicle.h:161` 的模板版不动。新增判据放在 `ground_vehicle.hpp:445` 紧邻处。
- `NormaliseDualHeads()` 的守卫（`train_cmd.cpp:2234`：伙伴带引擎位则 `continue`）**恰好就是新语义
  所需的行为**（跳过重排，两半留在原地），代码不变，只更新注释（原文称其为「consist 畸形」）。

### 2.3 与段机制的接线

- `SetSegmentTailFakeEngine()` 去掉 `IsRearDualheaded()` 早退；改为：若目标车是双头后半节，
  则**先打 `SetEngine()` + `FakeDualHead`**，再打段尾假引擎身份（等价于「就地拆成单头」）。
- 降段/解段时反向撤销：清假引擎身份，若该车带 `FakeDualHead` 则**清引擎位 + 清旗标**，
  恢复成上游认可的双头后半节。

## 3. 风险清单（必须在实现时逐条核对，并写入 KI）

1. 功率/速度：后半节 `IsEngine()` 变真后，`Train::Power()` / `MaxSpeed()` / 调车相关计算
   可能重复累计后半节功率，也可能因为 `IsMultiheaded()` 而被减半——需实测「段尾假双头」的
   加速度/最高速与拆分前完全一致。
2. 买卖/自动替换/克隆：所有「后半节不可单独卖/移动」的保护都依赖 `IsRearDualheaded()`；
   漏改一处 ⇒ 玩家能把双头拆一半卖出去。
3. `NormaliseDualHeads` / depot 拖动 / `CmdMoveRailVehicle` 的隐式断言（如 `assert(IsEngine())`）。
4. 存档：读档期的双头修复逻辑若按「无引擎位」判定，会把我们人工授的引擎位改回去；
   需确认它是否只对旧存档版本生效，否则要在读档收尾处按 `FakeDualHead` 复原。
5. NewGRF：`position in consist` / 双头相关变量（`u.rail.*`）读取 `other_multiheaded_part` 的地方。
6. 段的几何/折叠判据（FOLDCHK 系列）以 `IsEngine()` 为依据的点需复核。

### 3.0 调用点清点（2026-09-21 实测，`grep` 全 `src/`）

**关键发现：本方案不需要「逐点人工判断」——全部 20 个 `IsRearDualheaded()` 调用点语义完全一致，
都可整体替换为新判据 `IsDualHeadRear()`。** 唯一必须分别处理的是读档期
（`ConnectMultiheadedTrains`，见 3.2）与定义本身。

#### 3.1 `IsRearDualheaded()` 全部 20 处（按语义归组）

| # | 位置 | 语义 | 处理 |
|---|---|---|---|
| 1 | `vehicle_cmd.cpp:531` `SetSegmentTailFakeEngine` | KI-143 早退（本方案要拆的就是它） | **改**：去早退，授引擎位 + `FakeDualHead` |
| 2 | `vehicle_cmd.cpp:1962` `CmdCloneVehicle` | 克隆时跳过后半节（重造前半节会带出后半节） | 替换 |
| 3 | `vehicle_cmd.cpp:525` | 注释 | 改写 |
| 4 | `train_cmd.cpp:2664` `CmdMoveRailVehicle` | 后半节不可单独调度 | 替换 |
| 5 | `train_cmd.cpp:2670` 同命令 | 允许「整对」一起移动 | 替换 |
| 6 | `train_cmd.cpp:2880` `CmdSellRailVehicle` | 后半节不可单独卖 | 替换 |
| 7 | `train_cmd.cpp:2370` `AddWagonToConsist` | 「允许挂车」NewGRF 回调不查后半节 | 替换 |
| 8 | `train_cmd.cpp:4802` `GetDecoupleVehicleAuto` | 沿链**配对括号**（前半↔后半成对）求解挂边界 | 替换（漏改会让解挂点算错） |
| 9 | `train_cmd.cpp:12862` `Train::CanLeadTrain` | 后半节可领头（双头调向的基础） | 替换（对普通双头结果不变） |
| 10 | `train.h:354` `GetNextUnit` | 单元遍历跳过后半节 | 替换 |
| 11 | `train.h:366` `GetPrevUnit` | 同上（反向） | 替换 |
| 12 | `vehicle.cpp:213` `VehicleServiceInDepot` | 后半节也做维修/限速重置 | 替换（`IsEngine()` 已覆盖，等价） |
| 13 | `vehicle.cpp:1050` `Vehicle::IsEngineCountable` | 后半节不计入 `num_engines` | 替换（漏改 ⇒ 公司车辆数虚高） |
| 14 | `economy.cpp:2020` `ReserveCargo` | 后半节不当「等价铰接链头」重复预留货位 | 替换 |
| 15 | `economy.cpp:2024` 同上（`through_load` 分支） | 同上 | 替换 |
| 16 | `autoreplace_cmd.cpp:289` `GetNewEngineType` | 自动替换跳过后半节 | 替换 |
| 17 | `autoreplace_cmd.cpp:641` `assert(!...IsRearDualheaded())` | 自动替换拆分前提 | 替换 |
| 18 | `autoreplace_cmd.cpp:946` `CmdAutoreplaceVehicle` | 后半节不可单独替换 | 替换 |
| 19 | `ground_vehicle.hpp:445` | **定义**（`Train` 经 `GroundVehicle<Train,VehicleType::Train>` 继承） | 保留原样（上游语义） |
| 20 | `tbtr_template_vehicle.h:161` | 模板车辆的**独立**同名方法（另一套类型） | **不动**（模板无段尾假引擎概念） |

相关但**不**经由 `IsRearDualheaded()` 的点（`other_multiheaded_part` 共 21 处，多为指针维护，
仅以下两处需在新语义下复核）：
- `train_cmd.cpp:2234` `NormaliseDualHeads` —— `if (t->other_multiheaded_part == nullptr ||
  t->other_multiheaded_part->IsEngine()) continue;`：**这条「伙伴带引擎位就跳过」的守卫正好是
  新版想让后半节带引擎位时的所需行为**（跳过重排、两半留在原地），故**不需要改**，只更新注释
  （原文把它称作「consist 畸形」）。
- `sl/vehicle_sl.cpp:78-121` `ConnectMultiheadedTrains` —— 见 3.2。

#### 3.2 读档期一定会破坏 `FakeDualHead`（风险 #4 已证实）

`sl/vehicle_sl.cpp:54 ConnectMultiheadedTrains()` 在**每次读档**都会跑（不受存档版本门控），
且两处会推翻人工授位：
- `:81-85` `if (!u->IsEngine()) { u->SetEngine(); u->spritenum--; }` —— 若后半节没引擎位就**补授**并改 `spritenum`；
- `:95-98` `if (w->IsEngine()) { w->ClearEngine(); w->spritenum++; }` —— 找到配对伙伴后**强制清掉**它的引擎位。

⇒ 必须改：配对写法改为「`w` 带 `FakeDualHead` 时**保留**引擎位、**不动** `spritenum`」，
即 `if (w->IsEngine() && !w->flags.Test(VehicleRailFlag::FakeDualHead)) { w->ClearEngine(); w->spritenum++; }`；
`:81-85` 的补授分支不必改（带旗标者已是引擎）。**同时**这是本方案唯一需要触碰的
`saveload` 侧代码，且是 `.cpp` ⇒ 改完可增量。

**存档格式无风险**：`FakeDualHead` 落在 `Train::flags`（`VehicleRailFlag` bitset / `uint32`），
该字段由 `src/sl/vehicle_sl.cpp:1349` `SLE_CONDVAR_X(Train, flags, SLE_UINT32, ...)` 原样保存，
现有 `SegmentFront`（★）/ `SegmentBack` 就靠它跨存档往返 ⇒ 新位自动持久化，**无版本号变更**。

#### 3.3 功率已核实无变化（风险 #1 基本排除）

`ground_vehicle.cpp:74 CalculatePower()` 遍历 `u->Next()`（**全部车辆**，非 `GetNextUnit`），
逐车累加 `u->GetPower() + u->GetPoweredPartPower()`。`GetPower()`（`train.h:514`）对
`IsMultiheaded()` 的车**一律减半**。双头两半都带 `GVSF_MULTIHEADED` ⇒ 原本就是「前半/2 + 后半/2 = 全额」，
给后半节补上引擎位不改变任何一项；`max_te` 也按有功率的车累加，同样不变。
⇒ 功率/牵引力无需改代码，实测只作回归确认。

## 4. 实施顺序（每步都必须过编译）

1. `src/train.h`：加 `VehicleRailFlag::FakeDualHead`；`src/ground_vehicle.hpp`：在
   `IsRearDualheaded()` 旁加 `IsDualHeadRear()`。
2. 全量替换 `IsRearDualheaded()` 的 18 个**调用点**（3.1 表，保留定义与模板版），其中
   `train.h:354/366`、`vehicle.cpp:1050`、`train_cmd.cpp:4802` 四处最关键（单元分组 /
   车辆计数 / 解挂边界配对），必须逐点复核。
3. 更新 `NormaliseDualHeads()` 注释（代码不变，见 2.2）。
4. 改 `SetSegmentTailFakeEngine()`（去早退 → 授引擎位 + `FakeDualHead`）与降段撤销
   `ClearSegmentTailFakeEngine()`（清引擎位 + 清旗标）。
5. 改 `sl/vehicle_sl.cpp:95-98 ConnectMultiheadedTrains()`（见 3.2，`.cpp` 可增量）。
6. 删全部 `*.obj` → 全量重编（约 25~40 分钟，`build\R3R_fullbuild.done` 应 `EXIT_CODE=0`）。
7. 实测：双头机车升段 → 段尾具备假引擎身份 → 逻辑翻转/身份迁移正常 → 拆一半卖不出去 →
   功率与最高速与拆分前一致 → 存档往返不丢 `FakeDualHead` → 读档后段尾引擎位仍在（3.2 回归）。

## 5. 状态

- 未实现（本轮先把 KI-149 rev.2 收尾；本方案待开工）。
- **开工前清点已完成**（2026-09-21），结论见第 3.0~3.3 节：20 个 `IsRearDualheaded()` 调用点
  语义一致可整体替换；`ConnectMultiheadedTrains()` 是唯一必需改的读档逻辑；`Train::flags`
  已存档故格式无变更；功率计算已核实不受影响。
- 剩余未知：双头后半节被授引擎位后，NewGRF 双头变量（`u.rail.*` / `position in consist`）与
  depot GUI（`depot_gui.cpp:169` 的 `other_multiheaded_part == v->Next()` 视觉配对）的行为，
  需游戏内实看。
