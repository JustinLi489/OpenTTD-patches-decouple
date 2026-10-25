# 第 295 轮：联机测试三条报告（挂接慢 / 一对完全不挂 / 克隆出 5、6 号）

> 状态：**仅登记，未取证、未改码、未构建**（按"先落盘再取证"铁律）。
> 记录时间：2026-10-24。产出于上一轮（第 294 轮）的修法 D 内测 exe（`build\openttd.exe` 23:28:54，52 320 768 B）。

---

## 一、玩家原话（2026-10-24）

> 「我开启了一个本地联机测试，里面有两组每组三列共享调度列车，现在发现一些问题，第一是列车挂接时间有点太慢了。第二是有一对列车完全不挂接。第三是读档之后，复制1，2号列车（当时全图仅两列车），复制出来的是5，6号列车」

场景要素（据此设判据）：
- **本地联机**（`localhost` 双客户端，非专用服务器）：命令会走「客户端预测 → 服务器执行 → 客户端确认/回滚」。
- **两组，每组三列**，且这三列**共享调度**（同一张 `OrderList` / 同一套排程在跑）。
- 第 3 条的时间点：**读档之后**，且**当时全图仅 1、2 号两列车**（⇒ 号池里 3、4 号应当空着）。

---

## 二、三条问题拆分与初步定性

| 编号 | 现象 | 一句话定性 | 严重度（暂定） |
| --- | --- | --- | --- |
| P1 | 挂接（couple）耗时太长 | "等待命中 → 提交"的节拍问题，可能叠加**联机命令往返 RTT** 与既有重试节流 | 中 |
| P2 | 有一对列车**完全不挂接** | 结构性拒挂（闸门/几何/订单侧），不是慢；必须定位是哪一道门 | 高 |
| P3 | 读档后克隆 1、2 号 → 得到 **5、6 号** | `unitnumber` 分配器被"多消耗了 2 个号"，或联机预测/回滚把号吃掉了 | 中（体验/不可回退：号池长期漂移） |

与既有 KI 的关系：
- P1/P2 属**耦合路径**（KI-465 修法 D 刚改过 `Couple()`/`DecoupleTrain()` 的 `orders` 交接方式，**必须优先排除本轮改动引入**）。
- P3 属**克隆路径**（KI-332 第 225 轮改过 `CloneVehicle` 的段身份复制，但**没有**改号分配；KI-237/`R3RRestoreUnitNumber` 第 148 轮改过号冻结/取回）。

---

## 三、疑点（一发现就登记，允许先无结论）

### 疑点-1（P3）：为什么跳过 3、4 号
- **期望**：全图只有 1、2 号车 ⇒ 号池 `GetFreeUnitNumber()` 从 1 起找第一个未被占用的号 ⇒ 两条克隆链应拿 **3、4**。
- **实测**：拿到 **5、6**（玩家口述）。
- **为什么存疑**：恰好跳过 **2 个**号 = 恰好等于"克隆了两列车"的数量，说明**每个克隆动作额外吃掉一个号**，而不是随机漂移。两种可能必须用日志分开：
  - (a) **联机预测/回滚**：客户端先本地执行一次（吃 3、4），服务器端重新执行（再吃 5、6），客户端那次被静默丢弃 → 号池永久少 2 个；
  - (b) **克隆内部多次分配**：`CloneVehicle` 在构建过程中对同一单元分配了两次号（或先给临时单元、后给最终单元），前一次没有归还号池。
- **需要的数据**：两端 `R3R_debug.log` 在这两次克隆前后的 `unitnumber` 相关探针行；以及 `command` 日志里是否出现**两次** `CmdCloneVehicle`（预测 + 服务器）。
- **派生疑点-1b**：号池一旦漂移是**不可逆**的（`unitnumber` 是存档字段），若确认是 (a)，则"被吃掉的号"永远不会回来 —— 需要决定是否要"克隆提交后回填最小可用号"这类补偿，或干脆接受漂移（玩家拍板）。

