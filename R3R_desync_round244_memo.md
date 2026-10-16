# R3R 联机同步（desync）审计 · 第 244 轮临时分析报告

- 日期：2026-10-16
- 轮次：第 244 轮（第 243 轮续五十九 = KI-370 之后的第一次专项审计）
- 玩家原话：「`@D:\sourcecode of JGRPP\build\R3R_debug.log` 看起来，这个流程也跑通了，你确认一下，然后下一轮就可以继续下一个问题了。下一方面，我希望能够针对同行容易爆雷的联机同步问题动刀」
- 本轮性质：**任务 A = 实机复测确认（只读）；任务 B = 联机同步审计第一轮（普查 + 分类 + 定性，零源码改动、零构建）**
- 交付：本文件 + `R3R_KNOWN_ISSUES.md` 末尾「第 244 轮」小节（KI-371 ~ KI-376）+ KI-370/KI-349 状态改「实机复测通过」

---

## 一、任务 A：KI-370 修复后流程复测 —— **通过**

### 1.1 取样有效性（先自证样本可信）
| 项 | 值 |
|---|---|
| `build\R3R_debug.log` | 12 632 行 / 919 520 B / mtime **2026-10-16 21:27:07** |
| `build\openttd.exe` | 51 868 160 B / mtime **2026-10-16 21:17:44**（= 第 243 轮续五十九 KI-370 修复版） |
| 结论 | 日志晚于 exe ⇒ **样本有效**，确为修复版 exe 产生 |

### 1.2 判据计数（`findstr` 复核）
| 判据串 | 命中行 | 判定 |
|---|---|---|
| `rail_map.h` | **0** | KI-370 断言崩溃已消失 ✅ |
| `Assertion` | **0** | 无任何断言 ✅ |
| `RESV-AUDIT-STRAY` | **0** | 无孤儿预留 ✅ |
| `REDSIG` | 10（= 2 次事件，**均为 veh=11**） | KI-349「veh=23 永久红灯」已消失；残留 2 次为 veh=11 的瞬态（自有抖动/他人占位），非永久定格 ✅ |
| `COUPLE-OK` | **9** | 耦合全程畅通 ✅ |
| `SPD-DROP` / `LOCO-MOVE` | 50 / 163 | 速度/运动探针工作正常 ✅ |

### 1.3 原崩溃场景是否跑完
- 原先崩在 `1073,139 → 1089,139` 那段耦合（旧日志尾 `831: RP cur=1081,139 td=8 depth=8`）。
- 本轮日志 539–773 段为该场景重演，**完整跑完 `wait=0..159` 至第 12 632 行无断言**；第 1019 行出现 `COUPLE-OK loco=2 rear=3 … tx=1083 ty=139` ⇒ 此前崩溃的那次订位步骤**已正常完成**。
- `rp()` 的 Trackdir 级订位（修法甲）+ KI-370 的 `IsPlainRailTile` 修正**双双生效**。

**任务 A 结论：KI-370 判据 ①②③④ 全部通过；KI-349 绿灯修复在实机不再产生永久红灯。**

---

## 二、任务 B：联机同步审计 —— 原理与 R3R 的风险面

### 2.1 OpenTTD 联机决定论
联机下每个客户端跑**同一份**模拟：同一 seed、同一命令序列 ⇒ 状态必须**逐位一致**。任何进入「模拟决策」或「写地图/写车辆」路径的状态，若在不同客户端（或同一客户端不同时刻、不同加载历史）取值不同，就会 desync。典型 3+1 类：

1. **NOSAVE 记账表被决策读取** —— 内存态随运行历史累积，读档/新客户端入局后与主机不同（**最致命，且往往"读档才炸"**）。
2. **容器迭代顺序** —— `std::unordered_map` / 指针集合若被**迭代**来决定顺序（不只是 contains 查询），顺序即决策。
3. **调用次数 / 帧时序节流** —— 按"被调用第 N 次"而非 `_tick_counter` 节流，一旦两端调用次数不同即分歧。
4. **客户端本地事件污染** —— GUI/预览/新闻/窗口开合引发的额外调用写进共享状态。

### 2.2 R3R 的结构性隐患
R3R 为绕开存档格式变更，**大量采用了「NOSAVE 内存记账表」**（预留台账、收割账本、段 ID 分配器、节流计数器……）。这在单机是优化，在联机是**desync 温床**：只要这张表参与过任何一次"是否释放/是否点绿/是否拒绝/给哪个 ID"的判断，主机与"读档进来的新客户端"就会分叉。

---

## 三、静态可变状态全量普查表（R3R 新增，src/ 下）

| # | 变量 | 位置 | 类型 | 用途 | NOSAVE | 分类 |
|---|---|---|---|---|---|---|
| 1 | `_r3r_resv_by_owner` | `pbs.cpp:159` | `unordered_map<OwnerKey, LedgerRow>` | 预留位归属台账 | 是 | **决策** |
| 2 | `_r3r_resv_owner_of_bit` | `pbs.cpp:160` | `unordered_map<bit_key, OwnerKey>` | 预留位→主人反查 | 是 | **决策** |
| 3 | `_r3r_resv_ledger_notes` | `pbs.cpp:269` | 计数器 | 触发 `R3RResvLedgerResync()` | 是 | 决策（间接） |
| 4 | `_r3r_couple_scan_state` | `pathfinder/yapf/yapf_rail.cpp:1750` | `std::map<int, ...>` | 空结果节流，直接改写寻路返回 | 是 | **决策** |
| 5 | `_r3r_segments` | `couple_group.cpp:974` | `std::vector<R3RSegmentRecord>` | 段身份行表（行内容有存档 R3SG，**容器+自由表 NOSAVE**） | 部分 | **决策** |
| 6 | `_r3r_free_segment_ids` | `couple_group.cpp:977` | `std::vector<uint16_t>` | 段 ID 自由表（LIFO） | 是 | **决策** |
| 7 | `_r3r_consist_resv` | `train_cmd.cpp:14931` | `unordered_map<uint32_t, vector<R3RResvRec>>` | 车底预留记账 | 是 | **决策** |
| 8 | `_r3r_consist_resv_gc_clock` | `train_cmd.cpp:14932` | `uint8_t` | `++clock==0` → 每 256 次调用跑 GC | 是 | **决策** |
| 9 | `_r3r_resv_stray_age` | `train_cmd.cpp:15045`（函数内 static） | `unordered_map<uint32_t,uint8_t>` | 孤儿预留老化计数 → REAP 释放 | 是 | **决策** |
| 10 | `_r3r_resv_audit_due` | `train_cmd.cpp:3230` | `bool` | 命令后提速审计 | 是 | 决策（时序） |
| 11 | `_r3r_resv_audit_has_pending` | `train_cmd.cpp`（15266 写） | `bool` | 有待老化项 → 加速审计 | 是 | 决策（时序） |
| 12 | `_r3r_decomp_seams` | `train_cmd.cpp:2292` | `btree_map<VehicleID,R3RDecompSeam>` | 解挂互锁（清速度/放行） | 是 | **决策** |
| 13 | `_r3r_pair_scan_tick` | `train_cmd.cpp:6758` | `unordered_map<uint32_t,uint64_t>` | 配对重扫节流（`_tick_counter`） | 是 | 决策（时序） |
| 14 | `_r3r_couple_enter_tick` | `train_cmd.cpp:6776` | `unordered_map<uint32_t,R3RCoupleEnterStamp>`（**含 `Vehicle*`**） | 进入耦合态时间戳 | 是 | 决策（时序） |
| 15 | `_r3r_couple_dest_idle` | `train_cmd.cpp:12005` | `btree_map<VehicleID,uint32_t>` | 目的地无车底空转计数 → 放弃 | 是 | 决策（时序） |
| 16 | `_r3r_waiter_purged` | `train_cmd.cpp:10444` | `btree_set<VehicleID>` | 等待者清扫去重 | 是 | 待核 |
| 17 | `_r3r_waiter_nobook_logged` | `train_cmd.cpp:10451` | `btree_set<VehicleID>` | `RESV-NOBOOK` 日志去重 | 是 | 仅日志 |
| 18 | `_r3r_live_vehicle_cache` | `station_cmd.cpp`（待核，~5073） | `robin_hood::set<const Vehicle*>` | 活车快照（丢弃悬垂共享车） | 是 | **待核**（指针集合） |
| 19 | `_r3r_couple_fail_news_shown` / `_r3r_couple_depot_news` | `train_cmd.cpp` | 集合 | 新闻去重 | 是 | 仅 GUI/新闻 |
| 20 | `_r3r_last_dbg_tick` | `train_cmd.cpp` | `uint32_t` | 调试节流 | 是 | 仅日志 |

---

## 四、TOP 5 desync 嫌疑（含机制与代码锚点）

### TOP 1（高）：预留台账 `_r3r_resv_by_owner` / `_r3r_resv_owner_of_bit` —— **机制已实证**
- **位置**：`pbs.cpp:159/160`，NOSAVE。文件头注释（`pbs.cpp:169` 一带）自陈：`R3R_RESV_NO_OWNER when no actor is set`，且**"a load-time or engine-internal write must not be booked"** ⇒ 读档期写入**不入账**。
- **读点（决定成败的不是日志）**：
  - `pbs.cpp:724` `TryReserveRailTrackdir()` —— **KI-361 绿灯回退**：若 `R3RTrainForLedgerBit(tile, track)` 返回自己的链 ⇒ `success = true` 且随后 `SetSignalStateByTrackdir(tile, td, Green)`（**写地图**，同步可见）。
  - `pbs.cpp:375` `R3RReleaseReservationLedgerChecked()` —— "**Never free a bit the ledger attributes to somebody else**"：台账主人 ≠ 自己 ⇒ **拒绝释放**。
  - `train_cmd.cpp:15120 / 15153 / 15165` 预留审计 —— `ledger_dead` / `ledger_absent` ⇒ 决定 **REAP 释放**哪些地图预留位。
  - `train_cmd.cpp:13218` —— `r3r_led = R3RResvLedgerOwner(...)` 影响 backout 行为。
- **desync 路径（两条）**：
  - **(a) 点绿分歧**：主机跑了一整局、台账有行；**新客户端读档 ⇒ 台账为空** ⇒ 同一格 `TryReserveRailTrackdir` 在主机返回 `success=true` 并点绿、在新客户端返回 `false` 且不点绿 ⇒ 信号/路径分歧。
  - **(b) 释放分歧**：`ledger_absent = (owner != nullptr) && (lowner == NO_OWNER) && !bit_on_some_path(t)`（`train_cmd.cpp:15165` 一带）在**台账为空的新客户端上恒为真**（凡"有主位但不在任何路径上"的格）⇒ 新客户端把主机保留的预留位当孤儿 **reap 掉** ⇒ 直接改地图。
- **为何不会自愈**：`R3RResvLedgerResync()`（`pbs.cpp:269` 触发）只做「**账 → 图**」的单向修剪（`R3RResvLedgerSyncNotes`），**无法为地图上"无账主"的位补出主人**（主人信息只在记账那一刻存在）⇒ 空台账**结构性无法复原**。
- **判定**：这是本轮**首要修复目标**，也是"读档/有人中途入局就炸"的教科书形态。

