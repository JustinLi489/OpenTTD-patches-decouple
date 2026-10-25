# 第 297 轮：`order_cmd.cpp:4909` 断言崩溃（2026-10-25）

玩家原话：

> 我把游戏挂了一阵子，回来的时候看到 order_cmd 4909行断言崩溃了，日志应该也跑好了

本轮**先取证**。第 296 轮的探针是分端日志版（`R3R_DBG_FILE` + 批量探针默认关），本次是它第一次跑出实机数据。

---

## 一、已确认事实（先落盘，待续）

### 1.1 崩溃点原文（`src\order_cmd.cpp:4909`）

```cpp
4908:	/* Get the current order */
4909:	assert(v->cur_implicit_order_index == 0 || v->cur_implicit_order_index < v->GetNumOrders());
4910:	v->UpdateRealOrderIndex();
```

即 **`cur_implicit_order_index` 越界**：既不是 0，也不小于 `GetNumOrders()`。
函数是 `ProcessOrders()`（`order_cmd.cpp` 内），上游刚刚跑完 via 判断（`4893-4906`，含第 286 轮 KI-453 的
`r3r_wait_waypoint && v->cur_speed != 0` 追加条件）——**该分支会调 `v->IncrementImplicitOrderIndex()`**，
是本次崩溃的头号嫌疑点（候选 A：自增越过表尾；候选 B：索引在别处被写坏）。

### 1.2 第 296 轮分端日志**已生效**（部署成功）

| 文件 | LastWriteTime | 大小 | 行数 |
|---|---|---:|---:|
| `build\srv\R3R_debug.log` | 2026-10-25 11:22:30 | 987 735 B | 待填 |
| `build\cli\R3R_debug.log` | 2026-10-25 11:22:50 | 983 831 B | 待填 |
| `build\cli\R3R_perf.log` | 2026-10-25 11:26:47 | 390 658 B | — |
| `build\srv\R3R_perf.log` | 2026-10-25 11:22:29 | 298 997 B | — |

对比第 296 轮之前那份单文件日志（`build\R3R_debug.log`，4.9 MB / 41 658 行，10-24 23:46）：
两端各自约 **0.99 MB**，且**没有再合写**。⇒ 判据 1（分端独立）与判据 2（体积下降）方向正确，
但体积尚未降到备忘 §二.3 预估的 4 000~5 000 行量级，需按标签分布核对剩余噪声来源（待续）。

### 1.3 REG-297-A 已澄清：读取时日志尚未写完

```
11:26:45  首读：crash-20261025T032645Z.log = 0 B        <- 当时还在写
11:27:09  再读：crash-20261025T032645Z.log = 23 808 B   <- 完整
```

玩家指出是读取时机问题。**结论：崩溃日志有效，栈回溯可用**，不必靠 `R3R_debug.log` 反推。
（教训：crash handler 是分几段追加写盘的，看到 0 B 应等待并复核 mtime，不要立刻判定"没有栈"。）

### 1.4 【决定性】崩溃现场：**崩溃车是克隆出来的那列车**

crash log 头部：

```
Crash reason: Exception E1212012
Message: Assertion failed at line 4909 of src\order_cmd.cpp:
         v->cur_implicit_order_index == 0 || v->cur_implicit_order_index < v->GetNumOrders()
Within context:
  0: CallVehicleTicks: veh: 99: (Train 7 (JRW_关西机场快速 03), c:0, st:FE, vs:HD,
     vf:F, vcf:l, gvf:s, tf:qJHSO, trk: 0x80, [tile: 5E761 (1889 x 188), type: 10 (Railway)])
Last sync record type: VEH_EFFECT
Non-main thread (ottd:game)
```

栈（`:30-51`）：

```
[05] ProcessOrders        + 930   order_cmd.cpp:4909
[06] TrainLocoHandler     + 9333  train_cmd.cpp:21506
[07] Train::Tick          + 307   train_cmd.cpp:22063
[08] CallVehicleTicks     + 1444  vehicle.cpp:1734
```

逐项：`veh 99`；`trk: 0x80` = `TRACK_BIT_DEPOT` ⇒ **车停在车库格内**（tile 1889 x 188, type 10 Railway）；
`st:FE` ⇒ 是前引擎（FrontEngine）；`vs:HD` ⇒ 停着（Stopped/Halted）；`tf:qJHSO` 含 `J`、`H`、`S`、`O`。

**命令日志（`:345-370`）是决定性的**——读档后 24 条命令里前 12 条全是克隆：

```
cmd: 09C CmdCloneVehicle | 21, true --> 51
cmd: 09C CmdCloneVehicle | 21, true --> 99     <-- 崩溃车 99 就是它
cmd: 09C CmdCloneVehicle | 24, true --> 75
cmd: 09C CmdCloneVehicle | 24, true --> 123
... 之后 0EF CmdDeleteCoupleGroup、09F CmdMassStartStopVehicle、055 CmdPause
```

