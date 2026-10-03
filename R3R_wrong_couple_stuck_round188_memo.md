# R3R 第 188 轮临时分析报告
## 「去指定车库挂车的机车在车站挂上了刚解下来的车底」＋「机车卡死一段时间（玩家手动掉头才解除）」

- 日期：2026-10-01；现场 `build\R3R_debug.log`。
- 本轮**未改任何源码**（纯取证定性），无构建自证。
- KI 条目：`R3R_KNOWN_ISSUES.md` 第 188 轮小节（KI-288 / KI-289 / KI-290）。

---

## 一、结论速览

两条报告都成立，且**互为因果**：

1. **挂错车底成立**。闸门 `R3RCoupleAllowedIgnoringPair()`（`couple_group.cpp:279-319`）的**车库分支**只拒绝「站在**别的车库**格上的候选」，候选站在**车站格**上就放行 ⇒ `GOTO_COUPLE → 车库 1,11` 的机车合法挂上了停在**车站** `4,11` 的车底 23。→ **KI-288**
2. **卡死成立，分两段**。
   - 段一：车库 1,11 那边根本没有等待车底（`CPL-PATHFOUND found=0`），按 KI-193 机车**原地等待**，于是 `stuck=1`；引擎「Handle stuck trains」块（`train_cmd.cpp:16808-16821`）随后**自己把机车掉头**（`rev=0`）——这一掉头正是报告 1 的扳机。→ **KI-289**
   - 段二：错挂后 27 节链变成 `db=1` 且领车端是**没有驾驶室的货车 0 号**，时间表索引还与真实索引脱钩（`tt=28 ≠ real=2`）；此后 ~735 行日志里该车**零 CRT/TRP/CPL 遥测、只挪了 4 格**，直到玩家手动 `rev=1` 掉头才恢复。→ **KI-290**

一句话：**等挂的机车被引擎卡死自愈逻辑掉头 → 车尾正好贴上 8px 外、且已进入 WAIT_COUPLE 的车底 → 几何撞挂路径放行（因为目的地闸门的车库分支是洞）→ 挂错后整链倒向 + 领车端无驾驶室 + 索引脱钩 → 继续卡死。**

---

## 二、时间线与行号锚点

带行号者为本轮 `Select-String` 直读；`9416–9545` 与 `10195+` 为 `Get-Content -Skip/-First` 连续窗口。

### A. 在车站 4,11 解挂（`9416` 起）

```
9416  DECOUPLE-FIRE consist=26 tx=4 ty=11 real=26 mode=2 num=1 segs=2 eff=23 plat=27/0
      GEO settle-decouple-u n=0  veh=23 tile=4,11  x=72  y=184 dir=1 db=0 ... segid=2
      GEO settle-decouple-u n=23 veh=0  tile=10,11 x=165 y=184 dir=1 db=0 ... segid=3
      DECOUPLE-JUMP fire_real=26 dec_idx=26 wait_idx=65535 step=-1 v=26 u=23 n_u=29
      DECOUPLE-DONE u=23 co=1 real=27 tx=4 ty=11 x=72 y=184
```

关键几何（整条链的起点）：车底 23 头车停在 `4,11 x=72`，机车 26 在 `4,11 x=64` —— **同格、相距 8px**；车底 23 整条 24 节沿 **+x** 铺到 `10,11 x=165`。

### B. 机车发现车库无车底，原地等（`9510–9545`）

