# 第 237 轮 临时分析报告：读档首帧保留位审计 `strayTiles=20 / reaped=0` 的定性 + KI-344 `CPL-SCAN` 收口

- 日期：2026-10-10
- 轮次：第 237 轮
- 性质：**纯取证定性，零源码改动，零构建**
- 关联 KI：KI-203c（游离/陈旧预留）、KI-345（本轮新增）、KI-344（`CPL-SCAN` 探针，第 236 轮）

---

## 一、输入与可信度

| 项 | 值 |
|---|---|
| 日志 | `D:\temp\codebuddy-dropped-files\6323ebfb-51d7-43f8-a156-918728cdb552\R3R_debug.log` |
| 规模 | ≈2 478 行（读到 2 478 行仍有内容；偏移 2 900 读出为空 ⇒ 文件在 2 478~2 899 行之间结束） |
| 产生者 | **第 236 轮 exe**（`build\openttd.exe` @2026-10-10 23:01:30，51 740 672 B，含 `CPL-SCAN`） |
| 判据 | 日志含 `CPL-SCAN` ⇒ 必为第 236 轮（第 235 轮及以前的 exe 无此字面量） |
| 采样有效性 | **有效**。日志是"读档 + 短程跑车"的一次会话，非暂停态 |

> 注意：与第 235 轮（1 965 行 / 171 606 B / 21:17:02）、第 236 轮（3 055 行 / 247 628 B / 21:31:18）是**三次不同运行**，勿混判。

---

## 二、关键取证（行号 + 原文）

### 2.1 读档后首次保留位审计

```
160~189  RESV-AUDIT-STRAY tick tile=1093,87 … 1112,106   （20 行，逐格 +1,+1 对角）
190      RESV-AUDIT-SUMMARY tick reservedTiles=58 owned=38 strayTiles=20 reaped=0 pendingStray=20
```

- 全日志 `RESV-AUDIT-SUMMARY` **只有 1 条**（`search_content` 全文件计数确认）⇒ 本会话**只跑过一次审计**。
- 20 条 `RESV-AUDIT-STRAY` 全部为 `track=4 bits=0x10`（`TRACK_LEFT`），tile 从 `1093,87` 起逐格 `(x+1, y+1)` 直到 `1112,106` ⇒ **是一条连续 20 格的单一预留路径**，不是 20 个互不相关的泄漏。

### 2.2 玩家此前主诉的 `(60,16)` 路点格

```
168      RESV-AUDIT-WATCH tick tile=60,16 kind=OTHER ttype=0 bits=0x0 reserved=0 owner=none
```

- `bits=0x0`、`reserved=0`、`owner=none` ⇒ **`(60,16)` 本次是干净的**，第 126 / 127 轮报的"路点格黑色预留残留"在本日志中**不存在**。

### 2.3 `CPL-SCAN`（KI-344 探针）全部成功侧

```
390      CPL-SCAN veh=2  found=1 closed=16 limit=10000 enter=49 noplat=31 nores=3 noconsist=6 consist=9 rej=4/0/0/0 acc=5/0 last=1089,139 lastveh=3 why=acc-car
417      CPL-SCAN veh=2  …（同形态）
2028     CPL-SCAN veh=23 found=1 … acc=0/3 last=1082,136 lastveh=22 why=acc-wait
…（共 10 行，全部 found=1）
```

- 10 条 `CPL-SCAN` **全部 `found=1`**，`why=acc-car`（9 条）/ `why=acc-wait`（veh=23 挂 veh=22，1 条）。
- ⇒ **KI-344 要抓的 `found=0` 在本日志中未复现**；判据①（`CPL-SCAN` 每次寻路恰一行且与 `CPL-PATHFOUND` 的 `veh/found` 一致）**通过**；判据③（成功那次 `acc>0` 且 `why=acc-*`）**通过**。
- `rej=4/0/0/0`（GROUP 分支 4 次）出现在 `veh=2`，最终仍 `acc=5` ⇒ 拒掉的是别的候选（其他车底配对被 GROUP 闸门拦下），最后 car-only 兜底成功 ⇒ 形态 **D 的成功侧**，符合设计。

### 2.4 那 20 格"孤儿走廊"本会话正被使用

```
2141~2430  DEPOTBLK veh=23 tile=1092,86→1092,87→1093,87→1093,88→1094,88→1094,89→…→1112,106 spd=112
2431~2444  DEPOTBLK veh=23 tile=1112,107 track=02 / 1112,109 track=02 …（转入南北向）
2361~2471  CPL-PAIR act=23 tgt=22 dist=56/57 actTile=…1103,97…1106,101…1112,117 tgtTile=1085,136
```

