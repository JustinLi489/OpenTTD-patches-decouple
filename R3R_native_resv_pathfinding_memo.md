# 原生（JGRPP/OpenTTD PBS）轨道预留与列车寻路机制解剖

**日期**：2026-10-16
**轮次**：第 243 轮续五十二（玩家提问："原生 openttd 的预留和寻路是怎么样的？列车是铺出一个预留，然后不管预留、直接按照自己踩着的预留走吗？"）
**性质**：机制调研（零源码改动、零构建）。全部结论均带源码行号锚点，可直接复核。
**关联**：`R3R_redsig_1112_129_round243_memo.md` §51（根源定性）；KI 条目 **REG-243-W**。

---

## 一、结论摘要（一句话）

原生机制把"预留"拆成**三个彼此不对称的动作**，而且**释放用的是两套不同的坐标系**：

| 动作 | 索引依据 | 原子性 |
|---|---|---|
| **订** | YAPF 算出的**一条完整路径** | **全有或全无**（任一格失败 ⇒ 整条回滚） |
| **走** | **轨道几何 + 信号 + 速度**（预留**不参与**决策） | — |
| **放-脚下** | **车节自己的 tile + trackdir** | 逐格 |
| **放-前方** | **地图上的预留位图**（顺着有位的格子往前爬） | 顺位走到爬不动为止 |

⇒ 玩家的直觉"**铺一个预留，然后不管它，踩着自己的预留走**"——**前半句对（订完确实基本不管），后半句只对一半**：列车踩的是**铁轨**，预留只决定这条铁轨在**信号意义上属于谁**；而释放时，"脚下那几格"确实是踩着自己订的位清掉的（这一半符合直觉），但"前方那条旧路径"**不是按列车轨迹清的，而是按地图上的位图清的**。这个不对称就是原生层的结构性弱点（详见 §六）。

---

## 二、预留存在哪里（没有"归属"字段）

预留**只存在于地图 tile 的位里**，没有任何"这格归谁"的字段：

- plain rail：`_m[t].m2` 的 bit 8-11（`SetTrackReservation` / `GetRailReservationTrackBits` / `HasReservedTracks`，`rail_map.h`）。
- 站台：整台一个 bit（`station_map.h` 的 `SetRailStationPlatformReservation`）。
- depot / 道口 / 隧道 / 桥：各自一个 bit，**语义是"有车在里面"，不可细分到轨道**。
- **列车自己不保存"我订了哪条路径"**。唯一的派生快照是 `consist->lookahead`（`TrainReservationLookAhead`），那是**刹车前瞻表**（距离/事件/信号），不是预留本体，且清预留时会 `lookahead.reset()`。
- 唯一的"归属"查询是**只读推断**：`GetTrainForReservation()`（`pbs.cpp:2162`）/ `GetTrainForWholeTileReservation()` —— 从位**回溯**到某辆车的路径，**不是真值**。

---

## 三、订（reserve）：一次性、整条、原子

**入口**：`Train::TryPathReserve()`（R3R 包装体 `R3RTryPathReserveCore`，`train_cmd.cpp:14289`）

1. **前提检查**（:14289 起）：是否在 depot（`track & TRACK_BIT_DEPOT`）、是否在 wormhole、`force_proceed` 等；不满足直接返回。
2. **交给 YAPF**：`ChooseTrainTrack(...)`（`train_cmd.cpp:13824`）→ `yapf_rail.cpp` 的 `YapfTrainChooseTrack`，在"到下一个安全点 / 信号 / 站台 / 等待点"的范围内找**一条**路径。
3. **逐格订位**：YAPF 的节点回调里每走一格就尝试订（`TryReserveRailTrack` / `TryReserveRailTrackdir`，`pbs.cpp:747` 等）。**订不上（被别人占了）⇒ 该节点不可行**。
4. **整条回滚**：寻路失败或后续校验失败时调 `FreeTrainTrackReservation(consist, origin.tile, origin.trackdir)`，调用点 `:14034 / :14069 / :14089 / :14135 / :14145`。⇒ **要么整条订下，要么一条都不留**。
5. **副作用**：订位同时把沿途 **PBS 信号置绿**、**道口置占用**、**站台置整台预留**；`lookahead` 表随之生成（供 realistic braking 减速/限速）。

**重新订（"不够长"再往前接一段）**：`IsReservationLookAheadLongEnough()`（`:13583`）判断预留长度是否够刹车；不够则 `TryLongReserveChooseTrainTrack`（`:13706`）从预留末端继续往前订。

---

## 四、走（drive）：列车**不查预留**

`Train::TrainController()` 的移动逻辑只看：**轨道几何**（`chosen_track` / `chosen_dir`，由 `v->track` + 出站方向算出）、**信号状态**、**速度与刹车曲线**。

预留只出现在两处"判断/副作用"里，**不参与"往哪走"的决策**：
- **信号颜色**：订位时置绿；列车**进入**新格时把它刚离开那格的 PBS 信号置红（`:16623-16626`）。
- **占位/安全判断**：`FollowTrainReservation()` / `GetTrainForReservation()` / `CheckTrainCollision` / 道口占用 —— 回答"我前方这条路有没有别的车、能不能进"。

⇒ 所以"**踩着自己的预留走**"这个说法只是**比喻**：列车踩的是铁轨；预留保证的是"这条铁轨此刻在信号层属于我"。

---

## 五、放（release）：**两条路径、两套坐标系**

### (A) 脚下逐格清 —— "踩着自己预留"的那一半（车 → 位）

`train_cmd.cpp:16628-16629`（位于 `TrainController()` 的"进入新 tile"代码块内）：

```cpp
/* Clear any track reservation when the last vehicle leaves the tile */
if (v->GetMovingNext() == nullptr && !(v->track & TRACK_BIT_WORMHOLE))
    ClearPathReservation(v, v->tile, v->GetVehicleTrackdir(), true);
```

- 触发条件：`GetMovingNext() == nullptr` ⇒ **moving 意义上的末节车**（整列车都离开该格才清）。
- 索引依据：**该车节此刻的 `tile` + `GetVehicleTrackdir()`**。
- ⇒ **走一格、清一格**，**用车的坐标做索引**。这就是上游注释里说的 "their bits were **consumed as it drove**"。

`ClearPathReservation()`（定义 `:12904`）按格类型分派：桥隧 `UnreserveBridgeTunnelTile`、站台"整台"释放（`:12959-12966`，仅当下一个格不属于同一站台时）、其它 `UnreserveRailTrack(tile, TrackdirToTrack(track_dir))`（`:12969`）。

### (B) 前方整条清 —— `FreeTrainTrackReservation()`（位 → 位）

定义 `train_cmd.cpp:12979`，核心循环（`:13070` 一带，`CFollowTrackRail`）：

```cpp
CFollowTrackRail ft(consist, consist->GetIndirectCompatibleRailTypes());
while (ft.Follow(tile, td)) {
    tile = ft.new_tile;
    TrackdirBits bits = ft.new_td_bits & TrackBitsToTrackdirBits(GetReservedTrackbits(tile));
    td = RemoveFirstTrackdir(&bits);
    ...
    ClearPathReservation(consist, tile, td);
}
```

**循环条件不是"列车走到哪"，而是"地图上下一格还有没有预留位可以顺着走"**。附带两条守卫：
- 起点保护 `:13030`：`if (TracksOverlap(GetReservedTrackbits(tile) | TrackToTrackBits(TrackdirToTrack(td)))) return;` —— **"不是我的位就别放"**。
- `:13055` 遇普通信号：释放后停；`:13061` 遇 PBS 红灯：停（"红灯不可能是我的预留"）。

⇒ **这条路用"地图位图"做索引，不是车的坐标。**

### 其余释放入口（供完整性）
- `ClearReservationUnderConsist()`（`:15396`）：先放 R3R 记账里"车已离开的旧格"，再逐节按 `u->tile + GetVehicleTrackdir()` 清脚下（`:15411-15414`）。
- `Train::Crash()`（`:15423`）：`FreeTrainTrackReservation` + 逐节清脚下（`:15431-15434`）。
- 车库编辑 / 卖车 / 链编辑后的清扫（R3R 各提交点 `R3RReleaseChainReservations()`）。

---

## 六、结构性弱点：两条释放路径**不等价**

- (A) 与 (B) **坐标系不同**：(A) 是 **车 → 位**（走一格清一格）；(B) 是 **位 → 位**（顺着位图爬）。
- (B) 隐含一个**致命假设**：**"顺着地图上的预留位往前爬出来的那条路" == "我当初订下的那条路径"**。
- 该假设在"车顺顺当当沿着自己订的路走完"的正常情形下**恰好成立**，所以原生机制长期看不出问题 —— **它根本没有"归属"概念，也不需要**。
- 但它在两类情形下**必然破裂**：
  1. **车已碾过的位被 (A) 消费掉** ⇒ (B) 的 forward-walk 在**第一个没有预留位的格子上当场死掉**，于是**旧路径远端**的位永远走不到（ghost reservation）。`train_cmd.cpp:10515` 附近的注释**亲口承认**这一点："it walks *forward* ... and gives up at the first tile that carries no reservation ... because the loco has just rolled over the tiles in front of it (**their bits were consumed as it drove**), while the *far* end of its old path" 再也够不到。
  2. **链编辑**（耦合/解挂/翻转/车库拖动）改变**链头身份与链序** ⇒ (A) 清的是"**新**车节的坐标"，(B) 爬的是"**旧**位图" ⇒ 二者错位。
- ⇒ **这正是 R3R"地鼠"在原生层的伏笔**（承接 §51）：R3R 一旦引入链编辑，就不断制造"车与路径关系被改写"的时刻，两条释放路径的不等价性于是被反复引爆：漏掉的位既**不在车轮下**（(A) 够不到），又**在走查里"有主"**（活着的订位者仍在，(B)/(B′) 都不动它）。

---

## 七、对"读档后老旧陈位批量清理"的口径意义

- 不能用 (B) 的假设去清存量（它假定位图自洽）。
- 也不能用"走查有主即保护"（对 KI-369 形态永久失效，见 §50/§51）。
- **可行口径不变**：读档后按"**全体列车当前路径并集**"重建一次账本（§48.5），**不在并集内、且无账本行的位一律清**；此后只在命令/提交点增量维护。
  - 原理上等价于：**放弃"位图自洽"假设，改用"活车路径并集"作为唯一的"应然位图"**——把 (B) 的索引从"地图位图"换成"车路径并集"。

---

## 八、待验证项 / 未确认

- **U-9（低）**：`ClearPathReservation` 的 `tunbridge_clear_unsignaled_other_end` 参数在 (A)/(B) 两处的传值差异（(A) 传 `true`，(B) 默认 `false`）是否对应"无信号桥隧对端也要清"的语义 —— 本轮未深挖，与地鼠无直接关系。
- **U-10（低）**：站台"整台释放"（`:12959-12966`）依赖"下一个格是否同一站台"，在 R3R 链编辑改变出站方向时的行为未取数（与 KI-207c/第 126/127 轮的路点整格问题同族）。
- **(B) 循环上界**：`while (ft.Follow(...))` 依赖位图单调向前（每格 `RemoveFirstTrackdir` 后 `bits` 若不空会怎样）—— 本轮未逐行核验其终止性证明，仅确认它不依赖车的坐标。

---

## 九、直接回答玩家

1. **"铺出一个预留"** —— 对：一次性、整条、原子（YAPF 算路径，逐格订，失败整条撤）。
2. **"然后不管预留"** —— 基本对：订完之后列车**不再读**预留来决定怎么走；预留只影响信号颜色和"前方有没有车"的判断。
3. **"直接按照自己踩着的预留走"** —— **不对**：列车踩的是**铁轨**，不是"预留"；预留是附在铁轨上的一个**信号意义上的占位**。
4. **"走的时候把踩过的预留消费掉"** —— **对，而且是机制的正式一环**（`:16628-16629` 逐格 `ClearPathReservation`），注释原话 "consumed as it drove"。
5. **但"消费"只覆盖脚下**；**前方那条旧路径的清理另有一套算法，而且那套算法是"沿着地图上的位图爬"**（`FreeTrainTrackReservation`）。**两套坐标系的不一致，就是原生层给 R3R 留下的结构性缺口。**

---

## 十、列车行进到底看什么：信号 vs 预留｜遇到"非 PBS 信号"会怎样（第 243 轮续五十三，2026-10-16）

**玩家追问原话**：「列车行进根本不是看预留的，而是看前方的信号灯的？那如果遇到了非 PBS 信号会怎么样呢」

**待取证清单（本轮先落盘，后取证）**：
- [ ] Q1 `Train::TrainController()` / `UpdateSpeed()` 里决定"能不能往前走"的判据到底是什么（信号状态？预留？）；
- [ ] Q2 PBS 信号与非 PBS（普通 block）信号在**列车侧**的判据差异（`IsPbsSignal` / `HasPbsSignalOnTrackdir` / `SIGTYPE_PBS` 的落点）；
- [ ] Q3 遇到**非 PBS 信号**且为红灯时，列车是否**必须停车**（预留能否把它变绿）；
- [ ] Q4 遇到**非 PBS 信号且为绿灯**时，预留是否会**跨过**它（订位边界）；
- [ ] Q5 预留语义与信号语义的**接缝**：PBS 信号"绿色"是不是只由预留驱动（`SetSignalStateByTrackdir` 的调用点）。

**（结论见 §十一）**

---

## 十一、结论：列车只看"下一格对我方向红不红"，红绿的**来源**才是 PBS/非 PBS 的分水岭

### 11.1 Q1 先答案：列车侧**完全不区分** PBS / 非 PBS

`TrainCheckIfLineEnds()`（`train_cmd.cpp:17343`）：

```cpp
/* Determine the track status on the next tile */
TrackStatus ts = GetTileTrackStatus(tile, TRANSPORT_RAIL, RoadTramType::Invalid, ReverseDiagDir(dir));  // :17362
...
TrackdirBits red_signals = ts.signals & reachable_trackdirs;                                           // :17366
...
/* approaching red signal */
if ((trackdirbits & red_signals) != 0) return TrainApproachingLineEnd(moving_front, true, reverse);     // :17382
```