```
      LOCO-AFTER-DECOUPLE veh=26 curType=0 real=0 idxnext=16 tile=4,11
        L-ORD 0 type=16 dest=1    ← GOTO_COUPLE → 车库 1,11
        L-ORD 1 type=6  dest=3 / L-ORD 2 type=6 dest=4 / L-ORD 3 type=16 dest=2
      TRP veh=26 mf=4,11 real=0 type=16 dest=1409 spd=0 fp=0 look=1 one=0 mstuck=1 fto=1 => ok=0 res=0
9537  DEPOT-ARR veh=26 spd=0 real=0(16) curType=16 stuck=1 tileDepot=0 tx=4 ty=11
              destTx=1 destTy=11 destDepot=1 destResv=0 tileEqDest=0 depotDir=2 enterDir=0 tt=0 impl=0
      CPL-ENTRY veh=26 tile=4,11 orderType=16 dontReserve=0
      CPL-ORIGIN veh=26 origin=4,11 td=0 vehDir=1 vehTd=0 dontReserve=0
      CPL-PATHFOUND veh=26 found=0                       ← 车库 1,11 无可挂车底
      CPL-S0-CHK veh=26 tile=4,11
[R3R] CPL-GATE reject=target-not-wait site=geo  act=26 tgt=23 aOrd=16 tOrd=0 aStop=0 tStop=0 grp=0
[R3R] CPL-GATE reject=target-not-wait site=scan act=26 tgt=23 aOrd=16 tOrd=0 aStop=0 tStop=0 grp=0
      COUPLE-FAIL loco=26 order=16 tx=4 ty=11 x=64 y=184
```

- `destDepot=1 destTx=1 destTy=11` ⇒ 命令目的地确实是**车库 1,11**；机车在车站格（`tileDepot=0 tileEqDest=0`）。
- 此刻**闸门是对的**：车底 23 `tOrd=0`（= `OT_NOTHING`）⇒ `CPL-GATE reject=target-not-wait`，两条路径全拒。

### C. 车底 23 拿到 WAIT_COUPLE（`9626–10152`）

```
9626  SVC-DEPOT-SKIP veh=23 cur=17 real=27(17) tile=4,11 tag=couple-protocol   （cur=17 = OT_WAIT_COUPLE）
```

⇒ 停在同格 8px 外的车底 23 从此成为**合法候选**。

### D. 引擎把等挂机车自动掉头（`10164`）

```
10164 REVERSEDIR veh=26 tile=4,11 dir=1 order=16 spd=0 nv=3 rev=0 stuck=1 db=0
```

`stuck=1` 且 `rev=0`（**非玩家**）⇒ 即 `train_cmd.cpp:16808-16821` 的卡死自愈掉头（`turn_around = wait_counter % (wait_for_pbs_path × DAY_TICKS) == 0 && reverse_at_signals`）。掉头后**车尾 24 朝向 +x**，正是车底 23 所在方向；`nv=3` 说明机车此刻只有自己 3 节。

### E. 撞上就挂（`10178–10184`）

```
10178 COUPLE-PREFLIGHT head=26 last=24 fold=0 dirfold=0 gap=3 pair=(-1 dir=15,-1 dir=15) mixed=0 onsplice=0 splice=(24 dir=1, 23 dir=1) spd=0 db=1
10180 NOCAB-SET this=26 db=1 last=0 lastSub=0x04 lastEng=0 lastLead=0
10184 COUPLE-OK loco=26 rear=0 consist=23 co=1 real=28 type=7 tx=4 ty=11 x=64 y=184
      ORD-AFTER-COUPLE head=26 n=29 real=28 impl=28 tt=28 borrowed=1 owner=23
```

`gap=3`（半车长内）⇒ 几何命中；`fold=0 dirfold=0` 不折叠 ⇒ 放行；**挂接点在车站 `4,11`**，而命令写的是车库 `1,11`；`borrowed=1 owner=23` ⇒ 合链后用 23 的命令表，索引跳到 28（`type=7` = `OT_CONDITIONAL`）。

### F. 第二段卡死（`10248–11083`）

