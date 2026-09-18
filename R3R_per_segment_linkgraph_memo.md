# R3R 按段链路刷新 + 按段运费结算（第 32~33 轮）

## 0. 玩家问题与拍板
场景：A 地一列车去 C、B 地一列车去 C，在 C 连挂后共同去 D。问：客货流怎么算？
第 31 轮结论（KI-70）：CargoDist 是逐跳站对模型，不认「连挂」；连挂只通过三件事影响货运——
链头 orders（决定整列能接哪些货）、整列容量（决定该腿上报的运力）、谁在跑这条腿（决定边会不会衰减消失）。

玩家拍板：**选方案 A「按段刷新」**，且补充语义：
1. 段被物理拖着实际跑了 C→D，就算作它会跑 C→D（不看它自己的排程里有没有 D）；
2. 运费结算也按段进行。

## 1. 现状（第 31 轮已核实）
| 环节 | 落点 |
|---|---|
| 订单预测建边 | `LinkRefresher::Run`（`linkgraph/refresh.cpp`），容量扫描 `for (v = this->vehicle; v; v = v->Next())` 覆盖**整条链**，但只在链头被调用 |
| 实测建边 | `VehicleIncreaseStats`（`vehicle.cpp:3393`，由 `Vehicle::BeginLoading` 调）逐车按 `refit_cap`/实载上报，边= `front->last_loading_station → front->last_station_visited` |
| 触发点 | `economy.cpp:2388`（装载未完成时）、`vehicle.cpp:3625`（装卸结束路径），均传链头 |
| 衰减 | `LinkGraph::Compress()` 每 `COMPRESSION_INTERVAL` 把 capacity/usage 折半 → 没人跑的边消失 |
| 装货挑货 | `front->GetNextStoppingStation()`（`economy.cpp:1542`/`1995`）→ 恒取链头 |
| 排程交接 | `Couple()`：`v->orders = couple_owner->orders`，`v->r3r_orders_borrowed = true`；**命令归属段（`couple_owner`）自己的 `orders` 指针被保留** |

## 2. 本轮实现：按段刷新（已落地，待实测）
### 2.1 设计
`LinkRefresher` 原来只有一个 `vehicle`，同时扮演两个角色：**路线来源**（`orders`/`cur_implicit_order_index`/`GetDisplayMaxSpeed`/`last_station_visited`…）与**容量范围起点**。本轮把两者拆开：

- `vehicle`：路线来源，恒为**链头**（链头的排程就是整链实际要走的路线）。
- `scope_begin` / `scope_end`：容量范围（含端点）。`scope_end == nullptr` 表示一直到链尾。
- 新增 `LinkRefresher::NextInScope(v)`，把构造器与 `HandleRefit` 里两处容量遍历改为按 scope 走。

新增入口 `LinkRefresher::RunPerSegment(front, ...)`，两遍：

- **Pass 1（物理腿，整链一次）**：用**链头的路线**刷新一次，容量算**整条链的所有车**。
  ⇒ 这正是玩家语义 (1)：链上每个段都被物理拖着跑这条路线，所以每个段的车都该在这条腿上占运力。
  这正是旧的整链刷新，**普通列车与连挂链的容量数值完全不变**（无回归）。
  ⚠ 关键约束：`IncreaseStats` 走的是 `EdgeUpdateMode::Refresh`——它是**取最大值（保底）**而不是累加（`linkgraph.cpp` 的 `IncreaseEdgeCapacity` 注释：refreshing keeps a minimum capacity）。所以 Pass 1 **必须整链一次**：若按段分别刷同一条腿，只会留下容量最大的那一段，其余段的运力被丢掉（3 段 ×50 会从 150 掉到 50）。
- **Pass 2（本段自己的计划腿）**：逐段扫描；若该段自己持有 `orders` 且与链头的 `orders` 不是同一份，则用它自己的路线再刷一次，容量限本段。
  ⇒ 保住「段被挂着期间，它自己的来路（例 A→C）继续被登记而不衰减」（KI-70 ②）。

单段列车/非火车（公路、船、飞机）：Pass 1 == 旧 `Run`，Pass 2 不触发（`seg->orders == front->orders`），行为与旧代码逐位等价。

