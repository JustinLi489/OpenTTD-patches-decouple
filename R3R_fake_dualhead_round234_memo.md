# R3R 第 234 轮：假双头（`FakeDualHead`）方案落地 —— 双头机车适配逻辑反转

> 上游 = 第 233 轮 KI-342（「双头机车"逻辑翻后链头落到后半"从未适配 —— `v_head_not_engine` 必然命中」），
> 决策文档 = `R3R_fake_dualhead_plan.md`（玩家 2026-09-21 拍板，2026-10-10 第 234 轮开工）。
> 本轮 = **需求已明确 + 修法已拍板 + 落地实现**，按长期规则先写本备忘与 KI 条目，再动代码。

---

## 一、玩家口径与拍板（原文）

> 双头机车的后端机车无法成为引擎，而段要求头尾均为引擎或假引擎，所以我的想法是把双头拆成单头，
> 再赋一个假双头的 flag 来绕开双头机车的后端机车无法成为引擎。

2026-10-10 第 234 轮追加拍板：**按原案开工**（不走第 233 轮 §修法清单里的 C1/C2 降级版）。

两条互斥约束是本方案的立足点：

1. **段的两端必须是引擎（或假引擎）** —— `SetSegmentTailFakeEngine()` 要求段尾拿到引擎位；
2. **双头机车后半节不能被打成引擎** —— KI-143 特意加的早退，因为上游 `NormaliseDualHeads()`
   依赖「后半节 `!IsEngine()`」把它搬回前半节身边。

玩家给的解法不是「放弃段尾假引擎」，而是**解耦这对约束**：把双头「拆成单头」（两半各自具备引擎身份），
再用一个**假双头旗标**把「这两半其实是一台双头机车」这一事实显式记下来，供所有真正需要它的判定使用。

---

## 二、开工前核查（2026-10-10 实测，`grep` 全 `src/`）

### 2.1 方案此前**一行都没落地**

| 检索式 | 路径 | 命中 |
|---|---|---|
| `FakeDualHead\|IsDualHeadRear` | `src/` | **0** |

⇒ 旗标与判据都不存在；KI-143 的早退仍在 `src\vehicle_cmd.cpp:698`。

### 2.2 `IsRearDualheaded()` 全量清点（22 命中 = 18 调用点 + 2 注释 + 2 定义）

| # | 位置 | 语义 | 处理 |
|---|---|---|---|
| 1 | `vehicle_cmd.cpp:698` `SetSegmentTailFakeEngine` | KI-143 早退（本方案要拆的就是它） | **改**：去早退，授引擎位 + `FakeDualHead` |
| 2 | `vehicle_cmd.cpp:692` | 同上函数的注释（自陈"绝不能授引擎位"） | **改写** |
| 3 | `vehicle_cmd.cpp:2269` `CloneVehicle` | 克隆时跳过后半节（重造前半节会带出后半节） | 替换 |
| 4 | `train_cmd.cpp:2816` `AddWagonToConsist` | 「允许挂车」NewGRF 回调不查后半节 | 替换 |
| 5 | `train_cmd.cpp:3286` `CmdMoveRailVehicle` | 后半节不可单独调度 | 替换 |
| 6 | `train_cmd.cpp:3292` 同命令 | 允许「整对」一起移动 | 替换 |
| 7 | `train_cmd.cpp:3643` `CmdSellRailVehicle` | 后半节不可单独卖 | 替换 |
| 8 | `train_cmd.cpp:7119` 解挂边界「配对括号」游走（`GetDecoupleVehicleAuto()` 之后） | 沿链配对括号（前半↔后半成对）求解挂边界 | 替换（漏改会让解挂点算错） |
| 9 | `train_cmd.cpp:9145` `IDENT` 逐节 dump（KI-334 探针 `rdh` 列） | 只读探针 | 替换（日志从此报"新语义下的后半"） |
| 10 | `train_cmd.cpp:9160` | 探针注释 | **改写** |
| 11 | `train_cmd.cpp:19352` `Train::CanLeadTrain` | 后半节可领头（双头调向的基础） | 替换（对普通双头结果不变） |
| 12 | `train.h:365` `GetNextUnit` | 单元遍历跳过后半节 | 替换 |
| 13 | `train.h:377` `GetPrevUnit` | 同上（反向） | 替换 |
| 14 | `vehicle.cpp:213` `VehicleServiceInDepot` | 后半节也做维修/限速重置 | 替换（`IsEngine()` 已覆盖，等价） |
| 15 | `vehicle.cpp:1050` `Vehicle::IsEngineCountable` | 后半节不计入 `num_engines` | 替换（漏改 ⇒ 公司车辆数虚高） |
| 16 | `economy.cpp:2020` `ReserveCargo` | 后半节不当「等价铰接链头」重复预留货位 | 替换 |
| 17 | `economy.cpp:2024` 同上（`through_load` 分支） | 同上 | 替换 |
| 18 | `autoreplace_cmd.cpp:289` `GetNewEngineType` | 自动替换跳过后半节 | 替换 |
| 19 | `autoreplace_cmd.cpp:641` `assert(!...IsRearDualheaded())` | 自动替换拆分前提 | 替换 |
| 20 | `autoreplace_cmd.cpp:946` `CmdAutoreplaceVehicle` | 后半节不可单独替换 | 替换 |
| 21 | `ground_vehicle.hpp:445` | **定义**（`Train` 经 `GroundVehicle<Train,VehicleType::Train>` 继承） | 保留原样（上游语义） |
| 22 | `tbtr_template_vehicle.h:161` | 模板车辆的**独立**同名方法（另一套类型） | **不动**（模板无段尾假引擎概念） |

