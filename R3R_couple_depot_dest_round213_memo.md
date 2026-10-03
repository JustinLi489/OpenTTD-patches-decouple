# R3R 第 213 轮临时分析报告：GOTO_COUPLE 目的地是车库时，耦合寻路看不见库内等待车底

- 轮次：第 213 轮（2026-10-03）
- 来源：玩家口述（承接第 210～212 轮遗留场景：机车 57 → 车库 27,7；另见 KI-309 车库死锁）
- 状态：**取证 + 定性 + 已按玩家口径实现（S1）+ 已编译**（游戏内复测待做）
- 关联：KI-309 / KI-307 / KI-316（第 212 轮兜底）/ KI-291（第 189 轮：库里干等）

## 一、玩家原话与需求

> 「列车的挂接寻路总是寻路到等挂列车所在 tile，但是那个场景等挂列车在车库里，挂接寻路根本找不到！所以我们需要的修复是当前往挂接的目标是车库的时候，直接去目标车库，再在车库里扫描符合列车，而非站台的先扫描后寻路」

- R-a：确认「耦合寻路只看得到站台上的等待车底、看不到库里的等待车底」；
- R-b：**修法方向**——`GOTO_COUPLE` 目的地是**车库**时，不再走「先扫描出等待车底、再寻路到它」的站台模型，而是**直接按「去这个车库」开车**；
- R-c：机车**进库之后**，由**库内扫描**去找符合（订单/分组/公司）的车底并挂接。

即：站台 =「先扫描、后寻路」；车库 =「先到库、后扫描」。

## 二、结论摘要

玩家诊断成立，且是三层叠加：

1. CPL 对车库格的判据是**必须带轨道预留**（F2），而**停稳在库里的车底在车库格上没有预留**；站台格另有一条「整站台扫描」补丁（F3），**车库格没有对应分支** ⇒ 库内车底永远进不了 CPL 的目标集 ⇒ `CPL-PATHFOUND found=0`。
2. 即便进了目标集，CPL 的**回退安全位探测对任何车库格直接 `fail=depot`**（F4）⇒ 回溯必失败；KI-309 另已记录 F5「寻路扩展不能进入车库格」⇒ **车库在 CPL 里是拓扑死胡同**。
3. 第 212 轮兜底只在 `res_dest.tile != INVALID_TILE && !res_dest.okay` 时才「改走普通寻路开往命令目的地」（F7）；车库型目的地能否吃到这条兜底取决于 `res_dest.okay`（U1）。吃不到 ⇒ `MarkTrainAsStuck()`，机车停在库门口——正是第 210 轮 `veh=57` 在 27,7 库门口的形态。
4. 库内挂接整块（F9）要求 `track == TRACK_BIT_DEPOT`（机车已在库里）；进不去就永远够不着。

⇒ 现状是「扫描（看不见）+ 寻路（进不去）+ 库内扫描（到不了）」三处全断。玩家的 R-b/R-c 正是把这三处接起来。

## 三、代码取证（锚点与事实）

| 编号 | 锚点 | 事实 |
| --- | --- | --- |
| F1 | `yapf_destrail.hpp:349` | CPL 目标集只收「站台格或车库格」。 |
| F2 | `yapf_destrail.hpp:357-359` | `if (!has_res && !IsRailStationTile(tile)) return false;` ⇒ 车库格**必须带预留**才有资格当挂接目标。 |
| F3 | `yapf_destrail.hpp:370-381` | `t == nullptr && IsRailStationTile(tile)` 时沿站台轴正反扫描整条站台找车底；**只对站台做，无车库版本**。 |
| F4 | `yapf_rail.cpp:112-121` | `FindSafeCouplePositionProc()`：车库格 ⇒ 打 `FSCP … fail=depot` 并 `return false`。 |
| F5 | `yapf_rail.cpp:770-771`（KI-309 已记） | 寻路扩展不能从车库格继续（起点除外）⇒ 目标在库内时拓扑不可达。 |
| F6 | `train_cmd.cpp:13384-13396` | `OT_GOTO_COUPLE` ⇒ **无条件先**跑 `DoTrainCouplePathfind()`，成功才采用其结果。 |
| F7 | `train_cmd.cpp:13397-13448` | 第 212 轮兜底：**仅当** `!res_dest.okay` 才 `DoTrainPathfind()` 开往目的地并打 `COUPLE-NOTARGET-FALLBACK`；否则 `:13449 MarkTrainAsStuck()`。 |
| F8 | `train_cmd.cpp:12161-12163` | `CheckTrainStayInDepot()`：`OT_GOTO_COUPLE` 的车**只在「所站车库 == 命令车库」**时留在库里（KI-291 已修）⇒「进库后停住」已具备。 |
| F9 | `train_cmd.cpp:16778-16950` | 库内挂接块：需 `track == TRACK_BIT_DEPOT` + `IsEngine()` + `couple_targets_this_depot`（订单目的地 depot == 当前 depot）或 `r3r_pending_depot_couple`，通过后调 `TrainCoupleHandler()` 做库内扫描。 |
| F10 | `train_cmd.cpp:11600-11601`（注释） | 库内扫描只查车库格 + 门口那一格 ⇒ 该能力**已经存在**，缺的只是「让机车进来」。 |
| F11 | `couple_group.cpp:322-328`（KI-307 锚点） | 候选判据**没有**排除「躺车库格的车底」⇒ 站台型目的地反被库内车底吸引（与本轮方向相反）。 |

