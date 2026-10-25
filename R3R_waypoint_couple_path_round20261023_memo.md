# 路点 + 前往挂接（GOTO_COUPLE）寻路交互 —— 第 2026-10-23 轮（第二轮）临时分析报告

> 输入：`D:\temp\codebuddy-dropped-files\bd0717d8-8b26-49c6-9ef5-95a4a4947b83\R3R_debug.log`
> 规模：1 577 049 B / 9 930 行 / LastWriteTime 2026-10-23 13:33:02
> 场景：机车 veh=21 执行 `OT_GOTO_COUPLE`（type=16）前往等待车底 veh=24（停靠 265,234，等待点 278,234）
> 玩家口述：「预留成功被拦截住了」+「路点+前往挂接的寻路机制可能还是有些问题」

## 一、已确认的正向事实（本轮改动/既有修复生效）

| 锚点 | 原文 | 判定 |
| --- | --- | --- |
| `:9699` | `RESV-NOBOOK veh=24 tile=265,234 order=17 caronly=0 reason=parked-waiter` | KI-208 停驶等待者门禁**生效**：停稳的 `OT_WAIT_COUPLE` 车底不再重订出站路线 |
| `:9673` | `RESV-GHOST-SUMMARY waiter veh=24 cand=3 freed=3` | 等待车底的幽灵路径预留 3/3 全部回收 |
| `:9674-9697` | `RESV-GC veh=24 tile=273,237 … 278,237 track=stn kind=STATION why=moved-gone` | 站台整格位随车离开被清理（24 行，6 格 × 4） |
| `:9711` | `RESV-GC veh=21 tile=254,233 track=stn kind=WAYPOINT why=free-path-hold` | 路点整格位被 GC 处理（见疑点 D-1） |
| `:9804/:9807` | `CPL-PATHFOUND veh=21 found=1` → `CPL-BEST veh=21 tile=278,234 td=8 cost=16442` | 挂接寻路本身能出路径 |

## 二、疑点（登记，判据待补）

### D-1（高）账本「kept=1」与「实际释放路点整格位」自相矛盾

```
:9705-9715  RESV-GC veh=21 tile=248,233 … 258,233   （共 11 格：10 条 track=0x0 plain + 1 条 track=stn kind=WAYPOINT）
:9711        RESV-GC veh=21 tile=254,233 track=stn kind=WAYPOINT why=free-path-hold
:9716        RESV-LEDGER-FORGET veh=21 why=free-path mask=0x1 released=10 kept=1 phase=choose-track
```

- 期望：`kept=1` 表示「11 条账本记录中保留了 1 条」，被保留的应当正是那条 `kind=WAYPOINT` 整格位。
- 实测：`kept=1` 的同一批输出里，`:9711` 已经把 `254,233` 的路点整格位按 `why=free-path-hold` 释放（**没有 `-skip` 后缀**）。
- 为什么存疑：若整格位真被释放，则列车此刻正压在/即将进入该格（mf 与 CPL 回溯路径都经过 254,233）⇒ 自身 PBS 保护消失；若只是打印而未释放，则 `RESV-LEDGER-FORGET` 的 `kept/released` 计数口径错误。两种都可能，日志分不清。
- 需要的判据：`R3RReleaseReservationSafe()` 的 `-skip` 后缀是否只在失败时追加；`free-path-hold` 分支到底调没调 `SetRailStationReservation(tile,false)`。

### D-2（高）`CPL-WAITCUT` 的 state 在 2/0 之间跳变，并直接把速度砍到 0

```
:9704  CPL-WAITCUT veh=21 state=2 wp=17 tile=248,233
:9732  SPDD veh=21 from=2 to=0 dt=21 ctx=waitcut ... tile=248,233 x=3977 db=1 order=16 real=12
:9717  COUPLE-FAIL loco=21 order=16 tx=248 ty=233 x=3977 y=3736
:9731  COUPLE-WAIT-OFFDEST veh=21 tile=248,233 mf=254,233 real=12 … onStnTile=1
:9800  CPL-WAITCUT veh=21 state=0 wp=17 tile=253,233     （随后放行，:9804 CPL-PATHFOUND found=1）
```

