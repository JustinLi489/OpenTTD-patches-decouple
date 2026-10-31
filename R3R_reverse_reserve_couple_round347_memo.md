# 第 347 轮：路点换向后，GOTO_COUPLE 订单迟迟不铺预留（普通订单则立即铺）

## 一、玩家原话（2026-10-28）

> 这个改了接线之后很多都要慢慢搞，比如说，这个前往挂接，应该在自己换向的时候立刻尝试铺设预留，
> 也应该就是调用 trypathreserve，而不是换向之后走一段距离再调用 trypathreserve
>
> 额，我不知道我是否表述准确，我的观察是这样的：假如一个路点是一条东西走向铁轨的东端尽头，
> 一个列车在这个路点换向之后如果下一条命令是普通的非R3R命令，那么就会到路点格最东端，
> 转换movingfront，然后立即尝试铺设预留（也许就是调用 trypathreserve）。
> 但是如果路点命令下一条是前往挂接命令，它就会到路点格最东端，转换movingfront，
> 走到路点格最西端，再尝试预留（也许就是调用 trypathreserve）

## 二、期望行为（玩家口径）

换向（moving front 翻转）那一刻，**无论下一条订单是什么类型**，都应立即走一次预留尝试入口（`TryPathReserve`）。
`OT_GOTO_COUPLE` 不应比普通订单更晚开始铺预留。

## 三、现象（待日志核实）

| 下一条订单 | 观察 |
|---|---|
| 普通订单（GOTO_STATION / GOTO_WAYPOINT / …） | 到路点格最东端 → 翻 moving front → **立即** `TryPathReserve` |
| `OT_GOTO_COUPLE`（前往挂接） | 到路点格最东端 → 翻 moving front → **走到路点格最西端** → 才 `TryPathReserve` |

差异量级 ≈ 机车/列车长度在路点格内走过的距离（一步到数格、若干 tick）。

## 四、疑点登记

- **D-1**：换向后同一 tick 内，`ProcessOrders` 之后 `TryPathReserve` 的调用是否被订单类型守卫挡住？若被挡，"走到西端再铺"可能不是延迟调用，而是"每 tick 都调、但每次都失败，直到机车几何位置变了才成功"——两种机制现象相同但修法完全不同。
- **D-2**：若确为"调用被跳过的延迟"，需要分清是 `TryPathReserve` 内部早退（如 `cur_speed == 0` 门禁 / 挂接 stuck 重试门禁 / `R3RCoupleSearchSkipped` 节流），还是 `TrainLocoHandler`/`ProcessOrders` 层的分支差异（停驶分支 vs 移动分支）。
- **D-3**：换向那一刻 `consist->tile` / `GetMovingFront()` / `origin` 三者的取值，挂接寻路用哪一个当起点？若挂接路径起点仍取旧 moving front（换向前那端），本 tick 必然算不出路径 ⇒ 表现为"等走一段"。

## 五、猜测根因（待验证，先落盘）

**G-1（主假设）**：`TryPathReserve` 对 `OT_GOTO_COUPLE` 有"起步条件"，而该条件在换向瞬间不成立（例如要求 `cur_speed == 0` 的停驶重试门禁、或 `origin` 取的是链头格而换向后链头格尚未"到达"），于是每 tick 都调但都早退；机车在移动分支里继续按旧预留滑行，直到几何位置满足条件才成功铺路。

**G-2（次假设）**：换向后若"下一格已有本车预留"，`ProcessOrders` 那条捷径会直接放行而不调 `TryPathReserve`；挂接订单因 `r3r_couple_stuck_retry` / `r3r_couple_split_res` 豁免而不吃捷径，两者次序不同 ⇒ 普通订单走捷径（看起来"立即铺"），挂接订单绕远。

> 两假设的判别方法：看换向后若干 tick 内是否**每 tick 都有** `TRP veh=` / 预留尝试日志（有=G-1 的"调了但失败"；没有=G-2 的"根本没调"）。

## 六、待取证清单（按优先级，逐条落盘）

1. `R3R_debug.log` 中同一列车「换向行 → 下一条订单类型 → 首次 `TRP veh=` 行」的三元组，普通订单与挂接订单各取一例，记录 tick 差与走过的格。
2. `src\train_cmd.cpp` 中 `ReverseTrainDirection` 之后、`TryPathReserve` 调用点附近的订单类型判据。
3. `src\train.h` 的 `TryPathReserve` inline 包装与 `TryPathReserveResultFlags` 的入口早退条件，逐条对照 `OT_GOTO_COUPLE`。
4. 停驶分支（`j < adv_spd`）与移动分支是否都调 `TryPathReserve`。

## 七、状态

- 第 347 轮：**仅登记 + 取证，未改任何源码、未构建**。
- 取证结论见 §八。

## 八、取证结论（2026-10-28，日志 `D:\temp\codebuddy-dropped-files\...\R3R_debug.log`）

### 8.1 结论 1：换向当 tick **已经**调用了 `TryPathReserve`，普通订单与挂接订单**完全同构**（玩家描述的"延迟调用"在调用时机层面不成立）

**挂接订单（veh=57，order=16）**：

