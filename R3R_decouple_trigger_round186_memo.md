# 第 186 轮临时分析报告：解挂为什么必须写成「下一条是解挂」——触发形态的机制还原

日期：2026-10-01（第五轮）
性质：**只读码定性，未改任何源码**。承接 KI-281 / KI-282（`idx20 DECOUPLE` 被静默跳过）。
玩家原话：

> 我想知道，为什么解挂命令要"下一条是解挂命令"的时候就触发，而不是"当前是解挂命令"的时候再触发，解决了这个，我们会轻松许多。

---

## 一、决定性证据（引擎源码原文）

`src\order_cmd.cpp:4580-4583`（`ProcessOrders(Vehicle *v)` 的开头分派，定义在 `order_cmd.cpp:4559`）：

```cpp
case OT_DECOUPLE:
case OT_WAIT_COUPLE:
    /* Decouple orders are consumed by the station arrival logic; wait orders keep the vehicle waiting. */
    return false;
```

同处分派表里 `return false` 的还有 `OT_LOADING` / `OT_LOADING_ADVANCE` / `OT_WAITING` / `OT_GOTO_COUPLE`（`OT_LEAVESTATION` 仅飞机放行）。

**注释逐字给出了答案**：解挂命令「**由到站逻辑消费**」（consumed by the station arrival logic）。
它不是一个"可以开过去、开到了就收工"的命令，而是一条**依附在到站事件上的附加动作**。

---

## 二、机制还原：站台路径为什么"当前"不可能成立

引擎的订单循环是「每 tick 把 `cur_real_order_index` 指的那条命令加载成 `current_order`；能跑的算目的地让列车开过去；到达后推进索引」。

站台停靠期间的时间线：

| 阶段 | 索引（real） | current_order | 列车状态 |
|---|---|---|---|
| 驶向车站 | `idx17 GOTO_STATION` | `OT_GOTO_STATION` | 行驶中 |
| **进入站台 → 停稳** | **`idx17 GOTO_STATION`（未动）** | `OT_LEAVESTATION` | **停在站台** |
| 装卸 / 等待 | `idx17` 仍未动 | `OT_LEAVESTATION` | 停在站台 |
| **准备离站（速度起）** | **推进到 `idx18`** | `OT_DECOUPLE` 等 | 开始移动 |

关键：**索引的推进由"离站"事件触发，而不是由"停稳"触发。**
⇒ 在解挂**必须发生**的那一刻（列车停稳在站台上），索引必然还压在那条到站命令上。

现场逐字吻合：`3251 DECOUPLE-FIRE consist=29 real=17 mode=2 num=1 segs=1 eff=23`
——`idx17` 正是 `32x` 表里的 `GOTO_STATION(5)`，**不是** `idx18` 的 `DECOUPLE`。

所以"当前是解挂"这个状态在站台路径上**永远晚于**解挂该发生的时刻：等索引真推进到解挂时，列车早就带着车底开走了。
⇒ 触发条件只能写成「(当前是到站命令) ∧ (列车确实停在它的终点) ∧ (下一条是解挂)」。

上游 JGRPP 原生的 Decouple 订单本来就是"到达后动作"条目（必须跟在一条行车命令之后），R3R 继承了这一形态，只扩了边界 / 分组 / 借用等参数，没有动它的触发形状。

---

## 三、关键补充：`当前是解挂` 在**车库路径**上已经成立（现成先例）

`train_cmd.cpp:6421` 一带（`GetDecoupleVehicle()` 注释）逐字记录：

> after entering a depot, ProcessOrders advances cur_real **ONTO the DECOUPLE order itself** (real=5 here)

库里没有站台那套"停靠—离站"流程：列车进库后引擎沿库内线性序列把索引一路往前推，**推到解挂上就停住**（因为 `ProcessOrders` 对它 `return false`，列车就停在库里等）。这就是 KI-282 所说「缺口 A：`at_order_dest` 只在 `GOTO_STATION` / `GOTO_DEPOT` / **库内 `DECOUPLE`** 三处有取值分支」中第三条分支的来源。