⇒ 崩溃的 `veh 99` = **克隆源的克隆品（源 21）**，玩家在读档同 tick（`1920-01-15 22:16`）连克隆了 4 列车，
之后挂机 `28870` state ticks（游戏内 1920-01-15 → 1920-01-27，12 天）才炸。

⇒ **REG-297-C（强怀疑，待代码取证）**：克隆把源链的订单索引带进了新链，而新链的订单表条数不同 /
隐式订单尚未生成 ⇒ `cur_implicit_order_index >= GetNumOrders()`。注意设置项 **`advance_order_on_clone: true`**
（`:343`）——克隆时还会主动推进订单索引，是"越界"的直接放大器。

### 1.5 时间线（同一台机，同一会话，与 §1.3 一致）

```
11:22:29  build\srv\R3R_perf.log  最后写入
11:22:30  build\srv\R3R_debug.log 最后写入
11:22:50  build\cli\R3R_debug.log 最后写入
11:26:45  crash 文件开始写（首读 0 B）
11:26:47  build\cli\R3R_perf.log  最后写入
11:27:09  crash 文件写完（23 808 B）
```

崩溃会话 `Network: server`（`:74`）、`Clients: #1 '廣州站' company 0 / #4 company 255`（`:91-93`）
⇒ 崩溃的是**服务端进程**（srv 侧），cli 侧（玩家客户端）随后才退出（perf.log 晚 2 秒对得上）。
此前 REG-297-B 的"谁崩的"疑问**已解决：srv 崩**。

---

## 二、根因链（代码取证到 3226/3251，再往下待续）

### 2.1 【已确认】克隆边上的"推进订单"就是越界值的唯一已知制造者

`src\order_cmd.cpp:3226` 起有一个只在**克隆**时跑、且有四个门禁的函数：

```cpp
3226: static void CheckAdvanceVehicleOrdersAfterClone(Vehicle *v, DoCommandFlags flags)
3228:     const Company *owner = Company::GetIfValid(v->owner);
3229:     if (!owner || !owner->settings.advance_order_on_clone || !v->IsInDepot() || !IsDepotTile(v->tile)) return;
3233:     const int order_count = v->GetNumOrders();
3241:     } else if (GetDepotVehicleType(v->tile) == v->type) {
3242:         for (VehicleOrderID idx = 0; idx < order_count; idx++) {
3243:             const Order *o = v->GetOrder(idx);
3244:             if (o->IsType(OT_GOTO_DEPOT) && o->GetDestination() == GetDepotIndex(v->tile)) {
3245:                 target_orders.push_back(idx + 1 < order_count ? idx + 1 : 0);
3246:             }
3247:         }
3248:     }
3249:     if (target_orders.empty()) return;
3251:     VehicleOrderID skip_to = target_orders[v->unitnumber % target_orders.size()];
3252:     Command<Commands::SkipToOrder>::Do(flags, v->index, skip_to);
3253: }
```

**四个门禁与崩溃车 99 逐字对上**：

| 门禁 | 崩溃事实 | 成立 |
|---|---|---|
| `advance_order_on_clone` | crash log `:343` = `true` | ✅ |
| `v->IsInDepot()` / `IsDepotTile(v->tile)` | `trk: 0x80` = `TRACK_BIT_DEPOT`，tile 1889x188 | ✅ |
| 车辆类型匹配库类型 | type 10 = Railway，库也是铁路库 | ✅ |
| `target_orders` 非空 | 车有去本库的 GOTO_DEPOT 订单才会进列表 | 待验证 |

调用点：`:3377`（`CO_SHARE` 分支末尾）与 `:3476`（`CO_COPY` 分支末尾）。`CmdCloneVehicle` 内部会转 `CmdCloneOrder`，故克隆车必过这里。

⇒ **`skip_to` 是"克隆品订单表里的下标"，由 `SkipToOrder` 写进 `cur_real_order_index` / `cur_implicit_order_index`。这是全流程里唯一一处"按旧表下标算、按新表落笔"的地方。**

### 2.2 猜测根因 REG-297-D（**待验证**）：`order_count` 与落笔时的 `GetNumOrders()` 不是同一张表

`3245` 的 `idx + 1` 允许等于 `order_count` 吗？不允许（三元表达式兜到 0）。
但 `3251` 用的是**库里存下来的下标**，而 `3252` 的 `SkipToOrder` 是**独立命令**，它内部会重新读 `GetNumOrders()`。
只要两者之间表变短，越界值就留在车上。可能的三条变短路径（都无 clamp）：

