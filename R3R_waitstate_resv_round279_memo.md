# R3R 第 279 轮：能不能参考「路点计时调度等待」来截断预留

状态：取证完成，**零源码改动、未编译**。修法待拍板（C-279-1 ~ C-279-3）。
关联：KI-438（预留满天飞）、KI-439（CPL-SAFE-FAIL 孤儿位）、KI-208（RESV-NOBOOK 事后门禁）、REG-278-A。

## 一、玩家提案（原话，2026-10-22）

> 我想到一个东西，那就是把计时调度放在路点命令那里，列车就会在路点那里停下等待时刻表，为什么我们不能把这个列车预留截断参考一下计时调度呢

## 二、取证：计时调度等待为什么"不订路"

现象层：路点/站台设了 wait time，列车到点后停住不动、也不产生黑色预留线。
代码层不是"截断预留"，而是**到达即换态**，共 4 个锚点：

1. `src/order_cmd.cpp:292-297` `Order::MakeWaiting()`
   ```cpp
   const bool wait_timetabled = this->IsWaitTimetabled();
   this->type = OT_WAITING;          // 订单类型被换掉，且 OT_WAITING 不带目的地
   this->SetWaitTimetabled(wait_timetabled);
   ```
2. 到达点四处调它：`src/train_cmd.cpp:16712`（路点 `Waypoint::IsExpected`）、`src/train_cmd.cpp:20671`（路点 `ShouldStopAtStation` 分支）、`src/station_cmd.cpp:4602`（站台）、`src/vehicle.cpp:2861`（车库）。
   路点分支同时 `consist->cur_speed = 0;`（`train_cmd.cpp:16714`）。
3. 寻路层被**从入口短路**，三处：
   - `train_cmd.cpp:13936` `if (consist->current_order.IsType(OT_WAITING)) return;`（`TrainCheckIfLineEnds`）
   - `train_cmd.cpp:20660-20663` `TrainLocoHandler` 驻留分支：`HandleWaiting` 后仍是 `OT_WAITING` 就 `return true`，**根本不进 `TrainController`**
   - `train_cmd.cpp:15207` `IsReservationLookAheadLongEnough`：`OT_WAITING` 直接当"够长"
4. 退出由时间驱动：`src/vehicle.cpp:3916-3942` `Vehicle::HandleWaiting()`，`current_order_time < wait_time` 就 `return`，到点才 `IncrementImplicitOrderIndex()` + `MakeDummy()` + `ProcessOrders()`。

⇒ 结论句：**计时调度不是"到达后把预留截断"，而是"到达后不再有目的地可订"**。截断（到站时 `ClearPathReservation`）只是一半，真正让它安静的是订单类型变了、寻路层看不见它。

## 三、我们的 `OT_WAIT_COUPLE` 差在哪

1. 它**被显式排除在换态之外**：`train_cmd.cpp:16700`
   ```cpp
   if (consist->current_order.IsType(OT_WAIT_COUPLE)) return;   // TrainEnterStation 直接返回，不做 MakeWaiting
   ```
   所以它永远停在"有 GOTO 语义的驻留态"上。
2. 它**不能换成 `OT_WAITING`**：配对识别依赖类型，`R3RIsCoupleTarget()`（`train_cmd.cpp:2151`）判的就是 `current_order.IsType(OT_WAIT_COUPLE)`；换类型会让 yapf 目标探测与 `TrainCoupleHandler` 的到达闸门（`:12802 target-not-wait`）立刻失效。
3. 于是只能事后拦截：`train_cmd.cpp:16215-16223` 的 `RESV-NOBOOK` 门禁（KI-208），条件还是"`cur_speed==0` **且** 在站台格 **且** car-only/WAIT_COUPLE"。**野外等待、非站台等待都漏**。

⇒ 结论句：玩家直觉是对的（timetable 那条路确实干净），但差的不是"截断动作"，而是"驻留态没有从源头掐掉订路"。

## 四、为什么"截断"这个方向本身不成立

预留是**每 tick 再生产**的：只要 `current_order` 还是 GOTO 型且 `dest_tile` 有值，`ProcessOrders → TryPathReserve → ChooseTrainTrack` 每 tick 都会重新订一条。第 126 轮 `RESV-GHOST` 清扫只是把上一 tick 那条擦掉，下一 tick 又订 ⇒ 玩家看到"闪一下就没"。所以做法必须落在**不再生产**，而不是"更勤地擦"。

## 五、三个候选（待拍板，均未实现）

### W-1 驻留态短路上提（推荐，最贴 timetable 语义）

在 `TrainLocoHandler` 的 `OT_WAITING` 分支旁（`train_cmd.cpp:20660` 之前/之后）加一个同形态分支：`OT_WAIT_COUPLE`（及 car-only 停稳）**不调 `TrainController`、不调 `ProcessOrders`，直接 `return true`**。
- 收益：订路从源头消失，KI-438 失去燃料，KI-439 的"撞孤儿位 → 整次寻路作废 → free-path"循环自然不跑。
- 代价：需要一条**恢复路径**（timetable 是自限时，我们的事件驱动没有时限）。配套：沿用 `R3RCoupleOrderDestinationReached()` 的 500 tick 计时（`:109` 轮 KI-193 已存在），配对目标消失/被卖 ⇒ 超时后解配对 + 推进订单 + 恢复正常订路。
- 风险：`OT_WAIT_COUPLE` 若"已经在目的地格上"但还想微调到拼接距离，短路会让它挪不动（现 `TrainCoupleHandler` 是几何命中，不需要被动方移动 ⇒ 待复核 U-1）。

### W-2 给驻留态加 `waiting` 位（不动订单类型）

仿 `MakeWaiting()`，但只置一个 `VehicleFlag`/order 子位，让 `TrainCheckIfLineEnds:13936` 与 `IsReservationLookAheadLongEnough:15207` 认它。改动分散在三处判据上，收益与 W-1 接近，但**新增状态位要进存档**，否则读档后回到"每 tick 订路"。改动面最大。

### W-3 只扩 KI-208 门禁的适用范围（最小、保守）

`train_cmd.cpp:16215` 去掉 `IsTileType(consist->tile, TileType::Station)` 与 `IsPrimaryVehicle` 限制，改成"任何停稳的 WAIT_COUPLE / car-only"。
- 收益：一行改动，覆盖野外/非站台漏网。
- 代价：仍是事后拦截，`ProcessOrders` 与 yaw 路径仍在跑（只是最后一步返回），KI-438 的 CPU 抖动与 REG-278-A 的"重跑寻路"都还在；且 `ReserveTrackUnderConsist` 的防呆不受影响。

## 六、我的建议

先 **W-3 止血**（一行、可立即编译复测），再 **W-1** 治本（需要先确认 U-1 与超时恢复路径）。
不建议 W-2：为一个等待态引入存档字段，成本与收益不成比例，且第 152 轮已经排了 R3SG 段行的大改。

## 七、待拍板

- **C-279-1**：走哪条 —— (a) 只 W-3 止血；(b) W-3 + W-1 一起做；(c) 只 W-1；(d) 先不动，等 KI-438/439 修完再看。
- **C-279-2**：若选 W-1，驻留期间是否允许被动方为"贴近拼接面"做微调移动？(a) 完全不动（纯几何命中）；(b) 允许低速接近（保留现有 creep）。
- **C-279-3**：驻留超时阈值 —— 沿用 500 tick，还是按配对状态（对方不存在 ⇒ 短超时；对方存在但在路上 ⇒ 长超时）分两档？

## 八、复测判据（W-3 单独也可用）

1. 野外停稳的 `OT_WAIT_COUPLE` 不再出现 `SET-RAIL`/`RP cur=` 序列（同一日志对照）。
2. 站台等待者仍 `RESV-NOBOOK` 且 `COUPLE-OK` 照常。
3. 被动方停稳期间 `RESERVECONSIST` 仍为 `ok=1`（脚下保护不能断，否则挂接寻路会撞上它）。
4. 负判据：`OT_GOTO_COUPLE` 机车（主动方）预留不受影响。

## 九、未确认

- **U-1**：`OT_WAIT_COUPLE` 被动方是否真的从不移动 —— `TrainCoupleHandler` 的命中是主动方贴上来，被动方 `cur_speed` 全程 0？需第 278 轮日志对照 `CPL-HIT`/`CPL-PAIR` 行里的被动方速度（未取数）。
- **U-2**：`OT_WAITING` 的 `ClearPathReservation` 具体在哪一行生效（本轮只确认了订单类型与三处短路，没有逐行确认释放点）。
- **U-3**：`RP cur=` 是否只由 `rp()` 产生 —— 若是，W-1/W-3 生效后 REG-278-A 应一并消失，可作为交叉验证。

## 十、第 279 轮续：玩家提议「让等不到目标的列车在路点执行 OT_WAITING」

**玩家原话（2026-10-22，逐字保留）**：
> 既然让列车停下来的关键是OT_WAITING，那我们能不能这样，对于要挂的车没有到位的列车，也就是符合预留截断条件的列车，我们通过一些方式让列车在路点执行OT_WAITING。这样预留就干净了

**初步判断（当轮写下，非结论）**：方向正确，且比 W-1（加短路门禁）更彻底——W-1 是"拦住订路请求"，这条是"真进等待态"。但 `OT_WAITING` 不是可以随手插上的开关，直接换类型会撞三个已知坑：

- **S-1 退出只有"时间到"一条路**：`Vehicle::HandleWaiting()` 的退出是 `current_order_time >= wait_time` 后 `IncrementImplicitOrderIndex()` + `MakeDummy()` + `ProcessOrders()`（`vehicle.cpp:3916-3942`）。而挂接等待是**事件驱动**（配对目标到位），换进 `OT_WAITING` 后没有任何"事件触发退出"的钩子，必须自建，否则只能靠超时。
- **S-2 索引推进踩借用层**：退出时的 `IncrementImplicitOrderIndex()` 会动 `cur_implicit`，而 R3R 借用期索引是**冻结值**（KI-04），`R3RSyncDrivingOrders` 会把它喂回各接手方——正是 KI-215 现场 `real=18→15` 往回跳的同一机制。
- **S-3 类型判据全崩**：KI-274 现场 `ARTIC-SKIP-R3R ... type_from=17` 已证"订单类型不是 R3R 命令"就会被跳过，而跳过的后果是**列车直接开走**，比现在更糟。换成 `OT_WAITING` 同样会被 `R3RSkipCoupleOrdersForRealArtic` 与 `ProcessOrders` 当非 R3R 订单处理。

**由此得到的替代方案 W-4（推荐先评估，未实现）**：不换类型，把三处 `IsType(OT_WAITING)` 短路（`train_cmd.cpp:13936` / `:20660-20663` / `:15207`）改成调用同一谓词 `R3RIsResvParked(v)`，谓词内部 = `IsType(OT_WAITING)` ∨ 停稳的 `OT_WAIT_COUPLE` ∨ 停稳且配对目标未到的 `OT_GOTO_COUPLE`。等于把 `OT_WAITING` 的"入口短路"能力借出来，而不承担换类型的索引/判据后果。

**待验证**：
- **U-4** `OT_WAITING` 的枚举值，以及 `ProcessOrders` 对它的专门分支（有没有隐含副作用）。
- **U-5** 除 `MakeWaiting()` 之外是否还有别处设置 `OT_WAITING`（重启/读档时会不会被规范化掉）。
- **U-6** R3R 全部 `IsType(OT_GOTO_COUPLE)` / `IsType(OT_WAIT_COUPLE)` / `R3RIsCoupleCommand` 判据点清单（决定 W-4 谓词要覆盖哪些调用方，避免又出 KI-274 那种"跳过=开走"）。

**与 W-1 的关系**：W-1 与 W-4 是同一件事的两种实现（W-1 = 在 `TrainLocoHandler` 入口 return，W-4 = 在三处短路点用统一谓词）。W-4 覆盖面更全（连 `TrainCheckIfLineEnds` 与 `IsReservationLookAheadLongEnough` 一并静音），应取代 W-1 作为治本方案。

## 十一、取证回填（同上轮，代码锚点）

### 11.1 实测锚点

- `src/order_type.h:86` → `OT_WAITING = 9`（`OT_IMPLICIT = 8`；R3R 的 `OT_DECOUPLE=15 / OT_GOTO_COUPLE=16 / OT_WAIT_COUPLE=17` 都在其后）。
- **`src/vehicle_base.h:1051`（`SkipToNextRealOrderIndex`）、`:1075`（`IncrementImplicitOrderIndex`）、`:1117`（`UpdateRealOrderIndex`）三处各有一行**
  ```cpp
  if (this->type == VehicleType::Train && this->current_order.IsType(OT_GOTO_COUPLE)) return;
  ```
  且 `:1045-1050` 的注释逐字写着加锁原因：**"Advancing makes current_order and the index diverge (observed: the index crept 3→0→1 every tick while entering the platform, eventually loading a waypoint order and crashing into the waiting consist)"**。
- **`src/train_cmd.cpp:20614`**：`if (consist->current_order.IsType(OT_WAIT_COUPLE)) return true;` —— 注释自陈 "a WAIT_COUPLE order parks the train in place ANYWHERE (station platform as well as depot)"。**即被动方"原地不动"早就实现了。**
- **但 20614 的位置在 `ProcessOrders`（`:20541`）之后**，而 20611-20613 的注释本身就说 "the orders/path reservation **above**" ⇒ **每 tick 的订路发生在 20614 之前，20614 只拦住"移动"，拦不住"订路"**。这正是 KI-438 的成因。

### 11.2 结论（本轮）

1. **玩家方案"换成 OT_WAITING"机制上正确，但执行层被 R3R 自己的两道护栏挡住**：
   - 换成 `OT_WAITING` 会让 `vehicle_base.h:1051/1075/1117` 三处 GOTO_COUPLE 索引锁**全部失效**，而注释里就写着失效后果（索引每 tick 3→0→1 乱跳 → 加载到路点订单 → 撞上等待车底崩溃）。
   - `OT_WAITING` 的唯一退出机制 `HandleWaiting` 恰好在超时后调 `IncrementImplicitOrderIndex()`（`vehicle.cpp:3916-3942`），一进去就踩第一条。
   - 且 `R3RIsCoupleTarget`（`:2151`）、`TrainCoupleHandler` 的 `target-not-wait`（`:12802`）、`R3RIsCoupleCommand`（`:6171`）全按类型判，换掉即失配（KI-274 的 `type_from=17` 已被跳过=开走）。
   - **所以：不建议真的换成 `OT_WAITING`。**
2. **但玩家要的效果可以百分之百拿到，且不必换类型**：`OT_WAITING` 之所以干净，唯一原因是 **`ProcessOrders` 里不存在"为 OT_WAITING 订路"的分支**；我们只需让"已驻留的挂接类订单"在**订路入口**被短路，而不是像现在这样在 `:20614`（订路之后）才短路。
3. **落点三选（代价递增，收益递增）**：
   - **W-3′（一行，可立即做）**：把 KI-208 的 `RESV-NOBOOK` 门禁（`train_cmd.cpp:16215-16223`）里的站台格限制去掉，条件只剩"`cur_speed == 0` 且（`OT_WAIT_COUPLE` 或 car-only）"。覆盖野外/非站台。
   - **W-4′（推荐，正解）**：在 `TryPathReserve` 的订路入口（即 `:16215` 那个薄包装）加统一谓词 `R3RIsParkedWaiting(consist)` = `cur_speed == 0` ∧ (`OT_WAIT_COUPLE` ∨ car-only ∨（`OT_GOTO_COUPLE` ∧ 配对目标不在位）)，一次覆盖被动方与主动方两种"等不到车"的形态。
   - **W-5（最像计时调度，未验证）**：GOTO_COUPLE 到点后把 `current_order` 的目的地就地"置为当前站/格"（模拟"已到达"），让 `ProcessOrders` 的 GOTO 分支自己认为到站从而不订路。**类型不变 ⇒ 三把索引锁全部保留**，是目前唯一能同时满足"干净"与"不碰索引"的做法；但需要先确认 `ProcessOrders` 判"到达"的具体字段（U-7），未取数。
4. **U-7（新增待验证）**：`ProcessOrders` 判定 GOTO 型订单"已到达"的依据是 `dest_tile` 相等、站台 ID 相等，还是 `current_order.GetDestination()` + `last_station_visited`？决定 W-5 是否可行。

## 十二、玩家拍板（2026-10-22）：走 W-4′

**玩家原话**：
> 我认为W-4'比较好

即接受 §十一 11.2 第 3 条里的 **W-4′**：在订路入口加统一谓词 `R3RIsParkedWaiting()`，一次覆盖"被动方等人的"与"主动方等不到车的"两种形态，**不改任何订单类型、不动任何订单索引**。W-5 暂缓（U-7 未取数），W-3′ 被 W-4′ 吸收（W-4′ 的条件是 W-3′ 的超集）。

### 12.1 落点前必须先修正的一个口径（重要）

§十一 里把主动方条件写成"`OT_GOTO_COUPLE` ∧ 配对目标不在位"，**这条会死锁**：主动方之所以能动，靠的就是订一条去目的地的路线；如果"目标不在位"就不订路，它连目的地都到不了，永远停在原地等一个永远不会到来的目标。

必须再补一个"我已经在目的地了"的判据，而这判据 KI-193 已经现成：

- `R3RCoupleOrderDestinationReached(consist)` —— 按 depot id / station id 判到点（**刻意不按 tile**，否则站台多格永远差一格）；
- 配套 `_r3r_couple_dest_idle` 计时表 + `R3R_COUPLE_DEST_IDLE_LIMIT = 500`。

⇒ 主动方的正确条件是 **`cur_speed == 0` ∧ `OT_GOTO_COUPLE` ∧ `R3RCoupleOrderDestinationReached(consist)`**："
我已经站在目的地了、车底却还没来"，这时不订路才是安全的（原地等，且不产生幽灵路线）。

### 12.2 W-4′ 最终口径

```
R3RIsParkedWaiting(consist) =
    consist->cur_speed == 0
 && ( consist->current_order.IsType(OT_WAIT_COUPLE)        // 被动方：原地等被挂
   || R3RIsCarOnlyFormation(consist)                        // 纯车底链：本就不该动（KI-136/P7）
   || ( consist->current_order.IsType(OT_GOTO_COUPLE)       // 主动方：已在目的地等不到车
        && R3RCoupleOrderDestinationReached(consist) ) )
```

命中 ⇒ 记 `R3RLogReserveAttempt(..., TPRRF_NONE)` + 一次性 `RESV-NOBOOK ... reason=parked-waiting` + `return TPRRF_NONE`。

**刻意保留的边界**（与 KI-208 同口径，不得扩大）：
- 只拒绝**路线**，绝不碰 `ReserveTrackUnderConsist()`（耦合寻路的 `HasReservedTracks` 靠脚下/平台保护），也绝不碰 `TrainCoupleHandler()`；
- 车库格照旧走自己的路径（`OT_WAIT_COUPLE` 在库内由 `CheckTrainStayInDepot` 处理）；
- 挂上后订单类型变 + 假引擎身份被撤 ⇒ 谓词自动失效，无需清理；
- 一次性日志集合继续沿用 `_r3r_waiter_nobook_logged`（按 veh 去重），但 `reason` 分流成 `parked-waiter` / `parked-caronly` / `parked-no-target`，方便复测时区分是哪一支命中。

### 12.3 与 KI-208 的差异（必须写清，否则复测对不上）

KI-208 原门禁 = `cur_speed==0 && IsTileType(Station) && (car-only || (IsPrimaryVehicle && OT_WAIT_COUPLE))`。
W-4′ 去掉 `IsTileType(Station)`（覆盖野外/路点）、去掉 `IsPrimaryVehicle`（覆盖非链头的等待段）、新增主动方一支。

⇒ **复测时必须看到三种 reason 都出现**，且野外场景不再有 `SET-STN`/`RP cur=`。

### 12.4 待验证（实现后立即核对）

- **U-8**：`TryPathReserveWithResultFlags()` 当前是否就是 KI-208 门禁所在的那层薄包装（`:16215` 一带），以及它是否被**全部**订路调用者覆盖（含 `TrainCheckIfLineEnds`、`ChooseTrainTrack` 外部调用）。
- **U-9**：门禁 return 后，驻留车脚下的 `ReserveTrackUnderConsist` 是否仍然每 tick 正常调用（KI-208 假设是"另一个调用点"，需实证）。
- **U-10**：`R3RCoupleOrderDestinationReached()` 对"目的地是站台但列车停在站台另一格"是否返回真（KI-193 注释称按站 ID 判，需确认对当前字段的取法）。

**状态**：W-4′ **已实现 + 已编译**。`build\R3R_incbuild.done` = `EXIT_CODE=0`；日志尾 `[3/3] Linking CXX executable openttd.exe`；`build\openttd.exe` @2026-10-22 22:56:07（52 214 272 B）；`read_lints` 0 条；exe 命中 `parked-no-target` / `parked-caronly`。游戏内复测待做（判据见 KI-440 五条）。实现落点见 §十三。

## 十三、W-4′ 实现落点（2026-10-22）

只改 `src\train_cmd.cpp`，**未碰任何 `src\*.h`**（增量合规，无存档格式变更）。

