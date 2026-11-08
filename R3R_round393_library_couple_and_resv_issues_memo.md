# 第 393 轮（2026-11-06）四问取证备忘

> 本文件是唯一可信状态载体：每条结论必须自带文件:行号锚点与原文片段。
> 规则：先落盘再取证；单轮工具配额 3~5 组，每组结束立即回填本文件。

## 零、玩家报告原文（2026-11-06）

> 编译已经成功，我向你报告，两个关于图像和显示的优化已经完成，但是，那个车库出发挂车站的问题没有解决。因此，这一轮，你需要（行事原则，你自己犯傻的问题自己改，规划结构的问题先说根因，留给我拍板）：
> 1，彻查车库出发挂车站的问题，不要敷衍的打一块补丁完事，宁可先告诉我根因。
> 2，检查一个问题：当一条链解挂成两条之后，假若一格内同时有两条链的链头，前链脱离被卡住，后链等待挂接，另一个机车前来挂接会忽视未脱离的前链，导致列车撞毁。
> 3，我发现，在现行的挂接寻路铺设预留，还是会一次性越过所有信号灯铺到底，这不符合我的预期，我的预期是像原生预留铺设机制那样的逐步铺设
> 4，关于参考命令，我似乎发现了一些有参考命令参与导致的命令手动跳过的紊乱

行事原则（玩家原话）：**自己犯傻的问题自己改；规划结构的问题先说根因，留给玩家拍板。**

## 一、四问登记

| 代号 | 一句话 | 严重度 | 性质 | 状态 |
|---|---|---|---|---|
| P-1 | ~~库内不出发~~ → **能出库，出库后挂不上、界面“等待空余轨道”** | 高 | 行为不符合（结构性） | 根因已定（三层 + 疑点 U-393-A）·待拍板 C-1/C-5 |
| P-2 | 解挂后同格两条链头，前链未脱离，第三方机车来挂后链时忽略前链 ⇒ 撞车 | 高 | 安全/数据（结构性） | 根因已定（代码上成立）·待拍板 C-2 |
| P-3 | 挂接寻路预留一次性越过所有信号灯铺到底，未像原生逐段铺设 | 中 | 架构/口径 | 根因已定（是设计形态）·待拍板 C-3 |
| P-4 | “参考命令”（自动填充记录）参与导致手动命令被跳过而紊乱 | 中 | 行为不符合 | 根因已定（空气区间整段吞）·待拍板 C-4 |

> 本轮**未改任何源码、未构建、未运行游戏**（只读日志 + 只读源码）。四个未销项（`CPL-SAFE-FAIL` 判据、`rej` 位义、99,97/99,105 不一致、99,97 上的车）已于 §4.3.1 全部销项。

## 二、既有记录交叉引用

- **P-1 就是上一轮登记的那条**：`R3R_KNOWN_ISSUES.md:21671`「### KI-558（待取证，严重度 高）：库内机车执行 GOTO_COUPLE 时不出发，界面停在"等待空余轨道"」，来源 392 轮备忘。注意**编号冲突**：`R3R_KNOWN_ISSUES.md:21648` 的 391 轮已把 KI-558 用作「车库 Ctrl+左键对铰接组翻转」，392 轮又复用了 558。本轮按最新编号（KI-560 之后）重新编为 **KI-561**，并在条目里注明与旧 KI-558 的指向关系。
- **口径现状（2026-11-06 最终）**：§4.3 曾据 `645052 RESV-BEGIN` / `645060 TRP type=16 => ok=1 res=1` 判定“机车能出库”，该判定**已按玩家 C-1 答复撤回**（原话「现在机车还是卡在车库里面不出库」）。⇒ KI-561 恢复原描述 **库内不出发**；两行日志为何不足为凭、以及销项所需的取证清单，见 §7.1 的 **U-393-B**。
- P-1 的已知同类路径（来自既有 KI，仅供对照，不是结论）：
  - KI-242（第 242 轮实测）：`cur_speed==0` 时 `TrainLocoHandler` 恒走停驶分支（不寻路、不预留），是“第 4 条结构性路径”。
  - KI-347：库内 `TRACK_BIT_DEPOT` / 非 PBS 段 / `OT_WAIT_COUPLE` 三条路径。
  - KI-330：`CheckTrainsOnTrack` 同格误判（TPR-BLOCKED）。
  - KI-208：`RESV-NOBOOK` 门禁只在**站台格**生效。
  - `R3R_KNOWN_ISSUES.md:9289`：「库内挂车豁免」`r3r_pending_depot_couple` = `IsEngine() && r3r_parked && 待执行命令是 GOTO_COUPLE && 目的地就是脚下这个车库`。

## 三、取证计划与假设（**尚未验证，逐条待销**）

### P-1 假设 H-1a（先读代码，不猜）

“界面显示等待空余轨道”说明走的是 `TryPathReserve` 失败 → `MarkTrainAsStuck` 那条路，即列车**确实尝试过预留**并失败；而不是 KI-242 那种“根本没进移动分支”。两者界面文案不同，必须先分清是哪一种：

- H-1a：进入移动分支但 `TryPathReserve`/`ChooseTrainTrack` 失败（真卡在寻路）。
- H-1b：根本没进移动分支（`cur_speed==0` + 库内早退），Stuck 是历史残留。
- H-1c：库里出发的“库门格”判定（`TRACK_BIT_DEPOT` / `IsStoppedInDepot`）导致 YAPF 起点非法。

判据：`build\R3R_debug.log` 里该车是否有 `TRP`/`TPR-BLOCKED`/`TPR-WALK`/`CPL-S0-CHK`/`LOCO-MOVE` 行。

### P-2 假设 H-2a

挂接候选筛选只看“对方是不是 `OT_WAIT_COUPLE` 的合法目标”，没有校验“该格/该位置是否已被另一条未脱离的链占用”；而 R3R 挂接是**直接重链接 + 拼接几何**，不走 `CheckTrainCollision` 的库内分支，于是把新车拼进了还压在原地的链所在格 ⇒ 视觉与物理重叠。

判据：解挂提交点（`DecoupleTrain`）之后两条链的 `tile` 是否相同；第三方挂接时 `CheckTrainCollision` 是否被调用、返回什么。

### P-3 假设 H-3a

R3R 的预留不是原生 `TrainController` 的逐段推进，而是寻路成功后由回溯 lambda（`src\pathfinder\yapf\yapf_rail.cpp` 的 `rp()`）一次性把整条路径全部 `ReserveRailTrack` 掉（第 240 轮 KI-346 修的就是这个 lambda 里“进入轨道算错”的 bug，但“一次铺到底”的机制本身没变）。

判据：`RP cur=` 行是否在同一 tick 内覆盖整条路径；对照原生 `TrainController` 的逐格推进（`_settings_game.pf.` 相关 / `TrainReservationTicks`？待查）。

### P-4 假设 H-4a

“参考命令”= 第 264 轮续确立的**自动填充记录**（`auto_ranges` / `R3RAutoRecordChainOrders()`）。当前实现是“记录 + 执行时按区间跳过”，若记录区间与手工命令的索引错位（KI-527 已修过 anchor 口径、KI-528 仍未查），就会表现为“手动命令被莫名跳过”。

判据：日志里 `AUTO-ORD`/`AUTO-ORD-DROP`/`SKIP-AUTO` 行与该车 `real=` 的对应关系。

## 四、取证记录（每组工具调用后立即追加）

### 4.1 P-1 第一层：库内 tick 入口的“停驶硬门”（已读到代码）

**锚点 1：`src\train_cmd.cpp:22272`**

```cpp
if (!r3r_pending_depot_couple && consist->vehstatus.Test(VehicleState::Stopped) && consist->cur_speed == 0) {
    ... SKIP-STOPPED 探针 ...
    return true;
}
```

**结论句 1**：一辆停在车库里、`Stopped` 且 `cur_speed==0` 的链，在这个门就直接 `return true` —— 后面的 `ProcessOrders()` / `TrainController()` / `TryPathReserve()` **本 tick 一次都不会执行**。这是 KI-242 那条“结构性路径”的上游变体：不是速度掉了才不寻路，而是**根本没进 handler 主体**。

**锚点 2：`src\train_cmd.cpp:22258-22261`**（豁免判据）

```cpp
const bool r3r_pending_depot_couple = consist->IsEngine() && consist->r3r_parked && r3r_pend != nullptr &&
        r3r_pend->IsType(OT_GOTO_COUPLE) && r3r_pend->GetCoupleIsDepot() &&
        IsRailDepotTile(consist->tile) &&
        r3r_pend->GetDestination().ToDepotID() == GetDepotIndex(consist->tile);
```

**结论句 2**：这个豁免要求「订单目的地是**车库**，且就是脚下这个车库」。玩家场景是「从车库出发去**车站**挂接」⇒ `GetCoupleIsDepot()==false` ⇒ `r3r_pending_depot_couple==false` ⇒ 硬门必然生效。**即：现有豁免的设计只覆盖“在库里挂库里的车底”，完全不覆盖“从车库出发去站台挂车底”** —— 后者恰恰是玩家报的场景。

**结论句 3（暂定，待销）**：因此“库里不出发”的第一层根因是「硬门只识别库内挂车豁免，不识别库外目的地的正常发车」，而 `r3r_parked` 这个额外条件（KI-118 修法 A 引入）使它更窄：非 R3R 工具停放的机车即使订单是库内 GOTO_COUPLE 也不豁免。

**未销项**：①机车此时 `Stopped` 是谁置的（玩家按停止 / `R3RStopChainInDepot` / 造车后未启动 / `CmdStartStopVehicle`）；②界面“等待空余轨道”对应 `VehicleRailFlag::Stuck`，而 Stuck 只在预留失败时置位 ⇒ 说明**曾经**尝试过并失败，需找到那次置位点，否则根因只解释“不重试”，不解释“为什么第一次就失败”。

### 4.2 P-1 第二层：“等待空余轨道”的置位点已锁定（已读到代码）

**锚点 3：`src\lang\simplified_chinese.txt:4610`** `STR_VEHICLE_STATUS_TRAIN_STUCK :{ORANGE}等待空余轨道` ⇒ 玩家看到的文案就是 `VehicleRailFlag::Stuck`，**不是** `Stopped`。

**锚点 4：`src\train_cmd.cpp:15414-15419`**

```cpp
/* Only leave when we can reserve a path to our destination. */
if (seg_state == SigSegState::Path && !TryPathReserve(v) && v->force_proceed == TFP_NONE) {
    /* No path and no force proceed. */
    MarkTrainAsStuck(v);
    return true;
}
```

**结论句 4**：库内出发的唯一 Stuck 置位点就在这里 —— **`TryPathReserve(v)` 返回 false**。所以 P-1 不是“没尝试”，而是“尝试了，YAPF 没给出路径”，随后每个 tick 重试、每次失败，界面恒为“等待空余轨道”。

