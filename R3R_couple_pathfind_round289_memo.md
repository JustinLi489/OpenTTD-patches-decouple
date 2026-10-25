# 第 289 轮：等待区路点停住之后，挂接寻路 found=0（临时分析报告）

## 一、玩家报告（原话，2026-10-23）

> 「列车的速度成功的压到了0，那么现在，就是挂接寻路的问题了」

外带日志 `D:\sourcecode of JGRPP\build\R3R_debug.log`。

**⇒ 第 288 轮（停车标同源 + 收尾压到 5 + 撤兜底）的正判据 1 已达成**：速度被压到 0，且日志里不再出现 `WP-NOARRIVE` / `WP-STOP-ADVANCE`（探针与兜底已删）。本轮转入挂接寻路。

## 二、采样有效性

| 项 | 读数 |
|---|---|
| 日志 | `build\R3R_debug.log`，1 320 行 / 193 691 B / mtime 2026-10-23 20:52:14 |
| 产生者 | `build\openttd.exe` 2026-10-23 20:42:59（52 275 712 B）= **第 288 轮那次增量构建** |

⇒ 日志晚于 exe，采样有效；本轮现象由第 288 轮改动直接产生，无陈旧可执行体干扰。

## 三、现场读数（行号锚点，原文摘录）

### 3.1 机车 21 已停在路点、速度 0

```
894  CPL-ENTRY veh=21 tile=249,233 orderType=16 dontReserve=0
895  CPL-ORIGIN veh=21 origin=249,233 td=8 vehDir=1 vehTd=8 dontReserve=0
897  CPL-PATHFOUND veh=21 found=0
...
1176 CPL-ENTRY veh=21 tile=250,233 orderType=16 dontReserve=0
1177 CPL-ORIGIN veh=21 origin=250,233 td=8 vehDir=1 vehTd=8 dontReserve=0
1178 CPL-PATHFOUND veh=21 found=0
1185 COUPLE-FAIL loco=21 order=16 tx=250 ty=233 x=4001 y=3736
```

### 3.2 尾部稳态（每 ~2 tick 一组，同一组读数逐字重复）

```
COUPLE-WAIT-OFFDEST veh=21 tile=250,233 mf=255,233 real=12 isDepot=0 destDepot=0 destStn=12 destTile=479497 onDepotTile=0 onStnTile=0
TPR-WALK veh=21 mf=255,233 origin=255,233 td=8 okay=1 exit=2 new=256,233 reach=1 spd=0 one=0 look=0
CPL-SKIP veh=21 tile=250,233 orderType=16 dontReserve=0 retryIn=2 holdAge=6
TRP veh=21 mf=255,233 real=12 type=16 dest=479497 spd=0 fp=0 look=0 one=0 mstuck=0 fto=0 => ok=0 res=0
[R3R] CPL-PAIR tick=3016 act=21 tgt=24 dist=16 actTile=250,233 tgtTile=265,234
COUPLE-FAIL loco=21 order=16 tx=250 ty=233 x=4001 y=3736
```

（`CPL-PAIR` 的 tick 为 3016 / 3018，dist=16 恒定，`tgtTile=265,234` 不变 ⇒ 目标车底 24 原地等待。）

### 3.3 数值汇总

| 量 | 读数 | 判读 |
|---|---|---|
| 机车 21 `spd` | **0** | 第 288 轮目标达成：停住了，没再带 15 冲过路点 |
| 机车 21 `tile` | 250,233（x=4001,y=3736） | 停在等待区路点上 |
| 机车 21 运动前端 `mf` | 255,233 | 与车头差 5 格 ⇒ 这是一条长链（`db`/链序使运动前端远离链头） |
| 订单 | `order=16` = `OT_GOTO_COUPLE`，`real=12` | 等待区路点已完成，已推进到挂接单 |
| `destStn=12` / `destTile=479497` | 与目标车底所在站一致（需核对） | — |
| `TRP ... ok=0 res=0` | 预留失败 | 与 found=0 一致 |
| `CPL-PATHFOUND veh=21 found=0` | **挂接寻路找不到路径** | 本轮主症状 |
| `CPL-PAIR dist=16` | 恒定 | 机车不再靠近目标车底 |
| `mstuck=0` | 未被判 stuck | 说明不是"卡住"路径，而是"每次都重新尝试寻路并失败" |

### 3.4 `CPL-SCAN` 清点（决定性读数，Q-289-A 解答）

```
897  CPL-PATHFOUND veh=21 found=0
898  CPL-SCAN tick=3001 veh=21 found=0 closed=82 limit=10000 nodes=308 enter=226
     noplat=150 nores=6 noconsist=67 consist=3 rej=3/0/0/0 acc=0/0 accwp=0
     last=278,234 lastveh=24 why=GROUP
1178 CPL-PATHFOUND veh=21 found=0
1179 CPL-SCAN tick=3009 veh=21 found=0 closed=82 limit=10000 nodes=308 enter=226
     noplat=150 nores=6 noconsist=67 consist=3 rej=3/0/0/0 acc=0/0 accwp=0
     last=278,234 lastveh=24 why=GROUP
```

判读（两次采样逐字相同 ⇒ 完全确定性的稳定态，不是随机/时序问题）：

| 字段 | 读数 | 含义 |
|---|---|---|
| `closed=82` / `limit=10000` | 远未触上限 | **不是**搜索被截断、不是节点爆炸 |
| `enter=226` | 展开过 226 个节点 | 搜索确实跑了，且覆盖范围不小 |
| `noplat=150` | 150 个节点"不在站台" | 大部分轨道不是站台格（正常） |
| `nores=6` | 6 个节点预留被拒 | 少量 |
| `noconsist=67` | 67 个节点上没有等待车底 | 正常 |
| **`consist=3`** | **3 个节点命中了"等待中的车底"** | ⇒ **等待链确实在场**，不是"目标不在场"(a) |
| **`rej=3/0/0/0`** | **GROUP=3 / FIT=0 / TARGET=0 / ORDER=0** | ⇒ **3 个候选全部被 GROUP 闸门拒掉** |
| `acc=0/0`、`accwp=0` | 零接受 | ⇒ 因此 `found=0` |
| `last=278,234 lastveh=24 why=GROUP` | 最后一次被拒：格 278,234、车 24、原因 GROUP | 与 `CPL-PAIR tgt=24` 同一辆车 |

**⇒ Q-289-A 解答：走的是 (b)「寻路跑了、候选在场、被闸门拒」，闸门唯一是 `GROUP`（挂接分组）。**
`noplat/nores/noconsist` 都只是沿途清点，`consist=3` 与 `rej GROUP=3` 一一对应 ⇒ 每一个找到的等待车底都被 GROUP 挡下。
注意 `last=278,234` 与 `CPL-PAIR tgtTile=265,234` 不同：车底 24 是一条多节链，扫描命中的是链上伸到 278,234 的那一节（`lastveh=24` 是链头车号）。

### 3.5 闸门定位（代码链，已读原文）

`rej=3/0/0/0` 的第 0 槽来自 `yapf_destrail.hpp` 的：

```560:609:src/pathfinder/yapf/yapf_destrail.hpp
		auto r3r_dest_reject = [](int why, TileIndex tile, const Train *t) { ... ++_r3r_cpl_scan_rej[why]; ... };
		if (!R3RCoupleAllowed(Train::From(Yapf().GetVehicle()), t)) {
			r3r_dest_reject(0, tile, t);
			return false;
		}
```

而 `R3RCoupleAllowed()`（`couple_group.cpp:604-608`）只有两步：

```604:608:src/couple_group.cpp
bool R3RCoupleAllowed(const Train *coupler, const Train *target)
{
	if (!R3RCouplePairMatches(coupler, target)) return false;
	return R3RCoupleAllowedIgnoringPair(coupler, target);
}
```

⇒ GROUP 拒绝的**唯一三个子原因**：
1. `pair-mismatch`：`coupler->First()->r3r_couple_target != target->First()->index` 或反向 requester 不匹配（`couple_group.cpp:528-535`）。
2. `dest-mismatch`：候选停在**别的车站**（`couple_group.cpp:409`：`IsRailStationTile(target->tile) && GetStationIndex(target->tile) != dest.ToStationID()`）。
3. `group-mismatch`：**第 224 轮 KI-336 拍板 ② 起分组白名单无条件生效**（`couple_group.cpp:463-464`）——同公司与跨公司都必须"相同挂接分组"；跨公司另需组对外开放。

**已排除的线索**：日志里 `CG-GATE` **零命中**。`CG-GATE`（`train_cmd.cpp:13034` 一带）只在 `R3RCoupleAllowedIgnoringPair()` 的同公司分支里打；跨公司分支直接 `return R3RCoupleGroupMasksAllowCrossCompany(...)` 不打。所以零命中只说明「不是同公司走到分组判据那一步」，**不能**据此断定不是分组问题——跨公司失败同样零命中。

**关键反证（不利 pair-mismatch）**：扫描层 `TrainCoupleHandler`（`train_cmd.cpp:13154-13206`）用的是 **`R3RCoupleAllowedIgnoringPair`**（不含 pair 检查），建锁成功后打 `CPL-PAIR`。日志里 `CPL-PAIR act=21 tgt=24` 反复出现 ⇒ 锁被成功建立/重建过 ⇒ 若锁未被清，yapf 侧的 `R3RCouplePairMatches` 应通过。**⇒ 见 §3.6：dest/group 已被排除，锁定 `pair-mismatch`。**

**新疑点 Q-289-E（重要）**：`CPL-SCAN ... last=278,234 lastveh=24` 与 `CPL-PAIR tgtTile=265,234` 对「链头 24 在哪一格」互相矛盾。两者都声称是链头车号 24，却给了两个格。可能解释：
- (i) `_r3r_cpl_scan_last_veh` 记的 `t->index` 是链中节、但 `lastveh` 显示的是链头号（探针已归一）而 tile 是那一节的格 ⇒ 链头 24 在 265,234，链尾伸到 278,234（13 节长链）；
- (ii) 采样时刻不同，车底在此期间移动；
- (iii) 车辆 index 复用（24 被释放后新链占用）。
若 (i) 成立，则**车底链跨了两个站台**：265,234 属车站 12（订单目的地、扫描层通过），278,234 属别的车站 ⇒ yapf 在 278,234 那一节上判 `dest-mismatch`。这恰好能解释「扫描层通过、寻路层全拒、rej 全是第 0 槽」。

### 3.6 决定性推理：yapf 侧唯一失败项 = `pair-mismatch`

扫描层 `TrainCoupleHandler()` 过闸的顺序（`train_cmd.cpp`）：

```13134:13206:src/train_cmd.cpp
		if (t->First() != t || t->First() == coupler->First()) { R3RClearStaleWaitMarker(t); continue; }
		if (!R3RIsCoupleTarget(t)) continue;
		if (t->vehstatus.Test(VehState::Stopped)) continue;
		if (!R3RCoupleTargetAtOrderStation(coupler, t)) continue;     // ← dest
		Train *locker = R3RGetCouplePairPartner(t);
		if (locker != nullptr) { if (!R3RCouplePairOutranks(coupler, locker)) continue; }
		if (!R3RCoupleAllowedIgnoringPair(coupler, t)) continue;        // ← dest + group（不含 pair）
		...
	if (!R3RPairCoupleTargets(coupler, best)) { ... return; }
	... CPL-PAIR tick=... act=21 tgt=24 dist=16 actTile=250,233 tgtTile=265,234
```

`CPL-PAIR act=21 tgt=24` 出现 3 次（tick 3015 / 3016 / 3018，`dist=16`、`tgtTile=265,234` 逐字相同）⇒ 扫描层里
**`R3RCoupleTargetAtOrderStation()` 通过、`R3RCoupleAllowedIgnoringPair()` 也通过** ⇒ dest（车站 12）与 group（分组白名单／跨公司授权）在扫描层已成立。

yapf 层与扫描层对的是同一对车、同一时刻、同一函数族，唯一差别是 yapf 用的是**含 pair 检查**的版本：

```604:608:src/couple_group.cpp
bool R3RCoupleAllowed(const Train *coupler, const Train *target)
{
	if (!R3RCouplePairMatches(coupler, target)) return false;
	return R3RCoupleAllowedIgnoringPair(coupler, target);
}
```

