# R3R 第 181 轮 临时分析报告：现场三条（无 48 号车 / 段号与名称借用 / 末尾解挂异常）

- 日期：2026-10-01
- 现场日志：`build\R3R_debug.log`（8170 行；`=== R3RDUMP-BEGIN ===`:4 / `=== R3RDUMP-END ===`:156 只有一对 ⇒ **单次会话**，不是多次追加）
- 玩家原文：①「这个场景根本没有 48 号车」②「第 2 次解耦、第 4 次耦合之后，出现了段编号、名称和链编号借用，名称借用」③「日志的最后，解挂命令出现异常，可按最后一次耦合定位（也有我调度命令的问题，但要防玩家未知行为）」
- 本轮状态：**只取证 + 定性，未改任何源码**。

## 〇、事件序号表（后面按此计数）

`COUPLE-OK` 8 次：244(#1 loco=24)、1164(#2 loco=27)、2457(#3 loco=27)、**3248(#4 loco=50 rear=23 consist=6)**、4632(#5 loco=48 rear=6)、5515(#6 loco=29 rear=6)、6871(#7 loco=27 type=6)、**7873(#8 最后一次 loco=27 rear=23 consist=23 real=20 type=15)**。

`DECOUPLE-FIRE`：773、**1498(第 2 次 consist=27 tx=58 ty=28 real=5 mode=1 segs=2 eff=6)**、2006、2833、3664、5108、5896。

## 一、第 1 条「没有 48 号车」= 借用改掉了它的可见身份

```
 59:R3RDUMP idx=48 et=523 sub=09 tile=60,79 un=6 FE=1 ENG=1 VIRT=0 ARTH=1 ARTM=0 SEGF=1 ... head=48 tail=50
 60:R3RDUMP idx=49 ... ARTH=1 ARTM=1
 61:R3RDUMP idx=50 ... ARTM=1
149:R3RDUMP-CHAIN head=48 n=3 inDepot=1 prio=1 ordCount=6 realType=6 tile=60,79 depotIdx=2
```

- 48/49/50 是**真铰接组**（ARTH/ARTM，et=523）、停在车库 60,79、链头带 ★。
- 关键：**48 的活车号 `un=6`，且第 59 行是读档后的第一份 dump ⇒ 存档里就已经是借来的号**。
- 同会话内它又被借走名字：
```
4631:NAME-XFER head=48 u=6 head_own=0 got_u=1     ← 48 自己无名，借 6 的名
4691:CTRL-PARK tag=couple head=48 seg=1 unit=6    ← 可见号=6
5185:LOCO-AFTER-DECOUPLE veh=48 ...               ← 解耦了，但全日志无 NAME-RESTORE veh=48
```
⇒ 玩家在界面上看到的是「6」（号/名），所以「找不到 48 号车」。**这是第 2 条同一根因的界面可见面，不是幽灵车。**

## 二、第 2 条：段编号 / 名称 / 链编号借用

### 2.1 段编号：出现「nseg=2 但 seg=3」

| 事件 | SEGID-SYNC nseg | CTRL-PARK seg | settle 实况 |
|---|---|---|---|
| depot-edit :172/174 | 2 | 1 | segid=1(0..5)/2(6..23) 自洽 |
| #1 :309/310 | 3 | 3 | 自洽 |
| #2 :1231/1232 | 3 | 3 | 自洽 |
| **第 2 次解耦 :1507/1508** | **2** | ctrl=0 | 1511+ : segid=1(0..5) + **segid=3**(27..29) |
| #3 :2517/2518 | 2 | 1 | 自洽 |
| **#4 :3306/3307** | **2** | **3** | 3310-3312: **segid=3**(50,49,48)、3313+: segid=1(6..) |
| #5 :4690/4691 | 2 | 1 | 自洽 |
| **#6 :5573/5574** | **2** | **3** | 5577-5579: **segid=3**(29,28,27)、5580+: segid=1(23..) |
| #7 :6894/6895 | 2 | 1 | 自洽 |
| #8 :7940/7941 | 3 | 1 | 7944+: 1(27..29)/2(6..23)/3(0..5) 自洽 |

```
3306:SEGID-SYNC tag=couple head=50 nseg=2
3307:CTRL-PARK tag=couple head=50 seg=3 unit=6      ← 段 3，链上只有 2 段
3310:GEO settle-couple n=0 veh=50 ... segid=3
3313:GEO settle-couple n=3 veh=6  ... segid=1
```
⇒ 段 ID 是**全局递增分配**而非链内 1..n，玩家观感即「段编号借用/跳号」。分配器是否只增不减、跨链是否会重复发放：见 §五 R-1。

### 2.2 段属性错位

```
7933:CHAIN-ATTRS head=27 n=27 FE=2 SEG=3 pow=11831 wt=808 len=105 spd=120
7934:  SEG idx=27 pow=11831 wt=808 len=105 spd=120
7935:  SEG idx=0  pow=0 wt=0 len=0
7936:  SEG idx=6  pow=0 wt=0 len=0
```
段头枚举正确（27/0/6），但**只有第一个段头带属性，其余全 0**。同形态见 3302-3304(head=50)、5569-5571(head=29，带属性的是 idx=6 / idx=23 而非链头段)。⇒ 段 pow/wt/len 的归属口径可疑（链头段反而全 0）。

### 2.3 名称借用：两处只借不还

`NAME-RESTORE` 全日志 4 条：847(veh=24)、1577(veh=27)、2896(veh=27)、3727(veh=50)。
```
4631:NAME-XFER head=48 u=6 head_own=0 got_u=1   →  解耦 5185 处 无 NAME-RESTORE veh=48   ❌
#6 couple 29（:5515）                             →  连 NAME-XFER head=29 都没有         ❌
7873 最后一次耦合 loco=27                         →  末尾无 NAME-XFER head=27             ❌
```
`head_own=0` = 链头自己无名，于是借走 u=6 的名；界面上该列车显示成 6 的名。

### 2.4 链编号/号侧借用（现行设计层）

```
3307:CTRL-PARK tag=couple head=50 seg=3 unit=6
4691:CTRL-PARK tag=couple head=48 seg=1 unit=6
7941:CTRL-PARK tag=couple head=27 seg=1 unit=7
7943:INVAR-CTRL tag=couple head=27 nseg=3 ctrl=6 ctrl_pri=1 head_pri=3 borrowed=1
7872:ORD-XFER keep veh=27 owner=6 cur_real=5 parked_real=9
```
链头可见号来自控制段（ctrl=6 / ctrl=23），即第 159/160/161 轮的「控制段特质借用」。玩家所说「链编号借用」在这一层。

## 三、第 3 条：末尾解挂异常（锚在最后一次耦合 7873）

```
7871:RESPACE-AFTER-EDIT couple head=27 px=1 stretch=1 unfixed=0 front=1 visited=26
7872:ORD-XFER keep veh=27 owner=6 cur_real=5 parked_real=9
7873:COUPLE-OK loco=27 rear=23 consist=23 co=1 real=20 type=15 tx=58 ty=26
7902:ORD-AFTER-COUPLE head=27 n=29 real=20 impl=20 tt=20 owner=6 u_has_orders=0
7923:  ORD idx=20 type=15 dest=0      ← ★ 耦合后索引正好停在 DECOUPLE 上
7937:NOCAB-SET this=27 db=1 last=23 lastSub=0x04 lastEng=0 lastLead=0
7938:CGRP-NORM-UNION head=27 nseg=3 mask=0
（日志末尾，8170 行附近）
CRT veh=27 order=15 dir=3 origin=59,18 td=10 found=0
RESV-WATCH SET-STN tile=60,16 track=255 phase=choose-track actor=27 head=27
DEPOT-ARR veh=27 spd=0 real=20(15) curType=15 tx=60 ty=31 destTx=-1 destTy=-1
PFD-REJ-GROUP tile=58,19 t=27 ordType=15 wc=0 rej=768
NOCAB-SET this=27 db=1 last=23 ...
```

- 27 刚挂上 23 节车底（`type=15` = OT_DECOUPLE 已是当前命令），`real=20` 落在 `ORD idx=20 type=15`(DECOUPLE) ⇒ **一挂上就要解挂**，且随后 `NOCAB-SET this=27`（链头被判无驾驶室）、`PFD-REJ-GROUP … t=27 ordType=15 rej=768`（解挂寻路被拒）、`CRT found=0` 原地不动。
- 索引 5 → 20 的跳变来源（继承 owner=6 的索引？）待确认，见 §五 R-3。
- 另有一条独立噪点：末尾 `CPL-S0-CHK veh=48 tile=58,67` + `COUPLE-FAIL loco=48 order=16` 反复刷屏，并伴随 `7732:[R3R] CPL-PAIR-STEAL act=48 tgt=23 from=27 myEnter=9813 hisEnter=11891`（48 抢走原属 27 的配对 23；早期还有 `2167: act=48 tgt=6 from=27`、`6790: act=48 tgt=0 from=29`）。⇒ **同一个 48 号车既是「号/名被借」的受害者，又是「抢别的链配对目标」的施害者**。

## 四、初步结论（三条同一根因）

1. 借用层（`CTRL-PARK` / `DEPOT-XFER-ID` / `NAME-XFER` / `ORD-XFER`）的**归还路径不完整**：解耦侧只有 `v` 半边、且部分事件（48、29、#8）根本没走到归还；`u` 半边的名/号只在特定分支归还。
2. 归还不完整 ⇒ 活字段带着借来的号/名**写进存档**（48 的 `un=6` 在第 59 行就存在）⇒ 界面出现「找不到的车」与重号。
3. 段 ID 采用全局递增 + 段属性只挂第一个段头 ⇒ 界面「段编号」与链内段数不一致，观感为「段编号借用」。

## 五、未确认项（下一轮取证）

- R-1 段 ID 分配器（`R3RSyncChainSegmentIds` 及分配处）：是否只增不减、是否跨链重复发放、UI 段号取的是 ID 还是链内序号。
- R-2 解耦归还分支：为何 48/29 没走 `NAME-RESTORE`；`R3RRestoreTrainName` / 号侧 `R3RRestoreUnitNumber` 的守卫条件。
- R-3 `ORD-XFER keep … cur_real=5` → `ORD-AFTER-COUPLE real=20` 的跳变来源（继承 owner 索引 vs 排除已执行 WAIT_COUPLE）。
- R-4 `PFD-REJ-GROUP … ordType=15 rej=768`：解挂寻路被分组闸门拒绝是否与借用后的分组掩码（`nseg=3 mask=0`）有关。
- R-5 48 号车 `CPL-PAIR-STEAL` 抢 23/6/0 的配对：配对表（`CPL-PAIR-*`）在借用/解耦后是否清理。

## 六、复测判据（修完后）

1. 48 号车（及任何链头）在解耦后必须出现成对的 `NAME-RESTORE`；界面上不再出现「借来的号/名」。
2. 存档往返一次后，`R3RDUMP` 中不得再有「活车号等于别链控制段号」的车辆。
3. 任一 `SEGID-SYNC nseg=N` 之后，`CTRL-PARK seg=` 必须 ≤ N；`GEO settle-*` 的 segid 集合必须恰为 1..N。
4. `CHAIN-ATTRS` 每个段头都要带自己的 pow/wt/len（链头段不得恒 0）。
5. 耦合后 `ORD-AFTER-COUPLE real=` 不得落在未执行的 DECOUPLE 上（或必须能正常触发并完成解挂）。
6. 不再出现「无主」的 `CPL-PAIR-STEAL`（目标应为自己的配对或被明确释放后才可抢）。

## 十一、第 181 轮续（2026-10-01）：R-1..R-5 定性 + 三处修复

### 11.1 R-1（KI-276 → 非缺陷）
`SEGID-SYNC ... nseg=` / `CTRL-PARK ... seg=` 打的是全局段 ID（`r3r_segment_id`，只增不减），界面「第 k 段」是链内序号（`R3RGetSegmentPosition()` 现算，子行标签 `STR_VEHICLE_LIST_SEGMENT_SUBROW` 收的是序号）。「nseg=2 却 seg=3」= ID 3 是链内第 2 段，不矛盾。段属性按段写行的行为不变。

### 11.2 R-2（KI-277 已修）名/号借用不对称
- 名侧：`Couple()` 无条件 `u->name.clear()`；链头本来无名时 `v->name_backup` 空 ⇒ `DecoupleTrain()` 的 `if (!v->name_backup.empty())` 跳过 ⇒ 永久顶替（现场 `NAME-XFER head=48 u=6 head_own=0 got_u=1`，之后 `LOCO-AFTER-DECOUPLE veh=48` 无 `NAME-RESTORE`）。
- 号侧：二次耦合（`ORD-XFER keep`）先 `ReleaseID(v->unitnumber_backup)`（链头自己的号被销毁）再 `v->unitnumber_backup = v->unitnumber`（借来的号变成它的身份），段行随之写入借来的号 ⇒ 48 显示成 6。
- 修法：名侧不再清空 `u->name`，归还端按 `v->name == u->name` 认出「本来无名」的借用并清回（`side=head-cleared`）；号侧新增 `was_borrowing` 采样（在 `r3r_orders_borrowed = true` 之前），借用中保留停放副本、只在 `unitnumber_backup == 0` 时停放。
- 旧档不追溯（48 身上的 6 已写进存档，原始值不存在于任何字段）。

### 11.3 R-3（KI-278 已修）耦合落点停在 DECOUPLE
合并后 `real=20` 落在一条 DECOUPLE 上；DECOUPLE 不可单独执行（只能由前一条 GOTO 的终点触发），`ProcessOrders` 永不收尾 ⇒ 停死 + NOCAB + `PFD-REJ-GROUP rej=768`。解挂/车库路径都有 `R3RSkipUnfireableDecoupleOrder()`，耦合路径漏 → 已补，探针 `COUPLE-SKIP-DECOUPLE`。

### 11.4 R-4 排除 / R-5 归并
`waiter_real_index` 采样于 `TryTrainCouple()` 之前（等待方此时还是链头且自持 orders），`ORD-AFTER-COUPLE ... u_has_orders=0` 是交接已完成后的正常形态，不能当「索引陈旧」的证据。48 的可见身份端到端链路（名 + 号 + 段行镜像）已归入 KI-277。

### 11.5 构建自证
`_tmp_inc_build.cmd`（复用，未新建）：首次 `train_cmd.cpp(7651): error C2679`（TinyString 无 `operator==`，改 `std::string_view` 比较）；修后 `GUARD: incremental is safe`、`[3/3] Linking`、`EXIT_CODE=0`、obj 03:50:56 > cpp 03:50:32、`build\openttd.exe` 03:52:36（51 561 472 B）、read_lints 0；exe 命中 `NAME-RESTORE veh=%d side=head-cleared`、`COUPLE-SKIP-DECOUPLE head=%d stepped=%u real=%d type=%d`。

### 11.6 未确认项 / 未做
1. 旧档里已被顶替的号/名无法自动追回（原始值不在任何字段）。
2. 段行（R3SG）中已写入的借来号码需等到下一个提交点（settle / `CTRL-PARK` / `DEPOT-XFER-ID`）才按新逻辑重写，旧档首帧仍可能短暂显示旧值。
3. 第 180 轮遗留 KI-274 的 A（订单闸门只对主动命令生效）/ B（车库拖动边自动打散）仍未实现。
4. 反例回归（链头本来有名字 / `was_borrowing=0` 的过期副本释放）只做了代码走查，需实机复测确认号池不泄漏。