- 判据只有一条：**下一格"对我这个方向是否红"**（`ts.signals & reachable_trackdirs`）。
- 这段代码里**没有 `IsPbsSignal`**。列车**不知道**前方信号是 PBS 还是普通，它只知道**红/绿**。
- ⇒ 所以"列车行进看信号"这个说法**完全正确**；"看预留"是错的（预留只在**颜色怎么来的**这一层起作用）。

### 11.2 红/绿的**来源**不同 —— 这才是 PBS 与非 PBS 的真正分野

| | 颜色由什么驱动 | 锚点 |
|---|---|---|
| **PBS 信号** | **预留** | 订位成功 ⇒ `SetSignalStateByTrackdir(..., Green)`（`train_cmd.cpp:13874`）；订位失败 ⇒ `...Red`（`:13911`）；列车**进入**该格后把它置回红（`:16623-16625`） |
| **普通信号** | **信号段占用**（`SigSegState`） | `signal_func.h:143-147`：`Free`（空闲/有绿出口）、`Full`（被车占用）、`Path`（**PBS 段**） |

⇒ **同一盏红灯，含义可能完全不同**：
- PBS 红灯 = "**没人给你订下前方这段路**"；
- 普通红灯 = "**段里有别的车**"。
反过来：**PBS 绿灯=有人订了**（就是你自己），**普通绿灯=段里没车**（谁都能进，无保护）。

### 11.3 Q3/Q4：遇到**非 PBS 信号**会怎样 —— 三种情形

**（a）它是红灯 ⇒ 必须停。预留救不了你。**
普通信号的颜色**不吃预留**（由段占用算），所以无论你订没订位、预留多远，红灯就是红灯 ⇒ `TrainApproachingLineEnd`（`:17382`）⇒ 停车等待。你要做的是**等段里的车走掉**，不是重新订位。

**（b）它是绿灯 ⇒ 直接通过，但"没有预留"地通过。**
普通信号只在**段内无车**时才绿 ⇒ 你穿过去时**天然安全**，但你的**预留并没有跨过这盏灯**（见 11.4），所以你此刻是"裸奔"的。

**（c）绿灯通过之后，如果灯**后面**的区段是 PBS 区段 ⇒ 立刻补订，订不上就判卡住。**
这是引擎里一处**显式命名的状态**，代码注释几乎就是本问题的答案（`train_cmd.cpp:14858-14891` 定义 + `:16893-16908` 处理）：

```cpp
enum TrainMovedChangeSignalEnum {
    CHANGED_NOTHING,
    CHANGED_NORMAL_TO_PBS_BLOCK, ///< A PBS block with a non-PBS signal facing us
    CHANGED_LR_PBS               ///< A long reserve PBS signal
};
...
if (... UpdateSignalsOnSegment(tile, TrackdirToExitdir(trackdir), GetTileOwner(tile)) == SigSegState::Path
        && HasSignalOnTrackdir(tile, trackdir)) {
    /* A PBS block with a non-PBS signal facing us? */
    if (!IsPbsSignal(GetSignalType(tile, TrackdirToTrack(trackdir)))) return CHANGED_NORMAL_TO_PBS_BLOCK;   // :14872-14874
}
```

```cpp
case CHANGED_NORMAL_TO_PBS_BLOCK:
    /* We are entering a block with PBS signals right now, but
    * not through a PBS signal. This means we don't have a
    * reservation right now. ... */
    if ((!HasReservedTracks(gp.new_tile, v->track) &&
            !TryReserveRailTrack(gp.new_tile, FindFirstTrack(v->track))) ||
            !TryPathReserve(first)) {
        MarkTrainAsStuck(first);          // :16893-16908
    }
    break;
```

⇒ 即：**"穿普通信号进 PBS 区段"被引擎当作一个需要现场补票的事件**（先订脚下那一格，再订前方路径）；补不上 ⇒ `MarkTrainAsStuck`。注释还说明它为什么几乎总能补上："普通信号只有在段内无车时才绿，所以拿到路总是可能的"——补不上只可能是玩家建了奇怪的路网。

### 11.4 预留**不跨**普通信号（三条独立证据）

1. **释放走查遇普通信号即停**（`train_cmd.cpp:13055-13058`）：
   ```cpp
   if (HasSignalOnTrackdir(tile, td) && !IsPbsSignal(GetSignalType(tile, TrackdirToTrack(td)))) {
       /* Conventional signal along trackdir: remove reservation and stop. */
       UnreserveRailTrack(tile, TrackdirToTrack(td));
       break;
   }
   ```
2. **寻路把普通信号当"段终点/安全停车位"**（`yapf_costrail.hpp:761-766`）：
   ```cpp
   } else if (TrackFollower::DoTrackMasking() && cur.tile_type == TileType::Railway) {
       if (HasSignalOnTrackdir(cur.tile, cur.td) && !IsPbsSignal(GetSignalType(cur.tile, TrackdirToTrack(cur.td)))) {
           end_segment_reason.Set(EndSegmentReason::SafeTile);
       }
   }
   ```
3. **普通信号之后，别人的预留根本不进入寻路代价**（`yapf_costrail.hpp:192-195`）：
   ```cpp
   inline int ReservationCost(Node &n, TileIndex tile, Trackdir trackdir, int skipped)
   {
       if (n.num_signals_passed >= this->sig_look_ahead_costs.size() / 2) return 0;
       if (!IsPbsSignal(n.last_signal_type)) return 0;      // ← 非 PBS 信号之后，预留代价恒为 0
   ```
   ⇒ **"预留挡路"这件事在普通信号之后不存在**。同理，非 PBS 的双向红灯在寻路里直接判**死路剪枝**（`:436-442`，`TreatFirstRedTwoWaySignalAsEOL`）。

### 11.5 一句话总纲（可直接用于后续设计）

> **列车只认"下一格红不红"（`TrainCheckIfLineEnds`）。**
> **PBS = 用预留把前方的灯点亮成绿，从而"看不到红"；普通信号 = 灯的颜色由段占用决定，预留对它无效。**
> **所以预留的作用范围严格被 PBS 信号包住：PBS 信号是预留的"闸门"，普通信号是预留的"墙"（订不过去、也释放不到对面）。**

**对 R3R 的含义**：R3R 的"预留账本"只对 **PBS 区段内**的位有意义；跨普通信号的位**本来就不该存在**。这条也解释了为什么 R3R 的审计/清理要按"活车路径并集"来判断（§十一.5 与 §51.6 丙一致）：因为普通信号本身就是天然的可信边界。

**状态**：本轮为机制调研（零源码改动、零构建）。**新增一个疑点**：`Q5` 中"PBS 绿灯由预留驱动"已证（`:13874`），但**普通信号在被预留的情况下是否可能被误置绿**未查（若某处对非 PBS 信号也调 `SetSignalStateByTrackdir(..., Green)` 即为缺陷）→ 登记 **U-11**（低优先，仅在被证明存在误置时升级）。

---

## 十二、"区间信号是预留的路障、绿了就照原路铺"这个理解对不对（第 243 轮续五十四，2026-10-16）

**玩家原话**：「区间信号只是预留的一个路障，路障一判定为绿灯，预留还是按照之前预定好的路径进行铺设？」

**我的初步判断（待取证，先落盘）**：**"路障"这个比喻不对**。区间信号不是"挡住、等绿灯再继续铺"，而是**寻路的段终点 / 剪枝点**（`yapf_costrail.hpp:761-766` 的 `SafeTile`、`:436-442` 的双向红灯死路）。且**订位是一次性原子的**（§48 已证）：订的那一刻信号必须已经是绿/可进，**不存在"先订一半、等绿灯再续铺"的动作**。

**待取证清单**：
- [ ] Q6 JGRPP 是否有 `PBSTraversalMode`（`None/Signal/SignalExit/End`？）——若有，它才是"预留能否跨过普通信号/跨多远"的**真正开关**；
- [ ] Q7 `ChooseTrainTrack()` 的签名与各调用点分别传哪个遍历模式（`TryPathReserve` 用哪个？`ChooseTrainTrack` 单轨快速返回用哪个？）；
- [ ] Q8 遍历模式在寻路中如何改变"是否把普通信号当作可穿越点/段终点/剪枝点"；
- [ ] Q9 若"绿灯时可跨过普通信号继续订"，那**订到的远端格子**在信号变红（别的车进来）时如何处理（是否就是 §51 地鼠的来源之一）；
- [ ] Q10 **正确的比喻**该是什么（段终点/边界 vs 路障）。

**（结论见 §十三）**

---

## 十三、结论：区间信号不是"路障"，是**计价终点**（第 243 轮续五十四，2026-10-16）

### 13.0 先纠正我自己的一个错记

JGRPP / 本工作区**没有** `PBSTraversalMode`（全库 0 命中）。我原本以为是它控制"预留能否跨信号"，**错**。真正的开关是下面这套。

### 13.1 Q6/Q7：决定"要不要订位"的开关

- `train_cmd.cpp:13831`：`bool do_track_reservation = _settings_game.pf.reserve_paths || (flags & CTTF_FORCE_RES);`
- **单轨快速路径**（`:13846-13883`）：**只有 `HasPbsSignalOnTrackdir`（PBS 信号）**才会强制 `do_track_reservation = true` 并把该信号置绿（`:13872-13874`）；否则 `else if (!do_track_reservation) return { track, result_flags };`（`:13879-13881`）—— **不需要订位就直接走，不产生任何预留**。
- **真正"铺预留"的函数是 `ExtendTrainReservation()`（`:13135`）**，调用点 `:13907`（在 `if (do_track_reservation)` 内），返回 `PBSTileInfo`（含 `okay` 标志）。
- `ChooseTrainTrackFlags`（`:92-98`）/ `ChooseTrainTrackLookAheadState`（`:79-89`）只管"强制订位 / 标 stuck / 前瞻校验"，**没有一个位是"允许跨过普通信号"**。

### 13.2 Q8（核心）：预留"铺到哪为止"——循环体里**没有红绿判断，也没有信号类型判断**

`ExtendTrainReservation()` 主体（`:13167-13267`）：

```cpp
while (ft.Follow(tile, cur_td)) {                       // :13167 沿【轨道几何】往前走（不是沿位图）
    if (KillFirstBit(ft.new_td_bits) == TRACKDIR_BIT_NONE) {
        if (HasOnewaySignalBlockingTrackdir(...)) { r3r_backout_reason = "oneway-signal"; break; }   // :13170
    }
    ...
    bool target_seen = ft.is_station || (IsTileType(ft.new_tile, TileType::Railway) && !IsPlainRail(ft.new_tile));  // :13179
    if (target_seen || KillFirstBit(ft.new_td_bits) != TRACKDIR_BIT_NONE) {   // 岔路 或 站台/车库/路点
        if (HasReservedTracks(ft.new_tile, ...)) { r3r_backout_reason = "choice-reserved"; break; }   // :13187/:13191
        ...
        return PBSTileInfo(ft.new_tile, ft.old_td, false);      // :13254 不 okay：剩下交给 YAPF
    }
    tile = ft.new_tile; cur_td = FindFirstTrackdir(ft.new_td_bits);
    if (IsSafeWaitingPosition(v, tile, cur_td, true, ...)) {                    // :13260
        ...
        if (!(wp_free && TryReserveRailTrackdir(v, tile, cur_td))) { r3r_backout_reason = ...; break; }   // :13263
        return PBSTileInfo(tile, cur_td, true);                // :13266 okay：安全停车位，订到此为止
    }
    ...
```

**停止/返回只有四类原因**：
① `oneway-signal`（单向信号挡路，`:13170`）；② `90deg-turn`（`:13173-13176`）；③ **岔路 / 站台车库路点**（`:13179-13255`，若所需轨已被别人订 ⇒ `choice-reserved` 退出，否则返回"不 okay"让 YAPF 接管）；④ **`IsSafeWaitingPosition` 安全等待位**（`:13260-13267`，订下并返回"okay"）。

⇒ **循环体里没有 `IsPbsSignal`、也没有"当前信号红/绿"的条件判断**。红绿只在 `IsSafeWaitingPosition` 内部起作用（见 13.4）。

### 13.3 Q10：正确的比喻

> **区间信号不是"路障"，而是"计价终点"。**
> **绿灯 ⇒ 这格"不能安全停车" ⇒ 预留不在这里停，继续往远处铺（跨过这盏灯）；**
> **红灯 ⇒ 这格"可以安全停车" ⇒ 预留就在这里收尾。**

所以你的理解要改成：**预留不是"先铺一半、等绿灯再续铺"（没有"等待"这个动作，订位是一次性原子的）**，而是"**订位那一刻一次走到底，停在第一个能安全停车的地方**"。绿灯是**许可**（能订得更远），不是**触发条件**（不会"因为变绿了再去补铺"）。

### 13.4 Q6-long reserve：唯一能"跨过一个本该停下的信号"的特例

`HasLongReservePbsSignalOnTrackdir()`（`:13658-13677`）：**只有当该信号是 PBS 信号（`:13660`）+ routing restriction 带 `TRPAUF_LONG_RESERVE`**时，预留才可以"跨过这个信号继续往前订"。**非 PBS（普通/区间）信号永远拿不到 long reserve**——`:13660` 就要求 `HasPbsSignalOnTrackdir` 成立。隧道桥有并存机制（`CheckLongReservePbsTunnelBridgeOnTrackdir`，`:13679-13702`）。列车侧对应状态是 `CHANGED_LR_PBS`（`:14876`、`:14887`）。

### 13.5 Q9：这解释了地鼠的另一半

`ExtendTrainReservation` 的 R3R 注释自陈（`:13221-13231`）：**"it only ever re-reserves the path it walks now"** —— 预留**只在订位那一刻铺一次，之后不随信号变化重铺**。于是"绿灯时铺到了远处的位"在信号/占用变化后**不会自动收回**，若同时链编辑又改写了车与路径的关系 ⇒ 就是 §51 / REG-243-W 的 ghost reservation。

### 13.6 U-12（本轮唯一待补项）

`IsSafeWaitingPosition` 内部**是否"只有红灯才算安全等待位"**（若绿灯也算，则预留会在每个绿灯处停下，你那句"路障"就部分成立）—— **已取证；结论见 §十四（并更正 §13.3）。**

---

## 十四、更正 §13.3：普通信号是预留的**无条件收尾锚点**，绿灯**不能**让它被跨过（第 243 轮续五十四，2026-10-16）

### 14.1 两处决定性证据

**(1) "Block signal" ≡ "非 PBS 信号"**（`rail_map.h:696-700`）：

