# 第 260 轮：自动命令（auto range）语义重申与规格固化

输入：玩家 2026-10-21 口头重申（本轮唯一输入，非日志）。

## 一、玩家口径（原话拆解，逐字保留要点）

1. **记录源**：段在**非控制期间**「经由、停靠」的车站、车库、路点，都要同步写进**该段自己的**调度计划里。
   - 强调：**不是**读取控制段的计划并抄下它那部分，而是**自己非控制期间读脚下走过的路**。
2. **执行语义（本轮新增，关键）**：这些自动录入的命令**应当从不被执行**，只是「时刻表参考的载体」。
3. **跳过语义（本轮新增，关键）**：对于调度序列
   `a1 → 去c1挂车 → c2(auto) → c3(auto) → a2`
   该段执行完「去c1挂车」之后，**重新获得链的控制权时**，调度命令应当**跳过 c2 和 c3，直接指向 a2**。

## 二、我对需求的理解（待玩家确认）

- auto 条目是「该段的历史足迹」，位置固定在「重新拿回控制权后的那个入口点」之前，即夹在
  `GOTO_COUPLE/WAIT_COUPLE/DECOUPLE` 这类交接命令与其后的第一条真实命令 `a2` 之间。
- 拿回控制权时，`cur_real_order_index` 必须**越过整段 auto range** 落到 `a2`，而不是停在 c2。
- auto 条目**永远不进入执行**：即使列车在 auto 条目对应的站台上停着，也不能因为它而"到站→装货→等待"。
- 记录时机 = 「段不控制整列」的那段时间内每跨过一个站/车库/路点记一条；控制期间不记（那段由控制段自己的计划负责）。

## 三、待确认点（问玩家）

- Q1：拿回控制权后跳过 auto range 时，是否需要**保留** auto 条目的耗时（作为时刻表参考显示），还是连耗时也一并忽略、只当文本？
- Q2：一段里出现**多个**交接点（例如 c1 挂车后又去 c2 挂第二次）时，auto 条目是否按「每个交接点后各自一段 auto range」多点插入，还是全段只有一个 auto range？
- Q3：`a2` 本身可能是 WAIT_COUPLE（等待被再次挂走），此时 auto range 应在它之前还是之后？

## 四、取证

### 4.1 记录源 = 自己脚下（**已符合玩家口径**）

`src\train_cmd.cpp:5737 R3RAutoRecordChainOrders(Train *chain)`：
- `:5749` 取 `R3RGetSegmentHeads(chain)`，`:5756 if (seg == ctrl) continue;` ⇒ **只记非控制段**。
- `:5760 R3RAutoOrderForTile(seg->tile, &want)` ⇒ 读的是**该段自己车节所在的格**（`seg->tile`），
  不是控制段的订单。`R3RAutoOrderForTile`（:5641）分派到 `OT_GOTO_STATION/OT_GOTO_WAYPOINT/OT_GOTO_DEPOT`。
- 结论：**需求第 1 条（自己读脚下走过的路）现状已满足，不需要改。**

### 4.2 插入位置 = "刚执行完的那条命令之后"

`:5787-5798`：`count == 0` 时锚定 `begin = cur_real_order_index + 1`（`cur_real_order_index` 冻结值 +1）。

⇒ 在玩家例子 `a1 → 去c1挂车 → a2` 里，若该段的 `cur_real_order_index` 停在「去c1挂车」，
则 auto range 从 **a2 原来的位置** 开始插入，auto 条目把 a2 往后顶到 `begin + count`。
**位置与玩家期望一致**（c2、c3 夹在挂车与 a2 之间）。

### 4.3 执行侧：**完全没有跳过机制**（需求第 2、3 条未实现）

全库 `auto_begin|auto_count|auto_signature` 共 19 处命中，全部是：
- `train_cmd.cpp:5593-5595` 探针打印；
- `:5769-5837` 记录/重写/哈希；
- `:5856-5878 R3RAutoFinaliseChainOrders()` 解挂时裁尾；
- `couple_group.h:564-575` 字段声明；`sl/couple_group_sl.cpp:288-290` 存档。

**没有任何一处让订单索引跳过 auto range**。`R3RAutoOrderShiftIndex()`（:5694）只做"插入/删除后的索引平移"。
⇒ 段拿回控制权后 `IncrementRealOrderIndex()` 会停在 `begin`（= c2），**c2 会被真的执行**。

