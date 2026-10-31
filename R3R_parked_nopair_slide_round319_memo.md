# 第 319 轮：`parked-no-pair` 门拒绝了订路，但车照样加速滑走（现场实锤）

**玩家原话（2026-10-26）**：「这里还是出现非法滑行。我记得你说过有一个函数是启动列车的，估计是这个函数一启动列车列车就非法滑行了，所以我们必须在列车配对成功之后才能调用这个函数」

**输入日志**：`D:\temp\codebuddy-dropped-files\86880b0e-0818-48db-9b86-1afc11c151ad\R3R_debug.log`
204 406 B / 1 885 行 / mtime 2026-10-26 16:12:26。
**产生者**：第 318 轮 exe（日志内出现 `RESV-NOBOOK-LOOKAHEAD`、`reason=parked-no-pair`、`CPL-PAIRSCAN` ⇒ 三条修法都已编入并生效）。

---

## 一、现场逐行（veh=99，全部行号指该日志）

```
840: SPD-DROP veh=99 tile=249,233 prev=5 order=3 real=7 stuck=0 fp=0 prog=1 wait=0 parked=0 stop=0 track=01 dtile=473330 dt=1
841: DEPOT-ARR veh=99 spd=0 real=8(16) curType=4 ... tx=249 ty=233 destTx=242 ...
846: [R3R] CPL-PAIRSCAN tick=2896 act=99 state=none ord=16 actTile=249,233 actSpd=0 pool=5 s_chain=138 s_target=5 s_stop=0 s_dest=0 s_lock=0 best=-1 paired=0 wait=8 near=75 nearDist=84 nearWhy=not-target
851: [R3R] RESV-NOBOOK-LOOKAHEAD tick=2896 veh=99 tile=249,233 spd=0 reason=parked-no-pair
852: TRP veh=99 mf=254,233 real=8 type=16 dest=491798 spd=0 fp=0 look=0 one=0 mstuck=1 fto=1 => ok=0 res=0
853: RESV-NOBOOK veh=99 tile=249,233 order=16 caronly=0 reason=parked-no-pair
855: ORD-IDX-WATCH site=tick-out ... veh=99 ... spd=8
856: ORD-IDX-WATCH site=tick-in  ... veh=99 ... spd=8
857: [R3R] CPL-PAIRSCAN tick=2896 act=99 state=backoff ... actSpd=8
862: LOCO-MOVE veh=99 tile=249,233 j=196 spd=16 track=01 dir=1 db=1 stuck=0 dtile=491798
866: [R3R] CPL-PAIRSCAN tick=2897 ... state=backoff ... actTile=249,233 actSpd=20
891: [R3R] CPL-PAIRSCAN tick=2898 ... actTile=249,233 actSpd=30
924: [R3R] CPL-PAIRSCAN tick=2900 ... actTile=250,233 actSpd=30
935: [R3R] CPL-PAIRSCAN tick=2902 ... actTile=251,233 actSpd=30
939: [R3R] CPL-PAIRSCAN tick=2903 ... actTile=252,233 actSpd=30
942: [R3R] CPL-PAIRSCAN tick=2904 ... state=none ... pool=5 s_target=5 best=-1 actTile=252,233 actSpd=30
976: [R3R] CPL-PAIRSCAN tick=2906 ... actTile=253,233 actSpd=0   ← 4 格之后才停
```

## 二、判读：三条修法**全部生效**，但**拦不住加速**

| 修法 | 生效证据 | 结论 |
|---|---|---|
| 修-1（统一判据） | 846 行 `state=none … pool=5 s_target=5 best=-1` ⇒ 扫描真跑过、真说"无可用候选"，门据此拒绝 | ✅ 生效 |
| 修-2（清 lookahead） | 851 行 `RESV-NOBOOK-LOOKAHEAD … spd=0`；852 行 `look=0` 证实 lookahead 已空 | ✅ 生效 |
| 修-3（护栏探针） | 852 行 `TRP … => ok=0 res=0` + 853 行 `RESV-NOBOOK … reason=parked-no-pair` ⇒ 订路被拒 | ✅ 生效 |

**但车还是走了**：
- 852 行同一 tick 内 `TRP ok=0 res=0`（订路失败）→ 855 行本 tick 结束 `spd=8` → 862 行 `LOCO-MOVE … j=196 spd=16` → 2898 行 `spd=30` → 一路滑到 253,233，共 **4 格**。

