# 第 389 轮：拿到 crash.sav 后的新证据 —— 读档红框「长度无效」+ 唯一列车「两段式 + 自动时刻表 + 自动分离」

日期：2026-11-03
关联：KI-555（第 388 轮）、`R3R_separation_order_null_crash_round388_memo.md`
状态：**取证中（本轮未改源码、未构建）**

---

## 一、玩家原话（2026-11-03）

> 「我拿到了 crash.sav，然后读取了它。一进去，它就跳红框："xx公司拥有的列车 DF4B-2001 长度无效。这个错误可能是 newgrf 导致的，游戏可能同步错误或崩溃"。同时，我注意到场景里仅有一个两段式列车，它被同时设置了自动时刻表和自动分离。你再分析一下？」

拆成三条事实：

- **F1**：现在**有存档** `crash.sav`，可以读档复现（不再受"只有崩溃日志"限制）。
- **F2**：读档**立刻**弹红框，文案是「长度无效 … 可能 newgrf 导致 … 可能同步错误或崩溃」，对象是 `DF4B-2001`。
- **F3**：场景里**只有一列车**，是**两段式**，且**同时开了自动时刻表 + 自动分离**。

---

## 二、F2 的代码定位（已实证）

文案唯一来源：`src\lang\english.txt:3716`

```
STR_BROKEN_VEHICLE_LENGTH :Train '{VEHICLE}' belonging to '{COMPANY}' has invalid length.
                           It is probably caused by problems with NewGRFs. Game may desync or crash
```

唯一发射点：`src\train_cmd.cpp:210-229`

```cpp
void CheckTrainsLengths()
{
    for (const Train *v : Train::IterateFrontOnly()) {
        if (!v->vehstatus.Test(VehState::Crashed) && !v->IsVirtual()) {
            for (const Train *u = v->GetMovingFront(), *w = v->GetMovingNext(); w != nullptr; u = w, w = w->GetMovingNext()) {
                if (u->track != TRACK_BIT_DEPOT) {
                    if ((w->track != TRACK_BIT_DEPOT &&
                            std::max(abs(u->x_pos - w->x_pos), abs(u->y_pos - w->y_pos)) != u->CalcNextVehicleOffset()) ||
                            (w->track == TRACK_BIT_DEPOT && TicksToLeaveDepot(u) <= 0)) {
                        ShowErrorMessage(GetEncodedString(STR_BROKEN_VEHICLE_LENGTH, v->index, v->owner), {}, WarningLevel::Critical);   // :221
                        ...
                        break;   // 每列车只报一次
```

调用点（**两处都在读档收尾**）：

- `src\saveload\afterload.cpp:325`（`AfterLoadGame` 末段，紧跟 `UpdateAirportsNoise()`、`ShowNewGRFError()` 之前）
- `src\saveload\afterload.cpp:4819`（另一收尾段）

⇒ **F2 的时机完全对上**：这不是运行中某 tick 的偶发，而是**读档收尾的固定校验**，所以"一进去就弹"。

### 判据含义（重要）

报错的充分必要条件是：沿**移动方向**（`GetMovingFront/GetMovingNext`，受 `VehicleFlag::DrivingBackwards` 影响）相邻两节车，实测像素切比雪夫距离 `max(|dx|,|dy|)` **不等于** `u->CalcNextVehicleOffset()`（=该车名义车身长度/铰接跨度）。

即 **存档里这列车的"物理几何 / 移动方向 / 名义长度"三者已经不自洽**，且是在**读档那一刻就已如此**（不是读档后被改坏）。理由：`CheckTrainsLengths` 是 `AfterLoadGame` 里的第一次检查，之前只有读档迁移逻辑（含 `R3RRebuildCouplePriorities` / `R3REnsureCoupleEndOrders`，见 `src\sl\vehicle_sl.cpp:556-563`）跑过。

---

## 三、F3 与崩溃点的对应关系（已实证）

崩溃栈（第 388 轮已符号化）：`TrainEnterStation → Vehicle::BeginLoading → UpdateVehicleTimetable → UpdateSeparationOrder → PopulateSeparationState:756`。

`src\timetable_cmd.cpp:1180-1185`：

```cpp
} else if (v->vehicle_flags.Test(VehicleFlag::TimetableSeparation) && v->vehicle_flags.Test(VehicleFlag::TimetableStarted)) {
    v->current_order_time  = time_taken;
    v->current_loading_time = time_loading;
    UpdateSeparationOrder(v);          // :1183 ← 进 PopulateSeparationState 的唯一入口
    v->current_order_time  = 0;
    v->current_loading_time = 0;
```

⇒ **F3 的"自动分离"就是这条崩溃路径的开关**：

- `VehicleFlag::TimetableSeparation`（`base_consist.h:50`）= 玩家界面的"时刻表自动分离"。
- `VehicleFlag::TimetableStarted`（同一枚举区）= 自动时刻表已经起跑（**存档里会保存**，所以读档后就是 true，不必等一整个周期）。
- `VehicleFlag::SeparationActive`（`base_consist.h:47`）在 `UpdateSeparationOrder()` 第一行就被 `Set`（`:787`），随后立刻 `PopulateSeparationState(v_start)`（`:789`）。

⇒ 结论：**只要这列车以 `TimetableSeparation=1 && TimetableStarted=1` 进站装载，就必然走进崩溃函数**。第 388 轮担心的"这条路径是否真的可达"被 F3 直接补齐——**不是巧合，是玩家亲手开着的功能**。

### "两段式"引入的额外风险

`PopulateSeparationState` 的遍历是**共享订单表的整条链**：

```cpp
if (v_start->GetNumOrders() == 0) return out;                                  // :736 只查 v_start 自己
for (const Vehicle *v = v_start->FirstShared(); v != nullptr; v = v->NextShared()) {  // :737
    if (!v->vehicle_flags.Test(VehicleFlag::SeparationActive)) continue;       // :738
    const int n = v->cur_real_order_index;                                     // :740 ← 每节车各用各的索引
    ...
    const Order *order = v->GetOrder(n);                                       // :755
    if (order->IsType(OT_CONDITIONAL)) continue;                               // :756 ← 崩
```

- `:736` 的早退只看 `v_start`，**不看链上其他节**；
- `:740` 用的是**每节车自己的** `cur_real_order_index`；
- `:755` 的 `GetOrder(n)` 用的是**共享表**的 `GetOrderAt`。

⇒ 只要链上**任意一节**（两段式里的第二段、或段内某节）的 `cur_real_order_index` 与表长不一致，就 nullptr。**两段结构把这个概率放大**：两个段各有自己的索引副本，同步点少、易分叉。

---

## 四、两个症状是否同源（当前判断）

| 症状 | 直接原因 | 是否同源 |
|---|---|---|
| 读档红框「长度无效」 | `train_cmd.cpp:219` 相邻节距离 ≠ `CalcNextVehicleOffset()` | **可能是同一根因的两个表现**（见 §五 假设 H-389-A），但**当前证据不足以断言** |
| 进站装载崩溃 | `timetable_cmd.cpp:756` 拿到 nullptr（索引 == 表长） | 由 §三 确认路径可达 |

两者都要求"这列车的 R3R 段结构 + 存档里的字段彼此不自洽"，且都发生在**同一列、全图唯一的列车**上 ⇒ 高度怀疑是**同一个损坏**的两处外显。

反面提醒（防止过早定案）：长度校验走 `GetMovingFront/GetMovingNext`（依赖 `DrivingBackwards`），崩溃走 `cur_real_order_index`（与 `DrivingBackwards` 无关）。**两条判据没有共用的数据结构**，所以"同源"目前只是概率判断，不是证明。

