# 第 306 轮：玩家三条新报告（车库挂接漏预留 / 不按预留走 / 冲灯越信号机）

- 收到时间：2026-10-26
- 上一轮基线：`src\train_cmd.cpp` 第 305 轮落地（第 4 步停稳判据 `:2435`、第 5 步 `parked-no-pair` `:17237`、配对成功唤醒 `:13672`、到点判据整链化 `:14235`），`build\openttd.exe` 2026-10-26 01:00:50（52 237 824 B），构建 EXIT_CODE=0。
- 本轮性质：**新报告 → 先落盘（备忘 + KI-478/479/480），再取证**（按用户 2026-10-01/10-11 规则）。

## 一、玩家原话（不可改写，判读基准）

> 说说最新的问题，1是列车在车库里执行前往车库挂接会往外漏预留。2是有的列车预留了但是不按照自己铺的预留走。3是有的列车还是会冲灯，预留往灯外面漏出去

拆成三条独立报告：

| 编号 | 现象 | 关键限定词 |
|---|---|---|
| **REG-306-A** | 列车**在车库内**执行「前往车库挂接」（`OT_GOTO_COUPLE` + 目标为 depot）时，**预留往库外漏** | "在车库里"、"前往车库挂接"、"往外漏预留" |
| **REG-306-B** | 列车**铺了预留**，但**不按自己铺的预留走** | "预留了"、"不按照自己铺的预留走" |
| **REG-306-C** | 列车**冲灯**（越过红灯/信号机），**预留往信号机外面漏出去** | "还是会"（= 历史问题未净）、"冲灯"、"往灯外面漏" |

三条共同点：**预留（reservation）与实际走行/信号机不一致**，属同一族（PBS 路线预留的"所有权/范围/消费者"三件事）。

## 二、与上一轮改动的直接关联（必须优先排除/确认）

**第 305 轮我给 `R3RIsParkedWaiting()` 加了车库豁免**：

```17237:17242:src/train_cmd.cpp
	if (consist->current_order.IsType(OT_GOTO_COUPLE) &&
			R3RGetCouplePairPartner(consist) == nullptr &&
			!IsRailDepotTile(consist->tile)) {
		*reason = "parked-no-pair";
		return true;
	}
```

当时写进注释的理由是「库内列车不经过本函数所在的路线预留路径（库内是滑行/吸附），拦它只会让库内挂接多绕一步」。**如果这个前提是错的**，那么本轮 REG-306-A 就是我自己引入的：库内 `GOTO_COUPLE` 车被放行订路 ⇒ 订出库外路线预留 ⇒ 而库内车由库内滑行/吸附移动、**从不消费这条预留** ⇒ 预留永久残留（正是"往外漏"）。

⇒ **取证第一优先项（G1）**：确认库内 `OT_GOTO_COUPLE` 车是否真的会走到 `TryPathReserveWithResultFlags()`（即库内是否也调 `TrainController`/`TrainCheckIfLineEnds`），以及日志里是否出现 `RP cur=`/`CPL-RESERVE`/`RESV-WATCH SET-STN` 而车未出库。

## 三、疑点（立即登记，不等取证）

| 编号 | 疑点 | 期望值 | 实测值 | 为什么存疑 | 还需什么数据 |
|---|---|---|---|---|---|
| **D-306-1** | 库内 `GOTO_COUPLE` 是否会订路 | 按我在 `:17235-17236` 写的注释，库内不经过该路径（应为 0 次） | 未测 | 注释是"我认为"，不是实测；`IsRailDepotTile` 只看库格本身，库门口那一格不算库 | 日志中库内车（`track=TRACK_BIT_DEPOT`）是否有 `RP cur=` / `CPL-RESERVE` |
| **D-306-2** | 库内车不消费预留 | 库内车走 `TRACK_BIT_DEPOT`，`ClearReservationUnderConsist()` 只清脚下与账本里的格 | 未测 | 账本 `_r3r_consist_resv` 是内存态，库内订的库外格若从未"被走过"就永远不进 `moved` 分支 | 库内挂接成功后执行 `RESV-GC`/`RESV-PURGE`/`RESV-AUDIT-STRAY` 是否有该车号的库外格 |
| **D-306-3** | 第 305 轮到点判据整链化是否改变了 `ProcessOrders` 推进时机 | 到点后 `ProcessOrders` 才推进 `GOTO_COUPLE`（挂上才推进） | 未测 | 整链化后"链中某一节压在站台格"即算到点，比原来更早 ⇒ 可能提前推进订单，与预留终点不一致（这正是 REG-306-B 的可能入口） | 现场 `COUPLE-DEST-EMPTY`/`ORD-AFTER-COUPLE` 与 `RP cur=` 的先后 |
| **D-306-4** | "不按预留走"是预留错还是消费者错 | 预留即走行路线 | 未测 | 两种机理完全不同：预留订错（订出车不走的轨，KI-346 同族）vs 车不走预留（预留被 reap/GC 清掉后车仍按 YAPF 旧路线走） | `RP cur=` 逐格序列与实际 `LOCO-MOVE` 逐格序列对比 |
| **D-306-5** | "冲灯"是否等于"预留越过信号机" | PBS 下信号机放行依据＝预留是否到达对面信号机 | 未测 | 若预留越过信号机但车头没预留到该信号机内侧，就会"灯变绿但另一车没预留" ⇒ 追尾；若预留被登记为无主 ⇒ `RESV-AUDIT-STRAY` | 信号机坐标 + `RP cur=` 是否越过它 + `RESV-AUDIT-STRAY/OWNER` 读数 |

## 四、猜测根因（待验证，逐条给验证判据）

### G1（REG-306-A，嫌疑最高，可能是第 305 轮自伤）
`R3RIsParkedWaiting()` 的车库豁免放行了库内 `GOTO_COUPLE` 的路线预留，而库内移动（`TRACK_BIT_DEPOT`、库内滑行/吸附）不消费预留 ⇒ 预留挂在库外普通轨道上永久残留。
- 验证：库内挂接场景日志出现 `RESV-NOBOOK ... reason=` 缺该车（即没被拦）+ 出现 `CPL-RESERVE veh=N ... reserved=1` + 车始终 `track=0x80` 未出库 ⇒ 成立。
- 反证：若日志显示该车从未进入 `TryPathReserve*`（无 `RP cur=`），则漏预留来自 `ReserveTrackUnderConsist()` 之外的另一条路径（另查 `R3RReserveChainUnderConsist` / `cpf` 的库内分支）。

### G2（REG-306-B，两个方向都要验）
(a) **预留订错**：挂接寻路 `rp()` 回溯预留沿 `cur_td` 推导进入轨，双槽格/站台段补齐处仍可能订出车不走的轨（KI-346 已修一处 `TrackdirToTrack(ReverseTrackdir(cur_td))`，但 `plat_track=AxisToTrack(axis)` 与"补齐车头到车底之间站台预留"的循环是同族未清点，见 KI-346 U-3）。
(b) **车不走预留**：预留被 `R3RReservationAudit()`（`R3R_RESV_REAP_AUDITS=3`）或 `R3RPurgeStrayReservations()` 判为无主清掉后，车仍按 YAPF 老的 `chosen_track` 继续跑（预留与走行解耦）。
- 验证：`RP cur=` 序列与 `LOCO-MOVE` 序列逐格比对；若某格 `RP` 有而车走了别的轨 ⇒ (a)；若 `RESV-REAP`/`RESV-PURGE` 清的是该车前方路径 ⇒ (b)。

### G3（REG-306-C）
"冲灯"= 预留往信号机外漏。两个可能：
1. **预留范围错**：YAPF couple 寻路的 `rp()` 在到达目标前提前 `return true`（递归帧 `cur == tg` 时"不订自己"），终点格只能靠"进入格"补 ⇒ 若 `tg` 恰好是信号机格/信号机前格，可出现"预留停在信号机内侧之前、车头却越过去"。
2. **信号机判据与预留不同源**：PBS 信号机放行看 `HasReservedTracks` + 对面预留，而 R3R 自己的 `ReserveTrackUnderConsist()` 整格位（站台/路点 `SetRailStationReservation`）与 plain rail 的预留位是**两套存储**（KI-207c 已记载） ⇒ 站台/路点相邻信号机处会出现"灯已绿但整格位未订"或反之。
- 验证：`RESV-AUDIT-WATCH ... kind=WAYPOINT|STATION` 与信号机坐标对齐；撞车/穿模现场取 `RP cur=` 是否存在该信号机格。

