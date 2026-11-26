# 第 525 轮：挂接预留为什么"不像原版"—— 原版循环 vs 挂接循环的逐项对照（含两项落地修法）

**玩家口径（2026-11-27 原话）**：

> 「我的需求的核心是：尽可能像原版，具体表现为：原生（没开真实制动的）铺设预留，是先检查能否铺设预留到下一个安全位，然后再铺设到下一个安全位，然后再尝试铺设预留。而挂接寻路，则应当同样遵循"先检查能否铺设预留到下一个安全位，然后再铺设到下一个安全位，然后再尝试铺设预留"，但实际上，日志里表现出的行为是我难以描述的总之十分不符合构想的。」

本轮任务 = 把"不符合构想"翻译成可判定的代码差异，并落地其中两项不依赖玩家拍板的部分。

---

## 一、输入与效度

- `build\R3R_debug.log`：3121 行 / 287 393 B / mtime 2026-11-26 22:34。
- 产生者 `build\openttd.exe`：54 409 728 B / mtime 21:59:21（第 523 轮续四「纯刹车」版 + 第 524 轮的 KI-684 刹车修复）。
- 日志晚于 exe ⇒ 采样有效。尾车 veh=15 停在 `1455,1873`、`spd=0`、`stuck=1`、`wait=0..62` 递增。

---

## 二、原版（未开真实制动）的预留循环 —— 一次"检查→铺到安全位→再试"

代码锚点全部在 `src\train_cmd.cpp` 的 `ExtendTrainReservation()`（:17222 起），由 `ChooseTrainTrack()` 在 :18735 调用：

| 步骤 | 位置 | 内容 |
|---|---|---|
| ① 从"自己预留的末端"起步 | :17297-17299 | `tile = origin.tile; cur_td = origin.trackdir;`，`origin` 来自 `FollowTrainReservation(consist)`（:18731）—— **永远从自己链条的末端往前接，不从车脚下重铺** |
| ② 逐格前进 | :17324 `while (ft.Follow(...))` | 一步一格，跨桥隧时 `tiles_skipped` |
| ③ 岔口/站台/路点 ⇒ 停手交给寻路器 | :17363-17477 | `target_seen \|\| KillFirstBit(new_td_bits) != NONE` ⇒ `okay=false` 返回，已铺的保留 |
| ④ 安全位判定 | :17512 | `IsSafeWaitingPosition(v, tile, cur_td, ...)` |
| ⑤ 安全位必须先"空"才铺 | :17514-17515 | `IsWaitingPositionFree(...) && TryReserveRailTrackdir(v, tile, cur_td)`—— 这就是"**先检查能否铺设预留到下一个安全位**" |
| ⑥ 中途逐格真订 | :17560 | `if (!TryReserveRailTrackdir(v, tile, cur_td)) { reason="reserve-fail"; break; }` |
| ⑦ 失败才整段回滚 | :17577-17640 | 只回滚"本次试铺"这一段；自己原有的预留一格不动 |

**语义**：每次扩展都是一小段、以"下一个安全位"为终点、铺不进去就退回来等下一 tick。预留永远是自己链条的**真实延长**。

---

## 三、挂接寻路的实际循环 —— 全路径试订 → 一次性整段铺 → 任一格不通则整条作废

代码锚点全部在 `src\pathfinder\yapf\yapf_rail.cpp`：

| 步骤 | 位置 | 内容 |
|---|---|---|
| A 全图 A* 找"能挂的车底" | :1833 `FindPath` | 终点判据 = `CYapfDestinationTrainRailT`（站台/预留 + 合法配对目标） |
| B **回溯段逐节点试订试撤** | :2021-2048 `while (pNode->parent != nullptr) { ... CheckSafePositionOnNode(pPrev) ... }` | 路径上**每一个节点**都要过 `FindSafeCouplePositionProc()`（:252-599） |
| B1 试订 | :451 `TryReserveRailTrack(tile, TrackdirToTrack(td))` | 在判据内部**真写地图** |
| B2 试撤 | :597 `UnreserveRailTrack(tile, TrackdirToTrack(td))` | 通过就立刻撤掉 |
| B3 任一格不通 ⇒ 整条路径作废 | :2033-2047 → `CPL-SAFE-FAIL` → `return INVALID_TRACKDIR` | 注意：**判据覆盖的是全程，不是"下一个安全位"** |
| C 再走一遍只读摸路 | :2175-2283 `rp()` | 产出物理格序 `r3r_resv_path` |
| D 一次性整段铺 | :2303-2313 → `R3RReservePathSequence()`（:744-1086） | 逐格 `ReserveSingleTrack()`，遇失败格就停、保留已订 |
| E "目的地即终点" | :1044-1056 | 只剩"最后一格"收尾，中段不再判安全位（REG-504-E） |

