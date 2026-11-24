# 第 495 轮：不按预留走 + 预留溢出站台（玩家 2026-11-24 报）

> 状态：**已登记，未取证完毕**（本轮只做代码边界定性 + 落盘，未改源码、未构建）
> 玩家原话：「我注意到，经过你的修复之后，列车的普通寻路和挂接寻路都会出现沿着预留走一半去了道岔上另一条没预留的岔路，也就是所谓的"不按预留走"的问题。而同时，预留超出站台的问题却依然存在」

## 一、两条现象（分开定性，不要混为一条）

| 编号 | 现象 | 范围 |
| --- | --- | --- |
| **P1** | 沿预留走一半，到道岔拐进**另一条没预留**的岔路（"不按预留走"） | **普通寻路 + 挂接寻路都会** |
| **P2** | 预留**超出站台** | 站台段相关（历史 KI-346 家族） |

P1 与时间相关（"经过修复之后"），P2 是**老问题延续**（玩家说"依然存在"）⇒ 两者很可能**不同根因**，必须分开查。

## 二、P1 候选根因（待验证，按嫌疑排序）

### 候选 A（主嫌，与"修复之后"直接对应）：`R3RAnswerFreeReservation()` 在寻路前整车释放预留

- 位置：`src\pathfinder\yapf\yapf_rail.cpp:166` 起（第 493 轮续新增），内部 `:170` 附近调
  `FreeTrainTrackReservation(const_cast<Train*>(veh), o.tile, o.trackdir)`。
- 调用链：`R3RAnswerReplay()`（`:1249`）判废分支 `:1275 dest-changed` / `:1288 origin-not-in-answer`
  → `R3RAnswerFreeReservation()`；而 `R3RAnswerReplay()` 的唯一调用点是 `:2576`，位于
  `if (reserve_track)` **之前**、本次 `FindPath` 之前的入口处。
- 关键风险面：那一刻图上那条预留，**正是玩家刚刚看到的"要走的路线"**。函数把它整车撤掉，
  紧接着的 `FindPath` 若选了另一条岔路（`safe/final` 常量漂移，REG-494-A 已见 `1457,1873` 恒定），
  观感就是"预留线突然改道/消失，车沿旧惯性走进没预留的岔路"。
- 佐证限度：这条只能解释**普通寻路**。挂接侧 `:1262` 直接 `return false`，进不了回放，
  所以**挂接出现 P1 必须另有共因**（见候选 B/C）。

### 候选 B（共因，已登记 KI-680）：部分预留仍放行

`:3276 CPLRES ... nTry=23 nOK=22` → `:3278 CPL-RET ... why=cpl-partial-res spd=160`。
即站台段最后一格落空，但整段已铺到岔口前就放行，列车越过岔口后**前方本就没有预留** ⇒
"走一半进没预留的岔路"。这条**同时适用于普通与挂接**，与"两者都会"最吻合。

### 候选 C（共因，REG-494-A）：safe position 与列车实际 trackdir 不一致

`CPL-DESTCHAIN` 的 `safe/final` 在 30+ 次里是同一常量 `1457,1873/13`，与 origin 无关；
若列车真实行进方向对应另一根轨，预留就铺在"平行线/另一岔路"上，车按自己方向走 ⇒ 两者分叉。
这也正好解释上一轮已经查实的 veh=15 断头线（KI-678）。

## 三、P2 代码边界（重要：站台段本体**不会**溢出）

`ReserveRailStationPlatform()`（`:658-756`）当前实现：

- `:691-693` 用**该格真实存在的轨道** `FindFirstTrack(plat_tracks)` 喂 `TryReserveRailTrack()`
  （注释 `:687-690` 说明 Station 分支写的是**整格位** `SetRailStationReservation`，`track` 只给断言用）
  ⇒ 第 240 轮 U-3「`plat_track=AxisToTrack(axis)`」**已经被改掉了**，不再是 P2 的成因。
- `:709` 循环条件是 `while (IsCompatibleTrainStationTile(tile, start) && tile != this->origin_tile)`，
  `IsCompatibleTrainStationTile()` 第一项就是 `IsRailStationTile` ⇒ **站台外第一格立刻停**。
- `:719` 把该格记进 `r3r_plat_exit_tile`，`:736` 打
  `RESV-PLAT-EDGE veh= start= last= exit= exitStn= dest= destStn= span= origin=`。
  `:711-718` 的注释明确：`exitStn` 恒 0、`last` 恒等于平台末格；若 `destStn=0` 且 `exit==dest`，
  则"溢出源是**预留目标被选到站台之外**"，不是这个循环多订了一格。

⇒ P2 的真实来源只剩两种，必须靠日志区分：
1. **出口格被后续递归正常订上**：`exit`（站台外第一格）紧跟着被 `ReserveSingleTrack()`/`rp()`
   订下 —— 这在本实现里是**设计内行为**（站台段结束后照常沿路径铺），只是观感像"溢出"。
   对应标签 `RESV-PLAT-OVERFLOW`（`:713-714` 注释所指）。
2. **目标点本身就在站台外**：`destStn=0`（`res_dest_tile` 不是站台格），此时预留当然"超出站台"。
   这就是 KI-346/REG-418-E 家族的原始形态。

## 四、需要的日志（最小集，现成标签，无需新探针）

| 标签 | 判什么 |
| --- | --- |
| `RESV-PLAT-EDGE ... exitStn= destStn= span=` | P2 是"出口格被续订"还是"目标在站台外" |
| `RESV-PLAT-OVERFLOW` | 出口格是否真被订上（P2 来源 1 的直接证据） |
| `ANSWER-UNRES veh= origin= td= reason=` | 候选 A 是否真的触发过整车释放（**有这行 P1 才归 A**） |
| `CPLRES ... nTry= nOK=` + `CPL-RET ... why=` | 候选 B（`cpl-partial-res` 是否伴随 P1） |
| `CPL-DESTCHAIN ... safe= final=` | 候选 C（safe/final 是否恒定/与 origin 无关） |

另需玩家提供：出问题的**列车号 + 道岔格坐标 + 大致时刻**，便于 dump 该岔口的 track bits。

## 五、判据（拿到日志后如何结案）

1. 若 P1 现场日志中**有** `ANSWER-UNRES`，且时间戳紧跟"预留改道" ⇒ 候选 A 成立，
   修法方向 = 把释放从"寻路前整车释放"改成"只释放答案本自己铺的那段，且不碰列车当前预留"
   （或退一步：判废时只 `drop` 不 `free`，回到第 493 轮续之前的行为）。
2. 若 P1 现场**无** `ANSWER-UNRES` 但有 `why=cpl-partial-res` ⇒ 候选 B 成立，
   修法方向 = `nOK<nTry` 时把 `ok` 降级为 0（KI-680 已记）。
3. 若 P1 现场 `safe=` 恒定且与车头方向不符 ⇒ 候选 C 成立，接 KI-678/REG-494-A。
4. P2 结案看 `RESV-PLAT-EDGE exitStn/destStn`：`exitStn=1` ⇒ 循环真溢出（与代码矛盾，需重查）；
   `exitStn=0 且 destStn=0` ⇒ 目标点选在站台外（改 `res_dest_tile` 选点）；
   `exitStn=0 且 destStn=1 且有 OVERFLOW` ⇒ 设计内续订，属观感问题，需玩家拍板是否截断。

## 六、本轮未做

- 未改任何源码、未构建（玩家只提现象，未点修法）。
- 未取新日志（手上 `build\R3R_debug.log` 4216 行是 veh=15 事件，不含本现象的道岔现场）。
- 未确认 P1 是"预留被释放"还是"预留从未铺到那里"，这两者在观感上相同但修法相反。