```
1307: LOCO-MOVE veh=57 tick=12460 tile=16,14 j=194 spd=5 track=01 dir=5 db=0 stuck=0 dtile=1932
1308: LINEND    veh=57 tile=16,14 x=15 sig=0 near=1 spd=5 brk=15 clamp=0 len=2 dir=5 db=0
1309: REVERSEDIR veh=57 tile=16,14 dir=5 order=16 spd=0 nv=3 rev=0 stuck=0 db=0
1313: REVERSEDONE db=1 mvfront=59
1317: R3R-RES-ONCE grant head=57 why=turn force_proceed=0 tile=16,14
1318: TPR-ZERO-REACH veh=57 mf=16,14 origin=16,14 td=0 okay=1 spd=0 real=1 order=16 look=0 turn=1
1319: TPR-WALK       veh=57 mf=16,14 origin=16,14 td=0 okay=1 exit=0 new=15,14 reach=9 spd=0 one=1 look=0
1320: TPR-EXIT       veh=57 state=choose pair=0 tile=16,14 spd=0 other=57 ... origin=16,14 okay=1
1322: CPL-ENTRY veh=57 tile=16,14 orderType=16 dontReserve=0
```

**普通订单（veh=54，order=2）对照**：

```
1848: LINEND    veh=54 tile=18,16 x=15 sig=0 near=1 spd=5 brk=15 clamp=0 len=2 dir=5 db=0
1849: REVERSEDIR veh=54 tile=18,16 dir=5 order=2 spd=0 nv=3 rev=0 stuck=0 db=0
1853: REVERSEDONE db=1 mvfront=56
1857: R3R-RES-ONCE grant head=54 why=turn force_proceed=0 tile=18,16
1858: TPR-ZERO-REACH veh=54 mf=18,16 origin=18,16 td=0 okay=1 spd=0 real=2 order=2 look=0 turn=1
1859: TPR-WALK       veh=54 mf=18,16 origin=18,16 td=0 okay=1 exit=0 new=17,16 reach=1 spd=0 one=1 look=0
1860: TPR-EXIT       veh=54 state=choose pair=0 tile=18,16 spd=0 other=54 ...
```

两者都是 `REVERSEDIR → REVERSEDONE → R3R-RES-ONCE grant why=turn → TPR-ZERO-REACH turn=1`，**同一 tick 内**。
`turn=1` 就是换向触发的一次性预留授权（`R3R-RES-ONCE grant ... why=turn`）。

### 8.2 结论 2：挂接订单在这次预留里**失败了**，失败的正是 KI-516

```
1341: CPL-BACK       pPrev=5,15 td=0 parent=15,15 ptd=2
1342: FSCP           tile=13,15 fail=notSeg best=54 ord=6 sf=1
1343: CPL-SAFE-FAIL  veh=57 tile=5,15
1344: CPL-BLOCKED-WAIT veh=57 tile=16,14 dest=12,15        ← 第 346 轮续 C-7 生效
1345: CPL-RET veh=57 tick=12460 tile=15,14 ... ret=0x1 why=cpl-fail spd=0 real=1
1346: TRP veh=57 mf=16,14 real=1 type=16 dest=1932 spd=0 fp=0 look=0 one=1 mstuck=0 fto=1 => ok=0 res=0
1348: SPD-DROP veh=57 tile=16,14 prev=5 ... dt=33
1349: STUCK veh=57 tile=16,14 state=1 stuck=1 ...
```

挂接寻路本身算对了（`CPL-PATHFOUND found=1`、`CPL-BEST tile=5,15`），但回溯经过 `(13,15)` 时上面是 **veh=54**（`ord=6`），`FSCP fail=notSeg` 把整条路径否决 ⇒ `res=0` ⇒ `MarkTrainAsStuck` ⇒ **原地停住**。

### 8.3 结论 3：「走一段再铺预留」的真身 = `R3R-RES-ONCE` 一次性放行把车推走一格

停住之后每 20 tick 重试一次（`CPL-S0-CHK ... wait=1..19 / 20..39 / 40..79 / 100..`），每次仍 `FSCP` 否决（障碍格从 `13,15 → 14,15 → 15,15` 在变，说明 54 正在移动）：

```
1445: FSCP tile=14,15 fail=notSeg best=54 ord=6 sf=1 → ok=0 res=0
1496: FSCP-CPL tile=15,15 veh=57 pos=16,14 dir=5 spd=0 end=16,14 endtd=0 → ok=0 res=0
1602: R3R-RES-ONCE used head=57 force_proceed=2 tile=16,14     ← 一次性放行被消费
1603: R3R-RES-ONCE clear head=57 tile=16,14
1604: TRP veh=57 mf=16,14 ... look=1 one=0 ... => ok=1 res=1  ← 这次成功了
1605: CPLSPD veh=57 tick=12560 tile=16,14 spd=0 ... adv=31
1606: STUCK veh=57 ... state=0 stuck=0 ... spd=2              ← 解除 stuck，开动
1790: CPL-RET veh=57 tick=12670 tile=14,14 ... ret=0x1 why=cpl-fail  ← 走了一格又失败
1791: TRP veh=57 mf=15,14 ... => ok=0 res=0
```

⇒ **观感链条**：换向当 tick 铺预留 → 被 54 挡、失败 → 停住重试 → 某次 `ForceReserveOnce` 放行、成功铺一格 → 车走一格（`16,14 → 15,14`）→ 下一格又失败 → 再停。
玩家看到的"走到路点格最西端才尝试预留"，实际是 **"每次只铺出一格、走一格再试"**，不是"调用时机延迟"。

### 8.4 与玩家口述的差异（必须回报）

| 玩家观察 | 日志事实 | 判定 |
|---|---|---|
| 挂接订单换向后要走到路点格另一端才 `TryPathReserve` | 换向当 tick 就调了（`TPR-ZERO-REACH turn=1`，与普通订单一模一样） | **调用时机无误**，面板观察被"失败-重试-挪一格"掩盖 |
| 普通订单换向后立即铺 | 立即调、且**成功**（不挡） | 一致 |
| 挂接订单换向后不立即铺 | 立即调、但**失败**（`FSCP notSeg`） | 差异来自"失败"而非"没调" |

### 8.5 新的机制冲突（本轮真问题，待拍板）= C-8