- 期望：挂接接近过程中，若「等待点切断」判据成立就应稳定成立；不成立就不该砍速。
- 实测：同一订单（order=16，real=12）、同一次接近动作内，先在 248,233 判 `state=2`（砍速 → 0 → `COUPLE-FAIL`），4 格之后在 253,233 判 `state=0`（放行）。
- 为什么存疑：`state=2` 时车的 `mf=254,233` 与 `tile=248,233` 分居两格（倒车链 db=1，链长跨 6 格），判据很可能拿「链尾/链头格」与「等待点格」比较，而链跨越路点格时两侧读数不同 ⇒ 造成来回翻转。
- 需要的判据：`CPL-WAITCUT` 的 `state` 数值定义（哪个分支会置 2）、`wp=17` 的取值来源、以及它比较的是 `tile` 还是 `moving front`。

### D-3（中）挂接路径必须穿过路点格，而路点格正是 GC 清理对象

```
:9809  CPL-BACK pPrev=259,234 td=4 parent=258,233
:9810  CPL-BACK pPrev=258,233 td=8 parent=254,233
:9811  CPL-BACK pPrev=254,233 td=8 parent=253,233
:9830  REACH cur=254,233 td=8 depth=1 trk=0 bits=0x1
```

- 期望：路径穿格本身正常（254,233 就在 253→258 的直线走廊上）。
- 实测：每次 `CPL-RESERVE` 成功（`:9839` 之前 `CPL-RESERVE veh=21 reserved=1 next=8`）后，下一 tick 又被 `RESV-GC … why=free-path` 清掉（`:9705-9716` 是一次完整清账，尾部 `:9900-9930` 又重复一次寻路→预订）。
- 为什么存疑：如果「预订-清账」每 tick 循环，列车虽然仍在缓慢前进（dist 18→8），但预留永远不稳定，且路点整格位会被反复清 ⇒ 玩家看到的「预留被拦住」可能只是这一次被拦住，下一 tick 又重犯。
- 需要的判据：`RESV-LEDGER-FORGET` 全日志出现次数与周期；`why=free-path` 的触发条件是否与 `phase=choose-track` 绑定。

## 三、猜测根因（待验证）

### G-1（待验证）路径账本把「路径预留」与「车底整格持有」混在同一 key 下

R3R 的 `_r3r_consist_resv` 账本（第 110 轮 KI-203 引入）用 `veh->index` 作 key，同一 key 里既有 plain rail 路径位、又有站台/路点整格位。`free-path` 清理（路径换向/重订）时按「整批释放」处理，路点整格位没有走 `-skip` 保护分支 ⇒ 每次重订都把自身脚下的路点保护清掉。
支撑：`:9716` 的 `released=10 kept=1` 与 `:9711` 的实际释放并存；`:9705-9715` 的 11 格正好是 `248..258` 一条直线走廊（正是 CPL 路径）。
证伪判据：读代码确认 `free-path-hold` 分支是否过滤 `kind=WAYPOINT/STATION`，以及 `kept` 是否只是「未处理条数」而不是「保留条数」。

### G-2（待验证）`CPL-WAITCUT` 用错端点导致 state 抖动

判据可能用 `consist->tile`（车头/链头所在格）与「等待点格」比较；本场景 db=1 倒车链的 `tile` 与 `moving front` 相差 6 格（248 vs 254），一边命中一边不命中 ⇒ state 2/0 跳变 ⇒ 速度被砍到 0 又立刻放行。
证伪判据：把判据改成 `ConsistFront`（或统一用 `GetMovingFront()->tile`）后 state 应稳定。

## 四、复测判据（下一版 exe）

1. 接近挂接过程中 `CPL-WAITCUT` 对同一次接近只出现一种 state（或 state 变化必须伴随 tile 变化），不再出现「state=2 砍速 → 4 格后 state=0 放行」。
2. `RESV-LEDGER-FORGET … kept=1` 与 `RESV-GC … kind=WAYPOINT` 二者必须自洽：要么 `kept=1` 且无该行释放，要么 `kept=0` 且有释出行。
3. 路点整格位不得在列车仍压在该格/路径穿该格期间被释放（`RESV-GC … kind=WAYPOINT why=free-path-hold` 不应出现，或出现必须带 `-skip`）。
4. `RESV-NOBOOK … reason=parked-waiter` 保持（不回归）。
5. 挂接最终成功：`COUPLE-OK loco=21`，而不是长时间只靠 `CPL-PAIR dist` 递减。

