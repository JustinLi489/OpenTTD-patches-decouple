# 第 308 轮：车库分支的 GOTO_COUPLE 被跳过（玩家 2026-10-26 报告）

> 状态：**已登记，根因待实证**。本文件按"收到报告第一时间优先写备忘"的规矩先落盘，取证过程按小节往下追加。
> 产生者：第 307 轮 exe（`build\openttd.exe` 2026-10-26 12:17:22 / 52 253 696 B）。

## 一、玩家原话（逐字）

> 「我真的好绝望，因为有列车不执行前往挂接的车库分支就跳了那个挂接命令。我感觉我做什么都在打地鼠」

拆成可判定的两件事：

1. 有一列车的排程里，"前往挂接"这一步是**车库分支**（`OT_GOTO_COUPLE` + `GetCoupleIsDepot()` 为真，挂接目标是车库）；
2. 该列车**没有执行**这条命令，直接"跳了"——即排程越过 `OT_GOTO_COUPLE` 继续跑下一条。

"跳了" 与第 307 轮 KI-487 的**执行侧跳过**（`ProcessOrders()` 的跳过循环 + `ORD-COUPLE-SKIP` 探针）是同一个动作名 ⇒ 首要怀疑就是**我自己上一轮引入的判据把这条命令判成了非法**。

## 二、猜测根因（待验证，最高优先级）

**猜测 R308-A（可能性最高）**：`R3RCoupleOrderFollowsStation()` 只认 `OT_GOTO_STATION` 作前驱。

```
src\train_cmd.cpp:714-730 一带
	const Order *couple = v->GetOrder(idx % num);
	if (couple == nullptr || !couple->IsType(OT_GOTO_COUPLE)) return false;
	... 往回跳过 OT_IMPLICIT ...
	return p->IsType(OT_GOTO_STATION);      // ← 只有车站算合法前驱
```

而 R3R 的车库挂接在订单层是**单条命令 + 一个属性**，不是两条命令：

```
src\train_cmd.cpp:10843  a->current_order.GetCoupleIsDepot() && IsRailDepotTile(a->tile) && ...
src\train_cmd.cpp:13417  if (order.GetCoupleIsDepot()) { ... }
src\train_cmd.cpp:14942  v->current_order.IsType(OT_GOTO_COUPLE) && v->current_order.GetCoupleIsDepot() && ...
```

⇒ 若玩家的排程是「`GOTO_DEPOT`（或其它非车站命令）→ `GOTO_COUPLE(库)`」，或者这条车库挂接命令是**排程第一条**（前驱卷绕到自己 / 无前驱），我的判据一律返回 false ⇒ `ProcessOrders()` 跳过它 ⇒ 列车直接跑下一条。**与被报告的现象逐字吻合。**

支撑：第 307 轮自己的复测判据第 5 条写的就是「车库挂接（KI-481 `depot-paired`）判据不退化」——本轮正是没守住那一条。

**猜测 R308-B（次高）**：不是跳过循环干的，而是 `SwitchToNextOrder()` 里我把 `OT_GOTO_COUPLE` 拆出来后 `break` 的路径在**车库场景**下另外走了一条本不该跳的逻辑（look-ahead 层，只影响寻路/刹车，不改排程，但会让列车不去车库）。判别很容易：看日志有没有 `ORD-COUPLE-SKIP site=process`。

**猜测 R308-C**：`R3RShouldHoldForCouple()` / `R3RTrainStopsAtWaypoint()` 被 R307-1 判据连带削弱，导致到库判定失效（不是"跳过"，而是"没去"）。

## 三、判别判据（拿到日志就能定死）

| 日志形态 | 结论 |
|---|---|
| `ORD-COUPLE-SKIP site=process veh=<该车> from=<库命令索引> to=<下一条>` | 猜测 R308-A 成立：判据把车库挂接判非法 |
| 只有 `ORD-COUPLE-SKIP site=lookahead`，没有 `site=process` | R308-B：look-ahead 层 |
| 完全没有 `ORD-COUPLE-SKIP`，但 `ORD-AFTER-COUPLE` / 下一条命令的目标出现在日志里 | R308-C 或更早期的索引推进（不是第 307 轮引入） |

## 四、需要玩家提供

1. `build\R3R_debug.log`（第 307 轮 exe 产生的那份，从报告现场开始到列车跳过为止的一段就够）。
2. 出事那列车的**排程截图或口述**（尤其是"挂接"前一条命令到底是什么：车站 / 车库 / 路点 / 无）。

## 五、当前处置

- 未改码。先把判据边界与玩家口径对齐再动手（车库到底算不算合法前驱，属 R307-1 的语义范围，必须由玩家拍板，不能我自己放宽）。
- 冲突点明写在这里：玩家第 307 轮原话是「**只有 GOTO STATION 后面能跟 GOTO COUPLE**」，而 R3R 的车库挂接是 R3R 核心功能，若其排程不是以车站开头，两条规则会直接撞车。