⇒ **"当前是解挂"不是新发明，而是库里已经在跑通的一条路径**：列车停在库里、`real` 在解挂上、`ProcessOrders` 每 tick 返回 false、等 R3R 的钩子把链拆掉并推进索引，**没有卡死**。

⇒ 玩家想要的改造 = **把库里这条语义搬到站台**。

---

## 四、"下一条"形状的代价（KI-281 的来源）

因为执行解挂时索引还压在那条到站命令上，执行完必须有人把索引**跨过"到站 + 解挂"这一对**；而且**不能只跨一格**——只跨一格会正好停在解挂上，`ProcessOrders` 对它返回 false，列车僵死（这正是 `R3RSkipUnfireableDecoupleOrder` 注释里"停在解挂上会让 `ProcessOrders` 永不为它收尾"的由来）。

于是产生 `R3RSkipUnfireableDecoupleOrder()` 的两步规则：
1. 若停在 `GOTO_*` 终点且**紧接着**是解挂 ⇒ 跨过这一对；
2. 之后只要落点还是解挂就继续跨（上界 = 命令总数）。

该函数被调用在多个提交点（解挂后两半各一次、耦合完成落点一次）。
⇒ 一旦"耦合完成"的落点正好压在一条解挂上，那条解挂就被**顺手吃掉** —— 29 条表里的 `4553 COUPLE-SKIP-DECOUPLE head=27 stepped=1 real=21 type=17` 就是这么把 `idx20` 消掉的（KI-281 / KI-282）。

**即：KI-281 不是一个可以局部打补丁的 bug，而是"下一条"这个触发形态的必然副作用。**

---

## 五、改成"当前是解挂"能简化什么（玩家的"轻松许多"成立）

1. **跳过机制整块可删**：解挂执行完索引天然前进一格，不存在"一对"要跨（`R3RSkipUnfireableDecoupleOrder` 的两个分支、以及 `Couple()` 落点那次跳过都可退场）。
2. **连续多条解挂天然依次触发**：`idx18` / `idx20` 两条各自都能触发（现在第二条被吃）。
3. **触发判据收敛成一条**：「当前命令是解挂 ∧ 列车在合法位置（站台 / 车库）停稳」，替代现在的"三合一 + 库内特例"。
4. **KI-282 的缺口 A 直接消失**：不需要"回溯最近一条已完成的旅行命令终点"——不是那个回溯方案不好，而是改形态后**不再需要回溯**。
5. **两条路径统一**：站台与车库合成一条，"库里"特例分支可以删。

---

## 六、待玩家拍板的四条（逐条：争议点 / 可选项 / 我的建议）

> 说明：这四条是本改造**必须先定、无法由我单方面决定**的。每条给出选项与代价，玩家可任意一条改成别的组合。

**拍板 1 —— 站台推进时机（2026-10-01 玩家已给定口径，待确认）**
- 玩家口径（原话要点）：**"结束装卸之后就推进，不用列车起步"**；顾虑是"一进站就推进排程，会不会跳过车站的装卸流程"。
- 复核结论：**玩家的想法就是现状，引擎无需改动。** 站台的推进点自带两道前提——①装卸确实完成（`VehicleFlag::LoadingFinished`）②时刻表等待已满（`vehicle.cpp:3873`），两者同时成立才推进（`vehicle.cpp:3878-3895`）。推进前的 `LeaveStation()` 只是**逻辑结账**（清装卸状态、结算货费），与推进同一 tick 完成；列车**物理位移发生在下一 tick**。⇒ "进站就推进、跳过装卸"不可能发生（没装完推不动），"必须等列车起步"也不成立（它本来就推在起步之前）。
- 因此**取消原方案的"R3R 提前推进"**（旧选项 A / B 作废）：不新增推进点、不改引擎推进时机，解挂改为**在引擎这一次推进之后**求值。
- **已拍板（2026-10-01）：同 tick 闭环。** 时序已核：`TrainLocoHandler` 内先跑链头的 `HandleLoading`（站台推进 → 索引落到解挂），稍后才跑解挂闸门，故同一 tick 内就能看到"当前就是解挂"并完成解挂与收尾，列车不会带着"索引已在解挂"的状态起步。

