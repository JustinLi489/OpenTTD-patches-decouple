# veh57 订单 5 被跳过（路点未到达即推进）— 第 369 轮

> 状态：**已定性现象 + 已定位推进区间（跨 tick、落在订单处理侧），未改任何源码**
> （2026-10-29）。本文件是唯一可信状态载体，每条结论都带 `build\R3R_debug.log` 行号锚点。
>
> 一句话结论：veh57 停在命令 4 的路点 `1706@(42,13)` 上（合法到达，`real 3→4`），
> 下一 tick 的订单处理侧把 `real` 又推到 5、并把 `current_order` 换成 `OT_GOTO_COUPLE`，
> 于是命令 5（路点 `917@(21,7)`，车从未去过）被整车跳过。**不是同一 tick 连跳两格，也不是
> "命令 4/5 同一点"。**

---

## 一、玩家原话（2026-10-29）

> 「请把视角转到 veh57 身上，veh57 在命令4指向的路点停着，没有走到命令5指向的路点
> 就认为命令5完成了，于是把命令指向了第六条」

要点拆解（后续逐条取证）：

| # | 待验事实 | 取证点 |
|---|---|---|
| F-1 | veh57 停在**命令 4** 指向的路点上 | `CRT veh=57`、`LOCO-MOVE veh=57` 最后位置、`ORD-*` |
| F-2 | 命令 5 的目标也是路点，且**没走到** | 订单表 dump（`ORD-*` / `ORD-IDX-WATCH`） |
| F-3 | 订单索引**自己前进了**（5 → 6），不是玩家手动跳 | `ORD-IDX-WATCH real=-1->N`、`ORD-PUSH`、`SKIP-*` |
| F-4 | 前进发生在"停着"状态，车没动 | `LOCO-MOVE veh=57` 在推进前后是否零行 |

## 二、现象定性（初步，待日志确认）

「没到达就被判完成」在 JGRPP/R3R 里只有四种可能通路，必须用日志把范围压到一条：

- **H-1（订单到点判据）**：`GOTO_WAYPOINT` 的完成判据按"是否**经过/接近**目标格"而非"停靠"，
  且两处路点相邻或有重叠轨道时，停在前一个路点上就同时满足后一个的判据。
- **H-2（索引推进代码）**：R3R 自己的推进逻辑（`R3RSyncDrivingOrders` / `ORD-PUSH` /
  `IncrementRealOrderIndex` / 第 264 轮 N1「cur_real 就地，若该处仍是 `OT_GOTO_COUPLE` 则 +1」）
  在停稳态多推了一格。
- **H-3（跳过逻辑误伤）**：`R3RSkipUnfireableDecoupleOrder` 一类"不可触发的订单就地跳过"
  把**路点订单**也当作不可触发（判据只看 `IsType`，没看是否 `GOTO_WAYPOINT`）。
- **H-4（到点标志被提前置位）**：`current_order.GetTravelType()` 相关的
  `UpdateOrderDistance` / `SetDestTile` 在上一站收尾时把 `dest` 设成了错误的格，导致
  "已在目标格"立刻成立。

## 三、猜测根因（**待验证，尚未实证**）

> 按用户规则「猜测的根因」必须立刻落盘，以下只是假设，验证后必须回填状态（成立/证伪）。

**猜测 G-1（已证伪，2026-10-29 第二次取证）**：原假设"命令 4、5 是同一/相邻路点，一次 tick 跳两格"。
证伪依据：命令 4 = 路点 `1706 @ 42,13`、命令 5 = 路点 `917 @ 21,7`，**相距 21+6 格**，不可能同时满足；
且 `3->4` 与 `4->5` 是两次独立推进（跨 tick，见 §5.4/§5.5）。

**猜测 G-2（部分成立，待 U-5 收口）**：原假设"借用排程下 `cur_real`/`cur_implicit` 各推一次，净 +2"。
观测到的是 `3->4`（到达命令 4 路点，合法）与 `4->5`（**跳过命令 5**，异常）两次**先后**推进，
而非同一次 +2 ⇒ "净 +2"表述不成立，但"停稳 + 排程刚归还（`n=33->7`、`tag=suppress`）后
多推一格"的方向仍成立，具体落点收窄到上一 tick 的订单处理侧（`OT_WAITING` 收尾）。

判别方法（下一步）：在 `ProcessOrders` 的 `OT_WAITING` 收尾分支与 `R3RSkipUnfireableDecoupleOrder`
家族各处成对打 `ORD-IDX-WATCH`，看 `4->5` 出现在哪个 site。

## 四、取证计划（下一步）

1. `Select-String 'veh=57'` 全部行 → 落盘到本文件第五节（只留行号 + 抬头）。
2. 抽 `ORD-` / `CRT veh=57` / `LOCO-MOVE veh=57` / `SKIP-` 四类看时序。
3. 找订单表 dump（若有 `ORD-DUMP`/`ORD-IDX-WATCH` 能读出路点坐标）确认命令 4/5 的 `dest`。
4. 若日志不足以判别 H-1，则本轮只落盘 + 加只读探针（订单索引变化打一行），不改行为。

## 五、取证记录（2026-10-29，`build\R3R_debug.log` 195 887 B @20:38:27）

### 5.1 veh=57 的订单表（`LOCO-ORD` 是 **0-based**）

`:1637`~`:1643`（在 `:1636 COUPLE-FAIL loco=57` 之后整表 dump）：

```
:1637  LOCO-ORD 0 type=6    <- OT_GOTO_WAYPOINT   命令1
:1638  LOCO-ORD 1 type=16   <- OT_GOTO_COUPLE     命令2
:1639  LOCO-ORD 2 type=1    <- OT_GOTO_STATION    命令3
:1640  LOCO-ORD 3 type=6    <- OT_GOTO_WAYPOINT   命令4
:1641  LOCO-ORD 4 type=6    <- OT_GOTO_WAYPOINT   命令5   ★被跳过的那条
:1642  LOCO-ORD 5 type=16   <- OT_GOTO_COUPLE     命令6   ★被推进到的位置
:1643  LOCO-ORD 6 type=2    <- OT_GOTO_DEPOT      命令7
```

（`order_type.h:76-95` 实测：`1=GOTO_STATION`、`2=GOTO_DEPOT`、`6=GOTO_WAYPOINT`、
`16=GOTO_COUPLE`、`9=WAITING`。）

⇒ **玩家视角的"命令4/命令5/命令6" = `real=3/4/5`，玩家的主诉与日志索引完全对得上**：
命令5 是 `OT_GOTO_WAYPOINT`，推进后 `real=5` 是 `OT_GOTO_COUPLE`。

### 5.2 时间线（同一 tick 内连推两格）

```
:1629 ORD-IDX-WATCH site=del-implicit-in  veh=57 real=3->4 impl=3->4 ... spd=5  curType=6 dest=917 dtX=21 dtY=7 vtile=42,13
:1630 ORD-IDX-WATCH site=del-implicit-out tag=deleted        veh=57 real=-1->4 ... spd=5  curType=6 dest=917 dtX=21 dtY=7
:1631 SPD-DROP veh=57 tile=42,13 prev=5 order=9 real=4 stuck=0 fp=0 prog=0 dtile=917 dt=31
:1632 DEPOT-ARR veh=57 spd=0 real=4(6) curType=9 tx=42 ty=13 destTx=21 destTy=7 tileEqDest=0
:1635 CPL-S0-CHK veh=57 tile=42,13 real=5 order=16 stuck=0 ... dtile=923
:1636 COUPLE-FAIL loco=57 order=16 tx=42 ty=13
:1643 LOCO-ORD 6 type=2
:1644 COUPLE-WAIT-OFFDEST veh=57 tile=42,13 mf=42,13 real=5 isDepot=0 destStn=2 destTile=923 onStnTile=1
:1645 ORD-IDX-WATCH site=loco-entry veh=57 real=4->5 impl=4->5 ... spd=0 curType=16 dest=923 dtX=27 dtY=7 vtile=42,13
```

读数要点：

1. **车全程没动**：`:1631`/`:1632`/`:1645` 三处 `tile` 都是 `42,13`，`spd=5→0→0`；
   `dt=31` 说明命令 5 的目标还差 31（远未到）。
2. **推进是同一 tick 内连跳两格**：`3->4`（`:1629`）与 `4->5`（`:1645`）之间没有任何
   `LOCO-MOVE veh=57`（全日志 `LOCO-MOVE` 无 veh=57 行）。
3. **推进点紧贴 `del-implicit`**：`:1629`/`:1630` 是唯一带 `tag=deleted` 的
   `DeleteImplicitOrder()` 探针，`real` 正是从这里开始变。
4. **`real=4` 时 `curType=9`（`OT_WAITING`）**（`:1631`/`:1632`）——命令 5 是路点，
   但当前订单类型却是"等待"，说明**隐式订单把真实订单的位置顶掉了**，索引/订单已错位。

### 5.3 到达了哪、没到达哪

**已定性（第 369 轮第二次取证）：E-1 / E-2 双双证伪，玩家口述完全成立。**

决定性的 4 行（`Select-String 'veh=57 real=[0-9]+->3 '` 的最后 4 条）：

```
ORD-IDX-WATCH site=tick-out        veh=57 real=4->3 impl=4->3 tt=4->3 n=33->7 ord=...CF14E0->...CF0540 spd=2  curType=6 dest=1706 dtX=42 dtY=13 vtile=35,12
ORD-IDX-WATCH site=tick-in         veh=57 real=4->3 ...                                   spd=2  curType=6 dest=1706 dtX=42 dtY=13 vtile=35,12
ORD-IDX-WATCH site=del-implicit-in veh=57 real=4->3 ...                                   spd=60 curType=6 dest=1706 dtX=42 dtY=13 vtile=42,13
ORD-IDX-WATCH site=del-implicit-out tag=suppress veh=57 real=4->3 ...                     spd=60 curType=6 dest=1706 dtX=42 dtY=13 vtile=42,13
```

读数：

- **命令 4（`real=3`）= 路点 `1706` @ `42,13`**，`n=33->7` + `ord=...CF14E0->...CF0540`
  = veh57 从**借用的 33 条表**拿回**自己的 7 条表**（解挂/交接后的归还，`tag=suppress` 是
  `GVF_SUPPRESS_IMPLICIT_ORDERS` 分支）。车随后由 `35,12` 开到 `42,13`，**正停在命令 4 的路点上**
  ⇒ 与玩家原话「veh57 在命令4指向的路点停着」逐字一致。
- **命令 5（`real=4`）= 路点 `917` @ `21,7`**：`:1629`/`:1631`/`:1632` 三处 `dest=917 dtX=21 dtY=7`。
- 车全程停在 `42,13`（`vtile`/`tx,ty` 恒 `42,13`），离命令 5 目标 `21,7` 约 21 格 x + 6 格 y（`dt=31`）。

⇒ **E-1 证伪**（两条命令不是同一点：`1706@42,13` vs `917@21,7`）；
**E-2 证伪**（`dest` 探针读的是 `cur_real_order_index` 对应的真实订单，`real=3` 时确为 `1706`）。

### 5.3b `real=4` 时 `curType=9` 是什么

`src\order_type.h:86` = `OT_WAITING = 9`，**上游原生**的"停靠等待"类型（不是 R3R 新增）。
所以 `:1631`/`:1632` 的 `order=9` 是"车刚到命令 4 的路点、进入 waiting"的正常残留：
真实订单索引已 4（命令 5），`current_order` 还是刚到达那条的 waiting 态 ⇒ **U-2 判为正常现象，
不是"隐式订单顶掉"**（对照 `:1632 DEPOT-ARR real=4(6) curType=9`：括号里的 6 才是
`GetOrder(4)->GetType()` = 路点，`curType=9` 是 `current_order.GetType()`）。

### 5.4 排除项：`del-implicit` 不是推进者（已读码核对）

`del-implicit-in/out` 探针在 `src\vehicle.cpp:3467` / `:3483` / `:3505`，宿主是
`Vehicle::DeleteUnreachedImplicitOrders()`（`src\vehicle.cpp:3454-3506`）。全文核对：

- 两个出口只做 `cur_implicit_order_index = cur_real_order_index`（`tag=suppress` 分支，`:3474`）
  或 `DeleteOrder()` + `cur_implicit_order_index` 前移（`tag=deleted` 分支，`:3493-3501`）；
- **函数体没有任何一处写 `cur_real_order_index`**。

⇒ `:1629 real=3->4` 只是"第一个看见 3 已变 4 的探针"，**真正的 3→4 发生在该调用点之前**。
`ORD-IDX-WATCH` 是快照差分式（`src\train_cmd.cpp:2496` 起，缓存上次 `real/impl/tt/ord/count/artic`，
任一变化才写一行），因此每一行的 `a->b` 都是**一次真实变化**，不是一次推进的两次打印
⇒ **U-3 解决**：`3->4` 与 `4->5` 是两次独立推进。

### 5.5 4→5 的落点：跨 tick 边界，落在 `ProcessOrders` 一侧

日志顺序把窗口夹得很窄：

```
:1631 SPD-DROP      veh=57 spd 5→0   real=4 order=9
:1632 DEPOT-ARR     veh=57 real=4(6) curType=9          <- 本 tick，仍是 4
:1633 CPLSPD        veh=57       （UpdateSpeed 内，spd=0）
:1634 MAXSPD-BREAK  veh=57       （UpdateSpeed 内，spd=0）
:1635 CPL-S0-CHK    veh=57 real=5 order=16              <- 已经 5 且 current_order=GOTO_COUPLE
:1645 ORD-IDX-WATCH site=loco-entry veh=57 real=4->5    <- 下一 tick 的入口才被探针看见
```

- `site=loco-entry` 是 `TrainLocoHandler` **入口**（`:1645`），它看到的是"上一 tick 结束时已是 5"
  ⇒ 推进**不在本 tick 的 `TrainLocoHandler` 主体**里；
- `:1635 CPL-S0-CHK` 位于停驶分支（`src\train_cmd.cpp:23399` 的 `if (j < adv_spd)` →
  `if (cur_speed == 0)` → `if (current_order.IsType(OT_GOTO_COUPLE))`，探针在 `:23432`），
  它已经看到 `real=5` 且 `order=16` ⇒ 上一 tick 结束时 `current_order` **已从 9 换成 16**；
- 因此推进 + 换订单**同时**发生在上一 tick 的订单处理侧（`ProcessOrders` / 等待态收尾），
  候选：`OT_WAITING` 收尾时的索引推进被走了两次（到达路点一次 3→4，等待结束又一次 4→5）。

### 5.6 待验证项

