# R3R「寻找屎山代码」阶段工作准则

> 建立日期：2026-10-20
> 触发：用户宣布进入「寻找屎山代码」阶段，并规定交互方式。

## 一、用户规定的问答方式（必须遵守）

用户提问格式：「**这件事如何实现**」。

AI 回答格式：**只说调用了什么东西 + 这个东西的功能是什么**，不先给结论、不先给评判、不主动改代码。

标准输出骨架：

```
问题：<用户原文>
调用链：
  <入口函数>                    <文件>:<行号>
   └─ <被调函数 1>              <文件>:<行号>   职责：一句话
   └─ <被调函数 2>              <文件>:<行号>   职责：一句话
```

每个被调函数必须写清三件事：

1. **调用点**（文件:行号，绝对定位，不许写「大概在」）
2. **职责**（输入 → 做了什么 → 副作用：改链序 / 改 flags / 写备份字段 / 写订单 / 发命令 / 存档）
3. **是否为真正生效的路径**（死代码、并行双实现、注释与行为不一致必须显式标出——这是屎山主要来源）

## 二、附加默认动作

- 多分支（如多个候选翻转、多条释放路径）要**列全分支 + 各自的触发条件**，不能只讲主路径。
- 涉及 R3R 自有概念（段 ★⊗ / 假引擎 / 假双头 / 控制段 / 借用的排程 / 整格预留）时，必须指出**该概念的权威宿主**（哪个字段、哪张表、哪张稀疏块），以及**有几处在重复维护同一份状态**。
- 刻意区分「存储层」与「镜像层」：谁是原值、谁是副本、谁在读副本、谁负责同步。

## 三、取证纪律（防上下文压缩丢失结论）

1. **先落盘再取证**：有任何疑点/猜测根因，先写进本备忘或对应轮次备忘 + KI 条目，再继续查。
2. **单轮配额 3~5 组工具调用**，每组出来后立刻把结论（含行号锚点 + 原文片段）追加进备忘。
3. **结论只认文件**：上下文丢了就读备忘恢复，不重跑取证。
4. 台账格式：`<文件>:<行号> <函数/探针> — <读数/判定>`，一句话一条。

## 四、本阶段不改代码

除非用户明确要求修复，本阶段只输出「调用链 + 职责 + 屎山判定」。任何判定为屎山的地方，先登记到本备忘的「屎山台账」章节，等用户拍板再动。

## 五、屎山台账（持续追加）

| # | 主题 | 位置 | 现象 | 严重度 | 状态 |
|---|------|------|------|--------|------|
| S-1 | 段特质两套载体 + 三套读法 | vehicle_base.h:375-385 / couple_group.h:546-556 / couple_group.cpp:1101-1200 | 车上 `*_backup`（临时）与段行 `R3RSegmentRecord`（正式）并存，读侧又分主行回退 / 子行不回退 / 直接查行三种 | 中 | 已定位 |
| S-2 | 排程是「别名借用」而非副本 | train_cmd.cpp:11111-11144 | 链头 `v->orders` 直接指向控制段的表，主人保留自己的指针，仅靠 `r3r_orders_borrowed` 区分；`ProcessOrders` 不看该标志 | 高 | 已定位 |
| S-3 | 返还判据靠值比对 | train_cmd.cpp:6337-6366 | 「已在借用」靠 `chain->name == ctrl->name_backup` / `unitnumber != backup` 推断，KI-280/294/297 都是给这套值比对打补丁 | 中 | 已定位 |
| S-4 | 注释与实现不一致 | vehicle_base.h:380 | `name_backup` 注释仍写 NOSAVE，实际第 160 轮已加入 R3VP 稀疏块 | 低 | 已定位 |
| S-7 | 段行的 `orders` 是死字段 | couple_group.cpp:1420（仅判空）/ train_cmd.cpp:5663-5668 | `R3RSegmentRecord::orders` 无任何赋值点，段排程实际取自车上 `orders` / `orders_backup`；与第 152 轮"段行是 formal home"的设计不符 | 中 | 疑点待核 |
| S-8 | 隐藏段的排程只能被隐式写入 | train_cmd.cpp:5820-5870 / 6048-6064 | `R3RAutoRecordChainOrders` 每 tick 往非控制段自己的表尾追加自动条目，无 UI/开关；段拿回表后又跳过不执行，玩家只见"多出来的命令" | 中 | 疑点待拍板 |
| S-6 | 排程是引用语义、号名组是值语义，却共用一个"寄存"概念 | vehicle_base.h:377-382 / order_cmd.cpp:112-114,686 | `orders/orders_backup` 同为指针（表唯一，谁指谁用）；`unitnumber/name/group_id` 的 backup 是值拷贝（多副本，谁是链头谁生效），号另背号池 UseID/ReleaseID | 高 | 已定位 |
| S-5 | 提交点分散 | train_cmd.cpp:5667-5672 / 3174 / 6103 / 6393 | 写行集中在 settle 的 3 个函数，但返还散在 `NormaliseTrainHead`、`R3RBorrowControlTraitsLive`、`R3RSyncChainAfterDepotEdit` 三处，另有 `R3RSyncDrivingOrders` 只搬指针与索引 | 中 | 已定位 |

