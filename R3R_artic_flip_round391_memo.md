# 第 391 轮：对铰接组执行「单节翻转」（Ctrl+左键）

日期：2026-11-04
状态：分析 + 实现中（本轮先落盘需求与方案，再改码）

## 一、玩家需求（原话要点）

> 原生 OpenTTD（含 JGRPP）对着**非铰接式、非双头**的列车执行 Ctrl+鼠标左键，可以反转车厢的图像（应该就是把 `vehicleflag::flip` 置 1 或 0）。
> 现在想让**铰接式**也能执行这样的操作。初步推测机制：
> 1. 反转该车厢的 `vehicleflag::flip`；
> 2. 打散它为假铰接式（如果是真铰接式）；
> 3. 反转它的 `position_in_articulated_part`（或许这个可以寄宿在 `vehicleflag::flip` 上面）。

## 二、现有代码锚点（取证）

| 位置 | 内容 |
| --- | --- |
| `src/depot_gui.cpp:1755-1756` | 车库内 Ctrl+左键点击**同一节**（未拖拽）⇒ `Command<Commands::ReverseTrainDirection>::Post(..., true)`，即 `reverse_single_veh = true`。 |
| `src/train_cmd.cpp:5672-5741` | `CmdReverseTrainDirection()`。`reverse_single_veh` 分支见下。 |
| `src/train_cmd.cpp:5683-5685` | **早退闸门**：`if (v->IsMultiheaded() \|\| EngInfo(v->engine_type)->callback_mask.Test(VehicleCallbackMask::ArticEngine)) return CommandCost(STR_ERROR_CAN_T_REVERSE_DIRECTION_RAIL_VEHICLE_MULTIPLE_UNITS);` ⇒ 双头车与**带 ArticEngine 回调的引擎（真铰接父车）**被拒绝，这正是玩家遇到的限制。 |
| `src/train_cmd.cpp:5687-5691` | 还要求整列 `front->IsStoppedInDepot()`（或虚拟车），故该操作只在车库内。 |
| `src/train_cmd.cpp:5693-5700` | Execute：`v->flags.Flip(VehicleRailFlag::Flipped)` → `front->ConsistChanged(CCF_ARRANGE)` → 刷新车库/详情/视图/列表窗口。 |
| `src/train.h:27-58` | `VehicleRailFlag` 位占用：0-22 原生，23 `ForceFlipReverse`（退役保留），24 `ArticGroupHead`，25 `ArticGroupMember`，26 `SegmentFlipped`，27 `ForceReserveOnce`，28 `FakeDualHead`。**下一个空位 = 29**；底层是 `EnumBitSet<VehicleRailFlag, uint32_t>`，加位不改变存档布局。 |
| `src/train.h:206-243` | 组角色语义层：`IsArticGroupMember()` 同时认「真 artic part（subtype 位）」与「`ArticGroupMember` 角色位」；`IsArticGroupHead()` 同时认「父车（`Next()` 是组员）」与角色位；`InArticGroup()` 为二者之或；`HasDearticulatedGroupRole()` 只认显式角色位。 |
| `src/vehicle_cmd.cpp:522-587` | `R3RDearticulateOneGroup(Train *v)`：把**一个**真 artic 组打散——父车 `SetArticGroupHead()` + 烘焙 weight/power/max_speed 均分；每个 part `ClearArticulatedPart()`、`SetEngine()`/`SetWagon()`、`SetArticGroupMember()` + 烘焙。`spritenum` 与 `cached_veh_length` 不变。 |
| `src/vehicle_cmd.cpp:659-674` | `DearticulateChainWithSnapshot(Train *head)`：从 `head` 起遍历整链，对每个「`Next()->IsArticulatedPart()`」的真组调 `R3RDearticulateOneGroup`。**会打散整链所有真组**，非单组。 |
| `src/vehicle_cmd.cpp:692-711` | `RearticulateChain()`：段降级时才把假铰接恢复为真铰接（清烘焙、清角色位、成员 `SetArticulatedPart()`）。⇒ 打散是**不可逆的玩家操作**，除非之后走段降级。 |
| `src/newgrf_engine.cpp:880-903` | NewGRF 变量 `0x4D`（Position within articulated vehicle）：`artic_before` 沿 `Previous()` 数 `IsArticGroupMember()`，`artic_after` 沿 `Next()` 数 `HasArticulatedPart()`，返回 `before \| after << 8`。已有 `IsSegmentFlipped() && HasDearticulatedGroupRole()` ⇒ **交换两字节**的分支（段逻辑翻转时报告翻转前 rank）。 |
| `src/newgrf_engine.cpp:1827-1833` | `0x4D` 有缓存 `NCVV_POSITION_IN_VEHICLE`，缓存失效由 `InvalidateNewGRFCache` 系列负责。 |

## 三、方案（本轮实现）

### 3.1 放开闸门，改为「整组翻转」