**拍板 2 —— 到站结账与解挂的先后（已被拍板 1 连带定案）**
- 既然"推进"的前提就是结账完成，顺序天然固定为：**装卸结账 → 推进 → 解挂**。
- 结账内容（装卸 / 乘客 / 时刻表）一字不动，只是把解挂的求值点接到推进之后。
- **已拍板（2026-10-01）：无异议。**

**拍板 3 —— 耦合后落点正好压在解挂上怎么办？（KI-281 的正式解法）**
- 现状：跨过去 → 顺手吃掉后面那条解挂（`idx20` 就是这么没的）。
- 选项 A：**不跨**，索引停在解挂上，等列车到位后按新形态正常触发。要求新增"位置合法性"判据，拒绝"列车还在路点上就解挂"。
- 选项 B：保留跨过，但只跨"确实无物可解"的那一条，并留痕。
- 建议：**A**（与改形态同向，一举消掉 KI-281）；B 只是缓解症状。
- **已拍板（2026-10-01）：A。** 追加要求（玩家）：位置合法性判据必须覆盖**车长 > 站台**的列车，见 §十。

**拍板 4 —— 连续多条解挂：谁执行、解出哪半（KI-282）**
- 选项 A：每条解挂各自按它前面最近的一条行车命令与当前位置，**独立**决定执行者与解出范围。
- 选项 B：整张表一次算清所有解挂的归属。
- 建议：**A**（每条自洽、易验证；B 在多交接点场景下推导链更长）。
- **已拍板（2026-10-01）：A。** 实现落点：每条解挂只按"当前 real 索引就是它自己"+ 列车当前停稳位置求值，边界取这条订单自己的设定（`GetDecoupleVehicle()` 只读 `cur_real`，不再回看 `cur_real+1`），不做整表推演。

---

## 七、结论

- 玩家方向**正确且比"补回溯判据"更根本**：上一轮的缺口 A 提案是"把特例放宽"，本方案是"把特例消灭"。
- "下一条"的成因不是设计者偏好，而是**引擎的索引推进时机**决定的：站台上"离站才推进"，而解挂必须在"停稳"时发生，两者时间对不上。
- 改造的实质 = **把解挂从"寄生在前一条命令到达事件上的动作"改成"一条自带完成事件的命令"**，完成事件 = "列车停稳在合法位置（站台 / 车库）"。库里已有同一语义在运行，属迁移而非新发明。

## 八、未确认项

- U-1：站台路径"索引停在解挂上跨 tick"到底会不会被引擎的 idle / 回库逻辑碰（需实测或读 `CheckIfTrainNeedsService` 与 `TrainLocoHandler` 的相关守卫）。
- U-2：到站结算（装卸 / 乘客 / 时刻表）与解挂在同一 tick 内的先后，是否需要按"结账后再拆链"的原顺序保留。
- U-3：`idx18` 与 `idx20` 两条解挂的"排程主人"如何逐条计算（承接 KI-282 未确认项 ①②）。
- U-4：`veh=30`（`nord=29` 却永不推进）是否同一张表的另一半（第 183 轮待办 3，仍未定性）。

## 九、复测判据草案（改形态后）

1. 同场景出现 `DECOUPLE-FIRE … real=20`（不再被 `COUPLE-SKIP-DECOUPLE` 吃掉），且全文不再出现 `COUPLE-SKIP-DECOUPLE` / `DECOUPLE-SKIP` 这两个跳过标签（或仅剩"真无物可解"的告警形态）。
2. 连续两条解挂各自触发，`idx19` / `idx21` 两个 `WAIT_COUPLE` 各归各半。
3. 车库内解挂零回归（该路径本就是这个形态，应逐字不变）。
4. 站台解挂全程不得出现列车"停在解挂上不动"超过一 tick 的现象（若采用同 tick 方案）。

---