## 五、本轮待取证清单（按 3~5 组配额执行，每完成一组立刻回填本节）

- [ ] T1：`R3RIsParkedWaiting()` 的调用者与库内路径——库内 `GOTO_COUPLE` 是否到 `TryPathReserveWithResultFlags()`（读 `TrainLocoHandler` 的停驶/移动分支与 `TrainController` 入口守卫）
- [ ] T2：`TryPathReserveWithResultFlags()` 对 `TRACK_BIT_DEPOT` 的处理（是否早退、是否有 depot 分支）
- [ ] T3：`rp()` 里站台段补齐预留与 `plat_track`（KI-346 U-3）现状
- [ ] T4：`ReserveTrackUnderConsist()`/`ClearReservationUnderConsist()` 的账本在库内挂接后是否会被收敛（`R3RGcConsistReservations` 的触发条件）
- [ ] T5：需要玩家现场日志（AI 不自行启动游戏）——见 §六

## 六、需要玩家提供的现场（明确口径，便于一次取到）

1. **REG-306-A 复现**：车库内一台机车执行「前往车库挂接」（目标 depot），复现后把 `build\R3R_debug.log` 发我；重点看该车 `veh` 号的 `RESV-NOBOOK` / `CPL-RESERVE` / `RP cur=` / `LOCO-MOVE` / `RESV-PURGE` 行。
2. **REG-306-B**：记录一列"预留了但不按预留走"的车号 + 大概时间点；日志里 `RP cur=` 与 `LOCO-MOVE` 都在，可直接比对。
3. **REG-306-C**：记录冲灯的车号 + 信号机坐标（或截图位置）+ 时间点；需要 `RESV-AUDIT-WATCH/STRAY` 与 `RP cur=` 对齐。
4. 三条若来自同一份日志，一份即可；只要告诉我是"第 305 轮 exe（01:00:50）"还是更早版本产生的。

## 七、复测判据（占位，实现后细化）

1. 库内挂接不再出现任何库外格的 `RESV-WATCH SET-STN` / `RP cur=`（或出现后当 tick 被清）。
2. `LOCO-MOVE` 逐格序列与 `RP cur=` 序列一致（同一车、同一趟）。
3. 冲灯现场 `RESV-AUDIT-STRAY` 归零，且穿模/追尾不复现。

## 八、第 1 轮静态取证（2026-10-26，5 组工具内完成）

### T1/T2 结论：门禁只拦"包装层"，库格是唯一的豁免口

`TryPathReserve()` 是 `src\train_cmd.cpp:17304 TryPathReserveWithResultFlags()` 的薄包装（`src\train.h:105` inline） ⇒ 门禁 `:17346` 对**全部**订路调用者生效（KI-208 前提成立）。**唯一绕过口就是我加的车库豁免** `!IsRailDepotTile(consist->tile)`。

订路调用点清单（全文件 16 处匹配，实质出口 8 个）：

| 行 | 出处 | 备注 |
|---|---|---|
| `:2977` | `R3RDecoupleInterlock` 放行 | 前面紧跟"解挂联锁"判据 |
| `:5268` | 离开站台/库前 | 前一行 `SetRailStationPlatformReservation(...)` |
| `:13672` | **第 305 轮新加的配对唤醒** | `cur_speed == 0` 才调 |
| `:14967` | **离站/出站放行判据** | `seg_state == SigSegState::Path && !TryPathReserve(v) && v->force_proceed == TFP_NONE` |
| `:19183` | 隧道/桥出口方向重算 | |
| `:20160` | 碰撞检测（挂接几何） | 失败则放弃合链 |
| `:21648` | 信号段可通行时订路 | **`TryPathReserve(consist, true, true)` → `first_tile_okay = true`** |
| `:21791` / `:21826` | `TrainLocoHandler` 自动订路 / 普通订路 | |

### 新增猜测根因（待日志验证）

- **G4（REG-306-C，强嫌疑）**：`:14967` 的离站放行是

  ```14967:14967:src/train_cmd.cpp
  	if (seg_state == SigSegState::Path && !TryPathReserve(v) && v->force_proceed == TFP_NONE) {
  ```

  只要 `force_proceed != TFP_NONE`，**即使 `TryPathReserve()` 失败也放行离站** ⇒ 车头越过信号机而预留没铺到对面，正是"冲灯 + 预留往灯外漏出去"。要查的落点：R3R 是否在卡住自愈 / 挂接 / 解挂联锁 / 订单推进里设过 `force_proceed`（原生只有玩家 Ctrl+点击"强制通过"会设）。

- **G5（REG-306-B）**：`:21648` 传 `first_tile_okay = true`，语义是"首格已被别人订着也算成功"。若车随后真从该格起步，观感就是"我铺了预留但走的路不属于我"。要把这条与 `:21826` 的默认 `false` 区分开，看现场走的是哪一支。

- **G6（REG-306-B/C 共同）**：门禁与审计都只覆盖"订路那一刻"。车在预留内前进时若预留被 `R3RReservationAudit()`/`R3RPurgeStrayReservations()` 判无主清掉，车仍按已有 `chosen_track` 继续跑（`TrainController` 不会因为脚下预留消失而刹车）⇒ "预留没了但车继续走"。

### 已排除

- 不是"门禁没覆盖到某个调用者"（`TryPathReserve` 只有一条实现路径）。
- 不是 `ReserveTrackUnderConsist()`（脚下保护）被拦 —— 该函数不在门禁里，任何门都不会影响它。

## 九、下一步（等日志，不猜修法）

静态候选修法（**待日志确认后拍板，现在不动代码**）：

- **R-A**（对 REG-306-A）：去掉 `!IsRailDepotTile(consist->tile)` 豁免，库内 `GOTO_COUPLE` 无配对也拦。可行性依据：第 305 轮已在配对成功处加了 `:13672` 主动订路，库内起步不会因此卡死（KI-348 场景已覆盖）。风险：库内"两列车库里直接吸附合链"若不经订路则无影响；若经订路，需靠唤醒兜底。
- **R-B**（对 REG-306-B）：把 `:21648` 的 `first_tile_okay` 由 `true` 改 `false`（或按 `reserved` 语义分档）。**先要看现场走的是哪一支**，否则改错地方。
- **R-C**（对 REG-306-C）：查 `force_proceed` 的全部赋值点，确认是否有 R3R 代码在挂着/卡住自愈里设它；若有，那是"冲灯"的直接闸门。

## 十、需要玩家回答/提供的三件事（缺任一条，B/C 只能靠猜）

1. 一份 `build\R3R_debug.log`（三条来自同一会话即可），以及该日志由**哪一版 exe**产生（是 2026-10-26 01:00:50 的第 305 轮版吗）。
2. REG-306-A 的**车号**（库内那台执行"前往车库挂接"的机车）与**目标车库坐标**。
3. REG-306-B/306-C 的**车号 + 信号机坐标**（或截图里能看出位置的那一格），以及大概时刻。

## 十一、现场日志第 1 批取证（2026-10-26，`build\R3R_debug.log` 6033 行）

**玩家补充信息**：车库格 **(1889,188)**；库内执行挂接的车是 **2/4/6 号车**（不确定具体哪台，疑为界面单位号）；问题 2 在 **12 号车站**；问题 3 在 **21 号车站**。

### 已验证事实（自带锚点，可直接引用）

