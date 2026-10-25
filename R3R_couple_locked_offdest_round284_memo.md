# R3R 第 284 轮：配对已锁定、主动方却停在 13 格外且挂接寻路恒失败（`COUPLE-FAIL` 每 2 tick）

> 本轮状态：**取证中**（先落盘，后深挖）。任何结论必须带行号锚点；未实证的一律标「猜测根因（待验证）」。

## 一、输入

| 项 | 值 |
|---|---|
| 玩家原话 | 「现在还是有些问题，请你看看」 |
| 日志 | `D:\temp\codebuddy-dropped-files\b002843a-6be4-4dc7-84d4-4869f32771a0\R3R_debug.log` |
| 体积 / 行数 / mtime | 219 805 B / 1 855 行 / 2026-10-23 17:33:07 |
| 产生者 | `build\openttd.exe`（第 283 轮 D-6b 版，17:30:07，52 272 640 B） |
| 上下文 | 承接 KI-450（第 278 轮 D-2/D-4）、KI-451（第 283 轮 D-6b） |

## 二、已核对事实（带行号锚点）

### 2.1 头部载入快照（`:1-30`）

- `SEGLOAD-PARK` 一批：veh=3..23 `seg=1 gid=3`；veh=21 `unit=1` 名 `"JRW_关西机场快速 01"`；veh=24 `unit=2` 名 `"JRW_纪州路快速 01"`（`nbk` 同名，`gid=65534`）；veh=45 `ubk=3 gbk=65534`；veh=48 `ubk=4`。
- 结论句：veh=21 与 veh=24 是两条**不同车底/不同段**的链，24 自带 `unit=2` 与停放名备份。

### 2.2 等待区截断（全日志仅 2 行 `CPL-WAITCUT`）

```
:764   CPL-WAITCUT veh=21 state=1 wp=17 tile=248,233
:1266  CPL-WAITCUT veh=21 state=0 wp=17 tile=253,233
```

- 结论句：`state` 由 1 掉到 0，即 KI-450 根因 R278-1 描述的「放行口静默分支」在**本轮仍然发生**；且发生在 253,233（运动前端 `mf=259,233`，说明是倒车链 `db=1`，`mf` 看的是链尾方向）。
- 本轮 `WBAR-RELEASE` 是否留痕待查（见 Q-284-5）。

### 2.3 尾部稳态循环（`:1790-1855`，tick 3069 / 3071 / 3073，每 2 tick 一组 5 行）

```
COUPLE-WAIT-OFFDEST veh=21 tile=253,233 mf=259,233 real=12 destStn=12 destTile=479497
TPR-WALK veh=21 mf=259,233 origin=259,233 td=8 okay=0 exit=2 new=260,233 reach=1 spd=0 one=0 look=0
TRP veh=21 ... => ok=0 res=0
CPL-PAIR act=21 tgt=24 dist=13 actTile=253,233 tgtTile=265,234
COUPLE-FAIL loco=21 order=16 tx=253 ty=233 x=4050 y=3736
```

- `destTile=479497`：按 2048 宽地图换算 `479497 = 234*2048 + 265` ⇒ **(265,234)**，与 `CPL-PAIR tgtTile=265,234` 完全一致。⇒ 订单目的地就是车底 24 脚下那一格。
- 结论句：**配对锁已建立**（`CPL-PAIR act=21 tgt=24` 出现），所以第 283 轮 D-6b 门禁（要求 `R3RGetCouplePairPartner(v)==nullptr`）**按设计不生效**；本轮问题是配对面的新形态。
- 结论句：`spd=0`、`real=12`（`order=16` = `OT_GOTO_COUPLE`）、`dist=13` ⇒ 机车停住不动，挂接寻路每 tick `ok=0`，靠 `COUPLE-FAIL` 刷屏推进不了。

### 2.4 `CPL-WAIT-NOBOOK` 全日志 **0 命中**

- 结论句：第 283 轮新增探针一次没打。两种可能：(a) 门禁未命中（配对已建立 / 无等待区路点 / 未越点）；(b) 命中但被 `(_tick_counter + index) % 500` 节流窗口漏掉。见 Q-284-1。

### 2.5 更正：`CPL-WAITCUT` 是**耦合寻路侧**的截断状态，不是 `R3RShouldCutCoupleAtWaitPoint` 的返回旁路

- 打印点：`src\pathfinder\yapf\yapf_rail.cpp:854`，位于 `FindNearestCoupleTrain()` 入口（`:843` 调 `R3RShouldCutCoupleAtWaitPoint(v, &r3r_cut_wp)`），**状态变化才写一行**（`:847-860` 的 `std::map` 边沿触发）。
- 语义（`:838-841` 注释）：`0` = 不截断（无等待区路点 / 本次挂接已放行）；`1` = 截断，终点 = 等待区路点；`2` = 已站在路点上但未放行（`:862-867` 直接返回 `INVALID_TRACKDIR`，不跑 YAPF）。
- 结论句：`:764 state=1` → `:1266 state=0` 表示**截断已被放开**（放行），YAPF 从此刻起被允许一路搜到目标车底。所以后半段的失败**不是截断造成的**，反而是"放行之后仍然找不到路"。

### 2.6 `CPL-PAIR` 是每 tick 一行且带 `tick=`

- 打印点 `src\train_cmd.cpp:13158`，格式 `[R3R] CPL-PAIR tick=%u act=%d tgt=%d dist=%u actTile=%d,%d tgtTile=%d,%d`。
- 现场原文（`:1783`）`[R3R] CPL-PAIR tick=3066 act=21 tgt=24 dist=13 actTile=253,233 tgtTile=265,234`；随后 `:1801 tick=3069`、`:1819 tick=3071`、`:1838 tick=3073`。
- 结论句：**配对锁持续存在**（不是一次性噪声），且 21↔24 的距离恒为 13 格，21 的 `tile` 与 24 的 `tgtTile` 都没变 ⇒ 双方都停着，谁也不动。

### 2.7 `COUPLE-WAIT-OFFDEST` 与 `TRP ok=0` 的因果顺序

- `:1782` `TRP veh=21 mf=259,233 real=12 type=16 dest=479497 spd=0 fp=0 look=0 one=0 mstuck=0 fto=0 => ok=0 res=0` ⇒ 本 tick `TryPathReserveWithResultFlags()` 返回 `TPRRF_NONE`，**链路无任何预留**。
- `GetCouplePosition()`（`src\train_cmd.cpp:13458`）第一句就是 `FollowTrainReservation(v, &other_vehicle);`，`:13463 if (other_vehicle == nullptr) return nullptr;`。
- 结论句：无预留 ⇒ `other_vehicle == nullptr` ⇒ `GetCouplePosition()` 返回 nullptr ⇒ `TrainCoupleHandler()` 走 `u == nullptr` 分支打 `COUPLE-WAIT-OFFDEST`。**`COUPLE-WAIT-OFFDEST` 不是"目的地判错"，而是"没有预留所以找不到候选"的连带输出**；真正的堵点是 `TRY-RESERVE` 拿不到轨道。

### 2.8 `TPR-WALK` 字段解码（打印点 `src\train_cmd.cpp:16911`）

| 字段 | 代码来源 | 现场值 | 含义 |
|---|---|---|---|
| `okay=` | `origin.okay` | `0` | `FollowTrainReservation()` 给出的起点无效 ⇒ **当前无有效预留** |
| `exit=` | `TrackdirToExitdir(origin.trackdir)` | `2` | 走向下一格的出口（枚举名待核，见 Q-284-7） |
| `new=` | `TileAddByDiagDir(origin.tile, exitdir)` | `260,233` | 下一步要进的格（x+1，与 `exit` 一致） |
| `reach=` | `TrackdirBitsToTrackBits(...)` | `1` | `TRACK_BIT_X`（横轨）⇒ **下一格有轨道可用** |
| `one=` / `look=` | `HasForceReserveOnce()` / `lookahead != nullptr` | `0/0` | 一次性预留未被消费、无 lookahead |
| `spd=` | `cur_speed` | `0` | 停着（不再进入移动分支，见第 242 轮 KI-348 的结构性结论） |

- 结论句：`reachable != TRACK_BIT_NONE` ⇒ 代码**会**继续走 `:16924` 分支调 `ChooseTrainTrack(consist, 260,233, exitdir, TRACK_BIT_X, CTTF_FORCE_RES|...)`（`:16925`）。所以失败点在 **`ChooseTrainTrack` 内部**（PBS 占用 / 信号 / 端点为有车格 / 90° 限制），不是"下一格没轨道"。
- 反证：全日志尾部**没有 `TPR-BLOCKED`**（KI-330 U-1b 的"另一列车挡路"探针未触发）⇒ 不是 `CheckTrainsOnTrack` 命中。

### 2.9 失败点的代码位置：`FSCP` 的 `else if` 分支缺自身链豁免

调用链（本轮核实）：

```
TrainCoupleHandler() → DoTrainCouplePathfind(consist, true)       // src\train_cmd.cpp:16030（定义 :14795）
  → YapfTrainCoupleTrack(v, !do_track_reservation)
    → CYapfCoupleRail::FindNearestCoupleTrain(v, dont_reserve)    // yapf_rail.cpp:811
      → 回溯循环 while (pNode->parent)                            // yapf_rail.cpp:987
        → CheckSafePositionOnNode(pPrev)                          // :998，失败即 CPL-SAFE-FAIL(:1002) + return INVALID_TRACKDIR(:1007)
          → FindSafeCouplePositionProc(tile, td)                  // :118
            → FSCP 探针 + TryReserveRailTrack                     // :199
```

