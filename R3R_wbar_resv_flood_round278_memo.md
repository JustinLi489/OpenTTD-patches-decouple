# 第 278 轮：截断路点生效后「预留满天飞」取证（F-437-D 实机复测）

- 玩家原话（2026-10-22）：「还是一样难绷的情况啊，怎么回事啊，各种预留问题满天飞」
- 输入日志：`D:\temp\codebuddy-dropped-files\646c456b-0129-49a8-9e94-58732dcd0c06\R3R_debug.log`
  - 1000 行 / 82 629 B / 最后写入 2026-10-22 22:14
  - 第 1 行即 `SEGLOAD-PARK veh=45`（读档边探针）⇒ **本文件是「读档 → 运行」的完整一次会话**，不是尾部截取（否则不会恰好从 SEGLOAD 开始）
  - 产生者应为 22:05:42 构建的 F-437-D 版 `build\openttd.exe`（日志晚于 exe）
- 结论摘要：**F-437-D 的三条判据本身没有失败，`WBAR-*` 全程 0 条是"没到那两个状态"，不是"没装进去"**。真正炸的是下游——`CPL` 挂接寻路每 tick 重订一条长预留、下一 tick 又整条 free-path 释放，且**释放的预留里有一段方向与回溯链完全不符**（下详 §4）。

## 一、工具教训（先记，避免重复踩）

`search_content`（ripgrep）对 `D:\temp\codebuddy-dropped-files\...\R3R_debug.log` **恒返回 0 条**（对 `RESV-` 这种必然存在的串也返回 0），而同一文件 `findstr` 能正常命中。
⇒ 读玩家丢过来的日志**只能用 `findstr` 统计/取行**，不能用 `search_content`，否则会把"有"读成"无"（本轮一度误判 `RESV-` 为 0 条）。

可用写法（本环境 shell 是 cmd，`;`/`&` 不是分隔符，必须一次一条）：

```
cmd /c findstr /C:"WBAR" "<file>" | find /c /v ""
cmd /c findstr /N /C:"CPL-SAFE-FAIL" /C:"CPL-SCAN" ... "<file>"
```

## 二、F-437-D 判据核对（全部通过"未触发"口径）

| 判据 | 计数 | 判定 |
|---|---|---|
| `WBAR-OVERSHOOT` | 0 | 未出现「越过路点」（车没走到路点格又离开） |
| `WBAR-RELEASE` | 0 | 未出现「配对就绪放行」 |
| `CPL-WAITCUT` | 1 | **截断在工作** |

唯一一条（`:827`）：

```
CPL-WAITCUT veh=21 state=1 wp=17 tile=205,274
```

`state=1` = 「截断到路点、且本车还没到路点」⇒ 与 §10.8 的实现一致（`pair_ready==false && front_on_wp==false` ⇒ `return 1`，此路径按设计不打 `WBAR-*`）。
`accwp=3`（`:830`/`:890`/`:956` 的 `CPL-SCAN`）说明 PFD 接受了 3 次「等待区路点」作为目的地。

⇒ **判据 ①②③ 无法用本日志判定**（③"普通车经过路点不出 `WBAR-*`"倒是因为 `WBAR-*` 全 0 而天然满足，但属于"没触发"而非"验证过"）。

## 三、挂接本身是成功的

```
:107 CPL-PAIR tick=2338 act=21 tgt=24 dist=0 actTile=1889,188 tgtTile=1889,188
:110 CPL-PAIR tick=2361 act=21 tgt=24 dist=0
:118 COUPLE-OK loco=21 rear=50 consist=24 co=0 real=1 type=1 tx=1889 ty=188 x=30234 y=3016
:352 DECOUPLE-FIRE consist=21 tx=267 ty=237 real=5 mode=0 num=0 segs=1 eff=24 plat=48/0
:431 DECOUPLE-JUMP fire_real=5 dec_idx=5 wait_idx=65535 step=-1 v=21 u=24 n_u=17
:436 DECOUPLE-ADV dec_idx=5 v_real=5 u_real=6
:437 DECOUPLE-DONE u=24 co=0 real=6 tx=273 ty=237
```

⇒ 挂车 + 解挂整链都跑通了（`wait_idx=65535` = 没找到等待点，`step=-1`，属 KI-215 已修的边界，本轮不追）。

## 四、真问题：「预留订了又放」的每 tick 洪水（KI-438）

### 4.1 释放量