```cpp
inline bool HasBlockSignalOnTrackdir(TileIndex tile, Trackdir td)
{
	return IsTileType(tile, TileType::Railway) && HasSignalOnTrackdir(tile, td) &&
		!IsPbsSignal(GetSignalType(tile, TrackdirToTrack(td)));
}
```

**(2) `IsSafeWaitingPosition()` 的第一条判据**（`pbs.cpp:2597-2602`）：

```cpp
bool IsSafeWaitingPosition(const Train *v, TileIndex tile, Trackdir trackdir, bool include_line_end, bool forbid_90deg)
{
	if (IsRailDepotTile(tile)) return true;

	/* For non-pbs signals, stop on the signal tile. */
	if (HasBlockSignalOnTrackdir(tile, trackdir)) return true;
```

⇒ **只要当前格上有"面向我"的非 PBS 信号，这格就是安全等待位 —— 无条件，与红绿无关。**

### 14.2 推论：**推翻 §13.3 的"绿灯 ⇒ 跨过"**

`ExtendTrainReservation()` 走到普通信号格 ⇒ `:13260` 判为安全位 ⇒ `:13263` 尝试订下该格 ⇒ `:13266` `return PBSTileInfo(tile, cur_td, true)` **结束延伸**。⇒ **预留必然在普通信号处收尾。**

**绿灯的真实作用**：只决定"**预留能不能把信号格本身也订进去**"：

| 信号格状态 | 结果 | 锚点 |
|---|---|---|
| 空闲（绿、`IsWaitingPositionFree` 真） | `TryReserveRailTrackdir` 成功 ⇒ 预留**包含**信号格 | `:13263-13266` |
| 已被别人订/占（即"红灯"） | `wp_free` 假 ⇒ `break` ⇒ 预留**退到信号前一格** | `:13263` |

（`break` 之后函数尾部返回的确切形态未取证 → **U-13**。）

**唯一能"跨过信号"的机制，且只对 PBS 开放**（`pbs.cpp:2622-2640`）—— 注意它只在"**下一格是 PBS 信号**"时才有条件判定：
- `SignalType::NoEntry` ⇒ 仍是安全位（`:2627`）；
- `GetSignalAlwaysReserveThrough(...)` ⇒ **不是**安全位（=允许跨过，`:2628`）；
- routing restriction `TRPAUF_RESERVE_THROUGH` 且执行结果 `TRPRF_RESERVE_THROUGH` ⇒ **不是**安全位（`:2629-2636`）；
- 其余 ⇒ 是安全位（`:2639`）。

`train_cmd.cpp:13658-13677` 的 `HasLongReservePbsSignalOnTrackdir()` 同样**以 `HasPbsSignalOnTrackdir` 为前提**（`:13660`）。⇒ **普通（区间）信号永远不可能被跨过。**

### 14.3 最终的比喻

> 区间信号**不是"路障"（挡住→等待→通过）**，而是**"预留的收尾锚点 / 边界"**：预留一定在这里停。
> **绿灯**只决定"这一格算不算在我的预留里"；**红灯**让预留**提前一格**停住。
> 这就是 §11 那句"**普通信号是预留的墙**"的精确含义 —— **是墙，不是门。**

⇒ 你的原话应修正为："区间信号是预留的**边界**（不是路障）；**绿灯不会让预留穿过它**，只决定预留是否覆盖它自身。"

### 14.4 结项与新增

- **U-12 结案**：不是"只有红灯才是安全位"，而是"**普通信号格一律是安全位**"，红灯额外让它订不进去。
- **U-13（新，低）**：`ExtendTrainReservation` 在 `:13263` 因 `wp_free` 假而 `break` 后，函数尾部返回的确切形态（整条延伸失败 ⇒ YAPF 另找 / `MarkTrainAsStuck`？还是返回"走到此为止"让 YAPF 接管）未取证。
- **对 R3R 的意义（强化）**：既然**普通信号必然是预留的自然终点**，"**跨普通信号的预留位**"在正常情况下**根本不该存在** —— R3R 若观察到这类位，就是 §51 地鼠的**强信号**（链编辑改写了"车 ↔ 路径"的对应关系，使预留的收尾点跑偏）。

---

## 十五、"预留停在普通信号前，那后面的路怎么办"（第 243 轮续五十五，2026-10-16）

**玩家原话**：「那我的预留在普通信号灯前面或那一格终止了，那后面的路我怎么铺设预留呢，普通信号不会铺设预留啊」

**初步判断（待取证，先落盘）**：**后面那段路不需要预留**。预留是"**PBS 区段内的通行权**"，不是"整条路线的通行权"。普通（闭塞）信号后面的区段靠**信号本身**互锁（同一时刻只允许一列车），所以"没订也安全"。只有当信号后面**又是 PBS 区段**时，才需要在**列车穿越那一刻现场补订**（`CHANGED_NORMAL_TO_PBS_BLOCK`，`train_cmd.cpp:14858-14891` / `:16893-16908`）。

**待取证清单**：
- [ ] Q1 `SigSegState` 的确切定义（成员名与语义）与 `UpdateSignalsOnSegment()` 的返回值；
- [ ] Q2 `CHANGED_NORMAL_TO_PBS_BLOCK` 的完整判定链与**触发时序**（移动前补订还是移动后补订？）；
- [ ] Q3 补订失败 ⇒ `MarkTrainAsStuck` 的实际后果（原地停？）；
- [ ] Q4 普通信号后面的纯闭塞段，是否真的**无预留参与**（要一条正面证据）；
- [ ] Q5 "预留按 PBS 段分段存在"这一说法是否成立（有无"整条路线连续预留"的反例）；
- [ ] Q6 列车在普通信号前等待（红灯）时是否还有别的订位动作。

**（结论见 §十六）**

---

## 十六、结论：预留是"**按 PBS 段分段存在**"的，后面的路**不需要**预先铺设（第 243 轮续五十五，2026-10-16）

### 16.1 先答你的问题：后面那段路**不需要预留**

预留 ≠ "整条路线的通行权"，而是"**PBS 区段（path segment）内的通行权**"。普通（闭塞）信号后面的区段靠**信号本身的互锁**保护（同一时刻段内只允许一列车），所以**"没订也安全"**。

引擎自己就是这么说的 —— `train_cmd.cpp:16895-16902` 的代码注释（原文）：

> "We are entering a block with PBS signals right now, but not through a PBS signal. **This means we don't have a reservation right now.** As a conventional signal will only ever be green if no other train is in the block, **getting a path should always be possible.**"

⇒ **普通绿灯本身就承诺了"段内无车"**，所以"此刻没有预留"不是漏洞；引擎正是靠这个保证，在列车穿过去的**那一刻当场补票**。

### 16.2 关键机制：穿越普通信号进入 PBS 段 ⇒ **现场补订**

`SigSegState` 定义（`signal_func.h:142-147`）：

```cpp
enum class SigSegState : uint8_t {
	Free, ///< Free and has no pre-signal exits or at least one green exit
	Full, ///< Occupied by a train
	Path, ///< Segment is a path segment      ← PBS 段
};
```

判定与产生（`train_cmd.cpp:14864-14891`，`TrainMovedChangeSignal()`）：

```cpp
if (trackdir != INVALID_TRACKDIR
    && UpdateSignalsOnSegment(tile, TrackdirToExitdir(trackdir), GetTileOwner(tile)) == SigSegState::Path  // 前方段是 PBS 段
    && HasSignalOnTrackdir(tile, trackdir)) {                                                             // 且这格有面向我的信号
	if (!IsPbsSignal(GetSignalType(tile, TrackdirToTrack(trackdir)))) return CHANGED_NORMAL_TO_PBS_BLOCK;  // 而它是【普通】信号
	...
}
```

处理（`train_cmd.cpp:16893-16909`，位于列车**移动过程中**，`gp.new_tile` 即正在进入的新格）：

```cpp
switch (TrainMovedChangeSignal(first, gp.new_tile, enterdir, true)) {
	case CHANGED_NORMAL_TO_PBS_BLOCK:
		/* ... we don't have a reservation right now ... */
		if ((!HasReservedTracks(gp.new_tile, v->track) &&
				!TryReserveRailTrack(gp.new_tile, FindFirstTrack(v->track))) ||   // ① 先订脚下刚进的那一格
				!TryPathReserve(first)) {                                          // ② 再订前方整条路径
			MarkTrainAsStuck(first);                                               // ③ 补不上才算卡住
		}
		break;
```

⇒ **时序是"穿过去的同时补订"**，不是"提前铺"。分两步：先订脚下那格（`TryReserveRailTrack`），再订前方路径（`TryPathReserve`）。隧道桥版本同理（`:14879-14883`）。

### 16.3 完整生命周期

| 步骤 | 发生什么 | 锚点 |
|---|---|---|
| ① 订位 | 只订**本 PBS 段内**，止于第一个安全位（普通信号格 / 红灯前一格 / 岔路） | `ExtendTrainReservation()` `:13135`、`:13260-13266` |
| ② 前进 | 走过脚下的格，预留**边走边释放**；释放遇普通信号**即停** | `:13055-13058` |
| ③ 遇普通红灯 | **停**（信号颜色不吃预留）；预留停在**红灯前一格** | `TrainCheckIfLineEnds` `:17343`；`wp_free` 假 ⇒ `break` `:13263` |
| ④ 灯变绿 | 穿过普通信号（**无预留**，因绿灯已保证段内无车） | `:16895-16902` 注释 |
| ⑤ 穿过瞬间 | 后方若是 **PBS 段** ⇒ `CHANGED_NORMAL_TO_PBS_BLOCK` ⇒ **当场补订**（脚下格 + 前方路径）；后方若是**纯闭塞段** ⇒ **什么都不做** | `:14872-14874` / `:16893-16909` |
| ⑥ | 回到 ① | — |

⇒ **"后面那段路"的预留不是不铺，而是"在你将要进去的那一瞬间才铺"**，并且**只在它确实是 PBS 段时才需要**。

### 16.4 "预留按 PBS 段分段存在"成立（四条独立证据）

1. `ExtendTrainReservation` 收尾于普通信号格（`:13260-13266`，见 §十四）⇒ **不跨**；
2. `ReservationCost()` 开头 `if (!IsPbsSignal(n.last_signal_type)) return 0;`（`yapf_costrail.hpp:192-195`）⇒ 普通信号之后别人的预留**根本不进寻路代价**；
3. 释放走查遇普通信号即 `UnreserveRailTrack` + `break`（`:13055-13058`）⇒ **释放也不跨**；
4. 引擎自陈 **"we don't have a reservation right now"**（`:16896`）⇒ 穿过普通信号**那一刻**确实没有预留。

### 16.5 Q1~Q6 结项

- **Q1 结案**：`SigSegState = Free / Full / Path`（`signal_func.h:142-147`），`Path` 即 PBS 段；`UpdateSignalsOnSegment()` 返回它（`:173`）。
- **Q2 结案**：判定 `:14872-14874`（隧道桥 `:14879-14883`），处理 `:16893-16909`；**时序 = 穿越的同一 tick 内补订**（`gp.new_tile`）。
- **Q3（`MarkTrainAsStuck` 的确切后果）未取证** → 登记 **U-14**（低）。
- **Q4 结案**：纯闭塞段无预留参与（16.4 的证据 2/3/4 + §十一.4）。
- **Q5 结案**：分段成立（16.4 四条）。
- **Q6 未深挖** → 并入 **U-14**。

### 16.6 对你的实际意义

> 你观察到的"预留在普通信号前 / 那一格终止"**完全正确**；但结论不是"后面的路没铺预留会出问题"，而是"**后面的路本来就不归预留管**"。
> 唯一需要补订的时刻是"**穿过普通信号、进入 PBS 段的那一格**"，而这件事由引擎自动完成（`CHANGED_NORMAL_TO_PBS_BLOCK`）。

**对 R3R 的含义（新增一条可查判据）**：既然"穿过普通信号进入 PBS 段"必然产生一次**现场补订**（新预留位），那么在被普通信号分隔的两段 PBS 之间，**正常不该存在跨段的预留位**；若观察到"预留跨越普通信号"，要么是链编辑残留（§51 地鼠），要么是这次现场补订留下的痕迹 —— 两者都值得用探针区分（判据：该位附近是否有一盏非 PBS 信号 + 穿越时刻有 `TryPathReserve` 记录）。

## 十七、"灰色覆盖层连续跨过一串区间信号灯 + 一个路径信号灯，直达目标站台"（第 243 轮续五十六，2026-10-16）

### 17.1 玩家观察（原话）

> "列车为了执行前往挂车命令，一条表示预留或路径的灰色覆盖层连续跨过了很多个区间信号灯和一个路径信号灯，直直的覆盖到自己要挂接的列车所在的站台"

几何（玩家给出）：`列车 - PBS - 一串普通信号灯 - PBS - 复杂的车站咽喉 - 被挂列车`。

### 17.2 与 §十六 的表面冲突（关键分界）

§十六 的结论"**预留按 PBS 段分段存在、普通信号处终止**"是**常规行驶预留**的性质 —— 那条链路是：

`TryPathReserve`（`train.h:105` 的 inline 包装）→ `ExtendTrainReservation()`（`train_cmd.cpp:13135`）→ 收尾于普通信号格（`:13260-13266`）。

而玩家这一格看到的是 **`OT_GOTO_COUPLE`（前往挂车）寻路的预留**，它走的是**另一条代码路径**：R3R 的耦合寻路在回溯时用**自己的 lambda `rp()`** 逐格订位。两条链路在引擎里**互不调用**，所以 §十六 的分段结论**不适用于这一格**。

### 17.3 猜测根因（待验证）

`src/pathfinder/yapf/yapf_rail.cpp` 的回溯 lambda `rp()`（约 `:1020-1124`）在回溯成功的每一格上**按格**调 `TryReserveRailTrack`：

```1036:1039:src/pathfinder/yapf/yapf_rail.cpp
					auto reserve_if_present = [](TileIndex t, Track trk) {
						const TrackBits bits = TrackdirBitsToTrackBits(GetTileTrackdirBits(t, ::TransportType::TRANSPORT_RAIL, 0));
						if ((bits & TrackToTrackBits(trk)) != 0) TryReserveRailTrack(t, trk);
					};
```

