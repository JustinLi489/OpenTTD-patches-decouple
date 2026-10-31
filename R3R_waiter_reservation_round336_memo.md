# 第 336 轮临时分析报告：脚下保护改为「选择性触发」（KI-351 落地）

日期：2026-10-27
关联：`R3R_reverse_long_reservation_round335_memo.md`、`R3R_KNOWN_ISSUES.md` KI-351

## 一、玩家拍板（原话）

> 「现在的脚下保护，居然动了 OTTD 的屎山。既然如此，我们只能让这个脚下保护为此让步了：我们让这个脚下保护只能选择性的在特定场景触发」

口径确认：**不是削弱保护语义，而是把「每 tick 无条件重订」改成「按场景/按缺失触发」**。目的是让 `ReverseTrainDirection()` 之后被 `FreeTrainTrackReservation()` 清空的 `res_tracks` 保持为 `TRACK_BIT_NONE`，从而让 `ChooseTrainTrack()` 里的 `early-haverserved` 早退不再命中，恢复上游「掉头后一次铺整条路径」的长预留行为。

## 二、取证锚点（本轮实测，行号=当前工作树）

`ReserveTrackUnderConsist()` 的真实调用点全项目只有 3 处（其余命中均在注释或 `src/train_cmd.cpp.r3r_bak`）：

| 行号 | 场景 | 频率 | 本轮处置 |
|---|---|---|---|
| `src/train_cmd.cpp:10311-10312` | 解挂提交点（`u->ClearReservationUnderConsist(); u->ReserveTrackUnderConsist();`） | 一次性 | **不动**（事件触发，本来就是选择性） |
| `src/train_cmd.cpp:19229` | `Train::Crash()` 之后「Crash clears the reservation」 | 一次性 | **不动** |
| `src/train_cmd.cpp:21545` | R3R platform-waiter gate，每 tick 对「站台 + 停稳 + (car-only 或 WAIT_COUPLE)」重订 | **每 tick** | **本轮改为选择性触发** |

定义：`src/train_cmd.cpp:19073`。逐节 `TryReserveRailTrack(u->tile, TrackBitsToTrack(bits))`；站台/路点格会落到 `SetRailStationReservation` 的整格位；`TRACK_BIT_DEPOT` 与 wormhole 分支单独处理。

gate 原文（`:21513-21560`）关键三行：
```21513:21515:src/train_cmd.cpp
	bool r3r_platform_waiter = R3RIsCarOnlyFormation(consist) ||
			(consist->IsPrimaryVehicle() && consist->current_order.IsType(OT_WAIT_COUPLE));
	if (r3r_platform_waiter && consist->cur_speed == 0 && IsTileType(consist->tile, TileType::Station)) {
```
其后是「首次 purge 路径预留 + `lookahead.reset()`」(`:21534-21543`)，然后**无条件**执行 `ReserveTrackUnderConsist()` 与沿站台轴整条 `TryReserveRailTrack()` (`:21545-21554`)。

### 关键结论句

- 结论 1：**每 tick 无条件重订只存在于 waiter gate 一处**，所以「脚下保护动了上游屎山」的责任面可以精确收窄到这一段。
- 结论 2：该 gate 的判据是「站台 + `cur_speed == 0` + (car-only 或 `OT_WAIT_COUPLE` 主车)」。⇒ 普通列车在站台尽头自然调头**不命中**；若玩家观察到普通列车也丢长预留，说明还有 G-2（`Stuck=1` 时上游根本不 Free）或另一条 R3R 路径，不能只靠本改动收口。
- 结论 3：`ForceReserveOnce`（`train.h` bit27）是「一次性无视红灯订路」，在 `ChooseTrainTrack` **更晚**处消费（`:18137-18199`），**不能**用来绕开 `early-haverserved` 早退，故本轮不借用它。

## 三、落地设计：S1~S4 场景清单

把「每 tick 订」拆成四个显式场景：

- **S1 进入等待态首 tick（边沿）**：沿用现有 `_r3r_waiter_purged.insert(...).second`。首 tick 仍做「purge 路径预留 → `lookahead.reset()` → 全量订脚下 + 整条站台」。语义与原来完全一致，只发生一次。
- **S2 预留缺失时才补**：非首 tick 只做**只读**健康检查 `R3RWaiterReservationIntact()`：
  - 逐节：`TRACK_BIT_DEPOT` / wormhole 跳过；`track == TRACK_BIT_NONE` 且格既非 plain rail、也非站台/路点 ⇒ 跳过（不调 `GetTrackBits()`，规避 `rail_map.h:168` 断言）；站台/路点格查 `HasStationReservation()`；其余查「`TrackBitsToTrack(bits)` 这一条轨是否仍在预留位里」（与 `ReserveTrackUnderConsist()` 实际订的范围**一致**，不能要求整格 bits 全订，否则双槽格会永久判缺失 ⇒ 退化成每 tick 订）。
  - 站台轴向整条：`IsCompatibleTrainStationTile()` 范围内每格 `HasStationReservation()`。
  - 全部在位 ⇒ **一行不订、不写日志**（这就是玩家要的「让步」）。
  - 有缺失 ⇒ 执行一次全量订 + 探针 `RESV-WAITER-REPAIR`（说明是谁把它清掉了：KI-427 前车出站清整条到发线、掉头 Free、地图改动等）。
- **S3 解挂提交点**：沿用 `:10311`，不动。
- **S4 崩溃后**：沿用 `:19229`，不动。

## 四、风险与边界

- 风险 R1：健康检查与 `ReserveTrackUnderConsist()` 的「订范围」必须逐字对齐（`TrackBitsToTrack(bits)` 单轨，而非 `bits` 全部），否则双槽格会天天判缺失，等于没改。已按此实现。
- 风险 R2：站台整格位无法区分「谁订的」，只能判「位是否还在」。别人订着也算在位 —— 对 PBS 阻挡语义无影响，可接受。
- 风险 R3：`IsCompatibleTrainStationTile()` 只覆盖站台，路点不在原循环范围内，保持原样不动。
- 风险 R4：S2 的补订比原来晚 1 tick（前车出站清掉 → 下一 tick 才补回）。这一 tick 内等待者的脚下可能被别的车抢订；可接受，且这是「让步」的必付代价。
- 未覆盖：普通列车（非等待者）掉头路径完全不经过本 gate。若复测仍无长预留，按第 335 轮 G-2/G-3 判据继续，并依赖本轮新增的 `RESV-WAITER-*` 探针先证明「gate 到底有没有命中掉头车」。

## 五、复测判据

1. 等待者停稳后（非首 tick、位未丢）日志**不再**每 tick 出现重订痕迹，`RESV-WAITER-REPAIR` 为 0 条或极少。
2. 等待者被前车出站清掉位（KI-427 场景）后，下一 tick 出现恰好一条 `RESV-WAITER-REPAIR`，且耦合寻路仍能找到它（`COUPLE-OK` 照常）。
3. 掉头后（`REVERSEDIR`）若该链是站台等待者，`RESV-WAITER-SKIP reason=just-reversed` 出现且**不再**同 tick 重订；普通行驶列车掉头后首次 `CRT found=1` 且出现长 `RP cur=` 串。
4. 负判据：正常等待者不得出现「无预留」状态（不得出现 `TPR-BLOCKED` 因等待者脚下空白而失败）。
5. 负判据：车库内/非站台 `OT_WAIT_COUPLE` 行为逐字不变（本 gate 不覆盖）。

## 六、最终落地口径（代码实际形态，与 §三 编号的对应）

§三 的 S3/S4 是「解挂提交点 / 崩溃后」，那是**既有的一次性调用点，本轮不动**。真正被本轮改造的 gate 内部拆成下面四个场景（对外汇报用这套编号）：

| 场景 | 触发条件 | 行为 | 代码 |
|---|---|---|---|
| **S1 首次进入等待态** | `_r3r_waiter_purged.insert()` 首次 | purge 路径预留 + `lookahead.reset()` + 全量订（脚下每节 + 站台沿轴整条） | `:21623`、`:21676` |
| **S2 预留丢了才补** | 非首次且 `R3RWaiterReservationIntact()` 为假 | 补一次全量订，打 `RESV-WAITER-REPAIR reason=reservation-lost` | `:21671-21676` |
| **S3 本 tick 刚掉头** | `_r3r_just_reversed` 有该车号且非 S1 | **什么都不订**，打 `RESV-WAITER-SKIP reason=just-reversed` | `:21665-21670` |
| **S4 稳态** | 以上都不成立 | **不碰地图**（原「每 tick 重订」被删掉的部分） | 无代码路径 |

掉头打标：`R3RMarkChainJustReversed()`（`:12393`，仅 `TileType::Station` 落标记）在 `ReverseTrainDirection()` 入口 `:5260` 调用 ⇒ force 换端、订单调向、自然调头、站内调头所有路径统一打标。标记用后即删（gate 消费 `erase`，等待态结束时也 `erase`）。

### 玩家审阅时提的三个取舍点（结论：维持现状）

1. **S3 只在站台上打标**（`:12395` 有 Station 守卫）。掉头发生在区间/车库时不打标、也就没有 SKIP 行 —— 保持现状。
2. **S2 判「位是否还在」不判「是不是我的」**。别人订着也算在位 ⇒ 稳态不每 tick 重订；代价是无法识别「被别人的位顶掉」。对 PBS 阻挡语义无影响。
3. **「站台沿轴整条」仍随 S1/S2 一起订**。它是耦合寻路找车底的依赖，未拆开。

## 七、构建自证（第 336 轮）

- 复用既有 `_tmp_inc_build.cmd`（**未新建任何 .cmd**）；`build\R3R_incbuild.guard.log` = `GUARD: incremental is safe (no header/lang file is newer than the newest object)` ⇒ 增量合法（本轮只改 `src\train_cmd.cpp`，未碰 `src\*.h`）。
- 日志尾部 `[2/3] Building CXX object CMakeFiles\openttd_lib.dir\src\train_cmd.cpp.obj` → `[3/3] Linking CXX executable openttd.exe`；`build\R3R_incbuild.done` = `EXIT_CODE=0`。
- 时间戳：`src\train_cmd.cpp` **19:37:01** → `train_cmd.cpp.obj` **19:44:40** → `build\openttd.exe` **19:46:03**（52 322 304 B），全部满足「obj 晚于源码、exe 晚于 obj」。
- `read_lints`：`src\train_cmd.cpp` **0 条**。
- exe 字面量核对：`findstr /M /C:"reason=just-reversed" /C:"RESV-WAITER-REPAIR"` 命中 `build\openttd.exe`。
- 构建前 `tasklist` 确认 `openttd.exe` 未运行（避免 LNK1168）。

## 八、边界（必须记住的一条）

S3 只在「刚掉头 + 恰好在站台上 + 恰好在等待态」三者同时成立时生效。**掉头的若是普通行驶列车，它根本不进这段代码** —— 那种情况下的长预留消失属 KI-351 的 G-2（`Stuck=1` 时上游不 Free）/ G-3（`RESV-NOBOOK`）分支，本轮的让步修不到。复测时先看日志：有 `REVERSEDIR` 但既无 `RESV-WAITER-SKIP` 也无 `RESV-WAITER-REPAIR` ⇒ 掉头列车不是等待者，转 G-2/G-3 继续。

## 九、第 337 轮：玩家复测「长预留还是不出现」（取证中，2026-10-27）

**输入**：玩家拖入的 `D:\temp\codebuddy-dropped-files\ed56b80e-cfa1-45e6-90c4-0cd063783cf6\R3R_debug.log`（约 1740+ 行）。玩家原话：「可恶，长预留还是不出现」。本日志产生者 exe 未核对（未读 `build\openttd.exe` 时间戳），但含第 336 轮新增探针字段格式 ⇒ 应先核实。

**已取到的锚点（逐字）**：
```
1379 LINEND veh=57 tile=16,14 x=15 sig=0 near=1 spd=5 brk=15 clamp=0 len=2 dir=5 db=0
1380 REVERSEDIR veh=57 tile=16,14 dir=5 order=16 spd=0 nv=3 rev=0 stuck=0 db=0
1384 REVERSEDONE db=1 mvfront=59
1388 TPR-EXIT veh=57 state=ok pair=0 tile=16,14 spd=0 other=57 otherTile=16,14 otherSpd=0 otherOrder=16 origin=16,14 okay=1
1389 TRP veh=57 mf=16,14 real=1 type=16 dest=1932 spd=0 fp=0 look=1 one=0 mstuck=0 fto=1 => ok=1 res=1
1390 COUPLE-WAIT-OFFDEST veh=57 tile=16,14 mf=16,14 real=1 isDepot=0 destDepot=0 destStn=0 destTile=1932 onDepotTile=0 onStnTile=1
1391 SPD-DROP veh=57 tile=16,14 prev=5 order=16 real=1 stuck=0 fp=0 prog=2 wait=0 parked=0 stop=0 track=01 dtile=1932 dt=33
```
另一处掉头：`1565 REVERSEDIR veh=54 tile=18,16 dir=5 order=2 spd=0 nv=3 rev=0 stuck=0 db=0`（`order=2`=OT_GOTO_DEPOT，见 1576 `DEPOT-ARR … destDepot=1`）。

**结论 1（§八 边界命中，非回归）**：全日志 **0 条 `RESV-WAITER-SKIP`、0 条 `RESV-WAITER-REPAIR`**；仅 1 条 `RESV-NOBOOK veh=54 tile=1,15 order=16 caronly=0 reason=depot-paired`（`:331`，与本题无关）。⇒ 两处掉头车都是「带引擎的机车链 + 订单 16/2」，**不是站台等待者**，第 336 轮的 S3 永远不会命中它们。第 336 轮改动对这两个现场**零作用**，符合 §八 的预判。

**结论 2（关键取证，与预判不同）**：掉头后 `TryPathReserve` **返回了成功**（`1389 … ok=1 res=1`），但**掉头之后没有任何 `CRT veh=57` 行**（全日志 `CRT veh=57` 只有 `:387`、`:392`、`:1738` 三处，均在掉头前/很久之后）。`1738 CRT veh=57 order=1 dir=5 origin=14,15 td=8 found=1` 出现在 veh=57 已跑出去之后。⇒ **yapf 的 `ChooseTrainTrack` 在掉头后根本没被调用**，所以不可能铺出长预留；这与「G-1：脚下被重订 ⇒ early-haverserved 早退」的形态一致，但**订脚者的身份不能归给 waiter gate**（gate 未命中）。

**猜测根因 R337-G1（待验证）**：`TryPathReserve` 在进入 `ChooseTrainTrack` 之前就早退（`res_tracks != TRACK_BIT_NONE` 的 early-haverserved 分支），因此既不进 yapf、也不打 `CRT`。订上脚下来源的候选：
- **a** `ReverseTrainDirection()` 的 `no_near_end_unreserve / no_far_end_unreserve` 为真时**根本不调 `FreeTrainTrackReservation()`**（`src\train_cmd.cpp:5313-5318` 的 `if` 条件），旧预留原样留着 ⇒ 掉头后 `res_tracks` 非空。本现场 `1384 REVERSEDONE db=1 mvfront=59` 说明走的是 `ReverseTrainSwapVehicles` 分支，正好是这两个参数为真的路径。
- **b** `Stuck` 为假但另一条 R3R 保护（`ReserveTrackUnderConsist` 的解挂/崩溃两处）在掉头同 tick 补订，本现场无解挂/崩溃记录，可能性低。
- **c** `res_tracks` 里只剩「脚下那几格」，本来就没被清 ⇒ 与 a 同源。

**下一步取证（等代码核对后再定性）**：①读 `5313-5318` 实际条件与两个参数在调用链上的取值；②确认 `TRP` 探针的 `res=1` 到底是「预留成功」还是「res_tracks 非空」；③找 `early-haverserved` 早退处是否有独立探针（若无，需加一行 `RESV-EARLY-HAVER res=<bits> veh=` 才能一刀切开 a/b/c）。

**代码侧已核对（本轮第二次取证，锚点均为当前源码）**：
1. `src\train_cmd.cpp:5299-5325`：`no_near_end_unreserve / no_far_end_unreserve` **只在「隧道桥 + 双向信号模拟」** 时可能被置真（`:5314` 的 `IsTunnelBridgeWithSignalSimulation && ...Bidirectional`）。本现场 `16,14`、`18,16` 都是普通站台/轨道格 ⇒ 两参数恒 false；`REVERSEDIR … stuck=0` ⇒ `:5321` 条件成立 ⇒ **`FreeTrainTrackReservation(consist)` 必然执行**。
2. `src\train_cmd.cpp:15803`：`FreeTrainTrackReservation` 在 `origin == INVALID_TILE`（默认参数）时第一句就是 `consist->lookahead.reset()` ⇒ 掉头时 lookahead **必然被清**。⇒ **猜测 a 证伪**。
3. 但 `1389 TRP veh=57 … look=1` ⇒ 掉头后 lookahead **又被建起来了**；而 `CRT` 由 `src\pathfinder\yapf\yapf_rail.cpp:1441` 在 `YapfTrainChooseTrack` 内 **无条件**打印（无 `R3RDbgEdge` 节流），全日志 `CRT veh=57` 在 `:1380`~`:1738` 之间 **一条都没有**。

⇒ **猜测根因 R337-G2（待验证，取代 G1）**：掉头后那次 `TryPathReserve` **没有进入 `YapfTrainChooseTrack`**，而是走了 **`ExtendTrainReservation()` 分支**（`look=1` 即 lookahead 非空），用一条"掉头前遗留/很短"的 lookahead 做 extend ⇒ 只续订到旧路径末端，**不会按新方向重新铺长预留** ⇒ 玩家看到「长预留不出现」，且 `CRT` 缺失。与 waiter gate 无关，`RESV-WAITER-*` 为零是正常的。

**R337 待验证清单**：①`TRP` 探针的打印点在函数入口还是返回前（决定 `look=1` 是"见到旧 lookahead"还是"新建了 lookahead"）；②`ExtendTrainReservation()` 的进入条件与它在 `lookahead` 失效时的行为（若 extend 会把 lookahead 原样留着，就是 G2 的实锤）；③本现场 `16,14` 掉头后车停住（`1391 SPD-DROP prev=5`）→ 停驶分支不再走 `TrainController` ⇒ 之后再无预留机会，长预留"永远不出现"；④若 G2 成立，修法方向 = `ReverseTrainDirection` 的 Free 之后**强制 `lookahead.reset()` 并禁止本 tick extend**（或让 `TryPathReserve` 在「本 tick 刚掉头」时跳过 extend 直接进 yapf，正好复用第 336 轮新加的 `_r3r_just_reversed` 集合，但**不能只对站台等待者**，要覆盖全部掉头链）。

**未做**：本轮尚无源码改动、未构建。

**白名单清单（玩家 2026-10-27 追问「白名单再列出来看看」，逐条从源码抄录，锚点=当前 train_cmd.cpp）**：

唯一一处「在站台上主动把脚下+整条站台订回来」的白名单 = `src\train_cmd.cpp:21624-21626`：
```cpp
bool r3r_platform_waiter = R3RIsCarOnlyFormation(consist) ||
        (consist->IsPrimaryVehicle() && consist->current_order.IsType(OT_WAIT_COUPLE));
if (r3r_platform_waiter && consist->cur_speed == 0 && IsTileType(consist->tile, TileType::Station)) {
```
⇒ 名单内只有两类，且各自还要满足两个附加条件：
1. `R3RIsCarOnlyFormation(consist)` —— 纯车底链（无引擎），订单类型不限；
2. `consist->IsPrimaryVehicle() && current_order.IsType(OT_WAIT_COUPLE)` —— 链头 + 订单 17；
3. 附加：`cur_speed == 0`（停稳）；
4. 附加：`IsTileType(consist->tile, TileType::Station)`（人就在站台格上）。

进名单后由第 336 轮分三档（`:21645-21686`）：
- `r3r_first_wait_tick` �� `R3RReleaseChainReservations(…,"waiter")` + `lookahead.reset()` + `ReserveTrackUnderConsist()` + 沿轴向整条站台 `TryReserveRailTrack`；
- `r3r_just_reversed && !first_wait_tick` ⇒ 只打 `RESV-WAITER-SKIP`，**本 tick 什么都不订**；
- `first_wait_tick || !R3RWaiterReservationIntact()` ⇒ 打 `RESV-WAITER-REPAIR` 并重订；
- 稳态（名单内、非首次、非刚掉头、预留完好）⇒ **一次地图写都没有**。

`ReserveTrackUnderConsist()` 全工程 4 个调用点，**另 3 个与白名单无关**：
`src\train_cmd.cpp:10319`（解挂时给解出的 u 半）、`:19340`（`TrainCrashed()` 里崩溃清了预留后重订）、`src\saveload\afterload.cpp:2522`（读档后）。

**现场对照**：`veh=57` 订单 16(GOTO_COUPLE)、`veh=54` 订单 2(GOTO_DEPOT)、两者 `caronly=0` ⇒ **两条都不在白名单**，`21626` 整块不进 ⇒ 既不铺脚下预留、也不会打 `RESV-WAITER-*`。这解释了「0 条 SKIP / 0 条 REPAIR」，并再次说明修法必须覆盖白名单外的普通掉头链。

---

## 十、第 337 轮续：按玩家要求移除 car-only 白名单 + 长预留铺不出的真根因（2026-10-27）

### 10.1 已落地的源码改动（仅 `src\train_cmd.cpp`，未碰任何 `.h`）

玩家原话：「把"纯车底链（无引擎），订单类型不限"从白名单移除」。

`src\train_cmd.cpp:21624`（改动前）：
```cpp
bool r3r_platform_waiter = R3RIsCarOnlyFormation(consist) ||
        (consist->IsPrimaryVehicle() && consist->current_order.IsType(OT_WAIT_COUPLE));
```
改动后（只剩一支）：
```cpp
bool r3r_platform_waiter = consist->IsPrimaryVehicle() && consist->current_order.IsType(OT_WAIT_COUPLE);
```
注释同步改写（原 21606-21623 的「must cover not only car-only formations but also…」整段删除，换成「已移除 car-only，代价与还原方法」）。

`R3RIsReservationHolder()` 里那一支**刻意未动**：它是记账/GC 判据，删掉会让车底链的存量预留被 `RESV-REAP` 当孤儿清掉——而现在已经没人替它重铺了。

### 10.2 这个改动对本次现场（veh=57 / veh=54）**零影响**

- `veh=57` order=16(GOTO_COUPLE)、caronly=0 ⇒ 旧判据的第 2 支（`IsPrimaryVehicle && OT_WAIT_COUPLE`）也不成立；
- `veh=54` order=2(GOTO_DEPOT)、caronly=0 ⇒ 同样不成立。
⇒ 两车**在改动前后都进不了** `21626` 那个 if。所以「移除白名单」不会让它们停止铺脚下预留（它们本来就没铺），也不会让长预留冒出来。

### 10.3 长预留铺不出的真根因：根本**没有进** `ChooseTrainTrack()`

`src\train_cmd.cpp:18646-18659`（`R3RTryPathReserveCore()` 内，`ChooseTrainTrack` 调用点 `:18710` 之前）：

```cpp
/* If we have a reserved path and the path ends at a safe tile, we are finished already. */
if (origin.okay && (moving_front->tile != origin.tile || first_tile_okay)) {
    ...
    if (_settings_game.vehicle.train_braking_model == TBM_REALISTIC) {
        FillTrainReservationLookAhead(consist);
        TryLongReserveChooseTrainTrackFromReservationEnd(consist, true);
    }
    ...
    return TPRRF_RESERVATION_OK;          // ← 直接成功返回，:18710 的 ChooseTrainTrack 不会执行
}
```

- `origin` = `FollowTrainReservation()` 沿**已有**预留走到的尽头格；
- 只要「尽头是安全位(origin.okay)」且「尽头不是车头自己脚下那一格」，就整条早退；
- 这正是日志形态：`1389 TRP veh=57 ... => ok=1 res=1` 但 `CRT` 一条都没有（`CRT` 在 yapf 内无条件打印、无节流）。

唯一的延展机会是 `TryLongReserveChooseTrainTrackFromReservationEnd()`（`src\train_cmd.cpp:16981-17000`），它内部**只**调 `TryLongReserveChooseTrainTrack()`，而后者（`:16975`）的条件是：

```cpp
if (ft.Follow(tile, td) && HasLongReservePbsSignalOnTrackdir(v, ft.new_tile, ..., lookahead_state.flags)) {
    /* 只在长预留 PBS 信号处才继续预留，递归 */
}
```

⇒ **没有 `HasLongReservePbsSignalOnTrackdir` 认定的长预留信号，延展函数是空操作**。另外 `:16986` `if (IsRailDepotTile(origin.tile)) return;`——预留终点落在车库格时它也直接放弃。

### 10.4 结论（与上一轮的推断对照）

- 第 336 轮我写的「掉头后走 `ExtendTrainReservation()` 分支、用残留 lookahead 继续 ⇒ 不重铺」方向正确；
- 本轮把机制锚到了具体行号：**早退在 `:18647`，延展在 `:16975` 被长预留信号拦住**；
- 所以「脚下铺预留」**不是**长预留铺不出来的原因。删掉白名单只是把这条旁证排除掉，修法必须落在 `:18647` 这个早退上（掉头当 tick 让它不成立，强制进 `ChooseTrainTrack` 重铺），而不是继续在 `21624` 白名单里做减法。

### 10.5 待验证

- V-1：本改动后复跑，`veh=57` 的日志形态应与改动前逐字相同（用来证明 10.2）；
- V-2：修 `:18647` 早退后再复跑，`CRT veh=57` 必须出现，且预留长度覆盖到目的地。

---

## 十一、行号勘误 + 「为何还是铺不出长预留」的完整答复（2026-10-27，玩家第二次追问）

### 11.1 勘误（§10.3 的锚点写错了，以本节为准）

§10.3 里写的 `TryLongReserveChooseTrainTrackFromReservationEnd()` 位于 `16981-17000`、`TryLongReserveChooseTrainTrack()` 的判据在 `16975`、车库早退在 `16986` —— **三个都不对**（16981/16975/16986 那片其实是 `TryLongReserveChooseTrainTrack()` 内部**隧道/桥**分支）。重新核对后的真锚点：

| 位置 | 函数/作用 |
|---|---|
| `src\train_cmd.cpp:17019-17038` | `TryLongReserveChooseTrainTrackFromReservationEnd()` 定义 |
| `:17023` | `origin = FollowTrainReservation(v, nullptr, OkayUnused)` |
| `:17024` | `if (IsRailDepotTile(origin.tile)) return;` ← 预留尽头在车库格 ⇒ 直接放弃 |
| `:17034` / `:17036` | 两处都调 `TryLongReserveChooseTrainTrack(v, origin.tile, origin.trackdir, true, ...)`（第 4 参 `long_enough = true`） |
| `:17012-17016` | `TryLongReserveChooseTrainTrack()` **尾部**（唯一做延展的地方）：`if (ft.Follow(tile, td) && HasLongReservePbsSignalOnTrackdir(...)) ChooseTrainTrack(...)` |
| `:16955-17010` | 同函数内的**隧道/桥**分支（§10.3 误指为延展入口） |
| `:17052-17055` | `ChooseTrainTrack()` 开头的 R3R 注释，自陈 path reservation 是从这里订的（「the three `TryReserveRailTrack(moving_front->tile, ...)` calls below」） |

### 11.2 答复：脚下预留不是长预留铺不出的原因（四道闸门）

**前提先纠正**：`veh=57`（order=16、caronly=0）从来不在 `21624` 白名单内，它的脚下那格**不是白名单铺的**，而是 `ChooseTrainTrack()` 订路线时的自然第一笔（`:17052-17055` 注释自陈从 `moving_front->tile` 起订）。⇒ 「移除白名单 ⇒ 机车已经没向脚下铺预留」不成立，脚下照旧会被订。

长预留铺不出来的四道闸门（按执行顺序）：

- **闸门 A｜`18647` 早退**：`if (origin.okay && (moving_front->tile != origin.tile || first_tile_okay))` ⇒ `:18658 return TPRRF_RESERVATION_OK;`。只要「已有预留 + 尽头是安全位 + 尽头不是自己脚下那格」，就整条早退，**`:18710` 的 `ChooseTrainTrack()` 永不执行** ⇒ 路线不会被重新规划成整条。日志形态：`TRP ... => ok=1 res=1`，但 `CRT` 零条（`CRT` 在 yapf 内无条件打印）。
- **闸门 B｜`18651` 刹车模型**：唯一的延展调用 `TryLongReserveChooseTrainTrackFromReservationEnd(consist, true)` 被 `if (train_braking_model == TBM_REALISTIC)` 包住。非 realistic 刹车模型下**连延展函数都不会被调用**。
- **闸门 C｜`17024` 车库格**：预留尽头落在车库格 ⇒ `return`。`veh=54`（order=2 GOTO_DEPOT）方向可能命中这条。
- **闸门 D｜`17013` 长预留信号**：`ft.Follow()` 成功**且** `HasLongReservePbsSignalOnTrackdir(...)` 为真，才会递归 `ChooseTrainTrack()` 继续订。条件不成立 ⇒ 函数体走完、**一格都不多订**（纯空操作）。

**因果总结**：JGRPP 的长预留不是「一次寻路订到订单目的地」，而是「`ChooseTrainTrack()` 订到第一个 LR 信号/Stop 为止，之后靠 LR 信号逐段递归续订」。所以长预留铺不出来 = ①`18647` 让车压根不再重新规划整条路线，②`17013` 又要求线路上确有长预留信号。**与脚下有没有被订、与白名单有没有那两条，都无关。**

⇒ 修法落点仍是 `18647`（掉头/等挂当 tick 让它不成立、强制进 `ChooseTrainTrack` 重铺），不是继续在 `21624` 里做减法。

---

## 十二、归属判定：原生 OTTD 的锅还是 R3R 的锅（2026-10-27，玩家追问）

### 12.1 逐段查归属：这四段没有一行是 R3R 写的

| 位置 | 内容 | 归属 |
|---|---|---|
| `:18646-18647` | `/* If we have a reserved path and the path ends at a safe tile, we are finished already. */` + `if (origin.okay && (moving_front->tile != origin.tile \|\| first_tile_okay))` | **原生**（英文注释、无 R3R 标记） |
| `:18651` | `if (_settings_game.vehicle.train_braking_model == TBM_REALISTIC)` 才 `FillTrainReservationLookAhead` + 延展 | **原生** |
| `:17012-17016` | `ft.Follow() && HasLongReservePbsSignalOnTrackdir(...)` 才递归续订 | **原生** |
| `:5487-5497` | `first_tile_okay = moving_front_td == INVALID_TRACKDIR \|\| !HasBlockSignalOnTrackdir(...)`，车库出口/隧道桥出口再置 false | **原生** |

⇒ 「早退」「只认 LR 信号」「`first_tile_okay` 的定义」三项都不是 R3R 引入的。

### 12.2 原生到底承诺了什么（**关键：它从不承诺"铺到订单终点"**）

`:18324` 的参数注释是 `first_tile_okay`：**"True if no path should be reserved if the current tile is a safe position."**
⇒ 原生的 `TryPathReserve()` 语义只有一句：**"把路订到下一个安全位为止"**；脚下已是安全位时，连订都不必订（`first_tile_okay = true`，普通无 block signal 的轨道上恒为 true）。

JGRPP 的 long reserve（`:18651` + `:17013`）也没有扩大这个语义：它是**"订到长预留信号、再用 LR 信号递归续订"**，仍然不是"订到订单目的地"。

**所以玩家期望的"长预留覆盖到目的地"，从来就不是原生行为，也不是 JGRPP 行为 —— 它是 R3R 的需求。** 在这个口径下它不是 bug，而是没实现的功能。

### 12.3 但 R3R 确实引爆了一样东西：**"地图预留 ⟺ yapf 路线"这个原生隐含前提**

- 原生里，地图上的预留位**只可能**由 `ChooseTrainTrack()`/`TryReserveRailTrack()` 顺着一条真实规划出来的路线订下去（`:17052-17055` 注释自陈"path reservation is booked from here"）。因此 `:18647` 的判据是**可信**的：沿预留走到尽头 = 沿规划路线走到安全位 = 真的没事可做。
- R3R 的 `ReserveTrackUnderConsist()` 把"订预留"拆成了**独立入口**：不需要路线、不需要 yapf，每 tick 直接把脚下/整条站台订上（白名单那支）。`ClearLookAheadIfInvalid()` 又会把 lookahead 清掉。于是出现原生从未有过的状态：**地图上有预留格，但车上没有任何规划路线（lookahead == nullptr）**。
- `FollowTrainReservation()` 只看地图位，不看 lookahead ⇒ 它沿这几格"路线外预留"走到尽头，报 `origin.okay = 1` ⇒ `:18647` 判定"已完成" ⇒ 早退 ⇒ 永远不进 `ChooseTrainTrack` 重铺。