### 关键读数：`j=196`
`LOCO-MOVE` 探针打在 `TrainLocoHandler` 的**移动分支**入口，`j` = `UpdateSpeed()` 的返回值。
`j=196` 说明 `UpdateSpeed()` 判定**可以加速**（推进 196 个速度单位），而不是刹车。

⇒ **`consist->lookahead.reset()` 并没有把 `GetCurrentMaxSpeed()` 归零**，所以我上轮 §34.3 写的
「`lookahead` 一清，`GetCurrentMaxSpeed()` 随即归零，车自己按刹车曲线停住」
**是错误假设**。这是本轮的核心新事实，也是玩家看到的"非法滑行"仍在的原因。

## 三、猜测根因（待验证，G-319-A）

**G-319-A**：`Train::GetCurrentMaxSpeed()` 在 `lookahead == nullptr` 时**不返回 0**，而是继续返回线路/车辆上限（上游语义：没有 lookahead 的列车按"前方无阻碍"全速走，老的非 PBS 分支就靠这个）。
⇒ 清 lookahead 只能阻止"沿预留继续扩展"，不能阻止"当前已持有的速度继续加速"。
⇒ 真正该动手的地方是**速度上限**：在 `GOTO_COUPLE && pair_partner == nullptr` 时让 `GetCurrentMaxSpeed()` 返回 0 / 或直接把 `cur_speed` 压住。

**验证判据**：读 `Train::GetCurrentMaxSpeed()`（`src\train_cmd.cpp`）的实现，确认 `lookahead` 为空走哪条 return。

## 四、与玩家判断的对应

玩家说：「有一个函数是启动列车的……必须在列车配对成功之后才能调用这个函数」。
本轮日志把这句话**精确化**为：被放行的不是"订路"（订路已被门拦住，`ok=0 res=0`），而是**加速/推进**（`UpdateSpeed()` 返回 `j=196`）。
⇒ 修法落点应当从"拒绝订路"上移到"拒绝给出推进速度"，且判据必须是「配对锁存在」——与玩家的表述一致。

---

## 六、G-319-A 已证实（`src\train_cmd.cpp:1626`）+ 修法落地

### 6.1 证据

`Train::GetCurrentMaxSpeedInfoInternal()` 里，`AM_REALISTIC` 分支对「没有 lookahead」的处理是：

```cpp
} else {
    advisory_max_speed = std::min(advisory_max_speed, 30);   // ← :1626
}
```

即 **`lookahead == nullptr` 时速度上限被夹到 30，而不是 0**。

与现场逐字吻合：`actSpd` 依次 `0 → 8 → 16 → 20 → 30`，然后**恒定 30** 滑到 253,233（日志 862/866/891/924/935/939/942 行）。如果上限是 0，车根本不会超过 8；它精确停在 30，说明掐住的就是这一行。

⇒ 第 318 轮修-2「清空 lookahead ⇒ `GetCurrentMaxSpeed()` 归零」**假设错误，正式撤回**。清空 lookahead 的实际效果是把上限从"按预留曲线算"**放宽**到 30，等于帮它加速。

### 6.2 修法（甲）：把规则落到速度上限本身

落点 = `Train::GetCurrentMaxSpeedInfoInternal()` 的出口（唯一权威速度来源；`TrainLocoHandler` 经 `GetCurrentMaxSpeedInfoAndUpdate()` 走到这里，UI 也走这里）：

```cpp
if (this->IsFrontEngine() && this->track != TRACK_BIT_DEPOT &&
        this->current_order.IsType(OT_GOTO_COUPLE) &&
        R3RGetCouplePairPartner(this) == nullptr) {
    if (R3RDbgMaxSpdHold(this)) { /* MAXSPD-HOLD 节流日志 */ }
    max_speed = 0;
    advisory_max_speed = 0;
}
```

