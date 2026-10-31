# 第 322 轮：配对锁解锁后挂车接近段被限速 5 / 30

## 一、玩家报告（原话）
> 不错，这次列车在该停的时候停下来了，我们的关注点可以改一下了。在配对锁的闸门解锁了之后，列车开始向被挂列车移动。但是，我注意到列车的移动速度是被某个函数限制的5和30，这不是我期望的。

输入日志：`D:\temp\codebuddy-dropped-files\1082ffb5-77eb-411b-bb47-0d3a98b98244\R3R_debug.log`
（300813 B，2026/10/26 17:15:35）

第 321 轮「无锁按住」已由玩家确认生效（该停时停下来了），本轮只处理"解锁后接近段速度上限"。

## 二、已确认的代码锚点（src\train_cmd.cpp，2026-10-26 读）

### A. 30 的两个来源
1. `src\train_cmd.cpp:1642-1644`（`Train::GetCurrentMaxSpeedInfoInternal()`，`UsingRealisticBraking()` 为真且 `lookahead == nullptr` 的 else 分支）：
   ```cpp
   } else {
       advisory_max_speed = std::min(advisory_max_speed, 30);
   }
   ```
   即"没有预留前视时兜底 30"。第 319 轮注释里"清空 lookahead 后上限被放宽成 30"指的就是这里。

2. `src\train_cmd.cpp:1546-1547`（`TCF_NO_DRIVING_CAB` 分支）：
   ```cpp
   constexpr int BACKWARDS_NO_CAB_SPEED_LIMIT = 32;
   max_speed = std::min<int>(max_speed, BACKWARDS_NO_CAB_SPEED_LIMIT);
   ```
   这是 **32** 而非 30，但现场 veh=51 是 `db=1`。若命中，读数会稳定在 32 附近，与"稳定 30"不完全吻合 ⇒ 暂列疑点 D-2。

### B. advisory 在两种刹车模型下都是硬上限
`src\ground_vehicle.cpp:472-542`（`DoUpdateSpeed`）：
- `:477-481`：`if (use_realistic_braking) { ... } else { max_speed = std::min(max_speed, advisory_max_speed); }`
- `:513-523`：realistic braking 时 `if (new_subspeed > (advisory_max_speed << 8) && new_lb_subspeed < new_subspeed) new_subspeed = Clamp(advisory_max_speed << 8, ...)`

⇒ 无论 `TBM_REALISTIC` 还是原始刹车，`advisory_max_speed` 都会实际压住 `cur_speed`。所以"5 与 30"只要落进 advisory 就一定是真限速，不是仅提示。

### C. 5 的来源（唯一会产生 5 的公式）
`src\train_cmd.cpp:1614-1616`：
```cpp
if (this->reverse_distance >= 1) {
    advisory_max_speed = std::min<int>(advisory_max_speed, ReversingDistanceTargetSpeed(this));
}
```
`ReversingDistanceTargetSpeed()` 在 `AM_REALISTIC` 且**非** realistic braking 时给 `((reverse_distance - 1) * 5) / 2`：
- `reverse_distance = 3` ⇒ **5**
- `reverse_distance = 7` ⇒ 15
- `reverse_distance = 13` ⇒ **30**

5 与 30 同时出现在这个序列里（13⇒30、3⇒5），而 `reverse_distance` 每前进一格减 1，正好构成"30→27→25→…→5"的递减阶梯。

`reverse_distance` 的赋值点（全 `src\*.cpp` 只有 4 处，R3R 代码零赋值）：
- 造车/读档 = 0
- 出库/换向清 0
- 倒车进库（`skipped` 相关）
- `tracerestrict` 的 `TRPRF_REVERSE_BEHIND`（`reverse_distance = cached_total_length + 8/16` 量级）

⇒ **疑点 D-1**：veh=51 在野外 `tile=249,234`，不可能走"倒车进库"，那 `reverse_distance` 只能来自地图上的 tracerestrict "reverse behind signal" 程序。这是玩家地图配置还是 R3R 触发，尚未实证。

### D. 其它已排除项
- `:1522 OT_LOADING_ADVANCE` 的 through_load 限速：非挂车接近段，排除。
- `:1568/1578` 站台接近（`15 * distance_to_go` / `25 * distance_to_go`）：倍数是 15/25，产生不出 5/30。
- `:1591 rail_depot_speed_limit`：非库内，排除。
- `:1604 current_order.GetMaxSpeed()`：订单不限速时为 `UINT16_MAX`。
- `:1606 GetBreakdownSpeed()`：需 `VehicleRailFlag::BreakdownSpeed`，日志无对应读数。
- `:1609 speed_restriction` / `:1612 signal_speed_restriction`：**未排除**，需探针读数（疑点 D-3）。

## 三、日志侧读数（已知）
- `CPL-PAIRSCAN ... veh=51 ... state=steady ord=16 actSpd=30`
- `LOCO-MOVE veh=51 ... spd=30` 连续多行，`tile` 不动
- `tile=249,234` 处速度序列出现过 `16 → 29 → 5 → 5 → 5 → 5 → 15 → 30 → 30`
- veh=51 在 `:2928` 前后 `CPL-PAIR act=99 tgt=75`，`db=1`

## 四、疑点清单（待验证）
- **D-1**：`reverse_distance >= 1` 是否在 veh=51 上成立？若成立，来源是 tracerestrict `REVERSE_BEHIND` 还是 R3R 自己的某条路径（R3R 代码全文零赋值 ⇒ 倾向地图配置）。
- **D-2**：`TCF_NO_DRIVING_CAB`（32）是否命中？若命中，日志应有 `NOCAB-LIMIT`。
- **D-3**：`speed_restriction` / `signal_speed_restriction` 是否为 5 / 30。
- **D-4**：`lookahead` 是否为 `nullptr`（即 30 是否来自 `:1643`）。
- **D-5**：`UsingRealisticBraking()` 真假（决定 `:480` 还是 `:513` 生效）。

## 五、本轮处置
不猜测改判据。先在 `GetCurrentMaxSpeedInfoInternal()` 返回处加**只读**分解探针
`MAXSPD-BREAK`（对 `IsFrontEngine() && current_order.IsType(OT_GOTO_COUPLE)`，
每车 512 tick 一行，复用既有 `R3RDbgMaxSpdHold()` 节流），一次复跑即可把 D-1..D-5 全部读出。

字段：`veh tile spd strict adv look nocab revd revspd spr sig ordmax realbrk db track maxTrack maxCurve`。

## 六、预置改法（待探针确认后择一/组合）
- 若 D-4 成立（30 = 无 lookahead 兜底）：对 `OT_GOTO_COUPLE` 且已建配对锁、非库内的前车，不再套 `:1643` 的 30，改由 `strict_max_speed` 与预留曲线决定。
- 若 D-1 成立且来源是 tracerestrict `REVERSE_BEHIND`：属原生"倒车到信号后"，不宜由 R3R 单方面忽略；需玩家拍板是"R3R 接近段忽略倒车距离限速"还是"改地图程序"。
- 若 D-3 成立：`signal_speed_restriction` 是信号速度适应，可对 `OT_GOTO_COUPLE` 前车置 `VehicleRailFlag::SpeedAdaptationExempt`（该旗标已存在，`:1611` 已判）。

## 七、复测判据（待改法落地后补）
1. `MAXSPD-BREAK` 能读出 5 与 30 各自来源字段。
2. 改动后挂车接近段速度不再稳定卡在 5 / 30。
3. 第 321 轮「无锁按住」不回归（该停仍停）。
4. `COUPLE-OK` 仍能达成。

## 八、构建自证（第 322 轮，2026-10-26）

- 构建入口：复用既有 `_tmp_inc_build.cmd`（未新建任何 `.cmd`）。
- 前置：`tasklist /FI "IMAGENAME eq openttd.exe"` → `No tasks are running`（玩家已关闭游戏）。
- 护栏：`GUARD: incremental is safe (no header/lang file is newer than the newest object)`。
- 链接：`build\R3R_incbuild.log` 尾部 `[3/3] Linking CXX executable openttd.exe`。
- 结果标记：`build\R3R_incbuild.done` = `EXIT_CODE=0`，LastWriteTime 2026/10/26 17:39:32。
- 产物：`build\openttd.exe` 52 291 584 B，LastWriteTime 2026/10/26 17:38:55；`src\train_cmd.cpp` 17:33:21 ⇒ 源码早于 exe。
- 产物字面量：`findstr /m /c:"MAXSPD-BREAK" build\openttd.exe` 命中 ⇒ 新探针已进二进制。
- `read_lints`：0 条。

