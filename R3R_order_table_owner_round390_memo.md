# 第 390 轮（2026-11-03）「表类型 1/2 × 进度类型 3/4」全部代码面归位核查

临时报告。姊妹条目：`R3R_KNOWN_ISSUES.md` 的「第 390 轮」小节（KI-557 + 复核记录）。

---

## 一、玩家口径（本轮任务原话）

> 「你的意思是，表类型1和表类型2在表改动的时候，有一个并不是按照原生逻辑来的？所以我们要做的，就是把脱离的那个掰回原生逻辑？如果是这样，那就请你执行修复。还有，如果你可以，请你检查一下这个表类型1，2和进度类型3，4涉及到的所有面是否都已经用原生逻辑摆正？」

判定：**成立**，但要说清是哪一层"脱离原生"。不是"拆表"这件事错，而是**拆表之后仍然沿用原生"共享环 = 持有者全集"的假设**——那一步才是脱离原生的。所以修法是"把集合的算法换掉"，不是"把表合回一张"。

---

## 二、四个东西的坐标系（第 389 轮 §14 已定，本轮沿用）

| 编号 | 实体 | 字段 | 坐标系归属 | 存档 |
|---|---|---|---|---|
| ① | 自己拥有的表 | `Vehicle::orders` | 所有权 | SAVED |
| ② | 当前指向/正在跑的表 | `r3r_follow_orders` + `GetEffectiveOrders()`（`vehicle_base.h:394/405`） | 执行 | 部分 |
| ③ | 自己当前的排程进度 | `cur_real_order_index` / `cur_implicit_order_index` / `cur_timetable_order_index` | **恒属于②** | SAVED |
| ④ | 书签：自己表上的进度 | `orders_backup_real_index` / `orders_backup_implicit_index`（`vehicle_base.h:413-414`） | **恒属于①** | R3VP |

原生 ①≡②，故"共享环成员"就是"全部以该表为坐标系的车"；R3R 拆开 ①② 后环只覆盖 ①，
**跟随者（`orders != list` 且 `r3r_follow_orders == list`）结构上永远不在环里**（`vehicle_base.h:887` 的 `FirstShared()` 只读 ①）。
⇒ 凡是"改表后同步索引"的代码，只要候选集合是环，就漏跟随者；漏调一次 ⇒ ③ == 新表长 ⇒ `GetOrder(n)` 返回 nullptr ⇒ 崩（KI-555）。
④ 因为写入端有两道守卫（`train_cmd.cpp:6766/6768`）+ 归还端钳制，所以只错不崩。

---

## 三、本轮已落地的修复（仅 `src\order_cmd.cpp`，未碰任何 `.h`）

### 3.1 统一原语（`order_cmd.cpp:1738`）

```cpp
template <typename Fn>
static void R3RForEachOrderListHolder(OrderList *list, Fn &&fn)
{
    if (list == nullptr) return;
    for (Vehicle *u : Vehicle::Iterate()) {
        const bool live   = (u->GetEffectiveOrders() == list);
        const bool parked = (u->orders_backup == list);
        if (live || parked) fn(u, live, parked);
    }
}
```

- `live` = ② 指向本表 ⇒ 需要维护 ③；
- `parked` = ④ 指向本表 ⇒ 需要维护 ④（书签插入/删除/重排同步）；
- 因 `r3r_follow_orders` 是裸指针、无反向索引，只能 `Vehicle::Iterate()` 全扫——只在命令级（用户点一次）发生，不进 tick 热路径。

配套两个书签原语：`R3RAdjustBookmarkForInsert`（`:1749`）、`R3RAdjustBookmarkForDelete`（`:1763`），都带越界钳制。

### 3.2 已改的 9 个调用点（`grep R3RForEachOrderListHolder` 实证）

