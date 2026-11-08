# 第 396 轮（2026-11-07）新问题取证：库内 veh=27 exit=11↔7 刷屏 + 挂接候选池恒空

## 0 输入与有效性

- `build\R3R_debug.log`：约 248 KB，mtime **2026-11-07 18:12:34**，末行 tick=4184。
- 产生者 `build\openttd.exe`：mtime **2026-11-07 18:04:17**（52 458 496 B）= 第 395 轮「KI-561 方案 B + KI-563 修法 D」那一次增量构建的产物。
- ⇒ 采样有效：玩家跑的就是两个修复后的 exe，下文读数可直接用于判这两个修复。

## 1 两个修复的正判据（本轮已达成）

| 修复 | 证据 | 判定 |
|---|---|---|
| KI-561 方案 B（库口格自有位） | `:746 RESV-SELF-DEPOT-MOUTH veh=27 tile=280,126 track=0 mouth=280,126 end_a=279,126 end_b=280,126`（全日志 1 次） | **生效**：判为自有位、未写地图 |
| KI-563 修法 D（安全停车位截断） | `CPL-RES-SAFE-STOP` 7 次：`:1302/:1495/:1565/:1634/:1732/:1820/:1896`，例如 `:1302 CPL-RES-SAFE-STOP veh=0 nth=1/15 tile=253,121 td=3 sig=0 booked=2`；`:1820 ... nth=0/14 ... booked=1` | **生效**：booked 按安全位分段增长，而 `sig=0` 说明判据确实与"格上有信号"解耦（首版错误口径已消失） |
| 副作用检查 | `CPL-RES-SIGNAL-STOP` = 0 次；`CPL-RES-PARTIAL` = 0 次；`CPL-RES-FAIL` = 0 次 | 首版刷屏形态已消失；本轮未走到失败分支 |
| 挂接成功 | `:277 COUPLE-OK loco=27 rear=26 consist=21 co=1 real=1 type=1 tx=279 ty=126`、`:359 COUPLE-OK loco=0 rear=8 consist=3 ... tx=250 ty=121` | 挂接链路仍可成功（2 次） |

## 2 新现象 A（严重）：veh=27 在库格内 exit_blocked，DEPOT-STAY 每 tick 交替刷屏

原文（`:162` 起，`:2301` 止，共 230 条）：

```
DEPOT-STAY veh=27 exit=11 power=5325 speed=0 order=16 wait=0 resv=1 track=128 fully=1 stuck=0
DEPOT-STAY veh=27 exit=7  power=5325 speed=0 order=16 wait=0 resv=1 track=128 fully=1 stuck=0
```

分布（`Group-Object` 统计，全日志）：

- `veh=27 exit=7` **114 次**、`veh=27 exit=11` **113 次**、`veh=27 exit=1` 1 次
- `veh=0 exit=1` 1 次、`veh=0 exit=7` 1 次
- 首行 `:162`、末行 `:2301`

判定（探针口径见 `src\train_cmd.cpp:15266-15291` 与各 `r3r_depot_stay(n)` 调用点）：

- `exit=11` = **exit_blocked**（`src\train_cmd.cpp:15446`），条件 = `seg_state == SigSegState::Full || HasDepotReservation(v->tile)`；探针自带 `resv=1` ⇒ **`HasDepotReservation(279,126) == true`** 是直接读数，不是推断。
- `exit=7` = `++v->wait_counter < 37`（`src\train_cmd.cpp:15360`）。7 与 11 交替 ⇒ 每轮累计 37 tick 后被 exit_blocked 拒、`wait_counter` 随该路径归零、下一 tick 从 0 重新累计。
- `track=128`(=TRACK_BIT_DEPOT) + `fully=1` ⇒ 整链完整在库内；`power=5325` ⇒ 非无动力（排除 exit=2）；`stuck=0` ⇒ **未**被 `MarkTrainAsStuck`（与 KI-561 当年的 exit=12 形态不同）；`order=16` = `OT_GOTO_COUPLE`，`destStn=4`。
- ⇒ 库格 `279,126` 上的 depot reservation 一直没释放，出库闸门恒拒；同时 `ORD-IDX-WATCH site=tick-in veh=27 real=4->0`（`:196`）说明订单索引每 tick 被打回 0。

## 3 新现象 B：挂接候选池恒空（pool=0，七个过滤器命中数全 0）

```
CPL-PAIRSCAN tick=4184 act=27 state=steady ord=16 destStn=4 actTile=279,126 actSpd=0 pool=0
             s_chain=0 s_target=0 s_stop=0 s_dest=0 s_lock=0 s_ign=0 s_ignd=0
             best=21 bestDist=7 paired=-1 wait=-1 near=-1 nearDist=0 nearWhy=none
CPL-PAIRSCAN tick=4184 act=0  ... destStn=1 ... best=3 bestDist=5 paired=-1
COUPLE-FAIL loco=0 order=16 tx=248 ty=127 x=3976 y=2041
COUPLE-WAIT-OFFDEST veh=0 tile=248,127 mf=248,127 real=3 isDepot=0 destDepot=0
                    destStn=1 destTile=67832 onDepotTile=0 onStnTile=0
DEPOTBLK veh=0 tile=248,127 track=002 eng=1 isdepot=0 ontile=0 idmatch=0 pend=0
         stopped=0 parked=0 spd=0 destRaw=1 depotIdx=4294967295
```

- `s_*` 七个计数器全 0 而 `best` 仍能给出 21 / 3 ⇒ **候选在"入池之前"就被全部拒掉**，不是"没找到车"。
- `COUPLE-WAIT-OFFDEST` 明确说 veh=0 的 `GOTO_COUPLE` 目标站 1 与自身位置不符（`onStnTile=0`）⇒ 该车当前被"必须同站挂"规则挡下。
- veh=0 与 veh=2 同格 `248,127`（`LOCO-MOVE veh=0 tick=4184 tile=248,127` / `LINEND veh=2 tile=248,127`），需与 KI-562（同格两链头）比对是否同族。

## 4 疑点登记（REG-396）

