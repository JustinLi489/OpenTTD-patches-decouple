# 等待区路点：蠕动 / 硬归零 / 走错站台（2026-10-23 第三轮）

玩家报告（原话要点）：
1. 主动方列车在路点并没有完全停住，而是以 2km/h 的速度蠕动。
2. 一帧内，主动方列车几乎是鼻尖进入等待区路点所在格，速度从 55 瞬间变为 2，且不是正常刹车的线性下落，而是**没有斜率**的变化。
3. 主动方列车在被动方到位之后，按理说怎么样寻路都不会出错，但偏偏它走错站台了。
4. 玩家猜想：前往路点命令总是受到它后面第一个前往车站/车库命令影响，或者说，它只是寻路里面的一个配菜，前往车站和车库才是主菜；现在它后面的命令是前往车站挂接，它没有这种主菜的搭配方案，所以导致列车行为紊乱。

## 一、采样有效性

| 项 | 值 |
| --- | --- |
| 日志 | `build\R3R_debug.log`，1 575 506 B / 9 912 行，LastWriteTime 2026-10-23 13:55:31 |
| 产生者 | `build\openttd.exe` 2026-10-23 13:46:15（= 第二轮 KI-444 修复版，`EXIT_CODE=0` 那一次） |
| 结论 | 采样晚于 exe，**本轮观测的是第二轮修复之后的实际行为** |

## 二、现象 1 / 2 的证据（蠕动与无斜率硬归零）

```
:9643  LOCO-MOVE veh=21 tile=248,233 j=192 spd=2 track=01 dir=1 db=1 stuck=0 dtile=491798
:9689  LOCO-MOVE veh=21 tile=248,233 j=192 spd=2 track=01 dir=1 db=1 stuck=0 dtile=491798
:9705  CPL-WAITCUT veh=21 state=2 wp=17 tile=248,233
:9721  SPDD veh=21 from=2 to=0 dt=21 ctx=waitcut cfrom=2 cto=0 cage=0 m=0 tile=248,233 x=3977 db=1 order=16 real=12 stuck=0 fp=0 one=0 prog=0 wait=0 track=01 dtile=491798
:9725  SPDD veh=21 from=20 to=21 dt=3 ctx=hold-for-couple cfrom=2 cto=0 cage=1 m=1 tile=248,233 x=3979 db=1 order=16 real=12 … prog=191 wait=0 track=01
:9726  SPDD veh=21 from=21 to=22 dt=0 ctx=hold-for-couple cfrom=2 cto=0 cage=1 m=1 …
:9727  SPDD veh=21 from=22 to=23 dt=0 ctx=hold-for-couple cfrom=2 cto=0 cage=1 m=1 …
:9728  SPDD veh=21 from=23 to=24 dt=0 ctx=hold-for-couple cfrom=2 cto=0 cage=1 m=1 …
:9729  SPDD veh=21 from=24 to=25 dt=1 ctx=stale cfrom=0 cto=0 cage=2 m=1 …   （此后 ctx=stale，速度 25→48 一路升）
:9754  SPDD veh=21 from=47 to=48 dt=1 ctx=stale …
:9755  SPDD veh=21 from=48 to=46 dt=0 ctx=stale …                             （此处才开始有斜率地刹车）
:9766  SPDD veh=21 from=21 to=18 dt=1 ctx=stale …
:9768  LOCO-MOVE veh=21 tile=250,233 j=198 spd=15
:9780  LOCO-MOVE veh=21 tile=251,233 j=208 spd=30
:9782  LOCO-MOVE veh=21 tile=252,233 j=194 spd=30
:9784  LOCO-MOVE veh=21 tile=253,233 j=202 spd=30
```

读数判定：

- `spd=2` 在 `248,233` 上持续（9643 与 9689 同值同格），**这就是玩家看到的 2km/h 蠕动**。
- `cur_speed` 被写 0（`cfrom=2 cto=0`），但下一 tick 又从 0 涨回 2，稳定在 2 ⇒ 不是"停住"，是"每 tick 被按住又被恢复"。
- `ctx=hold-for-couple` 连打 4 行后立刻转 `ctx=stale`，说明 `R3RShouldHoldForCouple()` 只在那几个 tick 为真，**不是持续按住**；随后列车一路加速到 48，再线性刹到 18，再加速到 30 —— 这是玩家说的"没有斜率"只在进入/离开路点那两处出现，与硬写 `cur_speed=0; subspeed=0;` 的落点一致。

硬归零的两个代码落点（都在 `src\train_cmd.cpp`）：

- `:16009-16011`：`DoTrainCouplePathfind()` 的未就绪/截断分支，`R3RSpdCtx(consist, "waitcut"|"pair-not-ready", cur_speed, 0); cur_speed = 0; subspeed = 0;`，**在 `r3r_wait_cut == 2` 的 return 之前**执行 ⇒ 第二轮 KI-444 只挡住了"撤预留 + Stuck"，没有挡住"硬归零"。
- `:21166-21171`：`R3RShouldHoldForCouple(consist)` 门禁，`strict_max_speed = advisory_max_speed = 0; R3RSpdCtx(consist, "hold-for-couple", cur_speed, 0); cur_speed = 0; subspeed = 0;`。

## 三、现象 3 的证据（走错站台）

```
:9723  [R3R] CPL-PAIR tick=3019 act=21 tgt=24 dist=18 actTile=248,233 tgtTile=265,234
:9783  [R3R] CPL-PAIR tick=3030 act=21 tgt=24 dist=13 actTile=253,233 tgtTile=265,234
:9787  CPL-WAITCUT veh=21 state=0 wp=17 tile=253,233            （放行）
:9788  PFD-ACCEPT kind=waiting tile=278,234 t=24 ordType=17 wc=1
:9791  CPL-PATHFOUND veh=21 found=1
:9792  CPL-SCAN tick=3030 veh=21 found=1 closed=5 limit=10000 nodes=25 enter=20 noplat=11 nores=0 noconsist=6 consist=3 rej=0/0/0/0 acc=0/3 accwp=0 last=278,234 lastveh=24 why=acc-wait
:9793  CPL-HEAD best=278,234 head=265,234 on=0
:9794  CPL-BEST veh=21 tile=278,234 td=8 cost=16442
:9795  CPL-BACK pPrev=278,234 td=8 parent=259,234
:9796  CPL-BACK pPrev=259,234 td=4 parent=258,233
:9797  CPL-BACK pPrev=258,233 td=8 parent=254,233
:9798  CPL-BACK pPrev=254,233 td=8 parent=253,233
```

- 车底本体（`veh=24` 链头）在 `265,234`（`:9723/:9783 tgtTile`、`:9793 head=265,234`）。
- 但寻路终点选成 **`278,234`**（`:9794 CPL-BEST`），`CPL-HEAD on=0` 明确表示 **best 不在车底链头上**。
- `:9788 PFD-ACCEPT kind=waiting tile=278,234 t=24 ordType=17` 给出终点来源：这是 yapf_destrail 按"目标车 `veh=24` 的 `OT_WAIT_COUPLE` 订单目标格"接受的，不是车底当前所在格。
- `CPL-BACK` 显示实际路径 253,233 → 254,233 → 258,233 → 259,234 → 278,234，**会从车底所在的 265,234 旁边穿过去、继续往东 13 格**。

⇒ 与玩家观测一致：车底已经在 `265,234` 停好（`:9655 DEPOT-ARR veh=24 spd=0`），但主动方被引向 `278,234`（车底订单里的等待点/对侧站台）⇒ **走错站台**。

## 四、连带发现（疑点，先登记）

- 疑点 REG-445-A：`:9687 RESV-NOBOOK veh=24 tile=265,234 order=17 caronly=0 reason=parked-waiter` —— 等待车底停稳后被第 127 轮 KI-208 门禁拒绝订路；`:9686 TRP veh=24 ok=0 res=0 mstuck=1`。若该门禁让它永远到不了 `278,234`，而主动方又被引向 `278,234`，两边**永远不会合**。期望值：车底到位后应能完成配对；实测：车底 spd=0 + 零预留 + mstuck=1。
- 疑点 REG-445-B：`:9725-9728` 的 `ctx=hold-for-couple` 只出现 4 行就转 `stale`，与"按住等待"的语义不符，需确认 `R3RShouldHoldForCouple()` 的判据为何在相邻 tick 反复翻转（怀疑与 `r3r_wait_cut` 的 0/2 抖动同源）。

## 五、对玩家猜想的评估（待验证）

玩家猜想"路点只是配菜，前往车站/车库才是主菜"。就代码现状看：等待区路点是**独立的主判据**（`R3RShouldCutCoupleAtWaitPoint()`，`:15905` 与 `yapf_rail.cpp:843` 两处调用），并非附属。但**寻路终点确实由车底的订单目标（车站/等待点）决定**（`:9788 kind=waiting … ordType=17`），这与玩家"被后面的车站命令支配"的直觉在结果上吻合：

- 猜测根因（待验证）：`yapf_destrail` 的 `kind=waiting` 接受判据把"车底 `OT_WAIT_COUPLE` 订单的目标格"当成合法会合点，而非"车底当前所在格/运动前端格"。当车底已经停在自己的等待点上、而订单目标指向别处（或对侧站台）时，主动方就会开向那个目标格。

## 六、复测判据（本轮修复后）

1. 主动方进入路点格后 `LOCO-MOVE … spd=` 必须为 0 且**稳定为 0**，不得出现持续 `spd=2` 的蠕动。
2. 速度从巡航值降到 0 必须经由正常刹车（`SPDD` 逐 tick 递减、有斜率），不得出现单帧 `from=55 to=2` 的跳变；日志中不得再出现 `ctx=waitcut` / `ctx=hold-for-couple` 行里 `cur_speed` 从非零直接归零。
3. 放行后 `CPL-HEAD` 的 `best` 必须等于 `head`（`on=1`），不得再出现 `best=278,234 head=265,234 on=0`。
4. 车底已停稳时，主动方最终 `COUPLE-OK`，不再沿 `CPL-BACK` 穿过车底所在格继续往东。
5. 负判据：没有等待区路点的普通 `OT_GOTO_COUPLE` 行为逐字不变。

---

## 七、第二轮复核（2026-10-23）：终点格为何记错 + 刹车的正确形态

### 7.1 玩家口径（本轮）

- 速度硬写 0 不对：**要求能正常刹车就正常刹车**。场景里列车从东边来，东西走向的路点，鼻尖应能停在**路点最西侧的停车标**；现在鼻尖还在格东边就被暴力写 0。
- 终点记错的猜测：「主动方 A 站出发去 B 点，鼻尖碰到 B 点就触发 YAPF，YAPF 默认按普通的前往车站设了一个终点，然后用挂接寻路条件不符合卡住列车；等条件符合时，终点早就写好了，写好的地方刚好是空站台，于是走错路。」

### 7.2 判读：骨架对，机制要改三处

**(1) 不是"鼻尖碰到 B 点才触发"。** 挂接寻路每 tick 都在跑，进入等待区前后一直在算：

```
:9723  CPL-PAIR tick=3019 act=21 tgt=24 dist=18 actTile=248,233 tgtTile=265,234
:9779  CPL-PAIR tick=3026 act=21 tgt=24 dist=15 actTile=251,233 tgtTile=265,234
:9783  CPL-PAIR tick=3030 act=21 tgt=24 dist=13 actTile=253,233 tgtTile=265,234
:9785  CPL-ENTRY veh=21 tile=253,233 orderType=16 dontReserve=0
:9787  CPL-WAITCUT veh=21 state=0 wp=17 tile=253,233   ← 截断闸门没开
```

即：还没进路点格（253,233 起的 5 格之外）就已经一次一次在重算终点，并且每次都算出 278,234。

**(2) 不是"YAPF 按普通前往车站设终点"。** 耦合寻路里 `dest_tile` 只作**启发式距离**，不参与接受：

```353:364:src/pathfinder/yapf/yapf_destrail.hpp
	TileIndex dest_tile; ///< heuristic destination (the order's station/depot).
	void SetDestination(const Train *v)
	{
		this->dest_order.AssignOrder(v->current_order);
		this->CYapfDestinationRailBase::SetDestination(v);
		this->dest_tile = (v->dest_tile == INVALID_TILE) ? TileIndex{} : v->dest_tile;
	}
```

真正终点由 `PfDetectDestination(tile, td)` 逐格判定（`yapf_destrail.hpp:446-658`），接受分支只有 `kind=car-only`（:630）与 `kind=waiting`（:647）两处。

**(3) 真正的错点：判"这格上有车底"靠的是预留归属，不是车底真的在这格。**

```498:543:src/pathfinder/yapf/yapf_destrail.hpp
		Train *t = nullptr;
		if (has_res) {
			t = GetTrainForReservation(tile, TrackdirToTrack(td));
			...
		}
		if (t == nullptr && IsRailStationTile(tile)) {
			/* 站台轴向后扫，靠预留位或车辆所在格找车底 */
			...
		}
		if (t == nullptr) { ++_r3r_cpl_scan_noconsist; return false; }
		++_r3r_cpl_scan_consist;
		t = t->First();
```

之后三道闸门（:606 `R3RCoupleAllowed`、:612 `TrainFitStation`、:623 `R3RIsCoupleTarget`）**全部只判 t 的属性**，没有任何一条校验"t 的任一车节是否真的在 tile 上"。

⇒ 于是**整条站台的预留**让每一个站台格（含车底并未占据的空格）都满足"这格有 t 的预留"，都能被 accept 为会合点。

现场支撑：

```
:9788-9790  PFD-ACCEPT kind=waiting tile=278,234 t=24 ordType=17 wc=1   （×3，同一格）
:9792       CPL-SCAN ... noconsist=6 consist=3 last=278,234 lastveh=24 why=acc-wait
:9793       CPL-HEAD best=278,234 head=265,234 on=0     ← 真实车头在 265,234
:9794       CPL-BEST veh=21 tile=278,234 td=8 cost=16442
:9795-9798  CPL-BACK 253,233 → 254,233 → 258,233 → 259,234 → 278,234
```

3 次 accept 全落在 278,234，264..277 一格都没有记录 —— 它们不是"没有预留"（玩家已确认 265..277 全归 24 号），而是**跟随器一步跳过整段站台，265..277 从未成为 YAPF 节点**（见 7.3）。

**结论**：终点不是"提前写死、条件追上"，而是**每 tick 重新判定，判定用"整站台预留"当"车底在场"的证据，再叠加跟随器跨段落点恒为站台末端那一格**。玩家直觉（终点在条件满足前就已锁定到某格）在结果上成立，但成因是「预留语义 ≠ 车底在场」，不是"提前规划了一条普通车站路线"。

### 7.3 REG-445-C 结案：残留假说**证伪**（2026-10-23 玩家确认）

玩家原话：「关于认错终点，（278，234）的预留就是被动方发出的预留，而且不应该算是残留，因为这是被动方发出的占用整条站台的预留，而（265-277，234）上面的预留也属于被动方。」

⇒ 278,234 不是孤儿/陈旧残留，是 `ReserveTrackUnderConsist()` 的**整站台预留**覆盖到了车底没占住的那一格。上一轮"陈旧预留"的猜测作废，`:9154 CRT veh=24 origin=278,234` 只是同一整站台预留的另一条读数，不构成残留证据。

**真正的错位在"落点选择"这一层，不在"预留归属"这一层**：

```text
253,233 → 259,234 ──[进入站台段，TrackFollower 一步跳过 length-1 格]──> 278,234
                                                                        ↑ 唯一的节点
265..277 = 车底本体所在格，从未成为节点
```

