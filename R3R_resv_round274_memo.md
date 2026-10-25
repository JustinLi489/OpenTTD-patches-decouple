# R3R 预留探针收敛后首份日志分析（第 274 轮）

> 玩家 2026-10-22 提供日志 `D:\temp\codebuddy-dropped-files\b5bbc617-3410-42bc-85d4-52cad4664460\R3R_debug.log`
> 玩家原话：「日志来了，希望能为你提供重要信息」
> 产出者应为本轮（第 273 轮收敛改动）编译的 `build\openttd.exe`（待核时间戳）。

## 一、文件与规模

| 项 | 读数 |
|---|---|
| 大小 | 869 647 B |
| 行数 | 12 107 行 |
| 日志 mtime | 2026-10-22 15:36:39 |
| 上轮基线（第 273 轮分析用日志） | 8057 行 |

## 二、标签频次（全量，Top 60 见下）

| 标签 | 条数 | 备注 |
|---|---|---|
| `RESV-WATCH` | 6578 | 预留 setter 层事件（门禁已放宽后大幅出现） |
| `UNRES-RAIL` | 4276 | 释放（setter 层） |
| `RESV-GC` | 202 | R3R 记账释放 |
| `RESV-REL` | 152 | |
| `RESV-AUDIT-WAIT` | 128 | |
| `GEO` | 96 | 非 `R3RDbgWrite` 出口（见 §四） |
| `CRT` | 90 | 同上 |
| `R3RDUMP` | 48 | 读档 dump |
| `RESV-WATCH-REP` | 36 | **本轮新增的 1/64 节流标签已生效** |
| `TRP` | 36 | 非 `R3RDbgWrite` 出口 |
| `TPR-WALK` | 29 | 同上 |
| `DEPOT-ARR` | 29 | 同上 |
| `COUPLE-FAIL` | 8 | |
| `CPL-*` | 各 3 | |
| `COUPLE-OK` / `DECOUPLE-DONE` / `DECOUPLE-FIRE` | 各 1 | 本轮只有一次挂车、一次解挂 |

**关键判据 1（已达成）**：`CLEAR-PATH` = **0 条** ⇒ 删探针生效，日志腾出 39.6% 空间。
**关键判据 2（已达成）**：`RESV-WATCH-REP` = 36 条 ⇒ 首现+计数节流分支真的被走到（非白名单格的非原生事件已进入观测）。
**关键判据 3（未达成，见 §三）**：`FOLDCHK` / `CPL-PAIR` / `SPD-DROP` / `EXTEND-BACKOUT` / `RESERVECONSIST` 等旧标签已全禁（预期），但 `GEO` / `CRT` / `TRP` / `TPR-WALK` / `DEPOT-ARR` 仍在 ⇒ 标签过滤只覆盖了 `R3RDbgWrite()` 一个出口。

## 三、尾部长串刷屏（新问题，疑似 ①b「神秘销毁」的现场）

日志尾部 25 行全为同一形态，只有三个格子在循环：

```
UNRES-RAIL tile=219,235 track=0 phase=extend-backout actor=24
UNRES-RAIL tile=217,235 track=0 phase=extend-backout actor=24
UNRES-RAIL tile=218,235 track=0 phase=extend-backout actor=24
... 反复循环，不推进
```

- 三格固定为 `217,235` / `218,235` / `219,235`，`track=0`（`TRACK_X`），`actor=24`，`phase=extend-backout`。
- `extend-backout` 是 R3R 自己的 phase 名 ⇒ 属**非原生产生/销毁**，正是 KI-418 要观测的对象。
- 该形态**每 tick 三行、无任何其它事件插入**，说明列车 24 卡在某个「扩展预留 → 回退 → 再扩展 → 再回退」的循环里。待定位：`extend-backout` 的 phase guard 所在函数与触发条件。

## 四、标签过滤的覆盖面缺口（实现边界，已记录）

`R3RDbgTagAllowed()` 只挂在 `R3RDbgWrite()` 内。日志仍有 `GEO`(96) / `CRT`(90) / `TRP`(36) / `TPR-WALK`(29) / `DEPOT-ARR`(29) 等标签 ⇒ 这些探针走的是**另一个写盘出口**（独立 ofstream / 直接 `fputs`），不受 `R3R_DBG_TAGS` 控制。待定位具体出口后决定是否一并接入。

## 五、取数结果

### 5.1 产物核对
- `build\openttd.exe` = 2026-10-22 15:24:18（52 089 344 B），日志 mtime = 15:36:39 ⇒ **日志确由本轮收敛后的 exe 产生，采样有效**。