- **REG-396-A（最要紧）**：库格 `279,126` 的 depot reservation 归属未知 —— 是 veh=27 自己（自订未清）还是已经死掉的链（僵尸）？直接判据缺：`HasDepotReservation()` 只回 bool，没有"谁订的"。取证方向 = 复用 `GetTrainForWholeTileReservation()` 思路对 depot 格做归属回溯，或对 `SetDepotReservation(true)` 全调用点加探针。
- **REG-396-B**：`wait_counter` 每 37 tick 归零的调用方未定位（怀疑是 `TryPathReserve` 失败路径 / `ProcessOrders` 的 park 逻辑）。
- **REG-396-C**：`DEPOT-STAY` 只在 `:162-:2301` 出现，2301 之后不再有 ⇒ veh=27 后来是出库了还是换了其它卡法？未确认（末行 tick=4184 仍有 `act=27 ... actSpd=0`）。
- **REG-396-D**：`ORD-IDX-WATCH site=tick-in veh=27 real=4->0` 每 tick 复现，与 KI-527 的"索引被回灌"是否同源未知。

## 5 玩家口径回填（2026-11-07，玩家确认 A + B 都是真实现象）

玩家原话（逐字）：

> 你看到的问题A，我这边的现象是veh27只铺设了预留，但是就是不出库，我怀疑这跟预留回溯有关。而我看到的第二个问题是，veh0的挂接寻路在某个PBS前面终止了，我怀疑这跟挂接寻路预留铺设的终点改为安全停车位而挂接寻路终点不是配对车底所在格有关。

拆成两条口径：

- **A（veh=27）**：**只铺设了预留，但就是不出库**。玩家怀疑 = **预留回溯**（回溯走完/回溯到的位置与实际不符，导致"看起来订好了但闸门不放行"）。
- **B（veh=0）**：**挂接寻路在某个 PBS 前面终止了**。玩家怀疑 = **第 395 轮修法 D 把预留终点改成"安全停车位"，而挂接寻路的真实终点是"配对车底所在格"**（两者不是同一个位置）。

⇒ B 的怀疑直接指向第 395 轮刚落的改动：`IsSafeWaitingPosition()` 判在路径**中间**某一格为真就 `break`，而挂接预留语义上必须铺到会合点（配对车底），中间任何"安全位"都不是终点。这是本轮最高优先的验证目标。

## 6 初判（待验证，勿当结论）

- **B 的机制成立性（推测根因，待验证）**：`R3RReservePathSequence()` 每格 `ReserveSingleTrack()` 成功后即问 `IsSafeWaitingPosition(coupler, stile, stdir, true, forbid_90)`
  - 挂接路径上的格绝大多数天然满足"可安全停车"（直轨、无岔口、PBS 后方）
  - ⇒ 第一次问就为真 ⇒ `booked` 只有 1~2 ⇒ 预留终点收在第一个安全位（日志 `CPL-RES-SAFE-STOP veh=0 nth=1/15 tile=253,121 booked=2` 正是此形态，且 7 次重复）
  - ⇒ 车头的预留到不了会合点 ⇒ 挂接寻路"在某个 PBS 前面终止"
  - 与原生 `ExtendTrainReservation()` 的**本质差异**：驾驶预留的终点**本来就是**安全位（车停在那里等信号）；挂接预留的终点是**会合点**，安全位只是路上的一格。同一个判据套在两个语义不同的场景上 = 判据选错对象（与第 394 轮"按信号截断"同类错误：都是拿一个"路径中间属性"当"终点"）。
- **A 的机制待查**：`resv=1` 是 `HasDepotReservation(279,126)` 的直读，但玩家看到的"只铺了预留"提示预留**确实被订上了**，问题在"订了却不放行"。两个可能面：①`seg_state == SigSegState::Full`（exit_blocked 的另一个 OR 分支，探针没单独打）；②库格预留是**自己订的**但出库路径没订成功，闸门等一个永不到来的条件。玩家的"预留回溯"猜测对应②。

## 7 取证结果（2026-11-07）：B 的怀疑**逐字成立**，已在日志里闭环

四行一组，连续 7 次逐字重复，锚点：

```
1251: CPL-PATHFOUND veh=0 found=1
1254: CPL-BEST veh=0 tile=248,133 td=1 cost=10081          ← 会合点（配对车底所在格）= 248,133
1302: CPL-RES-SAFE-STOP veh=0 nth=1/15 tile=253,121 td=3 sig=0 booked=2
1303: CPL-RES-OK veh=0 total=15 booked=2 stop_safe=1 tiles=254,121/0;253,121/3
1304: CPL-RESERVE veh=0 reserved=1 next=0 path=15 booked=2
```

判定：

- 摸路总长 **`path=15`**（起点 `254,121` 起算）**正好覆盖到会合点 `248,133`** —— 说明 YAPF 给出的路径终点就是配对车底所在格，不是别的。
- 但实际只订了 **`booked=2`** 格（`254,121` → `253,121`）就 `break`，`stop_safe=1`。
- 触发截断的 `tile=253,121` 的 **`sig=0` —— 该格没有信号**，是普通直轨被 `IsSafeWaitingPosition()` 判成"可安全停车"。
- ⇒ 预留终点从「会合点 `248,133`」被换成了「路径第 2 格 `253,121`」，差 **13 格**。车头永远到不了会合点。
- 同一形态 7 次（`:1302/:1495/:1565/:1634/:1732/:1820/:1896`），每 tick 从同一起点重订、每次都是 `booked=2` ⇒ **不推进**，与 KI-568 的 `pool=0`（候选入池前被整体拒）互相印证。
- veh=27 同族但不完全同形：`734 CPL-BEST veh=27 tile=285,127 td=8 cost=6642`（会合点 `285,127`）、`748 CPL-RES-OK veh=27 total=3 booked=3 stop_safe=0 tiles=280,126/0;281,126/5;281,127/4`、`749 CPL-RESERVE veh=27 reserved=1 next=8 path=3 booked=3` ⇒ 这次 **`stop_safe=0`（不是安全位截断）**，但摸路本身只产生 `path=3` 格、停在 `281,127`，离会合点 `285,127` 还差 4 格，`reserved=1` 却是"订了 3 格就当成功" ⇒ 与 A 的"只铺了预留但不出库"吻合。

### 7.1 结论（B）

**第 395 轮修法 D 的判据对象错了**：`IsSafeWaitingPosition()` 回答的是"这一格能不能安全停车"，用在**驾驶预留**（`ExtendTrainReservation()`）里是对的（那种预留的终点本来就是安全位），搬到**挂接预留**上就变成"路径中间第一个能停的地方 = 终点"。而挂接预留的终点语义是**会合点（配对车底所在格）**，与会合点之间隔着 13 格的合法路径被整体丢掉。

