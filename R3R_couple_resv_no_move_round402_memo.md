# R3R 第 402 轮：预留铺好了，但机车不前进

> 触发：2026-11-08 玩家原话「崩溃已经得到修复，但是机车现在是铺设了预留但不行进」
> 前置：第 401 轮 KI-570（`rail_map.h:40` 断言）已修并编译（`build\openttd.exe` 14:03:45）
> 严重度：**高**（挂接流程走不到 `COUPLE-OK`，等价于第 400 轮现象只解一半）

---

## 一、现象

- 崩溃已消除 ⇒ `CPL-RES-FAIL` 失败分支现在能安全走完（说明确有"站台格订失败"发生，U-400-1 大概率成立）。
- 新症状：**预留铺出来了**（铁轨上能看到预留预览），但**机车不动**。

## 二、猜测根因（待验证，按可能性排序）

### G-402-1：站台整段平台预订把机车自己挡在平台外（最可疑）

R-400-1 让 `ReserveSingleTrack(tg, cur_td)` 对站台格走 `ReserveRailStationPlatform(tile, TrackdirToExitdir(ReverseTrackdir(td)))`，从 `tg` 沿回退方向把 **248,130~248,133 整段平台**置位。
如果这段平台原本是等待车底 `veh=3` 自己占的（U-400-1），或者预订后 `GetTrainForWholeTileReservation()` 认到的主人不是本机车，那么机车从 248,129 尝试进 248,130 时会被 `CheckTrainsOnTrack()` 判为"前方有车"⇒ `MarkTrainAsStuck` + 拒绝放行（与 KI-330「同格对手」同族）。
特征：日志出现 `TPR-BLOCKED` / `CPL-RES-*` 后无 `LOCO-MOVE veh=0`。

### G-402-2：`cur_speed == 0` 的停驶分支永不启动（KI-348 第 4 条结构性路径）

`TrainLocoHandler` 里 `j = UpdateSpeed()`；`if (j < adv_spd)` 走停驶分支。`cur_speed == 0` ⇒ `j = 0`，`adv_spd >= 1` ⇒ **恒走停驶分支**；该分支对 `OT_GOTO_COUPLE` 只做探针 + `CheckTrainCollision || TrainCoupleHandler()`，**不调 `TrainController()`**（唯一预留/推进点）。
即：摸路这次成功了，但下一 tick 因为速度为 0 又回到"只探不推"的分支 ⇒ 预留挂着没人消费，车不动。
特征：日志有 `CPL-RES-OK`、但整段无 `LOCO-MOVE veh=0`（P3 探针就是为这条设的正面证据）。

### G-402-3：`SetReservationTarget` 落在 `tg`（平台末端），而 `booked` 只到平台前

若 `CPL-RES-PARTIAL`（`booked>0` 但不足），终点缩到 `r3r_stop`；若没缩（`booked == path.size()` 但其实只订了 1 格平台失败后退化成 2 格），终点仍写 `tg=248,133`，机车按"终点在平台末端"待命而路径只到 248,129 ⇒ 前进判据不放行。
特征：`RESV-END endTile=248,133` 与 `CPL-RES-* booked=` 的实订范围自相矛盾（第 400 轮备忘 §二 已记录过这个矛盾）。

### G-402-4：`CPL-RES-SAFE-STOP` 仍在平台前

若截断点在 248,129（本来就在车前方一格），机车"应该能动一格"却不动 ⇒ 说明连 248,128 都没进（更像 G-402-1/G-402-2 而非本项）。

## 三、待验证判据（日志）

1. `CPL-RES-OK` / `CPL-RES-PARTIAL` / `CPL-RES-FAIL` 各出现几次，`path=` `booked=` `tiles=` 具体值（是否含 `248,13x`）。
2. `RESV-END endTile=` 是否仍是 248,133。
3. 是否存在 `LOCO-MOVE veh=0`（及其最后一条的 tile）。**完全没有** ⇒ G-402-2 成立。
4. 是否有 `TPR-BLOCKED` / `TRP veh=0 ... ok=0`，其 `otherTile/otherSpd/otherOrder` 指向谁。
5. 是否有 `CPL-S0-CHK`（停驶分支探针）与 `SPD-DROP veh=0`（谁把速度归零）。
6. 预留预览是否**越过**平台（若铁轨上预留一路铺到 248,133 但车停在 248,127，则订到了、进不去 = G-402-1）。