---

## 五、猜测根因（待验证，按优先级）

### H-389-A（**新，优先验**）：读档迁移 `R3REnsureCoupleEndOrders()` 改了表长但没搬索引

- 读档期 `src\sl\vehicle_sl.cpp:556-563` 会跑 `R3REnsureCoupleEndOrders` + `R3RRebuildCouplePriorities`。
- 若 `R3REnsureCoupleEndOrders()` 在**开口的 `OT_GOTO_COUPLE` 后面补 `OT_COUPLE_END`**（或删/合并条目），**表长变化**，而该车 `cur_real_order_index` 是从存档里读进来的旧值 —— 若它原本等于旧表长（或迁移把索引搬错），读档后立刻 `n == GetNumOrders()`。
- 这条能解释"**完全不需要任何运行期 R3R 动作、一读档就埋好炸弹**"，与玩家早前"全图没执行过任何 R3R 行为"的说法不冲突。
- 待核：`R3REnsureCoupleEndOrders()` 的实现（`src\order_cmd.cpp` ~2116 附近）是否 `InsertOrder` 时同步 `cur_real_order_index / cur_implicit_order_index / cur_timetable_order_index`；以及读档路径的 `cur_real_order_index` 取值是否已过 `UpdateRealOrderIndex()`。

### H-389-B（沿用 KI-555 的 REG-388-E）：索引锁 `vehicle_base.h:1158` 绕过唯一钳制 `:1161`

- 若玩家在**上一次会话**里已经把索引顶到 `num`（删表尾的 `OT_GOTO_COUPLE`，撞锁不钳制），那个越界值**被存进存档** ⇒ 这次读档一进站就崩。
- 与 H-389-A 的区别：A 是"读档迁移现造"，B 是"存档带着旧伤"。两者都需要 `n == GetNumOrders()` 这一条事实。
- 与第 388 轮的 Command Log 旁证一致（`CmdDeleteOrder 2, 2` 紧跟 `CmdInsertOrder 2, 2, t:1F`=GOTO_COUPLE，`t:1F` 编码已核：`order_base.h:280-285` 的 `base=15 + sub<<4`，`0x1F` ⇒ 16 = `OT_GOTO_COUPLE`）。

### H-389-C：两段链的 `DrivingBackwards` 不一致导致"长度无效"

- `GetMovingFront/GetMovingNext` 按 `VehicleFlag::DrivingBackwards` 选 `First()/Last()` 并沿链步进；该 flag 是**逐车字段**，上游靠 `ConsistChanged()` 从链头复制全链。
- R3R 的段编辑/翻转/解挂直接重链接、不调 `ConsistChanged` ⇒ 链上一半带 flag 一半不带（历史 KI-217 已实证过这个形态，表现为遍历周期性自环）。
- 若如此，"长度无效"是**遍历方向错**造成的假报警，也解释了为什么是"两段式"才出。
- 待核：`GetMovingFront/GetMovingNext` 与 `CalcNextVehicleOffset` 的实现，以及 R3R 段编辑后是否补归一。

---

## 六、下一步（按成本排序）

1. **静态核 H-389-A**（不需要玩家动作，纯读码）：`R3REnsureCoupleEndOrders()` 实现 + 读档期 `cur_real_order_index` 载入路径。
2. **让玩家用探针内测版读同一个 `crash.sav`**（关键，能一次拿全 U-388-1/2/3/5/7）：
   - 关注日志里读档段：`ORD-IDX-WATCH` / `COUPLE-AIR-SKIP` / `R3REnseureCoupleEndOrders` 相关行；
   - 关注 `veh=2` 的 `real=` 与 `num=` 是否相等；
   - 若一进站就崩，日志尾部 3000 行足够。
3. **让玩家在订单窗口数一下这列车的订单条数**（最便宜的交叉验证：条数 = 表长）。
4. 修法不变：**D-1 防御先行**（`timetable_cmd.cpp:740` 钳制 + `:746`/`:755` 判空，只改 .cpp，增量约 3 分钟）；`D-2'` 治根等 H-389-A/B 定案再动（改 .h ⇒ 全量 16~20 分钟）。

---

## 七、未确认项

| 编号 | 内容 | 需要什么 |
|---|---|---|
| U-389-1 | 「两段式」= 两个 R3R 段（★×2），还是只是两节车？ | 玩家描述 / 存档 `dump_vehicle 2` |
| U-389-2 | 读档红框是否每节都报（`:228 break` 说每列车只报一次） | 玩家观察 |
| U-389-3 | 该车 `TimetableStarted` 是否为 1（决定 `:1180` 分支是否成立） | 存档 / 探针 |
| U-389-4 | 该车 `cur_real_order_index` 与 `GetNumOrders()` 的实际值 | 探针 / 订单窗口条数 |
| U-389-5 | `R3REnsureCoupleEndOrders` 是否在表尾插条目并同步索引 | 读码（本轮待做） |
| U-389-6 | 长度无效是"真几何损坏"还是"遍历方向错"（H-389-C） | `GetMovingFront/Next` 读码 + `dump_vehicle` 的 `vf:B` |

---

## 八、本轮未做

- 未改任何源码、未构建。
- 未读到 `crash.sav` 本体（用户口头描述，文件位置待确认）。

---

## 九、取证结果（本轮已核完，H-389-A 证伪 / H-389-B 做实）

### 9.1 H-389-A **证伪**：`R3REnsureCoupleEndOrders()` 确实同步索引

`src\order_cmd.cpp:1875-1931`：

```cpp
list->InsertOrderAt(std::move(end_order), at);                 // :1906 表长 +1
Vehicle *const ring = list->GetFirstSharedVehicle();           // :1908
for (Vehicle *u = ring; u != nullptr && guard < 1000; u = u->NextShared(), ++guard) {   // :1910 共享链全员
    if (u->GetEffectiveOrders() != list) continue;
    if (at <= u->cur_real_order_index && u->cur_real_order_index + 1 < u->GetNumOrders()) u->cur_real_order_index++;  // :1912
    ...  // :1913/:1914 同构处理 implicit / timetable
    u->ResetDepotUnbunching();                                 // :1915
}
```

- 插入后 `GetNumOrders()` 已是新长度 ⇒ 条件 `cur+1 < 新长` 等价于 `cur <= 旧长-1`，即**只对原本合法的索引递增**。
- 若原本 `cur == 旧长`（已越界），条件为 `旧长+1 < 旧长+1` = false ⇒ **不递增**，但新长为 `旧长+1` ⇒ 该索引**反而变成合法**。
- ⇒ 这个函数**既不会造出越界、还可能顺手修好一个越界**。**H-389-A 不成立**，读档迁移不是元凶。
- 但它同时说明：**读档会静默改玩家的订单表内容**（给开口 `OT_GOTO_COUPLE` 补 `OT_COUPLE_END`）⇒ 请玩家"数订单条数"来反推表长的做法**不可靠**（U-389-4 的取证手段要换）。

### 9.2 H-389-B **做实**：`cur_real_order_index` 是存档字段，且现代存档读入后没有任何钳制

两段代码合起来闭环：

1. **持久化**：`src\saveload\vehicle_sl.cpp:102`

   ```cpp
   SLE_CONDVAR(Vehicle, cur_real_order_index, SLE_FILE_U8 | SLE_VAR_U16, SLV_158, SL_MAX_VERSION),
   ```

   （`:101` 的 `cur_implicit_order_index` 亦然）⇒ **越界值会原样写进 .sav**。