| 行 | 原文 | 说明 |
|---|---|---|
| `:375` | `RESV-LEDGER-FORGET veh=21 why=decouple mask=0x3 released=3 kept=14` | 解挂边，正常 |
| `:632` | `RESV-LEDGER-FORGET veh=24 why=free-path mask=0x1 released=3 kept=14` | — |
| `:824` | `RESV-LEDGER-FORGET veh=21 why=free-path mask=0x1 released=95 kept=0 phase=choose-track` | **一次 95 格** |
| `:994` | `RESV-LEDGER-FORGET veh=21 why=free-path mask=0x1 released=28 kept=0 phase=choose-track` | 一次 28 格 |

`RESV-GC` 全日志 **157 条**，绝大多数紧跟在 `CPL-RESERVE reserved=1` 之后一 tick。

### 4.2 循环形状（tick 2979 起，逐 tick 重复）

```
:780-815  RESV-GC veh=21 tile=224,239 ... 253,233 track=0x3/0x2/0x0 why=free-path   ← 上一 tick 的整条路径被逐格释放
:816-819  UNRES-RAIL/RESV-REL/RESV-WATCH CLEAR-STN tile=254,233 track=whole owner=21 age=0 phase=gc-stn
          RESV-GC veh=21 tile=254,233 track=stn kind=WAYPOINT why=free-path          ← 等待区路点整格，age=0（刚订就放）
:824      RESV-LEDGER-FORGET ... released=95
:825      CPL-ENTRY veh=21 tile=205,274 orderType=16
:827      CPL-WAITCUT state=1 wp=17
:828      PFD-ACCEPT kind=waitwp tile=254,233
:830      CPL-SCAN found=1 why=cut-wp accwp=3
:831      CPL-BEST veh=21 tile=254,233 td=8 cost=7821
:832      CPL-BACK pPrev=254,233 td=8 parent=237,233
:833      CPL-BACK pPrev=237,233 td=8 parent=205,274
:867      CPL-TRACE-RESULT st=205,274 best=9
:868-883  RP cur=205,274 td=9 depth=0..15（trk=1 bits=0x2，沿 y 递减向北）
:884      RESV-WATCH SET-RAIL tile=236,233 track=0 phase=yapf-couple actor=21 head=21
:885      CPL-RESERVE veh=21 reserved=1 next=9
```

**每 tick 一遍**：先 free-path 掉上一份 95 格，再重新算、重新订一份。屏幕上就是「预留线闪一下、消失、再闪一下」。

### 4.3 与"截断"的关系（为什么 F-437-D 之后更明显）

`CPL-WAITCUT state=1` 是**持续态**：只要配对没就绪，`R3RShouldCutCoupleAtWaitPoint()` 每 tick 都返回 1，`PfDetectDestination` 每 tick 都只认那个路点，于是每 tick 都要重跑 `FindNearestCoupleTrain()` 并重订整条路径。
旧 latch 的"一次放行后永久绿"把这个循环掩盖了（放行后不再进截断分支）。⇒ **本轮不是引入 bug，是把一直被掩盖的每 tick 重订暴露出来**。

### 4.4 预留方向与回溯链不一致（最异常的一条，登记为疑点 REG-278-A）

- `CPL-BACK` 链（yapf 认定的实际路径）：`205,274 → 237,233 → 254,233`（x +32 / y −41）
- `RP` 实际订的 16 格：`205,274 → 205,273 → … → 205,259`（x 不变 / y −15，纯北向）

两者**第一条就分叉**。`SPD` 侧无异常（车在 205,274→205,272 确实在动），但预留线画在"向北直行"上。
与 KI-163（`rp()` 沿站台轴盲扫 / 补齐车头到车底那段的 step 方向）同族，必须复核 `yapf_rail.cpp` 的 `rp()` 是否又走了轴向 delta 而不是 `TileOffsByDiagDir(exitdir)`。

同时 `:884 SET-RAIL tile=236,233 track=0` 只订了 1 格（`track=0` 而非预期轨道），而 `:793-823` 释放时 `track=0x0` 也占多数 —— **整条路径里有大量"track 记成 0"的条目**，这本身可疑（记账键值不对会让后续释放/审计失效）。

### 4.5 CPL-SAFE-FAIL：自己的预留把自己判死