| 编号 | 待验 | 状态 |
|---|---|---|
| U-1 | 命令 4 / 命令 5 的路点坐标 | **已解决**：4=`1706@42,13`，5=`917@21,7` |
| U-2 | `real=4` 时 `curType=9` 是否异常 | **判为正常**：`OT_WAITING` 是原生等待态，属"已到达那条路点"的残留 |
| U-3 | `3->4`/`4->5` 是两次推进还是一次 | **已解决**：探针是差分式，两次真实推进 |
| U-4 | 是否曾到过 `21,7` | **已解决**：`vtile`/`tx,ty` 恒 `42,13`，未到过 |
| U-5 | 上一 tick 里是谁把 `real 4→5` 且把 `current_order` 换成 `OT_GOTO_COUPLE` | **已解决**（见 §5.7，具体语句已钉到 `vehicle.cpp:4007`） |

### 5.7 U-5 定案：`real 4→5` 的确切语句（纯代码交叉验证，未改任何源码）

四个锚点逐字对上日志，链条闭合：

**① 到达路点进入等待 — `src\train_cmd.cpp:19645-19670`（`TrainEnterStation` 的 `Waypoint::IsExpected` 分支）**

```cpp
19658:	if (Waypoint::IsExpected(bst)) {
19659:		consist->DeleteUnreachedImplicitOrders();   // ← 日志 :1629/:1630 的 del-implicit 探针就在此行内
19660:		UpdateVehicleTimetable(consist, true);
19664:		consist->current_order.MakeWaiting();       // ← current_order 变成 OT_WAITING(9)
19665:		consist->current_order.SetNonStopType(ONSF_NO_STOP_AT_ANY_STATION);
19667:		consist->cur_speed = 0;                     // ← 日志 :1631 SPD-DROP veh=57 prev=5 正是这里
19669:		return;                                     // ★ 这一支**从不**推进 cur_real_order_index
```

⇒ real 3→4 是**进入本函数之前**的到站推进（常规 `IncrementRealOrderIndex`），
本函数只负责"删隐式订单 + 设 WAITING + 停车"。与 `:1629 real=3->4`、`:1631 order=9 real=4`、
`:1632 DEPOT-ARR real=4(6) curType=9` 三条读数逐字吻合（括号 6 = `GetOrder(4)->GetType()` = 路点）。

**② 下一 tick 进入等待收尾 — `src\train_cmd.cpp:23137-23153`**

```cpp
23137:	if (consist->current_order.IsType(OT_WAITING) && consist->reverse_distance == 0) {
23139:		consist->HandleWaiting(false, true);        // ← stop_waiting=false, process_orders=true
23140:		if (consist->current_order.IsType(OT_WAITING)) return true;
```

**③ 收尾里推索引 — `src\vehicle.cpp:3991-4012`（`Vehicle::HandleWaiting` 的 `OT_WAITING` 分支）**

```cpp
3995:	uint wait_time = std::max<int>(this->current_order.GetTimetabledWait() - this->lateness_counter, 0);
3997:	if (!stop_waiting && this->current_order_time < wait_time && ... ) return;   // 路点无 timetabled wait ⇒ wait_time=0 ⇒ 不 return
4007:	this->IncrementImplicitOrderIndex();   // ★★ real 4→5 就发生在这一行内部
4008:	this->current_order.MakeDummy();
4010:	if (process_orders) ProcessOrders(this);   // ← current_order 重算成 real=5 的 OT_GOTO_COUPLE(16)
```

**④ 推两格的机制 — `src\vehicle_base.h:1113-1132`（`IncrementImplicitOrderIndex`）**

```cpp
1113:	void IncrementImplicitOrderIndex()
1116:		if (type==Train && current_order.IsType(OT_GOTO_COUPLE)) return;   // R3R 只给 GOTO_COUPLE 上了锁
1118:		if (this->cur_implicit_order_index == this->cur_real_order_index) {
1120:			this->SkipToNextRealOrderIndex();   // ★ real 4→5
1121:		}
1126:		do { cur_implicit_order_index++; ... } while (impl != real && !IsType(OT_IMPLICIT));
```

**⇒ 结论句**：到达路点时 `cur_implicit_order_index` 已被追平到 `cur_real_order_index`（都=4），
所以 `HandleWaiting` 收尾的 `IncrementImplicitOrderIndex()` 走了"impl==real ⇒ 连推 real"分支，
`cur_real_order_index` 4→5，命令 5（路点 `917@21,7`）**从未被解析成 `current_order`、车从未出发**。
随后 `MakeDummy()` + `ProcessOrders()` 把 `current_order` 换成 real=5 的 `OT_GOTO_COUPLE(16)`，
与 `:1635 CPL-S0-CHK real=5 order=16` / `:1636 COUPLE-FAIL loco=57` / `:1644 COUPLE-WAIT-OFFDEST destTile=923`
三条读数完全一致。

**根因归属（两层叠加，均非本场景独有）**

- **R1 · `impl==real` 是 R3R 造出来的**：`DeleteUnreachedImplicitOrders()` 的 `suppress` 分支直接
  `cur_implicit_order_index = cur_real_order_index`（`vehicle.cpp:3474`）、`deleted` 分支把 impl 前移到 real；
  且 `IncrementRealOrderIndex()`（`vehicle_base.h:1140-1150`）在 impl==real 时走
  `IncrementImplicitOrderIndex()` —— 二者叠加使"impl 恰好等于 real"成为 R3R 链的**常态**。
- **R2 · 上游 `OT_WAITING` 的收尾假定 impl 落后 real**：原生路径到站时中间夹着 `OT_IMPLICIT`，
  收尾推 impl 到 real 就自然停住，`IncrementImplicitOrderIndex()` 的 1120 分支在上游几乎不会被走到。
  R3R 把隐式订单删/追平后，该分支变成**每次路点等待收尾都必然触发** ⇒ 多推一格。

**修复候选（玩家 2026-10-29 拍板「先用 P1 试试」）**

| 代号 | 修法 | 影响面 | 评价 |
|---|---|---|---|
| P1 | 路点等待收尾**不推格**（`Vehicle::HandleWaiting` 的 `OT_WAITING` 分支判"是否路点等待"，是则跳过 `IncrementImplicitOrderIndex()`） | 只影响路点等待 | **已采用**：仅动 `vehicle.cpp`，等待时长语义不动 |
| P2 | `vehicle.cpp:4007` 换成只推 implicit 的原语 | 全部 `OT_WAITING`（站台/车库/路点） | 治根，但需回归站台等待 |
| P3 | `vehicle_base.h:1120` 加"路点/未到点不跳过"守卫 | 全局索引推进 | 不推荐，点散且易误伤 |

### 5.8 P1 实现（2026-10-29，仅 `src\vehicle.cpp`，未碰 `src\*.h`）

**落点与原候选的差异（重要）**：P1 没有改 `train_cmd.cpp:19664` 的进等待分支，而是把"不推格"做在
**收尾侧**（`Vehicle::HandleWaiting` 的 `OT_WAITING` 分支）。原因：进等待分支必须保留 `MakeWaiting()`，
否则会丢掉路点的 timetabled wait（`23137-23153` 的二次等待分支也依赖等待态）；而在收尾侧跳过那一步
**与"把 impl 退回一格"数学等价**（`impl==real` 时该调用本来就是空操作 + 一次越界的 `SkipToNextRealOrderIndex`），
且其余动作（`UpdateVehicleTimetable(this,false)` / `MakeDummy()` / `ProcessOrders()` / `force_proceed`）全部不动。

**判据实现（无需车辆标志位、无需跨 TU 登记表）**：新增文件内
`static bool R3RIsWaypointWait(const Vehicle *v)`＝
`type == VehicleType::Train` && `current_order.IsType(OT_WAITING)` &&
`Waypoint::IsValidID(current_order.GetDestination().ToStationID())`。
依据 `Order::MakeWaiting()` 保留 `dest`：路点等待的 `dest` 指向刚到达的路点，站台/车库等待指向车站/车库，
因此天然区分；也就不存在"用车辆 index 做键的登记表在 index 复用后残留"的风险。为用 `Waypoint::IsValidID`
新增 `#include "waypoint_base.h"`（`vehicle.cpp` 原本未包含）。

**代码形态**

```cpp
/* OT_WAITING 收尾 */
UpdateVehicleTimetable(this, false);
if (R3RIsWaypointWait(this)) {
    R3RDbgWrite("WP-WAIT-KEEP veh=%d real=%d impl=%d dest=%d n=%d\n",
            (int)this->index.base(), (int)this->cur_real_order_index,
            (int)this->cur_implicit_order_index,
            (int)this->current_order.GetDestination().base(), (int)this->GetNumOrders());
    InvalidateVehicleOrder(this, 0);   // 补 IncrementImplicitOrderIndex 末尾漏掉的刷新
} else {
    this->IncrementImplicitOrderIndex();
}
this->current_order.MakeDummy();
```

**为什么是空操作而非"补推一次"**：路点到达时 `real` 已经指向下一条（本例 4 = 命令 5 `917@21,7`），
`impl` 被 `suppress` 追平到 4；上游那一步的语义是"把 impl 从到站那条的隐式位走到 real"。`impl==real`
时它无事可做，正确动作就是**什么都不做**，让 `ProcessOrders()` 从 `real=4` 解析命令 5。

**探针判读**：正常收口应出现 `WP-WAIT-KEEP veh=57 real=4 impl=4 dest=1706 n=7`；站台 timetabled wait
与车库 halt（`vehicle.cpp:2849` 那条 `ODATFB_HALT` 分支）**不得**出现该行。

**构建自证**：入口 `R3R_fullbuild.cmd inc detach`（复用既有脚本，未新建 `.cmd`）；
护栏 `build\R3R_incbuild.guard.log` = `GUARD: incremental is safe`；`build\R3R_incbuild.done` = `EXIT_CODE=0`
（2026-10-29 21:21:30）；日志尾部 `[2/3] Building CXX object ...\src\vehicle.cpp.obj` → `[3/3] Linking CXX executable openttd.exe`；
`error C*`/`fatal error`/`FAILED:`/`build stopped` 计数 0；`src\vehicle.cpp` 21:17:03 → `vehicle.cpp.obj` 21:19:32
（70 081 214 B）→ `build\openttd.exe` 21:21:09（52 408 320 B）；exe 内命中 `WP-WAIT-KEEP`；`read_lints` 0 条。

**坑（本轮踩到，记录备查）**：首次 `Start-Process ... inc detach` 后立刻查 `R3R_incbuild.done` 看到
`EXIT_CODE=0` 且 exe 有更新，但那是**上一轮 20:32 的构建**（早于本轮 21:17 的编辑）；
判据必须看 `vehicle.cpp.obj` 的 mtime 是否晚于 `vehicle.cpp`，不能只看 done 与 exe 存在/更新。

### 5.9 P1 实机复测反馈（第 370 轮，2026-10-29）

**玩家原话**

> 「见 build/R3R_debug.log，veh57 排程没有神秘跳过了，变成了卡在路点显示"等待放行（时刻表）"」

**已确认**

1. P1 达到设计目的的一半：**索引不再被多推一格**（玩家观察「排程没有神秘跳过了」），
   说明 `IncrementImplicitOrderIndex()` 的连带 `SkipToNextRealOrderIndex()` 已被挡住。
2. 新症状：车停在路点、**不出发**，车辆状态显示「等待放行（时刻表）」
   ⇒ `current_order` 仍处于 `OT_WAITING`（等待态没有被收尾），不是索引问题而是**等待态退出条件**问题。

**疑点 REG-370-A（取证失败，必须先解决）**

| 项 | 期望 | 实测 |
|---|---|---|
| 日志含 veh=57 | 应有大量 `CRT veh=57` / `LOCO-MOVE veh=57` | `build\R3R_debug.log` 全文 **0 行** 含 `veh=57` |
| 日志含 P1 探针 | 应有 `WP-WAIT-KEEP veh=57 real=4 impl=4 dest=1706 n=7` | 全文 **0 行** 含 `WP-WAIT-KEEP` |
| 日志规模/时间 | 覆盖本次复测 | 183 197 B / 2026-10-29 21:25:53（exe 为 21:21:09 ⇒ 时间窗对得上） |
| 日志活跃车号 | veh=57 | veh=48 / 54 / 27 / 51，末尾 tick=14539x，`CPL-PAIRSCAN act=54`、`COUPLE-DEST-EMPTY loco=54` |

⇒ **当前这份日志不包含玩家所指的现场**（可能是另一段会话/或在日志停止写入之后才复现）。
需要玩家提供：①确认 `build\openttd.exe` 就是 2026-10-29 21:21 那版（exe 大小 52 408 320 B）；
②重跑并**在 veh57 卡住时原地等 10 秒以上**再退出游戏，确保日志落盘；③若日志被覆盖，请复现后立刻备份。

**猜测根因（待验证，先落盘）**

`Vehicle::HandleWaiting()` 的 `OT_WAITING` 分支在真正收尾前有三道门：
`IsType(OT_WAITING)` → `ConditionalOrderWait` 未置位或 `stop_waiting` → `wait_time == 0`。
P1 只动了收尾动作内部的 `IncrementImplicitOrderIndex()`，**没有动门的判据**，
因此若路点订单的 `GetTimetabledWait()` 非 0（或 `lateness_counter` 为负导致 `wait_time > 0`），
车就会一直在路点上等「时刻表」——这与「等待放行（时刻表）」的状态文本吻合。
待验证：路点订单的 `GetTimetabledWait()`/`lateness_counter`/`vehicle_flags` 实际读数，
以及 P1 之前为何没等（当时多推一格后直接离开了等待态，把"本该等"掩盖成了"跳过一条命令"）。

**待验证项**

| 编号 | 待验 | 状态 |
|---|---|---|
| U-6 | veh57 路点订单的 `GetTimetabledWait()` 与 `lateness_counter` | 待现场日志 |
| U-7 | 「等待放行（时刻表）」对应哪个状态串/判据（`ConditionalOrderWait` vs timetabled wait） | 待查码 |
| U-8 | 本案路点是否本就设了等待时间（若设了，等是正确行为，问题变成"玩家期待不等待"） | 待玩家确认 |

### 5.9.1 代码侧定位（纯静态交叉验证，未改动行为）

`Vehicle::HandleWaiting()`（`vehicle.cpp:4007-`）的 `OT_WAITING` 分支结构（读码逐行）：

```cpp
uint wait_time = std::max<int>(current_order.GetTimetabledWait() - lateness_counter, 0);        // 4011
if (!stop_waiting && current_order_time < wait_time
    && current_order.GetLeaveType() != OLT_LEAVE_EARLY) return;                                 // 4013 门①
if (!stop_waiting && process_orders && ShouldVehicleContinueWaiting(this)) return;              // 4016 门②
... vehicle_flags.Reset(ConditionalOrderWait); UpdateVehicleTimetable(this,false);              // 4021-4022
```

- **门①（时刻表等待）**：`Order::GetTimetabledWait()`（`order_base.h:1022`）是
  `IsWaitTimetabled() ? wait_time : 0` —— **未显式设等待时间时恒为 0**，门①不可能拦住。
  故「等待放行（时刻表）」不来自门①（除非该路点订单确实设了等待时间，见 U-8）。