### 5.2 `RESV-WATCH` 6578 行的分布
- `kind`：`CLEAR-RAIL` 3180 / `SET-RAIL` 3111 / `SET-STN` 138 / `CLEAR-STN` 136 / `SET-BRIDGE` 6 / `CLEAR-BRIDGE` 2 / `SET-DEPOT` 2 / `CLEAR-DEPOT` 3。
- `phase`：`controller` 3163 / `choose-track` 3102 / `yapf-resv` 302 / `extend-backout` 10（后 1215 次被 1/64 节流吞掉，只留 REP）。
- `tile`：**最大单格仅 5 次**，Top20 每格 5 次 ⇒ 6578 行分散在 **1300+ 个不同格**，本质是"非白名单格的**每键首现**各打一行"。

### 5.3 `UNRES-RAIL` 4276 行的分布
- `phase`：`controller` 3042 / `extend-backout` 1225 / `yapf-resv` 8 / `gc-stn` 1。
- `tile` Top3：`217,235` 406、`218,235` 406、`219,235` 406 ⇒ **1218 / 1225 = 99.4%** 的 `extend-backout` 释放都打在这三格上。

### 5.4 尾部刷屏的时序（首次出现 = 第 10095 行，至末行 12107）
`10095..12107` = **2013 行**，形态固定，同一 tick 内两个列车交替：

```
RESV-WATCH SET-RAIL  tile=217,235 track=0 phase=choose-track actor=24 head=24
RESV-WATCH SET-RAIL  tile=218,235 track=0 phase=choose-track actor=24 head=24
RESV-WATCH SET-RAIL  tile=219,235 track=0 phase=choose-track actor=24 head=24
RESV-WATCH SET-BRIDGE tile=220,235 track=0 phase=choose-track actor=24 head=24
RESV-WATCH SET-BRIDGE tile=223,235 track=0 phase=choose-track actor=24 head=24
RESV-WATCH SET-RAIL  tile=224,235 track=0 phase=choose-track actor=24 head=24
...（回退）
UNRES-RAIL tile=217,235 track=0 phase=extend-backout actor=24
UNRES-RAIL tile=218,235 track=0 phase=extend-backout actor=24
UNRES-RAIL tile=219,235 track=0 phase=extend-backout actor=24
```
同一时刻 `actor=50 head=24` 在 `147..156,235` 上一长串 `CLEAR-RAIL phase=controller`，`actor=47 head=21` 在 `206..210,25x` 同款 —— 即 y=235 是一条走廊，24/50/21 三列车互相卡。

### 5.5 代码落点
- `EXTEND-BACKOUT` 与 `extend-backout` phase guard 同在 `src\train_cmd.cpp:14622-14642`（`ExtendTrainReservation()` 的回退分支）。
- 回退循环 `src\train_cmd.cpp:14647-14660`：`while (tile != stopped) { ft.Follow(tile, cur_td); tile = ft.new_tile; cur_td = ...; UnreserveRailTrackdir(tile, cur_td); }`。
- `UnreserveRailTrack()`（`src\pbs.cpp:1090-1156`）**有** `TileType::TunnelBridge` 分支（`SetTunnelReservation` / `UnreserveRailBridgeHeadTrack`），所以"桥位能否被释放"只取决于循环里 `tile` 是否真的等于桥头格。

## 六、结论

**已达成（玩家判据）**
1. `CLEAR-PATH` = 0 ⇒ 删探针生效。
2. `RESV-WATCH-REP` = 36 ⇒ 首现+1/64 计数分支真的被走到。
3. 桥头事件不再静默 —— **KI-415 判据③首次拿到证据**：`SET-BRIDGE tile=220,235 / 223,235 phase=choose-track actor=24 head=24`，即"桥头那两格是被 24 号列车在 choose-track 阶段订下的"。

**新做实的问题**
4. **①b「神秘销毁」的机理**：列车 24 每 tick 在 `217..219,235` 反复 `choose-track 订 → ExtendTrainReservation 失败 → extend-backout 回退释放`，共 405 轮、1218 行 `UNRES-RAIL`，占了日志 10%。"预留被瞬间销毁、只剩路点那一格"就是这个循环。
5. **①a「越界/神秘预留」的疑似机理（待验）**：同一次尝试里 `SET-BRIDGE` 打了 `220,235` 与 `223,235` 两条，而回退 405 轮只留下 2 条 `CLEAR-BRIDGE`；`ft.Follow()` 跨桥时 `ft.new_tile` 直接是**桥的另一端**，循环里 `tile` 从不等于桥头格 ⇒ 桥头位可能永不被 `UnreserveRailTrack` 走到。若成立，这就是桥头孤儿预留的来源。

**我上一轮的实现失误（必须修）**
6. **`EXTEND-BACKOUT` 被我的标签白名单误禁（日志 0 条）**，而它恰恰带 `stop= next= reason= owner= whole= track= need= resv= led=`——正是判定"谁挡住了扩展"的唯一钥匙。`UNRES-RAIL` 打了 1225 次就证明它触发过 1225 次。
7. **总行数反而涨了**：8057 → 12107（+50%）。原因是放宽门禁后"非白名单格每键首现各一行"，而一条路径首次走会经过上千格。收敛方向要改成**按 phase/kind 聚合**（每窗一行 `RESV-WATCH-SUM`），而不是逐格首现。
8. **`R3RFopenDbg` 是第二个出口**（`src\r3r_perf.h:202`），`GEO`(96) / `CRT`(90) / `TRP`(36) / `TPR-WALK`(29) / `DEPOT-ARR`(29) / `R3R-RES-ONCE`(2) 走的是它，不受 `R3R_DBG_TAGS` 控制。