## 七、`build\R3R_debug.log` 实读（2026-11-24 玩家指认）

### 7.1 环境限制（先记，省得下轮重复踩）

- `search_content` 对该日志**恒返回 0**（即便搜 `SEGLOAD-PARK` 这种第 5 行必有的串），
  对 `src` 目录则 20s 超时 ⇒ 本轮只能 `read_file` 分段读。
- `execute_command` 在本环境**只回显命令串、不真正执行**（`cmd /c dir /b ...` 返回的 stdout
  就是命令本身，`Out-File` 不产生文件）⇒ 无法用 findstr/Select-String 落盘。

### 7.2 日志身份：**是第 494 轮同一现场的延续，不是新的独立会话**

| 读数 | 证据 |
| --- | --- |
| 总行数 | `4217`（`4216` 有内容，`4300` 起为空） |
| 开头 = 读档 | `1-4 R3R-VER-REMAP upstream 368 -> 366`、`5-7 SEGLOAD-PARK veh=15/18/75`、`14 SEGTR-SNAP tag=load-raw head=15 nseg=1` |
| 结尾 = veh=15 仍卡死 | `4212-4216 CPL-S0-CHK veh=15 tile=1448,1874 ... order=16 stuck=1 fp=0 prog=0 wait=160..164 parked=0 stop=0 track=01 dtile=7685533` |

⇒ 车号（15/18/75）与坐标（1440~1449,1874）与第 494 轮完全同源。**这份日志只能证明 veh=15
这一例仍然存在，不能单独证明"普通+挂接普遍不按预留走"**；后者需要玩家另一次复现。

### 7.3 已读到的关键行（原文）

- `800 RESV-SELF-AIR veh=15 tile=1475,1884 track=0 kind=stn dist=1`
  `801 RESV-SELF-AIR veh=75 tile=1494,1875 track=0 kind=stn dist=1`
  `803/806/813/820` 同形（veh=15 的 1475→1474，veh=75 的 1494→1491 逐格递减）
  ⇒ 站台**整格位**被自己的车占用时按"空气"处理（第 345/368 轮语义），**不是**溢出证据。
- `804-822 LINEND veh=15 tile=1474,1884 x=0..14 sig=1 near=0 spd=38→15 brk=225→15 clamp=1 len=2 dir=1 db=0`
  ⇒ 车在 `1474,1884` 前方线路尽头减速到停（`dir=1` 朝北/东侧），`clamp=1` 说明被刹车钳制。
- `827 SPD-DROP veh=15 tile=1474,1884 prev=25 order=3 real=0 stuck=0 fp=0 prog=11 wait=0 parked=0 stop=0 track=01 dtile=7701951 dt=3 ctx=linend-table cfrom=25 cto=15 cage=0 m=1`
  ⇒ 减速归零发生在 `LINEND` 判据内，不是预留被撤。
- `828-829 ORD-IDX-WATCH site=loco-entry/skipgate-in ... curType=3`（`OT_GOTO_DEPOT=3`? 见备注）
  ⇒ 该 tick 订单已推进到 `real=0`，与 `4212` 的 `order=16` 不是同一时刻，说明 veh=15 中途改过单。
- `2500-2505 / 4200-4210 CPL-FT ok veh=15 / veh=4294967295 old=1442,1874 → 1440,1874` 与
  `old=1441,1874 → 1441,1875`（`td=3`）⇒ 该断头线场景只有向西（`td=0`）与向西南（`td=3`）
  两个可走方向，`1440,1874` 即为西端尽头 ⇒ 与 KI-678 一致。
- `4210 CPL-FT ok veh=4294967295 old=1442,1874 td=8 dir=2 new=1443,1874 ... reach=0x520 after=0x100`
  ⇒ `veh=4294967295`（INVALID_VEHICLE）的临时 PathFinder 在做**反向**搜索（`td=8`），即 REG-494-A 里那个实例。

### 7.4 本节结论

1. 这份日志**不能**支撑"普通寻路 + 挂接寻路普遍不按预留走"的判定，只能确认 veh=15 仍卡在
   `1448,1874`（KI-678 断头线未变）。
2. 日志中**未见** `RESV-PLAT-EDGE` / `RESV-PLAT-OVERFLOW` / `ANSWER-UNRES`（`read_file`
   已覆盖 `1-829`、`2500-2505`、`4200-4216`；中间 `830-2499` 未读，下一轮补）。
3. P1/P2 的实证仍缺口：需要一次**新的**复现日志，并请玩家标出"哪一列车、在哪个道岔、
   从哪一格走进没预留的岔路"。

## 八、P1 实证：普通寻路侧已抓到完整链条（veh=75，日志 `1030-1081`）

### 8.1 现场原文（`build\R3R_debug.log`）

```
1043 RESV-BEGIN veh=75 tick=38844 tile=1484,1875 mf=1484,1875 td=0 spd=0 real=3 order=1 ...
1044 TPR-EXIT veh=75 state=ok pair=0 tile=1484,1875 ... other=75 otherTile=1484,1875 otherSpd=0 otherOrder=1 origin=1484,1875 okay=1
1045 TRP veh=75 mf=1484,1875 real=3 type=1 dest=7677335 spd=0 fp=0 look=0 one=0 mstuck=1 fto=1 => ok=1 res=1
1048 EXT-ENTRY veh=75 origin=1484,1875/0 order=1 spd=0 first=75
1049 EXT-WALK veh=75 ord=1 old=1484,1875/0 new=1483,1875 bits=0x1 skip=0 stn=0 plain=1 exit=0 cur=0 choice=0 target=0
1053 EXT-WALK veh=75 ord=1 old=1482,1875/12 new=1482,1874 bits=0x2000 skip=0 stn=0 plain=1 exit=3 cur=13 choice=0 target=0
1057 EXT-WALK veh=75 ord=1 old=1481,1874/12 new=1481,1873 bits=0x2000 skip=0 stn=0 plain=1 exit=3 cur=13 choice=0 target=0
1063 EXT-WALK veh=75 ord=1 old=1479,1873/0 new=1478,1873 bits=0x1001 skip=0 stn=0 plain=1 exit=0 cur=0 choice=1 target=0
1064 EXT-CHOICE veh=75 origin=1484,1875 stop=1479,1873/0 next=1478,1873 tracks=0x11 skip=0 stn=0 ord=1 steps=7
1077 CRT veh=75 order=1 dir=1 origin=1479,1873 td=0 found=1
1078 RPFAIL veh=75 tick=38844 origin=1479,1873 tile=1457,1873 td=0 ans=1 nTry=3 nOK=2 vehTile=1484,1875 vehTrk=0x1 vehDir=1 vehTd=0
1079 CPLRES veh=75 tick=38844 origin=1479,1873 destTile=1457,1873 destTd=0 node=1456,1873 root=0 ans=1 nTry=22 nOK=21 empty=0 vTile=1484,1875 vDest=1431,1874 gap=5 oSrc=other
1080 ANSWER-STORE veh=75 n=21 from=1459,1873 to=1460,1873 dest=1431,1874 nth=1
1081 RESV-TERM veh=75 real=3 type=1 tile=1457,1873 td=0 tcount=2 safe=1 tail=0 spd=0 vtile=1484,1875 why=pbs-single whole=-1 exitdir=0 tick=38844 destTile=1431,1874 destEq=0
1082 RESV-SELF-AIR veh=75 tile=1484,1875 track=0 kind=stn dist=0
```

### 8.2 判定（P1 普通寻路侧，成立）