2. **读入不修**：`src\saveload\afterload.cpp:3188` 才是那个"填 / 修 `cur_real_order_index`"的块

   ```cpp
   if (IsSavegameVersionBefore(SLV_158)) {                  // :3188 ← 只对 SLV_158 之前的老档
       ...
       for (Vehicle *v : Vehicle::IterateFrontOnly()) {     // :3272
           if (!v->IsPrimaryVehicle()) continue;
           if (v->cur_implicit_order_index >= v->GetNumOrders()) v->cur_implicit_order_index = 0;  // :3276
           v->cur_real_order_index = v->cur_implicit_order_index;   // :3278
           v->UpdateRealOrderIndex();                       // :3279
       }
   }
   ```

   ⇒ **现代存档（SLV >= 158，本崩溃日志是 0.73.1）根本不进这个块**，`cur_real_order_index` 直接用文件里的值，`afterload.cpp` 全文再无第二处钳制（`:4173` 的 `cur_timetable_order_index` 是另一字段）。

**结论（升级为实证）**：只要上次会话把 `cur_real_order_index` 顶到 `GetNumOrders()`（REG-388-E 的产物，锁绕过 `vehicle_base.h:1161` 的钳制），这个越界值就被**写进存档**；本次读档原样载入、无人纠正；那列车开着"自动分离"，一进站 `BeginLoading → UpdateVehicleTimetable → UpdateSeparationOrder → PopulateSeparationState:755` 取 `GetOrder(n)` = `nullptr` ⇒ 崩。

⇒ **这是"存档带病"，不是本次会话新产生的**。也解释了为什么"一进去就（弹框/后续崩）"，以及为什么崩溃与玩家的操作序列无关。

### 9.3 对修法的直接影响（新增一条必须项）

- **D-1（运行时判空）仍然必要**，但**不足以修好存档**：它只能让程序不崩，索引还是错的，分离状态/时刻表会继续算错（甚至可能把错索引滚成更坏的形态）。
- **必须新增 D-4（读档钳制）**：在读档收尾（`afterload.cpp:325` 附近 `CheckTrainsLengths()` 之前）对全员加一次钳制：
  ```cpp
  for (Vehicle *v : Vehicle::Iterate()) {
      const VehicleOrderID num = v->GetNumOrders();
      if (v->cur_real_order_index >= num) v->cur_real_order_index = 0;      // 或 num ? num-1 : 0
      if (v->cur_implicit_order_index >= num) v->cur_implicit_order_index = 0;
  }
  ```
  只改 `.cpp` ⇒ 增量构建，不触碰存档格式。
- **D-2'（治根：锁不越过钳制）** 仍要做，否则下次还会再产生一个越界存档。

### 9.4 顺带：`9.2` 反向解释了 KI-556 的一部分

若存档里 `cur_real_order_index` 越界，`PopulateSeparationState` 的 `order_count`/`cumulative_ticks` 计算也会基于错索引 ⇒ 但这**不产生像素几何错误**，故 KI-556 的"长度无效"仍需 H-389-C 独立解释（两条判据无共享数据结构，见 §四）。

---

## 十、【重要前提更正，2026-11-03 玩家补充】这份 `crash.sav` 是**崩溃时游戏紧急自动保存的存档**

### 10.1 玩家原话

> 「不，你漏掉了一个关键的前提：这份存档本身就是游戏崩溃的时候游戏紧急自动保存的存档，所以它必须带病」

### 10.2 这个前提推翻了什么、保留了什么

**被推翻（我上一轮的因果链）**：§9.2 我写"越界索引是**上次会话**删表尾 `OT_GOTO_COUPLE` 撞锁后写进存档、跨会话存活"。若存档是崩溃瞬间的紧急快照，则越界值**不需要跨会话解释**——它就是崩溃那一刻内存里的状态被 dump 下来。

**仍然成立（实证，不受影响）**：

1. `cur_real_order_index` 是存档字段（`sl\vehicle_sl.cpp:102`）。
2. 现代存档读入**不做**索引钳制（`afterload.cpp:3188` 的 `IsSavegameVersionBefore(SLV_158)` 整块跳过）⇒ **越界值一旦进档就不会被读档修正**。这一条现在的作用从"解释来源"变成"解释为什么读档后立刻还会崩"。
3. KI-556 的发射点/判据/时机（`train_cmd.cpp:210-229`，`afterload.cpp:325`/`:4819`）。
4. KI-555 的崩溃栈与路径可达性（`timetable_cmd.cpp:1180-1185` 自动分离开关）。

**新推论（关键）**：既然存档是崩溃快照，那么**红框「长度无效」反映的也是崩溃那一刻的几何状态** ⇒ 崩溃发生时这列车**已经几何不自洽**，不是"健康列车 + 一个坏索引"。这把 KI-556 与 KI-555 从"可能同源"推向"**很可能是同一场崩溃的两个侧面**"：订单取到 nullptr 可能只是这场混乱里最先致命的一处。

**同时新增一个必须承认的局限**：紧急存档由崩溃处理器在异常态下写出，本身可能**不完整/半写**（堆已受损时的 SaveGame）。所以"存档里字段=X"只等于"崩溃瞬间内存里大概率是 X"，不能当"崩溃前 N tick 一直如此"的证据。

### 10.3 对修法与诊断顺序的影响

- **D-1（`timetable_cmd.cpp:740/746/755` 判空+钳制）仍然必做**：它修的是"任何让 `n` 越界的路径都会踩空指针"这个真实缺陷，与来源无关。
- **但不能承诺"修完这盘就不崩"**：若几何/链序损坏是主因，崩点会换到别处（图上的"错位/穿模"也会继续）。
- 因此诊断必须回答一个问题：**崩溃那一刻，这列车的 `cur_real_order_index` 与 `GetNumOrders()` 到底是不是相等**（形态 (a)）。是 ⇒ 索引越界是致命点，D-1 直接止血；不是 ⇒ 索引不是主因，几何/`DrivingBackwards`/`ConsistChanged` 才是，D-1 只是降噪。
- 取数手段（按成本排序）：①内测版读档后 `dump_vehicle <id>` 逐节看 `real=`/订单数/`vf:B`(★)、`tf:` 里 `S`/`E`(★⊗)、`db=`；②`build\R3R_debug.log` 里 `ORD-IDX-WATCH`/`CHAIN-ATTRS`/`GEO` 行；③订单窗口数条数（**已被 §9.1 判定不可靠**，读档会补 `OT_COUPLE_END` 改表）。

### 10.4 新增未确认项

| 编号 | 内容 | 需要什么 |
|---|---|---|
| U-389-7 | 紧急存档写出时是否完整（崩溃处理器内 SaveGame 是否可能半写） | 读 `crashlog.cpp` 的 `EmergencySave()` 实现 |

### 10.5 U-389-7 已结案（本轮读码实证）：紧急存档 = 崩溃点之后、抢不到锁也照存，走正常非线程 SaveGame

```1000:1044:src/crashlog.cpp
	if (!VideoDriver::EmergencyAcquireGameLock(20, 2)) {  ... have_game_lock = false; }
	...
	_savegame_DBGL_data = buffer;        // :1037 崩溃日志文本内嵌进存档
	_save_DBGC_data = true;              // :1038
	if (!VideoDriver::EmergencyAcquireGameLock(1000, 5)) {
		this->WriteToStdout("Failed to acquire gamelock before writing crash savegame and screenshot, proceeding without lock as current owner is probably stuck\n\n");   // :1041
	}
	CrashLog::MakeCrashSavegameAndScreenshot(name_buffer);   // :1044
```