### TOP 2（高）：预留审计与收割循环
- `_r3r_resv_stray_age`（`train_cmd.cpp:15045`，函数内 static）+ `R3RReservationAudit()`（`:15028`）→ `age>=3` 即 **`RESV-REAP` 释放地图预留位**。
- `_r3r_consist_resv`（`:14931`）+ `_r3r_consist_resv_gc_clock`（`:14932`，`++clock==0` 每 **256 次调用**跑 GC，`:15392`）→ `R3RGcConsistReservations()` 释放预留。
- **风险点**：①判据依赖 TOP 1 的台账（台账空 ⇒ 误判孤儿）；②`gc_clock` 是**调用计数**（非 `_tick_counter`），一轮内调用次数若两端不同即节拍错位；③两者都**改写地图预留位**（模拟可见）。
- 注：`_r3r_resv_audit_due`（`:3230`）由命令（`CmdMoveRailVehicle`/`CmdSellRailWagon`）置位，**命令是同步的 ⇒ 这一半确定性较好**；真正的风险集中在「台账内容」与「调用计数」。

### TOP 3（中）：段 ID 分配器 `_r3r_segments` / `_r3r_free_segment_ids`
- `couple_group.cpp:974/977`。段行**内容**有存档（稀疏块 R3SG），但**容器与自由表是 NOSAVE**。
- `R3RSegmentAlloc()` 从自由表 **LIFO** 取 ID。主机运行中释放过段 ⇒ 自由表有回收项；新客户端读档后自由表由重建逻辑生成 ⇒ **同一时刻新建段拿到的 ID 可能与主机不同**。
- 若 ID 仅作不透明键 ⇒ 无害；但若参与**任何排序/比较/回落到"最小 ID"的决策**（或 ID 被写进可比较字段） ⇒ 分歧。
- **待验证**：`R3RSegmentAlloc` 的检索顺序、以及 ID 是否进入决策（见 §七 G-2）。

### TOP 4（中）：耦合寻路空结果节流 `_r3r_couple_scan_state`
- `yapf_rail.cpp:1750`，**按调用次数**递减 `hold`（`R3RCoupleScanHold`），并在 `hold > 0` 时**直接让寻路返回 `INVALID_TRACK`**。
- 目前唯一调用者为 `train_cmd.cpp:13122`（模拟侧），未见 GUI/预览调用 ⇒ **当前大概率是"潜伏"而非"已炸"**。
- 但仍属风险：①按**调用次数**而非 tick 节流；②`std::map` 按车辆索引缓存、**车辆移除不清理**（索引复用会继承旧 `hold`）；③任何将来新增的调用点（调试/GUI）都会立刻变成 desync 源。

### TOP 5（中）：解挂互锁 `_r3r_decomp_seams`
- `train_cmd.cpp:2292`，NOSAVE，注册于解挂、在前车驶离后清除；**命中时直接清 follower 速度并返回"放行"**（决定运动）。
- 存档往返后互锁丢失（若在中途存档）；单机是短命状态、影响小，联机下仍属"上了决策路径的 NOSAVE 态"。

---

## 五、疑点登记（REG-244-x）

- **REG-244-A**：`R3RResvLedgerResync()` 到底做几件事？注释称"re-checks the ledger against the map"——若只做「账→图」修剪，则 §TOP1 的"读档空台账不自愈"成立；若它也做「图→账」补主，则需重新定性。**期望值**=单向修剪；**待验证**=读 `pbs.cpp:269-330` 的 `LedgerResync` 实现。
  - **【2026-10-16 已核实为「单向修剪」】** ⇒ §TOP1 desync 路径成立。
- **REG-244-B**：R3R 是否已在联机下被实际验证过？工作区/KI 全无 desync 条目 ⇒ 推测**从未做过联机 desync 复测**（本项本身即为待办）。
- **REG-244-C**：`_r3r_couple_enter_tick` 存了裸 `Vehicle*`（`train_cmd.cpp:6776`）。指针值跨客户端不同；若仅做身份比较无害，若被迭代/排序则危险。**待验证**=用法。
- **REG-244-D**：`_r3r_live_vehicle_cache`（`station_cmd.cpp`，行号待精确定位）是 `robin_hood` 指针集合。`robin_hood` 迭代顺序由 hash+插入历史决定 ⇒ **跨客户端不可复现**。若该集合仅做 `contains/count`，无害；若被**迭代**来做删除决策（"丢弃悬垂共享车"）则 desync。**待验证**=是否有 `for (auto ... : _r3r_live_vehicle_cache)`。
  - **【2026-10-16 已核实 = 安全，本条结案】** 精确定位 `static robin_hood::unordered_flat_set<const Vehicle *> _r3r_live_vehicle_cache;` 于 **`station_cmd.cpp:5073`**，配套 `static bool _r3r_live_vehicle_cache_valid`（`:5074`）、`R3RIsLiveVehicleCached()`（`:5077-5085`）、`ClearDeleteStaleLinksVehicleCache()`（`:5087-5091`）。`R3RIsLiveVehicleCached()` 的唯一用法是 `_r3r_live_vehicle_cache.find(v) != _r3r_live_vehicle_cache.end()`（`:5084`，纯成员判断），**全文件无任何对它的迭代**；缓存内容由 `Vehicle::Iterate()` 全量重建（`:5081`），值的全集只由"活车集合"决定 ⇒ hash 顺序不进入任何判定 ⇒ 见 §11.3。
- **REG-244-E**：`train_cmd.cpp:15236` 的 `for (auto it = _r3r_resv_stray_age.begin(); ...)` 是 `unordered_map` 迭代（用于修剪）。若修剪只做 erase-by-key，顺序无关；若修剪过程有**计数/阈值**参与决策，则与哈希顺序挂钩。**待验证**。

---

## 六、猜测根因（待验证）

- **G-1（高置信）**：R3R 的 desync 主因 = **NOSAVE 预留台账 `_r3r_resv_by_owner` 被 KI-361 绿灯回退与预留审计读取**，而"读档期写入不记账"是显式设计 ⇒ 任何"主机已跑一段 + 客户端读档入局"的组合必然分叉。对应 KI-371/KI-372。
- **G-2（中置信）**：段 ID 自由表的 **LIFO 分配顺序**在"运行中的主机"与"读档进来的客户端"之间不同，若段 ID 参与排序/回落 ⇒ 段身份判定分歧。对应 KI-373。
- **G-3（中置信）**：若两端调用次数出现差异（GUI/调试/未来新增调用点），`_r3r_couple_scan_state`、`_r3r_consist_resv_gc_clock` 这类**调用计数节流**会立刻分叉。对应 KI-374。
- **G-4（低置信）**：`robin_hood`/指针集合的**迭代顺序**参与决策。对应 REG-244-D。

---

## 七、修复方向（优先级排序，待玩家拍板后落地）

1. **P0 —— 台账确定性（治 TOP1/TOP2）**，三选一：
   - **(甲) 台账入档**：把 `_r3r_resv_by_owner` / `_r3r_resv_owner_of_bit` 写进稀疏存档块（仿 R3VP/R3SG，按 tile+track 索引）。最彻底、消除"读档空台账"，代价=存档格式新增块 + 需在指针阶段重连车主引用。
   - **(乙) 读档重建**：读档/入局后**从地图重建台账**（owner 由"格上/路径上的车"推定）。零存档变更，但"主人"信息本就不在地图里，只能近似 ⇒ 仍可能与主机不同（不推荐单独用）。
   - **(丙) 去台账化（治本但改动大）**：让 KI-361 绿灯回退与释放判据**不依赖 owner 台账**，改用"地图位的可推导属性"（例如：该位当前是否被任何列车路径覆盖 / 是否只有自己链在格上）。
2. **P1 —— 段 ID 分配确定性（治 TOP3）**：把自由表(或至少"下一个可用 ID")随段表入档；或改成**确定性分配**（始终取最小空闲 ID，与历史无关）。
3. **P2 —— 节流去调用计数化（治 TOP4/TOP2②）**：所有"每 N 次调用"改为 `_tick_counter` 驱动；`_r3r_consist_resv_gc_clock` 尤其。
4. **P3 —— 审计基础设施**：加一个"R3R NOSAVE 决策态"自检（每次读档后 dump 各表规模），并用 `-d desync=3` 建立标准复测流程。

---

## 八、联机复测方法（建议）

1. 起一个专用存档（含耦合/解挂/多段链/等待挂接场景）。
2. 主机开服；**第二个客户端中途入局**（或主机存档后客户端加载同一存档）→ 观察是否 desync。
3. 启动参数加 `-d desync=3`，出现 desync 时保存的 `desync_*.log` 会指出是"命令"还是"随机/状态"分歧点（`Command` vs `State`）。
4. 针对性判据（修复后）：
   - 修复后**不再出现 desync 报告**；
   - 或（若仍出现）报错点**不再落在** `R3RReleaseReservationLedgerChecked` / `TryReserveRailTrackdir` / `R3RReservationAudit` / `R3RSegmentAlloc`。

---

## 九、未确认项

- U-1：R3R 此前是否做过任何联机测试（REG-244-B）。
- ~~U-2：`_r3r_live_vehicle_cache` 的确切行号与是否被迭代（REG-244-D）。~~ **【已结案 2026-10-16】** 行号=`station_cmd.cpp:5073`；只做 `find()` 成员判断，**从不迭代** ⇒ 安全（详见 §11.3 与 REG-244-D 附记）。
- U-3：`_r3r_couple_enter_tick` 中裸 `Vehicle*` 的用法是否仅身份比较（REG-244-C）。
- U-4：段 ID 是否真的进入任何决策/比较（TOP3 定性前提）。
- ~~U-5：`R3RSegmentAlloc` 的取 ID 顺序细节（是否 LIFO）。~~ **【2026-10-16 已结案】** 确认 **LIFO**：`couple_group.cpp:1004-1010` 取 `_r3r_free_segment_ids.back()` 后 `pop_back()`；释放侧 `:1028` `push_back(id)` ⇒ 段 ID 与"历史回收顺序"绑定（见 §12.0 E-2 与第 3 项改法）。
- U-6：`_r3r_resv_stray_age` 修剪循环的判定是否与哈希顺序挂钩（REG-244-E）。

---

## 十、本轮结论

- **任务 A：KI-370 实机复测通过**（断言 0、流程跑通、9 次 COUPLE-OK）。
- **任务 B：完成 R3R 首次联机同步审计的普查与定性**，锁定 **KI-371（预留台账）为首要 desync 源**，机制已实证；KI-372~KI-376 为同族风险。
- **下一步**：待玩家拍板修复方向（§七 P0 甲/乙/丙），再落地代码 + 构建 + `-d desync=3` 复测。

---

## 十一、判据框架（第 244 轮续，2026-10-16）：NOSAVE 是「可推导性声明」，不是筛选条件

**玩家提问（原文）**：「那我们现在的问题是不是就简化为了寻找那些 nosave 但是实际上需要记入存档的字段，我认为，字段不可能既是 nosave 又不录入存档」。