`rp()` **没有"PBS 段"概念、没有任何普通信号收尾判定**（对比：引擎的 `ExtendTrainReservation` 收尾处有 `wp_free`/信号判定，`:13260-13266`）。⇒ 若 YAPF 找到的路径本身穿过一串普通信号，`rp()` 就会把整条路径（含跨信号的每一格）**一路订到底**，灰色覆盖层自然连续不断。

**旁证（引擎侧注释自陈）**：`src/pbs.cpp:699-724`（R3R 的 KI-361 注释）已写明耦合寻路是**旁路**引擎段级订位的：

```704:714:src/pbs.cpp
	/* R3R (KI-361, memo round243 §26): the couple pathfinder books a few tiles
	 * Track-level only (its rp() lambda has no signal-setting capability and the
	 * tiles are "not part of any node segment"), so a later Trackdir-level
	 * reservation of the very same bit returns false here even though the bit is
	 * still *ours*. ... */
```

⇒ "couple pathfinder books ... tiles ... not part of any node segment" **直接说明它不按段订**，与 §十六 的段级模型是两条腿。

### 17.4 取证结果

- **U-16 已证（灰色覆盖层 = 预留可视化，不是路径预览）**：开关 `gui.show_track_reservation` **默认 true**（`src/table/settings/gui_settings.ini:1307-1309`，`def = true`），绘制一律用暗色 `PALETTE_CRASH`：站台 `station_cmd.cpp:4148-4151`（有 overlay 分支）与 `:4157-4160`（贴地 sprite 分支）、隧道 `tunnelbridge_cmd.cpp:2354`、桥 `:2530`/`:2734`；配套的 `MarkTileGroundDirtyByTile` 出现在 `pbs.cpp:756`、`train_cmd.cpp:12755`/`:12945`/`:14553`/`:16762`。⇒ 玩家看到的就是**地图上真实存在的预留位**，不是调试路径预览。
- **U-15 主搜索部分取证（倾向"无冲突"）**：入口 `YapfTrainCoupleTrack()`（`yapf_rail.cpp:1764`）→ `CYapfCoupleRail`（`:1479`）＝ `CYapfRail_TypesT<CYapfCoupleRail, CFollowTrackRail, CYapfDestinationTrainRailT, CYapfFollowCoupleRailT>`，其基类是**普通 rail 寻路的 `CYapfRailBase`**（`:1450`，含红灯 `EndSegmentReason` 一族处理），只有"终点类型"被换成列车（`CYapfDestinationTrainRailT`）。⇒ 主搜索仍走同一套信号代价/剪枝；真正"跨信号订位"的是**找完路径之后的第二遍手订**（`reach()` + `rp()`）。剩余待验（低）：couple 变体下红灯剪枝是否与普通寻路逐字相同（未实测）。
- **U-17（低）**：这些跨普通信号的位在列车**实际行进**中如何释放？会不会与 KI-345/KI-346 的"孤儿预留走廊"同族（订了但列车永远走不到 ⇒ 无人释放）。未查。

### 17.5 结论（已定案）

"预留按 PBS 段分段"的准确表述是："**引擎的行驶预留**（`TryPathReserve` → `ExtendTrainReservation()`）按 PBS 段分段"。而 R3R 的**耦合寻路预留**链路是：

`YapfTrainCoupleTrack()` → `FindNearestCoupleTrain()`（`yapf_rail.cpp:805`，YAPF 找路径）→ `reach()`（`:961-996`，用 `TrackFollower` 从起点**沿轨道逐格跟随**到被挂列车）→ `rp()`（`:1020-1124`，**逐格** `TryReserveRailTrack`）。

这条链路**没有 PBS 段概念、没有任何信号收尾判定**，因此"灰色覆盖层连续跨过多盏普通信号直达目标站台"是**设计使然**，与 §十六 不冲突（两条腿互不调用）。

**三个自陈锚点**：
1. `yapf_rail.cpp:807-808`："this is the GOTO_COUPLE pathfinder -- it **books the route the loco drives to the waiting consist**"；
2. `:951-956`："Trace the rail network from the start tile to the consist ... and **reserve the tiles along that path (they are not part of any node segment, so TryReservePath does not cover them)**"；
3. `pbs.cpp:704-714`（KI-361 注释）："the **couple pathfinder books a few tiles Track-level only** ... 'not part of any node segment'"。

### 17.6 对既有判据的修正（重要）

REG-243-X/§十六 里写的"**跨普通信号的预留位 = 地鼠强信号**"**必须加限定**：若该位落在某台**正在执行 `GOTO_COUPLE`** 的机车的耦合路径上（`CPL-ENTRY`/`CPL-TRACE-RESULT`/`RP cur=` 一行能对上），它是**合法**的，不是地鼠。⇒ 地鼠判据回到 §51.6(丙) 的口径：**"无账本行 且 不在任何活车当前路径并集上"** 才是孤儿；单看"跨了普通信号"会误报。

## 十八、GOTO_COUPLE 的灰层越过了车站咽喉前的 PBS 信号，列车却停在信号前不动（第 243 轮续五十七，2026-10-16）

### 18.1 玩家观察（原话）

> "既然这是游戏机制使然，那么我就报告一个真正 bug 的问题：（几何如下：列车-PBS-一串普通信号灯-PBS-复杂的车站咽喉-被挂列车），在车站咽喉前面的那个 PBS，我看到灰色的路径显示越过了它，但是列车却在信号灯前面停下来了"

### 18.2 为什么这是"真 bug"而不是机制使然（关键分界）

§十七 的结论只解释了**灰层为什么连续**（耦合寻路按整条 YAPF 路径逐格手订），但**不解释列车为什么走不动**。在 PBS 体系里这是**两套独立状态**：

| 状态 | 载体 | 由谁设置 |
|---|---|---|
| **灰层**（地图预留） | 轨道位 `rail_map.h` 的 `_m[t].m2` bit 8-11（`SetTrackReservation`） | 任何 `TryReserveRailTrack`（含 R3R 的 `rp()` 手订） |
| **能不能走**（通行权） | 该列车进入信号所保护的 **PBS 段**时能否拿到**段级预留**，并据此驱动信号放行 | `TryPathReserve` / 段级预留 |

⇒ "灰层越过信号 + 车停在信号前"正是这两者**不一致**的现场形态：位订到了，通行权没拿到（或拿到了但车已 0 速、永不再试）。

### 18.3 猜测根因（三条，待验证；可叠加）

- **G-1（高）耦合寻路的手订没有"信号放行能力"**。`pbs.cpp:704-714`（KI-361 注释）自陈 "its **rp() lambda has no signal-setting capability** and the tiles are 'not part of any node segment'" ⇒ `rp()` 铺的灰位**不构成段预留、不让 PBS 信号变绿**。列车到信号前仍需段级 `TryPathReserve` 成功才能进段；失败即停在信号前。
- **G-2（高）现成读数就在代码里**：`yapf_rail.cpp:1136-1137` 已把段级预留结果打出来 —— `bool reserved = this->TryReservePath(nullptr, pNode->GetLastTile(), true);` → `CPL-RESERVE veh=%d reserved=%d next=%d`。若现场出现 **`reserved=0` 且 `RP cur=` 已是一长串**，即为"灰位铺出去、段预留失败"的铁证。
- **G-3（高，KI-242 已定位的"第 4 条结构性路径"）0 速即脑死**：列车一旦 `cur_speed==0`，`TrainLocoHandler`（`train_cmd.cpp:18072` 的 `j=UpdateSpeed()` → `:18084 if (j<adv_spd)`）**恒走停驶分支**，唯一会调 `TrainCheckIfLineEnds`/`TrainController`（寻路+预留+通行）的移动分支（约 `:18108` else）**永远不再执行**；同时 `TrainCoupleHandler` 每 tick 仍在重试并重铺灰位 ⇒ 现场形态正是"灰层一路铺、车一动不动"。

### 18.4 待验证判据（日志取数清单）

- **P-1** `CPL-RESERVE veh=N reserved=`（`=0` 即段预留失败，见 G-2）；
- **P-2** 第 242 轮三探针：`SPD-DROP veh=N`（速度归零那一帧）、`CPL-S0-CHK`（每次进入停驶分支）、`LOCO-MOVE veh=N`（**跨格才打 ⇒ 缺行即"从未进移动分支"**）；
- **P-3** `TPR-BLOCKED veh=N ... origin=`（预留终点受阻）/ `CPL-GATE` / `COUPLE-FAIL` / `COUPLE-DEST-EMPTY` 的循环形态；
- **P-4** `RP cur=` 序列是否确实越过了"咽喉前那个 PBS 信号"的格（与玩家给的几何逐格对齐）。

### 18.5 代码侧取证（本轮已查，2026-10-16）

- **C-1 【已证：只订位、无信号能力】**
  - `TryReserveRailTrack(tile, track, trigger_stations)`（`pbs.cpp:747`）**只写地图预留位**（Railway 分支 `:754-757` → `TryReserveTrack` + `MarkTileGroundDirtyByTile`；depot 分支 `:759-764`；road/crossing 分支 `:768` 起），**全程没有任何信号操作**。函数头注释 `:742-745` 自陈"every branch below only writes through the map-accessor setters"。
  - **唯一点绿灯**在 `TryReserveRailTrackdir`（`pbs.cpp:699`）：`:702` 先调 `TryReserveRailTrack`，随后 **`:723-724 if (success && HasPbsSignalOnTrackdir(tile, td)) → SetSignalStateByTrackdir(tile, td, SignalState::Green)`**。⇒ **PBS 绿灯的唯一驱动是 Trackdir 级预留成功**。
- **C-2 【已证：司机侧段预留走 Trackdir 级 ⇒ 会点绿灯】**
  - 司机寻路/预留侧（`ExtendTrainReservation`）两处调用均为 Trackdir 级：`train_cmd.cpp:13263`（安全等待位 `TryReserveRailTrackdir(v, tile, cur_td)`，失败原因 `safe-reserve-fail`/`safe-not-free`）、`:13300`（`TryReserveRailTrackdir(v, tile, cur_td)`，失败原因 `reserve-fail`）。⇒ 司机侧订位**必然**顺带点绿它经过的 PBS 信号（`train_cmd.cpp:14328-14330` 注释同证）。
- **C-3 【已证：`TryReservePath` 本身有信号能力，但被 rp() 的预订"顶掉"】**
  - `TryReservePath`（`yapf_rail.cpp:443`，属 `CYapfReserveTrack`）逐节点调 `ReserveSingleTrack`（`:337`），其非站台分支 **`:346 if (!TryReserveRailTrackdir(Yapf().GetVehicle(), tile, td))`** ⇒ **Trackdir 级、会点绿灯**。
  - 但耦合寻路 `CYapfFollowCoupleRailT` 的流程是 **先 `rp(st, best_td, 0)`（`:1126`）**、**后 `this->TryReservePath(nullptr, pNode->GetLastTile(), true)`（`:1137`）**（`unsafe_pos=true`，则 `:468` 的 `IsWaitingPositionFree` 检查被跳过）。而 `rp()` 用的是 **Track 级**：`:1038 reserve_if_present → TryReserveRailTrack`、`:1054`、`:1103`、`:1119` 全部 Track 级、**都不点绿灯**。
  - ⇒ 顺序决定：**`rp()` 先把沿途位订成"自有"，随后 `TryReservePath` 的 `TryReserveRailTrackdir` 在同一批位上必然返回 false（位已是我们的）**；能否补点绿灯**完全取决于 KI-361 的自有回退**（`pbs.cpp:715-721`：`!success && v!=nullptr && IsPlainRail(tile)` 时查 `R3RTrainForLedgerBit(tile, track)`，若 `booker->index == v->First()->index` 且 `HasBit(GetRailReservationTrackBits(tile), track)` 则 `success = true` → 才走到 `:723` 点绿灯）。
  - **`v` 侧无疑问**：耦合路径里 `Yapf().GetVehicle()` 就是机车（非 nullptr），门禁满足。
  - **【重要修正（避免错结论）】`IsPlainRail` 的语义包含信号格**：`rail_map.h:51-55 IsPlainRail(t) = (GetRailTileType(t) == Normal || == Signals)`。⇒ **KI-361 回退的门禁并**不**排除"带 PBS 信号的格"**（此前一轮我曾怀疑"信号格不是 plain rail 所以回退够不到"，**该怀疑错误，已否**）。
  - ⇒ 因此**唯一能解释"绿灯没点上"的剩余缺口是账本归属**：若 `rp()` 订下的那一位在账本里查不到（`R3RTrainForLedgerBit` 返回 nullptr）或归到别的车，则回退不成立 ⇒ `success` 仍 false ⇒ `ReserveSingleTrack` 返回 false ⇒ 整个 `TryReservePath` 走 `undo_reservation`（`:523-527`）并返回 false ⇒ **`CPL-RESERVE reserved=0`、末行 `return INVALID_TRACKDIR`（`:1146`）** ⇒ 信号不绿、车停在信号前。

### 18.6 影响与可能落点

若 G-3 成立，本 bug 与 KI-242 / KI-330 / KI-347 同族（"`cur_speed==0` ⇒ 停驶分支 ⇒ 脑死"），修法落点在 `TrainLocoHandler` 的停驶分支（`OT_GOTO_COUPLE` 应仍允许走一次寻路/预留重试），而**不是**预留层；灰层另有独立问题（G-1：手订不该越过信号铺位，属观感/误导，与 KI-346 的"订了车不走的轨"同族）。若 G-3 不成立而 G-2 成立（`reserved=0`），落点则在耦合寻路的段预留调用与信号授权。

### 18.7 修正后的首要假设（G-1′：**"先手订、后段预留"的顺序使绿灯授权悬在账本上**）

> C-1/C-2/C-3 查完后，G-1 的原始表述（"耦合寻路的手订**没有**信号放行能力"）**不精确**：耦合路径的**最后一步** `TryReservePath`（`:1137`）**有**信号能力（走 `TryReserveRailTrackdir`）。真正的结构性缺口是**顺序 + 归属**：

1. **顺序**：`rp()`（Track 级，无绿灯）**先**把整条路径的位（含咽喉前那个 PBS 信号所在的格）订成"自有"；`TryReservePath`（Trackdir 级，有绿灯）**后**跑，在同一批位上 `TryReserveRailTrack` 恒返回 false（位已被自己订走）。
2. **归属**：此时**只有** KI-361 的自有回退（`pbs.cpp:715-721`）能让 `success=true` 从而点到绿灯；回退的判据是 **`R3RTrainForLedgerBit(tile, track)` 认出"这一位是本链订的"**。
3. ⇒ **若账本没记住 `rp()` 的这笔预订（或记成了别的 owner），回退不成立 ⇒ `success` 仍 false ⇒ `TryReservePath` 整体失败（`CPL-RESERVE reserved=0`）⇒ 那个 PBS 信号**不绿** ⇒ 列车停在信号前，而 rp() 铺的灰层**仍在**（这正好解释玩家看到的"灰路径越过了信号、车却停下"）。**

