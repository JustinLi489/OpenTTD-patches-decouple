# R3R 公路车「Disconnecting road vehicle」崩溃取证（第 387 轮，2026-11-03）

## 一、报告来源

玩家 2026-11-03 指示：KI-553/554 可结案的条件下，研究 `Docs/OpenTTD` 最新崩溃日志。

实际路径（`Docs\OpenTTD` 不存在，真实位置为用户文档目录）：

```
C:\Users\冯洁敏\Documents\OpenTTD\
```

最新崩溃：`crash-20261101T173030Z.log`（`2026-11-02 01:30:31`，48 164 B），
配套 `crash.dmp`（3 068 350 B）、`crash-20261101T173030Z.sav`（16 336 348 B）、
`crash-20261101T173030Z.png`（235 904 B）。

## 二、崩溃现场（原文）

```
Crash at: 2026-11-01 17:30:30 (UTC)
In game date: 2403-05-04 (49, 4) (DL: 9)
Game loaded at: 2403-04-29 (22, 4), (3573 state ticks ago), 2026-11-01 17:28:27

Crash reason:
 Exception:  E1212012
 Location:   7FFD5958016C
 Message:    Disconnecting road vehicle.

Within context:
    0: IndividualRoadVehicleController: veh: 39930 (Road Vehicle [N/A], c:0, st:A, vs:D,
       vcf:dR, rvs: 1, rvf: F, [tile: 78DB65 (7013 x 966), type: 20 (Road), ...],
       front: 39928 (Road Vehicle 16, st:F, vs:HD, vf:FTzsa, ..., [tile: 7CDB65 (7013 x 998), ...]))
       veh: 39929 (Road Vehicle [N/A], ..., rvs: FE, rvf: B, [tile: 7CDB65 (7013 x 998), ...],
       front: 39928 ...)
    1: CallVehicleTicks: veh: 39928 (Road Vehicle 16, ..., rvs: FE, [tile: 7CDB65 (7013 x 998), ...])
```

栈：

```
[03] IndividualRoadVehicleController + 5683 (src\roadveh_cmd.cpp:1758)
[04] RoadVehController + 456 (src\roadveh_cmd.cpp:2189)
[05] CallVehicleTicks + 1824 (src\vehicle.cpp:1736)
```

版本：`r3r-pre-ledger-removal-2026-10-25-m (2)`，Build date `Nov 1 2026 01:01:09`，
`R3R_PROBES_DEFAULT=0`（**发布树**，无探针，所以拿不到 R3R 调试日志）。
地图 `8192 x 4096`，多人公司 13 家（Human），`last_newgrf_count: 469`。

## 三、已确认事实（源码对照）

断言位置 `src\roadveh_cmd.cpp:1757-1760`：

```cpp
if (dir == INVALID_TRACKDIR) {
    if (!v->IsFrontEngine()) FatalError("Disconnecting road vehicle.");
    v->cur_speed = 0;
    return false;
}
```

调用链（同文件 `:1750-1755`）：

```cpp
} else if (no_advance_tile) {
    dir = (Trackdir) prev->state;
} else {
    dir = FollowPreviousRoadVehicle(v, prev, tile, diagdir, false);
}
```

`FollowPreviousRoadVehicle()`（`:1361-1417`）的两处 `INVALID_TRACKDIR`：

- `:1381` —— `prev->state` 为 `RVSB_WORMHOLE(0xFF)` / `RVSB_IN_DEPOT(0xFE)`，但**后车正要进入的那一格**
  既不是 `TileType::TunnelBridge` 也不是 `IsRoadDepotTile`；
- `:1415` —— `prev->state >= TRACKDIR_END` 且不属于任何已识别特殊值。

按现场 dump 反推，命中的是 **`:1381`**：

| 车辆 | 角色 | `rvs:` | `state` 含义（`src\roadveh.h:40-41`） | tile |
|---|---|---|---|---|
| 39928 | 链头（front engine, 16） | `FE` | `RVSB_IN_DEPOT` | 7013 × 998 |
| 39929 | 中间节 | `FE` | `RVSB_IN_DEPOT` | 7013 × 998 |
| 39930 | 后节 | `1` | 在途 trackdir 1 | 7013 × **966** |

⇒ 同一条链（`front: 39928`）上，前两节在**车库**（state=0xFE），
后节仍在**32 格之外的在途位置**（y 966 vs 998，差 32 格）。

## 四、猜测根因（待验证）

**G1（主推测）：车队被拆成两处，但链指针未断。**
`39930` 的 `state` 仍是普通 trackdir（在途），说明它从未被"送进车库"；
而它的 `Previous()`/`front` 链已指向在车库的 `39928`/`39929`。
于是 `IndividualRoadVehicleController` 走到"进下一格"分支时，
用 `prev`(=39929, state=0xFE 车库) 去推方向，但**自己前方那格不是车库** ⇒ `:1381` 返回 INVALID ⇒ 断言。

**G2（可能是 G1 的成因，也可能独立）：这是应急存档的二次崩溃。**
日志第 3 行自陈 `As you loaded an emergency savegame no crash information would ordinarily
be generated.`，且 `Game loaded at ... 17:28:27` 后仅 3573 state ticks 再次崩溃；
上一份崩溃 `crash-20261101T171039Z.log` 仅早 20 分钟。
⇒ 若两次断言相同，则是"崩溃 → 应急存档 → 读档后状态本就不一致 → 再崩"的循环，
**真正需要修的是第一次崩溃前的状态破坏点**，不是这条断言。

**G3（是否与 R3R 有关，未定）**：R3R 的改动集中在列车（`Train`），
本轮崩溃栈全在原生 `roadveh_cmd.cpp`。但 R3R 确实动过共享层
（`vehicle.cpp`、`vehicle_cmd.cpp`、车库 GUI 的拖动/拆解路径），
且该版本是发布树（无探针），**不能用"日志里没有 R3R 探针"反证无关**。
必须比对 `crash-20261101T171039Z.log` 与更早版本是否同形态，并确认崩溃前是否有车库操作。

## 五、待验证清单

| 编号 | 待验证项 | 判据 |
|---|---|---|
| U-1 | 17:10:39 那份崩溃是否同一断言同一车号 | 该 log 的 `Crash reason` + `Within context` 车号 |
| U-2 | 是否有更早的第一次崩溃（非应急存档） | 更早 log 的 `Game loaded at` 是否等于 `Crash at`（同一次会话内首次崩） |
| U-3 | 崩溃前是否发生过公路车车库操作（买/卖/拖动/克隆） | 无 R3R 日志（发布树）；只能靠 `.sav` 复现或玩家回忆 |
| U-4 | 该断言在原生 JGRPP 是否存在同样触发面 | 对照 `roadveh_cmd.cpp` 上游实现；若上游同样有该 `FatalError`，则属上游缺陷面，R3R 至多是触发者 |
| U-5 | 39930 与 39928 是否本来是同一条链（`front` 一致即为是） | 已看到 `front: 39928`，**已确认同链** |

## 六、状态

**仅取证与定性，未改任何源码、未构建。**
崩溃性质：**高**（`FatalError` 直接 abort，丢未存档进度）。