## 五、下一步取证计划（源码，按优先级）

1. `grep CPL-WAITCUT` → 找到 state 语义与端点选择。
2. `grep "free-path-hold"` → 找到 `R3RReconcileConsistReservation` 的分支，确认是否过滤 `kind=WAYPOINT/STATION`。
3. `grep RESV-LEDGER-FORGET` → 确认 `released/kept` 计数口径。
4. 视结论决定改码点（大概率在 `train_cmd.cpp` 的账本 reconcile 与 CPL-WAITCUT 判据）。

## 六、未确认

- U-1：`wp=17` 是订单类型常量（`OT_WAIT_COUPLE=17`）还是别的编号。
- U-2：`dt=21` 与 `ctx=waitcut` 中 `dt` 的含义（计时器/距离）。
- U-3：本次日志的 exe 是否含 2026-10-23 第一轮「车头即停」改动（日志无新增字面量，无法从文本判定；需玩家确认 exe 时间戳）。

## 七、源码取证结论（2026-10-23 第二轮，第 6 次工具调用后落盘）

### 7.1 D-1 / G-1 **证伪并撤销**（`kept=1` 是正确的，路点整格位没有被释放）

`src/pbs.cpp` 已经是第 362/363 轮的 **purpose 模型**，不再是"同 key 整批释放"：

```10:12:src/pbs.cpp
 * The purpose of the bit that is being written *right now*, taken from the phase
 * the caller named with R3RResvPhaseGuard().
```

- `R3RResvLedgerPurpose()`（`src/pbs.cpp:241-248`）：只有 `consist` / `clear-consist` / `waiter-platform`
  三个 phase 记 `R3R_RESV_PURPOSE_CONSIST`，其余（含 `choose-track`、`yapf-couple`、`reap`）记 `PATH`。
- `R3RResvLedgerForgetMask(why)`（`src/pbs.cpp:267-274`）：`free-path` / `couple` / `flip` / `depot-edit`
  **只返回 `R3R_RESV_PURPOSE_PATH`**；注释第 255-258 行明写"在这清 consist 位就是 KI-362"。
- 因此 `:9711 RESV-GC veh=21 tile=254,233 track=stn kind=WAYPOINT why=free-path-hold` 的 `-hold`
  后缀是**保留**（mask 不含 CONSIST ⇒ 不动地图、只打日志），与 `:9716 kept=1` 完全自洽。
- 结论：KI-443 **不是缺陷**，`released=10 kept=1` 的 10 是 10 条 plain rail 路径位（248..258 去掉 254），
  1 是 254,233 的路点整格位。备忘 §二 D-1 的"自相矛盾"判定作废；复测判据 2/3 相应改写为
  「`kind=WAYPOINT` 行必须带 `-hold`，不得出现无后缀（真释放）」。

### 7.2 D-2 / G-2 **成立并定位到行**（真根因：state=2 被当成"找不到车底"处理）

`CPL-WAITCUT state` 语义（`src/train_cmd.cpp` 的 `R3RShouldCutCoupleAtWaitPoint()`）：
`0` = 不截断（放行）、`1` = 进入等待区需截断（首次由 `:15906` 一次性清旧预留）、
`2` = **运动前端已经停在等待区路点格上**、配对尚未就绪。

`src/train_cmd.cpp:15991-16012` 把两种"按住"情形合并处理：

```15991:16012:src/train_cmd.cpp
			if (keep_track != INVALID_TRACK && consist->cur_speed != 0 && pair_ready && r3r_wait_cut == 0) return { keep_track, result_flags };
			if (!pair_ready || r3r_wait_cut != 0) {
				...
				consist->cur_speed = 0;
				consist->subspeed = 0;
			}
```

紧接着（**无条件**）落到 KI-316 的处置：

```16035:16037:src/train_cmd.cpp
			if (changed_signal != INVALID_TRACKDIR) MarkSingleSignalDirty(tile, changed_signal);
			FreeTrainTrackReservation(consist, origin.tile, origin.trackdir);
			if (mark_stuck) MarkTrainAsStuck(consist);
```