**结论：方向对了一半，但按字面执行会「漏掉一整类、并且误伤一大半」。**

### 11.1 NOSAVE 的真实语义

`NOSAVE` 不是"我忘了存"，而是一条**声明**：「本状态**可由持久状态推导**，或**纯本地**（显示/调试），因此无需持久化」。

⇒ 审计的正确形式 = **验证这条声明在每一个读取点上是否成立**。
⇒ 判据不在"字段"，而在**读取点**：声明成立 ⇒ 合法 NOSAVE，不用管；声明不成立 ⇒ 才是问题。

### 11.2 出口有三条，不是一条（玩家框架只列了第三条）

| 出口 | 做法 | 代价 | 优先级 |
|---|---|---|---|
| **(A) 去依赖** | 改判据，让决策不再读该状态（或改用存档内可推导的属性） | 不动存档格式 | **最高（最治本）** |
| **(B) 确定性重建** | 读档后由存档内已有数据**唯一地**重建该状态 | 零存档变更，但要能证明唯一 | 中 |
| **(C) 入档** | 写进稀疏块（仿 R3VP/R3SG） | 动存档格式 + 指针阶段重连 | **最低（最后手段）** |

⇒ "NOSAVE 且需要记录"的字段**确实存在**，但**入档不是唯一解，多数应走 (A)/(B)**。

### 11.3 三类**合法 NOSAVE**（不能拿"NOSAVE"当筛选条件）

1. **纯缓存**：值 = 持久状态的纯函数，且**只查询、不迭代**。
   - **【本轮结案】`_r3r_live_vehicle_cache`（`station_cmd.cpp:5073`）安全**：`R3RIsLiveVehicleCached()`（`:5077-5085`）**只有** `_r3r_live_vehicle_cache.find(v) != end()`，全文件**无任何 `for (... : _r3r_live_vehicle_cache)`**；缓存由 `ClearDeleteStaleLinksVehicleCache()`（`:5087-5091`）显式整体失效、失效后由 `Vehicle::Iterate()` 全量重建 ⇒ 值只由"活车集合"决定，**`robin_hood` 的 hash 顺序不参与任何判定** ⇒ **REG-244-D 结案：安全**（U-2 结案）。
2. **纯调试 / 日志闸门**：`_r3r_waiter_nobook_logged`、`R3RDbgEdge` 的全部节流表、`_r3r_last_dbg_tick` 等 —— 只影响"打不打印"，不影响 `return` 值。
3. **每 tick 重算的瞬时态**：两端 tick 同步 ⇒ 重算结果相同。

### 11.4 玩家框架会**漏掉**的一整类：状态**在存档里**，但「读档路径」与「运行路径」行为不同

这是 desync 的**第二形态**，与 NOSAVE 无关，只查 NOSAVE 会完全看不到：

- **(a) 读档期排除写入** —— 玩家自己举的预留台账例子，其实**同时**属于 NOSAVE 类和本类（台账是 NOSAVE，且它的写入口显式排除读档期）。R3R 里凡注释写着"load-time / engine-internal writes are not recorded"的地方都要查。
- **(b) 读档期改写 / 迁移状态** —— R3R 实例：`R3RRebuildCouplePriorities()`（`train_cmd.cpp`）**只被 `src/sl/vehicle_sl.cpp` 的 `part_of_load` 调用**，内含 `LEGACY-ARTIC-DEARTIC` 打散、`R3RSettleChainSegments(chain,"load")` 重发段 ID、KI-263 的 `DEPOT-XFER-ID ... load` 车号穿透。
  - 这些迁移**目前未证实有问题**（推演：服务器开局读过一次档 ⇒ 迁移已烘焙进它后来发出的存档 ⇒ 新入局客户端拿到的存档已不含需迁移的形态 ⇒ 两端一致）。
  - 但这是**结构性脆弱点**：一旦某个迁移**非幂等**、或迁移条件**依赖了存档未记录的输入**，读档的一方就会与一直在跑的一方分叉。⇒ 必须**逐项验证幂等性**（列入 §十二 待办）。
- **(c) 有存档，但**迭代顺序**或**触发节拍**不确定**：`std::unordered_map` 遍历选"下一个"、按**调用次数**而非 `_tick_counter` 节流 —— 字段本身存了也没用。

### 11.5 正确方法论：**从「决策点」反向追，而不是从「字段」正向筛**

正向看字段无法判断它是否真的到达决策（多为间接：经缓存 / 容器 / 多层封装），也发现不了 11.4(b) 类。
⇒ 先枚举 R3R 的**模拟可见写操作**（决策点），再对每个决策点反查它读了什么：

**R3R 的决策点清单（全部在 `src/`）**
1. 写/清地图预留位（`SetTrackReservation` / `SetRailStationReservation` / `UnreserveRailTrack`）
2. 写信号状态（`SetSignalState`，含 KI-361 绿灯回退）
3. 改车辆运动（`cur_speed` / `subspeed` / `MarkTrainAsStuck`）
4. 分配 / 释放 ID（段 ID、车辆号）
5. 改链结构（`SetNext`/`SetPrevious`、★/⊗、假引擎位）
6. 推进订单（`cur_real_order_index`、`current_order.Free()`）

**每个决策点问五问**：
1. 判据读了哪些状态？
2. 其中哪些**不在存档**？（→ NOSAVE 类，即 KI-371/372/373/375/376）
3. 其中哪些**在存档但读档期被排除/改写**？（→ 路径不对称类，11.4a/b）
4. 哪些的**求值 / 迭代顺序**不确定？（→ 哈希顺序类，REG-244-E）
5. 哪些的**触发节拍**是"调用次数"而非 tick？（→ 节流类，KI-372 的 GC / KI-374）

### 11.6 按本框架对 §三 普查表的重新分类

| 分类 | 成员 | 处置 |
|---|---|---|
| **合法 NOSAVE（排除）** | `_r3r_live_vehicle_cache`（已结案）、`_r3r_waiter_nobook_logged`、`R3RDbgEdge` 节流表、`_r3r_last_dbg_tick` | 不动 |
| **NOSAVE + 决策 + 依赖历史（要处理）** | `_r3r_resv_by_owner` / `_r3r_resv_owner_of_bit`（KI-371）、`_r3r_resv_stray_age`（KI-372）、`_r3r_consist_resv`（KI-372）、`_r3r_segments` / `_r3r_free_segment_ids`（KI-373）、`_r3r_decomp_seams`（KI-375）、`_r3r_couple_dest_idle`（KI-376）、`_r3r_waiter_purged`（KI-376，待核） | 走 11.2 的 (A)/(B)/(C) |
| **NOSAVE 但由命令置位（安全）** | `_r3r_resv_audit_due`（`train_cmd.cpp:3223`，由 `CmdMoveRailVehicle`/`CmdSellRailWagon` 置位 ⇒ 命令同步 ⇒ 两端一致） | 仅节拍需复核 |
| **节流载体（与字段无关）** | `_r3r_consist_resv_gc_clock`（KI-372）、`_r3r_couple_scan_state`（KI-374） | 改 `_tick_counter` 驱动 |
| **顺序不确定** | `_r3r_resv_stray_age` 修剪循环（`train_cmd.cpp:15236`，REG-244-E，待核） | 待核 |
| **路径不对称（11.4）** | 台账"读档期不记账"（KI-371 的另一面）；读档迁移族（`LEGACY-ARTIC-DEARTIC` / `R3RSettleChainSegments("load")` / `DEPOT-XFER-ID load`） | 逐一验证幂等性 |

### 11.7 对玩家提问的直接回答

1. "NOSAVE 但需要记录"的字段**确实存在**（上表第 2 行），玩家的直觉方向正确；
2. 但**不能只找它们**：第 3~6 行四类都不是"NOSAVE 字段"，却同样能造成 desync；
3. 而且**入档不是唯一解**：判据是"NOSAVE 声明在读取点是否成立"，不成立的优先走 **去依赖**，其次 **重建**，**入档才是最后手段**（动存档格式，风险最高）；
4. ⇒ 正确的问题表述是：**「找出所有参与模拟决策的状态，逐项证明它跨客户端可复现；证不出来的，三选一出口」**。

---

## 十二、逐项改法清单（2026-10-16，待玩家过目/拍板，尚未改码）

### 12.0 本轮新增取证（3 条，均影响改法选择）

| # | 事实 | 锚点 | 对改法的影响 |
|---|---|---|---|
| E-1 | 台账 `owner` 存的是 **`uint32_t` vehicle index**，不是指针（取用时 `Train::GetIfValid((size_t)owner)`） | `pbs.cpp:330-334`（`R3RResvLedgerOwner`）、`:346-354`（`R3RTrainForLedgerBit`） | ⇒ **入档方案天然可行**：index 跨客户端一致，写稀疏块**不需要指针阶段重连**，只需读档后 `GetIfValid` 过滤死主 |
| E-2 | 段 ID 分配确认 **LIFO**（`id = _r3r_free_segment_ids.back(); pop_back();`） | `couple_group.cpp:1004-1010`（`R3RSegmentAlloc`）、`:1028`（释放 `push_back`） | ⇒ 改法不必入档：**把 LIFO 换成"取最小空闲 ID"即确定性**（已分配集合来自 R3SG 行 ⇒ 两端一致） |
| E-3 | `_r3r_decomp_seams` / `_r3r_consist_resv` 的 value **全是 POD**（`VehicleID`/`TileIndex`/`Track`/`uint64_t`/`bool`，无指针） | `train_cmd.cpp:2257-2263`（`R3RDecompSeam`）、`:14926-14929`（`R3RResvRec`） | ⇒ 这两项若走"入档"，**成本极低且无指针重连**（与台账同级），不需要走昂贵的 (丙) |

### 12.1 逐项方案

**第 1 项 · 预留台账 `_r3r_resv_by_owner` / `_r3r_resv_owner_of_bit`（KI-371，TOP1）**
- **推荐出口：(C) 入档**（因 E-1，代价从"高"降到"中"）；(丙) 去台账化列为**后续架构优化**，不在本轮。
- **改动点**：
  1. `sl/` 新增稀疏块（拟名 **R3RL**，`CH_SPARSE_TABLE`，仿 R3VP/R3SG 写法）：按 owner 分组，每组 `[owner(index), n, (tile, track, purposes)×n]`；反查表 `_r3r_resv_owner_of_bit` **不单独写**，读档时由正向表重建（它是纯派生）。
  2. 写出条件 `R3RHasResvLedger()`（两表非空）。
  3. 读入后由 `AfterLoadVehiclesPhase2()` 调一次 `R3RResvLedgerRebuildFromSave()`：过滤 `Train::GetIfValid(owner) == nullptr` 的行（死主），重建反查表。
  4. **取消"读档期写入不记账"的例外**（`pbs.cpp:169` 一带文件头注释自陈的设计）——否则刚恢复的台账会被后续读档期写入再次撕开。
