# R3R 第 212 轮：GOTO_COUPLE「目的地无可挂目标 ⇒ 永久停驻」修复（KI-316 变体 (a)）+ KI-319 谎报抢锁修复

- 日期：2026-10-03
- 依据：第 210 轮报告 `R3R_hxn5b_stuck_round210_memo.md`（KI-316/317）、第 211 轮追加（KI-319/320）
- 现场：`build\R3R_debug.log`（4150 行，mtime 2026-10-03 15:22:40，第 210/211 轮同一份，未新采）
- 改动文件：**仅 `src\train_cmd.cpp`**（未碰任何 `src\*.h`、未碰 `src\lang\*.txt`）
- 构建：复用既有 `d:\sourcecode of JGRPP\_tmp_inc_build.cmd`（**未新建任何 .cmd**）

---

## 一、本轮做了什么

第 210/211 轮把问题钉在三层（KI-316 高 / KI-317 中 / KI-319 低）后，把修法留成「待玩家拍板」。本轮玩家的指示是「继续未完成的任务」，故按第 210 轮报告 §七 判据直接采纳**变体 (a)：找不到可挂目标时仍按订单目的地驱动**（该行为同时是判据 1 的原文要求），并顺手落地 KI-319 的低风险修法。KI-317 的这一分支被 (a) 在主路径上消解（残余见四）；KI-320 仍需现场数据，本轮不动。

---

## 二、KI-316 根因回顾（第 210 轮已证，本轮复核代码）

`ChooseTrainTrack()`（`src\train_cmd.cpp`）里 `OT_GOTO_COUPLE` 走的是**专用**寻路：

```13359:13377:src/train_cmd.cpp
```

- `DoTrainCouplePathfind()` 的目标是**可挂车底**（`R3RIsCoupleTarget()` + 订单指定的站/库），**不是**订单目的地。
- 现场：目的地根本没有符合的车底 ⇒ `CPL-PATHFOUND found=0` ⇒ `path_found == INVALID_TRACK` ⇒ 旧代码直接 `MarkTrainAsStuck()` + `FreeTrainTrackReservation()` + `return { FindFirstTrack(tracks), result_flags }`（无预留、无移动）⇒ **永久停驻**，运输中断。
- 旧现场两条：veh=51 停在机待线 99,108、订单指向 99,97；veh=57 停在 27,7 库门口（`DEPOT-ARR … destTx=27 destTy=7 tileEqDest=0 stuck=1`）。两者 `tileEqDest=0` 正是因为**动不了**，而 KI-193 的停驻门（`COUPLE-DEST-EMPTY`）以「已到目的地」为前提 ⇒ 也永远够不着，两层互相锁死（= KI-317）。

---

## 三、本轮修复

### 3.1 KI-316（变体 (a)）

`ChooseTrainTrack()` 的 couple 寻路失败分支改为：

1. `MarkSingleSignalDirty(tile, changed_signal)`（沿用原分支的收尾语义）；
2. `FreeTrainTrackReservation(consist, origin.tile, origin.trackdir)` —— 先清掉失败方可能留下的残留，再让普通寻路从干净状态起算；
3. 当 `res_dest.tile != INVALID_TILE && !res_dest.okay` 时，调用**普通寻路** `DoTrainPathfind(consist, …, do_track_reservation, &res_dest, &final_dest)` 驶向订单目的地（与下方「路线只是猜的」分支对其它订单类型做的事逐行同构）；
4. `HandlePathfindingResult(r3r_fb_found)`；若成功 ⇒ `CTTRF_RESERVATION_MADE`（仅当 `do_track_reservation`）+ 返回 `best_track`，并打探针
   `COUPLE-NOTARGET-FALLBACK veh=%d tile=%d,%d dest=%d,%d ord=%d`；
5. 若回退也失败 ⇒ 一字不改地回落原行为：`MarkTrainAsStuck()` + `return { FindFirstTrack(tracks), result_flags }`。

配套新增文件内静态节流表（声明在 KI-193 的 `_r3r_couple_dest_idle` 旁）：

```11610:11618:src/train_cmd.cpp
```

在 `TrainCoupleHandler()` 的非 `OT_GOTO_COUPLE` 早退块里 `erase`（与 `_r3r_couple_no_target` / `_r3r_couple_wait_heals` 同处），保证「离开耦合订单」后下一次进入会重新报一次。