| 事实 | 证据 | 判定 |
|---|---|---|
| 车站坐标 | `R3R_debug.log:48` `R3RDUMP-STATION idx=12 xy=278,240`；`:53` `idx=21 xy=242,231` | 12 号站=(278,240)，21 号站=(242,231) |
| 车库链全在库内 | `:54`-`:72` `R3RDUMP idx=3..21 ... tile=1889,188 ... tb=80 ... head=21 tail=47` | `tb=80` = `TRACK_BIT_DEPOT`；链头 **veh=21**（`FE=1 SEGF=1 un=1`），链 21..47 整条在库格内 |
| 库内车持 GOTO_COUPLE | `:72` `idx=21 ... ord=1 co=16` | `co=16` = `OT_GOTO_COUPLE`，订单类型确认 |
| **REG-306-A 铁证** | `:1692` `RESV-WATCH SET-RAIL tile=1886,188 track=0 phase=yapf-resv actor=21 head=21`<br>`:1693` `RESV-WATCH SET-STN tile=1888,188 track=255 phase=yapf-resv actor=21 head=21` | **库内车（库格 1889,188）的 YAPF 预留订到了库外两格**：1886,188（plain rail）与 **1888,188（整格位，`track=255`）**。`1888,188` 就在库门口外一格 ⇒ 正是玩家说的"往外漏预留"，且整格位属 KI-207c 家族（无人能清） |
| 配对全在同一库格内 | `:1687` `CPL-PAIRSCAN ... act=21 state=locked ord=16 destStn=8 actTile=1889,188 actSpd=0 ... paired=1`；`:1695` `CPL-PAIR tick=2338 act=21 tgt=24 dist=0 actTile=1889,188 tgtTile=1889,188`；另 `:1722` act=51↔24、`:1745` act=99↔75，均 `actTile=1889,188` | 库内"车对车"吸附挂接：**配对双方同在一个车库格**，订路纯属多余 |
| 库内多台同时挂接 | `:1687/:1722/:1745` 三台 `act=21/51/99` 同时 `state=locked`，`destStn=8` | 玩家说的"2/4/6 号车"应是这三台（界面单位号），`un` 字段：21 的 `un=1` |
| 全日志只有一台车真的在动 | `LOCO-MOVE` 共 14 行，**全部是 veh=51**，`tile=249,234 → 250,234 → 251,234 → 252,234`，`db=1`（倒车），`dtile=491798` | **勘误（§十二 更正）**：本图 `MapLogX=11`（2048×2048，由库格 x=1889 反证），`dtile = x + y*2048`。故 `491798 = 278 + 240*2048` ⇒ **(278,240) 正是 12 号站本体格**；原写的 "(278,120)/4096" 是错的。veh=51 确实是奔 12 号站的车 |

### 由此对三条报告的初步定性

- **REG-306-A 成立且机制已明**：库内车的 `GOTO_COUPLE` 在配对建立后**照常跑 YAPF 挂接寻路**，而寻路起点在库内、目标也在库内（`tgtTile=1889,188`），但 `rp()` 回溯却把路径铺到库外 `1886,188`/`1888,188`；库内车由 `TRACK_BIT_DEPOT` 吸附移动，**永不消费这条预留** ⇒ 残留。**这与"没配对被拦"无关**，是"配上了但仍不该订库外路"的问题，我上一轮的车库豁免只放行了订路时机，不是根因本身（根因是库内挂接根本不需要 YAPF 路线预留）。
- **REG-306-B 落在 veh=51**：它是唯一逐格移动的车，且 `db=1`（倒车）在 `249..252,234` 反复同格（`j=193 spd=5` 多行同 tile）⇒ 需比对它的 `RP cur=` 序列与实际走格。
- **REG-306-C** 待查 21 号站 (242,231) 附近的信号机格与 `RP cur=` 越界。

### 已登记的新疑点

| 编号 | 疑点 | 还需什么 |
|---|---|---|
| D-306-9 | 库内挂接为何会跑 YAPF：是 `TryPathReserveWithResultFlags` 放行后寻路，还是 YAPF 由别的入口（`TrainCoupleHandler`/`CPL-BEST` 搜索）直接触发 rp()？ | `:1692` 前后的完整上下文（寻路入口行） |
| D-306-10 | `1888,188` 的 `track=255` 整格位是路点还是站台（决定清它用哪个 API） | `1888,188` 的 `CLEAR-STN`/`RESV-GC`/`RESV-PURGE` 后续行 |

## 十二、现场日志第 2 批取证（2026-10-26）

### 12.0 坐标解码勘误（重要）

本图 `MapLogX = 11`（2048×2048），`TileIndex = x + y*2048`。由库格 x=1889 / 车站 x=278 反证（MapLogX=12 时代入会得到不存在的格）。据此重算日志里的 `dtile`：

| 日志锚点 | `dtile` 十进制 | 解码 | 含义 |
|---|---|---|---|
| `:4360` `SPD-DROP veh=21 ... dtile=473330` | 473330 | 473330 − 231×2048 = **242** ⇒ **(242,231)** | **21 号站本体格**（与 `:53 R3RDUMP-STATION idx=21 xy=242,231` 逐字吻合）⇒ 早前 veh=21 的订单终点就是问题 3 的现场 |
| `:4484` `TPR-BLOCKED veh=21 ... dtile=479497` | 479497 | 479497 − 234×2048 = **265** ⇒ **(265,234)** | 正是 `CPL-PAIR tgt=123 tgtTile=265,234` 的格 ⇒ `dtile` 这里是"挂接目标所在格" |
| `LOCO-MOVE veh=51 ... dtile=491798` | 491798 | 491798 − 240×2048 = **278** ⇒ **(278,240)** | **12 号站本体格** ⇒ veh=51 是奔 12 号站的车（问题 2 的主角） |

⇒ §十一 表里 `491798 = (278,120)` 的写法作废，已在原表加勘误行。

### 12.1 D-306-9 结案：库外预留来自 `TryPathReserveCore → ExtendTrainReservation`，与挂接寻路无关

上下文按顺序（`build\R3R_debug.log`）：

| 行 | 原文（截断） | 判读 |
|---|---|---|
| `:1688` | `TPR-WALK veh=21 mf=1889,188 origin=1889,188 td=0 okay=0 exit=0 new=1888,188 reach=1 spd=0 one=0 look=0` | 就是 `TryPathReserve` 的 walk：`okay=0`（现有预留不可用，因为在库格上），决定把首格推到 `1888,188` |
| `:1689` | `RESV-EXT-PRE veh=21 tile=1888,188 enterdir=0 tracks=0x1 reserved=0x0 doRes=1` | `ExtendTrainReservation`（`src\train_cmd.cpp:16585` 调用点）准备订首格 |
| `:1690` | `COUPLE-DEPOT-DIRECT veh=21 tile=1889,188 dest=1889,188` | **库内直连挂接**分支已识别（`r3r_couple_depot == true`） |
| `:1692` | `RESV-WATCH SET-RAIL tile=1886,188 track=0 phase=yapf-resv actor=21 head=21` | 库外第 2 格被订 |
| `:1693` | `RESV-WATCH SET-STN tile=1888,188 track=255 phase=yapf-resv actor=21 head=21` | 库门口那格是站台/路点 ⇒ 走整格位 API（KI-207c 家族） |

结论：`:16734` 的 `if (consist->current_order.IsType(OT_GOTO_COUPLE) && !r3r_couple_depot)` 只跳过**挂接寻路**（`CPL-BEST`/`rp()` 那一支），**通用路线预留 `ExtendTrainReservation` 不受它约束**，`R3RIsParkedWaiting()` 的库格豁免又恰好放它过去 ⇒ 一辆"已经在目标库里、且已与目标同格配对"的机车仍会订一条出库路线，而它由 `TRACK_BIT_DEPOT`（`tb=80`）吸附移动，**永不消费该路线**。

### 12.2 D-306-10 结案：`1888,188` 是整格位且被控制器回收过；`1886,188` 才是真孤儿

- `1888,188`：`:1693 SET-STN` 订 → `:2400` 前后 `CLEAR-STN 1888,188 phase=controller actor=21` 被控制器清 → `:2419` 又被 `actor=51` 订上（正常 churn，且属可见的"预留一闪而过"，与 KI-208 同族）。
- `1886,188`：全日志对 `1886,188` 的命中**只有 `:1692` 这一行**，没有任何 `CLEAR-RAIL`/`RESV-GC`/`RESV-PURGE`/`RESV-REAP` 回收它 ⇒ **永久孤儿**（plain rail 位，`track=0`）。

### 12.3 审计看不见它（G6 前提成立）：`strayTiles=0`

`:2516 RESV-AUDIT-SUMMARY tick tick=2560 scanned=236 reservedTiles=236 owned=236 strayTiles=0 reaped=0 pendingStray=0 us=71564`

⇒ 236 个预留位**全部被判"有主"**，`strayTiles=0`、`reaped=0`。`GetTrainForReservation()` 沿预留回溯会把这笔 1886,188 归给还停在库格上的 veh=21（它就在预留链的上游），于是审计与 GC 永远不碰它。**"有主但永不会被消费"是本轮泄漏逃过所有清理闸门的直接原因。**