**关键实测量（唯一能定死的读数）**：现场日志里的 **`CPL-RESERVE veh=N reserved=`**（`yapf_rail.cpp:1141`）。
- `reserved=0` ⇒ G-1′ 坐实（段预留失败、绿灯无授权）；此时再看 `RP cur=` 是否已铺到咽喉后 ⇒ 铺了却没授权，铁证；
- `reserved=1` 而车仍停 ⇒ 转 G-3（0 速脑死，KI-242 家族）。

**旁证量**：`R3RTrainForLedgerBit` 的归属 —— 若日志/探针能打出 rp() 订位时账本登记的 owner 与 `TryReservePath` 查询时返回的 owner 不一致，即为 G-1′ 的直接证据。

### 18.9 【本轮收敛的核心结论】KI-361 自有回退是 **plain-rail-only**，救不了"咽喉"里的非 plain rail 格

读 `pbs.cpp:699-732` 原文（`TryReserveRailTrackdir`）逐字：

```cpp
699 bool TryReserveRailTrackdir(const Train *v, TileIndex tile, Trackdir td, bool trigger_stations)
701     const Track track = TrackdirToTrack(td);
702     bool success = TryReserveRailTrack(tile, track, trigger_stations);
      /* KI-361 注释 :704-714 自陈：rp() 只订 Track 级、无信号能力，
         后来同一位的 Trackdir 级预留会返回 false，即使位仍是"我们的"；
         后果 (a) 调用者把该格从路径里丢掉（再不释放 ⇒ 孤儿预留），
              (b) PBS 信号永不变绿，车被自己订的格永远挡住。
         修法：把"已被本链订下"当作预留成功。**"Only plain rail is
         special-cased; every other tile type keeps its original semantics."** */
715     if (!success && v != nullptr && IsPlainRail(tile)) {
716         const Train *const booker = R3RTrainForLedgerBit(tile, track);
717         if (booker != nullptr && booker->index == v->First()->index &&
718                 HasBit(GetRailReservationTrackBits(tile), track)) {
719             success = true;
720         }
721     }
723     if (success && HasPbsSignalOnTrackdir(tile, td)) {
724         SetSignalStateByTrackdir(tile, td, SignalState::Green);   // ← 绿灯唯一驱动
726         ...aspect...
730     }
731     return success;
```

**两个已核实的量**：
- `IsPlainRail(t)`（`rail_map.h:51-55`）= `GetRailTileType(t) == Normal || == Signals` ⇒ **车站格(`Station`)、车库(`Depot`)、道口、隧道桥一律为 false**。
- 账本 owner（`pbs.cpp:168-177` `R3RResvLedgerActorHead()`）= `r3r_resv_actor` 的 `v->First()->index.base()`；`rp()` 在 `R3RResvPhaseGuard("yapf-couple", R3RResvActorID(v))`（`yapf_rail.cpp:809`）内执行 ⇒ 预订**会**入账本、owner = 机车链头。⇒ 归属侧（`booker->index == v->First()->index`）在耦合场景**通常成立**。

⇒ **真正会断的只有一件事：该格的 `IsPlainRail` 为假。** 即：

> **G-1″（新首要根因）**：耦合寻路 `rp()` 沿路径订下的位里，若有一格**不是 plain rail**（车站格/车库/道口/隧道桥），随后的 `TryReservePath`（`:1137`）在该格调 `TryReserveRailTrackdir` → `TryReserveRailTrack` 因"位已是我们的"返回 false → **KI-361 回退被 `IsPlainRail(tile)` 拦掉，救不回来** → `ReserveSingleTrack` 返回 false → 整个 `TryReservePath` 走 `undo_reservation`（`:523-527`）返回 false → **`CPL-RESERVE reserved=0`、末行 `return INVALID_TRACKDIR`** → **沿途所有 PBS 信号（含咽喉前那个）一个都不变绿** → 列车停在信号前，而 `rp()` 铺的灰层**仍在**（`undo` 只撤 `TryReservePath` 自己订的，撤不掉 `rp()` 的位）。

**这恰好解释了玩家的原话与几何**：
- 为什么偏偏是"**PBS + 复杂车站咽喉**"——咽喉里天然有车站格/道口格，是唯一会让 `IsPlainRail` 为假的地方；
- 为什么"灰路径越过了信号"——灰层是 `rp()` 铺的，它本来就无信号概念、一路铺过去；
- 为什么"车停在信号前"——绿灯唯一驱动 `:723-724` 被 `reserved=0` 跳过。

**次要分支（若 reserved=1 仍停）** = G-3（0 速脑死，KI-242 家族：`cur_speed==0 ⇒ TrainLocoHandler` 停驶分支 ⇒ 从不进移动分支 ⇒ 不寻路不预留）。

### 18.10 待办（下一轮）

1. **仍需玩家现场日志**：`CPL-RESERVE veh=N reserved=?`（`yapf_rail.cpp:1141`）—— `=0` ⇒ G-1″ 坐实。
2. 代码侧补齐（G-1″ 的自证）：读 `ReserveSingleTrack`（`yapf_rail.cpp:337-...`）确认它对**车站格**走的分支（是否也调 `TryReserveRailTrackdir`），以及 `rp()` 的 `reserve_if_present` 是否**真的会**订车站格（若 `reserve_if_present` 只订 plain rail，则 G-1″ 需改口为"`TryReservePath` 在车站格上首次预订成功、失败点另在他处"）。
3. 若 G-1″ 成立，候选修法（**待玩家拍板，勿先改码**）：把 KI-361 回退的门禁由 `IsPlainRail(tile)` 放宽为"该位确实已被本链订下"（去掉瓦片类型限制，或补上 `Station`/`Depot` 一并放行），并只在"地图位确实存在"的前提下置绿。

### 18.8 本轮仍未做

- 未取现场日志（**需要玩家提供**含 `CPL-RESERVE` / `RP cur=` / `SPD-DROP` / `COUPLE-FAIL` 的 `build\R3R_debug.log`）；
- 未查 `R3RTrainForLedgerBit` 的实现细节（账本如何登记 Track 级预订、`R3RResvLedgerNote` 的调用点）——这是 G-1′ 的下一步代码取证；
- 未查 `UnreserveSingleTrack` / `undo_reservation`（`:523-527`）失败路径是否会**误释放 rp() 铺的位**（若会，则"灰层"本应消失却仍在 ⇒ 与 U-17 交叉，需一并取数）；
- 未改任何源码、未构建（按规则：先落盘证据与假设，待玩家日志确认 G-1′/G-3 后再定修法落点）。

### 18.11 【本轮定案·复用现场日志 `build\R3R_debug.log`】G-1″ 被证伪；真根因＝"信号格那一段被 `rp()` 以 Track 级订下后，**再也没有任何调用者对它发起 Trackdir 级预留**，KI-361 的绿灯回退因此永不触发"

**取样有效性**：`build\R3R_debug.log` mtime 18:38:17 晚于 `build\openttd.exe` 18:24:14 ⇒ 现场日志由当轮 exe 产生，可用（本轮**零新增探针、零源码改动、零构建**）。

**决定性读数一（信号为什么红）**——`build\R3R_debug.log:11718-11725` 尾部终态，`REDSIG`（"为什么红"专用探针）：

```
11718:REDSIG veh=23 old=1112,128 new=1112,129 dir=1 chosen=0x2 i=1 prog=0 spd=2 wait=0 stuck=0
11719:REDSIG   tdbits=0x2 reach=0x16 sigs=0x1717 red=0x16 bits=0x2
11720:REDSIG   new present=0x8 states=0x7 resv=0x2 veh=0
11721:REDSIG     track=1 type=5 oneway=1 pbs=1
11722:REDSIG   beyond=1112,130 valid=1 plain=1 resv=0x2 veh=0
11723:REDSIG     beyond track=1 owner=23 near=22
11724:REDSIG     beyond track=1 dir=1 hit=22
11725:REDSIG     beyond track=1 dir=9 hit=36
```

判读：
- 列车运动前端在 `1112,128`，要进 `1112,129`（`dir=1`，南向）；**该格有一个 PBS 单向（no-entry）信号**（`:11721 track=1`(Track 枚举 TRACK_Y) `type=5` `oneway=1` `pbs=1`）。
- `red=0x16` 且 `chosen=0x2` ⇒ `red_signals & chosen_track != 0` ⇒ **列车按"方向红灯"刹车**（这正是 REDSIG 探针所在的"单向信号前停车"分支）。
- **信号格自己与信号后一格都已经订下了**：`:11720 new ... resv=0x2`（1112,128 的 TRACK_Y 位已被订）、`:11722 beyond=1112,130 ... plain=1 resv=0x2`（信号后一格是**普通轨道且已被订**）。
- **而这笔订位是"它自己的"**：`:11723 beyond track=1 owner=23`（问的是本车 veh=23）。

⇒ **"位是我的，信号却是红的，于是我被自己订的格子挡住"** —— 与 `pbs.cpp:704-714`（KI-361 自己的代码注释）**逐字吻合**：

> "…(b) the PBS signal is never set green, so the train is held forever behind a tile it reserved itself. … **both observed in the 1112,129 red-signal scene**"

**⇒ 现象学定性完成：本现场就是注释里那个 `1112,129 red-signal scene`，且 KI-361 的修法在当前 exe 里没有生效（症状逐字复现）。**

**决定性读数二（G-1″ 被证伪）**：
- 全日志所有 `CPL-RESERVE veh=N reserved=` 读数**均为 `reserved=1`**（行 851 / 1974 / 7592 / 7692 / 9075），**无一条 `=0`** ⇒ §18.9 提出的 G-1″（"TryReservePath 整体失败、全信号不绿"）**被现场日志证伪**——段预留是成功的、末行没有走 `undo_reservation`。
- ⇒ 排除了"预留失败"这一支；剩下的只能是 **"预留成功了，但绿灯那一步对**这一段**从未执行"**。

**决定性读数三（谁订的、以及为什么没绿）**——预订行按 phase 打标：
```
2415:RESV-WATCH SET-RAIL tile=1112,130 track=3 phase=yapf-resv   actor=23 head=23
2416:RESV-WATCH SET-RAIL tile=1112,131 track=2 phase=yapf-resv   actor=23 head=23
2541:UNRES-RAIL tile=1112,131 track=2 phase=controller actor=23
2544:UNRES-RAIL tile=1112,130 track=3 phase=controller actor=23
…（此时该处是**斜轨** UPPER/LOWER，后被玩家改建为直轨 TRACK_Y）
7691:RESV-WATCH SET-RAIL tile=1112,128 track=1 phase=yapf-couple actor=23 head=23   ← 信号格
7690:RESV-WATCH SET-RAIL tile=1112,129 track=1 phase=yapf-couple actor=23 head=23
7689:RESV-WATCH SET-RAIL tile=1112,130 track=1 phase=yapf-couple actor=23 head=23
7692:CPL-RESERVE veh=23 reserved=1
```
- 信号格 `1112,128` 与它前后两格的位，是 **`phase=yapf-couple`（耦合寻路）以 `track=1`（Track 级）**订下的 ⇒ 走的是 `TryReserveRailTrack`，**该函数全程不碰信号**（C-1 已证）。
- 逐字对上 REDSIG 的读数：REDSIG 报"1112,128 resv=0x2 + 1112,130 resv=0x2"——**就是这三行 SET-RAIL 订下的位**。

**为什么 Trackdir 级（唯一能点绿的那条路）从此不再经过这些格**：
1. 绿灯唯一驱动是 `TryReserveRailTrackdir`（`pbs.cpp:699`）的 `:723-724 SetSignalStateByTrackdir(..., Green)`。
2. 耦合寻路里唯一点绿的机会是 `TryReservePath`（`yapf_rail.cpp:443 → ReserveSingleTrack :346`），而调用点是 `yapf_rail.cpp:1137 TryReservePath(nullptr, pNode->GetLastTile(), true)` —— **只沿"最佳节点（node）"的末段推路径**；`rp()`（`:1126`）却把整条回溯路线的位逐格订下去，**包含这些"不属于任何 node 段"的格**（这正是 `pbs.cpp:704-706` 注释的自陈："its rp() lambda has no signal-setting capability and the tiles are 'not part of any node segment'"）。
3. ⇒ **对 1112,128 这个 trackdir，从来没有一次 `TryReserveRailTrackdir` 被调到**（Track 级订位不会自发补一次 Trackdir 级；司机侧 `ExtendTrainReservation` 认为"位已归我"直接沿用；耦合寻路只推 node 段）。既然没有任何调用者再碰这个位，**KI-361 的自有回退（`pbs.cpp:715-721`，触发点在 `TryReserveRailTrackdir` 函数体内）就永远没有机会执行** ⇒ `SetSignalStateByTrackdir(..., Green)` 永不执行 ⇒ **信号恒红**。
4. 尾段旁证：终止态里 veh=23 只有 `CPL-PAIR`/`COUPLE-FAIL`（`:11715/:11726`），**没有任何 `TRP veh=23`**（`:11695-11744` 区间内只有 `TPR-WALK veh=11` / `TRP veh=11`）⇒ 停住之后司机侧确实不再做预留尝试，也就永远不会更新信号。

**为什么车停在这一格而不是别处（次要机制，与上互为因果）**：`:11715 CPL-PAIR act=23 tgt=22 dist=38 actTile=1112,125 tgtTile=1085,136` + `:11726 COUPLE-FAIL loco=23 order=16 tx=1112 ty=125` + `:11734 COUPLE-WAIT-OFFDEST veh=23 tile=1112,125 mf=1112,128 real=6 destStn=1 destTile=287807 onStnTile=0` ⇒ 它的 `GOTO_COUPLE`(order=16) 目标车底 22 在 38 格外（车站 1 咽喉，1085,136），本车回到 `COUPLE-WAIT-OFFDEST`（不在订单目的地站台格）循环；`LOCO-MOVE veh=23 tile=1112,125 j=256 spd=2` 说明它**仍在移动分支**（不是 §18.6 假设的 G-3 "0 速脑死"），只是被前述红灯钉在 1112,125 这一格前。⇒ **G-3 在本现场不成立**（曾出现的 `SPD-DROP prev=27` 是红灯刹车的**结果**，不是原因）。

