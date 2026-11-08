# 第 394 轮（2026-11-07）：玩家三条新报告（车库→车站挂接 / 挂接预留越信号灯 / Ctrl+左键铰接组与「没有电力」）

状态：**刚接到报告，已按铁律落盘，取证未完**。零源码改动、零构建。
前置：KI-562（同格双链头）玩家复测通过，已于 2026-11-07 结案。

## 零、玩家原话（逐字）

> 1，车库内机车去挂接车站的车底仍然存在问题
> 2，挂接寻路会连续铺设预留越过所有信号灯，这个问题曾经修复过，但没修好
> 3，在对铰接式执行ctrl+左键操作的时候，总是只能对链的第一节铰接式组生效。而且，有时候，不知道触发了什么条件，它会无法启动，显示“没有电力”，对，就是“没有电力”

**编号对应（本条为唯一权威，KI 条目见 `R3R_KNOWN_ISSUES.md` 第 394 轮小节）**

| 玩家第几条 | 对应历史条目 | 历史修复轮次 | 本轮定性 |
|---|---|---|---|
| 1 车库→车站挂接 | **KI-561** | 第 393 轮 D-1g（`R3RIsParkedWaiting` 的 `depot-paired` 支单源化） | 复测失败·待取证 |
| 2 预留越所有信号灯 | **KI-563** | 第 393 轮 C-3=A（`R3RReservePathSequence` 失败分支保留部分预订） | 复测失败·**修法不完整**（见 §三） |
| 3A Ctrl+左键只对第一节铰接组 | **KI-558** → 新 **KI-565** | 第 391 轮（只打散并翻转"被点车厢所属的那一组"） | 新登记·待取证 |
| 3B 「没有电力」 | 新 **KI-566** | 无（首次登记） | 新登记·待取证 |

⇒ P1、P2 玩家用了「仍然」「曾经修复过」，是**未修透**；P3 是第 391 轮刻意"只做单组"的口径残留（REG-394-C）。**不要当全新问题重新发明修法**。

## 一、P1：车库内机车去挂接车站的车底「仍然存在问题」（KI-561）

**上一修（第 393 轮 D-1g）到底改了什么**

- 现场（第 393 轮）：`veh=51` 在库内、订单 `idx=7 type=16 dest=9`（去车站 9 挂车），对端 `veh=6` 在 18 格外的 `44,116` 原地等挂。每 ~90 tick 循环 `RESV-BEGIN` → `TRP ok=0 res=0` → `RESV-NOBOOK reason=depot-paired`，坐标与 `spd` 不变、`mstuck=0`。
- 真凶：`R3RIsParkedWaiting()`（`train_cmd.cpp:18141`）的 `depot-paired` 支（原 `:18252-18255`）只判 `r3r_goto_couple && r3r_in_depot && r3r_partner != nullptr`，**不判对端在哪**；而它守护的出库口（原 `:18439`）是库内列车订路的唯一门 ⇒ 对端在库外时该车永久卡库。
- 修法（已落地）：门追加 `&& R3RDepotCoupleAppliesToThisDepot(consist)`，新增该函数（判据 `pend || (OT_GOTO_COUPLE && GetCoupleIsDepot() && dest == GetDepotIndex(tile))`），与库内挂接块 `couple_targets_this_depot` 共用同一判据（单源）。

**本轮为什么可能仍不行（猜测根因，待验证）**

- **G1-2′（最可疑）**：`R3RDepotCoupleAppliesToThisDepot()` 只认"**挂接到本库**"（`GetCoupleIsDepot()` 为真且 `dest` 是本库）。玩家 P1 是"库内机车去挂**车站**的车底" ⇒ 该函数返回 **false** ⇒ 门**不再关死**，这正是 D-1g 的设计目标。⇒ **若现场仍卡，卡点必然搬到了别处**，最可能：
  - G1-2′-a：库内订路还有第二道门（`CheckTrainStayInDepot()` 的出库落地动作 `train_cmd.cpp:15421-15435` 之后）；
  - G1-2′-b：`RESV-NOBOOK` 的 `reason=` 从 `depot-paired` 换成了别的（说明门换位而非消失）；
  - G1-2′-c：出库成功但**挂接寻路**摸不到车站车底（那是 KI-340/第 232 轮"命中端 vs 拼接端角色错位"同族，与库内门无关）。
- G1-3′：`pend`（`r3r_pending_depot_couple`）在"目标在车站"时恒 false，若某条路径仍以 `pend` 为"接管"凭据，会再次落回旧行为。

### 取证结论 2（代码实证，2026-11-07）：D-1g 的两道门**都已经放行**，卡点必在下游

逐支核 `src\train_cmd.cpp:18185-18327` 的 `R3RIsParkedWaiting()`（`TryPathReserveWithResultFlags()` 里唯一的路线预订闸门，`:18495-18505`）：

| 支 | 条件 | 「库内机车 → 车站车底」是否命中 |
|---|---|---|
| `parked-no-pair`（`:18214`） | `goto_couple && partner==nullptr && **!IsRailDepotTile**` | **不命中**（在库内 ⇒ `!in_depot` 为假）；但**出库之后立刻命中**（见下） |
| `depot-paired`（`:18308`） | `goto_couple && in_depot && partner!=nullptr && R3RDepotCoupleAppliesToThisDepot()` | **不命中**（`R3RDepotCoupleAppliesToThisDepot()` 的两支都要求"目的地是本库"，车站挂接两条都假）⇒ D-1g 生效 |
| `parked-waiter` / `parked-caronly`（`:18316/:18321`） | `cur_speed==0` 且（`OT_WAIT_COUPLE` / 纯车底） | 机车不命中 |

库内挂接块（`:22432` 起，守卫 `track==TRACK_BIT_DEPOT && IsEngine() && R3RChainFullyInDepot()`）在 `couple_targets_this_depot==false` 时走 `else if (!current_order.IsType(OT_GOTO_COUPLE))`（`:22510`）⇒ 订单是 GOTO_COUPLE ⇒ **整块空转、不挡**。`CheckTrainStayInDepot()`（`:15252`）的到库守卫（`:15278`）要求 `GetCoupleIsDepot()` ⇒ 车站挂接时也**不 return true**。

⇒ **门这一层已通，玩家仍报"有问题"意味着卡点在下游。**

**G1-4（新猜测根因，最可疑）：出库后第一格即被 `parked-no-pair` 拦住。**
`R3RIsParkedWaiting()` 第一支对库格做了豁免（`!r3r_in_depot`），但豁免**只覆盖库格本身**。机车一出库（`r3r_in_depot=false`）而配对锁还没建立（`R3RGetCouplePairPartner(v)==nullptr`）时，该支立刻成立，判据来源是 `_r3r_pair_verdict` 缓存（`:18236-18243`）：
- 缓存说无候选（`none` / `refused` / 尚未扫过）⇒ `reason="parked-no-pair"` ⇒ **拒绝订路**，同时 `lookahead.reset()`（`:18263-18273`）⇒ `GetCurrentMaxSpeed()` 归零 ⇒ 车**停在库门口/半路**；
- 而配对锁建立的前提是对方车底已处于 `OT_WAIT_COUPLE` 且通过 `R3RIsCoupleTarget()`（`:18336` 注释自陈：等待方还停在 `GOTO_STATION` 时"扫描静默 continue"）。

⇒ 与玩家 P1 的现象一致：车底在车站还没进入 `WAIT_COUPLE`（或装货/进站途中）时，机车出库后即被冻在门口，等车底进入 `WAIT_COUPLE` 后才恢复。**判据**：日志出现 `RESV-NOBOOK ... reason=parked-no-pair` 且 `tile` **不在库格** ⇒ G1-4 成立（若 `tile` 在库格 ⇒ 回来查 `depot-paired`，说明 D-1g 未生效）。

### 取证结论 4（玩家回填 Q1-1=a 后的重判，2026-11-07）：**G1-4 已被证伪**，卡点收敛到"库内根本不放行"

**Q1-1 = a（机车在库里完全不动）** ⇒ 车从来没有离开库格 ⇒ `!r3r_in_depot` 从未为真 ⇒ **G1-4（出库后被 `parked-no-pair` 拦）不可能是本案成因，作废**。

逐支复核 `R3RIsParkedWaiting()`（`src\train_cmd.cpp:18185-18327`）对"库内 GOTO_COUPLE 机车 + 目的地车站 + 已配对"的判定：

| 行 | 判据 | 结果 |
|---|---|---|
| `:18210-18212` | `r3r_goto_couple = IsType(OT_GOTO_COUPLE)`；`r3r_in_depot = IsRailDepotTile(tile)`；`r3r_partner = R3RGetCouplePairPartner()` | 均 true/非空（Q1-3 已确认配对条件匹配） |
| `:18214` | `parked-no-pair`：要求 `partner == nullptr && **!in_depot**` | **不命中**（in_depot=true） |
| `:18308` | `depot-paired`：要求 `in_depot && partner != nullptr && R3RDepotCoupleAppliesToThisDepot()` | **不命中**（见下） |
| `:18314` | `cur_speed != 0` ⇒ 放行 | 库里静止，继续 |
| `:18316/:18321` | `OT_WAIT_COUPLE` / 纯车底 | 机车 + GOTO_COUPLE，不命中 |

`R3RDepotCoupleAppliesToThisDepot()`（`:18160-18183`）两支**都要求 `GetCoupleIsDepot()`**（目的地必须是**库**）：
- 支一 `:18167-18171`：`OT_GOTO_COUPLE && GetCoupleIsDepot() && ToDepotID()==this_depot` ⇒ 本案目标是**车站**，`GetCoupleIsDepot()` 为假 ⇒ false；
- 支二 `:18177-18182`：`IsEngine() && r3r_parked` + 有效表里的 pending 也要求 `GetCoupleIsDepot()` ⇒ 假。

⇒ **`R3RIsParkedWaiting()` 这条路对本案彻底放行，D-1g 是有效的。卡点不在它。**

**新的唯一嫌疑落点 = `CheckTrainStayInDepot()`（`:15252-15444`）。** 它每 tick 返回 true 车就留在库里，且它内部有 6 个 `return true` 出口，其中两个能造成**永久**不动：

1. **`:15415`（最可疑）**
   ```cpp
   if (seg_state == SigSegState::Path && !TryPathReserve(v) && v->force_proceed == TFP_NONE) {
       MarkTrainAsStuck(v);
       return true;
   }
   ```
   出库前必须先订到路线；`TryPathReserve()` 失败 ⇒ `MarkTrainAsStuck` + 留在库里，**每 tick 重试、每 tick 失败 ⇒ 表现就是"完全不动"**。而车底停在 18 格外的车站（`track=0x1` 普通轨道），YAPF 的挂接终点是"车底所在格"，中间要穿过库口与站场——**若挂接寻路的终点判据要求对方处于 `WAIT_COUPLE` 且在场，而对方恰好满足，则失败原因只能是自己这一侧的起点/库格处理**。
2. `:15292`（次可疑，与 KI-566 同源）
   ```cpp
   if (v->gcache.cached_power == 0 && !R3RIsCarOnlyFormation(v)) { vehstatus.Set(Stopped); return true; }
   ```
   一旦置 `Stopped`，**下一 tick `TrainLocoHandler` 直接早退，本函数根本不再执行** ⇒ 永久冻结、且状态行正好显示「没有电力」（`vehicle_gui.cpp:4711` 同一判据）。⇒ **KI-561 与 KI-566 可能是同一条死锁的两个面**。

其余出口均为**有界**、不致永久卡死：`:15278` 到库守卫（`GetCoupleIsDepot()` 假，不命中）、`:15301` `OT_WAITING`、`:15308` `OT_WAIT_COUPLE`（订单不匹配）、`:15315` `reverse_distance`（递减）、`:15326` `wait_counter < 37`（37 tick 后自然放行）、`:15342` 回本库（订单不匹配）。

**下一组取证目标**：`TryPathReserve()` 的出库入口（`:18439` 一带，注释自陈"库内列车订路的唯一入口"）→ `TryPathReserveWithResultFlags()`（`:18495` 一带）在库内场景下失败的具体闸门；并核对 `TrainLocoHandler` 里 `CheckTrainStayInDepot()` 与 `Stopped` 早退的先后顺序（决定 `:15292` 死锁是否可达）。

**修法方向（待 Q1-1 定性，暂不改码）**：
- 若 G1-4 成立 ⇒ 把「库格豁免」扩展为「**已在执行 GOTO_COUPLE 且尚未到达作业点**的车不拦」（即只在"已经停在作业点等配对"时拦），或给 `parked-no-pair` 增加"距离目的地仍很远 ⇒ 允许赶路"的渐进放行；
- 若现象是 (c) 到站挂不上 ⇒ 与 KI-340 同族（命中端 vs 拼接端角色错位），与本门无关。

**必须由玩家补的现象口径**：见 §五 Q1-1～Q1-4。四种表现的根因面完全不同，不先定性就是盲改。

## 二、P2 之外先澄清：KI-563 与第 393 轮 C-3=A 的真实关系

（见 §三，此处不重复。）

## 三、P2：挂接寻路连续铺预留、越过所有信号灯（KI-563，**修法不完整**）

**玩家期望（第 393 轮 C-3 的原话口径）**：完全照原生 —— 遇信号或他人预留边界即停，下一个 tick 续订；不再一次性铺到底。

**第 393 轮实际改的是哪一段（关键）**

- 原文（KI-563 状态行）：「落点：`yapf_rail.cpp` 的 `R3RReservePathSequence()` **失败分支**由"整批回滚 + `booked=0` + `return false`"改为"`booked>0` 即保留已订格、打 `CPL-RES-PARTIAL`、`YapfNotifyTrackLayoutChange` 后 `return true`"；调用点部分预留时把 `SetReservationTarget()` 从 `r3r_resv_path.back()` 收缩到 `r3r_resv_path[booked-1]`。」
- ⇒ 改的是**订不上时怎么退**，**没改"订得上时订多远"**。

**为什么这是必然失败（REG-394-B，本轮首次说清）**

挂接预留是两遍法：①摸路只读，把整条物理路线记进 `r3r_resv_path`（`yapf_rail.cpp:1542`）；②`:1610-1614 SetReservationTarget(...r3r_resv_path.back()...)` + `R3RReservePathSequence(r3r_resv_path, ...)` **按序一次订完**。只要沿途没有任何他人预留冲突，`booked` 就等于整条路径长度，**一次铺到底、穿过所有信号灯** —— 这与"失败时保留几格"完全正交。所以：

> **越灯不是回滚过度，而是正向从不截断。** C-3=A 修的是回滚，故复测必然仍是"铺到底"。

**修法方向（待拍板，落点同一函数）**

- 正向截断：在 `R3RReservePathSequence()` 里增加"**遇到信号灯（或他人预留边界）即停**"的条件 —— `booked` 停在信号灯**前一格**，`SetReservationTarget` 收缩到该格，返回 true（表示本 tick 有进展），下 tick 从该格续订。
- 不碰 `TryReservePath`（非单段分支 `:1637` 走的原生实现本就逐节点延伸、冲突时 `EXTEND-BACKOUT`，不存在一次铺到底）。
- **需先确认**：Q2-2（一次铺完 vs 每 tick 推进几格）—— 若现场是后者，则病在别处（原生 `TryReservePath` 的 `max_reserve`/路径长度上限），修法完全不同。

### 取证结论 1（代码实证，2026-11-07）

- `src\pathfinder\yapf\yapf_rail.cpp:632-693`（`R3RReservePathSequence`）：`for (i = 0; i < steps.size(); ++i)` → `ReserveSingleTrack(stile, stdir)` → **只有 `res_fail_tile != INVALID_TILE` 才 break**，成功则 `++booked` 走到底。**全函数没有任何 `HasSignalOnTrack` / `HasSignalOnTrackdir` 判断** ⇒ 成功路径零截断，一次订满 `r3r_resv_path` 的每一格。**P2 的"连续铺设越过所有信号灯"在代码上成立且必然**。
- 调用点同一文件 `:1648-1658`：`SetReservationTarget(pNode, r3r_last...)` 先把目标挂到**路径最后一格**，再 `R3RReservePathSequence(...)`；KI-563 的收缩只发生在 `reserved && booked>0 && booked<size`（即**订失败**）时 ⇒ 成功时不收缩。
- 引擎自己的口径：`src\pbs.cpp:1380-1381`
  ```cpp
  /* Non-pbs signal? Reservation can't continue. */
  if (IsTileType(tile, TileType::Railway) && HasSignalOnTrackdir(tile, trackdir) && !IsPbsSignal(GetSignalType(tile, TrackdirToTrack(trackdir)))) break;
  ```
  ⇒ 引擎只在**非 PBS 信号**处停；**PBS 信号本身允许被预留穿过**（这是原生定义，不是 bug）。
- **修法因此不是"遇灯即停"那么简单**：原生真正的粒度是「**一次只订一段，列车走一段再延伸**」（`ExtendTrainReservation` 逐段 / `TryReservePath` 只处理一段），所以它不会一次占满全线。R3R 的错在于 `rp()` 把**整条到会合点的物理路径**都收进 `r3r_resv_path` 再一把订完。正确修法 = 把 `r3r_resv_path` 截到**第一个信号（含 PBS 信号）或第一个特殊边界**为止，`booked` 到该处即止，下一 tick 由 `TryReservePath` 重新摸路续订。

- **REG-394-E（新疑点，必须先问清，否则会改错）**：若玩家现场的信号**全是 PBS 信号**，那"预留穿过 PBS 信号"在原生语义下是**合法的**（PBS 信号的含义就是"预留即通行"）。此时玩家真正受害的是「一次占满整条线 ⇒ 对向/后续列车长时间进不来」，修法应是"**只订一段**"（长度问题），而不是"禁止穿过 PBS 信号"（合法性问题）。若现场有非 PBS 信号而 R3R 仍越过，那才是**违反原生**的硬 bug。⇒ 需 Q2-1 补一句"信号灯是普通信号还是 PBS（路签）信号"。