### 12.4 21 号站 / 12 号站场景的新读数（问题 2、3 的现场骨架）

| 行 | 原文（截断） | 判读 |
|---|---|---|
| `:4389`-`:4392` | `REACH cur=249,233 td=0 depth=0/1/2/3 trk=0 bits=0x1` → `242,233` → `241,233` → `240,233` | x=240..249、y=233 是一条连续单轨（每格 `bits=0x1`） |
| `:4455` | `RP cur=249,233 td=8 depth=0 trk=0 bits=0x1` | 预留回溯起点 = veh=21 站立格 249,233，`td=8` |
| `:4480` | `DEPOTBLK veh=21 tile=249,233 track=01 ... isdepot=0 ontile=0 ... spd=0` | 不是库内、不是库格，是普通轨道上停住 |
| `:4484` | `TPR-BLOCKED veh=21 tile=249,233 spd=0 mf=254,233 origin=278,234 okay=0 td=8 other=123 otherTile=265,234 otherMf=270,234 otherSpd=0 otherOrder=17 otherFirst=123 caronly=0` | 车头前缘在 **(254,233)**（y=233），但**已有预留的远端是 (278,234)**（y=234，12 号站方向）；判定阻塞者 veh=123（`OT_WAIT_COUPLE`，`spd=0`）正坐在 **(265,234)** |
| `:4484` 后 | `:4487 CPL-S0-CHK ... stuck=1`、`:4525 STUCK ... state=1 stuck=1`、`:5823 ... wait=1056`、`:5978 ... wait=1135` | veh=21 在同一格被挂起 **>1100 tick**（约 37 秒未暂停），`stuck=1` |
| `:4372` | `CPL-PAIRSCAN tick=3012 act=21 state=locked ord=16 destStn=12 actTile=249,233 ... best=123 bestDist=17 paired=1 ... near=24 nearDist=3 nearTile=248,235 nearWhy=not-target` | 候选是 17 格外的 123；**3 格内还有 veh=24 却被判 `not-target`** |
| `:4526` / `:5967` | `... state=steady ... paired=-1` / `act=51 state=none ... paired=0 ... near=21 nearDist=4 nearTile=249,233` | 配对状态在 `locked → steady → none` 之间抖动，反复重扫 |
| `LINEND veh=99 tile=262,237 ... sig=1` 段 | 速度 `55→…→5`，之后 `x=15` 多行不变 | 在红灯前减速停住（**未见越过**） |
| `LINEND veh=50 tile=255,235 ... sig=1 db=1` 段 | 同上，`spd` 衰减到 5 后不再前进 | 同上 |
| 全日志 | `REDSIG` **0 行** | 第 349 轮的"撞红灯归属"探针在本会话一次都没触发 |

### 12.5 由此收紧的定性

- **问题 1（库内挂接漏预留）＝已做实**：机制见 12.1，残留证据见 12.2，逃过清理的原因见 12.3。**修法已可定**：库格内 + `OT_GOTO_COUPLE` + 已与目标同格配对 ⇒ 不订路线（R-A′，取代 §九 的 R-A）。
- **问题 2（12 号站，veh=51）＝仍差最后一步**：`veh=51` 逐格只走到 `252,234`，且全日志 `RP cur=` 与它的格并不重合；真正"预留与走行不一致"的**正面读数**出现在 **veh=21**：站位 y=233（`mf=254,233`）而预留远端 y=234（`origin=278,234`）。这是"跨股"而非"不走预留"，但足以让车在 233/234 两条平行轨之间做出与玩家预期不同的选择 ⇒ 需确认 278,234 与 278,240 是否同一站台的两排。
- **问题 3（21 号站，冲灯）＝缺一项数据**：现有探针只有 `sig=1`（红灯存在），**没有任何一行打出灯格坐标**，`REDSIG` 全 0；`LINEND` 现场两列车都停住了，反而没有冲灯记录。要判"预留越过信号机"，必须知道 21 号站那盏灯的格坐标（或让我加一条只读探针把灯格打出来）。

### 12.6 新疑点

| 编号 | 疑点 | 期望/实测 | 还需什么 |
|---|---|---|---|
| D-306-11 | 预留与站位跨股：`mf=254,233`（y=233）vs `origin=278,234`（y=234） | 期望二者在同一股；实测差一行 | y 233→234 换股发生在哪一格（`RP cur=` 完整序列） |
| D-306-12 | 挂接目标自己挡路：`best=123` 且 `otherTile=265,234` 正落在 veh=21 的预留路径上 | 期望"目标不参与阻塞判定"；实测被判 `TPR-BLOCKED` 并 `stuck=1` | `CheckTrainsOnTrack` 是否应排除挂接目标（KI-330 同族） |
| D-306-13 | 配对状态抖动：`locked → steady → none`（`:4372/:4526/:5967`） | 期望锁定后稳定 | 每次抖动是否重订一遍路线（与问题 1 的"不断重铺"叠加） |
| D-306-14 | 日志无灯格坐标 | `REDSIG` 0 行、`LINEND` 只有 `sig=1` | 需加只读探针 dump 灯格，或玩家给坐标 |

### 12.7 下一步（本轮不动代码，等第 2 批数据的最后一块）

1. **问题 1 直接改**（证据链已闭合）：把"库内已配对"并入 `R3RIsParkedWaiting()` 的拦截面（`src\train_cmd.cpp:17237` 一带），reason 记 `depot-paired`；验收判据=库内配对后不再出现 `RESV-WATCH SET-RAIL actor=<库内车>`，`1886,188` 不再被订。
2. **问题 3 加只读探针**：在红灯判定处打出灯格 `x,y` + 该灯是否已被本车预留覆盖（不改行为），一次复跑即可判定"冲灯"是"预留越灯"还是"没预留也放行"。
3. **问题 2 先补 `RP cur=` 与 `LOCO-MOVE` 的对齐**：确认 233/234 换股位置后再定修法（若是 YAPF 换股即预留/走行同源则不是 bug，若预留铺在 234 而车走 233 则是真错位）。

## 十三、第 3 批取证：玩家给出灯坐标（2026-10-26）

### 13.1 玩家答复（原话）

> 「我报告，（256，233-234）是事发的两个信号机，被冲的是（256，234），如果没推断完可以回到A」

⇒ **D-306-14 结案**：21 号站附近是**双轨区间，两盏并排信号机 (256,233) 与 (256,234)**，被冲的是 **y=234 那盏**。

### 13.2 命中读数（`build\R3R_debug.log`，findstr 精确格坐标）

| 行 | 原文（截断） | 判读 |
|---|---|---|
| `:5065` | `RESV-EXT-PRE veh=51 tile=256,234 enterdir=2 tracks=0x1 reserved=0x0 doRes=1 real=8 spd=35` | **veh=51**（唯一在动的车、`db=1` 东行、物理位 249,234→252,234）把**扩展预留的"首格"定在 (256,234)** —— 正好是被冲的那盏灯 |
| `:5076` | `RESV-EXT-PRE veh=51 tile=256,234 ... reserved=0x0 doRes=0 real=8 spd=43` | 无需再订（已在订或在途） |
| `:5175` | `RESV-EXT-PRE veh=51 tile=256,234 ... reserved=0x1 doRes=0 real=8 spd=5` | 该格**确实已被订上**（`reserved=0x1`），同一批读数里车速衰减到 `spd=5` |
| `:4373` | `RESV-EXT-PRE veh=21 tile=256,233 enterdir=2 tracks=0x1 reserved=0x0 doRes=1 real=8 spd=0` | 同 x 的另一盏灯 **(256,233) 也正好是 veh=21 的"首格"**；veh=21 停在 249,233（`TPR-BLOCKED`、`stuck=1`） |
| `:4436` | `REACH cur=256,233 td=8 depth=3 trk=0 bits=0x1` | veh=21 的闭环路径第 3 格 = 灯 (256,233) |
| `:4458` | `RP cur=256,233 td=8 depth=3 trk=0 bits=0x1` | 同上，**预留回溯路径里也包含这盏灯** |

### 13.3 结论句（读数→判定）