## 十、追加要求：车长 > 站台（玩家 2026-10-01 点名）

**引擎自己的事实（三条证据）**

1. 超长列车停稳时，越出站台末端的那部分车被打上 `VehicleRailFlag::BeyondPlatformEnd`（`train_cmd.cpp:616-639` 的定位计算），**这些车不在站台 tile 上**。
2. 引擎承认"前端可能已不在站台 tile 上"：离站判据写作 `!IsTileType(moving_front->tile, Station) || moving_front->flags.Test(BeyondPlatformEnd)`（`train_cmd.cpp:4023-4025`、`4300-4303`）。
3. 引擎为此专门提供 `Train::GetStationLoadingVehicle()`：**从移动前端跳过 `BeyondPlatformEnd` 的车，返回第一辆真正在站台上的车**（`train.h:414-419`）。

**对解挂的三条硬要求**

- **R-1 位置判据不得只看链头那一格。** 现行闸门 `IsTileType(consist->tile, Station) && 站 ID 匹配`（`train_cmd.cpp:16435-16436`）对超长列车会**恒假**（链头在站台外）⇒ 解挂永不触发、列车僵在站台。必须改成"**列车与该站的对应关系**"：以链中第一辆真正在站台上的车（`GetStationLoadingVehicle()`）取站 ID 与目的地比对；链头越界但链中仍有车在站台上 ⇒ 视为"停在站台上"。
- **R-2 解挂后必须重算平台对齐标志。** 拆链改变了列车长度，`BeyondPlatformEnd` / `NotYetInPlatform` 的覆盖范围随之改变；`AdvanceInPlatform` 若残留，会让下一次停站立刻进入 `OT_LOADING_ADVANCE`、限速蠕行出站（KI-128）。耦合路径已显式清理（`train_cmd.cpp:10161-10166`），**解挂路径目前没有**，须补齐。
- **R-3 位置合法性是"关系"不是"格"。** 解出的某一半整段落在站台外时，其"停稳"语义按列车自身判断（`cur_speed == 0` 且索引在解挂上），既不因"它不在站台 tile 上"而拒绝解挂，也不因"它在站台外"而判非法——否则超长列车这一支永远解不开。

---

## 十一、实施清单（2026-10-01 上手）

| 项 | 内容 | 落点 |
|---|---|---|
| A | 解挂闸门改为「`cur_speed == 0` ∧ **当前 real 订单就是 `OT_DECOUPLE`** ∧ 停在合法位置（站台 / 车库，按 R-1 判）」，删去"下一条是解挂"的 `next_real` 分支 | `train_cmd.cpp:16325-16520` |
| B | 收敛跳过机制：删掉 `R3RSkipUnfireableDecoupleOrder()` 的**第 1 步**（跨"到站 + 解挂"这一对）；第 2 步保留，语义改为"当前就是解挂且**确实无物可解** ⇒ 跨过这一条"。`Couple()` 落点那次跳过（`train_cmd.cpp:7498-7501`、`7593`）随拍板 3 一并退场 | `train_cmd.cpp:7118-7149` |
| C | 解挂后两半各自前进一格（`decouple_idx + 1`），不再需要"跨一对"；每条解挂独立求值（拍板 4 A） | `R3RAdvanceAfterDecouple()`（`train_cmd.cpp:7177+`）、`DecoupleTrain()` |
| D | 解挂后清理 / 重算平台对齐标志（R-2），沿用耦合路径的写法 | `train_cmd.cpp:10161-10166` 的对偶位置 |
| E | 探针：`DECOUPLE-FIRE` 增记 `plat=`（站台内车数 / 越界车数）与 `idxnext=`（执行后的落点索引），供 R-1 / 拍板 4 现场复测 | `train_cmd.cpp:16539-16549` |

**编译状态（2026-10-01）**：`_tmp_inc_build.cmd` → `EXIT_CODE=0`，`[3/3] Linking CXX executable openttd.exe`，无新增告警。

**本轮落地清单（已完成）**

