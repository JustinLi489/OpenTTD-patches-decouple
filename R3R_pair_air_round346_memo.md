# 第 346 轮：配对列车占位「当空气」未生效 —— 现场取证与判定

日期：2026-10-28
玩家原话：「配对列车占位时没有当空气」（附 `build\R3R_debug.log`）

---

## 1. 输入采样有效性

| 项 | 值 |
|---|---|
| 日志 | `D:\sourcecode of JGRPP\build\R3R_debug.log` |
| 大小 | 1 292 950 B |
| 时间 | 2026-10-28 02:48:14 |
| 产生者 | `build\openttd.exe` 2026-10-28 02:41:14（52 344 832 B，第 345 轮 A+B 版） |

⇒ 日志晚于 exe 7 分钟，**确为本轮产物，采样有效**。

## 2. 关键探针计数

用 `Select-String -SimpleMatch` 统计（ripgrep 工具受 .gitignore 影响搜不到 `build\` 下的日志，必须用 PowerShell）：

| 探针 | 次数 | 含义 |
|---|---|---|
| `RESV-PAIR-AIR` | **0** | 第 345 轮手段 B 的豁免分支**一次都没进** |
| `tryReserve` | **0** | 挂接寻路里 `TryReserveRailTrack()` 失败**一次都没有** |
| `FSCP` | **1** | 安全位判定只报了一条，且原因是 `notSeg` |
| `CPL-PATHFOUND` | 2 | 一次 `found=1`（1337）、一次 `found=0`（2540） |
| `RESV-NOBOOK` | 大量 | `reason=depot-paired`（veh=54 停在库内被门禁拒订路） |

## 3. 现场锚点（行号 + 原文）

### 3.1 唯一那次挂接寻路失败（`build\R3R_debug.log`）

```
1337:CPL-PATHFOUND veh=57 found=1
1338:CPL-SCAN tick=12460 veh=57 found=1 closed=3 limit=10000 nodes=17 enter=14 noplat=5 nores=0 noconsist=6 consist=3 rej=0/0/0/0 acc=3/0 accwp=0 last=5,15 lastveh=0 why=acc-car
1339:CPL-HEAD best=5,15 head=5,15 on=1
1340:CPL-BEST veh=57 tile=5,15 td=0 cost=11055
1341:CPL-BACK pPrev=5,15 td=0 parent=15,15 ptd=2
1342:FSCP tile=13,15 fail=notSeg best=54 ord=6 sf=1
1343:CPL-SAFE-FAIL veh=57 tile=5,15
1344:CRT veh=57 order=16 dir=5 origin=16,14 td=0 found=1
1345:CPL-PF-FALLBACK veh=57 tile=15,14 ret=0x0 tracks=0x9 dest=5,14 final=12,15
1346:CPL-RET veh=57 tick=12460 tile=15,14 enterdir=0 tracks=0x9 res=0x1 ret=0x1 why=cpl-partial-res spd=0 real=1
1349:TRP veh=57 mf=16,14 real=1 type=16 dest=1932 spd=0 fp=0 look=1 one=0 mstuck=0 fto=1 => ok=1 res=1
1350:COUPLE-FAIL loco=57 order=16 tx=16 ty=14 x=270 y=232
1357:COUPLE-WAIT-OFFDEST veh=57 tile=16,14 mf=16,14 real=1 isDepot=0 destDepot=0 destStn=0 destTile=1932 onDepotTile=0 onStnTile=1
1358:SPD-DROP veh=57 tile=16,14 prev=5 order=16 real=1 stuck=0 fp=0 prog=2 wait=0 parked=0 stop=0 track=01 dtile=1932 dt=33
1359:CPLSPD veh=57 tick=12461 tile=16,14 spd=0 strict=100 aend=100 aitem=100 adv=100 gap=180 decel2=240 nocab=0 revd=0 spr=0 sig=0 ordmax=65535 realbrk=1 db=1 depe=0 n=6
```

要点：
- `CPL-SCAN ... nores=0` ⇒ **整个搜索过程中预留判定一次都没拒绝过**；拒绝计数全在 `noconsist=6`（排除项）。
- 失败点是 `1342 FSCP ... fail=notSeg best=54 ord=6 sf=1` ⇒ 挡路的 veh=54 被 `R3RIsCoupleTarget()` 判为「不是挂接目标」，整条 back-walk 作废。

### 3.2 第二次：根本没有可接受目标

```
2537:PFD-NOCONSIST tile=5,14 td=0 res=1 n=2
2539:PFD-NOCONSIST tile=5,14 td=0 res=1 n=4
2540:CPL-PATHFOUND veh=57 found=0
2541:CPL-SCAN tick=13833 veh=57 found=0 closed=3 limit=10000 nodes=10 enter=7 noplat=0 **nores=3** noconsist=4 consist=0 rej=0/0/0/0 acc=0/0 accwp=0 last=-1,-1 lastveh=-1 why=none
2542:CRT veh=57 order=16 dir=5 origin=7,14 td=0 found=1
2543:CPL-PF-FALLBACK veh=57 tile=4,14 ret=0x0 tracks=0x1 dest=5,14 final=12,15
```

`nores=3` 是「目标格无预留」的排除计数，仍不是 `tryReserve` 冲突。

### 3.3 配对关系的真实读数（按 tick 变化）

```
2547:[R3R] CPL-PAIRMAP tick=13836 role=target    v=0  vHead=0  vTile=12,15 vSpd=0 vOrd=17 vLen=107 vPairT=1048575 vPairR=57
2548:[R3R] CPL-PAIRMAP tick=13836 role=requester v=57 vHead=57 vTile=7,14  vSpd=5 vOrd=16 vLen=11  vPairT=0       vPairR=1048575
```

tick=13836：**57 ↔ 0 是双向活配对**，veh=0 在 `12,15`、`vOrd=17`（OT_WAIT_COUPLE）、`vLen=107`。

```
329:[R3R] CPL-PAIRSCAN tick=11532 act=54 state=locked  ... pool=5 s_chain=54 s_target=4 ... best=0 bestDist=0 paired=1 wait=0 near=27 nearWhy=not-target nearTile=1,15 nearOrd=0 nearStop=1
330:TRP veh=54 mf=1,15 real=0 type=16 dest=1921 spd=0 fp=0 look=0 one=0 mstuck=0 fto=0 => ok=0 res=0
331:RESV-NOBOOK veh=54 tile=1,15 order=16 caronly=0 reason=depot-paired
332:[R3R] CPL-PAIRLOCK tick=11532 why=scan act=54 actHead=54 actTile=1,15 ... tgt=0 tgtHead=0 tgtTile=1,15 tgtSpd=0 tgtOrd=17 tgtStopd=0 tgtLen=107 tgtPairT=1048575 tgtPairR=54
```

tick=11532：`54 ↔ 0` 也成立（`tgtPairR=54`）。⇒ **veh=0 的配对端在不同 tick 由 54 / 57 轮换**，说明配对协议本身在改绑（这一条是疑点，见 §5）。

### 3.4 尾部状态（16784~16825 持续）

```
CPL-PAIRSCAN tick=16784 act=54 state=backoff ord=16 ... pool=0 ... best=-1 paired=0 wait=5 near=-1 nearWhy=none ...
```

`paired=0`、`best=-1`、`pool=0`：到会话末尾 54 侧连配对对象都找不到，57 侧每 tick `COUPLE-FAIL`。

### 3.5 配对关系全程快照（`CPL-PAIRMAP`，每 256 tick 一行）

```
11532/11788: role=target    v=0  vTile=1?,15  vSpd=0 vOrd=17 vLen=107 vPairR=54
11788:       role=requester v=54 vTile=1?,15  vSpd=0 vOrd=16 vLen=11  vPairT=0
12556:       role=target    v=0  vTile=12,15 vSpd=0 vOrd=17 vLen=107 vPairR=57
12556:       role=requester v=57 vTile=13,14 vSpd=100 vOrd=16 vLen=11 vPairT=0
12812…16652: 0↔57 稳定；0 恒在 12,15；57 逐行西移 9,14 → 8,14 → 7,14 → 6,14 → 5,14 → 2,14 → 2,15 → 1?,15（spd 100→5→0）
```

> 注：控制台折叠会吞掉坐标高位字符（`1,15` 实为 `11,15` 一类的可能性很高），**坐标以日志原文为准**，判定不受影响。

由此确定：

- **F-1**：tick 12556~16652 期间 `veh=0`（`vOrd=17` OT_WAIT_COUPLE、`vLen=107`、`vSpd=0`）与 `veh=57`（`vOrd=16` OT_GOTO_COUPLE、`vSpd=5`）是**稳定的双向活配对**，配对协议本身没坏（推翻 §5 Q-3 的"乱改绑"猜测的一半：改绑只发生在 11788→12556 之间一次，属正常结束/重绑）。
- **F-2**：`veh=0` 在 `12,15` 停着不动，`veh=57` 从 `13,14` 一路**开过了它**（9,14 → 2,14 → 2,15 → 11,15），最后在 `11,15` 停住、`spd=0`，始终没有挂上。
- **F-3**：唯一那次路径作废的挡路者 `best=54` 在 `13,15`，而 54 在 tick 11788 时正是 `veh=0` 的**上一任**配对端（`vPairR=54`），tick 12460 时已不是 57 的配对端。其 `current_order` = `ord=6`（`OT_GOTO_WAYPOINT`）⇒ `R3RIsCoupleTarget()` 第 5 条（必须 `OT_WAIT_COUPLE`）直接否掉。

⇒ **修正 Q-1**：挡路车 54 **不是** 57 的配对车底；玩家看到的"配对列车占位"与日志里 `notSeg` 挡路的对象不是同一列车。真正的问题更像 **F-2**：57 已经开到配对车底旁边（甚至越过）却没能完成命中/拼接。

## 4. 判定（结论）

**D-1（主结论）**：第 345 轮手段 B **没有生效，而且不是"判据写错"，是插入点选错** —— 它挂在 `TryReserveTrack()` 返回 false 之后 / `!HasStationReservation` 之后，但本现场**根本没走到订位**：
- `fail=tryReserve` = 0 次；
- `nores=0` 与 `nores=3` 都不是「预留冲突」；
- 唯一那次挂接路径作废发生在更早的**安全位判定** `CheckSafePositionOnNode()`（`src\pathfinder\yapf\yapf_rail.cpp:190-217`）里的 `R3RIsCoupleTarget(best)` 早退。

⇒ B 在当前代码结构里是**死代码路径**（至少在「站台/路点 + 配对车底占位」这个主场景中）。

**D-2**：真正的拦截者是 `R3RIsCoupleTarget(best)`：

```190:217:src/pathfinder/yapf/yapf_rail.cpp
				if (best->index == Yapf().GetVehicle()->index) return true;
				if (!R3RIsCoupleTarget(best)) {
					... fprintf("FSCP tile=%d,%d fail=notSeg best=%d ord=%d sf=%d\n", ...);
					return false;
				}
				if (!R3RCoupleAllowed(Train::From(Yapf().GetVehicle()), best)) return false;