### 2.2 改动清单
| 文件 | 改动 |
|---|---|
| `src/linkgraph/refresh.h` | 加 `RunPerSegment` 声明、`scope_begin`/`scope_end` 成员、构造器两个带默认值的新参、`NextInScope()`；`Run` 保持原签名 |
| `src/linkgraph/refresh.h` | 另加 `RunScoped` 声明（Pass 1/2 共用的实现体，类静态成员：`HopSet`/`RefreshFlags` 是类内类型，不能做成自由函数） |
| `src/linkgraph/refresh.cpp` | 原 `Run` 主体提取为类静态成员 `LinkRefresher::RunScoped(route_v, scope_begin, scope_end, ...)`；`Run` 变成一行转发（`scope_begin = v`、`scope_end = nullptr`，行为不变）；新增 `RunPerSegment`；构造器/`HandleRefit` 容量遍历改走 scope；新增 `#include "../train.h"`（用 `Train::IsSegmentFront()` 判段边界） |
| `src/economy.cpp:2388` | `LinkRefresher::Run(front, true, true)` → `RunPerSegment(front, true, true)` |
| `src/vehicle.cpp:3625` | `LinkRefresher::Run(this, ...)` → `RunPerSegment(this, ...)` |

段边界判定沿用既有机制：链头、或带 `SegmentFront`★ 的车（`w->type == VehicleType::Train && Train::From(w)->IsSegmentFront()`）。

### 2.3 验证方法（待实测）
1. **回归**：普通（无★）列车跑线，观测量应无变化——用 `linkgraph` 调试窗口或 `R3R_AB_probe` 对比同场景 A/B。
2. **场景**：A→C 与 B→C 在 C 连挂后去 D，连挂后**持续数日不回家**：
   - 期望：A/B 的货仍被派往 C（A、B 站不积压）；C→D 有货。
   - 旧行为（KI-70②）：A→C 边衰减消失，A 站积压、评分下降。
3. **日志**：`linkgraph` 窗口看 A→C、C→D 的 capacity 是否在连挂期间维持。
4. **多段**：3~5 段长链跑一圈，看帧率（`R3R_perf.log`）与装卸耗时（刷新成本随段数线性上升）。

## 3. 本轮实现：按段运费结算（KI-71，已落地，待实测）
目标：A 段拿 A→C 那一段运费、B 段（或实际拉 C→D 的段）拿 C→D 那一段，解挂后各段自己的账目能看出自己挣了多少。

### 3.1 付款结构
- 装/卸时为整链建一个 `CargoPayment`（挂在前车上），`PayTransfer`（`cargopacket.cpp` `Stage` 内）逐包算中转费、`PayFinalDelivery`（`cargoaction.cpp` `CargoDelivery` 内）逐包算交付费，各自累加到 `visual_transfer`/`visual_profit`（+ `route_profit`）；
- `~CargoPayment()` 统一 `front->profit_this_year += (visual_profit + visual_transfer) << 8` 并 `SubtractMoneyFromCompany(route_profit)`。

### 3.2 实现（低风险版，已落地）
| 文件 | 改动 |
|---|---|
| `src/economy_base.h` | `CargoPayment` 增 3 个 NOSAVE 字段：`VehicleID r3r_recipient`（当前收款段头，`Invalid()` = 默认归链头）、`Money r3r_recipient_base`（开 scope 时的 `visual_profit + visual_transfer`）、`Money r3r_booked`（已分给各段的总额）；加 `R3RSetPaymentRecipient(Vehicle *v)` 声明 |
| `src/economy.cpp` | 新增文件内静态 `R3RGetSegmentHeadOf(v)`（沿 `Previous()` 回溯，直到链头或 `Train::IsSegmentFront()`；非火车即自身）；实现 `R3RSetPaymentRecipient`；`~CargoPayment()` 改为「先结清最后一段，再把 `(visual_profit+visual_transfer)<<8 - r3r_booked` 记到链头」 |
| `src/economy.cpp:1549` 附近 | `v->cargo.Stage(...)` 前后包 `R3RSetPaymentRecipient(v)` / `R3RSetPaymentRecipient(nullptr)`（中转费） |
| `src/economy.cpp:2149` 附近 | `v->cargo.Unload(...)` 前后同样包裹（交付费） |

要点：
1. **立即入账**，不延迟到 `~CargoPayment`：每次 scope 结束时把该车贡献的差值 `(visual_profit+visual_transfer) - base` 直接加到 **该车所属段的段头** `profit_this_year += share << 8`，并累进 `r3r_booked`。这样不需要保存段头指针/表，规避了「付款对象活到 `LeaveStation`、期间车辆被卖掉」导致的指针失效与 index 复用错账；段头若已不存在（`Vehicle::GetIfValid` 返回空）则这一份留在链头。
2. **总量守恒**：`~CargoPayment` 只把「总额 - 已分出去的部分」记给链头 ⇒ 整链账目和 `(visual_profit+visual_transfer)<<8` 完全一致，比旧代码不多不少。
3. **口径用 `visual_*` 差值而不是 `route_profit` 差值**（与原设计草案不同）：`PayTransfer` 只加 `visual_transfer`、不动 `route_profit`，若按 `route_profit` 记账，中转费会被算到「下一个被记账的段」头上（错账）。用 `visual_*` 差值与真实入账量同源。
4. **不动 `route_profit`、不动公司总收入**：存档经济零漂移。
5. 默认路径（无★普通列车、链上每辆车都属于链头那一段）→ `head == front`，一律不开 scope，行为与旧代码逐位等价。

