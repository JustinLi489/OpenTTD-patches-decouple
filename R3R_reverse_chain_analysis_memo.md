# R3R「反转链后异常」分析备忘

> 续作先读本文件。分支 `feature/decouple`(领先 origin 3 commits;工作区含 depot M2 等改动,勿覆盖)。
> 创建:2026-09-04。任务来源:用户提供 `build/R3R_debug.log`,要求分析**反转链之后出了什么问题**。

## 1. 本文件目的
记录本次对 `build/R3R_debug.log` 的分析过程与结论,防止会话压缩丢失上下文。
读者应同时参考:
- `R3R_goto_couple_flip_plan.md`("到达后调向 / 到站反转" Reverse-on-Arrival 方案);
- `R3R_segment_decouple_plan.md`(按段解挂 M1/M2/M2-UI,均已实现并编译通过);
- 记忆 31435600 / 60885272(折叠崩溃定位、GOTO_COUPLE 方案演变)。

## 2. 已知上下文(分析前)
- "反转链" = 列车整列调向(reverse)操作后的链条行为。相关代码点:
  `train_cmd.cpp CmdReverseTrainDirection(~3344)` / `ReverseTrainDirection(~3070)` /
  `BeginLoading` hook(vehicle.cpp,Reverse-on-Arrival 自动调向挂点,若已实现)。
- 崩溃史:veh=2 进 tile 39,30 找不到 `_connecting_track` 折叠崩溃;fold-geom 探针待删。

## 3. 结论(2026-09-04,基于 `build/R3R_debug.log` 9811 行全量重构)

### 3.1 反转链本身:成功,无崩溃
- 两次 waypoint 折返均正常执行 `REVERSEDIR → REVERSEDONE`,无 fold-geom、无折叠崩溃(记忆 31435600 的崩溃场景在本日志未复现):
  - 折返 #1(行 2495-2505):waypoint1 38,35,`db=0` → `REVERSEDONE db=1 mvfront=2`(backup 模式,移动端 veh2),之后 CRT origin=38,35 found=1,机车正常行驶。
  - 折返 #2(行 6775-6784):waypoint2 37,28,`db=1` → `REVERSEDONE db=0 mvfront=0`(flip 复原,移动端回 veh0),调向结果正确。
- 之前修的折叠保护在此场景有效;问题出在**反转链完成之后**,与折叠无关。

### 3.2 失效现象(反转后卡死)
- 行 6783:`CT veh=0 tile=37,29 curType=2 real=4` —— 机车离站找路时,lookahead 订单已推进到 **idx4(GOTO_DEPOT)**,`curType=2`=OT_GOTO_DEPOT。
- 行 6784:`CRT veh=0 order=2 dir=3 origin=38,29 td=5 found=0` —— 机车以 GOTO_DEPOT 为目标从 tile 38,29 路径搜索**失败**;这是全日志唯一一次 `order=2` 的 CRT、也是唯一一次 `found=0`。
- 此后 ~3000 行全部是同一状态重复:`DEPOT-ARR veh=0 curType=9(WAITING) tileEqDest=1`(机车永久困在 waypoint2 tile 37,28)+ 解挂段 `veh=3..8 RESERVECONSIST ok=0` 反复失败。整局死锁:机车永远到不了 station 接段,解挂段永远等不到耦合,站台 reserved-track 状态异常(ok=0)。

### 3.3 根因
机车在 waypoint2(37,28)折返完成后本应执行 **idx3 = GOTO_COUPLE**(回 station 39,33 接解挂段 veh3-8),但订单推进把它**当不可执行订单跳过**,直接落到 idx4 GOTO_DEPOT:
- 代码点:`src/train_cmd.cpp` `VehicleOrderSaver::SwitchToNextOrder`(~5067-5088)的 switch 只含 `OT_GOTO_DEPOT/OT_GOTO_STATION/OT_GOTO_WAYPOINT`、`OT_CONDITIONAL`,`OT_GOTO_COUPLE` 落入 `default` → `++cur_real_order_index` 被跳过。
- 调用链:`AdvanceOrdersFromVehiclePosition`(~5098-5132,机车停靠 waypoint 离站找路时命中 5121→5129)→ `SwitchToNextOrder(true)` → idx2→3(GOTO_COUPLE 被跳)→4(GOTO_DEPOT,设为 current_order)→ ChooseRailTrack 以 GOTO_DEPOT 找路 → 6784 found=0。
- 位置佐证:CT 探针(5550-5567)恰在 `orders.AdvanceOrdersFromVehiclePosition/AdvanceOrdersFromLookahead`(5538-5541)之后,所以它打印的 `real=4` 正是临时推进结果。
- 佐证:全日志不存在任何 `CRT order=16`(GOTO_COUPLE 路径搜索)或 couple-pathfind 成功行——idx3 从未被执行过;而 depot 内的 idx0 GOTO_COUPLE 由 depot 组装/耦合代码直接处理,不经过订单推进,所以 depot 场景不受影响。

## 4. 后续动作(建议)
1. **✅ 已实施(2026-09-04)**:`SwitchToNextOrder`(train_cmd.cpp)已加 `case OT_GOTO_COUPLE`,与 GOTO_STATION/GOTO_WAYPOINT 并列——机车 waypoint/station 完成后推进时能选中 GOTO_COUPLE,不再被 default 跳过而直奔 idx4 GOTO_DEPOT。
2. **✅ 已实施**:`AdvanceOrdersFromVehiclePosition` 对 `OT_GOTO_COUPLE` 与 `OT_GOTO_DEPOT` 同样提前 return(lookahead 不推进),执行 GOTO_COUPLE 期间订单推进交给 R3R 耦合完成逻辑。
3. **✅ 已实施(2026-09-04,第二复现轮)**:`PfDetectDestination`(yapf_destrail.hpp):机车驶近后自身 reservation 覆盖等待组所在 platform tile,`GetTrainForReservation` 返回机车自己(GOTO_COUPLE)→ 终点判定永远失败(found=0,机车卡在等待组前一格)。修复:把 GOTO_COUPLE 主车 own 的 reservation 视为空并落入平台扫描;平台扫描同样忽略 GOTO_COUPLE owner,并让 VehiclesOnTile fallback 接受 `WAIT_COUPLE` 主车(不只 IsConsistGroup)。此轮 found=0 与 first/last 反转无关——Couple(3919)本支持 nose-to-nose,卡点是机车永远拿不到最后一段路径 → 不移动 → 无碰撞 → 永不 Couple。
4. 待办(排障后):移除全部硬编码探针(R3RTrackBitsProbe/fold-geom/CT/CRT/REVERSEDIR/REVERSEDONE/DEPOT-ARR/CHAIN/RESERVECONSIST/CPL-*/PFD/PFD-SCAN/FOLDCHK 等),删除 R3R_debug.log 相关代码。