**新疑点（登记，未定）**
9. `RESV-AUDIT-WAIT tick=3072 waited=1024 interval=2048 due=0 pending=0` 共 128 条，其中 11028/11029/11030 **连号**、tick 与 waited 完全相同 ⇒ 同一 tick 内被调用上百次，节流没生效。

## 七、下一轮改法（待玩家拍板）

- **C-274-A（必做）**：把预留侧诊断标签加入白名单 —— `EXTEND-BACKOUT`、`LOOP-CLOSED`、`R3R-RES-ONCE`、`RESV-REL`、`RESV-AUDIT-*`、`RESV-WALK`；同时把 `R3RFopenDbg` 出口纳入同一过滤（或在 `R3RFopenDbg` 加 `R3R_DBG_TAGS` 开关）。
- **C-274-B（必做，收日志）**：`RESV-WATCH` 非白名单分支由"逐格首现"改为"按 `(kind,phase)` 每 256 次/每 512 tick 汇总一行 `RESV-WATCH-SUM`"；`UNRES-RAIL` 同样加 `(phase,tile)` 计数汇总，只对白名单格逐条。
- **C-274-C（取证）**：给回退循环补一行 `EXTEND-BACKOUT-BRIDGE`，或在循环里对 `ft.tiles_skipped > 0` 时额外释放桥头格，以验证 §6.5 的桥头孤儿假设。
- **C-274-D（修 bug）**：`RESV-AUDIT-WAIT` 加 tick 变化判据。

## 八、第 274 轮续：A/B/C/D 落地（2026-10-22）

> 玩家原话：「可以把ABCD一次改完」⇒ 一次改完四项，只编一次（改 `src\r3r_perf.h` 必然触发全量）。

### 8.1 A —— 白名单把预留侧诊断标签放回来（`src\r3r_perf.h`）

`R3RDbgTagAllowed()` 末尾追加三条前缀放行（原有 `RESV` / `UNRES` 保留）：

```cpp
if (n >= 6 && std::strncmp(fmt, "EXTEND", 6) == 0) return true;   /* EXTEND-BACKOUT / -SKIP */
if (n >= 4 && std::strncmp(fmt, "LOOP", 4) == 0) return true;     /* LOOP-CLOSED */
if (n >= 7 && std::strncmp(fmt, "R3R-RES", 7) == 0) return true;  /* R3R-RES-ONCE */
```

理由写进代码注释：第 274 轮日志里 `UNRES-RAIL phase=extend-backout` 打了 1225 次 ⇒ 回退确实跑了 1225 次，`EXTEND-BACKOUT` 却是 0 行，把唯一能回答"谁挡住扩展"的标签关掉了（§六.6）。
体量改由**调用点节流**控制，不再靠白名单（见 8.3）。

### 8.2 B —— 两张预留日志表改为按 `(kind, phase)` / `phase` 聚合（`src\pbs.cpp`）

- `R3RResvWatchLog()` 的非监视格分支：删掉"每 `(tile,track,kind,phase)` 首现 + 1/64 REP"，改为按 `(kind, phase)` 聚合，`n==1` 打完整 `RESV-WATCH`，之后每 256 次打一行
  `RESV-WATCH-SUM <kind> phase=<phase> n= tiles= capped= last=x,y actor= head=`
  —— `tiles=` 是 distinct tile 计数，用来区分"重试循环只砸三格"与"一条路径扫过上千格"（§5.2 实测 6578 行散在 1300+ 格、单格最多 5 次，就是单位选错的证据），每张表 8192 键上限，超限打 `capped=1` 而不是谎报计数。
- 监视格（KI-207 幽灵陷阱）**逐条保留**，一个字没动。
- 新增 `static void R3RUnresWatchLog(TileIndex, Track)`（`#if R3R_PROBES` 内，`pbs.cpp:1112`），`UnreserveRailTrack()` 里原来内联的 `UNRES-RAIL` 改为调它：监视格逐条、其余按 `phase` 聚合，每 256 次一行 `UNRES-SUM phase= n= tiles= capped= last=x,y actor=`（§5.3 的 4276 行里 99.4% 是三格×406 次，属于纯刷屏）。

### 8.3 C —— `EXTEND-BACKOUT` 恢复但按调用点节流，并新增桥头残留探针（`src\train_cmd.cpp`）

- `ExtendTrainReservation()` 回退分支（约 14643）：`EXTEND-BACKOUT` 由无条件输出改为按
  `(veh, next tile, reason, owner)` 计数节流 —— 同键前 4 次全打，之后每 256 次一行，行尾追加 `n=%u`；
  **原有字段与顺序逐字节未动**（`origin= stop= next= reason= err= owner= whole= track= need= resv= led=`）。整块包在 `if (R3RDbgOn())` 内。