## 九、给玩家的复跑指令与判据

1. 用 `build\openttd.exe`（2026/10/26 17:38:55，52 291 584 B）复跑同一场景：让机车执行 `GOTO_COUPLE`、配对锁解锁后向被挂列车移动。
2. 复跑后把 `R3R_debug.log` 整份丢回来（探针每车 512 tick 一行，体积很小）。
3. 我要读的唯一样式：`MAXSPD-BREAK veh=<id> tile=x,y spd= strict= adv= look= lookend= nocab= revd= revspd= spr= sig= ordmax= realbrk= db= track= maxTrack= maxCurve=`。
4. 归因表（一次读数即可定案）：
   - `adv=30` 且 `look=0` ⇒ 30 来自 `:1643` 无前视兜底。
   - `strict=32` 且 `nocab=1` ⇒ 命中倒车无驾驶室上限（读数会贴 32）。
   - `adv=5` 或 `adv=30` 且 `revd>=1`、`revspd` 等于该读数 ⇒ 命中 `ReversingDistanceTargetSpeed`（5 对应 revd=3、30 对应 revd=13）。
   - `adv=5`/`adv=30` 且 `spr` 或 `sig` 等于该读数 ⇒ 命中 tracerestrict 限速 / 信号速度适应。
5. 若日志出现 `MAXSPD-BREAK` 但 `veh=51` 的 `adv` 远大于 30，则说明限速发生在 `GetCurrentMaxSpeedInfoInternal()` 之外（`DoUpdateSpeed` 的 `tempmax`/`min_speed`），届时转查 `ground_vehicle.cpp:504-532`。

## 十、第 322 轮现场读数与根因定案（2026-10-26）

**输入**：`D:\temp\codebuddy-dropped-files\10ca3832-f7c1-44d2-829d-ecf515c6c670\R3R_debug.log`，240 935 B，LastWriteTime 2026/10/26 17:43:50；产生者应为含第 322 轮 `MAXSPD-BREAK` 探针的新 exe（口径自证见 §八）。`MAXSPD-BREAK` 全文命中 2 行。

**原文（两行逐字）**：

```
:849  MAXSPD-BREAK veh=99 tile=249,233 spd=0 strict=428 adv=30 look=0 lookend=-1 nocab=0 revd=0 revspd=-1 spr=0 sig=0 ordmax=65535 realbrk=1 db=1 track=1 maxTrack=428 maxCurve=65535
:1950 MAXSPD-BREAK veh=51 tile=249,234 spd=0 strict=428 adv=30 look=0 lookend=-1 nocab=0 revd=0 revspd=-1 spr=0 sig=0 ordmax=65535 realbrk=1 db=1 track=1 maxTrack=428 maxCurve=65535
```

**逐项判读（按 §九 归因表）**：

| 字段 | 读数 | 判定 |
|---|---|---|
| `strict` | 428 | 硬上限=车辆自身最大轨速，**未被压缩**（=maxTrack=428） |
| `adv` | 30 | 建议速度被压到 30 ⇒ 就是玩家看到的「30」 |
| `look` / `lookend` | 0 / -1 | **lookahead == nullptr** ⇒ 命中 `src\train_cmd.cpp:1643` 无前视兜底分支 |
| `nocab` | 0 | **排除** `:1546` 倒车无驾驶室上限（32） |
| `revd` / `revspd` | 0 / -1 | **排除** `:1615` `ReversingDistanceTargetSpeed()`（无倒车距离） |
| `spr` / `sig` | 0 / 0 | **排除** tracerestrict 限速与信号速度适应（`:1609`/`:1612`） |
| `ordmax` | 65535 | 订单未设限速 |
| `realbrk` | 1 | realistic braking 开启 ⇒ `:1643` 的 30 兜底生效 |
| `db` | 1 | 两车均处于 `DrivingBackwards`（倒车行驶）状态 |
| `maxCurve` | 65535 | 曲线限速无贡献 |

**根因定案（已实证，不再待验）**：接近挂车时 `lookahead == nullptr`，`GetCurrentMaxSpeedInfoInternal()` 落到 `src\train_cmd.cpp:1643` 的 realistic-braking 无前视兜底，把 `advisory_max_speed` 钉死在 **30**。与「倒车无驾驶室 32」「倒车距离 5/30」「tracerestrict」「信号适应」「订单限速」「曲线限速」六条候选全部无关（读数逐项归零）。

**由此产生的两个必须解释的追问（下一步取证重点）**：

- Q-1：为何 `lookahead == nullptr`？两车都在 `OT_GOTO_COUPLE` 且 `spd=0`，若 PBS 路线仍在，lookahead 不该为空。候选：①第 127 轮 `RESV-NOBOOK` 门禁（`cur_speed==0 && Station 格 && WAIT_COUPLE/car-only`）提前 `return TPRRF_NONE`；②`spd=0` 后 `TrainLocoHandler` 走停驶分支、根本不进 `TrainController`（第 242 轮 KI-348 已证实该分支**不寻路不预留**）；③路线终点已在脚下被消化。注：两车 `tile` 为 `249,233`/`249,234`（相邻格），`track=1`。
- Q-2：「5」的来源仍未取证。本轮两行都是 30，未见 5。`adv=30` 时玩家看到 5，说明 5 出现在**另一相位**（例如 lookahead 存在且 `lookend` 很小、或 `db` 在切换过程中），或 5 来自 `strict`。若下一份日志仍只出 30，则应把探针从「512 tick 节流」改为「`adv` 变化时打一行」（边沿触发），以捕捉 30→5 的跃变瞬间。

**口径修正**：§四曾把「30 来自无前视兜底」列为候选之一，现升级为**唯一成立项**；§五~§七 的其余候选全部证伪。

## 十一、因果链闭合（同一份日志的上下文读数，2026-10-26）

**Q-1 已答**：`lookahead == nullptr` 的直接原因是 **`parked-no-pair` 门拒绝预订整条路线**。同一 tick 的紧邻行逐字如下：

```
:847  RESV-NOBOOK veh=99 tile=249,233 order=16 caronly=0 reason=parked-no-pair
:848  DEPOT-ARR veh=99 spd=0 real=8(16) curType=16 ... destTx=278 destTy=240 ...
:849  MAXSPD-BREAK veh=99 ...
:850  CPL-S0-CHK veh=99 tile=249,233 real=8 order=16 stuck=0 fp=0 prog=1 wait=0 parked=0 stop=0 track=01 dtile=491798
:851  COUPLE-FAIL loco=99 order=16 tx=249 ty=233 x=3984 y=3736

:1947 TRP veh=51 mf=254,234 real=8 type=16 dest=491798 spd=0 fp=0 look=0 one=0 mstuck=1 fto=1 => ok=0 res=0
:1948 RESV-NOBOOK veh=51 tile=249,234 order=16 caronly=0 reason=parked-no-pair
:1949 DEPOT-ARR veh=51 spd=0 real=8(16) curType=16 ... destTx=278 destTy=240 ...
:1950 MAXSPD-BREAK veh=51 ...
:1951 CPL-S0-CHK veh=51 ...
:1952 COUPLE-FAIL loco=51 order=16 tx=249 ty=234 x=3984 y=3752
```

两车序列完全同构：`TRP ok=0 res=0`（路线预订失败、无预留）→ `RESV-NOBOOK ... reason=parked-no-pair` → `lookahead` 为空 → `adv=30`。

**门的来历（代码自证）**：`src\train_cmd.cpp` 的 `parked-no-pair` 分支注释写明它是 **2026-10-26 新增**、依据玩家硬要求「没有配对锁就禁止滑行」：*"a GOTO_COUPLE chain must stay put from the very first tick ... The route is refused, `GetCurrentMaxSpeed()` drops to 0 and the chain brakes by itself."*（见 `TryPathReserveWithResultFlags()` 上方注释块，:17691-17725 一带；门本体在 `:17495` 附近）。**结论：30 不是 bug，是该门按设计生效后落到的 realistic-braking 无前视兜底值。**