```
10248 NOCAB-SET this=26 db=1 last=0 lastSub=0x04 lastEng=0 lastLead=0
10250 DB-CLEAR head=26 staleNocab=1 last=0 lastLead=0
10283 CHAIN-ATTRS2 couple head=26 len=103 pow=4187 wt=808 spd=80 nocab=0 db=0
10284 REVERSEDIR veh=26 tile=4,11 dir=1 order=1 spd=0 nv=27 rev=0 stuck=1 db=0    ← 又一次自动掉头
10312 REVERSEDONE db=1 mvfront=0                                                    ← 领车端 = veh 0（货车）
10340 NOCAB-SET this=26 db=1 last=0 lastSub=0x04 lastEng=0 lastLead=0
10341 CRT veh=26 order=1 dir=1 origin=21,8 td=8 found=1
10346 DEPOT-ARR veh=26 spd=0 real=2(1) curType=1 ... tt=28 impl=2 tileEqDest=0
10347 TRP-AUTO veh=26 real=2 type=1 dest=1176 ok=1 res=1 stuck=0 wc=0
10348..11082  TT-CHK-BREAK pre-processorders veh=26 real=2 tt=28 impl=2 n=29 curType=1   （每 tick 一行，无其它遥测）
11083 REVERSEDIR veh=26 tile=8,11 dir=1 order=1 spd=0 nv=27 rev=1 stuck=0 db=1          ← rev=1 = 玩家手动掉头
11139 CRT veh=26 order=1 dir=1 origin=2,11 td=0 found=1
```

- `10250+10283` 有一段**健康正向**（`nocab=0 db=0`），车就是在这段从 `4,11` 挪到 `8,11`。
- `10284` 再次 `rev=0` 自动掉头 ⇒ `10312 REVERSEDONE db=1 mvfront=0`：领车端成了 **0 号车**，而它是 `lastSub=0x04 lastEng=0 lastLead=0`（普通货车，**无驾驶室**）。
- `10346` 起 `real=2` 对 `tt=28` ⇒ KI-94 时间表索引脱钩，每 tick 打 `TT-CHK-BREAK`。
- `10347→11082`：该车**无任何 CRT/TRP/CPL 行**，位置仅 `4,11 → 8,11`。`11083` 玩家 `rev=1` 掉头后 `mvfront=26`（领车端回到真机车）⇒ `CRT found=1` ⇒ 恢复。

---

## 三、根因 1：车库目的地的闸门分支有洞 —— KI-288

```305:319:src/couple_group.cpp
if (order.IsType(OT_GOTO_COUPLE)) {
    const DestinationID dest = order.GetDestination();
    if (order.GetCoupleIsDepot()) {
        // Depot order: only a candidate standing on a tile of the named depot is refused here.
        if (IsRailDepotTile(target->tile) && GetDepotIndex(target->tile) != dest.ToDepotID()) return false;
    } else {
        if (IsRailStationTile(target->tile) && GetStationIndex(target->tile) != dest.ToStationID()) return false;
    }
}
```

**不对称**：车站分支拒「候选站在别的车站」；车库分支只拒「候选站在别的**车库**格」，候选站在**车站**格时 `IsRailDepotTile()` 为假 ⇒ **放行**。注释本意是放行「车库门口那段已经不是车库格的接近轨道」，实现却放大成「非车库格即放行」，把车站平台也包括进来了。

镜像副本同样如此（它只给探针行选标签，但注释要求同步）：

```10819:10825:src/train_cmd.cpp
static bool R3RCoupleTargetAtOrderStation(const Train *coupler, const Train *target)
{
	const Order &order = coupler->current_order;
	if (!order.IsType(OT_GOTO_COUPLE) || order.GetCoupleIsDepot()) return true;
```

**为何现在才暴露**：该闸门是 KI-165（2026-09-22）加的，此前现场只有「车站命令挂错车站」；「车库命令 + 候选停在车站」还需候选恰好落在 8px 内（解挂刚把车底卸在鼻子底下），组合很窄。