- `veh=23` 本会话的行驶走廊 **恰是审计判为 stray 的 `1093,87 → 1112,106` 那 20 格**。
- 读档瞬间 `veh=23` 在 `964,133` 的车库里（`R3RDUMP-CHAIN head=23 … tile=964,133 inDepot=1 spd=0`，第 151 行）⇒ **审计时刻它确实不在走廊上**，故被判 `owner=none` 是"当时正确"。
- ⇒ 这条 20 格预留**极可能是上一会话遗留、由本会话会走这条路的 `veh=23` 链持有**（"持有者尚未到位"型孤儿），而不是永久无人认领的垃圾。

### 2.5 等待链的平台自留位（健康）

```
2365~2372  RESERVECONSIST veh=22/31/32/33/34/35/37/24 tile=1085,136/1084,136/1083,136/1082,136 track=0x1 fb=0 bits=0x1 ok=0 resNow=1
…（每 ~24 行重复一次，直到日志末尾）
```

- 等待车底链（head=22，含 31/32/33/34/35/37/24）在 `1082..1085,136` 反复重申脚下预留 ⇒ 正常。

### 2.6 耦合后 `RESV-GC … holder-gone-skip`（安全设计生效）

```
2349~2356  RESV-GC veh=10 tile=1087,139 track=stn kind=STATION why=holder-gone-skip   （8 行）
```

- 耦合（`COUPLE-OK` 一带）后原持有者 `veh=10` 的站台整格记账消失 ⇒ 走 `holder-gone` 清理；但释放前校验未通过（该格现在仍应保留 / 归新链所有）⇒ 按第 126 轮引入的安全口径 **`-skip`**：**只删账本条目、不改地图**。
- ⇒ 这正是第 125/126 轮设计的预期行为，**不是新泄漏**。

### 2.7 `veh=2` 的 `COUPLE-FAIL`（按设计）

```
393   CPL-SAFE-FAIL veh=2 tile=1089,139
395   FSCP-RES owner=11 otile=1080,140 cpl=1073,139
396   FSCP-REACH-NONE tile=1079,139 (no chain reaches this tile along a contiguous reservation)
397   FSCP-VEH tile=1079,139 veh=14 track=0x20 dir=4 front=0 ord=0
405   COUPLE-FAIL loco=2 order=16
```

- `veh=2`（GOTO_COUPLE）的挂接路线必经 `1079,139`，而该格被 **`veh=11` 链的车节 `veh=14` 占着**，且该格沿连续预留回溯不到自己的链 ⇒ 安全位检查失败 ⇒ 本 tick 不挂。
- ⇒ 属"前方格被别的链合法占用"的**按设计**拒绝，非缺陷（与 KI-330 同族但不同因：此处不是"同格自车误判"，是真被邻链占）。

---

## 三、判读（结论）

1. **`reaped=0` 不是 bug。** `R3RReservationAudit` 的 reap 需要同一格**连续 `R3R_RESV_REAP_AUDITS=3` 次审计**仍判游离才释放。本日志只有 **1** 次审计，且这 20 格 `age=0` ⇒ `reaped=0` 是**设计预期**。
   - 修正此前（本轮开工时）的倾向性判断："reap 逻辑未触发 ⇒ 疑似 bug" —— **收回**。
   - 佐证：第 125 轮记录过 `RESV-AUDIT-STRAY … kind=STATION` 的整格泄漏在 `age=3` 被 reap 清掉 ⇒ 机制本身在早前轮次已验证可用。