1. **预留目标被夹死在常量格 `1457,1873`**：`1078 tile=1457,1873`、`1079 destTile=1457,1873`、
   `1081 tile=1457,1873 safe=1` 三条互证，而真实目的地是 `1431,1874`（`vDest=1431,1874`，
   距离 26 格）。`destEq=0` 明确"订到的格 ≠ 目的地"。
2. **预留只铺到 1457 就收手且"部分失败仍放行"**：`1078 nTry=3 nOK=2`、`1079 nTry=22 nOK=21`
   —— 两次都是 `nOK < nTry`，但在 `1079` 之后没有任何回滚/降级迹象，`1081 why=pbs-single safe=1`
   当成功处理。**这就是"走到 1457 之后前方没有预留"的直接来源**（候选 B 坐实，对应 KI-680）。
3. **岔口就在最后一段路上**：`1063 bits=0x1001 choice=1`（该格两条轨道，`choice=1` 说明必须
   在岔口二选一）、`1064 EXT-CHOICE ... tracks=0x11 ... steps=7` —— `0x11` 即双轨岔口格，
   从 `1484,1875` 起第 7 步。列车一旦在 `1478,1873` 选了与预留不同的分支，就表现为
   "沿预留走一半、拐进没预留的岔路"。
4. **这次是答案本命中（`ans=1`）**：`1078/1079 ans=1` + `1080 ANSWER-STORE n=21` ⇒ 铺的路径
   来自/写回答案本。因此候选 A 也**不能排除**：答案本把"上次那条到 1457 的路径"反复复用
   （`from=1459,1873 to=1460,1873`），即使本次拓扑已经不同。

⇒ P1 的成因是**叠合**的：`res_dest_tile` 选点错（常量 1457,1873，REG-494-A/KI-679）+
部分预留放行（KI-680）+ 答案本复用旧路径（KI-681 家族）。三者任一单独修都不够。

### 8.3 待续

- `1030-1076` 的密集 `CPL-FT veh=75 old=1442,1874 → 1440,1874` 说明挂接摸路也在扫同一条
  断头线（`1440,1874` 西端尽头），与 veh=15 的 KI-678 同一拓扑。
- ~~`830-2499` 区间尚未读完，`RESV-PLAT-EDGE` 仍未见~~ ⇒ **已在 1090-1389 段读到**，见 §九。

## 九、P2 实证：`RESV-PLAT-EDGE` 抓到（veh=75，日志 1311-1371）

### 9.1 现场原文（同一 origin、连续两次选点，换了平行轨）

```text
1311 RESV-PLAT-EDGE veh=75 start=1424,1874 last=1438,1874 exit=1439,1874 exitStn=0 dest=1424,1874 destStn=1 span=15 origin=1456,1873
1312 RESV-PLAT-REACH veh=75 tile=1424,1874 dest=1424,1874 exit=1439,1874
1314 CPLRES veh=75 tick=39810 origin=1456,1873 destTile=1424,1874 destTd=0 node=1424,1874 root=0 ans=1 nTry=19 nOK=18 empty=0 vTile=1457,1873 vDest=1415,1866 gap=1 oSrc=other
1316 RESV-TERM veh=75 real=3 type=1 tile=1424,1874 td=0 tcount=-2 safe=1 tail=0 spd=72 vtile=1457,1873 why=pbs-single whole=1 exitdir=0 tick=39810 destTile=1415,1866 destEq=0
1348 RESV-WATCH CLEAR-STN tile=1424,1874 track=255 phase=choose-track actor=75 head=75
1349 ANSWER-UNRES veh=75 origin=1441,1874 td=0 reason=origin-not-in-answer
1350 ANSWER-DROP veh=75 reason=origin-not-in-answer
1364 RESV-PLAT-EDGE veh=75 start=1424,1876 last=1438,1876 exit=1439,1876 exitStn=0 dest=1424,1876 destStn=1 span=15 origin=1456,1873
1366 CPLRES veh=75 tick=39848 origin=1456,1873 destTile=1424,1876 destTd=0 node=1424,1876 root=0 ans=1 nTry=23 nOK=22 empty=0 vTile=1457,1873 vDest=1415,1866 gap=1 oSrc=other
1368 RESV-TERM veh=75 real=3 type=1 tile=1424,1876 td=0 tcount=-2 safe=1 tail=0 spd=15 vtile=1457,1873 why=pbs-single whole=1 exitdir=0 tick=39848 destTile=1415,1866 destEq=0
1370 SPD-DROP veh=75 tile=1457,1873 prev=17 order=1 real=3 stuck=0 fp=0 prog=255 wait=0 parked=0 stop=0 track=01 dtile=7644551 dt=7 ctx=red-signal
1371 DEPOT-ARR veh=75 spd=0 real=3(1) curType=1 stuck=0 tileDepot=0 tx=1457 ty=1873 destTx=1415 destTy=1866 destDepot=0 destResv=0 tileEqDest=0
```

### 9.2 判定（按 KI-683 的三条判据，落第三条）

1. **`exitStn=0`** ⇒ 站台循环在站台外第一格 `1439,1874` 就停住了，
   `:709` 的 `IsCompatibleTrainStationTile(tile, start)` 守卫生效 ⇒ **站台段本体没有溢出**。
   排除"循环真写过头"（那需要 `exitStn=1`）。
2. **`destStn=1`** ⇒ **预留终点本身落在站台格里**（`1424,1874` 是站台最西端，`span=15` 即 `1424..1438`
   整条平台）。`RESV-PLAT-REACH tile=1424,1874` 确认铺到站台末端。
3. **`destTile=1424,1874` ≠ `vDest=1415,1866`**（`RESV-TERM destEq=0`）⇒ 预留终点与真实目的地不符，
   **又是选点错**（KI-679 / REG-494-A 同源）。

⇒ **P2 定性：不是"站台循环写过头"，而是"预留终点被选在站台格上"**。于是从东侧 `1456,1873`
进站的整条路径被一路铺到站台最西端 `1424,1874`，视觉上就是"预留铺满 / 超出站台"；而列车真正要去的
`1415,1866` 反而**没有预留**（`destEq=0`）—— 与 P1 的"前方无预留"是同一病根的两种表现。

### 9.3 新证据：选点不是稳定常量，而是**在站台西端漂移**

同一 `origin=1456,1873`、同一 `vDest=1415,1866`，两次相邻尝试的选点分别是
`1424,1874` 与 `1424,1876`（**y 换了一条平行轨**），`nTry/nOK` 从 `19/18` 变 `23/22`。
第二次之后立刻 `1370 SPD-DROP ... ctx=red-signal`、`1371 DEPOT-ARR spd=0`（停在 `1457,1873` 不动）。
⇒ 与 REG-494-A 的"30+ 次恒为 `1457,1873/13`"**不是同一形态**：这里是**在站台西端附近抖动**，
但结果一样 —— 预留铺在列车不会走的那条轨 / 那个终止点上。

### 9.4 与 P1 的合并结论

两条现象同根：**`res_dest_tile` / safe position 选点错**（落在站台格、或落在平行轨）⇒ 预留铺一处、
列车走另一处。KI-680 的"部分预留仍放行"（`1078 nTry=3 nOK=2`、`1314 nTry=19 nOK=18`、`1366 nTry=23 nOK=22`
全部 `nOK<nTry` 且无回滚）把"铺错"进一步变成"只铺到一半就放行"，于是 P1 与 P2 在同一现场并存。

## 十、修复方案（分档，待拍板）

