# R3R 第 388 轮：`PopulateSeparationState` 空指针崩溃取证备忘

> 状态：**取证完成（崩溃点与调用栈已实证）/ 根因未定（两条猜测，待验证）/ 未改任何源码 / 未构建**
> 输入：朋友测试 R3R v1.0.4 产生的崩溃日志
> 日期：2026-11-03

---

## 一、输入与版本

| 项 | 值 |
|---|---|
| 崩溃日志 | `C:\Users\冯洁敏\Downloads\crash-20261102T120318Z.log`（607 行 / 60 013 B，文件时间 2026-11-03 03:44:33） |
| 读入存档 | `test2.sav`（`Saving the map failed` 前无异常，日志末尾正常写出 NewGRF 列表 410 项） |
| 引擎版本 | OpenTTD 0.73.1（JGRPP） |
| R3R 标记 | `r3r-pre-ledger-removal-2026-10-25-m` |
| 构建时间 | `Nov  1 2026 01:01:09` |
| 探针开关 | `R3R_PROBES_DEFAULT=0`（**发布版，无 `R3R_debug.log`**） |
| git 版本串 | `3b87101b00eaf337d0d483c6c425acda28f48da34` |
| 异常码 | `C0000005`（ACCESS_VIOLATION / 读） |

**版本一致性已确认**：工作区 `d:\sourcecode of JGRPP\build-release\openttd.exe`（23 317 504 B，2026-11-01 01:26:36）内嵌同一版本串、同一构建时间 ⇒ 崩溃的就是这棵树的产物，同目录 `openttd.pdb` 可用于符号反查。

---

## 二、取证方法（可复现）

临时工具：`d:\sourcecode of JGRPP\build\r3r_crashmap.ps1`（纯 ASCII，避免 PS 5.1 按 ANSI 读 UTF-8 的乱码问题），输出落盘 `build\r3r_crashmap_out.txt`。

三步：

1. **定基址**：崩溃指令字节序列取自反汇编现场
   `0F B6 6F 0C 40 0F B6 DD 80 E3 0F 40 0F B6 F5 40 C0 EE 04 80 FB 0F 74 05`
   在 exe 内搜索 → 唯一命中，文件偏移 `0xC7FF84` → RVA `0xC80B84`；用 `RIP - RVA` 反推模块基址 `0x7FF7F1D00000`（0x1000 对齐校验通过）。
   注：PE `ImageBase = 0x140000000`，现场因 ASLR 重定位到 `0x7FF7F1D00000`。
2. **解析 pdb**：`dbghelp.dll` 的 `SymInitialize` / `SymLoadModuleEx`（`BaseOfDll=0`，取映像首选基址 `0x140000000`）/ `SymFromAddr` / `SymGetLineFromAddr64`。
3. **逐帧换算**：栈上每个绝对地址 `- 崩溃基址 + 映像基址` 后查符号与行号。

---

## 三、完整调用栈（已实证，含源码行）

| # | 绝对地址 | RVA | 符号 | 源码位置 |
|---|---|---|---|---|
| 00 | `0x7FF7F2980B84` | `0xC80B84` | `PopulateSeparationState+0x2224` | `src\timetable_cmd.cpp:756` ← **崩溃点** |
| 01 | `0x7FF7F2980E80` | `0xC80E80` | `UpdateSeparationOrder+0x20` | `src\timetable_cmd.cpp:790` |
| 02 | `0x7FF7F298179B` | `0xC8179B` | `UpdateVehicleTimetable+0x6DB` | `src\timetable_cmd.cpp:1206` |
| 03 | `0x7FF7F2ABED1D` | `0xDBED1D` | `Vehicle::BeginLoading+0xFD` | `src\vehicle.cpp:3568` |
| 04 | `0x7FF7F2A62854` | `0xD62854` | `TrainEnterStation+0x444` | `src\train_cmd.cpp:19318` |
| 05 | `0x7FF7F2A5FA46` | `0xD5FA46` | `TrainController+0x3E6` | `src\train_cmd.cpp:20524` |
| 06 | `0x7FF7F2A640DF` | `0xD640DF` | `TrainLocoHandler+0x169F` | `src\train_cmd.cpp:23262` |
| 07 | `0x7FF7F2A5EB8E` | `0xD5EB8E` | `Train::Tick+0xBE` | `src\train_cmd.cpp:23393` |
| 08 | `0x7FF7F2ABFAE0` | `0xDBFAE0` | `CallVehicleTicks+0x630` | `src\vehicle.cpp:1723` |
| 09 | `0x7FF7F2703E3C` | `0xA03E3C` | `StateGameLoop+0x29C` | `src\openttd.cpp:1618` |
| 10 | `0x7FF7F270178D` | `0xA0178D` | `GameLoop+0x2ED` | `src\openttd.cpp:1888` |
| 11 | `0x7FF7F232C5C4` | `0x62C5C4` | `VideoDriver::GameLoop+0x94` | `src\video\video_driver.cpp:43` |
| 12 | `0x7FF7F232C898` | `0x62C898` | `VideoDriver::GameThreadThunk+0x58` | `src\video\video_driver.cpp:102` |
| 13 | `0x7FF7F232BCAB` | `0x62BCAB` | `std::thread::_Invoke<...>` | MSVC `thread:60` |
| 14 | `0x7FF7F2D638A3` | `0x10638A3` | `thread_start<...>` | UCRT `thread.cpp:97` |

