# R3R 机车卡死 depot"等待空余轨道"——GOTO_COUPLE 在 depot 内被无条件锁死 (2026-09-07)

## 用户现象
机车处于 GOTO_COUPLE 挂接阶段,一直显示「等待空余轨道」(TRAIN_STUCK),不动作;
机车停在 depot tile(38,27),日志持续 `COUPLE-FAIL loco=0 ... tx=38 ty=27` 死循环。

## 日志时间线(关键行,R3R_debug.log)
1. 机车(veh=0)带车底(3..8)自 depot 38,27 出发(real order 0=couple,首轮在 depot 内 COUPLE-OK)。
2. 到站台 39,33 DECOUPLE-FIRE,车底留站台(real=3(17)=OT_WAIT_COUPLE,39,32/39,33)。
3. 机车回 42,28 depot(real=2(2)),随后又回 38,27 depot(real=3(2),tileEqDest=1)。
4. 在 38,27 depot 内 real order 推进到 4=GOTO_COUPLE(couple dest tile=39,31,站台!depot 口轨道南下)。
5. `COUPLE-FAIL` + `RESCHECK resAhead 38,37... 39,34 全 0x0` + `RESERVECONSIST ok=0`(车底在站台每 tick 自保留)死循环。

机车 couple order 的**目标是站台 39,31(车底 39,32/33 WAIT_COUPLE),不是当前 depot 38,27**;
但机车此刻停在 depot 内(couple order 激活于 depot 停靠期间)→ 被 `TrainLocoHandler` 的
depot-couple 分支锁死,永远驶不出 depot。

## 根因(train_cmd.cpp)
`TrainLocoHandler` 8821-8849:
```cpp
if (consist->track == TRACK_BIT_DEPOT && consist->IsEngine()) {
    if (consist->current_order.IsType(OT_GOTO_COUPLE)) {   // <-- 无条件
        TrainCoupleHandler(...); cur_speed = 0; ...        // 每 tick 强制 0 速
        ...
    } else {
        NormalizeTrainVehInDepot(consist, true);
    }
    ...
}
```
该分支把「机车在 depot + current order=GOTO_COUPLE」一律当作「机车已到达 couple 目标 depot,
停在 depot 内等车底」:每 tick `cur_speed=0`,只在 depot tile+库口 tile 两格找车底。
**没有校验 couple order 的目标是否是当前 depot**。于是:
- couple 目标为站台(39,31)/其它 depot、机车只是恰好停在 HOME depot(刚执行完 GOTO_DEPOT
  停靠、real order 推进到 couple)时 → 机车被锁死在当前 depot,永远无法驶出;
- couple handler 在库内找不到目标车底(车底在站台)→ COUPLE-FAIL 无限重试死循环。

对比:`CheckTrainStayInDepot`(5141-5143)早已有正确的目标校验——仅当
`GetCoupleIsDepot() && 目的地 depot == 当前 depot tile` 才让 couple 机车留在 depot;
否则机车应驶出 depot 去 couple 目的地。8821 分支抢先于它(9052)无条件锁定,绕过了该校验。

界面「等待空余轨道」:`VehicleRailFlag::Stuck` 置位后 GUI(vehicle_gui.cpp 4299)显示
STR_VEHICLE_STATUS_TRAIN_STUCK;机车困在 couple 寻路/保留失败路径上被 MarkTrainAsStuck。

## 修复(train_cmd.cpp 8821-8849)
depot-couple 分支只在 couple order 目标确为**当前 depot** 时执行(couple depot 语义:
`GetCoupleIsDepot()` + `IsRailDepotTile(tile)` + `destination.ToDepotID()==GetDepotIndex(tile)`,
与 CheckTrainStayInDepot 5141-5143 同判据);couple 目标非当前 depot 的机车不锁速、不库内 couple,
直接走正常驶出流程(由 couple pathfinder 去站台/其它 depot 挂接);
非 couple order 机车仍走原有 `NormalizeTrainVehInDepot`。