**冲突**：第 346 轮续的 C-7 规定"挂接路径被第三方车挡住 ⇒ 原地等待"，但 `R3R-RES-ONCE`（`VehicleRailFlag::ForceReserveOnce`，换向/耦合/解挂时授予的一次性放行）会在重试时**强行放行一次预留**，于是车被推走一格 —— 与"等"的语义直接矛盾，也正是玩家观感的来源。

**C-8（建议）**：`R3R-RES-ONCE` 放行点加判据 —— 若本次是 `OT_GOTO_COUPLE` 且 `R3RCouplePathBlockedByThirdParty()` 为真（路径被第三方车挡），**不消费、不放行**，保持原地等待；障碍离开后由正常预留（非 once）成功，once 位自然保留到下次真正需要时。
判据沿用第 346 轮续的新函数，无新增判据面；负判据：`found=0`（车底未到位）/ 非挂接订单 / 车库挂接 —— 放行行为逐字不变。

**待确认**：玩家是否要"彻底不动，直到路径放出来"（选 C-8），还是"允许一格一格蹭过去也无妨"。

### 8.6 玩家拍板（2026-10-28）= **选 C-8**

**玩家原话（本轮唯一决策句）**：「选C8」。

**口径固化**：挂接订单（`OT_GOTO_COUPLE`）的路径被第三方车挡住时 —— **彻底不动，直到路径放出来**。允许"一格一格蹭过去"的方案被否决。

**落地判据（实现口径，供复测比对）**：
- 位置：`src\train_cmd.cpp` 的 `R3RTryPathReserveCore()`，`R3ROneShotReserveGuard` 实例化处（`} r3r_one_shot{...}` 那一行）之前求值一个 `r3r_one_shot_blocked`，用它把 `armed` 置假。
- 触发三条件（缺一不抑制）：`HasForceReserveOnce()` && `current_order.IsType(OT_GOTO_COUPLE)` && `R3RCouplePathBlockedByThirdParty()`（= 上一次挂接寻路 `CPL-SAFE-FAIL`，见 `yapf_rail.cpp` 每次挂接寻路入口清零）。
- 「不放行」= 不把 `force_proceed` 顶到 `TFP_SIGNAL`，本次照普通规则寻路（被挡就 `TPR ok=0 res=0`）。
- 「不消费」= 守卫析构既不清 `ForceReserveOnce`、也不必还原 `force_proceed`（没改过），once 位留给障碍离开后真正需要的那次。
- 新探针：`R3R-RES-ONCE hold head=N tile=x,y why=couple-path-blocked`，按 `(车号, tile)` 节流 ⇒ 车不动只打一行，正好作为「位置不再变化」的正面证据。
- 负判据（必须逐字不变）：`found=0`（车底还没到位）/ 非挂接订单 / 车库挂接。

**为什么不会死锁**：障碍离开后普通寻路即可成功（不需要越权窗口）；且配对建立时 `R3REnsureCouplePair()` 会主动替它订一次路线（见 `R3RIsParkedWaiting()` 注释里的同款论证）。

### 8.7 落地与构建自证（2026-10-28）

**代码**（仅 `src\train_cmd.cpp`，未碰 `src\*.h`）：
1. `R3RTryPathReserveCore()` 内、`R3ROneShotReserveGuard` 结构体定义之前新增约束块：`const bool r3r_one_shot_blocked = consist->HasForceReserveOnce() && consist->current_order.IsType(OT_GOTO_COUPLE) && R3RCouplePathBlockedByThirdParty();`，命中时按 `(车号, tile)` 节流打 `R3R-RES-ONCE hold head=%d tile=%d,%d why=couple-path-blocked`（`std::unordered_map<uint32_t,uint32_t>` 快照，文件内已用同款容器）。
2. 实例化行 `} r3r_one_shot{...}` 的 `armed` 参数改为 `consist->HasForceReserveOnce() && !r3r_one_shot_blocked` ⇒ 抑制时既不顶 `TFP_SIGNAL`、析构也不清 once 位、不还原 `force_proceed`。

**构建**：复用 `_tmp_inc_build.cmd`（未新建 `.cmd`）；日志尾部 `[2/3] Building CXX object CMakeFiles\openttd_lib.dir\src\train_cmd.cpp.obj` → `[3/3] Linking CXX executable openttd.exe`；`build\R3R_incbuild.done` = `EXIT_CODE=0`；`src\train_cmd.cpp` @2026-10-28 14:09:23 → `build\openttd.exe` @2026-10-28 14:11:26（52 344 832 B）；`read_lints` 0 条；exe 内命中 `R3R-RES-ONCE hold head=%d tile=%d,%d why=couple-path-blocked`。

**状态**：已实现 + 已编译，游戏内复测待做（判据见 KI-517「复测判据」4 条）。

### 8.8 复测反馈（2026-10-28）：C-8 生效，问题前移到「安全位订不上」

**玩家原话**：「问题依然存在，可见于日志。我提出猜想：机车铺预留，最多只能铺设到站台外面第一格，而不能铺入站台内（虽然对于引擎自身的路线没有影响），但是，这意味着预留无法铺到安全停车位」。

**日志指纹**：`build\R3R_debug.log` 4569 行 / 566214 B / 14:15:18，产生者 `build\openttd.exe` @14:11:26（C-8 版）。

**C-8 生效的正面证据**：
- `1377 R3R-RES-ONCE hold head=57 tile=16,14 why=couple-path-blocked`（新探针命中）。
- 此后 57 全程再无 `R3R-RES-ONCE used`（旧版必在 `1602` 出现 used 并被推走一格）。
- 位置恒定：`4520..4569` 的 `CPL-S0-CHK veh=57 tile=16,14 ... wait=54..69` 逐行同 tile。
⇒ **KI-517「走一格才铺一段」已被消除**，判据 ① 达成。