`CPL-BACK pPrev=278,234 parent=259,234` 就是这条跨段边（跨 19 格）。于是一路走通：

1. 节点 278,234 上有 24 号的整站台预留 ⇒ `GetTrainForReservation()` 返回 24；
2. 三道闸门（`R3RCoupleAllowed` / `TrainFitStation` / `R3RIsCoupleTarget`）全过，因为它们只判 24 的属性；
3. accept 成终点 ⇒ `best=278,234`，而车头本体在 265,234（`on=0`）。

⇒ 正确落点应当是**车底本体所在的那一格**（按贴合方向取车底端节所在的格，本例即 265,234 一侧），不是跟随器跳段后的站台末端格。

### 7.3.1 修法落点（REG-445-D，待实现）

两条必须同时改，缺一仍错：

- **判据加"本体在场"**：`PfDetectDestination()` 在 `has_res` 命中后，追加校验"t 这条链上至少有一节真的在这格"（比对 `VehiclesOnTile<VehicleType::Train>(tile)` 里是否有 t 的链成员），纯整站台预留覆盖到的空格一律返回 false。
- **落点归一化**：节点是被跳段产生的时候，`best` 必须回落到"被跳过那段里第一个有车底本体的格"，而不是节点自身。可直接复用第 93 轮为 `reach()`/`rp()` 写的 `R3RSkippedStationStretchContains()`（`src\pathfinder\yapf\yapf_rail.cpp`），在 couple 寻路的 best 提交处补一次同样的"跳段内包含目标"校正。

副作用检查：预留与 `CPL-BACK` 回溯都必须止于归一化后的格，否则列车仍会把整条站台订下去（KI-346 家族）。

### 7.3.2 玩家追问「寻路都算好站台了，直接开进去不就行了？」（2026-10-23 定性）

答：**寻路解决的只是"到得了"，不解决"到得了就能挂上"**。两件事归两套判据管，终点格选错时前者照样成功、后者必然失败。

三层拆解：

1. **它不是在找站台，是在找"车底所在的那一格"**。站台是那条站台没错（它没走错站台），错的是站台东/西哪一头。它认定车底在 278，所以认为"开到 278 就正好贴上去"。
2. **站台不是点，是一条十几格的线**。停哪一格是致命差异：差 13 格 ⇒ 命中判定（鼻尖↔车底尾距离贴合）永远不成立 ⇒ 到了 E 也挂不上。
3. **要去东头必须碾过西头**。车底就静止停在 265，主动方开往 278 必须穿过 265 那一格：碰撞检测/预留/Stuck 全在这里炸。这就是玩家看到的一切"幺蛾子"（幽灵预留、站台被整条订走、`mstuck=1`、原地转圈重试、`COUPLE-FAIL` 刷屏）的共同出口。

⇒ 所以"直接进那个站台"在物理上是办得到的，办不到的是"停对位置"。修法必须让终点落到本体格（§7.3.1 两条），不能靠"进站后自行找车"。

### 7.3.3 玩家追问「从东头进、沿站台一直走，总能撞上被动方吧？」（2026-10-23 定性）

答：物理上确实会撞（引擎不穿模），但**「撞上」在 PBS 铁路里不是自然事件，是必须被逐格批准的行为**，而现在没有任何一条批准它往西走。

三道关，任一道都足以让「一路走过去撞上」不发生：

1. **它自己认为「到了」**。终点格 278 一旦到达，`CRT found=1` 成立、速度归零、订单收尾。列车没有理由再往西多走一格——它不是盲车，是按目的地行驶的。
2. **往西的每一格都要重新 Bid 预留**。265 上真的压着车底的车身，引擎防撞机制天生否掉「进入有车的格」；唯一的合法例外是 R3R 给等待车底开的门（`R3RCoupleAllowed` / `R3RIsCoupleTarget`），而该门只在**寻路目标本身就是那节车底**时才生效。终点不是它，门不开。
3. **就算硬挤**，没有预留就前进 ⇒ `TPR-BLOCKED` / `mstuck=1` / 原地打转，这正是现场反复出现的形态，而不是「冲上去撞上」。

⇒ 所以正常挂车恰恰就是「撞上」：终点必须落在车底本体格（或紧邻位），让列车**有理由一路开过去贴住**，`CheckTrainCollision` 命中后由引擎完成拼接。终点定在东头，这条唯一可行的物理通路被切断，剩下的全是等待与重试。

一句话：**撞车要买票，票上写的终点是东头，它就只能停在东头。**

### 7.3.4 撤回 §7.3.3 的「它开到东头就停了」（2026-10-23，玩家质疑后实测复核）

玩家追问（原话要点）：「列车在站台东边，寻路到了站台最西侧，而等待方停在站台最东侧；主动方从最东侧进入，甚至不用进入站台就能和被动方发生碰撞，这怎么把锅甩到寻路上面去。」

**§7.3.3 第 1 条（「到达 278 即停、没有理由再走」）作废，撤回。**实测证据：

```text
build\R3R_debug.log:9861  CPL-PAIR tick=3032 act=21 tgt=24 dist=12 actTile=254,233
build\R3R_debug.log:9905  CPL-PAIR tick=3034 act=21 tgt=24 dist=11 actTile=255,233
build\R3R_debug.log:9907  CPL-PAIR tick=3036 act=21 tgt=24 dist=10 actTile=256,233
build\R3R_debug.log:9911  CPL-PAIR tick=3039 act=21 tgt=24 dist=9  actTile=257,233
build\R3R_debug.log:9912  LOCO-MOVE veh=21 tile=257,233 j=212 spd=30 track=01 dir=1 db=1 stuck=0
（日志止于此，共 9912 行）
```

⇒ 主动方从未到达 278，也**从未进入站台**：它正以 `spd=30` 正常行驶，最后读数距车底还有 9 格。此前"到了东头（278）就收工停下"的叙述是错的，不能继续用。

**几何订正（按坐标，不按东西口语）**：站台是 x 递增方向（东西向，东= x 增大）。

| 对象 | 格 | 位置 |
| --- | --- | --- |
| 主动方起点/当前 | 248,233 → 257,233 | 站台**外侧西端**，向东驶来 |
| 站台入口 | 258,233 → 259,234 | 拐进站台的第一格 |
| 车底本体 veh=24 | 265,234 | **入口内侧约 6 格**，挡在途中 |
| 寻路终点 best | 278,234 | 站台**东端**（`CPL-HEAD on=0`） |

`CPL-BACK 253,233 → 254,233 → 258,233 → 259,234 → 278,234` 这条跨段边（`pPrev=278,234 parent=259,234`）意味着列车要**物理穿过 265,234** 才能抵达 278,234。所以玩家的推论是对的：**入口不远处就撞上车底，根本轮不到"停在站台另一端"这个错误发生。**

**锅的重新划分（两半，各占一半）**：

1. **落点这一半的责任不是"停错位置"，而是下面两条**：
   - **动机**：终点 278 要求它越过 265 继续东行 13 格。一旦在 265 命中失败并回滚，它会继续加速向前，把车底甩到身后，之后再也回不来；终点若为 265，到达即减速停住，失败也能原地重试。
   - **预留**：要开往 278 就必须订 259..278 整条站台，而 265..277 加 278 全在 veh=24 的**整站台预留**里（`:9788 PFD-ACCEPT tile=278,234`＝该整站台预留的末端格）。两套整站台预留正面冲突 ⇒ 这就是它到 258,233 之后只有 `CPL-RESERVE`、`RESV-REL who=CLEAR-STN`、反复重订而推进不了的原因，也是"幽灵预留/蠕动"的来源。
   - 终点若落在 265，它只需订到 265，**不会去抢车底整站台预留的另一端**。
2. **命中/拼接这一半（本轮新事实）**：`:9912 LOCO-MOVE … db=1` —— 主动方是**倒车逼近**（DrivingBackwards=1）。倒车时"鼻尖"在链尾，命中判据（鼻尖↔车底尾）与拼接判据（`v->Last()`↔`u->First()`）天然是两对端点 ⇒ 与第 232 轮 `SPLICE-GAP-REJECT`、KI-340「命中端与拼接端角色错位」同族。**即使开到 265 贴住，也未必拼得上。**

**新疑点 REG-445-E（未取证，禁止下结论）**：日志止于 `:9912`，全日志无 `CPL-HIT`、无 `COUPLE-OK`、无 `COUPLE-FAIL`。因此「开到 265 到底挂不挂得上」目前**没有数据**；`db=1` 是否会让它从车底旁擦过去，需下一份完整日志（跑到 265 附近并出现命中判定）才能定性。

### 7.4 刹车正确形态（KI-445 修法订正）

玩家 2026-10-23 确认：「如果按照正常刹车，列车是可以对着西侧停车标停下的，因为场景里的列车是刹车性能很好的磁浮类列车。」⇒ **主修路径（交还引擎原生制动）可行，不必保留任何硬写**。

现状两处硬写（`src\train_cmd.cpp:16009-16011`、`:21166-21171`）都发生在**车头已经进入路点格之后**——`R3RShouldHoldForCouple()` 的前置就是

```13148:13154:src/train_cmd.cpp
	const Vehicle *front = v->GetMovingFront();
	const TileIndex head_tile = (front != nullptr) ? front->tile : v->tile;
	if (!IsRailWaypointTile(head_tile)) return false;
```

⇒ 即使只删掉硬写，也只剩"格内那十几像素"的减速距离，仍会冲过停车位。所以修法必须是两层：

- **主修**：等待区路点作为一次正常的 `OT_GOTO_WAYPOINT` 停靠，交给引擎原生减速与停靠位对齐（`Train::GetCurrentMaxSpeed()` 的提前制动 + `TrainEnterStation` 的停车位计算），列车自然停在路点格西侧停车标，全程有 `SPDD` 斜率。
- **不做兜底**（玩家 2026-10-23 否决）：「关于第一点，我认为强制写 0 真的没有必要了，不必再加一个写 0 的兜底，原生刹车机制已经很好了。」⇒ 连 `strict_max_speed = 0` 这一层也不保留，等待区路点不施加任何速度干预，减速与停靠位全由引擎原生制动负责。

### 7.4.1 原话与口径（2026-10-23）

- 玩家：「关于第一点，我认为强制写 0 真的没有必要了，不必再加一个写 0 的兜底，原生刹车机制已经很好了。」
- 定案：KI-445 的修法 = **只做删除，不做替代**。`cur_speed = 0` / `subspeed = 0` 两处硬写删掉，`strict_max_speed = advisory_max_speed = 0` 也一并删掉；`R3RShouldHoldForCouple()` 若仍需保留，只保留"逻辑上判定是否该在此等待"的布尔语义，不再碰限速与速度字段。
- 附带要求：删完之后若列车仍停在格东边缘，那就是"停靠位计算"的问题（`TrainEnterStation` 路线），不许再回退成写 0。

### 7.5 复测判据补充

6. 从东驶来进入东西向路点时，车头停在路点格**西侧停车位**，鼻尖不再停在格东边缘。
7. 全程 `SPDD` 单调递减到 0，日志中不再出现 `ctx=waitcut` / `ctx=hold-for-couple` 的硬归零行。
8. `PFD-ACCEPT` 的 `tile` 必须等于车底实际所在格（或车底将停靠的站台末端格），不得等于任何陈旧预留格；负判据 = `278,234` 不再被 accept。

### 7.6 玩家新报告：预留铺对了，列车到岔口却不按预留走（2026-10-23，数字实锤）

玩家原话：「主动方的黑色预留路径层确实铺往了正确的地方，但是主动方走到一个岔口直接不按照预留走，往别的站台以 30km/h 的速度走了！」

#### 实锤 1：**行驶目标与预留目标差 6 格 y ＝ 整整一条平行站台**

```text
build\R3R_debug.log:9722  DEPOT-ARR veh=21 spd=0 real=12(16) ... tx=248 ty=233
                          destTx=278 destTy=240 destDepot=0 destResv=0 tileEqDest=0
build\R3R_debug.log:9720  COUPLE-WAIT-OFFDEST veh=21 tile=248,233 mf=254,233 real=12
                          isDepot=0 destDepot=0 destStn=12 destTile=491798
build\R3R_debug.log:9689~9912  所有 LOCO-MOVE 行 dtile=491798 恒定
```

`491798 = 240 × 2048 + 278` ⇒ **(278,240)**，与 `destTx=278 destTy=240` 逐位吻合（地图宽 2048 已由该行独立佐证）。

而挂车寻路给出的目标是另一格：

```text
build\R3R_debug.log:9793  CPL-HEAD best=278,234 head=265,234 on=0
build\R3R_debug.log:9794  CPL-BEST veh=21 tile=278,234 td=8 cost=16442
build\R3R_debug.log:9795  CPL-BACK pPrev=278,234 td=8 parent=259,234
build\R3R_debug.log:9796  CPL-BACK pPrev=259,234 td=4 parent=258,233
```

⇒ **同一时刻，车按 (278,240) 行驶，预留铺向 (278,234)，y 差 6。** 玩家说的“走错站台”是字面事实：不是站台内的端头错，是**隔了一条平行站台**。

#### 实锤 2：预留被自己的清理代码当场清空

```text
9690  RESV-WATCH CLEAR-RAIL tile=248,233 track=0 phase=choose-track actor=21 head=21
9691  RESV-GC veh=21 tile=248,233 track=0x0 why=free-path
9692  RESV-GC veh=21 tile=249,233 track=0x0 why=free-path
 ...  （248,233 → 258,233 共 10 格）
9697  RESV-GC veh=21 tile=254,233 track=stn kind=WAYPOINT why=free-path-hold
9702  RESV-LEDGER-FORGET veh=21 why=free-path mask=0x1 released=10 kept=1 phase=choose-track
```

落点：`src\train_cmd.cpp:14557` 的 `R3RResvLedgerForget(consist->index, "free-path");` 位于 `FreeTrainTrackReservation()` 入口，**无条件执行**，随后从 `moving_tile` 起逐格 `UnreserveRailTrack`。⇒ 预留不是“画错了”，是**画好之后被自己的撤预留路径销毁**。

#### 实锤 3：不达速是老毛病（第 271 轮已登记），本轮 spd 复现

源码注释 `src\train_cmd.cpp:15943-15965` 已写下几乎相同的玩家原话：

> 「列车明明已经向某处铺设了预留，但是走过去之前又被神秘销毁，导致列车无法达速运行」
> 「铺 85~95 格 → 走几格 → 清空 → 再铺」的循环，速度永远上不去。

本轮读数复现同一形态：`:9689 spd=2 → :9743 spd=37 → :9768 spd=15 → :9780/9782/9784 spd=30`（反复起落，卡在 30 附近不动 ⇒ 玩家眼里的“以 30km/h 走”）。

#### 机制（猜测根因，待验证）

- **G-445-F**：PBS 的方向选择只受“当前格 + 前方已预留位”约束。预留一旦被 `FreeTrainTrackReservation` 清空，列车就按订单 `dest_tile=(278,240)` 自由寻路，CPL 铺的 (278,234) 方向完全失效 ⇒ 岔口拐向 y=240 那条站台。**这解释了“预留铺对了却不按预留走”。**
- **G-445-G**：`dest_tile=(278,240)` 本身是错的（应为车底实际所在站台 (278,234)，理想是车底格 (265,234)）。⇒ 预留（CPL 寻路，取物理车底位置）与行驶（订单 dest）是**两套互不相干的目标**，这才是“走错站台”的根源。