**锚点 5：`src\train_cmd.cpp:18141` `R3RIsParkedWaiting()`**

```cpp
const bool r3r_goto_couple = consist->current_order.IsType(OT_GOTO_COUPLE);
const bool r3r_in_depot = IsRailDepotTile(consist->tile);
...
if (r3r_goto_couple && r3r_partner == nullptr && !r3r_in_depot) { ... "parked-no-pair" ... }
```

**结论句 5**：`RESV-NOBOOK` 那套“先配对再启动”的门对**库内格豁免**，所以 P-1 也不是被 NOBOOK 门拦掉的。⇒ 失败点在 `TryPathReserve` 内部，即 `OT_GOTO_COUPLE` 的 YAPF 目标构造/起点处理。

**下一步（待销）**：`TryPathReserveWithResultFlags()`（`src\train_cmd.cpp:18354`）里 `OT_GOTO_COUPLE` 走哪套 YAPF（destrail 的 `FindNearestCoupleTrain`），以及从 `TRACK_BIT_DEPOT` 出发时起点/首格如何被接受。这是“为什么第一次就失败”的最后一层。


### 4.3 P-1 第三层（**结论已按 C-1 撤回，仅留证**）：曾据日志判“库内不出发”被证伪，真失败点是挂接安全位校验（2026-11-06）

**现场材料**：`build\R3R_debug.log` **94 801 477 B / 680 406 行 / 2026-11-05 22:25:39**（实测命令：`Get-Item` + `[System.IO.File]::ReadLines` 计数；此前正文里写的 1 477 801 B 是错的，已订正），产生者 `build\openttd.exe` 52 453 376 B / 21:57（与当前源码同步）。
**旁注（探针洪泛）**：680 k 行 / 95 MB 属探针刷屏量级（`DEPOTBLK` 同一行重复 76 次、`CPL-SAFE-FAIL` 循环每 ~20 tick 一轮、`CPL-S0-CHK wait=` 累加到 10 000+），登记为观测项 O-393-1（不影响结论，但取证成本高）。

**筛选过程**：`TRP` 共 14 106 行；其中 `type=16`（OT_GOTO_COUPLE）失败 `ok=0 res=0` 的分布为 veh=51 ×84、veh=48 ×13、veh=57 ×12、veh=50 ×3……；而 `DEPOTBLK ... isdepot=0 ontile=1`（订单是 GOTO_COUPLE 但目的地不是车库，且车正停在车库格上）**只有 veh=51 ×76，全部 tile=62,117 track=80**。⇒ veh=51 就是 P-1 的现场主角。

**结论句 6（重要，推翻了 KI-561 的原始描述）**：veh=51 **能**从车库出发：
- `645052 RESV-BEGIN veh=51 tick=11550 tile=62,117 mf=62,117 td=0 spd=0 real=0 order=16`
- `645060 TRP veh=51 mf=62,117 real=0 type=16 ... fto=0 => ok=1 res=1`
⇒ 库内 GOTO_COUPLE（目的地非车库）**不被** `:22272` 停驶硬门或 `R3RIsParkedWaiting` 的 NOBOOK 门永久拦死，`TryPathReserve` 在库里**成功过**。P-1 的真实症状是“出去以后挂不上”，不是“不出发”。

**veh=51 全程（extract: `build\r393_p1_veh51.txt`，行号=日志行号）**：
- `644972 DEPOT-ARR real=0(6) tx=62 ty=117` → `645488 real=1(6) 40,115` → `648592 real=2(6) 99,93` → `649309 real=2(9) 99,109 tileEqDest=1` → `649339 real=3(16) tx=99 ty=109 destTx=99 destTy=97`
- 即：三张 GOTO_WAYPOINT 跑完，才轮到 `real=3` 的 GOTO_COUPLE，目标格 99,97。

**失败链条（同一个 tick 内，逐行）**：
```
654028 RESV-BEGIN veh=51 tick=19001 tile=99,109 td=1 spd=0 real=3 order=16 look=1
654029 TPR-WALK  veh=51 mf=99,109 origin=99,109 td=1 okay=1 exit=1 new=99,110 reach=0
654031 TRP veh=51 mf=99,109 real=3 type=16 ... => ok=0 res=0
654110 LOCO-MOVE ... j=194 spd=5 endTile=99,109 endTd=1 endPos=263 curPos=264 endBits=0202
654111 LINEND  veh=51 tile=99,109 x=15 sig=0 near=1 spd=5 brk=15
654112 REVERSEDIR veh=51 tile=99,109 dir=3 order=16 spd=0 rev=0 stuck=0 db=0
654122 TPR-ZERO-REACH veh=51 mf=99,109 td=9 turn=1
654125 RESV-EXT-PRE veh=51 tile=99,108 enterdir=3 tracks=0x2 reserved=0x4 doRes=1
654127 EXTEND-BACKOUT veh=51 origin=99,109/9 stop=99,108/9 next=99,107 reason=choice-reserved owner=6 whole=1
654128 RESV-EXT-DEST veh=51 bad=1 ...
654133 CPL-ORIGIN-FOLLOW veh=51 td=9 trk=1 ok=0 nt=-1,-1 nbits=0x0 st=0 skip=0
654143 CPL-PATHFOUND veh=51 found=1
654146 CPL-BEST veh=51 tile=99,97 td=9 cost=11700
654149 CPL-SAFE-FAIL veh=51 tile=99,97          <== 候选安全位被否
654150 CPL-BLOCKED-WAIT veh=51 tile=99,109 dest=99,105
654151 CPL-PATHBUG veh=51 alock=6 found=0 rej=0x1 dest=99,105 dist=4 spd=0
654152 TRP veh=51 ... one=1 fto=1 => ok=0 res=0
654166 STUCK veh=51 state=1 stuck=1 fp=0 spd=0 order=16 real=3 prog=2 track=02 dtile=13539
```
**结论句 7（订正版，2026-11-06 复读日志尾部：原结论作废）**：**不是**“永久不恢复”。veh=51 在 tick 19001 失败后确实在原地反复重试并累加等待（`CPL-S0-CHK wait=` 递增），但**后来恢复了**：
- `669424 RESV-FOREIGN-RES veh=51 tile=51,116 enterdir=0 res=0x1`、`669426 CPL-ORIGIN-FOLLOW veh=51 td=1 ok=0 st=1`（前方 51,116 被别人预留占着，仍是同类阻塞）
- `669427 CPL-SCAN ... lastveh=27 ... why=acc-car`、`669761 GEO settle-couple n=2 veh=51 tile=49,116 segid=3`、`669790 ORD-IDX-WATCH site=tick-out real=9->0 impl=9->0`
- 前方 `6694xx/GEO settle-couple` 之后到日志末尾，veh=51 已在 52,116 一带以 `spd=120` 正常行驶（`CPLSPD`/`TRP ok=1`），并在 **tick 33590 于 49,116 挂上了 veh=27**。
⇒ 症状不是“不出发/永不挂上”，而是**代价极大的绕行 + 长等待**：tick 11 550 出库 → tick 33 590 才挂上（约 22 000 tick）。原文那句“永久同形态重试/累加到 10 000+ tick 不恢复”**作废**（是只读了失败段没读尾部造成的误判）。

**结论句 7-旧（已作废，仅留档）**：之后是**永久同形态重试**：`654190 EXTEND-BACKOUT reason=reserve-fail` → `CPL-ENTRY/ORIGIN/PATHFOUND/BEST(cost=11400)` → `CPL-SAFE-FAIL tile=99,97` → `TRP ok=0` → `CPL-S0-CHK wait=20…39` 一路累加到 10 000+ tick 不恢复。所以界面“等待空余轨道”= `VehicleRailFlag::Stuck` 由**挂接寻路失败**置位（不是 `CheckTrainStayInDepot`），且失败点**确定在 `CPL-SAFE-FAIL`/`CPL-PATHBUG` 这一段**。

**旁证**：`654127 EXTEND-BACKOUT ... reason=choice-reserved owner=6 whole=1` + `654125 RESV-EXT-PRE tile=99,108 reserved=0x4` ⇒ 掉头后出格 99,108 已被 **veh=6 的整格预留**占着（`owner=6`），延伸预留被拒；这与 P-2“同格/邻近格被别的链占着”是同族现象。

**未销项 → 已全部销项（2026-11-06 追加，锚点见 4.3.1）**。

### 4.3.1 四个未销项的销项记录

**销 1 · `CPL-SAFE-FAIL` 的判据**：打印点在 `src\pathfinder\yapf\yapf_rail.cpp:1374`，由 `:1370 if (!this->CheckSafePositionOnNode(pPrev))` 触发 —— 即**回溯链上某个节点的安全位判据失败**，命中后 `:1382 _r3r_couple_path_blocked = true` 并 `return INVALID_TRACKDIR`（整条路径作废）。具体拒因由 `FindSafeCouplePositionProc`（同文件 `:176` 起）内部的分支决定，各分支自打 `FSCP ... fail=<原因>`。本次现场只有一个 FSCP 行：
```
654148 FSCP tile=99,106 fail=notSeg best=48 ord=6 sf=1
```
⇒ 拒因 = `notSeg`（同文件 `:271-277`：该格上的车 `!R3RIsCoupleTarget(best)` 即判“不是段/不是合法挂接目标” ⇒ `return false`）。请注意 `CPL-SAFE-FAIL` 打的是 `pPrev->GetLastTile()`（99,97），`FSCP` 打的是被检查的那一格（99,106），两者不同格属正常：一个节点覆盖一段 tile（`IterateTiles`），失败时外层打印节点末格。
⇒ 结论句 11：**`CPL-SAFE-FAIL` 不是“没路”，而是“路算出来了但回溯经过的车不是合法挂接目标”**，与 4.4 层二同一机制。

**销 2 · `CPL-PATHBUG ... rej=0x1`**：`rej = R3RCoupleSearchRejectMask(consist)`（声明 `src\train_cmd.cpp:2601`，实现 `yapf_rail.cpp:2332`）。位义见 `:2330` 注释原文：**`bit 0 = GROUP, 1 = FIT, 2 = TARGET, 3 = ORDER`**，取值是“上一次真实（非节流）搜索里被四道目的地闸门拒过的类别”。
⇒ `rej=0x1` = **只有 GROUP（挂接分组白名单）拒过候选**，与同一 tick 的扫描汇总行自洽：`654144 CPL-SCAN ... rej=2/0/0/0`（rej 计数按 GROUP/FIT/TARGET/ORDER 四类分别计数，仅 GROUP 非 0）。注意它打的是“上一次真实搜索”的掩码，不是本 tick 的即时值，只能作定性用。