与第 394 轮首版「按信号截断」是**同一类错误的第二次**：都拿"路径中间的某个属性"冒充"终点"。第一次用语法（`HasSignal`），第二次用语义（`IsSafeWaitingPosition`），但两者都不是"会合点"。

### 7.2 结论（A，初版，已被 7.3 修正）

`CPL-RESERVE veh=27 reserved=1 next=8 path=3 booked=3` + 会合点 `285,127` ⇒ 预留只盖住 3 格（`280,126` 库口 → `281,126` → `281,127`），车要开到 `285,127` 才能挂上，中途没有任何一格被订 ⇒ 出库闸门（`CheckTrainStayInDepot` 的 exit=11）看到的是"库格有预留 + 段不满"，而路径不通 ⇒ 整列车一格不动。玩家的"预留回溯"猜测方向正确：问题不在"回溯到哪一格"，而在**回溯结论只被用来订了 3 格就收工**。

### 7.3 修正（2026-11-07，读代码 + 反证）：`path=3` 不是"摸路只走 3 格"，而是"站台/车库格不入 steps"的正常口径

代码锚点（`src/pathfinder/yapf/yapf_rail.cpp`）：

- `:1633-1638` `rp_impl` 记录 steps 的前提是 `!IsRailStationTile(cur) && !IsRailDepotTile(cur)`，只有这种 plain rail 格才 `push_back`；站台整段与车库格**按设计不订**（注释：与 `rp_last_tile` 口径一致——预留止于会合点，不越过车底）。
- `:1632` `if (cur == tg) return true;`；`:1650-1652` 站台被 `tiles_skipped` 跳过且 `R3RSkippedStationStretchContains(...)` 含 tg 时也直接 `return true`（不再 push）。
- ⇒ `path=3` 只表示"这条路线里需要逐格订的 plain rail 格有 3 个"，**不表示摸路停在 281,127**。

反证（`build\R3R_debug.log`，全部 veh=27）：

```
:340 CPLRES veh=27 tick=1491 origin=280,126 destTile=285,127 destTd=8 node=285,127 root=0 unsafe=0 nTry=3 nOK=3 empty=0
:341 RESV-TERM veh=27 real=1 type=1 tile=285,127 td=8 tcount=-2 safe=1 tail=0 spd=0 vtile=279,126
:342 RESV-END veh=27 tick=1491 from=279,126 endTile=285,127 endTd=8 endPos=106 curPos=0 endBits=0101 junction=0 real=1 spd=0
:346 TPR-EXIT veh=27 state=ok pair=0 tile=279,126 spd=0 other=0 otherTile=65535,65535 otherSpd=0 otherOrder=-1 origin=285,127 okay=1
:433 LINEND veh=27 tile=285,127 x=0 sig=1 near=0 spd=55 brk=225 cl=0 len=2 dir=5 db=0
```

- `RESV-TERM` 的 `tile=285,127`、`RESV-END endTile=285,127 endPos=106` ⇒ 预留**确实铺到了会合点**，没有被修法 D 截断（修法 D 在这里根本没触发：`CPL-RES-SAFE-STOP` 全日志 7 次全是 veh=0）。
- `TPR-EXIT state=ok ... okay=1` ⇒ **tick=1491 的出库闸门是放行的**，随后 `LINEND veh=27 tile=285,127 spd=55` 说明车已开到会合点。
- `:493/:494 RESERVECONSIST veh=21/22 tile=285,127 ... resNow=1` ⇒ 会合点整格位由 `ReserveTrackUnderConsist()` 保护，与"站台格不入 steps"互为补充。

⇒ **A 的"只铺 3 格就收工"是误读**：3 格只是 plain rail 部分，`282,127~284,127`（站台）走整格位保护。
⇒ A 的真正待查点变成：**DEPOT-STAY 刷屏的 `:162..:2301` 区间内混有一次成功出库（`:346`）**，说明该区间不是"一次连续卡死"，而是多次进出库/多次挂接解挂叠加。必须按"每一次 DEPOT-STAY 段"分别取同 tick 的 `CPLRES`/`RESV-END`/`TPR-EXIT`，才能判断某一段是真卡死。

### 7.4 A 的下一步取证口径（待执行，勿提前下结论）

- 对 `DEPOT-STAY veh=27 exit=11` 的每一段（按 tick 连续性分组），取该 tick 的 `CPLRES`/`RESV-TERM`/`RESV-END`/`TPR-EXIT` 四行。
- 判据：
  - 若同 tick `TPR-EXIT state=ok okay=1` 却仍 `exit=11` ⇒ 闸门与预留读数矛盾（高级别 bug）；
  - 若同 tick 无 `CPLRES`（=`TryPathReserve` 根本没被调到）⇒ 卡点在 `CheckTrainStayInDepot` 之前的 `Stopped`/`cur_speed` 分支，与预留无关；
  - 若 `CPLRES` 有但 `nOK=0` ⇒ 逐格预订失败，回到 KI-563 的失败分支（要 `CPL-RES-FAIL`）。
- `exit=11` 的另一半 `seg_state == SigSegState::Full` 至今**没有探针**，A 的判读必须先补这一个读数（否则 `resv=1` 与 `seg_state` 无法分离）。

### 7.5 关键洞察：为什么修法 D 不但截错位置，还会**永久**卡在同一格

- 修法 D 的判据是**绝对位置属性**（"`253,121` 这一格能否安全停车"），而挂接预留的调用点是**每 tick 从头摸路、从头订**（`:1542` 摸出整条 `r3r_resv_path` → `:1610-1614` 按序订）。
- 于是：第一 tick 停在 `253,121`（`booked=2`）；第二 tick 重新摸路，`steps` 仍是同一条，走到 `253,121` 时**同一个绝对判据再次为真**，再次 `break` ⇒ `booked` 仍是 2。日志里 7 次 `booked=2` 逐字相同、`next=0` 就是这一机制的直读证据。
- ⇒ 只要截断判据是"路径上的绝对位置属性"，无论用信号还是安全位，"逐段推进"都**不可能**发生 —— 它会停在第一处满足属性的格上永远不动。这与玩家看到的"只铺了预留但不出库"完全同形（预留看起来铺了，但没有向会合点推进一格）。
- ⇒ 正确的"逐段"必须是**相对判据**：已由本链订下的格（`HasReservedTracks(stile, bits)` 为真，或 `booked` 起点之前的格）**不再参与截断判断**，只有"本 tick 新订的格"才检查信号/冲突。这样 `253,121` 第二 tick 因为是已订格而被跳过，截断点随已订长度前移，每 tick 推进 → 最终到会合点。

