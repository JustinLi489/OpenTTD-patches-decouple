# 第 304 轮：读旧 R3R 存档时 `vehicle.cpp:3094` 断言崩溃（2026-10-25）

> 状态：取证中。本文件按铁律"先落盘再取证"建立，后续每组取证结论**立刻**追加，不攒到最后。

## 0. 玩家报告原文

「新的版本读取旧R3R存档，vehicle.cpp 3094行断言崩溃了」

- 「新的版本」= 2026-10-25 22:36:29 编译的内测版（`build\openttd.exe`，52,228,608 B），即**两代预留账本已删净**的那一版（第 303 轮续）。
- 「旧 R3R 存档」= 之前版本产出的 `.sav`，可能含已删存档块（`R3RL` 第二代预留账本 / `R3VP` 车辆停放状态 / `SYRD` 站场 等）。

## 1. 断言原文（已核实，唯一一处）

`src\vehicle.cpp:3088-3096`：

```cpp
void FreeUnitIDGenerator::ReleaseID(UnitID index)
{
	if (index == 0 || index == UINT16_MAX) return;

	index--;

	assert(index / BITMAP_SIZE < this->used_bitmap.size());   // <= 3094 就是这一行
	ClrBit(this->used_bitmap[index / BITMAP_SIZE], index % BITMAP_SIZE);
}
```

**语义**：释放在用的"单位号"（车辆 unit number）时，要求该号所在 bitmap 槽位已经存在。

`used_bitmap` **只在 `UseID()` 里按需扩容**（`src\vehicle.cpp:3071-3082`，`slot >= size` 才 `resize`），`NextID()`（`:3056`）也只读不扩。
⇒ **断言唯一可能被踩的方式：对一个"从未 `UseID()` 过、且比当前最大已用号还大"的号调用 `ReleaseID()`。**

**必然推论**：这不是"号池满了"或"号重复"，而是**释放顺序/来源出了问题** —— 有代码在号池还没长到这个槽位时就去清位。

## 2. 疑点（待验证）

- **疑点 1（首要）**：读档阶段 `unitnumber_backup`（R3R 停放车号，随 `R3VP` 稀疏块入库）被 `ReleaseID()`，而该备份号在本次读档的 `used_bitmap` 里从未 `UseID()` 过 ⇒ 越界。旧档里备份号可能远大于活车最大号（历史号、或跨存档残留），正好越过 `used_bitmap.size()`。
- **疑点 2**：读档顺序 —— 若"先 Release 备份号、后 Use 活车号"，则 `used_bitmap` 还是空的/很短。
- **疑点 3**：与本轮删账本的关系 —— `R3VP` 不是账本（是停放状态块），未删；但**第二代账本 `R3RL` 已删**，若旧档读入路径发生了变化（丢弃 chunk），可能改变车辆读档后的字段状态。需确认 `BodyType`/`unitnumber` 处理是否受牵连。
- **疑点 4**：断言在 `ReleaseID`，调用者未必是车辆本身 —— `OrderList`/`CoupleGroup` 等也各有号池，但 `FreeUnitIDGenerator` 只服务车辆 unit number（需核实实例清单）。

## 3. 取证计划（按配额逐组做，每组做完立刻回填本节）

- [x] G1：`ReleaseID(` / `UseID(` 全仓调用点全集（已完成，见 §3.1）
- [x] G2：读档路径里 `unitnumber` / `unitnumber_backup` 的占号顺序（已完成，见 §3.2）
- [x] G3：`Load_R3VP`（停放状态块）字段布局与旧档兼容性 —— **已排除**"字段错位读成垃圾值"（见 §3.3，`NSL` 表头驱动）
- [x] G4：`R3RRestoreUnitNumber` / `R3RUnitNumberParkedByOther` / 各 Release 调用点是否有"未 Use 就 Release" —— **已坐实**读档期 `ReleaseID` 早于 `UseID`（见 §3.4）
- [ ] G5：**未取得**（等玩家 `crash-*.log`）；本轮结论来自静态时序推导 + 修法两处兜底，不阻塞落地（见 §7 U-1）

### 3.1 G1：号池只有一处扩容入口（已核实）

`UseID(` 调用点（全仓 24 命中，扣掉同名 `GetTranslatedHouseID` 等）：
- `src\sl\vehicle_sl.cpp:662`、`:673`（**读档占号，R3R 加的第二条**）
- `src\vehicle_cmd.cpp:245`、`:296`、`:302`（克隆/建车回滚）
- `src\train_cmd.cpp:3847`、`:3886`、`:7787`、`:7943`、`:8022`、`:9536`
- `src\economy.cpp:497`、`:545`（AI/初始建车）