- **门②（条件订单等待）**：`ShouldVehicleContinueWaiting()`（`vehicle.cpp:3924-3939`）：
  ```cpp
  if (v->vehicle_flags.Test(ConditionalOrderWait) && v->tick_counter % 32 != 0) return true;   // 3929 节流
  if (v->cur_implicit_order_index < v->GetNumOrders()
      && v->GetOrder(v->cur_implicit_order_index)->IsType(OT_IMPLICIT)) return false;         // 3932
  bool loop = AdvanceOrderIndexDeferred(v, v->cur_implicit_order_index + 1) == v->cur_implicit_order_index;  // 3935
  FlushAdvanceOrderIndexDeferred(v, loop);
  if (loop) v->vehicle_flags.Set(ConditionalOrderWait);                                        // 3937
  return loop;
  ```
  **判据显式拿 `cur_implicit_order_index` 当"当前订单位"**（`impl+1` 起步找下一可实现订单，
  若绕回 `impl` 本身即判 `loop`）。P1 改的恰恰是 `impl` 的推进方式 ——
  **这是"卡在路点等放行"的头号嫌疑（猜测根因 R-370-1）**：
  `impl` 停在 4（=命令 5）后，`AdvanceOrderIndexDeferred(v, 5)` 的循环判定与上游常态不同，
  一旦被误判为 loop，就会置 `ConditionalOrderWait` 并每 tick 走 3929 的"恒 true"快路径，
  车永远不出发、状态显示条件/时刻表等待。
- 「等待放行（时刻表）」的中文串在 `src\lang\simplified_chinese.txt` 里**搜不到**（拆词/换词），
  待按状态枚举反查（U-7）。

**下一步（本轮实施）**：给门①/门②各加一行只读探针 `WP-WAITHOLD gate=time|cond veh= real= impl= n=
wtime= cot= ltype= cow=`（节流 `tick_counter % 64`，**保持原 return 顺序、零行为变更**），
以便下次复测直接读出是哪道门、以及 `loop` 判定时的 impl/real 读数。

### 5.9.2 探针实现（2026-10-29，第 370 轮；只读、零行为变更）

- `src\vehicle.cpp` 新增文件内 `static void R3RWaypointWaitProbe(const Vehicle *v, const char *gate, uint wait_time, bool stop_waiting)`，
  位置在 `R3RIsWaypointWait()` 之后、`HandleWaiting()` 之前（`vehicle.cpp:4001-4026`）。
  先判 `R3RIsWaypointWait(v)`（只记路点等待，站台/车库停靠不记，控日志体积），再按 `v->tick_counter % 64 == 0` 节流，
  写一行：`WP-WAITHOLD gate=%s veh=%d real=%d impl=%d n=%d wtime=%u cot=%u ltype=%u cow=%d stop=%d`。
- 三个打点（`HandleWaiting()` 的 `OT_WAITING` 分支）：
  - `gate=time` → `vehicle.cpp:4041`，门①命中（`current_order_time < wait_time` 且非 `OLT_LEAVE_EARLY`）；
  - `gate=cond` → `vehicle.cpp:4050`，门②命中（`ShouldVehicleContinueWaiting()` 报 loop）；
  - `gate=pass` → `vehicle.cpp:4056`，两道门都放过、本 tick 离开该订单（其后才是 `Reset(ConditionalOrderWait)`/`UpdateVehicleTimetable`/P1 的 `WP-WAIT-KEEP`）。
- **零行为变更的两处保证**：①`ShouldVehicleContinueWaiting(this)` 仍**恰好调用一次**——门②原写法是
  `if (!stop_waiting && process_orders && ShouldVehicleContinueWaiting(this)) return;`，现改为
  `if (!stop_waiting && process_orders) { if (ShouldVehicleContinueWaiting(this)) { probe; return; } }`，
  短路顺序与副作用（`FlushAdvanceOrderIndexDeferred` + `Set(ConditionalOrderWait)`）完全一致；
  ②`gate=pass` 探针插在 `vehicle_flags.Reset()` 之前，不参与任何判定。
- 判读口径：
  - 只有 `gate=time` 反复刷 → **该路点订单确实设了等待时间**（U-8），属正确行为，应去订单窗口核对等待时间；
  - 只有 `gate=cond` 反复刷且 `cow=1` → R-370-1 成立（loop 误判，P1 把 `impl` 留在路点订单上导致 `AdvanceOrderIndexDeferred` 绕回自身）；
  - 出现 `gate=pass` 但车仍不动 → 问题不在 `HandleWaiting()`，需转向 `ProcessOrders()` / 下一张 `OT_GOTO_WAYPOINT`；
  - 完全没有任何 `WP-WAITHOLD` 行 → 根本不在 `OT_WAITING` 状态，"等待放行（时刻表）"来自别处（U-7）。

### 5.9.3 构建自证（2026-10-29，第 370 轮探针）

- 21:34 首次 `R3R_fullbuild.cmd inc detach`：`build\R3R_incbuild.done` = `EXIT_CODE=97` ⇒ 闸门拦下，
  原因是**玩家此刻正开着 `openttd.exe`（PID 17412）**（脚本保护：exe 被占用会 LNK1168）。
- 玩家关闭游戏后（`tasklist` 已无 openttd）重跑同一入口，`build\R3R_incbuild.done` = `EXIT_CODE=0`（21:53）；
- 日志 `build\R3R_incbuild.log`：`[2/3] Building CXX object ...openttd_lib.dir\src\vehicle.cpp.obj` →
  `[3/3] Linking CXX executable openttd.exe`；`error C*` / `fatal error` / `FAILED:` / `build stopped` 计数 0；
- 时间戳链：`src\vehicle.cpp` 21:32:04 → `vehicle.cpp.obj` 21:51:31 → `build\openttd.exe` 21:53:41（52 408 320 B），
  obj 与 exe 均晚于源码；
- 产物自证：`build\openttd.exe` 字节流命中新字面量 `WP-WAITHOLD`；`read_lints src\vehicle.cpp` 0 条。
- 结论：第 370 轮探针已入库，可直接交给玩家复测；本轮**只加只读探针，未改任何判定/运动/几何行为**。


### 5.10 第 370 轮首次取证：探针 0 命中（2026-10-29，纯取证）

**样本有效性**：`build\R3R_debug.log` 235 517 B / 2 224 行，写于 21:57:43；产生者 `build\openttd.exe`
21:53:41（52 408 320 B，即 §5.9.3 的探针版）⇒ 样本包含本轮探针，有效。

**命中统计**（`Select-String -SimpleMatch`）：

| 串 | 命中行数 |
| --- | --- |
| `veh=57` | 92 |
| `WP-WAITHOLD` | **0** |
| `WP-WAIT-KEEP` | **0** |

**veh=57 现场锚点（行号为 1-based）**：
- `:31` `SEGLOAD-PARK veh=57 unit=2 ubk=7 n="T8701/2-南宁-河内（嘉林）1" nbk="HXN5B-XXXX2" gid=65534 gbk=-1 seg=4 borrowed=1 prio=3`（读档停放）
- `:69/:74/:81` `ORD-IDX-WATCH site=load|settle-in|settle-out tag=load veh=57 real=-1->4 impl=-1->4 tt=-1->4 n=-1->33 ... curType=1 dest=923 dtX=27 dtY=7 vtile=17,14`
- `:307` `site=tick-in`、`:308 site=loco-entry`、`:309 site=skipgate-in`、`:310 site=post-skipgate`：`real=4 impl=4 n=33 curType=1`
- `:521` `TPR-EXIT veh=57 state=choose pair=0 tile=35,12` → `:522 RESV-SIGPASS veh=57 tile=36,12` → `:524 CRT veh=57 order=6 dir=5 origin=36,12 td=8 found=…` → `:527 RPFAIL`/`:528 CPLRES`/`:529 RESV-TERM veh=57 real=3 type=6 tile=42,13 td=8`
- `:533` `TRP veh=57 mf=35,12 real=3 type=6 dest=1706 spd=…`
- `:1224` `ORD-IDX-WATCH site=tick-in veh=57 real=3`；`:1226..:1572` `LINEND veh=57 tile=42,13 x=0..14 sig=0 near=0 spd=…`（14 次，说明车头已经停在 42,13 上反复走"线尽头"判定）
- `:1572` `LINEND veh=57 tile=42,13 x=14 sig=0 near=0 spd=5 brk=15 clamp=0 len=2 dir=5 db=0`
- `:1573` `ORD-IDX-WATCH site=del-implicit-in tag=- veh=57 real=3->4 impl=3->4 tt=3->4 n=7->7 curType=6 dest=917 dtX=21 dtY=7 vtile=42,13`
- `:1574` `ORD-IDX-WATCH site=del-implicit-out tag=deleted veh=57 real=-1->4 impl=-1->4 n=-1->7 curType=6 dest=917`
- `:1575` **`SPD-DROP veh=57 tile=42,13 prev=5 order=9 real=4 stuck=0 fp=0 prog=0 wait=0 parked=0 stop=0 track=01 dtile=917 dt=31`**
  （`order=9` = `OT_WAITING`；`real=4`；`dest=917`）
- `:1576` **`DEPOT-ARR veh=57 spd=0 real=4(6) curType=9 stuck=0 tileDepot=0 tx=42 ty=13 destTx=21 destTy=7 destDepot=0 destResv=0 tileEqDest=0 depotDir=-1 enterDir=2 tt=4 impl=4`**
- `:1577` 之后直到文件末（`:2224`，其间出现 `[R3R] CPL-PAIRMAP tick=13709` 等，tick 已推进到 14 000+）**`veh=57` 一行都没有**，
  即停车后约上千 tick 里该车再无任何探针输出。

**结论 1（P1 有效、原症状确已消失）**：`real=4` 稳定停在命令 5（零基 index 4，路点 917@21,7），`del-implicit` 阶段 `real/impl 3→4` 是正常的
`DeleteUnreachedImplicitOrders()` 推进，之后没有再跳到 6。

**结论 2（新症状 = 永远停在 `OT_WAITING`）**：`:1575/:1576` 明确 `curType/order=9`；`Order::MakeWaiting()`
（`src\order_cmd.cpp:292-297`）只把 `type` 改成 `OT_WAITING` 并保留 `dest`，所以 `dest=917` 仍在。

**疑点 REG-370-B（关键，探针 0 命中）**：`HandleWaiting()` 里三个打点（`gate=time` / `gate=cond` / `gate=pass`）
一千 tick 内一次都没打，而 `R3RIsWaypointWait()` 的判据（Train + `OT_WAITING` + `Waypoint::IsValidID(917)`）
按上面读数应当全部成立。三种可能，待下一版探针区分：

- **C-1 `HandleWaiting()` 根本没被调用**：唯一入口 `TrainLocoHandler()`（`src\train_cmd.cpp:23137-23140`）是
  `if (consist->current_order.IsType(OT_WAITING) && consist->reverse_distance == 0) { if (mode) return true; consist->HandleWaiting(false, true); ... }`
  ⇒ 若 `reverse_distance != 0`，整块被跳过，`HandleWaiting` 永不执行，车就永久停在 `OT_WAITING`。
- **C-2 `R3RIsWaypointWait()` 为假**：例如 `Waypoint::IsValidID(917)` 在 R3R 段/停放语境下不成立，或
  `GetDestination().ToStationID()` 拿到的不是 917。
- **C-3 节流永不命中**：探针用 `v->tick_counter % 64 == 0`（`src\vehicle.cpp:4018`）；若该车在等待期 `tick_counter`
  不递增（`vehicle.cpp:1725/1738` 才是递增点），节流值冻结且恰非 64 的倍数 ⇒ 一行都不打。**这一条同时解释了
  第 369 轮的 `WP-WAIT-KEEP` 也是 0 行**，但 `WP-WAIT-KEEP` 与本节探针共用同一节流口径，仍需实测区分。

**猜测根因（待验证，最长嫌疑 C-1）**：`reverse_distance != 0` 使 `TrainLocoHandler` 的等待处理块被整块跳过，
车停在路点上永远不会被 `HandleWaiting()` 判超时 ⇒ 界面显示"等待放行（时刻表）"，而 `wait_time` 实际为 0
（`:1575` 的 `wait=0` 是 `wait_counter`，不是 `GetTimetabledWait()`，故不能据此排除门①）。
支撑：探针三项全灭 + `OT_WAITING` 状态确凿；反证需求：`reverse_distance` 读数。

**下一步（第 370 轮补 2，已实施）**：加两条**状态变化触发**（不再用 `tick_counter % 64`）的只读探针：

- `WP-LOCO-WAIT`（`src\train_cmd.cpp` 的 `TrainLocoHandler()` 等待块入口，即 `:23137` 的 `if` 之前）：
  打印 `veh mode revdist type real impl n dest cot waitT spd`，**专门读 `reverse_distance`**；
- `WP-HANDLE`（`src\vehicle.cpp` 的 `HandleWaiting()` 走 `case OT_WAITING` 之后、门①之前）：
  打印 `veh wantWp dest validWp waitT lateness wait_time cot ltype sw po`，专门区分 C-2。

节流改为 `_tick_counter` + 状态哈希：状态变化立刻打一行，否则每 256 tick 至多一行 ⇒ 无论哪条计数器冻结都保证有输出。

### 5.11 第 370 轮补 2：两处探针落地（2026-10-29，只读探针，零行为变更）

**落地位置**

| 探针 | 文件:行 | 触发点 | 字段 |
| --- | --- | --- | --- |
| `WP-LOCO-WAIT` | `src\train_cmd.cpp:22318`（调用 `:23209`） | `TrainLocoHandler()` 等待块 `if (... && reverse_distance == 0)` **之前**，条件是 `current_order.IsType(OT_WAITING)` | `veh mode revdist type real impl n dest cot waitT spd` |
| `WP-HANDLE` | `src\vehicle.cpp:4088`（调用 `:4129`） | `Vehicle::HandleWaiting()` 的 `case OT_WAITING` 里、门①之前 | `veh wantWp dest validWp waitT lateness wait_time cot ltype sw po tile` |

**节流写法重写（本轮关键）**：新增 `R3RWaitProbeDue()`（`src\train_cmd.cpp:22271` 区、`src\vehicle.cpp:4001` 区，各 TU 一份 static），
按 `_tick_counter` 计时 + 状态哈希：状态一变立刻输出，否则每 256 tick 至多一行。
`WP-WAITHOLD` / `WP-WAIT-KEEP` 一并改用该口径（否则本轮无法排除 C-3）。
状态哈希**刻意排除** `current_order_time` 与剩余 `wait_time`（每 tick 单调变化，纳入即刷屏）。

**判读表（拿到新日志先跑这张表）**