### 7.6 修法候选（**未改码，等玩家拍板**）

- **E-1（推荐，最小改动）**：`R3RReservePathSequence()` 的截断判断加"已订格豁免" —— 仅当该格**尚未被本链订过**时才看信号/安全位；已订格照旧 `++booked` 直接跳过。保留 KI-563 的"遇信号停"，但把它变成可推进的"逐段"。
- **E-2（与 E-1 组合）**：把截断判据从 `IsSafeWaitingPosition()` 收窄为**只认信号**（删掉 `include_line_end` / `ft.Follow` 失败 / 非 PBS block 的误判面），因为后两条与"终点"无关，会随机把普通直轨（`sig=0`）当终点。
- **E-3（口径确认，需玩家答）**：PBS 信号在原生里**允许被预留穿过**（`src\pbs.cpp` 只在非 PBS 信号处 break）。所以要请玩家确认：挂接预留遇 **PBS** 信号到底该停还是该穿？
  - 若"该停" ⇒ E-1+E-2 即可（PBS 也停，但每 tick 推进）；
  - 若"该穿" ⇒ 截断只认非 PBS 信号，`253,121` 这种 `sig=0` 的格永不停，直接铺到会合点。
- **不建议**：退回"一次铺满全线"（会复现玩家对 KI-563 的原始抱怨，且预留越过十几个信号在观感上不可接受）。

### 7.7 交付状态

- 本轮**零源码改动、零构建**：只做只读取证 + 文档。
- 已落盘：本 memo §5~§7；`R3R_KNOWN_ISSUES.md` 第 396 轮小节（KI-563 状态改为"修法 D 证伪 ⇒ 未修，修法待拍板"、KI-567 状态补"整段非连续卡死 + 需按段分组 + `seg_state` 无探针"、REG-396-F 闭环、新增 REG-396-G「`path=3` 是站台/车库不入 steps 的正常口径」）。

---

## 8. 玩家两问的回答（2026-11-07 第二轮）

**玩家原话**：「关于B，我们是不是让YAPF终点寻路到车底所在格，预留却只延伸到下一个安全停车位就可以了。关于A，所以根因是什么，你好像没讲清。」

### 8.1 B：思路对，但要补"推进机制"，否则就是现在的死态

玩家的分层是对的，且修正了我上一轮的表述偏差：

| 层 | 终点语义 | 现状 |
| --- | --- | --- |
| YAPF 寻路（`reach()`/`rp()`） | **会合点 = 配对车底所在格** | 本来就是这个，**不用改** |
| 预留铺设（`R3RReservePathSequence()`） | **下一个安全停车位** | 修法 D 已经是这个语义，但**没有推进机制** |

⇒ 修法 D 的问题不是"截断判据该不该是安全位"，而是**判据是绝对位置属性 + 每 tick 从头摸路** ⇒ 每次都在同一格再截一次。`CPL-RES-OK ... booked=2` 7 次逐字相同就是这个证据。

所以要落地玩家的方案，**必须同时满足三条**：

1. **已订格豁免（必须）**：截断判断只对"本 tick 新订的格"生效。已由本链订下的格（`HasReservedTracks(stile, bits)` 为真）直接 `++booked` 跳过，不参与判据。否则 `253,121` 永远在同一位置再截一次。
2. **判据收窄（必须）**：中间格不能用 `IsSafeWaitingPosition(..., include_line_end=true)`。该函数除信号外还有两条会返回 true 的分支（`ft.Follow()` 失败且 `include_line_end`、"跟随器跟不下去"`new_td_bits==TRACKDIR_BIT_NONE`），这就是 `sig=0` 的普通直轨被判成"安全停车位"的来源。中间格只该认**真信号**（PBS / 非 PBS 按 8.2 口径）。
3. **停车位格计入预留**（现状已如此，保留）：`booked` 包含该格，`SetReservationTarget` 收缩到该格 —— 列车要能开到它前面停下。

### 8.2 代价：会"走走停停"，需玩家拍板

按"下一个安全停车位"推进 ⇒ 挂接机车在每个信号/安全位**停一次**，等下一 tick 续订。玩家的线路上有十几个双向 PBS，就是走走停停十几次。

三个口径选项：

- **B-1（玩家原案）**：停在下一个安全停车位，逐段推进。观感=走走停停，语义最接近原生 PBS 驾驶预留。
- **B-2（折中）**：不做"安全位"截断，改成**每 tick 最多推进 N 格**（如 8 格）。既不一次铺满全线，也不受"安全位"误判影响，机车基本不停顿。
- **B-3**：只认**非 PBS** 信号截断（原生 `pbs.cpp` 的语义：PBS 信号允许预留穿过），PBS 一律穿过。玩家第 394 轮说过"之前观察普通 block 信号也这样"，若现场两个都要停则此案不适用。

**需玩家回答 Q-396-1**：B-1 / B-2 / B-3 选哪个？（B-1 需接受走走停停）

### 8.3 A：根因是**状态机死锁**，不是"预留铺得短"

上一轮我把 A 说成"只订 3 格就收工"，这个表述是错的（§7.3 已自我修正：3 格是站台/车库不入 steps 的正常口径）。真正的根因在 `src\train_cmd.cpp` 的 `CheckTrainStayInDepot()`，链条如下：

```text
15358  if (v->force_proceed == TFP_NONE) {
15360      if (++v->wait_counter < 37) return r3r_depot_stay(7);   ← exit=7，累计 36 次
15364      v->wait_counter = 0;
15367      if (seg_state == SigSegState::Full || HasDepotReservation(v->tile)) exit_blocked = true;
15446  if (exit_blocked) return r3r_depot_stay(11);                ← exit=11，每 37 tick 一次
15449  if (seg_state == SigSegState::Path && !TryPathReserve(v) && v->force_proceed == TFP_NONE) {
15451      MarkTrainAsStuck(v);  return r3r_depot_stay(12);
15455  SetDepotReservation(v->tile, true);                          ← 放行时订下库格位
```

而 `TryPathReserve()` 内部对"库内列车"有一条硬规则（`train_cmd.cpp:18803-18809`）：