## 四、取证记录

日志：`build\R3R_debug.log` 1801 行 / 192 193 B / 11-08 14:11:22，产生者 `build\openttd.exe` 14:03:45（第 401 轮 KI-570 修复版）⇒ 采样有效。

### 4.1 站台格确实订失败，而且主人不是自己、也不是配对目标

```
1685: CPL-RES-FAIL veh=0 nth=2/3 tile=248,133 td=1 want=1 booked-before=2 failtd=1 type=5 tbits=-1 res=-1 plain=0 st=1 depot=0 owner=5 selfheld=0 otile=248,133 ospd=0 ord=0
1686: CPL-RES-PARTIAL veh=0 booked=2/3 stop=248,133 td=1
1687: CPL-RESERVE veh=0 reserved=1 next=1 path=3 booked=2
```

- `type=5` = `TileType::Station`、`st=1` ⇒ 确系站台格（KI-570 的守卫生效、不再断言）。
- `booked-before=2` ⇒ 前两格订上了；失败发生在第 3 格（`nth=2/3`）= `248,133`。
- `owner=5`、`selfheld=0` ⇒ 该站台格整格位的主人被认成 **`veh=5`**，既不是本机车（0）也不是配对目标（`CPL-PAIRSCAN best=3 bestDist=5`）。
- 故 `booked=2/3` 时走 PARTIAL。

### 4.2 停点/终点写成了"失败格"，不是"最后一格成功格"

```
1690: RESV-END veh=0 tick=4088 from=248,127 endTile=248,133 endTd=1 endPos=95 curPos=0 endBits=0202 junction=0 real=3 spd=0
```

`CPL-RES-PARTIAL ... stop=248,133` 与 `RESV-END endTile=248,133` 一致地指向**失败那一格**。于是机车被"指派"走到一个它订不到的格（该格归 `veh=5`），自己的 PBS 寻路必然失败。

### 4.3 机车确实进了移动分支，但被按在 248,127

```
tail17: LOCO-MOVE veh=0 tick=4516 tile=248,127 j=256 spd=2 track=02 dir=7 db=1 stuck=0 dtile=67832 look=1 endTile=248,133 endTd=1 endPos=95 curPos=0 endBits=0202
tail18: LINEND veh=2 tile=248,127 x=15 sig=1 near=0 spd=2 brk=15 clamp=0 len=2 dir=3 db=1
tail07: COUPLE-WAIT-OFFDEST veh=0 tile=248,127 mf=248,127 real=3 isDepot=0 destDepot=0 destStn=1 destTile=67832 onDepotTile=0 onStnTile=0
tail15: CPL-PAIRSCAN tick=4516 act=0 ... best=3 bestDist=5 paired=-1 ...
```

- `LOCO-MOVE` 存在 ⇒ **G-402-2（永走停驶分支）不成立**，车确实在移动分支里。
- `LINEND ... sig=1` ⇒ `TrainApproachingLineEnd(signal=true)` 路径，即"前方不可通行（按信号处理）"，`brk=15` 但 `clamp=0`（`cur_speed=2 < 15`，不是它压的速度）；`x=15` ⇒ 车头已到格末，`near=0` 故不是"太靠近格末"那条归零分支（`:21885`）。
- `cur_speed` 只有 **2**（另一个快照 `DEPOTBLK ... spd=0`），`tile` 恒 `248,127` ⇒ 车在原地低速抖动/被反复刹停，从未跨格。
- `RESV-END ... curPos=0` ⇒ 自己的 PBS 路径进度停在 0，即"路径没真正建立起来"。

### 4.4 与猜测根因的对照

