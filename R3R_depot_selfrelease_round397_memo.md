# 第 397 轮：R-396-1 首版复测未生效（DEPOT-SELF-RELEASE 零命中）取证与分析

- 日期：2026-11-08
- 触发：玩家原话「见build/R3R_debug.log，问题依然存在，还是库内铺设了挂接但是就是不出来」
- 上一轮落地：`R3R_depot_stay_round396_memo.md` §8.9（R-396-1 = 库格自有预留限时豁免，256 tick）

## 一、采样有效性（已核）

| 项 | 读数 |
| --- | --- |
| `build\R3R_debug.log` | 81 672 B / 708 行 / mtime 2026-11-08 01:19:03 |
| 产生者 `build\openttd.exe` | mtime 2026-11-07 23:59:41（52 458 496 B） |

⇒ exe 正是 R-396-1 那一版（23:59:41 = 第 396 轮续增量构建产物），**采样有效**，本轮结论可直接用于判定 R-396-1 成败。

## 二、现场读数（探针计数）

```
DEPOT-STAY            = 90
  exit=11 (exit_blocked) = 44
  exit=7  (等 37 tick)   = 45
  exit=1  (库型 GOTO_COUPLE 停留) = 1
DEPOT-SELF-RELEASE    = 0      ← 关键
COUPLE-OK             = 1
CPL-BEST              = 1
CPLRES                = 3
RESV-TERM             = 3
RESV-END              = 4
TPR-EXIT              = 4
CPL-RES-SAFE-STOP     = 0
RESV-SELF-DEPOT-MOUTH = 1
```

原文样本（逐字，`:622`~`:708` 区段反复同形）：

```
DEPOT-STAY veh=27 exit=11 power=5325 speed=0 order=16 wait=0 resv=1 track=128 fully=1 stuck=0
DEPOT-STAY veh=27 exit=7  power=5325 speed=0 order=16 wait=1 resv=1 track=128 fully=1 stuck=0
```

- `exit=11` / `exit=7` 仍按 44/45 交替 ⇒ **KI-567 的死循环形态逐字未变**，与第 396 轮（114/113）同族，只是本会话更短。
- `wait=0` 出现在 exit=11 行：`:15413 v->wait_counter = 0;` 在判 `exit_blocked` 之前执行（`:15415` 之后才取 `seg_state`），故 `wait=0` 是必然形态，非新证据。
- `resv=1` + `track=128`(TRACK_BIT_DEPOT) + `fully=1` + `stuck=0` + `order=16`(OT_GOTO_COUPLE) 全部与第 396 轮逐字一致。
- **`DEPOT-SELF-RELEASE = 0`** ⇒ R-396-1 的豁免分支在本会话**一次都没命中**。这是本轮唯一的新事实，也是"问题依然存在"的直接原因。

## 三、根因（已实证·代码级）：豁免种子与出口互相依赖，构成第二层循环依赖

R-396-1 首版的种子写在放行成功之后：

```15518:15520:src/train_cmd.cpp
	SetDepotReservation(v->tile, true);
	/* R3R (U-396-1c, R-396-1): 记下「本车刚在此库格放行过」，供后续 tick / TryPathReserve 判自我预留。 */
	R3RMarkDepotSelfRelease(v);
```

但 `exit_blocked` 在更早处就直接返回了：

```15509:15509:src/train_cmd.cpp
	if (exit_blocked) return r3r_depot_stay(11);
```

⇒ 死锁态下执行流**永远到不了** `:15518`，`_r3r_depot_self_release` 永远没有这条记录，`R3RDepotSelfReleased()` 恒 false ⇒ `:15430` 的 `(depot_reserved && !self_released)` 恒真 ⇒ 继续 exit=11。

这就是一个**自我依赖的循环**：

- 要豁免，必须先"曾经放行过"；
- 要放行，必须先"不被 exit_blocked 拦住"。

⇒ R-396-1 首版在"预留是存档带入 / 历史残留 / 本会话从未放行"的场景下**结构上不可能生效**。本轮日志正是这种场景（90 条 DEPOT-STAY 里 exit=11 占一半，而 `DEPOT-SELF-RELEASE` 0 条）。

> 一句结论：种子不该取自"放行的那一刻"，而应取自"**这个库格预留现在是不是本车自己的**"。

## 四、疑点登记（REG-397-*）

- **REG-397-A（未解·影响判定）**：`exit=11` 有两个 disjunct —— `seg_state == SigSegState::Full` 与 `(depot_reserved && !self_released)`（`:15430`）。当前探针只给 `resv=1`，**无法区分究竟命中哪一个**。若真因是 `seg_state == Full`（即库外信号段整段占满），那连"自有预留豁免"都不对症。⇒ 必须先补一条 `seg_state=` 读数。
- **REG-397-B（未解）**：`resv=1` 的这个预留到底属于谁？`HasDepotReservation()` 只答"有没有"，不答"是谁的"。若归属是 `nullptr`（无主残留）而不是 veh=27 自己，那连"自订自锁"这个定性都要重审（第 396 轮的定性来自"置位点只有 `:15455`/`:19104`，而 `:15455` 必然是本车放行"，但存档带入的位不经过任何置位点）。
- **REG-397-C（旁证·未深挖）**：本会话 `COUPLE-OK=1`、`TPR-EXIT=4`、`RESV-END=4` ⇒ veh=27 曾成功出库过至少一次（与第 396 轮 REG-396-G 同族），说明"卡死"是**间歇态**而非全程。按第 396 轮定的方法，必须按 `DEPOT-STAY` 段分组取同 tick 的 `CPLRES`/`RESV-END`/`TPR-EXIT` 才能定位真正卡死那一段。

## 五、修法候选（R-397-1，建议）

把"豁免"的判据从"曾经放行过"改为"**该库格预留的归属是本车 / 无主**"，即用 `pbs.cpp` 已有的归属查询替代 tick 快照：

- 归属查询入口（第五轮记的）：`GetTrainForReservation(TileIndex, Trackdir, RailTypes)` 与整格版 `GetTrainForWholeTileReservation(TileIndex)`（`pbs.cpp`，第 125 轮新增）。
- 判定：`TileIndex owner = GetTrainForWholeTileReservation(v->tile)`；`self_owned = (owner == nullptr || owner == v->First())`。
- 这样「存档带入的残留位（无主）」「本车自己订的位」两类都能过闸，而**他人持有的位照旧阻塞**，与 R-396-1 拍板口径完全一致，且不再依赖任何 tick 快照。
- 同时把 `seg_state` 也打进 `DEPOT-STAY`（或单独 `DEPOT-EXIT-STATE` 行），把 REG-397-A 的 disjunct 分开。
- **保留** `:18874` `TryPathReserve` 的自锁豁免，但同样改为 `self_owned` 判据（否则放行后在 `:18803-18809` 立刻 `TPRRF_NONE` → exit=12）。

风险/边界：
- `GetTrainForWholeTileReservation()` 在"格上有车停放"时会直接把格上列车当主人 ⇒ 对"库内自己停着"的情形恒返回自己，天然自洽。
- 若归属是"另一列车"但该列车已不在库内（悬垂），需另配 GC；本轮不动 GC 面。

## 六、复测判据（R-397-1 落地后）

1. `DEPOT-STAY veh=27 exit=11` 消失或骤减；`track` 由 `0x80` 变为普通轨道位；车真正驶离库格（`LINEND veh=27` 出现新 tile）。
2. 新增 `DEPOT-SELF-RELEASE`（或改名后的等价探针）在同一车身上出现 ≥1 次，且带 `owner=` 读数（自己/无主/他人）。
3. 负判据：**他人**持有该库格预留时仍 `exit=11`。
4. 负判据：`seg_state == SigSegState::Full` 仍无条件阻塞（若实测发现真因是这个，则本条会暴露出来）。
5. 普通出库（非 `OT_GOTO_COUPLE`）无回归。

## 七、状态与构建自证

- R-396-1 首版：**未生效（已实证）**，原因见 §三。
- R-397-1：**已实现 + 已编译**，游戏内复测待做。

构建自证（2026-11-08，复用既有入口 `R3R_fullbuild.cmd inc detach`，未新建任何 .cmd）：

| 项 | 读数 |
| --- | --- |
| `build\R3R_incbuild.done` | `EXIT_CODE=0`（2026-11-08 01:27:26） |
| 日志尾部 | `[2/3] Building ... train_cmd.cpp.obj` → `[3/3] Linking CXX executable openttd.exe` |
| 错误计数 | `error C` / `fatal error` / `FAILED:` / `build stopped` = 0 |
| `src\train_cmd.cpp` | 2026-11-08 01:22:32 |
| `train_cmd.cpp.obj` | 2026-11-08 01:25:31 |
| `build\openttd.exe` | **2026-11-08 01:27:12（52 488 192 B）** |
| 字面量自证 | `findstr` 命中 `booked=` / `seg=%d` |
| `read_lints` | 0 条 |

注：第一次启动构建时 `openttd.exe`（PID 13424）仍在运行，待玩家关闭后才成功（脚本闸门会写 `EXIT_CODE=97`，故必须看时间戳而非仅看文件存在）。

## 七之二、R-397-1 落地清单（供复测对照）