**读法**：列车到站 → `BeginLoading` → 更新时间表 → 更新分离（separation）状态 → 崩溃。**与 KI-552（公路车 `FollowPreviousRoadVehicle`）无关，是全新条目。**

---

## 四、崩溃点、现场约束与指令

### 4.1 崩溃源码

```cpp
// src\timetable_cmd.cpp（735-760 附近，PopulateSeparationState）
for (const Vehicle *v = v_start->FirstShared(); v != nullptr; v = v->NextShared()) {
    if (!HasBit(v->vehicle_flags, VehicleFlag::SeparationActive)) continue;
    const int n = v->cur_real_order_index;
    ...                                   // 744 起：for (i = 0; i < n; i++) { const Order *order = v->GetOrder(i); if (order->IsType(...)) ... }
    const Order *order = v->GetOrder(n);  // 755
    if (order->IsType(OT_CONDITIONAL)) continue;   // 756 ← 崩在这里
```

`order == nullptr`，`order->IsType()` 读 `[rdi + 0xC]`（`rdi = 0`）触发 `C0000005`。

### 4.2 为什么是 nullptr（`GetOrder` 的两条 null 返回路径）

`Vehicle::GetOrder(index)` = `GetEffectiveOrders()->GetOrderAt(index)`：

- `GetEffectiveOrders()` 为 `nullptr` → 直接 null；
- `OrderList::GetOrderAt()` 有边界检查，越界返回 `nullptr`。

两条都属于"订单表视图与索引不一致"。

### 4.3 **关键现场约束（推断强度：高）**

崩溃落在 **756**（`GetOrder(n)` 的结果被解引用），而**没有**落在 744 循环体内的 `order->IsType(...)` 上。由代码结构可反推：

- 若 `n` 越界且 `n > 表长`，则 744 循环里 `i` 递增到 `表长` 时 `GetOrder(i)` 已经返回 `nullptr`，崩溃会落在循环体内（更早的行号）⇒ **矛盾**；
- 若 `cur_real_order_index == 0xFFFF`（`INVALID_VEH_ORDER_ID`，转 int 为 65535），同样会在循环内崩 ⇒ **排除**；
- ⇒ 唯一自洽结论：**`n == v->GetNumOrders()`**（把 `GetEffectiveOrders()==nullptr` 视作长度 0），即**索引恰好停在有效表尾之后的那一位**，前缀 `0..n-1` 全部有效。

进一步分类（两种都能满足上式）：

- **(a) `n == 表长 > 0`**：索引越界**一条**，这是"跑到表尾没有回绕"的典型形态；
- **(b) `n == 0` 且 `GetEffectiveOrders() == nullptr`**：这辆车当前**没有任何有效订单表**（`orders` 与 `r3r_follow_orders` 均为空）。

注意 733 行附近的早退只检查了 `v_start->GetNumOrders() == 0`，**没有**逐个检查共享链上其他车 ⇒ (b) 在"链上某车被摘走订单表"时可达。

### 4.4 崩溃点代码的位置特征

`PopulateSeparationState+0x2224`（寄存器 RVA 差值折算 ≈ 8.7 KB）说明该函数被编译器大量内联（`GetOrder`/`GetOrderAt`/`GetNumOrders` 全部展开），因此**不能**据偏移判断具体语句，必须依赖 pdb 行号（已取得）。

---

## 五、崩溃车辆现场标志解读

```
veh: 2: (Train 1 (DF4B-2001), c:0, st:FE, vs:Ds, vf:Tzsa, vcf:zd, gvf:, tf:rHSZ,
         trk: 0x02, [tile: 216CB (1739 x 66), type: 50 (Station), ...])
```

| 字段 | 值 | 含义 |
|---|---|---|
| `vf:` | `T z s a` | **`z` = `SeparationActive`**（这正是进入 `PopulateSeparationState` 的分支条件） |
| `tf:` | `r H S Z` | `H` = `ArticGroupHead`、`S` = `SegmentFront`、`Z` = `SegmentFlipped` |
| `gvf:` | 空 | 无分组/无 `GOINGUP/DOWN` 等 |
| `st:` | `FE` | `Stopped` + `FrontEngine` |
| `tile` | `1739,66`（Station） | 正在站台上装载 |