**判读锚点**：`RP cur=` 连续行数、`CPL-RES-PARTIAL veh= booked= stop=x,y td=`、`CPL-RES-OK veh= total= tiles=`（**本次总格数=全线长度即为 P2 实证**）、`CPL-TRACE-RESULT`、`RESV-EXT-PRE ... doRes=1` / `EXTEND-BACKOUT ... bad=1`（普通路径的逐段形态，作对照基线）。

## 四、P3：Ctrl+左键对铰接式「只对第一节铰接组生效」+「没有电力」（KI-565 / KI-566）

### P3-A：只对第一节铰接组生效（KI-565）

**第 391 轮 KI-558 的原始设计（玩家当时的原话）**

> 「原生 OpenTTD（含 JGRPP）对着非铰接式非双头的列车执行 ctrl+鼠标左键，可以反转车厢的图像……现在，我想要对铰接式也能执行这样的操作。初步推测机制如下：对铰接式进行此操作，反转该车厢的 vehicleflag::flip，然后打散它为假铰接式（如果是真铰接式），然后反转它的 position_in_articulated_part」

**实际实现**：`CmdReverseTrainDirection(reverse_single_veh=true)` 在 `src\train_cmd.cpp:5683-5685` 原本只对「`IsMultiheaded()` 或引擎带 `VehicleCallbackMask::ArticEngine`」早退；修法 = 定位**被点车厢所属的整组** → 真铰接先 `R3RDearticulateOneGroup()` 打散（**只打散这一组，不连带整链**）→ 组内每节取反 `Flipped` → 组内置 `ArticGroupFlipped`（`VehicleRailFlag = 29`）→ `newgrf_engine.cpp` 的 NewGRF 变量 `0x4D` 在 `HasDearticulatedGroupRole() && (IsSegmentFlipped() != IsArticGroupFlipped())` 时交换 `artic_before/artic_after`。

⇒ **"只处理一个组"是当时刻意的口径，不是 bug**。玩家现在说"总是只能对链的第一节生效"，两种可能：

- **口径差**：玩家期望按下一次 Ctrl+左键就把**整列所有铰接组**都翻转（像非铰接列车整列反图那样）；现有实现只翻被点那一组 ⇒ 表现为"只有一节组变了"。**这是 REG-394-C，需拍板**。
- **定位 bug（猜测 G3-1/G3-2）**：若玩家点的是第 2/3 节、甚至最后一节，结果仍是**第一节**的组变化 ⇒ 定位代码取错了对象（典型错法：用 `v->First()` / `GetFirstVehicleInChain()` / 按 `articulated` 父车反查时退回链头），那才是真 bug。

**判读锚点**：`ARTIC-FLIP` 探针应打出被处理组的车号（`ARTIC-FLIP head=%d n=%u real_artic=%d flipped=%d seg=%d`）；对比玩家点击的车号。若 `ARTIC-FLIP head=` 恒等于链上第一个铰接组的组头 ⇒ 定位 bug 成立。

**取证结论 2（代码实证，2026-11-07）：KI-565 根因已定 —— 组头回溯循环判错了对象，是确定性 bug（不是口径差）**

先排除 UI 侧：`src\depot_gui.cpp:1130` 取 `result.wagon`（Train 类型就是被点的那一节），`:1143` `this->sel = v->index;`；`:1755-1756` 用 `Vehicle::Get(sel)->index` 发 `ReverseTrainDirection(..., true)`。⇒ **GUI 传的确实是被点车厢，不是链头**。

真凶在 `src\train_cmd.cpp:5712-5715`（`reverse_single_veh == true` 分支）：

```cpp
Train *group_head = v;
while (group_head->Previous() != nullptr && group_head->Previous()->IsArticGroupMember()) {
    group_head = group_head->Previous();
}
```

判的是 **`Previous()` 是否铰接组成员**，而不是 **`group_head` 自身**。因 `IsArticGroupMember()`（`train.h:212-215`）= `IsArticulatedPart() || flags.Test(ArticGroupMember)`，链上组成员永远"前一个也是成员"，于是无论点哪一节，循环都会一路退到**链上第一个铰接组之后紧邻的那节**（即第一个组的组头/组前车）⇒ 与玩家「总是只能对链的第一节铰接式组生效」**逐字吻合**。

修法（一行）：

```cpp
while (group_head->IsArticGroupMember()) group_head = group_head->Previous();
```

**必须同时收紧的连坐点（否则修完会连翻两组）**：`:5718`

```cpp
is_group = group_head->Next() != nullptr && group_head->Next()->IsArticGroupMember();
```

改成"同一组"判据（如 `group_head->Next()->IsArticGroupMember()` 且 `group_head->Next()` 的组头仍是 `group_head`，或按"沿 Next 数到第一个非成员为止、成员属于同一 artic 组"判定），否则原本"只退到第一个组"的副作用反而保护了 `is_group`，修正后会跨组。

**状态**：根因已定、修法已定，**尚未改码**（等 Q3-5 口径拍板：若玩家要的是"整列所有组一起翻"（A），则本修法不够，需另加遍历；若只要"点哪节翻哪组"（B），本修法即完成）。

### P3-B：「没有电力」（KI-566）

**文案已定位（已实证）**：`src\lang\simplified_chinese.txt:4609`

```
STR_VEHICLE_STATUS_TRAIN_NO_POWER : {RED}没有电力
```

- 这是**车辆状态栏**串（`GetVehicleStatusString` 系的 `Train` 分支），**不是** `CmdStartStopTrain` 的命令报错串（后者另有 `STR_ERROR_TRAIN_START_NO_CATENARY` 等）⇒ 含义 = 「该列车当前动力统计为 0 / 动力轨不匹配」，**不是电力系统坏了**。
- 疑点 REG-394-D：玩家同时说"无法启动"和"显示没有电力"，二者可并存（状态栏 + 不变速），也可能是一个现象的两面。需 Q3-3/Q3-4。

**猜测根因（待验证）**

- G3-3（与 KI-565 同源，最可疑）：Ctrl+左键触发的"打散（`R3RDearticulateOneGroup`）/ 重铰接（`RearticulateChain`）"过程中，链路 `ConsistChanged()` 缺失或顺序错 ⇒ `gcache.cached_power` 未重算 ⇒ 0 ⇒ 状态栏「没有电力」且 `cur_speed` 恒 0（表现为"无法启动"）。
- G3-4（次可疑）：打散时把引擎位（`FrontEngine` / `IsEngine`）清掉但没有按 `R3RDearticulateOneGroup` 的烘焙规则补功率，导致整链 `cached_power == 0`。第 225 轮 KI-332 记过完全相同的手法（`R3RDearticulateOneGroup` 负责"组头 `SetArticGroupHead` + 烘焙均分余数、成员 `ClearArticulatedPart/SetArticGroupMember` + 烘焙"）⇒ 若批量路径绕过它直接改位，功率就丢了。
- G3-5（已排除方向）：第 391 轮的 `ArticGroupFlipped` 与 `IsSegmentFlipped()` 的 XOR 只影响 NewGRF 变量 `0x4D` 的镜像，**不会**让功率变 0 ⇒ 不列为"没有电力"成因。

**判读锚点**：`dump_vehicle <id>` 的 `tf:` 段（`A`=真 artic、`H`=ArticGroupHead、`M`=ArticGroupMember）、`CHAIN-ATTRS` 的 `pow=`、`ConsistChanged` 调用点。判据 = 操作后每节组头 `H` 与烘焙值都在、`pow != 0`。

**取证结论 3（代码实证，2026-11-07）：文案的唯一赋值点已定位，判据是 `gcache.cached_power == 0`**

全仓 `STR_VEHICLE_STATUS_TRAIN_NO_POWER` 的**唯一使用点**是 `src\vehicle_gui.cpp:4708-4715`：

```cpp
} else if (v->vehstatus.Test(VehState::Stopped) && (!mouse_over_start_stop || v->IsStoppedInDepot())) {
    if (v->type == VehicleType::Train) {
        if (v->cur_speed == 0) {
            if (Train::From(v)->gcache.cached_power == 0) {
                append(STR_VEHICLE_STATUS_TRAIN_NO_POWER);   // ← 唯一出口
            } else {
                append(STR_VEHICLE_STATUS_STOPPED);
            }
```

⇒ 触发三条件全满足才会显示：①`VehState::Stopped`（停驶）；②`cur_speed == 0`；③**`gcache.cached_power == 0`**（该车所在链的动力合计为 0）。
⇒ 这与"电力系统/电气化轨"无关；是**动力统计归零**。且它只出现在**车辆窗口的状态行**，所以玩家看到的"无法启动 + 显示没有电力"是同一个事实的两面：链上没有可用的引擎功率 ⇒ `UpdateSpeed()` 给不出加速度 ⇒ 不动。

**G3-3 修正为具体落点**：`cached_power` 由 `Train::ConsistChanged()` 沿链累加 `GetPower()` 写入。R3R 打散铰接组（`R3RDearticulateOneGroup`）把父车的 `ArticulatedPart` 清掉、成员改为 `ArticGroupMember`；**若父车因此不再被算作引擎**（`railveh_type` 被改成 `Wagon`，或 `FrontEngine` 位只在组内第一节而未迁到打散后的组头），整链 `GetPower()` 之和就是 0，`cached_power` 归零 ⇒ 恰好命中上述三条件。
- 另一面：`ConsistChanged()` 缺失只会让 `cached_power` **保持旧值**（非 0），不会凭空变 0 ⇒ **单靠"漏调 ConsistChanged"无法解释「没有电力」**，必须叠加"引擎位丢失/功率烘焙为 0"。这修正了 G3-3 的原表述（原写法不成立）。

**因此 KI-566 的取证靶点收敛为两处**：
1. `R3RDearticulateOneGroup()` 里对"组头 `railveh_type` / `FrontEngine` 位 / 烘焙功率"的处理，尤其"组头本身是 artic part、真引擎在组内别处"的形态；
2. `CmdReverseTrainDirection(reverse_single_veh=true)` 的组定位 bug（KI-565）导致**翻错了组**：若被翻的组是"真引擎所在组"，而玩家看的是另一组，可能出现「显示的车（链头）在没引擎的那一段」⇒ `cached_power` 的统计起点/内容与玩家预期不符。⇒ **KI-566 有可能是 KI-565 的下游症状**，须先修 565 再看 566 是否复现。

**判据（复测用）**：操作后 `dump_vehicle <链头>` 的 `CHAIN-ATTRS pow=` 必须非 0；车辆窗口功率显示非 0；`tf:` 段里每组有且仅有一节带 `e`（engine）标记且组头 `H` 与成员 `M` 齐全。

## 五、需要玩家补充的信息（已回填，2026-11-07）

- **Q1-1**：P1 的具体表现 —— (a) 机车在库里完全不动（`COUPLE-FAIL`/`reason=depot-paired` 循环）／(b) 出库了但停在库门口或半路不到站台／(c) 到站台了但挂不上（`CPL-PAIR` 越走越远）／(d) 挂上了但位置/图像错。
  **答：a** ⇒ 机车根本没有出库动作。**G1-4（出库后被 `parked-no-pair` 拦）因此被证伪**，卡点回到「库内那一步根本不放行」。
- **Q1-2**：是否稳定复现？同一存档重放是否每次同一位置卡住？
  **答：「我不明白为什么这么问，反正就是所有的车库挂车站的场景都一样」** ⇒ 非个例、**普遍性缺陷**，所有"库内机车 → 车站车底"场景全中。⇒ 排除存档脏数据/单点几何问题，必须是判据层面的通病。
- **Q1-3**：该机车是**真引擎**还是**假引擎头**（车库里"设为段"的车厢链）？
  **答（玩家实际回答的是 Q1-4 的内容）：「是的，双方条件完全匹配，理论可以完成配对」** ⇒ 订单条件匹配（机车 `GOTO_COUPLE`、车底 `WAIT_COUPLE`）成立。Q1-3 本身未答，暂按"真引擎机车"处理（Q1-1=a 时若为假引擎头，`CheckTrainStayInDepot()` 的 `IsEngine()` 守卫会另行触发，需现场日志区分）。
- **Q1-4**：双方订单 —— 机车是 `GOTO_COUPLE`（前往并挂接）吗？车底是 `WAIT_COUPLE`（等待挂接）吗？
  **答：是**（见上）。⇒ 排除"订单未配对导致门自锁"。
- **Q2-1**：越过的"所有信号灯"是**同一条到发线/正线**上的连续灯，还是横跨多个区间/车站？
  **答：「是一堆十几个 PBS（双向），不可能是 JGRPP 原生的长预留干的。但是之前观察普通 block 信号也这样」** ⇒ **两类信号都会**。⇒ REG-394-E 判定：**不能只按"非 PBS 才截断"改**（原生 `pbs.cpp:1380-1381` 只在非 PBS 处 break），必须**两种信号都截断**——因为玩家要的是"像原生一样每次都只订很短一段"，而不是"遵守 PBS 语义"。
- **Q2-2**：预留是**一个 tick 内铺完**（一次性长预留），还是**每 tick 往前推进几格**？
  **答：1 tick 内铺完** ⇒ 与代码实证完全一致（`R3RReservePathSequence()` 成功路径零截断）⇒ **修法确定 = 正向截断**（不是原生长度问题）。
- **Q2-3**：是否只在**车库→车站**挂接时出现？站台→站台挂接有没有？
  未答（不阻塞，Q2-2 已足以定修法）。
- **Q3-1**：`Ctrl+左键` 是按在**车库**里，还是**主视口 / 车辆列表**里？
  未答（不阻塞：GUI 三条路径都已实证传的是被点车厢）。
- **Q3-2**：该铰接式是**真铰接**（NewGRF 原生，`dump_vehicle` 的 `st:` 含 `A`）还是 R3R **打散过的假铰接**（`tf:` 含 `H`/`M`）？
  未答（不阻塞：`IsArticGroupMember()` 对两者同判，修法覆盖两种）。
- **Q3-3**：「没有电力」是**按下 Ctrl+左键后立刻**出现，还是过一段时间/开动时才出现？
  **答：「我刚才难以复现，但是记得那时候那个文本是出现在列车启停按钮上面」** ⇒ 位置确认 = **车辆窗口的启停按钮上方状态行**，即 `vehicle_gui.cpp:4708-4715` 的 `STR_VEHICLE_STATUS_TRAIN_NO_POWER` 唯一出口 ⇒ 判据确认为 `Stopped && cur_speed==0 && cached_power==0`。**难点：难以复现** ⇒ 不能靠现场日志追，必须从代码层面找"什么时候会把 `cached_power` 弄成 0"。
- **Q3-4**：出现「没有电力」的那列车，车辆窗口里功率是否显示 **0**？
  **答：记得似乎是的** ⇒ 与 `cached_power == 0` 判据自洽，KI-566 判据闭环。
- **Q3-5（口径拍板，REG-394-C）**：Ctrl+左键对铰接式的期望是 A=**整列所有铰接组一起翻**，还是 B=**仍只翻被点的那一组、但点哪一节就翻哪一组**？
  **答：B** ⇒ 第 391 轮 KI-558 的"单组"语义保持不变，只需修正**定位**。⇒ KI-565 修法 = 那一行 + `is_group` 收紧，**不需要**另加整列遍历。

## 五-B、口径回填后的总判定（2026-11-07）

| 条目 | 定论 | 修法状态 |
|---|---|---|
| KI-565 | 确定性定位 bug，口径 B（点哪节翻哪组） | **可立即改码**：`train_cmd.cpp:5712-5715` 改判自身 + `:5718` 收紧同组判据 |
| KI-563 | 正向从不截断，1 tick 铺完，PBS + 普通信号都要截 | **可立即改码**：`R3RReservePathSequence()` 遇任一信号即 break，`SetReservationTarget` 收缩到 `booked-1` |
| KI-561 | **a = 库里完全不动，且全部车库场景普发** | G1-4 证伪 ⇒ 需重新定位"库内为何不放行"（下一组取证） |
| KI-566 | 判据 = `cached_power == 0`，位置 = 启停按钮上方状态行，难复现 | 需从代码找归零点；怀疑与 KI-565 打散/翻错组同源 |

## 六、取证计划（按序，单轮配额 3~5 组，每组后立刻回填本节）

1. `src\pathfinder\yapf\yapf_rail.cpp` 的 `R3RReservePathSequence()` + 调用点 `:1610-1637` 现行代码 —— 确认"成功路径不截断"，给 KI-563 定修法（信号截断条件写在哪）。
2. `src\train_cmd.cpp` 的 `R3RIsParkedWaiting()` / `R3RDepotCoupleAppliesToThisDepot()` / `CheckTrainStayInDepot()` 现行代码 —— 定 KI-561 卡点（门是否已放行、下一道门是谁）。
3. `src\train_cmd.cpp` 的 `CmdReverseTrainDirection(reverse_single_veh=true)` 组定位 + `ARTIC-FLIP` 探针 —— 定 KI-565（口径差 or 定位 bug）。
4. `STR_VEHICLE_STATUS_TRAIN_NO_POWER` 的赋值点（`train_cmd.cpp` 的 `UpdateSpeed`/`GetStatusString` 一带，`cached_power == 0` 判据）—— 定 KI-566。

## 六-B、取证结论 5（2026-11-07）：库内出库的唯一闸门链已定位