> 本节只出方案，**不动源码**。所有落点行号取自本轮实际读到的代码，编译前仍需按函数名复核
> （文件可能因别处改动移位）。改 `yapf_rail.cpp` 是 `.cpp`，可走增量；改 `train_cmd.cpp` 同理；
> 新增跨文件函数**一律不碰 `src\*.h`**（两份 `static` 各自一份，注释互指）。

### 10.0 先决疑点 REG-495-B（必须先落一行日志，否则 B/C 会修错方向）

`1081 RESV-TERM ... tile=1457,1873 td=0 tcount=2 safe=1` 里的 `safe=1` 不是 R3R 自报，
而是**引擎原生 `IsSafeWaitingPosition(consist, 1457,1873, td=0, true, forbid_90_deg)` 的返回值**
（打印点 `src\train_cmd.cpp:19815`）。也就是说：

- 若该格**有出站信号** ⇒ "预留停在信号前"是 PBS 设计，P1 的真因就变成
  **列车停在 `1457,1873` 后没有发起下一段预留**（`1370 SPD-DROP ctx=red-signal` 之后的
  `CRT found=0` 一直重试），修法应落在"预留耗尽时的重订触发"，而不是选点。
- 若该格**无信号、纯因"其他分支是死路/90°禁转"被判 safe** ⇒ 才是真正的选点错，修法落在
  `IsSafeWaitingPosition` 的 R3R 补充谓词。

判据行（复用现成 `R3RSafeWaitingWhyLog`，零行为）：在 `train_cmd.cpp:19810` 的
`RESV-TERM` 之前插一行 `R3RSafeWaitingWhyLog("resv-term", consist, res_dest.tile, res_dest.trackdir, true, _settings_game.pf.forbid_90_deg);`，
输出里看是 `signal` / `station` / `dead-end` / `90deg` 哪一支。
**这一步不通过，10.2 与 10.3 都不要动手。**

### 10.1 档 0：计数口径拆分（零行为，其它档的前置）

现在 `nTry` / `nOK`（`yapf_rail.cpp` 的 `r3r_ntry` / `r3r_nok`）把三类完全不同的结果混在一个
分母里：真失败、合法跳过（伙伴等待格 / 站台整格 air / 同格已订）、重复访问。
所以 `nOK<nTry` **不能**直接当"预留铺了一半"。`1081 tcount=2` 提示：`22/21` 很可能只是
"双轨格两根轨里订上一根"，属正常。

- 落点：`yapf_rail.cpp` 的 `TryReservePath()` lambda（`:1055` 一带）内，把现有各 `continue`
  分支分别 `++r3r_nskip`，`TryReserveRailTrackdir` 返回 false 的分支 `++r3r_nfail`。
- 落点：`CPLRES` 打印（`:1208`）追 `nSkip=` / `nFail=`，`RPFAIL`（`:1095` 附近）追 `fail=`。
- 纯探针，不影响判定。**B 档完全依赖这一档的 `nFail`。**

### 10.2 档 A（答案本，风险最低）：只收"干净答案"

第 493 轮的 `R3RAnswerFreeReservation()`（`yapf_rail.cpp:166`，用答案本 origin 沿预留清）
已经落地，本轮 `:1290` / `:1349` 也确有 `ANSWER-UNRES` + `ANSWER-DROP` —— **A 不是缺口，
问题是答案本里存的是"终点错"的答案**：`1080 ANSWER-STORE n=21 dest=1431,1874` 而实际铺到 `1457,1873`。

- 落点：`yapf_rail.cpp:2655-2658`
  ```cpp
  const bool r3r_res_ok = this->TryReservePath(target, node->GetLastTile());
  if (r3r_res_ok && r3r_nfail == 0 && !R3RResvEndIsUnsafe(v, this->res_dest_tile, this->res_dest_td)) {
      R3RAnswerCommit(v->index, v->dest_tile);
  } else if (r3r_res_ok) {
      R3RAnswerDrop(v->index, "end-unsafe");   // 已有函数，按实际签名补参
  }
  ```
- 效果：终点站台格/岔口格/有失败格的答案**不入本**，下次回放就会重新寻路，而不是照抄旧错答案。
- 风险：答案本命中率下降 ⇒ 大编组站台重复寻路开销回升（第 462 轮优化收益部分回退）。
  可接受：正确性优先，且只有"终点不安全"的少数才 drop。

### 10.3 档 C（治本，选点）：岔口格不得作为预留终点（无信号时）

- 新增同一份谓词，**两处各写一份 static**（不碰 `.h`，注释互指"必须逐字一致"）：
  `yapf_rail.cpp` 放在 `FindSafePositionProc` 之前；`train_cmd.cpp` 放在 `ExtendTrainReservation` 之前。
  ```cpp
  /* 预留终点可否停在此格：无信号的双轨岔口格不允许作为终点，
     否则预留停在岔口、列车过岔时选另一分支 ⇒ "不按预留走"。 */
  static bool R3RResvEndIsUnsafe(const Train *v, TileIndex tile, Trackdir td)
  {
      if (!IsValidTile(tile) || !IsPlainRailTile(tile)) return false;   // 站台/库/桥隧/道口不动
      const TrackBits bits = GetTrackBits(tile);
      if (CountBits((uint)bits) < 2) return false;                      // 单轨格不可能分叉
      if (HasSignalOnTrack(tile, TrackdirToTrack(td))) return false;    // 有信号 ⇒ 停在信号前合法
      const TrackBits other = bits & ~TrackToTrackBits(TrackdirToTrack(td));
      return other != TRACK_BIT_NONE;
  }
  ```
- 落点一（引擎侧扩展循环，权威）：`train_cmd.cpp:19732`
  ```cpp
  while (!IsSafeWaitingPosition(consist, res_dest.tile, res_dest.trackdir, true, _settings_game.pf.forbid_90_deg)
         || R3RResvEndIsUnsafe(consist, res_dest.tile, res_dest.trackdir)) { ... 继续向西扩一格 ... }
  ```
- 落点二（YAPF 选点 + 兜底搜索）：`yapf_rail.cpp` 的 `FindSafePositionProc`（`:288` 附近，
  即 `res_dest_tile` 被赋值处）在 `IsSafeWaitingPosition` 为真之后追加 `!R3RResvEndIsUnsafe(...)`；
  同文件 `stFindNearestSafeWaitingPos` 的安全位兜底（`train_cmd.cpp:17736` 的 `EXT-SAFE` 判据）
  必须同步加同一谓词，否则两条路会给出不同终点（第 386 轮续之四已因两处不一致踩过一次）。
- 风险：终点被推远 ⇒ 更多 `ok=0`（宁可"订不到"也不"订错"）；`forbid_90_deg` 语义下需确认
  没有把"合法会让线"误伤 —— 由 `CountBits<2` 与"有信号即放行"两道门限兜住。
- **不做**：不改 `IsSafeWaitingPosition` 本体（`pbs.cpp`，反 desync 冻结区）。

### 10.4 档 B（兜底，最后做）：真失败不准以 `safe=1` 收场

- 落点：`yapf_rail.cpp:1223` `if (target != nullptr) target->okay = true;` 之前
  ```cpp
  if (r3r_nfail > 0) { undo_reservation(); return false; }   // 只认真失败，不含 skip
  ```
- 为什么排最后：`nOK<nTry` 的主体极可能是"合法跳过 / 同格另一根轨"，先上这条会把正常挂接
  判成失败（负判据难通过）。**必须先有档 0 的 `nFail`。**
- 风险：失败率上升 ⇒ 出现更多 `TRP ok=0` / `STUCK`，需用负判据验证"正常场景不回归"。

### 10.5 建议落地顺序与自证

