# 第 509 轮：KI-685 修复后复测 —— 「预留超出站台」仍未解决

日期：2026-11-25
现场日志：`build\R3R_debug.log`（297 239 B，mtime 2026-11-25 22:00）
产生者：`build\openttd.exe`（54 397 952 B，2026-11-25 21:55:28，即第 508 轮 KI-685 修复版）
玩家原话：「崩溃已经消失，请见 build/R3R_debug.log，但预留超出站台的问题依然存在」

---

## 一、正判据：KI-685（station_map.h:31 断言崩溃）已解除

- 全日志无 `station_map.h` 断言、无 `GetStationIndex` / `GetRailStationAxis` 崩溃行。
- 会合点判定仍生效，终点仍是 1439：
  - `build\R3R_debug.log:1469` `CPL-SCAN tick=41499 veh=15 found=1 ... last=1439,1874 lastveh=18 why=acc-car`
  - `:1470` `CPL-HEAD-STATIC veh=15 best=1439,1874 snap=1439,1874 meetvalid=1 meet=1439,1874 farvalid=1 head=1439,1874 on=0`
  - `:1471` `CPL-BEST veh=15 tile=1439,1874 td=0 cost=11554`
  - `:1461..1467 / :1591..1597 / :1673..1674` `PFD-MEET kind=car-only tile=1439,1874 t=18 n=…`
  ⇒ 第 504/508 轮的「会合点 = 1439,1874 作为挂接终点」功能逐条成立。

## 二、负判据：预留仍超出站台（问题未解决）

原文锚点：

| 行号 | 原文 |
|---|---|
| `:1452-1460` | `PFX-GEO t=1436/1437/1438,1874 … stn=1` ／ `t=1439..1444,1874 rail=1 stn=0` |
| `:1497-1512` | `RP cur=` 回溯 16 步，末为 `cur=1464,1878 td=13` |
| `:1513-1535` | 连续 23 行 `RESV-SELF-AIR veh=15 tile=… track=… kind=rail` |
| `:1537` | `CPL-RES-SAFE-STOP veh=15 nth=43/44 tile=1440,1874 td=0 booked=44 goal=1` |
| `:1538` | `CPL-RES-OK veh=15 total=44 booked=44 stop_safe=1 goal=1 tiles=1473,1884/0;…` |
| `:1539` | `CPL-RESERVE veh=15 reserved=1 next=0 path=44 booked=44` |
| `:1540` | `CPL-RET veh=15 tick=41499 tile=1473,1884 enterdir=0 tracks=0x11 res=0x1 ret=0x1 why=cpl-partial-res spd=0 real=1` |

读法（初步）：预期挂接锁定段应止于 **1440,1874**（= 1439 的外侧一格，安全停车位），实测 `CPL-RES-SAFE-STOP`/`CPL-RES-OK`/`CPL-RESERVE` 三行都是 `booked=44 / 44`，即整条进路全订满；玩家观感为「预留一直铺过去、盖过了站台」。

## 二·补　逐格读数（2026-11-25 取数）

**A. 挂接机车 veh=15（场景一，站台 1424..1438 / y=1874）**

- 摸路终点确实是会合点 1439：
  - `:1469` `CPL-SCAN ... last=1439,1874`
  - `:2081-2091` `CPL-TRACE st=1450,1874` + `RP cur=1449,1874 … cur=1439,1874 td=0 depth=10`
  - `:2177-2180` `CPL-TRACE st=1442,1874` + `RP cur=1441 … cur=1440 … cur=1439 depth=3`
- **但真正落笔订下的最后一格是 1440，不是 1439**：
  - `:1537` `CPL-RES-SAFE-STOP veh=15 nth=43/44 tile=1440,1874 td=0 booked=44 goal=1`
  - 代码 `src\pathfinder\yapf\yapf_rail.cpp:878-889` 在 `i + 1 == steps.size()` 时 break，而 `:857` 的 `++booked` 在其之前 ⇒ `steps[43] = 1440,1874` 是 `steps.back()`。
  - `:1538` `CPL-RES-OK veh=15 total=44 booked=44 stop_safe=1 goal=1 tiles=1473,1884/0;…(truncated)`
  - `:1539` `CPL-RESERVE veh=15 reserved=1 next=0 path=44 booked=44`
  ⇒ 本轮预留覆盖 `1473,1884 … 1440,1874` 共 44 格；**1439,1874（会合点本身）不在 steps 里**，与 `:858-877` 注释「目的地格自己必须被订下」不符。
- 站台格范围：`PFX-GEO t=1436/1437/1438,1874 … stn=1`、`t=1439,1874 … stn=0` ⇒ 站台末端 = 1438，站台外 = 1439 起。预留止于 1440 ⇒ 站台外两格（1439、1440）在预留范围内，站台内 0 格。

**B. 另一列车 veh=75（场景二，站台 1484,1875）**

- `:678` `RESV-WATCH SET-STN tile=1484,1875 track=255 phase=yapf-resv actor=75 head=75`（站台整格位）
- `:870/:872` `SET-RAIL tile=1483,1875 track=0 phase=choose-track` ／ `SET-RAIL tile=1459,1873 track=0 phase=yapf-resv`
- `:905/:907` `CLEAR-STN tile=1484,1875 phase=controller` ／ `CLEAR-RAIL tile=1483,1875 phase=controller`
- `:954-959` `RESV-HAVERES veh=75 tile=1441,1874 / 1440 / 1439 / 1438 / 1437 …`（行驶中自订）
⇒ 场景二预留正常配对清除，未见泄漏。

## 三、疑点登记（先落盘，待取证）

- **D-1（疑点·现象口径）**：「预留超出站台」的准确所指尚未与玩家对齐。两种候选：
  - (a) 预留轨道**图形**越过 1438（站台近端）继续显示到 1439/1440；
  - (b) 预留**实际订下的格数**多于「到安全停车位」所需（例如该止于 1440 却订到了 1439 甚至更里）。
  ⇒ 需按 `CPL-RES-OK` 的 `tiles=` 全表逐格核对实际订下的最后一格。
- **D-2（疑点·新探针语义）**：`RESV-SELF-AIR` 这一标签本轮才在日志里大量出现（23 行/次），其含义（是否「把预留订到了车自己所在格的『空气轨』」）需查源码确认；若它代表「订了一条车根本不会走的轨」，则与 KI-346（第 240 轮 rp() 订错 Track）同族。
- **D-3（疑点·`why=cpl-partial-res`）**：`CPL-RET` 返回 `cpl-partial-res` 而 `CPL-RESERVE` 又报 `reserved=1`，即「部分预留」与「成功」同时成立，判读口径需复核。
- **D-4（疑点·`next=0`）**：`CPL-RESERVE ... next=0` 与 `CPL-RES-OK ... goal=1` 并存，`next` 字段语义待查（是否 = 剩余未订格数）。

## 四、本轮已排除

- 崩溃未复发 ⇒ KI-685 的 `IsRailStationTile` 守卫有效，且**不是**本次「预留超出站台」的成因（两者相互独立）。
- 排除「终点判定退回 1424/1437」：全量 `CPL-HEAD-STATIC` / `CPL-BEST` / `PFD-MEET` 均为 1439,1874，无一条落在 1424/1437。

## 五、待验证 / 下一步

1. 取全 `CPL-RES-OK` 的 `tiles=` 明细（日志该行被截断为 `...(truncated)`），确认实际订下的最大/最小格坐标。
2. 查 `RESV-SELF-AIR` 探针源码，判定是否「订了不走的轨」。
3. 复核 `CPL-RES-SAFE-STOP` 的 `nth=43/44`：43 是「0 基第 43 格 = 第 44 格 = 1440,1874」，与 `goal=1` 组合是否说明「最后一格（1440）已订、而 1439 未订」，若如此则预留**并未**越过 1440。

## 六、复测判据（本轮修复后）

1. 同场景不再出现「比安全停车位更靠里」的预留格（按 `CPL-RES-OK` 的 `tiles=` 逐格核对）。
2. `CPL-RES-SAFE-STOP` 的 `nth` 与 `booked` 的差 ≤ 1（安全停位那一格不计入 booked 属正常）。
3. 站台格（1424..1438）上不出现 `RESV`/`SET-RAIL` 之外的越界订位。
4. 负判据：普通进站列车（无挂接）的预留不变。

## 七、口径澄清（2026-11-25，玩家第 3 次澄清）+ 两次预留取证

玩家口径（原话要点）：
- **(1423–1421, 1874) 与 (1421–1419, 1873)** 这 6 格被预留**占有过两次**；
- 第一次 = **veh=75 的正常预留路径**（合法）；
- 第二次 = **veh=15/17 的「超出站台的预留」**，第二次**不应该存在**。

### 7.1 第一次：veh=75 自我预留（正常，合法）

锚点全部取自 `build\R3R_debug.log`（本次重新落盘 `build\_out4.txt` 后逐行读原文）：

| 日志行 | 原文（节选） |
|---|---|
| `:1241` | `TPR-WALK veh=75 mf=1424,1874 origin=1424,1874 td=0 okay=1 exit=0 new=1423,1874 reach=1 spd=3 one=1` |
| `:1243` | `RESV-HAVERES veh=75 tile=1423,1874 … res=0x0 tracks=0x1 spd=3 real=2 order=6` |
| `:1245` | `CRT veh=75 order=6 dir=1 origin=1419,1873 td=0 found=1` |
| `:1270` | `RESV-HAVERES veh=75 tile=1423,1874 … res=0x1 tracks=0x1 spd=27` |
| `:1272` | `RESV-HAVERES veh=75 tile=1422,1874 … res=0x1 tracks=0x1 spd=69` |
| `:1273` | `RESV-HAVERES veh=75 tile=1421,1874 … res=0x10 tracks=0x10 spd=80` |
| `:1274` | `RESV-HAVERES veh=75 tile=1421,1873 enterdir=3 res=0x20 tracks=0x20 spd=80` |
| `:1275` | `RESV-HAVERES veh=75 tile=1420,1873 … res=0x1 tracks=0x1 spd=80` |
| `:1277` | `RESV-HAVERES veh=75 tile=1419,1873 … res=0x1 tracks=0x1 spd=80` |
| `:1279` | `RESV-HAVERES veh=75 tile=1418,1873 … res=0x1 tracks=0x11 spd=80` |

读法：`spd` 3→27→69→80 递增、格位 1423→1418 单调推进 ⇒ **真实行驶中的自我预留**，路径合法。其轨道位是 `0x1 / 0x10 / 0x20 / 0x11`（X 轴 / 左斜 / 右斜 / 双槽）。

### 7.2 第二次：veh=15 的挂接寻路（不该存在）

| 日志行 | 原文（节选） |
|---|---|
| `:1682` | `FSCP-RES tile=1447,1874 tk=0 owner=15 otile=1474,1884 odir=1 ospd=39 ostop=0 ord=16 oroi=1 oend=1419,1873 oendtd=0 othru=1 …` |
| `:1683` | `FSCP-REACH tile=1447,1874 veh=15 ord=16 roi=1 pos=1474,1884 dir=1 spd=39 … end=1419,1873 endtd=0 isGtr=1` |
| `:1684` | `FSCP-CPL tile=1447,1874 veh=15 pos=1474,1884 dir=1 spd=39 end=1419,1873 endtd=0 look=0 real=1` |
| `:1904-1906` | 同形，`pos=1460,1875 dir=1 spd=160`，仍 `end=1419,1873 endtd=0 isGtr=1` |
| `:1959-1961` | 同形，`pos=1457,1874 dir=0 spd=160`，仍 `end=1419,1873 endtd=0 isGtr=1` |
| `:1985-1987` | 同形，`pos=1456,1874 dir=1 spd=160`，仍 `end=1419,1873` |
| `:2040-2042` | 同形，`pos=1451,1874 dir=1 spd=160`，仍 `end=1419,1873` |
| `:2466` | `UNRES-RAIL tile=1419,1873 track=0 phase=release-ghost actor=17` |
| `:2467` | `RESV-WATCH CLEAR-RAIL tile=1419,1873 track=0 phase=release-ghost actor=17 head=17` |
| `:2468-2474` | `RESV-GHOST couple veh=17 tile=` **1419,1873 track=0 / 1420,1873 track=0 / 1421,1873 track=5 / 1421,1874 track=4 / 1422,1874 track=0 / 1423,1874 track=0** |

判定（玩家口径**成立**，且有直接锚点）：
1. veh=15 的挂接寻路（`ord=16`=OT_GOTO_COUPLE、`roi=1`）**五次采样全部**把 YAPF 终点算成 `end=1419,1873 endtd=0 isGtr=1` —— 正是玩家点名的第 6 格（最西一格），且 `isGtr=1` 在五次采样里恒定。
2. 预留轨迹 `1423,1874 → 1422,1874 → 1421,1874 → 1421,1873 → 1420,1873 → 1419,1873` 与玩家点名的两段**逐格重合**：先向西越过站台西端（1424 站台首格），再在 1421 拐上 y=1873 的平行线。
3. 第二次的轨道位是 `track=0 / 4 / 5`，与第一次 veh=75 的 `0x1 / 0x10 / 0x20 / 0x11` **不同** ⇒ 确系**第二批位置**，不是 veh=75 的残留。
4. `veh=17` 只是 `phase=release-ghost` 的**释放者**（actor=17/head=17），**不是订者**；`:2466` 的 `UNRES-RAIL` 与 `:2470` 的 `CHG-RAIL`（reserved bits 由有变无）反证这些位**确实曾被订上**。

### 7.3 猜测根因（待验证，勿当结论）

- **H-1（偏可能）**：`end=` 打的不是 YAPF 目标，而是 `rp()` 回溯链的末格。若如此，`1419,1873` 是回溯走偏的产物 —— 与 KI-163 修掉的「站台跳段到达判定沿轴向正反盲扫 32 格、只做坐标加减」同族（`isGtr=1` 很像"轴向 greater 方向命中"的标记）。
- **H-2**：`end=` 就是 YAPF 目标，则挂接寻路的目的地判定把「车底所在站台段的西侧端点」算成了 1419,1873（站台首格 1424 的西侧一格是 1423，却落到 1419，且换到平行线）。
- 无论 H-1/H-2，本轮暴露的**共性**是：挂接寻路的终点/回溯终点可以落在**车底所在站台段之外**，且允许换到平行线（y=1873）。

### 7.4 待验证 / 下一步（并入 §五）

1. 查 `FSCP-RES` / `FSCP-REACH` / `FSCP-CPL` 三行的打印点（`src\pathfinder\yapf\yapf_rail.cpp` 的 FindSafeCouplePosition 区块），确认 `end` 取的是 `best.end_tile`（YAPF 目标）还是回溯 `cur`，一并确认 `isGtr` 语义 ⇒ 判 H-1 / H-2。
2. 本会话里 veh=15 的 `CPL-BEST` / `CPL-ENDTILE` 值（若已是 1419,1873 ⇒ H-2；若仍是 1424/1438 ⇒ H-1）。
3. 修法方向（待取证后定）：挂接寻路的到达判定必须以「车底所在站台段的可行端点」为唯一依据；或在其后加一道闸门「终点必须与 couple 目标同站台段/同轨」，否则丢弃该路径。

### 7.5 复测判据（修订，替换 §六 第 1、3 条）

1. 同场景下 1423–1419 这 6 格**只在 veh=75 通过时**出现预留；veh=15 的挂接寻路不再在其上留位（`RESV-GHOST couple veh=17` 这 6 行消失）。
2. `FSCP-*` 的 `end=` 必须落在车底所在站台段内（1424..1438,1874）或其东侧安全停位（1439/1440），不得出现 y=1873/1875/1876 的平行线坐标。
3. 站台 1424..1438 与站台外西端 1419..1423 上不再出现 `track=4/5` 的第二批位。
4. 负判据：veh=75 一类普通行驶列车的自订预留轨迹逐格不变。

## 八、玩家判定：KI-684 的 S5 修法**未生效**（2026-11-25）+ `end=` 语义订正

玩家原话要点：**「KI-684 的修复并没有生效」**。

### 8.1 先订正我上一轮的两个猜测根因（H-1 / H-2 均作废）

§7.3 的 H-1（`end=` 是 `rp()` 回溯链末格）与 H-2（`end=` 是 YAPF 目标、目的地判定越界）**都不对**。源码逐行确证（`src\pathfinder\yapf\yapf_rail.cpp`，全部位于 `FindSafeCouplePosition` 的 `CheckSafePositionOnNode` 检查块内）：

| 探针 | `end=` 的真实来源 | 代码锚点 |
|---|---|---|
| `FSCP-RES ... oend=` | `FollowTrainReservation(oh, nullptr)`，`oh = GetTrainForReservation(tile,tk)->First()` ＝ **占住该格的预留 owner 链头** | `yapf_rail.cpp:486-492` |
| `FSCP-REACH ... end=` | `FollowTrainReservation(tr, nullptr)`，`tr` 为 `TrainReservationPassesThroughTile()` 成立的链（"这条链的预留可达该格"，**不代表所有权**，函数自陈见 `:503-518`） | `yapf_rail.cpp:526,533-542` |
| `FSCP-CPL ... end=` | `FollowTrainReservation(coupler->First(), nullptr)`，即 **询路车自己**的预留末端 | `yapf_rail.cpp:572-579` |

`isGtr` 的语义同样订正：`:541` 打印的是 `(int)(gtr == tr)`，**"这条链就是该格预留名义 owner"**，与"轴向 greater 方向命中"毫无关系。

⇒ 结论：`FSCP-*` 三行**完全不携带 YAPF 目标信息**，它们只回答"谁的预留铺到了这里、铺到哪一格为止"。用它们反推"会合点算错"是错的。

### 8.2 读数重解：那 6 格是 **veh=15 自己的预留**，末端恒为 1419,1873

- `:1682` `FSCP-RES tile=1447,1874 tk=0 owner=15 ... oend=1419,1873 oendtd=0 othru=1` ⇒ 该格预留的 owner 回溯到 **veh=15 自己**，其**预留末端**是 `1419,1873`。
- `:1683` `FSCP-REACH ... veh=15 ... end=1419,1873 ... isGtr=1` ⇒ `gtr == tr`，即 veh=15 就是该格预留的名义主人（与上一条互证）。
- `:1684` `FSCP-CPL ... veh=15 ... end=1419,1873 endtd=0` ⇒ veh=15 自己 `FollowTrainReservation()` 的末端同样是 `1419,1873`。
- 五组采样（`:1682-1684`、`:1904-1906`、`:1959-1961`、`:1985-1987`、`:2040-2042`）**全部**是 `1419,1873`，随 `ospd` 39→160 变化而不变。

⇒ 因此"第二次占有"这 6 格的确是 veh=15 的挂车寻路铺出的预留，且**它的预留终点就是 1419,1873**（越过站台西端 1424，并换到 y=1873 平行线）。这不是会合点（1439,1874）该出现的位置，方向完全相反。

### 8.3 为什么 S5 落地了却没生效（推断，待验证）

KI-684 状态修订 10 的 S5 改动口径是「让会合点成为唯一 destination + `res_target_tile`/`tg`/destination 判定三处同用会合点」，其作用面是**挂接寻路的目标格**。但实测读数显示：**真正铺出这 6 格的预留，其终点仍是 1419,1873**，而 S5 之后的目标应是 1439,1874。二者矛盾 ⇒ 至少有以下一种情形成立：

- **S5-a**：这条越界预留不是由 `res_target_tile` 决定的，而是 `rp()` 里「补齐车头到车底之间站台预留」那段循环沿 `cur_td` 反向补订出来的（该循环自 KI-346 起才改用真实 step，方向仍由回溯链决定），会合点换了也拦不住它。
- **S5-b**：`R3RComputeMeetTile()` 在本场景返回 `INVALID_TILE`，于是按设计"整链退回改动前行为"（修订 10 明写的失败边界），会合点闸门根本没参与。
- **S5-c**：越界预留是**改动前铺下、一直未清**的存量位（`reserve` 位随存档保存），S5 只影响新铺路径，不回收旧位。

判别（下一轮取证，不必再写新探针）：
1. 本场景日志里 `CPL-HEAD-STATIC` 是否同时出现 `meetvalid=1 meet=1439,1874`。若 `meetvalid=0` ⇒ S5-b。
2. `RP cur=` 逐格序列的**末端**是否为 1419,1873（若是，说明是 rp() 自己铺到那里 ⇒ S5-a）；结合 `CPL-BEST`/`CPL-TRACE-RESULT` 判断铺路起点是 meet 还是车底。
3. 该 6 格的订位时刻（`SET-RAIL`/`RESV`）是否早于本会话的挂接寻路 ⇒ S5-c。

### 8.4 口径更新

- **KI-684 状态改为：修法已落地并编译，但实机确认未生效（玩家 2026-11-25）**，不再按"已修/待复测"对待。
- `passes_through`（`FSCP-REACH` 的 `tr`）**不是所有权**；今后任何"订者是谁"的判断只能用 `FSCP-RES` 的 `owner=` 或 `FSCP-VEH`，禁止用 `FSCP-REACH` 的 `isGtr` 反推归属。

## 九、玩家三条取证（2026-11-25）：S5-a / S5-b / S5-c **全部排除**

玩家原话要点，编号留档：

- **P-1**：「日志内所有 `CPL-HEAD-STATIC` 均有 `meetvalid=1`」
- **P-2**：「日志内没有找到 `RP cur=` 序列内有 `1419,1873`」
- **P-3**：「我保证，这六格位为当前会话产生，绝非残留」

逐条对账 §8.3 的三候选：

| 候选 | 预期读数 | 实测 | 判定 |
|---|---|---|---|
| S5-b `R3RComputeMeetTile()` 返回 `INVALID_TILE` | `meetvalid=0` | P-1：全部 `meetvalid=1` | **排除** |
| S5-a 越界段由 `rp()` 的补齐循环反向补订 | `RP cur=` 末端含 `1419,1873` | P-2：序列内没有该格 | **排除** |
| S5-c 改动前铺下的存量预留 | 订位时刻早于本会话 | P-3：本会话产生 | **排除** |

⇒ 三条同时不成立，只剩一个逻辑结论：**这 6 格既不是挂接寻路（`rp()`）铺的，也不是旧残留，而且会合点本身解析正常。** 也就是说，KI-684 的越界预留**产生于另一条预留写入路径**，S5 改的地方（目标格/会合点）对这条路径没有约束力——这与玩家「修复没生效」的判定在机制上完全自洽。

## 十、新头号假设 H-3：越界 6 格由 `ExtendTrainReservation()` 的长预留延伸铺出

### 10.1 支撑证据链

1. `FSCP-CPL veh=15 ... end=1419,1873`（`:1684`）已证明 **veh=15 自己的预留链末端就在 `1419,1873`**——预留确实存在、确实属于它、确实以该格为终点。
2. 但 P-2 证明它不是 `rp()` 写的 ⇒ 只能是同一辆车的**另一条预留写入路径**把它的预留链推到了那里。
3. 全树里对"已存在的预留做延伸/续订"的入口只有 `ExtendTrainReservation()`（`src\train_cmd.cpp` 约 `:17431`）。
4. **决定性旁证**：第 471 轮给该函数加 `EXT-ENTRY / EXT-WALK / EXT-RES` 三探针时，取数的原始目的逐字就是——「判定现场 `1455,1874` 岔口（`tracks=0x11=X|LEFT`、`reserved=0x1` 是自己的 `X`）为何把预留拐进北侧平行线 **`1421/1420/1419,1873`** 且不撤销」。**与本轮越界 6 格是同一串格、同一个岔口。**
5. S5 的落点全部在 `yapf_rail.cpp` / `yapf_destrail.hpp` 的目标判定侧，`ExtendTrainReservation` 一行未动 ⇒ 天然管不到它。

