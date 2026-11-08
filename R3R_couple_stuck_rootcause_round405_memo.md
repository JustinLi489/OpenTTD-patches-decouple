# 第 405 轮：挂接永久失败根因定位（2026-11-08）

> 输入：`build\R3R_debug.log`（187 013 B / 1 733 行，mtime 11-08 15:53:47），
> 产生者 `build\openttd.exe`（mtime 11-08 15:47:43，含 `CPL-NEXT` / `LOCO-MOVE` / `RESV-END` 字面量
> ⇒ 确为第 404 轮探针版）⇒ 采样有效。
>
> 本轮**零源码改动、零构建**，纯取证 + 根因定性。

---

## 一、现象

机车链 `veh=0`（8 节，`CPL-PAIRMAP vLen=8`）订单 `order=16`（`OT_GOTO_COUPLE`），
目的地 `248,132`（`DEPOT-ARR veh=0 ... destTx=248 destTy=132`）。
等待车底 `veh=3`（`vLen=24`）停在 `248,132` 一带的站台上，并被 `SVC-DEPOT-SKIP`
（KI-220 的自动回库守卫）长期挂着不动。

机车从 `spd=80` 一路刹到 `spd=2` 后**永久停住**在 `248,127`，`COUPLE-FAIL` 每 tick 刷，
日志末（tick 4608）仍是同一行读数，全程没有 `COUPLE-OK`。

---

## 二、日志时间线（关键锚点，均为原文片段）

| 行 | 原文（节选） | 判读 |
|---|---|---|
| :1320 | `RESV-EXT-PRE veh=0 tile=248,128 ... reserved=0x0 doRes=1 spd=80` | 高速时走 `TryPathReserve`（带 `CTTF_FORCE_RES`），扩展入口进入 248,128，此时该格**无预留** |
| :1321 | `EXTEND-BACKOUT veh=0 origin=248,127/1 stop=248,129/1 next=248,133 reason=choice-reserved err=0 owner=5 whole=1 track=1 need=0x16 resv=0x2 led=3 n=1` | **核心行**：跟随器从 248,127 走到 248,129 后**一步跳过整段站台**落到 `next=248,133`，在 248,133 判 `choice-reserved`；`whole=1`（站台整格预留）`owner=5`（整格主人是等待车底那列车的 veh5）`led=3`（账本回溯答成 veh3） |
| :1322-:1323 | `UNRES-RAIL ... tile=248,128 track=1` + `CLEAR-RAIL tile=248,128 track=1 phase=extend-backout` | back-out 把 trial 期间订的位（含 248,128）**全部撤销** |
| :1324 | `RESV-EXT-DEST bad=1 ... new=248,128` | 扩展失败，预留终点收缩为 248,128 |
| :1345/:1377/:1408/:1430 | 同形态 `RESV-EXT-PRE ... doRes=1 spd=66` | 重复尝试，重复失败 |
| :1453 | `RESV-EXT-PRE ... doRes=0 spd=53` | 调用入口切换为 `TrainController` 的移动分支（不带 `CTTF_FORCE_RES`，且存档 `pf.reserve_paths=0`）⇒ 之后驾驶侧**不再自行订路** |
| :1584 | `CPL-RES-OK veh=0 total=3 booked=3 tiles=248,128/1;248,129/1;248,133/1` | 挂接寻路（yapf `ReserveSingleTrack`）**订成功 3 格**，含会合点站台格 248,133 |
| :1586-:1587 | `RESV-FOREIGN-SKIP veh=0 tile=248,128 ... owner=3` / `tile=248,133 ... owner=5` | 同 tick 的归属复核（非仲裁版 `GetTrainForReservation`）把这两格答成 veh3 / veh5 ⇒ 判"不是我的" |
| :1616 | `RESV-EXT-PRE veh=0 tile=248,128 ... reserved=0x2 doRes=0 spd=15` | 248,128 这时**有预留（0x2 = TRACK_Y）**，但 doRes=0 ⇒ 不走扩展 |
| :1617 | `RESV-ARB tile=248,128 station=0 a=veh3 wait=1 b=veh0 wait=0 picked=veh0` | 第 361 轮仲裁判 **veh0** 是主人；与上面 owner=3 直接冲突 ⇒ 两本账分叉 |
| :1618 | `SPD-DROP veh=0 ... prev=22` | 速度掉到 0（此后残余 2） |
| :1619 | `DEPOT-ARR veh=0 ... destTx=248 destTy=132` | 订单目标仍是 248,132（veh3 所在格） |
| :1624 起 | `CPL-NEXT ... to=248,128 plain=1 bits=0002 resv=0002 wholeOwner=3 spd=2`（此后每 tick 同一行） | 稳态死锁：位在、owner 判成 veh3、速度恒 2 |
| :1615/:1621/:1639 | `LINEND veh=2 tile=248,127 ... sig=1 brk=15 clamp=1` → `clamp=0 ... spd=2` | 前方被钳住（信号/line-end），与上面的 back-out 同一结果 |
| :1671/:1693/:1719 | `LOCO-MOVE ... endTile=248,133 endTd=1 endBits=0202 movdir=3 dir=7 db=1 spd=2` | 预留终点确实是 248,133，但车一步也不动 |