`TrainLocoHandler` 调用 `CheckTrainStayInDepot(consist)` 的**唯一**位置是 `src\train_cmd.cpp:22925`（包在 `R3RScopeTimer` 里，命中即 `return true` 结束本 tick）。返回 true 的出口与"是否可能永久"：

| 行 | 出口 | 永久性 |
|---|---|---|
| `:15292` | `cached_power == 0 && !R3RIsCarOnlyFormation` ⇒ `vehstatus.Set(Stopped)` + true | **永久**（下 tick 被 `Stopped` 早退挡住，本函数不再执行） |
| `:15313` | `IsWaitingForUnbunching()` | 取决于 unbunching 计时是否被重置 |
| `:15333`→`:15412` | `seg_state == Full \|\| HasDepotReservation(v->tile)` ⇒ `exit_blocked` | **永久**（库格残留预留即恒真） |
| `:15415` | `!TryPathReserve(v)` ⇒ `MarkTrainAsStuck` + true | **永久**（每 tick 重试、每 tick 失败） |
| 其余 | `:15278` 到库守卫 / `:15301` OT_WAITING / `:15308` OT_WAIT_COUPLE / `:15315` reverse_distance / `:15326` wait_counter<37 / `:15342` 回本库 | 有界或订单不匹配 |
| `:22907`（调用点之前） | `R3RIsCarOnlyFormation && cached_power == 0` ⇒ `cur_speed=0` + true | 只对纯车底，机车不命中 |

⇒ KI-561（Q1-1=a，全部车库场景普发）的可能根因只剩三条：**A=`TryPathReserve` 恒失败**（`:15415`）、**B=库格残留 `HasDepotReservation`**（`:15333`）、**C=`cached_power==0` 自停死锁**（`:15292`，与 KI-566 同一条）。三者都**无法从静态代码再收窄**（取决于现场状态），故本轮**加一条只读探针** `DEPOT-STAY`，把每个出口编号打出来，一次日志即可定性。

## 七、状态

- KI-561 / KI-563 / KI-565 / KI-566 已登记；KI-558 状态行标注"复测失败 → 拆 KI-565/566"。
- 本轮尚未改任何源码、未构建。
- 取证进度（2026-11-07）：4 组全部完成（计划 1~4）。结论：
  - **KI-565（Ctrl+左键只对第一节生效）＝ 确定性代码 bug**，落点 `train_cmd.cpp:5712-5715` 组头回溯判 `Previous()` 而非自身，一行修法已定，`is_group`（`:5718`）须同步收紧。
  - **KI-563（预留越灯）＝ 正向从不截断**（`R3RReservePathSequence()` 全函数无信号判断），第 393 轮 C-3=A 只改失败回滚 ⇒ "修过没好"是必然。修法＝把 `r3r_resv_path` 截到第一个信号/特殊边界，逐 tick 续订；但需先答 REG-394-E（现场是否全 PBS 信号）。
  - **KI-561（库内机车挂车站车底）＝ D-1g 两道门都已放行**，卡点在下游；最可疑 G1-4：出库后第一格被 `parked-no-pair`（`!in_depot && partner==nullptr`）拦住。
  - **KI-566（没有电力）＝ `vehicle_gui.cpp:4711` 的 `cached_power == 0`**，即动力统计归零；单纯漏 `ConsistChanged` 不成立，必须叠加引擎位丢失/功率烘焙为 0；且可能只是 KI-565 的下游症状。
- **仍未改码、未构建**：等 Q1-1 / Q2-2 / REG-394-E / Q3-5 回填。其中 KI-565 已可无条件开工（与口径无关的部分：定位 bug 本身就是错的），只差 Q3-5 决定"修完是否还要加整列遍历"。
- **不得**在拿到 Q1-1（P1 口径）、Q2-2（P2 形态）、Q3-5（P3 口径）之前动源码 —— 三条都可能因口径不同而修错地方。

## 八、实现落地（2026-11-07，口径已全部回填）

### 8.1 KI-565（`src\train_cmd.cpp:5712-5715`，口径 Q3-5=B）

现行代码判的是"**前车**是否属于铰接组"，于是只要 `v` 是某一组的**父车**，而它前面紧邻上一组的成员，循环就会越过自己的组、一路回溯到**上一组的父车**，翻转的永远是链上更靠前的那一组；`v` 是组成员时反而正常。这正是"总是只对链的第一节铰接组生效"的机理（越靠前的组越容易被误命中）。

修法（一行判据换向）：判**自身**是否为组成员，回溯到最近的父车即停。

```cpp
Train *group_head = v;
while (group_head->Previous() != nullptr && group_head->IsArticGroupMember()) {
    group_head = group_head->Previous();
}
```

- `v` 为父车（非成员）⇒ 不回溯，`group_head == v`（正确）；
- `v` 为成员 ⇒ 沿链前移到父车为止（正确）；
- 顺带使 `:5718` 的 `is_group`（`group_head->Next()->IsArticGroupMember()`）真正只判"本组是否多于一节"，无需额外加"同一组"判据——回溯后的 `group_head` 必为父车，其 `Next()` 为成员即同组。

### 8.2 KI-563（`src\pathfinder\yapf\yapf_rail.cpp` 的 `R3RReservePathSequence()`，口径 Q2-1/Q2-2）

现行循环只有 `res_fail_tile != INVALID_TILE`（订不上）才 break，**全函数没有信号判断** ⇒ 一次 tick 把整条 `r3r_resv_path` 铺完。第 393 轮 C-3=A 只改"失败时保留部分预留"，正向通路没动，所以"修过没好"是必然。

修法：每格订成功后判定该格该轨是否有信号（`IsTileType(tile, TileType::Railway) && HasSignals(tile) && HasSignalOnTrack(tile, TrackdirToTrack(td))`），**PBS 与普通 block 一律截断**（玩家 Q2-1：两类都会越，且要求"像原生一样每次只订很短一段"）⇒ 订到该格即停（信号格本身要订，因为列车要开到信号前），后续格交给下一 tick 续订。截断后 `booked < steps.size()`，调用点 `:1655-1658` 已有的"预留终点收缩到 `booked-1`"自动接管，无需改动。

### 8.3 KI-561（只读探针 `DEPOT-STAY`）

`CheckTrainStayInDepot()` 每个 `return true` 前打一行 `DEPOT-STAY veh=%d exit=<tag> power=%d speed=%d order=%d wait=%d resv=%d seg=%d fully=%d`（边沿去重），一次日志即可在 A（`:15415` TryPathReserve 恒失败）/ B（`:15333` 残留库预留）/ C（`:15292` 功率 0）三者间定性。纯只读、零行为变更。

### 8.4 顺序

先落 8.1 + 8.2 + 8.3（互不冲突的三个文件/函数），一次构建；KI-566 暂不动（等 565 修完复测，看是否为其下游症状）。

### 8.5 实现细节补记（实际落地的样子）

**8.1 实际代码**（`train_cmd.cpp`，含注释说明为何判自身）：

```cpp
Train *group_head = v;
while (group_head->Previous() != nullptr && group_head->IsArticGroupMember()) {
    group_head = group_head->Previous();
}
```

连坐点复核：`is_group` 不用改（回溯后必为父车，`Next()` 属成员即同组，不会跨组）；翻转循环的终止条件 `w != group_head && !w->IsArticGroupMember()` 也天然停在下一组父车。

**8.2 实际代码**：断点放在 `++booked;` 之后、循环末尾，判据

```cpp
if (IsTileType(stile, TileType::Railway) && HasSignals(stile) &&
        HasSignalOnTrack(stile, TrackdirToTrack(stdir))) { ... break; }
```

`IsPbsSignal()` 只用来写日志、**不参与**判定（口径 Q2-1：两类都截）。截断后走"成功"出口，`booked < steps.size()` ⇒ 调用点已有的 `SetReservationTarget()` 收缩生效。`CPL-RES-OK` 补 `booked=` / `stop_signal=`，tiles 列表上限 `booked`。

**8.3 实际探针**：lambda `r3r_depot_stay(tag)`，`static std::unordered_map<uint32_t,uint32_t>` 按车号去重；`resv=` 有 `IsRailDepotTile()` 守卫（`HasDepotReservation()` 内含 `dbg_assert_tile(IsRailDepot)`，未守卫会在非库格断言崩溃 —— 与第 110 轮附记 7b 同一教训）。12 个出口编号见 KI 条目。

### 8.6 构建自证（2026-11-07）

- 复用既有入口 `R3R_fullbuild.cmd inc`（**未新建任何 .cmd**；只改两个 `.cpp`，未碰 `src\*.h` ⇒ 护栏判增量安全）。
- 日志：`-- Generating Doxyfile_GS` → `[2/4] Building CXX ... yapf_rail.cpp.obj` → `[3/4] ... train_cmd.cpp.obj` → `[4/4] Linking CXX executable openttd.exe` → `EXIT_CODE=0`；错误模式计数 `ERRCOUNT=0`。
- `build\R3R_incbuild.done` = `EXIT_CODE=0`（01:19:34）。
- 时间戳链：`src\pathfinder\yapf\yapf_rail.cpp` 01:13:41 → obj 01:17:12；`src\train_cmd.cpp` 01:15:02 → obj 01:17:13；`build\openttd.exe` 01:19:19（52 457 984 B）晚于两者。
- 产物字面量自证：`findstr /C:DEPOT-STAY /C:CPL-RES-SIGNAL-STOP /C:stop_signal=` 于 `build\openttd.exe` 全部命中。
- `read_lints` 两文件 0 条。

### 8.7 仍未做

- **游戏内复测**：三条修法都还没有现场日志。
- **KI-561 仍只是"加了探针"**：需要玩家复现一次并回 `build\R3R_debug.log`，看 `DEPOT-STAY ... exit=` 是 11 / 12 / 2。
- **KI-566 未动**：等 KI-565 修后复测；若"没有电力"仍在，靶点转到 `R3RDearticulateOneGroup()` 的 `railveh_type` / `FrontEngine` / 烘焙功率。
- 未编发布树（`build-release`），本轮只编内测树。

## 九、第 394 轮复测（2026-11-07 晚，玩家回日志 `build\R3R_debug.log`）

### 9.1 玩家原话

> 「见 build/R3R_debug.log，veh0 和 veh3 那一组是一次性预留铺到底的修复，出现了 veh0（机车）的寻路紊乱。而 veh21 和 veh27 那一组就是车库出发去车站挂被卡住的样本了」

两个独立事实：
1. **KI-563 的修法引发了新问题**（`veh0` 机车寻路紊乱）—— 这是**回归**，不是旧问题；
2. **KI-561 拿到现场样本**（`veh21` 机车 + `veh27` 车底），`DEPOT-STAY` 探针这次有数据。

### 9.2 猜测根因（待验证，先落盘）

**R-F1（KI-563 回归，最可疑）**：截断只改了"订到哪"，**没有改"寻路目标"**。`R3RReservePathSequence()` 返回成功时，调用点按 `booked` 收缩 `SetReservationTarget()`；若第一个信号距离起点很近（`booked` 只有 1~2 格），预留终点会落到**机车脚下/紧邻格**，而 YAPF 的 `rp()` 已经把整条 `r3r_resv_path` 当作"可达"来算代价 ⇒ **代价模型与实际预留不一致**，下一 tick 又要重新算，表现为"寻路紊乱"（目标忽远忽近、来回改主意）。

**R-F2**：截断点在"信号格本身"（我按原生 `pbs.cpp` 语义保留信号格在预留内）。若机车**正停在信号格上**、或信号格就是路径第 1 格，`booked` 可能为 1 ⇒ 预留退化成"只有自己脚下这一格"，`TryPathReserve` 仍返回成功（`booked>0`）⇒ 列车以为自己有路，实际下一格没订 ⇒ 反复。

**R-F3**：回归可能不在截断，而在 `CPL-RES-OK` 的 `tiles` 列表我改成了只列 `booked` 个 —— 这是纯日志改动，不影响行为，**排除**（列出以证明不是它）。

### 9.3 本轮待取证（按此顺序，每步落盘）

| 编号 | 要回答的问题 | 证据 |
| --- | --- | --- |
| E-1 | `veh0` 的"紊乱"具体长什么样（目标格跳变？原地打转？预留长度跳变？） | `veh=0` 的全部 `CPL-*` / `RP cur=` / `TRP` / `CRT` 行 |
| E-2 | 截断后 `booked` 的实际分布（是否普遍只有 1~2 格） | `CPL-RES-SIGNAL-STOP ... booked=` 与 `CPL-RES-OK ... total= booked=` |
| E-3 | `veh0` 是否有 `exit=12`（订路失败被 `MarkTrainAsStuck`） | `TRP veh=0 ... ok=0` / `TPR-BLOCKED veh=0` |
| E-4 | `veh21`/`veh27` 的 `DEPOT-STAY exit=` 编号 | `DEPOT-STAY veh=21` / `veh=27` |

### 9.4 已取到的读数（2026-11-07 01:35 会话）

输入：`build\R3R_debug.log` 470 810 B / 5 232 行 / mtime 01:35:14（晚于 `build\openttd.exe` 01:19:19 ⇒ 是本轮新 exe 产生，采样有效）。
标签计数：`DEPOT-STAY` **75**、`CPL-RES-SIGNAL-STOP` **2**、`CPL-RES-OK` **2**、`CPL-RES-FAIL` **35**。

**E-2（KI-563 截断过狠，R-F2 成立）** —— 两次截断逐字：

```
:3023 CPL-RES-SIGNAL-STOP veh=0 nth=0/15 tile=254,121 td=0 pbs=1 booked=1
:3024 CPL-RES-OK veh=0 total=15 booked=1 stop_signal=1 tiles=254,121/0
:3899 CPL-RES-SIGNAL-STOP veh=0 nth=1/14 tile=253,122 td=2 pbs=1 booked=2
:3900 CPL-RES-OK veh=0 total=14 booked=2 stop_signal=1 tiles=253,121/3;253,122/2
```

判读：①路径 15 格，**第 0 格就是 PBS 信号**（`nth=0/15`）⇒ 只订了 1 格；②下一次只剩 14 格、**第 1 格又是 PBS 信号** ⇒ 只订 2 格；③`pbs=1` 表明两处截断点都是 **PBS**，不是 block 信号。这与玩家 Q2-1「一堆十几个 PBS（双向）」完全吻合 —— **这条线路的 PBS 密度是每 1~2 格一个**。
⇒ 我按「两类信号都截」实现，在这种线路上等于「每 tick 只能往前订 1 格」。**这就是 `veh0` 寻路紊乱的直接来源**（R-F1 + R-F2 同时成立）。

**E-4（KI-561 现场，A 嫌疑成立）** —— `veh=27`（车底）两条出口高频交替：

```
:3331 DEPOT-STAY veh=27 exit=12 power=5325 speed=0 order=16 wait=0 resv=0 track=128 fully=1 stuck=1
:3334 DEPOT-STAY veh=27 exit=7  power=5325 speed=0 order=16 wait=1 resv=0 track=128 fully=1 stuck=1
（:3401/:3403、:3486/:3487、:3550/:3552 … :5216/:5217 同形，共 30+ 次）
```

判读：①`exit=12` = `TryPathReserve()` 失败 ⇒ `MarkTrainAsStuck()`，`stuck=1` 从此常驻；②`exit=7` = `++wait_counter < 37`，且 `wait=1` 说明计数器**每 tick 被重置回 0**（不是 37 递增）；③`track=128` = `TRACK_BIT_DEPOT`（仍在库内）、`fully=1`、`resv=0`（**没有库格残留预留，B 嫌疑排除**）、`power=5325`（**有功率，C 嫌疑排除**）。
⇒ **KI-561 = A 嫌疑（`TryPathReserve` 恒失败）**，且失败后 `MarkTrainAsStuck` 使其永久卡住；`wait_counter` 被别的分支清零，是"每 tick 重试、每 tick 失败"的稳定死循环。

`veh=0`：`:177 exit=1`（到库守卫）→ `:332 exit=7`（wait-37），`power=4185`、`fully=1`、`stuck=0`，属正常出库前等待，与紊乱无关。

### 9.5 由 9.4 推出的两个必改点（待验证后再动码）

**P-1（KI-563 回归，必须先修）**：截断判据要从「任一信号」改成**只对非 PBS 信号截断**（恢复原生 `pbs.cpp` 语义），因为现场 PBS 密度 1~2 格一个，两类都截等于把预留压成 1 格。
**代价**：全 PBS 线路上预留仍会很长 —— 但那正是 JGRPP 原生行为，玩家"原生不会这么长"的印象需用日志反驳或另加**长度上限**（见 9.6 待拍板）。

**P-2（KI-561）**：`TryPathReserve` 对 `order=16`(GOTO_COUPLE)、`fully=1`、`resv=0` 的库内车底恒失败。下一步要看 `TRP veh=27` / `TPR-BLOCKED veh=27` 找失败原因（`R3RIsParkedWaiting` 哪一支命中、还是寻路本身无路）。

### 9.6 待玩家拍板（KI-563 的最终口径）

- **口径 α**：只截非 PBS 信号（= 原生语义）。全 PBS 线路仍会长预留，但绝不再出现"1 tick 铺完十几个"以外的异常。
- **口径 β**：α + 附加长度上限（如最多 8 个信号 / 64 格），保证任何线路上预留都不会太长。这是**原生没有的创新行为**，需要你确认。

---

## 十、KI-561 根因做实（2026-11-07，无主预留位挡死出库）

### 10.1 决定性读数（`build\R3R_debug.log`）

`veh=27`（车底、`track=128` 在库内、`power=5325`、`stuck=1`）的订路失败行**逐字相同、连续 30+ 次**：