1. **隐式订单被删**：`3300-3375` 一式共享走的是 `dst->Orders()`，而 `SkipToOrder` 会触发 `DeleteUnreachedImplicitOrders()`（`vehicle.cpp:3462-3498`，内部有 `>= GetNumOrders() ⇒ 0` 的自保护，但那是在它自己循环里）。
2. **R3R 段重建**：克隆品要过 `R3RSettleClonedChain` → `R3RSettleChainSegments`（第 225 轮 KI-332 新增），段行会重写/回收，若订单表随之重建，先前算好的 `skip_to` 就失配。
3. **借用层回灌**：`R3RSyncDrivingOrders`（`train_cmd.cpp:7529` / `:7559`）与 `R3RPushProgressToOwner`（`:7059-7060`）直接把 owner 的 `cur_implicit_order_index` 抄给对方，**只看 `GetEffectiveOrders()==owner_orders`，不校验 `GetNumOrders()`**。

### 2.3 为什么 12 天后才炸（不是克隆即刻崩）

崩溃在 `game date 1920-01-27`，克隆命令在 `1920-01-15 22:16`，间隔 `28870` state ticks。
⇒ 越界值被写上后**长期潜伏**：车停在库里（`vs:HD`），`ProcessOrders` 每 tick 都会跑，但 4909 断言只有在
执行到"取当前订单"那一步才触发——车库内的车会走 `ProcessOrders` 的早期分支（`4893-4906` 的 via/waypoint 判断
会 `IncrementImplicitOrderIndex()`），**一旦自增越过表尾就炸**。这与 §1.1 的嫌疑点（候选 A）完全一致。

⇒ 修法必须**同时**做两件事，缺一不可：
- **治标（必须，防崩）**：在 `ProcessOrders` 入口（`4908` 之前）对该索引做一次 clamp，命中时打
  `ORDIDX-CLAMP veh= old= num= real= order=` 而不是 `assert`。理由：模拟器崩溃的代价远大于"索引被拉回 0"，
  且正常路径本来就在范围内（clamp 不触发），只有 bug 路径被拦住并留下证据。
- **治本（定位后）**：`SkipToOrder` 的落笔、`R3RSyncDrivingOrders`、`R3RPushProgressToOwner`、
  `orders_backup_implicit_index` 还原（`train_cmd.cpp:7423` / `:7811`）四处统一走一个带 clamp 的 setter。

---

## 三、待取证清单（下一轮，最多 3 组）

| 编号 | 要取的东西 | 用途 | 状态 |
|---|---|---|---|
| E-1 | `CmdSkipToOrder` 的实现 | 确认它是否 clamp、写哪些字段 | 待做 |
| E-2 | `train_cmd.cpp:7423` / `:7811` 处 `orders_backup_implicit_index` 的来源与是否有界校验 | 备份还原是否越界 | 待做 |
| E-3 | `R3RSettleChainSegments` 是否重建订单表 | 段重建是否让旧下标失配 | 待做 |
| E-4 | 两份 `R3R_debug.log` 里 `veh 99` / `veh 21` 的行 | 崩溃车自身的 R3R 轨迹 | 待做 |
| E-5 | `CPL-HIT` / `UNIT-NEW` 计数 | 顺带收第 296 轮的 3 条判据 | 待做 |

---

## 四、结论

1. **崩溃车 = 克隆品**（`veh 99`，`CmdCloneVehicle 21 → 99`），停在本库格内（`trk 0x80`）。
2. **越界字段 = `cur_implicit_order_index`**，在 `ProcessOrders`（`order_cmd.cpp:4909`）被断言拦下，
   由 `TrainLocoHandler`（`train_cmd.cpp:21506`）驱动，服务端进程崩（`Network: server`）。
3. **唯一已知制造者 = `CheckAdvanceVehicleOrdersAfterClone`**（`order_cmd.cpp:3226-3253`），
   四道门禁与崩溃车事实全部吻合（`advance_order_on_clone=true` + 在库 + 铁路库）。
4. **潜伏期 12 天**说明是"写下后长期不动、直到某次自增越过表尾"，不是克隆当场的立即越界。
5. 尚未确定**具体哪一行写入**了越界值（§2.2 三条候选），需 E-1~E-3。

## 五、复测判据（第 296 轮 6 条的实测结果）

**待填**（崩溃抢先，本轮优先处理崩溃）。

---

## 六、玩家口径拍板（2026-10-25，本轮决定）

玩家原话：

> 「没事，你可以加探针，然后我建议先别防护，只加探针。反正目前我只是单人测试」

⇒ **本轮只加只读探针，不加 clamp 防护**。理由（玩家）：当前是单人测试，崩溃可接受，先要"谁写了越界值"的实证，
不要用 clamp 把 bug 掩盖掉。

⇒ 由此定案，本轮范围：

