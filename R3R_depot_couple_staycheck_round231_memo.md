# R3R 第 231 轮：库型 GOTO_COUPLE「到库后什么都不发生」根因做实与修复（KI-339）

- 日期：2026-10-10
- 轮次：第 231 轮
- 对应 KI：**KI-339**（第 230 轮登记为「只读探针取证」，本轮**已修**）
- 改动文件：`src\train_cmd.cpp`（单个 `.cpp`，未碰任何 `src\*.h` / `src\lang\*.txt`）
- 上游备忘：`R3R_depot_couple_scan_after_arrive_round230_memo.md`（第 230 轮探针设计）、`R3R_couple_depot_first_round229_memo.md`（第 229 轮 KI-338）

---

## 一、取证过程

### 1.1 工件

| 项 | 值 |
| --- | --- |
| 日志 | `build\R3R_debug.log`（2026-10-10 18:05:24 起写，产自含 `DEPOTBLK` 探针的 exe） |
| exe | `build\openttd.exe`（2026-10-10 18:00:09，51 737 088 B，= 第 230 轮探针版） |
| 探针 | `R3REDGE_DEPOTBLK`（第 230 轮新增，插在 `R3REnsureCouplePair` 之后、depot 块之前） |

第 230 轮登记的复测判据①要求「失败场景应出现 `DEPOTBLK veh=65 …`」。本轮日志**命中**，且读数把第 230 轮的候选成因 A/B/C/D 一次性排除完毕。

### 1.2 探针原文（关键锚点）

机车 `veh=66` 从 `actTile 981,134` 一路开向目标库 `964,133`（`CPL-PAIR act=66 tgt=61`，`dist` 18→0 单调递减，说明**寻路与预留全程健康、目标解析正确**）：

```
1507:DEPOTBLK veh=66 tile=979,134 track=01 eng=1 isdepot=1 ontile=0 idmatch=0 pend=0 stopped=0 parked=0 spd=160 destRaw=0 depotIdx=4294967295
1518:DEPOTBLK veh=66 tile=973,134 track=01 eng=1 isdepot=1 ontile=0 idmatch=0 pend=0 stopped=0 parked=0 spd=138 destRaw=0 depotIdx=4294967295
1530:DEPOTBLK veh=66 tile=968,134 track=01 eng=1 isdepot=1 ontile=0 idmatch=0 pend=0 stopped=0 parked=0 spd=102 destRaw=0 depotIdx=4294967295
1536:DEPOTBLK veh=66 tile=966,133 track=20 eng=1 isdepot=1 ontile=0 idmatch=0 pend=0 stopped=0 parked=0 spd=78  destRaw=0 depotIdx=4294967295
1538:DEPOTBLK veh=66 tile=965,133 track=01 eng=1 isdepot=1 ontile=0 idmatch=0 pend=0 stopped=0 parked=0 spd=72  destRaw=0 depotIdx=4294967295
1545:DEPOTBLK veh=66 tile=964,133 track=01 eng=1 isdepot=1 ontile=1 idmatch=1 pend=0 stopped=0 parked=0 spd=61  destRaw=0 depotIdx=0
1551:DEPOTBLK veh=66 tile=964,133 track=01 eng=1 isdepot=1 ontile=1 idmatch=1 pend=0 stopped=0 parked=0 spd=61  destRaw=0 depotIdx=0
1556:DEPOTBLK veh=66 tile=964,133 track=01 eng=1 isdepot=1 ontile=1 idmatch=1 pend=0 stopped=0 parked=0 spd=61  destRaw=0 depotIdx=0
```

同期库内等待方静止不动（`veh=22` 是 `order=17` = `OT_WAIT_COUPLE`，`veh=23` 是 `order=16` = `OT_GOTO_COUPLE`，两者 `tile=964,133` 原地）：

```
1503:SKIP-STOPPED veh=22 order=17 real=17 spd=0 tile=964,133 parked=0 front=1 nord=14 idx=0 tt=65535
1504:SKIP-STOPPED veh=23 order=16 real=16 spd=0 tile=964,133 parked=0 front=1 nord=7  idx=0 tt=65535
1547:SKIP-STOPPED veh=22 order=17 real=17 spd=0 tile=964,133 parked=0 front=1 nord=14 idx=0 tt=65535
1548:SKIP-STOPPED veh=23 order=16 real=16 spd=0 tile=964,133 parked=0 front=1 nord=7  idx=0 tt=65535
```

`CPL-PAIR` 在 1546 行起 `dist=0`（机车与目标同格），此后 1550/1555 反复打印同一读数。

### 1.3 读数判读