1. 闸门从"内联在 `ProcessOrders` 之前执行"改为 **lambda `r3r_try_decouple_arrival`**，求值点移到 `HandleLoading(mode)` **之后** —— 这是"同 tick 闭环"的物理前提（原先闸门早于推进，装卸完成那一 tick 必然错过）。
2. 闸门判据："三合一 + 下一条是解挂 → `should_decouple`" 改为 **「当前 real 就是 `OT_DECOUPLE`」**；位置合法性改为 `IsRailDepotTile(consist->tile) || 链中第一辆在站台上的车`（R-1，兼容车长 > 站台）。
3. `R3RSkipUnfireableDecoupleOrder()` **第 1 步删除**（跨"到站 + 解挂"一对），只留"落点自己就是解挂且无物可解"。
4. 耦合落点不再一律跳过：新增条件 —— **只有在"落在解挂上且此刻不在站台/车库"时才兜底跨过**，其余情况按拍板 3 A 停在解挂上等闸门。
5. 解挂后对留下的一半清 `AdvanceInPlatform`（R-2，KI-128）。
6. 探针：`DECOUPLE-FIRE` 增 `plat=站台内/越界`，`LOCO-AFTER-DECOUPLE` 增 `idxnext=`。

**新增复测判据（§九 之外）**

5. 超长列车在站台解挂：`DECOUPLE-FIRE` 的 `plat=` 显示"站台内 ≥ 1 辆"，且解挂成功、两半各自继续。
6. 解挂后列车离站不得出现"限速蠕行"（KI-128 签名：`AdvanceInPlatform` 残留导致的低速通过）。

---

## 十二、落地后的第一次复测：两条现场回归（2026-10-01，已修）

**玩家原话**：「第一个，等待挂接命令被莫名跳过了，第二个，我的车底它会自己蠕动」。

**现场**：`build\R3R_debug.log`（1021 行 / 64 KB），由本轮 A~E 落地后的 exe 产出。承接 §十一 的落地清单。

### 12.1 决定性证据链（日志锚点逐行）

| 行 | 原文（节选） | 读出什么 |
|---|---|---|
| 707 | `DEPOT-ARR veh=24 spd=0 real=3(15) … tx=33 ty=9` | 新闸门口径成立：**当前 real = 3 就是 `OT_DECOUPLE`** |
| 716-717 | `DEPOT-ARR veh=24 spd=0 real=3(15)` → `DECOUPLE-FIRE consist=24 tx=33 ty=9 real=3 mode=2 num=1 segs=2 eff=0 plat=27/0` | 解挂在 idx 3（`head=6` 那张 29 条表：`i=3 type=15`） |
| — | `i=4 type=17`（`WAIT_COUPLE`）、`i=5 type=1`（`GOTO_STATION(5)`） | 玩家期望：解出方应停在 **idx 4 的等待挂接** |
| 754 | `DECOUPLE-JUMP fire_real=3 dec_idx=4 wait_idx=65535 step=-1 v=24 u=0 n_u=29` + `JUMP-ORD 4 type=17` | **解挂索引被记成 4 = D+1** |
| 759-761 | `DECOUPLE-ADV dec_idx=4 v_real=1 u_real=5` → `DECOUPLE-DONE u=0 co=1 real=5` | 解出方落点 **5**，**跨过了 idx 4 的等待点** |
| 760 | `DECOUPLE-ODOF first=0 second=0` | 策略也是从错的那格（4=等待点）读的 |
| 829 | `DEPOT-ARR veh=0 spd=0 real=5(1) … destTx=58 destTy=19` | 车底拿到 `GOTO_STATION(5)`，**开始自己驶离 = "蠕动"** |

### 12.2 根因（两条，均为本轮实现自身的缺陷）

**根因 1（KI-284，高）`decouple_idx` 差一 —— 旧闸门口径的 `+1` 残留。**

`DecoupleTrain()` 里：

```cpp
const VehicleOrderID fire_real = v->cur_real_order_index;
VehicleOrderID decouple_idx = fire_real;
if (n_fire > 0) decouple_idx = (decouple_idx + 1) % n_fire;   // ← 旧口径残留
```