**语义**：挂接预留是"**先证明整条路每一格都订得上（两次真写+真撤），再把整条路一次订下来**"；任何一格订不上 ⇒ 一条都不订、且不承认半条。

### 3.1 差异清单（这就是"不符合构想"的可判定形式）

| 编号 | 原版 | 挂接 | 后果 |
|---|---|---|---|
| **D1** | 检查对象 = **下一个安全位**（一格） | 检查对象 = **路径上每一格的每一个节点** | 远端一格被别人占 ⇒ 车连眼前 7 格都不肯走 |
| **D2** | 检查 = 一次 `IfWaitingPositionFree` + 逐格 `TryReserveRailTrackdir` | 检查 = **两次真写地图再撤**（B1/B2 每节点一次） | 多出来的写/撤就是孤儿位的温床（KI-689） |
| **D3** | 失败 ⇒ 退回本次这一小段，自己原有预留不动 | 失败 ⇒ `INVALID_TRACKDIR`，本 tick **一格都不铺** | 车原地不动、每 tick 重算同一条路 |
| **D4** | 预留永远是"真实位" | 伙伴的位按 `RESV-PAIR-AIR` **当空气**（`pbs.cpp:598-605`，`return true` **但不写地图**），而 `R3RReservePathSequence` 把它算进 `booked`（`yapf_rail.cpp:949/1023`） | `CPL-RES-OK booked=20` 可以是**假账**：地图上一格都不是自己的 |
| **D5** | 无"孤儿位"概念（位一定属于某人） | 没有回收器（第 243 轮续十二的 `R3RReservationAudit()`/`RESV-REAP` **在当前源码里已不存在**，`grep` 零命中；日志 `RESV-AUDIT-*`/`RESV-REAP` 亦零条） | 一旦出现无主位，永久挡路 |

---

## 四、本现场读数与因果链（行号为 `build\R3R_debug.log`）

1. `:2845 CPL-RES-OK veh=15 total=20 booked=20 … tiles=1455,1874/4;1455,1873/5;…;1447,1874/2;…`
   `:2847 CPL-RET veh=15 tick=41111 tile=1455,1874 … ret=0x10 why=nores-final spd=160`
   ⇒ **铺了一整段，返回却是"无预留"**（D4 的直接后果）。
2. `:2849 RESV-PAIR-AIR veh=15 tile=1455,1874 track=4 kind=rail`
   ⇒ 自己脚下的这一格被判定成"伙伴的位"（链条一直连到车底 18，见 §3.1 D4）。
3. `:2851 RESV-HAVERES veh=15 tile=1455,1873 enterdir=3 res=0x20 tracks=0x20 spd=160 … foreign=1`
   ⇒ 车头前方那格的 `TRACK_RIGHT` 被当成"别人的预留" ⇒ **行驶侧拒绝延伸**。
4. `:2927 CPL-RES-FAIL veh=15 nth=0/19 tile=1455,1873 td=13 want=5 booked-before=0 … res=32 owner=-1 selfheld=0`
   ⇒ 挂接侧连第一格都订不上，而占位者是 **owner=-1**（无主）。
5. `:2928 CPL-RESERVE veh=15 reserved=0 next=13 path=19 booked=0`
6. `:2942 R3R ... CPL-NOOWNRESV veh=15 tile=1455,1874 spd=160 … front=1455,1874 end=1455,1874 reason=no-own-reservation`
   ⇒ 自己链条长度 0 ⇒ 引擎判定"无预留" ⇒ 停车（`CPL-S0-CHK stuck=1` 之后 wait 一路涨到 62）。
7. 另一处无主位：`:3003 FSCP tile=1447,1874 fail=tryReserve … res=0x1` + `:3004 FSCP-RES … owner=-1` + `:3005 FSCP-REACH-NONE`
   ⇒ 同一机制的第二份证据（第 524 轮 KI-689 已登记）。