#### 已登记疑点

- **REG-445-F**：`RESV-GC why=free-path` 在 `phase=choose-track` 阶段无条件清账本+实体预留，第 271 轮为“车还在动不许拆预留”加的守卫（`:15957` / `:15959`）为何没覆盖本次这条调用路径 —— 未取证。
#### 玩家答复（2026-10-23，决定性）

- 玩家原话：「`278,240` 和 `278,234` 是同一个车站的两个平台(站台)。第二个问题我不知道」
- **定案 1（REG-445-G 结案）**：`278,240` 与 `278,234` **同属车站 12，是两个不同平台** ⇒ **车站选对了，平台选错了**。这不是“跑到邻居车站”的严重事故，而是“同一车站内订到空平台、实际该去车底所在平台”。性质=平台级错配，修法只需换平台，不必换车站。
- **定案 2**：`dest_tile` 的写入方玩家不知情，由 AI 自行取证（见下）。

⇒ 由此修正 G-445-G 的表述：错的是**平台选择**（订单 dest_tile 落到 y=240 的平台），而 CPL 挂车寻路按车底物理位置算到 y=234 的平台。两套平台选择器各自为政。

#### 已登记疑点

- **REG-445-F**：`RESV-GC why=free-path` 在 `phase=choose-track` 阶段无条件清账本+实体预留，第 271 轮为“车还在动不许拆预留”加的守卫（`:15957` / `:15959`）为何没覆盖本次这条调用路径 —— 未取证。
- **REG-445-G（已结案 2026-10-23）**：同站不同平台。原疑问句留档：`destStn=12` 与 (278,234) 所在车站是否同一个车站 —— 已答：同站；平台不同。

#### 下一步取数（两条）

1. real=12 这条 GOTO_COUPLE 的 `dest_tile` 由谁写入（挂车配对时是否本应改写成车底所在平台格）。**玩家答复：不知情 ⇒ AI 取证中**。
2. `:9690` 前一行的 choose-track 调用点，确认 `FreeTrainTrackReservation` 的触发条件。

#### 修法方向（待拍板，按平台级错配重写）

- **甲（治方向）**：GOTO_COUPLE 的 `dest_tile` 不要用“车站默认平台”，要钉到**配对车底所在平台**（理想是车底格 `265,234`，退一步是车底平台末端 `278,234`）。即使预留被清，列车也朝着车底那条站台走。直治“走错站台”。
- **乙（治速度/蠕动）**：`choose-track` 阶段不清自己刚铺的 `rp()` 预留（或清完立即重铺）。直治“不达速/蠕动”。
- 建议甲先：方向错比速度慢严重。

### 7.7 根因定案（2026-10-23）：`dest_tile` 恒等于车站锚点 `st->xy`，与车底在哪条平台无关

玩家答复「278,240 和 278,234 是同一个车站的两个平台」+「dest_tile 由谁写入我不知道」⇒ 已由 AI 溯源定位。

#### 三层调用链（逐层带行号）

```text
src\train_cmd.cpp:15186-15194   VehicleOrderSaver::SwitchToNextOrder()
        case OT_GOTO_COUPLE:
            this->v->current_order = *order;
            return UpdateOrderDest(this->v, order, 0, true);

src\order_cmd.cpp:4542-4550     UpdateOrderDest()
        case OT_GOTO_COUPLE:
            if (order->GetCoupleIsDepot()) { v->SetDestTile(Depot::Get(depot_id)->xy); }
            else { v->SetDestTile(v->GetOrderStationLocation(order->GetDestination().ToStationID())); }
            return true;

src\train_cmd.cpp:16799-16811  Train::GetOrderStationLocation()
        const Station *st = Station::Get(station);
        if (!st->facilities.Test(StationFacility::Train)) { this->IncrementRealOrderIndex(); return {}; }
        return st->xy;          // ← 16810，恒为车站中心格，不查平台、不查车底
```

⇒ **`dest_tile` 恒等于车站 12 的中心格 `(278,240)`**，与车底实际停机平台 `(278,234)` 毫无关系。

#### 两套目标对照（这就是“预留铺对、车走错”的全部原因）

| 目标 | 代码路径 | 落到哪 |
| --- | --- | --- |
| 实际行驶（PBS dest） | `order_cmd.cpp:4548` → `train_cmd.cpp:16810` `st->xy` | `(278,240)` 空平台 |
| 挂车寻路（CPL / rp） | `yapf_destrail` 按车底 `tile` 算 | `(278,234)` 车底平台 |

⇒ 不是随机走偏，不是预留被清才走偏；是**两个不同函数各自选平台**的必然结果。`FreeTrainTrackReservation` 清预留的作用只是“让错误失去纠正机会”（这一层归 G-445-F）。

#### 修法（定案，待实现）

只改 `UpdateOrderDest()` 的 `OT_GOTO_COUPLE` 非库分支（`order_cmd.cpp:4548`），把“车站锚点格”换成“配对车底所在平台格”，优先级：

1. 车底所在格 `tgt->tile`（本案 `265,234`，与 CPL-HEAD head=265,234 同源）；
2. 兜底：车底所在站台外端格（本案 `278,234`，等于 CPL-BEST）；
3. 再兜底：`st->xy`（现行为，保持向后兼容）。

约束与风险：

- **`OT_GOTO_STATION` 绝不能这么改**：常规定班车进站靠 `st->xy` + 站内 PBS 挑空平台，改它会破坏原生平台选择。
- **库分支（`GetCoupleIsDepot()`）不动**：`depot->xy` 是唯一正确解。
- 车底配对表来源已有：`TrainCoupleHandler` 的 CPL-PAIR（act/tgt）与 `R3RIsCoupleTarget()` 判定；需确认在 `UpdateOrderDest` 调用时刻（可能早于寻路）能拿到 tgt。
- 车底可能在寻路前离开平台 ⇒ 必须每 tick/每次 `UpdateOrderDest` 重算，不得缓存写死。

#### 复测判据（4 条）

1. 同场景 `DEPOT-ARR` / `LOCO-MOVE dtile=` 应从 `(278,240)` 变成 `(278,234)` 或 `(265,234)`；
2. `CPL-BEST` 与 `dtile` **一致**（这是本轮的核心目标：两套目标合流）；
3. 列车在 `259,234` 岔口拐向车底那条平台，`COUPLE-OK` 不再以“走错站台后乱跑”收场；
4. 负判据：普通 `GOTO_STATION` 定班列车进站平台选择逐字不变、多平台站仍能自动挑空平台。

### 7.8 为什么这个缺陷长期没暴露（2026-10-23 玩家追问）

#### 一句话

`dest_tile` 平台错配从 `OT_GOTO_COUPLE` 落地那天起就存在，但它**长期被挂车预留掩盖着**：预留活着时列车跟着预留走，`dest_tile` 写错也不影响方向。只有“预留刚被清掉 + 车刚好在站外岔口前”这个窗口里，L1 才单独生效。**两个缺陷互相遮蔽，任一个单独存在都不表现为“走错平台”。**

#### 分层

| 层 | 内容 | 何时产生 | 何时可见 |
| --- | --- | --- | --- |
| L1 | `dest_tile = st->xy`（车站锚点平台），与车底所在平台无关 | 从 GOTO_COUPLE 存在起恒成立 | 仅在没有其他方向约束时生效 |
| L2 | `FreeTrainTrackReservation()` 在 `phase=choose-track` 清掉刚铺的 10 格预留 | 取决于调用路径（REG-445-F） | 清空那一瞬 |

#### 五个必要条件（缺一个就成不了这个症状）

1. 目标车站 ≥2 平台 —— 单平台站 `st->xy` 必与车底同平台，L1 不可能显形。
2. 车底停在**非锚点平台** —— `st->xy` 只锚定一条平台；车底若恰停在锚点那条，`dest_tile` 恰好是对的。
3. 岔口在**站外**且早于 `dest` 到达 —— 若分岔发生在站内，原生“进站挑空平台”逻辑可能接管并把车送到车底平台（掩盖）。
4. 挂车寻路（CPL）已经铺出预留 —— 预留活着时列车跟预留走，`dest_tile` 写错也无影响（**主掩盖器**）。
5. 预留紧接着被清 —— 唯一让 L1 单独生效的事件（`:14557` 入口无条件执行）。

⇒ 条件 4 就是“沉默期”的成因：只要 CPL 预留存活到命中，全程没有任何代码用 `dest_tile` 做方向决策。

#### 为什么“以前”更容易落在沉默期

- 旧测试场景多为单平台站，或车底就停在主平台格（条件 1、2 不成立）；
- 早期挂车多在站台缓冲区内直接命中，预留虽短但覆盖全程（条件 4 成立、条件 5 不成立）；
- 第 271 轮为“车还在动不许拆预留”加的守卫（`:15957`/`:15959`）若曾覆盖本条路径，则条件 5 长期不成立 ⇒ **这属 REG-445-F，仍未取证，不作为本轮结论**。

#### 为什么没被当成 bug 记录

走错平台后列车仍在本站范围内：PBS 会重新寻路，部分几何下能经站内道岔/换端绕回车底平台并挂上，表现为“绕了一下才挂上”，被当成正常寻路抖动。只有像本次这样**两条平行平台 + 岔口紧邻 + 预留被拆干净**，才以“明显走错平台 + 30km/h”的形态暴露出来。

#### 可验证判据（证明“以前也有，只是被掩盖”）

1. 用旧快照 exe（`build\r3r_stable_2026-09-29\openttd.exe` 等）跑同存档同站：若 `DEPOT-ARR` 的 `destTx/destTy` 仍为 `(278,240)` ⇒ L1 一直存在，与版本无关。
2. 历史日志中搜 `COUPLE-WAIT-OFFDEST`，比对同车号同帧的 `CPL-BEST tile`：若历史上就出现过两者不一致、而列车仍成功挂车 ⇒ 正是“预留掩盖 dest”的直接证据。
3. 历史日志中找“进错平台后又绕回”的形态（`LOCO-MOVE` 的 tile 序列在站内折返）⇒ 症状曾存在但未被记录。

#### 对修法顺序的含义

L1 应独立修，不必等 L2：修 L1 后即使预留被清，列车仍朝车底平台走。反之只修 L2（不清预留）会让 L1 继续被掩盖，问题会以“某天又在另一个岔口冒出来”的形式复现。L2 只影响速度，而 30km/h 蠕动另有 KI-445 一份来源。

## 八、修复落地（2026-10-23 第四轮，玩家口径「按推荐的来修 + 别忘了速度突然置零」）

### 8.1 KI-446（L1）：`OT_GOTO_COUPLE` 的 `dest_tile` 改取配对车底所在格

- 落点：`src\order_cmd.cpp` 的 `UpdateOrderDest()` / `case OT_GOTO_COUPLE:` 非库分支。
- 原来：`v->SetDestTile(v->GetOrderStationLocation(...))` ⇒ `Train::GetOrderStationLocation()` 末行 `return st->xy;` ⇒ 车站**锚点格**，与车底平台无关。
- 现在：新增文件内 `static TileIndex R3RGetCoupleOrderDestination(const Vehicle *v)`：
  1. 非 Train ⇒ `INVALID_TILE`；
  2. `R3RGetCouplePairPartner(Train::From(v))` 取配对车底，为空 ⇒ `INVALID_TILE`；
  3. 车底 `tile` 必须是 `IsRailStationTile` ⇒ 返回该格，否则 `INVALID_TILE`。
  调用点写成 `couple_dest != INVALID_TILE ? couple_dest : station_dest`，取不到时**逐字保留旧行为**（车站锚点），因此“车底不在站台/尚无配对”的路径不受影响。
- 为什么取“车底当前格”而不是“平台末端”：现场 `:9793 CPL-HEAD best=278,234 head=265,234 on=0` 已经证明落点被 `TrackFollower` 的站台跳段推到末端格；把 sed 也定到 `278,234` 只会复制同一个错误。目标应是车底本体所在格 `265,234`，让 PBS 目的地和“车底真在哪”一致。
- 已知残留风险（须实测）：把 PBS 目标设为**格上有车**的 tile，`PfDetectDestination()` 侧可能判“终点被占用”。若实测表现为 `found=0` / 到不了，按 §7.3.1 补“`t` 的任一车节真在这格”校验（`VehiclesOnTile` 比对），而不是回退到 `st->xy`。

### 8.2 KI-445：等待区不再施加任何速度干预（只删不替）

按玩家两条原话（「原生刹车机制已经很好了」「不必再加一个写 0 的兜底」），**整段删除、不做替代**：

1. `src\train_cmd.cpp` 的 `DoTrainCouplePathfind()` 里 `!pair_ready || r3r_wait_cut != 0` 分支：
   删掉 `R3RSpdCtx(consist, "waitcut"/"pair-not-ready", …)` + `consist->cur_speed = 0; consist->subspeed = 0;`。
   保留同分支内的 `CPL-WAITHOLD` 只读探针，以及 KI-444 的 `state == 2` 早退（不撤预留、不打 `Stuck`）。
2. `TrainLocoHandler()` 的 `if (R3RShouldHoldForCouple(consist))` 门禁：
   删掉 `max_speed_info.strict_max_speed = 0; max_speed_info.advisory_max_speed = 0;` 与 `cur_speed/subspeed = 0`（连 `R3RSpdCtx(consist, "hold-for-couple", …)` 一并删）。
   该 `if` 里只剩 `CPL-HOLD` 心跳日志 ⇒ **速度完全交给原生制动**，列车按 PBS 路径止于等待路点自然减速停下。
3. 路由侧不变：`TryPathReserveWithResultFlags()` 的 C4-3 门禁（`CPL-HOLD-NOBOOK`）仍然拒绝在等待区继续订路，所以“删掉写 0 就会提前订路”的回归不会发生；`state == 2` 的 `keep_track` 早退也未动（KI-444 行为保持）。

### 8.3 预期效果

- `SPDD … ctx=waitcut` / `ctx=hold-for-couple` 的“非零→0 一帧跳变”消失，速度只经原生制动逐 tick 递减（`ctx=stale` 那条递减斜率成为唯一路径）。
- 不再出现同格同值 `spd=2` 的长期蠕动；停车位置由 `TrainEnterStation`/路径终点决定，允许停在格内任意位置。
- `CPL-HEAD … on=1` 成为主判据：`best == head` 即目的地与“车底真在哪”一致。

### 8.4 本轮构建状态

- 首次尝试 `_tmp_inc_build.cmd`（复用既有入口，未新建 `.cmd`）被护栏拦下：`build\R3R_incbuild.done = EXIT_CODE=97`（`openttd.exe` 正在运行，PID 29268，13:51:50 启动的玩家测试会话）。
- **对象级编译通过**（只调 `ninja` 编两个 `.obj`，不链接，不影响在跑会话）：`[2/3] order_cmd.cpp.obj`、`[3/3] train_cmd.cpp.obj`，日志 `build\R3R_objcheck.log`；`order_cmd.cpp 15:22:22 → obj 15:26:06`、`train_cmd.cpp 15:21:21 → obj 15:26:07`。
- **链接完成**（玩家 15:28 退出游戏后重跑 `_tmp_inc_build.cmd`）：`GUARD: incremental is safe` → `[2/2] Linking CXX executable openttd.exe` → `build\R3R_incbuild.done = EXIT_CODE=0`（15:31:54），错误计数 0，`build\openttd.exe @2026-10-23 15:31:39`（52 272 640 B）。
- 因此本轮状态：**已改码、已编译、已链接，待实机复测**；判据沿用 KI-445/KI-446 条目与上文 §8.3。