| 观测 | 判定 |
| --- | --- |
| 有 `WP-LOCO-WAIT` 且 `revdist != 0` | **C-1 成立** ⇒ 修 `TrainLocoHandler` 的 `reverse_distance` 守卫 |
| 有 `WP-LOCO-WAIT`，`revdist = 0` 但 `type != 9` | 订单已被别处改写（与 `SPD-DROP` 的 `order=9` 对照） |
| 有 `revdist=0 type=9`，且 `WP-HANDLE wantWp=0` / `validWp=0` | **C-2 成立** ⇒ 路点判定被绕过（目的地不是有效路点） |
| 有 `WP-HANDLE wantWp=1`，但无 `WP-WAITHOLD gate=pass` | 两道门之一在拦（看 `gate=time` / `gate=cond`） |
| 有 `WP-WAITHOLD gate=pass` 但无 `WP-WAIT-KEEP` | `R3RIsWaypointWait()` 在 pass 时刻为假（dest 已变） |
| 完全无 `WP-LOCO-WAIT` | `TrainLocoHandler` 没被调用（该车根本不 tick）⇒ 另一起因 |

**构建自证**：见 §5.11.1。

#### 5.11.1 构建自证（第 370 轮补 2）

- `build\R3R_incbuild.done` = `EXIT_CODE=0`（2026-10-29 22:05:41，`MODE=inc`）。
- 日志：`[2/4] vehicle.cpp.obj` → `[3/4] train_cmd.cpp.obj` → `[4/4] Linking CXX executable openttd.exe`；
  `error C*` / `fatal error` / `FAILED:` / `build stopped` 计数 **0**。
- 时间戳链：`src\vehicle.cpp` 22:02:29 → `vehicle.cpp.obj` 22:03:35；`src\train_cmd.cpp` 22:02:41 → `train_cmd.cpp.obj` 22:03:35；
  `build\openttd.exe` 22:05:26（52 408 320 B）晚于两个 obj。
- exe 字节流 3/3 命中：`WP-LOCO-WAIT` / `WP-HANDLE` / `WP-WAITHOLD`（旧版仅有 `WP-WAITHOLD`）。
- `read_lints`（`src\vehicle.cpp` + `src\train_cmd.cpp`）0 条。

### 5.12 第 370 轮补 2 复测：真凶是 C-4（等待块不可达），C-1/C-2/C-3 全部排除

**日志**：`build\R3R_debug.log` 1 726 行 / 179 154 B / mtime **2026-10-29 22:09:50**。
**产生者**：`build\openttd.exe` PID **21096**，进程 **START 22:08:53**，PATH `D:\sourcecode of JGRPP\build\openttd.exe`
⇒ 晚于 22:05:26 的补 2 版构建 ⇒ **这份日志确由补 2 探针 exe 产生**，不是旧进程残留（旧版 exe 已于 22:05:26 被覆盖）。

**命中计数（全日志）**：`WP-LOCO-WAIT` = **0** ／ `WP-HANDLE` = **0** ／ `WP-WAITHOLD` = **0** ／ `WP-WAIT-KEEP` = **0** ／ `SPD-DROP` = 6 ／ `veh=57` = 92 行。

**现场（与上一份日志逐字同形）**

| 行 | 内容 |
| --- | --- |
| 1609 | `ORD-IDX-WATCH site=del-implicit-in tag=- veh=57 real=3->4 impl=3->4 tt=3->4 n=7->7 artic=0->0 ... front=1 spd=5 parked=0 curType=6 dest=917 dtX=21 dtY=7 vtile=42,13` |
| 1610 | `ORD-IDX-WATCH site=del-implicit-out tag=deleted veh=57 real=-1->4 impl=-1->4 tt=-1->4 n=-1->7 ...` |
| 1611 | `SPD-DROP veh=57 tile=42,13 prev=5 order=9 real=4 stuck=0 fp=0 prog=0 wait=0 parked=0 stop=0 track=01 dtile=917 dt=31` |
| 1612 | `DEPOT-ARR veh=57 spd=0 real=4(6) curType=9 stuck=0 tileDepot=0 tx=42 ty=13 destTx=21 destTy=7 destResv=0 tileEqDest=0 depotDir=-1 enterDir=2 tt=4 impl=4` |
| — | 1613..1726 共 114 行**无任何 `veh=57`** |

**判定：C-1 / C-2 / C-3 全部排除，新结论 C-4（第 4 条路径）**

1. `SPD-DROP`（`src\train_cmd.cpp:22357`）与等待块（`:23137`）在**同一次 `TrainLocoHandler()` 调用内，且 `SPD-DROP` 在前**；该行 `order=9` 即 `OT_WAITING`（`order_type.h`），`curType=9` 也一致。
2. 补 2 的 `WP-LOCO-WAIT` 打在 `:23137` 的 `if` **之前**、条件只有 `IsType(OT_WAITING)`，节流是"状态一变即打、首行必打" ⇒ 执行流只要到该点就**必然**留行。
3. 实测 **0 行** ⇒ 执行流在 `:22357` 之后、`:23137` 之前**提前 return 了**。
   - **C-1 排除**：`reverse_distance` 守卫在 `:23137` 上，根本没走到，其值对本次现象无解释力；
   - **C-2 排除**：`WP-HANDLE` 也是 0 ⇒ `Vehicle::HandleWaiting()` 从未被进入；
   - **C-3 排除**：新节流状态变化立即输出 + 首行无条件，不再是"被节流吃掉"。

**C-4 与 KI-348 不是同一件事**：`if (j < adv_spd)` 的停驶分支在 `:23473`，**晚于**等待块 `:23137`，所以"停驶分支不调 `TrainController`"并不能阻止列车先走完等待块。拦路的 `return` 必须落在 **`:22357` ~ `:23137`** 之间。

**下一步（第 370 轮补 3）**：在 `:22357` ~ `:23137` 区间的每个 `return` 前加一支极轻探针（`WP-EXIT why=<tag> veh= order= real=`，仅 `cur_order.IsType(OT_WAITING)` 时打一行），一次性定位是哪个早退；候选（按该区间出现顺序待核）：`CheckTrainStayInDepot()` 返回真、`R3RIsCarOnlyFormation()` 早退、`Stopped` 早退、`OT_GOTO_COUPLE` 配对门、`R3RChainFullyInDepot` 相关门。
注意：`DEPOT-ARR`（`CheckTrainStayInDepot` 入口探针）在 `SPD-DROP` 后一行出现，只证明**函数被调用**，不证明它返回真——必须靠 `WP-EXIT` 才能定性。

## 六、复测/收口判据（初稿）

1. 复跑同场景：veh57 停在命令 4 路点时，订单索引**不动**；
2. 走到命令 5 路点那一刻，索引 `4 → 5 → 6` 逐格推进，不出现 `4 → 6`；
3. 负判据：两个相邻路点连续命令的正常列车不得因此变成"每格都停"。

## 七、第 370 轮补 3：`WP-EXIT` 早退探针（已落地 + 已编译，复测待做）

### 7.1 为什么必须补 3
§5.12 已把范围压到"`SPD-DROP`（`:22357`）之后、等待块（`:23137`）之前提前 return"，但该区间有 10 个 `return`，
其中能解释现场的那几个恰好都是**静默**的（`CheckTrainStayInDepot()` 返回真、`R3RIsCarOnlyFormation()` 停放、
解挂互锁，以及停驻态不再重复刷的 `SKIP-STOPPED`）。继续人肉推理会再耗一轮，且"日志没有"与"根本没走到"分不清。
⇒ 在每个 `return` 之前打一支统一探针，一次复测定位。

### 7.2 落地（仅 `src\train_cmd.cpp`，未碰 `src\*.h`）
新增 `static void R3RWaypointExitProbe(const Train *consist, const char *why)`（定义 `:22345`，`R3RDbgWrite` 在 `:22373`）：

`WP-EXIT why=%s veh=%d order=%d real=%d impl=%d n=%d spd=%d tile=%d,%d revdist=%d stop=%d parked=%d front=%d power=%d caronly=%d tileDepot=%d track=%02X prog=%d dtile=%u`

- 节流同补 2：同一 `(veh, why)` 状态变化立即输出，否则每 256 tick 一行；`why` 按字符串字面量指针比较（逐调用点唯一），不用 `v->tick_counter`。
- 9 个调用点，全部先判 `current_order.IsType(OT_WAITING)`，普通列车零开销：

| 行 | `why` | 对应原 `return` |
|---|---|---|
| 22399 | `entry` | handler 入口（正面证据：本 tick 确实被调用过） |
| 22511 | `crashed` | 车辆已撞毁 |
| 22525 | `breakdown` | `HandlePossibleBreakdowns()` 命中 |
| 22705 | `stopped` | `Stopped && !(库内待挂)` 停驶门 |
| 22844 | `notfront` | 库内挂车后身份已迁到新链头 |
| 23155 | `mayreverse` | `r3r_may_reverse` 掉头 |
| 23210 | `interlock` | `R3RDecoupleInterlockWait()` 命中 |
| 23256 | `caronly` | 无动力链停放（`spd==0` 时原本完全静默） |
| 23263 | `depot` | `CheckTrainStayInDepot()` 返回真（原本完全静默，头号嫌疑） |

- 刻意不加：`OT_LOADING` 的 `return`（`:23177`）与 `OT_WAIT_COUPLE` 的 `return`（`:23209`）—— 走到这两处时
  `current_order` 必然不是 `OT_WAITING`，加了永远不输出。
- 探针整体在 `if (!R3RDbgOn()) return;` 之后求值，`R3R_PROBES_DEFAULT=0` 的发布树零开销。

### 7.3 判读表（复测时怎么用）

| 现象 | 结论 | 下一步 |
|---|---|---|
| 有 `entry`、无任何出口行 | 早退落在 `:22399`~`:22511` 之间（本次已全覆盖，理论不该发生） | 重查该区间是否有被 grep 漏掉的条件编译/宏内 `return` |
| `why=depot` | `CheckTrainStayInDepot()` 在**非车库**的等待链上返回真 | 读该函数，用本行的 `tileDepot=0` 定位分支 |
| `why=caronly` | 等待链被判定为无动力编组（`caronly=1 power=0`） | 查 `R3RIsCarOnlyFormation()` 为何把有机车的链判成车底链 |
| `why=stopped` | 与 `SPD-DROP` 的 `stop=0` 矛盾（该 tick 尚未置 Stopped） | 说明是之后某 tick 才被置 Stopped，需配 `SKIP-STOPPED` 行一起看 |
| `why=interlock` / `why=mayreverse` | 解挂互锁 / 掉头把自己的命令锁住 | 各自单独查 |
| 连 `entry` 都没有 | handler 根本没被这条链调用（不是 front、或不在 tick 列表） | 查 `Train::Tick` 的 front 判定与 `vehstatus` |

### 7.4 构建自证（2026-10-29）
- 入口：复用 `R3R_fullbuild.cmd inc detach`（未新建任何 `.cmd`）；`build\R3R_incbuild.guard.log` = `GUARD: incremental is safe`。
- 日志：`[2/3] Building CXX object ...\src\train_cmd.cpp.obj` → `[3/3] Linking CXX executable openttd.exe`；
  `build\R3R_incbuild.done` = `EXIT_CODE=0`（2026-10-29 22:16:12）；错误计数 0。
- 时间戳链：`src\train_cmd.cpp` 22:14:11 → `train_cmd.cpp.obj` 22:14:58 → `build\openttd.exe` 22:16:08（52 408 320 B）。
- 字面量：exe 内命中 `WP-EXIT why=%s veh=%d`；`read_lints` 0 条。
- 状态：**只加探针、零行为变更**，游戏内复测待做。

## 八、第 371 轮（2026-10-29 22:24 复测）：探针 0 行的真因是 tag 白名单，C-4 撤回

### 8.1 采样身份
- `build\R3R_debug.log` 188 988 B / 1 852 行 / `2026-10-29 22:24:21`。
- 产生者 `build\openttd.exe` 52 408 320 B / `22:16:08` = 第 370 轮补 3 版（二进制内含
  `WP-EXIT why=%s veh=%d` 字面量）⇒ 探针**确实编进了**这份 exe，采样有效。

### 8.2 计数（判定性）

| 探针 | 命中 |
|---|---|
| `WP-EXIT`（任何 why） | **0** |
| `WP-LOCO-WAIT` | 0 |
| 任何 `WP-` 前缀行 | **0** |
| `SPD-DROP` | 5 |
| `veh=57` | 92 |

### 8.3 真因：`R3RDbgTagAllowed()` 白名单里没有 `WP`

`src\r3r_perf.h:309-376` 是探针 tag 白名单（第 274 轮引入），`R3RDbgWrite()`（`:383-399`）在写盘前
`if (!R3RDbgTagAllowed(fmt)) return;`（`:386`）。白名单放行的前缀（`:322-375`）只有：
`RESV` / `UNRES` / `EXTEND` / `LOOP` / `R3R-RES` / `COUPLE` / `CPL` / `FOLD` / `SPLICE` / `ARTIC` /
`SKIP-` / `UNIT` / `CLONE` / `ORD` / `CTRL` / `AUTO-` / `OWNER` / `NAME` / `RESERVE` / `DECOUPLE` /
`SPD` / `DUALHEAD` / `TTB` / `DEPOT` / `SEGID` / `SEGROW` / `LEGACY` / `CGRP` / `GRP` / `R3R-`。
**`WP` 不在其中** ⇒ 第 369/370 轮的全部 WP 家族探针都被静默丢弃：

- `train_cmd.cpp:22331` `WP-LOCO-WAIT`（R3RDbgWrite）
- `train_cmd.cpp:22373` `WP-EXIT`（R3RDbgWrite）
- `vehicle.cpp:4069` `WP-WAITHOLD`（R3RDbgWrite）
- `vehicle.cpp:4107` `WP-HANDLE`（R3RDbgWrite）
- `vehicle.cpp:4162` `WP-WAIT-KEEP`（R3RDbgWrite）

对照组（同一份日志里为什么别的探针有行）：

- `SPD-DROP` → 前缀 `SPD` 在白名单（`:366`）；
- `DEPOT-ARR` → 前缀 `DEPOT`（`:369`）；
- `ORD-IDX-WATCH` → 前缀 `ORD`（`:358`）；
- `LINEND`（`train_cmd.cpp:21959`）与 `LOCO-MOVE`（`:23607`）→ 裸 `fprintf(dbg / r3r_dbg, ...)`，
  不经白名单，所以照常出。

⇒ 「`SPD-DROP` 之后紧跟 `DEPOT-ARR`、之后一片空白」不是"handler 早退"，而是**这两个探针恰好都在白名单/裸 fprintf 里，
WP 家族全被过滤**。

### 8.4 立即作废的旧结论

1. §5.12 的 **C-4「等待块不可达 / handler 在 `SPD-DROP` 与等待块之间提前 return」撤回为"未验证"**：
   它的唯一证据是 `WP-LOCO-WAIT` 0 行，而该行 100% 会被白名单吞掉。
2. 第 370 轮补 3 的判读表（§7.3）在修白名单之前**整张不可用**——连 `entry` 也永远打不出来。
3. 第 369/370 轮凡是引用过 WP 家族「0 行」的推理（含 `WP-WAITHOLD`/`WP-HANDLE`/`WP-WAIT-KEEP`）一律作废；
   引用 `CRT veh=57` 零行的判据也要复核（`CRT` 不在白名单），`LOCO-MOVE`（裸 fprintf）与 `LINEND` 的读数仍然有效。