| 位置 | 改动 |
| --- | --- |
| `src\train_cmd.cpp` `R3RDepotSelfReleased()` | 判据由"256 tick 内曾放行"改为"库格预留归属=本链"（`GetTrainForWholeTileReservation(head->tile)` 的 `owner->First()==head`） |
| `:15430` exit=11 | 沿用同一函数；阻塞式 `seg_state == SigSegState::Full \|\| (depot_reserved && !self_released)` 不变 |
| `:15440` `DEPOT-SELF-RELEASE` | 扩为 `veh= tile= wait= order= owner= booked= seg=` |
| `:15433` `r3r_depot_stay` lambda | 增加 `seg` 形参并编进边沿 key；`DEPOT-STAY` 尾部加 `seg=%d` |
| `:15509` exit=11 出口 | 传 `(int)seg_state` |
| `:18874` `TryPathReserve` | 沿用同一函数（只改前者会立刻 exit=12） |

## 八、第 398 轮复测（2026-11-08，R-397-1 生效但 exit=12 顶替）

**采样有效性**：`build\R3R_debug.log` 106 877 B / 924 行 / 2026-11-08 01:57:50；产生者 `build\openttd.exe` 2026-11-08 01:27:12（52 488 192 B）= R-397-1 那一版。有效。

**R-397-1 确实生效了**：

```
DEPOT-STAY exit= 分布：exit=1 x1, exit=7 x77, exit=12 x76   （exit=11 = 0）
DEPOT-SELF-RELEASE x6：veh=27 tile=279,126 wait=0 order=16 owner=27 booked=1 seg=2
exit 序列：1,7,12,7,12,7,12,7,12,...（严格 7/12 交替，共 154 条）
```

- `exit=11` **归零** ⇒ 库格预留不再拦住出库（R-397-1 的判据正确，`owner=27` 也证实"格上停着车即主人"）。
- `seg=2` = `SigSegState::Path`（枚举定义 `signal_func.h:143`：`Free=0, Full=1, Path=2`）⇒ `reserve_paths` 开启时 `seg_state` 恒为 Path，`seg_state == SigSegState::Full` 这一半从来不会命中；`exit=11` 只剩预留那一半，现已放开。**REG-397-A 结案：库里不是信号段满。**
- `booked=1` ⇒ `_r3r_depot_self_release` 里已有本车记录（放行码 :15551 这次真的执行到了）。

**新堵点 = exit=12（`TryPathReserve` 返 `TPRRF_NONE`，:15545-15548）**：

每轮（37 tick）固定三段：

```
DEPOT-SELF-RELEASE veh=27 ... seg=2        （豁免已确认）
RESV-BEGIN veh=27 tick=2195 tile=279,126 mf=279,126 td=8 ... prevEnd=285,127 prevEndPos=106
TRP veh=27 mf=279,126 real=4 type=16 dest=65308 spd=0 fp=0 look=1 one=0 mstuck=0 fto=0 => ok=0 res=0
DEPOT-STAY veh=27 exit=12 ... stuck=1
STUCK veh=27 tile=279,126 state=1 stuck=1 ...
```

判读：

1. `TRP ... => ok=0 res=0` 是外层 `TryPathReserve` 的出口读数 ⇒ 核心确实拒绝。
2. **但 `TPR-EXIT` / `TPR-WALK` / `TPR-ZERO-REACH` / `TPR-BLOCKED` 在卡死段全部静默**（只有早期 :320~:444 那几次成功出入库才有）⇒ NONE 来自 `R3RTryPathReserveCore` 里 `:18974`（`FollowTrainReservation`）**之前**。
3. 外层三道 R3R 门（`RESV-NOBOOK` :18628 / `CPL-WAIT-NOBOOK` :18661 / `CPL-HOLD-NOBOOK` :18684）在卡死段也静默（`RESV-NOBOOK` 只有 :200 那次 `reason=depot-paired`）⇒ 不是"等待配对的门"拒的。
4. `:18974` 之前适用"车在库内"的 NONE 出口只有两个：`:18907`（库格预留，已豁免）与 `:18913`（**库口下一格 `HasReservedTracks`，此前完全没有探针**）。
5. `stuck=1` 常驻 ⇒ `MarkTrainAsStuck` 走过（首次清 `wait_counter`，之后不再清），与"每 37 tick 重来一次"吻合：`:15434 ++wait_counter` 累到 37 → 判 → 卡 → 回 0 重来。
6. `prevEnd=285,127` 且现场有 `SVC-DEPOT-SKIP veh=21 ... tile=285,127` / `TPR-BLOCKED ... other=21 otherTile=285,127` ⇒ 库外 6 格处停着 veh=21，它的预留沿路可能覆盖库口下一格。

**结论（强推断，待 R-398-1 探针坐实）**：`TryPathReserve` 的库口格检查（`:18913`）在拦。两个子情形，修法不同：

- `nextOwner = 27`（本车自己的旧路径残留）⇒ 修法 = 出库前清掉自己的旧预留再重订（自愈释放）。
- `nextOwner = 21`（别人/veh=21 的预留）⇒ 修法 = 配对协议优先（让库内车先把库口留出来，或让 veh=21 让位），属于调度层。
- `nextOwner = -1`（无主/存档残留）⇒ 修法 = 按 `booked` 语义当作自有残留清掉。

**R-398-1 落地（仅探针，零行为改动）**：`src\train_cmd.cpp` `R3RTryPathReserveCore` 的 `moving_front->track == TRACK_BIT_DEPOT` 段改为先用局部量求值再判定，并新增

```
DEPOT-EXIT-GATE veh= tick= mf= next= dir= depotResv= selfRel= mfOwner= nextResv= nextOwner= nextMf= stuck=
```

- `next` = 库口下一格、`dir` = `GetRailDepotDirection()`
- `nextResv` = `HasReservedTracks(next, …)`（即 `:18913` 的原判据）
- `nextOwner`/`nextMf` = `GetTrainForWholeTileReservation(next)` 的归属车号与其车头所在格
- 只在 `depotResv || nextResv` 时打，频率 ≤ 1 行 / 37 tick

**复测判据**：出现 `DEPOT-EXIT-GATE ... nextResv=1` 即坐实 `:18913`；再看 `nextOwner` 决定走"自清旧预留"还是"配对让位"。

**构建自证**：`R3R_fullbuild.cmd inc detach` → `build\R3R_incbuild.done` = `EXIT_CODE=0`（2026-11-08 02:03:22）；`[3/3] Linking CXX executable openttd.exe`；错误计数 0；`src\train_cmd.cpp` 02:01:20 → `build\openttd.exe` **02:03:14（52 488 192 B）**；`findstr` 命中 `DEPOT-EXIT-GATE`；`read_lints` 0 条。

## 九、第 399 轮复测：`nextOwner=21` 坐实（2026-11-08，探针 R-398-1 命中）

**采样有效性**：`build\R3R_debug.log` 88 335 B / **753 行** / mtime 2026-11-08 02:07:32；产生者 `build\openttd.exe` 2026-11-08 02:03:14（52 488 192 B）= **R-398-1 那一版**。⇒ 有效，本段读数可直接定型修法。

**探针读数（逐字，40 条同形，`:594` 起）**：

```
DEPOT-EXIT-GATE veh=27 tick=2195 mf=279,126 next=280,126 dir=2 depotResv=1 selfRel=1
                 mfOwner=27 nextResv=1 nextOwner=21 nextMf=285,127 stuck=0
DEPOT-EXIT-GATE veh=27 tick=2213 ... nextResv=1 nextOwner=21 nextMf=285,127 stuck=1
（此后 tick=2232/2250/2269/2287/2306/2324/.../2916 全部逐字同形）
```

- 40 条 `DEPOT-EXIT-GATE`（`:594`~`:751`），**`nextResv=1` 恒 1、`nextOwner=21` 恒 21**，无一条为 `-1`、也无一条为 `27`。
- `depotResv=1 selfRel=1 mfOwner=27` ⇒ R-397-1 的库格豁免持续生效（否则 `:18907` 先拦）。
- `mf=279,126 next=280,126 dir=2` ⇒ 库口朝向 DIAGDIR_SE 的反向邻格，`next` 就是 `:18913` 检查的那一格。
- **结论（已实证）**：`exit=12` 的拒绝点 = `:18913`（库口下一格 `HasReservedTracks`），阻塞者是 **veh=21**，不是本车残留（排除 R-398-1 方案 A「自清旧预留」与方案 C「无主残留」）。

**veh=21 的身份（逐字，`:619` / `:634`）**：

```
[R3R] CPL-PAIRMAP tick=2312 role=target v=21 vHead=21 vTile=285,127 vSpd=0 vOrd=17
      vStop=0 vFront=1 vLen=20 vPairT=1048575 vPairR=27
[R3R] SVC-DEPOT-SKIP veh=21 cur=17 real=3(17) spd=0 tile=285,127 tag=couple-protocol
```

- `role=target`、`vPairR=27` ⇒ veh=21 正是 veh=27（库内机车）的**挂接配对目标**，不是无关的挡道车；`vLen=20` 约 20 节车底。
- `vOrd=17` = `OT_WAIT_COUPLE`、`vSpd=0` ⇒ 它已停在等待点静止等人来挂，按设计此时**不应持有任何路径预留**。
- `nextMf=285,127` = 它自己的车头格，与 `vTile` 一致 ⇒ 预留的**起点**在 285,127，却沿路覆盖到 280,126（库口下一格，相距 5 格）。

