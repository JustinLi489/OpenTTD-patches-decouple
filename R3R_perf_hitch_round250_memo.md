# 第 250 轮：B 路修好后仍卡顿 —— 563 格预留「铺了又拆」活锁

日期：2026-10-19
现场：`build\R3R_debug.log`（589 073 B，mtime 19:41:39），产生者 `build\openttd.exe` @19:32:23（51 891 712 B，= KI-382 闭环检测修复版）。
本轮性质：**纯取证 + 定性**，尚未改码。

---

## 一、日志标签分布（全部 11 000 余行的构成）

| 行数 | 标签 |
|---|---|
| **10 714** | **RESV-GC** |
| 94 | LR-TRACE |
| 51 / 51 | CLEAR-PATH / UNRES-RAIL |
| 36 | GEO |
| 27 | RESV-AUDIT-WATCH |
| 20 | RESV-LEDGER-FORGET |
| 19 | CRT |
| 14 | ORD-IDX-WATCH |
| 11 | ARRANGE-IN / -NDH-DONE / -RFC-DONE / -IC-DONE |
| 8 | SEGTR-SNAP |
| 7 / 7 | TRP / TPR-WALK |
| 6 | **LOOP-CLOSED** |
| 3 | RESV-AUDIT-SUMMARY |
| 1 | SPD-DROP / ATTACH-FAIL / VALIDATE-FAIL / REVERSEDIR / REVERSEDONE |

**RESV-GC 占全部日志的 ~97%。**

## 二、决定性读数：563 格「铺一遍、立刻拆一遍」

`RESV-LEDGER-FORGET` 共 20 行，其中 **17 行是 `released=563 kept=0`**：

```
行  385: RESV-LEDGER-FORGET veh=0 why=free-path mask=0x1 released=17  kept=0
行  968: RESV-LEDGER-FORGET veh=0 why=free-path mask=0x1 released=563 kept=0
行 1535: RESV-LEDGER-FORGET veh=0 why=free-path mask=0x1 released=563 kept=0
行 2100: RESV-LEDGER-FORGET veh=0 why=free-path mask=0x1 released=563 kept=0
行 2667: ... 3233: ... 3798: ... 4363: ... 4928: ... 5495: ... 6061: ... 6626: ... 7191: ... 7758: ... 8326: ... 8892: ... 9457: ... 10022: ... 10587: ... 11154:  (全部 released=563)
```

对应 `LOOP-CLOSED` 6 行，参数逐字相同：

```
行  403: LOOP-CLOSED veh=0 origin=188,526/9 tile=188,526/9 steps=564 capped=0
行 2102: 同上
行 4930 / 7193 / 7761 / 10589: 同上
```

尾部原文（`Get-Content -Tail 40`）是一次完整的拆除，逐格一行：

```
RESV-GC veh=0 tile=188,564 track=0x1 why=free-path
RESV-GC veh=0 tile=188,563 track=0x1 why=free-path
... (逐格递减) ...
RESV-GC veh=0 tile=188,527 track=0x1 why=free-path
RESV-LEDGER-FORGET veh=0 why=free-path mask=0x1 released=563 kept=0
TRP veh=0 mf=188,526 real=0 type=0 dest=4294967295 spd=0 fp=0 look=0 one=0 mstuck=0 fto=0 => ok=0 res=0
```

**判读**：veh=0 每一轮都把整圈 563~564 格的预留铺出来（走满一圈才由闭环检测发现 `origin==tile`），随即 `free-path` 把 563 格逐格拆掉；下一轮从头再来。`steps=564` = 环周长，`capped=0` 表示是「绕回起点」判据命中的，不是步数兜底。

## 三、卡顿的三重放大（按量级排序）

### 3.1 第一重：`R3RDbgWrite` 每次一行一次 fopen/fclose

`src\r3r_perf.h:224-239`：

```cpp
inline void R3RDbgWrite(const char *fmt, ...)
{
	if (!R3RDbgOn()) return;
	R3RPerfCounters &p = R3RP();
	p.dbg_writes++;
	const uint64_t t0 = R3RPerfNowNs();
	FILE *dbg = fopen("R3R_debug.log", "a");   // ← 每次调用打开一次
	if (dbg != nullptr) {
		va_list ap; va_start(ap, fmt);
		vfprintf(dbg, fmt, ap);
		va_end(ap);
		fclose(dbg);                            // ← 每次调用关闭一次
	}
	p.dbg_ns += R3RPerfNowNs() - t0;
}
```

