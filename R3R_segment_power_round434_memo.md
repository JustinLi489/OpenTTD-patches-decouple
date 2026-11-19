# 第 434 轮：散链内铰接式 → Ctrl+左键 → 设为段，得到的段功率为 0（应为 1）

## 一、玩家报告（原话）

1. 「对一个散的纯车厢链内部的一个铰接式执行 ctrl+左键，随后将其设为段，那么制造出来的段就功率为 0 而非 1。我希望的上策是让这样子制造出来的段功率为 1，下策是让散链禁止 ctrl+左键的铰接式行为。优先选上策」
2. 「为什么我需要段功率至少为1呢，因为功率为0，列车就无法启动，显示"没有电力"。而正常的R3R设为段，纯车底段也是功率为1的」

⇒ **关键基准**：正常"设为段"路径产出的纯车底段，功率显示 1 且能启动；本路径产出 0，启动即报 `STR_ERROR_TRAIN_START_NO_POWER`（"没有电力"）。

## 二、已取证事实（行号锚点 + 读数）

### 2.1 功率汇总链只有一处写点，且无下限

- `src/ground_vehicle.cpp:27-73` `GroundVehicle::PowerChanged()`：
  - `:62` `if (this->gcache.cached_power != total_power || ...)`
  - `:64` `if (total_power == 0) this->vehstatus.Set(VehState::Stopped);`
  - `:66` `this->gcache.cached_power = total_power;` ← **唯一赋值点（除读档/清零外）**
- `src/ground_vehicle.cpp:76-98` `CalculatePower()`：
  - `:84` `uint32_t current_power = u->GetPower() + u->GetPoweredPartPower();`
  - `:90` `total_power += current_power;`
- 结论句：**`cached_power` 是整链逐车功率的纯加和，代码里没有任何 `max(1, …)` 下限**。所以"正常段功率为 1"只能来自某一节的 `GetPower()`/`GetPoweredPartPower()` 本身返回 1，而不是汇总层兜底。

### 2.2 全部 `cached_power` 出现点已枚举

- `src/train_cmd.cpp`：`:607`（`ConsistChanged` 末尾把链头之后的车的缓存清 0）、`:1312`/`:1822`/`:25312`/`:25321`（只读统计）、`:15845`（`CheckTrainStayInDepot` 自动停车豁免）、`:23733`（car-only 自走防护）、其余为探针打印。
- `src/vehicle_cmd.cpp`：仅 `:1473`（`CmdStartStopVehicle` 的 `cached_power == 0 → STR_ERROR_TRAIN_START_NO_POWER`，**没有 car-only 豁免**）。
- ⇒ 结论句：`CmdMakeSegment` / `R3RPromoteFreeWagonChainToFront` / `R3RCreateCarOnlyFormation` 里**都不写 `cached_power`**（不一致来源不在这里）。

### 2.3 纯车底判定不依赖功率

- `src/train_cmd.cpp:2423-2427` `R3RIsCarOnlyFormation()` = `IsEngine() && RailVehInfo(engine_type)->railveh_type == Wagon`。
- ⇒ 结论句：即使把功率抬到 1，`R3RIsCarOnlyFormation()` 的语义不受影响，`CheckTrainStayInDepot` 豁免、解挂/耦合的 car-only 分支都照旧成立。

### 2.4 `GetPoweredPartPower()` 只对"动力车厢"非零

- `src/train.h:597-605`：`PoweredWagon` 位为真才返回 `RailVehInfo(gcache.first_engine)->pow_wag_power`，否则 0。

## 三、猜测根因（待验证）

**猜测 R434-A（主）**：两条路径产出的**链头车身份不同**，导致 `GetPower()` 的取值不同。
- 正常路径：链头是普通车厢，`SetEngine()` 后 `GetPower()` 取该车 `engine_type` 记录的 `power`（部分 GRF 车厢记录 power 记 1，用于"能显示但不驱动"），故整段 `cached_power == 1`。
- Ctrl+左键铰接式路径：链头/参与 `CalculatePower` 的车里含 **真铰接部件（`IsArticulatedPart()`）**，部件的 engine 记录 power=0 且未置 `PoweredWagon`，`GetPoweredPartPower()` 也为 0 ⇒ 加和为 0。
- 待验证判据：读 `Train::GetPower()` / `Engine::GetPower()` 实现；确认 `IsArticulatedPart()` 的车 `GetPower()` 是否被强制归 0；确认设段后链头究竟是普通车厢还是 artic part。

**猜测 R434-B**：Ctrl+左键选中 artic part 后，`CmdMakeSegment` 的链头解析（`while (t->Previous())`）落在 artic part 上，`SetEngine()` 打到 part 而不是"父车"，`IsEngine()` 与 `IsArticulatedPart()` 同时为真，功率汇总走 part 分支得 0。

**猜测 R434-C（兜底）**：与 GRF 无关的通用解释——本修改树里曾有"car-only 段功率 1"的实现，但在某轮被删/被绕过；需 git 取证。若成立则修法为恢复下限。