**这就是"R3R 引爆"的准确含义：不是原生有 bug，而是 R3R 制造了原生判据无法分辨的输入（有格、无路线），把原生的"信任预留"喂成了"假性完成"。**

### 12.4 排除项：现场 `res=1` 已经排掉了 R3R 三道新闸门

`:18206` 的 `TryPathReserveWithResultFlags()` 前面有三道 R3R 闸门，命中时直接 `return TPRRF_NONE`（日志 `res=0`）：
- `:18248` `R3RIsParkedWaiting()` → `RESV-NOBOOK`
- `:18281` `R3RShouldActAsWaitingForCouple()` → `CPL-WAIT-NOBOOK`
- `:18304` `R3RShouldHoldForCouple()` → `CPL-HOLD-NOBOOK`

现场 `TRP veh=57 ... => ok=1 res=1` ⇒ **已越过三道闸门、进了 Core**，是在 `:18647` 返回的。所以不是闸门误拦，确认是早退。

### 12.5 结论与修法口径（对 §11.2 的收紧）

- 责任划分：**判据是原生的、且原生语义自洽；破坏其前提的是 R3R。** 玩家的直觉成立 —— R3R 引爆的"东西"就是**脱离路线的孤立预留**。
- 因此 §11.2 里写的"修法落点在 `:18647`"需要收紧：**不要改原生的早退语义**（那是"脚下安全位不必订路"的正当优化，改它会让所有停着的车每 tick 重铺，性能与 PBS 语义都会震）。
- 正确落点在 R3R 侧，二选一：
  1. **可信性校验**（小改）：在 `:18647` 之前加一条"若 `consist->lookahead == nullptr` 且 `origin.tile` 距车头不超过 N 格，则视为路线缺失、继续往下走 `ChooseTrainTrack`"——即让 R3R 自己补上"预留必须有路线背书"这个原生默认成立、但被 R3R 破坏的前提；
  2. **自建整条路线预留**（大改）：R3R 按订单目的地自己跑一遍 yapf 拿整条路线并逐格订，不依赖原生的"到安全位为止"。这才是真正实现"长预留到目的地"。

⇒ 推荐先做 1（只改 `.cpp`、可回退、不动原生判据），再做 2。

### 12.6 待验证

- V-3：给白名单外的车（如 `veh=57`）打印 `lookahead=` 与 `origin.tile` 的格距，验证"`lookahead == nullptr` 且 origin 紧贴车头"是否恒成立 ⇒ 成立即 12.3 的实证；
- V-4：按 12.5-1 加校验后复跑，`CRT veh=57` 应出现，且 `TPR ... res=1` 不再在 origin 紧贴车头时提前出现。

### 12.7 大白话版（2026-10-27，玩家要求"讲大白话"）

**三句话**：
1. 火车订路，原生从来不"一次订到终点"，只订到"下一个安全的地方"，到了再订下一段。
2. 所谓的"长预留"（一路订到目的地）不是原生功能；它是"遇到 LR 信号牌就自动续一段"，路上没这种牌子，就一格都不续。
3. 更狠的是：车上只要还挂着一段"没走完的旧申请"，程序就认为"它已经有路了"，连看都不再看一眼，直接跳过重新规划。

**打比方**：
调度问一列火车："你有去目的地的行程单吗？"
火车不用回答路线，只要低头看地上——**自己脚下那块地还插着自己的旗子**，就答"有"。
调度于是满意地走了，不再给它排行程。
问题是：那块地只是"占位"，跟"怎么去目的地"一点关系都没有。车就永远站在自家院子里等。

**对上号**：
- "一个安全的地方" = 原生的 `TryPathReserve` 语义（`:18324` 的注释自己写着"脚下已是安全位就不必订路"）；
- "LR 信号牌续一段" = `:17013` 的 `HasLongReservePbsSignalOnTrackdir`；
- "低头看地上有没有旗子就答有" = `:18647` 的 `origin.okay` 早退；
- **"只插旗子不修行程"的那个人是 R3R**：`ReserveTrackUnderConsist()` 是 R3R 加的独立入口，它在站台上给停着的车订脚下/整条站台，不需要也不产生任何路线。

**结论**：判据是原生的、原生自洽；是 R3R 造出了原生从没见过的一种状态——**有旗子、没行程**——把"看旗子"的判据喂成了"已安排好"。

**修法大白话**：不要让车只看地上有没有旗子；要看它车上有没有一张行程单（`lookahead`）。没有行程单、旗子又只插在脚下，就当作"没路"，重新给它排一次。

---

## 十三、玩家口径纠正 + 关键代码更正（2026-10-27）

### 13.1 术语纠正：本文档 §1~§12 里的「长预留」用错了词

玩家原话口径：**「长预留」= 能一路铺设到安全停车位（订单终点）的预留，不需要走到哪一格铺到哪一格。**

⇒ 本文档此前把「长预留」混指 JGRPP 的 **LR 信号接力**（`HasLongReservePbsSignalOnTrackdir` + `TryLongReserveChooseTrainTrackFromReservationEnd`），那是**另一个东西**，只是名字撞车。LR 接力是"预留末端已在规划路线尽头、还想再往前延"时的补充手段，不是玩家要的功能。

⇒ 因此 §12.2 那句「原生长预留也不承诺订到目的地」在**术语上是错的**：JGRPP 的 LR 接力确实不承诺，但玩家要的「一次铺到终点」本来就不该靠 LR 接力实现。

### 13.2 关键更正：早退分支**内部也会**填 lookahead（§12.5 的事实基础要改）

上一轮说「早退 ⇒ 根本不填 lookahead ⇒ 长预留没启动」是**错的**。实读（以本轮读到的行号为准，与上一轮引用的 18647 不一致，可能是我此前引用错位）：

```18378:18390:src/train_cmd.cpp
	/* If we have a reserved path and the path ends at a safe tile, we are finished already. */
	if (origin.okay && (moving_front->tile != origin.tile || first_tile_okay)) {
		/* Can't be stuck then. */
		if (consist->flags.Test(VehicleRailFlag::Stuck)) SetWindowWidgetDirty(WindowClass::VehicleView, consist->index, WID_VV_START_STOP);
		consist->flags.Reset(VehicleRailFlag::Stuck);
		if (_settings_game.vehicle.train_braking_model == TBM_REALISTIC) {
			FillTrainReservationLookAhead(consist);
			TryLongReserveChooseTrainTrackFromReservationEnd(consist, true);
		}
```

⇒ 早退分支里长预留机制**是启动的**。真正的缺陷不是"没启动"，而是**起点错**：

- `FillTrainReservationLookAhead()` 是**沿现有预留前进**（它描述"从当前位置照现有预留能走多远"）；
- 现场现有预留只有 R3R 那几格孤立预留（脚下/站台），于是 lookahead 极短、`reservation_end_position` 就在车头附近；
- `TryLongReserveChooseTrainTrackFromReservationEnd(consist, true)` 从这个**错误末端**继续往外延，要求 LR 信号（`:17700` 一带的 `r3r_extend_long_reserve` 还被 `reservation_end_position <= prev_res_end` 主动关闭）。

**所以病根仍是同一个**：R3R 造出「有预留格、无规划路线」的孤立预留，早退把它当成真实路线的终点，长预留就以这个假终点为起点去延伸 —— 起点本身就是假的，铺多远都到不了目的地。

### 13.3 修法优先级据此调整（玩家已认可）

- **修法 1（可信性校验）升级为主线修法**，不只是"止血"：让 `origin` 不成立、进 `ChooseTrainTrack()` 做一次真正的到订单终点的规划 —— 而 ChooseTrainTrack/yapf 的预留本来就是**沿整条规划路径一次订下**的（`:17052` 一带注释自陈 "path reservation is booked from here"）。规划到终点 ⇒ 预留自然一路铺到终点，这正是玩家定义的「长预留」。
- **修法 2（自建整条路线预留）降级为备选**：只有当修法 1 落地后实测发现预留仍被信号/安全位截断时才需要。不再是"必须做的第二步"。

### 13.4 待验证

- V-5：修法 1 落地后，观察 `ChooseTrainTrack` 成功后同一 tick 的预留是否直接覆盖到订单目的地（看 `RP cur=` 序列长度 vs 到目的地格距）；覆盖 ⇒ 修法 2 可永久不做。
- V-6：若只覆盖到第一个信号/安全位，则确认截断点是 yapf 的 `pf` 终点还是 `TryReserveRailTrack` 的逐格守卫，再决定是上修法 2 还是改 LR 信号配置。
- V-7（承 V-3）：给 `veh=57` 这类白名单外车打印 `lookahead == nullptr` 与 `origin.tile` 格距，实证「孤立预留 = 脚下紧贴」。

---

## 十四、玩家给出的精确场景（2026-10-27，作为本条的唯一标准现场）

### 14.1 线路与过程（玩家原话口径）

- 线路布局：**轨道尽头依次是 路点 → 车站（站内停着等待挂接的车底）→ 车库**；路点与车库之间另有一股**空股道**可绕行。
- 走法：机车从**车库**出发 → 走**空股道** → 前往**路点** → **进入路点后换向**。
- 关键现场事实（玩家观察）：**换向之后，路点内部的那块预留并没有被销毁。**
- 现象对比：
  - **旧 exe**：换向后机车会铺出一条**到等待车底**的预留（等待车底所在位置同时就是安全停车位）——即一次铺到终点；
  - **现在**：机车**边走边铺**，预留不再是一次性到安全停车位。

### 14.2 这个场景把 §13 的推断变成了可观察事实

- §13.2 说「早退的起点是假的、R3R 的孤立预留把假终点喂给了 origin」——那是从代码反推的。
- 玩家这条观察直接给出了**喂料的实物**：换向后路点内部预留仍然在，于是 `origin.okay` 成立。
- 加上 §14.1 的"边走边铺"，症状可以确定是**同一条根因的另一种表现**：
  - 车**停着** → 早退后没人推进 → 完全卡死（前几轮的现场）；
  - 车**在动** → 早退后由 look-ahead 逐格推进 → 边走边铺（本场景）。
  两者都说明「重新规划整条路线」那一步没被执行。

### 14.3 机制映射（代码锚点）

- 换向后重新订路的入口在 `src/train_cmd.cpp` 的 TrainController 一段；紧邻 `TryPathReserve()` 之前先无条件把脚下站台/路点标成整格位已订：

```5499:5502:src/train_cmd.cpp
		if (moving_front_td != INVALID_TRACKDIR && IsRailStationTile(moving_front->tile)) SetRailStationPlatformReservation(moving_front->tile, TrackdirToExitdir(moving_front_td), true);
		if (TryPathReserve(consist, false, first_tile_okay)) {
			/* Do a look-ahead now in case our current tile was already a safe tile. */
			CheckNextTrainTile(moving_front);
```

- `:5499` 就是玩家看到的「路点内部预留没被销毁」的**持续供料点**：换向只反转方向、不动车节所在 tile，所以这行每 tick 都会把脚下的整格位重新标上；R3R 的记账清理（`ReserveTrackUnderConsist` / KI-203）也是按 tile 记的，tile 没变 ⇒ 不清。
- 紧接着 `:5500` 的 `TryPathReserve` 因为 `origin.okay` 成立而早退，**不规划**；
- `:5501` 那句原生注释正好描述本场景：「当前格本来就是安全位」——于是只做 `CheckNextTrainTile` 的 look-ahead，一格一格往前补 ⇒ **边走边铺**。
- 因此修法 1（可信性校验）在这个场景下的判据非常干净：让 `origin.okay` 不再被这块路点占位欺骗 ⇒ 进 `ChooseTrainTrack` ⇒ 一次规划到等待车底 ⇒ 长预留。

### 14.4 复测判据（本场景专用）

- R-1：换向的那一 tick（或其后 1~2 tick）必须出现 `CRT veh=<机车> … found=1`；只出现 `TRP … ok=1 res=1` 而无 `CRT`，即为未修复。
- R-2：同一 tick 的 `RP cur=` 序列应一次列到**等待车底最后一节**所在的格（长串），而不是逐格增长。
- R-3：等待车底所在格必须被本次预留覆盖（它就是安全停车位）；若只铺到路点出口或站台外格即止，说明规划仍被截断（转 §13.3 的修法 2）。
- R-4：换向后路点内部的整格占位仍会存在（`:5499` 每 tick 重标），但它**不得**再被当成"已有路线"的证据——即它存在与"是否规划"必须解耦。
- R-5（负判据）：真正停在站台上正常等待的列车（vehicle 站台 WAIT_COUPLE）不得因为修法 1 而反复寻路重启。

---

## 十五、疑点 D-1：旧 exe 同样「脚下有占位」，为什么它就能一次铺到终点？（2026-10-27 玩家提出）

### 15.1 玩家的反驳（原话要点）

> 路点内的预留从机车开始执行前往路点命令开始就有了，确认是**机车自己铺设**的。
> 但是，之前也是「因为车脚下有格子被占着，就认为它已经有路可走」，为什么之前就可以？这是值得考虑的。

⇒ 这条直接打在 §13.2/§14.2 的因果链上：如果「脚下有占位 ⇒ `origin.okay` 成立 ⇒ 早退不规划」，那**旧 exe 也应该早退**。旧 exe 没有早退、反而一次铺到等待车底，说明**早退的触发条件在旧/新之间发生了变化**，不能只归因于"有占位"。

⇒ **必须撤回/收窄此前表述**：已有占位**只是必要条件**，不是充分条件。真正要看的是 `origin` 到底怎么被算出来的、`origin.okay` 逐项条件是什么。

### 15.2 顺带自查：§14.3 有一处推断可能不成立（待核）

§14.3 说「`:5499` 每 tick 重标路点整格位」。但 `:5499` 的守卫是 `IsRailStationTile(moving_front->tile)`，**路点是否算 `IsRailStationTile` 尚未核实**（OTTD 系里 waypoint 与 station 共用 `TileType::Station`，但 `IsRailStationTile` 是否放行 waypoint 需看定义）。若路点不算，则 §14.3 的"持续供料点"就找错了，路点整格位另有设置点（KI-207c 已知：`Train::ReserveTrackUnderConsist()` 会订路点整格位）。⇒ 已登记为待核 U-8。

### 15.3 猜测根因（待验证，一律未实证）

按"旧 exe 能过、新 exe 不能过"，差异必然落在下面某一处，且都不再是"有没有占位"：

- **G-1 所有者归属不同**：`origin` 是沿预留回溯找到的"起点/主人"。旧 exe 时路点格的整格位不算作本机车所有（例如算作等待车底 actor，或无人），回溯在本车预留与路点格之间断开 ⇒ `origin.okay` 不成立 ⇒ 进 `ChooseTrainTrack` 一次规划到底。R3R 后来（第 125/126/127 轮）改了整格位归属/审计/清扫，可能把它算到了本车名下。
- **G-2 连续性不同**：`origin.okay` 要求从 `origin.tile` 到车头这一整段**每一格**都是本车预留。旧 exe 在路点那一格订的是**整格位**而前面几格是 plain rail 位，两者在"轨道位匹配"上可能不连续 ⇒ 回溯在断口停下。新 exe 若断口被补上（或整齐划一），回溯就能一路走到路点 ⇒ okay。
- **G-3 `first_tile_okay` 不同**：早退条件是 `origin.okay && (moving_front->tile != origin.tile || first_tile_okay)`。换向后车头恰在路点格内，若 `first_tile_okay` 为假，即使 origin 已 okay 也不会早退。旧 exe 的 `first_tile_okay` 计算（含车库口/隧道口/站台特判）与现在不同 ⇒ 结果不同。
- **G-4 时序不同（与"新旧"无关，与"走法"有关）**：旧 exe 在该场景下车是**一次规划到路点**，进路点时预留已覆盖路点且**没有越过路点的路线**；新 exe 因为边走边铺，进路点时预留状态与旧 exe 不同（可能多订了或少了断口）。
- **G-5 「路点内预留」的语义不同**：玩家看到的"路点内有预留"可能只是**整格位**（PBS 的 station/waypoint 整格位，`_m[t].m2` bit 8-11 的 station 版本），而 `origin` 回溯看的是 **track 级**预留位；整格位在，track 级未必在 ⇒ 两种"有占位"看着一样、对 `origin` 的意义完全不同。

### 15.4 待验证判据（按顺序取数）

- U-6：读 `TrainController` 里 `origin` 的构造代码，逐条列出 `origin.okay` 为真的**全部**条件（含 `origin.tile` 怎么来、`first_tile_okay` 怎么算）。★ 这是回答本题的唯一硬证据，必须拿到行号。
- U-7：在同一 tick 里打印 `origin.tile` / `origin.okay` / `moving_front->tile` / `first_tile_okay` / 路点格 track 级预留位与整格位两个读数（V-7 探针扩项），用于对比"旧 exe 会不会同样 okay"。旧 exe 已无法加探针 ⇒ 改用**推理**：只要找出新 exe 里恰好使 `origin.okay` 为真的那一项，且证明旧 exe 该项不成立（或该项由 R3R 后期轮次引入），即完成论证。
- U-8：核实 `IsRailStationTile()` 是否包含路点；据此订正 §14.3。
- D-1 结论口径：本题的答案必须能回答"是哪一项让旧 exe 的 `origin.okay` 为假"。若找不出，则说明早退假设本身不成立，须另找"新 exe 为何只边走边铺"的机制。

---

## 十六、D-1 的答案：早退是无辜的，新旧差异在「早退那一刻手上有没有长预留」（2026-10-27 实读）

### 16.1 `origin.okay` 到底是什么（硬证据）

`origin = FollowTrainReservation(consist, &other_train)`（`src/train_cmd.cpp:18502`），而 ok 的定义只有一行：

```1396:1396:src/pbs.cpp
	ftoti.res.okay = flags.Test(FollowTrainReservationFlag::OkayUnused) ? false : IsSafeWaitingPosition(v, ftoti.res.tile, ftoti.res.trackdir, true, _settings_game.pf.forbid_90_deg, true);
```

⇒ **`okay` 与"脚下有没有被占"无关**，它只回答一件事：**沿预留一路走到的尽头那一格，是不是安全等待位。** `ftoti.res` 是 `FollowReservation()` 沿预留前进后停下的位置。

同时起点也不一定是脚下：

```1374:1390:src/pbs.cpp
	if (!flags.Test(FollowTrainReservationFlag::IgnoreLookahead) && _settings_game.vehicle.train_braking_model == TBM_REALISTIC && v->lookahead != nullptr) {
		tile = v->lookahead->reservation_end_tile;
		trackdir = v->lookahead->reservation_end_trackdir;
		...
	} else {
		const Train *moving_front = v->GetMovingFront();

		tile = moving_front->tile;
		trackdir = moving_front->GetVehicleTrackdir();
	}
```

⇒ 有有效 lookahead：从**行程单尽头**开始回溯；没有/无效：从**车脚下**开始。`R3RTryPathReserveCore()` 开头 `ClearLookAheadIfInvalid(consist)`（`:18331`）负责把过期的清掉。

早退条件（`:18379`）：

```18378:18379:src/train_cmd.cpp
	/* If we have a reserved path and the path ends at a safe tile, we are finished already. */
	if (origin.okay && (moving_front->tile != origin.tile || first_tile_okay)) {
```

### 16.2 因此：「早退」不是 bug，它是正常优化

早退的真实语义是：**"我手上已有的预留，够我开到下一个安全停车位了，这一 tick 不需要再规划。"**

它成立与否只描述**手上预留的长度**，不描述"能不能继续往前开"。于是新旧行为可以完全一致地触发早退，却得到相反结果：

| | 换向后早退那一刻，手上的预留 | 早退之后 |
|---|---|---|
| 旧 exe | **一次规划到等待车底**的长预留（尽头 = 等待车底所在安全位） | 沿长预留一路开过去，途中每 tick 都"已经够了"⇒ 表现为玩家看到的"一次铺到安全停车位" |
| 新 exe | 只有路点内几格（尽头 = 脚下或相邻格，本身就是安全位 `first_tile_okay=1`） | 只能走一格；走完这格再补一格 ⇒ 表现为"边走边铺" |

⇒ **我在 §13/§14 把早退当病根是错的，需要撤回。**"脚下有占位"只是让早退**有条件成立**，它既不是新旧差异，也不是病根。玩家这一问正好戳破了那层因果。

### 16.3 真正的新旧差异：换向后那一次「完整规划」有没有发生

- 旧 exe：换向后某处拿到过一次真正的 `ChooseTrainTrack` 规划，预留一次铺到等待车底，长预留从此存在；
- 新 exe：换向后没有拿到这次规划（或拿到的规划被 R3R 的占位/门禁截断），于是从来没有长预留。

⇒ 之后就是正反馈环，且**自己锁死自己**：

```
预留只有脚下几格 → lookahead 无效被清 → 从脚下开始回溯
→ 尽头＝脚下（路点格），本身是安全位 → okay=1 且 first_tile_okay=1
→ 早退，不规划 → 预留还是只有脚下几格 → …
```

这个环解释了为什么症状稳定复现、且**只靠"再等几 tick"永远不会自愈**。

### 16.4 修法 1 的判据需要改写（按本节结论）

- **不要废除早退**（它是正常优化，废掉会让全图每 tick 重新规划，性能与副作用都不可接受）。
- 需要的是：**在"手上预留的可用长度为 0"这一刻强制规划一次**。可操作判据：`origin.tile == moving_front->tile`（回溯原地未动，等于没有可用前路）+ 该车处于运行态（`cur_speed != 0`）或处于"刚换向/订单未完成"事件窗口 ⇒ 忽略早退，落到 `ChooseTrainTrack()`。
- 对"真正停在站台/安全位等待"的车：`cur_speed == 0` ⇒ 不进强制分支，行为逐字不变（这也是不误伤 R-5 的保障）。
- 比原方案额外好处：判据是**几何自证的**（尽头就在脚下），不依赖"谁是预留主人""这格算不算孤立预留"这些容易出错的归属判断。

### 16.5 待验证（新增）

- U-9：用现成的 `TPR-EXIT` 行（`:18537`，已含 `origin=x,y okay=`）在换向后连看 5~10 tick，确认 `origin` 是否**始终等于车头 tverde 格**、`okay=1`。若是，正反馈环成立、修法判据可直接用 `origin.tile == moving_front->tile`；若 origin 会跳远，则要再查 `FollowReservation` 被什么截断。
- U-10：`IsSafeWaitingPosition()` 对**路点格**是否恒为真（决定"脚下即安全位"是否在本场景必然成立）。若路点恒真，则本场景的早退是必然而非巧合。

---

## 十七、修法 1 落地 v1（2026-10-27，玩家批准「按你推荐的修法试试」）

### 17.1 实际改动点（实读行号）

早退块在 `R3RTryPathReserveCore()` 内，行号与 §16 的引用一致：

```18646:18659:src/train_cmd.cpp
	/* If we have a reserved path and the path ends at a safe tile, we are finished already. */
	if (origin.okay && (moving_front->tile != origin.tile || first_tile_okay)) {
		/* Can't be stuck then. */
		if (consist->flags.Test(VehicleRailFlag::Stuck)) SetWindowWidgetDirty(WindowClass::VehicleView, consist->index, WID_VV_START_STOP);
		consist->flags.Reset(VehicleRailFlag::Stuck);
		if (_settings_game.vehicle.train_braking_model == TBM_REALISTIC) {
			FillTrainReservationLookAhead(consist);
			TryLongReserveChooseTrainTrackFromReservationEnd(consist, true);
		}
		/* R3R (KI-179): the path is reserved, so the one-shot ability is used up. */
		r3r_one_shot.reserved = true;
		tpr_note(0, other_train, tpr_pair_waived);
		return TPRRF_RESERVATION_OK;
	}
```

紧跟其后（跳过即落到）是真正规划的那条路：`:18662` depot 试探 → `:18667` `exitdir = TrackdirToExitdir(origin.trackdir)` → `:18710` `ChooseTrainTrack(consist, new_tile, exitdir, reachable, CTTF_FORCE_RES|...)`。

⇒ **跳过早退不需要新写任何规划代码**，原生那条路一直都在，只是被早退挡在门外。

### 17.2 判据（v1）

```cpp
const bool r3r_zero_reach = (origin.tile == moving_front->tile);      // 回溯原地未动 = 零可用前路
const bool r3r_force_replan = r3r_zero_reach && (consist->cur_speed != 0)
		&& !consist->current_order.IsType(OT_LOADING);
if (!r3r_force_replan && origin.okay && (moving_front->tile != origin.tile || first_tile_okay)) { ... }
```

- **`origin.tile == moving_front->tile`**：几何自证「手上没有可用前路」，与"谁是预留主人""这算不算孤立预留"全部解耦（§16.4 的优点）。
- **`cur_speed != 0`**：只抓"边走边铺"形态——车在跑却零预留，必然只能逐格补。**真正停稳在安全位的车不进这条**，这是 R-5 负判据的保障。
- **`!OT_LOADING`**：站台装卸中的车再加一道，绝不被强制重规划。
- 三条同时成立才跳过早退，因此**停站/等待/装卸行为逐字不变**。

### 17.3 附带探针

新边沿枚举 `R3REDGE_TPRZERO`（插在 `R3REDGE_STOPGATE` 之后、`R3REDGE_COUNT` 之前，`:8487-8488` 之间；该枚举在 `train_cmd.cpp` 文件内定义 ⇒ 不动任何 `src/*.h`，增量合法）：

```
TPR-ZERO-REACH veh= mf=x,y origin=x,y td= okay= spd= real= order= look=
```

边沿 key=车号、payload=origin tile ⇒ 同一站位的重复只留一行，用来判定"是否真的走到过一次零预留 + 在动"。

### 17.4 预期效果（对应 R-1/R-2/R-3）

换向/行进中第一次满足判据的 tick：不再 `return TPRRF_RESERVATION_OK`，而是落到 `ChooseTrainTrack()`，`new_tile` 取 `origin.tile` 沿车头方向的下一格 ⇒ 一次规划到等待车底（安全位）⇒ `RP cur=` 出现长串、`CRT veh=… found=1`。之后预留变长，早退自然恢复为正常优化（这正是旧 exe 的形态）。

### 17.5 本轮**明知不覆盖**的情形（留第二步）

- **车完全静止**（`cur_speed == 0`）且零预留、订单是 `GOTO_*`：本判据刻意不启用（怕误伤停站），所以"停着卡死"那一类现场本轮不动。若复测发现它也需要，第二步再加"`VehicleRailFlag::Stuck` 为真或等待计数器超阈值 ⇒ 同样强制规划一次"，而不是放宽成无条件。
- **U-11（待验证）**：换向那一瞬间 `cur_speed` 是否为 0。若为 0，则首次规划推迟到车重新起动的那一 tick；若车因零预留根本不启动，则本轮抓不到，需要转到第二步的事件式判据（在换向提交点置一次性标志）。
- 未验证：`origin.tile == moving_front->tile` 是否还会在其它健康场景（进站减速、信号前停车）出现且 `cur_speed != 0`；若出现，`ChooseTrainTrack` 会重订一次，可能产生多余预留，须靠复测日志确认无"幽灵预留"回潮。

### 17.6 落地与构建自证（2026-10-27）

- 改动文件：`src/train_cmd.cpp` 两处（均不涉及任何 `src/*.h`，增量合规）：
  1. `R3REDGE` 枚举新增 `R3REDGE_TPRZERO`（插在 `R3REDGE_STOPGATE` 之后、`R3REDGE_COUNT` 之前）；
  2. `R3RTryPathReserveCore()` 早退条件处新增 `r3r_zero_reach` / `r3r_force_replan` 判据、`if (r3r_force_replan)` 探针块，早退条件改为 `if (!r3r_force_replan && origin.okay && (...))`。
- `read_lints(src/train_cmd.cpp)` = **0 条**。
- 构建：复用既有 `_tmp_inc_build.cmd`（未新建任何 .cmd）；`build\R3R_incbuild.log` 仅 3 步、含 `[3/3] Linking CXX executable openttd.exe`、无 `error C*` / `FAILED:` / `build stopped`；`build\R3R_incbuild.done` = `EXIT_CODE=0`。
- 状态：**已实现 + 已编译**，游戏内复测待做。

### 17.7 复测方法（给玩家）

1. 用新 `build\openttd.exe` 复跑同一线路（车库 → 空股道 → 路点 → 换向 → 等待车底）。
2. 换向/行进后应在日志里看到 `TPR-ZERO-REACH veh=<机车> mf=x,y origin=x,y ... spd=<非0> look=0`。
3. 紧随其后应出现 `CRT veh=<机车> ... found=1` 与一长串 `RP cur=`（一次列到等待车底所在格）。
4. 负判据：站台正常装卸/等待挂接的车不得出现 `TPR-ZERO-REACH`（它们 `spd=0` 或 `order=17`/`OT_LOADING`）。
5. 若 `TPR-ZERO-REACH` 出现了但仍是逐格增长，说明规划又被下游截断 ⇒ 转 §13.3 修法 2，不要再动早退。

---

## 十八、第 338 轮修法 C 的**实机复测结果**：未触发，且逐格点不在早退（2026-10-27）

**输入**：玩家拖入 `D:\temp\codebuddy-dropped-files\432b4305-a74f-4325-a7f9-22d2173d2eb3\R3R_debug.log`，1747 行。玩家原话「预留还是逐格铺设」。