```
:3252 CPL-RES-FAIL veh=27 nth=0/3 tile=280,126 td=8 want=0 booked-before=0 failtd=8
      type=1 tbits=1 res=1 plain=1 st=0 depot=0 owner=-1 selfheld=0 otile=-1,-1 ospd=-1 ord=-1
（:3328 / :3399 / :3483 / :3548 / :3606 / :3661 / :3715 / :3785 / :3832 / :3927 / :3981 … 同形）
```

配上同 tick 的：
```
:3255 TRP veh=27 mf=279,126 real=4 type=16 dest=65306 spd=0 fp=0 look=0 one=0 mstuck=0 fto=0 => ok=0 res=0
```

### 10.2 字段判读（对照 `src\pathfinder\yapf\yapf_rail.cpp:652-664` 的格式串）

| 字段 | 值 | 含义 |
| --- | --- | --- |
| `nth=0/3` | 第 **0** 格就失败，共 3 格 | 连第一格都订不上 |
| `booked-before=0` | 失败前已订 **0** 格 | 整条路径一格没铺 |
| `tile=280,126` / `mf=279,126` | 库内车 → 前方第一格 | 出库第一步 |
| `type=1` `plain=1` | `TileType::Railway` + `IsPlainRail` | 普通铁轨，不是站台/库 |
| `tbits=1` / `res=1` | 该格轨道位 = X、**已存在预留位 = X** | **位被占** |
| `owner=-1` | `GetTrainForReservationArbitrated(280,126, TRACK_X)==nullptr` | **占位者不存在** |
| `selfheld=0` | 占位者不是自己/同链 | 排除自锁 |
| `st=0` `depot=0` | 不是站台整格位、不是库格 | 排除 KI-207c 站台位 |

⇒ **结论：`280,126` 的 `TRACK_X` 预留位是「无主预留」（孤儿位）**。`TryReserveRailTrack()` 见位已被占 ⇒ 拒绝 ⇒ `booked=0` ⇒ `TryPathReserve` 返回失败 ⇒ `MarkTrainAsStuck()`（`stuck=1` 常驻）⇒ 下一 tick `wait_counter` 被清回 0 再试、再失败。

这与 §9.4 的 A 嫌疑（`exit=12` 恒失败）**完全闭环**：不是 YAPF 找不到路（`TRP` 有 `dest`、`ok=0` 但 `look=0`），而是**订不到位**。

### 10.3 与既有 KI 的关系

- 与 **KI-345/KI-346**（孤儿预留走廊、`rp()` 订错轨）同族：现场是双槽格订了车不走的轨，位无主。
- 与 **KI-203c/207c**（`R3RPurgeStrayReservations` / `R3RReservationAudit`）同族：这正是"能查能清"的目标形态。
- **修法方向（待验证）**：出库/挂接订路前，或 `TryPathReserve` 失败时，对「本车路径第一格 + `owner==nullptr` + `res!=0`」做一次受控清理（`UnreserveRailTrack(280,126,TRACK_X)`），即可自愈；但需先确认该位是否真孤儿（不能误清活车预留）。

### 10.4 待验证（U 项）

- **U-10-1**：`280,126/TRACK_X` 的位是本会话新产生还是随存档带入？（查日志有无 `RESV-AUDIT` / `RESV-REAP` / `RESV-PURGE` 提及 `280,126`；若无，则属"账本外陈旧位"）
- **U-10-2**：`veh=21`（机车）是否也被同一格挡住（`veh21/veh27` 同一组）。
- **U-10-3**：为何 `GetTrainForReservationArbitrated` 返回空（占位车已死？还是位是 R3R 记账残留？）。

### 10.5 代码级真因（`src\pbs.cpp:2296-2319`）

`R3RReservePathSequence()`（`yapf_rail.cpp:619`）逐格调 `ReserveSingleTrack()` → `TryReserveRailTrackdir()` → `TryReserveRailTrack()`（`pbs.cpp:542`）。后者对 plain rail 的冲突有两条豁免：

```cpp
:551 if (!changed && R3RReservationHeldByCouplePartner(tile)) { ... return true; }  // 挂接伙伴
:559 if (!changed && R3RReservationHeldBySelf(tile, track))   { ... return true; }  // 自己（KI-368/REG-359-L）
```

而 `R3RReservationHeldBySelf()`（`pbs.cpp:2296`）判"是不是自己的"用的是**沿预留回溯找主人**：

```cpp
:2313 const Train *const a = R3RFollowReservationToOwner(tile, td0, rts);
:2314 const Train *const b = R3RFollowReservationToOwner(tile, ReverseTrackdir(td0), rts);
:2315 if (a == nullptr && b == nullptr) return false;   // ← 回溯不出来 ⇒ 判"不是我"
```

**现场正是这一支**：`owner=-1`（两个方向都回溯失败）。原因：`veh=27` 的车节还在 **库格 279,126（`TRACK_BIT_DEPOT`）**，而回溯走的是 plain rail 的连续链，到库格就断了 ⇒ 两个方向都返回 nullptr ⇒ **自订豁免失效** ⇒ `TryReserveRailTrack` 返回 false ⇒ `booked=0` ⇒ `TryPathReserve` 失败 ⇒ `MarkTrainAsStuck()`。

⇒ **KI-561 真因 = 「库内出库」特有的自订位不可辨识**：
1. `ExtendTrainReservation()` 先（Track-level）把出库后第一格 `280,126/TRACK_X` 订上；
2. 随后 `R3RReservePathSequence()`（Trackdir-level）又要订同一格 → `TryReserveTrack()` 返回 false（位已置）；
3. 豁免 `R3RReservationHeldBySelf()` 因"车还在库里、回溯不到"而判否；
4. 一格都订不上 → 恒 `stuck=1`。

同一机制**不影响库外场景**：车行驶在普通轨上时，回溯能沿轨走到自己，豁免成立（这就是 KI-368/REG-359-L 当时修的场景）。

### 10.6 修法候选（待拍板，均只改 .cpp）

- **候选 A（最小、定点）**：`R3RReservationHeldBySelf()` 在 `a==nullptr && b==nullptr` 时，若 `actor` 的链上有车节停在**库格**且该库格与本格相邻（`DistanceManhattan(actor_tile, tile)==1`），返回 true。范围极窄（只放行"我在库里、前面这格挨着我的库"）。
- **候选 B（结构性）**：`R3RReservePathSequence()` 开头把 `steps[0]` 与 `ExtendTrainReservation` 已订范围对齐 —— 若首格已被预留且 `GetTrainForReservation*` 为空，视为"刚被自己 Extend 订下"，跳过并计入 booked。治标面更宽但不是精确定位。
- **候选 C（记账）**：`ExtendTrainReservation` 订位时把 (tile,track) 记进本车账本，`R3RReservationHeldBySelf` 先查账本。最准，但要动 `pbs.cpp` 的订位主路径，风险最高。

我的倾向：**先做候选 A**（一行判据、范围最小、可立即验证），A 不成就上 C。

### 10.7 同 tick 证据链（`build\R3R_debug.log:3224-3255`，逐行）

```
3224 RESV-BEGIN veh=27 tick=3677 tile=279,126 mf=279,126 td=8 spd=0 real=4 order=16 one=0 look=0
3225 TPR-WALK veh=27 mf=279,126 origin=279,126 td=8 okay=0 exit=2 new=280,126 reach=1
3227 RESV-EXT-PRE veh=27 tile=280,126 enterdir=2 tracks=0x1 reserved=0x0 doRes=1
3228 CPL-ENTRY veh=27 tile=279,126 orderType=16 dontReserve=0      ← 进挂接寻路
3249 RP cur=280,126 td=8 depth=0 trk=0 bits=0x1
3250 RP cur=281,126 td=5 depth=1 trk=5 bits=0x21
3251 RP cur=281,127 td=4 depth=2 trk=4 bits=0x10
3252 CPL-RES-FAIL veh=27 nth=0/3 tile=280,126 ... res=1 owner=-1 booked-before=0
3253 CPL-RESERVE veh=27 reserved=0 next=8 path=3 booked=0
3254 CPL-RET veh=27 tick=3677 tile=280,126 enterdir=2 tracks=0x21 res=0x0 ret=0x1 why=cpl-fail
3255 TRP veh=27 mf=279,126 real=4 type=16 => ok=0 res=0
```

判读：
- `:3227` 时 `280,126` 的预留是 **0x0**；`:3252` 时变成 **0x1**，中间**没有任何第三方事件**（无 SET-RAIL / 无别人订位），唯一动过预留的就是本 tick 的 `ExtendTrainReservation()`（`train_cmd.cpp:17310`，在 `CPL-ENTRY` 之前）。
- ⇒ `280,126/TRACK_X` 是**本车自己刚订的**，`owner=-1` 只是"回溯不到库里的车"。
- ⇒ 修法即 §10.6 候选 A/C，不是"清孤儿位"（该位不脏，是自订）。

### 10.8 KI-563 回归定性（玩家指出的 `veh=0` 寻路紊乱）——已证实

日志两组：
```
3023 CPL-RES-SIGNAL-STOP veh=0 nth=0/15 tile=254,121 td=0 pbs=1 booked=1
3899 CPL-RES-SIGNAL-STOP veh=0 nth=1/14 tile=253,122 td=2 pbs=1 booked=2
```
- 该线路上 **PBS 信号密度 = 1~2 格一个**，两条 STOP 行 `booked` 只有 1 和 2 ⇒ **每 tick 只订 1~2 格**。
- 原生语义（`src\pbs.cpp:1377-1382` 的 `FollowReservation()`）：遇库格 break、**只有 `!IsPbsSignal()` 才 break** ⇒ **原生 PBS 信号从不截断预留**。
- 所以第 394 轮"两类信号都截"是**比原生更严**的新行为：YAPF 的代价模型以为能一路走到底，实际每 tick 只多订 1 格 ⇒ 预留持续落后于车 ⇒ 表现为 `veh=0` 寻路/预留紊乱。

⇒ **KI-563 首版口径错误已确认，需回退到"只截非 PBS"（α）或加长度上限（β）。**

### 10.9 本轮待拍板编号（玩家答编号即可回填）

- **C-394-1（KI-563 口径）**
  - **α**：只截**非 PBS** 信号（= 原生 `pbs.cpp:1381` 语义）。PBS 线路上仍会长预留，但恢复原生行为，`veh=0` 紊乱应消失。
  - **β**：α + 附加上限（如最多 8 个信号 / 64 格）。超出原生，需你确认。
  - **γ**：完全回退第 394 轮那段截断（连非 PBS 也不截），只保留"失败保留部分位"。
- **C-394-2（KI-561 修法）**
  - **A**：`R3RReservationHeldBySelf()` 加"我在库里、本格挨着我的库格 ⇒ 这格是我的"豁免（一行判据、范围最窄）。
  - **C**：给 `ExtendTrainReservation` 的订位加本车账本，`R3RReservationHeldBySelf` 先查账本（最准、动订位主路径）。
  - **B（本轮新增，玩家问出来的方向）**：不换判据，改「不重复订同一格」——挂接寻路开始前，把本车刚刚由 `ExtendTrainReservation` 订下的那一段（origin → res_dest）视为已订、跳过不订。见 §11.2。

## 十一、玩家两问的代码取证（2026-11-07）

### 11.1 Q1：挂接寻路与普通寻路铺预留，是不是两个函数？

**不是两个，底层同一个。** 两条路最后都落到 `TryReserveRailTrackdir(veh, tile, trackdir)`：

| 用途 | 外层函数 | 订位调用点 |
|---|---|---|
| 普通寻路（驱动路径预留） | `ExtendTrainReservation` | `train_cmd.cpp:16133`（安全停位）、`:16170`（逐格）；脚下保护 `:17392` |
| 挂接寻路（YAPF 选完路后回溯铺） | `R3RReservePathSequence` → `ReserveSingleTrack` | `yapf_rail.cpp` 内同名 lambda，同样调 `TryReserveRailTrackdir` |

**真差别在「路从哪来」和「什么时候调」**：

1. 普通寻路：`CFollowTrackRail` 沿当前轨道**一格一格**跟着走，边走边订，遇信号/别人预留就停 ⇒ 终点是"下一个安全停点"。
2. 挂接寻路：先让 YAPF 用代价函数算出**整条**路径（可到很远处的会合点/等待车底），算完再沿节点路径**一次性**把整条订上。
3. 调用顺序（`TryPathReserve`）：先 `ExtendTrainReservation`（`:17310` 一带）；若"短预留成功"就 `res-ok-brief` 直接返回（`:17386-17395`），**根本不会进挂接寻路**。只有 `long_reserve` 为真、或 Extend 失败，才落到挂接分支（`:17346` 的 `r3r_couple_stuck_retry` 放行 + 后续 `DoTrainCouplePathfind`）。

⇒ 库里机车挂车现场的时序是：**Extend 先订了库外第一格（280,126）→ 挂接 YAPF 又从同一格开始再订一次 → 撞到自己刚订的位 → 判"不是我的" → 失败**。

### 11.2 Q1 的答案：改成跟普通一样，会有改善吗？

- 理解为"把挂接的订位换成 `ExtendTrainReservation`"——**不会**：两者最终都调同一个 `TryReserveRailTrackdir`，"是不是我的"判定一字不改，照样失败。
- 理解为"别让挂接重订 Extend 已经订过的那段"——**会，而且这是正解方向**（§10.9 新增 B 候选）。A（加豁免判据）和 C（加账本）本质上都是在事后补证明，B 是从源头不做重复劳动。

### 11.3 Q2：车库出库机制核实

玩家图景「棚子下的普通轨道 + 末端传送门 = 内置 PBS 信号」：**大方向对，细节不对**。

- **对的部分**：出库前必须先在库外把路订上，订不上车就不动 —— 行为上确实像"门口有个门禁"；"一次铺预留出去"也对，`ExtendTrainReservation` 从库格 trackdir 出发踏出库外后，一路铺到下一个安全停点（不是只铺一格）。
- **不对的部分**：
  1. 库格不是"棚子下的普通轨道"，是**独立 tile 类型** `TileType::RailwayDepot`；车在库里 `track` 位是 `TRACK_BIT_DEPOT(0x80)`，不是普通轨位。
  2. **没有"传送门"实体，也没有信号**；库格无信号位，出库不涉及"信号变绿"。
  3. 库格有自己的一套**整格预留**（`pbs.cpp` 的 `SetDepotReservation` / `GetDepotReservationTrackBits`）；`FollowReservation()` 遇库格**直接 break**（不能穿库格回溯）—— 这就是 §一「回溯到库格断了」的代码依据。
- ⇒ 准确说法：**出库 = 给"从库格伸出去的那条路"做一次正常 PBS 预留**，不是"穿过一个内置信号"。

## 十二、第 394 轮续（2026-11-07）：玩家追问两条 —— 表层包装能否统一 / 出库两路为何一成一败

**玩家原话（逐字）**：
1. 「关于"一"，虽然他们的底层是一样的，但是表层的包装不一样也会有很大的不一样。因此我要求你：要不然把它们的表层包装也统一，要不然告诉我为什么不能」
2. 「关于"二"，我需要你讲普通寻路和挂接寻路出库的区别，为什么一个成功另一个却失败」

### 12.1 两套表层包装的实际差异（逐项对照，代码实证）

| 维度 | `ExtendTrainReservation`（普通/驱动） | `R3RReservePathSequence`（挂接） |
|---|---|---|
| 路径来源 | `CFollowTrackRail ft` 自己沿轨走，只能单调前进 | 入参 `steps` = YAPF 节点回溯出的整条路径，可绕行/可跨站台跳段 |
| 订位调用 | `train_cmd.cpp:16133`（安全位）、`:16170`（逐格） | `yapf_rail.cpp:636` `ReserveSingleTrack(stile, stdir)` |
| 信号 | 不显式判；靠 `IsSafeWaitingPosition` 与 `ft.err`（EC_OWNER/EC_NO_WAY）自然停在停点 | `:710` 显式 `HasSignals && HasSignalOnTrack` 截断（第 394 轮 KI-563 新建） |
| TraceRestrict | **完整**：`:16139-16168` slot acquire / `TRPRF_WAIT_AT_PBS` | **完全没有** |
| 终点语义 | 下一个安全停点（安全位） | YAPF 给的目标（会合点/等待车底） |
| 失败处理 | 整段回滚：`:16178-16223` 从 `origin` 重走释放（`EXTEND-BACKOUT`） | 保留已订（KI-563 A，`:676-690`），`booked==0` 才 false |
| 覆盖范围 | 覆盖全部调 `TryPathReserve` 的列车 | 只覆盖挂接（`DoTrainCouplePathfind`） |

### 12.2 Q1 结论：能统一「订位层」，不能统一「路径来源层」

- **不能统一的部分（硬约束）**：`ExtendTrainReservation` 的路径由 `CFollowTrackRail` 现场走出来，是一维单调的；挂接路径是 YAPF 在路网上搜索出来的，可能绕行、可能被 `tiles_skipped` 跳过整段站台。若把挂接改成"直接调 `ExtendTrainReservation` 算路径"，等于让挂接放弃 YAPF 结果、沿直线盲走，**永远到不了会合点**（除会合点恰在直线尽头）。这不是包装问题，是能力缺失。
- **能统一的部分（正解，也顺带修掉 KI-561）**：把「订这一格」这件事抽成一个共用函数，两边都调：
  ```cpp
  static bool R3RBookTrackdir(Train *v, TileIndex tile, Trackdir td);
  ```
  内部顺序 = ①查本车账本（B 候选：本车已订过此格 ⇒ 直接 true）→ ②`TryReserveRailTrackdir` → ③记账。这样 `train_cmd.cpp:16133/:16170` 与 `yapf_rail.cpp:636` 走**完全相同**的归属判定与记账路径，KI-561 的"自己撞自己"在第 ① 步就被消掉，不必再依赖 `R3RFollowReservationToOwner` 回溯（那条回溯在库格起点天然断裂）。