```
:952 CPL-ENTRY veh=21 tile=234,233 orderType=16
:956 CPL-SCAN tick=2989 found=1 why=cut-wp
:957 CPL-BEST veh=21 tile=254,233 td=8 cost=3300
:959 FSCP tile=238,233 fail=tryReserve td=8 want=0 type=1 tbits=0x25 res=0x20 plain=1
:960 FSCP-RES tile=238,233 tk=5 owner=21 otile=234,233 odir=1 ospd=235 ostop=0 ord=16 oend=257,234
:961 FSCP-REACH-NONE tile=238,233 (no chain reaches this tile along a contiguous reservation)
:962 FSCP-VEH tile=238,233 veh=16 track=0x20 dir=0 front=0 ord=0
:963 FSCP-VEH tile=238,233 veh=17 track=0x20 dir=0
:964 CPL-SAFE-FAIL veh=21 tile=254,233
```

- `res=0x20` + `owner=21` + `ospd=235` ⇒ 该格已被 **veh=21 自己**正在行驶的路径预留占着。
- `fail=tryReserve` ⇒ back-walk 想在 238,233 再订一次，撞上自己的位。
- 结果整次挂接寻路作废（`:994 released=28`）。

与 KI-221 的"同格车误判"同族但不同因：那次是 `CheckTrainsOnTrack` 把别的车当障碍，这次是 **back-walk 的 TryReserveRailTrack 与自己的既有预留冲突**。
⇒ 猜测根因 REG-278-B：`FindSafeCouplePositionProc()` 的 `tryReserve` 分支没有对「owner == 本车」做豁免（`FSCP-REACH-NONE` 说明它也没找到"沿连续预留能到达该格"的链，即 `R3RFollowReservationToOwner` 的双向回溯没认自己）。

## 五、疑点清单（按"有疑点即落盘"规则登记）

| 编号 | 疑点 | 期望值 | 实测值 | 还需什么数据 |
|---|---|---|---|---|
| REG-278-A | `RP` 预留方向与 `CPL-BACK` 分叉 | RP 应逐格等于 CPL-BACK 链 | `205,274→205,259` vs `205,274→237,233` | 该 tile 是否站台/路点格（`IsRailStationTile`/`GetPlatformLength`）；`rp()` 内 step 来源 |
| REG-278-B | 自己的预留导致 `CPL-SAFE-FAIL` | 同 owner 应豁免 | `owner=21` 且 `ospd>0` 仍 `fail=tryReserve` | `FindSafeCouplePositionProc` 的 tryReserve 分支与 `R3RFollowReservationToOwner` |
| REG-278-C | 大量 `RESV-GC ... track=0x0` | 记账应记真实 `Track` | 793-823 段 30+ 条 `track=0x0` | `ReserveTrackUnderConsist` 里 `fresh` 收集的 track 会不会取到 0 |
| REG-278-D | `RESV-AUDIT-ARM tick=2338 interval=2048 due=0` 后无 `RESV-AUDIT-SUMMARY` | 应每 2048 tick 一条 | 全日志 0 条 SUMMARY | 会话是否只跑到 tick 2989（不到一个周期）⇒ 大概率正常，暂记 |
| REG-278-E | `WBAR-RELEASE` 0 条 | 若配对曾被上轮 `COUPLE-OK` 就绪过，应有 1 条 | 0 | `COUPLE-OK` 发生在 depot 内（`:118`），当时可能 `R3RShouldCutCoupleAtWaitPoint` 未被调用（不在 GOTO_COUPLE 寻路边） |

## 六、猜测根因（待验证）

1. **REG-278-A 是真 bug，且优先级最高**：预留线与实际路径分叉，会让「预留挡住别的车」出现在错误的格子上（玩家说的"各种预留问题"里最刺眼的就是幽灵线）。若确认，`rp()` 的 step 需要再次修正（KI-163 只修了"沿站台轴盲扫"那一半）。
2. **REG-278-B 是"订了又放"的直接触发点**：`CPL-SAFE-FAIL` ⇒ 整条路径 free-path ⇒ 下一 tick 重订。若不修，截断态每 tick 都会刷一次 95 格。
3. **每 tick 重订本身需要成本闸门**：即使 A/B 都修好，`state=1` 持续期间仍会每 tick 重算一次完整 CPL 寻路（`closed=2~3`、`nodes=10~13`，不算贵，但预留的反复 SET/CLEAR 对观感和 desync 面都不好）。可考虑「上一 tick 已订到同一个等待路点且路径未变 ⇒ 复用，不重订」。

## 七、下一步取证（最多 3 组，取完即回写本节）