**但问题未解，阻塞点前移**（全部同一 tick 内）：
```
4526 TPR-WALK veh=57 mf=15,14 origin=15,14 td=0 okay=0 ...
4529 CPL-ORIGIN-TD veh=57 tile=16,14 tdb=0x101 tbits=0x1
4545 FSCP-SELFHELD tile=15,14 veh=57 tk=0 want=3 res=0x1
4546 FSCP tile=15,14 fail=tryReserve td=3 want=3 type=1 tbits=0x29 res=0x1 plain=1 depot=0 st=0 self=1
4549 FSCP-VEH tile=15,14 veh=59 track=0x1 dir=5 front=0 ord=0
4551 FSCP-SELF tile=15,14 veh=59 track=0x1 dir=5 front=0 overlap=0x1
4552 CPL-SAFE-FAIL veh=57 tile=15,15
4553 TRP veh=57 ... => ok=0 res=0
```
读数要点（含一次订正）：`want=3` 是 `TrackdirToTrack(td)` 的值 = **TRACK_LOWER**（Track 枚举 0=X,1=Y,2=UPPER,3=LOWER），不是 TRACK_BIT_Y；`tbits=0x29`（X|LOWER|RIGHT）**含 LOWER** ⇒ 轨道存在，失败不是"轨不存在"。真因是 `res=0x1`——该格 X 位已被本链自己订下（`1722 RESV-WATCH SET-RAIL tile=15,14 track=0 phase=controller actor=59 head=57`，即链上 veh=59 沿 TRACK_X 行驶时订的脚下位），X 与 LOWER 交叉互斥 ⇒ `tryReserve` 拒绝。

**失败链闭合**：安全位订不上 ⇒ `4552 CPL-SAFE-FAIL` ⇒ `4339 CPL-RET veh=57 tick=12650 tile=15,14 tracks=0x9 res=0x0 ret=0x1 why=nores-couple-blocked` ⇒ `4553 TRP ok=0 res=0`；`1720 CPL-NORES-BLOCK veh=57 front=16,14 end=16,14 best=0x8` 说明寻路想要的正是 LOWER(0x8)。全日志 `CPL-SELFLOCK` 0 条 ⇒ train_cmd.cpp:18037-18046 那条"自锁不可达"出口从未被走到。

**玩家猜想 C-9 判定：结论对、机制不对**。"到不了安全停车位"成立；"站台限制"不成立——失败格 `15,14` 是普通轨道（`st=0 plain=1 type=1`），本场景站台格是 `12,14`（`389 RESV-WATCH SET-STN tile=12,14`），站台格本身可订（整格位 `SetRailStationReservation`；yapf 走 `ReserveRailStationPlatform`，见 `yapf_rail.cpp:480-485`）。

**性质**：第 338~342 轮已定性的「自己挡自己 + 计划层不可达」（KI-510/KI-358 同族）。C-8 只是把"每 tick 被推走一格"换成了"完全不推进"。物理上确实是死局：车节已站在道岔 X 位上，道岔只能沿 X 走，计划里的 LOWER 永远等不到。

**待拍板（C-10）**：A=自锁时先沿 X 退回、退出交叉格再重规划；B=把自锁格排除出安全位候选，改停更远安全位/换一条不经该道岔的路；C=不让 moving front 进入交叉格（停车点前移），从源头避免跨岔。

**状态**：未改码；KI-518 已记录上述结论与 C-10 选项。

### 8.9 玩家纠正（2026-10-28）＝观察对象错位 + 反对「道岔只能沿 X 走」

**玩家原话**：

> 等等，你看的是veh57-59这台三节铰接式机车，但是出问题的在veh54-56这台三节铰接式机车。而它们三节的长度加起来都不到1格
> 57-59不是我们观察的，54-56才是我说的挂接寻路有问题的机车。你说：54-56车节站在道岔 X 位上，道岔只能沿 X 走，计划里的 LOWER 永远等不到。但我说，54-56完全有能力选UPER-X-LOWER，但它偏偏选了X，不选LOWER

**认定**：§8.8 的两条表述被玩家否决 —— ①「问题车 = veh57-59」判错对象，玩家观察的是 **veh54-56**（三节铰接，三节总长 < 1 格）；②「道岔只能沿 X 走」判错机制，玩家认为 54-56 具备走 `UPPER→X→LOWER` 穿越的能力，却在道岔上选了 X。

**疑点登记（待验证）**：

- **D-5（对象）**：日志 `FSCP-VEH veh=59 track=0x1 / FSCP-SELF self=1` 说的是"57 链自己的 59 节占 X 位"，而玩家指认的是 **54-56**。两种可能必须分清：(a) 54-56 才是真正卡住安全位的第三方车，59 只是同链自身位的表征；(b) 54-56 与 59 是同一"占位簇"的不同视角。需要逐行 dump 54/55/56 的 tick 轨迹与预留归属（`RESV-WATCH ... actor=`）来定。
- **D-6（能力）**：交叉格 `15,14`（`tbits=0x29`=X|LOWER|RIGHT）上，X 位已被订时，"另一车订 LOWER"在 PBS 规则里是否被允许？若 `TryReserveRailTrack` 只查同位（`GetRailReservationTrackBits & TrackToTrackBits(track)`），则 LOWER 本应可订，`FSCP tryReserve` 失败必有别的原因（整格位/交叉互斥/self 守卫）；若引擎对交叉格做了"整格独占"，则玩家的"有能力选 UPPER-X-LOWER"在**同格并存**意义上不成立，但在**55/56 改走 LOWER 而非 X** 意义上仍可能是寻路选错。
- **D-7（长度）**：玩家强调"三节加起来不到 1 格"。若 54-56 是 artic 部件（`cached_veh_length` 极短），则它们在格内的占位与"整格 X 位预留"不匹配 —— 即**预留粒度是整条 Track、不是车辆实际长度**，短车也会独占整条 X 位。这可能是玩家觉得"不合理"的根源，需在代码里确认是否存在"按格位而非按轨位"的额外阻塞。