⇒ `rej[0]=3` 只能落在 `R3RCouplePairMatches()` 上：

```528:535:src/couple_group.cpp
bool R3RCouplePairMatches(const Train *coupler, const Train *target)
{
	...
	return c->r3r_couple_target == t->index && t->r3r_couple_requester == c->index;
}
```

**结论（本轮定性）：`CPL-PATHFOUND found=0` 的真因是「寻路那一刻配对锁不成立」，即 `pair-mismatch`；不是等待链不在场，也不是分组/目的地被拒、不是搜索被截断（`closed=82 « limit=10000`）。**

补注（避免误读）：
- `CPL-PAIR` 重打 3 次**不能**证明"锁每 tick 被清又被重建"——`R3RDbgEdge` 是 KI-14 的窗口/状态节流，payload 相同也会在窗口到期后重打（`dist` 恒 16 却仍重打正是这个原因）。
- 因此"锁为什么在 yapf 侧不成立"还必须查：①`R3RPairCoupleTargets()` 之后锁被谁清（`R3RUnpairCoupleTargets()` 的**全部**调用点、`R3RClearStaleWaitMarker()`、链头重排时 `r3r_couple_target/requester` 是否随 `First()` 迁移）；②同 tick 内 yapf `ChooseTrainTrack` 与 `TrainCoupleHandler` 的先后顺序（若寻路先跑，则用的是上一 tick 的锁）；③`Yapf().GetVehicle()` 是否真是链头 21（若 21 已是某链的中间节，`First()` 归一后读的是另一辆车的字段）。

### 3.7 「锁为什么会不在」——首要机制嫌疑（已读原文）

`R3REnsureCouplePair()` 每 tick 由 `TrainLocoHandler()` 调用一次，它既建锁也**清锁**：

```13099:13109:src/train_cmd.cpp
	Train *partner = R3RGetCouplePairPartner(coupler);
	if (partner != nullptr && partner->First() != coupler->First() && R3RIsCoupleTarget(partner) &&
			!partner->vehstatus.Test(VehState::Stopped) &&
			R3RCoupleAllowedIgnoringPair(coupler, partner)) {
		_r3r_pair_scan_tick.erase(coupler->index.base());
		return;                                   // ← 稳态：保持锁
	}
	if (partner != nullptr) R3RUnpairCoupleTargets(coupler);   // ← 否则「清锁」
```

重扫时的候选过滤同样是：

```13138:13139:src/train_cmd.cpp
		if (!R3RIsCoupleTarget(t)) continue;
		if (t->vehstatus.Test(VehState::Stopped)) continue;     // ← 停着的车底被跳过
```

⇒ **两处都要求「等待车底没有 `VehState::Stopped`」**。而 `R3RIsCoupleTarget()`（`train_cmd.cpp:2336-2351`）本身**不含**任何速度/停驶判据，只看
`t->First()==t`、`!R3RChainHasRealArticPart(t)`、`current_order.IsType(OT_WAIT_COUPLE)`。也就是说「停驶」这一条是**额外加上去的**。

**推论链**：等待车底一旦停稳（`Stopped` 置位）→ 稳态判据失败 → `R3RUnpairCoupleTargets(coupler)` **清锁** → 重扫又因同一原因跳过该车底 → **锁永久为空** → yapf 侧 `R3RCouplePairMatches()` 必假 → `rej[0]++`、`found=0`、且**正好**不打 `CG-GATE`（pair 检查在 `R3RCoupleAllowedIgnoringPair` 之前返回）。这与 §3.4/§3.6 的全部读数一致。

**与 `CPL-PAIR` 出现 3 次的关系**：`CPL-PAIR` 在 tick 3015/3016/3018（`tgtTile=265,234`，三次逐字相同），而 `CPL-SCAN` 在更早的 tick 3001/3009 就已 `rej=3`。若上面的推论成立，则「24 在扫描那一刻刚好没有 Stopped 标志（还在滑行/刚被解挂）」时才建得起锁，一旦停稳就再建不起来 —— 这正好解释为何 `CPL-PAIR` 只在某一小段 tick 出现、之后只剩 `CPL-PATHFOUND found=0`。**但这仍是推断，必须用探针定死（见 §3.8）。**

### 3.8 下一步：一条只读探针（已设计，待落地）

在 `yapf_destrail.hpp` 的 `!R3RCoupleAllowed(...)` 分支里打印**三个子判据的真值**与双方的锁字段、停驶标志。判读表：

| `pairOK` | `destOK` | `groupOK` | 结论 |
|---|---|---|---|
| 0 | 1 | 1 | 锁不成立 ⇒ 查 §3.7 的清锁机制（`Stopped` 或 `Unpair` 时序） |
| 1 | 0 | 1 | 目的地被判到别的车站（`GetStationIndex(t->tile) != destStn`） |
| 1 | 1 | 0 | 分组白名单/跨公司授权（第 224 轮 KI-336 口径） |
| 0 | 0 | 0 | 状态被别的路径改过（先查同 tick 时序） |

同时打印 `ch->r3r_couple_target` / `th->r3r_couple_requester`（-1 = `Invalid` 即空锁），并把双方 `Stopped` 一起打出来 —— 这两列直接检验 §3.7。

**落地状态（2026-10-23，本次）**：探针已写入 `yapf_destrail.hpp` 的 GROUP 拒绝分支，标记 `CPL-WHY`，16 行/进程上限，只读。
实打字段：`node act tgt ch th pairTgt pairReq pairOK ignOK aStn tStn destStn isDepot aStop tStop n`。
`read_lints` 0 诊断。**待增量构建 + 玩家复跑取数**（`openttd.exe` 当时正在运行，需先关）。

复测判据（拿到 `CPL-WHY` 后一次定死）：
- `pairOK=0` ⇒ §3.7 机制成立（`aStop/tStop` 里应看到等待车底 `tStop=1`），修法方向=把「停驶」从「能否作为耦合目标」的判据里去掉（等待车底本来就该是停着的）；
- `pairOK=1 ignOK=0` 且 `tStn != destStn` ⇒ 目的地判据（车底停在别的车站/站台延伸段）；
- `pairOK=1 ignOK=0` 且 `tStn == destStn` ⇒ 分组白名单（第 224 轮 KI-336 口径）／跨公司授权。

### 3.9 探针到手：锁确实是空的（玩家判断正确），并证伪 §3.7

新日志（`D:\temp\codebuddy-dropped-files\7fd80827-...\R3R_debug.log`）里 `CPL-WHY` 已生效，两次采样逐字相同：

```
897 CPL-WHY node=278,234 act=21 tgt=24 ch=21 th=24 pairTgt=1048575 pairReq=1048575
    pairOK=0 ignOK=1 aStn=65535 tStn=65535 destStn=12 isDepot=0 aStop=0 tStop=0 n=1
900 CPL-PATHFOUND veh=21 found=0
901 CPL-SCAN tick=3001 veh=21 found=0 closed=82 limit=10000 nodes=308 enter=226
    noplat=150 nores=6 noconsist=67 consist=3 rej=3/0/0/0 acc=0/0 accwp=0 last=278,234 lastveh=24 why=GROUP
1286 [R3R] CPL-PAIR tick=3015 act=21 tgt=24 dist=16 actTile=250,233 tgtTile=265,234
```

判读（**铁证，不再是推断**）：

| 字段 | 读数 | 含义 |
|---|---|---|
| `pairTgt` = `pairReq` = **1048575** | `0xFFFFF` = `VEHICLE_ID_INVALID` | 机车 21 的 `r3r_couple_target` 与车底 24 的 `r3r_couple_requester` **都是空的** —— 两端一颗锁都没有 |
| `pairOK=0` | 空锁必然 | `R3RCouplePairMatches()` 假 |
| `ignOK=1` | 目的地 + 分组**同时通过** | 车站 12 对得上、分组白名单/跨公司授权都没问题 |
| `destStn=12` / `isDepot=0` | 订单目的地 = 车站 12 | 与 §3.4 的 `rej why=GROUP` 对齐 |
| `aStop=0` / `tStop=0` | **双方都没有 `VehState::Stopped`** | **§3.7 的「停稳被跳过」假设被证伪**，必须撤回 |

**⇒ 玩家问「是不是配对锁没锁上才挂不上」——是。** 寻路那一刻锁完全为空（两端 Invalid），而候选本身在 yapf 眼里是合格的（`ignOK=1`），所以 `rej[0]=3` 全部落在 `pair-mismatch` 这唯一一项上。§3.6 的定性成立，§3.7 的机制解释作废（原因不是停驶）。

**新的关键读数（决定下一步）**：`CPL-PAIR tick=3015` 成功建锁，而两次寻路是 tick **3001 / 3009（间隔正好 8）**，锁在 3009 时还是空的。
`R3REnsureCouplePair()` 里失败重扫的节流是 `R3R_PAIR_RESCAN_TICKS = 8`（`train_cmd.cpp:13116-13118`、失败时 `_r3r_pair_scan_tick[...] = _tick_counter`，`:13179/:13187`），而挂接寻路的重试是 `retryIn=2`（`CPL-SKIP`）。**建锁周期 8 tick、寻路周期 2 tick —— 锁天然「比寻路慢一拍」**。

⇒ 下一个待证点（二选一）：
- (P) 3001/3009 时 `R3RIsCoupleTarget(24)` 还是假（24 的订单尚未切到 `OT_WAIT_COUPLE`），所以 EnsurePair 的候选循环整个跳过它，直到 3015 才第一次有合法候选；
- (Q) 24 一直是合法候选，只是 8 tick 节流让 EnsurePair 在 3001/3009 直接 `return` 没扫。

判别只需一行只读探针：在 `R3REnsureCouplePair()` 的 `if (scan_it != ... ) return;` 之前/之后打 `PAIR-SCAN tick act throttled=0/1 pool=候选数 best=车号`。

### 3.10 玩家口径核对：锁**确实**建起来了，只是晚了 8 tick

玩家指出「挂接理论应成立、配对锁应当建立」。核对代码与日志：**这个判断是对的，锁真的建立了** ——

```
1286 [R3R] CPL-PAIR tick=3015 act=21 tgt=24 dist=16 actTile=250,233 tgtTile=265,234
```

`CPL-PAIR` 是 `R3REnsureCouplePair()` 在 `R3RPairCoupleTargets()` **成功返回之后**才打的（`train_cmd.cpp:13190-13206`）⇒ tick 3015 时 `r3r_couple_target/requester` 两端都已写入。

于是问题精确重述为**时序错位**，而不是"锁建不起来"：

| tick | 事件 | 锁状态 |
|---|---|---|
| ≤3007 | `R3REnsureCouplePair()` 扫了一次，没选出候选（原因待定，见 P/Q） | 空，且写入节流戳 `_r3r_pair_scan_tick=3007` |
| 3001 | yapf `FindNearestCoupleTrain` 跑到，`CPL-WHY pairTgt=pairReq=1048575` | **空** ⇒ `pairOK=0` ⇒ `rej[0]++`、`found=0` |
| 3007 → 3015 | 节流窗口 `R3R_PAIR_RESCAN_TICKS=8`：`3007 + 8 = 3015`，期间 `R3REnsureCouplePair()` 每次进去直接 `return`（`:13116-13118`） | **一直空** |
| 3009 | yapf 第二次跑到（`CPL-SCAN tick=3009`），锁仍空 | 空 ⇒ 第二次 `found=0` |
| 3015 | 节流到期，扫描成功，`CPL-PAIR` | **锁建立成功** ✅ |

**算术完全吻合**：失败戳 3007 + 节流 8 = 3015，正是建锁那一 tick。而 yapf 的重试周期是 `CPL-SKIP retryIn=2`（`:1289`），远快于 8 ⇒ **建锁周期 8 tick、寻路周期 2 tick，锁天然比寻路慢，中间必然出现 4 次空锁寻路。**

`COUPLE-WAIT-OFFDEST`（`:1284` 与 `:1287`）也证明机车一直在原地等（`spd=0`、`destStn=12` 稳定），不是跑错地方。

**日志缺口（决定修法的最后一步）**：这份日志在 `:1290 TRP ... ok=0 res=0` 处**截断**，3015 建锁之后再也没有 `CPL-PATHFOUND`。所以还差一个读数：