### 4.4 可仿照的既有"不可执行订单"范本

`src\train_cmd.cpp:8129 R3RSkipUnfireableDecoupleOrder(Train *v)`：循环
`const Order *o = v->GetOrder(v->cur_real_order_index); if (!o->IsType(OT_DECOUPLE)) break; v->IncrementRealOrderIndex();`
—— 用"索引一旦落在某类订单上就整段推开"的写法，与本需求同构。本轮的跳过逻辑照此实现。

### 4.5 猜测根因（待验证）

**猜测**：`auto range` 被设计成"可替换的一段，锚在 cur_real+1"，但设计时把"参考"与"可执行"混为一谈——
订单表里没有"不可执行"这个属性，索引自然推进就会踩进去。**支撑证据**：4.3 全库无跳过点。
**待验证判据**：改后在段拿回控制权的同一 tick，日志应出现 `AUTO-ORD-SKIP seg=.. from=<begin> to=<begin+count>`，
且 `cur_real_order_index == begin + auto_count`（= a2）。

## 五、实现方案（本轮落地）

1. 新增 `R3RIsAutoRecordedOrderIndex(const Train *, VehicleOrderID)`：仅当 `v->orders == R3RSegmentOwnOrderList(v)`
   （即正在执行**自己**的表）且索引落在 `[auto_begin, auto_begin+auto_count)` 时返回真。
2. 新增 `R3RSkipAutoRecordedOrders(Train *v)`：在真时把 `cur_real_order_index`、
   `cur_implicit_order_index`、`cur_timetable_order_index` 一起推到 `begin + auto_count`，返回跨过条数，并打一行探针。
3. 调用点（双保险）：
   - `TrainLocoHandler()` 入口每 tick 一次（代价 = 一次 `R3RSegmentGet` + 两个比较）；
   - 控制权交接提交点 `R3RSettleChainSegments()` 末尾（拿回控制权的瞬间就把指针指向 a2，UI 立刻正确）。
4. 不删除、不隐藏 auto 条目：它们仍是时刻表参考（玩家口径）。

## 六、第 261 轮：两个确认的答复（玩家 2026-10-21 拍板，逐字保留）

**Q2（一份计划里会不会出现多次交接）→ 会，且每处都要独立 auto 区间。**

玩家原话：「会出现多次，每处都要独立的 auto 区间(我需要把单个区间改成按交接点分段的多个区间（数据结构调整，工作量中等但结构更正确）)」

⇒ 规格变更：现有「单区间」模型（`auto_begin` + `auto_count`，见 `couple_group.h` 字段声明 / `train_cmd.cpp:5787-5798` 的 `count == 0` 锚定）**作废**，改为「按交接点分段的区间集合」：
- 一份 `OrderList` 上允许多个 auto 区间，每个区间锚在**某个交接命令**（`OT_GOTO_COUPLE` / `OT_WAIT_COUPLE` / `OT_DECOUPLE`）之后、该交接段之后的第一条真实命令之前。
- 区间按 `begin` 升序、互不重叠；插入/删除订单时**所有**区间一起做索引平移（现 `R3RAutoOrderShiftIndex()` 单区间逻辑要循环化）。
- 合法性判据从「一条链只有一个区间」放宽为「每个交接点最多一段」，重叠即视为脏数据（读档归一）。

**Q1（auto 条目耗时怎么处理）→ 保留显示，并计入总时长。**

玩家原话：「保留显示，也计入总时长(最简单；参考价值完整，但总时长会偏大（含不会实际走的耗时），会影响分离间隔的计算)」

⇒ 口径固化：
- auto 条目在时刻表窗口**照常显示** `travel_time` / `wait_time`，照常进 `UpdateTotalDuration()` 求和；
- **只**在**执行侧索引**上跳过（`cur_real_order_index` / `cur_implicit_order_index`，必要时 `cur_timetable_order_index` 不动——它要留作参考）；
- 禁止为了"看起来不误算"去清零 auto 条目的 `travel_time/wait_time`，也不要从总时长/分离间隔求和里剔除它们；
- 已接受的副作用：含 auto 条目的计划总时长偏大 ⇒ 分离（`VehicleFlag::SeparationActive`）计算出的间隔比实际运行偏大。这是玩家明确接受的代价，**不作为缺陷登记**。