`FindSafeCouplePositionProc` 对「本格已有的预留」分两支：

| 分支 | 条件 | 是否带自身豁免 |
|---|---|---|
| A（:130-197） | `HasReservedTracks(tile, 本轮要的 track)` 为真——要的轨**已被订** | **有**：`:159 if (best->index == Yapf().GetVehicle()->index) return true;`（第 110 轮为"机车被自己脚下格锁死"加） |
| B（:198-310） | 要的轨**没被订**，但该格**别的轨**有预留（`GetReservedTrackbits(tile) != TRACK_BIT_NONE`） | **没有**：直接 `TryReserveRailTrack(tile, TrackdirToTrack(td))`，失败即 `fail=tryReserve` → `return false` |

现场读数（§2.4 已录，此处复述关键位）：
`:1448 FSCP tile=259,233 fail=tryReserve ... td=5 want=5 tbits=0x25 res=0x1` + `:1449-1451 FSCP-RES tile=259,233 tk=0 owner=21 ... isGtr=1 cpl=259,233`。

- 结论句 1：`tbits=0x25` = `TRACK_X(0x1) | TRACK_UPPER(0x4) | TRACK_RIGHT(0x20)` 三轨道岔格；`res=0x1` = 只有 `TRACK_X` 被订；`want=5` = `TRACK_RIGHT`。
- 结论句 2：`HasReservedTracks(tile, RIGHT)` 为假（res 里没有 0x20）⇒ 走的是**分支 B**，也就是**唯一没有自身豁免的那一支**。
- 结论句 3：`TryReserveTrack`（`src\rail_map.h:267-277`）的实现是 `res |= bits; if (TracksOverlap(res)) return false;`，`TRACK_X` 与 `TRACK_RIGHT` 在道岔格内交叉 ⇒ 必拒。所以 `fail=tryReserve` 的根因是**交叉轨冲突**，不是"轨道不存在"（`reserve_if_present` 那类存在性检查在 FSCP 里根本没走到）。
- 结论句 4：`FSCP-RES ... owner=21` ⇒ 那笔 `TRACK_X` 预留的归属正是**寻路机车 21 自己**（`isGtr=1`：`GetTrainForReservation()` 返回的就是它）。

⇒ **自锁机制成立**：21 自己脚下的 `TRACK_X` 预留，把 21 自己的挂接寻路要订的 `TRACK_RIGHT` 判成交叉冲突 ⇒ `FSCP` 拒 ⇒ `CheckSafePositionOnNode` 拒 ⇒ `CPL-SAFE-FAIL` ⇒ `CPL-SKIP retryIn=8` ⇒ 无路径、无预留 ⇒ `FollowTrainReservation` 无目标 ⇒ `COUPLE-WAIT-OFFDEST`。下个 tick 重复，8 tick 一次，永不自行解开。

### 2.10 更正：`veh=47` **不是**阻塞源

`:1450 FSCP-VEH tile=259,233 veh=47 track=0x1 dir=... front=0 ord=0` 只是"该格上停着哪些车"的**描述性 dump**（`:294-299`，无条件打印）。它进入否决链的唯一路径是分支 A 的 `check_train_on_tile`（要求 `tr->track` 含 `TrackdirToTrack(td)`，且随后过 `:159` 自身豁免 / `:164` `R3RIsCoupleTarget` / `:178` `R3RCoupleAllowed` / `:179` second_best 四道闸）。本轮走的是分支 B，**这四道闸一条都没执行**。

- 结论句：此前"`veh=47` 挡路"的猜测**证伪**；`veh=47`（`SEGLOAD-PARK seg=1 gid=3 borrowed=0 prio=1`、`front=0`、`ord=0`）与阻塞无关，它只是同时站在 259,233 上。真正的阻塞者是**21 自己的 `TRACK_X` 预留**。

### 2.11 即使放行 `FSCP`，`rp()` 也订不上——所以修法不能只补 `FSCP`

`rp()` 从 `st`（= `pNode->GetLastTile()`，即机车所在格）开始递归（`:1201 rp(st, best_td, 0)`），**返回成功后**才对当前格订预留：单比特支 `:1152`、多比特支 `:1178`，都调 `reserve_dir_if_present(cur, cur_td)` → `TryReserveRailTrackdir(v, t, td)`（`:1111-1114`），而它只是 `if ((bits & TrackToTrackBits(TrackdirToTrack(td))) != 0) TryReserveRailTrackdir(v, t, td);` ——**返回值被丢弃**。

- 结论句 1：`rp()` 会对 `st`=259,233 订 `TrackdirToTrack(best_td)`；若 `best_td` 落在 `TRACK_RIGHT`，与自持的 `TRACK_X` 同样 `TracksOverlap` ⇒ `TryReserveRailTrackdir` 内部返回 false，被静默丢弃 ⇒ 路径"看起来订成功"，实际这一格没订上 ⇒ 机车仍然不会动（或下一 tick 因 `res=0x1` 再次被 FSCP 拒）。
- 结论句 2：因此**只给 FSCP 分支 B 补自身豁免是不够的**，必须同时解决"自身旧预留"本身。

### 2.12 修法候选（未落地）

| 编号 | 做法 | 优点 | 风险/前提 |
|---|---|---|---|
| **F-284-1**（首选） | 挂接寻路入口先释放本链的轨道预留：`DoTrainCouplePathfind()` 之前（`src\train_cmd.cpp:16030` 一带）对本链调 `ClearReservationUnderConsist()` / 链级释放，再寻路 | 一次性解决"旧预留挡新路线"，FSCP 两支与 `rp()` 都不再撞自身；语义正当（挂接寻路本就要重订整条路径） | 需确认只清本链、且不清掉等待车底（对方）的整站预留；清空的瞬间列车失去 PBS 保护（它已停着，且同 tick 内重订） |
| F-284-2 | 仅给 FSCP 分支 B 补自身链豁免（`owner->First() == v->First()` ⇒ 不订不拒，`return true`） | 改动最小、不碰预留生命周期 | 按 §2.11，`rp()` 仍订不上 ⇒ 大概率只是把失败点后移，不解决自锁 |
| F-284-3 | 在 `rp()` 订 `st` 之前先放开本链在 `st` 上的旧轨（仅同格、仅自身） | 精准 | 同格释放/重订的时序易漏，且 `SetReservationTarget` 已在后面依赖该格 |

- 结论句：**F-284-1 是唯一能闭环的方向**；F-284-2 的价值仅在于让日志更准确（把"被自己挡"与"被外部挡"分开），可作附带探针。

### 2.13 `TRACK_X` 预留的**写入方**尚未坐实（决定修法能否闭环）

- 结论句 1：现场日志 `RESERVECONSIST veh=21` **0 命中**、`RESERVECONSIST.*259,233` **0 命中**（ripgrep count 模式）⇒ 21 没有走"车底脚下保护"那条打印路径。但这**不能**反推 `TRACK_X` 不是 `ReserveTrackUnderConsist()` 订的：该探针在 R3R 里是带条件边沿打印（第 110/125 轮引入的记账只在 `ok || HasReservedTracks` 分支写行），未打印 ≠ 未执行。
- 结论句 2：因此 `TRACK_X` 的写入方有两个互斥候选，**直接决定 F-284-1 是否有效**：
  - **W-1 普通 PBS 路线预留残留**（`TryPathReserve` 早先把 259,233 沿 `TRACK_X` 订出站，机车停稳后既不推进也不释放）⇒ **清了就不会回来**，F-284-1 有效；
  - **W-2 `ReserveTrackUnderConsist()` 每 tick 重订**（机车脚下站在 X 轨上）⇒ **清了立刻回来**，F-284-1 **无效**，只能走 F-284-2/3（放宽判据或临时换轨）。
- 判别方法（下一步，只需日志）：`RP cur=259,233 td=? trk=?` 是否出现、其 `td` 是否落在 `TRACK_X`（`td∈{0,1}`）；以及 `RESV-GC`/`CLEAR-RAIL` 是否涉及 259,233。若 `RP cur=259,233` 存在且 `trk=0` ⇒ W-1 成立。
- 待验证项 **U-284-9**：W-1 还是 W-2。判据见上；若判据不足，建议落一条只读探针 `FSCP-SELF-RES`（分支 B 中当冲突 owner 属于本链时打印 `tile/self_track/want_track/res`），零行为变更。

### 2.14 时间线收口（本轮第 4 组，决定性）：冲突预留被 GC 清掉，但"清完没人再订"

日志锚点（同一 tick 内连续）：