## 六、取证记录

### 6.1 结论先行（2026-10-26，代码级定案，不需要日志）

**猜测 R308-A 成立，且是 100% 结构性必然，不是概率问题。** 第 307 轮的 R307-1 判据把
`OT_GOTO_COUPLE` 的合法前驱硬编码成"只有 `OT_GOTO_STATION`"，而 R3R 的**车库挂接**
（"前往挂接的车库"）在订单层就是一条 `OT_GOTO_COUPLE` + `GetCoupleIsDepot()==true` 的
命令，它前面通常**不是**车站 ⇒ 判据恒假 ⇒ `ProcessOrders()` 的跳过循环必然把它跳掉。

### 6.2 三条代码锚点（逐字）

**锚点 1 —— 判据唯一放行条件（只有车站）**

```
src\train_cmd.cpp:708  bool R3RCoupleOrderFollowsStation(const Vehicle *v, VehicleOrderID idx)
src\train_cmd.cpp:714    const Order *couple = v->GetOrder(static_cast<uint>(idx) % num);
src\train_cmd.cpp:715    if (couple == nullptr || !couple->IsType(OT_GOTO_COUPLE)) return false;
src\train_cmd.cpp:717-723  往回跳过 OT_IMPLICIT
src\train_cmd.cpp:721    if (!order->IsType(OT_IMPLICIT)) return order->IsType(OT_GOTO_STATION);   // ← 唯一放行条件
```

判据**完全没看** `couple->GetCoupleIsDepot()` —— 车库型与车站型在这行被一视同仁。

**锚点 2 —— 执行侧跳过循环（无条件调用上述判据）**

```
src\order_cmd.cpp:4948    if (v->type == VehicleType::Train) {
src\order_cmd.cpp:4950      while (order != nullptr && order->IsType(OT_GOTO_COUPLE) &&
src\order_cmd.cpp:4951              !R3RCoupleOrderFollowsStation(v, v->cur_real_order_index)) {
src\order_cmd.cpp:4954-4958   三索引一起前进（real / implicit / timetable）
src\order_cmd.cpp:4959        R3RProbeCoupleOrderSkipped(v, from, to);    // → ORD-COUPLE-SKIP site=process
```

判据为假 ⇒ 索引前进一行、重新取命令 ⇒ **列车永远看不到这条车库挂接命令**。

**锚点 3 —— 车库挂接是"单条命令 + 属性"，没有独立的前驱 `GOTO_DEPOT`**

```
src\order_gui.cpp:1480  case OT_GOTO_COUPLE: {
src\order_gui.cpp:1482    if (order->GetCoupleIsDepot()) {
src\order_gui.cpp:1483      AppendStringInPlace(line, STR_ORDER_GO_TO_COUPLE_DEPOT, v->type, order->GetDestination().ToDepotID());
```

即 UI 上那条「前往车库 X 挂接」就是**一条** `OT_GOTO_COUPLE`，目的地字段直接填 `DepotID`
（`src\order_cmd.cpp:1460-1461` 校验 `GetCoupleIsDepot()` 时校验 `Depot::IsValidID`）。
它不会在排程里生成一条前置的 `GOTO_DEPOT`。

### 6.3 必被跳过的三种排程形态（覆盖玩家场景的全部可能）

| 排程形态 | `R3RCoupleOrderFollowsStation` 结果 | 实际行为 |
|---|---|---|
| `[挂接(库)]` 单条 / 车库挂接是第一条 | prev 卷绕到 `num-1`，指向自己或最后一条 ≠ 车站 ⇒ **false** | **必跳** |
| `[GOTO_DEPOT, 挂接(库)]` | prev = `OT_GOTO_DEPOT` ⇒ **false** | **必跳** |
| `[GOTO_WAYPOINT, 挂接(库)]` | prev = `OT_GOTO_WAYPOINT` ⇒ **false** | **必跳** |
| `[GOTO_STATION, 挂接(库)]` | prev = `OT_GOTO_STATION` ⇒ true | 不跳（偶然正确） |
| `[GOTO_STATION, 挂接(站)]` | true | 不跳（R307-1 的目标场景） |

⇒ 只要玩家那列车的车库挂接命令前面不是车站（这是车库挂接最自然的排法），行为必然是"跳过"，
与玩家原话"不执行前往挂接的车库分支就跳了那个挂接命令"逐字一致。

### 6.4 自己上一轮的失误（必须记下）

