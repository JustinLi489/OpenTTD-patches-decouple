# R3R 第 307 轮：排程推进两条新约束（玩家 2026-10-26 拍板）

> 状态：**已收到规则并落盘，未改码**。本文档是唯一可信状态载体；对话只是过程。

## 一、规则原文（玩家原话，不得改写）

> 「等一下，我们先这样试试：一，只有 GOTO STATION 后面能跟 GOTO COUPLE。二，假如 GOTO STATION 的下一条命令是 GOTO COUPLE，那么这个前往车站的命令必须完全停稳才能跳排程。」

## 二、语义拆解

### R307-1：GOTO_COUPLE 的合法前驱只有 GOTO_STATION

- 合法前驱 = `OT_GOTO_STATION`（是否含站场 `MOF_R3R_YARD` 变体 ⇒ 视为同一 `OT_GOTO_STATION`，不另判）。
- 其它任何命令（`OT_GOTO_WAYPOINT` / `OT_GOTO_DEPOT` / `OT_GOTO_COUPLE` / `OT_WAIT_COUPLE` / `OT_DECOUPLE` / 条件跳转 / 时间表）后面跟 `OT_GOTO_COUPLE` ⇒ **不合法**。
- 子问题 **P-1（待玩家拍板，默认按 (a) 执行）**：
  - (a) **跳过不执行** —— 与 KI-180 / KI-274 的 `SKIP` 口径一致（不订路、不驻留，直接继续下一条）；
  - (b) 拒绝写入 —— 订单窗口不允许添加/保存时报错。

### R307-2：前站必须"完全停稳"才允许跳到 GOTO_COUPLE

- 判据：当 `next` 是 `OT_GOTO_COUPLE` 且 `cur` 是 `OT_GOTO_STATION` 时，推进条件追加 `consist->cur_speed == 0`（完全停稳）。
- 与既有 `OT_DECOUPLE` 触发口径同源（`cur_speed == 0` + 到站），保持一致性。
- 反向保证：非 `GOTO_COUPLE` 的下一命令**不受影响**（正常到站即推进，不额外要求停稳）。

## 三、待取证点

| 编号 | 待查 | 用途 |
|---|---|---|
| T-1 | `AdvanceOrdersFromVehiclePosition` 现行推进判据是否含速度/停稳 | R307-2 的落点 |
| T-2 | `OT_GOTO_COUPLE` 合法性校验点（`R3RIsCoupleCommand` / `CmdModifyOrder` 白名单 / `GetCouplePosition`） | R307-1 的落点 |
| T-3 | `TrainEnterStation` 到达与 `stopped` 置位时序 | 判定"完全停稳"的可靠读数 |
| T-4 | `R3RAutoRecordChainOrders` 自动记录是否会放出非法序列（GOTO_COUPLE 前置非车站） | R307-1 是否需在自动记录侧也过滤 |

## 三之一、首轮取证结论（2026-10-26）

### T-2 → R307-1 落点已定位：`SwitchToNextOrder()`

`src\train_cmd.cpp:15874-15925`（`ChooseTrainTrackLookAheadState::SwitchToNextOrder`）里：

```
15894:				case OT_GOTO_STATION:
15895:				case OT_GOTO_WAYPOINT:
15896:				case OT_GOTO_COUPLE:
15897:					/* R3R: GOTO_COUPLE is a travel order. It must be selectable
...                   * when advancing, e.g. after a waypoint/station order is
                      * completed. Without this case it fell into the default
                      * branch and was skipped ... */
15903:					this->v->current_order = *order;
15904:					return UpdateOrderDest(this->v, order, 0, true);
```

**结论句**：`OT_GOTO_COUPLE` 与 `OT_GOTO_STATION` / `OT_GOTO_WAYPOINT` 在同一个 case 分支里**无差别选中**，**没有任何前驱类型校验** ⇒ R307-1（只有 GOTO_STATION 后面能跟 GOTO_COUPLE）在此处零防护，落点唯一且清晰。

### T-1 → R307-2 落点已定位：`AdvanceOrdersFromVehiclePosition()`

`src\train_cmd.cpp:15927-15988`，真正的推进在 `:15975-15987`：