```c
/* We have to handle depots specially ... If we are still
 * inside the depot, a depot reservation can never be ours. */
if (moving_front->track == TRACK_BIT_DEPOT) {
    if (HasDepotReservation(moving_front->tile)) {
        if (mark_as_stuck) MarkTrainAsStuck(consist);
        return TPRRF_NONE;
    }
```

**死锁三步**：

1. 第 N 次走到 `:15455`，`SetDepotReservation(279,126, true)` 订下库格位 —— 这是"已被放行、正要开出去"的状态。
2. 这一 tick 之后机车**没有真正离开库格**（`v->track` 仍是 `TRACK_BIT_DEPOT`(128)、`cur_speed==0`、`speed=0` 全程）。释放点在 `src\vehicle.cpp:2745` 的 `SetDepotReservation(t->tile, false)`，只在"离开库格/清路径预留"时触发 ⇒ **从未触发，预留永久残留**。
3. 此后每 37 tick：
   - `:15367` 读到 `HasDepotReservation(279,126)==true` ⇒ `exit_blocked` ⇒ `return 11`；
   - 即使强行绕过 exit=11，`:15449` 的 `TryPathReserve()` 也会撞上 `:18807` 的"库内列车的库格预留必不可能是自己的" ⇒ `TPRRF_NONE` ⇒ `:15451 MarkTrainAsStuck` ⇒ exit=12。
   ⇒ **两条路同时封死**，机车永不出库。

**exit=7 / exit=11 交替 113:114 的成因也解释了**：`wait_counter` 每轮 36 次 exit=7、第 37 次归零后判 exit=11，然后下一 tick 又从 0 开始累计 —— 所以每个 37 tick 周期恰好 36 行 `exit=7` + 1 行 `exit=11`，与日志分布吻合（`stuck=0` 是因为 exit=11 分支根本不调 `MarkTrainAsStuck`）。

**第一因（尚未定位，留作 U-396-1）**：第 1 步"已被放行"之后，机车为什么没产生速度/没离开库格？候选：
- U-396-1a：放行票被后续逻辑作废（如挂接订单在库内被 `r3r_depot_stay(1)` 早退，见 `:15312-15315` 的"库型 GOTO_COUPLE 到库后什么都不发生"），于是 `SetDepotReservation(true)` 已置位但列车被挡住；
- U-396-1b：KI-348 那条"`cur_speed==0` ⇒ `j<adv_spd` ⇒ 永不进移动分支"的停驶分支（第 242 轮），停车后预留既无人推进也无人释放。

**注意 `:15312-15315` 这个早退**（`OT_GOTO_COUPLE` + `GetCoupleIsDepot()` + 整链在库 ⇒ `r3r_depot_stay(1)`）—— `exit=1` 在日志里恰好出现 1 次（veh=27）。若 veh=27 是"库型挂接"，这条早退会让它在库内被永久钉住，但那时 `resv` 怎么会是 1？只有 `:15455`/`:19104` 两处会置位，说明**确实先被放行过一次**。⇒ 顺序是"先放行 → 再被挡住"。

### 8.4 A 的修法候选（未改码，待拍板）

- **A-1（最小自愈，推荐先做）**：`CheckTrainStayInDepot()` 在 `:15367` 判定前先做一次"库格预留自愈" —— 若整链在库、`cur_speed==0`、且库格预留的持有者回溯到本车（`R3RReservationHeldBySelf()` 同源判据已有库口豁免），就 `SetDepotReservation(v->tile, false)`。理由：库内停住的列车**不应**持有库格预留（`:18807` 的注释就是这个不变量），释放它不违反原生语义。
- **A-2（治本）**：查清 U-396-1a/1b，让"放行后没走成"的那条路径不再置位、或置位后立即回滚。若第一因是 `:15312` 的库型挂接早退，则早退必须发生在 `SetDepotReservation(true)` **之前**（当前顺序不满足，可能是历史残留）。**需先补探针**：`SetDepotReservation` 两个置位点（`:15455`/`:19104`）与释放点（`vehicle.cpp:2745`）各打一行 `RESV-DEPOT-SET/CLR veh= tile= why=`。
- **不建议**：给 exit=11 加"自有预留不算阻断"的豁免 —— 那只是让列车能出库，库里那一位仍然残留，会让**别的**列车进不了这个库（同一格只有一位）。

### 8.5 待玩家回答

- **Q-396-1（B 口径）**：B-1 停在安全位（走走停停）/ B-2 每 tick 最多 N 格 / B-3 只认非 PBS 信号截断？
- **Q-396-2（A 修法）**：先做 A-1 自愈，还是先补 `RESV-DEPOT-SET/CLR` 探针查第一因？（可同时做：A-1 不依赖第一因）
- **Q-396-3（A 现场）**：veh=27 的订单是"库型挂接（`GetCoupleIsDepot()`）"还是"出库→站台挂接"？日志 `CPL-PAIRSCAN ... destStn=4` 像是后者，但 `exit=1` 那一次说明它一度满足库型条件。

### 8.6 「已放行却没离库」的机制回答（2026-11-07，玩家追问，代码已确证）

**结论先说：`:15455` 的"放行"不是状态，而是一张"只对当前 tick 有效"的通行证。**

放行分支 `:15455-15477` 做四件事，然后 `return false`：

```15455:15477:src/train_cmd.cpp
	SetDepotReservation(v->tile, true);
	...
	Train *moving_front = v->GetMovingFront();
	moving_front->track = AxisToTrackBits(DiagDirToAxis(DirToDiagDir(moving_front->direction)));
	moving_front->vehstatus.Reset(VehState::Hidden);
	moving_front->UpdateIsDrawn();
	R3RSpdCtx(v, "leave-depot", v->cur_speed, 0);
	v->cur_speed = 0;
	...
	return false;
```

`return false` 的含义仅是"**本 tick** 不把列车算作留在库里"，让 `TrainLocoHandler`（`:22959-22962`）继续往下走到移动代码。**位移一点都不在这个函数里产生**，完全依赖紧随其后那段移动代码在同一个 tick 里把车推过库格边界。

而 `return true`（exit=7 / exit=11）意味着 `TrainLocoHandler` 立刻 `return true`：