- 若 tick 3017/3019 出现 `CPL-PATHFOUND found=1` + `CPL-HIT`/`COUPLE-OK` ⇒ 全案定性为**「锁晚到 8 tick」**，修法 = 把失败重扫节流从 8 降到与 `retryIn` 对齐的 2（或首次失败后同 tick 允许一次立即重扫），**不需要动 yapf 的 pair 强制**；
- 若 3017 之后仍 `found=0` ⇒ 锁在建好之后又被人清掉，转查 `R3RUnpairCoupleTargets()` 的其余调用点。

### 3.11 追加采样（tick 3015 之后）：锁反复建成，但**车根本不动**

玩家跑了一阵后的日志（`build\R3R_debug.log`，实时写入那份）给出了 §3.10 缺的那一段：

```
1286 CPL-PAIR tick=3015 act=21 tgt=24
1291 CPL-PAIR tick=3016 act=21 tgt=24
1309 CPL-PAIR tick=3017 act=21 tgt=24
1327 CPL-PAIR tick=3019 act=21 tgt=24
1345 CPL-PAIR tick=3022 act=21 tgt=24
1363 CPL-PAIR tick=3024 act=21 tgt=24
1382 CPL-PAIR tick=3074 act=21 tgt=24
1400 CPL-PAIR tick=3149 act=21 tgt=24
```

**⇒ Q 已定死：锁能建、而且建了很多次。** §3.10 的"锁晚到 8 tick"只是 3001/3009 那两个采样点的现象，不是全貌。

但同一段里每 2 tick 重复的是**另一个闭环**：

```
CPL-PAIR        tick=3016 act=21 tgt=24          ← 锁建立
COUPLE-FAIL     loco=21 order=16 tx=250 ty=233 x=4001 y=3736   ← 挂接尝试失败，位置一动不动
COUPLE-WAIT-OFFDEST veh=21 tile=250,233 spd=0 destStn=12 destTile=479497
TPR-WALK        veh=21 mf=255,233 origin=255,233 td=8 okay=1 exit=2 new=256,233 reach=1
TRP             veh=21 mf=255,233 real=12 type=16 dest=479497 spd=0 ... => ok=0 res=0
```

三条硬读数：

1. **`CPL-PAIR` 每 2 tick 重打一次**（3015/3016/3017/3019/3022/3024…）⇒ 锁**建立了又被清掉**，稳态从未成立。清锁者不是 `Stopped`（`aStop=tStop=0`），而是 `TrainCoupleHandler` 搜索通道里那条"每次搜索通过都丢弃陈旧锁"（`:13956-13958` 注释与调用）。
2. **`CPL-PATHFOUND` 在 3015 之后一次都没有**，`CPL-WHY` 也没有 ⇒ yapf 的**挂接寻路（`FindNearestCoupleTrain`）在 3015 之后完全没再跑**，机车改走普通 `GOTO_COUPLE` 路径。
3. **`TRP ... => ok=0 res=0`**（`train_cmd.cpp:16462-16468`）：`ok` = `TPRRF_RESERVATION_OK` 未置位，`res` = `TPRRF_NONE`（`0`）—— **既没有拿到预留，也没有 `REVERSE_AT_SIGNAL` 等任何标志**。结合 `TPR-WALK reach=1 new=256,233`（第一步可行），说明失败点在 `ChooseTrainTrack(consist, 256,233, exitdir=2, reachable=1, CTTF_FORCE_RES)`：**从 256,233 往前 yapf 无路 / 拿不到预留**。

`COUPLE-FAIL` 的 `x=4001 y=3736` 在 3016/3017/3019/3022/3024 逐字不变 ⇒ **机车 21 一步没动**。

**⇒ 结论修正（第 289 轮最终形态）**：
- 「配对锁」问题**已解决**：锁能正常建立（`CPL-PAIR` 连续出现证明 `R3RPairCoupleTargets()` 成功）。
- 但锁建好也没用，因为**机车 21 到不了 265,234**：普通路径 yapf 在 256,233 之后无路（`res=0`），车原地空转，每 2 tick 建一次锁再被清一次。
- 「挂不上」的真正当前瓶颈 = **路径可达性/预留**，不再是配对层。

**下一个探针（只读）**：在 `R3RTryPathReserveCore()` 的 `ChooseTrainTrack()` 返回处打 `result.ctt_flags`（尤其 `CTTRF_NOPATH` / `CTTRF_LRV` / `CTTRF_RESERVATION_MADE` 三位）与 `GetLastSignalBlockedTile()` 之类，一次就能区分「真无路」还是「被别的车/信号挡住」。已知 `mstuck=0 fto=0 fp=0 look=0`，排除"强制继续"与"已有前瞻预留"两条。

### 3.12 新疑点（探针已落地并编译通过）：`pair_ready` 的「必须在站台」判据

读 `R3RShouldCutCoupleAtWaitPoint()` 的放行判据（`train_cmd.cpp:13444-13456`）：

```13444:13456:src/train_cmd.cpp
	bool pair_ready = false;
	Train *a = R3RGetCouplePairPartner(v);
	if (a != nullptr && a->cur_speed == 0 && IsRailStationTile(a->tile) && R3RIsCoupleTarget(a)) {
		pair_ready = true;
		for (const Train *w = a; w != nullptr; w = w->GetNextUnit()) {
			if (w->cur_speed != 0 || !IsRailStationTile(w->tile)
					|| GetStationIndex(w->tile) != GetStationIndex(a->tile)
					|| GetRailStationAxis(w->tile) != GetRailStationAxis(a->tile)) {
				pair_ready = false;
				break;
			}
		}
	}
```

**与日志对撞**：`CPL-WHY ... tStn=65535`（`tStn` = `IsRailStationTile(24->tile) ? GetStationIndex : 65535`）—— 配对对象 24 所在 `265,234` **不是车站格**。
⇒ `IsRailStationTile(a->tile)` 为假 ⇒ `pair_ready` 恒假 ⇒ 该函数按 13378-13384 的口径返回 **2（未就绪且已停在栅栏格 ⇒ 无合法终点，原地等）** ⇒ 调用方 `ChooseTrainTrack` 在 `r3r_wait_cut == 2` 时直接 `return`（`16197` 附近，KI-444 分支）⇒ **不撤预留也不前进**。

这与 §3.11 的读数完全吻合：
- `TRP ... => ok=0 res=0` 不是「无路」，而是 `wait_cut == 2` 的正常等待返回；
- `CPL-PAIR` 每 2 tick 重建（配对照常建立），但机车永远不迈步；
- `COUPLE-FAIL x=4001 y=3736` 逐字不变。

**注意这是语义冲突**：`TrainCoupleHandler` 侧的 `R3RCoupleTargetAtOrderStation()` 认可这辆车底（所以选它、上锁），而放行判据要求「整链停在同一站台」。两处对「可挂车底停在哪」的口径不一致。

**探针（已落地，`read_lints` 0，增量构建 21:36:02 成功）**：

1. `WBAR-STATE veh= a= pair= aSpd= aStopped= aOnStn= aIsTgt= aStn= vSpd= onwp= over=` —— 签名变化才写（`train_cmd.cpp:13458` 前）。
2. `CPL-WAITCUT veh= tile= state= real= spd= mf=` —— 签名变化才写（`16114` 的截断块内）。

判读：若出现 `WBAR-STATE ... a=24 pair=0 aOnStn=0 aIsTgt=1` 且 `CPL-WAITCUT state=2` ⇒ 根因坐实，修法是放宽 `pair_ready` 的站台判据（或让扫描层与放行层共用同一「目的地」口径）。

### 3.13 新探针读数：**§3.12 的 `pair_ready` 假说被证伪，撤回**

日志 `build\R3R_debug.log`（19 5407 B，2026-10-23 21:39:42），产生者 `build\openttd.exe`（21:36:02，含 `WBAR-STATE`/`CPL-WAITCUT`）⇒ 采样有效。

```
896: CPL-WAITCUT veh=21 state=0 wp=17 tile=249,233
```

- **`state=0`** ⇒ `R3RShouldCutCoupleAtWaitPoint()` 返回 0（不截断）。**§3.12 的推断（`pair_ready` 恒假⇒返回 2⇒原地等）全部撤回**，`wait_cut` 根本没拦车。
- `WBAR-STATE` **零输出** ⇒ 该函数里的 `if (pair_ready)` 之前仍有早退（`state=0` 直接走 `return 0`），所以那五个子判据在这条路径上不参与决策。探针留着无害，但不是本案焦点。
- 只有一行是因为签名去重后 `state` 再没变过。

同一段日志的新关键行（**此前没见过的分支**）：

```
1288: TPR-WALK  veh=21 mf=255,233 origin=255,233 td=8 okay=1 exit=2 new=256,233 reach=1 spd=0
1289: CPL-SKIP  veh=21 tile=250,233 orderType=16 dontReserve=0 retryIn=2 holdAge=6
1290: TRP       veh=21 mf=255,233 real=12 type=16 dest=479497 spd=0 ... => ok=0 res=0
```

⇒ `TRP ok=0 res=0` 极可能**不是"无路"**，而是 `CPL-SKIP` 那条"本 tick 不预订/推迟挂接重试"的主动返回（KI-444 同族：`res=0` 是正常等待态，不是失败态）。

其余稳定读数：

| 行 | 读数 | 含义 |
|---|---|---|
| 1286/1291/1309/1327 | `CPL-PAIR tick=3015/3015/3018/3020 act=21 tgt=24 dist=16` | 锁**持续重建成功**（每 2~3 tick 一次），配对层健康 |
| 1191/1239/1260/1292/1310/1328 | `COUPLE-FAIL loco=21 ... x=4001 y=3736` | 每 tick 一次，坐标逐字不变 ⇒ 机车一步没动 |
| 897-899 / 1181-1183 | `CPL-WHY ... pairOK=0 ignOK=1` | 这两次采样（3001/3009）锁为空；`n` 到 6 后再没输出 |
| 901 / 1185 | `CPL-SCAN tick=3001/3009 ... rej=3/0/0/0 why=GROUP` | 扫描两次，之后不再出现 |

**下一个待证点**：`CPL-SKIP` 的 `retryIn=2` 是否**每 tick 被重置**（若重置⇒永远不重试⇒死循环），以及 `holdAge=6` 的计数口径。落点在 `R3RTryPathReserveCore()` 里打这一行的位置附近。

### 3.14 `CPL-SKIP` 疑点解除：节流健康，但**锁建立后搜索层再没被调用过**

代码（`src\pathfinder\yapf\yapf_rail.cpp`）：

| 锚点 | 事实 |
|---|---|
| `:1840-1864` `R3RCoupleScanHold()` | `logged` 一个 episode 只写**一行** `CPL-SKIP`；`retryIn = 8 - age`，`age >= 8` 时 `holding = false` 并返回 0 ⇒ **自动解锁，不是每 tick 重置** |
| `:1819` | `R3R_COUPLE_SCAN_RETRY = 8`，与 `holdAge=6 / retryIn=2` 自洽 |
| `:1899-1908` `R3RCoupleScanResult()` | `empty_tick` **只在"真搜索返回空"时刷新**；成功则 `holding=false` |

⇒ §3.13 的待证点（`retryIn` 每 tick 重置导致永不重试）**不成立**，撤回。`holdAge=6` 只是"6 tick 前做过一次空搜索"的正常读数。

但由此暴露一个更硬的事实（日志实测，`findstr /N` 全量扫）：

```
 901: CPL-SCAN tick=3001 veh=21 found=0 ... rej=3/0/0/0 ... why=GROUP
1185: CPL-SCAN tick=3009 veh=21 found=0 ... rej=3/0/0/0 ... why=GROUP
1286: CPL-PAIR tick=3015 act=21 tgt=24 dist=16
1289: CPL-SKIP  veh=21 ... retryIn=2 holdAge=6
1291: CPL-PAIR tick=3015
1309: CPL-PAIR tick=3018
1327: CPL-PAIR tick=3020        ← 日志到此结束（1345 行）
```