| 编号 | 判定 |
| --- | --- |
| G-402-1（站台整格位挡车） | **成立**，但机制更精确：不是"我们自己订不上自己的位"，而是该站台格的整格位归 `veh=5`，我方只能 PARTIAL |
| G-402-2（0 速永不启动） | **不成立**（有 `LOCO-MOVE`） |
| G-402-3（终点写失败格、与自己实订范围矛盾） | **成立**（`stop=248,133` / `endTile=248,133` 都是失败格） |
| G-402-4（SAFE-STOP 在平台前） | 与本现场无关（本现场的 SAFE-STOP 出现在 253,121 / 252,122 那几次探索） |

### 4.5 玩家补报的关键事实（REG-402-A 已结案）

> 「机车是 veh0，veh5 是本场景配对目标链的一部分」

- `owner=5` **不是外来车**，而是**配对目标链的成员**（`COUPLE-WAIT-OFFDEST veh=0 … real=3` ⇒ 目标链 3 节；`CPL-PAIRSCAN best=3 bestDist=5` ⇒ 链头 `veh=3`）。
- 由此把 4.1 重新解释：`ReserveRailStationPlatform()` 在第 3 格 `248,133` 失败，是因为**目标车底自己整格占着平台末端**，这是物理事实、不该也无法协商 —— 我方订不到它是**正常的**。
- 于是真正的缺陷不再是"订不上"，而是**摸路把终点定到了目标车底自己的格子上**：`path=3` 只含 `248,131/132/133`，末项 `133` 归目标链；而挂接只需要贴到目标链**相邻的格**（`248,130`）即可。清单里既没有 128/129/130，又把终点放在 133 ⇒ 一是多订/失败，二是车辆自身的 PBS 目标也被指向了它进不去的格。
- `destTile=67832` 换算：本图宽 512 ⇒ `67832 = 248 + 132*512` ⇒ **挂接目标点 = `248,132`**，正好在目标链内部，比摸路终点 `248,133` 近一格 —— 说明 `tg` 的取法偏了一格（取了链上更远的一节），不是"平台不可达"的问题。

### 4.6 新登记疑点

- **REG-402-A**：`veh=5` 是谁？它为什么占着 `248,13x` 的站台整格位（另一个等待车？还是已挂上/未挂上的车底链的某一节）？这决定修法方向：若它是"同站台另一个等待者"，则属正常冲突，应停平台前等；若它是本场景的配对目标另一节，则归属判定本身错了。
- **REG-402-B**：`booked=2` 到底是哪两格（`CPL-RES-OK` 分支才打 `tiles=`，PARTIAL 分支不打），缺这一条无法判断"能否走到 248,132"。
- **REG-402-C**：`LINEND sig=1` 的 signal 来源 —— 已订的两格为何不被认作可通行（是 `path_found == INVALID_TRACK` 导致整段按信号处理，还是真红信号）。

## 五、修法 R-402-1（已落地，仅 `src\pathfinder\yapf\yapf_rail.cpp`，未碰 `.h`）

### 5.1 真正的病根：整段平台预订缺了「伙伴/自己占位＝空气」语义

- `CYapfReserveTrack::ReserveRailStationPlatform()`（`yapf_rail.cpp:530`）原来是**硬判** `HasStationReservation(tile) ⇒ return false`。
- 而 `TryReserveRailTrack()` 的 Station 分支（`pbs.cpp:602-626`）**早已**有第 345/368 轮加的两条豁免：`RESV-PAIR-AIR`（整格位归配对伙伴 ⇒ 不写、不失败）和 `RESV-SELF-AIR`（归自己 ⇒ 同上）。
- 站台段正是挂接的**终点段**，目标车底链自己占着平台末端是物理事实。硬判的后果：`ReserveRailStationPlatform(248,133, 南)` 首格就撞上 `veh=5` ⇒ 立刻 false ⇒ **130/131/132 这三格完全没订**（它们本来没人占！），机车再也进不了站台。
- 这解释了全部读数：`booked=2/3` 只订到平台**前**的两格、平台段一格未订、`curPos=0`（自己的 PBS 路径建不起来）、`LINEND sig=1`（按不可通行处理）、`cur_speed` 只剩 2。