- 新增 `EXTEND-BACKOUT-SKIP`：回退循环里用 `ft.tiles_skipped != 0` 记下被 `Follow()` 一步跨过的近端桥/隧道头（最多 8 个），循环结束后回读 `GetReservedTrackbits()`：
  `EXTEND-BACKOUT-SKIP veh= tile=x,y skipped= resvAfter=0x%X`
  —— 直接验证 §6.5：「桥头位永不被 `UnreserveRailTrack` 命名 ⇒ 回退清不掉 ⇒ 孤儿位」，即 `SET-BRIDGE`(6) 多于 `CLEAR-BRIDGE`(2) 的来源。
  记账本身也用 `R3RDbgOn()` 门禁（失败扩展是每 tick 重试的路径，探针关掉时必须归零）。

### 8.4 D —— 第二个写盘出口加粗开关 `R3R_DBG_RAW`（`src\r3r_perf.h`）

`R3RFopenDbg()` 增加 `R3R_DBG_RAW`（默认开，`=0` 关）：

```cpp
static const bool raw_on = []() { const char *env = std::getenv("R3R_DBG_RAW"); return !(env && env[0]=='0'); }();
return (R3RDbgOn() && raw_on) ? fopen("R3R_debug.log", mode) : nullptr;
```

覆盖 §四 点出的 `GEO`(96) / `CRT`(90) / `TRP`(36) / `TPR-WALK`(29) / `DEPOT-ARR`(29) / `R3R-RES-ONCE`(2)（约 120 个裸 `fprintf` 站点，无标签可过滤）。默认**保持开**：它们是事件级探针（282 行），不是本轮要收敛的洪水；`R3R_DBG_RAW=0` 才彻底静音。
`R3R_DBG=0` 仍然一票否决全部。

### 8.5 顺带修掉的新疑点（REG-274-B，已修）

`RESV-AUDIT-WAIT tick=3072 waited=1024 ...` 128 行同 tick 连号（§六.9）。代码在 `train_cmd.cpp:19604-19619`，闸门是 `r3r_audit_now % interval == interval/2`，而 `r3r_audit_now = _tick_counter`（**全局量**）⇒ 该 tick 上**每一列前车**各打一行；同族副作用更重：`RESV-AUDIT-DUE` 每列车一行，`R3RReservationAudit("tick")` 也在同一 tick 被调 N 次 ⇒ 杂散位的 `age` 一 tick 涨 N ⇒ "3 次审计后 reap" 实际一 tick 就到。

修法：新增 `static uint32_t r3r_resv_audit_last_tick = 0xFFFFFFFFu;` 与 `audit_tick_fresh`，只在 tick 变化时打 DUE/WAIT 并调审计 —— 与紧邻上方 `_r3r_resv_sweep_tick != _tick_counter` 的既有写法同构（那里的注释本来就写明"this function runs once per train"）。`_r3r_resv_audit_due = false` 保持幂等。

### 8.6 构建与自证

- 复用既有 `_tmp_inc_build.cmd`（未新建任何 `.cmd`）。
- 第一轮（改 `r3r_perf.h` + `pbs.cpp` + `train_cmd.cpp`）护栏：`GUARD: 1 header/lang file(s) are newer than the newest object file` / `newer: src\r3r_perf.h (2026-10-22 15:46:01)` / `GUARD: REMOVED 620 object file(s) - upgrading this build to a FULL rebuild` ⇒ **692 步**；`[692/692] Linking CXX executable openttd.exe`、`EXIT_CODE=0`、错误计数 0、exe 16:10:12。
- 第二轮（REG-274-B 去重，仅 `.cpp`）：`GUARD: incremental is safe` ⇒ `[2/3] train_cmd.cpp.obj` → `[3/3] Linking`、`EXIT_CODE=0`、错误计数 0。
- 时间戳：`src\train_cmd.cpp` 16:11:41 → `train_cmd.cpp.obj` 16:12:33 → `build\openttd.exe` **16:12:42（52 141 056 B）**；`src\pbs.cpp` 15:46:27 → `pbs.cpp.obj` 16:04:10；`read_lints` 三文件 0 条。
- exe 字面量 4/4 命中：`RESV-WATCH-SUM` / `UNRES-SUM` / `EXTEND-BACKOUT-SKIP` / `R3R_DBG_RAW`。


### 8.7 复测判据