`ReleaseID(` 调用点：
- `src\vehicle.cpp:1187`（`Vehicle::PreDestructor` 释放 `unitnumber`，**无条件，不看 IsPrimaryVehicle**）
- `src\vehicle.cpp:1210`（同函数释放 `unitnumber_backup`，条件 `!=0 && !=unitnumber && !R3RUnitNumberOwnedByOther()`）
- `src\vehicle.cpp:3905`
- `src\vehicle_cmd.cpp:944`、`:950`、`:979`、`:983`
- `src\train_cmd.cpp:3742`（`ReleaseID(parked)`）、`:7783`、`:7939`、`:9871`、`:11283`、`:12746`

### 3.2 G2：读档确实给备份号占位（已核实）

`src\sl\vehicle_sl.cpp:660-674`（`AfterLoadVehiclesPhase2` 的逐车循环内）：

```cpp
if (part_of_load && v->unitnumber != 0) {
    if (v->IsPrimaryVehicle()) {
        Company::Get(v->owner)->freeunits[v->type].UseID(v->unitnumber);
    } else {
        v->unitnumber = 0;                    // 非 primary 车号清零，不占池
    }
}
if (part_of_load && v->unitnumber_backup != 0) {   // R3R 第 144 轮加
    Company::Get(v->owner)->freeunits[v->type].UseID(v->unitnumber_backup);
}
```

⇒ **只要 `unitnumber_backup` 在读这一刻已是正确值，它就已经占到池**，后续 `ReleaseID(backup)` 不会越界。

⇒ 于是断言只能由两类原因产生：
- **A 类（值错）**：`unitnumber_backup` 在读档时是个**垃圾/超大值**（比如旧档字段错位），`UseID` 也用了同一个错值 —— 那反而不越界（同一 slot）… 所以 A 类单独不能解释，除非 `UseID` 那一支被跳过（`part_of_load==false` 或 `unitnumber_backup==0` 而之后才被写成大值）。
- **B 类（顺序错）**：某个 `ReleaseID(x)` 里的 `x` **不是**从 `unitnumber`/`unitnumber_backup` 读出来的存档值，而是 R3R 运行时算出来的新号 ⇒ 从未 `UseID` ⇒ 越界。重点看 `train_cmd.cpp:3742`（`ReleaseID(parked)`）与 `:11283`、`:12746`。

### 3.3 G3：R3VP 是表头驱动命名字段块（**排除**"字段错位读成垃圾值"）

`src\sl\couple_group_sl.cpp:123-151` 的 `_r3r_park_desc[]` 全部是 `NSL(...)`，`Load_R3VP()` 用 `SlTableHeaderOrRiff(_r3r_park_desc)`（`:171`）⇒ 旧档缺的字段按默认值走，**不会错位**。`unitnumber_backup` 是 `SLE_UINT16`（`:134`）。

⇒ 值错（A 类）基本排除。**问题在顺序。**

### 3.4 G4：根因锁定 —— **读档期 `ReleaseID()` 早于 `UseID()`**

三处锚点，闭合：

1. `src\sl\vehicle_sl.cpp:488-529`
```cpp
void AfterLoadVehiclesPhase2(bool part_of_load)
{
    ...  // 493-507: orders_backup 的 Initialize
    for (Vehicle *v : Vehicle::IterateFrontOnly()) {   // :509
        ...
        case VehicleType::Train:
            if (t->IsFrontEngine() || t->IsFreeWagon()) {
                t->ConsistChanged(CCF_SAVELOAD);                 // :520
                if (part_of_load) R3RRebuildCouplePriorities(t);  // :529  ← 释放/删除发生在这一行
            }
```
2. `src\train_cmd.cpp:7306-7315`：`R3RRebuildCouplePriorities()` **第一句**就是
```cpp
R3RReleaseStaleNonSegmentUnitNumbers(chain);   // :7315
```
3. `src\train_cmd.cpp:3737-3748`：`R3RReleaseParkedUnitNumber()` →
```cpp
Company::Get(v->owner)->freeunits[v->type].ReleaseID(parked);   // :3742
```
而号池的占位在 `src\sl\vehicle_sl.cpp:660-674`，**是同一次 `IterateFrontOnly()` 循环里、同一辆车处理流程的更后面**：
```cpp
if (part_of_load && v->unitnumber != 0) { ... UseID(v->unitnumber) ... }   // :662
if (part_of_load && v->unitnumber_backup != 0) { ... UseID(v->unitnumber_backup) ... }  // :673
```