**为什么能落地成"开到、等待、继续"**：到达目的地后由既有 KI-193 停驻门接管 —— `R3RCoupleOrderDestinationReached()` 判到点（按 depot id / station id，不按 tile）⇒ `COUPLE-DEST-EMPTY`，命令保留；满 `R3R_COUPLE_DEST_IDLE_LIMIT`(500) tick 无车底 ⇒ `CmdSkipToOrder` 范式推进订单，列车继续下一段运输。

**安全边界（刻意保留的旧行为）**：只有 couple 寻路**失败**时才回退。"目的地暂时到不了"（等待车底挡路、信号未放行）时普通寻路同样失败 ⇒ 仍 `Stuck` 原地等待重试，不会被导向别处。正常挂车场景 `found=1` ⇒ 根本不进本分支。

### 3.2 KI-319（低，谎报抢锁）

成对扫描候选循环里，在 `if (t->vehstatus.Test(VehState::Stopped)) continue;` 之后、成对锁检查与 `CPL-PAIR-STEAL` 打印**之前**，插入：

```cpp
if (!R3RCoupleTargetAtOrderStation(coupler, t)) continue;
```

`R3RCoupleTargetAtOrderStation()` 是早已存在的静态助手（原本只用于在 `CPL-GATE reject` 探针里挑 `dest-mismatch` / `group-mismatch` 标签），语义与 `R3RCoupleAllowedIgnoringPair()` 的目的地部分逐字一致。准入集合**一字不改**（后者稍后本会以同一判据拒掉这些候选，但那是**在 STEAL 打印之后**）⇒ 只是不再把「注定被拒的候选」广告成「被别人抢锁」。同处把该助手的文档注释改为「兼作候选预筛」。

---

## 四、未做 / 未确认

1. **KI-317 残余未修**：KI-289 的 `COUPLE-WAIT-HEAL` 仍够不着 `found=0`（入口仍需 `pf.reverse_at_signals` 周期命中或 `TPRRF_REVERSE_AT_SIGNAL`）。若目的地本身不可达（回退寻路也失败），机车仍只能 `Stuck` 且无有界自愈。待复测数据再定是否补变体 (b)。
2. **KI-320 未动**：仍需一次「两车同目的地、距离明显不同、进入订单先后相反」的现场日志。
3. **未实机复测**：本轮只到「已实现 + 已编译」。
4. ~~**未确认项 R-1**~~ ⇒ **已于同日静态结案（见 §七.1，结论：会驶到并停在订单目的地）+ 派生新残余 R-1b（见 §七.2）**。
5. ~~**未确认项 R-2**~~ ⇒ **已于同日定性（见 §七.3，结论：可接受残余，最坏配置不引入新回归）**。

---

## 五、复测判据（4 条）

1. 同场景（veh=51 @99,108 → 99,97；veh=57 @21,13 → depot 27,7）应出现 `COUPLE-NOTARGET-FALLBACK veh=51 …` / `veh=57 …`，且机车**开动**驶向目的地（不再停在机待线/库门口）。
2. 到达后应出现 `COUPLE-DEST-EMPTY`（KI-193 停驻门，命令保留）；满 500 tick 后订单被跳过、列车继续后续运输（`real=` 前进）。
3. 同场景不再出现 `CPL-PAIR-STEAL act=57 tgt=6`；若出现则必伴随 `CPL-PAIR act=57 tgt=6`（KI-319）。
4. **无回归**：等待车底确实在目的地时仍是 `CPL-PATHFOUND found=1` → `COUPLE-OK`，且**不出现** `COUPLE-NOTARGET-FALLBACK`；"目的地暂不可达"（挡路/信号）仍 `Stuck` 原地等待重试；`CPL-PAIR` / `CPL-PAIR-STEAL` 的打印条件本身未改，同站争抢语义不回退。

---

## 六、构建自证（第 212 轮）