- 附带可统一的第二处：**失败策略**。现状 `Extend` 整段回滚、`PathSequence` 保留已订，两者语义相反。可统一为"保留已订、下次续订"（与原生 PBS 一致），但要单独评估 `Extend` 回滚是否承担了"试延伸"语义（函数名 `Extend` 与 `EXTEND-BACKOUT` 说明是**试探性**延伸，失败必须回滚否则会留下半截预留）⇒ 建议**保持不回滚**，只在注释里写明二者不同是有意的。

### 12.3 Q2：普通寻路 vs 挂接寻路出库，一成一败的逐 tick 对照

共同起点：`CheckTrainStayInDepot()` → `TryPathReserveWithResultFlags()` → **先** `ExtendTrainReservation`。

**普通寻路（成功）**
1. `Extend` 从库格 `279,126/TRACK_BIT_DEPOT` 出发，踏出库外到 `280,126`；
2. `:16170 TryReserveRailTrackdir(v, 280,126, ...)` ⇒ 该格当前 `reserved=0x0` ⇒ `changed=true` ⇒ **直接返回成功**，全程不经过任何"归属判定"；
3. 一直逐格订到下一个安全停点 ⇒ `PBSTileInfo(..., true)`；
4. `TryPathReserve` 走 `res-ok-brief` 早退（`:17386-17395`），**根本不会进挂接寻路**；
5. 车按这条预留开出库。→ 全程"首订"，每格都是新订，无需证明归属。

**挂接寻路（失败）**
1. `Extend` 同样先跑，同样在 `280,126` 订下一位（`RESV-EXT-PRE` 时 `reserved=0x0`，其后变 `res=1`，中间无第三方事件 ⇒ 证明是本车订的）；
2. 但挂接要订到会合点，`Extend` 订不到那么远（或 `long_reserve` 为真）⇒ 不早退，继续 `ChooseTrainTrack` 让 YAPF 出路径；
3. 回到 `R3RReservePathSequence`，`steps[0]` 就是 `280,126/td=8` ⇒ **第二次订同一格**；
4. `TryReserveRailTrack` 见该位已有预留 ⇒ `changed=false` ⇒ 转自订豁免 `R3RReservationHeldBySelf()`（`pbs.cpp:2296`）；
5. 豁免要靠 `R3RFollowReservationToOwner()` 沿 plain rail 回溯找主人（`pbs.cpp:2313-2315`），而本车此刻仍在**库格**（`TRACK_BIT_DEPOT` 不是 plain rail）⇒ 回溯到库格即断 ⇒ 两向皆 `nullptr` ⇒ `return false` ⇒ 判"第三方占位"；
6. `booked=0`（第一格就失败）⇒ KI-563 A 的"保留已订"分支不成立 ⇒ `return false` ⇒ `CPL-RES-FAIL` ⇒ 整条没订上 ⇒ `TPR ok=0` ⇒ `MarkTrainAsStuck()`。

**一句话**：普通寻路是**首订**（`changed=true`，不需要证明"这格是我的"）；挂接寻路是**重订**（`changed=false`，必须证明"这格是我的"），而"证明"这件事在**库格起点**天然失败。所以不是寻路算法不行，是**第二条路上多了一道在库里无法通过的证明**。

⇒ 修法优先级：**B（不重订已订段）/ 账本** 最优 —— 它让挂接路径的第一步根本不去订那格，绕过整道证明；A（库格相邻豁免）是在证明环节打补丁；C（账本）是 B 的实现手段，两者可合并为"统一订位层 + 账本"。

## 十三、第 394 轮续二（2026-11-07）：玩家提出 KI-563 新修法 D —— 不显式信号截断，改「订到下一个安全停车位」

**玩家原话（逐字）**：「啊哈，我似乎知道关于预留穿过所有信号灯怎么修了。我们不需要预留的显式截断，我们只需要把 `R3RReservePathSequence` 的终点定为下一个安全停车位即可」

### 13.1 方案定性（D 候选）

- 现状做法（第 394 轮 KI-563 落地的 `yapf_rail.cpp:710-724`）：走完一格就判 `HasSignals(tile) && HasSignalOnTrack(tile, track)` ⇒ `break`。这是**语法层截断**——看到信号就停，不管该信号是不是"安全停点"、也不管它后面还有多远可走。
- 玩家提议（D）：删掉这段显式信号判定，改成在 `steps` 序列里找**第一个 `IsSafeWaitingPosition()` 为真的格**，订到它为止。这是**语义层截断**——原生 PBS 的定义本来就是"订到下一个安全停点"，信号只是安全停点的常见形态，不是定义本身。
- 因此 D 在口径上比现状更接近原生，方向正确。`ExtendTrainReservation` 早已是这么做的（`train_cmd.cpp:16130`）。

### 13.2 待验证（落盘于取证前，防上下文丢失）

- **V-D1**：`IsSafeWaitingPosition` 的确切签名与判据。它是否需要"列车当前位置/方向"参与？若需要，对挂接路径中间格（列车还没走到）能否直接判定——`R3RReservePathSequence` 手上有 `Train *` 与每步的 `(tile, trackdir)`，理论上可以传，但语义是"该格能否安全停车"，与列车当前位置无关，应可判。
- **V-D2**：安全位在挂接路径上**未必存在**（长弯道、信号密集段、全程无岔）。必须有 fallback：一路无安全位 ⇒ 订到 `steps` 末尾（YAPF 终点，即会合点/等待车底脚下，本身即停车位）。否则会出现"一格都不订"反而比现状更死。
- **V-D3**：安全位被第三方占用（`IsWaitingPositionFree()` 为假）时该怎么处理。`ExtendTrainReservation:16133` 的做法是 `break` 出去然后整段 back-out 回滚；但 `R3RReservePathSequence` 的 KI-563 A 口径是"保留已订"。二者相反，D 落地时必须先定这一条，否则会重复 KI-563 的老路。
- **V-D4**：D **不解决 KI-561**。KI-561 是第一格（280,126）就撞自己，第一个安全位无论在哪，`steps[0]` 依然是那格，依然要重订。D 与 B/A/C 是两条独立的问题线，不可混为一谈。
- **V-D5**：D 与信号的关系——若某安全位正好在信号格上（PBS 信号前停车位），`IsSafeWaitingPosition` 对"信号格 + 该 trackdir" 应返回真；但普通 block signal 与 PBS signal 的判定是否不同，需要看实现。

### 13.3 取证回填（2026-11-07，代码实证）

**V-D1 答**：签名 `bool IsSafeWaitingPosition(const Train *v, TileIndex tile, Trackdir trackdir, bool include_line_end, bool forbid_90deg = false)`（`pbs.h:240` / `pbs.cpp:2688`）。**不需要列车当前位置参与**，`v` 只用于取 rail type（`v->GetIndirectCompatibleRailTypes()`）⇒ 可以对挂接路径的中间格直接判。

**V-D5 答（本方案的关键）**：`pbs.cpp:2688-2745` 的判据序列：

| 条件 | 返回 |
|---|---|
| `IsRailDepotTile(tile)` | **true** |
| `HasBlockSignalOnTrackdir(tile, trackdir)`（**普通 block 信号，且朝向我**） | **true** |
| 隧道桥信号模拟入口 + 该 track 穿桥 | **true** |
| `ft.Follow` 失败（线路尽头 / 终点站末格） | `include_line_end` |
| 下一格唯一 trackdir 且是 **PBS 信号** | 条件性：`NoEntry`→include_line_end；`AlwaysReserveThrough`→**false**；带 `TRPAUF_RESERVE_THROUGH` 且程序判 `TRPRF_RESERVE_THROUGH`→false；否则 **true** |
| 下一格是反向单行 PBS 信号 / 隧道桥只出信号 | `include_line_end` |
| 其余 | false |

⇒ **普通 block 信号格本身就是安全位；PBS 信号格本身不是**（要走到下一格做条件判定）。这正是玩家要的效果：**block 信号照旧拦住，PBS 信号被放行**。D 比 KI-563 的 α 候选（"按 `IsPbsSignal()` 分类放行"）更准，因为它自动处理 `NoEntry` / `AlwaysReserveThrough` / `ReserveThrough` 这些特例，而 α 只会一刀切。

**V-D2 答**：**不会出现"一格都不订"**。`train_cmd.cpp:17961` 的 `while (!IsSafeWaitingPosition(consist, res_dest.tile, res_dest.trackdir, true, ...))` 已经在 `ChooseTrainTrack` 之后把 `res_dest` 保证成安全位 ⇒ `steps` 的**最后一格必然安全**，D 的扫描一定有终点。

**同文件已有现成范式**（说明改动小、不引入新概念）：
- `yapf_rail.cpp:160-168` `FindSafePositionProc()`：`if (IsSafeWaitingPosition(...)) { res_dest_tile = tile; res_dest_td = td; return false; }` —— 找到第一个安全位即停，**同一个文件、同一种写法**。
- `yapf_costrail.hpp:733`：`if (IsSafeWaitingPosition(v, t, td, true, ...)) break;` —— YAPF 的 waypoint 平台搜索也是"第一个安全位即停"。
- `train_cmd.cpp:16130`：`ExtendTrainReservation` 的终点判定。

**D 的精确落点（替换 `yapf_rail.cpp:710-724` 整块）**：

```cpp
if (IsSafeWaitingPosition(Train::From(Yapf().GetVehicle()), stile, stdir,
                          true, _settings_game.pf.forbid_90_deg)) {
    if (IsWaitingPositionFree(v, stile, stdir, _settings_game.pf.forbid_90_deg, &st)) {
        this->ReserveSingleTrack(stile, stdir);   /* 订下安全位本身 */
        if (this->res_fail_tile == INVALID_TILE) ++booked;
    }
    stopped_at_safe_position = true;
    break;                                        /* 无论订没订上，都停在这里 */
}
```

**V-D3 答（必须先拍板）**：安全位被第三方占用时，应"**订到它之前就停、保留已订**"（= KI-563 A 口径），而不是像 `ExtendTrainReservation:16133` 那样 `break` 后整段 back-out。理由：原生 PBS 的行为就是车停在前一格等待，不是放弃整条路。

**V-D4 重申**：D **不解决 KI-561**。KI-561 是 `steps[0]`（280,126）第一格就撞自己已订的位；D 只决定"订到哪一格为止"，第一格依然要订、依然要撞。两条问题线独立，D 与 B/A/C 必须分别落地。

## 十四、第 394 轮续三（2026-11-07）：玩家拍板「修法 D」+ 一条关键事实澄清

**玩家原话（逐字）**：「我决定就是修法D了（先别修），但是，关于那个车库出发去挂车站上的列车的问题，我如实告诉你，从来没有出库列车单独预留车库格的现象」

### 14.1 决定

- **修法 D 选定，暂不动手**（玩家明确"先别修"）。落地口径按 §13.3 的伪代码与 §13.4 的探针改法，等玩家发话再改。
- D 的边界不变：只解决 KI-563（一 tick 铺穿多个 PBS 信号），**不解决 KI-561**。

### 14.2 玩家新事实：从来没有「出库列车预留车库格」的现象

**这条事实与 §12.3 的分析不冲突，而是在给分析补上最后一块地基。** 逐条对齐：

1. 我说的"被订的第一格"是 **280,126 = 库外第一格**（plain rail），**不是库格 279,126**。库格从头到尾没有被订过。
2. 玩家观察（库格无预留）在代码上是**硬性不可能**，不是"通常不订"：库格是 `TRACK_BIT_DEPOT`，不是 plain rail；PBS 预留位存在 `_m[t].m2` bit 8-11，只有 plain rail 的访问器（`SetTrackReservation` / `GetRailReservationTrackBits` / `HasReservedTracks` / `UnreserveTrack`）能读写，而这些访问器全部要求 `IsPlainRailTile(tile)`（`rail_map.h:168` 的 `dbg_assert_tile(IsPlainRailTile(tile))` 已经在 KI-203 的崩溃里被现场验证过一次）。⇒ 库格**在数据结构层面就存不下 PBS 预留位**，UI 也不可能画出库格的预留预览。
3. ⇒ 因此 §12.3 第 5 步的"回溯到库格即断"不是偶发情形，而是**每列出库挂接列车、每一次都必然发生**：`R3RFollowReservationToOwner` 从 280,126 沿 plain rail 回溯，一走到 279,126（库格）就因 `IsPlainRailTile` 为假而停，两向皆 `nullptr` ⇒ 判"第三方占位"。KI-561 是 100% 复现的结构缺陷，不是概率性 bug。
4. ⇒ 反向也说明：**方案 A（库格相邻豁免）是为一个不存在的前提打的补丁**。真正的豁免条件只能是"回溯失败 + 起点在库格"，属于事后补救；**B（不重订已订段）/账本** 才是在源头不产生这道证明。这进一步支持 §12.2 的结论。

### 14.3 待验（本轮不改码，仅登记）

- **V-E1**：`rail_map.h` 的预留访问器是否**全部**带 `IsPlainRailTile` 保护（含 `HasReservedTracks`、`GetRailReservationTrackBits`、`SetTrackReservation`、`UnreserveTrack`）。若个别访问器缺少保护，则"库格永不可能有预留"这条推论需要按访问器逐个收窄（结论大概率不变，但表述要精确）。
- **V-E2**：`ReserveTrackUnderConsist()`（`train_cmd.cpp`，平台等待门每 tick 调）对停在**库内**的列车是否真的一次都不写入。玩家现象是"从来没有"，与 V-E1 同一机制，但这里是**行为面**的独立证据，可作为 V-E1 的交叉验证。
- **V-E3**：D 落地后 KI-561 仍会照旧触发，复测时不要因为"D 已经修了信号截断"就误判 KI-561 已消；两者探针不同（`CPL-RES-SAFE-STOP` vs `CPL-RES-FAIL`/`RESV-EXT-PRE`）。

### 14.4 【自我纠错】§14.2 第 2 条写错了 —— 库格**有**预留位，KI-561 的前提被推翻

取证 `rail_map.h` + `pbs.cpp` + `train_cmd.cpp` 后，§14.2 第 2 条「库格在数据结构层面就存不下 PBS 预留位」**是错的**，必须订正：

1. **plain rail 与库格是两套字段，不是一个**：
   - plain rail：`_m[t].m2` bit 8-11，`GetRailReservationTrackBits` / `SetTrackReservation` / `UnreserveTrack`，全部 `dbg_assert_tile(IsPlainRailTile(t))`（`rail_map.h:227 / 242`）。
   - 库格：`_m[t].m5` bit 4，`HasDepotReservation` / `SetDepotReservation` / `GetDepotReservationTrackBits`（`rail_map.h:299-328`），`dbg_assert_tile(IsRailDepot(t))`。
   - 统一入口 `GetReservedTrackbits(t)`（`pbs.cpp:341-346`）对库格**返回库格预留位**，不是一个恒空的假值。
2. **出库时确实会试探性预留库格**：`train_cmd.cpp:19102-19106`（`TryPathReserve` 内，`ChooseTrainTrack` **之前**）
   ```cpp
   /* If we are in a depot, tentatively reserve the depot. */
   if (moving_front->track == TRACK_BIT_DEPOT && moving_front->tile == origin.tile) {
       SetDepotReservation(moving_front->tile, true);
   ```
   失败则在 `:19159-19162` 释放（`SetDepotReservation(false)`）。另有 `pbs.cpp:572-577` 的 `TryReserveRailTrack` 库格分支，以及 `train_cmd.cpp:15455`（停车等待路点的出库预告）。
3. ⇒ **玩家观察的正确解读**：库格预留**会**存在，但它**从不单独存在**——要么跟着库外那条整路一起活着（成功后直到列车驶出库格，`vehicle.cpp:2745` 才清），要么随整条路失败被 `:19161` 一起释放。所以画面上永远看不到"孤立的一格库格预留"。玩家的事实**成立**，但结论不是"库格不能预留"。
4. ⇒ **KI-561 的根因推断前提被推翻**：既然库格有预留位、`FollowReservation` 能走进去、`CheckTrainsOnTrack` 能在库格上找到本车，那么"回溯在库格处断裂 ⇒ 判第三方 ⇒ 首格失败"这条链条**不成立**。`R3RFollowReservationToOwner`（`pbs.cpp:1965`）的真实路径是「沿预留一路跟到终点格，再看那格上停着谁」，不是"累加回溯"。KI-561 的真因需重查。

**新登记疑点 REG-394-D（未解）**：`R3RReservePathSequence` 在 `steps[0]` 处 `booked=0` 的确切原因。候选：
- (a) `R3RFollowReservationToOwner` 第 1969 行的**单向信号早退**：280,126 在朝向库格的反方向上有 one-way block signal ⇒ 直接 `nullptr`（现场没取过这一格的信号数据，未验）；
- (b) `FollowReservation` 的 `rts`（rail type 集合）不覆盖库内轨道类型 ⇒ 走不进库格（`:2082` 用 `GetRailTypeInfo(...)->indirect_compatible_railtypes`）；
- (c) `CheckTrainsOnTrack(ftoti, ftoti.res.tile)` 只查**终点那一格**（`pbs.cpp:1979`），而库格与列车所在格若因 `res.tile` 停在 280,126（库格被 `pbs.cpp:1509` 的 `IsRailDepotTile && !GetDepotReservationTrackBits` 判停）而库格预留恰好为 false ⇒ 走不到；
- (d) 走的是 `GetTrainForReservationArbitrated`（`pbs.cpp:2177`），它第一行 `if (!IsPlainRailTile(tile)) return nullptr;` ⇒ 对库格直接空。