1. **档 0 + REG-495-B 那一行**（一次构建拿数据）；
2. 按 §10.0 的判读结果二选一：走 **档 A**（答案本）还是 **档 C**（选点）；
3. **档 B 最后**，且只对 `nFail>0` 生效；
4. 每档单独构建、单独复测，不合并提交。

验证断言（全部是现成标签，不需要新探针）：

| 档 | 断言 | 负判据 |
| --- | --- | --- |
| 0 | `CPLRES` 出现 `nSkip=/nFail=`，且 `nFail` 与 `RPFAIL` 行数对得上 | 无 |
| A | 出错场景 `ANSWER-STORE` 消失或 `ANSWER-DROP reason=end-unsafe` 出现 | 正常大站台：`ANSWER-STORE` 仍出现、`Ans-hit` 不明显下降 |
| C | 同 origin 下 `RESV-TERM tile=` 不再等于岔口格（`1457,1873` / `1424,1874`），`destEq` 趋近 1 | 普通直行线 `RESV-TERM` 逐字不变；站台挂车 `RESV-PLAT-EDGE` 不变 |
| B | `RESV-TERM why=` 不再在 `nFail>0` 时出现 `pbs-single safe=1` | 挂接成功数不下降、无新增 `STUCK` |

### 10.6 本轮明确不做

- 不动 `pbs.cpp`（`IsSafeWaitingPosition` / `CheckTrainsOnTrack` 反 desync 区）；
- 不动 `ReserveRailStationPlatform()`（`exitStn=0` 已证守卫生效）；
- 不为"挂接侧 P1"开新分支 —— 挂接与普通寻路共用 `TryReservePath` + 同一 `res_dest` 选点，
  档 C 一次覆盖两侧；挂接独有的 `:1262` 早退不影响选点。

### 10.7 待拍板清单（按编号回填即可）

- **Q-1**：是否按"档 0 + REG-495-B 一行日志"先走一次构建（零行为、只取 `safeWhy` 与 `nFail/nSkip`）？
- **Q-2**：若只给一档预算，优先 **A（答案本，保守、收益小）** 还是 **C（岔口不作为终点，治本、风险中）**？
- **Q-3**：档 B（`nFail>0` 一律不准以 `safe=1` 收场）能否接受"失败率上升、换正确性"？默认建议=能，但放最后。
- **Q-4**：挂接侧那条（`CPL-FT old=1442,1874 → 1440,1874`）需要新现场日志：车号 + 道岔格坐标 +
  大致时刻，用于确认是否与普通寻路同一 `res_dest` 选点面。是否方便补一条？
- **Q-5**：是否允许在档 C 落地时**顺带**把 `EXT-SAFE`（`train_cmd.cpp:17736`）与
  `FindSafePositionProc`（`yapf_rail.cpp:288`）两处谓词合并为同一份实现（各写 static，不改 `.h`）？

## 十一、玩家澄清后的修订（2026-11-24，REG-495-B 结案）

### 11.1 澄清原文与直接推论

玩家原话：**「1457,1873 是 PBS 信号灯的前一格，符合原生安全位条件」**。

直接推论（三条，均无需再取日志）：

1. **REG-495-B 结案，判定=`design`（设计使然）**。`1081 RESV-TERM tile=1457,1873 td=0 tcount=2 safe=1`
   是 `IsSafeWaitingPosition()` 的正确返回：PBS 分段预留的合法终点就是"信号前格"。⇒ 我之前列的
   "若该格有信号 ⇒ 真因是没发起下一段预留" 这一支成立，**选点无错**。
2. **§10.3 档 C（禁"岔口格"当终点）作废**。`1457,1873` 的 `tcount=2` 是双轨格，但它是**信号前格**，
   不是"无信号岔口"；原谓词 `HasSignalOnTrack(tile, TrackdirToTrack(td))` 会把信号装在**邻格**
   （`1458,1873`）的情形漏掉——即"信号前格本身无信号"是常态，谓词方向反了。此档整段不再执行。
3. **P1 的真 bug 面收窄为一条**：`1078 RPFAIL tile=1457,1873` + `1079 CPLRES destTile=1457,1873
   nTry=22 nOK=21` ⇒ **失败的恰好是终点（安全位）那一格**，21 格都铺上了、终点格没订上，
   却仍按成功返回（`:1223 target->okay = true`）并 `1080 ANSWER-STORE n=21`。
   列车开到 `1456,1873`（终点前）后前方无预留 ⇒ 过 `1478,1873 tracks=0x11` 那个岔口时
   按自身判断走 ⇒ 这就是"不按预留走"。**B 档从"可能误伤"升级为唯一主修。**

### 11.2 修订后的修法（两档，原 A 保留降级为配套）

- **B 主修（必做）**：终点格失败即整体失败，不许 `okay=true`。
  落点 `yapf_rail.cpp:1223` 之前：
  ```cpp
  /* 终点(安全位)那一格没订上 ⇒ 前方无预留可依，不得宣称成功；
     否则列车开到安全位前一格就会在岔口自行选路（KI-682 现场 1478,1873 tracks=0x11）。 */
  if (r3r_res_fail_at_dest) { undo_reservation(); return false; }
  ```
  判据不需要 `nFail>0` 这种粗粒度（会误伤"双轨格订上一根"），只要
  `res_fail_tile == res_dest_tile`（失败格就是目标安全位）就失败。**这一条不需要档 0 的
  `nSkip/nFail` 拆分**，因为 `res_fail_tile` 是现成变量，`RPFAIL` 已经打印了它。
- **A 配套（保留）**：`yapf_rail.cpp:2655` 的 `R3RAnswerCommit` 加同一条件
  `&& !r3r_res_fail_at_dest`，否则答案本会反复存"终点缺一格"的答案（现场 `:1080` 正是如此）。
- **B 的回滚要点**：`:1127` 的 `undo_reservation()` 已能逐格回收；终点格本身没订上，无需额外释放，
  但必须 `restore_res_fail_state()`/清 `res_fail_tile`，否则下轮 `CPLRES` 读到旧失败坐标。

### 11.3 仍未定（P2，新增提问 Q-6）

P2 的 `1424,1874` 是**站台最西端格**（`destStn=1`、`span=15`），它与 P1 的 `1457,1873`
**可能同构**：站台出口 PBS 信号的前一格也在站台上，同样合法。若如此，P2 的"超出站台"其实是
正常分段预留，"真目的地 `1415,1866` 无预留"也是正常（过了信号会再订一段）。

- **Q-6**：`1424,1874` 是否也是某一盏 PBS 信号的前一格？若是 ⇒ P2 与 P1 完全同构，只做 B+A 即可
  同时收口两条；若不是 ⇒ P2 另有选点错，档 C 需改成"站台格只在作为某 PBS 信号前格时才可作终点"。

### 11.4 位置事实更正 + Q-6 结案（2026-11-24，玩家第二次澄清）

玩家原话：**「并不是前一格，而是后一格，也就恰好是 PBS 信号灯所在格；`1424,1874` 是某个 PBS 信号的前一格」**。

1. **11.1 第 3 条里"列车开到安全位前一格 `1456,1873`"作废**。正解：沿行驶方向 `1456,1873` 是
   安全位 `1457,1873` 的**后一格**，并且它**就是那盏 PBS 信号灯所在的格**。
2. **物理链条据此重排（与日志更贴合）**：`1079 CPLRES destTile=1457,1873 node=1456,1873 nTry=22
   nOK=21` ⇒ 回溯链末端恰好停在 `1456,1873`（信号格），而真正该当终点的安全位 `1457,1873` 是
   `1078 RPFAIL` 那一格、**根本没订上**。于是预留的实际覆盖面 = 到 `1456` 为止，列车一路开到这个
   "最后一格有预留"的信号格上停住，前方（`1457` 起）零预留 ⇒ 下一次驱动只能自行选路。
   ⇒ **停在信号格比停在安全位更糟**：它既没在安全位等待，又压住了信号格，等于"欠一格还报成功"
   的最坏形态。这是 B 主修必须做的直接理由。
