# R3R 第 79 轮计划：段的自动吸附（问题一）与双头段「拖不出来」（问题二）

> 玩家拍板（2026-09-21）：问题三「双头机车两头分居不同段 / 翻转后前半节带头」**搁置**，本轮不做支持。
> 本文件只覆盖问题一、问题二。对应 KI-144 / KI-145 / KI-146（见 `R3R_KNOWN_ISSUES.md` 第 79 轮小节）。
>
> **状态（2026-09-21 02:16）：P1 / P2-A / P2-B 已全部落地并增量编译通过**（`build\openttd.exe` 02:16:50，`build\R3R_incbuild.done` = `EXIT_CODE=0`），探针 `NOABSORB-SEG` / `NDH-SEG-KEEP` 已挂上。下一步 = 游戏内复测 T1~T5（见 §6）。

---

## 1. 统一根因（两个问题其实是同一条链的两种入口）

### 1.1 三条既有机制

| 环节 | 代码 | 行为 |
|---|---|---|
| A. 购买瞬间 | `src/train_cmd.cpp` `CmdBuildRailWagon`（≈1659-1679） | 新车厢 `SetFreeWagon()`，只往同 tile 的 **自由车厢链** 上并；段头/段尾不满足 `IsFreeWagon()` ⇒ **购买瞬间不会直接进段** |
| B. 每 tick 主动抓 | `src/train_cmd.cpp` `TrainLocoHandler`（10949-10951） | `track==TRACK_BIT_DEPOT && consist->IsEngine() && !current_order.IsType(OT_GOTO_COUPLE)` ⇒ `NormalizeTrainVehInDepot(consist, true)`；段头即使是**假引擎**（`R3RCreateCarOnlyFormation` 打的）`IsEngine()` 也为真 ⇒ **段会主动吸收同 tile 的自由车厢链 / `FrontWagon` 链** |
| C. 落点是段首之后 | `NormalizeTrainVehInDepot`（1691-1725）→ `MoveRailVehicle(v, u)` → `ArrangeTrains`（2371 `InsertInConsist(dst=u, src=v)`） | `InsertInConsist(dst, chain)` = 插在 `dst` **之后**，而 `dst` 传的是**链头 u** ⇒ 车厢被插到「段首正后方」，不是链尾 |

### 1.2 双头把它变成「夹心」

同一轮 `ArrangeTrains` 末尾无条件跑 `NormaliseDualHeads(*src_head); NormaliseDualHeads(*dst_head);`（2379-2380）。上游语义是「把双头后半节搬到前半节之后、**下一个引擎之前的最后一辆**之后」（2096 的扫描）：

```
[CmdBuildRailWagon / tick 钩子 把 C 插到段首 A 之后]      [A★, C, B(SB)]
   ↓ Arrangement → NormaliseDualHeads(A)：扫描越过 C 停在 C，u=C ≠ B
[RemoveFromConsist(B) + InsertInConsist(C, B)]            [A★, C, B(SB)]   ← C 被夹在两半之间
```

### 1.3 段成员判定把它判成「段内」

`TrainDepotGetSegmentFront`（`src/depot_gui.cpp` 160-172）从被拖车辆沿 `Previous()` 回溯，先遇 `SegmentFront`(★) 即判段内、先遇 `SegmentBack` 即判段外。C 现在位于 ★(A) 与 SB(B) 之间 ⇒ **判段内** ⇒ `TrainDepotMoveVehicle`（depot_gui.cpp 257-264）改走 `TrainDepotMoveSegment` ⇒ **拖 C 变成搬整段，再也拖不出来**（用户看到的 `[《——ABC——》]`）。

### 1.4 结论

- 问题一的「自动吸附」= 1.1-B（tick 钩子）+ 1.1-C（落点在段首后）。
- 问题二的「拖不出去」= 1.2（双头后半节被推到 C 之后，把 C 夹进 ★..SB）+ 1.3（段成员判定只看区间）。
- 两者**都可以由「段不再自动吸收」+「段内双头两半永远相邻」两条修复同时根治**。