**猜测根因（待验证，先落盘）**：

- **G-3**：`FSCP`/安全位判据把"格上任何车的任何轨位预留"一律当作阻塞（self 也含），导致 57 要订的 LOWER 被同格他人 X 位挡下；修法方向应从"整格阻塞"退回"按轨位判冲突 + 交叉格按几何判碰撞"，而非让车退让。
- **G-4**：54-56 在道岔上选择 X 而非 LOWER，是寻路在该格的 trackdir 选择被前序格约束（进入方向决定只能直行 X），需要看 54 的 `CPL`/`RP cur=` 序列确认；若确为"本可 LOWER 却选 X"，则属寻路偏好问题，不在预留层。

**状态**：仅登记，未改码未构建；证据文件为 14:15:18 版日志（566214 B），当前 `build\R3R_debug.log` 已被 14:28:18 新会话覆盖（`veh=5x` 零命中），复现需玩家重跑或提供该版日志副本。

### 8.10 取证 1：原始日志已定位 + 54-56 的真实读数（2026-10-28）

**日志副本**：`D:\temp\codebuddy-dropped-files\b101421a-e7ca-4201-9158-976b2a59644f\R3R_debug.log`（552.94 KB = 566 210 B，与 14:15:18 版 566 214 B 一致，行号与 §8.8 引用逐行对齐：`4546 FSCP tile=15,14 fail=tryReserve ...`、`4569` 为末行前一）。⇒ 后续取证一律用这个副本，不再依赖会被覆盖的 `build\R3R_debug.log`。

**读数 A：veh=54/55/56 是三节铰接组，且确实走过 LOWER**

```
1501: FSCP-VEH tile=15,15 veh=56 track=0x1 dir=5 front=0 ord=0
1502: FSCP-VEH tile=15,15 veh=55 track=0x1 dir=5 front=0 ord=0
1503: FSCP-VEH tile=15,15 veh=54 track=0x1 dir=5 front=1 ord=6
2160: FSCP-VEH tile=14,15 veh=55 track=0x8 dir=6 front=0 ord=0
2161: FSCP-VEH tile=14,15 veh=56 track=0x8 dir=6 front=0 ord=0
```

- `front=1` 在 54 ⇒ 54 是该三节组的链头（正确对象）。
- `15,15` 上三节都 `track=0x1`（TRACK_BIT_X）、`dir=5`（SW）；`14,15` 上两节 `track=0x8`（TRACK_BIT_LOWER）、`dir=6`（W）。
⇒ **54-56 的行程里既有 LOWER 又有 X**，玩家的"有能力选 LOWER 却选 X"有事实基础：它在 `14,15` 沿 LOWER 走，到 `15,15` 换成 X。

**读数 B：15,14 的自锁是"本链自己"占 X，与 54-56 无关**

```
1722: RESV-WATCH SET-RAIL tile=15,14 track=0 phase=controller actor=59 head=57
4545: FSCP-SELFHELD tile=15,14 veh=57 tk=0 want=3 res=0x1
4546: FSCP tile=15,14 fail=tryReserve td=3 want=3 type=1 tbits=0x29 res=0x1 plain=1 depot=0 st=0 self=1
4549: FSCP-VEH tile=15,14 veh=59 track=0x1 dir=5 front=0 ord=0
```

⇒ `15,14` 的 `res=0x1`（X 位）属于 59（57 链尾节），57 要订 `want=3`=TRACK_LOWER 被拒。**这是 57 链内部的自锁，不是 54-56 造成的**。玩家指认的 54-56 与 `15,14` 的自锁是两件事，必须分开处理（D-5 的 (a) 成立、54-56 另有其因）。

**读数 C：引擎层互斥的真实规则（否定我上一轮的措辞，但也不支持"同格并存"）**

```
src\rail_map.h:267  inline bool TryReserveTrack(TileIndex tile, Track t)
src\rail_map.h:272    if ((res & bits) != TRACK_BIT_NONE) return false;
src\rail_map.h:274    if (TracksOverlap(res)) return false;  // crossing reservation present
```

⇒ 交叉格上 `X` 与 `LOWER` 是**物理交叉**，`TracksOverlap` 恒真 ⇒ 同格不能同时持有 X 与 LOWER。**我上一轮说"道岔只能沿 X 走"的措辞是错的**（那是"该格当前只有 X 位可达"的表象），正确表述是：*同格同时只能持有一条自洽通路（互不重叠的轨位集合），X 与 LOWER 二选一*。玩家"54-56 能走 UPPER-X-LOWER"在**换位序列**意义上成立（先 X、离开后再 LOWER，不同格不同时），在**同格并存**意义上不成立。

**新的疑点 D-8**：54-56 在 `15,15` 用 X（`track=0x1`、`dir=5`），但 `15,15` 的 trackbits 尚未取到 —— 若该格同时含 `LOWER`，则 54-56 本可沿 LOWER 直行而把 X 让给 57 的路径；若不含，则"选 X"是被轨道拓扑强制的，用户观察需修正。**下一步必取 `15,15` 的 tbits**（日志 `FSCP tile=15,15 ... tbits=` 或 `RESV-WATCH SET-RAIL tile=15,15`）。

**状态**：取证 1 完成，未改码。