```22957:22962:src/train_cmd.cpp
	{
		R3RScopeTimer r3r_dep_timer(&r3r_dep_ns, &r3r_dep_calls);
		if (CheckTrainStayInDepot(consist)) {
			if (consist->current_order.IsType(OT_WAITING)) R3RWaypointExitProbe(consist, "depot");
			return true;
		}
	}
```

⇒ 整个 tick 一步不动。**exit=7 与 exit=11 都是"完全冻结 tick"，唯一能动的只有放行那一个 tick。**

于是链条是：

1. 放行那一 tick：订下库格位、把 `moving_front->track` 从 `TRACK_BIT_DEPOT`(128) 改成普通轨道位、`v->cur_speed = 0`、`return false`。
2. 但 `cur_speed` 刚从 0 起步，本 tick 的 `j = UpdateSpeed()` 过不了 `j < adv_spd`（第 242 轮 KI-348 已实证的停驶分支），**这一 tick 实际没有产生位移**；`v->track` 仍是 128。
3. 下一 tick 重新进 `CheckTrainStayInDepot`：`:15318-15320` 要求整链每节都 `track == TRACK_BIT_DEPOT && tile == v->tile`，此刻成立 ⇒ 不 return false；`:15360` 的 `wait_counter` 已被放行时归零，重新累计 37 tick（这 37 tick 全是 exit=7，全部 `return true` ⇒ 一动不动）。
4. 第 37 tick 走到 `:15367`，读到**自己刚在步骤 1 订下的那一位** ⇒ `exit_blocked` ⇒ `:15446 return 11` ⇒ 又是 `return true`。
5. 从此每个 37 tick 周期重复 3-4，永不产生位移 ⇒ `vehicle.cpp:2745` 的释放点永远等不到"离开库格"事件。

**与上游的差别只有一处**：上游 `:15367` 没有 `HasDepotReservation()` 这个条件，所以每 37 tick 都会**再放行一次**（再给一个 `return false` 的 tick），几十个周期内必然把车推出去；R3R 加的这一条把"第二个周期起的所有通行证"全部作废，于是唯一那张一次性通行证一旦没走成，就永久卡死。**这不是"放行没生效"，而是"放行只发一次，而驶出库格需要连续多个 tick 的放行"。**

**仍有一处分歧未定（U-396-1c，三选一，需 3 行探针）**：按 `:15318` 的循环，若放行时 `moving_front == v`（即 `v->track` 已变成普通位），下一 tick 第一节就不满足条件 ⇒ 立即 `return false`，根本到不了 `:15367`，车应当正常驶出。现场 `track` 恒 128 说明下列之一成立：

- (a) `moving_front != v`（`GetMovingFront()` 在 `IsDrivingBackwards()` 时返回 `Last()`），改的是链尾的 track，链头 `v` 仍 128 ⇒ 下一 tick 重进库逻辑；
- (b) 那次放行后车确实动过、又整链回到 128（重新进库/被别的路径写回）；
- (c) 那一位**不是本车订的**（`HasDepotReservation` 是全格位，邻车、`ReserveTrackUnderConsist`、残留存档位都会算）。

判据：取日志中 `exit` 由非 11 变为 11 的那一行前 3 行，看是否出现 `LOCO-MOVE veh=27` / `TRP veh=27` / `REVERSEDIR`，并打一行 `RESV-DEPOT-SET/CLR veh= tile= why=` 区分 (a)/(c)。

### 8.7 「普通出库」与「挂接出库」的分歧点（2026-11-07，玩家追问，代码已确证）

**玩家的猜测（"extendreserve 时挂接寻路多花几个 tick"）方向对但归因错位：多花的不是 tick，是多跑一遍寻路；而且在两种情况下根本不是同一条路径。**

三条分支必须先按订单拆开看，`CheckTrainStayInDepot` 只在其中两条里出现：

#### (1) 普通出库（GOTO_STATION / GOTO_DEPOT / GOTO_WAYPOINT）

`:15449 TryPathReserve(v)` → `TryPathReserveWithResultFlags()` → `R3RTryPathReserveCore()`，`ChooseTrainTrack()` 内顺序：

```17306:17475:src/train_cmd.cpp
TileIndex origin = FollowTrainReservation(consist, ...);   // :17306 顺着已有预留走
const TrackBits res = ExtendTrainReservation(consist, origin, ...);  // :17310 ← 唯一一次寻路
...
if (consist->current_order.IsType(OT_GOTO_COUPLE) && !r3r_couple_depot) {  // :17459
    Track path_found = DoTrainCouplePathfind(consist, true);               // :17475
```

**只跑 `ExtendTrainReservation` 一次**，成功就 `return`，永远进不了 `:17459` 的挂接分支。

#### (2) 站台挂接出库（`GetCoupleIsDepot() == false`）

`ExtendTrainReservation`（`:17310`）**照样先跑**，而它的目的地来自**订单**（`dest_tile` / 站台整格），跟"车底在哪"无关；等待车底踩着的站台整格预留对它是障碍 ⇒ 常常返回 `INVALID_TILE` ⇒ 在 `:17311` 就早退，**根本到不了 `:17475`**。这正是 KI-457 要加"停住的挂接机车不吃早退"豁免的原因（注释自陈：挂接寻路的唯一理由就是普通寻路把等待车底的平台预留当障碍、必然失败）。

侥幸通过后，`:17475` 再跑 `DoTrainCouplePathfind` → `YapfTrainCoupleTrack`；**真失败时还要跑第三次**：

```17486:17494:src/train_cmd.cpp
 * 兜底口径：挂接寻路失败时，把同一格交给**普通寻路**再算一次。普通寻路的
 * 安全位回溯（FindSafePositionOnNode）只判几何、不逐格 TryReserveRailTrack，
```

⇒ 站台挂接出库最坏是 **3 次寻路**（Extend + Couple + 普通兜底），普通出库是 **1 次**。

#### (3) 库型挂接出库（`GetCoupleIsDepot() == true`）

**不走 `TryPathReserve`，也就没有 Extend/Couple 之争**：

```15312:15315:src/train_cmd.cpp
if (v->current_order.IsType(OT_GOTO_COUPLE) && v->current_order.GetCoupleIsDepot() &&
		IsRailDepotTile(v->tile) &&
		v->current_order.GetDestination().ToDepotID() == GetDepotIndex(v->tile) &&
		R3RChainFullyInDepot(v)) return r3r_depot_stay(1);
```