## 六、调查记录

### 6.1 段特质 / 排程在「两段合并为一条链」时的寄存与返还（2026-10-20 取证完成）

#### 两套载体（先记住这个，后面全靠它）

1. **车上临时停放副本**（`src\vehicle_base.h:375-385`）
   - `UnitID unitnumber_backup`（:379，**SAVED**，在 R3VP 稀疏块）
   - `TinyString name_backup`（:380，注释写 NOSAVE，实际第 160 轮已进 R3VP —— 注释滞后）
   - `GroupID group_id_backup`
   - `OrderList *orders_backup` + `orders_backup_real_index` + `orders_backup_implicit_index`
   - 语义：段失去链头身份时，把自己那一套塞进这里；号**仍占号池位**，不 ReleaseID。
2. **段行 `R3RSegmentRecord`**（`src\couple_group.h:546-556`）
   - 字段：`orders / real_index / implicit_index / timetable_index / orders_borrowed / unitnumber / name / group_id`
   - 按 `r3r_segment_id` 密集索引，`R3RSegmentGet(id)`（`couple_group.cpp:985`）取行。
   - 语义：注释自陈是这三个特质的 **formal home**（正式家）；`orders` 只是指针，表本体仍归 ORDL 与共享链。

#### 寄存（合并发生的那一刻）

```
Couple()                                          train_cmd.cpp
 ├─ 采样 was_borrowing = v->r3r_orders_borrowed          :11131
 ├─ 排程寄存 v->orders_backup = v->orders（仅第一次）    :11132-11135
 │    └─ 已是借用态 ⇒ 只打 ORD-XFER keep，不覆盖          :11136-11142
 ├─ v->orders = couple_owner->orders（**别名，不搬表**）  :11143
 ├─ v->r3r_orders_borrowed = true                        :11144
 ├─ 车号 u->unitnumber_backup = u->unitnumber             :11173
 │    v->unitnumber_backup（仅空时） = v->unitnumber       :11175
 │    v->unitnumber = u->unitnumber; u->unitnumber = 0    :11176-11177
 └─ 车名 u->name_backup = u->name（仅当其非借来）          :11208
      v->name_backup（head_had_own 时）= v->name           :11209
      v->name = u->name（**不清 u 的活名**）                 :11210
```

`R3RParkTrainName()`（:3082）与 `R3RRestoreTrainName()`（:3093）是名字侧的一对；
分组**不在 Couple() 里借**，而是在 `R3RNormaliseChainGroups()` 推平整链分组时由
`R3RParkTrainGroupID()`（:3124-3130）逐段停放，再统一到控制段。

#### 隐性化落地（提交点把「车上副本」收进「段行」）