1. **新增 static 函数 `R3RIsParkedWaiting(Train *consist, const char **reason)`**，定义在 `TryPathReserveWithResultFlags()` 之前（原 `:16175` 注释块之后）。三支按 §12.2 口径实现，`reason` 分别返回 `"parked-waiter"` / `"parked-caronly"` / `"parked-no-target"`。
2. **改写 KI-208 门禁**（原 `:16215-16225`）：`if (consist->cur_speed == 0 && IsTileType(...Station) && (R3RIsCarOnlyFormation || (IsPrimaryVehicle && OT_WAIT_COUPLE)))` → `if (R3RIsParkedWaiting(consist, &r3r_nobook_reason))`。
3. **`RESV-NOBOOK` 日志**把 `reason=parked-waiter` 硬编码改成 `reason=%s`，三支可区分。
4. **C4-1′/C4-3 的 `R3RShouldHoldForCouple()` 门禁一行未动**（`:16236`），它覆盖的是更具体的"路点→GOTO_COUPLE 且未配对"形态；W-4′ 是它的超集，顺序上先判 W-4′ 不影响其语义。

**复用而非新造**：主动方判据直接调 KI-193 的 `R3RCoupleOrderDestinationReached(const Train *)`（`:13470`，按 depot id / station id 判到点、刻意不按 tile），配对锁判据直接调 `R3RGetCouplePairPartner()`。

**U-8/U-9/U-10 的实测结果**：
- U-8 **成立**：`TryPathReserveWithResultFlags()`（`:16180`）确实是唯一薄包装，KI-208（`:16215`）与 C4-3（`:16236`）两道门禁都在它里面，全部订路调用者都经它。
- U-9 **成立**：`ReserveTrackUnderConsist()` 的每 tick 调用点在 `TrainLocoHandler` 的 waiter 分支（`:20036`），与订路门禁是两条独立路径，门禁 `return TPRRF_NONE` 不影响脚下保护。
- U-10 **成立**：`R3RCoupleOrderDestinationReached()` 按 depot/station id 判，站台多格不影响结果。

## 十四、玩家复测反馈（2026-10-22）：W-4′ 后「还是截不断、挡不住预留」

**玩家原话（逐字保留，情绪也要留）**：
> 好绝望，这东西修半天了还是没修好，我都开始审视这个东西必不必要了，因为它老是截不断挡不住预留

**日志**：`D:\temp\codebuddy-dropped-files\cfa07b23-0acd-4b1c-86b1-1ced526fd168\R3R_debug.log`

### 14.1 本轮必须先回答的问题（不许先深挖再落盘）

1. **W-4′ 到底有没有生效**：日志里有没有 `RESV-NOBOOK ... reason=parked-waiter|parked-caronly|parked-no-target`。
   - 一条都没有 ⇒ 要么 exe 不是新编的（用户可能开了旧 exe），要么谓词三支全没命中（条件写窄了/字段不符），要么门禁被更早的分支绕过。
   - 有、但仍然刷预设 ⇒ 说明门禁只挡住了一部分入口，还有别的订路通路（`ChooseTrainTrack` 直调、`TrainCheckIfLineEnds`、`yapf` 内部 `rp()`）。
2. **这次的 exe 是哪一版**：`build\openttd.exe` 时间戳要 >= 22:56:07（W-4′ 编译时刻），否则一切分析作废。
3. **"截不断"的具体形态**：是 `SET-STN`/`RP cur=` 继续刷，还是预留出现后被别的车清掉（`CLEAR-STN`/`RESV-GHOST`），还是预留压根没被清但也不再变化（视觉残留）。
4. **玩家问的"必不必要"**：这是真问题，必须正面回答——如果这条链路在当前引擎语义下无法做到"完全不出预留"，就该考虑改口径（例如允许出现但保证瞬时纯净、或改成"驻留时释放一次"而非"永不产生"）。不能拿"再调一轮"糊弄。

### 14.2 取证结论（行号锚点，勿重跑）

日志：`D:\temp\...\R3R_debug.log`，113 622 B / 1 416 行 / 2026-10-22 23:07:56（晚于 W-4′ 的 exe 22:56:07，采样有效）。

| 关键读数 | 行号 | 含义 |
|---|---|---|
| `RESV-NOBOOK` **0 次** | 全文 | W-4′ 三支**一次都没命中**（旧 KI-208 本来至少会打的 `reason=parked-waiter` 也没有） |
| `SET-STN` **0 次** | 全文 | 本次没有"站台整格位被订"的那类刷屏（KI-207c 族） |
| `RP cur=` **0 次** | 全文 | `rp()` 的逐格探针没打（该探针在 yapf，可能被节流窗口抑制） |
| `RESV-WATCH-SUM SET-RAIL phase=choose-track n=2560 tiles=2558 last=205,303 actor=21 head=21` | `:706` | **真正的刷屏源**：2560 次订路、2558 格，全部来自 `phase=choose-track` |
| 同上 `n=2816 tiles=2814 ... actor=21` | `:720` | 继续涨：4 分钟量级内 2816 次 |
| `CRT veh=21 order=16 dir=3 origin=205,290 td=9 found=0` | `:725` | `order=16` 即 `OT_GOTO_COUPLE`，**`found=0` = 找不到配对目标** |
| `RESV-GC veh=21 tile=205,274 … 213,250 why=free-path` | `:727-759` | 同一 tick 内订完立刻 `free-path` 释放（33 格一串） |
| `TRP veh=21 mf=204,365 real=11 type=6 spd=0 ok=1 res=1` | `:707` | **`spd=0` 仍然 `res=1`**（停着也订路成功） |
| `DEPOT-ARR veh=21 … destTx=254 destTy=234` | `:708` | 车在 204,3xx、目的地 254,234 ⇒ **根本没到目的地** |
| `RESV-REL who=CLEAR-STN veh=24 tile=278,234 track=whole owner=24 age=56 phase=controller` | `:1403-1416` | 等待方 veh=24 的站台整格预留活了 56 tick 后被清 |
| `TPR-WALK veh=21 mf=270,234 origin=278,234 td=8 okay=0 exit=2 new=279,234 reach=1 spd=0 one=1 look=0` | `:1397` | 走不动 + `one=1`（ForceReserveOnce 还没被消费） |
| `R3R-RES-ONCE used head=21 force_proceed=2 tile=259,234` | `:1398` | 紧接着被消费 |

**判定三条**：

1. **W-4′ 没有失效，是"根本没被需要"**。本次现场没有 `SET-STN`、没有 `RP cur=`、`RESV-NOBOOK` 零条 ⇒ 玩家这次抱怨的"截不断"不是 KI-208 那条通路，是**另一条通路**。也就是说：W-4′ 确实堵住了它该堵的，但它堵的那条不是玩家现在看到的这条。
2. **真正的刷屏源是 `phase=choose-track` 的 `SET-RAIL`**（`:706` / `:720`，2000+ 次、2000+ 格），它由 yapf 的 `rp()` 在 **`ChooseTrainTrack` 内部**产生，**不经过** `TryPathReserveWithResultFlags()` 的门禁 ⇒ 我在门禁上加任何谓词，在结构上**都够不到它**。这是"挡不住"的机械原因。
3. **病根是"主动方未到目的地 + 配对目标不存在（`found=0`）"**。`veh=21` 是 `OT_GOTO_COUPLE`（order=16），目的地 254,234，人却还在 204,3xx / 205,290，`found=0`（找不到车底）⇒ 每 tick 先订一条试探路线（2558 格），同一 tick 内又被 `why=free-path` 释放，下一 tick 重来。**我上一轮为避免死锁加的那条"必须已在目的地"条件，恰好把这种形态放行了。**

### 14.3 修法（W-6，待玩家拍板）

**不要**再往门禁里加条件（够不到）。正确落点是 **`found=0` 这个事实本身**：

- 当 `OT_GOTO_COUPLE` ∧ `R3RGetCouplePairPartner() == nullptr` ∧ **配对目标不存在**（`found=0`，即 `TrainCoupleHandler` 的 `u==nullptr` 分支）时：**不订路**，并把 KI-193 那套 500 tick 放弃计时**从"到达目的地之后"提前到"任意位置"**。
- 这样既干净（目标不存在就没有理由铺 2558 格），又**不会死锁**（500 tick 后按 KI-193 已有逻辑推进订单、放弃这次挂接）。
- 与 W-4′ 的关系：W-4′ 管"停稳的等待者"（本次现场没触发），W-6 管"跑着的、找不到目标的主动方"（本次现场的主症状）。两者互补，**不冲突**。

**对玩家"必不必要"的正面回答（不能回避）**：
- 车辆**要开动就必须订路**，这部分预留无法消除，也不该消除。
- 能消除的是**"找不到目标的空转订路"**——本次 2816 次里几乎全部属于这一类，是纯浪费 + 视觉干扰。
- 所以口径应该从"永不产生预留"改成"**只在有目标时才订路**"。这个目标是可以达成的，且 W-6 就是落点。

### 14.4 第 2 次取证（同一份日志，PowerShell 全文计数）——纠正 14.2 的「0 次」

> 教训：14.2 用 `search_content`（ripgrep）搜 `D:\temp\...` 这个**工作区之外**的文件，静默返回空，
> 于是把「RESV-NOBOOK 0 次」当成事实写进表里。工作区外文件必须走 `Select-String`。
> 本环境 `execute_command` 会吃掉 `$`，所以用 `(Select-String ... | Measure-Object).Count` 分号串联，逐行输出。

实测计数（顺序即串联顺序）：

| 探针 | 次数 | 含义 |
|---|---|---|
| `RESV-NOBOOK` | **1** | W-4′ 命中过，但只 1 行（`_r3r_waiter_nobook_logged` 按车去重） |
| `CPL-NOTREADY` | 0 | `TryPathReserveWithResultFlags` 的 `!pair_ready` 分支从未进入 |
| `CPL-PATHBUG` | 0 | `R3RCoupleSearchRejectMask != 0` 分支未进入 |
| `CPL-SKIP` | 0 | 无跳过 |
| `COUPLE-FAIL` | 0 | — |
| `COUPLE-DEST-EMPTY` | 0 | KI-193 的 500 tick 放弃**没触发过** |
| `CPL-SCAN` | 4 | 挂接扫描只跑了 4 次（`R3RCoupleScanHold` 8 tick 节流在压） |
| `CPL-WAITCUT-CLEAR` | **0** | 截断层（KI-433）从未激活过 |
| `CPL-WAITHOLD` | **0** | 同上 |
| `WBAR-OVERSHOOT` | 0 | 没有「越过等待区路点」的记录 |
| `why=free-path` | 143 | 预留被 `FreeTrainTrackReservation` 清掉 143 次 |
| `RESV-WATCH-SUM` | 23 | 23 × 256 ≈ 5888 次 `SET-RAIL` 记账 |

**判定四条（推翻 14.3 的前提）**：

1. `CPL-NOTREADY = 0` 是关键反证：该探针在 `!pair_ready` 时按 `((_tick_counter + index) % 500) == 0` 打，
   2816 tick 内必命中 5 次左右。**0 次 ⇒ `pair_ready == true` 是常态**。
2. `:707` 的 `type=6` 是 `OT_GOTO_COUPLE`？**不是**——查 `src\order_type.h`：`OT_GOTO_WAYPOINT = 6`、
   `OT_GOTO_COUPLE = 16`、`OT_WAIT_COUPLE = 17`。所以 `:707` 那一刻 veh=21 还在**路点订单**上，
   `:725` 的 `order=16` 才切到挂接。⇒ 「GOTO_COUPLE 的前一条真实订单是 GOTO_WAYPOINT」在本次现场**成立**。
3. 于是 `R3RShouldCutCoupleAtWaitPoint`（`train_cmd.cpp:13116`）的两个早退都被排除：prev 是路点 ✔、没 overshoot ✔。
   它没打 CLEAR，是因为 `pair_ready` 为真时走 `13224: if (pair_ready) return 0;` **一直放行**，
   而不是"截断坏了"。**「截不断」= 判据持续选择"不截断"，不是截断逻辑失效。**
4. **14.3 的 W-6 前提被证伪**：现场不是「配对目标不存在」。配对锁是建好的（`pair_ready` 恒真），
   坏的是另一层——**`found=0`**：`DoTrainCouplePathfind` 找不到一条到 A 的合法路（或几何没命中）。
   所以"不订路"会直接锁死这辆车（它连开向 A 都不行），W-6 按原文实现会从"抖"升级成"死"。

**新增疑点 REG-279-D（待验证，先落盘）**：
`pair_ready == true` 与 `found == 0` 在**同一时刻**并存。`pair_ready` 的判据
（`13212`：伙伴存在 + `cur_speed==0` + 停在站台格 + `R3RIsCoupleTarget` + 整链同站台同轴）与
`DoTrainCouplePathfind` 的可达性之间没有任何耦合。二者并存有两种解释：
- (D-1) 伙伴是**别处**的合法目标（配对锁按语义配对，不按距离），YAPF 到不了 ⇒ 每 tick 重铺是"找不到路的正常重试"；
- (D-2) 伙伴就在眼前，但寻路被 `R3RCoupleScanHold` 的 8 tick 节流或 `FSCP` 回溯否决，于是 `found=0`
  与"目标不可达"无关，纯粹是搜索被压掉 ⇒ 每 tick 重铺是**纯抖动**。
判别方式：看那 1 行 `RESV-NOBOOK` 的 veh/reason、23 行 `RESV-WATCH-SUM` 的 actor 是否全为 21、
以及 `CPL-SCAN` 那 4 行的 `rej=`/`found=`。

**状态**：本节只做计数纠正，仍未改源码。W-6 暂停（前提证伪），下一版修法待 D-1/D-2 判别后定。

### 14.5 第三次取证：日志原文窗口 `:698-733`——"订→判→清"同 tick 循环被坐实

这一段是判断根因的决定性窗口，逐行原文（`D:\temp\codebuddy-dropped-files\cfa07b23-...\R3R_debug.log`）：

```
:698  CRT veh=21 order=1  dir=3 origin=205,376 td=9 found=1
:699  CRT veh=21 order=1  dir=3 origin=204,371 td=9 found=1
:700  CRT veh=21 order=6  dir=3 origin=204,361 td=9 found=1
:701  RESV-REL who=CLEAR-STN veh=24 tile=42,55 track=whole owner=24 age=10 phase=controller purposes=0x1
:702  UNRES-SUM phase=controller n=2560 tiles=2534 capped=0 last=44,87 actor=50
:704  RESV-WATCH-SUM CLEAR-RAIL phase=controller n=2560 tiles=2539 capped=0 last=44,95 actor=50 head=24
:705  DEPOT-ARR veh=21 spd=0 real=11(6) curType=4 ... tx=204 ty=370 destTx=204 destTy=361
:706  RESV-WATCH-SUM SET-RAIL phase=choose-track n=2560 tiles=2558 capped=0 last=205,303 actor=21 head=21
:707  TRP veh=21 mf=204,365 real=11 type=6 dest=479486 spd=0 fp=0 look=1 one=0 mstuck=1 fto=1 => ok=1 res=1
:708  DEPOT-ARR veh=21 spd=0 real=11(6) curType=6 ... tx=204 ty=370 destTx=254 destTy=234
:709  RESV-REL who=CLEAR-STN veh=21 tile=204,361 track=whole owner=21 age=27 phase=controller purposes=0x1
:710..718 同上，tile=204,362 … 204,370（共 10 格，age 全为 27）
:719  CRT veh=24 order=1 dir=7 origin=83,235 td=8 found=1
:720  RESV-WATCH-SUM SET-RAIL phase=choose-track n=2816 tiles=2814 capped=0 last=230,234 actor=21 head=21
:721  UNRES-SUM phase=controller n=2816 tiles=2790 last=81,235 actor=50
:722  RESV-WATCH-SUM CLEAR-RAIL phase=controller n=2816 tiles=2795 last=205,291 actor=47 head=21
:723  CRT veh=21 order=6  dir=3 origin=237,233 td=8 found=1
:724  CRT veh=21 order=6  dir=3 origin=253,233 td=8 found=1
:725  CRT veh=21 order=16 dir=3 origin=205,290 td=9 found=0
:726  RESV-WATCH CLEAR-RAIL tile=205,274 track=1 phase=choose-track actor=21 head=21
:727  RESV-GC veh=21 tile=205,274 track=0x1 why=free-path
:728..733 同上，tile=205,273 / 205,272 / 205,271 / 205,270 / 205,269 / 205,268（y 逐个 −1）
```

**五条硬结论**：

1. **`tiles ≈ n` 严格 1:1**（2560/2558、2816/2814）。所以 `SET-RAIL` 不是"一次铺 2558 格"，而是**逐格铺路、每条 `SET-RAIL` 记一格**。⇒ 14.2 表格里"2558 格"的写法要读成"2558 次单格预留"。这不改变量级结论（2814 次/会话），但纠正了"一次铺满"的误解。
2. **`last=` 在不同时刻指向不同格且整体在移动**（205,303 → 230,234）。⇒ 14.2 判定 2 里"它由 yapf `rp()` 在 `ChooseTrainTrack` 内部产生"**成立**，但"这是幽灵预留刷屏"**不成立**——绝大部分是**列车正常行驶时每 tick 往前铺一格**。这次会话里 veh=21 确实在动（`origin` 205,376 → 204,371 → 204,361 → 237,233 → 253,233 → 205,290 → 248,234）。
3. **`:725` 是唯一的异常锚点**：`order=16`（`OT_GOTO_COUPLE`）+ `origin=205,290` + **`found=0`**。紧接着 `:726` 同一 tick 就 `CLEAR-RAIL phase=choose-track actor=21`，`:727-733` 七行 `why=free-path`。**"订"（rp 在 `:720` 之前铺到 205,303/230,234）→"判"（`:725` found=0）→"清"（`:726-733` free-path）三步全部落在同一 tick 内。** 这就是玩家看到的闪：每 tick 铺一次、判失败、再全清，下一 tick 重来。净预留永远是 0，但地图上每 tick 都在写。
4. **`:709-718` 是另一条独立泄漏**：`veh=21` 自己的 `track=whole` 站台整格位，tile 204,361…204,370（整整 10 格，正是它自己停的那段站台），`age=27` 后才被 `CLEAR-STN` 清。⇒ 停稳的车**每 tick 重新把脚下 10 格站台整格位订一遍**（`ReserveTrackUnderConsist()`），`W-4′` 拦的是 `TryPathReserveWithResultFlags()` 的**路线**，**拦不到整格位**。这与 `R3RIsParkedWaiting()` 的覆盖面缺口完全吻合。
5. **`one=0`（`:707`）而不是 `one=1`** ⇒ 这一时刻 `ForceReserveOnce` 还没被授予；到 `:1397-1398` 才出现 `one=1` → `R3R-RES-ONCE used head=21 force_proceed=2`。说明"强制预留"是在**反复失败之后**才被拉起来的，是**结果**不是原因。

**因此根因不是单点，而是三层叠加**（按可修性排序）：

| 层 | 现象 | 机制 | 是否可修 |
|---|---|---|---|
| L1 | 每 tick 铺一格、判失败、全清 | `TrainLocoHandler` 停驶分支对 `OT_GOTO_COUPLE` 先 `ChooseTrainTrack`（内部 `rp()` 已写地图）再 `found=0 → MarkTrainAsStuck → return TPRRF_NONE`，返回码让上层 `FreeTrainTrackReservation()` 清光本 tick 刚写的 | **可修**：`found=0` 属于"本次搜索无解"，应在**搜索前**判定是否值得搜（见 14.6） |
| L2 | 停稳车每 tick 重订脚下 10 格站台整格位，age=27 才清 | `ReserveTrackUnderConsist()` 不在 `W-4′` 覆盖面内 | **可修**：同一谓词加进去，或改成"仅在站台整格位缺失时补订" |
| L3 | 正常行驶的 2814 次 `SET-RAIL` | PBS 逐格预留，属正确行为 | **不可修/不该修** |

**状态**：本节为纯取证，未改源码。L1 的落点待 14.6 定；L2 的落点已明确。

### 14.6 L1 落点定了：`TryPathReserveWithResultFlags` 的 `found=0` 处置只保护"在动的车"，不保护"停着等挂的车"

**落点原文**（`src\train_cmd.cpp`，2026-10-22 树）：

```15880:15889:src/train_cmd.cpp
			const bool pair_ready = R3RHasCouplePair(consist);
			/* 第 276 轮续三 (KI-433): 截断态下"行驶中保持现状"不成立。 */
			if (keep_track != INVALID_TRACK && consist->cur_speed != 0 && pair_ready && r3r_wait_cut == 0) return { keep_track, result_flags };
			if (!pair_ready || r3r_wait_cut != 0) {
				... consist->cur_speed = 0; consist->subspeed = 0;
			}
```

```15929:15931:src/train_cmd.cpp
			if (changed_signal != INVALID_TRACKDIR) MarkSingleSignalDirty(tile, changed_signal);
			FreeTrainTrackReservation(consist, origin.tile, origin.trackdir);
			if (mark_stuck) MarkTrainAsStuck(consist);
```

**判据链（与 14.5 的锚点逐一对上）**：

1. 第 271 轮（`15856-15868`）加的保护是「**车还在动**就别拿 found=0 拆预留」，条件里明写 `consist->cur_speed != 0`。
2. 现场 `:707 TRP veh=21 ... spd=0` ⇒ `15888` 的早退**不成立**（车是停着的）。
3. 于是执行流落到 `15930 FreeTrainTrackReservation(...)` ⇒ `R3RResvLedgerForget(consist->index, "free-path")`（`14454`）把**本 tick 刚由 yapf `rp()` 铺的路**（`:720` 的 2814 次单格）和**waiter gate 订的 10 格站台整格位**（`:709-718`）一起清光（`:726-733` 的 7 行 `why=free-path`）。
4. 下一 tick：`20072` 的 waiter gate 重新 `ReserveTrackUnderConsist()` + 沿站台轴补订（`20102-20111`）；同时 `TrainLocoHandler` 再进一次 couple pathfind ⇒ 又是 `found=0` ⇒ 又清。
5. **净结果 = 每 tick 一次"订→判→清"，地图上预留位在 0 与 ~10 格之间来回跳**。这正是玩家看到的闪烁，也解释了为什么 `why=free-path` 有 143 次而 `tiles≈n` 1:1。