- **代价**：动存档格式（新增块，向后兼容：旧档无块 ⇒ 空表 ⇒ 退化为现状）；不涉及指针重连；约 1 个 `sl` 文件 + `pbs.cpp` 恢复函数。
- **风险**：中。主要风险是"活主 index 在存档往返后语义漂移"（车辆 index 本身是同步的，风险低）。
- **不做的事**：不改 `R3RTrainForLedgerBit` 的任何判据语义。

**第 2 项 · 预留审计 / 老化（KI-372，TOP2）**
- **推荐出口：(A) 去依赖 + 改 tick 驱动**（**依赖第 1 项先修**：孤儿判定的分歧根因是台账空）。
- **改动点**：
  1. `_r3r_resv_stray_age`（`train_cmd.cpp:15045`，函数内 static）值由 `uint8_t age` 改 **`uint64_t stray_since`（首次判孤儿的 `_tick_counter`）**，reap 条件改 `_tick_counter - stray_since >= 3 * audit_interval`。审计触发已由 tick 决定（`train_cmd.cpp` 的审计门），故两端节拍一致。
  2. `_r3r_consist_resv_gc_clock`（`:14932`）的 `++clock == 0` 改 **`_tick_counter % 256 == 0`** 驱动（`:15392` 调用点）。
  3. 修剪循环（`:15236`，REG-244-E）若只做 erase-by-key ⇒ 顺序无关，**保留**；若含计数决策 ⇒ 改按 key 排序遍历。
- **代价**：零存档变更；约 3 处。
- **风险**：低。**注意**：此项单独修**不够**——只要台账两端不同，orphan 判定本身就会不同，所以第 1 项是前提。

**第 3 项 · 段 ID 分配器（KI-373，TOP3）**
- **推荐出口：(A) 去历史依赖**（因 E-2，无需入档）。
- **改动点**：`R3RSegmentAlloc()`（`couple_group.cpp:1004`）由"自由表 back/pop"改为**取最小未使用 ID**（可维护 `_r3r_segment_alloc_hint`：从上次分配位置向后扫是确定性的，且与两端已分配集合一致 ⇒ 结果一致）；`_r3r_free_segment_ids` 可退化为"仅统计/或删除"。
- **先决条件（U-4）**：必须先确认段 ID 是否进入任何**排序/比较/回落**决策。若纯不透明键 ⇒ 本项降级为"加固（低优先）"；若进入 ⇒ 本项为 P1。
- **代价**：零存档变更；1 个函数 + 自由表语义调整；O(段数) 扫描，调用频率低（升段/耦合/读档）。
- **风险**：低。**关联**：读档期 `R3RSettleChainSegments(chain,"load")` 会**重发段 ID** ⇒ 与"自由表内容"直接耦合，改完后此项自动变确定（见第 10 项）。

**第 4 项 · 耦合寻路空结果节流 `_r3r_couple_scan_state`（KI-374，TOP4）**
- **推荐出口：(A) 去掉"按调用次数"语义**。
- **改动点（二选一）**：
  - **首选**：改成**同 tick 缓存**——记 `last_tick`，同一 tick 内的重复调用直接返回 `INVALID_TRACK`，跨 tick 重算 ⇒ 完全确定，性能收益保留大半；
  - 或直接删除节流（纯性能优化，正确性优先）。
- **附带**：车辆移除时清理条目（现在是 `std::map<int, ...>` 按 index 缓存，**索引复用会继承旧 hold**）。
- **代价**：零存档变更；1 个文件（`yapf_rail.cpp:1746-1760`）+ 一个清理点。
- **风险**：低。

**第 5 项 · 解挂互锁 `_r3r_decomp_seams`（KI-375，TOP5）**
- **推荐出口：(C) 入档**（因 E-3：结构全 POD，成本低）或 **(B) 重建**。
- **改动点（若走 C）**：并入第 1 项的稀疏块方案（同一轮做）：`[follower(VehicleID), head, seam_car, seam_tile, tick, reported]`。`tick` 是 `_tick_counter` ⇒ 两端一致。
- **代价**：小（POD 字段，无指针重连）。
- **风险**：低。**边界**：`reported`（日志去重）不必入档，可读档后置 false。

**第 6 项 · 目的地无车底空转超时 `_r3r_couple_dest_idle`（KI-376）**
- **推荐出口：(A) 去计时** —— 把"空转满 500 tick 放弃"改成**纯状态判据允许时立即放弃**（目的地无可挂车底是地图可推导的）。
- **若必须保留"等一会儿"的语义**（避免误放弃）：退而记录**到达目的地的 tick**（该时刻是地图/订单可推导的，两端一致），而不是本地累加计数。
- **代价**：零存档变更；1 处判据（`train_cmd.cpp:12005` 附近）。
- **风险**：中（改的是"放弃时机"→ 可能影响行为体验，需要复测）。**建议先做成"只打日志不放弃"跑一轮**，确认无误放弃再启用。

**第 7 项 · 配对进入时间戳 `_r3r_couple_enter_tick`（REG-244-C）**
- **推荐出口：(A) 契约明确化**（低优先，但顺手）。
- **改动点**：`R3RCoupleEnterStamp::owner`（`train_cmd.cpp:6772-6775`）由 `const Train*` 改 **`VehicleID`**，比较用 ID；**同一 tick 并列时按 vehicle index 做 tie-break**（否则"谁先进订单"退化为遍历顺序依赖）。
- **代价**：零存档变更，约 5 行。
- **风险**：低。

**第 8 项 · 等待者清扫去重 `_r3r_waiter_purged`（待核）**
- **先做**：核 `train_cmd.cpp:10444` 的用法是否**幂等**（若清扫幂等 ⇒ 去重集合只影响执行时机 ⇒ 改成每 tick 无条件清扫即可，去掉集合）。
- **出口**：待核后定 (A)。

**第 9 项 · 确认安全，不动**
- `_r3r_live_vehicle_cache`（`station_cmd.cpp:5073`，已结案）、`_r3r_waiter_nobook_logged`（`train_cmd.cpp:10451`，仅日志）、`_r3r_couple_fail_news_shown` / `_r3r_couple_depot_news`（GUI/新闻）、`_r3r_last_dbg_tick`（调试）、`_r3r_pair_scan_tick`（`:6758`，已是 `_tick_counter` 驱动，仅需复核 key 清理时机）、`_r3r_resv_audit_due`（`:3230`，命令置位 ⇒ 命令同步）。

**第 10 项 · 读档迁移族幂等性核验（11.4b，非 NOSAVE 类）**
- 逐项验：`LEGACY-ARTIC-DEARTIC`（打散后再读应不再触发）、`R3RSettleChainSegments(chain,"load")`（重发段 ID ⇒ 与第 3 项强耦合）、`DEPOT-XFER-ID ... load`（KI-263 车号穿透 ⇒ 幂等性）。
- **出口**：不预设，验完再定；**这一项是"服务器跑一段 + 客户端读档"场景的第二条独立分叉路径**，不能因为"推演没问题"就跳过。

### 12.2 建议的实施顺序（供拍板）

| 批次 | 内容 | 理由 |
|---|---|---|
| **批 1（P0）** | 第 1 项（台账入档）+ 第 2 项（tick 化） | 唯一"机制已实证"的 desync 源，且第 2 项依赖第 1 项 |
| **批 2（P1）** | 第 4 项（去节流）、第 6 项（去计时）、第 3 项（最小空闲 ID） | 零存档变更、改动小、都是"去历史依赖" |
| **批 3（P2）** | 第 5 项（互锁入档，可并入批 1 的块）、第 7 项（契约化）、第 8 项（待核后） | 成本低但有存档/行为影响 |
| **批 4（P3）** | 第 10 项（迁移幂等核验）+ 复测基础设施（读档 dump 各表规模、`-d desync=3` 标准流程） | 验收手段，先有手段再谈"修好了" |

### 12.3 待玩家拍板的三个问题

1. **第 1 项走 (C) 入档 还是 先做 (丙) 去台账化？** 我建议 (C)（因 E-1 成本已降），(丙) 留作后续。
2. **第 6 项（空转超时）** 是否先做成"只打日志不放弃"跑一轮，再决定启用？
3. **第 3 项是否现在就做？** 取决于 U-4（段 ID 是否进入决策）——要不要我先把 U-4 证掉？

---

## 十三、十项的应用场景（人话版，2026-10-16）

总纲：R3R 的「段」是游戏里可拆可拼的车组；联机要求每台机器算出同一结果。下面按「玩家什么时候会碰到它」分组。

### A 组 · 记录「这格轨道现在归谁」（第 1、2 项）

**场景**：机车要出站/去挂车，得先把前方几十格轨道「订」下来（铁轨上的黑色预留），别的车不能走。订完之后，引擎与 R3R 每帧都要反复问一句：「**这格现在归谁？**」

- **第 1 项（台账 `_r3r_resv_by_owner`）= 那本「谁订了哪格」的登记簿**。谁在问它：①机车回溯自己的路径订位时发现「这格是别人订的」⇒ 决定让不让（KI-361 的绿灯回退）；②释放预留时「绝不放掉别人的位」；③清理孤儿预留时判断「车主还在不在」。**不打包的症状**：服务器跑了两小时、簿子有记录；你中途读档进来、簿子是空的 ⇒ 同一格，服务器说「归某车、不让」，你说「空的、让」⇒ 从这一帧起两台机器不一样（这正是「读档/中途入局就炸」的经典形态）。
- **第 2 项（老化 + 收割 `_r3r_resv_stray_age`）= 簿子的「无人认领就清掉」机制**：某格预留连续 3 次巡查都查不到车主 ⇒ 判幽灵、释放。**场景**：玩家看到铁轨上一块永远不消失的黑色预留（KI-203 那类），或拆站/卖车后的残留。**不打包的症状**：两端「数到第几次才清」不同 ⇒ 一台清了、一台没清 ⇒ 地图预留状态不同 ⇒ 信号/寻路分歧。**关键**：它**依赖第 1 项**——簿子为空的机器会把主人还在的预留位当孤儿，直接清掉。

### B 组 · 段号的发号机（第 3 项）

**场景**：R3R 的「段」是有身份的东西——段名、挂接分组、优先级（哪段是「控制段」）都挂在段号上。每次耦合/升段要新建一个段 ⇒ 找发号机要一个空号。

- **第 3 项（`_r3r_segments` / `_r3r_free_segment_ids`）= 发号机**，现实现是「回收过的号最后进先出」（LIFO，已实证）。**场景**：服务器跑一局，段号被回收又重发，号池顺序早与开局不同；你读档进来，号池按存档重建 ⇒ 同一时刻新建段，服务器发 3 号、你发 7 号。**症状**：**只有当段号参与排序/比较时**才炸（故先证 U-4）；若只当不透明编号用则是潜伏。**改法为什么便宜**：改成「永远发最小空号」，与历史无关 ⇒ 两端一致，且不动存档。

### C 组 · 挂车过程中的「节奏」（第 4、6、7 项）

**场景**：机车收到「前往挂接」订单，去找一台等待的车底并挂上。这个过程里有三处计时/计次。