这是**第二套放行机制**：车被钉在库里（exit=1），由库内几何命中（`GetCouplePosition` / `TryTrainCouple` 的 depot 分支）完成挂接；挂上之后订单变成普通订单，下一轮才走 (1) 的普通出库。**库型挂接与普通出库的分歧是"两套机制"，不是"多几个 tick"。**

#### tick 数差异的真实来源（只有挂接才有）

1. `R3RCoupleScanHold`：`YapfTrainCoupleTrack` 每次空结果后节流 **8 tick**（`:14583` 注释自陈 "throttled after every empty result by R3RCoupleScanHold(), 8 ticks"）。普通寻路没有这层节流。
2. 只订到安全位：挂接的 `rp()` lambda 回溯时逐格订，终止于"下一个安全停车位"（现场 `booked=2`），走完就停、下一 tick 重摸；普通出库一路铺到目的地。
3. `ExtendTrainReservation` 那一次在挂接场景基本是白跑（注定撞平台预留）。

**一句话回答**：分歧不在 `ExtendTrainReservation` 慢，而在**目标由谁决定** —— 普通出库的目标是订单目的地（静态，一次算完）；挂接出库的目标是"车底当前所在格"（动态、每 tick 会动），所以必须"Extend 白跑一次 + Couple 重算 + 8 tick 节流 + 只订到安全位"，表现成多花很多 tick。库型挂接则是完全不同的第三套机制（库内等命中，不寻路）。

**附带结论（对 A 的意义）**：库里那台 veh=27 若是**库型挂接**，`exit=1` 就是它的**正常状态**（等库内命中），不是故障；真正的故障只在"命中条件永远不成立"时（如 `:15312` 的 `R3RChainFullyInDepot` 不满足、或目标车底不在本库）。这条与 U-396-1c 并列，需先确定 veh=27 的 `GetCoupleIsDepot()` 取值。

### 8.8 玩家建议「给出库挂接已配对列车多一点等待时间」的评估（2026-11-07，未改码）

**玩家原话**：*"那么出库挂接就会搞很久，但是出库的时机只有1tick，因此，我建议：对于出库挂接且已成功配对的列车，给它多一点等待时间"*

#### 结论：**延长等待时间方向无效，现场是"每周期都被自己的位挡住"，不是等得不够久**

代码锚点（本轮确证，`:15358-15446`）：

```15358:15369:src/train_cmd.cpp
	if (v->force_proceed == TFP_NONE) {
		/* force proceed was not pressed */
		if (++v->wait_counter < 37) {
			return r3r_depot_stay(7);
		}

		v->wait_counter = 0;

		seg_state = _settings_game.pf.reserve_paths ? SigSegState::Path : UpdateSignalsOnSegment(v->tile, DiagDirection::Invalid, v->owner);
		if (seg_state == SigSegState::Full || HasDepotReservation(v->tile)) {
			/* Full and no PBS signal in block or depot reserved, can't exit. */
			exit_blocked = true;
		}
	}
```

三个事实：

1. `wait_counter` 在 **`:15364` 就被归零**（判 exit_blocked **之前**），所以 `exit=11` 那一 tick 也把计数清零了 ⇒ 下一个周期重新等 37 tick。**阈值 37 改成 300、3000，只是让每个死循环周期变长**，永不放行 ⇒ 更卡。
2. `exit=11` 的成因是 `:15367 HasDepotReservation(v->tile)` 读到**自己在 `:15455` 订下的那一位**（放行时 `SetDepotReservation(v->tile, true)`）。这是 R3R 自锁，与等待时长无关。
3. 现场日志是**连续多次 exit=11**（§8.6 步骤 4），说明每个周期都在 `:15446` 就 `return true`，**一次都没走到 `:15455` 的放行**。等待时间再多也换不来第二次放行。

#### 为什么"允许再放行一次"就够（关键机制）

放行 tick 做了一件决定性的事：`moving_front->track` 从 `TRACK_BIT_DEPOT`(128) 被改成普通轨道位（`:15463-15464`）。而下一 tick 重进本函数时，`:15318-15320` 的循环是：

```15317:15320:src/train_cmd.cpp
	/* bail out if not all wagons are in the same depot or not in a depot at all */
	for (const Train *u = v; u != nullptr; u = u->Next()) {
		if (u->track != TRACK_BIT_DEPOT || u->tile != v->tile) return false;
	}
```

只要**任何一节**的 track 不是 128，就 `return false` ⇒ **该 tick 完全不走库逻辑**、直接进移动代码。⇒ 每一次成功的放行都自动换来"下一 tick 脱离库逻辑"，即使 `moving_front != v`（U-396-1c 的 (a)）也成立。这就是上游"每 37 tick 无条件再放行一次"能把车推出去的全部原理，而 R3R 的 `HasDepotReservation` 条件把第二个周期起的放行全部取消了。

#### 三个候选修法（按贴合玩家意图与风险排序）

- **R-396-1（推荐，最小且精确对准"再多给机会"）**：给"本车在本库格已放行但尚未离库"一个短期豁免。新增文件内 `static std::unordered_map<uint32_t, uint32_t> _r3r_depot_release_tick`；`:15455` 之后记 `[v->index.base()] = _tick_counter`；`:15367` 的判据改为
  `if (seg_state == SigSegState::Full || (HasDepotReservation(v->tile) && !R3RDepotSelfReleased(v)))`
  其中 `R3RDepotSelfReleased(v)` = 表内有本车记录且 `_tick_counter - t < 某上限（如 256）`；`vehicle.cpp:2745` 的离库释放点与 `VehicleEnterDepot` 里清表。效果：每 37 tick 都能再放行，直到驶出库格；不改变"别的车占着库时不许出去"的语义（那条仍由 `seg_state == Full` 与 HasDepotReservation 的**第一次**判断把关）。
- **R-396-2（治根，风险中）**：放行时不仅改 `moving_front->track`，而是**整链**都写成普通轨道位（或至少按 `:15318` 保证至少一节非 128 的语义显式写出），让"放行即脱离库逻辑"不依赖 `GetMovingFront()` 的返回值。需要复核库格 `track` 位语义与 `UpdatePosition()` 的一致性。
- **R-396-3（玩家原话的可用变体，最保守）**：仅在"库型/站台挂接已配对"这一情形放宽——`R3RGetCouplePairPartner(v) != nullptr` 时，把 `:15360` 的 37 换成更小的值（如 1~4），等价于"挂接车出库试探更频繁"。**这只是把周期缩短，不给新通行证**，单独用无效，只能与 R-396-1 叠加。