`reverse_single_veh` 分支的语义从「翻转一节」改为「翻转该节所属的**整个铰接组**」：

1. **定位组头**：从被点的 `v` 沿 `Previous()` 回溯到组头（`v` 本身可能是组内成员）。
2. **仍拒绝双头**：`v->IsMultiheaded()` 保持早退（双头的前/后半不是 artic 组关系，单独翻转一半会破坏对称；玩家本轮只要求铰接）。
3. **真铰接先打散**（玩家要求）：若组头 `Next()->IsArticulatedPart()`，在 Execute 内对该组调用 `R3RDearticulateOneGroup()`（只打散此组，不动同链其它组），使其变成假铰接（角色位 + 烘焙值）。
4. **整组翻转 `Flipped`**：以组头当前 `Flipped` 取反为新值，组内每节统一 Set/Reset（保证组内一致，二次点击可复原）。
5. **镜像 0x4D**：新增 `VehicleRailFlag::ArticGroupFlipped = 29`，整组每节置位；`newgrf_engine.cpp` 的 `0x4D` 分支把「`IsSegmentFlipped()` 或 `IsArticGroupFlipped()`」都视为需要交换 `before/after`。这样 GRF 按组内位置选图时，翻转后每节仍绘制它翻转前那一面（与段逻辑翻转同一口径）。
6. 末尾照旧 `front->ConsistChanged(CCF_ARRANGE)` + 刷窗口。

### 3.2 为什么新增位而不是「寄宿在 Flipped 上」

- `Flipped` 是**逐节**的图像镜像位，原生单节翻转、段翻转、GRF 都可能设置它；用它推导 0x4D 是否交换，会与 `SegmentFlipped` 分支叠加（段翻转已交换一次，再按 `Flipped` 交换就错）。
- `ArticGroupFlipped` 是**组级语义**（组内每节一致），只由本命令设置，语义单一、零回归：不打此位的车在 0x4D 上逐字不变。
- 底层 `uint32_t`，位 29 空闲，存档布局不变（`Train::flags` 本来就是 32 位整体存）。

### 3.3 已知边界 / 待确认

- **U-1 打散不可逆**：真铰接被打散后，只有段降级（`RearticulateChain`）能恢复为真 artic；本轮不做「再点一次还原真铰接」。
- **U-2 只处理一个组**：`DearticulateChainWithSnapshot` 会打散整链，故改用 `R3RDearticulateOneGroup`（需去掉 `static` 并在 `train_cmd.cpp` 前置声明）。同链其它真组保持原样。
- **U-3 双头 + 铰接的机车**（如双头动车组）：仍被 `IsMultiheaded()` 拒绝，本轮不放开。
- **U-4 `ArticGroupFlipped` 与 `SegmentFlipped` 同时为真**（实现时已改为 XOR，见 §五）：两者都是**自反镜像**（各代表一次"链序未变但视觉翻"），所以判据必须是 `IsSegmentFlipped() != IsArticGroupFlipped()`，两次镜像即归位；`||` 会在「先手动翻组、后段逻辑翻转」时少镜像一次。该组合是否与 GRF 实图一致仍需实测。
- **U-5 重复翻转**：第二次 Ctrl+左键时组已是假铰接，只做 4/5 两步（不再打散），`Flipped` 取反、`ArticGroupFlipped` 同步取反 ⇒ 恢复原状。
- **U-6 订单/段/车号**：本操作不触碰 orders / segment / unitnumber，`ConsistChanged` 只重算几何缓存。
- **U-7 虚拟车（模板车编辑器）**：走 `front->IsVirtual()` 分支同样允许，需实测模板车窗口行为。

## 四、复测判据（游戏内）

1. 车库中 Ctrl+左键点击一节**真铰接**车厢（父车或任一 part）⇒ 整组 3 节图像同时镜像；日志出现 `ARTIC-FLIP` 探针（`head= n= real_artic=1 flip=0->1`）。
2. 再点一次 ⇒ 整组恢复原朝向，`flip=1->0`，不再打散第二次（`real_artic=0`）。
3. 用 `dump_vehicle <id>` 看：父车 `H=1`、成员 `M=1`、`st:` 不再含 `A`（真铰接已打散）；组内每节 `tf:` 均含 `F`（Flipped）与新增的组翻转字符。
4. 组内各节 GRF 图像（尤其按 0x4D 选图的 GRF）：翻转前后逐节"面"不变，只是整体左右镜像。
5. 负判据：非铰接车 Ctrl+左键行为逐字不变（`flip` 只改那一节，不产生 `ArticGroupFlipped`）。
6. 负判据：双头车仍被拒绝（提示 "multiple units"）。
7. 负判据：同链上的**其它**真铰接组不受影响（仍是真铰接，`st:` 含 `A`）。
8. 存档往返：`ArticGroupFlipped` 随 `Train::flags` 保存，读档后 0x4D 行为不变。

## 五、实现落地（2026-11-04，第 391 轮）