**一句话**：本 tick 的挂接预留是**假账**（D4）——地图上真正属于自己的位是 0 格；`RESV-HAVERES foreign=1` 于是把车钉死；而唯一能让它重新前进的两条路（重订/延伸）都被**无主位**（`:2927` 的 `TRACK_RIGHT`、`:3003` 的 `TRACK_X`）挡住，且系统里**没有孤儿回收器**（D5）。

---

## 五、本轮落地（两项，均不删任何既有检查）

### 修法 1（D5）：无主预留位在"预订失败点"当场回收并重试一次
- 新函数 `R3RReapOrphanReservationBits(TileIndex)`（`src\pbs.cpp`，紧接 `TryReserveRailTrack()` 之后；不改 `.h`，沿用本树"手写 extern"的既有做法）。
- 判据（三条同时成立才清，且只清**冲突的那一根 Track**）：
  1. `IsPlainRail(tile)`（站台/车库/桥隧/道口一律不碰）；
  2. `GetTrainForReservation(tile, t) == nullptr`（顺预留找主人找不到）；
  3. 遍历所有链头，`TrainReservationPassesThroughTile(tr, tile)` 全为 false（没有任何链的可达范围覆盖该格——与既有探针 `FSCP-REACH-NONE` 同一把尺子）；
  4. 该格上**没有任何车节压在这根 Track 上**（`tr->track & TrackToTrackBits(t)` 为空）—— 保证绝不清掉车脚下的位。
- 调用点：`CYapfReserveTrack::ReserveSingleTrack()`（`yapf_rail.cpp:670-695`）普通轨分支 `TryReserveRailTrackdir` 失败后 ⇒ 清理 ⇒ **重试一次**；只有重试仍失败才记 `res_fail_tile`。挂接摸路与普通驾驶预留共用这一入口。
- 探针：`RESV-ORPHAN-CLEAR tile=..,.. track=.. reason=ownerless`。

### 修法 2（D1/D3）：挂接回溯不再"任一格不通 ⇒ 整条作废"，改为**截断到最后一格已验证位**
- `FindNearestCoupleTrain()` 的回溯循环（:2021-2048）：`CheckSafePositionOnNode(pPrev)` 失败时不再 `return INVALID_TRACKDIR`，而是记 `r3r_trunc_tile = pNode->GetLastTile()`（= 失败段之前的最后一格）并继续回溯（更靠起点的失败会覆盖成更近的截断点）。
- 循环结束后：
  - 截断位 == 起点 ⇒ 没有任何可铺前进的格 ⇒ 维持旧行为（`_r3r_couple_path_blocked = true` + `INVALID_TRACKDIR`，由 train_cmd.cpp 走"原地等"）；
  - 否则把摸路终点 `tg` 换成截断位 ⇒ 只铺到截断位为止，车开到那里停住，下一 tick 重算续铺（**与原版"铺到下一个安全位再试"同构**）。
- 探针：`CPL-SAFE-TRUNC veh=. from=. tile=.`（每次截断一行）。

---

## 六、仍未解决（需玩家拍板的结构性问题，本轮不动）

**P-525-A（D4，根子）**：`RESV-PAIR-AIR` 把"伙伴的位"和"自己铺到伙伴门口的位"混为一谈。因为预留位**不记主人**，`FollowReservation()` 一路走到底，只要自己的链在几何上接到车底的预留，整条链的归属就被读成车底（`GetTrainForReservation` 返回链末端那列车）。于是：
- 自己铺的 20 格被读成"伙伴的"⇒ 不写地图（假账）⇒ 自己没有预留 ⇒ `no-own-reservation` 停车；
- 反方向也一样：只要两条链相接，`R3RReservationHeldBySelf()` 也认不出来（`:2420-2422` 任一方向找到的头不是自己就返回 false）。