与 2026-09-21 方案清点（20 处）的差异：新增了 `train_cmd.cpp:2816`（`AddWagonToConsist`）与
`train_cmd.cpp:7119`（解挂边界配对游走）两处；行号全部漂移，已按本节表为准。

### 2.3 与 KI-342 爆雷的因果闭环

`NormaliseSubtypes()`（`train_cmd.cpp:2696-2717`）对链头只有两支：

```2704:2710:src/train_cmd.cpp
	if (chain->IsWagon()) {
		chain->SetFreeWagon();
	} else {
		assert(chain->IsEngine());
		chain->SetFrontEngine();
	}
```

双头后半 `IsRearDualheaded() == IsMultiheaded() && !IsEngine()` 且非 wagon ⇒ 落进 else ⇒ 断言失败。
于是两处翻转守卫只能按「引擎位」判合法链头：

```10123:src/train_cmd.cpp
			if (v_flip_head != v && !v_flip_head->IsEngine()) {
```
```10179:src/train_cmd.cpp
				if (head != v && !head->IsEngine()) {
```

整链倒置后新链头**必然是双头后半**（无引擎位）⇒ 候选2 `fall through`、候选3 `give up this tick`，
逐 tick 重试 = 玩家感知的爆雷。**本方案给后半授引擎位 + 打旗标后，这两条守卫自然放行**，
`NormaliseSubtypes()` 的 `IsEngine()` 分支也自然成立（无需放宽断言）。

---

## 三、设计（含与 2026-09-21 方案的一处必要偏差）

### 3.1 新旗标（bit 28，不再是"27 位之前的空位"）

`src\train.h` 的 `VehicleRailFlag` 现状：0~22 为上游位，23 `ForceFlipReverse` 为 R3R 退役但**保留占位**，
24~27 = `ArticGroupHead` / `ArticGroupMember` / `SegmentFlipped` / `ForceReserveOnce`。
⇒ 方案原文"用 27 位之前的空位"已过期，**实际可用位是 bit 28**。

- 新增：`FakeDualHead = 28`，语义 =「我是双头机车的后半节，引擎位是人工授的」。
- 底层类型是 `enum class VehicleRailFlag : uint8_t` 但容器是 `EnumBitSet<VehicleRailFlag, uint32_t>`
  （`train.h:57`）⇒ 字段宽度不变、**存档格式不变**；`Train::flags` 由 `sl\vehicle_sl.cpp` 的
  `SLE_CONDVAR_X(Train, flags, SLE_UINT32, ...)` 原样保存，`SegmentFront`（★）/`SegmentBack`（⊗）
  就靠它跨存档往返 ⇒ 新位自动持久化，**无版本号变更**。

### 3.2 新判据（单一真源）

```
IsDualHeadRear()  ==  IsMultiheaded() && (!IsEngine() || HasBit(FakeDualHead))
```

**偏差说明（必须记录）**：2026-09-21 方案把新判据放在 `ground_vehicle.hpp:445` 紧邻 `IsRearDualheaded()` 处。
实际不可行——`FakeDualHead` 是 `VehicleRailFlag`（`train.h`）的位，而 `GroundVehicle<T,Ty>` 看不见
`VehicleRailFlag`（它是 `Train` 专用），`ground_vehicle.hpp` 里没有 `flags` 这个 `VehicleRailFlags` 成员。
⇒ 判据落在 **`Train`（`src\train.h`）** 的 R3R 辅助方法区（紧邻 `IsSegmentFront/IsSegmentBack`）。
18 个调用点全部是 `Train*`，落在 `Train` 上不影响任何一处。

### 3.3 与段机制的接线

- `SetSegmentTailFakeEngine()`：去掉 `IsRearDualheaded()` 早退；改为——若目标车是双头后半节，
  先 `SetEngine()` + 打 `FakeDualHead`（等价于「就地拆成单头」），再授段尾假引擎身份。
- `ClearSegmentTailFakeEngine()`：反向撤销——清假引擎身份；若该车带 `FakeDualHead` 则
  **清引擎位 + 清旗标**，恢复成上游认可的双头后半节。
- `NormaliseDualHeads()`（`train_cmd.cpp:2645`）的守卫 `if (t->other_multiheaded_part == nullptr ||
  t->other_multiheaded_part->IsEngine()) continue;`（`:2658`）**恰好就是新语义所需的行为**
  （伙伴带引擎位就跳过重排、两半留在原地）⇒ **代码不变**，只更新注释（原文自陈其为「consist 畸形」）。