- **候选 A 排除**：`ontile` 在 1545 起 = **1**（车确实站在库格上）。
- **候选 B 排除**：`eng=1`（链头是引擎）。
- **候选 C 排除**：`isdepot=1` 且 `idmatch=1`（订单目的地**就是**本库，`destRaw=0` / `depotIdx=0` 逐字匹配）。
- **⇒ 第 230 轮候选 **D**（四项全 1 却仍无 `COUPLE-OK`）。**

而 D 的成因，读数的**唯一异常字段**给出了答案：

> `track=01`（`TRACK_BIT_X`），**不是 `80`（`TRACK_BIT_DEPOT`）**。

且 1545→1551→1556 三次读数**逐字相同**（含 `spd=61` 一字不变），说明列车在此状态下**完全冻结**（不是"还在滚动"、不是"日志截断"；第 230 轮 U-1 至此关闭）。

`spd` 历史（160→138→102→78→72→61→61→61）显示列车是在进库途中**停在这个位置**的，而不是正常减速停稳。

---

## 二、根因推断（三层，全部落在 `src\train_cmd.cpp`）

### 层 1：`tile` 先于 `track` 变成"库内"

`VehicleEnterTile_Rail()`（`src\rail_cmd.cpp`，进库分支）是**唯一**把 `v->track` 改写成 `TRACK_BIT_DEPOT` 的地方，而它要求车头像素坐标命中 `_fractcoords_enter[dir]`（库内停车坐标）。

而列车的 `v->tile` 在**车头刚越过格线**时就已经变成库格了。于是存在一个**数 tick 的窗口**：

```
tile == 库格  且  track == 01（还带着进来时那条普通轨道位）
```

### 层 2：`CheckTrainStayInDepot()` 在这个窗口里就早退 ⇒ 列车永远走不完最后几像素

`src\train_cmd.cpp:12506`（R3R 定制闸门）：

```cpp
if (v->current_order.IsType(OT_GOTO_COUPLE) && v->current_order.GetCoupleIsDepot() &&
		IsRailDepotTile(v->tile) &&
		v->current_order.GetDestination().ToDepotID() == GetDepotIndex(v->tile)) return true;
```

该闸门是 R3R 自己加的（防止 GOTO_COUPLE 机车到库后又被"出库逻辑"赶出去），但它的判据只看了 **`v->tile` 是不是目标库格**，没看**车是否真的已经进库**。于是它在上面的窗口里就命中并 `return true`。

`TrainLocoHandler()` 在 `:17826` 调用它，拿到 `true` 就**整段早退**——后面的运动块（`consist->UpdateSpeed()` → `TrainCheckIfLineEnds()` → `for(;;) TrainController(...)`，约 `:17995` 起）**一个字节都不执行**。

⇒ 列车被钉在库格边缘，**永远无法再往前挪那几像素**，`_fractcoords_enter` 永远命中不了，`track` 永远是 `01`。这正好解释读数的"冻结且 `track=01`"。

**旁证（说明只要不早退，引擎本来会正常进库）**：

- `TrainCanLeaveTile()`（`:16886`）对"正朝库内走的车"返回 `false`（引擎把它识别为**正在进库**，不是"能离开本格"）；
- `TrainCheckIfLineEnds()`（`:16950`）紧接着在 `:16961` `if (!TrainCanLeaveTile(moving_front)) return true;` 返回 `true` ⇒ **不会**被误判成 line-end 而刹车，也不会调 `TrainApproachingLineEnd()` 清速。

⇒ 即：**闸门只要不提前命中，列车就会自己走完进库流程。**

### 层 3：库内扫描块的门禁要 `track == TRACK_BIT_DEPOT`，于是永不生效

`TrainLocoHandler()` 里的 R3R 库内耦合块（`:17349`）：

```cpp
if (consist->track == TRACK_BIT_DEPOT && consist->IsEngine()) {
	...
	if (couple_targets_this_depot) {
		...
		const bool r3r_coupled = TrainCoupleHandler(consist);
		...
		consist->cur_speed = 0;
	}
}
```

它要求 `track == TRACK_BIT_DEPOT`。而被层 2 钉住的车 `track` 恒为 `01` ⇒ 块内 `couple_targets_this_depot` 为真的那条路**从未被执行**；`else if (!consist->current_order.IsType(OT_GOTO_COUPLE))`（`:17421`）也因为订单**就是** `OT_GOTO_COUPLE` 而不成立 ⇒ 整段无日志、无动作。

**这正是第 230 轮观察到的"静默分叉点"，本轮的贡献是指出它的**前提条件**（`track == TRACK_BIT_DEPOT`）**永远不满足**，而把前提变成永久不满足的元凶是层 2 的早退。**

### 因果链小结