**仍未答（沿用现状，不改默认）**：Q3 —— 当 auto 区间之后的 `a2` 本身就是 `OT_WAIT_COUPLE` 时，区间是否仍插在它之前。现实现（`begin = cur_real_order_index + 1`）落在它之前，在玩家未拍板前保持不变。

### 6.1 数据结构改造的落点清单（待实现，下轮执行）

1. 字段：`couple_group.h` 的 `auto_begin` / `auto_count` / `auto_signature` → 区间数组（容器选型须与存档格式一起定；`auto_signature` 用于"整段替换"判重，多区间下改为每个区间一份或整体一份需一并复核）。
2. 存档：`src\sl\couple_group_sl.cpp:288-290` 的字段读写 → 变长数组（注意旧档兼容：单区间旧档迁移为"一个区间"）。
3. 记录：`train_cmd.cpp` 的 `R3RAutoRecordChainOrders()`（:5737 起）—— 游标不再是"全局一个 cursor"，而要**按所在交接段定位目标区间**（:5787-5798 的 `count == 0` 锚定改成"找当前所在交接段对应的区间，没有则新建"）。
4. 平移：`R3RAutoOrderShiftIndex()`（:5694）单区间 → 全区间扫描。
5. 裁尾/定稿：`R3RAutoFinaliseChainOrders()`（:5856-5878）解挂时裁尾 → 逐区间裁。
6. 执行侧跳过：`R3RIsAutoRecordedOrderIndex()` 判据由单区间改为"落在任一区间内"；`R3RSkipAutoRecordedOrders()` 跳到**该区间的 end**（不是全局末尾）。
7. 探针：`AUTO-ORD` / `AUTO-ORD-SKIP` 行增加 `slot=` 或 `iv=[b,e)`，便于实机判读多区间。

### 6.2 落地记录（第 261 轮实现，2026-10-21）

**存储（`src\couple_group.h` + `src\sl\couple_group_sl.cpp`）**
- `R3RSegmentRecord` 新增固定 8 槽：`AUTO_RANGE_SLOTS = 8`、`auto_num_ranges`（高水位，不压缩）、`auto_ranges_begin[8]` / `auto_ranges_count[8]` / `auto_ranges_signature[8]`；`count == 0` 即自由槽。
- 旧三字段 `auto_begin/auto_count/auto_signature` **降级为纯兼容**：内存里除写档镜像外不再被任何判定读取。
- 存档无名版本迁移：`Load_R3SG()` 读到"`auto_count != 0` 且 `auto_num_ranges == 0`"（= KI-388 旧档）时把单区间折进槽 0，随后清掉旧三字段；`auto_num_ranges` 越界钳到 8。
- `Save_R3SG()` 写档前把**第一个 live 槽**镜像进旧三字段（旧二进制仍能读到一个单区间计划）。命名表新增 `auto_num_ranges` / `auto_ranges_begin|count|signature`，`NamedSaveLoad` 无名版本变更，旧档读到全 0 = 无区间。
- 槽的数组顺序 = **分配顺序**（不是按 begin 排序）：区间由锚点定位，顺序无意义；排序会让游标 map（键 = 段 ID + 槽号）跟着错位。原注释里"按 begin 升序"已改写。

**游标**：`_r3r_auto_order_cursor` 由 `map<seg_id,uint16>` 改为 `map<(seg_id<<8)|slot, uint16>`（`R3RAutoCursorKey()`）。

**记录（`R3RAutoRecordChainOrders()`）**
- 每轮先对**每个**槽做合法性/签名校验，失配的槽只丢自己的声明（`AUTO-ORD-DROP ... slot=`），玩家改过的订单原样保留。
- 锚点仍是 `cur_real_order_index + 1`（冻结索引 ⇒ 同一交接段内的每一格都落在同一槽里，区间只会变长）。
- 选槽：取"`begin <= anchor <= begin+count`"中 begin 最大的槽；没有则**先复用自由槽**，再考虑扩到高水位（`AUTO-ORD-FULL` 表示 8 槽耗尽）。
- 增删改走 `R3RAutoOrderErase/Append(..., row, slot, ...)`，内部 `R3RAutoRangesShift(ol,row,except,from,removed,inserted)` 对**其它所有槽**做平移并重算各自签名（`except` 避免本槽被二次平移）。
- 探针：`AUTO-ORD seg= head= slot= kind= dest= iv=[b,e) cursor=`。