`state=2` 时 YAPF 因为"车已到路点、没有合法终点"返回 `INVALID_TRACK` ⇒ `path_found == INVALID_TRACK`，
于是每 tick 都执行 `FreeTrainTrackReservation(why=free-path)`（= `:9705-9716` 那 10 条 plain rail 被清）
并 `MarkTrainAsStuck`。下一 tick 配对就绪 ⇒ `state=0` ⇒ 重新铺满 ⇒ 再有一 tick 不就绪 ⇒ 再清。
这就是玩家看到「预留一会儿被拦住、一会儿又没了」的循环，也是 `:9717 COUPLE-FAIL` 的来源。

- 端点不是问题：`state=2` 用的是运动前端（`:9731 COUPLE-WAIT-OFFDEST veh=21 tile=248,233 mf=254,233`
  说明 254,233 就是运动前端所在的等待点格），`tile=248,233` 只是 `v->tile`（链头）在打印。
  ⇒ G-2 的"端点选错"**不成立**，state=2 本身是**正确**的判据结论。
- 真正的错误是**对 state=2 的反应**：它不是失败，是"已到位、等配对"的正常等待形态。

### 7.3 修法（最小、只改 `src/train_cmd.cpp`，不碰 `.h`）

在 `consist->subspeed = 0;`（`:16011`）之后、KI-316 处置之前插入 state=2 的早退：

```cpp
			if (r3r_wait_cut == 2) return { keep_track != INVALID_TRACK ? keep_track : FindFirstTrack(tracks), result_flags };
```

语义：车已经停在等待区路点格上，预留与 `Stuck` 保持不动，本 tick 不再寻路、不再清账；
下一个 tick 若配对就绪，`state` 自然回 0，`CPL` 正常铺到会合点。
`state == 1` 仍走 KI-316（首 tick 需要把旧预留清掉重铺到路点），行为不变。

## 八、复测判据（修订版，替代 §四）

1. `CPL-WAITCUT state=2` 之后**不得**紧跟 `RESV-LEDGER-FORGET veh=<loco> why=free-path`；`CPL-WAITHOLD … state=2`
   之后预留条数保持不变（可用 `RESV-AUDIT-SUMMARY` 或 `CPL-RESERVE` 的节奏核对）。
2. `RESV-GC … kind=WAYPOINT` 必须始终带 `-hold` 后缀（真释放只允许出现在解挂/销毁路径）。
3. 配对就绪后 `CPL-PATHFOUND found=1` → `CPL-RESERVE reserved=1` → 最终 `COUPLE-OK loco=21`；
   不再出现"每 tick 重铺同一条 248..258"的循环。
4. `RESV-NOBOOK … reason=parked-waiter` 保持（不回归）。
5. 负判据：普通（无等待路点）`OT_GOTO_COUPLE` 机车找不到车底时，仍按 KI-316 撤预留 + `Stuck`，行为不变。

## 九、构建自证（2026-10-23 第二轮）

复用既有 `d:\sourcecode of JGRPP\_tmp_inc_build.cmd`（未新建任何 `.cmd`）；启动前 `tasklist` 确认
`openttd.exe` 已退出。

| 判据 | 实测值 |
| --- | --- |
| 护栏 | `GUARD: incremental is safe (no header/lang file is newer than the newest object)` |
| 编译步 | `[2/3] Building CXX object …src\train_cmd.cpp.obj` → `[3/3] Linking CXX executable openttd.exe` |
| `build\R3R_incbuild.done` | `EXIT_CODE=0`（13:46:23） |
| 错误计数 | `error C` / `fatal error` / `FAILED:` / `build stopped` = **0** |
| 时间戳链 | `src\train_cmd.cpp` 13:38:31 → `train_cmd.cpp.obj` 13:44:58 → `build\openttd.exe` 13:46:15（52 272 640 B） |
| `read_lints` | 0 条 |

状态＝**已实现 + 已编译**，游戏内复测待做（判据见 §八）。本轮只改 `src\train_cmd.cpp` 一个文件，未碰任何 `.h`。