```880:896:src/crashlog.cpp
/* static */ bool CrashLog::WriteSavegame(char *filename, const char *filename_last, const char *name)
{
	if (_m.tile_data == nullptr) return false;
	try {
		GamelogEmergency();
		format_to_fixed_z::format_to(filename, filename_last, "{}{}.sav", _personal_dir, name);
		/* Don't do a threaded saveload. */
		return SaveOrLoad(filename, SaveLoadOperation::Save, DetailedFileType::GameFile, Subdirectory::None, false) == SaveLoadResult::Ok;
	} catch (...) { return false; }
}
```

四条结论：

1. **时机**：紧急存档在崩溃处理流程里、**崩溃点之后**执行 ⇒ 文件内容是"崩溃那一刻内存态"的照片，不是"崩溃前的健康态"。
2. **格式自洽**：走的是**正常** `SaveOrLoad(..., Save, GameFile, None, threaded=false)`，与玩家手动存档同一个写出器 ⇒ **存档文件本身格式合法、能正常读进来**（这就是为什么你能顺利读档、只是弹红框）。
3. **可能撕裂（新增疑点 REG-389-A）**：`:1040` 抢锁失败时会**无锁直接存**（注释自陈 "current owner is probably stuck"）。⇒ 若崩溃发生在非主线程（音频/视频/网络），主线程可能仍在改内存，存出来是"跨时刻混合态"。

   **【2026-11-03 本轮更正】**：`crash-20261102T120318Z.log:18` 明确写着 `Non-main thread (ottd:game)` ⇒ **崩溃发生在游戏线程（`ottd:game`），不是主线程**。上一版这里写"本例栈是主线程 `TrainEnterStation→...→PopulateSeparationState`"是**错的**，两条理由：
   - 该日志的 `Decoded stack trace`（`:30-42`）只有 12 个裸地址，**没有任何符号**，本地也没有与之匹配的 PDB，所以"栈是 `TrainEnterStation→PopulateSeparationState`"从来不是从这份日志读出来的；
   - 崩溃确实在 `TrainController` 里（见 §12.2 的 SCOPE_INFO 论证），而 `TrainController` 是 `CallVehicleTicks` 调用的，两者都在 `ottd:game` 线程。

   ⇒ 撕裂风险的准确表述：`ottd:game` 已崩停，但 `ottd:winmain`（窗口消息/GUI/命令队列）**仍在运行**；`EmergencyAcquireGameLock` 失败时无锁存档，主线程可能继续提交 `Command` 或刷新窗口缓存 ⇒ 存档里"车辆池/订单池"这类被两线程都碰过的结构，仍可能跨时刻混合。**结论方向不变**（存档是崩溃瞬间的照片、且可能略脏），但"主线程已停所以内存一致到断点"这句必须撤回。
4. **附带收获**：`:1037` `_savegame_DBGL_data` / `:1038` `_save_DBGC_data = true` ⇒ 这盘 `crash.sav` **自带崩溃日志文本**（你贴的栈就是从这里来的）+ 崩溃截图。所以"存档 + 日志"是同一时刻的两份互证材料，不需要再对齐时间。

⇒ **对诊断的正向价值**：既然它是崩溃瞬间的完整内存快照，那么"这列车每节的 `cur_real_order_index` 与 `GetNumOrders()`"就是**崩因的现场照片**。读出来若相等（形态 (a)），KI-555 直接定案；若不等，则索引不是主因，KI-556 的几何错位才是主战场。
| U-389-8 | 崩溃瞬间该车 `cur_real_order_index` 是否 `== GetNumOrders()`（形态 (a)） | `dump_vehicle` / 探针日志 |
| U-389-9 | 崩溃瞬间该车几何是否已错位（KI-556 红框的直接来源） | `dump_vehicle` 逐节 x/y/dir/db + `GEO` 行 |

---

## 十一、【玩家 2026-11-03 定调】不要急着打补丁，先确认根因；判据是「原生 JGRPP 正常、R3R 崩 ⇒ 只能是 R3R 引入」

### 11.1 玩家原话

> 「我不建议急急忙忙的打补丁，而是确认了根因。因为自动和自动分离这东西原生 JGRPP 跑得好好的，到 R3R 就崩溃了，只能说明是 R3R 引入的问题，不是原生的问题。我接下来把这份存档地址发给你，它在 C:\Users\冯洁敏\Downloads\r3r，里面只有一份存档，就是它」

⇒ 定位方法改为**差分解剖**：不是问"哪个索引越界了"，而是问"R3R 改了哪条原生不变式，使得原生 `PopulateSeparationState` 的前提不再成立"。

### 11.2 H-389-D（本轮新增，候选首选）：R3R 借用层破坏了"共享链索引同步"这一原生前提

`PopulateSeparationState` 的遍历是原生的：

```736:755:src/timetable_cmd.cpp
	if (v_start->GetNumOrders() == 0) return out;
	for (const Vehicle *v = v_start->FirstShared(); v != nullptr; v = v->NextShared()) {
		...
		const int n = v->cur_real_order_index;
		...
		const Order *order = v->GetOrder(n);
```

**原生前提**：`FirstShared()/NextShared()` 沿共享同一 `OrderList` 的车遍历，而原生里进入这条链只有两条路——①玩家显式"共享订单"（Ctrl+点），②无共享（链上只有自己）。这两条路下，共享链上每节的 `cur_real_order_index` 都由原生代价点统一推进，**恒同步、恒 < 表长**。

**R3R 的破坏**：借用层（`r3r_orders_borrowed` / `orders_backup`，KI-93 修复时已让借用注册成原生共享链，见 `r3r_borrow_is_shared`）把**多节车/多列车**挂到同一 `OrderList` 上，而借用期的索引是**冻结的**（KI-04；KI-215b 的 `ORD-PUSH` 只是补账、不是持续同步）。⇒ 共享链上出现"索引不同步"的车，冻结值可能正好等于表长 ⇒ `GetOrder(n)` = `nullptr`。

**这条同时解释三件事**：

1. **为什么原生不崩**：原生共享链索引恒同步，`n < num` 恒成立。
2. **为什么"两段式 + 自动分离"崩**：两段 ⇒ 链上多节 `GetEffectiveOrders()` 指向同一表 ⇒ 原生那个循环多跑几轮，命中冻结索引的概率从"零"变成"必然"。
3. **为什么"一读档就埋雷"**：冻结索引是逐车存档字段（`vehicle_sl.cpp:102`），读档门是 `afterload.cpp:3188` 的 `IsSavegameVersionBefore(SLV_158)`（现代档整块跳过）⇒ 冻结值原样复活。

**与 §9.1 的关系**：`R3REnsureCoupleEndOrders` 只对"原本合法"的索引递增，它对冻结索引完全无感——所以读档迁移也救不了它。

### 11.3 H-389-D 的验证判据（不改一行业务代码）

| # | 判据 | 数据源 |
|---|---|---|
| V-1 | 崩溃/报错那节车在共享链上**不是链头**（`v != v_start`） | 崩栈 + `dump_vehicle` |
| V-2 | 该节车 `cur_real_order_index == GetNumOrders()`（形态 (a)） | `dump_vehicle` / 探针 |
| V-3 | 该链处于借用态（`ORD-AFTER-COUPLE ... borrowed=1` 或 `r3r_orders_borrowed=1`） | 读档期日志 |
| V-4 | 同一共享链上两节的 `cur_real_order_index` **不相等** | `dump_vehicle` 逐节 |
| V-5 | 代码侧：R3R 把借用注册成 `first_shared` 后，`NextShared()` 链长度 > 1 且成员索引未同步 | 读码（本轮待做） |

V-4 是**决定性**的：原生不可能出现"同一张表、两节车、索引不同"，一旦出现就是 R3R 引入。