**结论**：崩溃车是一节**被 R3R 改造过的段链头**（段首 ★ + 段翻转标记 + 铰接组头），分离功能开启，正在站台 `BeginLoading`。这条链经历过 R3R 的段身份/翻转/组角色处理，**不是普通原生列车**。

---

## 六、时间关联（Command Log，可作旁证）

日志的 `Command Log` 段（最近 256 / 共 937 条）中，`1920-03-16` 当天有连续 26 条（序号 5~30）**全部指向 vehicle/order `2`**（即崩溃车）：

```
 5 | CmdPause                       | 0, false
 6 | CmdModifyOrder                 | 2, 6, 3, 4, 0
 7 | CmdModifyOrder                 | 2, 6, 2, 1, 0
 8 | CmdModifyOrder                 | 2, 15, 3, 4, 0
 9 | CmdModifyOrder                 | 2, 15, 2, 1, 0
10 | CmdInsertOrder                 | 2, 17, order: t: F, f: 1, d: 0, rc: FE --> 17
12~23 | CmdInsertOrder              | 2, 16 / 15 / 14 / 13 / 11 / 10 / 9 / 8 / 7 / 6 / 5 ...
24~27 | CmdSkipToOrder              | 2, 4 / 2, 3 / 2, 2 / 2, 1
28 | CmdInsertOrder                 | 2, 4, ...
29 | CmdInsertOrder                 | 2, 2, ...
30 | CmdDeleteOrder                 | 2, 2
```

即崩溃车在同一天被**插入约 17 条订单、连续 4 次跳过订单、再删除订单 2**。序号 0~4 是之后的操作（拆信号 ×4、改设置、暂停）。

**判定**：时间上"订单表被大幅重排"先于崩溃，属**强旁证但非因果证明**（日志精度只到游戏日）。可确定的是：崩溃发生在**订单表刚被编辑过的车辆**上。

---

## 七、猜测根因（待验证）

### REG-388-A：借用表与索引脱钩（**首要怀疑**）

`GetEffectiveOrders()` 是 R3R 引入的：

```cpp
const OrderList *GetEffectiveOrders() const { return this->r3r_follow_orders != nullptr ? this->r3r_follow_orders : this->orders; }
```

若在某一时刻 `r3r_follow_orders` 被清空（解耦/归还/解挂/段重排），而 `cur_real_order_index` 仍保留**借用表的坐标**，而车自己的表更短 ⇒ 索引越界一条（正好命中 4.3 的形态 (a)）。

支撑证据：
- 崩溃车是段链头（`tf:SZ`），R3R 的借用链正是以段链头为主体；
- 库里已有大量"索引跨表搬运"的强制赋值点（`src\train_cmd.cpp` 中 `cur_real_order_index =` 至少 50 余处，含 7405 写回 owner、7965/8221/8251 借用与取回、10069 解挂等待点、11588/11628/13116 等），**并非每一处都有边界检查**（13118 有，其它多数没有）；
- 已知历史条目 KI-527（AUTO-ORD 把索引写成占位 0）、KI-353（`ORD-RETURN` 落 0）属同族"索引与表不匹配"。

**证伪判据**：探针版复现时，崩溃前能打出一行 `cur_real_order_index >= GetNumOrders()` 的场合，且该车 `r3r_follow_orders` 刚被清空。

### REG-388-B：订单编辑命令在段/借用链上未归一索引（**并列怀疑**）

第六节的命令序列（插入 17 条 + 连续 skip + 删除）会走 `CmdInsertOrder` / `CmdDeleteOrder` / `CmdSkipToOrder` 对 `cur_real_order_index` 的 ±1 调整；R3R 对段链/共享链的索引同步点若与命令路径不同步，可能把索引推到 `表长`。

**证伪判据**：探针在三个命令的 `Execute` 末尾打印 `before/after` 索引与表长，若出现 `after == num_orders` 即成立。

> 两条不互斥，可能同时成立；4.3 的形态 (b)（表视图为 null）另需一条独立通路（订单表被摘走而未补 follow 指针），暂记为 **REG-388-C**。

---

## 八、修法建议（未实施）

### D-1 防御性（可立即落地，风险极低）

`src\timetable_cmd.cpp` 两处空指针入口：

```cpp
for (int i = 0; i < n; i++) {
    const Order *order = v->GetOrder(i);
    if (order == nullptr) { vehicle_ok = false; break; }   // 新增
    ...
}
const Order *order = v->GetOrder(n);
if (order == nullptr) continue;                            // 新增（或 n = 0 后重取）
if (order->IsType(OT_CONDITIONAL)) continue;
```

更稳妥的写法是在取 `n` 之后立刻钳制：