`RESV-GC` 的六个打印点全部走这个函数（`src\pbs.cpp:393/403/412/422/433/440`）。
⇒ 一轮释放 = **563 次 CreateFile + 563 次 CloseHandle**；一轮铺路再叠上 563 次预留/台账插入。
Windows 上 `fopen(...,"a")` 每次都要定位文件尾、CloseHandle 要落元数据，单次约 5~30 µs，563 次约 **3~17 ms** —— 一帧 30 ms 的量级。

### 3.2 第二重：`R3RResvLedgerForget` 是逐行循环（`src\pbs.cpp:473-503`）

```cpp
for (const R3RResvLedgerRec &row : rows) {
    ...
    if (!R3RReleaseReservationLedgerChecked(key, row.tile, row.track, why)) { ... }
    ...
}
```

`R3RReleaseReservationLedgerChecked()` 每格都要：查 `_r3r_resv_owner_of_bit`（unordered_map）、判 tile 类型、`UnreserveTrack()`、再打一行日志。**563 格 × 每格一次哈希查找 + 一次日志 fopen**，全部在同一个 tick 内完成。

### 3.3 第三重：563 格预留本身

每轮要沿环走 564 步（`steps=564`），每步一次预留 + 一次台账插入（`_r3r_resv_by_owner[key].push_back`，注意这是 `std::vector` 逐条 push，可能多次 realloc），然后全部丢弃。**这些工作对游戏状态零贡献**——下一轮结果完全一样。

## 四、与原生的差别在哪里

原生 OpenTTD 的同场景：这台车一 tick 内要么预留成功（十几格的前瞻距离），要么预留失败并标记 stuck，**不存在「走满一圈 564 格才发现不对」这一步**。R3R 的 `IsReservationLookAheadLongEnough()` 判据要求预留到「够长」，在闭合环上永远不够长，于是只能一直往前铺，铺到绕回原点才被 KI-382 的闭环检测拦下。

⇒ **KI-382 把「单 tick 死循环（永久卡死）」降级成了「每轮 564 步的活锁（可运行但很贵）」**。死锁变成活锁，卡死消失，卡顿留下。

## 五、猜测根因（待验证）

**G-1（主）**：`LOOP-CLOSED` 只是「这一轮放弃」，没有任何**负缓存**。下一 tick 同一列车、同一位置、同一订单，会重新走完 564 步再放弃。→ 修法方向：闭环命中后在本 tick 的这次 `TryPathReserve` 直接判「不可长预留」，让列车按普通 PBS 前进（或标 stuck），并且**同一 (veh, origin, order) 在若干 tick 内不再重试长预留**。

**G-2**：`LR-TRACE` 只有 94 行而 `steps=564` —— 说明这 564 步不是递归（02 层），是 `ExtendTrainReservation` 里的**平铺循环**。所以 T-0 深度上限、T-1 环检测、T-2 无进展、T-3 节点预算这四层护栏**一个都拦不到它**（它们都挂在递归上）。真正拦住它的是 KI-382 新加的闭环检测。

**G-3（需实测）**：`RESV-AUDIT-SUMMARY` 仅 3 条且 tick 号最大 2048，而 `LOOP-CLOSED` 有 6 条、`RESV-GC` 上万行 —— 需确认 2048 tick 之后审计是否停摆（**疑点 REG-250-A**：若审计按 2048 tick 跑，2000+ tick 的日志里应该有多条 SUMMARY；只 3 条可能是暂停态 `_tick_counter` 不前进，也可能审计被抑制，需看 `RESV-AUDIT-WATCH` 27 行的时间分布）。

## 六、未确认项