- **第 4 项（寻路节流 `_r3r_couple_scan_state`）**：车底还没到位时，机车每帧都重新寻路纯属白烧 CPU ⇒ 现在写成「上 8 次都找不到，接下来 7 次直接回答没路」。**症状**：两端「调用次数」不同（一台开了调试/GUI、或将来加个预览窗口）⇒ 一台还在跳过、另一台已重新找路 ⇒ 找路结果不同 ⇒ 分歧。
- **第 6 项（目的地空转 `_r3r_couple_dest_idle`）**：机车开到订单指定目的地，那儿**没有车底** ⇒ 现在空转 500 帧才放弃并跳过订单（防死循环刷屏，KI-193）。**症状**：你中途读档进来，计数器从 0 开始 ⇒ 你比服务器晚 500 帧才放弃 ⇒ **一台走了、一台还在原地等**。
- **第 7 项（抢车底 `_r3r_couple_enter_tick`）**：两台机车同时想要同一台等待车底，规则是「谁先进入前往挂接状态谁优先」。**症状**：这个「先」本身是帧时间戳（安全），但若两台车在**同一帧**进入，就退化成「谁先被遍历到」——遍历顺序在联机下不保证一致 ⇒ 谁抢到车底可能不同。**改法=同帧并列时按车号排序。**

### D 组 · 解挂后的「让路」互锁（第 5 项）

**场景**：列车在站台解挂成两半，前一半要先驶离站台，后一半才准起步（否则贴一起/穿模）。这个「等着前车走开」的记忆就是第 5 项（`_r3r_decomp_seams`）。

- 效果玩家已实机确认过：「后车确实等到前车离开站台才起步」（第 228 轮）。
- **症状**：**正好在解挂那一刻存档**、之后读档 ⇒ 互锁记忆丢失 ⇒ 后车当场开走、前端还在站台上 ⇒ 两台机器一帧不对就分叉。**改法=把这个记忆也打包**（结构很小，全是不含指针的小字段）。

### E 组 · 站台上「等待挂接」的车底别乱订路（第 8 项）

**场景**：站台上停着一列车底、订单是「等待挂接」。它停稳后引擎偶尔仍会替它算一条出站路线（含尽头路点）⇒ 铁轨上黑色预留预览**闪一下又消失**（第 127 轮玩家报的观感问题）。修法是在订路入口加门禁拒绝它，**第 8 项（`_r3r_waiter_purged`）就是「这台车我已拒绝过，别重复拒绝」的去重集合**。

- **症状**：若拒绝动作本身不幂等（每帧都真做一次），去重集合在不在就影响「做了几次」，而拒绝/清扫会动地图 ⇒ 两端时机不同。**改法：确认清扫幂等后直接去掉这个集合**（每帧无条件拒绝）。

### F 组 · 确认不用管的（第 9 项）

- 活车快照 `_r3r_live_vehicle_cache`：只回答「这台车还活着吗」，内容由存档里的车推出 ⇒ 天然一致（已结案）。
- 各种「这条日志/新闻打印过没有」的去重集合：只影响打不打印，不影响车怎么开。
- 调试节流、`_r3r_pair_scan_tick`（本来就是按帧数）、`_r3r_resv_audit_due`（由玩家命令置位，命令是同步的）。

### G 组 · 读档时的一次性动作（第 10 项）

**场景**：这几项都不在游戏过程中每帧跑，而是「**按下读档那一刻做一次**」。

- 旧档里「焊死铰接」的段链自动打散（`LEGACY-ARTIC-DEARTIC`）——若不幂等，读第二次会再打散、或该打散的没打散。
- 读档重建段行（`R3RSettleChainSegments("load")`）会**重发段号**——与第 3 项是同一件事的两面，改完第 3 项这里才真确定。
- 车库拖动形成的「车号/车名借用」读档后要补上（`DEPOT-XFER-ID load`）。
- **为什么单列**：这是「服务器跑一段 + 客户端读档」的**第二条独立分叉路径**，不能因为「推演看着没问题」就跳过。

**一句话收口**：A/B/C/D/E 五组是「运行中每帧都要做的判断」，G 组是「读档那一下做的动作」，F 组是不用做的。

---

## 十四、玩家拍板（2026-10-16）

1. **十项应用场景已过目，无异议**（§十三）。
2. **先做 (C)** —— 即 **第 1 项「预留台账」走 (C) 入档**（§12.3 问题 1 的选项 C），落地为 **批 1 = 第 1 项 (C) 入档 + 第 2 项 tick 化**（第 2 项必须排在第 1 项之后，见 §12.1 第 2 项）。
3. 「然后再做别的」= 批 1 完成后按 §12.2 批次表继续（批 2 → 批 3 → 批 4）。
4. **测试手段**：玩家拟用「**一台机器两个进程**」做联机自测（见 §十五）。

> 状态：§十五 的双进程方案在落盘时为**草稿（含待取证项）**，取证结果在同节回填。

---

## 十五、单机双进程联机自测方案（2026-10-16）

### 15.1 为什么这条路径是对的

要复现的本轮主题是「**主机跑一段 + 客户端读档/中途入局**」的分叉（§A 组第 1 项、G 组第 10 项）。单机双进程恰好直接覆盖这条路径，因为：
- 客户端**中途加入/读档**时，OpenTTD 会把主机当前存档**整份传给客户端** ⇒ 走的就是「主机已跑一段、客户端从存档冷启动」这条真实链路（正是台账为空/段 ID 号池不同这类 bug 的触发条件）。
- 本地回环延迟极低 ⇒ **去掉网络噪声**，剩下的不一致就是真的状态不一致，判读干净。
- 两端同 exe / 同 GRF ⇒ 前端差异归零。

### 15.2 三个坑与结论（**2026-10-16 已实证**）

| # | 坑 | 结论 | 锚点 |
|---|---|---|---|
| P-1 | 两端日志互相覆盖 | **真坑，必须处理**：R3R 探针日志**全部**用相对路径 `fopen("R3R_debug.log","a")` ⇒ 落在**进程 CWD**（不是 exe 目录、也不是用户目录）。两进程 CWD 相同（典型：都双击同一个 exe）⇒ 写同一文件、两端行交错 ⇒ **无法判读**。对策 = 让两进程 CWD 不同 | `src/r3r_perf.h:201-204`、`:224-236`；`src/train_cmd.cpp:6982`/`:6997`；`src/sl/couple_group_sl.cpp:161/195/282/310`；`src/openttd.cpp:1836` |
| P-2 | 两端配置隔离 | **可做**：`-c <file>` 指定配置文件；另有 `-g`/`-D`/`-n`/`-p`/`-d`（表见 15.4-2） | `src/openttd.cpp:841` 等 |
| P-3 | desync 检测 | **可做**：`DebugLevel` 有 `desync` / `yapfdesync` ⇒ `-d desync=3`；另有引擎级**命令日志**（CommandLog）可 diff 两端、直接指出第一条分歧命令 | `src/debug.h:44-45`、`:112-114`；`src/command_log.h` |

### 15.3 取证清单（本轮已完成）

- [x] T-1：`R3R_debug.log` 写出路径 ⇒ **相对 CWD**（见 15.4-1）。
- [x] T-2：`src/openttd.cpp` 命令行参数表（见 15.4-2）。
- [x] T-3：desync 级别与命令日志（见 15.4-3）。

### 15.4 取证结果（2026-10-16）

**15.4-1 日志路径 = 相对 CWD（P-1 的答案，最关键）**

- 全树 R3R 探针日志的打开方式都是 `fopen("<相对文件名>", "a")`：
  - `src/r3r_perf.h:201-204`：`R3RFopenDbg(mode)` ⇒ `fopen("R3R_debug.log", mode)`
  - `src/r3r_perf.h:224-236`：`R3RDbgWrite()` 核心写点 ⇒ `fopen("R3R_debug.log", "a")` + `vfprintf` + `fclose`
  - `src/train_cmd.cpp:6982`：`fopen("R3R_debug.log", "rb")`（只取体积）；`:6997`：`fopen("R3R_perf.log", "a")`
  - `src/sl/couple_group_sl.cpp:161 / :195 / :282 / :310`：写档/读档留底走同一文件名
  - `src/openttd.cpp:1836`：诊断循环也走同一文件名
- ⇒ `fopen` 的相对路径解析基准是**进程当前工作目录（CWD）**，与 exe 所在目录无关。
- ⇒ **两进程若 CWD 相同，R3R_debug.log / R3R_perf.log 会被同时追加，两端行交错且无来源标记**（探针行里没有"我是哪端"的字段）⇒ 判读直接失效。
- ⇒ **对策（必做其一）**：
  - (a) **把 openttd.exe 复制到两个目录**（如 `build\srv\`、`build\cli\`），各自从自己目录启动 ⇒ CWD 天然分开（**最稳，推荐**）；
  - (b) 用 `Start-Process -WorkingDirectory <不同目录>` 显式指定 CWD（注意本环境既有坑：`Start-Process` 传含空格的绝对路径要用 `cmd.exe /c` 包一层）。
- 附注：`R3R_perf.log` 见既有备忘记载"由 exe 写在自身所在目录"——本轮取证显示同一份代码里就是相对名 ⇒ 实际取决于 **CWD**；两种说法在"CWD == exe 目录"时等价，复制 exe 到两目录的做法在两种口径下都成立。

**15.4-2 命令行参数（`src/openttd.cpp`，逐条核到行）**

| 参数 | 语义 | 锚点 |
|---|---|---|
| `-c <file>` | **指定配置文件**（⇒ 两端配置隔离） | `:841` |
| `-g <sav>` | 启动即加载存档 | `:759-761` |
| `-D` | dedicated 专服（`videodriver="dedicated"`、music/sound 置 null） | `:714-717` |
| `-n <host:port#company>` | **客户端直连**（`connection_string`） | `:726-728` |
| `-p` | 密码 | `:729` |
| `-d <level>` | 调试级别（Windows 下 `CreateConsole()` 开控制台） | `:743-758` |
| `-f` | `_dedicated_forks = true` | `:725` |
| `-x` | 不保存配置 | `:842` |
| `-G <seed>` | 地图生成种子 | `:834-840` |
| `-v` / `-s` / `-b` | video / sound / blitter 驱动 | `:711-713` |

**15.4-3 desync 检测与命令日志**

- `DebugLevel` 中独立存在 `desync`、`yapfdesync` 两级：`src/debug.h:44-45`；配套 API `ClearDesyncMsgLog` / `LogDesyncMsg` / `DumpDesyncMsgLog`：`:112-114` ⇒ **`-d desync=3` 为可用开关**。
- 引擎另有**命令日志**（`CommandLog`）：`src/command_log.h` 及 `src/command.cpp` / `src/console_cmds.cpp` / `src/misc.cpp` / `src/openttd.cpp` / `src/network/network_admin.cpp` 等 11 处引用 ⇒ 可记录两端**每条已执行命令**，**diff 两端命令日志 = 直接指出第一条分歧命令**（比翻探针日志强得多）。
- **启用门控条件尚未取证**（见 15.5 U-A）。

**15.4-4 推荐自测姿势（示意，路径按本地调整）**