1. `RESV-WATCH` 从 6578 行降到个位数（每 `(kind,phase)` 一行）+ 少量 `-SUM`；`UNRES-RAIL` 从 4276 降到个位数 + `UNRES-SUM`；总行数应 < 1500（预计 800 上下）。
2. `EXTEND-BACKOUT` 恢复出现，且同一 `(veh,next,reason,owner)` 只留 4 行 + 每 256 次一行 `n=`；`reason=` 若是 `choice-reserved`，看 `owner=` 是活车 / 已死 / -1，即可回答"405 轮为何全失败"。
3. `EXTEND-BACKOUT-SKIP` 若出现且 `resvAfter` 非 0 ⇒ 桥头孤儿假设**成立**（§6.5 定性）。
4. `R3R_DBG=0` 跑一遍应得 0 行；`R3R_DBG=1 R3R_DBG_RAW=0` 应得"只有带标签行、没有 GEO/CRT/TRP"。
5. 监视格（KI-207 那几个幽灵格）仍逐条出现 ⇒ B 的聚合没有吃掉陷阱。

---

## 九、第 274 轮续复测判读（2026-10-22，A/B/C/D 落地后首份日志）

### 9.1 采样来源

- 日志：`D:\temp\codebuddy-dropped-files\ae5f8eb8-a8c4-4ead-a7e5-54d63ffac98a\R3R_debug.log`
- **1054 行 / 88 354 B**，末次写入 16:22:31；产生者应为 `build\openttd.exe` 16:12:42（A/B/C/D + REG-274-B 去重版）。
- 对照：上一份 第 274 轮 日志 **12 107 行**（`RESV-WATCH` 6578 / `UNRES-RAIL` 4276，`EXTEND-BACKOUT` 0）。

### 9.2 判据逐条

| # | 判据 | 结果 | 证据 |
|---|------|------|------|
| 1 | `RESV-WATCH` 降到个位数 + 少量 `-SUM`；`UNRES-*` 同理；总行 < 1500 | **通过**（量级）| `RESV-WATCH*` 44 行（其中 `-SUM` 29）；`UNRES-*` 18 行（`UNRES-SUM` 14）；总 1054 行，降幅 91% |
| 2 | `EXTEND-BACKOUT` 恢复 + 同键 4 行后每 256 行 | **通过** | 14 行；`:950-953` 同一键 `n=1..4`，`:1033` 同键 `n=256` |
| 3 | `EXTEND-BACKOUT-SKIP` 出现且 `resvAfter` 非 0 ⇒ 桥头孤儿假设成立 | **未触发**（0 行）| 本场景扩展没有跨过桥/隧道头，§6.5 仍**未验证**（不是被证伪）|
| 4 | `R3R_DBG=0` 0 行 / `R3R_DBG_RAW=0` 只剩带标签行 | 未做（需另跑一次）| — |
| 5 | KI-207 监视格仍逐条出现 | **通过**（未见回归）| 本场景未命中那几个幽灵格，无法正面验证；聚合路径未吞掉逐条分支（代码路径互斥）|

### 9.3 决定性锚点（原文）

```
947:EXTEND-BACKOUT veh=24 origin=200,235/8 stop=207,235/8 next=208,235 reason=safe-not-free err=0 owner=-1 whole=0 track=-1 need=0x0 resv=0x0 led=-1 n=1
950:EXTEND-BACKOUT veh=24 origin=216,235/8 stop=219,235/8 next=220,235 reason=reserve-fail err=0 owner=-1 whole=0 track=-1 need=0x0 resv=0x0 led=-1 n=1
951:...(同键) n=2
952:...(同键) n=3
953:...(同键) n=4
1033:EXTEND-BACKOUT veh=24 origin=216,235/8 stop=219,235/8 next=220,235 reason=reserve-fail err=0 owner=-1 whole=0 track=-1 need=0x0 resv=0x0 led=-1 n=256
960:UNRES-SUM phase=extend-backout n=256 tiles=10 capped=0 last=219,235 actor=24
961:RESV-WATCH-SUM CLEAR-RAIL phase=extend-backout n=256 tiles=10 capped=0 last=219,235 actor=24 head=24
997:RESV-REL-PATH count=3584 who=CLEAR-RAIL veh=24 tile=218,235 track=0x0 age=0 phase=extend-backout
1031:UNRES-SUM phase=extend-backout n=768 tiles=10 capped=0 last=218,235 actor=24
1035:RESV-REL-PATH count=4096 who=CLEAR-RAIL veh=24 tile=218,235 track=0x0 age=0 phase=extend-backout
977/1012/1036:COUPLE-FAIL loco=21 order=16 tx=252 ty=234 x=4034 y=3752
```

### 9.4 结论：「405 轮全失败」的答案

**不是 `choice-reserved`（不是"有谁占着这个位"），而是 `reserve-fail`。**

`reason=reserve-fail` 只对应 `train_cmd.cpp:14609` 那句
`if (!TryReserveRailTrackdir(v, tile, cur_td)) { r3r_backout_reason = "reserve-fail"; break; }`
—— 即扩展走到 `219,235 → 220,235` 这一步时，**订位函数本身返回 false**，连"该位已被占"的判定都没走到（走那条路会打印 `choice-reserved` 并填 `owner/track/whole/led`）。