---

## 三、根因（REG-405-A，高）

**会合点站台格在两套预留语义下判读相反，导致「挂接侧订、驾驶侧撤」的每 tick 互拆循环。**

1. 站台格是**整格预留**（`_me[t].m6` bit2，`SetRailStationReservation`）。等待车底 `veh=3`（其链上 `veh=5` 那一节）正停在 248,130..133 的站台段上 ⇒ 该段参与整格预留归属，`GetTrainForWholeTileReservation()` 会答 `veh=5`（日志 `whole=1 owner=5`）。
2. **挂接寻路**（`yapf_rail.cpp` 的 `ReserveSingleTrack` / `R3RReservePathSequence`）对站台会合点走"空气位"豁免（日志 `RESV-PAIR-AIR veh=0 tile=248,133/132/131/130 kind=stn`），把 248,133 的**单轨位**订上，并认为整条会合路径成立（`CPL-RES-OK booked=3`）。
3. **驾驶侧** `Train::ExtendTrainReservation()` 用 `TrackFollower` 走这条路径：站台段被一步跳过（`stop=248,129` → `next=248,133`，`tiles_skipped` 语义，同 KI-163/KI-253 家族），落点 248,133 处按**整格语义**判 `reason=choice-reserved`（`whole=1 owner=5`）⇒ 认为"下一格被别人占"。
4. 原生 `ExtendTrainReservation` 对 `choice-reserved` 的处理是**整 trial 回滚**（`UNRES-RAIL` + `CLEAR-RAIL phase=extend-backout`），于是它把第 2 步刚订上的 248,128 / 248,129 **一起撤掉**，终点收缩成 248,128（`RESV-EXT-DEST bad=1 new=248,128`）。
5. 终点收缩 ⇒ 机车到达 248,128 前就无位可进（`MAXSPD-BREAK` 钳到 0，残余 `spd=2`）⇒ `LINEND sig=1 brk=15 clamp=1` ⇒ 停住。
6. 下一 tick 挂接寻路又订回同样 3 格（`CPL-RES-OK booked=3`），驾驶侧又 back-out ⇒ 无限循环，`COUPLE-FAIL` 刷屏。

**一句话**：`term_exempt=0`（第 399 轮的"终点例外"没在本场景生效），挂接侧的会合点豁免与驾驶侧的整格 `choice-reserved` 判据互不认账；而 `choice-reserved` 的回滚粒度是"整条 trial"，于是把合法路径连同会合点一起废掉。

### 为什么不是"别人的位被抢"

- `:1584` 订位时 248,128 是空的（`:1320 reserved=0x0`），`booked=3` 是真订上；
- `:1617` 仲裁明确 `picked=veh0`；
- 因此 248,128 的位**确实是 veh0 自己订的**，`:1586 owner=3` 只是回溯歧义（见下节）。

---

## 四、共存缺陷（REG-405-B，中，诊断口径）

同一格在"本车路径末端与等待车底座位首尾相接"处，两个归属函数给出不同答案：

| 读取点 | 函数 | 日志证据 | 结论 |
|---|---|---|---|
| 挂接寻路自订豁免 | `TryReserveRailTrackdir` 内部判据 | `CPL-RES-OK booked=3` | 我的 |
| 第 361 轮仲裁（仅 `r3r_couple_foreign_res` 一处） | `GetTrainForReservationArbitrated()` | `:1617 RESV-ARB picked=veh0` | 我的 |
| `ExtendTrainReservation` 的 stale-own 自救（`led=`） | `GetTrainForReservation()`（非仲裁） | `:1321 led=3` | 别人的 ⇒ 自救不触发 |
| 链清扫 / 释放复核 | `R3RReleaseReservationChecked` → `GetTrainForReservation()` | `:1586 RESV-FOREIGN-SKIP owner=3` | 别人的 ⇒ 跳过释放 |
| `CPL-NEXT` 探针 | `GetTrainForWholeTileReservation()` | `wholeOwner=3` | 别人的 |