- **U-1**：这一轮 563 格铺/拆的**实际频率**（每 tick 一次？还是每 N tick？）。日志无 tick 号，`RESV-LEDGER-FORGET` 也不打 tick。需要给该行加 `tick=` 或用 `R3R_perf.log` 的 `dbg_writes/dbg_ns` 计数反推。
- **U-2**：`look=0 one=0 res=0` 与 `dest=4294967295`（INVALID_TILE）说明这列车此刻**没有有效订单终点**；为何还在反复长预留需要确认（是 `OT_WAIT_COUPLE` 之外的什么订单，还是订单已失效）。
- **U-3**：`R3R_perf.log` 的 `dbg_ns` 累计值能否直接量出日志占比（最有说服力的旁证）。
- **U-4**：`RESV-AUDIT-WATCH` 27 行、`ORD-IDX-WATCH` 14 行对应的逻辑是否也在热路径。

## 七、初判修法（未落地，待拍板）

1. **日志降级（最省事、立竿见影）**：`RESV-GC` 的逐格行改成**汇总一行**（`RESV-GC-SUM veh= why= released=563 tiles=188,527..188,564`），或按 `why` 做边沿/节流；`RESV-LEDGER-FORGET` 一行的信息量已经够定位。
2. **停止活锁（治根）**：`LOOP-CLOSED` 命中时给该 (veh, order index, origin tile) 记一个短期「长预留不可用」标记，让后续若干 tick 直接走普通 PBS；或直接把该列车按 stuck 处理并要求玩家改道（对闭合环这是唯一正解）。
3. **减少无谓预留**：闭环检测改在**走第 2 圈之前**判（现在 `steps=564` 已走满一圈，可接受），但更重要的是：一旦 `origin==tile` 就别再逐格 `UnreserveTrack`——直接在预留阶段就不落账（现在落账再拆账，做了两遍功）。

## 八、复测判据

1. 同场景 `RESV-GC` 行数从万级降到**个位数**（改汇总后应为每轮 1 行）。
2. `LOOP-CLOSED` 出现次数从「每轮一次」降到「每次场景切换一次」（有负缓存）。
3. 帧率/手感回到接近原生；`R3R_perf.log` 的 `dbg_ns` 占比从（待测）降到 <1%。
4. **负判据**：正常直线/站台/车库场景的预留与释放行为逐字不变（`RESV-GC` 汇总数值相同）。

## 九、第 250 轮补充二：性能计数器取证——9.4ms 不是日志，是 stuck 重试（2026-10-19）

### 9.1 取数（`build\R3R_perf.log`，736 条 `PERF-TICK`）

先做一次样本有效性筛选：**680/736 条是 `loco=0.0/s`**（`TrainLocoHandler` 调用次数为 0），说明采样窗口落在暂停态；此时 `glTrains` 是冻结的陈旧均值（这就是 9.16ms 反复出现的真因）。**只有 56 条 `loco>0` 可用**，逐字原文（行号为 `R3R_perf.log`）：

```
1042: PERF-TICK loco=60.5/s 285ms/s | coupleH=0.0/s 0ms/s | plat=0.0/s 0ms/s | resv=0.0/s 0ms/s | edgeGate=4.9/s 0ms/s
      | ctrl=0.0/s 0ms/s | coll=0.0/s 0ms/s | spd=30.2/s 0ms/s | vp=30.2/s 0ms/s | dump=0ms/s | mov=0.0/s
      | ord=60.5/s 0ms/s | prv=0.0/s 0ms/s | load=60.5/s 0ms/s | dep=60.5/s 0ms/s | dec=60.5/s 0ms/s | wpr=0.0/s 0ms/s
      | stk=30.2/s 284ms/s | rev=0.0/s 0ms/s | sub=284ms/s resid=1ms/s | nl=100ms/s glTrains=9.16ms
 908: PERF-TICK loco=52.6/s 184ms/s | ... | stk=26.3/s 182ms/s | rev=0.0/s 0ms/s | sub=183ms/s resid=1ms/s | nl=35ms/s glTrains=6.63ms
 906: PERF-TICK loco=71.2/s 192ms/s | ... | stk=19.4/s 109ms/s | rev=0.4/s 79ms/s | sub=190ms/s resid=1ms/s | nl=72ms/s glTrains=5.09ms
```

### 9.2 结论 D-1～D-5（均为实测，非推断）