| 行 | 原文（节选） | 含义 |
|---|---|---|
| `:1442` | `CPL-PATHFOUND veh=21 found=1` | 挂接寻路**找到**候选路径（全日志仅 4 条 `CPL-PATHFOUND veh=21`：`:766 found=1`、`:826 found=1`、`:1267 found=0`、`:1442 found=1`） |
| `:1448` | `FSCP tile=259,233 fail=tryReserve td=5 want=5 tbits=0x25 res=0x1` | 回溯到 259,233，订 `TRACK_RIGHT` 被自持的 `TRACK_X` 判交叉冲突 |
| `:1452` | `CPL-SAFE-FAIL` + `CPL-SKIP retryIn=8` | 该 tick 放弃 |
| `:1454` | `RESV-GC veh=21 tile=259,233 track=0x0 why=free-path` | **旧 `TRACK_X` 预留被"找不到路径就清预留"的 GC 释放** |

- 结论句 1：`why=free-path` 能清掉这笔预留 ⇒ 它是**路线预留**（W-1），不是 `ReserveTrackUnderConsist()` 的脚下保护（后者不由 free-path 路径清）⇒ **§2.13 的 W-1 成立、F-284-1 有效**。
- 结论句 2：**顺序反了是这个 bug 的形态**——设计是"寻路失败 → 清预留"（`:1454`），于是唯一一次可能成功的寻路（`:1442`）被自己旧预留否掉，而预留随后被清掉、却**没有任何机制再跑一次寻路**（`CPL-PATHFOUND veh=21` 在 `:1442` 之后再无出现；21 停稳不进移动分支 ⇒ `TrainController`/`TryPathReserve` 不跑）。
- 结论句 3：此后每 2 tick 只剩 `TPR ok=0 res=0` + `CPL-PAIR dist=13` + `COUPLE-FAIL`（`:1790-1855`），即"无预留 ⇒ 挂接扫描无候选 ⇒ 不动 ⇒ 距离不变"的稳态，与 §2.7 一致。
- 结论句 4：因此修法应是「**寻路前预清本链预留**」（F-284-1），而非只放宽 FSCP（F-284-2）：预清后 `:1448` 的冲突不存在，`rp()` 也能正常订 `TRACK_RIGHT`，`:1442 found=1` 这次寻路就应变成真正成功并驱动列车前进。**且该改动等效于把既有 GC（`:1454`）提前一 tick，不是全新机制**。

### 2.15 修正 G-284-D 的因果（以本轮为准）

原 §2.9 表述"自持 `TRACK_X` 把寻路恒拒"只覆盖了**第一次**失败，不覆盖"此后为何永不恢复"。完整因果：

```
① 21 自持 TRACK_X（路线预留残留，W-1）
② 挂接寻路唯一一次 found=1 → FSCP 分支B 交叉冲突 → CPL-SKIP
③ RESV-GC why=free-path 清掉 TRACK_X（此时冲突已消失，但寻路不重跑）
④ 21 cur_speed==0 ⇒ 不进移动分支 ⇒ TrainController/TryPathReserve 不跑 ⇒ 预留不会重订
⑤ 挂接寻路无路径可回溯 ⇒ 只剩 COUPLE-FAIL ⇒ dist=13 恒定 ⇒ 永久冻结
```

⇒ ②③ 之间缺的正是"预清 + 重跑"的配对；修 F-284-1 后 ② 不再发生，③ 失去触发机会，链条断开。

### 2.16 待拍板（玩家按编号回填）

- **P-1：修法选哪个？**
  - `P-1a` = **F-284-1**（推荐）：在 `src\train_cmd.cpp` 的 `DoTrainCouplePathfind()` 调用点（`:16030`）之前，对本链做一次"释放本链轨道预留"（复用既有 `ClearReservationUnderConsist()` + 第 110 轮记账 `R3RReconcileConsistReservation()`），再寻路。等效于把既有 `RESV-GC why=free-path`（`:1454`）从"失败后"提前到"寻路前"，只清本链、不碰等待车底。
  - `P-1b` = F-284-1 **加** F-284-2（同时给 `FSCP` 分支 B 补自身链豁免 + `FSCP-SELF-RES` 只读诊断日志）：行为更宽，但会改变"自身预留也算障碍"的既有语义，需额外出负判据。
  - `P-1c` = 只做 F-284-2（诊断优先，不改预留生命周期）：不解决自锁，只能让日志把"被自己挡"与"被外部挡"分开。
- **P-2：清预留的时机边界** —— 是只在该链 `current_order.IsType(OT_GOTO_COUPLE)` 时清（最小影响面），还是所有挂接寻路一律预清？
- **P-3：是否同时处理"清完仍不想动"的兜底**（例如预清后仍 `found=0` 超过 N tick 时，标记 `Stuck` 让界面显示"等待空余轨道"而不是"正前往"）？

### 2.17 U-284-9 收口：那笔 `TRACK_X` = 21 自己「向东直行」的路线预留（W-1 成立）

关键读数（`R3R_debug.log:1448-1451`、`:762-763`；枚举见 `src\track_type.h:75-94`）：

| 读数 | 翻译 | 含义 |
|---|---|---|
| `:763 CPL-ORIGIN veh=21 origin=253,233 td=8 vehDir=1 vehTd=8` | `td=8 = TRACKDIR_X_SW`（`track_type.h:85`） | 21 本身**横着（沿 X 轴）**站在 253,233 |
| `:1449 FSCP-RES ... tk=0 owner=21 ... oend=260,233 oendtd=8` | `tk=0 = TRACK_X`；`oend=260,233`、`oendtd=8` | 21 的 PBS **预留终点在正东 260,233、X 轴**，且 259,233 的 X 轨归它 |
| `:1448 FSCP ... td=5 want=5 tbits=0x25 res=0x1 plain=1` | `td=5 = TRACKDIR_RIGHT_S` → `TRACK_RIGHT`（`track_type.h:82`） | 挂接寻路要从 259,233 拐上 `TRACK_RIGHT` 叉轨；`tbits=0x25`=`X\|UPPER\|RIGHT`，`res=0x1` 只有 X 被订 |

- 结论句 1：21 在 253,233、预留终点 260,233、同为 X 轴 ⇒ 它早先订了一条 **253,233→260,233 的东西向直行路线**，259,233 恰是这条路线**中间的一格** ⇒ 该格 `TRACK_X` 必然记在 21 名下（`res=0x1`）。
- 结论句 2：因此「横轨被占用」不是外车入侵、也不是 `ReserveTrackUnderConsist()` 的脚下保护重订，而是**21 自己一条还没走完的路线预留**（W-1 成立，与 §2.14 的 `why=free-path` 能清掉它互相印证）。
- 结论句 3：对修法的意义 —— F-284-1（寻路前清本链预留）清掉后**不会立刻被重订**（非脚下保护），可闭环；F-284-2 仍不足（`rp()` 同样撞交叉）。
- 结论句 4：`td=8` 与 `td=5` 分属 X 轴与 RIGHT 叉轨，`TracksOverlap(X, RIGHT)` 在 `tbits=0x25` 的道岔格内为真 ⇒ `rail_map.h:267-277` 的 `res |= bits; if (TracksOverlap(res)) return false;` 必拒，机制闭合。

### 2.18 **【更正 §2.17】横轨是挂接寻路自己铺的，且并非越界**

玩家原话（2026-10-23）：「挂接寻路要让它拐上斜轨道，但是它为什么会向横轨道铺预留呢，这是很明显的预留越界啊」

**§2.17 有三处错，此处更正，§2.17 结论作废：**

1. 错在「横轨是 21 早先的旧路线、本次挂接寻路没铺」。实际 `RP cur=` 七行连打（`build\R3R_debug.log:808-814` 与 `:868-874`）：
   `RP cur=248,233 td=8 depth=0 trk=0 bits=0x1` → `249` → `250` → `251` → `252` → `253` → `254,233`，全部 `trk=0`（`TRACK_X`）、`bits=0x1`。
   `rp()` 就是预留 lambda 本体（`src\pathfinder\yapf\yapf_rail.cpp:1083`），日志行在 `:1087` 打的。⇒ **横轨是本次挂接寻路订的，共 7 格。**
2. 错在「它不是要拐斜轨」。`FSCP tile=259,233 fail=tryReserve td=5 want=5 ... tbits=0x25 res=0x1`（`:1448`）：`want` 打印的是 `TrackdirToTrack(td)`（`:203`），`TRACK_RIGHT` 的枚举值是 **5**（不是位值 0x20），`td=5`=`TRACKDIR_RIGHT_S`。⇒ 在 259,233 这一格它**确实**要订右斜轨。
3. 错在「没走到 rp()」。`:816 CPL-RESERVE veh=21 reserved=1 next=8` 证明预留已经提交。

**但「预留越界」不成立。** 路线铺横轨是因为**这一轮的目标就在横轨上**：
- `:825 PFD-ACCEPT kind=waitwp tile=254,233`
- `:828 CPL-BEST veh=21 tile=254,233 td=8 cost=2400`
- `:816 CPL-RESERVE veh=21 reserved=1 next=8`
⇒ 目标 = 东边横轨上的等待路点 254,233，路线必然沿 `TRACK_X` 向东。铺横轨是对的。

**§2.17 的「所有权」论证也站不住（本条最重要）：**
`owner=21` 来自 `GetTrainForReservation()`、`othru=1` 来自 `TrainReservationPassesThroughTile()`，而本文件 `:236-258` 的注释（第 91 轮已纠正）明写：前者会沿预留走到一端去点名「站在那里的车」、站台格还会扫整条站台；后者语义是「这条链的预留**到达**该格」，**不是「这条链持有该格」**。⇒ `owner=21 / othru=1` 不能当持有者证据，§2.17「是 21 自己一条没走完的直行路线」**未证实**。