**销 3 · 99,97 与 99,105 不一致**：**不是矛盾，是两个不同语义的格**。铁证是紧邻的两行：
```
654144 CPL-SCAN ... last=99,97 lastveh=6 why=acc-car
654145 CPL-HEAD best=99,97 head=99,105 on=0
```
- `best=99,97` = **YAPF 终点格**（`CPL-BEST veh=51 tile=99,97 td=9 cost=11700`，第 654146 行）；
- `head=99,105` = **目标车底 veh=6 的链头格**（`on=0` 明确写出“best 格上没有链头”）；
- `CPL-BLOCKED-WAIT ... dest=99,105` 打的是 `consist->dest_tile`（`train_cmd.cpp:17509-17512`），即配对锁定的会合点 = 车底链头格。
⇒ 车底 veh=6 是一条从 99,97 延伸到 99,105 的长链（机车在 99,109，车底近端 99,105、远端 99,97）。**挂接终点被选在了远端 99,97，而真正的拼接面在近端 99,105**。这是比“第三方挡路”更靠前的一层异常，见 4.4「层二附」。

**销 4 · 99,97 上是什么车**：是**合法挂接目标本身**（不是障碍）。证据链：
- `654134/654135/654142 PFD-ACCEPT kind=car-only tile=99,97 t=6 ordType=17 wc=1`（三次接受，`ordType=17` = `OT_WAIT_COUPLE`）；
- `654144 CPL-SCAN ... last=99,97 lastveh=6 why=acc-car`（`why=acc-car` = 因“car-only 车底”被接受）；
- 同 tick `CPL-SCAN ... consist=5`，说明扫描期确实把 veh=6 当成等待车底。
⇒ 真正把路径否掉的是**过路车 veh=48**（`ord=6` = `OT_GOTO_WAYPOINT`、`sf=1` = 段头），不是 veh=6。

### 4.4 P-1 根因（三层，均为结构性）

**层一 · 目的地不确定**：`OT_GOTO_COUPLE` 没有固定目的地格，每 tick 由挂接寻路重新选“最近的合法等待车”：
`pathfinder/yapf/yapf_rail.cpp:1160 FindNearestCoupleTrain()` → `SetOrigin(v->tile…)` → `SetDestination(v)` → `FindPath()`。所以“车往哪开”是**逐 tick 决策**，不是一条固定路线。

**层二 · 路径全有或全无**：`FindPath` 之后回溯，对每个 `pPrev` 跑 `CheckSafePositionOnNode`（`:1370`）→ `FindSafeCouplePositionProc`（`:176`）。只要回溯链上出现**任何**第三方列车、且它不是“合法挂接目标”，就 `fail=notSeg` 直接 `return false`（`:271-277`），随即 `:1370-1384` 置 `_r3r_couple_path_blocked = true` 并 `return INVALID_TRACKDIR` —— **整条路径作废**，没有“截断到障碍前”的语义。
现场铁证：`654148 FSCP tile=99,106 fail=notSeg best=48 ord=6 sf=1` —— 第三方是 **veh=48，订单类型 6 = `OT_GOTO_WAYPOINT`，`IsSegmentFront=1`**，即一个**纯过路的正常段头**；它只是站在 99,106，就把 veh=51 去 99,97 挂 veh=6 的整条路径否掉。
（`fail=2nd` 的同格保护有盲区：`:225` 按 `TrackdirToTrack(td)` 过滤轨道位，**不同轨道的同格第二条链头看不见**；而 `:241-255` 的多链头分支是 `return true` **放行**。）

**层三 · 等待语义 + 前方被整格预留锁住**：路径被否后按 KI-516 既定口径（玩家 2026-10-28“路径被挡住就等，等到路径被放出来”）**不降级绕路、就地等**；而车此刻已在路尽头 99,109 掉过头（`654111 LINEND near=1` → `654112 REVERSEDIR dir=3`），掉头后要走的 99,108 正被 **veh=6 的整格预留**占着（`654125 RESV-EXT-PRE … reserved=0x4` → `654127 EXTEND-BACKOUT reason=choice-reserved owner=6 whole=1`）。
⇒ “挂接路径被第三方否掉 + 前进方向被整格预留锁住”构成**双锁**，只有第三方走开才解。

**结论句 8**：界面“等待空余轨道”= `VehicleRailFlag::Stuck` 由**挂接寻路失败**置位（与 `CheckTrainStayInDepot` 的库型失败是两条不同的置位路径）；P-1 的性质是“**任何第三方路过都能让挂接列车站死**”，属结构性缺陷，不是单点 bug。

**层二附（疑点 U-393-A — 已按玩家 C-5 关闭为非缺陷，仅留档）：本次还有“路径终点 ≠ 拼接面”这一独立异常**——即 KI-200 那条“path end 与 reservation target 必须同端”的保护在本次未生效。**玩家口径（2026-11-06）**：「挂接寻路终点在远端是一个无伤大雅，符合游戏原生逻辑的一个机制，现在的挂接寻路问题也不是它造成的」⇒ 本节不再作为成因层。

- 事实（同一 tick 相邻三行）：
  ```
  654145 CPL-HEAD best=99,97 head=99,105 on=0
  654146 CPL-BEST veh=51 tile=99,97 td=9 cost=11700
  654147 CPL-BLOCKED-WAIT ... dest=99,105
  ```
  `best` 是 YAPF 节点终点格（`yapf_rail.cpp:1305` 打印的是 `pNode->GetLastTile()`），`head` 是解析出的拼接面，`on=0` 说明**终点格不在车底上**（`best_on_consist=false`）。
- 代码路径：`yapf_rail.cpp:1291-1297` —— `on=1` 时强制 `consist_head_tile = pNode->GetLastTile()`（同端）；`on=0` 时退回“取曼哈顿最近的那一端”。本次走到后者，解析出 99,105（离机车 99,109 更近，**这个值是期望值**），但 **path end 仍钉在 99,97**（远端）。
- 为什么这算异常：同文件 `:1279-1290` 的 KI-200 注释原文写着“path end and reservation target must be the SAME end”，并记录历史现场正是 `CPL-BEST 58,19` + `CPL-HEAD best=58,19 head=58,23` ⇒ “the hand-rolled reserve walk below had to trace a route back through the consist, reserving the whole detour”（15 格 RP 链）。本次 `best=99,97 / head=99,105` 是**同一病症的另一形态**：保护只在 `best_on_consist` 为真时生效，`on=0` 时无保护。
- 后果推断（与层二叠加）：终点在远端 ⇒ 回溯必须从 99,109 一路走到 99,97，途中 99,106 的 veh=48 先被 `fail=notSeg` 判死，**连“拼在近端 99,105”都不可能**。若终点取近端，同样的 veh=48 仍在 99,106 上，结论未必改变，但至少不会出现“穿过整条车底订预留”的第二重风险。
- **待验证（U-393-A）**：①为何 `on=0` —— 99,97 若真有车体，`best_on_consist` 应为真，需读 `:1264-1291` 的 `t/h/best_on_consist` 赋值与 `VehiclesOnTile` 取格口径（可能与 `:1422` 最小 ID 同源）；②`td=9` 在 99,97 上是哪个方向，是否与该格实际轨道相符（`CPL-PATHBUG` 未报 `rej` 位，说明闸门认为目的地合法）；③PFD-ACCEPT 已三次接受 99,97 为 `t=6` 的目的地，说明“接受为目的地”与“判定在车底上”是两把尺子，**这个不一致本身就是缺陷候选**。

### 4.5 P-2 根因：全链无“同格第三方车体”校验

- **取同格车**：`pbs.cpp:1401 CheckTrainsOnTrack()` → `:1403 VehiclesOnTile<Train>` → `:1422-1423 /* ALWAYS return the lowest ID (anti-desync!) */` ⇒ **恒取 VehicleID 最小那一条链头**，与几何、与“这格还压着几条链”无关。
- **命中层单候选不回退**：`train_cmd.cpp:14461 GetCouplePosition()` → `:14468 Train *u = Train::From(other_vehicle)->First();` → `:14474 if (!R3RCanCoupleNow(v, u, "geo")) return nullptr;` —— 最小 ID 那条不合格就整格判“没有等待车”，**不会去找同格另一条链**。
- **候选判据不含占用**：`R3RIsCoupleTarget()`（`train_cmd.cpp:2680-2728`）只要求“`IsSegmentFront` + 是链头 + `cur_speed==0` + `OT_WAIT_COUPLE`”，**没有“该格是否已被别人车体占据”**。3x3/车库扫描同样只取哈希链上第一个通过者（`:14859-14872`）。
- **提交层只认两条链**：`TryTrainCouple()`（`:11525`）从入口（`:11538` `v->First()!=v`、`:11539` 自己挂自己）到 `:12122 bool ok = CheckTrainAttachment(head).Succeeded();`，**全程不调 `CheckTrainCollision`、不枚举同格第三列车**；`CheckTrainAttachment` 只沿合并链 `Next()` 遍历（`:3710-3713`）。
- **撞车检测晚一帧且被“解挂缝豁免”削弱**：`CheckTrainCollision` 只在移动后由 `TrainController` 调（`:23180`）；`:19958-19961` 的低速 + “两侧订单类型不同（一侧 WAIT_COUPLE）”豁免会把“合并链压住前链”判成解挂缝、**不报撞**。
- **保护只在一处且是放行式**：`yapf_rail.cpp:241-255 if (n_tile_heads >= 2 && tile_heads_have_target) { … return true; }`，注释自陈“视为可通行、但不当作终点”。

**结论句 9**：玩家所述“忽视未脱离的前链导致撞毁”在代码上**成立**。窗口 = 同格两链头中被选中的那条合格时，另一条（卡住的前链）完全不进任何判据；且解挂后两半本就“seam to seam 同格相接”（`:19878-19879`、`:19924-19929` 自陈），不是罕见前提。

### 4.6 P-3 根因：两遍法 / TryReservePath —— 一次订到底，不在信号处截断

`yapf_rail.cpp` 挂接预留是**两遍法**：
1. **摸路（只读）**：`rp()` 沿 `TrackFollower` 走完整条物理路线，把每格 `{tile, td}` 记进 `r3r_resv_path`（`:1542`；失败分支会回滚 `:1580-1583`）。
2. **预订（一次到底）**：`:1610-1614` `SetReservationTarget(pNode, r3r_resv_path.back()…)` + `R3RReservePathSequence(r3r_resv_path, r3r_booked)` —— **按序把整条记录一次订完**。
非单段分支：`:1617 reserved = this->TryReservePath(nullptr, pNode->GetLastTile(), true);` 同样是“节点链一次订完”。
两条分支都**不在信号处截断、不看他人预留边界**；失败才回滚（`:1606-1608` 注释：“失败则回滚已订格并如实返回失败”）。
**结论句 10**：玩家看到的“一次性越过所有信号铺到底”是这段代码的**设计形态**。对比证据：普通路径在**同一份日志**里走的是一格一格延伸（`RESV-EXT-PRE … doRes=1` → 遇 `choice-reserved` 就 `EXTEND-BACKOUT … bad=1` 撤回），两套预留语义并存。