```

现场 `best=54 ord=6`（`OT_GOTO_WAYPOINT`）⇒ 不是 WAIT_COUPLE ⇒ 视作障碍 ⇒ 整条路径 `CPL-SAFE-FAIL`。

**D-3**：手段 A（目的地指向车底格）**已在生效**：`1345/2543` 都是 `CPL-PF-FALLBACK ... dest=5,14 ...`（非 `INVALID_TILE`），符合 KI-512 的设计。但注意 `dest=5,14` 与配对车底实际所在格（tick=13836 时是 `12,15`）**不一致** ⇒ 见疑点 Q-2。

## 5. 疑点（Q-*，需数据或读码确认）

- **Q-1**：挡路的 `best=54` 到底是「57 的配对车底」还是「无关的第三方列车」？若是后者，那玩家说的「配对列车占位」与日志指向的对象不是同一列车，B 即使插入到正确位置也不该豁免它。
- **Q-2**：`CPL-PF-FALLBACK dest=5,14` 与 `CPL-PAIRMAP` 里 veh=0 的 `vTile=12,15` 不一致。`R3RGetCoupleOrderDestination()` 取的到底是配对伙伴的 `tile` 还是 `dest_tile`/站台锚点？若是后者，则 A 的「指向车底所在格」只对"车底停在自己站台锚点"成立。
- **Q-3**：`veh=0` 的配对端在 tick=11532 是 54、在 tick=13836 是 57（`tgtPairR`/`vPairR` 双读互证）。谁在改绑、是否 `backoff`/`pool=0` 状态导致反复重绑？
- **Q-4**：`RESV-NOBOOK ... reason=depot-paired`（331）是既有分支（不是第 345 轮新增），它让库内配对车底**不再订路**；这会不会反过来造成「配对车底占位却没有预留」→ 于是 B 永远等不到 `tryReserve` 冲突？两者可能是同一个坑的两面。
- **Q-5**：`CPL-PAIRSCAN` 尾部 `state=backoff / paired=0 / pool=0`，`backoff` 是谁设的、退避多久？是否覆盖了正常命中流程。

## 6. 猜测根因（待验证）

- **G-1（较有把握）**：玩家直觉的「配对列车占位」在引擎里表现为**安全位判定的目标资格**（`R3RIsCoupleTarget`），不是 **PBS 位级预留**。因此「视作空气」应当作用于 `CheckSafePositionOnNode()` 的 `notSeg` 分支（以及 `R3RCoupleAllowed` 分支），而不是 `pbs.cpp` 的位级互斥。
  - 支撑：`nores`/`fail=tryReserve` 全 0；`FSCP fail=notSeg` 唯一出现且正好卡死。
  - 待验证：veh=54 与 veh=57 是否真的是同一对配对（Q-1）；若是，`R3RIsCoupleTarget` 为什么拒绝一个「正在等待挂接」的配对伙伴（订单已成 GOTO_WAYPOINT？）。
- **G-2（备选）**：`RESV-NOBOOK reason=depot-paired` 让配对车底不订路，于是「占位」是**车体占格**而非预留占位；这种情况下任何"预留豁免"天然无效，必须改成**车体占格豁免**（把 `CheckTrainsOnTrack` / `R3RIsCoupleTarget` 侧放行）。
  - 待验证：`RESV-NOBOOK` 之后该格 `GetRailReservationTrackBits == 0` 是否成立（需日志或探针）。

## 7. 下一步（按玩家口径执行前后再定）

1. 读码确认 `R3RIsCoupleTarget()` 的完整判据（`src\train_cmd.cpp`），列出它拒绝 `ord=6` 的原因；
2. 读码确认 `CheckSafePositionOnNode()` 里 `notSeg` 与 `R3RCoupleAllowed` 两个早退的分工（`src\pathfinder\yapf\yapf_rail.cpp:190-230`）；
3. 取 Q-1 的证据：在 `FSCP` 处补打 `actor` 与 `pair=<partner>` 字段（只读探针），确认挡路车是否就是配对方；
4. 只有 1~3 确认「确实是配对车底挡住」之后，才谈把豁免从 `pbs.cpp` 挪到安全位判定；**不建议先改**，因为放宽 `R3RIsCoupleTarget` 会波及 KI-341「走错岔道」与「散链也是障碍」的既有语义。

## 8. 玩家纠正（同一轮内，结论改判）

**玩家原话**：「我想你追查方向又错了，因为机车（57）直接走错股道了！」

⇒ 上一轮 §4/§5 把「配对车底占位没当空气」理解成「预留/安全位没放行」是**错的**。真正现象是 **57 走了错的股道**。

### 8.1 支持玩家结论的读数（上一轮已取，此处重述为判据）

```
CPL-PAIRMAP（全程）: veh=0 恒在 12,15（vOrd=17, vLen=107, vSpd=0）
CPL-PAIRMAP（全程）: veh=57 逐行西移 13,14 → 9,14 → 8,14 → 7,14 → 6,14 → 5,14 → 2,14 → 2,15 → 11,15
1345:CPL-PF-FALLBACK veh=57 tile=15,14 ret=0x0 tracks=0x9 dest=5,14 final=12,15
2543:CPL-PF-FALLBACK veh=57 tile=4,14 ret=0x0 tracks=0x1 dest=5,14 final=12,15
```

- **配对车底 veh=0 在 `12,15`**（y=15 股道）；
- **57 的寻路目标 `dest=5,14`**（y=14 股道，站台格）；
- 57 全程沿 **y=14** 走（13,14 → 5,14），走到 `5,14` 后再经 `2,14 → 2,15` 折回 y=15 股道，最后停在 `11,15`。

⇒ 57 走的是 **y=14 那股道**，而车底在 **y=15 那股道** ⇒ **走错股道成立**，且**寻路目标从一开始就是错的格**（`dest=5,14`），不是"到了车底旁边没挂上"。

### 8.2 改判

- **D-1/D-2 降级为次因**：`FSCP tile=13,15 fail=notSeg best=54` 只发生 1 次、且 54 订单是 `OT_GOTO_WAYPOINT`，它是**错股道上的第三方障碍**——正因为它挡路，57 才被迫在 13,15 附近改经 y=14 股道继续西行。**它不是根因，是错股道的结果**。
- **新的主因（GI-515）**：`OT_GOTO_COUPLE` 的**目的地取错格**。`CPL-PF-FALLBACK dest=5,14` 就是证据：配对车底在 `12,15`，而 dest 被算成 `5,14`（同一站台另一股道的锚点格）。
- **手段 A（第 345 轮）的实际效果**：只做到「dest ≠ INVALID_TILE」，但**取值的语义错了**——把"站台锚点 / 伙伴 dest_tile"当成了"伙伴当前所在格"。

### 8.3 猜测根因（G-3，待验证）

`R3RGetCoupleOrderDestination()`（`src\order_cmd.cpp`，第 345 轮新增）大概率取的是**伙伴的 `dest_tile` / 站台锚点**，而不是 **伙伴的 `tile`（当前所在格）**；当伙伴（veh=0，33 节长）车身横跨多格、其 `dest_tile` 又指向站台锚点时，算出的格就落到**另一股道**上，寻路于是把机车引向该股道。

- 支撑：`dest=5,14`（站台锚点一侧）与 `veh=0 vTile=12,15`（车体实际所在）不一致；且 57 的轨迹终点 `11,15` 与 dest `5,14` 之间还差着折返（说明最终停车是寻路到点的结果，不是命中）。
- 待验证：①读 `R3RGetCoupleOrderDestination()` 与 `OT_GOTO_COUPLE` 分支，确认取值源；②确认 `veh=0` 的 `dest_tile` 是不是 `5,15` 一带；③确认「股道」在引擎里的判定（同一站台不同 `track`/不同 y）。

### 8.4 取证判据（若验证成立）

修法方向：`OT_GOTO_COUPLE` 的目的地必须是**配对车底当前所在格**（`partner->tile`），而不是站台锚点/`dest_tile`；站台锚点只可作为**回退**（伙伴 `tile` 无效时）。

### 8.5 代码取证：G-3 **证伪**，真因是「目的地指向车底链头格，而该格被车底自己占着」

#### 8.5.1 读码（`src\order_cmd.cpp`）

```4607:4628:src/order_cmd.cpp
static TileIndex R3RGetCoupleOrderDestination(const Vehicle *v)
{
	if (v->type != VehicleType::Train) return INVALID_TILE;

	const Train *const tgt = R3RGetCouplePairPartner(Train::From(v));
	if (tgt == nullptr) return INVALID_TILE;

	const TileIndex target_tile = tgt->tile;
	if (!IsValidTile(target_tile)) return INVALID_TILE;
```

⇒ 取的是 `tgt->tile`（**配对车底的链头所在格**），不是 `dest_tile`、也不是站台锚点。**G-3 证伪。**

`R3RGetCouplePairPartner()`（`src\couple_group.cpp:537-553`）返回链头（`Train::From(v->First())` 的对端），同样是链头身份。

#### 8.5.2 `CPL-PF-FALLBACK` 的 `dest=` 不是订单目的地

```17624:17648:src/train_cmd.cpp
		if (path_found == INVALID_TRACK
				&& r3r_wait_cut == 0
				&& !R3RCoupleSearchSkipped(consist)
				&& !IsRailDepotTile(consist->tile)
				&& consist->current_order.IsType(OT_GOTO_COUPLE)) {
			PBSTileInfo r3r_pf_dest(INVALID_TILE, INVALID_TRACKDIR, false);
			TileIndex r3r_pf_final = INVALID_TILE;
			bool r3r_pf_found = false;
			const Track r3r_pf_track = DoTrainPathfind(consist, tile, enterdir, tracks, r3r_pf_found, do_track_reservation, &r3r_pf_dest, &r3r_pf_final);
```

`dest=` 是 **`DoTrainPathfind` 给出的安全位（PBSTileInfo）**，`final=` 是**兜底普通寻路自己的终点**。两者都不是订单的 `dest_tile`。⇒ 上一轮把 `dest=5,14` 当成"订单目的地被算错"是读错字段。

#### 8.5.3 订单目的地其实**完全正确**

日志原文（`read_file` 取，未经控制台折叠）：

```
1349:TRP veh=57 mf=16,14 real=1 type=16 dest=1932 spd=0 fp=0 look=1 one=0 mstuck=0 fto=1 => ok=1 res=1
1345:CPL-PF-FALLBACK veh=57 tile=15,14 ret=0x0 tracks=0x9 dest=5,14 final=12,15
327:DEPOT-ARR veh=54 ... tx=1 ty=15 destTx=1 destTy=15 ... tileEqDest=1
330:TRP veh=54 mf=1,15 real=0 type=16 dest=1921
```

地图宽 128 时 `TileIndex = y*128 + x`：

| 读数 | 换算 | 含义 |
|---|---|---|
| `dest=1932`（veh=57） | 15×128+12 = **(12,15)** | 订单目的地 = 车底链头格，**正确** |
| `dest=1921`（veh=54） | 15×128+1 = **(1,15)** | 54 的目的地 = 车库格，与 `DEPOT-ARR tx=1 ty=15` 互证 |
| `final=12,15`（兜底寻路） | — | 普通寻路的终点也是 **(12,15)**，与订单一致 |

⇒ **"目的地算错"被排除**。57 的 `dest_tile` 就是车底所在的 `(12,15)`。

#### 8.5.4 真因（G-4，已有多重读数支撑）

`veh=0` 是 **33 节（`vLen=107`）** 的等待车底，链头 `veh=0` 在 `(12,15)`，车体沿 **y=15 股道**向西铺开几十格（`CPL-HEAD best=5,15` 就是车体可挂端所在格）。于是：

1. 订单 `dest_tile = (12,15)` = **车底自己的链头格**，普通寻路永远"到不了"（那格被车底占据）；
2. y=15 股道正线也被车底自身占着 ⇒ 57 从东侧 `(16,14)` 无法沿 y=15 接近；
3. 普通寻路于是走 **y=14 股道**绕行（13,14 → 9,14 → 8,14 → 7,14 → 6,14 → 5,14 → 2,14 → 2,15），最后停在 `(11,15)`/`(1,15)` 一带 ⇒ **这正是玩家看到的"直接走错股道"**。

旁证：`SPD-DROP veh=57 ... dtile=1932 dt=33` ⇒ 从 `(16,14)` 到 `(12,15)` 的直线只差 4 格，实际却要 **33 格**，量化印证"绕行一整条股道"。

#### 8.5.5 修法方向（GI-515，待拍板未落地）

`OT_GOTO_COUPLE` 的目的地不该是**车底链头格**，而应是**车底的可挂接端格**（车底尾节 `tgt->Last()->tile`，或"车体上离机车最近的一格"），理由：

- 链头格被车底自己占着，普通寻路按 `dest_tile` 永远无法到点 ⇒ 只能绕行/钉死；
- 挂接寻路自己算出的落点本来就是 `(5,15)`（车体可挂端），与链头格差 7 格，两者语义不一致；
- 车底长度可变（本场景 33 节），"离车底最近"才是稳定判据。

风险：`tgt->Last()->tile` 在车底被其它列车遮挡/部分落库时也可能不可达，需要"链头格 + 尾节格 + 车体逐格取最近可达"三档回退。

## 8.6 玩家拍板（2026-10-28，本轮）

**玩家原话**：「“目的地不该取链头格，而该取车底可挂接端格”是对的。C1,取「车体上离机车最近的一格」；C2暂时不做」。

| 编号 | 问题 | 拍板结果 |
|---|---|---|
| 方向 | 目的地取链头格还是可挂接端格 | **方向确认成立**：取「可挂接端格」 |
| C-1 | 具体取尾节格还是车体上离机车最近的一格 | **取「车体上离机车最近的一格」**（遍历配对车底全链，按 Chebyshev 距离最小取格） |
| C-2 | 是否顺带把返回值限定「必须与机车同站台」 | **暂不做** |

**落点**：`src\order_cmd.cpp` 的 `R3RGetCoupleOrderDestination()`（第 345 轮新增，`4607-4628`）。这是唯一改动点；`UpdateOrderDest()` 的 `couple_dest != INVALID_TILE ? couple_dest : station_dest` 回退结构保持不变。

**保留的旧行为（逐字不变）**：
- `v->type != Train` ⇒ `INVALID_TILE`；
- 无配对伙伴（`R3RGetCouplePairPartner()` 返回 nullptr）⇒ `INVALID_TILE`；
- 全链无任何可用格 ⇒ 回退链头格，再做 tile 类型校验后返回（即旧逻辑的完整副本作第三档）。

**未做**：C-2（同站台限定）；未动 `R3RGetCouplePairPartner()` 语义；未动挂接寻路的可挂端判据（`R3RIsCoupleTarget` 仍只认段头/链头为可挂对象——**停车点**不必是链头格，这是本次修法的前提，两件事不矛盾）。

## 8.7 落地实现与构建自证（第 346 轮）

### 8.7.1 改动（唯一文件 `src\order_cmd.cpp`，未碰任何 `src\*.h`）

`R3RGetCoupleOrderDestination()` 函数体重写，三档取值：

1. **首选（新增）**：从配对车底链头起沿 `Vehicle::Next()` 遍历**全链每一节**（含铰接部件，它们的 `tile` 即父车格），跳过 `!IsValidTile` 与非 `Railway/Station/TunnelBridge` 格，按 **Chebyshev 距离**（`max(|dx|,|dy|)`）取离机车 `v->tile` 最近的一格。用 `const Vehicle *` 作循环变量，避免 `Train::From()` 下转型与 `Next()` 的 const 重载问题。
2. **回退**：全链无可用格 ⇒ 退回链头格并做同样的 tile 类型校验（**逐字等于第 345 轮旧行为**）。
3. `v->type != Train` / 无配对伙伴 ⇒ `INVALID_TILE`（不变）。

`UpdateOrderDest()` 的 `couple_dest != INVALID_TILE ? couple_dest : station_dest` 结构未动，`R3RGetCouplePairPartner()` 未动，C-2（同站台限定）按拍板**未做**。

刻意不引入 `<algorithm>`/`<cstdlib>`（`std::abs`/`std::max`/`INT_MAX` 在本文件无先例），绝对值与取大全部手写，保持**.cpp-only 增量合规**（KI-183 护栏）。

### 8.7.2 构建自证

```
GUARD: incremental is safe (no header/lang file is newer than the newest object)
[1/5] FindVersion.cmake → Version string: r3r-pre-ledger-removal-2026-10-25-m, Release: 0.73.1
[2/3] Building CXX object CMakeFiles\openttd_lib.dir\src\order_cmd.cpp.obj
[3/3] Linking CXX executable openttd.exe
build\R3R_incbuild.done = EXIT_CODE=0
```

| 产物 | 时间 |
|---|---|
| `src\order_cmd.cpp` | 2026-10-28 13:22:33 |
| `build\openttd.exe` | 2026-10-28 13:25:28（52 344 832 B） |
| `build\R3R_incbuild.log` | 2026-10-28 13:25:30 |

`read_lints` 0 条。本轮未新增日志字面量，故不做 exe 串核对。

### 8.7.3 复测判据（待玩家实机）

1. 同场景 `TRP veh=57` 的 `dest` 应从 `1932`（(12,15)）变为**车体最近格**——按 §8 读数约在 `(5,15)` 一带（`1932 - 7*128 = 1036` 之类的换算），且 `dt` 从 33 降到个位数；
2. `CPL-PAIRMAP` 中 57 的轨迹应沿 y=15 股道推进，**不再出现** `13,14 → 2,14 → 2,15` 的整条股道绕行；
3. 出现 `COUPLE-OK`；
4. 负判据：非 `OT_GOTO_COUPLE` 订单、车库挂接（`GetCoupleIsDepot()` 分支）逐字不变；
5. 负判据：无配对伙伴时 `dest` 仍回落站台锚点（`UpdateOrderDest` 的 `station_dest`）；
6. KI-509 / KI-359 P1 / KI-357 / KI-511 判据不回退。

### 8.7.4 已知风险

- 遍历 33 节 × 每 tick 一次（`UpdateOrderDest` 亦被 PBS 前瞻调用）⇒ 几十次 tile 读与类型判定，量级可忽略，但**未做节流**；
- 「最近格」不保证可停靠（弯道/道岔上的车节），若实测出现机车停在相邻格反复 `COUPLE-FAIL`，则需把 C-1 升级为「最近且可停靠」（即把 C-2 的同站台约束或 `TrainFitStation()` 拉回来）；
- 车底被部分落库 / 跨桥时，最近格可能落在隧道桥格，`DoTrainPathfind` 对 `TunnelBridge` 的处理沿用旧逻辑，未额外收窄。

## 8.8 玩家二次复跑反馈（2026-10-28，C-1 已落地后仍走错股道）

**玩家原话**：「机车还是走错股道。但是，请你注意，在这个场景对于成功配对的列车，只要他们撞上，就必然能够成功耦合，所以问题一定出在寻路上面」。

### 8.8.1 这条前提的推论（先摆清楚，避免再跑偏）

- 「撞上就必然耦合」⇒ **命中/拼接判据不是瓶颈**，`FOLDCHK` / `SPLICE-GAP` / `COUPLE-PREFLIGHT` 等一律不可能是本次主因；只要机车**物理到达**车底所在股道并碰到车底，就一定有 `COUPLE-OK`。
- ⇒ 失败点是**机车根本没走到车底那条股道**，即 **PBS / 普通寻路的选路结果**，不是订单目的地、不是命中判据、不是拼接判据。
- ⇒ 第 346 轮 C-1（目的地取车体最近格）即便生效，也只解决「目标格被车底自己占着」，**不解决「两条平行股道之间选哪条」**。这正是现象未变的原因方向。

### 8.8.2 待验证清单（V1~V4）

| 编号 | 待验证 | 判据 |
|---|---|---|
| V1 | 新 exe 是否真的在跑 | 日志首行/时间戳对应 `build\openttd.exe` @2026-10-28 13:25:28（52 344 832 B） |
| V2 | `dest_tile` 是否已变成车体最近格 | `TRP veh=57 ... dest=` 应从 `1932`(12,15) 变为 `(5,15)` 一带（约 1036） |
| V3 | 机车实际轨迹沿哪条股道 | `CPL-PAIRMAP veh=57` 逐行 tile 序列 + `SPD-DROP veh=57 ... dt=` |
| V4 | 寻路在哪一步选了错股道 | `CPL-HEAD` / `CPL-BEST` / `CPL-TRACE` / `RP cur=` / `CPL-PF-FALLBACK` 的 dest/final/tracks |

**猜测根因（G-5，待验证）**：`DoTrainPathfind()` 的终点是「车体最近格」，而该格仍在 y=15 股道的**车底占位走廊**里；PBS 前瞻判定该终点不可达/代价过高后回退普通寻路（`CPL-PF-FALLBACK`），普通寻路在**两条平行股道**之间按代价择优——y=15 被车底自身占满 ⇒ 它选 y=14 ⇒ 走错股道。若成立，则修法不在目的地取值，而在**「配对车底自身占位应视作可通行/可穿越」**或**「拿到可挂端后直接沿该股道接近，不走全图最优」**。

### 8.8.3 读数（新 exe 13:25:28，日志 13:29:42 / 260 686 B）

**V1 通过**：日志晚于新 exe。

**V2 结论：`dest` 仍是 `1932`(12,15)，C-1 修法在数学上等价于旧行为。** 原文：

```
1243:TRP veh=57 mf=16,14 real=1 type=16 dest=1669 ... => ok=0 res=0
1340:TRP veh=57 mf=16,14 real=1 type=16 dest=1932 ... => ok=1 res=1
```

（`1669 = 13*128+5 = (5,13)` 是更早一次采样，`1932=(12,15)` 是 57 真正上路后的值。）

原因：`veh=0` 车体 33 节，**链头 `(12,15)` 本身就是车体上离机车最近的一节**（车体自 12,15 向西铺开）。⇒ 我的「遍历全链取 Chebyshev 最近格」选出来的就是链头格，与第 345 轮旧值逐字相同 ⇒ **C-1 白改**。这是本轮最重要的纠错。

**V3 轨迹（`LOCO-MOVE veh=57`，逐格）**：

```
16,14 → 15,14 → 14,14 → 13,14 → 12,14 → 11,14 → 10,14 → 9,14（spd 100→79→49→41→5 停）
全程 db=1（倒车态），dtile 恒 = 1932
```

⇒ 机车沿 **y=14 股道一路西行**，在 `(9,14)` 刹停，**从未切到 y=15**。

**V4 挂接寻路的真实终点**：

```
1330:CPL-HEAD best=5,15 head=5,15 on=1
1331:CPL-BEST veh=57 tile=5,15 td=0 cost=11055
1336:CPL-PF-FALLBACK veh=57 tile=15,14 ret=0x0 tracks=0x9 dest=5,14 final=12,15
```

⇒ 挂接寻路自己认为可挂端是 **`(5,15)`（车底西端）**，并且找到代价 11055 的路径；但订单 `dest_tile` 是 `(12,15)`（车底东端/链头）。**两个终点不一致**：一个是"能挂上的地方"，一个是"车底自己站着的地方"。

**新的疑点（REG-346-A）**：57 在 `(9,14)` 停住时 `spd=5`、`stuck=0`、`res=1`（预留成功过），说明它不是被挡死，而是**主动刹停**（或预留到尽头）。是谁让它停，尚未取数 ⇒ 下一步看 `CPL-S0-CHK` / `SPD-DROP veh=57`。

**猜测根因 G-6（待验证）**：`(12,15)` 被车底自身占据，路径永远"差一点到不了"；PBS/普通寻路在 y=14 与 y=15 两条平行股道之间按代价择优，选了 y=14，而 y=14 股道与 y=15 的连接道岔在 **x=2** 附近（旧轨迹 `2,14 → 2,15`）⇒ 它本应沿 y=14 走到 x=2 再折回，但中途在 `(9,14)` 就停了（原因待查）。⇒ **修法方向改为：`OT_GOTO_COUPLE` 的目的地取「挂接寻路自己算出的可挂端」（`CPL-HEAD best`），而不是"离机车最近的一格"。** 这与玩家 C-1 的原意相反，需重新拍板。

### 8.8.4 真因锁定：`CPL-SAFE-FAIL` 否决了整条挂接路径（G-6 证伪）

原文（`read_file` 1322-1341，同一 tick 12460 的连续 15 行）：

```
1328:CPL-PATHFOUND veh=57 found=1
1329:CPL-SCAN tick=12460 veh=57 found=1 closed=3 limit=10000 nodes=17 enter=14 noplat=5 nores=0 noconsist=6 consist=3 rej=0/0/0/0 acc=3/0 accwp=0 last=5,15 lastveh=0 why=acc-car
1330:CPL-HEAD best=5,15 head=5,15 on=1
1331:CPL-BEST veh=57 tile=5,15 td=0 cost=11055
1332:CPL-BACK pPrev=5,15 td=0 parent=15,15 ptd=2
1333:FSCP tile=13,15 fail=notSeg best=54 ord=6 sf=1
1334:CPL-SAFE-FAIL veh=57 tile=5,15
1335:CRT veh=57 order=16 dir=5 origin=16,14 td=0 found=1
1336:CPL-PF-FALLBACK veh=57 tile=15,14 ret=0x0 tracks=0x9 dest=5,14 final=12,15
1337:CPL-RET veh=57 tick=12460 tile=15,14 enterdir=0 tracks=0x9 res=0x1 ret=0x1 why=cpl-partial-res spd=0 real=1
1340:TRP veh=57 mf=16,14 real=1 type=16 dest=1932 spd=0 fp=0 look=1 one=0 mstuck=0 fto=1 => ok=1 res=1
```

**因果链（本轮结论）**：

1. 挂接寻路**成功**找到通往可挂端 `(5,15)` 的路径（`CPL-PATHFOUND found=1`，`cost=11055`）。
2. 回溯时安全位检查在 **`(13,15)`** 失败：`FSCP tile=13,15 fail=notSeg best=54 ord=6 sf=1` —— 该格上最近的车是 **veh=54**，它的订单是 `ord=6`（`OT_GOTO_WAYPOINT`），**不是 R3R 段/配对对象**（`notSeg`）。
3. ⇒ `CPL-SAFE-FAIL` ⇒ **整条挂接路径作废**。
4. ⇒ 退回普通寻路 `CPL-PF-FALLBACK`，其终点 `final=12,15` 正是**车底自己占着的格**（永远不可达）⇒ 只能预留出 `why=cpl-partial-res` 的部分路径（`res=0x1`）。
5. ⇒ 机车沿着这条"部分预留"的路径跑：沿 **y=14**（`track=01`）向西 `16,14 → 9,14`，预留到头 ⇒ `decel2=240 realbrk=1` 刹停在 `(9,14)`（`spd=5`，`gap=56` 递减），全程再没重新预留（`TRP veh=57` 只有 3 条）。

⇒ `(13,15)` 是 y=15 股道从东侧接近车底的**必经格**，被 veh=54（路点订单，非挂接对象）占着/预留中。**机车走 y=14 并不是"选路偏好"，而是挂接路径被安全位判据整条否决后的降级结果。**

**G-6 证伪**：不是"平行股道择优"，也不是"目的地取值"；`(9,14)` 刹停是部分预留走到了尽头。

**修复方向（待拍板，编号 C-3/C-4）**：

- **C-3（推荐）**：`FSCP fail=notSeg` 不应否决**整条**挂接路径。当失败格只是「第三方非挂接车辆的途经占位」时，正确行为是**保留挂接路径并在该格前减速等待**（它本来就会被 PBS 挡住），而不是丢弃路径退回一条通向不可达终点的普通路径。
- **C-4**：退一步的兜底——`CPL-PF-FALLBACK` 的终点不该用「车底格」，而应用**挂接路径算出的可挂端**（`CPL-HEAD best`），至少让降级路径也指向能挂上的地方。

### 8.8.5 为什么 `notSeg` 的否决是错的（`FSCP` 代码取证）

`src\pathfinder\yapf\yapf_rail.cpp` 的 `FindSafeCouplePositionProc()`：

```157:210:src/pathfinder/yapf/yapf_rail.cpp
	bool FindSafeCouplePositionProc(TileIndex tile, Trackdir td)
	{
		...
		if (HasReservedTracks(tile, tracks)) {
			... /* 收集该格上最近的车 best / second_best */
			if (best != nullptr) {
				if (best->index == Yapf().GetVehicle()->index) return true;
				if (!R3RIsCoupleTarget(best)) {
					fprintf(dbg, "FSCP tile=%d,%d fail=notSeg best=%d ord=%d sf=%d\n", ...);
					return false;
				}
```

- 该函数是回溯路径上的**逐格安全位判据**：返回 `false` ⇒ `CheckSafePositionOnNode` 失败 ⇒ **整条挂接路径作废**。
- `(13,15)` 上是 **veh=54**（`ord=6` = `OT_GOTO_WAYPOINT`，`sf=1`），`R3RIsCoupleTarget(54)` 为假 ⇒ `fail=notSeg` ⇒ 返回 false。
- 但 `CPL-BACK pPrev=5,15 td=0 parent=15,15 ptd=2` 说明挂接路径是 **`(15,15)` 沿 y=15 股道向西直达 `(5,15)`**——**穿过的正是车底自己的车身**（车底占位因是挂接目标而被放行）。也就是说：**只要不被 54 否决，这条路会直接让机车沿 y=15 撞上车底**，与玩家「撞上就必然耦合」的判断完全吻合。
- veh=54 是**路过**的活车（`origin=16,15 → 2,15`，日志末尾已 `DEPOT-ARR tx=18 ty=16 dest=1,15` 进库）。它一会儿就走开 ⇒ 因为一个**临时**障碍把整条正确路径扔掉，是本轮真正该修的地方。

### 8.9 候选修法（第 346 轮续，**未改码，待拍板**）

三个候选都指向同一句话：**挂接路径已经算对了（沿 y=15 直达车底），却被一个临时障碍否决**。

#### C-3（治根，推荐）：回溯遇到第三方车格时「截断」而不是「作废」

- 落点：`src\pathfinder\yapf\yapf_rail.cpp` 的 `TryReservePath()` 回溯循环（`while (pNode->parent != nullptr)` 内 `CheckSafePositionOnNode(pPrev)` 失败处）。
- 现状：`return INVALID_TRACKDIR` ⇒ 整条挂接路径丢弃 ⇒ 上层掉进第 344 轮降级分支。
- 改法：失败时**保留已走过的那段**——把预留目标改设为「失败格的起点侧节点」并按其 trackdir 走完剩余预留，等价于「挂接到障碍前的最后一个格（含站台内）」。若失败发生在第一跳（`r3r_hops <= 1`，机车侧没有可退的格），保持现状返回 `INVALID_TRACKDIR`，行为与改前逐字相同。
- 依据：`TryReservePath()` 本就有 `unsafe_pos` 语义（截断预留不要求目标格是合法等待位），截断不需要新判据。
- 效果：本案机车从 `(16,14)` 走到 `(15,14) → (15,15) → (14,15)` 停住等 veh=54 走开，下一 tick 重新算出完整挂接路径 ⇒ 沿 y=15 撞上车底 ⇒ `COUPLE-OK`。
- 风险：位于 desync 敏感区（YAPF 预留），需保证截断点仍在机车自己链上、且截断后 `chosen_track` 满足 20024 的采纳门（第 335 轮已有同构守卫）。

#### C-4（兜底）：降级分支的目标改指「可挂端」

- 落点：`src\train_cmd.cpp:17624-17651` 的 `CPL-PF-FALLBACK` 块。
- 现状：`DoTrainPathfind(consist, tile, ...)` 用的是 `consist->dest_tile = (12,15)`（被车底自己占着，普通寻路永不可达）⇒ 只预留到 `(5,14)`，**那股道是 y=14，与车底平行的另一条股道** ⇒ 玩家看到的「走错股道」。
- 改法：降级前临时把目标换成**挂接路径算出的可挂端**（`CPL-HEAD best`，本案 `(5,15)`；或车底面朝机车那一端外侧的进近格），寻路完恢复。需要 `CYapfCoupleRail` 把 `best` 格回传出来。
- 效果：降级路径也会走 y=15，最多停在 54 之前。
- 风险：改 YAPF 接口（加输出参数）；且普通寻路对「被车底占的终点格」仍然到不了，只能止步邻近格 ⇒ 需要靠「相邻即命中」的拼接判据收尾，**不保证一次成功**。

#### C-6（最小，但可能无效）：`FSCP` 的 `notSeg` 增加豁免

- 落点：`src\pathfinder\yapf\yapf_rail.cpp:157-210` 的 `if (!R3RIsCoupleTarget(best)) { ... return false; }`。
- 改法：`best` 不是挂接目标但位于**同一车站站台**时放行。
- 问题：放行后该格仍要 `TryReserveRailTrack`，别人的格订不下 ⇒ 会转成 `fail=tryReserve` 再次整条作废（第 344 轮日志里的历史签名）。**单独做大概率无效，须与 C-3 的截断合并**。
- 结论：C-6 不单列，作为 C-3 的补充说明。

#### 顺带：C-1 的去留

C-1（遍历车底全链取离机车最近的格）在本现场**选出的是链头格 `(12,15)`，与第 345 轮旧值逐字相同** ⇒ 无效但无害。建议**保留不回滚**：它在「车底链头在远端、最近格在近端」的几何下仍是对的，只是本案链头恰好在近端。回滚会丢掉这个正确性。

### 8.10 玩家拍板口径（2026-10-28 原话）：「不是，它自己的路径被挡住了那就等待啊，等到路径被放出来啊」

⇒ **不截断、不绕行、不降级，原地等**。据此把 C-3/C-4/C-6 全部作废，改取最小改动的 **C-7**：

- **判据**：`CPL-SAFE-FAIL`（`yapf_rail.cpp:1141-1151`）只在「路径已经找到（`found=1`）、但回溯时某格的安全位判据不通过（第三方车占位）」这一种情况下置位一个文件内标志 `_r3r_couple_path_blocked`；`found=0`（真的没路）**不置位**。
- **行为**：`train_cmd.cpp:17624` 的 `CPL-PF-FALLBACK` 降级分支追加条件 `&& !R3RCouplePathBlockedByThirdParty()`。被挡 ⇒ **不降级** ⇒ `path_found` 保持 `INVALID_TRACK` ⇒ 落回 KI-316 处置（原地停住 + `MarkTrainAsStuck`），下一 tick 由 `r3r_couple_stuck_retry`（`train_cmd.cpp:17078`）重算整条挂接路径。
- **为什么这样就够**：本案障碍 veh=54 是过路车（日志末尾已 `DEPOT-ARR` 进库）。它一走开，下一 tick 的 `CPL-PATHFOUND found=1` → 回溯不再碰它 → 机车沿 y=15 撞上车底 → 撞上即耦合（玩家已有结论）。
- **为什么不能沿用降级**：降级目标恒为 `consist->dest_tile=(12,15)`（被车底自己占着、普通寻路永不可达），只能订到 `(5,14)` —— **y=14 是另一条股道**，这就是「走错股道」的全部来源。不降级 = 不换股道。
- **风险与负判据**：①`found=0`（目标车底还没进等待位）仍走原降级，逐字不变；②真·永久死锁（障碍永不离开）会表现为原地停住而非乱走，这是玩家要的行为；③不碰 YAPF 搜索与预留语义，只加一个只读查询函数（跨文件用手写前向声明，不改任何 `src\*.h`）。

## 9. 状态

- 第 345 轮 A：**已生效**（`dest=5,14` 非 INVALID_TILE），但目标格与配对车底实际格不符 ⇒ Q-2。
- 第 345 轮 B：**未触发（死路径）**，本现场 0 次。
- 第 346 轮 C-1：**已实现已编译，复测证伪（与旧值逐字相同）**，但代码保留。
- 第 346 轮续：真因锁定为 `CPL-SAFE-FAIL`（`FSCP fail=notSeg best=54`），候选 C-3/C-4/C-6 见 §8.9，**等玩家拍板，尚未改任何源码**。