**未定性，需取数**：`RESV-WALK start=... phase=... how=... end=... found=...` 探针（`pbs.cpp:1958`）已在树里，只要抓一次失败的挂接就能读出 `how=blocked|direct|platform|none` 和 `end=`。这是下一步唯一需要的日志，不需要改码。

### 14.5 本轮结论

- 修法 D 已拍板、暂不实施（玩家"先别修"）。
- §12.3 的"库格断链"解释**撤回**，KI-561 归因为待定（REG-394-D），之前据此推荐的 B/账本方案依然可能有效，但**理由要重写**——它不是因为"库格不能预留"，而是因为"挂接不该重订 Extend 已订的段"，这本身就是正确设计。

## 十五、车库出发挂站台车底：问题链重述（基于玩家 2026-11-07 纠正）

**玩家原话（逐字）**：「从来没有出库列车单独预留车库格的现象」

### 15.1 这句话有且只有两种可能解读，二者推出相反结论 —— 必须用日志分死

| 解读 | 含义 | 推出结论 |
|---|---|---|
| **解读甲：从未设过** | `SetDepotReservation(t, true)` 在"库内待发去挂车"这条路径上从未执行 | 回溯沿预留走到库格必然无位可认领 ⇒ §12.3 的"断在库格"**成立**（但理由换成"库格此刻是空的"） |
| **解读乙：设过但不孤立** | 设了、且与库外整条预留同生共死，所以画面上永远看不到孤立的一格 | 失败判定发生时库格预留位**仍为 true** ⇒ 回溯**能**走到库格并认领本车 ⇒ §12.3 **不成立**，真因在跟随器停点/信号早退 |

**判别手段（不需要改码、不需要新数据，树里已有探针）**：`SetDepotReservation()`（`rail_map.h:311-317`）的 `R3RResvWatchLog(b ? "SET-DEPOT" : "CLEAR-DEPOT", t)`。在既有 `build\R3R_debug.log` 里搜 `SET-DEPOT`：

- **零命中** ⇒ 解读甲成立，`train_cmd.cpp:19102-19106` 的 `if (moving_front->track == TRACK_BIT_DEPOT && moving_front->tile == origin.tile)` 在该场景恒假。这是玩家事实的直接代码解释，且把 KI-561 归因收回 §12.3（换理由）。
- **有命中且紧跟 `CLEAR-DEPOT`** ⇒ 解读乙成立，库格预留位在判定时仍在，回溯断点不在库格。
- **有 `SET-DEPOT` 无 `CLEAR-DEPOT`** ⇒ 库格被永久占住，属另一种泄漏缺陷（新问题）。

**这是本轮唯一还需要的一条数据，且它已经在旧日志里。**

### 15.2 重述后的问题链（三段，标注证据等级）

**第一段 · 订位：谁先占了 280,126**

1. 列车在库内，订单 `OT_GOTO_COUPLE`，目标车底停在站台上。库格是 `TRACK_BIT_DEPOT` 整格占用，**不是轨道节点，不进 `steps`**。
2. `steps[0]` = 库外第一格（现场 280,126）。
3. 该格在挂接 YAPF 跑之前**已被订**（现场 `resNow=1`）。订它的是本车出库时那条既有预留路（`ExtendTrainReservation` 铺到安全位的那条），不是第三方车。
4. `R3RReservePathSequence` 对 `steps[0]` 再订一次 ⇒ `TryReserveTrack`（`rail_map.h:267-277`）第 272 行 `if ((res & bits) != TRACK_BIT_NONE) return false;` ⇒ 返回值 false，`booked=0`，进入归属判定。

> 证据等级：第 3、4 条是**实证**（现场 `resNow=1` + `booked=0` 同时成立，且该格预留是本车出库路径所铺）。第 1、2 条是代码事实（库格无 trackdir 节点）。

**第二段 · 归属判证：为什么会判成"不是我的"**

5. 判定入口是 `R3RFollowReservationToOwner`（`pbs.cpp:1965`），**它不是"沿预留逐格累加回溯"**，而是「沿预留一路跟到**终点格**，再看那格上停着谁」：
   - `:1969` **单向信号早退**——若 280,126 在**朝库格的反方向**上有 one-way block signal 且该 trackdir 上没有 PBS 信号，直接 `return nullptr`（不打任何"我为什么放弃"的细分）;
   - `:1977` `FollowReservation(...)` 顺预留走到头;
   - `:1979` `CheckTrainsOnTrack(ftoti, ftoti.res.tile)` —— **只查终点那一格**，不是全路径;
   - `:1988` 站台特例：终点在站台上时沿站台轴整段扫。
6. 于是本车的预留能不能被认回来，**完全取决于跟随器最后停在哪一格、以及那一格上有没有本车**：
   - 停在库格且库格有预留位 ⇒ 走进库格 ⇒ 库格上停着本车 ⇒ 认领成功 ⇒ 豁免、不失败。
   - 停在库格且库格**无**预留位 ⇒ `pbs.cpp:1509/1856` 的 `IsRailDepotTile(tile) && !GetDepotReservationTrackBits(tile)` 判停 ⇒ 停在库格但库格上没有"可认领的预留"这层语义仍需看 `CheckTrainsOnTrack` 是否把库格上的车算进来。
   - 停在库格**之前**（rail type 不相容 / 单向信号早退）⇒ `found=nullptr` ⇒ 判第三方 ⇒ `booked=0`。

> 证据等级：第 5 条是**代码事实**。第 6 条三条分支**全部待定**，正是 REG-394-D。

**第三段 · 后果：为什么表现为"出库去挂车就卡住"**

7. `booked=0` 不是"没订上"，而是"订不上因为被占、且占位者不是我被认定"。KI-563 的 A 口径（保留已订）在这里会让**库内那一段预留留着不撤**，于是一次失败在画面上被放大成"整条出库路都亮着预留却走不动"。
8. 下一 tick 重试，`steps[0]` 仍是那格、仍被同一位占住 ⇒ 每 tick 重复，形成玩家看到的"卡在库门口反复挂不上"。
9. 这与 KI-563（一 tick 铺穿十几个信号）**互相独立**：D 只改"订到哪一格停"，`steps[0]` 该撞还是撞（§13.2 V-D4）。

### 15.3 结论：现在该做什么

- **先 grep 一次 `SET-DEPOT`**，用它把解读甲/乙钉死。这一步比任何代码推理都便宜，且旧日志里就有。
- 若为**甲**：KI-561 = 「出库挂接时库格试探性预留未发生 ⇒ 回溯在库格拿到空位 ⇒ 首格自撞被判第三方」。修法落在 B（不重订 Extend 已订段）+ 给 `SetDepotReservation` 的库格分支补上"出库挂接也该试探预留"的两选一。
- 若为**乙**：KI-561 归因转为「跟随器停点/单向信号早退」，需要 `RESV-WALK` 的 `how=` 与 `end=` 才能定位，修法可能只需放宽 `:1969` 的早退或让 `CheckTrainsOnTrack` 不只看终点格。
- **两种情况下 D 都照旧独立落地**，不受此结论影响。

## 十六、第 394 轮续四（2026-11-07）：玩家重跑「卡在车库不去车站挂车」，veh=27

### 16.1 采样来源（已核，采样有效）

| 项 | 值 |
|---|---|
| 日志 | `build\R3R_debug.log`，652 834 B / 6 949 行，2026-11-07 10:53 |
| 产生者 | `build\openttd.exe`，52 457 984 B，2026-11-07 01:19:19（=当前树，含第 394 轮前全部改动） |
| 目标车 | **veh=27** |

### 16.2 已确认的硬事实（日志原文锚点）

**事实 1：veh=27 停在车库格 279,126，速度恒 0，从未移动。**

`SKIP-STOPPED veh=27 order=0 real=16 spd=0 tile=279,126 parked=0 front=1 nord=6 idx=0 tt=0 blocked=N wclr=0`
出现 **34 次**（`blocked=` 计数 1 → 2289 单调递增，说明这是同一列车连续数百 tick 的重复）。

- `tile=279,126` = **车库格**，与 §14 讨论的库格**同一格**。
- `order=0`（`curType=0` = `OT_NOTHING`）、`real=16`（`cur_real_order_index=16`）、`nord=6`（订单表 6 条）。
- 配套 `ORD-IDX-WATCH site=... veh=27 real=-1->0 ... dest=4294967295(INVALID_TILE) vtile=279,126`：索引在一次重排中被重置为 0。

⇒ **veh=27 卡住时的形态不是「预留失败」，而是「根本没进预留路径」**：它每 tick 落在停驶分支被跳过（`SKIP-STOPPED`），没有 `CRT`/`TPR-*`/`CPL-*` 中的任何一条。

**事实 2：全日志的 `TPR-BLOCKED` 是另一列车（veh=9），不是 veh=27。**

`TPR-BLOCKED veh=9 tile=213,119 spd=0 mf=213,119 origin=209,119 okay=1 td=0 other=12 otherTile=213,119 otherMf=213,119 otherSpd=0 otherOrder=17 otherFirst=12 caronly=1`
共 **27 条**，首次在 `:799`。

- `okay=1` ⇒ 自己的预留本身健康；
- `other=12`、`otherTile == mf`（同格）、`otherOrder=17`（`OT_WAIT_COUPLE`）、`caronly=1`、`otherSpd=0`
- ⇒ **与 KI-330（第 221 轮做实）的形态逐字相同**：`CheckTrainsOnTrack` 把同格上另一列「纯车底等待者」误判为阻塞者，`MarkTrainAsStuck` + `return TPRRF_NONE`。

**事实 3：全局计数（供后续分层）**：`TPR-BLOCKED`=27、`TPR-WALK`=51、`COUPLE-FAIL`=32、`SKIP-STOPPED veh=27`=34。

### 16.3 疑点登记（先落盘，未定性）

- **REG-394-E（高）**：veh=27 的 `order=0 / real=16 / dest=INVALID_TILE` 三者不自洽——`real=16` 指向 `OT_GOTO_COUPLE`（枚举值 16），但 `current_order` 已是 `OT_NOTHING` 且无目标格。合理猜测：订单在某次重排里被**部分推进**（`cur_real_order_index` 前进但 `current_order` 被 `Free()` 后没重建），于是 `ProcessOrders` 既无目标可算、也不会重算路径 ⇒ 永久停驶分支。**待验**：`SKIP-STOPPED` 探针（`train_cmd.cpp` 约 :8170-8181）的准确判据与它之前的 `ProcessOrders` 早退条件。
- **REG-394-F（中）**：veh=9 的 `TPR-BLOCKED` 是否就是 KI-330 的未修复形态（第 221 轮修法待拍板、至今未落地）⇒ 若是，本条与 veh=27 **是两个独立缺陷**，不能合并归因。

### 16.4 下一步（只需两条数据）

1. `SKIP-STOPPED` 探针的判据代码（`train_cmd.cpp` :8170-8181 一带）+ 它上一行日志 → 定 veh=27 是「订单缺目标」还是「被 `Stopped` 标志挡住」。
2. veh=27 完整订单表 6 条的类型序列（`nord=6`，`real=16` 已越界/不匹配）→ 定 REG-394-E 是索引越界还是枚举巧合。

**取证方式调整**：终端输出折行严重（每 40 字符硬折），后续改「把筛选结果写到一个 txt 再 `read_file`」，或直接按行号 `read_file` 精确读取，不再用 `Select-String | ForEach-Object` 直接打屏。

## 十七、§16 的定性**作废**——veh=27 走完了挂接全流程，卡在「第一格预留被无主占位」

### 17.1 先纠错：§16.2「事实 1」的推断是错的

§16 只读了 veh=27 的前 25 行就下结论「没进预留/挂接路径」，这是**采样错误**。全量导出 `build\R3R_veh27.txt`（1080 行 / 114 100 B）后，探针族计数为：

| 探针 | 行数 |
|---|---|
| `CPL-*`（挂接寻路全程） | **532** |
| `ORD-IDX-WATCH` | 130 |
| `RESV-*` | 39 |
| `SKIP-STOPPED` | 34 |
| `COUPLE-*` | 4 |
| `SEGLOAD-PARK` | 3 |

⇒ veh=27 **每 tick 都在跑完整挂接寻路**，而且是「找到目标→算出路径→订位失败」的纯循环。§16.2 的「根本没进预留/挂接路径」**撤回**。

### 17.2 玩家的判断成立：配对确实成功了

`build\R3R_veh27.txt:1046-1048`（循环里每 tick 重复这一段）：

```
1046: CPL-PATHFOUND veh=27 found=1
1047: CPL-SCAN tick=2935 veh=27 found=1 closed=2 limit=10000 nodes=9 enter=7
      noplat=3 nores=0 noconsist=1 consist=3 rej=0/0/0/0 acc=3/0 accwp=0
      last=285,127 lastveh=21 why=acc-car
1048: CPL-BEST veh=27 tile=285,127 td=8 cost=6642
```

- `found=1`：找到目标；
- `consist=3` / `acc=3`：3 个候选车底通过筛选；
- `last=285,127 lastveh=21 why=acc-car`：**配对到 veh=21 那组车底，终点格 285,127**；
- `CPL-BEST cost=6642`：路径代价算出来了。

⇒ **「配对成功」是事实，卡点不在配对、不在寻路。**

### 17.3 唯一的失败点：`CPL-RES-FAIL`，第一格上有一个「无主预留位」

`build\R3R_veh27.txt:1049-1051`：

```
1049: CPL-RES-FAIL veh=27 nth=0/3 tile=280,126 td=8 want=0 booked-before=0
      failtd=8 type=1 tbits=1 res=1 plain=1 st=0 depot=0
      owner=-1 selfheld=0 otile=-1,-1 ospd=-1 ord=-1
1050: CPL-RESERVE veh=27 reserved=0 next=8 path=3 booked=0
1051: TRP veh=27 mf=279,126 real=4 type=16 dest=65308 spd=0 fp=0 look=0 one=0
      mstuck=0 fto=0 => ok=0 res=0
```

逐字段：

- `nth=0/3` ⇒ 失败在**路径第 0 格**，一格都没订上；
- `tile=280,126` ⇒ 就是库格 279,126 外面紧邻的那一格；
- `res=1` ⇒ 该格**确实有预留位**（不是空位）；
- `owner=-1` ⇒ `R3RFollowReservationToOwner` 回溯**找不到任何列车**；
- `selfheld=0` ⇒ 也不认作「本车自己持有」；
- `otile=-1,-1 / ospd=-1 / ord=-1` ⇒ 回溯根本没摸到任何车；
- `plain=1 / depot=0` ⇒ 是普通铁轨格，不是库格也不是站台。

⇒ **280,126 上有一个没有任何列车能认领的预留位（孤儿预留）**，把出库第一步堵死。因为是 `nth=0`，后面 2 格有没有问题根本轮不到。

### 17.4 与之咬合的第二个事实：从库格沿该 trackdir 跟随出不去

`build\R3R_veh27.txt:1041-1045`：

```
1041: CPL-ENTRY  veh=27 tile=279,126 orderType=16 dontReserve=0
1042: CPL-ORIGIN veh=27 origin=279,126 td=8 vehDir=5 vehTd=8 dontReserve=0
1043: CPL-ORIGIN-TD veh=27 tile=279,126 tdb=0x101 tbits=0x1
1044: CPL-ORIGIN-FOLLOW veh=27 td=0 trk=0 ok=1 nt=279,126 nbits=0x100 st=0 skip=0
1045: CPL-ORIGIN-FOLLOW veh=27 td=8 trk=0 ok=0 nt=-1,-1 nbits=0x100 st=0 skip=0
```

- `279,126` 的轨道位 `tdb=0x101` = `TRACK_BIT_DEPOT(0x100) | TRACK_BIT_X(0x1)` ⇒ 库格；
- 出库方向 `td=8`（`TRACKDIR_LEFT_N`），而列车朝向 `vehDir=5`（`DIR_W`，朝西）——**两者不是同一个方向**；
- 沿 `td=0` 跟随能停在原地（`ok=1 nt=279,126`），沿 `td=8` 跟随则 `ok=0 nt=-1,-1`：**一步都走不出去**。

⇒ 挂接寻路的起点照抄了一个**在该格上不可通行的 trackdir**，于是「回溯认领自己」这一侧同样不可能成功（与 §17.3 的 `selfheld=0 / owner=-1` 互相印证：不是「对方占着我的位」，而是**我和对方都够不着那一位**）。

### 17.5 状态旁证：列车被标 stuck，且库格自身无预留

`build\R3R_veh27.txt:1052-1053`：

```
1052: DEPOT-STAY veh=27 exit=12 power=5325 speed=0 order=16 wait=0 resv=0
      track=128 fully=1 stuck=1
1053: DEPOT-STAY veh=27 exit=7  power=5325 speed=0 order=16 wait=1 resv=0
      track=128 fully=1 stuck=1
```

- `order=16`（`OT_GOTO_COUPLE`）**订单没丢**，车辆身份正常；
- `track=128`（`TRACK_BIT_DEPOT`）、`fully=1`（整列出库完全在库格内）；
- `resv=0` ⇒ **库格自己没铺预留**；
- `stuck=1` ⇒ 已被标为卡住（后果，不是原因）。

### 17.6 结论（本轮可直接定性的部分）

**KI-561 / REG-394-D 的真实形态**：不是「配对失败」，也不是「出库路径算错」，而是——

> 库外第一格 280,126 上存在一个**无主预留位**；挂接寻路在 `nth=0` 就被它挡住（`res=1` 而 `owner=-1`），且本车从库格出发所用的 trackdir（`td=8`）在该格上不可通行，导致「认领自己」这条退路同时失效 ⇒ 每 tick 重试、每 tick 同样失败 ⇒ 列车永久停在库里。

