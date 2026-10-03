# R3R 第 210 轮：HXN5B「停在机待线不去挂车」现场分析（临时报告）

- **日期**：2026-10-03
- **现场**：`d:\sourcecode of JGRPP\build\R3R_debug.log`，257 376 B / 4 150 行（mtime 2026-10-03 15:22:40）
- **本轮未改任何源码**，纯取证；不改判据、不改行为、不构建。
- **工具坑**：`build\` 被 `.gitignore` 覆盖 ⇒ `search_content` 在其下恒返 0，读日志必须用 `cmd /c findstr /n`；`search_content` 的 `outputMode="count"` 不可靠（第 209 轮已记），一律用 `content` / `findstr`。

---

## 一、玩家两条抱怨 → 两个互不相同的问题

| 玩家原话 | 现场锚点 | 归属 |
|---|---|---|
| 「不去车库挂车」 | L91 `CPL-GATE reject=target-not-wait site=depot act=54 tgt=27 aOrd=16 tOrd=0 aStop=0 tStop=1 grp=0` | **KI-314**（第 208 轮已建，玩家已拍板「否」维持现状）：目标车底 27 被**手动停止** ⇒ `tStop=1`，其 `current_order` 恒为 `OT_NOTHING`（`tOrd=0`）⇒ 永不作挂接目标。 |
| 「一直停在机待线」 | L1943 `DEPOT-ARR veh=51 spd=0 real=3(16) curType=16 stuck=1 tx=99 ty=108 destTx=99 destTy=97 tileEqDest=0`；L1984 `DEPOT-ARR veh=57 spd=0 real=4(16) curType=16 stuck=1 tx=21 ty=13 destTx=27 destTy=7 tileEqDest=0` | **KI-316（本轮新增，高）**：GOTO_COUPLE 机车找不到可挂目标 ⇒ `found=0` ⇒ 不订路 ⇒ 永久停在原地；且到不了目的地（`tileEqDest=0`）⇒ KI-193 兜底永不武装。 |

**结论：这是两个不同的 bug。** 第一条是"有目标但目标不合格"（设计口径，玩家已拍板不改）；第二条是"没有目标、也不肯往目的地开"（真缺陷）。

## 二、身份反查（`SEGTR-SNAP` 是唯一可靠的车号/名称来源）

| 车 | 名称 | 锚点 |
|---|---|---|
| veh 54 | `HXN5B-XXXX` | L212 `SEGTR-SNAP tag=couple head=54 … bk u=6 n="HXN5B-XXXX"` |
| veh 57 | `HXN5B-XXXX2` | L881 `SEGTR-SNAP tag=couple head=57 … bk u=7 n="HXN5B-XXXX2"` |
| veh 48 | `DF4D-3058`（未卡死，末尾仍在 102,68 尝试挂车） | L1794 `SEGTR-SNAP tag=couple head=48 … bk u=4 n="DF4D-3058"` |
| veh 51 | **车号未定**（另一台执行 GOTO_COUPLE 的机车，11 条订单、当前 `real=3`=type16；全日志无 `SEGTR-SNAP`，因从未成功挂/解） | L1919 `REVERSEDIR veh=51 tile=99,108 order=16` |

⇒ 玩家说的"HXN5B 停在机待线"＝ **veh 57（HXN5B-XXXX2，停在 21,13）**；同症状还有 **veh 51（停在 99,108）**。

## 三、事件时间线（关键锚点）

| 行 | 事件 |
|---|---|
| L91 | `CPL-GATE reject=target-not-wait site=depot act=54 tgt=27 tStop=1`（KI-314 现场） |
| L101-102 | `NAME-XFER head=54 u=0` → `COUPLE-OK loco=54 rear=26 consist=0` |
| L211-214 | `SEGTR-SNAP couple head=54 nseg=3 ctrl=6`（车号 HXN5B-XXXX） |
| L771 | `COUPLE-OK loco=57 rear=26 consist=0` |
| L880 | `SEGTR-SNAP couple head=57 nseg=3 ctrl=6`（HXN5B-XXXX2） |
| L1135 / L1171 | `SEGTR-SNAP decouple-v head=57` / `decouple-u head=0` |
| L1685 / L1793 | `COUPLE-OK loco=48 rear=26 consist=0` / `SEGTR-SNAP couple head=48 nseg=3 ctrl=6`（DF4D-3058） |
| L1919 | `REVERSEDIR veh=51 tile=99,108 order=16` → 其后 `CPL-PATHFOUND veh=51 found=0` / `COUPLE-FAIL` |
| **L1943** | **`DEPOT-ARR veh=51 … stuck=1 tx=99 ty=108 tileEqDest=0`（最后一次；此后不再移动）** |
| L1965 | `REVERSEDIR veh=57 tile=21,13 order=16` → `CPL-PATHFOUND veh=57 found=0` / `COUPLE-FAIL` |
| **L1984** | **`DEPOT-ARR veh=57 … stuck=1 tx=21 ty=13 tileEqDest=0`（最后一次；此后不再移动）** |
| L2497 | `REVERSEDIR veh=54 tile=18,16 order=16 stuck=1 db=1` |
| L2981 / L3009 | `SEGTR-SNAP decouple-v head=48 nseg=2` / `decouple-u head=6 nseg=1` |
| L3426-3427 | `NAME-XFER head=54 u=27` → **`COUPLE-OK loco=54 rear=47 consist=27`**（玩家重启车底 27 后才挂上） |
| L3524 | `SEGTR-SNAP couple head=54 nseg=2 ctrl=27` |
| L3956 / L3959 | 全日志最后两次 `CPL-PATHFOUND veh=51 / veh=57 found=0`（仍在重试，仍不动） |
| L4100+ / L4149-4150 | 日志末尾 veh=48 在 102,68 尝试挂车（去 100,57）；veh=0 @102,58、veh=6 @100,63（均 `type=17` WAIT_COUPLE）持续 `SVC-DEPOT-SKIP` |

**计数（`findstr` 实测）**：`CPL-PATHFOUND` 且 `found=0` = **63**；`COUPLE-OK` = **4**（54/57/48/54）；`CPL-GATE reject` = **6**（1 条 depot=KI-314，5 条 scan）；`COUPLE-DEST-EMPTY` = **0**；`COUPLE-WAIT-HEAL` = **0**；`COUPLE-WAIT-NOHEAL` = **0**。

## 四、KI-316 根因：GOTO_COUPLE「找不到目标」⇒ 不订路 ⇒ 永久停驻

- GOTO_COUPLE 的寻路目标是**可挂目标**（couple 专用寻路），**不是订单目的地**。目的地 (99,97)/(27,7) 附近没有 `OT_WAIT_COUPLE` 车底 ⇒ `CPL-PATHFOUND found=0` ⇒ `TryPathReserveWithResultFlags()` 拿不到 `TPRRF_RESERVATION_OK` ⇒ 无路径 ⇒ 列车被标 `Stuck`（L1943/L1984 `stuck=1`）。
- 连锁死结：`R3RCoupleOrderDestinationReached()` 需要"停稳在目的地"才为真，而车根本到不了目的地（全日志 `tileEqDest=0`）⇒ KI-193 的 `COUPLE-DEST-EMPTY` 兜底（到点后 500 tick 跳过该命令）**永不武装**（0 命中）。
- 净效果：车「既不走、也不跳命令」，永久停在机待线 —— 正是玩家所述。
- 补充：日志末尾确有等待车底（veh=0 @102,58、veh=6 @100,63 均 WAIT_COUPLE），但它们不在 51/57 的目的地格 ⇒ 对 51/57 而言仍是"目的地无目标"。

## 五、KI-317 根因：`found=0` 型卡死拿不到任何自愈出口

- 代码：`src\train_cmd.cpp:17335-17393`（`TrainLocoHandler` 的 Stuck 分支）。
  - `:17352` `turn_around = (wait_counter % (wait_for_pbs_path*DAY_TICKS)==0) && _settings_game.pf.reverse_at_signals`
  - `:17353` heal 用尽 ⇒ `turn_around=false`；`:17355` 非窗口且 `force_proceed==TFP_NONE` ⇒ 直接 `return true`
  - `:17356` `TryPathReserveWithResultFlags(consist)`；`:17359` `r3r_rev_wanted = turn_around || (path_result & TPRRF_REVERSE_AT_SIGNAL)`
  - `:17367` / `:17375` 打 `COUPLE-WAIT-NOHEAL` / `COUPLE-WAIT-HEAL`；`:17381` 反转；`:17393` `return true`
- 两个出口都够不着：①KI-289 `COUPLE-WAIT-HEAL` 需 `turn_around`（`pf.reverse_at_signals` 开 + 周期命中）或 `TPRRF_REVERSE_AT_SIGNAL` —— `found=0` 产生不了后者，前者本 save 下 0 命中；②KI-193 `COUPLE-DEST-EMPTY` 需"已到目的地"（§四已证永假）。
- 现场：`COUPLE-WAIT-HEAL` / `NOHEAL` 均 0 命中 ⇒ 整段 Stuck 逻辑对 `found=0` 型卡死无任何产出，每 backoff tick 在 `:17393` 返回，车纹丝不动。唯一出现过的一次掉头（L1919/L1965）之后即转 `stuck=1`、再无动作。
- 定性：R3R 的"耦合等待自愈"只覆盖"已到目的地、无目标"（KI-193 注册 `_r3r_couple_no_target`@`:11830` 之后的 KI-289 路径），**不覆盖"途中找不到目标"**。

## 六、KI-318 复核：本轮 6 条 `CPL-GATE reject` + `SVC-DEPOT-SKIP` 均为**非缺陷**

- 6 条 `CPL-GATE reject`：1 条 `site=depot act=54 tgt=27 tOrd=0 tStop=1`（=KI-314，目标被手动停）；5 条 `site=scan`：L556/603 `act=57 tgt=54 tOrd=6`、L954/974/1004 `act=54 tgt=57 tOrd=1` —— 都是 9 格扫描把**正在执行自己订单**（type 6 路点 / type 1 车站）的邻车当候选后按设计拒绝，**正常**。
- `SVC-DEPOT-SKIP`（veh=0 / veh=6 / veh=48，`tag=couple-protocol`）＝ KI-220「耦合协议车辆不回库自动检修」守卫，正常。
- ⇒ 仅登记备查，**不修**。

## 七、将来改码后的复测判据

1. GOTO_COUPLE 目的地无 `WAIT_COUPLE` 目标时，机车应「继续驶向目的地」而不是原地 `Stuck`；`CPL-PATHFOUND found=0` 应随"到达目的地"收敛为一次性的 `COUPLE-DEST-EMPTY`。
2. 出现 `COUPLE-DEST-EMPTY`（KI-193）且 `R3RCoupleOrderDestinationReached()` 为真。
3. `found=0` 型卡死应有**有界**自愈（`COUPLE-WAIT-HEAL` / `NOHEAL` 至少其一的命中）。
4. KI-314 行为不变（手动停止的目标照旧拒绝）。
5. 无回归：正常站台挂车（L102 / L771 / L1685 / L3427）仍 `COUPLE-OK`。

## 八、第 211 轮补充：veh57 后半段「抢锁空转」的精确机制（同一日志，只读取证）

玩家追问「veh57 在日志后面的行为，把你所知的问题列出来」。本轮只读取证、未改码、未构建。§一~§七 已覆盖的问题（KI-316 目的地无可挂目标、KI-317 无自愈出口、KI-318 gate 属正常）不再重复，以下是新增。

### 8.1 veh57 后半段时间线（复核 §三，补齐全貌）

1. L771 / L845 / L880：耦合成功 → `ORD-IDX-WATCH site=settle-in tag=couple veh=57 real=0->4 impl=0->4 tt=0->4 n=6->33 ord=...8FE330->...8FCAD0`、`SEGTR-SNAP head=57 nseg=3 ctrl=6 borrowed=1`（合并 30 节、3 段、控制段是 veh6）。
2. L1135 / L1171：解挂 → `SEGTR-SNAP tag=decouple-v head=57` / `tag=decouple-u head=0`（57 留下，0..26 那半解出）。
3. 之后 57 回到自己的 6 条排程，`real=4 type=16 dest=923`（L3938）。
4. L1943 / L1984：`DEPOT-ARR veh=57 ... stuck=1`（21,13），随即 L1965 一次 `REVERSEDIR veh=57 tile=21,13 dir=5 order=16`。
5. 此后直至日志末行一直停在 21,13：`CPL-ENTRY → CPL-ORIGIN(td=8) → CPL-PATHFOUND found=0 → CPL-SKIP retryIn=8 → COUPLE-FAIL`，`TRP ... => ok=0 res=0`。

### 8.2 新增发现：`CPL-PAIR-STEAL` 是空转（探针位置错误）

**现场**：L3951/L4010 `CPL-PAIR-STEAL act=51 tgt=6 from=48 myEnter=13793 hisEnter=16893`；L3952/L4013 `CPL-PAIR-STEAL act=57 tgt=6 from=48 myEnter=13809 hisEnter=16893`。对照 L3943/L3987 `CPL-PAIR act=48 tgt=6 dist=7 actTile=102,68 tgtTile=100,63`。
**判读**：全日志没有 `CPL-PAIR act=57 tgt=6` ⇒ 锁从未易主，日志里的「51/57 与 48 争抢」是假象。

**机制**（`src\train_cmd.cpp`）：
- `:11445-11447` `R3RCouplePairOutranks` 放行 ⇒ `:11450-11453` 打印 STEAL。**只打印，不改 `_r3r_couple_pairs`**。
- `:11458` `if (!R3RCoupleAllowedIgnoringPair(coupler, t)) continue;` —— 真正的准入判据在其后。
- `R3RCoupleAllowedIgnoringPair()`（`src\couple_group.cpp:322`）含 KI-165 的目的地判据 `:370`：候选在站台上且 `GetStationIndex(target->tile) != dest.ToStationID()` ⇒ false。
- veh6 在 100,63（另一车站），veh57 的订单目标是站 923 ⇒ 必被 `:370` 拒 ⇒ `best==nullptr`（`:11467`）⇒ `_r3r_pair_scan_tick` 记时刻、`R3R_PAIR_RESCAN_TICKS=8`（`:11415-11417`）后再扫一圈，每圈再打一条 STEAL。

**讽刺点**：`:11471-11475` 的注释正是 KI-188「Do not report a lock which was never taken -- that lie is what made the KI-188 log read as a self-lock」。那条修复只覆盖了 `CPL-PAIR`，同类谎报在 STEAL 上原样保留。

**代价**：①日志误导（把「每 8 tick 空转一圈」读成「抢锁争用」）；②每 8 tick 一次注定失败的全池 `Train::Iterate`。

**建议修法（未做）**：把目的地判据提到候选循环最前（= 在 STEAL 打印之前），`best` 选择改成「先过目的地、再比距离」。

### 8.3 附带：优先权口径不看距离/目的地（推论）

`:11460-11464` 只按 `DistanceManhattan` 取最近、无距离上限；`R3RCouplePairOutranks`（`:6568-6574`，`mine < his`）只看「谁更早进入耦合订单」。二者叠加 ⇒ 若有车进入订单更早但位置更远，它可以夺走紧邻目标的近车所锁定的车底。本现场因 `:370` 拦下未发生，列为待核实推论（KI-320）。

### 8.4 本轮排除（不要重复报）

- **不是**「LOCO-ORD 只列 6 条与 `n=33` 不符」：L3265/L4003 在**解挂之后**，57 已拿回自己的 6 条表，L4004-4009 与之一致，自洽。
- **不是** `CPL-GATE reject` / `SVC-DEPOT-SKIP`：见 §六（KI-318）。
- **不是** 57 抢锁失败导致卡死：卡死的直接成因仍是 KI-316（目的地 27,7 无 `WAIT_COUPLE` 目标），STEAL 只是同一 `found=0` 过程的副产物。