配套读数完全自洽：
- `owner=-1 whole=0 track=-1 led=-1 need=0x0 resv=0x0` 全部保持初值 ⇒ 一次都没填过（`need=/resv=` **只在 `choice-reserved` 分支赋值**）。
- 回退只砸 **10 个不同格**（`tiles=10`）却重复 768 次 ⇒ 每轮走的正是同一条短路径 216,235→219,235。
- `origin=216,235/8` 在 `n=256` 那行**逐字不变** ⇒ 列车原地不动、每 tick 重试同一段。

⇒ 405 轮失败是**单点订位失败的死循环**，不是"被别的列车合法挡住"，也不是"孤儿位"。上一轮我在对话里把 `need=0x0` 读成"请求空轨道集"是**错的**（`need` 未填 ≠ 空集），特此作废。

### 9.5 疑点（REG-274-D / REG-274-E，待验证）

- **REG-274-D（高，待验证）**：`220,235` 为什么让 `TryReserveRailTrackdir()` 直接返回 false？候选：(a) 该格不是 plain rail / 不可订（车库、站台整格、隧道桥头、道口）；(b) 该 trackdir 对应的 track 位已被预留（`HasReservedTracks` 命中但没进 choice 分支 —— 可能因为该格**不是** `target_seen` 且只有单 trackdir，走的是 `TryReserveRailTrackdir` 内部的占用判定）；(c) `v` 自己的旧预留（KI-361 把 own-chain 位当成功，反向也可能有例外）。需要：`220,235` 的 `GetTileType/IsPlainRail/HasSignals/GetReservedTrackbits/TrackdirToTrack(cur_td)` 实况，以及 `TryReserveRailTrackdir()` 的失败枚举（建议在失败点加只读 dump，不要先改行为）。
- **REG-274-E（中，待验证）**：`SET-RAIL phase=choose-track n=3840 tiles=2939`——同一会话内 choose-track 在 **2939 个不同格**各订一次。这可能是"每 tick 重算整条 PBS 路径"的正常形态，也可能是 `TryPathReserve` 被高频重入（车在原地图路径里来回试）。判据：`n` 的增长速率 vs `_tick_counter` 增量，以及同一 actor 的 `SET-RAIL` 是否在相邻 tick 覆盖同一条路径。
- **REG-274-F（低）**：`RESV-REL-PATH ... track=0x0`（`tile=218,235/224,241/888,236` 多次）——释放日志里出现 `track=0x0`（`TRACK_BIT_NONE`）。若这是 `UnreserveRailTrack(tile, INVALID_TRACK/0)` 被调用，说明有释放路径把"格"当成"轨"传参；也可能只是 `RESV-REL-PATH` 的字段取自 `GetRailReservationTrackBits` 之外的量。需读该行打点处的取值表达式确认（只读）。

### 9.6 关键补充证据（2026-10-22，`220,235` 一带的台账痕）

```
895-908: RESV-GC veh=24 tile=209,235 .. 219,235 track=0x0 why=free-path
910: RESV-GC veh=24 tile=220,235 track=0x0 why=free-path-gone
911: RESV-GC veh=24 tile=223,235 track=0x0 why=free-path-gone
913: RESV-GC veh=24 tile=226,235 track=0x0 why=free-path-gone
914: RESV-GC veh=24 tile=230,235 track=0x0 why=free-path-gone
944: RESV-LEDGER-FORGET veh=24 why=free-path mask=0x1 released=59 kept=0 phase=choose-track
945: CRT veh=24 order=1 dir=1 origin=255,235 td=8 found=1
955: DEPOT-ARR veh=24 spd=0 real=10(1) curType=1 stuck=1 tileDepot=0 tx=211 ty=235 destTx=265 destTy=231 destDepot=0 destResv=0 tileEqDest=0 depotDir=-1 enterDir=2 tt=10 impl=10
```

**读法**：`track=0x0` + `why=free-path-gone` 是**整格预留位**（隧道/桥头/道口/站台这类由 `*_map.h` 单 bit 承载的位）在台账里的记号——`free-path-gone` 的意思是"释放前回读，该格的 plain-rail track 位上已经没有这一位了"，于是 GC **只删账本、不改地图**。若那格真是整格位，这一位就永远留着（没有任何 plain-rail 路径能释放它）。

于是出现闭环：

1. `TryReserveRailTrack()`（`pbs.cpp:1025`）对这些格走的是整格分支——`IsTunnel && !HasTunnelReservation` / `IsBridge && TryReserveRailBridgeHead()` / `IsLevelCrossing && !HasCrossingReservation` / `Station && !HasStationReservation`；**唯一可订条件全是"尚未被预留"** ⇒ 位一旦残留，之后返回 false。
2. `TryReserveRailTrackdir()` 的 own-chain 豁免（`pbs.cpp:993-999`）加了 `IsPlainRailTile(tile)` 守卫（KI-370 为修 `rail_map.h:40` 断言而加）⇒ **整格位即使记在自己链名下也不认成功**。
3. 扩展每 tick 走到同一格 ⇒ `reserve-fail` ⇒ backout ⇒ 下一 tick 原样重来。405 轮零进展由此产生。