**完整因果链（已实证）**：

1. 机车执行 `GOTO_COUPLE`，目的地 `(278,240)`，自己停在 `(249,233)` / `(249,234)`；
2. 此刻**没有配对锁** ⇒ `parked-no-pair` 门命中 ⇒ `TryPathReserveWithResultFlags()` 直接返回 `TPRRF_NONE`，整条路线不订（`TRP ... ok=0 res=0`，`RESV-NOBOOK reason=parked-no-pair`）；
3. 没有路线 ⇒ `lookahead == nullptr`；
4. `GetCurrentMaxSpeedInfoInternal()` 落到 `src\train_cmd.cpp:1643` 的无前视兜底 ⇒ `advisory_max_speed = 30`；
5. 链条自行刹到 `cur_speed = 0`（两车 `spd=0`），从头到尾没有移动过（`mstuck=1`、`fto=1`、`COUPLE-FAIL` 每 tick 重打）。

**因此「428 → 30」已完全解释；「30 → 5」仍缺证据。** 新增待查项：

- Q-3（下一层根因）：为什么 `parked-no-pair` 长期成立——即 `R3REnsureCouplePair()` 为什么扫不到候选（`_r3r_pair_verdict` 是 `has_candidate=false` 还是根本没有条目）。需读 `CPL-PAIRSCAN` / `R3RDbgPairInvariantViolation` 的 `cand=` / `verdict=` 字段。
- Q-4：5 仍未见读数。建议把 `MAXSPD-BREAK` 从 512-tick 节流改为「`advisory_max_speed` 变化时」边沿触发，以抓 30→5 的跃变；否则 5 可能永远落在两次采样之间。

## 十二、配对扫描读数：两台车是两种命运（2026-10-26）

**`CPL-PAIRSCAN` 关键行（逐字）**：

```
:2023 CPL-PAIRSCAN tick=2958 act=99 state=steady ord=16 actTile=258,233 actSpd=30 pool=0 ... best=75 bestDist=8 paired=-1
:2033 CPL-PAIRUNLOCK tick=2959 why=coupled act=99 actTile=259,233 actSpd=30 actOrd=16 actPairT=75 ... tgt=75 tgtTile=265,234 tgtSpd=0 tgtOrd=17
:2369 CPL-PAIRSCAN tick=2964 act=51 state=none ord=16 actTile=249,234 actSpd=0 pool=4 s_chain=139 s_target=4 ... best=-1 near=99 nearDist=14 nearWhy=not-target nearTile=263,234 nearOrd=1
```

| 车 | 结局 | 判读 |
|---|---|---|
| veh=99 | **配对成功**（`PAIRUNLOCK why=coupled`，目标 veh=75 `tgtOrd=17`），在 `258,233` 时 `actSpd=30` | 它以 30 的速度蹭到 `259,233` 完成挂车 ⇒ 玩家看到的「30」就是这个 |
| veh=51 | **配对失败**：`state=steady` 扫完 `pool=4` 仍 `best=-1`；最近车 `near=99` 但 `nearOrd=1`（99 已挂上 75、订单变成 GOTO_STATION）⇒ `nearWhy=not-target` | 它想挂的目标已经不存在，`s_chain=139` 说明 139 次因"不是本链"被拒，`s_target=4` 次因目标资格被拒；永久 `spd=0` |

**由此得到的第二层结论**：`parked-no-pair` 门的判据完全正确——51 确实没有候选，停住是对的；99 有候选，但它**也是被限速 30 蹭过去的**（`actSpd=30` 贯穿到耦合完成）。所以：

- 「30」= 门的副作用，**不是 bug 而是设计代价**：门拒绝订路后没有任何前视信息，realistic braking 只能给 30 的兜底建议速度，于是最后一小段（`bestDist=8`，约 8 格）是 30 蹭完的，不是正常加减速曲线。
- 「5」仍无读数。最可能来自 30 与 0 之间的减速中间态（`CPL-S0-CHK` 的 `prog` 与 `_r3r_pair_verdict` 抖动会造出注释里写的「0/27/30 creeping start」），但**没有证据，不下结论**。

**给玩家的可选修法（均未落地，等拍板）**：

| 方案 | 做法 | 代价/风险 |
|---|---|---|
| A（最小） | `parked-no-pair` 门只在「离配对候选 > N 格」时拒绝订路；已有候选（`best>=0`）时放行到能看见目标为止 | 要在门里读 `_r3r_pair_verdict` 的 `best` 与距离，门从"纯布尔"变成"带几何"，且可能与第 127 轮 `RESV-NOBOOK` 的"禁止滑行"硬要求冲突 |
| B（治观感） | 保留门，但在 `look == nullptr` 且 `OT_GOTO_COUPLE` 时把兜底从 30 改成 15（或按剩余距离线性），让最后一小段更顺 | 只改一个常量，风险最低；但仍不能解决"目标消失后永久停住" |
| C（治根） | 让 51 这类"候选已消失"的链在 `R3R_COUPLE_DEST_IDLE_LIMIT` 后按 KI-193 走 `COUPLE-DEST-EMPTY` 推进订单，而不是无限等 | 已有机制，需要确认 51 为何没触发（`real=8` 是 GOTO_COUPLE、`mstuck=1`） |

**未确认**：U-1 51 的候选目标原本是谁（`s_target=4` 拒了谁、那 4 个是谁）；U-2 99 从 428 降到 30 的完整曲线（需边沿触发）；U-3 `s_chain=139` 的 139 是否包含 51 自己的链内车。

## 十三、更正：99 的 30 有两个来源，前后不同（2026-10-26，玩家追问）

玩家追问「99 配对成功了为什么还要以 30 蹭」，逼出上一轮简化说法的一处错误：30 并非全程都来自 `:1643` 的无前视兜底。

**关键取证（同一份日志）**：

```
:846  TRP veh=99 mf=254,233 real=8 type=16 dest=491798 spd=0 ... look=0 ... => ok=0 res=0
:849  MAXSPD-BREAK veh=99 tile=249,233 spd=0 strict=428 adv=30 look=0 ...
:1412 TRP veh=99 mf=254,233 real=8 type=16 dest=491798 spd=0 ... look=1 ... => ok=1 res=1
:1413 CPL-PAIRLOCK tick=2928 why=scan act=99 actTile=249,233 actSpd=0 actOrd=16 actPairT=75 tgt=75 tgtTile=265,234 tgtOrd=17 tgtPairR=99
:2023 CPL-PAIRSCAN tick=2958 act=99 state=steady ... actTile=258,233 actSpd=30 ... best=75 bestDist=8
:2033 CPL-PAIRUNLOCK tick=2959 why=coupled act=99 actTile=259,233 actSpd=30 ... tgt=75 tgtTile=265,234
:2331 TRP veh=99 mf=270,234 real=12 type=1 ... look=1 ... => ok=1 res=1
```

**阶段一（tick < 2928，`:846`/`:849`）**：无配对锁 ⇒ `parked-no-pair` 门命中 ⇒ `ok=0 res=0 look=0` ⇒ `:1643` 兜底 `advisory_max_speed=30`。这一段与 51 完全同构（`:1950`），差别只是 51 永远等不到锁。

**阶段二（tick = 2928 起，`:1412`/`:1413`）**：锁建立，**同一 tick** 门放行且配对代码主动替它订路成功 —— `TRP ... look=1 => ok=1 res=1` 是直接证据。此后 `lookahead != nullptr`，`GetCurrentMaxSpeedInfoInternal()` 走的是另一条分支：

```1618:1632:src/train_cmd.cpp
	if (this->UsingRealisticBraking()) {
		if (this->lookahead != nullptr) {
			...
			TrainDecelerationStats stats(this, this->lookahead->cached_zpos);
			if (this->lookahead->flags.Test(TrainReservationLookAheadFlag::DepotEnd)) {
				...
			} else {
				LimitSpeedFromLookAhead(max_speed, stats, this->lookahead->current_position, this->lookahead->reservation_end_position,
						0, this->lookahead->reservation_end_z - stats.z_pos);
			}
```