### 10.2 判别（纯读日志，无需改码，探针已在树里）

按此顺序看 `build\R3R_debug.log`：

1. `EXT-ENTRY veh=15 origin=1455,1874 ...` 是否存在。缺失 ⇒ 该次延伸不是从岔口起算，H-3 需改锚。
2. `EXT-WALK veh=15 ... old=1421,1874 ... new=1421,1873 ...` 是否出现 `y=1874 → y=1873` 的**换轨步**。出现即 H-3 成立：延伸阶段在岔口按 `choice` 选到了北侧平行线。
3. `EXT-RES` 是否逐格对上 `1421,1873 / 1420,1873 / 1419,1873`（`ok=1`）。对上即坐实"这三格是延伸订的"。
4. 反向对照：`EXT-WALK` 若只走到 `1421,1874` 就停，则越界格仍无主，需转查 `long-reserve` 递归（`train_cmd.cpp` 约 `:18509`）。

### 10.3 若 H-3 成立，KI-684 的正确落点

修法应从「把会合点钉成唯一目标」（S5，已验证无效）改为**延伸阶段的岔口判定**，二选一：

- **E-1**：`ExtendTrainReservation` 的行走在岔口遇到「该轨已由自己预留（`reserved` 含自身 `X`）」时，不允许换到未被自己预留的平行轨（现有守卫把"自己的位"当"可走"，于是把链子带出站台轴）。
- **E-2**：在延伸的 `target_seen`/到达判定里加站台轴约束：一旦已进入目标站台轴，禁止沿岔口离开该轴（与 `R3RComputeMeetTile` 的轴语义同源，形成前后一致的"不许离开站台轴"不变式）。

> 口径提醒：E-1/E-2 都属行为改动，须先拿到 10.2 的三条读数再定，不得凭假设改码。

## 十一、取数结果：`ExtendTrainReservation` 确在跑，但轨迹**止于 1440,1874**（H-3 暂不成立）

### 11.1 取证方式与一个必须先记住的工具坑

`search_content` **遵循 `.gitignore`**，而 `build/*.log` 被忽略 ⇒ 用它搜 `build\R3R_debug.log` 一律返回 0 命中。这个 0 **不代表日志没有该内容**（本轮一开始就被它误导过一次）。正确做法：`cmd /c findstr /n /c:"<关键字>" build\R3R_debug.log > build\_xxx.txt`，再 `read_file` 读那个 txt。

另：树里这批探针的实际名字是 **`RESV-EXT-PRE`**，不是第 471 轮备忘里记的 `EXT-ENTRY / EXT-WALK / EXT-RES`（后续轮次改过命名）。查 `EXT-ENTRY` 等旧名一律 0 行，**不能据此判定"延伸没跑"**。

### 11.2 `RESV-EXT-PRE` 全部 34 行（`build\R3R_debug.log`，全为 veh=15）

```1403:1403:build\R3R_debug.log
RESV-EXT-PRE veh=15 tile=1473,1884 enterdir=0 tracks=0x1 reserved=0x0 doRes=1 real=1 spd=0
```
（唯一 `doRes=1`，即本次真正订下的第一格，`spd=0`）

逐格轨迹（`enterdir` / `tracks` / `reserved` / `doRes` / `spd`）：

| 日志行 | tile | 说明 |
|---|---|---|
| 1662 | 1473,1884 | `0x1/0x1 doRes=0 spd=39` 起步 |
| 1691→1753 | 1472,1884 → 1468,1880 | 西南向逐格 |
| 1771→1888 | 1465,1880 → **1459,1875** | `1459,1875 tracks=0x11 reserved=0x1`（岔口，只订了自身 `X`） |
| 1918→1969 | 1458,1875 → **1455,1874** | `1455,1874 tracks=0x11 reserved=0x1`（同一岔口形态） |
| 1994→2022 | 1454,1874 → **1450,1874** | `1450,1874 tracks=0x11 reserved=0x1` |
| 2048→2111 | 1449,1874 / **1448,1874** | `1449,1874 tracks=0x9 reserved=0x1` |
| 2155→2191 | **1441,1874** / **1440,1874** | 序列**最后一行** |

### 11.3 判定

1. **`ExtendTrainReservation()` 本会话确为 veh=15 跑过**，且一路在 **y=1874** 上由东向西逐格推进，`reserved` 恒含自身 `X`（`tracks=0x11/0x9` 的岔口格 `reserved=0x1`）。
2. **轨迹止于 `1440,1874`**，而会合点目标正是 `1439,1874`（`1440` = 会合点的东邻格）⇒ 延伸在到达会合点前一刻停下，**属正常收尾**，不是越界。
3. **延伸轨迹完全没有出现 `1873` 这一行，也没有出现在 `1443` 以西**。而越界 6 格是 `1423,1874 / 1422,1874 / 1421,1874 / 1421,1873 / 1420,1873 / 1419,1873`——**全部在站台（`1424..1438`）西侧之外**。
4. ⇒ **H-3 作为"越界 6 格的作者"证据不足，暂不成立**。延伸只覆盖站台**东侧**那一段（`1459→1440`），越界 6 格在站台**西侧**，空间上不属于同一条推进。

### 11.4 三条新疑点（已按规则登记，勿凭记忆重跑）

- **R-1（缺口）**：序列在 `1453..1451`、`1447..1442` 有缺口，其余格连续。是 `R3RDbgEdge` 节流、还是"该格已预订故不打"、还是站台跳段所致？**未定**，须读 `RESV-EXT-PRE` 的打印条件。
- **R-2（止于 1440）**：是探针在"到达会合点即停"时不打，还是延伸本身有格数/tick 上限？**未定**，须读代码中该函数的退出条件。
- **R-3（ownership 可被顺走）**：`FollowTrainReservation()` 在站台格会扫整个平台（`pbs.cpp` 的 `CheckTrainsOnTrack`，见 KI-162 第 90 轮），且沿"连续预留位"行走不校验归属 ⇒ `FSCP-CPL veh=15 ... end=1419,1873` **只证明"从 veh=15 沿预留能一路走到 1419,1873"，不等于"这 6 格是 veh=15 订的"**。这一点直接影响 KI-686 的"订者=veh=15"结论，需重新表述。

### 11.5 下一步（按性价比排序）

1. 读 `RESV-EXT-PRE` 的定义与 `ExtendTrainReservation()` 的退出条件（同时回答 R-1/R-2）。
2. 用 `findstr` 把日志里 `1423,1874`、`1422,1874`、`1421,1873`、`1420,1873`、`1419,1873` 五格**逐格全文**抽出来（含 `SET-RAIL`/`CLEAR-RAIL`/`RP cur=`/`RESV-*`/`FSCP-*`），看第一次提到每格的是哪一行——**谁先提到，谁就是候选订者**。
3. 若五格在日志中**从未被任何订位探针提及**，则不能排除"预留位由引擎侧 `TryReserveRailTrack` 在未被探针覆盖的路径写入"，需回到 `pbs.cpp` 侧的 `TryReserveRailTrack` 全调用点清单核对。

## 十二、五格逐格取证（`build\_t5.txt`，取自 `build\R3R_debug.log`）——**订者 = veh=75，普通 YAPF 行驶预留**

取证命令：`cmd /c findstr /n /c:"1423,1874" /c:"1422,1874" /c:"1421,1874" /c:"1421,1873" /c:"1420,1873" /c:"1419,1873" build\R3R_debug.log > build\_t5.txt`，共 37 行。

### 12.1 时间序（按日志行号）

**阶段 A —— `veh=75` 走到并订下（`:1241`–`:1277`）**

```1241:1241:build\R3R_debug.log
TPR-WALK veh=75 mf=1424,1874 origin=1424,1874 td=0 okay=1 exit=0 new=1423,1874 reach=1 spd=3 one=1 look=0
```
```1243:1247:build\R3R_debug.log
RESV-HAVERES veh=75 tile=1423,1874 enterdir=0 res=0x0 tracks=0x1 spd=3 real=2 order=6 ...
RESV-SIGPASS veh=75 tile=1423,1874 track=0 dir=0 doResIn=1 spd=3 reserved=0 ownRes=0 coType=6 real=2 db=0
CRT veh=75 order=6 dir=1 origin=1419,1873 td=0 found=1
RPFAIL veh=75 tick=40817 origin=1419,1873 tile=1402,1870 td=0 unsafe=0 nTry=5 nOK=4
CPLRES veh=75 tick=40817 origin=1419,1873 destTile=1402,1870 destTd=0 node=1402,1870 root=0 unsafe=0 nTry=20 nOK=19 empty=0 vTile=1424,1874 vDest=1402,1870 gap=5 oSrc=other
```
- `:1243` 时 `1423,1874` 的 `res=0x0`（**尚未被订**）。
- `:1245` `CRT veh=75 order=6 dir=1 origin=1419,1873 td=0 found=1`：veh=75 当前**预留起点**就是 `1419,1873`，且 `found=1`（路径已存在）。
- `:1247` `CPLRES ... destTile=1402,1870 ... vTile=1424,1874 vDest=1402,1870`：veh=75 的**目的地是 `1402,1870`**，与挂接无关。

**阶段 B —— 逐格确认归属（`:1270`–`:1277`）**

```1270:1277:build\R3R_debug.log
RESV-HAVERES veh=75 tile=1423,1874 enterdir=0 res=0x1 tracks=0x1 spd=27 real=2 order=6 ...
RESV-HAVERES veh=75 tile=1422,1874 enterdir=0 res=0x1 tracks=0x1 spd=69 real=2 order=6 ...
RESV-HAVERES veh=75 tile=1421,1874 enterdir=0 res=0x10 tracks=0x10 spd=80 real=2 order=6 ...
RESV-HAVERES veh=75 tile=1421,1873 enterdir=3 res=0x20 tracks=0x20 spd=80 real=2 order=6 ...
RESV-HAVERES veh=75 tile=1420,1873 enterdir=0 res=0x1 tracks=0x1 spd=80 real=2 order=6 ...
RESV-HAVERES veh=75 tile=1419,1873 enterdir=0 res=0x1 tracks=0x1 spd=80 real=2 order=6 ...
```
- 六格 `tracks` 与 `res` **逐格完全相同**（`0x1/0x10/0x20`），即这六格上的预留位**全部是 veh=75 自己要走的那一轨**，`foreign=0`、`split=0`。
- `veh=75` 在这里是 `order=6`（`OT_GOTO_WAYPOINT`）、`real=2`、`spd` 由 3 递增到 80（**正常加速行驶**）。

**阶段 C —— 写位铁证（`:1825`）**

```1825:1825:build\R3R_debug.log
RESV-WATCH CHG-RAIL tile=1421,1873 track=4 phase=yapf-resv actor=75 head=75
```
- `phase=yapf-resv`、`actor=75`、`head=75`：这格预留是**普通 YAPF 寻路预留**写下的，写位者就是 veh=75 自己。这是 SetRailReservation 的写探针，属最高等级证据。

**阶段 D —— 被 R3R 的 ghost 清扫一次性抹掉（`:2466`–`:2474`）**

```2466:2474:build\R3R_debug.log
UNRES-RAIL tile=1419,1873 track=0 phase=release-ghost actor=17
RESV-WATCH CLEAR-RAIL tile=1419,1873 track=0 phase=release-ghost actor=17 head=17
RESV-GHOST couple veh=17 tile=1419,1873 track=0
RESV-GHOST couple veh=17 tile=1420,1873 track=0
RESV-WATCH CHG-RAIL tile=1421,1873 track=4 phase=release-ghost actor=17 head=17
RESV-GHOST couple veh=17 tile=1421,1873 track=5
RESV-GHOST couple veh=17 tile=1421,1874 track=4
RESV-GHOST couple veh=17 tile=1422,1874 track=0
RESV-GHOST couple veh=17 tile=1423,1874 track=0
```

### 12.2 判定

1. **订者 = `veh=75`，路径 = 普通 YAPF 行驶预留（`phase=yapf-resv`），与挂接寻路（`rp()`）、`ExtendTrainReservation()` 均无关。**
   - 它不是 `rp()` 铺的（P-2 的 `RP cur=` 无该格 —— 与玩家取证一致）；
   - 它不是延伸铺的（§11 的 `RESV-EXT-PRE` 轨迹属 veh=15、止于 `1440,1874`）；
   - 它也不是存量（P-3 —— 确为本会话 `:1243 res=0x0 → :1270 res=0x1` 之间新订）。
2. ⇒ **KI-684 的 S5 方向（改挂接寻路的目标格/会合点）从根上与这条预留无关**，`未生效`不是实现瑕疵，而是**修错了对象**。这也解释了为什么 `FSCP-*` 全部读数是 veh=15：挂接寻路只是在**读**别人（veh=75）的预留。
3. **`FSCP-RES owner=15` 是回溯误报**，与 §11.4 的 **R-3** 预测一致：`GetTrainForReservation()` 沿连续预留位回溯、在站台/双槽格上会"顺"到更早的链头，故把 veh=75 的位报成 15 的。**订者只能信 `RESV-WATCH CHG-RAIL actor=`**。
4. **veh=75 的这条路径本身是合法的**：它 `order=6`（`OT_GOTO_WAYPOINT`）去 `1402,1870`，物理位置就在 `1424,1874` 一带往西，走 `1423/1422/1421,1874` 再经 `1421,1873` 的双槽 L 形连接换到北侧平行线，是正常的线路走向。

### 12.3 新疑点 R-4（严重，须尽快取证）

`:2466`–`:2474` 的 `RESV-GHOST couple veh=17` 把这六格**当作 ghost 全部释放**，其中 `:2471` 释放的 `1421,1873` 甚至标 `track=5`，而 `:1825` 明确记录该格预留是 **`track=4` 且 actor=75**：

- 若 `:2466` 时刻 veh=75 仍持有这些位 ⇒ **R3R 的 ghost 清扫误清了另一列车的合法预留**，属新缺陷（会让 veh=75 的 PBS 路径中途消失，可能引发碰撞/卡死），严重度高于原「越界预留」现象。
- 若 veh=75 已驶离并自行释放 ⇒ 这批 `RESV-GHOST` 是正常的"提交点清扫残留"，只是清扫点恰好落在同一串格上，属巧合（需看 `:1826`–`:2465` 之间 veh=75 的 `CRT`/`RP` 位置推进）。

判别所需：`findstr /n /c:"veh=75"` 抽出 `:1826`–`:2470` 区间内 veh=75 的全部行走痕迹，确认它是"还在这六格附近"还是"早已西行远去"。

### 12.4 口径修正（重要）

- **KI-686 的现象描述需改**：那六格的预留**不是"超出站台的多余预留"**，而是**另一列正常列车（veh=75）自己的行驶路径预留**恰好穿过站台西端并换到 `y=1873` 平行线。玩家看到的"黑色预览在站台外"是**真实存在的合法预留**，观感上像越界，实际是路径走向。
- 因此"预定范围内不该有预留"这一前提本身要重审：需要确认站台西端外那段（`1424,1874` 以西 → `1419,1873`）是否**在 veh=75 的合法行驶线上**。若是（按 `tracks=0x1/0x10/0x20` 连续且 `new=1423,1874 reach=1` 看，是），则 **KI-686 可判定为"非缺陷/观感误诊"**，真正的待查项只剩 R-4（ghost 误清）。

## 十三、玩家新观察（2026-11-25）：「这六格被预留了两次，第一次是 veh=75 的合法预留，第二次不正常」——**成立，且 §十二 的"单次写入"表述必须更正**

### 13.1 取证（`build\_w2.txt`，`findstr /n /c:"RESV-WATCH" /c:"RESV-HAVERES veh=75" /c:"RESV-GHOST"`，共 151 行）

把 `veh=75` 的**全部行程探针**按行号串起来，它与这六格相关的读数分成**四个阶段**：

**阶段 A（`:676`–`:978`）veh=75 第一趟：`real=1 order=1`（进站），六格完全未被订**

```978:978:build\R3R_debug.log
RESV-HAVERES veh=75 tile=1424,1874 enterdir=0 res=0x1 tracks=0x1 spd=50 real=1 order=1
```
- 该趟自 `1504,1875` 一路西行，**止于站台西端 `1424,1874`**（`:978`），之后再无 `1423,1874` 的 HAWERES。
- 交叉印证：`:1243` 的 `1423,1874 res=0x0` ⇒ 第一趟**从未**订过这六格。

**阶段 B（`:1243`–`:1277`）veh=75 第二趟：`real=2 order=6`（出站西行），六格被**第一次**订下**

```1243:1277:build\R3R_debug.log
RESV-HAVERES veh=75 tile=1423,1874 enterdir=0 res=0x0 tracks=0x1 spd=3 real=2 order=6
RESV-HAVERES veh=75 tile=1423,1874 enterdir=0 res=0x1 tracks=0x1 spd=27 real=2 order=6
RESV-HAVERES veh=75 tile=1422,1874 enterdir=0 res=0x1 tracks=0x1 spd=69 real=2 order=6
RESV-HAVERES veh=75 tile=1421,1874 enterdir=0 res=0x10 tracks=0x10 spd=80 real=2 order=6
RESV-HAVERES veh=75 tile=1421,1873 enterdir=3 res=0x20 tracks=0x20 spd=80 real=2 order=6
RESV-HAVERES veh=75 tile=1420,1873 enterdir=0 res=0x1 tracks=0x1 spd=80 real=2 order=6
RESV-HAVERES veh=75 tile=1419,1873 enterdir=0 res=0x1 tracks=0x1 spd=80 real=2 order=6
```
- `1421,1873` 这次订的是 **`tracks=0x20` 即 `track=5`（TRACK_RIGHT）**，与 `enterdir=3` 一致（车确实从 1874 侧斜向进 1873 侧）。
- 该趟继续西行 `:1279 1418,1873` → `:1345 1402,1870`（目的地），全程 `spd` 3→80。

**阶段 C（`:1823`–`:1825`）veh=75 第三趟：`real=3 order=6`（**返程东行**，起点 `1403,1870`），六格被**第二次**写位**

```1823:1825:build\R3R_debug.log
RESV-HAVERES veh=75 tile=1403,1870 enterdir=2 res=0x0 tracks=0x1 spd=0 real=3 order=6
RESV-WATCH CHG-RAIL tile=1421,1873 track=4 phase=yapf-resv actor=75 head=75
```
- **第二次写的 `track=4`（TRACK_LEFT）与第一次的 `track=5`（TRACK_RIGHT）不是同一条轨**——这正是玩家所说"第二次不正常"的行级铁证。
- 时间序上它晚于阶段 B（`:1825` > `:1277`），且此刻 veh=75 已回到 `1403,1870` 开始**向东**的新一趟（`:1850 1403,1870 → :2215 1415,1871 → :2718 1416,1872`）。

**阶段 D（`:2467`–`:2475`）veh=17 的挂接提交点把这六格当 ghost 一次清光**

```2467:2475:build\R3R_debug.log
RESV-WATCH CLEAR-RAIL tile=1419,1873 track=0 phase=release-ghost actor=17 head=17
RESV-GHOST couple veh=17 tile=1419,1873 track=0
RESV-GHOST couple veh=17 tile=1420,1873 track=0
RESV-WATCH CHG-RAIL tile=1421,1873 track=4 phase=release-ghost actor=17 head=17
RESV-GHOST couple veh=17 tile=1421,1873 track=5
RESV-GHOST couple veh=17 tile=1421,1874 track=4
RESV-GHOST couple veh=17 tile=1422,1874 track=0
RESV-GHOST couple veh=17 tile=1423,1874 track=0
RESV-GHOST-SUMMARY couple veh=17 cand=6 freed=6
```
- 注意 `:2470` 释放的是 **`track=4`**（= 阶段 C 那笔），`:2471` 读到并清掉的却是 **`track=5`**（= 阶段 B 那笔）⇒ **阶段 B 与阶段 C 的两次写入在清扫时同时存在**，故 `cand=6 freed=6` 六格全被清。

### 13.2 判定与自我更正

1. **玩家观察成立**：这六格确被写了两次——
   - **第一次（合法）**：阶段 B（`real=2` 出站西行），`track=5`，车确实走这条轨；
   - **第二次（异常）**：阶段 C（`real=3` 返程），`track=4`，写位者仍是 `actor=75`、`phase=yapf-resv`。
2. **§十二 的表述必须更正**：我上一轮把 `:1825` 写成"写位铁证、订者=veh=75、单次写入"，据此还关掉了 KI-684。**这是错的**——`:1825` 只是**第二次**写入的那一笔，不是第一次的凭证；第一次（阶段 B）当时根本没有 `CHG-RAIL` 行被抽出来（因为我的 `findstr` 只覆盖六格坐标，`track=5` 那笔的写位行可能落在别的格上或未被打印）。⇒ **KI-684 不能以"单次写入"为由关闭，须重新打开为"两次写入中第二次异常"。**
3. **第二次异常的形态与 KI-346 完全同族**：`1421,1873` 是双槽格（`tracks=0x20/0x10`、`LEFT|RIGHT`），阶段 C 在车**返程东行**时把 `track=4`（LEFT）也订上，但车在 `:2215` 处走的是 `1415,1871 tracks=0x20`（RIGHT）——若返程路径不经过该格的 LEFT，则这笔就是**车根本不走的轨 = 无主位**，与 KI-346 `rp()` 的 `ReverseTrackdir` 退化 bug 是同一类（订了 `entry_track` 之外的轨）。
4. **阶段 D 的清扫性质待判**：veh=75 在 `:2717`–`:2718` 仍在向东走（活着），而 veh=17 在 `:2467` 把这六格全清了 ⇒ 若当时这些位仍属 veh=75 的返程路径，则 R3R 的 ghost 清扫**误清了另一列车的合法预留**（§12.3 的 R-4 维持成立，且现在有了"六格全清"的实证 `freed=6`）。

### 13.3 下一步（判"第二次"到底是谁、写了什么）

1. **必须补抽**：`findstr /n /c:"1421,1873" /c:"1421,1874"` 单格全文，确认阶段 B 的 `track=5` 有没有独立的 `CHG-RAIL/SET-RAIL` 行、以及 `:1825` 前后是否还有第二条写位行（我的上一份 `_t5.txt` 只 37 行，可能被 `findstr` 的 `/c:` 多模式合并漏掉）。
2. **判"第二次"是否无主位**：看 veh=75 返程（`real=3`）在 `1415,1871` 之后的走向 —— 若它继续走 `y=1871/1872` 而不回到 `1873` 的 LEFT，则 `track=4` 属"订了不走的轨"，可确诊为 KI-346 同族。
3. **判阶段 D 是否误清**：核对 `:2467` 时刻 veh=75 的当前位置与该六格的距离；若它仍在西侧远处、且路径要经过这些格，则 R-4 误清成立。

## 十四、§13.3 三条取数结果：R-4 误清**成立**，根因落在 `R3RCollectReservedPathTiles` 的"位不区分主人"

### 14.1 补抽结果（`build\_s1421.txt`，`findstr /n /c:"1421,1873" /c:"1421,1874"`）

全文只有 6 行，无被漏掉的写位行：