#### 需要玩家拍板（Q-396-4）

- **Q-396-4a**：走 R-396-1（自己放行过就不算阻塞，等价于上游"每 37 tick 再放行"），还是 R-396-2（放行时整链改 track，一次性脱离库逻辑）？
- **Q-396-4b**：R-396-1 的豁免上限取多少？（建议 256 tick ≈ 7 个等待周期；超过则视为真阻塞、照旧 exit=11）
- **Q-396-4c**：豁免是否只给"挂接已配对 + 库型/站台挂接"订单（玩家原话），还是给所有出库车（更贴近上游，但会影响普通出库的行为面）？

**注**：在拍板前，玩家可用 `v->force_proceed`（界面 force proceed 按钮）验证该车是否能开出库格——`:15358` 的 force 分支（`:15371-15373`）完全跳过 `wait_counter` 与 `exit_blocked`，若按 force 后能出库，则 R-396-1/R-396-2 的机理成立、可放心落地；若按 force 也出不去，则 U-396-1c 的 (b)/(c) 成立，得先补 `RESV-DEPOT-SET/CLR` 探针。

### 8.9 玩家拍板：走 R-396-1（2026-11-07）

**玩家原话**：*"按照R-396-1来吧"*

#### 执行口径（按 R-396-1 原文，不扩不缩）

- 判据：`:15367` 的 `HasDepotReservation(v->tile)` 对"**本车已在该库格放行过、且尚未离库**"的列车豁免。
- 范围：**不限订单类型**（R-396-1 原文如此，也是最贴近上游 JGRPP"每 37 tick 无条件再放行一次"的做法）。Q-396-4c 的"只给挂接已配对"分支不落地。
- 豁免上限：**256 tick**（≈ 37 tick × 7 个等待周期），超过则视为真阻塞、照旧 `exit=11`。
- 记录点：`:15455 SetDepotReservation(v->tile, true)` 之后记 `(tile, _tick_counter)`。
- 清除点：车离开库格（`SetDepotReservation(v->tile, false)` 那一侧）与重新 `VehicleEnterDepot()` 时清；另用 `(tile, tick)` 双字段校验，防索引复用误判。
- 语义不变项：`seg_state == SigSegState::Full` 照旧无条件阻塞；别人的库格预留照旧阻塞（豁免只对"本车自己那一次放行"生效，即首次判断仍由 `HasDepotReservation` 把关；豁免窗口内车仍在库格上，别的车不可能进入该格）。

#### 为什么这样就能出库（机理复述，落地依据）

放行 tick 把 `moving_front->track` 从 `TRACK_BIT_DEPOT` 改成普通轨道位（`:15463-15464`）；下一 tick 的 `:15318-15320` 循环只要发现**任何一节** track ≠ 128 就 `return false` ⇒ 该 tick 完全不走库逻辑、直接进移动代码。所以每拿到一次通行证就自动换来"下一 tick 脱离库逻辑"，几十个周期内必然推出库格。

#### 复测判据（4 条）

1. 现场复跑：`exit=11` 不再无限重复；同一台车在 256 tick 内出现多次放行（新增探针 `DEPOT-SELF-RELEASE`），最终 `track` 从 `0x80` 变为普通轨道位并驶离库格。
2. `exit=7`（等 37 tick）与 `exit=1`（库型挂接）行为不变。
3. 负判据：库里被**别的**车订着时（`HasDepotReservation` 由他人持有）仍出不去（`exit=11` 照常）。
4. 负判据：普通出库（非挂接）行为无回归——每 37 tick 仍只放行一次，日志无额外 `DEPOT-SELF-RELEASE` 刷屏。

#### 实现落地（2026-11-07，仅改 `src\train_cmd.cpp`，未碰 `src\*.h`）

四处改动：

1. **新增 helper**（插在 `R3RChainFullyInDepot()` 之后、`CheckTrainStayInDepot()` 之前）：
   `struct R3RDepotSelfReleaseRecord { TileIndex tile; uint32_t tick; }` + `static std::unordered_map<uint32_t, R3RDepotSelfReleaseRecord> _r3r_depot_self_release` + `R3R_DEPOT_SELF_RELEASE_TICKS = 256` + `R3RMarkDepotSelfRelease(const Train *)` + `R3RDepotSelfReleased(const Train *)`。索引/格一律用 `v->First()` 归一（两处调用方传的车不同，链头必须一致）；表超过 1024 条时顺手剔除过期项。
2. **`:15367` exit=11 判据**：改为 `if (seg_state == SigSegState::Full || (depot_reserved && !self_released))`，其中 `depot_reserved = HasDepotReservation(v->tile)`、`self_released = depot_reserved && R3RDepotSelfReleased(v)`；豁免命中时打 `DEPOT-SELF-RELEASE veh= tile= wait= order=`（同车 256 tick 内只打一行，避免刷屏）。
3. **`:15455` 放行点**：`SetDepotReservation(v->tile, true)` 之后立即 `R3RMarkDepotSelfRelease(v)`。
4. **`:18807` TryPathReserve 的同一句自锁**：改为 `if (HasDepotReservation(moving_front->tile) && !R3RDepotSelfReleased(consist))`。**这是 R-396-1 的必要组成部分，不是额外扩大范围**：不改这里，`exit=11` 放行之后会立刻在 `:18807` 拿到 `TPRRF_NONE` → `MarkTrainAsStuck` → `exit=12`，车仍然出不去（§8.3 已预告）。

记录失效机制：车离格后 `head->tile` 变化 ⇒ 记录不匹配 ⇒ 豁免自动失效；无需跨文件清表（`VehicleEnterDepot()` 的 `SetDepotReservation(false)` 也不必通知本表）。`VehicleEnterDepot()` 清位后 `HasDepotReservation` 为假，本就不走豁免分支。

**刻意未改的第三处**：`:15379` 的 `if (!HasDepotReservation(v->tile)) VehicleEnterDepot(v);`（"GOTO_DEPOT 到同一库格再入库"的 service 分支）属另一条语义路径，订单是 `OT_GOTO_DEPOT` 而非出库，本次不动，登记为残留观察点 R-396-5。

**构建**：`R3R_fullbuild.cmd inc detach`（复用既有入口，未新建任何 .cmd）；`tasklist` 确认 openttd.exe 未运行后启动。

