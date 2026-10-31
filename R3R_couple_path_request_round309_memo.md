# R3R 挂接寻路三项前置要求核查（第 309 轮，2026-10-26）

本轮**零源码改动、零构建**，只核查玩家提出的三条前置要求是否已经满足。
对应 KI 条目：`R3R_KNOWN_ISSUES.md` 的 **KI-490**（第 309 轮小节）。

## 一、玩家要求原文（要点）

1. 要先配对成功，才能准备发起寻路。
2. 要完全停稳，才能准备发起寻路。
3. 挂接寻路应当像普通寻路一样逐步铺设预留，只不过终点被限定在了某一条特定的到发线。

## 二、核查结论（一句话版）

- ① **已做到**
- ② **未做到**（缺口在此）
- ③ **基本做到**，但「特定到发线」的含义需要玩家确认是「目标车底所在的那条」还是「可由排程指定的第几条」

## 三、逐条取证

### ① 先配对 —— 已做到

落点：`src\train_cmd.cpp` 的 `R3RIsParkedWaiting()`（约 17346 起），`parked-no-pair` 支（约 17404-17407）。

判据形态（读到的要点）：

- 条件 = 当前订单 `IsType(OT_GOTO_COUPLE)` **且** `R3RGetCouplePairPartner(...) == nullptr` **且** 不在库里
- 命中即视为「该车应停放」，**拒绝本次订路**并写 `reason=parked-no-pair`
- 这支**排在速度门之前**，即不看 `cur_speed`、也不看「是否已到目的地」

⇒ 只要还没有配对锁，挂接车就订不到任何路线；配对一旦建立，该支自动失效。满足要求①。

### ② 完全停稳 —— 未做到（关键缺口）

同一函数的速度门（约 17428）：

```
if (consist->cur_speed != 0) return false;   // 放行
```

含义：车一旦**有速度**，`R3RIsParkedWaiting()` 就交出 false，后续 `TryPathReserveWithResultFlags()`（约 17596 前）照常往下走到 `R3RTryPathReserveCore` → `ChooseTrainTrack()`（`train_cmd.cpp:16571`），而 `OT_GOTO_COUPLE` 且非车库型订单在这里走的就是挂接寻路 `DoTrainCouplePathfind()`（`train_cmd.cpp:15489` → `YapfTrainCoupleTrack`）。

所以现状是：**已配对 + 还在滑行 ⇒ 立刻发起挂接寻路**。

旁证（更强的反证）：代码注释自陈「the moment the lock exists this predicate turns false AND the pairing code books a route by itself (a chain at cur_speed 0 never enters the movement branch, KI-348, so nothing else would ever wake it up)」
—— 即配对成功处会**主动替它订一次路线**来唤醒它。设计取向是「锁一到就出发」，与要求②正好相反。

另有两条相关事实，说明「停稳」目前只在别处被用到、没被用到准入上：

- `r3r_couple_stuck_retry`（`train_cmd.cpp:16637-16640`）：只有 `cur_speed == 0` 的挂接车才绕过「下一格已有预留就直接返回」的捷径去重算挂接路径。这是**停稳后的补救**，不是**停稳才发起**。
- `R3RCoupleScanHold()`（`src\pathfinder\yapf\yapf_rail.cpp:1838`）只是「空搜索后 8 tick 内不再搜」的节流，与速度无关。

⇒ 要求②目前**不成立**。

### ③ 逐格预留 + 终点限定 —— 基本做到

**逐格铺设**：`DoTrainCouplePathfind`（`train_cmd.cpp:15489`）→ `YapfTrainCoupleTrack` → YAPF 回溯 lambda `rp()` 内逐格 `reserve_if_present(tile, track)`，与普通寻路（`DoTrainPathfind` / `YapfTrainChooseTrack`）**同一套铺设机制**，不是"一次性整条占位"。第 240 轮（KI-346）修的就是这条 `rp()` 里"进入轨算错成同 Track"的缺陷，也侧面证明它确实在逐格订。

**终点限定**：终点来自挂接目标的搜索（`FindNearestCoupleTrain`，YAPF destrail），即「配对锁定的那一列车底所在位置」，而**不是**订单上写的车站/车库 tile；在配对尚未就绪时，`R3RShouldCutCoupleAtWaitPoint()`（约 17028/17041）会把终点截断在等待区路点，绝不铺到车底身上。

⇒ 从"逐步铺设"和"终点不是订单格而是特定位置"这两点看，要求③已具备。唯一待澄清的是「特定到发线」的粒度（见 §四 C-490-2）。

## 四、待玩家拍板

- **C-490-1（②怎么落）**
  - 方案 A（推荐）：在 `R3RShouldCutCoupleAtWaitPoint()` 的 `pair_ready` 判据里加一条「本车 `v->cur_speed == 0`」。效果：未停稳 ⇒ 终点只能到等待区路点；停稳 ⇒ 才把终点放到车底所在到发线。不新增闸门、不会让车在半路失去路线。
  - 方案 B（字面）：已配对但未停稳就拒绝一切订路。副作用：车靠 `GetCurrentMaxSpeed()=0` 急刹，可能停在道岔/隧道/桥上。
- **C-490-2（③的粒度）**
  - 现状语义 = 「目标车底所在的那条到发线」；
  - 若要求「指定站内第几条到发线」，需新增订单字段 + UI，属新功能，不在本轮范围。

## 五、状态

- ① 已具备、③ 基本具备，**② 未实现**。
- 本轮未改任何源码、未构建；`build\openttd.exe` 仍是第 308 轮（2026-10-26 12:40:47，52 253 696 B）。
- 拍板后落地方案 A 的工作量：单文件 `src\train_cmd.cpp` 一处判据 + 一条探针（建议 `CPL-WAIT-SPD`，记录 `veh/spd/pair/ready`），增量构建即可。