三条候选（本轮未实施）：
- **A-1 断开接触**：挂接预留**止于会合点的前一格**，绝不把位铺到与车底预留相邻的位置；靠"相邻即可挂"的判定完成耦合（需要确认 `TrainCoupleHandler` 的耦合距离阈值，本轮未查）。
- **A-2 让等待车底释放尾部之外的位**：本现场车底的预留一直伸到 `1441,1874`（`:2983 owner=18 whole=1`），把会合点 `1438,1874` 也盖住了 ⇒ 机车永远订不到真正的最后一格。若等待车底在 WAIT_COUPLE 状态下只保留"车体压住的格"，会合点就能被真实预订。
- **A-3 改归属判据**：`FollowReservation()` 遇到车节即停（引擎级改动，影响所有列车，风险最高）。

**P-525-B**：`booked` 的语义（`CPL-RES-OK`）仍是"含假账"，读日志时必须用 `CPL-RES-PARTIAL-TILE` / `RESV-*` 交叉核对。

---

## 七、复测判据（修后）

1. 日志出现 `RESV-ORPHAN-CLEAR`（至少一次，`1447,1874` 或 `1455,1873`），且其后同格 `FSCP-RES` 不再 `owner=-1`。
2. `CPL-RESERVE … booked=` 之后**同 tick** 出现 `RESV-EXT-DEST bad=0`（下一 tick 的行驶延伸不再 `reserve-fail`）。
3. veh=15 不再出现 `CPL-NOOWNRESV … no-own-reservation`（或至少不再是停车主因）。
4. 出现 `CPL-SAFE-TRUNC` 时，车应**开到截断位**再停（位置坐标前进），而不是原地不动。
5. `CPL-S0-CHK … wait=` 不再单调递增到几十（即车不是永久停驻）。
6. 无 `LOOP-CLOSED` 后的无限重入、无崩溃。

---

## 八、本轮未做

- 未改 `_r3r_couple_path_blocked` 的语义（零进展截断时仍按 KI-516 原地等）。
- 未验证 P-525-A 的 A-1/A-2/A-3。
- 未查 `TrainCoupleHandler` 的耦合距离阈值（A-1 的前提）。

---

## 九、构建状态：玩家手跑全量 ⇒ 一处编译错误 ⇒ 已修，待重编

### 9.1 玩家全量的实况
- `build\R3R_fullbuild.log` 51 638 B / 01:13:15，`build\R3R_fullbuild.done` = `EXIT_CODE=1`。
- 698 步走到 `[589/698]` 时失败，**唯一**失败 TU = `src\pbs.cpp`：
  ```
  D:\sourcecode of JGRPP\src\pbs.cpp(748): error C2065: “DIAGDIR_BEGIN”: 未声明的标识符
  D:\sourcecode of JGRPP\src\pbs.cpp(748): error C2065: “DIAGDIR_END”: 未声明的标识符
  ```
  本树没有 `DIAGDIR_BEGIN/END` 宏，遍历枚举要用 `EnumRange(DiagDirection::Begin, DiagDirection::End)`
  （`src\direction_type.h:83-91` 的 `Begin/End`；`EnumRange` 在 `src\core\enum_type.hpp:241`，
  经 `src\pbs.h:14 direction_type.h` 传递可见）。
- **`src\pathfinder\yapf\yapf_rail.cpp` 已在 `[335/698]` 编译通过** ⇒ 本轮 yapf 侧改动（extern、
  重试、截断）零诊断，只有 pbs.cpp 那一行要修。
- 全日志 `warning C` 0 条。

### 9.2 已修
`src\pbs.cpp:748` 改为：
```cpp
for (DiagDirection d : EnumRange(DiagDirection::Begin, DiagDirection::End)) {
    if (IsRailDepotTile(tile + TileOffsByDiagDir(d))) return 0;
}
```
依赖自证：`EnumRange` 来自 `src\pbs.h:14 → direction_type.h:13 → core/enum_type.hpp`；
`IsRailDepotTile` 在 `src\rail_map.h:107`，本文件已用同头的 `GetRailReservationTrackBits`（rail_map.h:225）；
`Train::Iterate()` 与 `Vehicle::Iterate()` 同源，本文件 :423 已在用后者。

### 9.3 为什么我这边编不了（与代码无关）
`R3R_fullbuild.cmd` → `vcvars64.bat` 需要 `reg.exe` 去定位 Windows SDK，而 `reg.exe` 在沙箱的
Program Blacklist 里 ⇒ `set INCLUDE` 只剩 MSVC 三个目录、没有
`C:\Program Files (x86)\Windows Kits\10\Include\10.0.26100.0\ucrt` ⇒ `yvals.h(22): fatal error
C1083: 无法打开包括文件: "crtdbg.h"`。玩家自己的终端不受此限（本轮全量正是证明）。