## 四、现状流程推演

1. `OT_GOTO_COUPLE` + `GetCoupleIsDepot()` ⇒ 目的地解析为**车库格**（`UpdateOrderDest` 的 `SetDestTile(Depot::Get(depot_id)->xy)`）。
2. `ChooseTrainTrack()` 进 `:13384` ⇒ **无条件**先跑 CPL（F6）。
3. CPL 组目标集：库内车底所在格是车库格、停稳无预留 ⇒ F2 直接 false；F3 的站台补丁不适用 ⇒ **目标集为空** ⇒ `found=0`。
4. 兜底门 `:13423`：`okay == true` 且未 `long_reserve` ⇒ 兜底不执行 ⇒ `MarkTrainAsStuck()`（第 210 轮 `veh=57` 的 `stuck=1`）。
5. 即便兜底执行（`!okay`）：机车被普通寻路带向 `Depot::xy`，**这一段方向是对的**；本场景 F9 的正门（订单目的地 depot == 当前 depot）**恰好满足**，进库后库内扫描应当能接手 ⇒ **真正断掉的是第 4 步（机车没被送到库里），而非库内扫描本身**。
6. 反向旁证（KI-309）：订单目的地是**站台**、两车都在库内 ⇒ F9 被正门挡住 + F5 让 CPL 无路 ⇒ 死锁。
   ⇒ 两方向合起来：**「订单目的地类型」与「车底实际所在」不一致时，现有三条路互不衔接。**

## 五、修法（玩家方案落地拆解；S1 已实现）

- **S1（本轮核心）**：在 `:13384` 之前判 `consist->current_order.GetCoupleIsDepot()`；为真则**跳过 CPL 与配对锁扫描**，直接走 `DoTrainPathfind()` 到 `Depot::xy`（等价于把第 212 轮兜底改成「车库型无条件」，不再受 `res_dest.okay` 门限制）。预期新增探针 `COUPLE-DEPOT-DIRECT veh=… depot=x,y`。
- **S2**：进库后停住 —— 由 F8 的 `CheckTrainStayInDepot()` 已有逻辑负责，**预期不改**；需复测确认不会走 `NormalizeTrainVehInDepot()` 被踢出（U2）。
- **S3**：库内扫描 —— 由 F9/F10 的现有 `TrainCoupleHandler()` 库内分支负责，**预期不改**；即需求 R-c 事实上已实现，本轮只是让它够得着。
- **S4 与既有条目关系**：
  - KI-309 修法甲（把 `couple_targets_this_depot` 由「订单声明」放宽为「配对锁目标就在本库」）与本轮**互补**：S1 保证机车到得了库，甲保证到了库一定能挂。
  - KI-307 甲（站台目的地不收车库候选）与本轮**方向相反、互不冲突**：那是「站台命令别被库内车底吸走」，本轮是「车库命令要能进库找车底」。
  - 不建议只做 S1 就宣布玩家场景闭环：若 KI-309 的现场仍是「订单写站台、车底在库」，S1 不起作用（那是 KI-309 甲的活）。

## 六、未确认项

- **U1**：车库型目的地下 `res_dest.okay` 的实际取值（决定第 212 轮兜底今天是否已经生效）。判据：现场是否有 `COUPLE-NOTARGET-FALLBACK`。
- **U2**：机车进入目标车库后 `CheckTrainStayInDepot()` 实测走哪一支（是否被 `NormalizeTrainVehInDepot()` 踢出）。
- **U3**：目标车库里**已停着**等待车底时，机车还能否进入该库（本次假设可以，未取证；若不可以，S1 必须配套「门口等待」形态）。
- **U4**：`R3REnsureCouplePair()` / `CPL-PAIR` 在库内场景是否会把锁先锁到别的候选。