⇒ 读档时序：`ConsistChanged`(:520) → `R3RRebuildCouplePriorities`(:529) → `R3RReleaseStaleNonSegmentUnitNumbers`(:7315) → `ReleaseID`(:3742) —— **此时 `used_bitmap.size()` 还是 0** ⇒ `assert(index / BITMAP_SIZE < size)` **必然失败**。

⇒ 触发条件三条同时成立（正是"旧 R3R 存档"的特征）：
1. 链上有车满足 `unitnumber_backup != 0 && unitnumber == 0 && !IsSegmentFront()`（旧版 exe 把**非段车**的号冻在备份里 —— 第 301 轮 KI-475 P2 要清理的正是这批存量）；
2. `part_of_load == true`（真读档）；
3. 该链是迭代中第一条被处理的车，或前面车的号都太小 —— 只要 `parked / BITMAP_SIZE >= used_bitmap.size()` 就崩。

⇒ **新档不崩、旧档崩**，与玩家现象逐字吻合。

## 4. 结论

**根因（已坐实）**：`R3RRebuildCouplePriorities()` 里的"存量幽灵号回收"（第 301 轮 KI-475 P2 新增，`train_cmd.cpp:7315`）在 `AfterLoadVehiclesPhase2()` 的号池占位（`vehicle_sl.cpp:660-674`）**之前**执行，`ReleaseID()` 打到尚未扩容的 `used_bitmap` 上 ⇒ `vehicle.cpp:3094` 断言崩溃。

即：**这不是账本删除引起的，而是第 301 轮读档清理与第 144 轮读档占号的时序冲突**，只在含"非段车冻结号"的旧存档上暴露。

**修法（两层，建议都做）**

- **修法 1（治本）**：`src\sl\vehicle_sl.cpp` 的 `AfterLoadVehiclesPhase2()` 开头（`part_of_load` 为真时）**先全量占号**，再做任何语义重建：
  ```cpp
  if (part_of_load) {
      for (const Vehicle *v : Vehicle::Iterate()) {
          if (v->owner >= MAX_COMPANIES) continue;
          if (v->unitnumber != 0 && v->IsPrimaryVehicle()) Company::Get(v->owner)->freeunits[v->type].UseID(v->unitnumber);
          if (v->unitnumber_backup != 0) Company::Get(v->owner)->freeunits[v->type].UseID(v->unitnumber_backup);
      }
  }
  ```
  660-674 原逻辑**保持不动**（`UseID` 是 `SetBit`，幂等）。这样 `delete v`（`:649` 的 UFO 分支）与所有读档期释放也一并被保护。
- **修法 2（防御）**：`src\train_cmd.cpp` 的 `R3RReleaseParkedUnitNumber()` 在 `ReleaseID()` 前先 `UseID(parked)`（幂等占位再还），使任何调用顺序都不越界。
- **特意不做**：不改 `FreeUnitIDGenerator::ReleaseID` 的断言（那是上游正确的不变式）；不给 `R3VP` 加版本号（表头驱动已足够）。

## 5. 复测判据

1. 载入同一份旧 R3R 存档不再崩溃，进入游戏正常。
2. `build\R3R_debug.log` 出现 `UNIT-RELEASE veh=N id=X reason=not-segment-load` 或 `UNIT-DROP ... owned-elsewhere`（证明回收逻辑仍然执行），**且**不再有崩溃。
3. 载入后新造一辆车，编号从"下一个空闲号"开始（不出现 1、2 之后跳到 5、6 且空缺号不可回收）。
4. 负判据：段首的 `unitnumber_backup` 不被回收（`IsSegmentFront()` 守卫生效），段的车号界面不变。
5. 负判据：新存档正常往返（读档后号池与存档一致，无重复号）。
6. `read_lints` 0 条，构建 `EXIT_CODE=0` + `[n/n] Linking`。

## 6. 落地（2026-10-25，已改码 + 已编译）

### 6.1 改动清单（仅 .cpp，未碰任何 `src\*.h` ⇒ 增量合规）