- `CPL-SCAN` **总共只有 2 条**，都在 `tick 3015`（锁建立）之前；`CPL-WHY` 也只有 6 行（tick ≤ 3009）。
- ⇒ **`CPL-WHY ... pairOK=0` 与 `CPL-PAIR` 并不冲突**：前者是锁建立**之前**的历史采样，时间错位解除（原 §5 Q-289-A 部分）。
- ⇒ **tick 3009 之后 `YapfTrainCoupleTrack()` 再没跑过**（否则节流到期必打新的 `CPL-SCAN`）。也就是说：配对锁一建立，流程就再也到不了挂接搜索 —— 卡点在搜索**之前**，不在搜索的判定里（`rej=3 why=GROUP` 是锁建立前的旧账，不是当前阻塞）。

同一段每 tick 复现的三行（`:1288/1307/1325/1343`）：

```
TPR-WALK veh=21 mf=255,233 origin=255,233 td=8 okay=1 exit=2 new=256,233 reach=1 spd=0
TRP      veh=21 mf=255,233 real=12 type=16 dest=479497 spd=0 fp=0 look=0 one=0 mstuck=0 fto=0 => ok=0 res=0
```

- `TPR-WALK` 每 tick 输出**逐字相同**的 `okay=1 exit=2 new=256,233 reach=1` ⇒ 预留试探本身认为"能走"；
- 紧接着 `TRP ok=0 res=0` ⇒ **失败点在 `TPR-WALK` 之后、`TRP` 汇总行之前**。

**下一个待证点**（收敛为两条）：
1. `TPR-WALK` 之后的代码路径上，哪一处把 `okay=1` 的结论翻成 `res=0`（候选：`R3RShouldCutCoupleAtWaitPoint` 之后的 `wait_cut` 分支、`R3RCoupleSearchSkipped` 之外的显式 return、或 `res_dest` 校验）；
2. 为什么 `CPL-PAIR` 建立以后连 `YapfTrainCoupleTrack()` 都不再被调用（`ChooseTrainTrack` 里那两条 `path_found != INVALID_TRACK` 之前的早退分支）。

### 3.15 真正的卡点：`ExtendTrainReservation()` 返回 INVALID_TILE，挂接寻路根本没被走到

`CPL-ENTRY` 实测只有 2 条（`tick 3001 / 3009`），与 `CPL-SCAN` 完全同一批。`tick 3018 / 3020` 时 `holdAge` 已是 9 / 11（≥ `R3R_COUPLE_SCAN_RETRY=8`），节流必然已解除，**却依然没有 `CPL-ENTRY`** ⇒ `YapfTrainCoupleTrack()` 在 tick 3015 之后一次都没被调用（`CPL-ENTRY` 在 `R3RCoupleScanHold` **之后**才打，所以"没有"意味着连函数入口都没进）。

调用链与出口穷举：

```
TryPathReserveCore  :17051  if (reachable != TRACK_BIT_NONE)
                    :17052      ChooseTrainTrack(consist, new_tile, exitdir, reachable, CTTF_FORCE_RES | ...)
                    :17060  if (!(result_flags & TPRRF_RESERVATION_OK)) return result_flags;   ← TRP 打 ok=0 res=0
ChooseTrainTrack 的四个 return：
                    :15968  trace-restrict WAIT_AT_PBS / REVERSE_AT        （会置 REVERSE_AT_SIGNAL）
                    :15982  !do_track_reservation                          （这里 do_track_reservation=true）
                    :16014  res_dest.tile == INVALID_TILE                  ← 无 RESERVATION_MADE ⇒ res=0
                    :16036  res_dest.okay && !long_reserve                 ← 带 CTTRF_RESERVATION_MADE ⇒ res=1
                    :16151  DoTrainCouplePathfind()                        ← CPL-ENTRY / CPL-SKIP 在这里
```

日志形态是 `res=0` **且**完全没有 `CPL-ENTRY` / `CPL-SKIP` / `COUPLE-DEPOT-DIRECT`，唯一自洽的出口就是 **`:16014`**：

```cpp
PBSTileInfo origin = FollowTrainReservation(consist, nullptr, FollowTrainReservationFlag::OkayUnused);
res_dest = ExtendTrainReservation(consist, origin, &tracks, &dest_enterdir, temporary_slot_state);
if (res_dest.tile == INVALID_TILE) { MarkTrainAsStuck(consist); return { FindFirstTrack(tracks), result_flags }; }
```

⇒ **`ExtendTrainReservation()` 每 tick 都失败**，从 `origin = 255,233 td=8` 出发、第一步 `new = 256,233`（`TPR-WALK ... exit=2 reach=1`），走不出去。因为没有预留（`res=0`），机车 `cur_speed` 恒 0，`COUPLE-FAIL` 的 `x=4001 y=3736` 就逐字不变 —— **与挂接判定、配对锁、目的地闸门全部无关**（那三个都是锁建立前的旧账）。

旁证：
- `TPR ... fto=0` ⇒ KI-330 的 `ForceReserveOnce` 从未被消费（因为根本没走到预留尝试之后的路径）；
- `TPR-WALK` 每 tick 输出**逐字相同**的 `origin/td/okay/exit/new` ⇒ 起点不动、走法不动、失败不动，典型"同一条 walk 重放"形态（与 KI-384 的 loop memo 家族同构，但日志里 **没有任何** `EXT*` / `LOOPMEMO` 探针，`ExtendTrainReservation` 内部目前是观测盲区）。

**遗留疑点（REG-289-D，待探针）**：`tick 3015` 走到了 `:16151`（有 `CPL-SKIP`，说明 `ExtendTrainReservation` 那次没返回 `INVALID_TILE`），而 `tick 3018 / 3020` 同样 origin `255,233` 却提前死在 `:16014`。两个 tick 之间 `ExtendTrainReservation` 的结果发生了翻转（可能从 `okay=0` / `long_reserve=1` 的"继续"态变成 `INVALID_TILE`），必须用探针取 `res_dest.tile / res_dest.okay / long_reserve` 才能定性。

**下一步（探针，零行为变更）**：在 `ChooseTrainTrack()` 的 `:16005~16048` 之间加 `EXT-DEST`，打印
`origin=(x,y,td,okay) res_dest=(x,y,td,okay) long_reserve= tracks=0xN mark_stuck= do_res=<0/1>`，
按签名去重，只在该车 `current_order.IsType(OT_GOTO_COUPLE)` 时输出。

### 3.16 第 2 份日志（含 `EXT-DEST` 的新 exe）：假说 H-289-3 被**部分证伪**，卡点前移到 `ChooseTrainTrack` 的"已有预留即返回"分支

采样有效性（先证）：

| 项 | 读数 | 判定 |
|---|---|---|
| `build\R3R_debug.log` | 193 164 B / 21:50:14 | 本轮现场 |
| `build\openttd.exe` | 52 297 216 B / 21:47:22 | 晚于 `src\train_cmd.cpp`(21:44:24) |
| `build\R3R_incbuild.done` | `EXIT_CODE=0` | 构建成功 |
| exe 内字面量 `EXT-DEST` | `EXE-HAS-EXT-DEST` | **探针确实在新 exe 里** |

⇒ 采样有效，"`EXT-DEST` 零命中"是**真实结果**，不是没跑到。

**日志实测（`findstr /N` 全量）**：

```
 894: CPL-ENTRY veh=21 tile=249,233 orderType=16 dontReserve=0
 895: CPL-ORIGIN veh=21 origin=249,233 td=8 vehDir=1 vehTd=8
 896: CPL-WAITCUT veh=21 state=0 wp=17 tile=249,233
 900: CPL-PATHFOUND veh=21 found=0
 901: CPL-SCAN tick=3001 ... why=GROUP          ← 锁建立前
1175: CPL-ENTRY veh=21 tile=250,233 orderType=16
1180: CPL-PATHFOUND veh=21 found=0
1181: CPL-SCAN tick=3009 ... why=GROUP          ← 锁建立前
1250: CPL-PAIR tick=3015 act=21 tgt=24 dist=16 actTile=250,233 tgtTile=265,234
1252: TPR-WALK veh=21 mf=255,233 origin=255,233 td=8 okay=1 exit=2 new=256,233 reach=1 spd=0 one=0 look=0
1253: CPL-SKIP veh=21 tile=250,233 orderType=16 retryIn=2 holdAge=6
1254: TRP veh=21 ... => ok=0 res=0
1271: TPR-WALK ... 同 1252（逐字相同）
1272: TRP veh=21 ... => ok=0 res=0         ← 之后无 CPL-ENTRY / CPL-SKIP
1289: TPR-WALK ... 同 1252（逐字相同）
1290: TRP veh=21 ... => ok=0 res=0         ← 日志到此结束
```

关键读数（三条，互相咬合）：

1. **`EXT-DEST` 一条都没有** ⇒ 既没进 `:16010` 的 `INVALID_TILE` 分支，也没进 `:16037` 的 `res_dest.okay` 分支。
2. **`TPR-WALK` 每 tick 都在打，`reach=1`** ⇒ `TryPathReserveCore` 走到了 `:17094` 并**确实调用了** `ChooseTrainTrack()`（`:17095`），排除"没进函数"。
3. **`tick 3015` 有 `CPL-SKIP`，`tick 3041/3057` 之后没有** ⇒ 两者之间 `ChooseTrainTrack` 走了**不同出口**。

把三个读数套进出口穷举表，唯一自洽的剩余出口是 **`:15942-15944`**：

```cpp
TrackBits res_tracks = (TrackBits)(GetReservedTrackbits(tile) & DiagdirReachesTracks(enterdir));
if (res_tracks != TRACK_BIT_NONE) return { FindFirstTrack(res_tracks), result_flags };  // result_flags 此时 = TPRRF_NONE
```

- 返回的 `result_flags` **不含** `CTTRF_RESERVATION_MADE` ⇒ `:17096` 不置 `TPRRF_RESERVATION_OK` ⇒ `:17103` 直接 return ⇒ `TRP ok=0 res=0` ⇒ 机车 `spd=0` 不动；
- 该出口**不打任何探针**、也**不调** `DoTrainCouplePathfind()`（在 `:16151`，更靠后）⇒ 与"`CPL-ENTRY` 消失"完全一致；
- 也不进 `:16008` 的 `if (do_track_reservation)` 块 ⇒ 与"`EXT-DEST` 零命中"完全一致。

**为什么会命中 `:15944`**：`tick 3015` 那一次 `ExtendTrainReservation()` 成功且 `res_dest.okay == 0`（否则会打 `EXT-DEST bad=0`），于是没有在 `:16070` 提前 `return`，流程继续走到 `DoTrainCouplePathfind()`（打 `CPL-SKIP`）；而它**已经把这步轨道订下来了**（`res_dest.tile` 有效即为订成功）。下一 tick 重新进来时，`new_tile=256,233` 上已有自己的预留 ⇒ `:15944` 命中 ⇒ 走"啥也不做直接返回"的短路。

> 即：**预留订上了，但没有任何一 tick 把"订上了"报成 `RESERVATION_OK`**；一次成功的续订反而让下一 tick 永远短路。

**新假说（H-289-4，待验证）**：`TryPathReserveCore` 在 `:15944` 命中时不视作成功，是"挂接机车一步不动"的直接原因。Kill 判据：在 `:15942` 前后加只读探针（`EXT-PRE tile/enterdir/tracks/reserved/res_tracks/do_res`），若每 tick 打印 `res_tracks=0x1`（或非 0）即证实。

**回填的旧结论**：
- §3.15 的"`:16014` 是卡点"**部分证伪** —— 第 1 份日志（无 `EXT-DEST`）时它只是最可能是，现有直接反证（零 `EXT-DEST`）；
- REG-289-D（同起点跨 tick 结果翻转）**有了机制解释**：`tick 3015` 是"首次没有已有预留"的 tick，之后每一 tick 都命中 `:15944` 短路，不再是随机翻转。

## 四、结论（当前已实证部分）

1. **第 288 轮的停车问题已收口**：`spd=0`、无 `WP-NOARRIVE`，且 `TRP`/`TPR-WALK` 显示运动前端 255,233 仍在正常前进可达（`okay=1 exit=2 reach=1 new=256,233`），只是整体速度 0。
2. **当前唯一阻塞**：`CPL-PATHFOUND veh=21 found=0` ⇒ 挂接寻路（YAPF `FindNearestCoupleTrain` 那条路径）判定"到不了目标车底"。机车因此每 2 tick 重试一次、`COUPLE-FAIL` 反复，`dist=16` 永不缩短。
3. 与 KI-330（解挂后 `CPL-PATHFOUND found=0`）同族形态，但本次是**主动挂接侧**、且机车自己速度已归零。