2. **本日志无法判定这 20 格是否会最终被 reap**：会话长度不足以跨过 3 次审计。**精确语义（本轮已在源码逐字确证，见 §七）**：`age` 在**每次**审计的收集阶段之后自增（`src\train_cmd.cpp:14957-14959`），而 `RESV-AUDIT-STRAY` 行打印的是**自增前**的旧值 ⇒ 审计#1 打印 `age=0`（age→1）、#2 打印 `age=1`（age→2）、#3 打印 `age=2`（age→3 且 `3 < R3R_RESV_REAP_AUDITS(3)` 为假 ⇒ **当次即 reap**，打印 `RESV-REAP … age=3`）。⇒ **第 3 次审计就释放**，总耗时 `3 × R3R_RESV_AUDIT_TICKS(2048)` = **6 144 tick**。要判定必须拿**更长**的日志。
3. **新增的、比 reap 更值得追的事实**：这 20 格是**一条连续 20 格的单一孤儿预留路径**（同 `bits=0x10`、逐格 `+1,+1`），且本会话 `veh=23` **正好沿它行驶** ⇒ 形态是"**读档带入的、持有链尚未到位的预留**"，属 KI-203c 的"跨会话保留"分支（预留位随存档保存；JGRPP 仅对 `savegame version < 100` 的旧档清全图预留）。
4. **`(60,16)` 本次干净**（`bits=0x0 reserved=0`）⇒ 玩家早前主诉的"路点格黑色预留"本会话不存在。
5. **`CPL-SCAN` 全 `found=1`** ⇒ KI-344 的 `found=0` 现场未复现；探针本身的判据①③通过，形态落 D 的成功侧。

---

## 四、未确认项

- **U-1**：这 20 格是否真的属于 `veh=23` 链？审计时刻它不在走廊（在车库），只能由"本会话它走的正是这条走廊"旁证；无法从本日志直接证实归属。
- **U-2**：`RESV-GC … holder-gone-skip`（2.6）的 8 条是否让 `1084..1087,139` 站台整格位**永久残留**（需要在更晚时刻再审计一次同一批格才能判）。
- **U-3**：审计里 `reservedTiles=58 / owned=38 / stray=20` 的 38 格归属分布未取（本轮未 dump `RESV-AUDIT-OWNER`）。
- ~~**U-4**：`age` / `pendingStray` 的年龄阈值常量名与取值需逐字复核~~ ⇒ **本轮已确证，见 §七**：`R3R_RESV_REAP_AUDITS = 3`（`train_cmd.cpp:14851`）、`R3R_RESV_AUDIT_TICKS = 2048`（`train_cmd.cpp:17202`）。

---

## 五、复测判据 / 下一步

**给玩家的唯一请求**：用第 236 轮 exe（或更新）**跑一段更长的会话**（至少 3 次 `RESV-AUDIT-SUMMARY`），再回日志。

判读：

| # | 判据 | 期望 |
|---|---|---|
| 1 | `RESV-AUDIT-SUMMARY` 出现 ≥3 次 | 能看到 `reaped` 是否增长 |
| 2 | 第 2/3 次审计里 `1093,87..1112,106` 是否仍在 | 不在 ⇒ 已被 reap（机制正常）；仍在 ⇒ 需查"为何 `veh=23` 未认领" |
| 3 | `RESV-AUDIT-STRAY` 是否出现 `age` 字段并递增 | 确认年龄计数在走 |
| 4 | `reaped>0` 时对应格是否出现 `RESV-REAP` | 释放确实发生 |
| 5 | 负判据：健康预留（对齐中的 PBS 路线）**不得**被 reap | `owned` 数不应无故骤降 |
| 6 | `RESV-GC … holder-gone-skip` 的格在下次审计里 `owner` 是否非 none | 验证 skip 不会造成永久孤岛 |

**若玩家日志显示这 20 格跨 3 次审计仍 `owner=none` 且不 reap**，那下一步**首要怀疑的不是 owner 判据、而是 reap 的释放校验被挡**（`train_cmd.cpp:14985-14989` 四道：①`GetTileType==Railway && IsPlainRailTile`；②格上无车 `GetFirstVehicleOnTile`；③该位确实还订着 `GetRailReservationTrackBits`；④`R3RResvPhaseGuard("reap")`）。任一不过即 `continue`，且 `age` 不会被 `erase`（会继续累积、下次立即 reap）⇒ 日志上表现为"`age` 一直涨但永不出 `RESV-REAP`"。

⚠ **明确否定一条曾经的候选修法**：**不要**放宽 `GetTrainForReservation` 的走链（即"沿预留走到链头即可、不要求端点有车"）。原因是该函数返回 `nullptr` **就是引擎的权威口径**——`src\train_cmd.cpp:14811-14823` 的注释写明 `CmdRemoveRailTrack` 用同一个调用来决定"能否拆掉这段轨"，返回 `nullptr` 即"无人拥有此路径、玩家可以拆除"；而第 110 轮恰恰是**把旧的"按最近车距离 `best > 24`"启发式换成这个权威判据**（因为旧版会漏报：现场 owner 在 15 格外，`best>24` 过滤掉、可游戏里玩家根本拆不掉那段轨，说明预留是活的）。⇒ 放宽即等于制造"自己认为无主、引擎认为有主"的分裂，会破坏拆轨判定。