```1273:1273:build\R3R_debug.log
RESV-HAVERES veh=75 tile=1421,1874 enterdir=0 res=0x10 tracks=0x10 spd=80 real=2 order=6 ...
```
```1274:1274:build\R3R_debug.log
RESV-HAVERES veh=75 tile=1421,1873 enterdir=3 res=0x20 tracks=0x20 spd=80 real=2 order=6 ...
```
```1825:1825:build\R3R_debug.log
RESV-WATCH CHG-RAIL tile=1421,1873 track=4 phase=yapf-resv actor=75 head=75
```
```2470:2474:build\R3R_debug.log
RESV-WATCH CHG-RAIL tile=1421,1873 track=4 phase=release-ghost actor=17 head=17
RESV-GHOST couple veh=17 tile=1421,1873 track=5
RESV-GHOST couple veh=17 tile=1421,1874 track=4
```

结论（更正 §13.2 第 2 条的口径）：

1. `1421,1873` 的 **`track=5`（阶段 B）没有任何 `CHG-RAIL/SET-RAIL` 行**，`1421,1874` 的 `track=4`（阶段 B）同样没有 ⇒ **写位探针覆盖不全**：`RESV-HAVERES` 能读到"位已存在"，但那次写入没走带探针的 setter。故"无写位行"既不证明"没订"，也不证明"单次写入"。
2. 本会话在 `1421,1873` 上**确实有两笔不同 track 的位同时存在**（`:2471` 清 `track=5`，`:2470` 清 `track=4`），这一点由 `freed=6` 的清扫结果反证，不受写位探针缺失影响。

### 14.2 阶段 C 的 `track=4` 是否"无主位"——**未定（新疑点 R-5）**

- veh=75 返程（`real=3`）的 HAWERES 轨迹：`:1850 1403,1870` → `:2190 1414,1870` → `:2207 1414,1871` → `:2215 1415,1871` → `:2717 1415,1872` → `:2718 1416,1872 tracks=0x1`（X 轨）。**日志到此结束**（尾部 30 行即 `:2718` 之后无 veh=75），无法观测它是否回到 `y=1873` 并走过该格 LEFT。
- `:1825` 的 `phase=yapf-resv` 是**普通行驶预留**（不是挂接 `rp()`），而 KI-346 的"订了不走的轨"缺陷已在第 240 轮修掉（改用 `TrackdirToTrack(nd)`）；因此**不能直接套用 KI-346 同族结论**。若返程路径真是 `1421,1874 LEFT → 1421,1873 LEFT`（LEFT 接 LEFT 是合法延续），这笔就是合法预留。
- 待补数据：让 veh=75 驶过 `1421,1873` 之后再收一次日志，看该格是否有 `RESV-HAVERES veh=75 ... res=0x10`（左轨实际通行）或后续 `CRT/RP` 是否绕开它。**在拿到这条之前，R-5 记为未定，不得据"KI-346 同族"改码。**

### 14.3 阶段 D 误清——**R-4 成立（高）**

- `:2466` 清扫发生时，veh=75 正从 `1415,1871` 驶向 `1416,1872`（`:2215`/`:2717`），距六格 3~8 格；`:2718` 证明它 `spd=80` **活着**并在向东行驶。
- `:1825` 证明 `1421,1873 track=4` 是 veh=75 自己 YAPF 路径里的位（`actor=75 head=75`）。
- 故 `:2466-2475` 那 6 格里，**至少 `1421,1873 track=4` 是活车的合法在途预留被清掉**。这与"清残留幽灵"的目标相反，属行为错误，严重度高于原"越界预留"观感问题。
- `RESV-GHOST-SUMMARY couple veh=17 cand=6 freed=6`：一次全清，无 skip。

### 14.4 根因（已定位到函数，不再只是猜测）

`src\pbs.cpp:2726 R3RCollectReservedPathTiles()` 为链上**每一节车**沿 `FollowReservationEnumerate` 双向走已订位，遇"格上有车"停（`:2759`），但**预留位本身不记录主人**。于是：

1. veh=75 去程在 `1421,1874`/`1421,1873` 留下未释放的 `track=4/5`（阶段 B 残留，本身就属待查缺陷）；
2. 合并链（head=17，含等待车底 18..74）停在 `1424..1438,1874`，其车底仍铺着出站预留（KI-207 的 parked-waiter 现象）；
3. 两边预留在地图上**连通**，`R3RCollectReservedPathTiles` 的 walk 沿 `1422,1874 → 1421,1874(75 的 LEFT) → 1421,1873(75 的 LEFT+RIGHT)` 一路走进去，把 75 的位收成"本链 own_path"；沿途没有车（75 在 `1416,1872`，比 walk 覆盖的 `1419` 更西），故 `:2759` 的 fence 看不见；
4. 第二道闸门 `GetR3RReservationOwnerNearby()`（`pbs.cpp:2422`，内部 `R3RFollowReservationToNearbyTrain`）对 `1421,1873 track=4` 双向都走不到 75（75 的其余位在 `1416,1872` 的 X 轨上，与该 LEFT 之间没有连续位），答 `nullptr`；
5. 清扫把 `nullptr` 当"孤儿"释放（`:13580` 一带的闸门 2）。

⇒ **这是第 303 轮删除预留账本后必然出现的结构性盲区**：归属只能靠地图几何回溯，而"位与位之间断链"和"真的无主"无法区分。KI-211c 的 start-tile guard 只挡住"同格两车"这一种情形，挡不住"隔着几格的连通残留"。

### 14.5 修法候选（待拍板，本轮未改码）

- **A（推荐，恢复最小账本）**：加 `(tile, track) → VehicleID` 的轻量写入账本（只在 setter/reserve 写入登记、释放注销、读档清空；不参与任何判定，仅作归属证据）。清扫闸门改为：账本答"别的活车" ⇒ skip；答"自己" ⇒ 清；答"无记录" ⇒ 再退几何判定（保持现状）。这样 `:1825` 那笔会答 75 ⇒ 不误清。**需先查第 303 轮删除账本的原因（内存/性能/desync），确认可再引入。**
- **B（保守，不动账本）**：`GetR3RReservationOwnerNearby()` 返回 `nullptr` 时**不再当孤儿释放**，只清"几何上能从本链连续走回链头且没有跨过任何他车残留位"的位。代价=真幽灵清不掉（退回 KI-207 原症状，需靠 `r3r_resv_purge` 手工清）。
- **C（条件最窄）**：清扫前对该位做"邻域活车检验"——若该位 3 格内存在活车，且该车 `GetVehicleTrackdir()` 指向该位，则 skip。实现简单但仍是启发式，不适合当正解。

### 14.6 待验证 / 下一步

1. （R-5）veh=75 驶过 `1421,1873` 后的 HAWERES，判定 `track=4` 是否车真走。
2. （R-6）查第 303 轮删除账本的原因（`R3R_KNOWN_ISSUES.md` 第 303 轮小节 + `pbs.cpp` 相关注释），决定 A 是否可行。
3. 阶段 B 的两笔残留（`1421,1873 track=5`、`1421,1874 track=4`）为何车离开后未释放 —— 与 KI-346/KI-207 残留释放路径有关，未查。
4. 本轮零源码改动、零构建。

## 十五、§14 作废并更正（玩家 2026-11-25 指正：写入 → 销毁 → 再写入，不是两笔并存）

### 15.1 玩家原话

> 这个位没有被两次连续写入，而是一次正常的写入（veh75），一次正常的销毁，再来一次异常的写入（veh15-17）。

### 15.2 宣布作废的条目

- §14.2「`1421,1873` 上同时存在两笔预留」——**作废**。依据（`:2470 CHG-RAIL track=4` + `:2471 RESV-GHOST track=5`）是误读：`RESV-GHOST` 是**清扫动作的输出**（`train_cmd.cpp:13668-13671`，`UnreserveRailTrack()` 成功之后才打印），`CHG-RAIL` 是 `SetRailReservationTrackBits()` 的通用变更日志（`rail_map.h:250-254`，打印的是"变化后的首轨"）。两者相加只能说明"本次清扫清掉了本链 6 笔位"，不能说明其中有 veh=75 的。
- §14.3 R-4「误清活车合法预留」——**撤回**。
- §14.4 根因「`R3RCollectReservedPathTiles` 沿连通位把 veh=75 的位收成本链 own_path」——**作废**。
- §14.5 三条修法候选（A 恢复账本 / B 保守不清 / C 邻域检验）全部**悬置**：它们治的是不存在的病。

### 15.3 新证据：这 6 格是 veh=15 自己的预留路径，终点 `1419,1873`

`:1682` 与 `:1904` 两条 `FSCP-RES`（打印点 `src\pathfinder\yapf\yapf_rail.cpp:487`）：

```
:1682 FSCP-RES tile=1447,1874 tk=0 owner=15 otile=1474,1884 odir=1 ospd=39 ostop=0 ord=16 oroi=1 oend=1419,1873 oendtd=0 othru=1 octgt=0 ocplok=0 cpl=1474,1884
:1904 FSCP-RES tile=1447,1874 tk=0 owner=15 otile=1460,1875 odir=1 ospd=160 ostop=0 ord=16 oroi=1 oend=1419,1873 oendtd=0 othru=1 octgt=0 ocplok=0 cpl=1460,1875
```

`oend` 的定义是 `FollowTrainReservation(owner->First(), nullptr)` 的终点（`yapf_rail.cpp:486` 与 `:491` 的 `oend.tile`），`owner=15` 就是询路车自己 ⇒ **veh=15 自己的预留终点就是 `1419,1873`**。

而 `:2466-2475` 清扫掉的 6 格是：

```
1419,1873 → 1420,1873 → 1421,1873 → 1421,1874 → 1422,1874 → 1423,1874
```

正好是从该终点 `1419,1873` 沿 y=1873 往东、再拐上 y=1874 回到车底（车底占 `1424..1438,1874`）的**一条连续路径**。即：这 6 笔从头到尾都是 veh=15 自己铺的。

旁证——同一张 `OT_GOTO_COUPLE(16)` 订单，两个寻路器给出相差约 18 格的终点：

| 来源 | 读数 | 语义 |
|---|---|---|
| 普通行驶寻路（`FollowTrainReservation`） | `oend=1419,1873` | 车底**另一侧**、站台西出口 |
| 挂接寻路 `CCouplePf` | `:1677 CPL-HEAD-STATIC veh=15 ... head=1439,1874`、`:1678 CPL-BEST veh=15 tile=1439,1874` | 车底头 |
| 挂接寻路报的目的地 | `:1686 CPL-BLOCKED-WAIT veh=15 tile=1474,1884 dest=1437,1874` | 车底头 |

### 15.4 修正后的结论

1. **玩家的时间轴成立**：veh=75 在 y=1873 线上正常订位并正常释放（`:1274` 去程 RIGHT、`:1825` 返程 LEFT）；随后 veh=15 的 GOTO_COUPLE 行驶寻路把预留铺进同一段线路，一路铺到 `1419,1873`。
2. `:2466-2475` 的 `release-ghost` 清扫（`actor=17`，耦合提交点，紧接 `:2463 FOLDCHK COUPLE-FLIP-BOTH` 与 `:2464 CPL-PAIRUNLOCK why=coupled`）清的是 **veh=15 自己的 6 笔**，属**正确行为**（耦合已成功，等待车底的出站路线作废）。
3. 真正的缺陷是"越界预留"的**成因**：GOTO_COUPLE 机车的普通行驶寻路把终点定在**车底另一侧**（`1419,1873`），于是路线穿过整条车底、预留铺到车底背后。玩家看到的"同一个位被写了两次"，是 veh=75 与 veh=15 两条不同车的路线在**同一条 y=1873 线路**上先后重叠，不是同一笔位被写两次。
4. §13 记的"越界/越过站台"**现象定性正确、归因错误**。

### 15.5 待查（新主线，取代 §14.6）

1. **谁把 veh=15 的订单终点算成 `1419,1873`**：查 `PfDetectDestination`（`src\pathfinder\yapf\yapf_destrail.hpp`）的 `OT_GOTO_COUPLE` 分支，以及 `GetCoupleTarget` / 目标车底位置的使用。`1419,1873` 与车底西端 `1424,1874` 差 5 格、且换到北线 y=1873，疑似"站台另一端出口 / 站台长度外推"。
2. **两个寻路器终点不一致**：普通寻路 `1419,1873` vs 挂接寻路 `1437,1874`；正常应统一到车底头。
## 十六、定位完成（2026-11-25）：`1419,1873` 不是任何终点，是**探针伪读数**；六格的实体主人是 veh=75

### 16.1 硬结论一：`oend` 只是日志字段，不参与判定，更不是"订单终点"

`src\pathfinder\yapf\yapf_rail.cpp:485-494`（`FSCP-RES` 打印块）：

- `Train *oh = const_cast<Train *>(owner->First());`
- `const PBSTileInfo oend = FollowTrainReservation(oh, nullptr);`
- 随后 `fprintf(dbg, "FSCP-RES ... oend=%d,%d oendtd=%d ...")` 把 `oend.tile` 打出去。

`oend` 求出后**只被这一行 fprintf 使用**，没有任何分支读它 ⇒ 它既不影响寻路、也不产生预订。所以"谁把 veh=15 的 OT_GOTO_COUPLE 终点算成 `1419,1873`"这个提问本身不成立：**没有任何代码把 `1419,1873` 当作终点**。

### 16.2 硬结论二：`oend` 被拖到 `1419,1873` 的原因 —— 走链位是"全局的、不带主人、且不在列车处停下"

同一文件 `:496-518` 的注释是权威口径：

- `FollowReservationEnumerate` / `TrainReservationPassesThroughTile` 沿的是**连续 reserved bits**，"the reserved bits are **global and carry no owner**"；
- 停在该串上的车"**through someone else's** reservation"也能到达该格，语义是"本链的预留**够得到**这格"，"**NOT** 'this chain holds the reservation' -- do not read it as ownership"。

代入现场（从 veh=15 车头往西）：

| 段 | 格 | 位的真正主人 |
|---|---|---|
| 行驶路径 | `1474,1884` … `1447,1874` … `1439,1874` | veh=15 自己（YAPF rp） |
| 穿过整条等待车底 | `1438,1874` … `1424,1874` | 等待车底 `ReserveTrackUnderConsist()` 的平台位 |
| 继续被"够到" | `1423,1874` `1422,1874` `1421,1874` `1421,1873` `1420,1873` `1419,1873` | **veh=75**（见 16.3） |
| 停下 | `1418,1873` 无位 | — |

⇒ `oend=1419,1873` 是"走链终点"，与 veh=15 的意图/订单无关。

### 16.3 硬结论三：六格的真实主人在 veh=75

`build\R3R_debug.log` 锚点：

```
:1241 TPR-WALK veh=75 mf=1424,1874 origin=1424,1874      ← 75 从车底西端格起步
:1243 tile=1423,1874 res=0x0 tracks=0x1 (spd=3)          ← 75 进入
:1270 1423,1874 res=0x1 ; :1272 1422,1874 res=0x1
:1273 1421,1874 res=0x10 ; :1274 1421,1873 res=0x20
:1275 1420,1873 res=0x1 ; :1277 RESV-HAVERES veh=75 tile=1419,1873 res=0x1 (spd=80)
:1247 destTile=1402,1870                                 ← 75 一路西行
:1830 CRT veh=75 order=6 dir=1 origin=1424,1873 destTile=1452,1869  ← 路点掉头，改走 y=1873 东行
:1825 CHG-RAIL tile=1421,1873 track=4 phase=yapf-resv actor=75      ← 东行订 track=4（LEFT）
```

⇒ y=1874 的 `1421,1874`(track=0x10) / `1422,1874` / `1423,1874`(track X) **不在 75 东行路径上**，是它西行时留下、从未释放的**陈旧残留位**；y=1873 的 `1419/1420/1421,1873` 属 75 的 y=1873 走廊。

### 16.4 修正 §15.3/§15.4（必须更正）

§15.3 的"这 6 笔从头到尾都是 veh=15 自己铺的"**不成立**，作废。正确的是：

- `oend` 只能证明"够得到"，不能证明"持有"（16.2 的代码注释即此意）；
- 六格的实际持有者是 **veh=75**，其中 3 格（y=1874）是陈旧残留；
- veh=15 的 `OT_GOTO_COUPLE` 意图终点在探针层面是 `1439,1874`（`:1677 CPL-HEAD-STATIC head=1439,1874` / `:1678 CPL-BEST tile=1439,1874` / `:1686 CPL-BLOCKED-WAIT dest=1437,1874`），与 `1419,1873` 相差 18 格纯属走链假象。

### 16.5 由此确定的两条真缺陷（取代 §13/§14/§15 的归因）

1. **陈旧残留已成立**：veh=75 西行经过 `1421/1422/1423,1874` 后未释放（全日志无对应 `CLEAR-RAIL`，且它掉头后走的 y=1873 根本不含这三格）。这三格正是玩家看到"铺到车底背后"的实体。
2. **清扫不区分主人（危害待确认）**：`:2466-2475` 的 `R3RCollectReservedPathTiles` 以"格上有车"为 fence（`GetFirstVehicleOnTile(...) != nullptr` 即停），而 veh=75 已驶离 ⇒ 它把 75 的 6 格一并收成 own_path 释放。若其中 `1419/1420/1421,1873` 当时仍是 75 的**活**路径位，就是**误清别的车** —— 需在玩家侧观察 veh=75 在耦合后是否失速/重订。

### 16.6 `oend` 类探针的读法纪律（防再误读）

`oend` / `othru` 一律只能读成"该车的预留**够得到**这格"，**不得**读成"该车订了这格"。要判持有，只看 `owner=`（`GetTrainForReservation`，`:470`）与 `FSCP-VEH`（格上实际有车）。

### 16.7 本轮状态

- 零源码改动、零构建。
- 待确认：U-1 veh=75 在耦合后是否失速（判 16.5 第 2 条是否真误清）；U-2 那三格陈旧位的释放时机（是否要加"列车离开后主动收缩/释放身后预留"）。

## 十七、质疑响应（玩家 2026-11-25："1419,1873 上从没出现过 SET-RAIL"）

### 17.1 玩家质疑

> 1419,1873 上从没出现过 SET-RAIL意味着什么，我怀疑你的结论

**质疑成立的部分**：§16.3 用 `:1277 RESV-HAVERES ... res=0x1` 去指认"这格的实体主人是 veh=75"，这一步是错的 —— `res=` 只读地图位，不带主人（§16.2 我自己刚引用过 yapf_rail.cpp:496-518 的 "reserved bits are global and carry no owner"），缺 SET-RAIL 更使得"谁写进去的"在日志里彻底无痕。

**质疑不成立的部分**：缺 SET-RAIL **不能**反证"这格从未被预留"，也**不能**反证"不是 75 预留的"。原因是探针有预算闸门（17.2）。

### 17.2 探针语义：不是每笔预留都会打 SET-RAIL

`src\rail_map.h:250-254`（唯一写入口，6 个 setter 覆盖全部预留位）：

```
const TrackBits r3r_old = GetRailReservationTrackBits(t);
if (r3r_old != b) {
    R3RResvWatchLog(r3r_old == TRACK_BIT_NONE ? "SET-RAIL"
                  : (b == TRACK_BIT_NONE ? "CLEAR-RAIL" : "CHG-RAIL"), t, ...);
}
```

⇒ `SET-RAIL` 只在 `NONE → 非 NONE` 的**第一次**成立；值已存在时改写只出 `CHG-RAIL`，清空出 `CLEAR-RAIL`。

而 `R3RResvWatchLog`（`src\pbs.cpp:249-332`）有两道预算：

| 情形 | 输出 |
|---|---|
| 在监视表里的格（`R3RIsWatchedResvTile`） | 每次变化一行 `RESV-WATCH <kind> tile=...` |
| 不在表里 + `phase=="misc"`（原生） | **整条丢弃**（`pbs.cpp:264` 提前 return） |
| 不在表里 + 具名 phase | 每个 `(kind, phase)` 对**只打第一例**（`pbs.cpp:323-325`），之后每 256 次一条 `RESV-WATCH-SUM`（`:326-329`） |

⇒ 非监视格上，一个 phase 的第一笔之后，**所有** `SET-RAIL` 都不再逐格落盘。

### 17.3 实证：全日志只有 3 条 SET-RAIL

`build\_a3.txt`（`findstr /c:SET-RAIL build\R3R_debug.log`）全文只有 3 行：

```
RESV-WATCH SET-RAIL tile=1483,1875 track=0 phase=choose-track actor=75 head=75
RESV-WATCH SET-RAIL tile=1459,1873 track=0 phase=yapf-resv  actor=75 head=75
RESV-WATCH SET-RAIL tile=1459,1875 track=0 phase=yapf-couple actor=15 head=15
```

三行恰好是三个 phase 的**首例**，格是 1483,1875 / 1459,1873 / 1459,1875 —— 说明这三格当时在监视表里，或恰为首例。整场会话预留了成千上万格，只有 3 条 `SET-RAIL`，**缺 SET-RAIL 是常态而非异常**。

### 17.4 `1419,1873` 的全部日志行（`build\_a2.txt`，共 22 行，去重后 6 类）

```
1245 CRT veh=75 order=6 dir=1 origin=1419,1873 td=0 found=1
1246 RPFAIL veh=75 tick=40817 origin=1419,1873 tile=1402,1870 td=0 unsafe=0 nTry=5 nOK=4
1247 CPLRES veh=75 tick=40817 origin=1419,1873 destTile=1402,1870 ... nTry=20 nOK=19 gap=5 oSrc=other
1277 RESV-HAVERES veh=75 tile=1419,1873 enterdir=0 res=0x1 tracks=0x1 spd=80 real=2 order=6 foreign=0
1682/1904/1959/1985/2040 FSCP-RES ... owner=15 ... oend=1419,1873（5 次，全是走链读数）
2466 UNRES-RAIL tile=1419,1873 track=0 phase=release-ghost actor=17
2467 RESV-WATCH CLEAR-RAIL tile=1419,1873 track=0 phase=release-ghost actor=17 head=17
2468 RESV-GHOST couple veh=17 tile=1419,1873 track=0
```

### 17.5 更正后的结论（三条，逐条可证）

1. **【撤回】"1419,1873 的主人是 veh=75"不是日志能证明的**。§16.3 该结论降级为"未定"。
2. **【仍成立】veh=75 确实在这一格预留过**，理由是行为必然性而非 `res=` 读数：`:1245` 它的车头（`origin`=移动前端）就在 `1419,1873`，`:1247` 从该格起 `nTry=20 nOK=19`（向西成功订下 19 格到 1402,1870）。JGRPP 的铁轨列车靠 PBS 预留才可能上格，脚下格与前方格必然是被它自己订下的；SET-RAIL 缺失只是 17.2 的预算所致。`foreign=0`（疑似"占位者不是外人"）与该结论同向，但语义待核，不作为证据。
3. **【仍是本轮真缺陷】清释放一侧没有所有权判据**：`:2466-2468` 由耦合链 17（`actor=17 head=17 head=17`，`phase=release-ghost`）释放这一格。`R3RReleaseChainReservations` / `R3RCollectReservedPathTiles` 判定"这格是不是我的"用的是**沿连续 reserved bits 够不够得到**（`pbs.cpp` FollowReservationEnumerate / `yapf_rail.cpp:496-518` 明确写 "the reserved bits are global and carry no owner"、"do NOT read it as ownership"），因此它**天生无法**区分"本链刚铺的"与"别人留下的"，也天生无法在日志里留下"谁铺的"。⇒ 结论从"误清了 75 的位"改为**"该清扫按设计无法判断主人，本次它清掉的 6 格中至少有一格（1419,1873）是 veh=75 当时自己的预留路径格（`:1247` 已订、`:1277` spd=80 仍在跑），即发生了一次误清"** —— 误清的判定依据是 75 仍在运动 + 该格在它的 nOK 预留序列内，不再依赖 res=/SET-RAIL。