## 九、第五轮（2026-10-23 15:40 实机）：路点未停车 / 未对准停车标即重新加速（KI-448）

### 9.1 现场锚点（`build\R3R_debug.log`，615 行 / 101 623 B / 15:40:17，exe = 15:31:39 的 KI-445+KI-446 版）

主动方 `veh=21`：`db=1`（倒车链）、`order=6`(=`OT_GOTO_WAYPOINT`)、`real=11`、`dtile=479486` = **(254,234)**。

| 行 | 读数 | 含义 |
| --- | --- | --- |
| `:1` | `SPDD veh=21 from=243 to=244 … order=6 real=11 dtile=479486 tile=205,351 track=02` | 起步向北，速度开始累积 |
| `:520` | `from=105 to=104 … ctx=stale cage=93 tile=245,233 x=3933` | 已进入原生制动，每 tick 稳定 −1 |
| `:574` | `from=56 to=55 … ctx=stale cage=94 tile=248,233 x=3969` | 制动到 55 km/h，位置仍向路点方向推进 |
| `:576` | `SPDD veh=21 from=55 to=56 dt=0 ctx=stale … order=16 real=12 … dtile=491798` | **同一 tick：订单 6→16、real 11→12、dtile 换成 (278,240)，速度由降转升** |
| `:578` | `LOCO-MOVE veh=21 tile=248,233 j=223 spd=56 track=01 dir=1 db=1 dtile=491798` | 已进移动分支，朝新目的地加速 |
| `:580 / :582 / :586 / :604` | `56→57 / 57→58 / 59→60 / 67→68` | 一路加速到 68 |
| `:606 / :609 / :612 / :614` | `68→67 / 67→66 / 66→64 / 64→62` | 又转降（仍未停），`x` 只在 3970→3982，全程同一格 248,233 |
| `:598` | `CPL-SKIP veh=21 tile=248,233 orderType=16 dontReserve=0 retryIn=8 holdAge=0` | 挂车寻路在退避（retryIn=8），列车却仍在加速 |

`dtile` 解码（地图宽 2048）：`479486 = 234×2048 + 254` → (254,234)；`491798 = 240×2048 + 278` → (278,240)。
全日志**没有** `CPL-HEAD` / `CPL-WAITHOLD` / `CPL-HOLD` / `RESV-NOBOOK` / `TPR-*`，veh=21 的 CRT 仅 3 条：`:187 order=6 origin=237,233 td=8 found=1`、`:188 order=6 origin=253,233 td=8 found=1`、`:189 order=16 origin=205,290 td=8 found=0`。

### 9.2 疑点（已登记 REG-448-A/B/C，见 KI 条目）

- **REG-448-A（订单被谁推进）**：`order=6` 的目的地是 (254,234)，列车在 **6 格之外**的 (248,233)、速度 55 km/h 时 `real` 就从 11 跳到 12 ⇒ 路点订单在"没到格、没停车、没对准停车标"的情况下被判完成。日志里没有任何 `ORD-*` / `COUPLE-*` / `SKIP-*` 佐证推进者。
- **REG-448-B（`:189` 无法定位）**：`CRT veh=21 order=16 origin=205,290 found=0` 的 origin 与前后文（tile 205,35x → 248,23x）不连续，无法判断它属于本会话哪一时刻。
- **REG-448-C（KI-445 之后靠什么停）**：若"路点到达判定"本身发生在列车仍在行进时，则删掉写 0 后必然表现为"重新加速"；需要确认等待区是否还有任何"允许发车"的显式闸门。

### 9.3 猜测根因（待验证，先落盘）

- **G1（主假设）**：`OT_GOTO_WAYPOINT` 的到达判定在 R3R 等待区路径上不是按 waypoint 自身格判定，而是按某个派生位置（`dest_tile` / `GetOrderStationLocation` / 等待区入口格）判定 ⇒ 列车一进入等待区就判"到路点"，切到下一条 `OT_GOTO_COUPLE` 并继续加速。此前 KI-445 的写 0 掩盖了这一点（切单瞬间被清零），删除后暴露为"诡异加速"。
- **G2**：`r3r_wait_cut != 0` 语义若是"等待被截断⇒可发车"，则它被过早置位也会产生同样现象（原来那个分支顺便把速度写 0，所以表现为"停住"，现在表现为"加速"）。
- **G3**：`CPL-SKIP … retryIn=8` 表明挂车寻路处于退避期，而门禁（`R3RShouldHoldForCouple` / C4-3 `CPL-HOLD-NOBOOK`）只覆盖"停在等待区"的状态，不覆盖"刚切单但仍在 55 km/h 行进"的窗口。

### 9.4 待取数 / 待确认

1. `Train::ProcessOrders()` 里 `OT_GOTO_WAYPOINT` 的完成判据（是否用 `v->tile`/`veh->tile`/`dest_tile`/等待区派生格）。
2. `r3r_wait_cut` 的置位点与语义（本轮 KI-445 删掉的正是 `!pair_ready || r3r_wait_cut != 0` 分支）。
3. `R3RShouldHoldForCouple()` 的判据是否包含"正在行进"（`cur_speed != 0`）。
4. 理想行为：列车应**先停稳并对准停车标**，再切单/发车；判据需写成"切单那一 tick 的 `cur_speed == 0` 且位置在停车标容差内"。

### 9.5 根因做实（2026-10-23，已改码前取证）

**结论：G1 部分成立（不是"派生位置"，而是"格首 vs 格末"两把尺子）；G2 证伪；G3 成立（切单窗口内没有任何减速手段）。**

**证据 1 —— 到达判据取「车头进入路点格的首像素」，不是停车点。**
`src\train_cmd.cpp` 的 `Train::ProcessOrders()` 内 `case OT_GOTO_WAYPOINT`：
```cpp
if (IsRailWaypointTile(moving_front->tile) && GetStationIndex(moving_front->tile) == consist->current_order.GetDestination()) ProcessOrders(consist);
```
日志逐格对齐：`:566 x=3967` 仍在 247,233 格末（`x=248*16-1`），`order=6`；`:568 x=3968` = **248,233 的首像素**（`248*16=3968`），该 tick 仍是 `order=6 spd 59→58`；`:574 x=3969 spd 56→55`；`:576 x=3970` 已变成 `order=16 real=12`，`spd 55→56`。⇒ 切单点在路点格**起点后 2~3 像素**，速度 55 km/h。

**证据 2 —— 停车点算出来是路点格的「末端」，两者差近一整格。**
`PredictStationStoppingLocation()`（`train_cmd.cpp:957`）：对 `order->IsType(OT_GOTO_WAYPOINT) && order->GetDestination() == dest` 直接 `osl = OrderStopLocation::FarEnd`（:968-969），且 `r3r_head_stop` 只用于**阻止**把 `osl` 改回 `Middle`（:1025 `overhang >= 0 && !r3r_head_stop`）⇒ `stop = station_length`（路点格 = `TILE_SIZE` = 16 单位）⇒ 车头目标位置在 `x≈3983`（格末）。**刹车目标比判到位置远 14 像素 ≈ 0.9 格**，所以"理论能刚好刹停"是对的，判据却提前 0.9 格触发。

**证据 3 —— 切单之后，那个 0 速目标立刻消失（这是"诡异加速"的直接来源）。**
同上 `r3r_head_stop` 的取值：`R3RWaypointOrderStopsForCouple(v, order) && order->GetDestination() == st`；而该函数对 `order != v->current_order` 的分支只在 **current_order 是 OT_GOTO_WAYPOINT** 时才调 `R3RTrainStopsAtWaypoint(v)`：
```cpp
static bool R3RWaypointOrderStopsForCouple(const Train *v, const Order *order) {
    if (!v->current_order.IsType(OT_GOTO_WAYPOINT)) return false;   // ← 切单后恒 false
    ...
}
```
⇒ 下一 tick `ChooseTrainTrack` 的前瞻（`train_cmd.cpp:1175-1182` 的 `limit_advisory_speed(item.start + PredictStationStoppingLocation(...), 0, ...)`）不再对路点格施加 0 速，`advisory_max_speed` 回到线路限速 ⇒ 55→68。

**证据 4 —— `r3r_wait_cut` 此刻 = 2，但它只"不预留"，不减速。**
`R3RShouldCutCoupleAtWaitPoint()`：prev real order 是 wait waypoint、`front_on_wp == true`、`pair_ready == false` ⇒ 返回 **2**（不是 0）。该值在 `train_cmd.cpp:16020` 的分支里只做 `return { keep_track, result_flags }`（原地等一下次重算、不预留）；KI-445 删掉的正是同一分支里的 `consist->cur_speed = 0`（详见 §8.4）⇒ 该分支现在**与"全速加速"完全兼容**。所以 G2 的"r3r_wait_cut 语义=可发车"不成立，它是"原地等"，只是没人负责把车停下来。

**证据 5 —— 门禁只覆盖"停稳"，不覆盖"55 km/h 仍在路点格内"。**
`CPL-SKIP … orderType=16 dontReserve=0 retryIn=8 holdAge=0` 说明挂车搜索在节流退避；`R3RShouldHoldForCouple()` 里 `R3RGetCouplePairPartner(v) == nullptr ⇒ return true`（"该停"），但它的返回值只被 `TrainCoupleHandler` 用来决定"要不要尝试挂车"，**不产生任何速度/预留约束**。⇒ G3 成立。

**根因一句话**：等待区路点的「判到位置」（格首）与「停车位置」（格末）不是同一个点，切单后原生刹车因为 `current_order` 不再是 `OT_GOTO_WAYPOINT` 而失去唯一的 0 速目标，于是列车在路点格内从 55 km/h 重新加速。

**修法（待拍板，未改码）**
- **A（推荐，治根）**：把「等待区路点必须在格末 0 速停住」这条目标与 `current_order` 解耦 —— 即让 `PredictStationStoppingLocation()` 与 `:1175` 的 `r3r_head_stop` 判据额外覆盖「`order` 是 `OT_GOTO_COUPLE` + prev real order 是 wait waypoint + `dest == st` + 车头尚未越过该路点格」⇒ 仍然 `osl = FarEnd`、仍然 `limit 0`。这样原生刹车继续踩到底、停在停车标；停稳后由 `r3r_wait_cut == 2` 原地等；一旦越过路点格立刻不再限制（**无死锁**，不需要写 0）。
- **B（不推荐）**：给 `ProcessOrders()` 的 waypoint 完成判据加 `cur_speed == 0` 门控。风险：刹车略过格就永不切单，需要额外 overshoot 兜底。
- **C（与 KI-445 裁定冲突）**：回到 `r3r_wait_cut == 2` 时写 `cur_speed = 0`。

**复测判据（方案 A）**
1. 同一场景：切单（`order 6→16`）那一行 `spd` 必须 ≤ 2（理想为 0），且不再出现"切单后连续加速"；
2. 切单后仍能看到原生制动（`SPDD … ctx=stale` 逐 tick −1）直到 0；
3. 车头最终位置与停车标误差 ≤ 1 像素，且仍在路点格内（`tile` 不变）；
4. 停稳后 `CPL-WAITHOLD`/`CPL-HEAD` 类行出现，配对就绪（`pair_ready`）后正常发车，`CPL-SKIP retryIn` 不再伴随加速；
5. 负判据：越过路点格（判据不再成立）的旧场景不得卡死，仍能直接去耦合。

### 9.6 方案 A 已落地（2026-10-23，玩家拍板「可以按A修」）

**只改 `src\train_cmd.cpp`，未碰任何 `src\*.h`（KI-183 增量合规）。**

1. **新增 `static bool R3RWaitCoupleHeadStopPending(const Train *v, DestinationID st)`（`train_cmd.cpp:712`，紧接 `R3RWaypointOrderStopsForCouple()` 之后）**，判据五项，全为真才返回 true：
   - `v->IsFrontEngine()` 且 `v->current_order.IsType(OT_GOTO_COUPLE)`；
   - `prev = v->GetOrder(cur_real_order_index - 1（回绕）)` 的 `GetDestination() == st`；
   - `R3RWaypointOrderStopsForCouple(v, prev)` 为真（即该路点的下一条正是当前这条 GOTO_COUPLE，与 `R3RShouldCutCoupleAtWaitPoint()` 同口径）；
   - 运动前端仍在那个路点格上：`IsRailWaypointTile(moving_front->tile) && GetStationIndex(...) == st`；
   - `R3RShouldCutCoupleAtWaitPoint(v, nullptr) != 0`（仍未放行）。
   
   第 4 条是**防死锁的核心**：车头一越过路点格就立刻失效；第 5 条保证配对就绪（`wait_cut == 0`，即该发车）时限制自动解除，不发车就不解除 —— 因此既不需要 `cur_speed = 0`（不违反 KI-445 裁定），也不会把车永久钉在停车标上。
   
   同处新增手写前向声明 `int R3RShouldCutCoupleAtWaitPoint(const Train *v, uint32_t *wait_wp);`（该函数定义在文件后段 `:13269` 一带）。

2. **`PredictStationStoppingLocation()`（`train_cmd.cpp:1016-1017`）**：`r3r_head_stop` 由 `waypoint && dest 匹配 && R3RWaypointOrderStopsForCouple` 扩展为 `(原式) || R3RWaitCoupleHeadStopPending(v, dest)`；并在 `osl` 判定链尾部新增 `else if (r3r_head_stop) osl = OrderStopLocation::FarEnd;` —— 让切单窗口内 `osl` 仍是**格末**（否则列车比路点格短时 `overhang < 0` 会退化成 `Middle`，停在格中）。

3. **`ApplyLookAheadItem()` 的 `case TRLIT_STATION`（`train_cmd.cpp:1235-1236`）**：同样的 `||` 扩展。这是**主修复点** —— 原来 `order->ShouldStopAtStation(...) || r3r_head_stop` 在切单后两个条件都为假，导致 `limit_advisory_speed(..., 0, ...)` **一次都没被调用**，`advisory_max_speed` 回到线路限速，于是 55→68。

**未改**：`GetTrainStopLocation()`（`:773` 的 `!r3r_head_stop`）—— 它只在真正停站/加载时被调用，切单后不再对路点格调用；且该处 `overhang >= 0` 时本来就会把 `osl` 改成 FarEnd，与本次结论一致。若后续实测发现停稳位置仍偏中，再按同款扩展。

**构建**：复用既有 `_tmp_inc_build.cmd`（未新建任何 `.cmd`），只编探针内测树 `build\openttd.exe`。构建结果见 §9.7。

### 9.7 构建自证（2026-10-23）

- 护栏：`build\R3R_incbuild.guard.log` = `GUARD: incremental is safe (no header/lang file is newer than the newest object)` ⇒ 增量合法（本轮未碰任何 `src\*.h`、未动 `lang\*.txt`）。
- 链接：日志含 `[2/3] Building CXX object CMakeFiles\openttd_lib.dir\src\train_cmd.cpp.obj` → `[3/3] Linking CXX executable openttd.exe`，且 `findstr` 对 `error C` / `fatal error` / `FAILED:` 零命中。
- 判决：`build\R3R_incbuild.done` = `EXIT_CODE=0`。
- 时间戳：`src\train_cmd.cpp` 15:56:36 → `train_cmd.cpp.obj` 15:57:36 → `build\openttd.exe` 15:58:57（52 272 640 B，晚于全部源码）。
- `read_lints`：`src\train_cmd.cpp` 0 条。
- 本轮未新增日志字面量（只复用既有函数），故不做 exe 字符串核对。
- 发布树 `build-release` **未编**（按既有口径，只在玩家明确要求发版时才编）。