---

## 2. 问题一 修复计划（P1：段永不自动吸收）

**目标**：链内存在任何 `SegmentFront`(★) 的列车，不再自动把 depot 里的自由车厢链并进来。普通（非段）列车的原生行为保持不变。

### 2.1 落点（纯 `.cpp`）

1. `src/train_cmd.cpp`，紧邻 `NormalizeTrainVehInDepot` 新增文件内静态 helper：

```cpp
/** R3R: does this chain already contain a segment (a SegmentFront marker)? */
static bool R3RChainHasSegment(const Train *t)
{
	for (const Train *v = t; v != nullptr; v = v->Next()) {
		if (v->IsSegmentFront()) return true;
	}
	return false;
}
```

2. `NormalizeTrainVehInDepot()` 入口加守卫（覆盖两个调用点：tick 钩子 `train_cmd.cpp:10949` 与建车 `vehicle_cmd.cpp:217`）：

```cpp
void NormalizeTrainVehInDepot(const Train *u, bool include_front_wagon)
{
	assert(u->IsEngine());
	/* R3R: a segment is a complete, self-contained unit -- it must never
	 * silently swallow the free wagons standing around in the depot. */
	if (R3RChainHasSegment(u)) return;
	...
```

   建车调用点（`CmdBuildRailVehicle`）传的 `u` 是刚造出的新机车，链内不可能有 ★ ⇒ 不受影响。

### 2.2 可选诊断

tick 钩子命中守卫时写一行 `NOABSORB-SEG veh=<head> n=<自由链节数>` 到 `R3R_debug.log`（边沿触发，KI-14 口径，不刷屏），便于复测核对。

### 2.3 明确不改

- **不改** `CmdBuildRailWagon` 的自由车厢自动连挂（只并 `IsFreeWagon()` 链，本来就碰不到段）。
- **不改** `include_front_wagon` 参数本身（`IsFrontWagon()` 目前全仓无生产者，属历史残留；留着不影响）。
- **不改** 非段列车在 depot 捡自由车厢的行为。

### 2.4 副作用 / 风险

- 玩家要往段上挂车厢，必须走显式路径（depot 拖动 / 连挂订单）——这正是 R3R「阶段一：depot 工具化」的口径（记忆 50765346）。
- 若将来有代码依赖「段停 depot 时被自动补齐车底」，会被这条守卫挡住 ⇒ 需在复测里确认没有此类依赖（T1 覆盖）。

---

## 3. 问题二 修复计划（P2：双头段不再把车厢夹进 ★..SB）

### 3.1 P2-A 主修：`NormaliseDualHeads` 在段内改为「紧贴」，绝不跨越段边界

`src/train_cmd.cpp` `NormaliseDualHeads`（2079-2105）当前是上游原样：

```cpp
if (t->other_multiheaded_part == nullptr || t->other_multiheaded_part->IsEngine()) continue;
/* Make sure that there are no free cars before next engine */
Train *u;
for (u = t; u->Next() != nullptr && !u->Next()->IsEngine(); u = u->Next()) {}
if (u == t->other_multiheaded_part) continue;
RemoveFromConsist(t->other_multiheaded_part);
InsertInConsist(u, t->other_multiheaded_part);
```

计划改为：**当前半节位于某个段内部时，后半节一律紧贴前半节**（`InsertInConsist(t, other)` = 插在 `t` 之后），不再做「搬到下一个引擎之前」的扫描。

新增同文件静态 helper（与 depot 侧 `TrainDepotGetSegmentFront` 同构，但只回答「在不在段内」）：

```cpp
/** R3R: is this vehicle inside a segment (i.e. between a SegmentFront and the
 *  SegmentBack that closes it)? */
static bool R3RIsInsideSegment(const Train *t)
{
	for (const Train *v = t; v != nullptr; v = v->Previous()) {
		if (v->IsSegmentFront()) return true;               /* reached the segment's front */
		if (v->Previous() != nullptr && v->Previous()->IsSegmentBack()) return false; /* crossed the boundary */
	}
	return false;
}
```