⇒ REG-274-D **已由代码证实（不再是猜测）**：

`R3RReleaseReservationLedgerChecked()`（`pbs.cpp:488-555`）的释放分支只有两种——
1. `TileType::Station && HasStationRail`（`:511-538`，整格位，正确释放）；
2. `TileType::Railway && IsPlainRailTile && HasTrack && HasBit(GetRailReservationTrackBits, track)`（`:540-547`）。

**没有 `TileType::TunnelBridge`、也没有 `TileType::Road(level crossing)` 分支** ⇒ 隧道/桥头/道口的整格位一律落到最后那句 `ours=false` 的兜底：`R3RResvLedgerDropBit()` + `RESV-GC ... why=<why>-gone` —— **只删账本、不动地图**。这就是 `220,235 / 223,235 / 226,235 / 230,235` 打成 `why=free-path-gone track=0x0` 的确切来源。

闭环（已在代码里逐条核对）：

1. 整格位留在图上；
2. 下一轮扩展走到该格，`TryReserveRailTrack()`（`pbs.cpp:1025`）对隧道/桥/道口只有"尚未被预留"才返回 true（`:1080-1092` / `:1046-1064`）⇒ 恒 false；
3. `TryReserveRailTrackdir()` 的 own-chain 豁免（`:993`）带 `IsPlainRailTile(tile)` 守卫 ⇒ 整格位连"记在自己名下"都不认；
4. 回到 `train_cmd.cpp:14609` ⇒ `reason=reserve-fail` ⇒ backout ⇒ 下一 tick 同一格重来。

⇒ **405 轮全失败的机制 = 台账释放漏了整格位分支，制造出一批永久占位；随后列车被自己（或存档带入）的占位钉死在单格上。**

修法方向（待拍板，未落地）：给 `R3RReleaseReservationLedgerChecked()` 补 `TileType::TunnelBridge` 与 `IsLevelCrossing` 分支，判定与释放保持"书主必须是本链 + 格上无车"两条前提（与 Station 分支同构，`:520-533`），释放走 `UnreserveRailTrack(tile, FindFirstTrack(bits))`；不要改 `TryReserveRailTrackdir` 的 `IsPlainRailTile` 守卫（那是 KI-370 为防 `rail_map.h:40` 断言而加的）。

### 9.7 下一步（按优先级）

1. 只读定位 `TryReserveRailTrackdir()`（`pbs.cpp`）的全部 `return false` 分支，并在失败点加**只读** dump（tile 类型 / plain / 预留位 / trackdir / actor），复跑一次即可定性 REG-274-D。
2. 确认 `RESV-REL-PATH` 的 `track=` 取值表达式（REG-274-F）；若确是 `track=0` 误传，属清理型 bug（不影响订位正确性但会掩盖真实轨道）。
3. `EXTEND-BACKOUT-SKIP` 仍是 0 —— 需要一次真正跨桥/隧道的场景，才能验证 §6.5。

### 9.8 「是不是预留主人台账（ledger）引入的？」—— **不是**（已用历史版本证伪）

玩家 2026-10-22 追问。结论：**这个缺口在台账引入之前就存在，台账只是让它从"静默跳过"变成"有日志可查"。**

取证（`git grep` 历史提交 `8d10a666f6`，即 2026-09-29 稳定检查点，台账尚未引入时）：

```
8d10a666f6:src/train_cmd.cpp:11818: static void R3RReleaseReservationSafe(uint32_t key, TileIndex tile, Track track, const char *why)
8d10a666f6-11835:  if (IsValidTile(tile) && GetTileType(tile) == TileType::Station && HasStationRail(tile)) { ... return; }
8d10a666f6-11852:  const bool ours = IsValidTile(tile) && GetTileType(tile) == TileType::Railway &&
8d10a666f6-11853:                  IsPlainRailTile(tile) && HasTrack(tile, track) &&
8d10a666f6-11854:                  HasBit(GetRailReservationTrackBits(tile), track);
8d10a666f6-11855:  if (ours) UnreserveTrack(tile, track);
8d10a666f6-11856:  R3RDbgWrite("RESV-GC ... why=%s%s\n", ..., ours ? "" : "-skip");
```

⇒ **旧实现同样只有 Station + plain rail 两类**，隧道/桥/道口同样落到"不动地图"，只是日志后缀是 `-skip` 而不是现在的 `-gone`。台账改造（`R3RReleaseReservationLedgerChecked`，`pbs.cpp:488`）是把这个函数原样搬进 pbs.cpp 并加书主校验，**分支集合没变**。