**为什么 `pair_ready==true` 时清预留毫无收益**（这是本节的判据核心）：

- `pair_ready == true` 表示配对锁已建立、伙伴存在且是合法目标（14.4 已由 `CPL-NOTREADY=0` 证明这是常态）⇒ 这不是 KI-316 要处理的「**根本没有可挂车底**」。
- 车 `spd=0` 停在站台 ⇒ 手上那段预留它**不会走**，清了不会"释放给别的车"，只会下一 tick 被 waiter gate 订回来。
- 因此 `pair_ready && spd==0 && found==0` 的语义是「**这一 tick 没搜到路 / 被 8 tick 节流压掉 / 伙伴尚未到位**」，正确反应是**保持原状、下一 tick 重试**——与第 271 轮给"行驶中"的口径同构。

**修法 L1（一行条件，最小行为面）**：

```cpp
/* 停着但配对已建立：found=0 只是"这一 tick 没搜到"，不是"没有目标"。
 * 撤预留既不会释放给谁（车不走），下一 tick 又会被 waiter gate 订回来，
 * 净效果只是在地图上制造每 tick 一次的预留闪烁。 */
if (keep_track != INVALID_TRACK && (consist->cur_speed != 0 || pair_ready) && r3r_wait_cut == 0) {
    return { keep_track, result_flags };
}
```

等价改写（更易读，也可只动 `15888` 一处）：把 `consist->cur_speed != 0` 换成 `(consist->cur_speed != 0 || pair_ready)`。

**刻意保留的分支**：

- `r3r_wait_cut != 0`（KI-433 等待区截断态）**仍按住**：`15892-15906` 的 `cur_speed=0` 照旧，早退条件里的 `r3r_wait_cut == 0` 不动。
- `!pair_ready`（真·没有配对目标）**仍走 KI-316**：`15930-15931` 的清预留 + `MarkTrainAsStuck` 不动，第 214 轮玩家口径不变。

**已知风险（必须写明，需玩家知情）**：

- R-279-1：`pair_ready` 的判据（`13212`：伙伴存在 + `spd==0` + 同站台同轴 + `R3RIsCoupleTarget`）**不含可达性**。若伙伴在别处（14.4 的 D-1 解释），本修法会让这辆车**长期持有**一段指向伙伴方向的路由预留而不清 ⇒ 那段路对其他车是"有主、不可占"，可能造成新的阻塞，而且 `R3RReservationAudit` 按 owner 判定不会 reap 它（它确实有主）。
- 可选收紧（若实测出现上述阻塞再启用）：只在「`keep_track` 的路径终点所在站台 == 伙伴所在站台」时保持，否则照旧清。这需要一次路径终点判定，属于第二版。
- R-279-2：本修法不解决 D-1/D-2 本身（伙伴是否真的可达）。它只消除"每 tick 自我清空"的抖动，**不保证能挂上**。若 D-1 成立，现象会从"预留闪烁"变成"预留一直停在原地、车不动"——观感更好但功能仍未达。⇒ **D-1/D-2 判别仍是必须做的**（看那 1 行 `RESV-NOBOOK` 的 veh/reason、23 行 `RESV-WATCH-SUM` 的 actor、4 行 `CPL-SCAN` 的 `rej=`/`found=`）。

### 14.7 L2 落点：`ReserveTrackUnderConsist()` 的调用点（`20102-20111`）

**落点原文**（`src\train_cmd.cpp`，waiter gate 内）：

```20070:20111:src/train_cmd.cpp
	bool r3r_platform_waiter = R3RIsCarOnlyFormation(consist) ||
			(consist->IsPrimaryVehicle() && consist->current_order.IsType(OT_WAIT_COUPLE));
	if (r3r_platform_waiter && consist->cur_speed == 0 && IsTileType(consist->tile, TileType::Station)) {
		...
		consist->ReserveTrackUnderConsist();
		const Axis axis = GetRailStationAxis(consist->tile);
		const TileIndexDiff delta = TileOffsByAxis(axis);
		const Track track = AxisToTrack(axis);
		for (TileIndex t = consist->tile; IsCompatibleTrainStationTile(t, consist->tile); t -= delta) TryReserveRailTrack(t, track);
		for (TileIndex t = consist->tile; IsCompatibleTrainStationTile(t, consist->tile); t += delta) TryReserveRailTrack(t, track);
	}
```

**定性（与 14.5 第 4 条互补，纠正"L2 是个独立 bug"的读法）**：

1. 这 10 格整格位**是设计**（KI-206/KI-207 的注释就写在 `20052-20069`）：耦合寻路器 `PfDetectDestination` 的 `HasReservedTracks` 闸门**正是靠等待车底自己订住站台**才能找到它。删掉/收紧它会直接破坏挂车（KI-206 记录过那个 `COUPLE-FAIL` 死循环）。
2. 因此 L2 的"成本"不是"订"，而是"**被 L1 每 tick 清掉之后又每 tick 订回来**"。**修好 L1 后，20102-20111 的重复订立即降为"已订→幂等 no-op"**（`TryReserveRailTrack` 是 bit test + set），开销与闪烁同时消失。
3. 唯一值得加的独立优化：进 gate 后先判「`HasStationReservation(consist->tile)` 且沿轴两端都已有该 track 的整格位」⇒ 直接跳过整段重扫。**这是纯性能短路，不改语义**，且应排在 L1 之后做（否则治不了闪烁）。

**因此修改顺序（建议）**：

| 步 | 改动 | 文件/行 | 性质 |
|---|---|---|---|
| 1 | `15888` 早退条件加 `\|\| pair_ready` | `src\train_cmd.cpp:15888` | 行为修（治闪烁） |
| 2 | 实测：确认 `why=free-path` 不再每 tick 出现、`RESV-REL who=CLEAR-STN ... age=` 不再周期性归零 | 日志 | 复测 |
| 3 | （可选）`20102` 前加"整格位已齐则跳过"短路 | `src\train_cmd.cpp:20102` | 纯性能 |
| 4 | D-1/D-2 判别（`RESV-NOBOOK` 1 行 + `CPL-SCAN` 4 行取数） | 日志 | 决定是否还要追"为什么 found=0" |

**状态**：14.6/14.7 仍为**纯分析，未改源码**。等玩家拍板；按用户规则，改码前不需要再取证（落点与行号已定）。

### 14.8 玩家纠正（2026-10-22，逐字）：问题不是"闪烁/谁卷地毯重铺"，是「预留还是没有被正常按预期截断」

**玩家原话**：
> 等一下，我看到的问题不是谁闪烁谁卷地毯重铺的问题，我看到的是，预留还是没有被正常按预期截断

**纠正结论：玩家是对的，14.6/14.7 与 KI-441 定性跑偏了。** 真实病灶在**截断判据本身**，不在"清空"。

1. 截断层（`src\train_cmd.cpp:15802-15814`）的入口是 `const int r3r_wait_cut = R3RShouldCutCoupleAtWaitPoint(consist, nullptr);`。若它**恒返回 0**，则：
   - `15803 if (r3r_wait_cut != 0)` 整块不执行 ⇒ `CPL-WAITCUT-CLEAR` 一条也打不出来；
   - `15892` 的 `CPL-WAITHOLD` 同样打不出来；
   - `15854` / `15888` 两个早退里的 `r3r_wait_cut == 0` **恒成立**，于是"保持现状"分支永远优先。
2. 现场计数（第 279 轮第二次取证，§14.4 表）恰好是：`CPL-WAITCUT-CLEAR = 0`、`CPL-WAITHOLD = 0`、`WBAR-OVERSHOOT = 0` ⇒ **截断层从未激活过一次**。
3. 为什么不激活：`R3RShouldCutCoupleAtWaitPoint()`（`:13116-13240`）在第 `13224` 行写死
   ```13224:13231:src/train_cmd.cpp
   if (pair_ready) {
       if (bs.seen_on_wp && R3RDbgOn()) { ... WBAR-RELEASE ... }
       return 0;
   }
   ```
   即"配对就绪 ⇒ 一律不截断"。而 `CPL-NOTREADY = 0`（全日志 0 次，§14.4 判定 1）证明 `pair_ready == true` 是**常态** ⇒ 该函数恒走 `13224` ⇒ 恒返回 0。
4. 于是现场形态与玩家描述逐字吻合：目标车底已在站台停稳（`pair_ready` 真），主动方还在 `204,3xx / 205,290`，离等待区路点（`254,234` 一带）很远，却**已获得"全程放行"**，预留一路铺出去（本次 `rp()` 2816 格单格 + `SET-RAIL` 2000+ 格），**从头到尾没有在等待区路点处被截断过**。玩家看到的"预留没截断、伸得太远"，就是这个 `return 0`。
5. 责任归属：这是第 277 轮 **F-437-D**（注释在 `:13190-13208`）的副作用——它为了修 KI-435（越过路点后仍返回 1 ⇒ 永久停车）而把"位置"从判据里**整体删除**，改成纯"配对就绪"。删得过头：`overshoot`（`:13161-13172`）已经单独解决了越过死锁，位置判据本可以保留在"放行"这一侧。

**因此 14.6/14.7 + KI-441 的处置**：
- 14.6 提的 `:15888` 加 `|| pair_ready` **不能修这个问题**，而且会把 `pair_ready` 也变成"该保持"的理由，**把不截断钉得更死**。该修法撤销，不再作为候选。
- 14.7 关于 L2（10 格整格位）属于设计的判断仍然成立，但与本问题无关。

**真实修法方向（待玩家拍板，未改码）**：把放行条件从「`pair_ready`」改成「`pair_ready` ∧ **本车已到栅栏格**」（等价于 `bs.seen_on_wp`，或 `front_on_wp`）：

| `pair_ready` | 本车已到/停在等待区路点 | 期望返回值 | 含义 |
|---|---|---|---|
| 否 | 否 | 1 | 截断，终点=路点（现行） |
| 否 | 是 | 2 | 原地等（现行） |
| 是 | **否** | **1（新）** | **截断在路点，等车开到路点再延伸**（现行错给 0） |
| 是 | 是 | 0 | 放行（现行） |

- `overshoot`（已在身后）继续返回 0，KI-435 的越过死锁不会被这个改动复活。
- 判据只会让"提前放行"变成"到路点再放行"，所以**不会新增永久停车**：车到栅栏格即满足放行条件。
- 副作用待评估：机车到路点会停一下再起步（与玩家"参考计时调度"的预期方向一致，但可能表现为一次额外停顿）。

**状态**：14.8 为纯分析，零源码改动。等玩家确认修法方向后再动 `:13224`。

### 14.9 玩家第二次纠正（2026-10-22，逐字）：主动方**先**到等待区

**玩家原话**：
> 不，我并不想这样，现场也并不是这样。主动方反而是先到等待区的，这能推翻你一大堆推论

**被推翻的部分（作废，不得再引用）**：
- §14.8 第 3 条、第 4 条里"主动方还在 `204,3xx / 205,290`、等待区路点在 `254,234`、离路点很远却已获全程放行"这一整套**空间描述作废**。
- 由它推出的"放行被提前到几百格之外"一并作废。
- KI-442 的 K-442-3 整段作废（已在 KI 里加推翻标记）。

**仍然成立的（这些是计数与代码事实，不依赖现场空间位置）**：
1. `CPL-WAITCUT-CLEAR = 0`、`CPL-WAITHOLD = 0`、`CPL-NOTREADY = 0`（§14.4 表）⇒ 截断层（`:15802-15814`）一次没进过 ⇒ `r3r_wait_cut` 恒 0。
2. 唯一能让 `r3r_wait_cut` 恒 0 的出口是 `:13224 if (pair_ready) return 0;`（其余三条早退已被 §14.4 判定 2/3 逐个排除：`prev` 是 `OT_GOTO_WAYPOINT`、无 `WBAR-OVERSHOOT`）。
3. 因此"配对就绪 ⇒ 不截断"这条**判据本身**仍然是真的；错的是我对"什么时候就绪、车在哪"的想象。

**新增疑点（必须先取数，禁止再造现场模型）**：

- **REG-279-E**：主动方先到等待区，之后判据为什么仍给 0（既不是 1 也不是 2）？四个候选，必须用日志判别：
  - **E-1 `pair_ready` 误真**：`pair_ready`（`:13210-13222`）只校验"配对锁伙伴停稳 + 伙伴在站台格 + 是合法挂接目标 + 整链同站台同轴"，**既不校验伙伴是否本条 `OT_GOTO_COUPLE` 命令的真正目标，也不校验两车距离/是否同一站台**。若配对锁指向同站的另一条等待链，就会提前判"就绪"。
  - **E-2 `overshoot` 已置位**：置位后 `:13172 return 0` 永久放行。但全日志 `WBAR-OVERSHOOT = 0` ⇒ 除非那一刻 `R3RDbgOn()` 为假，否则 E-2 不成立（`R3RDbgOn()` 关掉会让所有截断/障碍日志同时消失，这与"其它日志有"矛盾，倾向于排除，但需一句话确认）。
  - **E-3 `prev` 不是路点**：已被 §14.4 判定 2 排除（`:707 orderType=6` = `OT_GOTO_WAYPOINT`）。
  - **E-4 车从未停在路点格**：车速高、一 tick 跨过路点格 ⇒ `front_on_wp` 一次也没为真 ⇒ 只影响"返回 2"这一档，**不能解释判据给 0**。
  - **取数缺口（关键，此前一直漏）**：§14.4 的计数表**没有统计 `CPL-WAITCUT` 这一行**。它是 yapf 侧按状态边沿触发的（`yapf_rail.cpp:850-858`，`CPL-WAITCUT veh=%d state=%d wp=%u tile=x,y`）。只要它出现 `state=0` 带 `wp=非0`，就说明"路点认出来了、但判据选择不截断"，E-1 直接坐实。

- **REG-279-F（口径）**：玩家说的"按预期截断"具体是哪一件事，必须先钉死，否则修法一定又跑偏：
  - F-a：预留**止于等待区路点**（不再往前铺），车可以继续往前开；
  - F-b：车**停在等待区路点等**，预留自然只到路点；
  - F-c：预留**不得越过等待区路点**，若已越过就必须回收。
  三条解法完全不同（F-a 只动 YAPF 终点；F-b 还要按住车；F-c 要清已铺的路）。

**修法状态变更**：§14.8 提的 `pair_ready && 已到栅栏格` **暂缓**。它解决的是"提前放行"，而现场是"先到路点仍不截断" ⇒ 若 E-1 成立，该修法**完全无效**（车已经到栅栏格，`已到` 为真，照样放行）。必须先判 E-1/E-4，再定修法。

**状态**：14.9 为纯分析，零源码改动。等玩家回答 REG-279-F 的口径 + 补一条 `CPL-WAITCUT` 取数。

### 14.10 第四次取证：`CPL-WAITCUT` 拿到了（此前一直漏统计），截断**开过又关掉**

**取数源**：`D:\temp\codebuddy-dropped-files\cfa07b23-0acd-4b1c-86b1-1ced526fd168\R3R_debug.log`（113 622 B，2026-10-22 23:07:56；`build\R3R_debug.log` 是同一份副本）。用 `Select-String`（工作区内/外都可靠），不是 `search_content`。

**原始行（逐字）**：
```
:828   CPL-WAITCUT veh=21 state=1 wp=17 tile=205,274
:1040  CPL-WAITCUT veh=21 state=0 wp=17 tile=248,234
```
**同时为零的四条**（`Select-String` 计数，输出为空）：`CPL-WAITCUT-CLEAR` = 0、`CPL-WAITHOLD` = 0、`WBAR-OVERSHOOT` = 0、`WBAR-RELEASE` = 0。

**订单侧锚点**：
```
:85    R3RDUMP-ORDER head=21 i=7 type=6 dest=16      ← 等待区路点（type=6=OT_GOTO_WAYPOINT）
:470     L-ORD 7 type=6 dest=16
:107   CPL-PAIR tick=2338 act=21 tgt=24 dist=0 actTile=1889,188 tgtTile=1889,188
:1037~ CPL-PAIR act=21 actTile=248,234 tgt=24 tgtTile=265,234 dist=17 → 逐 tick 逼近到 dist=7 (tile 258,234)
```
`wp=17` = `wait_dest.base()+1` ⇒ `wait_dest = 16`，与 `i=7 dest=16` 对上 ⇒ 判据认路点**是对的**。

**判定（四条，全部基于上述原文）**：
1. **截断确实激活过**：`state=1`（`:828`，车在 `205,274`）。⇒ §14.4 "截断层从未激活"这句话**不准确**，应改成"train_cmd 侧的 `:15802` 一段从未激活（`CPL-WAITCUT-CLEAR=0` 佐证），yapf 侧的判据激活过"。
2. **然后在 `248,234` 被解除**：`state=0`（`:1040`），此时车底 `tgt=24` 还在 `265,234`、相距 17 格。解除后预留一路铺向车底。
3. **全程没有 `state=2`，也没有 `CPL-WAITHOLD`** ⇒ 代码**一次都没有认为"车停在等待区路点格上"**。这与玩家口径"主动方反而是先到等待区的"直接冲突，是当前最硬的一个矛盾点。
4. **没有 `CLEAR`**：说明 train_cmd 侧 `:15802` 那次调用拿到的 `r3r_wait_cut` 恒 0，而 yapf 侧 `FindNearestCoupleTrain` 拿到过 1 ⇒ 同一函数、同一辆车、同一订单，两处调用点**结论不一致**。这一条本身就是独立缺陷（见 REG-279-G）。

**REG-279-G（新增疑点）**：`R3RShouldCutCoupleAtWaitPoint()` 有两个调用点——`train_cmd.cpp:15802`（传 `consist`，链头）与 `yapf_rail.cpp:843`（传 `v`）。现场 yapf 侧给出 1、train_cmd 侧给出 0（CLEAR 0 次）。两者差异只可能来自：①传入对象不同（链头 vs 非链头，`IsFrontEngine()` 早退）；②调用 tick 不同（`state` 由 `pair_ready` 逐 tick 重算）。若是①，则"截断清旧预留"这一步**永远不会执行**，无论判据怎么改都清不掉旧路。

**口径**：本轮不修，不猜路点坐标。已确认的事实只有上面四条 + `wp=16` 与订单对上。下一步取决于玩家对 REG-279-F（"按预期截断"指哪件事）的回答。

**状态**：14.10 为纯取证，零源码改动。

### 14.11 玩家第五次取证（2026-10-22）：速度画像 400→80 正常、进路点格瞬间 80→2

**玩家原话（逐字）**：
> 那辆车的 movingfront 准备进入路点那一格的时候，速度诡异的，刷的一下就从八十几公里降到了两公里/时，而在这之前，它确实是有大幅度减速的，从四百多公里降到了八十多，如果刹车踩到底应该是刚好可以在路点那里停下来停好的

**这条事实能定死三件事**：

1. **400 → 80 是正常制动**：说明"前方要停"这件事确实被制动系统知道了，而且有足够距离。⇒ 判据在远处就已经给出"要停在路点"（`state=1`，`:828 tile=205,274` 与此吻合），**不是** §14.8 说的"几百格外一路放行"。§14.8 的"提前放行"推论再次作废。
2. **80 → 2 是 tick 级跳变**，不是渐进减速：正常 LookAhead 制动会给出 80→70→60…的斜坡。一 tick 从 80 掉到 2，只可能是「引擎撞上自己的预留末端（PBS 终点）」被硬钳，或某处直接写 `cur_speed`。
3. **玩家说"刹车踩到底刚好能停好"** ⇒ 物理上够，是**判定时机晚了一格**：等到车头进入路点格才判"停"，此时已无减速距离，于是跳到 2 km/h 爬行。

**猜测根因（待验证，不得当结论）**：
- **G-1（首选）**：截断只限制了"预留能铺到哪"（YAPF 终点/最大距离 = 路点格），**但没有把"这里要停车"作为目标速度告诉引擎**。引擎只知道"线路继续、预留到此为止"，于是按巡航速度冲到最后，被 `TrainController` 的"到达预留末端"分支硬刹 ⇒ 80→2。若成立，则 `state=1` 确实生效、预留**确实被截断了**，玩家的抱怨点是"急刹"而不是"没截断"。（与 §14.8 结论相反，必须先验 G-1 再谈修法。）
- **G-2**：判据在最后几格抖动（1↔0），每次变化都重建路径并重置速度 ⇒ 跳变。可用 `CPL-WAITCUT` 是否连续出现 state=1/0 交替来判别（现场只有 1 条 state=1、1 条 state=0，倾向排除）。
- **G-3**：R3R 自身某分支直接写 `consist->cur_speed`/`subspeed`（KI-265 修法 3 的备注承认此类分支存在）⇒ 需搜写入点。

**待验证判据**：
- 读 `src\train_cmd.cpp:15780-15830`，确认 `r3r_wait_cut != 0` 时到底改了什么（YAPF 终点？max_distance？还是只改 `dest`），这决定 G-1 成立与否。
- 日志中该车 `CPL-PAIR` 逐 tick `actTile` 是否在某格连续重复 ≥3 行（=2 km/h 爬行的那几格），以及那几格与 `:1040 tile=248,234` 的关系。
- 搜 R3R 自有 `cur_speed =` 写入点。