`end_speed = 0` ⇒ 它必须以 0 速停在**路线预订终点**（挂接点前），于是 `GetRealisticBrakingSpeedForDistance()`（`src\train_cmd.cpp:1287`，v² = u² + 2as）按剩余距离反算上限：距离还有 8~10 格时算出来正好 30，再近就 5，贴到终点就是 `REALISTIC_BRAKING_MIN_SPEED`。

**因此 30 的第二种来源是「必须在预订终点停住」的刹车曲线，不是限速，也不是兜底。** 换句话说：它不是被限速 30 慢慢巡航，而是「要在 8 格内停稳，此刻最快只能 30」；`CPL-PAIRSCAN actSpd=30`（`:2023`）与解锁瞬间 `actSpd=30`（`:2033`）都落在这条曲线的中段。

**「5」由此获得解释（但仍缺直接读数）**：同一条 `v²=2as` 曲线在更近距离处的值。`MAXSPD-BREAK` 是 512 tick 节流，30→5 的跃变几乎必然落在两次采样之间，所以两行都只抓到 30。要实证需改边沿触发。

**修正上一轮结论**：§十~§十二把 30 一律归给 `:1643` 兜底是不完整的 —— 那只对 `look=0` 的第一阶段成立；配对成功后是 `LimitSpeedFromLookAhead` 的距离约束。

**由此新增的修法候选**：

| 方案 | 做法 | 说明 |
|---|---|---|
| D | 让 `GOTO_COUPLE` 的预订终点带非零 `end_speed`（挂接语义本就以撞上告终），刹车曲线不再把接近段压到 30 | 落点在 `ApplyLookAheadItem` 的 `TRLIT_STATION`/耦合 item 分支；风险=接近速度偏高，需与 `CheckTrainCollision` 容差一起核 |
| E | 配对成功后预订一条更长的路线（越过目标车若干格），让它先加速、再按曲线减速 | 只改配对处的订路请求；风险=白占一段预留 |

**未确认**：U-4 30 与 8 格距离的定量对应（需打印 `deceleration_x2`、`reservation_end_position - current_position` 与 `LimitSpeedFromLookAhead` 返回值）；U-5 阶段二是否也存在 `ApplyLookAheadItem` 里更低的 item 限速（如等待区路点停车位）叠加。

## 十四、第 323 轮：CPLSPD 速度-距离曲线探针（玩家要求落地）

**玩家原话**：「你加一个探针把列车配对成功到耦合成功期间的速度变化打印下来吧，这样你就知道 30 和 5 哪来的了」。

### 14.1 为什么必须换触发方式

第 322 轮的 `MAXSPD-BREAK` 每车 512 tick 一行（`R3RDbgMaxSpdHold()`，:1508）。30→5→0 的整个接近段通常只有几十 tick，**结构上必然**落在两次采样的缝隙里 —— 这就是「两行读数都是 30、5 一次没抓到」的唯一原因，不是 5 不存在。所以新探针改成**状态触发**：只在 `(cur_speed, gap)` 这一对读数发生变化时写一行。匀速段不产生行，减速曲线的每个台阶都留下痕迹。

### 14.2 区间判据（回答「配对成功 → 耦合成功」）

两条同时成立才记：
- `current_order` 是 `OT_GOTO_COUPLE`（区间入口：进入"前往挂接"）；
- `lookahead != nullptr`（= 已通过 `parked-no-pair` 门、**真的订到了路线**）。

`look=0` 的那一段属于 `:1643` 的兜底 30，由既有 `MAXSPD-BREAK look=0` 认领（`:849`），新探针不掺和 —— 这样两段 30 的来源在日志里天然分开。

### 14.3 字段口径（用于把 30/5 归因到具体那一层）

| 字段 | 来源 | 用途 |
|---|---|---|
| `strict` | `max_speed`（刚出限速/曲线/开关那层） | 排除 428 之外的全局限制 |
| `aend` | 叠加 `:1629` 的「到预订终点必须停住」后的 `max_speed` | **主嫌疑**：`v² = decel2 * gap` |
| `aitem` | 再叠加 `ApplyLookAheadItem`（站台/路点停车位、信号）后的 `max_speed` | 区分 U-5 的 item 约束 |
| `adv` | `advisory_max_speed`（最终建议上限） | 与 TRP `adv=` 对照 |
| `gap` | `reservation_end_position - current_position` | 刹车曲线里的 `s` |
| `decel2` | `stats.deceleration_x2` | 可以现场手算 `sqrt(decel2*gap)` 与 `aend` 对账 |
| `nocab/revd/revspd/spr/sig/ordmax` | 与 `MAXSPD-BREAK` 同源 | 排除「另一个 30」（`revd=13 ⇒ 30` 那条） |

### 14.4 判读口径（写死，避免事后又猜）

1. `aend == IntSqrt(decel2*gap)`（±1 取整）⇒ **30/5 全部是刹车曲线**，U-4 成立，§十三的结论收官。
2. `aend` 大而 `aitem` 小 ⇒ 命中 U-5，是 item 层（等待区路点停车位等）在压。
3. `revd` 随 5/30 同步变化 ⇒ 命中 `ReversingDistanceTargetSpeed`（`:1615`），与刹车曲线无关，§十三第 2 处结论要改写。
4. 出现 `spd` 与 `gap` 同时不变却换挡的行 ⇒ 状态触发漏采，探针有 bug，先修探针再谈结论。

### 14.5 落点与实现

- 文件级：`R3R_COUPLE_SPD_MAX_LINES`（每车每 episode 硬上限，防病态刷屏）、`_r3r_couple_spd_last`（veh → {spd,gap}）、`_r3r_couple_spd_lines`。
- 探针函数 `R3RDbgCoupleSpdCurve()`，定义在 `R3RDbgMaxSpdHold()` 之后、`GetCurrentMaxSpeedInfoInternal()` 之前。
- 取数点：在 `GetCurrentMaxSpeedInfoInternal()` 内，`stats` 构造后记 `decel2`；`:1632`（终点约束）后记 `aend`；`ApplyAdvisory` 之后记 `aitem`；在 `UsingRealisticBraking()` 块结束后调用探针。
- 清理：`_r3r_pair_verdict.erase()` 那处（约 `:13850`，订单已不是 `GOTO_COUPLE`）一并 erase 三个表，避免槽位复用后跨车读数。
- 只读：不写任何车辆/订单/预留状态，不影响行为。

**未确认（待复测后回填）**：U-6 实测第一行 `aend` 是否等于 `sqrt(decel2*gap)`；U-7 耦合瞬间是否确有 `gap→0` 的末行；U-8 是否存在 `revd` 与速度同步的样本。

### 14.6 构建自证（第 323 轮，2026-10-26）

| 项 | 实测 |
|---|---|
| 构建入口 | `_tmp_inc_build.cmd`（复用，未新建 .cmd） |
| 构建前检查 | `Get-Process openttd` 无输出（未运行） |
| 护栏 | `GUARD: incremental is safe (no header/lang file is newer than the newest object)` |
| 编译步 | `[2/3] Building CXX object ...src\train_cmd.cpp.obj` → `[3/3] Linking CXX executable openttd.exe` |
| 退出码 | `build\R3R_incbuild.done` = `EXIT_CODE=0`（18:09:37） |
| 时间戳 | `src\train_cmd.cpp` 18:04:58 → `train_cmd.cpp.obj` 18:05:55 → `build\openttd.exe` 18:09:04（52 320 768 B） |
| 静态检查 | `read_lints` 0 条 |
| 字面量 | exe 内 `CPLSPD veh=` 命中（ASCII 字节搜索返回 `True`） |

状态＝**已实现 + 已编译**，游戏内复跑取数待玩家执行。KI 条目 = `R3R_KNOWN_ISSUES.md` 末尾「第 323 轮」的 **KI-501**。

### 14.7 请玩家复跑时注意的取样点