- 入口：复用既有 `_tmp_inc_build.cmd`（未新建 .cmd）；护栏 `build\R3R_incbuild.guard.log` = `GUARD: incremental is safe (no header/lang file is newer than the newest object)`
- `build\R3R_incbuild.done` = `EXIT_CODE=0`（2026-10-03 18:01:59）
- 日志：`[3/3] Linking CXX executable openttd.exe`；`error C*` / `fatal error` / `FAILED:` / `build stopped` 计数 **0**
- 时间戳链：`src\train_cmd.cpp` 17:50:32 → `train_cmd.cpp.obj` 17:52:11 → `build\openttd.exe` 18:01:35（51 672 064 B）
- `read_lints(src\train_cmd.cpp)` = 0 条
- 产物自证：exe 内命中字面量 `COUPLE-NOTARGET-FALLBACK veh=%d tile=%d,%d dest=%d,%d ord=%d`

**状态 = 已实现 + 已编译，游戏内复测待做。**

---

## 七、R-1 / R-2 静态结案（2026-10-03 追加；纯代码取证，无需现场日志）

把 §四 的 R-1 / R-2 两个「未确认项」用代码证据收口。**只读代码，未改任何源码、未重新构建**（当前版本仍是第 212 轮的 `build\openttd.exe` @18:01:35）。

### 7.1 R-1 = 机车会驶到订单目的地并停下（站台 / 车库两型均已证）——结案

证据链（四处，逐条可复核）：

1. **`GOTO_COUPLE` 订单确实写 `dest_tile`。** `src\order_cmd.cpp` 的 `UpdateOrderDest()` 在 `case OT_GOTO_COUPLE`（`:4512-4520`）里显式赋值：车库型 ⇒ `Depot::Get(id)->xy`；站台型 ⇒ `v->GetOrderStationLocation(order->GetDestination().ToStationID())`。而 `ProcessOrders()`（`src\train_cmd.cpp:4663-4673`）对 `OT_GOTO_COUPLE` 会调用 `UpdateOrderDest(v, &v->current_order)` ⇒ `dest_tile` 每 tick 有效。
2. **普通寻路拿得到这个目的地。** `CYapfDestinationRailBase::SetDestination()`（`src\pathfinder\yapf\yapf_destrail.hpp:146`）的 `switch` 里**没有 `OT_GOTO_COUPLE`** ⇒ 落 `default:`（`:176-180`）= `dest_tile = v->dest_tile; dest_station_id = Invalid(); dest_trackdirs = GetTileTrackdirBits(dest_tile, TRANSPORT_RAIL, 0)`。**车库型与普通 `OT_GOTO_DEPOT` 走的是同一段代码**（后者在 `:170-174` 之后 `[[fallthrough]]` 进 `default`）⇒ 定位机制逐字相同。
3. **到达判定就是「订单目的地」。** `PfDetectDestination()`（`:207-229`）：`dest_station_id == Invalid()`（`default` 分支必置 Invalid）且 `any_depot == false`（`any_depot` 只在 `OT_GOTO_DEPOT + ODATFB_NEAREST_DEPOT` 时置位；`GOTO_COUPLE` 不经过该 case）⇒ 判定为 `tile == dest_tile && HasTrackdir(dest_trackdirs, td)`。站台型 `dest_tile` = `Train::GetOrderStationLocation()` 的返回值 = **`st->xy`**（`src\train_cmd.cpp:13982`；铁轨车站的基准格，必为 `HasStationTileRail` 的格），车库型 = 车库格。
4. **到了不会继续走，而是停住等人。** `OT_GOTO_COUPLE` 的 `IsBaseStationOrder()` 为假（定义见 `src\order_base.h`：`OT_IMPLICIT || OT_GOTO_STATION || OT_GOTO_WAYPOINT`）⇒ `ChooseTrainTrack()` 的"到站推进订单"分支（`:13570` 一带）不进；`AdvanceOrdersFromVehiclePosition()`（`:12946`）对 `OT_GOTO_DEPOT`/`OT_GOTO_COUPLE` **提前 return** —— 其上方注释（`:12930-12935`）原文即写明「the platform entrance tile **which is the GOTO_COUPLE destination**」「Advancing the look-ahead here … would make the pathfinder target the order after the GOTO_COUPLE instead of the waiting consist」。⇒ 预留终点 = 目的地，无路可续 ⇒ 停在目的地。