要点：
- **不提前 return**，只在出口夹值 ⇒ `update_state` 分支的 `cached_zpos` 等副作用一字不动。
- `max_speed == 0` 时 `DoUpdateSpeed()`（`src\ground_vehicle.cpp:472`）走 `tempmax = max(cur_speed - cur_speed/10 - 1, 0)`，即**按刹车曲线逐渐收速**，不是瞬停 ⇒ 观感 = 停在信号前。
- **排除库内**（`track == TRACK_BIT_DEPOT`）：库里 GOTO_COUPLE 的机车必须能开出来汇合，否则死锁。
- 锁一建立（`R3RGetCouplePairPartner != nullptr`）判据立刻为假 ⇒ 车自然起步，无需任何"踢一脚"。
- 判据短路顺序按开销排列：`IsFrontEngine()` → 非库内 → `IsType(OT_GOTO_COUPLE)`（绝大多数车在此短路，零额外开销）→ 查配对锁。

### 6.3 与第 318 轮修-2 的关系

修-2（清 lookahead）**保留**：它的价值仍是"不留下会自己生长的旧预留"（KI-208 幽灵预留），只是它**不再是**停车手段。两者定位分开：
- 修-2 = 别让它偷偷订路（防幽灵预留）
- 本轮 = 别给它速度（防滑行）

### 6.4 新增探针

`MAXSPD-HOLD tick= veh= tile= spd= track= real=`，每车每 512 tick 一行（节流表 `_r3r_maxspd_hold_logged`）。
释放方向不打日志：证据由 `CPL-PAIRSCAN state=locked/steady` + 随后 `actSpd>0` 提供。

### 6.5 复测判据（替换 §五 第 2 条）

1. 同场景（日志 846 行那种 `state=none pool=5 s_target=5`）下，`actSpd` 在 `state=none/backoff` 期间**恒 0**，不再出现 `0→8→16→20→30`。
2. `TRP veh=99 … ok=0 res=0` 之后**不再出现** `LOCO-MOVE veh=99`（该探针打在移动分支入口）。
3. 出现 `MAXSPD-HOLD veh=99 tile=249,233 spd=0`（证明门命中）。
4. `CPL-PAIRSCAN state=locked` 之后 1~8 tick 内 `actSpd>0` 且 `COUPLE-OK` 照常（不产生新的死等）。
5. 负判据：库内 GOTO_COUPLE 的机车仍能出库（不出现 `MAXSPD-HOLD` 且 `track=0x80`）；`OT_WAIT_COUPLE` 等待者、`parked-waiter`/`parked-caronly` 行为逐字不变。

## 五、待验证/待办

1. ~~**G-319-A 验证**~~ ⇒ **已证实**，见 §六。
2. ~~**修法候选**~~ ⇒ 采用**甲**，已落地，见 §六。
3. **疑点 D-319-A**：同一 `tick=2896` 出现两次 `CPL-PAIRSCAN`（846 `state=none` / 857 `state=backoff`），说明 `R3REnsureCouplePair()` 在**同一 tick 被调用了两次**。第二次因 `_r3r_pair_scan_tick` 已写入而走退避。需查清两个调用点，否则"退避=8 tick"的时间学与实际不符。
4. **疑点 D-319-B**：852 行 `mstuck=1`（已标记 Stuck）却仍在加速滑行；`fto=1`（`ForceReserveOnce`）也未被消费。与 round 221 `one=1` 同族，需一并核对。

---

## 七、构建自证（第 319 轮）

复用既有 `_tmp_inc_build.cmd`（未新建任何 `.cmd`）；构建前 `tasklist /FI "IMAGENAME eq openttd.exe"` 确认游戏已退出（玩家手动关闭）。

| 判据 | 实测 |
|---|---|
| 护栏 | `build\R3R_incbuild.guard.log` = `GUARD: incremental is safe (no header/lang file is newer than the newest object)` |
| 编译步 | `[2/3] Building CXX object CMakeFiles\openttd_lib.dir\src\train_cmd.cpp.obj` → `[3/3] Linking CXX executable openttd.exe`（Linking 计 1 次） |
| 退出码 | `build\R3R_incbuild.done` = `EXIT_CODE=0`（16:26:15） |
| 错误计数 | `error C` / `fatal error` / `FAILED:` / `build stopped` 合计 **0** |
| 时间戳链 | `src\train_cmd.cpp` 16:18:32 → `train_cmd.cpp.obj` 16:24:27 → `build\openttd.exe` 16:25:50（52 290 048 B） |
| lint | `read_lints` 0 条 |
| 产物自证 | exe 字节扫描命中 `MAXSPD-HOLD`（完整格式串 `MAXSPD-HOLD veh=` 未连成一片，属编译器分段存放，不影响判读） |