### 5.2 改动

1. `ReserveRailStationPlatform(TileIndex &tile, DiagDirection dir)` ⇒ 加 `Trackdir td` 参数，循环内把 `HasStationReservation/SetRailStationReservation` 改为 `TryReserveRailTrack(tile, TrackdirToTrack(td), false)`（**三参**；`pbs.h:87` 的声明是 `(TileIndex, Track, bool)`，不接受 `Train*`）：
   - 空格 ⇒ 照旧订上；
   - **归配对伙伴/自己 ⇒ 当空气，不写不失败，继续推进**（于是 130/131/132 能订到）；
   - 归别人 ⇒ 仍失败，不越权；
   - `trigger_stations=false`，随机化仍在末尾统一触发一次，与改动前一致。
2. 调用点 `ReserveSingleTrack()` 的站台分支同步传 `td`。
3. `CPL-RES-PARTIAL` 补打逐格清单（`CPL-RES-PARTIAL-TILE veh= k= tile= td=`），下次一眼看清订了哪几格。

### 5.3 为什么不是「改 tg」或「改摸路清单」

`tg=248,133` 由挂接协议给出，属外部契约；而"平台整段能否订"是预订层的语义缺陷。第 345/368 轮已经在**单格**路径上定过口径（伙伴/自己＝空气），站台整段只是漏了同一口径 —— 补它比动 `tg` 风险小得多，且不改变任何"别人的位仍失败"的越权防线。

## 六、构建自证

- 第 1 次：`error C3861: "SAFEGUARD_DO_NOT_USE_THIS_METHOD"` —— 本工程把 `snprintf` 宏禁用了，不能用。
- 第 2 次：`error C3861: "seprintf"` —— 本文件未包含声明它的头。
- 第 3 次：改用逐行 `fprintf`（不拼串）—— 但仍失败：`yapf_rail.cpp(548): error C2660: "TryReserveRailTrack": 函数不接受 4 个参数`（`pbs.h:87` 只有 `(TileIndex, Track, bool)`）。玩家据此指出"exe 时间戳是半小时之前"，核实为真：`build\R3R_incbuild.done` = `EXIT_CODE=1`，14:03:45 那个 exe 是**第 401 轮**产物。
- 第 4 次（成功）：调用改为 `TryReserveRailTrack(tile, TrackdirToTrack(td), false)`；`R3R_fullbuild.cmd inc detach`；`[2/3] yapf_rail.cpp.obj` → `[3/3] Linking CXX executable openttd.exe`；`build\R3R_incbuild.done` = `EXIT_CODE=0`（14:55:29）；`yapf_rail.cpp` 14:35:07 → obj 14:51:59（8 966 297 B）→ `build\openttd.exe` **14:55:13（52 488 192 B）**；日志 `error C/fatal error/FAILED:/build stopped` 计数 **0**；`read_lints` 0。
- 教训：
  1. `yapf_rail.cpp` 里**只能**用 `fprintf` 拼输出，禁用 `snprintf`/`seprintf`。
  2. 调 `pbs.h` 的函数前先看声明：`TryReserveRailTrack` 三参（无 `Train*`）／`TryReserveRailTrackdir` 才有 `Train*`。靠 `r3r_resv_actor`（`R3RResvPhaseGuard`）来标明"谁在订"，不靠参数。
  3. 判构建成功**必须看 `.done` 内容 + exe 时间戳**，不能只看"命令跑完了"。

## 七、第 403 轮：第 402 轮修复的实机复测取证（2026-11-08）

**输入**：`build\R3R_debug.log`（190 909 B，14:59:37）由 `build\openttd.exe`（52 488 192 B，14:55:13 = 第 402 轮版）产生 ⇒ 采样有效，上一轮修复确实进了被测 exe。

### 7.1 修复生效（铁证）