### 8.5 现场三条有效读数（下一步的起点）

- `SPD-DROP veh=57 tile=42,13 prev=5 order=9 real=4 stuck=0 fp=0 prog=0 wait=0 parked=0 stop=0 track=01 dtile=917 dt=31`
- `ORD-IDX-WATCH site=delete-implicit-out tag=deleted veh=57 real=-1->4 impl=-1->4 n=-1->7 curType=6 dest=917 dtX=21 dtY=7 vtile=42,13`
- `DEPOT-ARR veh=57 spd=0 real=4(6) curType=9 stuck=0 tileDepot=0 tx=42 ty=13 destTx=21 destTy=7 destDepot=0 destResv=0 tileEqDest=0 depotDir=-1 enterDir=2 tt=4 impl=4`

换算锚：`src\order_type.h` 的 `OT_WAITING = 9`（不是 18/19）⇒ `curType=9` 就是等待订单，
"订单其实不是等待态"这条不成立。

### 8.6 本轮动作

- 修 `src\r3r_perf.h` 白名单：新增 `WP` 家族（改头文件 ⇒ 护栏会判全量重编 620 个 obj）。
- 纪律（建议长期有效）：**新增探针前必须先确认其 tag 前缀在 `R3RDbgTagAllowed()` 内**，
  否则"日志 0 行"与"没触发"不可区分——这是第 274/296 轮之后**第三次**踩同一个坑。
- 修好后复测必须看到 `WP-EXIT why=...` 才算开始有效取证；若仍然 0 行，才轮得到 §7.3 的判读。

## 九、第 372 轮（2026-10-29 22:53 复测）：白名单已生效，WP 家族全部出线；KI-534 原症状未复现，但暴露"等待门 pass 却不推进"

### 9.1 采样身份（有效）

- 构建：`build\R3R_incbuild.done` = `EXIT_CODE=0`，`2026-10-29 22:51:26`；
  `build\openttd.exe` 52 487 168 B / `2026-10-29 22:51:03`（第 371 轮白名单修复版，全量 692 步）。
- 日志：`build\R3R_debug.log` 418 641 B / **4 672 行** / `2026-10-29 22:53:33`
  ⇒ 由该 exe 产生，采样有效（会话约 2.5 分钟）。

### 9.2 计数（判定性）

| 探针 | 命中 | 备注 |
|---|---|---|
| 任何 `WP-` 行 | **2 971** | 白名单修复**生效**（第 370 轮为 0） |
| `WP-EXIT` | 4 | 全部 `why=entry`，行 **1611 / 2512 / 3431 / 4381** |
| `WP-LOCO-WAIT` | 1 693 | `mode=0/1` 逐 tick 交替 |
| `WP-WAITHOLD` | 846 | `gate=time` 423 + `gate=pass` 423 |
| `WP-HANDLE` | 5 | 行 1616 / 1621 / 2521 / 3440 / 4390 |
| `WP-WAIT-KEEP` | 423 | 与 `gate=pass` 一一对应 |
| `SPD-DROP` | 6 | |
| `LINEND` | 46 | |
| `COUPLE-FAIL` | 5 | |
| `veh=57` | 3 063 | |
| `veh=57` 且 `real=6` | **0** | |
| `veh=57` 且 `real=5` | 2 | 行 **412** / **421**（早期往返，非跳条，见 9.4） |

### 9.3 现场读数（原文）

一次「入口 → 静止」的完整切片（行 1606~1620）：

- `1570 LINEND veh=57 tile=42,13 x=144 sig=0 near=0 spd=5 brk=15 clamp=0 len=2 dir=5 db=0`
- `1611 WP-EXIT why=entry veh=57 order=9 real=4 impl=4 n=7 spd=0 tile=42,13 revdist=0 stop=0 parked=0 front=1 power=14202 caronly=0 tileDepot=0 track=01 prog=0 dtile=917`
- `1612 SPD-DROP veh=57 tile=42,13 prev=5 order=9 real=4 stuck=0 fp=0 prog=0 wait=0 parked=0 stop=0 track=01 dtile=917 dt=31`
- `1613 DEPOT-ARR veh=57 spd=0 real=4(6) curType=9 stuck=0 tileDepot=0 tx=42 ty=13 destTx=21 destTy=7 destDepot=0 destResv=0 tileEqDest=0 depotDir=-1 enterDir=2 tt=4 impl=4`
- `1614 WP-LOCO-WAIT veh=57 mode=1 revdist=0 type=9 real=4 impl=4 n=7 dest=3 cot=0 waitT=0 spd=0`
- `1615 WP-LOCO-WAIT veh=57 mode=0 revdist=0 type=9 real=4 impl=4 n=7 dest=3 cot=1 waitT=0 spd=0`
- `1616 WP-HANDLE veh=57 wantWp=1 dest=3 validWp=1 waitT=0 lateness=0 wait_time=0 cot=1 ltype=0 sw=0 po=1 tile=42,13`
- `1617 WP-WAITHOLD gate=pass veh=57 real=4 impl=4 n=7 wtime=0 cot=1 ltype=0 cow=0 stop=0`

稳态循环（行 1619~4672，逐 tick 重复，`real` 恒 4、`spd` 恒 0）：

```
WP-WAIT-KEEP veh=57 real=4 impl=4 dest=3 n=7
WP-LOCO-WAIT veh=57 mode=1 ... type=9 real=4 impl=4 n=7 dest=3 cot=0 waitT=1 spd=0
WP-LOCO-WAIT veh=57 mode=0 ... type=9 real=4 impl=4 n=7 dest=3 cot=1 waitT=1 spd=0
WP-WAITHOLD gate=time veh=57 real=4 impl=4 n=7 wtime=2 cot=1 ltype=0 cow=0 stop=0
WP-LOCO-WAIT ... cot=1 waitT=1
WP-LOCO-WAIT ... cot=2 waitT=1
WP-WAITHOLD gate=pass veh=57 real=4 impl=4 n=7 wtime=2 cot=2 ltype=0 cow=0 stop=0
```

日志最后一行（4 672）仍是 `WP-LOCO-WAIT veh=57 mode=1 ... real=4 ... cot=1 waitT=1 spd=0`。

### 9.4 判定

1. **tag 白名单修复生效**：WP 家族 2 971 行，第 370 轮的「0 行」不再是观测障碍。第 371 轮的结论（C-4 撤回）得到正面确认。
2. **KI-534 原症状本会话未复现**：`real` 全程只出现 4（以及早期 3↔4 交替），`real=6` **0 行** ⇒ 没有"跳过命令 5 跳到第 6 条"。
   仅有的两行 `real=5`（行 412 `DEPOT-ARR veh=57 spd=0 real=5(15) curType=4`、行 421
   `ORD-RETURN-OWN veh=57 num=7 real=5->1 impl=5->1 bk=1//1 orphan=0`）是更早的一次
   借用归还（`real` 从 5 回到 1），与跳条无关。
3. **新疑点 A（阻塞级）**：`WP-WAITHOLD` 的 `gate=pass` 出现 423 次（与 `gate=time` 1:1 交替），
   但每一次 pass 之后 `real` 仍是 4、`spd` 仍是 0、`tile` 仍是 42,13 ⇒ **等待门通过却不推进索引**，
   列车原地卡在路点的等待订单（`order=9` = `OT_WAITING`）上不动。
4. **新疑点 B**：`cot`（current order type）在 **0 / 1 / 2** 之间每 tick 摇摆，
   而 `type=9`、`real=4` 恒定 ⇒ 说明每 tick 有一处代码把 `current_order` 反复改写。
5. **新疑点 C**：`waitT` 恒为 1、`wait_time` 恒为 2、`lateness` 恒为 -1 ⇒ 等待计时没有累积到满足退出条件，
   与疑点 A 互为因果（pass 与 time 各 423 次说明判据在边界上抖动）。
6. **`WP-EXIT` 只有 `entry`、无任何出口行**：按 §7.3 判据 2，说明执行走完了等待块，
   而出口探针只埋在提前 return 的路径上（待读码确认埋点位置），不能据此推断早退。

### 9.5 猜测根因（待验证）

- **G-372-1**：`gate=pass` 只表示"路点等待时长条件满足"，真正推进 `real` 依赖 `ProcessOrders()`
   对 `OT_WAITING` 的处理；当前 `real` 不动 ⇒ 要么该分支没有把 `OT_WAITING` 当可推进订单，
   要么推进后又被某处写回 4。验证方法：读 `vehicle.cpp:4069/4107/4162` 与
  `train_cmd.cpp:22331/22373` 的上下文，确认 gate=pass 之后的控制流去向。
- **G-372-2**：`cot` 摇摆 + `waitT` 不累积 ⇒ 每 tick 有一次「重建 current_order」的动作
  （可能是 R3R 的订单索引同步 `R3RSyncDrivingOrders` 或 `ORD-RETURN-OWN` 借用归还路径）
  把等待计时清零，使 `wait_time` 永远差一天。

后续动作：读上述五处代码定位 `gate=pass` 之后的控制流，再决定是探针埋点问题还是真 bug。

### 9.6 根因（已由代码确认，2026-10-29 第 372 轮）

`src\vehicle.cpp` 的 `Vehicle::HandleWaiting()`（`:4122-4179`）里，第 369 轮 **P1 修法写成了"完全不推进"**：

```cpp
/* :4147 */ R3RWaypointWaitProbe(this, "pass", wait_time, stop_waiting);
            this->vehicle_flags.Reset(VehicleFlag::ConditionalOrderWait);
            UpdateVehicleTimetable(this, false);
/* :4161 */ if (R3RIsWaypointWait(this)) {
/* :4162 */     R3RDbgWrite("WP-WAIT-KEEP veh=%d real=%d ...");
/* :4166 */     InvalidateVehicleOrder(this, 0);      // ← 只刷新界面
            } else {
/* :4168 */     this->IncrementImplicitOrderIndex();  // ← 正常推进被整段跳过
            }
/* :4170 */ this->current_order.MakeDummy();
/* :4172 */ if (process_orders) ProcessOrders(this);
```

⇒ 对 R3R 路点，等待结束后 **`real` 一格都不前进**，`current_order` 被 `MakeDummy()`（`:4170`）后
`ProcessOrders()` 重新解析 `real=4` 这条**已完成**的 `OT_GOTO_WAYPOINT` ⇒ 又变 `OT_WAITING`（`order=9`）
⇒ 下一 tick 再 `gate=pass` ⇒ 再 KEEP …… 无限循环。

这与 9.3 的读数逐字吻合：

- `gate=pass` 与 `gate=time` 各 423 次（1:1，永不收敛）；
- `real` 恒 4、`tile` 恒 42,13、`spd` 恒 0（从不前进）；
- `cot` 在 0/1/2 摇摆（`MakeDummy` + 重新进入等待把 `current_order_time` 反复清零，
  `wait_time = max(GetTimetabledWait(1) - lateness(-1), 0) = 2`，所以每 3 tick 走完一轮）；
- `waitT` 恒 1 不累积。

即 P1 的**意图**（不要一次吃掉下一条订单）对，**实现**错成了「一条都不推进」。

### 9.7 修法（本轮已实现）

`HandleWaiting()` 的 KEEP 分支改为**显式推进恰好一条真实订单**，并让 implicit 与 real 保持锁步
（R3R 已删除 `OT_IMPLICIT` 订单，两个索引本就该相等）：

```cpp
if (R3RIsWaypointWait(this)) {
    R3RDbgWrite("WP-WAIT-KEEP veh=%d real=%d impl=%d dest=%d n=%d\n", ...);
    R3RDbgWrite("WP-WAIT-ADV veh=%d real=%d->%d impl=%d->%d\n", ...);
    this->SkipToNextRealOrderIndex();                       // real +1，跳过 OT_IMPLICIT（无）
    this->cur_implicit_order_index = this->cur_real_order_index;
    InvalidateVehicleOrder(this, 0);
} else {
    this->IncrementImplicitOrderIndex();
}
```

- `SkipToNextRealOrderIndex()`（`src\vehicle_base.h:1084`）本来就只 +1；它内部对
  `OT_GOTO_COUPLE` 的锁定不会命中（此处 `current_order` 是 `OT_WAITING`）。
- 显式 `cur_implicit_order_index = cur_real_order_index` 使 `real`/`impl` 同步，
  避免下一轮 `IncrementImplicitOrderIndex()` 的 `do{...}while(implicit != real && !OT_IMPLICIT)`
  在"没有 implicit 订单"的表里绕圈（这正是 KI-534 原始 4→6 跳条的怀疑来源）。
- 不改任何数据格式，不改上游路径（`R3RIsWaypointWait()` 为假时逐字不变）。

### 9.8 待复测判据

1. `WP-WAIT-KEEP` 之后必须紧跟一行 `WP-WAIT-ADV veh=57 real=4->5 impl=4->5`，且**只 +1**；
2. 随后 `order` 变为 6（`OT_GOTO_WAYPOINT`，目标 917 / tile 21,7）、`spd` 上升、`tile` 离开 42,13；
3. `real` 不再出现 6（无跳条）、`gate=pass` 与 `gate=time` 不再 1:1 无限交替；
4. 到达 917 的第二个路点后同样一遍：KEEP → ADV（5→6）→ 继续 7 条表；
5. 负判据：非 R3R 路点等待（原生 timetable 等待）行为逐字不变，`WP-WAIT-KEEP` 不应出现。

### 9.9 构建自证（第 372 轮）

- 入口：`R3R_fullbuild.cmd inc detach`（复用既有唯一入口，未新建任何 `.cmd`）。
- 前置：`tasklist /fi "imagename eq openttd.exe" /nh` 无输出 ⇒ 游戏进程已退出，未触发闸门 97。
- 护栏：`build\R3R_incbuild.guard.log` = `GUARD: incremental is safe (no header/lang file is newer than the newest object)`
  ⇒ 本轮只改 `src\vehicle.cpp`，增量合法。
- 日志：`[2/3] Building CXX object ...\src\vehicle.cpp.obj` → `[3/3] Linking CXX executable openttd.exe`。
- `build\R3R_incbuild.done` = `EXIT_CODE=0` @ `2026-10-29 23:05:25`。
- 日志内 `error C` / `fatal error` / `FAILED:` / `build stopped` 计数 **0**。
- 时间戳单调：`src\vehicle.cpp` 22:56:19 → `vehicle.cpp.obj` 23:04:19 → `build\openttd.exe` 23:05:16（52 487 168 B）。
- 产物自证：`openttd.exe` 内命中新字面量 `WP-WAIT-ADV veh=`（1 处）。
- `read_lints src\vehicle.cpp` = 0 条。

⇒ 状态：已实现 + 已编译，游戏内复测待做（判据见 §9.8）。

---

## 十、第 373 轮：复测日志取证（玩家报「命令多跳又回来了」）

### 10.1 采样身份