1. 用「前往挂接」让前车去挂一列车底，日志里应出现连续多行 `CPLSPD veh=<前车>`（不是只有两行）。
2. 判据 2：`look=0` 的阶段**不应该**有 `CPLSPD`（那时 `lookahead` 为空，探针直接返回）。
3. 判据 3：末尾应出现 `gap` 很小的行，对比耦合瞬间。
4. 若日志里一行 `CPLSPD` 都没有，先确认 `R3R_PROBES` 没被关（探针外层有 `R3RDbgOn()` 门）。

## 十五、第 323 轮实测回填：30 和 5 的来源已经查清（2026-10-26）

**输入**：玩家回传 `build\R3R_debug.log`，`CPLSPD` 共 **74 行**，全部 `veh=99`，覆盖 `tick=2928~2938`（11 tick），`decel2=216` 全程恒定，`dpe=0`、`realbrk=1`、`db=1`、`revd=0`（⇒ 与 `ReversingDistanceTargetSpeed` 无关，候选 B 彻底出局）。

### 15.1 实测台阶（原文读数）

| 行号 | tick | tile | spd | aend | adv | gap(px) | n |
|---|---|---|---|---|---|---|---|
| — | 2928 | 249,233 | 30 | 51 | 51 | 12 | 27 |
| — | 2929 | 249,233 | 40→42 | 42 | 42 | 8 | 38~40 |
| — | 2929 | 249,233 | 33 → **29** | 29 | 29 | **4** | 47~48 |
| 1395 | 2938 | 250,233 | **5** | 5 | 5 | **-8** | 74 |

首尾：`n=1` 时 `gap=16 → aend=59`；`n=74` 时 `gap=-8 → aend=5`。

### 15.2 对账：`aend` 就是 `IntSqrt(decel2 * gap)`，逐点吻合

`decel2 = 216`（= 2a，`stats.deceleration_x2`）。`GetRealisticBrakingSpeedForDistance()`（`src\train_cmd.cpp:1292-1296`）算 `speed_sqr = 0² + 216 * distance`，再 `IntSqrt`：

| gap | 216×gap | √ | 实测 aend |
|---|---|---|---|
| 16 | 3456 | 58.8 | **59** ✅ |
| 12 | 2592 | 50.9 | **51** ✅ |
| 8 | 1728 | 41.6 | **42** ✅ |
| 4 | 864 | 29.4 | **29** ✅ |
| 2 | 432 | 20.8 | **21** ✅ |
| 1 | 216 | 14.7 | **15** ✅ |
| 0 及负 | ≤0 | — | **5**（地板）|

**⇒ U-4 成立：30 是这条刹车曲线在 gap≈4px（约 1/4 格）处的取值，不是限速、不是兜底。** 玩家观感里的「30」= 列车还剩四分之一格时被允许的最高速。

### 15.3 「5」不是曲线点，是硬地板

`gap=0` 与 `gap<0` 一律 `aend=5`，来自：

```80:82:src/train_cmd.cpp
enum {
	REALISTIC_BRAKING_MIN_SPEED = 5,
};
```
```1360:1361:src/train_cmd.cpp
		const int floor_speed = (end_speed == 0) ? REALISTIC_BRAKING_MIN_SPEED : 15;
		max_speed = std::min(max_speed, std::max(floor_speed, end_speed));
```

耦合接近走的是 `LimitSpeedFromLookAhead(..., end_speed = 0, ...)`（`:1629`）⇒ `floor_speed = 5`。**所以列车永远压不到 0，而是以 5 一路蹭**：实测 `gap` 从 0 变负一直走到 **-8px**（半格），`spd` 恒 5，直到 `COUPLE-OK`（`:1681`，`loco=99 rear=98 consist=75 real=12`，tick≈2938~2959）。这正是玩家说的「以 30 蹭过去」的后半段成因。

### 15.4 探针自身的一处口径缺陷（不影响结论）

调用点传的 `strict` 是 `UsingRealisticBraking()` 块**结束后**的 `max_speed`，所以 `strict` 恒等于 `aitem`（74 行全部 `strict == aend == aitem == adv`）。这丢了「原始硬上限（428）」那一层。好消息是 `aend == aitem` 每行都成立 ⇒ **`ApplyLookAheadItem` 的 item 层在本场景没有再加任何约束**，U-5（等待区路点停车位叠加）**否定**。

### 15.5 结论（可直接用于决策）

1. **30 与 5 是同一套机制的两个位置**：30 = `v²=2as` 曲线在 gap≈4px 的值；5 = 曲线撞上 `REALISTIC_BRAKING_MIN_SPEED` 地板。
2. 由此 **方案 D（给耦合终点非零 `end_speed`）与方案 E（订更长路线）都不对症** —— 真正让车「蹭」的不是 30，是 **5 这个地板让它永远停不住**。若要让接近更利落，正确落点是耦合场景单独放宽地板（例如 `end_speed==0 && 目标是挂接` 时不把下限抬到 5，或允许 0 后由碰撞判据完成命中），而不是改曲线参数。
3. **U-6 ✅（`aend = sqrt(decel2*gap)`）**、**U-7 ✅（couple 前有 `gap→负` 的蠕行末段）**、**U-8 ❌（`revd` 全程 0）**、**U-5 ❌（item 层未参与）**。

## 十六、更正（第 323 轮复跑）：§15.5 第 1 条作废 —— 30 不是曲线点，它和 5 都是「恒定值平台」

**玩家原话**：「不对，你查看一下最后两次速度变化，它的速度确实是保持在 30 和 5 一段时间，不是刹车曲线的结果」。

**判定：玩家成立，§15.5 第 1 条（"30 = 曲线在 gap≈4px 的值"）作废。** 新日志（`build\R3R_debug.log`，314 650 B / 18:23:31）`CPLSPD` 共 **148 行 = 74（`veh=99`, tick 2928~2938）+ 74（`veh=51`, tick 2988~2998）**，两段同构。

### 16.1 复跑原始读数（`veh=51`，tick 2988~2998）

| tick | gap(px) | 限速 `aend` | 实测 `spd` 序列 |
|---|---|---|---|
| 2988 | 16 | 59 | 0,8,9,…,16（同一 tick 内多个子步，纯加速）|
| 2988 | 12 | 51 | **30** |
| — | 8 | 42 | 40,41,42 |
| — | 4 | 29 | 33 → 29 |
| — | 1 | 15 | 15 |
| 2990 | 0 | **5** | 15,13,11,9,8,7,6,**5** |
| 2991~2998 | **−1…−8** | **5** | **恒 5（9 个 tick 一行不变）** |

`veh=99` 同形：尾部 tick 2933~2938 恒 5、gap −3…−8（6 tick）。

### 16.2 两条更正

**(1) 30 与刹车曲线无关，是「无预留时的 advisory 钳位」，且被 `GetCurrentMaxSpeed()` 变成硬上限。**

```1722:1724:src/train_cmd.cpp
		} else {
			advisory_max_speed = std::min(advisory_max_speed, 30);
		}
```
```1786:1790:src/train_cmd.cpp
int Train::GetCurrentMaxSpeed() const
{
	MaxSpeedInfo info = this->GetCurrentMaxSpeedInfo();
	return std::min(info.strict_max_speed, info.advisory_max_speed);
}
```

即 `lookahead == nullptr`（没有预留）时，advisory 被夹到 30，而 `GetCurrentMaxSpeed()` 取 `min(strict, advisory)` ⇒ **实际硬上限就是 30**，与 428、与 `sqrt(216*gap)` 都无关。

日志实证（三列车同形）：

```
MAXSPD-BREAK veh=99 tile=249,233 spd=0 strict=428 adv=30 look=0 ...
MAXSPD-BREAK veh=51 tile=249,234 spd=0 strict=428 adv=30 look=0 ...
MAXSPD-BREAK veh=21 tile=249,233 spd=0 strict=428 adv=30 look=0 ...
```

**关键：这一段被 `CPLSPD` 的区间判据刻意排除了**（探针要求 `lookahead != nullptr`），所以它在日志里完全不可见 —— 这正是我上一轮把「玩家看到的 30 平台」错认成「曲线在 gap=4 的 29」的原因。全日志 `spd=30` 只有 **2 行**（每车一行，且都在 gap=12、限速 51 的加速途中），不可能是「保持一段时间」的那个 30。