| 做 | 不做 |
|---|---|
| 在 `cur_implicit_order_index` 的各个写入点加带 tag 的探针 | **不加** `ProcessOrders` 入口的 clamp |
| 越界时额外打一条醒目标记行（便于 `findstr` 直接捞） | **不改** `assert` 行为（该崩还是崩） |
| 打印写入前后值 + `GetNumOrders()` + `cur_real_order_index` | **不动** `CheckAdvanceVehicleOrdersAfterClone` 的落笔逻辑 |
| 覆盖 §2.2 的 A/B/C 三类写入路径 | 不做治本 setter 重构 |

判据：复现同一场景后，越界那一刻**最后一条** `ORDIDX-*` 行即为元凶（tag 直接指出调用者）。

## 七、下一轮（等玩家复现后）要回答的问题

1. 越界值是**哪一次写入**留下的（tag）？
2. 写的那一刻 `GetNumOrders()` 是多少、`cur_real_order_index` 是多少？
3. 从写入到崩溃之间订单表**减少了多少条**（是否与 `DeleteUnreachedImplicitOrders` 或段重建同步）？
4. 若是 R3R 借用层（`R3RSyncDrivingOrders` / `R3RPushProgressToOwner`）回灌，则确认 KI-04 的"冻结值回灌"是否就是本崩溃的直系原因。

---

## 八、本轮落地：探针清单（2026-10-25，只读，零行为变更）

**关键排除（先记）**：`CmdSkipToOrder` 内部就有 `sel_ord >= v->GetNumOrders() ⇒ CMD_ERROR` 的校验（**`order_cmd.cpp:2004`**），
而且命令两侧本来就带 `cmd-skip-in` / `cmd-skip-out` 探针。⇒ **`CheckAdvanceVehicleOrdersAfterClone` 的 `skip_to`
不可能直接把越界值写进字段**（越界会被命令层拒掉）。越界值只能来自"写的时候合法、之后表变短"，
或者"绕开命令层直接赋值"的地方。这直接缩小了嫌疑范围。

### 8.1 五处新增探针

| # | 文件:位置 | 标签 | 作用 |
|---|---|---|---|
| 1 | `train_cmd.cpp` `R3RWatchOrderIndex()` 内（`ORD-IDX-WATCH` 打印之前） | `ORDIDX-OOB site=… tag=… veh= impl= real= tt= num= eff= front= spd= parked= indepot= ticket=KI-472` | **越界显式标记**。用与断言同一把尺子（`GetNumOrders()`，含隐式订单），越界即额外打一行，`findstr ORDIDX-OOB` 直接捞 |
| 2 | `order_cmd.cpp:4909` 断言之前 | `process-orders-bad`（走 #1 的 WATCH 桶） | 断言前的现场。该车第一次越界必留一行（该桶无历史快照 ⇒ 首行必打） |
| 3 | `vehicle.cpp` `DeleteUnreachedImplicitOrders()` 入口 | `del-implicit-in` | **表变短的头号嫌疑**。唯一成批删订单的地方（`DeleteOrder` 让后续索引整体前移） |
| 4 | 同上，两个出口（`suppress` 分支的 `return` 前 / 函数末尾） | `del-implicit-out` tag=`suppress`\|`deleted` | 与 #3 成对：入口 `impl` == 出口 `impl` 而 `n=` 变小 ⇒ 就是这里把车留在越界位 |
| 5 | `train_cmd.cpp` 两条备份还原路径 | `backup-restore-in/out`（`R3RSyncDrivingOrders` 归还借用排程）、`backup-pickup-in/out`（车库/解耦取回自己的排程） | 全流程**唯一"直接赋值、无任何界校验"**的隐式索引写入点（`orders_backup_implicit_index` → `cur_implicit_order_index`） |

### 8.2 判读顺序（复现崩溃后照这个走）

1. `findstr ORDIDX-OOB` → 取**第一条**，记下 `veh=`、`impl=`、`num=`（这就是坏值现场）。
2. 按该 `veh=` 往上翻 `ORD-IDX-WATCH`，找到**`n=` 变小而同一次 `impl=` 没有跟着减小**的那一行。
3. 那一行的 `site=` 就是元凶标签：
   - `del-implicit-in/out` ⇒ 隐式订单被删（删了但索引没回卷）
   - `backup-restore-*` / `backup-pickup-*` ⇒ 备份索引照着变短的当前表还原
   - `syncdriving-*` / `ord-push` ⇒ 借用层回灌了冻结的旧位置（KI-04 家族）
   - `settle-in/out` ⇒ 段重建过程里表被换
   - `cmd-skip-*` ⇒ 跳过命令（注意 #2004 已排除越界写入，若命中说明是"写时合法"）
4. 若 `ORDIDX-OOB` 一条都没有但仍崩 ⇒ 说明越界发生在**断言那一次读之前的一瞬间**（同 tick 内写入，中间没有任何探针点），
   下一轮就在 `ProcessOrders` 入口（`4908` 之前）补一条无条件快照。

### 8.3 构建自证