**定稿（`R3RAutoFinaliseChainOrders()`）**：遍历该段所有槽的游标；非解挂提交点只把游标清 0；`decouple-v/u` 才按各自的游标逐槽裁尾（`AUTO-ORD-TRUNC ... slot=`）。`cursor == 0` 的槽不裁（本轮没碰过它）。

**执行侧（新增 `R3RAutoRangeFind()` / `R3RSkipAutoRecordedOrders()`）**
- 判据：`v->orders == R3RSegmentOwnOrderList(v)`（借用态自然不命中）+ 索引落在任一槽内。
- 跳过目标是**该槽的 end**（不是全局末尾），并支持连续相邻区间（最多 8 次循环）与环回（end >= 订单数 ⇒ 0）。只动 `cur_real_order_index` / `cur_implicit_order_index`；`cur_timetable_order_index` **不动**（KI-396：时刻表照常显示、照常计入总时长）。
- 跳过成立时按 `CmdSkipToOrder` 同款收尾：`IsAnyLoadingType() ⇒ LeaveStation()`（否则站点 loading 列表会留悬垂条目，KI-95）、`OT_WAITING ⇒ HandleWaiting(true)`、`current_order.Free()` + `SetDestTile(INVALID_TILE)`。
- 执行前先验签名，失配即判 `AUTO-ORD-STALE` 并丢槽（防止玩家在借用期改表后按旧偏移乱跳）。
- 调用点两处：①`R3RSettleChainSegments()` 末尾 `R3RAutoFinaliseChainOrders()` 之后（控制权刚回来的瞬间）；②`TrainLocoHandler()` 入口 `R3RAutoRecordChainOrders(consist)` 之后（兜底）。
- 探针：`AUTO-ORD-SKIP seg= head= slot= from= to= iv=[b,e) order=`、`AUTO-ORD-STALE`。

**顺带**：`SEGTR-SNAP` 的 `auto b=/c=/sig=` 改成 `auto n=<槽数> first=[b,e)`（旧字段已不维护，继续打印只会永远是 0）。

**未实现/边界**
- 8 槽上限：超过即停止记录（`AUTO-ORD-FULL`），不覆盖既有区间。
- 玩家在借用期插入/删除订单不会平移已存区间（`R3RAutoOrderShiftIndex` 只在记录路径的 erase/append 内生效）⇒ 签名校验只能抓"区间内部被改"，抓不到"区间之前插了单"导致的整体偏移。这是 KI-388 既有的已知边界。
- Q3（`a2` 本身是 `OT_WAIT_COUPLE` 时区间插在它前还是后）仍未拍板，沿用现状（插在前）。

### 6.3 构建自证（第 261 轮）

- 复用 `_tmp_inc_build.cmd`（未新建任何 .cmd）；`tasklist` 先确认 `openttd.exe` 未运行。
- 护栏：`GUARD: 2 header/lang file(s) are newer than the newest object file`（`src\couple_group.h` 13:41:54、`src\order_base.h` 01:00:15 与最新 obj 00:14:43 比）⇒ `REMOVED 620 object file(s) - upgrading this build to a FULL rebuild`，本轮因改 `couple_group.h` 必须全量。
- 结果：`[692/692] Linking CXX executable openttd.exe`；`build\R3R_incbuild.done` = `EXIT_CODE=0`（14:06:21）；日志 `error C*`/`fatal error`/`FAILED:`/`build stopped` 计数 **0**。
- 时间戳链：`src\train_cmd.cpp` 13:42:38 → `train_cmd.cpp.obj` 14:02:57 → `build\openttd.exe` 14:06:01（52 033 536 B）；`src\couple_group.h` 13:41:54 早于 obj。
- `read_lints`：`train_cmd.cpp` / `couple_group.h` / `sl\couple_group_sl.cpp` 三文件 0 条。
- exe 字面量核对（`findstr /M /C:`）：命中 `AUTO-ORD seg=%u head=%d slot=%u`、`iv=[%u,%u) cursor=%u`、`AUTO-ORD-SKIP seg=`、`AUTO-ORD-TRUNC seg=`、`AUTO-ORD-STALE seg=`、`AUTO-ORD-FULL seg=`、`AUTO-ORD-DROP seg=`、`auto n=%u first=[`。
- 状态：已实现 + 已编译，**游戏内复测待做**（判据 = KI-395 的 5 条）。