### 17.6 缺 SET-RAIL 这个观察本身的价值（保留为独立缺陷）

探针的"每 (kind,phase) 首例 + 256 汇总"预算，使得**非监视格的预留创建完全不可追溯**：事后想说"这格是谁铺的"根本无据（本轮就是被它坑到）。两条可选修法（待拍板）：

- A：把 `rail_map.h` 的 hook 改为"每格每 phase 首例"，成本可控（key 已存在 `unwatched_seen`，只需把 `pair` 换成 `pair^tile` 并给 seen 表加上限策略）；
- B：给 ledger 记账（KI-359 的账本已被第 303 轮删除）恢复一个只读版本，专门回答"这格现在账面属于谁"；
- C（最小）：把耦合/解挂提交点的嫌疑格（本链脚下 + 前方 N 格）临时加入监视表，只在这些时刻逐格落盘。

### 17.7 需要玩家确认的判别实验（C-1）

若想彻底分清 `1419,1873` 那笔是"75 的活预留"还是"存档带入的幽灵"，唯一可靠手段是把该格加入监视表后复跑同一场景：

- 期望（若 75 铺的）：出现 `RESV-WATCH SET-RAIL tile=1419,1873 track=0 phase=... actor=75`；
- 若复跑后**仍然零 SET-RAIL**而只有 CLEAR：则该位是存档/旧会话带入的幽灵，`§16.5` 第 1 条（陈旧残留）转为对**该格**成立。

### 17.8 状态

- 本轮零源码改动、零构建。
- §16.3 的主人指认撤回；§16.5 两条缺陷**都保留**（第 1 条"陈旧残留"缩小为"1421/1422/1423,1874 三格待定"；第 2 条"清扫无所有权判据"升级为本轮唯一硬结论）。

## 十八、`:2466` + `:2468` 两行的逐字段读法（2026-11-25 问"这份日志代表了什么"）

### 18.1 结论先行：这是**同一个事件的上下两层记录**，不是两次释放

```
2466 UNRES-RAIL tile=1419,1873 track=0 phase=release-ghost actor=17      ← 地图写入层（pbs.cpp）
2468 RESV-GHOST couple      veh=17 tile=1419,1873 track=0                 ← 决策层（train_cmd.cpp）
2475 RESV-GHOST-SUMMARY couple veh=17 cand=6 freed=6                      ← 账：6 收集、6 释放，一格一对
```

判据：`cand=6 freed=6` 且每格只出 `RESV-GHOST` 一次 ⇒ `freed` 计数只在 `train_cmd.cpp:13669` 自增一次，说明这两行是**同一次 `UnreserveRailTrack()`**（写入层由 `phase` 守卫继承标签，见 18.2）。

### 18.2 代码锚点（`src\train_cmd.cpp` 与 `src\pbs.cpp`）

- 决策层 `train_cmd.cpp:13647-13649`：`if (!doomed.empty()) { R3RResvPhaseGuard phase_guard("release-ghost", R3RResvActorID(chain_head)); ... }`
- 校验三道（`13665-13666`）：`GetTileType==Railway && IsPlainRailTile && HasBit(GetRailReservationTrackBits(tile), track)` —— **不满足就 `continue`，一行日志都不会有**
- 写入 `13668`：`UnreserveRailTrack(rec.tile, rec.track);` → 该函数在 `pbs.cpp` 里打 `UNRES-RAIL`，其 `phase=` / `actor=` 取自活跃的 `R3RResvPhaseGuard`，所以必然是 `release-ghost` / `17`
- 决策日志 `13670-13671`：`RESV-GHOST %s veh=%d tile=%d,%d track=%u`，`%s`=`why`（此处 `"couple"`）、`veh`=`chain_head->index.base()`

### 18.3 逐字段含义

| 字段 | 含义 | 来源 |
|---|---|---|
| `why=couple` | 调用点是**挂车提交点**（不是解挂/waiter/GC） | `R3RReleaseChainReservations(chain_head, "couple")` |
| `actor=17` / `veh=17` | **合并后**那条链的链头车号 | `R3RResvActorID(chain_head)` / `chain_head->index.base()` |
| `phase=release-ghost` | 本次写入属于"清扫幽灵预留"阶段，不是 YAPF 订路 | `13648` 守卫 |
| `track=0` | 释放的是 **TRACK_X**（bit 0x1），E-W 轴 | `rec.track` |
| `tile=1419,1873` | 被释放的那一格 | `rec.tile` |

### 18.4 `veh=17` 是谁：既不是 75，也不是 15

```
2463 COUPLE-PREFLIGHT head=17 last=15 fold=0 dirfold=1 gap=221 ... spd=160 db=0
2475 RESV-GHOST-SUMMARY couple veh=17 cand=6 freed=6
```

`:2463` 说明挂车+翻转之后 **链头=17、链尾=15**（挂车机车 15 被翻到尾部）。因此 `veh=17` 只是"清扫的发起者"这个身份，**与"这格是谁铺的"毫无关系**——这正是 §17 说的"日志里没有所有权"。

### 18.5 这两行**证明了什么**（三条，均可核）

1. **该位在清扫那一刻确实存在**：`13666` 的 `HasBit` 校验通过才走到 `13668`，否则无日志。
2. **释放的位与 §16 读到的位是同一笔**：`:1277 RESV-HAVERES veh=75 tile=1419,1873 res=0x1 tracks=0x1` 与 `track=0` 完全对应（同一格、同一 TRACK_X）⇒ 这笔位从 `:1277` 一路活到 `:2466`（约 1 200 行日志跨度）都没被清。
3. **它是被"够得到 + 最近主人"两道闸门放过的**：`13640-13641` `const Train *rail_owner = GetR3RReservationOwnerNearby(t, track); if (rail_owner != nullptr && rail_owner->First() != chain_head) continue;` —— 该函数问的是**沿预留串最近的那列车**，不是逐格账本。75 早已驶离该格 ⇒ 返回 `nullptr` 或返回本链 ⇒ 闸门放行。

### 18.6 这两行**不能证明**什么

- 不能证明主人是 17（`veh=` 只是执行者）；
- 不能证明这格是"陈旧残留"或"活路径"（取决于 75 那一刻是否仍在用它，日志没有逐格账本 —— §17.6）；
- 不能证明释放是错的：若 75 早已把该位交还，清掉它是**正确**行为。只有"75 仍在跑且该位在它 `nOK` 预留序列内"时才叫误清（§17.5 第 3 条）。

### 18.7 一句话口径

> `:2466/:2468` 记录的是：**合并链（头=17，尾=15）在挂车提交点做了一次幽灵清扫，把 `1419,1873` 的 TRACK_X 位删掉了**。它是误清事件的**执行现场**，不是"谁铺的位"的证据；主人判定必须回到 §17.5 的行为链（75 的 `nOK=19` 序列 + 当时 `spd=80`）。

## 十九、`RESV-GHOST` 这个探针"出现"代表什么（2026-11-25 追问）

### 19.1 一句话

**`RESV-GHOST` 是一条「删除成功」回执：R3R 的链编辑清扫在这一刻真的把地图上的一个预留位（轨道位或站台/路点整格位）删掉了。** 它不表示"发现了幽灵"，只表示"删掉了一个"。

### 19.2 它是全量记录，不受采样节流（与 `RESV-WATCH` 完全不同）

`src\r3r_perf.h:408-411`：

```
inline void R3RDbgWrite(const char *fmt, ...)
{
	if (!R3RDbgOn()) return;
	if (!R3RDbgTagAllowed(fmt)) return;
```

- 白名单 `:335` `if (n >= 4 && std::strncmp(fmt, "RESV", 4) == 0) return true;` ⇒ **`RESV-*` 全族放行**（`:336` 的 `UNRES` 同理）；
- 之后**没有**按 key 的边沿节流（不像 `R3RDbgEdge`），只有 `R3RDbgOn()` 一个总闸。

⇒ `RESV-GHOST` 的条数**等于**实际释放的位数，可与 `RESV-GHOST-SUMMARY` 的 `freed=` 逐条对账（§18.1 已做：6 条 = freed 6）。**这一族不能用 §17.3 那种"只有 3 条 ⇒ 被采样吞了"的方式去怀疑完整度**——受采样约束的是 `RESV-WATCH`（监视表 + 每 `(kind,phase)` 首例 + 每 256 次 SUM）。

### 19.3 出现一次 = 走完了这四步

| 步 | 代码 | 含义 |
|---|---|---|
| ① 候选 | `train_cmd.cpp:13643` `doomed.push_back(...)` | 该位在本链 `own_path` 可达集内，且 `:13640` 最近主人闸门通过（`nullptr` 或 `->First()==chain_head`） |
| ② 阶段 | `:13648` `R3RResvPhaseGuard("release-ghost", actor)` | 本次释放归因到某个提交点 |
| ③ 校验 | `:13665-13666` 类型 + `HasBit(GetRailReservationTrackBits(tile), track)` | 位**此刻仍在**，不是陈旧账本 |
| ④ 写入 + 回执 | `:13668` `UnreserveRailTrack` → `:13670` `RESV-GHOST` | 地图位被真正清零，并留下这条去执行记录 |

任何一步不成立（尤其 ③），就**不会**出现 `RESV-GHOST`，但也不会报错——这是"不出现"的主因。

### 19.4 看 `why=` 就知道是谁清的（四个调用点，全覆盖）

| `why` | 调用点 | 时点 |
|---|---|---|
| `couple` | `:13805` | 挂车提交，合并链已成型 |
| `couple-settled` | `:13907` | 几何落定后的补扫（专治"有主但已死"） |
| `decouple` | `:11273/:11274` | 解挂，v 与 u **两侧各扫一次** |
| `waiter` | `:23953` | 等待车底停稳的第一个 tick |

⇒ 同一场景里 `RESV-GHOST` 的条数分布，直接说明"幽灵从哪个提交点漏出来"。

### 19.5 不出现 ≠ 没释放（三个断因，别误读 0）

1. 发布版 `R3R_PROBES=0` ⇒ `r3r_perf.h:426` 把 `R3RDbgWrite` 宏成 `((void)0)`，整族消失；
2. `R3RDbgOn()` 为假（日志关）；
3. 该位在 ③ 校验前已被别人改掉（`HasBit` 假）⇒ `continue`，既不计 `freed` 也无日志（属"地图被别人改了"，非缺陷）。

### 19.6 与 `UNRES-RAIL` 的关系

`UNRES-RAIL` 是 `pbs.cpp` 侧 `UnreserveRailTrack()` 的**地图写入层**记录，带 `phase=`/`actor=`（取自 `:13648` 的守卫）；`RESV-GHOST` 是 `train_cmd.cpp` 侧的**决策层**记录，带 `why=`/`veh=`。同一位一次释放 = 一对行（§18.1）。**排查优先读 `RESV-GHOST`**：它的白名单与节流语义已实证（19.2），`why` 也比 `phase` 更直接指向提交点。

---

## 十五·补（原文保留）

3. veh=75 那两笔（`:1274 RIGHT`、`:1825 LEFT`）的释放行，用于闭环"正常销毁"（玩家已断言，补日志锚点即可）。
4. 本轮仍零源码改动、零构建。

---

## 二十、第 510 轮：玩家复测确认 KI-684 未修（2026-11-26）

### 20.1 玩家结论

- 原话：「请注意，KI684 依然没有修好」。⇒ 已写入 `R3R_KNOWN_ISSUES.md` 的 KI-684 **状态修订 16**，状态维持「未修 / 重新打开」，并**明确封掉 S5 路线**。

### 20.2 版本与有效性

| 项 | 读数 |
|---|---|
| `build\openttd.exe` | 2026-11-26 01:27，54 397 952 B |
| `build\R3R_debug.log` | 2026-11-26 01:29，306 036 B |

该 exe 已含 S5 全套（`snap_meet_tile` / `PFD-MEET` / `dest_tile` 钉 1439），仍复现 ⇒ 采样有效，且**证明 S5 对预留侧无约束力**。

### 20.3 定性收口（本轮不再重跑旧取证）

- S5 改的是「哪一格算到达」（`PfDetectDestination`）与「A\* 朝哪走」（`SetDestination` 的 `dest_tile`）。
- 异常那一笔在**预留写入侧**：`yapf_rail.cpp` 的 `rp()` / `ExtendTrainReservation()` 用 `TrackdirToTrack(...)` 落地，双槽格上写出 `track=4`（TRACK_LEFT），而返程实走 `tracks=0x20`（RIGHT）。
- 两条路径互不约束 ⇒ **终点改对、轨照样写错**。KI-684 的正确定义应改为「预留写入侧写出一条本次路径不存在的轨位」，与 KI-346 同族，不再是「终点判定错格」。

### 20.4 修复落点（未改码）

1. `rp()` / `ExtendTrainReservation()` 每笔预留改用跟随器本步实际 `new_td` 取轨（`FindFirstTrackdir(tdb)` / 循环体 `(Trackdir)FindFirstBit(tb)`），禁止经 `TrackdirToTrack(cur_td)` 与 `ReverseTrackdir` 退化推导；核查第 240 轮 KI-346 是否已覆盖本文件全部写入点（疑仍有第三处）。
2. 写位前追加「该格该轨属于本步 `new_tile/new_td`」自证，不满足只记日志不写位。
3. R-4（`RESV-GHOST couple veh=17` 清 `track=5`）独立加固「清位前校验归属」，不与本条混合。

### 20.5 未确认项（沿用修订 15，仍未取数）

- A：阶段 B 的 `track=5` 是否有独立写位行（探针只在部分 `SetRailReservation` 路径打点）。
- B：`track=4` 是否落在 veh=75 返程真实路径上（在 ⇒ 合法、本条降级；不在 ⇒ 无主位确诊）。
- C：`:2466` 时刻 veh=75 的位置（判 R-4 是否成立）。

### 20.6 复测判据（修法落地后）

1. 同场景不再出现「六格双写」（同一双槽格在一次行程内被订两条不同轨）。
2. `RESV-WATCH CHG-RAIL` 的 `track=` 与该步 `RESV-HAVERES` 的 `tracks &` 一致，且落在 `RP cur=` 序列内。
3. `RESV-GHOST` 释放量下降（不再 `freed=6` 清他车位），`RESV-AUDIT-STRAY` 无新增。
4. 负判据：单轨直行线路预留逐字不变、`COUPLE-OK` 照常、无 `rail_map.h:168` / `pbs.cpp:144` 断言。

---

## 二十一、第 511 轮：KI-684 拟修方案（2026-11-26，先定方案未改码）

### 21.1 读码更正（推翻修订 16 的落点）

| 事实 | 出处 |
|---|---|
| `rp()` 现为**只读**，不再订任何位 | `yapf_rail.cpp` rp() 正文（第 504 轮 E1b 重构） |
| 挂接预留走两遍式 `R3RReservePathSequence(steps, booked)`，逐格 `ReserveSingleTrack(stile, stdir)` | `yapf_rail.cpp` 该函数定义处 |
| `phase=yapf-resv` 的唯一产生点 | `yapf_rail.cpp:1000`（`TryReservePath()` 入口） |
| 普通行驶预留的写位路径 | `yapf_rail.cpp:1044-1091`：`res_dest_node` 逐父节点 `IterateTiles` → `ReserveSingleTrack(tile, td)`（:1064） |

结论：KI-684 那笔异常位由 **`TryReservePath()` 的节点自报 trackdir 直连写位** 产生，与 `rp()` / 挂接路径无关；第 240 轮 KI-346 的修法只覆盖了挂接支，普通行驶支从未被覆盖 ⇒ S5 之后必然仍复现。

### 21.2 方案（三步，按顺序）

- **S1 判定分叉（必须先做，不加行为）**：六格双写到底是「无主位」还是「双槽格上两笔各订一条槽的合法预留」。
  - 判据 A：该六格有无 `RESV-AUDIT-STRAY`（owner=none 才算无主）。
  - 判据 B：veh=75 的 `RP cur=` 序列里是否真的包含 `track=4` 的那几格（在 ⇒ 合法，KI-684 降级为「预留互斥/预览显示」问题）。
  - 判据 C：释放端是否成对释放（`UNRES-RAIL` 的 tile+track 与写入端一一对应）。
  - 若 B 成立 ⇒ 本条转为「双槽格 per-track 互斥与预览」问题，修法不再动写位。
- **S2 写位加实测校验（仅 B 不成立时才做）**：`ReserveSingleTrack(tile, td)` 之前，要求 `td` 属于「本步跟随器实测的 `(new_tile, new_td)` 集合」；取自 `node->IterateTiles` 的 `td` 视为**声明值**，与实测值不一致时按实测值订，不一致本身打日志。
- **S3 兜底闸门**：订位前用 `GetTileTrackStatus`/`HasTrack` 之外再加一条「该 track 在本格是否属于本列车路径」的自证；不满足只记日志不写位（与 KI-346 的 `reserve_if_present` 语义对齐，但补 track 维度）。

### 21.3 明确不做

- 不再改 YAPF 终点（`PfDetectDestination` / `SetDestination`）——S5 已实证无效。
- 不整链回滚、不碰 `cur_real_order_index`。
- 不与 R-4（`RESV-GHOST` 误清 `track=5`）混在同一次提交。

---

## 二十二、第 512 轮：玩家纠正「问题出在 veh=15 身上」（2026-11-26）

### 22.1 玩家原话（决定性口径，优先于探针推断）

> 请注意，KI684 的问题出在 veh15 身上

⇒ 落盘为 `R3R_KNOWN_ISSUES.md` 的 KI-684 **状态修订 17**。修订 14/16 中「订者 = veh=75 普通行驶预留、与挂接寻路无关」的表述**作废**。

### 22.2 与 §十二 / §十三 的关系（自我更正）

- §十二 结论 1（「订者 = veh=75，与 `rp()` / `ExtendTrainReservation()` 均无关」）**不再作为本条口径**。
- §十二 结论 3 把 `FSCP-RES owner=15` 判为「`GetTrainForReservation()` 回溯误报」——**该判断本轮推翻**：按玩家口径，`owner=15` 是正确归属，反而是 `CHG-RAIL actor=75` 的 actor 字段需要存疑（记为新疑点 **R-5**）。
- §十三 已记录「第二次的轨道位是 `track=0/4/5`，与第一次 veh=75 的 `0x1/0x10/0x20/0x11` 不同 ⇒ 确系第二批位置」——这与玩家口径方向一致，只是当时把第二批仍归给 75。

### 22.3 历史旁证（第 471 轮，此前被忽略）

第 471 轮 `veh=15` 的现场问题描述原文即：`1455,1874` 岔口（`tracks=0x11=X|LEFT`、`reserved=0x1` 是自己的 X）为何把预留**拐进北侧平行线 `1421/1420/1419,1873` 且不撤销**。

- 与被报六格**坐标完全重合**（`1421,1873` / `1420,1873` / `1419,1873`，加南侧 `1423/1422/1421,1874`）。
- 当轮已为此在 `ExtendTrainReservation()`（`train_cmd.cpp` ≈ `:17431`）加三处只读探针：`EXT-ENTRY`（入口 `veh= origin= order= spd= first=`）、`EXT-WALK`（`old= → new= bits= skip= stn= plain= exit= cur= choice= target=`）、`EXT-RES`（`veh= tile= td= track= ok= ord=`，落在 `TryReserveRailTrackdir` 调用点）。
- ⇒ 「这六格属于 15 的挂接预留」早有独立线索，不是本轮才提出。

### 22.4 新疑点 R-5：`actor=75` vs `owner=15` 谁是真订者

| 读数 | 行号 | 含义 |
|---|---|---|
| `RESV-WATCH CHG-RAIL tile=1421,1873 track=4 phase=yapf-resv actor=75 head=75` | `build\R3R_debug.log:1825` | 写位探针说写位者是 75 |
| `FSCP-RES owner=15` | 同会话多处 | 溯源探针说归属是 15 |

- 解释甲：`actor` 取 `R3RResvActorID(Yapf().GetVehicle())`，挂接寻路若在发起者实例上跑而写位落在既有预留之外，actor 与实际受益者可分离 ⇒ 以 `EXT-RES/owner=15` 为准。
- 解释乙：`:1825` 确是 75 的返程自订（§十三 阶段 C），玩家说的"第二次"是另一批（15 的 `track=0/4/5`）。
- 判别读数（唯一、必须取）：同一 tick 内对齐 `EXT-ENTRY veh=15` / `EXT-WALK veh=15` / `EXT-RES veh=15 tile=1421,1873` 与 `CHG-RAIL tile=1421,1873 track=?`。`EXT-RES` 在该格出现 ⇒ 甲成立、订者=15。

### 22.5 修法落点（据此改回，取代 §21.2 的 S2/S3 主修点）

- 主修点 = `train_cmd.cpp` 的 `ExtendTrainReservation()`：每笔 `TryReserveRailTrackdir()` / `TryReserveRailTrack()` 用**跟随器本步实际 `new_td`** 取轨，禁止 `TrackdirToTrack(ReverseTrackdir(...))` 退化推导（KI-346 同族）；写位前自证「本格本轨属于本步 `new_tile/new_td`」，不满足只记日志不写位。
- `TryReservePath()`（`yapf_rail.cpp:990/1064`）与 `rp()`（已只读）**本轮不作为本条主修点**，仅作为「若甲不成立」的备选排查面。
- §21.2 的 S1（判定分叉三判据）仍然有效，但判据 B 的样本对象由 veh=75 改为 **veh=15**。

### 22.6 待取数（三条，取完即可动手）

- D-1：`EXT-*` 在 `1421,1873` 是否为 15 写过位（判 R-5）。
- D-2：15 的挂接路径里是否包含 `track=4`（LEFT）那一侧（玩家口径=不包含）。
- D-3：`:2466` 时刻 15 的位置（判 R-4 是否成立）。

### 22.7 复测判据（在 §20.6 四条之上追加）

5. 六格只在 veh=75 通过时被预留；`veh=15`/`veh=17` 的挂接寻路不再在其上写位（`RESV-GHOST couple veh=17` 那 6 行消失）。
6. `FSCP-*` 的 `end=` 必须落在 `1424..1438,1874` 或东侧安全停位 `1439/1440`，不得出现 `y=1873/1875/1876` 的平行线坐标。
7. 负判据：veh=75 一类普通行驶列车的自订预留轨迹逐格不变。

---

## 二十三、第 513 轮：玩家第二次纠正——核心是「预留穿透站台」（2026-11-26）

### 23.1 玩家原话（决定性口径）

> 我纠正，KI684 的核心是 veh15 的挂接寻路预留超出站台，也就是说，预留从站台一边进入，又从另一边出来，但 veh15 的命令是去那个站台挂接

⇒ 落盘为 KI-684 **状态修订 18**。至此本条定性定稿，不再回到「撞车/双写/无主位」的旧叙述。

### 23.2 我此前三轮的错误（逐条作废）

| 轮次 | 我写的结论 | 状态 |
|---|---|---|
| 修订 14 | 六格是 `veh=75` 的普通 YAPF 行驶预留，与挂接无关 ⇒ KI-684 应关闭 | **作废**（修订 17 已改责任方为 15） |
| 修订 15/16 | 六格被写两次，异常在第二次；`track=4` 退化推导（KI-346 同族） | **不是核心**（槽位维度，玩家说的是范围维度） |
| §21.2 S2/S3 | 修 `TryReservePath()` → `ReserveSingleTrack()` 的 trackdir 直连 | **不是核心落点** |
| §12 / §13 | 「预留超出站台」是观感误诊，实为合法路径走向 ⇒ KI-686 可判非缺陷 | **作废**（字面成立，确实越界） |