```
15975:		if (v->current_order.IsBaseStationOrder() ?
15976:				HasStationTileRail(moving_front->tile) && v->current_order.GetDestination() == GetStationIndex(moving_front->tile) :
15977:				moving_front->tile == v->dest_tile) {
e.g.			if (... TBM_REALISTIC && IsBaseStationOrder) { ShouldStopAtStation(...) ⇒ STOP_FOUND }
15984:			if (v->current_order.IsType(OT_WAITING)) SetBit(state.flags, CTTLASF_STOP_FOUND);
15985:			this->SwitchToNextOrder(true);
15986:			return;
15987:		}
```

**结论句**：判据只有"**运动前端所在的格子是不是目标站台格**"（`HasStationTileRail(moving_front->tile)`），**完全没有 `cur_speed == 0` / `stopped` 判据** ⇒ 车头一进站台格就 `SwitchToNextOrder(true)`，如果下一条是 `OT_GOTO_COUPLE`，当刻就以"未停稳"状态切进了挂接命令。这正是玩家 R307-2 要挡的形态，且与第 286 轮 KI-453（等待区路点订单在"进格"时就推进）**完全同源**，修法可照抄该轮的"改为停在停车标"思路。

### T-4 关联：`:15952` 已有的同类早退

```
15952:		if (v->current_order.IsType(OT_GOTO_DEPOT) || v->current_order.IsType(OT_GOTO_COUPLE)) return;
```

**结论句**：`current_order` 已经是 `OT_GOTO_COUPLE` 时这一函数早退（交给 R3R 耦合逻辑推进）——说明 R3R 早已承认"GOTO_COUPLE 不能靠 look-ahead 推进"，**只漏了"推进到 GOTO_COUPLE 的那一步"没有停稳门槛**。

### T-3 → 车站到达本身**不停车**，这是 R307-2 的真正病根

`src\train_cmd.cpp:17923-18012` 的 `TrainEnterStation()` 三条分支对比：

| 分支 | 行 | 是否强制停车 |
|---|---|---|
| `OT_WAIT_COUPLE` | `:17930` | 直接 `return`，完全不走进站流程 |
| 路点（`Waypoint::IsExpected`） | `:17936-17948` | **是** —— `MakeWaiting()` + `R3RSpdCtx(...)` + `cur_speed = 0;` + `UpdateTrainSpeedAdaptationLimit(0)` |
| **`OT_GOTO_COUPLE`** | `:17969-17985` | **否** —— 只把速度限到 20（`stn-gotocouple`）后 `return`，明确注释"do not stop at the platform edge, keep creeping forward" |
| 普通车站 | `:18003` | `BeginLoading()` ⇒ `current_order` 变 `OT_LOADING`，**车还在滑行** |

**结论句**：普通到站走 `BeginLoading()`，`current_order` 当场变 `OT_LOADING`；而 `AdvanceOrdersFromVehiclePosition()` 的 `:15947-15950` 对 `IsAnyLoadingType()` **无条件** `SwitchToNextOrder(true)` ⇒ 车头刚进站台格（可能还是高速）就已经把排程推到了下一条。若下一条是 `OT_GOTO_COUPLE`，此刻 `current_order` 变 `OT_GOTO_COUPLE`，`:15952` 随即早退、look-ahead 目标改成等待车底，列车**在滑行中**就被引导去挂车。

⇒ R307-2 的门槛必须加在**两个推进口**：`AdvanceOrdersFromVehiclePosition` 的 `IsAnyLoadingType` 分支（`:15947`）与站台格分支（`:15975`）。

### 风险登记 R-307-A（必须先告知玩家）

若该 `OT_GOTO_STATION` 订单本身**不需要停车**（`ONSF_NO_STOP_AT_ANY_STATION` 通过站 / 无货可装且 no-load），`cur_speed` 永远不会归零 ⇒ "必须停稳才推进"会使列车**永久卡在该站**、永远不进入 `GOTO_COUPLE`。按玩家"先这样试试"的字面执行，但必须在日志里留下可辨识的探针（`ORD-STOPGATE`），以便一眼看出是卡在门槛上而不是寻路失败。

### T-5 → 索引侧**已有** GOTO_COUPLE 锁（无需另加）

`src\vehicle_base.h` 三个推进函数开头一律：

```
1092:		if (this->type == VehicleType::Train && this->current_order.IsType(OT_GOTO_COUPLE)) return;   // SkipToNextRealOrderIndex
1116:		if (this->type == VehicleType::Train && this->current_order.IsType(OT_GOTO_COUPLE)) return;   // IncrementImplicitOrderIndex
1158:		if (this->type == VehicleType::Train && this->current_order.IsType(OT_GOTO_COUPLE)) return;   // UpdateRealOrderIndex
```