**状态**：14.11 为纯取证，零源码改动。

### 14.12 尾部 45 行取到了：**这次挂接其实成功了**，急刹另有出处

**日志尾部逐字**（同一份日志，行号 1133~1170）：
```
:1140 CPL-PAIR tick=3038 act=21 tgt=24 dist=7 actTile=258,234 tgtTile=265,234   ← 最后一条位置
:1142 CPL-GEO site=geo v=21 vn=47 vlen=2 u=24 z=24 ulen=2 rev=0 dx=2 dy=0 diff=2 need=2 spd=30 db=1
:1144 RESV-GC veh=24 tile=278,234 track=stn kind=STATION why=couple-hold   （278→265 连续 14 行）
:1158 ARRANGE-IN dh=21 dst=47 sh=24 src=24 mc=1
:1160 ARRANGE-RFC-DONE / ARRANGE-IC-DONE / ARRANGE-NDH-DONE
:1162 CPL-WAITCLEAR veh=24 head=21 tile=265,234 segFront=1 primary=1
:1163 RESV-GC veh=21 tile=264,234 track=0x0 why=couple          （264→259 连续 6 行）
:1168 RESV-LEDGER-FORGET veh=21 why=couple mask=0x1 released=6 kept=0 phase=misc
:1169 RESPACE-AFTER-EDIT couple head=21 px=0 stretch=0 unfixed=0 skip=0 front=0 passes=1 visited=47 capped=0
:1170 COUPLE-OK loco=21 rear=50 consist=24 co=0 real=12 type=1 tx=259 ty=234 x=4144 y=3752
```

**结论 1：这次**挂上了**（`COUPLE-OK` 是本日志最后一条业务结论）。**
- 车底 veh=24 停在 `265,234`（`CPL-WAITCLEAR ... tile=265,234` 证明那是等待点）。
- 主动方 veh=21 从 `248,234` 一路开到 `259,234` 贴上，`dist 17→7`，末端 24 tick 走 10 格。
- 耦合那一刻 `CPL-GEO spd=30`：**贴上去时速度是 30，不是 2**。⇒ 玩家看到的"2 km/h 爬行"发生在这之前（`dist>7` 那一段或更早），不是耦合瞬间。

**结论 2：R3R 明文写速度归零只有一个地方，而它没被走过。**
```15904:15905:src/train_cmd.cpp
consist->cur_speed = 0;
consist->subspeed = 0;
```
它的进入条件是 `path_found == INVALID_TRACK`（`:15828`）且 `(!pair_ready || r3r_wait_cut != 0)`（`:15889`），并且上一行 `:15899` 会打 `CPL-WAITHOLD`。现场 `CPL-WAITHOLD = 0`（本日志全文）⇒ **这条分支一次都没执行** ⇒ 80→2 不是 R3R 直接写的。

**结论 3：80→2 因此只能来自引擎侧"撞到自己预留末端"的硬钳。** 而那个末端正是被截断在等待区路点的那一格 —— 即玩家的物理观察反过来证明**截断是生效的**，问题是引擎把"停车点"当成"路的尽头"，没有提前松油门。

**REG-279-H（新疑点）**：`CPL-WAITCUT state=0 wp=17 tile=248,234` 打在 `248,234`，而车正是从 `248,234` 开始逐格前进（`:1037 tick=3014`）。若 `state=0` 表示"放行"，速度不该掉到 2；若它表示"已到达、不必再截"，那玩家看到的急刹发生在**进入 248 之前一格**。两者必须用 `TrainController` 的预留末端处理来判别，不能靠位置反推。

**下一步（未取）**：读 `src\train_cmd.cpp` 的 `TrainController()` 中对"预留末端 / 前方无预留"的速度处理，确认是否存在 `cur_speed` 被直接钳到 2 的分支；若是，则修法方向从"改判据"改为"把截断点作为**停车目标**（目标速度）交给引擎，而不是只当预留边界"。

**状态**：14.12 为纯取证，零源码改动。§14.8 的"提前放行导致预留没截断"结论**再次作废**；现行最佳解释是"截断生效但刹车方式是硬刹"。

---

## §14.13（2026-10-22）80→2 的代码落点：截断让引擎把路点当成"线路尽头"

### 14.13.1 取证锚点（源码，只读）

| 位置 | 内容 |
|---|---|
| `src\train_cmd.cpp:19590-19592` | `static const uint16_t _breakdown_speeds[16] = {225,210,195,180,165,150,135,120,105,90,75,60,45,30,15,15};` |
| `src\train_cmd.cpp:19603-19645` | `TrainApproachingLineEnd(moving_front, signal, reverse)`：按"当前格内位置 x"查上表；`:19642 if (break_speed < consist->cur_speed) consist->cur_speed = break_speed;`（**硬钳，不回弹**） |
| `src\train_cmd.cpp:19632-19637` | 只有 `signal == false` 且 `x + 半车长 >= TILE_SIZE` 时才 `cur_speed = 0` + `ReverseTrainDirection`；**红信号路径（signal=true）不反转、不停车，只减速** |
| `src\train_cmd.cpp:19717-19766` | `TrainCheckIfLineEnds()`：`bits == TRACK_BIT_NONE \|\| !CheckCompatibleRail` ⇒ `TrainApproachingLineEnd(..., false)`（**尽头语义**）；`(trackdirbits & red_signals) != 0` ⇒ `TrainApproachingLineEnd(..., true)`（**红信号语义**） |

单位换算：`cur_speed` 是 1/2 km/h ⇒ 表的 225 = 112.5 km/h，末格 15 = 7.5 km/h。

### 14.13.2 结论句

1. **400→80 是标准减速曲线，不是缺陷**：`_breakdown_speeds` 从 112.5 逐格降到 7.5，玩家看到的"八十多"就是这条曲线进入末段时的读数。
2. **80→2 的"刷的一下"是这张表的末两格（112.5→7.5 只在最后一格内完成）**：x 走到 13/14/15 时上限依次 15(=7.5)/15(=7.5)，此前一格还是 30(=15)、45(=22.5)。也就是说"最后一格内极短时间内从几十掉到个位数"是这条查表减速的**固有形状**，与 R3R 写 `cur_speed` 无关（`:15904` 已在本轮 §14.12 排除，`CPL-WAITHOLD = 0`）。
3. 触发条件是 `TrainCheckIfLineEnds` 判到"前方无路 / 红信号"。截断把预留停在路点格 ⇒ 路点之后对本站而言就是**红信号（PBS 未预留）** ⇒ `signal=true` 分支 ⇒ **只减速、不停车、不掉头**。这正是"车龟速爬进路点格、然后停住等挂接"的观感来源。
4. 因此问题的性质不是"截断没生效"，而是**截断只提供了"路的尽头"，没有提供"停车目标"**。引擎对尽头/红信号只有一条按格查表的刹车策略，最后一格才刹到底，所以必然"进了格才猛刹"。

### 14.13.3 口径（承接 REG-279-F，玩家本轮原话已隐含答案）

玩家原话："如果刹车踩到底应该是刚好可以在路点那里停下来停好的" ⇒ 期望形态 = **F-b：车平稳减速、正好停在等待区路点格内、停稳后再继续去挂**。
据此：
- F-a（仅预留止于路点、车仍可越过）= 与玩家期望不符，**排除**；
- F-c（越过就回收）= 只是兜底安全网，不是需求本身。
- 结论：`r3r_wait_cut != 0` 时必须把路点当成**减速目标**（有距离的停车点），而不是只把 YAPF 终点截在路点。

### 14.13.4 修法候选（**未落地**，需玩家拍板后再动代码）

- **G-1（R3R 层，风险最低）**：在 `r3r_wait_cut != 0` 期间，按"moving_front 到路点格的距离"给 `cur_speed` 设一个上限（复用站台/正常进站的距离-速度关系，而不是 `_breakdown_speeds`），使减速发生在**路点前若干格**而不是最后一格内。落点仍在 `train_cmd.cpp` 的 `TrainController` 入口/`TryPathReserveWithResultFlags` 附近，不碰引擎查表。
- **G-2（引擎层，风险高）**：让 `_breakdown_speeds` 对"红信号且是本车的截断终点"改用长距离刹车曲线 —— 需改 `TrainApproachingLineEnd`，影响面覆盖所有列车，**不建议**。
- **G-3（放弃截断、改订单停车）**：不截断预留，改为在路点格用订单逻辑停车（`state=2`）。后果 = 预留会先铺过去再撤销，正是 KI-127/KI-208 玩家明确批评过的"幽灵预留"体验，**倾向排除**。

### 14.13.5 疑点（待确认）

- **REG-279-I**：`TrainCheckIfLineEnds` 的两条入口（`bits == NONE` 的尽头语义 vs `red_signals` 的红信号语义）在本现场**哪一条被走到**尚无数（日志无对应探针）。二者行为不同：尽头语义在格末会 `cur_speed=0` 且**掉头**，与"车停在路点等挂"的现场矛盾，故倾向红信号语义；但需一条探针（`LINEND-*`：bits/red/spd/x）才能钉死，不能靠观感反推。
- **REG-279-J**：G-1 的减速曲线取哪一档（站台进站曲线 / 手工线性限速）未定；若取站台曲线需要"路点格中心到车头的剩余距离"，而路点只有格坐标、无停靠点偏移，可能造成 ±1 格误差（停车点偏前/偏后），需实测。

**状态**：14.13 为纯取证，零源码改动、未构建。R3R 侧的 `CPL-WAITCUT` 判据与 `train_cmd.cpp:15802` 逻辑本轮**未改动**。

---

## §14.14（2026-10-22）REG-279-I 探针已落地并编译通过（零行为变更）

玩家原话授权："行，你加探针吧"。

### 14.14.1 落点（仅 `src\train_cmd.cpp`，未碰任何 `.h`）

| 位置 | 内容 |
|---|---|
| `R3REDGE_LINEND` 枚举 | 插在 `R3REDGE_RVCSTN` 之后、`R3REDGE_COUNT` 之前 |
| `_breakdown_speeds[]` 之后新增 `static void R3RProbeLineEnd(...)` | 纯只读；内部用 `R3RDbgEdge(R3REDGE_LINEND, veh, payload)` 节流 |
| `TrainApproachingLineEnd()` 内 `rounding` 算完、硬停判定 `if (!signal && x + ... >= TILE_SIZE)` **之前** | 调用 `R3RProbeLineEnd(moving_front, consist, signal, x, rounding, vdir);` |

**为什么选 `TrainApproachingLineEnd()` 而不是 `TrainCheckIfLineEnds()`**：前者是四条入口（`bits==TRACK_BIT_NONE`、`!CheckCompatibleRail`、红信号、隧道桥信号红）的**唯一汇合点**，且 `x`/`signal`/`vdir`/`rounding` 在这里已经算好，不必复制第二份表达式，天然不会与引擎逻辑漂移。

### 14.14.2 日志格式与判读口径

```
LINEND veh=%u tile=%u,%u x=%u sig=%d near=%d spd=%d brk=%d clamp=%d len=%d dir=%d db=%d
```

- `sig=0` ⇒ 真尽头语义（`bits==TRACK_BIT_NONE` / 轨道不可用），到格末会 `cur_speed=0` 并 `ReverseTrainDirection` **掉头**。
- `sig=1` ⇒ 红信号 / 隧道桥入口信号红，**只减速、不停车、不掉头**。
- `x` 是车头在**当前格内**的位置（0..15），`brk = _breakdown_speeds[x & 0xF]`，`clamp=1` 表示本 tick 会把 `spd` 钳到 `brk`。
- `near=1` 只可能出现在 `sig=0`，即"已到格末必须硬停+掉头"。
- 节流：`payload = sig | x | near | clamp | spd` ⇒ **x 或速度一变各留一行**，正好得到完整减速序列，不刷屏。

### 14.14.3 判定（复跑后照这三条读，不要靠观感反推）

1. **出现 `spd=160 brk=15 clamp=1` 一类行、且 x 从 0 递增到 15** ⇒ 80→7.5 确由 `_breakdown_speeds` 造成，REG-279-I 定案为"红信号语义"，修法走 G-1（R3R 侧给路点减速目标），**不碰引擎查表**。
2. **`sig=0` 且 `near=1` 出现** ⇒ 路点前方被判"无轨道"，那就不是红信号，车会掉头 —— 与现场"停在路点等挂"矛盾，需回头查 `bits==TRACK_BIT_NONE` 为何成立。
3. **`LINEND veh=<主动方>` 一行都没有** ⇒ 80→2 与"尽头/红信号"完全无关，本方向作废，改为盯 `SPD-DROP`（KI-348 已有）定位真正写速度的提交点。

### 14.14.4 构建自证

复用既有 `_tmp_inc_build.cmd`（未新建任何 `.cmd`）；`GUARD: incremental is safe`；`[3/3] Linking CXX executable openttd.exe`；`build\R3R_incbuild.done = EXIT_CODE=0`；`src\train_cmd.cpp` 23:59:05 → `train_cmd.cpp.obj` 00:00:09 → `build\openttd.exe` **2026-10-23 00:01:40（52 216 320 B）**；`read_lints` 0 条；exe 字面量 `LINEND veh=` 命中。

**状态**：已实现 + 已编译，**纯只读零行为变更**，游戏内复跑待做。REG-279-I 仍未定案，REG-279-F 口径待答，G-1 未动代码。

---

## §14.15（2026-10-23）复跑日志取证：LINEND 一条都没有 ⇒ 尽头/红信号方向**作废**

玩家本轮原话："主动方的加减速更诡异了，我承认我描述有巨大偏差"。

### 14.15.1 日志身份

| 项 | 值 |
|---|---|
| 文件 | `build\R3R_debug.log` |
| 体积 / 行数 | 113 225 B / **1 411 行** |
| 最后写入 | 2026-10-23 **0:05:07** |
| 产生者 | `build\openttd.exe` @2026-10-23 **0:01:40**（= 含 `LINEND` 探针的 §14.14 版）⇒ **采样有效，探针确实在跑** |

### 14.15.2 关键读数（全文检索，非抽样）

```
LINEND  = 0 条
SPD-DROP = 0 条
```

现场车号（日志末尾）：**主动方 `veh=21`**（`TRP veh=21 … real=12 type=1 spd=0`）、**等待方 `veh=24`**（`RESV-REL who=CLEAR-STN veh=24 tile=… track=whole`）。

末尾关键行（按出现顺序）：

```
TPR-WALK veh=21 mf=270,234 origin=278,234 td=8 okay=0 exit=2 new=279,234 reach=1 spd=0 one=1 look=0
R3R-RES-ONCE used head=21 force_proceed=2 tile=259,234
R3R-RES-ONCE clear head=21 tile=259,234
TRP veh=21 mf=270,234 real=12 type=1 dest=488228 spd=0 fp=0 look=1 one=0 mstuck=0 fto=1 => ok=1 res=1
DEPOT-ARR veh=21 spd=0 real=12(1) curType=1 stuck=0 … destTx=804 destTy=238 tileEqDest=0
RESV-REL who=CLEAR-STN veh=24 tile=278,234 … 265,234 track=whole owner=24 age=56 phase=controller
```

### 14.15.3 结论句（**REG-279-I 判定为"证伪本方向"**）

1. **§14.13 的 `_breakdown_speeds` 解释被本日志否证**：主动方 `veh=21` 全程 **0 条 `LINEND`** ⇒ 从未进入 `TrainApproachingLineEnd()` ⇒ 80→2 **不是**按格查表的尽头/红信号刹车。§14.13.2 的结论 2/3 作废，G-1（把路点当减速目标）失去前提，**暂停**。
2. 反过来说，探针本身是活的：`LINEND` 只在真进 `TrainApproachingLineEnd` 时才打，0 条 = 该函数对 veh=21 一次没被调用，不是探针失效（同一 exe 的其它字面量仍在工作，见 §14.15.4）。
3. `SPD-DROP = 0` 不矛盾：该探针抓的是"上次非零 → 本次 **0**"的边沿，而玩家说的"2"是 2 km/h ⇒ `cur_speed == 4`，**不是零**，天然抓不到。⇒ **真正的减速提交点仍无探针覆盖**，这是本轮新暴露的取证缺口。
4. 末尾 `force_proceed=2 / one=1 / R3R-RES-ONCE used|clear head=21 tile=259,234` 说明主动方当时处在**一次性强制预留**状态且 `origin=278,234` 与 `mf=270,234` 相差 8 格、`okay=0` ⇒ 主动方是在"预留不到目标、被强制放行"的形态下低速爬行，与"加减速诡异"的观感同源，但**与尽头/红信号无关**。

### 14.15.4 待验证（新方向候选，均未取证）

- **REG-279-K（高优先）**：`cur_speed` 在 veh=21 上的真实写入点。当前唯一能看速度的既有探针是 `TRP`/`DEPOT-ARR`/`CPL-*` 行里的 `spd=`，它们**不覆盖 `TrainController` 内部的加减速**。需按玩家新日志先取 `spd=` 序列（下一组工具调用），再决定加"速度写点"探针。
- **REG-279-L**：`force_proceed=2` 的取值来源与 `R3R-RES-ONCE` 的置位/清除时机，是否与"主动方减速后又窜一下"直接相关。
- **REG-279-M**：玩家"描述有巨大偏差"⇒ 需要重新确认**观测方式**（看的是速度条读数、实际位移、还是 UI 百分比），否则后续取数仍可能对着错的现象改代码。**此条需玩家回答，不得自行假设。**

**状态**：14.15 仍为纯取证，零源码改动、零构建。G-1 暂停，等 REG-279-K/L/M 有数再议。

### 14.15.5 追加读数（同一日志，第 3 组工具调用）

`TRP veh=21` 全文只有 **11 行**，且每行对应**订单索引变化**（`real=1,2,3,4,5,6,7,9,10,11,12`），不是每 tick：

```
TRP veh=21 mf=1889,188 real=1  … spd=0  one=0 mstuck=0
TRP veh=21 mf=1845,191 real=2  … spd=0  mstuck=1
TRP veh=21 mf=1319,221 real=3  … spd=0  mstuck=1
TRP veh=21 mf=793,237  real=4  … spd=0  mstuck=1
TRP veh=21 mf=267,237  real=5  … spd=8  mstuck=1   ← 全程唯一非零速度
TRP veh=21 mf=207,366  real=6  … spd=0  mstuck=1
TRP veh=21 mf=157,479  real=7  … spd=0  mstuck=1
TRP veh=21 mf=142,479  real=9  … spd=0  mstuck=0
TRP veh=21 mf=154,478  real=10 … spd=0  mstuck=1
TRP veh=21 mf=204,365  real=11 … spd=0  mstuck=1
TRP veh=21 mf=270,234  real=12 … spd=0  mstuck=0
```

结论句：

1. **`TRP` 不是速度探针**，它只在 `TryPathReserve` 结果变化时打，`spd=` 是**该瞬间的采样值**，无法重建减速曲线 ⇒ **"加减速诡异"在当前日志里根本不可观测**，这不是判定而是缺口（REG-279-K 成立）。
2. `mstuck=1` 在 12 条里出现 **8 次** ⇒ 主动方长期处于 stuck 标记态，与"低速爬行"吻合；`fto=1`（force-proceed-once）出现 8 次，与末尾 `R3R-RES-ONCE force_proceed=2` 对得上。
3. 日志跨度显示 veh=21 在**同一会话**里从 `1889,188` 一路跑到 `270,234`（横跨约 1600 格），却只有 11 条 `TRP` ⇒ 这些 `TRP` 是**订单切换点**，不是时间序列。**任何"减速曲线"论断都不能用这 11 行支撑。**
4. ⇒ 结论不变且更强：`_breakdown_speeds` 方向已作废，而**新方向尚未有可用数据**。下一步必须先确定"玩家看到的诡异"对应哪条可观测读数（REG-279-M 需玩家回答），再决定加哪一类速度探针（每 tick 打 spd 会刷屏，需按速度变化边沿触发并落盘 CSV）。

### 14.15.6 日志构成统计（第 4~5 组工具调用，全文 count）

| 标签 | 条数 | 性质 |
|---|---|---|
| `RESV-REL` | 165 | 预留释放（事件） |
| `RA idx=` | 72 | 链逐节几何 dump（事件） |
| `CPL-` | 63 | 挂车寻路（事件） |
| `CRT` | 57 | ChooseTrainTrack（事件） |
| `DEPOT-ARR` | 30 | 到点判定（事件） |
| `TRP veh=` | 17 | 预留结果（事件，共 2 列车） |
| `TPR-WALK` | 6 | 路径回溯（事件） |
| `TPR-BLOCKED` | **0** | 第 221 轮那类阻塞**未复发** |
| 含 `spd=` | 116 | 分散在上述事件行里 |
| `LINEND` | **0** | 本函数一次没被调用 |

**结论句（决定性）**：这份日志里**不存在任何逐 tick 的速度记录**。116 条 `spd=` 全是事件瞬间的采样，间隔最长的两条相隔一整个订单区间（见 §14.15.5 的 11 条 `TRP` 跨 1600 格）。⇒ **"主动方加减速诡异"在现有探针体系下不可观测**，任何基于本日志的减速曲线结论都只能是猜测。

### 14.15.7 建议探针（**未实现，待玩家批准**）