| 行 | 面 | 处理 |
|---|---|---|
| 1855 | `CmdInsertOrderIntl` 主插入 | live→③右移；parked→④右移 |
| 1897 | 同上、`couple_list`/marker 分支 | 同上 |
| 1958 | 同上的第二支（逐条判断 + `ResetDepotUnbunching` + `InvalidateVehicleOrder`） | 同上 |
| 2131 | `CmdDeleteOrder` | live→③左移；parked→④左移+钳制 |
| 2315 | `CmdMoveOrder` | live→`adjust_vehicle`；parked→用同一 `adjust_order_idx` 重映射 ④ |
| 2453 | `CmdReverseOrderList` **Reverse** | live→三个索引映射 + `cur_timetable=INVALID` + `ResetDepotUnbunching` + `InvalidateVehicleOrder(VIWD_REMOVE_ALL_ORDERS)`；parked→映射书签两索引 |
| 2498 | `CmdReverseOrderList` **AppendReversed** | live 刷新 |
| 3327 | `CmdModifyOrder` 装载/卸载标志传播 | 先收集成 `r3r_holders` 向量再原样循环 |
| 3780 | 复制/插入订单 + `CmdMoveOrder` 之后 | live 刷新（dst 的有效表） |
| 3837 | refit 传播 | live：`InvalidateVehicleOrder` + 命中则 `SetRefit` |

### 3.3 顺带修掉的两处"错表"（比漏调更隐蔽）

1. `CmdReverseOrderList` 的 Reverse 分支原来用 `v->FirstShared()` 取环（=①的世界），却反转 `GetEffectiveOrders()`（=②）⇒ **表被反转、索引按另一张表的环维护**。现统一用 `eff_orders` + 原语。
2. 上一轮"修法 D"只补了编辑者本人 `v` 一个（`order_cmd.cpp:2035-2038` 注释自陈），同表**其他跟随者仍漏**；现由原语一次覆盖。

### 3.4 残留检查

`grep -n "shared_ring\|follower\|owned_orders" src\order_cmd.cpp` ⇒ 无实质命中（仅函数内注释与 `FindLiveUserOfOrderList` 的局部变量，与本修法无关）。

`read_lints src\order_cmd.cpp` ⇒ 0 条。

---

## 四、"所有面"核查表（本轮范围：① ② × ③ ④ 交叉）

分类口径：
- **W（写侧）** = 会改表内容或改 ③/④ 的代码 ⇒ **必须**覆盖持有者全集，否则必错（可崩）；
- **R（读侧）** = 只读 ②/③ 做展示/计算 ⇒ 不改数据，但索引越界时会崩，靠钳制防；
- **P（属性传播侧）** = 用环把某个 flag/记录广播给"同排程的车" ⇒ 漏车表现为行为不一致。