**当前唯一直接的持有者线索**：`:1451 FSCP-VEH tile=259,233 veh=47 track=0x1 dir=1 front=0 ord=0` —— 259,233 上物理站着一节车在 `TRACK_X` 上。它与 21 是同一列车还是另一列车（ord=0、front=0 更像等待中的散链/车底），本轮未定，列 REG-284-U3。

**待查疑点（新增）**
- `REG-284-U1`：`TRP veh=21 mf=259,233`（`:1310` 起）与 `CPL-ENTRY veh=21 tile=248,233`（`:823`）/ 后又 `tile=253,233`（`:1264`、`:1437`）并存；21 的车体到底覆盖哪一段、`mf` 为何在 259,233，需一次 GEO/链节 dump 定。
- `REG-284-U2`：259,233 要 `TRACK_RIGHT` 的那条候选（`td=5`）是怎么被选进 back-walk 的——机位压在横轨上却要在此格拐斜轨，属「自己挡自己」的准确机制，但「为何偏要在此格拐」未证实。
- `REG-284-U3`：`:1451` 的 `veh=47` 属于 21 链还是独立列车（决定横轨是被「自己车体」还是「隔壁车」占着）。

## 三、从源头抓：预留记账 + 单一定向源（玩家 2026-10-23 要求）

玩家原话：「我感觉我们在打地鼠，一只不见了又冒出来一只，然后你的分析和场景还原能力又稀烂，能不能从源头抓一下啊」

### 3.1 为什么一直在打地鼠（结构性，不是本领问题）

本会话已经先后给出三个互相矛盾的解释（别人的预留 → 21 的旧路线 → 挂接寻路自己铺的），每一个都能被日志片段支持。

**【2026-10-23 玩家追问「之前我们引入的预留账本系统是什么」后的重大纠错】** 上一版本文在此处写的「R-A 没有持有者账本」**是错的**。预留账本**早已存在**并已上线多轮：`src\pbs.cpp:155-177` 的 `struct R3RResvLedgerRec {TileIndex tile; Track track; uint8_t purposes; …booked;}` + 两张表 `_r3r_resv_by_owner`（按**链头车号**索引，正向：这列车占了哪些位）与 `_r3r_resv_owner_of_bit`（按 `(tile,track)` 索引，反向：这位归谁）；接口在 `src\pbs.h:95-126`：`R3RResvLedgerNote()`（记账/销账，**喂入点是地图层 setter 的钩子，不是业务调用点**）、`R3RResvLedgerOwner()`、`R3RResvLedgerForget()`、`R3RReleaseReservationSafe()`、`R3RResvLedgerForEachBit()`、`R3R_RESV_NO_OWNER=UINT32_MAX`；`pbs.h:36-40` 自陈它「是 real fix 而不是 probe」，且**只把 watched-tile 日志留在探针开关后面**；账本存 `R3RL` 块（`src\sl\couple_group_sl.cpp`）随存档往返，key 用 VehicleID 不用指针。此外 `src\pbs.cpp:124-147` 还写了三条维护法（Release law 按 purpose 位掩码摘除、Mirror law 定期与地图对账 `R3RResvLedgerResync()`、以及 owner 缺省的处置）。

⇒ 所以本轮「打地鼠」的真因不是「系统没有账本」，而是**我没有先去查这本已有的账，而是拿两个不证明所有权的老 API 去反推**（`GetTrainForReservation` / `TrainReservationPassesThroughTile`，其局限写在 `src\pathfinder\yapf\yapf_rail.cpp:236-258`）。工具存在但未被接进诊断流程 —— 这是本次流程失误，不是结构缺失。

三条结构性欠账（**已按上述纠错改写**）：

- **R-A（修正）账本存在，但「问一句就能答」的入口没被用起来。** 账本有 `R3RResvLedgerOwner(tile,track)`，watched-tile 机制也支持用 `r3r_resv_watch.txt` 免重编加关注格（`pbs.h:31-34`）⇒ 259,233 这类现场**本该一次问出「归哪列车、什么 purpose、booked 多少 tick 前」**，我却去反推了三轮。真缺口 = 流程/查询入口，不是数据结构；`RESV-ORPHAN` 那类「新建不变量」也不再需要从零造。
- **R-B 订轨来源不唯一。** `rp()` 里同时存在「TrackFollower 实测的 nd」和「推导出来的 track」（`ReverseTrackdir(cur_td)` 已由 KI-346 修；但平台段 `AxisToTrack(GetRailStationAxis(...))`、`oendtd`、`TrackdirToTrack(cur_td)` 仍在）。推导一旦与实测不符，就会订出**车根本不走的轨**，成为无主位。
- **R-C 生命周期不配对。** 订是逐格随手订；放是按「车节**当时**所在格」清（`ClearReservationUnderConsist`，KI-203/KI-207c 已记）。车一动，旧格就再没人碰得到 ⇒ 残留。

### 3.2 源头事实（本轮取证，可直接依赖）

- `src\pbs.cpp:1085 bool TryReserveRailTrack(TileIndex, Track, bool)` 是**唯一**写入收口；`src\pbs.cpp:1029 bool TryReserveRailTrackdir(...)` 第一件事就是调它（`:1032`）。全工程调用点仅 `train_cmd.cpp`(18) / `pbs.cpp`(5) / `yapf_rail.cpp`(5) / `pbs.h`(2) ⇒ **记账只需在一处插桩即可 100% 覆盖**。
- `src\pbs.cpp:1034-1041` 注释自陈：挂接寻路按 Track 级订，后续 Trackdir 级订同一位会 `return false`，即使该位**仍是本车**的 ⇒ ①调用方把该格从路径里丢掉（永不释放 → 孤儿）②PBS 信号不变绿（车被自己订的格挡住）。**这是一个已知、已写进注释、但只做了局部补丁的语义裂缝。**

### 3.3 收敛方案（修正版：S-1 / S-2 作废，因为它们早已存在）

| 编号 | 内容 | 状态 |
|---|---|---|
| **S-0 用起来** | 把已有账本接进现场诊断：对关注格直接取 `R3RResvLedgerOwner()` + purpose + `booked` 距今多少 tick；或按 `pbs.h:31-34` 往游戏目录丢 `r3r_resv_watch.txt`（一行一个 `x y`，**免重编**） | **代码已有，只差使用** |
| ~~S-1 记账~~ | 上一版提议在 `TryReserveRailTrack` 内插桩建表 `key=(tile<<3)\|track → {veh,tag,tick}` | **作废**：账本已存在，且喂入点在地图 setter 层，**写和释放都覆盖**，比只管写的业务收口更全 |
| ~~S-2 不变量~~ | 上一版提议新建 `R3RReservationInvariant()` 打 `RESV-ORPHAN` | **作废/待核**：`R3RResvLedgerForEachBit()`（第 255 轮 P1）正是为「遍历账本而非遍历全图」而加；需核实现有审计行是否已含 `owner/purpose/age` 三栏，缺则补字段，不另建一套 |
| **S-3 语义统一** | `TryReserveRailTrackdir` 在「该位已由**本车** Track 级订下」时返回 `true` 并补设绿灯，非本车仍拒绝 | ⚠ 改行为，需拍板 |
| **S-4 定向源单一** | `rp()` 内 track 只允许两种来源：当前格 `TrackdirToTrack(cur_td)`、进入格 `TrackdirToTrack(nd)`（KI-346 已做）；平台段 `AxisToTrack(axis)` 改为由 `f3.exitdir` 的 trackdir 取 track；删净 `ReverseTrackdir` 推导 | ⚠ 改行为，需拍板 |

### 3.4 落地顺序与判据

1. **S-0 立刻可做、零代码风险**：拿到 259,233 的 `owner / purpose / age`，一步判定它是「有主的合法预留」还是「已无主的残留」——这一步就能终结本轮的三轮翻案。
2. 核实 S-2 现状（只读）：确认现有审计输出字段是否够用，不够只补字段，**不新建第二套账**。
3. S-3 / S-4 属行为变更，需玩家拍板后再动（会改变信号绿/路径丢弃行为，必须单独复测）。

**结论句（修正）**：账本不是要「引入」，是**早已引入而我上一轮没查**。本轮正确的动作是**把已有账本用起来**（S-0），不是再建一本。真正还需动手的只剩 S-3（Track/Trackdir 语义裂缝，`pbs.cpp:1034-1041` 自陈）与 S-4（`rp()` 定向源不唯一）。

### 3.5 账本的收益与代价（玩家 2026-10-23 两次追问；**注意：账本早已存在**，本节是它的能力/边界说明，不是待引入方案）

#### 3.5.0 账本什么时候写、什么时候擦（玩家 2026-10-23 追问「哪些情况可以写记录，哪些能擦记录」）

**写入的唯一入口**是 `R3RResvWatchLog()`（`pbs.cpp:738`），它第一件事就是 `R3RResvLedgerNote()`（`:350`），然后才走探针日志。喂入点是**六个地图 setter**（清单见 `src\r3r_resv_probes.h:17-19`），所以写/擦都是由「地图上那一位的值真的变了」驱动，不是由业务意图驱动。