- 改动文件：`src\order_cmd.cpp`、`src\vehicle.cpp`、`src\train_cmd.cpp`（**全部 .cpp，未碰任何 `src\*.h`** ⇒ 增量合规，未触发 KI-183 护栏）
- 入口：复用 `_tmp_inc_build.cmd`（未新建任何 `.cmd`）
- `build\R3R_incbuild.done` → `EXIT_CODE=0`（2026-10-25 11:38:14）
- 日志：`[5/5] Linking CXX executable openttd.exe`，无 `error C*` / `fatal error` / `FAILED:` / `build stopped`
- `build\openttd.exe` → 2026-10-25 **11:37:59**，**52 326 400 B**（晚于三个源码文件的改动时刻）
- `read_lints` 三文件 **0 条**

### 8.4 本轮刻意不做

- **不加 clamp 防护**（玩家口径：单人测试，先要实证）
- 不改 `assert` 行为（该崩还是崩）
- 不动 `CheckAdvanceVehicleOrdersAfterClone` 的落笔逻辑
- 不做统一 setter 重构
- 不碰头文件（避免全量重编）

---

## 九、第 298 轮：复现成功，元凶锁定（2026-10-25 11:57 崩溃）

### 9.1 输入与判定

- 崩溃日志：`C:\Users\冯洁敏\Documents\OpenTTD\crash-20261025T035722Z.log`（24 638 B，11:57:36 写完）
- 服务端日志：`build\srv\R3R_debug.log`（1 019 041 B，11:57:37）⇒ 崩溃进程是 **srv**，采样有效
- **断言行号 4928 = 原 4909**：本轮在 `assert` 之前插了 19 行注释/探针，行号整体后移。玩家观察到"断言点变了"是行号位移，**不是新问题**。断言原文：`v->cur_implicit_order_index == 0 || v->cur_implicit_order_index < v->GetNumOrders()`。
- 崩溃车与上一轮**完全相同**：`veh 99`（`Train 7 (JRW_关西机场快速 03)`，`st:FE`，`vs:HD`，`trk: 0x80`=DEPOT，tile 1889×188，Railway），`1920-01-27`、`28870 state ticks` 与上一次逐字一致 ⇒ **该崩溃 100% 可复现**。
- `Build date: Oct 25 2026 00:07:46` **不能**用来判断 exe 版本（CMake 缓存里 `_VERSION_DATE` 是旧的）。以 exe 字面量为准：`build\srv\openttd.exe` 命中 `ORDIDX-OOB site=` / `process-orders-bad` / `del-implicit-in` ⇒ **跑的就是本轮探针版**（问题：`$(VERSION_DATE)` 之类只在 configure 时抓）。

### 9.2 铁证：10 条 `ORDIDX-OOB`，全是 veh=99

`findstr ORDIDX-OOB build\srv\R3R_debug.log` 共 10 条（`srv\R3R_debug.log:11310` 起），读数**每一行都相同**：

```
impl=16 real=16 tt=16 num=9 eff=9 front=1 spd=0 parked=0 indepot=1 ticket=KI-472
```

即 **索引 16，订单表只有 9 条**，超界 7 条。10 个站点依次为：
`syncdriving-out-owner`（首个）→ `settle-in/out tag=decouple-v` → `depotstay-in` → `loco-entry` → `skipgate-in` → `post-skipgate` → `post-depotblk` → `pre-processorders` → `process-orders-bad tag=assert-4909`（末个）。

**关键：越界值在这条链进入 `R3RSyncDrivingOrders()` 之后就已经存在**（第一个采样点是 `syncdriving-out-owner`，即函数**出口**），而入口采样 `syncdriving-in` 时表还是 17 条：

```
11289 ORD-IDX-WATCH site=del-implicit-in     veh=99 real=14->15 impl=14->15 n=17 ord=E0F0
11290 ORD-IDX-WATCH site=del-implicit-out tag=deleted  veh=99 n=17 ord=E0F0   （表未变短）
11293 ORD-IDX-WATCH site=loco-entry          veh=99 real=15->16 impl=15->16 n=17 ord=E0F0
11302 DEPOT-ARR veh=99 spd=0 real=16(15) curType=15 ... impl=16
11309 ORD-IDX-WATCH site=syncdriving-in tag=keep  veh=99 real=5->16 impl=5->16 n=17 ord=E0F0
11310 ORDIDX-OOB     site=syncdriving-out-owner   veh=99 impl=16 real=16 num=9 eff=9
11311 ORD-IDX-WATCH site=syncdriving-out-owner veh=99 real=5->16 impl=5->16 n=9  ord=E0F0->EC30
11315 ORD-IDX-WATCH site=settle-in tag=decouple-v  veh=99 real=12->16 n=17->9 ord=E0F0->EC30
11438 ORDIDX-OOB     site=process-orders-bad tag=assert-4909  → 断言
```