**疑点（REG-399-A，待验证）**：veh=21 停在等待点（`spd=0` + `OT_WAIT_COUPLE`）却仍持有覆盖库口下一格的路径预留。第 127 轮 `RESV-NOBOOK` 门禁只拦「**新订**路线」，本身不释放**已订**预留；若 veh=21 是在「驶向等待点的途中」订下这条路径、到位停稳进入 `OT_WAIT_COUPLE` 后没有归还尾段，则残留的路径尾部就永久卡住库口。本轮日志中 `RESV-NOBOOK` 未见（`:200` 那条 reason=`depot-paired` 属早期），无法从现有数据区分"未清"与"门禁未覆盖（`285,127` 非 Station 格 ⇒ 门禁不生效）"两种来源。

**猜测根因（待验证）**：`R3RTryPathReserveCore` 的库口格判据 `:18913` 是**不区分归属的无条件拒绝**——只要库口下一格有任何预留就返回 `TPRRF_NONE`。当该预留的持有者恰好是**本车的挂接目标**（`CPL-PAIRMAP` 里 `vPairR=27`）时，二者互等：库内车要出库才能靠近目标，目标的残留预留又挡住出库口 ⇒ 与 KI-567 的"A 等 B、B 等 A"同构，只是这次配对关系在日志里有明文（`role=target` + `vPairR`）。**待验证判据**：能证明 280,126 这笔预留确实属于 veh=21 的**旧路径残留**（而非它当前合法预订的出站路线）。

**修法候选（按优先级，均待拍板）**：

- **R-399-1（首选·治因）**：让"停稳的挂接目标"归还路径预留——在 `OT_WAIT_COUPLE` 且 `cur_speed==0` 时释放其路径尾部（保留脚下格与其所在整格保护位，同 KI-207c/第 126 轮 `RESV-GHOST` 的口径：只放不属于"整格等待区"的部分）。要求在站外（非 Station 格）同样生效，弥补第 127 轮 `RESV-NOBOOK` 只覆盖站台格且只管新订的缺口。
- **R-399-2（兜底·治象）**：库口格判据加**自有配对豁免**——若 `:18913` 的持有者 `GetTrainForWholeTileReservation(next)` 是 `R3RGetPairedTarget(consist)`（即本车的挂接目标），视为不阻塞并放行（沿用 R-397-1「格上停着车即主人」的豁免风格）。风险：放行后两车在同一占用面交汇，须确认不会立即触发 `CheckTrainCollision` 回滚成另一种死循环。
- **R-399-3（最弱）**：让配对目标在 `OT_WAIT_COUPLE` 停稳时主动退到不占库口的位置；属调度层，代价最大，仅在前两者都不成立时考虑。

**本轮未做**：未改任何判定逻辑、未构建。下一步取数（1 组即可定型）：查 `280,126` / `285,127` 附近是否有 `RESV-PURGE` / `RESV-AUDIT-STRAY` / `RESV-GC` 行，以及 veh=21 最后一次 `CRT`/`TRP`（路径终点），用以区分 REG-399-A 的两种来源并据此选 R-399-1 或 R-399-2。

### 9.1 关键新证据：库口那 3 格是 **veh=27 自己**在挂接准备阶段订的（`:554`~`:575`，tick=2177）

```
:554 CPL-HEAD best=285,127 head=285,127 on=1
:555 CPL-BEST veh=27 tile=285,127 td=8 cost=6642
:556 CPL-BACK pPrev=285,127 td=8 parent=280,126 ptd=8
:557 CPL-BACK pPrev=280,126 td=8 parent=279,126 ptd=8
:562 CPL-FIRST-TD st=279,126 hops=2 yapf=8 at=280,126 tdb=0x101 ok=1 best=8 start=280,126 trk=0
:564 RP cur=280,126 td=8 depth=0 trk=0 bits=0x1
:567 RESV-SELF-DEPOT-MOUTH veh=27 tile=280,126 track=0 mouth=280,126 end_a=279,126 end_b=280,126
:568 RESV-SELF-AIR veh=27 tile=280,126 track=0 kind=rail
:570 CPL-RES-OK veh=27 total=3 booked=3 stop_safe=0 tiles=280,126/0;281,126/5;281,127/4
:572 CPL-RET veh=27 tick=2177 tile=280,126 enterdir=2 tracks=0x21 res=0x1 ret=0x1 why=cpl-partial-res spd=0 real=4
:573 RESV-END veh=27 tick=2177 from=279,126 endTile=285,127 endTd=8 endPos=106 curPos=0 endBits=0101 junction=0 real=4 spd=0
```

⇒ **同一次挂接准备（tick=2177）里，veh=27 自己用 `cpl-partial-res` 订下了 280,126/281,126/281,127 三格，其路径终点正是 285,127（= veh=21 所在格）。** 18 tick 后（`:593` tick=2195）`RESV-BEGIN veh=27 ... prevEnd=285,127 prevEndPos=106` 再次出库订路，`:18913` 就在 280,126 撞上这笔**自己订的**预留并返回 NONE。

**归属为何显示 21（REG-399-A 修正为「归属回溯误判」）**：280,126 是库口外的普通轨，没有车停着；`GetTrainForWholeTileReservation()` 沿预留回溯到路径终点 285,127 时，该格**正停着 veh=21**（格上有车即主人），于是把主人记成 21。也就是说 `nextOwner=21` 是**回溯算法的产物**，不代表 veh=21 真的持有 280,126；真正的订主是 veh=27 自己。

**⇒ 结论升级（已实证）**：`exit=12` 与 `exit=11` 是**同一族「自订预留自锁」**，只是锁在两个不同的格上：

| | 锁格 | 订主 | R-397-1/R-399-1 |
| --- | --- | --- | --- |
| `exit=11`（已修） | 库格 279,126（depot reservation） | 27 | `R3RDepotSelfReleased()` 已豁免 |
| `exit=12`（本处） | 库口下一格 280,126（plain rail） | **27**（`cpl-partial-res`，`:570`） | **尚无豁免** |

**修法定型（R-399-1′，与 R-397-1 对称，最小改动）**：`R3RTryPathReserveCore` 的 `:18913` 在 `next_resv` 为真时**先求归属再决定**——若归属为本车（`next_owner == consist`，或用 R3R 自己的 `cpl-partial-res` 记账/`R3RDepotSelfReleased` 同族记账证实"本车近期在该库口订过位"），则不 `return TPRRF_NONE`，继续走 `FollowTrainReservation` 由它按正常路径语义处理自有预留。仅在归属为**第三方且非配对目标**时保持拒绝。

**风险与待验**：放行后 `FollowTrainReservation` 见到自己订的 3 格部分预留是否会正确消费/续接（而非重复预订或断言）；以及 `cpl-partial-res` 这三格在出库路径接手后是否被正确记账继承（避免变成无主孤儿）。⇒ 落地前需读 `:18890-18940` 与 `cpl-partial-res` 的记账/释放点。

### 9.2 落地：R-399-1′（仅 `src\train_cmd.cpp`，未碰 `src\*.h`）

**为什么不用 `nextOwner` 直接判**：现场 `nextOwner=21` 是 `GetTrainForWholeTileReservation()` 沿预留回溯到终点格 285,127、把停在那里的等待车底当主人的**误判**，改用 `owner == consist` 会判成"别人的"从而继续拦；改用 `owner == 配对目标` 又会在真第三方挡道时误放。⇒ 用**订主侧记账**（谁订谁记），不用归属侧猜测。

三处改动：

1. **新增记账（插在 `R3RMarkDepotSelfRelease` 之后，`R3RDepotSelfReleased` 之前）**
   - `struct R3RCouplePartialReserveRecord { TileIndex mf_tile; uint32_t tick; bool logged; }`
   - `static std::unordered_map<uint32_t, R3RCouplePartialReserveRecord> _r3r_couple_partial_reserve`，有效期 `R3R_COUPLE_PARTIAL_RESERVE_TICKS = 512`（车在库里每 37 tick 重试一次，512 tick 足够覆盖十几次重试）。
   - `R3RMarkCouplePartialReserve(const Train *v)`：以 `v->GetMovingFront()->tile` 为键存一笔。
   - `R3RCouplePartialReserved(v, mf_tile, next_tile)`：命中条件 = 同车 + 未超时 + **车头格未变**；命中后打一次性探针 `DEPOT-EXIT-SELF-PARTIAL veh= mf= next= why=cpl-partial-res`。

2. **记账写入点**：`cpl-partial-res` 出口（原 `:18002`）在 `return r3r_chosen_ret(..., "cpl-partial-res")` 之前调 `R3RMarkCouplePartialReserve(consist)`。

3. **判据放宽**：库口闸门（原 `:18936`）由 `else if (r3r_next_resv)` 改为 `else if (r3r_next_resv && !R3RCouplePartialReserved(consist, moving_front->tile, r3r_next_tile))`。**只在"本车自己刚保留过半截路径且车头没动过"时放行**，第三方预留与超时记录仍照旧 `return TPRRF_NONE`。

**为什么不改 `:18913` 的 `HasReservedTracks` 本身、也不动 `cpl-partial-res` 的保留语义**：保留半截预留是 KI-563 有意为之（防止机车到挂接面前丢预留），撤掉会让车在挂接面之前失去路径；本修复只解决"下一 tick 从库里重订时被自己挡"，两侧语义都不动。