### 4.7 P-4 根因：`auto_ranges` 已被 KI-541 删除，换成“空气区间”，但“按范围整段吞”没变

- **旧机制（`src\train_cmd.cpp.r3r_bak`，已不在编译树里）**：`R3RAutoRecordChainOrders():6892` 记录，`R3RSkipAutoRecordedOrders():7158` 执行；判据是**纯索引半开区间** `idx >= begin && idx < begin+count`（`:6668-6672`），命中后 `v->cur_real_order_index = (begin+count)`（`:7195-7204`）⇒ **区间内任何手工命令被一次吞掉**。另有边界口径不一致：记录侧 `if (anchor > (b + count)) continue;`（`:7001-7002`，闭）而跳过侧是半开；`R3RAutoRangesShift`（`:6682-6697`）只在插入点 `from` 落于区间**外部**时平移 `begin`，插入点落在区间内部时手工命令直接被划入参考区间。
- **现机制（当前 `src`，KI-541 第 376 轮）**：`auto_ranges` 字段已删（`couple_group.h:556-563` 注释），改为把参考命令夹在 `OT_GOTO_COUPLE` … `OT_COUPLE_END` 两个标记之间；`order_base.h:1853-1870 R3RIsCoupleAirOrder()` 按**标记**判定，执行侧 `R3RSkipCoupleAirRanges()`（`train_cmd.cpp:6945-6975`）命中后一路扫到 `OT_COUPLE_END`，`cur_real_order_index = (end_idx+1)%num`（`:6965-6971`）。
- ⇒ **“参考命令参与 ⇒ 手工命令被跳过”的直接机制就是“整段跳到区间末尾”**：夹在两标记之间的手工命令被无条件吞掉；旧实现则是“索引落在区间内”被吞。
- 附带副作用（已登记 KI-541 后续）：跳过末尾无条件把 `cur_timetable_order_index` 对齐到 `cur_real_order_index`（`:6982-6984`），时刻表/分离被占位命令拉偏。

## 五、待玩家拍板清单

> 四项全部是**结构性口径**问题，按你的原则本轮**未改任何源码**，只报根因。请按编号回填选择。

## 五、待玩家拍板清单 → **已于 2026-11-06 全部回填**

**C-1 = 玩家纠正：症状不是“全有或全无”，而是「机车还是卡在车库里面不出库」**

- 玩家原话：「C1，我想是我表达或你理解有误，现在机车还是卡在车库里面不出库」。
- ⇒ **上一轮 §4.3 的「标题订正：机车能出库」作废**（见 §4.3 开头的撤回标记）。KI-561 恢复原描述：**库内机车执行 GOTO_COUPLE 不出发**。
- 我上一轮据以订正的两行 `645052 RESV-BEGIN` / `645060 TRP type=16 => ok=1 res=1` 现在必须重核：那两行**不能证明车真的离开库格**——`TRP ... ok=1 res=1` 只说明订路/预留成功，库内 `TRACK_BIT_DEPOT` 出库还另需 `CheckTrainStayInDepot` 放行；且当时未核这两行的 `veh=` 是否就是玩家所指那台。⇒ 登记疑点 **U-393-B**（见 §7）。
- ⇒ 原 C-1 的 A/B/C 三选项**针对的是“出库后挂不上”，不再适用于本条**；P-1 的正确落点是“库内不出库”，见 §7 的 D-1。

**C-2 = 玩家要求复述历史修复**（不是新选项）⇒ 见 §5.1。

**C-3 = 选 A**：完全照原生 —— 挂接寻路的**目标**改为“能订到的最远点”，遇到信号/他人预留边界即停，下一 tick 续订。（P-3 的 B/C 不采用。）

**C-4 = 玩家自定义口径（非 A/B/C 任何一项）**

- 玩家原话：「实际上，我也难以描述问题，只好说说我的预期：列车正常行驶推进排程，现有的 GOTOCOUPLE 之后推进到 COUPLEEND 是正常的，手动跳命令也按照这样吧，就是说空气区间对于自然推进和手动跳过都是空气，直接跳到 COUPLEEND 算了」。
- 落地口径：**空气区间对“自然推进”和“手动跳过命令”是同一件事 —— 都是空气，都直接跳到 `OT_COUPLE_END`**。
- ⇒ 保留执行侧现状（`R3RSkipCoupleAirRanges()` 一路扫到 `OT_COUPLE_END`，`cur_real_order_index=(end_idx+1)%num`）；**不采用 C-4-A 的“遇手工命令即停”**。真正的缺口变成：**手动跳过路径（玩家按 Skip 按钮 → `CmdSkipToOrder`）是否也走同一套空气区间跳过**（见 §7 的 D-2）。

**C-5 = 选 A（不动）**

- 玩家原话：「对于我来说，挂接寻路终点在远端是一个无伤大雅，符合游戏原生逻辑的一个机制，现在的挂接寻路问题也不是它造成的」。
- ⇒ **U-393-A 关闭（判定为非缺陷）**。§4.4「层二附」整段降级为“已判定非缺陷，仅留档”，不再作为根因层；KI-561 里的第四层推断删除。

### 5.1 C-2 复述：那次“同格双链头 → 挂接配对出问题”的修复 = REG-359-H / REG-359-I

**当时的问题（第 363 轮续定位，KI-533 条目内 REG-359-H）**：`FindSafeCouplePositionProc`（`src\pathfinder\yapf\yapf_rail.cpp:194-249`）用**「index 最小的那台车」代表整个格**，只要这台不是合法挂接目标，**整格就判障碍**，格上另一台合法的等待车底被完全忽略。

两个具体缺陷：

1. **轨道筛选（`:199`，即 `:225` 的 lambda）**：只有满足 `tr->track == TRACK_BIT_WORMHOLE || HasBit((TrackBits)tr->track, TrackdirToTrack(td))` 的车节才被计入，**但结论是整格结论** ⇒ 同格另一条**不同轨**上的链头完全不可见。
2. **单点量化（`:202` 起 `best` / `:227` 起 `second_best`）**：`best` 取 `index` 最小者，之后 `if (!R3RIsCoupleTarget(best)) return false;` ⇒ 一台不合格就整格否决；`second_best` 虽然记了，但**不参与“是否存在合法目标”的存在量化**。

**现场铁证**：`:789 FSCP tile=36,12 fail=notSeg best=57 ord=6 sf=1`（`ord=6`=`OT_GOTO_WAYPOINT`）⇒ `:790 CPL-SAFE-FAIL veh=48` ⇒ `:791 CPL-BLOCKED-WAIT veh=48 dest=35,12` ⇒ `:796 COUPLE-FAIL loco=48`。而 `veh0` 才是合法等待车底（`:535 GEO settle-decouple-u n=0 veh=0 tile=35,12 starF=1 segid=1`），`veh57` 是解挂留下的**机车半**、同格不同轨。

**当时做的是什么（REG-359-I，第 364 轮落地，代码现在就在树里）**：把“单点代表整格”改成“**整格链头集合 + 存在量化**”——

- `:198-221` 新增 `tile_heads[MAX_TILE_HEADS=4]` 与 `note_tile_head()`：**故意不套 `:199` 的轨道筛选**，用 `VehiclesOnTile<VehicleType::Train>(t)` 收集整格所有链头（按 `->First()` 去重）；
- `:217-219` 只要存在一个 `h` 满足 `R3RIsCoupleTarget(h) && R3RCoupleAllowed(loco, h)`，就置 `tile_heads_have_target = true`（**这就是存在量化**）；
- `:241-255` 当 `n_tile_heads >= 2 && tile_heads_have_target` 时打 `FSCP tile=x,y skip=multihead n=%u h=%d,%d`，并**按注释口径“暂存” ⇒ 视为可通行、但不当终点**（`return true` 放行）。

**复述时必须点出的“只做了一半”**（这是本次 P-1 的一个潜在交叠点）：REG-359-H 给的推荐修法 A 原文是「即视为可通行，**并用该 `h` 作为本格目标向上传**」，但落地实现 REG-359-I 只做了前半句——“放行但不当作终点”。⇒ 若格上唯一的合法目标恰好**应该**是本帧终点，放行后仍找不到终点，会退回“全有或全无”的失败形态。本次 P-1 现场 `:654148 FSCP tile=99,106 fail=notSeg best=48` 是**单链头格**（veh=48 独占），`skip=multihead` 分支不触发，所以**不是 REG-359 的直接复发**，但两者共用一个“取格上某台车代表整格”的判据基因，修 C-1 时要一并复核。

## 六、复测判据

> 本轮无源码改动，下列判据是**给拍板后实现用的验收标准**，同时给出**当前基线读数**（用于确认“改完真的变了”，避免改完看不出差别）。

**R-1（对应 C-1，P-1）**
1. 第三方过路车（`ord=6 OT_GOTO_WAYPOINT`、`sf=1`）站在挂接路径上时，机车**不再长期站死**：日志中同一 `(veh, tile)` 的 `CPL-SAFE-FAIL` + `FSCP ... fail=notSeg` 连续出现不超过 N tick（N = 拍板时的超时值），随后出现 `CPL-BEST` 换到别的候选或 `CPL-RESERVE reserved=1`。
2. 不再出现“等待 22000 tick 才挂上”的形态：基线 = `645052 RESV-BEGIN`（tick 11550 出库）→ `669761 GEO settle-couple`（tick 33590，`veh=51 tile=49,116 segid=3`），**改后该间隔应显著缩短**，且中间 `669424 RESV-FOREIGN-RES` / `669426 CPL-ORIGIN-FOLLOW ok=0 st=1` 的连续段应变短。
3. 负判据：路真的被永久占用时（另一列车停在同一格且订单非挂接），仍必须**不移动**（不得为了“不等”而穿模）。

**R-2（对应 C-2，P-2）**
1. 解挂出同格双链头的场景（`CheckTrainsOnTrack` 取到最小 ID 那条链）中，第三方机车挂后链时：若前链仍压在同格且未脱离，必须**拒挂**（或先分离），日志应出现新的拒绝探针；不得出现 `COUPLE-OK` 后两列车体坐标重合。
2. 正判据（不得误伤）：正常解挂缝（seam to seam、两侧订单类型不同的低速豁免场景）仍能通过，`GEO settle-decouple-*` 的 `n` 与改前一致。
3. 基线：本次同格判定取最小 ID 的行为由 `pbs.cpp:1422-1423` 注释锁定，改后若要保留反 desync 语义，必须仍返回**确定性**结果（不能改成“随机选一条”）。