**(2) 5 是曲线撞地板之后的「恒定值」，不是曲线的尾巴。**

`gap ≤ 0` 时 `LimitSpeedFromLookAhead` 走 `REALISTIC_BRAKING_MIN_SPEED`（`src\train_cmd.cpp:81` = 5），曲线本该继续降到 0 却被拦在 5 ⇒ 观测到的是**平台**（9 tick 一行不变），不是「还在减速」。玩家的措辞准确。

### 16.3 修正后的三段模型

| 观感 | 真实来源 | 持续 | 日志可见性 |
|---|---|---|---|
| 「一直以 30 开过来」 | `advisory_max_speed = min(advisory, 30)`（`:1723`，无预留）；`GetCurrentMaxSpeed()` 取 min ⇒ 硬上限 30 | 整个 **look=0 阶段**（配对成功之前） | 只有 `MAXSPD-BREAK look=0 adv=30`，`CPLSPD` 看不到 |
| 「临近时忽然掉速」 | 刹车曲线 `IntSqrt(216×gap)`：59→51→42→29→21→15 | 最后 **1 格（16px）、约 2 tick** | `CPLSPD` 中段那几行 |
| 「以 5 慢慢贴上去」 | `REALISTIC_BRAKING_MIN_SPEED = 5` 地板（`:81`/`:1360-1361`） | **8~9 tick**（gap 0 → −8）| `CPLSPD` 尾部恒 5 |

⇒ 玩家肉眼看到的两个平台（30、5）都是**常量**；中间那段真正的曲线只占 1 格、2 tick，几乎看不见。**"不是刹车曲线的结果"这个判断对 30 完全成立，对 5 也成立（5 是地板截断后的常量）。**

### 16.4 对修法的连带更正

- **30 段不需要改**：它是上游「无预留限 30」行为，且 R3R 侧已有"配对成功即主动订路"的一脚（`:14161-14174`）。要缩短 30 段只能让 lookahead 更早建立，不是改常数。
- **5 段仍是真病灶**：停不住 ⇒ 只能蹭 8px 靠碰撞命中（KI-502 的 F1/F2 依然指向这里）。
- 方案 D（终点带非零 `end_speed`）/ E（订更长路线）**继续作废**。
- **探针需补一条**：`lookahead == nullptr` 时另打 `CPLSPD-NOLOOK`（每车每 tick 一行，只记 `spd/strict/adv/gap=-1`），否则「30 平台」永远拿不到逐 tick 时长。未实现，待拍板。

**U 项收口**：U-4 **修正为「30 = advisory 钳位，不是曲线点」**；U-6 ✅（曲线本身对得上，但只覆盖 2 tick）；U-7 ✅（5 平台 + gap→负）；U-5/U-8 仍为 ❌。

## 十七、第二次更正（玩家 2026-10-26 第二口径）：那个 5 在 30 平台「刚好前面」，不是尾部地板

**玩家原话**：「我说的5也不是这个地方，5是处在你说的"30平台"的刚好前面，（30你说对了）」。

**含义**：§16 认的 5（`REALISTIC_BRAKING_MIN_SPEED` 地板，尾部 gap 0→−8 的蠕行）**不是玩家指的那个 5**。玩家看到的是**时间上紧接 30 平台之前**的另一段 5 ⇒ 观测序列是：… → **5 平台** → **30 平台** → （最后 1 格、约 2 tick 的曲线）→ 尾部 5 蠕行。

⇒ **30 的结论保留**（§16.2(1)：`advisory_max_speed = std::min(advisory_max_speed, 30)`，`src\train_cmd.cpp:1723`；`GetCurrentMaxSpeed()` 取 `min(strict, advisory)`，`:1786-1790`）。
⇒ **5 需要重新定位**：它不是曲线的地板截断，而是「30 钳位生效之前 `max_speed`/advisory 已被别的开关压到 5」，且该段 `lookahead` 状态未知。

### 17.1 待验候选（猜测根因，尚未实证）

| 编号 | 猜测 | 支撑锚点 | 待验判据 |
|---|---|---|---|
| G-1 | `lookahead != nullptr` 但 `max_speed` 已被压到 5（站台停车 item / 信号）⇒ 进入 :1723 分支时 `min(5,30)=5` | `:1695-1724`、`:1389` `TRLIT_STATION` | 同段 `CPLSPD` 的 `aend/aitem/adv/gap` |
| G-2 | `signal_speed_restriction` 或 `speed_restriction` == 5 | `:1685-1690` | `MAXSPD-BREAK` 的 `spr=/sig=` |
| G-3 | 订单限速 `current_order.GetMaxSpeed()` 压低 | `:1681` | `MAXSPD-BREAK` 的 `ordmax=` |
| G-4 | AM_REALISTIC 加速段 `min_speed=2` 起步 + 低加速度，视觉上「停在 5」 | `:1732` 注释、`ground_vehicle.cpp:525-532` | 需逐 tick `cur_speed`（`CPLSPD-NOLOOK`） |

**取证入口**：`MAXSPD-BREAK` 每车 512 tick 一行，含 `spd(=cur_speed)/strict/adv/look/spr/sig/ordmax/maxTrack/maxCurve`，是唯一在 `look=0` 段还能出数的探针；`CPLSPD` 对 `look=0` 段盲。

### 17.2 实测（`build\R3R_debug.log`，`veh=51` 尾部，日志行序）

| 阶段 | 实测读数 | 机制 |
|---|---|---|
| 停车 | `MAXSPD-BREAK veh=51 tile=249,234 spd=0 strict=428 adv=30 look=0` | `look=0` ⇒ adv=30，但此刻车停着 |
| 起步+曲线 | `CPLSPD tick=2988` gap 16 限速 59，spd 0→8→…→16；gap 12 限速 51（此处 spd=30 加速掠过）；gap 8 限速 42，spd 40→42 | 曲线正常 |
| 曲线减速 | gap 4 限速 29（spd 33→29）；gap 1 限速 15；`tick=2990` gap 0 限速 5，spd 15→13→…→5 | 曲线 + 地板 |
| **5 平台（8 tick）** | `tick 2991~2998`：`spd=5 strict=5 aend=5 aitem=5 adv=5 gap=-1…-8`，位置停在 `250,234` | `LimitSpeedFromLookAhead` 命中 `position <= current_position` ⇒ `floor_speed = 5`（`:1352-1361`） |
| **30 平台（9 格）** | `CPLSPD` 到 `n=74`（tick 2998）后**再无输出** ⇒ `lookahead == nullptr`；`LOCO-MOVE veh=51 tile=251,234 … spd=30` 一路到 `256,234`、`258,233`、`259,233` | `advisory_max_speed = min(adv, 30)`（`:1723`） |
| 挂接 | `GEO settle-couple veh=51 tile=259,233 x=4144 y=3736 segid=3 clen=2 dist=5 nom=5` → `SPD-DROP prev=30` → `REVERSEDIR spd=0` | 提交点 |

⇒ 真实顺序是 **曲线 → 5 平台（8 tick）→ 30 平台（9 格）→ 挂接**，与玩家「5 在 30 平台刚好前面」逐字吻合。

**三条更正**：

1. 5 **不是**曲线的尾巴，**也不是**「蹭 8px 靠碰撞命中」（§15.5 / §16.4 的口径作废）。它是 `gap < 0` 之后**仍被地板允许继续前进**的 8 tick。
2. 30 平台**不在** 5 之前，而在 5 **之后**：5 段结束时 `lookahead` 被清（同一 tick 末行 `n=74`），立刻切成无预留 30，车再加到 30 跑 9 格才真正挂上。
3. 「停不下来」的真实观感 = **减速到 5 磨 8 tick → 突然 30 冲出去 → 9 格外挂上**，不是「慢慢贴上去」。

### 17.3 KI-502 定性更正

真问题不是「末端停不住」单点，而是「**越过预留终点后仍有 8 tick 的 5 蠕行，随后掉进 `look=0` 的 30 平台**」——两个常量平台首尾相接，肉眼就是「卡一下然后猛冲」。修法候选相应调整：