## 七、复测判据（实现后）

1. 车库型 `GOTO_COUPLE` 不再常驻 `CPL-PATHFOUND found=0`；
2. 出现「机车进入目标车库」证据（进库探针 / `DEPOT-ARR … destDepot` 命中 / `tileEqDest=1`）；
3. 库内出现 `CG-GATE` / `CPL-PAIR dist=0` / `COUPLE-OK`；
4. 站台型 `GOTO_COUPLE` 行为逐字不变（无回归）；
5. `read_lints` 0、增量 `EXIT_CODE=0`、`build\openttd.exe` 晚于所改 `.cpp`。

## 八、本轮改动（第 213 轮，2026-10-03）

阶段一：取证 + 定性 + 备忘 + KI-320 登记（未改源码）。
阶段二：按玩家口径实现 **S1**，**仅改 `src\train_cmd.cpp`，未碰任何 `src\*.h`**。

### 8.1 实现落点（S1）

1. `ChooseTrainTrack()` 内、原 `if (consist->current_order.IsType(OT_GOTO_COUPLE))` 耦合块**之前**新增：
   ```cpp
   const bool r3r_couple_depot = consist->current_order.IsType(OT_GOTO_COUPLE) && consist->current_order.GetCoupleIsDepot();
   ```
   并把耦合块的门改成 `... && !r3r_couple_depot`（`:13430`）⇒ **车库型订单完全不跑 CPL**（不扫描、不上配对锁）。
2. 车库型改由**普通寻路**处理，与 `OT_GOTO_DEPOT` 同路：`YapfTrainChooseTrack` 的目的地函子 `CYapfDestinationTileOrStationRailT::SetDestination()` 对 `OT_GOTO_COUPLE` 落 `default:` 分支取 `v->dest_tile`（车库型由 `UpdateOrderDest()` 设为 `Depot::Get(id)->xy`），`PfDetectDestination()` 判 `tile == dest_tile && HasTrackdir(dest_trackdirs, td)` ⇒ 直达车库格。
3. 为此放宽第 212 轮兜底的两个 `OT_GOTO_COUPLE` 门为 `(!IsType(OT_GOTO_COUPLE) || r3r_couple_depot)`：
   - `:13501`（「A path was found, but could not be reserved」→ 普通寻路分支）；
   - `:13527`（「保留 CPL 预留就早退」的守卫）。
   ⇒ 车库型不再被早退拦下、也不再受 U1 的 `res_dest.okay` 门限制（**U1 因此失去相关性**）。
4. 新增探针 `COUPLE-DEPOT-DIRECT veh=%d tile=%d,%d dest=%d,%d`（`:13421`），**边沿触发**：新静态节流表 `_r3r_couple_depot_news`（`:11639`）随离开 `GOTO_COUPLE` 订单一起 `erase`（`:11733`）。
5. S2（`CheckTrainStayInDepot`，F8）与 S3（`TrainCoupleHandler` 库内扫描，F9/F10）**未改**——需求 R-c 本已实现，本轮只是让机车够得着。

### 8.2 构建自证

- 复用既有 `_tmp_inc_build.cmd`（**未新建任何 .cmd**）；
- `build\R3R_incbuild.guard.log` = `GUARD: incremental is safe (no header/lang file is newer than the newest object)`；
- 日志 `[3/3] Linking CXX executable openttd.exe`，`error C` / `fatal error` / `FAILED:` / `build stopped` 计数 **0**；
- `src\train_cmd.cpp` 18:52:01 → `train_cmd.cpp.obj` 18:53:33（晚于源码）→ `build\openttd.exe` 2026-10-03 18:55:11（51 672 576 B）；
- `build\R3R_incbuild.done` = `EXIT_CODE=0`（18:55:42）；
- `read_lints` 0 条；exe 内可检索到 `COUPLE-DEPOT-DIRECT`。

### 8.3 仍未做 / 待复测

- **未做**：U2（进库后 `NormalizeTrainVehInDepot()` 是否踢出）、U3（目标库被等待车底占用时机车能否进入）、U4（跳过 CPL 后库内配对锁由 `TrainCoupleHandler()` 走 KI-309 甲路径是否如预期）均**未取证**；站台型耦合与 KI-307/KI-309 的联动回归未测。
- **待玩家复测**：第七条 5 条判据，外加「库内 `COUPLE-OK` 后是否稳定（不反复进库/出库）」。