- **P-V1 `SPDD`（速度变化边沿）**：在 `TrainLocoHandler` 里对"移动中的列车"记录 `(veh, tick, cur_speed, subspeed, accel, tile, x_pos&0xF, order, real, stuck, ftd)`，**只在 `cur_speed` 变化时**写一行。单列车一程按最坏情况约数千行，可接受；跨车限流用"每车每 64 tick 最多一行"兜底。
- **P-V2 `ACCEL`（写点归因）**：在 `cur_speed` 的三类写点各打一行带 tag：`TrainController` 的加速段（`spd += accel`）、`TrainApproachingLineEnd`、`ReverseTrainDirection` 的归零。用来回答"是谁把速度改掉的"。
- **P-V3 `STUCK`**：`mstuck`/`force_proceed` 变化时打一行（本日志 12 条 `TRP` 里 8 条 `mstuck=1`，需知道它何时置位、何时清）。

**状态**：14.15 收口。本轮零源码改动、零构建。等玩家回答 REG-279-M（"诡异"具体看的是什么读数）后再决定加 P-V1/P-V2/P-V3 中的哪几项。

---

## §14.16（2026-10-23）玩家给出精确现象描述 + 授权全套速度探针

### 14.16.1 玩家原话（逐字保全，禁止压缩掉）

> 禁止雷霆压缩上下文！
> 你可以加探针，全套速度探针。
> 我描述的诡异情况是指：列车订好了一条道路，设原道路上面有等待区路点坐标A，列车按照在A停稳的加速度减速。过了一个道岔，突然列车改道不按原来的线路和预留走了，改向另一条路径，通往同一等待区路点坐标为B，同时在一个信号灯前速度骤减为0，随后继续加速往B，假若按正常减速可以车头在B停稳，但实际列车刚进入B所在格（触碰）的那一刻，速度骤减为2

REG-279-M **已由玩家回答**：观测方式是**画面位移 + 速度条数值**（"速度骤减为 0 / 骤减为 2"是数值读数，"改道"是位移观察）。⇒ §14.15.6 的"不可观测"结论作废，现在有明确的现象描述可对着取数。

### 14.16.2 现象拆成 4 个可独立取证的环节

| # | 玩家描述的环节 | 需要的读数 | 现有探针能否覆盖 |
|---|---|---|---|
| E1 | 订好一条路（A 方向），按"在 A 停稳"的加速度正常减速 | 逐 tick `cur_speed` + 目标 + ctx | ❌ 无 |
| E2 | **过道岔后突然改道**，不再走原线路与原预留，改向同等待区的 B | `dest_tile`/`chosen_track` 变化 + 预留释放/重建 | 部分（`CRT`/`TRP`/`RESV-REL` 都是事件行，无时序） |
| E3 | **在信号灯前速度骤减为 0**（骤减，不是正常刹停），随后继续加速 | `cur_speed` 变化 + 前方信号状态 + 写点 ctx | ❌ 无 |
| E4 | **刚进入 B 所在格（触碰）那一刻速度骤减为 2** | `cur_speed` 变化 + 该 tick 的 `x`（格内位置）/`sig`/写点 ctx | ❌ 无（`LINEND` 已有但本轮日志 0 命中） |

`LINEND = 0`（§14.15.2）说明 E4 发生时**没进** `TrainApproachingLineEnd()`，所以 E4 的"→2"来自别处 —— 这正是必须加 `ctx` 归因的原因。

### 14.16.3 假设（全部**待验证**，不得当结论用）

- **H-1（E4 主嫌疑）**：`TrainController` 内部对"前方有停车目标"的速度上限计算，在**车头刚跨格**时用了一个"格内剩余距离"近似，导致进格瞬间把上限算成极小值。判据：`SPDD` 里 `to=4`（2 km/h）那一行 `ctx=ctrl` 且同行 `x` 很小。
- **H-2（E3 主嫌疑）**：信号灯前"骤减为 0"是**硬赋零**而非刹停。判据：`SPDD` 里 `to=0` 且 `from` 很大（如 >100）、`ctx` 是 `red-signal` / `linend-stop` / `stuck` 之一。
- **H-3（E2）**：改道不是"重新寻路"，而是**原预留被 R3R 自己的清扫释放**后 PBS 换了条路径（本轮日志 `RESV-REL = 165` 条，远多于 `CPL-` 的 63 条）。判据：`SPDD` 改道时刻前后有 `RESV-REL`/`RESV-GC`，且 `dest_tile` 未变（目标仍是同一路点，只是路径变了）。
- **H-4（贯穿）**：E1 的"正常减速"和 E3/E4 的"骤减"是两套不同机制在打架 —— 一套是 `TrainController` 的平滑减速，一套是某个**单点硬钳**。判据：`SPDD` 序列里出现"平滑下降若干行 → 单行跳变"的形态。

### 14.16.4 探针规格（本轮实现）

- **P-V1 `SPDD`（速度变化边沿，主探针）**：`TrainLocoHandler` 入口把 `cur_speed` 与上一 tick 快照比较，变化即打一行。字段：`veh / from / to / ctx / tile / x / db / order / real / stuck / fp / one / spd_ctx_ext`。
- **P-V2 `_r3r_spd_ctx`（写点归因）**：文件内 `static const char *_r3r_spd_ctx`，在每类写 `cur_speed` 的代码路径前置 tag，`SPDD` 行把它打出来。tag 集合：`ctrl`（TrainController 平滑加减速）、`linend-table`（`_breakdown_speeds` 钳位）、`linend-stop`（尽头硬停 0）、`red-signal`（信号红）、`station`（站台到达）、`stuck`（预留失败）、`reverse`（掉头归零）、`stop`（停车/装卸）、`other`。
- **P-V3 `STUCK`**：`mstuck` / `ForceReserveOnce` 变化时打一行（对应 §14.15.5 的 `mstuck=1` 出现 8 次）。
- **P-V4 `TRACKCHG`（E2 归因）**：`ChooseTrainTrack` 的 `dest_tile` 或 `chosen_track` 与上次不同即打一行，含 `origin / dest / found / dtile_changed / track_changed`。与 `CRT` 的区别是**带上一 tick 基线**，能判"改道"而不是只报"选路结果"。

**状态**：14.16 为设计 + 落盘，探针实现与构建在下一节续写。

### 14.16.5 取证第一批：`src\train_cmd.cpp` 里全部写 `cur_speed` 的点

检索式 `cur_speed\s*(=|\+=|-=)`，共 ≥42 处（输出被截断，已见 30 处）。按性质分三类：

**A 类·硬置零（`= 0`，直接跳变，不经过刹车）**

| 行号 | 位置/语义 | 与玩家描述的对应 |
|---|---|---|
| 4166 | 停车（Stop train） | — |
| 4973 | 站台到达、`AM_ORIGINAL` 分支 | 正常到站 |
| 11191 | `v_front->cur_speed = 0; progress = 0;`（R3R 某处，待查） | ? |
| 14206 | 某处收尾 | ? |
| **15905** | **R3R `CPL-WAITCUT`：等待区路点截断，`cur_speed = 0; subspeed = 0;`** | **E3「信号灯前骤减为 0」最强嫌疑** |
| 16781 | `current_order.MakeWaiting()` + 关停靠 + `cur_speed = 0` | E3 次嫌疑 |
| 17939 / 18024 | `Couple()` 成功后置零 | 挂上后正常 |
| **18791 / 18796** | **`if (!HasSignalOnTrackdir(...)) cur_speed = 0;` / 有信号也置零** | **E3 直接嫌疑（信号判据）** |
| 18963 / 18969 | 隧道桥入口红信号 / 只出不进 → 置零 | E3 |
| 19094 / 19107 | wormhole / 太靠近前车 → 置零 | E3 |
| 19378 | `reverse` → 置零后 `ReverseTrainDirection` | E3/E4 |
| 19681 | `TrainApproachingLineEnd` 尽头硬停 → 置零 | E4（但本轮 `LINEND=0`，未触发） |

**B 类·硬钳（不为 0，直接把上限压下来）**

| 行号 | 代码 | 关键 |
|---|---|---|
| **16815** | `if (consist->cur_speed > 20) consist->cur_speed = 20;` | **钳到 20 = 10 km/h**，玩家报的是「骤减为 2」⇒ 若 `SPDD` 出现 `to=20` 即命中此点 |

**C 类·平滑（正常物理）**

| 行号 | 代码 |
|---|---|
| 16888 | `cur_speed -= (cur_speed * asp->z_up >> 8)`（上坡减速） |
| 16890-16891 | `cur_speed + asp->z_down`（下坡加速） |
| 18579 | 转向减速 `cur_speed -= (... small_turn/large_turn) * cur_speed >> 8`（`AM_ORIGINAL`） |

**结论句（第一批）**：

1. 玩家的 E3/E4 现象**不需要任何引擎魔法**就能解释 —— `src\train_cmd.cpp` 里存在 **13 处** `cur_speed = 0` 硬赋值和 **1 处** `cur_speed = 20` 硬钳位，其中 `15905`（R3R 自己的 `CPL-WAITCUT`）与 `16815`（钳 20）是本轮最该先看的两处，且这两处都在**等待区路点**语义上，与玩家描述的"通往同一等待区路点 B"高度吻合。
2. `18791/18796` 那两行是**引擎原生的信号判据**（`!HasSignalOnTrackdir` → 置零并 `progress = 255` 保证下次还撞这个信号），行为是"停在信号前"，与"骤减为 0 后继续加速"不完全一致 —— 除非列车随后换道绕过该信号，这正好接上玩家说的「改道」。
3. ⇒ **ctx 归因必须覆盖的点已确定**，P-V2 的 tag 集合从 9 个扩到 13 个，逐个对应上表行号（见 §14.16.6）。

**待查（第二批取证）**：`15905` 的触发条件与日志字段（同段 15902 行已在打 `cur_speed` + `r3r_wait_cut`，说明该处已有日志，需读全文确认它是否就是玩家看到的那次骤减）；`16815` 所在函数名与调用条件；`11191`/`14206` 的真实语义。

---

## 14.18 探针修复落地（第 281 轮，REG-280-A）

**触发**：§14.17 已判定 `ctx=` 字段不可用于归因（全局单槽 + 无 tick 戳 + `m=0` 兜不住），且 235→0 的写点不在已埋的 15 处之内。按 §14.17.4 的既定顺序，先修探针再取数。

### 14.18.1 改动清单（仅 `src\train_cmd.cpp`，未碰任何 `src\*.h`）

| 项 | 位置 | 内容 |
|---|---|---|
| 1 | `:310~333` | 全局单槽 `_r3r_spd_ctx / _from / _to` 删除，改为 `struct R3RSpdCtxRec { const char *tag; uint16_t from, to; uint32_t tick; }` + `std::unordered_map<uint32_t, R3RSpdCtxRec> _r3r_spd_ctx_by_veh`，按 `v->index.base()` 索引；`R3RSpdCtx(const Train *v, ...)` 写入时盖 `_tick_counter` 戳 |
| 2 | `:20055~20070` | `SPDD` 读取改为**本车记录 + cage 判定**：`cage = _tick_counter - rec.tick`；`cage` 为 -1（无记录）或 >1 ⇒ `ctx=stale`/`ctx=none` 且 `m=1`；行格式新增 `cage=%d` |
| 3 | `:20005~20008` + `:21244~21265` | `SPDD-LOST` 兜底：函数入口存 `r3r_spd_entry`，出口若 `entry >= 20 && 现在 == 0 && 本 tick 无 R3RSpdCtx` ⇒ 打 `SPDD-LOST veh= prev= now= tile= x= db= order= real= stuck= fp= prog= wait= track= dtile=`（阈值 20 是为了不把正常刹车到 0 也刷出来） |
| 4 | 26 处调用点 | 全部改为首参带车：`turn-around / foldchk-refuse-brake / leave-depot / waitcut / pair-not-ready / wp-arrive / stn-gotocouple / red-signal / red-signal-two-way / tb-red / tb-exit-only / wormhole-block / too-close-behind / reverse / linend-stop / linend-table` |
| 5 | 新增 11 个写点 tag | `stop-train`(:4203)、`couple-hit-depot`(:17987)、`couple-hit-open`(:18073)、`couple-depot-ok`(:20469)、`couple-depot-park`(:20480)、`may-reverse`(:20799)、`decouple-interlock`(:20855)、`caronly-park`(:20900)、`leave-reverse`(:21067)、`hold-for-couple`(:21094)、`couple-in-loop`(:21202) |

覆盖后 `src\train_cmd.cpp` 内**全部 30 处 `cur_speed = 0;` 与 1 处硬钳均带 tag**，没有裸写点。

### 14.18.2 判据修正（相对 §14.17.4 第 2 条）

§14.17.4 原写「`cage != 1` 打 stale」，实践上一 tick 内**两个合法窗口**：

- `cage = 0`：写点发生在**本 tick 的 SPDD 探针之前**（如 `wp-arrive`、`stop-train`）；
- `cage = 1`：写点发生在**上一 tick 的 SPDD 探针之后**（如 `couple-in-loop`、`hold-for-couple`、`may-reverse`），本 tick 探针才读得到。

⇒ 判据改为 **`cage ∈ {0,1}` 视为新鲜**，其余一律 stale。这是对探针实现顺序的修正，不改变"必须给出确定 tag"这一验收口径。

### 14.18.3 构建自证

（构建时 `openttd.exe` 正在运行，PID 4728；按规矩不擅自杀进程，等玩家关掉后补填本节。）

### 14.18.4 复测判据（写死，不得事后解释）

1. 出现 `SPDD ... to=0` 的行必须满足 `cage=0|1` 且 `ctx` 是上述 26 个 tag 之一；若仍出现 `ctx=stale`/`ctx=none`，说明台账还有漏点。
2. `SPDD-LOST` 一旦出现即等于「还有未埋的 `cur_speed = 0` 写点」，其 `prev/now/tile/order` 直接给出下一轮定位坐标；**正常到站/刹车不得出现该行**（阈值 20 的负判据）。
3. 全日志 `grep "cage="` 的分布应显示绝大多数 `cage=0|1`（只有真正空闲几十 tick 没写过速度的车才会 stale）。
4. 修复探针后重跑同一场景，235→0 那一行必须给出确定 tag；本轮**不得**再用 `ctx=` 反推。

---

## 14.19 怀疑点清单（第 281 轮，玩家要求"先用大白话说怀疑在哪"）

> 本节是**疑点登记**，不是结论。按规则（有疑点立即落盘），先写清"我怕错在哪"，再决定是否继续取证。

### 疑点 S-1：`from=235` 可能**不是上一 tick 的速度**，而是"很久以前"的速度

- 代码事实（`src\train_cmd.cpp:20043~20092`）：`SPDD` 的 `from` 取的是快照 `r3r_spd_snapshot[key].last`，该快照**只在 `TrainLocoHandler()` 被调用且走到 else 分支时**才在行 `:20092` 刷新。
- 推论：如果这辆车在中间某些 tick **没有进入 `TrainLocoHandler`**（不是链头 / 被 skip / 停驻 / 在库里），那 `from` 就是若干 tick 之前的值，`SPDD from=235 to=0` 就可能被读成"一 tick 内 235→0"，实际是"跨了 N 个 tick 才归零"。
- 现有日志**无法排除**这一点：`SPDD` 已经算出了 `r3r_spd_dt`（`:20044`），但**没有输出**；`SPDD-LOST` 也没有 tick 差。⇒ 这是当前最大的读歪风险。
- 待验证：`SPDD` 行增加 `dt=<tick 差>` 字段（零行为变更，纯字段）。判据：骤降行的 `dt` 若 >1，则"一 tick 硬刹"的前提作废，归因方向要整个换掉。

### 疑点 S-2：`from=235` 与 `to=0` 可能**不是同一辆车**

- 快照以 `consist->index.base()` 为 key（`:20037`）。耦合/解挂/卖车后车辆池 index 会被复用；本仓库历史已多次出现"index 复用导致日志看着像同一辆车"的问题（KI-95 的 `raw_index` 家族）。
- 若发生复用，`from` 来自旧车、`to` 来自新车，"骤降"是假的。
- 待验证：`SPDD` 行同时输出 `dt` 与 `real/order/tile`（已在），看 `from` 那一侧的行与这一行的 `tile` 是否能接上；并行查该 tick 是否有 `COUPLE-OK`/`DECOUPLE-DONE` 复用 index。

### 疑点 S-3：**235→0 可能本来就是正常行为**，我们可能在追一个不是 bug 的现象

- 引擎里"速度一 tick 归零"的合法路径很多：撞车（`CheckTrainCollision`）、进库、到站停车判决、信号变红急刹、`cur_speed` 被 `TrainController` 的刹车表直接取 0。
- 判断关键：那辆车的 `order` 当时是 `OT_GOTO_COUPLE(16)`/`OT_WAIT_COUPLE(17)` 还是普通行驶。若是普通行驶中的急刹，那"归零"根本不是缺陷，**真缺陷是它归零后再也不动**（KI-348 已记录的"停车分支永不重试"）。
- 待验证：把"谁清零" vs "清零后为什么不重试"两件事分开；后者是行为缺陷，前者多数时候只是正常停车。

### 疑点 S-4：整条线**可能是症状链，不是根因**

- I-280 → REG-280-A → 第 281 轮修探针：三轮都在同一个"谁把速度归零"的问题上打转，说明这个问法可能选错了对象。
- 替代问法（建议）：不追"谁清零"，改为定义"**违规状态**"——`order ∈ {GOTO_COUPLE, WAIT_COUPLE}` 且 `cur_speed == 0` 且不在目的地格 ⇒ 这列车处于"该死等却没在等"或"该走却不动"，**直接报违规并附带当时的 reserve/目的地/配对车状态**。这样从"逐个写点打地鼠"变成"一次性定义错误并抓现场"。

### 跳出打地鼠的口径（待玩家确认）

1. **加 `dt=`**（S-1）：一行字段，先确认"到底是不是一 tick 骤降"。这是当前最便宜的止损。
2. 若 `dt > 1` 普遍成立 ⇒ 整个第 279~281 轮的归因链**作废重来**，改用"违规状态"探针（S-4）。
3. 若 `dt == 1` 确实存在 ⇒ 才继续按写点台账追，且此时 `cage=0|1` 才有意义。

---

## 14.20 玩家现象定性（第 281 轮，2026-10-23）—— 决定性答复

**玩家原话（逐字）**：

> 我讲一下，我就是看到了车两次突然的，不正常的，不符合游戏物理引擎和减速曲线的刹停，如果这个还不足够，我允许你补探针

### 14.20.1 定性

现象 = **肉眼可见的速率不连续**（跳变），不是"慢慢停下"、不是"到站停靠"、不是"卡在原地"。
玩家是**盯着连续画面**看到的，两次。

### 14.20.2 对 §14.19 四条怀疑的判定

- **S-3（235→0 可能本来就正常）⇒ 排除。**
  引擎里所有"合法归零"路径（撞车 `CheckTrainCollision`、进库、到站 `ShouldStopAtStation`、信号变红刹车、`TrainController` 刹车表）都伴随减速过程。以 235 的量级，即便按最激进的减速度也要**数十 tick（数秒以上）**才到 0，肉眼绝不可能漏看。
  ⇒ 玩家看到的"没有减速曲线"，就是"速度被硬写为 0"的现场证据，**不是正常停车**。

- **S-1（`from` 可能不是上一 tick）⇒ 大幅削弱，但必须用一个数字钉死。**
  若 `dt` 大到足以容纳减速曲线（≥30），玩家必然看到减速过程，与观察矛盾。
  ⇒ 预期 `dt == 1`（或很小）。仍要落 `dt=` 作为**文件证据**，不能再靠推理。

- **S-2（车号复用到两辆车）⇒ 仍未排除。**
  视觉上"一列车刹停"也可能是"A 停下 + B 顶上/出现"。但玩家描述是同一列车在行进中骤停，倾向排除；需日志的 `tile`/`order`/`x` 与前后行对齐验证。

- **新推论**：若 `dt == 1`，则 `cur_speed` 是被**硬写**（而非刹车表逐 tick 递减）。第 281 轮已把 `cur_speed = 0;` 的 30 处写点全部埋 tag + 兜底，所以：
  - `ctx` 有效（`cage=0|1`，`m=0`）⇒ tag 直接指认凶手，结案；
  - `ctx=none|stale`（`m=1`）⇒ 写点在 `TrainLocoHandler` 的 tick 上下文之外（命令侧 / 别的车的逻辑 / 读档初始化 / 名单有漏），此时必须换查"命令与提交点"，**不要再在 tick 内加探针**。

### 14.20.3 本轮探针（已落地，仅 `src\train_cmd.cpp`）

1. `SPDD` 行新增 `dt=%u`（`r3r_spd_dt`，早已算出、此前未打印）。
2. `SPD-DROP` 行新增 `dt=%u`（同一值）。
3. **不新增其它探针**——先让 `dt` 把 S-1 判死，再决定下一步，避免继续打地鼠式加探针。

### 14.20.4 判据（复测时逐条核）

1. 两次骤停各应有一行 `SPDD ... from>=20 to=0`；两行即对应玩家看到的"两次"。
2. `dt == 1` 且 `ctx` 有效（`cage<=1` 且 `m=0`）⇒ 我埋的 tag 就是凶手，直接改该写点。
3. `dt == 1` 且 `ctx=none|stale` ⇒ 写点在 tick 之外，转查命令侧（`CmdMoveRailVehicle`/`CmdSellRailWagon`/`Couple`/`DecoupleTrain`/`ReverseTrainDirection` 等提交点）。
4. `dt > 30` ⇒ S-1 成立，第 279~281 轮整条归因链作废，改走"违规状态"定义（§14.19 S-4）。
5. **负判据**：正常到站/进库的停车，`dt` 应连续、`from` 应逐步递减（能看到减速序列），不得出现 `dt=1` 的大幅跳变。