```
R3RSettleChainSegments(chain, tag)                 train_cmd.cpp:5660-5672
 ├─ R3RBorrowControlTraits(chain, tag)                    :5236
 │    └─ 链头段自己的那一套：backup 优先，借用态禁止回退到活字段  :5247-5264
 ├─ R3RSyncSegmentTraits(chain, tag)                      :5302
 │    └─ 控制段那一套：借用期取链头活字段，否则取控制段段头      :5312-5314
 ├─ R3RSyncHiddenSegmentTraits(chain, tag)                 :5432
 │    ├─ seeds = R3RGetSegmentHeads(chain)，segs.size()<2 ⇒ return   :5436-5437
 │    ├─ 跳过 ctrl（由上面负责）与 chain（由上一函数负责）          :5447-5448
 │    └─ R3RRecoverSegmentTraits(seg)                     :5454
 │         └─ 段内向后扫，把滞留在别的车上的 backup 收回段头       :5373-5390
 └─ R3RCheckControlSegment(chain, tag)                     :5672
```

结论句：**段变隐性后，它的号/名/组不再靠车上的字段保存，而是由段行保存**；车上
`*_backup` 只是搬运途中的临时口袋。这也是"读档只需读段行"的前提（R3SG）。

#### 返还（三处入口，互不覆盖）

```
① NormaliseTrainHead(head)                        train_cmd.cpp:3191
   ├─ head->ConsistChanged(CCF_ARRANGE)                 :3197
   ├─ R3RRestoreTrainGroupID(head)                      :3202（自带 num_vehicle -1/+1 簿记）
   ├─ R3RRestoreTrainName(head)                         :3213
   ├─ R3RRestoreUnitNumber(head)                        :3217（号被别人穿着 ⇒ 延迟，不销毁备份 :3165-3179）
   └─ 真没号 ⇒ 领新号                                  :3224-3225

② R3RBorrowControlTraitsLive(chain, tag)          train_cmd.cpp:6200-6374
   ├─ ctrl == chain ⇒ 直接搬回（6280-6318，号/名/组三块）
   └─ 仍在借用且 ctrl 仍在链上 ⇒ 换上控制段那套（6337-6366）

③ R3RSyncChainAfterDepotEdit(chain)              train_cmd.cpp:6393
   ├─ R3RRenumberPriorities → R3RReelectControlSegment      :6401-6404
   ├─ R3RSyncDrivingOrders(chain, false)                   :6405（定义 :6103，只搬指针+索引）
   ├─ R3RBorrowControlTraitsLive(chain, "depot-edit")       :6410
   ├─ 取回自己的号                                         :6425-6439
   └─ 取回排程：先 orders_backup，否则链上第一个有表的 donor + AddToShared  :6440-6475
```

解耦侧另有：`DecoupleTrain()` 的车号互换块（:8347-8365，含 KI-297 的"陈旧副本才清 0"）、
`R3RSyncDrivingOrders()`（:6103）把 owner 的冻结索引导回链头，以及 KI-180 的口径——
**解挂出来的两半绝不共用一张订单表**（:8090-8100）。

#### 判定

- 排程从来不是"复制一份寄存"，而是**借用别名**：表只有一份，主人保留 `orders` 指针，
  链头只是指向它；`r3r_orders_borrowed` 是唯一凭据，而 `ProcessOrders`/`TrainLocoHandler`
  读表时不看这个标志 ⇒ 谁写表谁就是事实上的主人。
- 返还的正确性依赖**值比对幂等**（`chain->name == ctrl->name_backup` 之类），不是显式状态机；
  KI-280 / KI-294 / KI-297 / KI-300 全是在给这套推断打补丁。
- 一份状态、两套载体、三种读法（主行回退 `R3RSegmentTraitRow` / 子行不回退
  `R3RSegmentSection*` / 直接 `R3RSegmentGet`）、五个提交点 —— 这就是本主题的屎山骨架。

### 6.2 追加：「寄存/返还」到底是引用语义还是值语义（2026-10-20 第二轮）

玩家提问：排程是不是一直放在内存里、只是段不再指向它？特质返还都是"指针挪开再挪回"吗？

**答案：排程是引用语义（指针换手），号/名/组是值语义（值拷贝 + 回填），两者不是一回事。**

证据（类型即判据，`src\vehicle_base.h:377-382`）：