### 9.4 请玩家执行的命令（**上一轮那条确实跑不起来：路径含空格且未加引号**）

cmd.exe（推荐，逐行）：
```bat
cd /d "d:\sourcecode of JGRPP"
R3R_fullbuild.cmd inc
```

或 PowerShell：
```powershell
Set-Location "d:\sourcecode of JGRPP"; & ".\R3R_fullbuild.cmd" inc
```

- 跑之前**先退出游戏**（否则链接 LNK1168、脚本写 `EXIT_CODE=97`）。
- 现在**不会**再退化成全量：`find src -name *.h -newer <最新 obj>` 为空，护栏会判定
  `incremental is safe`（全量留下的 obj 都在，只需重编 `pbs.cpp` 再链接，约 1~3 分钟）。
- 脚本结尾有 `pause < CON`，需要按一下键才关窗；想看结果直接读
  `build\R3R_incbuild.done`（`EXIT_CODE=0`）与日志尾部 `[n/n] Linking CXX executable openttd.exe`。



---

## 十、第二轮（同一轮续）：玩家实测「预留在不安全的位置 1442,1874 截止」

### 10.1 输入效度
`build\R3R_debug.log` 3181 行 / 324 061 B / 2026-11-27 01:34:22，产生者 `build\openttd.exe`
01:27:18（54 416 384 B，第 525 轮第一版：孤儿回收 + 截断）⇒ 采样含本轮第一版。

### 10.2 先说好消息
全日志出现 2 次 `COUPLE-OK`，尾部 `CPL-GEO-DONE v=15 u=18 merged=1 spd=160`，随后
`GEO settle-couple head=17 n=57` —— **死锁解除、挂接完成**。孤儿回收（`RESV-ORPHAN-CLEAR`）
与截断确实把车从"原地等 62 tick 不动"里救了出来。

### 10.3 玩家看到的不安全截止位是怎么来的（行号为 `build\R3R_debug.log`）
```
:1966 CPL-BACK pPrev=1424,1874 td=0 parent=1442,1874 ptd=0
:1967 FSCP tile=1424,1874 skip=multihead-blocked n=2 h=18,75 hasT=1
:1968 CPL-SAFE-FAIL veh=15 tile=1424,1874
:1978 CPL-SAFE-TRUNC veh=15 from=1474,1884 tile=1442,1874 hops=6
:2015 CPL-RES-SAFE-STOP veh=15 nth=41/42 tile=1442,1874 td=0 booked=42 goal=1
:2062 RESV-EXT-EXIT veh=15 kind=choose origin=1442,1874/0 tile=1441,1874 td=0 nextbits=0x9
```
1. YAPF 的 best 节点是 `1424,1874` —— **车底自己那一排的远端**（`tgtLen=216` 节，18/75 两条链都压在那格）。
2. 该节点的 segment 是「整段站台跳格」：`1442 → 1424` 一步跨过整个站台（`PFX-GEO` 显示 1436/1437/1438 是站台格，1439 起才是普通轨）。
3. 安全位判据在 `1424` 上失败（`multihead-blocked`，KI-562 规则）⇒ 我第一版的截断取
   "失败节点的**父节点**末格" = `1442,1874` ⇒ 摸路终点被改写成 1442 ⇒ 预留 42 格收在 1442。
4. `1442,1874` 是岔口 `1441,1874`（`tb=0x9 = X|LOWER`）的**前一格**：既不是安全位，下一 tick
   `ExtendTrainReservation` 从 1442 往前也只看见 choice（`steps=0`），预留再也长不出去。

⇒ **这是第 525 轮第一版（我的截断）自己造出来的缺陷**：截断点取了 YAPF 的节点边界，而不是
"本车真的要开到哪里"。车要去的只是会合点 `consist_head_tile = 1438,1874`（`CPL-HEAD-STATIC head=1438,1874`），
1424 在会合点之外、属于车底内部，判它没有意义。

### 10.4 修法（已落地，KI-691）