**这不是「预留失败」类问题，而是「孤儿预留 + trackdir 不匹配」复合**，与 KI-345/346（孤儿预留走廊）**同族**，但发生位置在**库门口**而非挂接路径中段。

### 17.7 疑点登记（待验，必须先落盘）

- **REG-394-G（高）**：280,126 的预留位是谁铺的？`owner=-1` 说明当前没有任何列车持有它。三种可能：(a) veh=27 自己在某条出库预留里铺过、随后链/段身份变化导致认不回来；(b) 另一列车开走/被卖掉后残留（KI-345 家族）；(c) 挂接 YAPF 的「进入轨」订错轨（KI-346 家族，第 240 轮只修了 `rp()` 的两处）。**判别数据**：全日志搜 `RESV-WATCH SET-RAIL tile=280,126` / `CLEAR-RAIL tile=280,126`，看有无 SET 而无 CLEAR。
- **REG-394-H（高）**：`CPL-ORIGIN` 的 `td=8` 与 `vehDir=5` 不一致，且该 trackdir 在库格上 `ok=0 nt=-1,-1`。若这个 td 是「本车出库的合法方向」，就不该跟不出去；若是错的，则**所有**从车库出发的挂接寻路都在用一个非法起点方向——后者会解释为什么这个场景必卡。**判别数据**：读 `CPL-ORIGIN` 探针取 td 的那几行代码，与库格 `GetTrackdirs`/`GetDepotDirection` 对一遍。

### 17.8 预留 SET/CLEAR 全表（全日志只有 14 条，可直接逐条看）

统计：`SET-RAIL`=6、`CLEAR-RAIL`=4、`SET-DEPOT`=2、`CLEAR-DEPOT`=2。全部原文（按日志顺序）：

```
SET-RAIL   tile=203,116 track=5 phase=choose-track  actor=18 head=18
SET-RAIL   tile=216,117 track=0 phase=yapf-resv     actor=18 head=18
CLEAR-DEPOT tile=202,116 track=255 phase=controller actor=18 head=18
CLEAR-RAIL tile=203,116 track=5 phase=controller   actor=18 head=18
CLEAR-RAIL tile=217,117 track=0 phase=choose-track actor=18 head=18
SET-DEPOT  tile=279,126 track=255 phase=yapf-resv   actor=27 head=27   <-- veh 27 的库格
SET-RAIL   tile=253,121 track=3 phase=yapf-couple   actor=0  head=0
SET-RAIL   tile=252,121 track=0 phase=controller    actor=2  head=0
SET-RAIL   tile=251,121 track=0 phase=choose-track  actor=0  head=0
CLEAR-DEPOT tile=250,121 track=255 phase=controller actor=0  head=0
CLEAR-RAIL tile=251,121 track=0 phase=controller    actor=0  head=0
SET-RAIL   tile=253,121 track=3 phase=yapf-resv     actor=0  head=0
SET-DEPOT  tile=279,126 track=255 phase=yapf-resv   actor=27 head=27   <-- 同上，第二次
CLEAR-RAIL tile=280,126 track=0 phase=choose-track  actor=27 head=27   <-- 出库第一格
```

三条硬结论：

1. **库格 279,126 确实被 veh=27 自己订了整格预留，两次（`SET-DEPOT`），且全日志没有任何 `CLEAR-DEPOT tile=279,126`。** ⇒ §15.1 的**解读乙**成立（设过），玩家「从未单独预留库格」的说法与日志相反——它设了，而且还留着。
2. **出库第一格 280,126 只有 `CLEAR-RAIL`、没有对应的 `SET-RAIL`。** 该位的「订」没有走 `RESV-WATCH` 探针（`R3RResvWatchLog` 只挂在 `SetTrackReservation`/`SetDepotReservation`/`ClearPathReservation` 三处，YAPF 内部 `TryReserveRailTrack` 大量路径不打点）⇒ 日志**看不见谁订了它**，只能看见 27 自己清过它一次。
3. **280,126 那次 CLEAR 用的是 `track=0`（`TRACK_X`），而失败侧要的 trackdir 是 `td=8`（`TRACKDIR_LEFT_N` → `Track_LEFT`）**，两者不是同一条轨。⇒ 「清过」不代表「清干净了我要的那一条」。

### 17.9 由此把 REG-394-G 收窄为「三个候选，一个判别式」

| 候选 | 内容 | 与日志的相容性 |
|---|---|---|
| G-1 | 280,126 的位是**别的车/已消失列车**残留（KI-345 家族孤儿） | 相容：全日志该格无 `SET-RAIL`，说明不是本车按探针路径铺的；`owner=-1` 直接指向「无人持有」 |
| G-2 | 280,126 的位是**本车出库预留**的一部分，但归属回溯走不通 | 相容：`selfheld=0` 是「回溯没走到我」的结果，不证明「不是我的」；库格 279,126 上确实有本车 `SET-DEPOT` |
| G-3 | 该位是 **KI-346 家族**「订了车不走的轨」 | 相容：`CLEAR` 用 `track=0`，失败用 `td=8`，两侧指向不同 track，正是「订的轨与走的轨不是同一条」的签名 |

**判别式（一条，二选一，不必改码）**：对该格跑一次现有审计探针 `RESV-AUDIT-STRAY`（`R3RReservationAudit`，`train_cmd.cpp`，每 2048 tick 一次，owner 判定版）。若 280,126 出现在 `RESV-AUDIT-STRAY` 名单 ⇒ **G-1/G-3（真孤儿）**；若不在名单（说明审计能认出主人）⇒ **G-2（认领侧缺陷）**，修法完全不同（改归属回溯，不改预留清理）。

> 注意：本日志里 `RESV-AUDIT-SUMMARY` 的节拍是 2048 tick，`CPL-SCAN tick=2935/2954/2972` 说明采样只覆盖约 3000 tick，审计大概率只跑过 1 次，未必抓到这一格 ⇒ 该判别式**需要玩家跑一段更长的日志**，或把审计间隔临时调小。

### 17.10 本轮对 KI-561 的定性（替换 §16.3 的 REG-394-E）

- 撤销 REG-394-E（「veh=27 订单状态坏掉」）：`DEPOT-STAY` 与 `TRP` 都显示 `order=16`、`real=4`、`power=5325`，**订单与车辆身份都是健康的**；§16 看到的 `order=0 real=16` 是 `SKIP-STOPPED` 探针自己的字段语义不同（它打的是 `current_order.type` 与另一套索引），不是故障。
- KI-561 的形态改为：**「出库挂接在路径第 0 格预留失败，占位者无主（owner=-1、selfheld=0、otile 无效），每 tick 重试同一格」**。

## 十八、关键反证（玩家 2026-11-07 现场观察）：`(279~280,126)` 上没有黑色预留预览

### 18.1 玩家原话（决定性一句，必须原样保留）

> 「哦我的天哪，veh27 被卡住的时候，我压根没有在 (279-280,126) 上面看到黑色的预留预览」

### 18.2 这条观察为什么会推翻 §17.3 的一半结论

`CPL-RES-FAIL` 探针自己打了 `plain=1`（该格 `IsPlainRailTile() == true`），说明 280,126 是**普通铁轨格**。按既有先例，普通铁轨格的预留位在游戏界面上是**可见的黑线**：

- KI-203（第 110 轮）：玩家报「veh=27 下一站是普通 GOTO_STATION，铁轨却显示黑色预留预览，跟到 tile 60,16」——**孤儿/无主预留同样会被画出来**；
- KI-207c（第 126 轮）：路点整格预留「只在路点那一格有黑色预览」。

⇒ 「有预留位」与「看不见预览」在 280,126 这种 plain rail 格上是**互斥**的。

因此只剩两种解释，且二者指向完全不同的修法：

| 编号 | 解释 | 后果 |
|---|---|---|
| **甲（新首选）** | **280,126 上根本没有预留位**，`CPL-RES-FAIL` 里的 `res=1` 被误读——该字段大概率不是「该格已被订」 | §17.3 的整条因果链作废，真正原因在探针语义或 `nth=0` 的取格逻辑 |
| **乙** | 有预留位但渲染被抑制（`Show reserved tracks` 设置被关 / 该格虽 `plain=1` 但走的是站台-路点那套 overlay 分支） | §17 结论仍成立，只是可视化没生效 |

### 18.3 附带澄清：279,126 看不见**不能作证**

279,126 的轨道位 `tdb=0x101` 含 `TRACK_BIT_DEPOT`，它是**车库格**。库格/非 plain rail 不画预留 overlay 属正常，所以「279,126 也看不见」不能用来支持甲。**唯一有判别力的是 280,126（`plain=1`）。**

同时这也意味着：§17.8 里那两条 `SET-DEPOT tile=279,126 track=255` 且无 `CLEAR-DEPOT` 的记录，**在画面上本来就是隐形的**，不能拿"看不见"去否定它们存在。

### 18.4 判别式（两条，各读一处源码即可）

1. 读 `CPL-RES-FAIL` 探针里 `res=%d` 的**取值表达式**（`train_cmd.cpp` 内 `"CPL-RES-FAIL"` 字面量附近）：
   - 若它等于 `TryReserveRailTrack()` 的返回值 / `GetRailReservationTrackBits()` 的位 ⇒ 真的是「已有预留」，走乙；
   - 若它是别的量（如 `HasReservedTracks` 的取反、`reserve_before`、或整格位 `st`）⇒ 走甲，探针语义写错了，必须改探针或改判据。
2. 读预留 overlay 的绘制入口与前置条件（`HasReservedTracks` → 绘制函数）：
   - 若判定只依赖 `HasReservedTracks(tile)` ⇒ 有 bit 就一定可见，**甲成立**；
   - 若还要求 owner 非空或有 PBS 信号等额外条件 ⇒ 乙成立。

### 18.5 待验（登记）

- **REG-394-I（高）**：`CPL-RES-FAIL` 的 `res=` 字段语义未核对（§18.4 第 1 条），这是当前唯一能一刀切开甲/乙的读数。
- **REG-394-J（中）**：`CPL-ORIGIN-FOLLOW td=8 ok=0 nt=-1,-1`（§17.4）说明探针在库格上取到的 trackdir 跟不出去；若甲成立，则 `td=8` 与 `tbits=1` 的错配同样是探针/调用侧的取格问题，需要一并复核。

### 18.6 探针字段语义已核实（读码收口，`src/pathfinder/yapf/yapf_rail.cpp:637-665`）

源码（第 394 轮树内，`ReservePath` 的失败分支）：

```cpp
const Track want = TrackdirToTrack(stdir);                          // :643
Train *owner = IsRailStationTile(ftile)
    ? GetTrainForWholeTileReservationArbitrated(ftile)
    : GetTrainForReservationArbitrated(ftile, want);                 // :644-646
const bool selfheld = (owner != nullptr && (owner == coupler || owner->First() == coupler->First())); // :648
const bool plain = IsPlainRail(ftile);                              // :651
fprintf(dbg, "CPL-RES-FAIL ... tbits=%d res=%d plain=%d ...",
    plain ? (int)GetTrackBits(ftile) : -1,                          // :657  tbits
    plain ? (int)GetReservedTrackbits(ftile) : -1,                  // :658  res
    (int)plain, ...);
```

由此三条**确凿**：

1. **`res=1` 就是 `GetReservedTrackbits(280,126) == TRACK_BIT_X(0x1)`**，即该格**唯一那条 X 轨已被预留**。⇒ §18.2 的「甲：根本没有预留位」**被排除**。位是真的。
2. **`owner` 是用 `want` 这单条轨去查的**（`GetTrainForReservationArbitrated(tile, want)`），不是整格。⇒ `owner=-1` 只说明「沿 want 这条轨回溯不到主人」，**不能推出整格无主**。
3. **`selfheld` 完全由 `owner` 派生**（`owner==nullptr` 直接 0）。⇒ §17.3 里「`selfheld=0` ⇒ 连自己都认不回来」的解读**过强**，它只等价于「owner 查空」。

⇒ 现在只剩「位存在但画面上没画」这一支（§18.2 的乙），需要核绘制侧条件。

### 18.7 绘制侧的已知前置（`src/rail_cmd.cpp`）

全库仅两处画预留：

```cpp
3860: if (_settings_client.gui.show_track_reservation && HasReservedTracks(ti->tile, track)) {
4026: if (_game_mode != GameMode::Menu && _settings_client.gui.show_track_reservation &&
           HasReservedTracks(ti->tile, CornerToTrackBits(halftile_corner))) {
```

⇒ 画不画只取决于两件事：**`show_track_reservation` 这个图形设置是否开启**，以及 `HasReservedTracks(tile, track)`。

`HasReservedTracks(t, b)` = `GetRailReservationTrackBits(t) & b`；`res=1` ⇒ 对 `TRACK_X` 这一位必然为真 ⇒ **只要该设置开着，280,126 上必然出现黑线**。

**REG-394-K（高，待玩家一句话）**：需要确认游戏内「显示轨道预留 / Show reserved tracks」是否开启。这是当前唯一能同时满足「`res=1` 为真」与「肉眼看不到」的解释。若玩家确认开着，则唯一剩下的可能是该位在采集瞬间之后被清（同 tick 内 `SetTrackReservation → ClearPathReservation`），需要改用逐 tick 采样去抓，而不是靠截图。

### 13.4 结论

玩家方案 D **成立，且优于第 394 轮已落地的显式信号截断**（`:710-724`）。建议按 D 改写该块，并保留 KI-563 A 的"保留已订"语义。同时删掉 `CPL-RES-SIGNAL-STOP` 探针里已无意义的 `pbs=` 字段（改为 `safe=`），新增 `CPL-RES-SAFE-STOP veh= nth= total= tile= td= booked=` 便于复测区分"停在 block 信号"还是"停在 PBS 信号前的安全位"。

## 十九、结案（玩家 2026-11-07 第二条现场观察）：不是孤儿位，是「同 tick 自订自撞」

### 19.1 玩家原话（决定性，原样保留）

> 「你是不是怀疑预留位在但是预览不显示啊，那我告诉你，我让车库里卡住的 veh27 强行通过信号，它顺畅的铺设预留并挂接了」

这条观察**一次排除两种解释**：

- 若 `280,126` 上真有一个持久的、占着的预留位（§17 的「孤儿位」），强行放行同样会在 `TryReserveRailTrack()` 撞位失败 —— 该函数不区分「谁在开车」，只查位是否被占。**它却通了** ⇒ 位上没有持久占用者。
- 若只是「位在但没画」（§18.2 的乙），强行放行也改变不了位的存在，仍会撞位。**它却通了** ⇒ 位在出库成功的那一刻根本不存在。

⇒ §18.2 的「甲/乙」二分、§18.7 的 REG-394-K（显示设置）**全部作废**：位不是常驻的。

### 19.2 与之逐条咬合的硬读数（`build\R3R_debug.log`，6961 行 / 653 630 B）

| 行号 | 原文（节选） | 含义 |
|---|---|---|
| `:3227` | `RESV-EXT-PRE veh=27 tile=280,126 enterdir=2 tracks=0x1 reserved=0x0 doRes=1` | 该 tick 起点，该格预留为 **0x0** |
| `:3252` | `CPL-RES-FAIL veh=27 nth=0/3 tile=280,126 ... res=1 ... owner=-1` | 若干行之后变 **0x1**，且认不出主人 |
| `:6407` | `RESV-EXT-PRE veh=27 tile=280,126 ... reserved=0x0` | 3180 行之后**又是 0x0** |
| `:6432` | `CPL-RES-FAIL ... res=1 owner=-1` | 同样又变 1 |
| `:6435` | `RESV-WATCH CLEAR-RAIL tile=280,126 track=0 phase=choose-track actor=27 head=27` | 同 tick 内被**本车自己**清掉 |

计数（PowerShell `Select-String`）：`CPL-RES-FAIL` 全日志 **52** 条、`RESV-EXT-PRE` **7** 条（其中 veh=27 在 `280,126` 只有 `:3227` 与 `:6407` 两条，均为 `reserved=0x0`）；`CPL-RESERVE veh=27 reserved=0 next=8 path=3 booked=0` 与失败行逐对出现，直到日志末尾 `:6943` 仍在失败。

**唯一自洽的时序**（同时解释「为什么全日志没有 `SET-RAIL tile=280,126`」）：

1. `ExtendTrainReservation()` 在 `CPL-ENTRY` 之前从库格出发，把 `280,126/TRACK_X` 订上（`reserved` 0x0 → 0x1）；
2. 因订不到安全位 / `long_reserve` 为真，不早退，继续走挂接寻路；
3. `R3RReservePathSequence()` 在 `steps[0]=280,126` 重订同一格 ⇒ `TryReserveRailTrack` 返回 false ⇒ 自订豁免 `R3RReservationHeldBySelf()` 依赖 `R3RFollowReservationToOwner()` 回溯，而回溯从 `280,126` 朝库格走一步即被 `IsPlainRailTile(279,126)==false` 截断（库格是 `TRACK_BIT_DEPOT`）⇒ 判「第三方占位」⇒ `booked=0`；
4. 该 tick 结束前整套预留被回滚/清除（`:6435` 现场抓到一次）；
5. 下一 tick `RESV-EXT-PRE` 读到的又是 `0x0`。

⇒ **位的存活期不足一个 tick**（订 → 判失败 → 清都在同一 tick 内），屏幕上不可能留下黑线。玩家「压根没看到预览」与「强行放行就通」两条观察**同时成立且互为印证**。

### 19.3 撤回与结案清单