```
OrderList *orders              // 指针
OrderList *orders_backup       // 指针（同类型，仅字段名不同）
UnitID     unitnumber_backup   // 值（uint16 级别的 ID）
TinyString name_backup         // 值对象（字符串内容拷贝，不是共享 buffer）
GroupID    group_id_backup     // 值
```

1. **排程是引用语义**
   - 表本体的家：`OrderListPool _orderlist_pool`（`src\order_cmd.cpp:114`）+ `OrderPool`（:112），
     与车辆池并列；`OrderList::Iterate()` 遍历池，存档由 ORDL 写出。
   - `Couple()` 不复制表、不搬表：`v->orders = couple_owner->orders`（`train_cmd.cpp:11143`），
     原来那张表只剩 `v->orders_backup` 一个指针指着（`:11133`）⇒ **表仍在池里，只是没有车的
     `orders` 指向它**。玩家的说法成立。
   - 返还 = 把备份里的地址写回活指针（`:6448-6450` 车库路径 / `DecoupleTrain` 两半互换），
     不是"搬数据回来"。
2. **但「永远在内存里」有两个反例**
   - `OrderList::FreeChain(false)`（`order_cmd.cpp:686`）：只有 `R3RFindOrderListReferrer()`
     找不到别的引用者时才真正销毁；R3R 的借用不登记共享链，所以这条判据是补丁（KI-93 崩溃现场）。
   - 读档：`R3VP` 只存 `orders_backup` 的 **REF_ORDERLIST 引用**，表本体由 ORDL 写出；
     若表没有任何车引用（孤儿），读档后就是悬垂/失效引用。
3. **号/名/组是值语义**，不是指针
   - `name_backup = name` 是 `TinyString` 赋值 = 字符串内容拷贝；`unitnumber_backup = unitnumber`
     是值拷贝；`group_id_backup = group_id` 是 ID 拷贝。
   - 返还 = 值回填 + 清备份（`R3RRestoreTrainName` :3093 / `R3RRestoreTrainGroupID` :3133），
     原件从未"被挪开"，只是活字段被外来值覆盖过。
4. **号还额外背一套账**：号池 `freeunits[type].UseID/ReleaseID`。停放时**占位不还**
   （`train_cmd.cpp:11495` 冻结语义），返还时补记 `UseID`（`:3186`），确认没人用才 `ReleaseID`
   （`:11165`）。所以号的返还代码远长于名字 —— 它同时要维护"值"和"池位"两本账。

**判定（登记 S-6）**：同一个「段特质」概念下混了两种所有权模型 ——
排程是**共享单实例 + 谁指谁用**（引用计数缺失，靠 `r3r_orders_borrowed` 和扫表兜底）；
号/名/组是**多副本 + 谁是链头谁生效**（无引用计数问题，但有"副本过期"问题）。
"寄存/返还"这个词把两者混为一谈，是这一块最常见的理解陷阱。

### 6.3 非链头/非控制段能不能编辑自己的排程（2026-10-21 取证完成）

**结论：玩家不能（零入口）；系统能（只有自动记录一条路径会写）。**

#### 玩家侧：三层全部关闭

1. **订单窗口不认识"段"**：`src\order_gui.cpp` 全文件对 `r3r_segment` / `R3RSegment` / `r3r_orders_borrowed` **零命中**；
   `OrdersWindow` 以传入车辆作 `this->vehicle`，所有读写都是 `this->vehicle->orders`（`GetNumOrders()` 在
   `vehicle_base.h:868` 只读 `this->orders`）。
2. **命令层只认 primary**：`PreInsertOrderCheck`（`order_cmd.cpp:1229`）、`CmdDeleteOrder`（:1800）、
   `CmdDuplicateOrder`（:1172）、`CmdMoveOrder`（:1991）、`CmdModifyOrder`（:2248）、
   `CmdInsertOrdersFromVehicle`（:3394/:3402）一律 `!v->IsPrimaryVehicle() ⇒ CMD_ERROR`。