**结论（本轮定案）**：本 bug 的根因**在预留层，不在信号层**——**"Track 级订位订到了含 PBS 信号的格，却没有任何 Trackdir 级调用再去碰它，于是绿灯永不产生"**。灰层（地图位）与通行权（绿灯）分离的双状态由此具象化为：**灰层越过了信号（rp() 铺的），车停在信号前（绿灯没点）**。KI-361 的回退修法是"亡羊补牢"型的——**它补救的是"后来又有人来订同一位"的场景，本场景里根本没有人来**，所以修了等于没修。

**【口径澄清·防误读（2026-10-16）】**本节的"不一致"**不是"灰层与预留两套铺设不一致"**——灰层**就是**预留的渲染（`gui.show_track_reservation` 直接读地图 `_m[t].m2` bit 8-11，`PALETTE_CRASH` 暗色），两者**完全一致**、不存在第二套。真正不一致的是**「预留(灰)」与「信号灯(绿)」**这一对：预留**确实订下了**（`resv=0x2`，所以灰层铺了过去、玩家看到的"预留成功"是真的），但绿灯是**另一件独立的事**（由 `TryReserveRailTrackdir` 负责），而这条链上**没有任何调用者**去点它（见前文"再没有 Trackdir 级预留"）。⇒ 正确表述是「**位订了、灯没点**」，不是「灰层和预留不一致」；玩家从"有灰层"推出的"**预留成功 ⇒ 可以开**"这一步是玩家侧的推断，引擎侧从没承诺这两件事绑定。

**【上游对照·定案（2026-10-16，玩家问"这是 R3R 引入的吗"）】答：缺陷是 R3R 引入的；但"Track 级订位不点灯"本身是上游契约、不是 R3R 改坏的。两者要分开说。**

- **证据 1：违约代码 `rp()` 是 R3R 自己的提交。** `git log -S "rp = [&](TileIndex cur, Trackdir cur_td, int depth)" -- src/pathfinder/yapf/yapf_rail.cpp` **唯一命中** `e30c8b0f1b R3R: couple pathfinding - reserve only confirmed path; loco no longer wanders into depot`。该 lambda 定位于 `yapf_rail.cpp:1021`，体内满是 R3R 标记（`R3RFopenDbg`/`R3RSkippedStationStretchContains`/`R3R:` 注释），**上游不存在这个函数**。
- **证据 2：`TryReserveRailTrack`（`pbs.cpp:747-810+`）全程不写信号。** 函数体只按 tile 类型写预留位：plain rail → `TryReserveTrack`（:755）、depot → `SetDepotReservation`（:761）、crossing → `SetCrossingReservation`（:782）、station → `SetRailStationReservation`（:791）、tunnelbridge → `SetTunnelBridgeReservation`（:803 起）。**零处 `SetSignalStateByTrackdir`**。R3R 在这个函数里只加了 KI-207 的探针注释（`pbs.cpp:742-745`），**行为未改** ⇒ "Track 级不点灯"＝上游原样。
- **证据 3：上游的点灯唯一入口是 Trackdir 级，而它的上游*唯一*调用者就是"正向订路"。** `TryReserveRailTrackdir`（`pbs.cpp:699-731`）第 702 行先调 Track 级，第 723-725 行 `if (success && HasPbsSignalOnTrackdir(tile, td)) SetSignalStateByTrackdir(..., Green)`；其注释（`pbs.cpp:690-691`）自陈 *"This also sets PBS signals to green if reserving through the facing track direction"*。**上游调用点只有 `yapf_rail.cpp:346`（YAPF 预留循环内逐格 Trackdir 级）**；R3R 另加了两处（`train_cmd.cpp:13263/13300` 的 safe-reserve）。
- **证据 4：上游其余 Track 级调用点全是"保护我已占/即将占的格"，不是"订我还没走到的正向格"。** 按上下文归类：`train_cmd.cpp:15331/15335/15362`＝`ReserveTrackUnderConsist` 一族（wormhole 两支＋主分支）；`16460/16808/16904/17119/17685/17688`＝`TrainController`/进入格/桥隧/库/平台占用一族；`13931/14083/14171`＝R3R 自己的 TRP 探索（`moving_front->tile`，即车头脚下格）。这些格要么车已在上面、要么没有"正对车头的 PBS 信号"，**不以"给车一条通行权"为目的** ⇒ 不产生本症状。
- **结论**：上游"正向订路 ⇒ 一定经 Trackdir 级 ⇒ 一定带灯"；R3R 的 `rp()` 把"订车还要走的正向格"退回 Track 级，**单方面破坏了这条上游不变式** §18.11 的症状由此而来。
- **对修法甲的含义（回玩家"怕修出新的 bug"）**：甲＝把 `rp()` 的正向订位改回 Trackdir 级，**是恢复上游契约、不是发明新机制**；且 `TryReserveRailTrackdir` 的第一步就是 `TryReserveRailTrack`，**返回值语义逐字相同 ⇒ 预留结果不变，唯一新增副作用是"给正对的 PBS 信号点绿"**。此前担心的 U-11（"普通信号被预留点亮"）**结构性不可能**：门禁 `HasPbsSignalOnTrackdir`（`rail_map.h:640-644`）＝ `IsTileType(Railway) && HasSignalOnTrackdir(tile, td) && IsPbsSignal(GetSignalType(...))`，非 PBS 信号恒为假。需换的点仅两处（`yapf_rail.cpp` 的 `cur/cur_td` 与"进入格"的 `nd`，后者由 KI-346 修复已带 trackdir）；由平台轴推导、无 trackdir 的补订保持 Track 级（它们没有"正对方向"，本就不该点灯）。

**【考古·为什么当初会退化（2026-10-16，玩家问"为什么上游 R3R 要引入那个违约的东西"）】动机是正确的，退化是无意的 —— 绿灯是在"重写订位"时被顺手丢掉的。**

- **事实 A（动机，提交信息自证）**：`e30c8b0f1b`（2026-08-28 21:25，Lzh20100913，12 files +716/−35）标题＝*"R3R: couple pathfinding - reserve only confirmed path; loco no longer wanders into depot"*。⇒ 它要修的是**旧毛病**：耦合寻路在**搜索阶段**就订位，把探索出来的路线（含朝车库方向那条）一并订下 ⇒ 机车乱跑进库。方向正确：搜索期只探测、**路径确认后**再订。同提交给 `TryReservePath()` 加了 `bool unsafe_pos = false` 形参。
- **事实 B（退化点，实证）**：**父提交的耦合寻路订位本来就是 Trackdir 级**——`git show e30c8b0f1b^:src/pathfinder/yapf/yapf_rail.cpp` 里 `bool ReserveSingleTrack(TileIndex tile, Trackdir td)` 体内为 `TryReserveRailTrackdir(Yapf().GetVehicle(), tile, td)`（带 `v`、带绿灯）。本提交新写的 `rp()` 改用 `TryReserveRailTrack(cur, TrackdirToTrack(cur_td))`（**无 `v`、无灯**）。
- **事实 C（为什么容易混）**：**同一提交里刚移植进来一段合法的 Track 级用法（探测式）**——`FindSafeCouplePositionProc`（注释自陈 *"ported from the pxp-decouple patch"*）内 `if (!TryReserveRailTrack(tile, TrackdirToTrack(td))) {…fail…} UnreserveRailTrack(tile, TrackdirToTrack(td));`：**订一下立刻撤**，回答"这格空不空"。这种用法**本就该用 Track 级**（无通行权、也不该点灯）⇒ 同一文件里"Track 级＝探测"与"Track 级＝订位"两种意图共用同一 API，后者的语义被吃掉一半。
- **推断（待验证，按规则标注）**：主因很可能是"`rp()` 还要按站台轴补订**没有 trackdir 的轨**（`AxisToTrack(axis)`），整个函数统一用 Track 级更自洽"，于是"有方向的那两笔"被一并降级。**无实据**表明作者有意避开点灯。
- **受害面收窄（重要）**：只有"**车头前方、正对行进方向、且挂 PBS 信号**"的格才需要绿灯；车后方/脚下格的 Track 级订位**完全正确**。1112,129 恰属前者。
- **⇒ 对甲的最终背书**：`TryReserveRailTrackdir` **不是外部新机制**，它就是同一文件、同一 `CYapfReserveTrack` 类里既有的 `ReserveSingleTrack` 的写法；甲＝把这次无意的 API 降级补回去。

**候选修法（待玩家拍板，勿先改码）**：
- **甲（治本·推荐）**：让订位与绿灯**在同一个函数里成对发生** —— `rp()` 的 Track 级订位路径（`yapf_rail.cpp` 的 `reserve_if_present` 一族）在订下**含 PBS 信号**的格时，顺带对该位所在 trackdir 补一次 `SetSignalStateByTrackdir(..., Green)`（或干脆把 rp() 改成走 `TryReserveRailTrackdir`，由它统一负责"订位 + 亮灯"）。这样"灰层铺到哪、绿灯就点到哪"，两者不可能再分离。
- **乙（治标）**：给 `rp()` 的 Track 级订位做**收尾扫尾** —— 路径订完后，对路径上所有"含 PBS 信号"的格补跑一遍 `TryReserveRailTrackdir`（此时自有回退会生效，`IsPlainRail` 门禁对信号格为真）。改动小、风险低，但属"事后补灯"，仍可能漏（例如路径被后来的 `undo` 部分撤销时）。
- **丙（可作独立缺陷）**：灰层可视化本身不该越过信号（§十七 的 U-17）。即使修好绿灯，`OT_GOTO_COUPLE` 的灰路径"一路铺过一串普通信号 + PBS"仍与引擎侧"预留按 PBS 段分段、普通信号是收尾锚点"（§十三~§十六）相冲突 ⇒ 观感与语义双错，建议一并收口。

**新增疑点（按规则立即登记）**：
- **U-18（中）**：`REDSIG` 的三个归属读数互相不一致 —— `:11723 owner=23`（引擎侧 `GetTrainForReservation` 家族）vs `near=22`（邻近归属）vs `:11724/:11725 dir=1 hit=22 / dir=9 hit=36`（按方向走预留链）。**期望**：三者应指向同一列车（本车 23）。**实测**：22 / 36 各占一个方向。**为什么重要**：若 `owner=23` 不可信，则"位是我的"这个前提本身要复核；若 `near/hit` 不可信，则 R3R 自己的孤儿判定/清理（KI-345/REG-243-W 一族）会误判。**还需什么数据**：`REDSIG` 探针源码（哪个读数是哪个 API）+ 那三格的逐格归属 dump。**暂不改码。**
- **U-19（低）**：`RESV-WATCH SET-RAIL tile=1112,130 track=3`（斜轨 UPPER/LOWER）之后该处被改建为直轨 TRACK_Y，`2541/:2544` 只见斜轨位被释放 —— **改建（拆轨/重建）时旧预留位是否被彻底过滤**未取数（与 KI-346/§51 的"地鼠"同族）。
- **U-20（低）**：终止态 `1112,125` 这一格（车脚下、`DEPOTBLK track=02`）与 `1112,128/129/130` 的位分别由谁订、何时释放，未逐格取数。

**仍未做**：未改任何源码、未构建（按规则待拍板）；未复核 `REDSIG` 探针源码（U-18）。

### 18.12 【U-18 结案】`REDSIG` 三个归属读数不是"互相矛盾"，而是**三个 API 在回答三个不同的问题**

**探针源码定位**（`src\train_cmd.cpp:16337-16398`，`R3REDGE_REDSIG` 边沿闸门内）：
- `owner`（`:16378`）= `GetTrainForReservation(beyond, t)` —— **`src\pbs.cpp:2162`**，且其**第一行就是问账本**：`:2178 if (Train *owner = R3RTrainForLedgerBit(tile, track)) return owner;`（KI-361 加的分支），账本记的是**写入者**（本场景 `phase=yapf-couple actor=23` 的 SET-RAIL 那一笔）。
- `near`（`:16379`）= `GetR3RReservationOwnerNearby(beyond, t)` —— **`src\pbs.cpp:2247`** → `R3RFollowReservationToNearbyTrain`（`:2136`）→ 沿预留位走，返回**路径上第一个"格上有车"的列车**（KI-210 的"最近列车"口径，**完全不问账本**）。
- `hit`（`:16392-16396`）= `R3RReservationOwnerInDirection(beyond, td_fwd/td_rev)` —— **`src\pbs.cpp:2279`**，同一"最近列车"走法、但**按单个方向**分别提问（KI-350 专为红花探针加的）。

**⇒ 读数解释（三者各自都对，只是问题不同）**：
| 读数 | 问的问题 | 答案 | 为什么 |
|---|---|---|---|
| `owner=23` | "**这一位是谁订的**" | 23（本车） | 账本优先（KI-361），记录的是写入者 `actor=23` |
| `near=22` | "**这条预留位沿路走进去，会撞到哪列车**" | 22（被挂的等待车底） | 耦合路径 rp() 订的位**与等待车底自己的预留啮合**，走到的物理尽头是停在站里的 22 |
| `hit: dir=1→22 / dir=9→36` | 同上，但**分方向** | 一个方向 22、另一方向 36 | 两个方向各自的"第一个有车的格"落在不同列车上 |

**结论（U-18 不升级为缺陷，但登记为口径约束）**：三个 API **语义不同、都被正确实现**，"不一致"是**预期的**。关键含义有两条：
1. **`owner=23` 可信、且是"这一位是不是我的"这个问题的正解**（账本口径），⇒ §18.11 的定案前提（"位是我的"）**成立**，不必回退。
2. **`near`/`hit` 恒答"物理尽头那列车"**（本场景恒为 22）—— R3R 的孤儿判定/清理若用 `near` 口径，会把**自己的新订位判成别人的**（正是 KI-361 注释里记的 2026-10-11 现场：SET-RAIL actor=23 而 walk 恒答 veh22，于是机车把自己的位当成红灯挡在自己面前）。这就是 KI-361 必须"账本优先"的原因，也是 KI-210 把 purge 做成"最近列车≠自己就一律不动"的原因（保守，宁可漏清不可误清）。
**⇒ 对 REG-243-W/REG-243-Y 清理口径的约束**：清理/审计路径**必须明确自己用哪套口径** —— 问"谁订的"用账本（`R3RTrainForLedgerBit`），问"这条路能不能走通/尽头是谁"用 walk；**不得把两者混用当同一结论**。`train_cmd.cpp:10695` 的 `GetR3RReservationOwnerNearby(t, track)` 调用点（预留记账/purge 侧）正是 `near` 口径，未读改。