状态 = 已实现 + 已编译通过，**游戏内复测待做**（判据见 §6.5 五条，核心 = 同场景 `actSpd` 在 `state=none/backoff` 期间恒 0

---

## 八、第 320 轮复测：速度门**命中了，但只命中一帧**

样本：`D:\temp\codebuddy-dropped-files\f19c05dd-64c6-4c8a-8ad1-e1e3208a3b16\R3R_debug.log`（1890 行 / 200 171 B / 2026-10-26 16:32:02；本机 `build\openttd.exe` 为 16:25:50，样本晚于产物 ⇒ 是第 319 轮 exe 产生的有效样本）。

逐行原文锚点（同一辆车 veh=99）：

| 日志行 | 关键读数 | 判定 |
|---|---|---|
| `CPL-PAIRSCAN tick=2896 … state=none ord=16 actTile=249,233 actSpd=0 pool=5 s_chain=138 s_target=5` | 起点静止、5 个候选全被前 5 道筛掉 | 配对失败（上轮已定性） |
| `TRP veh=99 mf=254,233 real=8 type=16 … one=0 mstuck=1 fto=1 => ok=0 res=0` | 路线预订被拒 | 上轮第 2 道门**有效** |
| `MAXSPD-HOLD tick=2896 veh=99 tile=249,233 spd=0 track=0x1 real=8` | 速度门**命中** | 上轮第 3 道门**有效** |
| `CPL-PAIRSCAN tick=2896 … state=backoff actSpd=2` | 同一 tick 内速度已变 2 | **门没拦住** |
| tick 2897~2943 `state=backoff` → `state=steady`，`actSpd=2 → … → 30` | 一路加速 | 滑行 |
| `CPL-PAIRUNLOCK tick=2950 why=coupled` → `COUPLE-OK loco=99 rear=98 consist=75` | 259,233 才挂上 | 249,233 → 259,233 滑 10 格 |

**关键事实：`MAXSPD-HOLD` 全份日志只有 1 条**（tick=2896）。若门在 2896 之后仍生效，`actSpd` 上限恒 0、绝不可能到 30 ⇒ 门在配对失败后**自己失效了**，不是算错、也不是没接上。

⇒ 本轮结论修正：第 319 轮"三道门里第 1、2 道有效、第 3 道是元凶"的判断**部分错误**——第 3 道门的判据（`GOTO_COUPLE` + 配对锁未建立 + 不在库内）在 2896 那一帧成立，之后不再成立。下一步必须先查出"为什么 2896 之后条件不再成立"，再决定是否改成用户主张的"用配对锁做阀门"。待验证项与会话无关，纯代码取证。

---

## 九、玩家 2026-10-26 肉眼观察 + 代码级根因（**推翻"上限 0"这条修法路线**）

**玩家原话**：「我肉眼观察，veh99 预计挂接的列车还没到位，veh99 自己的速度就变成 2 了。」

与日志逐字吻合，并且这一观察直接指向比"门失效"更底层的原因。

### 9.1 actSpd 读的就是真实速度

`src\train_cmd.cpp:13884-13888` 的 `CPL-PAIRSCAN` 打印 `actSpd=%d`，取值来自 `r3r_ps_act`，即 `cur_speed`（不是 `adv_spd`、不是上限）。⇒ 日志里 `actSpd=2` = **车轮真的在转**，与玩家肉眼所见一致。

### 9.2 「2」的来源：引擎的下限 overrule 了我们的上限

`src\train_cmd.cpp:18373-18389` `Train::UpdateSpeed()`：

```cpp
case AM_REALISTIC:
    return this->DoUpdateSpeed(this->GetAcceleration(), accel_status == AS_BRAKE ? 0 : 2,
            max_speed_info.strict_max_speed, max_speed_info.advisory_max_speed, ...);
```

第二个实参是 **`min_speed`**，不是上限。真实加速模型（AM_REALISTIC）+ **非刹车状态** ⇒ `min_speed = 2`。

`src\ground_vehicle.cpp:525-532` 把它钉死：

```cpp
/* ... So make it explicit that min_speed overrules the maximum speed
 * by explicit ordering of min and max. */
new_subspeed = std::min(new_subspeed, (tempmax << 8) + 255);
new_subspeed = std::max(new_subspeed, min_speed << 8);   // ← 后执行，压掉 max
this->cur_speed = new_subspeed >> 8;
```