- **D-1**：`sub ≈ loco`、`stk ≈ sub` ⇒ `TrainLocoHandler` 的时间**几乎 100% 落在 stuck 块**（`src\train_cmd.cpp:18778-18842`）。同时 `ctrl=0.0/s`、`mov=0.0/s`、`coll=0.0/s` ⇒ 这列车**根本没进过 TrainController**，一直没动。
- **D-2**：stuck 块里唯一的重活是 `:18800 TryPathReserveWithResultFlags(consist)`（长预留入口）⇒ 每 tick 一次完整的 564 步长预留尝试，全部以失败/闭环收尾。实测 **284ms/s ÷ 30.2/s = 9.4ms 每次**。
- **D-3**：**日志不是主因**（§3.1 的 563×fopen 估算被部分证伪）。`dbgWrite` 全程实测 `0.5~15.6/s`、`5~12ms/s`，只占 284ms/s 的 <4%——因为 563 行 burst 全会话只发生 **19 次**（`RESV-LEDGER-FORGET ... released=563` 共 17 次 + 2 次 562/564），不是每 tick 都刷。U-3 由此闭环。
- **D-4**：`glTrains=5.09~9.16ms`、`nl=35~124ms/s` 是**同一件事的上层视图**（整段列车 tick 的平均耗时 vs 其中 handler 之外的部分），不是另一处开销；`spd/vp/load/dep/dec` 等桶实测均为 `0ms/s`。
- **D-5**：判读纪律——`R3R_perf.log` 里必须先筛 `loco>0` 的行，暂停期样本（680/736）完全不可用，`glTrains` 会给出冻结的假值。

### 9.3 修法追加（第 4 条）

(d) **stuck 重试负缓存**：`TryPathReserveWithResultFlags` 返回 `!TPRRF_RESERVATION_OK` 时，把 `(veh, origin tile, cur_real_order_index, dest_tile)` 记入短期失败表，在 `path_backoff_interval` 到期前直接 `return true`（不重跑 564 步）；`LOOP-CLOSED`（闭合环，订单几何上不可达）应记更长的期限并单独打一行 `LR-NEGCACHE veh= why=loop`。这一条与 (b) 互补：(b) 让"这一轮"不再走 564 步，(d) 让"后续每 tick"不再重跑。

### 9.4 判据追加

5. `stk=*.x/s NNNms/s` 降到 **<10ms/s**（`loco=/s` 不变，说明仍是每 tick 进块，但块内不再做长预留）。
6. `ctrl=0` 保持（该车确实没动），`nl`/`glTrains` 同步回落到帧时间的 <5%。
7. 负判据：正常可预留的列车 `stk` 桶仍为 0，`rev` 行为不变。

## 十、今日（2026-10-19）A/B 两个问题的登记（口径以本节为准）

玩家口径：**A = 朋友那次崩溃；B = 18:14 那次卡死。** 两者都发生在长预留（long reserve）里，但**不是同一条路径、也不是同一个刹车**。

| 维度 | A 路（朋友崩溃，KI-378） | B 路（18:14 卡死，KI-382） |
|---|---|---|
| 无限的是什么 | `TryLongReserveChooseTrainTrack` ↔ `ChooseTrainTrack` **互相递归** | `ExtendTrainReservation` 里**一个 `while` 循环** |
| 靠什么刹车 | 「下一层预留失败」⇒ 递归终止 | 「第二圈重复预订必然失败」⇒ 循环终止 |
| 刹车怎么坏的 | 每层都预留成功（`long_enough` 恒假 ⇒ 一直往上跨信号） | 2026-10-16 的 KI-361 让「自己的台账位」也算预留成功 |
| 耗尽什么 | **空间（栈）** ⇒ 崩溃，有 crash log | **时间（单 tick 不返回）** ⇒ 冻结，无 log、不崩 |
| 怎么确认的 | 反汇编逐帧符号化，`TryLongReserveChooseTrainTrack`/`ChooseTrainTrack` **交替 54 帧** | 日志 `LR-BUDGET` **0 次**、`depth` 最大 13 ⇒ **排除递归**，锁定平铺循环 |

### 10.1 登记要点（只登记，本轮不改码、不构建）