**仍未做（更新）**：U-18 **已结案（口径不同，非缺陷）**；U-19（改建后旧预留位过滤）、U-20（终止态逐格归属）**仍未取数**；未改任何源码、未构建。

---

### 18.13 【修法「甲」落地】`rp()` 的订位由 **Track 级**升级为 **Trackdir 级** ——"灰层铺到哪、绿灯就点到哪"（第 243 轮续五十八，2026-10-16；已实现 + 已编译）

**玩家拍板**：§18.11 给出的候选修法 **甲（治本·推荐）**，原话"好，动手吧"。本轮仅动 `src\pathfinder\yapf\yapf_rail.cpp`（**纯 `.cpp`，未碰任何 `*.h` / `*.hpp` / lang**，故 KI-183 护栏判增量合法）。

#### 18.13.1 核心改动：新增方向版订位 lambda（`:1049-1052`）

```cpp
auto reserve_dir_if_present = [&](TileIndex t, Trackdir td) {
    const TrackBits bits = TrackdirBitsToTrackBits(GetTileTrackdirBits(t, ::TransportType::TRANSPORT_RAIL, 0));
    if ((bits & TrackToTrackBits(TrackdirToTrack(td))) != 0) TryReserveRailTrackdir(v, t, td);
};
```

- 与既有 `reserve_if_present`（`:1036-1039`，Track 级）**共用同一道"该格真有这条轨"前置校验** ⇒ 保留 `pbs.cpp:144/145` 断言安全（`assert_msg_tile((TrackdirBitsToTrackBits(...) & TrackToTrackBits(track)) != 0, ...)`，`pbs.cpp:749-750`）。
- 唯一区别＝终点 API。`TryReserveRailTrackdir(v, tile, td)`（`pbs.cpp:699`）先订位，再在 `:723-725`：
  ```cpp
  if (success && HasPbsSignalOnTrackdir(tile, td)) {
      SetSignalStateByTrackdir(tile, td, SignalState::Green);
      MarkSingleSignalDirty(tile, td);
      ...
  }
  ```
  而 **Track 级 `TryReserveRailTrack`（`pbs.cpp:747`）全程不碰信号**（本轮 `read_file` 复核 `:747-790`：只有 `TryReserveTrack`/`SetDepotReservation`/… 与 `MarkTile*Dirty`，**无任何 `SetSignalState*`**）。

#### 18.13.2 5 处调用点升级（全部是"机车确定会以某方向驶过"的格）

| 现行号 | 分支 | 改动 |
|---|---|---|
| `:1067` | 站台跳段分支（当前格 `cur`） | 原 `TryReserveRailTrack(cur, TrackdirToTrack(cur_td))` → `reserve_dir_if_present(cur, cur_td)` |
| `:1090` | 单后继分支（当前格 `cur`） | 原 `reserve_if_present(cur, TrackdirToTrack(cur_td))` → `reserve_dir_if_present(cur, cur_td)` |
| `:1106` | 单后继分支（**驶入格** `f3.new_tile`） | 原 KI-346 修法"`entry_track = TrackdirToTrack(ReverseTrackdir(cur_td))` + `TryReserveRailTrack`" → `reserve_dir_if_present(f3.new_tile, nd)`（`nd` 即跟随器真正续走的 trackdir，`FindFirstTrackdir(tdb3)`） |
| `:1116` | 多后继回溯循环（当前格 `cur`） | 同 `:1090` |
| `:1132` | 多后继回溯循环（**驶入格** `f3.new_tile`） | 同 `:1106`（`nd` 由 `(Trackdir)FindFirstBit(tb)` 提到循环内具名） |

#### 18.13.3 刻意**保持 Track 级不动**的一处

`:1080` 的 `reserve_if_present(p, plat_track)` —— **站台轴格**（机车与车底之间的平台段，`plat_track = AxisToTrack(GetRailStationAxis(f3.new_tile))`）。这些格**只有轴、没有行车方向**，且站台格上不存在信号 ⇒ 既无法、也不需要点绿。

#### 18.13.4 为什么这正好治 §18.11 的根因

- **旧行为**＝`rp()` 把含 PBS 信号的格以 **Track 级**订下（`phase=yapf-couple`，`RP cur=`/`RESV-WATCH SET-RAIL tile=1112,128/129/130 track=1 actor=23`），而唯一点绿入口 `TryReservePath`（`:1137`）**只沿"最佳 node 末段"推进**、从不经过这些"不属于任何 node 段"的格（`pbs.cpp:704-706` 注释自陈）⇒ 这些 trackdir **再无任何 `TryReserveRailTrackdir` 调用** ⇒ KI-361 的自有回退（`pbs.cpp:715-721`，**只在 `TryReserveRailTrackdir` 函数体内**）**永不触发** ⇒ 信号恒红 ⇒ 机车停在自己已订下的位前（KI-349 / REG-243-Y 现场 `REDSIG veh=23` `1112,128 resv=0x2 red=0x16`、`beyond=1112,130 plain=1 resv=0x2 owner=23`，与 `pbs.cpp:704-714` 注释点名的 `1112,129 red-signal scene` 逐字吻合）。
- **新行为**＝`rp()` 对每一格都做 **Trackdir 级**订位 ⇒ **点绿与铺位成对发生在同一格**（"灰层铺到哪、绿灯就点到哪"），不再依赖"事后有人回来对同一 trackdir 补订"。

#### 18.13.5 顺带结清 U-11（低）

`SetSignalStateByTrackdir(..., Green)` 的调用被 `HasPbsSignalOnTrackdir(tile, td)`（`rail_map.h:640-644`，**结构性排除非 PBS 信号**）守卫 ⇒ §18.11 新增的这批 Trackdir 级订位**不会**点亮普通信号；"新增 Trackdir 级订位会引入'普通信号被预留非法点亮'新缺陷"的担忧**不成立**（本轮取证）。

#### 18.13.6 负判据 / 不回归

- 单轨格上 `TryReserveRailTrackdir(t, td)` 的第一步仍是 `TryReserveRailTrack(t, TrackdirToTrack(td))`（`pbs.cpp:701-702`）⇒ **订位语义逐字不变**，唯一增量＝"绿灯 + 账本/相位（`R3RResvPhaseGuard` 已在 `:809` 外层，KI-361 账本照旧入账）"。
- 顺序副作用（正向）：历史顺序是"先 `rp()` 后 `TryReservePath`"，`rp()` 现在**自己**就把绿点了 ⇒ `TryReservePath` 里 KI-361 的自有回退从"唯一途径"退化为**冗余兜底**（KI-361 保留不动，作为其它调用者的保护）。
- 站台格 / 车库格（`:1056-1059` 拒绝穿过、只放行起点）／非 plain rail 分支一律未改；`RP cur=` 探针（`:1025`）与 512 深度上限（`:1053`）未动。

#### 18.13.7 构建自证（第 243 轮续五十八）

- 复用既有 `_tmp_inc_build.cmd`（**未新建任何 `.cmd`**，遵守既有脚本复用规则）。
- `build\R3R_incbuild.guard.log` = `GUARD: incremental is safe (no header/lang file is newer than the newest object)` ⇒ **改的是 `yapf_rail.cpp`（.cpp）而非 `yapf_destrail.hpp`**，本轮**不需要全量**（第 109 轮那次 692 步全量是改头文件 `yapf_destrail.hpp` 引起的，与本轮无关）。
- `build\R3R_incbuild.log` 尾部：`[2/3] Building CXX object CMakeFiles\openttd_lib.dir\src\pathfinder\yapf\yapf_rail.cpp.obj` → `[3/3] Linking CXX executable openttd.exe`（Linking 恰 1 次；`error C[0-9]` / `fatal error` / `FAILED:` / `build stopped` 零命中）。
- `build\R3R_incbuild.done` = `EXIT_CODE=0`（@21:02:22）。
- 时间戳链：`src\pathfinder\yapf\yapf_rail.cpp` @21:01:33 → `yapf_rail.cpp.obj` @21:01:56 → `build\openttd.exe` @21:02:04（51 868 160 B）逐级变新。
- `read_lints` 0 条。

#### 18.13.8 仍待做

1. **游戏内复测**：同一 `1112,129` 场景应看到 veh=23 越过该 PBS 信号（判据见 KI-349 文末）。
2. `reserve_dir_if_present` 只解决"**自己订的位不点绿**"；`rp()` 仍会**按整条 YAPF 路径跨普通信号铺位**（§十七 U-15 / U-17 的独立问题）——本轮未动（该项是"灰层该不该越过信号"，与"绿灯该不该点"是两件事）。
3. `CPL-RESERVE reserved=` 与 `REDSIG` 探针保留，复测时作回归对照。

---

### 18.14 【修法甲引爆的断言崩溃·已修】`pbs.cpp:715` 的 `IsPlainRail(tile)` 对非 Railway 格调用 `GetRailTileType` ⇒ `rail_map.h:40` 断言（第 243 轮续五十九，2026-10-16；已实现 + 已编译）

**玩家报告**（原话）：「哎，rail_map.h第40行怎么断言崩溃了 ，应该是引入了一处错误吧」，附现场日志 `D:\temp\codebuddy-dropped-files\a1063a60-68b8-48a8-8b7e-4bf29243e4c1\R3R_debug.log`。

**取样有效性**：该日志 831 行 / 78 449 B / mtime 2026-10-16 21:09:05，晚于续五十八版 `build\openttd.exe` @21:02:04 ⇒ **由修法甲版 exe 产生，可用**。

#### 18.14.1 断言点

`src\rail_map.h:40`（`GetRailTileType` 体内）：

```cpp
[[debug_inline]] inline static RailTileType GetRailTileType(TileIndex t)
{
	dbg_assert_tile(IsTileType(t, TileType::Railway), t);   // <-- 第 40 行
	return (RailTileType)GB(_me[t].m6, 0, 2);
}
```

⇒ 崩溃语义 = **有人拿一个"不是 Railway 类型"的格去调 `GetRailTileType()`**。而 `IsPlainRail()`（`rail_map.h:51-55`）自身就有 `@pre IsTileType(t, TileType::Railway)`（`:48`）并直接转发给它；真正"安全版"是 `IsPlainRailTile()`（`:62-65` ＝ `IsTileType(t, TileType::Railway) && IsPlainRail(t)`）。

#### 18.14.2 现场锚点（日志尾部 770-831，崩溃即刻）

- `770-774`：`CPL-S0-CHK veh=2 tile=1073,139 real=3 order=16 stuck=1 … wait=156→159`（veh=2 机车停在 `1073,139`，订单 `order=16`＝`OT_GOTO_COUPLE`，**久等不动的耦合态**）。
- `775-776`：`CPL-ENTRY veh=2 tile=1073,139 orderType=16` / `CPL-ORIGIN veh=2 origin=1073,139 td=8`。
- `777-781`：`PFD-ACCEPT kind=car-only tile=1089,139 t=3 ordType=17`（目标＝`1089,139` 的 waiting car-only 车底）。
- `782-788`：`CPL-PATHFOUND veh=2 found=1` → `CPL-HEAD best=1089,139 head=1084,139` → `CPL-BEST veh=2 tile=1089,139 td=8 cost=11000` → `CPL-BACK 1089,139→1078,139→1075,139→1073,139` → `CPL-TRACE st=1073,139 tdb=0x101`。
- `790-810`：`REACH cur=…` 逐格（`reach()` 闭包），注意 `799-806`：`1080,140 → 1081,140 → 1089,140`——**一步跨 8 格**，即 `TrackFollower` 跳过了 `1082..1088,140` 的**站台段**。
- `811`：`CPL-TRACE-RESULT st=1073,139 best=8`。
- `812-831`：`RP cur=…` 逐格（`rp()` 递归本体）与 REACH 序列逐字同构，最后一行 `831: RP cur=1081,139 td=8 depth=8 trk=0 bits=0x1`。
- **日志到此戛然而止**：其后没有任何 TRP/CPL-RESERVE/COUPLE-OK/CLEAR/REAP 探针 ⇒ **崩溃发生在该 `rp()` 调用树内**（`rp()` 每格只打一行，而"递归返回 true 之后才做订位"这一步**不打日志**，所以崩在订位步时日志自然停在最后一条 `RP cur=`）。

#### 18.14.3 根因（一句话）

`pbs.cpp` 的 `TryReserveRailTrackdir()` 里，KI-361（round243 §26）加的自有回退门禁写成了

```cpp
if (!success && v != nullptr && IsPlainRail(tile)) {        // <-- 错：IsPlainRail 有 @pre Railway 且内部断言
	const Train *const booker = R3RTrainForLedgerBit(tile, track);
	if (booker != nullptr && booker->index == v->First()->index &&
			HasBit(GetRailReservationTrackBits(tile), track)) {
		success = true;
	}
}
```

注释（`:713-714`）自己写的是「**Only plain rail is special-cased**」⇒ **本意就是 `IsPlainRailTile`（plain rail ＝ Railway 且 Normal/Signals）**，`IsPlainRail` 是写错了一层（漏掉 tile-type 前置检查）。于是只要 `tile` 是**非 Railway 类型**，`GetRailTileType` 立刻在 `rail_map.h:40` 断言。

#### 18.14.4 为什么"现在才炸"（修法甲与它的因果关系）