**① 判据只覆盖行驶段**（`yapf_rail.cpp` 回溯段）：
- 新增 `r3r_drive_end = 会合点`；
- 先从 best 节点往回找"segment 含会合点"的节点 `r3r_goal_node`；
- 比它更深（更靠车底远端）的节点**整段跳过**，不判也不截断；
- `r3r_goal_node` 自己只判到会合点为止（用 lambda 版 `IterateTiles`，遇会合点即停）。

**② 截断必须收在安全位**（`yapf_rail.cpp` 第二遍预订之前）：
- 只在 `r3r_truncated` 时生效；
- 在 `r3r_resv_path` 上找**最后一个** `IsSafeWaitingPosition(...)` 的格，清单收到它为止；
- 一路没有安全位 ⇒ 一格不订 + `_r3r_couple_path_blocked = true`（原地等，不降级去绕道）；
- 探针 `CPL-SAFE-CUT veh= from= to= end=,`。

于是正常路径的预留终点回到**会合点 1438,1874**（紧贴车底），而不是 1442。

### 10.5 复测判据
1. `CPL-RES-OK … tiles=…;1438,1874/<td>` —— 清单**最后一格是会合点**（不再是 1442）。
2. 不出现 `CPL-SAFE-CUT … end=-1,-1`（即截断时总能找到安全位）。
3. `CPL-SAFE-FAIL veh=15 tile=1424,1874` 的 `multihead-blocked` **不再出现在行驶段**（该格现在不参与判定）。
4. `veh=15` 的 `RESV-END endTile=` 落在会合点或安全位，不再出现 1442。
5. 挂接仍能完成（`CPL-GEO-DONE merged=1`）。

### 10.6 构建：玩家手动跑 `R3R_fullbuild.cmd inc` ⇒ `EXIT_CODE=1` ⇒ 两处编译错误已修，本轮已编成

**玩家那次的报错**（`build\R3R_incbuild.log` 7270 B / 01:44:28，失败 TU = `yapf_rail.cpp`）：
```
yapf_rail.cpp(2061): error C2672: "CYapfRailNode::IterateTiles": 未找到匹配的重载函数
yapf_rail.cpp(2090): error C2248: "CYapfReserveTrack<Types>::FindSafeCouplePositionProc": 无法访问 private 成员
yapf_rail.cpp(2088): error C2672: 同上
```
根因两条：
1. **模板实参写成了模板名**：`IterateTiles<Tbase,...>` 的 `Tbase` 只用于 `typename Tbase::TrackFollower`。
   在 `CYapfReserveTrack` 类内，注入类名 `CYapfReserveTrack` 就等于 `<Types>`（:1257 的既有用法因此能编）；
   而我的调用写在派生类 `CYapfFollowCoupleRailT` 里，那里 `CYapfReserveTrack` 只是模板名 ⇒ 重载匹配失败。
   已改成 `IterateTiles<CYapfReserveTrack<Types>>`。
2. **private 成员**：`FindSafeCouplePositionProc` 是 `CYapfReserveTrack` 的 private，派生类取不到成员指针。
   改为在基类新增公开成员 `CheckSafePositionOnNodeTo(Node *node, TileIndex stop_tile, TileIndex *fail_tile)`
   （`yapf_rail.cpp:1155`），内部用 lambda 版 `IterateTiles<CYapfReserveTrack>` 走到 stop_tile 即停，
   派生类只调用它。原 `CheckSafePositionOnNode()` 保留未动。

**本轮编译自证**（绕 `reg.exe`：手动设 `INCLUDE`/`LIB`/`PATH` 直调 ninja，因为
`vcvars64.bat` 靠 `reg.exe` 找 Windows SDK，而它被 Program Blacklist 拦下）：
- 先单编两 TU 验证语法：`cl /c pbs.cpp`、`cl /c yapf_rail.cpp` 均无任何输出（无错无警）。
- 再 `ninja -C build -j8 openttd.exe`：`[2/3] Building CXX object …yapf_rail.cpp.obj` →
  `[3/3] Linking CXX executable openttd.exe`，无错误。
- 时间戳：`src\pathfinder\yapf\yapf_rail.cpp` 01:45:23 → obj 01:46:51 → `build\openttd.exe`
  01:49:08（54 425 088 B）；`src\pbs.cpp` 01:16:37 → obj 01:20:26。