```cpp
const int n = (v->GetNumOrders() > 0 && v->cur_real_order_index < v->GetNumOrders()) ? v->cur_real_order_index : 0;
```

这属于"让上游代码在索引不一致时不再崩"，**不掩盖**问题（仍可加只读探针记录一次）。

### D-2 治根（需复现日志支撑）

新增统一原语 `R3RClampRealOrderIndex(Vehicle *v)`：`GetNumOrders()==0` 或 `cur_real_order_index >= GetNumOrders()` ⇒ 归 0（同时同步 `cur_implicit/timetable`），并在全部写 `cur_real_order_index` 的提交点末尾调用一次。落点初步清单（来自 `cur_real_order_index =` 搜索结果）：`train_cmd.cpp:7405 / 7965 / 8221 / 8251 / 10069 / 11588 / 11628 / 13116`，以及 `vehicle_base.h` 内 `IncrementRealOrderIndex` 的调用方。

> 注意：**不能**只做 D-2 而不做 D-1 —— 崩溃是"用户看到游戏直接退出"，防御层必须先挡住。

---

## 九、未确认项

| 编号 | 内容 | 需要什么 |
|---|---|---|
| U-388-1 | 崩溃车 `veh 2` 到底是共享链上的 `v_start` 还是链上其他车（决定是 (a) 还是 (b)） | 探针版日志（`PopulateSeparationState` 入口打 `veh/real/num/eff_null`） |
| U-388-2 | `n` 的确切值与他自己的表长 | 同上 |
| U-388-3 | 那次 `CmdDeleteOrder 2, 2` 与 `CmdSkipToOrder` 之后的索引快照 | 命令层探针 |
| U-388-4 | `veh 2` 的 `SegmentFront/SegmentFlipped/ArticGroupHead` 是否由 R3R 段编辑产生（还是读档带入） | 存档 / 段行 dump |
| U-388-5 | 该存档的 `r3r_follow_orders` 关系（是否有借用） | 探针 |
| U-388-6 | 同源崩溃是否会在原生 JGRPP 上发生（若否，则责任在 R3R 的索引搬运） | 静态对比即可（上游 `PopulateSeparationState` 无 R3R 改动，但上游索引不变量成立） |

---

## 十、复现契约与复测判据

**复现契约**（给朋友的最小要求）：

1. 使用**探针内测版**（`build\openttd.exe`，`R3R_PROBES_DEFAULT=1`）重跑同一存档；
2. 复现后取 `build\R3R_debug.log` **尾部 3000 行** + 新的 `crash-*.log`；
3. 若能，额外在崩溃前打开列车 2 的订单窗口截一张图（订单条数用于交叉验证表长）。

**复测判据（D-1 落地后）**：

1. 同一存档不再崩溃（游戏可持续运行 ≥ 10 分钟）；
2. 该车不再进 `PopulateSeparationState` 的 null 分支（若加了只读探针，应看到一次记录）；
3. 分离功能（间距显示/自动时刻表）行为不变 —— 负判据：正常列车到站装载时 `UpdateSeparationOrder` 仍逐 tick 正常执行；
4. 无 `assert` / `dbg_assert` 新增触发。

---

## 十一、本轮产物

| 文件 | 说明 |
|---|---|
| `R3R_separation_order_null_crash_round388_memo.md` | 本备忘 |
| `build\r3r_crashmap.ps1` | 临时符号反查脚本（可复用，纯 ASCII） |
| `build\r3r_crashmap_out.txt` | 反查输出（调用栈原文） |
| `build\r388_facts.txt` | 现场标志与 git 状态摘录 |
| `R3R_KNOWN_ISSUES.md` | 新增 KI-555 |

---

## 十二、玩家新证据（2026-11-03）：崩溃时全图未执行过任何 R3R 行为

**玩家原话要点**：①朋友报告触发该 bug 时**全图还没有执行过任何一次 R3R 行为**；②玩家自己初步怀疑——「**自动时刻表和时刻表自动分离在对于"仅供参考"的调度命令处理时有问题**」。

### 12.1 这条证据对既有结论的影响

| 既有推断 | 新状态 |
|---|---|
| **REG-388-A**（`r3r_follow_orders` 被清空、索引仍是借用表坐标） | **前提消失，降级为"暂撤"**。若全图从未执行 R3R 行为，`r3r_follow_orders` 恒为 `nullptr`（只有 `Couple()`/`DecoupleTrain()`/`R3RGetControlSegment` 相关路径才写它）⇒ 借用层从未参与，A 不可能成立 |
| 形态 **(b)** 的"借用表被清空"分支 | 同步排除；但形态 (b) 的**原生分支仍在**（见 12.3） |
| 形态 **(a)** `n == GetNumOrders() > 0` | **升为首要**：它不需要任何 R3R 运行期行为，只需要订单表内容 + 索引搬运错位 |
| 现场标志 `tf:H/S/Z`（段身份） | 解释改为"**随存档带入 / 由订单编辑命令写入**"，而不是"本轮 R3R 行为产生"。R3R 的段标记与空气区间配对在读档期就会跑：`src\sl\vehicle_sl.cpp:556-563` 的 `if (part_of_load) { R3REnsureCoupleEndOrders(t); R3RRebuildCouplePriorities(t); }` |