| # | 面 | 位置 | 类 | 现状 |
|---|---|---|---|---|
| 1 | 插入订单 | `order_cmd.cpp:1802+` | W | ✅ 已归位（原语） |
| 2 | 删除订单 | `order_cmd.cpp:2086+` | W | ✅ 已归位 |
| 3 | 移动订单 | `order_cmd.cpp:2315` | W | ✅ 已归位 |
| 4 | 反转订单 | `order_cmd.cpp:2453/2498` | W | ✅ 已归位（并修错表） |
| 5 | 修改订单（装载/卸载/refit 传播） | `order_cmd.cpp:3327/3837` | W/P | ✅ 已归位 |
| 6 | 复制排程（含 `CmdMoveOrder` 后） | `order_cmd.cpp:3780` | W | ✅ 已归位 |
| 7 | 读档补 `OT_COUPLE_END` 迁移 | `order_cmd.cpp:1875-1931`（`R3REnsureCoupleEndOrders`，由 `sl\vehicle_sl.cpp:556-563` 调） | W | ✅ 该函数本来就遍历**共享链全员**同步三个索引（第 389 轮 H-389-A 已核），本轮未动 |
| 8 | 行进推进 ③ | `vehicle_base.h:1097/1161-1168` `ProcessOrders` | W | ✅ 原生语义（自己 ++，越界归零），只在"自己的表"上推进；跟随时推进的是借来的表——**这是设计使然**，不改 |
| 9 | 书签 ④ 写入 | `train_cmd.cpp:4022-4026` `R3RParkOwnOrderProgress` | W | ✅ 五道守卫（`orders_backup!=nullptr` / `r3r_follow_orders!=nullptr` / `r3r_orders_borrowed` / `orders==nullptr` / 已非 INVALID）⇒ 从不记录"借来的坐标" |
| 10 | 书签 ④ 归还 | `train_cmd.cpp:7559-7606`、`:8003-8010` | W | ✅ 归还前重验 + 越界落 0 的钳制（`7485` 一带）；本轮只确认，未改 |
| 11 | 书签 ④ 存档 | `sl\couple_group_sl.cpp:132-133`（R3VP）、`sl\vehicle_sl.cpp` 读入钳制 | W | ✅ 越界值在读入时被钳（第 389 轮已核） |
| 12 | 克隆时段身份/进度的照搬 | `couple_group.cpp:1380-1410`（`R3RCopyClonedSegmentTraits`） | W | ⚠️ 直接搬 ④ 两索引，**未校验目标表长度**。克隆品表由 `CloneVehicle` 现造、条数与源一致 ⇒ 当前安全；若将来克隆走到"表已变"的场景需加钳制 |
| 13 | 备份/克隆 `OrderBackup` | `order_backup.cpp:53/245` | W | ❌ **缺口**（见 §五 AUD-390-1） |
| 14 | `CopyConsistPropertiesFrom` 搬 ③ | `base_consist.cpp:34-36` | W | ⚠️ 见 §五 AUD-390-2 |
| 15 | 分离状态计算 | `timetable_cmd.cpp:737` `PopulateSeparationState` | R | ❌ **缺口**（见 §五 AUD-390-3；也是 KI-555 的崩溃面） |
| 16 | 调度派发（ScheduledDispatch / dispatch_records） | `schdispatch_cmd.cpp:50/394/569/848` | P | ❌ **缺口**（见 §五 AUD-390-4） |
| 17 | 旧档分离标志迁移 | `afterload.cpp:3698-3710` | P | ⚠️ 只影响老档迁移（`SLV` 条件块内）；跟随者不在环里 ⇒ 老档迁移漏车 |
| 18 | 展示类（车队列表/出发板/订单窗/时刻表窗/基建费/组统计） | `vehicle_gui.cpp`(8) `departures_gui.cpp`(6) `departures.cpp`(1) `timetable_gui.cpp`(2) `order_gui.cpp`(2) `infrastructure.cpp`(2) `group_cmd.cpp`(2) `station_cmd.cpp`(2) `vehiclelist.cpp`(1) `viewport.cpp`(1) `vehicle.cpp`(11) | R | 只读，不改表；索引越界时靠 #19 钳制兜底。本轮**未逐行复核**，列为待核 |
| 19 | 索引越界兜底 | D-1 `timetable_cmd.cpp:740/746/755`、D-4 `afterload.cpp:325` 前全员钳制 | R | ⏳ 第 389 轮定案、**尚未落地**（本轮未动，属 KI-555 修法清单） |

---

## 五、本轮新发现（编号 AUD-390-x，全部"已读码确认、未改"）

### AUD-390-1（写侧，中）：`OrderBackup` 用"①的环邻"代表"②的表主人"

- `order_backup.cpp:52-53`：`if (v->IsOrderListShared()) this->clone = (v->FirstShared()==v) ? v->NextShared() : v->FirstShared();`
- `order_backup.cpp:243-247`：`ClearVehicle()` 同样取环邻。
- 判据 `IsOrderListShared()`（`vehicle_base.h:834`）看的是 ①；若 `v` 是跟随者（① 空/另一张表，② 借来的表），`clone` 记下的是**错的人**，`Restore` 时拿回错表或用错基表。
- 影响面：卖车/克隆/车库"卖光"路径中的 `OrderBackup`。本轮**未改**（改法应改成"② 的首持有者"，即 `GetEffectiveOrders()->GetFirstSharedVehicle()`，且要在 `Restore` 时重验表仍存活）。
- 归入待办，KI-557 附记 A。

### AUD-390-2（写侧，低）：`CopyConsistPropertiesFrom` 无条件搬 ③

- `base_consist.cpp:34-36` 把 src 的三个索引直接抄给 dst。
- 若 src 的 ③ 属于一张借来的表、dst 拥有自己的表，则 dst 拿到**别人表的坐标**。
- 现有调用点（克隆、`OrderBackup`、`R3RRelocateFrontIdentity`）都另有身份迁移逻辑兜过，故当前**未观察到故障**；但这是一个"无守卫的跨坐标系拷贝"，属隐患。
- KI-557 附记 B。

### AUD-390-3（读侧，高）：`PopulateSeparationState` 的环 + 逐车 ③/② 混用