**R-3（对应 C-3，P-3）**
1. `GOTO_COUPLE` 机车的 `RESV-*` 链在遇到信号或他人预留时应**分段出现**（每 tick 只订到边界），不再出现“一次 `R3RReservePathSequence` 订完整条 `r3r_resv_path`”的单行形态。
2. 与普通路径的对照必须收敛：普通路径现有 `RESV-EXT-PRE ... doRes=1` → `EXTEND-BACKOUT ... bad=1` 的逐格形态应成为**两种路径共同**的形态。
3. 负判据：分段后不得出现“每 tick 反复订/撤”的抖动（同一格 `RESV-BEGIN` 与 `CLEAR` 交替超过 2 次/秒即为失败）；`COUPLE-OK` 的成功率不得下降（用同一存档跑同样时长对比）。

**R-4（对应 C-4，P-4；**已按 2026-11-06 答复改写**）**

> 旧版 R-4 写的是“区间内的手工命令必须被执行”（C-4 旧 B/C 口径），与玩家新口径冲突，已作废。玩家口径：「空气区间对于自然推进和手动跳过都是空气，直接跳到 COUPLE_END」。详细缺口见 §7.3（D-2）。

1. 自然推进与手动跳过**同语义**：两者命中区间后都必须一路扫到 `OT_COUPLE_END`，`cur_real_order_index = (end_idx+1)%num`（现行 `train_cmd.cpp:6965-6971`），区间内的手工命令**一律不执行**。
2. 手动跳过必须**当场**即跳到区间末尾，不允许出现“先落到区间内的参考命令、再由下一 tick 的 `R3RSkipCoupleAirRanges()` 纠正”的两段式（现行 `CmdSkipToOrder`（`order_cmd.cpp:2171-2225`）就是这个两段式，见 §7.3）。
3. 两条路径对 `cur_timetable_order_index` 的处置必须一致（现行手动跳把它置 `INVALID`（`:2202`），自动跳是“非 INVALID 才跟随”（`:6988`））。
4. 负判据：区间外的普通命令照旧逐条执行，不得因引入“手动也跳”而把区间外命令一起吞掉。

---

## 七、C-1 / C-4 的落地缺口（U-393-B、D-1、D-2）

> 本节是 §5 两个悬空引用的实体（§5 写“见 §7”）。**本轮仍未改任何源码。**

### 7.1 U-393-B（疑点，未销项）：C-1「卡在车库里面不出库」与日志证据冲突

- **玩家口径（2026-11-06）**：「C1，我想是我表达或你理解有误，现在机车还是卡在车库里面不出库」。⇒ 症状是**库内不出发**，不是“出库后挂不上”。
- **我方证据（`build\R3R_debug.log`，2026-11-05 22:25:39，95 MB / 680 406 行）指向 veh=51 已出库**：
  - `645052 RESV-BEGIN veh=51 ... tile=62,117 mf=62,117 ... order=16`
  - `645060 TRP veh=51 ... type=16 => ok=1 res=1`
  - `645488 ... real=1(6) 40,115 ...`（同一 veh=51 的坐标已变成 40,115）
- **为什么仍不判定 C-1 有误（三条硬理由）**：
  1. `TRP ok=1 res=1` 只证明**寻路+预留成功**，出库还要过 `CheckTrainStayInDepot()` 的 `:15415` 之后的落地动作（`:15421 SetDepotReservation` → `:15429-15435` 改写 `moving_front->track` 并把 `cur_speed` 归零）；只凭 TRP 不能证明“离开库格”。
  2. 未核 `645052` / `645060` 里的 `veh=51` 是否就是玩家此刻指着的那列车。玩家现场完全可能是另一列车、另一个库（本次日志里库格 `62,117` 只有 veh=51）。
  3. 日志是 11-05 22:25 的，玩家的“现在”可能已是**新的会话**（11-06），我们没有那份日志。
- **销项所需的最小取证（下一次会话只做这一件事即可）**：让卡住的那列车保持卡住 10 秒以上，然后按 `veh=<id>` grep 下列行：
  | 需要的行 | 出处（已核） | 用途 |
  |---|---|---|
  | `DEPOTBLK veh=...` | `train_cmd.cpp:22356` | 一次拿到 `track/eng/isdepot/ontile/idmatch/pend/stopped/parked/spd/destRaw/depotIdx` |
  | `depotstay-in` 基线 | `train_cmd.cpp:15257`（`R3RWatchOrderIndex`） | 有该行 ⇒ 确实进了 `CheckTrainStayInDepot`；没有 ⇒ 被 `TrainLocoHandler` 的 Stopped 早退挡在外面 |
  | `post-depotblk` 基线 | `train_cmd.cpp:22459` | 库内挂接块的出口快照 |
  | `TRP veh=... type=16 => ok=? res=?` | — | 出库预留结果 |
  | `STUCK veh=...` | — | 是否置了 `VehicleRailFlag::Stuck`（界面“等待空余轨道”的唯一来源） |
  | 该车所在库格坐标 + GOTO_COUPLE 的目标（本库 / 另一库 / 站台车底） | 现场 | 决定走 D-1 的哪一支 |
- **判读表（先写好，下次直接查表，避免重跑取证）**：
  | 现场读数 | 命中支路 | 结论 |
  |---|---|---|
  | `DEPOTBLK ... idmatch=1` + 有 `depotstay-in` + 无 `TRP` | D-1a | 设计内停驻（到本库等挂），问题转移到“目标车底为何不进这个库” |
  | `DEPOTBLK ... idmatch=1` + `COUPLE-*` 失败刷屏、无 `COUPLE-OK` | D-1b | **每 tick 强制 `cur_speed=0` 永久停在库内**，且不置 Stuck ⇒ 与“等待空余轨道”文字可能对不上 |
  | `idmatch=0` + `STUCK` + 每 37 tick 一次 `TRP ... ok=0` | D-1c | 出库口被 `seg_state==Full` / `HasDepotReservation` 或 `TryPathReserve` 失败挡死 |
  | `DEPOTBLK ... ontile=1 track!=80`（非 `TRACK_BIT_DEPOT`） | D-1d | 链没全进库，库内挂接块从未执行（`:22375` 要求 `R3RChainFullyInDepot`） |
  | `DEPOTBLK ... eng=1 spd=0 stopped=1` 且该链是**假引擎/纯车底** | D-1e | `:15292` 的动力闸门误判 |

### 7.2 D-1：六条「库内不出库」候选机制（代码全部已核，尚未定哪一条）

- **D-1a 到本库等挂（设计内）**：`train_cmd.cpp:15278-15281` —— `OT_GOTO_COUPLE && GetCoupleIsDepot() && IsRailDepotTile(v->tile) && 目标库ID == 本库ID && R3RChainFullyInDepot(v)` ⇒ `return true`。这是“我已到目标库，等车底被送来”的正常形态，**不是卡死**。
- **D-1b 库内挂接块永久停驻（头号嫌疑）**：`:22375` 要求 `track == TRACK_BIT_DEPOT && IsEngine() && R3RChainFullyInDepot()`；`:22386-22390` 的 `couple_targets_this_depot`（= `r3r_pending_depot_couple` 或“GOTO_COUPLE 目标是本库”）成立时，`:22433-22435` **每 tick 无条件** `cur_speed = 0; progress = 0;`，只有真的耦合成功才在 `:22421-22427` 清 `r3r_parked`。⇒ 只要目标链**不在本库**（或全被 `R3RCanCoupleNow()` 拒），这台机车就**永远停在库内**，且因为 `:22417-22419` 只是“尝试期间临时清 Stopped”，失败后 `:22436` 又恢复，界面未必显示“等待空余轨道”。注释 `:22376-22385` 自陈这是为修“HOME 库被锁死”而加的 `couple_targets_this_depot` 收窄，收窄后仍漏“目标是另一库/站台但 `r3r_pending_depot_couple` 意外为真”的情形。
- **D-1c 出库口被挡（界面“等待空余轨道”的字面来源）**：`:15326-15328` 的 `wait_counter < 37` 只是 37 tick 延迟；`:15333-15336` 的 `seg_state == SigSegState::Full || HasDepotReservation(v->tile)` ⇒ `exit_blocked` ⇒ `:15412 return true`（**永久**，若库格预留泄漏）；`:15415-15418` 的 `TryPathReserve` 失败 ⇒ `MarkTrainAsStuck` ⇒ `return true`。只有后两者会长期不动且置 Stuck。
- **D-1d 链没全进库**：`:15284-15286` 任一节 `track != TRACK_BIT_DEPOT || tile != v->tile` ⇒ `return false`（交回正常移动代码）；配合 `:22375` 的 `R3RChainFullyInDepot` 条件，若某节卡在库外（或停在另一个库格），库内挂接块**永不执行**，而 `:15278` 也不成立 ⇒ 机车既不挂也不出，`cur_speed` 走正常移动分支却无有效目标。
- **D-1e 动力闸门误判**：`:15292` `cached_power == 0 && !R3RIsCarOnlyFormation(v)` ⇒ 置 `VehState::Stopped` 并 `return true`（永久）。若某条链（假引擎/打散过铰接的车底）被误判成非 car-only 且 `cached_power==0`，同样永久停在库内。
- **D-1f 耦合成功后提前 return**：`:22448-22451` `!consist->IsFrontEngine()` ⇒ `return true`（正常，换头后由新链头接管），不是缺陷。

### 7.3 D-2（C-4 落地缺口）：手动跳过**已**被下一 tick 纠正，但差在“当场”与时刻表

- **事实 1（手动路径不调空气跳）**：`CmdSkipToOrder`（`src\order_cmd.cpp:2171`）只做 `cur_implicit_order_index = cur_real_order_index = sel_ord`（`:2200`）与 `cur_timetable_order_index = INVALID_VEH_ORDER_ID`（`:2202`），**没有**调用 `R3RSkipCoupleAirRanges`。
- **事实 2（空气跳只有两个调用点）**：`R3RSkipCoupleAirRanges`（定义 `train_cmd.cpp:6931`）只在 `train_cmd.cpp:21995`（`TrainLocoHandler` 入口，每 tick 无条件）与 `:7122`（提交点）被调用。
- **⇒ 净效果上 C-4 已经成立**：手动跳进空气区间后，**下一个 tick** `:21995` 会把它扫到 `OT_COUPLE_END` 之后（`cur_real_order_index = (end_idx+1)%num`）。所以“空气就是空气”并不需要新语义，只需要**补齐两点不一致**：
  1. **当场不一致（可见）**：命令执行后的那一 tick 内索引仍停在区间内的参考命令上，随后被空气跳挪走 ⇒ 玩家看到“跳了又自己跳一次”，日志里会出现 `COUPLE-AIR-SKIP from=<手工目标> to=<end+1>`。
  2. **时刻表语义不一致**：手动跳到 `:2202` 直接清 `INVALID`，自动跳是“非 INVALID 才跟随”（`train_cmd.cpp:6982-6984` 的无条件对齐在此之后）⇒ 两条路径对时刻表的处置不同。
  3. **GUI 的“跳过”按钮自己算 `cur+1`**（`order_gui.cpp` 的 `WID_O_SKIP` 路径）⇒ 点击会先落到区间内，再靠下一 tick 纠正；期间 `CmdSkipToOrder` 自身对 `OT_WAITING`/loading 做的清理（`:2204` 起的“discard the stale current order”块）会先跑一次。