> 结论句：**崩溃不需要任何"挂车/解挂"动作，只需要「订单表里有 R3R 的仅供参考区间」+「自动时刻表/分离」。** 这与玩家的怀疑方向一致，也解释了为什么"全新的图、什么都没干"也会崩。

### 12.2 「仅供参考」的调度命令 = KI-541 的空气区间（air range）

代码里不是"仅供参考"字样，而是 `（参考结束）`：

- `src\lang\simplified_chinese.txt:4893`：`STR_ORDER_R3R_COUPLE_END : （参考结束）`
- `src\order_gui.cpp:990-994`（注释原文）：*"An air range is a span the player wrote between an `OT_GOTO_COUPLE` and the `OT_COUPLE_END` marker behind it. It is a reference for the driver, never a command: the execution side jumps over it (`R3RSkipCoupleAirRanges()`) and the order window draws it greyed out and suffixed"*
- `src\order_gui.cpp:1616`：区间内的行追加 `STR_ORDER_R3R_AUTO_FILLED`（灰字后缀）
- `src\train_cmd.cpp:6859-6863`：`OT_GOTO_COUPLE` 与 `OT_COUPLE_END` 之间是玩家手写的占位命令，**"never executed"**
- 结构由 `InsertOrder()` 自动维护：`src\order_cmd.cpp:1727-1736` 插入 `OT_GOTO_COUPLE` 时自动在 `sel_ord+1` 插一条 `OT_COUPLE_END`；`src\order_cmd.cpp:1875-1910` 的 `R3REnsureCoupleEndOrders()` 在移动/删除/重排后修复配对

**⇒ 玩家只要在订单列表里加一条「挂接」命令，就会自动获得一条"仅供参考"的区间结构**，不需要真的去挂车。

### 12.3 已核实的代码事实（本次取证，带行号）

**(1) `PopulateSeparationState` 全文（`src\timetable_cmd.cpp:733-783`，逐字）**

```cpp
std::vector<TimetableProgress> out;
if (v_start->GetNumOrders() == 0) return out;                     // 736：只保护 v_start
for (const Vehicle *v = v_start->FirstShared(); v != nullptr; v = v->NextShared()) {   // 737
    if (!v->vehicle_flags.Test(VehicleFlag::SeparationActive)) continue;              // 738
    bool separation_valid = true;
    const int n = v->cur_real_order_index;                        // 740
    int cumulative_ticks = 0;
    bool vehicle_ok = true;
    int order_count = n * 2;
    for (int i = 0; i < n; i++) {                                 // 744
        const Order *order = v->GetOrder(i);                      // 745（无判空，但未崩）
        if (order->IsType(OT_CONDITIONAL)) { vehicle_ok = false; break; }
        if (!IsOrderUsableForSeparation(order)) separation_valid = false;
        cumulative_ticks += order->GetTravelTime() + order->GetWaitTime();
    }
    if (!vehicle_ok) continue;
    const Order *order = v->GetOrder(n);                          // 755
    if (order->IsType(OT_CONDITIONAL)) continue;                  // 756 ← 崩
    ...
    if (order->IsSlotCounterOrder() || order->IsType(OT_DUMMY) || order->IsType(OT_LABEL)) { // 762
        separation_valid = false;
    }
```

**762 行白名单不含 `OT_COUPLE_END` / `OT_GOTO_COUPLE` / `OT_DECOUPLE` / `OT_WAIT_COUPLE`** ⇒ 空气区间的开/闭标记会被当成正常订单参与分离计算（`order_count`、`cumulative_ticks` 都算进去）。

**(2) `R3RSkipCoupleAirRanges`（`src\train_cmd.cpp:6877-6934`）——唯一搬动索引的"跳过"实现**