**待复测判据（4 条）**：

1. 同场景（veh=27 库内、veh=21 等待）出现 `DEPOT-EXIT-SELF-PARTIAL veh=27 mf=279,126 next=280,126 why=cpl-partial-res`，且随后 `DEPOT-EXIT-GATE` 的 `nextResv=1` 不再导致出库卡死（`mf` 离开 279,126）。
2. `TPR-EXIT ... ok=1` / `RESV-END ... endTile=` 之后机车**真的驶出库格**（`LOCO-MOVE` 或坐标变化），不再每 37 tick 重复 40 次同形读数。
3. 负判据·第三方占库口：另一列车合法停在库口外的预留上时，本车仍在库里等待（`DEPOT-EXIT-SELF-PARTIAL` 不出现、`exit=12` 行为不变）。
4. 负判据·超时失效：本车 `cpl-partial-res` 后 512 tick 内没有任何出库尝试，再尝试时记账应已过期（走拒绝路径，不会误放）。

**未做**：未改 `REG-399-A` 的归属回溯算法（`GetTrainForWholeTileReservation` 的"终点格有车即主人"误判仍是既有行为，只在本修复里绕开）；未处理"cpl-partial-res 三格在出库路径接手后是否留成孤儿"（待复测看 `RESV-AUDIT-STRAY`）。

**构建自证（R-399-1′）**：`R3R_fullbuild.cmd inc detach` → `build\R3R_incbuild.done` = `EXIT_CODE=0`（2026-11-08 **02:14:28**）；日志尾部 `[2/3] Building CXX object ...train_cmd.cpp.obj` → `[3/3] Linking CXX executable openttd.exe`；`src\train_cmd.cpp` 02:12:07 → `build\openttd.exe` **02:14:19（52 488 192 B）**；`findstr DEPOT-EXIT-SELF-PARTIAL build\openttd.exe` = **1 命中**；`read_lints` 0 条。状态＝已实现 + 已编译，游戏内复测待做。

## 十、与"另一个平行问题"（KI-563 逐步铺设）的关系复核（2026-11-08）

玩家指出昨天是**两个平行问题**：①库内出库自锁（KI-567 / exit=11、exit=12，本轮已修）；②挂接寻路把预留从"一次性铺到底"改"逐步铺设"时出现的问题（KI-563）。本节把②的现状与①的接口对齐，避免下轮重复取证。

### 10.1 KI-563 三轮尝试史（结论只认清单与代码，逐条可核）

| 轮次 | 做法 | 结果 |
| --- | --- | --- |
| 第 393 轮 C-3=A | 只改 `R3RReservePathSequence()` 的**失败分支**（失败时保留已订格，不再整批回滚） | 与"越灯"**正交**（成功路径仍一次订满），复测必然失败 |
| 第 394 轮首版 | 循环内按"格上有信号"截断，**PBS 与 block 一律截** | 玩家报 `veh0`/`veh3` **寻路紊乱**：该线 PBS 密度 1~2 格一座 ⇒ 每 tick 只订 1~2 格，`booked` 追不上 YAPF 代价估计 ⇒ **已撤回**（α/β/γ 三候选作废） |
| 第 395 轮修法 D | 改判据对象：订成功后用 `IsSafeWaitingPosition()` 判定"第一个安全停车位"即停 | 正判据达成（`CPL-RES-SAFE-STOP` 7 次、`CPL-RES-SIGNAL-STOP` 0 次）但**判据对象错** |
| 第 396 轮 | 复测修法 D | **证伪**：`veh=0` 会合点 `248,133`（`CPL-BEST`），预留却在 `253,121`（`sig=0` 普通直轨）收工，`booked=2`，**差 13 格**，7 次重试逐字同形 ⇒ KI-563 退回「未修」 |

### 10.2 回归风险（REG-399-B，新登记）：修法 D 的代码**仍在树里且仍生效**

`src\pathfinder\yapf\yapf_rail.cpp` 里 `stopped_at_safe` / `IsSafeWaitingPosition(...)` / `CPL-RES-SAFE-STOP` 三处**都还在**（`：636-638` 取 `r3r_coupler`、`:729-737` 判定并打日志）。⇒ 清单里 KI-563 写作"退回未修"，但**运行时行为仍是修法 D**：挂接预留会在"第一个安全停车位"提前收工（现场即 `booked=2/15`、离会合点 13 格）。这不是本轮引入的，但属**文档与代码不一致**，且正是"改逐步铺设时出现的问题"的直接残留，必须由玩家拍板去留。

### 10.3 与本轮 R-399-1′ 的接口边界（确认无交叉污染）

两者同处 `R3RReservePathSequence()`，但改的是两条互斥的路：

- **KI-563 / 修法 D**：管"**订到哪停止**"（成功路径，`booked` 的上限）。
- **第 393 轮 C-3=A / 本轮 exit=12**：管"**失败时怎么办**"（`cpl-partial-res` 保留已订格 + `return true`）。

本轮 `train_cmd.cpp` 的三处改动（记账 + 两处判据）**没有触碰 `yapf_rail.cpp` 一个字符**，因此不会改变修法 D 的截断点；反过来，修法 D 若被回退，"保留半截"分支的触发频率会变化，但本轮记账是按"发生过 `cpl-partial-res`"记的，逻辑上不依赖截断点在哪。

### 10.4 待拍板（下轮开工前请给编号回复）

- **Q-399-B1**：修法 D 的代码先**回退**（回到"订满全程、只在冲突处停"）还是**保留待新修法**？
  - 回退理由：已被证伪、且会让预留停在离会合点十几格的普通直轨上；
  - 保留理由：它至少把"1 tick 铺满全线"压住了，回退会让"越过所有信号铺到底"立刻复发。
- **Q-399-B2**（**2026-11-08 玩家口径已把选项收窄，见 §10.5**）：预留终点该由谁决定？
  - **(a) 不截断（现结论）**：沿 `steps` 一订到底，只在"会合点"或"第一个订不到的格"停下 —— 即原生 `TryPathReserve()` 语义，也是玩家原话"寻路直接寻到终点"的落地形态；
  - **(b) 搜索层分段（另立条目，本轮不做）**：若真要"每拍只铺到下一个安全位"，必须把**寻路目标**改成"下一个安全停车位"（搜索出短路径、到点再续搜）；在**预留层**截断必然造成"路径长 / 占位短 / 代价估计不一致"，这正是第 394 轮 `veh0` 寻路紊乱的成因；
  - **(c) 只处理"越灯"**：若非 PBS 信号必须挡住预留，应由**搜索层按闭塞语义**限制（或在条目里明确承认"长路径越灯"是已知代价），不在预留层猜边界。

**本轮未做**：未改 `yapf_rail.cpp`、未回退修法 D、未构建。文档同步：`R3R_KNOWN_ISSUES.md` 的 KI-563 标题下已加"代码现状（REG-399-B）"一条。

### 10.5 玩家口径纠正（2026-11-08）：预留与寻路是两层，截断只能发生在"订不到"

> ⚠ **本节的"修法 D 层次错"论断已被 §10.6 推翻**（2026-11-08 玩家二次纠正：普通寻路也是"插旗到安全位"）。保留本节仅为记录当时的推理路径，结论以 §10.6 为准。

**玩家原话**：「预留肯定是每次延申到下一个安全停车位的，但是，寻路不应该是直接寻到终点吗」

这句话把"逐步铺设"这个词的歧义点破了，**结论是玩家对、前几轮（含修法 D）把层次搞错了**：

| 层 | 归谁管 | 事实 |
| --- | --- | --- |
| **搜索层**（YAPF 摸路） | `FindNearestCoupleTrain` → `r3r_resv_path`（`yapf_rail.cpp:1706`） | **一次算到会合点**，路径完整、不分段；`CPL-BEST` 给出的就是终点。玩家说的"直接寻到终点"= 这一层，本来就是对的。 |
| **预留层**（逐格占位） | `R3RReservePathSequence()`（`yapf_rail.cpp:623`） | 对**已算好的整条路径**做 `ReserveSingleTrack()` 循环，`++booked`（`:699`）。它天然是循环，但循环 ≠ 逐步推进；真正的停止条件只有「订不到」。 |

⇒ 原生 PBS 之所以看起来"每拍到下一个安全位就停"，**不是因为预留层在安全位截断，而是因为它的路径终点本来就在下一个安全位**（信号 aspect 决定搜索长度）。R3R 挂接的路径终点在几十格外的会合点，所以同一条路径一订就是几十格 —— 这才是"越灯"的来源。

**修法 D 的层次错（已证伪，可逐条对上）**：它在**预留层**用 `IsSafeWaitingPosition()` 伪造"路径很短"的效果（`:729-742` 的 `break`），但**搜索层的结果仍是长路径** ⇒ 路径长、占位短、YAPF 代价估计按长路径算 ⇒ 三者不一致，于是出现 `booked=2/15`、预留停在离会合点 13 格普通直轨（`253,121` vs `248,133`）的怪状态。第 394 轮按"格上有信号"截断（PBS 也截）是同一个层次错的另一种写法，只是截得更碎，所以直接表现为寻路紊乱。