## 五、疑点（按规则先落盘，待验证）

- **Q-289-A（高）→ 已解答（见 §3.4）**：`CPL-SCAN` 有输出，`consist=3 rej=3/0/0/0 acc=0 why=GROUP` ⇒ 等待链在场、3 个候选全被 **GROUP 闸门**拒。`CPL-SAFE-FAIL/CPL-BEST/CPL-HIT/CPL-BACK` 零命中的原因是**闸门比这些探针更早**（候选在 `PfDetectDestination` 就被拒，走不到安全位置检查/回溯）。原疑点两种可能：
  (a) 这些探针走 `R3RDbgWrite`，tag 被 `R3RDbgTagAllowed()` 白名单吞掉（同 Q-285-1 的老坑）；
  (b) 寻路在到达"安全位置检查/回溯"之前就早退（例如目标不在同一 YAPF 连通域、或 `FindNearestCoupleTrain` 的目标枚举阶段就失败）。
  必须先用代码确认 `CPL-PATHFOUND` 的打点位置与 `found=0` 的赋值分支，再决定要不要补探针。
- **Q-289-B（中）**：`CPL-PAIR` 判定的 `act=21 / tgt=24` 是"当前配对"，`dist=16`；但 `TRP` 的 `mf=255,233`、`TPR-WALK ... new=256,233` 说明**运动前端可以继续往东走**（朝向目标 265,234）。那么"谁该动"：机车是不是应该先往前开到车底旁边再挂？若挂接寻路要求"起点必须已与目标相邻/同站台"，则 `found=0` 是设计使然，需要改的策略是"先靠近"而不是"寻路成功"。
- **Q-289-C（中）**：`COUPLE-WAIT-OFFDEST veh=21 ... onStnTile=0` — 机车**不在站台上**，而目标车底 24 在 `265,234`。这条探针的名字（OFFDEST）暗示"配对目标不在目的地"，需要读代码确认它是"报告原因"还是"主动放弃"。
- **Q-289-D（低）**：`CPL-SKIP ... retryIn=2 holdAge=6` 的节流参数是本轮新增还是既有？`holdAge` 增长到多少会放弃/改走别的策略，需确认。

## 六、待办（下一步取证顺序）

1. 读 `src\pathfinder\yapf\yapf_destrail.hpp`：`CPL-PATHFOUND` 打点位置、`found` 的赋值来源、`FindNearestCoupleTrain` 的早退分支（哪些分支不打探针）。
2. 读 `src\train_cmd.cpp` 的 `TrainCoupleHandler`：`COUPLE-WAIT-OFFDEST` 的触发条件，判断它是"诊断"还是"结束本次尝试"。
3. 读 `CPL-PAIR` 生成处，确认 `dist` 与"是否允许开过去"的关系。
4. 视需要补只读探针（`CPL-GATE` 已有 target-not-wait 之类），但**先定性再改码**。

## 七、复测判据（本轮修完后的目标形态）

1. 机车 21 从 `250,233` 出发、`dist` 由 16 单调递减到命中阈值（≤ 拼接容差），出现 `CPL-HIT` / `COUPLE-OK loco=21`。
2. `CPL-PATHFOUND found=1`（不再是 0），`TRP ok=1 res=1`。
3. 停住时不再刷 `COUPLE-FAIL`（每 2 tick 一次）。
4. 负判据：第 288 轮的"速度压到 0"不回退（不得出现带 15 冲过路点、不得出现 `WP-NOARRIVE`）。
5. 负判据：目标车底不在目的地时的既有行为（`COUPLE-DEST-EMPTY` 500 tick 放弃）逐字不变。

## 八、第 3 份日志（2026-10-23 21:58）：`EXT-*` 零命中的真因 = **探针被标签白名单静音**（假阴性）

### 8.1 采样有效性

| 核对项 | 读数 |
|---|---|
| `build\openttd.exe` | 2026-10-23 21:57:24，52 297 216 B |
| `build\R3R_incbuild.done` | `EXIT_CODE=0`（21:57:31） |
| `build\R3R_debug.log` | 2026-10-23 21:58:32，192 477 B / 1 273 行 |
| exe 内字面量 | `EXT-PRE` FOUND、`EXT-DEST` FOUND、`TPR-WALK` FOUND、`CPL-SKIP` FOUND、`COUPLE-WAIT-OFFDEST` FOUND |

⇒ 日志晚于 exe，探针确认在二进制里，采样有效。

### 8.2 读数（行号锚点）

```
 894 CPL-ENTRY veh=21 tile=249,233 orderType=16 dontReserve=0
1175 CPL-ENTRY veh=21 tile=250,233 orderType=16 dontReserve=0
1222 TRP veh=21 mf=255,233 real=12 type=16 dest=491798 spd=0 fp=0 look=0 one=0 mstuck=0 fto=0 => ok=0 res=0
1247 TRP veh=24 mf=270,234 real=11 type=17 dest=4294967295 spd=0 fp=0 look=0 one=0 mstuck=1 fto=1 => ok=0 res=0
1250 CPL-PAIR tick=3015 act=21 tgt=24 dist=16 actTile=250,233 tgtTile=265,234
1252 TPR-WALK veh=21 mf=255,233 origin=255,233 td=8 okay=1 exit=2 new=256,233 reach=1 spd=0 one=0 look=0
1253 CPL-SKIP veh=21 tile=250,233 orderType=16 dontReserve=0 retryIn=2 holdAge=6
1254 TRP veh=21 mf=255,233 real=12 type=16 dest=479497 spd=0 ... => ok=0 res=0
1272 TPR-WALK veh=21 ...（逐字同 1252）
1273 TRP veh=21 ... => ok=0 res=0   ← 日志结束
```

全日志标签计数：`RESV-` 33 行、`UNRES-` 3 行、`EXT-PRE` **0**、`EXT-DEST` **0**、`EXTEND-` 0、`LOOP-` 0、`R3R-RES` 0。

### 8.3 真因（代码铁证，`src\r3r_perf.h`）

```cpp
inline void R3RDbgWrite(const char *fmt, ...)
{
    if (!R3RDbgOn()) return;
    if (!R3RDbgTagAllowed(fmt)) return;   // ← 标签白名单，fopen 都不到
    ...
}

inline bool R3RDbgTagAllowed(const char *fmt)
{
    if (all_tags) return true;                    // R3R_DBG_TAGS=all|1|*
    ... n = 首 token 长度（≤8）
    if (n >= 4 && strncmp(fmt,"RESV",4)==0) return true;
    if (n >= 5 && strncmp(fmt,"UNRES",5)==0) return true;
    if (n >= 6 && strncmp(fmt,"EXTEND",6)==0) return true;
    if (n >= 4 && strncmp(fmt,"LOOP",4)==0) return true;
    if (n >= 7 && strncmp(fmt,"R3R-RES",7)==0) return true;
    return false;
}
```

`EXT-PRE` 的首 token 是 `EXT-PRE`（n=7），拿去和 `"EXTEND"` 做 `strncmp(...,6)` 比较的是 `"EXT-PR"` ⇒ **不等**；`EXT-DEST` 同理（`"EXT-DE"`）。两个探针都走 `R3RDbgWrite`，因此在写日志之前就被丢弃。

正面印证同一份日志：`RESV-*`（33 行）、`UNRES-*`（3 行）都在白名单里所以照常出现；而 `TPR-WALK` / `CPL-ENTRY` / `COUPLE-WAIT-OFFDEST` 走的是 `R3RFopenDbg("a")` + `fprintf` 直写（只受 `R3R_DBG_RAW` 与 `R3RDbgOn()` 管），不经过标签白名单，所以照常出现。

⇒ 这是第 274 轮为压噪音加的过滤器，把第 289 轮新加的诊段探针一起吞了。

### 8.4 结论修正（重要）

- **§3.16 的"H-289-3 被部分证伪 / 卡点前移到 `:15944`"作废**。`EXT-DEST` 零命中不再是"两个分支都没走到"的证据，只是"日志里不显示"。
- **H-289-4（`:15944` 已有预留即短路）也未被验证**：`EXT-PRE` 同样零命中，无法判定 `reserved=0x0` 还是非 0。
- 目前能确证的只有：`TPR-WALK reach=1`（`reachable != TRACK_BIT_NONE`）⇒ `:17115` 判真 ⇒ **`ChooseTrainTrack()` 在本 tick 确实被调用**。这是上一轮链条里唯一还站得住的硬约束。

### 8.5 处置（二选一，推荐 B）

- **A（不重编）**：启动游戏前设环境变量 `R3R_DBG_TAGS=all`，白名单整体放行；缺点=日志会回到全量噪音（第 274 轮之前的规模）。
- **B（推荐，一次增量编译）**：把两个探针改名成白名单前缀 —— `EXT-PRE` → `RESV-EXT-PRE`，`EXT-DEST` → `RESV-EXT-DEST`。它们本来就是预留侧诊断，语义上也属于 `RESV-*` 集合；改完无需任何环境变量，日志也不炸。

### 8.6 新疑点 / 规约（REG-289-E）

凡新增 `R3RDbgWrite` 探针，**首 token 必须命中 `R3RDbgTagAllowed()` 白名单**（`RESV-` / `UNRES-` / `EXTEND-` / `LOOP-` / `R3R-RES-`），否则等于没加。本轮连续两份日志都因此白跑一次，属可避免的取证浪费。若探针必须用别的名字，二选一：改名，或该处改用 `R3RFopenDbg` 直写。

## 九、第 3 份日志的决定性读数：候选车底扫到 3 个，**全被 GROUP 闸门拒**（H-289-1 复活）

### 9.1 一次完整的失败寻路（`build\R3R_debug.log:1175-1188`，原文）

```
1175:CPL-ENTRY veh=21 tile=250,233 orderType=16 dontReserve=0
1176:CPL-ORIGIN veh=21 origin=250,233 td=8 vehDir=1 vehTd=8 dontReserve=0
1177:CPL-WHY node=278,234 act=21 tgt=24 ch=21 th=24 pairTgt=1048575 pairReq=1048575 pairOK=0 ignOK=1 aStn=65535 tStn=65535 destStn=12 isDepot=0 aStop=0 tStop=0 n=4
1178:CPL-WHY ... n=5
1179:CPL-WHY ... n=6
1180:CPL-PATHFOUND veh=21 found=0
1181:CPL-SCAN tick=3009 veh=21 found=0 closed=82 limit=10000 nodes=308 enter=226 noplat=150 nores=6 noconsist=67 consist=3 rej=3/0/0/0 acc=0/0 accwp=0 last=278,234 lastveh=24 why=GROUP
1182:RESV-GC veh=21 tile=254,233 track=stn kind=WAYPOINT why=free-path-hold
1183:RESV-GC veh=21 tile=256,233 track=0x0 why=free-path
1184:RESV-GC veh=21 tile=257,233 track=0x0 why=free-path
1185:RESV-GC veh=21 tile=258,233 track=0x0 why=free-path
1186:RESV-LEDGER-FORGET veh=21 why=free-path mask=0x1 released=3 kept=1 phase=choose-track
1187:COUPLE-FAIL loco=21 order=16 tx=250 ty=233 x=4001 y=3736
1201:COUPLE-WAIT-OFFDEST veh=21 tile=250,233 mf=255,233 real=12 destStn=12 destTile=491798 onStnTile=0
1202:SPDD veh=21 from=20 to=0 dt=0 ctx=stop-train cfrom=15 cto=0 cage=0 m=0 ... stuck=1
1203:STUCK veh=21 tile=250,233 state=1 stuck=1 fp=0 parked=0 spd=0 order=16 real=12 prog=5
```

### 9.2 判读（三条硬结论）