### 23.3 核心形态（一句话）

`veh=15` 的订单是「去该站台挂接」，配对目标就停在那段站台上；挂接预留却从站台**一侧**进入、沿轨道继续走、从**另一侧**穿出站台，并在站台之外继续订位。

⇒ 缺陷是**预留终止边界缺失**，不是轨道槽位取错。

### 23.4 与第 471 轮现场吻合

第 471 轮 `veh=15` 的问题描述原文：「`1455,1874` 岔口（`tracks=0x11=X|LEFT`、`reserved=0x1` 是自己的 X）把预留**拐进北侧平行线 `1421/1420/1419,1873` 且不撤销**」。

- `1424,1874` = 站台内（车底所在线）
- `1421/1420/1419,1873` = 该站台北侧的**另一条轨**
- 即「从站台一边进（1874 线）、从另一边出（拐到 1873 线）」的逐格坐标形态。

### 23.5 猜测根因（待验证，登记即落盘）

- **G-1（主）**：预留的停止判据是「路径还能不能走 / 目标格是否已达」，而非「下一格是否仍在配对目标那段站台内」。`pHead/pTail/end` 已被正确算出（`CPL-ENDTILE` / `CPL-ENDTILE-PAIR`），但没被当作硬边界用。
- **G-2**：站台是「整格位 + 一步跳过 `tiles_skipped`」的对象，跳过后的落点已在站台另一端之外，延长逻辑把它当新起点继续走 ⇒ 越界部分被当成正常延伸。
- **G-3**：`EXT-WALK` 的 `stn=` / `plain=` / `target_seen` 三处 stop 位只在「目标已见过」时生效；穿出站台恰好发生在「目标在身后 / 在侧线」的时刻，条件不成立 ⇒ 一路订到站台外。

### 23.6 修法方向（一句话）

**每写一格之前先判「这一格是否还在配对目标那段站台内」，不在就停，并且停优先于走。** 最坏结果是少订一格（车多等一 tick），绝不能多订到站台外。

- 落点一：`train_cmd.cpp` 的 `ExtendTrainReservation()` 终止链（`stn` / `plain` / `target_seen` 三处 stop 位之前加站台范围判）。
- 落点二：`yapf_rail.cpp` 的 `R3RReservePathSequence()` 站台段范围（跳段后的落点不得被当作站台内的新起点）。
- 新增一行诊断：`EXT-STOP reason=off-platform tile=x,y td=? seg=pHead..pTail`，把「为什么在这里停」写死进日志，方便复测时一眼确认。

### 23.7 取证 D-4（唯一决定性取数，只读现有日志）

按 tick 对齐三组读数，判预留末格是否落在站台之外：

1. `CPL-ENDTILE` / `CPL-ENDTILE-PAIR` 的 `pHead` / `pTail` / `end` / `stn=`
2. `veh=15` 的 `RP cur=` 末端格序列
3. `EXT-ENTRY` / `EXT-WALK` / `EXT-RES`（`veh=15`）

判据：末格坐标 ∈ 站台格集合 ⇒ 未越界（改查其他线）；末格在站台外 ⇒ **越界确诊**，直接按 §23.6 动码。

### 23.8 复测判据（核心两条）

1. `veh=15` 挂接预留的最后一格必须落在站台格内（同站台、同轴、在 `pHead..pTail` 区间），**不得出现站台对侧 / 平行线坐标**（`y=1873/1875/1876` 之类）。
2. 负判据：`veh=75` 一类普通行驶列车的自订预留逐格不变、`COUPLE-OK` 照常、无 `rail_map.h:168` / `pbs.cpp:144` 断言。

### 23.9 现有日志读数（2026-11-26，只读取证）

命令：`findstr /n /c:"CPL-ENDTILE" /c:"EXT-ENTRY" /c:"EXT-WALK" /c:"EXT-RES" /c:"CPL-BLOCKED-WAIT" build\R3R_debug.log`（结果落 `build\_w3.txt`，5 行）。

| 行号 | 读数 |
|---|---|
| `:1680` | `CPL-BLOCKED-WAIT veh=15 tile=1474,1884 dest=1437,1874` |
| `:1907` | `CPL-BLOCKED-WAIT veh=15 tile=1460,1875 dest=1437,1874` |
| `:1956` | `CPL-BLOCKED-WAIT veh=15 tile=1457,1874 dest=1437,1874` |
| `:1982` | `CPL-BLOCKED-WAIT veh=15 tile=1456,1874 dest=1437,1874` |
| `:2037` | `CPL-BLOCKED-WAIT veh=15 tile=1451,1874 dest=1437,1874` |

判读：

1. `dest=1437,1874` **恒不变**，与已知配对目标（车底 `1424..1437,1874`，`pTail=1437,1874`，`end=1438,1874`）一致 ⇒ 目标算对了，问题不在终点。
2. `CPL-ENDTILE` / `EXT-ENTRY` / `EXT-WALK` / `EXT-RES` **零命中** ⇒ 这份日志由未含这些探针的 exe 产生（或探针被 `R3RDbgEdge` 窗口抑制）⇒ **现有日志无法直接读出「预留末格」**。
3. 五条全是 `CPL-BLOCKED-WAIT`（预留未铺成即放弃）⇒ 这份日志记录的是「铺不出来」阶段，尚未到「铺成后穿透站台」的形态。
4. veh=15 自身位置按 `1474,1884 → 1460,1875 → 1457,1874 → 1456,1874 → 1451,1874` 收敛，**跨了 y=1884 / 1875 / 1874 三条线** ⇒ 它的进场路径本身就在换线（`1460,1875` 与 dest 的 `1874` 不同线）。故「从一边进、从另一边出」在日志里可能部分体现为**进场路径绕行**，与「预留末格越界」必须分开判。

新疑点 **R-6**：`CPL-BLOCKED-WAIT` 的 `tile` 是车自身位置而非预留末格；把两者混读会把「车在绕行」误判成「预留越界」。

待取 **D-5**：含 `CPL-ENDTILE` + `EXT-*` 探针的 exe 复跑同场景（或确认当前 `build\openttd.exe` 是否已含这些字面量），拿到 `pHead/pTail/end` + `RP cur=` 末端 + `EXT-RES` 逐格，即可闭口判 §23.7。

---

## 二十四、第 514 轮：按 A 方案动手（2026-11-26 玩家拍板「算了，就按A修复吧」）

### 24.1 玩家拍板

> 算了，就按A修复吧

A = 不依赖更多日志，直接改「预留终止边界」：订每一格前先判「这一格是否还在配对目标那段站台内」，不在就停，停优先于走。

### 24.2 读码发现（决定落点，重要）

已通读 `src\train_cmd.cpp` 的 `ExtendTrainReservation()`（`17221`–`17621`，逐行读完）。**结论：这条延长链不可能是「穿透站台」的写位者。**

- 循环体 `17312`：`bool target_seen = ft.is_station || (IsTileType(ft.new_tile, TileType::Railway) && !IsPlainRail(ft.new_tile));`
- `17313`：`if (target_seen || KillFirstBit(ft.new_td_bits) != TRACKDIR_BIT_NONE)` ⇒ 走 `17314`–`17427`，**直接 `return PBSTileInfo(ft.new_tile, ft.old_td, false)`（okay=false，交给 YAPF）**。
- 即：跟随器一路进站台格，`is_station` 立刻为真 ⇒ 延长在「进站台的第一格」就停并交棒，**不可能沿站台走完再从另一头穿出去**。
- 真正逐格铺预留的是 YAPF 侧（`src\pathfinder\yapf\yapf_rail.cpp` 的挂接路径铺位序列，第 471 轮 `RP cur=` / `REACH cur=` 那套探针就在那里），不是 `ExtendTrainReservation()`。

⇒ **A 方案的落点从 `ExtendTrainReservation()` 改到 `yapf_rail.cpp` 的挂接预留铺位处**（`Train::TryReservePath` / YAPF 的 `R3RReservePathSequence` 一类）。`ExtendTrainReservation()` 只保留一句话日志的机会，不动判定。

### 24.3 修改口径（定稿）

1. 仅对 `v->current_order.IsType(OT_GOTO_COUPLE)` 且存在活配对（`R3RGetCouplePairPartner(v) != nullptr`）生效。
2. 以配对伙伴链上第一个站台格为参考（`station index` + `platform axis` + **垂直轴上的行坐标**，三者必须一致）⇒ 只有「同一站台的同一条平台」算站台内；同站不同平台（`y=1873` 对 `y=1874`）判为站台外。
3. 铺位状态机：一旦本次铺位路径踏入目标平台（`on_platform = true`），此后**任何一格落在平台外即停止铺位并成功返回**（保留已铺的位，不 back-out）。
4. 停优先于走：最坏结果是少铺一格（车多等一 tick），绝不多铺到站台外。
5. 负判据：非 `GOTO_COUPLE` 列车、无配对伙伴的列车、普通行驶列车，铺位逐格不变。

### 24.4 待验证（改完必查）

- `CPL-ENDTILE` / `CPL-ENDTILE-PAIR` 的 `pHead/pTail/end` 落在哪条平台（判第 2 条的参考点是否取对）。
- 若 `R3RGetCouplePairPartner()` 在铺位时刻尚未建立（配对是寻路成功后才有），参考点须回退到 `v->dest_tile` 所在平台。

---

## 二十五、第 514 轮：A 方案已落地（仅改 `src\pathfinder\yapf\yapf_rail.cpp`）

### 25.1 落点

`R3RReservePathSequence(const std::vector<std::pair<TileIndex, Trackdir>> &steps, size_t &booked)`（该文件 `:744` 起）—— 这就是挂接两遍法第二遍「逐格真订」的唯一入口（`r3r_single_seg` 分支专用；非单段挂接走原生 `TryReservePath()`，不由本函数铺位）。它的循环原本只有「第三方占用」一个停止分支（`ReserveSingleTrack` 失败 ⇒ `KI-563 A` 保留已订格），**完全没有站台范围判据** —— 这正是「可以从站台一边订进去、从另一边订出来」的结构性缺口。

### 25.2 参考站台怎么取（最终实现）

1. `R3RGetCouplePairPartner(r3r_coupler)` 取配对伙伴，沿伙伴链 `Next()` 找**第一个 `IsRailStationTile` 且非 `IsRailWaypointTile`** 的车节所在格 ⇒ `r3r_plat_ref`。
   - 刻意**不**用 `tg`（= `consist_head_tile` / 会合格）：第 471 轮现场 `tg` 常常是站台外一格的 `end=1438,1874`，用它当参考会让判据整体失效。
   - 刻意**不**用 `v->dest_tile` 兜底：伙伴不在站台上时（库里/野地/路点）应整块不参与，若此时按 `dest_tile` 的站台去截断，会把「伙伴在站台外、会合点在站台外」的合法挂接截死在站台边界。⇒ **作用域收窄为「伙伴停在站台上」**，`r3r_plat_ref == INVALID_TILE` 时本段逻辑一字不参与。
2. 站台段范围：以 `GetRailStationAxis(r3r_plat_ref)` 定轴，沿轴正反各走 `IsCompatibleTrainStationTile(...)` 直到不兼容（上限 64 格，`TileAddWrap` 防出图），得连续平台段 `[lo, hi]` 与所在行 `perp`（`Axis::X` ⇒ 行 = `TileY`）。
   - 用**连续段**而不是 `IsCompatibleTrainStationTile` 全域：同站同轴的另一段平台（被道口/正线切开的第二段）不会把 `entered` 状态提前置位。

### 25.3 判据（每格写之前）

```
same_row  = (perp(stile) == plat_perp)
on_piece  = same_row && lo <= axial(stile) <= hi
below     = same_row && axial <  lo && axial >= lo - 8     // 贴边接近侧
above     = same_row && axial >  hi && axial <= hi + 8
leaped    = (below && seen_hi) || (above && seen_lo)       // 整段被跳格跨过
if (entered || leaped) => 不订这一格，break（成功收尾）
状态更新：on_piece ⇒ entered=true；below ⇒ seen_lo=true；above ⇒ seen_hi=true
```

- `entered` 覆盖「同一排穿过站台」：踩进平台段之后任何一格落到段外即停。
- `leaped` 覆盖「站台被 `TrackFollower` 一步跳过」：`tiles_skipped` 会让 `r3r_resv_path` 里**根本没有站台格**，此时 `cur -> new_tile` 直接由 `lo-1` 跳到 `hi+1`，光靠 `entered` 永远不置位 —— 这就是「从一边进去、从另一边出来」最隐蔽的形态。
- `below/above` 限死 `±8` 格：同排但隔几百格的另一段线路（同一个 `y` 坐标）不得参与判据，否则会误触发。跳格落点恰为 `hi+1` / `lo-1`，`8` 足够。
- 停止时**不**置 `res_fail_tile`、**不**置 `stopped_at_goal`、**不**回滚：`booked` 里已经是站台内订到的格，调用方 `booked > 0 && booked < steps.size()` 那条既有分支会把预留终点收缩到最后一格已订（与该函数原有的部分预留语义完全一致）。

### 25.4 诊断行

```
EXT-STOP veh=%d reason=off-platform nth=%u/%u tile=%d,%d td=%d plat=%d,%d axis=%d lo=%d hi=%d entered=%d leaped=%d booked=%u
```

写在停止分支里（`R3RFopenDbg("a")` + `fprintf`，本文件禁用 `snprintf`）。判读：出现 `EXT-STOP` ⇒ 拦下了一次越界；若 `nth` 越接近 `steps.size()` 说明越界发生在末段。

### 25.5 复测判据

1. `veh=15` 的挂接预留末格必须落在站台格内（同站台、同轴、在伙伴 `pHead..pTail` 区间），不得再出现 `y=1873/1875/1876` 一类站台对侧或平行线坐标。
2. 越界被拦下时应见 `EXT-STOP ... reason=off-platform`，且随后 `COUPLE-OK` 仍能成立（少订一格 ⇒ 多等一 tick 重算，不应导致挂接失败）。
3. 负判据：`veh=75` 等普通行驶列车自订预留逐格不变（本例根本不进 `R3RReservePathSequence()`）；无 `rail_map.h:168` / `pbs_map`/`pbs.cpp:144` 一类断言。
4. 伙伴不在站台上的挂接（库内/野地/路点等待车底）行为逐字不变（`r3r_plat_ref == INVALID_TILE`）。

### 25.6 状态

- 源码已改（两个插入点，均在本文件内，**未碰任何 `src\*.h`**）。
- 构建自证（2026-11-26 02:27 完成，增量）：复用既有入口 `R3R_fullbuild.cmd inc detach`（未新建任何 `.cmd`）；
  - `build\R3R_incbuild.guard.log` = `GUARD: incremental is safe (no header/lang file is newer than the newest object)`
  - 日志 `build\R3R_incbuild.log`：`[1/5]` FindVersion 配置 → `[2/3] Building CXX object CMakeFiles\openttd_lib.dir\src\pathfinder\yapf\yapf_rail.cpp.obj` → `[3/3] Linking CXX executable openttd.exe` → `==== R3R build ended 2026/11/26 2:27:02.07 MODE=inc EXIT_CODE=0`
  - `build\R3R_incbuild.done` = `EXIT_CODE=0`（02:27:02）；日志无 `error C*` / `fatal error` / `FAILED:` / `build stopped`
  - 时间链：`src\pathfinder\yapf\yapf_rail.cpp` 02:04:49 → `...\yapf_rail.cpp.obj` 02:22:26 → `build\openttd.exe` 02:26:21（54 397 952 B，晚于全部源码）
  - `read_lints` 该文件 0 条
  - exe 字面量自证：`Select-String build\openttd.exe -Pattern 'reason=off-platform'` 命中，行内含完整格式串 `EXT-STOP veh=%d reason=off-platform nth=%u/%u tile=%d,%d td=%d plat=%d,%d axis=%d lo=%d hi=%d entered=%d leaped=%d booked=%u`
- 状态 = **已实现 + 已编译**，游戏内复测待做（判据见 25.5）。

## 二十六、第 515 轮：A 方案复测**未命中**（EXT-STOP 零出现）

**玩家 2026-11-26 原话**：「EXT-STOP一次都没出现，预留末格依然超出站台」

### 26.1 现象定性（先落盘，未取证）

- 判据完全没有触发 ⇒ 第 514 轮的代码**没有生效在真实现场上**，而不是"生效了但判错"。
- 关键事实：`EXT-STOP` 那条 `fprintf` **没有 `R3RDbgOn()` 闸门**（见 `yapf_rail.cpp:853`），只要走进 `if (r3r_plat_ref != INVALID_TILE)` 且判据成立就一定会写盘。零出现只有三种可能：
  1. `r3r_plat_ref == INVALID_TILE`（整块逻辑被作用域收窄屏蔽掉）；
  2. 进去了但 `r3r_plat_entered`/`r3r_leaped` 恒假（判据几何算错，静默失效）；
  3. 根本没走到 `R3RReservePathSequence()`（越界预留由别的代码写）。
- 三者都**不会留下任何日志**，所以必须补"入口摘要"探针才能分辨。这是本轮最重要的教训：**静默分支必须配入口/出口摘要行，否则"没日志"无法与"没执行"区分**（与第 165 轮 RESPACE-AFTER-EDIT 每次提交点都打是同一条纪律）。

### 26.2 候选根因（全部待验证，按可能性排序）

- **G-1（最可能）配对锁缺失**：`R3RGetCouplePairPartner()`（`couple_group.cpp:537`）要求 `r3r_couple_target` / `r3r_couple_requester` 互指且一致，否则返回 nullptr。若寻路发生在 `R3RPairCoupleTargets()` 之前（或锁已被清），`r3r_plat_ref` 永远是 `INVALID_TILE` ⇒ 整块跳过。**验证判据**：入口摘要行打出 `pair=` 车号；若恒 `pair=-1` 而现场确实有 CPL-PAIR 行 ⇒ 证实。
- **G-2 伙伴链上第一个"站台格"找不到**：车底停在站台**外一格**（会合点常如此）或整条链在路点格上 ⇒ 循环扫不到 `IsRailStationTile && !IsRailWaypointTile`。**验证判据**：入口行打 `pairtile=` 与 `pairfirst=`，看伙伴车节的格型。
- **G-3 几何判据静默失效**：`r3r_plat_row_x` 由 `GetRailStationAxis()` 定轴。若站台轴向与列车行进方向**垂直**（行包式/尽头式布局），`r3r_same_row` 恒假 ⇒ `entered`/`leaped` 恒假 ⇒ 零日志。这正是 25.6 预留的 U-2。**验证判据**：入口行打 `axis/perp/lo/hi/ref`，与 `RP cur=` 的坐标对照即可判。
- **G-4 打错落点（结构性）**：越界的预留未必由 `R3RReservePathSequence()` 写。修订 18 曾把主修点判给 `ExtendTrainReservation()`，第 514 轮以"单段挂接只走 yapf"为由否掉了 D-4 取数。若现场根本不在 `r3r_single_seg` 分支（例如非单段、或走 `TryReservePath()` 的普通行驶支 + `rp()`），A 方案就是打在空处。**验证判据**：入口行加 `seg=`（是否单段）+ `steps=`；若整场零 `EXT-PLAT` 行而 `RP cur=` 有输出 ⇒ 证实未走本函数。
- **G-5 站台段切分与玩家口径不一致**（U-1 复现）：伙伴链上若有两段同轴站台（被道口/正线切开），我取的是伙伴所在那一段；玩家看到"超出站台"的那几格可能属于另一段。此情形只会"少拦"，不解释"零触发"。

### 26.3 下一轮探针计划（只读，仅 `yapf_rail.cpp`）

- `EXT-PLAT`（入口，每次 `r3r_resv_path` 非空即打一行，节流：同车同时只打一次/2s）：`veh= pair= pairtile= ref= axis= perp= lo= hi= steps=`
- `EXT-DONE`（出口：`return` 前一行）：`veh= booked= nth/steps= stopped= entered= ref=`
- 判读表：零 `EXT-PLAT` ⇒ G-4；`EXT-PLAT` 有而 `ref=-1` ⇒ G-1/G-2；`ref` 有效而 `lo==hi`/`same_row` 恒假 ⇒ G-3。

### 26.4 取证（2026-11-26 02:34 的 `build\R3R_debug.log`，226 635 B）

**⇒ G-1 基本坐实；另发现 G-6（新，跨排拐出）。**

读数（`build\R3R_debug.log`，用 `Select-String` 取，因该文件在 .gitignore 里 `search_content` 看不见）：

- `EXT-STOP` 计数 = **0**；`RP cur=` = 43 行；`CPL-PAIR` = 70 行。
- `CPL-PAIRSCAN tick=41925 act=15 state=steady ord=16 ... best=118 bestDist=23 paired=-1 wait=-1 near=-1 ...` ⇒ **veh=15 在寻路时刻 `paired=-1`（配对锁不存在）**。
- 配对锁确实存在过，但属于另一对：`CPL-PAIRLOCK tick=37894 why=scan act=75 actHead=75 actTile=1505,1875 actPairT=18 actPairR=1048575 tgt=18 tgtHead=18 tgtTile=1505,1875 tgtPairT=1048575 tgtPairR=75`。
- 摸路末段：`RP cur=1448,1874 td=0 depth=1..10` 一路递减到 `RP cur=1439,1874`（全部 y=**1874**）；`RESV-HAVERES veh=15 tile=1447,1874 enterdir=0 res=0x1 order=16`。

**判定一（G-1 成立，零 EXT-STOP 的充分解释）**：`R3RGetCouplePairPartner()`（`couple_group.cpp:537`）要求 `r3r_couple_target` / `r3r_couple_requester` 互指且一致；veh=15 当时 `paired=-1` ⇒ 返回 `nullptr` ⇒ `r3r_plat_ref == INVALID_TILE` ⇒ `yapf_rail.cpp:839` 那个 `if` 整块不参与，**一行日志都不会有**。第 514 轮把参考源押在配对锁上＝押在"锁在本 tick 之前就已建立"这个未验证前提上，属**前提错误**，不是判据写错。

**判定二（G-6，新猜测根因，待验证）**：越界形态是**换排拐出**（历史现场 `1424..1437,1874` 的站台线 → 越界格 `1419..1421,1873`，y 由 1874 变 1873）。而第 514 轮的两条判据都以 `r3r_same_row`（`perp == plat_perp`）为前提：
- `entered` 需要 `same_row && axial∈[lo,hi]`；
- `below/above`（进而 `leaped`）也需要 `same_row`。
⇒ 一旦摸路拐到相邻排，`same_row` 恒假，**三条子判据同时静默失效**，即便 `r3r_plat_ref` 有效也打不出 `EXT-STOP`。这是判据结构缺陷（把"越界"定义在"同排"坐标系里）。