（`n=` 是节流桶里的"上次值→本次值"，读的时候要按 `ord=` 指针换没换来对齐。）

### 9.3 元凶：`R3RReturnBorrowedOrders()` 的 `was_follower` 分支"索引保持原地"

调用链：`R3RSyncDrivingOrders(chain=99, inherit_progress=false)`（`train_cmd.cpp:7488`）
→ `owner = R3RGetPriorityHead(chain)` 且 **`owner == chain`**（99 自己就是命令所有者）
→ `train_cmd.cpp:7498-7502` 分支调 `R3RReturnBorrowedOrders(chain, false)`
→ 该函数 `train_cmd.cpp:7432-7438`（`was_follower == true`，修法 D 的跟随指针机制）：

```cpp
if (was_follower) {
    /* 修法 D: 自己的表从未离开 orders，归还只是把跟随指针摘掉；索引保持原地
     * （它就是本车自己那份表上的位置，借用期由 R3RSyncDrivingOrders/
     * R3RPushProgressToOwner 负责冻结与回灌）。 */
    chain->r3r_follow_orders = nullptr;
    chain->r3r_follow_owner = VehicleID::Invalid();
}
```

**`ord=E0F0 -> EC30` 就是这一行造成的**：有效表从「跟随的别人那张 17 条表」切回「自己那张 9 条表」。
**而索引被原样保留 = 16，它是在被借那张 17 条表的坐标系里一路推进出来的**（14→15→16，见 9.2）。切表之后 16 ≥ 9 ⇒ 下一次 `ProcessOrders` 直接撞断言。

对"索引本应冻结在原地"的注释是**被日志证伪的**：借用期间链头一直在被 tick，
`ProcessOrders`/`IncrementImplicitOrderIndex`（`order_cmd.cpp` 里那几处 `v->IncrementImplicitOrderIndex()`）推进的是**活字段**，
两次 `R3RSyncDrivingOrders` 之间没人拦。`inherit_progress=false`（日志 `tag=keep`）只表示"不采纳 owner 的进度"，不等于"不推进自己的"。

> 一句话：**归还借来的排程时切回了自己那张更短的表，却没把索引换算/取回自己的位置。**
> 与 KI-04「冻结值回灌」同族，但方向相反——这次不是回灌旧值，而是**把一个属于别人的坐标留在了自己身上**。

### 9.4 为什么是"挂了 12 天才崩"

`veh 99` 停库（`spd=0`、`track=0x80`、`vs:HD` 头显），但 `ProcessOrders` 每 tick 照跑；
索引在借用期以"完成一条隐式订单就 +1"的速度慢慢爬（14→15→16），
一旦 `R3RSyncDrivingOrders` 被触发（本次是解挂提交点 `settle-* tag=decouple-v` 前后）切表，立刻越界。
⇒ 潜伏期 = 索引爬过自己表长的时间，与"每天都挂机"完全吻合。

### 9.5 修法候选（**未落地，等玩家拍板**）

| 方案 | 做法 | 评价 |
|---|---|---|
| **甲（治本，推荐）** | `R3RReturnBorrowedOrders()` 的 `was_follower` 分支不再"保持原地"，改为**取回自己的位置**：若 `orders_backup_real_index`/`_implicit_index` 有效（≠ INVALID）就用它，再对 `GetNumOrders()` 取模/clamp；无效则落 0。 | 语义正确（回到自己表上的原位置），且用的是**已有但一直没被读**的两个备份字段 |
| 乙（防护） | 归还后（以及在 `ProcessOrders` 入口）clamp `impl/real/tt` 到 `GetNumOrders()` | 玩家明确"先别防护"，只作兜底；单独用会把 `real` 也悄悄改成随机位置 |
| 丙（最小） | `was_follower` 分支若 `cur_implicit_order_index >= chain->GetNumOrders()` 就置 0 | 改动最小，但等于"忘记进度"，对长途多站计划会重跑 |

**共同前提**：修完必须同时保证 `cur_real_order_index` 与 `cur_implicit_order_index` 一致（`UpdateRealOrderIndex()` 会用 impl 推 real），否则换个断言换个坑。

### 9.6 仍未确认（U 项）

- **U-1**：`del-implicit-in/out` 两条的 `n` 都是 17、`ord` 未变 ⇒ 本次 `DeleteUnreachedImplicitOrders()` **没有删任何东西**，可以排除「表被删短」这一路。但 9.2 里 `11315 settle-in` 显示 `n=17->9`，需确认 9 条表的来源是"备份表"还是"重建表"。
- **U-2**：`11309 syncdriving-in tag=keep` 的 `real=5->16`，桶里上次值是 5 ⇒ 说明这条链**更早**还有一次索引 5 的状态，`5 → 16` 的跳变发生在两次采样之间（可能在 `11289` 之前的 `syncdriving` 调用里被 owner 回灌）。需要更早的 veh=99 序列才能定位"5 是怎么变成 16 的"。
- **U-3**：为什么 `backup-restore-in/out` 探针一条都没有 —— 说明走的是 `was_follower` 而不是 `orders_backup` 分支，与 9.3 判定一致（可用作交叉验证）。
- **U-4**：`SPDD-LOST veh=99 prev=30 now=0 ... prog=9`（`:11291`）与 `ORD-IDX-WATCH site=spd...`（`11292`）显示车速在 del-implicit 之后被清零，与"库内停驻"一致，不单独立案。