**据此收窄的结论（写入 Q-399-B2 选项，等玩家编号）**：

1. **预留层不做任何"安全位 / 信号"截断**：回到"沿路径订到会合点，或第一个订不到的格为止"（原生 `TryPathReserve` 语义）。`IsSafeWaitingPosition` 的 `break` 属修法 D，应回退。
2. **"每拍只铺到下一个安全位"若真要做，只能改搜索层**：把挂接寻路的目标从"会合点"改成"下一个安全停车位"，到点后重新搜索续订。这是在 `FindNearestCoupleTrain` 里换目标，不是在 `R3RReservePathSequence` 里加 `break`；风险面（候选唯一性、`CPL-BEST` 语义、重试节流）远大于本轮收益，故列为 (b) 且不建议本轮动。
3. **"越灯"另案定性**：是否允许越过非 PBS 信号，属闭塞语义，应在搜索层表达（或明确承认长路径越灯为已知代价），不得在预留层猜。**本条不再当作 KI-563 的必要条件**——KI-563 的现场形态（离会合点 13 格早停、挂不上）与"越灯"无关，是修法 D 自己造的。

**待玩家一句话确认**：是否按上面第 1 条办（= 回退修法 D，预留回到"订到会合点 / 订不到为止"，"逐步"留作搜索层以后的事）。若确认，下轮只改 `yapf_rail.cpp` 一处循环，不动 `train_cmd.cpp`。

### 10.6 二次口径纠正（2026-11-08 玩家）：普通寻路 = 搜到命令终点 + 插旗到安全位，挂接应完全同构

**玩家原话**：「普通寻路的导航终点就是一次性到命令终点，绝对不是什么路上的红绿灯。而挂接寻路，应当是一样的。相对应的，普通寻路是插旗子到安全停车位，所以挂接寻路也是插旗子到安全停车位」

⇒ **§10.5 的"层次错"论断作废**（它把"插旗只到安全位"错算成搜索层的事）。正确模型是两件事并存：

| 环节 | 普通寻路（GOTO_STATION 等） | 挂接寻路（GOTO_COUPLE） |
| --- | --- | --- |
| **搜索终点** | 订单终点，**一次搜到**，跨多少个信号都不管 | 会合点，**一次搜到**（`CPL-BEST` → `r3r_resv_path`，`yapf_rail.cpp:1706`） |
| **插旗（预留）** | 只插到**下一个安全停车位**（`IsSafeWaitingPosition`；本文件也有 `FindSafePositionProc`，`yapf_rail.cpp:162`） | `R3RReservePathSequence()` 沿 `steps` 循环（`:623`、`++booked` `:699`） |
| **谁负责续旗** | **列车运动本身**：每前进一格就 `ExtendTrainReservation` 续一段 ⇒ 任意时刻只留一小段预留，列车照样能开到终点 | **没有**。摸路只在 `TrainCoupleHandler` 命中流程里跑一次，一次摸路配一次提交，不存在"第二段" |

⇒ 结论：**"止于安全位"是对的（玩家口径正确），修法 D 只做了前半句**。它确实让插旗停在了安全位，但这个预留**没有任何续旗机制**，于是永久停在 `booked=2`，离会合点 13 格。上一轮写的"路径长 / 占位短 / 代价估计不一致"只是现象描述，**不是根因**。

**待验证（下一轮主攻，未取证不下结论）**：现场 `veh=0` 7 次重试 `booked=2/15` 逐字同形 ⇒ 机车没有前进。两种可能必须分清：

- **H1**：机车根本没被下达"往前开"的指令（挂接命中流程只做命中判定 + 预留，行驶仍靠 `TrainLocoHandler` 的移动分支），所以起点不变、重摸逐字同形；
- **H2**：机车动了，但在 `253,121` 被收缩后的预留终点钉住，下一 tick 重摸仍从原地起。

判 H1/H2 的取数：比 7 次 `CPL-ENTRY` / `CPL-ORIGIN` / `CRT veh=0 ... found=` 里的**起点坐标是否相同**，以及有无 `LOCO-MOVE`（第 242 轮加的移动分支探针，`train_cmd.cpp`）。

**新修法方向（待玩家拍板后才动代码，Q-399-B2 旧三选项作废）**：

- **(A) 止于安全位 + 每 tick 续旗**：挂接摸路在每次重试时重新跑、起点随列车前进而变 ⇒ 天然"逐步"，最贴近原生；前提是 H1 为假。
- **(B) 止于安全位 + 把整条 `steps` 记为账**：每 tick 续订下一段到下一个安全位（需新状态，改动大）。
- **(C) 若 H1 为真**：真正该修的是"预留只订 2 格时机车为什么不前进"（是否 `SetReservationTarget` 收缩把列车钉死），与截断点无关。

### 10.7 玩家新现场（2026-11-08，纯净场景）：续旗正常，止于"终点前最后一个信号"

**玩家原话**：「我跑了一个比较纯净的有关场景，我似乎发现，续旗子是正常的，直到到终点前的最后一个信号灯」

这条把 KI-563 的现场形态从"预留根本不延伸（`booked=2/15`）"改写为"预留一路延伸到终点前最后一个信号为止"。两者**不矛盾**，是同一段代码在不同信号分布下的两种表现：

| 场景 | 路径上信号分布 | 修法 D 的实际行为 |
| --- | --- | --- |
| 第 396 轮现场（`253,121`，15 格） | 中间信号密集 | 第一个 PBS 信号就把 `IsSafeWaitingPosition` 判真 ⇒ `booked=2` 立即收工 |
| 本轮纯净场景 | 中间无信号，只有终点前一个 | 一路没有安全位可停 ⇒ 一直铺到最后一个信号才 `break` ⇒ 看起来"续旗正常，只差最后一格" |

⇒ **修法 D 的真实行为 = "预留到第一个安全位为止"**。这与 §10.6 表里"插旗"那一行的原生规则一致，**规则本身没错**；错的是**最后一个安全位之后没有续旗**，而挂接必须越过它贴住车底（会合点在该信号之后）。

⇒ 修法方向收窄为「**终段例外**」：安全位截断只允许发生在路径中段；若截断点之后剩余的 `steps` 已属终点段（截断点到会合点之间不存在下一个安全位），或剩余格数 ≤ 阈值，则必须继续订到会合点。这样既保留"逐段插旗"，又不至于差最后一格挂不上。

**待验证（日志锚点待补，禁止先下结论）**：

- **V-1**：纯净场景 `CPL-BEST`（会合点 tile）与 `CPL-RES-SAFE-STOP` / `CPL-RESERVE` 的 tile 差值应为 1~2 格（若差值大，说明另有原因）。
- **V-2**：截断点那个信号的类型（PBS / block）与相对位置（是否紧邻会合点最后一格）。
- **V-3**：截断后是否伴随 `COUPLE-FAIL`、机车 `Stuck`，或下一 tick 是否有新的 `CPL-ENTRY`（起点是否前进）。

### 10.8 取证（2026-11-08 纯净场景，`build\R3R_debug.log` 1376 行 / 144 852 B / 02:33:53）

**读数（行号锚点）**：

| 项 | 实测 |
| --- | --- |
| 重试轮次 | `CPL-ENTRY` 7 次（`:480 :672 :742 :811 :911 :1006 :1092`），`CPL-RES-OK` 7 次，`COUPLE-FAIL` 1 次，`COUPLE-OK` 1 次（`:82`，属场景早段的另一次挂接） |
| 起点 | **逐轮西移**：`258,121` → `256,121` → `256,121` → `255,121` → `254,121` → `253,122` → `253,122` |
| 会合点 `CPL-BEST` | 恒 `248,133 td=1`（`:495 :687 :757 :826 :923 :1017 :1103`），路径 `total=15`（后两轮 14 / 12） |
| 截断点 `CPL-RES-SAFE-STOP` | 恒在**起点前方 1~2 格**：`:543 tile=253,121 nth=1/15 booked=2 sig=0`、`:1061 tile=253,121 nth=0/14 booked=1 sig=0`、`:1135 tile=252,122 nth=0/12 booked=1 sig=0` |

⇒ **V-1/V-2 答复**：截断点不是"终点前最后一个信号"，而是**起点前方第 1~2 格**，且探针 `sig=0`（该格 `HasSignalOnTrack` 为假）。⇒ 截断**不是**信号判据造成的。

⇒ **关键事实**：机车**一直在前进**（`258,121` → `253,122`），说明"止于安全位 + 下一 tick 重摸续旗"这条链**是通的**（§10.6 的 (A) 形态成立，H1/H2 都排除）。问题只在于**每轮只能推进 1~2 格**。

**猜测根因（待验证，勿当结论）**：`IsSafeWaitingPosition()`（`src\pbs.cpp:2725-2743`）只有三个 `return true` 门：depot（`:2727`）、block 信号（`:2730`）、以及

```2740:2743:src/pbs.cpp
	/* End of track? */
	if (!ft.Follow(tile, trackdir)) {
		/* Last tile of a terminus station is a safe position. */
		if (include_line_end) return true;
	}
```

调用方传 `include_line_end = true`（`yapf_rail.cpp:729`）。现场 `sig=0` 且非 depot ⇒ 命中的只能是**这个 line-end 分支**。即：`CFollowTrackRail::Follow()` 在"从该格沿该 trackdir 出发"这一步失败，被判成"线路到头 = 可以安全停车"，于是一格或两格就收手。