**修法（第 515 轮，两处一起改）**：
1. **参考源去锁化**：`r3r_plat_ref` 改为三级：① 配对锁伙伴链上的站台格（保留原逻辑）；② 锁缺失时改用**摸路终点** `steps.back().first`（即 `SetReservationTarget` 传的会合点 `tg`）——会合点必然紧邻配对车底；若它不是站台格，就在它同排 ±8 格内找第一个 `IsRailStationTile && !IsRailWaypointTile`；③ 仍找不到则 `INVALID_TILE`（整块不参与，行为不变）。
2. **越界判据去"同排"限制**：`entered` 仍按"同排且在 [lo,hi]"置位（这是"进入"的定义），但**越界判定改为 `entered && !(同排 && on_piece)`** —— 踩进站台段之后，**任何**落在该段之外的格（含换排、含轴向越端）都停。另给 `leaped` 补一条"跨排跳格"：`|perp - plat_perp| == 1 && axial > hi + 8`（或 `< lo - 8`）且此前有同排贴边读数 ⇒ 停。`below/above` 的 ±8 窗口保留（避免同排远处线路误触发）。


- 复测取数建议（一条命令即可定位）：在 `build\R3R_debug.log` 里 `findstr /n /c:"EXT-STOP" /c:"CPL-RES-SAFE-STOP veh=15" /c:"CPL-ENDTILE-PAIR" /c:"COUPLE-OK loco=15"`；判读三件事：①`EXT-STOP` 的 `tile=` 是否已到站台外沿（`hi+1`/`lo-1`）且 `plat/lo/hi` 与伙伴站台一致；②`booked` 是否为站台内最后一格（`booked < nth`）；③随后是否仍出现 `COUPLE-OK`（少订一格不应导致挂接失败）。
- 未确认（保留）：U-1 若伙伴链上有**两段同轴站台**（被道口/正线切开），`r3r_plat_ref` 取到的是车头所在那一段，若会合点在另一段则该段不会被当作边界 —— 属"少拦"不属"多订"，可接受但需现场确认；U-2 站台轴向与列车行进方向垂直的行包式布局（`Axis` 与 `td` 不同轴）未取数。

### 26.5 第 515 轮落地（2026-11-26）

**⇒ 已实现 + 已编译（只改 `src\pathfinder\yapf\yapf_rail.cpp`，未碰任何 `src\*.h`）。**

**落地内容（`R3RReservePathSequence()`，函数入口 :744）**

1. **参考站台三级锚点（去锁）** —— 新增 `int r3r_pair_idx`；原配对锁扫描保留为第一级，锁缺失时追加：
   - ② `steps.back().first`（`SetReservationTarget()` 收到会合点 `tg`），若它是真站台（`IsRailStationTile && !IsRailWaypointTile`）直接采用；
   - ③ 否则在会合点四方向 ±8 格（`TileAddWrap`）内找第一个真站台格；
   - ④ 仍无 ⇒ `INVALID_TILE`，整块判据不参与（行为退回第 511 轮之前，逐字不变）。
2. **无锚点留痕 `EXT-NOREF`** —— `static uint32_t r3r_noref_seen[64]` 按车号（环形去重）每车只打一次：`EXT-NOREF veh= pair= steps= goal=x,y`。目的：把"日志没有"与"没走到这里"彻底分开（第 514 轮就是栽在这个不可区分点上）。
3. **越界判据去"同排"限制 + 补跨排** —— 循环内改为三条并列，任一成立即 `break`（不订该格、不置 `res_fail_tile`、不回滚）：
   - ① `r3r_leaped`（同排整段跨过，**语义不变**）：`(below && seen_hi) || (above && seen_lo)`；
   - ② `r3r_off_piece = r3r_plat_entered && !r3r_on_piece`：踩进站台段之后**任何**不在段内的格（换排/轴向越端都算）都停。**订正第 514 轮的实现错误**：原写法 `if (r3r_plat_entered || r3r_leaped)` 等于"第二次踩上站台就停"，且因站台格被 `r3r_resv_path` 整体排除（`:2184` 的 `if (!IsRailStationTile(cur) && !IsRailDepotTile(cur))`）而恒不可达；
   - ③ `r3r_cross_row`（**新，REG-515-A**）：`|perp - plat_perp| == 1`（相邻排）且此前已有同排贴边读数（`seen_lo||seen_hi`）且 `axial ∈ [lo-8, hi+8]` ⇒ 停。这是现场真实越界形态（站台 y=1874 → 越界格 y=1873/1875/1876）唯一能命中的判据。
   - 探针升级：`EXT-STOP veh= reason=<cross-row|leaped|off-platform> nth=/ steps= tile= td= plat= axis= lo= hi= perp= platperp= entered= leaped= crossrow= booked=`（`reason` 走 `%s`，exe 里 `reason=%s` 与三个取值是独立字面量，别按整串搜）。

**结构性事实（本轮确认，纠正 §26.2 的猜测方向）**

- `r3r_resv_path` **不包含站台格**（`:2184` 显式排除，只有"整段跨过"分支 `:2206` 才把会合点 `tg` 自身补进来，且它是最后一格）。⇒ 任何"靠站台格自身置位"的判据在本函数里天然不可达，边界只能建立在**贴边格读数**（`below/above`）与**换排**上。
- 因此判据①②在"站台被整段跳过"的现场形态下真正能干活的只有①（`below && seen_hi`）；②③是为"会合点落在站台内 / 路线拐到相邻排"两类形态兜底。

**REQ 未做（保持原口径）**

- 不动 `ExtendTrainReservation()`（`train_cmd.cpp`）那条链；不动 `rp()/reach()` 的摸路（它们只找路，不写预留位）。
- 不动 `ReserveRailStationPlatform()` 的整格位语义：站台那一段的预留仍由它按 `tg` 补齐，A 方案只负责**不往站台外延伸**。

**复测判据（6 条）**

1. `EXT-NOREF` 若出现，必须能读到 `goal=`；`goal` 与会合点一致而 `ref` 仍无 ⇒ 扫描窗口 ±8 不够（需扩窗，属新缺陷）。
2. `EXT-STOP` 至少出现一次，`reason=` 为 `cross-row`（现场主形态）或 `leaped`；`tile=` 必须是站台外沿（`hi+1`/`lo-1`）或相邻排（`|perp-platperp|==1`）。
3. `booked < nth`（预留被截断），且 `CPL-RES-OK/CPL-RES-PARTIAL` 的 `tiles=` 末格不再落在站台另一侧。
4. 随后仍出现 `COUPLE-OK loco=15`（少订一格只应多等 tick，不应挂不上）。
5. 负判据：`veh=75` 一类普通行驶列车逐格预留不变（它们不进本函数）。
6. 负判据：站台**前**的正常接近（`1449..1440,1874` 全在 `hi+1..hi+8`）不得被截断到 `hi+9` 以外。

**构建自证（第 515 轮）**

- 入口：复用既有 `R3R_fullbuild.cmd inc detach`（**未新建任何 `.cmd`**）。
- `build\R3R_incbuild.guard.log` = `GUARD: incremental is safe (no header/lang file is newer than the newest object)`。
- `build\R3R_incbuild.done` = `EXIT_CODE=0`（2026/11/26 02:43:42）。
- 日志尾：`[2/3] Building CXX object ...yapf_rail.cpp.obj` → `[3/3] Linking CXX executable openttd.exe`；`error C|fatal error|FAILED:|build stopped` 计数 = **0**。
- mtime 顺序：`src\pathfinder\yapf\yapf_rail.cpp` @02:41:26 → `yapf_rail.cpp.obj` @02:42:05 → `build\openttd.exe` @02:43:28（54 397 952 B）。
- `read_lints`：`yapf_rail.cpp` **0** 条。
- exe 字面量：`crossrow=%d`、`EXT-NOREF veh=%d`、`leaped=%d` 三串均命中（`findstr /m` 各返 19 B 文件名）。

**状态：已实现 + 已编译（内测树 `build\openttd.exe` @2026-11-26 02:43:28）；游戏内复测待做。发布树未编。**

---

## 27. 第 516 轮：复测**仍未解决**，且证明 A 方案治错了层（2026-11-26）

### 27.1 玩家报告

> 「编译已经完成，但 KI684（也就是我们正在对付的问题）并没有解决」

### 27.2 采样有效性（先自证再下判断）

- `build\openttd.exe` @ **2026/11/26 02:43:28**（54 397 952 B）= 第 515 轮产物。
- `build\R3R_debug.log` @ **2026/11/26 02:48:12**（225 449 B）**晚于** exe ⇒ 本日志确由 515 轮 exe 产生，读数可用。

### 27.3 探针计数（`build\R3R_debug.log`）

| 探针 | 计数 | 判读 |
|---|---|---|
| `EXT-NOREF` | **0** | 参考站台**拿到了**（三级锚点生效，`r3r_plat_ref != INVALID_TILE`） |
| `EXT-STOP` | **0** | 三条越界判据**一次都没命中** |
| `RESV-SELF-AIR` | **74** | `R3RReservePathSequence()` **确实被调用、循环确实在跑**（不是"函数没执行"） |
| `CPL-RES-OK` | 2 | 两次"整条路径全订上"，从未被 break 截断 |
| `CPL-RES-PARTIAL` | 2 | 另一类失败（订不上，非本判据） |
| `CPL-RES-FAIL` | 1 | `nth=1/44 tile=1472,1884 res=16 owner=15 selfheld=1`（自己已持有，属 KI-563 A 正常续订） |
| `CPL-BEST` | 8 | `veh=15 tile=1439,1874 td=0`（恒为同一会合点） |
| `CPL-PAIRLOCK` | 2 | **本次配对锁建立了**：`act=15 actTile=1474,1884 … tgt=18 tgtTile=1424,1874 tgtLen=216` |
| `COUPLE-OK` | 1 | 有一次挂接成功（需确认是否 loco=15） |

⇒ **`EXT-NOREF=0` + `RESV-SELF-AIR=74` 两条合起来把"没执行/没锚点"两种可能彻底排除**，剩下的唯一可能是**判据几何恒假**。

### 27.4 现场几何（`build\R3R_debug.log` 行号 + 原文）

- 伙伴车底：`:29 CPL-PAIRLOCK tick=41493 … act=15 actTile=1474,1884 … tgt=18 tgtTile=1424,1874 tgtLen=216`；上一轮同场景 `CPL-SNAP … near=1437,1874 meet=1439,1874 head=1424,1874 tail=1437,1874` ⇒ **站台 ≈ `1424..1437/1438 × y=1874`，被 27 节车底占满**。
- 会合点：`:3/:31/:36/:62/:68/:71/:78/:81 CPL-BEST veh=15 tile=1439,1874 td=0`（8 次全部相同）⇒ **`tg = 1439,1874`，在站台 hi 之外 +2 格**。
- 预留路径（`:1500-1522` 与 `:1525` 的 `tiles=` 全量）：

  ```
  1473,1884/0 1472,1884/4 1472,1883/5 1471,1883/4 1471,1882/5 1470,1882/4
  1470,1881/5 1469,1881/4 1469,1880/5 1468,1880/0 1467,1880/0 1466,1880/0
  1465,1880/4 1465,1879/5 1464,1879/4 1464,1878/5 1463,1878/4 1463,1877/5
  1462,1877/4 1462,1876/5 1461,1876/4 1461,1875/5 1460,1875/0 …(斜线南下)
  1459,1875 1458,1875 1457,1875 1457,1874 1456,1874 … 1441,1874 1440,1874
  ```

- 末次：`:2086 CPL-RES-SAFE-STOP veh=15 nth=9/10 tile=1440,1874 td=0 booked=10 goal=1`、`:2087 CPL-RES-OK veh=15 total=10 booked=10 … tiles=1449,1874/0;…;1440,1874/0`。

### 27.5 结论（推翻第 515 轮的前提）

**A 方案的判据是"路径穿过站台再出去"，而本现场根本不存在这个形态：**

1. `r3r_resv_path` 排除站台格（`:2184`），路径上**一格站台都没有**；
2. 路径在 `y=1874` 上的范围是 **`1440..1457`**，站台是 `1424..1437/1438` ⇒ **从未进入 `[lo,hi]`**，`r3r_plat_entered` 恒 false ⇒ 判据②（`off_piece`）恒假；
3. 路径一路 `x` 递减到 1440 就结束，**从未跨到 lo 侧** ⇒ 判据①（`leaped`）恒假；
4. 相邻排只出现在 `1458..1460,1875`（`perp = plat_perp + 1`），但 `axial = 1458~1460 > hi + 8`（`1445/1446`）⇒ 判据③（`cross_row`）恒假。

⇒ **三条判据零触发不是实现 bug，是几何必然。** 第 515 轮的推断（"判据建立在同排/换排坐标系上，改掉就能拦"）在现场不成立。

**真正的偏差在上游，不在预留函数里：**

- `CPL-SNAP … meet=1439,1874`（= `tail 1437 + 2`）**已经把会合点定在站台外**；
- `CPL-BEST tile=1439,1874` 忠实继承它；
- `R3RReservePathSequence()` 只是在 `cur == tg` 时 `return true` 且不入列 ⇒ 预留自然止于 **1440 = `tg - 1`**。

⇒ 「预留末格落在站台外」是**会合点落在站台外的直接、忠实的后果**。在 `R3RReservePathSequence()` 里截断/裁剪**在原理上无法修复本形态**（你想让它停进站台，但站台内一格都不在路径上、也没被预留过）。

**待验证的猜测根因（已登记，见 §27.6）**：`meet = tail + 2` 这个 offset 对"站台被占满、机车从站台外正线贴上来"的场景过大——机车鼻尖其实应该停在 **`1438`（站台端格外沿）甚至 `1437` 同一格**才能触发挂接；`+2` 让机车永远停在站台外 3 格，导致 `COUPLE-OK` 只有 1 次且可能不属于 veh=15。

### 27.6 已登记疑点 / 猜测根因（按铁律当场落盘）

- **D-1（疑点）**：`CPL-RES-OK total=44 booked=44 stop_safe=1`，44 格长预留一路斜线跨了 `y=1884→1874` 两条正线，每 tick 重复整段重订（`:1572` 起又从头订一遍）。这是"长预留"设计使然还是每次重算，未确认。
- **D-2（猜测根因，待验证）**：会合点 `meet` 的 `+2` offset 对"站台占满"场景偏大。验证判据：把 `meet` 改成站台端格（`1438`/`1437`）后，`CPL-BEST tile` 应落在站台外沿 ≤1 格内、`COUPLE-OK loco=15` 应出现；反判据：非站台场景（`veh=75` 类）的 `meet` 一字不变。
- **D-3（疑点）**：`COUPLE-OK` 仅 1 次，需确认 `loco=15` 是否出现过；若从未成功，则"机车停在站台外"确实是挂接失败的直接原因（而非仅观感问题）。

### 27.7 下一步（第 516 轮待做）

1. 定位 `CPL-SNAP` / `meet` 的计算点（不在 `yapf_rail.cpp` 的预留段），核 `tail + 2` 的来历与站台场景应有的值；
2. 若确认 offset 是病根 ⇒ 修 `meet` 计算（**不要**再往 `R3RReservePathSequence()` 里加判据）；
3. 复测判据：`CPL-SNAP meet=` 落在站台 `[lo,hi]` 内或其外沿 1 格内、`CPL-BEST tile` 随之、`COUPLE-OK loco=15` 出现、预留末格不再落在站台外。

### 27.8 定位到上游后的新发现（第 516 轮，取证完成）

**`meet` 的来源（`src\pathfinder\yapf\yapf_destrail.hpp:643 R3RComputeMeetTile()`）**

```cpp
static TileIndex R3RComputeMeetTile(TileIndex near_tile, TileIndex far_tile)
{
	...
	const TileIndexDiff step = (near_c > far_c) ? delta : -delta;
	TileIndex cur = near_tile;
	for (int guard = 0; guard < 64; ++guard) {
		const TileIndex next = cur + step;
		if (IsCompatibleTrainStationTile(next, cur)) { cur = next; continue; }
		return next;                    // next 已经是站台外第一格
	}
	return INVALID_TILE;
}
```

现场代入：`near_tile=1437,1874`（尾）、`far_tile=1424,1874`（头）、`axis=X`、`step=+1`。
`1438` 仍兼容（是站台格）⇒ `cur=1438`；`1439` 不兼容 ⇒ **`return 1439`**。

⇒ **`meet=1439` 是这段代码按设计的正确输出**：站台实际覆盖到 `1438`（注释亦写明"车底自己的站台整格预留覆盖到站台近端末格（现场 1438）"），`1439` 是站台外第一格。
⇒ **D-2（`+2` offset 是病根）当场证伪**：不存在 `+2`，是"走到站台外第一格"，站台长 15 格（`1424..1438`）时结果必然是 `1439`。

**新发现 E-1（硬缺陷，比 A 方案更贴近现象）：会合点格 `tg` 自己从未被预留。**

- `:2087 CPL-RES-OK veh=15 total=10 booked=10 … tiles=1449,1874/0;…;1440,1874/0` ⇒ `steps` 只有 10 格，**末格 1440**；
- `:2082-2085 RESV-SELF-AIR veh=15 tile=1443..1440,1874` ⇒ 订到 1440 为止；
- 全日志 `RESV-SELF-AIR … tile=1439,1874` **零行**（74 行全查过，`1439` 一次都没出现）。

⇒ 递归帧 `cur == tg` 时 `return true` 且不入列、不订自己，而 `R3RReservePathSequence()` 的循环只遍历 `steps` ⇒ **`tg=1439` 这一格始终没有预留位**。PBS 语义下机车最多开到"最后一格已订"（1440）就停，**永远差一格进不了 `tg`**，与"`COUPLE-OK` 只有 1 次"吻合。

⇒ 因此"预留末格在站台外"有两层，且**都不是预留函数里的越界判据能治的**：
1. 几何层：站台被 27 节车底占满到 `1438`，`meet` 合法地只能取 `1439` ⇒ 末格必然在站台外（`1439`，正常情况下）；
2. 缺陷层：`1439` 连预留都没有 ⇒ 实际末格退到 `1440`（站台外**第二**格），机车到位差一格。

**E-2（待验证）**：若补订 `tg` 后机车能开到 `1439` 并触发挂接，则"末格超出站台"应缩小为恰好"站台外第一格"，属物理必然、非缺陷。

### 27.9 口径分歧点（必须由玩家确认，否则又会白跑一轮构建）

当前代码事实决定了两种完全不同的修法：

- **若玩家诉求是"机车要能开到会合点/挂上"** ⇒ 修 E-1（把 `tg` 纳入预留，一行补订）；
- **若玩家诉求是"预留末格必须落在站台格内（`1424..1438`）"** ⇒ 这与 `R3RComputeMeetTile()` 的既有口径冲突：站台被车底占满时，机车在物理上不可能进入站台格，必须改的是"车底让位/挂接端重新定义"，不是预留边界。

问玩家 Q-516-1、Q-516-2（见回复正文）。

## 28. 第 517 轮：玩家三条澄清（决定性口径，直接推翻 §27 的读数方向）

### 28.1 玩家原话（2026-11-26）

> 「你又忘记前提：我澄清，这绝对不是地图几何的问题。此外，1439格是信号灯格，有双向可通过PBS信号灯。还有，超出站台的预留是从1424超出，超出的格有1423-1419，但这绝对不是地图几何的问题」

### 28.2 必须当场作废的旧表述

1. **作废 §27.5 / §27.9 的「物理必然论」**：此前写"站台被 27 节车底占满到 1438 ⇒ 机车物理上不可能进入站台格 ⇒ 末格落在站台外是物理必然"。玩家两次明令否定（"这绝对不是地图几何的问题"），该前提不得再作为结论依据。
2. **作废「超界发生在 hi 侧」的读数方向**：§27.4 全程盯的是 `1440..1457`（机车自己从 `1473,1884` 斜线下行过来的**来路**）。玩家指出真正的超界在 **lo 侧**：`1424` 之外再铺 `1423,1422,1421,1420,1419` 五格。**来路 ≠ 超界**，两者是不同对象，之前整章判据都建立错了对象上。
3. **`1439` 的定性更正**：`1439` 不是"站台外一格空地"，它是**信号灯格，且是双向可通过的 PBS 信号灯**。⇒ 机车在物理与信号语义上都可进入/停在 `1439`，"会合点在站台外"不能当借口；`R3RComputeMeetTile()` 返回 `1439` 本身也不是缺陷。

### 28.3 由此产生的取证目标（第 517 轮）

- 谁在 `1423..1419` 上写了预留：查 `RESV-SELF-AIR` / `SET-RAIL` / `RP cur=` 中 x∈1419..1423 的行，定位 `veh=` 与调用来源（`R3RReservePathSequence` 还是 `ExtendTrainReservation` 还是普通 YAPF 预留）。
- 该段路径的 `tg`（终点）到底在哪：若 `tg` 在 `1424` 或更右，则 `1423..1419` 属**路径回溯越过终点**；若 `tg` 本身在 `1419` 一带，则病灶在终点选择（会合点/站台参考点算错），不在预留函数。
- 与 E-1 的关系：E-1（`tg=1439` 格自身未入列、未订）仍成立且独立；但与本次超界不是同一段路径，须分别处置。

### 28.4 取证结果（`build\_r517a.txt`，取自 `build\R3R_debug.log`）——**超界根因 = `FSCP` 把挂接停位定到 `1419,1873`**

**E-3（硬缺陷，直接对应玩家观察）**：`veh=15` 的挂接寻路终点（`oend`）是 **`1419,1873`**，不是会合点 `1439,1874`，也不是站台端格。

原文锚点（`_r517a.txt` 行号 → 日志行号）：

| 行 | 原文（节选） |
|---|---|
| `13` → `:1669` | `FSCP-RES tile=1447,1874 tk=0 owner=15 otile=1474,1884 odir=1 ospd=39 ostop=0 ord=16 oroi=1 **oend=1419,1873** oendtd=0 othru=1 octgt=0 ocplok=0 cpl=1474,1884` |
| `14` → `:1670` | `FSCP-REACH tile=1447,1874 veh=15 ord=16 roi=1 pos=1474,1884 dir=1 spd=39 stopped=0 seg=1 cplok=0 ctgt=0 **end=1419,1873** endtd=0 isGtr=1 cpl=1474,1884` |
| `15` → `:1671` | `FSCP-CPL tile=1447,1874 veh=15 pos=1474,1884 dir=1 spd=39 **end=1419,1873** endtd=0 look=0 real=1` |
| `18..29` → `:1893..2025` | 同一 `end=1419,1873` 随 `pos` 从 `1460,1875 → 1457,1874 → 1456,1874 → 1451,1874` 逐帧推进，**`end` 恒定不变** |

读法：`end` 是挂接寻路最终认定的"安全停位"，`pos` 是机车当前位置。机车从 `1474,1884` 一路向站台推进，而它认的目标终点始终是 `1419,1873`——**在站台 lo 端（1424）之外 5 格、且跨到相邻平行线 `y=1873`**。预留忠实沿该终点铺出，于是在 `1424` 之外继续订 `1423,1874 → 1421,1873 → 1420,1873 → 1419,1873`。

**排除项（避免误判为 `veh=75` 的账）**：`_r517a.txt:1-12` 的 `TPR-WALK / RESV-HAVERES / RESV-SIGPASS / RPFAIL / CPLRES` 全是 **`veh=75` 自己**（`order=6`、`real=2`、`spd` 27→69→80 在真实行驶）的正常路径预留，属合法。超界的账只落在 `owner=15` 的 `FSCP` 一条线上。

**与 §27 的关系**：§27.4 盯的 `1440..1457` 是 `veh=15` 从 `1473,1884` 下来的**来路**（向站台推进的必经段），不是超界；真正的超界在 `1424` 之外的 **lo 侧**（`1423..1419`），由此前从未被纳入判据视野。⇒ 第 515 轮的三条判据（全部围绕 `[lo,hi]` 内侧与 hi 外沿）在对象上就选错了。