`NormaliseDualHeads` 里在既有 KI-143 守卫之后插入：

```cpp
		/* R3R: inside a segment the two halves must stay adjacent. Upstream
		 * moves the rear half behind the last wagon following the front half,
		 * which would sandwich any wagon that was attached behind the segment
		 * between ★ and SegmentBack -- the depot would then read that wagon as
		 * part of the segment and it could no longer be dragged out alone
		 * (see KI-145). Keep it right behind the front half instead. */
		if (R3RIsInsideSegment(t)) {
			Train *other = t->other_multiheaded_part;
			if (t->Next() != other) {
				RemoveFromConsist(other);
				InsertInConsist(t, other);
			}
			continue;
		}
```

**两种入口都被覆盖**：

| 入口 | 拖动后的链 | 修复后 |
|---|---|---|
| 拖到段后（`dst = B`，即 SB） | `[A★, B(SB), C]` | 两半节已相邻 ⇒ no-op ⇒ C 留在 SB 之后 ⇒ `TrainDepotGetSegmentFront(C)` 先遇 SB ⇒ **段外，可单独拖出** |
| 拖到两半之间（`dst = A`） | `[A★, C, B(SB)]` | B 上移到 A 之后 ⇒ `[A★, B(SB), C]` ⇒ **C 被挤出段外** |
| 非段双头机车（任意） | 不变 | `R3RIsInsideSegment(t)==false` ⇒ **完全保持上游行为**（后半节仍被推到车厢组末尾） |

**为何不选「扫描到 `SegmentBack` 就停」这一更小的改法**：它能修「拖到段后」，但修不了「拖到两半之间」（`[A★, C, B(SB)]` 里扫描结果 u 正好就是另一半，判定为 no-op，C 继续被夹）。只有「段内紧贴」能同时覆盖两种入口。

### 3.2 P2-B 防御：depot 侧把「夹心车」判为段外（治旧存档）

`src/depot_gui.cpp` `TrainDepotGetSegmentFront`（160-172）在回溯前加一条特例：

```cpp
	/* R3R: a wagon sandwiched between the two halves of a dual-headed engine is
	 * NOT a segment member, even though it sits between ★ and SegmentBack. This
	 * only happens with consists saved before the KI-145 fix. */
	if (v->Previous() != nullptr && v->Previous()->IsMultiheaded() && v->Previous()->IsEngine() &&
			v->Previous()->other_multiheaded_part == v->Next()) {
		return nullptr;
	}
```

作用：

- **已存在的坏存档**（玩家此前拖出的 `[《——ABC——》]`，且落盘过）也能把 C 单独拖出来；拖动触发的 `ArrangeTrains` → P2-A 会把 B 上移，坏布局**就地自愈**。
- 只影响「被夹在两半之间」这一种形态，正常段的判定不变。
- 已知限制：若 C 被夹得更深（`A, x, C, y, B`），这条特例不命中，仍判段内 —— 记入 KI-145 的「已知限制」，留待 P2-C。

### 3.3 P2-C 后续（本轮不做）

段边界目前有 **三套口径**（`GetLastChainVehicle` / 「到下一个 ★ 或链尾」/ 翻转时的「块首块尾」），加上 depot 侧「找不到 `SegmentBack` 就回退到链尾」——任何一次重链不更新标记都会让段「吃掉」后面的车。建议后续单独一轮：收敛成单一 helper，并给「★..SB 区间内出现非成员车辆」加不变式探针（写 `R3R_debug.log`）。

### 3.4 明确不做

- 不给双头后半节打引擎位（第 78 轮 KI-143 刚修：`SetSegmentTailFakeEngine` 对 `IsRearDualheaded()` 早退，必须保留）。
- 不引入「段成员」车旗：要动 `src/train.h` + 存档格式 ⇒ 按 KI-15 / 记忆 66636022 必须全量重编，且要处理旧档兼容，代价远大于收益。