但 `CPL-BEST` 明明能算出 15 格到 `248,133`（YAPF 沿同一批 trackdir 走过），所以 `Follow` 失败**不代表真的没路**——要么是站台格跳跃（`IsCompatibleTrainStationTile`）在这一步返回 false，要么是该 trackdir 与格上轨道组合不匹配。这条一旦证实，修法 D 的判据就属"用了一把不属于挂接路径的尺子"。

**下一步（二选一，等玩家给方向）**：
- **P-1**：打印 `ft.Follow` 失败的具体原因（`fi` 的 `okay/err/cmd/新格`）到 `CPL-RES-SAFE-STOP` 行，一轮就能定性；
- **P-2**：若确认是 line-end 误判，修法 D 的调用改传 `include_line_end = false`（挂接从不以"线路到头"为终点），信号/depot 两个门保留。

### 10.9 玩家新读数（2026-11-08）：预留止于 `248,127`/`248,128`，会合点在 `248,130`/`248,133`

**玩家原话**：「根据我的观察，预留截止于(248,127),(248,128)就没有了，而预计的汇合点是在(248,130)或(248,133)，但是(248,133)再过到134就没有铁轨了。但是，总之，你说的预留截断点和汇合点相隔17格是肯定不成立的」

**这条直接推翻 §10.8 的坐标读数**，登记为疑点 **REG-399-C**：

| 项 | §10.8 我读的（`build\R3R_debug.log` 1376 行 / 02:33:53） | 玩家这次目视 |
| --- | --- | --- |
| 预留止于 | `253,121` / `252,122` | **`248,127` / `248,128`** |
| 会合点 | `248,133` | `248,130` 或 `248,133` |
| 差距 | 曼哈顿 17 格（我上轮写"13~17 格"） | **2~6 格** |

⇒ 两种可能，必须先分清，**不得再用 253,121 那套读数下任何结论**：
- **C-a**：这份日志不是玩家这次的纯净场景（玩家在 02:33 之后又跑过、日志被覆盖 / 或玩家看的是另一次尝试）；
- **C-b**：日志确是本场景，但 `CPL-RES-SAFE-STOP` 打的不是"最终预留终点"（截断后又被他处续订了），即我的读数口径错了。

**玩家补充的几何事实（高价值）**：`(248,133)` 再往 `(248,134)` **没有铁轨** ⇒ 会合点所在格就是**线路末端（terminus）**。这正好落回 `IsSafeWaitingPosition()` 的第三个门（`src\pbs.cpp:2740-2743`）：

```2740:2743:src/pbs.cpp
	/* End of track? */
	if (!ft.Follow(tile, trackdir)) {
		/* Last tile of a terminus station is a safe position. */
		if (include_line_end) return true;
	}
```

⇒ 若线路在 `248,133` 到头，那么从 `248,127`/`248,128` 出发的 `Follow()` 若失败（站台跳跃 / 到末端），就会被判成"安全位"而截断 —— 与玩家第一句"直到终点前最后一个信号灯"互为印证（该处同时还有一个信号）。

**待验证（下轮取数，先取证再动码）**：
- **V-4**：日志 mtime / 大小是否已不是 02:33:53 那份（判 C-a）。
- **V-5**：`248,127`/`248,128` 那格的信号类型（PBS / block / 双向）与 `HasSignalOnTrackdir` 结果，以及它到 `248,133` 之间的剩余格数。
- **V-6**：`248,133` 是否 `IsRailStationTile` / 轨道末端（`!ft.Follow` 为真）—— 决定是否命中 line-end 门。
- **V-7**：`CPL-RES-OK` 的 `booked` 与 `tiles=` 列出的最后一格，是否就是 `248,127`/`248,128`（判 C-b）。

### 10.10 取证收口（2026-11-08）：**玩家对，我错** —— 真实预留终点 = `248,127`，不是 `253,121`

**日志已换**：`build\R3R_debug.log` 现在 **2865 行 / 302 684 B / 02:38:48**（我 §10.8 读的是 1376 行 / 144 852 B / 02:33:53，被追加覆盖）。`build\openttd.exe` = 52 488 192 B / 02:14:19。⇒ **REG-399-C 判为 C-b：我拿内部截断探针的坐标当成了地图上的预留末端**，`253,121` 那套"差 13~17 格"的读数作废。

**决定性锚点（新日志，行号）**：

| 行 | 原文 | 含义 |
| --- | --- | --- |
| `:240` | `RESV-TERM veh=0 real=2 type=1 tile=248,127 td=1 tcount=1 saf…` | **真正的预留终点 = `248,127`**，与会合点 `248,133` 差 6 格 |
| `:239` | `RESV-SIGPASS veh=0 tile=248,127 track=1 dir=1 doResIn=1 spd=` | `248,127` **有信号**（玩家说的"终点前最后一个信号灯"），且允许预留进入 |
| `:517`/`:539` | `REACH cur=248,127 td=1 depth=13 trk=1 bits=0x2` / `RP cur=248,127 ...` | YAPF 摸路（depth=13）与 `rp()` 订旗都恰好收在 `248,127` |
| `:495` | `CPL-BEST tile=248,133` | 会合点 = `248,133`（玩家说"或 248,130"，取 `248,133`） |
| `:543` | `CPL-RES-SAFE-STOP tile=253,121 nth=1/15 sig=0 booked=2` | **这只是 `R3RReservePathSequence()` 内部的二次截断探针**，不是地图预留末端；14 次重试里它恒为 `253,121`/`252,122` |

**几何事实（玩家）**：`248,133` → `248,134` **没有铁轨** ⇒ 会合点所在格即**线路末端（terminus）**。

**结论（修正 §10.6）**：整条链是这样的——

1. `rp()` 摸路订旗，一路订到 `248,127`，因为 `248,127` 是有信号的**安全停车位**（`IsSafeWaitingPosition` 判真 ⇒ `RESV-TERM saf=1`）；
2. `248,127` → `248,133` 这 6 格**没人订**（会合点就在末端，不需要"安全停车位"）；
3. 机车最多开到 `248,128`，离会合点还差 5 格 ⇒ 无路可走 / 挂不上；
4. `R3RReservePathSequence()` 的 `253,121`/`booked=2` 是另一条更早的截断（起点前 2 格），把问题进一步放大，但**不是唯一元凶**。

⇒ 与玩家口径完全一致：**插旗到安全位是对的，但"最后一个安全位"之后必须继续订到会合点**——终点不需要满足"安全停车位"（列车到达即结束，不在此等待让行）。

**修法（待玩家确认，只改 `yapf_rail.cpp`）**：`R3RReservePathSequence()` 的截断判断加**终点例外**——当 `i` 已是 `steps` 的最后一段（截断点之后到会合点之间不再有下一个安全位）时，不再 `break`，继续把剩余格订满；即"安全位截断只作用于路径中段"。同时 `rp()` 一侧的 `RESV-TERM` 也应按同一规则放到会合点。

**待验证（下轮取数）**：`248,127` 的信号是 PBS 还是 block（决定走 `:2730` 还是 `:2745+` 的门）、`248,128`～`248,133` 是否为站台格。

### 10.14 第 400 轮：终点例外上线后复测**仍然不通**（玩家「见日志，问题依然存在」）

**采样**：`build\R3R_debug.log` 1 689 行 / 179 148 B / 02:56:23，产生者 `build\openttd.exe` 02:52:23（= 第 399 轮终点例外版）⇒ 采样对新 exe 有效。

**读数 1：`CPL-RES-TERM-EXEMPT` 全日志 0 行** ⇒ 例外**一次都没触发**，`i == r3r_last_safe` 从未成立。

**读数 2：截断点全在起点附近且 `sig=0`**（`:663`、`:847`、`:917`、`:986`、`:1089`、`:1172`、`:1246`）：

```
663: CPL-RES-SAFE-STOP veh=0 nth=1/15 tile=253,121 td=3 sig=0 booked=2
1172: CPL-RES-SAFE-STOP veh=0 nth=0/14 tile=253,121 td=3 sig=0 booked=1
1246: CPL-RES-SAFE-STOP veh=0 nth=0/12 tile=252,122 td=3 sig=0 booked=1
```

⇒ 截断**不是**在 `248,127` 那个有信号的安全位，而在 `253,121`/`252,122` 这种 `sig=0` 的格。与第 399 轮续取证结论一致：命中 `IsSafeWaitingPosition()` 的 **line-end 分支**（`pbs.cpp:2740-2743`，`CFollowTrackRail::Follow()` 在该格失败即判"线路到头=安全位"）。

**读数 3（本轮新事实，关键）：摸路本身只给 2 格**（`:1531`）：

```
1520: CPL-BEST veh=0 tile=248,133 td=1 cost=6100
1531: CPL-RES-OK veh=0 total=2 booked=2 stop_safe=0 term_exempt=0 tiles=248,128/1;248,129/1
```

⇒ `R3RReservePathSequence()` 收到的 `steps` 只有 `248,128`、`248,129` 两格（`total=2`，`booked=2` 已订满、`stop_safe=0`），而 `CPL-BEST` 给出的会合点是 `248,133`。**这条 `steps` 根本不含会合点**，所以"订满"也无济于事 —— 终点例外再正确也救不了。