3. **Q-6 结案 = 是**：`1424,1874` 也是某 PBS 信号的前一格 ⇒ P2 与 P1 完全同构。
   ⇒ `27171` 的"①选点错（C 形态）"与 `27173` 的"P2=选点错"两条定性一并**证伪/降级**；
   **不需要任何选点类修法**（档 C 永久作废，也不再需要"站台格仅在作为信号前格时才可作终点"这种补丁）。
   修法收敛为两处：**B 主修 + A 配套**，各一行判据。
4. **误伤面复核（对 B 判据的一次压测）**：`RESV-SELF-AIR`（`pbs.cpp:633`/`:691`）与
   `RESV-PAIR-AIR`（`:621`/`:683`）都是 `return true`、不写 `res_fail_tile` ⇒ 判据
   `res_fail_tile == res_dest_tile` 只会在**真失败**时成立，"自己的位当空气"这类合法跳过不会误触。
   故 B 不需要 `nSkip/nFail` 拆分（档 0 彻底不必做）。
5. **新增风险项（记入复测判据）**：若 `1457,1873` 是被外来预留长期占住（`RESV-PAIR-AIR` 从未命中
   它，只有后来的 `1282 RESV-SELF-AIR` 命中），B 会让该车每 tick 失败——诚实但可能停驶。
   复测时看 `RPFAIL` 是否退化为刷屏；若刷屏，再补"同一车连续 N 次失败 ⇒ 回退截断预订"的兜底
   （本轮**不做**，避免一次改两件事）。

### 11.5 【重大更正】B 判据被代码证伪：`RPFAIL` 是探针误报，预留本身没有洞（2026-11-24，动手前）

玩家已批准「按 B+A 开工」，但在写第一行前读码发现 **B 的前提不成立**，`res_fail_tile ==
res_dest_tile` 在本现场恒为假 ⇒ B+A 是**空操作**，不能按原案落地。

**(1) `ReserveSingleTrack()` 的返回语义（`src\pathfinder\yapf\yapf_rail.cpp:768-882`）**
- 真失败只有两处：站台 `:779`、普通轨道 `:851` —— 两处都 `res_fail_tile = tile; return false;`
- 成功路径结尾是 `:881 return tile != this->res_dest_tile || td != this->res_dest_td;`
  ⇒ **"这一格就是预留目标格"时返回 false，但位已经订上了**，这是"到点收手"的正常语义，
  **不写 `res_fail_tile`**。

**(2) 于是 `:1081-1084` 的统计把"到点收手"误记成失败**
```
++r3r_ntry;
if (r3r_tok) ++r3r_nok;
else if (!r3r_stop) { ... 打 RPFAIL ... }   // 只排除了 r3r_stop，没排除 tile==res_dest 的到点
```
现场 `nTry=22 nOK=21` 的来源就是：22 次 iterate 里最后一次正是终点格，返回 false（到点）、
被算成"失败"，少 ++ 一次 `nOK`。`1078 RPFAIL tile=1457,1873 nTry=3 nOK=2` 同理。
⇒ **`RPFAIL` 行不是"没订上"的证据**，我上一轮把它读反了。

**(3) 真失败的兜底早就存在**：`:1112 if (this->res_fail_tile != INVALID_TILE)` ⇒ 回滚整条预留
（`:1114-1120` 逐 node `UnreserveSingleTrack`）后 `return false`（`:1123`），根本走不到 `:1223`。
而现场 `:1079 CPLRES`（打印点在 `:1180-1221`，位于 `:1112` 之后）确实打出来了 ⇒ **那次调用
`res_fail_tile` 是 `INVALID_TILE`** ⇒ 1457 那一格是订上了的。

**(4) 结论：`yapf_rail.cpp` 里不存在"有洞还报成功"的漏洞。** B/A 原案作废，需要换落点。
新的候选真凶（**待验证**）：`ExtendTrainReservation()`（`src\train_cmd.cpp`，约 `:17431`）里
`TryReserveRailTrackdir()` 失败只 `break`（第 471 轮的 `EXT-RES ext_res_ok=0` 探针就是打在这里），
既不回滚也不告诉调用方 ⇒ 安全位那格订不上时，"延伸"静默半途而废，而列车照样开到最后一格有
预留的格（信号格）上停住 —— 与"停在 1456 而不是 1457"更吻合。判定所需数据：`EXT-RES` 里
`tile=1457,1873 ok=0` 的那一行，以及紧随其后是否有第二次 `EXT-ENTRY` 重试。

**(5) 顺带**：`CPLRES node=1456,1873` 取的是 `res_dest_node->GetLastTile()`（`:1184`），
和 `destTile=1457,1873`（`res_dest_tile`）不是同一个量，不能再把二者当"末格=目标格"来读。

### 11.6 玩家裁决 B1+B2，附加条件「答案本与挂接寻路分离」已核查＝已分离（零工作量）

**玩家原话（2026-11-24）**：「可以改道B1+B2，但需要提醒，放弃修复A需要你保证答案本和挂接寻路分离
（如果工作量大，你可以搁置）」。

**结论：这道闸门代码里早就装好了，而且是双向的，不需要任何新工作。**

| 方向 | 锚点 | 判据 |
|---|---|---|
| 写侧（进答案本） | `src\pathfinder\yapf\yapf_rail.cpp:1012-1013` | `const bool r3r_record_answer = record_answer && !Train::From(Yapf().GetVehicle())->current_order.IsType(OT_GOTO_COUPLE);` |
| 写侧（挂接两个调用点传参） | `:1856`（单段挂车回溯）、`:2375`（长预留） | 两处 `this->TryReservePath(..., false)` ⇒ `record_answer=false` |
| 读侧（抄答案本） | `:1262` | `if (veh->current_order.IsType(OT_GOTO_COUPLE)) return false; // 挂接寻路有独立语义，不抄答案` |

双重闸门的用意写在 `:1005-1010` 的注释里（玩家 2026-11-23 拍板原文口径）：「挂接寻路"不需要也不应该"
用答案本 —— 挂接的答案取决于等待车底的实时位置与配对锁，跨 tick 复用会拿旧终点去抄；而且挂接时
`dest_tile` 是车底所在格，与普通寻路的 key `(v->index, v->dest_tile)` 天然会撞」。所以写侧除了
调用点传 `false`，还在函数体内兜了一层订单判据，「防止将来新增挂接调用点忘传参又污染答案本」。

**因此：**
1. 放弃 A 与「分离」无关 —— A 原本的判据（`res_fail_tile == res_dest_tile`）在 §11.5 已被证伪为恒假，
   它即便落地也是空操作；A 既不进答案本、也不碰挂接寻路。
2. 挂接机车在一次 `TryPathReserve` 里确实会先后走「`ExtendTrainReservation`（直段）+ `ChooseTrainTrack`
   → `CYapfFollowRailT::ChooseRailTrack`」，但它走到 `ChooseRailTrack` 时 `current_order` 仍是
   `OT_GOTO_COUPLE` ⇒ `R3RAnswerReplay()` 在 `:1262` 直接返回 false，原生 YAPF 照常跑；
   `R3RAnswerCommit()` 的入口 `r3r_record_answer` 同样为 false ⇒ 不写表。