**下一步**：定位 `FSCP`（`FindSafeCouplePositionProc` / `FSCP-RES/FSCP-REACH` 探针）为何把 `end` 算成 `1419,1873`——是"沿站台轴向外找安全位"时方向取反（应为 +x 去 `1439`，实际 -x 去 `1419`），还是 back-walk 的 `best` 被 station 段"到站即安全"判据误取。

### 28.5 §28.4 的读法订正（源码核对，`yapf_rail.cpp:486/533`）

读完源码，`oend` / `end` 字段的语义必须正名：

```486:487:src/pathfinder/yapf/yapf_rail.cpp
						Train *oh = const_cast<Train *>(owner->First());
						const PBSTileInfo oend = FollowTrainReservation(oh, nullptr);
```

- `FSCP-RES ... owner=N ... oend=x,y`：`oend` = **占位者 N 自己当前预留的末端**（`FollowTrainReservation(owner)` 走到的末端），**不是**询路车的挂接目标。
- `FSCP-REACH ... veh=N ... end=x,y`：`end` = 该链自己预留的末端（同一含义）。

⇒ **订正后的结论（比 §28.4 更强、更贴近玩家观察）**：

`FSCP-RES tile=1447,1874 owner=15 ... oend=1419,1873 othru=1 octgt=0` 说明 **`veh=15` 自己的预留已经一路延伸到 `1419,1873`**，而 `1447,1874` 这一格上那条预留位的主人正是它自己（`owner=15`、`othru=1`、`octgt=0`＝它不是挂接目标而是询路车本身）。全序列 `:1893..2025` 的 `end=1419,1873` 只是随 `pos` 前移而复述同一个末端。

⇒ 玩家看到的"从 `1424` 超出到 `1423..1419`"就是**这条预留的末段**：它在 `1455,1874` 一带的岔口拐进北侧平行线 `y=1873`，沿 `1423,1874 → 1421,1873 → 1420,1873 → 1419,1873` 铺过去，末端停在站台 lo 端之外 5 格。

⇒ 因此病灶**不是**"预留函数越界判据写错"，也**不是**我上一轮猜的"`meet` offset 偏大"，而是：**`veh=15` 的挂接预留整体走错了线（进了 `y=1873` 平行线），且终点落在 `1419,1873`**。这一形态与第 470/471 轮 `1455,1874` 岔口备忘登记的"预留拐进北侧平行线 `1421/1420/1419,1873` 且不撤销"**同一现象**，须按那条线的既有取证继续。

### 28.6 决定性命中：`1423..1419` 五格的主人实测是 `veh=75`，不是 `veh=15`

逐行核对 `_r517a.txt`（左=该文件行号，右=日志行号）：

| 格 | 日志 | 原文 | 主人 |
|---|---|---|---|
| `1423,1874` | `:1258` | `RESV-HAVERES veh=75 tile=1423,1874 enterdir=0 res=0x1 tracks=0x1 spd=27 real=2 order=6 …` | **75** |
| `1422,1874` | `:1259` | `RESV-HAVERES veh=75 tile=1422,1874 … res=0x1 … spd=69` | **75** |
| `1421,1874` | `:1260` | `RESV-HAVERES veh=75 tile=1421,1874 … res=0x10 tracks=0x10 spd=80` | **75** |
| `1421,1873` | `:1261` | `RESV-HAVERES veh=75 tile=1421,1873 enterdir=3 res=0x20 tracks=0x20 spd=80` | **75** |
| `1420,1873` | `:1263` | `RESV-HAVERES veh=75 tile=1420,1873 … res=0x1` | **75** |
| `1419,1873` | `:1265` | `RESV-HAVERES veh=75 tile=1419,1873 … res=0x1` | **75** |

写位者也有独立的正面证据：`_r517a.txt:16` → 日志 `:1816`

```
RESV-WATCH CHG-RAIL tile=1421,1873 track=4 phase=yapf-resv actor=75 head=75
```

`actor=75 head=75` ⇒ 该位由 `veh=75` 在 `phase=yapf-resv` 阶段写入。而 `veh=75` 是**普通列车**：`order=6`（`OT_GOTO_STATION/DEPOT`，非 16）、`real=2`、`spd` 从 `3 → 27 → 69 → 80` 真实加速，`CRT veh=75 order=6 … found=1`、`RPFAIL veh=75 … origin=1419,1873 tile=1402,1870`、`CPLRES veh=75 … destTile=1402,1870` ⇒ 它正沿 `1424,1874 → 1402,1870` 正常出站行驶，这 5 格是**它自己的合法 PBS 前行预留**。

### 28.7 由此得到的新疑点 **REG-517-A**（必须先验，否则又会改错层）

`FSCP-RES ... oend=1419,1873` 里的 `oend` 来自 `FollowTrainReservation(oh, nullptr)`（`yapf_rail.cpp:486-487`）。该函数沿"有预留"的格子连续前推，**不校验预留主人**，而 `1423..1419` 恰好是 `veh=75` 的连续预留 ⇒ **`oend=1419,1873` 极可能是"借道 75 的预留位走到断点"的产物，而不是 `veh=15` 自己铺到了 1419**。

期望：若 `oend` 是借道结果，则应能在日志里找到 `15` 在 `x<1424` 的写位记录（`actor=15` 的 `SET-RAIL`/`CHG-RAIL`）。
实测（待做）：本轮尚未筛 `actor=15` 的写位记录。
为什么存疑：若 15 并无 `x<1424` 的写位，则玩家看到的"从 1424 溢出"在账面上属于 75 的正常预留，则 KI-684 的对象需要重新确认（可能是视觉层：两条预留线在预览里连成一条）。

**REG-517-A 验证结果（成立）**：`build\_r517c.txt` = 全日志 `actor=15` 的写位记录，**仅 4 条，无一条落在 `x < 1424`**：

```
:1523 RESV-WATCH SET-RAIL  tile=1459,1875 track=0 phase=yapf-couple actor=15 head=15
:1592 UNRES-RAIL           tile=1447,1874 track=2 phase=yapf-fscp   actor=15          （释放）
:1598 RESV-WATCH CHG-RAIL  tile=1457,1874 track=4 phase=yapf-couple actor=15 head=15
:1599 RESV-WATCH CHG-RAIL  tile=1457,1874 track=5 phase=yapf-fscp   actor=15 head=15
```

⇒ `veh=15` 从未在 `1423..1419` 写过任何预留位 ⇒ `oend=1419,1873` 确定为 `FollowTrainReservation` **借道 `veh=75` 预留位**的产物（该函数不校验主人）。**`1423..1419` 这五格在账面上属 `veh=75` 的合法出站预留**，不是 `veh=15` 越界。

⇒ 因此"从 `1424` 溢出 5 格"这条现象，需要玩家再确认一次观察对象：它出现时，附近是否有一台普通列车刚从站台向西驶离（本日志里就是 `veh=75`，`order=6`、`spd 3→27→69→80`、`destTile=1402,1870`）。若玩家看到的确实是这台车的预留，则该段属正常 PBS 行为；若玩家看到的是**挂接车**的预留线，则说明存在日志未覆盖的第二条写位路径（`ReserveTrackUnderConsist` / `ExtendTrainReservation` 不走 `RESV-WATCH`），需补探针。

**已确认的另一半（不依赖 REG-517-A）**：`veh=15` 的挂接路径**确实走在 `y=1873` 平行线上**，而非 `y=1874` 站台线——

```
:1666 CPL-BACK pPrev=1439,1874 td=0 parent=1442,1874 ptd=0
:1667 CPL-BACK pPrev=1442,1874 td=0 parent=1448,1873 ptd=0
```

即回溯链为 `… → 1448,1873 → 1442,1874 → 1439,1874(best)`。同时 `:1668 FSCP tile=1447,1874 fail=tryReserve td=2 want=2 type=1 tbits=0x5 res=0x1 plain=1 self=0` ⇒ 在 `1447,1874` 因"已有一条 `X` 方向自留位"而订不到 `want` 的方向，`CPL-SAFE-FAIL veh=15 tile=1442,1874` 紧随 ⇒ **这正是"挂接预留订不满、与 `1440..1449` 那 10 格之间出现断口"的机制**。它与 E-1（`1439` 自身不入列）叠加，构成"机车到不了会合点、预留线看起来乱串"的两处独立成因。

### 28.8 **§28.6 / §28.7 的"属 veh=75"结论撤回**（REG-517-B，2026-11-26 源码核对）

**结论句：`RESV-WATCH` 对非关注格做了「按 (kind, phase) 全局聚合」，所以"`actor=15` 只有 4 条写位"根本不能反证"`veh=15` 没在 `1423..1419` 写过位"。§28.6/§28.7 据此下的归属判定作废，降级为"未定"。**

源码锚点（`src\pbs.cpp:249-330`，`R3RResvWatchLog()`）：

```263:275:src/pbs.cpp
	const bool watched = R3RIsWatchedResvTile(tile);
	if (!watched && R3RResvPhaseIs(r3r_resv_phase, "misc")) return;
	...
	if (watched) {
		R3RDbgWrite("RESV-WATCH %s tile=%d,%d track=%u phase=%s actor=%d head=%d\n", ...);
		return;
	}
```

```301:330:src/pbs.cpp
	const uint64_t pair = (uintptr_t)kind ^ ((uintptr_t)r3r_resv_phase << 3);
	R3RWatchAgg &agg = unwatched_agg[pair];
	agg.n++;
	...
	if (agg.n == 1) {
		R3RDbgWrite("RESV-WATCH %s tile=%d,%d ... actor=%d head=%d\n", ...);
	} else if (agg.n % R3R_WATCH_SUM == 0) {   /* R3R_WATCH_SUM = 256 */
		R3RDbgWrite("RESV-WATCH-SUM %s phase=%s n=%u tiles=%u ... last=%d,%d actor=%d head=%d\n", ...);
	}
```

读法（三条，逐条否定 §28.6/28.7）：

1. **只看聚合键**：`pair = kind ^ (phase << 3)`，**完全不含 tile、不含 actor**。所以同一 `phase` 下的第 2、3、…、255 次 `SET-RAIL` 写位**一行都不打**。`yapf-couple` / `yapf-fscp` 这两个 phase 的写位，全进程只有"第一条"明文可见。
2. **聚合表是 `static`**：`unwatched_agg` / `unwatched_seen` 是函数内 `static`，**进程生命周期内永不重置**。所以哪怕是新的一场、新的路径，只要同 phase 出现过一次，后续照样静默到第 256 次才来一行 SUMMARY，而 `SUMMARY` 的 `actor/last` 取的是"第 256n 次"那一刻的那一格、那一台车——与前面 255 格无对应关系。
3. **`_r517c.txt`（`actor=15` 的 4 条）不是"15 的全部写位"**：它是"明文行里恰好 actor=15 的那 4 行"。⇒ "无一落在 `x < 1424`"这个否定命题**从日志里无法验证**，它与"被聚合掉了"完全不可区分。

**连带要撤回的表述**：
- §28.6 表格里 `RESV-HAVERES veh=75 tile=1423..1419` 六行的读法（"主人=75"）只能支持"**75 的寻路在这些格上读到'已有预留位'**"——`RESV-HAVERES` 是读点，证明"该位当时已被订"，**不证明"是 75 写的"**（`FollowTrainReservation` 本就不校验主人，§28.5 自己写过这一点）。
- `:1816 RESV-WATCH CHG-RAIL tile=1421,1873 track=4 phase=yapf-resv actor=75 head=75` 这一行是真实的 75 写位，但它只是"明文可见的**第一条** `yapf-resv` 写位"（`tile` 恰好是 1421,1873 属巧合），不能外推为"这五格全是 75 的"。

**因此玩家观察重新变得完全可信**：`1423..1419` 是否有一条是 `veh=15` 写的，现有日志**无法判否**。`veh=15` 的挂接路径确实走在 `y=1873` 侧（`CPL-BACK` 链），而 `R3RReservePathSequence` 的站台锚点搜索（`:777-818`）在找不到配对伙伴锚点时会在 `steps.back()` 之外 **±8 格搜索 `r3r_plat_ref`** —— 这条分支有没有把预留铺到站台 lo 端之外，必须用**新探针**取证，不能靠现有日志。

**新探针要求（REG-517-B 待验证判据）**：
- 对 `x <= 1423 && y == 1873/1874` 这一带**加进 `R3RIsWatchedResvTile()` 关注集**（关注格走 `:271` 的直通分支，永远明文、不被聚合）；
- 或给 `RESV-WATCH` 增加"`actor == 当前询路车`"时强制明文的旁路。
- 取证目标：`veh=15` 在 lo 侧是否留下过 `SET-RAIL`/`CHG-RAIL`；有 ⇒ KI-684 的超界对象就是挂接预留（回到我方原判方向）；无 ⇒ 才能把 §28.6 的"属普通列车"结论重新立起来。

**判据优先级**：E-1（`tg` 不入列、不预订）**不受本条影响**，它的证据是路径清单而非地图写位（`CPL-RES-OK ... 1440,1874/0` 末格 + `RESV-SELF-AIR tile=1439,1874` 零行 + `:2200 if (cur == tg) return true;` 源码），仍然成立。

### 28.9 第 517 轮实施记录（E-1 修复 + REG-517-B 探针，已编译通过）

玩家裁定：`1423..1419` 若是 `veh=15` 写的则属挂接机车，若不是也应是站台上那列 `veh=18` 写的，**不可能是过路的 `veh=75`**（75 在日志里只留 `CHG-RAIL tile=1421,1873 track=4`，是 `y=1873` 平行线上的过路动作）。据此直接实施 B（修 E-1），并把 A（关注带）一并编入。**已执行，`ninja` 退出码 0，`openttd.exe` 时间 `2026-11-26 03:20:53`，仅重编 `src\pbs.cpp` 与 `src\pathfinder\yapf\yapf_rail.cpp`。**

**改动 1（B，E-1 修复）`src\pathfinder\yapf\yapf_rail.cpp` 的 `rp_impl`，原 `:2200 if (cur == tg) return true;`**

现在到达会合点时先入列：

```cpp
if (cur == tg) {
    ...
    if ((cur != st || depth != 0) && !IsRailStationTile(cur) && !IsRailDepotTile(cur)) {
        r3r_resv_path.push_back(R3RCoupleStep{cur, cur_td});
    }
    return true;
}
```

口径与既有规则严格一致：站台/车库格仍不订（预留止于平台，站台跳格分支 `:2228` 负责整段），起点格 `(st, depth 0)` 仍不订；其余（本案 `tg=1439,1874` 是普通信号格）**像任何一格一样被 `R3RReservePathSequence()` 预订**。副作用符合预期：
- `SetReservationTarget()` 的终点现在正好是 `tg`（"预留止于会合点、不越过车底"的语义不变，只是把会合点本身补上了）；
- 若该位已在本车链名下 ⇒ `TryReserveRailTrack()` 走 `R3RReservationHeldBySelf()` 分支报 `RESV-SELF-AIR` 并返回成功（`src\pbs.cpp:591-600`），不会因"订自己的位"而失败；
- 若该位属**别人** ⇒ 预订在该格失败，`CPL-SAFE-FAIL veh=15 tile=1439,1874` 会明确出现，取代过去"默默少一格"。

**改动 2（A，REG-517-B 探针）`src\pbs.cpp`**

新增 `R3RIsOverrunBandTile()`：`1419 <= x <= 1423 && (y == 1873 || y == 1874)`，在 `R3RIsWatchedResvTile()` 里**先于**列表查询判断。关注格走 `R3RResvWatchLog()` 的 `:271-275` 直通分支，**永久明文、不进 `(kind, phase)` 聚合**，因此 `RESV-WATCH` / `RESV-SELF-AIR` / `UNRES-RAIL` / `RESV-WALK` 在这 10 格上都会逐条落日志。

**复跑后的判读（按优先级）**
1. `CPL-RES-OK veh=15` 末格是否变成 `1439,1874`，且 `COUPLE-OK` 是否在机车进入 `1439` 后触发 ⇒ 验 E-1 修复有效。
2. `RESV-WATCH ... tile=142x,187x ... actor=15` 是否出现 ⇒ 若是，KI-684 的超界对象确为挂接预留（我方原判方向坐实）；若全部是 `actor=18` ⇒ 站台内那列车的行驶/站台预留越界（`veh=18` 的车头朝向 lo 端，预留向站台外延伸若干格），属另一处成因；若两者皆无 ⇒ 需检查 `ReserveTrackUnderConsist()` / `ExtendTrainReservation()`（不走 `RESV-WATCH`）。
3. `CPL-SAFE-FAIL` 若出现在 `1439,1874`，说明会合点被**他人**占用，属"会合点选点"问题而非预留铺法问题。

### 28.10 第 518 轮：REG-517-B 定案 + 会合点差一格（本轮已修，编译 03:36:25）

**日志**：`build\R3R_debug.log`（第 517 轮 exe，`2026-11-26 03:26:31`，1666 行），同场景。

#### 28.10.1 REG-517-B 定案：写位者确为 `veh=15`，性质是**行驶 lookahead 预留**

关注带生效后，同一段 6 格被两台车先后写过，**track 序列完全相同**（`0,0,4,5,0,0`）：

```
1122-1127  RESV-WATCH SET-RAIL tile=1423,1874/0  1422,1874/0  1421,1874/4  1421,1873/5  1420,1873/0  1419,1873/0   phase=choose-track actor=75
1175-1198  RESV-WATCH CLEAR-RAIL（同样 6 格，phase=controller actor=75）
1469-1474  RESV-WATCH SET-RAIL tile=1423,1874/0  1422,1874/0  1421,1874/4  1421,1873/5  1420,1873/0  1419,1873/0   phase=choose-track actor=15
```

⇒ 玩家判断正确：**这次是 `veh=15` 写的，不是 75**。且 `phase=choose-track` 是 `ChooseTrainTrack()` 的**行驶预留**（`src\train_cmd.cpp:18317` 的 `R3RResvPhaseGuard r3r_phase("choose-track", ...)`），**不是挂接预留**（挂接是 `yapf-couple`）。

该预留的终点由两条独立读数钉死：

```
1495: FSCP-RES tile=1447,1874 tk=0 owner=15 otile=1474,1884 odir=1 ... ord=16 ... oend=1419,1873 oendtd=0 othru=1
1496: FSCP-REACH tile=1447,1874 veh=15 ... end=1419,1873 endtd=0 isGtr=1
```

`oend=1419,1873` / `isGtr=1` = **15 自己的预留终点是 1419,1873，比它当时的挂接目标更远**。KI-684 的"超界 5 格"就是这条行驶 lookahead 预留，与挂接铺法无关 —— 上一轮 E-1 的修法对它本来就没有作用面。

#### 28.10.2 E-1 修复已生效

```
1409: CPL-RES-SAFE-STOP veh=15 nth=44/45 tile=1439,1874 td=0 booked=45 goal=1
1411: CPL-RESERVE     veh=15 reserved=1 next=0 path=45 booked=45
```

`booked` 从 44 变 45，第 45 格正是 `tg=1439,1874`。E-1 关闭。

#### 28.10.3 仍未挂上：两个独立拦阻，主因是**会合点差一格**

**(甲) `CPL-SAFE-FAIL 1442,1874` = 15 自己的行驶预留挡住挂接路径**

```
1494: FSCP tile=1447,1874 fail=tryReserve td=2 want=2 type=1 tbits=0x5 res=0x1 plain=1 self=0
1495: FSCP-RES tile=1447,1874 tk=0 owner=15 ...
1498: CPL-SAFE-FAIL veh=15 tile=1442,1874
```

`1447,1874` 有 `X | LOWER` 两条轨（`tbits=0x5`）；15 的行驶预留占了 `X`（`res=0x1`），挂接回溯要订 `LOWER`（`want=2`）⇒ 交叉互斥。`self=0` 是因为 `yapf_rail.cpp:425-432` 的二次确认要求"本链车节真的站在该格"，而 15 的车头在 `1474,1884`。这是 §38 已知的自锁形态，暂不动（§38 已证伪简单放行会走错岔道）。

**(乙) 会合点 `1439` 与车底车尾 `1437` 之间隔着一个空格 `1438` —— 本场 `COUPLE-OK` 0 次**

```
1480: CPL-SNAP veh=15 near=1437,1874 meet=1439,1874 head=1424,1874 tail=1437,1874
1490: CPL-HEAD-STATIC veh=15 best=1439,1874 snap=1439,1874 meetvalid=1 meet=1439,1874 ... on=0
```

车底 18 占 `1424..1437`（`CPL-PAIRLOCK ... tgtTile=1424,1874 tgtLen=216`），车尾在 `1437`；`R3RComputeMeetTile()` 沿站台轴一直走到**站台外第一格**才返回，于是 `meet=1439`。15 停在 1439 时车头与 18 车尾 1437 相隔一整格 1438 ⇒ 钩头不相接 ⇒ 挂接永触发不了。这是本轮新增的、可直接确证的偏差（旧注释把 1438 当成"车底的站台整格预留"，但那是**空格**，不是车体）。

**修法（已实施）**：`src\pathfinder\yapf\yapf_destrail.hpp` 的 `R3RComputeMeetTile()` 改为只取「近端车格沿轴向背离车底的**一格**」（现场 = `1438`），不再一路走到站台外；该格不可通行（非铁轨/非法格）时返回 `INVALID_TILE`，调用方退回原「同排」口径，行为逐字不变。`ninja` 退出码 0，`openttd.exe = 2026-11-26 03:36:25`（重编 `yapf_rail.cpp`）。

**复跑判据**
1. `CPL-SNAP ... meet=1438,1874`、`CPL-RES-OK ... tiles=…;1438,1874` 末格 = 1438 ⇒ 修法生效；
2. 出现 `COUPLE-OK`（本场为 0）⇒ 挂接真正完成；
3. 若 `CPL-RES-OK` 的 `booked < total` 且末格停在 1439 ⇒ 1438 被车底整格预留拒绝，届时改从"挂接时收缩车底站台预留"入手；
4. `1423..1419` 是否仍有 `actor=15 phase=choose-track`：挂上之后 15 是**牵引整列出站**，该段属正常出站预留；若仍要求预留不得越过站台端点，则属另一件事（行驶 lookahead 截断），需要单独拍板。

## 29. 第 520 轮：玩家复报「KI-684 并没有修复成功，预留超出了站台」——以本备忘为基础重新定位（2026-11-26）

### 29.1 本备忘已定案、不得重复取证的部分

- **§28.10.1（定案）**：`1423..1419`（连同 `1422,1874` / `1421,1874` / `1421,1873`）六格 = `actor=15`、`phase=choose-track` 的**行驶 lookahead 预留**，终点由 `FSCP-RES ... oend=1419,1873` 与 `FSCP-REACH ... end=1419,1873 isGtr=1` 两条独立读数钉死。它**不是**挂接预留（挂接是 `phase=yapf-couple`），也**不是** `veh=75` 的账（§28.6/§28.7 已撤回，§28.10.1 用关注带逐条明文反证）。
- **§28.10.2（已修）**：E-1「`tg` 会合点格自身不入列、不预订」→ `booked 44→45`，已关闭。
- **§28.10.3(乙)（已修）**：`meet 1439→1438`（`R3RComputeMeetTile()` 改为只取「近端车格沿轴向背离车底的一格」）。
- ⇒ 本备忘的**未修项只剩 §28.10 复跑判据 4 那一条**："若仍要求预留不得越过站台端点，则属另一件事（行驶 lookahead 截断），需要单独拍板"。