- **A 路（KI-378）**：崩溃版 Build Oct 18 01:36:32 不含任何 R3R 护栏；此后已落 T-0 深度上限 128 / T-1 环检测 / T-2 无进展 / T-3 节点预算 1024。判据来源=反汇编符号链（`[10] TryLongReserveChooseTrainTrack` ↔ `[11] ChooseTrainTrack` 交替 54 帧）。
- **B 路（KI-382 → KI-383）**：10-19 已加闭环检测（`LOOP-CLOSED`），死锁解除；残余为「每 tick 重跑 564 步」的活锁（实测 9.4ms/次、284ms/s），见 §九。
- **两起事故的共同点（本备忘录结论 C-1）**：两条路的刹车都是**间接推断**——A 路靠"下层会失败"、B 路靠"第二圈会失败"，都不是显式判据（深度/环/步数/无进展）。所以任何让"失败"变得不失败的改动（KI-361 就是）都会同时把刹车拆掉。

### 10.2 未确认

- U-A1：A 路现场"千层级"与 128 上限的关系（崩溃版无上限，故不能反推现版需要多少层）。
- U-B1：B 路活锁的负缓存期限取值（`path_backoff_interval` 还是独立 300 tick）未定。

## 十一、第 251 轮：B 路修法落地（玩家拍板"按推荐修法来"）

### 11.1 拍板内容

两条一起做，落点全在 `src\train_cmd.cpp`、不改头文件（增量合法）：

1. **（d）闭环失败负缓存**：`ExtendTrainReservation()` 因"绕回起点"中止时，把 `(车, 起点格, 起点 trackdir)` ⇒ `(结果格, 结果 trackdir, new_tracks, enterdir)` 记入短期表，`R3R_LOOP_MEMO_TICKS = 60` tick 内同参数再进入直接返回该结果，不再重走 564 步。日志 `LOOP-MEMO veh= origin= tile= age=`（与 `LOOP-CLOSED` 共用 `R3REDGE_WALKLOOP` 边沿枚举，不新增枚举）。
2. **（KI-384）步数上限下沉 —— 撤销，不做**。曾把 `r3r_walk_limit` 由 `Map::Size() * 2` 改成显式 4096 步，被玩家当场否掉并已回退原值。
3. **玩家口径（2026-10-19，硬性）**：**不要**给 `ExtendTrainReservation` 的平铺循环加"走 N 步内纯轨道就 break"这类固定步数上限。理由=合法延伸的长度由车辆本身（车长+制动距离+速度）与线路几何决定，不存在一个安全的固定常数；这类上限会在"很长的合法预留"上提前截断，把能走的车判成走不通，属于靠猜的护栏。允许的护栏只有"回到起点/已访问格"（几何上必不合法）这一类**判据**，不是长度常数。
4. 因此 KI-384 只能按"回访检测 + 无进展"两条下沉，`r3r_walk_limit` 保持 `Map::Size() * 2` 不变（该值仅作为"2 倍地图格数"的理论完备上界，实际永不到达，不构成行为约束）。

### 11.2 为什么这样做是安全的（三条）

- **只复读"失败"结论**：缓存条目只在该次延伸"绕回起点、okay=false"时写入；命中也只返回同一个 `okay=false`，与重走一遍得到的判定一致（环上没有外部障碍时才可能绕回起点）。
- **天然自限**：key 含起点格与起点 trackdir；列车一旦能动，起点就变，key 变，缓存自然失效。真正卡住的车才命中缓存。
- **有期限**：60 tick 后条目过期并重算（`_tick_counter` 暂停时不前进，所以暂停期间不会老化）。表满 4096 条即整体清空，不会无界增长。

### 11.3 预期读数

- `stk` 桶从 ~284ms/s 降到 **<10ms/s**（每 60 tick 才付一次 9.4ms）。
- `loco=/s` 不变（仍每 tick 进 handler），`ctrl=0` 保持。
- 日志出现 `LOOP-MEMO veh=…`（同一次卡顿只出少数几行，因为边沿枚举 128 帧窗口）。

### 11.4 判据