**状态**：已实现 + 已编译，**游戏内复测待做**（判据见 §9.5 末尾 5 条）。

---

## 十、第 274 轮：方案 A 首次复测 —— 仍不刹停（2026-10-23 16:07 日志）

### 10.1 输入有效性

- `build\R3R_debug.log` = 803 行 / 132 773 B / **16:07:34**
- 产生者 `build\openttd.exe` = **15:58:57**（= §9.7 编出的**含方案 A** 的探针版）⇒ **采样有效，这是方案 A 的首次实机复测。**

### 10.2 玩家口述（原话）

「不知道为什么，列车就是不刹停，你说有没有可能是这样，站台上已经出现了被动方的预留了，但是被动方还没有到位，但是主动方就误以为可以出发了」

### 10.3 已确认读数（锚点）

| 行 | 原文片段 | 判定 |
|---|---|---|
| `:194` | `CRT veh=21 order=16 dir=3 origin=205,290 td=9 found=0` | 本会话早期已出现过 `order=16`（另一次进入挂车阶段） |
| `:579` | `SPDD veh=21 from=55 to=54 dt=0 ctx=stale cfrom=0 cto=0 cage=-13 m=1 tile=248,233 x=3970 db=1 order=16 real=12 stuck=0 fp=0 one=0 prog=55 wait=0 track=01 dtile=491798` | **切单点与修复前逐字一致**：tile=248,233、x=3970、spd 55→54、`order=16 real=12`。`db=1`（整链在倒车系里读数） |
| 尾部 | `LOCO-MOVE veh=21 tile=250,233 j=200 spd=15 track=01 dir=1 db=1` | 车头**已越过路点格 2 格**（248,233 → 250,233），仍以 15 km/h 前进 ⇒ **没有停在格末停车标** |

- 探针统计：全日志 `COUPLE-FAIL` / `CPL-SKIP` / `WBAR` / `CPL-WAITHOLD` / `CPL-HEAD` / `COUPLE-OK` / `CPL-PAIR` / `CPL-GATE` / `SPD-DROP` **全部 0 条**，只有 `RESV-` 4 条。
  ⇒ 说明本会话**没有走到**耦合失败/等待截断/挂车成功那几条分支。

### 10.4 猜测根因（待验证，本轮最高优先级）

**猜测 R274-A（指向「限速指令根本没被重新算」）**：`SPDD` 行里的 `ctx=stale` 是关键线索 —— 若它表示「沿用的是**陈旧的 look-ahead 缓存**」，则 `ChooseTrainTrack` / `ApplyLookAheadItem` 在切单后的整段行程里**一次都没重跑**，于是：
- 方案 A 加在 `ApplyLookAheadItem` 里的 `limit_advisory_speed(..., 0, ...)`（主修复点）**从未被调用**；
- `:16020` 的 `r3r_wait_cut`（等待区截断）同样从未被调用 ⇒ **`wait_cut` 是 0 还是 2，整个切单后窗口里没有任何一次判定**。
- 这正是玩家直觉的机制版本：**不是"误以为可以出发"，而是"没有任何人再问一次能不能出发"**。
- 支撑：切单后 55→54 的逐 tick −1 是**普通线路限速减速**，不是"在停车标前刹住"（停车标在 x≈3983，距 x=3970 只有 13px，若 limit 0 真生效应在 1~2 tick 内掉到个位数）。
- 待验证判据：① `ctx=` 的取值来源（`SPDD` 探针里 ctx 字段对应哪个变量/枚举，stale 的定义）；② 切单后到尾部之间是否有任何一次 `CRT`/`CPL-*` 行（有 ⇒ lookahead 重算过）。

**猜测 R274-B（玩家的假设）**：站台上被动方（车底）已有预留、但车底还没到位，主动方据此认为可以发车。对应代码是 `R3RShouldCutCoupleAtWaitPoint()` 返回 0（`pair_ready` 真）⇒ 方案 A 的 pending 判据第 5 条为假 ⇒ 主动解除限速。**目前无证据**：全日志 0 条 `WBAR`，无法判断 `pair_ready`。

### 10.5 方案 A 自身的已知设计缺口（无论 R274-A/B 谁成立都要修）

**缺口 R274-C（确定性推演，不依赖日志）**：pending 判据第 4 条「车头必须仍在路点格」是防死锁设计，但与**刹车物理**冲突：
- 切单判据（`ProcessOrders` 的 `OT_GOTO_WAYPOINT`）在**格首像素**就成立，那一刻速度还有 55 km/h；
- 停车标在**格末**，只剩 ~13px；55 km/h 的刹车距离远大于 13px ⇒ 车**必然冲出格**；
- 冲出格 ⇒ pending 立刻失效 ⇒ 限速解除，而此时残余速度既不足以停住、又已无目标 ⇒ **以 15 km/h（`LimitSpeedFromLookAhead` 的 `max(15, end_speed)` 下限）持续爬行**。这与尾部 `spd=15 tile=250,233` 完全吻合。
⇒ **方案 A 按当前写法不可能让现场列车停在停车标上。** 必须让「等待区路点停车目标」在**越过路点格之后仍然有效一段**（例如到该路点的 `dest_tile` 端头 / 直到真正停稳或配对就绪），同时靠「已停稳 + 配对就绪」而不是「位置是否还在格内」来解死锁。

### 10.6 本轮待办

1. 核对 `ctx=stale` 的来源（R274-A 成立与否）；
2. 核对切单后是否还有 `CRT`/`CPL-*`（lookahead 是否重算）；
3. 按 10.5 修 pending 的失效条件（把「车头在格内」换成「未停稳 或 未放行 或 未越过该路点的 dest_tile 端头」之类可停住的判据）。

---

## 十一、第 275 轮：玩家新假设 —— 路点并不会自己截断预留，只是「借用最近的 PBS 信号灯」来截断（2026-10-23）

### 11.1 玩家原话（逐字）

「我可能又找到原因了，我们的这个机制并不能让路点学会截断预留，相反，我们只是让路点学会使用最近的信号灯（可能是PBS）帮它截断预留。因为我在游戏里就发现预留在路点前面第一个信号灯（为PBS）截断而非在路点被截断」

### 11.2 这条假设的含义（把它转成代码判据）

玩家的观察是**视觉层面**的：预留（黑/白虚线预览）的**终点**落在路点前方第一个 PBS 信号灯那一格，而不是落在路点格。

若成立，则等价于代码上：
- 我们的等待区路点机制**从未真正生成过**「以路点格末为终点的 0 速目标」；
- 真正让列车减速/停下的，是 look-ahead 里那个 **`TRLIT_SIGNAL` / `TRLIT_SIGNAL` 型限速项**（`LimitSpeedFromLookAhead(item.start, 0, ...)` 只在
  `realistic_braking_aspect_limited == TRBALM_ON && lookahead_end_position == item.start(/+1)` 时才生效，否则信号只经 `TRLIT_TRACK_SPEED`/`SPEED_RESTRICTION` 影响）；
- 一旦该信号灯是**普通信号**（不是 PBS）或**根本不存在**，预留就会一直延伸到 PBS 边界/线路末端，列车于是「不刹停」——与 §10.3 尾部 `spd=15` 永久爬行完全吻合。

### 11.3 疑点（必须登记，尚未验证）

**疑点 R275-A（高）**：`R3RWaitCoupleHeadStopPending()` 只影响 `PredictStationStoppingLocation()` 与 `ApplyLookAheadItem()` 的 `TRLIT_STATION` 分支；
而 `TRLIT_STATION` 项**是否会被真的生成**，取决于 `pathfinder/`（`FollowTrainReservation` 与 `TrainReservationLookAhead` 的构建）在路点格上是否推入 `TRLIT_STATION`。
若路点格只被当成普通轨道格、look-ahead 里根本没有 `TRLIT_STATION` 项，则本轮所有改动**全是死代码**（这能同时解释 §10.3 的"切单后一路降到 15 却停不住"）。

**疑点 R275-B（高）**：玩家观察的"预留终点在 PBS 信号灯"说明 look-ahead 的实际终点由 `lookahead_end_position` 决定，而 `reservation_end_position` 的截断点由 PBS（`TryPathReserve` / `ChooseTrainTrack` 的 `r3r_wait_cut` 分支）决定。需要核对我们把 `r3r_wait_cut != 0` 分支写成 `keep_track` 之后，**预留终点究竟落在哪一格**（是路点格、路点格前、还是信号灯格）。

### 11.4 待验证判据（下一组取证要给答案）

1. `TrainReservationLookAhead` / `FollowTrainReservation` 在**路点格**上推入的是哪种 `IT_*` 项（`TRLIT_STATION`？还是什么都不推）—— 搜 `TRLIT_STATION` 的唯一 push 点，看它的条件是否含 waypoint。
2. `wait_cut` 分支（`ChooseTrainTrack` 里 `r3r_wait_cut` 相关处）返回 `keep_track` 之后，`lookahead_end_position` 与 `reservation_end_position` 的取值来源。
3. 现场日志里是否出现过 `TRLIT_SIGNAL`/信号灯截断的痕迹（例如 `CPL-TRACE` 的终点格是否等于路点前方信号灯格）。

**状态**：已登记，待验证；未改任何源码。

### 11.5 第一组取证结论（`train_cmd.cpp:15930-16090`，已确认）

**结论 R275-1（成立）**：等待区截断**根本没有控制"预留铺到哪一格"**。截断只做了两件事：

1. `:15966-15978`：进入截断的**第一个 tick** 调一次 `FreeTrainTrackReservation(consist, origin.tile, origin.trackdir)` 把旧预留清掉（按 `(veh, cur_real_order_index)` 记账，同一订单只清一次），探针 `CPL-WAITCUT-CLEAR`；
2. `:15983`：然后**无条件**调 `DoTrainCouplePathfind(consist, true)` —— 即**挂车专用 YAPF** 重新铺路，`:15989-15991` 把它的返回值当 `best_track`。

**结论 R275-2（这条正是玩家指出的那层）**：既然路线是**挂车 YAPF** 铺的，预留终点就由挂车 YAPF 的规则决定（PBS 信号灯 = 安全等待位 = 天然边界；路点不是 PBS 边界），**路点从来没被当成预留终点**。所以视觉上"预留在路点前面第一个 PBS 信号灯处被截断"，就是挂车 YAPF 自己按信号灯停的 —— 与我们加的 `wait_cut` 无关。**玩家 2026-10-23 的观察成立。**

**结论 R275-3（不刹停的直接机制）**：`:16081-16082`

```cpp
if (r3r_wait_cut == 2) {
    return { keep_track != INVALID_TRACK ? keep_track : FindFirstTrack(tracks), result_flags };
}
```

`state == 2`（"原地等"）**直接返回 keep_track、连挂车 YAPF 都不重跑** ⇒ 列车继续按手上那条"铺到信号灯/车底"的旧预留行驶。这与 §10.3 尾部 `spd=15 tile=250,233` 永久爬行完全一致。

**结论 R275-4（`TRLIT_STATION` 的分布）**：`TRLIT_STATION` 全仓只出现在 `train_cmd.cpp` / `pbs.h`（枚举定义）/ `table/newgrf_debug_data.h`（dump），**没有独立 push 点**（`items.emplace_back` / `AddItem(` 在 `train_cmd.cpp` 零命中）⇒ look-ahead 项的生成走 `TrainReservationLookAhead` 自己的接口（`pbs.h` 里的 `AddItem` 家族）。**§10.4 猜测 R274-A 的"限速指令没被重算"得到间接支持**：切单后若 look-ahead 不重建，`ApplyLookAheadItem()` 整段不会跑，本轮方案 A 的三处改动就全是死代码。

**下一步取证**：`R3RShouldCutCoupleAtWaitPoint()`（`:13269` 一带）里 state 1 / state 2 的判据分别是什么，以及截断分支有没有任何"把 YAPF 终点设成路点"的入口（若无 ⇒ 修法必须换：不是加限速，而是把挂车 YAPF 的终点/边界改到路点）。

### 11.6 第二组取证结论（第 275 轮收口，2026-10-23）

**结论 R275-5（R275-A 证伪：方案 A 不是死代码）**：`TRLIT_STATION` 项的生成点是 `pbs.cpp` 的 `TrainReservationLookAhead::AddStation(int tiles, StationID id, int16_t z_pos)`，由 `FollowReservation` 在跟随器进入**站台类格**（`ft.is_station`）时调用 `AddStation(1 + ft.tiles_skipped, GetStationIndex(ft.new_tile), z)`。路点格本身就是 station tile ⇒ **等待区路点一定会生成 `TRLIT_STATION` 项**，`ApplyLookAheadItem()` 的 `case TRLIT_STATION` 每 tick 都会跑，KI-448 方案 A 的三处改动**不是**死代码。

**结论 R275-6（方案 A 生效了，但物理上刹不住 —— 这才是"就是不刹停"的真相）**：现场 `build\R3R_debug.log` 逐帧读数：

| 锚点 | 读数 | 判读 |
|---|---|---|
| SPDD `tile=247,233 x=3960 from=72→71 … db=1 order=6 real=11` | 72 km/h 起**连续逐帧制动** | 0 速目标确实存在（方案 A 生效） |
| SPDD `tile=248,233 x=3976 from=39→38` | 已进路点格仍在降速 | 制动持续，但位置已越过格首 |
| §10.3 `x=3970 from=55` | **进格时约 55 km/h** | 距格末（x≈3983）仅约 13px，不可能停住 |
| CPL-ENTRY / CPL-ORIGIN `tile=248,233 orderType=16` | 订单已切 `OT_GOTO_COUPLE`、车头仍在路点格 | 切单时刻在车头进格首像素 |
| CPL-WAITCUT `state=2 wp=17 tile=248,233` | 截断判据认为"在原地等" | 走 `:16081` 早退、不重跑 YAPF |
| 尾部 SPDD 39→…→18、LOCO-MOVE `tile=250,233 spd=15` | 冲出格后落到 15 km/h 下限 | pending（"车头仍在路点格"）失效 ⇒ 0 速目标消失 |

⇒ 方案 A 用的是 `limit_advisory_speed`（**软限速 = 尽力减速，不是硬停**），且其 0 速目标位置取 `PredictStationStoppingLocation()` 对 waypoint 的 `FarEnd`（格末 x≈3983），而**到达判据**在车头进格首像素（x=3968）就触发并切单 —— 两把尺子相差约 0.9 格。刹车距离不够 + `state==2` 不重跑 YAPF ⇒ 必然冲出格并永久爬行。

**结论 R275-7（收口：玩家 2026-10-23 的假设就是根因）**：预留终点从来由**挂车 YAPF** 决定，`wait_cut` 只做「清一次旧预留 + 重跑同一个 YAPF」；YAPF 的天然终点是 PBS 安全等待位 ⇒ **预留在路点前第一个 PBS 信号灯处截断，路点从未被设为终点**。因此：
- 现象"预留在信号灯截断而非路点"= 事实，机制正确，与 `wait_cut` 无关；
- "不刹停" = 我们用的是派生自 look-ahead 项的**事后限速**，改不了"车必须开到哪里才停"的几何；
- 在 `ApplyLookAheadItem` 里加限速/加 0 速目标这条路**已走到尽头**（最多让车在正确位置减速，不能让它停住并被放行）。