### 3.3 已知副作用/风险（需玩家实测确认）
- 连挂列车的**链头显示利润会下降**（原本全记在链头，现在分给各段；各段自己的账在解挂后单独可见）。这是玩家要的效果，但车辆窗口/车辆列表的外观会变。
- 收入动画（`ShowFeederIncomeAnimation` 等）仍按整链一个数字播放，与「分段入账」的口径不一致（动画不会拆）。
- `profit_last_year`/公司年度统计口径未动：公司总收入不变，但「车均利润」会随分段而变。
- 身份迁移（`R3RRelocateFrontIdentity` / `CopyVehicleConfigAndStatistics` 会搬 profit）与解挂归还路径未复核——已分给段头的利润不会再被搬走（立即入账，不走 `front`），但换端重排那一瞬间的付款边界未实测。
- 早退分支 `~CargoPayment()` 里 `CleaningPool()` 直接返回 ⇒ 存档加载清理时不做分账（此时也不该做）。

## 4. 本轮未做的开放点（见 KI-72）
- 挂车方**自留计划 `orders_backup`** 的归属段未映射：Pass 2 只覆盖「段自己持有且与链头不同」的 `orders`；`front->orders_backup`（例：A 段的 A→C + GOTO_COUPLE）当前**不会**被按段刷新。
  **归属结论（本轮查清）**：`R3RSyncDrivingOrders` 里 `chain->orders_backup = chain->orders`（只在「开始借用」时写入，即 owner != chain 时把**链头自己那份**计划存到链头上）⇒ `orders_backup` 恒属于**链头所在的那一段**，要接线就是「Pass 2 里对链头那一段额外用 `orders_backup` 刷一遍、容量限该段」。本轮未做（改动要动 `RunScoped` 的路线来源参数），登记为 KI-72 ①。
- 刷新成本：Pass 1 = 1 次订单遍历（旧行为），Pass 2 = 「持有自己计划的段数」次。段数多时装卸瞬间开销线性上升，未实测帧率影响。
- Pass 1 已改为整链一次，故「同一 (from,to) 被多个段分别刷新导致容量被 max 掉」的问题不再存在（该问题已在实现阶段被识别并规避）。

## 5. 第 33 轮：Release 全量重编核验 + 交付给玩家的实测清单

### 5.1 构建核验（已完成）

| 项 | 值 |
|---|---|
| 构建 | `build-release\`（RelWithDebInfo；`FLAGS = /Zi /O2 /Ob2 /DNDEBUG -std:c++20 -MT`，探针默认 OFF） |
| 方式 | **第 1 轮**：`d:\sourcecode of JGRPP\R3R_release_build.cmd` → **删全部 `*.obj` 全量重编**，691 步。**第 2 轮**（修掉 KI-73 之后，改动只有 `.cpp`）：**增量重编**（重编 `train_cmd.cpp.obj` + LTCG 链接，`[3/3] Linking`，约 6 分钟） |
| 结果 | 两轮均 `NINJA_EXIT=0` / `BUILD FINISHED rc=0` / `EXIT_CODE=0`；完成标记 `build-release\R3R_release_build.done`（第 2 轮写入） |
| 产物 | **交付实测版 = `build-release\openttd.exe` @ 2026-09-17 04:15:07，22 735 360 B（第 2 轮，含 KI-73 修复）**。第 1 轮的对照产物（代码等价但兜底路径析构不执行）存档为 `build-release\openttd_r33_full.exe` @ 04:08:50 + `build-release\R3R_release_build_r33_full.log`；第 2 轮日志 `build-release\R3R_release_build.log` |
| KI-15 合规核验 | 623 个 `*.obj` 最早写入 **03:34:56**（第 1 轮全量起点），最新源码头文件 `src\linkgraph\refresh.h` @ **03:26:12** ⇒ `STALE_OBJ_COUNT=0`，**无陈旧 / 混合对象**（实测的 exe 就是改过的源码） |
| 依赖闸门 | `[5c]` 通过：`build.ninja` 的 `DEFINES` 含 `WITH_ZLIB / WITH_LIBLZMA / WITH_ZSTD / WITH_LZO / WITH_PNG / WITH_OPUSFILE` ⇒ **无 KI-25 退化**（不会报 "loader for 'zstd' is not available"）；脚本尾部 DLL 检查为 `ok, not linked: ucrtbased.dll / vcruntime140d.dll / msvcp140d.dll` ⇒ 未混入 Debug CRT |
| 触发全量重编的原因 | 本轮改了 **两个头文件**（`src/linkgraph/refresh.h`、`src/economy_base.h`）⇒ 按 KI-15 / KI-16 必须全量 |
| 警告 | 只有 `src/lang/simplified_chinese.txt` 的模板参数 `info` 级警告（既有，非本轮引入）+ 一条 `C4150`（见 5.2，已当轮修掉） |

### 5.2 重编日志暴露并当轮修掉的缺陷（新 KI-73）

`R3R_release_build.log` 第 820 行：`train_cmd.cpp(4474): warning C4150: 删除指向不完整 CargoPayment 类型的指针；没有调用析构函数`。

- 落点是 **KI-69（第 31 轮）**的兜底分支：`delete v->cargo_payment; // ~CargoPayment 会把 v->cargo_payment 置空`。
- `train_cmd.cpp` 只（间接）含 `economy_type.h`、**没有** `economy_base.h` ⇒ `CargoPayment` 是不完整类型 ⇒ **析构不会跑**。
- 后果：①`front->cargo_payment` **不会置空**（悬垂）；②`CargoPayment` 池槽不经 `PoolItem::operator delete → Tpool->FreeItem()` 归还（池泄漏）；③**析构里的资金结算完全不执行**（`SubtractMoneyFromCompany(route_profit)` 与 `profit_this_year` 入账）⇒ 该次装卸运费不扣不记。
- 修法：`train_cmd.cpp` 顶部 `#include "economy_base.h"`（附注释说明为何必须引入完整类型；仅 `.cpp` ⇒ 增量重编，不触发 KI-15）。已在**第 33 轮**修掉并增量重编（`openttd.exe` @ **04:15:07**，`NINJA_EXIT=0`）。
- **取证提醒**：`openttd_r33_full.exe`（04:08:50）是修复**之前**的全量产物，只可作对照；验证 §5.3 E 组第 9 项必须用 `build-release\openttd.exe`（04:15:07）。