---

## 七、第二轮取证（2026-11-03）

### 7.1 断言归属：**上古原生代码，不是 R3R 加的**

```
git --no-pager -C "d:\sourcecode of JGRPP" log --oneline -S "Disconnecting road vehicle." -- src/roadveh_cmd.cpp
c186f91cbd (svn r10097) -Feature: Add support for articulated road vehicles ...
3e326085fa (svn r10097) ...
```

⇒ 该 `FatalError` 来自 2007 年 svn r10097「铰接公路车」引入的防御断言，与 R3R 无关。
它报的是**症状**（链断了），不是根因。

### 7.2 上一份崩溃（17:10:39）与最新一份**逐字相同**

| 项 | 17:10:39 | 17:30:30 |
|---|---|---|
| Message | Disconnecting road vehicle. | 同 |
| veh / prev | 39930 / 39929 | 同 |
| tile | 39930@`7013,966`；39928/39929@`7013,998` | 同 |
| `rvs:` | 39930=`1`；39929/39928=`FE`(IN_DEPOT) | 同 |
| Game loaded at | 2403-04-29，**3573 state ticks ago** | 同 |

⇒ **确定性复现**：同一份应急存档载入后固定 3573 ticks 必崩。
损坏状态已固化在 `.sav` 里，不是随机抖动。

### 7.3 「第一次崩溃」没有日志

两份日志第 3 行都是
`As you loaded an emergency savegame no crash information would ordinarily be generated.`
⇒ 二者都是**加载应急存档之后**再崩的。真正的首次崩溃（生成那份应急存档的那一次）
没有 crash log，因此**无日志可查**，U-2 结论＝缺失，只能靠 `.sav` 或玩家回忆。

### 7.4 排除 10-27 的两类崩溃

| 日志 | 消息 | 主体 | 判定 |
|---|---|---|---|
| `crash-20261027T100208Z` / `T103907Z` | `follow_track.hpp:140` 断言 | **Train** 57(HXN5B)/59，tile Station `16,14`，游戏日期 1920 | 列车问题，与本次无关 |
| `crash-20261027T105727Z` | `train_cmd.cpp:20024` `chosen_track & (bits\|GetReservedTrackbits)` | Train | 列车 PBS 预留，与本次无关 |
| `crash-20261029T163552Z` / `T171136Z` / `20261028T170606Z` | `Message: <none>` | 未取 | 另案 |
| **`crash-20261101T171039Z` / `T173030Z`** | **Disconnecting road vehicle.** | **RoadVehicle 39928-39930** | **本条** |

### 7.5 机制重述（已由源码与 dump 双向确认）

1. 链：`39928`(front engine, `IsFrontEngine`=true) → `39929` → `39930`(articulated part)。
2. `39928`/`39929` 的 `state == 0xFE == RVSB_IN_DEPOT`，且 tile 在车库 `(7013,998)`。
3. `39930` 的 `state == 1`（普通 trackdir，**在途**），tile 在 `(7013,966)`，相距 **32 格**。
4. `39930` 走到「进下一格」分支 → 非 front engine → `FollowPreviousRoadVehicle()`
   → `prev->state` 命中 `RVSB_IN_DEPOT` 分支 → 检查**后车要进入的那格**是不是
   `TileType::TunnelBridge` / `IsRoadDepotTile` → 两者都不是 → `:1381` 返回 `INVALID_TRACKDIR`
   → `:1758` `FatalError("Disconnecting road vehicle.")`。

⇒ **充要条件**：同一条公路车链上，前节已在车库、后节仍在 32 格外的普通道路上。
原生代码假定「prev 在车库 ⇒ 我也在车库/隧道桥」，该假定被破坏即触发。

## 八、仍缺的关键信息（需要玩家或复现）

| 编号 | 缺什么 | 为什么卡住 | 获取方式 |
|---|---|---|---|
| U-3 | 崩溃前是否对这批公路车做过车库操作（买/卖/拖动/克隆/改 NewGRF） | 发布树无 R3R 探针，日志零线索 | 玩家回忆；或看崩溃前 1 分钟内操作 |
| U-6 | 39930 为何会留在车库外 32 格 | 需要逐 tick 追踪该车 `state` 变化 | 内测版加载 `.sav` + 公路车探针（需改码构建） |
| U-7 | 与 R3R 是否有因果关系 | R3R 不改 `roadveh_*`，但改过 `vehicle.cpp`/车库 GUI 等共享层 | 只有在 U-6 拿到断链点后才能定论 |

### 8.1 建议的下一步（需玩家决定）

**A（低成本，推荐先做）**：玩家用**内测版** `build\openttd.exe`（探针 ON）加载
`C:\Users\冯洁敏\Documents\OpenTTD\crash-20261101T171039Z.sav`，
跑约 1 分钟复现（3573 ticks 必崩），然后把 `build\R3R_debug.log` 尾部给我。
内测版虽无公路车专用探针，但能确认「内测版是否同样崩」以及崩溃前是否有 R3R 列车活动。

**B（需要改码）**：在 `IndividualRoadVehicleController` 的 `:1757` 前加只读探针，
记录 `v->index / prev->index / prev->state / v->tile / prev->tile / tile / front->index / v->IsFrontEngine()`，
每车每状态只打一次（边沿触发），然后复现取数，定位链在哪一 tick 被撕开。

**C（治症状，不推荐单独使用）**：把 `:1758` 的 `FatalError` 降级为「记录 + 清速度 + 把该节
从链上摘除或复位到车库」，避免玩家丢进度。但这只是把 abort 换成静默数据破坏，
**必须在 A/B 定论后作为兜底，不能代替根因修复**。

## 九、玩家 2026-11-03 的三条决定（原话要点，作为后续口径）

| 编号 | 玩家原话要点 | 对本轮方向的影响 |
|---|---|---|
| D-387-1 | 「我想先肉眼看看 39928 是什么一个车」 | 先解决「这是辆什么车」，用游戏内手段，不靠探针 |
| D-387-2 | 「这个问题似乎不能搁置，因为我使用原生 OpenTTD 读取这个存档就没有这个问题」 | **不许搁置**；且「原生不崩」= 最重要的一条对照证据，必须据此缩小范围 |
| D-387-3 | 「至于用内测版跑日志，我不推荐，因为内测版的探针会大量影响游戏速度，导致要等很久才可能等到崩溃」 | **方案 A 作废**（原 §8.1 A 已失效）；不准再要求跑内测探针 |

⇒ 三条一起把方向钉死：**必须用「零探针、低成本、秒级」的手段取证**，不能再走「跑一分钟等崩」。

## 十、39928 是什么车（日志字段解码，已定论）

日志格式由 `src\scope_info.cpp:60-76` 决定：`类型 + 单位号（0 则 [N/A]）+ (车名)`。