**修法方向（未实现）**：车库分支补一支 `if (IsRailStationTile(target->tile)) return false;`（车站绝不属于任何车库）；并同步改 `R3RCoupleTargetAtOrderStation()`，删掉「车库命令 ⇒ 直接 true」。若要保留车库门口接近轨道的宽容，应显式白名单化而非「非车库格即放行」。**注意**：这只让错挂不可能发生，不解决「机车被自动掉头」，两条须一起做。

---

## 四、根因 2：原地等挂的机车被卡死自愈逻辑掉头 —— KI-289

KI-193 只实现了「不推命令」，**没有**保护等待中的机车不被引擎挪走。等待态在引擎眼里就是「无预留、不能前进」⇒ `ok=0 res=0 mstuck=1` ⇒ `stuck=1`（`9537`）。引擎随后：

```16808:16821:src/train_cmd.cpp
	/* Handle stuck trains. */
	if (!mode && consist->flags.Test(VehicleRailFlag::Stuck)) {
		++consist->wait_counter;
		bool turn_around = consist->wait_counter % (_settings_game.pf.wait_for_pbs_path * DAY_TICKS) == 0 && _settings_game.pf.reverse_at_signals;
		...
		if ((path_result & TPRRF_RESERVATION_OK) == 0) {
			if (turn_around || (path_result & TPRRF_REVERSE_AT_SIGNAL)) ReverseTrainDirection(consist);
```

`wait_counter` 攒够后 `ReverseTrainDirection()` ——即 `10164` / `10284` 的 `REVERSEDIR ... rev=0 stuck=1`。

**修法方向（未实现，需拍板）**：在 KI-193 认定「到点且目的地无等待车底」的原地等待态里，让该车退出卡死自愈（例如该状态下不 arm `turn_around`，或把 `Stuck` 语义与「命令性等待」区分开）。**不要把 `stuck=1` 简单清掉**——`Stuck` 还被预留/PBS 逻辑读。

---

## 五、根因 3（KI-290）：错挂之后的整链僵死

现象与事实：`REVERSEDONE db=1 mvfront=0`（领车端是无驾驶室货车 0 号）+ `NOCAB-SET ... lastSub=0x04 lastEng=0` + `tt=28 ≠ real=2`，此后 ~735 行无 CRT/TRP/CPL、只挪 4 格，玩家手动掉头即恢复。

需要强调两点，避免误判：
- `TCF_NO_DRIVING_CAB` **只把速度上限压到 32 km/h**（`train_cmd.cpp:460-473` 一带），**不会把速度置 0**；所以「无驾驶室」本身不足以解释「完全不动」。
- `NOCAB-LIMIT` 是很早前的一次性 static 日志（本日志仅在 `1816/1820` 行对 `veh=27` 打过一次），**该车没有 `NOCAB-LIMIT` 行不代表没触发**，不能当证据。

候选机制（**未证实，见 U-1**）：①`db=1` + 领车端无驾驶室 + 27 节长链导致逐 tick 反复重算/重试而不产生遥测；②`borrowed=1 owner=23` 的借用索引 + `tt/real` 脱钩让订单推进在 `TT-CHK-BREAK` 处空转；③两者叠加。判据与取证方案见 `R3R_KNOWN_ISSUES.md` 的 KI-290。

---

## 六、未确认项

- **U-1**：段二到底是「完全静止」还是「32 km/h 慢爬」。日志没有全局 tick 计数，`DEPOT-ARR`/`CRT`/`TRP` 都是边沿触发（慢爬不会打行），只能证明「无新增预留/无路径事件」，不能区分二者；该车确实从 `4,11` 到了 `8,11`（4 格）。需下一轮补只读探针（每 N tick 打 `cur_speed/db/GetMovingFront/cur_real/tt/mvfront`）才能定论。
- **U-2**：`9537` 就 `stuck=1` 是 KI-193 原地等待的**预期形态**还是另有第二个预留失败原因（`destResv=0` 只说明没预留）。从 `CPL-PATHFOUND found=0` 看属预期，但同一 tick 内是否还有其它 `TryPathReserve` 失败未取到证据。
- **U-3**：`wait_for_pbs_path × DAY_TICKS` 在玩家设置下具体是多少 tick 未查（不需要，但可解释「等多久才掉头」）。
- **U-4**：KI-284/285/286/287 的复测仍未做，与本条互不阻塞。