### 疑点-2（P2）：完全不挂接 ≠ 慢
- **判据分离方式**：慢是"有 `CPL-PAIR/CPL-HIT` 但拖着不提交"；完全不挂是"**从不出现** `CPL-HIT` 或出现 `COUPLE-REFUSE-*` / `SPLICE-GAP-REJECT` / `CPL-GATE target-not-wait`"。
- **已知会"永久不挂"的结构性路径**（本轮必须先逐条排除）：
  1. `R3RChainHasRealArticPart` 命中 ⇒ `FOLDCHK-REFUSE-REAL-ARTIC` 每 tick 拒绝（第 170 轮引入）——**真铰接**车底永远挂不上；
  2. 目标不是 `OT_WAIT_COUPLE` ⇒ `CPL-GATE target-not-wait`；
  3. `R3RSkipCoupleOrdersForRealArtic` 把 `WAIT_COUPLE` 也跳过（KI-274，**未修**）；
  4. `TryReserveRailTrack` 预留失败 / `TPR-BLOCKED` 同格误判（KI-330，**未修**）；
  5. 挂接目标在**另一组**的共享链上 ⇒ `R3RCoupleAllowed` 跨组授权闸门。
- **为什么存疑**：玩家说是"一组三列"里的一对，同组同公司，跨公司闸门理应不适用 ⇒ 更可能是 ①/④。

### 疑点-3（P1）：慢是"节拍"还是"RTT"
- 既有已知节流：`CPL-SKIP retryIn=8`（重试间隔）、耦合要求 `cur_speed == 0`（必须停稳）、`TrainCoupleHandler` 每次只尝试一次几何命中。
- **本地联机**下每次命令往返 + 双端各跑一遍 handler，可能把"约 1 秒"放大成"数秒"。
- **需要的数据**：一次成功挂接的完整时间轴：`CPL-PAIR`（首次配对）→ `CPL-HIT`（几何命中）→ `COUPLE-FLIP-*`（若有）→ `COUPLE-OK`，四者各自的日志行号/时刻 ⇒ 才能判断是哪一段在耗。

### 疑点-4（P1/P2 共同）：是否本轮修法 D 引入
- 第 294 轮把 `Couple()`/`DecoupleTrain()` 的 `orders` 交换改成"只设清跟随指针"，`GetEffectiveOrders()` 被 `GetNumOrders()/GetOrder()` 等访问器接管。
- **风险点**：若某处判断"能不能挂"读的是 `v->orders`（本体，可能为 `nullptr`）而实际排程在 `GetEffectiveOrders()` 里，就会**恒判为"无排程"** ⇒ 表现为"完全不挂"。
- **必须先做只读核对**：`R3RIsCoupleTarget()` / `TrainCoupleHandler()` / `CPL-GATE` 三处读的是本体还是 effective。

---

## 四、猜测根因（待验证，允许先错）

- **G-1（P3，优先验）**：联机命令预测/回滚导致 `CmdCloneVehicle` 被执行两次，`GetFreeUnitNumber()` 两次都成功但只有第二次结果留在世界 ⇒ 号池跳过 2 个（= 两条链）。
- **G-2（P2）**：三列共享调度里的某一对，其中一列带**真铰接**（artic）车底，被 `FOLDCHK-REFUSE-REAL-ARTIC` 永久拒绝（第 170 轮为防止克隆/翻转破坏 artic 组而加的门），在"两张表共享调度"场景下永远等不到。
- **G-3（P1）**：慢的主因不是寻路，而是"必须停稳 + 每次失败重试间隔 8 tick + 联机双端各跑一遍"叠加；若命中后还要走 `COUPLE-FLIP-*` 四候选，则再乘一轮。
- **G-4（P1/P2）**：修法 D 把 `R3RIsCoupleTarget()` 之类的判据读到了空的 `orders` 本体（应为 `GetEffectiveOrders()`）⇒ 既可能"慢"（每 tick 重试）也可能"完全不挂"。

---

## 五、本轮取数需求（交给玩家 / 从既有日志取）

1. `build\R3R_debug.log`（产生者 exe 大小 + 时间必须与本轮 exe 一致，否则作废）。
2. P2 那对车的**车号**（`veh=A`/`veh=B`）与**所在组**（是"三列里哪两列"）。
3. P3 克隆前后的 `unitnumber` 快照（或直接给 `dump_vehicle` 输出）。
4. 若是联机：**两端**日志各一份（本地预测只在客户端留痕，服务器端才是一等公民）。
5. 复现时是否开着订单窗口（影响 KI-466 的自检门控）。

---

## 六、待填：修复后复测判据

（本轮未修，留空待补。）

---

## 七、只读取证（第 1 组：号池机制）

### 7.1 `GetFreeUnitNumber()` 是**纯查询**，不占号