- **F1**（原「命中点前移半格」）改述为「缩短 `gap<0` 的蠕行距离」，与 30 平台无关。
- **F2**（允许地板 0）风险上升：`gap<0` 时停死、且 `lookahead` 仍为空 ⇒ 可能永远停在原地（第 172 轮「拒绝挂车≠让机车停车」教训）。
- **F3** 维持现状仅文档说明。
- **F4（新增，推荐）**：`gap <= 0` 且目标为挂接时**立刻撤销预留并触发重订**（不等越界 8px），让 5→30 的切换落在更靠近目标的位置，消除「磨一段再猛冲一段」。

## 十八、第三次口述（玩家 2026-10-26 第三口径）：给了现场几何，5 平台 = 出站信号机那一格

### 18.1 玩家原话要点（逐字保留关键句）

> （254，233）是等待车站的最西端，（256，233）是等待车站的出站 PBS 信号机所在格，可以向西发出预留。主动挂接的列车到了（264，234）的最西端，就可以成功与被挂列车成功耦合。现在，主动方列车前进鼻端在（255，233）这一格内部移动，呈现的正常的加减速曲线。鼻端进入（256，233），速度以加减速曲线变为平台 5。鼻端进入（257，233）直到耦合成功，速度变为平台 30。

并明确要求：**先就场景达成共识，再动手修**。

### 18.2 AI 复述的场景（待玩家确认）

主动方从西向东行驶，鼻端 x 递增：`… → 255,233（正常曲线）→ 256,233（平台 5，持续约一格 = 8 tick）→ 257,233 … → 264,234（命中耦合）`。

三重机制对应：

| 阶段 | 现场 | 代码 |
|---|---|---|
| 正常曲线 | 鼻端在 `255,233` 格内 | `lookahead` 有效，`IntSqrt(end²+216×gap)` 逐 tick 收紧 |
| **平台 5** | 鼻端进入 `256,233`（出站 PBS 信号机格 = **本段预留/lookahead 的终点格**） | `position <= current_position` ⇒ `floor_speed = (end_speed==0)?5:15`（`train_cmd.cpp:1352-1361`），曲线被地板顶住 |
| **平台 30** | 鼻端进入 `257,233` 起，直到 `264,234` 挂上 | `lookahead == nullptr` ⇒ `advisory_max_speed = min(adv,30)`（`:1722-1724`） |

⇒ 与 §17.2 的日志时间线（`veh=51`：`CPLSPD spd=5 gap=-1…-8` 8 tick → `LOCO-MOVE spd=30` 9 格 → `GEO settle-couple`）**结构完全同构**，只是坐标带不同（见 18.4 疑点）。

### 18.3 由场景得出的定性（本轮关键收获）

1. **5 不是「离目标近所以慢」**，而是「**到达预留终点、且 `end_speed==0` 但不许停**」的地板值。它的持续长度 = 鼻端穿过那**一格**的时间，与到挂接目标的真实距离无关。
2. **30 不是「算出来的安全速度」**，而是「**没有任何前瞻信息时的默认上限**」。257 之后 `lookahead` 已空，没有任何 `sqrt(216×gap)` 在算，所以是死平台。
3. **真正的目标（264 的等待车）全程没有进入公式**：耦合靠 `TrainCoupleHandler` / `CheckTrainCollision` 的几何命中，不靠预留、也不靠刹车曲线。⇒ 两个平台的接缝恰好落在「预留终点」而非「挂接面」，这是信息缺失，不是调参问题。
4. 因此要修的核心是：**`OT_GOTO_COUPLE` 的刹车终点应当取「等待车的挂接面」，而不是「本段预留的最后一格」**。F1/F4 只是绕开接缝位置，属治标；把挂接面并入 lookahead（暂记 **F5**）才是治本，可与 F4 叠加。

### 18.4 疑点（登记待验）

- **REG-322-A（坐标带不一致）**：本口述为 `255/256/257 → 264,234`，而 §17.2 同一现象的日志为 `250,234`（平台 5）→ `251,234…259,233`（平台 30）。整体相差约 +5/+6 格。可能是：①日志打的是**链头车 tile**、玩家说的是**鼻端所在格**；②不同存档/不同站；③列车长度不同导致鼻端与链头的格差不同。⇒ 需确认是否同一场景，否则禁止把两套坐标混用。
- **REG-322-B（5 平台只有一格？）**：按 `floor_speed` 的语义，只要 `position <= current_position` 就一直给 5 ⇒ 5 的持续范围应等于「鼻端从 lookahead 终点格内穿出」的时长；口述恰为「进入 256 那一格」⇒ 与语义吻合。但若 257 仍在同一 lookahead 的终点范围内（多个 item 同位置），5 会跨多格，需日志核对。
- **REG-322-C（站台跳段可能压缩 5 平台）**：`TrackFollower` 进入站台时 `tiles_skipped` 一步跳到站台末端（见 KI-163 的站台段处理），若 256 是站台内格、257 已在站台外，则 lookahead 在 256 处提前收尾是**跳段**造成而非真实预留终点。⇒ 待验，会影响 F5 的落点。

### 18.5 待玩家确认的问题

- **Q1** 主动方行进方向 = 从西向东（鼻端 x 递增），`(264,234)` 是最终耦合点。确认？
- **Q2** 站台 y=233 覆盖哪些格？`254` 是最西端，`256` 是站台**东端格**（出站信号机所在）吗？`257` 已在站台外吗？
- **Q3** `(256,233)` 是否就是主动方本 tick 的**预留终点**（`lookahead` 终点）——即预留只到这一格、`257` 之后没有预留？日志侧可用 `RP cur=` / `CPL-TRACE-RESULT` 核对。
- **Q4** 被挂列车的挂接锚点在哪：它的链尾/挂接面是 `(264,234)` 的西边缘吗？`264,234` 与站台行 `y=233` 是什么关系（岔道/斜接/邻线）？
- **Q5** 与 §17.2 的 `veh=51` 日志是否同一场景（见 REG-322-A）？

## 十九、第四次口述（玩家 2026-10-26 答 Q1~Q5）：预留止于 255，256 是无预留的信号机格

### 19.1 玩家新事实（逐条）

1. 主动方：**链头朝东、movingfront 朝西、DB=1**（倒车姿态接近），鼻端（movingfront 端）x 递增：`255 → 256 → 257 → … → 264`。
2. 站台 `y=233` **最西仅到 x=254**；`x=255` 是非站台格；`x=256` 是「有单向路径信号灯（one-way PBS signal）的铁路轨道」，东西走向，**灯对东往西的列车禁止驶入**。
3. **`(255,233)` 铺了预留；`(256,233)` 没有预留。** ← 决定性实证
4. 耦合面 = `(264,234)` 的**西侧边缘**，越过即与等挂车重叠。
5. 被挂方**车钩朝东**（挂接面在东端）。
6. `y=231..234` 这一带逻辑相同，y 不影响判读。
7. 与 `veh=51` 属**同一场景的两个相似事件**（不是同一趟）⇒ **REG-322-A 结案**：坐标差来自不同列车/不同股道，不是数据矛盾。

### 19.2 修订后的 5 / 30 区间（共识）

| 阶段 | 鼻端所在 | 该格预留 | 机制 |
|---|---|---|---|
| 正常曲线 | `255` 格内 | **有** | `lookahead` 有效，`√(end²+216×gap)` 逐 tick 收紧 |
| **平台 5（约 1 格 / 8 tick）** | 进入 `256` 格 | **无** | lookahead 末项已被越过/耗尽 ⇒ `gap < 0` ⇒ `floor_speed = (end_speed==0)?5:15` |
| **平台 30（257 → 264 耦合面）** | `257…263`，触及 `264` 西缘 | **无** | `lookahead == nullptr` ⇒ `min(advisory, 30)` |

### 19.3 关键更正（推翻 §17.2 / §18.2 的一半）