### 11.7 修法方向（待拍板，尚未改码）

**唯一正确的切入点 = 挂车 YAPF 的终点/边界，而不是 look-ahead 限速。** 两条候选：

- **方案 D（推荐）**：`r3r_wait_cut == 1`（尚未放行）时，把等待区路点格设为挂车 YAPF 的**终点节点**，使 `DoTrainCouplePathfind()` 铺出的预留本身止于路点格 ⇒ 预留视觉、`safe waiting position`、制动三者同时对齐；`state == 2`（已停在路点格）继续原地等。
- **方案 E（兜底）**：保留现有 YAPF 终点规则，但在 `ChooseTrainTrack` 返回前把 `best_track` / 预留**截断**到路点格之前的最后一个安全位（等价于把路点当 PBS 边界）。

**待确认（下一组取证，不得跳过）**：
1. `YapfTrainCoupleTrack()` / `FindNearestCoupleTrain()` 里"终点"由谁设置（`PfDetectDestination` / `AddDestinationNode` / `SetDestination`），有没有现成的"到此为止"参数可直接填路点格;
2. `R3RShouldCutCoupleAtWaitPoint()` 返回 1 时，调用方（`:15983`）除重跑 YAPF 外是否还有任何把 `wait_wp` 传给 YAPF 的通道（若无 ⇒ 方案 D 需新增参数，触及 `yapf_rail.cpp` 签名）;
3. 方案 D 下 `state 2 → 1` 的回退（车被挤出路点格后重新要求预留）是否会产生原地抖动。

**状态**：结论已收口（R275-5/6/7 成立）；修法方向已定但**未改任何源码、未构建**；等玩家拍板 D/E 后再动 `src`（预计改 `src\pathfinder\yapf\yapf_rail.cpp` + `src\train_cmd.cpp`）。

### 11.8 玩家拍板（2026-10-23，原话「那就按D来修」）

**D = 让挂车 YAPF 的终点/边界落在等待区路点格**，使预留本身止于路点。落地要点（本轮执行）：

1. `r3r_wait_cut == 1`（尚未放行）时，`DoTrainCouplePathfind()` 的终点若仍按「最近的等待车底」算，需改成**先到路点、路点是终点**；
2. `state == 2`（已停在路点格原地等）保持现状不重跑 YAPF；
3. 放行后（`wait_cut == 0`，配对就绪）必须恢复原语义，否则永远挂不上 —— 这是 D 的**首要防死锁判据**；
4. 不得改动 `R3RShouldCutCoupleAtWaitPoint()` 的判据语义，也不得回退 KI-445（不写 `cur_speed = 0`）。

**取证入口（本轮必查）**：`src\pathfinder\yapf\yapf_rail.cpp` 的挂车寻路类，重点 `YapfTrainCoupleTrack` / 其 pf 的终点判定函数（`PfDetectDestination` / `AddDestinationNode` / `FindNearestCoupleTrain`），确认「终点」是车底所在格还是可由调用方指定。

**状态**：已拍板，开始按 D 落地（未改码前先取证入口）。

### 11.9 取证：D 早就实现了，但本次现场**一次都没生效**（2026-10-23 复测）

**结论 R275-8（重要纠错：D 已在第 276 轮落地）**：`R3RShouldCutCoupleAtWaitPoint()`（`train_cmd.cpp:13279-13403`）就是 D 的实现，`yapf_rail.cpp:869` 的 `Yapf().SetCoupleWaitCut(r3r_wait_cut == 1, r3r_cut_wp)` 把等待区路点当终点。所以本轮**不是从零实现 D**，而是修 D 的执行缺口。

**结论 R275-9（现场铁证：截断从未接受过路点终点）**：`build\R3R_debug.log` 计数——
- `PFD-ACCEPT kind=waitwp` = **0**
- `accwp=[1-9]` = **0**
- `CPL-WAITCUT` = **1**（唯一一条 `state=2 wp=17 tile=248,233`）

⇒ 判据在这趟车的一生里只进入过 state=2，**从头到尾没有一次返回 1**，YAPF 也就从没被要求以路点为终点。这与"预留在路点前第一个 PBS 灯处截断"完全吻合（终点仍是车底方向的安全等待位）。

**猜测根因（待验证）— R275-G1**：`train_cmd.cpp:13396-13401` 的
```cpp
if (front_on_wp) {
    return 2;   // 已站在栅栏格上但未放行：无合法终点
}
```
**漏了 `cur_speed == 0` 这一半条件**。现场车以约 55 km/h 冲进路点格 ⇒ `front_on_wp` 立刻为真 ⇒ 返回 2 ⇒ `yapf_rail.cpp:862-867` 直接 `return INVALID_TRACKDIR`（不寻路）⇒ `ChooseTrainTrack` 保留旧 track ⇒ **既没被截断、也没被刹车**，车按旧预留驶离路点格 ⇒ 下一 tick `front_on_wp` 为假且 `seen_on_wp` 为真 ⇒ `overshoot = true`（`:13327-13334`）⇒ `:13335` `return 0` ⇒ 本命令内这堵墙**永久失效** ⇒ 之后一路放开到 15 km/h 下限爬行。

**修法（本轮执行，方案 D 收口）**：`front_on_wp && v->cur_speed == 0` 才返回 2；`front_on_wp` 但仍在动 ⇒ 返回 **1**，让 YAPF 以脚下路点格为终点铺一条短预留并由原生刹车停住。理由：`overshoot` 已在车真正驶离整格后兜底，不会重演 KI-435 的"终点在身后 ⇒ 永久停车"。

### 11.10 落地（第 275 轮续，2026-10-23，仅改 `src\train_cmd.cpp`）

**改动一（R275-G1，主因）**：`R3RShouldCutCoupleAtWaitPoint()` 的**进入条件**由"只认 `current_order == OT_GOTO_COUPLE`"改为两阶段：

- 阶段一（接近路点）：`current_order` 是 `OT_GOTO_WAYPOINT` 且下一条是 `OT_GOTO_COUPLE`（判据复用 `R3RTrainStopsAtWaypoint()`，与 KI-448 的 look-ahead 同口径）⇒ `wait_dest = current_order.GetDestination()`；
- 阶段二（进格后订单已推进）：`current_order` 是 `OT_GOTO_COUPLE`、前一条是路点 ⇒ `wait_dest = prev->GetDestination()`（原逻辑）。

**改动二（R275-G2）**：原 `if (front_on_wp) return 2;` 改为 `if (front_on_wp && v->cur_speed == 0) return 2;`，其余（在栅栏格内但仍在动）落到 `return 1`。

**为什么这两条才是 D 的真缺口**：D 的终点规则（`yapf_destrail.hpp:459-477` 的 `couple_wait_cut` 分支）本身没问题——它只按 `IsRailWaypointTile` + `StationID+1` 匹配，车站在格内也能命中。坏的是"什么时候启用截断"：判据在真正需要减速的整个接近段恒返回 0，进格后又因 `cur_speed != 0` 返回 2（不寻路、不刹车）。

**构建自证**：复用既有 `_tmp_inc_build.cmd`（未新建任何脚本）；`build\R3R_incbuild.done` = `EXIT_CODE=0`（16:32:08）；日志 `[2/3] Building CXX ... train_cmd.cpp.obj` → `[3/3] Linking CXX executable openttd.exe`；`src\train_cmd.cpp` 16:29:36 → `train_cmd.cpp.obj` 16:30:37 → `build\openttd.exe` 16:32:01（52 272 640 B）；`read_lints` 0 条。

**复测判据（5 条）**：
1. 同场景接近路点时应出现 `CPL-WAITCUT veh=N state=1`（且 `wp` 非 0），而不是一上来就是 `state=2`；
2. 日志应出现 `PFD-ACCEPT kind=waitwp` 且 `CPL-SCAN ... accwp=` ≥ 1（D 真正生效的正面证据）；
3. 列车应在等待区路点**格末停车标**上停住（`cur_speed == 0`、不再 15 km/h 爬行），随后出现 `WBAR-RELEASE` 且配对后正常 `COUPLE-OK`；
4. 负判据：不接 `GOTO_COUPLE` 的普通路点**不得**出现 `CPL-WAITCUT`/`PFD-ACCEPT kind=waitwp`，通过行为逐字不变；
5. 负判据：已越过路点后不得出现 `WBAR-OVERSHOOT` 之后又被拦（即不得重现 KI-435 的永久停车）。

**状态**：已改码 + 已编译（`EXIT_CODE=0`）；游戏内复测待做（需玩家用 `build\openttd.exe` 跑同一场景回日志）。

### 11.11 第 278 轮复测取证（2026-10-23，玩家三点报告）

**玩家原话要点（必须保留）**：
1. 「预留是在信号灯前面结束的」；
2. 「命令却是在路点那里跳到前往挂接命令的」；
3. 「随后主动方列车的寻路紊乱了」。

**现场**：`D:\temp\codebuddy-dropped-files\7b94d354-...\R3R_debug.log`，209 816 B / 1 455 行，`LastWriteTime` 16:35:58（本轮 `build\openttd.exe` 16:32:01 产物）。
关键词计数：`CPL-WAITCUT`=2、`PFD-ACCEPT`=2、`PFD-REJ`=0、`WBAR`=0、`CPL-SCAN`=3、`COUPLE-OK`=0、`COUPLE-FAIL`=2、`CRT veh=`=7。

#### 11.11.1 正判据 1/2/5 全部通过 —— 方案 D 这次真的生效了

| 判据 | 现场锚点 | 读数 |
|---|---|---|
| ①`state=1` + `wp≠0` | `build\R3R_debug.log:766` | `CPL-WAITCUT veh=21 state=1 wp=17 tile=248,233` |
| ②`PFD-ACCEPT kind=waitwp` + `accwp≥1` | `:767` / `:769` | `PFD-ACCEPT kind=waitwp tile=254,233`；`CPL-SCAN ... accwp=3 ... last=254,233 ... why=cut-wp` |
| ②bis 终点 = 路点 | `:770` | `CPL-BEST veh=21 tile=254,233 td=8 cost=900` |
| 预留止于路点格 | `:810-816` | `RP cur=248,233 → 249 → 250 → 251 → 252 → 253 → 254,233 td=8 depth=0..6`（**恰好 7 格，止于路点格 254**） |
| 预留登记 | `:818` / `:877` | `CPL-RESERVE veh=21 reserved=1 next=8` |
| ⑤无 overshoot | 全日志 | `WBAR*` 计数 = **0** |

⇒ 玩家第 1 点「预留在信号灯前面结束」**极可能就是正确行为**：`PFD-ACCEPT kind=waitwp tile=254,233` + `RP` 序列止于 254，说明 254,233 就是等待区路点格，预留不再越过它铺到信号灯外。**待玩家确认 254,233 是否就是那个等待区路点**（若玩家眼中路点在 255 及更远，则另议）。

#### 11.11.2 缺陷 R278-1（高）：判据在订单推进的瞬间从 1 掉成 0，放行**无任何日志**

`:1272` 是第二条、也是唯一一条状态变化记录：

```
CPL-ENTRY veh=21 tile=253,233 orderType=16 dontReserve=0
CPL-ORIGIN veh=21 origin=253,233 td=8 vehDir=1 vehTd=8 dontReserve=0
CPL-WAITCUT veh=21 state=0 wp=17 tile=253,233     <- 关键
CPL-PATHFOUND veh=21 found=0
CPL-SCAN tick=3008 veh=21 found=0 closed=82 limit=10000 nodes=308 enter=226
         noplat=150 nores=6 noconsist=67 consist=3 rej=3/0/0/0 acc=0/0 accwp=0
         last=278,234 lastveh=24 why=GROUP
```

车头此时在 `253,233`（路点格 254 的**前一格**），而订单表（`:1283-1295` 的 `LOCO-ORD`）为：

```
0 type=16 | 1..6 type=1 | 7 type=6 | 8 type=2 | 9..10 type=1 | 11 type=6 | 12 type=16
```

`real=12`、`order=16`、`prev=order[11]=type 6(OT_GOTO_WAYPOINT)` ⇒ 阶段二完全成立、`wait_wp=17` 也赋上了（日志 `wp=17` 可证）。

**因此 `state=0` 只剩一个出口**：`pair_ready == true` 时的 `return 0`（`src\train_cmd.cpp:13411-13417`）。理由是代码里其余 `return 0` 都在 `wait_wp` 赋值（`:13325`）之前，会让日志打成 `wp=0`；而 `bs.overshoot` 分支置位时必打 `WBAR-OVERSHOOT`，现场 `WBAR`=0。

**放行为何无日志**：`WBAR-RELEASE` 的打印带 `if (bs.seen_on_wp && R3RDbgOn())`（`:13412`）⇒ `seen_on_wp == false` 时静默放行。而 `seen_on_wp` 只由 `front_on_wp` 置位（`:13348-13350`），本例运动前端在 `259,233`（`:1286` `COUPLE-WAIT-OFFDEST veh=21 tile=253,233 mf=259,233 real=12 ...`）⇒ **从未被观测在 254** ⇒ 静默。再叠加 `order_index` 从 11 推进到 12 时 `_r3r_wbar_state` 被 erase（`:13306-13310`），阶段一累积的 `seen_on_wp` 也一并清掉。

⇒ **结论 R278-1**：`pair_ready` 是"与位置无关"的放行口（口径原意是"就绪即不拦"），但现场它发生在**车头还在路点前一格**的时候，且**不留任何日志**。这与玩家第 2 点「命令在路点那里跳到前往挂接命令」吻合：命令已在 `GOTO_COUPLE`，墙又静默解除。

#### 11.11.3 缺陷 R278-2（高）：`pair_ready` 与挂车 YAPF 用**两把不同的尺子** ⇒ 放行即 `found=0` ⇒ 预留被释放 ⇒ `stuck`

`CPL-SCAN` 的 `why=GROUP`、`last=278,234 lastveh=24` 说明：YAPF **已经找到**等待车底 `veh=24`（在 `278,234`），但在 `R3RCanCoupleNow()` 的**分组闸门**被拒。

而 `pair_ready` 的判据（`:13399`）只有：

```cpp
if (a != nullptr && a->cur_speed == 0 && IsRailStationTile(a->tile) && R3RIsCoupleTarget(a))
```

**不含任何分组/货种/公司闸门**，与 YAPF 侧的 `R3RCanCoupleNow(v, u, "geo")` 不是同一套。

⇒ **放行 → YAPF 找不到合法终点 → `found=0` → 旧预留被 `FreeTrainTrackReservation()` 一次性清光**，现场紧接 5 行释放 + 记账遗忘：

```
RESV-GC veh=21 tile=254,233 track=stn kind=WAYPOINT why=free-path-hold
RESV-GC veh=21 tile=253,233 / 256,233 / 257,233 / 258,233 / 255,233  why=free-path
RESV-LEDGER-FORGET veh=21 why=free-path mask=0x1 released=5 kept=1 phase=choose-track
COUPLE-FAIL loco=21 order=16 tx=253 ty=233
COUPLE-WAIT-OFFDEST veh=21 tile=253,233 mf=259,233 real=12 ... destStn=12
SPDD veh=21 from=30 to=0 dt=5 ctx=stop-train ... stuck=1
STUCK veh=21 tile=253,233 state=1 stuck=1 ... order=16 real=12
DEPOT-ARR veh=21 spd=0 real=12(16) curType=16 stuck=1 ... destTx=278 destTy=240
```