```
到达目标库格（tile 已是库格，track 仍是 01）
  → CheckTrainStayInDepot:12506 命中 ⇒ TrainLocoHandler 早退
    → 运动块不执行 ⇒ 车前进不了几像素 ⇒ track 永不变成 TRACK_BIT_DEPOT
      → 库内耦合块(KI-339 静默分叉点)门禁 track==DEPOT 不成立 ⇒ 永不扫描 ⇒ 无日志、无动作
        → 列车永久冻结在库格上（spd 停在 61，读数三次逐字相同）
```

---

## 三、修复（第 231 轮）

### 3.1 新增判据：整列已真正进入库

在 `src\train_cmd.cpp` 的 `CheckTrainStayInDepot()` 之前新增文件内 static 辅助：

```cpp
static bool R3RChainFullyInDepot(const Train *v)
{
	for (const Train *u = v; u != nullptr; u = u->Next()) {
		if (u->track != TRACK_BIT_DEPOT) return false;
	}
	return true;
}
```

**只看 `track` 位、不看 `tile`** —— 这样多格（贯通式 `drive_through_train_depot`）库里跨格的车节也能被认作"在库内"（与 `:12506` 闸门原有的"跨格也算"口径保持一致，不收紧原有语义）。

### 3.2 闸门加条件（`CheckTrainStayInDepot`，`:12506`）

```cpp
	if (v->current_order.IsType(OT_GOTO_COUPLE) && v->current_order.GetCoupleIsDepot() &&
			IsRailDepotTile(v->tile) &&
			v->current_order.GetDestination().ToDepotID() == GetDepotIndex(v->tile) &&
			R3RChainFullyInDepot(v)) return true;
```

效果：

- **窗口期（`tile` 已是库格、整车还没进完）** ⇒ 闸门**不再命中** ⇒ `:12511` 的"还有车节不在库内 ⇒ `return false`"照常生效 ⇒ `TrainLocoHandler` 继续跑到运动块 ⇒ 列车把最后几像素走完 ⇒ `VehicleEnterTile_Rail` 把 `track` 置成 `TRACK_BIT_DEPOT`。
- **整车进库后** ⇒ 闸门恢复命中 ⇒ 停在库内（原有的防"出库"语义**逐字保留**）。
- 目的地 ≠ 当前库的情形**完全不受影响**（这一类本来就不该命中，第 229 轮 KI-338 的结论不变）。

### 3.3 库内扫描块门禁同步加条件（`:17349`）

```cpp
	if (consist->track == TRACK_BIT_DEPOT && consist->IsEngine() && R3RChainFullyInDepot(consist)) {
```

**为什么必须同步加**：进库是**逐节**完成的（车头先 `track=DEPOT`，车尾还在普通轨道上，持续数 tick）。若只改 3.2 而不改这里，那么在这个"车头已进库、车尾还在库外"的中间态，块内 `:17406` 的 `consist->cur_speed = 0;` 会**把列车停在库门口**——而运动块的推进量 `j = UpdateSpeed()` 在 `cur_speed == 0` 时落到"停止分支"（`j < adv_spd`），**车尾就再也进不来了**（尾车同样不再被推进）。加条件后，整段（含 `else if` 里的 `NormalizeTrainVehInDepot`）都只在整车进库后生效，进库途中交给引擎原生运动逻辑。

### 3.4 刻意不做的事

- **不动** `TrainCoupleHandler`、`couple_targets_this_depot`、`r3r_pending_depot_couple`、`R3REnsureCouplePair` 的任何逻辑（本轮只修"条件何时成立"，不修"成立后做什么"）。
- **不动** `GetDepotIndex(v->tile)` 的 `IsRailDepotTile` 守卫（该守卫是既有历史崩溃的防护，注释已注明）。
- **不加**新的日志/探针（第 230 轮的 `DEPOTBLK` 已经够用，且它会把 `track` 值直接打出来，正好是本轮的判据字段）。

---

## 四、结论

- **KI-339 根因定性（高置信）**：库型 `OT_GOTO_COUPLE` 到达目标库后"什么都不发生"，**不是**寻路失败、**不是**目的地解析错、**不是** `TrainCoupleHandler` 静默失败，而是：

  > R3R 自己的"到库停驻"闸门（`CheckTrainStayInDepot:12506`）把"**车格是库格**"当成了"**车已进库**"，在列车还差几像素没进库时就早退了 `TrainLocoHandler`，使 `track` 永远停在 `TRACK_BIT_X`（`01`）；于是需要 `TRACK_BIT_DEPOT` 的库内耦合块永不执行。