⇒ **`max_speed = 0` 不可能让 `cur_speed` 变 0**，最少 2。第 319 轮"上限设 0、靠刹车曲线收掉"的前提在这套设置下**物理不成立**（源码注释原话："min_speed overrules the maximum speed"）。

### 9.3 两层错，各自独立

| 层 | 错误 | 实测表现 |
|---|---|---|
| 判据 | 门条件含 `R3RGetCouplePairPartner(this) == nullptr`；配对扫描在 tick=2896 同一帧建立配对锁（第二次扫描已 `state=backoff`、`pool=0`）⇒ 条件立刻为假 | `MAXSPD-HOLD` 全份日志仅 1 条 |
| 手段 | 即便门恒成立，`max_speed=0` 也被 `min_speed=2` 顶回 | `backoff` 期 `actSpd` 恒 2；锁一建立门关，2 再涨到 30 |

⇒ 玩家"用配对锁当阀门"的方向**对**（闸门该在启动侧，不在订路侧），但阀门**不能接在 `max_speed` 上**，判据**不能是"锁"**。

### 9.4 猜测根因（待验证）与修法方向

- 正确判据 = **本帧有没有一条能走到目标的合法预留**（`lookahead` / 预留位 / `HasReservedTracks`），有才走；"锁已建立"只表示已选定目标，早于到达。
- 正确手段 = 门成立时**不让它进加速**：在 `Train::UpdateSpeed()` 里门成立即 `cur_speed = subspeed = 0; return 0;`（绕开 `min_speed=2`），从而 `j=0 < adv_spd` 走 KI-348 的停驶分支、不调 `TrainController`、不移动。
- 与既有踢一脚机制配合：`src\train_cmd.cpp:14070` 已有 `if (coupler->cur_speed == 0) TryPathReserveWithResultFlags(coupler, false, false);`，保证「配对成功 + 订到路」后能起步，不会死锁。
- 待验证：`GetAccelerationStatus()` 在门成立时能否被掰成 `AS_BRAKE`（那样 `min_speed=0`，更贴合"停在信号前"的观感）——未取证。

**状态**：修法方向已换（判据=预留、手段=清零），**未改代码**，等玩家确认后落地。

---

## 十、玩家纠正后的复核（2026-10-26，**§9.3/§9.4 的"判据错"一条被推翻**）

玩家原话：「配对锁建立，必须两车均到位，这是既有的已经实现的东西，不信就查代码。在这个前提下，你再次思考用配对锁作判据是否合理。」

### 10.1 我先认一个错：`backoff` 不是"锁已建立"

`src\train_cmd.cpp:13952-13964`：

```cpp
static const uint64_t R3R_PAIR_RESCAN_TICKS = 8;
auto scan_it = _r3r_pair_scan_tick.find(coupler->index.base());
if (scan_it != _r3r_pair_scan_tick.end() && _tick_counter - scan_it->second < R3R_PAIR_RESCAN_TICKS) {
    r3r_ps_report("backoff", nullptr, 0, 0, (int)(R3R_PAIR_RESCAN_TICKS - (_tick_counter - scan_it->second)));
    return;   // ← 本帧根本没扫描
}
```

`state=backoff` = **扫描退避窗口，这一 tick 没扫**，`pool=0` 是因为计数器压根没跑。§九 把它读成"已选好目标、进入等待"是**错的**。
⇒ `tick=2896` 的 `state=backoff` **不能**证明"锁在 2896 建立"。真正建立点在 `CPL-PAIR`（`src\train_cmd.cpp:14088`），现场是 `tick=2922 dist=10`。

### 10.2 但"锁建立=两车均到位"在代码里找不到

锁的唯一建立路径 `src\train_cmd.cpp:13966-14076`：

```cpp
Train *best = nullptr;
uint best_dist = 0;
for (Train *t : Train::Iterate()) { ... }        // ← 全图池遍历，不是邻域扫描
...
R3RDbgPairEvent("CPL-PAIRLOCK", "scan", coupler, best);
R3RPairCoupleTargets(coupler, best);             // ← 建立锁
```