**结论句**：**一旦** `current_order` 已是 `OT_GOTO_COUPLE`，索引三路全锁、只有耦合逻辑能推进 ⇒ "进了 GOTO_COUPLE 之后乱跳"不是问题；问题**只在"跳进 GOTO_COUPLE 的那一步"没有任何门槛**。这与 R307-1 / R307-2 的落点判断一致：门槛要加在"选择下一订单"与"推进到下一订单"的那一侧，不要动索引锁。

**另需注意**：`SwitchToNextOrder()` 只在 `VehicleOrderSaver` 保护下改 `current_order`（`Restore()` 会还原，`:15848-15858`）⇒ 它影响的是 **look-ahead 的刹车/寻路目标**（这正决定"会不会停稳"），不是持久排程。持久排程的推进另有入口（`OT_LEAVESTATION` → `IncrementRealOrderIndex`），待 T-6 确认。

## 四、状态

- **已改码 + 已编译**（`build\openttd.exe` 2026-10-26 12:17:22 / 52 253 696 B，`EXIT_CODE=0`），游戏内复测待做。详见 §六。
- T-1 / T-2 / T-3 均已定位到具体行；R307-2 需覆盖两个推进口；R-307-A 风险已登记。
- KI 条目：`KI-487`（R307-1）、`KI-488`（R307-2），已写入 `R3R_KNOWN_ISSUES.md` 的「第 307 轮」小节。

## 五、落地（第二轮，已改码）

### 5.1 P-1 取值：(a) 跳过不执行

未等玩家回填，按文档里登记的默认值 (a) 执行：不合法的 `GOTO_COUPLE` **不拒绝写入**（写入一侧一字未改，玩家仍可照旧编辑订单），只在**执行侧**跳过。理由：与 KI-180 / KI-274 的 `SKIP` 口径一致，且"先这样试试"要求可回退、不动存档语义。

### 5.2 共用判据（新函数，唯一真源）

`src\train_cmd.cpp` 新增两个函数：

| 函数 | 位置 | 作用 |
|---|---|---|
| `bool R3RCoupleOrderFollowsStation(const Vehicle *v, VehicleOrderID idx)` | 紧接 `R3RTrainStopsAtWaypoint()` 之后（前置声明在其上方，因为 `R3RTrainStopsAtWaypoint()` 也要用它） | **唯一判据**：索引 `idx` 处的命令是不是 `OT_GOTO_COUPLE` 且其**紧邻的前一条真实命令**是 `OT_GOTO_STATION`（往回跳过 `OT_IMPLICIT`，隐式排程必然为假）。**R307-1 的合法性 = R307-2 的触发条件**，同一个函数 |
| `static bool R3RStationOrderHoldsForCouple(const Train *v)` | 紧随其后 | R307-2 的门槛：`cur_speed != 0` **且** 正在执行的车站命令后面是合法挂接命令。`OT_LOADING*` / 站台命令：从 `cur_real_order_index + 1` 起**跳过 `OT_IMPLICIT`** 取第一条真命令再判；`OT_LEAVESTATION`：判 **`cur_implicit_order_index`**（real 索引留作兜底）——见 §5.7 的纠错 |

### 5.3 R307-1 三处落点

| 落点 | 文件:函数 | 行为 |
|---|---|---|
| ① look-ahead | `train_cmd.cpp` `SwitchToNextOrder()` 的 `case OT_GOTO_COUPLE`（从原三合一 case 中拆出） | 不合法 ⇒ `break`（落到 `++cur_real_order_index` 继续找），不设 `current_order`、不 `UpdateOrderDest` ⇒ 寻路目标不会变成等待车底 |
| ② 持久排程 | `order_cmd.cpp` `ProcessOrders()` 取到 `order` 之后（`OT_IMPLICIT` 归一之后） | 新增 while 循环：不合法就把 `cur_real_order_index` / `cur_implicit_order_index` / `cur_timetable_order_index` 同步前进一行并重取命令，最多跳 `GetNumOrders()` 次（全非法则停在原地交回上层，不死循环） |
| ③ 等待区路点 | `train_cmd.cpp` `R3RTrainStopsAtWaypoint()` 与 `R3RWaypointOrderStopsForCouple()` | 判据由 `next->IsType(OT_GOTO_COUPLE)` 改成 `R3RCoupleOrderFollowsStation(v, next)` ⇒ **路点后面不可能再有合法挂接命令** ⇒ 第 286 轮 KI-453 的"路点停车等挂接"自动失效，路点恢复通过式（否则会出现"在路点停一次、然后跳过挂接命令继续跑"的幽灵停车） |