## 七、第 262 轮：玩家复测「自动添加的命令没有录入」

**玩家原话（2026-10-21）**：「我发现，测试中，自动添加的命令并没有成功录入调度命令」+「关于你的 Q3，我拍板保持现状」。

**Q3 已定（不改码）**：`a2` 本身是 `OT_WAIT_COUPLE` 时，auto 区间仍**插在它之前**。此前 §6.2「未实现/边界」里的待拍板项就此关闭。

**现象定性**：非控制段在借用控制段排程期间经过的车站，本应被 `R3RAutoRecordChainOrders()` 追加到它自己的表里（探针 `AUTO-ORD seg= head= slot= kind= dest= iv=[b,e) cursor=`），实测既没写进表的可见结果，也无从判读 —— 需先确认是「探针没打」还是「打了但写错位置」。

**疑点（先登记，待验证）**
- D-262-A：`R3RAutoRangeSlots(row)` 若在 `auto_num_ranges == 0` 时返回 0，则记录路径里凡是拿它当循环上界/夹紧的地方都会退化成空循环，槽永远不会被分配 ⇒ 区间永不产生。KI-395 之前用单一 `auto_count`，没有这个「槽数为 0」的中间态。
- D-262-B：旧三字段降级为镜像后，`R3RAutoOrderErase/Append`（新增 `row, slot` 参数）与 `R3RAutoRangesShift`（新增 `except`）的**全部**调用点是否都已按槽语义改对；编译通过不等于写对了槽号。
- D-262-C：`R3RAutoRecordChainOrders()` 的 `want` 采集依赖 `R3RSegmentOwnOrderList(seg) != nullptr` 与 `seg != ctrl`；若本轮测试场景里被挂段始终处于借用态，那就不该有记录 —— 需玩家日志里的 `ORD-AFTER-COUPLE`/`borrowed=` 做对照。

**取证计划**：读 `R3RAutoRecordChainOrders()` 全函数 + `R3RAutoRangeSlots/Holds`、`R3RAutoOrderErase/Append`、`R3RAutoRangesShift`、`R3RAutoOrderShiftIndex` 的实体，逐条核对槽号与上界；再用玩家 `build\R3R_debug.log` 里 `AUTO-ORD*` 的有无/取值定性。

### 7.1 取证结果（2026-10-21，第 262 轮）

**日志**：`build\R3R_debug.log`（338 244 B / 4 867 行，2026/10/21 14:32:54），即第 261 轮 exe 的实测日志。

**读数（全部为原文锚点）**
- `AUTO-ORD` 共 **6 条**，全部 `seg=1 head=21 slot=0`；`iv` 依次 `[2,3) [2,4) [2,5) [2,6) [2,7) [2,8)`，`cursor` 1..6。
  ⇒ **记录路径确实在跑，并且真的往订单表里插了 6 条**（`begin=2` 恒定 = 锚点 `cur_real_order_index+1` 恒为 2，符合 KI-395 的"同一交接段落到同一槽"设计）。
- `AUTO-ORD-DROP` / `AUTO-ORD-FULL` / `AUTO-ORD-STALE` / `AUTO-ORD-SKIP` / `AUTO-ORD-TRUNC` 全部 **0 条**。
  ⇒ 没有槽冲突（`AUTO-ORD-FULL` 未触发）、没有签名失配、也没有任何一次"跳过"被触发。
- 旁证：`COUPLE-OK loco=21 rear=50`、`ORD-AFTER-COUPLE ... borrowed=1 owner=24`、`DECOUPLE-FIRE` / `DECOUPLE-DONE u=24 real=6`。

**D-262-A 判定：证伪（不是"槽数为 0 导致空循环"）**
`R3RAutoRangeSlots()`（`src\train_cmd.cpp:5671`）返回 `auto_num_ranges` 的钳位值，初值 0；但 `:5904-5913` 的扩槽分支是 `if (slots >= AUTO_RANGE_SLOTS) continue; slot = slots; row->auto_num_ranges = slot + 1;`
⇒ 首次 `slots == 0` 时正常拿到 **slot 0** 并把高水位抬到 1，与日志 `slot=0` 完全一致。空循环只影响"遍历已有槽"，不影响"开新槽"。