### 3.4 读档期必须保留人工授位

`sl\vehicle_sl.cpp:95-98` 在**每次读档**都会跑（不受存档版本门控）：

```95:98:src/sl/vehicle_sl.cpp
							if (w->IsEngine()) {
								w->ClearEngine();
								w->spritenum++;
							}
```

⇒ 必须改为「`w` 带 `FakeDualHead` 时保留引擎位、不动 `spritenum`」。`:81-85` 的补授分支不必改
（带旗标者已经是引擎）。这是本方案唯一需要触碰的 `saveload` 侧代码。

---

## 四、实现落地清单

1. `src\train.h`：`VehicleRailFlag` 加 `FakeDualHead = 28`；`Train` 加 `IsDualHeadRear()` / `SetFakeDualHead()` / `ClearFakeDualHead()`。
2. 按 §2.2 表替换 18 个调用点（保留 `ground_vehicle.hpp:445` 定义与 `tbtr_template_vehicle.h:161` 模板版），其中
   `train.h:365/377`、`vehicle.cpp:1050`、`train_cmd.cpp:7119` 四处最关键（单元分组 / 车辆计数 / 解挂边界配对）。
3. `NormaliseDualHeads()` 注释更新（代码不变）。
4. `SetSegmentTailFakeEngine()` / `ClearSegmentTailFakeEngine()` 按 §3.3 改。
5. `sl\vehicle_sl.cpp:95-98 ConnectMultiheadedTrains()` 按 §3.4 改。
6. 构建（改 `train.h` ⇒ 由 `R3R_inc_guard.ps1` 判定增量合法性；若判全量则约 20 分钟）。
7. 实测（判据见 §七）。

---

## 五、构建自证

- 复用既有入口 `_tmp_inc_build.cmd`（**未新建任何 `.cmd`**）。
- 护栏 `R3R_inc_guard.ps1` 判定：`src\train.h`(20:33:47) 晚于最新 obj ⇒ `REMOVED 620 object file(s) - upgrading this build to a FULL rebuild`，全量 **692** 步。
- `build\R3R_incbuild.log` 尾行 = `[692/692] Linking CXX executable openttd.exe`；日志内 `error C*` / `fatal error` / `FAILED:` / `build stopped` 计数 **0**。
- `build\R3R_incbuild.done` = `EXIT_CODE=0`。
- `build\openttd.exe` @ **2026-10-10 21:03:46**（**51 738 112 B**）晚于全部改动源码（最新 src = `sl\vehicle_sl.cpp` @20:36:20）。
- obj 均晚于对应 .cpp：`train_cmd.cpp` 20:36:17→obj 20:57:44、`vehicle_cmd.cpp` 20:36:13→obj 20:58:08、`sl\vehicle_sl.cpp` 20:36:20→obj 20:48:16、`economy.cpp` 20:33:58→obj 20:50:51、`autoreplace_cmd.cpp` 20:34:04→obj 20:49:03。
- `read_lints` 7 个改动文件 **0** 条。
- 本轮未新增任何日志字符串（纯语义改动、无探针）⇒ 不做 exe 内字面量命中核对。

---

## 六、未确认项

- **U-1**：双头后半节被授引擎位后，NewGRF 双头相关变量（`u.rail.*` / `position in consist` /
  `PositionHelper`）与车库 GUI（`depot_gui.cpp` 的 `other_multiheaded_part == v->Next()` 视觉配对）
  的行为，需游戏内实看。
- **U-2**：功率/速度方案已静态核实无变化（`ground_vehicle.cpp` 的 `CalculatePower()` 遍历**全部**车辆
  逐车累加，`Train::GetPower()` 对 `IsMultiheaded()` 一律减半 ⇒ 原本就是「前半/2 + 后半/2 = 全额」，
  补引擎位不改变任何一项），但需实测回归确认。
- **U-3**：`autoreplace_cmd.cpp:641` 的 `assert(!old_v->IsRearDualheaded())` 在替换语义上把
  「后半节」判为非法输入；新旧判据对真后半节结果相同（都失败），故替换等价——但若将来出现
  「替换一条含 FakeDualHead 的链」的场景需复核。
- **U-4**：本现场双头链两半在链序上本就隔 2 节（`sep=2 adj=0`，KI-334）系独立存量问题，与本方案无关。

---

## 七、复测判据

1. 双头机车升段 → 段尾具备假引擎身份（`SetSegmentTailFakeEngine` 不再早退）。
2. 含双头机车的链走一次逻辑翻（整链倒置）后：**不再出现** `v_head_not_engine`，
   `NormaliseSubtypes` 不再断言，翻转后链序/朝向/段头身份健康。
3. 拆一半卖不出去（`CmdSellRailVehicle` 仍拒绝后半节）。
4. 功率与最高速与拆分前一致（回归确认 U-2）。
5. 存档往返不丢 `FakeDualHead`；读档后段尾引擎位仍在（§3.4 回归）。
6. 负判据：普通（未授位）双头机车行为逐字不变——`GetNextUnit` 跳过后半、公司车辆计数、
   自动替换、`ReserveCargo` 均无变化。
