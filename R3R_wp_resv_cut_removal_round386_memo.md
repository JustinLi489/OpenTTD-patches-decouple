# 第 386 轮临时分析报告：删除「预留能在路点截止」机制 + road vehicle 崩溃取证

日期：2026-11-02
状态：机制删除已落地（源码已改，待编译）；道路车辆崩溃仅取证，未改码。
关联 KI：KI-551（路点预留截断机制删除）、KI-552（road vehicle FatalError 崩溃）。

---

## 一、玩家原话（2026-11-02）

> 「我的朋友报告，最新版本中，列车会无条件把预留在路点截止，而不是我所说的"符合条件的列车"，
> 因此，我希望直接把之前的这个"预留能在路点截止"这个机制删除。此外，同样的存档，游戏跑着跑着
> 就因为 road vehicle 崩溃了，崩溃日志在 C 盘\Docs\OpenTTD，最新的那个就是」

两条互不相关的诉求：
1. **删除机制**（不再修补条件判断）——本报告 §二、§三。
2. **road vehicle 崩溃定位**——本报告 §四。

---

## 二、目标机制的确切落点（取证）

该机制 = KI-429「等待区路点截断」，第 276 轮引入、第 277/289/346 轮反复修补。
它由一处状态判定 + 两处"应用"组成：

| # | 位置 | 作用 |
|---|------|------|
| 1 | `src\train_cmd.cpp:14203` `R3RShouldCutCoupleAtWaitPoint()` | 状态判定：0=不截断 / 1=截断且终点=等待区路点 / 2=已停在路点上但未放行 |
| 2 | `src\pathfinder\yapf\yapf_rail.cpp:1215-1257` `FindNearestCoupleTrain()` 内 | **应用 A**：`SetCoupleWaitCut(r3r_wait_cut == 1, wp)`；`state==2` 直接 `return INVALID_TRACKDIR` |
| 3 | `src\pathfinder\yapf\yapf_destrail.hpp:427-452 / 460-485` | **应用 A 的实现**：`couple_wait_cut` 成员 + `SetCoupleWaitCut()` + `PfDetectDestination()` 只认等待区路点做终点 |
| 4 | `src\train_cmd.cpp:17371-17417` `TrainLocoHandler()` 的 `OT_GOTO_COUPLE` 分支 | **应用 B**：`r3r_wait_cut != 0` 时 `FreeTrainTrackReservation()` 把已有长预留整条清掉（KI-433） |

"无条件截止"的观感来源正是 #2 + #3：只要判定返回 1，YAPF 的终点集合被硬性收窄到路点，
回溯/预留必然止于路点；而判定本身的门槛（`R3RTrainStopsAtWaypoint` / 配对就绪 `pair_ready`）
在朋友那个存档里几乎恒不成立 ⇒ 表现为"无条件"。

## 三、删除方案（已落地）

原则：**只删"应用"，保留"状态判定"**。因为 `R3RShouldCutCoupleAtWaitPoint()` 还被两处
与她无关的功能使用，删掉会连带打断等待区停车：

- `src\train_cmd.cpp:850` `R3RWaitCoupleHeadStopPending()`：`return ... != 0;` —— 头停车闸门；
- `src\train_cmd.cpp:18322` 附近：`state==0 且 wait_wp!=0` 用作"越墙（overshoot）"判据。

具体改动：
1. `yapf_rail.cpp`：删 35-38 行的前向声明；删 1215-1257 的整块（注释、`r3r_cut_wp`、
   `CPL-WAITCUT` 边沿日志、`state==2` 早退、`SetCoupleWaitCut()` 调用）。保留
   `_r3r_couple_path_blocked = false;` 与 `Yapf().SetDestination(v);`。
2. `yapf_destrail.hpp`：删 `couple_wait_cut` / `couple_wait_cut_wp` 成员、`SetCoupleWaitCut()`、
   以及 `PfDetectDestination()` 里 `if (this->couple_wait_cut) { ... }` 整段（含
   `PFD-ACCEPT kind=waitwp` 探针）。判定落回原本的"站台/车库 + 已有预留"逻辑。
3. `train_cmd.cpp`：删 17372-17417（`r3r_wait_cut` 计算、`CPL-WAITCUT`、`CPL-WAITCUT-CLEAR`
   与 `FreeTrainTrackReservation()`）；连带删掉 17440/17444 对 `r3r_wait_cut` 的引用
   （`&& r3r_wait_cut == 0` 恒真 ⇒ 直接去掉，注释同步订正）。

删除后的行为（预期）：
- 机车前往 `OT_GOTO_COUPLE` 时按普通挂接寻路铺预留，可以一直铺到目标车底（不再止于路点）；
- 不再有"进截断第一 tick 清空长预留"的动作（KI-433 的抖动源一并消失）；
- 等待区路点头停车（`R3RTrainStopsAtWaypoint` / `R3RWaitCoupleHeadStopPending`）**保持不变**，
  车仍然会在路点停下等配对，只是预留继续向前延伸。
- 连带失效的探针字面量：`CPL-WAITCUT`、`CPL-WAITCUT-CLEAR`、`PFD-ACCEPT kind=waitwp`。

风险（已知、玩家已接受）：
- KI-429 原始现场（`FSCP tile=258,233` 交叉预留失败）可能回归 —— 那时长预留会伸进道岔区，
  表现为挂接寻路 `CPL-SAFE-FAIL` / 找不到路径。第 344 轮的"挂接寻路失败时改走普通寻路"
  兜底仍在线，能给出"到最近安全位为止"的路径，不会像 KI-316 那样原地钉死。
- KI-435（越过路点后截断终点在身后 ⇒ 永久停车）整类问题结构性消失。

---

## 四、road vehicle 崩溃取证（只取证，未改码）

### 4.1 日志位置更正