---

## 4. 问题三 处置：搁置（玩家拍板 2026-09-21）

- 玩家口径：**游戏没有必要提供「双头机车两头分居不同段」的能力**，本轮起不做支持、不做校验/拒绝逻辑的专项开发。
- 因此 KI-143 结尾那句「是否应当禁止升段 / 只允许整段翻转后仍由前半节带头，需要玩家拍板」**结案 = 搁置**（登记为 KI-146）。
- 残余影响仅限「段内彻底倒序把后半节顶到段首」这类畸形场景（KI-141 / KI-143 口径），本轮不动。若日后要兜底，最省事的落点是升段/翻转后检测「新段首是否 `IsRearDualheaded()`」，命中即回滚（`STR_ERROR_REAR_ENGINE_FOLLOW_FRONT`）。

---

## 5. 附：本轮澄清「倒车与引擎位无关」（回应提问）

**结论：后半节不需要引擎位也能倒车。`IsEngine()` 与「能不能带队（cab）」是两个不同的概念。**

`src/train_cmd.cpp:12580-12591`：

```cpp
bool Train::CanLeadTrain() const
{
	/* NewGRFs can allow unpowered wagons to lead trains. */
	if (this->GetEngine()->info.extra_flags.Test(ExtraEngineFlag::HasCab)) return true;
	if (this->IsArticulatedPart()) return this->GetFirstEnginePart()->IsEngine();
	return this->IsEngine() || this->IsRearDualheaded();      /* ← 后半节在这里为真 */
}
```

即：双头后半节（`IsRearDualheaded()`）**天然 `CanLeadTrain()==true`**；NewGRF 的 `ExtraEngineFlag::HasCab` 是第二条路径（无动力车厢也能带队）。

`ReverseTrainDirection()` 的选择分支（`src/train_cmd.cpp:3499`）：

```cpp
} else if (consist->vehicle_flags.Test(VehicleFlag::DrivingBackwards) ||
           _settings_game.difficulty.train_flip_reverse_allowed == TrainFlipReversingAllowed::None ||
           consist->Last()->CanLeadTrain()) {
	/* The train will back up. */
	for (Train *u = consist; u != nullptr; u = u->Next()) {
		u->vehicle_flags.Flip(VehicleFlag::DrivingBackwards);
		...
	}
```

- 「倒车」= 给全链打上 `DrivingBackwards`，**物理链序与链头都不变**，只是把「前进方向」的解释取反（`SetMovingDirection()`：`direction = IsDrivingBackwards() ? ReverseDir(d) : d`）。
- 动力来源不变：牵引/加速遍历整条链的引擎车辆，与 `DrivingBackwards` 无关 ⇒ 双头前半节的功率照用。
- `allow_trains_to_flip = none`（`TrainFlipReversingAllowed::None`）时，连「车辆视图掉头按钮 / 订单调向」的 `force_end_swap` 也一并降级为倒车（该分支上方的 KI-127 注释）⇒ 全程只靠 `DrivingBackwards`。
- 唯一与「尾部能否带队」相关的是限速：`!consist->Last()->CanLeadTrain()` 时 32 km/h。
- 上游另有物理掉头分支 `ReverseTrainSwapVehicles(consist)`（3496）：那才需要「另一端能带队」，与本议题无关。

⇒ 第 78 轮「禁止给后半节引擎位」不会影响倒车能力，只影响 `NormaliseDualHeads` 的稳定性判定。

---

## 6. 验证与构建

**改动文件（已落地）**：`src/train_cmd.cpp`（P1 helper + 入口守卫 + `NOABSORB-SEG` 探针、P2-A + `NDH-SEG-KEEP` 探针、`R3RDbgEdgeTag` 两个新 tag、两个 emitter 的前置声明与 `R3R_PROBES` 门控宏）、`src/depot_gui.cpp`（P2-B 特例）⇒ **无 `src/*.h` 变更，纯 `.cpp`，增量构建合法**（KI-15 / 记忆 66636022）。