另外一层更关键的旁证：`UnreserveRailTrack()`（`pbs.cpp:1190-1257`）本身**一直具备**完整整格释放能力——
- `TileType::Road` + `IsLevelCrossing` → `SetCrossingReservation(false)`（`:1224-1230`）
- `TileType::Station` + `HasStationRail` → `SetRailStationReservation(false)`（`:1232-1237`）
- `TileType::TunnelBridge` + rail → `SetTunnelReservation(false)` / `UnreserveRailBridgeHeadTrack(tile,t)`（`:1239-1256`）

所以不是"引擎不会放"，而是"R3R 的释放函数根本没把这几类格送进 `UnreserveRailTrack()`"。KI-207c（第 125/126 轮）只补了 Station 这一类，缺口从那时就留下了。

台账带来的**唯一**副作用是：兜底分支会 `R3RResvLedgerDropBit()` 删掉账本行。这一条不加重问题（旧代码连账本都没有），甚至对后续释放略有利——行不在时 `R3RResvLedgerOwner()` 返回 `R3R_RESV_NO_OWNER`，按"missing row 保留旧行为"放行。真正钉死列车的是**图上那个没人放掉的位**，与账本无关。

⇒ 归因更新：**KI-422 = KI-207c 未推广到隧道/桥/道口的旧缺口（存量 bug），非台账回归。** 台账的价值在于这次终于能看见它。

### 9.9 玩家追问：账本会不会引入「站台（进站/出站）」相关缺陷 —— 定性结论

玩家 2026-10-22 追问，桥隧他认为可控，真正担心的是站台进出。逐条查了三处代码，结论分三层：

**（一）出站：账本完全不参与决策，结构上不可能被它卡住。**
- 账本对「引擎自己放的位」只观察不决策：`R3RResvLedgerNote()` 的 CLEAR-* 分支（`pbs.cpp:366-393`）只做 `R3RResvLedgerDropRow()` + 打 `RESV-REL`，**不动地图**。
- R3R 自己的几何清扫站台分支（`train_cmd.cpp:11694-11735` + 释放 11770-11780）直接调 `SetRailStationReservation(rec.tile, false)`，**根本不问账本**（不经 `R3RReleaseReservationLedgerChecked`）。
- ⇒ 出站时平台位由引擎 `ClearPathReservation()` 清、账本只删行；R3R 清扫也绕开账本。两条路都不存在"账本拒绝 ⇒ 车出不去"。

**（二）进站：只有一处沾账本，且方向是「放宽」不是「收紧」。**
唯一调用点 `train_cmd.cpp:14517-14523`（`ExtendTrainReservation` 的 `stale-own-bit`）：当挡路的位是**自己的**（`r3r_backout_ledger == own_key`）时，账本**授权**释放它并重试一次 —— 账本在这里让进站更容易，不是更难。只有 `-notowner` 返回 false 才会跳过这次授权，而 `-notowner` 的触发条件是「正向表 `_r3r_resv_owner_of_bit` 与反向行 `_r3r_resv_by_owner[key]` 不一致」，正常耦合/解挂会把行整体搬到新链头（`pbs.cpp:569-574` 归一到 `v->First()`、`:589` 重新写回、`:598` 传 key==新头），不产生这种不一致 ⇒ 非常规路径。

**（三）唯一需要盯的站台类缺口：路点的「无主整格位」清理。**
- 平台的引擎级释放被 `IsRailStationTile()` 守卫 ⇒ **路点不在内**（KI-207c 的原始缺口），路点整格位只能靠 R3R 清扫或审计回收。
- 账本在此多给了一个跳过理由：`RESV-GC ... why=...-notowner(veh=X)`（`pbs.cpp:504-508`），而旧代码没有这一条、会按地图直接判归属。
- 但这条**有日志**，不像桥隧走 `-gone` 静默；且审计的整格回收（`train_cmd.cpp:17042-17050`）仍能清「无车且 walk 不到主人」的路点位。

**⇒ 总结定性（已写 KI-423）**：
1. 站台的「进站/出站动作」**不会**被账本卡住——账本在出站侧零决策权、在进站侧只放宽；
2. 账本确实**新增**了一个站台/路点位的跳过理由 `-notowner`，但它只影响「台上多余整格位是否及时清掉」（画面/挡路），不影响列车进出站；
3. 与桥隧的真差异在于：桥隧是**连引擎都没能力判归属**（无 walk），站台/路点有 walk 可用，所以站台侧最坏情况是「位多留一会儿」，桥隧侧才是「位永久留」。

**疑点 REG-274-H（待验证，暂不影响结论）**：`R3RResvLedgerForgetMask()`（`pbs.cpp:267-274`）把 `"flip"` 列为 PATH-only，但全仓库 `R3RResvLedgerForget(` 调用点的 reason 只有 `decouple/couple/free-path/destroy/resync`，**没有一处传 "flip"**。若逻辑翻转会改变链头而账本行未重归一，翻转后平台/路点行可能挂在旧链头 id 上（行本身不会卡住地图，但会让 `-notowner` 与审计 `ledger_dead` 的判读变模糊）。需要一次「翻转后立刻出站」的日志来确认是否出现 `-notowner`。