- `timetable_cmd.cpp:737` 用 `FirstShared()/NextShared()` 取集合；`:740` 读**每节车自己的** `cur_real_order_index`；`:745/:755` 的 `GetOrder()` 走**每节车自己的** `GetEffectiveOrders()`；`:736` 的早退只看 `v_start` 的表非空。
- ⇒ (a) 跟随者不在环里 ⇒ 自动分离状态算不全；(b) 环成员若 ③ 越界 ⇒ `:756` 解引用 nullptr ⇒ 这正是 KI-555 的崩溃点。
- 本轮**不改**：修法清单（D-1 钳制 + D-4 读档全员钳制）已在第 389 轮定案，且属"防崩"而非"归位"；此处只登记为"环≠全集"的读侧同族。
- KI-557 附记 C（并交叉引用 KI-555）。

### AUD-390-4（属性传播侧，中）：调度派发命令按环广播

- `schdispatch_cmd.cpp:50`（`ScheduledDispatch` 开关）、`:394/:569/:848`（`dispatch_records` 增删改）全部 `for (v2 = v->FirstShared(); ...)`。
- 跟随者不在环 ⇒ 同一张排程上的车 flag/记录不同步 ⇒ 派发行为按"谁是表主人"而变。
- 未改；修法同 #1（把环换成 `R3RForEachOrderListHolder` 的 live 分支，或抽出公共 helper）。
- KI-557 附记 D。

### 未确认（U-390-x）

| 编号 | 内容 | 需要什么 |
|---|---|---|
| U-390-1 | 现场是否真的存在"② 表的跟随者 ≥ 2"（本轮修法覆盖的正是这类） | 探针 `E-5`：打 `GetEffectiveOrders() != orders` 的车数与 `r3r_follow_owner` |
| U-390-2 | `OrderBackup` 缺口是否在实际（卖车/克隆）路径被触发 | 在 `OrderBackup` 构造/`Restore` 打 `backup-clone=` 探针 |
| U-390-3 | `CmdReverseOrderList` 修错表之前是否已造成过实际错乱 | 现有存档里"反转过排程的既有车"索引自检 |
| U-390-4 | 展示类 11 个文件里是否还有"环 + ②"混读导致显示错行 | 逐文件读码（本轮未做） |

---

## 六、构建与验证状态

- 本轮只改 `src\order_cmd.cpp`（未碰 `.h`、未碰 `lang\*.txt`）⇒ 增量合规。
- `read_lints src\order_cmd.cpp` = **0 条**。
- **构建自证**（复用既有入口 `R3R_fullbuild.cmd`，未新建任何 `.cmd`）：`R3R_fullbuild.cmd inc detach` → `build\R3R_incbuild.done` 内容 `EXIT_CODE=0`（21:53:36）；`build\openttd.exe` @2026-11-03 21:53:31（52 419 584 B）晚于 `src\order_cmd.cpp` @21:44:58 ⇒ 本次改动已进 exe。
- **未做任何游戏内复测**。⇒ 状态 = 「已实现 + 已编译，游戏内复测待做」，不得记「已修」。

---

## 七、复测判据（编译后）

1. **崩溃线**：同 KI-555 场景（两段式 + 自动时刻表 + 自动分离）删/插订单后不再崩；`PopulateSeparationState` 无 nullptr 解引用。
2. **插入**：对一张被跟随的表插入一条，站在它前面的那条（`at <= idx`）③ +1，站在后面的不变；同表 parked 车的 ④ 同样 +1。
3. **删除**：删第 k 条，`idx > k` 的全部 −1；`idx == 旧表长−1` 的车必须落到 `新表长−1`（不得停在 `新表长`）。
4. **重排/反转**：`CmdMoveOrder` 与 `CmdReverseOrderList` 后，所有 live 车的 ③ 落在同一条订单上（不是"同一下标"）；parked 车的 ④ 亦然。
5. **负判据**：单表（无跟随）列车的行为逐字不变——环成员集合 = 原语返回的 live 集合。
6. **性能**：命令级 `Vehicle::Iterate()` 全扫，主观无卡顿（不进 tick 热路径）。

---

## 八、下一步（按优先级）

1. 编译（`R3R_fullbuild.cmd inc detach`）并按 §七 复测。
2. AUD-390-3 与 KI-555 的 D-1/D-4 同批修（都是"防崩"，且共用同一份钳制判据）。
3. AUD-390-1/2/4 属"归位"的第二批：把 `FirstShared()` 环的三个用法（`OrderBackup`、`schdispatch_cmd`、`afterload` 老档迁移）统一到同一个 helper。
4. §四 #18 的 11 个展示文件逐行复核（读侧，优先级最低）。

---