---

## 七、下一步（修法未实现，本轮只取证）

1. **KI-288**：`couple_group.cpp:305-311` 车库分支补「候选在车站格 ⇒ 拒」，同步 `train_cmd.cpp:10819-10825`。**这是让「车位级错挂」不可能发生的最小改动。**
2. **KI-289**：让 KI-193 的原地等待态免于卡死自愈掉头。**这条不做，机车仍会被无意义掉头**（掉头后 `depotDir/enterDir`、目的地关系全乱），只是不再挂错。
3. **KI-290**：先补 U-1 的只读探针取数，再定性。**在拿到 U-1 之前不要动速度/刹车逻辑**（历史教训：KI-265 的「拒绝分支清 cur_speed」会让机车停在挂车距离外，永远挂不上）。
4. 复测判据（三条，详见 KI 条目）：
   - 同场景不再出现「命令写车库、`COUPLE-OK` 打在车站」；
   - 等待态机车不再出现 `REVERSEDIR ... rev=0 stuck=1`（或出现后立刻被我们的逻辑拦回）；
   - 错挂不再发生时，`tt/real` 脱钩与「~735 行零遥测」随之消失。

---

## 八、落地（第 188 轮下半段，KI-288 + KI-289 已实现并编译）

> 玩家拍板：KI-289 的口径是**自愈两次再停止**（既不是一次也不许自愈，也不是永久掐死）。

### 8.1 KI-288：闸门两处对称补洞（仅两个 .cpp，未碰 .h）

| 文件 | 位置 | 改动 |
| --- | --- | --- |
| `src\couple_group.cpp` | `R3RCoupleAllowedIgnoringPair()` 车库分支（原 `:305-311`） | 在「别的车库格 ⇒ 拒」之后补 `if (IsRailStationTile(target->tile)) return false;`。车站格绝不属于任何车库。 |
| `src\train_cmd.cpp` | `R3RCoupleTargetAtOrderStation()`（原 `:10819-10825`） | 车库命令原为**无条件 `return true`**，改为镜像判断：候选在车站格 ⇒ `false`；否则 `!IsRailDepotTile(...) || GetDepotIndex(...) == dest.ToDepotID()`。 |

- **宽容没有丢**：车库门口那段「已非车库格」的接近轨道是 plain rail，两条判断都放行。
- 车站命令那一支一字未动（`:317` 的对称拒绝本来就在）。

### 8.2 KI-289：「等一个不存在的车底」时把引擎自愈限制在 2 次（仅 `src\train_cmd.cpp`）

**常量与状态**（紧邻 `R3R_COUPLE_COMMIT_FAIL_LIMIT = 4`）：

```cpp
static const uint32_t R3R_COUPLE_WAIT_HEAL_LIMIT = 2;
static btree::btree_map<VehicleID, uint8_t>  _r3r_couple_no_target;   ///< 本 tick 处于「停车 + GOTO_COUPLE + 一个候选都没有」
static btree::btree_map<VehicleID, uint32_t> _r3r_couple_wait_heals;  ///< 已用掉的自愈次数
```

**生产者**——`TrainCoupleHandler()` 的 `if (u == nullptr)` 且 `!r3r_rolling` 分支（「精确位置没命中 + 9 格触碰扫描零候选」）打标记：

```cpp
_r3r_couple_no_target[v->index] = 1;
```

- **刻意不要求 `R3RCoupleOrderDestinationReached()`**：本轮现场机车停在**车站** 4,11，命令目的地却是**车库** 1,11（`tileEqDest=0`），按「已到点」判根本覆盖不到。
- 标记**持久**（非到点停驻时每 tick 重打）；`u != nullptr`（候选出现）或订单不再是 `OT_GOTO_COUPLE` 时清除，后者同时清 `_r3r_couple_wait_heals`，让下一次约会从两次额度重新开始。