### 5.4 R307-2 三处落点

| 落点 | 文件:函数:原行 | 行为 |
|---|---|---|
| ① loading 推进口 | `train_cmd.cpp:15947` `AdvanceOrdersFromVehiclePosition()` 的 `IsAnyLoadingType()` 分支 | 保留 `SetBit(CTTLASF_STOP_FOUND)`，命中门槛则 **return，不 `SwitchToNextOrder`** |
| ② 站台格推进口 | 同函数 `:15975` 站台格分支（`SwitchToNextOrder(true)` 之前） | 同上 |
| ③ 持久推进口 | `TrainLocoHandler()` 的 `if (current_order.IsType(OT_LEAVESTATION))` 分支，**`current_order.Free()` 之前** | 命中门槛则就地刹停（`R3RSpdCtx` + `cur_speed = 0; subspeed = 0;`）并 `return true`，**保留 `OT_LEAVESTATION` 一 tick**，下一 tick 速度为 0 时再走原逻辑 |

**为什么 ③ 必须"就地刹停 + 保留订单"而不是直接 `return`**：`cur_speed != 0` 时若只是不推进，`OT_LEAVESTATION` 下没有寻路目标、`TrainController` 的 `case OT_LEAVESTATION` 会直接 return（不订路），列车会带速滑到尽头；而若先 `Free()` 再拦，会造出 `OT_NOTHING` + `TryPathReserve` 的中间态（KI-201 附记 6 的老坑）。刹停既满足"完全停稳"的字面，也让下一 tick 必然放行，不会死锁。必须先于 `Free()`。

### 5.5 探针（两条新边沿枚举，`R3REDGE_COUPLESKIP` / `R3REDGE_STOPGATE`）

- `ORD-COUPLE-SKIP site=lookahead|process veh= idx= from= to= num=` —— 不合法的挂接命令被跳过。key = `veh<<16 ^ 索引`，每 (车, 索引) 每窗口一行。
- `ORD-STOPGATE veh= site=loading|platform|leavestation order= real= num= spd= tile=x,y` —— 车站命令被"没停稳"挡住。site 区分三个落点；`leavestation` 那一条出现即表示**装载在车未停稳时就完成了**（旧行为下车会带速跳进挂接命令）。
- `process` 侧的探针走 `R3RProbeCoupleOrderSkipped()`（定义在 `train_cmd.cpp` 的 `R3RDbgEdge` 之后，`order_cmd.cpp` 里只有前置声明），因为边沿节流与标签枚举都是 `train_cmd.cpp` 文件内的。

### 5.6 已知风险 / 未覆盖

- **R-307-A（照字面实现，未软化）**：若该车站命令本身不停车（`ONSF_NO_STOP_AT_ANY_STATION` / `NO_STOP_AT_DESTINATION` 通过站），装货会**瞬间完成**并进 `OT_LEAVESTATION`，于是本站门槛会在**平台中途**把 `cur_speed` 强行归零一次（一 tick 后放行）。原稿推断的"永久卡死"**不成立**（已按 `vehicle.cpp:3965-3982` + `order_cmd.cpp` `UpdateRealOrderIndex()` 复核：索引会前进，下一 tick 速度为 0 即放行）。真实副作用是**通过站被急刹一次**；是否要改成"非停车车站 + 挂接 直接判非法"（并入 R307-1）待玩家拍板。判据：日志出现 `ORD-STOPGATE ... site=leavestation spd>0`，回车速在该站出现 0。
- **R-307-B**：`ProcessOrders()` 的跳过循环**不做** `current_order.Free()`；若跳过发生时 `current_order` 仍是刚完成的那条（例如路点），随后的 `order->Equals(current_order)` 判定与 `UpdateOrderDest` 照常走新命令，行为正确。但若整份排程都是非法挂接命令，索引在原地上限回卷、每 tick 重入一次（有 `++r3r_skipped > num` 上限，不会死循环），且最后一圈回卷后仍会把那条不合法命令**照旧执行**（刻意不 strand 列车）。
- **未做**：写入侧的拒绝（P-1 (b)）；`R3RAutoRecordChainOrders()` 自动记录的过滤（T-4）——它记录的是链头实际跑过的命令序列，执行侧已保证不会跑到非法挂接命令，故暂不动。