## 九、玩家逐条理解核对（2026-11-03，读码确认，未改码）

### 9.1 AUD-390-3：方向对一半，但"多算"应改为"漏算"

玩家说法：「分离会算所有和这张表沾边的，所以越界崩溃」。

- **实际是少算，不是多算。** 集合来源 `timetable_cmd.cpp:737` 的 `v_start->FirstShared()`，而 `FirstShared()`（`vehicle_base.h:887`）只读 `orders`（①）⇒ **跟随者结构上不在环里**，整批被漏掉。
- **跟随者永远进不了数组**：`SeparationActive` 由 `UpdateSeparationOrder(v)` 开头 `:787` 只给 v 自己置位；跟随者调到自己时，它的 `FirstShared()` 是它自己那张表（环里只有它一个）→ `:790 size<2` 直接 return。⇒ 它只给自己打勾、从不参与任何一次排序。
- **崩溃与"多算"无关，是"索引本来已坏"**：`:740` 取 `n = cur_real_order_index`，`:745`/`:755` 走 `GetOrder(n)`（`vehicle_base.h:1179` → `GetEffectiveOrders()`），越界或 `INVALID_VEH_ORDER_ID(65535)` 时返回 nullptr，`:746` 的 `order->IsType()` 直接解引用空。⇒ 本函数不是罪魁，是**第一个把 KI-555 坏索引当真用的地方**。
- **玩家说"应当只有自己持有表、且该表是控制表的车"要修一处**：分离要算的是"**谁在跑这张表**"（`GetEffectiveOrders() == 目标表`），不是"谁持有表"。控制段主人是主人，跟随者也在跑这张表，**必须进数组占位**，否则距离算成"前车很远"→ 全部放行 → 扎堆。真正该排除的是"① 指向这张表、但 ② 指向别人表"的车（坐标不在本表上）。⇒ 判据是 `GetEffectiveOrders() ==`，不是 `orders ==`。
- **两件事要分开做**：归位（本条的换集合）+ 防崩（KI-555 的 D-1/D-4：`n` 先夹紧、`GetOrder` 为 nullptr 就 `continue`）。
- **可以复用 KI-557 的原语**：`R3RForEachOrderListHolder` 的 `live = GetEffectiveOrders()==list` 与这里的判据逐字一致，无需另写。

### 9.2 AUD-390-1：理解基本正确，但缺的不止一处（三样）

- 备份侧 `order_backup.cpp:46-67`：`CopyConsistPropertiesFrom(v)`（`:49`）已搬 ③ 与时刻表/调度属性；**共享分支 `:52` 只记 `clone = 环邻`**（`:53`），既不存"我实际跑的表"（②），也不存 `dispatch_schedules`（`:60-63` 只在 else 分支填）。
- 恢复侧 `:96` `v->CopyConsistPropertiesFrom(this)` 确实把 ③ 写回，`:99-101` 还有钳制。
- ⇒ 缺的是三样：① `clone` 选的是 **① 的环邻**（下一个共享者），不是 **② 的表主人**；② 共享分支没存 `dispatch_schedules`（恢复后定时发车配置丢失）；③ 没有"我跑的是哪张表"这个字段，跟随者的备份恢复时被 `clone` 分支改挂到错的表。
- 玩家提的四件里：**自有表（`orders`）+ 自有进度（③）本来就有**；缺的是 **指向表（`r3r_follow_orders`）+ 书签（④ `orders_backup_real_index`）**，外加 clone 选人规则。OrderBackup 构造里没有任何一行写 ④。

### 9.3 AUD-390-4：方向同意，但"只对控制表有效"要改成"对全部执行者同时生效"

- `timetable_cmd.cpp:696` 的 SetSeparation、`schdispatch_cmd.cpp:50/394/569/848` 的 ScheduledDispatch / dispatch_records 都是按环广播。
- 正确口径不是"只改控制表主人"，而是"**改所有 `GetEffectiveOrders() == 这张表` 的车**"，否则同一张表上有的车开分离、有的没开。
- 附带风险：`:693` 的"分离 vs 定时发车互斥"只检查 v 自己，跟随者不检查 ⇒ 可能出现"主人开分离、跟随者开定时发车"的组合。

### 9.4 AUD-390-2：确实与 390-1 同源，但被兜住了