按 §三 方案改码，共 4 个 .cpp + 1 个 .h（`train.h` 只加枚举位与访问器，无布局变化）：

| 文件 | 改动 |
| --- | --- |
| `src/train.h` | `VehicleRailFlag::ArticGroupFlipped = 29`；新增 `IsArticGroupFlipped()/SetArticGroupFlipped()/ClearArticGroupFlipped()/FlipArticGroupFlipped()`（紧邻 `FlipSegmentFlipped()`） |
| `src/vehicle_cmd.cpp` | `R3RDearticulateOneGroup()` 去掉 `static`（加跨 TU 注释）；`RearticulateChain()` 里对每节补 `ClearArticGroupFlipped()` |
| `src/train_cmd.cpp` | 手写前置声明 `Train *R3RDearticulateOneGroup(Train *v);`；`CmdReverseTrainDirection()` 的 `reverse_single_veh` 分支重写 |
| `src/newgrf_engine.cpp` | 0x4D 交换判据改为 `HasDearticulatedGroupRole() && (IsSegmentFlipped() != IsArticGroupFlipped())` |

`reverse_single_veh` 分支最终形态：

1. 闸门从「`IsMultiheaded() || ArticEngine 回调`」改为只留 `IsMultiheaded()`（双头仍拒）。
2. `front->IsStoppedInDepot()` 守卫不变。
3. Execute 内：沿 `Previous()` 走到组头（`IsArticGroupMember()` 对真 artic 与假成员都成立）。
4. `is_group = group_head->Next() && group_head->Next()->IsArticGroupMember()`；`real_artic = group_head->Next()->IsArticulatedPart()`（只看 subtype 位）。
5. `!is_group` ⇒ 逐字保留原生 `v->flags.Flip(Flipped)`（普通单节车零改动、不打组标记）。
6. `is_group` ⇒ 真组先 `R3RDearticulateOneGroup(group_head)`（只打散**这一组**），再组内逐节 `flags.Flip(Flipped)` + `FlipArticGroupFlipped()`，直到下一节不是成员。
7. 探针 `ARTIC-FLIP head= n= real_artic= flipped= seg=`（`ARTIC` 前缀已在 `r3r_perf.h` 白名单内）。
8. 末尾照旧 `front->ConsistChanged(CCF_ARRANGE)` + 四个窗口刷新。

**U-2 已落实**：只打散被点的那一组，同链其它真组保持 `st:A` 不变。**U-4 已订正为 XOR**（两个自反镜像叠加即归位）。

### 5.1 实现时新增的观察点

- **V-1 组标记的存续范围**：`ArticGroupFlipped` 逐节随组走；若之后车库把该组拆到两条链（拖动合并/解挂），每半仍带标记 ⇒ 0x4D 仍镜像。语义上"这半组当初被翻过"仍成立，但 GRF 若按整组 rank 取图可能显示偏差，需实测。
- **V-2 只翻组不改链序**：这是与段翻转（`SegmentFlipped`）的分工 —— 组翻转不动链序，所以必须靠标记镜像；段翻转会倒置链序，标记只是把 0x4D 拉回翻前值。
- **V-3 真铰接打散的既有代价**：`R3RDearticulateOneGroup` 会把父车 weight 烘焙为 `w_q+w_r`、成员继承动力/速度 override（与升段/克隆路径同源），属于"打散"本身的行为，不是本轮新增。

### 5.2 构建自证（2026-11-04）

- 入口：复用既有 `R3R_fullbuild.cmd inc detach`（**未新建任何 .cmd**）；构建前 `tasklist` 确认 `openttd.exe` 未运行。
- 护栏 `build\R3R_incbuild.guard.log`：`src\train.h (2026-11-04 16:07:35)` 晚于最新 obj ⇒ `REMOVED 620 object file(s) - upgrading this build to a FULL rebuild`（KI-183 预期行为）。
- `build\R3R_incbuild.log`：`[692/692] Linking CXX executable openttd.exe`；`build\R3R_incbuild.done` = `EXIT_CODE=0`；`error C` / `fatal error` / `build stopped` 计数 **0**。
- 时间戳链：`src\train_cmd.cpp 16:08:37 → train_cmd.cpp.obj 16:33:43`、`src\vehicle_cmd.cpp 16:07:43 → vehicle_cmd.cpp.obj 16:34:03`、`src\newgrf_engine.cpp 16:08:56 → newgrf_engine.cpp.obj 16:28:35`；`build\openttd.exe 2026-11-04 16:36:30`（52 449 280 B）。
- `read_lints` 四个文件 0 条。
- exe 字面量自证：`ARTIC-FLIP head=%d n=%u real_artic=%d flipped=%d seg=%d` 命中 ✓；`ArticGroupFlipped` 是枚举名，不以文本出现在 exe（正常）。
- 发布树 `build-release` 本轮**未编**（按既定口径只在明确要求时编），停留在第 390 轮版本。