- `build\R3R_debug.log` = 216 238 B / 2 106 物理行 @ `2026-10-29 23:09:36`
- 产生者 `build\openttd.exe` = 52 487 168 B @ `23:05:16`，`build\R3R_incbuild.done` = `EXIT_CODE=0`
  ⇒ 正是第 372 轮（`WP-WAIT-ADV` +1 版）的 exe，采样有效。
- 注意：本日志**每行被折成多个物理行**（约 40 字符换行），直接 `Select-String` 会拆散字段；
  本轮取证统一先 `-replace "\r\n",""` 合并成逻辑行再正则，`ORD-IDX-WATCH` 实际 66 条（物理行看起来 146 条）。

### 10.2 第 372 轮修复确认生效（只 +1）

`WP-` 系列全日志仅 4 条逻辑行：`WP-EXIT why=entry veh=5`、`WP-WAITHOLD gate=pass veh=57 real=4 impl=4`、
`WP-WAIT-KEEP veh=57 real=4 impl=4 dest=3 n=7`、`WP-WAIT-ADV veh=57 real=4->5 impl=4->5`。

⇒ 路点等待出口**确实只推进 1 条**，`real` 4→5，`impl` 锁步，未见 4→6。

### 10.3 veh=57 的完整索引轨迹（合并后 32 条）

```
settle-in/decouple-v  r4->r2      ← 解挂时换表（自己表→新表）
settle-out            r4->r3
depotstay-in … tick-in（一系列）  r4->r3
…                     r3->r4      ← 第 1 次 +1
…                     r4->r5      ← 第 2 次 +1（= WP-WAIT-ADV）
```

两次递增都是 `+1`，且 `real`/`impl`/`tt` 三者同时只变 1。**veh=57 侧没有多跳**。

### 10.4 全日志里仅有的三处「大跨度」及其性质

| 位置 | 变化 | 性质 |
|---|---|---|
| `veh=48` `site=settle-in tag=couple` | `real=2->7`、`n=14->33`、`ord=…EB80 -> …EC20` | **换表**：耦合时借用了等待方 33 条表，索引按新表位置取值（KI-04 借用交接的正常形态），不是同一张表上跳条 |
| `veh=0` `site=ordlist-delete/insert` | `real=-1->3` | **建表**：`-1` = 该车原先无表，接管后落在 3，非同一表跳条 |
| `veh=54` `tick-in` 等 | `real=3->0` | **回退**（不是多跳），疑与借用归还/停放恢复有关，待查 |

### 10.5 疑点（REG-373-A）

玩家观察到的「命令多跳」在 veh=57 的 `real` 序列里**找不到对应读数**（只 4→5）。
可能形态（均未证实，不得当结论）：

1. 界面订单窗口高亮/滚动到第 6 条，但 `real` 仍是 5（显示层 vs 执行层）；
2. 观察对象不是 veh=57，而是同场景里另一列车（如 `veh=54` 的 3→0 回退、或 `veh=48` 换表后的 7）；
3. `dest`/`dtile` 变化被误读成索引变化：`real=5` 时 `curType=16`（OT_GOTO_COUPLE）、`destTile=923`（27,7），
   而列车实际在 `42,13` ⇒ 表现为「命令指向远处车站」；
4. 折行日志导致玩家按物理行读数错位。

⇒ 需要玩家补充：**哪一列车（车号）、界面第几条跳到第几条、当时是否刚过路点**。

### 10.6 本轮未改源码、未构建

只做取证与落盘。待疑点定性后再决定是否动 `WP-WAIT-ADV` 的推进语义。

---

## 十一、第 373 轮续：玩家定位到具体现象（订单窗口 第 4 条 → 第 6 条）

### 11.1 玩家原话（决定性描述）

> 我打开列车（veh57 的调度列表），然后看到列车没执行第五条就从第四条跳到第六条去了

⇒ 观察面 = **游戏内订单窗口（1-based 数行）**，不是日志。

### 11.2 veh=57 自己那张表的逐条类型（决定性命中）

日志 `COUPLE-FAIL loco=57` 之后紧跟 `LOCO-ORD` 逐条 dump（共 7 条，与 `n=7` 一致）：

| 索引(0-based) | 界面行(1-based) | `type` | 含义 |
|---|---|---|---|
| 0 | 第 1 条 | 6 | `OT_GOTO_WAYPOINT`（路点） |
| 1 | 第 2 条 | 16 | `OT_GOTO_COUPLE`（前往挂车） |
| 2 | 第 3 条 | 1 | `OT_GOTO_STATION`（车站） |
| 3 | 第 4 条 | 6 | 路点 |
| **4** | **第 5 条** | **6** | **路点（玩家说"没执行"的那一条）** |
| 5 | 第 6 条 | 16 | `OT_GOTO_COUPLE`（玩家说"跳过去"的那一条） |
| 6 | 第 7 条 | 2 | `OT_GOTO_DEPOT`（车库） |

`type=9` 不是订单类型：`src\order_type.h:86` = `OT_WAITING`，属 `current_order` 运行时态，
日志 `WP-EXIT why=entry ... order=9` / `DEPOT-ARR ... curType=9` 都是这个运行时态，**不是表里第几条**。

### 11.3 同一时刻的索引读数（与玩家观察对表）

```
ORD-RETURN-OWN veh=57 num=7 real=5->1 impl=5->1 bk=1/1 orphan=0   ← 归还自己的 7 条表，位置在新表=1
AUTO-ORD seg=4 head=57 slot=0 kind=station dest=2 iv=[2,3) cursor=1 anchor=2 real=4 cur=waypoint own=0 borrowed=1 n=7
WP-EXIT  why=entry veh=57 order=9 real=4 impl=4 n=7 spd=0 tile=42,13 dtile=917
DEPOT-ARR veh=57 spd=0 real=4(6) curType=9 tx=42 ty=13 destTx=21 destTy=7 tt=4 impl=4
WP-HANDLE veh=57 wantWp=1 dest=3 validWp=1 waitTime=0 lateness=0 wait_time=0 cot=1 sw=0 po=1 tile=42,13
WP-WAITHOLD gate=pass veh=57 real=4 impl=4 n=7 waitTime=0 cot=1 ltype=0 cow=0 stop=0
WP-WAIT-KEEP veh=57 real=4 impl=4 dest=3 n=7
WP-WAIT-ADV  veh=57 real=4->5 impl=4->5
CPL-S0-CHK veh=57 real=5 order=16 dtile=923
```

⇒ `real` 4→5 = 界面 **第 5 条 → 第 6 条**，只 +1（第 372 轮的 +1 修复成立）。
玩家口中的「第四条跳到第六条」与「没执行第五条」指向同一件事：**索引 4 这条路点被"经过"了，但没有产生任何可见执行效果**。

### 11.4 猜测根因（待验证，已登记 KI-537）

索引 4 的路点等待判据在**进入当刻就 `gate=pass`**：

- `WP-EXIT why=entry`（入口即出）
- `WP-HANDLE waitTime=0 lateness=0 wait_time=0`
- `WP-WAITHOLD gate=pass`

⇒ 等待门没有"停下来等"，而是立刻放行，于是 `real` 直接从 4 推到 5。从玩家视角就是「第 5 条啥也没干就跳过去了」。

支撑证据：`DEPOT-ARR ... destTx=21 destTy=7`（目标路点 21,7 / tile 917）而车实际停在 `42,13`，
`spd=0` 由 `SPD-DROP veh=57 ... prev=5` 归零；`prog=0`、`tileEqDest=0` ⇒ 车并不在路点格上，
却仍判定为"路点已满足"。

待验证判据：
1. 源码侧确认 `WP-WAITHOLD gate=pass` 的判据是「已停在路点」还是「进站条目/受时间表约束」；
2. 若判据误把 `prog=0 且 tileEqDest=0` 当满足，则第 5 条在任何位置都会被立即放行（可复现性很强）；
3. 负判据：真正停在 21,7 路点上的等待仍必须 `gate=time` 而不是 `pass`。

### 11.5 另一条同轮观察（不是本次多跳）

`ORD-SKIP seg=4 head=57 slot=0 from=2 to=3 iv=[2,3) order=0` —— 跳过的是区间 `[2,3)`
即索引 2（第 3 条，车站），属自动填充区间（KI-400 家族），与 4→5 无关，不要混为一谈。

### 11.6 源码对照：第 372 轮改的就是这段（`src\vehicle.cpp:4161-4177`）

```4161:4177:src/vehicle.cpp
			if (R3RIsWaypointWait(this)) {
				R3RDbgWrite("WP-WAIT-KEEP veh=%d real=%d impl=%d dest=%d n=%d\n", ...);
				const VehicleOrderID old_real = this->cur_real_order_index;
				const VehicleOrderID old_impl = this->cur_implicit_order_index;
				this->SkipToNextRealOrderIndex();
```

同处注释（第 370 轮补 2 写入）已逐字记录玩家上一轮的原话现象：
「veh57 stopped at waypoint 1706 (order 4) and jumped straight from order 4 to the GOTO_COUPLE behind order 5,
never parsing order 5 (waypoint 917 at 21,7) and never leaving the tile」。
⇒ 上一轮的真凶是 `IncrementImplicitOrderIndex()` 在无 `OT_IMPLICIT` 的表里
`do{...}while(implicit != real && !IsType(OT_IMPLICIT))` 连吃多条；换成 `SkipToNextRealOrderIndex()` 后
本日志实测 `real` 确实只 +1（4→5），**连吃已消失**。

### 11.7 第二层问题（本轮新暴露，已登记 KI-538）

`real` 只 +1 之后，剩下的不是"多跳"而是「**第 5 条没把车送到它就完成了**」：

| 读数 | 期望 | 实测 |
|---|---|---|
| 车所在格 | 索引 4 的路点（`dtile=917` = 21,7） | `tile=42,13`（tile 3370） |
| `tileEqDest` | 1 | **0** |
| `prog` | 非 0（有行程） | **0** |
| `spd` | — | 0（由 `SPD-DROP prev=5` 归零） |
| 等待门 | `gate=time`（真等过） | **`gate=pass`（进入即放行）** |

⇒ 车在解挂后的原地 `42,13` 就把索引 4 的 `OT_GOTO_WAYPOINT(21,7)` 判成"已到达并等待结束"，
`WP-EXIT why=entry` 是同一件事的另一面（进入等待态的当刻即退出）。玩家看到的就是
「第 5 条根本没去那个路点，命令就翻过去了」。

下一步取数（未做）：谁把索引 4 置成 `OT_WAITING` —— 是 `ProcessOrders()` 的停稳到达判定，
还是 `R3RSyncDrivingOrders` / 解挂提交点直接写 `current_order`。探针已足够（`WP-EXIT why=`、
`SPD-DROP`、`DEPOT-ARR tileEqDest/prog`），只需再加一条"`OT_WAITING` 的写入点"trace。

### 11.8 本轮结论一句话

- 第 372 轮的 +1 修复**成立**（`real 4→5`，`impl/tt` 锁步，无 4→6）；
- 玩家本轮报的"第 4 条跳第 6 条"在读数上是**第 5 条 → 第 6 条**，根因已从"连吃多条"变成
  "第 5 条在错误的格子上被判定完成"（KI-538）；
- 两件事都不能靠继续调 `SkipToNextRealOrderIndex` 解决。

---

## 十二、第 373 轮续：根因落到 `Order::ShouldStopAtStation()` 的 R3R 补丁（缺 dest 校验）

### 12.1 源码定位（`src\order_cmd.cpp:5082-5093`）

```cpp
/* R3R: defined in train_cmd.cpp. */
bool R3RTrainStopsAtWaypoint(const Train *v);

bool Order::ShouldStopAtStation(const Vehicle *v, StationID station, bool waypoint) const
{
	/* R3R: a train whose next order is a GOTO_COUPLE stops with its front at this waypoint. ... */
	if (waypoint && v->type == VehicleType::Train && this == &v->current_order &&
			R3RTrainStopsAtWaypoint(static_cast<const Train *>(v))) return true;

	return this->ShouldStopAtStation(v->last_station_visited, station, waypoint);
}
```

原生分支（`:5062-5072`）对路点是：

```cpp
if (waypoint) return this->IsType(OT_GOTO_WAYPOINT) && this->IsWaitTimetabled() && this->dest == station;
```

⇒ **R3R 的提前 `return true` 少了 `this->dest == station` 这一项**。它只要求：
①正在执行的订单是 `OT_GOTO_WAYPOINT`；②它的下一条是 `OT_GOTO_COUPLE`；
而**不要求列车此刻进入的路点格就是该订单指定的那个路点**。

### 12.2 触发链（把 §11 的读数逐条对上）

1. 列车在解挂点 `42,13`（该格是路点格，`waypoint=true` 由 `IsRailWaypoint(tile)` 决定），
   `current_order` 已是索引 4 的 `OT_GOTO_WAYPOINT`（目标在 `21,7`），下一条索引 5 是 `OT_GOTO_COUPLE`；
2. `VehicleEnterTile_Station()`（`src\station_cmd.cpp`）调
   `consist->current_order.ShouldStopAtStation(consist, GetStationIndex(tile), IsRailWaypoint(tile))`
   ⇒ 命中 R3R 补丁 ⇒ `true`（**即使 `42,13` 不是 `21,7`**）；
3. 于是 `TrainEnterStation(consist, station@42,13)` 的路点分支执行
   `consist->current_order.MakeWaiting()`（`src\train_cmd.cpp:19645` 起），
   `MakeWaiting()` **保留 `current_order` 的 `dest`**（也就是索引 4 那个路点），
   所以 `R3RIsWaypointWait()` 看到的仍是"合法路点目的地"⇒ 判为路点等待；
4. `HandleWaiting()`：`GetTimetabledWait()==0`（路点没开时间表）⇒ `wait_time=0` ⇒
   第一道门 `current_order_time < 0` 不成立 ⇒ `R3RWaypointWaitProbe(this, "pass", ...)`
   打印 `WP-WAITHOLD gate=pass`；
5. `SkipToNextRealOrderIndex()` ⇒ `real 4→5`、`impl` 锁步 ⇒ 打印 `WP-WAIT-ADV real=4->5`。

⇒ 玩家看到的「第 5 条没执行就跳到第 6 条」＝**索引 4 的路点在错误的格子上被判到达**，
第 372 轮的"一次只 +1"是对的，错的是"这一 +1 发生的地点/时机"。

### 12.3 为什么以前没炸 / 为什么现在才炸

- 原生路点：`IsWaitTimetabled()` 为假时根本不停 ⇒ 不会进 `TrainEnterStation` 的 waypoint 分支；
  R3R 为了"头停路点等挂接"无条件让它停，于是必须自己保证"停的那个路点就是目标路点"；
- 以往场景列车大多是**驶入目标路点格**才触发，`station == dest` 天然成立，缺项看不出来；
  本轮是**解挂原地恰好落在另一个路点格上**，才把缺项暴露成"跳单"。

### 12.4 修法（最小改动，仅 `src\order_cmd.cpp`）

在补丁条件里补上与原生一致的 dest 比对：