```cpp
OrderList *const orders = v->GetEffectiveOrders();
if (orders == nullptr) return 0;
const uint16_t num = (uint16_t)orders->GetNumOrders();
if (num == 0) return 0;
for (uint16_t guard = 0; guard < num; guard++) {
    if (v->cur_real_order_index == INVALID_VEH_ORDER_ID) break;
    const uint16_t idx = (uint16_t)v->cur_real_order_index;
    if (idx >= num) break;                                        // 6890：有上界保护
    if (!R3RIsCoupleAirOrder(orders, (VehicleOrderID)idx)) break;
    ... 找 OT_COUPLE_END ...
    const VehicleOrderID target = (VehicleOrderID)((end_idx + 1) % num);   // 6911：取模，恒 < num
    v->cur_real_order_index = target;                             // 6917
    if (v->cur_implicit_order_index != INVALID_VEH_ORDER_ID &&
            R3RIsCoupleAirOrder(orders, v->cur_implicit_order_index)) {
        v->cur_implicit_order_index = target;                     // 6920
    }
    ...
}
if (v->cur_timetable_order_index != INVALID_VEH_ORDER_ID) v->cur_timetable_order_index = v->cur_real_order_index;  // 6934
```

**结论句**：这个函数**不会**写出 `num`（`(end_idx+1)%num` 且有 `idx >= num` 早退）⇒ 单靠它不能产生 `n == num`。但它是**唯一把 `cur_timetable_order_index` 无条件对齐到 `cur_real_order_index`** 的地方，因此是"自动时刻表在空气区间上写时间"的必经之路。

**(3) `R3RIsCoupleAirOrder`（`src\order_base.h:1853-1870`）的判定口径**

```cpp
if (self->IsType(OT_COUPLE_END)) return true;                     // 闭合标记自己也算 air
for (uint step = 1; step < num; step++) {
    const Order *const o = list->GetOrderAt((VehicleOrderID)((index + num - step) % num));
    if (o == nullptr) return false;
    if (o->IsType(OT_GOTO_COUPLE)) return true;                   // 向后找到开口 ⇒ 在区间内
    if (o->IsType(OT_COUPLE_END)) return false;
}
```
指向**表首**的订单若其"前一条"（环回后是表尾）是 `OT_GOTO_COUPLE`，则表首那段也算 air ⇒ **区间可以跨越表尾→表首的环回点**。此时 `target=(end_idx+1)%num` 可能落在很小的下标，`if (target <= idx) break;` 会提前收手，但索引已改。

**(4) 时间表侧的入口（`src\timetable_cmd.cpp:1180-1188`，`UpdateVehicleTimetable` 尾部）**

```cpp
} else if (v->vehicle_flags.Test(VehicleFlag::TimetableSeparation) && v->vehicle_flags.Test(VehicleFlag::TimetableStarted)) {
    v->current_order_time = time_taken;
    v->current_loading_time = time_loading;
    UpdateSeparationOrder(v);                                     // 1183 → 789 PopulateSeparationState
    v->current_order_time = 0;
    v->current_loading_time = 0;
}
```
同函数 1158-1167：`AutomateTimetable` 打开时对未定时订单**直接写时间**（`ChangeTimetable(..., MTF_TRAVEL_TIME/WAIT_TIME, true)`），索引用的是 `v->cur_timetable_order_index`。

> 时序：`UpdateVehicleTimetable` 先写时间（1155/1162/1165），**后**调 `UpdateSeparationOrder`（1183）⇒ 若 `cur_timetable_order_index` 与 `cur_real_order_index` 在空气区间上不同步，分离算出的 `cumulative_ticks` 就建立在被污染的时间上。

### 12.4 新猜测根因 REG-388-D（**待验证**，玩家方向）

> **REG-388-D**：空气区间使"订单表长度"与"实际可执行订单序列"分离；`OT_COUPLE_END` 只是一个标记，但它占了一个真实下标。自动时刻表（`AutomateTimetable`）与自动分离（`TimetableSeparation`）都按**物理下标**遍历（`for i in 0..n`、`GetTotalDuration()`），于是：
> - 分离的 `cumulative_ticks` 把永不被执行的占位命令时长算进"累计运行时间"（注释 `train_cmd.cpp:6871-6873` 承认此取舍 = KI-396），
> - 而 `cur_real_order_index` 被 `R3RSkipCoupleAirRanges` 在 tick 中间"搬过"整段，若这次搬动发生在 `UpdateVehicleTimetable` **内部**（先取 `n`、后被搬），`n` 与 `GetNumOrders()` 的关系就可能在某一瞬间不等。

**证伪/证实判据**：
- 证实：探针版复现时，崩溃前一刻出现 `COUPLE-AIR-SKIP veh=2 from=… to=… end=… num=…`，且 `n` 等于该行的 `num`（或 `from` 是表尾）。
- 证伪：日志中 `veh 2` 从未出现 `COUPLE-AIR-SKIP`，且其订单表内无 `OT_COUPLE_END`。

### 12.5 还必须解释的一件事：`n` 到底怎么变成 `num`

`R3RSkipCoupleAirRanges` 有上界保护，所以 `n == num` 需要另一个写入点。**本轮只列出候选，尚未定案**（避免在没有日志的情况下再猜一轮）：