- **建议落点（仍不改码，等实现轮）**：在 `order_cmd.cpp:2200` 之后、`UpdateRealOrderIndex()` 之前插一次 `R3RSkipCoupleAirRanges(Train::From(v))`；并把 `:2202` 改成“跳过没改变 `cur_real_order_index` 才置 INVALID”，使手动与自动对时刻表的语义一致。

---

## 八、第 394 轮（2026-11-06）：veh=51 卡库现场取证 —— 真凶 `RESV-NOBOOK reason=depot-paired`

> 本轮仍是**只读取证、零源码改动、零构建**。输入 = `build\R3R_debug.log`，1 151 268 B / 12 311 行 / mtime `2026-11-06 12:33:19`。

### 8.1 现场读数（veh=51 末段行为，逐字）

`veh=51` 全日志命中 1034 行，首行 `:5`、末行 `:12311`（**日志最后一行就是它**）。末段是稳定死循环，三个周期样本：

| 行号 | 原文 |
|---|---|
| `:12190` | `TRP veh=51 mf=62,117 real=7 type=16 dest=14897 spd=0 fp=0 look=0 one=0 mstuck=0 fto=0 => ok=0 res=0` |
| `:12191` | `RESV-NOBOOK veh=51 tile=62,117 order=16 caronly=0 reason=depot-paired` |
| `:12198/:12204/:12205` | 同形态 `RESV-NOBOOK ... reason=depot-paired`（同一 tick 内多次） |
| `:12207` | `DEPOTBLK veh=51 tile=62,117 track=80 eng=1 isdepot=0 ontile=1 idmatch=0 pend=0 stopped=0 parked=0 spd=0 destRaw=9 depotIdx=3` |
| `:12231` | `RESV-BEGIN veh=51 tick=22586 tile=62,117 mf=62,117 td=0 spd=0 real=7 order=16 one=0 look=0 prevEnd=-1,-1 prevEndPos=-1` |
| `:12232` | `TRP veh=51 ... => ok=0 res=0` |
| `:12242` | `DEPOTBLK veh=51 ... track=80 ... stopped=0 parked=0 spd=0`（tick 22660） |
| `:12306` | `RESV-BEGIN veh=51 tick=22752 ...`（**周期 ≈ 74~92 tick，永远同一个坐标**） |
| `:12311` | `RESV-NOBOOK veh=51 tile=62,117 order=16 caronly=0 reason=depot-paired`（**日志最后一行**） |

不变量：`tile=62,117` 从头到尾没变；`track=80`=`TRACK_BIT_DEPOT`（**确实在库格内、链已全进库**）；`eng=1`；`spd=0`；`real=7`、`order=16`（`OT_GOTO_COUPLE`）不变；`mstuck=0`（**从不置 Stuck** ⇒ 界面不会有“等待空余轨道”）。每次尝试都以 `TRP ... ok=0 res=0` + `RESV-NOBOOK reason=depot-paired` 收场。

### 8.2 真凶：新增第七支 **D-1g = `depot-paired` 永久拒订路**

代码锚点（`src\train_cmd.cpp`，均已逐行读过）：

- 拒绝发生在 **`TryPathReserveWithResultFlags()`** 里的总门：`:18052` `if (R3RIsParkedWaiting(consist, &r3r_nobook_reason))` ⇒ `:18055-18058` 打 `RESV-NOBOOK ... reason=%s` ⇒ `:18060` `return TPRRF_NONE`。
- `R3RIsParkedWaiting()` 的**第三支**（本现场命中的那一支）在 `:18252-18255`：
  ```
  if (r3r_goto_couple && r3r_in_depot && r3r_partner != nullptr) {
      *reason = "depot-paired";
      return true;      // 拒绝一切路线预留
  }
  ```
  其注释 `:18241-18251` 自陈设计意图：「库内挂接是 `tb=80` 吸附 + **同格直接 `Couple()`**，这条路线永远不会被走完……**一旦配对已落定，库内就不再需要任何路线预留**」。
- ⇒ 该支的**隐含前提**是“配对锁已建立 ⇒ 对端就在本库/本格 ⇒ 马上会吸附耦合”。现场 `:12207` 的 `isdepot=0 / idmatch=0 / pend=0 / parked=0` 说明：**订单的挂接目标是“车底（非库）”，既不是 `r3r_pending_depot_couple`，也不是“到本库等挂”**，但 `r3r_partner != nullptr` 仍成立 ⇒ 前提被打破，门却照旧关死。

**为什么这一支能完全卡住（三条锁同时成立）**：
1. `RESV-NOBOOK` 只拒**路线预留**，不动既有预留与几何命中 —— 所以不会有任何“失败/放弃”信号，`mstuck` 恒 0；
2. `cur_speed` 本来就是 0（本来就没路），于是 `TrainLocoHandler` 走停驶分支，`TrainCoupleHandler` 只做几何命中/候选扫描，**不做寻路**；
3. 几何命中要求两链在库里同格吸附，而对端不在这台车的库格 ⇒ 永远命中不到 ⇒ 死循环。

### 8.3 与 §7.2 六条候选的判定（**逐条排除，只留 D-1g**）

| 候选 | 现场读数 | 判定 |
|---|---|---|
| D-1a 到本库等挂 | `isdepot=0`（`GetCoupleIsDepot()` 假）、`idmatch=0` | **排除**（没走到 `:15278`） |
| D-1b 库内挂接块强制 `cur_speed=0` | `pend=0 parked=0` ⇒ `couple_targets_this_depot` 不成立 | **排除**（该块未执行；`spd=0` 是“没路可走”的结果，不是被强制） |
| D-1c 出库口被挡 / `TryPathReserve` 失败置 Stuck | `mstuck=0`、无 `exit_blocked` 形态；`TRP ok=0` 是**门拒绝**导致，与 `seg_state`/`HasDepotReservation` 无关 | **排除**（“等待空余轨道”文本本现场**根本没有**） |
| D-1d 链没全进库 | `track=80`=`TRACK_BIT_DEPOT`、`ontile=1` | **排除** |
| D-1e 动力闸门 | `eng=1` 真引擎；未置 `Stopped`（`stopped=0`） | **排除** |
| D-1f 耦合成功后提前 return | 无 `COUPLE-OK` | **排除** |
| **D-1g（本轮新增）** | `RESV-NOBOOK ... reason=depot-paired` 每 tick 重复、`TRP ok=0 res=0`、坐标/spd 不变 | **命中** |

### 8.4 U-393-B 状态更新

- 从「待取证」升级为「**机制已定位（D-1g），待确认对端位置与配对锁来源**」。
- 仍缺的一条：**veh=51 的配对锁对端是谁、它在哪**。日志里有现成探针可查：`CPL-PAIRSCAN tick=... act=51 ... tgt=<id> tgtTile=x,y tgtOrd= tgtSpd=` 与 `CPL-PAIRLOCK ... act=51 ... tgt=<id> tgtTile=x,y`。旁证同族形态：`veh=54` 在 `1,15` 库内、`ord=16`、`paired=1`、`tgt=0`，同样吃 `RESV-NOBOOK reason=depot-paired`（`tick=11532` 那一组）⇒ **不是 veh=51 单例**。
- 修法方向（**未实现，等拍板**）：`depot-paired` 支必须补一个“对端确实在本库格（或本库）”的条件，否则应退回“允许订路/出库去接车底”。最保守的补法是在 `:18252` 加 `&& (R3RGetCouplePartnerTile(consist) == consist->tile || R3RPartnerIsInSameDepot(consist))`；等价语义 = 只有“同格吸附耦合即将发生”时才允许关死这条车的路线。

### 8.5 复测判据 R-5（新）

1. 正判据：`veh=51` 不再出现连续 `RESV-NOBOOK ... reason=depot-paired`；应出现 `TRP veh=51 ... => ok=1 res=1` 并离开 `62,117`。
2. 负判据（不得误伤）：**对端真的同库同格**时，仍应保持“库内不订路”的既有形态（不出现该库格的 `RESV-BEGIN/TRP`），吸附耦合照旧 `COUPLE-OK`。
3. `mstuck` 仍应保持 0 或按新语义合理出现；不得因放行而出现“库内乱窜”或 `rail_map.h:168` 类断言。

### 8.6 对端身份取证：U-393-B 销项（2026-11-06，日志末段）

**日志元数据**：`build\R3R_debug.log` 后段，末行即循环行；`veh=51` 末段行为如下（行号均为该文件行号）。

1. **配对锁对象已查明**（`:11744`）：
   ```
   CPL-PAIRLOCK why=scan act=51 ... tgt=6 tgtTile=44,116 tgtOrd=17 tgtSpd=0 tgtLen=83 ... actPairT=6 actPairR=1048575 tgtPairT=1048575 tgtPairR=51
   ```
   ⇒ 51 的配对目标是 **veh=6**（一条 83 节 `car-only` 车底，订单 `17`=`OT_WAIT_COUPLE`），停在 **`44,116`**；`tgtPairR=51` 说明对端也认可。`tick=21828`。

2. **对端所在格不是库**：`RESERVECONSIST veh=6 tile=44,116 track=0x1` ⇒ `TRACK_X` 普通轨道；全日志无 `DEPOTBLK veh=6`。且末段 `SVC-DEPOT-SKIP veh=6 cur=17 real=17(17) tag=couple-protocol` 说明它一直在原地等挂、没有去库。

3. **51 自己为什么在库里**（`:11724-11738`）：
   - `:11724/:11725` 借来的 33 条隐式订单被删回自有 10 条表（`curType=2`、`dest=15038`=`62,117`、`real=6`）；
   - `:11726` `SPDD-LOST veh=51 tile=62,117 track=80` ⇒ 整链已在库内（库 3）；
   - `:11737` 订单推进 `real 6->7` ⇒ `curType=16`（`GOTO_COUPLE`）、`dest=14508`=`49,116`、`dtX=44 dtY=113`；
   - `:11738` `DEPOT-ARR ... tile=62,117 dest=44,113 destDepot=0` ⇒ **挂接目的地是车站 9，不是本库**。
   ⇒ 它的时刻表就是「库 3 整备（idx=6 `type=2` `dest=3`）→ 站 9 挂车（idx=7 `type=16` `dest=9`）」，行为完全正确；**一次都没能开出库**。