**消费者**——`train_cmd.cpp` 的 "Handle stuck trains" 块（`:16867` 起）：

```cpp
const bool r3r_wait_no_target = _r3r_couple_no_target.count(consist->index) != 0;
uint32_t *r3r_heals = nullptr;
if (r3r_wait_no_target) r3r_heals = &_r3r_couple_wait_heals[consist->index];
else                    _r3r_couple_wait_heals.erase(consist->index);
const bool r3r_heal_exhausted = r3r_heals != nullptr && *r3r_heals >= R3R_COUPLE_WAIT_HEAL_LIMIT;

bool turn_around = ...&& _settings_game.pf.reverse_at_signals;
if (r3r_heal_exhausted) turn_around = false;          // ← 唯一的既有逻辑改动
...
const bool r3r_rev_wanted = turn_around || (path_result & TPRRF_REVERSE_AT_SIGNAL);
if (r3r_rev_wanted) {
    if (r3r_heal_exhausted) { /* 只打一次 COUPLE-WAIT-NOHEAL，不掉头 */ }
    else { if (r3r_heals) ++*r3r_heals; /* COUPLE-WAIT-HEAL n=/limit= */ ReverseTrainDirection(consist); }
}
```

- **非等待态一次都没碰**：标记不存在时 `r3r_heals == nullptr` ⇒ `r3r_heal_exhausted` 恒假 ⇒ `turn_around` 与 `ReverseTrainDirection()` 的调用条件逐字不变。
- `*r3r_heals` 用「超打一格当 log-once 哨兵」（`== LIMIT` 才打 `NOHEAL`，然后 `++`），避免每 tick 刷屏。
- 未做（刻意的）：**没有**清 `VehicleRailFlag::Stuck`（PBS/预留逻辑还读它，见 KI-208）；**没有**动速度/刹车（KI-265 教训）；**没有**碰 `_r3r_couple_dest_idle` 的 500 tick 计时与 KI-193 的 4 次提交失败兜底 —— 那两条仍是独立出口。

### 8.3 构建自证

- `_tmp_inc_build.cmd`（复用，**未新建 .cmd**）；guard = `incremental is safe`；`[4/4] Linking CXX executable openttd.exe`。
- `build\R3R_incbuild.done` = `EXIT_CODE=0`（2026-10-01 20:55:29）；日志内 `error C` / `fatal error` / `FAILED:` / `build stopped` = 0。
- `couple_group.cpp` 20:50:17 → obj 20:54:11；`train_cmd.cpp` 20:53:12 → obj 20:54:09；`build\openttd.exe` 20:55:12（51 566 080 B）。
- `read_lints` 两文件 0 条；exe `findstr` 命中 `COUPLE-WAIT-HEAL` / `COUPLE-WAIT-NOHEAL`。

### 8.4 待复测（游戏内）

1. 同场景：`REVERSEDIR ... rev=0 stuck=1` 最多 2 次，随后一行 `COUPLE-WAIT-NOHEAL ... heals=3`，机车停在原地。
2. 每次自愈前应有 `COUPLE-WAIT-HEAL ... n=1/2 limit=2`。
3. 不再出现「命令 `destDepot=1 destTx/destTy` 指车库、`COUPLE-OK` 的 `tx/ty` 在车站格」（KI-288）。
4. 非等待态（订单不是 `OT_GOTO_COUPLE`，或确有候选）不得出现 `COUPLE-WAIT-HEAL/NOHEAL`，掉头行为逐字不变。
5. KI-290（错挂后的整链僵死）在同一场景下应自然消失；若仍出现，按 U-1 探针取 `spd/db/mvfront` 逐 tick 曲线再定性。**拿到 U-1 之前不动速度/刹车逻辑。**