- 目录：`build\srv\openttd.exe`、`build\cli\openttd.exe`（复制，保证 CWD 与日志分离）。
- 主机：`openttd.exe -D -c srv.cfg -g <存档>`（专服，日志 `build\srv\R3R_debug.log`）。
- 客户端：**中途加入**（主机跑一段后再执行）`openttd.exe -c cli.cfg -n 127.0.0.1:3979#0` ⇒ 走"整份存档传客户端"的冷启动链路，正是 §A 组第 1 项 / G 组第 10 项要打的分叉路径。
- 判据优先级：① 引擎 desync/命令日志（权威，指"有没有分歧、第一条分歧命令是啥"）→ ② R3R 探针按**同 tick** 对齐两端（辅助，指"分歧发生在哪个机制"）。
- **反例提醒**：两端都从 0 开始同时开跑，**不容易**暴露本轮这类"台账为空/号池不同"的问题；必须让客户端走"读档/中途入局"。

### 15.5 待确认（下一轮）

- U-A：命令日志（`CommandLog`）的启用方式与输出文件名/路径。
- U-B：用 `-c` 指定不同 config 时，`save/`、`screenshot/` 等用户目录是否仍共用（若共用，两端避免同名存档）。
- U-C：OpenTTD 启动时是否会把 CWD 切到 exe 目录（决定 (a)(b) 两种对策哪个必要；无论答案如何，「复制 exe 到两目录」都成立）。

---

## 十六、第 1 项 (C) 入档 —— 开工（2026-10-16）

玩家 2026-10-16 确认「现在开始」。范围 = 批 1 前半：**预留台账入档**（第 1 项）。

### 16.1 实施清单（滚动更新）

- [ ] **I-1 取证**：台账结构 + 全部读写点（`_r3r_resv_by_owner` 一族）
- [ ] **I-2 取证**：R3VP / R3SG 稀疏块写法模板（`src/sl/couple_group_sl.cpp`）
- [ ] **I-3 设计**：字段集 + 块 ID + 版本策略 + 读档重建时机
- [ ] **I-4 实现**：新增稀疏块 + 读档重建函数
- [ ] **I-5 取消**「读档期写入不记账」的旧例外（否则刚恢复的台账会被再撕一次）
- [ ] **I-6 构建自证**（复用 `_tmp_inc_build.cmd`，不新建 .cmd）
- [ ] **I-7 双进程联机复测**（按 §十五）

### 16.2 硬约束（开工前写死，避免走偏）

1. **优先后端不动**：能只改 `.cpp` 就只改 `.cpp`（改 `src/*.h` 会触发 KI-183 护栏判全量 692 步 ≈20 分钟）。
2. **不动既有预留判定语义**：本项只解决「台账从存档来」；第 2 项（老化/收割）另做。
3. **旧档必须能读**：走 `CH_SPARSE_TABLE` 自描述头，旧档无此块 ⇒ 取默认（空台账），**不升 SLV 版本号**。
4. **读档重建必须晚于列车建立**：台账 key 是车号（VehicleID）⇒ 必须在车辆池就绪之后落地，且与 `R3RPurgeStrayReservations` 的时序要对齐（否则读档首帧把真主人的位当孤儿）。

### 16.3 取证结论（本轮回填）

**16.3-1 台账不止一本（关键岔口，`src/train_cmd.cpp` 实测）**

| 对象 | 定义/锚点 | 语义 |
|---|---|---|
| `struct R3RResvRec { TileIndex tile; Track track; }` | `train_cmd.cpp:14926-14929` | 一条「我订了这格的这个轨」 |
| `static std::unordered_map<uint32_t, std::vector<R3RResvRec>> _r3r_consist_resv;` | `:14931` | **按「车」记**的台账：key = 记账时的调用者车号（= 链头），value = 它订下的格列表 |
| `static uint8_t _r3r_consist_resv_gc_clock;` | `:14932` | 该台账的 GC 计数 |
| `R3RIsReservationHolder(const Train*)` | `:14936` | 判定「这列车是"故意持有"站台预留的吗」（与平台等待者门禁同源） |
| `static std::unordered_map<uint32_t, uint8_t> _r3r_resv_stray_age;` | `:15045`（**函数内 static**，在 `R3RReservationAudit()` 里） | 孤儿位老化计数器：key = `(tile.base() << 3) \| track`，槽 6 保留、**槽 7 = 整格（站台/路点）**（常量 `R3R_RESV_WHOLE_TILE_KEY = 7`，`:15044`） |
| `R3R_RESV_REAP_AUDITS = 3` | `:15041` | 老化几次后释放 |
| `R3RResvLedgerOwner(tile, track)` / `R3R_RESV_NO_OWNER` | `:15120-15130`、`:15173` | **另一本按「格」记的台账（ledger）**：给定格+轨问车主 |
| `R3RResvLedgerForget(VehicleID::Invalid(), "resync")` | `:15262` | 每次审计横扫末尾「把 ledger 与地图对齐」（KI-363 Mirror law；借「无事可忘」这条入口做 resync，注释自陈**为了不动 `pbs.h`、保住增量构建**） |
| `_r3r_resv_audit_has_pending` | `:15266` | 下次审计是否要提早回来 |

⇒ **结论：要入档的是"一本还是两本"必须定下来**。`_r3r_consist_resv`（按车）与 `R3RResvLedger*`（按格）是**同一事实的两个索引**；如果两者都能从「存档里的格 + 存档里的车」重建，则**只入档一个**（更小的那个）就是最优解。这是本项**第一个设计岔口**，下一轮先证 ledger 的定义与全部 mutator。

**16.3-2 现成模板：`src/sl/couple_group_sl.cpp` 的 R3VP（照抄四件套）**

- 描述表：`static const NamedSaveLoad _r3r_park_desc[] = { NSL("orders_backup", SLE_REF(...)), NSL("unitnumber_backup", SLE_VAR(...)), ... }`（`:117` 起，字段用 `NSL("名", SLE_XXX(...))`）
- **读**：`SaveLoadTableData slt = SlTableHeaderOrRiff(_r3r_park_desc);` → `while ((index = SlIterateArray()) != -1)` → `Vehicle::GetIfValid(index)` → `SlObjectLoadFiltered(v, slt)`（`:154-183`）
- **写**：`SaveLoadTableData slt = SlTableHeader(_r3r_park_desc);` → 遍历 `Vehicle::Iterate()` 并**按需过滤**（`if (!R3RHasParkState(v)) continue;`）→ `SlSetArrayIndex(v->index); SlObjectSaveFiltered(v, slt);`（`:185-216`）
- **指针阶段**：`SlPrepareNamedSaveLoadTableForPtrOrNull(desc)` + `SlObjectPtrOrNullFiltered(...)`（`:218-230`）
- **注册**（`:341-346`）：
  ```
  { 'R3VP', Save_R3VP, Load_R3VP, Ptrs_R3VP, nullptr, CH_SPARSE_TABLE },
  { 'R3SG', Save_R3SG, Load_R3SG, Ptrs_R3SG, nullptr, CH_SPARSE_TABLE },
  ```
  ⇒ 新块只需**再加一行**到 `couple_group_chunk_handlers[]`（4 字符块 ID，如 `R3RL`）。
- 关键性质：`CH_SPARSE_TABLE` + `SlTableHeader` **自描述**，旧档无此块 ⇒ 各字段取默认值 ⇒ **不升 SLV 版本**（与 16.2 约束 3 一致）。

**16.3-3 已知陷阱（抄模板时必须避开）**

- 台账 key 用的是**裸车号**（`uint32_t`，来自 `v->index.base()`），存档里写 `VehicleID` 更稳（`SLE_VAR(...) / SLE_UINT32` 亦可行，但照 `R3VP` 的 `SLE_REF(REF_ORDERLIST)` 惯例，只写「不含指针的小字段」最安全）。
- `_r3r_resv_stray_age` 现在**在函数里 static**（`:15045`）⇒ 要入档必须**提到文件级**（否则 sl 代码看不见它）。
- 台账里**只有 `tile` + `track` 两个字段**（`R3RResvRec`），没有车号 ⇒ 入档时车号来自 map 的 key，需在 Save 时把 key 一起写进去（`SlSetArrayIndex(key)` 即可，与 `R3VP` 用 `v->index` 当数组索引同一手法）。

**16.3-4 ledger 的真相（第二轮，`src/pbs.h` 实证）——第 1 项的靶心**

台账的真名是 **ledger**，全部 API 在 `src/pbs.h:92-108`：

| 声明 | 锚点 | 语义 |
|---|---|---|
| `inline constexpr uint32_t R3R_RESV_NO_OWNER = UINT32_MAX;` | `:95` | 「无主」哨兵 |
| `void R3RResvLedgerNote(const char *kind, TileIndex tile, Track track);` | `:98` | 记/销一笔；**由地图 setter 经 `R3RResvWatchLog` 调用** ⇒ 注释自陈"no call site can be forgotten"（`pbs.h:31-32`）；无 actor 时 no-op |
| `uint32_t R3RResvLedgerOwner(TileIndex tile, Track track);` | `:101` | 给定格+轨问车主 |
| `void R3RResvLedgerForget(VehicleID owner, const char *why);` | `:104` | 释放该车名下全部位并删行 |
| `void R3RReleaseReservationSafe(uint32_t key, TileIndex tile, Track track, const char *why);` | `:108` | 释放前自证"这格仍是我的" |

**决定性一句（`pbs.h:99-101` 注释原文，逐字）**：

> "Ledger owner of one bit, or `R3R_RESV_NO_OWNER` **when it was never booked (e.g. it came in with a savegame)**."

⇒ **ledger 是纯内存态（NOSAVE）；随存档进来的预留位在 ledger 里一律是「无主」**。这就是第 1 项要修的缺陷本身，**已被代码自己的注释自证**——不需要再推演。
⇒ 连带后果：读档后审计（`train_cmd.cpp:15120-15130`）拿到 `lowner == R3R_RESV_NO_OWNER` 时，对**活车仍站在格上的位**还能靠 `walk` 判有主；但对**车已开走的位**就会走 `ledger_absent`（`:15165`，要求 `!bit_on_some_path`）⇒ 判为 stray ⇒ 3 次审计后 `RESV-REAP` 释放 —— **两端"数到第几次"不同即分叉**（与 §十三 A 组的推演一致）。

**权威性判定**：`pbs.h:36-40` 自陈 ledger "is a real fix rather than a probe"、"books **every** reservation bit under the chain head that laid it down, and it is fed from the very same hook"，且审计末尾把 `R3RResvLedgerForget(Invalid, "resync")` 当「与地图对齐」入口用（`train_cmd.cpp:15262`）。
⇒ **入档主体 = ledger（按格 → 车主 VehicleID）**；`_r3r_consist_resv`（`train_cmd.cpp:14931`，按车 → 格列表）是 KI-203 的"精确释放"辅助索引，下一轮确认它能否由 ledger 反推（若能，则**只入档一份**）。

**命名更正**：备忘 §十二/§十三 里我口述的 `_r3r_resv_by_owner` **不是代码里的名字**，真容器定义在 `src/pbs.cpp`（下一轮取证）。