| 文件 | 位置 | 改动 |
|---|---|---|
| `src\sl\vehicle_sl.cpp` | `AfterLoadVehiclesPhase2()` 开头，`SCOPE_INFO_FMT` 之后（现 `:493-517`） | **修法 1（治本）**：`if (part_of_load) { for (Vehicle *v : Vehicle::Iterate()) { ... UseID(v->unitnumber)（仅 IsPrimaryVehicle）/ UseID(v->unitnumber_backup) } }`，并附 12 行注释说明 `used_bitmap` 只在 `UseID()` 扩容、`ReleaseID()` 断言槽位存在。原 `:686-700` 的占号块**一字未动**（`UseID` = `SetBit`，幂等）。 |
| `src\train_cmd.cpp` | `R3RReleaseParkedUnitNumber()`（现 `:3737-3754`） | **修法 2（防御）**：`ReleaseID(parked)` 之前先 `UseID(parked)`，并附注释（读档期本函数可能早于占号循环，先占再还 ⇒ 任何调用顺序安全）。 |

### 6.2 覆盖面（为什么这两处就够）

提前全量占号发生在 `AfterLoadVehiclesPhase2()` 的最前面，而本函数里后续**所有**释放/删除都排在其后：
- `:529` 的 `R3RRebuildCouplePriorities()` → `R3RReleaseStaleNonSegmentUnitNumbers()` → `ReleaseID(parked)`（本轮崩溃点）；
- `:649` 一带的 `delete v`（UFO 分支）→ `Vehicle::PreDestructor` 的 `vehicle.cpp:1187` / `:1210` 两处无条件 `ReleaseID`；
- 读档链重建里 `train_cmd.cpp:7786-7789` / `:7942-7945`（取回自己的号时的 `ReleaseID(chain->unitnumber)`）。

⇒ 一律落在"池已填好"之后，`used_bitmap.size()` 已覆盖存档内最大号，断言不再有机会被踩。
另：`ReleaseID` 的其它调用点（`vehicle_cmd.cpp:944/950/979/983`、`train_cmd.cpp:9876`、`:11288`、`:12751`、`vehicle.cpp:3905`）全在**命令/运行期**路径，号池此时早已由正常建车 `UseID` 填过，不在本缺陷面内。

### 6.3 构建自证

- 复用既有 `_tmp_inc_build.cmd`（**未新建任何 .cmd**）；输出首行 `GUARD: incremental is safe (no header/lang file is newer than the newest object)` ⇒ 增量合规。
- 日志尾 `[4/4] Linking CXX executable openttd.exe`（Linking 命中 1 次）。
- `build\R3R_incbuild.done` = `EXIT_CODE=0`（2026-10-25 23:26:03）。
- `error C` / `fatal error` / `FAILED:` / `build stopped` 计数 **0**。
- obj 时间戳：`build\CMakeFiles\openttd_lib.dir\src\train_cmd.cpp.obj` @ **23:24:40**、`build\CMakeFiles\openttd_lib.dir\src\sl\vehicle_sl.cpp.obj` @ **23:24:40**，均晚于对应 `.cpp`。
- 产物：`build\openttd.exe` = **52,230,144 B @ 2026-10-25 23:25:53**（晚于两个 obj；上一版 52,228,608 B @ 22:36:29 ⇒ 确为本轮新产物）。
- `read_lints`（`src\sl\vehicle_sl.cpp`、`src\train_cmd.cpp`）= **0 条**。
- 本轮未新增日志字面量，故不做 exe 串核对（`UNIT-RELEASE` / `UNIT-DROP` 为既有串）。

## 7. 状态与未做

- 状态：**已实现 + 已编译**，游戏内复测待做（判据见 §5 六条，核心是 §5-1 不崩 + §5-2 仍有 `UNIT-RELEASE`/`UNIT-DROP`）。
- 未做 / 未确认：
  - **U-1**：未取到玩家侧调用栈（`crash-*.log` 未提供）。本轮的调用点认定来自静态时序推导（§3.4），若复测仍崩，需 `crash-*.log` 锁定是否另有更早（`AfterLoadVehiclesPhase1` 之前）的释放点。
  - **U-2**：`R3VP` 稀疏块在"旧档无该块"与"旧档有 R3VP 但字段更少"两种情形的往返仍是按表头默认值，未单独实测（§3.3 已排除值错，但未跑存档往返）。
  - **U-3**：本次只修"崩"，不改第 301 轮 KI-475 P2 的回收口径（非段车幽灵号仍会还池）。
- 关联 KI：`KI-476`（`R3R_KNOWN_ISSUES.md` 末尾「第 304 轮」小节）。