**疑点 REG-400-A（必须先定性）**：同一 tick 里 `CPL-BEST tile=248,133`（摸路认出的会合点）与 `r3r_resv_path` 只有 2 格（到 `248,129` 为止）自相矛盾。可能的成因：
1. `r3r_resv_path` 的填充路径与 `CPL-BEST` 的 `best_*` 不是同一份摸路结果（前者来自 `rp()`/`build_path` 回溯，后者来自 `reach()` 判据）；
2. 回溯在 `248,129` 断开（`parent` 链断/被 `rp()` 的失败回滚 `resize(r3r_mark)` 截断）；
3. 起点 `248,128` 之后路径需要跨站台跳段（`R3RSkippedStationStretchContains`）而回溯没补上。

**疑点 REG-400-B**：预扫描把 `253,121` 之后的某格也判成安全位（否则 `r3r_last_safe` 应等于 `253,121` 的下标、在 `nth=1` 处就该豁免）。即 line-end 误判在路径中段**多处**出现，`i == r3r_last_safe` 这个"只认最后一个"的窄条件会被中间的假安全位顶掉。⇒ 终点例外的判据应改为"**截断点之后到会合点之间不存在真正的安全位（有信号/车库）**"，而不是"i 是最后一个 IsSafeWaitingPosition 为真的下标"。

**结论（暂定）**：本轮修复方向正确但**不充分**，需先解 REG-400-A（steps 不含会合点）；在 A 未解前，B 的判据收窄也无法让机车开到会合点。

#### 10.14.1 逐行读数（`build\R3R_debug.log:1505-1533`，同一次摸路，起点 248,127）

```
1506: TPR-WALK veh=0 mf=248,127 origin=248,127 td=1 okay=1 exit=1 new=248,128 reach=2
1508: RESV-SIGPASS veh=0 tile=248,128 track=1 dir=1 doResIn=1 spd=0 reserved=0 ownRes=0
1509: CPL-ENTRY veh=0 tile=248,127 orderType=16 dontReserve=0
1511: CPL-ORIGIN-TD veh=0 tile=248,127 tdb=0x202 tbits=0x2
1512: CPL-ORIGIN-FOLLOW veh=0 td=1 trk=1 ok=0 nt=-1,-1 nbits=0x2 st=0 skip=0
1514: PFD-ACCEPT kind=car-only tile=248,133 t=3 ordType=17 wc=1
1517: CPL-PATHFOUND veh=0 found=1
1518: CPL-SCAN tick=4088 veh=0 found=1 closed=1 limit=10000 nodes=5 enter=4 noplat=1 ... last=248,133 why=acc-car
1519: CPL-HEAD best=248,133 head=248,133 on=1
1520: CPL-BEST veh=0 tile=248,133 td=1 cost=6100
1521: CPL-BACK pPrev=248,133 td=1 parent=248,127 ptd=1
1523-1525: REACH cur=248,127 td=1 depth=0 / 248,128 depth=1 / 248,129 depth=2
1526: CPL-FIRST-TD st=248,127 hops=1 yapf=1 at=248,128 tdb=0x202 ok=0 best=1 start=248,127 trk=1
1528-1530: RP cur=248,127 td=1 depth=0 / 248,128 depth=1 / 248,129 depth=2
1531: CPL-RES-OK veh=0 total=2 booked=2 stop_safe=0 term_exempt=0 tiles=248,128/1;248,129/1
1533: RESV-END veh=0 tick=4088 from=248,127 endTile=248,133 endTd=1 endPos=95 curPos=0 endBits=0202
```

**四条硬读数**：

1. **`RP` 只打印到 `depth=2`（`248,129`），没有 `depth=3`** ⇒ `rp_impl` 在 `cur=248,129` 内部就 `return false` 或提前 `return true`，**从未递归到 248,130**。
2. **`steps` 只有 2 格且全是普通轨**（`tiles=248,128/1;248,129/1`）⇒ 按 `rp_impl` 的 push 条件（`!IsRailStationTile(cur) && !IsRailDepotTile(cur)`，`yapf_rail.cpp:1676-1681`），这两格**不是**站台/车库；会合点 `248,133` **不在 steps 里**。
3. **`CPL-BACK pPrev=248,133 parent=248,127`** ⇒ YAPF 的节点链认为 `248,133` 的父是 `248,127`，即**一步跨 6 格**。这与 `RP` 逐格只走到 `248,129` 直接冲突 —— 两条代码路径对同一段轨道给出了不同的连通结论。
4. **`REACH` 同样停在 `depth=2`**，与 `RP` 一致；而 `CPL-FIRST-TD ... at=248,128 hops=1` 说明 YAPF 首步确实落在 `248,128`。

#### 10.14.2 猜测根因（待验证，REG-400-C）

`rp_impl` 的站台跳段分支（`yapf_rail.cpp:1692-1695`）：

```1692:1695:src/pathfinder/yapf/yapf_rail.cpp
					if (!f3.Follow(cur, cur_td)) return false;
					if (f3.is_station && f3.tiles_skipped > 0 && R3RSkippedStationStretchContains(cur, f3.new_tile, f3.exitdir, f3.tiles_skipped, tg)) {
						return true;
					}
```

`TrackFollower` 进入站台格时会**一步跳到站台末端**（`follow_track.hpp` 的 `new_tile += step*(length-1)`、`tiles_skipped = length-1`）。若 `248,130`～`248,133` 是一段站台，则：

- `rp_impl` 在 `cur=248,129` 处调用 `Follow` ⇒ `new_tile = 248,133`、`tiles_skipped = 3`；
- `R3RSkippedStationStretchContains(248,129, 248,133, exitdir, 3, tg=248,133)` 命中 ⇒ **`return true`**（视为"已到达会合点"）；
- 但被跳过的站台格（`248,130..248,132`）既没被 push 进 `steps`，也没有任何一句代码为它们订整格位 —— 注释 `:1677-1678` 明言"站台/车库不订"。

⇒ `steps` 止于 `248,129`，`R3RReservePathSequence` 订完 2 格即 `booked=2/total=2`，站台整格预留无人订 ⇒ 机车开到 `248,129` 进不了站台 ⇒ `cur_speed` 归零（`:1534` `spd=0`）⇒ 下一次 `TryReservePath` 起点仍是 `248,127`（`:1535-1554` CPLSPD 逐 tick 加速但 `TRP` 只有一次）⇒ 每 4 秒重复同一组 2 格预留。

**待验证判据（缺一条即可定性）**：
1. `248,130`～`248,133` 的 `TileType`（是否 `TileType::Railway` + `IsRailStationTile`）；
2. `248,133` 上是否有站台（`PFD-ACCEPT ... t=3` 的 `t=3` 是否指站台类型）；
3. `Cpl-BACK` 的 `parent=248,127` 是否来自站台跳段（若是，则 YAPF 与 rp 的差异被解释）。

**若成立，修法**：站台跳段分支不能直接 `return true`，必须把被跳过的站台格**补进 steps**（每格 `SetRailStationReservation` 口径由 `R3RReservePathSequence` 对站台格单独处理），或至少把 `248,130..248,133` 作为整格位订下 —— 这正是第 93 轮 KI-163 曾做过的"补齐站台预留"循环，单段 `rp` 改造时丢了。

**未确认（不得当作结论）**：`248,128` 有信号（`:1508`）却未触发 `CPL-RES-SAFE-STOP`（`stop_safe=0`），说明 `IsSafeWaitingPosition` 在该 trackdir 上判 false；此点与 REG-400-B 相关但本轮不展开。

### 10.12 玩家追问答复：不是「因为终点」；「配对车预留算安全位」这条机制不存在

**玩家原话**：「为什么最后一个安全位之后没有人续旗，是因为那是终点吗，但是我记得之前的机制就是配对的车发出的预留都算安全位啊」

**① 不是「因为终点」**。`IsSafeWaitingPosition()`（`src\pbs.cpp:2725-2782`）全部 `return true` 就这几门：`:2727` 车库、`:2730` **block（非 PBS）信号**、`:2732` 隧道桥信号入口、`:2740-2743` **线末**、`:2748` 下一格无可走方向且 `include_line_end`、`:2750-2768` 单个 PBS 信号。**线末（终点）恰恰是安全位**，不是不安全位。`248,127` 被选中是因为它自己命中了某条门（最可能 `:2730` 的 block 信号，与玩家第一句「终点前最后一个信号灯」吻合）。

**②「配对车的预留算安全位」这条机制不存在，方向相反**。两函数分工严格：

| 函数 | 看什么 | 结论 |
| --- | --- | --- |
| `IsSafeWaitingPosition()` `pbs.cpp:2725` | 只看几何/信号（车库、信号、线末） | **一个字都不看预留** |
| `IsWaitingPositionFree()` `pbs.cpp:2823` | 只看预留 | `:2826-2829`：`if (TrackOverlapsTracks(reserved, track)) return false;`（注释原文 "Tile reserved? Can never be a free waiting position."） |

原生驾驶预留（`train_cmd.cpp:16287-16293`）的组合是「安全 **∧** 空闲」才是预留终点；被配对车订着的格会让 `wp_free=false` ⇒ `safe-not-free` ⇒ `break`。**配对车的预留是阻塞，不是放行。**