| 行 | 原文摘要 | 含义 |
|---|---|---|
| 1702-1705 | `RESV-PAIR-AIR veh=0 tile=248,133 / 248,132 / 248,131 / 248,130 kind=stn` | 站台四格**不再整段否决**，逐格按"配对伙伴＝空气"放行 |
| 1706 | `CPL-RES-OK total=3 booked=3 tiles=248,128/1;248,129/1;248,133/1` | 预订成功 |
| 1707 | `CPL-RESERVE veh=0 reserved=1 next=1 path=3 booked=3` | **`booked` 由 2/3 变 3/3** |

⇒ 上一轮定位的"硬判 `HasStationReservation()` ⇒ 平台整段一格都订不上 ⇒ booked 被截断"**已被证伪式修掉**，判据①达成。

### 7.2 仍不动（新卡点在另一层）

| 行 | 原文摘要 | 含义 |
|---|---|---|
| 1710 | `RESV-END veh=0 from=248,127 endTile=248,133 endPos=95 curPos=0` | 预订终点对，但**自身 PBS 路径进度恒 0** |
| 1712 | `CPLSPD veh=0 spd=0 strict=80 ... gap=995 ... realbbrk=1 db=1` | 速度 0、**`db=1`（DrivingBackwards）** |
| 1715 | `MAXSPD-BREAK ... adv=80 look=1 lookend=95 ... realbbrk=1 db=1 track=2 maxTrack=80` | 允许 80，实际 0 |
| 1767 | `LOCO-MOVE veh=0 tile=248,127 j=256 spd=2 db=1 ... curPos=0 endBits=0202` | 进了移动分支，`tile` 仍 248,127 |
| 1768 | `LINEND veh=2 tile=248,127 x=15 sig=1 near=0 spd=2 brk=15 clamp=0 len=2 dir=3 db=1` | `x=15`（已顶到格末）却 `sig=1` 判不可通行 |

⇒ 症状与第 401 轮同形（原地 spd=2 抖动、不跨格），**与预订失败无关**。

### 7.3 疑点（立即登记，未验证）

- **REG-403-A（`db=1`）**：机车整段处于 DrivingBackwards 状态。`db=1` 时 `GetMovingDirection()` = `ReverseDir(direction)`，若 `endTile` 在"名义前进方向"的另一侧，`j=256` 推的可能是反方向。待验判据：同一 tick 打印 `direction / GetMovingDirection() / endTile 相对方位`，看 `j` 的推进方向与 `endTile` 是否一致。
- **REG-403-B（`curPos=0`）**：`SetReservationTarget(endTile=248,133)` 之后自身 PBS 为何一格都没前进。待验：`TryPathReserve` 是否在 248,130/131/132（**只被 PAIR-AIR 放行、实际没写入预留位**的空格）处断掉——摸路的"空气"与开车时的"实际位"是两套判据，这是最可疑的一环。
- **REG-403-C（`RESV-FOREIGN-SKIP owner=3`）**：1708/1709 两行 `RESV-FOREIGN-SKIP veh=0 tile=248,128/248,129 owner=3` 出自 `ClearPathReservation()` 的防误清守卫（`src\train_cmd.cpp:15979-15995`），说明这两格的预留位**被几何回溯答成 veh=3**。它本身只是"漏放"（残留），但旁证：248,128/129 归属不明，可能让自身寻路认定"前方被占"。

### 7.4 下一步取数（一轮即可判定）

在 `LOCO-MOVE`／`RESV-END` 同一处补三列：`GetMovingDirection()`、`endTile` 相对本车的方位（+1/-1 沿轨）、`TryPathReserve` 的返回标记（`R3RGetLastReserveAttemptFlags()`）。若 `movdir` 与 `endTile` 方位相反 ⇒ 落 REG-403-A；若 `TryPathReserve` 在 248,130 处被拒 ⇒ 落 REG-403-B（此时修法应是：**站台段要么整段真写位、要么让自身寻路也认同一套"空气"豁免**，而不是只让摸路认）。