```cpp
if (waypoint && v->type == VehicleType::Train && this == &v->current_order &&
		this->GetDestination().ToStationID() == station &&
		R3RTrainStopsAtWaypoint(static_cast<const Train *>(v))) return true;
```

语义：**只有当列车进入的正是"当前路点订单"指定的那个路点时，才强制停车等挂接**；
进入任何别的路点格一律回落到原生判据（不停 / 按时间表停）。

风险面：只会**收紧**（把原本误停的格变成不停），不会新增停车点；
唯一要复核的是"多格路点"与"同一路点被两条订单复用"的场景——
两种情况下 `dest == station` 都成立，行为逐字不变。

### 12.5 复测判据

1. 解挂后停在别的路点格上，不得再出现 `WP-WAIT-ADV veh=57 real=4->5` 早于列车驶到 `21,7`；
2. 列车必须真的开到 `21,7` 的路点格后才 `real 4→5`，且那时 `tileEqDest=1`；
3. 负判据：列车进入 `21,7` 时仍必须 `ShouldStopAtStation()==true`（`gate=pass` 是允许的，
   因为路点没有等待时间，"停在路点上然后执行挂接"本来就是进入即推进）；
4. 原生路点（没有 `GOTO_COUPLE` 后继）行为逐字不变：不停、不产生 `OT_WAITING`。

### 12.6 未确认项

- **U-1**：`42,13` 的路点 `StationID` 无法从现有探针直接读出（`WP-HANDLE dest=3` 是
  索引 4 的 `dest`，不是当前格的路点）。但"必为路点格"可由 `TrainEnterStation(waypoint=true)`
  这一唯一入口反推，不影响修法。
- **U-2**：`destTx=21 destTy=7` 与 `dtile=917`（tile 917 = 21,14）不一致，
  说明 `DEPOT-ARR` 与 `WP-EXIT` 打的两个量不是同一个来源（前者 `dest_tile`，后者另一字段），
  仅作旁证，不作判据。
- **U-3**：`KI-536`（`veh=54` 同表 `real 3→0`）与本条无关，仍未定性。

---

## 十三、第 373 轮续二：修复后实机复测（2026-10-29 23:26 日志）

### 13.1 采样有效性

| 项 | 值 |
|---|---|
| `build\R3R_debug.log` | 182 060 B / 1806 行 / mtime `10-29 23:26:11` |
| `build\openttd.exe` | 52 487 168 B / mtime `10-29 23:23:03`（= 含 §12.4 dest 校验的修复版） |

⇒ 日志晚于 exe，采样有效。

### 13.2 判据结果

| 判据 | 结果 | 证据 |
|---|---|---|
| ① 不再提前 `WP-WAIT-ADV` | **通过** | 全文 `WP-WAIT-ADV`=0、`WP-WAITHOLD`=0、`WP-EXIT`=0（`Select-String` 计数为 0）；即没有任何"路点被判到达 → 等待门 → 推进索引"的记录 |
| ② 真到目标路点才推进 | **未覆盖** | 本次路点目标是 `27,7`（`:512 DEPOT-ARR destTx=27 destTy=7`），列车全程停在 `35,12`，`WP-EXIT`=0 ⇒ 没有"驶入目标路点"的样本可判 |
| ③ 进入目标路点仍 `ShouldStopAtStation()==true` | **未覆盖** | 同上；`ShouldStopAtStation`/`WP-STOP` 无探针命中 |
| ④ 原生路点无回归 | **未覆盖** | 本日志无无 `GOTO_COUPLE` 后继的路点场景 |

### 13.3 本次现场（与 KI-538 的关系）

- `:444`~`:508` `LINEND veh=57 tile=35,12 x=0..14 sig=1 spd=52→5` → 被信号挡住正常减速停稳，不是路点判到达；
- `:511` `SPD-DROP veh=57 tile=35,12 prev=5 order=3 real=4`；
- `:512` `DEPOT-ARR veh=57 spd=0 real=5(15) curType=4 tileEqDest=0 tx=35 ty=12 destTx=27 destTy=7` ⇒ **停在 35,12、目的地 27,7，`tileEqDest=0`**；
- `:521` `ORD-RETURN-OWN veh=57 num=7 real=5->1 bk=1//1 orphan=0` ⇒ 解挂提交点归还借来的表（KI-526 家族原语），随后 `:528/:535` `settle tag=decouple-v real 4->2 / 4->3`、`:579 DECOUPLE-DONE u=0 co=1 real=6`、`:645 LOCO-AFTER-DECOUPLE veh=57 curType=0 real=3 idxnext=6 tile=35,12`。

⇒ 本次解挂点的索引变动全部来自"借表归还 + decouple-v settle"，**没有任何一行来自路点等待/推进**。这与修复前的症状（`:WP-WAIT-ADV real=4->5` + `tileEqDest=0` + `spd=0`）形成正面对比：同样的"停在非目标格且 `tileEqDest=0`"形态，这次没有再触发索引推进。

### 13.4 结论与仍未收口的部分

- **KI-538 的症状（在错误路点格被判定到达并推进）本次未复现**，判据①达成；玩家"似乎解决了"的观察与日志一致。
- 但 `35,12` 是否路点格无探针可直读（`WP-*` 全 0 意味着本次**可能根本没进路点分支**），因此**不能排除"本次场景没覆盖修复路径"**。要收口必须补一个"列车真的驶入目标路点格"的样本：届时应见 `WP-EXIT why=entry` + `WP-WAIT-ADV real→+1` 且 `tileEqDest=1`（判据②③），以及一个"停在别的路点格"却**不**推进的样本。
- 若要在下次复测里直接判路点格身份，建议在 `VehicleEnterTile_Station()` 的 `IsRailWaypoint(tile)` 分支加一行只读探针（`WP-ENTER veh=%d tile=x,y dest=%d waypoint=%d eq=%d`），本轮**未加**（无源码改动）。

### 13.5 本轮源码状态

本轮**零源码改动、零构建**，仅做日志取证与文档回填。

---

## 十四、第 373 轮续三：补 `WP-ENTER` 只读探针（玩家指示「你先补探针吧」）

### 14.1 为什么需要它

`13.3` 的结论是「本次 `WP-EXIT`=0、`WP-*` 全 0，无法判断 `35,12` 到底是不是路点格」。
现有探针全部打在**判定之后**（`WP-EXIT` 在 `TrainLocoHandler` 的 return 处、`WP-WAIT-ADV` 在推进索引处），
所以「没进路点分支」与「进了但被某道门禁拦下」在日志里完全同形。本探针打在判定之前，
把每道门禁的读数一次列出。

### 14.2 落点与判读口径

- 文件：`src\station_cmd.cpp`，`VehicleEnterTile_Station()`（`:4590`）内。
- 位置：`HasStationTileRail()` 门禁（`:4617`）之后、`ShouldStopAtStation()` 判据（`:4618`）之前。
- 触发条件：`IsRailWaypoint(tile) && t == consist && R3RDbgOn()`（只记车头进入的路点格）。
- 输出格式（一行）：
  `WP-ENTER veh= tile=x,y wp= ord= real= impl= n= dest= eq= stop= headstop= bpe= prev= rail= spd= revdist= x= y=`
  其中 `wp`=该格是路点(恒 1)、`ord`=当前订单类型、`eq`=该路点是否就是当前订单的 `dest`、
  `stop`=修正后 `ShouldStopAtStation()` 的结果、`headstop`=`R3RTrainStopsAtWaypoint()`、
  `bpe`/`prev`/`rail`=前三道门禁读数、`x`/`y`=格内相对坐标。

判读表：

| 现象 | 结论 |
|---|---|
| `eq=1 stop=1` | 到了订单指定的路点 ⇒ 判到达符合判据③ |
| `eq=0`（无论 `stop` 为何） | **不是**当前订单的路点 ⇒ KI-538 的触发形态；此时必须 `stop=0` 才说明修复有效 |
| `rail=0` / `bpe=1` / `prev=0` | 被前置门禁拦下，与路点判据无关 |
| 整场没有任何 `WP-ENTER` | 列车根本没进路点格 ⇒ 复测场景未覆盖修复路径（不用再猜） |

### 14.3 防刷屏

`R3R_WP_ENTER_MAX_LINES = 4000` 全局行上限（KI-217 教训：无上限探针可把日志刷到 GB 级），
超限后只打一行 `WP-ENTER-CAP` 并永久停止。按「每车头每次进格一行」计算，正常场景量级在几十行以内。

### 14.4 本轮源码状态

改 `src\station_cmd.cpp`（.cpp 单文件，未碰任何 `src\*.h`、未动语言文件 ⇒ 增量合规）。

### 14.5 构建自证

- 入口：`R3R_fullbuild.cmd inc detach`（复用既有入口，未新建 .cmd）。
- `build\R3R_incbuild.guard.log`：`GUARD: incremental is safe (no header/lang file is newer than the newest object)`。
- `[2/3] Building CXX object ...\station_cmd.cpp.obj` → `[3/3] Linking CXX executable openttd.exe`。
- `build\R3R_incbuild.done` = `EXIT_CODE=0`；日志尾 `==== R3R build ended 2026/10/30 周五 14:30:19.96 MODE=inc EXIT_CODE=0`。
- 时间链：`src\station_cmd.cpp 14:27:23` → `station_cmd.cpp.obj 14:28:25` → `build\openttd.exe 14:30:16`。
- `read_lints`：该文件 0 条。
- exe 字面量自证：`findstr` 命中 `WP-ENTER veh=%d tile=%d,%d wp=1 ...` 与 `WP-ENTER-CAP lines=%d veh=%d ...`。

### 14.6 复测怎么用（原计划，已被 §十五 的实测取代，仅留作对照）

1. 用新 exe（`14:30:16`）复跑同一场景；
2. 抓 `WP-ENTER` 行：解挂后停放的那一格若出现在 `WP-ENTER` 里且 `eq=0`，则该格确为路点格 ⇒ 这是 KI-538 的完整触发形态，只要 `stop=0` 即证明修复有效；若 `eq=0` 却 `stop=1`，说明修复被绕过，需立刻回看 `ShouldStopAtStation()`；
3. 车真开到目标路点时应见 `eq=1 stop=1`，随后才允许出现 `real 4→5`；
4. 整场无 `WP-ENTER` ⇒ 场景未覆盖修复路径，需要换一个"解挂点落在路点格上"的存档复测。

---

## 十五、第 373 轮续四：探针版实机复测（2026-10-30 14:35 日志）

### 15.1 采样有效性

| 项 | 值 |
|---|---|
| `build\R3R_debug.log` | 169 076 B / 1657 行 / mtime `10-30 14:35:38` |
| `build\openttd.exe` | mtime `10-30 14:30:16`（= §十四 加 `WP-ENTER` 的探针版） |
| 体积/行数自洽 | 169 076 / 1657 ≈ 102 B/行，与本轮日志格式吻合；`WP-ENTER-CAP`=0（未触上限） |

⇒ 日志晚于 exe、探针确实生效，采样有效。

### 15.2 探针读数（全量 16 行，均在 `veh=57`）

`Select-String 'WP-ENTER veh='` 命中 16 行，行号
`:1226 :1242 :1247 :1253 :1257 :1270 :1276 :1282 :1294 :1300 :1309 :1325 :1334 :1353 :1373 :1602`，
**全部落在同一格 `tile=42,13`**，x 从 0 递增到 15（列车在这一格内从格首滑到格末），spd 从 60 递减到 5。

第 1 行（`:1226`）：

```
WP-ENTER veh=57 tile=42,13 wp=1 ord=6 real=3 impl=3 n=7 dest=4 eq=1 stop=0 headstop=0 bpe=0 prev=1 rail=1 spd=60 revdist=0 x=0 y=8
```

其余 15 行同形（仅 `spd`/`x` 递减递增）：

```
WP-ENTER veh=57 tile=42,13 wp=1 ord=6 real=4 impl=4 n=7 dest=3 eq=0 stop=0 headstop=1 bpe=0 prev=1 rail=1 spd=58..5 revdist=0 x=1..15 y=8
```

### 15.3 本轮结论（三条）

1. **探针本身工作正常**：`wp=1`、`rail=1`、`prev=1`、`bpe=0` 全绿 ⇒ `42,13` 确实是路点格且三道前置门禁全过，
   真正被拦的是最后的 `ShouldStopAtStation()`（`stop=0`）。这一条是第 373 轮续二靠反推拿不到的。
2. **`eq=0` 时确实不判到达**：`real=4` 的 15 行全部 `eq=0`（车所在路点 `dest=4` 与订单目标 `dest=3` 不同）、
   `headstop=1`，而 `stop=0`，并且全文 `WP-WAIT-ADV`=0、`WP-WAITHOLD`=0、`WP-EXIT`=0
   ⇒ **没有发生"在非目标路点格被判到达 → 等待门 → 推进索引"**。这正是 KI-538 要修的症状，本轮未复现。
3. **`ord=6` 是路点订单，不是条件单（重要更正）**：`src\order_type.h:75-95` 的枚举被 R3R 重排过 ——
   `OT_GOTO_STATION=1 / OT_GOTO_DEPOT=2 / OT_LOADING=3 / OT_LEAVESTATION=4 / OT_DUMMY=5 /
   OT_GOTO_WAYPOINT=6 / OT_CONDITIONAL=7 / OT_IMPLICIT=8 / OT_WAITING=9 / … / OT_DECOUPLE=15 /
   OT_GOTO_COUPLE=16 / OT_WAIT_COUPLE=17`。⇒ 本轮 `ord=6` = `OT_GOTO_WAYPOINT`，
   与 `headstop=1` 完全自洽（`src\train_cmd.cpp:672` 的类型守卫通过、`:682` 判"下一单是 `OT_GOTO_COUPLE`"）。
   **我最初把 6 当成 `OT_CONDITIONAL` 的推断已证伪，据此写的两条疑点见 §15.4（均已证伪）。**

### 15.4 疑点登记（含两条已证伪的误判）

**REG-373-A（已证伪，误判）**：曾判"`ord=6`=`OT_CONDITIONAL` ⇒ `real=3` 的 `dest=4` 是残留值 ⇒ `eq=1` 是假阳性"。
证伪依据 `src\order_type.h:83`：`OT_GOTO_WAYPOINT = 6`、`OT_CONDITIONAL = 7`。
⇒ 本轮 `eq=1`（`:1226`，`real=3`、`dest=4`、`tile=42,13`）是**真读数**：车确实到了当前订单指定的那个路点。

**REG-373-B（已证伪，误判）**：曾判"`curType=6` 却 `headstop=1` 矛盾、`stop=0` 可能不是 dest 校验拦的"。
证伪依据 `src\train_cmd.cpp:669-683`：`R3RTrainStopsAtWaypoint()` = `IsFrontEngine()` && `IsType(OT_GOTO_WAYPOINT)`
&& 下一条 `IsType(OT_GOTO_COUPLE)`；`ord=6` 是路点订单，`headstop=1` 自洽，`stop=0` 确实落在
`ShouldStopAtStation()` 的路点分支（`R3RTrainStopsAtWaypoint` 已过、`dest==station` 校验未过）。