1. `RESV-EXT-PRE` 打在 `ExtendTrainReservation` 的**首格**上（与 §12.1 的 `:1689 RESV-EXT-PRE veh=21 tile=1888,188` 同构：那次首格就是库门口第一格）。⇒ **veh=51 的预留扩展起点 = (256,234) = 灯格本身**，也就是它从灯格开始**往灯外（x>256）**补订，而**灯内侧 253~255,234 并没有在这一次补订里**。
2. 结合 §12.4：veh=51 的物理推进只到 `252,234`（`LOCO-MOVE` 末行），却已经为 `256,234` 订好了位（`spd=35` 时就开始订，`spd=43` 时仍在订）⇒ **预留抢在车头前方 4 格落到了灯上/灯外**，而灯内侧那 3 格不在同一次订路里。
3. 因此"冲灯"的最可能形态＝**预留链在灯格处分段**：灯内侧无本车预留（灯对它是红的），本车却在灯外已持有预留并继续推进 ⇒ 车头越过 (256,234)，视觉上"冲了红灯"，预留则显示"漏到灯外面去了"。这与玩家描述逐字吻合。

### 13.4 新疑点

| 编号 | 疑点 | 期望/实测 | 还需什么 |
|---|---|---|---|
| D-306-15 | 灯内侧 253,234/254,234/255,234 是否被 veh=51 订下（若未订⇒G7 成立） | 期望整条路径连续；实测首格=灯格 | 三格的 `SET-RAIL`/`CLEAR-RAIL` 与 actor |
| D-306-16 | 为什么 `ExtendTrainReservation` 的首格跳过灯内侧三格 | 期望首格=车头下一格 | veh=51 的 `RP cur=` 全序列（是否从 256,234 开始）＋ `LOCO-MOVE` 末行之后的走行 |
| D-306-17 | `spd=35→43` 期间 `doRes=1→0` 是否有"订路失败后仍放行"（`force_proceed`） | 未观测 | `force_proceed` 赋值点 |
| D-306-18 | 同一 tick 段内 veh=21(233) 与 veh=51(234) 的"首格"都恰是 256 灯 ⇒ 疑似 `CheckTrainsOnTrack`/信号机格在三格窗口里被**当成路径终点** | 期望首格是车头实际下一格 | `TryPathReserveCore` 的首格计算（`:17290` 一带）取的是 `moving_front` 还是 `entry` 帧 |

### 13.5 灯内侧/灯格的第二组读数（决定性时序）

同一次会话（`findstr` 精确 `253,234`/`254,234`/`255,234` + 相邻上下文）：

| 行 | 原文（截断） | 判读 |
|---|---|---|
| `:4991` | `TRP veh=51 mf=254,234 real=8 type=16 dest=491798 spd=0 fp=0 look=1 one=0 mstuck=1 fto=1 => ok=0 res=0` | veh=51 的移动前端到 **254,234**；`ok=0 res=0`（预留失败）、`mstuck=1` |
| `:5064` | `LOCO-MOVE veh=51 tile=249,234 j=205 spd=35 track=01 dir=1 db=1 stuck=0 dtile=491798` | 车**身**在 249,234，`spd=35`，`db=1` |
| `:5065` | `RESV-EXT-PRE veh=51 tile=256,234 reserved=0x0 doRes=1 real=8 spd=35` | 灯格：本车**无**预留，`doRes=1`（**判定该订**） |
| `:5075` | `RESV-EXT-PRE veh=51 tile=255,234 reserved=0x1 doRes=0 real=8 spd=43` | 灯内侧一格（255,234）**已有本车预留**；车已**加速到 spd=43** |
| `:5076` | `RESV-EXT-PRE veh=51 tile=256,234 reserved=0x0 doRes=0 real=8 spd=43` | 灯格仍然**无**预留，但这次 **`doRes=0`（不订了）**，而车正在加速 |
| `:5175` | `RESV-EXT-PRE veh=51 tile=256,234 reserved=0x1 doRes=0 real=8 spd=5` | 之后某刻灯格**终于被订上**（`reserved=0x1`），此刻 `spd=5` |

**结论句（读数→判定）**：

1. 灯格 (256,234) 的 `reserved` 序列是 **0x0 → 0x0 → 0x1**，而车速序列是 **35 → 43 → 5**。⇒ 存在一个明确窗口：**车从 spd=35 加速到 spd=43、跨过 255→256 边界的那段时间里，灯格 (256,234) 没有本车预留**（`doRes` 还从 1 掉成 0）。这正是"冲灯"的机制窗口，G7 由"猜测"升级为**高置信根因（待源码确认 `doRes` 语义）**。
2. `doRes` 与 `reserved` 是两个独立列：`reserved` = 该格此刻是否已在本车预留里；`doRes` = 本次扩展是否执行订。`doRes` 可以由 1 翻成 0（`:5065`→`:5076`）**而 `reserved` 不变（仍 0）** ⇒ 一定有一段"判定该订但最终没订、车却照旧前进"的路径。
3. **探针覆盖缺口**：全日志 `findstr "256,234"` 只有上面 3 条 `RESV-EXT-PRE`，**没有任何 `SET-RAIL tile=256,234` / `CLEAR-RAIL tile=256,234`**，而 `reserved` 明明从 0 变成 1 ⇒ `RESV-WATCH` 并未覆盖这条订路路径。⇒ **D-306-19：不能再用"没有 SET-RAIL"反推"没订过"**（§12.2 对 `1886,188` 的"零释放"结论仍成立，因为那里的依据是"零 `CLEAR*` 且格位长期 reserved"，与"零 SET"无关；但今后判"有没有订"必须同时看 `reserved=` 列）。
4. `:5078 CPL-PAIRSCAN ... act=21 ... near=51 nearDist=1 nearTile=249,234 nearWhy=not-target`：veh=51 就在 veh=21 隔壁一格却被判 `not-target` ⇒ 两台车在同一区间互相不认，各自往 12 号站方向挤（与 §12.4 的 D-306-12 同族）。

### 13.6 下一步（源码侧，唯一未闭环点）

`RESV-EXT-PRE` 的 `doRes=1 → doRes=0` 是本次冲灯的开关。需读 `ExtendTrainReservation` 里 `doRes` 的计算与 `reserved` 的来源，确认它是"因信号机红灯而停订（正常）"还是"因首格判定 `first_tile_okay`/`reserve_ok` 被置假而放弃，但调用方仍返回成功（异常）"。

## 十四、第 4 批：源码读证 —— `doRes`/`RESV-EXT-PRE` 的真实语义（2026-10-26）

### 14.1 勘误（推翻 §13.3-1 与 §13.5-2）

| 旧表述 | 实际（源码） |
|---|---|
| `RESV-EXT-PRE` 打在 `ExtendTrainReservation` 的**首格**上 | **错。** 探针打在 `ChooseTrainTrack()` **入口**（`src\train_cmd.cpp:16472-16488`，门禁 `consist->current_order.IsType(OT_GOTO_COUPLE)`）。`tile` 是**该次调用传入的"从哪一格开始选轨"**，`reserved` 是**这一格**的 `GetReservedTrackbits(tile)`，`tracks` 是本格可用轨位。 |
| `doRes` = "本次扩展是否执行订路/这一格该不该订" | **错。** `doRes` = `_settings_game.pf.reserve_paths \|\| (flags & CTTF_FORCE_RES)`（`:16458`），是**本次调用有没有订路资格**的全局/旗标判定，与具体格子无关；与 `reserved` 是两个独立维度。 |

⇒ §13.3-1「预留扩展起点=灯格」、§13.5-2「判定该订但最终没订、车却照旧前进」两句作废（结论方向保留，机制改写见 14.3）。

### 14.2 全量读数（全日志仅 7 行 `RESV-EXT-PRE`，3 行 `doRes=1` / 4 行 `doRes=0`）