- exe 字面量：`RESV-ORPHAN-CLEAR` / `CPL-SAFE-TRUNC` / `CPL-SAFE-CUT` / `at=%d,%d` 全部命中。
- 注：`build\R3R_incbuild.done` 里仍是玩家那次失败的 `EXIT_CODE=1`（我直调 ninja、没走 .cmd 包装，
  也不自行改写 done 文件）。三件套以 obj/exe 时间戳 + 上面的 ninja 输出为准。

---

## 十一、第三轮：玩家报「第一段预留应到 1457,1874 截止，实际到 1456,1874 才截止」

### 11.1 输入效度
`build\R3R_debug.log` 3598 行 / 322 913 B / 2026-11-27 01:54:13，产生者 `build\openttd.exe`
01:49:08（第 525 轮续版：孤儿回收 + 截断 + 行驶段判据 + 安全位截断）⇒ 采样含全部三项改动。

### 11.2 玩家所指的那一格（行号为该日志）
```
:3033 CPL-FIRST-TD st=1457,1874 hops=5 yapf=0 at=1456,1874 tdb=0x101 ok=1 best=0 start=1456,1874 trk=0
:3036 RP cur=1456,1874 td=0 depth=0
:3054 CPL-RES-OK veh=15 total=19 booked=19 … tiles=1456,1874/0;1455,1874/0;…
```
`st=1457,1874` 就是机车脚下的格（`CPL-ENTRY veh=15 tile=1457,1874`），而 `rp()` 从
`best_start=1456,1874` 起步 ⇒ **机车自己那一格 1457,1874 从未进入预留清单**，清单最近端是 1456。
`rp_impl()` 里"跳过起点格"的写法是 `if (cur != st || depth != 0)`，它只在 `rp()` 从 `st` 起步时才生效；
一旦从 YAPF 首步格（`first_tile`）起步，`st` 压根没被访问过，于是"跳过"变成"整格丢失"。
这与玩家口径（第一段预留应当铺到 1457,1874 为止）不符。

**注意另一读法**：同一 tick 的**行驶预留**也结束在 1456,1874
（`:3014 RESV-EXT-EXIT veh=15 kind=choose origin=1456,1874/0 tile=1455,1874 steps=0`），
若玩家说的是那一段，则修法不同。本轮按"挂接预留清单必须含机车自身格"落地，另一读法登记为疑点 REG-525-E。

### 11.3 更深的一层：PAIR-AIR 把机车自己的预留判成了车底的，导致整条预留是假账
```
:3148 RESV-PAIR-AIR veh=15 tile=1455,1874 track=4 kind=rail
:3149 CPL-RES-OK veh=15 total=20 booked=20 … tiles=1455,1874/4;1455,1873/5;…
:3150 CPL-RESERVE veh=15 reserved=1 next=12 path=20 booked=20
:3151 CPL-RET veh=15 tick=41609 tile=1455,1874 … ret=0x10 why=nores-final
```
20 格里 **19 格被判 PAIR-AIR**（`1455,1874/4、1447,1874/2、1446..1439,1874、1438,1874`），
一格都没写地图 ⇒ `booked=20` 是假账 ⇒ 紧接着 `CPL-RET why=nores-final`（机车没有预留）。
根因在 `R3RReservationHeldByCouplePartner()`（`src\pbs.cpp:517`）：它用
`GetTrainForReservation(tile, track)` 判归属，而那个函数把**整条连续预留链**走到末端再问"谁站在那儿"。
机车自己的位与车底的站台位在几何上首尾相接 ⇒ 从机车位往西走先到车底 ⇒ 判成"车底的"。
这正是上一轮登记的 **P-525-A**，本轮拿到第二个独立证据。

### 11.4 由此引发的回归：新加的孤儿回收把机车自己的活预留清掉了
```
:3232 RESV-ORPHAN-CLEAR tile=1455,1873 track=5 reason=ownerless
:3233 UNRES-RAIL tile=1455,1873 track=5 phase=yapf-couple actor=15
:3235..3242 RESV-ORPHAN-CLEAR tile=1454,1873 … 1447,1873   （共 9 位）
:3254 CPL-RES-OK … tiles=1455,1873/5;…
:3256 CPL-NORES-BLOCK veh=15 tile=1455,1874 … end=1455,1874
:3257 COUPLE-FAIL loco=15 …
```
链条：第一步 `1455,1874/4` 被判 PAIR-AIR **没写地图** ⇒ y=1873 那一串位与机车之间断开 ⇒
`GetTrainForReservation` 找不到人、`TrainReservationPassesThroughTile` 也不覆盖 ⇒ 我的回收器
判定"无主" ⇒ 一次清掉 9 位（全是机车上一 tick 刚铺的）⇒ 之后 `path=0 booked=0`、尾部
`CPL-S0-CHK … wait=80..99` 一直涨。**这是第 525 轮修法 1 的回归，必须收。**