### 11.4 关于存档本体

`.sav` 是压缩容器（chunk 由 zstd/lzma 压缩），其中 `DBGC`/`DBGL` 虽有崩溃日志文本也压在压缩块内 ⇒ **无法离线解析出字段值**。要拿 V-1~V-4 的读数，只有两条路：
① 内测版（`build\openttd.exe`，探针 ON）读这盘，读档期探针自动打 `GEO settle-load` / `ORD-*` / `CHAIN-ATTRS`，再把 `build\R3R_debug.log` 回传；
② 读档后在控制台 `dump_vehicle <id>` 逐节看 `real=`/订单数/`vf:B`。

---

## 十二、本轮取证（2026-11-03）：崩溃点定位、"veh: nullptr" 解密、删除订单时间线

材料：`C:\Users\冯洁敏\Downloads\r3r\crash-20261102T120318Z.log`（唯一原始材料，`.sav` 不可离线解析）。
本轮**零源码改动、零构建**（遵守 11.1 的"先定根因、不打补丁"）。

### 12.1 线程事实（已回填 §10.5 第 3 条）

`crash-20261102T120318Z.log:18` = `Non-main thread (ottd:game)`；`:30-42` 的 `Decoded stack trace` 只有 12 个裸地址、无符号。
⇒ ①崩溃在游戏逻辑线程；②"栈是 `TrainEnterStation→PopulateSeparationState`"这一旧表述**无出处，撤回**；③"主线程已停 ⇒ 内存一致到断点"随之撤回，无锁紧急存档的撕裂面比我上一版估计的更大（`ottd:winmain` 仍在跑）。

### 12.2 `Within context: veh: 2 ... , veh: nullptr, veh: nullptr` 的正确读法（**更正此前的误读**）

以前把它读成"链上后两节是 nullptr（列车只有一节车）"。**错**。它来自 JGRPP 的 SCOPE_INFO 机制：

```train_cmd.cpp:TripLocomotive/TrainController
	SCOPE_INFO_FMT([&], "TrainController: veh: {}, veh: {}, veh: {}", ...)
```

三个参数依次是 `v`（当前车）、`prev`（**函数内局部变量，崩溃时还没被赋值 ⇒ nullptr**）、`nomove`（**入参，本次调用传的就是 nullptr**）。
⇒ `veh: nullptr, veh: nullptr` 是**作用域变量快照**，与"链上有几节车"无关；因此不能据此判断列车只有一节。

真正的信息量在于：崩溃时**仍在 `TrainController` 的作用域内**（SCOPE_INFO 是 RAII 作用域栈，只有函数未返回才在栈上）⇒ 崩溃点在 `CallVehicleTicks → TrainController` 这条调用树里，与该车（veh 2，`DF4B-2001`，停在 Station 格 1739 x 66）的订单处理/时刻表结算路径相关。

（训练推论：`UpdateSeparationOrder(v)` 的两条调用点 `timetable_cmd.cpp:982`（自动时刻表首次启动）与 `:1183`（到站结算）都在 `ProcessOrders/TrainController` 下游；该车 `vf:` 含 `T`+`z`+`s`+`a`，恰好是这两条路径的开关条件，故"可行"。）

### 12.3 `vf:` / `tf:` / `vs:` / `vcf:` 字母表（据 `src/vehicle.cpp` 的 dump 字母表核对）

| 段 | 现场值 | 逐个字母 |
|---|---|---|
| `vf:` | `Tzsa` | `T`=TimetableStarted、`z`=SeparationActive、`s`=TimetableSeparation、`a`=AutomateTimetable |
| `tf:` | `rHSZ` | `r`=？（见注）、`H`=ArticGroupHead、`S`=SegmentFront、`Z`=SegmentFlipped |
| `vs:` | `Ds` | `D`=DefaultPalette、`s`=TrainSlowing |
| `vcf:` | `zd` | `z`=VCF_GV_ZERO_SLOPE_RESIST、`d`=VCF_IS_DRAWN |

结论：**四个自动分离/自动时刻表标志同时为真**（`T`+`z`+`s`+`a`）——这正是 `PopulateSeparationState` 唯一会真的去遍历共享链取 `n` 的状态；`tf:` 里的 `H`（ArticGroupHead）+`S`（SegmentFront）证明这列车是 **R3R 的段/去铰接组建模**，即"原生机制跑在 R3R 数据结构上"。

注：`tf:` 的 `r` 我还没在字母表里对上（`dump_vehicle` 的 Train 分支后半有新加的 `H/M/S/E/Z/O`，前半是原生 `R W P r h e q s L b p v z F B Y A K J X c`）；**未确认项 U-389-10**，不影响本推断（判"段建模"只需 `H`/`S`）。

### 12.4 崩溃前同一 tick 的订单编辑时间线（`Command Log`，`1920-03-16, 12, 6`，窗口 `1736 x 36`）

按顺序、只列 vehicle 2：

| cmd | 操作 | 语义 |
|---|---|---|
| 6,7,8,9 | `CmdModifyOrder 2,6 / 2,15`（flags 3→4、2→1 等） | 改两条订单的装货/非停参数 |
| 10 | `CmdInsertOrder 2,17`（`t:F` = **OT_DECOUPLE(15)**） | 末尾插一条解挂订单 |
| 12~16 | `CmdInsertOrder` @16,15,14,13,11 | 连续插入 5 条 |
| 24~27 | `CmdSkipToOrder 2,4 / 2,3 / 2,2 / 2,1` | **连点"跳到下一条"把 `cur_real_order_index` 推到 1** |
| 28 | `CmdInsertOrder` @4 | |
| 29 | `CmdInsertOrder` @2 | |
| **30** | **`CmdDeleteOrder 2,2`** | 删一条 |
| 32 | `CmdInsertOrder` @2 | |
| 33 | `CmdInsertOrder` @1 | |
| 34 | `CmdInsertOrder` @0 | |
| **35** | **`CmdDeleteOrder 2,0`** | 再删一条 |
| 36 | `CmdPause 0,true` | 暂停 → 随即崩溃 |

三个可直接读出的事实：

1. **崩溃前最后一刻是"订单表被连续增删 + 索引被手工 Skip 到 1"**，不是行驶中自然推进；表长在几条命令内多次变化，`cur_real_order_index` 也被 `SkipToOrder` 显式改写。
2. `CmdInsertOrder` 与 `CmdDeleteOrder` **对索引的调整方向相反**（插入且 `pos <= index` 则 `+1`；删除且 `pos < index` 则 `-1`）。30 与 34/35 这两步就是"插 0 → 删 0"的成对抵消；中间任何一处漏调/多调，索引就停在错误值。
3. `CmdPause` 之前**没有**先让该车"到站结算"的迹象，但崩溃恰在 `TrainController` 里 ⇒ 该 tick 的命令队列执行完后，本 tick 的车辆 tick 照跑（暂停只影响下一 tick 起）。

### 12.5 H-389-E（**猜测根因（待验证）**）：删除/插入订单时，R3R 借用层的"同表但不入环"的车被漏调索引

支撑证据（按强弱排）：