3. **隐藏段段头不是 primary**：`IsFrontEngine()` = `IsGroundVehicle() && HasBit(subtype, GVSF_FRONT)`
   （`vehicle_base.h:1173-1175`）；R3R 的"假引擎"只改 `railveh_type`（`SetEngine()`），**不设 `GVSF_FRONT`**
   ⇒ 不进 `IterateFrontOnly()`、不进车辆列表、不能被窗口/命令选中。车队列表子行只显示/选择，
   点击一律映射回链头（第 155 轮 P2b）。

#### 系统侧：确实有"段自己的表"，而且有专用访问器

```
5663:5668:src/train_cmd.cpp
static OrderList *R3RSegmentOwnOrderList(const Train *seg)
{
	if (seg->r3r_orders_borrowed) return seg->orders_backup;   // 借用中 ⇒ 自己的表停在 backup
	if (seg->orders != nullptr) return seg->orders;            // Route A：主人保留自己的指针
	return seg->orders_backup;
}
```

唯一会**写**隐藏段排程的是自动记录（第 257 轮 KI-388/395/396/397）：

- `R3RAutoRecordChainOrders(chain)`（:5820-5870）每 tick 遍历 `R3RGetSegmentHeads(chain)`，
  `if (seg == ctrl) continue;` 只对**非控制段**，用 `R3RSegmentOwnOrderList(seg)` 取它自己的表，
  `ol->InsertOrderAt(...)` 把链头刚驶过的车站/路点/车库追加到该段表尾，并在段行
  `auto_ranges_begin/count/signature[]` 登记区间（`row->auto_ranges_*` 是真正在用的段行字段）。
- `R3RSkipAutoRecordedOrders(v)`（:6048-6064，调用点 :6200 与 :18817）在该段拿回自己的表后
  跳过这些自动条目：`if (own == nullptr || v->orders != own) return 0; /* Still borrowing: not our list. */`
  ⇒ 自动记录只在"不是控制段"时**写**、在"拿回自己表"时**跳过不执行**。

#### 结论与两个疑点

- 编辑权严格跟**链头**走：链头借用别人的表时反而能改（同公司，命令层无守卫），只是跨公司借用被
  KI-132 挡住（`order_cmd.cpp:1234-1240`）；隐藏段连自己的表都碰不到。
- **S-7（疑点）**：`R3RSegmentRecord::orders` 字段**从来不被赋值** —— `couple_group.cpp` 里
  `row->orders` 只有一处判空（:1420 附近），`R3RSegmentStoreTraits` 只写号/名/组；
  `R3RSegmentOwnOrderList` 也是从车上取表。⇒ 第 152 轮设计的"段行存排程"只落了字段没落写入，
  段排程的唯一载体是车上 `orders`/`orders_backup`。
- **S-8（疑点）**：自动记录是**无 UI、无开关**的隐式写入，玩家看不到自己的段表被追加了什么；
  一旦该段成为链头，这些条目会被"跳过不执行"，表现为"排程里多了一条莫名命令"（KI-395/KI-396 已承认
  只跳索引不动时刻表）。是否要给它一个可见入口，属待拍板。

### 6.4 路径预留的铺设与销毁全集（2026-10-21 取证完成）

> 结论先写：**铺设只有 2 个原语（track 级 / trackdir 级）+ 1 个"整平台"特例；销毁有 6 条互相独立的路径
> （自己开走、重建路线、车消失、R3R 链编辑提交点、站台整平台、地图改造），另有 4 条 R3R 自愈兜底。**
> 记账（ledger）与几何（`GetTrainForReservation()` 反查）是两套并行的归属体系，几何式在链合并/翻转后
> 会失去归属，账本式与审计负责兜底。

#### A. 原语层（`src\pbs.cpp`）

铺设：