这个 `+1` 在**旧闸门**下是对的：那时 `fire_real` 是"刚跑完的到站命令"，解挂是它的后继，`+1` 正好落在解挂自身（KI-215c 的注释也是按这个写的）。§十一 的 A 项把闸门改成「当前 real 就压在 `OT_DECOUPLE` 上」之后，`fire_real` **本身就是解挂的索引** ⇒ 记成 D+1 使下游三处全部差一格：

1. `R3RAdvanceAfterDecouple()` 从 `decouple_idx + 1` 重启 ⇒ 解出方落到 **D+2**，**跨过紧随解挂的 `WAIT_COUPLE`**（现象 ①）；
2. `DECOUPLE-ODOF` 从 `D+1` 读订单策略 ⇒ 读到等待点、玩家声明被静默忽略；
3. `DECOUPLE-JUMP` 的 `JUMP-ORD` dump 起点偏一格，日志自证被带偏。

**⇒ 玩家的两条报告是同一格的两种表现**：跳过等待点（原因）→ 车底落在 `GOTO_STATION` 上开走（现象 ②）。

**根因 2（KI-285，中）R-2 的平台标志只清了一半。**

```cpp
DecoupleTrain(consist, true);
for (Train *w = consist; w != nullptr; w = w->Next()) w->flags.Reset(VehicleRailFlag::AdvanceInPlatform);
```

`DecoupleTrain()` 返回后 `consist` 只是**含原链头的那一半**，被摘出去的另一半已是独立链，`consist->Next()` **永远走不到**。平台标志是逐车字段，两半都可能带 ⇒ "解出的车底继续跑"的场景残留 `AdvanceInPlatform`，下一次停站立刻进 `OT_LOADING_ADVANCE` 被限速 = **KI-128 的限速蠕行**，与"自己蠕动"这一措辞吻合。

### 12.3 修法

| 编号 | 内容 | 落点 |
|---|---|---|
| 修 1 | `decouple_idx` 不再假定口径，**按订单类型自证**：`decouple_idx = fire_real`，只有 `GetOrder(fire_real)` 不是 `OT_DECOUPLE`（旧口径）时才 `+1` —— 两种闸门口径都不差一 | `src\train_cmd.cpp` 的 `DecoupleTrain()`（`decouple_idx` 计算块） |
| 修 2 | 平台标志快照**移到拆链之前**拍（拆前整链收进 `std::vector<Train *>`），拆完对快照逐个清 `AdvanceInPlatform` ⇒ 两半都覆盖 | `src\train_cmd.cpp` 解挂闸门 lambda 内 `DecoupleTrain()` 前后 |

**未动**：`R3RApplyDecoupleOrdersStrategy()` 的 `ODOF_WAIT_FOR_COUPLE` 分支本身带 `wait-already` 兜底（若落点已是 `WAIT_COUPLE` 就不再插），所以修 1 修正读数后不会产生重复等待点。**未改任何 `src\*.h`。**

### 12.4 构建自证（2026-10-01）

- 入口：复用既有 `_tmp_inc_build.cmd`（**未新建任何 `.cmd`**）；改动仅 `src\train_cmd.cpp` ⇒ `build\R3R_incbuild.guard.log` = `GUARD: incremental is safe`。
- `build\R3R_incbuild.done` = `EXIT_CODE=0`（17:53）；日志尾 `[3/3] Linking CXX executable openttd.exe`；`error C*` / `fatal error` / `FAILED:` / `build stopped` 计数 0。
- 时间戳：`src\train_cmd.cpp` 17:48:32 → obj 17:51:52 → `build\openttd.exe` 17:53:26（51 565 056 B）。`read_lints` 0 条。

### 12.5 新增复测判据（承 §九 的 1~6）