### 8.11 取证 2：两格的轨道拓扑 + 54-56 其实在"震荡"（2026-10-28）

**读数 D：两格的 trackbits（决定谁能选什么轨）**

```
1498: FSCP tile=15,15 fail=tryReserve td=2 want=2 type=1 tbits=0x15 res=0x1 plain=1 ... self=0
4546: FSCP tile=15,14 fail=tryReserve td=3 want=3 type=1 tbits=0x29 res=0x1 plain=1 ... self=1
```

- `15,15`：`tbits=0x15` = **X(0x1) + UPPER(0x4) + LEFT(0x10)**，**没有 LOWER**。
- `15,14`：`tbits=0x29` = **X(0x1) + LOWER(0x8) + RIGHT(0x20)**，**没有 UPPER**。

⇒ 玩家说的 `UPPER-X-LOWER` **无法在同一格内完成**（15,15 无 LOWER、15,14 无 UPPER），它只能是一条**跨格**通路；跨格要求两格同时被同一列车连续持有，而 `54` 正占着 `15,15` 的 X 位 ⇒ 57 订 `UPPER(2)` 被 `TracksOverlap` 拒。**我上一轮的"道岔只能沿 X 走"措辞错误，正确说法是"同格 X 与 UPPER/LOWER 物理交叉、不能同时预留"，但玩家"该车本可换一条轨"的直觉在跨格语境下是对的。**

**读数 E：54 不是静止站在道岔上，而是在 `15,15 ↔ 16,15` 之间来回**

```
1500: FSCP-RES tile=15,15 tk=0 owner=54 otile=15,15 odir=5 ospd=100 ostop=0 ord=6 oroi=1 oend=18,16 oendtd=8 othru=1
1549: FSCP-RES tile=15,15 tk=0 owner=54 otile=16,15 odir=4 ospd=98  ostop=0 ord=6 oroi=1 oend=18,16 oendtd=8 othru=0
4026: FSCP-RES tile=15,15 tk=0 owner=54 otile=15,15 odir=5 ospd=100 ...
4091: FSCP-RES tile=15,15 tk=0 owner=54 otile=16,15 odir=4 ospd=98  ...
```

- `ospd=100/98` 是**满速**（不是停住），`otile` 在 `15,15` 与 `16,15` 之间交替、`odir` 在 `5↔4` 翻转 ⇒ 54 在**两格之间往复**，不是"停在道岔上挑轨"。
- `54` 自己的订单：`ord=6`、`oend=18,16`、`endtd=8` ⇒ 它在跑自己的第 6 条命令（`1503` 显示它仍是 `front=1` 的三节组链头），与 57 的挂接无关。

⇒ **玩家观察"54-56 站在道岔 X 位上"的机制是"它反复经过/折返"，而 57 每 20 tick 重试一次，恰好总撞在它在格上的时刻。** 这解释了两件事：①为什么"停车位永远订不上"（不是静态占位，是周期性占用）；②为什么 57 等多久都不行（只要 54 的往复周期与 57 的重试节拍同相，永远撞车）。

**疑点 D-9（升级，玩家指认对象）**：54-56 为什么在 `15,15 ↔ 16,15` 往复？它的 `oend=18,16 endtd=8` 是可达目标，`ospd` 满速却不离开 ⇒ 这是**它自己的寻路/预留被拒后的折返**，很可能才是玩家说的"54-56 挂接寻路有问题"。需要 54 的 `TPR/CPL` 行序列定因（下一步）。

**疑点 D-10**：`15,14` 的自锁（`res=0x1` 属 59、`self=1`）与 54 无关，是第 338~342 轮已定性的「自己挡自己」。两件事必须分开修：D-10 需要 57 先退车释放 X 位（C-10 的 A），D-9 需要看 54 的寻路为何不通过。

**状态**：取证 2 完成，未改码。

### 8.12 玩家假设 G-5（待验证，先落盘）

**玩家原话（2026-10-28）**：

> 好，那么，你认为，54-56卡住的原因就是56压在了（15，14）上面，导致54-56不能选择（15，14）的道岔，对吧

**登记为猜测根因 G-5（待验证）**：`54-56` 反复出不去（`15,15 ↔ 16,15` 往复）的直接原因是"**56 压在 `15,14` 上**"，使 `54-56` 自己无法预订 `15,14` 的道岔（`tbits=0x29` = X|LOWER|RIGHT）。

**与已有读数的冲突（必须先解决，否则不能确认）**：

- 已有 FSCP 采样里 `56` 只出现在 `15,15`（`1501 FSCP-VEH tile=15,15 veh=56 track=0x1`）与 `14,15`（`2161 FSCP-VEH tile=14,15 veh=56 track=0x8`），**从未在 `15,14`**。
- `15,14` 的 X 位归属已实证为 59（`1722 RESV-WATCH SET-RAIL tile=15,14 track=0 actor=59 head=57`、`4549 FSCP-VEH tile=15,14 veh=59`），`54-56` 从未在该格留下预留/位置记录。

⇒ 若 G-5 成立，必要条件是在日志里找到 `veh=56` 与 `tile=15,14` 同现的行（位置、预留、TPR-WALK 任一形式）。取数判据：`tile=15,14` 全行 dump + `veh=5[456]` 位置序列；命中即确认，零命中则 G-5 证伪、54-56 卡因仍在 `15,15` 自身（D-9）。

**状态**：登记待验，本轮取数中。

### 8.13 取证 3：G-5 **证伪** —— `15,14` 上与 54-56 无任何交集

**取数方法**：对副本日志做 `tile=15,14` 全行 dump（60+ 行，含 `RESV-EXT-PRE`/`CPL-RET`/`RESV-WATCH`/`FSCP*` 全家族）。