- 第 230 轮候选 D 成立，且其**上游成因**已定位；第 230 轮 U-1（"是日志截断还是真卡"）**结论 = 真卡**（三次逐字相同读数 + `spd` 恒定 61）。
- 修复是**闸门收紧**（把"tile 是库格"收紧为"整车 track 都是 DEPOT"），属最小改动，不动任何耦合语义。

---

## 五、未确认项

- **U-1**：车尾进库的那几 tick 里，`VehicleEnterDepot()`（`src\vehicle.cpp:2787` 会把 `cur_speed` 清 0）与库内块的交互是否需要额外防护 —— 按代码推断不需要（`VehicleEnterDepot` 只在**整车**最后一节进入时被调用，那时 `R3RChainFullyInDepot` 已成立），但**未经实机验证**。
- **U-2**：贯通式（多格）库里跨格车节是否真会以 `track == TRACK_BIT_DEPOT` 出现 —— 本修复按"是"处理（与 :12506 原口径一致）；若实机发现跨格库有别的表示，需回看 `drive_through_train_depot` 分支。
- **U-3**：`veh=23`（库里另一台 `order=16` 的 GOTO_COUPLE 车）为何长期停在库内不动 —— 本日志里它 `spd=0 / parked=0 / front=1`，可能与本次冻结同源（它也在同一个库格里），但也可能是**另一个**独立问题（例如它自己也在等别人来挂）。**本轮未动它**，留给复测时按 `DEPOTBLK veh=23` 读数判定。
- **U-4**：`act=66 tgt=61` 的配对是 `R3REnsureCouplePair` 锁的；本次日志里 `tgt=61` 的实体（车底链）在库内的具体车号/位置未逐行展开（日志显示 `CPL-PAIR dist=0` 即同格，符合预期）。

---

## 六、复测判据（下一轮取数用）

1. **正判据（核心）**：同样场景重跑，机车到达目标库后应出现 `track=80`（`TRACK_BIT_DEPOT`）的 `DEPOTBLK` 行，**不再**是 `track=01` 反复三次。
2. **正判据**：随后出现 `COUPLE-OK loco=66 …`（或至少 `COUPLE-FAIL`，即"有人负责报告"了）；`CPL-PAIR … dist=0` **不再无限重复**。
3. **正判据**：机车 `spd` 不再恒定在某个非零值（要么停稳为 0 且 `track=80`，要么开始运动/被耦合）。
4. **负判据**：目的地 ≠ 当前库的库型 GOTO_COUPLE（第 229 轮 KI-338 场景，`1229,194 → 964,133`）行为**逐字不变**：仍应 `TRP ok=1 res=1` 出库、横穿、到目标库后 `COUPLE-OK`。
5. **负判据**：站台型挂接不受影响（`COUPLE-OK` 照旧）。
6. **负判据**：车库工具登记路径（`r3r_pending_depot_couple`，探针打 `pend=1`）行为不变 —— 该路径的整列本来就在库里，`R3RChainFullyInDepot` 恒成立。
7. **负判据**：多格（贯通式）库场景若有，进库/出库/库内耦合均无回归。
8. **负判据**：进库途中不再出现任何 `TrackReservation`/`TRP` 相关的 `ok=0 res=0` 异常（进库本身不走预留，回归风险低，仅作观察项）。

---

## 七、构建自证（第 231 轮）

- 复用既有 `_tmp_inc_build.cmd`（**未新建任何 `.cmd`**）；仅改 `src\train_cmd.cpp` 一个 `.cpp`（未碰任何 `src\*.h` / `src\lang\*.txt`）。
- 护栏 `build\R3R_incbuild.guard.log` = 「GUARD: incremental is safe (no header/lang file is newer than the newest object)」（2026-10-10 18:30:57）。
- 日志 `build\R3R_incbuild.log`：`[1/5]` FindVersion（always-run）→ `[2/3] Building CXX object …src\train_cmd.cpp.obj` → `[3/3] Linking CXX executable openttd.exe`；`build\R3R_incbuild.done` = `EXIT_CODE=0`（18:32:45）；`error C*` / `fatal error` / `FAILED:` / `build stopped` 计数 = **0**。
- 时间戳三件套：`src\train_cmd.cpp` **2026-10-10 18:30:09** → `src\train_cmd.cpp.obj` **18:31:30** → `build\openttd.exe` **18:32:26（51 737 088 B）**。
- 产物自证：`train_cmd.cpp.obj` 内可检到新符号 `R3RChainFullyInDepot`（`static` 函数符号不落 exe 字符串表，故按 obj 校验）；`read_lints` 对 `src\train_cmd.cpp` = **0 条**。
- 文档：KI 条目 = `R3R_KNOWN_ISSUES.md` 的「第 231 轮」小节（`KI-339` 状态改为「已修（第 231 轮，已编译通过；游戏内复测待做）」）。