## 九、玩家新口径（2026-10-03，待确认）

**玩家原话**：「额，那么你之前添加的那个等太久就直接回滚普通寻路改回去了吗，我不希望有这个功能」

### 9.1 先答问

**没有改回去。** 第 212 轮 KI-316 的兜底 `COUPLE-NOTARGET-FALLBACK` 仍在 `src\train_cmd.cpp:13443-13497`，**站台型路径一字未动**。本轮（第 213 轮）对该区域只做了两件事：

1. 在它**之前**插入车库型分支 `r3r_couple_depot`（`:13414` 声明 / `:13430` 跳过 CPL / `:13421` 探针 `COUPLE-DEPOT-DIRECT`）；
2. 放宽 KI-316 时期给 `OT_GOTO_COUPLE` 加的两处守卫为 `(!IsType(OT_GOTO_COUPLE) || r3r_couple_depot)`（`:13501`、`:13527`），好让车库型能走到普通寻路。

### 9.2 三处候选机制（需玩家指定要删哪一个）

- **M1「找不到就回滚普通寻路」** = `COUPLE-NOTARGET-FALLBACK`，`src\train_cmd.cpp:13443-13497`（门 `:13469` = `res_dest.tile != INVALID_TILE && !res_dest.okay`；打印 `:13484`；节流表 `_r3r_couple_fb_news`）。触发＝CPL 返回 `INVALID_TRACK`（全图找不到任何可挂目标），**立即**执行，**不是**等太久。后果＝放弃挂接，用普通寻路驶向订单目的地。
- **M2「车库型直接去车库」**（第 213 轮新增 = 玩家上一条要求的）= `:13414` / `:13430`。触发＝订单为 `OT_GOTO_COUPLE` 且 `GetCoupleIsDepot()`，立即且无条件。后果＝完全不跑 CPL，直达车库格。
- **M3「等太久就放弃」** = `COUPLE-SKIP-COMMIT-FAIL`，`src\train_cmd.cpp:11996-12034`，常量 `R3R_COUPLE_COMMIT_FAIL_LIMIT = 4`（`:11659`）。触发＝**候选就在眼前却挂不上**（折叠修正回滚 / NewGRF 拒绝等），4 个等待窗口 × `R3R_COUPLE_DEST_IDLE_LIMIT = 500` tick（`:11616`，≈15 s/窗口）⇒ 约 1 分钟后自动跳过订单。

### 9.3 「等太久」的真正归属 = 文档过期（本轮核对）

真正**没候选**（找不到 `u`）的分支**不跳过命令**：`src\train_cmd.cpp:11893-11921` 是**原地无限等待**，每 500 tick 只打一行心跳 `COUPLE-DEST-EMPTY … (waiting in place, order kept)`，**命令保留**。

但两处文档仍写着「满 `R3R_COUPLE_DEST_IDLE_LIMIT` tick 后跳过命令」，属第 109 轮旧行为的残留描述（第 112 轮起已改为保留）：

- `R3R_KNOWN_ISSUES.md` 的 KI-316 条目第 212 轮修复段（该文件 `:10089`）；
- 代码注释 `src\train_cmd.cpp:13457-13460`。

⇒ 待随本次口径一并清理（纯注释/文档清理，不含行为变更）。

### 9.4 玩家指认与待拍板

- **玩家 2026-10-03 追问原话**：「既然如此，我想先了解上一轮或者上上轮你的修改，你是否在那时候添加了一个找不到路就回滚普通寻路的功能，我记得有，而我想要删除的就是那个（但是现在，先不删，我要谨慎的询问你）」。
- **指认成立**：玩家记忆正确 —— 该功能确实由我加入，就是 **M1**（KI-316 变体 (a)，**第 212 轮** 落地）。
- **轮次归属（答"上一轮还是上上轮"）**：**第 211 轮无任何代码改动**（纯只读取证）；**第 212 轮**＝新增 M1；**第 213 轮（上一轮）**没有新增同类机制，它只加了车库型无条件直去车库（M2），并把 M1 给 `OT_GOTO_COUPLE` 加的两处门（`:13501`、`:13527`）放宽 ⇒ 第 213 轮反而把 M1 的适用面**扩大到车库型**。
- **玩家决定**：**暂缓删除**（"现在先不删"）⇒ 本轮不动源码、不构建，等玩家最终拍板。
- **仍待拍板**：M1、M3，还是 M1+M3？M2 为第 213 轮新功能；若连 M2 也不要，则退回 KI-321 修复前的状态（车库型再次永远到不了库）。