**16.3-5 头文件约束的冲突与规避路线（设计决策，记下来免得重复推演）**

- ledger 的容器定义在 `pbs.cpp` 内。若它是 `static`/匿名命名空间，则 `couple_group_sl.cpp` 里的 sl handler **看不见它** ⇒ 常规做法要往 `pbs.h` 加访问器 ⇒ **改 `.h` ⇒ KI-183 护栏判全量 692 步（≈20 分钟）**。
- **规避路线（优先试）**：把 `Save_R3RL` / `Load_R3RL` **整个实现在 `pbs.cpp` 内部**（该文件已 include saveload 相关头），`couple_group_sl.cpp` 只在 handler 表里加一行注册 + **手写 extern 函数声明**（`void Save_R3RL(); void Load_R3RL();`）⇒ **一个头文件都不碰，增量构建保住**。
- 只有在这条路线被 pbs.cpp 的实际结构挡住时，才退到「改 `pbs.h` + 全量重编」，并**事先告知玩家**。

**16.3-6 ledger 的容器与两张表（第三轮，`src/pbs.cpp` 实证）——入档字段就此定死**

```146:150:src/pbs.cpp
struct R3RResvLedgerRec {
	TileIndex tile;
	Track track;
	uint8_t purposes;  ///< bitmask of R3RResvPurpose, i.e. why the bit is held
};
```

- 目的位枚举（`pbs.cpp:153-157`）：`R3R_RESV_PURPOSE_PATH = 0x01`（探路/耦合路径预留）、`R3R_RESV_PURPOSE_CONSIST = 0x02`（车正站在上面的格）、`R3R_RESV_PURPOSE_ALL = 0x03`（车一走就无人持）。**这一位必须入档**：它是「这格算路径还是算车底」的判据，事后重建不出来（Release law 靠它决定释放掩码，见 `:244-251`）。
- **两张表**（`pbs.cpp:159-160`）：
  - `static std::unordered_map<uint32_t, std::vector<R3RResvLedgerRec>> _r3r_resv_by_owner;` ← **与我在 §十二/§十三 口述的 `_r3r_resv_by_owner` 一字不差**
  - `static std::unordered_map<uint32_t, uint32_t> _r3r_resv_owner_of_bit;`（反向索引：bit_key → owner）
- `R3RResvBitKey(tile, track)`（`pbs.cpp:162-166`）：`(tile.base() << 3) | slot`，其中 `slot = (track == INVALID_TRACK) ? 7 : track` ⇒ 与 `train_cmd.cpp:15044` 的 `R3R_RESV_WHOLE_TILE_KEY = 7` **同一套编码**（两处独立实现、编号一致，属刻意对齐）。
- 车主身份（`pbs.cpp:171-177`）：`R3RResvLedgerActorHead()` = `Vehicle::GetIfValid(r3r_resv_actor)` 的 `->First()->index.base()`；无 actor ⇒ `R3R_RESV_NO_OWNER`（`pbs.h:42-44` 的 `r3r_resv_actor` 是 VehicleID 不是指针）。
- **决定性简化**：`_r3r_resv_owner_of_bit` 是 `_r3r_resv_by_owner` 的**纯派生索引**（前者 = 后者的 (tile,track) 反查），代码里两者恒同增同删（`R3RResvLedgerDropBit` 先查反向表拿到 owner，再删正向行 + 反向条目，`:254-261`）。
  ⇒ **入档只需写 `_r3r_resv_by_owner` 一张表**；读档时遍历它重建 `_r3r_resv_owner_of_bit`。§16.3-1 的「一本还是两本」岔口**关闭**。
  ⇒ 另一本 `_r3r_consist_resv`（`train_cmd.cpp:14931`）是 KI-203 的"按车精确释放"辅助，**与 ledger 无关、同样只是内存态**，需单独判定是否入档（下一轮）。
- **入档字段集（草案，三字段）**：`owner`（车号，来自 map 的 key）+ 每条 `{ tile, track, purposes }`；`INVALID_TRACK` 用 slot 7 编码（或直接存 `0xFF`）。
- **必改的设计声明**：`pbs.h:90-94` 原文 —— "The tables are in-memory and **not saved**: after loading they are empty and the ownership question falls back to the walk plus the audit" ⇒ 第 1 项要修的就是这一句所描述的行为。
- 记账节流（`pbs.cpp:265-280`）：`R3R_LEDGER_RESYNC_NOTES = 8192`，`R3RResvLedgerNote()` 每 8192 次写调用一次 `R3RResvLedgerResync()`（Mirror law 的全表校验）。
- 地图 setter 六个钩子（`r3r_resv_probes.h:13-19`）：plain rail / depot / crossing / station+waypoint / tunnel / bridge head，全部经 `R3RResvWatchLog()` ⇒ **读档重建不会经过这些钩子**（读档直接写 `_m` 字节），这就是必须入档的原因。

**16.3-7 ledger 的六条法律 / 全部读写点（第四轮，`src/pbs.cpp:271-510` 逐行实证）**

| 函数 | 锚点 | 行为（与入档直接相关） |
|---|---|---|
| `R3RResvLedgerNote(kind,tile,track)` | `:271-328` | `CLEAR-*` ⇒ 只删（`:287-293`）；`SET-*`/`CHG-*` ⇒ **`owner == R3R_RESV_NO_OWNER` 时直接 `return`，不记账**（`:295-296`）；同 owner 同 bit ⇒ `row.purposes \|= purpose`（`:312`，不 return） |
| `R3RResvLedgerOwner` | `:330-334` | 反查 `_r3r_resv_owner_of_bit`，缺行 ⇒ `R3R_RESV_NO_OWNER` |
| `R3RTrainForLedgerBit` | `:346-354` | 车号 → `Train::GetIfValid`；注释自陈"车道已卖"只属保险检查 |
| `R3RReleaseReservationLedgerChecked` | `:369-436` | 释放前问 ledger 归属；`booker != key` 且不指向同一链头 ⇒ 拒绝并记 `RESV-GC ... notowner`（`:376-389`）；站台整格：位已不在 ⇒ 删行返回 true（`:393-399`）、有车/有主 ⇒ 保留（`:401-409`）、否则释放（`:410-418`） |
| `R3RResvLedgerForget(owner,why)` | `:438-500` | `owner == Invalid` ⇒ **转 `Resync()`**（`:440-448`，审计借这条入口做对齐）；否则把该 owner 的行整体 `std::move` 出来、按 mask 逐行重判：`left = purposes & ~mask`，**非零 ⇒ 行保留**（`:468-473`）；`R3RReleaseReservationLedgerChecked` 返回 false ⇒ **行保留**（`:479-486`）；成功释放 ⇒ 删反向条目（仅当仍指向自己，`:490-494`）；末尾 `RESV-LEDGER-FORGET ... released= kept=` |
| `R3RResvLedgerResync()` | `:505-`（本次读到 `:510`，实现续下） | **只删不加**：遍历 `_r3r_resv_by_owner`，地图上该 bit 已不在 ⇒ 删行 |

**对第 1 项的三条硬结论（决定实现形态）**

1. **「台账随存档变空」的直接代码原因**：`R3RResvLedgerNote()` 在无 actor 时 `return`（`:295-296`）。读档期直接写 `_m` 字节、且没有 actor ⇒ 一条都不记。**不是设计疏漏，是 `pbs.h:90-94` 明确写的设计**（"in-memory and not saved"）。
2. **`Resync()` 救不了这个缺口**：它**只删不加**（`:505+`），天生无法把"地图上有、表里没有"的位补回来 ⇒ 入档是不可替代的；两者互补（入档负责恢复，Resync 负责清理漂移）。
3. **`purposes` 位就是"防再撕"的抓手**：读档恢复的行若带 `CONSIST` 位，则 `couple`/`flip`/`depot-edit`/`free-path` 这四类释放（mask = `PATH`，`:244-251`）**只会摘掉 PATH 位、行与 CONSIST 位保留**（`:468-473`）。
   ⇒ §16.2 约束 5 的措辞要改：**不是"取消读档期不记账"，而是"读档重建直接写两张表"**（绕过 `Note()` 的 actor 门），因为放行 actor 门会让读档期的整图写入都进台账，反而制造假行。语义以 16.3-7 为准，覆盖 16.2-5。

**16.5 设计定稿（2026-10-16，玩家「按你推荐的来」授权）**

- **范围**：只入档 ledger 的**正向表** `_r3r_resv_by_owner`（`pbs.cpp:159`）⇒ 决策 = **一本**。
  - `_r3r_resv_owner_of_bit`（`pbs.cpp:160`）：**不入档**，Load 时遍历正向表重建（纯派生索引，见 16.3-6）。
  - `_r3r_consist_resv`（`train_cmd.cpp:14931`）、`_r3r_resv_stray_age`（`train_cmd.cpp:15045`，函数内 static）：**本轮不动**（面变大、且不属于「台账为准」这一缺口）。
- **块 ID**：`R3RL`，`CH_SPARSE_TABLE`，在 `src/sl/couple_group_sl.cpp` 的 handler 表加一行。
- **挂点问题消解（关键，省掉一整轮取证）**：Save/Load **全部实现在 `pbs.cpp` 内** ⇒ Load 时直接往两张表里填，**不需要任何外部读档挂点**。`R3RRebuildCouplePriorities()`（`train_cmd.cpp:5895`，全仓唯一调用点 = `src/sl/vehicle_sl.cpp:529` 的 `if (part_of_load) R3RRebuildCouplePriorities(t);`，确认只在读档跑）**不必改、也不必调用**。
- **不校验车辆存在**：ledger key 是裸车号；`R3RTrainForLedgerBit()`（`pbs.cpp:346-354`）已有 `Train::GetIfValid` 兜底 ⇒ Load 期既不必也**不能**依赖车辆池已建立。
- **不在地图校验**：「表里有、地图上没有」的漂移交给**既有** `R3RResvLedgerResync()`（`pbs.cpp:505-539`，只删不加）；它有两个既有触发点：每 8192 次 ledger 写（`pbs.cpp:277`）与审计末尾 `R3RResvLedgerForget(Invalid,"resync")`（`train_cmd.cpp:15262`）⇒ **无需新挂点**。
- **`pbs.cpp` 当前无 saveload include**（`:10-28` 实测：stdafx / debug / viewport_func / vehicle_func / newgrf_station / follow_track / tracerestrict / newgrf_newsignals / train_speed_adaptation / bridge_signal_map / r3r_perf / train + table/strings.h）⇒ 需**新增 include**；这属 `.cpp` 改动，**不改任何 `src/*.h` 的 mtime ⇒ 不触发 KI-183 护栏**，走增量构建。
- **字段集**：`SlSetArrayIndex(owner)` 承载车号；行 = `{ tile, track, purposes }`。`INVALID_TRACK` 直接写 0xFF（读回时非 0xFF 才当 track 用，与 `R3RResvBitKey` 的 slot7 编码解耦，避免依赖实现细节）。
- **保持既有语义**：`purposes` 位（`R3R_RESV_PURPOSE_PATH/CONSIST`）必须往返（16.3-7 结论 3：靠它才能在 `couple`/`flip`/`depot-edit`/`free-path` 释放时只摘 PATH、保住 CONSIST）。