### 11.5 本轮修法（三处，全部只改 .cpp）
1. **PAIR-AIR 收紧为"最近的车体就是配对伙伴"**（`src\pbs.cpp` `R3RReservationHeldByCouplePartner`）：
   普通轨改成两个方向都用既有助手 `R3RFollowReservationToNearbyTrain()`（它在**第一个**碰到车的格停下，
   就是"最近车体"语义，见 pbs.cpp:2154-2176 的注释）。两个方向都必须落在伙伴链上才算空气。
   于是机车自己那一段（往东第一个碰到的是机车自己）不再被判成车底的 ⇒ 真写地图、真归属自己。
2. **孤儿回收加"跨 tick 陈旧"门槛**（`R3RReapOrphanReservationBits`）：同一位必须**至少跨 2 个 tick**
   都被判无主才清；只看一瞬间会把"刚铺、当 tick 暂时不可归属"的活位当孤儿（11.4 现场）。KI-690 那
   类长期孤儿（车底开走留下的）天然满足该门槛，回收能力不变。
3. **挂接预留清单补上机车自身格**（`src\pathfinder\yapf\yapf_rail.cpp`）：`rp()` 从 YAPF 首步格起步时，
   把 `{st, origin.trackdir}` 先尽力订一次，成功就插到清单头 ⇒ 第一段预留如实铺到机车脚下
   （玩家现场即 1457,1874），再往西铺到会合点。

### 11.6 疑点（登记）
- **REG-525-E**：玩家所指的"第一段"也可能是**行驶预留**（`RESV-EXT-EXIT origin=1456,1874`）。
  若如此，则"应到 1457,1874"= 行驶预留至少要覆盖机车脚下那一格，需要另改
  `ExtendTrainReservation()` 的 origin 取法（当前取自 `FollowTrainReservation`，天然不含脚下格）。
- **REG-525-F**：`R3RReservePathSequence` 的 `booked` 仍把 PAIR-AIR 计为已订，`CPL-RES-OK` 会高估；
  11.5-1 之后应大幅减少，但仍不是"地图上真订了 N 格"的严格计数。

### 11.7 复测判据
1. `CPL-RES-OK … tiles=` 的**第一格 = 机车脚下那一格**（现场应为 1457,1874）。
2. 不再出现 `RESV-PAIR-AIR` 连续覆盖机车整条路径的情形；一条路径里 PAIR-AIR 应只剩车底实际压住的格。
3. 不出现 `CPL-RET … why=nores-final`（`booked>0` 而地图无位）。
4. `RESV-ORPHAN-CLEAR` 一次最多清"真正长期无主"的位；不得出现"刚铺 9 位被一次清空"的形态。
5. 挂接仍能完成（`CPL-GEO-DONE merged=1`）。

### 11.8 构建自证（第 525 轮续·第三版）
- `src\pbs.cpp`（PAIR-AIR 最近车体判据 + 孤儿回收跨 tick 门槛）、
  `src\pathfinder\yapf\yapf_rail.cpp`（挂接清单补机车自身格）改动写入，`read_lints` 0 条。
- 绕 `reg.exe`（沙箱拦）手动设 `INCLUDE`/`LIB`/`PATH` 直调 ninja：
  `[2/4] pbs.cpp.obj` → `[3/4] yapf_rail.cpp.obj` → `[4/4] Linking CXX executable openttd.exe`，无错无警。
- `build\openttd.exe` 02:03:59（54 425 088 B）；exe 字面量 `RESV-ORPHAN-CLEAR` / `CPL-SAFE-CUT` /
  `age=%u` 全部命中。
- 注：`build\R3R_incbuild.done` 仍为玩家那次失败的 `EXIT_CODE=1`（直调 ninja、未走 .cmd 包装）。