3. B2 改的是 `ExtendTrainReservation` 内部的失败回滚，作用域是该次调用的试铺位，与答案本的表
   （`_r3r_answer_steps` / `_r3r_answer_dest`，key 只含普通寻路）没有任何共享状态 ⇒ 互不影响。
   唯一需要守的纪律：**B2 的回滚只能撤本次 Extend 自己订的位**（沿用既有 `R3RReleaseReservationChecked`
   / `UnreserveRailTrack` 逐格回滚面），不得扩到"整条路径全撤"——否则会把该车已有的、由答案本或
   上一次成功预留持有的位一并撤掉。

**工作量评估：0。** 不需要为「分离」新增代码、探针或构建；只需在 KI 里固化上述三条锚点，
供以后有人想给挂接寻路接答案本时先看到这条禁令。

**下一步（按玩家批准开工）**：B1 修探针（`yapf_rail.cpp:1081-1084` 把「到点」从失败统计里排除，
新增 `nAtDest` 并打进 `CPLRES`，零行为变更）；B2 修延伸（`ExtendTrainReservation` 的
`TryReserveRailTrackdir()` 失败分支改为回滚本次试铺并告知调用方）。

### 11.7【更正 §11.5 末段】B2 的回滚**早就存在**，我上一轮「只 break 不回滚」的判断被代码证伪

**读码事实（`src\train_cmd.cpp`，第 495 轮实施前复核）**：

```
:17794   const bool r3r_ext_res_ok = TryReserveRailTrackdir(v, tile, cur_td);
:17798   探针 EXT-RES ... ok=%d
:17803   if (!r3r_ext_res_ok) { r3r_backout_reason = "reserve-fail"; break; }
:17806   if (ft.err == Owner || ft.err == NoWay) { ...; return PBSTileInfo(ft.old_tile, ft.old_td, true); }
:17823   /* Sorry, can't reserve path, back out. */
:17827   R3RResvPhaseGuard r3r_phase_backout("extend-backout", R3RResvActorID(v));
:17857   R3RDbgWrite("EXTEND-BACKOUT veh=%u origin=%d,%d/%d stop=%d,%d/%d next=%d,%d reason=%s ...")
:17885   while (tile != stopped || cur_td != stopped_td) { if (!ft.Follow(tile, cur_td)) break; ... UnreserveRailTrackdir(tile, cur_td); }
:17913   if (temporary_slot_state.IsActive()) temporary_slot_state.PopFromChangeStackRevertTemporaryChanges(v->index);
:17916   return PBSTileInfo();      // 空 = 失败，调用方无需额外通知
```

三条结论：

1. `:17803` 的 `break` **不是**裸跳出——它跳出 `while (ft.Follow(...))` 后直接落到 `:17823` 的
   back-out 块，那里按 `stop=ft.old_tile`（= 最后一格已被 Follow 接受的格，即已订上的那格）
   一路 `UnreserveRailTrackdir` 回撤到 `origin`，连 `temporary_slot_state` 都回滚了。
   ⇒ **「失败即回滚」是第 243 轮就已落地的既有行为**（`REG-243-O`），不是缺口。
2. 失败并非静默：`EXTEND-BACKOUT ... reason=reserve-fail stop=x,y next=x,y` 是既有日志
   （第 274 轮续还专门把它从白名单里放回来并做了节流），只是我上一轮没有按这个标签去搜。
3. 返回值 `PBSTileInfo()`（空 tile）本身就是「告知调用方」的契约，`TryPathReserve` 侧据此
   走兜底分支，无需新增出参。

**⇒ B2 原案作废（不是改道，是它本来就做完了）。** 现在 B2 只剩一件**验证性**工作：
把 `1447,1874 ok=0` 那两次去对账 `EXTEND-BACKOUT ... reason=reserve-fail`。若能对上，
则 P1 现场与 Extend 失败无关，真凶必须另找（下一步优先怀疑：
① 预留止于信号格 1457,1873 后列车为何不再前进；② `CPLRES`/`RESV-TERM` 的
「到点收手」是否被上层误判为失败而撤销整条预留）。

**已完成（无需改码）**：B1 的前提同样要重核 —— `:1084 else if (!r3r_stop)` 已经把「到点」
排除在 RPFAIL 之外（第 462 轮方案乙），残留问题只是 `:1081 ++r3r_ntry` 仍把到点那一格
计进分母，使 `nOK<nTry` 看起来像失败。这一项才是 B1 的真实内容，且是纯探针修正。

### 11.8 对账结果：`1447,1874 ok=0` 的 EXTEND-BACKOUT 全部对上，且**与 P1 不是同一现场**

`build\R3R_debug.log` 内 `EXTEND-BACKOUT ... next=1447,1874 reason=reserve-fail` 共 **4 行**：

```
:3619  veh=15 origin=1449,1874/0 stop=1448,1874/0 next=1447,1874 reason=reserve-fail err=0 owner=-1 whole=0 track=-1 need=0x0 resv=0x0 led=-1 n=1
:3682  veh=15 origin=1448,1874/0 stop=1448,1874/0 next=1447,1874 reason=reserve-fail ... n=2
:3753  veh=15 origin=1448,1874/0 stop=1448,1874/0 next=1447,1874 reason=reserve-fail ... n=3
:3820  veh=15 origin=1448,1874/0 stop=1448,1874/0 next=1447,1874 reason=reserve-fail ... n=4
```

读法：
- `need=0x0 resv=0x0 led=-1` ⇒ 失败格 `1447,1874` 上**既没有要跨的轨道位、也没有已被订的位、
  账本里更没有主人**。这不是「被别人占了轨」，而是 `TryReserveRailTrackdir` 在该格/该 trackdir
  上根本没有可订的位（`owner=-1 whole=0 track=-1` 也是这个意思）。
- `stop=1448,1874 == origin=1448,1874`（n=2..4）⇒ 回滚退到了起点，**没有残留半截预留**；
  n=1 那次 `origin=1449,1874 stop=1448,1874` 是"订上了 1448、到 1447 失败"，也只多退一格。
- `ord=16`（GOTO_COUPLE）⇒ 这是**挂接机车的普通寻路阶段**在从车脚下往等待车底走的路上
  早退，随后由 YAPF 接管（`target->okay` 由答案本/YAPF 侧负责）。

**⇒ 与 P1（veh=75 停在 1456,1873、预留止于信号格 1457,1873）没有任何交集**：坐标不同
（y=1874 vs 1873）、车号不同、订单语义不同（挂接 vs 普通行车）。§11.5 把这两行当作
「P1 的真凶证据」是**过度关联**，现予撤回。

**回到 P1**：真正的待答问题只剩一个 —— `1457,1873`（PBS 信号灯所在格）被订下之后，
列车为什么不前进。这既不是 Extend 失败（Extend 在 P1 车次上没报 backout），也不是
YAPF「有洞报成功」（`:1112` 保证有洞必回滚）。下一步应查 P1 车次（veh=75）的
`CPLRES` / `RESV-TERM` / `CRT` / `TRP` 行：看 `okay=1` 之后列车侧是否因
`destTile` 未达、`res_dest_node` 为空或「到点收手」被上层判成不完整而不再推进。

### 11.9 P1 全链条取数（veh=75）——「终点格没订上」彻底证伪，真凶是**红旗 + 预留目标换线**

`build\R3R_debug.log` 里 `1456,1873|1457,1873` 相关行（共 37+ 条）拼出完整链条：