| 格子类型 | setter | 记账 kind | 文件行 |
|---|---|---|---|
| 普通轨道 | `SetRailReservationTrackBits` | `SET-RAIL` / `CHG-RAIL` / `CLEAR-RAIL` | `rail_map.h:252` |
| 站台 / 路点（整格位） | `SetRailStationReservation` | `SET-STN` / `CLEAR-STN` | `station_map.h:599` |
| 车库 | `SetDepotReservation` | `SET-DEPOT` / `CLEAR-DEPOT` | `rail_map.h:315` |
| 隧道 | `SetTunnelReservation` | `SET-TUNNEL` / `CLEAR-TUNNEL` | `tunnel_map.h:92` |
| 桥头 | `SetBridgeReservationTrackBits` | `SET-BRIDGE` / `CHG-BRIDGE` / `CLEAR-BRIDGE` | `bridge_map.h:419` |
| 道口 | `SetCrossingReservation` | `SET-XING` / `CLEAR-XING` | `road_map.h:494` |

写进去要同时满足三条：
1. **值真的变了**：每个 setter 都先比旧值（`if (HasXxx != b)` / `if (r3r_old != b)`），重复设同值不记。
2. **有户主**：`R3RResvLedgerActorHead()`（`:194`）取当前 actor 的链头做 owner；没有 actor（读档、引擎内部写）⇒ `R3R_RESV_NO_OWNER`，**直接不记**。
3. **多轨格只记第一轨**（`CHG-*` 只报 `FindFirstTrack`）⇒ 刻意「少记不多记」，漏记的后果只是释放晚一点，绝不会误放别人的位（`:117-120`）。

purpose 由当前 phase 决定（`:241`）：phase ∈ {`consist`, `clear-consist`, `waiter-platform`} ⇒ `CONSIST`（车身压着的位），其余 ⇒ `PATH`（路径位）。同一位被别的户主重订时**先删旧行再归新户**。

**擦除有两个层面**：

| 层面 | 触发 | 擦什么 |
|---|---|---|
| A. 地图动作自动擦 | 六个 setter 的值变回空（`CLEAR-*`） | 只擦该位那一行 + 反向索引；原本无行时只计数（`RESV-LEDGER-UNOWNED`），不动任何东西 |
| B. 按户整批擦 `R3RResvLedgerForget(owner, why)` | 见下表五个时刻 | 见 mask |

| why | 调用点 | mask（`R3RResvLedgerForgetMask` `:267`） |
|---|---|---|
| `"destroy"` | `vehicle.cpp:1252` 车辆销毁 | ALL（PATH+CONSIST） |
| `"decouple"` | `train_cmd.cpp:9145/9146` 解挂两半 | ALL |
| `"couple"` | `train_cmd.cpp:11036`（挂前清目标）、`:12138`（挂后清合并链） | **只 PATH**，车身下的位保留（否则每 tick 重订，KI-362） |
| `"free-path"` | `train_cmd.cpp:14665` 铺路前重算 | **只 PATH** |
| `"resync"` | `train_cmd.cpp:17781`，用 `VehicleID::Invalid()` 触发 | 不擦位，只把地图上已经不存在的行删掉（对账） |

未知 reason ⇒ 保守全擦（宁多勿漏）。`resync` 借"无户主 forget"这条路走，是为了不动 `pbs.h`（增量构建合规，`:619-621`）。

**擦不动 / 不该擦的三种情况**：①读档带进来的位没有户主，永远不被按户擦到，只能等地图动作自己变空；②`resync` 对 `R3R_RESV_NO_OWNER` 的行直接 `continue`（`:919`），不顺手删；③带校验的释放（`R3RReleaseReservationLedgerChecked` / `R3RReleaseReservationSafe`）会先问地图"这格还是不是这一位"，不是就只删行、不改地图（Release law）。

#### 3.5.0b 行为视角汇总（玩家 2026-10-23 追问「说行为，不要说函数」）

**写入 = 「某块地被某列车占上了」这一个事件。** 凡是能让列车占地的玩家可见行为都写，写入本身不需要玩家知道：

**【2026-10-23 玩家纠正后改写】行为与「地的种类」是两个维度，不能混在一列。** 玩家原话：「列车到路点又是什么行为呢，列车往前走，预留穿过路点而不是在路点停下啊」——正确。路点不是行为、也不是停靠点；它只是**路径上的一格**，与普通轨道同属"铺路"这一个行为，区别仅在**占法**（路点整格 vs 普通轨道某一条轨）。

**维度一：行为（只有 7 种，与地类无关）**

| 行为 | 记成 | 覆盖哪些地 |
|---|---|---|
| ① 铺路：开车/继续前进/岔口选路/换端后重找路 | 路径位 | 路线穿过的**所有**地类（普通轨、路点、站台、车库、隧道、桥头、道口） |
| ② 停：车身压着格子不动（等挂接的车底、停靠中的车） | 车身位 | 同上，看车压在哪 |
| ③ 挂车：机车接近等待车底（也是①铺路的特例） | 路径位 | 接近路径穿过的地 |
| ④ 解挂 | 整户擦 | — |
| ⑤ 卖车/销毁 | 整户擦 | — |
| ⑥ 放弃/重算路线、改单跳站、被堵重试 | 擦路径位 | — |
| ⑦ 拆地/改建 | 该位随地图消失 | — |

**维度二：地的种类（决定这一位怎么记，不决定行为）**

| 地类 | 占法 | 备注 |
|---|---|---|
| 普通轨道 | 只记一条轨 | 多轨格故意少记 |
| **路点** | **整格** | 引擎里路点复用车站 tile 类型 ⇒ 走整格预留；**不是**因为列车在此停靠 |
| 站台 | 整格（整段算一格） | 列车可停靠，也可只是穿过 |
| 车库 | 整格 | |
| 隧道 / 桥头 / 道口 | 整格 | 归属无法按车反查，只能报"是否被保护" |

**所以「列车开过路点」与「列车开过普通轨道」是同一个行为**，预留就是穿过去、不停车；我之前把它单列成"到达/经过路点"属措辞错误，已删。

**唯一会在路点格产生「车身位」的情形**：车真的停在那格上（等挂接的 WAIT_COUPLE 车底恰好站在路点格，或订单终点就是该路点且已停稳）——此时是②停，不是①铺路。

**为什么路点仍值得单独警惕（历史真因，与"停不停"无关）**：路点在引擎里 `TileType::Station + HasStationRail` 但不是 `IsRailStationTile`，于是"放整个站台预留"那条分支被 `IsRailStationTile` 守卫**定义上排除路点** ⇒ 路点整格位最容易被漏放，这就是历史上「只剩路点那一格」幽灵预留家族的来源（见 KI-207c 及后续轮次）。

#### 3.5.0c 为什么预留会"穿过路点、也穿过路点后面的信号灯"（玩家 2026-10-23 追问，已代码实证）

**玩家问题原话**：「列车从 A 经由路点 B 前往 C，明明自己离路点很远，但是预留依旧穿过了路点和它后面的一个信号灯呢，而不是在路点后面的信号灯那里截止预留，像出站信号机一样呢」。

**结论：预订终点不是"订单目的地"，而是"订单目的地里第一个能安全停车的位置"。路点不是安全停车位，所以引擎会把终点往后推到下一张订单（C），一次性把整条 A→C 铺下来 —— 路点与其后的信号灯都只是被穿过的格。**

**证据链（三处，逐行）**：

1. `train_cmd.cpp:16265-16303` —— 拿到 `res_dest`（= 第一个订单的目的地，即路点 B）后，**立刻进安全回推循环**：
   ```cpp
   while (!IsSafeWaitingPosition(consist, res_dest.tile, res_dest.trackdir, true, _settings_game.pf.forbid_90_deg)) {
       /* Extend reservation until we have found a safe position. */
       ...
       /* Get next order with destination. */
       if (orders.SwitchToNextOrder(true)) {
           DoTrainPathfind(consist, next_tile, exitdir, reachable, path_found, true, &cur_dest, nullptr);
           if (cur_dest.tile != INVALID_TILE) { res_dest = cur_dest; ... continue; }
       }
       ...
   }
   ```
   注意扩展方式：**不是"往前挪一格"，而是 `SwitchToNextOrder(true)` 换下一张订单 + `DoTrainPathfind` 重新算到新目的地**。⇒ 只要路点不安全，预留就会整段铺到 C（C 也不安全则继续吃订单到 D…）。

2. `pbs.cpp:3003-3060` `IsSafeWaitingPosition()` —— 判定哪些格算"能安全停车"：
   - `IsRailDepotTile` → true（`3005`）
   - **`HasBlockSignalOnTrackdir(tile, trackdir)` → true（`3008`）：非 PBS 信号灯格本身就算安全位**
   - 隧道桥信号入口 → true（`3010`）
   - 否则看**下一格**：下一格是顺向 PBS 信号灯 → 条件性 true（`3031-3045`；`GetSignalAlwaysReserveThrough` 为真时反而返回 **false** = 强制穿过）；下一格是**反向 one-way PBS 信号** → `include_line_end`（`3048-3051`）
   - 线路末端且 `include_line_end` → true（`3018-3021`）
   - 以上都不满足 → **`return false`（`3059`）**
   ⇒ **路点格：不是车库、格上没有信号灯、也不是线路末端 ⇒ 命中 `3059` 的 false。** 这就是"路点不能作为预留终点"的机制级原因。