**真因（两条，第一条是可见性、第二条是刷新）**
1. **KI-398（设计口径，未修）**：`R3RAutoRecordChainOrders()` 写的是 `R3RSegmentOwnOrderList(seg)`（`:5847`），借用期它就是 `seg->orders_backup`；而订单窗口显示的是 `seg->orders`（= 控制段的表）。日志 `borrowed=1 owner=24` ⇒ 这 6 条被写进了段 1 自己的表，**玩家在借用期点开订单窗口看的是控制段 24 的排程，当然看不到**。记录本身没失败。
2. **KI-397（真 bug，本轮修）**：这些增删不是走命令，`OrderList::InsertOrderAt/DeleteOrderAt` 不会触发任何窗口刷新；`train_cmd.cpp` 里原本 **0 处** `InvalidateWindowClassesData(WindowClass::VehicleOrders)`（全库搜索确认）。⇒ 即使显示的是同一张表，订单窗口/时刻表窗口也会继续画旧行，看起来就是"没录入"。

**本轮修法**
- `src\train_cmd.cpp` 新增 `R3RInvalidateOrderWindows()`（`:5809`），在 `R3RAutoRecordChainOrders()` / `R3RAutoFinaliseChainOrders()` 真正改表时置 `changed`，函数末统一刷新（避免每 tick 无谓 Invalidate）。
- `AUTO-ORD` 探针补 `own=`（`ol == seg->orders`）/`borrowed=`/`n=`（写后总单数），把"写进哪张表"变成可判读量，为 KI-398 的拍板取数。

**待玩家拍板（KI-398）**：借用期内这些记录**是否需要在界面上可见**。可选：A 维持现状（段拿回自己排程/独立后才可见）；B 订单窗口为借用段增加"自动记录"只读分组；C 直接写当前驱动中的表（会污染控制段排程，不建议）。

**构建自证（第 262 轮）**：复用 `_tmp_inc_build.cmd`（未新建 `.cmd`），仅改 `src\train_cmd.cpp`；
`build\R3R_incbuild.done` = `EXIT_CODE=0`，错误计数 0；`train_cmd.cpp` 14:39:36 → obj 14:40:38 →
`build\openttd.exe` 2026-10-21 14:41:37；`read_lints` 0；exe 命中 `own=%d borrowed=%d n=%u`。

### 7.2 玩家质疑：非控制段借用期没有可编辑的表，"边跑边编辑自己的排程"怎么成立（D-262-D）

**玩家原话**：「段在处于非控制阶段的时候自己的排程指针是指向别的排程的，那怎么边跑边编辑自己的排程呢」

**代码事实（`src\train_cmd.cpp` 逐行）**

- `R3RSegmentOwnOrderList()` `:5663-5668`
  ```cpp
  if (seg->r3r_orders_borrowed) return seg->orders_backup;   // 借用期 ⇒ 段自己的表
  if (seg->orders != nullptr) return seg->orders;
  return seg->orders_backup;
  ```
- 记录路径 `R3RAutoRecordChainOrders()` `:5847-5848`
  ```cpp
  OrderList *const ol = R3RSegmentOwnOrderList(seg);
  if (ol == nullptr || ol->IsShared()) continue;   /* Never write into a shared list. */
  ```
  ⇒ **`ol == nullptr` 静默 continue，一行日志都不打。**
- 执行侧 `R3RSkipAutoRecordedOrders()` `:6052-6053`：`if (own == nullptr || v->orders != own) return 0;`（借用期明确不跳过）。

**结论：玩家判断成立，而且这一点比 KI-398 更根本（登记 KI-399）**

1. Route A 下**只有链头**在被挂接时把自己的表停进 `orders_backup`（`Couple()` 的 `v->orders_backup = v->orders`），**链中其它段从来没有过自己的 `OrderList`** ⇒ 对它们 `R3RSegmentOwnOrderList()` 返回 `nullptr`。
2. 于是记录路径对"非链头段"永久在 `:5848` 静默放弃：经过车站也不记录，且日志无痕。
3. 与日志完全吻合：`AUTO-ORD` 6 条**全部**是 `seg=1 head=21`（链头，`orders_backup` 非空的那一个），其余段零记录，同时没有任何 `AUTO-ORD-SKIP/DROP/FULL` 能解释这段空白。
4. 即便段确实有自有表（链头那种情形），写进去的也是一份**不参与驱动**的副本（`orders` 仍指控制段的表），只有段拿回排程才生效 ⇒ "边跑边编辑"目前只在"编辑一份暂停副本"这一层成立，"立刻生效/立刻可见"不成立（可见性属 KI-398）。