7. `DECOUPLE-JUMP` 应打 `dec_idx=3`，`JUMP-ORD 3 type=15(解挂)`、`JUMP-ORD 4 type=17(等待)`；`DECOUPLE-ADV` 应为 `u_real=4`、`DECOUPLE-DONE u=0 … real=4`。
8. 同场景车底**停在原站台**（不再出现 `DEPOT-ARR veh=0 real=5(1)` 那种"解出即驶离"）。
9. 解出的车底此后若被再次挂走，离站**不得**出现低速通过（KI-128 / KI-285 的 `AdvanceInPlatform` 残留）。

### 12.6 本轮仍未确认

- U-1（承接 §八 / KI-283）：同 tick 闭环落地后"索引停在解挂上"的跨 tick 窗口已消掉，但**未实测**（含 idle / 回库逻辑是否碰它）。
- U-4：`veh=30`（`real=17` = `WAIT_COUPLE` 停驻）定性仍未做。
- KI-281 / KI-282（`idx20 DECOUPLE`）**未动**：本轮形态改造是它们的前置，须以本轮 exe 复测后再定。

### 12.7 §十「车长 > 站台」的落地归档（KI-286）

本节把 §十 的三条硬要求（R-1 / R-2 / R-3）逐条落到代码位置，并**正式立目为 KI-286**（`R3R_KNOWN_ISSUES.md` 第 186 轮续小节，紧随 KI-285 之后）。此前 KI-283 的状态条只写了"见 KI-286"，条目本身尚未落笔，本次补上，避免悬空引用。

| 编号 | 状态 | 代码位置 | 说明 |
|---|---|---|---|
| R-1 位置判据按"列车与该站的对应关系" | **已落地（有一处残留）** | `src\train_cmd.cpp:16462-16467` | `plat = consist->GetStationLoadingVehicle()`（`src\train.h:414-419`，跳过 `BeyondPlatformEnd` 的车）；`at_order_dest = IsRailDepotTile(consist->tile) \|\| (plat != nullptr && IsTileType(plat->tile, TileType::Station))` ⇒ 链头越界但链中仍有车在站台上时，解挂照旧触发 |
| R-2 解挂后重算平台对齐标志 | **已落地（本轮续补齐）** | `src\train_cmd.cpp:16558-16581` | 快照移**到拆链前**拍（整链 `std::vector<Train *>`），拆后逐车清 `AdvanceInPlatform`；对偶于耦合路径 `src\train_cmd.cpp:10168-10181`。**§12.2 的根因 2 / KI-285 就是本条此前只做了一半** |
| R-3 位置合法性是"关系"不是"格" | **结构性满足** | 闸门 `r3r_try_decouple_arrival`（`src\train_cmd.cpp:16366`） | 闸门只对整链求值，代码内没有任何针对**解出侧**的 tile 检查 ⇒ 解出的一半整段落在站台外既不拒绝解挂、也不判非法 |

- **现场取证探针**：`DECOUPLE-FIRE … plat=<站台内车数>/<越界车数>`（`src\train_cmd.cpp:16537-16554`）。`plat_in≥1 && plat_beyond>0` 就等于现场证明 R-1 走的是"链中取车"支。
- **残留（未做，待玩家拍板）**：旧判据里的「**站 ID 与目的地比对**」（本报告 §十 R-1 引的 `IsTileType(consist->tile, Station) && 站 ID 匹配`）在新闸门里**没有了** —— 新判据只判 tile 类型。风险面：离站那一 tick 若因 `cur_speed != 0` 未求值，索引会带着 `OT_DECOUPLE` 离开，此后在**任意**站台停稳时闸门仍会命中。是否补回、超长列车用链中哪辆车取站 ID，须与玩家口径一致后再动。
- **复测判据**：承 §十一 判据 5 —— ①`DECOUPLE-FIRE` 的 `plat=` 显示"站台内 ≥ 1 辆"且越界 > 0；②解挂成功、两半各自继续（不再"僵在站台、解挂永不触发"）；③此后任一半离站不出现低速蠕行（KI-128 / KI-285 签名）。
- **构建**：本条的三处落点都已在 §12.4 那次增量构建（17:53:26 / `EXIT_CODE=0`）之内，**本次仅补文档，未改任何源码、未重新编译**。
