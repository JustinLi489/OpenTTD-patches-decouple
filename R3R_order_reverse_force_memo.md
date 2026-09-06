# R3R 备忘:order「调向」升级为强制物理换端 + 解挂点规则确认

- 日期:2026-09-04
- 分支:`feature/decouple`
- 相关文件:`src/train_cmd.cpp`
- 复现日志:`build/R3R_debug.log`

---

## 0. [2026-09-04 追加] 决定:弃用 force 换端方案(代码暂不删除)

- **决定**(用户原话):"force 换端我打算弃用它了,但是不急着删除"。
- **弃用对象**:本 memo §1 的整套方案——order「调向」到达后走
  `ForceFlipReverse` → `ReverseTrainDirection` 强制整链 first/last
  `ReverseTrainSwapVehicles` 物理换端。
- **弃用原因**:与记忆 97996690 规则冲突。该规则要求:含多段(★ 划分)的链
  做 first/last 反转必须"逐段各自反转、段序不颠倒、★ 迁新段头";而 force
  换端正是对整链做 `ReverseTrainSwapVehicles`(整链空间旋转),会颠倒段与段
  的运营次序,在多段链上不合法。既然换端方向改为"逐段各自反转",force 整链
  换端这一实现路径被放弃。
- **保留但不删的代码**(勿清理,等逐段反转方案验证后一并处置):
  - `train.h` `VehicleRailFlag::ForceFlipReverse = 23` 标志;
  - `train_cmd.cpp` 消费点:`ReverseTrainDirection` 入口
    `3084-3085`(读+清标志)、force_end_swap 分支 `3200-3224`;
  - 触发点三处:`6224-6229`(station 到达)、`6286-6294`(waypoint 正常到达)、
    `8495-8501`(waypoint fallback),以及按钮路径 `3450`;
  - 保护:`2175-2181`(consist 重构清除标志,既有保护,保留)。
- **status**:force 分支当前仍生效(未删除),但不再是 R3R 目标行为;
  后续替换为逐段反转原语后再评估删除。

---

## 1. 结论:order「调向」属性 = 强制 first/last 物理换端(用户拍板)

玩家期望在路点 / 站台上勾选「调向」后,列车完成**首尾物理互换**
(与车辆视图「调转方向」按钮一致),而不是现在的「原地转向」。

### 1.1 现状(修改前)

- 车辆视图按钮路径(force 换端):
  - `CmdReverseTrainDirection` 收到 `force_flip_reverse=true`
    → `train_cmd.cpp:3450` `flags.Set(VehicleRailFlag::ForceFlipReverse)`
    → `ReverseTrainDirection()` 入口 `3084` 消费该标志得 `force_end_swap=true`
    → `3200-3224` 先清 `DrivingBackwards`,再
    `AdvanceWagonsBeforeSwap → ReverseTrainSwapVehicles → AdvanceWagonsAfterSwap`,
    实现 first/last 互换。
- order「调向」路径(仅 db 翻转,不换端):
  - waypoint 正常到达:`6161-6169`(`ReverseAtWaypoint`,日志 `REVERSEDIR force_swap=0`)
  - station 到达:`6223-6236`(`ReverseAtStation`)
  - waypoint fallback:`8408-8426`(停车在路点 tile 上但未到停靠点)
  - 三处都直接 `ReverseTrainDirection(consist)`,不带 force → 只走
    `3225` 的 backup / 普通翻转分支。日志判据:`REVERSEDIR ... force_swap=0`
    → `REVERSEDONE db=0 mvfront=0`,RF 打印顺序不变。

### 1.2 修改

三处 order 调向触发点在调用 `ReverseTrainDirection` 前,
先 `consist->flags.Set(VehicleRailFlag::ForceFlipReverse);`
(与按钮路径 `3450` 语义一致,`ReverseTrainDirection` 入口消费并清除)。

- `6161-6169` waypoint 正常到达
- `6223-6236` station 到达(含 `AM_ORIGINAL && cur_speed != 0` 时
  `Flip(Reversing)` 的延迟翻转路径——force flag 已先置位,减速停稳后的
  翻转同样消费)