### 9.7 玩家同时报告的行为错误（另立，待取证）

> 玩家原话：「我注意到场景中一个明显的行为错误：那个路点的预留又出了问题，路点没能截断预留」

- 归属：KI-207c / KI-345 / KI-346 家族（整格位预留 + 路点），**与 9.3 的断言无关，不合并处理**。
- 已知现场锚点（本轮 srv 日志）：`RESV-WATCH` 117 条、`RESV-GHOST` 12 条；
  `srv\R3R_debug.log:10848-10849` `RESV-REL who=CLEAR-STN veh=99 tile=1844,188/1843,188 track=whole owner=99 age=28 phase=controller`
  `:11246 RESV-REL who=CLEAR-STN veh=99 tile=1888,188 track=whole`、`:11276 RESV-REL who=CLEAR-DEPOT veh=99 tile=1889,188 track=whole`
- **已取到的锚点**（同一份 srv 日志）：
  - `:4046`/`:4129` `RESV-GC veh=99 tile=254,233 track=stn kind=WAYPOINT why=free-path-hold`
  - `:5828`/`:6669` `RESV-GC veh=51 tile=254,234 track=stn kind=WAYPOINT why=free-path-hold`
  - `:7419`/`:7498`/`:7594` `RESV-GC veh=21 tile=254,233 track=stn kind=WAYPOINT why=free-path-hold`
  - `:2116`/`:2391`/`:2621`/`:11217` `AUTO-ORD-DROP kind=waypoint dest=15 seg=1/3/5 head=99/51/21 reason=not-in-source-range leg=[1,5)`（最后一行为 `leg=[12,16)`）
  - 计数：`SET-STN` 3 条 vs `CLEAR-STN` 504 条；`kind=WAYPOINT` 命中 8 条**全部 `why=free-path-hold`**
  ⇒ 路点格不是"没订上"，而是**订上之后从未被当作"到达即截断"的终点释放**，一直挂在 `free-path-hold` 直到 GC。
- **两个待验方向**：
  - A：`AUTO-ORD-DROP kind=waypoint dest=15 reason=not-in-source-range` ⇒ «去路点»这条自动记录订单在采样/白名单阶段被丢弃 ⇒ 车上没有"去路点"命令 ⇒ 预留不以路点为终点、越过路点继续铺。
  - B：路点整格位（`SetRailStationReservation` 的 m6 bit2）在 PBS 释放时不被 `SetRailStationPlatformReservation` 覆盖（后者以 `IsRailStationTile()` 为守卫，路点不是站台）⇒ 只能靠 R3R 自己的释放分支或 GC ⇒ 表现即"订了不截断，攒着等 GC"。
- **下一步取数（未执行）**：①问清形态属 (a) 预留越过路点延伸 / (b) 路点格残留黑线 / (c) 两者；②取 `RP cur=` 序列看末格是否等于路点格；③查 `dest=15` 对应的路点坐标。
- 状态：**已登记为 KI-473，未取证完成、未修**（本轮先收断言，不合并处理）。

---

## 十、第 299 轮：修法甲落地（2026-10-25）

玩家原话：**「关于崩溃那条，按照修法甲进行修复」**。

### 10.1 改动（仅 `src\train_cmd.cpp`，未碰 `src\*.h`）

**① 借用开始处记住"自己的位置"** —— `R3RSyncDrivingOrders()` 切表块，
在 `chain->r3r_follow_orders = owner_orders;` **之前**插入：

```cpp
if (chain->orders_backup == nullptr && chain->orders_backup_real_index == INVALID_VEH_ORDER_ID) {
    chain->orders_backup_real_index     = chain->cur_real_order_index;
    chain->orders_backup_implicit_index = chain->cur_implicit_order_index;
    R3RDbgWrite("ORD-BORROW-PARK veh=%d bkr=%d bki=%d num=%u\n", ...);
}
```

- 修法 D 下 `orders_backup` 恒为空（自己的表留在 `orders` 里，KI-465），
  所以这两个备份索引是"自己表上的位置"**唯一的落脚处**；
- `only-if-INVALID`：保留借用开始那一刻的值。借用期推进的是 owner 那张表
  （`cur_*_order_index` 已成为别人的坐标），自己表上的位置不会变，多次 sync 不该覆盖；