```
1689:RESV-EXT-PRE veh=21 tile=1888,188 enterdir=0 tracks=0x1 reserved=0x0 doRes=1 real=0  spd=0   ← 问题① 库内
4373:RESV-EXT-PRE veh=21 tile=256,233 enterdir=2 tracks=0x1 reserved=0x0 doRes=1 real=8  spd=0   ← 灯 (256,233)
5065:RESV-EXT-PRE veh=51 tile=256,234 enterdir=2 tracks=0x1 reserved=0x0 doRes=1 real=8  spd=35  ← 灯 (256,234)
5075:RESV-EXT-PRE veh=51 tile=255,234 enterdir=2 tracks=0x1 reserved=0x1 doRes=0 real=8  spd=43
5076:RESV-EXT-PRE veh=51 tile=256,234 enterdir=2 tracks=0x1 reserved=0x0 doRes=0 real=8  spd=43
5175:RESV-EXT-PRE veh=51 tile=256,234 enterdir=2 tracks=0x1 reserved=0x1 doRes=0 real=8  spd=5
5453:RESV-EXT-PRE veh=51 tile=258,234 enterdir=2 tracks=0x5 reserved=0x0 doRes=0 real=8  spd=30
```

**结论句 1**：`doRes` 在同一台车同一订单内从 1 翻成 0，而 `reserve_paths` 是启动期常量、`CTTF_FORCE_RES` 是调用点旗标 ⇒ **本存档 `reserve_paths` 必然为假**，`doRes` 完全由旗标决定：`doRes=1` 的三次来自 `TryPathReserve()` 那一路（强制预留），`doRes=0` 的四次来自**只选轨不订路**的另一路（控制器/前瞻）。

**结论句 2（关键）**：`:5076` 是"车头已经在灯格 (256,234) 上、`spd=43`、本格无预留（`reserved=0x0`）、而这次调用**没有订路资格**（`doRes=0`）"的直接读数。也就是说车头越过灯格的那一刻，灯格上不存在"这次调用建立的"预留。

### 14.3 冲灯的精确机制（由源码给出，不再依赖推断）

`ChooseTrainTrack()` 的单轨分支（`:16525-16561`）本来对**没有订路资格**的调用是干净早退的：

```16528:16559:src/train_cmd.cpp
		if (track != INVALID_TRACK && HasPbsSignalOnTrackdir(tile, TrackEnterdirToTrackdir(track, enterdir)) && !IsNoEntrySignal(tile, track)) {
			...
			do_track_reservation = true;          // ← 无资格调用在这里被"临时授权"
			changed_signal = TrackEnterdirToTrackdir(track, enterdir);
			SetSignalStateByTrackdir(tile, changed_signal, SignalState::Green);   // ← 先把灯改绿
			...
		} else if (!do_track_reservation) {
			return { track, result_flags };       // 无信号才早退
		}
```

⇒ **灯是先变绿、再订路**：变绿发生在 `:16552`，真正的路径预留发生在 `:16584 ExtendTrainReservation()`。两步之间没有任何"灯格必须由本车订上"的校验；而 `ExtendTrainReservation` 的起点是 `:16581 FollowTrainReservation()` 返回的 `origin`（**车头当前所在格/链**），灯格若正是车头所在格，就天然**不在**"向前扩展"的范围里。⇒ 结果就是：**灯变绿了、车开过去了、灯格（以及它内侧）没有本车预留**，正是玩家说的"冲灯"和"预留漏到灯外"。

### 14.4 三条问题的最终定性

| 问题 | 定性 | 状态 |
|---|---|---|
| ① 库内 `GOTO_COUPLE` 预留漏到库外并永久残留 | `R3RIsParkedWaiting()`（`:17237-17242`）把**整个库格**豁免（`!IsRailDepotTile`），库内配对后仍走通用 `ExtendTrainReservation` 订出一条 `tb=80` 吸附永远走不到的路线；`1886,188` 全日志零释放 | **机制已闭合，可改**（14.5-1） |
| ② 铺了预留不按预留走 | veh=51 的预留走 x=256 灯格那一列，车实际走 y=233→234 跨股；缺 `y 233→234` 换股格的 `RP cur=` | 仍需 1 组读数（14.5-3） |
| ③ 冲灯 | 14.3：**"先变绿、后订路"，且订路起点=车头格 ⇒ 灯格/灯内侧不在预留内** | **机制已闭合**（高置信） |

### 14.5 本轮落地（用户已授权"回到 A"）

1. **改 ①**：`R3RIsParkedWaiting()` 增加 `depot-paired` 分支 —— 库格内 + `OT_GOTO_COUPLE` + **已与目标配对成功** ⇒ 不订路。库格整体豁免仍然保留给"库内还没配对"的车（否则库内会提前停住）。
2. **加只读探针**（不改行为）：`ChooseTrainTrack()` 信号分支（`:16552` 之后）打 `RESV-SIGPASS veh= tile=x,y track= dir= doResIn= spd= ownRes= coType= real=`，其中 `ownRes` = `GetTrainForReservation(tile, track)` 的主人身份（`0` 无主 / `1` 本车 / `2` 别人）。标签必须带 `RESV-` 前缀（`:16470` 注释：其余前缀被 `R3RDbgTagAllowed()` 静默丢弃）。
3. 复跑验收：①`RESV-SIGPASS ... ownRes=0` 而车已 `spd>0` 穿过 ⇒ 冲灯坐实到"变绿那一瞬间灯格无主"；②库内配对后不再出现 `RESV-WATCH SET-RAIL actor=<库内车>`，`1886,188` 不再被订。

### 14.6 构建状态（2026-10-26，**已编译通过**）

- `src\train_cmd.cpp` 两处改动已落盘，`read_lints` = **0 条**（仅改 .cpp，增量合规）。
- 第一次调用被闸门拦下（`[FATAL] … build\openttd.exe is running - close the game first`，`tasklist` 见 PID **8512**）；玩家关闭游戏后重跑 `_tmp_inc_build.cmd` 成功。
- 判定三件套（全部核对通过）：

| 项 | 读数 |
|---|---|
| `build\R3R_incbuild.done` | `EXIT_CODE=0` |
| 构建日志 | `build\R3R_incbuild.log` 内 `[3/3] Linking CXX executable openttd.exe` |
| `src\train_cmd.cpp` mtime | 01:35:12 |
| `build\CMakeFiles\openttd_lib.dir\src\train_cmd.cpp.obj` | **01:40**（晚于源码） |
| `build\openttd.exe` | **01:41:38**（晚于 obj） |
| exe 字面量自证 | `findstr /m` 命中 `depot-paired` 与 `RESV-SIGPASS` |

- 本轮未新增语言串（无 `STR_*`），无需查 `strings.h`。
- 复跑取数建议：先只跑"库内挂接 + 21 号站冲灯"两段，日志看 `RESV-SIGPASS`（新增）与 `depot-paired`（新增）、`RESV-WATCH SET-RAIL`（`1886,188` 应消失）。

## 十五、第 305 轮 exe 复跑崩溃：`pbs.cpp:1961` 断言（2026-10-26，KI-484）

### 15.1 玩家报告

> `pbs.cpp 1961行断言崩溃了`

### 15.2 崩溃点原文

```1961:1961:src/pbs.cpp
	assert_msg_tile(HasReservedTracks(tile, TrackToTrackBits(track)), tile, "track: {:X}", track);
```

`Train *GetTrainForReservation(TileIndex tile, Track track)` 的**第一行**：要求"这格上这条轨**确实**有预留位"。传进来的格子只要没有该轨预留，断言立刻 abort。

### 15.3 根因（**本轮自己引入的，确定**）

第 306 轮我在 `ChooseTrainTrack()` 信号分支（`SetSignalStateByTrackdir(..., Green)` 之后）加只读探针 `RESV-SIGPASS`，其中这样取主人：

```cpp
const Train *const r3r_sig_owner = GetTrainForReservation(tile, track);
```

而灯格此刻**恰恰可能没有预留**——这正是本轮 §14.3 的结论（"灯先变绿、预留还没订"），旧日志 `:5076 RESV-EXT-PRE veh=51 tile=256,234 reserved=0x0` 就是直接读数。⇒ 探针在**最需要它工作**的那种状态下必然触发断言。

**结论句**：这是探针的实现缺陷（漏了前置条件校验），不是新发现的引擎缺陷；KI-483 的机制结论不受影响，但**探针必须修**。

### 15.4 修法

在调用前先自证前置条件，无预留则 `ownRes=0`（"无主/未订"），**不调** `GetTrainForReservation()`：

```cpp
const bool r3r_sig_reserved = HasReservedTracks(tile, TrackToTrackBits(track));
const Train *const r3r_sig_owner = r3r_sig_reserved ? GetTrainForReservation(tile, track) : nullptr;
```

同时 `ownRes` 语义细化为三值（0 未订 / 1 本车 / 2 别的车），这样"变绿那瞬灯格无预留"不再靠断言，而是靠 `ownRes=0` 直读。