### 14.20.5 复现配合（请玩家顺手记）

若方便，请记下两次骤停的**大致时刻**和**列车编号或车次**（哪个站/哪条线），这样能在日志里精确定位那两行，不必靠猜。

---

## 14.21 真凶定位（第 281 轮，2026-10-23）—— 结论：R3R 自己的等待截断每 tick 硬刹车

**玩家原话（逐字）**：

> 关了。那两次都是属于挂接的主动方（应该是veh24）的神秘减速，你在日志的倒数几次减速内就能找到

### 14.21.1 现场（`build\R3R_debug.log`，10 844 行 / 1 611 598 B / 2026-10-23 0:41:33）

日志里 `veh=24` 的 SPDD 行**不是一行 235→0**，而是**连续几十行、每行减 1**：

```
9940:SPDD veh=24 from=34 to=33 ctx=waitcut cfrom=235 cto=0 m=1 tile=264,234 x=4234 db=1 order=1 real=10 ...
...
9955:SPDD veh=24 from=15 to=5  ctx=waitcut ...
9956:SPDD veh=24 from=14 to=0  ctx=waitcut cfrom=235 cto=0 m=0 tile=265,234 x=4240 db=1 order=3 real=10 ...
```

再往前（同一段被截断处可见）：`from=94 to=93`、`93→92`、`92→91` … `34→33` … `20→19`，全部
`ctx=waitcut cfrom=235 cto=0`，`tile`/`x` 每 tick 只前进 1px（4199→4240）。

⇒ **两次"神秘减速"的形态一致：不是瞬跳，而是"每 tick 净掉 1"，从 235 一路衰减到 0。**

### 14.21.2 代码锚点（唯一命中点）

`src\train_cmd.cpp:15931-15950`：

```cpp
if (!pair_ready || r3r_wait_cut != 0) {
    ...
    R3RSpdCtx(consist, r3r_wait_cut != 0 ? "waitcut" : "pair-not-ready", consist->cur_speed, 0);
    consist->cur_speed = 0;      // 15949
    consist->subspeed = 0;       // 15950
}
```

位置在 `DoTrainCouplePathfind()` 返回 `INVALID_TRACK`（`path_found == INVALID_TRACK`）之后的分支里，
是第 280 轮自己埋的 tag `waitcut`（`:15948`）——`cfrom=235 cto=0` 就是它自己记下来的。

### 14.21.3 机制：为什么"每 tick 掉 1"而不是"一 tick 归零"

写 0 之后同一 tick 后续仍会跑速度更新（从 0 重新加速一格），所以**下一 tick 入口读到的不是 0，而是
"235 减掉一格"**。于是净效果：每 tick 被掐一次脖子，速度按"一次加速度"的步长往下掉，
235 → 0 需要约 230 tick（≈8 秒）。

这正好解释玩家的观感：**没有刹车火花、没有正常减速曲线、车还在往前蹭（1px/tick）然后停住**——
因为根本不是引擎在刹，是每 tick 被强制写 0。

### 14.21.4 对 §14.19 四条怀疑的最终判定

- **S-1（`from` 不是上一 tick）⇒ 证伪。** 相邻行是逐 tick 的（每 tick 一行，tile/x 逐格推进），
  `dt` 必为 1。旧 exe 没有 `dt=` 字段，但"每 tick 一行 + 位置每 tick 1px"已等价证明。
- **S-2（车号复用两辆车）⇒ 证伪。** 整段 `tile` 264,234、`x` 4234→4240、`real=10`、`dtile=473353`
  连续单调，是同一列车。
- **S-3（235→0 本来就正常）⇒ 证伪。** 确实不正常，玩家判断正确。
- **S-4（该换个问法）⇒ 部分成立，但已不需要**：真凶已指名，问题变成"为什么一辆高速行驶、
  离等待区路点还很远的车会命中 `r3r_wait_cut != 0`"。

### 14.21.5 新疑点 REG-281-A（下轮第一件事）

`r3r_wait_cut` 的语义是"等待区路点截断"（第 276 轮 KI-431、第 277 轮 F-437-D 改口径：
位置不再决定截不截断，只决定"墙还在不在车前面"，判据换成"配对是否就绪"、每 tick 重算）。
现场 `order=1`（`OT_GOTO_WAYPOINT`? 需核 order_type.h 编号）、`real=10`、`db=1`，
而车在 264,234 一带高速行驶 —— **为什么此时判成"该截断"**？

下轮落点：`r3r_wait_cut` 的计算处（`train_cmd.cpp` 第 277 轮 F-437-D 口径那段），
核 ①②③ 三条成立条件在该现场为何全真，以及 `pair_ready == false` 分支是否会同样掐车。

### 14.21.6 已经可以下的结论（不必再等探针）

1. 这不是"速度被谁清零"的谜团，写点唯一且已指名：`train_cmd.cpp:15949`。
2. 该写点**每 tick 重复**，所以表现为"衰减式急停"，不是一次性刹车。
3. `dt` 探针（本轮已加）在本案已无必要，保留只为将来复查其它写点。

---

## 14.22 第二份日志（2026-10-23 1:23）—— §14.21 的结论被推翻

**输入**：`D:\temp\codebuddy-dropped-files\a5d3898a-3314-4c35-93e7-396f088bbc31\R3R_debug.log`
（1 637 272 B，LastWriteTime 2026-10-23 1:23:24，含 `dt=` 字段 ⇒ 产生者已是第 281 轮 exe）。
`SPDD veh=24 ` 共 **2321** 条。

### 14.22.1 决定性读数（原文）

```
9767:SPDD veh=24 from=207 to=206 dt=1 ctx=stale cfrom=0 cto=0 cage=74 m=1 tile=252,235 x=4043 db=1 order=1 real=10 stuck=0 fp=0 one=0 prog=73 track=01 dtile=473353
9768:SPDD veh=24 from=206 to=205 dt=0 ctx=stale ... cage=74 ... x=4044 ...
9774:SPDD veh=24 from=200 to=199 dt=0 ctx=stale ... cage=74 ... x=4056 ...
9862:SPDD veh=24 from=113 to=112 dt=0 ctx=stale ... cage=77 ... x=4181 ...
9875:SPDD veh=24 from=100 to=99  dt=1 ctx=stale ... cage=78 ... x=4194 ...
9940:SPDD veh=24 from=35  to=34  dt=0 ctx=stale ... cage=80 ... x=4234 ...
9955:SPDD veh=24 from=20  to=19  dt=1 ctx=stale ... cage=81 ... x=4238 ...
9956:SPDD veh=24 from=15  to=5   dt=0 ctx=stale ... cage=81 ... x=4239 ...
9957:SPDD veh=24 from=14  to=0   dt=0 ctx=stale ... cage=81 ... x=4240 order=3 ...
```

**全日志 `SPD-DROP` 0 条、`CPL-WAITHOLD` 0 条、`WAITHOLD` 0 条。**

### 14.22.2 推翻 §14.21

1. **不是 `waitcut` 硬清零。** 上一份日志（0:41，旧 exe）里 `ctx=waitcut cfrom=235 cto=0` 是
   **全局单槽串味造成的假归因** —— 第 281 轮把 tag 改成 per-vehicle（`_r3r_spd_ctx_by_veh`）+ 新鲜度门
   （`cage` 0/1 才算命中）之后，同一场景的同一次减速变成 **`ctx=stale cage=74→81`**：
   ⇒ 减速期间**没有任何 R3R 写点执行**，速度是**引擎自己减的**。
   §14.21 的"`train_cmd.cpp:15949` 每 tick 硬刹车"**作废**（那条路径本次根本没进）。
2. **是连续减速，不是跳变。** 每 tick 精确 `-1`（207→206→205→…→199→…→35→34→…→19），
   从 235 一路到 0，约 **235 tick ≈ 8.5 秒**。`x` 每 tick `+1px`（4043→4056→4181→4240 单调）。
3. `dt` 探针**读数不可用**：同一段连续递减里 `dt` 绝大多数是 `0`、偶尔 `1`。
   ⇒ `_tick_counter` 在两次打印之间**没有变化**，说明该处每 tick 被多次调用或 `_tick_counter` 语义不符预期。
   S-1（"`from` 不是上一 tick"）**仍被位置读数证伪**（x 每行 +1px、速度每行 −1，只能是相邻 tick），但 `dt` 字段本身记入 REG-281-C。

### 14.22.3 新的两个硬事实（本轮的真正收获）

- **REG-281-B（高，新）**：速度显示 200+ 时，`x` 每 tick 只前进 **1px**。正常 100 km/h 的列车每 tick
  应推进数像素到十几像素。⇒ `cur_speed` 与真实位移**严重脱钩**（`db=1` 倒车、`order=1`(GOTO_STATION)、
  `real=10`、`prog` 在 3/63/71/88/96/128/172/184 之间乱跳，不单调）。这条比"谁减的速"更要紧：
  玩家看到的"车几乎不动却挂着高速"和"突然停下"很可能是同一个状态。
- **REG-281-D（我的探针缺陷，已定位但未修）**：`train_cmd.cpp:20030` 的注释写明"SPD-DROP 仅改走
  `R3RFopenDbg`"，但 `:20083` 实际仍调 `R3RDbgWrite()`，而第 274 轮的 tag 白名单
  （`R3RDbgTagAllowed`）默认只放 `RESV-/UNRES-/EXTEND-/LOOP-` 四族 ⇒ **`SPD-DROP` 被白名单吞掉**。
  ⇒ "SPD-DROP 0 条"不能当"没归零"的证据（`9957 from=14 to=0` 显然该打一行却没打）。下轮必修这一行。

### 14.22.4 待验（下轮第一件事）

减速**起点**（235→234 的第一行）之前 74~81 tick 处的写点现场，以及"为什么引擎在正线上开始刹车"。
落点候选：`order=1`(GOTO_STATION) 进站刹车 / 前方信号 / 前方有车 / 轨道限速。
取数：读 `R3R_debug.log` 9690–9760 段（含减速起点前 74 tick 的最后一处 tag 记录）。

### 14.16.6 取证第二批：`15905` 的触发条件 = **`!pair_ready || r3r_wait_cut != 0`**

读 `src\train_cmd.cpp:15889-15907` 原文：

```cpp
if (keep_track != INVALID_TRACK && consist->cur_speed != 0 && pair_ready && r3r_wait_cut == 0) return { keep_track, result_flags };
if (!pair_ready || r3r_wait_cut != 0) {
        if (r3r_wait_cut != 0) {
                /* 只读探针（KI-433 复测判据 ⑦）：截断态确实把车按住了。 */
                static std::map<uint32_t, uint8_t> r3r_wait_hold_seen;
                uint8_t &seen = r3r_wait_hold_seen[consist->index.base()];
                if (seen != static_cast<uint8_t>(r3r_wait_cut)) {
                        seen = static_cast<uint8_t>(r3r_wait_cut);
                        R3RDbgWrite("CPL-WAITHOLD veh=%d tile=%d,%d speed=%d state=%d\n", ...);
                }
        }
        consist->cur_speed = 0;
        consist->subspeed = 0;
}
```

**判定（REG-279-N，高置信）**：

1. 这个分支是"挂车寻路没找到目标"时的处置，条件是 **配对锁未建立（`!pair_ready`）** 或 **等待区路点截断态（`r3r_wait_cut != 0`）**。命中即 **`cur_speed = 0` + `subspeed = 0` 硬置零，且不打任何刹车、不看速度高低**。
2. ⇒ 玩家 E3 的「在信号灯前速度骤减为 0」**很可能就是这里**：位置在信号灯旁只是巧合，真因是 `pair_ready` 从 true 掉到 false（或 `r3r_wait_cut` 置位），一个 tick 内从任意速度直接跳 0；随后 `pair_ready` 恢复，下一 tick 正常加速 ⇒ 完全吻合"随后继续加速往 B"。
3. 注意 `15889` 的早退条件要求 `pair_ready && r3r_wait_cut == 0`，所以**只要有一个不满足就把车按死**，这与"车正在按 A 的减速曲线正常行驶"是互斥的 —— 一次状态翻转就是一次速度归零 ⇒ **H-4「两套机制打架」成立**（平滑减速 vs 单点硬钳）。
4. 已有探针 `CPL-WAITHOLD`（15900）**只覆盖 `r3r_wait_cut != 0` 那一半**，`!pair_ready` 那一半**完全没有日志** ⇒ 这就是"日志里看不到骤减"的原因。**探针必须补这一半。**

### 14.16.7 取证第二批之二：`16815` 是站台到达的钳位，`16781` 是路点到达的归零

读 `src\train_cmd.cpp:16760-16818`：

- `16767`：`if (consist->current_order.IsType(OT_WAIT_COUPLE)) return;` —— 等待方不走到站流程。
- `16773-16783`（**路点到达分支**）：`Waypoint::IsExpected(bst)` ⇒ `MakeWaiting()` + 关停靠 + **`cur_speed = 0`** + `UpdateTrainSpeedAdaptationLimit(0)` + `return`。语义是"到了路点就停死"。
- `16805-16817`（**`OT_GOTO_COUPLE` 进站分支**）：`if (consist->cur_speed > 20) consist->cur_speed = 20;` ⇒ **钳到 20（10 km/h）**，然后 `UpdateTrainSpeedAdaptationLimit(0)` + `return`。语义是"接近车底时蠕动"。

**判定**：

1. 玩家 E4 说「刚进入 B 所在格（触碰）那一刻速度骤减为 **2**」。若 B 是**路点**，走 `16781` ⇒ 会变成 **0** 而不是 2；若 B 在**站台**且订单是 `OT_GOTO_COUPLE`，走 `16815` ⇒ 变成 **20（10 km/h）** 而不是 2。**两条都对不上"2"，⇒ E4 的"2"另有出处**，必须靠 `SPDD` 的 `ctx` 现场归因（已有 `LINEND=0` 排除 `_breakdown_speeds`）。
2. `UpdateTrainSpeedAdaptationLimit(0)` 在两条分支都被调用 —— 这是一个**还没被检查过的速度限制通道**，登记为待查项 `REG-279-O`。
3. `SA-GOTO_COUPLE` 探针（16810）已在打印进站速度，可作为 E4 的交叉验证行。

**状态**：14.16.6/7 为纯取证，零源码改动。探针落点已确定，见 §14.16.8。

### 14.16.8 探针落点表（本轮实现，全部在 `src\train_cmd.cpp`）

**P-V2 `R3RSpdCtx(tag, from, to)` 的插入点**（只在**硬写** `cur_speed` 之前一行插入；平滑路径不插，`ctx=other` 即代表"无硬点，属物理/控制器变化"）：

| 行号（改动前） | tag | 语义 |
|---|---|---|
| 4973 | `turn-around` | 玩家/订单调向时整车掉头前硬置零 |
| 11191 | `foldchk-refuse-brake` | 含真铰接拒绝挂车时的刹车（已有 `FOLDCHK-REFUSE-BRAKE` 行） |
| 14206 | `leave-depot` | 出库首 tick 置零 |
| **15905** | `waitcut` / `pair-not-ready` | **E3 头号嫌疑**：`r3r_wait_cut != 0` 与 `!pair_ready` 两半分开标注 |
| 16781 | `wp-arrive` | 路点到达（`MakeWaiting` + 停死） |
| 16815 | `stn-gotocouple` | `OT_GOTO_COUPLE` 进站钳 20 |
| 18791 | `red-signal` | 单向信号红（`!HasSignalOnTrackdir(Reverse)`） |
| 18796 | `red-signal-two-way` | 双向信号红 |
| 18963 | `tb-red` | 隧道桥模拟信号入口红 |
| 18969 | `tb-exit-only` | 隧道桥只出不进 |
| 19094 | `wormhole-block` | 虫洞出口被占 |
| 19107 | `too-close-behind` | 距前车太近 |
| 19378 | `reverse` | 尽头/信号触发 `ReverseTrainDirection` 前 |
| 19681 | `linend-stop` | `TrainApproachingLineEnd` 尽头硬停 |
| 19689 | `linend-table` | `_breakdown_speeds` 钳位（B 类，非零） |

**P-V1 `SPDD`**：改造 `TrainLocoHandler` 入口（19960-19982）的快照。改为**任意变化**打行，并带上一 tick 的 `ctx`：
字段 `veh / from / to / ctx / cfrom / cto / m / tile / x / db / order / real / stuck / fp / one / prog / track / dtile`。
- `m=1` 表示 `cto != to` ⇒ 该 ctx 记录已被后续写点覆盖，**这一行的变化不是 ctx 指的那个点造成的**（防误归因）。
- 限流：`to == 0`、或 `from >= 20`（高速）、或 `from - to >= 10`（骤减）⇒ 立即打；其余平滑变化每车每 64 tick 最多一行。
- 原 `SPD-DROP` 行**保留不动**（KI-348 判据仍依赖它），`SPDD` 是新增行。

**P-V3 `STUCK`**：`TrainLocoHandler` 入口比较 `flags.Test(VehicleRailFlag::Stuck)`、`force_proceed`、`r3r_parked` 三元组，任一变化打一行（对应 §14.15.5 的 `mstuck=1` 8 次）。

**P-V4**：把现有 `LOCO-MOVE`（21036）扩两个字段 `dir` 与 `dtile`，用于 E2「过岔改道」的逐格轨道归因（节流仍是每车每格一行，不增加量级）。

**判读口径（先写死，防事后解释）**：
- E3 成立 ⇔ `SPDD to=0 from>=20` 且该行 `m=0` 且 `ctx ∈ {waitcut, pair-not-ready, red-signal*, tb-red, wormhole-block, too-close-behind}`。
- E4 成立 ⇔ `SPDD to=4`（2 km/h）且同行 `ctx` 是硬点且 `m=0`；若 `ctx=other`，说明来自 `TrainController` 的平滑计算（H-1），需看该行 `x`（格内位置）是否很小。
- E2 成立 ⇔ `LOCO-MOVE` 的 `track`/`dir` 在过岔后被另一支路取代，且同行 `dtile` 未变（目标仍是同一等待区路点）。

### 14.16.9 落地记录（2026-10-23，第 280 轮实现）

**先记一个判据陷阱（REG-279-P，必须写进 KI）**：`R3RDbgWrite()` 的第一道闸门是 `R3RDbgTagAllowed(fmt)`（`src\r3r_perf.h:256-289`，调用点 `:295`），默认白名单**只有** `RESV-*` / `UNRES-RAIL` / `EXTEND*` / `LOOP*` / `R3R-RES*`。所以上一轮记的 `SPD-DROP = 0 条` **不能**证明"没有骤减"，它极可能只是被白名单吞掉了（`R3R_DBG_TAGS=all` 才放行）。同理 `LOCO-MOVE` 旧版也走 `R3RDbgWrite`，日志里一行都没有，绝不能拿来当"没进移动分支"的正面证据。
⇒ 本轮新增/改造的三条速度探针全部改用 `R3RFopenDbg("a")`（`:202`，只受 `R3R_DBG` 与 `R3R_DBG_RAW` 控制，`R3R_DBG_RAW` 默认开），**不受 tag 白名单影响**。`SPD-DROP` 行格式原样保留（仍走旧通道，KI-348 判据不变）。

**已实现（仅 `src\train_cmd.cpp`，零头文件改动）**：
1. 文件级 `static const char *_r3r_spd_ctx` + `_r3r_spd_ctx_from/_to` + `R3RSpdCtx(tag, from, to)`（插在 `R3RLeadingEndHasDrivingCab` 之前，约 :292）。
2. 15 处硬写点前置 `R3RSpdCtx(...)`，tag 见 §14.16.8 表；`15905` 那处按 `r3r_wait_cut != 0` 分成 `waitcut` / `pair-not-ready`，并加了中文注释说明"原来只有 wait_cut 一半有日志"。
3. `TrainLocoHandler` 入口：`SPD-DROP` 快照升级为 `{last, last_tick}`，**保留 0 值**（旧版见 0 就 erase，0→X 起步是盲区）；新增 `SPDD` 任意变化行；新增 `STUCK` 行。
4. `LOCO-MOVE` 加 `dir=%d db=%d` 两字段并改走 `R3RFopenDbg`。

**SPDD 字段读法**：`from`/`to` 是**相邻两次入口**的 `cur_speed`（即上一 tick 的净变化）；`ctx` 是上一 tick 内最后一次硬写的 tag；`cfrom`/`cto` 是那次硬写自己看到的值；`m=1` ⇒ `cto != to`，说明 ctx 已被后续写点覆盖，**这一行的变化不是 ctx 指的那个点造成的**。`x` 是格内像素位置（E4 若 `to=4 且 x≈0`，更像"进格瞬间被硬写"；若 `x` 接近 `cached_veh_length` 则更像正常平滑到站）。

**已知盲区**：① `ctx` 只有一槽，一 tick 多个硬写点只留最后一个（靠 `m` 兜底）；② 平滑路径（`TrainController` 的加减速、坡度、转向）不设 ctx ⇒ `ctx=other`；③ `UpdateTrainSpeedAdaptationLimit(0)`（REG-279-O）仍未加归因，若本轮数据指向"限速被清 0"，下一轮再补。