3. `train_cmd.cpp:16248-16260` `check_destination_seen` —— 只有 `OT_GOTO_WAYPOINT` 且路点带 `OrderWaypointFlag::Reverse` 时才置 `CTTLASF_REVERSE_FOUND`（要停下掉头）；普通路点不停车。与上一轮 §3.5.0b「路点不是停靠点」逐字一致。

**"为什么像出站信号机那样在信号灯前截止"其实也是同一条规则**：出站信号机场景能截止，不是因为"信号灯是边界"，而是因为**信号灯格本身被 `3008` 判为安全位**（或信号灯前那一格被 `3048` 判为安全位）。信号灯是"合法停车点"，路点不是 —— 差别在 safety 判定，不在"信号灯会拦住预留"。

**"为什么离路点很远就已经被预留"**：预留是**一次性声明整条路径**（A 出发那一 tick，YAPF 算到 B→不安全→换订单算到 C→整条 A→C 逐格 `TryReserveRailTrack`），不是"跟着车头一格一格推进"。所以车还在 A 附近时，B 与其后信号灯早就被订下了。

**什么情况下路点确实能截住预留**：路点格**紧邻的下一格**正好是顺向 PBS 信号灯（`3031`）或反向 one-way PBS 信号（`3048`）时，`IsSafeWaitingPosition(路点格)` 返回 true ⇒ 预留停在路点。这也解释了玩家在不同布局下看到的两种表现。

**什么时候信号灯反而不被穿过**：`GetSignalAlwaysReserveThrough()` 为真、或 tracerestrict 程序输出 `TRPRF_RESERVE_THROUGH`（`3034-3043`）时，`IsSafeWaitingPosition` 对该信号灯返回 false ⇒ 预留**必须**穿过它继续往前。这两者都是玩家可配的"预留穿过"语义。

#### 3.5.0d 「把路点直接判为安全停车位」方案评估（玩家 2026-10-23 提议，本轮只评估、未改码）

**玩家提议原话**：「对于符合条件的列车（也就是今天疯狂打地鼠的那个预留截断问题），我们直接把路点判为安全停车位」。

**先给结论：方向是对的，引擎其实早就把路点当"可能的预订目标"了；但不能去改 `IsSafeWaitingPosition` 的全局返回值，必须做成"带参数、只在预留终点这条链上打开"。**

**一、玩家的直觉有代码依据（这是提议最有力的支撑）**

`train_cmd.cpp:14933-14941`（`TryPathReserve` 的走链循环 `while (ft.Follow(tile, cur_td))`）原文：

```cpp
/* Station, depot or waypoint are a possible target. */
bool target_seen = ft.is_station || (IsTileType(ft.new_tile, TileType::Railway) && !IsPlainRail(ft.new_tile));
if (target_seen || KillFirstBit(ft.new_td_bits) != TRACKDIR_BIT_NONE) {
    /* Choice found or possible target encountered.
     * On finding a possible target, we need to stop and let the pathfinder handle the
     * remaining path. ... */
```

⇒ **路点在引擎里本来就是"预订目标候选"**（和站台、车库并列），走链遇到它就会停下、交给 YAPF 去算。它不是被引擎无视，而是算出来以后在 `train_cmd.cpp:16265` 的安全回推那一步被否掉（因为 `IsSafeWaitingPosition(路点格)=false`）。所以"把路点判为安全位"不是硬塞语义，是把引擎已经承认的目标身份**兑现**。

**二、但 `IsSafeWaitingPosition` 是全局谓词，共 6 个调用点，逐点影响不同**

| # | 调用点 | 用途 | 路点恒 true 的后果 | 结论 |
|---|---|---|---|---|
| 1 | `train_cmd.cpp:16265` | `ChooseTrainTrack` 的安全回推（决定预留终点） | 预留停在路点，不再 `SwitchToNextOrder` 吃下一张订单 | **要打开**（这正是诉求） |
| 2 | `train_cmd.cpp:15039` | `TryPathReserve` 走链遇到 safe 位就当场 `TryReserveRailTrackdir` 并返回 `okay=true` | 走链进路点即收口 | **要打开**（否则与 #1 不一致） |
| 3 | `pbs.cpp:2005` | `FollowTrainReservation` 的 `ftoti.res.okay` | 沿预留反查时认为预留健康 | **要打开**（否则反查说不 okay ⇒ 重算 ⇒ 打地鼠照旧） |
| 4 | `yapf_costrail.hpp:733` | 在**路点平台**上找第一格"安全等待位"（`Break` 条件） | 平台第一格就 break，原本找的是平台末端/灯前 | **保持 false**（改变路点占用代价评估） |
| 5 | `yapf_costrail.hpp:739-741` | 路点被占用时的 `extra_cost += rail_lastred_penalty` 判据之一 | 少了一个惩罚项，列车可能不再避开被占路点平台 | **保持 false** |
| 6 | `yapf_destrail.hpp:177` | YAPF **目的地检测** `PfDetectDestination`（"找任意安全位"类寻路） | 任何这类寻路一进路点就判定"已到达" | **保持 false**（会截断去站台/车库的寻路） |
| 7 | `rail_cmd.cpp:700` | `at_safe_waiting_position`（拆轨/信号操作保护判定） | 路点格被判"有车在等"的机会变多，改变拆地保护 | **保持 false**（无关且危险） |

**三、实施关键：三处必须同时打开，缺一处就打地鼠照旧**

只改 #1 ⇒ 预留确实停在路点，但 #3 反查时 `IsSafeWaitingPosition(路点)` 仍为 false ⇒ `okay=false` ⇒ 下游认为"预留不完整"⇒ 重新 `ChooseTrainTrack` ⇒ 撤销重订 ⇒ **打地鼠一模一样**。#2 同理（走链会越过路点继续找下一个 safe 位）。

**四、推荐形态**

给 `IsSafeWaitingPosition()` 加尾参 `bool treat_waypoint_as_safe = false`（`pbs.h:293` 声明同步），函数开头：

```cpp
if (treat_waypoint_as_safe && IsRailWaypointTile(tile)) return true;
```

只让 #1/#2/#3 传 `true`，#4~#7 沿用默认 `false`。这样 YAPF 寻路代价、目的地检测、拆轨保护逐字不变。

**五、必须先确认的前置风险（否则会引爆老家族）**

预留终点频繁落在路点上 ⇒ 路点整格位的**释放回路**被高频踩到。历史上「只剩路点那一格、拆掉路点就消失」的幽灵预留（KI-207c 家族）正是这个缺口。第 125/126/127 轮已补：`GetTrainForWholeTileReservation()`、`R3RReleaseChainReservations()` 的站台/路点分支（`R3RResvBit.whole_tile`）、`SetRailStationReservation(tile,false)` 释放路径。**实施前须确认这三条仍在且覆盖"预留终点在路点"这个新形态**，否则本方案会显著放大 KI-207c。

**六、另一个真风险：列车会停在路点上等**

预留终点在路点，意味着被堵时列车真的停在路点格。若路点压在咽喉道岔上会挡路——但这与"停在信号灯前"同类，属设计内；且玩家场景里路点后紧跟信号灯，停路点 ≈ 停灯前，几乎等价。

**七、复测判据（若后续实施）**

1. 同一场景：`RP cur=` / `RESV-WATCH` 应止于路点，不再出现路点后信号灯及 C 方向的长串预留；
2. 打地鼠签名消失：同一路线不再出现「订下 → `CLEAR-*` → 下一 tick 重订」的循环（对照第 127 轮 `RESV-NOBOOK` 口径）；
3. 负判据：普通轨道格、裸轨段仍 `IsSafeWaitingPosition=false`，行为逐字不变；
4. 负判据：以站台/车库为终点的寻路不被路点截断（#6 必须仍为 false）；
5. 负判据：路点被占时列车仍会绕开该平台（#4/#5 的惩罚项仍生效）；
6. 幽灵预留不放大：`RESV-AUDIT-STRAY` / `RESV-REAP` 命中数不因本改动上升。

#### 3.5.0e 实施记录（2026-10-23，玩家拍板「按推荐的来」后落地）

**玩家原话**：「可以按照你推荐的来，如果真有效果，一次全量重编可比打一天地鼠强多了」。

**改动清单（4 处，2 个 .h/.cpp + 1 个 .cpp）**

| 文件 | 位置 | 改动 |
|---|---|---|
| `src\pbs.h` | 声明 | 尾参 `bool treat_waypoint_as_safe = false` |
| `src\pbs.cpp` | `IsSafeWaitingPosition()` | 实现加同名尾参；在 `IsRailDepotTile` 之后、信号灯分支之前加 `if (treat_waypoint_as_safe && IsRailWaypointTile(tile)) return true;` |
| `src\pbs.cpp` | `FollowTrainReservation()` 的 `ftoti.res.okay` 赋值 | 传 `true`（第 3 张表 #3） |
| `src\train_cmd.cpp` | `ChooseTrainTrack()` 安全回推 `while` | 传 `true`（#1） |
| `src\train_cmd.cpp` | `TryPathReserve()` 走链 `IsSafeWaitingPosition` | 传 `true`（#2） |