**REG-373-C（仍未确证，中）**：`real=3→4` 的推动者未定。`ORD-IDX-WATCH site=tick-in / depotstay-in` 只标出
"在 tick 入口完成推进"，而全文 `WP-EXIT`=0 ⇒ 该推进**没有**经过 `TrainLocoHandler` 里打 `WP-EXIT` 的路点到达分支，
更可能是 `ProcessOrders` 的"按位置推进路过型路点"（`AdvanceOrdersFromVehiclePosition` 一类）。
⇒ 判据②暂时只能记"未确证"，不能记"通过"。

### 15.5 判据结果汇总（与 §13.2 对照）

| 判据 | 上一轮（§13） | 本轮（§15） |
|---|---|---|
| ① 不再提前 `WP-WAIT-ADV` | 通过（但 `WP-*` 全 0，属"未进分支"） | **通过，且这次是"进了分支"的通过**（`WP-ENTER`=16、`CAP`=0） |
| ② 真到目标路点才推进 | 未覆盖 | **未确证**（REG-373-C：`real 3→4` 无 `WP-EXIT`，推动者未定） |
| ③ 进入目标路点仍会停 | 未覆盖 | **未覆盖**（缺 `eq=1 && headstop=1` 样本；车未驶入 `dest=3` 的路点 `21,7`） |
| ④ 原生路点无回归 | 未覆盖 | **未覆盖**（本场景路点订单是 `ord=6`，无 `ord` 差异可对照） |

**本轮对 KI-538 的直接价值（一句话）**：拿到了 §13 缺失的那一半 ——
`headstop=1`（下一单要挂车）+ 车停在**别的**路点格（`eq=0`）+ `stop=0` + 零 `WP-WAIT-ADV`/`WP-WAITHOLD`，
即"路点等待门开着、但格子不是订单指定的那个路点"时**不再被判到达**。这正是 KI-538 的触发形态，且未复现。

本轮现场走向（尾部，约 `:1608`~`:1657`）：列车在 `42,13` 掉头（`REVERSEDONE db=1 mvfront=59`、`RA idx=57/58/59 tile=42,13`）
→ `RESV-BEGIN veh=57 tick=13689 tile=42,13 ... real=4 order=6` → `TPR-ZERO-REACH ... turn=1`
→ `CRT veh=57 order=6 dir=5 origin=40,13 td=0 found=1` → `CPLRES ... destTile=21,13 nTry=19 nOK=18`
→ `RESV-TERM ... tile=21,13` → `RESV-SELF-AIR veh=57 tile=42,13 track=0 kind=wpt`
→ `SPD-DROP veh=57 tile=42,13 prev=5 order=6 real=4 ... dtile=917`
→ `DEPOT-ARR veh=57 spd=0 real=4(6) curType=6 tx=42 ty=13 destTx=21 destTy=7 tileEqDest=0 tt=4 impl=4`。
⇒ 目标是 `21,7`（`dtile=917`），列车此刻仍在 `42,13`，**通往目标路点的那一段没跑完**，所以②③拿不到样本。

---

## 十六、第 373 轮续五：追加日志（1861 行）——等到正样本，真伪待一条源码判据

### 16.1 采样

| 项 | 前一轮（§15） | 追加后 |
|---|---|---|
| `build\R3R_debug.log` | 169 076 B / 1657 行 / 14:35:38 | **194 160 B / 1861 行 / 14:39:09** |
| `build\openttd.exe` | 14:30:16 | 14:30:16（未变，仍是探针版） |
| `WP-ENTER` | 16 行（`:1226`~`:1602`） | **仍 16 行、位置不变**（新增的 204 行里没有新的路点格进入） |

⇒ 追加部分（`1603`~`1861`）是**同一次会话续跑**，采样有效。

### 16.2 新增区段的标签分布（`1603`~`1861`，按出现次数）

`44×[R3R] 30×LINEND 28×ORD-IDX-WATCH 26×SKIP-STOPPED 18×DEPOTBLK 7×COUPLE-FAIL
5×CPL-S0-CHK 5×DEPOT-ARR 5×COUPLE-WAIT-OFFDEST 4×COUPLE-DEST-EMPTY 3×SPD-DROP
2×WP-LOCO-WAIT 2×AUTO-ORD 2×TPR-EXIT 2×CPLRES 2×CRT 2×RESV-TERM 2×RESV-BEGIN`
以及各 1 条：`RESV-SIGPASS WP-HANDLE MAXSPD-BREAK WP-WAIT-ADV WP-WAIT-KEEP CPLSPD
WP-WAITHOLD TPR-ZERO-REACH TPR-WALK REVERSEDIR REVERSEDONE RPFAIL AUTO-ORD-DROP WP-EXIT RESV-SELF-AIR RESV-END`

全日志计数：`WP-WAIT-ADV=1 WP-WAITHOLD=1 WP-EXIT=1 COUPLE-OK=1 DECOUPLE-DONE=1 CPL-HIT=0`。

### 16.3 关键行原文（行号为准）

```
1463: COUPLE-OK loco=48 rear=26 consist=0 co=1 real=7 type=1 tx=35 ty=12 x=574 y=200
1708: WP-EXIT why=entry veh=57 order=9 real=4 impl=4 n=7 spd=0 tile=21,13 revdist=0 stop=0
      parked=0 front=1 ... track=01 prog=0 dtile=917
1711: WP-LOCO-WAIT veh=57 mode=1 revdist=0 type=9 real=4 impl=4 n=7 dest=3 cot=0 waitT=0 spd=0
1712: WP-LOCO-WAIT veh=57 mode=0 revdist=0 type=9 real=4 impl=4 n=7 dest=3 cot=1 waitT=0 spd=0
1713: WP-HANDLE veh=57 wantWpp=1 dest=3 validWp=1 waitT=0 lateness=0 wait_time=0 cot=1
      ltype=0 sw=0 po=1 tile=21,13
1714: WP-WAITHOLD gate=pass veh=57 real=4 impl=4 n=7 wtime=0 cot=1 ltype=0 cow=0 stop=0
1715: WP-WAIT-KEEP veh=57 real=4 impl=4 dest=3 n=7
1716: WP-WAIT-ADV veh=57 real=4->5 impl=4->5
```

（长行在控制台被折行，上面把折行拼回；字段顺序未改。）

### 16.4 三种判读，取决于「`21,13` 是否属于路点 `dest=3`」

| 前提 | 结论 |
|---|---|
| `21,13` 属路点 3（即 `eq=1`） | 这是**判据③的正样本**：进入目标路点 → `OT_WAITING`(type=9) → `WP-WAITHOLD gate=pass` → `real 4→5`，**KI-538 可收口** |
| `21,13` 不属路点 3（即 `eq=0`） | **KI-538 症状复现**：`stop=0` 却 `gate=pass` 并推进 ⇒ 修复只挡住了 `ShouldStopAtStation` 一侧，等待门是另一条判据链 |

注意 `dtile=917` = `(21,7)`（128×7+21=917），而车在 `21,13` —— **订单 dest tile 与实际到达格不同**，这两者是同一路点的不同格、还是根本不同站，是上表的分水岭。

### 16.5 疑点 REG-374-A（中，必查）

`WP-EXIT why=entry ... stop=0` 与 `WP-WAITHOLD gate=pass` + `WP-WAIT-ADV` **同场并存**。
若 `WP-WAITHOLD` 的 `gate` 与 `ShouldStopAtStation()` 无关（自己另判"到没到路点"），
那么 KI-538 的 dest 校验只修了 `stop` 侧、没修 `wait` 侧 ⇒ 必须回源码把 `WP-HANDLE`→`WP-WAITHOLD`→`WP-WAIT-ADV` 的判据链读一遍才能定案。

### 16.6 待办

1. 读 `WP-HANDLE` / `WP-WAITHOLD` / `WP-WAIT-ADV` 的判据链，确认 `gate=pass` 是否经过 `dest==station` 校验；
2. 确认 `21,13` 的 station id（决定 §16.4 走哪一行）。

---

## 十七、收口轮：判据③正向样本成立，KI-538 可收口（第 373 轮续四）

### 17.1 采样

| 项 | 值 |
|---|---|
| 日志 | `build\R3R_debug.log`，194 160 B / 1861 行 / mtime `10-30 14:39:09` |
| exe | `build\openttd.exe` mtime `10-30 14:30:16`（探针版） |
| 判定 | exe 早于日志 ⇒ 采样有效，本轮新增 1603~1861 段可信 |

### 17.2 决定性源码证据（三条判据全部带 `dest == station`）

1. **`Order::ShouldStopAtStation(last_station_visited, station, waypoint)`**
   `src\order_cmd.cpp:5064`：
   ```cpp
   if (waypoint) return this->IsType(OT_GOTO_WAYPOINT) && this->IsWaitTimetabled() && this->dest == station;
   ```
2. **`Order::ShouldStopAtStation(v, station, waypoint)`（R3R 版，KI-538 修复点）**
   `src\order_cmd.cpp:5100-5102`：
   ```cpp
   if (waypoint && v->type == VehicleType::Train && this == &v->current_order &&
           this->GetDestination().ToStationID() == station &&
           R3RTrainStopsAtWaypoint(static_cast<const Train *>(v))) return true;
   ```
   注释 `:5091-5099` 自陈这就是 KI-538 的 dest 校验（无它会在**每一个**进入的路点格误判到达）。
3. **`VehicleEnterTile_Station` 的路点调向分支**（MakeWaiting 的另一处入口）
   `src\station_cmd.cpp:4602`：`... && GetDestination() == station_id && ...`，命中后 `:4611 MakeWaiting()` 并 `:4613 return {}`（不入 `EnteredStation`）。

⇒ **`waypoint == true` 时，代码里不存在"不带 `dest == station` 就能返回 true / 就能 MakeWaiting"的路。**

### 17.3 论证链：为什么 `21,13` 必然属路点 3（`eq = 1`）

1. 日志 `1706-1720` 证明该 tick 内发生了 **`MakeWaiting()`**：`1710 DEPOT-ARR ... curType=9`（`OT_WAITING`）、`1711/1712 WP-LOCO-WAIT type=9`、`1713 WP-HANDLE type=9`、`1709 SPD-DROP prev=5`（`cur_speed` 被写 0）。
2. `MakeWaiting()` 的调用点数量有限且**全部受守卫**：
   - `TrainEnterStation` 路点分支（`train_cmd.cpp:19658-19667`：`Waypoint::IsExpected` → `DeleteUnreachedImplicitOrders`(:19659) → `UpdateVehicleTimetable`(:19660) → `MakeWaiting`(:19664) → `cur_speed = 0`(:19667)）——**入口条件是 `vets.Test(VehicleEnterTileState::EnteredStation)`**（`:20213` / `:20623`）；
   - `VehicleEnterTile_Station` 路点调向分支（`station_cmd.cpp:4611`）——带 `dest == station_id`；
   - 其余（depot 到站 / 带 `ShouldStopAtStation` 守卫的等待方）同样带站点判定。
3. `EnteredStation` 只在 `station_cmd.cpp:4662` `if (!ShouldStopAtStation(...)) return {};` **之后**才设置。
4. 结合 17.2 第 1、2 条 ⇒ `ShouldStopAtStation(consist, GetStationIndex(21,13), /*waypoint=*/true)` 为真 ⇒ **`GetDestination().ToStationID() == GetStationIndex(21,13)`**，而 `GetDestination()` 就是 `dest=3`（`1711-1715` 全打 `dest=3`）。
5. ⇒ `GetStationIndex(21,13) == 3`，即 **`21,13` 就是路点 3 的格**（多格路点，`dtile=917`=(21,7) 是该路点的原点格；`station_cmd.cpp:4666-4672` 用 `GetTrainStopLocation(..., &station_length)` 说明路点具有长度、可跨多格）。

### 17.4 判定

| 判据 | 结论 | 依据 |
|---|---|---|
| ① 不误判 | **通过**（§十五） | 16 条 `WP-ENTER` 全在 `42,13`，`eq=0 stop=0 headstop=1`，零 `WP-WAIT-ADV` |
| ② 真到达能推进 | **通过** | `1716 WP-WAIT-ADV real=4->5 impl=4->5`、`1719 real=5 order=16`(GOTO_COUPLE) |
| ③ 等待门不绕过 dest 校验 | **通过（正向样本成立）** | 17.3 论证链：`gate=pass` 的前提是 `MakeWaiting` 已被合法触发，而它必然经过 17.2 的 `dest == station` |
| ④ `-1`/断链影响 | **通过** | `1711/1712 WP-LOCO-WAIT mode=1 cot=0` → `mode=0 cot=1` 属正常态迁移；`WP-WAIT-KEEP`(`1715`) 后立即 `ADV`，无残留 |

⇒ **KI-538 收口**：修复在"不误判"（判据①）与"真到达仍能停/能推进"（判据②③）两侧都有正样本。

### 17.5 更正 §16.3 / §16.4 的两处误读

1. **`WP-EXIT stop=` 不是 `ShouldStopAtStation()` 的返回值**。`train_cmd.cpp:22379` 打印的是
   `(int)consist->vehstatus.Test(VehState::Stopped)`。
   ⇒ §16.4 表里"`stop=0` 却 `gate=pass`"的推理基础作废，REG-374-A 随之关闭。
2. **`dtile=917`(21,7) 与 `tile=21,13` 不同格 ≠ 没到达**。路点可跨多格（17.3 第 5 条），`dest_tile` 取的是路点原点格。
   ⇒ `1710 DEPOT-ARR ... tileEqDest=0` 对多格路点属正常读数，不构成缺陷证据。

### 17.6 取证工具坑（REG-374-B，必记）

`build\` 被 `.gitignore` 忽略 ⇒ **`search_content` 对 `build\R3R_debug.log` 恒返回 0 命中（假阴性）**。
本轮 `tile=21,13` 用 `search_content` 得 0、改用 `Select-String` 实测 **61 处**；`WP-ENTER` 实测 16 行且 **无 `WP-ENTER-CAP`**（未触上限）。
⇒ 今后对 `build\*.log` 一律用 `powershell -NoProfile -Command "(Select-String ...).Count"`，不得以 `search_content` 的 0 结果反推"日志没有"。

### 17.7 遗留（不阻塞本条收口）

- `veh=57` 推进到 `real=5`(GOTO_COUPLE) 后本段日志只见 `COUPLE-FAIL loco=57`（找目标中）；`COUPLE-OK` 唯一一条是 `loco=48`。veh=57 自身挂接结果不在本 KI 范围，属另一条（挂接候选/等待方可达性）议题。
- `WP-ENTER` 的 `t == consist` 守卫（`station_cmd.cpp:4638`）+ 倒车 `db=1` 时 moving front 非车头，可解释"进入 21,13 却没有 `WP-ENTER`"——本轮未逐条证明，但判据①③不依赖它。