- `src/order_cmd.cpp:2027` 起 `DeleteOrder()` 里确有一段 R3R 专用索引同步（KI-465 "修法 D"）：以 `GetFirstSharedVehicle()` 环为遍历范围，另对"当前编辑的 follower 自己"补调一次。⇒ 说明此前**已经发生过**"删订单导致别的车索引不同步"的 bug，且当时的结论就是"环 + 自己"两处要覆盖。
- 注释里明确警告："不能因为 `first_shared` 为空就退回 `v->FirstShared()`：那会把跟随者自己的（闲置）表环当成被编辑的表环，导致它被调整**两次**"。⇒ 该修法只处理"一个 follower 被编辑"的场景，**没有**处理"同一张被跟随的表上还有别的 follower"。
- R3R 借用层存在两种成员形态：①`r3r_borrow_is_shared` 的真共享链成员（在环里，会被遍历到）；②只靠 `r3r_follow_orders` 指针跟随的成员（**不在被跟随表的共享环里**，只能靠"自己被编辑"那一支补调）。若同一条链上存在 **≥2 个 ②型成员**，删除/插入订单时只有被编辑的那一个被调整，其余停在旧索引。
- 现场 `tf:` 有 `H`+`S` ⇒ 该车是段/去铰接组建模，`GetEffectiveOrders()` 很可能走跟随分支而非自己的表 ⇒ 形态 ② 成立的前提具备。

⇒ 机制：表长 N，某 ②型车索引 = N-1（最后一条，合法）；玩家删掉位置 0 的订单 ⇒ 表长 N-1，若该车漏调则索引仍是 N-1 == 新表长 ⇒ `PopulateSeparationState` 的 `GetOrder(N-1)` 返回 `nullptr` ⇒ `order->...` 读 `0x0C` 崩溃（与 `Fault addr 0x0C` 吻合）。

**为什么原生不崩**：原生没有任何"同表但不入环"的车——`FirstShared()` 环就是全部持表者，索引调整覆盖全集。
**为什么是 R3R 引入**：②型成员（follow 指针）是 R3R 独有的结构。

### 12.6 H-389-E 的验证判据（待取数，仍不改码）

| # | 判据 | 手段 |
|---|---|---|
| E-1 | 该车（及同链其它节）`GetEffectiveOrders()` 是否 `!=` 自己持有的 `orders`（即走跟随分支） | 读码 `GetEffectiveOrders()`/`r3r_follow_orders`；探针 `ORD-IDX-WATCH` |
| E-2 | `CmdDeleteOrder`/`CmdInsertOrder` 之后，同表的 ②型成员索引是否与"应当的值"一致 | 在 `DeleteOrder`/`InsertOrder` 的 `cmd-delete-in/out`、`cmd-insert-in/out` 快照里比对**逐车** `real=` |
| E-3 | 崩溃前一刻 veh 2 的 `real=` 是否 `==` 当时 `GetNumOrders()` | 内测版读档 + 复现操作用 `dump_vehicle` / 探针 |
| E-4 | 负判据：纯原生（无 R3R 借用/无段）同场景删同一位置的订单，索引仍严格 `<` 表长 | 原生 0.73.x 对照 |

### 12.7 仍未确认

- U-389-10：`tf:` 的 `r` 字母含义（不影响结论）。
- U-389-11：`DeleteOrder` 修法 D 的遍历闭包到底覆盖哪些形态（我只读到主遍历 + follower 补调，**没有逐行核对 ②型多成员场景**）⇒ 这是 E-2 的读码任务，下一轮做。
- U-389-12：崩溃前 `cmd 30`/`cmd 35` 两次删除具体删掉了哪两条（日志只给位置，不给内容）⇒ 需要内测版复现时打 `ORD-IDX-WATCH` 前后对照。

---

## 十三、代码核对（2026-11-03 第 2 组）：`DeleteOrder`/`InsertOrder` 的索引同步闭包

玩家指令：「你就先读代码核对吧」⇒ 本轮只读码，**零改动、零构建**。目标 = 判据 E-2（修法 D 的遍历闭包到底覆盖谁）。**结论：闭包有确定缺口，H-389-E 的第一半成立。**

### 13.1 事实（逐行，`src\order_cmd.cpp`）

`DeleteOrder(Vehicle *v, VehicleOrderID sel_ord)`，`:2027-2120`：

```order_cmd.cpp:2031-2092
OrderList *const eff_orders = v->GetEffectiveOrders();      // 2031
if (eff_orders == nullptr) return;                          // 2032
eff_orders->DeleteOrderAt(sel_ord);                          // 2033
OrderList *const owned_orders = v->orders;                   // 2039
const bool follower = (eff_orders != owned_orders);          // 2040
Vehicle *const shared_ring = follower ? eff_orders->GetFirstSharedVehicle() : v->FirstShared();  // 2041
auto adjust_indices = [&](Vehicle *u) {
    if (u->GetEffectiveOrders() != eff_orders) return;        // 2044  ← 过滤正确
    ...                                                        // 2050-2077 索引调整
};
if (shared_ring != nullptr) {                                 // 2084
    for (Vehicle *u = shared_ring; u != nullptr && ring_guard < 1000; u = u->NextShared()) adjust_indices(u);  // 2087
}
if (follower && shared_ring != v) adjust_indices(v);          // 2092  ← 只补"编辑者自己"
```

`InsertOrder(Vehicle *v, Order &&new_o, VehicleOrderID sel_ord)`，`:1725-1804`：**同构、同缺口**（`:1749-1752` 选环，`:1802` 遍历，`:1806` 补自己）。

⇒ **实际被调整的集合 = 「`eff_orders` 的原生共享环成员」∪「编辑者 `v` 自己」**（`v` 若已在环里则不重复）。

### 13.2 缺口 = 「同表、但不在环里、又不是编辑者」的车

`adjust_indices` 的过滤条件是对的（`GetEffectiveOrders() == eff_orders`），**错的是候选集合**：它只枚举了环 + 编辑者，没枚举"全池中 GetEffectiveOrders()==eff_orders 的车"。掉进缺口的形态至少两类：

| 形态 | `orders` | `r3r_follow_orders` | 在 `eff_orders` 环里？ | 被调整？ |
|---|---|---|---|---|
| ① 原生共享成员 | == eff_orders | nullptr | **是** | 是 |
| ② 编辑者自己是 follower | 自有闲置表 / nullptr | == eff_orders | 否 | 是（`:2092` 特判） |
| ③ **其他 follower（同表）** | 自有闲置表 / nullptr | == eff_orders | **否** | **否** ← 缺口 |
| ④ `orders==eff_orders` 但没进环（R3R 历史路径/读档带入） | == eff_orders | nullptr | **否** | **否** ← 缺口 |

### 13.3 为什么原生不可能有 ③④（差分，正是玩家要的那一刀）

- **原生不变式**：`orders == X` ⟺ 该车在 `X` 的 `first_shared` 环里（`AddToShared`/`RemoveFromShared` 维护，`DebugCheckSanity` 还断言 `v->orders == this` 与计数一致）。而 `Vehicle::FirstShared()`（`vehicle_base.h:887`）就是 `(orders==nullptr) ? First() : orders->GetFirstSharedVehicle()` —— 环 = 全部持表者 ⇒ `DeleteOrder` 遍历环 = 遍历全集。**原生这套是完备的。**
- **R3R 修法 D** 新增 `r3r_follow_orders`（`vehicle_base.h:394`）与 `GetEffectiveOrders()`（`:405`：`r3r_follow_orders != nullptr ? r3r_follow_orders : orders`）。follower 的 `orders` 指向**自己的闲置表**（或 nullptr），`FirstShared()` 仍只读 `orders` ⇒ **follower 在结构上永远进不了 `eff_orders` 的环**。修法 D 的作者知道这点（`:2035-2038` 注释写了"跟随者不在被跟随表的共享环里"），但补丁只补了 `v` 这一个。

⇒ 这是 **R3R 独有的结构**造成的、原生不可能出现的漏调面，与玩家定调（"原生跑得好好的，到 R3R 就崩"）逐字对应。