## 四、疑点（REG-434-A）

- 疑点：用户观察"正常设为段的纯车底段功率为 1"与静态代码"纯加和、无下限"不自洽。
  - 期望值：正常路径也应得 0（若车厢记录 power=0）。
  - 实测值：玩家界面读到 1（并能启动）。
  - 为什么存疑：缺少两条路径的 `CHAIN-ATTRS pow=` 实测对照。
  - 还需数据：同一存档内 `CHAIN-ATTRS` 的 `pow=` 两例（正常段 / Ctrl+左键铰接式段），以及设段后的链头车号与 `dump_vehicle` 的 `tf:`/`st:`（是否含 A/H/M）。

## 五、拟定修法（上策，待拍板后落地）

统一在"设为段"的提交点保证下限，而不是只修一条路径：
- 位置：`CmdMakeSegment` 成功分支（`R3RCreateCarOnlyFormation` 之后、`ConsistChanged` 之后），以及 `R3RCreateCarOnlyFormation()` 内部（覆盖所有提升入口）。
- 写法意向：`ConsistChanged` 之后若 `gcache.cached_power == 0` 则置 1（并确保后续不再被 `PowerChanged` 重置回 0 —— 注意 `PowerChanged` 每次 `CargoChanged` 都会重算，只在外面点一下会被下一次重算冲掉）。
- **注意点**：若只在提交点写 1，任何 `ConsistChanged`（装卸货、耦合、读档）都会把它冲回 0 ⇒ 下限必须落在 `CalculatePower`/`PowerChanged` 的汇总层，或对 car-only formation 在 `PowerChanged` 里夹 `max(1, total_power)`。这是本轮最关键的实现约束。

## 六、复测判据（草案）

1. Ctrl+左键铰接式 → 设为段，车辆详情窗口功率显示 1 hp，点启动不再报"没有电力"。
2. 正常设为段的纯车底段仍显示 1（不回退）。
3. 含真机车/动车组的列车功率数值逐字不变（负判据）。
4. 装卸货、耦合/解挂、存档往返后，纯车底段功率仍为 1（验证下限没被 `ConsistChanged` 冲掉）。
5. `R3RIsCarOnlyFormation` 相关行为（等待者不自动开车、解挂 car-only 分支）无回归。

## 七、根因定稿（2026-11-13，已实证）

**结论句：1 hp 下限本来就有，位置在 `Train::GetPower()` 内，但它落在 `power_override` 的 early return 之后，所以只被人造 override 绕过。**

### 7.1 既有下限（此前被忽略的关键代码）

`src\train.h:596-602`（`GetPower()` 的正常分支）：

```cpp
/* R3R: a consist front (wagon-chain promoted to engine) reports at
 * least 1 hp. Its real wagon data is untouched, but a 0-power engine
 * triggers the "no power" auto-stop and shows 0 hp in the window;
 * 1 hp keeps it couplable and shows the intended "1" instead. */
if (this->IsEngine() && RailVehInfo(this->engine_type)->railveh_type == RailVehicleType::Wagon) {
    power = std::max<uint16_t>(power, 1);
}
```

⇒ 这就是"正常设为段的纯车底段功率 = 1"的来源：普通车厢链没被打散，`power_override` 仍是 `UINT16_MAX`，走这条分支被夹到 1。**汇总层（`CalculatePower`/`PowerChanged`）本来就没有下限，也不需要**——下限在逐车取值处。

### 7.2 唯一绕过点

`src\train.h:571-573`（同函数最前面）：

```cpp
if (this->power_override != UINT16_MAX) return this->power_override;
```

`DearticulateChainWithSnapshot()`（`src\vehicle_cmd.cpp:530-566`）对每个打散组按 `total_power / n_total` 烘焙 override；**纯车厢组 `total_power == 0` ⇒ 全体烘焙成 `power_override = 0`**（组头 `p_q + p_r` 也是 0）。

`CmdMakeSegment()` 的执行顺序（`src\vehicle_cmd.cpp:808-867`）是：

1. `R3RStopChainInDepot()` / `R3RPromoteFreeWagonChainToFront()`（把链头提升成假引擎车底）
2. `R3RDumpUpgradeDbg(t, "UPGRADE-BEFORE-SPLIT")`
3. **`DearticulateChainWithSnapshot(t)`** ← 这一步把 override 烘焙成 0
4. `SetSegmentFront()` → `SetSegmentTailFakeEngine()`
5. `t->ConsistChanged(CCF_ARRANGE)`

⇒ 第 3 步之后链头的 `GetPower()` 直接 `return 0`，第 5 步汇总得 `cached_power = 0`；而**同一条链若不执行第 3 步（正常设为段）**，同一辆假引擎车会命中 7.1 的 floor 返回 1。

### 7.3 与猜测的对照