**修法选项（待玩家拍板，未改码）**

- **A（建议）按需建段私有表**：`own == nullptr` 时以该段为准创建并 `OrderList::Initialize(seg)`，指针挂在 `seg->orders_backup`（Route A 语义：不抢别人、只自持），此后该段记录有落点。存档侧已有支撑：R3VP 稀疏块存 `orders_backup` 指针、ORDL 写表本体、`AfterLoadVehiclesPhase2()` 对"无主的 `orders_backup` 表"补 `Initialize`（第 94 轮 KI-169a）。风险点：新表只是 `orders_backup` 位、驱动不读它，但要保证 `IsOrderListShared`/`DeleteVehicleOrders` 不会误判成共享或提前销毁。
- **B 记录不落 OrderList**：另存 pending 记录（段行内临时数组），段拿回排程/独立时物化。改动更大、两份真相，但零表语义风险。
- **C 维持现状 + 补可见性**：`ol == nullptr` 时打 `AUTO-ORD-NOLIST seg=… head=…`（只加日志），先把"哪些段被静默放弃"变成可观测，再定 A/B。

**必须先答的口径问题**：借用期记录下来的东西，是"给该段将来独立时用的私有行程"，还是"应该直接作用于当前正在跑的那条计划"？前者 = A/B；后者 = 写控制段的表（会污染控制段排程，不建议）。口径不定，A/B 都落不了地。

### 7.3 口径已定 + 玩家追问"为什么不直接塞进 orders_backup"（D-262-E）

**玩家口径（2026-10-21 拍板）**：「借用期录入的东西，是准备给该段的私有行程使用的。」
⇒ 上面那个口径问题关闭：**术语定为"段将来独立时的私有行程"**，因此写 `orders_backup` 方向正确，
C（写控制段的表）彻底出局。

**玩家追问原话**：「为什么自动记录的调度计划，不能通过 orders_backup 这个指针塞到段的私有表里面」
⇒ 这不是"能不能"的问题，而是"现在这条代码路径根本没试过"：

```5847:5848:src/train_cmd.cpp
OrderList *const ol = R3RSegmentOwnOrderList(seg);
if (ol == nullptr || ol->IsShared()) continue;   /* Never write into a shared list. */
```

`R3RSegmentOwnOrderList()` 对**非链头段**返回 `nullptr`（链头在 `Couple()` 里被 `v->orders_backup = v->orders`
停放过，其余段的 `orders` 本来就是 `nullptr`，`orders_backup` 自然也是 `nullptr`），于是 `:5848` 直接
`continue`——**没有"表为空就建一张"的分支**，也没有日志。

**结论句（待验证项单列）**：逻辑上"给段建私有表并挂到 `orders_backup`"可行，但要先满足 4 个约束
（下一节逐条取证）：①`OrderList` 的创建/池化方式与 `Initialize(seg)` 必须成对；②存档必须能带走
（R3VP 稀疏块 + ORDL 写表本体 + 读档 `AfterLoadVehiclesPhase2` 补 Initialize）；③`R3RRebuildCouplePriorities()`
的 KI-169a 分支把"链头 `orders_backup != nullptr`"当成停放标志，段也带 backup 时**可能被误判**；
④`DecoupleTrain()` 的归还块按 `v->orders_backup != nullptr` 决定是否交换排程，段自带 backup 可能**抢跑归还**。
⇒ 这四条里只要有任意一条不成立，"塞进 orders_backup"就会引入比 KI-399 更严重的错归属。

### 7.4 取证结果（2026-10-21，第 262 轮续）：**能塞，而且代码里已有先例**

**结论句**：`orders_backup` 完全可以承载"段的私有表"。现在没塞的**唯一原因**是
`R3RAutoRecordChainOrders()` 在 `ol == nullptr` 时直接 `continue`，从没写"懒建表"分支 ——
属实现遗漏，不是设计上做不到。