---

## 六、本轮未做

- 未改任何源码；未构建。
- 未做"读档后孤儿预留主动清理"（第 110 轮已论证其风险：需要在读档期释放全部列车预留→全图清预留→逐列车重建，存在重建窗口/寻路失败风险）。
- 未实现"清预留控制台命令"（只需改 `console_cmds.cpp`，不动头文件；此前已列为可选手段）。
- KI-203c 的 `(60,16)` 具体来源（第 110 轮玩家已否定"陈年孤儿"，不得再断言）。

---

## 七、reap 机制权威细节核实（本轮源码逐字确证，`src\train_cmd.cpp`）

| 项 | 位置 | 事实 |
|---|---|---|
| 审计调用节流 | `17200-17207`（`TrainLocoHandler` 入口） | `static uint32_t r3r_resv_audit_tick = 0;` + `static const uint32_t R3R_RESV_AUDIT_TICKS = 2048;`，`_tick_counter - r3r_resv_audit_tick >= 2048` 时跑一次并刷新基准 ⇒ 每 2048 tick 一次，**进程级 static（NOSAVE）** |
| reap 阈值 | `14851` | `static const uint8_t R3R_RESV_REAP_AUDITS = 3;` |
| 年龄表 | `14855` | `static std::unordered_map<uint32_t, uint8_t> _r3r_resv_stray_age;`，**函数内 static（NOSAVE）**；key = `(tile.base() << 3) \| track`，整格位用 slot 7（`R3R_RESV_WHOLE_TILE_KEY = 7`，`14854`） |
| owner 判据 | `14932`（普通轨）/ `14906` 一带（整格位） | `Train *owner = GetTrainForReservation(t, track);` ⇒ **引擎权威口径**，`nullptr` 即"无主、玩家可拆轨" |
| 年龄递增 | `14956-14959` | 收集阶段结束后对 `strays_now` 逐 key `age++`，再 `if (age < 3) continue;` ⇒ **第 3 次审计当次释放**；`RESV-AUDIT-STRAY` 打印的是**自增前**的值（故首次必为 `age=0`） |
| 释放四道校验（普通轨） | `14985-14989` | ①`Railway && IsPlainRailTile` ②`GetFirstVehicleOnTile(..., Train)==nullptr` ③`HasBit(GetRailReservationTrackBits(tile), track)` ④`R3RResvPhaseGuard("reap")`；不过则 `continue` 且 **不 erase age** |
| 释放后 | `14995-14998` | `reaped++` + `RESV-REAP … age=%u` + `erase(key)` |
| 自愈语义 | `14844-14850`、`15001-15012` | 位一旦重新有主 ⇒ `owned++` 且 `erase(key)`；本回合未见者一律从年龄表剔除 ⇒ **不是泄漏** |
| SUMMARY | `15014` | `RESV-AUDIT-SUMMARY %s reservedTiles=%u owned=%u strayTiles=%u reaped=%u pendingStray=%u`，`pendingStray = _r3r_resv_stray_age.size()` |

### 由本节推出的两条新结论

1. **新增结论 6（重要，可能是玩家抱怨"黑色预留一直在"的真因之一）**：`r3r_resv_audit_tick` 与 `_r3r_resv_stray_age` **都是 NOSAVE 的进程级 static**，而 `_tick_counter` 也是进程内计数 ⇒ **每次启动游戏，孤儿预留的年龄都从 0 重新累积，必须重新等满 3 次审计（6 144 tick）才会被清**。因此**若玩家每次调试都"启动 → 读档 → 跑几分钟 → 关掉"（单次会话 < 6 144 tick），存档里带入的孤儿预留将永远清不掉**——这条完全能解释"每次都能看见那截黑色预览"。⇒ 复测请求必须写清"**同一次会话内跑够 ≥6 144 tick（约 3 分钟游戏时间）**再退出"，而不是"多跑几次"。
2. **新增结论 7**：整格位（站台/路点）的 `_r3r_resv_stray_age` 用的是同一个表、slot 7，故站台/路点孤儿与普通轨孤儿**同批同寿**（`(60,16)` 这类路点格若仍游离，理论同样会在第 3 次审计被 reap 掉，走 `14964-14981` 的整格分支、日志 `track=stn kind=WAYPOINT`）。