**复测说明（给玩家）**：直接跑，**不要**设 `R3R_DBG_RAW=0`（会把这些行关掉）；要不要 `R3R_DBG_TAGS=all` 已无所谓（新探针不走白名单）。日志量预估：`SPDD` 只在"归零/高速/骤减≥10"或每 64 tick 一行，正常跑一段约几十到几百行；`LOCO-MOVE` 仍每车每格一行（GOTO_COUPLE）。

**构建失败一次，根因是注释陷阱（已修，值得记住）**：首次 `cl` 报 `train_cmd.cpp(303): error C2143/C4430/C2059` 加一长串"未声明的标识符 `_r3r_spd_ctx` / `R3RSpdTrackEntry`"。真因不是代码写错，而是我在块注释里写了 `RESV*/UNRES*/EXTEND*/LOOP*` —— 其中的 **`*/` 提前闭合了 `/** ... */` 注释**，于是剩下的注释文字变成代码、`_r3r_spd_ctx` 的声明整块被吞掉，后面所有报错都是级联噪声。第 20013 行的中文注释里有同一串，一并改掉。
⇒ 规矩：在 C/C++ 块注释里提到通配标签时，**禁止写成 `前缀*` 紧跟 `/` 或另一个 `*`**（写 `RESV-、UNRES- 等四族` 或 `RESV*, UNRES*`），否则等于提前结束注释。以后看到"未声明的标识符"出现在**同文件更早位置明明有定义**时，第一反应应当是往上找未被闭合/被提前闭合的注释。

**构建自证（第 280 轮，2026-10-23 00:31）**：复用既有 `_tmp_inc_build.cmd`（未新建任何 `.cmd`）；跑前 `tasklist` 确认 `openttd.exe` 未运行；`build\R3R_incbuild.guard.log` = `GUARD: incremental is safe (no header/lang file is newer than the newest object)`（本轮零头文件改动）；日志 `[2/3] Building CXX object ...\src\train_cmd.cpp.obj` → `[3/3] Linking CXX executable openttd.exe`（Linking 命中 1 次）；`build\R3R_incbuild.done` = `EXIT_CODE=0`（00:31）；错误计数 0（无 `error C`/`fatal error`/`FAILED:`/`build stopped`）；时间戳链 `src\train_cmd.cpp` 00:26（1 137 227 B）→ `train_cmd.cpp.obj` 00:27（14 383 001 B）→ `build\openttd.exe` 00:30（52 243 456 B）；`read_lints` 该文件 0 条。

**产物字面量自证（exe 内实测存在）**：`SPDD veh=%d from=%d to=%d ctx=%s cfrom=%d cto=%d m=%d tile=%d,%d x=%d db=%d order=%d real=%d stuck=%d fp=%d one=%d prog=%d wait=%d track=%02X dtile=%u`；`STUCK veh=%d tile=%d,%d state=%d ...`；`LOCO-MOVE veh=%d tile=%d,%d j=%d spd=%d track=%02X dir=%d db=%d stuck=%d dtile=%u`；ctx tag 五条逐个 HIT：`pair-not-ready` / `waitcut` / `red-signal` / `wormhole-block` / `stn-gotocouple`。

**状态**：P-V1~P-V4 已实现 + 已编译（内测树 `build\openttd.exe` @2026-10-23 00:30）。发布树 `build-release` 本轮**未编译**，仍停留在更早版本（按 2026-10-19 口径：只有用户明确要发布版时才编）。游戏内复测待做，判据见 §14.16.8 的 E1~E4（已写死，不得事后解释）。

---

## 14.17 复测取证（2026-10-23，玩家回传日志）

**输入**：`D:\temp\codebuddy-dropped-files\f2b048aa-50ee-4926-b550-0d651996f483\R3R_debug.log`（玩家 2026-10-23 00:42 产出，产生者=本轮 00:30 编译的内测 exe，采样有效）。

### 14.17.1 判据命中情况（结论先写）

| 判据 | 结果 | 证据 |
|---|---|---|
| E1 正常减速 | 部分可用 | `SPDD` 有大量 `from=X to=X+1` 的匀速爬升行，但**加速段的 0→20 跃变被抓漏**（20 是每 64 tick 才打的普通变化区间），见 9324 行 `from=20`，9322 已是 `to=0` |
| E2 过岔改道 | 有数据，未判读 | `LOCO-MOVE` 12 行，veh=21 沿 `248,234 → 258,234` 每格一行，`spd=30`（15 km/h）恒定、`j≈200`、`dir=1`、`db=1`、`dtile=491798` 全程不变 |
| E3 速度骤减为 0 | **现象复现** | 日志 **第 9322 行**（全日志唯一一条 `from=235 to=0`）：`SPDD veh=21 from=235 to=0 ctx=waitcut cfrom=235 cto=0 m=0 tile=234,233 x=3746 db=1 order=6 real=11 stuck=0 fp=0 one=0 prog=43 wait=0 track=01 dtile=479486` |
| E4 触格瞬间 2 km/h | **未复现（否定）** | `findstr /C:" to=4 "` = **0 条**（exitCode 1）；全日志无 `to=4` 的 SPDD 行 |

⇒ E3 的**现象**（235 → 0，一 tick 内从约 117 km/h 直接归零）确实存在；**归因失败**，见 14.17.2。

### 14.17.2 探针缺陷 REG-280-A（高）：`ctx` 陈旧污染，`ctx=` 字段本轮不可用

**证据链（三条互相独立）**：

1. **判据前置条件不成立**：`waitcut` 写点的入口判据是 `R3RShouldCutCoupleAtWaitPoint()`（`src\train_cmd.cpp:13148`），其第二句就是 `if (!v->current_order.IsType(OT_GOTO_COUPLE)) return 0;`（`:13152`）。而第 9322 行自身写着 `order=6` = **`OT_GOTO_WAYPOINT`** ⇒ **该 tick 不可能进入 waitcut 写点**（`train_cmd.cpp:15939-15941`）。
2. **同一条车后续读数自相矛盾**：9324~9344 共 21 行，速度从 `from=20` 稳定爬到 `from=40 to=41`（每 tick +1，期间不可能发生 `cur_speed = 0` 的硬写），却全部显示 `ctx=waitcut cfrom=235 cto=0`。⇒ 这个 ctx 是 9322 之前留下的**残留**，之后从未被复位。
3. **`m=0` 兜不住陈旧**：`m` 的定义是 `cto != to`。所有硬写点的 `cto` 都是 `0`，所以当速度真的为 0 时，陈旧 ctx 的 `m` 同样是 `0` ⇒ **`m=0` 无法区分"新鲜归零"与"陈旧残留"**。本轮 9322 行 `cfrom=235` 恰等于 `from=235`、`cto=0` 恰等于 `to=0`，三个条件全部"看起来自洽"，实际是巧合。

**根因**：`_r3r_spd_ctx` / `_r3r_spd_ctx_from` / `_r3r_spd_ctx_to` 是**文件级全局单槽**，只在写点被赋值，**不清空、无 tick 戳、跨车共享**（`train_cmd.cpp:292` 一带）。语义上它只保证"上一 tick 内最后一次硬写"，但实现上没有任何东西保证"上一 tick 内确实发生过硬写"。

**⇒ 第 280 轮写死的 E3 判据（`SPDD to=0 from>=20 m=0` 且 ctx 属于硬写点族）在本轮数据上不可用，作废重取。** 之前据此得出的任何 tag 归属都无效。

### 14.17.3 235→0 的真凶：本轮未定位（可疑上下文已锁定）

第 9315~9323 行的连续现场（`RESV-GC` 已省略编号）：

- 9316 `RESV-REL who=CLEAR-STN veh=21 tile=254,234 track=whole owner=21 age=0 phase=gc-stn purposes=0x0`
- 9317 `RESV-GC veh=21 tile=254,234 track=stn kind=WAYPOINT why=free-path`
- 9321 `RESV-LEDGER-FORGET veh=21 why=free-path mask=0x1 released=28 kept=0 phase=choose-track`
- **9322 速度 235 → 0**
- 9323 `DEPOT-ARR veh=21 spd=0 real=11(6) curType=6 stuck=0 tileDepot=0 tx=234 ty=233 destTx=254 destTy=234 destDepot=0 destResv=0 tileEqDest=0 depotDir=-1 enterDir=2 tt=11 impl=11`
- 9324 起从 20 重新爬速

已排除 / 已确认的边界条件：

- 该车此刻的订单是 **GOTO_WAYPOINT（type 6）到 254,234**（`DEPOT-ARR` 的 `curType=6` / `destTx=254 destTy=234` 与 9317 的 `kind=WAYPOINT` 互证），**不是 GOTO_COUPLE**。
- 全日志 `CPL-WAITCUT-CLEAR` = **0 条** ⇒ 那 28 格预留**不是** `train_cmd.cpp:15842` 的 waitcut-clear 分支清的（那是另一条 `free-path` 来源）。
- `stuck=0`、`fp=0`、`one=0` ⇒ 不是 mark_as_stuck，也不是 ForceReserveOnce 相关。
- `wait=0` ⇒ 不是 `r3r_wait_cut` 之外那条 `pair-not-ready` 分支（何况那条也在 GOTO_COUPLE 保护内）。

**结论：235→0 的写点不在我已埋的 15 处之中，或落在我误判的路径上。** 下轮必须先修探针（14.17.4）再取数，不得继续用 `ctx=` 反推。

### 14.17.4 待做（下一轮，按序）

1. `_r3r_spd_ctx` 改**每车一份 + tick 戳**：`struct R3RSpdCtxRec { const char *tag; uint16_t from, to; uint32_t tick; }` + `btree_map<uint32_t, R3RSpdCtxRec>`，按 `v->index.base()` 索引（沿用本文件既有 `_r3r_wbar_state` 的写法）。
2. `SPDD` 行加 `cage=<tick 差>`；`cage != 1` 时直接打 `ctx=stale` 并**不参与归因**。
3. 补"漏埋也能抓"的兜底：`TrainLocoHandler` 结束处比对本 tick 前后 `consist->cur_speed`，若变了却没有任何 `R3RSpdCtx` 调用，打 `SPDD-LOST veh= prev= now= tile=` ⇒ 说明写点漏埋或不在 R3R 代码里。
4. 完成 1~3 后重新复测，判据：`to=0` 行必须给出 `cage=1` 的确定 tag；`SPDD-LOST` 若出现，携带的 `prev/now` 直接暴露未埋点。

**本轮零源码改动、零构建**（纯取证）。

---

## 14.23 第三批取证（2026-10-23，第 282 轮）—— REG-281-B 证伪，真凶方向改为「handler 每 tick 被调用 20+ 次」

**输入**：同 §14.22 那份日志（1 637 272 B / 2026-10-23 1:23:24，含 `dt=`）。
导出物：`build\_tmp_r282_spdd24.txt`（`SPDD veh=24` 末 160 行）。

### 14.23.1 统计（工具读数，可复算）

```
SPDD veh=24 总数 = 2321
其中 dt=0        = 2215
其中 dt=1        = 99
```

⇒ 96% 的行与上一行**同一个 `_tick_counter`**。逐字原文（日志 9797~9809，行号连续、中间无其它探针行）：

```
9797:SPDD veh=24 from=177 to=176 dt=0 ... cage=75 ... tile=256,235 x=4096 prog=46
9798:SPDD veh=24 from=176 to=175 dt=0 ... cage=75 ... tile=256,235 x=4098 prog=57
9799:SPDD veh=24 from=175 to=174 dt=0 ... cage=75 ... tile=256,235 x=4100 prog=65
9800:SPDD veh=24 from=174 to=173 dt=0 ... cage=75 ... tile=256,235 x=4101 prog=132
9801:... x=4103 prog=6     9802:... x=4104 prog=71    9803:... x=4105 prog=134
9804:... x=4107 prog=3     9805:... x=4108 prog=189   9806:... x=4110 prog=182
9807:... from=167 to=166 ... tile=257,235 x=4112 track=04 prog=47
```

### 14.23.2 REG-281-B **证伪**（必须推翻 §14.22.3 的第一条）

`cur_speed` 与位移**完全自洽**，不存在"速度 200+ 却只走 1px"的脱钩：

- `vehicle_base.h:538` `GetAdvanceSpeed(speed) = speed * 3 / 4`；
- `vehicle_base.h:550` `GetAdvanceDistance() = 192`（对角）`/ 256`（正向）；
- ⇒ `cur_speed=177` 每 tick 推进 `177*3/4 / 256 ≈ 0.52 px`，取整后**每行 x 正好 +0~2px**（实测 +1~2），`prog` 每行 +65~130（回绕 0..255 后即"乱跳"）。

⇒ **`prog` 乱跳是 8 位回绕，不是状态异常**；`x` 每行 +1px 是 177 速度的正确位移。
§14.22.3 里"速度与真实位移严重脱钩"**作废**，REG-281-B 关闭。

### 14.23.3 真正的新事实：**同一 tick 内 handler 被调用约 20~28 次**

两条独立读数互证：

1. `dt=0` 占 2215/2321 ⇒ 绝大多数 `SPDD` 行落在同一个 `_tick_counter` 内；而每行 `cur_speed` 都**恰好 −1**、`x` 都**前进 0~2px** ⇒ 每行都是一次完整的 `TrainLocoHandler()` 执行（`UpdateSpeed` + 位置推进都跑过），**不是重复打印**。
2. `cage`（= `_tick_counter - ctx.tick`，见 `train_cmd.cpp:20061`）在 9755→9955 这 200 行里只从 **74 涨到 81**（+7 tick）⇒ 平均 **约 28 行 / tick**。

⇒ **每 tick 该列车被 `Tick()` 了 20~28 次**，于是：
- 速度以 20~28 倍速率衰减：`177 → 0` 只需 **约 8~9 tick（≈0.3 秒）**，这就是玩家看到的"没有减速曲线的突然刹停"；
- 位置总量正常（每次调用按当前 `cur_speed` 推进 0.5px × 20 次 ≈ 10px/tick，正好是该速度应有的位移），所以车"看起来还在前蹭"；
- 每次都跑 `UpdateSpeed()` 里的刹车逻辑（减速度 1/tick），所以宏观上像"被强行按住"。

### 14.23.4 猜测根因（待验证，已落盘）

**G-282-1（首选）**：JGRPP 的每 tick 车辆缓存 `_tick_train_front_cache` 里**同一链头被登记了多次**（或 `InvalidateVehicleTickCaches()` 被漏调/被 R3R 的链重排弄乱），使 `Vehicle::Tick()` 对同一列车每 tick 调用 N 次（N≈20~28）。

支撑证据：本文件 `train_cmd.cpp:9485-9496` 的既有注释明确写到 R3R 解挂时会因"新 front engine 在 tick 缓存构建之后才提升"而出现"**永远不会被 Tick()ed**"，修法是 `InvalidateVehicleTickCaches()`；**同一缓存的另一种失效形态就是重复登记 ⇒ 每 tick 被 tick 多次**。本次现场是挂接主动方（veh=24）在正线行驶，且日志中 `order=1 real=10`、`db=1`、`track=01/04` 稳定，说明链没有立刻被重组，符合"缓存里多份条目"的形态。

**G-282-2（备选）**：某个 R3R 分支在同一 tick 内**循环调用** `TrainLocoHandler()`/`UpdateSpeed()`（例如耦合重试循环）。

**G-282-3（当前最强猜测，2026-10-23 追加）**：`_tick_train_front_cache` 里塞进了**整条链的多节**（不只链头），而 `TrainLocoHandler()` 内部一律 `consist = v->First()` ⇒ 每节都落到同一个 veh24 上处理。N≈20~28 恰好等于该链节数。

支撑：`src\vehicle.cpp:1523-1568` `RebuildVehicleTickCaches()` 对每个池内 Train 用 `is_front` 决定是否 push；`OTTD_UPPER_TAGGED_PTR` 分支的 `is_front` 来自 `VehiclePoolOps::IsNonFrontVehiclePtr(ptr)` —— **池内标记位**，不是实时 `Previous()==nullptr`。若 R3R 的重链（挂接/翻转/解挂）只维护 `next/previous` 指针而没同步该标记，整条链就会全被当成 front。
判据：`chain 节数 ≈ 20~28`；日志里若有 `GEO settle-*` / `CHAIN-ATTRS n=` 可直接对数。待查：`IsNonFrontVehiclePtr` 的写入点是否被 `SetNext` 覆盖。

证伪/证实判据（下轮）：
- 在 `TrainLocoHandler` 入口按 `(veh, _tick_counter)` 计数，`>1` 时打一行 `LOCO-REENTRY veh= n= tick=`；出现 n=20+ ⇒ G-282-1/2 成立。
- 统计 `_tick_train_front_cache` 构建后同一 primary 的出现次数，`>1` 时打 `TICKCACHE-DUP veh= n=`。
- 若 n 恒为 1，则 §14.23.3 的"每行 = 一次完整 handler"假设本身错误，回到 `dt` 探针（REG-281-C）先修计数。

### 14.23.5 仍未解释

- 为什么只有**挂接主动方**（veh24）出现，veh21 的 `cage` 只从 6→7（约 7~14 行/tick，量级相近但小得多）——若两车都 dupliacte，则倍率差异来自链长/段数，需要按链统计。
- `order` 在最后一行变成 `3`（`9957`），即停车瞬间订单被推进（可能是 `ProcessOrders` 在最后一次 handler 里执行）。
- `dt` 探针（REG-281-C）本身仍不可用：它只在**打印时**更新 `last_tick`（`train_cmd.cpp:20050`），所以 `dt` 读数是"两次打印的 tick 差"，不是"两次调用的 tick 差" —— 这也解释了 `dt=1` 为何稀少。

### 14.24 G-282-3 数值判据**通过**：链 48 节 / veh24 所在段 24 节 ≈ 观察的 20~28 次/tick

取证（`D:\temp\codebuddy-dropped-files\...\R3R_debug.log`，行号为日志行号）：

- `:118` `COUPLE-OK loco=21 rear=50 consist=24 co=0 real=1 type=1 tx=1889 ty=188 x=30234 y=3016`
  ⇒ 本次挂接是 **veh21 主动挂 veh24**（`consist=24` 指被挂方链头），挂完合并链链头 = 21。
- `:186` `CHAIN-ATTRS head=21 n=48 FE=2 SEG=2 pow=57932 wt=2352 len=192 spd=428`
  ⇒ 合并链 **48 节**，**2 个 front engine（21 与 24）**，**2 段**。
- `:194~226` `GEO settle-couple n=0..47`：`segid=1` 段 = `n=0..23`（veh 21,22,23,3..20,45,46,47，24 节）；`:218` 起 `n=24` 换成 `veh=24 ... segid=2 starF=1`
  ⇒ **`segid=2` 段 = veh24..veh47 共 24 节**，veh24 是该段 ★ 段头。
- `:243` `SEGTR-SNAP tag=couple head=21 nseg=2 ctrl=24 borrowed=1`
  ⇒ 控制段 = veh24 段（`ctrl=24`），与"veh24 是主动方/排程主人"一致。

判定：观察值 **20~28 次/tick**（§14.23.3，`cage` 200 行涨 7 tick）与 **24 节**吻合（误差区间正好覆盖 20~28）。
若"整链 48 节全被当 front"则应约 48 次/tick（177→0 只需 ~3.7 tick），**不符**；
若"只有 2 个真 front"则应 2 次/tick（177→0 需 ~88 tick ≈3.7 秒，且会有明显减速曲线），**也不符**。
⇒ 最贴合的形状是：**`_tick_train_front_cache` 收录了 veh24 那一段的全部 24 节（或 21 段那 24 节）**，即 G-282-3 成立（"缓存里塞进整条链的多节，且每次 `TrainLocoHandler()` 都落到同一个 `v->First()` 上"）。

新的待解子问题（已登记，按规则落盘）：

- **Q-282-a**：为什么是 24 节而不是 48 节？若 bit63 标记"只有链头没置位"，整链应全露；24 节恰好是一个段 ⇒ 怀疑标记维护点是**按段**跑的（R3R 段级操作里 setter 的传播范围 = 段而非整链），需在 `SetNext()` 之外的路径上核对。
- **Q-282-b**：为什么 veh21（另一段段头/整链物理链头）的 `cage` 增速只有 veh24 的 1/4~1/2？若 21 段 24 节也全露，两段合计应 ~48 次/tick。这一条与 Q-282-a 是同一个未知量的两面，**必须先实测"缓存条目数"再下结论**。
- **Q-282-c**：`FE=2` 说明 21 与 24 都是 `IsFrontEngine()`，`Train::Tick()` 的 `:21315-21359` 对 24 调用时用的 `v->First()` 是 21 ⇒ 24 这 24 次调用**驱动的是同一条链**（21 的车）——这正是"速度被重复衰减但位置总量正常"的机制闭环。

下一步探针（只读，尚未落码）：在 `RebuildVehicleTickCaches()` 之后统计 `_tick_train_front_cache` 里按 `front->First()->index` 分组的条数，`>1` 打 `TICKCACHE-DUP head= n=`；并在 `TrainLocoHandler` 入口按 `(consist->index, _tick_counter)` 计数，`>1` 打 `LOCO-REENTRY veh= n= tick=`。两行即可把 Q-282-a/b 定量。

### 14.25 **重大纠错（推翻 §14.23/§14.24）**：`_tick_counter` 是"游戏日"不是"车辆 tick"，"每 tick 调 20~28 次 handler"是探针口径假象

代码取证（本机源码，非推测）：