其余 5 处调用点（`yapf_costrail.hpp` ×2、`yapf_destrail.hpp`、`rail_cmd.cpp`、以及 `pbs.cpp` 内部其它路径）**一律沿用默认 `false`**，逐字不变。

**为什么判据放在 `IsRailDepotTile` 之后、信号灯之前**：路点格上可能同时存在信号灯（路点+信号复合位形）。放在最前会把"路点上有信号灯时按信号灯规则判定"的既有行为一并覆盖掉；放在 depot 之后只处理"纯路点格"，与 §3.5.0c 里 `pbs.cpp:3059` 返回 false 的那一档精确对应。

**释放回路取证（本轮新增，结论=完整，不需要额外补丁）**

链路逐段核实：

1. 订：`TryReserveRailTrack()`（`pbs.cpp` 约 `1110-1138`）对 `TileType::Station` 用 `HasStationRail(tile) && !HasStationReservation(tile)` 分发到 `SetRailStationReservation(tile, true)` ⇒ **路点会订整格位**。
2. 查：`GetReservedTrackbits(t)`（`pbs.cpp:941`）对 `TileType::Station && HasStationRail` 返回 `GetStationReservationTrackBits(t)`；后者（`station_map.h:609`）= `HasStationReservation(t) ? GetRailStationTrackBits(t) : TRACK_BIT_NONE` ⇒ 路点整格位被订时返回**该格全部轨道位**（不是 NONE，不会让走链提前 break）。
3. 放：`FreeTrainTrackReservation()` 的走链（`train_cmd.cpp:14719-14767`）用上面这个 `TrackBits` 求 `td`，随后 `ClearPathReservation(consist, tile, td)`（`train_cmd.cpp:14553`）；路点**不是** `IsRailStationTile` ⇒ 跳过 `14610` 的站台整条分支 ⇒ 落到 `14642` 的 `else` ⇒ `UnreserveRailTrack(tile, TrackdirToTrack(track_dir))`。
4. `UnreserveRailTrack()`（`pbs.cpp` 约 `1271-1297`）的 `TileType::Station` 分支用 `HasStationRail`，**不是** `IsRailStationTile` ⇒ `SetRailStationReservation(tile, false)` 生效。

⇒ **订侧、查侧、放侧三处对路点都是对称的**。§3.5.0d 第五点担心的「路点整格位释放缺口」实际上只存在于 `SetRailStationPlatformReservation()` 的"整条到发线一次性释放"分支（`station_map.h` 该函数带 `@pre IsRailStationTile`，路点永远走不到），而路点本来就不该走那条；逐格路径是完整的。KI-207c 家族的历史成因是 R3R 自己的链编辑清扫只扫 plain rail（第 126 轮已补 `whole_tile` 分支），不是引擎逐格路径。

**因此本轮不需要动释放代码**，复测时按 §3.5.0d 第七点 6 条判据盯即可，其中第 6 条（`RESV-AUDIT-STRAY`/`RESV-REAP` 不上升）是这条结论的验证。

**状态**：已实现，构建中。

**擦除分三层，玩家可触发的只有第二层：**

| 层 | 玩家做了什么事 | 擦掉什么 |
|---|---|---|
| 自动（跟着地走） | 列车开过那一格、到站停下、改单/跳站/放弃路线、被堵后重试 | 只擦那一位；站台整段被清空也走这条 |
| 自动（地没了） | 拆掉/改建轨道、站台、车库、道口 | 整格重建**绕过钩子**，靠后台对账补删 |
| 整列车（专门动作） | **卖车/销毁** | 全擦（路径位+车身位） |
| | **解挂** | 两半各自全擦 |
| | **挂车** | 只擦路径位，车身位保留 |
| | **要求重新规划路线** | 只擦路径位 |
| 后台（不用玩家做） | 定期对账，拿账本比地图 | 删掉地图上已不存在的行，不碰地图 |

**疑点（2026-10-23 新发现，需后续核实）**：`R3RResvLedgerForgetMask()`（`pbs.cpp:267-272`）认得四个"只擦路径"的理由 `free-path` / `couple` / `flip` / `depot-edit`，但全仓 `R3RResvLedgerForget` 的调用点只有 `destroy`（`vehicle.cpp:1252`）、`decouple`（`train_cmd.cpp:9145/9146`）、`couple`（`:11036`/`:12138`）、`free-path`（`:14665`）、`resync`（`:17781`，ownerless）——**没有任何调用点传 `flip` 或 `depot-edit`**。⇒ 「换端」「车库内拖动编组」在设计上被列为可整户擦的时刻，当前**并未实际接入**；它们之后只能靠"地变空"逐条自动擦。这是否为遗漏需查证：若换端/车库编辑后出现"账本行残留、但地图位已被重写"，则该缺口即真因，应补调用点。


#### 3.5.1 为什么会想到账本——探针与账本的覆盖率强度不同

- **探针**：我猜「这里可能有问题」，就在这埋一行 ⇒ 覆盖率取决于**我的猜测**。猜漏了就是静默盲区，而盲区无法自证存在。玩家「对探针没信心」是对这种覆盖方式的正确判断。
- **账本**：预留写入在代码结构上**只有一个入口**（`src\pbs.cpp:1085 TryReserveRailTrack`，`TryReserveRailTrackdir` 于 `:1029` 仅转调）⇒ 覆盖率取决于**代码结构**，不取决于我是否想到。这是本会话目前唯一能拿到「确定性覆盖」的位置。

#### 3.5.2 零变化的部分（先划出安全边界）

| 项目 | 结论 |
|---|---|
| 地图上的预留位 | **仍是唯一权威状态**（`_m[t].m2` bit 8-11）。账本只是旁挂影子表，不参与任何判定 |
| 存档格式 / 版本 | 不变（账本可设为 NOSAVE；即便入档也是新增稀疏块） |
| 寻路代价、通行判定、信号逻辑 | 不变（S-1 只写表不读表） |
| 已有行为 | 一行不改；账本写错 = 诊断信息错，**不会让车走错、不会崩** |

⇒ 这是「可回退、可无视」的改造。S-1 单独落地时，即使账本是垃圾，游戏跑起来和现在逐字一致。

#### 3.5.3 真正会变化的部分

1. **「谁订的」从推断变查询。** 现在只能靠 `GetTrainForReservation()`（顺着预留走到一端点名站在那里的人）和 `TrainReservationPassesThroughTile()`（只表示预留**到达**该格），二者都不证明所有权（`yapf_rail.cpp:236-258` 注释自陈）。账本直接给出 `veh + 写入点 tag + tick`，一次定位。
2. **语义裂缝被强制暴露。** `pbs.cpp:1034-1041` 已自陈：Track 级订下后，Trackdir 级订同一位会 `return false`（即使仍是本车）。一旦记账，这两条路写的是**同一个 key**却给出不同结论 ⇒ 账本与返回值自相矛盾，矛盾点会被直接打出来。这是好事（把已知裂缝变成可测），也是**逼出 S-3**。
3. **孤儿可精确判定。** 判据从「距最近列车 >24 格」这类启发式，升级为「账本有位 ∧ 无任何活车实际路线经过该格该轨」⇒ 越界铺轨、残留、自己挡自己三类合并为一个量。
4. **释放口不完整会被自动审计。** 见 3.5.4。

#### 3.5.4 账本的三种失败模式（必须事先接受，否则账本会比探针更误导）

| 编号 | 失败模式 | 具体漏点 | 检测手段 |
|---|---|---|---|
| **L-1** | 漏记**释放** | 释放不经过 `TryReserveRailTrack`：`UnreserveRailTrack`、`ClearPathReservation`、站台/路点整格位的 `SetRailStationReservation(false)`、拆轨改建 | 账本有位 + 地图已无位 ⇒ `RESV-LEDGER-STALE` |
| **L-2** | 漏记**非收口写入** | 任何绕过 `TryReserveRailTrack` 直接写预留位的地方（含引擎的 `SetTrackReservation` 直调） | 地图有位 + 账本无记录 ⇒ `RESV-LEDGER-MISS` |
| **L-3** | **读档断档** | 地图位从存档来，账本（NOSAVE）为空 ⇒ 读档后全部预留都成「无账」 | 读档后一次性统计 MISS 数，标记为 `native`（未知来源）而非当成孤儿 |

**关键论点**：这三种失败**不需要提前排除**，因为 `STALE` / `MISS` 本身就是答案——它们精确指出「收口不完整的位置」。探针坏掉是静默的，账本坏掉是**自报**的。这正好对上「对探针没信心」：账本能证明自己何时不可信。

#### 3.5.5 能替代哪些探针、替不掉哪些

- **可退役**：`RESV-AUDIT*` / `RESV-REAP` / `RESV-WATCH` / `RESV-GHOST` 的归属部分、`FSCP-RES` 里 `owner=` / `othru=` 这类猜测性字段。
- **替不掉（仍需探针）**：几何问题（位置/朝向/间距/包围盒）、订单索引推进、段身份——账本管的是「预留归谁」，不是「列车长什么样」。

#### 3.5.6 行为变更与开关（务必分开看）