| 候选 | 位置 | 可行性 |
|---|---|---|
| D-1 | `OrderList::SkipToOrder` / `CmdSkipToOrder`（`order_cmd.cpp`）跳到"表尾之后"表达"结束当前循环" | 需查实现是否有 `<= num` 语义 |
| D-2 | `Vehicle::IncrementRealOrderIndex()`（`vehicle_base.h`）在 `num==0` 或 `orders==nullptr` 时的行为 | 已知 `num==0` 时会走到 `cur_real_order_index = 0`，不一定越界 |
| D-3 | `R3REnsureCoupleEndOrders()` 在**表尾**插入 `OT_COUPLE_END`（`order_cmd.cpp:1906`），若此刻 `cur_real_order_index` 恰好指向旧表尾之后的空位 | 插入是按位搬索引的，需查 `InsertOrderAt` 的索引调整是否含"索引==num 时不搬"的分支 |

### 12.6 与 KI-555 的关系

KI-555 的描述与严重度不变（崩溃点已实证、高）。**新增内容**：玩家证据使 REG-388-A 暂撤、REG-388-D 新增为并列怀疑，且"未执行任何 R3R 行为"把嫌疑整体推向**订单表内容与索引搬运**（KI-541 空气区间 + 自动时刻表/分离），而不是运行期的段/借用状态。

---

## 十三、索引锁绕过唯一钳制点（2026-11-03 续查，REG-388-E 基本做实）

### 13.1 三处 R3R 锁的逐字代码（`src\vehicle_base.h`，本轮 `read_file` 实测行号）

```cpp
// :1084-1105  SkipToNextRealOrderIndex()
if (this->type == VehicleType::Train && this->current_order.IsType(OT_GOTO_COUPLE)) return;  // :1092 锁
if (this->GetNumManualOrders() > 0) {
    do { this->cur_real_order_index++;
         if (this->cur_real_order_index >= this->GetNumOrders()) this->cur_real_order_index = 0; // :1098 回绕
    } while (this->GetOrder(this->cur_real_order_index)->IsType(OT_IMPLICIT));
    this->cur_timetable_order_index = this->cur_real_order_index;                                  // :1100
}
```

```cpp
// :1113-1132  IncrementImplicitOrderIndex()
if (this->type == VehicleType::Train && this->current_order.IsType(OT_GOTO_COUPLE)) return;  // :1116 锁
...
assert(this->cur_real_order_index == 0 || this->cur_real_order_index < this->GetNumOrders()); // :1123 断言
```

```cpp
// :1155-1172  UpdateRealOrderIndex()
if (this->type == VehicleType::Train && this->current_order.IsType(OT_GOTO_COUPLE)) return;  // :1158 锁（先）
/* Make sure the index is valid */
if (this->cur_real_order_index >= this->GetNumOrders()) this->cur_real_order_index = 0;       // :1161 钳制（后）
```

**结论句**：`vehicle_base.h` 里**只有 `:1161` 一处**会把越界的 `cur_real_order_index` 拉回 0；而 `UpdateRealOrderIndex()` 又是"订单表被缩短之后"唯一被调用的索引修复入口。`:1158` 的锁排在钳制**之前**并直接 `return` ⇒ **`current_order == OT_GOTO_COUPLE` 时钳制整段被跳过**。`:1123` 的断言同样在 `:1116` 锁之后 ⇒ 同一个锁把断言也绕过了（内测版复现时**不应期待 assert 先炸**）。

### 13.2 完整可复现链路（REG-388-E，**零 R3R 运行期动作**）

1. 列车订单表**表尾就是 `OT_GOTO_COUPLE`**，`cur_real_order_index = num-1`，`current_order = OT_GOTO_COUPLE`（机车在等挂车）。注意：目标车底不存在时该状态会**永久保持**（KI-193 报的就是这件事，表现为 `COUPLE-FAIL`/`COUPLE-DEST-EMPTY` 刷屏而不推进）。
2. 玩家删掉这条命令 —— 现场日志里正好有 `CmdDeleteOrder 2,2`。
3. `order_cmd.cpp:2033` 先 `DeleteOrder(v, sel_ord)` 真删（表长 num → num-1）。
4. `order_cmd.cpp:2043-2054` 的 `adjust_indices`：`if (sel_ord == u->cur_real_order_index) u->UpdateRealOrderIndex();` → 撞上 `:1158` 锁 → **no-op**。
5. 删掉的正是开口 `OT_GOTO_COUPLE` ⇒ `R3REnsureCoupleEndOrders()`（`order_cmd.cpp:2116`）无开ロ可配对、不会补条目 ⇒ 表长停在 num-1。
6. 于是 `cur_real_order_index == GetNumOrders()` 长期成立（唯一能修它的钳制被锁死）。
7. 下一次 `UpdateSeparationOrder()` → `PopulateSeparationState()`：`n = v->cur_real_order_index`（== num）；`for (i = 0; i < n; ++i)` 全部合法不越界；**`:755` `GetOrder(n)` 返回 nullptr，`:756` 解引用 ⇒ 崩**。