| 车号 | 日志原文摘要 | 解码 |
|---|---|---|
| 39928 | `Road Vehicle 16` / `st:F` / `vs:HD` / `vcf:lzR` / `rvf:B` / `rvs:FE` / `[tile: 7CDB65 (7013 x 998)]` / `[type: 20 (Road)]` / `data: 00 0001 00 12 83 00 00 0FC0` | **front engine，单位号 16**，无自定义车名；Hidden+Stopped；`rvs=0xFE`=`RVSB_IN_DEPOT`；停在车库格 |
| 39929 | `Road Vehicle [N/A]` / `st:A` / `vs:HD` / `rvf:B` / `rvs:FE` / 同 tile 7013,998 / 同 data 0x83 | 铰接部件（单位号 0）；同样在库、Hidden |
| 39930 | `Road Vehicle [N/A]` / `st:A` / `vs:D` / `rvf:F` / `rvs:1` / `[tile: 78DB65 (7013 x 966)]` / `data: 00 0000 00 12 81 00 00 0FC0` | 铰接部件；`rvs=1` 正常 trackdir；**Stopped 但未 Hidden**；在 32 格外普通道路 |

结论：**39928 = 一辆 3 节铰接公路车（front + 2 parts），公司 0，单位号 16**。
- 39928/39929 两节的 tile 与 `data:` 逐字相同（`0x7CDB65`）⇒ 两节叠在同一个车库格内，正常。
- 39930 的 tile 是 `0x78DB65`，x 相同、y 差 32；`data:` 的 m2 由 `0001` 变 `0000`（车库标志消失）、m5 由 `83` 变 `81` ⇒ 它在**普通道路**格上。
- 39930 只有 `vs:D`（Stopped）没有 `H`（Hidden），与「库内车一律 Hidden」对照 ⇒ 它确实在库外。

## 十一、机制做实：入库是「逐节」的，链尾才触发真正的入库

`src\road_cmd.cpp` 的 `VehicleEnterTile_Road()` 的 `RoadTileType::Depot` 分支（:2994-3013）：

```
if (rv->frame == RVC_DEPOT_STOP_FRAME &&
        _roadveh_enter_depot_dir[GetRoadDepotDirection(tile)] == rv->state) {
    rv->InvalidateImageCache();
    rv->state = RVSB_IN_DEPOT;      // ← 逐节各自置
    rv->vehstatus.Set(VehState::Hidden);
    rv->direction = ReverseDir(rv->direction);
    if (rv->Next() == nullptr) VehicleEnterDepot(rv->First());   // ← 只有链尾到齐才结算
    rv->tile = tile;                // ← 逐节各自搬自己的 tile
    ...
}
```

三条推论：
1. 该回调是 **每节各走一次**（铰接部件也各自经过车库入口格），每节独立置 `RVSB_IN_DEPOT` 并独立改写自己的 `tile`。
2. **只有链尾**（`Next()==nullptr`）进入时才调 `VehicleEnterDepot(first)` 做「整车入库」结算。
3. ⇒ 只要链条在「入库进行到一半」时被中断（暂停/存档/订单变更/前车先到），存档里就会永久固化出
   **「前面若干节 `rvs=FE` + 后面若干节仍在路上 `rvs=1`」** 的畸形链。这与日志现场（39928/39929=FE、39930=1）**逐字吻合**。

崩溃的必然性：39930 仍在链上（`front` 仍是 39928），它下一次移动进 `IndividualRoadVehicleController`
→ `FollowPreviousRoadVehicle(39930, prev=39929)`，prev 在库、39930 要进的格不是车库也不是隧道桥
⇒ `:1381` 返回 `INVALID_TRACKDIR` ⇒ `:1758` `FatalError("Disconnecting road vehicle.")`。

**因此本条崩溃的充要条件可以精确表述为：一条多节公路车链出现「前节 IN_DEPOT、后节仍在路上」的混合态，
且后节尝试移动。** 与 `roadveh_cmd.cpp` 内部逻辑无关 ⇒ 这也解释了为什么改 `roadveh_*` 不是必须的。

## 十二、R3R 改动的静态排查结果（本轮已做，缩小范围）

| 文件 | 结论 |
|---|---|
| `src\roadveh_cmd.cpp` | `git status`/`git diff` **零改动**；`RVSB_IN_DEPOT` 仅 4 处（建车、超车检查×2、`RoadVehLeaveDepot` 检查），**无进库赋值点** |
| `src\road_cmd.cpp` | 零改动（逐节入库的原生代码原样） |
| `src\roadveh.h` | 零改动 |
| `src\articulated_vehicles.cpp` | 零改动 |
| `src\vehicle_cmd.cpp` 的 `CmdCloneVehicle()` | R3R 克隆补丁**全部限定 `VehicleType::Train`**（:2448 `v->type==Train && w->type==Train`、:2494 `while (v->type==Train ...)`、:2522 `w_front->type==Train`）⇒ **对公路车零影响，嫌疑排除** |

⇒ R3R 不碰公路车控制器。剩下两个可能面：**(甲)** 读档/存档路径（`src\sl\*.cpp`，R3R 有 R3VP 稀疏块与
`R3RPurgeInvalidLoadingVehicles`）；**(乙)** 存档本身在这辆车入库中途被写下（与 R3R 无关，纯时序），
而「原生不崩」另有原因（见 §十三 REG-387-A）。

## 十三、疑点登记（按用户规则「有疑点/猜测根因立刻落盘」）

### REG-387-A（关键疑点，未解）：既然 `roadveh_cmd.cpp`/`road_cmd.cpp` 零改动，为什么原生不崩？

- 期望值：若存档里就是「前节 FE + 后节 1」的混合态，且后节会移动，**原生与 R3R 应当同样崩**。
- 实测值：玩家称原生 OpenTTD 读同一存档**无此问题**。
- 为什么存疑：两者公路车代码一致 ⇒ 差异只能来自（a）读档后车辆状态不同，（b）后节在原生里不移动，
  （c）原生其实没真正加载成功/没跑满 3573 ticks，（d）「原生」指的是哪个可执行文件尚未确认
  （官方 trunk 无法读 JGRPP 存档，需先排除这种「假对照」）。
- 判别所需数据：**同一 `.sav`、同一 index 39928/39929/39930，两版各自 `dump_vehicle` 的 `rvs`/`tile`/`vs`**。
  若 tick 0 就不同 ⇒ 读档路径（甲）；若相同 ⇒ 差异在运行时（乙/或原生没跑起来）。

### G-387-4（猜测根因，待验证）：入库中途被打断 ⇒ 固化混合态

- 支撑：§十一 的逐节入库代码 + 日志现场字段逐字吻合。
- 待验证：需要看到「39930 的 tile 就是购买/建车时的出厂 tile」或「崩溃前有过暂停/存档/订单变更」。
- 若成立，则修法不在 `roadveh_*`，而在**入库的原子性**（要么整链一起置 FE，要么都不置）。

## 十四、零探针取证清单（替代已作废的方案 A，供玩家执行）

全部只用**发布版**（探针 OFF，性能正常），且**先暂停游戏**，不跑 3573 ticks，秒级完成：

1. 加载 `crash-20261101T171039Z.sav`，**立刻暂停**（避免它自己崩掉）。
2. `~` 打开控制台，依次输入：
   - `dump_vehicle 39928`
   - `dump_vehicle 39929`
   - `dump_vehicle 39930`
   把三段的 `rvs:` / `tile:` / `vs:` / `front:` 抄回（或截图）。