**取数方式（本环境限制）**：`search_content` 对该日志完全失效（连 `^` 都是 0 命中，疑似 rg 判为二进制；`build\` 又被 `.gitignore` 忽略）。可用的做法＝先 `Copy-Item` 到工作区根目录、再用 `Select-String ... | Set-Content` 落盘、最后 `read_file` 读结果。**后续取这个玩家的日志必须走这条路。**

### 18.1 判定 1：修法 C **一次都没触发**（`TPR-ZERO-REACH` = 0 条）

`Select-String 'TPR-ZERO-REACH|CRT veh=|REVERSEDIR|RP cur='` 只命中 14 行，其中零条 `TPR-ZERO-REACH`。⇒ 判据 `origin.tile == moving_front->tile && cur_speed != 0` 在整个会话里从未同时成立。

### 18.2 判定 2：为什么没触发 —— **U-11 成立：换向那一瞬间 `spd=0`**

```
1374:REVERSEDIR veh=57 tile=16,14 dir=5 order=16 spd=0 nv=3 rev=0 stuck=0 db=0
1382:TPR-EXIT veh=57 state=ok pair=0 tile=16,14 spd=0 other=57 otherTile=16,14 otherSpd=0 otherOrder=16 origin=16,14 okay=1
1383:TRP veh=57 mf=16,14 real=1 type=16 dest=1932 spd=0 fp=0 look=1 one=0 mstuck=0 fto=1 => ok=1 res=1
```

`origin=16,14 == mf=16,14`（零可用前路，§16.3 的正反馈环现场原样重现）且 `okay=1`，**但 `spd=0`** ⇒ 修法 C 的第二条件不成立 ⇒ 放它早退。这正是 §17.5 U-11 写下的可能情形，现在实测确认。

⇒ 修法 C 的判据要再改：不能依赖「车在动」，必须换成**事件式**（换向提交点置一次性标志）或「订单未到点 + 零预留」。

### 18.3 判定 3（新，且比早退更硬）：**逐格点的真凶是耦合寻路只铺了 4 格就失败**

同一 tick 里耦合寻路（CPL）已经找对目标（等待车底在 `5,15`），但预留铺设 `rp()` 只走到 `13,15` 就返回失败：

```
1460:CPL-PATHFOUND veh=57 found=1
1461:CPL-SCAN tick=12551 veh=57 found=1 closed=3 limit=10000 nodes=17 enter=14 noplat=5 nores=0 noconsist=6 consist=3 rej=0/0/0/0 acc=3/0 accwp=0 last=5,15 lastveh=0 why=acc-car
1462:CPL-HEAD best=5,15 head=5,15 on=1
1463:CPL-BEST veh=57 tile=5,15 td=0 cost=10242
1464:CPL-BACK pPrev=5,15 td=0 parent=15,15 ptd=2
1465:CPL-BACK pPrev=15,15 td=2 parent=16,14 ptd=0
1466:CPL-TRACE st=16,14 tdb=0x101
1467:REACH cur=15,14 td=3 depth=0 trk=3 bits=0x29
1468:REACH cur=15,15 td=2 depth=1 trk=2 bits=0x15
1469:REACH cur=14,15 td=0 depth=2 trk=0 bits=0x9
1470:REACH cur=13,15 td=0 depth=3 trk=0 bits=0x1
1471:CPL-FIRST-TD st=16,14 hops=2 yapf=3 at=15,14 tdb=0x2929 ok=1 best=3 start=15,14 trk=3
1472:CPL-TRACE-RESULT st=16,14 best=3
1473:RP cur=15,14 td=3 depth=0 trk=3 bits=0x29
1474:RP cur=15,15 td=2 depth=1 trk=2 bits=0x15
1475:RP cur=14,15 td=0 depth=2 trk=0 bits=0x9
1476:RP cur=13,15 td=0 depth=3 trk=0 bits=0x1
1477:CPL-RESERVE veh=57 reserved=1 next=3
1478:CPL-RET veh=57 tick=12551 tile=15,14 enterdir=0 tracks=0x9 res=0x0 ret=0x8 why=nores-final spd=5 real=1
1479:RESV-WATCH SET-RAIL tile=15,14 track=3 phase=controller actor=59 head=57
```

要点：
- `CPL-BACK` 的父链只有两跳（`16,14 → 15,15 → 5,15`），说明 yapf 在站台段**一步跳过多格**（TrackFollower 语义），而不是逐格；
- `REACH`/`RP` 却只有 4 格且停在 `13,15`，距离目标 `5,15` 还差 8 格；
- `ret=0x8` 且 `why=nores-final` ⇒ rp() 最终在**某格预订失败**后整体判负；
- 结果只订到 `15,14` 一格（`RESV-WATCH SET-RAIL tile=15,14 track=3`）。

⇒ 这就是玩家看到的"逐格铺设"：每 tick 只能订 1~4 格，且每次都以 `nores-final` 收场。**与早退无关**（早退在预留够长时才发生，而这里压根没订长）。

### 18.4 疑点 D-2（待验证，下一步唯一焦点）

`rp()` 为什么在 `13,15` 之后停下并 `ret=0x8 why=nores-final`？候选：

- **H-1 下一格 `12,15` 被别人占**：同一日志里 `veh=54` 停在/驶过 `12,15`（`1366:TPR-EXIT veh=54 ... tile=12,15`、`1369:TRP veh=54 mf=12,15`、`1367:R3R-RES-ONCE used head=54 ... tile=12,15`），若 `12,15` 仍有他人预留或停车，rp() 到那里必然订不动 ⇒ `nores-final`。
- **H-2 站台跳段没铺完**：`CPL-FIRST-TD ... tdb=0x2929 hops=2` 与 `CPL-BACK` 的"跳格"父链不一致，可能是 rp() 的站台补铺循环（第 240 轮 KI-346 修过的 `plat_track` 那一支，U-3）在跳段格上算错轨道，导致中间段压根没进 RP 列表。
- **H-3 `nores-final` 语义本身**：需读 `yapf_rail.cpp` 中 `CPL-RET` 的 `ret`/`why` 取值表，确认 `0x8` 是哪一位、`nores-final` 在哪个 return 点。

**下一步（仅一组取证）**：读 `src\pathfinder\yapf\yapf_rail.cpp` 的 `rp()` 与 `CPL-RET`/`nores-final` 打印点，逐条列出会 `return` 失败的条件，判定 H-1/H-2/H-3 哪一条成立。

### 18.5 顺带记录（本日志其它读数）

- `1739:CRT veh=57 order=1 dir=5 origin=14,15 td=8 found=1` + `1740:TPR-EXIT ... origin=35,12 okay=1` ⇒ 机车已挂上车底（`1721:SEGTR-SNAP tag=couple head=57 nseg=3`）、订单已切到 `type=1`（GOTO_STATION），此时 `origin=35,12` 已跳很远（长预留存在），**early-exit 正常发挥作用**，与 §16 的结论一致。
- `1730:SPD-DROP veh=57 tile=12,15 prev=30 ... dt=264`：挂接后停在新位置，属正常提交点。
- `1728/1729/1747:AUTO-ORD-DROP kind=station dest=0 ... reason=not-in-source-range`：第 264 轮续 C-2 的实现按预期在丢弃越界自动填充项，非缺陷。
- U-8 已解一半：`IsRailStationTile()` 只认 `StationType::Rail`（`station_map.h:103-106`），**路点不算** ⇒ §14.3 把 `:5499` 当成"路点整格位持续供料点"是**错的**，路点整格位来自 `Train::ReserveTrackUnderConsist()`（KI-207c）。§14.3 已按此失效，仅保留"这段代码是站台场景的同类现场"这一用途。

## 十九、第 339 轮：`rp()` 语义澄清 + H-1 成立（12,15 被 veh=54 占）（2026-10-27）

### 19.1 读码结论：`rp()` 不订任何东西，它只算「订到哪一格」

`src\pathfinder\yapf\yapf_rail.cpp`（`CYapfFollowRailT::rp()` 与随后三行）：

- `rp()` 全程只 `TrackFollower::Follow()` 走路径、更新 `rp_last_tile`，**不调用任何 Reserve**（`:1189-1228`，第 330 轮注释自陈）。
- 走完后：`SetReservationTarget(pNode, rp_last_tile, pNode->GetLastTrackdir())`（`:1227`）——**预留目标被设成 `rp_last_tile`，不是 yapf 终点**。
- 真正订路是 `bool reserved = this->TryReservePath(nullptr, pNode->GetLastTile(), true);`（`:1236`）。
- 因此 `1477:CPL-RESERVE veh=57 reserved=1 next=3` 的真实含义＝「订到 `rp_last_tile`（=13,15）成功」，而不是「订到目标 5,15」。

⇒ 逐格/短预留的直接原因就是 `rp_last_tile` 停在 `13,15`。

### 19.2 H-1 成立：`12,15` 被 veh=54 挡住

- `13,15` 的 `bits=0x1` 只有 `TRACK_X`，唯一后继格是 `12,15`；
- `12,15` 当时被 `veh=54` 占用：`1366:TPR-EXIT veh=54 state=ok pair=0 tile=12,15 spd=0 ... origin=18,16 okay=1`、`1369:TRP veh=54 mf=12,15`；
- `rp()` 在 `13,15` 处 `f3.Follow(13,15,TRACK_X)` 到 `12,15` 失败（`CanEnterNewTile()` 剔除他人预留/占用且不豁免自己）⇒ `return false`，`rp_last_tile` 保持 `13,15`（`:1191` 只对非站台格赋值）⇒ 预算目标被压到 `13,15` ⇒ 只订 4 格。
- `veh=54` 随后起步（`spd` 0→2→60）离开 `12,15`，`57` 的 `next` 从 3 变 2 继续推进，说明这是**前车占道**，不是自锁。

### 19.3 `CPL-RET ... res=0x0 ret=0x8 why=nores-final` 是探针噪声，不是失败原因

- `res = GetReservedTrackbits(tile) & DiagdirReachesTracks(enterdir)`，`tile=15,14`、`enterdir=0`（`DIR_NE`）可达轨道＝`UPPER|RIGHT = 0x24`，而该格实订的是 `track=3`（`TRACK_LOWER = 0x8`）⇒ 与 `0x24` 无交集 ⇒ `res=0x0`。**预留本身存在**（`1479:RESV-WATCH SET-RAIL tile=15,14 track=3`）。
- `why=nores-final` 只对应 `!do_track_reservation` 的返回点，是**另一次不请求预留的调用**（同一 tick 的 `1444:RESV-EXT-PRE veh=57 tile=15,14 ... doRes=0` 对得上），与 CPL 的 `reserved=1` 是两次不同调用，不能并列解读。

⇒ 18.3 里「rp() 最终在某格预订失败后整体判负」的措辞作废：rp() 从不预订；`ret=0x8/why=nores-final` 也不是 CPL 失败码。

### 19.4 修法 C 零触发已复核

`Select-String 'TPR-ZERO-REACH'` 对整份 1747 行日志 **0 命中**（`Set-Content` 因无输入未生成文件，即为空集）。与 18.2 一致：换向提交点 `REVERSEDIR veh=57 ... spd=0`，`cur_speed != 0` 恒假。

### 19.5 本轮结论

本份日志里「预留短」的直接成因是**前车 `veh=54` 停在 `12,15`**（物理上不可能把预留订进别人占着的格），属于 PBS 的正常阻塞；`54` 一走，`57` 立刻恢复推进。**不是**修法 C 想解决的「零可用前路自锁」现场，也没有新的 R3R 缺陷证据。

⇒ 下一步只能二选一，需玩家确认现场：
1. 若玩家看到的「逐格」就发生在 `12,15` 被占的那几秒 ⇒ 属正常阻塞，无需改码（可只把探针说明写清楚）；
2. 若玩家在**没有任何前车**的空线上也看到逐格 ⇒ 需要按 §18.2 改成事件式判据（换向/零预留后 N tick 内强制重规划），并另取一份空线日志作证。

### 19.6 待确认

- **U-12**：`veh=54` 在 `12,15` 静止的时长与原因（它的 `origin=18,16 okay=1` 说明它自己有长预留，像是在等信号/避让）。若它是「被 `57` 的短预留反过来挡住的」则构成互相等待，需按 §19.5 第 2 条处理。

## 二十、第 340 轮：玩家新观察「路点掉头的车几乎都不铺连续预留」（2026-10-27，先登记、后取证）

### 20.1 玩家原话（原文保留）

> 根据我再次测试的肉眼观察，我发现几乎在路点掉头的列车都不会发出连续预留到安全停车位，包括一个执行了前往车库挂接的列车

### 20.2 这条观察推翻了 §19.5 的「无需改码」分支

§19.5 曾把结论收在「本份日志的短预留 = 前车占道，可能是正常阻塞」。玩家这次给的是**跨场景的普遍性陈述**：只要在路点掉头，就看不到一条连续到安全位的预留；而且**车库挂接**（GOTO_COUPLE 指向车库里的等待车底）也一样。⇒ 这不再是「某一格被别人占」的个案，而是**换向/掉头提交点之后从不重铺**的系统性问题，与第 337 轮登记的 **R337-G2**（掉头后走 `ExtendTrainReservation` 续订、从不按新方向重铺）是同一个面。

### 20.3 猜测根因（待验证，先落盘避免重复取证）

- **G2-A（主推）**：`ReverseTrainDirection()` 尾部那次 `TryPathReserve(...)` 把「路径已找到」当参数传进来（耦合提交点注释里已记过它尾部确有 `TryPathReserve`），于是 `R3RTryPathReserveCore()` 直接走 `lookahead` / `path_found` 分支 ⇒ 落到 `ExtendTrainReservation()`，沿**掉头前的旧预留终点**续订，而旧终点在新方向上位于车尾之后 ⇒ 每次只补 0~2 格，永远不调用 `ChooseTrainTrack()` 重铺。而修法 C 的判据要求 `cur_speed != 0`，掉头提交点 `spd=0`（§19.4 已实测）⇒ 修法 C 完全绕开这条路。
- **G2-B**：掉头提交点没有清 `PathReservationEnd`/`reservation_end_tile`，`origin.tile` 仍是旧方向那一头，`okay=1`（因为旧终点原本就是安全位），于是早退与 extend 双双成立。
- **G2-C（车库挂接面）**：`GOTO_COUPLE` 指向车库车底时走另一条入口（`CPL-DEPOT-SKIP` 相关分支），如果它同样带「路径已找到」或 `lookahead`，则同一机制；若它是 `dont_reserve`/零预留入口，则是另一条独立缺陷，需分开判。

### 20.4 验证所需数据（下一份日志要能区分 G2-A / G2-B / G2-C）

对**任一**在路点掉头的列车（越简单越好，空线、无前车），抓掉头那一 tick 前后 ~5 tick 的：

1. `REVERSEDIR veh=... tile= dir= order= spd= ...`（换向提交点）
2. 紧随的 `TPR-EXIT veh=... origin=x,y okay=1 ...` 与 `TRP veh=... mf=x,y ... res=1` —— 关键看 **`origin.tile` 是否等于掉头前的旧终点、`look=` 是否为 0**
3. 是否出现 `RESV-EXT-PRE veh=... tile=x,y ... doRes=1`（=走了 extend 分支）**而同时没有** `CRT veh=... found=1`（=没走 ChooseTrainTrack）
4. 若出现 `CRT`，看它算出的 `origin` 是不是新方向前方；若压根不出现 `CRT` ⇒ G2-A/G2-B 成立
5. 车库挂接那条：额外要 `CPL-ENTRY` / `CPL-ORIGIN` / `CPL-DEPOT-SKIP` / `CPL-RESERVE` / `CPL-RET` 五行

判读表：

| 现象 | 判定 |
|---|---|
| 掉头后 `RESV-EXT-PRE doRes=1` 且无 `CRT`，预留端点仍指向旧方向 | G2-A 成立（续订代替重铺） |
| 掉头后 `TPR-EXIT origin=` == 旧终点 且 `look=0`、`okay=1` 直接早退 | G2-B 成立（旧终点未失效） |
| 车库挂接出现 `CPL-RESERVE reserved=1 next=0` 或 `next` 恒为 1 | G2-C 成立（车库入口独立缺陷） |

### 20.5 与修法 C 的关系（口径）

修法 C 的判据是 `零可用前路 && cur_speed != 0`，本现象发生在**换向提交点（spd=0）与续订分支**，两条路都不相交 ⇒ **继续调修法 C 的判据不会改善这条现象**。正确改法方向：在换向/掉头提交点**主动作废旧预留终点**（清 `PathReservationEnd` 或强制 `TryPathReserve(false)` 走重铺），而不是指望早退判据。此方向需先由 §20.4 的数据坐实 G2-A 还是 G2-B。

### 20.6 静态读码结果：G2-A / G2-B 均不成立，真因 = G2-D（2026-10-27，无需日志即可判定）

逐条读码（行号=当前工作树），四个事实串起来就足够定案：

1. **`FreeTrainTrackReservation()` 自带 `lookahead.reset()`** —— `src/train_cmd.cpp:15804`：
   ```15804:15804:src/train_cmd.cpp
   	if (origin == INVALID_TILE) consist->lookahead.reset();
   ```
   而换向提交点 `:5322` 正是**无参**调用 `FreeTrainTrackReservation(consist)` ⇒ 掉头后 `lookahead == nullptr`。
   ⇒ **G2-B（旧 `reservation_end_tile` 残留）当场证伪**；也说明 `src/pbs.cpp:1374` 那个「有 lookahead 时起点取 `reservation_end_tile`」的分支**进不去**，`FollowTrainReservation()` 走 `:1385-1390` 分支：起点恒为 `moving_front->tile` + 车头 trackdir。

2. **清完预留后回溯长度恒为 0**：`src/pbs.cpp:1395-1396` 从车头脚下 `FollowReservation()`，没有预留就原地停 ⇒ `res.tile == moving_front->tile`；`okay = IsSafeWaitingPosition(脚下格, ...)`，而路点/站台格本身就是安全位 ⇒ **`origin.okay == 1`**。
   ⇒ **G2-A（沿旧终点续订）也证伪**：旧终点已经被 `lookahead.reset()` 抹掉，不存在「沿旧终点每次续 0~2 格」的通道。

3. **早退判据退化成只剩 `first_tile_okay` 一个变量** —— `src/train_cmd.cpp:18655-18672`：
   ```18655:18672:src/train_cmd.cpp
   	const bool r3r_zero_reach = (origin.tile == moving_front->tile);
   	const bool r3r_force_replan = r3r_zero_reach && (consist->cur_speed != 0) && !consist->current_order.IsType(OT_LOADING);
   	...
   	if (!r3r_force_replan && origin.okay && (moving_front->tile != origin.tile || first_tile_okay)) {
   ```
   代入事实 2：`origin.tile == moving_front->tile`（`r3r_zero_reach=1`）、`origin.okay=1` ⇒ 括号里只剩 `first_tile_okay`。
   而 `:5491`：
   ```5491:5491:src/train_cmd.cpp
   	bool first_tile_okay = moving_front_td == INVALID_TRACKDIR || !HasBlockSignalOnTrackdir(moving_front->tile, moving_front_td);
   ```
   **路点格上没有 block signal ⇒ `first_tile_okay = true`** ⇒ 早退命中，`return TPRRF_RESERVATION_OK`——注意这是**假成功**：它一个轨道位都没订。

4. **早退分支本身也几乎不铺路**：`:18676-18679` 只在 `TBM_REALISTIC` 才 `FillTrainReservationLookAhead + TryLongReserveChooseTrainTrackFromReservationEnd`；非真实制动模型下什么都不做。
   紧随其后的补救 `CheckNextTrainTile()`（`:5502` 调用，定义 `:15281-15345`）**只在「下一格恰好有 PBS 信号」时才 `ChooseTrainTrack`**：
   ```15327:15338:src/train_cmd.cpp
   	if (!HasReservedTracks(ft.new_tile, TrackdirBitsToTrackBits(ft.new_td_bits))) {
   		/* Next tile is not reserved. */
   		if (KillFirstBit(ft.new_td_bits) == TRACKDIR_BIT_NONE) {
   			Trackdir td = FindFirstTrackdir(ft.new_td_bits);
   			if (HasPbsSignalOnTrackdir(ft.new_tile, td) && !IsNoEntrySignal(ft.new_tile, TrackToTrackdir(td))) {
   				...
   				ChooseTrainTrack(consist, ft.new_tile, ft.exitdir, tracks, CTTF_NONE);
   ```
   掉头点出口的下一格通常没有信号 ⇒ 一格都不订。

**结论句（G2-D，取代 G2-A/B）**：掉头/换向提交点把 `first_tile_okay` 当"已有前路"传给 `TryPathReserve()`，但 `first_tile_okay` 只证明"**脚下这一格**是安全位"；此时预留刚被 `FreeTrainTrackReservation()` 清空，新方向上可用前路长度=0 ⇒ 走早退、零预留。
玩家看到的「逐格铺设」= `ReserveTrackUnderConsist()`（KI-207c）每 tick 订脚下那几格，随车前进而前进，视觉上就是边走边铺。
而修法 C 的 `r3r_force_replan` 正是为这个矛盾准备的，却被自己的 `cur_speed != 0` 前提挡死——换向提交点几乎恒 `spd=0`（§19.4 已实测 `REVERSEDIR veh=57 ... spd=0`）⇒ **唯一能救的分支被速度闸门关掉**。车库里那一例（"前往车库挂接"）也是同一个提交点，不是独立缺陷（G2-C 暂不成立，等日志）。

### 20.7 修法 D（待落地）：把「换向」从 `cur_speed` 判据里分离

- 不动 `first_tile_okay` 的全局语义（它保护的是「停在信号前不该被 MarkTrainAsStuck」与「装卸中不铺路」两条上游行为，改它会波及全图列车）。
- 只在**换向提交点**给一次「本次必定强制重铺」的信号，让 `r3r_force_replan` 的第二个合取项成立：
  `r3r_force_replan = r3r_zero_reach && !OT_LOADING && (cur_speed != 0 || <本次是换向后第一次订路>)`。
- 传递方式（避免改 `src/*.h` 触发 KI-183 全量）：`train_cmd.cpp` 内新增文件作用域的一次性标记（按 `VehicleID` 键，consume 即清），在 `:5500` 那次 `TryPathReserve()` 之前置位。
- 副作用与边界：强制重铺后 `ChooseTrainTrack()` 仍受信号约束；若前方确实是红灯/被占，`TryPathReserve` 返回 false，`else if (order != OT_LOADING) MarkTrainAsStuck()` 与既有失败路径一致（这正是上游"等信号"语义）。

### 20.8 修法 D 落地（2026-10-27，仅 `src/train_cmd.cpp`，未碰 `src/*.h`）

1. 新增一次性标记（插在 `ReverseTrainDirection()` 之前，紧跟既有的 `R3RMarkChainJustReversed()` 前置声明，约 `:5236-5268`）：
   ```5236:5268:src/train_cmd.cpp
   static btree::btree_set<VehicleID> _r3r_force_replan;
   static void R3RMarkForceReplan(const Train *consist) { _r3r_force_replan.insert(consist->index); }
   static bool R3RConsumeForceReplan(const Train *consist) { return _r3r_force_replan.erase(consist->index) > 0; }
   ```
   按 `VehicleID` 键，`btree_set` 与既有的 `_r3r_just_reversed`（`:12382`）同款，头文件已在用，**不需新增 include**。
2. 换向提交点 `:5499` 之后、`TryPathReserve()` 之前置位（`ReverseTrainDirection()` 内）：
   ```5499:5506:src/train_cmd.cpp
   		R3RMarkForceReplan(consist);
   		if (TryPathReserve(consist, false, first_tile_okay)) {
   ```
3. 核心里消费（`R3RTryPathReserveCore()` 入口、`ClearLookAheadIfInvalid()` 之后）：
   ```18332:18336:src/train_cmd.cpp
   	const bool r3r_pending_replan = R3RConsumeForceReplan(consist);
   ```
   **放在入口而不是早退前**：早退前还有「被别的列车挡住」等多处 `return`，只在入口消费才能保证"一次性"不被前一次失败吞掉或被下一次调用误用。
4. 早退判据接入（`:18663-18666`）：
   ```18663:18666:src/train_cmd.cpp
   	const bool r3r_force_replan = r3r_zero_reach && !consist->current_order.IsType(OT_LOADING) &&
   			(consist->cur_speed != 0 || r3r_pending_replan);
   ```
   即：**换向后这一次，即使 `cur_speed == 0` 也强制走 `ChooseTrainTrack()` 重铺**；`OT_LOADING` 豁免保留（上游"装卸中不铺路"语义不动）。
5. 探针：`TPR-ZERO-REACH` 行尾追加 `turn=%d`（是否由换向触发），用于在玩家日志里直接证明修法 D 命中。

### 20.9 构建自证（第 340 轮续）

- 复用既有 `_tmp_inc_build.cmd`（**未新建任何 .cmd**）。
- 护栏：`GUARD: incremental is safe (no header/lang file is newer than the newest object)`。
- `build\R3R_incbuild.done` = `EXIT_CODE=0`（2026-10-27 21:34:09）。
- 日志尾部：`[2/3] Building CXX object ...train_cmd.cpp.obj` → `[3/3] Linking CXX executable openttd.exe`；`error C` / `fatal error` / `FAILED:` / `build stopped` 计数 **0**。
- 时间戳链：`src\train_cmd.cpp`（本轮编辑）→ `train_cmd.cpp.obj` @21:31:45 → `build\openttd.exe` @21:34:06（52 322 304 B）。
- `read_lints` `src\train_cmd.cpp` = 0 条。
- 坑记录：本轮第一次用 `Start-Process -FilePath 'cmd.exe' -ArgumentList '/c','_tmp_inc_build.cmd' -WorkingDirectory ...` **没有真正执行**（`done`/`exe` 仍是 20:55 的旧文件，且无 `ninja`/`cl` 进程）；改用 `cmd /c _tmp_inc_build.cmd` 直接调用才跑起来。⇒ 判据必须看**时间戳是否本轮**，只看文件存在会误判成"已构建"。

### 20.10 待复测判据（修法 D）

1. 空线路点掉头：掉头后应出现一条**连续到下一处安全位**的预留（而不是只有脚下几格）。
2. 同 tick 日志：`TPR-ZERO-REACH ... spd=0 turn=1`（`turn=1` 即修法 D 命中），随后应出现 `CRT veh=<该车> found=1` 与 `RESV-WATCH SET-RAIL ...`；不再出现"只有早退、无 CRT"。
3. 「前往车库挂接」那一例同判：若它在路点掉过头，应同样出现 `turn=1`。
4. 负判据：正常行驶中的列车不得出现多余的 `TPR-ZERO-REACH`（`spd!=0` 的既有修法 C 行保持原样）；装卸中（`OT_LOADING`）在站台换向不出现强制重铺。
5. 负判据：前方确是红灯/被占时，掉头后仍不越权——`MarkTrainAsStuck` 与上游"等信号"表现一致，且车不会穿进被占格。

## 22. 疑点登记（2026-10-27，玩家质询）：`FSCP tile=13,15 best=54` 的来源不成立？

**玩家原话**：「13,15 那里怎么会有预留的？54 压根没有往那一格铺设预留」

**期望值**：`13,15` 上不存在 veh=54 的预留（54 的 `CRT` 路径目标是从 `origin=16,15` 到 `2,15`，走的是 16,15→15,15→14,15 一带；`13,15` 位于 `y=15` 行、x=13，按西行进方向看确实在通道上，但需证）。

**实测值（日志锚点）**：
- `build\R3R_debug.log:1348` `FSCP tile=13,15 fail=notSeg best=54 ord=6 sf=1`
- 同一批：`:1333 TPR-EXIT veh=57 state=choose`、`:1348` 前后 `CPL-SAFE-FAIL veh=57 tile=5,15`、`CPL-RET ... ret=0x1 why=cpl-fail`

**为什么存疑**：若 `13,15` 上没有 54 的预留、也没有 54 的车节，那 `best=54` 就不可能是"预留归属回溯"或"物理占用"任一路径的产物；反之若 `best` 来自 `GetTrainForReservation()` 的追溯，则说明**地图上确实存在一个被归属到 54 的位**——而 54 的 `CRT`/`RESV-WATCH` 记录里看不到 13,15，二者矛盾。

**待验证判据**：①`FindSafeCouplePositionProc` 里 `best` 到底是「格上物理车」还是「预留回溯主」；②日志中 veh=54 的预留覆盖格集合（`RP cur=` / `RESV-WATCH` / `RESV-EXT-PRE`）是否含 `13,15`；③`13,15` 是否有物理车节；④若两者皆无，则 `best=54` 属**幽灵归属**（KI-330/207c 家族），需查 `GetTrainForReservation` 的回溯是否会跨格串到 54。

**状态**：已定性（2026-10-27 续三），见 §22.1。

### 22.1 结论：`best=54` 不是「预留归属」，是 13,15 上真有一节 54 的车身

**读码（`src/pathfinder/yapf/yapf_rail.cpp:119-172` `FindSafeCouplePositionProc`）**：
1. 第一道门 `if (HasReservedTracks(tile, tracks))`（:131）——**该格必须有一位预留**，否则整段直接 `return true`，**连日志都不会打**。所以 `FSCP tile=13,15` 能出现，先证明 `13,15` 上有预留位。
2. `Train *best` 的来源只有 `check_train_on_tile()`（:134-143）：遍历 `VehiclesOnTile<VehicleType::Train>(t)`，条件 `tr->track == TRACK_BIT_WORMHOLE || HasBit(tr->track, TrackdirToTrack(td))`，然后取 `tr->First()`。⇒ **`best` 恒为「该格上的物理车节所属链头」，从不通过预留回溯求得**（这与 `pbs.cpp` 的 `GetTrainForReservation` 是两条独立路径）。
3. `:168` 打印 `best=%d ord=%d sf=%d`，所以我读到的 `best=54 ord=6 sf=1` 逐字就是"13,15 上那一节车属于链头 54、该链订单 type=6、它是段头"。
4. `fail=notSeg` 出自 :165 `!R3RIsCoupleTarget(best)`：54 的订单是 `type=6`（GOTO_DEPOT 系），不是 `OT_WAIT_COUPLE(17)`/`OT_GOTO_COUPLE(16)` ⇒ 不是挂接目标 ⇒ 该格判为障碍。

**日志侧证 54 的车身确实横在 12,15–14,15 三格**（同一 tick 前后）：
- `:1236 LOCO-AFTER-DECOUPLE veh=54 ... tile=12,15`（链头在 12,15）
- `:1237 TPR-BLOCKED veh=54 tile=12,15 spd=2 mf=12,15 origin=12,15 okay=1 td=8 other=0 otherTile=12,15 otherOrder=0 caronly=1`（被 veh=0 同格挡）
- `:1241 CRT veh=54 order=6 dir=5 origin=14,15 td=8 found=1`（**预留起点/mf 已到 14,15**，而链头 tile 仍是 12,15 ⇒ 车身跨 12,15→13,15→14,15）
- `:1242 DEPOT-ARR veh=54 spd=0 real=1(6)`（链长 6 节 ≈ 48px ≈ 3 格，与跨三格自洽）
- `:1238 TPR-EXIT veh=54 state=veto` → `TRP veh=54 ... mstuck=1 => ok=0 res=0` → `:1252 STUCK veh=54 tile=12,15 state=1 stuck=1`

**因此对玩家质询的答复**：`13,15` 的预留位是 **54 自己中间车节脚下的 consist 预留**（`ReserveTrackUnderConsist`，KI-207c），不是"54 往前铺的路径预留"，也不是 57 订的；而 `best=54` 更与预留无关——它来自 `VehiclesOnTile(13,15)` 看到的**那一节车身**。**「54 没往那格铺预留」和「13,15 上有 54 的位/车」两件事同时成立，不矛盾。**

### 22.2 追加取证（2026-10-27 续四）：consist 预留是「逐节、整格」，且正常预订**不打日志**

**读码（`src\train_cmd.cpp:19191-19248` `Train::ReserveTrackUnderConsist()`）**：
1. `:19197` `for (const Train *u = this; u != nullptr; u = u->Next())` —— 遍历**整条链的每一节车**，不是只订链头。
2. `:19233` `TryReserveRailTrack(u->tile, TrackBitsToTrack(bits))` —— 对该节**脚下那一格的全部轨道位**整格订位（`bits = u->track`，`u->track == TRACK_BIT_NONE` 时回退 `GetTrackBits(u->tile)`）。
3. `:19242-19247` `const bool anomaly = (!ok || !res_now); if (anomaly && ...) R3RDbgWrite("RESERVECONSIST ...")` —— **只有订失败/位丢失才打行**，正常订上时一行都不打。

⇒ 这解释了玩家观察与日志的落差：**「54 没往 13,15 铺路径预留」是对的，日志里也确实查不到 54 订 13,15 的行；但地图上 13,15 的位是 54 的车节脚下格被 consist 保护逐节订上的**（正常路径不留痕）。

**54 车身横跨 12,15→14,15 的第二个独立证据（倒车形态）**：
- `:1782 CRASH-INFO ... mf=54 mftile=15,15 ... mfdb=1` —— 54 的移动前端已到 **15,15** 且 `mfdb=1`（`DrivingBackwards`）。倒车时 `GetMovingFront()` 取链尾，链头 `tile=12,15` 不变 ⇒ 车身沿 12,15→13,15→14,15 铺开。
- 因此 `:1457 FSCP tile=14,15 fail=notSeg best=54` 与 `:1348 FSCP tile=13,15 ...` 同源：**13,15 与 14,15 都是 54 的车节脚下格**。

**决定性反证（54 开走后 57 立刻能走 13,15）**：
- `:1726 REACH cur=13,15 td=0 trk=0 depth=3 reach_bits=0x1` —— 57 第二次寻路把 13,15 当作可达格；
- `:1766 RESV-EXT-PRE veh=57 tile=14,15 enterdir=0 tracks=0x9 reserved=0x8 doRes=0 real=1` + `:1767 CPL-RET ... ret=0x8 why=early-have-reserved` —— 57 顺利通过并在 14,15 已持有位；
- `:1752 RESV-WATCH SET-DEPOT tile=1,15 ... actor=54 head=54` —— 54 抵达车库后，13,15/14,15 的保护位随之消失。

⇒ 若 `13,15` 的位是孤儿残留或 57 自己铺的，54 挪走不会让它变通。**只有「随车体存在、随车体离开而释放」的 consist 保护位才会呈现这个行为**，本条与 §22.1 的读码结论互证。

**结论（收口）**：不改 `FSCP` 的 `notSeg` 判据（它是正确的：格上压着非挂接目标车体就不能在此挂车）；要解的是 `veh=0 ↔ veh=54` 的**同格死锁**（KI-330 修法①同格豁免）。54 一旦能动（或 0 先离开 12,15），13,15/14,15 空出，57 的 `FSCP` 自然通过——`:1726/:1766/:1752` 已给出这条链的正面样本。

---

## 23 第 341 轮：玩家拍板「给路点掉头车一次铺预留的能力」

### 23.1 玩家原话（口径，不得改写）
> 哎呀你这什么垃圾判断能力啊，我直接简单粗暴的办法：给这种路点掉头的车一次铺预留的能力。这样就干净了。你要判断的就是给能力该怎么写条件

⇒ 口径：**不修 `FSCP`、不修 KI-330 同格死锁、不改寻路代价**；只做一件事——给「掉头」这个事件一次「独立铺预留」的能力（`Train::SetForceReserveOnce()`，即 `VehicleRailFlag::ForceReserveOnce`，KI-179 既有机制）。本轮要交付的就是**授予条件**。

### 23.2 两套既有一次性机制（先分清，别混用）
| 机制 | 载体 | 授予点 | 消费点 | 作用 |
|---|---|---|---|---|
| `_r3r_force_replan`（KI-351） | `btree::btree_set<VehicleID>`（`train_cmd.cpp:5251`） | `ReverseTrainDirection()` `:5541` | `R3RTryPathReserveCore()` `:18379` | 只让 core **不要**走「脚下是安全位就早退」的短路，从而真的进 `ChooseTrainTrack()` |
| `ForceReserveOnce`（KI-179） | 逐车 `VehicleRailFlag::ForceReserveOnce` | 耦合提交点 `:13377`、解挂 `:10390/:10391` | 同上 `:18390-18441` 的 `R3ROneShotReserveGuard` | 这一次尝试里把 `force_proceed` 顶到 `TFP_SIGNAL`，**无视红灯做一次预留**；成功即 `ClearForceReserveOnce()`，失败只还原 `force_proceed`、flag 保留到下次 |

⇒ 玩家要的「一次铺预留的能力」= 第二套。现有掉头路径只有第一套（`:5541`），这解释了为什么 `TPR-ZERO-REACH turn=1` 已经出现、`state` 也变成 `choose`，却仍有车铺不出路径：**它能规划了，但被红灯/信号门挡住时没有任何越权余地**。

### 23.3 授予点：放在 core 里，不放 `ReverseTrainDirection`
选择 `R3RTryPathReserveCore()` `:18379` 之后（`const bool r3r_pending_replan = R3RConsumeForceReplan(consist);` 的下一行），理由：
1. `r3r_pending_replan` **只由 `ReverseTrainDirection()` 置位** ⇒ 天然把「掉头」这一个事件作为触发源，不需要给 `ReverseTrainDirection` 加参数区分订单/路点/手动。
2. 时序正确：`r3r_one_shot` guard 在 `:18390` 才构造（`:18441` 读 `HasForceReserveOnce()`），此处授予 **同一次调用立刻生效**，也正是 `ReverseTrainDirection` `:5542` 那一次 `TryPathReserve`。
3. 与 replan 消费点相邻，两个标志成对：一个负责「肯规划」，一个负责「规划得出去」，不会出现只给一半的悬挂态。
4. `R3RIsCarOnlyFormation()` 在 `:18196` 已被本文件使用，`:18379` 处必然可见，无需新增前向声明。

### 23.4 条件（七条，全部满足才授予）
| # | 条件 | 理由 |
|---|---|---|
| ① | `r3r_pending_replan`（即 `ReverseTrainDirection` 刚释放了自己的预留） | 唯一「前路确定被自己清空」的时刻，不是猜测 |
| ② | `!consist->HasForceReserveOnce()` | 幂等；已持有则不重复授予，也不刷探针 |
| ③ | `consist->force_proceed == TFP_NONE` | 已是 `TFP_SIGNAL`（别处一次性能力未消费）不必叠加；`TFP_STUCK`（玩家强制）绝不覆盖 |
| ④ | `!R3RIsCarOnlyFormation(consist)` | 纯车底不订路（KI-208/KI-339 门禁），给了用不上，且会在挂接后残留成越权（KI-219 血案） |
| ⑤ | `!consist->current_order.IsType(OT_WAIT_COUPLE)` | 等待者同为「不订路」形态，同上 |
| ⑥ | `consist->current_order.GetType() != OT_LOADING` | 装卸中调向不是「前路被清空」形态；且与 `r3r_force_replan` 的 `:18707` 口径一致 |
| ⑦ | `!IsRailDepotTile(consist->tile) && !(consist->track & TRACK_BIT_DEPOT)` | 库内调向不需要跨信号；KI-189 已踩过「库内授予、出库才发现被烧/遗留」的坑 |

**不做的**：不给 `ReverseTrainDirection` 加触发源参数（条件 ③~⑦ 已把「不需要能力」的场景排除干净，手动调向按钮拿到一次越权也无害——它同样是「刚刚自己清空了预留」）；不在 `TryPathReserve` 失败后回收 flag（KI-179 的「失败保留」是刻意设计，否则拥挤线路上的掉头车永远出不去；越权窗口本身不跨 tick，因为 guard 析构会把 `force_proceed` 还原到 `saved_force_proceed`）。

### 23.5 复测判据（4 条）
1. 路点/站台掉头后出现 `R3R-RES-ONCE grant head=<N> why=turn`（本轮新增探针）且同一列车不再出现「只订脚下几格」的逐格爬行。
2. 同车随后出现 `R3R-RES-ONCE used head=<N>`（能力被一次成功预留消费），且 `TPR ok=1 res=1`。
3. 负判据：库内掉头（`TRACK_BIT_DEPOT`）**不**出现 `why=turn` 授予；纯车底/`OT_WAIT_COUPLE` 等待者掉头同样不出现。
4. 负判据：耦合、解挂的既有 `R3R-RES-ONCE grant` 行数与行为逐字不变。

**升级后的真因（取代 §21.4 的"预留互斥"表述）＝三方同格死锁**：
- `veh=0`（107px / 17 节的纯车底链，`tgtOrd=17` WAIT_COUPLE）停在 **12,15**；
- `veh=54`（机车，订单 type=6 去 16,14）链头也在 **12,15**、车身铺到 14,15 ⇒ `:1237` 被 `other=0 otherTile=12,15 caronly=1` 判阻塞 ⇒ `STUCK stuck=1` 原地不动（KI-330 家族：同格纯车底被判阻塞）；
- `veh=57` 要从 16,14 过去挂 `veh=0`，唯一通道经过 13,15，而 13,15 被卡死的 54 车身占着、且 54 不是挂接目标 ⇒ `FSCP fail=notSeg` ⇒ `CPL-SAFE-FAIL` ⇒ `why=cpl-fail` ⇒ 零预留。

⇒ **关键结论修正**：57 没铺出预留不是"掉头后不重铺"，也不是"54 的合法预留互斥"，而是**54 被 veh=0 同格锁死、横在通道上**。要解的是 54↔0 的同格死锁（KI-330 修法 ①「同格豁免」正是针对它），54 一走开，13,15 空出，57 的 `FSCP` 就能过。

**仍未证（记为 U-340-1）**：`13,15` 的预留位到底记在 54 的 consist 账上还是它的 PBS 路径上（不影响结论，只影响日志判读）；54 的 `origin=14,15` 与 `tile=12,15` 之间是否有更早的移动记录（本段日志内 `mf` 从 12,15 跳到 14,15 的那一步未见，属探针覆盖空档）。

### 23.6 落地与构建自证（2026-10-27）

- 落点：`src\train_cmd.cpp` 的 `R3RTryPathReserveCore()`，`r3r_pending_replan` 之后新增授予块 `:18408-18420`（七条条件 + `R3R-RES-ONCE grant ... why=turn`），紧接既有 `R3ROneShotReserveGuard`（`:18431`）⇒ 同一次调用立即生效。
- 未碰任何 `src\*.h`、未新增语言串 ⇒ 无存档格式变更、不触发全量。
- 构建：复用既有 `_tmp_inc_build.cmd`（**未新建任何 `.cmd`**）；`build\R3R_incbuild.done` = `EXIT_CODE=0`；`src\train_cmd.cpp` @21:58 → `build\openttd.exe` @22:03（**52 322 304 B**）；`read_lints` 0 条。
- KI 编号：本轮条目 = **KI-356**（写入 `R3R_KNOWN_ISSUES.md` 末尾「第 341 轮」小节）。注意 `KI-353` 已被「第 243 轮续八」的孤儿预留条目占用，本节初稿若引用过 KI-353 属撞号，以 KI-356 为准。
- 状态：已实现 + 已编译，**游戏内复测待做**（判据见 §23.5 与 KI-356）。

## 21. 修法 D 实机复测取证（2026-10-27 21:43 日志，第 340 轮续二）

### 21.1 采样有效性

- `build\R3R_debug.log` = 242 510 B / 1 792 行 / 21:43:57。
- 产生者 `build\openttd.exe` = 52 322 304 B / 21:34:06 = **修法 D 版**（晚于 `train_cmd.cpp.obj` 21:31:45）⇒ 采样有效。

### 21.2 命中：2 次掉头，2 次都是 `turn=1`（修法 D 生效）

| 日志行 | 内容 |
| --- | --- |
| :1323 | `REVERSEDIR veh=57 tile=16,14 dir=5 order=16 spd=0 nv=3 rev=0 stuck=0 db=0` |
| :1331 | `TPR-ZERO-REACH veh=57 mf=16,14 origin=16,14 td=0 okay=1 spd=0 real=1 order=16 look=0 turn=1` |
| :1738 | `REVERSEDIR veh=54 tile=18,16 dir=5 order=2 spd=0 nv=3 rev=0 stuck=0 db=0` |
| :1746 | `TPR-ZERO-REACH veh=54 mf=18,16 origin=18,16 td=0 okay=1 spd=0 real=2 order=2 look=0 turn=1` |

两次都是 `spd=0`、`okay=1`、`origin.tile == mf`——正是 §20.6 判定会走早退的形态；`turn=1` 证明判据确实被换向标记打开，且 `TPR-EXIT` 的 `state` 由 `ok`/`reach0` 变成 `choose`：

- :1748 `TPR-EXIT veh=54 state=choose pair=0 tile=18,16 spd=0 other=54 ... origin=18,16 okay=1`（对照 :1315 改前的 `state=ok`）。

### 21.3 正判据②成立：veh=54 掉头后一路铺到车库

```
:1749 CRT veh=54 order=2 dir=5 origin=16,15 td=13 found=1
:1751 CRT veh=54 order=2 dir=5 origin=2,15 td=0 found=1
:1752 RESV-WATCH SET-DEPOT tile=1,15 track=255 phase=yapf-resv actor=54 head=54
      TRP veh=54 mf=18,16 real=2 type=2 dest=1921 spd=0 ... => ok=1 res=1
      DEPOT-ARR veh=54 spd=0 real=2(2) curType=2 ... destTx=1 destTy=15 destDepot=1 destResv=1
```

`ChooseTrainTrack` 连续两次 `found=1`、终点 `2,15`，随后整格预留登在目标车库 `1,15`（`phase=yapf-resv`，有主 `head=54`），`res=1` + `destResv=1`。⇒ **路点/普通轨掉头后铺出连续预留这一目标已达成。**

### 21.4 未成立的那一例（veh=57）不是早退，是挂接寻路被挡

veh=57 的订单是 `order=16`（`OT_GOTO_COUPLE`），所以 `ChooseTrainTrack` 之后立刻切进挂接专用寻路：

```
:1333 TPR-EXIT veh=57 state=choose ... other=57
      RESV-EXT-PRE veh=57 tile=15,14 enterdir=0 tracks=0x9 reserved=0x0 doRes=1
      CPL-ENTRY / CPL-ORIGIN / CPL-ORIGIN-TD / CPL-ORIGIN-FOLLOW ...
      CPL-PATHFOUND veh=57 found=1
      CPL-SCAN tick=12460 veh=57 found=1 closed=3 nodes=17 ... why=acc-car
      CPL-HEAD best=5,15 head=5,15
      CPL-BEST veh=57 tile=5,15 td=0 cost=11055
      CPL-BACK pPrev=5,15 td=0 parent=15,15 ptd=2
      FSCP tile=13,15 fail=notSeg best=54 ord=6 sf=1
      CPL-SAFE-FAIL veh=57 tile=5,15
      CPL-RET veh=57 tick=12460 tile=15,14 ... ret=0x1 why=cpl-fail
      TRP veh=57 mf=16,14 real=1 type=16 dest=1932 spd=0 ... => ok=0 res=0
      COUPLE-WAIT-OFFDEST veh=57 tile=16,14 ... destTile=1932 onStnTile=1
      SPD-DROP veh=57 tile=16,14 prev=5 order=16 real=1 stuck=1 ...
```

它**已经找到目标**（`found=1`、`best=5,15`），不是"没路"，而是在安全停车位检查处被 `13,15` 上的 **veh=54**（`fail=notSeg`、`sf=1`）否掉整条路 ⇒ `why=cpl-fail` ⇒ `res=0`。此时 veh=54 正从 18,16 掉头返回车库，并且它自己那条路已整段预留（:1752 `SET-DEPOT 1,15`、:1241 `CRT veh=54 origin=14,15`）。⇒ **两列车抢同一段线路/同一车库入口，属真互斥（或 FSCP 把"正在离开的车"当永久障碍），与"掉头后不重铺"无关。**

### 21.5 后续行为：veh=57 恢复行驶并逐格续订（非"不铺"）

```
:1734 RESV-WATCH SET-RAIL tile=15,14 track=3 phase=controller actor=59 head=57
      LOCO-MOVE veh=57 tick=12708 tile=16,14 j=264 spd=30 dir=5 db=1
      RESV-EXT-PRE veh=57 tile=14,15 enterdir=0 tracks=0x9 reserved=0x8 doRes=0
      CPL-RET veh=57 tick=12749 tile=14,15 ... res=0x8 ret=0x8 why=early-have-reserved
```

`doRes=0` + `why=early-have-reserved`：前方一格已有预留，正常续订；`reserved=0x1/0x4/0x8` 每格一位，属正常 PBS 逐格延伸，不是"一格都不订"。

### 21.6 相邻格真冲突与探针噪声

- :1784 `RESV-WATCH SET-RAIL tile=15,15 track=0 phase=consist actor=54 head=54`、`RESERVECONSIST veh=55 tile=15,15 track=0x1`、`RESERVECONSIST veh=58 tile=14,15 track=0x8`：两列车的车节在 15,15 / 14,15 相邻格交错占位，属真几何冲突。
- 日志末 `CRASH: movingFirst_order=2 v_order=0 vFirst_order=16 same_chain=0 speed=0 tile=1934` 是 `src\train_cmd.cpp:19658` 的历史调试字面量（源码注释写明 "remove after locating the crash"），**不是崩溃**——其后仍有 `UNRES-RAIL` / `RESV-WATCH` / `CPL-PAIRMAP` 行。

### 21.7 本轮判断（结论句）

1. 修法 D 按设计生效：`turn=1` 命中 2/2，`state` 由早退的 `ok` 变为 `choose`。
2. 「路点掉头不铺连续预留」在 21:34 版上**已不再出现**：唯一完整样本 veh=54 掉头后订到车库 `1,15` 且 `ok=1 res=1`。
3. 剩下的 veh=57 是另一问题：`GOTO_COUPLE` 车掉头后挂接寻路在 `FSCP 13,15 fail=notSeg best=54` 处被否，`why=cpl-fail` ⇒ 无预留。待判：是 54 的合法预留互斥（无害），还是 FSCP 把"正驶离的车"误当永久障碍（需 `notSeg` 判据复核）。
4. 负判据未出现异常：`spd!=0` 的修法 C 行（:1237 `TPR-BLOCKED veh=54 ... spd=2`）保持原样，未见多余 `TPR-ZERO-REACH`。

## 24. 第 342 轮（2026-10-27）：KI-356 复测取证 —— 「授予了，但没行使」

玩家原话：**「请看日志，列车并没有行使自己的能力，怀疑是早退」**。采样 = `build\R3R_debug.log`（1826 行以上），产生者 = 22:03 版 `build\openttd.exe`（含 KI-356 授予块）。

### 24.1 授予端：正常，两个样本都拿到能力

- `:1382 R3R-RES-ONCE grant head=57 why=turn force_proceed=0 tile=16,14`（紧随 `:1381 REVERSEDIR` / `:1383 TPR-ZERO-REACH veh=57 ... turn=1`）
- `:1802 R3R-RES-ONCE grant head=54 why=turn force_proceed=0 tile=18,16`

⇒ 七条条件在真实场景里命中 2/2，`why=turn` 标记正确，`force_proceed=0` 说明授予时确实是 `TFP_NONE`、guard 随后把它顶成 `TFP_SIGNAL`。

### 24.2 veh=54：能力**真的行使了**（对照正样本）

```
:1807 RESV-SIGPASS veh=54 tile=4,16 track=0 dir=0 doResIn=1 spd=0 reserved=0 ownRes=0 coType=2 real=2 db=1
:1809 SET-DEPOT (订到库 1,15)
:1810 R3R-RES-ONCE used head=54 force_proceed=2 tile=18,16
```

⇒ `RESV-SIGPASS ... reserved=0 ownRes=0` = 车在**无预留**状态下靠 `force_proceed=TFP_SIGNAL` 越过红灯进入下一格，随后订路成功。这一条正是能力设计要解决的形态。

### 24.3 veh=57：从授予到消耗隔了 100+ tick，期间**一次都没用上**

失败段（每次尝试同一形态，行号递增重复）：

```
:1407 FSCP tile=13,15 fail=notSeg best=54 ord=6 sf=1
:1408 CPL-SAFE-FAIL ...
:1409 CPL-RET veh=57 tick=12460 tile=15,14 ... ret=0x1 why=cpl-fail spd=0 real=1
:1410 TRP veh=57 ... spd=0 fp=0 look=1 one=1 mstuck=0 fto=1 => ok=0 res=0
:1420 STUCK veh=57 tile=16,14 state=1 stuck=1 ...
```

`one=1` 一直挂着（`:1410`、以及 1462/1504/1554/1616 四次重试同形），直到阻塞解除：

```
:1666-1669 RP cur=15,14 / 15,15 / 14,15 / 13,15        ← 4 格部分预留
:1670 CPL-RESERVE veh=57 reserved=1 next=3
:1671 CPL-RET veh=57 tick=12560 ... ret=0x8 why=cpl-partial-res spd=0 real=1
:1672 R3R-RES-ONCE used head=57 force_proceed=2 tile=16,14
:1673 R3R-RES-ONCE clear head=57 tile=16,14
:1674 TRP veh=57 ... fp=0 one=0 => ok=1 res=1
:1676 STUCK veh=57 ... stuck=0 spd=2
```

### 24.4 根因（结论句）

1. **「早退」点坐实**：`src\pathfinder\yapf\yapf_rail.cpp` 的 `CheckSafePositionOnNode()` 里 `best = VehiclesOnTile(t)->First()`，`R3RIsCoupleTarget(best)` 为假即 `fail=notSeg` → `return false` → 整条挂接路径作废 → `ChooseTrainTrack()` 返回 `CTTRF_NONE` → 本次预留**一格都没订**。该判据位于 YAPF **安全位回溯**中，**在一切信号判据之前**。
2. **能力的作用面覆盖不到它**：`ForceReserveOnce` 的唯一杠杆是 `force_proceed = TFP_SIGNAL`，它只影响 `YapfTrainCheckRedSignal()` / `RESV-SIGPASS`（越红灯）与 `ChooseTrainTrack` 的入口信号判定；`notSeg` 这条判据根本不读 `force_proceed` ⇒ 玩家观察属实：**授予了，但这条路上一次都没行使**。
3. **这个阻塞是真阻塞，不是判据错误**：`best=54 ord=6 sf=1`（`ord=6` = `OT_GOTO_WAYPOINT`，即另一列路点车，同为段头），13,15 上确实站着 54 这条链的车节（旁证 `:1784 RESERVECONSIST veh=55 tile=15,15` / `veh=58 tile=14,15`：两列车节在相邻格交错占位）。54 让开后 13,15 立刻可订（`:1669 RP cur=13,15`）⇒ 冲突是瞬时的（约 100 tick），不是永久误判。
4. **附带的真缺陷（口径过宽）**：`R3ROneShotReserveGuard` 的消耗口径是「本次调用只要 `reserved==true` 就算花掉」（`src\train_cmd.cpp:18440-18449`），于是 `:1671` 这次 `why=cpl-partial-res` 的 4 格部分预留（与红灯毫无关系）把能力顺手花掉了 —— `:1672 used` 看起来像「能力生效」，实际上它只是被记账消耗。

### 24.5 疑点登记（按"有疑点立刻落盘"）

- **REG-342-A**：`:1671 cpl-partial-res` 只订 4 格就停（到 13,15），而 `CPL-PATHFOUND best` 指向的挂接目标是 `5,15`。期望=一路铺到目标或下一个安全位；实测=4 格。待判：是 `:1392 CPL-WAITCUT veh=57 state=0 wp=20 tile=16,14` 的等待区路点截断，还是安全位截断，抑或 13,15 之后确实不可订。
- **REG-342-B**：13,15 的阻塞解除时刻与 54 的移动是否严格对应（54 在 `:1793 LINEND veh=54 tile=18,16 spd=5` 已跑到 18,16，说明它在 12560 之前就离开了 13,15 一带）⇒ 需要按 tick 对齐才能判定"是否被多挡了一段时间"。

### 24.6 待拍板（玩家回编号即可）

- **C-1**：维持现状 —— 能力就是「红灯豁免」，veh=57 的 notSeg 归为真阻塞，不改码。
- **C-2**：收紧消耗口径 —— 让能力只在「真的靠它越了红灯（`RESV-SIGPASS` 那一下）」或「完整预留成功（非 partial）」时才 `ClearForceReserveOnce()`；`cpl-partial-res` 这种不花。好处：掉头车的能力能留到下一次真正需要越红灯时；风险：KI-219 式的跨 tick 残留面变大（须同时保留「越权窗口」护栏）。
- **C-3**：给 `notSeg` 阻塞加补救 —— 挂接寻路失败时不直接 0 格，而是降级按普通路径订到阻塞点（现状 `cpl-partial-res` 已部分做到 4 格，可能无需再做）。

### 24.7 口径澄清：「铺预留的能力」与「闯红灯」是同一个开关（玩家质疑「是不是改错了」的答复）

玩家质疑原话：**「这证不是用来赋予能力铺预留的吗，你怎么说成闯红灯的了，你是不是改错了啊」**。逐条对账后的结论：**没有改错，措辞来源是这套机制的实际作用面。**

1. 用的是既有机制，不是新造的：`VehicleRailFlag::ForceReserveOnce` 的官方注释原文就是 `red-signal bypass it never earned`（`src\train_cmd.cpp:10378`），KI-179 的既定用途 = 「两半各自订过路、挂接后合并车做**一次**独立预留」。
2. 这张证在代码里唯一能扳的开关是 `force_proceed`，而它在 `TrainController()`（`src\train_cmd.cpp:15493-15586`）里管的是三件事：信号前等不等 37 tick、`reserve_paths` 下要不要 `TryPathReserve()`、订不到路时要不要 `MarkTrainAsStuck()`。**「订得出预留」和「红不红灯」在这套机制里是同一个闸门的两面** ⇒ 说成「闯红灯许可」是在描述它的实际覆盖面，不是把目的改成闯红灯。
3. 不会被误解成「放任列车闯红灯跑掉」：`R3ROneShotReserveGuard` 在预留成功后**当场**把 `force_proceed` 还原成 `TFP_NONE`（`:18470-18480`）。正样本 veh=54 的顺序即 `RESV-SIGPASS` → `SET-DEPOT 1,15` → `used` → `clear`，放宽只存在于订路这一次调用内。
4. veh=57 没行使，是因为它的失败点在 `CheckSafePositionOnNode()` 的 `notSeg`（别的车占格），那道判据**不读** `force_proceed`。证没错，是那道门不认证。
5. 若玩家要的是「不管前面被谁占着也强行铺出去」，那已经超出这张证的能力边界，要动的是 `notSeg` 判据本身（=允许把预留订到别人车身上，有撞车风险），属另一议题，需单独拍板。

**结论：KI-356 的落点与语义未偏题；上一条口语化回答里「闯红灯」是解释作用面，措辞易误解，已在此写清。**

### 24.8 概念澄清：这个「能力」的本质是什么（玩家问「本质是一个什么」）

代码状态：`src\train_cmd.cpp` 的 KI-356 授予块**已按玩家要求原样还原**（39 行还原，未编译）。

三层定义，按"能做什么"而不是"叫什么名字"：

1. **PBS 世界里「走」的前提是「占」**：列车出发前必须把当前位置到目的地的沿途轨道**登记成本车预留**（`ChooseTrainTrack()` 找到路后由 YAPF 逐格 `reserve`）。信号灯只是"这段路有没有人占"的显示器，不是开关。所以「铺预留」= 提前占位记账，**不修路、不产生新轨道、不改变线路容量**。
2. **「订几格」完全由 YAPF 决定，不由此能力决定**：`notSeg`、红信号、安全位、死路这些判据都在 YAPF 里。这个能力**一格都订不出来**，它只影响"订不到的时候准不准走"。
3. **它真实的作用身份 = 一次「跳过守门员」的临时通行证**：`force_proceed = TFP_SIGNAL` 让 `TrainController()`（`src\train_cmd.cpp:15493-15586`）整段跳过"等 37 tick / `TryPathReserve()` / 订不到 `MarkTrainAsStuck()`"。也就是：**不是"多给一个订座系统"，而是"这次免票进站"**。免票进站的后果（无预留区间行车）正是 KI-219 的来源。

⇒ **「铺预留的能力」这个名字本身是错的**，准确名字是「一次性免信号前等待、可无预留前进的放行」。玩家要的「车头虚拟一道 PBS 信号灯」= 走完整申请流程（申请到就整段占下、申请不到就停着等），**不需要这张证**，需要的是"触发申请"；触发已由 KI-351 修法 D（`:18748` `r3r_force_replan`）提供。

### 24.9 自我纠正：KI-356 方向与口径**相反**，且正样本证据是误读（玩家：「说明你就没做出我想要的东西」）

玩家原话：**「我在这个东西创造之初不就说过吗：就像自己前面有一道 PBS 一样，触发 PBS 的效果……那说明你就没做出我想要的东西」**。结论：**玩家判断成立，两级错误。**

**错误一（方向）：** 「虚拟 PBS 信号」的真实机制是 `TrainController()` 的 `TFP_NONE` 分支（`src\train_cmd.cpp:15493-15586`）——等节拍 → `TryPathReserve()` → 订到就走 / 订不到 `MarkTrainAsStuck()` 等。KI-356 发的是 `force_proceed = TFP_SIGNAL`，而 `TFP_SIGNAL` 会让**整段 if 被跳过**（连 `TryPathReserve()` 都不进）。也就是说：**它实现的不是「前面有一道 PBS 信号」，而是「这道信号不存在」**。方向完全相反。

**错误二（证据）：** §24.2 把 `:1807 RESV-SIGPASS veh=54 tile=4,16 ... reserved=0 ownRes=0` 当成「靠 `TFP_SIGNAL` 无预留越红灯」是**误读**。该打印点在 `src\train_cmd.cpp:17265-17287`，属于「列车/路径经过信号格时的状态记录」，`reserved=0` 只表示**该格此刻没有被预留**（`HasReservedTracks()` 为假），并不代表列车闯了红灯，也不证明 `force_proceed` 起过作用。
⇒ **REG-342-C（新登记）：KI-356 至今没有任何一条日志能证明它产生过正面效果。** `:1810 used head=54` 只是"本次调用 reserved==true ⇒ 记账花掉"，与能力是否起作用无关。

**修正后的判断：**
1. KI-356 = 未被证实有效 + 机制方向与玩家口径相反 ⇒ 应撤销，不保留。
2. 玩家要的效果与这张证无关，它本来就该由默认 `TFP_NONE` 分支 + 修法 D 提供；若删证后仍订不出，被挡的一定是 YAPF 内部判据（`notSeg` / 普通信号 / `CPL-WAITCUT`），需要逐条定位，而不是加通行证。
3. 复测判据（删证后应满足）：`fp` 全程为 0、不再出现 `R3R-RES-ONCE grant/used/clear`；掉头当 tick 出现 `TPR-ZERO-REACH ... turn=1` + `state=choose`；订不到时 `stuck=1` 且 `spd` 保持 0；订到时 `res=1` 且预留格数覆盖到目的地/安全位。

### 24.10 机制定名：`TryPathReserve()` 的真实触发条件（玩家口径：「我要的就是这个 trypathreserve」）

玩家理解：**「列车前面一格（或接续/延续预留时更远）有 PBS 时，就调用 `TryPathReserve()`，我要的就是这个」**。方向正确，但因果要反过来——这决定了后续改法是否走偏：

1. **不是「看见灯才申请」，而是「要往前走就必须先申请」，灯只是申请结果的显示器。** PBS 段里的信号不是阀门，是「这段有没有人占」的读数（由 `HasReservedTracks()` 驱动）。所以正确说法是：**列车要用前方轨道 → 必须持有该段预留 → 没有就调 `TryPathReserve()`**。
2. **真实触发条件（`src\train_cmd.cpp:15493-15586`）**：`force_proceed == TFP_NONE` 时进入该分支 → `wait_counter` 节流 → 计算 `seg_state`（`reserve_paths` 开启即 `SigSegState::Path`）→ 需要离开当前信号段时调用 `TryPathReserve()`；失败则 `MarkTrainAsStuck()`。
3. **「有预留延续就不申请」成立**：若 `lookahead` 仍覆盖前方，列车沿现有预留走，不重复申请；只有预留到尽头/被释放时才申请。**掉头恰好把整条预留释放**（`ReverseTrainDirection()`），所以掉头后必然处于「无预留延续」状态 ⇒ 必然要申请一次。KI-351 修的正是「掉头后没去申请」（core 早退）。
4. **节拍等价性（关键）**：`if (++v->wait_counter < 37) return true;` —— 真信号前的车也是**每 37 tick 申请一次**，不是每 tick。所以掉头车「失败后等 37 tick 再试」与真信号前**完全同频**；veh=57 的 ~100 tick 等待 = 2~3 个标准窗口，属正常信号前行为，不是延迟缺陷。
5. **结论**：玩家要的 `TryPathReserve()` 调用**已经在发生**（日志标记 `TPR-ZERO-REACH` / `TRP` 即入口与返回值；`:1383 veh=57 turn=1`、`:1410 veh=57 ok=0 res=0`）。真正缺的不是「调用」，而是「调用失败后被哪条 YAPF 判据挡住」。四项判据中 1~3 已满足，仅第 4 项（`fp` 全程为 0、无 `RES-ONCE`）被 KI-356 破坏 ⇒ **撤证即可，不需要新机制**。

### 24.11 **撤回 §24.9 的「方向相反」结论**：KI-179 原始口径确认无误（玩家回忆：「挂接后赋予一次，铺设了正常预留（包括在 PBS 前）就销毁」）

玩家原话：**「最初我要求你赋予铺预留能力的时候，是在挂接后赋予一次，当列车铺设了正常预留（包括在PBS前），就把这个能力销毁。记起来了吗，我要的就是这个」**。**记起来了，而且 §24.9 的判断是错的，在此撤回。**

**错在哪：拿「行车」条款去解释「订路」条款。** §24.9 引用 `TrainController()`（`src\train_cmd.cpp:15493-15586`）说「`TFP_SIGNAL` 会跳过整个 if，连 `TryPathReserve()` 都不进」——但那段 if 管的是**行车/出发**路径；**掉头与挂接的订路是在 `ReverseTrainDirection()` / 挂接逻辑内部直接调用 `TryPathReserve()` → `ChooseTrainTrack()`，根本不经过那段 if**。所以「发证 = 跳过 TryPathReserve」在本议题的场景里不成立。

**`force_proceed` 在订路里的真实读取点（`ChooseTrainTrack()` 内，`src\train_cmd.cpp:17219-17230`）：**

```
if (IsRestrictedSignal(tile) && consist->force_proceed != TFP_SIGNAL) {
    ... 跑 trace-restrict 程序；TRPRF_TRAIN_NOT_STUCK / WAIT_AT_PBS / SLOT_ACQUIRE 等可拒绝本次订路 ...
}
```

⇒ 持有 `TFP_SIGNAL` 时**整段限制信号检查被跳过**，这正是「原本订不出第一格、发证后能订出」的实现口径。**KI-179 的「挂接后授予一次 + 铺设正常预留（含 PBS 前）即销毁」在代码上有确切落点**：`R3ROneShotReserveGuard`（`:18424-18483`）在 `reserved == true` 时 `ClearForceReserveOnce()`，与玩家原话逐字一致。

**据此更正：**
1. KI-356 的方向**没有跑偏** —— 它就是把 KI-179 的同一机制扩到「路点掉头」这个同样是「前路被自己清空」的时刻。
2. **C-2（收紧消耗口径）不成立**：玩家原话是「铺设了正常预留（包括在 PBS 前）就销毁」，`cpl-partial-res` 的 4 格也是成功铺设的正常预留，现行为（`reserved==true` 即清）**正确**，不应改。
3. **C-1（维持现状）成立**。
4. §24.9 的「REG-342-C：能力从未被证实有效」也一并降级为「未证实但也未被证伪」：`:1807 RESV-SIGPASS` 打印点在 `ChooseTrainTrack()` 内部（`:17265-17287`），是**订路过程中**经过信号格的记录，`reserved=0` 只表示该格此刻无预留，既不能证明也不能否定能力生效。
5. **剩下的唯一真边界**：`CheckSafePositionOnNode()` 的 `notSeg` **不读 `force_proceed`**，所以 veh=57 那种「别的车横占格子」的情形能力无从发力。是否要给 `notSeg` 开口子（=允许把预留订到别人车身上，有撞车/穿透风险），需玩家单独拍板。

**代码状态**：KI-356 授予块已按玩家要求**原样还原**，本轮未再改动。

### 24.12 回答「原始能力像信号灯，为什么到这里又不行了」——**不是不行，是 57 当时没在做「铺预留」**

玩家问：**「原始的那个铺预留能力就像自己前面有个信号灯，为什么到了这里又不行了？」**

**前提纠正：同一个能力、同一个掉头场景，veh=54 已经成功。** `build\R3R_debug.log`：

```
:1802 R3R-RES-ONCE grant head=54 why=turn force_proceed=0 tile=18,16
:1803 TPR-ZERO-REACH veh=54 ... turn=1
:1805 TPR-EXIT ... state=choose
:1806 CRT veh=54 order=2 dir=5 origin=16,15 td=13 found=1     ← 进 ChooseTrainTrack
:1808 CRT veh=54 order=2 dir=5 origin=2,15  td=0  found=1     ← 订到目的地
:1809 RESV-WATCH SET-DEPOT tile=1,15 ... actor=54
:1810 R3R-RES-ONCE used head=54 force_proceed=2
:1812 TRP veh=54 ... fp=0 look=1 one=0 => ok=1 res=1          ← 成功
```

**57 为什么没行使：它那一刻的业务不是「铺预留」。** 它的订单 `order=16`（`LOCO-ORD 1 type=16`，即 OT_GOTO_COUPLE），走的是**挂接寻路**，不是 `ChooseTrainTrack()`：

```
:1382 grant head=57
:1387 CPL-ENTRY veh=57 tile=16,14 orderType=16
:1392 CPL-WAITCUT veh=57 state=0 wp=20
:1402 CPL-PATHFOUND veh=57 found=1
:1407 FSCP tile=13,15 fail=notSeg best=54 ord=6 sf=1          ← 卡在实体占格，不是信号
:1408 CPL-SAFE-FAIL veh=57 tile=5,15
:1410 TRP veh=57 ... fp=0 one=1 => ok=0 res=0                 ← 能力原封未动
```

`FSCP` = 挂接安全位搜索，判据 `notSeg`（13,15 上是 veh=54 的车厢）**不读 `force_proceed`**，也不产生普通预留 ⇒ 能力要开的那扇门（`ChooseTrainTrack()` 内 `IsRestrictedSignal && force_proceed != TFP_SIGNAL`，`:17222`）在 57 这条路径上**根本没有经过**。

**所以「原始像信号灯、到这里不行」的真实原因：**

| | 原始场景（挂接后一次） | veh=54 掉头 | veh=57 掉头 |
|---|---|---|---|
| 走哪条代码路径 | `ChooseTrainTrack()` 铺普通预留 | 同左 | **挂接安全位搜索 `FSCP`** |
| 挡路的是什么 | 信号/限制信号（逻辑闸） | 信号（`:1807 RESV-SIGPASS`） | **实体车厢占格 `notSeg`** |
| 能力能否作用 | 能 | 能（已成功） | **不能（该路径不读该开关）** |

**比喻层面的正解**：真正的 PBS 信号灯也管不了「我要把车停到别人车厢上完成挂接」这件事——信号灯管的是**通行权**，不是**占位事实**。57 需要的是「挂接安全位」，那里本来就该判红/无位。所以「不像信号灯」恰恰不是能力退化，而是那件事本来就不属于信号灯的辖区。

**结论**：能力没有变、也没有失效；变的是 57 当时的业务类型。KI-356 保持现状即可。

### 24.13 「挂接寻路并入 YAPF」工程与本题的关系（玩家提问，回答：**直接相关**）

玩家问：**「我们是不是有一个挂接寻路并入 YAPF 的工程没做完，会不会和这个有关？」** ⇒ **有关，而且 57 的「能力没机会用」正是这个缺口的直接表现。**

**现状（源码取证）：**
- 挂接寻路**本身已经是 YAPF**：`DoTrainCouplePathfind()`（`src\train_cmd.cpp:16011-16014`）只是 `YapfTrainCoupleTrack(v, !dont_reserve)` 的薄壳，YAPF 内走独立 destination 类型 `CYapfDestinationTrainRailT`。所以「没并入 YAPF」这个说法要修正为：**没并入的是「预留铺设 + 安全位收口」这一层**。
- 两套回溯闭包**互不调用**（`R3R_couple_resv_unsafe_stop_round310_memo.md` §11 已确证）：普通寻路 = `ExtendTrainReservation()` + `while (!IsSafeWaitingPosition(...))`（`train_cmd.cpp:17189`）；挂接寻路 = `yapf_rail.cpp:1081-1200` 里 `FindNearestCoupleTrain` 自己的 `rp()`，收车条件 `if (cur == tg) return true;`（`:1114`，`tg = consist_head_tile`，`:1019`）。

**由此，普通寻路拥有的四样东西，挂接路径一样都没继承：**

| 普通寻路有 | 挂接寻路 |
|---|---|
| 信号判据 + `force_proceed != TFP_SIGNAL` 豁免（`train_cmd.cpp:17222`） | 无（`YapfTrainCoupleTrack` 全链不读 `force_proceed`） |
| `IsSafeWaitingPosition` 安全位收口（`:17189`） | 无（`rp()` 只认「到得了」） |
| `ExtendTrainReservation` 逐格续订 + 截断 | 自己的 `rp()` 逐格铺，无安全语义 |
| 失败分级（`CTTRF_*` 结果位） | 只有 `FSCP fail=notSeg/2nd/other/tryReserve` |

**57 的现场正落在这里**：`FSCP tile=13,15 fail=notSeg` 出自 `FindSafeCouplePositionProc`（`yapf_rail.cpp:150-197`），是挂接寻路自家的安全位检查，自然不认 `force_proceed`。所以「同一个掉头动作、同一个能力，54 有效、57 无效」不是随机现象——**订单类型不同就走了两套互不相通的寻路**：普通订单 → `DoTrainPathfind`（能力生效），挂接订单 → `YapfTrainCoupleTrack`（能力不可达）。

**边界（泼冷水）：并进来也不会让 57 通行。** `R3R_couple_resv_unsafe_stop_round310_memo.md` §13.2 已定：`fail=other` / `fail=tryReserve` 是第三方通行权，`notSeg` 是「这格不是等待段」，**任何一种放开都等于把预留铺到别人车厢上 ⇒ 撞车/穿模**。并入解决的是**判据一致性**，不是**放行**。
⇒ 真正的收益是：让「像前面有一道 PBS 信号」对所有订单类型**统一成立**，而不是只在普通寻路里成立。

**状态**：该工程在 memo 中从未立项（只有 310 轮的诊断与「两套代码互不调用」的定性），**没有任何一行实施代码**。若要推进，需先出范围与风险清单。

### 24.14 **重大更正：§24.13 及之前对 veh=57 现场的读取是错的——它没有「停着等」，它一直在 30 速「裸奔」直到与 54 相撞**

玩家原话：**「你对现场也是读取烂的一塌糊涂，veh57 甚至没有 look forward 就在铁轨上裸奔，所以才和 54 撞车了」**。**玩家判断成立，此前「57 被 notSeg 挡住、停着等」的说法作废。**

**真实时间线（`build\R3R_debug.log`）：**

| tick | 行 | 事实 |
|---|---|---|
| 12460/12480 | `:1382`/`:1460` | 两次挂接寻路 `FSCP tile=13,15 fail=notSeg best=54`，`TRP ok=0 res=0 look=0`，车在 16,14 **停着等** |
| — | `:1424-1473` | `CPL-S0-CHK ... wait=3..30`，只在**这 30 余 tick** 内是停的 |
| ~12690 | `:1779-1784` | `RP cur=15,14/15,15/14,15/13,15` → `CPL-RESERVE veh=57 reserved=1 next=3`，**铺到 4 格挂接预留** |
| 12691 | `:1792` | `LOCO-MOVE veh=57 tile=16,14 spd=30` —— **已经 30 速在跑** |
| 12714/12761 | `:1818`/`:1824` | 15,14 spd=30、15,15 spd=30，全程无 `look` |
| 12749 | `:1820` | `CPL-RET ... why=early-haverserved spd=30` ← **命中「已有预留就早退」捷径，本次不订路** |
| 12749/12761/12802 | `:1815`/`:1819`/`:1825` | `RESV-EXT-PRE ... doRes=0`（三次调用全部**没有订路资格**）且 `spd=30` |
| 12802 | `:1828` | `CRASH: movingFirst_order=2 v_order=0 vFirstOrder=16 speed=0 tile=1934` |
| 12802 | `:1829` | `mf=54 mftile=15,15 mfspd=100` \| `v=58 vtile=14,15 vspd=0` \| `vhead=57 vheadspd=30` |

⇒ **撞车形态：54（去车库，`spd=100`）从 15,15 冲进 14,15，撞上停在 14,15 的 58（57 链第 2 节，`spd=0`）。57 自身 `spd=30`、`look=0`。**

**「裸奔」的精确机制（代码已定位，且注释自陈）：**

```17158:17158:src/train_cmd.cpp
TrackBits res_tracks = (TrackBits)(GetReservedTrackbits(tile) & DiagdirReachesTracks(enterdir));
```

- `res_tracks != TRACK_BIT_NONE` ⇒ 立即 `return { FindFirstTrack(res_tracks), CTTRF_NONE };` —— **不调 yapf、不订路，直接沿「该格已有的那点预留」走**。日志里 `doRes=0` + `why=early-haverserved` 就是它。
- 该判据用的是 `GetReservedTrackbits(tile)`：**该格上任何人的预留**，不是「本车朝这个方向的整段预留」。**一格有一个 bit，就足以让它认定「前方已铺好」。**
- 岔道格上更糟：`17187-17192`（第 329 轮 KI-504）注释自陈 —— *「岔道格（如 15,14 同时有 TRACK_X|TRACK_LOWER）上本车预留往往两根轨都有 ⇒ res_tracks 有两个 bit ⇒ `FindFirstTrack` 恒取最低位 ⇒ 车被钉在直行轨上，yapf（含 `DoTrainCouplePathfind`）根本没被调用」*。现场 15,15 `tracks=0x4 reserved=0x1`、14,15 `tracks=0x9 reserved=0x8` 正是这种「走 0x4、订 0x1」。

**⇒ 所以 57 的状态不是「无预留」，而是「有一格、但没有前瞻、且走的那条轨不在自己预留里」。** 对 PBS 而言这种车与空车无异：别人的 `ChooseTrainTrack()` 沿预留图回溯时看不到它，于是 54 合法地把 14,15 当成空路开进去 ⇒ 撞。

**这一条与「铺预留能力」无关，必须分开记：**
- 能力（`force_proceed == TFP_SIGNAL`）只跳过 `:17222` 的**限制信号检查**；
- 57 的病在 `:17158` 的**预留早退**：它把「本格有一点预留」当成「整条路铺好了」，于是既不订新预留，也不产生 lookahead。

**⇒ 结论：KI-356 那张证即便生效也救不了这个现场；真正要修的是 `:17158` 早退的判据（须要求「本车朝 `enterdir` 方向的预留足以唯一确定走线」，否则继续走 `ExtendTrainReservation` / 挂接寻路）。** KI-457（停住的挂接车）与 KI-504（行驶中的挂接车、岔道格）都已在这条捷径上打过补丁，现场说明补丁还漏了「15,15/14,15 这类相邻双岔道 + 30 速」的组合。

### 24.15 玩家判断「57 如果真进 yapf 就不会撞」——**结论成立，但落点不是「挂接寻路并入」，而是两条绕过 yapf 的捷径**

玩家原话：**「只要 veh57 掉头的时候，调用的 trypathreserve，也就是挂接寻路完全接线进入 yapf，就不会出现这种状况」**。

**判定：结论成立（57 若进 yapf，`FollowTrainReservation` 会发现前方预留属于 54 ⇒ 判不安全 ⇒ 停车等待 ⇒ 不撞）。但「接线的对象」需要修正——57 不是没被接进挂接寻路，而是 `ExtendTrainReservation` / `ChooseTrainTrack` 里有两处捷径**在调 yapf 之前就 return 了**。**

**证据一：挂接预留只铺成 1 格（静默失败，`reserved=1`）**

```1655:1671:build\R3R_debug.log
:1666 RP cur=15,14 td=3 depth=0 trk=3 bits=0x29
:1667 RP cur=15,15 td=2 depth=1 trk=2 bits=0x15
:1668 RP cur=14,15 td=0 depth=2 trk=0 bits=0x9
:1669 RP cur=13,15 td=0 depth=3 trk=0 bits=0x1
:1670 CPL-RESERVE veh=57 reserved=1 next=3
:1671 CPL-RET veh=57 tick=12560 tile=15,14 enterdir=0 tracks=0x9 res=0x0 ret=0x8 why=cpl-partial-res spd=0 real=1
```

`rp()` 规划了 4 格，`CPL-RESERVE reserved=1` ⇒ **实际只铺成 15,14 一格**，随后 `why=cpl-partial-res` 以「部分预留」返回。`RESV-WATCH` 也只出现一条：`:1784 SET-RAIL tile=15,14 track=3 actor=59 head=57`。**15,15 / 14,15 / 13,15 三格从来没有订上。**

**证据二：行进时两处「不看归属」的捷径把 yapf 架空了**

```17212:17219:src/train_cmd.cpp
/* Do we have a suitable reserved track? */
if (res_tracks != TRACK_BIT_NONE && !r3r_couple_stuck_retry && !r3r_couple_split_res) return r3r_chosen_ret(FindFirstTrack(res_tracks), result_flags, "early-haverserved");

bool mark_stuck = (flags & CTTF_MARK_STUCK);

/* Quick return in case only one possible track is available */
if (KillFirstBit(tracks) == TRACK_BIT_NONE) {
    Track track = FindFirstTrack(tracks);
```

- **14,15**（`tracks=0x9 res=0x8`）：命中 `:17213` 早退，`why=early-haverserved`（日志 `:1820`）。`res_tracks` 来自 `GetReservedTrackbits(tile)` —— **该格上任何人的预留**，不校验主人；单 bit（`KillFirstBit(0x8)==NONE`）⇒ 连 KI-504 的 `r3r_couple_split_res` 豁免都不触发（它要求「不唯一」）⇒ 早退。
- **15,15**（`tracks=0x4 res=0x1`）：`KillFirstBit(0x4)==NONE` ⇒ 命中 `:17218` 唯一轨快捷返回，**连 yapf 都不进**，直接沿几何唯一轨走。

⇒ 两格都是「调 yapf 之前就 return」。所以玩家说的「接线进 yapf」要想生效，必须**把这两条捷径的出口堵上**，而不是去改 `YapfTrainCoupleTrack`。

**证据三：54 恰恰在 15,15 上有整格预留，57 直接开了进去**

```1831:1833:build\R3R_debug.log
:1831 RESV-WATCH SET-RAIL tile=15,15 track=0 phase=consist actor=54 head=54
:1832 RESERVECONSIST veh=55 tile=15,15 track=0x1 fb=0 bits=0x1 ok=0 resNow=1
:1833 RESERVECONSIST veh=58 tile=14,15 track=0x8 fb=0 bits=0x8 ok=0 resNow=1
```

`55`（57 链第 3 节）想订 15,15 的 `0x1` ⇒ `ok=0`；`58` 想订 14,15 的 `0x8` ⇒ `ok=0`。**两格都已被别人占着，而 57 照样开了进去。** 54 的 `CRT origin=16,15 → origin=2,15`（`:1806`/`:1808`）正是 `15,15 → 14,15 → …` 这条西行路线，与格位吻合。

**要落的修法（两处，都在 `ExtendTrainReservation` 内）**

1. `:17213` 早退加前置：`res_tracks` 必须由**本车**持有（`GetTrainForReservation(tile, track) == consist`），否则不早退、继续走 yapf。
2. `:17218` 唯一轨快捷加同款前置：该格若被**别人**预留，不得走快捷，必须落到 `ChooseTrainTrack()` 让 yapf 判安全位。

**预期行为**：57 在 15,15 就会停下等 54 过去，然后按订单续行。这正是「像前面有一道 PBS 信号」该有的效果，且对 54 一侧同样成立（54 也不会开进 57 的预留）。

**边界**：这两处改动影响全部订单类型（不只挂接），属于「所有列车都变保守」的改动，须按 KI-504 的既有豁免逻辑一起复核（`r3r_couple_stuck_retry` / `r3r_couple_split_res` 的语义要重排），不能直接叠加。

### 24.16 玩家问「把挂接寻路全面接入原生预留与寻路机制，可以解决问题吗」——**不能。对 57 这个现场尤其不能，它压根没进挂接寻路**

玩家原话：**「所以，把挂接寻路全面接入原生预留与寻路机制，这可以解决问题吗」**（2026-10-27，承接 §24.13/§24.15）。

**判定：不能解决问题。理由分三层，第一层是硬事实。**

**一、57 从未进入挂接寻路，接入与否对它零影响**

§24.14 的取证已把时间线钉死：57 在 `15,14` 被 `ChooseTrainTrack()` 的「已有预留就早退」拦下（`build\R3R_debug.log:1820` `CPL-RET ... why=early-haverserved spd=30`），三次调用的 `RESV-EXT-PRE ... doRes=0`（`:1815`/`:1819`/`:1825`）证明它连**订路资格**都没有。而挂接寻路 `DoTrainCouplePathfind()` 位于 `TryPathReserveCore()` 内、在 `ChooseTrainTrack` 与 `ExtendTrainReservation` **之后**（代码锚点 `train_cmd.cpp:17428-17434`，该分支注释自陈"接近目标时普通寻路必然失败，才需要挂接寻路"）。⇒ 57 在更上游就 return 了，`CPL-ENTRY` 全日志零命中正是这个事实的直接读数。

**所以"接入状态"只决定"进了挂接寻路之后用哪套判据"，不决定"能不能进得去"。** 57 卡在门外，改门内的东西不产生任何变化。

**二、两条捷径是"原生机制自己的门"，不是挂接寻路的缺口**

```17212:17213:src/train_cmd.cpp
	/* Do we have a suitable reserved track? */
	if (res_tracks != TRACK_BIT_NONE && !r3r_couple_stuck_retry && !r3r_couple_split_res) return r3r_chosen_ret(FindFirstTrack(res_tracks), result_flags, "early-haverserved");
```

`res_tracks` 来自 `GetReservedTrackbits(tile) & DiagdirReachesTracks(enterdir)`（`:17158`）——**该格上任何人的预留**。这两处（`:17213` 早退、`:17218` 唯一轨快捷）都在 `ChooseTrainTrack()` 内，属于**原生寻路**，不是挂接寻路。把挂接订单"全面接入原生"的后果，恰恰是让它**必然**经过这两道门，与今天的表现逐字相同。

**三、真"全面接入"还有反向代价：挂接能力会归零**

`train_cmd.cpp:17428-17433` 的注释是挂接寻路存在的唯一理由：普通寻路把等待车底的平台预留当成障碍，**越接近目标越必然失败**。日志 `RESV-EXT-DEST ... bad=1`（`:17335`）就打在挂接分支之前。⇒ 消灭挂接寻路、全走原生，等于每一列挂接机车都在接近目标时 `res_dest.tile == INVALID_TILE`，挂接永远不成功。

**该做的「接入」是判据移植，不是路径合并**（这是 §24.13 表里那四样东西的落点）：

| 要移植的 | 原生锚点 | 挂接侧落点 |
|---|---|---|
| 信号判据 + `force_proceed` 豁免 | `train_cmd.cpp:17221-17222` | `YapfTrainCoupleTrack` 链内 |
| `IsSafeWaitingPosition` 安全位收口 | `:17189`（由 `FollowTrainReservation` 提供） | `rp()` 收车条件（`yapf_rail.cpp:1114`） |
| 逐格续订 + 截断 | `ExtendTrainReservation`（`:17321`） | `rp()` 的 `reserve_if_present` |
| 失败分级 `CTTRF_*` | `:17213` 起的返回位体系 | `FSCP fail=notSeg/2nd/other/tryReserve` |

**⇒ 结论（修法次序）：**
1. **先堵两条捷径**（§24.15 的两处，`res_tracks` 必须由本车持有 / 被他人预留时不得走唯一轨快捷）。这一步可独立验证，且不依赖任何"并入"工程；
2. 堵完之后 57 才会真正落到 `ExtendTrainReservation` → 挂接寻路，这时它才拿到 `FollowTrainReservation` 的前瞻（`train_cmd.cpp:17317` `origin = FollowTrainReservation(consist, nullptr, FollowTrainReservationFlag::OkayUnused)`）——**这才是"像前面有一道 PBS 信号"的机制来源**；
3. 判据移植（上表）是长期工程，且**只解判据一致性、不解放行**（§24.13 边界：`fail=other/tryReserve` 任何一种放开都等于把预留铺到别人车厢上）。

**待验证（U 项，登记后不重复取证）：**
- U-24.16-A：堵捷径后 57 是否真出现 `CPL-ENTRY` + `FSCP` 行（判据：同一掉头动作的日志里 `why=early-haverserved` 消失、`CPL-ENTRY` 出现 ≥1）。
- U-24.16-B：`:17213` 加"本车持有"前提后，**行驶中**列车通过"自己预留的唯一轨格"不得退化（负判据：正常直行线路 `TPR ok=1 res=1` 逐字不变、不出现 `resfail-extend`）。
- U-24.16-C：`GetTrainForReservation(tile, track)` 在该格同时有本车与他车预留（双槽岔道）时的返回值语义未取数——这会决定"归属校验"能否直接用单值比较。

**状态**：本轮为方案问答，未改源码、未构建；未新增 KI（非新缺陷，属 §24.15 修法口径的澄清）。

## 25. 第 343 轮（2026-10-27）：堵两处捷径——落地 §24.16 修法 1

玩家口径：**「好，先堵捷径吧」**（承接 §24.16，只做修法 1，判据移植留待后续）。

### 25.1 改什么（精确到判据）

`ChooseTrainTrack()`（`src\train_cmd.cpp:17093` 起）里两处绕过 yapf 的出口，都只看「该格有几个预留 bit」，从不看「这点预留是不是本车的」：

| 行 | 现状判据 | 病症 |
|---|---|---|
| `:17213` | `res_tracks != TRACK_BIT_NONE` ⇒ `return { FindFirstTrack, CTTRF_NONE }` | 别人的一格预留即视为「整条路已铺好」，不订路、不产 lookahead |
| `:17218` | `KillFirstBit(tracks) == TRACK_BIT_NONE` ⇒ 唯一轨快捷 | 唯一轨被别人预留时同样不查归属直接走 |

`res_tracks` 来自 `GetReservedTrackbits(tile) & DiagdirReachesTracks(enterdir)`（`:17158`）——**该格上任何人的预留**。

### 25.2 新增判据

```
r3r_cpl_order_here  = OT_GOTO_COUPLE && !GetCoupleIsDepot()
r3r_couple_foreign_res =
    r3r_cpl_order_here && res_tracks != NONE &&
    !(res_tracks 中至少一根轨的预留主人是本车)
```

主人查询按格型分流（**不能一律用 `GetTrainForReservation()`**——它的首行 `assert_msg_tile(HasReservedTracks(...))` 走 plain rail 路径，站台/路点整格位会断言，正是 KI-207c 的教训）：

- `IsPlainRailTile(tile)` ⇒ 逐 bit 调 `GetTrainForReservation(tile, track)`；
- `IsRailStationTile(tile)`（含路点）⇒ 调 `GetTrainForWholeTileReservation(tile)`；
- 其他格型（车库/道口/隧道桥）⇒ `owned = true`，保持原判据不动。

`nullptr` 主人（stray 预留）按**非本车**处理 ⇒ 不吃捷径（保守）。

两处出口各加 `&& !r3r_couple_foreign_res`。

### 25.3 作用域（刻意收窄）

只覆盖**挂接订单 + 非车库**，与既有两条豁免（`r3r_couple_stuck_retry` `:17185`、`r3r_couple_split_res` `:17194`）同域。**全部非挂接订单逐字不变**——这样 §24.16 的 U-24.16-B（负判据）由构造保证，无需额外实测；"所有列车都变保守"的回归风险不在本轮引入。

### 25.4 探针

新增 `RESV-FOREIGN-RES veh=%d tile=%d,%d enterdir=%d res=0x%X tracks=0x%X spd=%d real=%d`，按 `(veh, tile)` 逐格去重（与 `RESV-EXT-PRE`/`RESV-SPLIT-PASS` 同款）。判读：该行出现 ⇒ 归属判据生效并拦下了 shortcut。

### 25.5 待复测

- **V-343-A**：57 同类场景（挂接机车 + 前方格被人预留）⇒ 出现 `RESV-FOREIGN-RES veh=57`，且随后出现 `CPL-ENTRY`（真进挂接寻路）与 `RESV-EXT-PRE ... doRes=1`；不再出现 30 速裸奔至相撞。
- **V-343-B**：同一场景不得退化为「永久订不出路」——若出现连续 `CPL-SAFE-FAIL`/`CPL-PATHFOUND found=0` 且车完全不动超过 `path_backoff` 数十次，说明归属判据把必要的「沿他人预留跟随」情形也堵死了，需按 `IsRailStationTile` 分支回退。
- **V-343-C**：正常直行/进站/库内调度（非挂接订单）逐字不变：`TRP ok=1 res=1` 序列、`CPL-*` 无新增、无 `RESV-FOREIGN-RES`。
- **V-343-D**：挂接机车经过**自己**预留过的格仍走 early-haverserved（即 `why=early-haverserved` 不归零），否则等于把行驶中的挂接车全推去 yapf，性能与行为都要重估。

### 25.6 构建自证

- 复用既有 `_tmp_inc_build.cmd`（未新建任何 `.cmd`）；护栏 `build\R3R_incbuild.guard.log` = `GUARD: incremental is safe (no header/lang file is newer than the newest object)`（本轮只改 `.cpp`，增量合法）。
- `build\R3R_incbuild.log` 尾部：`[2/3] Building CXX object CMakeFiles\openttd_lib.dir\src\train_cmd.cpp.obj` → `[3/3] Linking CXX executable openttd.exe`。
- `build\R3R_incbuild.done` = `EXIT_CODE=0`（@23:03:20）。
- 时间戳链：`src\train_cmd.cpp` 23:00:04 → `train_cmd.cpp.obj` 23:01:02 → `build\openttd.exe` 23:02:58（52 322 304 B）。
- 日志错误计数 `error C\d|fatal error|FAILED:|build stopped` = **0**；`read_lints` 0 条。
- 新增字符串 `RESV-FOREIGN-RES` 是源文件字面量，编译链接成功即已入 exe（未另做 findstr 二进制核对，因其引号转义在本机 shell 下失败、不据此断言）。

### 25.7 状态

已实现 + 已编译（内测树 `build\openttd.exe` @2026-10-27 23:02:58）。游戏内复测待做，判据见 §25.5 的 V-343-A ~ V-343-D。发布树未编（按 2026-10-19 口径，仅在明确要求发布时才编）。

## 26. 第 344 轮（2026-10-27）：玩家质疑「57 真的没有调用 TryPathReserve 吗」——**质疑成立，我上一轮表述错误**

### 26.1 输入

`D:\temp\codebuddy-dropped-files\46dd9a71-...\R3R_debug.log`：253 958 B / 1 920 行。**含第 343 轮探针**（`RESV-FOREIGN-RES` 计数 = 1，见 `:1701`）⇒ 这是刚编出来的 exe 跑出来的采样，不是旧日志。

### 26.2 结论 1：57 调用过 `TryPathReserve`，而且很多次

`TRP` 就是 `TryPathReserve` 的出口探针，veh=57 命中 9 行：

| 行 | 读数 |
|---|---|
| `:393` | `ok=1 res=1`（成功订到预留） |
| `:1233` | `ok=0 res=0` |
| `:1326` | `ok=0 res=0`，`one=1 fto=1 look=0` |
| `:1371`/`:1413`/`:1463`/`:1510` | 同上 |
| `:1568` | `ok=1 res=1 look=1` |
| `:1790` | `ok=0 res=0` |

还有 `TPR-ZERO-REACH`（`:1299`）、`TPR-WALK`（`:1300`）、`TPR-EXIT`（`:1301`）、`RESV-EXT-PRE`（`:1302`/`:1696`/`:1700`/`:1760`）、`LOCO-MOVE`（`:1699`）—— 这一整套都是 `TryPathReserve` 内部/下游的探针。

**⇒ 更正 §24.16 与上一轮回复的表述**：我说的「57 连订路资格都没有」是错的。「`CPL-ENTRY` 零命中」只在**旧日志**（第 336 轮那份）成立，它证明的是"没进挂接寻路入口"，**不等于**"没调用 `TryPathReserve`"。正确的层级关系是：

```
TryPathReserve()                       ← 57 一直在调（TRP 行）
 └─ ExtendTrainReservation()           ← RESV-EXT-PRE 在这里
 └─ ChooseTrainTrack()
     ├─ :17213 早退 early-haverserved  ← 旧日志在这里 return，所以到不了下面
     └─ DoTrainCouplePathfind()        ← CPL-ENTRY 在这里（旧日志零命中）
```

### 26.3 结论 2：第 343 轮的归属判据**按预期生效**

时间线（同一 tick 段，tick=12714）：

```
1696: RESV-EXT-PRE veh=57 tile=15,15 enterdir=1 tracks=0x4 reserved=0x1 doRes=0 spd=30   ← 本格预留与本方向可达轨不相交 ⇒ res_tracks 为空，归属判据不参与
1699: LOCO-MOVE veh=57 tick=12714 tile=15,14 j=272 spd=30 track=08 dir=6 db=1
1700: RESV-EXT-PRE veh=57 tile=14,15 enterdir=0 tracks=0x9 reserved=0x8 doRes=0 spd=30
1701: RESV-FOREIGN-RES veh=57 tile=14,15 enterdir=0 res=0x8 tracks=0x9 spd=30 real=1   ← 新判据命中：0x8 不是本车的
1702: CPL-ENTRY veh=57 tile=15,14 orderType=16 dontReserve=0                            ← 因此没被 :17213 早退，进了挂接寻路
```

对照第一次（tick=12460）：`:1302 RESV-EXT-PRE ... reserved=0x0 doRes=1` → `:1303 CPL-ENTRY`（预留为空本来就不会早退）。

**⇒ V-343-A 通过**：`RESV-FOREIGN-RES` 出现且紧跟 `CPL-ENTRY`，`why=early-haverserved` 在本段彻底消失。
**⇒ V-343-B 通过**：没有退化成"永久订不出路"——57 掉头后能加速到 30 并前进（`:1699 j=272 spd=30`），不是原地死锁（`:1328` 那次 `spd=0` 是掉头后零长度预留所致，见 26.5）。

### 26.4 结论 3：暴露出**第二层阻塞**——`FSCP fail=tryReserve`（新 KI-358）

```
1721: FSCP tile=14,15 fail=tryReserve td=0 want=0 type=1 tbits=0x9 res=0x8 plain=1 depot=0 st=0
1722: FSCP-RES tile=14,15 tk=3 owner=54 otile=17,16 odir=5 ospd=100 ostop=0 ord=2 oroi=2 oend=1,15 oendtd=8 othru=1
1723: FSCP-REACH tile=14,15 veh=27 ord=0 roi=0 pos=1,15 dir=5 spd=0 stopped=1 seg=1
1724: FSCP-REACH tile=14,15 veh=54 ord=2 roi=2 pos=17,16 dir=5 spd=100 stopped=0 seg=1 end=1,15 endtd=8 isGtr=1
1725: CPL-SAFE-FAIL veh=57 tile=5,15
1726: CPL-PATHBUG veh=57 alock=0 found=0 rej=0x1 dest=12,15 dist=3 spd=0
1727: CPL-RET veh=57 tick=12749 tile=14,15 enterdir=0 tracks=0x9 res=0x8 ret=0x1 why=cpl-fail spd=0
```

读法：57 的挂接寻路已经**完整跑通**（`:1716 CPL-PATHFOUND found=1`、`:1717 CPL-SCAN ... consist=5`、`:1719 CPL-BEST tile=5,15`），但在回溯补预留时，路径要穿过 `14,15`，而该格的 `TK=3` 被 **veh=54** 持着（54 正以 100 的速度开往 depot `1,15`）。`FSCP` 的安全位检查判"这格不能进"，于是 `CPL-SAFE-FAIL` → `COUPLE-FAIL`（`:1728`）→ 57 停在 `14,15` 前的 `15,14`。

**这是正确行为**（不该把预留铺到别人车身上，正是 §24.13 边界写的"`fail=other/tryReserve` 任何一种放开都等于把预留铺到别人车厢上"），但**它把 57 卡死了**：57 已经在 `15,14`，前方一格永远被 54 的预留占着（54 走完这段才会放），而 57 是 `GOTO_COUPLE` 订单、`COUPLE-WAIT-OFFDEST`（`:1735`，`destTile=1932` 但 57 不在目标格）⇒ `ProcessOrders` 不为它推进，`STUCK=1`（`:1738`）。

⇒ 修法方向（待拍板，**不在本轮动手**）：交接格"对方正在路过"时，57 应退到**安全位**等待（把 `IsSafeWaitingPosition` 的收口从"路径能否穿过"改成"停在哪儿等"），而不是停在对方预留的最后一格上。

### 26.5 结论 4：掉头后「零长度预留」是上一条的前因

```
1290: REVERSEDIR veh=57 tile=16,14 dir=5 order=16 spd=0 nv=3 rev=0 stuck=0 db=0
1298: R3R-RES-ONCE grant head=57 why=turn force_proceed=0 tile=16,14
1299: TPR-ZERO-REACH veh=57 mf=16,14 origin=16,14 td=0 okay=1 spd=0 real=1 order=16 look=0 turn=1   ← origin == mf
1300: TPR-WALK veh=57 mf=16,14 origin=16,14 td=0 okay=1 exit=0 new=15,14 reach=9 spd=0 one=1 look=0
```

掉头瞬间车头格 == 预留起点格 ⇒ `ExtendTrainReservation` 只订了 0 格 ⇒ `look=0`、`one=1`（`ForceReserveOnce` 被授一次）。于是 57 手上没有任何前瞻，只能靠每 tick 重订，而重订又落到别人的格上。这与 `:1565 CPL-RET ... ret=0x8 why=cpl-partial-res`（部分预留）是同一条线的两个读数。

**⇒ U-344-A（登记，不重复取证）**：`R3R-RES-ONCE grant ... why=turn` 这条一次性预留与"零长度"的关系仍需确认——若去掉 `why=turn` 的授予，57 是否会在掉头后走常规预留并拿到真实前瞻？

### 26.6 判定汇总

| 判据 | 结果 | 依据 |
|---|---|---|
| 玩家质疑「57 没调用 TryPathReserve」 | **质疑成立** | 9 条 `TRP veh=57`、`TPR-*` 4 条 |
| V-343-A（堵捷径生效） | **通过** | `:1701 RESP-FOREIGN-RES` → `:1702 CPL-ENTRY` |
| V-343-B（未退化为死锁） | **通过** | `:1699 spd=30` 前进 |
| V-343-C（非挂接不变） | 未单独核对（本段无对照样本） | — |
| V-343-D（本车预留仍走 early-haverserved） | 未取样（本段两次都不满足条件） | — |
| 第二层阻塞 | **新增 KI-358** | `:1721 FSCP fail=tryReserve` / `:1722 owner=54` |

### 26.7 状态

纯取证，本轮未改源码、未构建。KI-357 状态改为「已实现 + 已编译 + 复测确认生效」，新增 KI-358（`fail=tryReserve` 交接格死锁，修法待拍板）。

## 27. 第 345 轮（2026-10-27）：玩家问「既然 57 调用了 TryPathReserve，为什么不能把挂接寻路改走普通寻路接线」

**玩家原话**：「既然 57 调用了 trypathreserve，但是寻路走的是挂接寻路导致出的问题，那我们为什么又不能把挂接寻路改到普通寻路接线了？」

**结论先行：不能切，而且切了也治不到这次的病。** 三条硬理由 + 一条现场事实，全部有代码/日志锚点。

### 27.1 前提校正：挂接寻路**就是**普通寻路，两者只差「终点函数」

不是两套寻路器，是同一个 YAPF 家族的不同实例：

| 层 | 普通寻路 | 挂接寻路 |
|---|---|---|
| 入口 | `YapfTrainChooseTrack()`（`src\pathfinder\yapf\yapf_rail.cpp:1585`） | `YapfTrainCoupleTrack()`（同文件 `:1963`） |
| 实现 | `CYapfRail / CYapfRailNo90::stChooseRailTrack` | `CYapfCoupleRail / CYapfCoupleRailNo90::stFindNearestCoupleTrain` |
| 调用点 | `DoTrainPathfind()`（`src\train_cmd.cpp`，包 `YapfTrainChooseTrack`） | `DoTrainCouplePathfind()`（`src\train_cmd.cpp`，包 `YapfTrainCoupleTrack`） |
| 终点 | 订单目的地 tile / 下一安全位（`CYapfDestinationAnyDepotRailT` 等） | `CYapfDestinationTrainRailT`：只接受满足挂接条件的等待车底 |

节点、轨道跟踪、`rp()` 回溯预留、代价表全是同一套基类代码。**「改到普通寻路」的真实含义 = 换掉终点函数 = 把 `CYapfDestinationTrainRailT` 删掉**。

### 27.2 为什么不能删这个终点函数（三条）

**(a) 普通寻路没有目的地可去。** GOTO_COUPLE 的 `dest_tile` 在配对成功前不是"车底所在格"：车站型是车站/站台、路点型是路点、车库型才是有效坐标（这正是 `:17502` 的 `r3r_couple_depot` 敢直接用普通寻路的原因——车库型 `dest_tile` 是 `Depot::Get(id)->xy`，见 `:17498` 注释）。而**车站型**恰好是 57 的现场：车底 veh=54 停在 `15,15`/`14,15` 一带，普通寻路到站台就算"到达"，会停在离车底十几格的地方不动——就是 `COUPLE-WAIT-OFFDEST`（`:1735`，`destTile=1932 onStnTile=0`）那个症状，不是修复。

**(b) 普通寻路把等待车底当障碍，必然 `path_found=false`。** 代码自己写着（`src\train_cmd.cpp:17476-17482`）：

> use the couple pathfinder to follow the waiting train's reservation. This lets the locomotive reserve up to the consist's own reservation (**which the normal pathfinder treats as an obstacle and fails on**).

以及 `:17394-17397`：

> 挂接寻路存在的**唯一理由**就是"普通寻路把等待车底的平台预留当成障碍、必然失败"。

等待车底站着的那一格带**整格预留**（站台位）+ 车本身，普通 YAPF 判定不可进入 ⇒ 搜不到。这不是可调参数，是终点语义决定的。

**(c) 预留覆盖范围不同。** 挂接寻路的回溯预留要一路铺到"车头贴上车底"（合并链长度），普通寻路的 `rp()` 到下一安全位/信号就停（`:17445-17455` 的 `res-ok-brief` 快速出口）。切过去等于放弃那一段预留，挂接瞬间又回到 `CheckTrainCollision` 撞车。

### 27.3 更关键：这次的问题不在寻路器这一层

第 343 轮的日志已经证明**接线是通的**：

```
1700: RESV-EXT-PRE veh=57 tile=14,15 enterdir=0 tracks=0x9 reserved=0x8 doRes=0 spd=30
1701: RESV-FOREIGN-RES veh=57 tile=14,15 res=0x8 tracks=0x9
1702: CPL-ENTRY veh=57 tile=15,14 orderType=16            ← 进了挂接寻路
1716: CPL-PATHFOUND veh=57 found=1                        ← 找到路径了
1719: CPL-BEST veh=57 tile=5,15 td=0 cost=10584
1721: FSCP tile=14,15 fail=tryReserve tbits=0x9 res=0x8    ← 卡在这里
```

`CPL-PATHFOUND found=1` 说明挂接寻路**已经完整跑通**。失败点在**路找到之后、提交预留之前**的 `FSCP` 安全位检查：路径必须穿过 `14,15`，而那格的 `TK=3` 被 veh=54 持着（54 正以 100 速开往 depot `1,15`）。

⇒ **换任何寻路器都会在同一个地方失败**，因为拒绝发生在 `IsSafeWaitingPosition` 系（`FSCP` 家族），与"用哪个 YAPF 实例找路"无关。切到普通寻路只会让 `found=1` 变成 `found=0`，先丢一个能力，再撞同一堵墙。

### 27.4 玩家想法里正确的那一半：确实是「两段寻路接力」

`ChooseTrainTrack()` 的顺序是：

1. `:17369` `ExtendTrainReservation()` —— 先按**普通逻辑**铺一段（`RESV-EXT-PRE` 探针就在这里）；
2. 若这一段到了安全位且不长 ⇒ `:17454` `res-ok-brief` **直接返回**，根本不进挂接寻路；
3. 否则 `:17518` 起才进挂接分支，`YapfTrainCoupleTrack()` 再算一遍。

同一段路被两套逻辑各算一遍，是有代价的（§24.13 的「逐格续订截断」就是为了收口这个）。若目标是"只留一套"，正确方向是**让挂接寻路在进入挂接订单的那一刻接管整段预留**（截断普通预留 + 由挂接寻路全权重铺），而不是把挂接订单交给普通寻路。

### 27.5 建议

- **不改接线**（否决玩家提的方案），理由见 27.2/27.3。
- 下一步按 **KI-358** 修 `IsSafeWaitingPosition` 的停等收口：57 在 `14,15` 前应退到安全位等 54 过去，而不是停在对方预留的最后一格上等死。
- 若玩家更想先做 27.4 的"单套预留接管"，那是 §24.13 的「逐格续订截断」项，工作量比 KI-358 大，且不解决本次死锁（本次是别人的预留，不是自己的预留重叠）。

**状态**：本轮为设计问答，未改源码、未构建。

## 28. 第 346 轮（2026-10-27）：玩家问「57 都调了 TryPathReserve，为什么还是没 lookahead 裸奔」

### 28.1 问题拆解

「调了 `TryPathReserve`」与「铺出了前瞻预留」是两件事。前者只是**尝试**，`ExtendTrainReservation` 可以从起点格起就返回 0 格，此时 `TRP` 探针会打出 `ok=0 res=0 look=0`——**函数跑了、预留没铺**。所以真问题是：**在什么条件下 `ExtendTrainReservation` 会以「零格」收场。**

### 28.2 猜测根因（待验证，先落盘）

**H1（主猜）：起点格「已有预留」被当成早退条件，而掉头时起点格恒被自己的预留占着。**

掉头瞬间 `:1298 R3R-RES-ONCE grant head=57 why=turn force_proceed=0 tile=16,14` 先给起点格订了一笔（一次性授权），紧接着 `:1299 TPR-ZERO-REACH veh=57 mf=16,14 origin=16,14 td=0 ... look=0 turn=1`。若 `ExtendTrainReservation` 的入口判据是「起点格 reserved != 0 就 return」，那这笔"帮忙"的预留恰好把前瞻锁死：每 tick 重订、每 tick 撞自己的预留、每 tick 返回 0 格。

**H2：第 343 轮的归属判据只放行「别人的预留」，没放行「自己的预留」。**

第 343 轮把「预留位非空就早退」改成「预留位属于本车才早退」。⇒ 起点格若是**自己**订的，判据仍然判"该早退"。这与 `why=turn` 授予的语义直接冲突：授予的目的一边要"车能动"，一边又让下一 tick 因"自己占着"而拿不到前瞻。

**H3（次猜）：`origin == mf` 时，就算不早退也只有 0 格可订。**

`:1299 mf=16,14 origin=16,14` 两格相同 ⇒ 预留起点就是车头所在格 ⇒ 能延伸出去的只有"车头格自己"，`reach` 再大也不产生前瞻。这解释了为什么 `:1300 TPR-WALK reach=9`（跟随器探了 9 格）却 `look=0`。

### 28.3 验证判据（待取证）

- **V-346-A**：读 `ExtendTrainReservation` 入口早退条件——是否区分「预留起点格」与「路径中间格」；`doRes=0`（`RESV-EXT-PRE` 行里的字段）究竟由谁决定。
- **V-346-B**：读 `R3R-RES-ONCE` 的授予点——它订的是哪一格（起点格还是车头前进格）、是否登记到本车 `res_tracks`，从而让下一 tick 的早退判据命中。
- **V-346-C**：日志侧对照 `:1326 TRP ok=0 res=0 one=1 fto=1 look=0` 与 `:1568 TRP ok=1 res=1 look=1`——同车两种结果，差别应落在「起点格归属」上。

### 28.4 结论（取证后回填）

**H1 证伪、H2 部分成立、H3 只是表象。真因是第四条：`lookahead` 只在「真的订成了」时才填。**

#### (1) `R3R-RES-ONCE ... why=turn` 不订任何格

`src\train_cmd.cpp:18456-18468`：七条门禁全过才 `consist->SetForceReserveOnce()`——**只设 flag（`ForceReserveOnce`，bit27），一格预留都不订**。所以"它把起点格订上、导致下一 tick 早退"不成立。

#### (2) 57 没有命中任何「禁止订路」门禁

日志全文 `RESV-NOBOOK` 只有 **1 条**，是 veh=54（`:331 reason=depot-paired`）；`veh=57` 零条，也没有 `RESV-NOBOOK-LOOKAHEAD`（`src\train_cmd.cpp:18203`）。
⇒ lookahead 不是被「禁止订路」清掉的（该分支 `:18199-18210` 才会 `lookahead.reset()`）。

#### (3) `TPR-WALK` 字段确认（`src\train_cmd.cpp:18859`）

```
TPR-WALK veh=%u mf=%u,%u origin=%u,%u td=%u okay=%d exit=%u new=%u,%u reach=%u spd=%d one=%d look=%d
```
`mf` = `GetMovingFront()->tile`（车头格），`origin` = 预留推进起点，`one` = `HasForceReserveOnce()`，`look` = `lookahead != nullptr`（**进入时的状态，不是结果**）。

现场 `:1300 TPR-WALK veh=57 mf=16,14 origin=16,14 td=0 okay=1 exit=0 new=15,14 reach=9 spd=0 one=1 look=0`：
`mf == origin == 16,14`，下一格 `15,14`，`reachable=0x9`（`TRACK_X|TRACK_Y` 都是岔道可选）——**不是没路可走**。

#### (4) 真因：`FillTrainReservationLookAhead()` 只在 `CTTRF_RESERVATION_MADE` 时调用

`src\train_cmd.cpp:18875-18889`：

```cpp
TryPathReserveResultFlags result_flags = TPRRF_NONE;
if (reachable != TRACK_BIT_NONE) {
    ChooseTrainTrackResult result = ChooseTrainTrack(consist, new_tile, exitdir, reachable, CTTF_FORCE_RES | ...);
    if (result.ctt_flags & CTTRF_RESERVATION_MADE) result_flags |= TPRRF_RESERVATION_OK;
    ...
}
if ((result_flags & TPRRF_RESERVATION_OK) == 0) {
    if (depot) SetDepotReservation(..., false);
    return result_flags;              // ← 直接返回，lookahead 一格不填
}
...
FillTrainReservationLookAhead(consist);   // :18819 —— 只有走到这里才有前瞻
```

**⇒ 「`TryPathReserve` 被调用」与「有 lookahead」是两个独立事实。前者只是尝试，后者要求 `ChooseTrainTrack` 明确回报 `CTTRF_RESERVATION_MADE`。**

57 的实际路径：
```
:1300 TPR-WALK  new=15,14 reach=0x9
  → ChooseTrainTrack(57, tile=15,14, exitdir=0, tracks=0x9, ...)
  → :17261  if (res_tracks != NONE && !stuck_retry && !split_res && !foreign_res)
              return { FindFirstTrack(res_tracks), CTTRF_NONE };   // early-haverserved
  → CTTRF_NONE 不含 CTTRF_RESERVATION_MADE
  → :18885 直接 return  ⇒  lookahead == nullptr  ⇒  look=0  ⇒  裸奔
```

#### (5) `early-haverserved` 的隐含假设是错的

这个出口的假设是「**这格已有预留 ⇒ 说明前瞻早在手上**」。但有三类预留**不产生 lookahead**：

| 预留来源 | 是否填 lookahead | 代码锚点 |
|---|---|---|
| `ChooseTrainTrack` 成功订路 | 是 | `:18819` |
| `R3R-RES-ONCE`（只设 flag） | 否（不订格） | `:18456-18468` |
| `ReserveTrackUnderConsist()`（脚下保护，每 tick 重订） | **否** | `:19073` |
| 掉头/解挂前残留的位 | 否 | `FreeTrainTrackReservation` 只清路径 |

第 343 轮只堵了「**别人的**预留」这一种（`r3r_couple_foreign_res`），「自己的、但不是 lookahead 带来的预留」照样触发早退。并且该出口**不做 `MarkTrainAsStuck`**（`:17171` 注释自陈 `CTTRF_NONE` 不 mark）⇒ 车既不订路也不停，就是裸奔。

#### (6) 修法方向（不在本轮动手）

`early-haverserved` 出口必须自证「前瞻已在」才允许早退，否则继续往后走：

- **方案 P1（最小）**：`:17261` 的条件加 `consist->lookahead != nullptr`。没有前瞻就不早退，往下走正常 YAPF / 挂接寻路，从而拿到 `CTTRF_RESERVATION_MADE` 并填前瞻。
- **方案 P2（保守）**：早退前显式 `FillTrainReservationLookAhead(consist)`，再用 `res_tracks` 作终点。风险=前瞻起点可能与该格不连续。
- 推荐 **P1**：语义单一（"有预留"不等于"有前瞻"），且天然覆盖脚下保护/RES-ONCE/残留三类来源。

**判据 P-346**：修后同场景应出现 `TPR-WALK ... look=1` 或 `TRP veh=57 ok=1 res=1 look=1`，且 `:1300` 那种 `one=1 look=0` 消失；负判据=真·已有前瞻的重订不能退化成每 tick 全长重算（用 `TRP-SUMMARY`/`RESV-EXT-PRE` 频次对比）。

### 28.5 状态

纯取证 + 设计判断，本轮未改源码、未构建。H1 已证伪并记录，真因指向 `:17261` 早退出口缺少「前瞻存在」前置判据（归入 KI-358 同族，见 §28.6）。

### 28.6 与 KI-358 的关系

两者是同一条链上的前后两环，**不是两个独立 bug**：

```
lookahead == nullptr（本条）
  → 车每 tick 只能走一格，靠 TryPathReserve 重订
  → 重订撞上 veh=54 的交接格 14,15
  → FSCP fail=tryReserve（KI-358）
  → CPL-SAFE-FAIL → COUPLE-FAIL → 停死
```

⇒ **先修本条（补前瞻），KI-358 的触发频率会大幅下降**（有前瞻时车会沿既有预留行驶，不必每格重订）；但 KI-358 本身仍需修，因为"交接格被别人占"在任何有前瞻的车身上也会发生。建议顺序：P1 → 观察 → 再决定 KI-358。

## 29. 第 346 轮续（2026-10-27）：玩家追问「把挂接寻路改成终点=配对车所在格的普通寻路 + 给配对车预留打 paired flag 视作空气，这样不就可以了？之前为什么否决我」

### 29.1 先更正我自己：上一轮否决的不是这个方案

玩家原话：「我们把挂接寻路变成终点为配对车所在格的普通寻路，给配对车的预留打一个 paired 的 flag 让它对挂接列车视作空气，这样不就可以了？之前为什么否决我」

**要认的错**：上一轮（§27）我否决的是「保持现有语义不变、直接把挂接订单交给普通寻路」，理由 (b) 是「普通寻路把等待车底的整格预留当障碍，必然 path_found=false」。玩家现在补的 **paired flag 正好就是解 (b) 的那把钥匙**——承认这一点，(b) 单独不再成立。⇒ 上一轮那句"换任何寻路器都会在同一处失败"对**这个新方案而言是打偏的**。

### 29.2 但这个 flag 第 343 轮已经打过了，叫 `RESV-FOREIGN-RES`

第 343 轮在 `ExtendTrainReservation` 里加的 `r3r_couple_foreign_res` 就是同一个思路：遇到"不是本车但属于配对对象的预留"时不再早退。日志实证：

```
1700: RESV-EXT-PRE veh=57 tile=14,15 enterdir=0 tracks=0x9 reserved=0x8 doRes=0 spd=30
1701: RESV-FOREIGN-RES veh=57 tile=14,15 res=0x8 tracks=0x9      ← 玩家的 paired flag，已跑
1702: CPL-ENTRY veh=57 tile=15,14 orderType=16                    ← 因此放行进了挂接寻路
1716: CPL-PATHFOUND veh=57 found=1                                ← 因此找到路
1721: FSCP tile=14,15 fail=tryReserve tbits=0x9 res=0x8           ← 仍然死在这
```

⇒ 玩家的机制**不是新想法，而是已经在代码里跑通的既成事实**，并且它确实成功把 57 从"早退"放进了挂接寻路（`:1702`）。**但它没有救下这一次，这是最有说服力的一条：flag 那层已经不是瓶颈了。**

### 29.3 失败点在识别层之后：`FSCP` 问的不是"这格预留是谁的"

`:1721 FSCP fail=tryReserve` 来自 `IsSafeWaitingPosition` 系。它判的是「我停在这一格安不安全」，输入是**几何 + 该格车体占用 + 预留状态**，不是"预留归属车"。⇒ 给预留打 flag 无论怎么打，都到不了这个判据里；要让 flag 生效必须**改 FSCP 的判据**（把"配对车的预留/车体"算作可进入），那是另一处改动，与"换寻路器"无关。

### 29.4 三条仍然不成立的理由（修正版）

**(a) 终点函数删不掉，只能改——工作量与保留挂接寻路等同。**
「终点=配对车所在格」正是 `CYapfDestinationTrainRailT` 的语义。普通寻路的默认终点是 `dest_tile`（车站型＝站台，不是车底所在格）。要让它认"配对车所在格"，就得在 `CYapfRail` 上挂一个**新终点函数**——那就是 `CYapfDestinationTrainRailT` 换个写法。⇒ "改到普通寻路"在工程上等于"改写同一个终点函数"，没有省下任何东西。

**(b) 配对车 veh=54 此刻正在以 100 速逃离，不是停着等挂接。**
`:1735 COUPLE-WAIT-OFFDEST veh=57 ... destTile=1932 onStnTile=0` + `:1298` 一带显示 54 正开往 depot `1,15`。把 54 的预留视作空气，等于允许 57 直接冲进一辆**高速撤离车**的走行路径。⇒ flag 在"54 停着等"的场景是对的，在本次场景会把"停死"升级成"撞车"。

**(c) flag 只作用于预留，不作用于车体。**
`CheckTrainCollision` 看的是车在不在格上，不看预留属于谁。要让 57 贴上去，还必须同时豁免**车体碰撞**，并且让 57 在接触前把速度降到 0（现有挂接语义就是"减到 0 再搭接"）。⇒ paired flag 不是一把钥匙，是一串。

### 29.4b 取证修正（关键，覆盖 29.2/29.3 的偏差）

**修正 1：玩家说的 paired flag 确实存在，但名字是 `R3RIsCoupleTarget`。**

`src\pathfinder\yapf\yapf_rail.cpp:119` `FindSafeCouplePositionProc()`，`:131` 起：

```cpp
if (HasReservedTracks(tile, tracks)) {
    ... 在格上找车 head ...
    if (best->index == Yapf().GetVehicle()->index) return true;   // :160 自己的格
    if (!R3RIsCoupleTarget(best)) { ...FSCP fail=notSeg...; return false; }   // :165
    if (!R3RCoupleAllowed(...)) return false;                     // :179
    if (second_best != nullptr) { ...FSCP fail=2nd...; return false; }        // :180
    FollowTrainReservation(best, &other_train);                   // :189
    if (other_train != nullptr && other_train != best) { ...FSCP fail=other...; return false; }
}
```

`:165` 就是玩家要的「配对车的预留对挂接列车视作空气」——**早就实现了**，而且是**实时查「这格上的车是不是我的挂接目标」**，比打 flag 更准（不用维护 flag 与预留的生命周期同步）。⇒ 方案本身不新，代码里跑了很久。

**修正 2：本次失败点不在这个分支里，而在它的 else-if 兄弟分支。**

`:199`：

```cpp
} else if (GetReservedTrackbits(tile) != TRACK_BIT_NONE) {
    if (!TryReserveRailTrack(tile, TrackdirToTrack(td))) {
        ... FSCP tile=14,15 fail=tryReserve td=%d want=%d type=%d tbits=0x%X res=0x%X ...
```

现场 `:1721 FSCP tile=14,15 fail=tryReserve tbits=0x9 res=0x8`，读法：
- `tbits=0x9` = 该格铺了 `TRACK_X|TRACK_Y`（岔道格）；
- `res=0x8` = 当前被预留的是 **`TRACK_Y`**（veh=54 的）；
- 走进 `:199` 分支说明 `HasReservedTracks(tile, tracks)` 为 **false**，即 **57 想走的那根轨不在被预留的轨里** ⇒ 57 要 `TRACK_X`，54 占着 `TRACK_Y`；
- `TryReserveRailTrack(14,15,TRACK_X)` 失败，因为 `TRACK_X` 与 `TRACK_Y` 在这格**交叉**，PBS 不允许交叉预留。

⇒ **真因不是"别人的预留挡路"，而是"两条交叉的轨不能同时预留"。** 这与「是不是配对车」无关——是防侧撞的物理规则。即使把 54 的预留打上 paired flag 视作空气、即使 57 完全无视它，X 与 Y 仍在格心交叉，两车照样相撞。

**修正 3：`r3r_couple_foreign_res`（`train_cmd.cpp:17227-17261`）不是 paired flag，作用相反。**

它判 `res_tracks` 里有没有一根轨的预留主人是**本车**（`owner->First() == consist->First()`）；没有就置 `foreign_res = true`，从而**不吃** `:17261` 的 `early-haverserved` 早退捷径，改走 `ExtendTrainReservation()` + YAPF。即「遇到别人的预留就不信捷径、让寻路器自己算」，不是「允许穿越别人的预留」。⇒ 29.2 说"这个 flag 已经打过了"不准确，更正为此。

### 29.5 结论（修正后）

- **玩家的方案不是坏方案，而且它的主体已经在跑**（`:165 R3RIsCoupleTarget`）。这也是上一轮我否决时唯一没看清的点，认。
- 但它治不到本次现象：本次失败在 `:199` 的「交叉预订」分支，`res=0x8` vs 需求 `TRACK_X`，是**几何交叉**而非归属问题。flag 改不了交叉。
- 真要 57 过去，只有两条路：①让 54 放开 `TRACK_Y`（等它走完 —— 正是 **KI-358** 该做的"退到安全位等"）；②让 57 的路径改用 `TRACK_Y`（它的路径规划选的就是 X，需改 YAPF 代价，不现实）。
- 因此施工顺序不变：**KI-359 P1**（`:17261` 早退加 `lookahead != nullptr`，让 57 有前瞻、能沿预留行驶而不是每格裸奔重订）→ 观察 → **KI-358**（`IsSafeWaitingPosition` 收口，让 57 在 `14,15` 前停等）。

**状态**：本轮为设计问答 + 取证，未改源码、未构建。

## 30. 第 346 轮续二（2026-10-27）：玩家问「你打算怎么让 57 等 54」

### 30.1 把「等」拆成四个必须同时成立的环节

| 环节 | 要做的事 | 现状 |
|---|---|---|
| A 判据 | 分清「路被别的车的交叉预留挡住」与「车底根本不存在」 | 两者都落到 `found=0` / `CPL-SAFE-FAIL`，不可区分 |
| B 落点 | 57 要有一个「合法停下来的格」，且该格能订到预留 | 无：路径要么全通、要么全否 |
| C 刹车 | 停在落点上，而不是 30 速裸奔撞上去 | 无：`CTTRF_NONE` 不 `MarkTrainAsStuck`（`:17171`），`TrainController` 不知道要减 |
| D 恢复 | 54 走开后自动续进，不需要玩家干预 | 天然成立（每 tick 重订），前提是 C 没把车弄成 `Stuck` 死态 |

⇒ 只做 C 不做 B 会造成「停在路中间堵死后面的车」；只做 B 不做 C 会让 57 以 30 速冲进 54 的格子。**B 与 C 必须一起做**。

### 30.2 四个候选方案与取舍

**方案 α：失败时清 `cur_speed`/`subspeed`（否决）**
第 173 轮已判定不可行（KI-265 修法 3）：清速度会让机车停在挂车距离外再也挂不上，因为下一 tick 没有「重新起步」的驱动源。除非同时接入 D，否则等于把死锁从寻路层搬到运动层。

**方案 β：显式「等待 blocker」状态机（可行但重）**
在 `TrainCoupleHandler` 里维护 `blocker_veh + 计时`，失败时每 tick 查 blocker 是否还在、是否在动，blocker 消失才放行。优点=可在 UI 上显示「等待 54 让路」；缺点=新增存档字段（否则读档丢状态）、新增一份生命周期，与 R3R 现存「尽量不引入新持久状态」的取向冲突。

**方案 γ：终点截断到 blocker 前一格（推荐）**
不改状态机、不清速度，而是**让寻路在阻塞处提前收尾**：`FindSafeCouplePositionProc` 在 `:199` 分支因交叉预留失败时，不是让整条路径作废，而是把当前回溯节点当作终点接受（截断），`rp()` 只铺到那一格。
- 57 开到有预留的最后一格后，PBS 的 `TrainCheckIfLineEnds` 自然发现「前方无预留」⇒ 刹车。**C 环节免费得到**，不需要手动清速度。
- 54 走开后下一 tick 重订，终点自然延伸过去。**D 环节也免费**。
- 有现成范式：等待区路点截断 `R3RShouldCutCoupleAtWaitPoint`（`yapf_rail.cpp:860-872`，返回值 1=截断/2=已站在截断点）就是同一套「收缩终点」的做法，可以直接照抄结构。

**方案 δ：给 54 打「即将离开」标记，57 按预计时间窗口等待（否决）**
需要预测 54 何时离开 `14,15`，而 54 的路径本身也可能被挡。预测失败就是永久等待，比现状更糟。

### 30.3 取证结果：失败点在 back-walk，且是「整条作废」

**U-346-D 已确认**：`FindSafeCouplePositionProc` 唯一实际调用点是 `CheckSafePositionOnNode`（`yapf_rail.cpp:427-435`）：

```cpp
inline bool CheckSafePositionOnNode(Node *node)
{
	dbg_assert(node->parent != nullptr);
	if (!node->IterateTiles(..., &CYapfReserveTrack<Types>::FindSafeCouplePositionProc)) {
		return false;
	}
	return true;
}
```
注意这里与兄弟函数 `FindSafePositionOnNode`（`:410`）**语义相反**：后者「回调返回 false ⇒ 找到了安全位 ⇒ return true」，前者「回调返回 false ⇒ 这一格不通 ⇒ return false」。`FindSafeCouplePositionProc` 的 false 是后者语义。

**U-346-E 已确认**：调用点在 back-walk 循环里（`:1029-1039`）：

```cpp
while (pNode->parent != nullptr) {
	pPrev = pNode;
	pNode = pNode->parent;
	++r3r_hops;
	...
	if (!this->CheckSafePositionOnNode(pPrev)) {
		fprintf(dbg, "CPL-SAFE-FAIL veh=%d tile=%d,%d\n", ...);
		return INVALID_TRACKDIR;          // ← 整条路径作废
	}
}
```
`CPL-PATHFOUND found=1` 是 YAPF 搜索本身成功，back-walk 在其之后做「验证整条路径可通行」，任一格失败即 `return INVALID_TRACKDIR`。⇒ 57 拿不到任何预留目标，下 tick 重试，正是「30 速裸奔」的来源。

**U-346-F**：代码路径确定存在（`:1033`），日志侧是否出现该行未复核——但既已 `return INVALID_TRACKDIR`，`found` 必然不是「带着截断终点成功返回」。

### 30.4 方案 γ 的落地形状（修正版）

1. **带出阻塞原因**：`yapf_rail.cpp:199-200` 的 `TryReserveRailTrack` 失败处目前只写探针，需新增出参（如成员 `res_blocked_by` / `res_blocked_tile`），把「是谁的哪根轨挡的」传出去；判据＝`GetReservedTrackbits(tile) != NONE && (reserved & ~TrackToTrackBits(want)) != TRACK_BIT_NONE`。
2. **收缩终点而不是作废**：`:1029` 失败时先问「是不是 blocker 阻塞」。是则不 `return INVALID_TRACKDIR`，改为
   - `SetReservationTarget(pNode, pNode->GetLastTile(), pNode->GetLastTrackdir())`（把终点从 `consist_head_tile` 收缩到**阻塞格的父节点**，即更靠近 57 的那一格）；
   - `break` 退出 back-walk；
   - 打 `CPL-CUT-BLOCKER veh=57 tile=14,15 cut=<x,y> blocker=<owner>`。
3. **要求截断点本身是安全等待位**：`pNode` 必须同时过 `IsSafeWaitingPosition()`，否则继续往近处退一格。这一条与 KI-358 是同一把尺子。
4. **刹车不由代码做**：不碰 `cur_speed`/`subspeed`（KI-265 修法 3 已判定会停在挂车距离外再也挂不上）。截断后 `lookahead` 只到截断点，`TrainCheckIfLineEnds` 自然发现「前方无预留」⇒ `TrainController` 刹车。
5. **前置条件 = KI-359 P1**：没有 `lookahead` 时车每 tick 重订、不会沿预留行驶，截断出来的「前方无预留」不会被引擎当成减速理由。⇒ **P1 必须先修**。

### 30.5 边界与遗留

- **54 永不离开的情况**：57 会永久停在截断点。需要超时兜底（例如 `COUPLE_BLOCKER_TIMEOUT` 之后改打 `CPL-PAIR` 重新配对），本轮不做，登记为待办。
- **截断点可能堵住后方列车**：这是「等」的固有代价，但比 30 速撞上去好；可在 UI 复用 `Stuck` 语义避免玩家困惑。
- **不动 `HasReservedTracks` 分支**：`:131` 那段已经正确处理「配对车占的格可通行」（`R3RIsCoupleTarget`），本次只改 `:199` 这个 else-if 兄弟分支。

### 30.6 状态

设计问答 + 代码取证，本轮未改源码、未构建。方案 γ 落点已定在 `yapf_rail.cpp:199`（带出原因）与 `:1029`（收缩终点），前置条件是 KI-359 P1。

## 31. 函数地图（2026-10-27，玩家要求「讲一下这里可能涉及到的函数」）

### 31.1 已核实行号（本轮 search_content 实读，非记忆）

| 函数 | 位置 | 职责 | 本场景表现 |
|---|---|---|---|
| `TrainLocoHandler` | `train_cmd.cpp:21637` | 每 tick 驱动一列车，选移动/停驶分支 | 每 tick 重入 |
| `TrainController` | `train_cmd.cpp:20222` | 预留 + 推进像素 + 折返 | 每格调一次预留 |
| `TrainCoupleHandler` | `train_cmd.cpp:14884` | 停车后的挂接几何命中/候选搜索 | **不在本链上**（不寻路） |
| `MarkTrainAsStuck` | `train_cmd.cpp:4753` | 置 `VehicleRailFlag::Stuck` | 未被调用 ⇒ 界面不显示卡住 |
| `ExtendTrainReservation` | `train_cmd.cpp:16059` | 先试延长现有预留，失败再走 YAPF | 第一道；`early-haverserved` 在它之前 |
| `TryLongReserveChooseTrainTrack` | `train_cmd.cpp:16925` | 长预留分支 | 视编组而定 |
| `ChooseTrainTrack` | `train_cmd.cpp:17093` | 总闸：按起点格类型分派，失败 `MarkTrainAsStuck` | `found=0` 在这里成型 |
| `CheckSafePositionOnNode` | `yapf_rail.cpp:427` | 「这个 node 每一格都通得过吗」 | 返回 false |
| `FindSafeCouplePositionProc` | `yapf_rail.cpp:119` | 逐格判定；`:131` 车体分支 / `:199` 预留分支 | `:199-200` 失败 |
| `TryReserveRailTrack` | `pbs.cpp:476` | 真正写预留位（含交叉冲突检测） | 返回 false |
| `GetReservedTrackbits` | `pbs.cpp:340` | 读该格预留位 | 读出 `0x8` |
| `IsSafeWaitingPosition` | `pbs.cpp:2387` | 「几何上适不适合当路径终点」（信号/尽头/道口/90°） | **不看预留** |
| `IsWaitingPositionFree` | `pbs.cpp:2491` | 「这格现在有没有被别人占/预定」 | `:2497` 判交叉 |

back-walk 循环体在 `yapf_rail.cpp:1017-1040`，`:1029` 调 `CheckSafePositionOnNode`，`:1033` 打 `CPL-SAFE-FAIL`，`:1038` `return INVALID_TRACKDIR`。

### 31.2 修正 §30.4 第 3 步的口径（重要）

`IsSafeWaitingPosition`(pbs.cpp:2387) 的函数体里**没有任何预留检查**——只判 depot / 路点 / 非 PBS 信号 / 桥隧信号入口 / 尽头 / 90°。真正把「已有别人预留」判死的是 `IsWaitingPositionFree`(pbs.cpp:2491)，其首行 `:2497 if (TrackOverlapsTracks(reserved, track)) return false;` 就带注释 "Tile reserved? Can never be a free waiting position."，且 `TrackOverlapsTracks` 对 `TRACK_X`/`TRACK_Y` **判为重叠**。

⇒ 方案 γ 第 3 步「截断点必须自己也是安全等待位」应同时过两个函数：`IsSafeWaitingPosition`（几何）+ `IsWaitingPositionFree`（占用）。只引前者是错的口径，已在此更正，KI-358 条目同步更正。

### 31.3 状态

纯函数地图 + 一处口径更正，未改源码、未构建。

## 32. 为什么挂接走 CYapfCoupleRail（2026-10-27 取证）

**分流点不在 YAPF 内部，在 `ChooseTrainTrack`。** `YapfTrainChooseTrack()`（`yapf_rail.cpp:1585`）只分派 `CYapfRailNo90` / `CYapfRail`，从不碰 `CYapfCoupleRail`；后者只被 `YapfTrainCoupleTrack()`（`yapf_rail.cpp:1963`，内部 `:1984-1986` 实例化 `CYapfCoupleRail`）使用。

`train_cmd.cpp:17518`：

```cpp
if (consist->current_order.IsType(OT_GOTO_COUPLE) && !r3r_couple_depot) {
	...
	Track path_found = DoTrainCouplePathfind(consist, true);   // :17569
```

`DoTrainCouplePathfind`（`:16011`）→ `YapfTrainCoupleTrack(v, !do_track_reservation)`（`:16013`）。否则走 `DoTrainPathfind`（`:15999`）→ `YapfTrainChooseTrack`。

四条理由（前三条代码注释自陈，`:17476-17482`）：

1. **终点不是格子而是列车**：普通 YAPF 的 destination 是 `dest_tile`/站台，couple 的终点是「另一列车」。`CYapfDestinationTrainRailT` 只接受「`WAIT_COUPLE` 且满足 GOTO_COUPLE 要求」的车底，避免机车到处乱挂。
2. **普通寻路必败**：等待车底自己占着格、且自己有一条预留。普通 YAPF 把「别人的预留」当障碍 ⇒ `found=0`。couple 版要「跟着等待车的预留走」，把预留接到它的预留末端。
3. **需要耦合专用的回溯预留**：`CYapfFollowCoupleRailT::stFindNearestCoupleTrain` 的 back-walk 逐格 `CheckSafePositionOnNode`，边验边订，保证路径每一格都真的能订上 —— 这是普通 follow 不做的。
4. **库型订单例外（KI-320, `:17502`）**：库格对 couple 搜索是拓扑死路（back-walk 拒绝一切库格，`FSCP fail=depot`），库内车底也没有预留 ⇒ 改走普通寻路开到库里，再由 `TrainCoupleHandler` 扫库匹配。

**对 KI-358 的意义**：这条分流解释了「同样的路，普通订单能走、挂接订单走不了」—— 卡住 57 的不是地图不通，而是 couple 版额外那条「每格都必须订到预留」的 back-walk 校验（`:1029`），而它失败即整条作废。要让它等，只能让这条校验学会「收缩终点」。

### 32.1 状态

纯取证问答，未改源码、未构建。

## 33. 「把当空气的判据套到普通 YAPF」可行性（2026-10-28 取证）

### 33.1 先定位：那两个判据在 `FindSafeCouplePositionProc` 的**第一支**，57 死在**第二支**

`yapf_rail.cpp:119` 起是 if / else-if 互斥两支：

- **第一支 车体占用**（`HasReservedTracks(tile, tracks)`，格上有车）：`:160` 自身豁免 `best->index == Yapf().GetVehicle()->index` ⇒ true；`:165` `R3RIsCoupleTarget(best)` 为假 ⇒ false。**「当空气」只活在这一支。**
- **第二支 预留冲突**（`GetReservedTrackbits(tile) != TRACK_BIT_NONE`，格上无车但被订）：`:199` `TryReserveRailTrack(tile, TrackdirToTrack(td))` ⇒ 失败即 false。

⇒ 57 现场 `tbits=0x9 res=0x8`（54 占 Y，57 要 X）走的是**第二支**。改第一支的判据对 57 **一行都不影响**。

### 33.2 普通 YAPF 本来就有「逐格真订 + 失败全撤销」，不需要套

共用基类 `CYapfReserveTrack<Types>`（`yapf_rail.cpp:82`），普通 `CYapfRail` 与 `CYapfCoupleRail` 用的是**同一份**：

- `:450 TryReservePath(target, origin, unsafe_pos)` —— 两者共同入口；
- `:473` `if (!unsafe_pos && !IsWaitingPositionFree(...)) return false;` —— **目标格被别人预留 ⇒ 普通版也在这里直接 false**；
- `:511` `return this->ReserveSingleTrack(tile, td);` → `:353 TryReserveRailTrackdir()` 逐格真订；
- `:513-525` 中途 `res_fail_tile` 非空 ⇒ 沿 `node->parent` 链 `UnreserveSingleTrack` 全撤 ⇒ `return false`。

⇒ 「订不上就整条作废」这套语义**普通版一行不少**。说「套到普通 YAPF」没有语义落点。

### 33.3 三条路都不通

1. **加一层等价验证**（让普通版也跑 `CheckSafePositionOnNode`）：`CheckSafePositionOnNode`(`:427`) 只被 couple 的 back-walk（`stFindNearestCoupleTrain`）调用，普通版没有回溯循环可挂。硬加 = 每次寻路多做一遍全路径验证，结论必然与 `TryReservePath` 一致，纯性能损失。
2. **把判据塞进 `ReserveSingleTrack`**：等于让普通列车「看到车就穿过去、看到别人的预留也订上去」⇒ PBS 互斥失效 ⇒ 对撞。这在普通订单上是纯事故，没有任何收益。
3. **判据只用于 `R3RIsCoupleTarget` 一类目标**：那正是 couple 版已做的，普通订单没有「挂接目标」这个概念，判据恒假，等于没加。

### 33.4 couple 版「当空气」不等于「免检」——57 就是活证明

放行只针对两类：①自己（会开走）；②挂接目标（要跟着它走、接到它预留末端）。放行之后**仍然**要过 `ReserveSingleTrack` 真订。57 全判据在身，照样被 `:199` 拦住 ⇒ 「当空气」对「等」这个需求**分量为零**。

**要 57 等，只能改第二支的失败语义**（从「整条作废」改成「终点收缩到冲突格前一格」），即方案 γ，与「当空气」无关。

### 33.5 状态

纯取证问答，未改源码、未构建。

### 33.6 调用树（两支分流，判据只挂在**真分支**）

> 术语约定：`consist->current_order.IsType(OT_GOTO_COUPLE) && !r3r_couple_depot` 为**真** ⇒ 走「真分支」（挂接专用寻路 `CYapfCoupleRail`）；为**假** ⇒ 走「假分支」（普通寻路 `CYapfRail / CYapfRailNo90`）。不用左右/上下指代。

```
TrainLocoHandler                                  train_cmd.cpp
 └─ TryPathReserve
     └─ ChooseTrainTrack                          train_cmd.cpp:~17440
         │
         ├─[分流判据] consist->current_order.IsType(OT_GOTO_COUPLE)
         │            && !r3r_couple_depot        train_cmd.cpp:17518
         │
         ├──(真)──► DoTrainCouplePathfind         train_cmd.cpp:16011
         │           └─ YapfTrainCoupleTrack      yapf_rail.cpp:1963
         │               └─ CYapfCoupleRail       yapf_rail.cpp:1984 实例化
         │                   └─ CYapfFollowCoupleRailT::stFindNearestCoupleTrain
         │                       └─ back-walk（从目标节点回溯到起点，逐格）
         │                           └─ CheckSafePositionOnNode      yapf_rail.cpp:427
         │                               └─ FindSafeCouplePositionProc  yapf_rail.cpp:119
         │                                   ├─ ① HasReservedTracks(tile)  格上有车
         │                                   │   ├─ best==自己    → true   :160 ★当空气①
         │                                   │   └─ R3RIsCoupleTarget? :165  ★当空气②
         │                                   └─ ② else 格上无车但 GetReservedTrackbits!=NONE
         │                                       └─ TryReserveRailTrack :199  ✗ 57 死在此
         │
         └──(假)──► DoTrainPathfind               train_cmd.cpp:15999
                     └─ YapfTrainChooseTrack      yapf_rail.cpp:1585
                         ├─ CYapfRailNo90         yapf_rail.cpp:1588
                         └─ CYapfRail             yapf_rail.cpp:1589
                             └─ CYapfRail::stChooseRailTrack  yapf_rail.cpp:1389
                                 └─ FindPath() → 结果后处理
                                     └─ CYapfReserveTrack<T>::TryReservePath  :450
                                         │   ←←← 与真分支共用同一份基类 →→→
                                         ├─ :473 IsWaitingPositionFree() 假 ⇒ return false
                                         ├─ :511 ReserveSingleTrack :344 → :353 TryReserveRailTrackdir
                                         └─ :513 失败 ⇒ 沿 node->parent 全撤 ⇒ return false
```

**四条「缺失的边」= 四层不可套**：

| # | 图中事实 | 结论 |
|---|---|---|
| 1 | **假分支**无 `CheckSafePositionOnNode` / `FindSafeCouplePositionProc` 节点 | 判据在普通版调用链上**不存在**，没有挂点 |
| 2 | **真分支** O9a 是「回溯边验边订」，**假分支**是「先订、失败再撤」 | 两者是**同层功能的两种实现**，不是「加一道过滤」的关系 |
| 3 | `CYapfReserveTrack` 两支共用，真订位在 `:511`/`:353` | 绕过它 = 绕过 `TryReserveRailTrackdir`，PBS 互斥当场失效 |
| 4 | 57 走的是**真分支**，★当空气①②**已在它身上生效** | 判据对「让 57 等」贡献 0，不是瓶颈 |

**状态**：本轮为设计问答 + 取证，未改源码、未构建。

### 33.7 追问「57 为什么走假分支」的答复：它没有走假分支

玩家 2026-10-28 追问。**结论=57 在 `order=16` 期间一律走真分支，从未因 coupling 失败切到假分支**；把三种"看起来像假分支"的形态分清即可：

| 形态 | 日志锚点 | 归属 |
|---|---|---|
| 掉头那一刻（`spd=0`、`origin==mf==16,14`） | `1389 TRP veh=57 mf=16,14 real=1 type=16 dest=1932 spd=0 fp=0 look=1 => ok=1 res=1` | **两支都没进**：`TryPathReserve` 在 `ChooseTrainTrack` 之前就 early-haverserved 早退（掉头后至 `:1738` 之间 `CRT veh=57` 零条，而 `CRT` 在 `YapfTrainChooseTrack` 内无条件打印） |
| `tick=12551` 正常重试 | `1461 CPL-SCAN tick=12551 veh=57 found=1 …` / `1463 CPL-BEST veh=57 tile=5,15 td=0 cost=10242` / `1477 CPL-RESERVE veh=57 reserved=1 next=3` / `1478 CPL-RET veh=57 … ret=0x8 why=nores-final` | **真分支**（`CPL-*` 是 `CYapfCoupleRail` 专用探针；假分支打 `CRT`） |
| 挂上车底之后 | `1739 CRT veh=57 order=1 dir=5 origin=14,15 td=8 found=1` | **假分支**，但**必然且正确**：`order=1`=`OT_GOTO_STATION`，分流判据 `:17518` 为假 |

**"真分支失败不会切到假分支"的代码依据**（`src\train_cmd.cpp`）：
- `:17778-17792` 注释明写：第 212 轮那条「没找到可挂车底 ⇒ 按 GOTO_COUPLE 目的地走」的 fallback **已按玩家 2026-10-03 口径删除**；失败动作是撤预留 + `MarkTrainAsStuck` 原地等，不再调 `DoTrainPathfind`。
- 因此 `if (path_found == INVALID_TRACK)` 块内没有任何通往 `DoTrainPathfind` 的边。

**玩家看到的「57 像普通列车一样自己开走」是什么**：不是假分支。是真分支里 `cpl-moving-keep` 早退把 `keep_track` 交回调用方，车按当前格 `best_track` 自行选了个方向——`:17684` 注释原话「落回**普通运动逻辑**」。寻路分支（是否调 yapf）与运动逻辑（按返回的 track 前进）是两层。

### 33.8 「只有真分支有当空气」的准确边界 + 57 为何一格没铺

**① 「当空气」是**真分支独有**，且只在 `if` 那一支**

| 分支 | 对应「等待位是否可接受」的判据 | 对「本车自己/目标车」是否网开一面 |
|---|---|---|
| 真分支 | `FindSafeCouplePositionProc`（`yapf_rail.cpp:119`） | **是**：`:160` 自己 ⇒ true（当空气①）；`:165` `R3RIsCoupleTarget` ⇒ true（当空气②）|
| 假分支 | `IsWaitingPositionFree`（`pbs.cpp:2491`） | **否，一刀切**：`:2497 if (TrackOverlapsTracks(reserved, track)) return false;` —— 连「这格的车是不是我、这位是不是我订的」都不问 |

⇒ 假分支不是「少一条判据」，是**连问都不问**。想给它加，等于在 `TryReservePath` 前另建挂点，且与紧随其后的 `TryReserveRailTrackdir` 判据重复（见 §33.6 第 2 条）。

**② 真分支的「当空气」只在 `if` 支，管不了 57**

```
:120  if (HasReservedTracks(tile, tracks))        // 格上有车 ← 当空气①②在这里
:199  else if (GetReservedTrackbits(tile) != NONE) // 格上无车、但被订 ← 无当空气
:200      if (!TryReserveRailTrack(tile, ...)) return false;   // ← 57 死在这里
```

**③ 57 一格没铺的四层链**（与 §18.5 一致，此处只补「为什么当空气救不了」）

1. 57 的冲突格 13,15：`tbits=0x9`（X|Y 双槽）、`res=0x8`（`TRACK_BIT_LOWER` 已被 54 订走），57 要的是同一 LOWER 位 ⇒ 属「格上无车、位被订」，走 `else if`。
2. `:200 TryReserveRailTrack` → `rail_map.h:272 TryReserveTrack` 是**硬互斥**：`if ((res & bits) != TRACK_BIT_NONE) return false;  // already reserved` —— **不区分是不是自己订的**。
3. 返回 false ⇒ `FindSafeCouplePositionProc` 判负 ⇒ `stFindNearestCoupleTrain` 整体判负 ⇒ `CPL-RET res=0x0 why=nores-final`，**没有一条被采纳的路径** ⇒ `TryPathReserve` 拿不到 path。
4. `CPL-RESERVE veh=57 reserved=1 next=3` 的 `next=3` 是**探针里 rp 的路径索引**，不是「已订 3 格」（§18.3 已纠过一次）；终值看 `res=0x0`。

**④ 两类不同的问题，别混**

- 当空气①② 解决的是「**格被别的车占着** ⇒ 仍可当作可通过/可等待」。
- 57 卡的是「**轨道位被订走** ⇒ 订不上」。后者在真分支的 `else if` 支没有任何豁免，在假分支的 `IsWaitingPositionFree` 里更是全局一刀切。

### 33.9 疑点 REG-33-A：KI-361 的注释承诺与实现不符（待验证）

`pbs.cpp:433-451` 的 KI-361 注释写「Treat "already booked by our own chain" as a successful reservation: the bit is ours either way, and the green is what the signal is for.」但同函数实际代码只有：

```
:431  bool success = TryReserveRailTrack(tile, track, trigger_stations);
:452  if (success && HasPbsSignalOnTrackdir(tile, td)) { SetSignalStateByTrackdir(...Green); ... }
:460  return success;
```

**没有任何 `success = true` 的豁免**，而下游 `rail_map.h:272` 对「已被订（含自己订）」一律 `return false`。

- 期望值：注释所述「自己订过的位再按 Trackdir 订 ⇒ 视作成功并点绿灯」。
- 实测值：success 仍为 false，绿灯不点，调用方丢格。
- 可能解释（待验）：注释描述的是「修法甲把 rp() 改成 Trackdir 级后不会再出现 Track+dTrackdir 双订」这一**历史结论**，而非新增分支；若是这样，注释措辞应订正，否则后来者会以为有豁免。
- 需什么数据：`grep -n "success = true" pbs.cpp` 全无命中即证实无实现；再看 1112,129 现场的 `RESV-*` 是否仍出现「自己订了却被判失败」形态。
- 影响面：与 57 无关（57 的位是 54 的），但会影响「本车已订格被再次判失败 ⇒ 丢格 ⇒ 孤儿预留」这一类现场。

### 33.10 【猜测根因·待验证】57 走真分支却「硬冲别人的预留」

玩家 2026-10-28 报告：**57 走真分支时不是「撞上就退」，而是真的开进了别人预留的轨道**。这与 §18.5/§33.8 的旧定性冲突——旧文写 `nores-final` ⇒「57 不动、只是这一 tick 没铺成」。若玩家观察成立，则「寻路判负」与「列车停住」之间**没有绑定关系**。

**猜测机制（待验证，未读代码前不成立）**：

1. `CYapfCoupleRail` 判负 → `CPL-RET res=0x0 why=nores-final` ⇒ **一条预留都没订下来**；
2. 但真分支里存在 `cpl-moving-keep` 早退（`:17684` 注释自称「落回**普通运动逻辑**」）：它把 `keep_track` 交回调用方，**绕过 yapf 的预留结论**；
3. `TrainController` 按交回的 track 前进时**不二次校验该轨是否属于别人**（预期没有 `HasReservedTracks` 复核）⇒ 车物理开进 54 已订的 LOWER 位；
4. ⇒ 预留系统（订位/校验）与实际运动之间出现**真空区**：订不上照样能走。

**这解释了三件旧事**：①`nores-final` 之后 57 位置仍改变（旧文无法解释）；②`CPL-RESERVE reserved=1 next=3` 与 `res=0x0` 的矛盾只是探针语义，真正结论是「没订也走」；③「当空气」判据对 57 无效的真正原因不是「它没走到那一步」，而是**走不走根本不依赖那一步**。

**待验证判据（一轮内按序取）**：
- P1 `cpl-moving-keep` 早退处返回的是 `keep_track` 还是 `TRACK_BIT_NONE`，以及早退条件里是否含「目标格预留归属」检查；
- P2 `TryPathReserve` 返回值 → `TrainController` 的消费链上是否有 `HasReservedTracks` / `GetTrainForReservation` 复核；
- P3 日志侧：`1461 CPL-SCAN` 失败之后 57 的 `tile`/`RP cur=` 是否继续推进到 54 的预留格；
- P4 若 P1~P3 成立 ⇒ 严重度高（可造成双车同格/穿模），修法方向=早退前校验 `keep_track` 目标格是否被**他人**预留，是则不允许早退（改为停在原地）。

**状态**：猜测根因，待 P1~P4 验证后更新为「成立 / 证伪」。

### 33.11 【重大纠错】`why=nores-final` 不是「预留失败」，而是「压根没请求预留」

**代码铁证**（`src\train_cmd.cpp:17853-17854`）：

```17853:17854:src/train_cmd.cpp
	/* No track reservation requested -> finished. */
	if (!do_track_reservation) return r3r_chosen_ret(best_track, result_flags, "nores-final");
```

⇒ `1478 CPL-RET veh=57 … res=0x0 ret=0x8 why=nores-final` 的正确读法：

- `why=nores-final` = **`do_track_reservation == false`**，本次调用根本没打算订路，直接 `return best_track`；
- `ret=0x8` = 交回的 `best_track`（`TRACK_BIT_LOWER`），**这根轨一根都没订**；
- 因此 §18.5/§33.8 里「rp() 订路失败 ⇒ 无路径 ⇒ 不铺预留」的因果链**是错的**：不是订失败，是**根本没订就交轨**。

**这就是「硬冲别人的预留」的确切机制**：

1. 真分支规划出一条指向车底的路（`CPL-BEST tile=5,15`），路过的格里有 54 已订的 LOWER 位；
2. 走出真分支时 `do_track_reservation=false`，于是不调 `rp()`、不订任何位，把 `best_track` 直接交回；
3. `TrainController` 按这根轨推进，**沿途零预留**——既没有自己的预留，也不检查别人的；
4. ⇒ 57 物理开进 54 已订的轨道。**寻路结论（可走）与预留结论（订不上）被这条 early return 短路掉了。**

**与 §33.9 的关系**：`:17858-17860` 的注释自陈设计假设——「a GOTO_COUPLE loco's couple pathfinder already reserved the path up to the consist / ExtendTrainReservation stops at the already-reserved consist platform」。即引擎**假定 CPL 已经订好了路**。一旦 CPL 没订成（`res=0x0`），这个假定崩塌，但没有任何断言或回退拦住它。

**待查（下一组）**：`do_track_reservation` 在何处、因何被置 `false`（是否由 `OT_GOTO_COUPLE` 或 `r3r_*` 分支置位），以及置 false 时是否复查「CPL 确实订到了路」。

**状态**：根因方向已从「订路失败」改判为「短路交轨」，待 `do_track_reservation` 赋值点取证后定稿。

### 33.12 【已查证】「硬冲」的代码落点：`:17853` 的早退短路了失败处置

`src\train_cmd.cpp` 的顺序是：

```
:17832  return r3r_chosen_ret(FindFirstTrack(tracks), result_flags, "cpl-fail");   // CPL 真失败分支
:17834  result_flags |= CTTRF_RESERVATION_MADE;                                     // CPL 成功分支末尾
:17835 }
:17837  if (res_dest.tile != INVALID_TILE && !res_dest.okay && (!OT_GOTO_COUPLE || r3r_couple_depot))
            → DoTrainPathfind(...) + HandlePathfindingResult(path_found)            // GOTO_COUPLE 走不到这里
:17853  /* No track reservation requested -> finished. */
:17854  if (!do_track_reservation) return r3r_chosen_ret(best_track, result_flags, "nores-final");
:17856  /* A path was found, but could not be reserved. */
:17857  if (res_dest.tile != INVALID_TILE && !res_dest.okay) { … }
```

**关键事实一**：`nores-final` 的 `return` **位于**「路径找到但订不上」块（`:17856-17857`）**之前**。`do_track_reservation==false` 时直接返回，那块里的处置**一行都不执行**——包括上一段 `:17800 FreeTrainTrackReservation()` 与 `:17801 MarkTrainAsStuck()`（它们在 `cpl-fail` 分支里）。

**关键事实二**：`do_track_reservation` 全函数只有两处赋值——`:17100` 初值 `_settings_game.pf.reserve_paths || (flags & CTTF_FORCE_RES)`、`:17295` 信号分支强制 `true`。`pf.reserve_paths` 默认值是 **`false`**（`src\table\settings\pathfinding_settings.ini:118`，含义是「无视信号类型总是预留」，不是总开关）⇒ **`do_track_reservation` 为假 ⇔ 本次调用没带 `CTTF_FORCE_RES`**。

**⇒ 「硬冲别人的预留」的确切链条**：

1. 57 的这次 `ChooseTrainTrack` 走真分支、CPL 跑完（`CPL-SCAN/BEST/BACK/RESERVE` 有行）；
2. 到 `:17853` 时 `do_track_reservation==false` ⇒ 立刻 `return best_track`（日志 `ret=0x8`、`res=0x0`）；
3. **既没有 `FreeTrainTrackReservation`（撤掉半截预留），也没有 `MarkTrainAsStuck`（标成卡住）**；
4. 交回的 `best_track` 被 `TrainController` 当作可走轨 ⇒ 车沿线推进；
5. 沿途该车**零预留**（`res=0x0`），`TrainController` 也不复核「这格是不是别人订的」⇒ 物理开进 54 已订的 `TRACK_BIT_LOWER`。

**这就把玩家观察的三件事串起来了**：①`nores-final` 之后 57 位置仍改变；②`CPL-RESERVE reserved=1` 与 `res=0x0` 并存；③「当空气」救不了它——问题不在判据，在**交轨出口**。

**待查（下一轮第一件事）**：
- Q1 `TryPathReserve` 对 `OT_GOTO_COUPLE` 调 `ChooseTrainTrack` 时是否应传 `CTTF_FORCE_RES`；若应传而未传，就是这一处的传参缺陷；
- Q2 `:17853` 这个早退是否应挪到 `:17856-17857` 的失败处置**之后**（即先判 `!res_dest.okay` 就撤预留 + 标卡住 + 返回 `INVALID_TRACK`）；
- Q3 CPL 成功分支（`:17834`）之后若 `res_dest.okay` 仍为假，是否也要走同一处置。

**状态**：`nores-final` 短路失败处置 = **代码事实已确认**；「该不该传 `CTTF_FORCE_RES`」= 待查。

### 33.13 定性判定：走错分支？还是函数错？（2026-10-28 玩家提问）

玩家问：「既然列车硬冲，那么它是走错分支，还是函数本身错误」。四种候选逐一排除：

| 候选 | 判定 | 依据 |
|---|---|---|
| A 分支选错（57 本该走假分支却走了真分支） | **否** | 日志有 `CPL-SCAN/CPL-BEST/CPL-BACK/CPL-RESERVE` 全套真分支探针；假分支不产生这些行 |
| B 寻路函数算错（CPL 该找到路却判负） | **否** | CPL 的判负是**正确**的：13,15 的 LOWER 位被 54 订着，`TryReserveTrack` 硬互斥 ⇒ 确实没有合法路 |
| C 出口顺序错（`:17853` early return 挡在失败处置之前） | **是（主因）** | `:17853-17854` 在 `:17856-17857` 之前 return；`do_track_reservation==false` 时 `FreeTrainTrackReservation` / `MarkTrainAsStuck` 一次不执行 |
| D 调用方传参错（本该带 `CTTF_FORCE_RES` 却没带） | **待查（放大器）** | `do_track_reservation` 全函数仅 `:17100` 初值 + `:17295` 强制 true 两处；`pf.reserve_paths` 默认 `false` ⇒ 为假只能是 flags 缺 `CTTF_FORCE_RES` |

**结论**：

1. **不是"走错分支"**——真/假分支的选择是对的，真分支也确实该走。
2. **也不是"寻路函数算错"**——CPL 判"无路可走"是正确的，它如实报告了"这一格订不上"。
3. **错的是函数末尾的出口排序**：`:17853` 那句 `if (!do_track_reservation) return best_track` 的本意是「本次调用不负责订路，选完轨就收工」，但它被放在「找到了路但订不上」的处置**之前**，于是把「CPL 跑过且没订到」这一种也一起短路放行了。
4. **性质是「防御缺失」，不是「计算错误」**：所有计算都对，缺的是「判负之后必须停住」这一步。
5. 如果 D 成立（本该传 `CTTF_FORCE_RES`），则 `nores-final` 只是放大器——真正缺的是传参，`:17853` 的顺序问题会把缺陷放大成"硬冲"。

**为什么长期没暴露**：正常列车走到 `:17853` 时 `best_track` 是「合法可走且无需预留」的轨，直接返回无害。只有 57 这种「CPL 判负、`best_track` 却被写成 0x8（LOWER）」的组合才会致命——判负的结论被丢掉了，但轨还被交回去了。

**唯一能定论的取证点**：`TryPathReserve` 对 `OT_GOTO_COUPLE` 调 `ChooseTrainTrack` 时传的 flags 是否含 `CTTF_FORCE_RES`。若不含 ⇒ D 成立（传参漏）；若含 ⇒ D 不成立，`do_track_reservation` 为假的来源另有他处（例如 CPL 分支内部把它改回 false），需重查。

## 34. 第 337 轮：`nores-final` 出口修复（已实现 + 已编译）

**玩家拍板（2026-10-28）**：「这边动的是 R3R 接线对吧，那就按照你推荐的来修吧」⇒ 采纳 §33.13 的修法口径。

### 34.1 改法（仅 `src\train_cmd.cpp`，未碰任何 `src\*.h`）

把 `:17853-17854` 的单行早退改成一个带自证的分支：

1. **只对挂接机车生效**：`consist->current_order.IsType(OT_GOTO_COUPLE) && !r3r_couple_depot`（车库型挂接 `r3r_couple_depot` 本就跳过 CPL 块，不干预）。
2. **自证「本车真有一段属于自己的可走预留」**：`FollowTrainReservation(consist, nullptr, FollowTrainReservationFlag::IgnoreLookahead)`，若返回的 `tile` 仍等于 `GetMovingFront()->tile`（或无 moving front）⇒ 认定「零预留」。
   - 判据与 `:17722-17724` 同一套，已由 KI-479 复测校准；
   - **必须 `IgnoreLookahead`**：真实制动模型下 lookahead 的终点是「计划终点」而非「已订下的路」，不忽略会让判据恒真（KI-478/479 的教训）。
3. 零预留时按 KI-316 处置：
   - `FreeTrainTrackReservation(consist, origin.tile, origin.trackdir)` —— 清掉残留半截预留；
   - `if (changed_signal != INVALID_TRACKDIR) MarkSingleSignalDirty(...)` —— 让信号灯刷新；
   - `if (cur_speed != 0 || mark_stuck) MarkTrainAsStuck(consist)` —— **在动的必须当场停**（`MarkTrainAsStuck` 会把 `cur_speed/subspeed` 直接写 0）；已经停着的车只在调用方要求时标卡住，避免把「正常等车底」刷成界面红；
   - `return r3r_chosen_ret(FindFirstTrack(tracks), result_flags, "nores-couple-blocked")` —— 与 `cpl-fail` 分支（`:17832`）同形态，绝不把 `best_track` 交回去。
4. 有真预留时行为**逐字节不变**：仍 `return r3r_chosen_ret(best_track, result_flags, "nores-final")`。
5. 新增只读探针 `CPL-NORES-BLOCK veh= tile= real= spd= stuck= front=,%d end=,%d best=0x%X`，按 `(tile, real, spd!=0)` 三元签名节流（沿用 `r3r_chosen_ret` 的签名去重风格），`R3RDbgOn()` 外层包裹。

### 34.2 与 57 现场的关系（必须分清，别误判为「57 已修」）

`build\R3R_debug.log` 里 57 有两条完全不同的失败线：

| 日志锚点 | `why=` | 走的分支 | 本次是否覆盖 |
|---|---|---|---|
| `:1478 CPL-RET veh=57 … res=0x0 ret=0x8 spd=30` | `nores-final` | `:17853` 早退 | **覆盖**（本轮） |
| `:1727 CPL-RET veh=57 … tracks=0x9 res=0x8 ret=0x1 spd=0` | `cpl-fail` | `:17832` | 不覆盖（该分支已 `Free` + `if (mark_stuck) Mark`） |

⇒ 本轮修的是「**不订路却照样交轨**」这一类；`cpl-fail` 那条已有处置，`res=0x8` 说明它撤了自己的预留（`res` 是 `GetReservedTrackbits(tile) & DiagdirReachesTracks(enterdir)`，0x8 = 本格 LOWER 位仍在 ⇒ 属于 54 的，不是 57 的）。

**仍未修、必须继续跟进**：
- **KI-358**（高）`FSCP fail=tryReserve` —— 交接格 `14,15` 被 veh=54 路过占用，57 判负后停在 `15,14` 且 `GOTO_COUPLE` 永不推进 ⇒ 永久 `STUCK=1`；
- **KI-359**（高）`early-haverserved` 早退出口不填前瞻 —— `:17261` 命中 `res_tracks != TRACK_BIT_NONE` 就 `return { FindFirstTrack(res_tracks), CTTRF_NONE }`，**既无 `CTTRF_RESERVATION_MADE`、也不调 `FillTrainReservationLookAhead()`** ⇒ `lookahead == nullptr` ⇒ 逐格重订 + 反复撞同一格。修法 P1（加 `consist->lookahead != nullptr` 条件）仍待拍板。

⇒ 57 这次的实际现场（`:1716-1738`）是 **357 修好归属判据之后暴露的 KI-358/359**，本轮补的是另一条并行的漏口。三条要分别验。

### 34.3 构建自证（2026-10-28 00:30）

- 复用既有 `_tmp_inc_build.cmd`（未新建任何 `.cmd`）；
- `build\R3R_incbuild.guard.log` = `GUARD: incremental is safe (no header/lang file is newer than the newest object)`；
- 日志尾部 `[2/3] Building CXX object … train_cmd.cpp.obj` → `[3/3] Linking CXX executable openttd.exe`；
- `build\R3R_incbuild.done` = `EXIT_CODE=0`（00:30:59）；
- `src\train_cmd.cpp` 00:27:59 → `build\openttd.exe` **00:30:49（52 322 304 B）**（exe 晚于源码）；
- `read_lints` 0 条；
- exe 字面量自证命中 `CPL-NORES-BLOCK veh=` 与 `nores-couple-blocked`（`build\openttd.exe:47617/47618`）。

### 34.4 复测判据

1. **正判据**：GOTO_COUPLE 机车走 `nores-final` 且真的零预留时，日志出现 `CPL-NORES-BLOCK` + `CPL-RET … why=nores-couple-blocked`，且列车**当场停住**（`spd` 归 0、界面显示卡住），不再沿 `best_track` 前进；
2. **负判据（最关键）**：CPL 真订到路的正常挂接，仍出现 `why=nores-final`（或正常 `CTTRF_RESERVATION_MADE`），**不得**出现 `CPL-NORES-BLOCK`，列车照常开去车底；
3. 车库型挂接（`r3r_couple_depot`）行为逐字不变，不得出现新 why；
4. 普通订单（非 GOTO_COUPLE）走 `nores-final` 时行为逐字不变；
5. 不产生「停住 → 下 tick 重订 → 又停住」的高频抖动：`CPL-NORES-BLOCK` 每车每次状态变化只一行；
6. 57 场景：`CPL-NORES-BLOCK` 是否出现在 57 身上——若 57 仍走 `cpl-fail`，则不出现，属预期（见 §34.2，须另修 KI-358/359）。

## 35. 第 338 轮：新日志复测取证 + KI-359 修法 P1 落地（源码已改，构建待做）

### 35.1 输入与采样有效性

- 玩家投放日志：`D:\temp\codebuddy-dropped-files\a4c5db47-1344-4919-9f25-a709d5b8f1d4\R3R_debug.log`
- 关键结论都带行号锚点，**下文所有读数均以该文件为准**（不引用 `build\R3R_debug.log`）。

### 35.2 KI-509（`nores-final` 出口）——**正判据达成，负判据未被违反**

- 全文 `CPL-NORES-BLOCK` 命中 **1 次**、`why=nores-couple-blocked` 命中 **1 次**，二者成对出现在同一 tick 的 `CPL-RET` 上，且紧接其后该车 `spd` 由非零归 **0**。
- `nores-final` 全文命中 **0 次** ⇒ 修复后「CPL 跑过但一格没订到」再也没被静默交回 `best_track`；这条路要么被拦下（`nores-couple-blocked`），要么真的订到了路（正常 `CTTRF_RESERVATION_MADE`）。
- 对照：本次会话**成功挂接**（`COUPLE-OK loco=54 rear=26`）＋随后正常解挂（`DECOUPLE-DONE`），说明负判据「正常挂接不得出现 `CPL-NORES-BLOCK`」未被违反——挂接链路本身没被这次收紧误伤。
- **判定：KI-509 = 修复生效（正判据①达成、负判据②未违反）。** 玩家上一轮抱怨的「带速度硬冲（spd=30）」形态在本次日志中不再出现（57 全程 `spd=0`）。

### 35.3 57 的滞留点已经**换了**：不再是 `nores-final`，而是 `FSCP fail=tryReserve`

- 全文计数（同一份日志）：`CPL-SAFE-FAIL` = **22**、`fail=tryReserve` = **19**、`early-haverserved` = **0**、`RESV-FOREIGN-RES` = **0**、`CPL-CUT-BLOCKER` = **0**。
- 尾部逐行（`CPL-BACK` → 安全位判定）：
  - `CPL-PATHFOUND found=1` / `CPL-BEST veh=57 tile=5,15 td=0 cost=10455` ⇒ 挂接寻路**找到了**终点（等待车底 `PFD-ACCEPT kind=car-only tile=5,15`）。
  - `CPL-BACK pPrev=5,15 … parent=15,15` → `CPL-BACK pPrev=15,15 … parent=16,14` ⇒ 路径只有 3 个节点（15,15→5,15 是跟随器一步跳过整段站台的正常形态）。
  - `FSCP tile=15,14 fail=tryReserve td=3 want=3 type=1 tbits=0x29 res=0x1 plain=1 depot=0 st=0`
  - 紧接 `FSCP-RES tile=15,14 tk=0 owner=57 otile=16,14 … oend=15,14 oendtd=0 othru=1 octgt=0 ocplok=0`
  - 紧接 `FSCP-VEH tile=15,14 veh=59 track=0x1 …`
  - 然后 `CPL-SAFE-FAIL veh=57 tile=15,15` ⇒ `return INVALID_TRACKDIR` ⇒ 整条路作废。
- 读数解读（**猜测根因，待验证**）：`15,14` 的 `tbits=0x29`（X|LOWER|RIGHT）里 **`TRACK_X` 已被订**（`res=0x1`），而路径要从该格走 **`TRACK_LOWER`（want=3）**；X 与 LOWER 在同一格**几何交叉**，PBS 铁律「交叉轨不能同时预留」⇒ `TryReserveRailTrack` 必然失败，与「谁先订的」无关。
- 另有两条彼此矛盾的归属读数（**疑点 R338-A**）：`FSCP-RES` 报 `owner=57` 且 `otile=16,14`（即「预留者就是询路车自己」），而 `FSCP-VEH` 报该格站着 `veh=59`（`ord=0`=`OT_NOTHING`）。二者用不同 API（前者 `GetTrainForReservation` 顺预留找端点，后者实测格上车辆），yapf_rail.cpp 的注释已自陈这两路经常不一致（KI-162 第 90/91 轮），**不能据此断言"是 59 挡的"或"是 57 自己挡自己"**。
- 57 的空间形态（**新事实**）：`CPL-ENTRY veh=57 tile=16,14` 而 `TPR-WALK … mf=15,14` ⇒ 询路车的「物理链头」与「运动前端」不在同一格，是**双头/多节机车横跨 `16,14` 与 `15,14`** 的形态。因此「前端想转 LOWER、自己的后半节还压在 X 上」这种自交叉是**可能**的——但需实测确认。
- `TPR-WALK veh=57 … okay=0 exit=0 new=14,14 reach=0x17 spd=0 one=0 look=0` 反复出现 ⇒ 57 每 tick 都尝试、都拿不到预留，`lookahead` 恒为 `nullptr`（因为 `CTTRF_RESERVATION_MADE` 从未置位），车停死在 `16,14`。

### 35.4 因此本轮落地的**不是** γ，而是 KI-359 修法 P1

- 施工顺序原本写「γ（KI-358 截断）+ KI-359 P1」，且 P1 是 γ 的**前置**（γ 把预留终点收缩到阻塞格前一格后，只有 `lookahead` 真的存在，引擎才会在新终点前制动）。
- 本次日志证明 `early-haverserved` **一次都没触发**（`=0`），所以 P1 对**本次会话**是空操作——它的价值在于堵住「不订路、不前瞻、照样放行」这条结构性缺口，而不是让 57 立刻动起来。已在 KI-359 条目里写明，不夸大。
- 落地改动（`src\train_cmd.cpp`，`ChooseTrainTrack()` 的 `early-haverserved` 出口之前）：新增
  `const bool r3r_couple_no_lookahead = consist->current_order.IsType(OT_GOTO_COUPLE) && !consist->current_order.GetCoupleIsDepot() && consist->lookahead == nullptr;`
  并把 `!r3r_couple_no_lookahead` 追加进该出口的豁免条件；同时新增只读探针
  `RESV-NOLOOK-PASS veh=%d tile=%d,%d enterdir=%d res=0x%X tracks=0x%X spd=%d real=%d`（`R3RDbgOn()` 外层、按 `(res_tracks,tile,spd)` 签名节流）。
- 作用域刻意与既有两条豁免同域（挂接订单 + 非车库），普通 `GOTO_STATION/WAYPOINT` 逐字不变。
- `read_lints` = **0 条**。
- **构建自证（2026-10-28 01:08~01:21，玩家口头「现在构建吧」后重跑）**：复用既有 `_tmp_inc_build.cmd`（未新建任何 `.cmd`）；`build\R3R_incbuild.guard.log`=`GUARD: incremental is safe (no header/lang file is newer than the newest object)`；`build\R3R_incbuild.done`=`EXIT_CODE=0`（01:21:08）；时间戳链 `src\train_cmd.cpp` 01:08:48 → `build\CMakeFiles\openttd_lib.dir\src\train_cmd.cpp.obj` 01:09:33（15 167 410 B）→ `build\openttd.exe` **2026-10-28 01:11:39（52 336 640 B）**；`build\R3R_incbuild.log` 本轮仅 9 行（都是脚本尾部的 cmake always-run 输出 `-- Generating Doxyfile*`，**不含** `[n/n] Linking` 行），故链接是否发生**以时间戳 + 字面量自证为准**，不以日志文字为准；错误计数 `error C[0-9]|FAILED:|build stopped` = **0**；exe 字面量实测 `RESV-NOLOOK-PASS veh=`=**True**、`nores-couple-blocked`=**True**（后者证明上一轮 KI-509 的改动仍在同一 exe 内）；`read_lints` 0 条。
  - 备注：`R3R_incbuild.log` 只有 9 行且无 Linking 行，是本脚本「ninja 阶段输出未进入该日志」的既有现象（同一轮 exe/obj 时间戳已前进即为编译与链接都发生过的硬证据），不是构建失败的信号。

### 35.5 下一步（按依赖排序）

1. **补构建自证**（guard / done / `[n/n] Linking` / obj 与 exe 时间戳 / exe 字面量 `RESV-NOLOOK-PASS veh=`）。
2. **KI-358 方案 γ 落地**：`yapf_rail.cpp:200` 的 `tryReserve` 失败处带出 blocker（判据＝`GetReservedTrackbits(tile) != NONE` 且 `reserved & ~TrackToTrackBits(want)` 非空），回程走查遇同一 blocker 时**不 return**，改把预留终点收缩到阻塞格前一格（该格必须同时过 `IsSafeWaitingPosition()` 与 `IsWaitingPositionFree()`），打 `CPL-CUT-BLOCKER`。
3. **疑点 R338-A 取证**：`15,14` 的 `TRACK_X` 到底是谁订的。取数点＝在 `FSCP-RES` 之后补打 `FollowTrainReservation(57->First())` 的 `oend` 与该格预留位，以及 `59` 的 `tile/track/ord/spd`。若确认是「57 自交叉」，则 γ 要额外处理「本车自己后半节占位」，否则收缩点会退化成原点（等于不动）。

## 36. 第 339 轮：R338-A 取证探针落地（构建被「游戏在运行」拦下）

### 36.1 玩家拍板

玩家 2026-10-28「可以啊，按照你推荐的来」⇒ 按 §35.5 的顺序执行：**先取证再落 γ**，不在归属未明时动 `TryReservePath`/截断逻辑。

### 36.2 为什么现有三条探针答不出 R338-A

| 探针 | 它实际回答的问题 | 为什么不够 |
|---|---|---|
| `FSCP-RES owner=… otile=… oend=…` | 顺预留位回溯到的「端点列车」 | 回溯算法在 segment/整格位上会指向错误对象（`yapf_rail.cpp` 注释自陈；KI-162 第 90/91 轮同族），`owner=57` 与 `FSCP-VEH veh=59` 直接矛盾 |
| `FSCP-REACH`/`plain=`/`tbits=` | 该格**几何**上有哪些轨、能否被预留 | 只说"能/不能"，不说"被谁占了交叉轨" |
| `FSCP-VEH` | 该格像素上站着哪些车 | 站着的车未必是订位者；且 `ord=0`(`OT_NOTHING`) 的 59 可能只是路过/停放 |

### 36.3 新增两条只读探针（`src\pathfinder\yapf\yapf_rail.cpp`，紧跟 `FSCP-VEH` 之后、`fclose(dbg)` 之前）

1. `FSCP-CPL tile=x,y veh=N pos=x,y dir= spd= end=x,y endtd= look= real=`
   - `veh`/`pos`/`dir`/`spd` = **询路车链头**（`coupler->First()`）的现状；
   - `end`/`endtd` = `FollowTrainReservation(链头)` 给出的**权威预留终点**（这是引擎自己的口径，不是回溯启发式）；
   - `look` = `lookahead != nullptr`，直接验证「本车到底有没有前瞻」。
2. `FSCP-SELF tile=x,y veh=N track=0xX dir= front= overlap=0xX`
   - 只打印**询路车自己链上**、`tile` 等于阻塞格的每一节；
   - `overlap = c->track & rb`（`rb` 是被占住的轨位）⇒ **非 0 即自交叉实锤**。

### 36.4 判读口径（拿到新日志后直接对照）

- 出现 `FSCP-SELF … overlap=0x1`（或任何非 0）⇒ **本车后半节压在被占交叉轨上**：γ 的截断点会退化到 origin，必须先做「询路前收缩本车预留（只留到运动前端）」或「禁止规划自交叉机动」，需玩家再拍板；**不得**放宽 `TryReserveRailTrack` 的交叉互斥。
- 完全没有 `FSCP-SELF` 行（链上没有节落在该格）⇒ 占位者是**别的列车**：γ 的截断路径成立，按 §35.5 第 2 条落地。
- `FSCP-CPL … look=0` 且 `end=` 与阻塞格无关 ⇒ 印证 KI-359「无前瞻」的判断，P1 是 γ 的必要前置。
- `FSCP-CPL … end=x,y` 若恰好等于 `15,14` ⇒ 该格预留是**本车自己前瞻的终点**，同样是"自己挡自己"，但修法不同（属前瞻过长，不是交叉冲突）。

### 36.5 构建状态：被拦下，未产出新 exe

- 首次尝试：`_tmp_inc_build.cmd` 返回 `EXIT_CODE=97`，原因是 `openttd.exe` 正在运行（`Get-Process` 实测 PID **13320**）。护栏行为正确（`LNK1168` 前先拒），时间是 2026-10-28。
- 因此本轮**没有**新 `build\openttd.exe`：现有 exe 仍是第 338 轮产物（`RESV-NOLOOK-PASS` 在内、`FSCP-CPL`/`FSCP-SELF` **不在**）。
- 需玩家关闭游戏后重跑同一条命令（复用 `_tmp_inc_build.cmd`，不新建脚本）。
- 源码侧：`read_lints` = **0 条**；改动仅 `src\pathfinder\yapf\yapf_rail.cpp`（纯探针，无行为变更，未碰 `src\*.h`）。

## 37. 第 340 轮：R338-A **定案**（自车自占），γ 作废，改走方案 δ

### 37.1 新日志（玩家提供）

`D:\temp\codebuddy-dropped-files\47639d20-f567-464b-b894-858eb417c685\R3R_debug.log`，2 187 行 / 275 946 B / 10-28 01:40。`FSCP-` 行共 40 条。三组关键读数（原样）：

```
FSCP-CPL  tile=15,15 veh=57 pos=16,14 dir=5 spd=0 end=16,14 endtd=0 look=0 real=1
FSCP-CPL  tile=15,14 veh=57 pos=16,14 dir=5 spd=0 end=15,14 endtd=0 look=0 real=1
FSCP-CPL  tile=14,15 veh=57 pos=16,14 dir=5 spd=0 end=15,14 endtd=0 look=0 real=1
FSCP-SELF tile=15,14 veh=59 track=0x1 dir=5 front=0 overlap=0x1
```

### 37.2 定案结论（三条探针互相咬合，无剩余歧义）

| 证据 | 取值 | 含义 |
|---|---|---|
| `FSCP` 报的占位 | `tbits=0x29`（`X｜LOWER｜RIGHT`）、`res` 含 `X` | 该格是真的多轨格（交叉道岔），预留里已有 `X` 位 |
| `FSCP-RES owner=` | `57` | `GetTrainForReservation` 把该格归到**询路车自己**的链头名下 |
| `FSCP-SELF` | `veh=59 track=0x1 dir=5 front=0 overlap=0x1` | 57 编组里的**第 59 节**站在 `15,14` 的 `X` 轨上，与本车想订的位**位级重叠** |
| `FSCP-CPL` | `end=15,14`、`look=0`、`spd=0`、`real=1` | 本车**权威预留终点就是 15,14**；且**无前瞻**；车头停着 |

**因此：`15,14` 的 `TRACK_X` 不是别的车抢的，也不是回溯启发式指错——是询路车 57 自己的第 59 节压着该格，而 57 想再订该格的交叉位（`LOWER`），被 `TryReserveRailTrack` 的「交叉位互斥」拒绝。而那个互斥检查**不区分 owner**，于是本车被自己的预留挡死。**

- 上一轮把 `owner=57` 与 `FSCP-VEH veh=59` 当作「矛盾 ⇒ 回溯不可信」，**该判断撤回**：59 是 57 链上的节，`GetTrainForReservation` 归到链头 57 是正确行为。
- 由此把 KI-359 的旁证也钉死：`look=0` 在这 40 条 `FSCP` 里稳定复现，**无前瞻是常态**，不是偶发。

### 37.3 方案 γ 作废（不是"先不做"，是"做了也无效"）

γ = 把预留终点收缩到「阻塞格靠 origin 侧的那一格」。本例阻塞格 `15,14`，靠 origin 侧的相邻格正是 **`16,14` = 车头当前位置**。收缩结果＝预留终点落在车头脚下＝**原地不动**，退化到比现状更差（现状至少还保留 `end=15,14`）。故 γ 在自占场景下**必须废弃**，不再排期。

### 37.4 取代方案 δ：同链豁免（识别「自己挡自己」，不放宽互斥）

- **改动点**：`src\pathfinder\yapf\yapf_rail.cpp` 的 `tryReserve` 分支（:199-200）。在执行 `TryReserveRailTrack` 判失败**之前**，先遍历该格已预留的每个 `tk`，用 `GetTrainForReservation(tile, tk)` 取 owner；若 `owner->First() == coupler->First()` ⇒ 该格是**本车自己持有**，`CheckSafePositionOnNode` 直接视为「不阻塞」继续走查，不进失败分支。
- **为什么安全**：完全不碰 `TryReserveRailTrack` 的交叉互斥，也不改 `GetReservedTrackbits` 的位级语义；只把「冲突对象是本车自己」这一种情况从失败路径摘出来。同一列车不可能靠"撞自己"造成事故；真正的几何重叠由 tile 上车辆占用检查（`VehiclesOnTile`/`TryPathReserve` 的占地判定）兜底。
- **可观测**：新增 `FSCP-SELFHELD tile=x,y veh=N tk=… want=…` 一条，证明该豁免真的被触发，而不是"看起来修好了"。
- **回退开关**：若复测出现真重叠/穿模，δ 需整段回退（保留探针），不得在其上叠加"放宽整格预留"之类的补丁。

### 37.5 构建自证（2026-10-28 01:41~01:59，玩家「游戏已关」后重跑）

| 项 | 值 |
|---|---|
| 前置 | `Get-Process openttd` 无进程（上一次 PID 16824 已退出） |
| guard | `GUARD: incremental is safe (no header/lang file is newer than the newest object)` |
| 源码 | `src\pathfinder\yapf\yapf_rail.cpp` **01:41:41**（95 480 B，含 δ + 三条探针） |
| obj | `yapf_rail.cpp.obj` **01:58:22**（8 641 516 B，晚于源码） |
| exe | `build\openttd.exe` **01:59:46（52 336 640 B）**，晚于 obj |
| done | `EXIT_CODE=0` |
| 错误 | `error C[0-9]\|FAILED:\|build stopped` = **0** |
| 字面量 | `FSCP-SELFHELD tile=`=**True**、`FSCP-SELF tile=`=**True**、`FSCP-CPL tile=`=**True** |
| lints | 0 |

一句话：**δ 已进 exe**，且 `FSCP-SELFHELD` 存在说明豁免路径是可选中的代码，不是被优化掉的分支。

### 37.6 复测判读（δ 生效 / 无效 两种形态）

- **生效**：57 在 `16,14` 不再卡死，日志出现 `FSCP-SELFHELD tile=15,14 veh=57 tk=0 want=3 res=0x1`，且该车随后离开 `16,14`、寻到挂接位。
- **生效但仍有滞留**：`FSCP-SELFHELD` 出现，但下游换成别的格报 `fail=tryReserve`（即冲突对象换成了**别人**）⇒ 属新问题，按 `FSCP-RES owner` 是否等于 57 区分，不要回退 δ。
- **无效**：完全无 `FSCP-SELFHELD`，仍出现原样 `FSCP tile=15,14 fail=tryReserve` ⇒ 说明 `GetTrainForReservation` 在整格位上没返回本车（位级 API 语义问题），δ 判据要换成 `GetReservedTrackbits` + `VehiclesOnTile` 的显式比对。
- **回退信号**：出现列车穿模/两节重叠同轨 ⇒ 立即回退 δ 整段。

## 38. 第 341 轮：δ **被实测证伪**（机车走错岔道）——回退，原行为其实是对的

### 38.1 玩家复测结论

「机车直接走错岔道了」。新日志 `R3R_debug.log`（2 486 行 / 308 721 B / 10-28 02:02）。计数：`FSCP-SELFHELD`=**1**、`FSCP-SELF`=0、`FSCP-CPL`=2、`FSCP tile=`=5、`CPL-CUT`=0。

### 38.2 证据链（同一次 tick=12670 内）

```
1799:FSCP-SELFHELD tile=15,14 veh=57 tk=0 want=3 res=0x1     ← δ 放行了（tk=0=X，want=3=LOWER）
1800:UNRES-RAIL tile=15,14 track=3 phase=yapf-fscp actor=57   ← 立刻又把这格的路径预留撤了
1802:REACH cur=15,14 td=3 … bits=0x29                        ← 计划：15,14 走 LOWER（北）
1803:REACH cur=15,15 td=2 …
1806:CPL-FIRST-TD … at=15,14 tdb=0x2929 ok=1 best=3 start=15,14 trk=3
1812:CPL-RESERVE veh=57 reserved=1 next=3
1813:RESV-CPL-XTILE veh=57 tile=14,14 tracks=0x11 resv=0x0 ret=0x3 accept=0x11   ← 实到 14,14（西），不是 15,15
1814:CPL-RET veh=57 tick=12670 tile=14,14 enterdir=0 tracks=0x11 res=0x0 ret=0x1 why=cpl-partial-res
```

**计划方向 = `15,14` 的 `LOWER`（北，去 `15,15`→站台），实际推进 = 沿 `X` 位**直行到 `14,14`（西）。车头过了 `15,14` 之后走的是「本车已预留的 X 位」，而 yapf 画的是 LOWER，两者不一致 ⇒ 走错岔道。

### 38.3 机制结论（δ 为什么必错）

- `15,14` 的 `X` 位是**本车自己**预留/占住的，道岔物理上被锁在 X。车头进入该格只能沿 X 走。
- δ 把「本车持有该格」当成「不阻塞、可通过」，于是 yapf 放心地把 LOWER 写进计划——**计划与道岔状态矛盾**。
- 因此 **第 340 轮之前的「失败」才是正确行为**：`TryReserveRailTrack` 拒绝 LOWER 恰好在说「这格的道岔位已经归 X，你过不去」。它不是误判，是唯一正确的判定。

### 38.4 处置

1. **回退 δ 的行为**（`if (!self_held && !TryReserveRailTrack(...))` → `if (!TryReserveRailTrack(...))`），代码回到第 339 轮语义：该格照旧失败、车照旧停在 `16,14`（正确但不动）。
2. **保留诊断**：`self_held` 计算与 `FSCP-SELFHELD` 行保留，并在 `FSCP` 主行加 `self=%d`，这样"这格的自锁是我自己造成的"一眼可见，不必再复现一轮。
3. **真修法必须在上层**（不在 `TryReserveRailTrack`，也不在 §35.5 的 γ）：
   - 该机动在**计划层**就不可行——列车要穿过「自己被锁在另一方向」的交叉格；
   - 可走的两条：①挂接寻路把这种「自锁交叉」路径判为不可达，另找挂接位；②先让本车**倒车/退出** `15,14` 释放 X 位，再重新规划。二者都要动 `FindSafeCouplePosition`/上层调度，属新方案 ε，需玩家拍板后再做。
4. 备忘里的 δ 段落（§37.4）保留原文不删，标注「已证伪」，避免以后重复提出。

### 38.5 回退后的构建自证（2026-10-28 02:03~02:05）

| 项 | 值 |
|---|---|
| 源码 | `yapf_rail.cpp` **02:03:11**（96 192 B） |
| obj | `yapf_rail.cpp.obj` **02:03:57**（8 641 512 B，晚于源码） |
| exe | `build\openttd.exe` **02:05:20（52 336 640 B）**（晚于 obj） |
| done / 错误 | `EXIT_CODE=0` / **0** |
| 字面量 | `st=%d self=%d`=**True**（新诊断已进 exe）、`FSCP-SELFHELD tile=`=**True** |
| lints | 0 |

语义自证：判定行已恢复为 `if (!TryReserveRailTrack(tile, TrackdirToTrack(td)))`，`self_held` 只在日志中出现 ⇒ 下次复测**不应**再出现"走错岔道"，应回到「车停在 `16,14`、该格 `fail=tryReserve self=1`」。若仍是走错岔道，说明错岔另有来源（不在 `FindSafeCouplePositionProc`），需另立新条目。

## 39. 第 342 轮：方案 ε-A 落地（自锁 = 不可达，不走"半截预留继续开"）

### 39.1 玩家拍板

2026-10-28「选A」⇒ 采纳 §38.4 的 ε-A：**把自锁交叉判为不可达，另找挂接位/等待位**；不改位级互斥、不做 ε-B 的倒车。

### 39.2 真正的病灶是哪个出口（复查上一份日志）

第 341 轮日志 `:1814` 的出口是 **`why=cpl-partial-res`**，对应 `train_cmd.cpp` 的 `res_dest.tile != INVALID_TILE && !res_dest.okay` 分支里的挂接特例：**保留半截预留并直接把 `best_track` 交回**。于是车带着"计划 LOWER、道岔锁 X"的矛盾继续跑 —— 这才是"走错岔道"的直接门。ε-A 就关这扇门。

### 39.3 改动（两文件、只碰 `.cpp`，不碰 `.h` 以免全量重编）

1. `src\pathfinder\yapf\yapf_rail.cpp`
   - 文件顶部（沿用 :35-38 既有的"手写前向声明"做法）新增 `R3RNoteSelfLock()` / `R3RTakeSelfLock()` / `R3RClearSelfLock()` 与两个 static 变量：yapf 对外只返回 `PBSTileInfo`，没有失败原因位，又改不得 `.h`，故用这条"最近一次自锁"通道把原因交回调用方。
   - `TryReservePath()` 入口 `R3RClearSelfLock()`（每次尝试从"无自锁"开始，不可能跨调用陈旧）。
   - `tryReserve` 失败分支：`if (self_held) R3RNoteSelfLock(tile, coupler->index);` —— **只有"失败且占位者是本链"才算自锁**；`self_held` 但 `TryReserveRailTrack` 成功（两位不交叉）不算。
2. `src\train_cmd.cpp`（`:17946` 那个 `cpl-partial-res` 特例之前）
   - `extern bool R3RTakeSelfLock(VehicleID, TileIndex *);`（块作用域手写声明）
   - 命中即：`CPL-SELFLOCK veh=… pos=… locked=… why=self-cross-unreachable` → `FreeTrainTrackReservation` → `MarkTrainAsStuck` → `return r3r_chosen_ret(best_track, result_flags, "selflock-unreachable")`。

### 39.4 语义边界

- 这一版做的是 A 里的**「判不可达 + 等待位」**：自锁路线不再被采用，车撤预留、标卡住，下一轮扫描重新评估（因此后续仍可能换到别的挂接位）。
- **未做**：路径级"换候选"排除表。现有 CPL 扫描（`yapf_destrail.hpp`）只按目标格过闸门、不知道中途路径，真正"按路径排除候选"属架构改动，需单独一轮。
- 不改 `TryReserveRailTrack`、不改 `GetReservedTrackbits`、不碰 `IsSafeWaitingPosition`。

### 39.5 构建自证（2026-10-28 02:09~02:12）

| 项 | 值 |
|---|---|
| `train_cmd.cpp` | 02:09:23 → obj **02:10:25**（15 168 816 B） |
| `yapf_rail.cpp` | 02:09:16 → obj **02:10:23**（8 644 605 B） |
| exe | **02:12:05（52 336 640 B）** |
| done / 错误 | `EXIT_CODE=0` / **0** |
| 字面量 | `CPL-SELFLOCK veh=`=**True**、`selflock-unreachable`=**True** |
| lints | 0 |

### 39.6 复测判据

- **正判**：出现 `CPL-SELFLOCK veh=57 pos=16,14 locked=15,14 why=self-cross-unreachable`，且 57 **不再**出现 `16,14 → 15,14 → 14,14` 的西行（`LOCO-MOVE` 里不出现 `14,14`）。
- **可接受**：57 停住等待（`STUCK`/`spd=0`），随后换到别的挂接位成功。
- **仍错岔**：若 `CPL-SELFLOCK` 出现但车照样西行 ⇒ 说明半截预留不是这条出口给的，需查 `KeepTrainReservation`/`ExtendTrainReservation` 的调用方。
- **完全无 `CPL-SELFLOCK`** 却仍错岔 ⇒ 该次失败不是走 `!res_dest.okay` 分支（可能走 `nores-couple-blocked`），需按新出口再定位。