1. **寻路确实跑完了，而且扫到了目标**：`closed=82`、`nodes=308`、`enter=226`、`consist=3`、`last=278,234`、`lastveh=24`。⇒ 不是"没找到车底"，是找到了 **3 个候选**（`consist=3`）却**一个都没接受**（`rej=3/0/0/0`，第一类计数=3，`acc=0/0`），最终 `why=GROUP`。
2. **拒绝原因是 GROUP 闸门**：`CPL-WHY` 三行（n=4/5/6）逐字相同：`pairTgt=1048575 pairReq=1048575 pairOK=0 ignOK=1`。`1048575 = 0xFFFFF` 是无效车号；`pairOK=0` 表示**配对握手未就绪**。`ignOK=1` 说明"忽略配对"的那条放行不存在。
3. **`aStn=65535 tStn=65535`** ⇒ 双方的 tile 都判定为"不在任何站台"（`destStn=12` 是订单目的地站，与它无关），`isDepot=0`，`aStop=0 tStop=0`。

### 9.3 结论修正（第二次，重要）

- 上一轮我用 `1250 CPL-PAIR tick=3015 act=21 tgt=24 dist=16` 否掉了 **H-289-1「配对没就绪」**。**该证伪是错的**：`CPL-PAIR` 打的是耦合对（act/tgt/dist，供距离判定），而 `CPL-WHY` 的 `pairTgt/pairReq/pairOK` 是**寻路侧的握手字段**。两者并存 ⇒ 发生了"耦合对已建立、但寻路侧握手仍为 Invalid"的分裂。
- 因此 **H-289-1 复活**，且现在是**唯一有直接读数支撑**的根因假设。`rej=3/0/0/0` + `why=GROUP` 就是它的后果。

### 9.4 新假设 H-289-5（待验证）

`pairOK=0`（`pair_tgt/pair_req` 仍是 `VehicleID::Invalid`）使 GROUP 类闸门对每一个候选车底恒拒 ⇒ `found=0` ⇒ 无 `RESERVATION_MADE` ⇒ `TRP ok=0 res=0` ⇒ `MarkTrainAsStuck` + `FreeTrainTrackReservation`（见 1186 的 `RESV-LEDGER-FORGET ... released=3`）。

下一步只做一件事：读 `CPL-WHY` 的打点处，确认 `pairTgt/pairReq/pairOK` 分别取自哪个字段、`pairOK` 由谁置位；再读 `R3RCoupleAllowedIgnoringPair()` 在 `pairOK==0` 时的分支，确认它是"必须 pair 就绪才放行"还是"pair 未就绪时另有放行口（`ignOK`）"。**在此之前不改任何行为代码。**

### 9.5 顺带澄清的两个旧读数

- `1248 RESV-NOBOOK veh=24 ... reason=parked-waiter`：目标车底 24 是"停稳的等待者"，第 127 轮 KI-208 的门禁让它不再订路 —— 属**设计行为**，不是故障；但这也意味着 24 永远不会动，若握手要求"对方也走过来"，就会永久死锁（待 H-289-5 验证时一并核对）。
- `1253 CPL-SKIP ... retryIn=2 holdAge=6`：`R3R_COUPLE_SCAN_RETRY=8` 的空搜索节流，6 tick 前刚做过一次空搜索（就是 1175 那次）。节流本身正常，`st.logged` 只在每个节流段打一次日志，所以后面不再重复出现 `CPL-SKIP`；这解释了 1253 之后到日志结束（1274 行）只有 `TPR-WALK`/`TRP`/`COUPLE-FAIL`，没有 `CPL-ENTRY`。

---

## 十、定案（第 289 轮续三，2026-10-23）

### 10.1 决定性代码：`rej[0]` 就是 `!R3RCoupleAllowed()` 的合并档

`src\pathfinder\yapf\yapf_destrail.hpp:606-648` 原文：

```cpp
606:		if (!R3RCoupleAllowed(Train::From(Yapf().GetVehicle()), t)) {
...
624:					const bool r3r_pair_ok = R3RCouplePairMatches(r3r_act, t);
625:					const bool r3r_ign_ok = R3RCoupleAllowedIgnoringPair(r3r_act, t);
...
646:			r3r_dest_reject(0, tile, t);
647:			return false;
648:		}
```

配合计数器的自我声明（同文件 `:40`、`:71`、`:566`）：

```cpp
40:inline uint32_t _r3r_cpl_scan_rej[4] = { 0, 0, 0, 0 }; ///< GROUP / FIT / TARGET / ORDER
71:	static const char * const why_names[7] = { "GROUP", "FIT", "TARGET", "ORDER", "acc-car", "acc-wait", "cut-wp" };
566:			++_r3r_cpl_scan_rej[why];      // r3r_dest_reject() 内唯一自增点
```

**结论**：`rej[0]` / `why=GROUP` 这个名字极具误导性。它对应的**不是**"挂接分组白名单"，而是 `R3RCoupleAllowed()` 整个函数的否定结果，该函数至少含三个子判据——pair 握手、目的地是否落在本车站、分组白名单——三者被压进同一个计数器。

⇒ 过去多轮（含本备忘 §9.2/§9.3、KI-454 附记续二）把 `why=GROUP` 读成"分组闸门"都是**误读**。

### 10.2 定案：唯一被踩中的子判据是 pair-mismatch

`1177-1179 CPL-WHY ... pairOK=0 ignOK=1 aStn=65535 tStn=65535 destStn=12 isDepot=0` 把这三点分离开：

| 子判据 | 读数 | 判定 |
|---|---|---|
| pair 握手 | `pairOK=0`，`pairTgt=1048575`、`pairReq=1048575`（`0xFFFFF` = `VehicleID::Invalid`），双方链头 21 / 24 的双锁**都为空** | **不过** |
| 目的地在本车站 | `R3RCoupleAllowedIgnoringPair()` 的返回值 `ignOK=1` ⇒ 目的地/分组两条已放行 | 过 |
| 分组白名单 | 同上，被 `ignOK=1` 覆盖 | 过 |

因为 `ignOK` 是"除 pair 之外全部子判据的与"，`ignOK=1` 与 `pairOK=0` 并存时，`R3RCoupleAllowed()` 必假、且**唯一**失败项就是 pair。

⇒ **定案：`found=0` 的唯一闸门是 pair-mismatch（双锁未建立），不是分组、不是目的地、不是"找不到车底"。**

### 10.3 但 3001/3009 那两次 pair 为空是时序必然，不是缺陷

双锁由扫描层（`TrainCoupleHandler` → `R3RPairCoupleTargets`）建立，建立成功后打 `CPL-PAIR`。全日志第一次 `CPL-PAIR` 是 `tick=3015`，而两次 `CPL-PATHFOUND found=0` 分别是 `tick=3001`、`tick=3009`——**配对晚了 6~14 tick**。

扫描层与寻路层本来就不同频：扫描层受 `R3R_COUPLE_SCAN_RETRY=8` 节流，寻路层每 tick 都可能被调用。所以 3009 那次读到 `pairOK=0` 是**预期行为**，不能据此判缺陷。

### 10.4 当前真正的卡点

`CPL-PAIR` 建立（3015、3077）之后，日志里**再没有出现任何 `CPL-PATHFOUND`、也没有第三次 `CPL-SCAN`**，只有：

- `1253 CPL-SKIP veh=21 tile=250,233 orderType=16 dontReserve=0 retryIn=2 holdAge=6`
- `CPL-WAIT-OFFDEST veh=21 mf=255,233 ... onStnTile=0 destStn=12`
- `TRP veh=21 => ok=0 res=0`、`COUPLE-FAIL loco=21 order=16`

即：**配对建立之后的挂接寻路被 `R3RCoupleScanHold()` 的 8-tick 空搜索节流整段吞掉，从未跑到 `FindPath()`**（`CPL-PATHFOUND` 与 `CPL-SCAN` 同频，都是 0 条即为证）。

日志在 `holdAge=6` 处截尾，距阈值 8 只差 2 tick，**现有样本不足以判决**接下来的重扫是"找到"还是"仍 pair-mismatch"。

### 10.5 由此产生的两个可执行动作（无需再推理）

1. **取数**：等 `RESV-EXT-PRE`/`RESV-EXT-DEST`（已改名、待编译）编进 exe 后复跑同一场景，重点只要三样：
   - 是否存在**第 3 次及以后的 `CPL-SCAN`/`CPL-PATHFOUND`**（证明重扫确实发生了）；
   - 那次重扫的 `pairOK` 是 0 还是 1（1 ⇒ 闸门已过，问题转向预留/延伸；仍 0 ⇒ 双锁在配对建立后又被清掉）；
   - `RESV-EXT-PRE`/`RESV-EXT-DEST` 是否出现（决定 `ExtendTrainReservation` 两个出口）。
2. **改标签**：`_r3r_cpl_scan_rej[0]` 与 `why_names[0]` 的 `"GROUP"` 应改成 `"ALLOW"`（或 `on_duty` 之外更准确的名字），并在 `CPL-SCAN` 行补打 `pairOK` 位，否则后续每一轮都会被这四个字骗一次。此项属探针可读性修正、零行为变更。

### 10.6 定案后的假设台账

| 编号 | 假设 | 状态 |
|---|---|---|
| H-289-1 | 配对没就绪导致拒绝 | **成立**（`pairOK=0` 直读），但仅解释 3001/3009，属时序必然 |
| H-289-2 | 分组白名单拦掉 | **证伪**（`ignOK=1`） |
| H-289-3 | 目的地不在本车站 | **证伪**（`ignOK=1`；`aStn/tStn=65535` 都非站台，但该子判据已过） |
| H-289-4 | 预留侧 `res_tracks` 提前 return | **未验**（需 `RESV-EXT-*`） |
| H-289-5 | pair=0 ⇒ 恒拒 ⇒ 标 stuck ⇒ 清预留 | **成立但仅在 3009 那一刻**；配对之后是否复现待第 3 次 `CPL-SCAN` |

---

## 十一、第 289 轮续四：新 exe 复跑取数（2026-10-23 22:10）

### 11.1 输入有效性

- `build\R3R_debug.log`：**192 743 B / 1 276 行**，`LastWriteTime = 2026-10-23 22:10:57`
- `build\openttd.exe`：**52 297 216 B**，`LastWriteTime = 2026-10-23 22:07:08` ⇒ 日志由含 `RESV-EXT-PRE`/`RESV-EXT-DEST` 改名版产生，**采样有效**
- 标签计数（`Select-String | Group-Object Pattern`）：`CPL-WHY`=6、`TRP`=4、`COUPLE-WAIT-OFFDEST`=3、`COUPLE-FAIL`=2、`CPL-PAIR`=2、`CPL-ENTRY`=2、**`RESV-EXT-PRE`=2**、`CPL-SCAN`=2、`CPL-PATHFOUND`=2、`CPL-SKIP`=1、**`RESV-EXT-DEST`=0**、`COUPLE-OK`=0、`COUPLE-DEST-EMPTY`=0

### 11.2 关键读数（逐字）

```
893:  RESV-EXT-PRE veh=21 tile=255,233 enterdir=2 tracks=0x1 reserved=0x0 doRes=0 real=12 spd=5
895:  CPL-ENTRY veh=21 tile=249,233 orderType=16 dontReserve=0
898:  CPL-WHY node=278,234 act=21 tgt=24 ch=21 th=24 pairTgt=1048575 pairReq=1048575 pairOK=0 ignOK=1 aStn=65535 tStn=65535 destStn=12 isDepot=0 aStop=0 tStop=0 n=1
901:  CPL-PATHFOUND veh=21 found=0
902:  CPL-SCAN tick=3001 veh=21 found=0 closed=82 limit=10000 nodes=308 enter=226 noplat=150 nores=6 noconsist=67 consist=3 rej=3/0/0/0 acc=0/0 accwp=0 last=278,234 lastveh=24 why=GROUP
1177: CPL-ENTRY veh=21 tile=250,233 orderType=16 dontReserve=0
1182: CPL-PATHFOUND veh=21 found=0
1183: CPL-SCAN tick=3009 veh=21 found=0 closed=82 ... rej=3/0/0/0 acc=0/0 ... last=278,234 lastveh=24 why=GROUP
1252: [R3R] CPL-PAIR tick=3015 act=21 tgt=24 dist=16 actTile=250,233 tgtTile=265,234
1255: CPL-SKIP veh=21 tile=250,233 orderType=16 dontReserve=0 retryIn=2 holdAge=6
1257: RESV-EXT-PRE veh=21 tile=256,233 enterdir=2 tracks=0x1 reserved=0x1 doRes=1 real=12 spd=0
1258: [R3R] CPL-PAIR tick=3062 act=21 tgt=24 dist=16 actTile=250,233 tgtTile=265,234
```