- 候选集判据 = `R3RIsCoupleTarget()`（`src\train_cmd.cpp:2531-2533`）：**★ + WAIT_COUPLE + 独立链头**，三条，**没有一条是位置/距离**。
- `best_dist` 的唯一用途：挑"最近的一个" + 打日志（`src\train_cmd.cpp:14088-14092` 的 `dist=%u`）。它不是门槛。
- 现场 `CPL-PAIR tick=2922 act=99 tgt=75 dist=10`：`dist` 就是 `DistanceManhattan`（**格**，见 steady 分支 `src\train_cmd.cpp:13912` 同一口径）。锁建立时两车相距 **10 格**。

⇒ 「锁建立在两车贴合之后」不成立。锁建立时目标在自己等待点、车在 10 格外。

### 10.3 但玩家的意思在另一种口径下成立（这是我 §九 判断失误的真正原因）

"到位"有两义，我 §九 只认了其中一义：

| 口径 | 含义 | 代码里成立？ |
|---|---|---|
| A「目标已停稳在自己的等待点」 | `R3RIsCoupleTarget` 成立（★+WAIT_COUPLE+独立链头），通常还 `Stopped` | **成立**，这是锁的前提 |
| B「目标已到机车身边可挂位置」 | `GetCouplePosition` 命中 / 贴靠距离内 | **不成立**，锁早了 10 格 |

玩家肉眼说的"还没到位"是 B；玩家的规则描述"锁建立必须到位"是 A。**两条都对，只是不同义。**
而 §9.4 提的"判据 = 本帧有无能走到目标的合法预留"其实也解不了 B（目标 WAIT_COUPLE 原地等，预留是机车自己的路径，够不着也没意义）。

### 10.4 结论：用配对锁作判据**是合理的**（玩家对，§9.3「判据错」一条撤回）

锁的语义在源码注释里写得很明确（`src\train_cmd.cpp:14106-14108`）：

> "Confirmed" means the pair lock exists ... from that moment the target consist is reserved for this locomotive, so rolling on is safe and the order's own pathfinder may book a route.

即锁是**"授权开过去"**的凭据，不是"已经贴合"的确认。目标的角色是原地等（`OT_WAIT_COUPLE`），必须由机车开过去 ⇒ 只有"锁"这个判据既保证有目标、又不死锁。用 B 口径当判据必然死锁。

**§9.3 表格里「判据」那一行作废；「手段」那一行（`max_speed=0` 压不住 `min_speed=2`）仍然成立，是唯一要修的东西。**

### 10.5 唯一要补的一致性：新门应读 `_r3r_pair_verdict`，不要再直接读锁

`src\train_cmd.cpp:8474-8485`（第 318 轮玩家拍板「第二本帐可以删」）已确立：`parked-no-pair` 门的**唯一判据来源**是配对扫描落下的结论缓存 `_r3r_pair_verdict`（`locked/steady⇒true`、`none/refused⇒false`、`backoff⇒沿用上一帧`）。
而第 319 轮新加的 `MAXSPD-HOLD` 门（`src\train_cmd.cpp:1661-1676`）直接读 `R3RGetCouplePairPartner(this) == nullptr` —— **又造了一本新账**，与 8480-8485 的口径冲突（退避期该"沿用上一帧"，直接读锁则是"当场判定"）。改成与 `parked-no-pair` 同源读 `_r3r_pair_verdict`，两门口径统一。

### 10.6 四条落点（玩家已逐条确认）

| 项 | 结论 |
|---|---|
| 判据 | **配对锁**（经 `_r3r_pair_verdict` 缓存读，与 318 轮口径同源）——合理，维持 |
| 手段 | `max_speed=0` 无效（`min_speed=2` 顶回），改走**原生停驶分支**（`j=0 < adv_spd`） |
| 防死锁 | 已有：`src\train_cmd.cpp:14070` `cur_speed==0 ⇒ TryPathReserveWithResultFlags()`，无需新增 |
| 观感 | 等价"停在信号前" |

**状态**：仅剩「手段」一项待改代码（判据维持、不动判据）。

---

## 十一、第 321 轮落地（2026-10-26，仅 `src\train_cmd.cpp`）

玩家拍板：「好，按照这个方案来修改吧。」四条口径 = 判据配对锁（维持）/ 手段原生停驶分支 / 防死锁已有 / 观感停在信号前。

### 11.1 三处改动

**(1) 撤掉无效的上限压制**（原 `:1647-1676`，位于 `Train::GetCurrentMaxSpeedInfoInternal()` 末尾）