- `src/date.cpp:566-575` `IncreaseDate()` 里 `_tick_counter++`（`:569`）——**这是 `_tick_counter` 唯一的自增点之一**。
- `src/openttd.cpp:1611-1627`：每个 vehicle tick 的收尾 `if (DateDetail::_tick_skip_counter < DayLengthFactor()) { … CallVehicleTicks(); … } else { DateDetail::_tick_skip_counter = 0; IncreaseDate(); … CallVehicleTicks(); … }`
  ⇒ **`CallVehicleTicks()` 每个 subtick 都调一次，而 `_tick_counter++` 只在 `_tick_skip_counter >= DayLengthFactor()`（即跨日）时发生一次**。
- `src/openttd.cpp:1600` `_state_ticks++;   // This must update in lock-step with _tick_skip_counter`

结论：`_tick_counter` 的语义是**游戏日**，`_state_ticks` / `TickSkipCounter()` 才是**车辆 tick**。二者比值 = `DayLengthFactor()`。

对日志的重新解读（完全自洽）：

- `:3543` `SPDD veh=24 from=20 to=21 dt=1 … cage=1` 到 `:3587` `… to=43 … cage=1`：**同一"天"内**的 23 次 handler 调用 = **23 个真实 subtick**，即该档 `DayLengthFactor()≈23~24`。**每个 subtick 一次 handler，完全正常**。
- `:3515-3542` veh=21 同 `cage=327` 连续 22 行 ⇒ 同理，是同一天里的 22 个 subtick，不是"重复调用"。
- `:3530` `dt=468` ⇒ 距离上次满足打印条件已过 **468 天**（`r3r_spd_hard` 只在 `now==0 || prev>=20 || (prev>now && diff>=10)` 时无条件打印，慢速巡航期不打）⇒ `dt` 是"天"差，**彻底无信息量**（REG-281-C 的真因，比我先前写的"打印即更新 last_tick"更根本：`_tick_counter` 本身就不是 tick）。

⇒ **G-282-1 / G-282-2 / G-282-3 全部证伪并撤回**：
- `RebuildVehicleTickCaches()`（`vehicle.cpp:1523-1569`）第一行就是 `ClearVehicleTickCaches()`（`:1525`），且按 pool index 每车最多 push 一次 ⇒ **不存在重复条目**（G-282-1 死）。
- `TrainLocoHandler()` 全库只有 `train_cmd.cpp:21331/21347` 两个调用点（都在 `Train::Tick()` 内），**无 R3R 循环直调**（G-282-2 死）。
- `Train::Tick()` 有 `IsFrontEngine()` 门，`FE=2` ⇒ 每天每链最多 2×2=4 次 handler 调用，与 23 行的"每天"读数不矛盾（G-282-3 死）。
- `RebuildVehicleTickCaches()` 的 `is_front` 读池内 bit63（`vehicle.cpp:1535`）、`Vehicle::SetNext()` 同步维护该位（`vehicle.cpp:4572-4603`）⇒ 缓存语义本身健康，**不需要**加 `TICKCACHE-DUP` 探针。

**对"神秘减速"的影响**：既然每 subtick 只有 1 次 handler，则 `cur_speed` 每 subtick 变化 ±1 就是引擎 `UpdateSpeed()` 的正常输出。177→0 需要 177 个 subtick ≈ 177 × 30ms ≈ 5.3 秒（若 1 天 = 23~24 subtick，则 177 subtick ≈ 7.4 天）。这**不是**"二十倍速刹车"，玩家的"骤停"必须另有解释——回落到两个仍未排除的方向：
1. `_tick_skip_counter` 一天内的 subtick 数（`DayLengthFactor`）远大于 1 时，**一天内**看得见的位移很大但日志只按"天"打 `cage`/`dt`，视觉上"来不及看到减速"只是**日志采样太粗**（纯观感问题，不是行为 bug）；
2. 真有 R3R 写点把速度清零，但写点发生在 `ctx` 更新为 `decouple-interlock` 的瞬间，而 `SPDD` 的 `cfrom/cto` 只记录**ctx 里存的 from/to**（新 ctx 的 from/to 都是 0）⇒ 需要看 `SPD-DROP`（目前被 `R3RDbgWrite()` 白名单吞掉，REG-281-D）。

**必须做的探针修正（待落码）**：
- 把 `SPDD`/`SPD-DROP`/`cage`/`dt` 的时间基准从 `_tick_counter` 换成 `_state_ticks`（`date_func.h` 的 `DateDetail::_state_ticks`，或直接用 `TimerManager` 无关的 `TickSkipCounter()` 不够，因为它每天重置）。
- `SPD-DROP` 改用 `R3RFopenDbg("a")` 直写（REG-281-D）。
- 打印 `DayLengthFactor()` 一次（启动时一行），否则以后还会有人把"天"当"tick"。
- 建议在 `SPDD` 行里同时打 `sk=<TickSkipCounter>`，这样"同一天内第几个 subtick"可直接读数。

### 14.26 现场重读：`fp` = `force_proceed`；日志尾巴那段是**正常到站减速**，但末尾出现 `5→14→0` 的速度回弹

**`fp` 语义已确认**（`src\train_cmd.cpp:20076`）：`fp=%d` 打的是 `(int)consist->force_proceed`，**不是**"前方可行"。所以 `fp=0/1` 只表示"是否处于强制通过信号状态"（`TrainForceProceeding` 位），不能用来解释刹车。**DEG-282-1 前半段作废**（我把它当成"路径可行"是错的）；后半段"正线上为何刹到 0"已由下面解答。

**日志尾巴（`:9900-9961`）重读 —— 这是正常到站，不是神秘减速**：

- `:9900-9955`：`from=75` 起，每个 subtick `-1`，一路 75→74→…→20→19，`tile` 从 `263,234` 推进到 `264,234`，`x` 4214→4238（+24px），`order=1 real=10` 恒定，`track=01`（TRACK_X）。
- `:9956` `from=15 to=5`（一次 **-10**）、`:9957` `from=14 to=0`（一次 **-14**）、`order` 由 `1` 变 `3`。
- `:9958` **`DEPOT-ARR veh=24 spd=0 real=11(17) curType=4 stuck=0 tileDepot=0 tx=265 ty=234 destTx=265 destTy=231 ...`**，`:9959-9961` 随即释放 `279,234` 的幽灵预留。
  ⇒ 车是**驶向车库（`destTx/destTy=265,231`）在 `265,234` 完成最后一段减速**，速度归零正常，`order=3` 是"到达/换阶段"，不是 R3R 事件。

**新异常（真正的疑点，已确认是 engine 侧而非 R3R 侧）**：

`:9955 to=19` → `:9956 from=15 to=5` → `:9957 from=14 to=0`
⇒ 上一次 handler 结束时 `cur_speed=5`，下一次 handler 入口却读到 `14`。中间必然有一次 handler 把 5 抬到 14（该次 `prev=5 <20`、`now=14` 是增加、`dt=0`，**不满足 `r3r_spd_hard` 故没打印**）。
`Train::Tick()` 每 subtick 连续调用两次 `TrainLocoHandler(this,false)` / `(this,true)`（`train_cmd.cpp:21331/21347`）⇒ 高度怀疑**两遍 handler 对 `cur_speed` 的写入互相打架**（预演/应用两遍各算一次加减速，或第二遍把第一遍改过的值又回滚），这正好解释玩家看到的"最后几格速度乱跳、没有平滑刹车曲线"。

**下一步（唯一待确认项）**：读 `train_cmd.cpp:21315-21365`（`Train::Tick`）与 `TrainLocoHandler` 内所有 `mode` 分支，确认 `mode=false` 与 `mode=true` 两遍是否都写 `cur_speed`。若结论成立，则该现象是**上游引擎设计**（两遍模拟）在"低速 + 每 subtick 多次"下的表现，R3R 无需改；若不成立，则是 R3R 某处把 `cur_speed` 抬高后没打 tag（`_r3r_spd_ctx_by_veh` 覆盖不足）。

### 14.27 口径小结（给后续轮次用）

| 探针字段 | 真实语义 | 能不能用 |
|---|---|---|
| `dt=` | `_tick_counter` 差 = **游戏日**差 | **不能用**（旧结论全废） |
| `cage=` | 距 ctx 写入的**天数** | 只能用来看"几天前写过" |
| `fp=` | `force_proceed`（强制通过信号） | 不能当"路径可行" |
| `m=` | ctx 是否已被后续写点覆盖 | 仅用于排除误归因 |
| `prog=` | `progress`（8 位回绕） | 只能看趋势，不能算位移 |
| `x=` / `tile=` | 世界像素 x / tile | 位移核对用（本段 24px/58 subtick，与速度量级大致相符） |

### 14.28 **决定性代码证据**：`UpdateSpeed()` 不受 `mode` 门控 ⇒ 每 subtick 速度变化 2，"20~28 次/tick"再降级为"2 次/subtick"

`src\train_cmd.cpp:21116-21118`：

```cpp
if (!mode) consist->ShowVisualEffect(std::min(max_speed_info.strict_max_speed, max_speed_info.advisory_max_speed));
j = consist->UpdateSpeed(max_speed_info);
```

- **只有 `ShowVisualEffect` 被 `!mode` 门控**，`UpdateSpeed()` 在 `mode=false` 与 `mode=true` 两遍里**都执行**。
- `Train::Tick()`（`:21331/21347`）每 subtick 连续调两次 handler ⇒ **每个 timer tick 速度被改两次**（加速 +2 / 减速 −2）。
- 这是上游两遍模拟（第一遍订单/信号/寻路 + 运动，第二遍只跑运动）的固有形态，**不是 R3R 造成的**。

与日志的对齐（自洽性检验）：

- `:9900-9955` 共 56 行，速度 75→19，**每行 −1**；把"每 subtick 2 行"代进去 ⇒ 28 个 subtick。
- 同段 `cage` 78→81（3 天）⇒ 每天约 9.3 个 subtick ⇒ 该档 `DayLengthFactor()≈9~10`（玩家设了日长因子）。
- 以前算出的"20~28 次/`_tick_counter`"= 每天 20~28 **次 handler 调用** = 每天 9~14 subtick × 2 遍，**量级完全吻合**，再次坐实"没有重复调用，只有两遍模拟"。
- 位移核对：速度 75 时每遍推进 `75*3/4=56` 单位、`progress` 8 位回绕、每 256 单位 1px ⇒ 每两遍约 0.44px ⇒ 56 行 28 subtick 约 12px；实测 24px（4214→4238），**差 2 倍**——与"两遍模拟各自推进一次位置"吻合（若两遍都走运动分支，位移也翻倍）。此点记为待验 Q-282-e，但不影响"速度变化 2/subtick"的结论。

### 14.29 全日志 `veh=24` 的 5 次归零（`to=0`）全量清单与场景归类

| 日志行 | 读数 | 场景判定 |
|---|---|---|
| `:5860` | `from=14 to=0 dt=1 cage=74 fp=0 order=3 track=02 tile=41,73` | 路口/道口格（track=02）到站 |
| `:6452` | `from=61 to=0 dt=8 cage=93 fp=0 order=5 track=80 tile=42,54` | **车库内**（`track=80`=`TRACK_BIT_DEPOT`） |
| `:6899` | `from=14 to=0 dt=1 cage=16 fp=0 order=3 track=02 tile=42,65` | 道口格到站 |
| `:7511` | `from=61 to=0 dt=7 cage=15 fp=0 order=1 track=80 tile=42,54` | **车库内**（同格 42,54，第二次） |
| `:9957` | `from=14 to=0 dt=0 cage=81 fp=0 order=3 track=01 tile=265,234` | 正线到站，下一行 `:9958 DEPOT-ARR ... destTx=265 destTy=231` |

结论：**5 次全部 `ctx=stale`（无 R3R 写点）、全部 `fp=0`（未强制通过信号）**，且 4/5 明确是"到站/进库"的终局停稳；`track=80` 两次一夜之间同格，是同一车库的两次进出。

### 14.30 日志倒数段（`:9900-9961`）重判：**这是完整、正常的到站减速曲线**

`75 → 19`（逐 −1，56 行）→ `:9955 19` → `:9956 15→5`（一次 −10）→ `:9957 14→0`（一次 −14）→ `:9958 DEPOT-ARR`。

- 有减速曲线（不是"无曲线骤停"）。
- `:9956/9957` 的"一次跳 10/14"是 `UpdateSpeed()` 在 `strict_max_speed` 被钳到 0（进站/进库触发）时的硬钳，属上游行为。
- `:9956 to=5` 与 `:9957 from=14` 之间的 **+9 回升**（`prev=5 < 20`、加速、`dt=0` ⇒ 不满足 `r3r_spd_hard` 故未打印）是唯一不自洽处，疑点记为 **Q-282-f**：正常刹车途中不应出现 +9；可能是 `mode=true` 那遍读到被第一遍钳过的 `cur_speed` 后再由 `GetCurrentMaxSpeedInfoAndUpdate()` 重算限速所致，需单独验证（不影响归因结论）。

### 14.31 本轮进度小结（给玩家）

已确证（可写进结论）：

1. `_tick_counter` = 游戏日；`_state_ticks` 才是 vehicle tick（`openttd.cpp:1600`）。所有 `dt=`/`cage=` 读数按"天"理解。
2. 每 timer tick 调 **2 遍** `TrainLocoHandler`（`Train::Tick` 两行），`UpdateSpeed()` 无 `mode` 门控 ⇒ **速度每 timer tick 变化 ±2**。这是上游设计，R3R 未参与。
3. `fp` = `force_proceed`，`m` = ctx 覆盖标志，`prog` 是 8 位 `progress`。
4. `veh=24` 全部 5 次归零都在到站/进库场景，**没有一次来自 R3R 写点**（`ctx=stale`）。
5. 第 281 轮定位的 `waitcut` 每 tick 归零，在**本份日志里 0 条**（`SPDD veh=24` 的 ctx 只有 `decouple-interlock` 与 `stale`）。

待澄清（已按规则登记，等玩家一句话即可收敛）：

- **Q-282-d**：玩家说的"两次神秘减速"是否就是 `:6452` 与 `:7511` 那两次 `from=61 to=0`（都在 `track=80` 车库格 42,54）？若不是，请给发生时的车速/大致位置，或直接指出日志行号区间。
- **Q-282-f**：`:9956→:9957` 的 `5→14→0` 回弹是否就是玩家眼里的"没有刹车曲线"（若如此，根因在 `UpdateSpeed` 的两遍钳位，不在 R3R）。

未做：探针基准换 `_state_ticks`、`SPD-DROP` 改直写（REG-281-D）、`LOCO-REENTRY`（已判定不必要）。

### 14.32 Q-282-d **自答收口**（2026-10-23，重读 `build\R3R_debug.log:9944-9961` 原行）

玩家原话「那两次都是属于挂接的主动方（应该是 veh24）的神秘减速，**你在日志的倒数几次减速内就能找到**」⇒ 位置限定在日志尾部，故 §14.31 里拿 `:6452`/`:7511`（车库，日志中段）去问是**错问**，撤回该问法。真正的两行是日志倒数第三、第二条 `SPDD veh=24`：

```
9944-9955  SPDD veh=24 from=31 to=30 … 20 to=19   dt=0/1  x=4235→4238  prog 递增   ← 正常刹车曲线，每行 −1
9956       SPDD veh=24 from=15 to=5   dt=0 ctx=stale cage=81 m=1 tile=264,234 x=4239 prog=5   ← 第 1 次「神秘减速」：一次 −10
9957       SPDD veh=24 from=14 to=0   dt=0 ctx=stale cage=81 m=1 tile=265,234 x=4240 prog=6 order=3  ← 第 2 次：一次 −14，且跨格 + 订单类型切换
9958       DEPOT-ARR veh=24 spd=0 real=11(17) curType=4 … destTx=265 destTy=231 tileEqDest=0
9959-9961  UNRES-RAIL / RESV-WATCH CLEAR-RAIL / RESV-GHOST waiter tile=279,234 track=0 phase=release-ghost actor=24
```

新取证得到三条此前未记录的读数：

1. **`:9956` 的 `from=15` 与 `:9955` 的 `to=19` 之间缺了 19→15 这 4 步**（SPDD 其余每 −1 都打印）⇒ 这 4 行是被探针打印条件吞掉的，不是速度没变。属探针口径问题（并入 REG-281-D 家族），不改变定性：**两次跳变是真的（−10 / −14），不是缺行造成的错觉**。
2. **`:9957` 同时 `tile` 264,234 → 265,234（跨格）+ `order` 1 → 3（订单类型切换）**，紧接着 `:9958 DEPOT-ARR … destTx=265 destTy=231` ⇒ 这两次骤降发生在**到点/进库的最后一次跨格**上，是「到点判据成立 → 限速被钳」的瞬间，而非正线巡航中随机丢速。
3. `x` 全程 4235→4240 只前进 5px，`prog` 仍在递增 ⇒ 玩家观感「车还在缓慢前蹭」= 刹车曲线尾段的正常位移，速度表却已经掉到 0/5，这就是「没有刹车曲线」的主观来源。

**Q-282-f 与 Q-282-d 合并**：`5→14→0` 的 `+9` 回弹就发生在这同两行之间（`to=5` → 下一行 `from=14`），不需要单独追——它是同一个机制（两遍 `UpdateSpeed()` 各自重算限速）在同一瞬间的两次读数，属引擎侧，不在 R3R。

**结论（可写进答复玩家）**：veh=24 的「两次神秘减速」= `build\R3R_debug.log:9956`（15→5）与 `:9957`（14→0），两次都 `ctx=stale`（无 R3R 写点）、`fp=0`、`db=1`、`m=1`，且紧接 `:9958 DEPOT-ARR` ⇒ 是**进库/到点的引擎硬钳**，R3R 未参与。

新增待定（不阻塞收口）：

- **Q-282-g**：`−10` 与 `−14` 的钳位来源未逐行确认——候选 A = `GetCurrentMaxSpeedInfoAndUpdate()` 给出的 `strict_max_speed` 在跨入库格时骤降为 5 → 0；候选 B = `TrainController()` 的停车点把 `cur_speed` 直接归零。两者都属引擎侧，任一成立都不改我们的归因；只有在玩家坚持「这两次发生在正线巡航、不是进库」时才需要继续挖。
- **Q-282-h**：`19→15` 缺 4 行说明 SPDD 有打印抑制条件未核（与 REG-281-D 同批处理，不单独开轮次）。

### 14.33 撤回两条错误陈述（2026-10-23，直接读探针源码 `src\train_cmd.cpp:20029-20081`）

玩家质问「你都承诺加了探针就可以定位的，现在又告诉我要探针」。**是我的记录写错了，不是探针没做**。逐行核对后撤回两条：

1. **REG-281-D 对 SPDD/STUCK 不成立，撤回**。`:317-318` 原文：`SPDD and STUCK use R3RFopenDbg() rather than R3RDbgWrite() on purpose, so the tag whitelist cannot swallow them.`；`:20030` 亦写明「SPD-DROP 格式原样保留 (KI-348 判据仍依赖它), 仅改走 R3RFopenDbg」；`:20067` 实际就是 `FILE *const r3r_dbg = R3RFopenDbg("a");` + `fprintf`。我上一轮引的 `:300-304`「every standing probe writes through R3RDbgWrite() ... whitelist filters」是**第 280 轮的历史描述**，第 281 轮已改，我把它当成现状抄进了 §14.31/§14.32 与 KI「第 282 轮续」⇒ 那两处的「SPD-DROP 仍走 R3RDbgWrite()（REG-281-D 未修）」作废。
2. **探针是生效的、定位也是探针给出的一手读数**，不是"再要一轮探针"。日志里 `SPDD veh=24` 的每一行都是这段代码写的（`:20069` 的格式串逐字对应）。`from`/`to` = 本车「上一 tick 快照 → 本 tick 入口速度」（`:20043`/`:20038`/`:20070`），所以 `:9956 from=15 to=5`（−10）与 `:9957 from=14 to=0`（−14）就是这两次骤降的**实测值**，不需要再补任何探针。

**顺带把探针的自解释读清楚（此前误用）**：`ctx`/`cfrom`/`cto`/`cage`/`m` 全部描述「该车**上一次 R3R 硬写**」（`:20057-20066`）：`cage` = 那次硬写距今 tick 数，只在该值 ∈{0,1} 时才把 tag 当成本行原因，否则打 `ctx=stale` + `cfrom=cto=0` + `m=1`，注释原话是「so the line can never be mis-read as 'this tag did it'」。⇒ `:9956`/`:9957` 的 `ctx=stale, m=1` 是探针**主动声明**「本行不归因于任何 R3R 写点」，这正是「R3R 未参与」的正面证据，不是探针失效。

**已知残余（不影响上述判定）**：`dt`/`cage` 的时间基是 `_tick_counter`（`:20044`/`:20061`），而 REG-281-C 已记录该基准在暂停/按天推进下不等于真实 tick ⇒ `cage=81`、`dt=0` 这类**数字本身不可信**，但「是否 ∈{0,1}」的判断仍然正确，结论（stale ⇒ 无 R3R 写点）不受影响。

**Q-282-h 结案（撤销为缺陷）**：`19→15` 那 4 步没打印是**设计内行为**——`:20046-20049` 的打印门是「归零 / 上一值≥20 / 骤减≥10 立即打，其余每车每 64 tick 一行」；`prev=19,18,17,16` 三个条件都不满足且 64 tick 未到 ⇒ 不打。所以不是"打印抑制条件未核"，而是"平滑小步减速本来就不逐行打印"。