3. 肉眼定位：`scrollto 0x7CDB65`（= 7013,998 车库）与 `scrollto 0x78DB65`（= 7013,966 公路上那节）。
   这样能直接看到「车库里的车」和「32 格外孤零零的一节」。
4. 对照实验：用**原生版**打开同一 `.sav`，同样暂停后 dump 这三个 id，比对 `rvs`/`tile`。
   —— 这一步同时回答 REG-387-A；若原生根本打不开该档，则「原生不崩」不成立，需重新定义对照。
5. 若控制台输出无法复制，可用 `screenshot` 存 PNG（默认 `Documents\OpenTTD\screenshot\`），
   告知文件名，我用 OCR 读。

> 说明：`dump_vehicle` 是 debug console 命令（`src\console_cmds.cpp` 注册），发布版同样可用，**不需要探针、不需要重编**。

## 十五、2026-11-03 关键新证据：同一存档两版读出来的**图像**不同

玩家原话要点：
> 「39928 在使用 JGRPP 0.73.3 读取存档的时候是正常的图像，使用 R3R 最新的发行版读取的时候就显示了空图像。
> 然后，我两个版本各自 dump 了一次然后截图，在 C 盘默认截图目录，早一些的是 r3r，晚一些的是原生 JGRPP。」

| 项 | 内容 |
|---|---|
| 存档 | 同一份（即 `crash-20261101T171039Z.sav` 一族） |
| A 版 | **R3R 最新发行版** ⇒ 39928 **空图像**（无车体贴图） |
| B 版 | **JGRPP 0.73.3 原版** ⇒ 39928 **图像正常** |
| 截图位置 | `C:\Users\冯洁敏\Documents\OpenTTD\screenshot\`，**早 = R3R，晚 = 原生 JGRPP** |

**这条证据的意义（REG-387-A 定档）**：上一轮 REG-387-A 只能推断「两版状态必有差异」，现在有了直接观测——
**同一个 `VehicleID`=39928，两版读档后连渲染图像都不同**。渲染图像来自
`engine_type` + `spritenum` + `subtype` + `direction` + 图像缓存，⇒ 差异至少落在这几个字段之一。

### G-387-5（新猜测根因，待验证）：R3R 读档路径把 39928 的车辆身份字段读坏

- 表现：图像空 ⇒ 最可能是 `engine_type` 被读成**无效/越界 engine**（`GetEngineData` 拿不到，
  绘制回退为空），或 `spritenum`/`subtype` 与实际不符导致图组取不到。
- 为什么「空」而不是「画成别的车」：取不到 engine 的车辆在 `Vehicle::GetImage()` 一族里通常直接
  返回空 sprite，这正是空图像的特征。
- 与崩溃的因果链（假设成立时）：身份字段错 ⇒ 该车被当成另一种/不合法车型 ⇒ 入库流程里
  `frame`/`state` 判定与其它节不一致 ⇒ 前节入 `RVSB_IN_DEPOT`、后节留在路上 ⇒ `:1758` FatalError。
  **这条链能同时解释「空图像」与「链被撕开」两个现象**，比"入库中途被打断"更能解释「原生完全不崩」。
- 落点候选（R3R 相对上游改动过的读档路径）：
  1. `src\sl\couple_group_sl.cpp` 的 **R3VP 稀疏块**（`CH_SPARSE_TABLE`）——若字段顺序/长度/索引
     映射与写出侧不一致，会把 A 车的停放状态写进 B 车，甚至越界改到相邻字段；
  2. `src\sl\vehicle_sl.cpp` 的 `AfterLoadVehiclesPhase2()`（R3R 在其主循环前插入
     `OrderList::Initialize(v)`）；
  3. `src\sl\station_sl.cpp` 的 `R3RPurgeInvalidLoadingVehicles*()`（只动 `loading_vehicles`，嫌疑低）；
  4. `src\vehicle.cpp` 的 `DumpVehicleFlagsGeneric`（只读，无嫌疑）。

### 待取数据（两图 dump 逐字段对比）

必须逐字比对以下字段，任一不同即为根因落点：
`Road Vehicle <n>`（单位号）/ `st:`（subtype）/ `vs:`（vehstatus）/ `vf:` / `vcf:` / `gvf:` /
`rvf:` / `rvs:` / `tile:` / `type:` / `height:` / `data:`。

判读口径：
- **`data:` / `height:` 不同** ⇒ 地图 tile 本身不同（世界数据差异，不是车辆字段）；
- **`rvs:` / `vs:` 不同** ⇒ 车辆状态不同（读档期被改写）；
- **`st:` / `vf:` / `rvf:` 不同** ⇒ 车型/子类型/标志不同 ⇒ 直接对上"空图像"；
- **`Road Vehicle <n>` 的单位号不同** ⇒ 连 `unitnumber` 都变了，问题在整车字段而非渲染。

## 十六、2026-11-03 两版截图 OCR 比对结果（本轮取证）

### 16.1 取数方式与结果

用工作区只读工具 `R3R_shot_ocr.ps1 -Count 2`（Windows.Media.Ocr, zh-Hans-CN）读两张截图，
原始输出落盘为 `R3R_shot_ocr_out.txt`（本环境无视觉能力，只能 OCR，小字号数字误识率高）。

| 图 | 时间 | 归属 |
|---|---|---|
| `屏幕截图 2026-11-03 015158.png` | 01:51:59 | **早 = R3R 发行版** |
| `屏幕截图 2026-11-03 015323.png` | 01:53:23 | **晚 = 原生 JGRPP 0.73.3** |

两图 dump **逐字近乎相同**（OCR 原文见 `R3R_shot_ocr_out.txt`）：

```
39928: Road Vehicle 16 , c:0, st:F,  vs:D,   ..., rvs:1,  rvf:C,  [tile:78DB65 (7013 x 96?), type 20 (Road), height 19, data: 00 0000 00 12 81 00 00 0FC0]
39929: Road Vehicle [N/A], c:0, st:A, vs:D,   ..., rvs:1,  rvf:A,  [同上 tile / 同上 data], front 39928
39930: Road Vehicle [N/A], c:0, st:A, vs:H D, ..., rvs:FE, rvf:B,  [同上 tile / 同上 data], front 39928
```

要点：
- `st:F` = `GVSF_FRONT`；`st:A` = `GVSF_ARTICULATED_PART`（`src/vehicle.cpp:4948-4949`）⇒ 39928 是前车、39929/39930 是铰接部件（三节链，与崩溃日志一致）。
- `vs:D` = 仅 `DefaultPalette`、**没有** `H`(Hidden)；`vs:H D` = Hidden + DefaultPalette（`src/vehicle.cpp:4957-4960`）。
- `rvf:` 出现两次：前一个是**公路车标志字母**（`src/vehicle.cpp:5041-5042`，只有 `L`=RVF_ON_LEVEL_CROSSING），后一个是 **`frame` 十六进制**（`src/vehicle.cpp:5071` 的 `rvs: {:X}, rvf: {:X}` 打的是 `r->state, r->frame`）⇒ `rvf:C/A/B` 是 frame 12/10/11，不是标志。
- `type/height/data:` 来自 `DumpTileFields`（`src/map.cpp:549-552`），`data` = `m1 m2 m3 m4 m5 _m6 _m7 _m8`。

### 16.2 结论句（本条为读数，非推断）

1. **两版 dump 相同** ⇒ 玩家观察到的"R3R 空图像 / 原生正常"**不可能**由 dump 里出现的任何字段解释。
2. **但 dump 根本不含决定图像的字段**：`dump_vehicle` 的输出只有
   `veh: <id>: (<类型> <单位号>, c:<owner>, <标志串>, [<tile 信息>])`（`src/scope_info.cpp:52-107`）
   + 标志串（`src/vehicle.cpp:5046-5081`）+ tile 字段（`src/map.cpp:528-552`）。
   **`engine_type`、`spritenum`、`x_pos/y_pos`、`direction`、`grf_cache`（NewGRF 变量缓存）一个都没有。**
   ⇒ "dump 相同" **不能**用来排除"图像决策输入不同"，G-387-5 不能被这条证据证伪。
3. 现在的状态与崩溃日志**恰好互补**：崩溃时 39928/39929 = `FE`+Hidden、39930 = `1` 在路上；
   现在 39928/39929 = `1` 在路上（未 Hidden）、39930 = `FE`+Hidden。
   ⇒ 同一列车被**两次**在入库/出库中途固化，残骸形状还不同 ⇒ 支持"入库提交点存在通用缺陷"而非一次性时序巧合。

### 16.3 新发现（代码级，已逐行核对）：PositionHelper 对**非列车**车型的回溯被 R3R 弄丢

`src/newgrf_engine.cpp:461-472`：

```cpp
const Vehicle *seg_front = v;
if (whole_chain) {
        seg_front = v->First();                       // 上游读法
} else if (v->type == VehicleType::Train) {           // ← 只有列车分支
        const Vehicle *w = v;
        while (w->Previous() != nullptr && !static_cast<const Train *>(w)->IsSegmentFront()) w = w->Previous();
        seg_front = w;
}
```

`else if` 没有对应的 `else` ⇒ **非列车（公路车 / 船 / 飞机）时 `seg_front` 停留在 `v` 本身**
⇒ 第 474 行 `for (u = seg_front; u != v; u = u->Next())` 一次都不进
⇒ **`chain_before` 恒为 0**。上游语义是从 `v` 沿 `Previous()` 一直走到物理链头。

返回值（`:549`）：`chain_before | chain_after << 8 | (chain_before + chain_after + consecutive) << 16`

⇒ 对任何铰接公路车，NewGRF 变量 **0x40（position/length in consist）** 与 **0x41（同型号连续长度）**
对第 2 节起的每一节都报 `position = 0`、`length` 少算（函数注释 `:457-460` 自己写着
"otherwise the segment front is the physical head"，说明本意是 `v->First()`，属漏写分支的笔误）。

**为何能解释"只有 R3R 空图像、dump 却一样"**：这三节链正是铰接链，若路车 GRF 用 0x40/0x41 选图，
R3R 下三节全部拿到 `position=0` 的候选 sprite（或越界取到无效 sprite 组 → 画不出来 = 空图像），
原生同一存档走正确回溯 → 每节各自的图。这两个输入**都不在 dump 里**，与玩家观察完全自洽。

- 登记 **G-387-6（猜测根因，待验证）**：空图像 = `PositionHelper` 非列车回溯缺失。
- 支撑证据：①代码层面已证 `chain_before` 对非列车恒 0；②现象只在 R3R 出现、原生正常；③dump 无该字段，无法反证。
- 待验证判据：修复后同存档在 R3R 下 39928/39929/39930 图像恢复；且非 NewGRF（原版）铰接路车图像前后逐字不变（上游语义恢复）。
- 修法：补 `else { seg_front = v->First(); }`（等价于把三支写成
  `seg_front = whole_chain ? v->First() : (v->type == VehicleType::Train ? <段头> : v->First())`）。
  列车路径一行不变，风险极低。

**已落地（2026-11-03 02:01）**：`src\newgrf_engine.cpp` 在 `else if (v->type == VehicleType::Train)`
之后补 `else { seg_front = v->First(); }`（含 G-387-6 注释）。列车与 `whole_chain` 两条路径逐字未动。

构建自证（复用既有 `R3R_fullbuild.cmd inc detach`，未新建 .cmd）：
- `build\R3R_incbuild.guard.log` = `GUARD: incremental is safe (no header/lang file is newer than the newest object)`
- `build\R3R_incbuild.log`：`[2/3] Building CXX object ...newgrf_engine.cpp.obj` → `[3/3] Linking CXX executable openttd.exe`
  → `==== R3R build ended 2026/11/03 周二 2:01:55.83 MODE=inc EXIT_CODE=0`
- `build\R3R_incbuild.done` = `EXIT_CODE=0`
- 时间戳链：`src\newgrf_engine.cpp` 01:59 → `newgrf_engine.cpp.obj` 02:00:27 → `build\openttd.exe` 02:01（52 419 584 B）
- `read_lints` 该文件 0 条

### 16.4 新疑点 REG-387-B：state 说在库、地图说在普通道路

39930 `rvs:FE`（`RVSB_IN_DEPOT`）却与两节同 `tile 0x78DB65`，而该 tile 的 `data` = `00 0000 00 12 81 ...`
⇒ `type 20 (Road)`、`m2 = 0000`（无 roadstop）、`m5 = 0x81` ⇒ **是普通道路格，不是车库格**。
⇒ `rv->state` 与 `rv->tile` 互相矛盾（入车库语义要求 tile 是 depot 格）。
存疑原因：OCR 可能把 `rvs:` 读错（但两图读出同一值，双次同错概率低）；也可能是真矛盾。
待取数：让玩家确认 39930 的 `rvs:` 读数是 `FE` 还是 `1`，以及三节是否真的同 `tile`。

### 16.5 仍待玩家提供的零探针证据（不需要内测版，全部在发行版里可用）

1. R3R 里 39928 的**车辆详情窗**（型号名 / 速度 / 载重）是否正常 —— 型号名错 ⇒ `engine_type` 被读坏。
2. 两图 `vf:` / `vcf:` / `gvf:` 三段的**完整字母**（OCR 全糊了，尤其 R3R 那张 `vcf:` 里有没有 `d` = `VCF_IS_DRAWN`）。
3. "空图像"到底是**整节完全不见**，还是画成透明/异形/位置错。
4. 这套公路车用的 **GRF 名称**（决定它是否真的读 0x40/0x41）。

### 16.6 玩家用豆包复核识图：两图**逐字完全一致**（本条为定论，OCR 疑点消除）

玩家原话：「我找了豆包帮你识图，她说两张图片中文本完全一致」。逐字文本（已落盘，权威读数）：

```
] dump_vehicle 39928
veh: 39928: (Road Vehicle 16, c:0, st:F, vs:D, vf:FTzsa, vcf:ld, gvf:, rvf:, rvs: 1, rvf: C, [tile: 78DB65 (7013 x 966), type: 20 (Road), height: 19, data: 00 0000 00 12 81 00 00 0FC0])
] dump_vehicle 39929
veh: 39929: (Road Vehicle [N/A], c:0, st:A, vs:D, vf:, vcf:d, gvf:, rvf:, rvs: 1, rvf: A, [同上 tile/data], front: 39928: (...))
] dump_vehicle 39930
veh: 39930: (Road Vehicle [N/A], c:0, st:A, vs:HD, vf:, vcf:, gvf:, rvf:, rvs: FE, rvf: B, [同上 tile/data], front: 39928: (...))
```

字段解码表（逐条对码，`src\vehicle.cpp:4948-4993`）：

| 字段 | 39928 | 39929 | 39930 | 含义（源码字母表） |
|---|---|---|---|---|
| `st:` | F | A | A | `GVSF_FRONT` / `GVSF_ARTICULATED_PART` ⇒ 三节链（前车+2 部件） |
| `vs:` | D | D | **HD** | D=`DefaultPalette`；H=`Hidden` ⇒ 只有尾节被隐藏 |
| `vf:` | FTzsa | 空 | 空 | F=`LoadingFinished`、T=`TimetableStarted`、z=`SeparationActive`、s=`TimetableSeparation`、a=`AutomateTimetable`（`:4966-4981`） |
| `vcf:` | **ld** | d | 空 | l=`VCF_LAST_VISUAL_EFFECT`、d=`VCF_IS_DRAWN`（`:4986-4988`） |
| `gvf:` | 空 | 空 | 空 | 无 GVF_GOINGUP/DOWN/CHUNNEL/SUPPRESS_IMPLICIT_ORDERS |
| `rvf:`(前) | 空 | 空 | 空 | 公路车标志无 `RVF_ON_LEVEL_CROSSING` |
| `rvs:` | 1 | 1 | **FE** | 正常 trackdir / `RVSB_IN_DEPOT` |
| `rvf:`(后) | C | A | B | **`frame` = 12 / 10 / 11**（`vehicle.cpp:5071` 打的是 `state, frame`） |

**读数结论**：
1. 两版 dump **逐字相同** ⇒ 图像差异**不可能**来自任何被 dump 的字段。§16.2 第 2 条（dump 不含图像输入）由"猜想"升级为**唯一出路**。
2. 39928 的 `vcf:` 含 `d`=`VCF_IS_DRAWN` ⇒ **R3R 确实把它画了**（不是"没进入绘制"），画出来的是空白 ⇒ 只能是"取到有效但空白的图"或"画到看不见的位置"。
3. `REG-387-B 确认成立`（不再是 OCR 疑点）：39930 `rvs:FE`（在库）却与另两节同 tile `0x78DB65`，该格 `type 20` 且 `m5=0x81` 的 **bits4-5 = 0 = `ROAD_TILE_NORMAL`**（road depot 是 2）⇒ 普通道路格上停着一个 self.claim 在库的车节 ⇒ state 与 tile 真实矛盾。同时 39928/39929 `rvs:1`、无一 Hidden ⇒ **「前两节在路上、尾节在库」与崩溃日志「前两节在库、尾节在路上」正好镜像**。

### 16.7 代码实证：空图像只可能来自"NewGRF 返回有效空白 sprite"，而该路径唯一被 R3R 改过的是 PositionHelper

`RoadVehicle::GetImage` 定义在 **`src\roadveh_cmd.cpp:126-144`**（该文件 R3R 从未改过，多轮 `git status`/`git diff` 零输出）：

```126:144:src/roadveh_cmd.cpp
void RoadVehicle::GetImage(Direction direction, EngineImageType image_type, VehicleSpriteSeq *result) const
{
	uint8_t spritenum = this->spritenum;
	if (IsCustomVehicleSpriteNum(spritenum)) {
		if (spritenum == CUSTOM_VEHICLE_SPRITENUM_REVERSED) direction = ReverseDir(direction);
		GetCustomVehicleSprite(this, direction, image_type, result);
		if (result->IsValid()) return;
		spritenum = this->GetEngine()->original_image_index;
	}
	assert(IsValidImageIndex<VehicleType::Road>(spritenum));
	SpriteID sprite = to_underlying(direction) + _roadveh_images[spritenum];
	...
}
```

⇒ 三条推论（把可能性收窄到一条）：
1. **`spritenum` 越界不可能表现为"空图像"**：第 138 行 `assert(IsValidImageIndex<VehicleType::Road>)` 会直接断言崩溃，而玩家现场没崩、画面还在跑 ⇒ 排除"spritenum 被读坏"。
2. 非自定义 sprite 走 `_roadveh_images[spritenum]`，同一个 spritenum 两版必得同一张图 ⇒ 若两版都走这一支，图像不可能不同。⇒ 唯一自洽解释：`spritenum = CUSTOM_VEHICLE_SPRITENUM`，走第 132 行 `GetCustomVehicleSprite()`，而它在两个版本里**解析到不同的 sprite**（R3R 那次解析出一张**有效但空白**的图，`result->IsValid()` 为真所以不回退）。
3. `GetCustomVehicleSprite()` 的选图完全由 **NewGRF 变量**决定，其中 `0x40`（position/length in consist）与 `0x41`（同型号连续长度）来自 `grf_cache.position_consist_length`，其产生函数就是 **`PositionHelper()`** —— 全库唯一被 R3R 改过、且已实证对非列车恒返回 `chain_before = 0` 的那一处（§16.3）。

⇒ **G-387-6 由"猜测根因"升级为首要根因**（仍待游戏内复测确认）。修复已于 2026-11-03 02:01 编入 `build\openttd.exe`（`EXIT_CODE=0`），复测判据：
- ① 同存档下 39928（及 39929/39930）图像恢复正常；
- ② 原版（非 NewGRF）铰接路车图像逐字不变；
- ③ 列车"段内 position"行为不变（多段链 NewGRF 变体图不变）；
- ④ 若仍空 ⇒ G-387-6 证伪，下一嫌疑转 `engine_type`（dump 不打印）与 `spritenum`（dump 不打印）。

### 16.8 玩家实机复测（2026-11-03，新 exe 02:01:50）：三条症状全消（本条为读数，非推断）

**玩家原话（三条，逐条照录）**：
> 「那个 GRF 的 ID 是 48531701，但是，根据我的肉眼观察，这座屎山似乎被一股神秘力量镇压了下去……我在出bug的版本中发现：1，当事汽车的长度，图像几乎都为空或默认，而新版本已变回正常。2，三节铰接式汽车只有两节出库，而新版本已变回正常。3，汽车进入某个特定车库之后游戏崩溃，而新版本汽车跑了一圈也没崩」

**GRF 标识**：玩家给出「48531701」。按 4 字节 GRFID 折算为 `0x02E488F5`（十进制 48531701 = 0x02E488F5），**未与 GRF 文件头/NewGRF 窗口核对**，此处仅记录玩家口径，不作为已核实事实。

**测试条件**：exe = `build\openttd.exe`（52 419 584 B @2026-11-03 02:01:50，含 §16.3/§16.7 的 `PositionHelper` 修复），同一个出 bug 的存档。

**判定（读数 → 结论，严格区分）**：

1. **第①条（图像/长度为空）与 G-387-6 严格闭环**：本次唯一被改的行为就是 `Vehicle::GetImage` 的选图输入（NewGRF 变量 `0x40/0x41` 的生产者 `PositionHelper()`）。空白图消失、长度恢复正常，正是该变量的直接下游。
   ⇒ **G-387-6 由"首要根因（待复测）"改判为「复测通过 / 成立」**。
2. **第②条（只有两节出库）有两种解释，均未证**：
   - `[2-A 纯视觉]` 第三节的图本来就是空白（同第①条），玩家在旧版里"看不见第三节"而非"第三节没出库"。此解释与"修复只影响渲染"完全自洽，按奥卡姆为首选。
   - `[2-B 行为]` 尾节 `rvs:FE`(`RVSB_IN_DEPOT`) 而 tile 是普通道路（`REG-387-B`），出库时尾节的 `state` 不允许它跟随前车，于是真的滞留在库里。
   - 区分判据（玩家已升级、旧 exe 不再具备）：旧版当时打开车库，看第三节是否还留在库里、或车辆列表的节数统计；若第三节仍在库里 = 2-B，若车库已空 = 2-A。
3. **第③条（进特定车库崩溃）不是本次修复能治的东西**：`Disconnecting road vehicle.` 的落点是 `src\roadveh_cmd.cpp:1757-1758`——
   ```1757:1758:src/roadveh_cmd.cpp
   		if (dir == INVALID_TRACKDIR) {
   			if (!v->IsFrontEngine()) FatalError("Disconnecting road vehicle.");
   ```
   触发条件 = `FollowPreviousRoadVehicle()` 对**非车头**车节返回 `INVALID_TRACKDIR`。而本次修复只改 `Vehicle::GetImage`（渲染），**完全不进入这条路径**。⇒ 崩溃不复现只能有四种解释：矛盾态本次未创建 / 玩家操作路径不同 / 存档已不含矛盾态 / 概率性时序。**不能据此宣布"崩溃已修"**。

### G-387-7（猜测根因，待验证）：② 与 ③ 同源 = 尾节 state 与 tile 矛盾，由"逐节搬迁被打断"制造

- 支撑证据：崩溃点只在**非车头**、只在**跟随失败**时触发；而"只有两节出库"正是"尾节没跟上链"的同一失败面的另一种观测。二者共同前置条件都是 `REG-387-B`（尾节 `rvs=FE` 自称在库，tile 却在普通道路）。
- 机制假设：进/出库是逐节搬迁，若中途被打断（崩溃、暂停、或原生代码按契约提前跳出），尾节停在"状态已是 IN_DEPOT 但位置还没进库"的混合态 ⇒ 出库时尾节不动（②）；之后车头再进库、尾节跟随失败 ⇒ `FatalError`（③）。
- 证伪/成立判据：
  - **成立**：新 exe 上 ② 或 ③ 复现，且 `dump_vehicle 39930` 的 `rvs:` 仍为 `FE` 而 tile 为 `type 20 (Road)`；
  - **证伪**：复现时 `rvs:` 正常（1 或其它非 FE），则矛盾态解释不通，转查 `FollowPreviousRoadVehicle()` 的路径匹配与 `state` 时序。
- 当前状态：**未能验证**（玩家现场已不崩、且 ② 大概率是 2-A 纯视觉）。

**GRF 侧输入已解决（2026-11-03 结案前补齐，本段推翻旧记录）**：
- 在 `C:\Users\冯洁敏\Documents\OpenTTD\content_download\newgrf\` 里按数字前缀直接命中：`48531701-HVW_Bus_set-0.3.1.tar`（内含 `HVWBS.grf`，7 954 604 B）。
- ⇒ **玩家给的 48531701 就是 GRFID 的十六进制原文**（`0x48531701`，字节 `48 53 17 01`）。此前"它是十进制数字、暂按文件编号记录"的写法**作废**；由此派生的 `0x02E488F5` 折算也一并作废（本就没核对过）。
- GRF 元数据（明文可读）：`HVW Bus Set` / 作者 `SosMakaroni` / `License: GPLv2` / `Ver.344 2025.12.08.`。
- 未做：`HVWBS.grf` 头部不是标准 `GRF` 偏移（前有 2 字节 `00 00`，`GRF` 落在 offset 2），按 offset 3 读容器版本得到垃圾值；不据此深挖容器格式，理由见 §16.10 的排他性论证——"GRF 内部是否读 0x40/0x41"已不再是判定所需。

### 16.9 玩家第二次复测（边界框读数）：把 G-387-6 从"推断"抬到"独立实证"

**玩家原话（照录）**：
> 「是这样的，我打开边界框，看到旧版本：仅两节边界框有显示且出库，整部汽车图像为空（如果谁显示了至少会有鬼畜的图像），边界框长度均为默认的1（原GRF绝对不是）。新版本：则有三节边界框显示且出库，图像正常显示，边界框长度正常。因此我推测屎山代码被神奇的压下去了（至少，bug像原生的那一样一个bug被另一个bug压下去了）」

**读数解码（已用代码核对，本节 v1 结论作废）**

调试边界框的画法由两段代码定死：

1. **有没有框**：`src\vehicle.cpp:1986-1990` 是逐 sprite 调 `AddSortableSpriteToDraw(v->sprite_seq.seq[i].sprite, ..., v->bounds, ...)`，循环次数 = `sprite_seq.count`。
   ⇒ **`sprite_seq` 为空（一张图都没拿到）⇒ 该节一个 sprite 都不入视口 ⇒ 根本不会有框**。
2. **框多大**：框的屏幕矩形来自 `v->bounds`（`src\viewport.cpp:1320-1323` 把 `bounds.extent` 算进 `ps.xmin..zmax`），**不是** sprite 尺寸。而 `RoadVehicle::UpdateDeltaXY()`（`src\roadveh_cmd.cpp:448-500`）：
   - 直向 N/E/S/W：`bounds.extent` 恒为 `{3,3,6}`，**与车长无关**，车长只体现在 `bounds.offset`（沿轴向偏心）；
   - 斜向 NE/NW/SE/SW：`bounds.extent.x` 或 `.y` **直接等于 `cached_veh_length`**（`:473` / `:479` / `:486` / `:492`）。

⇒ 于是「框长 = 1」只有两种解读：
- **(a)** 该车朝斜向，且 `cached_veh_length == 1` —— 物理缓存长度异常（正常公路车 4~8）。这是**与 sprite 无关的另一条异常**，需要单独解释：它由 `ConsistChanged(CCF_LENGTH)` / NewGRF 长度回调给出，本次修复完全没碰过这条链。
- **(b)** 该车朝直向，看到的就是固定的 3×3 小方框，玩家把"小方块"读成"长度 1"。

**⇒ §16.9 v1 的结论（"框长 1 = GRF 返回 1px sprite = G-387-6 实证"）作废**：它错把 `bounds` 当成了 sprite 尺寸。G-387-6 仍是最合理根因，但证据强度回到「推断 + 待验证」。

**真正新增的证据：第三节"连框都没有"**
- 这等价于 `39930->sprite_seq.count == 0`，比"拿到 1px 图"更严重：不是画了空白，而是**一张图都没产出**。
- 与 `GetCustomVehicleSprite()` 的失败路径吻合（GRF 回调给不出有效 sprite 时可能一个 sprite 都不写），仍指向 `0x40/0x41`，但不能排除 `spritenum` / `engine_type` 侧的其它原因。

**第②条再判（结合 32 格证据）**
- 上次崩溃 dump：39930 tile = 7013,966，前两节 7013,998，**相距 32 格**。
- 若玩家当时相机在车库附近，第三节早已跑出视野 ⇒ 「只有两节」= **视野内只有两节**，而不是"没出库"。
- 这同时把③串起来：前节在库、后节在 32 格外的路上 ⇒ 后节控制器一跑就 `Disconnecting road vehicle.`（`src\roadveh_cmd.cpp:1757-1758`）。
- **玩家已答复（2026-11-03，两个分叉都收敛）**：
  - 朝向：「车沿着 x/y 轴走（总之不是处于拐弯边界框变固定的状态）」⇒ 落在 (b) 支：轴向 `bounds.extent` 恒 `{3,3,6}`，屏幕约 6×3 px，**与车长无关**，车长只进 `bounds.offset`。
  - 第三节位置：「第三节不可能在视野外，因为这个汽车的调度命令导致它根本不可能在视野外，所以它只可能在车库里」⇒ **排除"视野外"**，所以「只有两节」是**真差异**，落在"第三节没跟出库"上，不是观察假象。
- **对「1 px 框」的最终判读**：轴向时框是一个**固定的小方框**，沿行进轴没有任何延伸，看起来就像"长度 1"。所以该读数**不携带尺寸信息**：既不能反推 `cached_veh_length == 1`（这会让 (a) 支复活），也不能反推 sprite 像素尺寸。它只能说明"这一节的可见体量约等于零"——与 §16.7/§16.8 的结论同向，不需要新解释。
- §16.9 v1 的错误到 v2 定稿的轨迹：v1 把框当 sprite 尺寸 ⇒ 作废；v2 先说"框小 = bounds 固定小方块 = 正常"⇒ 正确，但当时把它当成"玩家读错"，现在按玩家答复收紧为"该读数在任何解读下都不是尺寸证据"。

**（保留）边界框读数不能直接当"出库"判据**：框是逐 sprite 画的，`sprite_seq` 为空或车在视野外都不会有框，与"车节是否跟随"无关。玩家在图像全空时只能靠框判断，这个观察方法本身会把"看得见"与"在队里"混在一起。

### REG-387-C（新疑点，2026-11-03 登记）

- **期望值**：`PositionHelper()` 只写 `grf_cache.position_consist_length`，仅被 `GetCustomVehicleSprite()` 读取，无游戏语义副作用。
- **实测值**：玩家报旧版两节出库、新版三节出库（若确为行为差异），无法用渲染解释。
- **为什么存疑**：要么玩家的"出库"判据实质是"看得见"（观察混淆），要么存在一条我尚未找到的 image → 行为通路。
- **还需要什么数据**：新版同一列车三节 `dump_vehicle` 的 tile + `vs` + `rvs`；以及旧版当时是否有第三节 dump（现有 dump 取自崩溃时刻，tile 差 32 格，不能代表玩家看出库那一刻）。
- **2026-11-03 收窄**：玩家答复"第三节只可能在车库里"排除了"视野外"这一半解释 ⇒ 旧版确有一节留在库里，属**行为差异**而非观察假象。但新版不复现，且本次修复的代码在渲染路径上（见 §16.10 排他性论证），因此该差异**无法归因到本次修复**，登记为观察项，不再追（不改判为"已修"）。

### 16.10 结案（2026-11-03）：图像侧逻辑闭环，行为/崩溃侧未复现、不记已修

**排他性论证（代码级，替代"必须 dump"）**

`grf_cache.position_consist_length` 在全代码库只有三处出现（已逐处核对）：

| 位置 | 作用 |
|---|---|
| `src\vehicle_base.h:63` | 字段定义 |
| `src\newgrf_engine.cpp:684-688` | `case 0x40:` 内**写入并返回**给 GRF |
| `src\newgrf_engine.cpp:1826-1833` | 缓存失效表，仅登记 key |

⇒ 该字段**没有第二个消费者**；`PositionHelper()` 的全部输出都从这里流向 NewGRF 变量 `0x40`（`0x41` 走 `position_same_id_length`，同构）。
⇒ 本次修复只改了 `PositionHelper()`（`src\newgrf_engine.cpp:472-479` 补 `else`），**别的什么都没动**。
⇒ 图像"从空变正常"这件事，**必然**经由 `0x40` 这条通道。这是逻辑排他，不需要再证明"该 GRF 是否读 0x40"，也不需要 dump。

**三条症状的最终判定**

| 症状 | 判定 | 依据 |
|---|---|---|
| ① 图像/长度几乎为空 | **已修 + 已实证** | `0x40` 通道唯一性 + 实机复测（§16.8）+ 崩溃 dump 中 `sprite_seq` 为空的反证（§16.9） |
| ② 三节只有两节出库 | **未复现，不记已修** | 玩家确认非视野外 ⇒ 真差异；但修复点在渲染路径，无通向行为的通路（REG-387-C 仍未解） |
| ③ 进特定车库崩溃 | **未复现，不记已修** | 崩溃点在 `roadveh_cmd.cpp:1757` 的 `FollowPreviousRoadVehicle()`，本次一行未碰 |

**为什么不再要求 dump**：`dump_vehicle` 是游戏内控制台命令，只能在运行中的游戏里执行；AI 侧可以无头加载存档，但没有注入控制台输入的通道（要注入就得改源码加探针，为取证改产品代码不划算）。而"①已闭环"不需要它，"②③未复现"也没有可 dump 的对象（不崩了）。⇒ **本轮以结案收口，不再向玩家索取任何取证数据。**

**遗留观察项（全部不阻塞，不要求玩家动作）**
- `REG-387-A`：原生为何不崩；`REG-387-B`：`rvs=FE` 与 tile 在普通道路的矛盾；`REG-387-C`：是否存在 image → 行为通路；`G-387-7`：②③同源假设。
- 复现契约：若 ② 或 ③ 再现，只需给 `build\R3R_debug.log` 尾部 + `crash-*.log`，不需要专门 dump。

### 关于「一个 bug 压另一个 bug」的回应（口径，非读数）

不是。`PositionHelper()` 的段内计数是 R3R 为**列车**设计的（段边界 = ★），非列车（公路车/船/飞机）走进了一条没有 `else` 的分支，`seg_front` 停在 `v` 自己 ⇒ `chain_before` 恒 0。这是 R3R 自己引入的回归；修法是补一个 `else { seg_front = v->First(); }`（`src\newgrf_engine.cpp:472-479`），**没有新增任何条件逻辑、没有用别的错误去抵消**。所以是"修掉一个被静默吞掉的变量错误"，不是"bug 压 bug"。