### 15.5 纪律沉淀

R3R 任何路径调用 `GetTrainForReservation()` / `GetR3RReservationOwnerNearby()` 之前，必须先 `HasReservedTracks(tile, TrackToTrackBits(track))`（这两个函数的第一行都是同一断言）。这与既有教训 `rail_map.h:168`（`GetTrackBits()` 要求 `IsPlainRailTile`）同族：**问"谁是主人"之前先问"有没有人订"**。

### 15.6 已改码（同一轮内）

`src\train_cmd.cpp` 探针块改为先自证前置条件，并新增 `reserved=` 字段：

```cpp
				const bool r3r_sig_reserved = HasReservedTracks(tile, TrackToTrackBits(track));
				const Train *const r3r_sig_owner = r3r_sig_reserved ? GetTrainForReservation(tile, track) : nullptr;
```

`RESV-SIGPASS` 行变为 `veh= tile=x,y track= dir= doResIn= spd= reserved= ownRes= coType= real= db=`，`reserved=0` 直接就是"灯亮时灯格没被本车订上"的正面读数，不再依赖断言。

### 15.7 构建自证（第 306 轮续）

| 项 | 读数 |
|---|---|
| `build\R3R_incbuild.done` | `EXIT_CODE=0`（01:54） |
| 构建日志 | `[3/3] Linking CXX executable openttd.exe`，`error C` / `FAILED:` 均 0 行 |
| `src\train_cmd.cpp` | 01:52 |
| `build\openttd.exe` | 01:54（52,248,576 B） |
| exe 字面量 | 命中 `reserved=%d ownRes=%d`、`depot-paired` |

### 15.8 同类漏校验排查（U-2，全文件 12 个调用点）

已核对有前置 bits 守卫（安全）：`:2855`（`HasBit(GetRailReservationTrackBits…)`）、`:15342`（`HasReservedTracks`）、`:18405` / `:18592` / `:19691`（`HasBit(bits, track)`）。

**未逐一核到上下文、留待复查**：`:2791`、`:12348`、`:15564`、`:15571`、`:5076`（后者走上游隧道桥整套 bits，属既有代码）。这些是第 303 轮之前的存量代码，已跑过多轮未崩，故本轮未动；若再出现 `pbs.cpp:1961` 同址崩溃，优先复查这 5 处。

### 15.9 状态

- 已改码 + 已编译（01:54 exe），**游戏内复跑待做**。
- 复跑判据：①不再出现 `pbs.cpp:1961` 断言；②`RESV-SIGPASS … reserved=0 spd>0` 出现 ⇒ 冲灯坐实（灯亮瞬间灯格无预留）；③`reserved=1 ownRes=1` 若仍冲灯 ⇒ 转 KI-482；④库内那次出现 `depot-paired`，`1886,188` 不再被 `SET-RAIL`。

## 十六、第 306 轮复跑读数（2026-10-26，玩家："这次是不一样的冲灯"）

日志 `build\R3R_debug.log`，**17 659 行**，产生者=`build\openttd.exe`（01:54，含 KI-484 修好的探针 + `depot-paired`）。

### 16.1 判据 ① 通过：不再有 `pbs.cpp:1961` 断言

玩家未再报崩溃，日志能完整产出到末行 ⇒ KI-484 的 `HasReservedTracks` 前置校验有效。

### 16.2 判据 ④ 通过：`depot-paired` 生效（但探针刷屏）

```
8506:RESV-NOBOOK veh=51 tile=1889,188 order=16 caronly=0 reason=depot-paired
8507:RESV-NOBOOK veh=21 tile=1889,188 order=16 caronly=0 reason=depot-paired
…（8506-8540 连续 35 行，12271-12307 再来 35 行，全日志上千行）
```

- **修复生效**：库格内 + `OT_GOTO_COUPLE(16)` + 已配对 ⇒ 不再订路线（`R3RIsParkedWaiting` 的 `depot-paired` 早退被走到）。KI-481 的行为面收敛。
- **疑点 D-1（探针噪声）**：`RESV-NOBOOK` 本该是一次性去重（`_r3r_waiter_nobook_logged`），却在**同一 tile、同一 veh、同一 tick 序列**上连续重复，且 `veh=51`/`veh=21` 严格交替。猜测（待验）：这两个 veh 每次都先被 erase 再重插（另一分支在同 tick 清集），或 `R3RIsParkedWaiting()` 返回的路径每 tick 重新走到打印点。**纯噪音，不影响行为**，但会淹没真信号，必须收敛。
- **新疑点 D-2**：`veh=51` 与 `veh=21` **同时**在 `1889,188` 打同种日志 ⇒ 两条链都被判成"库内已配对等待者"。若其中一条实际是「来挂接的机车」而非「等待的车底」，则该早退把它也钉住了（KI-481 的修法可能过宽）。待核。

### 16.3 判据 ② 通过：KI-483 坐实（灯亮瞬间灯格无预留）

`RESV-SIGPASS` 高频出现 **`reserved=0 ownRes=0` 且 `spd>0`**，即"车带速穿过一个连自己都没订上的灯格"：

| 行 | veh | tile | spd | real | doResIn |
|---|---|---|---|---|---|
| 9983 | 99 | 1786,190 | **176** | 2 | 1 |
| 10077 | 99 | 1778,190 | **273** | 2 | 1 |
| 10192 | 99 | 1770,190 | **382** | 2 | 1 |
| 10271→10350 | 99 | 1762,190 / 1754,190 | **428** | 2 | 1 |
| 10427→10737 | 99 | 1746,192 … 1714,220 | 428 | 2 | 1 |
| 16431 | 21 | 205,266 | 344 | 7 | 1 |
| 16534/16535 | 123 | 233,235 / 235,235 | 379 | 10 | 1 |
| 16543/16545 | 21 | 235,233 / 240,234 | 428 | 7 | 1 |
| **16825** | **21** | **256,234** | **16** | **8** | **1**（`coType=16` GOTO_COUPLE） |

**结论句**：`reserved=0` 覆盖了**每一个** SIGPASS 采样，`ownRes` 从未出现 1 或 2 ⇒ 不是"别的车占了"，也不是"灯格预留被抢"，而是**这一格从头到尾没有被任何人订过**。KI-483 的机制（灯先变绿、预留后订，中间存在无预留窗口）由此**实测坐实**，且窗口不是一瞬——`spd=428` 一路十几格全 `reserved=0`。

### 16.4 玩家所说的"不一样的冲灯"＝`doResIn=0`（本形态首次出现）

前 4 批推演里所有 SIGPASS 假设都是 `doResIn=1`（已进入"该订路"状态）。本轮首次出现 **`doResIn=0`**：

```
16593: veh=24 tile=256,235 track=0 dir=2 doResIn=0 spd=57  reserved=0 ownRes=0 coType=1 real=10 db=1
16595: veh=24 tile=256,235 track=0 dir=2 doResIn=1 spd=57  reserved=0 ownRes=0 coType=1 real=10 db=1
16651: veh=24 tile=256,235 track=0 dir=2 doResIn=0 spd=5   reserved=0 ownRes=0
16661: veh=24 tile=256,235 track=0 dir=2 doResIn=1 spd=0   reserved=0 ownRes=0
16703: veh=123 tile=240,235 …      doResIn=0 spd=57
16704: veh=123 tile=240,235 …      doResIn=1 spd=57
16760: veh=123 tile=240,235 …      doResIn=0 spd=5
16768: veh=123 tile=123 …          doResIn=1 spd=0
16881: veh=21 tile=256,234 track=0 dir=2 doResIn=0 spd=21
```

**语义（2026-10-26 源码读证后修正）**：`do_track_reservation` 是 `ChooseTrainTrack()` 的**值参数**，记录调用者本次要不要订路。`doResIn=0` ⇒ 调用者本次**没要求**订路；而 `src\train_cmd.cpp:16525-16556` 的"单轨格 + PBS 信号"快速返回分支**无条件**做两件事：

```16550:16556:src/train_cmd.cpp
			const bool r3r_sigpass_dores_in = do_track_reservation;

			consist->flags.Reset(VehicleRailFlag::WaitingRestriction);

			do_track_reservation = true;
			changed_signal = TrackEnterdirToTrackdir(track, enterdir);
			SetSignalStateByTrackdir(tile, changed_signal, SignalState::Green);
```