**读数 F**：全部 `tile=15,14` 行只出现 **veh=57 与 veh=59**，`54/55/56` 零命中。代表性行：

```
1321 RESV-EXT-PRE veh=57 tile=15,14 enterdir=0 tracks=0x9 reserved=0x0 doRes=1
1722 RESV-WATCH SET-RAIL tile=15,14 track=0 phase=controller actor=59 head=57
1790 FSCP-RES tile=15,14 tk=0 owner=57 otile=16,14 odir=5 ospd=0 ord=16 oend=15,14 oendtd=0
1792 FSCP-VEH tile=15,14 veh=59 track=0x1 dir=5 front=0 ord=0
1794 FSCP-SELF tile=15,14 veh=59 track=0x1 dir=5 front=0 overlap=0x1
```

**关键订正**：`15,14` 的 X 位所有者是 **owner=57**（`otile=16,14`，即 57 链的车身/车尾覆盖到 `15,14`），`59` 只是链上被采样到的那一节。**这是 57 链自己跨两格占位（16,14 起步、车尾压进 15,14）**，与 54-56 无关。

**判定**：G-5 **不成立**。`56` 从未出现在 `15,14`；`54-56` 三节在日志里同处 `15,15`（`1501~1503` 三行同 `tile=15,15`），符合玩家所说"三节总长不到 1 格"——**整组共用一个 tile，物理上不可能有一节单独压到相邻格**（视觉渲染会跨格边界，但引擎的 tile 归属不会）。

**推论（修正玩家与我的共同误解）**：玩家看到的"56 压在 15,14 上"极可能是**贴图跨格**造成的观感；在占用/预留层，`54-56` 的位置集合 = `{15,15}`。

**54-56 卡住的真实落点**：`15,15 ↔ 16,15` 往复（`1500/1549/4026/4091`），即出不去发生在 **`16,15` 一侧或更远**，`15,14` 完全不在它的路径上。**D-9 仍未定因，需取 `16,15` 的占用者与 54 的 `TPR/CPL` 行。**

**状态**：G-5 证伪；D-9 待取数（下一步 = `tile=16,15`）。

### 8.14 取证 4：D-9 初步定位 —— 54 被「同格静止纯车底」判成阻塞（KI-330 同族）

**读数 G（`tile=16,15|17,16` 与 `TPR-BLOCKED veh=5x` 全量，共 6 行）**：

```
1218 TPR-BLOCKED veh=54 tile=12,15 spd=2 mf=12,15 origin=12,15 okay=1 td=8 other=0 otherTile=12,15 otherMf=12,15 otherSpd=0 otherOrder=0 otherFirst=0 caronly=1
3734 TPR-BLOCKED veh=54 tile=12,15 spd=2 mf=12,15 origin=12,15 okay=1 td=8 other=0 otherTile=12,15 otherMf=12,15 otherSpd=0 otherOrder=0 otherFirst=0 caronly=1
1549 FSCP-RES tile=15,15 tk=0 owner=54 otile=16,15 odir=4 ospd=98 ostop=0 ord=6 oend=18,16 oendtd=8 othru=0
2018 FSCP-RES tile=14,15 tk=3 owner=54 otile=17,16 odir=5 ospd=100 ostop=0 ord=2 oroi=2 oend=1,15 oendtd=8 othru=1
4091 FSCP-RES tile=15,15 tk=0 owner=54 otile=16,15 odir=4 ospd=98 ostop=0 ord=6 oend=18,16 oendtd=8 othru=0
4494 UNRES-RAIL tile=17,16 track=0 phase=choose-track actor=54
4495 RESV-WATCH CLEAR-RAIL tile=17,16 track=0 phase=choose-track actor=54 head=54
```

**判定**：`54` 的 `TPR-BLOCKED` 与 KI-330 的现场**逐字段同构**——`other=0`（另一列车链头 veh=0）、`otherTile == mf == 12,15`（同一格）、`otherSpd=0`（停着）、`otherOrder=0`（`OT_NOTHING`，不是 `OT_WAIT_COUPLE`）、`otherFirst=0`、`caronly=1`（纯车底）。即：

1. `pbs.cpp` 的 `CheckTrainsOnTrack` 把**停在同一格上的静止纯车底**判为"前方阻塞"，不排除同格静止；
2. 命中后 `MarkTrainAsStuck` + 寻路返回 `TPRRF_NONE`（`train_cmd.cpp` 的 `FollowTrainReservation` 分支，早于 `ChooseTrainTrack`）；
3. 于是 `54` 这一 tick 不推进、下 tick 重试，位置在 `15,15 ↔ 16,15` 抖动（`FSCP-RES` 的 `otile` 交替即由此而来），`ord` 在 `6 ↔ 2` 之间回退，`17,16` 的 X 位预留被反复 `UNRES-RAIL/CLEAR-RAIL`（4494/4495）。

⇒ **D-9 的机制与 KI-330 是同一个缺陷的第 N 个现场**，不是新的道岔拓扑问题，也不是 54-56 的订单/寻路选错轨。

**与 D-10 的关系**：现在三条独立阻塞全部归位——A=`15,15` 被 54 占 X（对 57 来说的第三方）；B=`15,14` 是 57 链自己的 59/57 车身压 X（自锁）；C=54 自己被 12,15 的静止车底 veh=0 判阻塞（KI-330 同族）。**玩家最初说"54-56 才是挂接寻路有问题的机车"在 C 这一条上是对的。**

**状态**：D-9 机制定性（KI-330 同族），待确认 veh=0 的身份（是否解挂产生的等待车底）与 KI-330 修法适用性。

---

## 第 348 轮（2026-10-28）：v57 走对了岔道，但速度被压在 30