| 原语 | 位置 | 分派 |
|---|---|---|
| `TryReserveRailTrack(tile, track, trigger_stations)` | :956 | `Railway`+`IsPlainRail` → `TryReserveTrack`（:963-966）；`Railway`+`IsRailDepot` → `SetDepotReservation(true)`（:968-974）；`Road`+`IsLevelCrossing` → `SetCrossingReservation(true)`+`UpdateLevelCrossing`，受 `safer_crossings`/`adjacent_crossings`/`IsCrossingOccupiedByRoadVehicle` 否决（:977-996）；`Station`+`HasStationRail` → `SetRailStationReservation(true)`（整格位，**含路点**）+ 可选车站随机化/动画（:998-1005）；`TunnelBridge` → `SetTunnelReservation`/`TryReserveRailBridgeHead` |
| `TryReserveRailTrackdir(v, tile, td, trigger_stations)` | :900 | 转调 track 级；R3R 补丁：KI-361 同链已订位视为成功、KI-370 必须用 `IsPlainRailTile()` 判类型 |
| `SetRailStationPlatformReservation(start, dir, bool)` | :845 | **沿站台轴逐格**设置整格位的唯一批量入口 |

销毁：

| 原语 | 位置 | 说明 |
|---|---|---|
| `UnreserveRailTrack(tile, track)` | :1058 | 与铺设 6 分支一一对应；入口 `GetReservedTrackbits(tile) != TRACK_BIT_NONE` 时打 `UNRES-RAIL ... phase= actor=` |
| `UnreserveRailTrackdir(tile, td)` | :1037 | 先把对面 PBS 信号置红 + `MarkSingleSignalDirty`，再降级到 track 级 |
| `UnreserveBridgeTunnelTile` / `UnreserveAcrossRailTunnelBridge` | — | 隧道桥跨越位 |

#### B. 世界层：谁在铺

1. **列车脚下常驻**：`Train::ReserveTrackUnderConsist()`（`train_cmd.cpp:17123`），phase=`consist`；
   覆盖 `TRACK_BIT_WORMHOLE`（隧道桥/自定义桥头 :17130-17142）、普通轨（:17143-17190，`TRACK_BIT_NONE`
   时回退 `GetTrackBits` 补平台轨），并把每个成功位记进 `_r3r_consist_resv` 账本（:17168-17174）。
2. **PBS 路线**：`TryPathReserve` → `ChooseTrainTrack`（`:15257`）→ YAPF；预留落在
   `yapf_rail.cpp` 的 `rp()`：`reserve_if_present`（track 级，:1039-1042）、`reserve_dir_if_present`
   （trackdir 级，:1052-1055，注释说明必须 trackdir 才能把对面 PBS 信号置绿 KI-349）、站台逐格补预留（:1079-1084）。
3. **安全停车位**：`TryReserveSafeTrack`（`:14620`）→ `YapfTrainFindNearestSafeTile`；无路可走时兜底订脚下位（:15553）。
4. **整条平台**：`SetRailStationPlatformReservation(..., true)`，调用点 `train_cmd.cpp:4889`（进站前）、
   `station_cmd.cpp:1563`（进站）、`station_cmd.cpp:1828`（车站重建）。
5. **R3R 提交点补订**：解挂后 `u->ClearReservationUnderConsist(); u->ReserveTrackUnderConsist();`（:9496-9497）；
   等待车底首次静止 `R3RReleaseChainReservations(consist,"waiter")`+`lookahead.reset()`+`ReserveTrackUnderConsist()`（:19517-19521）。
6. **读档**：`afterload.cpp:2519-2523`（`< SLV_101`）对每列 `ReserveTrackUnderConsist()`；
   `:3527-3531` 清 blocked 站台的残留位。

#### C. 世界层：谁在销毁（6 条独立路径）

1. **列车自己开走**：`ClearPathReservation`（`:14051`）——最后一节车离开该格时调用
   （`FreeTrainTrackReservation` 内 :14232；移动时 :18434）。站台分支特殊：只有"下一格不是同一站台"
   才 `SetRailStationPlatformReservation(..., false)`（:14106-14113），停在站台中段不清。