日志尾部（时间线终点，约 tick 3040~3072）：

```
COUPLE-WAIT-OFFDEST veh=21 tile=250,233 mf=255,233 real=12 isDepot=0 destDepot=0 destStn=12 destTile=479497 onDepotTile=0 onStnTile=0
TPR-WALK veh=21 mf=255,233 origin=255,233 td=8 okay=1 exit=2 new=256,233 reach=1 spd=0 one=0 look=0
CPL-SKIP veh=21 tile=250,233 orderType=16 dontReserve=0 retryIn=2 holdAge=6
TRP veh=21 mf=255,233 real=12 type=16 dest=479497 spd=0 fp=0 look=0 one=0 mstuck=0 fto=0 => ok=0 res=0
RESV-EXT-PRE veh=21 tile=256,233 enterdir=2 tracks=0x1 reserved=0x1 doRes=1 real=12 spd=0
[R3R] CPL-PAIR tick=3062 act=21 tgt=24 dist=16 actTile=250,233 tgtTile=265,234
COUPLE-FAIL loco=21 order=16 tx=250 ty=233 x=4001 y=3736
COUPLE-WAIT-OFFDEST veh=21 ...
TPR-WALK veh=21 ...
TRP veh=21 ... => ok=0 res=0
RESV-AUDIT-WAIT tick=3072 waited=1024 interval=2048 due=0 pending=0
```

### 11.3 判据 ①：配对确实建立了（上一轮定案的 H-289-1「仅解释 3001/3009」被证实）

`CPL-PAIR tick=3015` 与 `tick=3062` 各一次，`act=21 tgt=24 dist=16` 稳定。而 6 条 `CPL-WHY` 全部落在 `n=1..6`，时间上属于 3001（n=1..3）与 3009（n=4..6）两次扫描 ⇒ **配对建立之后没有任何一次 `CPL-WHY`**，因为根本没有新的 `CPL-SCAN`。

### 11.4 判据 ②（新，决定性）：配对建立后的挂接寻路**从未被调用**

`CPL-ENTRY`、`CPL-SCAN`、`CPL-PATHFOUND` 三个探针全部只在 3001/3009 出现（各 2 条），3015 之后 0 条。而这三个探针是 `CYapfTrainRailT::FindNearestCoupleTrain()` 的**无条件入口打点**（`CPL-ENTRY` 在函数开头、`CPL-PATHFOUND`/`CPL-SCAN` 在 `FindPath()` 之后无条件回放，见 `yapf_rail.cpp:816-886`），不受任何节流约束。

⇒ 3015~3072（约 57 tick）内 `FindNearestCoupleTrain()` **一次都没被调用**。日志尾部的 `TPR-WALK veh=21 mf=255,233 origin=255,233 ...` 与 `RESV-EXT-PRE ... doRes=1` 属**普通预留路径**（`TryPathReserve` 自身的 walk/延伸），不是挂接寻路。

⇒ 卡点从"挂接寻路被节流吞掉"（上一轮 §10.4 的推断）**上移一级**：是 `TrainCoupleHandler()` 在调用挂接寻路之前就返回了。

### 11.5 判据 ③（新）：`TrainCoupleHandler` 走的是 `u == nullptr` 分支

`COUPLE-WAIT-OFFDEST veh=21` 共 3 条，最后两条紧跟在 `CPL-PAIR`（3062）与 `COUPLE-FAIL` 之后，字段：

- `mf=255,233`（运动前端）、`real=12`（`cur_real_order_index`）、`destStn=12`、`destTile=479497`
- **`onStnTile=0` ⇒ 运动前端 255,233 不在站台格上**；`onDepotTile=0` ⇒ 也不在库内
- `destDepot=0 isDepot=0` ⇒ 订单是 GOTO_COUPLE 到车站（`destStn=12`）而非车库

这条打点位于 `TrainCoupleHandler()` 里"找候选车底得到 nullptr"的分支（KI-193 为 `COUPLE-DEST-EMPTY` 加的同族探针），⇒ **配对成功（`CPL-PAIR act=21 tgt=24`）、握手成功之后，扫描候选仍然返回 nullptr**，于是后续 `DoTrainCouplePathfind()` 根本不被调用。

⇒ 这与 §10 的定案不矛盾而是接续：**`pair` 是寻路层闸门，`TrainCoupleHandler` 的候选搜索是另一层，后者现在成了新的唯一卡点。**

### 11.6 疑点 REG-289-B（新登记，必须查代码）：`RESV-EXT-PRE` 有 2 条、`RESV-EXT-DEST` 有 0 条

- `893: doRes=0 spd=5`（车还在滑行，`dont_reserve` 语义：不订路）
- `1257: doRes=1 spd=0`（车已停稳，允许订路）

两条都进了 `ExtendTrainReservation()` 之前的 PRE 打点，但**没有任何一条 DEST 打点**。因为 PRE 与 DEST 是同一次调用的入口/出口（同一函数两端），DEST=0 ⇒ **每一次 `ExtendTrainReservation()` 都在 DEST 打点之前返回了**。

这正好解释了尾部 `TRP veh=21 ... => ok=0 res=0`（`TryPathReserve` 未成功、未预留）：延伸没走完，`res=0`。

而 `reserved=0x1`（1257 那一行）说明**目标格 256,233 当时是已预留状态**（`tracks=0x1`=`TRACK_X`，`reserved=0x1` 即已订），所以极可能是"延伸到自己已订的格 ⇒ 提前 return true/return"这类短路，也可能是"该格被别的车占/不允许延伸"。

**待验证项**：读 `ExtendTrainReservation()` 全部早退分支，确认 PRE 与 DEST 之间有几处 return、各自条件是什么，以及 `reserved=0x1 doRes=1` 会命中哪一个。**在此之前不改行为代码。**

### 11.7 疑点 REG-289-C（新登记）：目标车底 24 自己也在动，且被 `RESV-NOBOOK` 冻结

尾部 `SPDD veh=24` 连续十余行，`tile=264,234/265,234`、`x=4236→4240`、`from=27→0` ⇒ 目标车底 24 在**向 21 的方向缓慢移动并最终停稳**；随后 `TRP veh=24 ... type=17(WAIT_COUPLE) => ok=0 res=0`，再 `RESV-NOBOOK veh=24 tile=265,234 order=17 caronly=0 reason=parked-waiter`。

⇒ KI-208 的"停稳等待者不订路"门禁对 24 生效（`caronly=0` 但仍命中，因为 `IsPrimaryVehicle() && IsType(OT_WAIT_COUPLE)`），24 从此不会再动。

这与 §9.5 记录的"若握手要求对方也走过来则会永久死锁"一致，但**本案不需要对方动**（21 应主动开过去），所以不是主因，只登记。

### 11.8 本轮结论（两句话）

1. **`CPL-SCAN found=0 / pair-mismatch` 这一层已经不是现场卡点**：配对在 3015 之后稳定，之后没有任何一次挂接寻路（`CPL-ENTRY`/`CPL-SCAN` 双证）。
2. **新唯一卡点 = `TrainCoupleHandler()` 的候选搜索返回 nullptr**（`COUPLE-WAIT-OFFDEST veh=21`，`mf=255,233` 不在站台格、`real=12`、`destStn=12`），它使 `DoTrainCouplePathfind()` 永不被调用 ⇒ 不寻路 ⇒ 不预留 ⇒ `TRP ok=0 res=0` ⇒ 机车原地不动。

### 11.9 防吞（本轮取数纪律，避免重走"日志没有=没触发"的坑）

| 探针 | 抑制方式 | 本轮读数是否可信 |
|---|---|---|
| `CPL-ENTRY`/`CPL-PATHFOUND`/`CPL-SCAN` | **无节流**，`FindNearestCoupleTrain()` 每次调用必打 | 可信，**0 条 = 真没调用** |
| `CPL-WHY` | `r3r_why_seen <= 16`（进程级，含重复） | 6 条已够，但**上限会吞**；若下次需按 tick 判别须改成按 tick 打 |
| `CPL-SKIP` | `st.logged` 每个节流段只打 1 行 | **不能按条数推调用次数**，只能证明"这一段在节流中" |
| `COUPLE-WAIT-OFFDEST` | 未见节流（3 条/2 个 tick 组） | 可信 |
| `RESV-EXT-PRE` | 未见节流（2 条对应 2 次 doRes 变化） | 可信 |
| `R3RDbgEdge(128 帧窗口)` | 影响 `FOLDCHK`/`SKIP-STOPPED`/`TTB-PROBE` 等 | 本轮未用这些下结论 |
| `COUPLE-FAIL` | 每次失败打（含订单 dump） | 可信 |

---

## 十二、第 289 轮续五（2026-10-23 22:2x）：**调用条件查明并落地修复**（KI-457）

### 12.1 玩家本轮的定性（决定性，直接指定了取证方向）

> "失败原因清楚了：u = GetCouplePosition() 要求车与目标精确对接，3x3 触摸扫描只检查 v->tile 周围 9 格；而机车在 250,233、目标在 265,234，相距 15 格，所以 u 必然为 nullptr。真正该把车开过去的是挂接寻路，但它没有被调用。查它的调用条件。"

⇒ §11.5 把卡点记成"`TrainCoupleHandler()` 的候选搜索返回 nullptr"是**看错了层**：`GetCouplePosition()`（9 格触摸）只服务"已经贴上了、只差最后一格握手"的收尾，它返回 nullptr 是 15 格距离下的必然，属**症状**。真正负责"把车开过去"的是 `DoTrainCouplePathfind()`，而它确实一次都没被调用（`CPL-ENTRY`/`CPL-SCAN`/`CPL-PATHFOUND` 三探针在 3015 之后全 0，且这三者是无节流入口打点）。

### 12.2 调用条件查明：`DoTrainCouplePathfind()` 唯一调用点被两处早退挡死

`DoTrainCouplePathfind()` 只有一个调用点，在 `TryPathReserveCore()` → `ChooseTrainTrack()` 返回之后：

```
src\train_cmd.cpp:16260:   Track path_found = DoTrainCouplePathfind(consist, true);
```

它处在 `ChooseTrainTrack()` 的**成功分支**之后，因此任何一条"提前 return"都能让它永不可达。现场命中的是第一条（已由探针互证）：

**(甲) 15995 行"已有预留即早退"（现场命中，实证）**

```
src\train_cmd.cpp:15992-15995
const bool r3r_couple_stuck_retry = consist->current_order.IsType(OT_GOTO_COUPLE) && ...
if (res_tracks != TRACK_BIT_NONE && !r3r_couple_stuck_retry) return { FindFirstTrack(res_tracks), result_flags };
```

现场读数（本轮第 4 份日志，逐字）：

```
1257: RESV-EXT-PRE veh=21 tile=256,233 enterdir=2 tracks=0x1 reserved=0x1 doRes=1 real=12 spd=0
805:  TPR-WALK veh=21 mf=255,233 origin=255,233 td=8 okay=1 exit=2 new=256,233 reach=1 spd=0 one=0 look=0
807:  TRP veh=21 mf=255,233 real=12 type=16 dest=479497 spd=0 ... => ok=0 res=0
```

- `enterdir=2` = `DIAGDIR_SW`，`_exitdir_reaches_tracks[DIAGDIR_SW] = TRACK_BIT_X|UPPER|RIGHT`（`src\table\track_data.h`），`tracks=0x1`（`TRACK_X`）落在其中 ⇒ `res_tracks = 0x1 & 0x1 = 0x1 != TRACK_BIT_NONE` ⇒ **必命中 15995 的早退**。
- 早退返回 `result_flags` 未置 `CTTRF_RESERVATION_MADE` ⇒ 上游 `TRP ... => ok=0 res=0`。
- 因为 return 发生在 16024 的 `ExtendTrainReservation()` 之前，所以 **`RESV-EXT-PRE` 有 2 条、`RESV-EXT-DEST` 恒 0 条**（§11.6 的疑点 REG-289-B 由此结案：不是 Extend 内部早退，是**根本没进 Extend**）。
- 同理 `CPL-ENTRY` 恒 0（16260 不可达）。