- `base_consist.cpp:34-36` 无条件搬 ③ 三个索引，调用方含 `OrderBackup`（`order_backup.cpp:49`）与克隆路径。
- 备份场景是"想要的"（同坐标系）；跨表场景（源车跑借来的表、目标车有自己的表）才是"把别人卷子上的第 5 题抄到我的第 5 题"。
- 目前被 `order_backup.cpp:99` 的 `UpdateRealOrderIndex()` 钳制兜住 ⇒ 维持"低"。

### 9.5 AUD-390-5：为什么它只在"读老档"时出问题

- `_jokerpp_auto_separation` / `_jokerpp_non_auto_separation` 是 JokerPP 老存档里的**两张表清单**（老版本没有把开关存在车上，而是记"这几张表开了/没开自动分离"）。
- 迁移（`afterload.cpp:3698-3709`）拿清单反查车：`list->GetFirstSharedVehicle()` 沿 **① 的环**走。跟随者不在环（① 不是这张表）⇒ **清单里有这张表、车也确实在跑这张表，但勾没打到它**。
- 后果：读老档后跟随者是"不自动分离"，且开一趟也不会自己补上（390-3 让它永远进不了数组）。
- 范围：只在 `SlXvIsFeaturePresent(XSLFI_JOKERPP)` 且读到那两张清单时发生，新档不受影响。

---

## 十、落地清单（玩家 2026-11-03 拍板：除 390-5 全做）

玩家原话：「除了 AUD-390-5 都可以做，你的方向我认可了」⇒ 实现 390-3 / 390-4 / 390-1 / 390-2，390-5 明确不做（`afterload.cpp` 的 JokerPP 迁移保持原样，仅在本节登记为"已知未修"）。

### 10.1 统一判据（一把尺子）

**"实际执行这张表" = `v->GetEffectiveOrders() == list && v->IsPrimaryVehicle()`**（后者与 `FirstShared()` 环成员的原生范围对齐——环里成员都是列车链头）。

### 10.2 AUD-390-3（高，必做）

`timetable_cmd.cpp:733` `PopulateSeparationState()`：

1. 集合：`v_start->FirstShared()` 环 → 遍历 `Vehicle::Iterate()`，保留 `GetEffectiveOrders() == list` 的车（`list = v_start->GetEffectiveOrders()`）。
2. 前提：`v_start->GetNumOrders() == 0` → `list == nullptr || list->GetNumOrders() == 0`。
3. 防崩（与 KI-555 同批）：
   - `n = v->cur_real_order_index`，若 `n >= list->GetNumOrders()`（含 `INVALID_VEH_ORDER_ID`）⇒ 跳过该车并记 `SEP-SKIP-IDX`；
   - 循环内 `GetOrder(i) == nullptr` ⇒ `vehicle_ok = false` 后 `break`，不再解引用。

### 10.3 AUD-390-4（中）

`timetable_cmd.cpp` 新增文件内模板 `R3RForEachTimetableVehicle(v_start, func)`（语义 = 10.1），替换下列按环广播：

- `:73`（改等待时间后的 lateness 回写）
- `:128`（`current_order` 同步）
- `:348`（Unbunching 重置）
- `:456` / `:468`（lateness 归一）
- `:552`（`StartTimetable` 的 `timetable_all`）
- `:625`（autofill 互斥）
- `:655`（自动时间表）
- `:696`（`SetSeparation` ← 本条主体）
- `:1123`（jam 重启时间表）

`schdispatch_cmd.cpp` 四处同理：`:50`（`CmdSchDispatchSetEnabled`）、`:394`（删/改派发时段后的记录平移）、`:569`（删路线）、`:848`（交换派发时段）。

### 10.4 AUD-390-1（中）

`order_backup.cpp` + `order_backup.h`：

1. 备份侧 `OrderBackup::OrderBackup(const Vehicle *v)`：
   - 共享分支与 else 分支都要存 `dispatch_schedules`（从 `GetEffectiveOrders()` 取），不再只有 else 分支；
   - `clone` 选人从"① 环邻"改成"**②（effective）那张表**上的另一个共享成员"；
   - 新增记录"我跑的是哪张表"（跟随者场景）+ 书签（`orders_backup_real_index` / `orders_backup_implicit_index`）。
2. 恢复侧 `DoRestore()`：写回书签；若备份持有效的跟随目标且仍存活，重建 `r3r_follow_orders/r3r_follow_owner/r3r_orders_borrowed`。