`R3RArbitrateReservationOwner()`（`src\pbs.cpp`，非站台取"非等挂"者）的语义是正确的
（等待车底不该占住主动方要走的位），但它**只接了一处**；
`ExtendTrainReservation` 的自救与 `R3RReleaseReservationChecked` 仍用非仲裁版
⇒ "位是我订的，但没人认"。

---

## 五、与既有条目关系

- **REG-399-C（终点例外）**：同族。399 在 yapf 侧加豁免，本轮证明**驾驶侧 `ExtendTrainReservation` 没有同等豁免**，同一自锁换了个人演。本场景日志 `term_exempt=0`，说明豁免根本没被这条路径用到。
- **REG-359-E（归属仲裁）**：第 361 轮只接入 `r3r_couple_foreign_res`，本轮列出其余 4 个未接入点（见上表）。
- **KI-330 / KI-332（同格误判阻塞）**：形态不同。KI-330 是 `CheckTrainsOnTrack` 把同格车当阻塞；本轮是 `choice-reserved` 的整格归属。
- **KI-163 / KI-253（站台段一步跳格）**：提供机制解释（`stop=248,129 → next=248,133`），不是缺陷本身。

---

## 六、修法方向（待拍板，未落地）

- **A（首选，对症）**：`ExtendTrainReservation` 在 `choice-reserved` 判定处加"会合点豁免" —— 若该格/该整格主人正是本车的挂接目标（`r3r_couple_goal` / `CPL-PAIRMAP` 记录的 pair），或该格是被等待车底占用的站台格且本车 `current_order.IsType(OT_GOTO_COUPLE)`，则按 `PBSTileInfo` 正常返回 line-end（到达），**不 back-out**。等价于把 399 的 `term_exempt` 语义补进驾驶侧。
- **B（治回滚粒度）**：`choice-reserved` 时不要整 trial 回滚，只回滚到"最后一个已确认安全位"，保住会合路径前段。
- **C（统一口径，辅助）**：把 `:16216` 的 `led=` 与 `R3RReleaseReservationChecked` 改用 `GetTrainForReservationArbitrated()`；否则 A 修好后仍会出现"位是我订的、清扫不敢碰"的日志噪声（不影响行车，但妨碍判读）。

A + C 组合最小；B 影响面最大需谨慎（会改变原生 PBS 回滚语义）。

---

## 七、未确认项

- **U-405-1**：`origin=248,127/1 stop=248,129/1 next=248,133` 中 `stop` 落在 248,129 而不是站台首格 248,130 —— 是本车链尾投影导致，还是站台 `IsCompatibleTrainStationTile` 判定的入口差异？未取数。
- **U-405-2**：`veh=5`（整格主人）与 `veh=3`（配对目标）是否同一条链、248,133 上究竟停的是哪一节？日志只给了 owner 编号，未给链结构。
- **U-405-3**：`doRes` 在 `spd 66→53` 处由 1 翻 0，是否确实是调用入口从 `TryPathReserve` 切到 `TrainController`（缺 `CTTF_FORCE_RES` 且 `pf.reserve_paths=0`）？需在 `ChooseTrainTrack` 入口补 `caller=` 才可证。
- **U-405-4**：`SVC-DEPOT-SKIP veh=3 cur=17 real=4(17)` 让等待车底长期不动，是否也是"该格永不释放"的共因？按本轮结论不是主因（位是 veh0 自己的），但会拉长僵持时间窗。
- **U-405-5**（新增疑点）：`:1320` 时 `reserved=0x0` 却同样在 `next=248,133` 判 `choice-reserved owner=5` —— 说明 `choice-reserved` 判的是**整格预留**（`whole=1`）而非单轨位；需确认站台整格预留是否在 `veh=0` 尚未订位时就已由 `veh=5` 持有（若成立，则"站台整格位"是全程锁死的墙，A 修法必须按整格例外处理）。

---

## 八、复测判据（修法落地后）

1. 同场景出现 `EXTEND-BACKOUT ... reason=choice-reserved` 时，`new=` 不应再收缩到整段路径起点；`RESV-EXT-DEST bad=0`。
2. `RESV-EXT-PRE ... doRes=1` 若干 tick 后能见到预留终点前移到会合点（`RESV-END` / `CPL-NEXT` 的 `to=` 稳定在 248,133），机车 `spd` 不再恒 2。
3. 出现 `COUPLE-OK loco=0 rear=...`，随后 `CPL-NEXT` 不再重复同一行。
4. 负判据：普通（非挂接）列车遇到站台上真有别人的车时，仍必须 back-out/停住，不得穿模。
5. 负判据：`RESV-FOREIGN-SKIP` 的判定若改用仲裁版，不得出现把别人位清掉的 `CLEAR-RAIL`（KI-479 防误清不可回退）。