⇒ 这就是玩家第 3 点「主动方列车的寻路紊乱」：**车在最需要预留的那一格被抽走预留**，随后 `STUCK`，再往后又自己动起来在 `253 → 254 → 255` 之间反复（`:1448` `RESV-REL who=CLEAR-STN veh=21 tile=254,233 phase=controller`、`:1450` `LOCO-MOVE veh=21 tile=255,233 spd=15 dir=1 db=1`，`SPDD ... ctx=stale cage=2 → 5 → 8`）。

#### 11.11.4 猜测根因（待验证，按铁律先登记）

- **G-278-A**：`pair_ready` 缺少 `R3RCanCoupleNow(v, a, ...)`（或至少分组闸门），导致"放行"与"YAPF 可达"不等价。支撑：`why=GROUP` + `lastveh=24` + 同为 `a` 车号。验证：把闸门加进 `pair_ready` 后同场景应看到 `state` 保持 1（不再静默变 0）、`found=1`。
- **G-278-B**：`pair_ready` 不该与位置无关。本场景车头尚在路点前一格就被放行。支撑：`:1272 tile=253,233` + `mf=259,233`（倒车链，运动前端已过路点 5 格，但**车头没到**）。验证：`pair_ready` 追加"车头已到栅栏格或已停稳"条件后，车应在 254 停住再放行。
- **G-278-C**：`order_index` 从 11→12 时 erase 墙状态（`:13306-13310`）会丢掉 `seen_on_wp`/`overshoot` 的连续性，使阶段一→阶段二交接处出现"看不见的空窗"。这正是 `WBAR`=0 的另一半解释。验证：erase 时把 `seen_on_wp`（及 `overshoot`）继承到新状态而不是清零。

#### 11.11.5 疑点登记（R278-Q）

- **Q-278-1**：玩家说"预留在信号灯前面结束"。实测预留确实止于 `254,233` 且该格被 `PFD-ACCEPT kind=waitwp` 认作路点。**存疑点**：`254` 与玩家看到的"信号灯"是不是同一位置？若玩家期望预留延伸到 255+（信号灯之后），则说明等待区路点的 `StationID+1` 指向了比预期更早的那格。需要玩家给出等待区路点的**实际坐标或截图**。
- **Q-278-2**：`COUPLE-WAIT-OFFDEST` 的 `destStn=12 destTile=491798`（= `278,234` 一带）与车头 `253,233` 相差 25 格，而 `mf=259,233`。若等待车底 `veh=24` 真在 `278,234`，则这条 GOTO_COUPLE 的目标与路点（254）位置相距 24 格 —— 路点是否放错了地方？需要 `CPL-PAIR`/车底位置快照。
- **Q-278-3**：`LOCO-ORD 0 type=16` 与 `12 type=16` 同时存在（表首尾都是挂接命令），`real=12` 已到表尾。推进到 0 会再触发一次挂接；是否本意如此未确认。
- **Q-278-4**：`db=1`（`DrivingBackwards`）全程为真。判据里 `front_on_wp` 用 `GetMovingFront()`，而"车头是否到路点"用 `v->tile` —— 倒车链上这是**两个不同的车节**，`R3RShouldCutCoupleAtWaitPoint` 内部目前混用（`front_on_wp` 看 mf、`return 2` 看 `cur_speed`）。是否应该统一，待拍板。

#### 11.11.6 修法候选（待拍板，未落地）

| 代号 | 内容 | 治什么 |
|---|---|---|
| **D-2（首选）** | `pair_ready` 判据追加 `R3RCanCoupleNow(v, a, "wait-cut")`（或至少 `R3RCoupleAllowed*` 分组/公司闸门），使"放行" ⟺ "YAPF 能找到它" | R278-2 根因（`why=GROUP` 放行即 `found=0`） |
| **D-3** | `pair_ready` 追加位置条件：车头（`v->tile`）已进入栅栏格，或 `bs.seen_on_wp` | R278-1（路点前一格就放行） |
| **D-4** | `WBAR-RELEASE` 去掉 `bs.seen_on_wp` 门禁（或新加 `WBAR-PASS`），每次放行都留一行含 `tile/mf/spd/why` | 可观测性：这次就是"静默放行"导致无法凭日志区分 |
| **D-5** | `bar` 因 `order_index` 变化被 erase 时，把 `seen_on_wp` 继承过去而不是清零 | G-278-C（阶段交接空窗） |

**状态**：第 278 轮仅取证 + 登记，**未改任何源码、未构建**。修法待玩家拍板（首推 D-2 + D-4 一起做：先能让 YAPF 找到，再让每一步可见）。

### 11.11.7 第 278 轮落地（D-2 + D-4，2026-10-23）

**玩家口径**：先说明「发现了什么、修了什么」，再编译；不要求额外方案讨论。

| 代号 | 落地内容（仅 `src\train_cmd.cpp`，`R3RShouldCutCoupleAtWaitPoint()`） |
|---|---|
| **D-2** | `pair_ready` 判据追加 `&& R3RCoupleAllowedIgnoringPair(v, a)`，与挂车 YAPF 的分组/目的地闸门同源。**不用 `R3RCanCoupleNow()`**：该函数入口判 `VehState::Stopped`（active-stopped），主车停稳时恒假 ⇒ 若照搬会让车停在路点永远放不了行（新死锁）。 |
| **D-4** | `struct R3RWaitBarrierState` 加 `bool released`（`emplace` 改 `{order_index,false,false,false}`）；`WBAR-RELEASE` 去掉 `bs.seen_on_wp` 门禁，改为每道墙首放必打，payload 补 `seen=` / `mf=x,y` / `pair=<veh>`。 |
| 未做 | D-3（位置条件）、D-5（erase 时继承 `seen_on_wp`）、Q-278-1~4 的取证（需玩家给路点坐标/分组关系）。 |

**构建自证**：复用 `_tmp_inc_build.cmd`（未新建 .cmd）；`build\R3R_incbuild.done`=`EXIT_CODE=0`；`[2/3] train_cmd.cpp.obj` → `[3/3] Linking CXX executable openttd.exe`；`src\train_cmd.cpp` 16:44:31 → obj 16:47:51 → `build\openttd.exe` 2026-10-23 **16:49:25**（52 272 640 B）；`read_lints` 0；exe 字面量命中 `WBAR-RELEASE veh=%d wp=%u onwp=%d seen=%d` 与 `mf=%d,%d pair=%d`。发布树 `build-release` 未编（按既有口径）。

**复测判据（5 条，与 KI-450 同文）**：
1. 分组不匹配现场：`CPL-WAITCUT` 保持 `state=1/2`，不再静默变 0，不出现 `STUCK veh=21`；
2. 每次真正放行必有 `WBAR-RELEASE ... seen=... mf=x,y pair=N` 一行；
3. 放行当 tick 不得紧跟 `found=0` + 一排 `RESV-GC why=free-path`（即「放行 ⟺ YAPF 可达」成立）；
4. 负判据：分组匹配的正常场景不被误拦，`COUPLE-OK` 照常；
5. 负判据：普通路点（下一条非 `GOTO_COUPLE`）逐字不变。

**待玩家确认**：①`254,233` 是否就是你说的「信号灯前那个等待区路点」（Q-278-1）；②机车 21 与等待车底 24 的挂接分组是否真的不匹配（YAPF 判 `why=GROUP`）；③`destStn=12 / destTile≈278,234` 与路点 254 相距 24 格，路点是否放置偏早（Q-278-2）。

### 11.12 第 278 轮续：玩家四条答疑 + 新增修复要求（2026-10-23）

**玩家原话（决定性，必须逐条保留）**：

> **C-278-1**：「254,233 是我说的那个信号灯前的等待区路点」
> **C-278-2**：「机车 21 与等待车底 24 的挂接分组都是无分组」
> **C-278-3（新修复要求）**：「我建议再补上一点修复：挂接配对只配对处于一个等挂，一个主动挂的一对列车，并且，在它们配对成功前，前往挂接这条命令的效果等于 OT_WAITING 的效果」
> **C-278-4（对预留的新观察，纠正我的读数口径）**：「还有，一开始的时候，主动方的预留压根就没在路点那里停下来，你看到的只是主动方的预留到了信号灯前，主动方到了路点之后才把路点到信号灯那一段销毁」

#### 11.12.1 由四条答疑直接产生的判定变更

| 项 | 旧结论 | 新判定（依玩家原话） |
|---|---|---|
| **Q-278-1** | 存疑：254 是否即玩家口中的信号灯 | **关闭**。254,233 就是等待区路点格 ⇒ 方案 D 的截断位置**正确**，§11.11.1 的"止于 254"是期望行为，不是缺陷。 |
| **G-278-A / R278-2 的分组解释** | 猜测"机车分组与车底 24 不匹配"导致 `why=GROUP` | **证伪**。双方**都是无分组** ⇒ 分组白名单不可能是拒绝原因。`why=GROUP` 必须另有来源，**待重新取证**（候选：`R3RCoupleAllowedIgnoringPair` 的前置早退未打印具体分支；`R3RIsCoupleTarget` 与该 reason 标签错配；公司边界闸门；`OT_WAIT_COUPLE` 判定失败被归到 GROUP 标签）。在查清前**不得**把 D-2 当成"已修"。 |
| **D-2（已落地的源码改动）** | 修法首选，视为正确 | **保留但降级**：它本身无害（`R3RCoupleAllowedIgnoringPair` 在双方无分组时返回 true ⇒ 行为不变），可作为"与 YAPF 同源"的正确方向，但**治不了本现场**。 |
| **R278-2 的预留现象** | "预留停到路点格，放行后被 free-path 清光" | **修正口径**（C-278-4）：预留一开始**并未在路点停**，而是**延伸到了信号灯**（即越过 254 到信号灯那一段）；等主动方车头真正开到路点之后，才把「路点到信号灯」这一段销毁。⇒ 我上一轮把 `RP cur=248→254` 读成"预留止于路点"是**误读**：那是销毁之后的末态，不是初始态。 |

#### 11.12.2 C-278-3 = 新增必修项 D-6（玩家拍板，非候选）

拆成两条独立要求，缺一不可：

- **D-6a 配对唯一性**：挂接配对只允许「**一个等挂方** + **一个主动挂方**」构成一对。同一列等待车底不得同时被多个主动方配对；同一主动方也不得同时持有多个目标。
- **D-6b 未配对 ⇒ 效果等同 OT_WAITING**：在配对成功之前，`OT_GOTO_COUPLE` 这条命令的运行期效果**必须等于 `OT_WAITING`** —— 即：不寻路、不预留、不推进订单、不因"找不到目标"而把已有预留清掉、车保持停住。

**为什么 D-6b 是本轮的真正解**：R278-2 的杀伤链是「放行 → YAPF found=0 → `FreeTrainTrackReservation()` 清预留 → 车在关键格失去预留 → STUCK → 253/254/255 乱走」。只要 GOTO_COUPLE 在未配对期完全不进入寻路/预留流程，这条链从源头断掉，**不依赖**去修正 `why=GROUP` 的某个具体判据。

**与既有条目的关系**：第 242 轮 KI-348 已实证「`cur_speed==0` 时 `TrainLocoHandler` 恒走停驶分支，对 GOTO_COUPLE 只做几何命中、不寻路」，即"停车态本来就不订路"。但那是有条件的（速度必须已为 0）。D-6b 要求把这条从"碰巧停住了"升级为"**语义上就是 WAITING**"，覆盖速度非 0、目标消失、配对未建立等各种入口。

#### 11.12.3 待取证清单（本轮，按铁律单轮配额）

- **E-278-1**：`src\pathfinder\yapf\yapf_destrail.hpp` 里判定为 `why=GROUP` 的**确切代码位置与条件**（双方无分组时为何命中）。
- **E-278-2**：`src\couple_group.cpp` 的 `R3RCoupleAllowedIgnoringPair()` 在"双方均无分组"时的返回值与早退分支（KI-195 说已删交集否决，需确认）。
- **E-278-3**：`R3RGetCouplePairPartner()` 当前是否已具备"唯一配对"语义（是否会被多个主动方同时认领、是否校验对方是 `OT_WAIT_COUPLE`）。
- **E-278-4**：GOTO_COUPLE 在 `TrainLocoHandler` / `TryPathReserveWithResultFlags` / `ProcessOrders` 三处的入口，确定 D-6b 的最小插入点（优先复用第 127 轮 KI-208 那个"停稳等待者不订路"的门禁位置）。

#### 11.12.4 取证结论（E-278-1 / E-278-2 已查明）

**E-278-1 结论：「`why=GROUP` 这个标签是错配的，它同时代表三件完全不同的事」。**

`src\pathfinder\yapf\yapf_destrail.hpp:606` 只有一处调用：

```cpp
if (!R3RCoupleAllowed(Train::From(Yapf().GetVehicle()), t)) {
    r3r_dest_reject(0, tile, t);   // why=0 ⇒ 打印成 PFD-REJ-GROUP
    return false;
}
```

而 `R3RCoupleAllowed()`（`src\couple_group.cpp:597-608`）是两层叠加：

```cpp
if (!R3RCouplePairMatches(coupler, target)) return false;              // KI-182 配对锁
return R3RCoupleAllowedIgnoringPair(coupler, target);
```

`R3RCoupleAllowedIgnoringPair()` 内部按顺序有三道闸门，**任何一道 false 都被 yapf 记成 `GROUP`**：

| 顺序 | 代码位置 | 闸门 | 语义 |
|---|---|---|---|
| 1 | `couple_group.cpp:385-411` | **目的地**（KI-165 / KI-288） | 命令写的是车站 A，候选却停在车站 B / 站台 ⇒ false |
| 2 | `couple_group.cpp:434-461` | 临时挂接分组合并 | 只改掩码，不单独否决 |
| 3 | `couple_group.cpp:503-510` | **公司** → **分组** | 跨公司走 `R3RCoupleGroupMasksAllowCrossCompany`，同公司走 `R3RCoupleGroupMasksCompatible` |

⇒ `why=GROUP` 实际可能是「配对锁不匹配」「目的地不匹配」「跨公司且分组不允许」「同公司分组不兼容」四者之一。**"双方都无分组"时**只剩前两种可能：

- 同公司 + 两方掩码均 0 ⇒ `R3RCoupleGroupMasksCompatible(0,0)` 按注释（`:480`「同公司两方都无分组 ⇒ 可挂（同一个隐式组）」）应为 **true** ⇒ 分组闸门不可能是元凶；
- 跨公司 + 两方掩码均 0 ⇒ 按注释（`:481`「跨公司两方都无分组 ⇒ 拒（隐式组不是任何人显式拥有的组）」）必为 **false** ⇒ 会命中。

**因此现场只剩两种解释，需一次日志对证**：**(甲)** `veh=24` 所在格 `278,234` 不是命令指定的车站 12（目的地不匹配，`:409`）；**(乙)** 机车 21 与车底 24 属于**不同公司**（`:503`）。二者都与"双方无分组"完全自洽。**待 §11.12.5 用 `CPL-PAIR` / `CG-GATE` / `destStn` 三者对证。**

**E-278-2 结论：D-6a（唯一配对）在当前代码里已经存在，不是缺失功能。**

`src\couple_group.cpp:528-595` 已有一套完整的配对锁：