第 307 轮实现 R307-1 时，我把"合法前驱"直接写成 `OT_GOTO_STATION` 单一类型，**没有把
`GetCoupleIsDepot()` 这一维纳入考虑**；而同一轮我自己写进复测清单的第 5 条恰恰是
「车库挂接（KI-481 `depot-paired`）判据不退化」。判据写得过窄 + 复测清单没自检 ⇒
玩家说的"打地鼠"是我造成的。教训：**任何"收紧命令合法性"的判据，必须先枚举该命令类型的
全部子形态（`GetCoupleIsDepot()` / `GetCoupleLoad()` / 是否 yard …），逐个确认放行/拒绝。**

### 6.5 修法候选（待玩家拍板，不得自行放宽）

| 选项 | 改动 | 影响面 | 备注 |
|---|---|---|---|
| **(a) 推荐** 车库挂接豁免 | `R3RCoupleOrderFollowsStation()` 取到 `couple` 后加一行<br>`if (couple->GetCoupleIsDepot()) return true;` | 一处改动，**所有**调用点统一放行车库挂接（执行侧跳过 / look-ahead / 路点停车三处） | 语义最自洽：车库挂接的目的地是车库，本质是"去车库"，与"车站装完货自动挂接"不是同一类衔接。副作用：`GOTO_WAYPOINT → 挂接(库)` 会恢复第 307 轮被撤销的"路点停车"行为（KI-453 原语义），我认为这才是对的 |
| (b) 只在跳过循环豁免 | `src\order_cmd.cpp:4950` 加 `!order->GetCoupleIsDepot() &&` | 影响面最小，但 **look-ahead 仍把车库挂接判成非法**，可能出现"命令保留了但列车不为它寻路/不刹车"的半坏状态 | 不推荐：修一半等于埋雷 |
| (c) 合法前驱加 `OT_GOTO_DEPOT` | 判据改成 `IsType(OT_GOTO_STATION) \|\| IsType(OT_GOTO_DEPOT)` | 车库挂接前面若不是车库（单条 / 路点 / 循环卷绕）仍然被跳 | 不推荐：按形态表只能修一格 |

**玩家原话「只有 GOTO STATION 后面能跟 GOTO COUPLE」需要一次口径确认**：这句话是否本意就是
"连车库挂接也要求前面是车站"？按 R3R 的既有功能（KI-481 depot-paired 车库挂接是核心玩法）
我判断不是，但这条规则是玩家定的，我不能自己放宽 —— 所以停在这里等一句"选 a"。

### 6.6 未确认为日志事实的部分

- 玩家那列车的**具体排程形态**还没看到（哪一格是车库挂接、前一条是什么）。按 6.3，结论对
  三种形态全都成立，所以不影响定案，但复测时需要它来对照 `ORD-COUPLE-SKIP` 的 from/to。
- 日志未取：本次没有要求玩家提供日志即定案，因为 6.2 的代码路径是**无条件**的，与运行期数据无关。

## 七、玩家拍板：撤销 R307-1 全部限制（2026-10-26）

玩家原话：
> 「算了，把我之前说的那个前面必须跟车站命令的限制删掉吧」

口径解读（不再追问，按此执行）：

| 项 | 处置 |
|---|---|
| **R307-1**（"只有 GOTO STATION 后面能跟 GOTO COUPLE"） | **整条撤销**。执行侧跳过 `OT_GOTO_COUPLE` 的逻辑全部删除 ⇒ 车库挂接、路点后挂接、单条挂接命令一律恢复可执行。 |
| **R307-2**（车站命令后接挂接命令必须完全停稳才跳排程） | **保留**。玩家只删"前面必须跟车站命令"这条限制，第 2 条是独立规则，未要求撤销。`R3RCoupleOrderFollowsStation()` 仍为 R307-2 服务（它本来就是"下一条是挂接命令吗"的判据）。 |
| **KI-453 路点停车（第 286 轮）** | **随之恢复**。第 307 轮撤销它的唯一理由是"路点后面的挂接命令已不合法，所以不必在路点停车等挂接"；合法性限制没了，撤销理由随之消失。`R3RTrainStopsAtWaypoint()` / `R3RWaypointOrderStopsForCouple()` 回到"下一条是 OT_GOTO_COUPLE 就在路点停车"。 |

### 7.1 落地清单（已核对行号）