1. 闭合环场景 `LOOP-MEMO` 出现且 `LOOP-CLOSED` 行数大幅下降（不再是每 tick 一次）。
2. `stk=*.x/s` 的 `ms/s` 降一个数量级。
3. **负判据**：正常可延伸的长预留不出现 `LOOP-MEMO`（`stk` 仍 0），列车照常订路、照常开车。
4. **负判据**：闭环上的列车 60 tick 后仍会重新尝试一次（`LOOP-CLOSED` 稀疏出现），不会永久放弃。
5. 无 `rail_map.h:168` / `pbs.cpp` 断言（缓存路径不碰地图）。

## 十二、第 252 轮：去掉 `2 * Map::Size()` 步数上限，只留几何环检测

### 12.1 玩家口径（硬性）

"2 倍 map size 的上限也最好不要有，保留一个环形检测就好了。" ⇒ `ExtendTrainReservation()` 里**不允许出现任何步数/长度比较**，只允许几何判据。

### 12.2 为什么"回到起点"这一条不够（必须先说清，否则去掉上限就是死循环）

现判据只有 `tile == origin.tile`：

- 起点在环上 ⇒ 绕一圈回到起点，能抓（KI-384 现场 564 步正是这种形态）。
- 起点**在支线上**、进入环后绕行 ⇒ 永远不回起点格；KI-361 之后自链位被 `TryReserveRailTrackdir()` 当作预留成功，"撞自己预留"这条隐式边界也没了 ⇒ 无限循环，退回 KI-371 的冻结。

所以"环形检测"的完备形式不是"回到起点"，而是"**任一已访问格重复**"。

### 12.3 用哪种 key：`(tile, Track)` 而不是 `(tile, trackdir)`

合法延伸订下的是互不相同的**预留位**：一格最多 2 个 `Track`（X/Y 或左右），一个 `Track` 一位。同一 `(tile, Track)` 出现两次 ⇔ 又走回自己已订的那一位 ⇔ 环。

用 `trackdir` 会把"同一 Track 反方向再走一次"算成两个不同 key（4 个/格），既放松了判据（真环可能晚 2 圈才认出）又与"预留位"语义不符。用 `Track` 恰好等价于预留位。

### 12.4 自然上界（这不是人为常数）

不同 `(tile, Track)` 组合最多 `2 * Map::Size()`，所以循环步数的上界是**判据推出来的**，不是额外加的条件；任何合法路径都远达不到。去掉原上限后，代码里不再有任何"走 N 步就 break"。

### 12.5 代价（实测口径）

| 维度 | 代价 |
|---|---|
| 时间 | 每走一格一次哈希插入（~40ns 量级）。正常延伸几步~几十步 ⇒ 每次调用 +0.2~2µs。相比 `ft.Follow()` 自身（GetTile / 预留位检查 / 90 度判定）可忽略；相对原来的 O(1) 上限比较是净增项，但只发生在"需要延伸预留"的调用上。 |
| 内存 | 集合条数 = 本次实际步数。正常几十条（含 bucket ~1KB）；病态整图无信号大环理论最坏 `2 * Map::Size()` 条（4096² 图 ~3350 万条、数百 MB），但判据在绕回**第一格**时就返回，走不到上界，故实际不可达。 |
| 行为 | 命中分支与旧版逐字一致：`return PBSTileInfo(tile, ft.old_td, false)`（okay=false 交给寻路），不再标记 stuck。负缓存（§十一）不变。 |

### 12.6 实现落点（仅 `src\train_cmd.cpp`）

1. `#include <unordered_set>`。
2. 循环前插入 `origin` 的 `(tile, Track)`（等价于旧版"回到起点"那一条，但合并进集合）。
3. 每步 `tile = ft.new_tile; cur_td = ...` 之后 `insert` 新格；插入失败即环。
4. 删掉 `r3r_walk_limit` / `r3r_walk_steps > r3r_walk_limit`；`r3r_walk_steps` 只留作日志计数。
5. `LOOP-CLOSED` 的 `capped=%d`（步数上限标志）改为 `track=%d`（重复的 Track），`steps=` 保留。

### 12.7 构建自证（2026-10-19 20:25）