- 旧口径「5 平台落在**预留终点格内**（该格仍有预留）」→ 更正为「5 平台落在**预留终点之后的第一个无预留格**（256）」。差一格的语义很重要：地板 5 不是"到达预留终点"的产物，而是"**越过终点、但 lookahead 尚未清空**"的产物；一旦清空（进入 257）立刻跳 30。
- 5 的持续长度 ≈ **一格 = 8 tick**，与到耦合面的真实距离（257→264 约 7~8 格）无关 ⇒ 5 是"**穿格耗时**"，不是刹车。
- 30 平台长度 = 257 到耦合面，**全程无预留、无前瞻** ⇒ 30 是"**零信息默认上限**"，与剩余距离无关。
- **信号机是预留的物理终点**：PBS 单向信号机把 256 划入下一区段，主动方路径预留止于 255 ⇒ 越过信号机后没有任何 lookahead ⇒ 以 30 冲完最后 ~8 格，靠几何命中挂上。

### 19.4 REG-322-B / C 处置

- **B（5 只有一格？）成立**，理由升级为"由 lookahead 清空点决定"，不是"站台跳段"。
- **C（站台跳段压缩 5 平台）证伪**：255 / 256 都不是站台格，站台到 254 为止 ⇒ 本次 5/30 与站台 `tiles_skipped` 无关（若 `veh=51` 当时在站台内，需单独复核，不混用）。

### 19.5 新疑点（登记）

- **REG-322-D**：越过信号机后为何**没有重订**（257~264 零预留、零前瞻）？三种可能：①单向信号机后的区段不由本车预留（信号系统保护）；②重订被拒（YAPF 认为终点=挂接面不可达）；③`OT_GOTO_COUPLE` 的寻路终点本就是目标车所在格，但预留被信号机截断在 255。⇒ 决定 F4 是否可行。
- **REG-322-E**：DB=1 + movingfront 朝西的**倒车接近**下，`GetCouplePosition` 用哪一端算命中（movingfront 端 vs 链头端），与"挂接面在 264"是否同端——同 KI-340 家族（命中端 ≠ 拼接端）。

### 19.6 修法候选（待拍板）

- **F5（新，治本，推荐）**：`OT_GOTO_COUPLE` 且已配对目标时，把「**到挂接面的像素距离**」作为第二刹车终点并入限速（`min(lookahead 曲线, 挂接曲线)`，end_speed=0），不依赖 PBS 重订 ⇒ 256 不再出 5、257~264 不再出 30，全程一条曲线收到 0。
- **F4（治标）**：`gap <= 0` 且目标为挂接时立刻撤销预留并触发重订，让 lookahead 覆盖到 264 —— 依赖 REG-322-D 的重订能成功。
- **F2**：允许地板 0 —— 风险=停死（`lookahead` 仍空，第 172 轮教训）。
- **F1 / F3**：缩短蠕行 / 仅文档。

### 19.7 待确认

- **C-1** `256` 的信号机是否 `SIGNALTYPE_PBS` + 单向（决定"预留必然止于信号机前"是否硬约束）。
- **C-2** 日志侧能否看到 257~264 全程零预留（`RESERVECONSIST ok=` / `RP cur=` 是否只到 255）。
- **C-3** 耦合面精确位置（264 格起点 / 格内某像素）。
- **C-4** 是否批准以 **F5** 为主修法。

## 二十、第五次口述（玩家 2026-10-26 答 C-1）+ 真因定论：预留成功，但 lookahead 从未重建

### 20.1 玩家新事实

1. `(256,233)` 的信号机确认为 **PBS 单向信号机**。
2. **鼻端进入 `(256,233)` 时，PBS 预留成功**：向 `(264,234)` 发出预留，并与等挂车脚下的预留**衔接成功**。
3. 更正：平台 30 区间 = `257` → `264` 的**西缘**（264/265 之间）。此前对"西"的解读有偏差，现统一为"264 格西侧边缘 = 衔接面"。

### 20.2 真因定论（推翻 §19 的"信号机后零预留"）

- 预留**存在**且一路铺到 264 西缘；缺的不是预留，是**速度前瞻 `lookahead` 没跟上**。
- `ChooseTrainTrack()` 的 GOTO_COUPLE 早退分支（`train_cmd.cpp:17571-17581`）：当 `res_dest.okay == false`（`ExtendTrainReservation` 停在等挂车平台）时直接 `return`，**既不置 `CTTRF_RESERVATION_MADE`，也不调 `FillTrainReservationLookAhead`**；对比正常出口 `17273` / `17597` / `17689` 都会重建。
- 外层 `R3RTryPathReserveCore()` 因缺 OK 位在 `18426-18429` 提前返回，同样不补建。
- 结果限速始终用"耦合前的旧 lookahead"：旧末端 = 255 ⇒ 256 格内 `gap<0` ⇒ `floor_speed = REALISTIC_BRAKING_MIN_SPEED = 5`（8 tick 穿一格）；`AdvanceLookAheadPosition`（`1447-1451`）在 `current_position > reservation_end_position + 8` 时 `reset()` ⇒ 257 起 `lookahead == nullptr` ⇒ `1722-1724` 兜底 30，直到 `Couple()` 的 `13517-13520` 才重建。
- 代码自带注释佐证：`12768-12777`（合并后必须丢掉 loco 的 stale lookahead）、`13506-13516`（不重建就会一直卡在 advisory 30 直到下一个信号/道口触发重建）。

### 20.3 修法定论：F6（取代 F5 作主选）

- 落点：`train_cmd.cpp:17577` GOTO_COUPLE 早退分支，返回前补一次重建 —— `lookahead.reset(); FillTrainReservationLookAhead(consist);`，若重建后 `reservation_end_position < 8`（未覆盖哪怕一格 ⇒ 实际无有效预留）则再丢弃，保留原 30 兜底。
- 效果：`√(216×gap)` 从鼻端直达 264 西缘（衔接面），`end_speed = 0` 由 `1707-1710` 施加 ⇒ 5 平台、30 平台同时消失，一条曲线收到挂接面。
- 为何不用 F5：预留末端**就是**挂接面（与等挂车预留衔接），无需另求挂接面距离；F6 复用引擎既有路径，改动面最小。
- F4 / F1 / F2 / F3 降为备选，不叠加。

### 20.4 本轮已落代码

- `src/train_cmd.cpp:17577` 附近，早退分支内新增上述重建 + 丢弃逻辑（仅 `TBM_REALISTIC` 生效，仅 `OT_GOTO_COUPLE` 且非挂接车库）。

### 20.5 待验证

- **V-1** 改后 `256→264` 应为连续递减曲线，不再出现 8 tick 的 5 平台、也不再出现 30 平台。
- **V-2** 若某次挂接前确实没有预留（`ExtendTrainReservation` 完全失败），仍应保持 30 兜底而不蠕行 —— 由 `reservation_end_position < 8` 的丢弃条件保证。
- **V-3** `ChooseTrainTrack` 调用频率下的重建开销：范围仅数格，`FollowReservation` 成本可忽略。

## 二十一、实机验证（2026-10-26）：F6 生效，5/30 平台消失

- 玩家原话：**「那个 30 平台和 5 平台都变成了正常的加减速曲线」**。
- V-1 通过：`256→264` 段由两段常量平台（8 tick 的 5 + 数格的 30）变为连续 `√(216×gap)` 刹车曲线。
- 构建：`build\openttd.exe`（2026-10-26 19:16:28，`r3r-pre-ledger-removal-2026-10-25-m`），经 `vcvars64.bat` 环境增量构建，`train_cmd.cpp` 编译 + 链接通过。
- 收口：KI-502 状态改为**已修复**；F1/F2/F3/F4/F5 作废，最终采用 **F6**。
- 保留待观察（不改代码，仅监测）：
  - **O-1** 无预留的挂接任务是否仍为 30 兜底（`reservation_end_position < 8` 丢弃条件是否生效）。
  - **O-2** `ChooseTrainTrack` 在 GOTO_COUPLE 高频调用时的重建开销（每 tick reset + FollowReservation 数格）——若出现卡顿，可加「仅在 `reservation_end_position` 落后实际预留末端时重建」的门限。
- 教训：本轮两次误判（先是「预留终点格」、再是「信号机后零预留」），根因都是**只看速度现象、未查 lookahead 的构建调用链**。以后遇到「常量平台」类问题，第一步先确认 `lookahead` 是谁建的、什么时候建的。