### 13.4 为什么「删除」比「插入」致命（+1/−1 不对称，解释现场为何是删订单后崩）

- 插入（`:1762-1767`）：`if (sel_ord <= index) { cur = index+1; if (cur < GetNumOrders()) index = cur; }` —— 插入后表长已 +1，漏调 ⇒ 索引**偏小**（停在一条已跑过的命令上，行为错但**不越界**）。
- 删除（`:2050-2054`）：`if (sel_ord < index) index--;` —— 漏调 ⇒ 索引**停在旧值**。若旧值 = 旧表长−1（最后一条）且 `sel_ord < index`，删掉一条后新表长 = 旧表长−1 ⇒ **index == 新表长** ⇒ `GetOrder(n)` 返回 `nullptr` ⇒ `timetable_cmd.cpp:756` 解引用 ⇒ `Fault addr 0x0C`。

现场 `cmd 30 CmdDeleteOrder 2,2` / `cmd 35 CmdDeleteOrder 2,0` 正是删除，且删除前刚做过 `SkipToOrder 4→3→2→1`（把编辑者索引压到 1，但**不是所有共享者/跟随者的索引都被压**）。⇒ 形态闭合。

### 13.5 本轮新增/关闭的条目

- **U-389-11 → 已答**：闭包 = 环 ∪ 编辑者，缺「同表非编辑者 follower」与「orders 同表却不在环」。
- **U-389-13（新，待取数）**：现场是否真存在 ≥2 个落在缺口里的车？代码允许（修法 D 允许多个 follower 跟随同一张表），但崩溃日志没有跟随关系读数。判据见 §12.6 的 E-2/E-3；最快路径 = 内测版读档，看 `ORD-IDX-WATCH` / `dump_vehicle` 的 `real=` 与 `num`。
- 仍未改任何业务代码。

### 13.6 崩溃点为何能"看到"漏调的车（`timetable_cmd.cpp:733-783` 逐行）

```timetable_cmd.cpp:733-756
std::vector<TimetableProgress> PopulateSeparationState(const Vehicle *v_start)
{
	if (v_start->GetNumOrders() == 0) return out;                 // 736  ← 读 eff 表条数
	for (const Vehicle *v = v_start->FirstShared(); v != nullptr; v = v->NextShared()) {  // 737  ← 读 own 表的环
		if (!v->vehicle_flags.Test(VehicleFlag::SeparationActive)) continue;               // 738
		bool separation_valid = true;
		const int n = v->cur_real_order_index;                    // 740  ← 每节车自己的索引
		...
		const Order *order = v->GetOrder(n);                       // 755  ← 读 eff 表
		if (order->IsType(OT_CONDITIONAL)) continue;               // 756  ← 空指针在这里炸
```

三条读源码得到的硬事实：

1. **`GetNumOrders()` 与 `GetOrder()` 都走 `GetEffectiveOrders()`**（`vehicle_base.h:899-903` / `:1179-1183`），而 **`FirstShared()` 走 own `orders`**（`vehicle_base.h:887`：`(orders==nullptr) ? First() : orders->GetFirstSharedVehicle()`）。
   ⇒ 同一个循环里「表长/取单」用一套指针、「遍历集合」用另一套指针。**原生两者恒同源**（`orders` 唯一），R3R 加了 `r3r_follow_orders` 之后两者可以不同源。这是 §13.3 缺口的第二条独立证据。
2. `:740` 的 `n` 是**被遍历那一节车自己的索引**，`:755` 的 `GetOrder(n)` 也是它自己的 eff 表 ⇒ 只有「索引 > 自己的 eff 表长」才崩。索引由谁维护 = `DeleteOrder`/`InsertOrder` 的 `adjust_indices`（§13.1）⇒ 漏调即越界。
3. `:738` 只看 `SeparationActive`。现场 `vf:Tzsa` 的 `z` 就是 `SeparationActive` ⇒ 该车（及链上同类车）**不会**被这一行跳过。

⇒ **E-3 完整形态**：tick 的链头 `veh 2` 是 follower（`orders` 是自有闲置表、`r3r_follow_orders` 指向被跟随表），它调 `UpdateSeparationOrder` → `PopulateSeparationState(veh 2)`；`FirstShared()` 返回的是 **veh 2 自有表**的环（含同为该环成员的 `u`）；`u` 也是同一张被跟随表的 follower（`GetEffectiveOrders()==eff_orders`）⇒ 删订单时 `u` 落在 §13.2 的缺口 ③，索引停在旧值；表被删短一条后 `u.cur_real_order_index == u.GetNumOrders()` ⇒ `:755` 得 `nullptr` ⇒ `:756` 读 `0x0C` 崩。崩溃日志只会打 tick 的 `veh: 2`，**不会**打真正越界的 `u` —— 与「上下文只有 veh 2」不矛盾。

### 13.7 最后一环：命令执行体里没有兜底（已核实）

```order_cmd.cpp:1975-2000
CommandCost CmdDeleteOrder(DoCommandFlags flags, VehicleID veh_id, VehicleOrderID sel_ord)
{
	...
	if (sel_ord >= v->GetNumOrders()) return DecloneOrder(v, flags);   // 1991
	if (v->GetOrder(sel_ord) == nullptr) return CMD_ERROR;             // 1993
	if (flags.Test(DoCommandFlag::Execute)) {
		R3ROrderIdxWatch r3r_idx_watch(v, "cmd-delete-in", "cmd-delete-out");  // 1996 只读探针
		DeleteOrder(v, sel_ord);                                              // 1997
	}
	return CommandCost();                                                     // 1999 直接返回
}
```

⇒ 命令执行体 = 一次 `DeleteOrder()` + 一个只读探针，**没有** `R3RSettleChainSegments()`、没有索引钳制、没有 `UpdateRealOrderIndex()` 兜底。`DeleteOrder` 体内也只有 `eff_orders->DeleteOrderAt()` + 环/自己的 `adjust_indices` + `R3REnsureCoupleEndOrders()`（`:2116`，只补挂接起止订单，不碰 `cur_real_order_index`）。

⇒ 漏调的索引**原样留到命令返回之后**，下一 tick 车辆 tick 直接消费。这与现场「`cmd 35` 删订单 → `cmd 36` 暂停 → 本 tick 车辆逻辑照跑 → 崩在 `TrainController`」完全一致（暂停只影响下一 tick 起）。

`CmdInsertOrder` 的兜底**推断同构**（`:1725-1806` 亦无 settle），记为 U-389-14 待顺手核实；插入方向的漏调不越界（§13.4），故不影响本案定性。

### 13.8 本轮结论（代码级）

**H-389-E 从"猜测根因（待验证）"升为「根因（代码级已证，现场读数待补）」**，三个环节全部落到具体行：

| 环节 | 证据 |
|---|---|
| 缺口的产生 | `DeleteOrder`/`InsertOrder` 的同步闭包 = `eff_orders` 原生环 ∪ 编辑者自己（`order_cmd.cpp:2041/2087/2092`），漏「同表非编辑者 follower」；而 follower 结构上不可能进环（`vehicle_base.h:394/405` vs `:887`） |
| 缺口为何会越界 | 删除是 `sel_ord < index ⇒ index--`（`:2050`），漏调即停在旧值；旧值 = 旧表长−1 时，新表长 = 旧−1 ⇒ `index == GetNumOrders()` |
| 越界如何变成崩溃 | `PopulateSeparationState` 对环上每节车取自己的 `n` 与自己的 eff 表（`timetable_cmd.cpp:740/755`），`:736` 的守卫只看 `v_start` 的表非空，`GetOrder(n)` 得 `nullptr`，`:756` 解引用 → `Fault addr 0x0C` |
| 为何没有兜底 | `CmdDeleteOrder` 执行体无 settle/钳制（`order_cmd.cpp:1995-1999`） |
| 为何原生不崩 | 原生 `orders` 唯一且 `FirstShared()` 就用它 ⇒ 环 = 全部持表者，闭包完备（§13.3） |