### 10.5 AUD-390-2（低）

`base_consist.cpp:34-36` 的 `CopyConsistPropertiesFrom()` 三个索引改为**同表才搬**：源与目标的 `GetEffectiveOrders()` 不同 ⇒ 不搬 `cur_real/implicit/timetable` 索引（保留目标的），只搬其余属性。

### 10.6 构建口径

改 `order_backup.h` / `base_consist.h`（若需）⇒ 必须全量（`R3R_fullbuild.cmd inc` 的护栏会把 obj 全删，等价全量，约 16~20 分钟）。构建前确认 `openttd.exe` 未运行。

## 十一、落地记录（2026-11-03，AUD-390-1/2/3/4 已改；AUD-390-5 按玩家拍板不做）

玩家原话：「除了 AUD-390-5 都可以做，你的方向我认可了」。⇒ 下表是**实际改成的样子**，与 §十 清单的差异一并注明。

### 11.1 AUD-390-3（`src\timetable_cmd.cpp` · `PopulateSeparationState`）

- 集合：`v_start->FirstShared()` 环 → `Vehicle::Iterate()` + `v->IsPrimaryVehicle() && v->GetEffectiveOrders() == list`（新模板 `R3RForEachTimetableVehicle`，文件头 `#include "safeguards.h"` 之后）。
- 防崩（与换集合同批，缺一则新集合更容易踩越界）：`list == nullptr || list->GetNumOrders() == 0` 提前 return；逐车 `n = v->cur_real_order_index` 后 `if (n < 0 || n >= (int)list->GetNumOrders()) continue;`；`order = list->GetOrder(n)` 与 `GetOrder(v->cur_implicit_order_index)` 均判 `nullptr`。
- 口径不变：仍只统计"同表 + `TimetableStarted` + `ScheduledDispatch` + 该订单是定时发车站点"的车。

### 11.2 AUD-390-4（`src\timetable_cmd.cpp` + `src\schdispatch_cmd.cpp`）

`timetable_cmd.cpp` 换掉的按环广播站点（全部改成 `R3RForEachTimetableVehicle`）：

| 位置 | 原行为 | 现行为 |
| --- | --- | --- |
| `ChangeTimetable()` 迟到量回写 | 环内每车 `lateness_counter -= most_late` | 全表执行者 |
| `ChangeTimetable()` 末尾 | 环内每车把 `current_order` 同步到新 timetable 索引 | 全表执行者 |
| `CmdAutomateTimetable()` | 环内每车改 `VehicleFlag::Automate` | 全表执行者 |
| `CmdStartStopTimetable()` 的 `timetable_all` | `GetFirstSharedVehicle()` 环内收集 `vehs` | 全表执行者 |
| `CmdSetAutoFillTimetable()` | 环内其余车清 `AutoFillTimetable` | 全表执行者 |
| `CmdSetSeparation()` | 互斥检查只看 `v`；广播走环 | 互斥检查扫**整表全员**；广播走全表执行者 |
| `CmdSetVehicleOnTime()` | 环内两趟（求最大迟到量 / 归一） | 全表执行者两趟 |
| 塞车重启时间表（`jam` 分支） | 环内清 `TimetableStarted` + `lateness_counter=0` | 全表执行者 |
| Unbunching 重置 | 环内 `ResetDepotUnbunching()` | 全表执行者（仍在订单循环内，与原生同频次） |

`schdispatch_cmd.cpp` 新增同构模板 `R3RForEachDispatchVehicle`（`safeguards.h` 之后），替换 `:50`、`:394`、`:569`、`:848` 四处；循环体里的 `continue` 相应改成 lambda 的 `return`。

### 11.3 AUD-390-1（`src\order_backup.cpp/.h`）

- `order_backup.h` 新增三个**不入档**字段：`VehicleID follow_owner`、`VehicleOrderID orders_backup_real_index/implicit_index`（书签）。不入档的理由：`BKOR` 块只在联机服务器存档里存在，且借用关系是内存态；硬塞进存档表要动 `XSLFI_*` 特性位与版本，收益不成比例（已在 KI 里登记为已知边界）。
- 备份侧（构造函数）：新分支 `if (v->IsR3RFollower())`，优先级高于原来的 `IsOrderListShared()` / else 两支：
  1. `follow_owner = v->r3r_follow_owner`；若为 `Invalid`，全扫 `Vehicle::Iterate()` 找**同一张 `GetEffectiveOrders()` 的非跟随者**当代表（`CO_SHARE` 拒绝跟随者，所以不能挑跟随者）；
  2. 抄书签 `orders_backup_real_index/implicit_index`；
  3. 仍复制一份订单副本作兜底（控制表在恢复时已死 ⇒ 落回旧行为，不至于让车裸奔）；
  4. `dispatch_schedules` 从 `GetEffectiveOrders()->GetScheduledDispatchScheduleSet()` 取 —— 修掉"跟随者存的是自己那份空/陈旧的派发时段"。