复用 `_tmp_inc_build.cmd`（未新建 .cmd）：`GUARD: incremental is safe`；日志 `[3/3] Linking CXX executable openttd.exe`；`build\R3R_incbuild.done` = `EXIT_CODE=0`；`src\train_cmd.cpp` 20:22:48 → `build\openttd.exe` 20:25:26（51 942 912 B）；`read_lints` 0 条；`r3r_walk_limit` 全文件 0 命中。发布树 `build-release` 未重编。

首轮两次编译失败并已修：①`INVALID_DIAGDIR` 在本仓库不存在（`DiagDirection` 是 `enum class`，无效值写作 `DiagDirection::Invalid`，见 `src\direction_type.h:92`）；②`R3RLoopMemo::tick` 是 `uint32_t` 而 `_tick_counter` 是 `uint64_t`，列表初始化报 `C2397` 需显式收窄。

### 12.7 判据

1. 同名环场景仍出现 `LOOP-CLOSED`（`steps=` 与旧版量级一致，约 564），日志不再有 `capped=` 字段。
2. **新增**：起点在支线、进环绕行的场景也会在其中止（旧版只有"回到起点"一条时不会）——可用环形线 + 侧线构造。
3. **负判据**：正常延伸（直轨 / 岔道 / 站台 / 隧道桥）不出现 `LOOP-CLOSED`，`stk=` 仍 0，列车照常订路。
4. **负判据**：源码里不存在 `r3r_walk_limit` 或任何步数比较（复查 `ExtendTrainReservation`）。
5. 无 `rail_map.h:168` / `pbs.cpp:144` 断言。

## 十三、第 253 轮：护栏覆盖面收口（只读核对，未改码）

### 13.1 起因
第 252 轮落地后，上一轮清单还留着两个"待核"：① `FlushDeferredDetermineCombineNormalShuntMode()` 是否也是选道/长预留的可重入入口（若可重入，第 252 轮只给 `ExtendTrainReservation` 上刹车就不够）；② `ChooseTrainTrack` 里 `for (;;)` + `orders.SwitchToNextOrder()` 是否会空转（§1.4af 已证伪，但当时没写进 KI-384）。

### 13.2 结论（逐条带锚点）
1. `src\signal.cpp:1789-1801` 的函数体只有两段：对 `_deferred_lookahead_combined_normal_shunt_mode` 逐项 `DetermineCombineNormalShuntModeWithLookahead()`、对 `_deferred_determine_combined_normal_shunt_mode` 逐项 `SetCombinedNormalShuntModeSignalToShunt()`，然后清表。**没有任何寻路/预订调用**（不碰 `TryPathReserve` / `ChooseTrainTrack` / `ExtendTrainReservation`），它只决定"合并正常/调车信号走哪种路由"这一个信号位。
2. 三处调用点全在**返回侧**：`src\train_cmd.cpp:13851`（隧道桥出口可用格算完）、`:14089`（隧道桥分支里 `ChooseTrainTrack(...)` 之后）、`:16411`（lambda 里 `TryPathReserve(consist)` 之后）⇒ 不构成"预订 → flush → 预订"的环。
3. 真正进入长预留的路径只有 `TryPathReserve` → `TryLongReserveChooseTrainTrack`（T-0 深度闸 / T-1 / T-2 / T-3 都在这里），而 `ExtendTrainReservation` 是它的**被调者** ⇒ 第 252 轮的 `(tile, Track)` 回访集合正是闸门外唯一刹车，位置正确。
4. `ChooseTrainTrack` 的 `for (;;)` / `orders.SwitchToNextOrder()` 有界：每条订单分支要么推进 `cur_real_order_index`，要么在 `OT_NOTHING`/订单表尾 return。
5. **收口**：不存在第三条无闸路径。KI-384 的"结构性不对等"至此变为"B 路由单一几何判据兜底"，两条实现路径不再有护栏真空。

### 13.3 登记疑点 REG-253-A（低，未验证）
`_r3r_loop_memo` 只在过期或容量满时清理，**不随车辆销毁清理**（`src\train_cmd.cpp:13210` 定义、`:13220` 查询、`:13234` 过期删除、`:13393-13396` 写入；全文无车辆析构/卖车钩子）。vehicle index 会被新造车辆复用。