玩家说「C 盘\Docs\OpenTTD」，实际是本机文档目录：
`C:\Users\冯洁敏\Documents\OpenTTD\`（`[Environment]::GetFolderPath('MyDocuments')` + `OpenTTD`）。
该目录下有两份同日日志：

- `crash-20261101T171039Z.log`（17:10:39Z）
- `crash-20261101T173030Z.log`（17:30:30Z，最新）

### 4.2 两份日志签名一致

两份都是同一个 FatalError：

```
Crash reason: Exception E1212012
Message: Disconnecting road vehicle.
Location:  src\roadveh_cmd.cpp:1758
```

栈顶函数：`IndividualRoadVehicleController()`。

### 4.3 崩溃现场数据（以 17:30:30 那份为准）

```
veh: 39930   tile 78DB65 (7013,966)   st:A（articulated part）  state = wormhole
prev 39929   tile 7CDB65 (7013,998)
front 39928  tile 7CDB65 (7013,998)
```

- 报错点 = `FollowPreviousRoadVehicle()` 对**非车头**的车节返回 `INVALID_TRACKDIR`；
- 该车节 `v->tile` 与 `prev->tile` **相差 32 格**（同一 x，y 差 32）—— 典型的隧道/桥梁
  wormhole 两端格；即"前车已在 wormhole 另一端头格，尾节还在另一端"的错位状态；
- 触发点前的上下文行：`Within context: FollowPreviousRoadVehicle(v, prev, ...)`（取不到
  fully typed 的 tile 说明是在非普通道路格上走自定义桥头/隧道分支）。

### 4.4 与 R3R 的关系（重要）

`git log -- src/roadveh_cmd.cpp` 显示该文件**只有上游提交**，R3R 从未改动；`roadveh.h` /
`road_map.cpp` / `roadstop.cpp` 无未提交改动，`road_map.h` 仅有探针头 include 的 2+/3-。
⇒ 崩溃发生在**未被我方修改的上游代码**里。

因此当前定性：**上游 articulated road vehicle + wormhole（自定义桥头/隧道）数据不一致**，
不是 R3R 逻辑直接产生；但"同一存档 + 469 个 NewGRF"下 20 分钟内两次可复现，说明它在
这份存档里是稳定触发，不能当偶发忽略。

### 4.5 待验证项 / 下一步

- `U-1` 该 RV 是否刚完成"自定义桥头 → 隧道"的跨接（需要 `crash.dmp` 或复现时的
  `R3R_debug.log` 才能确认；当前日志只给了三节车的 tile）；
- `U-2` 两份日志之间是否有共同的"最后一次玩家操作"（拆路/改桥/换轨型）—— 若是拆路，
  属于"路在车下被改掉"的数据损坏，防御点应放在命令层而非控制器；
- `U-3` 是否 `front` 的 `state` 已离开 wormhole 而尾节仍按旧状态推进（链序错位）；
- `U-4` 崩溃前是否发生过 save/load（读档后 wormhole 状态重建）；
- `U-5` 该车组是否为**多节铰接**（st:A 只标了 39930，需确认 39929 是否也是 artic part）。

候选防御（**未实施，待玩家拍板**）：
- `D-1`（保守）：在 `roadveh_cmd.cpp:1754` 非车头分支的 `FatalError` 之前加一次"重同步"：
  若 `prev->tile` 与 `v->tile` 处于同一 wormhole 的两端，则把 `v` 直接对齐到 `prev`
  （`v->tile/state/x_pos/y_pos` 拷贝 + `UpdateDeltaXY`），并写一行诊断日志，不中断游戏。
- `D-2`（激进）：命令层禁止在"有 articulated RV 正在通过"的自定义桥头上改建/拆路。
- `D-3`（上游）：按 `Disconnecting road vehicle` 向上游 JGRPP 提 issue，附 `crash.dmp`。

---

## 五、构建自证

入口：`R3R_fullbuild.cmd inc detach`（复用既有，未新建任何 .cmd）。
- guard：`build\R3R_incbuild.guard.log` 判 `incremental is safe`（`yapf_destrail.hpp` 的改动本轮之前已编入，本次只需重编 `yapf_rail.cpp` / `train_cmd.cpp` 等 6 步）；
- ninja：`[5/6] Building CXX ... train_cmd.cpp.obj` → `[6/6] Linking CXX executable openttd.exe`；
- `build\R3R_incbuild.done` = `EXIT_CODE=0`（2026-11-02 01:50:02），日志内 error C*/fatal error/FAILED: 计数 0；
- 时间戳链：`src\train_cmd.cpp` 2026-11-02 01:45:47 → `train_cmd.cpp.obj` 01:47:35；`yapf_rail.cpp.obj` 01:47:32 → `build\openttd.exe` 01:49:44（52 420 096 B）；
- `read_lints` 三文件（train_cmd.cpp / yapf_rail.cpp / yapf_destrail.hpp）0 条；
- exe 字面量：`CPL-WAITCUT`、`CPL-WAITHOLD`、`cpl-wait-state2` 全部**搜不到**（机制与探针彻底移除）；`cpl-search-skip`、`cpl-moving-keep` 仍能搜到（保留的其它分支未误删）。

---

## 六、残留引用清理（编译前必做，已落地）

删掉 `TrainLocoHandler()` 里 `const int r3r_wait_cut = R3RShouldCutCoupleAtWaitPoint(consist, nullptr);`
之后，同一函数后续还有 7 处引用该局部变量，不清理必然 `error C2065: "r3r_wait_cut": 未声明的标识符`。
处理原则：`r3r_wait_cut` 已恒为 0（机制删除后不存在截断态），故所有
`&& r3r_wait_cut == 0` 恒真直接去掉、`!= 0` 恒假的分支整段删除。

| # | 位置（本轮改前行号） | 原写法 | 处置 |
|---|---------------------|--------|------|
| 1 | 17409 KI-344 普通寻路兜底 | `&& r3r_wait_cut == 0` | 删除该行 + 注释里"截断态不许往前铺"一条改写为"随截断机制删除" |
| 2 | 17437 KI-516 被第三方挡 | `&& r3r_wait_cut == 0` | 删除该行 |
| 3 | 17536 KI-271 节流跳过 | `... && r3r_wait_cut == 0` | 删除该条件 |
| 4 | 17589 KI-479 行驶中保持现状 | `... && pair_ready && r3r_wait_cut == 0` | 删除该条件 |
| 5 | 17637 KI-445 未就绪/截断 | `if (!pair_ready \|\| r3r_wait_cut != 0)` | 整块删除（块内已无代码，只剩注释） |
| 6 | 17640 KI-433 `CPL-WAITHOLD` 探针 | `if (r3r_wait_cut != 0) { ... }` | 整块删除，探针字面量作废 |
| 7 | 17665 KI-444 `state==2` 早退 | `if (r3r_wait_cut == 2) return ... "cpl-wait-state2";` | 整块删除，早退理由消失（不存在截断态） |

保留：`if (!pair_ready)` 在 17667 处另有一块（`CPL-NOTREADY` 500 tick 心跳），
与本轮无关，逐字未动；`pair_ready` 仍被 17585 使用，变量保留。

清理后核对：
- `search_content "r3r_wait_cut|CPL-WAITHOLD|cpl-wait-state2"` → 仅剩 3 处**注释**里的历史说明
  （17374 描述被删机制、17409/17638 新写的"已删除"注记），无任何代码引用；
- `read_lints` 三个改动文件 → 0 条。

---

## 七、2026-11-02 下午复测反馈「问题依然存在」——取证与待澄清

### 7.1 采样有效性（先确认玩家用的是不是新 exe）

| 项 | 读数 | 判定 |
|---|---|---|
| `build\openttd.exe` | 52 420 096 B，2026-11-02 01:49:44 | = 本轮删机制后的内测版，**采样有效** |
| `build\R3R_debug.log` | 40 559 760 B，2026-11-02 13:34:53 | 下午会话，约 40 MB |

⇒ 不是"玩家还在跑旧 exe"，机制确实已经不在运行。

### 7.2 探针核对（删除是否生效）

| 探针 | 命中数 | 说明 |
|---|---|---|
| `CPL-WAITCUT` | 0 | 已删，符合预期 |
| `CPL-WAITHOLD` | 0 | 已删，符合预期 |
| `PFD-ACCEPT` | 0 | 已删，符合预期 |

### 7.3 但这场日志里几乎没有挂接样本

| 探针 | 命中数 | 含义 |
|---|---|---|
| `CPL-PAIR` | **1** | 整场只出现 1 次挂接配对行 |
| `RESERVECONSIST` | 0 | 没有任何挂接期间的脚下预留记账 |
| `COUPLE-OK` | 0 | 没有一次挂接成功 |
| `COUPLE-FAIL` | 0 | 也没有挂接失败行 |

⇒ **当前日志无法给"预留是否还止于路点"提供判据**：该场景（GOTO_COUPLE 机车 → 等待车底）在这份日志里基本没有发生。

### 7.4 日志实际形态（尾部）

尾部被 `WP-EXIT` 成对刷屏（同一 veh 连续两行 `why=entry` / `why=depot` 交替）：

```
WP-EXIT why=entry veh=27586 order=9 real=9 impl=9 n=10 spd=0 tile=5191,3505 revdist=0 stop=0 parked=0 front=1 power=27192 caronly=0 tileDepot=1 track=80 prog=8 dtile=28718151
WP-EXIT why=depot veh=27586 order=9 real=9 impl=9 n=10 spd=0 tile=5191,3505 ...
```

同场还有 `RESV-BEGIN` / `TPR-WALK` / `TPR-EXIT` / `RESV-SIGPASS` / `CRT` / `WP-ENTER` / `RESV-TERM`，`spd` 最高 350
⇒ 这是一个**大存档、多车、真实运营**场景（不是之前那套 R3R 挂接测试小场景）。
`WP-EXIT` 刷屏的车全部 `tileDepot=1 track=80 spd=0`（停在车库格上），属第 373 轮附近那套路点停车/出站探针。

### 7.5 疑点（按规则立刻登记）

- **REG-386-A（阻塞判定）**：`CPL-PAIR`=1 而 `RESERVECONSIST`/`COUPLE-OK`/`COUPLE-FAIL` 全 0，
  与"玩家在挂接场景里看到预留止于路点"所要求的日志形态不符 ⇒ 要么玩家观察的是另一个会话（本日志不是那次），
  要么玩家所指的"问题"根本不是预留截断。**未澄清前不得改码。**
- **REG-386-B**：`C:\Users\冯洁敏\Documents\OpenTTD\` 下最新崩溃文件仍是 `crash-20261101T173030Z.*`
  （mtime 11-02 01:30:41），**没有** 2026-11-02 下午新增的 crash log
  ⇒ 若玩家指的是 road vehicle 崩溃（KI-552），本机也无新样本可查。

### 7.6 猜测根因（待验证，不落地）

- **G-386-1**：删掉 KI-429 之后若仍看到"预留止于路点"，很可能不是"截断"，而是
  `PfDetectDestination()` 回落后只认「站台 / 车库 / 已有预留」三类终点；
  若目标车底不在这些位置，YAPF 只能把最近可停处当终点 ⇒ 预留自然止于那一格（包括路点格）。
  这属于**原设计口径**而非残留截断。
  验证判据：抓一次挂接现场，看 `CPL-BEST tile=…` 与 `CPL-PATHFOUND found=?` 与 `RP cur=` 逐格序列，
  若 `CPL-BEST` 指向的就是路点格而非车底格，则成立。
- **G-386-2**：`WP-EXIT` 成对刷屏（车库格 `track=80`、`spd=0`、`tileDepot=1`）说明有一批列车
  在库内被反复判定"退出路点"，若玩家看到的现象是"列车/预留在路点处反复"，
  可能归到这条线（路点停车/出站判据），与 KI-551 无关。需玩家确认现象细节。

### 7.7 待玩家澄清（编号回填，避免重问）

- **C-1**：您说的"问题依然存在"，指的是哪一个？
  - C-1a 列车预留在路点截止（KI-551 要解决的那个现象）
  - C-1b road vehicle 崩溃（KI-552）
  - C-1c 别的现象（请一句话描述）
- **C-2**：这次的现场是哪个存档/场景？是不是 `build\R3R_debug.log`（40 MB，11-02 13:34:53）这一场？
  若是，请说明观察到的列车编号（veh）或大致位置，我去抓那一簇行。
- **C-3**："预留止于路点"是**看铁轨上的黑色预留预览**判断的，还是看列车**停在路点不动**判断的？
  （前者=预留位问题，后者=停车/订单问题，两条线完全不同的排查路径。）

---

## 八、玩家答复回填（2026-11-02，原文要点）

- **C-1 = C-1a**：「预留在路点截止依然存在」⇒ KI-551 本轮改动**未解决问题**，删除 KI-429 截断机制
  **不是**（或不只是）造成该现象的原因。KI-551 状态应记为「复测未通过」。
- **C-2**：「`build\R3R_debug.log` 确实可以得到本次场景的情况，但是本身这个场景就十分巨大，
  你可能取证困难」⇒ 采样有效（就是这 40 MB / 11-02 13:34:53 的日志），但玩家明确提示**全场扫读不可行**，
  必须靠定点标签/定点车号取数，不能靠通读。**仍未给 veh 号**。
- **C-3（本轮最关键的新事实）**：「主要是看到**铁轨黑色预览**，同时列车在**穿过路点时速度减速到 5 或 30**」
  ⇒ 两件事同时成立：
  1. **预留位**确实止于路点（不是看列车停住判断的）⇒ 是 PBS 预留终点问题；
  2. 列车**没有停**，而是**穿过路点时被压速到 5 或 30**，过完继续走
     ⇒ 存在一个「车在等待区路点附近就把 `cur_speed` 压到很低」的**速度压制点**，
     而且它与「预留终点收到路点格」同源或同时发生。

### 8.1 C-3 带来的判据升级（旧判据不足）

旧复测判据只问「预留是否止于路点」，没有区分「止于路点」是**终点算错**还是**被某个 hold 分支提前收尾**。
C-3 的"减速到 5 或 30"提供了第二个独立观测量：

- 若预留止于路点 **且** 车在路点处被压到极低速度 ⇒ 高度怀疑有一处「路点栅栏/hold」逻辑**仍在**，
  它在压速的同时把 YAPF 目标收窄到路点；
- 若预留止于路点 **但** 车匀速通过 ⇒ 纯粹是终点判定问题（`PfDetectDestination` 侧），与减速无关。

⇒ 本轮取证**必须**同时抓这两条线，不能只盯预留。

### 8.2 新猜测根因（待验证，立刻落盘）

- **G-386-3（中高，最可疑）**：`R3RShouldHoldForCouple()`（等待区路点栅栏，几何受限那条判据）
  在 KI-429 删除后**仍然存活**。它会在等待区路点把列车按住（表现为减速/停），
  而一旦"按住"，YAPF 自然没有理由把预留铺过路点 ⇒ 两条现象同时出现。
  验证判据：抓一次现场，看减速那一刻是否打出该函数相关的 hold 探针，以及 `dest_tile` 是否被改写成路点格。
- **G-386-4（中）**：速度压制点不在 R3R 判据里，而在**路点订单限速**或 `advisory_max_speed` 上
  （5 / 30 这种固定小数值很像 `advisory_max_speed` 的量级，且 `cur_speed` 显示值 = 内部值的倍数换算）。
  验证判据：取该车减速前后的 `actSpd`/`adv_spd`/`max_speed`/`advisory_max_speed` 四联读数。
- **G-386-5（沿用 G-386-1）**：预留终点被 `PfDetectDestination()` 回落成「最近可停处」= 路点格。

### 8.3 取证计划（受"场景巨大"约束，先代码后日志）

1. **先静态定位所有"压速度/按住"的存活判据**：`R3RShouldHoldForCouple`、`R3RShouldWaitForCouplePair`、
   以及任何写 `cur_speed = 0` / 写 `max_speed` / 写 `advisory_max_speed` 的 R3R 分支（不改码，只列表）；
2. **再定位"路点"参与速度计算的代码点**：`IsRailWaypoint(tile)` 附近的限速/停车判定；
3. 最后才回到 40 MB 日志，用**单标签定点抓取**（不做全量通读），标签优先级：
   hold/减速类探针 → `CPL-BEST`/`CPL-PATHFOUND` → `RP cur=` 逐格序列；
4. 若日志仍不足以区分 G-386-3 与 G-386-4 ⇒ 需要玩家提供**一辆车**的最小复现（单车 + 一条路点 + 一条
   GOTO_COUPLE），或给出现场 veh 号，否则"巨大场景"无法收口。

### 8.4 本轮纪律

- **不改码**：现象与现有猜根因之间证据链尚未闭合，且已证明"猜错机制 → 删了也没用"（KI-551 就是教训）。
- **不再宣称"已修"**：KI-551 状态改「复测未通过」，新问题另开 KI-553（路点减速 5/30 + 预留止于路点）。

---

## 九、本轮取证结论（2026-11-02，全部来自 `build\R3R_debug.log`，40 559 760 B / 13:34:53）

### 9.1 三道"拒绝订路"的门：**一次都没命中**

| 探针 | 对应门 | 命中数 |
|---|---|---|
| `RESV-NOBOOK` / `parked-no-pair` | `R3RShouldActAsWaitingForCouple()`（train_cmd.cpp:18418） | **0 / 0** |
| `CPL-HOLD-NOBOOK` | `R3RShouldHoldForCouple()`（14106 判据，18441 拒绝订路） | **0** |
| `CPL-WAIT-NOBOOK` | 同上（库内兜底支） | **0** |
| `CPL-HOLD-NATIVE` | `R3RShouldWaitForCouplePair()`（14128，速度清零） | **0** |
| `CPL-NOTREADY` | `!pair_ready` 心跳 | **0** |
| `CPL-S0-CHK` | GOTO_COUPLE 停驶分支（s=0） | **0** |

⇒ **G-386-3 被证伪**（至少对这份日志）：没有一道门在拦路，也没有一道门在压速度。

### 9.2 更关键的负面证据：本场**没有发生任何挂接寻路**

| 探针 | 命中数 | 含义 |
|---|---|---|
| `CPL-PATHFOUND` | **0** | 挂接寻路（`yapf_rail.cpp` 的 `FindNearestCoupleTrain` 出口）**一次都没跑过** |
| `CPL-PAIR` | 1 | 整场只有 1 条配对行 |
| `CPL-WAITCUT` / `PFD-ACCEPT` | 0 / 0 | 已删机制，符合预期 |
| `COUPLE-OK` / `COUPLE-FAIL` | 0 / 0 | 无一次挂接成败 |

⇒ **REG-386-C（阻塞级）**：这份 40 MB 日志里**不存在挂接场景**。而"预留在路点截止"的原始病根
（KI-429 / KI-551）位于挂接寻路里 ⇒ **本场日志从原理上不可能复现 KI-551 的现象**。
玩家看到的"预留在路点截止"要么发生在**另一个会话**，要么**不是挂接预留**。

### 9.3 路点通过现场：不停车、不减速

`WP-ENTER` 共 **1801** 条。样例（原文）：

```
WP-ENTER veh=22639 tile=1626,160 wp=1 ord=2 real=27 impl=27 n=28 dest=143 eq=0 stop=0 headstop=0 bpe=0 prev=1 rail=1 spd=120 revdist=0 x=3 y=8
WP-ENTER veh=22639 tile=1626,160 ... eq=0 stop=0 ... spd=120 ... x=2 y=8
WP-ENTER veh=22639 tile=1626,160 ... eq=0 stop=0 ... spd=120 ... x=1 y=8
```

- `ord=2`（`OT_GOTO_WAYPOINT`）、`eq=0`（这一格**不是**订单指定路点）、`stop=0`、`spd=120`
  ⇒ 正常通过，**没有减速**。`prev`/`x`/`y` 逐条变化说明车确实在走。
- ⇒ **REG-386-D**：至少这批 `WP-ENTER` 样本里没有"穿路点减速"的现象，
  玩家看到的 5/30 不在这批车上（或不在 `WP-ENTER` 能覆盖的时机上）。

### 9.4 速度归零事件：`SPD-DROP` = **2029** 条

- 这是本轮唯一"大量出现"的速度类探针，说明"速度从非零变零"在这台大存档里是常态
  （到站/进库/让行），**不能直接等同于玩家看到的路点减速**。
- 若要把 5/30 定到位，必须**先有车号**，再用 `Select-String "veh=<id>"` 定点抓
  （全场景通读不可行，玩家已明确提示）。

### 9.5 由此产生的新判断（REG-386-E，待玩家确认）

**"铁轨黑色预览止于路点"本身可能是正常现象**：若该车当前订单是 `GOTO_WAYPOINT`，
路点就是终点，PBS 预留理应止于路点。只有当**下一条命令是 `GOTO_COUPLE`** 时，
"预留应越过路点一路铺到车底"才成立（这才是 KI-551 要修的东西）。
⇒ 必须先确认现场那列车的**命令表**，否则无法区分「正常」与「缺陷」。

---

## 十、下一步（需要玩家给一条信息）

本轮取证已到"无车号即无法继续"的边界。缺的唯一输入是**现场车辆的编号**：

1. 复现时暂停游戏，打开那列车的车辆窗口（或车辆列表），记标题里的编号（形如 `Train 1234`）；
2. 或看订单窗口截图里的命令表：**路点后面是 `Goto（挂接）` 还是别的**；
3. 拿到车号后，我用 `Select-String "veh=<id>"` 定点抓该车全部行走，
   直接读 `RP cur=` 逐格预留序列与速度变化，不再碰 40 MB 全量。

在拿到车号之前，**不再改码**：本次已证明"猜机制 → 删机制"是错误的循环（KI-551 就是教训）。

---

## 十一、三车号定点取证（2026-11-02，veh=1039 / veh=40566 / veh=13390）

### 11.1 抓取方式与样本量

`Select-String -Pattern 'veh=1039\b|veh=40566\b|veh=13390\b' build\R3R_debug.log`
→ 落盘 `build\R3R_trace_veh.txt`，共 **963 行**（Out-File 需 `-Width 4096`，否则按 80 列折行，
读出来全是碎片）。

三车在日志中**没有 `CPL-*` 探针**，与玩家"路点命令之后是普通非 R3R 命令"一致。

### 11.2 取证限制（REG-386-F，工具层，务必记住）

`search_content` 对 `build\` 下文件**恒返回 0 命中**——它尊重 `.gitignore`，而 `build/` 被忽略。
⇒ 我先前"三车没有 `RP cur=`"的判断**不可采信**。此后 `build\` 目录的检索一律用
`findstr` / `Select-String`，不得用 `search_content`。
另：本环境 `find /c /v ""` 与 `find /c "str"` 均报"参数格式不正确"（引号被吞），
计数改用 `findstr` 输出到文件后用 `read_file`，或直接看输出。

### 11.3 veh=40566 的 WP-ENTER 序列（决定性证据）

`build\R3R_debug.log` 行 83624 → 90197，同一格 `tile=2276,2318 wp=1`，共 16 条：

| 日志行 | ord | real/impl | dest | eq | spd | x |
|---|---|---|---|---|---|---|
| 83624 | **6**(GOTO_WAYPOINT) | 11/11 | 548 | **1** | 76 | 15 |
| 83891 | 1 (GOTO_STATION) | 12/12 | 129 | 0 | 74 | 14 |
| 84073 | 1 | 12/12 | 129 | 0 | 71 | 13 |
| 84186 | 1 | 12/12 | 129 | 0 | 68 | 12 |
| 84451 | 1 | 12/12 | 129 | 0 | 65 | 11 |
| 84717 | 1 | 12/12 | 129 | 0 | 62 | 10 |
| 84964 | 1 | 12/12 | 129 | 0 | 59 | 9 |
| 85116 | 1 | 12/12 | 129 | 0 | 56 | 8 |
| 85265 | 1 | 12/12 | 129 | 0 | 52 | 7 |
| 85659 | 1 | 12/12 | 129 | 0 | 48 | 6 |
| 86003 | 1 | 12/12 | 129 | 0 | 44 | 5 |
| 86219 | 1 | 12/12 | 129 | 0 | 39 | 4 |
| 86788 | 1 | 12/12 | 129 | 0 | 34 | 3 |
| 87361 | 1 | 12/12 | 129 | 0 | 28 | 2 |
| 87927 | 1 | 12/12 | 129 | 0 | 20 | 1 |
| 90197 | 1 | 12/12 | 129 | 0 | **5** | **0** |

读数解读（必读，勿再误判）：

1. **路点不是终点**：83624 行 `ord=6 eq=1` ⇒ 到达路点，订单当场推进到 `ord=1 real=12 dest=129`。
   玩家"路点之后是普通非 R3R 命令"的陈述**成立**。
2. **减速发生在路点之后**：76 → 5 的整个下降段都在 `ord=1` 之后 ⇒ **不是**"到达路点的停靠减速"，
   而是"进入下一条 GOTO_STATION 后走不动"。
3. **列车被钉在路点格内**：16 条全为 `tile=2276,2318`，`x` 由 15 单调递减到 0（格边界）
   ⇒ 以每 tick 约 1px 的速度在路点格内蠕行到格端后停住。
4. **停止时速度是 5 不是 0**：`spd=5 stop=0` ⇒ 属限速/等待形态，不是正常刹停。

### 11.4 结论（两个现象合并为一个故障）

**"预留止于路点"与"穿路点减速到 5"是同一故障的两个投影**：

- 到达路点后订单已推进到 `GOTO_STATION dest=129`，理应为 129 重新寻路并铺出完整预留；
- 实际列车停在路点格、速度只剩 5 ⇒ **为 129 的寻路/预留没有铺出去**，
  只剩脚下（路点格）那一格预留 ⇒ 玩家看到的黑色预览"止于路点"；
- 因无路可走，列车被压到 5 而非 0（`stop=0`）。

### 11.5 猜测根因（待验证）

**G-386-7（主假设）**：路点到达后，对下一条 `OT_GOTO_STATION` 的 `TryPathReserve` 未成功
（返回 `TPRRF_NONE` / 被标记 Stuck），列车停在路点格。
支撑：`spd` 持续下降且 `x` 单调递减＝"没有前方预留 ⇒ 无法加速"的典型形态；无任何 `CPL-*` 交互。

**G-386-8（备选）**：`advisory_max_speed` 在下一条订单上被设成 5（限速而非无路）。

区分判据（下一步）：
1. `findstr /C:"veh=40566" build\R3R_trace_veh.txt | findstr /C:"RP cur="` —— 是否存在 `RP cur=` 行及终点；
2. `findstr /C:"TRP veh=40566" build\R3R_debug.log` —— 是否有 `res=0 ok=0`；
3. `findstr /C:"BLOCKED" build\R3R_debug.log | findstr /C:"40566"`。

### 11.6 疑点（REG-386-G）

- `ord=1 real=12` 在 16 条里**一次都没推进**，而 `spd` 一直在降 ⇒ "索引已推进但寻路从未成功"
  是**稳定态**而非抖动。
- `x=0` 之后无后续 `WP-ENTER`，需确认是"停住"还是"跨格离场"。

### 11.7 玩家答复回填（原话要点，勿丢）

- C-1：「预留在路点截止依然存在」⇒ KI-551 复测未通过。
- C-2：「build\R3R_debug.log 确实可以得到本次场景的情况，但是本身这个场景就十分巨大，你可能取证困难」
  ⇒ 已改为定点抓取（本节 963 行即产物）。
- C-3：「主要是看到铁轨黑色预览，同时列车在穿过路点时速度减速到 5 或 30」
  ⇒ 本节 `spd 76→5` 即该现象的日志形态。
- 2026-11-02 追加：「你可以关注 veh1039，veh40566，veh13390，它们路点命令之后都是普通非 R3R 命令，
  因此不应该预留在路点截止」⇒ 三车号已用于定点取证；40566 读数证实"路点后是普通命令"。

### 11.8 第二组取证：TRP（预留结果）与 RP（逐格预留）

1. `findstr /C:"RP cur=" build\R3R_trace_veh.txt` → **零命中**
   ⇒（已排除 11.2 的 gitignore 干扰，这是 findstr 的真实结果）三车没有任何 `RP cur=` 逐格预留行。
2. `findstr /C:"TRP veh=40566" build\R3R_debug.log` → **只有 1 条**：

```
TRP veh=40566 mf=2278,2317 real=11 type=6 dest=18991332 spd=0 fp=0 look=1 one=0 mstuck=0 fto=0 => ok=1 res=1
```

判读（关键）：

- 全部日志里 40566 只有**一次** TryPathReserve，且是 `real=11 type=6`（路点订单），结果 `ok=1 res=1`
  ⇒ 路点那一次的预留**成功**，这正是"黑色预览铺到路点"的来源。
- `real=12 type=1 dest=129`（到达路点后的 GOTO_STATION）**全程零 TRP**
  ⇒ 列车在 real=12 期间**从未再次调用 TryPathReserve**。
- `spd=0` 是那次调用时的速度读数（停在路点前），`one=0` 表示 ForceReserveOnce 已消费。

### 11.9 修正后的结论（把 G-386-7 具体化）

**不是"预留被截断到路点"，而是"预留铺到路点之后，再也没有为下一条命令发起新一轮寻路"。**

链条：
1. 列车执行 `GOTO_WAYPOINT`（real=11），寻路成功，预留正好止于路点（这**完全正常**，路点就是本次终点）；
2. 到达路点后订单推进到 `GOTO_STATION dest=129`（real=12），此时旧的预留已用尽；
3. 但列车**没有再次进入 TryPathReserve**（零 TRP），于是既没有新预留可走，也就不能加速；
4. 速度从 76 一路被压到 5、在路点格内以每 tick≈1px 蠕行到格端（x 15→0）后停住
   ⇒ 玩家同时看到「黑色预览止于路点」与「穿路点减速到 5」。

**⇒ KI-551 的真实落点在"订单推进后的重新寻路"，不在"预留截断"。**
第 386 轮删除的 `couple_wait_cut` 机制与此**无关**（三道门日志全 0 已证），删除无害但也无用。

### 11.10 下一步（唯一待办）

读 `TrainLocoHandler` / `TrainController` 中 `TryPathReserve` 的调用条件，回答：

- 为什么 `cur_speed` 从 76 降到 5 的过程中（仍在移动）没有触发新的 TryPathReserve？
- 判据：`real=12` 期间 `spd>0` 却零 TRP ⇒ 说明只要"当前格仍有旧预留覆盖"就不会重新寻路，
  而路点格的预留覆盖到格末就没了 ⇒ 形成"只剩 5 km/h 也不重订"的稳定态。
- 重点怀疑：JGRPP 的 `TrainController` 只在 `force_reserve` 或"预留耗尽且速度已降为 0"时才重订；
  5 km/h 的残速正好卡在两者之间。

### 11.11 玩家 2026-11-02 确认（三车同源，写死在文件里）

玩家原话：「是的 veh=1039 和 veh=13390 当时也是表现为"停在路点格上、速度 5 或 30、
黑色预览只到路点"」。

⇒ **三车（1039 / 40566 / 13390）现象完全一致**，取值 5 与 30 只是两档残速读数，不是两种故障：

| 车号 | 现象 | 日志内 wp 记录 |
|---|---|---|
| veh=40566 | 停在路点格、spd 76→5、预览止于路点 | 16 条 `WP-ENTER`（§11.3，决定性） |
| veh=1039 | 停在路点格、spd 5 或 30、预览止于路点 | 采样期未到达路点格（无 WP-ENTER） |
| veh=13390 | 同上 | 同上 |

结论固化：

1. **不再是"40566 个例"**，而是"路点订单完成后不重新寻路"的**通用缺陷**；
2. 三车路点之后都是普通命令（玩家两次确认）⇒ 与 R3R 的段/挂接/解挂**全部无关**，
   `CPL-*` 探针零命中即为旁证；
3. KI-551 的定位从"预留截断"**改判为"订单推进后不再发起新一轮寻路"**（§11.9），
   第 386 轮删除的 `couple_wait_cut` 既不是原因也不解决它。

### 11.12 下一组取证锚点（已定位，待读）

`TryPathReserve` 在 `src\train_cmd.cpp` 的调用点（search_content 结果，共 11 处，关键两处）：

- **:22709** `if (UpdateSignalsOnSegment(moving_front->tile, dir, ...) == SigSegState::Path || _settings_game.pf.reserve_paths) { TryPathReserve(consist, true, true); }`
  —— 位于移动/信号更新段，是"平时补预留"的主入口。
- **:21134** `if ((!HasReservedTracks(gp.new_tile, v->track) && !TryReserveRailTrack(...)) || !TryPathReserve(first)) { MarkTrainAsStuck(first); }`
  —— 缺预留时补订并可能标 stuck。

⇒ 待回答：:22709 被 `if (j < adv_spd)` 之类的停驶分支挡住的路径（KI-348 已证该分支不寻路），
以及 `R3RConsumeForceReplan`（:18473）只在掉头时置位 ⇒ 普通"路点到达"既没有 replan 标记、
也可能不在移动分支内调用 :22709。

### 11.13 代码取证（2026-11-02，`src\train_cmd.cpp` 定锚）

**A. 唯一的"移动 + 寻路"闸门（`TrainLocoHandler` 尾部，:23068-23219）**

```23068:23219:src/train_cmd.cpp
Train *moving_front = consist->GetMovingFront();
int adv_spd = moving_front->GetAdvanceDistance();
if (j < adv_spd) {
    /* if the vehicle has speed 0, update the last_speed field. */
    if (consist->cur_speed == 0) { consist->SetLastSpeed(); ... }
    /* 停驶分支：cur_speed != 0 时这里几乎什么都不做 */
} else {
    ...
    TrainCheckIfLineEnds(moving_front);        // :23193  ← 缺预留时 TryPathReserve(:21134)
    for (;;) {
        j -= adv_spd;
        TrainController(moving_front, nullptr); // :23198  ← 引擎唯一常规订路点
        ...
```

⇒ **`j < adv_spd` 为真 ⇒ 既不移动、也不调 `TrainCheckIfLineEnds`/`TrainController` ⇒ 永不订新预留。**
本文件自己的注释已多次确认此事实（:1737-1740、:18103-18104、:21946-21948、:23033-23035，
KI-348 实证）。

**B. 低速残速正好落在该分支**

`j` 来自 `UpdateSpeed()`，`adv_spd = GetAdvanceDistance()`（格内像素步长，数量级 8~16）。
速度越低 `j` 越小：`spd=5` 时几乎必然 `j < adv_spd`；`spd=30` 也常落在边界内侧。
⇒ **玩家看到的"速度 5 或 30 停住"就是 `j < adv_spd` 的残速形态**，不是"引擎要求它停"。

### 11.14 代码取证续：沿途补预留的三个闸门（2026-11-02）

**A. 沿途延伸预留只有两条路，且都带前置条件（`TrainController` 内，:21100-21230）**

```
:21221  if (first->lookahead != nullptr && first->cur_speed > 0 &&
            first->lookahead->reservation_end_position <= first->lookahead->current_position + 24)
            TryLongReserveChooseTrainTrackFromReservationEnd(first, true);   // 从预留终点往前续订
:21224  else if (first->tick_counter % _settings_game.pf.path_backoff_interval == 0)
            CheckNextTrainTile(v);                                            // 进格时的补订
```

- 第一条要求 **`lookahead != nullptr`**。而 `AdvanceLookAheadPosition()` 一旦车头越过
  `reservation_end_position + 8` 就把 lookahead **置空** ⇒ 真正到达预留终点之后必然为空。
- 第二条 `CheckNextTrainTile()` 只在「下一格是 PBS 信号机 / 隧道桥信号 / 需要处理路点订单推进」
  时才真的 `ChooseTrainTrack`（:15124-15160）；**普通轨道连续区间什么都不做**。

⇒ 结论句：**预留一旦到达它的终点（路点格 = 订单目的地 = 预留终点），若之后没有 PBS 信号机，
引擎没有任何"中途重新寻路"的代码路径。** 只能等 `TrainController` 再次被调度，而它需要
`j >= adv_spd`。

**B. 速度降到残速后彻底锁死**

刹车模型把速度压到 5/30（玩家读数）→ `j < adv_spd` → `TrainController` 根本不跑（§11.13 A）
→ `cur_speed` 继续归零 → `j == 0 < adv_spd` 恒成立 ⇒ **永久不会再寻路**。
这与玩家"停在路点格上、黑色预览只到路点"完全吻合：预留止于路点、订单已推进到后续普通命令，
但没有任何 tick 会去为新订单铺新预留。

**C. 已排除的 R3R 判据（重要，避免继续误查）**

- `R3RTrainStopsAtWaypoint()`（`src\train_cmd.cpp:669-683`）：**只**对
  「`current_order.IsType(OT_GOTO_WAYPOINT)` 且**下一条是 `OT_GOTO_COUPLE`**」返回 true。
  玩家三车（1039/40566/13390）路点命令后面都是普通命令 ⇒ 该判据为 false
  ⇒ `Order::ShouldStopAtStation(v, st, waypoint=true)` 的 R3R 重载**不会**把通过式路点当停车点。
- 日志佐证：`build\_wpenter.txt` 里全部样本 `eq=0 stop=0 headstop=0`（`VehicleEnterTile_Station`
  的 WP-ENTER 探针门禁全部放行）。⇒ **KI-538/KI-553 的停车门禁没有误触发**，
  速降不是它们造成的。

**D. 修正 §11.9 的旧判断（疑点已澄清）**

§11.9 写的"零 `TRP` 行 ⇒ 没重订"不能作为独立证据：`TRP` 只由 `TryPathReserve` 的特定出口打印，
而沿途延伸走的是 `CheckNextTrainTile` / `TryLongReserveChooseTrainTrackFromReservationEnd`，
**这两条路都不打 `TRP`**。因此"没看到 TRP"是正常的，真正的证据是 A 组那两条前置条件不成立。

**E. 对应的修法候选（待拍板，未落地）**

1. **（治本，最小）** 把 `:23192-23207` 现有的"实时制动前瞻自愈"（当前只在
   `OT_GOTO_COUPLE && !GetCoupleIsDepot()`）放宽为「任何订单类型」，让
   `cur_speed > 0 && lookahead == nullptr` 的移动列车都按 32 tick 限流重建前瞻。
2. **（治死锁）** 在停驶分支（`cur_speed == 0`）对「`lookahead == nullptr` 且当前订单仍有
   有效目的地」的列车加一次限流 `TryPathReserve`（建议 256 tick），否则速度归零后
   永远没有 tick 会重订（与 KI-348 的 `OT_GOTO_COUPLE` 死锁同构）。
3. **（可选）** 路点格不应当选为预留终点：让通过式路点在 `ChooseTrainTrack` 的
   安全停车位搜索里不成立 —— 风险最高（触及 YAPF 与路点语义），建议只在前两条不足时再考虑。

**F. 本轮未做/未取证**

- 未取得 `veh=15644 ord=6 spd=5` 的完整上下文（前一轮抽取命令被中断）。
  待验证：该车 `spd=5` 期间是否出现 `LOCO-MOVE`/`RESV-LA-REBUILD`；若两者皆无，
  §11.13 A + §11.14 A 的推断即被实测确认。
- 未核实 `TryLongReserveChooseTrainTrackFromReservationEnd()` 内部是否也有
  `cur_speed > 0` 之外的门禁（待读 :16500 一带）。
- 未改任何源码、未构建（本轮纯代码取证）。

### 11.15 实测取证：veh=15644 全量上下文（2026-11-02，决定性）

抽取方式：`findstr /c:"veh=15644" build\R3R_debug.log > build\_v15644.txt`，共 **132 行**。
（注意：`build\` 在 `.gitignore` 里，`search_content`/ripgrep 会静默跳过，必须用 findstr。）

**A. 现场事实（逐行锚点）**

| 行 | 读数 | 含义 |
|---|---|---|
| :39 | `WP-ENTER veh=15644 tile=4518,1466 wp=1 ord=6 real=8 n=20 **dest=782 eq=1** stop=0 headstop=0 spd=76` | 首次到达**目的**路点 782（就是当前格），速度 76 |
| :40-48 | `ORD-IDX-WATCH ... real=8->9 impl=8->9 ... dest **12005798 -> 11858322**` | 订单推进到 real=9，新目的**坐标 4498,1447**（=1447*8192+4498），旧的是 4518,1465 |
| :37 | `TRP ... look=1 ... => ok=1 res=1` | 到达路点前最后一次**成功**预留 |
| :36 | `RESV-END ... endTile=4518,1466 endPos=199 curPos=0 endBits=0202 real=8` | **最后一次**预留终点＝路点格 4518,1466 |
| :35 | `RESV-TERM ... tile=4518,1466 td=9 **tcount=-2 safe=1** tail=0` | 预留终点是"安全停车位" |
| :52-66 | `WP-ENTER tile=4518,1466 ... real=9 **eq=0** stop=0 headstop=0`，`spd=74→71→68→65→62→59→56→52→48→44→39→34→28→20→5`，`y=14→0`（`x=8` 恒定） | 列车**仍在移动**（格内 -y 方向），速度被一路压到 5 |
| :67-68 | `WP-ENTER tile=4518,**1465** ... spd=5`，:132 同 | 跨过预留终点格继续爬行（惯性），**仍未停** |
| 全程 | **零 `RESV-BEGIN`、零 `RESV-END`、零 `TRP`（:37 之后）** | 订单推进后**再没有任何新预留** |
| :19-32 | `CRT veh=15644 order=6 dir=5 origin=4507,1466 td=8 **found=1**` ×6 | `ChooseTrainTrack` 成功找到路径，却**没有产生 RESV-END** |

**B. 结论（修正 §11.13/§11.14 的侧重）**

1. **不是"速度归零后没有 tick"**：整段 spd 76→5，列车一直在动，`TrainController` 一直在跑
   （`CRT found=1` 有输出）。所以 §11.13 的 `j < adv_spd` 死锁是**终端形态**，不是起点。
2. **真起点＝"预留终点到达后延伸失败"**：
   - 订单在 :39 那一刻从"去路点 782（当前格）"推进到"去路点 781（4498,1447）"；
   - 旧预留终点正好是当前格 4518,1466（`endPos=199`），列车随后越过它；
   - 越过之后 `AdvanceLookAheadPosition()` 把 `lookahead` 置空（§11.14 A 第一条前置条件失效），
     剩下的只有 `CheckNextTrainTile()`，而它只在下一格是 PBS 信号机时才 `ChooseTrainTrack`；
   - 结果：`CRT found=1`（寻路本身成功）但**没有任何代码把结果转成新预留**。
3. **速度 76→5 是刹车模型在"往安全停车位 endPos=199 停车"** 的正常表现；它停不下来（惯性 +
   已越过 endPos）就变成 5 km/h 蠕行 —— 这就是玩家看到的"压到 5 / 30"。

**C. 新疑点（必须登记，待验证）**

- **REG-386-B**：`CRT found=1` 出现 6 次而 `RESV-END` 一次都没有。按 `ChooseTrainTrack` 的正常
  收尾应该打 `RESV-END`。⇒ 要么这 6 次走的是"结果与现有预留相同 ⇒ 什么都不做"的分支，
  要么 `TryLongReserveChooseTrainTrackFromReservationEnd()`（第二参 `no_reserve_vehicle_tile=true`）
  里有一条"新的预留被判定为多余"的早退。**下一步必须先读这个函数**（约 :16500 一带）
  以及 `ChooseTrainTrack` 里 `RESV-END` 的打印条件。
- **REG-386-C**：`ORD-BORROW-REFRESH veh=15644 bkr=8->9 tag=live` 出现在同一帧 ⇒ 该车是
  **R3R 排程借用链**（`r3r_orders_borrowed`）。借用的 `current_order` 投影是否会影响
  `CheckNextTrainTile` 里的 `ProcessOrders` 与订单目的地求值，需一并核对（但玩家三车都号称
  与 R3R 段/挂接无关，故先按普通链处理）。
- **REG-386-D**：`WP-ENTER` 探针**未限流**（单格内 15 帧 15 行），与 KI-534 的"每 256 tick 一行"
  不符，说明该探针的抑制键里没有位置/速度，候选是 `(veh, why)` 之外的键失效 —— 只影响日志体积。

**D. 修法优先级（依本组实测重排）**

1. **（治本·首选）** 在"预留终点已到达、`lookahead == nullptr`、`cur_speed > 0`、当前订单仍有
   目的地"时，主动调用一次 `TryPathReserve(v)`/`ChooseTrainTrack`，通过 `VehicleOrderSaver`
   让新订单参与寻路。落点建议：`TrainController` 的 `:21221` 分支把 `lookahead != nullptr`
   这个前置条件去掉（改为"lookahead 为空或快耗尽"），并保留限流。
2. **（兜底）** §11.14 E-2 的停驶分支补订（速度归零后打破死锁）。
3. 先不动 YAPF 的"路点能否作为安全停车位"——风险最高，且从本组证据看，
   把路点当安全位本身没错（通过式路点也确实要经过），错的是"经过之后没人续订"。

**E. 本轮未做**

- 未读 `TryLongReserveChooseTrainTrackFromReservationEnd()`（REG-386-B 的直接答案）。
- 未改任何源码、未构建。

### 11.16 代码取证：lookahead 的"延伸"与"丢弃"谁先谁后（关键）

`AdvanceLookAheadPosition()`（`src\train_cmd.cpp:1442-1493`）：

```cpp
:1444   v->lookahead->current_position++;
:1447   if (v->lookahead->current_position > v->lookahead->reservation_end_position + 8 &&
            v->track != TRACK_BIT_DEPOT) {
:1449       v->lookahead.reset();   /* 注释：it will be recreated later with a new reservation */
:1450       return;                 /* ★ 这里 return，下面 :1484 的延伸永远走不到 */
:1451   }
...
:1484   if (v->lookahead->current_position == v->lookahead->next_extend_position) {
:1485       SetTrainReservationLookaheadEnd(v);
:1488       TryLongReserveChooseTrainTrackFromReservationEnd(v, true);
:1489       if (v->lookahead == nullptr) return;
:1491       v->lookahead->SetNextExtendPositionIfUnset();
:1492   }
```

**结论句：`AdvanceLookAheadPosition()` 是唯一"自动延伸预留"的入口，但它要求
`current_position == next_extend_position` 严格相等；一旦错过这个点（或被 :1447 的
`> reservation_end_position + 8` 先命中 reset+return），lookahead 就被丢弃，
**而丢弃之后没有任何代码会重新创建它**（注释里那句 "it will be recreated later" 在本场景不成立）。**

叠加 §11.14 A：`TrainController` 的自动延伸要求 `lookahead != nullptr`；`CheckNextTrainTile`
只在 PBS 信号格才真订。⇒ **lookahead 一旦被 :1449 清空，本车就再也回不到寻路逻辑**，
只剩停驶分支（`j < adv_spd`）把它钉死在原地。

**REG-386-E（新疑点，待验证，优先级最高）**：现场 `veh=15644` 的
`RESV-END ... endPos=199 curPos=0` 之后，日志**再没有任何 `RESV-END` / `RPFAIL` / `TRP`**。
两种可能：
1. `next_extend_position` 被设成大于 `reservation_end_position + 8` 的值
   ⇒ 先命中 :1447 丢弃，:1488 的延伸一次都没机会跑（**最可能**）；
2. 延伸跑过一次但 `TryLongReserveChooseTrainTrackFromReservationEnd()` 内部直接 return
   （无日志出口）。

判据：若 (1) 成立，应看到"从 curPos=0 到 curPos=199 全程无 RESV-END、且此后 lookahead=null"，
且 `SetNextExtendPositionIfUnset()` 的实现里 `next_extend_position` 的默认起点
（`reservation_end_position` 或 `lookahead_end_position` 附近）与 `end + 8` 的关系可解释。
**下一步必读**：`SetNextExtendPositionIfUnset()`、`SetTrainReservationLookaheadEnd()`、
`FillTrainReservationLookAhead()` 三处，确认 `next_extend_position` 的初始值与推进步长。

**F. 修法（依本节收窄）**

- **首选（不改引擎几何语义）**：在 `TrainLocoHandler` 的停驶/移动两支之外，对
  「`current_order` 有有效目的地 + `lookahead == nullptr` + 本 tick 未寻路」的列车，
  以 `pf.path_backoff_interval` 限流调用一次 `TryPathReserve(v, false)`（或
  `TryLongReserveChooseTrainTrackFromReservationEnd` 的等价入口），
  使"lookahead 被丢弃"不再等于"永久不再寻路"。
- 也可在 `AdvanceLookAheadPosition()` 的 :1449 处，`reset()` 之前先尝试一次延伸
  （但该函数被极高频率调用，必须严格限流，否则是性能地雷）。

**G. 本轮未做**

- 未读 `SetNextExtendPositionIfUnset()` / `SetTrainReservationLookaheadEnd()`（REG-386-E 的直接答案）。
- 未改任何源码、未构建。

### 11.17 REG-386-E 结案：`next_extend_position` 被设到预留终点之外（根因锁定）

三处源码拼起来就是完整答案：

```cpp
/* pbs.h:220-223 */
void SetNextExtendPositionIfUnset()
{
    if (this->next_extend_position <= this->current_position) this->SetNextExtendPosition();
}

/* pbs.cpp:1428-1438 */
void TrainReservationLookAhead::SetNextExtendPosition()
{
    int32_t threshold = this->current_position + 24;
    for (const TrainReservationLookAheadItem &item : this->items) {
        if (item.type == TRLIT_SIGNAL && item.start > threshold) {
            this->next_extend_position = item.start - 24;   /* ★ 指向"前方第一个信号机 - 24" */
            return;
        }
    }
    this->next_extend_position = this->current_position;
}
```

**结论句：`next_extend_position` 只在「前方 24 单位内有信号机」或「完全没有信号机」时才落在
`current_position` 附近；一旦前方有信号机但该信号机位于预留终点之后（本现场正是这种：
预留终点＝路点格 4518,1466 / `endPos=199`，而信号机在更远的 4498,1447 方向），
`next_extend_position` 就被设成「信号机 - 24」，**永远大于 `reservation_end_position`**。

于是 `AdvanceLookAheadPosition()` 每 tick 走的是：
- `current_position++` → `current_position == next_extend_position`？否（next_extend 远在前方）
  ⇒ :1484 的延伸**永不触发**；
- 继续走到 `current_position > reservation_end_position + 8` ⇒ :1449 `lookahead.reset(); return;`
  ⇒ lookahead 为空；
- 之后 `TrainController` 的自动延伸需要 `lookahead != nullptr`（:21086）⇒ 不延伸；
  `CheckNextTrainTile` 只在下一格是 PBS 信号机时才订（:15146-15160）⇒ 本现场下一格是普通
  路点/普通轨道 ⇒ 什么也不做；
- 停驶分支（`j < adv_spd`）里没有任何寻路调用 ⇒ **永久停滞**。

⇒ `:1449` 注释写的 "it will be recreated later with a new reservation" 在"预留终点早于下一个
信号机"的场景下**不成立**：没有任何代码会重建它。

**修法（本轮落地，仅 `src\pbs.cpp`，未碰 `src\*.h`）**：在 `SetNextExtendPosition()` 里给
`next_extend_position` 加一条上限 —— **延伸点不得晚于预留终点前 8 单位**，否则退化为
`current_position`（即"到终点前一直尝试延伸"）：

```cpp
if (item.type == TRLIT_SIGNAL && item.start > threshold) {
    this->next_extend_position = item.start - 24;
    if (this->next_extend_position > this->reservation_end_position - 8) {
        this->next_extend_position = this->current_position;
    }
    return;
}
```

- 作用域：只在「信号机 - 24 > 预留终点 - 8」时改道，即**本来就永远等不到延伸**的那些列车；
  正常列车（终点＝信号机前）逐字不变。
- 回退风险：这些列车会从"每 tick 都试延伸"变成"到终点前每 tick 试延伸"——它们原本
  一次都不会试，因此这是纯增益；`TryLongReserveChooseTrainTrackFromReservationEnd()` 在
  无法延伸时只做一次 `FollowTrainReservation` + yapf（有 `_long_reserve_disabled` 与
  递归守卫），量级可控。
- 仍需复测：三车（1039 / 40566 / 13390）路点之后是否恢复新的 `RESV-END`/`RESV-BEGIN`、
  速度是否恢复正常（不再 5/30 蠕动）、黑色预览是否继续向前铺。

**H. 本轮未做**

- 未验证「现场 4518,1466 之后到 4498,1447 之间确有信号机」这一前提（日志无对应探针）；
  若该区段**无信号机**，则 `SetNextExtendPosition` 会走 `= current_position` 分支，本修法
  不是本现场的答案 —— 需在复测时看是否出现新的 `RESV-END` 来判断。
- **已落地（2026-11-02 14:49:42，`src\pbs.cpp`，未碰任何 `src\*.h`）**：按上节代码给
  `SetNextExtendPosition()` 加上限（`next_extend_position > reservation_end_position - 8`
  ⇒ 退化为 `current_position`）。改动位置 `src\pbs.cpp:1428-1457`。
- **构建自证（2026-11-02 14:55，已完成）**：`R3R_fullbuild.cmd inc detach`，`MODE=inc`；
  `[2/3] Building CXX object ...\src\pbs.cpp.obj` → `[3/3] Linking CXX executable openttd.exe`
  （14:55:17）；`build\R3R_incbuild.done` = `EXIT_CODE=0`，日志 `error C*/FAILED` 计数 0；
  时间戳链 `src\pbs.cpp` 14:49:42（126 889 B）→ `pbs.cpp.obj` 14:53:23（3 401 733 B）→
  `build\openttd.exe` 14:54:58（52 420 096 B）；`read_lints` 0 条。
  本轮未新增日志字面量（只改判定逻辑 + 注释），故不做 exe 串核对。

## 十三、第 386 轮续之三（2026-11-02 15:05 复测：现象仍在，但"零预留"的前提已被推翻）

### 13.1 复测确实用了含本修复的 exe

- `build\openttd.exe`：2026-11-02 14:54:58（52 420 096 B）⊂ 含 `src\pbs.cpp` 14:49:42 的
  `next_extend_position` 上限。
- `build\R3R_debug.log`：2026-11-02 15:05，**15 227 036 B**（日志体量异常，见 13.6 KI-554）。
- ⇒ 玩家"依然存在"的观察**发生在修复之后**，本轮修法未消除现象。

### 13.2 现场仍在同一处：`veh=15644`，路点 `4518,1466`

`build\R3R_debug.log:89733-89740`（同一 tick=10105203）：

```
:89733 TPR-WALK veh=15644 mf=4506,1466 origin=4506,1466 tdd=8 okay=1 exit=2 new=4507,1466 reach=1 spd=0 one=0 look=0
:89734 CRT veh=15644 order=6 dir=5 origin=4507,1466 td=8 found=1
:89735 RPFAIL veh=15644 tick=10105203 origin=4507,1466 tile=4518,1466 td=9 unsafe=0 nTry=12 nOK=11
:89736 CPLRES veh=15644 tick=10105203 origin=4507,1466 destTile=4518,1466 destTd=9 node=4518,1466 root=0 unsafe=0 nTry=17 nOK=16 empty=0
:89737 RESV-TERM veh=15644 real=8 type=6 tile=4518,1466 td=9 tcount=-2 safe=1 tail=0 spd=0 vtile=4506,1466
:89738 RESV-END veh=15644 tick=10105203 from=4506,1466 endTile=4518,1466 endTd=9 endPos=199 curPos=0 endBits=0202 junction=0 real=8 spd=0
:89739 TRP veh=15644 mf=4506,1466 real=8 type=6 dest=12005798 spd=0 fp=0 look=1 one=0 mstuck=0 fto=0 => ok=1 res=1
:89740 STUCK veh=15644 tile=4506,1466 state=0 stuck=0 fp=0 parked=0 spd=2 order=6 real=8 prog=10 track=01 dtile=12005798
```

`veh=15644` 全日志只出现 34 行，且都集中在这几行（89733 起）到文件末段。

### 13.3 结论句（推翻上一轮的"零预留"判定）

- **这条路点订单最终是拿到预留的**：`:89739 => ok=1 res=1`、`look=1`、`:89738 endPos=199`
  覆盖到 `4518,1466`。上一轮"之后再也没有 `RESV-END`"的判据本轮**不成立**
  ⇒ §11.17 的"`next_extend_position` 永不推进"**不是（唯一）主因**，pbs.cpp 上限修法无效但也没变坏。
- `RPFAIL nTry=12 nOK=11` ⇒ yapf 只把 12 段里的 11 段铺通，**进目标格 4518,1466 的那一步没成**；
  随后 `RESV-TERM ... tcount=-2 safe=1 tail=0` 就地收尾 ⇒ **预留终点 = 路点格本身 = `endPos=199`**。
- 但 `real=8 type=6 dest=12005798` 说明这条订单的**目的地本来就是路点**。所以"预留截止在路点"
  **不是**任何 R3R 截断代码造成的——按 §12.2 的取证，`couple_wait_cut` / `CPL-WAITCUT` / waitwp
  那批截断实现已整删（`src\` 里只剩注释），而是**上游语义：路点就是这条命令的终点**。
- ⇒ 玩家的怀疑方向正确，但要精确表述为：**不是"我们截断它"，而是"我们把路点当成了寻路终点"**
  （所以预留必然在路点格收尾），而**越过/到达之后没有任何代码为新订单重新订路**。

### 13.4 新疑点 REG-386-G（待验，优先级最高）

`spd=2` 与 `look=1 res=1` 并存：**有预留、有 lookahead，却几乎不动**。两种可能：

- (a) 预留只到 `4518,1466`（12 格 ≈ 199 单位），`current_position` 一涨到 199 就命中
  `train_cmd.cpp:1447` 的 `> reservation_end_position + 8 ⇒ lookahead.reset(); return;`，
  之后靠惯性滑行、速度被刹到 5 ⇒ 与"5 km/h 蠕动且黑色预览停在路点"逐条吻合（**最可能**）；
- (b) `GetCurrentMaxSpeed()==0`：advisory speed 被 `limit_advisory_speed(..., 0, ...)` 钉在当前位置
  （`train_cmd.cpp:1396-1402` 的路点停车目标分支）。

判据：取 `veh=15644` 在 `:89740` 之后的逐 tick `spd` / `curPos`；若 `curPos` 单调到 199 后
出现 `reset`/`RESV-LA-*` 且 `spd` 归零 ⇒ (a) 成立。

### 13.5 下一修法（方向确定，待落地）

- **根治（对齐玩家口径）**：让**通过式路点不作为寻路终点** —— 在 `UpdateOrderDest()` / yapf 目标判定里，
  若 `current_order` 是 `OT_GOTO_WAYPOINT` 且 `!R3RTrainStopsAtWaypoint(v)`
  （即不是"下一条是 GOTO_COUPLE、要在路点头停等挂接"的那类路点，
  见 `train_cmd.cpp:669-683`），就把寻路目标**顺延到下一条真实到点订单**（车站/车库/下一个该停的路点），
  使预留**穿过**路点而不是**截止**在路点。
- **兜底（改动更小）**：在 `AdvanceLookAheadPosition()` 的 `train_cmd.cpp:1447`
  （`lookahead.reset()` 处）之后补一次受 `pf.path_backoff_interval` 限流的 `TryPathReserve(v)`，
  让"丢弃 lookahead"不再等于"永久不再寻路"。

### 13.6 待办与新问题

- **需要玩家指认**：本轮日志里"仍然卡住"的那列车（车号或所在格）。`veh=15644` 在 `:89740`
  之后没有任何日志行，无法独立判断它是"已在加速脱离"还是"仍在 2 速蠕行"。
- **KI-554（新，低）**：本轮日志 15 MB，`ORD-IDX-WATCH`（4 个 site）/`WP-EXIT` 等探针按
  tick×车刷屏，需改边沿触发，否则以后每轮取证都要先清理几 MB 噪声。

## 十四、订正（玩家 2026-11-02 拍板）：寻路终点 ≠ 预留终点；路点不得作为 safe waiting position

**玩家原话（决定性，原文保留）**：
> “路点”天然就是预留的截止面，你这个说法是错误的，在原生 OTTD 包括 R3R 古早一点的版本，
> 都没有说预留能够在“路点”截止的语义，这绝对是 R3R 加上去的。还有，请你不要混淆“寻路终点”
> 和“预留终点”的概念，路点可以作为 YAPF 的寻路终点，但不可以成为预留截止的“安全停车位”。

### 14.1 撤回 §13.3 的“上游语义”判定

§13.3 写“预留止于路点是上游语义「路点就是这条订单的终点」造成的”——**该判定错误，予以撤回**。
正确口径（两个概念必须分开）：

| 概念 | 代码面 | 路点是否允许 |
| --- | --- | --- |
| 寻路终点 YAPF destination（决定车往哪开） | `UpdateOrderDest()` 的 `dest_tile`、YAPF 到达判定 | **允许**，这是订单语义，不动 |
| 预留终点 safe waiting position（PBS 预留到哪收尾 / 车可以在哪停住） | `IsSafeWaitingPosition()` 及其调用链、预留终止判定 | **禁止**，路点不是停点，只能通过 |

⇒ 现场 `RESV-END veh=15644 ... endTile=4518,1466 endTd=9 endPos=199`（`build\R3R_debug.log:89738`）
证明预留**收尾在路点格末**。若上游没有这条语义，则**必有 R3R 新增的代码把路点当成了 safe waiting
position**，这是必须删掉的越界改动，而不是“天然语义”。

### 14.2 取证清单（按此顺序，勿混概念）

1. `IsSafeWaitingPosition()`（声明在 `src\pbs.h`）的**实现与全部调用点**，重点看其中
   `IsRailWaypointTile()` / 路点整格位分支 —— 这是“预留终点”，凡放行路点即违规。
2. `CYapfDestinationTileRail::PfDetectDestinationTile()`（`src\pathfinder\yapf\yapf_rail.cpp`）
   对路点的接受 —— 属“寻路终点”，**允许**，不要误删（这是上一轮差点搞错的地方）。
3. `Train::ReserveTrackUnderConsist()` / `TryPathReserveWithResultFlags()` 的预留终止条件里
   是否对路点特判。
4. `src\pbs.cpp` 的 `TryReserveRailTrack()` / 整格位（`SetRailStationReservation`）是否把路点
   当作可收尾位（KI-125 那批“整格位预留”的旁支）。

### 14.4 实证（决定性）：确实是 R3R 第 284 轮加的，不是上游语义

`src\pbs.cpp:2696-2704` 原文：

```cpp
bool IsSafeWaitingPosition(const Train *v, TileIndex tile, Trackdir trackdir, bool include_line_end, bool forbid_90deg, bool treat_waypoint_as_safe)
{
	if (IsRailDepotTile(tile)) return true;

	/* R3R (第 284 轮): a waypoint the train is currently heading for is a valid
	 * place to end the reservation. Checked first so that the "next tile" rules
	 * below (which usually do not see a signal on a waypoint tile) cannot push
	 * the reservation past it. See the parameter documentation above. */
	if (treat_waypoint_as_safe && IsRailWaypointTile(tile)) return true;
```

声明 `src\pbs.h:240`：

```cpp
bool IsSafeWaitingPosition(const Train *v, TileIndex tile, Trackdir trackdir, bool include_line_end, bool forbid_90deg = false, bool treat_waypoint_as_safe = false);
```

- 上游只有前 5 参（第五个是 `forbid_90deg`）；**第六参 `treat_waypoint_as_safe` 与 2704 行
  那个 `return true` 是 R3R 第 284 轮新增的**，注释自陈目的就是「让车正在前往的路点成为
  预留的合法结束点」——正是玩家指认的那处改动。玩家判断正确，§13.3 的上游归因不成立。
- 概念落点确认：YAPF 到达判定（`yapf_rail.cpp` 的 `PfDetectDestinationTile`）是**寻路终点**，
  放行路点没问题；`IsSafeWaitingPosition()` 是**预留终点 / 安全停车位**判定，这里对路点
  `return true` 才是越界。
- 机制链：`src\pbs.cpp:1513`
  `ftoti.res.okay = ... IsSafeWaitingPosition(v, ftoti.res.tile, ftoti.res.trackdir, true, _settings_game.pf.forbid_90_deg, true);`
  —— 第六参传 **true**，于是回溯到路点格时 `okay = true`，预留就地收尾 ⇒ 现场
  `RESV-END ... endTile=4518,1466(路点) endPos=199`。

### 14.5 修法方向（待拍板）

**删掉 `treat_waypoint_as_safe` 这一路**：移除 `pbs.h:240` 的第六参、`pbs.cpp:2704` 的分支，
并清掉所有调用点传 `true` 的地方，恢复「路点不是安全停车位」——预留在路点格不许收尾，
必须越过路点继续铺到下一条真实到点订单的安全位/信号。

保留的例外（**不得**用 safe waiting position 实现）：下一条是 `GOTO_COUPLE`、车要头停在路点上
等挂接的那种路点，属于**停车语义**，由订单层的 `R3RTrainStopsAtWaypoint()`（`train_cmd.cpp:669`）/
KI-448 那条链负责，与 PBS 预留终点无关。

### 14.6 调用点清单（全库 10 命中，其中 4 处传 `true`）

传 `true`（**违规面，必须清**）：

- `src\pbs.cpp:1513` — `ftoti.res.okay = ... IsSafeWaitingPosition(v, ftoti.res.tile, ftoti.res.trackdir, true, _settings_game.pf.forbid_90_deg, true);`
- `src\train_cmd.cpp:16042` — `if (IsSafeWaitingPosition(v, tile, cur_td, true, _settings_game.pf.forbid_90_deg, true)) {`
- `src\train_cmd.cpp:17873` — `while (!IsSafeWaitingPosition(consist, res_dest.tile, res_dest.trackdir, true, _settings_game.pf.forbid_90_deg, true)) {`
- `src\train_cmd.cpp:17939` — 同上的探针 payload 里再算一次

不传（`=false`，保持上游语义，**不要动**）：

- `src\rail_cmd.cpp:700`、`src\pathfinder\yapf\yapf_rail.cpp:162`、
  `src\pathfinder\yapf\yapf_destrail.hpp:185`、`src\pathfinder\yapf\yapf_costrail.hpp:733` / `:740`

⇒ 修法就是：删这 4 处的第 6 参 + 删 `pbs.h:240` 的默认形参 + 删 `pbs.cpp:2704` 的分支
（连 `pbs.cpp:2686-2688` 的 `@param treat_waypoint_as_safe` 文档一起删）。

### 14.7 落地风险（改前逐条核，改后逐条复测）

1. `train_cmd.cpp:17873` 的 `while (!IsSafeWaitingPosition(...))` 是**推进预留终点**的循环：
   删 `true` 后路点不再终止，循环会越过路点继续推进——正是目的；终点仍由信号/尽头/站台末端
   兜住（`include_line_end=true` 已传），但要确认不会把预留推到已停列车身上。
2. `pbs.cpp:1513` 的 `okay` 用于“预留远端是否安全”（阻塞/占用判定）：路点格从 okay 变
   not-okay，可能让“停在路点上的车”被判成不安全终点 ⇒ 复测须确认不把正常列车误判为阻塞；
   若出现，正确做法是**给该判定单独传参**，而不是全局放行路点。
3. `train_cmd.cpp:16042` 与 `:17939` 改前必须看上下文（16042 在阻塞/检查路径上，17939 是探针）。
4. 合法停车场景（下一条是 `GOTO_COUPLE`、车要头停在路点上等挂接）由订单层
   `R3RTrainStopsAtWaypoint()`（`train_cmd.cpp:669`）/ KI-448 负责，**不依赖** safe waiting
   position ⇒ 删掉后这些场景仍能正常停车。
5. 副作用面：预留变长 = 占用更多 PBS 区段，可能降低咽喉通过能力，需复测吞吐。

### 14.3 判据

路点格**不得**成为预留收尾格：`RESV-END ... endTile=<路点格>` 只允许出现在“该路点上有停车命令且
车已停稳”的情形；正常通过式路点的预留必须**越过路点**继续铺到下一条真实到点订单的安全停车位/信号。
- **新增待确认项 REG-386-F**：本修法的前提是「4518,1466 → 4498,1447 之间确有信号机」。
  日志无 `next_extend_position` 探针，无法先验；复测时用两条硬判据反推：
  ①路点之后重新出现 `RESV-END`（且 `endTile` 越过 4518,1466）⇒ 修法命中；
  ②仍然零 `RESV-END` ⇒ 前提不成立（该区段无信号机，走的本就是 `= current_position` 分支），
  根因另有其处，需回到 §11.16 的第二条假设（延伸被调用但内部早退）。

**C. 离站订路的旁门只在 station 离站时走（:22693-22713）**

```22693:22713:src/train_cmd.cpp
} else if (consist->flags.Test(VehicleRailFlag::LeavingStation) && ...) {
    ...
    if (UpdateSignalsOnSegment(...) == SigSegState::Path || _settings_game.pf.reserve_paths) {
        TryPathReserve(consist, true, true);
    }
    consist->flags.Reset(VehicleRailFlag::LeavingStation);
}
```

⇒ 这条路只在 `LeavingStation` 置位时生效。**路点（waypoint）不是车站，到达路点不会置这个标志**，
所以"订单推进后补一次预留"的旁门对路点场景完全不生效。

### 11.14 新假设 G-386-9（完整死锁链，强于 G-386-7）

```
① 列车执行 GOTO_WAYPOINT(real=11) → 寻路成功(TRP ok=1 res=1)，预留按设计止于路点格；
② 列车开到路点，TrainEnterStation 的 waypoint 分支推进订单到 real=12(GOTO_STATION dest=129)，
   但**没有释放旧预留、也没有为 dest=129 重新寻路**（零 TRP 已证）；
③ 旧 lookahead 的 reservation_end 就在路点格末 ⇒ GetCurrentMaxSpeedInfoInternal 按
   "预留末端"限速 ⇒ 列车沿刹车曲线从 76 一路降（§11.3 的 16 条 spd）；
④ 速度降到 j < adv_spd ⇒ 落入 :23070 停驶分支；
⑤ 停驶分支不调 TrainController / TrainCheckIfLineEnds ⇒ 永不重订 ⇒ 稳定停住；
   而 spd 不动（残速 5/30）⇒ 玩家看到"停在路点格上，速度 5 或 30"，
   旧预留的黑色预览也一直只铺到路点 ⇒ 两个现象同源。
```

关键因果**不是**"预留被截断"，而是**"订单推进后没有任何路径会为下一条订单再订一次预留，
而残余速度又把列车锁进不寻路的停驶分支"**。

### 11.15 修法候选（待拍板，未落地）

| 方案 | 落点 | 优点 | 风险 |
|---|---|---|---|
| A 路点到达即释放+重订 | `TrainEnterStation` 的 waypoint 分支 / 订单推进处 | 治前因，最干净 | 触及引擎到站路径；释放时机错会瞬间丢预留 |
| B 停驶分支补一次订路 | :23070 停驶分支内，判据="有目的地 + lookahead 空/预留已尽 + 订单刚推进" | 局部、可控、不动到站路径 | 需防每 tick 重试打日志/耗 CPU |
| C 路点到达时置 `LeavingStation` | 借用 :22693 既有旁门 | 复用原生路径，改动最小 | 语义借用，可能与站台标志的其他用途冲突 |
| D 只加只读探针先定死 | 停驶分支打 `j/adv_spd/spd/lookahead/resv_end/order/real` | 零风险，先证 G-386-9 | 多一轮 |

**建议顺序：先 D 证死假设，再按 B（或 C）修。** D 的判据：
停驶分支中出现该车、`j < adv_spd`、`lookahead` 的 `reservation_end` 就在脚下格、
`order=1 real=12` ⇒ G-386-9 成立；若 `j >= adv_spd` 却仍零 TRP，则 G-386-9 被证伪，
需改看 `CheckTrainStayInDepot`/`LeavingStation` 之外的其他早退。

### 11.16 决定性取证（2026-11-02）：路点到达处被 R3R 无条件硬停

`src\train_cmd.cpp:19182-19194`（`TrainEnterStation` 的 waypoint 分支）：

```19182:19194:src/train_cmd.cpp
if (Waypoint::IsExpected(bst)) {
    consist->DeleteUnreachedImplicitOrders();
    UpdateVehicleTimetable(consist, true);
    consist->last_station_visited = station;
    consist->force_proceed = TFP_NONE;
    SetWindowDirty(WindowClass::VehicleView, consist->index);
    consist->current_order.MakeWaiting();
    consist->current_order.SetNonStopType(ONSF_NO_STOP_AT_ANY_STATION);
    R3RSpdCtx(consist, "wp-arrive", consist->cur_speed, 0);
    consist->cur_speed = 0;
    consist->UpdateTrainSpeedAdaptationLimit(0);
    return;
}
```

对照基线（`build\_base_train_cmd.cpp` = `git show 4553e2d844:src/train_cmd.cpp`，2026-06-30 上游、R3R 改动之前）：
该分支只有 `DeleteUnreachedImplicitOrders / UpdateVehicleTimetable / last_station_visited /
force_proceed / SetWindowDirty / MakeWaiting / SetNonStopType` 然后 `return;`，
**没有 `R3RSpdCtx("wp-arrive")` / `cur_speed = 0` / `UpdateTrainSpeedAdaptationLimit(0)` 三行**。

⇒ 三行是 R3R 后加的（`R3RSpdCtx` 本身是 R3R 设施），目的是让「等待区路点」停稳等人挂。
但它没有判据，**把纯通过式路点也一起打停**。

**结论（修正 G-386-9 的 ②③）**

```
① 列车执行 GOTO_WAYPOINT(real=11) → TRP ok=1 res=1，预留按上游设计止于路点格（这一步与上游一致，不是缺陷）；
② 进入路点格 → TrainEnterStation 的 waypoint 分支：MakeWaiting() 把订单转 OT_WAITING 并**无条件 cur_speed = 0**；
   路点通常是多格，每进入一格就可能再触发一次 ⇒ 现场看到 spd 在 3/5/15/30 之间抖动、x 逐格递减 1~3px；
③ 订单已推进到 real=12(GOTO_STATION dest=129)（现场日志为证），列车却被按在原地、速度归零/残速；
④ 残速落在 j < adv_spd ⇒ :23070 停驶分支 ⇒ 不调 TrainCheckIfLineEnds / TrainController ⇒ 永不续订（§11.13-A）；
⑤ 稳定态 = 车停在路点格上、速度 5 或 30、黑色预留预览只铺到路点、日志零 TRP —— 与玩家三条现象逐条吻合。
```

即：**不是"预留该跨没跨"，而是"路点到达被硬停"**。`WP-ENTER ... stop=0 / headstop=0`
（判据认为不该停）与实际被停之间的矛盾，就是 KI-538 的签名。

### 11.17 落地修法（第 386 轮实现）：停车两行条件化

落点唯一、不动寻路：`src\train_cmd.cpp` 的 `TrainEnterStation` waypoint 分支。

- 采样必须在 `MakeWaiting()` **之前**：`R3RTrainStopsAtWaypoint()` 要求 `current_order.IsType(OT_GOTO_WAYPOINT)`，
  而 `MakeWaiting()` 会把 type 改写成 `OT_WAITING`。
- 判据沿用第 287/288 轮既有语义（`OT_GOTO_WAYPOINT` 且下一条为 `OT_GOTO_COUPLE`）＝「等待区路点」。
- 等待区路点：三行原样保留（`wp-arrive`、`cur_speed = 0`、`UpdateTrainSpeedAdaptationLimit(0)`）。
- 纯通过式路点：只保留上游的 `MakeWaiting + SetNonStopType`，**完全不碰速度**（与基线逐字一致）
  ⇒ 列车原速通过，订单由 `ProcessOrders` 推进，随后由 `TrainCheckIfLineEnds(:21134)` /
  `:22709` 旁门在预留耗尽时正常续订。符合玩家口径：不引入任何速度钳制。

**对"预留是否要跨过路点"的回答**：不需要。上游寻路终点本来就止于路点格
（基线 `UpdateOrderDest` / `ShouldStopAtStation` 语义相同），"跨格"从来不是不变式；
不变式是**列车不能在通过式路点上被减速**。减速只允许由 look-ahead 的 0 速目标驱动原生刹车。

### 11.18 复测判据（第 386 轮）

1. 同场景 `veh=40566`：日志不再出现 `wp-arrive` 的 SPDCTX 钳制（`SPDCTX ... ctx=wp-arrive` 计数 0），
   列车以原速穿过路点，`x` 不再逐格递减到 0。
2. 通过路点后出现 `TRP veh=40566 ... dest=<下一条命令目的地> => ok=1 res=1`（不再零命中）。
3. 等待区路点（GOTO_WAYPOINT + 下一条 GOTO_COUPLE）仍出现 `ctx=wp-arrive` 且停稳、`WP-ENTER stop=1`。
4. 负判据：普通车站到站（非路点）路径逐字不变（本改动只碰 `Waypoint::IsExpected` 分支）。

### 11.19 构建自证（第 386 轮）

复用 `R3R_fullbuild.cmd inc detach`（未新建任何 `.cmd`）：

- `build\R3R_incbuild.guard.log` = `GUARD: incremental is safe (no header/lang file is newer than the newest object)`；
- `[2/3] Building CXX object CMakeFiles\openttd_lib.dir\src\train_cmd.cpp.obj` → `[3/3] Linking CXX executable openttd.exe`；
- `build\R3R_incbuild.done` = `EXIT_CODE=0`，日志 `error C*/fatal error/FAILED:/build stopped` 计数 **0**；
- 时间戳链：`src\train_cmd.cpp` 2026-11-02 14:21:19 → `train_cmd.cpp.obj` 14:23:49 → `build\openttd.exe` **14:23:54（52 420 096 B）**
  （首轮 14:20:30 已完成；随后仅把注释里的条目编号 KI-538 改为 KI-553 并重建，二进制不变）；
- `read_lints` 0 条；本轮未新增日志字面量（`wp-arrive` 为既有串），故不做 exe 串核对。

**状态：已实现 + 已编译，游戏内复测待做**（判据见 §11.18）。KI 条目 = `R3R_KNOWN_ISSUES.md` 的 KI-553。

---

## 十二、第 386 轮续（2026-11-02 玩家复测反馈）：预留在路点停止**依然存在**

### 12.1 玩家原话与指向

> 「关于预留在路点停止的问题依然存在，我怀疑这与我们之前做的"让路点成为预留能够截止的地方"有关，
> 请你根据 KI 和当时的临时备忘定位改了什么」

⇒ KI-553（`TrainEnterStation` 的 `cur_speed = 0`）不是唯一成因。玩家指出的方向更根本：
**曾经有一整套"让路点在 PBS 里当终点"的机制**（KI-429 家族，2026-10-22 第 276 轮引入），
第 386 轮虽已删，但必须逐条核对是否删净、是否有第二条通路仍在把预留截在路点。

### 12.2 已核实：KI-429 截断机制的四个落点，现已全部删除（证据）

| # | 落点（原） | 当前状态 | 证据 |
|---|---|---|---|
| 1 | `yapf_destrail.hpp` 的 `couple_wait_cut` / `couple_wait_cut_wp` 成员 + `SetCoupleWaitCut()` | **已删** | `yapf_destrail.hpp:427-430` 只剩「第 386 轮 KI-551 … 整机制删除」注释；全 `src` 搜 `SetCoupleWaitCut` 只命中注释（`yapf_rail.cpp:1214`） |
| 2 | `PfDetectDestination()` 里「截断态只接受等待区路点作终点」（打 `PFD-ACCEPT kind=waitwp`） | **已删** | `yapf_destrail.hpp:438-441` 注释 + 443 起直接走普通「platform or depot」判定 |
| 3 | `DoTrainCouplePathfind()` 接收 `wait_wp` 并把 YAPF 终点收窄到路点 | **已删** | 签名现在是 `static Track DoTrainCouplePathfind(const Train *v, bool do_track_reservation)`（`train_cmd.cpp:15793`），唯一调用 `:17387 DoTrainCouplePathfind(consist, true)` |
| 4 | `TrainLocoHandler` 进截断态首 tick `FreeTrainTrackReservation()` 清长预留 + `CPL-WAITCUT` 探针 | **已删** | `train_cmd.cpp:17371-17382` 整段改成注释：「已整机制删除…不再主动清预留」 |

⇒ **结论 A：KI-429 的「截断=把路点当预留终点」四件套已经不在代码里了。**
所以现在看到的"预留在路点停止"不可能由它们直接产生，必然是别的通路。

### 12.3 仍在的、与"路点=终点"相关的三处（待验，下一轮取证）

- **U-554-1 `UpdateOrderDest()` 的 `case OT_GOTO_WAYPOINT`**：上游把 `dest_tile` 设成路点格，
  于是**任何**寻路（含普通寻路）的终点天然就是路点格 —— 这是上游语义，本身就会让预留"止于路点格"。
  待查：R3R 是否在此处把 dest 改成路点**末格/停点**（比上游更靠前），或加了"到路点即 STOP_FOUND"。
- **U-554-2 普通寻路的目的地判定**（`CYapfDestinationRailwayT::PfDetectDestination`，与 couple 那个类不是同一个）：
  路点是 `TileType::Station` + `IsRailStationTile` ⇒ 天然被接受为终点；待查是否另有 R3R 早接受分支。
- **U-554-3 `R3RShouldCutCoupleAtWaitPoint()`（`train_cmd.cpp:14203`，仍活着）+ `_r3r_wbar_state` 墙机制**：
  目前只作「能否做挂接 / 是否已越墙」判据（`:18258` `R3RCoupleWaitWallBlocks` 之类），
  待查它是否在 `ProcessOrders`/`TrainLocoHandler` 里间接影响"订单推进"或"下一次订路"。

### 12.4 待验证判据（下一轮）

1. 同场景日志：路点前最后一次 `TRP ... dest=x,y`、以及该订单 `real/type`；若 `dest` 恒等于路点格 ⇒ U-554-1 成立。
2. 路点到达那一 tick 是否有 `WP-ENTER`（`station_cmd.cpp`）+ 紧随的 `TRP`；若 `WP-ENTER stop=0` 且随后**没有任何** `TRP`
   ⇒ 卡点仍在 `TrainLocoHandler` 的停驶分支或订单推进副作用，不在预留截断。
3. 若出现 `CPL-WAITCUT` 行 ⇒ 说明还有未删净的截断通路（当前代码里该探针应已不存在，故此为"复现即回归"的硬判据）。

---

## 十五、第 386 轮续之四（2026-11-02）：按玩家口径删除 `treat_waypoint_as_safe`（已改码）

### 15.1 玩家口径（本轮唯一依据，原话）

> 「路点」天然就是预留的截止面，你这个说法是错误的，在原生OTTD包括R3R古早一点的版本，
> 都没有说预留能够在「路点」截止的语义，这绝对是R3R加上去的。还有，请你不要混淆
> 「寻路终点」和「预留终点」的概念，路点可以作为YAPF的寻路终点，但不可以成为预留截止的
> 「安全停车位」

⇒ 直接推翻 §13.3 的"上游语义"判定（该判定已在 §14.1 撤回，本轮不再引用）。

### 15.2 落地改动（4 处调用点 + 1 处定义 + 1 处声明）

| 文件:行 | 改动 |
|---|---|
| `src\pbs.h:240` | 删第 6 形参 `bool treat_waypoint_as_safe = false` |
| `src\pbs.cpp`（原 `:2686-2704`） | 删参数文档段 + 删整条分支 `if (treat_waypoint_as_safe && IsRailWaypointTile(tile)) return true;`；签名同步去掉第 6 参 |
| `src\pbs.cpp:1513` | `FollowTrainReservation()` 计算 `res.okay` 时不传第 6 参（这是 `RESV-END ... okay=` 的唯一来源） |
| `src\train_cmd.cpp:16042` | 回溯 walk 的安全位判定不传第 6 参 |
| `src\train_cmd.cpp:17873` | `while (!IsSafeWaitingPosition(...))` 推进预留终点时不传第 6 参 |
| `src\train_cmd.cpp:17939` | `RESV-TERM` 探针的 `safe=` 独立见证不传第 6 参 |

**保持不变（防止误删）**：

- `R3RTrainStopsAtWaypoint()`（`train_cmd.cpp:669`）与 `:1396-1402` 的 advisory speed 分支
  —— 这是**订单层**"在路点停车"，与"预留终点"无关，KI-448 语义完好。
- `IsWaitingPositionFree()`（`pbs.h:260`）本就没有该参数，未动。
- `src\rail_cmd.cpp:700`、`src\pathfinder\yapf\yapf_rail.cpp:162`、`yapf_destrail.hpp:185`、
  `yapf_costrail.hpp:733/740` —— 本来就不传第 6 参，签名收窄后自动兼容，**未改一字**。
  ⇒ 这正是"路点仍可作 YAPF 寻路终点"的保证：寻路侧从来没被这个参数影响过。

> 注：`src\train_cmd.cpp.r3r_bak` 是本轮之前的备份文件，不参与编译，其中仍是旧签名，**不要**当残留处理。

### 15.3 预期效应（修复机制说明）

删除后，`res_dest.tile` 落在路点格时 `IsSafeWaitingPosition()` 不再直接返回 true，而是走
"下一格是否有信号/是否为尽头"的常规判定 ⇒ `while` 循环会继续 `SwitchToNextOrder(true)` +
`DoTrainPathfind()` 把预留延伸到路点之后第一个真正安全的位置。

于是：

- `RESV-END ... okay=1` 不再出现在路点格（若仍出现，说明还有别的通路把路点判成安全位，属回归）；
- `veh=15644` 那种"预留终点 = 路点格、到点即刹到 5 km/h 蠕动"的形态应消失；
- 路点仍能作为 `GOTO_WAYPOINT` 的**寻路终点**被到达（`PfDetectDestinationTile` 未动）。

### 15.4 风险与判据（复测必看）

风险 R-386-11：第 284 轮的注释声称该参数是为了消除"预留跨过路点导致 whack-a-mole"。删掉后若
重现反复 reserve/clear，说明真因在订单层（`R3RTrainStopsAtWaypoint` 让车停在路点而预留越过它），
需另立条目，**不再回退本改动**（玩家已明确该语义必须删）。

复测判据：

1. 同场景（`veh=15644` 过 `4518,1466` 路点）不再出现"到路点后 spd 掉到 5/30 蠕动"；
2. `RESV-END` 的 `endTile` **不等于**路点格，或 `okay=1` 不在路点格上；
3. `RESV-TERM ... safe=1` 成为常态（`safe=0` 只在真不安全/junction 场景出现）；
4. 路点订单照常被判定到达（`WP-ENTER` 仍在，订单 `real` 正常推进）；
5. 负判据：站台末端尽头、车库、闭塞/PBS 信号处的停车点行为逐字不变；
6. 负判据：`R3RTrainStopsAtWaypoint()` 的"在路点停车等挂接"场景（KI-448）仍能停住。

### 15.5 状态

**已改码 + 已构建成功**（见 §十六）。§十五 之前构建的 exe 不含本次改动。

---

## 十六、构建自证（第 386 轮续之四，删参后）

| 项 | 读数 |
|---|---|
| 入口 | `R3R_fullbuild.cmd inc detach`（复用既有唯一入口，未新建 .cmd） |
| 护栏 | 改 `pbs.h` ⇒ 判全量，实际 `MODE=inc` 但步数 692 全量 |
| 结尾 | `[692/692] Linking CXX executable openttd.exe` |
| 判决 | `build\R3R_incbuild.done` = `EXIT_CODE=0`；`R3R_incbuild.log` 尾 `2026/11/02 周一 15:56:17.15 MODE=inc EXIT_CODE=0` |
| exe | `build\openttd.exe` @ `2026/11/2 15:55:57`，`52 419 584 B` |
| 时间序 | exe(15:55:57) 晚于 `pbs.h` / `pbs.cpp` / `train_cmd.cpp` 全部改动 ⇒ 无陈旧 obj |
| lint | 三文件 0 条 |
| 停用核对 | 构建前 `tasklist` 确认 `openttd.exe` 未运行（避开 EXIT_CODE=97） |

## 十七、第 386 轮续之四后的第一份「依然存在」报告判读（2026-11-02）

### 17.1 硬结论：这份日志不是新 exe 产生的

| 对象 | 时间戳 |
|---|---|
| `build\R3R_debug.log` | `2026/11/2 15:05:55`，`15 270 366 B` |
| `build\openttd.exe`（含本轮删参） | `2026/11/2 15:55:57` |

⇒ 日志比新 exe **早 50 分钟**，必然由上一版 exe（`15:54:58` 那一次的 pbs.cpp 上限修法版，
**不含** `treat_waypoint_as_safe` 删除）产生。

**因此「路点处预留截止的问题依然存在」属预期，不是回归，也不是修法失败。**
判据链：删参改动落地时间（15:56 链接完成）晚于该日志结束时间（15:05）⇒ 该日志在物理上
不可能体现删参效果。

### 17.2 该日志的价值：删参前基线（复测对照物）

日志末尾仍完整保留旧行为，可作为 A/B 对照：

```
:89738 RESV-END veh=15644 tick=10105203 from=4506,1466 endTile=4518,1466 endTd=9
       endPos=199 curPos=0 endBits=0202 junction=0 real=8 spd=0
```

`endTile=4518,1466` 正是路点格、`endPos=199` 是路点格内位置 ⇒ 预留就地收尾在路点上，
与 §14.4 认定的 `treat_waypoint_as_safe` 分支行为逐字一致。

### 17.3 下一步（等玩家用新 exe 复测）

必须用 `build\openttd.exe` @ `2026/11/2 15:55:57`（或更新）重跑同一场景，然后按 §15.4 判据判读。
**取数前先核时间戳**：日志 mtime 必须晚于 exe mtime，否则一律判「样本无效、不予判读」，
这是本轮踩过的坑（§17.1）。

新的正判据（针对同一条 `veh=15644` / 路点 `4518,1466`）：
1. `RESV-END ... endTile=` 不得等于 `4518,1466`（应越过它到下一安全位，通常是信号/尽头）；
2. 若仍有 `endTile=4518,1466`，则同一行 `endPos` 必须不是格内停车位，且必须伴随
   `RESV-TERM ... safe=0`（路点不再被认作安全位）；
3. `WP-ENTER`/订单推进照常（路点仍是合法寻路终点，§14.1 的两概念分离不得退化）；
4. 不再出现「到路点后 spd 掉到 5/30 蠕动」。

---

## 十八、复测通过：KI-553 / KI-554 结案（2026-11-03）

### 18.1 样本有效性（先核时间戳）

| 对象 | 时间戳 | 判定 |
|---|---|---|
| `build\openttd.exe` | `2026-11-02 15:55:57`，`52 419 584 B` | 含删参的版本 |
| `build\R3R_debug.log` | `2026-11-03 01:06:05`，`46 505 263 B` | 晚于 exe ⇒ **样本有效** |

与 §17.1 的无效样本（日志早于 exe 50 分钟）区分清楚。

### 18.2 判据核对结果

| # | 判据 | 实测 | 结论 |
|---|---|---|---|
| 1 | `RESV-END` 的 `endTile` 不得等于路点 `4518,1466` | 全日志 `RESV-END` 共 **365** 次，其中 `endTile=4518,1466` **0** 次 | **通过** |
| 2 | 路点仍可作 YAPF **寻路**终点 | `4518,1466` 仍出现 55 次，全部属 `R3RDUMP`（车在路点格上）、`RPFAIL`（`destTile=4518,1466`）、`CPLRES`（`destTile=4518,1466`）——即寻路侧，无一为预留终点 | **通过（两概念未退化）** |
| 3 | 不再出现到路点后 5/30 km/h 蠕动 | `veh=15644` 在该路点之后最后一次出现为 `:319627 LINEND veh=15644 tile=4498,1373 spd=65 brk=180`，前两行 `spd=68` / `spd=71`，`brk` 随信号递增（`sig=1`）⇒ 正常进站/信号减速，非蠕动 | **通过** |
| 4 | 订单层「路点停车等挂接」（KI-448）不受影响 | 本轮未动 `R3RTrainStopsAtWaypoint()` 与 advisory speed 分支 | 未回归 |
| 5 | 站台尽头/车库/PBS 信号停车点不变 | 未动这些路径（`IsSafeWaitingPosition` 其余 6 处调用点本来就不传第 6 参） | 未回归 |

### 18.3 结论

- **KI-553（路点预留截止 / 蠕动）＝ 已修 + 复测通过，结案。**
- **KI-554（探针刷屏，低）** 本体未修，仍挂账；本日志 46 MB 亦印证刷屏仍在，但不影响功能。

### 18.4 保留的判据（防回归）

此后凡改 `IsSafeWaitingPosition()` / 预留终点推进逻辑，先看这两条：

1. `endTile` 落在路点格 ⇒ 直接判回归（路点永远不是安全停车位）；
2. 路点从 `RPFAIL`/`CPLRES`/`PfDetectDestinationTile` 一侧消失 ⇒ 另一种回归（寻路终点被误删）。

第 284 轮引入该参数的理由（"消除预留在路点附近 reserve/clear 反复"）在本次全量日志中**未复现**，
故 §15.4 的风险 R-386-11 判为未命中。