`src\vehicle.cpp:3115-3131`：
```cpp
return c->freeunits[type].NextID();   // NextID() 只是找第一个未置位，见 :3068-3076
```
`NextID()` 的文档注释原文：「Find first unused unit number. **This does not mark the unit number as used.**」占号必须另调 `UseID()`（`:3083`）。

⇒ **推论**：任何"拿到了号却没 `UseID`"或"`UseID` 了但对象随后被丢弃且没 `ReleaseID`"的路径，都会让号池**永久跳过**该号（`unitnumber` 是存档字段，被吃的号不会回来）。

### 7.2 每列"列车"被克隆时可能取 **2 个**号 —— P3 的最强候选

`src\vehicle_cmd.cpp:174`（`CmdBuildVehicle`）：
```cpp
UnitID unit_num = (flags.Test(DoCommandFlag::QueryCost) || flags.Test(DoCommandFlag::AutoReplace)
                   || (type == VehicleType::Train && e->VehInfo<RailVehicleInfo>().railveh_type == RailVehicleType::Wagon))
                  ? 0 : GetFreeUnitNumber(type);
```
⇒ 只有**非车厢**（机车/假引擎/前端）才取号；车厢恒 0。

`src\vehicle_cmd.cpp:245-251`（Execute 成功后）：
```cpp
Company::Get(v->owner)->freeunits[v->type].UseID(v->unitnumber);
R3RDbgWrite("UNIT-NEW id=%u type=%u owner=%u free_next=%u\n", ...);
```
⇒ **每次成功购车都会留一行 `UNIT-NEW`**，含本次号与池子下次会发的号。**这是判 P3 的直接证据来源，不需要加任何代码。**

`src\vehicle_cmd.cpp:283-303`（`R3RCreateCarOnlyFormation`，R3R 自加）：
```cpp
} else {
    front->unitnumber = GetFreeUnitNumber(VehicleType::Train);
    if (front->unitnumber != UINT16_MAX) Company::Get(front->owner)->freeunits[VehicleType::Train].UseID(front->unitnumber);
}
```
⇒ **车底链升级成"纯车底编组"时会自己取一个号**。克隆一条"机车 + 车底"的列车时，克隆品同样会走这条路径 ⇒ **每列克隆可能消耗 ≥2 个号**，与"跳过 3、4、拿到 5、6"在数量级上吻合（两列 × 各多耗 2 个 = 正好 4 个）。

- **注**：本条不能单独定罪。`R3RCreateCarOnlyFormation` 若随后被 `R3RDestroyCarOnlyFormation` 打散，号会 `ReleaseID`（`vehicle_cmd.cpp:944/950`、`:979/983` 有归还逻辑），所以要看**实际执行序列**，不能按"存在这条路径"就下结论。

### 7.3 判 P3 的日志口径（无需改码）

| 观测 | 判定 |
| --- | --- |
| `UNIT-NEW` 恰好 **2 行**，`id=3` 然后 `id=4` | 正常，玩家看错或别处改名 |
| `UNIT-NEW` **4 行**（3、4、5、6） | 号被**分配了两次**（联机预测/回滚 或 克隆内部重复构建），3、4 对应的车被丢弃且未归还 |
| `UNIT-NEW` 2 行但 `id=5`、`id=6`，且 `free_next` 在克隆前已是 5 | 3、4 在**克隆之前**就已泄漏（与本轮动作无关，是历史遗留漂移） |
| 完全没有 `UNIT-NEW` | 走的不是 `CmdBuildVehicle`（可能是 `CmdCloneVehicleFromTemplate` 或模板路径），需另加探针 |

⇒ **第一条要玩家给的东西：克隆前后 `build\R3R_debug.log` 里的全部 `UNIT-NEW` 行（含 `free_next=`）与 `grep UNIT-` 结果。**

## 八、只读取证（第 2 组：耦合闸门是否被修法 D 影响）

### 8.1 `R3RIsCoupleTarget()`（`train_cmd.cpp:2371`）的四道硬条件

```cpp
if (t == nullptr || !t->IsSegmentFront()) return false;   // 必须带 ★
```
加上调用侧 `TrainCoupleHandler` 的两条（`:13333`、`:13338`）：
```cpp
if (t->First() != t || t->First() == coupler->First()) { R3RClearStaleWaitMarker(t); continue; } // 必须是独立链头
if (t->vehstatus.Test(VehState::Stopped)) continue;                                            // 不能被玩家按了"停止"
```
还有 `:13348` 的 `R3RCoupleTargetAtOrderStation(coupler, t)`（目标必须停在**订单指定的站/库**）。