1. **修法甲之前**：`rp()` 对每一格只调 `reserve_if_present` → `TryReserveRailTrack()`。该函数按 `GetTileType` 分派（`pbs.cpp:747-790`，本轮复核**无任何 `GetRailTileType`/`IsPlainRail` 调用**）⇒ 对任何格都安全。
2. **修法甲之后**：`rp()` 的 5 处调用点全改成 `reserve_dir_if_present` → `TryReserveRailTrackdir()`（`pbs.cpp:699`）⇒ **第一次真正踏进 `:715` 那行**。
3. **触发时机 = 第二趟**：耦合寻路每 tick 重跑同一条路径。**第一趟**每格 `TryReserveRailTrack` 都成功（`success=true`，不进回退）；**第二趟**该位已属本链 ⇒ `TryReserveRailTrack` 返回 **false** ⇒ 进入回退门禁 ⇒ 若这一刻的 `tile` 不是 Railway 类型 ⇒ **断言崩溃**。
4. **哪些格能走到这里**：`rp()` 的守卫只排除**站台**与**车库**（`!IsRailStationTile(t) && !IsRailDepotTile(t)`），而
   - **路点（`StationType::RailWaypoint`）不被 `IsRailStationTile` 覆盖**（后者要求 `IsRailStation`＝`StationType::Rail`）⇒ 路点格会被送进来；
   - **道口**（`TileType::Road` + `IsLevelCrossing`）不被任何守卫排除，且 `GetTileTrackdirBits` 对道口返回非零轨道 ⇒ 也会被送进来；
   - **隧道/桥**（`TileType::TunnelBridge`）同理。
   这四类里任何一个"已被本链订下"的格，都会在第二趟把 `IsPlainRail(tile)` 变成 `GetRailTileType(非 Railway 格)` ⇒ `rail_map.h:40`。
   本场景 `1073..1093,139/140` 这条走廊正是"库/站台/路点混排"的一段（`1082..1088,140` 是站台段，`1089,139` 是 waiting 车底所在），路径上出现路点/道口完全正常。
5. **同一行还是引擎侧的潜在地雷**：`yapf_rail.cpp:346`（YAPF 预留循环）与 `train_cmd.cpp:13263/13300`（R3R safe-reserve）也都调 `TryReserveRailTrackdir`，`ReserveSingleTrack()` 只对 `IsRailStationTile` 走了站台分支（路点/道口/隧道桥仍会落到 `TryReserveRailTrackdir`）⇒ 本轮一行修法**同时**排掉这几条路径上的同一颗雷。

#### 18.14.5 修法（一行，最小改动）

`src\pbs.cpp`：

```cpp
-	if (!success && v != nullptr && IsPlainRail(tile)) {
+	if (!success && v != nullptr && IsPlainRailTile(tile)) {
```

- **语义**：与注释原意逐字一致（"只有 plain rail 走这条回退，其它 tile 类型保持原语义"）⇒ 路点/道口/隧道桥/站台格的 `success` 保持 `TryReserveRailTrack` 的原值，其后的 `if (success && HasPbsSignalOnTrackdir(...))` 与 `return success` 不变。
- **无新行为**：`IsPlainRailTile` ＝ `IsTileType(Railway) && IsPlainRail`，对 plain rail 格与旧码**返回同一个值**；差别仅在"非 Railway 格"由"崩溃"变成"正常跳过回退"。回退体内部也只对 plain rail 有意义（`GetRailReservationTrackBits` 的 `@pre` 同样是 Railway）。
- **顺带守住**：修法甲在路点/道口/隧道桥上的 Trackdir 级订位照旧发生（`TryReserveRailTrack` 一步），只是永不点亮非 PBS 信号（`HasPbsSignalOnTrackdir` 结构性排除）。

#### 18.14.6 构建自证（第 243 轮续五十九）

- 复用既有 `_tmp_inc_build.cmd`（**未新建任何 `.cmd`**）；仅改 `src\pbs.cpp`（纯 `.cpp`，未碰任何 `*.h`）⇒ 护栏判增量合法。
- `build\R3R_incbuild.guard.log` = `GUARD: incremental is safe`；`build\R3R_incbuild.log` 尾部 `[2/3] Building CXX object CMakeFiles\openttd_lib.dir\src\pbs.cpp.obj` → `[3/3] Linking CXX executable openttd.exe`（Linking 恰 1 次；`error C[0-9]` / `fatal error` / `FAILED:` / `build stopped` 零命中）。
- `build\R3R_incbuild.done` = `EXIT_CODE=0`（@21:17:55）；时间戳链 `src\pbs.cpp` @21:13:59 → `pbs.cpp.obj` @21:16:16 → `build\openttd.exe` @2026-10-16 21:17:44（51 868 160 B）逐级变新。
- `read_lints` 0 条。

#### 18.14.7 复测判据

1. **崩溃消失**：同一 `1073,139 → 1089,139` 耦合场景持续跑（含路点/道口）不再在 `rail_map.h:40` 断言。
2. **正判据不回退**：`RP cur=` 逐格序列、`CPL-*` 序列与修复前逐字相同；`COUPLE-OK` 照常出现；KI-349 的绿灯行为（`reserve_dir_if_present` 点绿）不受影响。
3. **负判据**：路点格/道口格/隧道桥格上的订位沿用 `TryReserveRailTrack` 语义（无"该订没订"，`RESV-AUDIT-STRAY` 不上升）。
4. **负判据**：plain rail 格上的 KI-361 自有回退仍生效（自身已订位的 plain rail 格不得被判成失败/产生孤儿位）。

#### 18.14.8 仍未做

1. 游戏内复测（判据见上）。
2. 回归根因：`rp()` 每 tick 在**同一批格**上重复订位（第二趟起 `TryReserveRailTrack` 必返 false）本身是"每 tick 重跑耦合寻路"的固有开销，本轮未动。
3. 未改 `reserve_dir_if_present` 的守卫口径（站台/车库已排除；路点/道口/隧道桥**设计上就该被订**，故不排除）。

---

### 18.15 【实机复测·通过】KI-370 断言消失 + 修法甲收效 —— 原崩溃场景跑通（第 243 轮续六十，2026-10-16；**零源码改动、零构建**）

**取样有效性**：`d:\sourcecode of JGRPP\build\R3R_debug.log` = **12 632 行 / 919 520 B / mtime 2026-10-16 21:27:07**，晚于 `build\openttd.exe` @**21:17:44**（第 243 轮续五十九 KI-370 修复版，51 868 160 B）⇒ **由本轮 exe 产生，可用**。

**玩家原话**：「看起来，这个流程也跑通了，你确认一下，然后下一轮就可以继续下一个问题了。」

#### 18.15.1 关键计数（`Select-String` 实测）

| 判据 | 探针/模式 | 读数 | 判定 |
|---|---|---|---|
| KI-370 判据① | `rail_map\|Assertion\|assert` | **0** | **通过**（原断言崩溃不再复发） |
| 原崩溃场景复现 | `1073,139` | **455 行** | 同场景确实被跑到 |
| KI-349 判据① | `REDSIG veh=23` | **0 组** | **通过**（不再"恒红"） |
| — | `REDSIG`（全部） | 10 行 = **2 组，均为 veh=11** | 见 18.15.3，属瞬态 |
| 挂接收尾 | `COUPLE-OK` / `COUPLE-FAIL` | **9 / 12** | 正常（重试后成功） |
| KI-346/KI-345 判据①③ | `RESV-AUDIT-STRAY` | **0** | **通过**（无无主位） |
| — | `RESV-AUDIT-SUMMARY` | 1 条：`tick=6930 reservedTiles=38 owned=38 strayTiles=0 reaped=0 pendingStray=0` | owned=reserved、stray=0 |
| — | `RESV-REAP` | 0 | 无待清孤儿 |
| 第 126/127 轮口径 | `RESV-NOBOOK` / `RESV-GHOST` | **1 / 2** | `RESV-NOBOOK veh=22 tile=1085,136 order=17 caronly=0 reason=parked-waiter` + `RESV-GHOST waiter veh=22 tile=1081,136 track=0` → `RESV-GHOST-SUMMARY waiter veh=22 cand=1 freed=1`（停放等待者不铺路、旧位被扫净） |
| 第 180 轮判据④ | `LEGACY-ARTIC-DEARTIC` | 0 | 本会话无旧档焊死铰接链，符合预期 |
| 第 242 轮探针 | `SPD-DROP` / `LOCO-MOVE` | **50 / 163** | 探针工作正常 |

#### 18.15.2 原崩溃场景收尾（逐行锚点）

- `539-773`：`CPL-S0-CHK veh=2 tile=1073,139 real=3 order=16 stuck=1 fp=0 prog=0 wait=0 … wait=159 parked=0 stop=0 track=01 dtile=287807` —— 与第 243 轮续五十九读到的**崩溃日志末段逐字同形**（同一 `order=16`、同一 `stuck=1`、同一 `dtile=287807`）。
- `1019`：`COUPLE-OK loco=2 rear=3 consist=3 co=1 real=6 type=1 tx=1083 ty=139 x=17336 y=2232` ⇒ **崩溃点（wait≈159 之后的订位步）现在正常走完，机车 2 成功挂上车底 3**。
- 其余 8 次 `COUPLE-OK`：`2351 loco=11 rear=12 consist=12@@1083,140`、`3904 loco=2 rear=3 consist=10@@964,133`、`4558 loco=11 rear=12 consist=19@@964,133`、`10067 loco=23 rear=24 consist=22 co=0 real=11@@1089,136`、`11111 loco=2 rear=10 consist=10@@1083,139`、`11743 loco=11 rear=19 consist=19@@1083,140`、`12492 loco=23 rear=24 consist=22 co=0 real=1@@964,133`、`372 loco=23 rear=24 consist=22@@964,133`。

#### 18.15.3 两处 `REDSIG` 均为瞬态（**不等于 KI-349 的恒红滞留**）

第一处（`11384-11388`，veh=11）：

```text
11375-11383  EXTEND-BACKOUT veh=11 origin=1081,137/0 stop=1078,137/0 next=1077,137 reason=choice-reserved err=0 owner=23 whole=1 track=0 need=0x19 resv=0x1 led=23
11384        REDSIG veh=11 old=1081,137 new=1080,137 dir=0 chosen=0x1 i=0 prog=0 spd=0 wait=0 stuck=1
11385        REDSIG   tdbits=0x1 reach=0x1009 sigs=0x1717 red=0x11 bits=0x1
11388        REDSIG   beyond=1079,137 valid=1 plain=1 resv=0x0 veh=0
11389        SPD-DROP veh=11 tile=1081,137 prev=15 order=6 real=2 stuck=1 fp=0 prog=9 wait=0 parked=0 stop=0 track=01 dtile=285745
11390        DEPOT-ARR veh=11 spd=0 real=2(6) curType=6 …
11392        TPR-WALK veh=11 mf=1081,137 origin=1081,137 td=0 okay=1 exit=0 new=1080,137 reach=1 spd=0 one=0 look=0
11393        CRT veh=11 order=6 dir=5 origin=1078,137 td=0 found=1
11394        TRP veh=11 mf=1081,137 real=2 type=6 dest=285745 spd=0 fp=0 look=1 one=0 mstuck=0 fto=0 => ok=1 res=1
```

⇒ 前方格被 **veh=23 持有**（`owner=23`），veh=11 正常 `EXTEND-BACKOUT` 退避；随后同 tick 内即 `ok=1 res=1` 重新订位通过，并沿 `1077,137 → 1076,138 → 1075,139 → 1074,139 → 1073,139`（`:11549`）→ `1082,140`（`:11643`）**越过该信号继续运行**。

第二处（`12318-12322`，veh=11 @`1032,136`）：紧随其后 `12329 TRP veh=11 … => ok=0 res=0`、`12347 TRP veh=11 … => ok=1 res=1` ⇒ 同 tick 自行恢复（该处 `beyond=1030,136 valid=1 plain=0`，即 beyond 格非 plain rail）。

**结论**：`REDSIG` 探针在「前方格被别的车持有 / 自身正在重订」时会打印，属**预期的瞬态**；KI-349 的判据是 **`REDSIG veh=23` 且机车永久停在自订位前**，本例 **零命中**。

#### 18.15.4 本轮判定

- **KI-370（`rail_map.h:40` 断言崩溃）**：**实机复测通过** —— 同场景 `1073,139` 跑满 `wait=0..159` 且日志继续到 12 632 行，`rail_map/assert` 零命中；`strayTiles=0`（判据③）；plain rail 上的 KI-361 回退照旧（判据④，`COUPLE-OK` 9 次为间接证）。
- **修法甲（KI-349 / KI-361 绿灯）**：**判据①②③④ 通过** —— `REDSIG veh=23` 消失（判据①）、`RP cur=`/`CPL-*` 与 `COUPLE-OK` 照常（判据②）、`RESV-AUDIT-STRAY=0` + 路点/道口订位语义无回归（判据③）、`RESV-NOBOOK`/`RESV-GHOST` 口径正常（判据⑤旁证）。
- **未覆盖**：判据④「非 PBS 普通信号颜色不变」仍只有结构性证据（`HasPbsSignalOnTrackdir` 排除非 PBS；`REDSIG` 行 `pbs=1`）；可视绿灯未取图。KI-349 场景可视复核留待有需求时再取。

#### 18.15.5 下一轮方向预告（玩家 2026-10-16 指定）：**联机同步（desync）**

玩家原话：「下一方面，我希望能够针对同行容易爆雷的联机同步问题动刀。」⇒ 下一轮改线到多机同步审计。**本轮不动手**，仅登记首轮落点候选（待下一轮开工时按"先落盘、后取证"逐条展开）：

1. **R3R 新增内存态清单**（均在文件内 `static`，**NOSAVE**，是 desync 的首要嫌疑）：`_r3r_consist_resv`（KI-203 记账，影响"该不该释放预留"）、`_r3r_couple_dest_idle`（KI-193 的 500 tick 到点计时器，影响是否 `R3RUnpairCoupleTargets`）、`_r3r_waiter_purged`/`_r3r_waiter_nobook_logged`、`_r3r_resv_audit_due`（第 243 轮续十二的脏标记）、`_r3r_last_dbg_tick` 等探针闸门表。
2. **关键问题**：这些表**是否被任何"改变仿真的决策"读取**？若读（如 KI-193 计时器、KI-203 释放判定），则"两侧表内容不同 ⇒ 行为不同 ⇒ desync"；若只用于打日志则安全。需逐表列出读点并分类（决策 / 仅日志）。
3. **其它候选面**：迭代顺序敏感（`std::unordered_map`/`btree_map` 遍历后**按序遍历**做决定）；`_tick_counter` vs 帧计数；命令参数/返回值是否双端一致；R3VP/R3SG 存档块在"联机中途加入/读档"下的一致性；`Randomizer` 使用点。
4. **取证手段**：引擎自带 desync 检查（`-d desync=3` 级别）与 `desync.log`/`frame` 对比；必要时给上述表加"仅日志"的哈希探针以便双侧对拍。