| # | 文件 | 位置 | 动作 |
|---|---|---|---|
| 1 | `src\order_cmd.cpp` | `:4817-4821` | 删掉 `R3RCoupleOrderFollowsStation` / `R3RProbeCoupleOrderSkipped` 前向声明 |
| 2 | `src\order_cmd.cpp` | `:4943-4963` | 删掉 `ProcessOrders()` 内的跳过循环（`site=process` 的唯一来源） |
| 3 | `src\train_cmd.cpp` | `:654-656` | 删掉为 `R3RTrainStopsAtWaypoint()` 准备的前向声明 |
| 4 | `src\train_cmd.cpp` | `:682-684` | `R3RTrainStopsAtWaypoint()` 恢复 KI-453 判据：下一条真实命令是 `OT_GOTO_COUPLE` ⇒ 停车 |
| 5 | `src\train_cmd.cpp` | `:795-797` | `R3RWaypointOrderStopsForCouple()` 同上恢复 |
| 6 | `src\train_cmd.cpp` | `:687-706` | 文档注释改写：只描述 R307-2（不再是"shared predicate of both rules"） |
| 7 | `src\train_cmd.cpp` | `:16010-16032` | `SwitchToNextOrder` 的 `OT_GOTO_COUPLE` 分支删掉跳过块（`site=lookahead` 的唯一来源），恢复"选中即前往" |
| 8 | `src\train_cmd.cpp` | `:8599-8612` | 删掉 `R3RProbeCoupleOrderSkipped()` 探针本体 |
| 9 | `src\train_cmd.cpp` | `R3REDGE_COUPLESKIP` | 删掉该边沿枚举（届时已无引用） |

### 7.2 保留但不再承担 R307-1 的东西

- `R3RCoupleOrderFollowsStation()`：函数体与"跳过 `OT_IMPLICIT` 找前驱、要求前驱是车站"的语义**完整保留** —— 这正是 R307-2 要判的东西（车站命令后面跟的是挂接命令才需要停稳）。只是不再有人拿它当"合法性"用。
- `R3RStationOrderHoldsForCouple()` 及其三处落点（`AdvanceOrdersFromVehiclePosition` loading/站台、`TrainLocoHandler` 的 `OT_LEAVESTATION`）：全部保留。

### 7.3 复测判据（本轮改完后）

1. **正判据**：玩家现场那列车（排程里"前往车库 X 挂接"）必须真的去车库并挂上 ⇒ 日志出现 `COUPLE-OK loco=…`，且 `ORD-COUPLE-SKIP` 一行都不再出现（该探针已被删除，只能看"命令是否被执行"）。
2. `[车库, 挂接(库)]` / `[路点, 挂接(库)]` / `[挂接(库)]` 单条三种排法均能执行。
3. `GOTO_WAYPOINT → GOTO_COUPLE` 恢复**路点停车**（KI-453 行为）：路点格末速度归零后再出发去挂接。
4. **负判据（R307-2 不得回退）**：`GOTO_STATION → GOTO_COUPLE` 时，车站命令仍必须完全停稳（`cur_speed == 0`）才推进到挂接命令；不能因为删了 R307-1 把 R307-2 一起带走。
5. 普通 `[车站, 车站, 车站]` / `[车站, 车库]` / `[路点, 车站]` 排程行为逐字不变。

## 八、构建自证（2026-10-26 12:40）

复用既有 `_tmp_inc_build.cmd`（未新建任何 `.cmd`）；纯 `.cpp` 改动，护栏判 `incremental is safe`。

| 判据 | 实测 |
|---|---|
| 日志链接行 | `[2/4] Building CXX object … order_cmd.cpp.obj` → `[3/4] … train_cmd.cpp.obj` → `[4/4] Linking CXX executable openttd.exe` |
| `build\R3R_incbuild.done` | `EXIT_CODE=0` |
| 时间戳 | `order_cmd.cpp` 12:38:38、`train_cmd.cpp` 12:39:09 → `build\openttd.exe` **12:40:47（52 253 696 B）** |
| lint | `read_lints` 两文件 0 条 |
| exe 字面量（正向） | `ORD-STOPGATE` 三处齐全：`site=leavestation` / `site=loading` / `site=platform` ⇒ R307-2 未受牵连 |
| exe 字面量（反向） | `ORD-COUPLE-SKIP` findstr exit 1、无输出 ⇒ R307-1 的探针已彻底消失 |

### 8.1 与第 307 轮的二进制差异

第 307 轮 exe：`52 253 696 B / 12:17:22`；本轮 exe：`52 253 696 B / 12:40:47`（**体积相同**，
因为删掉的字符串与新增的注释不改变镜像大小，不能用体积区分这两版 ⇒ 判据一律看 mtime）。

### 8.2 仍未做 / 仍待确认

- **游戏内复测未做**（判据见 §7.3）。核心看两条：那列车真的去车库挂上了；`ORD-COUPLE-SKIP` 一行都不会再出现（探针已删，所以只能靠"命令是否被执"判断）。
- 第 307 轮的 T-4（`R3RAutoRecordChainOrders()` 自动记录侧过滤）与 P-1 (b)（写入侧拒绝）**随 R307-1 撤销一并作废**，不再需要做。
- R307-2 在第 307 轮遗留的待复测项（`ORD-STOPGATE spd>0`→`spd=0` 时序、负判据、`ONSF_NO_STOP_*` 急刹观感）仍然有效，本轮未动它。