| 编号 | 原结论 | 处置 |
|---|---|---|
| §17.3 | 「`280,126` 存在无主预留位（孤儿位）」 | **作废**：位是同一 tick 内本车自订的临时位 |
| §17.6 | 「孤儿预留 + trackdir 不匹配复合」 | **作废**：只剩 §12.3 的回溯断链 |
| §17.9 G-1 / G-3 | 真孤儿 / 订错轨 | **排除**：若真有无人认领的位，强行放行不可能成功 |
| §17.9 G-2 | 本车出库预留、归属回溯走不通 | **确认**（与 §11.1、§12.3 合流） |
| §18.2 甲 | 该格根本没位 | **排除**（`res` 取 `GetReservedTrackbits`，tick 内确实为 1） |
| §18.2 乙 / REG-394-K | 位在但渲染被抑制 / 显示设置关 | **作废**：不需要解释「为什么没画」，持久状态里没有位 |
| REG-394-I | `res=` 字段语义待核 | **结案**（§18.6 已核，确为 `GetReservedTrackbits(ftile)`） |
| REG-394-G | 谁铺了 `280,126` | **结案**：本车 `ExtendTrainReservation` |
| §17.8 结论 2 | 「只有 CLEAR-RAIL 无 SET-RAIL ⇒ 不是本车铺的」 | **作废**：`R3RResvWatchLog` 对非关注格走聚合、不打逐条 SET（`pbs.cpp:249-290`），**「日志没有 SET」不能推出「没设过」** |

### 19.4 强推为什么能通（机制层）

「强行通过信号」把车推进 `TrainLocoHandler` 的**移动分支**，走 `TrainController → TryPathReserve` 的**短预留**路径：`ExtendTrainReservation` 订到安全位即成功 ⇒ 命中 `res-ok-brief` 早退（`train_cmd.cpp` `:17386-17395` 一带）⇒ **根本不进挂接寻路** ⇒ 不会出现「第二次订 `steps[0]`」⇒ 不撞自己 ⇒ 顺畅铺位并完成挂接。

⇒ 卡与不卡的唯一差别，是**有没有走到挂接寻路那条支路**，与预留位状态无关。与 §11.1 / §12.3 判断完全一致。

### 19.5 KI-561 最终定性（替换 §17.10）

> **KI-561 根因**：库内机车执行 `OT_GOTO_COUPLE` 时，`ExtendTrainReservation()` 先把「库外第一格（`280,126`）的单一轨道」订下；随后因长预留 / 找不到安全位继续走挂接寻路，`R3RReservePathSequence()` 在 `steps[0]` 重订同一格，撞上自己刚订的位；自订豁免 `R3RReservationHeldBySelf()` 依赖 `R3RFollowReservationToOwner()` 从该格沿 plain rail 回溯找主人，而反向第一步就是库格（`TRACK_BIT_DEPOT`，非 plain rail）⇒ 回溯必然断链 ⇒ 两向皆 `nullptr` ⇒ 判「第三方占位」⇒ `booked=0` ⇒ `TryPathReserve` 失败 ⇒ `MarkTrainAsStuck()` ⇒ 每 tick 重复 ⇒ 永久卡在库里。

这是**结构性缺陷，不是脏数据**：任何从库内出发、需要走挂接寻路的列车都 100% 复现（与 §12.3 第 3 条一致）。

### 19.6 修法优先级（据此定稿）

| 顺序 | 方案 | 内容 | 理由 |
|---|---|---|---|
| 1（首选） | **B / 账本** | `R3RReservePathSequence` 不重订本车已持有的段：查 R3R 预留账本（KI-359 已在 `R3RResvWatchLog` 喂账）或「回溯失败但该格正是本车起点格/库格相邻」时直接视为已订、`booked++` 继续 | 源头不产生「需要证明我是我」这道题 |
| 2 | C / 起点链豁免 | 豁免条件从「回溯成功」改为「起点格 = 本车所在格 / 库格，且失败格是出库第一格」 | 事后补救，覆盖面窄于 B |
| 3 | A / 库格相邻豁免 | 见 §12 | 同上，且修不了「非库内出发但回溯断链」的同族 |
| ✗ | 清孤儿位 / 改显示设置 | — | **已证不需要**（位上没有持久占用者） |

### 19.7 复测判据（改码后）

1. 同场景：`CPL-RES-FAIL nth=0 ... res=1 owner=-1` 不再导致失败，或该行不再出现；
2. `CPL-RESERVE veh=27 reserved=1 booked=3`（不再 `0/3`）；
3. 出库后 `COUPLE-OK` 且持续行驶、`DEPOT-STAY stuck=0`；
4. 负判据：另一列车**真占**在 `280,126` 时仍须 `booked=0` 判阻塞（不得把第三方占用误认成自己）；
5. 负判据：`R3RFollowReservationToOwner` 回溯行为对非库内起点逐字不变。

---

## 20. 落地（第 395 轮，2026-11-07）：库口几何豁免

### 20.1 玩家决定性取证（本节即 §19 结案的触发点）

玩家原话：「我让车库里卡住的 veh27 强行通过信号，它顺畅的铺设预留并挂接了」。

推论链（这一步之后所有竞争假设全部关闭）：
- `TryReserveRailTrack()` 只看地图位、不看是谁在开车 ⇒ 若 `280,126`/`TRACK_X` 存在**持久**占用，强推一样过不去 ⇒ §17.3/§17.6 的「孤儿位」作废；
- 强推也改变不了「位存在但预览不画」⇒ §18.2 乙 / REG-394-K 作废；
- 强推走的是移动分支的**短预留**（`ExtendTrainReservation` 成功即 `res-ok-brief` 早退），**不进挂接寻路** ⇒ 不进 `R3RReservePathSequence()` ⇒ 不会产生「第二次订 `steps[0]`」⇒ 不撞自己。这与 §19 的「同 tick 自订自撞」完全一致，且是唯一的自洽解释。

### 20.2 落地位置与形状（仅 `src\pbs.cpp`，未碰 `src\*.h`）

两处改动，都在自订豁免函数里：

1. `R3RFollowReservationToOwner()` 新增可选出参 `TileIndex *end_tile = nullptr`：把 `FollowReservation()` 返回的 `ftoti.res.tile`（**回溯终点**）回传。理由：`FollowReservation()` 只走 plain rail（`pbs.cpp` 的 follow 循环），**库格不能进**，所以「从库内起步铺下的预订」回溯终点必是**库口格**，这就是"这段预订作者是我"的可验证指纹。
2. `R3RReservationHeldBySelf()` 的 `TileType::Railway` 分支，在「两向回溯都没找到任何列车」之后，追加：

```cpp
if (head->track == TRACK_BIT_DEPOT && IsRailDepotTile(head->tile)) {
    const TileIndex mouth = head->tile + TileOffsByDiagDir(GetRailDepotDirection(head->tile));
    if (end_a == mouth || end_b == mouth) {
        R3RDbgWrite("RESV-SELF-DEPOT-MOUTH veh=%u tile=%d,%d track=%u mouth=%d,%d end_a=%d,%d end_b=%d,%d\n", ...);
        return true;   // 不写地图；释放仍由驾驶预留负责
    }
}
```

`mouth` 用的就是库内挂接扫描（`train_cmd.cpp:14798`）的同一几何：`v->tile + TileOffsByDiagDir(GetRailDepotDirection(v->tile))`。

### 20.3 为什么这样改能覆盖整段而不是只覆盖第一格

对库口连续的自有预订 `E, E+1, E+2 …`：
- 对 `E+k` 朝库方向回溯：走回 `E` 后下一步试图进库格失败 ⇒ 终点恒为 `E`（= mouth）⇒ 命中豁免；
- 朝外侧回溯：走到本车预订末端，末端无车 ⇒ `nullptr`；
⇒ 每个 `E+k` 都判「自有」⇒ `booked` 逐个累加，`R3RReservePathSequence()` 不会在 `steps[1]` 处再撞一次，也不会退化成"只放行 1 格"。

### 20.4 为什么不会误伤第三方（负判据的代码依据）

| 情形 | 回溯结果 | 结果 |
|---|---|---|
| 第三方车停在预订任一端（普通轨） | 该向找到车，`a`/`b` 非空且 `a->First()!=head` | 照旧 `false`（阻塞） |
| 第三方占**库口格本身** | 前置 `ExtendTrainReservation()` 已在 `E` 失败 ⇒ 到不了挂接寻路 | 该支不可达 |
| 我车在普通轨/站台（非库内） | `head->track != TRACK_BIT_DEPOT` | 该支不进入，行为逐字不变 |
| 库口外真有外来位但两端无人（陈旧位） | 会被判自有 | 已知放宽项：位无主、无人会开过去，放行不会造成追尾 |

### 20.5 构建自证

见 §20.6（构建后回填）。

### 20.6 构建结果（回填）

**构建自证（第 395 轮，与 KI-563 修法 D 同一轮同一棵树）**：入口 `R3R_fullbuild.cmd inc detach`（复用既有入口，未新建任何 `.cmd`）；`build\R3R_incbuild.guard.log` = `GUARD: incremental is safe (no header/lang file is newer than the newest object)`（两个改动都是 `.cpp`，未碰 `src\*.h`，KI-183 增量合规）；`build\R3R_incbuild.log` 共 14 行，`[2/4] Building CXX object ...\src\pbs.cpp.obj` → `[3/4] ...\src\pathfinder\yapf\yapf_rail.cpp.obj` → `[4/4] Linking CXX executable openttd.exe`；`build\R3R_incbuild.done` = `EXIT_CODE=0`（2026-11-07 18:04:34）；`error C` / `fatal error` / `FAILED:` 计数均 0，`Linking` 命中 1 次；`src\pbs.cpp` 17:50:10 → `pbs.cpp.obj` 18:02:26、`src\pathfinder\yapf\yapf_rail.cpp` 17:58:20 → `yapf_rail.cpp.obj` 18:02:26、`build\openttd.exe` 2026-11-07 18:04:17（52 458 496 B）晚于两个 `.cpp`；`read_lints` 两文件 0 条；产物字面量核对 `RESV-SELF-DEPOT-MOUTH` = 1 命中、`CPL-RES-SAFE-STOP` = 1 命中、`stop_safe=` = 1 命中、旧的 `CPL-RES-SIGNAL-STOP` = 0 命中（已删干净）。

## 21 第 395 轮：KI-563 修法 D —— 截断判据由「信号」改为「安全停车位」

### 21.1 玩家拍板

第 394 轮首版（按「格上有信号」截断）造成 `veh0` 寻路紊乱后，候选 α/β/γ 全部被否；玩家拍板 **修法 D**：改用原生 PBS 的**语义**判据「第一个安全停车位」，不再自己判信号。原话要点：顺手把修法 D 改掉，然后再构建。

### 21.2 为什么首版必错（复述，防再犯）

第 394 轮实测（`build\R3R_debug.log`）：

```
3023 CPL-RES-SIGNAL-STOP veh=0 nth=0/15 tile=254,121 td=0 pbs=1 booked=1
3899 CPL-RES-SIGNAL-STOP veh=0 nth=1/14 tile=253,122 td=2 pbs=1 booked=2
```

该线路 PBS 信号密度 **1~2 格/个** ⇒ 首版每 tick 只订 1~2 格，`booked` 永远追不上 YAPF 的代价估计 ⇒ 预留持续落后于车 ⇒ 寻路紊乱。而原生 `pbs.cpp:1377-1382` 的 `FollowReservation()` **只在 `!IsPbsSignal()` 时 break** ⇒ "PBS 也截"比原生更严，属口径错误。根因是**判据选错层级**：信号只是安全位的常见形态，不是定义。

### 21.3 落地（仅 `src\pathfinder\yapf\yapf_rail.cpp`，未碰 `src\*.h`）

`R3RReservePathSequence()` 循环内，`ReserveSingleTrack()` 成功并 `++booked` 之后：

```cpp
if (IsSafeWaitingPosition(r3r_coupler, stile, stdir, true, r3r_forbid_90deg)) {
    stopped_at_safe = true;   // 打 CPL-RES-SAFE-STOP
    break;
}
```

循环外取一次：

```cpp
Train *const r3r_coupler = const_cast<Train *>(Train::From(Yapf().GetVehicle()));
const bool r3r_forbid_90deg = _settings_game.pf.forbid_90_deg;
```

`IsSafeWaitingPosition()` 就是原生那把尺子，同一函数的两处既有使用者：`ExtendTrainReservation()`（`train_cmd.cpp:16130`，驾驶预留）与 `FindSafePositionProc()`（本文件 `:162`）。⇒ 挂接预留与驾驶预留从此在**同一条边界**收尾；PBS 的 `NoEntry` / `AlwaysReserveThrough` / `TRPRF_RESERVE_THROUGH` / 90 度等细节全部交给该函数，R3R 侧不再自判。

### 21.4 两个实现要点（都是踩过才定下来的）

**① 必须「先订、后判安全位」，不能「先问空闲再订」。**
第一版写成 `if (IsWaitingPositionFree(...)) break;` 放在 `ReserveSingleTrack()` **之前**，会导致：上一次 tick 自己铺下的格（KI-361 自订豁免可订成功）被 `IsWaitingPositionFree()` 判成"占用"（它不排除自己）⇒ 预留终点被收到**车尾后面**。改为订成功后再判，既有格自己必然订得上，语义正确。

**② 函数尾 `return true;` 必须改成 `return booked > 0;`。**
修法 D 下存在"第一格即安全位、且该位被第三方占 ⇒ 一格都没订"的路径。若仍无条件 `return true`，调用方 `:1706-1716` 会把整条摸路当成已预留，而 `r3r_booked == 0` 时**连 `SetReservationTarget()` 收缩都不做**（收缩条件 `r3r_booked > 0 && r3r_booked < size`）⇒ 预留终点停在会合点、实际一格没订 ⇒ `reserved=1 / booked=0` 的空真（第 392 轮的老症状）复现。改成 `booked > 0` 后如实报失败，交 `train_cmd.cpp` 的 KI-316 路径处置。

**③ 安全位被占 ≠ 整批回滚。**
`ReserveSingleTrack()` 失败时走 KI-563 A 分支：`booked > 0` ⇒ `CPL-RES-PARTIAL` + `YapfNotifyTrackLayoutChange` + `return true`（停在它之前、保留已订格）。这正是 V-D3 要的口径，与 `ExtendTrainReservation()` 的整段 back-out **不同**（后者回滚的是"路径搜索失败"，前者是"停在边界前"）。

**④ 兜底不变。**
全程没有任何安全位（密信号 / 无岔口 / 纯弯）时循环照旧订满 `steps`；最后一格是会合点，`ChooseTrainTrack()` 已保证预留终点安全 ⇒ 不会比改动前订得更少。

### 21.5 日志口径

| 旧（第 394 轮，已删） | 新（第 395 轮） |
|---|---|
| `CPL-RES-SIGNAL-STOP veh= nth=/%u tile= td= pbs= booked=` | `CPL-RES-SAFE-STOP veh= nth=/%u tile= td= sig= booked=` |
| `CPL-RES-OK ... stop_signal=` | `CPL-RES-OK ... stop_safe=` |

`sig=` 只是"该格是否真有信号"的参考附带，**不参与判定**（用于事后区分"安全位带信号/不带信号"两种形态）。

### 21.6 复测判据

1. 挂接预留不再 1 tick 铺满全线 / 越过一串信号；应出现 `CPL-RES-SAFE-STOP`，且 `booked` 按安全位分段增长。
2. 第 394 轮首版造成的 `veh0` / `veh3` 寻路紊乱消失（PBS 密信号线路不再每 tick 只订 1~2 格）。
3. 失败时出现 `CPL-RES-PARTIAL booked=n`，不再整批回滚（`CPL-RES-FAIL booked-before=0` 仅在第一格就冲突时出现）。
4. 负判据：无安全位的线路行为不劣于改动前；`COUPLE-OK` 照常出现。
5. 与 KI-561 联合复测：库内机车出库挂车应出现 `RESV-SELF-DEPOT-MOUTH` 且随后 `COUPLE-OK`。

### 21.7 构建自证（第 395 轮）

- 入口：`R3R_fullbuild.cmd inc detach`（复用既有入口，未新建任何 `.cmd`）。
- 护栏：`build\R3R_incbuild.guard.log` = `GUARD: incremental is safe (no header/lang file is newer than the newest object)`（本轮只改 `.cpp`，未碰 `src\*.h`）。
- ninja：`build\R3R_incbuild.log`（14 行）`[2/4] ...\src\pbs.cpp.obj` → `[3/4] ...\src\pathfinder\yapf\yapf_rail.cpp.obj` → `[4/4] Linking CXX executable openttd.exe`；`error C` / `fatal error` / `FAILED:` 计数 0；`Linking` 命中 1 次。
- 判决：`build\R3R_incbuild.done` = `EXIT_CODE=0`，mtime 2026-11-07 18:04:34。
- 时间链：`src\pbs.cpp` 17:50:10 → `pbs.cpp.obj` 18:02:26；`src\pathfinder\yapf\yapf_rail.cpp` 17:58:20 → `yapf_rail.cpp.obj` 18:02:26；`build\openttd.exe` **2026-11-07 18:04:17，52 458 496 B**（晚于两个 `.cpp` 与两个 `.obj`）。
- 产物核对：exe 内 `CPL-RES-SAFE-STOP` 1 命中、`stop_safe=` 1 命中、`RESV-SELF-DEPOT-MOUTH` 1 命中；旧字面量 `CPL-RES-SIGNAL-STOP` **0 命中**（首版已删净）。
- `read_lints`：`yapf_rail.cpp` / `pbs.cpp` 均 0 条。
- 状态：**已实现 + 已编译**；游戏内复测（§21.6 的 5 条 + §20 的负判据）待玩家跑。