2. **重建路线前清旧路**：`FreeTrainTrackReservation`（`:14126`）—— 先
   `R3RResvLedgerForget(consist,"free-path")`（:14137，账本），再沿 `CFollowTrackRail` 前向逐格
   `ClearPathReservation`；遇普通信号、红灯 PBS、占用中的隧道桥即 break（:14202-14228）。
   调用点极多：`ChooseTrainTrack` 的每个失败分支（:15504/:15539/:15559/:15605/:15615）、
   到达/换向（:4711）、`Crash`（:17236）、`DeleteVisibleTrain`（:20732）、以及 `rail_cmd.cpp`
   拆轨/转辙/改轨型/拆站（:1029/:1076/:1158/:1488/:1793/:1960/:2436/:2655/:2956/:3171）。
3. **列车消失/卖车/撞车**：`Train::ClearReservationUnderConsist`（:17201，账本 `"cleared"` + 逐节
   `ClearPathReservation`）、`Train::Crash`（:17236-17238）、`DeleteVisibleTrain`（:20732 + `FreeTrainStationPlatformReservation` :20738）。
4. **R3R 链编辑提交点**：`R3RReleaseChainReservations(head, why)`（:11593），两遍式（先收集后释放，
   避免边扫边放切断后面位的归属），站台/路点按整格位处理；`why` 四个：`"decouple"`（:9488/:9489）、
   `"couple"`（:11916）、`"couple-settled"`（:12023）、`"waiter"`（:19517）。
   并行账本版：`R3RResvLedgerForget(owner, why)`（`pbs.cpp:557`，mask 见 :267-270，`couple`/`free-path`/`flip`/`depot-edit` 只放 PATH、保留车下 CONSIST 位）。
5. **站台整平台**：`FreeTrainStationPlatformReservation`（`station_cmd.cpp:1527`，头尾双向清）
   + `FreeTrainReservation`（:1546 = 路径 + 平台）；车站重建/拆除与删车走这条。
6. **地图/存档改造**：`rail_cmd`/`tunnelbridge_cmd`/`station_cmd` 的 `FreeTrainReservation*`；
   读档 `< SLV_100` 全图清（`afterload.cpp:2482-2517`，分 Railway/Depot/Crossing/Station/TunnelBridge 五类）。

#### D. R3R 自愈与兜底（会真实释放，但不属"正常销毁"）

1. `R3RResvSweepSlice()`（`train_cmd.cpp:16985`）——每 tick 只扫一片地图，把 ownerless 位记账待 reap。
2. `R3RReservationAudit(tag)`（:16650）——稳态 2048 tick / 脏标记（`_r3r_resv_audit_due`，:3559、
   `R3RMarkReservationAuditDue()` 由 `CmdMoveRailVehicle`/`CmdSellRailWagon` 置位）时 256 tick 一次；
   按 owner 判定，连续 `R3R_RESV_REAP_AUDITS=3` 次 stray 才 `RESV-REAP` 释放。
3. `R3RPurgeStrayReservations()`（`pbs.cpp:2552`）+ 控制台 `r3r_resv_purge`（`console_cmds.cpp:4494`）——
   手动清 plain rail 与站台/路点整格；**刻意不动 depot / crossing**（那是"有车在内"的保护位）。
4. `R3RGcConsistReservations()` + `R3RReconcileConsistReservation(...,"moved")`（:17196-17197）——
   consist 账本里"车已离开的格"走 `R3RReleaseReservationSafe`，四道校验（tile 类型/`IsPlainRailTile`/
   仍有位/确有此轨）后才改地图。

#### E. 三条必须记住的结构事实

- **铺设按 `HasStationRail`，释放按 `IsRailStationTile`**：前者含路点，后者不含
  （`pbs.cpp:2409-2415` 注释自陈）⇒ 路点整格位只能靠 R3R 的整格分支/审计清，引擎自己永远清不掉。
- **两套归属**：几何式 `GetTrainForReservation` / `GetTrainForWholeTileReservation`（`pbs.h:316/:324`）
  在链合并、翻转、同格双车后会给错答案；账本式按 `VehicleID` 记账，是 KI-363 之后的权威。
- **`FreeTrainTrackReservation` 只往前走向**（:14126 注释 :11553-11559）：车开过之后"身后"的旧路径它
  永远走不到 ⇒ 这正是 `R3RReleaseChainReservations` 与审计存在的理由，也是玩家看到的"幽灵预留"来源。