## 兼容与回归点
- depot couple(车底就在 depot 内等挂):8821 条件满足 → 原行为不变。
- 机车 HOME depot 停靠 + couple 目标站台:修复后驶出 → 目标车底 WAIT_COUPLE 在站台 → couple pathfinder(6363-6383)挂接。
- 用户 UI「等待空余轨道」随死锁消失而消失。
- 验证:重放本场景,机车应驶出 38,27 到 39,31 挂上站台车底(尾对尾?方向细节待实测确认)。

---
## 第二轮(2026-09-07 修复后复测):仍卡「等待空余轨道」——couple pathfinder 拒绝 depot 起点

### 现象(修复 8821 后日志 6963-7677)
机车不再被 depot 分支无条件锁速,但**仍困在 depot 38,27 内**,stuck=1,每 tick 重复:
```
CT veh=0 tile=38,28 ...          ← ChooseTrainTrack 的 new_tile=库门口格(注意:不是机车位置!)
CPL-ENTRY veh=0 tile=38,27       ← couple pathfinder 起点=机车 tile(depot)
CPL-FOLLOW from=38,27 to=38,28   ← YAPF 主寻路能正常从 depot 展开(起点 node parent==null 放行)
CPL-PATHFOUND found=1 best=39,33(车底)
CPL-BACK 39,33→38,29→38,27       ← 回溯链完整
CPL-TRACE st=38,27 tdb=0x202     ← 单段分支:暴力 trace 从机车 tile 开始
REACH cur=38,27 td=1 depth=0     ← 第一步就被 depot 拒绝规则杀死
CPL-TRACE-RESULT st=38,27 best=255
CPL-RESERVE reserved=1 next=255  ← next=INVALID_TRACKDIR
→ DoTrainCouplePathfind 返回 INVALID_TRACK(6367)
→ path_found==INVALID(6376-6377) → MarkTrainAsStuck → 「等待空余轨道」
```
### 根因(yapf_rail.cpp ~729-960,couple pathfinder 回溯后的单段分支)
机车在 depot 内、couple 目标是站台 → 出库路径预留必须走 ChooseTrainTrack 的 couple 分支(6363-6383),
即 couple pathfinder。YAPF 主寻路从 depot tile 出发没问题;但**回溯后**进入
`pNode->parent==nullptr && pPrev!=nullptr` 的单段处理(766):为求真出发方向,它用
`reach()`/`rp()` 递归从机车 tile(st=pNode->GetLastTile()=38,27 depot)暴力 trace 到车底(tg):
```cpp
if (IsRailDepotTile(cur)) return false;   // reach():cur=st=起点 depot → 第一步就 false
if (IsRailDepotTile(cur)) return false;   // rp():  同
```
该规则本意「couple 路径不得穿过 depot」,但当**机车自己就在 depot 内**(起点=depot tile)时,
把起点一并误拒 → best_td=255 → next_trackdir 被覆盖为 INVALID_TRACKDIR(950-953)
→ DoTrainCouplePathfind 返回 INVALID_TRACK → MarkTrainAsStuck。
机车从此每 tick stuck 分支直接 return(9129),仅周期重试 couple pathfinder,同样失败,永久等待。

### 修复(yapf_rail.cpp)
`reach()` 与 `rp()` 的 depot 拒绝规则放行起点 tile(cur==st):
```cpp
if (IsRailDepotTile(cur) && cur != st) return false;  // reach:放行自己出发的 depot
if (IsRailDepotTile(cur) && cur != st) return false;  // rp:  放行自己出发的 depot
```
- cur==st 只出现在递归第 0 层(起点),之后 cur 恒为后继轨道 tile;
- depot 起点由 TryReservePath(956)/SetDepotReservation(6620)处理,rp 对 depot 不 TryReserveRailTrack
  (855/907 等处已有 `!IsRailDepotTile(cur)` 跳过),不会触发 pbs.cpp:144 平台/depot 直接保留断言;
- 主寻路 CPL-FOLLOW from=38,27 to=38,28 已证明 TrackFollower 能从 depot tile Follow 出库,
  reach/rp 复用同一 TrackFollower,出库方向(td=1/9 中的门外方向)可正常 trace。

### 待验证
机车应在 depot 内 reserve 成功(含 SetDepotReservation)→ 加速出库 38,28 → 沿保留路径
驶向 39,33 与站台车底挂接;期间 stuck 应清除。