- 旧机制（`orders_backup != nullptr`）已经在备份表时写过位置，**不覆盖**。

**② 归还时取回自己的位置** —— `R3RReturnBorrowedOrders()` 的 `was_follower` 分支
（原来是"索引保持原地"，即 KI-472 元凶）：

```cpp
if (chain->orders == nullptr && chain->orders_backup != nullptr) {  // 旧档畸形态
    chain->orders = chain->orders_backup; chain->orders_backup = nullptr;
}
OrderList *const own = R3RSegmentOwnOrderList(chain);
const uint num = (own != nullptr) ? own->GetNumOrders() : 0;
VehicleOrderID own_real = chain->orders_backup_real_index;
VehicleOrderID own_impl = chain->orders_backup_implicit_index;
if (num == 0 || own_real == INVALID_VEH_ORDER_ID || own_real >= num) own_real = 0;
if (num == 0 || own_impl == INVALID_VEH_ORDER_ID || own_impl >= num) own_impl = 0;
R3RDbgWrite("ORD-RETURN-OWN veh=%d num=%u real=%d->%d impl=%d->%d bk=%d/%d orphan=%d\n", ...);
chain->cur_real_order_index     = own_real;
chain->cur_implicit_order_index = own_impl;
chain->cur_timetable_order_index = own_real;
chain->r3r_follow_orders = nullptr;
chain->r3r_follow_owner  = VehicleID::Invalid();
```

- 备份缺失或已越界（表被删短/重建）⇒ 落 0。**宁可从头跑，也不能留越界索引**；
- `cur_timetable_order_index` 一并跟 `real`，避免 `UpdateVehicleTimetable` 另一处断言；
- 「旧档畸形态」那三行是顺手补的：`was_follower` 且 `orders == nullptr` 时，
  末尾会把 `orders_backup` 无条件清空 ⇒ 等于把这张表扔掉；先搬回 `orders` 再取位置。

### 10.2 为什么这样就对了（与 §9.2 的读数对齐）

`srv\R3R_debug.log:11309` 是 `syncdriving-in tag=keep veh=99 real=5->16 impl=5->16 n=17` ——
**桶里上次值是 5**，正说明归还前"自己的位置"是 5；16 只是借来那张 17 条表的坐标。
修法甲把 5 写回后，切表瞬间是 `5 < 9`，`ProcessOrders` 不再越界。
反过来，旧代码保留 16 ⇒ `16 >= 9` ⇒ 撞 `order_cmd.cpp` 的断言（4928 行）。

### 10.3 为什么不是乙/丙

| 方案 | 为什么没选 |
|---|---|
| 乙 clamp | 玩家明确"先别防护"；且单独用会把 `real` 悄悄改成任意位置（信息丢失且静默） |
| 丙 越界置 0 | 等于忘记进度：长途多站计划（现场 9 条）会从头重跑，运营行为大变 |

甲是唯一"还原真实位置 + 缺数据时安全退化"的方案。

### 10.4 复测判据

1. 同一场景（读档后连克隆 4 列车、`veh 99` 停库挂机 12 天）不再崩，`ORDIDX-OOB` **0 条**；
2. 出现 `ORD-RETURN-OWN veh=99 num=9 real=16->5 impl=16->5 bk=5/5`（值随机，关键是写回值 `< num`）；
3. 借用开始出现成对的 `ORD-BORROW-PARK veh=99 bkr=5 bki=5 num=9`；
4. 负判据：正常「挂车 → 解挂」仍接着原进度跑，不出现无故跳回第 0 条；
5. 负判据：借用期 `cur_implicit_order_index` 仍随 owner 的表推进（`ORD-PUSH` 行为不变）。

### 10.5 构建自证

复用既有 `_tmp_inc_build.cmd`（未新建任何 `.cmd`）：

| 项 | 读数 |
|---|---|
| 护栏 | `GUARD: incremental is safe (no header/lang file is newer than the newest object)` |
| 编译 | `[2/3] Building CXX object ...\src\train_cmd.cpp.obj` |
| 链接 | `[3/3] Linking CXX executable openttd.exe`（1 次） |
| 判决 | `build\R3R_incbuild.done` = `EXIT_CODE=0` |
| 时间戳 | `src\train_cmd.cpp` 12:08:39 → `train_cmd.cpp.obj` 12:09:38 → `build\openttd.exe` 12:10:50（52 326 400 B） |
| 字面量 | exe 命中 `ORD-RETURN-OWN veh=` / `ORD-BORROW-PARK veh=` |
| lint | `src\train_cmd.cpp` 0 条 |
| 分发 | 已复制到玩家实际运行的 `build\srv\openttd.exe`（12:10:50，52 326 400 B） |

### 10.6 状态

- 已实现 + 已编译，**游戏内复测待做**（判据见 10.4）；
- KI-473（路点不截断）**未动**，仍待取证；