- `R3RCouplePairMatches(c)`：要求 `c->r3r_couple_target == t->index && t->r3r_couple_requester == c->index` —— **双向互指**，天然保证一对一；
- `R3RPairCoupleTargets(c, t)`：先 `R3RUnpairCoupleTargets` 双方各自旧锁再写新锁 ⇒ 一个车底被新主动方认领时，旧主动方自动解锁；
- `R3RGetCouplePairPartner(v)`：先看主动半、再看被动半，双侧校验；
- 选目标的入口在 `src\train_cmd.cpp:13058-13100`（`R3RUpdateCouplePair` 一类），稳态判据为「partner ≠ 自己链 + `R3RIsCoupleTarget(partner)` + `!partner->vehstatus.Test(VehState::Stopped)` + `R3RCoupleAllowedIgnoringPair(coupler, partner)`」，扫描候选时同样只认 `t->First()==t`、`R3RIsCoupleTarget`、非 Stopped，再按距离取最近。

⇒ **玩家 C-278-3 的前半段（"只配对一个等挂 + 一个主动挂"）已经实现**，本轮不需要重做；需要做的是**后半段 D-6b**（配对成功前 GOTO_COUPLE 的运行期效果 = `OT_WAITING`）。同时注意现场嫌疑：既然配对锁存在，`why=GROUP` 又发生在**已锁对象**身上，则 `R3RCouplePairMatches` 通过、而**目的地/公司闸门**不通过的可能性最大（即锁上了一个命令不允许的候选 —— 选目标时用的 `R3RCoupleAllowedIgnoringPair` 与 `CPL-GATE` 的到点闸门可能对"目的地"用了不同的判定来源）。

#### 11.12.5 下一步取证（一次到位）

对现场日志取三组行：`CPL-PAIR`（配对锁建立/解除）、`CG-GATE`（分组掩码实测值）、`COUPLE-WAIT-OFFDEST`/`destStn`（命令目的地）。判读口径：
- 若出现 `CPL-PAIR ... tgt=24` ⇒ 配对锁成立 ⇒ 排除配对问题，锁定目的地/公司闸门；
- 若 `CG-GATE` 里 `cmask=0x0 tmask=0x0 compatible=0` ⇒ 走的是**跨公司**分支（乙）；
- 若全程无 `CG-GATE`（同公司分支只在值变化时打，`compatible=1` 首见也会打一行）而 `why=GROUP` 仍在 ⇒ 命中**目的地**闸门（甲）。

#### 11.12.6 取证结论（K-278-5，本轮最重的一条）：`why=GROUP` 的真身是「**配对锁从未建立**」

现场日志（1 455 行）对四类关键词的**完整**命中：

| 关键词 | 命中数 | 内容 |
|---|---|---|
| `CPL-PAIR` | **0** | 配对锁的建立/解除，一次都没有 |
| `CG-GATE` | **0** | 分组掩码比较从未执行到 |
| `CPL-GATE` | **0** | 到点闸门从未执行到 |
| `COUPLE-WAIT-OFFDEST` | 2 | `:1296` / `:1343`，内容逐字相同：`veh=21 tile=253,233 mf=259,233 real=12 isDepot=0 destDepot=0 destStn=12 destTile=491798 onDepotTile=0 onStnTile=0` |

`destTile=491798` 在 `MapSizeX=2048` 下解为 `(278, 240)`（与 `DEPOT-ARR ... destTx=278 destTy=240` 互证）；而 YAPF 扫到的车底 `veh=24` 在 `(278, 234)` —— **同一车站台两端，相距 6 格**。⇒ **目的地并不匹配不上**，甲（目的地不匹配）的嫌疑下降；乙（跨公司）也未被 `CG-GATE` 证实（因为压根没走到那里）。

**真正的机制（代码链已闭合，无需再猜）**：

`R3RCoupleAllowed()`（`couple_group.cpp:597-608`）的第一层就是配对锁：

```cpp
if (!R3RCouplePairMatches(coupler, target)) return false;
```

`R3RCouplePairMatches()`（`:528-535`）要求 `c->r3r_couple_target == t->index && t->r3r_couple_requester == c->index`（**双向互指**）。现场 `CPL-PAIR` 为 0 ⇒ 锁不存在 ⇒ **`R3RCouplePairMatches(21, 24)` 恒为 false** ⇒ `R3RCoupleAllowed` 恒 false ⇒ `yapf_destrail.hpp:606-608` 打 `r3r_dest_reject(0, ...)` ⇒ **日志显示 `why=GROUP`**。

⇒ **「双向无分组」与「为什么被拒」在本现场并不矛盾**：它根本没走到分组判据。`why=GROUP` 这个标签在 `R3RCoupleAllowed` 有两层、`IgnoringPair` 内部又有三道闸门的情况下**完全失去区分力**，这是 R278-Q 的根源，也是第 236 轮 KI-344 census 设计时没预料到的（当时只按 `IgnoringPair` 的三道闸门编号）。

**由此推翻/修正我上一轮的两处判断**：

1. **D-2 的改动对现场无效**：我把 `R3RCoupleAllowedIgnoringPair` 加进 `pair_ready`，但 YAPF 侧在**更外层**就被 `R3RCouplePairMatches` 挡住 ⇒ 加不加都是 `found=0`。D-2 保留无害（双方无分组时返回 true），但**不能算作 R278-2 的修复**，必须在 KI-450 里注明。
2. **R278-2 的因果链需要改写成**：`配对锁未建立` → YAPF 恒 `found=0`（与分组无关）→ `FreeTrainTrackReservation()` 清预留 → 车在关键格失去预留 → `STUCK` → 253/254/255 乱走。而"放行"（`pair_ready` 静默置 0）只是**让列车更早开始这次注定失败的寻路**，是**加速器**而不是根因。

**下一个必须回答的问题（E-278-5）**：`train_cmd.cpp:13058` 那段配对维护（稳态判据 + 扫描 + `R3RPairCoupleTargets`）**为什么一次都没执行到**？候选：①它只在 `TrainCoupleHandler` 的某个分支里被调用，而本现场没进那个分支；②`R3RIsCoupleTarget(veh=24)` 为假（24 的订单不是 `OT_WAIT_COUPLE`？）；③`veh=24` 处于 `VehState::Stopped` 被 `:13098` 跳过；④扫描节流 `R3R_PAIR_RESCAN_TICKS=8` 叠加 `_r3r_pair_scan_tick` 让它在有限窗口内一次没到期。

### 11.12.7 E-278-5 定案（2026-10-23 取证，结论句）

**取证读数（`build\R3R_debug.log`，1441 行）**

- 探针计数：`CPL-PAIR=0`、`CG-GATE=0`、`CPL-GATE=0`、`CPL-HOLD-NOBOOK=0`、`WBAR-*=0`、`CPL-ENTRY=3`、`CPL-WAITCUT=2`、`CPL-SKIP=1`、`UNRES=3`。
- `:118-131` `R3RDUMP-CHAIN head=24 ... SEGF=1 n=24 ... curReal=10 coType=1 type=1`，其订单表 `i=11 type=17`（WAIT_COUPLE）、`i=16 type=15`（DECOUPLE）⇒ **24 的等待命令在 real=11，尚未执行**。
- `:1264` `CPL-WAITCUT veh=21 state=0 wp=17 tile=253,233`；`:1268` `CPL-SCAN ... last=278,234 lastveh=24 why=GROUP` ⇒ `CPL-PATHFOUND found=0`。
- `:1290` `COUPLE-WAIT-OFFDEST ... real=12 destStn=12 destTile=491798`；`:1292` `STUCK veh=21`；`:1311` `CPL-SKIP retryIn=7`；`:1330` `TRP ... ok=1 res=1`。
- `:1419` `TRP veh=24 mf=270,234 real=11 type=17 dest=4294967295 spd=0 ... => ok=0 res=0` ＋ `RESV-NOBOOK veh=24 ... order=17 caronly=0 reason=parked-waiter` ⇒ **veh=24 到日志末尾（1419/1441）才变成合法等待者**。
- `:74` `R3RDUMP idx=21 ... DB=1 SEGF=1`、`:104` `R3RDUMP-CHAIN head=21 ... n=24`；`:1290` 前后 `TRP veh=21 mf=259,233` 而 `veh=21 tile=253,233` ⇒ 车链横跨路点、运动前端已在东侧。

**判定 1（E-278-5 结论）**：配对锁不是被配对逻辑拒绝，而是**被判据时机挡在门外**——`R3RIsCoupleTarget()` 要求目标**当前**订单就是 `OT_WAIT_COUPLE`；失败寻路那一瞬 `veh=24` 还在跑 `GOTO_STATION`（real=10）⇒ 扫描静默 `continue`，`CPL-PAIR` 一次都不打。等它推进到 real=11 时日志只剩 22 行。**由此推翻上一节的候选②之外的三个候选**：调用点进得去（否则 `CPL-ENTRY` 不会 3 次）、24 未被 `Stopped` 跳过（`RESV-NOBOOK` 证明它在跑停驶分支）、节流 8 tick 在 3000+ tick 里必然到期。同时推翻我自己「veh=24 静停等待」的假设（其 `SPDD` 行为 `order=1` 的正常行车日志）。

**判定 2（真正的病灶，D-6b 的依据）**

1. `R3RShouldCutCoupleAtWaitPoint()` `:13365` `if (bs.overshoot) return 0;` —— 运动前端一旦离开路点格，**本命令内这堵墙永久失效**；现场 `state=0` 正是此分支，于是不再截断，直接朝目标铺全路。
2. `R3RShouldHoldForCouple()` `:13209-13212` 要求 `GetMovingFront()->tile` **正在**路点格 ⇒ overshoot 之后恒假；C4-3 门禁 `:16527` 因此**从未生效**（`CPL-HOLD-NOBOOK=0` 实证），兜底彻底失效。
3. 两者叠加 ⇒ 无合法目标时 `found=0` ⇒ `FreeTrainTrackReservation()` ⇒ `STUCK` ⇒ 253/254/255 来回乱走 + 2km/h creep。
4. `R3RCoupleTargetAtOrderStation()` `:12908-12923` **只比目标格站号 vs 订单目的地**，不要求主动方在同站 ⇒ D-6b 的闸门可以放心挡路，不会连带把配对扫描挡死。

**判定 3（D-6a 无需做）**：`couple_group.cpp:528-595` 的双向互指 + steal 已是严格一对一，「一个等挂一个主动挂」这条 C-278-3 的前半已经满足。

**D-6b 落地口径（待实现）**：新增 `R3RShouldActAsWaitingForCouple(v)` ＝ `IsFrontEngine` && `current_order.IsType(OT_GOTO_COUPLE)` && `R3RGetCouplePairPartner(v)==nullptr` && `R3RShouldCutCoupleAtWaitPoint(v,nullptr)==0`（还在朝路点走时不挡）；插在 `TryPathReserveWithResultFlags()` 的 `:16507` 与 `:16527` 之间，命中即 `R3RLogReserveAttempt(..., TPRRF_NONE)` + 探针 `CPL-WAIT-NOBOOK` + `return TPRRF_NONE`（**不** mark_as_stuck、**不**动已有预留）。这样配对成立前 GOTO_COUPLE 的运行期效果 ≡ `OT_WAITING`，配对锁一建立自动放行。

**顺带（低）**：`yapf_destrail.hpp:606` 单一 `r3r_dest_reject(0,...)` 把「配对锁缺失」和「分组不兼容」混印成 `why=GROUP`，须拆 `why=PAIR` / `why=GROUP`，否则后续每轮都会被这条日志误导（本轮已误导过一次）。

### 11.12.8 D-6b 落地（第 283 轮，2026-10-23）

玩家原话：「可以，就这么修吧」⇒ 按 §11.12.7 的 D-6b 实现，**只改 `src\train_cmd.cpp`，未碰任何 `src\*.h`**。

**改动一：新增判据函数**（`src\train_cmd.cpp:16499`，紧跟 `R3RIsParkedWaiting()` 之后）

```cpp
static bool R3RShouldActAsWaitingForCouple(const Train *v)
{
	if (v == nullptr || !v->IsFrontEngine()) return false;
	if (!v->current_order.IsType(OT_GOTO_COUPLE)) return false;
	if (R3RGetCouplePairPartner(v) != nullptr) return false;

	uint32_t wait_wp = 0;
	if (R3RShouldCutCoupleAtWaitPoint(v, &wait_wp) != 0) return false;
	return wait_wp != 0;
}
```

**改动二：门禁**（`TryPathReserveWithResultFlags()` 内，KI-208 waiter 门之后、C4-3 `R3RShouldHoldForCouple()` 门之前，`:16563-16585`）

```cpp
	if (R3RShouldActAsWaitingForCouple(consist)) {
		R3RLogReserveAttempt(consist, mark_as_stuck, first_tile_okay, TPRRF_NONE);
		if (((_tick_counter + consist->index.base()) % 500) == 0) { ... CPL-WAIT-NOBOOK ... }
		return TPRRF_NONE;
	}
```

探针字段：`CPL-WAIT-NOBOOK veh= tile= real= mf= spd=`，500 tick 节流（与 `CPL-HOLD-NOBOOK` 同构，避免每 tick 刷屏）。

**与 §11.12.7 口径的一处收紧（必须记住）**：原方案写的是 `R3RShouldCutCoupleAtWaitPoint(v,nullptr)==0`，落地改为「返回 0 **且** `wait_wp != 0`」。理由：该函数对「普通 GOTO_COUPLE，根本没有等待区路点」也返回 0（`:13296` 一带 `wait_wp` 保持 0 后直接 `return 0`）。若不加 `wait_wp != 0`，就会把**所有**尚未建立配对锁的 GOTO_COUPLE 列车在**任何位置**钉死，等于废掉整条挂接命令。加了这个条件后，返回 0 的组合只剩 `overshoot` 与 `pair_ready` 两种，而 `pair_ready` 已被「伙伴为空」排除 ⇒ 判据精确等价于「越过等待区路点且不再朝它走」。

**刻意不做**：不清既有预留（避免 KI-208 的「先铺后拆」闪烁）；不在门内主动 `MarkTrainAsStuck`（返回 `TPRRF_NONE` 后上层 `CheckNextTrainTile()` 仍会走既有路径，但只在确实尝试过预留时）；不碰 `ReserveTrackUnderConsist()`（车底脚下轨道保护，挂接寻路需要）；不碰 `TrainCoupleHandler()` 与 `R3REnsureCouplePair()`（配对扫描在 `TrainLocoHandler()` 里独立运行，早于预留）。配对锁一建立判据立即失效，车恢复正常挂接。

**构建自证（第 283 轮）**

- 复用既有 `_tmp_inc_build.cmd`（未新建任何 `.cmd`）；构建前 `Get-Process openttd` 无进程占用。
- 第一次构建（17:28:50 的 exe）为注释轮次笔误（写成"第 280 轮"）前的版本；订正注释后重跑一次，最终产物以第二次为准。
- 第二次：`build\R3R_incbuild.done` = `EXIT_CODE=0`（17:30:09）；日志尾 `[2/3] Building CXX object CMakeFiles\openttd_lib.dir\src\train_cmd.cpp.obj` → `[3/3] Linking CXX executable openttd.exe`；`error C|fatal error|FAILED:|build stopped` 计数 **0**。
- 时间戳：`src\train_cmd.cpp` 17:29:41 → `train_cmd.cpp.obj` 17:30:05 → `build\openttd.exe` 17:30:07（52 272 640 B）⇒ obj 晚于源码、exe 晚于两者。
- `read_lints`：`src\train_cmd.cpp` 0 条。
- 产物自证：`build\openttd.exe` 内可检索到 `CPL-WAIT-NOBOOK`。