### 5.7 首版判据纠错（必须记住，勿回退）

首版 `R3RStationOrderHoldsForCouple()` 的 `OT_LEAVESTATION` 分支取 **`cur_real_order_index`**，注释假设"装货完成时 real 索引已推进到挂接命令"。

**该假设是错的**，两个锚点：

```
src\vehicle.cpp:3965-3982     装货完成 → LeaveStation() 把 current_order 变成 OT_LEAVESTATION
                              → 末尾只调 IncrementImplicitOrderIndex()（只动 implicit）
src\vehicle_base.h:1155 一带   UpdateRealOrderIndex() 才把 implicit 复制到 real
                              （由 ProcessOrders() 调用，即紧接本门槛的 Free() 之后）
```

而 `TrainLocoHandler` 的顺序是「先 `HandleLoading()`，再进 `OT_LEAVESTATION` 分支」⇒ **门槛正好夹在这两者之间，real 索引此刻仍是车站命令的索引**，首版判据在"最关键的离站时刻恒不触发"，R307-2 等于没生效。

修法（`src\train_cmd.cpp`）：

1. `OT_LEAVESTATION` 分支改判 `cur_implicit_order_index`（real 索引留作兜底）；
2. `OT_LOADING*` / 站台命令分支改为「从 `cur_real_order_index + 1` 起跳过 `OT_IMPLICIT` 取第一条真命令」再交给同一判据，避免自动记录的隐式命令夹在车站与挂接命令中间时漏判。

## 六、构建自证

复用既有 `_tmp_inc_build.cmd`（**未新建任何 .cmd**）。本轮跑了两次增量：第一次收录 §5.2 首版判据，第二次收录 §5.7 的纠错（第二次即最终 exe）。

**构建一**：`[4/4] Linking`，`build\R3R_incbuild.done` = `EXIT_CODE=0`（12:09:55），`build\openttd.exe` 12:09:52 / 52 248 576 B。

**构建二（最终）**：

| 项 | 值 |
|---|---|
| guard | `GUARD: incremental is safe (no header/lang file is newer than the newest object)` |
| 步骤 | `[1/3] order_cmd.cpp.obj` → `[2/3] train_cmd.cpp.obj` → `[3/3] Linking CXX executable openttd.exe`（Linking 1 次） |
| 判决 | `build\R3R_incbuild.done` = `EXIT_CODE=0` |
| 错误计数 | `error C` / `fatal error` / `FAILED:` / `build stopped` = **0** |
| 源码 → obj | `src\train_cmd.cpp` 12:14:41 → obj 12:15:05；`src\order_cmd.cpp` 12:05:27 → obj 12:07:35 |
| exe | **2026-10-26 12:17:22 / 52 253 696 B** |
| lint | `read_lints` 两文件 0 条 |
| exe 字面量 | 3/3 命中：`ORD-STOPGATE`、`ORD-COUPLE-SKIP`、`r307-station-couple-stop` |

未碰任何 `src\*.h`、未改语言文件 ⇒ 护栏判增量合法（不是"改头文件必须全量"那条路）。

## 七、复测判据（交玩家）

1. 车站后接挂接命令的正常场景：`ORD-STOPGATE … site=loading|platform|leavestation` 出现且同行 `spd>0`，随后 `spd=0` 才进入 `OT_GOTO_COUPLE`（`COUPLE-OK` 在其后）。
2. 车站后面不是挂接命令：无 `ORD-STOPGATE`、无 `ORD-COUPLE-SKIP`，进站/离站时序与第 306 轮 exe 一致（不得多停一 tick）。
3. 路点/车库后跟挂接命令：出现 `ORD-COUPLE-SKIP`，列车不去追等待车底；路点**不停车**（KI-453 已随本轮撤销）。
4. 车库挂接（KI-481）、配对等待、解挂判据不退化。
5. `ONSF_NO_STOP_*` 车站后接挂接命令：观察是否出现平台内急刹（R-307-A），回报是否可接受。

- **不动的部分**：`WAIT_COUPLE` / `DECOUPLE` 的前驱规则、`CmdModifyOrder` 白名单、订单 GUI、存档格式。