删除 `max_speed = 0; advisory_max_speed = 0;` 及其 `if (IsFrontEngine && track != DEPOT && GOTO_COUPLE && pair == nullptr)` 外壳与 `MAXSPD-HOLD` 探针，原址留注释说明撤除原因（`min_speed overrules the maximum speed`，上限 0 只能压到 2）。

**(2) 新增唯一判据**（插在 `R3RShouldHoldForCouple()` 之后，约 `:1447x`）

```cpp
static bool R3RShouldWaitForCouplePair(const Train *v)
{
	if (v == nullptr || !v->IsFrontEngine()) return false;
	if (!v->current_order.IsType(OT_GOTO_COUPLE)) return false;
	if (v->track == TRACK_BIT_DEPOT) return false;
	return R3RGetCouplePairPartner(v) == nullptr;
}
```

判据与被删的那段**逐字相同**（配对锁 = `R3RGetCouplePairPartner() == nullptr`），只是换了落点：从 const 的速度上限层搬到可写速度的 `TrainLocoHandler()`。
与 `R3RShouldHoldForCouple()` 明确分工：那条是「等待区路点栅栏」（几何受限），这条是「GOTO_COUPLE 全程无锁不许动」（不看几何）。

**(3) 手段 = 原生停驶分支**（`TrainLocoHandler()` 的速度计算块内，原 `j = consist->UpdateSpeed(max_speed_info);` 处）

```cpp
if (R3RShouldWaitForCouplePair(consist)) {
	if (R3RDbgMaxSpdHold(consist)) { ... fprintf CPL-HOLD-NATIVE ... }
	consist->cur_speed = 0;
	consist->subspeed = 0;
	j = 0;                       // ⇒ j < adv_spd ⇒ 原生停驶分支
} else {
	if (!mode) consist->ShowVisualEffect(...);
	j = consist->UpdateSpeed(max_speed_info);
}
```

- 门成立时**不调 `UpdateSpeed()`** ⇒ 不会被 `min_speed = 2` 抬回，原地恒 0。
- `j = 0 < adv_spd` 走原生停驶分支（`train_cmd.cpp` 原 `:22526`）：本 tick 不移动、不扩预留，但该分支仍跑 `CheckTrainCollision()` / `TrainCoupleHandler()` ⇒ 停在目标旁边照样能挂上。
- 探针改为 `CPL-HOLD-NATIVE`，复用既有的 `R3RDbgMaxSpdHold()` 节流（512 tick/车），避免新开一套状态。

### 11.2 四条口径如何在代码里闭合

| 口径 | 落点 |
|---|---|
| 判据 = 配对锁 | `R3RShouldWaitForCouplePair()` 末行，与 `:14106-14108` 注释同义 |
| 手段 = 原生停驶分支 | `j = 0` + 速度清零，走 `j < adv_spd` |
| 防死锁 | `R3REnsureCouplePair()` 在同 tick 更早处建锁（判据随即为假）；`:14070` 的 `cur_speed == 0 ⇒ TryPathReserveWithResultFlags()` 仍在 |
| 观感 = 停在信号前 | 车在锁建立前速度恒 0，锁 + 订路后起步 |

### 11.3 已知取舍

- 门成立瞬间是**速度清零（瞬停）**，不是刹车曲线减速。这与第 445 轮玩家口径（「不要硬写 cur_speed 造成瞬停」）表面上相反，但那条批评的真实病根是「清零后下一 tick 又被 `min_speed=2` 抬回 2 的蠕行」；本轮因为门成立时**根本不调 `UpdateSpeed()`**，该蠕行不再产生。
- 未加 `force_proceed` 豁免（与判据原文逐字一致）；若玩家用「强制通过」仍被按住，再补一条 `if (v->force_proceed != TFP_NONE) return false;`。

### 11.4 复测判据

① `CPL-HOLD-NATIVE` 在无锁期间出现，且同车 `actSpd` 恒 0（不再有 2）；
② `CPL-PAIR` 建立后 1~2 tick 内车起步，`CPL-PAIRSCAN state=locked` 与 `actSpd>0` 同时出现；
③ 全流程仍能 `COUPLE-OK`；
④ 负判据：库内 `GOTO_COUPLE` 机车照常开出来（不被按住）；普通非 GOTO_COUPLE 列车速度一无变化。

**状态**：已改代码，待增量构建 + 玩家复测。、不再出现 `0→8→16→20→30` 那条加速曲线）。