4. **锁与门同 tick 咬合**：`:11740` `CPL-PAIRSCAN ... state=locked best=6 bestDist=19` → `:11741` `RESV-BEGIN veh=51 tile=62,117 track=80` → `:11742` `TRP ... => ok=0 res=0` → `:11743` `RESV-NOBOOK veh=51 ... reason=depot-paired`。此后每 ~90 tick 重复，**末行（`:12311`）仍是这一组**。

5. **否定面**：`isdepot=0`（订单不是库内挂接）、`pend=0 parked=0`（无 `r3r_pending_depot_couple`）、`idmatch=0` ⇒ `train_cmd.cpp:22386-22391` 的库内挂接块 `couple_targets_this_depot` **不成立、根本没执行**（与 §8.3 的 D-1b 排除一致）。

**结论（U-393-B 销项）**：`train_cmd.cpp` 的 `R3RIsParkedWaiting()`（`:18141`）里 `depot-paired` 支（`:18253`）**只判 `r3r_in_depot && r3r_partner != nullptr`，不判对端在哪**，而它守护的出库口 `:18439` 是**唯一**能让库内列车订路的门 ⇒ 只要配对锁的对象在库外（本例：`44,116` 普通轨道 18 格外的等待车底），这台车就**永久卡在库内**。门的前提（“配对已落定 ⇒ 本库内马上吸附”）只在“对端同库同格”时成立。

### 8.7 修法方案（D-1g，**已于 2026-11-06 落地，见 §8.8**）

- **单一事实来源**：`:22386-22391` 已经有一份“这对车会不会在本库内吸附”的判据 `couple_targets_this_depot`（= `IsRailDepotTile && (pend || (cur_type == OT_GOTO_COUPLE && GetCoupleIsDepot() && dest == GetDepotIndex(tile)))`）。门（`:18253`）应该复用**同一份判据**，而不是只看 `partner != nullptr`。
- **补丁（草案，3 行）**：把 `:18253` 的
  ```cpp
  if (r3r_goto_couple && r3r_in_depot && r3r_partner != nullptr) {
  ```
  改成
  ```cpp
  if (r3r_goto_couple && r3r_in_depot && r3r_partner != nullptr &&
      R3RDepotCoupleAppliesToThisDepot(consist)) {
  ```
  其中新 helper（文件内 static，放在 `:18141` 之前）= 抽自 `:22386-22391` 的同一段判定；`consist->track == TRACK_BIT_DEPOT` 已保证 `IsRailDepotTile` 成立。
- **为什么不选“判对端同库”**：`R3RGetCouplePartnerTile()` 之类要额外查车，且仍会漏掉 `pend` 形态；而“本库挂接块是否会接管”本来就是这条门想表达的意思。两者在本例（`isdepot=0`）结论相同：都放行。
- **风险与边界**：
  1. 放行后 51 会正常订路出库 → 驶向 `44,113` 一带与 veh=6 吸附；若配对在中途失效，走既有 `CPL-PAIR*` 失锁路径，不新增逻辑。
  2. 真正同库同格的库内挂接（KI-481 口径）**逐字不变**（`pend`/`GetCoupleIsDepot`+`idmatch` 仍为真 ⇒ 仍关死）。
  3. 与 C-3=A 的预留粒度改造正交，可先落地本补丁。
- **复测判据**：见 §8.5 R-5（正判据 `veh=51` 出库；负判据同库同格仍不订路）。

### 8.8 D-1g 落地实现（2026-11-06，玩家拍板原话：「可以按照你推荐的落地」）

**改动范围**：仅 `src/train_cmd.cpp`（未碰任何 `src\*.h` ⇒ 增量构建合法）。

1. **新增单源判据** `static bool R3RDepotCoupleAppliesToThisDepot(const Train *consist)`，插在 `R3RIsParkedWaiting()` 之前（原 `:18141` 位置，带完整 D-1g 注释）。**两支都与库内挂接块逐字同源**：
   ```cpp
   if (!IsRailDepotTile(consist->tile)) return false;
   const DepotID this_depot = GetDepotIndex(consist->tile);
   /* 支一 = 库内挂接块的 couple_targets_this_depot */
   if (consist->current_order.IsType(OT_GOTO_COUPLE) &&
           consist->current_order.GetCoupleIsDepot() &&
           consist->current_order.GetDestination().ToDepotID() == this_depot) return true;
   /* 支二 = 库内挂接块的 r3r_pending_depot_couple（KI-182，必须按有效表读） */
   if (!consist->IsEngine() || !consist->r3r_parked) return false;
   const Order *pending = (consist->GetEffectiveOrders() != nullptr) ?
           consist->GetOrder(consist->cur_real_order_index) : nullptr;
   return pending != nullptr && pending->IsType(OT_GOTO_COUPLE) &&
           pending->GetCoupleIsDepot() &&
           pending->GetDestination().ToDepotID() == this_depot;
   ```
   **首编失败与订正（2026-11-06 18:0x）**：初版写成 `if (consist->r3r_pending_depot_couple) return true;`，把它误当成 `Train` 成员 ⇒ `train_cmd.cpp(18162): error C2039: "r3r_pending_depot_couple": 不是 "Train" 的成员`，`done=EXIT_CODE=1`、`ninja: build stopped`（日志 `build\R3R_incbuild.log`，14:25:58）。实际它是 `CheckTrainStayInDepot()` 内的**局部 bool**（`train_cmd.cpp:22299-22302`，`IsEngine && r3r_parked && 有效表当前命令 == GOTO_COUPLE(depot 本库)`），同名的 `r3r_pend` 也是局部指针（`:22298`）。订正为上面两支后重编：`[3/3] Linking`、`done=EXIT_CODE=0 @18:17:34`、`train_cmd.cpp@18:15:09 → obj@18:16:06 → build\openttd.exe@18:17:30`、`read_lints 0`。

2. **闸门追加条件**（`depot-paired` 支，原 `:18280`）：
   ```cpp
   if (r3r_goto_couple && r3r_in_depot && r3r_partner != nullptr &&
           R3RDepotCoupleAppliesToThisDepot(consist)) { ... }
   ```
   并在上方第 306 轮注释块尾部补 D-1g 段（记录 veh=51 / veh=6 现场、为何 `depot-paired` 必须是“同源判据”）。
3. **库内挂接块改为调用同一函数**（原 `:22386-22390`）：
   ```cpp
   const bool couple_targets_this_depot = R3RDepotCoupleAppliesToThisDepot(consist);
   ```
   这样“关路”与“接管”两处**永久同源**，不会再分叉成两本账（同第 318 轮教训）。

**行为差异面（穷举）**：
| 库内 GOTO_COUPLE 且已有配对锁 | 旧 | 新 |
|---|---|---|
| 目的地 = 本库 depot（KI-481/`COUPLE-DEPOT-DIRECT`） | 关路 | **关路（逐字不变）** |
| `r3r_pending_depot_couple`（玩家停进库收车底） | 关路 | **关路（不变）** |
| 目的地 = 车站站台 / 别的 depot（本例 veh=51） | **关路（卡死）** | **放行走出库** |
| 无配对锁 | 走 `parked-no-pair` 支 | 不变 |

**风险复核**：放行后该车走正常出库预留 → 出库后 `r3r_in_depot` 为假、本支自然失效；配对若中途失锁走既有 `CPL-PAIR*` 路径；不新增任何状态字段、不改存档。

**复测判据**：§8.5 R-5 三条（正判据 `veh=51` 出现 `TRP ... ok=1 res=1` 并离开 `62,117`；负判据同库同格仍无 `RESV-BEGIN`、照旧 `COUPLE-OK`；不得出现“库内乱窜”/`rail_map.h:168` 断言）。

## 8.9 KI-563 落地：挂接预留改为原生粒度（订到第一个失败格即停）

**玩家拍板原文（C-3）**：「选 A —— 预留粒度完全照原生：遇信号或他人预留边界即停，下一个 tick 续订；不再一次性铺到底。」

**改动（仅 `src\pathfinder\yapf\yapf_rail.cpp`，未碰任何 `src\*.h`）**：

1. `CYapfReserveTrack::R3RReservePathSequence()`（两遍法的第二遍，`:601` 起）：失败分支原来是「整批回滚 + `booked=0` + return false」，现在改为
   - `booked > 0` ⇒ 打 `CPL-RES-PARTIAL veh=%d booked=%u/%u stop=%d,%d td=%d`，`YapfNotifyTrackLayoutChange(INVALID_TILE, INVALID_TRACK)`，**return true（保留已订格）**；
   - `booked == 0` ⇒ 照旧 return false（一格都没订上，交 KI-316 的 `FreeTrainTrackReservation + MarkTrainAsStuck`）。
   - 函数头注释同步改写（"@return booked > 0 为 true（可能是部分预留）"）。
2. 调用点（`:1630-1644`）：`reserved` 为真且 `0 < r3r_booked < r3r_resv_path.size()` 时，把 `SetReservationTarget()` 从原 `r3r_resv_path.back()` **收到 `r3r_resv_path[r3r_booked - 1]`** —— 否则后续逻辑（`ReserveSingleTrack` 的 dest 比较、`ExtendReservation`）会以为整条到会合点都在自己名下。
   - 全成功时行为逐字不变（`r3r_booked == size()` ⇒ 不收缩）。

**为什么这是"原生粒度"而不只是"部分成功"**：普通路径的 `TryReservePath()` 本身就是逐节点 `ExtendTrainReservation()`、遇 `choice-reserved` 就 `EXTEND-BACKOUT`（同一份日志里 `RESV-EXT-PRE → EXTEND-BACKOUT bad=1` 可对照）；只有 R3R 单段挂接这条两遍法把它变成"全有或全无"。改后形态与原生一致：**订一段 → 走一段 → 在信号/他人预留前停 → 下一 tick 续订**。
- 续订为什么不会自锁：已订格再订时走 `pbs.cpp:429 TryReserveRailTrackdir()` 的**自订豁免**（KI-361），返回 true 而非 false；因此下一次 `rp()` 重摸整条路径时，前面已订的格不会再成为失败点。

**非单段分支（`:1637 TryReservePath(nullptr, pNode->GetLastTile(), true)`）本轮未改**：该分支调用的就是原生 `TryReservePath()`，其内部本就是逐节点延伸 + 冲突回退，不存在"一次铺到底"的问题；`r3r_resv_path` 的"一次订完"只发生在单段分支。

**行为差异面**：
| 现场 | 旧 | 新 |
|---|---|---|
| 挂接路径中途遇他人预留/红灯 | 整批回滚 → `CPL-RESERVE reserved=0 booked=0`、车一动不动 | 保留已订格 → `reserved=1 booked=K<N`、车走到第 K 格 |
| 一格都订不上（脚下都被占） | 回滚、失败 | 不变（失败） |
| 路径全程可订 | 全订 | 逐字不变（`booked == path`，不动 `res_dest`） |