**与崩溃行吻合**：`timetable_cmd.cpp:756` 只可能由"索引 == num"或"num == 0"两种形态到达（若索引 > num 会先死在 `:746` 的循环体）。本条给出前者，且**不需要任何 R3R 运行期行为**——与玩家新证据"全图没执行过 R3R 行为"完全一致。

**与 12.5 候选表的关系**：12.5 的 D-1/D-2/D-3 全部降级；真凶是"锁绕过钳制"，D-3（`R3REnsureCoupleEndOrders` 插表尾搬索引）已核对 `:1912-1914` 有 `+1 < GetNumOrders()` 保护、不会写出 num。

### 13.3 对玩家"仅供参考"怀疑的判定（部分成立，但非崩溃最近因）

- 空气区间**本身不产生**崩溃：`R3RSkipCoupleAirRanges`（`train_cmd.cpp:6890` 起）有 `idx >= num` 早退、`target = (end_idx + 1) % num` ⇒ 不会写出 num。
- 但空气区间与自动时刻表/自动分离确实有三处缺陷（**非崩溃，行为错**）：
  a. `IsOrderUsableForSeparation()`（`timetable_cmd.cpp:719-731`）对"没有时间"的订单返回 true ⇒ 永不执行的 `OT_COUPLE_END` 被当成合法分隔点；`:762` 的不可分离白名单只有 `IsSlotCounterOrder / OT_DUMMY / OT_LABEL`，**不含** `OT_COUPLE_END / OT_GOTO_COUPLE / OT_DECOUPLE / OT_WAIT_COUPLE`。
  b. `PopulateSeparationState()` 用 `order_count = n * 2`、`cumulative_ticks` 按**物理下标**算相对位置 ⇒ 表里夹一段永不执行的占位命令，区间两侧车辆的"序号差"被人为拉大，分离距离整体偏移（= KI-396 的已知取舍被放大）。
  c. `AutomateTimetable` 侧在 `UpdateVehicleTimetable`（`timetable_cmd.cpp:1158-1167`）按 `cur_timetable_order_index` 写时间；而 `R3RSkipCoupleAirRanges` 末尾**无条件** `cur_timetable_order_index = cur_real_order_index`（`train_cmd.cpp`，§12.3(2) 已引）⇒ 一旦 real 索引越界，时刻表索引同步越界，`GetOrder(...)` 空指针还有第二处入口。
- 因此：交界处确有缺陷，但**崩在 `:756` 的最近因是 13.2 的索引锁 + 删除**，不是区间核算。

### 13.4 具体修法建议（待拍板，本轮未动源码）

- **D-1（防御，最小，只改 .cpp 可增量）** `timetable_cmd.cpp` 的 `PopulateSeparationState()`：取 `n` 之后夹到 `[0, num)`；`:755` 后加 `if (order == nullptr) return false;`（或 `continue`）。
- **D-2'（治根，推荐）** 把 `vehicle_base.h:1158` 的锁**挪到 `:1161` 之后**：

```cpp
if (this->cur_real_order_index >= this->GetNumOrders()) this->cur_real_order_index = 0;
if (this->type == VehicleType::Train && this->current_order.IsType(OT_GOTO_COUPLE)) return;
```
即"钳制永远生效，只锁自增与 implicit 跳过"。语义不变（锁的目的只是不推进索引），但挡死 13.2。代价：改头文件 ⇒ 全量重编。
- **D-3'（更保险的等价写法）** 三处锁改成 `if (train && GOTO_COUPLE && cur_real_order_index < GetNumOrders()) return;`，越界时自动放行钳制路径。

### 13.5 复测判据

1. 内测版：订单 `[A, GOTO_COUPLE]`，机车等挂（`current_order = OT_GOTO_COUPLE`），删第 2 条 ⇒ 不再崩溃（且 `cur_real_order_index` 归 0）。
2. 负判据：普通 `[A,B,C]` 删除当前条，索引仍落在合法范围、行为逐字不变。
3. 区间负判据：插入 `GOTO_COUPLE` 自动生成的区间，删掉整段后分离时间恢复；`OT_COUPLE_END` 不再被当作可分离分隔点（13.3a 修完后）。
4. 内测版探针 `R3ROrderIdxWatch`（`order_cmd.cpp:1996`）应出现 `cmd-delete-in/out` 且 `out < num_new`。

### 13.6 本轮未做

未改任何源码、未构建；REG-388-A 保持暂撤；**尚未**用日志核对 `CmdDeleteOrder 2,2` 那一刻该车的订单表长度（需要内测版探针行）。