玩家记忆里「自己人的预留被豁免」确有其事，但豁免的是**同一条链**，且只在 R3R 自己的挂接寻路里：

```348:349:src/pathfinder/yapf/yapf_rail.cpp
					const Train *const holder = GetTrainForReservation(tile, tk);
					if (holder == nullptr || holder->First() != coupler->First()) continue;
```

判据 `holder->First() == coupler->First()` —— **本链**，与「配对的那列车」无关。

**③「没人续旗」的准确机制**：挂接预留 `R3RReservePathSequence()` 是**一次性**调用，命中安全位即 `break` 并把终点收到该格（`yapf_rail.cpp:1706-1716`）；真正的"续"靠**下一次 `TryReservePath`**，起点是**机车脚下那一格**。机车开到 `248,128` 后前面 6 格没订 ⇒ 无路 ⇒ 停住 ⇒ 起点永远停在 `248,128` ⇒ 再也不摸路 ⇒ 自锁。原生驾驶预留不锁死，是因为列车每次都停在**安全位上**，下一 tick 从那里重算再往前顶一格；挂接的会合点 `248,133` 在末端，中间 6 格没有任何机制去订。

### 10.13 修法落地（第 399 轮，玩家批准「那就这么改吧」）

**改一处文件**：`src\pathfinder\yapf\yapf_rail.cpp` 的 `CYapfReserveTrack::R3RReservePathSequence()`，**未碰任何 `src\*.h`**。

1. **预扫描最后一个安全位**（插在 `bool stopped_at_safe = false;` 之后、主循环之前）：倒序扫 `steps`，第一个满足 `IsSafeWaitingPosition(...)` 的下标记为 `r3r_last_safe`（无则 `steps.size()`），新增 `bool term_exempt = false;`。
2. **主循环加终点例外**：命中安全位时先判 `if (i == r3r_last_safe)` ⇒ 置 `term_exempt=true`、打 `CPL-RES-TERM-EXEMPT veh=… nth=… tile=… td=… booked=…`、`continue`（**不 break**，继续订满到会合点）；否则走原 `CPL-RES-SAFE-STOP` + `break`。路径中段有多个安全位时仍按第一个截断，口径不变。
3. **探针扩容**：`CPL-RES-OK` 增加 `term_exempt=%d`，便于判读例外是否生效。

**为什么 booked 订满就够**：调用方 `yapf_rail.cpp:1706-1716` 只在 `reserved && booked > 0 && booked < r3r_resv_path.size()` 时才把终点收到 `r3r_resv_path[booked-1]`。例外生效时 `booked == size()` ⇒ 不收缩 ⇒ 预留终点自然留在会合点。

**待复测判据**（4 条）：
1. 同场景出现 `CPL-RES-TERM-EXEMPT veh=… tile=248,127`，且同一次 `CPL-RES-OK` 的 `term_exempt=1`、`booked == total`；
2. 地图上黑色预留延伸到会合点（`248,133`），机车不再停在 `248,128`；
3. `COUPLE-OK` 出现（`loco=` 对应机车、`rear=` 对应车底）；
4. 负判据：路径中段有多安全位的普通挂接仍出现 `CPL-RES-SAFE-STOP` 且 `term_exempt=0`，`booked < total`，行为逐字不变。

**仍未确认（登记疑点 REG-399-D）**：日志 `:240 RESV-TERM veh=0 real=2 type=1 tile=248,127` 与 `:239 RESV-SIGPASS` 在同一次挂接里出现，但 `RESV-TERM` 属于**驾驶预留**（`ChooseTrainTrack` 的 `ExtendTrainReservation` 终点探针），不在本轮改动的 `R3RReservePathSequence` 之内。若复测后机车自身的驾驶预留仍止于 `248,127`（黑色预览只到 127），则需对 `ExtendTrainReservation` 的 `GOTO_COUPLE` 订单做同样的终点例外——那是**全局代码**，风险高于本轮，须先拿到复测日志再决定，不得顺手改。

**构建自证（2026-11-08 02:52）**：入口 `R3R_fullbuild.cmd inc detach`（复用既有脚本，未新建 .cmd）；`build\R3R_incbuild.guard.log` = `GUARD: incremental is safe (no header/lang file is newer than the newest object)`；`build\R3R_incbuild.done` = `EXIT_CODE=0` @02:52:38；`src\pathfinder\yapf\yapf_rail.cpp` @02:50:00 → `build\CMakeFiles\openttd_lib.dir\src\pathfinder\yapf\yapf_rail.cpp.obj` @02:51:06（8 963 216 B）→ `build\openttd.exe` @02:52:23（52 488 192 B）；`read_lints` 0 条；exe 内 ASCII 检索 `CPL-RES-TERM-EXEMPT` = **True**（新探针确实进了二进制）。发布树 `build-release` 未编，停留在旧版。

### 10.11 玩家追问（2026-11-08）：「最后一个安全位之后没人续旗，是因为那是终点吗？我记得配对车的预留都算安全位」

**玩家原话**：「为什么最后一个安全位之后没有人续旗，是因为那是终点吗，但是我记得之前的机制就是配对的车发出的预留都算安全位啊」

**答复（代码实证，两条都要纠正）**：

**① 不是「因为是终点」。** `IsSafeWaitingPosition()`（`src\pbs.cpp:2725-2782`）全文只有这些 `return true`：
- `:2727` 车库格
- `:2730` **block（非 PBS）信号**
- `:2732` 隧道桥信号入口
- `:2740-2743` 线末（`!ft.Follow` 且 `include_line_end`）
- `:2748` `new_td_bits == NONE` 且 `include_line_end`
- `:2750-2768` 单个 PBS 信号（含 NoEntry / AlwaysReserveThrough / TRPRF_RESERVE_THROUGH 三种例外）

**线末（终点）恰恰是安全位**，不是不安全位。所以「因为那是终点所以不续」不成立。`248,127` 之所以被选中，只可能是它自己命中了上面的门 —— 最可能是 **`:2730` 的 block 信号**（与玩家第一句「直到终点前的最后一个信号灯」逐字吻合），或 PBS 信号的 `:2767`。

**② 「配对车的预留算安全位」这条机制不存在，实际方向相反。** 两个函数分工严格：

| 函数 | 看什么 | 结论 |
| --- | --- | --- |
| `IsSafeWaitingPosition()`（`pbs.cpp:2725`） | **只看几何/信号**（车库、信号、线末） | **一个字都不看预留** |
| `IsWaitingPositionFree()`（`pbs.cpp:2823`） | 只看预留 | `:2826-2829`：`if (TrackOverlapsTracks(reserved, track)) return false;` 注释原文 **"Tile reserved? Can never be a free waiting position."** |

即：**被任何车（含配对车）订过的格，永远不算「空闲等待位」**。原生驾驶预留（`ExtendTrainReservation`，`train_cmd.cpp:16287-16293`）的组合是：

```16287:16293:src/train_cmd.cpp
		if (IsSafeWaitingPosition(v, tile, cur_td, true, _settings_game.pf.forbid_90_deg)) {
			PBSWaitingPositionRestrictedSignalState restricted_signal_state;
			bool wp_free = IsWaitingPositionFree(v, tile, cur_td, _settings_game.pf.forbid_90_deg, &restricted_signal_state);
			if (!(wp_free && TryReserveRailTrackdir(v, tile, cur_td))) { r3r_backout_reason = wp_free ? "safe-reserve-fail" : "safe-not-free"; break; }
```

⇒ **「安全」∧「空闲」** 才是预留终点；被配对车订着的格会让 `wp_free=false` 直接 `break`（`safe-not-free`）。所以配对车的预留是**阻塞**，不是放行。

玩家记忆里「自己人的预留被豁免」确有其事，但豁免的是**本链自己**、且只在 R3R 自己的挂接寻路里：

```342:360:src/pathfinder/yapf/yapf_rail.cpp
			Train *const coupler = const_cast<Train *>(Train::From(Yapf().GetVehicle()));
			bool self_held = false;
			...
					const Train *const holder = GetTrainForReservation(tile, tk);
					if (holder == nullptr || holder->First() != coupler->First()) continue;
```

判据是 `holder->First() == coupler->First()` —— **同一条链**才豁免，与「配对的那列车」无关。

**③ 所以"没人续旗"的准确机制**（不是设计意图，是驱动方式差异）：
- 挂接预留 `R3RReservePathSequence()` 是**一次性**调用，命中安全位就 `break` 并把终点收到该格（`yapf_rail.cpp:729`、`:1712-1715`）；
- 真正的"续"靠**下一次 `TryReservePath`**，而它的起点是**机车脚下那一格**（`origin_tile`）；
- 机车开到 `248,128` 就再没有已订的格可进 ⇒ `TrainController` 无路 ⇒ 停住 ⇒ 起点永远停在 `248,128` ⇒ **再也不摸路** ⇒ 自锁；
- 原生驾驶预留之所以不锁死，是因为列车**本身每一次都停在一个安全位上**，下一 tick 从那里重算再往前顶一格；而挂接的会合点在 `248,133`，中间 6 格没有任何机制去订。

⇒ 修法口径不变，且现在有了明确理由：**安全位截断对"路径中段"成立，对"终点段"不成立** —— 终点段（截断点与会合点之间不存在下一个安全位）必须一次订满，不能指望下一 tick 自己长出来。