### 13.9 仍未闭合的（现场读数，非代码）

- **U-389-13**：现场是否真有 ≥2 个 follower 落在缺口里（崩溃日志无跟随关系读数）。
- **U-389-14**：`CmdInsertOrder` 是否同样无兜底（推断同构，未逐行核）。
- 需要内测版读档 + 复现那串增删，抓 `ORD-IDX-WATCH`（`cmd-delete-in/out`）与 `dump_vehicle` 的 `real=`/`num`，才能把"现场确实是这条路"钉死。在此之前**不打补丁**（遵玩家定调）。

---

## 十四、玩家四问：①自己的表 ②当前指向的表 ③当前进度 ④书签进度

> 玩家原话：「我想知道，这是否和"自己拥有的表""当前指向的表""自己当前的排程进度""书签上记录的自己表的排程进度"这四者有关」
> **答案：全部命中，但要分层——只有 ③ 能崩，④ 只错不崩，①②是根。** 本章零改动、零构建。

### 14.1 四者的代码本体与坐标系归属

| # | 玩家用词 | 代码字段 | 语义/坐标系 | 存档 |
|---|---|---|---|---|
| ① | 自己拥有的表 | `Vehicle::orders` | **所有权坐标系**。书签④以它为基准 | ORDL（`OrderList::Iterate`） |
| ② | 当前指向的表 | `r3r_follow_orders`（`vehicle_base.h:394`）+ `GetEffectiveOrders()`（`:405`） | **执行坐标系**。`GetNumOrders()`/`GetOrder()` 都走它（`:899-903`/`:1179-1183`） | 指针（NOSAVE，读档由 `R3RRebuildCouplePriorities` 重建） |
| ③ | 自己当前的排程进度 | `cur_real_order_index`（原生字段，仅一个） | **恒属于②的坐标系** | 原生 SAVED |
| ④ | 书签上记录的自己表的进度 | `orders_backup_real_index` / `orders_backup_implicit_index`（`vehicle_base.h:413-414`）；表本体 `orders_backup`（`:409`） | **恒属于①的坐标系** | R3VP 稀疏块（KI-169a / 第 160 轮补 name_backup） |

**关键结构事实：③ 只有一个字段，④ 也只有一个字段，而"表"有两张（①②）。** 原生时代 ①≡②，所以"两套坐标系"退化成一套，四者实际只有两个量；修法 D 拆开 ①② 之后，坐标系变成两套，但**索引字段没有跟着拆**。

### 14.2 谁在维护谁（逐处核对）

| 维护动作 | 代码位置 | 动 ③？ | 动 ④？ |
|---|---|---|---|
| 正常推进 | `ProcessOrders`/`UpdateRealOrderIndex` | 是（按②的坐标系） | 否 |
| 删除订单 | `DeleteOrder::adjust_indices`（`order_cmd.cpp:2044-2077`） | 是，**但只覆盖环∪编辑者**（§13 缺口） | **否** |
| 插入订单 | `InsertOrder`（`:1749-1806`） | 同上 | **否** |
| 借出前停放书签 | `R3RParkOwnOrderProgress`（`train_cmd.cpp:4009`） | 否 | 是（**仅当 ④ 为 INVALID、即只写一次**，`:4022`） |
| 切表点刷新书签 | `R3RRefreshOrderBookmark`（`:6757`） | 否 | 是（覆盖写） |
| 归还书签 | `R3RSegmentReturnOrders`（`:4013` 附近） | 是（④→③） | 是（清 ④） |

两处守卫决定了 ④ 的**安全边界**：

```train_cmd.cpp:6766-6768
if (v->GetEffectiveOrders() != own) return false;          // 6766 只有"正在跑自己的表"才写
if (v->cur_real_order_index >= own->GetNumOrders()) return false;  // 6768 越界不写
```

⇒ ④ **从不记录越界值**，且只在 `②==①` 时才被写。这两条把 ④ 从"会崩"降级为"只会错"。

### 14.3 分层结论

**第一层（能崩）= ②+③，与 ①④ 无关。**
③ 是②坐标系里的索引；`DeleteOrder` 只调整"正在跑这张 eff 表的人"的 ③，而候选集合漏了「同表非编辑者 follower」（§13）。漏调 ⇒ ③ 停在旧值 ⇒ 等于新表长 ⇒ `PopulateSeparationState` 的 `GetOrder(n)==nullptr` ⇒ `Fault addr 0x0C`。**这条链里 ① 和 ④ 完全没参与。**

**第二层（只错不崩）= ①+④。**
`DeleteOrder`/`InsertOrder` **从不碰 ④**。所以：某车 `orders_backup` 正指着自己的表①，玩家在别的车上编辑了同一张表（或它自己把表借出去期间被编辑）⇒ ① 变短，④ 仍停在旧坐标。归还时 `④→③` 会用旧坐标，**但归还点有钳制**（`:4018-4019` 注释自陈"0 在自己 8 条表上合法，7485 的钳制与 7480 的告警都放过"）⇒ 结果 = **解挂后命令跳回错误位置**，不崩。这正是 KI-526 / KI-527 / 第 353 轮那一族"命令跳回第一条"的同源缺陷。

**根 = ①② 分家后，③ 没有拆成两套。** R3R 只维护了"当前执行表"这一套坐标，而"以某张表为坐标系的索引"实际有 N+1 套（每个持表者/跟随者的 ③ + 每个停放者的 ④），编辑订单时没人枚举全集。

### 14.4 由此修正的表述（替换 §12.5/§13.8 的单线说法）

原表述只说了"漏调 follower 的 ③"。现在应写成**三条隐含不变式 + 各自破在哪**：

| 不变式 | 内容 | 破在哪 |
|---|---|---|
| I-1 | `②==①` 时 ③ 与 ④ 同坐标系，④ 可安全镜像 ③ | 未破（`:6766` 保证） |
| I-2 | `②!=①` 时 ③ 属于②、④ 冻结 | 未破（`:6766` + `:4022` 保证） |
| **I-3** | **任一表被删/插时，所有以该表为坐标系的索引必须同步** | **破成两处**：③ 的候选集合缺 follower（会崩）；④ 根本不在同步范围内（只错） |

### 14.5 新增判据（给内测版取数用，接 §12.6 的 E-1~E-4）

- **E-5**：崩溃前该车（及链上每节）`GetEffectiveOrders() != orders` 是否成立？成立 ⇒ 走②③线，缺口 ③ 型。
- **E-6**：同链各车的 `orders_backup_real_index` 是否 `>= orders_backup->GetNumOrders()`？成立 ⇒ ④ 线也已脏（预期存在、被钳制、不崩，但会解释"解挂跳单"）。
- **E-7**（负判据）：只借出、不解挂、编辑表后归还 —— ④ 若脏，应表现为 `ORD-RETURN-OWN real=<被钳制值>` 而非崩溃。

### 14.6 状态

- 本章纯读码，**未改任何业务代码、未构建**。
- U-389-15（新）：④ 线的钳制点（`train_cmd.cpp:7485`）尚未逐行读；本轮只据 `:4018-4019` 注释与 KI-526 现场（`ORD-RETURN-NOBK real=65535` → `ORD-RETURN-OWN real=2->0`）判定"有钳制、不崩"。