- `8408-8426` waypoint fallback

### 1.3 不改的位置

- `8372`:`ProcessOrders + CheckReverseTrain` 自动折返,不得 force。
- depot 调向(`HasReverseAtDepot`)不在本次范围。
- `2175-2181`:consist 重构(合并/解挂)会清除 `ForceFlipReverse`,属既有保护,保留。

### 1.4 复测判据

日志中应为:
```
REVERSEDIR veh=0 ... force_swap=1
  RF idx=...      ← 各节 RF 打印首尾对调
```

---

## 2. 解挂点规则与车底排程配置核对

### 2.1 规则(已确认)

DECOUPLE 不携带目的地。它触发于:列车停稳在**前一条真实命令**
(GOTO_STATION / GOTO_DEPOT / GOTO_WAYPOINT)的终点时,发现下一条真实命令
是 DECOUPLE → 在当前位置解挂。车底被解下后留在该位置。

### 2.2 用户当前配置(车底排程 7 条,GUI 从 1 起数)

| GUI 编号 | 类型        | 目的地 |
|----------|-------------|--------|
| 1        | WAIT_COUPLE | -      |
| 2        | GOTO_STATION| -      |
| 3        | DECOUPLE    | -      |
| 4        | WAIT_COUPLE | -      |
| 5        | GOTO_DEPOT  | 42,28  |
| 6        | GOTO_DEPOT  | 38,27  |
| 7        | DECOUPLE    | -      |

### 2.3 规则推论(符合预期)

- 命令5 → 停在 42,28:下一条是命令6(GOTO_DEPOT),**不触发解挂**,继续行驶。
- 命令6 → 停在 38,27:下一条是命令7(DECOUPLE),**在 38,27 触发解挂**。✓

即:按此配置重测,解挂应发生在 **38,27**,无需改代码。

### 2.4 旧日志为何解挂在 42,28

`R3R_debug.log:6428` `DEPOT-ARR real=5(2) ... destTx=42 destTy=28 tileEqDest=1`,
`6432` `DECOUPLE-FIRE tx=42 ty=28`。

说明**旧日志录制时,real=5(第 6 条命令)的终点是 42,28**,与 2.2 的当前
配置(第 6 条 → 38,27)不一致 → 判定旧日志为配置修改前的产物
(可能当时命令5/6 的 depot 目的地顺序与现在相反或第 6 条曾指向 42,28)。

**待办**:按当前配置重测。
- 若在 38,27 解挂 → 规则无问题,结束。
- 若仍在 42,28 解挂 → 查命令6 是否被跳过/未执行。
  疑点线索:`6427 SKIP-STOPPED veh=0 order=5 real=2 spd=0 tile=42,28`
  (探针 `train_cmd.cpp:8170-8181`,列车曾以 Stopped 态停在 42,28)。

### 2.5 解挂流程参考日志(旧)

```
6428:DEPOT-ARR real=5(2) ... depot 42,28
6432:DECOUPLE-FIRE consist=0 tx=42 ty=28 real=5
6433:ADVANCE veh=0 order=5 real_before=3 implicit_before=3
6434:DECOUPLE-DONE u=3 isCG=1 real=0 ...   ← u=3 车底头,解下留原地
6442:LOCO-AFTER-DECOUPLE ... real=4        ← 机车还原为自身 5 条排程
```

---

## 3. 附带问题(待用户确认,不影响本次改动)

解挂后机车停在 42,28,随后按机车命令5(GOTO_DEPOT)→ 38,27 进库,
命令1(GOTO_COUPLE)在 38,27 找不到车底(车底已解在 42,28),
导致 `COUPLE-FAIL` 死循环刷屏(`6461-6511`)。

需确认:机车这条 GOTO_COUPLE 预期去挂哪组车底、当时应在哪个 depot。

---

## 4. 本次改动动作清单

1. [x] 本备忘。
2. [ ] `train_cmd.cpp` 三处调向触发点前设置 `ForceFlipReverse`。
3. [ ] vcvars64 环境下重编 `openttd.exe`。
4. [ ] 玩家复测:确认 force_swap=1 + 在 38,27 解挂。