### 5.3 玩家实测清单（按优先级，逐项打勾）

**A. 零回归（最重要，先做）**
1. 用**不含任何 ★ 段标记的普通列车**跑几条线，打开 linkgraph 调试窗口 / 用 `R3R_AB_probe` A/B 对比：各边 capacity、usage、公司收入与改动前应**完全一致**（Pass 2 不触发 ⇒ 逐位等价）。
2. 公路 / 船 / 飞机跑一趟：观测量无变化（`RunPerSegment` 对非火车等价于旧 `Run`）。

**B. 按段刷新场景（KI-70 核心）**
3. 摆场景：A 站一列车 → C，B 站一列车 → C，两车在 C 挂接后共同去 D。连挂后**持续跑很多天不回家**。
   - 期望（新）：A→C、B→C 两条边的 capacity 在连挂期间**维持住**，A、B 站不积压，货源仍派往 C；C→D 有货。
   - 旧行为（对照）：A→C 边在 `Compress()` 下逐轮折半直至消失 ⇒ A 站积压、站评分下降。
4. 解挂后再看：A→C 边应随该段重新跑车而**恢复/保持**，不出现「解挂后上游边已消失、要重新养”。

**C. 按段运费结算（KI-71）**
5. 同一 A/B/C/D 场景跑几个循环后**解挂**，逐段点开车辆窗口看「本年利润」：被挂段（跑过 C→D 的那一段）应有自己的一份，链头不再是全部。
6. 核对公司现金/总收入：与改动前**同场景同循环数**对比，收支总量应一致（只是分摊口径变了）。
7. 观察副作用：连挂期间链头显示利润比过去低（钱分给了各段）；收入动画仍是整链一个数字（预期，不拆）。

**D. 段数与帧率（KI-72 ②）**
8. 用 3~5 段长链跑一圈，看 `R3R_perf.log`（Release 默认关探针，可用 `R3R_PERF=1` 打开）里装卸瞬间耗时与整体帧率：刷新成本 = Pass 1（1 次）+ Pass 2（持有自己计划的段数）。若发现装卸瞬间卡顿，把段数记下来反馈。

**E. KI-73 的兜底分支**
9. 复现 KI-69 现场：**装货途中在站台解挂**（`R3R_debug.log` 应出现 `SETTLE-LOAD site=decouple-*`）。确认：不崩溃、`front->cargo_payment` 之后的窗口/装货不再报 `economy.cpp:1535` 断言、该次装卸的钱正常进出。

### 5.4 仍待拍板
- KI-72 ①：要不要把 **`front->orders_backup`（链头那一段的自留计划）**也纳入 Pass 2 按段刷新。归属已查清（`orders_backup` 恒属链头那一段），接线需要把 `RunScoped` 的路线来源从 `route_v->orders` 改成可传入 `const OrderList *`。