1. `yapf_rail.cpp` 的 `rp()` 全文 + `reach()`，确认 step 来源与"补齐车头车底"分支。
2. `FindSafeCouplePositionProc()`（`yapf_rail.cpp` 约 :108）的 `tryReserve` 分支 + `R3RFollowReservationToOwner` 的 owner 判定。
3. `ReserveTrackUnderConsist` 的 `fresh` 收集，确认 `track=0x0` 怎么来的。

## 八、复测判据（修完后）

1. `RP cur=` 序列逐格等于 `CPL-BACK` 链（`205,274→237,233→254,233`），不再出现纯北向 16 格。
2. `state=1` 期间不再每 tick 出现 `RESV-LEDGER-FORGET ... released=95`（同一 tick 内最多一次，且不重复释放同一批）。
3. 负判据：`COUPLE-OK` 仍成立、`DECOUPLE-DONE` 仍成立。
4. 路点整格预留不再出现 `age=0`（订下与释放不是同一 tick）。

## 九、追加取证：`CPL-SAFE-FAIL` 的格子是「孤儿预留」（2026-10-22 当轮）

读 `yapf_rail.cpp:198-311` 后**推翻了 §4.5 的猜测**，实际机制如下。

### 9.1 那段代码在干什么

```cpp
:130 if (HasReservedTracks(tile, tracks)) { ... 找格上的车，判 couple target ... }
:198 else if (GetReservedTrackbits(tile) != TRACK_BIT_NONE) {
:199     if (!TryReserveRailTrack(tile, TrackdirToTrack(td))) { ...探针... return false; }   // :302
:308     R3RResvPhaseGuard r3r_phase_fscp("yapf-fscp", ...);
:309     UnreserveRailTrack(tile, TrackdirToTrack(td));   // ← 订到就立刻撤销：这是「试探」
:310 }
:311 return true;
```

即 `else if` 分支是**探测**：该格已有「别的轨道」的预留 ⇒ 试着订我要的那条轨，订到就立刻撤销并放行，订不到就 `return false`（判不安全）。

### 9.2 现场读数的三个关键位（`:959-963`）

| 字段 | 值 | 含义 |
|---|---|---|
| `tbits=0x25` | `TRACK_X(0x01) + UPPER(0x04) + TRACK_Y(0x20)` | 该格是可交叉的双槽格 |
| `res=0x20` | 只有 `TRACK_Y` 被预留 | 站着的那条是 Y 向 |
| `want=0` | 我们想订 `TRACK_X` | 因为 `CPL-ORIGIN td=8`（东向） |
| `FSCP-RES owner=21 otile=234,233 ospd=235` | 回溯出的"主人"= 本车 | — |
| **`othru=0`** | `TrainReservationPassesThroughTile(21, 238,233) == 0` | **本车自己的预留根本不经过这一格** |
| **`FSCP-REACH-NONE`** | 全图没有任何链的连续预留到达该格 | **这条 `res=0x20` 谁都不认领** |
| `FSCP-VEH veh=16 / veh=17 track=0x20` | 格上物理停着 16/17 | 它们只是停在那儿，不带这条预留 |

⇒ 结论修正：**不是"自己的预留撞自己"，而是 238,233 上挂着一条 TRACK_Y 的孤儿预留（stale，无主）**。
`GetTrainForReservation()` 按 `pbs.cpp` 的语义会沿预留追到一个"站在那儿的车"，于是把这个幽灵位误指给了本车（这也是 `pbs.cpp:236-258` 那段注释自陈的老毛病）。

### 9.3 于是整条链闭合了

1. 某处留下孤儿预留 `TRACK_Y @ 238,233`（来源未定，见 9.4）。
2. `FindSafeCouplePositionProc` 判该格不安全 ⇒ `CPL-SAFE-FAIL` ⇒ **整次挂接寻路作废**。
3. 作废 ⇒ `FreeTrainTrackReservation` ⇒ `RESV-LEDGER-FORGET released=28/95`（KI-438）。
4. 下一 tick 重新寻路 ⇒ 又撞同一条孤儿预留 ⇒ 回到 2，**每 tick 一次**。
5. 孤儿预留的唯一兜底是 `RESV-AUDIT`，但周期 2048 tick：`:106 RESV-AUDIT-ARM tick=2338 interval=2048 due=0`，本会话只跑到 tick 2989，**审计一次都没跑**（下一次 tick 4386）。

### 9.4 孤儿预留来源（未定，登记 REG-278-F）