**现场**：`build\R3R_debug.log` 2163 行、16:34:37；产生者 `build\openttd.exe` 16:32:40（含第 347 轮 S2 后续的"安全位判据本车豁免"）。

**结论 1（上一轮修复已验证通过）**：全日志 `FSCP ... fail=other ... other=57` 0 条、`CPL-SAFE-FAIL` 0 条；`LOCO-MOVE veh=57 tile=15,14 track=08`（LOWER，旧现场是 `track=01`），`CPL-RESERVE veh=57 reserved=1 next=3 path=4 booked=4`（:1683/:1847）与 `CPL-TRACE`/`CPL-BACK` 全程 LOWER+南下，即"订得对、走得对"。

**结论 2（本轮现象根因）**：速度卡在 30 与限速、倒车、无驾驶室全部无关，是**上游 realistic-braking 的 `lookahead == nullptr` 兜底**：

```
src\train_cmd.cpp:1695-1725
if (this->UsingRealisticBraking()) {
    if (this->lookahead != nullptr) { ... LimitSpeedFromLookAhead ... }
    else { advisory_max_speed = std::min(advisory_max_speed, 30); }   // :1723
}
```

**铁证（唯一定量读数）**：

```
1521 MAXSPD-BREAK veh=57 tile=16,14 spd=0 strict=100 adv=30 look=0 lookend=-1
     nocab=0 revd=0 revspd=-1 spr=0 sig=0 ordmax=65535 realbrk=1 db=1
     track=1 maxTrack=100 maxCurve=65535
```

`strict=100`＝真实上限，`adv=30`＝兜底结果，`look=0`＝`lookahead` 为空是唯一可疑项；
`revd=0`（不是倒车距离）、`nocab=0`（不是 32 km/h 无驾驶室限速）、`spr=0`/`sig=0`/`ordmax=65535`（不是信号或订单限速）。对照 `:600 MAXSPD-BREAK ... strict=5 adv=5 look=1` —— lookahead 存在时 adv 直接等于 strict，可见兜底只在 look==0 时生效。

**同族旁证**：`:1858 RESV-NOLOOK-PASS veh=57 tile=14,15 enterdir=0 res=0x1 tracks=0x9` —— 预留扩展路径同样因"没有 lookahead"直接 PASS。即 R3R 挂接寻路用低层 `TryReserveRailTrack` 订下的 4 格（`CPL-RESERVE`），**从未配套建立 lookahead**。

**速度曲线（解释"先在 55、后掉到 30"）**：

```
tick 12563~12587  CPLSPD veh=57 ... spd=6 → 55   （此段 adv=100，可正常加速）
tick 12611        LOCO-MOVE veh=57 tile=16,14 j=260 spd=31   （:1805，掉头）
tick 12635~       LOCO-MOVE veh=57 tile=15,14 j=270 spd=30 track=08   （:1853 起恒定 30）
```

**另记（不是本轮主因）**：`:2015 / :2091 NOCAB-SET this=57 db=1 last=26 lastSub=0x08 lastEng=1 lastLead=1 leadCab=0` —— 日志尾部才置 `TCF_NO_DRIVING_CAB`（32 km/h），发生在 30 平台之后，会叠加同量级限速，需在修好 lookahead 后复查是否仍在。

**待验证（疑点）**：`lookahead` 在哪一步被清空/从未创建。候选 A＝R3R 挂接预订替换普通预留时 `FreeTrainTrackReservation` 把 lookahead 复位；候选 B＝"无预留滑行"分支根本不建 lookahead。判据＝在 `SetupLookAhead`/`lookahead.reset()` 两处加时间戳探针，与 `CPL-RESERVE` 行对齐。

**修法候选**：①挂接预留订成后对链头重建 lookahead（`SetupLookAhead()` 后 `UpdateLookAhead()`）；②`:1723` 兜底对"手上有挂接预留（`CPL-RESERVE` 记账非空）"的车放宽。①治根、②兜底。

**已定性（补）**：疑点已解，`lookahead` 不是"从未创建"，而是**建好后被 `AdvanceLookAheadPosition()`（`src\train_cmd.cpp` 约 1447 行）删掉**——判据是车头越过 `reservation_end_position + 8` 且不在库内就 `lookahead.reset()`，注释自陈"它会在有新预留时重新创建"；挂接路径只有 4 格，开完就失效，而重建只发生在 `ChooseTrainTrack` 的 `result_flags & CTTRF_RESERVATION_MADE` 分支（约 18159），R3R 的 couple 寻路走 `r3r_chosen_ret` 提前 return，不经该分支 ⇒ 永久为空。

**落地（第 348 轮，KI-519）**：`TrainLocoHandler` 移动分支入口（`TrainCheckIfLineEnds(moving_front)` 之前）加自愈——`TBM_REALISTIC && lookahead == nullptr && cur_speed > 0 && OT_GOTO_COUPLE && 非库` 时 `FillTrainReservationLookAhead(consist)`，按车号限流 32 tick，探针 `RESV-LA-REBUILD veh= tile= spd= order= own= look= end=`。

**构建自证（第 348 轮）**：复用既有 `build\_r3r_build.cmd`（未新建 `.cmd`；根目录已无 `_tmp_inc_build.cmd`，改用记忆外的真实入口，注意 `Start-Process -ArgumentList '/c','_tmp_inc_build.cmd'` 会因脚本不存在而静默失败、done/log 保持旧时间戳）；`[2/3] train_cmd.cpp.obj` → `[3/3] Linking`、`CMAKE_EXIT=0`、`src\train_cmd.cpp` 16:39:27 → `build\openttd.exe` 16:42:06（52 344 832 B）、`read_lints` 0、exe 命中 `RESV-LA-REBUILD`。