死锁闭环：车被 `MarkTrainAsStuck` 停住（`cur_speed==0` ⇒ 不进 `TrainController`）→ stuck 重试调 `TryPathReserve` → `ChooseTrainTrack` 命中早退 → `ok=0 res=0`、既不 mark 也不重算 → 路径永不更新 → 车钉死在预留末端前一格（250,233 与 mf 255,233 之间）。

**(乙) 16096 行"预留失败即早退"（同族第二道门，必须一并放开）**

```
src\train_cmd.cpp:16090-16096
if (res_dest.tile == INVALID_TILE) {
    if (!r3r_couple_stuck_retry) { /* mark_stuck + 信号改回红 + return */ }
}
```

`ExtendTrainReservation()` 一旦失败（最典型：**等待车底的平台预留被当成障碍**——这正是挂接寻路存在的唯一理由，见 16152-16158 的既有注释），代码在 16096 就 return，**排在 16260 之前**。也就是说：即使放开 (甲)，机车越接近目标越必然在 (乙) 被踢回 ⇒ 只修 (甲) 仍会死锁，必须同判据一并放开。

### 12.3 落地（仅 `src\train_cmd.cpp`，未碰任何 `src\*.h`）

判据一次算清、两处复用：

```cpp
const bool r3r_couple_stuck_retry = consist->current_order.IsType(OT_GOTO_COUPLE) &&
        !consist->current_order.GetCoupleIsDepot() && consist->cur_speed == 0;
```

- **(甲) 15995**：`if (res_tracks != TRACK_BIT_NONE && !r3r_couple_stuck_retry) return ...`
- **(乙) 16096**：`if (!r3r_couple_stuck_retry) { mark_stuck; 信号改红; return ... }`（放行时保持刚点绿的信号，与 16130 成功出口口径一致；挂接寻路自身失败仍走 KI-316 的 `FreeTrainTrackReservation + MarkTrainAsStuck`，与既有失败路径逐字相同）

**语义边界（刻意收窄）**：
- 只对「挂接订单（`OT_GOTO_COUPLE`）」放行；普通 `GOTO_STATION`/`GOTO_DEPOT`/`WAYPOINT` 的早退逐字不变。
- 只对「非车库型挂接」放行；`GetCoupleIsDepot()` 为真仍走普通寻路（KI-320 口径）。
- 只对「已停住（`cur_speed == 0`）」放行；**行驶中**的车辆不受影响（避免每 tick 白白重算 + 抖动）。
- 未改 `ExtendTrainReservation()` / `FindNearestCoupleTrain()` / yapf 任何实现。

### 12.4 构建自证（第 289 轮续五）

- 复用既有 `_tmp_inc_build.cmd`（**未新建任何 .cmd**）
- `build\R3R_incbuild.log` → `GUARD: incremental is safe`；`[3/3] Linking CXX executable openttd.exe`（1 次）；`error C*`/`fatal error`/`FAILED:` 计数 **0**
- `build\R3R_incbuild.done` = `EXIT_CODE=0`
- 时间戳三元组：`src\train_cmd.cpp` 22:20:39 → `train_cmd.cpp.obj` 22:21:27 → `build\openttd.exe` 2026-10-23 22:23:43（52 298 752 B）
- `read_lints` on `src\train_cmd.cpp`：0 条
- 未新增日志字面量（`RESV-EXT-PRE`/`RESV-EXT-DEST` 已在上一版 exe 内），故本轮不做 exe 字面量核对

### 12.5 复测判据（5 条，按重要性排序）

| # | 判据 | 期望读数 |
|---|---|---|
| 1 | **`DoTrainCouplePathfind()` 终于被调用** | 出现 `CPL-ENTRY veh=21 tile=250,233 orderType=16` 且其后有 `CPL-SCAN`/`CPL-PATHFOUND`（3015 之后的新条目） |
| 2 | 预留延伸这次真的跑了 | 出现 `RESV-EXT-DEST ... bad=?`（**从 0 条变为非 0**，直接证明 (乙) 生效） |
| 3 | 机车开始向车底移动 | `TRP veh=21 ... => ok=1 res=1` 或 `cur_speed != 0`；`CPL-PAIR` 的 `actTile` 从 250,233 向其东侧推进、`dist=16` 单调下降 |
| 4 | 不出现"铺→清→铺"抖动 | 不出现 `RESV-EXT-DEST bad=1` 与 `TRP ok=0 res=0` 交替且位置不变的循环；`CPL-BEST` 的 `tile` 与 `TRP` 的 `mf` 不来回跳 |
| 5 | 负判据（不得回归） | 非挂接订单（普通 `GOTO_STATION`/`GOTO_DEPOT`/`WAYPOINT`）的 `RESV-EXT-PRE ... doRes=1` 后**仍然**早退（`TRP ok=0 res=0` 且无 `RESV-EXT-DEST`）；车库型挂接订单（`GetCoupleIsDepot()=1`）行为不变；行驶中（`spd>0`）的 `RESV-EXT-PRE` 仍早退 |

### 12.6 未确认项（登记，不阻塞复测）

- **U-289-F**：`reserved=0x1` 是**自己**的预留还是**等待车底**的？（`FollowTrainReservation` 返回 `origin=255,233 okay=1`，说明 255,233 是我方预留末端；256,233 仍带 `TRACK_X` 预留位 ⇒ 可能是上一轮挂接寻路铺下、未被回收的存量位，与 KI-346/KI-345 家族同源）。若复测出现判据 4 的抖动，即指向这里。
- **U-289-G**：(乙) 放行后 `ExtendTrainReservation()` 失败时会被 `TemporarySlotState`/`do_track_reservation` 的清理路径回收多少已铺预留，未取数；若出现"`bad=1` 但地图上留下黑色预览"，按 KI-203d 的残位口径处理。
- **U-289-H**：`CPL-SKIP ... retryIn=2 holdAge=6` 的节流（KI-316 的 `R3RCoupleScanHold`）是否会让判据 1 的首次调用晚 2 tick —— 仅影响"多久看到"，不影响"是否看到"。

### 12.7 实机复测：**通过**（2026-10-23，玩家口述 + 新日志逐条核对）

- 玩家原话：「我太感动了，列车挂接寻路又恢复正常了」。
- 判定：§12.5 判据 ①（`DoTrainCouplePathfind()` 重新可达）与 ③（机车自行开向目标车底、最终完成挂接）**成立**。⇒ KI-457 的两道门（`train_cmd.cpp:15995` 已有预留早退、`:16096` `ExtendTrainReservation()` 失败早退）就是 §11 之后"车钉死不动"的完整根因，第 289 轮续五的修复闭环。
- **核对依据**：玩家补交的新日志 `build\R3R_debug.log`（1855 行 / 252 849 B / 2026-10-23 22:25:49），产生者 `build\openttd.exe` 52 298 752 B @22:23:43 —— 与 §12.4 构建自证里的 exe 大小逐字节相同，即**本轮修复版**，采样有效。

| # | §12.5 判据 | 结果 | 证据（日志行号，逐字） |
|---|---|---|---|
| ① | `DoTrainCouplePathfind()` 重新可达 | **通过** | `:1290 RESV-EXT-PRE veh=21 tile=256,233 enterdir=2 tracks=0x1 reserved=0x1 doRes=1 real=12 spd=0`（**与修复前必命中早退的那行读数逐字相同**）→ `:1291 CPL-ENTRY veh=21 tile=250,233 orderType=16 dontReserve=0`。计数：`CPL-ENTRY=3`、`CPL-SCAN=3`、`CPL-PATHFOUND=3`（修复前恒 0） |
| ② | `RESV-EXT-DEST` 由 0 变非 0 | **判据口径失效（非失败）** | 仍 0 条（bad=1 与 bad=0 皆 0）。见 §12.9 疑点 REG-289-C：`ExtendTrainReservation()` 有「`tile` 有效 + `okay==false`」第三态，既不进 `16061`（bad=1）也不进 `16103`（bad=0），直接在 `16102` 落到挂接分支 ⇒ 全 0 恰是放行生效的伴随现象 |
| ③ | 机车向车底移动、`dist` 单调下降 | **通过** | `:1372 CPL-RESERVE veh=21 reserved=1 next=8` → `:1373 TRP veh=21 mf=255,233 real=12 type=16 dest=479497 spd=0 fp=0 look=1 one=0 mstuck=0 fto=0 => ok=1 res=1`（修复前恒 `ok=0 res=0`）→ `CPL-PAIR dist=13→12→11→10→9→8`、`actTile 253,233→…→258,233` → `:1457/1468/1482/1499 RESV-EXT-PRE ... spd=96/106/114/127` → `:1575 COUPLE-OK loco=21 rear=50 consist=24 co=0 real=12`（**挂接完成**） |
| ④ | 不出现"铺→清→铺"抖动 | **通过** | `:1296 CPL-PATHFOUND found=1`、`:1297 CPL-SCAN tick=3017 ... why=acc-wait` 后直接 `:1372 CPL-RESERVE`；无 `TRP ok=0` 与 `ok=1` 在同一位置交替；`:1807 TRP veh=21 mf=270,234 ... type=1 => ok=1 res=1` 为挂接后转普通订单，属正常 |
| ⑤ | 负判据（非挂接/车库型/行驶中不回归） | **通过** | `CPL-ENTRY` 全日志仅 3 条、`orderType` 全为 `16`（`OT_GOTO_COUPLE`），无一条来自其它订单；`RESV-EXT-PRE` 6 条中 `:893 ... doRes=0 spd=5` 与 `:1457/1468/1482/1499 spd=96/106/114/127` 全为 `doRes=0`（未进放行面）；唯一 `doRes=1 spd=0` 的 `:1290` 正是挂接机车 |

- **顺带结清**：`:1225/1256/1284/1289 TRP veh=21 ... => ok=0 res=0` 全部早于 `:1290`，即 tick 3016 及之前仍失败、tick 3017（`:1297 CPL-SCAN tick=3017`）起成功 —— 与"放行判据在停住（spd=0）时刻才生效"逐字吻合。
- 结论修正：§11.5「新唯一卡点 = `TrainCoupleHandler()` 的候选搜索返回 nullptr」**作废**（那是 15 格距离下的必然症状）；真卡点是本节的 `ChooseTrainTrack()` 两道早退。§12.2 的因果链（早退 ⇒ 无 `RESERVATION_MADE` ⇒ `TRP ok=0 res=0` ⇒ `MarkTrainAsStuck` ⇒ 车钉死）与实机结果一致。
- 遗留（不阻塞）：U-289-F/G/H 三项仍未取数；KI-457 的状态已在 `R3R_KNOWN_ISSUES.md` 同步为"已修 + 已编译 + 实机复测通过（判据①③④⑤通过、②判据口径失效）"。

### 12.9 疑点 REG-289-C：`RESV-EXT-DEST` 全 0 的第三种解释（已登记，待取证）

- **期望值**：§12.5 判据② 预判"放行后 `ExtendTrainReservation()` 会失败 ⇒ 出现 `RESV-EXT-DEST bad=1`"，于是把"EXT-DEST 由 0 变非 0"当作第二道门生效的证据。
- **实测值**：新日志 `RESV-EXT-DEST` **0 条**，bad=1 与 bad=0 **都没有**，而车已经成功开走并完成挂接（`:1575 COUPLE-OK loco=21 rear=50`）。
- **为什么存疑**：两个探针分别位于 `src\train_cmd.cpp:16074`（`res_dest.tile == INVALID_TILE` 分支）与 `:16126`（`res_dest.okay` 为真的分支），**覆盖不到**「`res_dest.tile` 有效但 `okay == false`」这个第三态 —— 该态在 `:16102` 越过两个 if，直接落到挂接分支，于是两个探针都不打。
- **最可能的原因**：`:1290` 那次 `ExtendTrainReservation()` 返回了"路径延伸到某格、但终点未被确认"的 `tile` 有效 / `okay=false` 结果（与 REG-289-D「同一起点跨 tick 结果翻转」同源：`okay` 取决于终点前后有无安全位/长预留）。
- **还需什么数据**：在 `:16102` 之后补一行 `RESV-EXT-MID veh= tile=x,y okay= dtd= lr=`（纯探针、零行为改动），一次即可判定是第三态，还是"根本没进 `16060`"。
- **对判据的修正**：判据②作废；"第二道门是否放开"以后一律用 **`CPL-ENTRY` 出现 + `TRP ok=1 res=1`** 判定（本轮两者均实证）。