⇒ **P2「完全不挂」的可判读分解**（日志里按顺序看哪一行缺失）：
| 日志现象 | 卡在哪 |
| --- | --- |
| 连 `CPL-PAIR` 都没有 | 候选列表为空 ⇒ ★/链头/停止/目的地四道之一（最可能是**目标没 ★**，即它根本不是"段"） |
| 有 `CPL-PAIR` 但**从不** `CPL-HIT` | 几何/寻路：`CPL-PATHFOUND found=0`、`TPR-BLOCKED`、`SPLICE-GAP-REJECT` |
| 有 `CPL-HIT` 但从不 `COUPLE-OK` | 提交被拒：`FOLDCHK-REFUSE-REAL-ARTIC` / `COUPLE-REFUSE-STILL-FOLDED` / `CheckTrainAttachment` |
| 有 `CPL-GATE reject=...` 每 tick 刷同一对车 | 被闸门永久拒，`reject=` 后面就是原因（`target-not-wait` / `pair-mismatch` / `group` …） |

- **重要**：`CPL-GATE` 是**边沿触发**（`R3REDGE_COUPLEGATE`，`:13233`），同一对车同一原因在 128 帧窗口内只打一次 ⇒ **"日志里没有 CPL-GATE" 不能反推"没被拒"**。这一条以前多次造成误判。

### 8.2 「慢」的两个已确认结构成本（`train_cmd.cpp:13310-13314` 注释自陈）

- 找不到候选时**不是每 tick 全池扫**，而是"过几 tick 再试"（退避）⇒ **第一次配对天然有延迟**；这是设计，不是 bug。
- 稳态判定（`:13302-13307`）要求配对伙伴 `R3RIsCoupleTarget` 为真、`!Stopped`、`R3RCoupleAllowedIgnoringPair` 为真，任一不满足就 `R3RUnpairCoupleTargets()` 重来 ⇒ **目标状态抖一下，配对就从 0 开始重新计时**。三列共享调度里目标被别的车反复带走/放回，就会一直处在"重新配对"状态 = 观感极慢。

## 九、疑点-5（本轮新发现，**优先级最高，必须玩家拍板前先查**）：修法 D 的 `r3r_follow_orders` 与**原生共享调度**是两套机制

- **玩家场景原话**：「两组每组三列**共享调度**列车」。
- `OpenTTD` 原生共享调度 = `OrderList::AddToShared()` 维护的 `first_shared/previous_shared/next_shared` 环，同环车辆的 `orders` **指向同一张表**。
- 修法 D（KI-465，上一轮）引入的 `r3r_follow_orders` 是**第二套借用**：车辆自己的 `orders` 保持不动，执行时读 `GetEffectiveOrders()`。
- **冲突点**：耦合后 v 变成 follower（`r3r_follow_orders` 指向 u 的表），但 v 的 `orders` 仍是它自己那张**共享表**，而它那两个共享伙伴还在按原表跑 ⇒ **同一个共享环上的三列车，一列跑 u 的表、两列跑原表**。
- 可能的三类后果（待日志验证）：
  1. 行为不一致 ⇒ 三列车的到站/等待逻辑各走各的，配对永远凑不齐（**P1 慢 / P2 不挂**）；
  2. 联机下双端算出不同结果 ⇒ desync（用户正在测联机，需重点核对）；
  3. 解挂时"归还"只摘跟随指针（`:7413-7419`），共享环原样 ⇒ 表没有丢，但索引可能已被 `R3RSyncDrivingOrders`（`:7556-7561`）改到 owner 的位置。
- **判据**：`R3RSyncDrivingOrders` 在 `chain->r3r_orders_borrowed` 为真时会打 `syncdriving-out-adopted`（`:7564` `R3RWatchOrderIndex`）。日志里若出现**同一列车号反复 `syncdriving-*` 且 `real=` 来回跳**，即坐实本条。
- **未做**：本轮**没有**评估"耦合时 v 正在共享环上"这个分支该不该禁止/该怎么处理（例如：v 是共享表成员时，是否应当把整环都改成 follower，或者拒绝耦合）。这需要玩家拍板口径。

## 十、本轮纪律

- 未改任何源码、未构建。
- 先落盘本备忘 + `R3R_KNOWN_ISSUES.md` 的 KI-467/468/469，之后才允许深挖代码（按"先落盘再取证"铁律，且取证配额内每完成一组即刻回写本文件）。