- 恢复侧（`DoRestore()`）：`follow_owner` 有效、车仍存活、是 primary、`GetEffectiveOrders() != nullptr`、且 `owner->tile == v->tile`（`Restore()` 本来就按车库格匹配备份，同格是基本要求）时重建 `r3r_follow_orders/r3r_follow_owner/r3r_orders_borrowed = true` 并写回书签，**跳过**"造自己那张表"的分支；否则完全走原路径。
- **未改** `clone` 的选人（`(v->FirstShared()==v) ? v->NextShared() : v->FirstShared()`）：该分支只在 `v->IsOrderListShared()` 时可达，此时 `v` 不是跟随者，环 ≡ 同一张表（没有跟随者能进环，而进环的就是同表执行者），原写法已经正确；按 §10.4 的"改成 ② 上另一个成员"反而可能挑到跟随者并被 `CO_SHARE` 拒掉。

### 11.4 AUD-390-2（`src\base_consist.cpp`）

`CopyConsistPropertiesFrom()` 的三个索引改为条件拷贝：

```cpp
const Vehicle *const src_vehicle = dynamic_cast<const Vehicle *>(src);
const Vehicle *const dst_vehicle = dynamic_cast<const Vehicle *>(this);
if (src_vehicle == nullptr || dst_vehicle == nullptr ||
        src_vehicle->GetEffectiveOrders() == dst_vehicle->GetEffectiveOrders()) {
    ... 三个索引照旧拷贝 ...
}
```

两侧都是车辆但表不同（夺得/丢掉借来的排程）⇒ 不拷，保留目标自己的索引；任一侧不是车辆（`OrderBackup` 备份与恢复）⇒ 仍无条件拷，故"备份 → 恢复"路径逐字不变。

### 11.5 构建与验证

- 改动了 `src\order_backup.h` ⇒ `R3R_fullbuild.cmd inc detach` 的护栏会把 obj 全删，等价全量。
- **构建自证（2026-11-03 夜）**：`build\R3R_incbuild.done` = `EXIT_CODE=0`（@23:13:31）；日志尾部 `[692/692] Linking CXX executable openttd.exe`（确为全量 692 步，非增量）；`build\openttd.exe` @23:13:09，52 448 256 B；obj 均晚于源文件——`base_consist.cpp` 22:44:21 → obj 23:00:16、`order_backup.cpp` 22:45:24 → obj 23:06:39、`schdispatch_cmd.cpp` 22:41:48 → obj 23:08:00、`timetable_cmd.cpp` 22:41:36 → obj 23:10:11。
- `read_lints`：`base_consist.cpp` / `order_backup.cpp` / `order_backup.h` / `timetable_cmd.cpp` / `schdispatch_cmd.cpp` 均为 0 条。

### 11.6 复测判据（游戏内）

1. 一列车挂上一条"借表"车底（跟随者存在）后，在订单窗口改时间表/开关自动填充/设分离/重设发车时间，**每个跟随者**的显示与行为同步变化（改前只有环内几节变）。
2. 同上的分离（separation）设置：给跟随者所在组开会触发"与新式调度互斥"的报错（改前只看被点的那台车）。
3. 定时派发（`ScheduledDispatch`）开关与派发记录：跟随者刷新一致，删时段/删路线/交换时段后各车记录同步平移。
4. 有跟随者的车在车库被卖掉重建（`Backup`→`Restore`）后，重建出来的车**仍跟随同一张控制表**（订单窗口显示的是控制表的内容，且控制表改动会跟着变），不再出现"私有副本"。
5. 反例：普通共享订单列车（无 R3R 借表）在 1~4 的所有操作上行为**逐字不变**。
6. 反例：`AUD-390-3` 的越界场景（删订单后 `idx == 旧表长−1`）不再崩溃（KI-555 的兜底生效）。