- 期望：新车不受影响（TTL `R3R_LOOP_MEMO_TICKS = 60`）。
- 触发条件：旧车留下 memo 后 60 tick 内，新车以**同一 origin tile + 同一 origin trackdir** 发起长预留。
- 实测：无（需构造"卖车 → 同格同向造车"，观察 `LOOP-MEMO` 是否替新车打出）。
- 影响：新车最多 60 tick 内只走普通 PBS（不做长预留），不崩溃、不丢订单，自愈。
- 若确认的修法：在台账/负缓存既有 GC 处按 `Train::GetIfValid` 过滤，或借 `R3RResvLedgerForget` 那类"车没了就抹账"的钩子。

### 13.4 文档回填（本轮唯一改动）
- KI-383：状态由「待实现」改「已修（第 251 轮负缓存 + 第 252 轮几何环判据）」，补两条落地（负缓存 key/TTL/写入点、第 252 轮集合判据），并**修正复测口径**——判据 ②「`LOOP-CLOSED` 每场景只出现一次」实际是**每 60 tick 一次**，正常形态是"零星 `LOOP-CLOSED` + 大量 `LOOP-MEMO`"。
- KI-384：标题由「未修」改「已修（第 251/252 轮）」，补「护栏覆盖面审计（第 253 轮）」四条 + 复测判据索引。
- 本轮**未改任何源码、未构建**；可执行文件仍是 2026-10-19 20:25:26 的 `build\openttd.exe`（51 942 912 B，含 KI-382/383/384 全部修复）。发布树 `build-release` 未重编。

## 十四、复测搁置声明（2026-10-19 玩家决定）

玩家原话：「我希望把复测搁置备忘，然后把今日的搁置三项逐步做一下。」本轮先把**复测**这一层统一搁置并落盘，再逐项做搁置的功能项。

### 14.1 搁置口径（照第 190 轮先例）

状态**不回退**、判据**不删除**、探针**不清除**；只在条目状态里加「复测搁置」四个字，表示"实现侧到此为止，剩下的只是玩家某天顺手跑一次日志"。触发重新取证的唯一条件是**出现新的现场日志**，不再以"等复测"为由把条目挂在待办里。

### 14.2 今日进入复测搁置的清单

| 条目 | 实现轮次 | 被搁置的只是这一步 | 判据留存 |
|---|---|---|---|
| KI-378 | 第 249 轮（`R3R_LONG_RESERVE_MAX_DEPTH` 深度闸） | 崩溃是否不再复现 | KI-378「复测判据」 |
| KI-382 | 第 249 轮（环闭合不 `break` 而交棒） | 是否不再整机冻结 | KI-382 判据 ①~⑤ |
| KI-383 | 第 251 轮（`_r3r_loop_memo` 负缓存）+ 第 252 轮（`(tile,Track)` 回访集合） | `RESV-GC` 万级是否回落、`stk` 桶是否降一个数量级 | KI-383 判据 ①~④（②已按 60 tick 口径修正） |
| KI-384 | 第 251/252 轮（B 路唯一刹车＝几何判据） | 结构护栏是否够用 | KI-384「复测判据」索引 |
| REG-253-A | 第 253 轮（仅登记） | 负缓存跨车号复用是否误伤新车 | 本备忘 §13.3 |

### 14.3 搁置后仍需记住的两点

1. **REG-253-A 是唯一有时效的**：它要"卖车 → 60 tick 内同格同向造车"的构造，正常存档很难碰巧遇到。将来日志里若出现**新车 id** 的 `LOOP-MEMO`，直接按 §13.3 结案，不必重跑实验。
2. **`stk` 判据有前提**：KI-383 的"`stk` 从 284ms/s 降到 <10ms/s"成立的前提是负缓存把每次 stuck 重试的整段延伸短路掉；若将来复测发现 `stk` 仍高，说明还缺"stuck 块级失败负缓存"（第 250 轮补充里列的修法 (d) 的另一半），那时按 KI-383 追加项处理。

### 14.4 可执行文件口径（搁置期不变）

`build\openttd.exe` = 2026-10-19 20:25:26（51 942 912 B，含 KI-382/383/384）；`build-release` 仍停在第 248 轮发布版，**不含**今日任何修复。