**证据 1：建表 API 现成**
- `OrderList : OrderListPool::PoolItem<&_orderlist_pool>`（`src\order_base.h:1567`）；
- `OrderList::Create(orders, v)`（静态工厂，`v` 成为 `first_shared`）——全库先例：
  `order_cmd.cpp:1679/3334/3432/4991`、`vehicle.cpp:4640`、`sl\vehicle_sl.cpp:351/399`、
  `schdispatch_cmd.cpp:337`、`order_backup.cpp:78`。其中 `OrderList::Create(nullptr, v)`
  就是"建一张空表"的标准写法。

**证据 2：已有代码往 `orders_backup` 塞表，且姿势正确**（`src\couple_group.cpp:1316-1336`，第 227 轮 KI-334）

```cpp
if (src_own != nullptr && src_own->GetNumOrders() > 0) {
    assert(d->orders_backup == nullptr);
    assert(OrderList::CanAllocateItem());
    d->orders_backup = OrderList::Create(std::move(orders), d);   // d 成为 first_shared
    d->orders_backup_real_index = src_real;
    d->orders_backup_implicit_index = src_impl;
    d->r3r_orders_borrowed = true;   // ← 关键：backup 与借用标志必须成对
}
```
注释自陈这就是"隐藏段自己带着排程"的既定形态，且"the same state R3RReturnBorrowedOrders()
expects when the segment head becomes a chain head again"。

**证据 3：存 / 读 / 删三侧都已支持**
- 存：R3VP 稀疏块写 `orders_backup` 指针 + ORDL 写表本体（`couple_group_sl.cpp`，第 94 轮 KI-169a）；
- 读：`AfterLoadVehiclesPhase2()` 对"无主的 `orders_backup` 表"补 `Initialize(v)`；
- 删：`Vehicle::PreDestructor()` `vehicle.cpp:1287-1291` 先摘指针再 `FreeChain(false)`，
  `OrderList::FreeChain()`（`order_cmd.cpp:686`）经 `R3RFindOrderListReferrer()` 判定是否还有别人在用；
  `DeleteVehicleOrders()` `:3846` 的 `ol == v->orders_backup ⇒ continue`，明确把 backup 交给 PreDestructor 处理。

**证据 4：为什么"必须同时置 `r3r_orders_borrowed = true`"（第 144 轮的坑）**
以下 5 处把"`orders_backup != nullptr`"当作状态位，**成对写 borrowed 就能全部绕过**：
- `train_cmd.cpp:6465`（读档 `R3RRebuildCouplePriorities`）：backup 非空 ⇒ 强制 `borrowed = true` 并提前 return；
- `train_cmd.cpp:6590`：backup 非空 ⇒ 把 backup 扶正成 `orders`；
- `train_cmd.cpp:6679`：owner 的 `orders == nullptr` 而 backup 非空 ⇒ 拿 backup 当驱动表；
- `train_cmd.cpp:6968`：`!borrowed && orders == nullptr && (IsFrontEngine() || backup != nullptr)` ⇒ 取回并清 backup；
- `train_cmd.cpp:10297`（`R3RRelocateFrontIdentity`）：backup 非空且 `!borrowed` ⇒ 补标 borrowed。

**证据 5：`R3RSegmentOwnOrderList()` 的第三分支正好兜住"orders 为空但 backup 有表"**
```cpp
if (seg->orders != nullptr) return seg->orders;
return seg->orders_backup;     // ← 非链头段：orders 恒 nullptr，返回的正是新建的表
```

**因此修法（KI-399 方案 A 的精确形态）**：
```cpp
OrderList *ol = R3RSegmentOwnOrderList(seg);
if (ol == nullptr) {
    if (!OrderList::CanAllocateItem()) continue;          /* 池满：放弃并记账 */
    seg->orders_backup = OrderList::Create(nullptr, seg); /* 懒建表，seg 成为 first_shared */
    seg->orders_backup_real_index = INVALID_VEH_ORDER_ID;
    seg->orders_backup_implicit_index = INVALID_VEH_ORDER_ID;
    seg->r3r_orders_borrowed = true;                      /* 与 backup 成对（证据 4） */
    R3RDbgWrite("AUTO-ORD-NEWLIST seg=%u head=%d\n", ...);
    ol = seg->orders_backup;
}
if (ol->IsShared()) continue;
```

**仍需在实现时核对的一处**：`begin` 的基准（`row->auto_ranges_begin[slot]` 初值 / `cur_real_order_index + 1`）
对"非链头段、`orders == nullptr`"是新路径，必须先读 `:5840-5940` 确认它不会算出越界区间。