```
:1078 RPFAIL veh=75 tick=38844 origin=1479,1873 tile=1457,1873 td=0 ans=1 nTry=3 nOK=2 vehTile=1484,1875 vehTrk=0x1
:1079 CPLRES veh=75 tick=38844 origin=1479,1873 destTile=1457,1873 destTd=0 node=1456,1873 root=0 ans=1 nTry=22 nOK=21 empty=0 vDest=1431,1874 gap=5 oSrc=other
:1081 RESV-TERM veh=75 real=3 type=1 tile=1457,1873 td=0 tcount=2 safe=1 why=pbs-single whole=-1 destEq=0
:1282 RESV-SELF-AIR veh=75 tile=1457,1873 track=0 kind=rail dist=1     ← 该格该轨**已经是自己的预留**
:1283 EXT-ENTRY veh=75 origin=1457,1873/0 order=1 spd=72              ← 列车此刻就压在 1457 上，速度 72
:1285 EXT-RES  veh=75 tile=1456,1873 ok=1
:1287 EXT-CHOICE veh=75 stop=1456,1873 next=1455,1873 tracks=0x9      ← 岔口，交给 YAPF
:1304 CRT veh=75 order=1 dir=1 origin=1456,1873 found=1
:1311 RESV-PLAT-EDGE veh=75 start=1424,1874 last=1438,1874 exit=1439,1874 exitStn=0 dest=1424,1874 destStn=1 span=15
:1314 CPLRES veh=75 tick=39810 destTile=1424,1874 node=1424,1874 nTry=19 nOK=18   ← 又是差 1
:1316 RESV-TERM veh=75 tile=1424,1874 tcount=-2 safe=1 whole=1
:1317-1336 LINEND veh=75 tile=1457,1873 sig=1 near=0 spd=72→15 brk=225→15 clamp 交替  ← 一路刹车
:1364 RESV-PLAT-EDGE veh=75 start=1424,**1876** last=1438,1876 ...            ← 目标换到平行线！
:1366 CPLRES veh=75 tick=39848 destTile=1424,1876 nTry=23 nOK=22              ← 又差 1
:1369 RESV-SELF-AIR veh=75 tile=1457,1873 track=0 kind=rail dist=0
:1370 SPD-DROP veh=75 tile=1457,1873 prev=17 order=1 real=3 stuck=0 fp=0 prog=255 ... ctx=**red-signal**
```

**三条硬结论：**

1. **`nOK = nTry - 1` 是恒定形态，三次都是差 1**（22/21、19/18、23/22）⇒ 那一格不是"随机订不上"，
   而是每次预留在**终点格**走 `:881 return tile != res_dest_tile || td != res_dest_td` 的「到点」
   分支（返回 false 但位已订、且**不写 `res_fail_tile`**）。旁证：若真失败，`:1112` 会回滚并
   `:1123 return false`，**根本走不到 `:1180` 的 `CPLRES` 打印**；而 `:1282 RESV-SELF-AIR
   tile=1457,1873` 直接证明该格该轨已在 veh=75 名下。⇒ **§11.1 第 3 条与 §11.4 第 2 条的
   「21 格铺上、终点格没订上」全部作废**，B 档的前提确实不存在（与 §11.5 一致）。
2. **`RPFAIL` 的误导性有了量化证据**：它打在 `nTry=3`（第一次碰到终点格时），而 `CPLRES`
   的最终值 22/21 —— 同一变量在两个时点打印，读日志的人极容易把"差 1"当成"洞"。
   ⇒ B1（把「到点」单列一个计数器并进 `CPLRES`）是**必要**的，否则这条误判会重犯。
3. **P1 的真凶换向**：`LINEND ... sig=1` 连续 20 行 + `SPD-DROP ... ctx=red-signal` ⇒ 列车
   停在信号格 1457,1873 是**红灯**造成的，不是"前方无预留"。而红灯期间预留目标在
   `1424,1874` 与 `1424,1876` 之间**换线**（`:1311` → `:1364`），这正是
   `R3R_junction_unsafe_resv_round378_memo.md §22.3` 记过的「YAPF 终点被甩到平行站台轨」同族形态。
   ⇒ 下一步取证方向应转向「目标为何从 1874 线跳到 1876 线」，而不是 Extend/YAPF 的洞。

**B1 + B2 的最终范围（据此收窄）：**
- **B2：不改码**。回滚 + 日志 + 空返回值三重俱全（§11.7），且与 P1 无关（§11.8）。
- **B1：做，但只能做「纯字段追加」**。不把到点格从 `r3r_ntry` 里剔除（否则单格到点时
  `empty=(r3r_ntry==0)` 会误报），只新增 `r3r_natdest` 计数 + 在 `RPFAIL` 行尾加 `atDest=`
  + 在 `CPLRES` 行尾加 `nAtDest=`，既有字段与顺序逐字不变。

### 11.10 B1 落地（已改码，仅 `src\pathfinder\yapf\yapf_rail.cpp`，未碰任何 `.h`）

**唯一的实现选择差异（须记清，否则下轮读日志会误解）**：`atDest=` 打的是**本行级别**的布尔
（这一行是不是"到点"），不是累计值；累计值只出现在 `CPLRES` 的 `nAtDest=` 上。

| 位置 | 改动 | 语义 |
| --- | --- | --- |
| `:1050` 附近 | 新增 `int r3r_natdest = 0;` + 注释（§11.9 的现场锚点） | 只读计数 |
| `:1089-1098` | 新增 `const bool r3r_at_dest = !r3r_tok && !r3r_stop && tile == res_dest_tile && td == res_dest_td;`，为真则 `++r3r_natdest;` | 判据逐字对齐 `:881` 的到点分支条件 |
| `:1112` / `:1124` | `RPFAIL ... atDest=%d\n`，实参 `r3r_at_dest ? 1 : 0` | **不改变 RPFAIL 的触发条件**（仍是 `!r3r_stop`），只是让它自我标注 |
| `:1216` / `:1233` | `CPLRES ... nAtDest=%d\n`，实参 `r3r_natdest` | 汇总值，可直接对账 `nTry - nOK - nAtDest == 真失败格数` |

刻意**不做**的三件事（避免把探针修正变成行为改动）：
1. 不把到点格从 `r3r_ntry` 剔除 —— 单格到点时 `empty=(r3r_ntry==0)` 会误报成"循环一次没进"。
2. 不改 `CPLRES` 的签名去重（`:1217-1221` 的 `r3r_cplres_sig` 里**没有** `nAtDest`）—— 加了会
   让本应被抑制的重复行重新打出来，探针总量被动增加；`nAtDest` 与 `nTry/nOK` 强相关，
   实际不会出现"nTry/nOK 不变而 nAtDest 变"的漏打。
3. 不动 `res_fail_tile` / 回滚路径 / `return` 值 —— B1 全程零行为变更。

**构建自证**：复用既有 `R3R_fullbuild.cmd inc detach`（未新建任何 `.cmd`，符合长期规则）；
`build\R3R_incbuild.guard.log` = `GUARD: incremental is safe`；日志 `[2/3] yapf_rail.cpp.obj` →
`[3/3] Linking CXX executable openttd.exe`；尾行 `==== R3R build ended 2026/11/24 5:07:24
MODE=inc EXIT_CODE=0`；`read_lints` 对该文件 0 条。

**B2 的最终交付 = 一份对账结论，不是代码**：玩家批准的是 B2，但 §11.7 已用代码证明
"break 之后必有 `:17823` back-out + 逐格 `UnreserveRailTrackdir` + `R3RResvPhaseGuard("extend-backout")`
+ 空 `PBSTileInfo()` 返回值"，且 §11.8 证明现场那 4 行 `EXTEND-BACKOUT` 全是挂接侧 veh=15、
与 P1（veh=75 普通行车）无交集。⇒ 本轮**不为 B2 写任何代码**，避免凭空加一层与既有回滚重复的清扫。