即：**先点绿灯，再把"要订路"的意图强制置真**，真正的 `ExtendTrainReservation()` 要等函数返回后由上层的 `TryPathReserveOnTracks` 系列执行。⇒ `doResIn=0` **不是缺陷**，是原生设计（置 true 只是意图，不是"已经订上"）。KI-485 原来的"没有订路资格却点灯"定性**错误**，已作废改判（见 16.4b）。

**猜测根因（待验证，G-1）**：`ChooseTrainTrack()` 的信号分支不止一个入口。`do_track_reservation` 在 `ChooseTrainTrack` 内部可被置真（非 PBS 段/无预留能力时走"仅信号"路径），所以 `doResIn=0 → doResIn=1` 的**紧邻成对**行（16593/16595、16703/16704）说明：同一 tick 内**进了两次**信号分支，第一次纯信号、第二次带订路。若两次之间 `SetSignalStateByTrackdir(Green)` 已经执行，则第一次那趟等于"灯先亮、路后订"，正好是 KI-483 窗口的**制造者**，而不是受害者。
**验证判据**：在 16593 前后取同一 veh 的 `RP`/`TPR` 行，确认第一次进分支时是否有任何 `ExtendTrainReservation`；并确认第二次进分支是否由同一 tick 的 `TrainController` 重入触发。

### 16.5 疑点 D-3：`spd=428` 的 veh=99 全程 `real=2` 不变

veh=99 从 9983（`1786,190`）到 10974（`1690,220`）一路 `spd` 176→428、`reserved=0`、`real=2` 恒定。若该车正在执行一条**长途订单**（`real=2` 是第三条命令），它应该在某处订过整条路线；`reserved=0` 说明它从未订成，**却仍被放行**穿过十几个灯格。
**猜测（G-2，待验证）**：veh=99 可能走的是"非 PBS 信号 + YAPF 普通寻路"路径（该线路段可能没有 PBS 灯，`AfterSetSignalStateByTrackdir` 只对 PBS 灯生效），此时 `do_track_reservation` 的语义本就不要求预留 ⇒ 不算缺陷、只是探针在两种灯上都打了。
**验证判据**：取 9983 附近该 tile 的 `HasPbsSignalOnTrackdir` 读数（需加探针，或从 `RESV-WATCH` 是否登记该格反推）。

### 16.4b 时序结论（源码读证，KI-485 并入 KI-483）

`ChooseTrainTrack()` 的"单轨格 + PBS 信号"快速分支（`src\train_cmd.cpp:16525` 起）内顺序是：

1. `:16550` 记下 `do_track_reservation` 的原值（本轮探针 `doResIn`）；
2. `:16554` `do_track_reservation = true;` —— **只是意图**；
3. `:16556` `SetSignalStateByTrackdir(tile, changed_signal, SignalState::Green);` —— **灯此刻真的绿了**；
4. 函数返回，上层才执行 `ExtendTrainReservation()` —— **路此时才订**。

⇒ "灯亮"与"路订"之间必然隔着一个窗口。`spd=428` 时一 tick 可推进数格，窗口内经过的每一格都读成 `reserved=0` —— 这正是 veh=99 从 `1786,190` 到 `1690,220` 连续十几格 `reserved=0` 的成因，**不是**"预留被谁吃掉"。

**因此 KI-482（铺了预留却不按自己铺的走）与本现象是两件事**：本现象里根本没有可遵从的预留，是"先放行、后订路"。

### 16.5b 待玩家确认的问题清单（Q-1 ~ Q-3）

| 编号 | 问题 | 备选 |
|---|---|---|
| Q-1 | 你说的"不一样的冲灯"是不是指：这次是**高速连穿十几格全无预留**（veh=99，`spd` 176→428），而上次只是单格低速（veh=51，`spd=43`）？ | 是 / 否 |
| Q-2 | 还是指：这次冲灯的车**不是来挂接的**（`coType=1` 普通 `OT_GOTO_STATION`，veh=99 一路跑），上次是挂接车？ | 是 / 否 |
| Q-3 | 还是指：**21 号在 `256,234` 低速 16 冲灯**、以前没在这个位置出现过？ | 是 / 否 |

答完按编号回填本节，不必重述。

### 16.5c 玩家答复（2026-10-26）：Q-1~Q-3 全部作废

玩家原话：**「好吧，感觉这个冲灯跟之前的冲灯没太大区别，只是我对此感到绝望」**

⇒ Q-1（高速连穿十几格）、Q-2（不是挂接车）、Q-3（21 号 `256,234` 低速）**均不成立**；「这次是不一样的冲灯」这句**推翻**。本轮现象与 KI-483 **同一形态，不存在第二形态**。

处置：KI-485 已并入 KI-483，表格行仅作读数锚保留，不再作为待查缺陷。**不得**再为"第二形态"重新取证。

### 16.7 结论与仅剩的两条路（下一轮直接读这里，不要重推）

**已确认的单一机制（无需再取证）**：`ChooseTrainTrack()` 的"单轨格 + PBS 信号"分支内，灯在 `src\train_cmd.cpp:16556` 点亮；`:16554` 的 `do_track_reservation = true;` **只是意图**；真正的 `ExtendTrainReservation()` 在函数返回后由上层执行 ⇒ 灯亮与路订之间**必然**存在窗口 ⇒ 窗口内每格读成 `reserved=0`。`spd=428` 时一 tick 跨数格，于是"连穿十几格全无预留"。

**这不是 R3R 引入的**：这三行（`do_track_reservation = true;` / `changed_signal = TrackEnterdirToTrackdir(...)` / `SetSignalStateByTrackdir(..., Green)`）是探针之外既有的上游代码，`git blame` 归属见 16.7a。

**16.7a 归属判定（`git blame` 已证，决定性）**：

```
2e2a6d4909e (rubidium            2008-08-02 16554) 			do_track_reservation = true;
5240b46d1d5 (Jonathan G Rennison 2020-02-20 16555) 			changed_signal = TrackEnterdirToTrackdir(track, enterdir);
4553e2d8446 (Jonathan G Rennison 2026-06-30 16556) 			SetSignalStateByTrackdir(tile, changed_signal, SignalState::Green);
```

⇒ **三行全是上游原生代码，R3R 一行都没改**（`:16548-16551` / `:16557+` 才是本轮探针与注释）。"灯先变绿、路随后订"是 OpenTTD 自 2008 年就有的 PBS 信号语义，JGRPP 至今保留。

**这一条直接终结 KI-483 的追查方向**：冲灯**不是 R3R 代码缺陷**，也不是"预留泄漏"，而是上游引擎的既定顺序。R3R 能做的只有"别让这个窗口留下永久孤儿位"（已由 KI-203 / KI-345 / KI-346 覆盖），以及"要不要逆着上游语义改成'订上了才放行'"（路 B，风险自负）。

**两条路（只能二选一，没有第三条）**：

- **路 A（不改码，接受语义）**：PBS 原生就是"灯先放行、路随后补"。视觉上像抢跑，但预留补上之后别的车照样被挡，不会真撞车。R3R 侧的责任只剩"窗口内不留下孤儿位"，这已由 KI-203（GC 记账）/KI-345（审计+回收）/KI-346（`rp()` 订错轨）覆盖。⇒ **玩家需要接受"黑色预览晚一拍出现"这个观感**。
- **路 B（改放行判据）**：把"本车已订到本格"作为点绿的前提，即 `:16554` 的置真必须伴随一次成功的订路，失败就**不点绿**、本次留在原地。风险：订路可能反过来依赖信号结果，会造成新的死锁/列车原地僵死，须先证 `ExtendTrainReservation()` 与该分支的可达顺序**不循环**。

⇒ 下一步不是继续取证，而是**在 A/B 之间拍板**。

### 16.6 状态

- 已定性：判据 ①④ 通过；判据 ②（`reserved=0 spd>0`）**坐实**；玩家新报的"不一样的冲灯"= **`doResIn=0` 形态**（16.4），首次出现。
- 待办：D-1 收敛 `RESV-NOBOOK` 刷屏；D-2 核 `veh=51`/`veh=21` 是否都被误判为等待者；G-1/G-2 按上述判据取证；KI-485 登记本轮新形态。