**复测判据**：
1. 正判据：同场景出现 `CPL-RES-PARTIAL veh=… booked=K/N stop=x,y` 且 `CPL-RESERVE reserved=1 booked=K`（K>0），列车**开始移动**（`LOCO-MOVE`/`TRP ... ok=1`），不再 533 次 `CPL-RES-FAIL` 后原地不动。
2. 正判据：黑色预留预览只铺到 `stop=x,y` 前后，**不越过**沿途信号灯。
3. 正判据：下一次 tick 继续 `CPL-RESERVE ... booked` 递增（续订），直到 `booked == path`（全订）。
4. 负判据：路径全程空闲时行为逐字不变（无 `CPL-RES-PARTIAL` 行、`CPL-RES-OK total` 与旧日志一致、`COUPLE-OK` 照旧）。
5. 负判据：一格都订不上时仍 `reserved=0`、仍走 KI-316 的 `MarkTrainAsStuck`（不得出现"reserved=1 而实际零格"的空真）。

**构建自证（2026-11-06 18:22）**：`R3R_fullbuild.cmd inc detach`；`build\R3R_incbuild.done` = `EXIT_CODE=0`（18:22:12）；日志尾部 `[3/3] Linking CXX executable openttd.exe`、`==== R3R build ended 2026/11/06 18:22:12 MODE=inc EXIT_CODE=0`；`src\pathfinder\yapf\yapf_rail.cpp` 18:20:01 → `yapf_rail.cpp.obj` 18:20:43 → `build\openttd.exe` 18:22:04（52 453 376 B）；`read_lints` 0 条。<br>本轮未新增语言串；新增日志字面量 `CPL-RES-PARTIAL` 已随 exe 一起产出。

## 8.10 KI-562 三条候选修法（待玩家拍板，编号 C-562-A/B/C）

> 前一轮在对话里问过 A/B/C 但未落盘，违反"问题清单必须写入备忘"的规则，此处补齐定义。**三条均为待实现，本轮未改源码。**

根因五段（见 KI-562 条目逐条行号）：`pbs.cpp:1401 CheckTrainsOnTrack → :1422-1423` 恒取最小 ID（注释 `ALWAYS return the lowest ID (anti-desync!)`，属反 desync 冻结区，不能动）；`train_cmd.cpp:14461 GetCouplePosition → :14468 First() → :14474 !R3RCanCoupleNow` 整格判"没有等待车"、不找同格另一条链；`R3RIsCoupleTarget()`（`:2680-2728`）不含"该格是否已被别的链占用未脱离"；`TryTrainCouple()`（`:11525` 起）提交层不枚举同格第三列车、不调 `CheckTrainCollision`。

**C-562-A（提交前同格守卫，最小侵入，推荐先做）**
落点：`train_cmd.cpp` 的 `TryTrainCouple()`，在 `bool ok = CheckTrainAttachment(head).Succeeded()`（`:12122`）**之前**、所有候选与 `COUPLE-REFUSE-STILL-FOLDED` 之后。
做法：以合并链（`head`）为对象，逐格收集 `VehiclesOnTile<VehicleType::Train>(tile)`，凡链头 `w->First()` 既不等于 `v`、也不等于 `u` 的**第三方链**，判"未脱离"= `w->First()->cur_speed == 0 && 该链任一节 tile == 合并链某节 tile`；命中即：`RestoreTrainBackup(original_src_head, original_dst_head)` + 必要的 `R3RUndoLogicalFlip` 回滚 + `R3RRefreshChainCaches` + 两侧 `ConsistChanged(CCF_ARRANGE)` + `return false`，并打 `COUPLE-REFUSE-OCCUPIED head= third= tile=x,y speed=`。
优点：完全不碰 `pbs.cpp`（反 desync 冻结区），可逐字复用 `COUPLE-REFUSE-STILL-FOLDED` / `REFUSE-REAL-ARTIC` 的回滚骨架。缺点：只防"撞"，不解决"前链明明卡住却仍以最小 ID 挡路"（那是 P-1/KI-561 家族）。

**C-562-B（命中层多候选枚举，治本）**
落点：`train_cmd.cpp:14461 GetCouplePosition()`。
做法：把 `Train::From(other_vehicle)->First()` 单候选改为遍历该格全部链头（`VehiclesOnTile`，`->First()` 去重），逐个试 `R3RCanCoupleNow(v, u, "geo")`；同时用 A 的同格重叠判定过滤"会与未脱离前链重叠"的候选。
优点：治"同格两条链只看得见最小 ID 那条"的真凶（与 REG-359-I 在 `FindSafeCouplePositionProc` 做的"整格集合 + 存在量化"同构）。缺点：命中层每帧多一次 `VehiclesOnTile` 遍历（格上车数 ≤4，量级可接受）；判定必须保持**确定性顺序**（按 `index` 升序取第一个通过者），否则重新引入 desync 风险。

**C-562-C（候选期就拒，让机车继续等）**
落点：`R3RIsCoupleTarget()`（`:2680-2728`）。
做法：加一道"本格是否有未脱离的其它链"判据，命中即返回 false ⇒ 后链不成为候选，机车继续 `WAIT_COUPLE` 等待，不进入挂接流程。
缺点（关键）：解挂刚完成的那一瞬间同格双链是**常态**（`GEO settle-decouple-*` 每次都是），会把正常解挂场景一起否掉，与 §5.1 的解挂缝豁免口径冲突。**不建议单独采用**，只能作为 B 的附加过滤。

**推荐顺序**：先 A（低风险、能立刻止住"重叠/撞毁"），再 B（治忽略）。C 只作为 B 内部的附加判据，不单独立项。

**复测判据（沿用 §六 R-2，补 3 条）**
1. 同格双链头 + 第三方机车来挂后链 ⇒ 出现 `COUPLE-REFUSE-OCCUPIED`，且**不出现** `COUPLE-OK` 后两列车体坐标重合。
2. 正判据（不得误伤）：正常单链等待位的挂接仍走原路径、`COUPLE-OK` 成功率与改前一致；`GEO settle-decouple-*` 的 `n` 逐字不变。
3. 确定性：`COUPLE-REFUSE-OCCUPIED` 里的 `third=` 在重放同一存档时必须每次相同（不得依赖遍历偶然顺序）。

## 8.11 KI-562 玩家新拍板（C-562-D）：数到发线链头数，仅单链头才放行

**玩家原话（2026-11-06）**：「我希望关于 KI-562，数当站台（到发线）的链头数，然后在只有一个链头的时候才给放行，不然算卡住」。

⇒ 这条**取代** §8.10 的 A/B/C 选型（A/B/C 作废，不落地）。口径不是"提交前拒挂"，而是**寻路侧的安全位判定**：到发线上有 ≥2 个链头时，该格判为不可通行 ⇒ 挂接寻路摸不到终点 ⇒ 机车原地不动（= 玩家说的"算卡住"），等前链脱离后自然恢复。

**落点（唯一）**：`src\pathfinder\yapf\yapf_rail.cpp` 的 `FindSafeCouplePositionProc()`（`:186` 起）。
- 该函数已经在数链头：`tile_heads[MAX_TILE_HEADS=4]` + `note_tile_head()`（`:202-221`），并且**对站台沿轴扫整条到发线**（`:232-238` 用 `IsCompatibleTrainStationTile` 逐格 `note_tile_head`），所以"到发线上的链头数"= `n_tile_heads`，不需要新增遍历。
- 旧判据（REG-359-I，`:241`）：`n_tile_heads >= 2 && tile_heads_have_target ⇒ return true`（**放行**、不当作终点）。
- 新判据（本轮）：`n_tile_heads >= 2 ⇒ return false`（**不放行**，与"卡住"一致），不再看 `tile_heads_have_target`；`n_tile_heads == 1` 时走原 `best` 判据，行为逐字不变。

**为什么旧判据是错的**：REG-359-I 当初的暂存语义基于「下一 tick 重入再扫，等那台车开走判据自然恢复」，但 KI-562 的现场恰恰是**前链不会开走**（它自己卡在同格等脱离）。于是"暂存"变成永久放行 ⇒ 寻路把该格当可通行、把后链当终点 ⇒ 命中最小的前链或拼到后链上 ⇒ 两列车重叠。按玩家口径：多链头 = 不可判定 = 卡住，比"猜一个"安全。

**不动的地方**：`R3RIsCoupleTarget()`（`train_cmd.cpp:2680`）与 `TryTrainCouple()` 一行未改 —— 本方案只在寻路侧收口，命中/提交层保持原样（若实测仍有重叠，再按 §8.10 的 A 补提交层守卫）。

**复测判据**：1. 同到发线双链头场景不再出现挂接成功的 `COUPLE-OK`（机车原地不动，`FSCP ... skip=multihead-blocked` 出现）；2. 前链脱离后（该线只剩 1 个链头）挂接自动恢复、`COUPLE-OK` 照常；3. 正判据（不得误伤）：正常单链等待位挂接路径逐字不变（无 `multihead-blocked`、`COUPLE-OK` 成功率与改前一致）；4. 不出现 `rail_map.h:168` / `pbs.cpp:144` 断言。

**构建自证（2026-11-06 18:55）**：`R3R_fullbuild.cmd inc detach`；`build\R3R_incbuild.done` = `EXIT_CODE=0`（18:55:11）；日志尾部 `[3/3] Linking CXX executable openttd.exe`、`==== R3R build ended 2026/11/06 18:55:11 MODE=inc EXIT_CODE=0`；`src\pathfinder\yapf\yapf_rail.cpp` 18:52:38 → `yapf_rail.cpp.obj` 18:53:43 → `build\openttd.exe` 18:54:59（52 453 376 B）；`read_lints` 0 条。

**复测结论（2026-11-07）：通过，KI-562 结案。** 玩家原话：「我向你报告，关于一格内有两链头的修复符合预期，可以结案」。

- 判定：C-562-D 生效 —— 到发线（站台沿轴整条 + 本格）链头数 ≥2 时不再放行，机车原地等（玩家口径的"算卡住"），等前链脱离、该线只剩单链头后恢复挂接。A/B/C 三条候选永久作废，不再落地。
- 已知边界（**不是本缺陷，勿再按 bug 排查**）：按此口径，若前链**永远不脱离**（自己卡死在同一到发线），该线的挂接会一直不放行 —— 这是"判不了就不猜"的预期行为，不再算 KI-562 复发。真出现时应在**前链为何不脱离**（解挂残留/自身寻路失败）上另立条目。
- 交叉：`R3RIsCoupleTarget()` / `TryTrainCouple()` 至今一行未改，若将来重现"两列车体坐标重合"，说明寻路侧收口不足，届时按备忘 §8.10 的 A（提交前同格守卫）补。