**16.6 实现蓝图（模板实证 + 逐行写法，下一轮照此开写）**

**模板实证（`src/sl/couple_group_sl.cpp`）**
- `#include "saveload.h"` 是该文件在 `src/sl/` 下的相对写法；**非 sl 目录一律 `#include "sl/saveload.h"`**（实证：`console_cmds.cpp:17` / `date.cpp:21` / `openttd.cpp:27` / `settings.cpp:58` 等）⇒ `pbs.cpp` 用后者。
- 描述表：`NSL("名", SLE_VAR(结构, 成员, SLE_UINTxx))`（`:24-33`）。
- Load 骨架：`SaveLoadTableData slt = SlTableHeaderOrRiff(desc);` → `while ((index = SlIterateArray()) != -1)`（`:35-44` / `:154-183`）。
- Save 骨架：`SaveLoadTableData slt = SlTableHeader(desc);` → `SlSetArrayIndex(x); SlObjectSaveFiltered(obj, slt);`（`:46-54` / `:185-216`）。
- 注册表 `:341-346` 现状：
  ```341:346:src/sl/couple_group_sl.cpp
  extern const ChunkHandler couple_group_chunk_handlers[] = {
  	{ 'CGPP', Save_CGPP, Load_CGPP, nullptr, nullptr, CH_TABLE },          // Couple Group Pool chunk
  	{ 'CGVR', Save_CGVR, Load_CGVR, nullptr, nullptr, CH_SPARSE_TABLE },   // Couple Group Vehicle mapping chunk
  	{ 'R3VP', Save_R3VP, Load_R3VP, Ptrs_R3VP, nullptr, CH_SPARSE_TABLE }, // R3R parked schedule / segment bookkeeping chunk
  	{ 'R3SG', Save_R3SG, Load_R3SG, Ptrs_R3SG, nullptr, CH_SPARSE_TABLE }, // R3R segment identity table chunk
  };
  ```
  ⇒ 追加第五行即可；`:348` 的 `_couple_group_chunk_handlers` 不动。

**关键设计选择：数组索引用 bit_key（唯一键），而不是 owner**

`_r3r_resv_by_owner` 是「一 owner → 多行」的多值映射；若拿 owner 当 `SlSetArrayIndex`，一个 index 就要序列化整个 vector（麻烦且易错）。而 bit_key（`pbs.cpp:162-166` 的 `(tile << 3) | slot`）**天然唯一** ⇒ 用它当索引，每行写一条 `{owner, purposes}`，`tile`/`track` 由索引反解。范围：`(2^24 << 3) | 7 ≈ 2^27 < 2^31` ⇒ `int` 索引安全。

- 新增（`pbs.cpp` 文件级）：
  ```cpp
  /** R3R: one row of the saved reservation ledger. The tile/track live in the
   *  array index (see R3RResvBitKey), so only the owner and the purpose bits
   *  have to be written. */
  struct R3RResvLedgerSaveRec {
  	uint32_t owner;
  	uint8_t purposes;
  };
  static const NamedSaveLoad _r3r_resv_ledger_desc[] = {
  	NSL("owner",    SLE_VAR(R3RResvLedgerSaveRec, owner, SLE_UINT32)),
  	NSL("purposes", SLE_VAR(R3RResvLedgerSaveRec, purposes, SLE_UINT8)),
  };
  ```
- Save（非 static，供跨 TU 注册）：遍历 `_r3r_resv_by_owner` 的两层，`rec = {it.first, row.purposes}`，`SlSetArrayIndex(R3RResvBitKey(row.tile, row.track))` + `SlObjectSaveFiltered(&rec, slt)`。
- Load（非 static）：先 `_r3r_resv_by_owner.clear(); _r3r_resv_owner_of_bit.clear();`，循环 `SlIterateArray()`，每行 `SlObjectLoadFiltered(&rec, slt)`，然后：
  - `owner == R3R_RESV_NO_OWNER` ⇒ 跳过（无主行无意义）；
  - `purposes == 0` ⇒ 归一为 `R3R_RESV_PURPOSE_ALL`（保守，与 `pbs.cpp:244-251` 的默认 mask 口径一致，避免「零用途行」被立刻摘掉）；
  - 反解 `tile = (TileIndex)((uint32)index >> 3)`、`slot = index & 7`、`track = (slot == 7) ? INVALID_TRACK : (Track)slot`；
  - 填两张表：`_r3r_resv_by_owner[owner].push_back({tile, track, purposes})` + `_r3r_resv_owner_of_bit[(uint32_t)index] = owner;`
- **`R3R_RESV_WHOLE_TILE_KEY = 7` 定义在 `train_cmd.cpp:15044`（不可跨文件引用）** ⇒ 在 `pbs.cpp` 内加 `static constexpr uint8_t R3R_RESV_SLOT_WHOLE_TILE = 7;` 并让既有 `R3RResvBitKey`（`:162-166`）也用它，做到「一处定义」。

**声明与注册（唯一要碰的第二个文件）**
- `src/sl/couple_group_sl.cpp` 顶部（`_couple_group_desc` 之前）加手写声明：
  ```cpp
  /* R3R: implemented in pbs.cpp next to the ledger itself -- keeping the
   * save/load code there means pbs.h does not have to grow an accessor (which
   * would force a full rebuild under the KI-183 guard). */
  void Save_R3RL();
  void Load_R3RL();
  ```
- handler 表追加：`{ 'R3RL', Save_R3RL, Load_R3RL, nullptr, nullptr, CH_SPARSE_TABLE }, // R3R reservation ledger chunk`

**明确不改（写死，防走偏）**：`src/pbs.h`（一个字不动）、`R3RRebuildCouplePriorities()`、`R3RResvLedgerResync()`、`R3RResvLedgerNote()`、`_r3r_consist_resv`、`_r3r_resv_stray_age`、SLV 版本号、`CH_SPARSE_TABLE` 之外的任何 chunk 标记。

**回退**：纯新增（4 处：pbs.cpp 的 include + 结构/描述表 + 两个函数、couple_group_sl.cpp 的声明 + 一行注册）⇒ 回退=删掉即可；旧档无 `R3RL` ⇒ 行为与今天逐字一致。

**自证判据**
1. 构建三件套（EXIT_CODE=0 / `[n/n] Linking CXX executable openttd.exe` / exe mtime 晚于 `pbs.cpp` + `couple_group_sl.cpp`）。
2. **块 ID 实证不能靠搜 exe**：`'R3RL'` 是四字符整数常量，不保证以文本落在二进制里 ⇒ 改用**存档文件字节**验证（新存档里应出现 `R3RL` 四字节）或 `read_lints` + 运行期日志。⇒ 登记为待验证项 V-1。
3. 行为判据交给 §十五 的双进程联机复测：**读档后台账不再是空的** ⇒ 不再出现「一端 reap、一端不 reap」的分叉。

### 16.4 下一步（第 1 项，待办更新）

- [x] I-1 台账取证（16.3-1 / 16.3-4 / 16.3-6 / 16.3-7）
- [x] I-2 稀疏块模板取证（16.3-2）
- [x] I-3a 入档范围：**只入档 `_r3r_resv_by_owner`**（`_r3r_resv_owner_of_bit` 由它重建）
- [ ] I-3b 定读档重建的**挂点**：候选 = `R3RRebuildCouplePriorities()`（既有记忆实证它"只被 `src/sl/vehicle_sl.cpp:521` 的 `part_of_load` 调用" ⇒ 天然只在读档跑）或 `AfterLoadVehiclesPhase2()`
- [ ] I-3c 定块 ID（拟 `R3RL`）与 `_r3r_consist_resv`（`train_cmd.cpp:14931`）是否一并入档
- [ ] I-4 实现（目标：**只改 `.cpp`**，不碰 `src/*.h`；save/load 实现在 `pbs.cpp` 内，`couple_group_sl.cpp` 只加注册行 + 手写 extern 声明）
- [ ] I-5 改 `pbs.h:90-94` 的"not saved"设计声明（**若必须改 `.h` ⇒ 全量 692 步，需先报玩家**）
- [ ] I-6 构建自证（复用 `_tmp_inc_build.cmd`）
- [ ] I-7 双进程联机复测（按 §十五）

---

## 十七、稳定点标记（2026-10-16，玩家要求「先执行一个标记稳定可回退版本的操作」）

**动机**：第 1 项（台账入档）要动 `pbs.cpp` 的 ledger，并**给存档新增一个块**。动手前先钉一个可回退点，避免"改坏了回不去"。

**基线（本轮实测读数，非记忆）**
- 分支 `feature/decouple`，HEAD = `d57768902f`，其上已有 tag `r3r-stable-2026-10-03`（= 上一个稳定点）。
- 工作区待入库：`src\` 下 **26 个**已改文件；根目录 2 个已改文档（`R3R_KNOWN_ISSUES.md`、`R3R_fake_dualhead_plan.md`）+ **22 个**未跟踪的 `R3R_*_memo.md`。
- 内测 exe：`build\openttd.exe` = **51 868 160 B**，mtime **2026-10-16 21:17:44**，SHA256 `01eba20ad35ab21d07f1126559a7788455e77223d6689a1ccfe76fc79b2450a8`。
- 与源码一致性：最新三个源文件 = `pbs.cpp`(10-16 21:13:59) / `yapf_rail.cpp`(10-16 21:01:33) / `train_cmd.cpp`(10-16 18:22:01) ⇒ **exe 晚于全部源码，快照有效**。
- 既有工作流脚本齐备（`R3R_fix_gbk.ps1` 2745 B / `R3R_inc_guard.ps1` 3731 B / `_tmp_inc_build.cmd` 1621 B / `R3R_fullbuild.cmd` 6796 B / `R3R_release_fullbuild.cmd` 10279 B）⇒ 本次**不新建任何脚本**。

**做法（沿用 09-29 / 10-03 两次先例，三重保险）**
1. `git commit`：源码 + 文档；
2. 新建稳定点备忘 `R3R_stable_20261016_memo.md` 再提交一次，`git tag -a r3r-stable-2026-10-16` 指向它（先例：tag 指向备忘提交）；
3. 离线快照 `build\r3r_stable_2026-10-16\`：`README.txt`（GBK）+ `openttd.exe` + `src\` 下全部改动文件（保持仓库相对路径）。

**回退方式**：`git checkout r3r-stable-2026-10-16 -- src`（**不用** `git reset --hard`）；或直接用离线快照目录覆盖 `src\` 与 `build\openttd.exe`。

**刻意不入库**（沿用先例清单）：所有 `_tmp_*` / `diag_*` / `cleanup*` / `start_batch_*` 临时脚本、散装一次性 `*.ps1`、`ninja` 可执行、`*.log` / `*.sav` / `*.zip`、`INCREMENTAL`、`R3R_KNOWN_ISSUES.damaged_20260915.bak`、0 字节畸形名 ` R3R_release_build.cmd`。