| 阶段 | 行为变更 | 建议默认 |
|---|---|---|
| S-1 记账 | **无** | 直接开启 |
| S-2 不变量审计 | **无**（只读比对 + 打日志） | 直接开启 |
| S-2' 自动 reap（主动释放地图位） | **有**（可能放掉合法预留） | **默认只报不杀**，需显式开关 + 人工确认 |
| S-3 Trackdir 语义统一 | **有**（改变信号绿/路径丢弃） | 单独开关、单独复测，不与 S-1 同批 |
| S-4 `rp()` 定向源单一化 | **有**（改变挂接寻路订轨） | 单独复测，需与 KI-346 判据合跑 |

#### 3.5.7 成本量级（可预估）

- 内存：稳态预留位估计数百至数千条，每条约 48~64 B ⇒ **数百 KB 量级**。
- 算力：每次**成功**预留一次哈希插入/覆盖；`ReserveTrackUnderConsist` 每 tick 会调（KI-203）⇒ Debug 构建下可感知，建议 `if (R3RDbgOn())` 包裹。
- 风险隔离：写表失败不影响游戏；表本身可一键清空重来。

#### 3.5.8 结论句

账本带来的是**可追溯性**，不是**自动修复**。它把「打地鼠」变成「打台账」——每只地鼠自带姓名与出处。地鼠本身要靠 S-3 / S-4 才算消灭。但在此之前，账本能让我们第一次回答「259,233 那格是谁订的」而不是再猜第四遍。

## 四、疑点

- **Q-284-1**：`CPL-WAIT-NOBOOK` 0 命中，是否等价于「D-6b 门禁没生效」？期望值：若 21 真处于"越过等待区路点且配对未落定"，应约 500 tick 一条。实测：0。存疑点：日志只有 1 855 行（约 3 000+ tick？），且 21 后来**已**配对成功，因此"没进门禁"是合理结果而非失效证据。还需 `:1266-1790` 区间核对 `CPL-PAIR` 首次出现的 tick / 行号。
- **Q-284-2**：`CPL-PAIR` 已建立却仍 `COUPLE-FAIL`，两者不矛盾吗？期望值：配对落定后主动方应能寻到目标并 `COUPLE-OK`。实测：13 格距离 + `TRP ok=0`。
- **Q-284-3**：`TPR-WALK ... td=8 okay=0 exit=2 new=260,233 reach=1` 的语义未查代码（`exit=2` / `reach=1` 分别代表什么出口）。`okay=0` 与 `reach=1` 同时出现需要解释。
- **Q-284-4**：21 停在 253,233 而非继续走向 265,234。是**寻路真的无路**（`okay=0` 表路径失败），还是**另有闸门**（`COUPLE-WAIT-OFFDEST` 说"未到目的地"，说明它不是被"已到站"的早退挡住）？
- **Q-284-5**：第 278 轮 D-4 承诺的 `WBAR-RELEASE ... seen= mf= pair=` 是否在 `:1266` 前后出现？若出现，`pair=` 是谁？
- **Q-284-6**：`COUPLE-WAIT-OFFDEST` 的判据（`R3RCoupleOrderDestinationReached()`，第 109 轮 KI-193）是"按 depot/station id 判到点"，而 `destStn=12` 与 21 所在格 253,233 的站号是否不同？若 21 所在格根本不是 12 号站，则"未到目的地"是正确判定，问题变成"为什么走不过去"。
- **Q-284-7**：`TPR-WALK exit=2` 与 `new=(260,233)`（x+1）的对应关系需按 `DiagDirection` 实际枚举核对（`DIAGDIR_NE/SE/SW/NW` 的顺序与 `TileAddByDiagDir` 的符号约定），本轮**暂不给枚举名**，以免把结论钉错。
- **Q-284-8**：`state=0` 时 `wp=17` 仍被打印，说明 `wait_wp` 有值 ⇒ "无等待区路点"这一支被排除；结合 `tile=253,233` / `mf=259,233` / 路点 254,233，21 **确实已越过**等待区路点。若不是 D-6b 的配对条件挡住，D-6b 本该生效。这条坐实了 G-284-A 的前提。

## 三·补、G-284-A 的坐实链（本轮新增证据）

1. 截断已放开（`CPL-WAITCUT state=0`，§2.5）⇒ 不是"截断把路拦在路点"。
2. 配对锁在（`CPL-PAIR tick=3066..3073`，§2.6）。
3. 无预留（`TRP ok=0 res=0 mstuck=0`，§2.7）⇒ `FollowTrainReservation` 无目标 ⇒ `COUPLE-WAIT-OFFDEST` / `COUPLE-FAIL`。
4. 下一格有轨道（`reach=1`）且不是"被别的列车挡住"（无 `TPR-BLOCKED`）⇒ 失败落在 `ChooseTrainTrack` 内部（§2.8）。

⇒ **缺口定位**：第 283 轮 D-6b 的门禁条件是"配对为空 + 已越点"；W-6（`found=0` 抽预留兜底）也只看挂接寻路。本轮 21 **配对非空**、`dist=13`、`GOTO_COUPLE` 订单仍在，两个兜底都不覆盖，于是"每 tick 试图寻路 → `ChooseTrainTrack` 失败 → 无预留 → 挂接扫描无候选"的自锁无人打断。

## 四、猜测根因（待验证）

- **G-284-D（本轮成立，实测实证）**：自锁的**直接机制**=「机车自己脚下的 `TRACK_X` 预留」与「挂接寻路要从同一格订的 `TRACK_RIGHT`」在道岔格 259,233（`tbits=0x25`）内交叉，`TryReserveTrack` 的 `TracksOverlap` 判拒，而 FSCP 分支 B（`yapf_rail.cpp:198`）没有第 110 轮给分支 A（`:159`）加的"自身链豁免" ⇒ `CPL-SAFE-FAIL` ⇒ 无路径 ⇒ 无预留 ⇒ `COUPLE-WAIT-OFFDEST`。证据链见 §2.9（代码锚点 + `res=0x1 owner=21 isGtr=1 want=TRACK_RIGHT`）。**上游成因**（谁把 `TRACK_X` 留在那里）：21 停稳后不再进移动分支（第 242 轮 KI-348 的结构性结论），旧预留既不推进也不释放；挂接寻路（`DoTrainCouplePathfind`）是独立入口，**不先清本链旧预留**（§2.11）。修法⇒F-284-1。
- **G-284-B（证伪）**：原猜"`origin=259,233` 出发格本身不合法 / 被车底 24 的链占着"。实测 `TPR-WALK reach=1`（下一格 260,233 有 `TRACK_BIT_X`）+ 无 `TPR-BLOCKED` + 阻塞 `owner=21`（自身）⇒ 与车底 24 无关。
- **G-284-C（证伪）**：`COUPLE-FAIL` 的节流来自 `CPL-SKIP retryIn=8` / 探针边沿窗口，不是"奇数 tick 有一次成功路径"。
- **G-284-A（已收窄为背景条件，不再单独作为根因）**：`COUPLE-WAIT-OFFDEST` 与 `TRY-COUPLE` 共存说明**配对扫描不要求主动方到站**（KI-451 已确认 `R3RCoupleTargetAtOrderStation()` 只比目标格），于是「配对已锁定」与「主动方还没到 GOTO_COUPLE 目的地」可以同时成立。此时 `TrainCoupleHandler()` 每 tick 做几何命中（`CPL-PAIR dist=13` 大于命中阈值 ⇒ 失败），而**驱动列车靠近的那条路**（路线预留）因 `ok=0` 拿不到轨道 ⇒ 冻结。修法方向：`found=0` 时（KI-440 的 W-6）与 D-6b 都只在"配对被抽空"时兜底；需要一条「**已配对但距离远**」的兜底：不抽预留、不判 `STUCK`，或让 `GOTO_COUPLE` 的路线预留把目标格当作合法终点。
- **G-284-B（待验证）**：`TPR-WALK exit=2 okay=0` 表示 `FollowTrainReservation()` 内某个早退（如 `first_tile_okay=false` / 轨道被占 / PBS 信号反向），而 `origin=259,233` 与 `mf=259,233` 相同 ⇒ 出发格本身就不合法，可能是**车底 24 的链占着 259,233 之后的格**（`dist=13`）。
- **G-284-C（待验证，低概率）**：`COUPLE-FAIL` 每 2 tick（不是每 tick）说明中间还夹着一次成功路径（`ok=1`）或 D-6b/W-6 门禁在奇数 tick 命中。需核对 `TRP veh=21` 全序列的 `ok=` 分布。

## 五、下一步取证清单（按 3~5 组/轮配额）

1. `CPL-PAIR` 首次出现行号 + 之前的 `CPL-WAITCUT`/`WBAR-RELEASE` 上下文（`search_content "CPL-PAIR"`）。
2. `TRP veh=21` 全序列的 `ok=` 分布，确认是否每 tick 都失败。
3. `:700-1300` 区间 veh=21 / veh=24 的订单与速度演变（`LOCO-ORD`、`R3RDUMP-CHAIN`、`ORD-AFTER-COUPLE`）。
4. `COUPLE-WAIT-OFFDEST` 打印点代码 + `TPR-WALK` 的 `exit=` 语义。
5. `R3RCoupleOrderDestinationReached()` 与 `R3RIsCoupleTarget()` 的配对时机是否要求主动方到达。

## 六、构建自证（本轮尚未构建）

- 待定。

## 七、待复测判据（待定，取证后补）