### 29.2 与 `R3R_meet_tile_in_platform_round518_memo.md` 的分工

| 备忘 | 对象 | 关键探针 |
|---|---|---|
| 本文件（round509） | **预留轨迹**（谁把位铺到哪、末端在哪） | `RESV-WATCH` / `RESV-HAVERES` / `FSCP-RES` / `FSCP-REACH` / `oend` / `end` |
| round518 | **挂接寻路能否找到目标**（扫不扫得到会合点） | `CPL-SCAN` / `CPL-BEST` / `snap_meet_tile` / `rej[4]` |

两者只在 `meet=1438,1874` 这一点交叉：`1438` 是站台格 ⇒ ①让机车能真正贴上车尾（本备忘 §28.10.3 修法的目的）；②让 YAPF 因站台跳段扫不到该格（round518 的病）。round518 的方案 A 修的是 ②，**没有也不应该**修"预留越界"。

### 29.3 结论：第 520 轮复报的对象就是 §29.1 最后那条未修项

- 写位路径已定位：`src\train_cmd.cpp:18312 ChooseTrainTrack()`，其入口守卫 `R3RResvPhaseGuard r3r_phase("choose-track", R3RResvActorID(consist));`（`:18317`）。这是**行驶预留（path reservation / lookahead）**，与挂接单段预留 `R3RReservePathSequence()` 是两条不同代码路径。
- 第 514 轮 A 方案的站台硬边界（`EXT-STOP reason=off-platform`）挂在 `R3RReservePathSequence()`；第 518 轮 E-1 与 `meet` 两处修改也都在挂接侧。⇒ **三次修复都不在 `ChooseTrainTrack()` 这条路上**，这就是"修了三次仍未解决"的唯一解释。
- 与 round518 备忘 §13 的猜测根因 G-1「覆盖面不足」互相印证，且现在把 G-1 从"未定位"升级为"已定位到函数"。

### 29.4 待玩家拍板的口径（不同口径 → 完全不同的修法）

- **X-1「预留必须止于站台端点」**：在 `ChooseTrainTrack()` 的 lookahead 预留循环里加站台硬边界（与 `EXT-STOP` 同款判据），下一步要进入"站台端点之外、且不在目标站台内"的格即截断。风险：截断过短会让机车在站台外找不到路/刹停，须与 `ExtendTrainReservation()` 的补订配合。
- **X-2「只有挂接/等待车底的车才需止于站台」**：边界限定 `OT_GOTO_COUPLE` / `OT_WAIT_COUPLE` 的机车，普通列车出站不受限。风险：观感上仍会看到过路车越界——本日志里 `veh=75` 曾在**同一六格**写下逐字相同的 `track 序列 0,0,4,5,0,0`（§28.10.1）。

### 29.5 复测判据（X-1 落地后）

1. `RESV-WATCH ... tile=142x,1873/1874 ... actor=15 phase=choose-track` 不再出现 `x <= 1423`（末格 ≥ `1424`）；
2. `FSCP-RES ... oend=` / `FSCP-REACH ... end=` 的 x 不再是 `1419`；
3. `CPL-*` 与 `COUPLE-OK` 行为不回退（不得因截断导致挂接失败）；
4. 负判据：`veh=75` 类普通出站列车在**非站台区段**的预留长度逐字不变。

本轮**零源码改动、零构建**。

## 30. 第 521 轮：玩家拍板口径 X-2（精确化）——挂接寻路向站台铺预留时不调 lookahead

### 30.1 玩家原话（2026-11-26）

> "挂接寻路在准备往站台铺设预留的时候应当不调用 lookahead 预留，这是我的口径。"

### 30.2 口径拆解（作为本轮实现的唯一验收标准）

- **适用对象**：当前订单是 `OT_GOTO_COUPLE` 且**非车库型**（`GetCoupleIsDepot() == false`）的机车链。
- **适用时机**：该链**正在向站台（会合点所在站台）铺设预留**的那一次动作。
- **要求**：这次铺设**不得调用 lookahead（行驶前瞻）预留**；预留只铺到本次动作必需的长度，不得因前瞻而把预留延伸到站台端点之外（本备忘 §28.10.1 的 `1423..1419,1873` 六格就是该越界）。
- **明确不做**：X-1（对全部列车、全部站台一律截断）**被否**；普通 `OT_GOTO_STATION` / `OT_GOTO_WAYPOINT` 列车的长预留必须逐字不变。

### 30.3 与 §28.10.1 的对应关系

- 越界六格的写位链路：`ChooseTrainTrack()`（`src\train_cmd.cpp:18312`，守卫 `:18317`）→ `if (do_track_reservation)` → `ExtendTrainReservation()`（调用点 `:18685`，定义 `:17221`）。
- 现成的 `CTTF_NON_LOOKAHEAD`（定义 `:104`，使用点只有 `:22260`）语义是"**清掉已有 lookahead 状态**"（`consist->lookahead.reset()`，`:18663-18667`），**不会阻止** `ExtendTrainReservation()` 重新铺一条长预留 ⇒ **不能**直接拿该 flag 当口径实现。

### 30.4 确定修法（已实现）

**落点**：`src\train_cmd.cpp` 的 `ExtendTrainReservation()`（定义 `:17221`），在 `CFollowTrackRail ft(v);` 之后、`_r3r_loop_memo` 回放之前插入早退闸门。

**判据（四条同时成立才收口）**：
1. `v->current_order.IsType(OT_GOTO_COUPLE)`；
2. `!v->current_order.GetCoupleIsDepot()`（车库型挂接不在此列，库内不需要站台预留）；
3. `IsRailStationTile(origin.tile)`（**本车已经站在站台格上**，即"准备往站台铺预留"的那一刻；区间行驶阶段 origin 不是站台格，照旧前瞻）；
4. `R3RGetCouplePairPartner(v) != nullptr`（已有配对锁，说明确实是要去挂这一列车底，不是路过）。

**动作**：`return PBSTileInfo(origin.tile, origin.trackdir, true);` —— 路径有效但**不再扩展**，本次调用一格前瞻预留都不铺；站台内的到会合点预留由挂接寻路 `DoTrainCouplePathfind()` → `R3RReservePathSequence()` 负责，脚下保护由 `ReserveTrackUnderConsist()` 负责。附 `EXT-EXIT kind=couple-stn-nolook` 只读探针（复用 `R3REDGE_EXTEXIT`）。

**为什么不复用现成的 `r3r_couple_no_lookahead`**：该变量（`:18533`）语义相反——它是"挂接订单 + 非车库 + **当前没有** lookahead 时不许吃 `early-haverserved` 捷径，强制走 `ExtendTrainReservation()` 去把前瞻填上"。正是这条把挂接机车的前瞻**强制**铺出来，才让 `1423..1419,1873` 六格被缓存进 lookahead 并长期可见。本轮闸门排在该调用之前，两者不冲突（填不上前瞻时 `r3r_couple_no_lookahead` 恒真、恒不走捷径，行为自洽）。

### 30.5 风险与待验证

- **V-1（性能/爬行）**：收口后挂接机车在站台区没有前瞻，`origin` 只推进到脚下，可能变成逐格重算。可接受（它本来就该停），但复测要看帧率与 `RESV-HAVERES nolook=1` 是否异常刷屏。
- **V-2（误伤）**：若某挂接机车的车底在**另一个**站台，它站在本站台格上会被收口困住。判据 4（配对锁）用于排除这种情形——但配对锁是否总在进站前建立，需现场确认（**疑点 REG-521-A**）。
- **V-3（交互）**：第 406 轮 `EXTEND-GOAL-EXEMPT`（`:17354-17398`）在本闸门之后才可能触发；闸门命中时该豁免不再需要，未命中时逐字不变。

### 30.6 构建自证（第 521 轮，2026-11-26）

- **工具坑（本轮踩到，必记）**：`cmd /c "(findstr /c:"xxx" file > out)"` 这类**嵌套双引号**会让外层 `cmd /c "..."` 在第二个 `"` 处提前收尾，findstr 实际执行成残缺命令并**静默产出空文件** ⇒ 会误判"源码里没有这个字面量"。正确写法：`findstr /m /c:xxx file > out`（引号内只留一层，或改用 `search_content` 工具）。
- 第一次核对时我曾据空文件怀疑改动丢失，实测 `src\train_cmd.cpp:17237-17250` 闸门**在位**；真正的状态是 **obj 陈旧**：`build\CMakeFiles\openttd_lib.dir\src\train_cmd.cpp.obj` = 2026-11-25 21:52，而 `src\train_cmd.cpp` = 2026-11-26 04:38 ⇒ 02:43/03:59 那两次构建都**没有**编译本文件，`build\openttd.exe`（03:59）不含本轮改动。⇒ **判据必须以"obj 晚于 .cpp 且 exe 晚于 obj"为准，不能只看构建脚本 returned 0。**
- 重建结果：`R3R_fullbuild.cmd inc detach` ⇒ guard `incremental is safe` ⇒ `[2/3] train_cmd.cpp.obj`（obj @04:51）→ `[3/3] Linking CXX executable openttd.exe`；`build\R3R_incbuild.done` = `EXIT_CODE=0`；`build\openttd.exe` @**2026-11-26 04:53（54 409 728 B）**晚于 obj 晚于 src(04:38)；`findstr /m /c:couple-stn-nolook build\openttd.exe` 与 `/c:EXT-EXIT` 均命中 ⇒ 新二进制确定含本轮闸门；`read_lints` 0 条。

### 30.7 复测判据（X-2 落地后，替代 §29.5）

1. **主判据（越界消失）**：挂接机车停在站台格、订单 `OT_GOTO_COUPLE` 期间，`RESV-WATCH ... actor=15 phase=choose-track` 不再出现 `x <= 1423`；`RESV-EXT-PRE veh=15` 轨迹的末行不再是 `1440,1874` 之后继续西推（本轮闸门命中后**根本不应有新轨迹**）。
2. **闸门自证**：日志出现 `EXT-EXIT veh=15 kind=couple-stn-nolook origin=…/… order=16`（`R3REDGE_EXTEXIT` 节流，同车同格每次状态变化才一行），且其 `origin` 落在站台格内。
3. **挂接不回退**：`CPL-SCAN found=1` / `CPL-BEST tile=1438,1874 cost=0` / 最终 `COUPLE-OK` 照旧（第 519 轮已达成，不得因本轮截断而丢失）。
4. **负判据（X-1 未被误实现）**：普通 `OT_GOTO_STATION`/`OT_GOTO_WAYPOINT` 列车（如 `veh=75`）在**非站台区段**的 `RESV-HAVERES` 轨迹、格数、`track` 序列**逐字不变**；`RESV-WATCH ... phase=choose-track actor=75` 仍可越 `1423..1419`（这是 X-2 口径下**允许**的观感残留，属预期而非回归）。
5. **负判据（车库型不受影响）**：库内挂接（`GetCoupleIsDepot()==true`）不出现 `kind=couple-stn-nolook`，`CPL-*` 逐字不变。
6. **性能**：不得出现 `RESV-HAVERES nolook=1` 异常刷屏或帧率下降（V-1）。

## 31. 第 521 轮复测（2026-11-26，玩家回传 `build\R3R_debug.log`）：行为变了但没修好，且闸门**根本没命中**

### 31.1 玩家原话（口径，不改）

> 「见 build/R3R_debug.log，KI684 行为出现了改变，但预留问题依然存在，具体表现为列车又不一次性铺预留了，变为边走边铺，同时中间出现了预留紊乱」

### 31.2 日志身份

- `build\R3R_debug.log` = **406 330 B @2026-11-26 05:05:12**，产生者 = 第 521 轮 exe（`build\openttd.exe` @04:53，含闸门）⇒ 采样有效。

### 31.3 硬事实（`findstr` 全量计数，逐条可复现）

| 探针 | 条数 | 含义 |
|---|---|---|
| `EXT-EXIT` | **0** | ~~本轮闸门一次都没命中~~ **假阴性，见 §31.6**：该标签被 `R3RDbgTagAllowed()` 丢弃，0 条不能证明任何事 |
| `RESV-NOLOOK-PASS veh=15 … nolook=1` | 多行 | veh=15 长期处于「挂接 + 非车库 + **无 lookahead**」状态 |
| `RESV-HAVERES veh=15 … nolook=1 look=0` | **235** | 每 tick 都在重算、且没有前瞻可吃 |
| `RESV-EXT-PRE veh=15 … doRes=0` | 多行 | `do_track_reservation == false` ⇒ **`ExtendTrainReservation()` 根本不会被调用** |
| `RESV-NOBOOK veh=75 … reason=depot-paired` | 1 | 另一条门禁（第 127 轮 `parked-waiter` 的同族）命中 veh=75 |
| `COUPLE-OK loco=75 rear=62 consist=18 co=1 real=1 type=1 tx=1505 ty=1875` | 1 | **挂接本身成功**（场景与上一轮不同：目标在 1505,1875 一侧） |
| `RESV-ARB tile=1458,1875 a=veh18 wait=1 b=veh15 wait=0 picked=veh15` | ≥1 | 整格位**仲裁把 veh18（等待车底，wait=1）的位判给了 veh15** |
| `RESV-SIGPASS veh=15 tile=1456,1874 … ownRes=2 coType=16 real=1 db=0` | ≥1 | 走到信号分支；该格预留属**别人**（ownRes=2） |
| `CPL-NEXT … wholeOwner=18` | ≥1 | 1456,1874 的**整格位属于 veh=18** |

### 31.4 结论（先定性，不猜修法）

1. ~~**闸门未命中**~~ **本条已推翻，见 §31.6**：`EXT-EXIT` 零条是日志白名单造成的假阴性（`r3r_perf.h:322-405` 只放行 `RESV`/`UNRES`/`EXTEND`/`LOOP`/`R3R-RES`/`COUPLE`/`CPL`/…，`EXT-` 前缀不在内，而 `EXTEND` 的 6 字节比较不匹配 `EXT-EXIT`）。**闸门极可能已经命中**，证据见 §31.6 的三次 `RESV-EXT-DEST`。
2. **「边走边铺」的机制已定位**：`RESV-EXT-PRE … doRes=0` ⇒ `do_track_reservation=false` ⇒ `ExtendTrainReservation()` 不被调用 ⇒ lookahead 永远填不上 ⇒ `r3r_couple_no_lookahead=true` ⇒ 不许吃 `early-haverserved` 捷径（`:18533`）⇒ 每 tick 重新 `ChooseTrainTrack()`，只订脚下 1~2 格 ⇒ 玩家看到的「边走边铺」。这是一个**自锁环**：无前瞻 → 不吃捷径 → 重算 → 但不铺前瞻 → 仍无前瞻。
3. **闸门为何不命中 = 待验证（REG-521-B）**：四条判据中最可能不成立的是 `IsRailStationTile(origin.tile)`（机车此刻可能站在站台**外**的区间格，如 1455..1458,1873~1875 一带）或 `R3RGetCouplePairPartner(v) != nullptr`（配对锁未建立）。日志里 veh=15 的 `RESV-EXT-PRE` 轨迹 tile 为 1458,1875 → 1457,1875 → 1457,1874 → 1456,1874 → 1455,1874 → 1455,1873，**需要逐格核对是不是站台格**。
4. **「预留紊乱」的具体形态**：`RESV-ARB … a=veh18 wait=1 b=veh15 wait=0 picked=veh15` ⇒ 行驶中的 veh=15 把**等待车底 veh=18 的整格站台位**抢走；同时 `CPL-NEXT … wholeOwner=18` 说明挂接侧仍认为那是 veh=18 的位。两侧认知不一致 = 紊乱。

### 31.5 待办（下一步，按优先级）

- **P1**：给闸门加**无条件**判据探针（不看四条是否全真，只打 `order / depotIsCouple / onStn / pair / doRes` 五元组），一次复测即可判定是哪一条卡住 —— 避免再盲改。
- **P2**：解自锁环。`doRes=0` 的源头在 `TryPathReserve(consist, false)` 的调用方（行进中重算）；要判断「挂接车在站台区是否应强制 `do_track_reservation=true`」，还是让 `r3r_couple_no_lookahead` 分支自己铺一小段（当前语义只「不许吃捷径」不「主动铺」）。
- **P3**：`RESV-ARB` 把 `wait=1` 的等待车底整格位判给 `wait=0` 的行驶车，是否合理需单独评估（可能正是玩家说的「紊乱」主因）。
- 未确认：`RESV-NOBOOK reason=depot-paired` 与 veh=75 的关系（本轮挂接成功，暂列旁证）。

### 31.6 复读日志后的关键更正（同日，写入即生效）

**一、`EXT-EXIT` 零条是假阴性（第 6 次踩同一个坑）**

`src\r3r_perf.h` 的 `R3RDbgTagAllowed()`（:322-405）按 format 串首 token 做白名单：`RESV` / `UNRES` / `EXTEND`（6 字节）/ `LOOP` / `R3R-RES` / `COUPLE` / `CPL` / `FOLD` / `SPLICE` / `ARTIC` / `SKIP-` / `UNIT` / `CLONE` / `ORD` / `CTRL` / `AUTO-` / `OWNER` / `NAME` / `RESERVE` / `DECOUPLE` / `SPD` / `BUY` / `WP` / `DUALHEAD` / `TTB` / `DEPOT` / `SEGID` / `SEGROW` / `LEGACY` / `CGRP` / `GRP` / `R3R-`。
`EXT-EXIT` 的 token 是 `EXT-EXIT`，`strncmp(fmt,"EXTEND",6)` 不匹配 ⇒ **被静默丢弃**。这正是 `:18417` 那段注释已经写过的坑（第 289 轮把 `EXT-PRE` 改名成 `RESV-EXT-PRE` 才修好），第 521 轮又把新探针命名为 `EXT-EXIT`，原地复发。
⇒ **纪律**：任何新探针的首 token 必须以 `RESV-` 开头（本族全部改成 `RESV-EXT-*`）。

**二、`RESV-EXT-DEST` 三条读数（本轮全部）**

```
1262: RESV-SIGPASS veh=75 tile=1423,1874 track=0 dir=0 doResIn=1 spd=3  reserved=0 ownRes=0 coType=6  real=2 db=0
1487: RESV-EXT-PRE veh=15 tile=1473,1884 enterdir=0 tracks=0x1 reserved=0x0 doRes=1 real=1 spd=0
1489: RESV-SIGPASS veh=15 tile=1473,1884 track=0 dir=0 doResIn=1 spd=0  reserved=0 ownRes=0 coType=16 real=1 db=0
1490: RESV-EXT-DEST veh=15 bad=0 origin=1474,1884 otd=0 ookay=0 dest=1474,1884 dtd=0 dokay=1 lr=0 tracks=0x0 real=1 spd=0
1542: RESV-SIGPASS veh=15 tile=1473,1884 track=0 dir=0 doResIn=0 spd=39 reserved=0 ownRes=0 coType=16 real=1 db=0
1822: RESV-SIGPASS veh=15 tile=1456,1874 track=0 dir=0 doResIn=0 spd=160 reserved=1 ownRes=2 coType=16 real=1 db=0
1823: RESV-EXT-DEST veh=15 bad=0 origin=1424,1874 otd=0 ookay=0 dest=1424,1874 dtd=0 dokay=1 lr=0 tracks=0x0 real=1 spd=160
2051: RESV-EXT-PRE veh=15 tile=1447,1874 enterdir=1 tracks=0x4 reserved=0x1 doRes=1 real=1 spd=0
2062: RESV-EXT-DEST veh=15 bad=1 tile=1455,1873 origin=1447,1873 otd=3 ookay=0 new=1447,1874 exit=1 tracks=0x4 real=1 spd=0
```

计数（`Select-String -SimpleMatch`，全文）：`doRes=1` **2**、`doRes=0` **27**、`RESV-SIGPASS` **5**、`RESV-EXT-DEST` **3**、`EXTEND-BACKOUT` **7**、`RESV-HAVERES` **235**、`RP cur=` **63**、`COUPLE-OK` **1**。

**三、判定 A（强）：X-2 闸门确实命中了，且正是「边走边铺」的直接原因**

`:1490` 与 `:1823` 的 `RESV-EXT-DEST` 形态是 `dest == origin` + `dokay=1` + `tracks=0x0` —— 这正是第 521 轮闸门早退的返回值（`*new_tracks = TRACK_BIT_NONE`、`*enterdir = DiagDirection::Invalid`、`return PBSTileInfo(origin.tile, origin.trackdir, true)`）。自然失败不会同时满足这三项（`tracks=0x0` 且 `dest` 恰好等于 `origin`）。
⇒ 闸门在 **2 / 3 次** `ExtendTrainReservation()` 调用上生效。`EXT-EXIT` 之所以 0 条，纯粹是 §一 的日志过滤。
⇒ 但闸门返回「成功预留到原地」，「原地」= 自己脚下那一格 ⇒ 上层认为「有预留、但只有 1 格」⇒ 列车每 tick 重算、每次只多订脚下 1~2 格 = 玩家看到的「边走边铺」。**这不是既有链路的固有表现，而是第 521 轮的返回码选错了语义**（应当返回「放弃/无前瞻」而不是「预留成功至原点」）。

**四、判定 B（待验证，REG-521-C）：`doRes=0` 与 `_settings_game.pf.reserve_paths` 的关系**

`doRes` 与 `spd` 完全相关：`spd=0` 的三次 `RESV-SIGPASS` 里两次 `doResIn=1`，`spd=39/160` 的两次 `doResIn=0`。
`ChooseTrainTrack` 的 `do_track_reservation = _settings_game.pf.reserve_paths || (flags & CTTF_FORCE_RES)`（`:18346`），而 `TryPathReserveWithResultFlags()` 的调用点（`:20628`）**恒带 `CTTF_FORCE_RES`**，`TrainController` / A* 的直接调用（`:16352`、`:22287`）不带。
⇒ 「停驶重试 doRes=1、行驶中 doRes=0」的唯一自洽解释是 **`_settings_game.pf.reserve_paths == false`**（即该玩家关闭了路径预留）。若成立，则「行驶中边走边铺」在其全局行为里本来就是常态，KI-684 的长预留只出现在 `TryPathReserve()`（停车/到站重试）那几次 —— 与 `:1490`/`:1823` 都发生在 `spd=0` 完全吻合。
⇒ **必须实证**，不能据此外推：下一轮探针打印 `_settings_game.pf.reserve_paths` 与 `flags`。

**五、判定 C：第 521 轮之前「一次性铺一大段」的来源也解释了**

`:1823 origin=1424,1874`（改前会一路铺到 1419,1873 那六格）说明改前那一次扩展是从 1424,1874 一路走出站台端；改后同一位置返回 `tracks=0x0`。**修法与现象完全对得上**，只是「变成 0」太粗暴。

**六、本轮下一步（P1 落地，不改行为）**

1. `EXT-EXIT` → **`RESV-EXT-EXIT`**（让日志可见），并补 `flags`、`reserve_paths`、`pair` 三个字段。
2. 新增无条件五元组探针 **`RESV-COUPLE-GATE`**（`R3RDbgEdge` 去重，key = 车号 + 四判据位）：`veh= order= depotIsCouple= onStn= pair= doRes= rp= flags= lr=`。
3. 判读表：`RESV-COUPLE-GATE` 只在 `ext` 被调用时出现；若 `onStn=0` ⇒ 判据①错；若 `pair=0` ⇒ 判据④错；若两者皆 1 而 `RESV-EXT-EXIT` 有行 ⇒ 闸门确实命中，转 P2 修返回语义。