- R434-A：**部分成立**，但机理不同——不是"artic part 的 GetPower 被归 0"（`train.h:592` 的 `!IsArticulatedPart()` 确实会归 0，但打散后已无 artic part），而是"烘焙 override 绕过 floor"。
- R434-B：**不成立**（`CmdMakeSegment` 先 promote 再打散，链头不会是 artic part）。
- R434-C：**不成立**（下限从未被删，只是被 early return 跳过）。
- REG-434-A 疑点**消除**：`cached_power` 无下限 ≠ 无下限，下限在 `GetPower()` 内。

### 7.4 修法选择：为什么改 `GetPower()` 而不是在 `CmdMakeSegment` 里点一下

在 `CmdMakeSegment` 里 `cached_power = 1` 会被下一次 `PowerChanged()`（装卸货/耦合/读档都会触发）冲回 0，必须在取值层修；而 `GetPower()` 是 `CalculatePower()` 的唯一逐车来源，且 override 的持久性由 `SLV_R3R_ARTIC_OVERRIDE`（`src\saveload\vehicle_sl.cpp:209-211`）保证 ⇒ 改这里**同时自愈存量存档**里已经烘焙成 0 的段（不必"降级再升级"）。

## 八、落地实现（2026-11-13）

### 8.1 `src\train.h`（`Train::GetPower()`）

override 分支内补同一把尺子（只夹 0→1，真实降额值不动）：

```cpp
if (this->power_override != UINT16_MAX) {
    uint16_t power = this->power_override;
    if (power == 0 && this->IsEngine() && RailVehInfo(this->engine_type)->railveh_type == RailVehicleType::Wagon) {
        power = 1;
    }
    return power;
}
```

### 8.2 `src\train_cmd.cpp`（car-only 自走防护的判据替换）

`cached_power == 0` 曾是"链内无真机车"的代理判据（`TrainLocoHandler` 内 car-only 停车闸门）。功率被夹到 1 后该代理失效，改为显式扫描：

- 新增 `static bool R3RChainHasRealTraction(const Train *consist)`（定义紧接 `R3RIsCarOnlyFormation()` 之后）：链上出现「`IsEngine()` 且 `railveh_type != Wagon`」或「`VehicleRailFlag::PoweredWagon`」⇒ 有真牵引力。
- 闸门改为 `if (R3RIsCarOnlyFormation(consist) && !R3RChainHasRealTraction(consist))`，停车语义（清 cur_speed/subspeed 并 return true）逐字保留。
- 另一处 `CheckTrainStayInDepot()`（`src\train_cmd.cpp:15845`）的条件是 `cached_power == 0 && !R3RIsCarOnlyFormation(v)`，车底段本来就被豁免 ⇒ **不受影响，未改**。

### 8.3 探针

`CmdMakeSegment()` 的 `ConsistChanged(CCF_ARRANGE)` 之后新增：

```
SEG-POWER head=%d caronly=%d ovr=%u power=%u
```

`ovr` 是链头的 `power_override`（打散路径此处应为 0），`power` 是 `gcache.cached_power`（修复后应为 1）。

### 8.4 负判据（刻意不做）

- 不改 `CmdStartStopVehicle`（`cached_power == 0 → STR_ERROR_TRAIN_START_NO_POWER`）——修复后自然不再命中，无需豁免。
- 不给 `CalculatePower()` / `PowerChanged()` 加统一下限——那会把"真 0 功率车"也抬成 1，改变 UI 与加速度语义。
- 不在 `CmdMakeSegment` 里写 `cached_power`（会被重算冲掉，见 7.4）。
- 车底段"能点启动"≠"能自己开走"：`R3RChainHasRealTraction` 闸门仍拦住自走（KI-136/P7 语义不回退）。

## 九、构建自证（2026-11-13）

- 入口：复用既有 `R3R_fullbuild.cmd inc detach`（未新建任何 .cmd）。
- 护栏 `build\R3R_incbuild.guard.log`：`GUARD: newest object = 2026-11-13 22:12:59` / `newer: src\train.h (2026-11-13 22:35:18)` / `REMOVED 621 object file(s) - upgrading this build to a FULL rebuild` ⇒ 改头文件按规矩走全量。
- 日志 `build\R3R_incbuild.log`：`[693/693] Linking CXX executable openttd.exe`；`build\R3R_incbuild.done` = `EXIT_CODE=0`；`error C|fatal error|FAILED:|build stopped` 计数 = **0**。
- 时间戳：`src\train.h` 22:35:18、`src\train_cmd.cpp` 22:35:27、`src\vehicle_cmd.cpp` 22:35:44 → `train_cmd.cpp.obj` 23:01:10、`vehicle_cmd.cpp.obj` 23:01:34 → `build\openttd.exe` **2026-11-13 23:04:46（52 886 016 B）**，全链单调。
- `read_lints` 三个文件 0 条。
- 产物字面量：`findstr /C:"SEG-POWER head=" build\openttd.exe` 命中（`SEG-POWER head=%d caronly=%d ovr=%u power=%u`）。
- 状态：**已实现 + 已编译**，游戏内复测待做（判据见 §六与 KI-630）。发布树 `build-release` 停在第 429 轮之前那一版，**不含本轮修复**（按口径只在玩家明确要求时编发布版）。