- `:965-969` 释放时多数记 `track=0x0`，而 `:884 SET-RAIL tile=236,233 track=0` 记账也是 0 —— 若记账的 Track 与真正订下的 Track 不一致，`UnreserveRailTrack(tile, track)` 就会「放了别的轨、留下这条」，正是孤儿位的产生方式。
- 也可能来自第 240 轮 KI-346（`rp()` 订错轨道）的**存量**：该修复只防新增，不追溯旧档/旧会话已订下的位。
- 判定需要：在 `R3RReleaseReservationSafe()` 里把「释放前后 `GetReservedTrackbits(tile)` 的实际值」各打一行（只读），对比记账 track 与实际释放结果。

### 9.5 三个修法（按"见效快/风险低"排序，待拍板）

- **F-278-A（快，风险低，治标）**：在 `FindSafeCouplePositionProc` 的 `else if` 分支，`TryReserveRailTrack` 失败**且** `FSCP-REACH-NONE` 条件（无任何链的连续预留到达该格）成立时，不 `return false`，而是主动 `UnreserveRailTrack(tile, 该轨)` 清掉孤儿位后按「已清」放行。
  - 优点：一次修掉"卡死 + 每 tick 重订"两个症状；`REACH-NONE` 是很强的判据（全图无链可达 ⇒ 必是孤儿）。
  - 风险：需要一次全 `Train::Iterate()`（已有代码就在旁边，可复用），频率=只在失败时。
- **F-278-B（治根，较慢）**：查清 `track=0` 记账来源并修 `ReserveTrackUnderConsist` 的 `fresh` 收集（要读 `train_cmd.cpp` 的记账段）。
- **F-278-C（兜底，立刻可做）**：把 `RESV-AUDIT` 的触发从「2048 tick 周期」改成「CPL-SAFE-FAIL 后立刻置脏、下次审计提前」（与第 243 轮续十二的 `_r3r_resv_audit_due` 同机制，只需加一个置位点）。
- **不建议**：把 `return false` 改成无条件 `return true`（会让所有"格上有别人的预留"都放行，等于关掉安全位判据）。

### 9.6 复测判据（F-278-A 落地后）

1. 同场景不再出现 `CPL-SAFE-FAIL veh=21 tile=254,233`。
2. 出现一行新的孤儿清理解说（建议 `FSCP-ORPHAN-CLEAR tile=x,y tk=n`），且**只出现一次**（清掉后不再复发）。
3. `RESV-LEDGER-FORGET ... released>=28` 不再逐 tick 重复（KI-438 随之收敛）。
4. 负判据：格上真有别人停着（`FSCP-VEH` 有车且 `REACH` 命中）时，仍必须 `return false`，不得误清别人的预留。

## 十、拍板清单（玩家按编号回答即可）

- **C-278-1｜先做哪一层？**
  - (a) 只做 **F-278-A**（孤儿位清理解除 `CPL-SAFE-FAIL`，最小改动，立刻止住"满天飞"）
  - (b) **F-278-A + F-278-C**（再让审计在失败后 256 tick 内提前跑，双保险）
  - (c) 直接做 **F-278-B**（查 `track=0x0` 记账，治根但需先取证、当轮不一定见效）
  - 推荐 (b)：A 修症状链、C 兜住存量孤儿位，B 留作下一轮专项。
- **C-278-2｜`F-278-A` 清孤儿位的边界**（只在 `FSCP-REACH-NONE` 成立时清）
  - (a) 只清「本格当前要订的那条轨」，其他位一概不碰（**推荐**）
  - (b) 该格上所有无主位一次清干净（更彻底，但会改地图上更多位）
- **C-278-3｜`state=1` 截断态要不要加"复用既有预留"的成本闸门**（KI-438 修法 ②）
  - (a) 加：上一 tick 已订到同一等待路点且路径未变 ⇒ 不重订（省 CPU、少闪烁，但要判定"路径未变"，风险中等）
  - (b) 不加：只靠修掉 `CPL-SAFE-FAIL` 让每 tick 重订变成"重订但成功"，先看实机观感（**推荐**，改动最小）
- **C-278-4｜REG-278-A（`rp()`/`CPL-BACK` 方向分叉）本轮是否一起查？**
  - (a) 一起查（可能与 KI-163 同源，但需读 `rp()` 全段）
  - (b) 先不管（**推荐**：它只是放大器，A/B 修完后若"预留线画在错格"仍可见再专项）

