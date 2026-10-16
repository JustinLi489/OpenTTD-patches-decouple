# R3R 临时分析报告 —— 1112,129 红信号 / 1112,130 预留归属走查（第 243 轮）

采样：`build\R3R_debug.log` 5231 行 / 产生者 `build\openttd.exe` @2026-10-11 02:21:41（51 772 416 B）
= 含 **KI-349 REDSIG 探针** 的构建 ⇒ 采样对 KI-349 有效。日志时间 2:28:59。

---

## 一、已确证事实（逐字日志锚点）

### 1. veh=23 停在 1112,129 的红信号前，状态恒定到最后一行
```
3645 REDSIG veh=23 old=1112,128 new=1112,129 dir=1 chosen=0x2 i=1 prog=0 spd=15 wait=0 stuck=0
3646 REDSIG   tdbits=0x2 reach=0x16 sigs=0x1717 red=0x16 bits=0x2
3647 REDSIG   new present=0x8 states=0x7 resv=0x2 veh=0
3648 REDSIG     track=3 type=0 oneway=1
3649 REDSIG   beyond=1112,130 valid=1 plain=1 resv=0x2 veh=0
3650 REDSIG     beyond track=1 owner=22 near=22
...
5129 REDSIG veh=23 old=1112,128 new=1112,129 dir=1 chosen=0x2 i=1 prog=0 spd=2 wait=0 stuck=0
```
- 13+ 次 REDSIG（3645…5129）字段**逐字相同**：`resv=0x2`（新格）/`resv=0x2`（beyond）从未变化
  ⇒ 红信号是**稳定阻塞**，不是闪烁；探针走的是 R3RDbgEdge（(new_resv,beyond_resv) 未变则被抑制）。
- 只有第一行 `spd=15`，之后恒 `spd=2` ⇒ veh=23 在 3645 之后被刹停并**再未通过**。

### 2. 解码（对照 src\train_cmd.cpp:16076-16132 与 src\rail_map.h）
- `chosen_track=0x2` = `TRACK_BIT_Y`；`red_signals & chosen_track` 命中 ⇒ 判定"前方红灯"。
- `new present=0x8` → 1112,129 全格**只有一个信号在 track=3（TRACK_LOWER）**；
  `states=0x7` → 只有 track3 那位是 0 = **红**；`type=0`=Block、`oneway=1`=单向。
- `new resv=0x2` → 1112,129 的 `TRACK_BIT_Y` 已被预留，且 `veh=0`（格上**无车**）。
- `beyond=1112,130 plain=1 resv=0x2 veh=0` → 保护段下一格同样：Y 轨被预留、**格上无车**。
- `beyond track=1 owner=22 near=22` → **"走到路径尽头的车"与"路径上最近的车"两种走查都返回 veh=22**。

### 3. veh=22 的身份与位置
- `SEGLOAD-PARK veh=22 unit=5 ... n="纪州路快速01" gid=1 prio=1`、`ORD-IDX-WATCH ... veh=22 ... n=14 ... front=1 parked=0`
  ⇒ veh=22 = 14 条订单的**段头/链头**，开局在库 964,133。
- `RESERVECONSIST veh=22 tile=1085,136 ok=0 resNow=1`（每 tick 重复）
  ⇒ veh=22 停在 1085,136（其consist 8 节铺在 1082,136…1085,136），**处 OT_WAIT_COUPLE(17) 等待挂接**。
- veh=23 = 主动方：7 订单、`real=6` 是 `OT_GOTO_COUPLE(16)`、`CPL-PAIR act=23 tgt=22`、`COUPLE-FAIL loco=23 order=16 tx=1112 ty=125`。

### 4. 孤儿预留走廊（与本节独立，但同日志并存）
```
RESV-AUDIT-SUMMARY tick reservedTiles=58 owned=38 strayTiles=20 reaped=0 pendingStray=20
170-189 RESV-AUDIT-STRAY tick tile=1093,87 track=4 bits=0x10 age=0
        ... 逐格 ... tile=1112,106 track=4 bits=0x10 age=0
```
- 恰 20 格，1093,87→1112,106 对角线，**每格 track=4(TRACK_LEFT) bits=0x10**，age=0，`reaped=0`。
- 这正是 **KI-346** 定位的签名（`rp()` 双槽格订错轨 ⇒ 无主位）。第 240 轮已修**产生源**，
  但 KI-346 明确写「存量孤儿位不追溯」⇒ 本日志的 20 格属**旧档带入**，与本轮红信号**无坐标交集**
  （走廊在 y=87..106，红信号在 y=129/130，且轨位 LEFT vs Y 不同）。

---

## 二、疑点（一发现即登记）

- **D-1（关键）走查方向偏置 ⇒ `owner=22 near=22` 不能作证据。**
  `GetTrainForReservation`（pbs.cpp:1642）与 `GetR3RReservationOwnerNearby`（pbs.cpp:1712）都是
  `for (i=0;i<2;++i, trackdir=ReverseTrackdir(trackdir))`，**第一轮**用的是
  `TrackToTrackdir(track)`（对 TRACK_Y = `TRACKDIR_Y_SE` = **南，即 veh=23 的行进方向**）。
  veh=23 自己的行进预留也朝南、其终点紧贴目标车底 veh=22 ⇒ 从 1112,130 向南走到尽头**必然**落在 veh=22。
  故该行**无法区分**：
  - (a) 1112,130 是 veh=22 的陈旧/幽灵预留（我们想证的）；
  - (b) 1112,130 本来就是 **veh=23 自己的**行进预留，只是走查按"尽头车"误判成 veh=22。
  这与 KI-210 已记录的同族误判面（live booking 接在 stale 后成一条连续位）**同构**。
  **期望值**：应能判归"谁真的订了这格"；**实测**：两种走查同向同答 ⇒ 判不了。

- **D-2 双向走查的"第二方向"从未执行。** 两个函数都在 i=0 成功即 return；北向（`TRACKDIR_Y_NW`）
  的查询结果**日志里不存在**，因此无法排除"北侧还有一段属 veh=23"。

- **D-3 审计只跑了 1 次（4800+ tick 只有 1 条 SUMMARY）**，`reaped=0/pendingStray=20`，
  没机会触发 age≥3 的回收，因此**不能**用"没有 RESV-REAP"反推"红信号不是幽灵"。

---

## 三、猜测根因（待验证，勿当结论）

- **G-1**：1112,129/130 的 `TRACK_BIT_Y` 预留属 **veh=23 自己**（其 GOTO_COUPLE 行进路径的尾部，
  终点贴合 veh=22），红信号之所以红是因为**其保护段（1112,130 起）仍带预留位** —— 即
  信号状态机把"自己预订的位"也当成占用 ⇒ veh=23 被**自己的预约**挡死。
  支撑：`beyond veh=0`（无车）、状态恒定不变（自己不动 ⇒ 位也不释放）、
  两种走查同向同答（走查偏差）。
  待验证：北向走查结果 / 1112,131 是否 reserved / veh=23 是否曾成功预留到 1085,136。

- **G-2**：1112,130 的位是 veh=22 的**陈旧预留**（它 parked 前订的出站路线残留），
  而 veh=22 的清扫（KI-206/KI-208 waiter 清扫）没覆盖到这一格。
  支撑：veh=22 每 tick 打 `RESERVECONSIST ... resNow=1`（它在反复重试订路）。
  待验证：同 G-1 的取数（北向）+ 查 `RESV-GHOST`/`RESV-WATCH` 是否提到 1112,130。

> G-1 与 G-2 的**判别式完全相同**：需要"1112,129/130 的位到底是谁订的"这**一个**事实。

---

## 四、下一步唯一取数（一次复跑即可判别）

在现有 REDSIG 块（train_cmd.cpp:16121-16131）里，对 beyond 的每个预留 track **打印两个方向各自的结论**，
而不是只打合并后的 `owner=/near=`：即增加 `beyond track=%d dir=SE owner=%d` 与 `dir=NW owner=%d` 两行
（复用 `R3RFollowReservationToNearbyTrain(beyond, TRACKDIR_Y_SE/NW, rts)`，只读，零行为变更）。
判读：
- 若 **NW 方向返回 veh=23** ⇒ 1112,130 是 veh=23 自己的预约尾部 ⇒ **G-1 成立**（信号逻辑/预留语义问题）。
- 若 **两个方向都只返回 22/或 NW 返回 none** ⇒ 1112,130 确为第三方（veh=22）旧位 ⇒ **G-2 成立**（清扫漏格）。

---

## 五、复测判据

1. 新探针应给出 1112,130 在 `TRACKDIR_Y_SE` 与 `TRACKDIR_Y_NW` 两个方向的独立归属。
2. 判 G-1 成立时：veh=23 应能通过该信号进站/接近目标，`COUPLE-FAIL` 循环消失。
3. 判 G-2 成立时：修复后该位被清，`RESERVECONSIST veh=22 ... resNow=1` 对应的幽灵位消失，
   且 `RESV-AUDIT-SUMMARY` 的 strayTiles 不再包含 x=1112 的 y=129/130。
4. 负判据：正常 PBS 区段的红信号语义与 `RESV-AUDIT-SUMMARY owned` 计数不得回退。
5. 与 KI-346 相互独立：20 格 LEFT 走廊（1093,87..1112,106）属旧档存量，**不计入**本轮判据。

---

## 六、未确认

- U-1：1112,131 及以南是否也带 TRACK_Y 预留（决定预约长度 ⇒ 决定是不是"一条到车底的完整路线"）。
- U-2：veh=23 在 2812 那次 `TRP ... ok=1 res=1` 的预约**终点格**是否就是 1112,130 一带（与 KI-348 的 U-3 同）。
- U-3：`states=0x7` 的其余三位（track 0/1/2）在本格未被 `present` 标记却为 1，是否只是复位值。

---

## 七、§四 取数已落地（第 243 轮续五，2026-10-11）

玩家指示「好，加探针吧」⇒ 已把 §四 的方向分离探针写进源码并编译通过（**只读，零行为变更**，未碰任何 `src\*.h`）。

### 7.1 代码落点

| 文件 | 改动 |
|---|---|
| `src\pbs.cpp` | `GetR3RReservationOwnerNearby()` 之后新增 `Train *R3RReservationOwnerInDirection(TileIndex tile, Trackdir trackdir)`：单方向版走查，先自证 `IsValidTile && IsPlainRailTile && HasBit(GetReservedTrackbits(tile), TrackdirToTrack(trackdir))`，再以 `GetRailTypeInfo(GetTileRailTypeByTrack(...))->indirect_compatible_railtypes` 调既有的 `static R3RFollowReservationToNearbyTrain(tile, trackdir, rts)` |
| `src\train_cmd.cpp` | ① 文件头前向声明 `Train *R3RReservationOwnerInDirection(TileIndex, Trackdir);`；② REDSIG 块在既有 `REDSIG     beyond track=%d owner=%d near=%d` 行**之后**追加两行 `REDSIG     beyond track=%d dir=%d hit=%d`（`dir` = `TrackToTrackdir(t)` / `ReverseTrackdir(...)`） |

> 说明（与 §四原文的偏差）：§四 写"复用 `R3RFollowReservationToNearbyTrain(beyond, TRACKDIR_Y_SE/NW, rts)`"。实现上**没有**把该 static 函数去 static，而是新增一个**单方向薄包装** `R3RReservationOwnerInDirection()`，以便把 railtype 推导留在 `pbs.cpp` 内、`train_cmd.cpp` 只传 `(tile, trackdir)`。语义完全等价，`R3RFollowReservationToNearbyTrain` 本体未动。

### 7.2 输出格式与解码

```
REDSIG   beyond=1112,130 valid=1 plain=1 resv=0x2 veh=0
REDSIG     beyond track=1 owner=22 near=22        <- 既有（合并/第一方向，保留）
REDSIG     beyond track=1 dir=1 hit=<...>         <- 新：dir=1 = TRACKDIR_Y_SE（南 = veh=23 行进方向）
REDSIG     beyond track=1 dir=9 hit=<...>         <- 新：dir=9 = TRACKDIR_Y_NW（北）
```

- `dir` 即 `Trackdir` 枚举值（`src\track_type.h`：`TRACKDIR_Y_SE=1`、`TRACKDIR_Y_NW=9`；`TrackToTrackdir(t)==(Trackdir)t`）。
- `hit=-1` 表示该方向走查**未遇到任何列车**。
- 既有 `owner=/near=` 行**保留**（便于与上一版日志逐字对比、不破坏已写好的解码脚本）。

### 7.3 判读（沿用 §四）

- 北向 `dir=9 hit=23` ⇒ `1112,130` 是 **veh=23 自身预约的尾部** ⇒ 倾向 **G-1**（信号状态机把"自己预订的位"当占用）。
- 北向 `dir=9 hit=-1`、或两方向都只给 22 ⇒ `1112,130` 是**第三方（veh=22）旧位** ⇒ **G-2 成立**（清扫漏格）。

### 7.4 构建自证（2026-10-11）

- 复用既有 `_tmp_inc_build.cmd`（**未新建 .cmd**）；`build\R3R_incbuild.guard.log = GUARD: incremental is safe`（未碰头文件 ⇒ 增量合法）。
- 日志 `[2/4] pbs.cpp.obj → [3/4] train_cmd.cpp.obj → [4/4] Linking CXX executable openttd.exe`；`build\R3R_incbuild.done = EXIT_CODE=0`；`error C*/fatal error/FAILED:/build stopped` 计数 **0**。
- 共两次增量（第二次仅把代码注释里的 KI 标签由 `KI-349` 更正为 `KI-350`，行为不变），**以下为最终产物**：
  时间戳链 `src\pbs.cpp`@03:15:26 → `pbs.cpp.obj`@03:15:47；`src\train_cmd.cpp`@03:15:28 → `train_cmd.cpp.obj`@03:15:48；`build\openttd.exe`@**03:17:20**（51 772 416 B，晚于全部源码）。
- `read_lints`（两个 .cpp）**0 条**；exe 字面量自证：`REDSIG     beyond track=%d dir=%d hit=%d`、`REDSIG     beyond track=%d owner=%d near=%d`、`REDSIG veh=` **均命中**。

### 7.5 复测要求（对应 §五）

用本 exe 复跑同场景，取 `beyond track=1 dir=1/9` 两行即可判 G-1 / G-2；其余判据（§五 2/3/4/5）不变。

**结果见 §八（第 243 轮续六）**：`dir=1 hit=22` / `dir=9 hit=36`；36 = 自称车**同链非头车**（探针 `hit` 语义缺陷，见 D-11）⇒ 按本支**本意**倾向 G-1，但接缝不可判，**未坐实**。

---

## 八、§四 取数结果（第 243 轮续六，2026-10-11 03:30 新日志）

**采样**：`build\R3R_debug.log` **4775 行 / 432 847 B** @2026-10-11 03:30:35，产生者 `build\openttd.exe` @**03:17:20**（51 772 416 B = §七 的方向分离探针构建）⇒ 对 §四 取数有效。同源另存 `R3R_debug_round243.log`（03:30:43；**不是**旧档，勿与 §一 的 5231 行采样混淆）。

### 8.1 原始数据（12 组 REDSIG，字段逐字相同；首见 3700，末见 4623）
```
3700 REDSIG veh=23 old=1112,128 new=1112,129 dir=1 chosen=0x2 i=1 prog=0 spd=15 wait=0 stuck=0
3701 REDSIG   tdbits=0x2 reach=0x16 sigs=0x1717 red=0x16 bits=0x2
3702 REDSIG   new present=0x8 states=0x7 resv=0x2 veh=0
3703 REDSIG     track=3 type=0 oneway=1
3704 REDSIG   beyond=1112,130 valid=1 plain=1 resv=0x2 veh=0
3705 REDSIG     beyond track=1 owner=22 near=22
3706 REDSIG     beyond track=1 dir=1 hit=22          <- 新：SE = 行进方向
3707 REDSIG     beyond track=1 dir=9 hit=36          <- 新：NW = 来向
```
- 12 组**全部** = `dir=1 hit=22` + `dir=9 hit=36`（`spd` 仅首行 15、其余恒 2）；既有 `owner=/near=22` 行保留。
- ⇒ §四 期望的 `dir=9 hit=23` **没出现**，北向落点是**新数字 36**。

### 8.2 `hit=36` 的身份（已坐实：自称车同链非头车）
```
 94 R3RDUMP idx=25 et=18 ... head=23 tail=25 | prev=36 next=-1
105 R3RDUMP idx=36 et=28 ... head=23 tail=25 | prev=30 next=25
151 R3RDUMP-CHAIN head=23 ... n=8 stopped=1 inDepot=1
294 CHAIN-ATTRS head=23 n=16 FE=2 SEG=2 pow=26400 wt=1120 len=128 spd=112
```
- `idx=36` 的 `head=23 tail=25` ⇒ **veh=36 是 veh=23 自己那条链的第 7 节**（链序 23→26→27→28→29→30→36→25），**不是第三方列车**。
- `GetFirstVehicleOnTile()`（`src\vehicle.cpp`：瓦片→车辆**哈希链**，头插；头文件注释明示顺序**非确定**）返回的是"该格任意一节" ⇒ `hit` 天然可能报出**同链非头车**。**「hit=23」这个期望值本身写错了**（D-11）。
- 几何自证（与 `hit=36` 逐字吻合）：
```
3716 COUPLE-WAIT-OFFDEST veh=23 tile=1112,125 mf=1112,128 real=6 ... destTile=287807 ...
```
  `len=128` = 16 节 × 8 单位 = 8 格；本时刻链已解耦为单段 8 节 = **64 单位 = 4 格 = 1112,125..1112,128**（每格 2 节）⇒
  `1112,125`: n=0,1 (23,26)｜`1112,126`: n=2,3 (27,28)｜`1112,127`: n=4,5 (29,30)｜**`1112,128`: n=6,7 (36,25)**。
  `tile=1112,125 mf=1112,128` ⇒ **链头(23) 在最北的 1112,125，行进前沿在 1112,128**（**尾端先行**）。
  ⇒ 北向走查 1112,130 → 1112,129（`resv=0x2`、无车）→ **1112,128（有车：36/25）** ⇒ `hit=36` **可由几何完全推导**。

### 8.3 结论（能说什么 / 不能说什么）
**能说**：
1. 北向落点 = **自称车自身链**（非第三方）⇒ 按 §7.3 第一支的**本意**（"北向落在自称车上"），结果**倾向 G-1**。
2. 南向落点 = **veh=22**（与旧 `owner=/near=22` 一致）⇒ 从 1112,130 向南有**一条连续预留**直通配对目标车底（≈27+ 格，中途无车）。
3. ⇒ 1112,130 **两侧都连续有预留**：北接自称车占位（1112,128–129）、南接 veh=22 ⇒ 该格落在两道预约的**接缝**上。

**不能说**（证据强度不足）：
- 北向"连续"是**必然结果**：自称车就占在 1112,128（离查询格只隔 1 格信号格），任何"接缝落在 129/130 之间"的布局都给同样输出 ⇒ **无法区分** (a) 1112,130 属 veh=23（G-1）与 (b) 1112,130 属 veh=22、1112,129 才属 veh=23（G-2 / 新 H-7）。
- ⇒ §五 判据 1 只算"跑出来了"；**判据 2/3 仍无据**；**G-1/G-2 未收口**。

### 8.4 新增疑点
- **D-11（探针语义缺陷）**：`hit` = 瓦片哈希链首节 ≠ 链头。判读必须 `hit->First()->index`（或 `hit == consist`），否则"同链非头车"会被误读为第三方（本例 36 极易被误读成"冒出第三列车"）。
- **D-12（几何，供全局复用）**：`v->tile` = **链头那节所在格**，`GetMovingFront()->tile` = **行进前沿格**；本场景相差 3 格（125 vs 128），列车**尾端先行**（链头在北）。备忘里一切"车头/车尾"判读须按此校准（§一 的"头靠 128"= 行进前沿，链头在 125）。
- **D-13（接缝不可判）**：信号机格 1112,129 **自身也带 `resv=0x2`**。若 1112,130 属 veh=22，则其预约必止于 1112,130（它在 27 格外、未越信号）⇒ 1112,129 必属 veh=23；**两种假设都自洽**，接缝落在 129/130 还是 130/131 之间，本探针**读不出**。
- **D-14（审计继承偏置，新）**：读档审计的 `owned/stray` 用 `GetTrainForReservation(t, track)`（`src\train_cmd.cpp:14936`）= KI-350 的**方向偏置**函数 ⇒ `owned` 会把"接缝另一侧的车"当成 owner ⇒ **`strayTiles` 必然少报**（1112,130 会被判 owned，即便它真是幽灵位）。本条把 KI-345/346 的清扫面与 KI-350 串起来。

### 8.5 新增候选根因
- **H-7（活位 + 互等死锁；与 G-1/G-2 并列）**：1112,130 的位可能是 **veh=22 的 live 预约**（它在 1085,136 持有一条朝向自称车的连续预约，北端止于信号 1112,129 之前一格）。此时**信号红完全合法**，真凶不是"预留泄漏"，而是**互相等待的挂接死锁**：veh=22 在 `OT_WAIT_COUPLE` 中持有通往 veh=23 的区段，veh=23 又在等这个区段。
  - 支撑：南向连续预留直抵 veh=22 且**中途无车**；`RESERVECONSIST veh=22 ... ok=0 resNow=1` 每 tick 重试（反复订自己 consist 却失败）；3700 起 1000+ tick 两车均无进展。
  - 待取数：若该段属 veh=22，它自己为何不动（`ok=0` 的原因）。

### 8.6 决定性试验（下一步）
1. **玩家侧（零改码，最快、可判死）**：把 **veh=22 挪走**（命令前进 / 或删除）后看 1112,129 是否转绿。
   - 转绿 ⇒ 该位属 veh=22（G-2/H-7 = **合法占用**）⇒ 收口到"挂接互等死锁"。
   - 仍红 ⇒ 该位属 veh=23 自己 ⇒ **G-1 坐实**（自己的预约挡死自己）。
2. **既有命令 `r3r_resv_purge`**（第 238 轮确认可用）：被清且转绿 ⇒ 判为无主/可回收 ⇒ G-2（注意 D-14：审计 owner 判定本身带偏置，需看"清掉"还是"拒清"）。
3. **探针侧（需改码，唯一直接取证）**：在 `TryReserveRailTrack` / `UnreserveRailTrack` 的**写入路径**上对 `(1112,130, TRACK_Y)` 加边沿日志（只读）——谁写、谁清，一目了然，彻底绕开"最近车走查"的偏置。

### 8.7 本轮边界
- **零源码改动、零构建**：只消费 §七 那次构建（03:17:20 exe）的产出。
- 未做：未改 `GetTrainForReservation` / `GetR3RReservationOwnerNearby` 的生产语义；未碰 KI-346 的 20 格存量走廊（`RESV-AUDIT-SUMMARY reservedTiles=58 owned=38 strayTiles=20 reaped=0`，与本格坐标/轨位均不同）。

## 九、断点恢复与关键落点（续七，2026-10-11）

**断点**：§八（续六）落盘后，继续推理「§8.6 第 3 条：写入路径探针怎么加」时上下文被截断。本轮**先落盘**（本节 + KI 续七）后动手，防止中间结论再次丢失。

**关键落点（推翻 §8.6 第 3 条的前提）**：预留**写入路径**的只读探针**早已存在**，且关注格清单是**热加载**的 ⇒ 该条**不需改码、不需构建**：

| 项 | 事实 |
|---|---|
| 基建 | `src\r3r_resv_probes.h`（第 152 轮入库；被 `pbs.h` / `rail_map.h` / `station_map.h` / `road_map.h` / `bridge_map.h` / `tunnel_map.h` include） |
| 覆盖面 | 六个预留位 setter（`SetTrackReservation` / `ClrTrackReservation` / `SetRailStationReservation` / `SetRailStationPlatformReservation` / `SetTunnelReservation` / `SetBridgeReservation`） |
| 触发条件 | `R3R_PROBES`（编译期，`build\` 内部树开）+ `R3RDbgOn()`（`R3R_debug.log` 有输出即开）+ **写入值发生变化**（同值重写不打印 ⇒ 不刷屏） |
| 清单 | `r3r_resv_watch.txt`：一行一对 `x y`，`#` 注释；**相对进程 CWD、每会话首次预留写入时解析一次** ⇒ **必须先放文件再启动，改完必须重启** |
| 行格式 | `RESV-WATCH <kind> tile=%d,%d track=%u phase=%s actor=%d head=%d`；kind ∈ `SET-RAIL` / `CLEAR-RAIL` / `CHG-RAIL`（及站台/路点/隧道/桥同族）；`phase` 来自 `R3RResvPhaseGuard`（如 `choose-track` / `release-ghost` / `gc-stn` / `couple`） |

- **上轮 `RESV-WATCH` 命中 0 次 ≠ 探针失效**：两份既有清单（`build\r3r_resv_watch.txt` 700 B @2026-09-25、工作区根同名 276 B）**都只含 KI-208 时代的 38,10 一带 7 格**，`1112,130` 不在其中。

**本轮动作**：向两份清单各追加 `1112 128` / `1112 129` / `1112 130` / `1112 131`（ASCII 注释）。**零源码改动、零构建。**
- `1112,130` = 争点（`resv=0x2 veh=0`：预约在、无车）；`1112,129` = 信号机格（自身 `resv=0x2`，D-13 接缝嫌疑）；`1112,128` = 自称车**行进前沿**格（看 consist 自身订位/清除时机）；`1112,131` = 争点以北一格（判别「止于 130」还是「延伸更远」）。

**判读表（一次复现即可终结 G-1 / G-2 / H-7）**

| 观察到的行 | 结论 |
|---|---|
| `SET-RAIL tile=1112,130 track=1 … actor=22 head=22`，此后**无 CLEAR** | 该位 = veh=22 的 live 预约 ⇒ **H-7（合法占用 + 互等死锁）**，红信号合法，转修「谁先让」 |
| `… actor=23 head=23`，无 CLEAR | 该位 = 自称车自己订的 ⇒ **G-1 坐实**（自己的预约挡死自己），收口到 KI-346/345 清扫面 |
| 有 `CLEAR-RAIL tile=1112,130` 之后位**仍在** | **清除失效 / 存在第二写入者** ⇒ 新分支（须比对 CLEAR 前后 `track`/`actor`） |
| `1112,130` **完全无写入**、而 `1112,129` 有 SET | 预约止于信号格 ⇒ 130 的位是**无主陈旧残留**（G-2 的「泄漏」支） |

- 判读要点：`track` 是 **track 序号**（本场景南北向 = `TRACK_Y` = 1）；`actor`/`head` 是写入那一刻的值，与后续链头变化无关；`RESV-WATCH` 的格号可与 `RESV-AUDIT-STRAY` 直接对齐，但**以本探针为准**（D-14：审计的 `owned` 判定带最近车走查偏置，会少报 stray）。
- 与 §8.6 第 1/2 条的关系：三条**互补**（本条给出**写入者身份**，第 1 条给出「位属谁」的行为后果，第 2 条给出「是否可回收」）。建议**同一会话**完成：放清单 → 启动 → 复现红信号（记时间）→ 挪走 veh=22 → `r3r_resv_purge`。

**新增采样（续七，同日 09:31 会话；该会话的清单尚未生效）**
- `build\R3R_debug.log`（写入中，>5419 行，mtime 09:31:47）：`REDSIG` **101 行**、`RESV-WATCH` **0 行** ⇒ 该会话启动**早于**本轮清单修改，**印证「每会话只解析一次」**（`R3RWatchedResvTiles()` 为函数内 `static` 惰性初始化，见 `src\pbs.cpp:58-79`）。
- 末次争点读数与续六**逐字相同**：`4627 REDSIG beyond=1112,130 valid=1 plain=1 resv=0x2 veh=0` / `4628 … owner=22 near=22` / `4629 … dir=1 hit=22` / `4630 … dir=9 hit=36` ⇒ 现象**稳定可复现**，D-13（接缝不可判）未变。
- **D-15（新疑点）**：同一会话随后 `5419 REDSIG veh=23 old=1112,123 new=1112,122 dir=3 … spd=15 wait=0 stuck=0`、`5420 tdbits=0x200 reach=0x2A00 red=0x2`、`5423 beyond=1112,121 resv=0x0` ⇒ 该车**倒退离开信号**，本会话末态该格与 `1112,130` 均已**非红**。待确认：是玩家手动调向/自动折返，还是红灯死锁自行解除（不影响本格取证口径，但**取证必须在复现的当场**取）。

**诚实登记**：本轮未改任何源码、未构建（仅改两份 `r3r_resv_watch.txt` + 文档）；§8.6 第 3 条中的「需改码」一句**作废**，改为「零改码热加载」。

**下一步（玩家侧，二选一即可）**
- **A（首选）**：**退出并重启** `build\openttd.exe`（清单已就位，重启即生效）→ 复现红灯 → 记下时间 → 把 `build\R3R_debug.log` 给我。判读用 §九 判读表，一次即可终结 G-1 / G-2 / H-7。
- **B**：不重启，直接 `r3r_resv_purge`（§8.6 第 2 条）+ 挪走 veh=22（第 1 条），得到的是行为证据而非写入者身份。

## 十、决定性现场取证（玩家报告，2026-10-11）：**把 veh=23 挪开后，位仍在、信号仍红**

**玩家原话（关键）**：「我刚才把 veh23 挪开了（人工调头），但是往 veh22 的预留还是幽灵般存在，然后 1112,129 的信号灯还是红的。」

**同刻日志旁证**（`build\R3R_debug.log`，6033 行，会话仍为 09:31 那次）：
```
CPL-S0-CHK veh=23 tile=1112,123 real=6 order=16 stuck=1 fp=0 prog=0 wait=125 parked=0 stop=0 track=02 dtile=287807
```
- `dtile=287807` = **1087,140**（mapX=2048 时 1087+140×2048）⇒ veh=23 订单仍是 `OT_GOTO_COUPLE(16)`，目标是 1087,140 站台、配对目标 veh=22@1085,136。
- **veh=23 现在停在 `1112,123`（距红信号 129 有 6 格）且 `stuck=1`** ⇒ 它**不是**持位者（持位者不会 stuck）；它调头后已不在接缝旁。

**判读（对 §五 G-1/G-2/H-7 的裁决）**
1. **G-1「自称车自己订的位、原地挡死自己」→ 基本证伪**：移动 veh=23 未清除该位 ⇒ 该位**不是由 veh=23 当前位置维持的活位**。唯一残留可能只剩"veh=23 离场后未释放的遗留位"（属泄漏支，不是"自己挡自己"的活位语义）。
2. **H-7「合法占用 + 互等死锁」→ 该分支的"合法"二字也被削掉**：玩家已把配对对方挪走 ⇒ **互等条件不复存在**，位却还在 ⇒ 它**不是合法的互等占位**，而是**无人回收的陈旧预留**。
3. **G-2（位属 veh=22）+ 新 H-8 成为最强假设**：veh=22（链）从南方一路连续预约到 `1112,130`（§八：南向直抵 veh=22、**中途无车**、27+ 格）；当它的配对目标 veh=23 离场后，这条走廊**既无重订也无释放** ⇒ `1112,129` 恒红、走廊恒显示为黑色预留（玩家所见"幽灵般存在"）。

**新增疑点**：
- **D-16**：`RESV-NOBOOK` 全日志**仅 1 行**（KI-208 守门员生效记录）。须核对这 1 行是否是 **veh=22**：若是，说明 veh=22 停稳在站台且**已被禁止重订新路线** ⇒ 那么 `1112,130` 的位就是**它被禁订之前留下的旧位**（不重订、不释放 = 纯泄漏），与 H-8 互为因果。
- **D-17（探针能力边界，重要）**：`RESV-WATCH` 只在**写入值发生变化**时打印 ⇒ 一个"本该被清却一直没人清"的陈旧位**永远不会自己出现在日志里**；要用本探针抓它，必须**人为制造一次写入**（重订或清除）。⇒ 「用关注格清单等它出数」这个思路对**存量泄漏无效**，只对**新发生的订/清**有效。本会话 `RESV-WATCH` 仍 0 行（会话早于清单改动，未重启），故写入者身份仍未直接取到。

**下一步（我建议的顺序，均为零改码）**
1. **把 veh=22 也挪走（或直接卖掉）**：由 §8.6 第 1 条的对称判据——**两位车主都不在场后位仍在 ⇒ 确定为无主陈旧泄漏**（G-2 的"泄漏"支收口）；若移除 veh=22 的**同一刻**位就被清掉，则反转结论为"由 veh=22 在维护它"。
2. 再执行 **`r3r_resv_purge`**：能清且 `1112,129` 转绿 ⇒ 判定为无主/可回收（G-2 + KI-346 家族）；**拒清** ⇒ 仍有 owner（注意 D-14：owner 判定自带"最近车"偏置，可能把接缝另一侧的车当成 owner）。
3. （可选，为将来的复现取证）**重启一次**让 §九 的清单生效——此后任何**新发生的**订位/清除都会带 `phase`/`actor`；但见 D-17，别指望它报出存量位。

## 十一、第二轮现场取证（2026-10-11 09:53）：**位属 veh=22（引擎自证）；但"挪走 veh22"这一测没执行到**

**玩家原话**：「我把 veh22 挪走了，采用的是人为跳过命令的方式，但是幽灵预留依旧存在，它一直延伸到 veh22 曾经停放的站台前，格子（1090，136）。」

**日志** `build\R3R_debug.log`（6352 行，mtime **09:53:17**，同一会话、仍在刷新）：

**(1) REDSIG 逐轨归属（第 243 轮探针，KI-350 口径）**
```
4221 REDSIG   beyond=1112,130 valid=1 plain=1 resv=0x2 veh=0
4264 REDSIG     beyond track=1 owner=22 near=22
4265 REDSIG     beyond track=1 dir=1 hit=22
4266 REDSIG     beyond track=1 dir=9 hit=36
     （此 4 行成组重复：4264 / 4304 / 4347 / 4389 / 4628 …）
```
- `resv=0x2` ⇒ 1112,130 是普通轨道且 TRACK_Y(bit1) 被预留；`veh=0` 只表示"该格上没车停着"（探针语义见 train_cmd.cpp:16106-16121，不是 owner）。
- **`owner=22 near=22`：引擎自己的 owner 查询沿预留链一路走到 veh=22** ⇒ 这条走廊的锚就在 veh22 身上。
- `dir=1` 那个方向 `hit=22`（即指向走廊南端的那一条）；另一方向 `dir=9` `hit=36`（另一列车）。

**(2) veh22 其实没离开那几格**
```
6097 RESV-GC veh=22 tile=1085,136 track=stn kind=STATION why=holder-gone-skip
6098 RESV-GC veh=22 tile=1085,136 ...（1084,136 / 1083,136 / 1082,136 成对重复）
```
⇒ veh22 的车底仍压在 **1082..1085,136**（就是它原来的站台），而走廊南端 1090,136 就在它鼻尖前几格。「跳过命令」只换了订单，**没让它离开**。

**判读（对 §十 的修订）**
1. §十 的结论 1、2 不变：veh=23 不是持位者，互等条件也已不存在。
2. **G-2 升级为事实**（不再只是"最强假设"）：`owner=22 near=22` 是引擎自证——1112,130 的位连着 veh22 那条走廊，南端直到 veh22 鼻尖前的 1090,136（站台前最后一格普通轨道）。
3. **但"挪走 veh=22"这一测并没有真正执行**：玩家主观已挪走，日志显示它仍在 1082..1085,136 ⇒ **本次读数不能用来证明"无主/孤儿"**，那一问必须重做。
4. **新增 D-18（疑点）**：玩家口径"挪走"与日志"仍在 1082..1085,136"矛盾——最可能是"跳过订单 ≠ 车离开"，也可能玩家动的是另一节/另一列；需玩家确认 veh=22 这一列现在到底在哪。
5. **新增 H-9（猜测根因，待验证）**：**停驻/订单被跳过、cur_speed==0 的列车不会回收它先前订下的走廊**——KI-348 已证"停驶分支不再进 TrainCheckIfLineEnds/TrainController"（既不重订也不释放），故 veh22 停下之后这条 20+ 格走廊被冻结、既不重订也无人释放 ⇒ 1112,129 恒红。与 KI-353（配对目标离线后走廊不回收）互为因果。
6. **旁证（新增泄漏面）**：`RESV-GC ... why=holder-gone-skip`（veh=22 的 1082..1085,136 站台整格位、veh=10 的 1084..1087,139）说明连 R3R 自己的记账 GC 走到这些格时**也只删账本、不动地图**（`-skip`）⇒ 站台整格位同样漏。
7. **RESV-AUDIT-STRAY 零命中**：走廊离 veh22 太近（nearest 小），审计按"远离所有列车 >24 格"判孤儿 ⇒ **看不见"锚在车上但已无用"的位**（D-14 的另一种表现）。

**下一步（重做干净测试，仍然零改码）**
1. **让 veh=22 真正离开那几格**：给它一条 `GOTO_DEPOT` 开进车库；**更干净的是直接卖掉这一列**（车没了，owner 查询才会返回 -1）。
2. 再看 1112,129：
   - 走廊**跟着消失** ⇒ 它是 veh22 的活预约、随车走，问题收窄为"为什么停驻时不回收"（H-9 / KI-353）；
   - 走廊**仍在且 REDSIG `owner=-1`** ⇒ **确定无主孤儿位**（KI-203 / KI-345 家族），此时 `r3r_resv_purge` 应当能清（能清即收口；拒清则要查 owner 查询为何仍命中）。
3. 顺手跑一次 `r3r_resv_purge`：**预期此刻拒清**（owner=22 非空）——这本身就是"清除器清不掉锚在车上的陈旧位"的证据，把 `RESV-PURGE` / `RESV-GC` 行抄给我即可。
（注：事后核实——工程里**可能并没有**这条控制台命令，见 §十二 待办；若无则此步作废，改用 §十二 的两个办法。）

## 十二、第三测：**veh22 入库并卖掉，位仍在 ⇒ 无主孤儿预留确立**（2026-10-11 10:08）

**玩家原话**：「我把 veh22 入库卖了，但是幽灵预留依旧存在。」

**日志** `build\R3R_debug.log` **6978 行 / mtime 10:08:06**（同一会话，仍在刷新）：
- **veh=22 的最后一组行**：`6714 SPD-DROP veh=22 tile=964,133 prev=30 order=5 real=11 stuck=0 fp=0 prog=19 wait=0 parked=0 stop=1 track=80 dtile=273348`，随后 `6715 / 6738 / 6780 / 6822 / 6864 SKIP-STOPPED veh=22 order=5 real=1 spd=0 tile=964,133 parked=0 front=1 nord=14 idx=11 tt=65535`。`track=80` = TRACK_BIT_DEPOT ⇒ 车**确实进了库（964,133）**；此后到日志末尾（114 行）**再无 veh=22** ⇒ 已售出。
- **REDSIG 逐轨归属止于 4630**（`owner=22 near=22` / `dir=1 hit=22` / `dir=9 hit=36`），之后零行。原因：该探针套着 `R3RDbgEdge` 边沿闸门（key=(veh,new_tile)、payload=(new_resv,beyond_resv)），**状态没变就不吐行** ⇒ "日志没有"只等于"读数没变"，不等于位没了。**目前缺一次"卖车之后"的 fresh 读数**。

**裁决**
1. 车已不存在（卖掉）、位仍在 ⇒ **无主孤儿预留（ownerless orphan）确立**，属 KI-203 / KI-345 / KI-346 家族；D-14「有主但已死」不再适用（现在是无主）。
2. **上一轮的两个候选同时被排除**：G-2「veh22 自己合法持有」——车都没了还"持有"，不成立；H-9「停驻车不回收旧走廊」——问题不是"停驻"，是**没人回收**。KI-353 据此改判：不是"配对目标离线后不释放"，而是**"位一旦失去持有者，就永远没有任何代码去清"**。
3. 三条清理路径为何都够不着：
   - `RESV-GC` 只清**自己记账的位**（`_r3r_consist_resv`，key=调用者 index，内存态 NOSAVE，见第 110 轮）；车一卖 key 就消失，账本里那些位再无人认领，GC 也不会去扫地图。
   - `RESV-AUDIT-STRAY` 判据是"**远离所有列车 >24 格**"（第 125 轮）；而走廊南端旁边正停着 veh=2(1090,139)、veh=11(1096,139)，veh=23 在 1112,126 ⇒ nearest 极小 ⇒ 审计看不见这条走廊。**"有人在旁边的孤儿位"是该口径的盲区。**
   - 引擎 owner 查询在车被删后应返回 nullptr，但**没有代码会因"owner 为空"去释放地图位**（唯一会的是 KI-345 的审计/reap，而上一条说明它够不着）。
   ⇒ 这正是第 240 轮备忘 **U-2**「存量孤儿位不追溯、需一次性清理」的现场复现。

**下一步（两选一）**
1. **取一次卖车后的直接读数**：把等在 1112,129 那列车**开动一格再停回**（或改道再回来），使 REDSIG 的 key/payload 改变 ⇒ 会重新吐出一组 `REDSIG beyond=1112,130 ... owner=? near=?`。若 `owner=-1 / near=-1` ⇒ 无主实锤。
2. **要能清掉**：需要一条"**全图扫 + 无主即释放**"的入口。待核实工程内是否已有可触发入口（§十三）；若没有，最小改动 = 给已有的 `R3RPurgeStrayReservations()` 挂一个控制台命令（只改 `console_cmds.cpp`，不碰 `src\*.h`，符合增量）。玩家侧临时手段仍是**把那格铁轨拆掉重铺**（预留位随轨道重设被过滤，见第 110 轮备忘）。

## 十三、核实结果：清理入口**已经存在**，且正是为"旧档带进来的孤儿位"准备的

- `src\console_cmds.cpp:482` `ConR3RPurgeReservations()` → `:4494` `IConsole::CmdRegister("r3r_resv_purge", ConR3RPurgeReservations)`；输出 `R3R: released {} orphaned track reservation bit(s).`，逐格写 `RESV-PURGE tile=x,y track=N`（站台/路点写 `track=stn kind=STATION|WAYPOINT`）。
- 判据（`src\pbs.cpp:1808-1864`，两遍：先收集后释放）：
  - 跳过**格上有车**的格；
  - 站台/路点整格位：无主（`GetTrainForWholeTileReservation()==nullptr`）⇒ 释放；
  - 普通轨道逐轨：`if (HasReservedTracks(t, TrackToTrackBits(track)) && GetTrainForReservation(t, track) != nullptr) continue;` —— **注意用的是引擎的"远端"版 owner 查询（`GetTrainForReservation`），不是 `…Nearby` 版**。
- pbs.cpp:1801-1804 的注释自陈：这条命令就是给"**从旧构建继承下来的幽灵位**"做按需清理的；周期审计则靠 `R3R_RESV_REAP_AUDITS` 次"stray"判定来顺手清。（本轮现场说明后者够不着，见 §十二。）

**⇒ 现在的预测分成两支，用一次命令即可定性：**
- 若 veh22 售出后 `GetTrainForReservation(1112,130,TRACK_Y)` 已找不到任何车 ⇒ 命令报 `released N`（N>0）且铁轨预览消失 ⇒ **孤儿=无主、清理可达**，剩下只是"自动化"（KI-355）。
- 若这条走法仍能走到别的车（§十一 读数里反向 `dir=9 hit=36` 提示这种可能）⇒ 命令报 **0**，则说明位在引擎眼里"仍有主"（被邻车的预约链接住了），此时必须换判据（KI-355 候选 ②：以 owner 为空为准、与距离解耦）。

## 十四、第四测：`r3r_resv_purge` 清掉 **32 个孤儿位**（2026-10-11）

**玩家原话**：「我输入了命令，然后它显示清理了32个孤儿预留的bit，但是，我想知道幽灵预留为什么会变孤儿，当然，自动清理也很重要」

**判定**
- `released 32`（>0）⇒ 这批位对 `GetTrainForReservation()` 全部返回 nullptr ⇒ **"孤儿=无主"实锤**。KI-353 的"位一旦失去持有者便永远无人清"、KI-355 的"审计因 >24 格判据看不见"两条同时成立；D-14「有主但已死」彻底排除。
- 同时证明清理路径**可达且安全**：两遍扫描（先收集后释放）+ 释放前重验 tile 类型与位，未误伤任何活车预约。
- 剩下的只有**自动化**：每 2048 tick 的 `R3RReservationAudit` 因为"远离所有列车 >24 格"这条判据，看不到旁边就是车队的孤儿位 ⇒ 只能靠人工命令。

**要取的数**：日志里逐行 `RESV-PURGE tile=x,y track=N`（站台/路点会是 `track=stn kind=STATION|WAYPOINT`）。用途：
- 若 32 位落成 `1090..1112 @ y=130..136` 的走廊、且每格 1~2 位、**同时含 `track=1`(TRACK_Y) 与 `track=0`(TRACK_X)** ⇒ 与第 240 轮 KI-346（双槽格把入口轨订错）同族，说明这条走廊是**修复生效前**产生、随存档带进来的存量。
- 若落在车站/路点格 ⇒ 第 125/126 轮 KI-207c 家族。
- 若沿着 veh22 的旧停车点（1082..1085,136 一带）⇒ KI-203 家族（`ReserveTrackUnderConsist` 只加不减 + 车节 tile 一变就没人走到）。

**猜测的根因（待验证）**：能产生"无主位"的家族有三个，但**共同机制只有一条：订位与释放各自独立、彼此不校验**——
1. PBS 路线预留：订是"寻路时沿计划逐格订"，释放是"沿车**实际走的**轨逐格取消"；车节 tile 一变（逻辑翻转 / 拼接 / 车库编辑 / 解挂 / 换端）或订单改向，两边就不再一一对应。
2. `ReserveTrackUnderConsist()` 只加不减、`ClearReservationUnderConsist()` 只按**调用那一刻的车节 tile** 清 ⇒ 车一挪，旧格再没人走到（第 110 轮 KI-203 原话）。
3. KI-346 的 `rp()` 是**天生孤儿**：订下的轨本来就没人会走（把入口轨写成当前轨），连"释放对不上"都轮不到。
⇒ 所以"变孤儿"不是某一次失误，而是**引擎只保证"位在活的预约链里"，不保证"位总有主"**；链一旦被切断（车改走别的轨 / 被卖 / 被翻转 / 路线被重订），位就永久留在地图上，而现存的两条清理路径分别以"距离"（审计）和"自己的内存账本"（GC）为判据 ⇒ 全都够不着。

**自动化修法（候选，待实现）**：把审计的 stray 判据与距离**解耦**——`R3RReservationAudit` 本来每 2048 tick 就全图扫描预留格，对每个位直接问引擎 `GetTrainForReservation() / GetTrainForWholeTileReservation()`，为空即记 stray（新探针 `RESV-AUDIT-NOOWNER`），沿用既有 `R3R_RESV_REAP_AUDITS` 次 + 四道校验后由 `RESV-REAP` 释放；"距离 >24"的旧判据保留并行（用于报告"孤立但仍有主"的可疑格）。这样"邻近有车的孤儿位"不再需要人工跑命令。

> **【本段前提已被 §十五 纠错】** 上面把审计说成"判据是距离 >24、需要与距离解耦"是**错的**：现行 `R3RReservationAudit()` 早已是 `owner == nullptr ⇒ stray`（距离启发式在 KI-203d 就删掉了，函数头注释自陈"no longer guess the owner from nearest train distance"）。所以"自动化"本来就存在，真正缺的只有**触发时延**。详见 §十五。

## 十五、第 243 轮补完：32 位孤儿位逐格数据 + 分类 + 自动化纠正 + 落地（2026-10-11）

### 15.1 玩家手动 `r3r_resv_purge` 的实际结果（新日志，`build\R3R_debug.log` @10:15:28，>7010 行）

命令输出在日志 **6979–7010 行，恰好 32 行**（与 §十四 记的"释放 32 位"逐字吻合）：

| # | tile | track | 轨道语义 | 备注 |
|---|------|-------|----------|------|
| 1 | 1112,129 | 1 | TRACK_Y | 信号格，§六/§八 的 VEHSIG 1112,129 |
| 2 | 1112,130 | 1 | TRACK_Y | 南向直线入弯 |
| 3–12 | 1111,131 / 1112,131 … 1107,135 / 1108,135 | 3 / 2 | LOWER / UPPER | 6 格**成对**斜线（每格南北两轨各 1 位） |
| 13–14 | 1106,136 / 1107,136 | 3 / 2 | LOWER / UPPER | 斜线接直线的两个接头格 |
| 15–17 | 1090,136 / 1091,136 / 1092,136 | 0 | TRACK_X | 站台外侧直线（**一直延伸到 veh22 旧停车点旁**） |
| 18 | 1093,136 | 5 | TRACK_RIGHT | 双槽格，y=136 侧 |
| 19 | 1093,137 | 4 | TRACK_LEFT | 双槽格，y=137 侧 |
| 20–31 | 1094,137 … 1105,137 | 0 | TRACK_X | y=137 成对直线（双线区间另一股） |
| 32 | 1106,137 | 2 | TRACK_UPPER | y=137 侧接斜线 |

### 15.2 分类判定（对应 §十四 的三支）

**命中第一支（KI-346 家族）**：32 位构成 `1090..1112 × y=129..137` 的**整条走廊**，每格 1~2 位，且**同时含 `track=1`(TRACK_Y) 与 `track=0`(TRACK_X)**，正落在 §十四 判据里 → 这条走廊是 KI-346「`rp()` 把入口轨写成当前轨，订出一条车根本不走的轨」的**修复生效前**产生、随存档带进来的存量；现已由引擎侧修复（第 240 轮）断源，只剩存量。

**同时沾第二支（KI-203 家族）**：走廊西端 1090,136/1091,136/1092,136 紧贴 veh22 的旧停车点（§四 记 `veh22 @1082..1085,136`），符合「`ReserveTrackUnderConsist()` 只加不减 + 车节 tile 一变就没人走到」。

**不是第三支（KI-207c）**：32 行全是 `track=N`（plain rail），**没有一行 `track=stn kind=…`** ⇒ 本次没有站台/路点整格位的孤儿。

### 15.3 与 20 格 KI-346 走廊的关系（回答"为什么审计没看见"）

- 首次审计（日志 **190 行**）`RESV-AUDIT-SUMMARY tick reservedTiles=58 owned=38 strayTiles=20 reaped=0 pendingStray=20`，20 条 `RESV-AUDIT-STRAY` 全在 **1093,87..1112,106**（第 240 轮那条），**与本次 32 位是两段不同的走廊**。
- 该审计发生在**读档后第一 tick**（静态计数器初值 0 + `_tick_counter` 很大 ⇒ 立即触发），此刻 veh22 **还活着**，那 32 位经 `GetTrainForReservation()` **能解析到 veh22** ⇒ 被算作 `owned`，不进 stray。
- 之后整段会话**只有这 1 条 SUMMARY**（全日志 `RESV-AUDIT-SUMMARY` 计数 = 1、`RESV-REAP` 计数 = 0）⇒ 第 2/3 次审计根本没跑，所以 veh22 被卖之后那 32 位即使变成无主，也**没机会被老化/回收**；玩家只能手动 `r3r_resv_purge`。

### 15.4 疑点（REG-243-A）：审计为什么整段会话只跑了 1 次

按 `R3R_RESV_AUDIT_TICKS = 2048`，读档后应每 2048 tick 一条 SUMMARY；日志却只有 1 条。两种可能：
- (a) **会话未暂停步进不足 2048 tick**：`_tick_counter` 在暂停时不前进，而玩家这段时间大量处于暂停（读日志、敲命令）⇒ 凑不满一个间隔。**（倾向此项，但未取到 tick 数实证）**
- (b) 存在让审计不再触发的缺陷（如 `_tick_counter` 被重置 / 该分支不可达）。**未证实，未证伪。**
> 判定需要的数据：带 `_tick_counter` 的探针（SUMMARY 行目前**不打印 tick**，这是取不到数的直接原因）。

### 15.5 落地修法（已实现，KI-355 修法 A，仅 `src\train_cmd.cpp`）

既然判据本就是 owner 判定、缺口只在"时延 + 只看 2048 tick 的固定节拍"，本轮做**最小改动**让自动回收在实战中真的跑得起来：

1. 新增文件内 `static bool _r3r_resv_audit_due` + `R3RMarkReservationAuditDue()`（声明在 `CmdMoveRailVehicle` 之前，文件内唯一置位函数）。
2. **可能制造孤儿的两个命令在 Execute 分支置位**：`CmdMoveRailVehicle`（库内重排，KI-203 家族入口）与 `CmdSellRailWagon`（卖车，本次 32 位的直接成因）。
3. 审计门（`TrainLocoHandler` 内）由固定节拍改为**脏标记驱动**：`_r3r_resv_audit_due ? 256 : 2048` 两档；跑完即清标记，所以连点车库只会多跑几次而不是持续满速全图扫描。稳态开销不变（默认仍 2048）。⇒ 卖车后约 `256 × R3R_RESV_REAP_AUDITS(3) ≈ 768 tick`（未暂停时不到半分钟）即自动 `RESV-REAP`，不必再敲命令。
4. 顺手**订正审计门处的过时注释**（原文仍写"lists reserved tiles whose nearest train is far away"，与函数实现不符，正是 §十四 误判的来源）。

### 15.6 复测判据（4 条）

1. 卖出/库内重排后 **不需要** `r3r_resv_purge`，日志自行出现 `RESV-AUDIT-SUMMARY tab reservedTiles=… strayTiles=…`，且 `strayTiles>0` 的格在 3 次审计内出现 `RESV-REAP tile=… age=3`。
2. 负判据：无命令触发的稳态下，SUMMARY 仍按 **2048 tick** 节拍出现（`SUMMARY` 计数与游戏时长匹配），即脏标记没有把稳态开销抬上去。
3. 负判据：正常行驶/等待中的列车，其**有主**的 PBS 预约位不得被 reap（`owned` 计数照旧、不出现对活车路径的 `RESV-REAP`）。
4. 32 位与 20 位两条走廊的存量位：`r3r_resv_purge` 与自动 reap 都能清；清后再审计 `strayTiles` 归 0。

### 15.7 未做

- 未给 SUMMARY 加 `tick=`（§15.4 的疑点因此仍无法从日志判定）。
- 未做"读档即老化"（把随档带入的位在第一次审计就直接视为 age 满）；若实测发现存量位要走满 3 次审计太慢，再补。
- 未动 KI-346 的 `rp()` 平台段 `plat_track = AxisToTrack(axis)`（U-3，仍无取数）。

### 15.8 构建自证（第 243 轮续十二）

复用既有 `_tmp_inc_build.cmd`（**未新建任何 `.cmd`**）。第一次后台启动被脚本自带闸门拦下：`build\R3R_incbuild.done` = `EXIT_CODE=97`（= `openttd.exe` 正在运行，占用 `build\openttd.exe`），此时 `R3R_incbuild.log` 的 mtime 仍是旧值（10:30 那次只写了 done、没写 log）。玩家关闭游戏后重跑：

| 项 | 值 |
|---|---|
| 护栏 | `GUARD: incremental is safe (no header/lang file is newer than the newest object)` |
| ninja 步 | `[2/3] …src\train_cmd.cpp.obj` → `[3/3] Linking CXX executable openttd.exe` |
| 判决 | `build\R3R_incbuild.done` = `EXIT_CODE=0`（10:36:02） |
| 错误计数 | `error C*` / `fatal error` / `FAILED:` / `build stopped` 全 0 |
| 时间戳链 | `src\train_cmd.cpp` 10:29:47 → `train_cmd.cpp.obj` 10:33:52（11 830 352 B）→ `build\openttd.exe` 10:35:53（51 772 416 B） |
| lint | `read_lints` 0 条 |

本轮未新增任何日志字符串（只加了一个 `static bool` + 一个置位函数 + 两档 interval 与注释），故不做 exe 字面量核对，校验以时间戳链 + 退出码 + lint 为准。

---

## 十六、第 243 轮续十三：孤儿的"根治"方向（玩家要求：不产生 / 不成为孤儿）

玩家 2026-10-11 提出方向性要求：不要"事后立即销毁"，希望**从根源上让它不产生**，或让它**不成为孤儿**。本节记录机制层结论与候选方案（**未改码，待拍板**）。

### 16.1 先给结论

- **"不成为孤儿"——在现有 PBS 数据模型下做不到**：预约位是写在地图格 `_m[t].m2` bit8-11 上的**地理标记**，没有 owner 字段；归属靠 `GetTrainForReservation()` 从该格**回溯活车路径**反推。车开走/被卖/被翻转后，没有任何活车的路径能回溯到那格，"有位却无主"是**该数据模型的必然态**。要让位"永不孤儿"，只能把它从"标记"改成"持有者句柄"（见 16.4 方案 C）。
- **"从根源上不产生"——做得到，且这才是正解**。审计 + `RESV-REAP`（含 KI-355 修法 A 的两档节拍）只应作为**兜底/报警器**，不能当主清理路径（玩家会先看到幽灵预留）。

### 16.2 孤儿产生的机械成因（订侧与放侧互不校验）

| 环节 | 现行实现 | 依据 |
|---|---|---|
| 订位 | 沿**计划路径**逐格 `SetTrackReservation()`：`rp()`（`yapf_rail.cpp` 3 处）+ `TryPathReserve`/`ReserveTrackUnderConsist`（`train_cmd.cpp`/`pbs.cpp`） | `TryReserveRailTrack` 计数：train_cmd.cpp 14 / pbs.cpp 4 / yapf_rail.cpp 3 / pbs.h 1 |
| 释放 | ①`ClearPathReservation(车当前 tile+trackdir)`（随车推进）；②`FreeTrainTrackReservation(链头)`（沿**当前** lookahead 链走） | `train_cmd.cpp` 释放段 |
| 归属 | 无 owner 字段；`GetTrainForReservation(tile,track)` 回溯反推 | 同上 |

⇒ **致命点**：订侧认定"计划路径"，放侧走的是"当前路径"。两者一旦分叉，放侧永远碰不到那条位：
1. **订错轨**（KI-346：`rp()` 把入口轨写成当前轨）——第 240 轮已修，但**修复前产生的位随档固化**；
2. **路径被替换**——重订路径 / 逻辑翻转 / 物理翻转 / 解挂 / 库内重排后，旧链不再被 lookahead 覆盖 ⇒ `FreeTrainTrackReservation` 走的是新链；
3. **车被卖/销毁**——释放循环随车一起消失。

且位**共享**（同 tile 不同 track 位）、**跨会话持久**（存 m2 bit8-11、现代档不清）⇒ 一旦漏放就是"永久 + 挡路 + 随档传播"。

### 16.3 推荐方案：层①记录驱动 + 层②生命周期钩子（层③兜底审计保留）

**层① 记录驱动（治本·核心）**：把 KI-203 已有的 per-consist 账本 `_r3r_consist_resv` 从"只记脚下保护位"**推广到全部订位入口**。
- 语义改成：**订 = 记；释放 = 按记**（不再依赖"车走没走过"）。
- 释放前一律走 `R3RReleaseReservationSafe()` 的四道校验（isValidTile / Railway / `IsPlainRailTile` / 位仍在），只放"确实还是我的"，绝不误伤别人的位。
- 账本 NOSAVE + 读档重建（**不动存档格式**）。
- 前提：**订位入口必须收敛**（把 `TryReserveRailTrack` 的散点调用收敛到一个"带记账的包装函数"），否则漏一个入口就漏一类孤儿。

**层② 生命周期钩子（把"事后全图扫描"前移为"当场释放"）**：在每条会让路径断链的事件上，按账本释放该车名下所有位：
- 卖车 `CmdSellRailWagon` / `Vehicle::PreDestructor`
- 库内重排 `CmdMoveRailVehicle`
- 逻辑/物理翻转 `R3RFlipChainBySegments` / `R3RUndoLogicalFlip`
- 解挂 `DecoupleTrain`
- 重订路径前（`FreeTrainTrackReservation` 之前）
- 要点：顺序必须"先释放旧账、再订新路径"，绝不能在位还在用时释放。

**层③ 兜底（已有，保留）**：审计 + `R3R_RESV_REAP_AUDITS=3` + `RESV-REAP`（含 KI-355 修法 A 的 256/2048 两档节拍），定位为**不变量守卫/报警**——`RESV-AUDIT-STRAY` 一旦出现即说明层①②有漏，而非"正常工作流"。

### 16.4 方案 C（终极·若坚持"永不孤儿"）：显式 owner 表

新增 per-tile 稀疏表存持有者（vehicle id / 持有集合），归属判定从"回溯"改为"查表"。释放 = 清表，表空 = 无位 ⇒ 孤儿在定义上不可能。代价：内存 + **存档格式变更** + 与引擎原生 PBS 的一致性维护。**不推荐作为首选**（重、且触碰存档）。

### 16.5 推荐落地顺序与代价

1. 层①（记录驱动）——先覆盖 `rp()` 两处 + `ReserveTrackUnderConsist` ⇒ 直接消灭"订错轨"与"路径分叉"两类孤儿。
2. 层②（钩子）——卖车/翻转/解挂/库编辑当场释放。
3. 层③ 保持，`RESV-AUDIT-STRAY` 从"清理依据"改述为"报警（不该出现）"。
4. KI-355 修法 A 的两档节拍保留作兜底。

风险：账本与引擎位状态必须一致（否则误放别人位 ⇒ 必须四道校验）；订位入口遗漏 ⇒ 必须收敛入口；"先释放后重订"顺序错 ⇒ 会释放正在使用的位。

### 16.6 待拍板

- Q1：按 **层①+层②** 落地（推荐，不动存档）？还是走 **方案 C**（彻底但要改存档）？
- Q2：层①的账本是否要覆盖到"引擎原生 PBS 订位"（即把 `TryReserveRailTrack` 全量收敛），还是要限定在"R3R 自己的订位路径"？
- Q3：`RESV-AUDIT-STRAY` 出现时，是"报警 + 自动 reap"（现状），还是"报警 + 不自动 reap（交由层①②修）"？

---

## 十七、第 243 轮续十四（2026-10-11）："预留到站台的那一段，为什么成了孤儿"

玩家提问原话：「我有一个疑问，那就是列车寻路的时候会预留到下一个安全停车位对吧。那么，场景中列车预留到了站台，那么理论上那一段现在的孤儿预留应该是有主人的，现在那一段怎么变成孤儿了？」

### 17.1 先给一句话答案

**你说得对——订下的那一刻它确实有主。** 但这个引擎里"谁是这个位的主人"**不是写在预留位上的标签**，而是**临时走查**出来的：从这一位出发，沿预留着的轨道往两个方向走，走到哪辆车站在这条预留带上，那辆车就算主人。所以"有主 / 无主"是一个**会随时间衰减的活属性**，不是订位当时烙下的记号。

### 17.2 代码实证：归属 = 走查，不是标签

| 环节 | 实现 | 位置 |
|---|---|---|
| 普通轨道位归属 | `GetTrainForReservation(tile, track)`：两个方向各走一次 `R3RFollowReservationToOwner()`，返回"预留路径**尽头**格上的那辆车"；两向皆空 ⇒ `nullptr` = 孤儿 | `pbs.cpp:1642` / `:1531` |
| 走查本身 | `FollowReservation()` 沿预留位走到尽头 + `CheckTrainsOnTrack()`；站台整格位另有"沿站台扫一遍找车"特例、隧道桥有对端特例 | `pbs.cpp:1553-1584` |
| 站台/路点整格位 | `GetTrainForWholeTileReservation()`：**车正站在格上 ⇒ 它就是主人（定义）**；否则对该格每个预留 track 双向走查 | `pbs.cpp:1681`（`:1686` 是"站在格上"分支） |

⇒ **预留位数据结构里根本没有 owner 字段**。所以"订过、但主人不在了"在这个模型下**是可以存在的**，"有位无主"是**必然可能态**，不是 bug 独有现象。

### 17.3 你关心的那一段位，此刻其实**不是无主**，而是"有主但已死"

现场日志 `build\R3R_debug.log`（veh=23 逼近 1112,129 的红信号探针，反复出现同形读数）：

```
4164: REDSIG   new present=0x8 states=0x7 resv=0x2 veh=0
4166: REDSIG   beyond=1112,130 valid=1 plain=1 resv=0x2 veh=0
4168: REDSIG     beyond track=1 owner=22 near=22
4169: REDSIG     beyond track=1 dir=1 hit=22
4170: REDSIG     beyond track=1 dir=9 hit=36
```

`beyond=1112,130` 的 TRACK_Y 位：**双向走查各命中一辆车**（一向 hit=22、一向 hit=36），合并回答 `owner=22 near=22`。⇒ 这一位**不是无主**，它此刻"有主"（甚至两头各有一个主人）。

这正是它**长命**的原因：审计问的是"这位置能不能走到一辆车"，答案是"能" ⇒ **不算 stray、不进 `RESV-REAP`** ⇒ 幽灵位一直挂着、一直挡路、一直被玩家看见。这就是 §15.3 说的"读档后第一 tick 审计时 veh22 还活着 ⇒ 32 位被算作 owned"的现场形态；本轮补齐了 §11 没记录的一条：**同一个位双向走查可以命中两台不同的车**（登记为 KI-357）。

### 17.4 它**什么时候才真的**变成无主孤儿

三个条件叠加：

1. **弃用而不释放**：释放走的是**车当前的路径**（`ClearPathReservation(车当前 tile/trackdir)` / `FreeTrainTrackReservation(当前 lookahead)`），订位走的是**计划路径**。两者一旦分叉（重订路径 / 逻辑·物理翻转 / 解挂 / 库内重排 / KI-346 订错轨），那条旧位**再也没人去放**。
2. **旧位仍锚着一辆车** ⇒ 走查仍能命中 ⇒ 判为 owned（§17.3，幽灵潜伏期）。
3. **锚车消失**（被卖 / 被删）或预留带被断开 ⇒ 走查返回 `nullptr` ⇒ **这才突然变成"无主孤儿"**，随后被审计认领并 `RESV-REAP`。

⇒ 准确说法是：**它从没被"释放"过，只是在锚车还在时被走查"判成有主"；锚车一没，判据立刻翻脸。** 本场景 32 位的直接成因就是第 3 步——veh22 入库并被卖掉（§12），锚没了，32 位一次性暴露为无主。

### 17.5 若那段"站台"其实是路点（KI-207c 专属那一刀）

订侧 `TryReserveRailTrack()` 按 `HasStationRail(tile)` 分发（**含路点**），放侧所有站台释放都走 `SetRailStationPlatformReservation()` 且被 `IsRailStationTile()` 守卫（**不含路点**）⇒ 路点整格位**引擎永远放不掉**；R3R 自己的记账 GC 遇到这类格只删账本条目、不碰地图（`RESV-GC … why=holder-gone-skip`）。这就是"拆掉路点才消失、只剩路点那一格"的成因（KI-207c，第 125/126 轮）。

### 17.6 对修法的直接含义（把层②从"体验"提到"正确性"）

**因为归属是走查，"有主但已死"就是判据的结构性盲区**：只要幽灵位还连着一辆活车（哪怕是停着不再用的），任何"事后扫全图问有没有主"的方案都抓不到它——它被判为 owned。要抓它，只能在**主人停止使用它的那一刻**当场释放 = §16.3 的层②生命周期钩子（重订路径前 / 翻转 / 解挂 / 库编辑 / 卖车）。

⇒ **兜底扫描在原理上抓不到"有主但已死"这一大类**；它们只能靠钩子根治。KI-356 的优先级因此由"体验优化"上调为"正确性必需"。

### 17.7 登记

- **KI-357（疑点·待验证，低）**：`1112,130` 的 TRACK_Y 位双向走查分别命中 veh22 与 veh36 —— 同一位为何连接两台车？是"一辆车的新订位接在一条死位后面、构成一条连续预留带"（KI-210 同族），还是别的？需按 §11 的方法取 `RESV-WALK` 明细（`R3RResvWalkLog`）逐行确认。**影响**：若确为"接在死位后"，则审计的 `owner != nullptr` 判据会把**死位**一并算成 owned ⇒ 又一类系统性少报（与 KI-351 同族但机制不同）。
- **KI-356 补充**：本轮证据把层②（生命周期钩子）从"体验优化"提升为"正确性必需"。

**本轮未改码、未构建**（纯取证 + 定性）。

---

## 十八、第 243 轮续十五（2026-10-11）："两头都有车，这段预留到底算谁的"

玩家追问：「往两边走，那发现了两个主人，那应该这段预留算谁的？」

### 18.1 一句话答案

**引擎里没有"归属字段"，所以"算谁的"取决于谁来问——三条查询路径、三种口径**（见 18.2 实测调用点表）。而**设计意图上的正确口径是"按最近端切分"**：一条预留带被"车"切成若干段，每段属于离它**最近**的那辆车。"整段算谁的"这个问题本身不成立。

### 18.2 三条口径（实测调用点）

| 口径 | 函数 | 语义 | 实测调用点 | `1112,130` 的答案 |
|---|---|---|---|---|
| **远端走查**（正向优先） | `GetTrainForReservation(tile,track)` `pbs.cpp:1642` → `R3RFollowReservationToOwner()` `:1531` | 从该位沿预留**双向**走，**先走到的**（`TrackToTrackdir` 方向优先）那辆车即主人 | 审计 `train_cmd.cpp:14947`；拆站台/改站 `station_cmd.cpp:1178`；拆改隧道桥 `tunnelbridge_cmd.cpp:552 / :1376 / :1510`；编组冲突检测 `train_cmd.cpp:4353` | **22** |
| **最近端** | `GetR3RReservationOwnerNearby(tile,track)` `pbs.cpp:1592`（整格版 `GetR3RWholeTileReservationOwnerNearby`） | 沿预留走，**第一个（最近的）**车即主人——因为车是链端点，走查**不可能穿过它** | R3R 链编辑清扫的归属闸门 `train_cmd.cpp:10652`（站台版 `:10620`） | 近端是谁就是谁 |
| **单向**（仅供探针） | `R3RReservationOwnerInDirection(tile,trackdir)` | 只走一个方向 | 红信号探针 `train_cmd.cpp:16155 / :16156`（KI-350） | `dir=1→22`、`dir=9→36` |

### 18.3 "两头都有车"意味着什么——KI-210 的注释早已写明

`pbs.cpp:1592` 一带注释：*"A booking can only ever be joined to another one at a train, never through it"*（一条预订只可能在**一辆车处**与另一条相接，绝无可能穿过一辆车）。

`train_cmd.cpp:10609-10619` 更把现场写死：*"A fresh booking joined to a stale one is a single continuous run of reserved bits, so the far-end walk sweeps past the locomotive that booked the fresh part and answers with the parked consist standing at the other end"*，并附现场日志 `RESV-WALK start=38,10 how=direct end=27,9 owner=veh6 head=veh6` + `RESV-GHOST waiter veh=6 tile=34,9 / 35,9 / 36,9 / 37,9 / 37,10 / 38,10`，随后 veh24 下一 tick 又把 38,10 订回来 ⇒ 就是玩家看到的"预览闪一下"。

⇒ **一条预留带上出现两个主人，说明它根本不是"一段"，而是两条预订首尾相接（接缝就是停在中间的那辆车）。** 归属必须**按最近端切分**。

### 18.4 为什么链编辑清扫必须用 nearby（KI-210 的教训）

若误用远端版：一条**活车的新预订**接在一条**死位**后面时，远端走查会**穿过活车**、答出停在另一头（恰是本链）的那辆车 ⇒ 本链误以为拥有那段，于是放掉**邻居的活路线** ⇒ 邻居下一 tick 重订 = 闪烁。修法即改用 `GetR3RReservationOwnerNearby()`（`:10652`）并加 `rail_owner->First() != chain_head ⇒ continue`。

### 18.5 但审计与"拆轨/拆站台"用的仍是远端版 —— 两个暴露面

1. **审计**（`train_cmd.cpp:14947`）：任一端命中即 `owner != nullptr` ⇒ 不算 stray ⇒ 整条走廊被算成"有主"。这就是 §17.4"有主但已死"在代码里的**确切位置**。
2. **拆轨 / 拆站台 / 改隧道桥**（`station_cmd.cpp:1178`、`tunnelbridge_cmd.cpp:552 / :1376 / :1510`）：这些路径拿到的是**远端**那辆车，随后可能 `FreeTrainTrackReservation(v)`。若该位实际属于近端的另一辆车，就存在"**拆一格轨却释放了另一辆车的整条预订**"的风险。原生引擎不会制造"拼接带"，但 R3R 的翻转/拼接/解挂会制造 ⇒ 属 R3R 引入的新暴露面，登记为 **KI-358**。

### 18.6 对修法的含义（与 §17.6 同向）

这正是账本方案（KI-356 层①）要解决的**根本痛点**：归属一旦**记在账上**（每辆车自己记着订了哪些位），"算谁的"就从"猜（走查顺序决定）"变成"查表"，两端都有车也不再歧义，并顺带干掉 18.5 的两个暴露面。⇒ **层①治"归属歧义"、层②治"有主但已死"，缺一不可。**

### 18.7 登记

- **KI-357 补充（状态：疑点，方向已明确）**：答案＝**按最近端切分**，`1112,130` 属近端那辆车；仍需 `RESV-WALK` 明细确认该位是"22 死走廊的东段"还是"36 活链的末端"。
- **KI-358（新，疑点·待验证，中）**：远端走查口径的调用点（审计 `:14947`、拆站台 `station_cmd.cpp:1178`、隧道桥 `tunnelbridge_cmd.cpp:552/:1376/:1510`）在"拼接带"上把归属判给对端车 ⇒ ①审计系统性少报"有主但已死"；②拆轨/改站台路径可能 `FreeTrainTrackReservation(对端车)` 而释放错车的活预订。待验证：能否构造"拆掉拼接带上一格轨 → 对端车丢预订"的现场。

**本轮未改码、未构建**（纯取证 + 定性）。

---

## 十九、第 243 轮续十六（2026-10-11）：玩家给出更清晰的归属图景 —— 实测证实"铺的人 ≠ 被算的人"

### 19.1 玩家的新理解（原话）

> 「1090,136 以及它往西北方向，是 veh23 铺设的预留；它的东方向那段，是整段站台的预留、属于 veh22。也就是说，这段幽灵预留不应该是孤儿，而属于 veh23。」

### 19.2 实测核对（`build\R3R_debug.log`，2026-10-11 10:58，1 189 221 B，**本会话**）

**① 这些位确实是 veh23 铺的**（巡检探针逐字为证）：
```
2057: RESV-WATCH SET-RAIL tile=1112,130 track=3 phase=choose-track actor=23 head=23
2058: RESV-WATCH SET-RAIL tile=1112,131 track=2 phase=choose-track actor=23 head=23
3503: RESV-WATCH SET-RAIL tile=1112,131 track=2 phase=yapf-couple actor=23 head=23
3504: RESV-WATCH SET-RAIL tile=1112,130 track=1 phase=yapf-couple actor=23 head=23
3505: RESV-WATCH SET-RAIL tile=1112,129 track=1 phase=yapf-couple actor=23 head=23
3506: RESV-WATCH SET-RAIL tile=1112,128 track=1 phase=yapf-couple actor=23 head=23
```
上下文：`3947: CPL-PAIR act=23 tgt=22 dist=57 actTile=1112,106 tgtTile=1085,136`、`LOCO-MOVE veh=23 tile=1112,106..113 spd=112` ⇒ veh23 正在跑**去挂 veh22** 的耦合寻路，`phase=yapf-couple` 即该路（yapf_destrail 的 `rp()` 预留）。

**② 审计与走查却把 owner 判给 veh22**：
```
12399: RESV-AUDIT-OWNER tick tile=1112,128 track=1 owner=veh22 bits=0x2
12401: RESV-AUDIT-OWNER tick tile=1112,129 track=1 owner=veh22 bits=0x2
12403: RESV-AUDIT-OWNER tick tile=1112,130 track=1 owner=veh22 bits=0x2
12405: RESV-AUDIT-OWNER tick tile=1112,131 track=2 owner=veh22 bits=0x4
13602: RESV-WALK start=1112,130 td=1 phase=controller how=direct end=1082,136/td0 owner=veh22 head=veh22
```
⇒ 同一条位：**铺设者 = veh23，引擎答的归属 = veh22**。

### 19.3 机制：走查"穿过自己的路线、被终点那辆车骗走归属"

veh23 停在 1112,125（`13595 DEPOTBLK veh=23 tile=1112,125 spd=0`），它铺的位在车头前方 1112,128..131。**铺设者站在预留带的入口端之外**；走查从 1112,130 按 `td=1`（`TrackToTrackdir` 方向）往带内走，一路走到带子**另一端** `1082,136`，那里停着 veh22 ⇒ 走查撞上 veh22 即返回它。

**⇒ 修正 §18.3**：这里**只有一条预订**（veh23 自己铺的耦合路线），不是"两条预订首尾相接"。§18.3 的"两端都有车 ⇒ 两条预订拼接"只是成因之一；**更常见的成因是"自己路线的终点格上站着另一辆车"——而这对耦合寻路是必然**（耦合路线的终点永远是待挂的那辆车）。

### 19.4 为什么"不是孤儿"也不够

`1112,128..131` 被判 `RESV-AUDIT-OWNER owner=veh22` ⇒ 审计口径下"有主"，**永远不会被 reap**。玩家说"不应是孤儿"与审计一致；**分歧只在归属人**：玩家=veh23、引擎=veh22。

危害在**谁来释放**：引擎认为它是 veh22 的 ⇒ 只有 veh22 那条链的清扫会碰它（`GetR3RReservationOwnerNearby` 同样答 veh22），而 veh22 侧根本不知道这条路（不是它铺的）、清扫的 `chain_head` 也不匹配 ⇒ **谁都不放**。veh23 侧若耦合失败/取消，`FreeTrainTrackReservation(veh23)` 只按它自己**当前**那条路径释放 ⇒ 这些位留在图上。

⇒ 这是"预览挂着不放"的直接成因，且**每次挂钩失败都会产生**（普遍性远高于拼接带）。

### 19.5 对修法的含义

"**按铺设者记账**"是唯一能同时覆盖 19.3 与 §18.3 两种情形的口径：账本记 `车辆 → 它铺的位集合`，审计问"这些位是否还有活着的铺设者"，释放时找**铺设者**而不是找走查撞到的车。⇒ **KI-356 层①（账本）由"解决归属歧义"上升为"修正归属判据本身"**。

### 19.6 登记

- **KI-358 状态升级**：**已实证**（`SET-RAIL actor=23` 与 `AUDIT-OWNER owner=veh22` 同格互证）；严重度 **中 → 高**（普遍发生 + 审计永久放过）。
- **KI-357 更新**：`1112,130` 的 `TRACK_Y` 位 = **veh23 铺的耦合路线**（不是"veh22 死走廊的东端"）。本会话无 `RESV-PURGE` ⇒ §十七那条 `1090..1112×129..137` 走廊属**上一会话**，其日志已被覆盖，须重取再定案。
- **新疑点 REG-243-B（低）**：`RESV-WALK … found=2047,2097151`（行 3336 / 13602 / 14227 / 14328）里 `found_on` 打出 **2047,2097151**，既非合法格坐标、也不是 `INVALID_TILE`（应打 4095,4095）⇒ `how=direct` 分支下 `found_on` 的初值/打印有问题，属探针缺陷，待核对 `R3RResvWalkLog` 默认实参。
- **旁证**：本会话 `RESV-GHOST decouple/waiter veh=2 / veh=11 / veh=22 / veh=23 tile=1090,139 / 1090,140 / 1081,136 track=0` 反复出现，说明解挂缝附近 1090,139-140 也是同类"铺了又被算给别人"的地带；`RESV-AUDIT-SUMMARY tick reservedTiles=72 owned=70 strayTiles=2` 表明当时除 `980,132 / 980,136` 两格外无 stray。

**本轮未改码、未构建**（纯取证 + 定性）。

---

## 二十、第 243 轮续十七（2026-10-11）：玩家问「按铺设者记账会不会拖慢游戏」——实测与成本口径

### 20.1 问题

玩家：记录哪辆车铺了哪些位当然感觉是个好主意，但是否会导致游戏的大量延迟。

### 20.2 关键事实：这件「记账」**已经**存在于全游戏最热的路径上，而且有实测数字

- 六个 setter 是**唯一**写预留位的口子（`SetTrackReservation` / `SetDepotReservation` / `SetCrossingReservation` / `SetRailStationReservation` / `SetTunnelReservation` / `SetBridgeReservationTrackBits`，见 `r3r_resv_probes.h` 头注释 :9-19），它们**已经**逐次调用 `R3RResvWatchLog()`；`pbs.cpp:49/56` 已有全局 `r3r_resv_phase` 与 `r3r_resv_actor`，由 `R3RResvPhaseGuard`（`pbs.h:59-73`）RAII 设置，取值点已覆盖 consist / choose-track / controller / yapf-couple / waiter-platform / release-ghost / reap / gc-stn（`train_cmd.cpp:10660/13653/14804/14988/15007/15089/15167/15957/17383`、`yapf_rail.cpp:442/796`）。
- ⇒ 「谁订了这一位」**不需要新增任何调用点**，只要把 `R3RResvWatchLog()` 里那句「不是关注格就 return」换成「写进 `r3r_resv_actor` 的账本」。
- 现有实现已经在同一条路径上做同类工作：`Train::ReserveTrackUnderConsist()`（`train_cmd.cpp:15086`）每次呼叫都构造 `std::vector<R3RResvRec> r3r_fresh`（**每次堆分配一次**）+ 每节 `push_back` + 末尾 `R3RReconcileConsistReservation()`（unordered_map 查写）。

实测（`build\R3R_perf.log`，2026-10-11 11:39 本会话，**Debug 内测树**，世界仅 6 链 / 36 辆；含 `resv=` 的样本 26 790 条，其中非零 943 条）：

```
PERF-TICK loco=14504.4/s 608ms/s | coupleH=2707.0/s 66ms/s | plat=3778.4/s 180ms/s |
          resv=3778.9/s 123ms/s | edgeGate=47965.7/s 36ms/s | ctrl=3816.4/s 101ms/s | coll=3816.4/s 8ms/s |
          spd=6118.2/s 36ms/s | vp=6118.2/s 8ms/s | mov=10624.9/s | ord=11823.4/s 10ms/s | ...
PERF-TICK loco=11707.7/s 657ms/s | plat=3366.5/s 168ms/s | resv=3366.9/s 114ms/s | ctrl=2879.6/s 85ms/s | ...
PERF-TICK loco=17405.8/s 557ms/s | plat=3481.2/s 149ms/s | resv=3481.2/s 114ms/s | ctrl=4820.8/s 71ms/s | ...
PERF-TICK loco=8118.7/s 344ms/s  | plat=1575.3/s 69ms/s  | resv=1575.8/s 53ms/s  | ctrl=1753.4/s 20ms/s | ...
```

`resv` 桶 123 ms/s ÷ 3778 次 ≈ **32 µs/次**（每条 6 节车、Debug 无优化 + RTC1）。

### 20.3 结论：记账本身不是延迟来源

- 一次账本写入 = 一次按 actor 的查表 + 一个小向量追加 ⇒ Release 量级 **~0.1 µs**。
- 写预留位的总频次 ≈ `resv` 调用数 × 每列车节数（本例 3.8 k/s × 6 ≈ 2.3 万/s），附加成本 ~0.4~2 ms/s；相对同一时刻预留机制自身的 123 ms/s（Debug）可忽略。
- **放大因子与预留机制本身同阶**（都随 列车数 × 路径长度 线性），不会出现"记账比被记账的事更贵一阶"。
- 唯一会真的拖慢的写法是把钩子塞进 yapf 的**逐节点/逐格扩展**。需注意 `rp()`（`yapf_rail.cpp:1008`）**只在找到路后调用一次**（`:1113`，内部沿路径递归，写次数 = 路径长），不是逐节点循环 ⇒ 在那里记账同样安全。

### 20.4 会真的造成卡顿的五种写法（实现纪律）

1. 在 `reach()` 一类**逐格扩展**里写账本（应只在 `rp()` 提交路径时写）。
2. 每次写都 `new` / 新建 `vector`（现有 `r3r_fresh` 每次呼叫分配一次就是反面教材，顺手应改成可复用缓冲 —— 这条比账本本身更值钱）。
3. 账本进存档（改存档格式 ⇒ KI-15 全量重编 + 反同步面）。账本应**内存态、读档重建**。
4. 审计/GC 变成 O(列车数 × 格数)；应保持 O(预留格数 + 路径长)。
5. 账本键用 `Train*`（车会被删）。必须用 `VehicleID` + 查询时 `Vehicle::GetIfValid`，并在 `PreDestructor` 清条目；否则"记账"会变成新的**「有主但已死」镜像 bug**。

### 20.5 登记

- **REG-243-C（新·待定实现口径，中）**：账本落点定在**六个 setter**（复用既有 `R3RResvWatchLog` 入口 + `r3r_resv_actor`），而不是 20 个调用点。**先决条件**=确认"普通 PBS 路线可从 `train->path` 反推，只有 `yapf-couple` 的 `rp()` 位必须记账"，如此热路径新增成本趋近于零。该先决条件未验证（本轮未取数）。

**本轮未改码、未构建**（纯取证 + 定性）。

---

## 二十一、第 243 轮续十八（2026-10-11）：账本式修复落地方案（先落盘再改码）

### 21.1 玩家拍板
玩家 2026-10-11：「好，那可以按照你说的来执行修复。记得先落盘临时备忘」。
⇒ 依 §16.3 / §17.4 / §19.5 / §20.5 的结论执行三层：**层① 按铺设者记账（修正归属判据本身）+ 层② 生命周期钩子（防新孤儿）+ 层③ 审计兜底（「有主但已死」）**。

### 21.2 先决条件（REG-243-C）核实结论：取消「区分」
原以为要区分「普通 PBS 路线可从 `train->path` 反推」与「只有 `yapf-couple` 的 `rp()` 位必须记账」。查 `R3RResvPhaseGuard` 的全部设点后该区分不再必要——**普通 PBS 路线也带 actor**：
`train_cmd.cpp:13653 choose-track(R3RResvActorID(consist))`、`15957 controller(v)`、`15089 consist(this)`、`15167 clear-consist(this)`、`17383 waiter-platform(consist)`、`10660 release-ghost(chain_head)`、`yapf_rail.cpp:442(nullptr, GetVehicle())`、`:796 yapf-couple(v)`。
⇒ **统一口径：所有预留位都记账**（`actor == Invalid` 时早退 = 不记），不区分来源。热路径附加成本按 §20.3 实测口径 ~0.1 µs/次，相对预留机制自身的 123 ms/s（Debug）可忽略。

### 21.3 归属的唯一口径：**铺设者 = actor 所在链的链头**
- 引擎的走查（`GetTrainForReservation`）答的是「路径**另一端**站着的车」，耦合路径的另一端**恒是待挂目标** ⇒ 结构性误判（§19.3 实证：`SET-RAIL actor=23` vs `AUDIT-OWNER owner=veh22`）。
- 账本记 `r3r_resv_actor`，并在记账时**规范化到链头**（`Vehicle::GetIfValid(actor)->First()->index`），使同一链从任意车节触发的记账/释放都能对上。

### 21.4 数据结构（内存态、NOSAVE、读档重建）
落点 `src/pbs.cpp`（六个 setter 的 `R3RResvWatchLog` 已是全写入点的汇点，天然 complete by construction）：
```
struct R3RResvLedgerRec { TileIndex tile; Track track; };
static std::unordered_map<uint32_t, std::vector<R3RResvLedgerRec>> _r3r_resv_by_owner; // owner(head id) -> bits
static std::unordered_map<uint32_t, uint32_t> _r3r_resv_owner_of_bit;                  // bitkey -> owner
```
`bitkey = (tile.base() << 3) | slot`，`slot = (track == INVALID_TRACK) ? 7 : track`（0..5 真实轨、7 整格位/隧道/桥/道口/库），与既有 `R3R_RESV_WHOLE_TILE_KEY`、`_r3r_resv_stray_age` 同一编码。
**已知取舍**：铁路/桥的 `CHG-*` 只报 `FindFirstTrack(b)` 一个轨 ⇒ 多轨同格时账本只记首轨（漏记方向 = 少释放，由层③兜底，绝不误释放）。

### 21.5 API（`pbs.h` 声明，`pbs.cpp` 定义）
```
inline constexpr uint32_t R3R_RESV_NO_OWNER = UINT32_MAX;
void      R3RResvLedgerNote(const char *kind, TileIndex tile, Track track);   // SET-*/CHG-* 记，CLEAR-* 销
uint32_t  R3RResvLedgerOwner(TileIndex tile, Track track);                    // 审计问「谁铺的」
void      R3RResvLedgerForget(VehicleID owner, const char *why);              // 层②：按 owner 释放它铺的全部位
void      R3RReleaseReservationSafe(uint32_t key, TileIndex tile, Track track, const char *why); // 从 train_cmd.cpp 搬来（原 static）
```
`R3RReleaseReservationSafe` 搬迁理由：`Forget` 需要它，而它原本是 `train_cmd.cpp` 的文件内 static；搬到 `pbs.cpp` 后 `train_cmd.cpp` 三处调用点（reconcile/GC/clear-consist）**一行不改**（同名同参，声明进 `pbs.h`）。

### 21.6 层②钩子清单（防新孤儿）
1. `Vehicle::PreDestructor`（`src/vehicle.cpp`，Train 分支）→ `R3RResvLedgerForget(this->First()->index, "destroy")`：**锚车被卖当场释放**，不等审计（这是 §17.4 第 3 步的直接修复）。
2. `FreeTrainTrackReservation`（`train_cmd.cpp:12921`）入口 → `Forget(consist->First()->index, "free-path")`：耦合寻路被取消/重订时，把 `rp()` 铺的临时位一并清（§19 的 veh23 位正是这条）。
3. `DecoupleTrain`（`train_cmd.cpp`）对 v、u 各一次 → `"decouple"`。
**刻意不做**：逻辑翻转（`R3RFlipChainBySegments`/`R3RUndoLogicalFlip`）与车库编辑点不加钩子——翻转只改 direction/段边界，位可能仍被使用，释放交给上述 2/3 与层③，避免误清。

### 21.7 层③审计增强（`R3RReservationAudit`，`train_cmd.cpp`）
stray 判据由「`GetTrainForReservation() == nullptr`」扩为「**或账本 owner 已死**」：
```
const uint32_t lowner = R3RResvLedgerOwner(tile, track);            // 站台用 INVALID_TRACK（slot 7）
const bool ledger_dead = (lowner != R3R_RESV_NO_OWNER) && (Train::GetIfValid((size_t)lowner) == nullptr);
if (owner == nullptr || ledger_dead) -> stray（照旧老化 3 次后 RESV-REAP）
```
⇒ 覆盖「有主（走查答得出）但铺主已不存在」这一类，原先被 `owner != nullptr` 永久放过（KI-355 层②的根因）。日志在 `RESV-AUDIT-STRAY` 行尾追加 `lowner=%d`（-1 = 无账本记录）便于判读。

### 21.8 编译期开关口径（重要决策）
账本是**正式修复**，不是探针 ⇒ `r3r_resv_phase`/`r3r_resv_actor`/`R3RResvPhaseGuard`/`R3RResvWatchLog` 一律移出 `#if R3R_PROBES`（无条件编译），只有 `R3RIsWatchedResvTile()` 与 watch 日志体留在探针内。代价 = 改 `pbs.h` + `r3r_resv_probes.h` + 六个 setter 头 ⇒ **护栏必判全量重编**（已知、可接受）。

### 21.9 实现纪律（逐条对照 §20.4）
1. 只挂在 `R3RResvWatchLog`（＝六个 setter），**不进 yapf 的逐格 `reach()`**。✅
2. 追加进已有 vector、不新建容器；`Forget` 用 `std::move` 取走再释放。✅（`ReserveTrackUnderConsist` 的 `r3r_fresh` 每次分配是既有问题，本轮不动。）
3. 账本**不进存档**、读档为空（读档后靠走查 + 层③）。✅ 不动 `saveload/*`。
4. 审计仍是一次全图 O(格) 扫描，账本查询 O(1) 哈希。✅ 不引入 O(车×格)。
5. 键一律 `VehicleID` 的 base（非 `Train*`），查询走 `Vehicle::GetIfValid`；`PreDestructor` 清条目。✅

### 21.10 构建与自证（已完成，2026-10-11 12:44）
**计划**：复用 `_tmp_inc_build.cmd`（未新建任何 .cmd）。因改 `pbs.h` + `r3r_resv_probes.h` + 六个 setter 头，护栏判全量（约 692 步、-j2 约 20 分钟）。
**字面量口径更正**：账本记账本身**不打独立日志串**（`R3RResvLedgerForget` 只做释放，其效果体现在 `RESV-GC ... why=…` 行），故本节原先写的 `RESV-LEDGER` **不存在**；exe 判据改用 `lowner=`（审计行新增字段）与 `why=`。

**实测（全部通过）**：
- 护栏 `build\R3R_incbuild.guard.log`（12:17:24）：`7 header/lang file(s) are newer than the newest object file`（`rail_map.h`/`bridge_map.h`/`road_map.h`/`station_map.h`/`tunnel_map.h`/`r3r_resv_probes.h`/`pbs.h`）⇒ `REMOVED 620 object file(s) - upgrading this build to a FULL rebuild`。
- 结果确为**全量 692 步**：`[692/692] Linking CXX executable openttd.exe`，`build\R3R_incbuild.done` = `EXIT_CODE=0`（12:44:19）。
- 错误计数：`error C*` / `fatal error` / `FAILED:` / `build stopped` = **0**。
- `build\openttd.exe` @2026-10-11 12:43:51（51 843 072 B）晚于全部源码；obj 均晚于对应 `.cpp`：`pbs.cpp` 12:13:08 → `pbs.cpp.obj` 12:35:04、`train_cmd.cpp` 12:16:07 → `train_cmd.cpp.obj` 12:38:52、`vehicle.cpp` 12:16:31 → `vehicle.cpp.obj` 12:39:14。
- `read_lints`：`pbs.cpp` / `pbs.h` / `train_cmd.cpp` / `vehicle.cpp` / `r3r_resv_probes.h` 全 **0 条**。
- exe 字面量：`lowner=%d` ✔、`RESV-AUDIT-STRAY` ✔、`RESV-GC` ✔、`why=` ✔、`free-path` ✔、`decouple` ✔、`destroy` ✔。

**实现与方案一致性核对**：§21.4~§21.9 逐条落地，唯一加强点 = `Vehicle::PreDestructor` 的钩子写成 `Train::From(this)->First()` 并判空（链头取不到时退回 `this->index`），比方案中的 `this->First()->index` 更稳。**无行为偏差**。

### 21.11 复测判据（游戏内）
1. §19 场景复跑：`1112,128..131` 的位在 veh23 耦合失败/取消后**不再长期挂着**（应出现 `RESV-GC ... why=free-path` 或审计 `lowner=` 后 `RESV-REAP`）。
2. `RESV-AUDIT-OWNER … owner=veh22` 若仍出现，同格也应有 `RESV-WATCH SET-RAIL actor=23` 可对照（判归属分歧仍在但释放已由账本接管）。
3. 卖车/库内整体删除一列后 3 次审计内出现 `RESV-REAP`（层②当场释放优先，`LCOWNER` 死应立刻清）。
4. 负判据：正常行驶列车的预留照旧（可 lift 铁轨仍被拒绝）、`RESV-AUDIT-SUMMARY` 的 `owned` 不异常下降、无 `rail_map.h:168` / `pbs.cpp:144` 断言。
5. 无新增卡顿：`R3R_perf.log` 的 `resv=` 桶与第 243 轮基线（3778 次/s、123 ms/s，Debug）同阶。

### 21.12 未做/待办
- 层①的「多轨同格只记首轨」粒度问题（见 21.4 取舍）。
- 读档后账本为空 ⇒ 读档当场的老孤儿只靠走查 + 层③（与现状一致，不追溯）。
- `FreeTrainTrackReservation` 钩子若在某些高频路径被调用，需按 §21.11 判据 5 复核开销（已加 `_r3r_resv_by_owner` 一次查找早退）。
- 逻辑翻转点是否也需要钩子：等层②/③复测后再定（本轮按纪律不做）。

## 22. 第 243 轮续十九（2026-10-11 12:52 复测）：账本真值 vs 走查误判 —— veh23 被自己订的 4 格挡住

### 22.1 采样有效性
- 玩家复测日志：`D:\temp\codebuddy-dropped-files\cbaf9a51-64fb-48a9-b26b-d9965e01d32e\R3R_debug.log`，532 176 B / **7528 行**，LastWriteTime 2026-10-11 12:52:35。
- 产生者 `build\openttd.exe` @2026-10-11 **12:43:51**（51 843 072 B）= 本轮 KI-360 账本版（§21.10 全量 692 步产物）⇒ **采样有效**，本日志是本轮账本修复后的实测。
- 玩家原话：「根据我的复测，问题依然存在」。

### 22.2 玩家现象
`1112,128..131` 一带长期带预留（黑色预览/红色信号），veh23 停在 `1112,125` 出不去（＝第 243 轮以来「1112,129 那个红信号」的同一现场）。

### 22.3 探针计数（本轮首次出现账本释放日志）
| 探针 | 计数 | 说明 |
|---|---|---|
| `RESV-GC` | **3361** | why: `free-path` 3304 / `holder-gone-skip` 24 / `decouple-skip` 24 / `free-path-skip` 6 / `decouple` 3 ⇒ 层②确在工作；54 条 `-skip` = 账本记了但地图校验不过（`R3RReleaseReservationSafe` 拒绝，安全行为） |
| `RESV-AUDIT-SUMMARY` | 1 | `tick=6930 reservedTiles=38 owned=38 strayTiles=0 reaped=0 pendingStray=0` |
| `RESV-AUDIT-STRAY` / `-OWNER` / `-WATCH` / `RESV-REAP` / `RESV-PURGE` | 0 / 0 / 9 / 0 / 0 | 审计视野内**一切正常** |
| `RESV-WATCH SET-RAIL tile=1112` | **6** | vs `CLEAR-RAIL tile=1112` 仅 **2** ⇒ 差 4 格无人清 |
| `REDSIG veh=23` | 5 | 红色信号事件 |
| `CPL-PAIR act=23` / `COUPLE-FAIL loco=23` / `CPL-RESERVE veh=23` / `LOCO-MOVE veh=23` | 80 / 16 / 1 / 76 | 反复尝试耦合、反复失败、原地打转 |
| `RESV-GHOST` / `RESV-NOBOOK` | 2 / 1 | 6081-6082 `waiter veh=22` + 6098 `NOBOOK veh=22 order=17 caronly=0`（第 127 轮 KI-208 门禁生效） |

### 22.4 决定性证据（逐行原文）
```
6537: RESV-WATCH SET-RAIL tile=1112,131 track=2 phase=yapf-couple actor=23 head=23
6538: RESV-WATCH SET-RAIL tile=1112,130 track=1 phase=yapf-couple actor=23 head=23
6539: RESV-WATCH SET-RAIL tile=1112,129 track=1 phase=yapf-couple actor=23 head=23
6540: RESV-WATCH SET-RAIL tile=1112,128 track=1 phase=yapf-couple actor=23 head=23
6541: CPL-RESERVE veh=23 reserved=1 next=1
6542: TRP veh=23 mf=1092,86 real=6 type=16 dest=287807 spd=0 ... => ok=1 res=1
```
（尾部，同一日志）
```
REDSIG veh=23 old=1112,128 new=1112,129 dir=1 chosen=0x2 i=1 prog=0 spd=2 wait=0 stuck=0
REDSIG   new present=0x8 states=0x7 resv=0x2 veh=0
REDSIG   beyond=1112,130 valid=1 plain=1 resv=0x2 veh=0
RESV-WALK start=1112,130 td=1 phase=controller how=direct end=1082,136/td0 found=2047,2097151 owner=veh22 head=veh22
REDSIG     beyond track=1 owner=22 near=22
REDSIG     beyond track=1 dir=1 hit=22
REDSIG     beyond track=1 dir=9 hit=36
CPL-PAIR act=23 tgt=22 dist=38 actTile=1112,125 tgtTile=1085,136
COUPLE-FAIL loco=23 order=16 tx=1112 ty=125 x=17800 y=2007
COUPLE-WAIT-OFFDEST veh=23 tile=1112,125 mf=1112,128 real=6 isDepot=0 destDepot=0 destStn=1 destTile=287807
```

### 22.5 判读（结论）
1. **真值**：`1112,128..131` 的铺设者 = **veh23**（账本 `actor=23 head=23`，`phase=yapf-couple`）。**判据口径**：走查答 **veh22**。这正是 KI-359 要治的「铺的人 ≠ 被算的人」，只是方向反过来（这次是**自己的格被判给别人**）。
2. **veh23 被自己订的位挡住**：红色信号（`REDSIG`）以走查 owner（veh22）为准 ⇒ 不自家人放行；同时这 4 格没有任何释放出口 ⇒ 停在 `1112,125`，`COUPLE-FAIL loco=23` 16 次。
3. **审计放行**：层③判据 = `owner == nullptr || ledger_dead`；本例 `owner=veh22`（活车）且账本 owner=veh23（活车）⇒ 两条都不成立 ⇒ `strayTiles=0`、`RESV-AUDIT-STRAY=0`。玩家能看见的残留**恰好落在审计视野之外**——KI-359 只补了"主人已死"，没补"主人被判错"。
4. **层②释放缺口**：`RESV-WATCH SET-RAIL tile=1112` 6 次 / `CLEAR-RAIL tile=1112` 2 次，差的 4 格即 6537-6540 那批；全日志无 `RESV-GC ... veh=23` 行 ⇒ `phase=yapf-couple`（耦合寻路）订下的位，在耦合失败/放弃/重试后**没有释放出口**（三个层②钩子 destroy/free-path/decouple 都碰不到它）。

### 22.6 猜测根因（待验证）
- **G1 走查串链**：`R3RFollowReservationToOwner` / `GetTrainForReservation` 沿预留格前进时不校验「下一个格是否属于同一 owner」，在成片预留的站场里会一路串到别条链上的车。支撑：`RESV-WALK start=1112,130` 与 `end=1082,136` 相距 30 格却答 `owner=veh22`；`found=2047`。待验证判据＝打印走查路径每格的账本 owner，看是否中途跨越了非 veh22 的格。
- **G2 判据未接账本**：`REDSIG`（红色信号）与审计的 owner 都直接来自走查；账本只被层③的 `ledger_dead` 用了一点点。修法＝判据侧"账本优先"。
- **G3 yapf-couple 预留无释放出口**：耦合寻路订下的位在耦合失败/放弃/成功后都没有 forget 调用点。

### 22.7 下一版复测判据
1. `1112,128..131` 应出现释放行（`RESV-GC ... veh=23 why=couple-abort|free-path`），`SET-RAIL tile=1112` 与 `CLEAR-RAIL tile=1112` 计数应配平。
2. `REDSIG` 的 owner 口径应为 **23**（账本），veh23 不再被自己的位挡住 ⇒ `CPL-PAIR dist` 能缩到命中阈值并出现 `COUPLE-OK loco=23`。
3. 审计新增"账本/走查分歧"计数（如 `ledgerMismatch=N`）应 >0（本例 4 格）。
4. 负判据：他车真预留仍须红灯（不得因账本口径而放行别人）。

### 22.8 未确认
- **U-1**：为何这 4 格的 track 是 `131=2 / 130=1 / 129=1 / 128=1`（track=2 与 track=1 混用）。
- **U-2**：veh23 从 `1092,82`（6543 `LOCO-MOVE`）一路开到 `1112,125`，中间是否还有第二次 `CPL-RESERVE`（计数仅 1 次）。
- **U-3**：`end=1082,136` 的 veh22 是等待挂接方（order 17，`RESERVECONSIST` 链 1082..1087,136），它为何成了这 4 格的走查主人。
---

## 23. 第 243 轮续二十（2026-10-11 12:5x）：**§22 复测结论作废 —— exe 陈旧，账本优先根本没编进去**

> 本节第一优先级：先把"疑点/纠错"落盘（用户长期规则），取证过程如下，均为只读命令。

### 23.1 纠错 A（决定性，作废 §22.5 第 2 条）

| 对象 | mtime | 说明 |
|---|---|---|
| `src\pbs.cpp` | **2026-10-11 12:58:38** | KI-361 的"账本优先"行在此文件里 |
| `build\CMakeFiles\openttd_lib.dir\src\pbs.cpp.obj` | 2026-10-11 12:35:04 | 比 pbs.cpp 旧 23 分钟 |
| `build\openttd.exe`（= §22 复测所用） | **2026-10-11 12:43:51** | 比 pbs.cpp 旧 15 分钟 |

⇒ **§22 复测用的 exe 不含 `GetTrainForReservation()` 里的账本优先行**（该行带 `现场日志 2026-10-11` 注释，属 12:58 那次改动）。因此 §22.5 第 2 条「红色信号以走查 owner 为准 ⇒ 不自家人放行」**不能**作为"账本优先无效/未生效"的证据，它只是"那个 exe 里从来没有账本优先"。
⇒ **§22.7 第 2 条复测判据必须在"含账本优先的 exe"上重跑**才算数。

### 23.2 纠错 B（证据链表述错误，结论不变）

§22.4 / §22.5 第 4 条写的「全日志无 `RESV-GC ... veh=23` 行」**表述有误**：实测 `RESV-GC veh=23` 共 **2052 行**，但**最后一行是 6487**（`tile=1097,92 track=0x4 why=free-path`），即全部早于 6537（`SET-RAIL tile=1112,131`）。正确的表述是："**6537 那批位落地之后，再没有任何释放该车账本行的记录**"——结论(层②释放缺口)不变，但引用的读数必须订正。

### 23.3 日志自身就给出了反证（只读复核）

`tile=1112,130` 的 `RESV-WATCH` 全日志**只有 3 行**：
- 1499 `SET-RAIL track=3 phase=choose-track actor=23 head=23`
- 1613 `CLEAR-RAIL track=3 phase=controller actor=23 head=23`
- **6538 `SET-RAIL track=1 phase=yapf-couple actor=23 head=23`**

此后到 7288（`REDSIG beyond=1112,130`）之间，该格**零 SET / 零 CLEAR / 零 GC / 零 PURGE / 零 REAP**。
⇒ 账本行 `(1112,130, slot=1) → 23` 一直存在。**若 exe 含账本优先，`GetTrainForReservation(1112,130,TRACK_Y)` 必返回 23 且不会触发走查**；而 7289 明确出现 `RESV-WALK start=1112,130 ... owner=veh22` ⇒ **该 exe 必然不含账本优先**，与 23.1 的时间戳互证（两路证据独立且一致）。

### 23.4 状态修正

- KI-360（层①②③账本落地）：已实现 + **已编译**（12:43 exe 含）。
- KI-361（层①账本优先进 `GetTrainForReservation`）：状态由「未修」改为 **「已实现（源码 12:58:38），未编译」**；§22 的"未生效"结论作废。
- 仍未做：判据③（审计侧"账本/走查分歧"计数）；G3（yapf-couple 预留释放出口）**先不做**——理由：账本优先后红色信号会认自己的位放行，veh23 能走完自己订的路并在拼接点命中，此时那 4 格是它自己正在使用的路（不是孤儿），耦合成功后由正常 free-path 路径释放；是否仍需补 G3 应由新 exe 的日志决定（若仍出现"订了不走"的位再补）。

### 23.5 本轮落地清单

1. **构建**：`src\pbs.cpp`(12:58:38) 新于 `pbs.cpp.obj`(12:35:04) ⇒ 合法增量，复用 `_tmp_inc_build.cmd`（未新建任何 .cmd）。
2. 构建自证三件套：`build\R3R_incbuild.done` 的 `EXIT_CODE=0`、日志尾部 `[n/n] Linking CXX executable openttd.exe`、`build\openttd.exe` mtime 晚于 12:58:38；再核对 `pbs.cpp.obj` 晚于 `pbs.cpp`。
3. 复测判据：沿用 §22.7 四条，但**第 2 条为关键判据**（`REDSIG beyond track=1 owner=23`、不再出现 `RESV-WALK start=1112,130`、`CPL-PAIR dist` 缩到命中并出现 `COUPLE-OK loco=23`）。

### 23.6 构建自证（已回填，2026-10-11 13:20）

- `build\R3R_incbuild.guard.log`（13:17:34）= `GUARD: incremental is safe (no header/lang file is newer than the newest object)` ⇒ 合法增量（`src\pbs.h` 停在 12:12:16，未改头文件）。
- 日志：`[2/3] Building CXX object ...\src\pbs.cpp.obj` → `[3/3] Linking CXX executable openttd.exe`（Linking 1 次）。
- `build\R3R_incbuild.done`（13:20:15）= `EXIT_CODE=0`；`error C*/fatal error/FAILED:/build stopped` 计数 **0**（日志全长 1037 B）。
- 时间戳链：`src\pbs.cpp` 12:58:38 → `pbs.cpp.obj` **13:18:28**（3 049 439 B）→ `build\openttd.exe` **13:20:06**（51 843 072 B）。三环单调，**新的 exe 才第一次含 KI-361 层①的账本优先**。
- 状态：已实现 + **已编译**，游戏内复测待做（判据见 §23.5 第 3 条 / KI-361）。
（本轮按"少释放不误释放"设计，需确认是否有反向情形）。

## 24. 第 243 轮续二十一（2026-10-11）：**落盘（防上下文压缩）+ 本轮新取证：代码锚点 / 疑点 / 猜测根因**

> 起因：玩家要求「先把已知信息落盘，防止上下文神秘压缩」。本节**只落盘**——本轮零源码改动、零构建，全部为只读取证。

### 24.1 已确证代码事实（行号可直接引用）

| # | 事实 | 位置 | 内容 |
|---|---|---|---|
| 1 | 信号位表 | `src/table/signal_data.h:12-26` | `_signal_along_trackdir`（13 项）= `0x8,0x8,0x8,0x2,0x4,0x1,0,0,0x4,0x4,0x4,0x1,0x8,0x2`；`_signal_against_trackdir` 为其反向；`_signal_on_track` = `0xC,0xC,0xC,0x3,0xC,0x3`（X/Y/UPPER/LEFT 共用 `0xC`，LOWER/RIGHT 用 `0x3`） |
| 2 | 红灯判定 | `src/train_cmd.cpp:16018-16022`、`:16069` | `ts = GetTileTrackStatus(gp.new_tile, TRANSPORT_RAIL, 0, ReverseDiagDir(enterdir))`；`trackdirbits = ts.trackdirs & reachable_trackdirs`；`red_signals = TrackdirBitsToTrackBits(ts.signals & reachable_trackdirs)`；判据 = `(red_signals & chosen_track) && first->force_proceed == TFP_NONE` |
| 3 | 置绿唯一入口 | `src/pbs.cpp` `TryReserveRailTrackdir` | `success = TryReserveRailTrack(tile, TrackdirToTrack(td), trigger_stations); if (success && HasPbsSignalOnTrackdir(tile, td)) SetSignalStateByTrackdir(tile, td, SignalState::Green);` ⇒ **只有该 Track 位"首次"预留成功时才置绿**；位已被任何来源订下 ⇒ `TryReserveRailTrack` 返回 false ⇒ 该信号**永远拿不到 Green** |
| 4 | 耦合订路的写入口 | `src/pathfinder/yapf/yapf_rail.cpp` 的 `rp()` 内 `reserve_if_present` | 只调 `TryReserveRailTrack(t, trk)`（**Track 级**）⇒ 若某格的首笔来自这里，该格的信号不会被点亮 |
| 5 | 耦合路径的第二次预留 | `src/pathfinder/yapf/yapf_rail.cpp:1123-1134` | 签名 `bool TryReservePath(PBSTileInfo *target, TileIndex origin, bool unsafe_pos = false)`（**第二参是 origin**）+ `this->origin_tile = origin;`；调用处传 `pNode->GetLastTile()`，而 `pNode` 在回溯 while 循环后是 **root 节点**（机车起点节点）⇒ 见 REG-243-F |

### 24.2 疑点（一发现即登记）

- **REG-243-D（探针读数错位，直接威胁"信号类型"判定）**：`src/train_cmd.cpp:16102-16108` 的 REDSIG 子循环写成
  ```cpp
  for (uint k = 0; k < 6; k++) {
      const Track t = (Track)k;
      if (!HasBit(GetPresentSignals(gp.new_tile), t)) continue;
      ... GetSignalType(gp.new_tile, t) ...
  }
  ```
  它把 `k` **同时**当「m3/m4 里的信号存储位索引」与「Track 枚举值」用，但两者不是一回事（`_signal_on_track[TRACK_LOWER=3] = 0x3`，而 `present=0x8` 是 bit3，属 X/Y/UPPER/LEFT 的 `0xC` 家族）⇒ 现场那行 `REDSIG     track=3 type=0 oneway=1` **既读了错的槽、又标了错的 track 名**，**不能**据此断言 1112,129 是 BLOCK 还是 PBS。
  - 修法（未做，只读探针）：改遍历 `for (Track t = TRACK_BEGIN; t < TRACK_END; ++t) if (HasSignalOnTrack(tile, t)) { ... }`。
- **REG-243-E（日志无法区分"置绿资格"，层②/③的关键判据缺口）**：`RESV-WATCH SET-RAIL` 由六个 setter 统一打出，**无法区分**这笔来自「`rp()` 的 Track 级直调」还是「`TryReserveRailTrackdir` 内部调用」。这正是判定 `6538 SET-RAIL tile=1112,130 track=1 phase=yapf-couple actor=23` 是否具备置绿资格的唯一缺口。
- **REG-243-F（`TryReservePath` 第二参语义未核实）**：见 24.1 第 5 行。若该调用确以"机车起点节点"为 origin，则耦合订下的 1112,128..131 **全程没走过 Trackdir 级入口**，"信号没被点亮"就有了解释；反之该解释不成立。**待读 `TryReservePath` 实现全文核实（不构建）**。
- **REG-243-G（owner/near 分离本身仍需独立解释）**：`REDSIG beyond track=1 owner=23 near=22` 中 owner=账本(23)、near=走查(22)。KI-361 已解释"哪个更该信"，但"位是 23 写的、走查首个撞到的却是 22"这一现象（22 是否在自己的预留路径上恰好压过这格）仍**未见独立证据**。

### 24.3 猜测根因（待验证，勿当结论）

- **猜测①（自阻塞：Track 级首写吞掉置绿机会）**：1112,128..131 的**首次**写入来自耦合寻路 `rp()` 的 `reserve_if_present`（Track 级）；随后机车自己的 Trackdir 级预留尝试订同一批位时，`TryReserveRailTrack` 因"已被订"返回 false ⇒ 这四格的 PBS 信号从未 `SetSignalStateByTrackdir(Green)` ⇒ 机车 23 被**自己订下的预留**挡在 1112,129 的红灯前。
  - 支撑：事实 3 的 `success && ...` 短路 + 事实 4（`rp()` 只用 Track 级）+ §23.3（`1112,130` 全日志 3 行，首笔 6538 即 `phase=yapf-couple`）。
  - 待验证判据：加一条"`TryReserveRailTrackdir` 里 `success==false` 且该格该 trackdir 存在 PBS 信号"的探针（即 REG-243-E 的修法），复跑看是否在 1112,129 命中。
- **猜测②（若 1112,129 其实是块信号）**：块信号红/绿由 segment 占用维护，与预留位无关 ⇒ 需要完全不同的解释链。**当前不能排除**，因为 REG-243-D 使 `type` 读数不可信 ⇒ **先修探针（REG-243-D）再判定**。

### 24.4 下一轮优先序（建议）

1. 修 REG-243-D（只读探针，几行），复跑拿到 1112,129 的真实 `type/oneway` ⇒ 判别猜测①②。
2. 读 `TryReservePath`（`yapf_rail.cpp` 内、`CYapfReserveTrack*`）实现全文，核实 REG-243-F（不构建）。
3. 判别清楚后再决定是否加 REG-243-E 的探针（会进 `pbs.cpp`，需增量构建）。

### 24.5 状态

- 本节（第 243 轮续二十一）**零源码改动、零构建**，纯落盘。
- 未变：KI-360 已实现 + 已编译；KI-361 已实现 + 已编译（`build\openttd.exe` 2026-10-11 13:20:06，51 843 072 B），游戏内复测待做。

## 25. 第 243 轮续二十二（2026-10-11）：核实 REG-243-F 与由此浮现的 REG-243-H（只读取证，零改动、零构建）

### 25.1 REG-243-F 核实结论：原推断**证伪**

`bool TryReservePath(PBSTileInfo *target, TileIndex origin, bool unsafe_pos)`（`src\pathfinder\yapf\yapf_rail.cpp:438-560`）里 `origin` **只有一个用途**：

- `:444` `this->origin_tile = origin;`
- 唯一读取点：`ReserveRailStationPlatform()`（`:317` 一带，站台整格预留）的循环边界 `while (IsCompatibleTrainStationTile(tile, start) && tile != this->origin_tile)`。

而**预留范围**由 `res_dest_node` 决定：`:473` `for (Node *node = this->res_dest_node; node->parent != nullptr; node = node->parent)`。

⇒ 24.1 事实 5 / REG-243-F 的原推断「调用处传 root 节点 ⇒ 耦合订下的四格全程没走过 Trackdir 级入口」**不成立** —— `origin` 与"走哪个入口"无关，它只影响站台预留的停止边界。

### 25.2 顺带纠正一个笼统说法：`ReserveSingleTrack` 其实**是** Trackdir 级

- `ReserveSingleTrack()`（`:332-356`）：普通轨道走 `TryReserveRailTrackdir(Yapf().GetVehicle(), tile, td)`（`:341`）—— **Trackdir 级、有置绿资格**；只有站台走 `ReserveRailStationPlatform()`（整格位）。
- ⇒ "R3R 的 `rp()` 没有置绿资格"**不能笼统地说**，必须看**具体那一笔**走的是哪条路径。这一点直接影响 REG-243-E 的判读口径。

### 25.3 新疑点 REG-243-H（比 F 要紧；耦合寻路的 Trackdir 级预留疑似**空转**）

- 位置：`class CYapfFollowCoupleRailT`（`:752`，**耦合寻路专用**；普通列车用 `CYapfFollowRailT::ChooseRailTrack` `:1301`，不受影响）内 `ChooseRailTrack` 的 `rp()` 分支（`:937-1117`，进入条件 `pNode->parent == nullptr && pPrev != nullptr`，即 while 回退走完后 `pNode` 落到 **root**）。
- 该分支结尾（`:1113-1116`）：
  ```cpp
  rp(st, best_td, 0);
  if (rp_last_tile != INVALID_TILE) {
      this->SetReservationTarget(pNode, rp_last_tile, pNode->GetLastTrackdir());  // pNode == root!
  }
  ```
  **把 `res_dest_node` 覆盖成 root 节点**（此时 `root->parent == nullptr`）。
- 紧接着 `:1124` `this->TryReservePath(nullptr, pNode->GetLastTile(), true)`：进入 `:473` 循环，首轮 `node = res_dest_node (= root)`，`root->parent == nullptr` ⇒ **循环体一次都不执行** ⇒ 节点遍历型（Trackdir 级）预留**全部空转**。
- 该分支自己的注释（`:940-943`）也承认这些格 "are not part of any node segment, so TryReservePath does not cover them"，因此用 `rp()` 内 `reserve_if_present(tile, track)`（**Track 级** `TryReserveRailTrack`）补订。
- **推论**：耦合寻路实际落位的位，若 `rp()` 订到了东西，就**只来自 Track 级补订**（无置绿资格）；`TryReservePath` 因 `res_dest_node` 被重置为 root 而恒空转，其返回值**恒为 true**（`:455` 因 `unsafe_pos=true` 短路、不检查 `IsWaitingPositionFree`；`:540` deferred_test 未设置；`:554` target==nullptr 跳过；`:560` return true）⇒ `CPL-RESERVE ... reserved=1` **不反映真实预留结果**（又一处读性陷阱）。
- **支撑（既有取证）**：§23.3 —— `tile=1112,130` 全日志仅 3 行 `RESV-WATCH`，首笔 6538 即 `phase=yapf-couple actor=23`，此后零释放。
- **与普通寻路不矛盾**：该 `rp()` 分支与 `consist_head_tile` 均为耦合寻路专有，普通 PBS 预留（`CYapfFollowRailT`）不受影响。

### 25.4 猜测① 状态更新（支撑加强，仍未定案）

- 支撑由「事实 3 + 事实 4（`rp()` 只用 Track 级）」**升级为**「REG-243-H：Trackdir 级遍历被 `res_dest_node = root` 短路，耦合寻路的位实际只由 Track 级补订落位 ⇒ 无置绿资格」。
- 仍**必须**先修 REG-243-D 拿到 1112,129 的真实信号类型，才能排除猜测②（若实为块信号，则与预留位无关）。

### 25.5 待验证判据

- REG-243-H 的**直接**验证需在 `TryReservePath` 入口或 `:473` 循环打只读探针（改 `yapf_rail.cpp` —— 该文件被多个 TU include，重建成本高；**本轮未做**）。
- 可先用现有日志**间接**判读：耦合那批格是否**只有** `rp()` 补订的少数格落位、且整段 node 路径上无任何 Trackdir 级候选痕迹。
- REG-243-D 的修法（已确证）：`GetPresentSignals` 只有 4 位（`GB(_m[tile].m3, 4, 4)`），而 `_signal_on_track` 对 X/Y/UPPER/LEFT 共用 `0xC`、对 LOWER/RIGHT 用 `0x3` ⇒ 原循环 `for (uint k = 0; k < 6; k++) HasBit(GetPresentSignals(...), k)` **既越界（k=4,5 恒假）又位/轨不同构**。正确修法 = 只在该格**实际存在的轨**上判信号：
  ```cpp
  const TrackBits present_tracks = GetTrackBits(gp.new_tile);   // 已在 if (new_plain) 内，GetTrackBits 断言安全
  for (Track t = TRACK_BEGIN; t < TRACK_END; ++t) {
      if (!HasBit(present_tracks, t)) continue;                 // 先限定该格物理存在的轨
      if (!HasSignalOnTrack(gp.new_tile, t)) continue;          // 再判该轨是否有信号
      ... GetSignalType(gp.new_tile, t) / IsOnewaySignal(gp.new_tile, t) ...
  }
  ```

### 25.6 状态

- 本节**零源码改动、零构建**，纯只读取证。
- 下一步：按 25.5 修 REG-243-D（`src\train_cmd.cpp`，只读探针、增量合规）+ 增量构建 + 请玩家复跑取数。

---

## 26. 第 243 轮续二十三（2026-10-11 复测）：新 exe 仍红灯 —— 账本优先已生效，但置绿资格不受账本影响（**实锤，非猜测**）

### 26.1 采样有效性

- 玩家原话「我在新的 exe 中发现原先的（刚才的）问题还没有解决」。
- 生产链（时间戳互证）：`src\train_cmd.cpp` 14:16:03（REG-243-D 探针修法，玩家自行重建）→ `build\R3R_incbuild.done` 14:25:19 = `EXIT_CODE=0` → `build\openttd.exe` 14:25:11（51 843 072 B）→ `build\R3R_debug.log` 14:30:07（519 223 B / 7 364 行）。⇒ 采样有效，日志确由含账本 + REG-243-D 的新 exe 产生。

### 26.2 三条判据（直接读新日志）

1. **账本优先已生效**：`REDSIG beyond track=1 owner=23 near=22`（≈7288 / 7340 两处）⇒ `owner` 字段走账本、答 **veh23（= 自己）**，`near=22` 走查答 veh22；并且 **全日志 `RESV-WALK` = 0 行** ⇒ 续二十「账本短路走查」的断言成立（`pbs.cpp:1873` 一带 `GetTrainForReservation()` 首查账本）。
2. **1112,129 是 PBS 单向信号**：`REDSIG track=1 type=5 oneway=1 pbs=1` ⇒ **猜测②（块信号）排除**；REG-243-D 的探针修法（按"该格实际存在的轨"判信号）生效。
3. **该信号未置绿**：`REDSIG new present=0x8 states=0x7 resv=0x2` ⇒ 1112,129 的信号槽 = **bit3**（`present=0x8`），而 `states` 的 bit3 = 0 ⇒ 该 PBS 信号恒红；同行块 `red=0x16` 含 `chosen=0x2` ⇒ 红灯命中，veh23 停在 1112,128（`spd` 15 → 2 → 0）。

### 26.3 实锤链（REG-243-H 由"疑似"转"成立"，猜测①定案）

- 1112,128 / 129 / 130 / 131 四格由耦合寻路 `rp()` 以 **Track 级** `TryReserveRailTrack` 补订（`SET-RAIL ... phase=yapf-couple actor=23`，行号见 §19.3）；`rp()` 自身注释也承认这些格 "are not part of any node segment" ⇒ **无置绿资格**。
- `pbs.cpp:457-467`：plain rail 分支走 `TryReserveTrack(tile, track)`，语义是「该位原本未订才返回 true」⇒ **已被订（不论所有者）⇒ false**。
- `pbs.cpp:430-442`（修前）：置绿的唯一条件是 `success && HasPbsSignalOnTrackdir(...)`，`success` 即上述返回值 ⇒ 列车推进到 1112,128、在 1112,129 上做 **Trackdir 级**预留时，`TryReserveRailTrack` 因「位已被自己订下」返回 false ⇒ **置绿分支永不执行** ⇒ 1129 的 PBS 信号恒红 ⇒ **机车被自己订的位锁死**（`COUPLE-FAIL` 每 tick 重试，车辆 `spd` 归零后停在 1112,125）。
- 对照（同日志）：`phase=choose-track` 那批是 Trackdir 级 ⇒ 被 controller 正常释放；`COUPLE-OK loco=23 rear=24`（≈273 行）说明耦合机制本身可用 ⇒ 问题只在 `rp()` 补订的这四格。

### 26.4 修法（**已落地**，`src\pbs.cpp`，未碰 `src\*.h`）

`TryReserveRailTrackdir()`：Track 级重订失败后，若该位**已由本列车所在链持有**（账本 `R3RTrainForLedgerBit(tile, track)->index == v->First()->index`，且 `HasBit(GetRailReservationTrackBits(tile), track)` 复核位仍在），则视为预留成功 ⇒（a）取得置绿资格（消除自锁红灯），（b）被调用方记入 `path` ⇒ 将来随路径释放，顺带清掉 §19 的孤儿位。仅 `IsPlainRail` 特判 + `v != nullptr` 守卫，其它 tile 类型与 `success == true` 的原路径逐字不变。

### 26.5 状态与待复测

- 源码：`src\pbs.cpp`（本节）+ `src\train_cmd.cpp`（REG-243-D 探针，玩家已重建并生效）。构建：见 KI「第 243 轮续二十三」小节。
- **构建自证（本节，2026-10-11 14:53）**：复用既有 `_tmp_inc_build.cmd`（未新建任何 `.cmd`）；构建前 `tasklist` 确认 `openttd.exe` 未运行；护栏 `build\R3R_incbuild.guard.log` = `GUARD: incremental is safe (no header/lang file is newer than the newest object)`（本轮只改 `.cpp`、未碰 `src\*.h` ⇒ 增量合法）；`build\R3R_incbuild.log` 尾 = `[2/3] Building CXX object CMakeFiles\openttd_lib.dir\src\pbs.cpp.obj` → `[3/3] Linking CXX executable openttd.exe`；`build\R3R_incbuild.done` = `EXIT_CODE=0`（14:54:03）；日志中 `error C*` / `fatal error` / `FAILED:` / `build stopped` 计数 **0**；时间戳链 `src\pbs.cpp` 14:46:58 → `pbs.cpp.obj` 14:51:50（3 049 787 B，较续二十的 3 049 439 B **+348 B**，证明新分支确已编入）→ `build\openttd.exe` 14:53:50（51 843 072 B）。本轮**未新增日志字符串**（纯控制流修复），故不做 exe 字面量核对；exe 总大小与续二十版相同（同为 51 843 072 B）系节对齐 padding 吸收，判据以 obj 大小差 + 时间戳链 + 退出码为准。状态＝已实现 + 已编译，游戏内复测待做。
- 复测判据 4 条：① 1112,129 的 `states` 出现 bit3（≥ `0x8`）、veh23 通过该格并出现 `COUPLE-OK loco=23`；② `CLEAR-RAIL tile=1112,128..131` 出现（四格不再成孤儿）；③ 负判据：他车真预留仍红、对向列车照旧被挡、块信号行为不变；④ 普通单轨直行线路 `RESV-AUDIT` 的 owned 计数不变，且无 `rail_map.h:168` / `pbs.cpp` 断言。

---

## 27. 第 243 轮续二十四（2026-10-11）：推理过程存档 + 疑点/猜测根因状态表（纯落盘，零源码改动、零构建）

> 本节**不是**新结论，而是把 §25~§26 之间那条长推理链、以及尚未落盘的**疑点**与**猜测根因**逐条固化，遵 2026-10-11 追加的两条即时落盘规则（「有疑点」「猜测的根因」）——防止长链推理的中间结论被上下文压缩后重复取证。

### 27.1 推理链（逐步存档）

1. **起点（§26.2 判据②）**：1112,129 `REDSIG track=1 type=5 oneway=1 pbs=1` ⇒ 它是 **PBS 单向信号**；同行 `new present=0x8 states=0x7 resv=0x2` ⇒ 信号槽 = **bit3**（`present=0x8`）、而 `states` 的 bit3 = 0 ⇒ **该信号恒红**。⇒ 猜测②（块信号，与预留位无关）**排除**。
2. **分界（§26.2 判据①）**：同一份日志 `REDSIG beyond track=1 owner=23 near=22`（`owner` 走账本、答 **veh23 自己**；`near=22` 走查答 veh22），且全日志 `RESV-WALK` = 0 行 ⇒ **账本优先已生效、归属已答对**。⇒ 关键切分：**「归属判定」与「置绿资格」是两件独立的事**——账本修好也不会让信号变绿（这正是 §26 标题的含义，也是续十九「账本落地但现象依旧」的真正原因）。
3. **定位置绿资格**：修前 `pbs.cpp:430-442` 置绿的唯一条件是 `success && HasPbsSignalOnTrackdir(...)`；其中 `success` 来自 `TryReserveRailTrackdir()` 内部对 plain rail 走的 `TryReserveTrack(tile, track)`，其语义是「**该位原本未订才返回 true**」⇒ **已被订（不论所有者是谁）⇒ false**。
4. **为什么会被自己先订下**：1112,128 / 129 / 130 / 131 由**耦合寻路**的 `rp()`（`yapf_rail.cpp`）以 **Track 级** `TryReserveRailTrack` 补订（`SET-RAIL ... phase=yapf-couple actor=23`，行号见 §19.3）⇒ 先有 Track 级位，等列车推进到该格再做 **Trackdir 级**重订时，`TryReserveTrack` **必返 false**。
5. **补强（REG-243-H，§25.3）**：耦合寻路 `ChooseRailTrack` 的 `rp()` 分支把 `res_dest_node` 覆盖成 **root**（此时 `root->parent == nullptr`），使紧随其后的 `TryReservePath()`（`:473`）循环首轮即不满足 ⇒ **Trackdir 级遍历整体空转**、返回值恒 true。⇒ 耦合寻路实际落位的位**只**来自 Track 级补订（无置绿资格）；`rp()` 自己的注释也承认这些格 "are not part of any node segment"。
6. **自锁**：自己订的 Track 级位 ⇒ 自己拿不到置绿资格 ⇒ 自己面前的 PBS 信号恒红 ⇒ 机车停在 1112,125 每 tick `COUPLE-FAIL` 重试，永不自解。
7. **修法（§26.4，已落地 `src\pbs.cpp`）**：在 `TryReserveRailTrackdir()` 里，Track 级失败后若该位**已由本列车所在链持有**（账本 `R3RTrainForLedgerBit(tile, track)->index == v->First()->index`，且 `HasBit(GetRailReservationTrackBits(tile), track)` 复核位仍在）⇒ 视为预留成功。
8. **落地与构建**：见 §26.5（`pbs.cpp.obj` 较上一版 **+348 B**、`build\openttd.exe` 2026-10-11 14:53:50、`EXIT_CODE=0`）。

### 27.2 猜测根因状态表

| 编号 | 猜测根因 | 状态 | 依据 |
|---|---|---|---|
| 猜测① | 耦合寻路 `rp()` 只做 Track 级补订 ⇒ 该位无置绿资格 ⇒ **自锁红灯** | **成立（实锤）** | §26.3 |
| 猜测② | 1112,129 是块信号（与预留位无关） | **证伪/排除** | §26.2 判据② |
| REG-243-F | 调用处传 root 节点 ⇒ 四格全程没走过 Trackdir 级入口 | **证伪** | §25.1 |
| REG-243-H | 耦合寻路 `res_dest_node = root` ⇒ `TryReservePath` 的 Trackdir 级遍历空转 | **成立** | §25.3 + §26.3 |

### 27.3 疑点登记（未决）

- **REG-243-A（低，仍未决）**：整段会话 `RESV-AUDIT-SUMMARY` 只出现 **1 条**（读档后第一 tick），而按 2048 tick 节拍本应有多条。
  - 期望：稳态每 2048 个未暂停 tick 一条 SUMMARY。
  - 实测：全日志 1 条。
  - 存疑：无法区分「`_tick_counter` 在暂停时不前进」与「节流条件写错」——因为**该行不打 `tick=`**。
  - 还需：给 SUMMARY 加 `tick=`（**未做**）。
- **REG-243-I（本节新增，中，读性陷阱）**：REG-243-H 使 `TryReservePath()` 返回值**恒 true** ⇒ 该分支结尾的 `CPL-RESERVE ... reserved=1` **不反映真实预留结果**。
  - 存疑：凡历史上以 `reserved=` 为判据的结论/判读口径都需复核。
  - 还需：列出受影响的探针字段与既有结论清单（**未做**）。
- **U-3（KI-346 家族，未验证）**：`rp()` 平台段的 `plat_track = AxisToTrack(axis)` 是否与 KI-346 同族（订到车根本不走的轨）——**未取数、未改**。

### 27.4 方案层未决（待拍板）

- **层②生命周期钩子清单**：耦合 / 解挂 / 车库编辑 / 卖车 / 读档 各提交点统一通知账本（现为「层①账本 + 层③审计兜底」）。
- **方案 C：显式 owner 表**（替代「账本 + 走查回退」）。

### 27.5 未完成清单

1. **游戏内复测**（需玩家）：按 §26.5 四条判据核「1112,129 置绿 + veh23 通过」。
2. **REG-243-A 修法**：`RESV-AUDIT-SUMMARY` 加 `tick=`（只读日志，需增量构建）。
3. **U-3 复核**：`rp()` 的 `plat_track`。
4. **REG-243-I 影响面复核**：受 `reserved=` 影响的探针/结论清单。

### 27.6 状态

- 本节**零源码改动、零构建**，纯推理过程与疑点/猜测根因落盘。
- KI 同步：`R3R_KNOWN_ISSUES.md` 的「第 243 轮续二十四」小节。

---

## 28. 第 243 轮续二十五（2026-10-11）：15:08 复测日志判读 —— **换现场了**，`1112,129` 不出现；新现场 = 1091,139「无预留红灯」把 veh23 钉死在站台

> 本节为**接受玩家报告后的第一时间落盘**（遵 2026-10-11 规则：先写备忘再动工）。本节零源码改动、零构建。

### 28.1 采样有效性

- 生产链：`src\pbs.cpp` 14:46:58（续二十四的自锁修复）→ `build\openttd.exe` **14:53:50**（51 843 072 B）→ `build\R3R_debug.log` **15:08:23**（160 539 B / **1 869 行**）。
- ⇒ 本日志**确由含 §26.4 自锁修复的新 exe 产生**，是对续二十四的**第一次实机复测**。

### 28.2 最重要的判读：**场景与 §26.5 判据不匹配（口径纠正）**

- 全日志检索结果：**`1112,129` / `1112,128..131` 零命中**；`RESV-WALK` **0 行**；`CLEAR-RAIL tile=1112` **0 行**。
- ⇒ 本条日志**没有走到耦合场景**，因此 §26.5 的四条判据（①1112,129 出现 bit3/veh23 通过 ②`CLEAR-RAIL tile=1112,128..131` ③负判据 ④`RESV-AUDIT` 计数不变）**在本日志上不适用，不能据此判定修复有效或无效**。
- 现象换成了**另一处红灯**：veh=23 停在 `1090,139`，被 `1091,139` 的 PBS 信号挡住。

### 28.3 新现场时间线（行号锚点，全部实测）

| 行 | 内容 | 判读 |
|---|---|---|
| 1545 | `DECOUPLE-FIRE consist=23 tx=1089 ty=139 real=3 mode=0 num=0 segs=1 eff=22 plat=16/0` | veh23 链在 1089,139 解挂 |
| 1551-1558 | `RESV-GC veh=23 tile=1082..1089,139 track=stn kind=STATION why=decouple-skip` | **§125/126 轮的站台整格 GC 生效**（真释放了 8 格） |
| 1559 | `RESV-GC veh=23 tile=1090,139 track=0x0 why=decouple` | 自身格 track 级位也放掉 |
| 1606 | `LOCO-AFTER-DECOUPLE veh=23 curType=0 real=1 idxnext=1 tile=1089,139` | 机车保留 `real=1`（type=1 GOTO_STATION dest=13） |
| 1836 | `SPD-DROP veh=23 tile=1090,139 prev=15 order=1 real=1 stuck=1 fp=0 prog=8 ... dtile=175173` | 速度 15→0，停在 1090,139 |
| 1831-1835 | `REDSIG veh=23 old=1090,139 new=1091,139 ...` | 见 28.4 |
| 1839-1869 | `TPR-WALK veh=23 ...` + `TRP veh=23 ... => ok=0 res=0` **每 tick 成对刷** | 永不自解 |

### 28.4 红灯字段判读（`1091,139`）

```
REDSIG veh=23 old=1090,139 new=1091,139 dir=2 chosen=0x1 i=8 prog=0 spd=0 wait=0 stuck=1
REDSIG   tdbits=0x100 reach=0x520 sigs=0x710 red=0x5 bits=0x1
REDSIG   new present=0x4 states=0xB resv=0x0 veh=0
REDSIG     track=0 type=4 oneway=0 pbs=1
REDSIG   beyond=1092,139 valid=1 plain=1 resv=0x0 veh=0
```

1. `type=4` ⇒ 该信号是 **Combo（块+PBS 复合）信号**；`pbs=1`、`oneway=0`、`track=0`（TRACK_X，东西向）。
2. `present=0x4`（槽 = bit2）而 `states=0xB` 的 **bit2 = 0** ⇒ **该信号当前恒红**（第 2 槽红）。
3. **最关键的差异**：`new resv=0x0` **且** `beyond resv=0x0 veh=0` ⇒ **本格与前方格都既无预留也无车**。这与 §19/§26 的 `1112,129` 现场（`resv=0x2` 有自己订下的孤儿位）**性质完全不同**。
4. ⇒ §26.3 的"自锁"链条（自己被 `rp()` 订下的位挡住置绿）**在此处不成立**：这里根本没有位可挡。

### 28.5 veh=23 恒 `ok=0`（新疑点 REG-243-J）

```
TPR-WALK veh=23 mf=1090,139 origin=1090,139 td=8 okay=1 exit=2 new=1091,139 reach=1 spd=0 one=0 look=0
TRP veh=23 mf=1090,139 real=1 type=1 dest=175173 spd=0 fp=0 look=0 one=0 mstuck=0 fto=0 => ok=0 res=0
TRP veh=23 mf=1090,139 real=1 type=1 dest=169028 spd=0 fp=0 look=0 one=0 mstuck=0 fto=0 => ok=0 res=0
```

- `origin=1090,139` **== mf 自身格** ⇒ 它自己的预留链**已在自身格终止**，前方没有任何延伸预留；`okay=1` 表示这份"只到脚下"的预留自洽。
- `ok=0 res=0` = `TryPathReserve` 返回 `TPRRF_NONE`（无任何结果位），**每 tick 重复**，`mstuck` 恒 0、`fp=0`。
- **对照组（同日志，证明机制可用）**：`1827 TRP veh=11 ... look=1 one=1 mstuck=1 fto=0 => ok=1 res=1`、`1854 TRP veh=2 ... look=1 one=1 mstuck=1 fto=1 => ok=1 res=1`；且 veh=11 走过 `1787 R3R-RES-ONCE grant` → `1824 TPR-WALK ... one=1 look=1` → `1825 used` → `1826 clear` 全流程。
- ⇒ **可复核的差异只有一个**：成功的两例 `look=1 / one=1`，veh=23 是 `look=0 / one=0`（`one` = `ForceReserveOnce` 是否挂起；`look` = 调用处是否携带 lookahead 语义）。
- `dest` 在 `175173`(=(1093,85)) 与 `169028`(=(1092,82)) 之间**跳变**（同一 tick 两次调用取值不同），`DEPOT-ARR` 行显示 `destTx=1093 destTy=85` ⇒ 目的地是**北方**（y≈82~85），而列车正在 **y=139 向东**走，即"目的地不在当前朝向的前方"。

**REG-243-J（疑点，中）**：veh=23 停在站台格 `1090,139`、`cur_speed==0`、红灯在前，`TryPathReserve` 每 tick 被调用但恒返 `TPRRF_NONE`，导致永远拿不到置绿资格 ⇒ 机车钉死。
- 期望：站台格上的机车在红灯前应能预留出站路径并把信号置绿。
- 实测：恒 `NONE`；`origin==自身格`（无向前预留）。
- 存疑：① `look=0` 是否等价于"lookahead 未建立 ⇒ 跳过预留主体"（须查 `TryPathReserve`/`R3RTryPathReserveCore` 的 lookahead 分支）；② `dest` 跳变是否代表"目的地不可达/顺序走查反复重算"；③ `1091,139` 是 **Combo(type=4)** 信号，其置绿是否要求"下一 block 空闲"而不只是预留（若如此，则与 §26 的 PBS 单向信号是**不同族**的问题）。
- 还需：`TryPathReserve` 早退分支的判据清单（尤其 `look=0` 路径），以及 `dest_tile` 的写入者。

### 28.6 归类与状态

- 本条**属 KI-347/KI-348 家族的第 5 个实例**（机车停在格上、`cur_speed==0`、预留恒失败、永不自解），但**成因待定**：既非 1112,129 那条"自锁"，也尚无证据指向库内/非 PBS 段/WAIT_COUPLE 三条旧路径。
- **续二十四的自锁修复：状态维持"已实现+已编译，游戏内复测待做"**——本条日志未触发该修复路径，既不能确认也不能排除（复测必须跑耦合场景）。
- 本轮（续二十五）**零源码改动、零构建**。

### 28.7 未完成清单（更新自 §27.5）

1. **游戏内复测（原判据）**：仍需跑**耦合场景**核 §26.5 四条判据（本条日志未覆盖）。
2. **REG-243-J 取证**：查 `TryPathReserve`/`R3RTryPathReserveCore` 中 `look`/`one` 对应的分支，定位 veh=23 为何恒 `NONE`；必要时加只读探针（记录命中的早退分支名）。
3. **`dest_tile` 跳变**：定位 `175173↔169028` 的写入者。
4. **KI-347/348 家族合并评估**：5 个实例是否同一根因（"速度归零后不再进入唯一预留点"）。
5. REG-243-A（`RESV-AUDIT-SUMMARY` 加 `tick=`）、U-3（`rp()` 的 `plat_track`）、REG-243-I（`reserved=` 影响面）仍挂起。

---

## 29. 第 243 轮续二十六（2026-10-14 21:53 新日志）：玩家直觉实锤 —— 账本式清理自己养出了**三条地鼠**（KI-362 / KI-363 / KI-364）

> 玩家原话：「你似乎为了事后消灭幽灵/孤儿预留，引入了一些地鼠，而我们可能在打地鼠」。
> 本轮**只读取证、零源码改动、零构建**。
> 日志 = `D:\temp\codebuddy-dropped-files\b0d56da7-bb3d-4937-b28b-1e0eca97b24c\R3R_debug.log`（160 653 B / **1 876 行**，mtime 2026-10-14 21:53:24）。

### 29.1 采样有效性（先堵住"exe 陈旧"这个旧坑）

| 对象 | mtime | 说明 |
|---|---|---|
| `src\train_cmd.cpp` | 2026-10-11 14:16 | 含 KI-360 层②③ |
| `train_cmd.cpp.obj` | 2026-10-11 14:23 | 晚于源码 |
| `src\pbs.cpp` | 2026-10-11 14:46:58 | 含 KI-361 层①（`GetTrainForReservation` 账本优先） |
| `pbs.cpp.obj` | 2026-10-11 14:51 | 晚于源码 |
| `build\openttd.exe` | **2026-10-11 14:53:50（51 843 072 B）** | 晚于全部 obj |
| 本日志 | 2026-10-14 21:53:24 | 晚于 exe 三天，其间 exe 未再变动 |

⇒ 本日志由**含 KI-360 + KI-361 层①**的 exe 产生，**采样有效**（不是续十九那种陈旧 exe 事故，不必作废）。

### 29.2 一句话结论

**是地鼠，而且不止一只。** 为"事后消灭幽灵/孤儿预留"引入的三套机制各养出一类地鼠：

| # | 地鼠 | 一句话 | 证据强度 |
|---|---|---|---|
| **A** | **整本抹账 ⇒ 订-放循环** | `FreeTrainTrackReservation` 的 `R3RResvLedgerForget(key,"free-path")` 把该链头**名下全部**账本位一次放光（不是只放这条 path 覆盖的位），于是同一批格每 tick「订→放→订→放」 | **实锤**（同格逐字重复 8 次 + veh=11 整条 9 格路径反复放） |
| **B** | **账本虚记（正负不同步）** | 32 条 `-skip` 释放证明**账本里记着地图上没有的站台整格位** ⇒ 账本比地图"多"；整格重建绕过 setter 是已知边界但会造成永久漂移 | **实锤**（`-skip` 32 条，全部站台整格） |
| **C** | **存量孤儿只能等时延 reap** | 20 格存档带入的孤儿位（`lowner=-1`）本会话 `reaped=0`，靠 3×2048 tick 的 age 计数才清，玩家观感"清了又没清" | **实锤**（`SUMMARY ... reaped=0 pendingStray=20`） |

### 29.3 地鼠 A：整本抹账 + 无条件释放（最要紧）

**A-1 计数（84% 的 RESV-GC 都是它）**

```
RESV-GC 总计                 216
  why=free-path             180   ← 新引入的大头
  why=free-path-skip          2
  why=holder-gone-skip       16
  why=decouple                2
  why=decouple-skip          16
  why=acc-car                 3
  why=moved                   0   ← 老账本（KI-203）一条都没打
```

对比第 110 轮（KI-203 落地时）`why` 只有 `moved` / `holder-gone` ⇒ **`free-path` 是账本式清理新引入的**，且一上来就是绝对主体。

**A-2 逐字重复：同一对格被放了 8 次（veh=2）**

```
431: RESV-GC veh=2 tile=1074,139 track=0x0 why=free-path
432: RESV-GC veh=2 tile=1075,139 track=0x0 why=free-path
463: RESV-GC veh=2 tile=1074,139 track=0x0 why=free-path
464: RESV-GC veh=2 tile=1075,139 track=0x0 why=free-path
485: …486:  （同两格）
524: …525:  （同两格）
547: …548:  （同两格）
569: …570:  （同两格）
591: …592:  （同两格）
630: …631:  （同两格）
```

⇒ 8 组，**逐字相同、全部无 `-skip`**（即每一次都真的把地图位放掉了）。
上下文（行 408-447）显示 veh=2 是**机车**、`order=16`（`OT_GOTO_COUPLE`）、停在普通轨道 `1073,139`：每 tick 走

```
409 REVERSEDIR veh=2 … db=1
413 TPR-WALK veh=2 mf=1073,139 origin=1073,139 td=8 okay=0 exit=2 new=1074,139
421 CPL-PATHFOUND veh=2 found=1
426 FSCP tile=1079,139 fail=tryReserve … owner=11     ← 前路被 veh=11 占住
430 CPL-SAFE-FAIL veh=2 tile=1089,139
431-432 RESV-GC veh=2 tile=1074,139 / 1075,139 why=free-path   ← 放掉自己刚订的两格
433 TRP veh=2 … => ok=0 res=0
434 COUPLE-FAIL loco=2
```

⇒ 「铺两格 → 撞到 veh=11 → 回滚放两格 → 下一 tick 再铺」的循环，8 次；这是**寻路重试 × 整本抹账**共同放大的地鼠。释放本身（放掉自己刚铺的位）方向正确，错在**每 tick 重复 + 日志噪声 + 地图位反复开关（红灯/预览闪）**。

**A-3 更严重：整条路径被反复放光（veh=11）**

```
1016 CRT veh=11 order=6 … found=1
1017 CRT veh=11 …
1018-1026 RESV-GC veh=11 tile=1091,140 / 1092,140 / 1092,139 / 1093,139 … / 1098,139
          （track=stn kind=WAYPOINT，共 9 格）why=free-path     ← 整条路径一次放光
1027 TRP veh=11 … look=1 one=0 … => ok=1 res=1
1047-1048 CRT veh=11 …
1049-1057 再放同样 9 格 why=free-path
1058-1059 CRT veh=11 …
1060-1061 再放
```

`spd` 在 2↔3 之间来回（行 1010 `spd=2`、1028 `spd=3`）⇒ 机车在慢慢挪，而**每一次路径重建都把 9 个位全放掉再订回**。这就是玩家看到的"幽灵红灯先发出再销毁"（KI-208）的**账本放大版**：以前只是视觉闪一下，现在多了一套账本之后变成**每 tick 抹账 + 重订**，且每次都在日志留下 `RESV-GC` 行。

**A-4 机制（代码锚点）**

- `src/pbs.cpp` `R3RResvLedgerForget(key, why)`：
  - 取出 `_r3r_resv_by_owner[key]`（**该 owner 名下全部行**）；
  - 逐行：若 `_r3r_resv_owner_of_bit[bit_key]` 仍等于 `key` 就销掉反查项，**然后无条件** `R3RReleaseReservationSafe(key, row.tile, row.track, why)`；
  - 最后 `_r3r_resv_by_owner.erase(key)`。
- 调用点（`src/train_cmd.cpp`）：`FreeTrainTrackReservation` @**12942**（`why="free-path"`）、解挂 @7923/7924（`"decouple"`）、销毁边（`"destroy"`）。
- 账本**唯一写入口**是六个体位 setter 的汇点 `R3RResvWatchLog()`（`src/r3r_resv_probes.h:31-35` 自陈「this hook is no longer probe-only」）。
  ⇒ 账本里同时躺着三类位：① yapf 的 path 位（**该放**）、② `ReserveTrackUnderConsist()` 的脚下保护位（**不该放**）、③ yapf `rp()` 的耦合补位（**视上下文**）。
  `Forget` 一律全放 ⇒ **key 粒度太粗**（只有"链头"，没有"谁订的/为什么订的"）。

**A-5 潜在误杀（REG-243-K，待验证）**

`R3RReleaseReservationSafe` 只校验「这格仍是 plain rail / station 且该 track 有位」，**不校验归属**。当 `_r3r_resv_owner_of_bit[bit_key]` 已被别的 owner 接管（`bit_it->second != key`）时，代码仍会走去释放地图位 ⇒ 理论上会**放掉别人刚接管的预留**。
- 正常路径下 `R3RResvLedgerNote` 会把旧 owner 的对应行删掉（`R3RResvLedgerDropRow`），故通常不会残留这种行；
- 但**地鼠 B 的漂移**（下节）会让两表不同步，届时这条"无条件释放"就是真误杀。
- 需判据：出现「A 的 Forget 放掉一格，紧接 B 的下一 tick 又把它订回且打 `SET-*`」的成对样本 ⇒ 成立。

### 29.4 地鼠 B：账本虚记（`-skip` 32 条 = 账本比地图多）

**B-1 证据（逐行原文）**

```
 952-959: RESV-GC veh=11 tile=1089,140 … 1082,140 track=stn kind=STATION why=decouple-skip  （8 格）
 960:     RESV-GC veh=11 tile=1090,140 track=0x0 why=decouple                                   （1 格真释放）
1568-1575: RESV-GC veh=23 tile=1089,139 … 1082,139 track=stn kind=STATION why=decouple-skip  （8 格）
1576:     RESV-GC veh=23 tile=1090,139 track=0x0 why=decouple                                   （1 格真释放）
1192-1199: RESV-GC veh=3  tile=1087,139 / 1086,139 / 1085,139 / 1084,139（各 2 次）why=holder-gone-skip
1655-1662: RESV-GC veh=22 tile=1085,139 / 1084,139 / 1083,139 / 1082,139（各 2 次）why=holder-gone-skip
```

⇒ **32 条 `-skip`，全部是 `track=stn kind=STATION` 的站台整格位**。
`-skip` 的语义（`R3RReleaseReservationSafe`）是「释放前自证失败 ⇒ **不改地图**」。所以这 32 条读数的含义是：**账本里记着这 8 格站台整格位，而地图上它们并不属于我们（或压根没有预留位）**。

**B-2 根因（已知边界，但后果被低估）**

`src/r3r_resv_probes.h:25-29` 早已自陈这个边界：

> 「a whole-tile rebuild (`MakeRailNormal`/`MakeRailDepot`/`MakeBridgeRamp`/`MakeStation`/…) overwrites the map bytes directly and **does not go through the setters**」

即：整格重建（含车站重建、站台整格位由 `SetRailStationPlatformReservation()` 这类**平台整体**接口清除）**绕过六个 setter** ⇒ 账本无从销账 ⇒ **账本永久比地图多**。
后果：①地鼠 A 的"整本抹账"每轮都要白跑这 8 格（打 `-skip`）；②反查表 `_r3r_resv_owner_of_bit` 与正查表 `_r3r_resv_by_owner` 漂移后，A-5 的"无条件释放"可能落到别人的位上。

### 29.5 地鼠 C：存量孤儿只能等时延 reap（本会话 `reaped=0`）

**C-1 唯一一条 SUMMARY（行 191）**

```
RESV-AUDIT-SUMMARY tick=6930 reservedTiles=58 owned=38 strayTiles=20 reaped=0 pendingStray=20
```

**C-2 20 条 stray 全 `lowner=-1`、`age=0`**

```
tile=1093,87 … 1112,106（连续斜线）  bits=0x10  age=0  lowner=-1
```

⇒ 这 20 格正是 **KI-346 那条"修复前产生、随存档带入"的 20 格走廊**（§15.3 已定性），`lowner=-1` 表示**账本里没有它们的记录**（读档后账本是空的）⇒ 只能靠审计的 age 计数熬到 `R3R_RESV_REAP_AUDITS`(=3) 次才 `RESV-REAP`。

**C-3 两个"看着像 bug 其实不是"的读数（防止下次误判）**

- `age=0` **不是 bug**：行内打印的是**自增之前**的值（首见为 0），下一次审计才会是 1。
- SUMMARY 只 1 条 **不是 bug**：审计是每 2048 tick 一次，而 `tick=6930` 是本会话读档后的起点，日志跨度不足下一次（8978）⇒ 只跑 1 次。
⇒ 但**观感**就是地鼠：同一批位跨会话长久存在、每次审计都报同样 20 格、玩家等不到 reap。这是"时延式清理"的固有体验缺陷，不是逻辑错。

### 29.6 与既有条目/记忆的关系

- KI-208（第 127 轮「幽灵预留先发出再销毁」）：地鼠 A 是它的**账本放大版**（原先只是视觉闪，现在多一层抹账-重订）。
- KI-346（第 240 轮 `rp()` 订错轨）：其遗留的 20 格走廊 = 本场地鼠 C 的实物。
- KI-355/359/360/361：本条 A/B/C 全部由这套账本机制产生，**是"修归属"的副作用**，不代表层①②③方向错，但必须补"清理粒度"与"账本-地图一致性"。
- 记忆 `87560799`（第 110 轮）：`ReserveTrackUnderConsist` 只加不减 + `_r3r_consist_resv` 老账本 → 老账本现已被 `moved`/`holder-gone` 覆盖，`why=moved` 本会话 0 条，说明老账本已基本空转（另一处待清理的重复机制）。

### 29.7 修法建议（**待拍板，本轮未落地**）

| 优先 | 修法 | 目标地鼠 | 代价 |
|---|---|---|---|
| 1 | `Forget` 加**范围参数**：`free-path` 只放「原 path 位」（按 `consist->path` / `origin+trackdir` 走查覆盖的格），保护位与耦合补位留在账上 | A | 中；需给账本行加"来源"字段（`src` = path / underconsist / couple-rp） |
| 2 | `R3RReleaseReservationSafe` 加**归属校验**（`_r3r_resv_owner_of_bit[bit_key] == key` 才动地图） | A-5 误杀 | 低，纯防御 |
| 3 | 站台整格位在**整格重建**与 `SetRailStationPlatformReservation` 处补销账钩子（或在审计时用地图真值反校账本回写） | B | 中；前者要动不是 setter 的路径 |
| 4 | 存量 stray 的初始 age 设为**读档即老**（例如读档后第一次审计直接把 `lowner=-1` 的位 age 置为 `REAP-1`），或提供控制台一键 `r3r_resv_purge` | C | 低 |
| 5 | 老账本 `_r3r_consist_resv`（KI-203）与新账本合并，去掉双写双清 | 结构性 | 中 |

> 保守起点 = 先做 **2 + 4**（低风险、立即减少噪声），再评估 1（真正治本）。

### 29.8 登记

- **KI-362（新，高）**：`FreeTrainTrackReservation` 的 `R3RResvLedgerForget("free-path")` 整本抹账 + 无条件释放 ⇒ 同格订-放循环（实锤）。
- **KI-363（新，中）**：账本虚记（`-skip` 32 条，账本比地图多），整格重建绕过 setter 造成的永久漂移。
- **KI-364（新，中）**：存量孤儿只能等 3×2048 tick 的时延 reap，观感"清不干净"。
- **REG-243-K（新疑点，待验证）**：`R3RReleaseReservationSafe` 不校验归属 ⇒ 账本漂移时可能放掉他人预留。

### 29.9 未做

- 未改任何源码、未构建（本轮为只读取证）。
- 未核实：`why=acc-car`（3 条）的语义；`RESV-WATCH`/`SET-*` 行数分布（本轮未统计）；地鼠 A 的循环频率到底是一 tick 一次还是几 tick 一次（日志未带 tick 号，只能用行距估）。
- 未取"误杀"正向样本（REG-243-K 仍待验证）。

---

## 30. 第 243 轮续二十七：地鼠 A/B/C 的治本修复（方案与落地）

> 玩家指示（2026-10-14）："鉴于这里出现了地鼠，我希望用最治本的方式修复，但是记得按长期记忆写备忘。"
> ⇒ 本节先落盘方案（§30.1~30.5），再改码（§30.6），最后构建自证（§30.7）。

### 30.1 为什么"打补丁"不是治本：账本的两条基本律被违反

§29 取证出的三个地鼠（KI-362/363/364）与一个疑点（REG-243-K）**不是四个独立 bug，而是同一个结构性缺陷的四个侧面**：账本被当成"单向记账日志"，而不是"与地图同真值的**所有权表**"。作为所有权表它必须满足两条基本律：

- **律 1（释放律）**：任何一次释放，只能释放"调用者按**用途**有权释放的那些位"；一位若仍被另一种用途需要，释放必须降级为"解除该用途的占用"，不得落图。
- **律 2（镜像律）**：账本的每一行都必须对应地图上一个**真实存在**的预留位；反之，账本不得丢下地图上仍然存在、且归属自己的位。

地鼠与疑点逐条落在律的违反上：

| 现象 | 违反 | 机制 |
|---|---|---|
| **A / KI-362** | 律 1 | `R3RResvLedgerForget(owner,"free-path")` 抹掉该链头名下**全部**位（含 `ReserveTrackUnderConsist` 订下的"脚下/站台保护位"）⇒ 保护位被放掉，下一 tick 又被 `ReserveTrackUnderConsist` 订回 ⇒ 同格"订-放-订-放"循环（实锤：同格重复 8 次、veh=11 整条 9 格反复放光） |
| **A-5 / REG-243-K** | 律 1 | `R3RReleaseReservationSafe()` 的"是不是我们的位"只看**地图状态**（该位有没有位），不看**归属** ⇒ 账本漂移时可能把别人的预留放掉（尤其在 `Forget` 先擦反向表、再调释放的当前顺序下，归属信息已经丢了） |
| **B / KI-363** | 律 2（前半） | 站台整格位被"整格重建 / `SetRailStationPlatformReservation`"改写时**绕过六个 setter**，账本收不到 `CLEAR-*` ⇒ 账本留下地图上已不存在的行（32 条 `-skip`）。且更糟：站台分支的 `ours=false` 有**两种语义**被混为一谈——(i) 地图上确实没位了（该丢行）、(ii) 位还在但格上有车/走查找到主人（**不该丢行**）。当前实现一律丢行 ⇒ 那些"位还在"的保护位变成**无主孤儿**，直接喂养了地鼠 C |
| **C / KI-364** | 律 2（后半） | 孤儿要熬满 `R3R_RESV_REAP_AUDITS(=3)` 次审计、每次间隔 `R3R_RESV_AUDIT_TICKS(=2048)` tick ⇒ 6144 tick 才 reap，而本会话（tick 8978 / 仅 1 条 SUMMARY）`reaped=0`，观感"清不干净" |

### 30.2 治本设计：给账本行加"用途位"，并把释放改成"按用途退订"

**核心改动 = 账本行从 `{tile, track}` 升级为 `{tile, track, purposes}`**，`purposes` 是用途位掩码：

- `R3R_RESV_PURPOSE_PATH`（bit0）：这条位是**路径预留**（换乘/前进路线，`FreeTrainTrackReservation` 有权整批撤）。
- `R3R_RESV_PURPOSE_CONSIST`（bit1）：这条位是**车下保护位**（`ReserveTrackUnderConsist` 订下的、车节脚下与站台整格，只有 `ClearReservationUnderConsist`/reconcile 有权撤）。

同一 `(tile,track)` 可被两种用途同时需要 —— 例如车正停在站台上，脚下的站台整格位是 CONSIST，而同一格的进入轨又可能是 PATH。此时**一行两位**：`purposes = PATH|CONSIST`。释放时按用途 `&= ~mask`，**只有掩码归零才真正落图释放**。这直接满足律 1，并且比"给行加单一 src 字段"（§29.7 建议 1）更严谨：单字段无法表达"一位两用途"。

用途的来源（不改任何头文件）：账本唯一的喂入点是 `R3RResvWatchLog()`，它能看到 `r3r_resv_phase`。全库 `R3RResvPhaseGuard` 的 phase 字面量一共 10 个：

```
"choose-track"、"controller"、"yapf-couple"、"release-ghost"、"reap"、"reap-stn"、"gc-stn"
"consist"、"clear-consist"、"waiter-platform"
```

前 7 个与路径/清扫有关 ⇒ 默认 `PATH`；后 3 个是 `ReserveTrackUnderConsist` / `ClearReservationUnderConsist` / 站台等待者路径独占的 phase ⇒ `CONSIST`。用一张 3 项的表在 pbs.cpp 内分类（`R3RResvLedgerPurpose()`），并在两个头文件的注释里写明"新增 phase 必须登记用途"。之所以分类放 pbs.cpp 而不是给 `R3RResvPhaseGuard` 加第三参：后者要改 `src/pbs.h`（被大量 TU include）⇒ 护栏判定全量重编 692 步（约 20 分钟）；而分类只是**把已有信息翻译成用途**，且**分类错误不构成正确性风险**（漏判最多退回"PATH 能放 CONSIST"= 今天的现状，绝不会反向放掉别人的位）。

### 30.3 治本设计：释放前后的顺序与归属校验（律 1 完整化）

1. **`R3RReleaseReservationSafe()` 增加归属校验**（REG-243-K 的正面回答）：

   ```
   const uint32_t booker = R3RResvLedgerOwner(tile, track);
   if (booker != R3R_RESV_NO_OWNER && booker != key) → 记 RESV-GC ...-notowner，不动地图、返回 false
   ```

   只否决"账本明说是别人的位"；**没有行**（读档带入的位、非 R3R 写的位）仍走旧行为（不能回答问题就不动结论），保证不误伤老账本 `_r3r_consist_resv` 与审计 reap。

2. **`R3RResvLedgerForget()` 改为"先释放、后擦反向表"**：当前顺序（先擦反向表再释放）让归属校验永远看到 `NO_OWNER`，等于没校验。新顺序 + 上一条 = `Forget` 天然只放自己的位。返回一个"本次实际落下多少位"的计数，供日志用。

3. **`R3RReleaseReservationSafe()` 返回 bool（是否"结清"）**，并把站台分支的 `ours=false` 拆成两种语义：
   - 地图上已无该位 ⇒ 结清（行该丢）。
   - 位还在、但格上有车 / 走查找到主人 ⇒ **不结清**（行该留）。
   `Forget` 据此**保留**未结清的行（不再无条件丢），从而"地图上仍属于自己的位"不被账本遗弃 ⇒ 从源头掐掉地鼠 C 的孤儿来源。

### 30.4 治本设计：账本反校地图（律 2 完整化，治 B）

新增 `R3RResvLedgerResync()`：正向遍历 `_r3r_resv_by_owner`，对每行用**类型无关**的 `GetReservedTrackbits(tile)` 查地图真值；位已不存在 ⇒ 丢行（并擦反向表），计数后写一行 `RESV-LEDGER-RESYNC rows=<n> dropped=<m> why=map-mismatch`。调用点：

- `R3RReservationAudit()` 末尾（审计本来就要全图扫一遍，加一次正扫成本可忽略；每 2048/256 tick 一次）。
- 这样地鼠 B 的"整格重建绕过 setter"造成的漂移**最多存活一个审计周期**，然后自动归零 —— 不需要去给 `MakeStation` / `SetRailStationPlatformReservation` 逐一补钩子（那才是打补丁），因为**镜像律的职责落在"与地图对账"上，而不是"要求所有写者都记得通报"**。这也顺带把 §29.7 建议 3 的两种做法统一成后者（反校）——更治本。

### 30.5 治本设计：孤儿 reap 的时延（治 C）

`RESV-AUDIT-SUMMARY` 里若 `farFromAnyTrain > 0`，说明有未结清的孤儿 ⇒ 下一轮审计改用**快档间隔**（复用第 243 轮续十二为命令触发加的 `_r3r_resv_audit_due`/256 机制，扩成三档：脏标记 256 / 有待结孤儿 256 / 稳态 2048）。效果：孤儿 3×256 = 768 tick 内 reap（未暂停 <30 秒），稳态开销逐字不变。

### 30.6 落地清单（本轮实际改了哪些）

| # | 文件 | 改动 |
|---|---|---|
| 1 | `src/pbs.cpp` | `R3RResvLedgerRec` 加 `uint8_t purposes`；新增 `R3R_RESV_PURPOSE_*` 常量与 `R3RResvLedgerPurpose()`（phase → 用途表）；`R3RResvLedgerNote()` 改为"同 owner 同行则 OR 用途、换 owner 则 DropRow 重建"；`R3RResvLedgerForget(owner, why)` **保持两参**（不改成三参，因为加 `purpose_mask` 参数要改 `src/pbs.h` ⇒ 护栏判全量 692 步），用途掩码改由新增的 `R3RResvLedgerForgetMask(why)` 按 `why` 字符串映射（`"free-path"`→`PATH`、其余→`ALL`）⇒ 调用侧一字不改却同样满足律 1；释放改为按用途退订 + 掩码归零才落图 + 先释放后擦反向表 + 保留未结清的行；`R3RReleaseReservationSafe()` 加归属校验（放宽为"我或我所在链的链头"）、返回 bool"是否结清"、站台分支拆两种语义；新增 `R3RResvLedgerResync()`（镜像律对账），并让**无主 `R3RResvLedgerForget(VehicleID::Invalid(), …)` 充当 resync 入口**（同样是为了免改 pbs.h） |
| 2 | `src/train_cmd.cpp` | 调用侧**无需**传 purpose（`free-path`/`decouple`/`destroy` 的掩码已由上面的 `why` 映射决定）；新增 `_r3r_resv_audit_has_pending` 并在 `R3RReservationAudit()` 末尾（`RESV-AUDIT-SUMMARY` 之后）设值 + 调 `R3RResvLedgerForget(VehicleID::Invalid(), "resync")`；调度扩成三档（命令脏标记 `_r3r_resv_audit_due` 256 / 孤儿待结 `_r3r_resv_audit_has_pending` 256 / 稳态 2048），`RESV-AUDIT-DUE` 增打 `due=`/`pending=` |
| 3 | `src/vehicle.cpp` | **未改**：`destroy` 的释放本来就是"结清该 owner 名下全部位"，与 `why`→`ALL` 映射逐字等价，改它只是显式化、无行为差异，本轮为最小改动故不动 |
| 4 | 头文件 | **未改任何 `src/*.h`** ⇒ 增量构建合法（不改数据结构布局，只在 .cpp 内部） |

> 刻意不做：不合并老账本 `_r3r_consist_resv`（§29.7 建议 5）。它在 `why=moved` 0 条时已基本空转，但合并要动 `ReserveTrackUnderConsist`/`ClearReservationUnderConsist` 的结构，风险与收益不成比例；本轮只让**新账本**满足两条律，老账本靠归属校验兜底（它的 key 与新账本同源，不会互相误杀）。

### 30.7 构建自证（已回填，2026-10-14 22:24）

复用既有 `_tmp_inc_build.cmd`（**未新建任何 `.cmd`**）；构建前 `tasklist` 确认 `openttd.exe` 未运行。护栏 `build\R3R_incbuild.guard.log` = `GUARD: incremental is safe (no header/lang file is newer than the newest object)`（本轮**未改任何 `src/*.h`**，`src\pbs.h` 未动 ⇒ 增量合法）。

| 项 | 值 |
|---|---|
| 日志尾 | `[2/4] Building CXX object ...\src\pbs.cpp.obj` → `[3/4] Building CXX object ...\src\train_cmd.cpp.obj` → `[4/4] Linking CXX executable openttd.exe`（Linking 1 次） |
| 完成标记 | `build\R3R_incbuild.done` = `EXIT_CODE=0`（22:24:32，13 B）；`error C*` / `fatal error` / `FAILED:` / `build stopped` 计数 **0** |
| 时间戳链 | `src\pbs.cpp` 22:21:04 → `pbs.cpp.obj` **22:24:19**；`src\train_cmd.cpp` 22:21:16 → `train_cmd.cpp.obj` **22:24:23** ⇒ `build\openttd.exe` **22:24:30**（51 843 072 B，晚于全部所改源码） |
| lint | `read_lints`（`src\pbs.cpp` / `src\train_cmd.cpp`）**0 条** |
| exe 字面量自证 | `RESV-LEDGER-FORGET veh=%u why=%s mask=0x%X released=%u kept=%u`、`RESV-LEDGER-RESYNC rows=%u dropped=%u why=map-mismatch`、`RESV-GC … why=%s-notowner(veh=%u)`、`RESV-AUDIT-DUE tick=%u interval=%u due=%u pending=%u` **全部命中** |

**踩坑记录（本轮新增，建议长期遵守）**：第一次启动构建时 `src\pbs.cpp`(22:21:04)/`src\train_cmd.cpp`(22:21:16) 的落盘时刻**晚于**构建启动（约 22:19）⇒ 那一轮 ninja 只编到 `pbs.cpp.obj`(22:20:04)，`train_cmd.cpp.obj` 仍是 10-11 的旧文件，链接出的 exe **不含 train_cmd.cpp 的改动**。危害：`R3R_incbuild.done=EXIT_CODE=0` + `[n/n] Linking` 全绿 + exe mtime 也是新的，"三件套"看起来通过，只有 `train_cmd.cpp.obj` mtime 早于源码这一条与之矛盾。**唯一可靠的补充判据 = 在 exe 里检索本轮新增的日志字面量**（`findstr /C:"interval=%u due=%u" ...\build\openttd.exe`：首次为空 ⇒ 确证未编入），据此重跑一次才有上表结果。⇒ 教训：改完源码后先确认源码 mtime 稳定（不再变动）**再**启动构建；交付前一律追加"exe 字面量"复核，不能只看 done/log/时间戳。

**状态**：KI-362 / KI-363 / KI-364 与 REG-243-K 均 = **已实现 + 已编译**，游戏内复测待做（判据见 `R3R_KNOWN_ISSUES.md` 的「第 243 轮续二十七」小节 6 条）。

---

## 31 续二十七 exe 的首次复测判读（2026-10-14 22:44 会话；纯取证，零源码改动、零构建）

### 31.1 采样有效性

| 项 | 值 |
|---|---|
| 产生者 | `build\openttd.exe` @2026-10-14 **22:24:30**（51 843 072 B）= 续二十七那个含"用途位账本"的 exe |
| 日志 | `build\R3R_debug.log` **1745 行**，末次写入 22:44:23（晚于 exe ⇒ 采样有效） |
| 运行时侧字面量自证 | `log:382 RESV-LEDGER-FORGET veh=2 why=free-path mask=0x1 released=18 kept=0` —— `mask=` / `released=` / `kept=` 三字段是**本轮新增**的格式 ⇒ 确证日志由新 exe 产生（KI-365 的"exe 字面量"判据在运行侧再次成立） |

### 31.2 判据逐条（6 条）

| # | 判据 | 结果 | 证据（`build\R3R_debug.log` 行号） |
|---|---|---|---|
| 1 | 地鼠 A：同格不再被反复 `SET-RAIL`/`CLEAR-RAIL`；veh=11 的 9 格链不再整条反复放光 | **不可判（未覆盖）** | 全日志 `SET-RAIL` / `CLEAR-RAIL` **0** 条，且 `RESV-WATCH` 也 **0** 条 ⇒ 本会话根本没走到旧现场"整格订→整格清"那条路径（不是"修好了"，是"没踩到"） |
| 2 | 地鼠 A：车停在站台时 `free-path` 不再放掉站台整格位（应见 `why=free-path-notowner` 或该位不在待放集合） | **未覆盖** | `RESV-GC` 共 367 条，`why=` 分布 = free-path 180 / free-path-hold 115 / free-path-gone 18 / decouple-hold 16 / holder-gone-hold 16 / decouple-gone 12 / holder-gone-gone 8 / decouple 2，**无 `-notowner`**；但本会话没有任何站台整格位预订（`RESV-WATCH`=0）⇒ 该分支未被触发 |
| 3 | 地鼠 B：`-skip` 行数显著下降；出现 `RESV-LEDGER-RESYNC … dropped>0` 能定位整格重建漂移 | **不可判** | 367 条 `RESV-GC` 中**无一条带 `-skip`**；`RESV-LEDGER-RESYNC` **0 条**（该函数挂在审计末尾，而审计从未运行，见 #5） |
| 4 | 地鼠 B：`-skip`/"位还在但格上有车"这类不再丢行 | **不可判** | 同上：无 `-skip` 行，也无 `RESV-AUDIT-OWNER` / `RESV-AUDIT-STRAY` 行可供对照 |
| 5 | 地鼠 C：`farFromAnyTrain>0` 的下一轮 SUMMARY 间隔 ≤256 tick，3 轮内出现 `RESV-REAP` | **不达成（审计从未运行）** | `RESV-AUDIT-SUMMARY` **0**、`RESV-AUDIT-DUE` **0**、`RESV-REAP` **0**、`RESV-AUDIT-STRAY/OWNER/WATCH` 各 **0**；连带的 `RESV-LEDGER-RESYNC` 也是 0 |
| 6 | 负判据：正常耦合/解挂/卖车/读档行为与 `COUPLE-OK`/`DECOUPLE-DONE`/`RESV-GHOST` 逐字不变；`RESV-AUDIT-STRAY` 不新增；无 `pbs.cpp:144/145`、`rail_map.h:168` 断言 | **部分通过** | `COUPLE-OK` 3 条（`log:267` loco=23、`log:1654` loco=11 等）、`DECOUPLE-DONE` 2 条、`REVERSEDIR`/`REVERSEDONE` 各 3 条，均正常；`RESV-GHOST` 0 条（本会话无该事件，不构成回归）；日志末尾 1721-1745 仍在正常打印 `REDSIG`/`TPR`/`ARRANGE` ⇒ 无崩溃、无断言。但"逐字不变"须与旧 exe 同场景对照，本会话样本不足 |

**关键行锚点**：`log:267 COUPLE-OK loco=23`、`log:382 RESV-LEDGER-FORGET veh=2 why=free-path mask=0x1 released=18 kept=0`、`log:404 … released=2 kept=0`、`log:1460 RESV-LEDGER-FORGET veh=23 why=decouple mask=0x3 released=13 kept=8`、`log:1654 COUPLE-OK loco=11`、`log:1721-1725 REDSIG veh=23 old=1090,139 new=1091,139 red=0x5 / resv=0x0 veh=0`、`log:1726 SPD-DROP veh=23 prev=15`。

### 31.3 判据外的**正面**结果（本轮新增，值得单独记）

**"按用途记账、只退对应用途的位"首次拿到运行时正例**：

- `log:382` free-path 只退 PATH 位（`mask=0x1`）且 `kept=0`（无残留行）—— 正是 KI-362 想建立的不变式"`free-path` 不得顺手把 CONSIST 保护位一起放掉"；
- `log:404` 同一链再次 `released=2 kept=0`（幂等、不重复记账）；
- `log:1460` 解挂路径 `why=decouple mask=0x3 released=13 **kept=8**` —— **`kept>0` 说明"该退的退、不该退的留住"在真实数据上成立**（若仍是"整行抹除"语义，这一行必为 `released=21 kept=0`）。

⇒ KI-362 / REG-243-K 的**机制侧已验证**；尚未验证的是站台整格位分支（#2）与审计/reap 节奏（#5）。

### 31.4 审计族全 0 的根因判定（本轮核心结论）

- 审计门在 `TrainLocoHandler()` 内（`src\train_cmd.cpp:17328-17349`）；**同一函数更靠前的 `SPD-DROP` 探针本会话打印 13 条** ⇒ 该函数体确实在执行，门不是被 early return 遮住的。
- 门 = `_tick_counter - r3r_resv_audit_tick >= interval`，`interval = (due || pending) ? 256 : 2048`；`r3r_resv_audit_tick` 是块内 `static`，**初值 0 且从未被更新**（因为审计从未跑）⇒ 判定退化为 `_tick_counter >= interval`。
- `_tick_counter` 是 `uint64_t`（`src\date_func.h:16`），**仅由 `IncreaseDate()` 在未暂停时 `++`**（`src\date.cpp:569`），且**不随存档保存/恢复**（`src\saveload\afterload.cpp:882` 只用它算 `_scaled_tick_counter`）。
- ⇒ **结论：本会话累积的"未暂停 tick"根本没过门槛。** 保守上界 `< 2048`；若 `log:1560/1598/1615/1644` 那几次库内重排（`ARRANGE-IN`，来自 `ArrangeTrains`，`src\train_cmd.cpp:2957-2969`）确实走的是 `CmdMoveRailVehicle::Execute`（会 `_r3r_resv_audit_due = true`，且该标记**只**在审计分支里清除 ⇒ 一旦置位就永久保持）⇒ interval 恒 256 ⇒ 更紧的界 `< 256`。
- 旁证（会话确实很短）：`COUPLE-OK` 仅 3 次、`DECOUPLE-DONE` 仅 2 次、veh=2 从 `log:362 tile=1073,139` 走到 `log:1732 tx=1024 ty=136`（约 50 格）。
- **误判根源记录**：`RESV-AUDIT-DUE tick=…`（`src\train_cmd.cpp:17345`）只在**审计真的发生**的那一 tick 打印，**不能用它反证"命令没置位 due"**。此前曾按"若 due 置位就该出现 DUE 行"推断，方向是错的。

### 31.5 未判项与疑点（已登记进 KI）

- **U-1（红灯未定性）**：`log:1721-1725` veh=23 停在 1090,139、`stuck=1`，目标格 1091,139 `red=0x5`；而 1091,139 与再下一格 1092,139 都是 `resv=0x0 veh=0` ⇒ 挡它的占用在更远处。本会话没有任何审计输出，**无法定性**是"幽灵预留"还是"正常 PBS 占用"。需在能跑出审计的会话里复现。
- **REG-243-L（新疑点）**：`RESV-AUDIT-DUE` 缺位**不能**推出"`due` 未置位"；本会话 6 条 `ARRANGE-IN` 无法从日志区分来源（`ArrangeTrains` 被 `Couple()` 与 `CmdMoveRailVehicle()` 共用）。
- **REG-243-M（取样纪律，本轮最大教训）**：6 条判据中 #3/#4/#5 都要求"会话跨过 256 个未暂停 tick"。**复测前必须把游戏速度拉满并连续运行 ≥3 分钟不停顿**（中途暂停会让 `_tick_counter` 停走），否则这三条永远不可判。
- **KI-366（诊断盲区）**：会话 tick 不足时，"审计族 0 条"与"审计调度被破坏"在日志上**完全同形**。建议加一条极低频心跳（门判定失败且 `_tick_counter - r3r_resv_audit_tick >= interval/2` 时打 `RESV-AUDIT-WAIT tick=%u next=%u interval=%u`，或进程内首次进入打一行 `RESV-AUDIT-ARM`），代价可忽略，收益=一眼判定"会话太短"。**本轮未实现**。

## 32 KI-366 心跳落地（2026-10-14 23:14；仅 `src\train_cmd.cpp`，已编译通过）

**动机**：§31.4 的结论是"审计族 0 条"由**会话太短**造成，但日志上它和"审计调度被破坏"**完全同形**，已浪费一整轮复测。故按 §31.5 的建议把两条诊断心跳直接做出来，使下一次复测**必然可判读**。

**改动（审计门内，`src\train_cmd.cpp` 约 17328-17374）**：

1. 新增两个块内静态量：`static uint32_t r3r_resv_audit_wait_log = 0;`、`static bool r3r_resv_audit_armed = false;`。
2. **`RESV-AUDIT-ARM`**（一次性）：进程内首次进入审计门即打印 `tick= interval= due= pending=` ⇒ 证明"门可达"，并暴露该会话**实际使用的 `interval`**（`due||pending` ⇒ 256，否则 2048）。
3. **`RESV-AUDIT-WAIT`**（节流）：门未命中且 `waited >= interval/2` 时打印 `tick= waited= interval= due= pending=`，由 `r3r_resv_audit_wait_log` 节流至**每 `interval` 至多一行** ⇒ 直接读出"还差多少 tick 才审计"。
4. 稳态开销：两次无分支比较 + 每 `interval` 至多一行日志，**逐字不改**既有 `RESV-AUDIT-DUE`/`RESV-AUDIT-SUMMARY` 行为。

**判读表（下次复测先看这两行）**：

| 观测量 | 判定 |
|---|---|
| 有 `ARM`、无 `WAIT`/`DUE`/`SUMMARY` | 门可达，但本会话连 `interval/2` 都没等到 ⇒ **会话太短** |
| 有 `ARM`、`interval=2048`、始终无 `DUE` | 库内重排**没有**走 `CmdMoveRailVehicle::Execute`（`due` 从未置位）⇒ 修正 §31.4 的保守上界推断 |
| 有 `ARM`、`interval=256` | 命令已置位 `due`，纯属没等到 256 tick |
| 连 `ARM` 都没有 | 审计门不可达 ⇒ 此时才怀疑调度被破坏 |
| 有 `WAIT waited=…/interval=…` | 直接给出缺口量，可据此告诉玩家"还需跑多久" |

**构建自证**：复用既有 `_tmp_inc_build.cmd`（**未新建任何 `.cmd`**）；`build\R3R_incbuild.guard.log` = `GUARD: incremental is safe`；`src\train_cmd.cpp` 23:12:09 → `train_cmd.cpp.obj` 23:12:54 → `build\openttd.exe` **2026-10-14 23:14:54（51 854 336 B）**，晚于全部所改源码；日志末行 `[3/3] Linking CXX executable openttd.exe`；错误计数 0；`build\R3R_incbuild.done` = `EXIT_CODE=0`；`read_lints` 0 条；**exe 字面量 2/2 命中**（`RESV-AUDIT-ARM tick=%u`、`RESV-AUDIT-WAIT tick=%u`）。

**下一步（必须由玩家跑）**：用本 exe（23:14:54）**速度拉满、连续不停顿跑 ≥3 分钟**复跑同一场景，然后回 `build\R3R_debug.log`。此时 §31.2 的判据 #2/#3/#4/#5 与 U-1 应变为可判读（REG-243-M 的取样纪律）。

---

## 34. 续三十一：新日志（2 044 497 B / 25 976 行）崩溃定性

### 34.1 已确认事实（先落盘，2026-10-14 23:57 收）
- 日志：`D:\temp\codebuddy-dropped-files\003ac241-e81a-440d-ba34-52cba88b0621\R3R_debug.log`，**2 044 497 B / 25 976 行**，`LastWriteTime 2026/10/14 23:57:18`（§33.5 那轮 exe 是 23:14:54、KI-367 修复 exe 是 23:43:58 ⇒ **本日志晚于 KI-367 修复**，但"是否用的是新 exe"尚需另证，见 34.4）。
- 日志第 1 行 = `SEGLOAD-PARK veh=3 …` ⇒ 会话**从读档开始**（不是新开局）。
- 全日志 `ssert` / `Assertion failed` / `first_shared` / `IsStoppedInDepot` 命中数 = **0** ⇒ **本次没有 KI-367 那条 `vehicle_base.h:692` 断言**，日志里也没有任何断言文本。
- 尾部（倒数第 29~28 行）出现站台关注格命中，**新关注格生效**：
  `RESV-WATCH CLEAR-STN tile=1085,139 track=255 phase=controller actor=2 head=2`
  `RESV-WATCH CLEAR-STN tile=1086,139 track=255 phase=controller actor=2 head=2`
  ⇒ 这是 §33.7/§33.8 要的"站台整格位被清"的第一份现场（`SET-STN` 是否配套见 34.2）。
- 尾部（倒数第 27~26 行）的 `CRASH: movingFirst_order=1 v_order=0 vFirst_order=1 same_chain=0 speed=22 tile=281614` + `CRASH-INFO …` **不是崩溃**：它是 `src\train_cmd.cpp:15471-15501` 的**遗留调试探针**，函数头注释原文为 `DEBUG (R3R — remove after locating the crash): log the coupling-condition failure to a file so it is always visible`，位置在 `CheckTrainCollision` 里"耦合条件不成立 ⇒ return 0"那一步。它在 §33 那份日志也出现过（`log:9804/9840/9841`），属**常态噪声**。⚠️ 这个探针名极具误导性，后续任何人读日志别再把它当崩溃。
- 日志**最后 4 行是 `SEGSAVE-ROW …`**（存档写出路径）⇒ 会话末尾正在**存档**；崩溃点应在此附近或存档之后。

### 34.2 待取证（下一步）
- `SET-STN` / `CLEAR-STN` 的**全日志计数与配对**（KI-208 口径：订-清是否配平）。
- `RESV-AUDIT-SUMMARY` / `RESV-REAP` 是否出现（KI-366 心跳 + KI-345 GC）。
- **真正的崩溃证据**：`R3R_debug.log` 里无断言 ⇒ 需找 `Documents\OpenTTD\crash-*.log`（新的时间戳）或现场截图；若没有新 crash 档，则"崩溃"可能是存档路径上的硬崩溃（访问违例/无断言）。
- `SEGSAVE-ROW` 前后是否有半截存档/`SEGID` 异常。

### 34.3 疑点登记（REG-243-N）
- **REG-243-N**：用户报告"快进到崩溃"，但日志无断言、无 abort、结尾还在正常存档 ⇒ "崩溃"与"日志内容"不自洽。可能：①崩溃发生在存档写出**之后**（日志已 flush 完整）②崩溃在**另一个进程/另一份日志**（如 `crash-*.log`）③"崩溃"实为**卡死/无限循环**（日志停更但进程存活）④游戏被强杀。需玩家确认症状（弹框文字/白屏/无响应/直接消失）并回 crash 目录清单。**期望值**：若真是 KI-367 家族，应能在 crash-*.log 里看到 `vehicle_base.h:692` 或其他断言；**实测值**：R3R_debug.log 内 0 处断言文本。

---

## 33. 复跑会话断言崩溃定性（2026-10-14 深夜，零源码改动）

### 33.1 断言点（用户忘看，本轮从崩溃日志取回）
- 崩溃日志：`C:\Users\冯洁敏\Documents\OpenTTD\crash-20261014T152420Z.log`
- 第 11 行（逐字）：`Message:    Assertion failed at line 692 of D:\sourcecode of JGRPP\src\vehicle_base.h: this == this->First()`
- 栈（第 26-37 行）：`OnTick_Station`(`src\station_cmd.cpp:5259`) → `DeleteStaleLinks`(`src\station_cmd.cpp:5078`) → `'DeleteStaleLinks'::'4'::<lambda_1>::operator()`(`src\station_cmd.cpp:5123`) → `Vehicle::IsStoppedInDepot`(`src\vehicle_base.h:692`) → `assert_str_error` → `FatalErrorI` → `DoOSAbort`；线程 = 非主线程 `ottd:game`。
- 会话：15:21:32 加载 → 15:24:20 崩溃（2 分 48 秒，`116648 state ticks`），游戏内日期 1920-05-06。
- 源码交叉：`vehicle_base.h:690-696` 的 `IsStoppedInDepot()` 首行 = `assert(this == this->First());`（上游原生）；`station_cmd.cpp:5112` `vehicles.push_back(l->GetFirstSharedVehicle());`、`:5117` `Vehicle *v = *iter;`、`:5123` `if (!v->IsStoppedInDepot() || …)`。

### 33.2 字面含义（已实证）
某个 `OrderList` 的 `first_shared` 指向的车**不是它所在链的链头**。`IsStoppedInDepot()` 的语义（注释+断言）只对链头成立，`DeleteStaleLinks` 却把 `GetFirstSharedVehicle()` 的返回值当链头用。

### 33.3 猜测根因（待验证）
R3R route A「排程借用」直接赋值 `v->orders = u->orders`（`Couple()`），既不走 `OrderList::AddToShared()` 也不走 `RemoveFromShared()`（= KI-93 的"借用不注册为共享链"）；耦合/解挂/逻辑翻/身份迁移/车库拖动/克隆/段升降都会改写"谁是链头"，而 `first_shared` 只由原生两个函数维护 ⇒ 原 `first_shared` 车一旦降为链中车即断言。
支撑：`log:9841 CRASH-INFO`（`v=10` 的 `First()=11`，即 10 非链头，且 `pair=0` 表示与 `mf=2` 不同链却同格 `1038,137`）；`log:9804 SEGTR-SNAP tag=couple head=2 nseg=2 ctrl=12 borrowed=1`（借用现场）；`log:9840 CRASH`（断言前一行，`same_chain=0`）。
待验证判据：在 `station_cmd.cpp:5117` 加只读探针（`v->index` vs `v->First()->index`），复现时命中即实证；并在各"改链头"提交点后加只读一致性探针定位坏在哪一步。

### 33.4 是否新回归
否。断言点与调用链全是上游/既有代码；续二十八 exe 只多了两口只读日志（KI-366）。更像"会话变长把潜伏缺陷暴露"（`DeleteStaleLinks` 需 link graph 站节点 edge 过期才走到该分支，本会话 `_tick_counter` 首次到 ~10 000）。

### 33.5 KI-366 与续二十七/二十八判据（本会话首次可判读）
- `log:161 RESV-AUDIT-ARM tick=6930 interval=2048 due=0 pending=0` ⇒ 心跳生效；首次 `interval=2048` ⇒ `due` 从未置位（判据 #3 口径 ②**通过**）。
- `log:1875/2351/9154 RESV-AUDIT-WAIT … interval=256`、`log:9107 WAIT tick=9362 waited=1920 interval=2048` ⇒ WAIT 共 5 条，缺口可量化。
- `log:191 SUMMARY tick=6930 strayTiles=20 reaped=0` → `log:2122 tick=7186 同` → `log:3026 tick=7442 strayTiles=20 reaped=20 pendingStray=0`；`log:9129 tick=9490 reservedTiles=19 owned=11 strayTiles=8 reaped=0 pendingStray=8` → `log:9185 tick=10002 strayTiles=8 reaped=8` ⇒ 判据 #5 **通过**。
- 计数：DUE 4、SUMMARY 6、REAP 28、STRAY 84、GC 1586、LEDGER-FORGET 1180、LEDGER-RESYNC 0、COUPLE-OK 15、DECOUPLE-DONE 14；`RESV-WATCH`/`SET-RAIL`/`CLEAR-RAIL` 全 0 ⇒ 判据 #1/#2 仍未覆盖，U-1 仍开放。
- REG-243-M 口径修正：门槛是"未暂停 tick ≥ interval"而非"墙钟 ≥3 分钟"；纪律改为"直到日志出现 `RESV-AUDIT-SUMMARY` 再收工"。

### 33.6 本轮未做
未改任何源码、未构建（本轮只做判读）；KI-367 的三个修法候选均待玩家拍板。

### 33.7 下一次复测的原始目标（玩家 2026-10-14 追问「我叫你跑日志是想看什么」⇒ 明确回填，勿再绕）
本次让玩家跑长会话的**原始目的**（= 续二十八 §31 那 6 条判据），逐条现状：
- **#1 / #2（地鼠 A：站台整格位"整格订→整格清"）→ 仍未覆盖**：要求日志出现非 0 的 `RESV-WATCH`/`SET-RAIL`/`CLEAR-RAIL` 且 `SET-RAIL` 与 `CLEAR-RAIL` 计数配平，以及 `RESV-GC … why=free-path-notowner`。本会话三者**全 0**（原因：本会话走的 `1112,128..131` 是**普通铁轨** `track=1/2/3`，不是站台整格位）⇒ 下次须在**站台场景**复跑。
- **U-1（红灯定性，仍开放）→ 与 #1/#2 同一现场**：上一现场 = `veh=23` 停在 `1090,139`、目标格 `1091,139` `red=0x5`（**站台格**），本会话剩余 8 个 stray 是否与它同源未查。下次须重演"`veh=23` 往 `1085,136` 等待链耦合"的站台场景，并把关注格清单（`build\r3r_resv_watch.txt` 现有 `1112,128..131`）**追加 `1091,139` 一带**（含 `1090..1092,136..140`）。
- **#3 / #5 → 已通过**（见 §33.5），**KI-366 心跳已验证**，均无需重测。
- **KI-367（断言崩溃）** 已单列条目，三个修法候选待拍板；它**不是**本次复测的原始目的，属附带发现。

### 33.8 KI-367 落地（第 243 轮续三十，2026-10-14，玩家拍板「先按你推荐的一行止血」）
**修法走方案 1 的变体**（只改 `src\station_cmd.cpp` 一个 .cpp，未碰任何 `src\*.h` ⇒ 增量合规）：

```5131:5131:src/station_cmd.cpp
// ... existing code ...
```

改动实质（`DeleteStaleLinks` 内层循环，原 5117-5126 → 现 5117-5131）：
- 新增 `Vehicle *const chain_head = v->First();`（紧跟在 `Vehicle *v = *iter;` 之后）。
- `IsStoppedInDepot()`、`date_of_last_service`、`LinkRefresher::Run()` 三处由 `v` 改为 `chain_head`。
- 去重缓存键 `_delete_stale_links_vehicle_cache.insert(...)` 也改为 `chain_head->index`（`LinkRefresher::Run` 刷的是整条链，按链头去重才自洽）。

**为什么不是教科书式的"只在 5117 改一行 `(*iter)->First()`"**（重要，理由留档）：
- 循环体第 5137 行 `Vehicle *next_shared = v->NextShared();` + 5139 `*iter = next_shared;` 是**沿共享排程链推进迭代器**的下一步。若把 `v` 直接换成链头，第二次迭代起 `next_shared` 会从"上一个共享节点的链头"的 `next_shared` 取，等于中途改变遍历路径；上游健康档里 `first_shared == 该车 First()`（`order_cmd.cpp:609 OrderList::Initialize` 与 `:953 RemoveVehicle` 都只让 primary vehicle 进共享链）故两者等价，但 R3R 陈旧档下会走出未定义路径。
- 本变体把 `v = *iter` 与 `chain_head` 分开：**遍历完全不动**（终止性与改动前逐字一致 ⇒ 不会引入新的死循环面），只把"必须传链头"的三个调用点换成链头。
- 对上游负判据 ③：`first_shared` 本就是链头 ⇒ `chain_head == v` ⇒ **行为逐字不变**。
- 未选方案 2/3（同步 `first_shared`）：`first_shared` 是 NOSAVE 且 R3R 有 ~十处改链头提交点，动 `AddToShared`/`RemoveFromShared` 会与 KI-93 家族的"排程借用不经共享链"口径正面冲突，风险远大于收益。方案 1 属**语义修正**（调用点本就该传链头，是 R3R 让 `first_shared` 陈旧才暴露），不是绕断言。

**构建自证**：复用 `_tmp_inc_build.cmd`（未新建任何 .cmd）；`build\R3R_incbuild.guard.log` = `GUARD: incremental is safe (no header/lang file is newer than the newest object)`；日志 `[2/3] Building CXX object ...\station_cmd.cpp.obj` → `[3/3] Linking CXX executable openttd.exe`；`build\R3R_incbuild.done` = `EXIT_CODE=0`（23:43 本轮）；`src\station_cmd.cpp` @23:41:33 → `station_cmd.cpp.obj` @23:42:34 → `build\openttd.exe` @**2026-10-14 23:43:58**（51 854 336 B）⇒ 三件套齐全；日志内 `error C*` / `fatal error` / `FAILED:` / `build stopped` 计数 **0**；`read_lints` 0 条。本轮未新增日志字面量 ⇒ 不做 exe 字面量核对。
**待复测判据**（照 KI-367 条目）：①长会话 + 双链同格不再崩；②不再命中 `first_shared != 链头`；③负判据：`Ctrl+共享排程` 车队的 `DeleteStaleLinks` 行为逐字不变。
**注意**：本修复只堵崩溃面，**不**修"`first_shared` 陈旧"本身 —— 下次长会话若审计仍报存活异常，仍按方案 2/3 复查。

### 35 续三十二：新崩溃日志定性 —— KI-367 未止血，真因是 first_shared 悬垂
输入：玩家日志（≈2 044 KB，首行 `SEGLOAD-PARK veh=3`）+ 崩溃档 `C:\Users\冯洁敏\Documents\OpenTTD\crash-20261014T155659Z.log`（15.33 KB，15:56:59Z = 本地 23:56:59，与日志 mtime 23:57:18 只差 19 s ⇒ 同会话）。
崩溃档原文：`Version: r3r-stable-2026-10-03-m (2)` / `Crash reason: 0xC0000005` / `Fault addr: FFFFFFFFFFFFFFFF` / `RAX: DDDDDDDDDDDDDE15`（MSVC Debug 堆"已释放"填充 0xDD ⇒ use-after-free）/ `Bytes at instruction pointer: 8B 00`。
栈：`[00] DataNode<...>(PoolID<VehicleIDTag> const&) +21 (robin_hood.h:1003)` → `[01] Table<...>::emplace +74 (robin_hood.h:1833)` → `[02] insert +40 (robin_hood.h:1937)` → `[03] lambda +1947 (station_cmd.cpp:5124)` → `[04] LinkGraph::MutableIterateEdgesFromNode (linkgraph.h:472)` → `[05] DeleteStaleLinks +374 (station_cmd.cpp:5078)`。

判定 1（崩溃 exe = 含 KI-367 修复的那个，"Build date" 是陈旧嵌入值）：崩溃档的 `Build date: Oct 11 2026 12:18:48` 来自 `openttd.cpp` 编译期 `__DATE__/__TIME__`，而 `build\CMakeFiles\openttd_lib.dir\src\openttd.cpp.obj` = 2026/10/11 12:34:40（此后从未重编）⇒ 增量构建不更新它，不能用它判版本。实测 `station_cmd.cpp.obj` = 2026/10/14 23:42:34、`build\openttd.exe` = 2026/10/14 23:43:58（51 854 336 B，全树唯一）；且崩栈行 5124 正是**加 KI-367 注释后**的 insert 行（旧行号 5118，注释 5118-5121 + 5122 `chain_head`）⇒ **崩溃 exe 含修复**。∴ **KI-367 方案 1 未止血**：同函数同行仍崩，形态由 `IsStoppedInDepot()` 断言改成访问违例。

判定 2（真因 = first_shared 悬垂）：`robin_hood.h:1003` = `mData(std::forward<Args>(args)...)`，即从入参 const 引用拷 key；栈帧 `<PoolID<VehicleIDTag> const&>` ⇒ 故障读的地址 = `&chain_head->index`，而该地址已 0xDD 填充 ⇒ `chain_head` 本身悬垂。`v` 来自上游 5112 `vehicles.push_back(l->GetFirstSharedVehicle());`（`l : OrderList::Iterate()`，5098）。现场：日志尾部 `RESV-GC veh=11 … why=destroy` + 两条「列车相撞!」+ `CRASH: … tile=281614`（与新闻 Ref1 同值）⇒ 车辆因碰撞被销毁，其 OrderList 的 `first_shared` 未重指，下一次站台 tick 的 `DeleteStaleLinks` 取到即崩。日志结尾为 `SEGSAVE-ROW`（正在存档）⇒ 崩溃在存档之后的下一个站台 tick，非存档路径。

待复核：U-reg-1 `First()` 内联后行号归属（PDB 可能把 `First()` 内的读归到 5124）；U-reg-2 已取样：全日志 `SET-STN`=22 / `CLEAR-STN`=24 / `RESV-`=18802 / `RESV-REAP`=28 ⇒ 站台整格位订-清**基本配平**（CLEAR≥SET，无泄漏迹象）、GC 有触发 ⇒ **本崩溃与 KI-208/KI-345 预留体系无关**，别再往预留方向找；U-reg-3 是谁的销毁路径漏了 `first_shared` 重指（`Vehicle::PreDestructor`→`DeleteVehicleOrders`，含 §33 已做的 KI-93 兜底）。

### 36 续三十二·修法可行性取证（2026-10-15）

**事实 1（现场代码，本轮已读 `src\station_cmd.cpp:5092-5136`）**：`5098 for (const OrderList *l : OrderList::Iterate())` → `5112 vehicles.push_back(l->GetFirstSharedVehicle());` → `5117 Vehicle *v = *iter;` → `5122 Vehicle *const chain_head = v->First();`（KI-367 方案 1 新增）→ `5124 _delete_stale_links_vehicle_cache.insert(chain_head->index)`。⇒ 方案 1 的 `v->First()` **本身就是对毒指针的 deref**，因此必然仍崩（与本轮崩溃栈逐帧吻合）。

**事实 2（崩溃档读数复述）**：`RAX = DDDDDDDDDDDDDE15`、`Fault addr = FFFFFFFFFFFFFFFF`、指令 `8B 00`（`mov eax,[rax]`），栈特化 `<PoolID<VehicleIDTag> const&>` ⇒ 毒的**不是"指向错的车"，而是指针值本身已被 0xDD 覆盖**。∴「取用前校验它仍指向池中活车」这条路**在 `DeleteStaleLinks` 内没有安全入口**。

**事实 3（Pool API，本轮已读 `src\vehicle_base.h`）**：`typedef Pool<Vehicle, VehicleID, 512> VehiclePool;`（:212）、`extern VehiclePool _vehicle_pool;`（:215）、`struct Vehicle : VehiclePool::PoolItem<&_vehicle_pool>, …`（:251）；`index` 存在 `PoolItem` 内，raw 取值靠 `GetRawIndex(const S &index){ return index.base(); }`（:1843-1845）；而 `Vehicle::IsValidID(index)`（:1632）与 `Vehicle::Get(index)` 都**以 raw index 为入参**。⇒ 从裸指针 O(1) 反推 index 只能 deref 该指针；毒指针下不可能。唯一"不 deref"的校验是**全池遍历**（`Vehicle::Iterate()` 建活指针集合再 contains），代价 O(车数)/次站台 tick。

**结论（判定 3）**：**止血方案在本函数内不可行**；必须**治本** —— 在销毁路径维护 `OrderList::first_shared`，使毒值根本不产生。
- 形态判别：`OrderList::Iterate()` 仍能把 `l` 取出来（5098 走到了）⇒ **表活、主人死**，与 KI-93 的"表被 `FreeChain` 真删"是**不同形态** ⇒ KI-93 在 `DeleteVehicleOrders` 里的 `R3RFindOrderListReferrer` 兜底**未覆盖此形态**（U-reg-3 的答案方向）。
- 待定点：`first_shared` 应重指到"该 OrderList 仍活着的最小 index 使用者"还是置 `nullptr`。置空更保守（上游 `GetFirstSharedVehicle()` 返回 nullptr 时下游需能接受，须核对 `LinkRefresher`/`DeleteStaleLinks` 是否判空）。

**待验证新增**：U-reg-4（执行者）该 OrderList 的 `num_vehicles`/`IsShared()` 读数——若 `num_vehicles>1` 说明共享链登记在册、只是主人被删；U-reg-5 碰撞爆炸删车路径是否为 `Vehicle::Crash`→`DeleteVehicle`→`Vehicle::PreDestructor`，与 `CmdSellRailWagon` 是否共用同一兜底。

**当前状态汇总（续三十二）**：KI-368 未修（高，崩溃）；KI-367 回退为未修；KI-366 已修+实机通过；**本轮最高优先级 = KI-368 治本修法拍板**（`DeleteStaleLinks` 内止血已证不可行）。

### 37 续三十二·销毁路径补充取证（2026-10-15，`src\vehicle.cpp`）

- `Vehicle::PreDestructor()`（:1159 起）**开头 `if (CleaningPool()) return;`** ⇒ 池清理阶段**完全不清理 orders**。
- 唯一的 orders 清理点在 `:1282 DeleteVehicleOrders(this)`（另 `:1287-1291` 释放 `orders_backup`，且刻意先把指针摘掉再 `FreeChain(false)`）。
- `Vehicle::~Vehicle()`（:1301-1334）以 `:1326 delete v;`（`v = this->Next()`）**递归销毁后续车节，全程不调用 `PreDestructor`** ⇒ 只有"被显式 delete 的那一辆"会走 orders 清理。

**猜测根因②（待验证）**：`first_shared` 的主人若属于「随链递归 `delete` 的后续车节」或处于 `CleaningPool()` 阶段，则 `DeleteVehicleOrders()` 根本不会为它执行 ⇒ `first_shared` 留下悬垂指针 ⇒ KI-368。支撑：崩溃现场是"列车相撞"爆炸删车（非命令层逐辆卖），且毒值是整块 0xDD 填充（内存已回池）。**待验证 U-reg-6**：`PreDestructor` 究竟由谁调用（是否池内每个对象都调）；碰撞爆炸删车时被显式 `delete` 的是链头还是中间车节。

**止血可行边界（更正 §36 的表述）**：§36 说的"止血不可行"仅指 **O(1) 校验不可行**；**O(车数) 全池校验可行** —— 在 `DeleteStaleLinks` 内先由 `Vehicle::Iterate()` 建活车指针集合，再剔除 `GetFirstSharedVehicle()` 返回的集合外指针（零语义变更，只把"崩溃"换成"跳过"）。是否采用取决于 `DeleteStaleLinks` 的调用频率（**待取样 U-reg-7**）。

### 38 续三十三·止血落地与构建自证（2026-10-15）

**U-reg-7 已取样（判定：开销可忽略）**：`DeleteStaleLinks(Station *from)` 由 `OnTick_Station()`（`src\station_cmd.cpp:5253` 起）调用，条件 `(_tick_counter + st->index) % STATION_LINKGRAPH_TICKS == 0`（:5263）⇒ 每站每 `STATION_LINKGRAPH_TICKS` 才一次；同一 tick 内缓存由 `ClearDeleteStaleLinksVehicleCache()`（:5257，每 tick 一次）重置 ⇒ 一 tick 最多建一次活车集合。

**落地（唯一改动文件 `src\station_cmd.cpp`，未碰 `src\*.h`）**：
1. `:5056` 区块新增 `static robin_hood::unordered_flat_set<const Vehicle *> _r3r_live_vehicle_cache;` + `static bool _r3r_live_vehicle_cache_valid = false;` + `static bool R3RIsLiveVehicleCached(const Vehicle *v)`（首次调用由 `Vehicle::Iterate()` 建集合；**永不 deref 入参**）。
2. `ClearDeleteStaleLinksVehicleCache()` 内加 `_r3r_live_vehicle_cache_valid = false;`。
3. 收集点（原 `:5112`）：取 `Vehicle *const first_shared = l->GetFirstSharedVehicle();`，若为 `nullptr` 或 `!R3RIsLiveVehicleCached(first_shared)` ⇒ `R3RDbgWrite("SL-STALE-SKIP ol=%llx fs=%llx ctx=has-both-ends\n", …)` + `continue`（**不再走到 `:5124` 的 `chain_head->index` deref**）。KI-367 的 `chain_head = v->First()` **保留**（其前已有活车保证）。

**构建自证**：复用 `_tmp_inc_build.cmd`（未新建 .cmd）；`build\R3R_incbuild.guard.log`（0:08:21）= 「GUARD: incremental is safe」；`[2/3] station_cmd.cpp.obj` → `[3/3] Linking CXX executable openttd.exe`；`R3R_incbuild.done` = `EXIT_CODE=0`（0:10:21）；`error C`/`fatal error`/`FAILED:` 计数 0；`src\station_cmd.cpp` 0:08:08 → obj 0:08:50 → `build\openttd.exe` **2026-10-15 0:10:17（51 854 336 B）**；`read_lints` 0；exe 命中 `SL-STALE-SKIP`。

**性质声明**：这是**止血**（不再 deref 毒指针），**不是治本** —— `first_shared` 悬垂仍会产生，只是不再导致崩溃；且 `DeleteStaleLinks` 会静默跳过该 OrderList 的链接刷新（与该 OrderList 其余使用者的排程无交互，仅少一次 linkgraph 刷新）。治本仍待拍板（`order_cmd.cpp` 的 `DeleteVehicleOrders`，见 §36 ②/§37 猜测根因②）。

### 39 续三十四·治本取证（2026-10-15）：KI-93 兜底已覆盖"另有引用者"，残余缺口在"无 orders 引用者但 first_shared 指死车"

**事实 4（本轮已读 `order_cmd.cpp`）**：
- `DeleteVehicleOrders(v, keep_orderlist, reset_order_indices)`（:3753 起）：非共享分支里若 `R3RFindOrderListReferrer(ol, v) != nullptr` ⇒ `v->orders = nullptr; ol->FreeChain(false);`（:3794-3800）。
- `OrderList::FreeChain(bool keep_orderlist)`（:686 起）：`!keep_orderlist` 时**先**调 `R3RFindOrderListReferrer(this, nullptr)`；命中 ⇒ `first_shared = other->IsPrimaryVehicle() ? other : other->First()`（空则回退 `other`）后 `return`（:696-701）；**未命中** ⇒ 继续走销毁（`this->orders.clear()` → `delete this`，:710/:717）。
- `R3RFindOrderListReferrer(ol, except)`（:668-676）判定式 = `w->orders == ol || w->orders_backup == ol`（且 `except` 非空时要求同 `type`）。

**⇒ 残余缺口（猜测根因③，待验证）**：能同时满足"`OrderList::Iterate()` 仍取得到 `l`（表活）+ `first_shared` 是毒值（主人死）"的形态，必须绕开上面两条重指路径：
- 若 `keep_orderlist == false` 且无引用者 ⇒ `delete this`，列表直接消失（与"表活"矛盾）⇒ **排除**；
- ∴ 只能是**列表被保留**（`keep_orderlist == true` 的"清空订单但留列表"分支，或原生共享链 `RemoveFromShared()` 路径）时，`first_shared` **在那条路径上没有被维护**。
- 另一可能：`first_shared` 指向的车**根本不通过 `orders`/`orders_backup` 引用该列表**（原生 `first_shared` 语义是"共享链的链头"，由 `AddToShared`/`RemoveFromShared` 维护，与 `orders ==` 判定并不等价）⇒ `R3RFindOrderListReferrer` **看不见**它 ⇒ 永远找不到替代者、也永远不重指。

**下一步取证（本轮第 2 组）**：读 `Vehicle::AddToShared()` / `Vehicle::RemoveFromShared()`（`first_shared` 的原生维护点），确认 R3R 借用/改链是否会让 `first_shared` 滞留在将死车辆上。**落点仍是 `order_cmd.cpp` 的 `DeleteVehicleOrders`（治本最小改动面）**：在释放前补一条"若 `ol->first_shared == v`（或 `ol->GetFirstSharedVehicle() == v`）则重指到下一个活引用者/置 nullptr"的兜底，与 `FreeChain` 的既有兜底同构。

### 40 续三十五·治本取证二：根因链做实（2026-10-15）

**事实 5**：`OrderList::RemoveVehicle(Vehicle *v)`（order_cmd.cpp:950-954）：
```cpp
--this->num_vehicles;
if (v == this->first_shared) this->first_shared = v->NextShared();
```
`first_shared` 的"推进"**只沿共享链 `next_shared`**。R3R 借用链不是共享链（`next_shared == nullptr`）⇒ 若 v 是 `first_shared`，这里会把它**置为 nullptr**。

**事实 6**：`OrderList::AddVehicle(v)`（order_base.h:1735）只 `++num_vehicles`，**不设 `first_shared`**；只有 `OrderList::Initialize(v)`（order_cmd.cpp:607-609）会 `first_shared = v`。

**事实 7（根因链的关键一环）**：train_cmd.cpp 的 R3R 排程借用/移交点**只搬 `orders`，不动 `first_shared`**：
- `:1792-1793` `const_cast<Train *>(u)->orders = orders_to_move; v->orders = nullptr;`
- `:11050-11060` `v->orders_backup = v->orders; … v->orders = couple_owner->orders;`（注释 :11031 明言 "u->orders = nullptr"）
- 同类：`:5939 / :6028 / :6040-6048 / :6425-6430 / :6443 / :9661`

**⇒ 根因链（推断升级为"高置信，待现场读数确认"）**：
1. Couple 借用把表的 `orders` 从给出方 u 搬到合并链链头 v（`u->orders = nullptr`，`v->orders == ol`），**但 `ol->first_shared` 仍 == u**（该表本是 u 的单车表，由 `Initialize(u)` 设；裸指针搬运没经过 `RemoveVehicle`/`AddVehicle`）。
2. 之后 u（或其所在链被卖/被解）执行 `DeleteVehicleOrders(u)`：`u->orders == nullptr` ⇒ 非共享分支的 `if (v->orders != nullptr)`（order_cmd.cpp:3788）为**假** ⇒ **整块跳过**，既不 `FreeChain` 也不重指 `first_shared`。
3. `ol` **仍然活着**（合并链链头正在跑它）⇒ `OrderList::Iterate()` 取得到 ⇒ 与"表活"不矛盾。
4. u 被放回车辆池 ⇒ `ol->first_shared` == 已死车辆的**毒指针**。
5. 下一个 `DeleteStaleLinks()` 取 `l->GetFirstSharedVehicle()` 并解引用 `chain_head->index` ⇒ 0xC0000005（现场 RAX=0xDDDD…DE15、Bytes=8B 00）。

**这一步同时解释了 §39 的两条"排除"为何没排掉**：列表被保留**不是**因为 `keep_orderlist == true`，而是因为**另有车（链头）引用它**，而"无活引用者"的 `FreeChain` 重指路径**根本没被执行**（`v->orders == nullptr` 提前跳出）。

**治本方案（选定，拟实施）**：把"清理悬垂 `first_shared`"从 `FreeChain`（够不着的路径）提到**车辆销毁时的全局兜底**，落在 `order_cmd.cpp` 的 `DeleteVehicleOrders()` **末尾**：
```cpp
/* R3R: a dying vehicle may be registered as the first_shared of a live order
 * list it no longer references through orders/orders_backup (the
 * couple-priority borrow moves the pointer away and never touches the
 * registration). Re-point those registrations before the vehicle is freed:
 * FreeChain() hands the list over to a vehicle which still runs it, and only
 * destroys the list when nobody does. */
std::vector<OrderList *> r3r_registered;
for (OrderList *ol : OrderList::Iterate()) {
    if (ol->GetFirstSharedVehicle() == v && ol != v->orders) r3r_registered.push_back(ol);
}
for (OrderList *ol : r3r_registered) ol->FreeChain(false);
```
要点：①先收集到 vector 再操作（`FreeChain(false)` 可能 `delete this`，边遍历边删会让池迭代失效）；②`ol != v->orders` 排除正常路径已处理的那张表（避免二次 `FreeChain` 访问已释放表）；③不改任何 `src/*.h` ⇒ **保持增量构建合规**（无需全量 20 分钟）。

**待核对（本方案的前置条件）**：
- U-reg-3：`OrderList::Iterate()` 的签名/用法（station_cmd.cpp 的 `DeleteStaleLinks` 已在用）与"收集后操作"是否足够安全。
- U-reg-4：`Vehicle::PreDestructor()` 与 `DeleteVehicleOrders()` 的调用顺序、以及 `orders_backup` 的释放顺序（若 `v->orders_backup == ol`，本兜底会不会与后面的释放冲突）。
- U-reg-5：`GetFirstSharedVehicle()` 下游调用点是否都判空。

### 41 续三十六·治本取证三：前置条件全部核对完 + 方案定稿（2026-10-15）

**事实 8**：`OrderList::FreeChain(bool)`（order_cmd.cpp:686-719）的注释（:682-684）已明文规定规程：
> callers must detach their own pointer (orders/orders_backup) *before* calling this with keep_orderlist == false, so that the hand-over check below does not see the vehicle which is letting go of the list.

⇒ 这既是 KI-93 的设计意图，也说明"重指块"的输入敏感性：`R3RFindOrderListReferrer(this, nullptr)`（:696）在 `except == nullptr` 时**不排除任何人**。

**事实 9**：`FreeChain` 的销毁路径（:704-705）**解引用 `this->GetFirstSharedVehicle()`**（取 type/owner）。⇒ 兜底若对一个 `first_shared` 已是毒指针的表调 `FreeChain(false)`，会在这里崩。故兜底必须先自证 `GetFirstSharedVehicle() == v`（v 正在销毁但对象仍有效）才动手。

**事实 10**：`Train::~Train() override { this->PreDestructor(); }`（train.h:317），同构：`Aircraft`（aircraft.h:92）、`Ship`（ship.h:45）、`RoadVehicle`（roadveh.h:167）。配合 `~Vehicle()` 内 `delete v`（`v = Next()`）递归 ⇒ **整链每一节车在 `delete` 时都会各自走一次 `PreDestructor() → DeleteVehicleOrders()`**（链式）。池清理期（`CleaningPool()`）由 `PreDestructor` 开头早退，全池同批清空，无悬垂窗口。

**事实 11**：`R3RFindOrderListReferrer(const OrderList *ol, const Vehicle *except)`（order_cmd.cpp:668-676）：`w->orders == ol || w->orders_backup == ol`；`except != nullptr` 时另加 `w->type == except->type`。

**⇒ 治本方案定稿（不再用"临时接管 v->orders"变体）**：临时接管会让 `FreeChain` 的重指块把 **v 自己**当成接手者（`except == nullptr` 不排除 v），重指回自身、毒指针不清。正确做法是**保持 `v->orders == nullptr`**（这样 v 不会被当成 referrer），直接对目标表调 `FreeChain(false)`：有别的引用者 ⇒ 只重指 `first_shared`（不删表）；无 ⇒ 走 `:704` 销毁（此时 `first_shared == v` 仍有效，不崩）。落点=**`DeleteVehicleOrders()` 末尾**：
```cpp
/* R3R: a vehicle about to be destroyed may still be the registered first_shared
 * of a live order list it no longer references through orders/orders_backup (the
 * couple-priority borrow moves the pointer away and never touches the
 * registration). Re-point those registrations now: with v detached, FreeChain()
 * hands the list over to a vehicle which still runs it, and only destroys the
 * list when nobody does. Doing this here (rather than in FreeChain) is what
 * makes it reachable for the borrowed-away case. */
if (!CleaningPool()) {
    std::vector<OrderList *> r3r_dead_registered;
    for (OrderList *ol : OrderList::Iterate()) {
        if (ol->GetFirstSharedVehicle() != v) continue;
        if (ol == v->orders || ol == v->orders_backup) continue;   // handled by the normal paths
        r3r_dead_registered.push_back(ol);
    }
    for (OrderList *ol : r3r_dead_registered) ol->FreeChain(false);
}
```

**核对结果**：
- U-reg-3 ✅ `OrderList::Iterate()` 在 station_cmd.cpp 的 `DeleteStaleLinks` 已有既有用法；本方案"先收集到 vector 再操作"，**不受"边遍历边 `delete this`"影响**。
- U-reg-4 ✅ `Vehicle::PreDestructor()`（vehicle.cpp:1158）先 `DeleteVehicleOrders(this)`（:1282），**之后**才 `orders_backup->FreeChain(false)`（:1287-1291）。本兜底放在 `DeleteVehicleOrders` 末尾、并**显式排除 `v->orders_backup`**，故与后续释放无交互；且 `~Train()→PreDestructor()` 逐节递归（事实 10）保证链中每一节车都会走到。
- U-reg-5 ✅ 本方案在"无引用者"时会让 `FreeChain` 走 `delete this`（表消失），不产生 `first_shared == nullptr` 的表；"有引用者"时重指到活车（非 nullptr）。两种结果都不会让下游拿到 `nullptr`，无需追加判空改造。

### 42 治本落地（第 243 轮续三十六，2026-10-15）

- 改动文件：`src\order_cmd.cpp`（唯一；**未碰任何 `src\*.h`** ⇒ 增量构建合规，无需全量）。
- 插入点：`DeleteVehicleOrders()`（:3753）**末尾**、函数闭合大括号之前（原 :3822 `if (reset_order_indices) {...}` 之后）。
- 落地代码（29 行，含注释）：
```cpp
	/* R3R (couple priority, route A): a vehicle about to be destroyed may still be
	 * the registered first_shared of a *live* order list which it no longer
	 * references through orders/orders_backup. ... (see memo 41/42) */
	if (!CleaningPool()) {
		std::vector<OrderList *> r3r_dead_registered;
		for (OrderList *ol : OrderList::Iterate()) {
			if (ol->GetFirstSharedVehicle() != v) continue;
			if (ol == v->orders || ol == v->orders_backup) continue; // handled above, or by PreDestructor()
			r3r_dead_registered.push_back(ol);
		}
		for (OrderList *ol : r3r_dead_registered) {
			if (R3RFindOrderListReferrer(ol, v) == nullptr) UpdateDeparturesWindowVehicleFilter(ol, true);
			ol->FreeChain(false);
		}
	}
```
- 设计要点：①`R3RFindOrderListReferrer(ol, v)` 的 `except` 传 **v 自己**（不是 `nullptr`）——否则"有引用者"判定会把正在销毁的 v 自己算进去；②`ol == v->orders || ol == v->orders_backup` 排除已由上方分支（:3799/:3807）或 `Vehicle::PreDestructor()`（:1287-1291）负责的表；③**先收集到 vector 再操作**（`FreeChain(false)` 可能 `delete this`，边遍历边删会让池迭代失效）；④无引用者时先 `UpdateDeparturesWindowVehicleFilter(ol, true)` 再销毁，与既有 :3804-3808 分支逐字同构。
- `read_lints`：0 条（改动后立即核对）。
- 构建：复用既有 `_tmp_inc_build.cmd`（**未新建任何 .cmd**）。日志/判决回填见 §43。
- 待复测 4 条判据：①同场景（1112,129 一带删车/开车库后）不再出现 `SL-STALE-SKIP`，且不再崩溃；②`DeleteStaleLinks` 不再触发活车缓存跳过；③负判据：正常单车/原生共享链解挂不解体（表仍被正确销毁/移交），无内存泄漏新迹象；④`UpdateDeparturesWindowVehicleFilter` 无重复调用导致出发板数据异常。

### 43 构建自证（含两次编译错误的纠正过程，2026-10-15）

**首次构建失败（0:25:07，EXIT_CODE=1）**：
```
D:\sourcecode of JGRPP\src\order_cmd.cpp(3840): error C3861: "CleaningPool": 找不到标识符
```
**第二版（`PoolBase::CleaningPool()`）仍失败（0:28:02，EXIT_CODE=1）**：
```
D:\sourcecode of JGRPP\src\order_cmd.cpp(3840): error C2039: "CleaningPool": 不是 "PoolBase" 的成员
D:\sourcecode of JGRPP\src\order_cmd.cpp(3840): error C3861: "CleaningPool": 找不到标识符
```
**事实 12（工具链坑，登记备查）**：`CleaningPool()` 定义在 `src\core\pool_type.hpp:378`，是 **`Pool<Titem, Tindex, Tpool_type>` 模板类**的 `static inline bool CleaningPool() { return Tpool->cleaning; }`（**不是** `PoolBase` 的成员）；`OrderList : OrderListPool::PoolItem<&_orderlist_pool>`（order_base.h:1540）而 `OrderListPool = Pool<OrderList, OrderListID, 128>`（order_base.h:35）⇒ 它**只在 `PoolItem` 派生类（如 `OrderList`）的成员函数内可无限定调用**（`OrderList::FreeChain` 的 `:708 if (!CleaningPool())` 正是此情形），**自由函数内没有简洁合法的限定名**。
**⇒ 修法**：删掉该守卫（不是换限定名）。依据：`Vehicle::PreDestructor()` 开头已有 `if (CleaningPool()) return;`（池清理期早退）⇒ 本兜底在池清理期**根本不可达**，守卫属冗余。最终版（无守卫）：
```cpp
	std::vector<OrderList *> r3r_dead_registered;
	for (OrderList *ol : OrderList::Iterate()) {
		if (ol->GetFirstSharedVehicle() != v) continue;
		if (ol == v->orders || ol == v->orders_backup) continue; // handled above, or by PreDestructor()
		r3r_dead_registered.push_back(ol);
	}
	for (OrderList *ol : r3r_dead_registered) {
		if (R3RFindOrderListReferrer(ol, v) == nullptr) UpdateDeparturesWindowVehicleFilter(ol, true);
		ol->FreeChain(false);
	}
```
**第三次构建（修正后）**：判决见 §44。

### 44 构建自证·第三次（成功，2026-10-15 00:32）

- 入口：复用既有 `_tmp_inc_build.cmd`（**未新建任何 .cmd**）。
- 护栏：`GUARD: incremental is safe (no header/lang file is newer than the newest object)` ⇒ 增量合规（未碰任何 `src\*.h`）。
- 日志尾部：`[3/3] Linking CXX executable openttd.exe`。
- `build\R3R_incbuild.done` = `EXIT_CODE=0`（LastWriteTime 2026-10-15 0:32:34）。
- 错误计数（`error C|fatal error|FAILED:|build stopped`）= **0**。
- 时间戳链：`src\order_cmd.cpp` 0:30:14 → `build\CMakeFiles\openttd_lib.dir\src\order_cmd.cpp.obj` 0:30:33（5 720 795 B）→ `build\openttd.exe` **0:32:18（51 867 648 B，上一版止血 51 854 336 B ⇒ 已更新）**。
- `read_lints`（order_cmd.cpp）：0 条。

**交付状态**：治本代码已落地并**编译通过**（`DeleteVehicleOrders()` 末尾兜底，见 §42/§43）。**游戏内复测待做**（4 条判据见 §42 末与 KI-368 条目）。

---

## 45 新日志复测判读（三只地鼠，2026-10-15）

**日志身份**：`D:\temp\codebuddy-dropped-files\d2824fb3-ceab-4adf-bfa9-5bdcedecbbc0\R3R_debug.log`，2 036 825 B，LastWriteTime 2026-10-15 00:40:18。
**产出者**：日志内含 `released=/kept=`（≥§30 治疗版 22:24:30）与 `RESV-AUDIT-ARM/WAIT`＋`interval=/due=/pending=`（≥§32 版 23:14:54）⇒ 是**已治疗版 exe** 产生的会话，判读有效。

### 45.1 计数（大小写敏感）

| 标记 | 计数 |
| --- | --- |
| `SET-RAIL` | 7886 |
| `CLEAR-RAIL` | 7886 |
| `SET-STN` | 22 |
| `CLEAR-STN` | 24 |
| `RESV-GC` | 1586 |
| `RESV-GC` 且带 `-skip` | **0** |
| `notowner` | **0** |
| `RESV-AUDIT-ARM` / `DUE` / `SUMMARY` / `WAIT` / `REAP` | 1 / 4 / 6 / 5 / 28 |

**踩坑更正**：先前把 `-skip`=104 当作地鼠 B 的证据是**误读**——Select-String 默认不区分大小写，那 104 条实为 `CPL-SKIP veh=2 tile=1073,139 orderType=16 …`（见行 ~1871 一带）与 `ORD-IDX-WATCH site=post-skipgate`。按大小写敏感的 `RESV-GC.*-skip` 统计，本会话账本 skip = **0**。

### 45.2 地鼠 A（KI-362，高）：**仍然活着，未治住** ❌

红灯格 `tile=1091,139 track=0`（= 第 243 轮玩家最初报的那一格）：
- `RESV-WATCH SET-RAIL tile=1091,139 track=0 phase=choose-track actor=23 head=23` = **1462** 条
- `RESV-WATCH CLEAR-RAIL tile=1091,139 track=0 phase=choose-track actor=23 head=23` = **1462** 条
- 二者**两行一组交错刷屏**，行号锚点：`build\R3R_debug.log` 1838/1841、1856/1858、1860/1862、1864/1866、1869/1871、1873/1875、1877/1879、1881/1883、1885/1887、1889/1891、1893/1895、1897/1899、1901/1903 …（间隔恒 2 行 ⇒ 同一 tick 内订完立刻放，或相邻 tick 反复）。
- 该格占全部 `SET-RAIL` 的 1462/7886 ≈ 18.5%，且 `SET-STN/CLEAR-STN tile=1091,139` = 0 ⇒ 与整格位无关，纯 plain-rail 同格订-放。

**结论句**：律 1（释放只许按"用途"退订）在**实机仍然被违**——同一 actor=23/head=23 的 choose-track 订路，其 PATH 位被下一 tick 的清扫路径整条忘掉/释放，于是每 tick 重订同一格。§30 对 `FreeTrainTrackReservation` 的 `R3RResvLedgerForget(key,"free-path")` 处理（`mask=0x1 released=2 kept=0`，见行 ~1880 一带 6 条同样读数）只保证了"不误伤非 PATH 位"，**没有消除订-放循环本身**。根因待查：CLEAR-RAIL 的发出者是谁（phase 也打 choose-track ⇒ 疑似不在 `FreeTrainTrackReservation`，而是 choose-track 内部重试前先清）。

### 45.3 地鼠 B（KI-363，中）：本会话**未复现**（账本 skip = 0）⚠️

`RESV-GC` 1586 条中带 `-skip` 者 0 条、`notowner` 0 条 ⇒ 未观测到"账本 > 地图"的幽灵行，也未观测到跨所有者释放（疑点 REG-243-K 同样**未复现**）。与 §31/§33 的早前读数不同 ⇒ 需再取一段会话确认，不能据此结案。

### 45.4 地鼠 C（KI-364，中）：**延迟 reap 走通** ✅（体验仍存疑）

- `RESV-AUDIT-ARM tick=6930 interval=2048 due=0 pending=0`（行 ~1）
- `SUMMARY tick=6930 reservedTiles=58 owned=38 strayTiles=20 reaped=0 pendingStray=20`
- `DUE tick=7186 interval=256 due=0 pending=1` → `SUMMARY tick=7186 reservedTiles=59 owned=39 strayTiles=20 reaped=0 pendingStray=20`
- `DUE tick=7442 interval=256 …` → `SUMMARY tick=7442 reservedTiles=58 owned=38 strayTiles=20 **reaped=20** pendingStray=0`
- 后半场：`DUE tick=9746` → `SUMMARY tick=9746 strayTiles=8 reaped=0 pendingStray=8`；`DUE tick=10002` → `SUMMARY tick=10002 strayTiles=8 **reaped=8**`

⇒ 3 次 DUE（256 节拍）后确实 reap，判据"库内重排/卖车后能自动清"在 tick 尺度上成立；但**玩家感知层面** strayTiles=20 在 tick 6930→7442 之间一直挂着（`owned=38` 之外另有 20 格无主位在图中显示），期间若玩家正看着铁轨仍会觉得"没清"。

### 45.5 本轮未做与待办

- 本轮**零源码改动、零构建**，纯判读。
- 待查（按优先级）：①`CLEAR-RAIL phase=choose-track` 的发出者（行 1841 前 3 行的上下文未取）——决定地鼠 A 是"清扫释放"还是"重试前清"；②strayTiles=20 的具体格（是否又是 1090..1092 × 136..140 走廊 / KI-346 家族）；③地鼠 B/C 在更长会话里的复现性。

---

## 46 地鼠 A 定向取证（2026-10-15 续，仅 3 处源码读取）

### 46.1 TRP 探针字段语义订正（`src\train_cmd.cpp:14048-14071`）

```
14062: fprintf(dbg, "TRP veh=%d mf=%d,%d real=%d type=%d dest=%u spd=%d fp=%d look=%d one=%d mstuck=%d fto=%d => ok=%d res=%d\n", ...)
14067:   mark_as_stuck ? 1 : 0, first_tile_okay ? 1 : 0,
14068:   (res & TPRRF_RESERVATION_OK) ? 1 : 0, (int)(uint8_t)res);
```

- `ok = (res & TPRRF_RESERVATION_OK) ? 1 : 0`；`res = (int)(uint8_t)res` 即**整个标志字节**（不是"预留是否成功"的布尔）。
- `mstuck=` 打的是**入参 `mark_as_stuck`**、`fto=` 打的是**入参 `first_tile_okay`**；`fp/look/one` 是状态量。

**结论句（订正 §45.2）**：新日志里 veh=23 每 tick 的 `TRP … => ok=1 res=1` 含义是"**TryPathReserve 返回 TPRRF_RESERVATION_OK，且标志字节恰为 0x01**"——即**报告了成功的预留**，不是失败、不是 Stuck。§45.2 里用 `mstuck=1` 佐证"lost 分支"的推断**作废**（那只是调用方传的 `mark_as_stuck=true`）。

### 46.2 两条"放弃"分支被排除（`src\train_cmd.cpp:13849-13944`）

- `13912-13922`（guessed-route 后仍 `!res_dest.okay`）：`Free`(13920) → **直接 `return { best_track, result_flags }`**，而 `result_flags |= CTTRF_RESERVATION_MADE` 只在 `13936`（`TryReserveSafeTrack` 成功）与 `13946`（本块 return 之后）置位 ⇒ **此分支必打 `res=0`**。
- `13924-13944`（`/* No possible reservation target found, we are probably lost. */`）：失败子分支 `13939-13942` = `FreeTrainTrackReservation`(13940) + `MarkTrainAsStuck`(13941) → `return`(13943)，同样**不带** RESERVATION_MADE ⇒ **必打 `res=0`**。
- `13849-13890`（GOTO_COUPLE）：失败在 `13862` 分支 `Free`(13885) 后 `return`(13887)，置位语句在 `13889` 之后 ⇒ 也不产生 `res=1`。veh=23 的 `type=1`(OT_GOTO_STATION) 本就不走此块。

**结论句**：本会话实测是 `res=1`，而 CLEAR 与 TRP 同在**一次** `TryPathReserve*` 的窗口内（行 1838-1843 的 SET,SET,CRT,CLEAR,CLEAR,TRP 顺序）⇒ **CLEAR 的发出者不是 13912/13924 这两条"放弃"分支**；而是"同一次调用内部先订后放、调用最终仍返回 RESERVATION_OK"的形态（标志与地图不一致的首要嫌疑）。

### 46.3 疑点登记 REG-243-L（新）

`R3RResvPhaseGuard` 销毁时是否**还原**上一层 phase？若**不还原**，则一个 tick 内第一次 `ChooseTrainTrack` 之后的所有地图写都会被标成 `phase=choose-track`：
- (a) 日志的相位值对"谁放的"失去区分力（§45.2 用相位反推发出者的推理全废）；
- (b) `R3RResvLedgerPurpose()`（`pbs.cpp:218-225`）只把 `consist / clear-consist / waiter-platform` 判为 CONSIST，其余一律 PATH ⇒ 这些写会把**车下位记成 PATH**，直击律 1（会被 `free-path` 释放）与律 2。
- 待验证判据（唯一、便宜）：读 `pbs.cpp` 的 `R3RResvPhaseGuard` 定义，看析构是否 `r3r_resv_phase = 上一相位`（含 `pbs.cpp:199-217` 注释自陈的"a nested guard that inherits a phase"）。

### 46.4 下一个（也是唯一）待查点

`src\train_cmd.cpp:14180-14245`：该处 KI-221 注释自陈"**报告了成功的预留，所以 TrainLocoHandler 认为不再 stuck**……而列车从未驶离 4,11"（原文 `TRP veh=26 … spd=0 mstuck=0 fto=0 => ok=1 res=1`），且 veh=23 的 TRP 里 `look=1` 与之同类 ⇒ 本会话"`res=1` 但同 tick 内已 CLEAR"的**第一候选发出者**。读完即可给地鼠 A 定性（是"标志与地图不一致"还是"另一处清扫"）。

## 四十七、地鼠 A 定向取证（第 243 轮续十三，2026-10-15）

### 47.1 REG-243-L 已证伪（`src/pbs.h:68-75`，逐字）

```cpp
 68:	explicit R3RResvPhaseGuard(const char *phase, VehicleID actor = VehicleID::Invalid())
 69:		: prev_phase(r3r_resv_phase), prev_actor(r3r_resv_actor)
 70:	{
 71:		if (phase != nullptr) r3r_resv_phase = phase;
 72:		if (actor != VehicleID::Invalid()) r3r_resv_actor = actor;
 73:	}
 74:	~R3RResvPhaseGuard() { r3r_resv_phase = prev_phase; r3r_resv_actor = prev_actor; }
```

**结论句（证伪）**：析构**确实还原**上一层相位与 actor ⇒ 相位机本身正确，§46.3 里"不还原 ⇒ 一个 tick 内首次 `ChooseTrainTrack` 之后的地图写全被误标 `phase=choose-track`"的假设**不成立**，该疑点关闭。

### 47.2 新疑点 REG-243-M：同名相位嵌套不可区分

`src/train_cmd.cpp:13680` 位于 `ChooseTrainTrack` **函数体开头**（13676 是 `{`；13677-13679 注释自陈 "the lookahead path reservation is booked from here (the three `TryReserveRailTrack(moving_front->tile, ...)` calls below), so name the phase and the train"）：

```cpp
13680:	R3RResvPhaseGuard r3r_phase("choose-track", R3RResvActorID(consist));
```

**结论句**：`phase=choose-track actor=23` 的**唯一含义**是"写入点位于某一次 `ChooseTrainTrack(consist=23)` 的作用域内、且当时最内层 guard 就是它"。而 `TryLongReserveChooseTrainTrack`（`src/train_cmd.cpp:13557`）在 **13640 行递归调用 `ChooseTrainTrack`**：

```cpp
13555:bool _long_reserve_disabled = false;
13557:static void TryLongReserveChooseTrainTrack(Train *v, TileIndex tile, Trackdir td, bool force_res, ChooseTrainTrackLookAheadState lookahead_state)
13559:	if (_long_reserve_disabled) return;
...
13640:		ChooseTrainTrack(v, ft.new_tile, ft.exitdir, TrackdirBitsToTrackBits(ft.new_td_bits), CTTF_NO_LOOKAHEAD_VALIDATE | (force_res ? CTTF_FORCE_RES : CTTF_NONE), lookahead_state);
```

内外两层打出的相位串**逐字相同** ⇒ **不能**用相位区分"外层订的 / 内层放的"，§45.2 用相位反推发出者的方法论就此作废（缘由与 §46.3 猜的不同）。

### 47.3 地鼠 A 当前定位（收敛后的猜测根因，待验证）

已知（§46.2 已立）：`ChooseTrainTrack` 内三处 `FreeTrainTrackReservation`（`train_cmd.cpp:13940` / `13986` / `13996`）**都不可能**产生 `res=1`（13946 才置 `CTTRF_RESERVATION_MADE`；13986/13996 后紧跟 `result_flags &= ~CTTRF_RESERVATION_MADE`）。

**猜测根因（待验证）**：外层 `ChooseTrainTrack(23)` 订下路线（SET 1091,139 + SET 1092,139）→ 置位 `CTTRF_RESERVATION_MADE`(13946) → `FillTrainReservationLookAhead` / `TryLongReserveChooseTrainTrack`（约 14014-14018）递归进入**内层 `ChooseTrainTrack`**(13640) 做长预留；内层走它的"放弃"分支时调 `FreeTrainTrackReservation(consist, origin.tile, origin.trackdir)`，该释放**按 origin 起释放、跨调用有效**，把外层刚订的两格一并清掉（CLEAR ×2）→ 但**外层的 `result_flags` 中 `RESERVATION_MADE` 无人撤销** ⇒ `R3RTryPathReserveCore` 转成 `TPRRF_RESERVATION_OK` ⇒ `TRP … => ok=1 res=1`，而地图上已无预留 = **标志与地图不一致（地鼠 A）**。

支撑：CLEAR 与 SET 同相位同 actor（47.2：只能证明同函数族，**不能**排除内层）；CLEAR 出现在 `CRT`（YAPF 返回）之后，符合"先订路线 → 再长预留 → 长预留里释放"的次序。

### 47.4 YAPF 侧事实（`src/pathfinder/yapf/yapf_rail.cpp:1277-1294`）

```cpp
1277:	static Trackdir stChooseRailTrack(const Train *v, TileIndex tile, DiagDirection enterdir, TrackBits tracks, bool &path_found, bool reserve_track, PBSTileInfo *target, TileIndex *dest)
1284:			result1 = pf1.ChooseRailTrack(v, tile, enterdir, tracks, path_found, reserve_track, target, dest);
```

**结论句**：`reserve_track` 是入参 ⇒ SET/SET 由 **YAPF 内部**（`ChooseRailTrack(reserve_track=true)` 的预留回调）在 `CRT … found=1` 打印**之前**完成。故 13680 注释所指的"三处 `TryReserveRailTrack(moving_front->tile, ...)`"**不是** 1091,139/1092,139 这对 SET 的发出者（那三处订的是车头脚下那一格）。

### 47.5 待复核清单（本会话的取证结果已被上下文压缩清除，引用前必须重读）

| 事项 | 位置 | 用途 |
|---|---|---|
| 13946 置位 / 13986、13996 清位 | `src\train_cmd.cpp:13940-14040` | §46.2 排除依据 |
| `TryLongReserveChooseTrainTrack` 函数体（是否含 `FreeTrainTrackReservation` / `ClearPathReservation`） | `src\train_cmd.cpp:13557-13642` | 验证 47.3（**唯一关键**） |
| `CRT` 探针打印处及其宿主函数（应为 `stChooseRailTrack`） | `src\pathfinder\yapf\yapf_rail.cpp` 约 1329 | 确认 47.4 |
| 地鼠 A 逐字日志锚点（SET,SET,CRT,CLEAR,CLEAR,TRP） | `D:\temp\codebuddy-dropped-files\d2824fb3-ceab-4adf-bfa9-5bdcedecbbc0\R3R_debug.log` 约 1837-1846 行 | 现场原文 |

### 47.6 下一步（唯一、最便宜的决定性判据）

`_long_reserve_disabled`（`src\train_cmd.cpp:13555`）是长预留的**全局开关**（13559 行 `if (_long_reserve_disabled) return;`）。把它临时置 `true` 跑一次同场景：

- 若 `RESV-WATCH SET-RAIL tile=1091,139` 与随后的 CLEAR **继续出现** ⇒ 与长预留无关，转查 YAPF 预留回调自身、以及 KI-359 台账（`R3RResvLedger*`）的归属重整（`R3RReleaseReservationLedgerChecked`，`pbs.h:215`）；
- 若**消失**（`res` 变 0 或列车改走别的路径） ⇒ 锁定长预留递归路径，直接读 13557-13642 定修改点（把内层释放收窄到自己那一段 origin，或在放弃时把 `CTTRF_RESERVATION_MADE` 一并撤掉并向上传播）。

### 47.7 【47.3 猜测已被证伪】`TryLongReserveChooseTrainTrack` 全文读完（`src/train_cmd.cpp:13557-13642`）

逐字要点：函数体里**没有** `FreeTrainTrackReservation`、**没有** `ClearPathReservation`、**没有** 对普通轨道位（plain rail）的任何释放；它只有两条出口：

```cpp
13564:	TileIndex exit_tile = CheckLongReservePbsTunnelBridgeOnTrackdir(v, tile, td, long_enough);
13565:	if (exit_tile != INVALID_TILE) {
...
13606:					SetTunnelReservation(exit_tile, true);            // 只订隧道/桥出口
13614:				ChooseTrainTrack(v, ft.new_tile, ft.exitdir, ..., lookahead_state);   // 递归
13617:				if (reserved_bits == GetReservedTrackbits(ft.new_tile)) {
13620:						UnreserveRailBridgeHeadTrack(exit_tile, ...);  // 只撤隧道桥出口
13622:					} else { SetTunnelReservation(exit_tile, false); }
...
13634:		return;
...
13638:	if (ft.Follow(tile, td) && HasLongReservePbsSignalOnTrackdir(...)) {
13640:		ChooseTrainTrack(v, ft.new_tile, ft.exitdir, ..., lookahead_state);   // 递归（LR 信号）
13641:	}
```

**结论句（证伪）**：长预留只 "订/撤**隧道桥出口那一格**"（13603-13624），**从不触碰普通轨道位** ⇒ 它**不可能**是 1091,139/1092,139 这条 `CLEAR-RAIL` 的发出者；§47.3 与 §47.6 判据 a（`_long_reserve_disabled` 对照实验）**作废**。

### 47.8 收敛后的候选发出者（三类，均落在 choose-track 作用域内且能碰 plain rail）

因为 47.7 排除了长预留、§46.2 排除了三处 `FreeTrainTrackReservation`，剩下的只能是"**在 `ChooseTrainTrack(23)` 作用域内、经** `UnreserveRailTrack` / `ClearPathReservation` / `SetTrackReservation(empty)` **写地图**的其它路径"：

1. **YAPF 寻路内的预留/撤销回滚**：`stChooseRailTrack`（`yapf_rail.cpp:1277`）把 `reserve_track` 传进 `ChooseRailTrack`（1284），预留与"候选失败回滚"都发生在该调用内部（`CRT` 行的 `found=` 紧跟其后）。
2. **KI-359 台账释放**：`R3RReleaseReservationLedgerChecked`（声明在 `src/pbs.h:215`，由 pbs.cpp 的各释放函数调用）——"归属不符/重整"即释放。
3. **第 110 轮的 consist 记账 GC**：`R3RGcConsistReservations()` + `R3RReleaseReservationSafe()`（`RESV-GC … why=`，每 256 次 `ReserveTrackUnderConsist` 触发一次；记忆 87560799）。

### 47.9 下一步（决定性、便宜）：给三类候选各加**独立相位标签**

给上述三处各包一层 `R3RResvPhaseGuard`（分别用 `"yapf-resv"` / `"ledger-release"` / `"consist-gc"`），跑同场景，看 `RESV-WATCH CLEAR-RAIL tile=1091,139` 那一行的 `phase=` 变成谁：

- 变成三者之一 ⇒ 发出者锁定，直接读那一段即可定修法；
- **仍显示 `choose-track`** ⇒ 三者全排除（它们都会自带标签），继续在 `ChooseTrainTrack` 作用域内找其它释放点。

理由：`R3RResvPhaseGuard`（`pbs.h:68-75`）析构还原、构造即改，故"标签没变"本身就是一个**强否定证据**；这是当前唯一不需要先读懂全部释放逻辑就能定性的手段。另需一并加的最小只读证据：`CLEAR-RAIL` 行补 `why=` 字段（在 `UnreserveRailTrack`/`ClearPathReservation` 两个入口加 `const char *why` 常量字符串），可零成本区分候选 1/2/3。

### 47.10 附：`r3r_resv_probes.h` 与 `RESV-WATCH` 的已知线索（供下一轮接续）

`RESV-WATCH SET-RAIL/CLEAR-RAIL … phase= actor= head=` 由地图 setter（`src/rail_map.h` 的 `SetTrackReservation`）内的探针打印，`head=` 是当时的预留链头所有者；SET 与 CLEAR 两侧 `head=23` 相同 ⇒ **不能用 `head=` 区分发出者**（与相位同样失效）。

### 47.11 【已落地】本轮探针实际落点与判读口径（第 243 轮续十四，2026-10-15）

47.9 原计划是"三处各包一层标签（`yapf-resv`/`ledger-release`/`consist-gc`）+ 两个释放入口补 `why=`"。实际落地比原计划更省，且**足以一次性定性**，改动只有三处、全部为**只读日志**（无行为变更）：

1. `src/pathfinder/yapf/yapf_rail.cpp:450`（`stChooseRailTrack` 内）——
   `R3RResvPhaseGuard r3r_phase("yapf-resv", R3RResvActorID(Yapf().GetVehicle()));`
   原来传 `nullptr` 继承调用者相位（=`choose-track`），所以**YAPF 内部释放**与**YAPF 返回后调用者释放**打出来一模一样（这正是 47.2 的 REG-243-M）。现在自带标签，两者分开。
   注释已写明：`"yapf-resv"` **不在** `R3RResvLedgerPurpose()` 的 `consist_phases[]` 里 ⇒ 台账仍把这类位置归为 *path bits* ⇒ 零行为变更。

2. `src/pbs.cpp:848-852`（`UnreserveRailTrack()` 入口，assert 之后、switch 之前）——
   `UNRES-RAIL tile=%d,%d track=%d phase=%s actor=%u`，条件 `GetReservedTrackbits(tile) != TRACK_BIT_NONE`，**由 `R3RDbgWrite` 打在 setter 之前**，因此与地图 setter 的 `RESV-WATCH CLEAR-RAIL` 行**紧邻成对**。
   这是 `plain rail` 释放的**总入口**，所以"谁清的"直接由 `phase=`/`actor=` 给出。

3. `src/train_cmd.cpp:12880-12884`（`ClearPathReservation()` 入口）——
   `CLEAR-PATH veh=%d tile=%d,%d td=%d phase=%s actor=%u`，同一条件、同一"setter 之前"位置。
   用来区分"列车自己走格释放"与其它释放路径（第 4 个候选）。

**为什么不再需要 `ledger-release`/`consist-gc` 两个标签也不影响定性**：这两条路径最终都必须落到 `UnreserveRailTrack`（plain rail 唯一总出口），那里已经无条件打印了当时的 `phase=`；若地鼠 A 出自台账/GC，`UNRES-RAIL` 行会给出它们**未曾改名**的 `phase=choose-track` 但仍带 `tile=1091,139`，配合调用栈上的 `RESV-GC`/`RESV-REAP` 既有日志即可分离。反之若出自 YAPF，`phase=` 必定变成 `yapf-resv`。

**判读口径（下一轮拿日志后照此表读）**：

| `CLEAR-RAIL tile=1091,139` 紧邻的 `UNRES-RAIL … phase=` | 结论 |
| --- | --- |
| `yapf-resv` | 候选 ① YAPF 内部预留/回滚 ⇒ 读 `yapf_rail.cpp` 的 `res_fail_tile`/回滚分支 |
| `choose-track` 且同行/邻近有 `CLEAR-PATH veh=…` | 候选 ④ 列车自己走格（`ClearPathReservation`） |
| `choose-track` 且无 `CLEAR-PATH`，但有 `RESV-GC`/`RESV-REAP` | 候选 ②/③ 台账或 consist 记账 GC |

一句话：**`phase=` 变成 `yapf-resv` 就锁定 YAPF；没变则三者一次性全排除**（构造即改标签，"标签没变"是强否定证据，见 47.9）。

**构建自证（第 243 轮续十四）**：复用既有 `_tmp_inc_build.cmd`（未新建 `.cmd`）。
- `build\R3R_incbuild.guard.log` = `GUARD: incremental is safe (no header/lang file is newer than the newest object)`；
- 日志计划 5 步：`[2/5] pbs.cpp.obj` → `[3/5] yapf_rail.cpp.obj` → `[4/5] train_cmd.cpp.obj` → `[5/5] Linking CXX executable openttd.exe`；
- `build\R3R_incbuild.done` = `EXIT_CODE=0`；日志内 `error C|fatal error|FAILED:|build stopped` 计数 **0**；
- 时间戳：`src\pathfinder\yapf\yapf_rail.cpp` 01:20:54 / `src\pbs.cpp` 01:21:00 / `src\train_cmd.cpp` 01:21:03 → `yapf_rail.cpp.obj` 01:22:36 / `pbs.cpp.obj` 01:22:36 / `train_cmd.cpp.obj` 01:22:45 → `build\openttd.exe` **2026-10-15 01:24:53（51 868 160 B）**（晚于全部 obj）；
- `read_lints`（三文件）0 条；
- exe 字面量 3/3 命中：`UNRES-RAIL tile=` / `CLEAR-PATH veh=` / `yapf-resv`。

**本案状态**：探针已就绪、已编译。**实机跑日志仍由玩家操作**（AI 不自行启动游戏）。下一轮唯一待办＝拿同一场景的 `build\R3R_debug.log`，按上表读 `CLEAR-RAIL tile=1091,139`（以及 1092,139）的 `phase=`/`actor=`。

### 47.12 【探针已命中·决定性读数】地鼠 A = 同一 tick 内「订了立刻撤」，相位落在 `choose-track`

**取样**：`build\R3R_debug.log`，355 182 B / **4 439 行** / mtime **2026-10-16 16:02:27**（第 243 轮续三十七探针 exe 产物）。
**探针命中计数**：`UNRES-RAIL` **896**、`CLEAR-PATH` **492**、`yapf-resv` **7**、`RESV-WATCH` **887**、`TRP ` **49**、`CPL-PAIR` **42**、`COUPLE-FAIL` **7**。

**地鼠 A 原样六行**（首次出现在 `L2476`，`1091,139` 全日志命中 **677** 行、最后一次 `L4437`＝日志末尾；每组都长这样，逐字）：

```
RESV-WATCH SET-RAIL tile=1091,139 track=0 phase=choose-track actor=23 head=23
RESV-WATCH SET-RAIL tile=1092,139 track=0 phase=choose-track actor=23 head=23
CRT veh=23 order=1 dir=5 origin=1090,139 td=8 found=1
UNRES-RAIL tile=1091,139 track=0 phase=choose-track actor=23
RESV-WATCH CLEAR-RAIL tile=1091,139 track=0 phase=choose-track actor=23 head=23
UNRES-RAIL tile=1092,139 track=0 phase=choose-track actor=23
RESV-WATCH CLEAR-RAIL tile=1092,139 track=0 phase=choose-track actor=23 head=23
TRP veh=23 mf=1089,139 real=1 type=1 dest=175173 spd=2 fp=0 look=1 one=0 mstuck=1 fto=1 => ok=1 res=1
```

**本轮由探针新做实的三条（此前全是推断）**：
1. **不是"神秘第三方"，就是同一次订路内的自我撤销**：`SET-RAIL(1091)`→`SET-RAIL(1092)` 之后**紧跟**同一对格的 `UNRES-RAIL`+`CLEAR-RAIL`，中间只夹一条 `CRT`（YAPF `FindPath` 之后的探针，`yapf_rail.cpp:1337`），**且下一 tick 又重来**。⇒ 地鼠 A 的"预留消失"= 本 tick 订完又立刻撤；`TRP … => ok=1 res=1` 是**撤完才打的汇总**（`res=1` 是函数返回值，不是地图真值）。这解释了为什么多轮都找不到"释放者"——它与订路在同一个调用栈里，而不是别的车/别的路径。
2. **撤的相位是 `choose-track`**：`"choose-track"` 这个相位字面量**全代码库只有一处**——`src\train_cmd.cpp:13690` 的 `R3RResvPhaseGuard r3r_phase("choose-track", R3RResvActorID(consist));`（在 `ChooseTrainTrack` 内）。⇒ 释放调用发生在 `ChooseTrainTrack` 作用域内（或其被调函数内）。
3. **不是 `ClearPathReservation` 那条路**：两条 CLEAR 只打了 `UNRES-RAIL`，**没有** `CLEAR-PATH` 行，而我们给 `ClearPathReservation` 入口加的探针在"位仍在"时必打（条件与 `UNRES-RAIL` 完全相同）。⇒ 释放者是**直接调 `UnreserveRailTrack`** 的那几条路，候选收窄为 `src\train_cmd.cpp` 的 5 处：**10698**（`R3RReleaseReservationSafe`）、**14984**、**15003**、**16880**、**18688**（12924/12939/13007/13027 四处属 `ClearPathReservation` 内部，已由第 3 条排除）。

**同时做实 YAPF 侧样本（供排除法）**：`yapf-resv` 全日志 **7 行**，都在 `SET` 侧、`actor=11/23`：`L341`(1090,140)、`L1524-1526`(1092/1091/1090,137)、`L1751/1752/1754`(SET-STN 1086,139 / 1085,139 / SET-RAIL 1090,139) ⇒ 新增的 `"yapf-resv"` 标签**确实生效**（`yapf_rail.cpp:450` 的守卫在跑），且地鼠 A 那两条 SET 的相位是 `choose-track` 而非 `yapf-resv` ⇒ **地鼠 A 的 SET 也不出自 `yapf_rail.cpp:450` 那个 YAPF 入口**。

**顺带校正现场坐标**：`1112,129` 在本日志中 **0 命中**；veh=23 的实际位置是 `mf=1089,139`、CRT 的 origin=`1090,139` ⇒ 用户口述的"卡在 1112,129 之前"对应的是 **y≈139 那条东西向走廊、x≈1089~1092**（与 §47.10 的 `RESV-PURGE` 走廊 x=1090..1112、y=129..137 是同一走廊，但 y 差 2）。

**疑点 REG-243-O（新登记，待验证）**：为什么同一个 tick 里 `ChooseTrainTrack` 先把 1091,139/1092,139 订上、再立刻撤掉，而 `TRP` 仍报 `res=1`？可能性（按优先级）：①`TryPathReserve` 的"一次性强制预留"（`one=`/`fto=1`/`R3R-RES-ONCE` 家族，日志里 veh=2 有 `R3R-RES-ONCE used head=2 force_proceed=2`）在成功后又走了一次"清理上一次的强制预留"；②`ChooseTrainTrack` 里订完后检查某条件（如 `CheckTrainCollision`/`TrainController` 的 `mark_as_stuck`）失败而回滚订路；③订的是 `reserve_track` 之外的旁路（平台整格/长预留补偿）。判据：读上列 5 处 `UnreserveRailTrack` 的**调用上下文**（哪一个在 `ChooseTrainTrack` 之后、或本就是 `ChooseTrainTrack` 的尾部回滚），并对照 `TRP` 探针里 `one=/fto=` 的定义。

**下一步（单点、最便宜）**：把这 5 处 `UnreserveRailTrack` 各自包一层**独立相位标签**（如 `"res-safe"` / `"stuck-clear"` / `"ctl-clear"` / `"line-end"` / `"res-once-clear"`），或直接给每条加 `why=` 字面量；复跑同场景看 `UNRES-RAIL tile=1091,139` 的 `phase=` 落到谁。**在此之前不再做任何源码阅读式推断**（§47.3 的教训）。

---

### 47.13 【已落地·待复跑】给"直接调 `UnreserveRailTrack`"的全部释放点各包独立相位标签（第 243 轮续三十八，2026-10-16）

**先纠正 §47.12 的候选表**：逐一核对后发现，那 5 处里**有 3 处早就带标签**（⇒ 已被 `phase=choose-track` 这条读数**排除**，不需要再动）：

| 行（改前行号） | 所在函数 / 现象 | 已有相位标签 | 是否被 `choose-track` 排除 |
|---|---|---|---|
| `train_cmd.cpp:10678` 起（调用点 10698） | `R3RReleaseChainReservations()` 的 `doomed` 释放循环 | 已有 `"release-ghost"` | **是** |
| `train_cmd.cpp:14984` 附近 | 站台整格 reap | 已有 `"reap-stn"` | **是** |
| `train_cmd.cpp:15003` 附近 | plain rail reap | 已有 `"reap"` | **是** |
| `train_cmd.cpp:16880` | `CrashTrain`/车毁路径 | 无 | 否（新增） |
| `train_cmd.cpp:18688` | `DeleteVisibleTrain` 路径 | 无 | 否（新增） |

⇒ 结论句：`UNRES-RAIL tile=1091,139 phase=choose-track` **已经排除了 release-ghost / reap-stn / reap 三条**（它们会打印各自的名字）。真正剩下的"无名"释放点只有两条车毁路径（`16880`/`18688`），而本场景**没有任何列车被销毁**（日志里 `COUPLE-OK`/`DECOUPLE-DONE` 正常、veh=23 一直存活）⇒ 按 §47.3 的纪律，**这说明 §47.12 的候选表本身不完整**：一定还有一处不在这 5 个里的释放点。

**因此本轮改为"全库尽收"**：把 `src` 下**全部** `UnreserveRailTrack(` / `UnreserveRailTrackdir(` 的调用点逐一核对标签，缺标签的一律补上。清单（改前）：

| 文件 | 调用点 | 状态 |
|---|---|---|
| `src\pbs.cpp` | `405`（`R3RReleaseReservationSafe` 内） | 已有 `"gc-stn"` |
| `src\pbs.cpp` | `826`（`UnreserveRailTrackdir()` 内） | **无** → 本轮**刻意不加**，改由两个调用点各自命名（见下） |
| `src\pbs.cpp` | `R3RPurgeStrayReservations()` 内两处 | **无** → 本轮补 `"purge"` |
| `src\train_cmd.cpp` | `10698` / `14984` / `15003` | 已有 `release-ghost` / `reap-stn` / `reap` |
| `src\train_cmd.cpp` | `ExtendTrainReservation()` 的"back out"循环（`UnreserveRailTrackdir`） | **无** → 本轮补 **`"extend-backout"`** ← 头号嫌疑 |
| `src\train_cmd.cpp` | `16880`（车毁） | **无** → 本轮补 `"crash-del"` |
| `src\train_cmd.cpp` | `18688`（车毁·可见） | **无** → 本轮补 `"del-visible"` |
| `src\pathfinder\yapf\yapf_rail.cpp` | `298`（couple 寻路 FSCP 半成功回滚） | **无** → 本轮补 **`"yapf-fscp"`** |
| `src\pathfinder\yapf\yapf_rail.cpp` | `374`（`UnreserveSingleTrack` 内，调 `UnreserveRailTrackdir`） | 由 `TryReservePath` 的 `"yapf-resv"` 守卫覆盖 ⇒ 已有名字 |

**为什么 `ExtendTrainReservation` 是头号嫌疑**（仅作为**待验证假设**登记，不当结论）：它的 "back out" 循环是**该函数自己**对"试探性延长预留"的回滚；`ExtendTrainReservation` 由 `TryPathReserve` 调用，而 `TryPathReserve` 在 `ChooseTrainTrack` 的作用域内 ⇒ 其释放**天然继承 `phase=choose-track`**，与 §47.12 实测读数**逐字吻合**；且循环体里就是 `UnreserveRailTrackdir(tile, cur_td)`——与"`UNRES-RAIL` 紧跟 `CLEAR-RAIL` 成对出现"的形态一致。**判据**：复跑后若 `UNRES-RAIL tile=1091,139 phase=extend-backout actor=23` ⇒ 假设成立。

**实现纪律**：全部新标签（`purge` / `extend-backout` / `crash-del` / `del-visible` / `yapf-fscp`）都**不在** `R3RResvLedgerPurpose()` 的 `consist_phases[]`（`{"consist","clear-consist","waiter-platform"}`）内 ⇒ 台账仍把它们归为 `PATH` 位，**零行为变更**（仅日志）。`UnreserveRailTrackdir()` 内部**刻意不放**守卫，否则会覆盖调用者的名字、把两条调用点混成一个标签。

**同时登记疑点 REG-243-P（新，待验证）**：早先"候选只有 5 处"这一枚举**是错的**（漏了 `ExtendTrainReservation` 的 back-out 循环与 `UnreserveRailTrackdir` 这条间接路径）。**期望值**：全库直接/间接释放点在补标签后应能一一映射；**实测值**：§47.12 的 5 处无法解释本现场。**教训**：枚举"释放点"必须按**调用函数**（而非按 `grep 到的行`）收，且必须把 `UnreserveRailTrackdir` 这类薄包装的调用者一并列出。

**待复跑判据（3 条）**：①`UNRES-RAIL tile=1091,139`（及 `1092,139`）的 `phase=` **不再**是 `choose-track`，而是上表某个具名标签；②`TRP veh=23 … ok=1 res=1` 不变、`COUPLE-OK`/`DECOUPLE-DONE` 行为逐字不变（负判据）；③新标签**只**出现在它们各自应有的场景（如 `crash-del`/`del-visible` 只在列车真的被销毁时出现，正常行驶会话里应为 0 条）。

**构建**：见 §47.14。

---

### 47.14 【构建自证】第 243 轮续三十八（2026-10-16 16:26）

复用既有 `_tmp_inc_build.cmd`（**未新建任何 `.cmd`**）；改前 `tasklist /FI "IMAGENAME eq openttd.exe"` 曾命中 PID 17600（`D:\sourcecode of JGRPP\build\openttd.exe`，启动 15:57:24），玩家关闭后重跑。

| 项 | 值 |
|---|---|
| 护栏 | `build\R3R_incbuild.guard.log` = `GUARD: incremental is safe (no header/lang file is newer than the newest object)`（本轮**未改任何 `src/*.h`** ⇒ 增量合法；guard.log mtime 16:23:56 属本轮） |
| 日志尾 | `[2/5] Building CXX object ...\src\pbs.cpp.obj` → `[3/5] ...\src\pathfinder\yapf\yapf_rail.cpp.obj` → `[4/5] ...\src\train_cmd.cpp.obj` → `[5/5] Linking CXX executable openttd.exe`（Linking 1 次） |
| 完成标记 | `build\R3R_incbuild.done` = `EXIT_CODE=0`（16:26:14，13 B） |
| 错误计数 | `error C` / `fatal error` / `FAILED:` / `build stopped` 计数 **0** |
| 时间戳链 | `yapf_rail.cpp` 16:09:35 / `pbs.cpp` 16:09:37 / `train_cmd.cpp` 16:09:48 → `build\openttd.exe` **16:26:10**（51 868 160 B）（晚于全部所改源码） |
| lint | `read_lints`（`src\pbs.cpp` / `src\train_cmd.cpp` / `src\pathfinder\yapf\yapf_rail.cpp`）**0 条** |
| exe 字面量（正） | `extend-backout` / `crash-del` / `del-visible` / `yapf-fscp` / `purge` 逐个 `findstr /C:` **全部 HIT**；既有 `gc-stn` / `release-ghost` 仍在 |
| exe 字面量（负判据） | `unres-td` **ABSENT** —— 与"刻意不加 `UnreserveRailTrackdir` 内部守卫、已回退草加版"一致，证明 pbs.cpp 确实重编 |

⇒ 本 exe = **第 243 轮续三十八版**（含全部释放点具名标签），可交付实机复测。

**复跑时只看一行**：`UNRES-RAIL tile=1091,139 ... phase=<名字>`（及 `1092,139`）。期望 `<名字>` **不再是 `choose-track`**，而是上表某个具名标签（优先期望 `extend-backout`）。

---

### 47.15 【实测命中】地鼠 A 的释放点锁定 = `ExtendTrainReservation` 的自身回滚（第 243 轮续三十九，2026-10-16）

**玩家复跑日志原文（新 exe = 2026-10-16 16:26:10 版，`build\R3R_debug.log`）**：

```
UNRES-RAIL tile=1091,139 track=0 phase=extend-backout actor=23
RESV-WATCH CLEAR-RAIL tile=1091,139 track=0 phase=extend-backout actor=23 head=23
UNRES-RAIL tile=1092,139 track=0 phase=extend-backout actor=23
RESV-WATCH CLEAR-RAIL tile=1092,139 track=0 phase=extend-backout actor=23 head=23
```

**判定**：
1. **判据① 成立**：`phase=` 由 `choose-track` → **`extend-backout`**，且 `actor=23` 正是当事机车（与 §47.12 的 `actor=23` 一致）。⇒ §47.13 登记的**头号假设升格为结论**：地鼠 A 的"撤预留"确实发生在 `ExtendTrainReservation()`（`src\train_cmd.cpp:13105`）**自己的 "back out" 回滚循环**（`src\train_cmd.cpp:13194` 起，"Sorry, can't reserve path, back out."）里。
2. **两格成对**：`1091,139` 与 `1092,139` 各出现一次 `UNRES-RAIL` + 一次 `CLEAR-RAIL`，`track=0`（=TRACK_X，东西向），**与 §47.12 的"同 tick 订完即撤"形态逐字吻合**。
3. **附带排除**：本轮新增的另外 4 个标签（`crash-del` / `del-visible` / `purge` / `yapf-fscp`）在本现场**一个都没出现** ⇒ 负判据③**通过**（这四条路径本场景不该触发）。
4. **REG-243-P 结清**：候选表漏洞确实只是枚举法错误，补标签后 100% 可归因（本次命中即证明"按调用函数收"的纪律正确）。

**REG-243-O 状态更新**：由"待验证假设" → **"释放点已锁定（已实测）／触发原因仍待解"**。即：
- 已解：**是谁**撤的（`ExtendTrainReservation` 的 back out）。
- 待解：**为什么**订下 `1091,139`/`1092,139` 后这一轮延长会失败到必须整体回滚（下一段要读 `src\train_cmd.cpp:13105-13200` 的失败分支与 `CFollowTrackRail ft(v)` 的终止条件）。

**下一步（单点、最便宜）**：读 `ExtendTrainReservation()` 的循环退出条件，判定失败是"下一格订不上（被别人占）"还是"路径缓存到头（`ft` 走不动）"；前者需看那格的主人是谁，后者是设计使然。**仍不凭代码推断下结论**，读完后若仍有两种可能，再加一条只读探针（打印 `back out` 循环入口的 `tile/cur_td/origin.tile/ft.old_tile` 与失败原因），再让玩家复跑一次。

---

### 47.16 【读码定位 + 探针落地】失败出口共 7 个，`EXTEND-BACKOUT` 已加（第 243 轮续四十，2026-10-16）

**读码结论（`src\train_cmd.cpp:13105-13223`，逐行读，非 grep）**：`ExtendTrainReservation()` 的**试延长**主循环只有 **7 个出口**，全部**直接 `break`**，无一例外落到同一个 back out 回滚：

| # | 行（改前） | 条件 | 命名 |
|---|---|---|---|
| 0 | — | `while (ft.Follow(...))` 判假 | `follow-fail` |
| 1 | `:13114` | `HasOnewaySignalBlockingTrackdir`（单向信号阻挡） | `oneway-signal` |
| 2 | `:13119` | 90° 转被禁且候选位清零 | `90deg-turn` |
| 3 | `:13131` | 岔口 / 目标格**已被预留** | `choice-reserved` |
| 4 | `:13150` | 安全位但 `wp_free` 假 **或** 该位订不上 | `safe-not-free` / `safe-reserve-fail` |
| 5 | `:13181` | TRP 动作 `TRPRF_WAIT_AT_PBS`（在起始信号处等待） | `wait-at-pbs` |
| 6 | `:13186` | `TryReserveRailTrackdir()` 失败 | `reserve-fail` |

⇒ **不存在"设计使然的静默退出"这一支**：除 #0（`ft.err == EC_OWNER/EC_NO_WAY` 时其实会在 `:13189` 提前 `return` 成功、不进 back out）之外，**任何进到 `:13194` back out 的调用，都必然是被上表某一条挡下的**。因此"为什么每 tick 都失败"= 上表哪一条恒真。

**回滚顺序算术自证（把日志反推成坐标，纯算术，非猜测）**：回滚循环 `:13200-13217` 语义是"从 `origin` 沿 `Follow` 前进，每前进一格就 `UnreserveRailTrackdir` 一次，直到 `tile==stopped`"。日志释放顺序 `1091,139` → `1092,139`（各一次，顺序单调 +x）⇒ `origin.tile = 1090,139`、`stopped = ft.old_tile = 1092,139`。⇒ **试延长是在"从 `1092,139` 迈出下一步"时失败的**（`1090,139` 自身是延长前既有预留，按设计不释放，故不出现在日志——与只看到两格吻合）。失败的那一格（`ft.new_tile`）**未出现在释放名单里**，正因为它从未被订上 ⇒ 与"订不上/被挡"一致，而不是"订完又撤"。

**探针落地（仅 `src\train_cmd.cpp`，未碰任何 `src\*.h`，零行为变更）**：新增 `const char *r3r_backout_reason = "follow-fail";`，上表 7 个出口各写一个字符串常量（**只赋值，不改任何分支条件**）；在 back out 入口 `R3RResvPhaseGuard("extend-backout", ...)` 之后、`tile = origin.tile;` 之前，加一行：

```
EXTEND-BACKOUT veh=%u origin=%d,%d/%d stop=%d,%d/%d next=%d,%d reason=%s err=%d
```

其中 `stop=` 是跟随器最后接受的格（= 回滚终止格），`next=` 是**它迈不过去的那一格**（即 §47.15 里"挡在哪"的答案），`err=` 是 `CFollowTrackRail::err`。用 `R3RDbgWrite()`（自带 `R3RDbgOn()` 闸门，与 `UNRES-RAIL` 同一通道）。

**判读口径（一次复跑即可）**：`reason=choice-reserved` ⇒ 挡点是**别人的预留**（下一步查那格主人）；`reason=reserve-fail` / `safe-not-free` ⇒ 在那格上订不上（查该格 slot/信号/TRP）；`reason=oneway-signal` ⇒ 单向信号（即 §47.12 的 `redsig` 侧，两条线在此收口）；`reason=90deg-turn` ⇒ 禁 90°；`reason=follow-fail` ⇒ 路径缓存到头（`err` 定性质）。**无论哪支，都能直接接下一步修法**，不再需要第三种可能的分叉。

**未做**：本轮未构建（下一步：复用 `_tmp_inc_build.cmd` 增量 + 五件套自证）；仍未解释"为何每 tick 都失败"（挡点是否永久存在），**须玩家用新 exe 复跑后由 `reason=` 与 `next=` 给出**。

### 47.17 【构建自证】第 243 轮续四十（2026-10-16 16:53）

复用既有 `_tmp_inc_build.cmd`（**未新建任何 `.cmd`**）；改前 `Get-Process openttd` 零命中（游戏已关，未触发 exitCode 97 占用闸门）。

| 项 | 值 |
|---|---|
| `build\R3R_incbuild.guard.log` | `GUARD: incremental is safe (no header/lang file is newer than the newest object)` |
| `build\R3R_incbuild.log` | `[3/3] Linking CXX executable openttd.exe`（**Linking 恰 1 次**；`error C[0-9]` / `fatal error` / `FAILED:` / `build stopped` **零命中**） |
| `build\R3R_incbuild.done` | `EXIT_CODE=0`（@16:52:15） |
| 时间戳链 | `src\train_cmd.cpp` @16:49:25 → `train_cmd.cpp.obj` @16:50:46 → `build\openttd.exe` @16:52:07（51 868 160 B）✓ 逐级变新 |
| `read_lints` | `src\train_cmd.cpp` **0 条** |
| exe 字面量 | `EXTEND-BACKOUT` ✓ / `choice-reserved` ✓ / `safe-not-free` ✓ / `reserve-fail` ✓；**负判据** `r3r_no_such_control_string_xyz` 零命中（证明检索法有效） |

**未做（收口待办）**："为何每 tick 都失败 / 挡点是否永久存在 / 1093,139 的主人是谁"**仍待玩家用本 exe（2026-10-16 16:52:07 版）复跑**后由 `EXTEND-BACKOUT … reason=` 与 `next=` 给出；本轮**不凭代码推断下结论**。

---

### 47.18 【实测命中·第二轮】`reason=choice-reserved`：挡点 = 1093,139 上已有预留位（第 243 轮续四十一，2026-10-16）

**现场**：`build\R3R_debug.log`（新 exe = 探针版复跑）。

**决定性读数（日志逐字，`build\R3R_debug.log:2529-2533`）**：

```
2529:EXTEND-BACKOUT veh=23 origin=1090,139/8 stop=1092,139/8 next=1093,139 reason=choice-reserved err=0
2530:UNRES-RAIL tile=1091,139 track=0 phase=extend-backout actor=23
2531:RESV-WATCH CLEAR-RAIL tile=1091,139 track=0 phase=extend-backout actor=23 head=23
2532:UNRES-RAIL tile=1092,139 track=0 phase=extend-backout actor=23
2533:RESV-WATCH CLEAR-RAIL tile=1092,139 track=0 phase=extend-backout actor=23 head=23
2549-2553: 同上，逐字重复
```

**三条判定**：
1. **§47.16 的算术自证得到实测逐字确认**：`origin=1090,139` / `stop=1092,139` 与预测完全一致 ⇒ 回滚顺序反推法成立，"试延长是在从 `1092,139` 迈出下一步时失败"成立。
2. **失败出口唯一锁定为 `choice-reserved`**（`src\train_cmd.cpp` 试延长主循环中「岔口/目标格已被预留」那一支，即 `HasReservedTracks(ft.new_tile, TrackdirBitsToTrackBits(TrackdirReachesTrackdirs(ft.old_td)))` 为真），`next=1093,139`、`err=0`（**不是** `follow-fail`；`ft` 跟随器本身健康，路径缓存没到头）。⇒ REG-243-O「为什么每 tick 都失败」的分叉已收敛：**不是设计使然，是有实体的预留位挡路**。
3. **`reason` 探针全部工作正常**：同日志早期还有 `veh=11 origin=1074,138/8 stop=1075,138/8 next=1076,138 reason=reserve-fail`（`build\R3R_debug.log:258` 等几十行）——说明命名机制能区分不同出口，`reserve-fail` 与 `choice-reserved` 是两种不同成因，**1090..1093,139 这一处专属于 `choice-reserved`**。

**新问题（须下一轮取证）**：`choice-reserved` 只回答"那格上**有没有**预留位"，**不回答"那是谁的位"**。该分支只看 `HasReservedTracks`，**不看所有权、不看是否孤儿**。⇒ 必须查 `1093,139` 的预留位归属：若为**别车的活预留**（有人合法占据）⇒ 需查为什么那辆车不走、或 1093,139 是不是双槽格被占错轨；若为**孤儿位**（`RESV-AUDIT-STRAY` 判无主 / `RESV-REAP` 曾清过）⇒ 属 KI-345/KI-346 家族（存量泄漏），修法走 GC/一次性清理，且需解释"为何清了又被订回"。

**未做**：本轮**未改任何源码、未构建**；`1093,139` 的主人身份**未取证**（下一步：在日志中定位 1093,139 相关的 `RESV-WATCH SET-RAIL/SET-STN`、`RESV-AUDIT-OWNER/STRAY`、`RP cur=1093,139` 行）。

### 47.19 【取证·第三轮】`1093,139` 是双轨岔口格、且**审计判为"有主"**（第 243 轮续四十二，2026-10-16）

**取证方式**：同一份 `build\R3R_debug.log` 上定向检索（`1093,139` / `RESV-AUDIT`），并回读 `src\train_cmd.cpp:13090-13185` 核对判据原文。**本轮仍未改任何源码、未构建。**

**结论句 1（读码，确定）**：`choice-reserved` 的判据**只看"该格上有没有任何预留位"，完全不查所有权、不查是否孤儿**——
`src\train_cmd.cpp:13136`：
```cpp
if (HasReservedTracks(ft.new_tile, TrackdirBitsToTrackBits(TrackdirReachesTrackdirs(ft.old_td)))) { r3r_backout_reason = "choice-reserved"; break; }
```
它位于 `13129` 的 `target_seen || KillFirstBit(ft.new_td_bits) != TRACKDIR_BIT_NONE`（"岔口/目标"）分支内 ⇒ 只要 `1093,139` 上有**任何一个**与本车可达轨向相交的预留位，本车就立刻放弃本次试延长。故 `choice-reserved` 本身**不能区分**"别车活预留"与"孤儿位"。

**结论句 2（日志，确定）**：`1093,139` 是**双轨岔口格**：
- `build\R3R_debug.log:826` `REACH cur=1093,139 td=8 depth=15 trk=0 bits=0x5`
- `build\R3R_debug.log:827` `REACH cur=1093,139 td=10 depth=15 trk=2 bits=0x5`
- `build\R3R_debug.log:847/848` 同形 `RP cur=1093,139 td=8/td=10 ... bits=0x5`
`bits=0x5` = `TRACK_X(1) | TRACK_UPPER(4)` ⇒ 两个 track 位 ⇒ 满足 `13129` 的"岔口"判据，**必然**走到 `13136` 的预留检查。这就是 veh=23 走到该格才失败、而 1090/1091/1092,139（`bits=0x1` 单轨）一路订得下去的原因。

**结论句 3（日志，确定）**：`1093,139` 的预留位**曾经被 `rp()` 通道订过**（不是幻读）：
- 上面 826/827 的 `RP cur=1093,139` 属某次挂接寻路的路径预留（同组收尾 `build\R3R_debug.log:852` `CPL-RESERVE veh=2 reserved=1 next=8`）。
- `build\R3R_debug.log:1412-1413`：`CLEAR-PATH veh=11 tile=1093,139 td=8 phase=controller actor=11` → `UNRES-RAIL tile=1093,139 track=0 phase=controller actor=11` ⇒ **veh=11 曾占该格并在 `phase=controller` 下释放**（证明该格确实会被人订、也会被人放）。

**结论句 4（日志，确定）· 本处最关键**：审计**判 `1093,139` 有主**，它不是孤儿位：
- `build\R3R_debug.log:5095` `RESV-AUDIT-SUMMARY tick tick=7186 reservedTiles=59 owned=39 strayTiles=20 reaped=0 pendingStray=20`
- 20 格 `RESV-AUDIT-STRAY` 名单（`build\R3R_debug.log:5067-5086`）**恰为** `1093,87 → 1112,106` 的对角走廊、**全部 `track=4 bits=0x10`**（= KI-346 家族那条残留走廊），`1093,139` **不在其中**。
- 旁证 `build\R3R_debug.log:5094` `RESV-AUDIT-OWNER tick tile=1090,139 track=0 owner=veh23 bits=0x1` ⇒ **veh=23 自己只占着 origin 格 `1090,139`**，它并没有"自己的位把自己挡住"。
⇒ 即：`1093,139` 上的位在审计口径下**有主**（`GetTrainForReservation/GetTrainForWholeTileReservation` 能追到某辆车）。这与"孤儿位（走 GC 清理）"是**两条不同的修法**，必须把主人身份钉死再动手。

**疑点（新登记）REG-243-S（待验证；编号刻意避开续三十八已登记并已结清的 REG-243-P）**：`1093,139` 有主，但全日志**检索不到该格的任何 `RESV-WATCH SET-RAIL tile=1093,139` 建立行**（只有 `RP cur=1093,139`、`CLEAR-PATH veh=11 tile=1093,139`、`EXTEND-BACKOUT ... next=1093,139`）。
- **期望值**：`RESV-WATCH SET-RAIL` 应覆盖所有经 `rp()`/`TryReserveRailTrackdir` 订下的格（826 附近那批挂接预留**没有**对应的 SET-RAIL 行）。
- **实测值**：`RP` 通道的预留没有 `RESV-WATCH SET-RAIL` 配对。
- **为什么存疑**：这说明 `RESV-WATCH` 探针**并不覆盖挂接寻路 `rp()` 这条通道**（与 §47.13 教训同源：探针覆盖面按"调用函数"收，不能按 grep 到的行收）。⇒ 因此**不能用"搜不到 SET-RAIL"反推"该格没被订过"**，也不能靠 SET-RAIL 找主人。
- **还需什么数据**：下一轮应直接**给 `13136` 这一支加只读探针**，把 `ft.new_tile` 的 `GetTrainForReservation(tile, track)` / `GetTrainForWholeTileReservation(tile)` 结果打出来（`CHOICE-RESV veh= tile= track=bits= owner=vehN|none`），一次复跑即可定主人身份，不再靠间接推断。

**两条路的修法分野（待主人身份确定后二选一）**：
- (A) 若主人是**活车**（有人合法占位，如等待/正在挂接的车底链）：这是**正常让路**，问题转为"veh=23 的订单/路径为何非走这条岔口不可"（`1090,139` 西向走廊尽头只有一个岔口 ⇒ 可能要提前在更早的格停下、或让路方先走）。需求数据：`1093,139` 主人的订单类型（`OT_WAIT_COUPLE` / `OT_GOTO_COUPLE`）与它停了多少 tick。
- (B) 若主人是**有主但已死/永不走**的车：属 KI-207c 家族（"有主但已死"），修法=在审计里加"主人长时间静止且不在本格"的退役判据后 `RESV-REAP`。

**未做**：本轮**未改任何源码、未构建**；`1093,139` 的主人身份**仍未直接取证**（下一步按 REG-243-S 的"还需什么数据"直接加 `CHOICE-RESV` 只读探针，一次复跑定论）。

### 47.20 【落地】`CHOICE-RESV` 只读探针（EXTEND-BACKOUT 扩字段）+ 构建自证（第 243 轮续四十三，2026-10-16 17:11）

**§47.19 末尾那句"本轮未改任何源码、未构建"仅指 §47.19 当轮；本节起改为已改码、已编译。**

**改动（仅 `src\train_cmd.cpp`，未碰任何 `src\*.h` ⇒ 增量合法）三处**：
1. `ExtendTrainReservation()` 局部新增三个只读变量（紧跟 `13112` 的 `r3r_backout_reason` 声明，纯声明无副作用）：
   `const Train *r3r_backout_owner = nullptr; int r3r_backout_owner_track = -1; bool r3r_backout_owner_whole = false;`
2. `:13136` 的 `choice-reserved` **单行 if 展开为块**：先问 `GetTrainForWholeTileReservation(ft.new_tile)`（引擎自带的所有权走查，对站台/路点格同样有效），为 `nullptr` 再按 `GetReservedTrackbits(ft.new_tile)` 逐轨问 `GetTrainForReservation(ft.new_tile, t)`。**只赋值、不平表、不动位置**；该分支的**判据表达式与 `break` 位置逐字不变**。
3. back out 入口日志行**尾部追加**三字段 `owner=%d whole=%d track=%d`（`owner=-1` = "有预留位但审计走查无人认领" ⇒ 候选孤儿）。**原有字段与原顺序逐字未动**（追加在末尾，与此前扩字段惯例一致）。

**构建自证（复用既有 `_tmp_inc_build.cmd`，未新建任何 `.cmd`）**：
- `build\R3R_incbuild.guard.log` = `GUARD: incremental is safe (no header/lang file is newer than the newest object)`
- `build\R3R_incbuild.log` 尾：`[2/5] ... FindVersion` → `[2/3] Building CXX object CMakeFiles\openttd_lib.dir\src\train_cmd.cpp.obj` → `[3/3] Linking CXX executable openttd.exe`（Linking 1 次）
- `build\R3R_incbuild.done` = `EXIT_CODE=0`
- 时间戳链：`src\train_cmd.cpp` @2026-10-16 17:09:13 → `build\CMakeFiles\openttd_lib.dir\src\train_cmd.cpp.obj` @17:09:45（11 834 062 B）→ `build\openttd.exe` @17:11:06（51 868 160 B）
- `read_lints(src\train_cmd.cpp)` = **0 条**
- 产物自证：`findstr /C:"owner=%d whole=%d track=%d" build\openttd.exe` **命中**（新字面量确在 exe 内）

**复测判据（1 条决定性 + 2 条旁证/负判据）**：
1. **决定性**：同场景复跑，看 `EXTEND-BACKOUT veh=23 ... next=1093,139 reason=choice-reserved` 那行的 `owner=`：
   - `owner=<某车号>` ⇒ 主人是**活车** ⇒ 走修法分野 (A)：查主人的订单类型（`OT_WAIT_COUPLE`/`OT_GOTO_COUPLE`）与已停留 tick 数，问题转为"veh=23 为何非走这条岔口不可"；
   - `owner=-1` ⇒ **无人认领** ⇒ 判定为**孤儿位**，走修法分野 (B)：在审计里加退役判据后 `RESV-REAP`（KI-207c 家族）。
2. **旁证**：`whole=1` ⇒ 引擎以"整格"口径认领（站台/路点/格上有停车）；`whole=0 track=N` ⇒ 按第 N 条轨认领（普通双轨岔口格多为这一支）。
3. **负判据**：其它出口（`reserve-fail`/`safe-not-free`/`safe-reserve-fail`/`oneway-signal`/`90deg-turn`/`wait-at-pbs`）该行的 `owner=` 必须恒为 `-1`（本轮只在 `choice-reserved` 支赋值）——若出现非 `-1`，说明有分支被漏改。

**未做**：游戏内复跑待玩家执行；`1093,139` 的主人身份**由这一次复跑直接给出**，不再做间接推断。

### 47.21 【复跑判读·决定性】`owner=23` 与 `owner=22` 是同一格残留位的两种「走查落点」，不是预订者（第 243 轮续四十四，2026-10-16）

**输入（玩家本轮日志）**：`build\R3R_debug.log` 的 4 行
`EXTEND-BACKOUT veh=23 origin=1090,139/8 stop=1092,139/8 next=1093,139 reason=choice-reserved err=0 owner=23 whole=0 track=2`
＋同一次回滚的 `UNRES-RAIL tile=1091,139 track=0 phase=extend-backout actor=23`。

**结论 1（读数自相矛盾，本身就是结论）**：同一格 `1093,139`、同一会话，前 4 行是 `owner=23 whole=0 track=2`，几十行之后转为 40+ 行 `owner=22 whole=1 track=-1`（§47.19 已记录该翻转）。走查不是账本——这正是 KI-359 在 `src\pbs.cpp:328-346`（`R3RTrainForLedgerBit` 注释）已写明的陷阱：「走查回答的是『这条预留路径的另一端站着谁』，连挂时那恒是被连挂的车」。⇒ **`owner=23` 不证明该位是 23 订的，`owner=22` 也不证明是 22 订的。**

**结论 2（机制，确定）**：`owner=23` 恰由 23 本次试延长自己造出来：23 在同一轮 `choose-track` 相位先订下 `1091,139`/`1092,139`（`SET-RAIL tile=1091,139 phase=choose-track actor=23`），回滚时再放掉（`UNRES-RAIL tile=1091,139 phase=extend-backout actor=23`）。走查从残留位 `1093,139` 沿 `1092→1091→1090` 走回去，落点必然是站在 `1090,139` 的 23；回滚把这条链断开后，`GetTrainForWholeTileReservation()` 换另一头走，落点变成 22。⇒ 两条读数都是**几何终点**，与「谁订的位」无关。

**结论 3（审计为何永远清不掉它）**：`src\train_cmd.cpp:14986-14999` 的判据是
`owner = GetTrainForReservation(t,track); lowner = R3RResvLedgerOwner(t,track); ledger_dead = (lowner!=NO_OWNER) && (Train::GetIfValid(lowner)==nullptr); if (owner == nullptr || ledger_dead) ⇒ stray;`
即「走查追到活车 **且** 账本主人还活着（或根本无账本行）」⇒ **owned**。两种情形会永久 owned：
- **(a) 账本无行**（`lowner==NO_OWNER`）：`ledger_dead` 恒 false，只有走查能判，而走查总能追到活车 ⇒ 永不 stray（随存档带入的存量位、或由未喂账本的旧路径铺下的位，正是这一种）；
- **(b) 账本行主人仍活着但位已陈旧**（链编辑把车节搬走、解挂／逻辑翻后原位再没人走到）：`ledger_dead` 也 false ⇒ 永不 stray。

KI-359 只堵了「账本主人已死」，**(a)(b) 正是它剩下的两半**。本轮 20 格 `RESV-AUDIT-STRAY`（`1093,87→1112,106`，全部 `track=4 bits=0x10`，KI-346 家族）走的是「`owner==nullptr` ＋ `lowner=-1`」这条；`1093,139` **不在其中** ⇒ 审计判它有主。

**结论 4（REG-243-S 定性）**：既**不是**「活车合法让路」（没有任何车真的要走 `1093,139` 的 LEFT 半轨——`whole=0 track=2` 那个「主人」是 23 自己造出的走查假象，而 22 也并未真的预留在该格上），也**不是**干净孤儿（走查一律追得到活车 ⇒ 审计／`RESV-REAP` 永远够不到它）。它是**「无账本凭据、但走查误认有主」的陈旧位**（KI-210 归属口径 × KI-207c 清理盲区），对 23 表现为**硬死锁**：格上残留的是 `TRACK_UPPER/LEFT` 位（`bits=0x5`），而 23 要走的恰是这条半轨（`origin=1090,139/8`＝`TRACKDIR_LEFT_S` ⇒ `TrackdirToTrack=TRACK_LEFT=2`，与 `track=2` 同轨）。

**结论 5（「搜不到 SET-RAIL」不能反推「没订过」）**：六个 setter（`src\r3r_resv_probes.h:14-19`）**无条件**喂账本（KI-359），而 `R3RResvWatchLog()` 只对 watch 名单内的格写日志（同文件 `:21-23`）⇒ `1093,139` 不在 watch 名单时**账本有行也不会有日志行**。故 §47.19 登记的 REG-243-S 疑点（「有主却搜不到建立行」）只能靠 `R3RResvLedgerOwner()` 直接问账本来收口。

**唯一缺的决定性字段（本轮落地）**：`led=R3RResvLedgerOwner(ft.new_tile, 触发轨)`（`src\pbs.h:101` 声明、`src\pbs.cpp:322-326` 实现；纯查表、无断言，无行返回 `R3R_RESV_NO_OWNER`）。顺带修掉本轮自己发现的探针口径缺陷：旧实现报的 `track=` 是「第一个走查有答案的预留位」，**未必是触发检查的那条轨** ⇒ 新实现先求 `need=TrackdirBitsToTrackBits(TrackdirReachesTrackdirs(ft.old_td))` 与 `resv=GetReservedTrackbits(tile)`，优先报 `resv & need` 里的轨，并把 `need=`/`resv=` 两个掩码一并打盘，人肉可复核。

**判读表（复跑后三选一，一步到位）**：
- `led=-1` ⇒ 无账本行 ⇒ **事实孤儿**（随存档带入／旧路径铺下）⇒ 修法 **(B′)**：审计对「无账本行 ＋ 格上无车」的位也判 stray 并交 `RESV-REAP`（边界：读档后全体位都无行，必须附加「格上无车 且 走查落点不在本格」才安全）。
- `led=<活车号（23）>` ⇒ 账本主人仍活着的**陈旧位**（(b) 情形）⇒ 修法 **(C)**：释放改成**账本驱动**——耦合／解挂／逻辑翻／车库编辑四个提交点按账本释放本链全部行，而不是按「此刻车节几何扫一遍」（正是 87560799／KI-203 记的老毛病：几何一变，旧位没人走到）。
- `led=<22>` ⇒ 连挂前的旧账（KI-210 归属口径落在 22 名下）⇒ 同 (C)，并复核「连挂提交点是否漏了把发起方路径位转记」。

**未做**：本条先落盘判读，随后才改探针 `led=/need=/resv=`（仅 `src\train_cmd.cpp`）＋增量构建；游戏内复跑仍待玩家。

### 47.22 【落地】`led=`／`need=`／`resv=` 三字段已进 exe（第 243 轮续四十五，2026-10-16）

**改动（仅 `src\train_cmd.cpp` 一个文件，未碰任何 `src\*.h` ⇒ 增量合法）**：
1. `ExtendTrainReservation()` 的只读变量区新增 `uint r3r_backout_need_bits`／`uint r3r_backout_resv_bits`／`int r3r_backout_ledger = -1`（默认 -1 ＝「无账本行」，避免与车号 0 混淆）。
2. `choice-reserved` 退出分支改写：先求
   `r3r_need_bits = TrackdirBitsToTrackBits(TrackdirReachesTrackdirs(ft.old_td))`、
   `r3r_resv_bits = GetReservedTrackbits(ft.new_tile)`（**与分支判据 `HasReservedTracks(ft.new_tile, TrackdirBitsToTrackBits(TrackdirReachesTrackdirs(ft.old_td)))` 同一表达式**，保证打的就是判据看的那两块掩码）；
   再求 `r3r_blame_track` ＝ `resv & need` 里第一条轨（**口径修正**：旧 `track=` 是「第一个走查有答案的预留位」，可能报一条与拒绝无关的轨）；走查（whole-tile → 逐轨）逻辑**逐字保留**，最后把 `track=` 覆盖为 `r3r_blame_track`（有则覆盖，无则回退走查结果），并对该轨查账本 `R3RResvLedgerOwner(ft.new_tile, r3r_blame_track)`（`src\pbs.h:101`／`src\pbs.cpp:322-326`，纯查表无断言，无行返 `R3R_RESV_NO_OWNER` → 打印 `-1`）。
3. `EXTEND-BACKOUT` 格式串**尾部追加** `need=0x%X resv=0x%X led=%d`（旧字段与旧次序逐字不变）。

**判读口径（复跑后一步到位）**：`led=-1` ⇒ 无账本行＝事实孤儿（随存档带入／整格重建／写入时无 R3R actor）⇒ 修法 (B′)；`led=23` ⇒ 账本主人仍活着的陈旧位 ⇒ (C)；`led=22` ⇒ 连挂前旧账 ⇒ 同 (C) 并复核耦合提交点是否漏转记发起方路径位。`owner=` 恒为**走查落点**，与 `led=` 不同属正常（KI-359）；两者同一行并排出现，正是本轮要的对照证据。

**构建自证（第 243 轮续四十五）**：复用既有 `_tmp_inc_build.cmd`（未新建任何 `.cmd`）；构建前 `tasklist` 确认 `openttd.exe` 未运行；`build\R3R_incbuild.guard.log` ＝ `GUARD: incremental is safe`；日志 `[2/3] train_cmd.cpp.obj` → `[3/3] Linking CXX executable openttd.exe`；`build\R3R_incbuild.done` ＝ `EXIT_CODE=0`；`src\train_cmd.cpp` @17:29:14 → `train_cmd.cpp.obj` @17:29:54 → `build\openttd.exe` @17:31:15（51 868 160 B）；`read_lints` 0 条；exe 字面量逐条命中：`EXTEND-BACKOUT veh=%u`／`need=0x%X`／`resv=0x%X`／`led=%d`（4/4）。

**下一步（等玩家复跑）**：读 `EXTEND-BACKOUT ... next=1093,139 reason=choice-reserved ... track=N need=0xM resv=0xK led=L`，按上表选 (B′) 或 (C) 落地；本轮**不改任何行为**。

### 47.23 【收口】`led=23` —— 阻塞位的主人就是**请求方自己**（第 243 轮续四十六，2026-10-16）

**日志自证**：`build\R3R_debug.log` = 241 842 B / 2 856 行 / mtime 17:38:36，产生者 `build\openttd.exe` 17:31:15（51 868 160 B，含 `led=` 字段）⇒ **采样有效，读数可用**。

**决定性读数（2596→2851 共 40+ 行逐字相同，仅 owner 漂移）**：
`EXTEND-BACKOUT veh=23 origin=1090,139/8 stop=1092,139/8 next=1093,139 reason=choice-reserved err=0 owner=22 whole=1 track=2 need=0x25 resv=0x4 led=23`
（首 4 行 2529/2549/2556/2563 是 `owner=23 whole=0`，自 2571 起转 `owner=22 whole=1`，`track/resv/need/led` 全程不变。）

**掩码解码**：`resv=0x4` = `TRACK_UPPER`(bit2) 唯一被订；`need=0x25` = `TRACK_X|TRACK_UPPER|TRACK_RIGHT`；交集 = 0x4 ⇒ `track=2` 与 `resv & need` 口径吻合（续四十五的收紧生效）。`led=23` = **`R3RResvLedgerOwner(1093,139, TRACK_UPPER)` 返 23**。

**结论 1（定性升级，比续四十四更硬）**：`1093,139/TRACK_UPPER` 的作者是 **23 自己**。而 23 本次要延长的那条预留是 `origin=1090,139/8 → stop=1092,139/8`（止于 1092,139，**不含 1093,139**）⇒ 该位是 **23 名下、但 23 当前预留路径并不使用的陈旧位** ⇒ **23 被自己的陈旧位挡住（自锁）**。续四十四只能用走查矛盾「推断」它是陈旧位，本轮拿到账本凭据，推断改为实锤。

**结论 2（KI-359 第二次实证 + 「无日志行」情形实证）**：全日志 `1093,139` 只有 825/826 `REACH cur=` / 846/847 `RP cur=`（veh=11 的寻路**经过**该格，`bits=0x5`）与 1419 `CLEAR-PATH veh=11 tile=1093,139 td=8 phase=controller` + 1420 `UNRES-RAIL tile=1093,139 track=0`（11 释放的是 **track=0 = TRACK_X**，**不是** track=2=UPPER）⇒ 该格**没有任何** `SET-RAIL`/`CLEAR-RAIL` 探针行却账本有行 ⇒ 正是 §47.21 结论 5 预告的「账本有行也可以没有日志行（不在 watch 名单 / 写入时无 actor 日志）」。**故「搜不到建立行」已彻底不构成疑点。**

**疑点 REG-243-T（新登记，必须判清才可动手）**：`owner=22 whole=1` 的**语义**未定。本探针把「`GetTrainForWholeTileReservation()` 返非空」一律记 `whole=1`，而该函数据记忆（KI-207c，`src\pbs.cpp`）是「格上停车即主人，否则逐 track 双向回溯」。⇒ 两种可能：(i) 22 只是走查落点（回溯终点），23 仍可放行；(ii) **22 物理停在 1093,139**，那么放行 23 就是错的，正解是让 23 等（并改判为「活车合法占格」）。**判据**=读 `GetTrainForWholeTileReservation()` 实现 + 日志中 22 的位置（`TPR`/`mf=`/`RESERVECONSIST` 是否出现 1093,139）。**未判清前不得落地放行类修法。**

**修法方向（较续四十四的 (C) 更小、更可证）**：若 U-1 判为 (i)，最小修法是 **(C′)=「不得被自己名下的位挡住」**：`choice-reserved` 出口在决定拒绝之前，先问 `R3RResvLedgerOwner(ft.new_tile, blame_track)`，**若等于本车（或其链）索引、且该位不在本车当前预留路径上**，则视为陈旧位：先按 `R3RReleaseReservationSafe` 式子释放该位（四道校验：plain rail + 位仍在 + 无车 + 无其他主），再本 tick 继续延长。这比通用 (C)（四提交点账本驱动释放）小一个数量级，且只在「自己挡自己」这一形态生效，不会误放别人的位。若 (ii)，则 (C′) 作废，改判 REG-243-S 为「活车合法占格 + 陈旧位共存」，只保留陈旧位清理（(B′)/(C) 择一）。

**未做**：U-1 判读（下一步）；任何行为改动（本轮只加/读探针与判读）。

### 47.24 【判读】REG-243-T = (i)：`whole=1` 是走查落点，`1093,139` 上没有车（第 243 轮续四十七，2026-10-16）

**判据一（代码，`src\pbs.cpp:2208-2230`）**：`GetTrainForWholeTileReservation()` 的第一优先分支是
`if (Vehicle *v = GetFirstVehicleOnTile(tile, VehicleType::Train)) return static_cast<Train *>(v);`（**pbs.cpp:2213**），
只有**格上无车**时才 `GetReservedTrackbits(tile)`（:2215）并逐 track 双向 `R3RFollowReservationToOwner()` 回溯（:2222-2228）。
⇒ 探针把这两种情况一律记 `whole=1`，**单看该字段无法区分**，必须另证「格上是否有车」。

**判据二（日志，三条独立）**：
1. `build\R3R_debug.log` 全日志 `1093,139` 只有 veh=11 的寻路**经过**（825/826 `REACH cur=`、846/847 `RP cur=`，`bits=0x5`）与释放（1419 `CLEAR-PATH veh=11 tile=1093,139 td=8 phase=controller` + 1420 `UNRES-RAIL tile=1093,139 track=0`），**没有任何 `RESERVECONSIST`／`GEO`／`settle-*`／`TPR` 位置读数**把某辆车放在该格上。
2. 解挂几何：`DECOUPLE-INTERLOCK set head=23 follower=22 seam_car=25 tile=1086,139`（日志 2506）之后 `LOCO-AFTER-DECOUPLE veh=23 tile=1089,139`，而 23 的延长方向是 x 递增（`origin=1090,139 → stop=1092,139 → next=1093,139`）⇒ 拼接面 1086,139 落在 23 **后方**，22 作为 follower 只会在 seam 之后（x ≤ 1086），与 1093,139 相隔 7 格以上。
3. **账本反证（最硬）**：若 22 物理停在 1093,139，该格就是 22 的 consist 保护位，`ReserveTrackUnderConsist()` 会按链头 22 喂进账本（KI-359：六个 setter 无条件记账）⇒ `led` 应为 22；实测 `led=23` ⇒ 该位不是「站着的车」写的。

**结论**：REG-243-T = **(i)**。`owner=22 whole=1` 是走查沿残余位反向走到底所致（KI-359：走查回答的是「这条预留路径的另一端站着谁」），22 与 1093,139 没有物理关系 ⇒ §47.23 的 **(C′) 成立、可落地**。

**同时收获的新事实**：该陈旧位是 23 在**刚解挂之后**（`DECOUPLE-INTERLOCK set head=23 follower=22`，2506 行）立刻重订路径（`RESV-WATCH SET-RAIL tile=1091,139` / `1092,139 track=0`、`CRT veh=23 order=1 dir=5 origin=1090,139 td=8 found=1`）时撞上的 ⇒ **「解挂后机车立即被自己的陈旧位钉住」**，与 KI-330／REG-243 系列同族。另注：23 当前路径订的是 `track=0 (TRACK_X)`，阻塞位是 `track=2 (TRACK_UPPER)` ⇒ **轨位不同**，确系另一次预留的残留（非本次路径的一部分）。

### 47.25 【落地】(C′) 实现（第 243 轮续四十七，2026-10-16）

**落点**：`src\train_cmd.cpp` 的 `ExtendTrainReservation()`（定义 `:13105`）choice-reserved 分支里原本无条件 `break;` 的那一处（`:13191`），只改这一个 `.cpp`，**未碰任何 `src\*.h`** ⇒ 增量合法。

**逻辑（四条件同时成立才放行，否则照旧回退）**：
```cpp
const uint32_t r3r_own_key = R3RResvActorID(v).base();
if (r3r_blame_track != INVALID_TRACK && r3r_backout_ledger == (int)r3r_own_key &&
        R3RReleaseReservationLedgerChecked(r3r_own_key, ft.new_tile, r3r_blame_track, "stale-own-bit") &&
        !HasReservedTracks(ft.new_tile, TrackdirBitsToTrackBits(TrackdirReachesTrackdirs(ft.old_td)))) {
    R3RDbgWrite("STALE-OWN-BIT veh=%u tile=%d,%d track=%d led=%d cleared=1\n", ...);
    /* fall through: treat it as an ordinary choice */
} else {
    break;
}
```
1. `r3r_blame_track != INVALID_TRACK`：确实有「本次要跨的轨」落到「格上已订的轨」上；
2. `r3r_backout_ledger == (int)r3r_own_key`：**账本有行、且这行就是本车**（`led=-1` 无行、或属于别人，一律不放行——孤儿位留给审计/GC，别人的位一律不动）；
3. `R3RReleaseReservationLedgerChecked()`（`src\pbs.cpp:361`）返回真：该函数自带四道校验（plain rail + 位仍在 + 格上无车 + 账本主人是本 key），**真正决定放不放的是它**，本处仅作为第一道闸门；
4. 释放后**重检同一个表达式**（`!HasReservedTracks(...)`）：若该格还有别的被订轨与 `need` 相交，仍 `break` 回退（不会硬闯）。

命中时打 `STALE-OWN-BIT veh= tile= track= led= cleared=1`，控制流**不 break**，落到普通 choice 处理（`:13196` 的回退 + `return PBSTileInfo(ft.new_tile, ft.old_td, false)`）把剩余路径交给寻路器。

**为什么不会误伤**：(a) 只在「阻塞位记在本车名下」时进入；(b) 释放函数本身拒放「有车站在格上」与「账本已转给别人的位」；(c) 释放后重检不放行仍有别的主的位；(d) **本来就不放行任何别人的位**（条件 2 把 `led=别人` 全排除了）。⇒ 效果等价于「清掉自己的陈旧位后重新试一次」，不是「无视预留冲过去」。

**未做**：通用 (C)（耦合/解挂/逻辑翻/车库编辑四个提交点按账本批量释放本链全部行）；(B′)；读档后老旧陈位的批量清理；实测复跑（待玩家）。

### 47.26 【构建自证】(C′) 已编译进 exe（第 243 轮续四十七，2026-10-16）

**第一次构建失败**（值得记录）：`error C3861: "R3RReleaseReservationLedgerChecked": 找不到标识符`（`src\train_cmd.cpp(13204)`），`build\R3R_incbuild.done=EXIT_CODE=1`。原因：该函数**只在 `src\pbs.cpp:361` 定义，`pbs.h` 没有导出**（对外只有 `R3RReleaseReservationSafe`，而它恰好把"到底放没放掉"这个答案丢掉了）。修法：在 `src\train_cmd.cpp:2015` 既有手写声明区（`void DearticulateChainWithSnapshot(Train *head);` 之后）补一条前向声明 `bool R3RReleaseReservationLedgerChecked(uint32_t key, TileIndex tile, Track track, const char *why);`，**依然只动 .cpp，不改任何 .h**（避免 KI-183 全量重建）。签名照 `pbs.cpp:361` 逐字。

**第二轮构建（通过）**：

| 判据 | 实测 |
| --- | --- |
| 护栏 | `GUARD: incremental is safe (no header/lang file is newer than the newest object)` |
| 编译/链接 | `[3/3] Linking CXX executable openttd.exe` |
| 完成标记 | `build\R3R_incbuild.done` = **`EXIT_CODE=0`**（mtime 18:00:08） |
| 错误计数 | 日志无 `error C` / `fatal error` / `FAILED` / `build stopped` |
| 时间戳链 | `src\train_cmd.cpp` @17:57:13 → `build\openttd.exe` @**17:59:59**（51 868 160 B） |
| lint | `read_lints` 0 条 |
| 新字面量 | exe 内 `STALE-OWN-BIT` = **True**（28591 单字节无损核对） |

**改动范围**：仅 `src\train_cmd.cpp`（行为修复一处 + 手写前向声明一条），未碰任何 `src\*.h`。构建前已确认 `openttd.exe` 未在运行。

## 48. 第 243 轮续四十九：(B′) 与通用 (C)

### 48.1 【计划】定义取回（先落盘）＋ 本轮范围

玩家 2026-10-16 指示：「当前的问题已经解决了，不过通用 C、B′ 也可以做一下」。⇒ (C′) 收口（§47.26 已编译通过、游戏内复测待做），本轮继续做 **(B′)** 与**通用 (C)** 两项。

**定义（逐字取自本备忘，不重跑取证）**：

- **(B′)**（§47.22 判读表，本文件 `:2420`）：
  > `led=-1` ⇒ 无账本行 ⇒ **事实孤儿**（随存档带入／旧路径铺下）⇒ 修法 **(B′)**：审计对「**无账本行 ＋ 格上无车**」的位也判 stray 并交 `RESV-REAP`（边界：读档后全体位都无行，必须附加「**格上无车 且 走查落点不在本格**」才安全）。

- **通用 (C)**（§47.22 判读表，本文件 `:2421`）：
  > `led=<活车号>` ⇒ 账本主人仍活着的**陈旧位** ⇒ 修法 **(C)**：释放改成**账本驱动**——**耦合／解挂／逻辑翻／车库编辑四个提交点按账本释放本链全部行**，而不是按「此刻车节几何扫一遍」（正是 KI-203 记的老毛病：几何一变，旧位没人走到）。

- **与 (C′) 的关系**：(C′)（§47.25）只在「延长时被**自己**挡在 `choice-reserved` 这一条出口」生效，是"事后单点自愈"；(C) 是"提交点整体清账"，覆盖 (C′) 覆盖不到的形态（链编辑后那些**再也走不到**的位——正是 KI-203/KI-355 的泄漏源）。两者互补，不冲突。

**本轮取证清单（待做，逐条落盘）**：
1. `R3RReservationAudit()` 现状（stray 判据、`RESV-REAP` 四道校验、老化计数）——(B′) 的落点与安全边界。
2. 账本 API 全貌（`pbs.h` 声明：记／销／查／枚举某 owner 全部行）——(C) 需要"按 owner 枚举本链全部行"的能力，若无则须新增（**新增即改 `pbs.h` ⇒ 触发 KI-183 全量重编**，必须提前算代价）。
3. `R3RReleaseChainReservations()` 现状 + 四个提交点（couple / decouple / 逻辑翻 / 车库编辑）的落点行号。
4. 逻辑翻（`R3RFlipChainBySegments`）是否也是"提交点"——若翻转发生在耦合的折叠修正路径内，则耦合提交点已覆盖，不必单列。

**风险预判（先登记，避免后知后觉）**：
- **R-1**：(C) 在**耦合**提交点"按账本释放本链全部行"会把**正要使用**的位也清掉（耦合瞬间本链路径位仍需保留）。⇒ 必须限定"只释放**本链当前车节几何之外**的行"，而不是无条件全清。
- **R-2**：(B′) 的安全边界若写成「无账本行即 stray」，读档后**全体位**都无行 ⇒ 会一次性清光全图预留。⇒ 定义里已写明必须附加「格上无车 且 走查落点不在本格」，本条必须逐字实现。
- **R-3**：改 `pbs.h` ⇒ 全量重编（692 步，约 20 分钟 @ -j2）。若能把新能力做成 `train_cmd.cpp` 文件内的静态遍历（账本已是 `pbs.cpp` 文件内 static 容器）则可避免；**待查账本容器是否已导出**。

**未做**：以上全部取证与改码（本轮先落盘计划）。

### 48.2 【取证二→成本重估】通用 (C) 的机制**已经完备**，缺口只在三个提交点没调它（第 243 轮续四十九，2026-10-16）

**§48.1 的 R-1 风险已被现成机制解决，本轮不必新增任何 API、不必改 `.h`。**

**证据一：账本驱动释放已部分落地（KI-359）**。`R3RResvLedgerForget()` 现有调用点 4 处：

| 落点 | why | 含义 |
| --- | --- | --- |
| `src\vehicle.cpp:1252`（`PreDestructor`） | `"destroy"` | **卖车**已做 |
| `src\train_cmd.cpp:7938-7939`（`DecoupleTrain`，v+u 双调） | `"decouple"` | **解挂**已做 |
| `src\train_cmd.cpp:12967`（`FreeTrainTrackReservation`） | `"free-path"` | 放弃路径已做 |
| `src\train_cmd.cpp:15154`（审计内） | `VehicleID::Invalid()` | resync 入口 |

⇒ 通用 (C) 的四个提交点里，**解挂已做**，缺 **耦合 / 逻辑翻 / 车库编辑**。

**证据二：`R3RResvLedgerForget()` 自带 purposes 掩码 + 四道校验**（`src\pbs.cpp:430-475`）：
```cpp
const auto own_it = _r3r_resv_by_owner.find(key);
if (own_it == _r3r_resv_by_owner.end()) return;
std::vector<R3RResvLedgerRec> rows = std::move(own_it->second);   // 取该 owner 全部行
...
const uint8_t mask = R3RResvLedgerForgetMask(why);
for (const R3RResvLedgerRec &row : rows) {
    const uint8_t left = (uint8_t)(row.purposes & ~mask);
    if (left != 0) { ...++kept; continue; }                       // 还有别的用途 ⇒ 保留
    if (!R3RReleaseReservationLedgerChecked(key, row.tile, row.track, why)) { ...保留 }
}
```
⇒ **"按账本释放本链全部行"就是它**，且**不会误放正在使用的位**（purposes 未清空即保留；四道校验失败即保留）。**§48.1 的 R-1 风险不成立**（原预判"会清掉正要使用的位"是把 Forget 当成了无脑全清）。

**证据三：耦合提交点的洞由代码注释自陈**（`src\train_cmd.cpp:10421-10422`）：
> "(The couple/decouple commit points do run `R3RReleaseChainReservations()`, but **by then the damage is done for any bit whose ownership broke in the merge**.)"

而耦合提交点现状是**几何式** `R3RReleaseChainReservations(v, "couple")`（`:10841`）与 `(v, "couple-settled")`（`:10943`）—— 几何扫不到的（合并后已易主/无主的）正是 (C) 要补的。

**新陷阱登记 R-T1（必须在落地前处理，否则新调用是空操作）**：
`R3RResvLedgerForget()` 内部会 **`v->First()` 归一化到当前链头**（`pbs.cpp:442-447`）：
```cpp
uint32_t key = owner.base();
if (const Vehicle *v = Vehicle::GetIfValid(owner); v != nullptr) {
    if (const Vehicle *h = v->First(); h != nullptr) key = h->index.base();
}
```
⇒ 若在**链已重链接之后**传旧链头 id，`First()` 会把它归到**新链头**，于是只清新链头的行，**旧链头的行永远清不掉**（这恰是注释自陈的 "damage"）。
对照 `DecoupleTrain` 的双调（`:7938-7939`）之所以有效：注释写明是在**"just been cut in two, before any identity is rewritten"** —— 即切分后、身份改写前，此时 v/u 各自的 `First()` 已分别正确指向两个新链头。
⇒ **落地 (C) 时的顺序要求**：调用点必须落在「链头身份已定、但旧账本 key 仍可解析」的时刻；耦合尤其如此（合并前 u 的 `First()` 指向 u 自己，合并后指向 v）。

**待查（下一步，也是最后一步取证）**：
1. `R3RResvLedgerForgetMask(why)` —— 各 why 剥哪些 purposes（决定 `"couple"` / `"flip"` / `"depot-edit"` 三个新 why 是否会被掩码拒绝成空操作）。
2. `Couple()` 里**重链接的确切位置**（决定耦合处 Forget 插在哪一行、传哪个 id）。
3. 逻辑翻（`R3RFlipChainBySegments`）与车库编辑（`R3RSyncChainAfterDepotEdit`）是否只在**链头变化**时需要，还是根本不需要（车节 tile 未变、位未动）——若只是账本 key 漂移，则解法应是"重命名 key"而非"释放"（需评估是否值得为此改 `pbs.h`）。

### 48.3 【取证三】掩码/resync/审计现状 + (B′) 的安全实现方案（第 243 轮续四十九，2026-10-16）

#### 48.3.1 掩码与 resync 的准确语义

- `R3RResvPhaseIs(a,b)`（`pbs.cpp:192-197`）是**精确 `strcmp`**（逐字符比到 `\0`），不是前缀匹配。
- `R3RResvLedgerForgetMask(why)`（`pbs.cpp:240-243`）：
  ```cpp
  return R3RResvPhaseIs(why, "free-path") ? (uint8_t)R3R_RESV_PURPOSE_PATH : (uint8_t)R3R_RESV_PURPOSE_ALL;
  ```
  ⇒ **只有 `"free-path"` 剥 PATH；任何其它 why（含新加的 `"couple"`/`"flip"`/`"depot-edit"`）都走 `ALL`**，即**连 CONSIST 位一起放**。链还在跑时这**不能接受**（KI-362 老毛病：车下的位被放掉，下一 tick `ReserveTrackUnderConsist()` 重新订，每 tick 一轮）。
  ⇒ (C) 落地时**必须同时改 `R3RResvLedgerForgetMask()`**，把新 why 归入 PATH-only 名单（纯 `.cpp` 改动，增量合法）。
- purposes 只有两类：`R3R_RESV_PURPOSE_PATH=0x01` / `R3R_RESV_PURPOSE_CONSIST=0x02`（`pbs.cpp:153-157`）。相位归 CONSIST 的只有 `"consist"` / `"clear-consist"` / `"waiter-platform"` 三个（`:218-225`），其余一律 PATH。
- `R3RResvLedgerForget()` 尾行日志（`:490-491`）：`RESV-LEDGER-FORGET veh=%u why=%s mask=0x%X released=%u kept=%u`。
- `R3RResvLedgerResync()`（`pbs.cpp:497-531`）：**只删「地图上已不存在该位」的行，不重算 owner**，也**不建立**新行。⇒ 它既不能解决 R-T1（链头漂移让 owner key 失配），也不能把读档后的活预留补进账本。

#### 48.3.2 (B′) 的落点：审计的两处 stray 判据

`R3RReservationAudit()`（`src\train_cmd.cpp:14950-15159`）现在两处判 stray：

- **站台/路点**（`:15025-15027`）：
  ```cpp
  const uint32_t lowner = R3RResvLedgerOwner(t, INVALID_TRACK);
  const bool ledger_dead = (lowner != R3R_RESV_NO_OWNER) && (Train::GetIfValid((size_t)lowner) == nullptr);
  if (owner == nullptr || ledger_dead) { ...stray... }
  ```
- **plain rail**（`:15057-15059`）：同构（`R3RResvLedgerOwner(t, track)`）。

两处都已实现 KI-359 的 `ledger_dead`（**有行但车没了**），**缺的正是 (B′) 要补的 `lowner == R3R_RESV_NO_OWNER`（根本没有行）**。

老化/释放是统一的：`strays_now` → `_r3r_resv_stray_age` 每轮 `++`，`age >= R3R_RESV_REAP_AUDITS (=3)` 才 reap；reap 前重验（站台 `:15091-15096`、plain rail `:15111-15115`，都含「格上无车」「位仍在」）。

#### 48.3.3 (B′) 的**安全实现**：不能用"无行即孤儿"（R-2 成立），但有一条零 API 的强判据

**R-2 确认成立**：`_r3r_resv_by_owner` 是 `pbs.cpp` 文件内 static 内存态（`:159`），读档期 `_m` 由反序列化直写、**绕过六个 setter**（注释自陈："a writer which bypasses the setters (MakeStation() rebuilding the tile, SetRailStationPlatformReservation() clearing a platform)"，`:137-140`）⇒ **读档后账本基本为空**，而存档里的预留**全是活预留**。
⇒ 若把 (B′) 实现成"`lowner == NO_OWNER` 即 stray"，则**读档后 3 次审计内会把全图所有活预留清光**。§48.1 预判的 R-2 **属实**，备忘 `:2420` 里写的两个附加条件（格上无车 且 走查落点不在本格）**不足以**挡住它——因为读档后的活预留恰好满足这两个条件（格上确实无车、走查沿该位走到路径另一端的车）。

**可行解法（零新 API、零 `.h` 改动）**：对"无行"的位，再问一句**「该位在不在其走查落点车的**自身当前预留路径**上」**——
- 在 ⇒ 是该车的活预留（读档带入未重建，或刚订下但账本被 resync 丢过）⇒ **不是** stray；
- 不在 ⇒ 走查能答"有主"纯粹是**沿残留路径走到底恰好遇到这辆车**（KI-355 layer 2 的形态）⇒ **就是孤儿**，判 stray。

判据用的现成 API 是 `R3RCollectReservedPathTiles(head, cb, ctx)`（本文件 `:10604` 已在用；`R3RReleaseChainReservations` 的 KI-211 注释说明它"walk the tiles and bits this chain booked *itself*, forward from the chain head along its own reservation"，且**在 pbs.cpp 内以第一辆车为界做了 fence**，不会跨到邻车）。
⇒ 效果：**读档活预留 `→ 在路径上 → 不判`**；**旧路径遗留位 `→ 不在路径上 → 判 stray`**。正是 (B′) 想要的，且不需要账本先被重建。

**成本控制（必须做，否则审计变贵）**：每位调一次 `R3RCollectReservedPathTiles` 是 O(位 × 路径长)。按 **owner 分桶缓存**：每轮审计对每个不同的落点车只收集一次路径集合（`btree::btree_set<uint64_t>`，键 `(tile<<3)|track`），且**只对"无行"的位**做这件事（有行的位走原判据）。

#### 48.3.4 (C) 的剩余未知项（下一步唯一要查的）

`R3RResvLedgerNote()` 的反向表分支（`pbs.cpp:291` 起）在 **`bit_it->second != owner`（同一 bit 被另一个 owner 记账）** 时怎么处理，决定了 (C) 在**逻辑翻 / 车库编辑**两处能否"零迁移成本"落地：

- 若 Note 会把该 bit **重指派**给新 owner（覆盖 `_r3r_resv_owner_of_bit` 并清掉旧行的该位）⇒ 那么**链头一变，下一次 `ReserveTrackUnderConsist()` 就自动把 CONSIST 位的 owner 迁到新链头**，R-T1 自愈，(C) 只需在提交点加一次 PATH-only 的 `Forget(旧链头, ...)`（清失效路径位）。
- 若 Note 直接 `return`（保留旧 owner）⇒ 旧 key 的行永久失配，必须新增"迁移 key"能力（改 `pbs.h` ⇒ 全量重编 692 步 ≈ 20 min @ -j2），或退而求其次：在提交点显式 `Forget(旧链头, "<why>")` 并在**同一处**重新 `Note` 一遍当前车节的位。

**登记**：R-T2 = `_r3r_resv_by_owner` 是否 NOSAVE ⇒ **已确认 NOSAVE**（`:159` 文件内 static，无存档块引用），这正是 R-2 的根源，也是 (B′) 必须走 §48.3.3 那条路的原因。

### 48.4 【取证四·§48.3.4 结案】R-T1 不是"顺序讲究"，是一堵**墙**（第 243 轮续五十，2026-10-16）

**结论句：账本的"按 key 查行 + 按 key 释放"两处都各自把 key 归一到 `First()`，因此一个已被并链吞掉的中段车号（合并前的 u 链头）**永远无法**通过 `R3RResvLedgerForget()` 释放它自己名下的行。**

两处证据（原文）：

1. **查行**（`pbs.cpp:442-450`）：
   ```cpp
   uint32_t key = owner.base();
   if (const Vehicle *v = Vehicle::GetIfValid(owner); v != nullptr) {
       if (const Vehicle *h = v->First(); h != nullptr) key = h->index.base();   // ← 归一化
   }
   const auto own_it = _r3r_resv_by_owner.find(key);                            // ← 按归一化后的 key 查
   ```
2. **释放闸门**（`pbs.cpp:367-382`）同样归一化：
   ```cpp
   const uint32_t booker = R3RResvLedgerOwner(tile, track);
   if (booker != R3R_RESV_NO_OWNER && booker != key) {
       uint32_t own_head = R3R_RESV_NO_OWNER;
       if (const Train *k = Train::GetIfValid((size_t)key); k != nullptr && k->First() != nullptr) {
           own_head = k->First()->index.base();                                 // ← 又归一化
       }
       if (booker != own_head) { ...return false; }                             // ← 拒放
   }
   ```
   ⇒ 合并后传 `u_old`：`own_head == 新链头`，而 `booker == u_old` ⇒ **`booker != own_head` ⇒ 直接拒放**。

**推论（顺序要求被证伪，改成"入口要求"）**：能给"被吞链"清账的唯一时机是**它还是自己链头的时候**，即**物理重链接之前**（对照 §48.2 里 `DecoupleTrain` 的双调之所以有效："just been cut in two, before any identity is rewritten"）。

**但 TryTrainCouple 会被反复调用（失败重试）**，所以在它入口对 **v（主动机车）** 调 Forget = 每次重试都放掉机车的在用路径位 ⇒ 不可接受（KI-362 同型抖动）。
⇒ **(C) 的落点定为：只对被吞的被动侧 u**（`u` 是挂接目标，按 `R3RIsCoupleTarget` 必然停驻；KI-206/208 本就已清它的出站路线），在 `Couple()` 内、**`TryTrainCouple()` 调用之前**（即最后仍是两个独立链、且已决定提交的时刻）调 `R3RResvLedgerForget(u->First()->index, "couple")`。
⇒ 合并提交后再对**新链头**补一次（`Forget(merged_head, "couple")`），它是 `R3RReleaseChainReservations(v, "couple")` 的账本版超集（几何扫不到的、`GetTrainForReservation` 归错人的行，只有账本能答）。

**逻辑翻 / 车库编辑两处的判决（本轮结论）**：
- 逻辑翻 `R3RFlipChainBySegments` 的**全部 4 个调用点都在 TryTrainCouple 内部**（`train_cmd.cpp:10115 / 10150 / 10212 / 10215`），也就是说它们都跑在**试验链（可能被 `RestoreTrainBackup` 回滚）**上；在其中释放 v 的路径位同样会落进"失败重试 ⇒ 每 tick 放一次"的抖动。**⇒ (C) 不在逻辑翻处落点**（并把理由写进代码注释，避免将来有人"顺手补上"）。
- 车库编辑 `R3RSyncChainAfterDepotEdit`（`:6377`）的两个调用点（`:3618/:3619`、`:3735`）在 `ArrangeTrains`（`:3382/:3684`）**之后**，重链接已完成 ⇒ 同 §48.3.4 的墙，传旧链头是空操作；而库里车 `cur_speed==0`、路径位本来就在进库时释放 ⇒ **收益接近零，本轮不落点**，登记为"已评估/不做"。

### 48.5 【判决·(B′) 改用"全体列车路径并集"】§48.3.3 的"问落点车自己的路径"会误杀耦合路径（第 243 轮续五十，2026-10-16）

**§48.3.3 方案的漏洞（本轮新发现，必须换掉）**：读档后账本为空，此时**机车正在铺设的挂接接近路径**也是"无行位"；该路径的走查落点恰好是**被挂的等待车底**（路径终点就是车底那一格），而这一格**不在等待车底自己的路径上** ⇒ 按 §48.3.3 会被判 stray ⇒ 3 次审计后 `RESV-REAP` **放掉机车正在用的接近路径**。⇒ 这正是 R-2 的具体致命实例，§48.3.3 的"落点车自己的路径"判据不成立。

**改用判据（保守、安全、实现简单）**：对"无行（`lowner == R3R_RESV_NO_OWNER`）"的位，**只有当该位不在「全图所有列车自身预留路径的并集」里时**才判 stray。
- 读档后：所有活预留（含机车的挂接接近路径，它在**机车自己**的路径上）都落在并集里 ⇒ **不判**（R-2 解决）。
- 真孤儿走廊：不在任何车的路径上 ⇒ 判 stray ⇒ 沿用既有 3 次老化 + reap 前四道重验（`:15091-15096` / `:15111-15115`）。
- 代价：每轮审计多一次"遍历全图列车 × `R3RCollectReservedPathTiles()`"（`pbs.cpp:2538`，该函数自带"同格两车则整格不采"的保守守卫）；只在**确实存在无行位**时才构建，构建结果用 `std::unordered_set<uint32_t>`（键 `tile.base()`）缓存整轮。
- 刻意**不按 track 细分**（只记 tile）：粗一点只会让判据更保守（少判 stray），不会误杀。

**登记 R-T3（已解除）**：`R3RResvLedgerForgetMask(why)`（`pbs.cpp:240-243`）当前只认 `"free-path"` 为 PATH-only，其余**一律 ALL**；⇒ (C) 落地必须**同时**把 `"couple"` 归入 PATH-only，否则链还在跑时把车下的 CONSIST 位放掉（KI-362 每 tick 一轮）。本轮落地即含此改动。

### 48.6 【落地】(C) + (B′) 实际落点与三处口径修正（第 243 轮续五十，2026-10-16）

**(C) 已落地**（3 处编辑）：
1. `pbs.cpp` 的 `R3RResvLedgerForgetMask()`：`"couple"` / `"flip"` / `"depot-edit"` 加入 PATH-only 分支（R-T3 解除）。
2. `train_cmd.cpp` 的 `TryTrainCouple()` 入口（`v->First() != v || u->First() != u` 两道 early-return 之后、任何 `ArrangeTrains()` 之前）：`R3RResvLedgerForget(u->First()->index, "couple");` —— **只清被动侧 u**（§48.4 的墙：合并后 u 名下的行再无人能释放；u 是挂接目标必然停驻，重复释放幂等）。
3. `train_cmd.cpp` 的合并提交点 `R3RReleaseChainReservations(v, "couple")` 之后：`R3RResvLedgerForget(v->First()->index, "couple");` —— 几何版的账本超集。

**逻辑翻 / 车库编辑两处：本轮不落点**（理由已写入 §48.4，判决不变）——逻辑翻 4 个调用点全在 `TryTrainCouple` 内部的**试验链**上（会被 `RestoreTrainBackup` 回滚，释放 = 失败重试每 tick 一次）；车库编辑两处调用点在 `ArrangeTrains` 之后（撞 §48.4 的墙）+ 库里 `cur_speed==0` 路径位本已释放 ⇒ 收益≈0。

**(B′) 的三处口径修正**（相对 §48.3.3 / §48.5）：

**修正 1：站台/路点整格位分支不适用 (B′)。** 读 `train_cmd.cpp:15113-15117` 的整格 reap 前四道重验后发现：它的第二道就是 `if (GetTrainForWholeTileReservation(tile) != nullptr) continue;` —— 与 stray 判据用的 `owner` 是**同一个调用**。所以在整格分支上加 (B′)（即 `owner != nullptr` 时也判 stray）只会生产**永远 reap 不掉**的 strays，而 `strays_now` 非空会让 `_r3r_resv_audit_has_pending = true`（`:15181`）⇒ 审计从 2048 节拍**永久降级**成 256 节拍（纯性能损失，零收益）。⇒ **(B′) 只在 plain-rail 分支落地**。

**修正 2：并集必须"完整"才可用。** `R3RCollectReservedPathTiles` 的 KI-211d 守卫（`pbs.cpp:2559-2566`）在"某辆车的格上有另外一条链"时**整条链一格不采** ⇒ 并集出现**洞**，而那个洞恰好就是"刚解挂/刚挂上、两车同格"这个最危险的时刻。⇒ 新增的收集函数 `R3RResvCollectAllPathTiles()` **回传 bool**：一旦发现任何链处于同格状态即返回 false；调用方拿到 false 时**本轮完全不做 (B′) 判定**（保守：无行位一律按旧口径 = 不判 stray）。代价是那种时刻 (B′) 静默失效，但 (B′) 需要 3 次**连续**审计才 reap，晚一轮无妨。

**修正 3：容器用 `std::unordered_map<uint32_t,uint8_t>` 而非 `std::unordered_set`。** 全工程只有 5 个文件用到 `unordered_set` 且本文件不在其中，`std::unordered_map` 已是本文件与审计函数的既有依赖（`:14990`）⇒ 不改任何 `#include`（也就不会碰 `.h` / 触发全量重编）。

**落点**（均只改 `src\train_cmd.cpp`）：
- 新增 `static bool R3RResvCollectAllPathTiles(std::unordered_map<uint32_t, uint8_t> &out)`，插在 `R3RReservationAudit()` 的文档注释之前（`:14942` 一带）。
- `R3RReservationAudit()` 内、tile 循环之前：懒构建的 `bit_on_some_path(t)` lambda（缓存整轮）。
- plain-rail 分支：`const bool ledger_absent = (owner != nullptr) && (lowner == R3R_RESV_NO_OWNER) && !bit_on_some_path(t);`，并入 `if (owner == nullptr || ledger_dead || ledger_absent)`。
- 两条 STRAY 日志行尾追加 ` why=%s`（`walk` / `dead` / `norow`），让现场日志能区分本次是哪种口径命中（旧前缀逐字不变）。

### 48.7 【构建自证】(C) + (B′) 增量构建通过（第 243 轮续五十，2026-10-16 18:24）

- 复用既有 `_tmp_inc_build.cmd`（未新建任何 .cmd）；先确认 `openttd.exe` 未在运行（进程数 0）。
- `build\R3R_incbuild.guard.log`（18:22:31，本轮）= `GUARD: incremental is safe (no header/lang file is newer than the newest object)` ⇒ 纯 .cpp 改动，无全量。
- `build\R3R_incbuild.log`：`[2/4] src\pbs.cpp.obj` → `[3/4] src\train_cmd.cpp.obj` → `[4/4] Linking CXX executable openttd.exe`；`build\R3R_incbuild.done` = `EXIT_CODE=0`（18:24:32）。
- 时间戳链：`src\pbs.cpp` 18:17:30 → `pbs.cpp.obj` 18:22:55；`src\train_cmd.cpp` 18:22:01 → `train_cmd.cpp.obj` 18:22:56；`build\openttd.exe` 18:24:14（51 868 160 B）晚于全部三者的最晚者。`read_lints` 两文件 0 条。
- exe 字面量核对（findstr，命中原文）：
  - `RESV-AUDIT-STRAY %s tile=%d,%d track=stn kind=%s age=%u lowner=%d why=%s`
  - `RESV-AUDIT-STRAY %s tile=%d,%d track=%u bits=0x%X age=%u lowner=%d why=%s`
  - 以及 `walk` / `dead` / **`norow`**（`norow` 是 (B′) 独有分支的字面量 ⇒ (B′) 确在二进制里）。
  - 注：(C) 用的 `"couple"` 字面量太通用（全工程几十处），无法用字符串自证；改以 obj 时间戳 + 编译成功 + lint 0 为证。

**残留/未确认（本轮登记）**：
- **U-1（已由"完整性"闸门挡住，但代价是真）**：只要有**任何**一条链处于"两车同格"状态，`R3RResvCollectAllPathTiles()` 即返回 false ⇒ **整轮 (B′) 静默失效**。这是为了堵 §48.5 的 R-2 洞（同格链零贡献 ⇒ 并集有洞 ⇒ 会误判活路径为 stray）。后果 = 忙场景里 (B′) 经常不生效（只是"晚几轮"，不会误杀）。
- **U-2**：并集的另一处天然洞 = "另一列车站在某条活预留的**中间**"（`FollowReservationEnumerate` 的 FRF 围栏在遇到车时停）。此时该车路径**远端**的位不在并集里 ⇒ 会被判 stray。判定：这类位本身就是"被堵住后的残留"（车会重新寻路并释放），reap 属期望行为；但要留意现场若出现 `why=norow` 且该格紧邻一辆停驻车，需复核。
- **U-3**：`(B′)+reap` 依赖"审计整轮在同一 tick 内完成"这一事实（并集与 reap 前四道重验在同一次调用里，中间不改地图）⇒ 无需在 reap 处重建并集。若将来把审计拆成跨 tick 两段，此前提即失效，必须在 reap 处重建。
- **U-4**：控制台/游戏内复测未做（见 §48.8 判据）。

### 48.8 【复测判据】(C) + (B′) 现场判读表（第 243 轮续五十，2026-10-16）

**(C) 三条**：
1. **正判据**：耦合成功时若有被吞链 u 出站路线残留 ⇒ `RESV-LEDGER-FORGET veh=<u 的号> why=couple mask=0x1 released=N kept=M`，且随后 `RESV-AUDIT-STRAY` 里**不再**出现 u 名下那条出站路线的格（旧实现漏掉的那批）。
2. **负判据（防抖动）**：`COUPLE-FAIL` 反复重试期间**不得**出现每 tick 一次的 `RESV-LEDGER-FORGET ... why=couple`（因为只清 u 且 u 无行即早退、不写日志；pbs.cpp:458）；一旦出现高频同类行即为 KI-362 同型抖动，须回退该调用点。
3. **负判据（防误清）**：合并链**车下**（CONSIST 用途）的位不得被这次 forget 放掉 ⇒ 日志里 `kept>0` 且其格恰是车节所在格；同时不得出现 `RESV-GC ... why=couple-gone`（= 把车下位放掉了）。

**(B′) 三条**：
4. **正判据**：存量孤儿走廊（1090..1112 × y=129..137 一族、1093,87..1112,106 一族）应出现 `RESV-AUDIT-STRAY ... age=0/1/2/3 ... lowner=-1 why=norow`，随后 `RESV-REAP ... track=%u` 释放，`RESV-AUDIT-SUMMARY ... strayTiles=` 归 0（旧实现这些格因为"走查能找到车"而永远不进 stray）。
5. **负判据（最关键，防误杀活路径）**：**读档后**（账本全空、所有活预留都是 `lowner=-1`）不得出现"某列车刚订好的路径被 reap"的形态 —— 判读三连：①读档后仍在趋近挂接的机车必须照旧 `COUPLE-OK`；②reap 之后不得出现该车的 `TPR-BLOCKED` / 预留 `res=0` 刷屏；③`why=norow` 的 STRAY 行所在格，事后人工核对不应落在任何列车的路径预览上。
6. **格式判据**：两条 STRAY 行只多出尾部 ` why=walk|dead|norow`，`walk` / `dead` 两类的旧前缀与语义逐字不变（`why=walk` 对应 `lowner=%d` 可为任意值但 `owner` 为空；`why=dead` 对应 `lowner` 指向已删车）。

**旁证/回归**：
7. `RESV-AUDIT-SUMMARY` 的节拍不得从 2048 永久降到 256（若 (B′) 制造了 reap 不掉的 stray，`_r3r_resv_audit_has_pending` 会恒真 ⇒ 表现为 SUMMARY 行明显变密）。这正是"整格位分支不落 (B′)"（§48.6 修正 1）要防的。
8. 既有 KI-346 / KI-345 修复的正判据不得回退：`owned` 计数、`unbooked=0`、双槽格订位（`REACH/RP cur=... trk=` 序列）逐字不变。

### 48.9 【修复前基线】现存在库日志的读数（第 243 轮续五十补记，2026-10-16，只读取证）

- **样本**：`build\R3R_debug.log`（`2026-10-16 18:10:53`，822 953 B，≥11 216 行），由**本轮 (C)/(B′) 落地之前**的 exe 产生。核对过全仓 `*.log`：**没有任何运行日志晚于新 exe（`build\openttd.exe @18:24:14`）** ⇒ **复测尚未开始**，本轮无新现场可判读。
- **`RESV-AUDIT-SUMMARY` 全日志仅 2 条**：
  - `:196` `tick=6930 reservedTiles=58 owned=38 strayTiles=20 reaped=0 pendingStray=20`
  - `:4492` `tick=7186 reservedTiles=35 owned=35 strayTiles=0 reaped=0 pendingStray=0`
  - ⇒ 两条相隔 **256** tick（6930→7186），即当时处于 256 档；`pendingStray` 20→0，但 `reaped=0` ⇒ 那 20 格是**按旧口径走查判 stray 后自然清掉**的（`RESV-REAP` 未记数），与本轮 (B′) 无关。
- **`RESV-AUDIT-STRAY`（`:181-190`，共 10 行、全在同一 tick）**：`tile=1103,97 / 1104,98 / … / 1112,106`，一律 `track=4 bits=0x10 age=0 **lowner=-1**` ⇒ 属**旧口径 `why=walk` 形态**（走查找不到人），**不是** (B′) 要救的"无账本行 + 走查有主"形态；且到 `:4492` 已 `strayTiles=0`。
- **对复测判据的直接影响（重要）**：
  - **判据 4 有"样本缺失 ⇒ 不可判"的风险**：本会话里 (B′) 的目标族（`1093,139` 一族）**完全没出现** ⇒ 若玩家仍在同一存档/同一路线复跑，不会打出任何 `why=norow` 行。复测必须选**含该族格**的场景（例如先重现续四十一~四十四 的 `1093,139` 挡点现场）。
  - **判据 1 同理**：`why=couple` 在基线里**零命中**（旧 exe 根本没有该调用点）⇒ 复跑后**首次**出现 `RESV-LEDGER-FORGET ... why=couple` 才算 (C) 生效的正面证据。
- **可作对照的既有读数（非本轮引入）**：`RESV-LEDGER-FORGET veh=2 why=free-path mask=0x1 released=2 kept=1`（`:9842..:10168`，每 5~6 行一次）＝既有 free-path GC 节拍；`veh=11 why=decouple mask=0x3 released=1 **kept=8**`（`:10336`）＝decouple 走 ALL 掩码且**保留车下 CONSIST 位** ⇒ 直接可作**判据 3（`kept>0`）**的对照读数。

## 49. 第 243 轮 (C)/(B′) 实机复测判读（2026-10-16）

**样本**：玩家投递 `D:\temp\codebuddy-dropped-files\319525c4-...\R3R_debug.log`，930 189 B / 11 981 行 / `18:38`（`cmd /c dir`）⇒ 晚于新 exe `18:24:14` ⇒ 有效。首行 `SEGLOAD-PARK`(:1) ⇒ 读档开局，与 §48.9 同档。

**计数**：`why=norow`=0；`why=couple`=4；`why=walk`=40；`why=dead`=0；`RESV-AUDIT-STRAY`=40；`SUMMARY`=3；`RESV-REAP`=**0**；`RESV-LEDGER-FORGET`=74；`RESV-PURGE`=0；`COUPLE-OK`=10；`DECOUPLE-DONE`=10。

**判据 1（(C)）＝触发证实、释放未观察到**：4 行 `RESV-LEDGER-FORGET veh=12 why=couple mask=0x1 released=0 kept=8`(:2026)、`veh=10`(:7721)、`veh=19`(:9171)、`veh=3`(:11065) ⇒ 耦合提交点确按 (C) 调了释放（正面证据），但 **`released=0`** ⇒ 账本行内无 PATH 位可放 ⇒ **四次全空转**，判据 1 实质效果无法验证。`kept=8` 全为 CONSIST 位（须保留，行为正确）。

**判据 4（(B′)）＝未覆盖**：`why=norow` 零命中，与 §48.9 事前预判一致。

**判据 7（回归）＝通过**：40 条 STRAY 全 `why=walk`（:171-190 / :5119-5138，20 格走廊 `1093,87..1112,106 track=4 bits=0x10 lowner=-1`，age 0→1），与基线(:181-190)逐字同形。

**SUMMARY 三条**：`:196` `tick=6930 rt=58 owned=38 stray=20 reaped=0 pend=20`；`:5142` `tick=7186 rt=43 owned=23 stray=20 reaped=0 pend=20`；`:8810` `tick=7442 rt=71 owned=71 stray=0 reaped=0 pend=0`。间隔均 256 tick ⇒ 全程"脏标记"档。

**疑点**：
- **U-5**：(C) 4/4 `released=0 kept=8` ⇒ 耦合前 `u->First()` 账本行疑无 PATH 位；需取数确认 (C) 是否空转（若空转，KI-363 根因判断需重审）。
- **U-6**：同一 20 格走廊 `tick=6930/7186` 判 stray（走查无人）→ `tick=7442` 整体变 owned（走查有人，owned 23→71）⇒ "格主"取决于当时是否恰有活路径终止其上（KI-359 固有抖动），可能是玩家症状本体。
- **U-7**：第 240 轮 KI-346 判据 ① 要求该 20 格不再出现，本日志仍出现 ⇒ 需分辨"随档带入的存量"（第 240 轮明言存量不追溯）还是修复未生效。

## 50. 决定性取证：`1092,136` 的单格档案 —— 症状侧的真凶（第 243 轮续五十一，2026-10-16）

**动机**：§49 的探针计数只能证明"(C) 调了、(B′) 没触发"，无法回答玩家"问题还是那样"。故改从**症状侧**取数：日志尾部 `EXTEND-BACKOUT veh=11` 共 **454 次**（`:11408` 起至 `:11862`，每 tick 一组 `UNRES-RAIL`×2 + `TRP` 环绕）⇒ 必有某格长期占位。

**该格全日志出现谱（`1092,136`，逐字取自本样本）**：
- `:6618` `RESV-WATCH SET-RAIL tile=1092,136 track=0 phase=yapf-resv actor=22 head=22`（veh=22 订下）
- `:6675-6677` `CLEAR-PATH veh=22 tile=1092,136 td=0 phase=controller actor=24` → `UNRES-RAIL tile=1092,136 track=0 phase=controller actor=24` → `RESV-WATCH CLEAR-RAIL ... actor=24 head=22`（**veh=22 那一笔已正常释放**）
- `:7687` `RESV-WATCH SET-RAIL tile=1092,136 track=0 phase=yapf-couple actor=23 head=23`（**veh=23 订下**）
- `:8807` `RESV-AUDIT-OWNER tick tile=1092,136 track=0 owner=veh23 bits=0x1`（审计判**有主**）
- `:11408` 起 `EXTEND-BACKOUT veh=11 ... next=1092,136 reason=reserve-fail err=0 owner=-1 whole=0 track=-1 need=0x0 resv=0x0 led=-1` × 454

**决定性事实（两次独立检索互证）**：对 `1092,136` 检索 `CLEAR-PATH|UNRES-RAIL|CLEAR-RAIL|RESV-LEDGER` 全部种类，**只命中 `:6675/:6676/:6677` 三条**（皆属 veh=22 那一笔）。⇒ **veh=23 在 `:7687` 订下的这笔预留，直到日志结束（11 981 行）零释放记录**。

**订位者的实际动向**：`veh=23` 是一列 `OT_GOTO_COUPLE`(order=16) 机车：
- `:6554` `SKIP-STOPPED veh=23 order=5 real=1 spd=0 tile=1092,13`
- `:8348` 起 `COUPLE-FAIL loco=23 order=16 tx=1112 ty=125` + `COUPLE-WAIT-OFFDEST veh=23 tile=1112,125 mf=1112,128 real=6 ... destTile=287807 onDepotTile=0 onStnTile=0`，共 **13 组**（`:8348/:8386/:8440/.../:11726`）
⇒ 订完这笔预留后，**veh=23 自己跑到了 `1112,125` 并长期停在那里反复 `COUPLE-FAIL`**（`%s` 与 `1092,136` 相距 20+11 格）。它再也走不到 `1092,136`，预留却永久挂着。

**结论（本轮核心）**：
1. **`(B′)/(C)` 在结构上不可能清掉这一格**：审计走查 `GetTrainForReservation` 能经账本回溯到活着的 veh=23 ⇒ `:8807` 判 `owner=veh23` ⇒ 既不进 `strayTiles`，也就不可能 reap。这正是 §47.23「`led=23` —— 阻塞位的主人就是**请求方自己**」的**同形态复现**，只是挡点从 `1093,139` 换成了 `1092,136`、受害者从 veh=11 的另一次尝试变成 454 次死循环。
2. **玩家可见症状 = `EXTEND-BACKOUT veh=11 reserve-fail` 死循环**（每 tick 回退 `1091,135`+`1092,135` 再重试），与玩家"问题还是那样"吻合。
3. **`(C)` 空转的解释**：(C) 只放 `u->First()` 账本行内的 **PATH** 位（§48.6），而被吞链 u 本就不持有那条出站路线的 PATH 位 ⇒ `released=0 kept=8`（4/4）是**预期内的空转**，不代表 (C) 错，但也确实**没治到本症状**。

**新增疑点**：
- **U-8**：`EXTEND-BACKOUT` 在 `reason=reserve-fail` 分支下 `need=0x0 resv=0x0 led=-1 track=-1 owner=-1` 全为空 ⇒ 该分支的掩码字段疑未填（与 §47.22 的字段落地口径需复核）。
- **REG-243-U（新，高）**：`phase=yapf-couple` 阶段订下的预留，其**回收责任人不明** —— 寻路成功则转为路径预留、失败/放弃时应由谁释放？现场证据 = veh=23 寻路后跑到别处，该位零释放 ⇒ 建议为此相位补一条"**订位者离开该路径即回收**"的账本规则。

**对"读档后老旧陈位批量清理"的直接影响**：清位口径**不能**继续用"走查有主即保护"（那正是 (B′) 的判据），否则本格永远清不掉。可行口径 = **"账本有行才算有主"**：读档后账本为空 ⇒ 全部活车按 §48.5 的"全体列车路径并集"重建一次账本，重建后**不在并集里的位一律清**；此后只在命令/提交点增量维护 ⇒ `led=-1` 的位自然成为清理对象。

## 51. 根源定性：为什么"给预留加主人"会不断养出地鼠（第 243 轮续五十一，2026-10-16）

**玩家提问原话**：「这一切地鼠和预留问题的源头是什么，是不是我们给地图上一切列车预留都添加的主人」。

**结论：方向对，但精确的说法是 —— 不是"加主人"这个动作错，而是我们只加了"一半"，且加的是一个会变的 key。**

### 51.1 原生引擎的模型：无主 + 几何释放（自洽，因为不依赖记忆）
- 预留在地图上只是几个 bit：`rail_map.h` 的 `_m[t].m2` bit 8-11（`SetTrackReservation` / `GetRailReservationTrackBits` / `HasReservedTracks`）。
- **没有任何字段记录"这格归谁"**。释放靠**几何/条件**：车走开、路径重算、`FreeTrainTrackReservation()` 按**调用那一刻自己的当前路径**逐格清。
- `pbs.cpp` 的 `GetTrainForReservation()` 是**只读推断**（从位回溯到某辆车的路径），**不是真值**。原生世界从不需要问"这格归谁"，它只需要"沿这条路往前走"。
⇒ 这套模型**自洽**，因为它的正确性不依赖任何跨 tick 的记忆。

### 51.2 R3R 为什么必须加"主人"（动机是对的）
R3R 要支持**链编辑**：耦合 / 解挂 / 逻辑翻 / 段升降 / 车库拖动 / 克隆。这些操作会改**链序**与**链头身份**，于是：
- 旧链头不再是链头（被并链吞掉）⇒ `R3RReleaseChainReservations()` 按几何反查**找不到该清谁**；
- 这正是 `train_cmd.cpp` 里 KI-205 那段注释自陈的 "by then the damage is done for any bit whose ownership broke in the merge"。
⇒ 所以 R3R 引入账本（`_r3r_resv_ledger`，key = `v->First()->index`）：「我订过哪些位」这件事**必须被记住**，否则链编辑后再也放不掉。

### 51.3 但只换了一半 —— 这是地鼠的真正源头
| 侧 | 现状语义 | 落点 |
|---|---|---|
| **订位侧** | **仍是几何的**（谁走到哪就订哪） | `yapf_rail.cpp` 的 `rp()`/`reach()`（yapf-resv）、**yapf-couple**、`choose-track`、`controller`、`ReserveTrackUnderConsist()`（车下 CONSIST 位）、waiter |
| **释放侧** | **变成账本的** | `R3RResvLedgerForget*()` / `R3RReleaseChainReservations()` / `RESV-REAP` / `RESV-PURGE` |

两套语义混用 ⇒ **每一条"几何订 / 账本放"的交界都是一只地鼠**。核实过的实例：
- `free-path` 整本抹账（KI-362）：几何订的 PATH 位与车下 CONSIST 位混在同一个 key 下，一放全放 ⇒ 订-放-订-放循环。
- `yapf-couple` 订了没人放（**KI-369**，本轮实证 `:7687` 订、直到日志结束零释放）。

### 51.4 "主人"这一概念的四条结构性裂缝（地鼠家族分类）
| # | 裂缝 | 机制 | 已核实的条目 |
|---|---|---|---|
| 1 | **key 易变** | owner 取 `v->First()`，而链头会被耦合/解挂/翻转/拖动换掉 ⇒ 释放时找错人、或指向一条已不经过该格的路径 | KI-362、REG-243-K；**§47.23 `led=23`（阻塞位主人=请求方自己）** |
| 2 | **用途混装** | 单一 index 无法区分 **PATH** 位与**车下 CONSIST 保护**位 ⇒ 必须升级为"**按用途计数的**所有权表" | 续二十七的诊断定式（律 1/律 2） |
| 3 | **双真值漂移** | 地图位可被**整格重建**（`MakeRailNormal` 等直写地图字节、不走 setter）与**平台整体清除**绕过账本 ⇒ **账本永久比地图多** | KI-363 |
| 4 | **读档断层** | 账本是 NOSAVE（`pbs.cpp:159`）⇒ 读档后**全部活预留瞬间变"无行"**，而走查又能找到活车 ⇒ 两套判据当场打架 | §48.5 整段论述；(B′) 的由来 |

**第 5 条（本轮新增，也是真正致命的）**：**释放责任没有强制配对**。订位点散在 `yapf-resv / yapf-couple / choose-track / controller / extend-backout / waiter / ReserveTrackUnderConsist` 七类，每一类都应有一处配对释放，但配对关系全靠人写 ⇒ 漏一处 = 一个永久孤儿。KI-369 就是 yapf-couple 漏的那一处。

### 51.5 本轮实测恰好给出"主人"概念的三种死法（判据级证据）
| 死法 | 现场读数 | 含义 |
|---|---|---|
| **有行但无位** | (C) 触发 4 次全 `released=0 kept=8`（§49） | 账本行在，但行里没有该类位 ⇒ 释放**空转** |
| **病位不满足"无行"前提** | (B′) `why=norow` = **0**（§49） | 真正的病位"走查有主"，**永远进不了** (B′) 分支 |
| **有主但主人已走** | **KI-369**：`:7687` 订、`:8807` 判 `owner=veh23`、正文零释放；而 veh23 自己跑到 `1112,125` 刷 `COUPLE-FAIL` | 走查恒"有主" ⇒ 永久 `owned` ⇒ **(B′)/(C) 结构上不可能清它** |

⇒ **三个死法各自否掉了"主人"的一个前提**（行→位、无行→有行、有主→在走）。结论：**靠"单一 owner"这一概念无法覆盖全部失效面**，继续加释放点只会继续养地鼠。

### 51.6 出路（三选，此处只定性，不落码）
- **甲｜回归几何**：放弃账本，释放一律按"当前路径"清；链编辑后的残留交给审计兜底。→ 等于删掉 (C)/(B′)/(C′) 三层，把希望压在"审计能判对"，风险=回到 KI-205 那句注释的处境。
- **乙｜账本升为唯一真值**：**所有**订位/释放点（含 yapf、controller、`ReserveTrackUnderConsist`、乃至引擎内部订位）都必须走账本 API，地图位只作账本投影；读档时按"全体列车路径并集"重建账本。→ 最干净，但改动面最大，且要处理非 R3R 的引擎内部订位。
- **丙｜账本降为辅助（推荐）**：**主判据一律用"位置是否在任何活车当前路径上"**（路径并集），账本只用于记账/加速。孤儿清理判据 = **"无账本行 **且** 不在任何活车路径并集上"** ⇒ 一次性覆盖 §51.5 的三种死法（尤其 KI-369：该格不在任何活车当前路径上）。这正是 §50 末尾建议的 **"账本有行才算有主"** 口径的最小形态。

**推荐：丙。** 理由：它不需要重写订位侧（改动面小），且把"主人"从**判据**降级为**线索**，从根上解开 §51.4-1（key 易变）与 §51.5-3（有主但走了）两处死结；代价是每轮多算一次路径并集（§48.5 已有实现，复用即可）。

**状态**：本轮为架构级定性（零源码改动、零构建），已登记 KI-369 与 REG-243-V。