**闭环落到 KI-193 门上**：`R3RCoupleOrderDestinationReached()`（`:11691-11701`）判据是「车头所在格属于该车站的任一铁轨格（`HasStationTileRail` + `GetStationIndex == dest`）」/「属于该车库」⇒ 机车一进入目的地车站即成真 ⇒ `COUPLE-DEST-EMPTY` 武装 ⇒ 满 500 tick 跳过命令。⇒ 变体 (a) 的「开到、等待、继续」成立。

### 7.2 R-1b = 新发现残余：站台型回退目标只是**单一格** `st->xy`（第 212 轮修复对这一子情形无效）

与普通 `OT_GOTO_STATION` 的关键差异：后者在 `SetDestination()` 走 `case OT_GOTO_STATION`（`:164-168`）⇒ `dest_station_id` 有效 ⇒ `PfDetectDestination()` 接受**该站任意站台格**（`:209-222`）；而 `GOTO_COUPLE` 走 `default` ⇒ **只接受 `st->xy` 这一格**。

⇒ 若该订单的**车站基准格 `st->xy` 恰好被一列"不可挂的列车"（分组/公司/订单不符，或根本没在等待）压住**：`DoTrainCouplePathfind()` = `found=0`，回退的 `DoTrainPathfind()` 也到不了 `st->xy` ⇒ `MarkTrainAsStuck()` ⇒ **仍与第 212 轮之前逐字相同地永久停驻**（KI-193 门同样够不着，因为没到目的地）。

- **判据**：复测若出现「`CPL-PATHFOUND found=0` 且日志**无** `COUPLE-NOTARGET-FALLBACK` 且机车不动」，即命中本子情形。
- **候选修法（本轮未做，避免无数据改行为）**：①回退前把 `consist->dest_tile` 临时指向 `CalcClosestStationTile(...)` 的空闲站台格再调 `DoTrainPathfind()`（用完还原）；②复刻 `:13488-13506` 的「找不到预留目标 ⇒ `TryReserveSafeTrack()` 找任意安全点」兜底，让机车至少先动起来；③把 KI-193 的"到点"判据放宽为"已进入目的地车站的接近范围"。

### 7.3 R-2 = 能进到回退分支已蕴含 `!res_dest.okay || long_reserve`；最坏配置不引入新回归 —— 降级为可接受残余

- `res_dest` 在回退分支（`:13423`）处的值是 `ExtendTrainReservation()`（`:13328`）留下的。而若 `res_dest.okay == true` 且 `!long_reserve`，函数会在 `:13335-13356` 一带**提前返回**，**根本到不了 `:13397`**。⇒ 能执行到 `:13397` 这件事本身已蕴含 `!res_dest.okay || long_reserve`。
- 第 210 轮现场有 63 条 `CPL-PATHFOUND found=0`（即已进到 `:13388` 的 couple 寻路）⇒ 现场必然满足 `!okay || long_reserve`；按 `okay == false`（"路线在目的地前就被挡住 / 只到车底站台"）解读与 `:13476-13480` 的既有注释自洽。
- 最坏配置（`okay && long_reserve`）：`:13423` 的 `!res_dest.okay` 为假 ⇒ 回退不执行 ⇒ 走 `:13449 MarkTrainAsStuck()` + `:13450 return { FindFirstTrack(tracks), result_flags }`。该路径上的 `FreeTrainTrackReservation()`（`:13422`）**是第 212 轮之前就存在的旧代码**，本轮只是把回退插在它之后 ⇒ **行为与旧版逐字一致，没有引入新回归**，只是"修复在该罕见配置下不生效"。
- **备用判据**：若复测「机车不动且**无** `COUPLE-NOTARGET-FALLBACK`」，除命中 7.2 的 `st->xy` 被占外，就是命中本配置；届时把 `:13423` 的门放宽为只看 `res_dest.tile != INVALID_TILE` 即可（该值在 `:13329-13333` 已保证非 `INVALID_TILE`）。

### 7.4 本轮（静态结案）结论

- R-1 **结案**（站台 / 车库两型，代码证据 4 条）。
- 新登记 **R-1b** 残余（站台型单一格 `st->xy` 被占 ⇒ 本轮修复失效），已同步写入 `R3R_KNOWN_ISSUES.md` 的 KI-316 附记与 KI-317。
- R-2 **降级为可接受残余**（最坏配置不引入新回归）。
- **未改任何源码、未重新构建**；§五 的复测 4 条判据不变，另加 R-1b 的判据（§7.2）。