构建与判据：

1. `_tmp_inc_build.cmd` ⇒ 末行 `[n/n] Linking CXX executable openttd.exe`，标记 `build\R3R_incbuild.done` = `EXIT_CODE=0`。
2. `build\openttd.exe` 时间戳须晚于 `train_cmd.cpp.obj` / `depot_gui.cpp.obj`。
3. `read_lints` 两个文件零诊断。

**构建结果（2026-09-21 02:16:50）**：增量构建通过 —— `build\R3R_incbuild.done` = `EXIT_CODE=0`，`build\R3R_incbuild.log` 末行 `[3/3] Linking CXX executable openttd.exe`；时间戳链 `src/train_cmd.cpp` 02:10:53 → `train_cmd.cpp.obj` 02:14:01 → `build\openttd.exe` **02:16:50**（50 792 960 B）；`read_lints` 两个文件零诊断。**编译通过 ≠ 实测通过**：T1~T5 仍待在游戏内复测。

游戏内测试用例：

| 编号 | 场景 | 期望 |
|---|---|---|
| T1 | 三种段（纯车厢段 / 机车+车厢段 / 双头段）分别停在 depot，旁边买 1~3 节车厢 | 车厢保持独立自由车厢链；段零变化；卖出车厢不影响段。双头段升段/降段仍正常 |
| T2 | `[《——AB——》]`，把 C 拖到段**后** | 链序 `[A,B,C]`；C 可单独拖出、可拖到空行 |
| T3 | 同上，把 C 拖到 A、B **之间** | 自动变 `[A,B,C]`；C 仍可单独拖出 |
| T4 | 载入已落盘的 `[《——ABC——》]` 形状旧档 | 能单独拖出 C，且链被就地修正 |
| T5 | 回归：非段双头机车拖车厢；普通机车 depot 捡自由车厢；段升段/降段/连挂/解挂；重跑第 78 轮场景 A/B | 行为不变、不冻结 |

探针（边沿触发，已落地）：`NOABSORB-SEG veh=<段链头> n=<同 tile 自由车厢节数> len=<段链长>`（P1 命中，tag `R3REDGE_NOABSORBSEG`）、`NDH-SEG-KEEP front=<前半节> rear=<后半节> moved=<0/1>`（P2-A 命中，`moved=1` 即夹心车被挤出段外那次，tag `R3REDGE_NDHSEGKEEP`）。两处调用点走 `R3R_PROBE_NOABSORB_SEG` / `R3R_PROBE_NDH_SEG_KEEP` 宏，`R3R_PROBES=0` 时连参数一起消失（发布版仍可证明无探针）。

---

## 7. 风险与已知限制

1. **P2-A 改变「段内双头的车序」**：可能影响 (a) NewGRF `position-in-segment` 逐节计数（第 77 轮 `PositionHelper` / `newgrf_engine.cpp`）；(b) 打散组与角色位（KI-141 / KI-142）。⇒ T1/T5 必须目测 GRF 选图，且覆盖「含打散组的段 + 双头」。
2. **段尾归属可能变化**：双头段 `[A★, B(SB)]` 与 `[A★, w1, B(SB)]` 里 SB 落在双头后半节或普通车厢上，P2-A 不改变其位置（只是禁止 B 被推到「更后面」），但若现有代码依赖「升段时 `GetLastChainVehicle` 取到后半节」（第 78 轮已改为对 `IsRearDualheaded` no-op）需在 T1 复核。
3. **P2-B 只覆盖「紧邻夹心」**：更深的夹心（`A, x, C, y, B`）仍判段内 ⇒ 记为已知限制。
4. 若复测发现 P2-A 引起 GRF 选图回归，回退口径：把 P2-A 降级为「扫描遇 `SegmentBack` 即停」（只修 T2 类入口），并把 T3 类入口改为「拖动时拒绝插入段内 + 提示」，需要另开一轮。
