# R3R 自动记录命令（第 264 轮）——插入位置仍错 / 灰字未出现 / 「直接抄控制段命令+时间」的设计口径

日期：2026-10-21
状态：取证中（已有初判，待日志读数确认）
关联：`R3R_auto_order_silent_round263_memo.md`（§十三，N1/N2 落地）、`R3R_KNOWN_ISSUES.md`「第 263 轮续三」「第 264 轮」

## 一、输入

- 玩家复测用的 `build\openttd.exe` = **16:41:04（52 036 096 B）**，即第 263 轮续三（N1/N2）版本。
- 本轮日志 `build\R3R_debug.log` = **16:45:34，590 120 B**（晚于 exe ⇒ 采样有效）。

## 二、玩家本轮三条报告

| 编号 | 原话要点 | 初判 |
| --- | --- | --- |
| R264-A | 自动命令**插入位置还是不对** | 未通过 N1 复测判据①；两种成因见 §3.1 |
| R264-B | 新增命令**没有灰色、也没有「（自动填充）」**，问"是不是单段化的问题" | **是**，与 F 同源，见 §3.2 |
| R264-C | 本意是**直接抄控制段对应部分的命令和时间**；现实现是"按脚下路径反推命令来假装抄"，弊端=会抄进源里没有的命令 | 设计口径问题，见 §3.3（需与玩家对口径） |

## 三、逐条定性

### 3.1 R264-A 位置仍错

N1-a 只把锚点由 `cur_real + 1` 改成「`cur_real`，若该处仍是 `OT_GOTO_COUPLE` 则 `+1`」。
它在**单表**（自己的表就是被驱动的那张表）情形下应恰好落到「刚执行完那条之后」。仍是错位，说明场景落在
**双表**：借用期 `orders` = 控制段的 17 条表，记录写进本段 `orders_backup` 的 9 条表，
`cur_real_order_index` 是**前者**的下标，被直接当作**后者**的插入点使用。
两张表在 `OT_GOTO_COUPLE` 之前的条目数一旦不同（本例 U-ORD 前缀 `17,1,1,1,1,15,...` 与 9 条表未必同构），
错位就与「±1」无关，而是「两套下标体系混用」。

⇒ 两个候选：

- **A-1（下标跨表）**：`anchor` 必须在**目标表**里重新定位到「刚执行完那条」之后，而不是照搬共用表下标。
  可用的锚点候选：目标表中最后一个 `OT_GOTO_COUPLE` 的下标 +1；或记录时的「本段 real 索引」（段行里维护的 `real_index`）。
- **A-2（记录时机）**：`R3RAutoRecordChainOrders` 每跨一格跑一次，某次 `cur_real` 已推进过下一条（例如
  耦合后立刻接着跑 `GOTO_STATION` 且已到站推进），会把整轮记录拉到更后面。

判据：日志 `AUTO-ORD ... anchor=%u real=%d cur=%s own=%d borrowed=%d n=%u`（本轮新加字段）。
- 若 `own=0 borrowed=1` 且 `real` 大于 `n`（9 条表的长度）⇒ 直接坐实 A-1。
- 若 `real < n` 且 `anchor == real` ⇒ A-2。

### 3.2 R264-B 灰字没出现 —— 是单段化（= F）的问题

`R3RIsAutoRecordedOrderRow()` 的命中条件是**三条同时成立**：

1. 被看的那辆车是**段头**（链头或 `IsSegmentFront()`）；
2. `r3r_segment_id != R3R_SEGMENT_NONE`（即该链当前是**多段**）；
3. 该段「自己的表」指针 == 窗口显示的 `v->orders`，且该段行 `auto_ranges` 覆盖此行下标。

单段化（解挂后一侧只剩一段）会把段 ID 归还、段行释放、`auto_ranges` 清零（§11.1 / D-263-B），
三条**全部**不成立 ⇒ 既不灰也不加后缀。所以玩家的猜测成立：**R264-B 就是 F**，
F 不做则 N2 在单段链上永远不可见（且记录同时失去"不执行"的凭据）。

⇒ 结论：**F 必须做**，且 F 的产物要能把「区间」信息带过单段化（无段 ID 期间也要有落脚处）。

### 3.3 R264-C 设计口径（本轮新登记）

玩家原意：

- 控制段是「权威排程」，其它段在交接期间应当**直接抄控制段对应片段的命令与时间**
  （时间 = 时间表/等待时间等），即"同一份计划被多段共享/投影"，而不是各自记一份。
- 实际没能实现，退化方案 = **观察列车实际走过的路，反推成命令**（本轮的 `R3RAutoRecordChainOrders`）。
- 该退化的固有弊端：**会抄进源里没有的命令**——列车可能因会让、绕行、临时进库、或解挂后的走位
  经过控制段计划中并不存在的站点/路点/库，于是记录里多出一条源计划没有的命令。

⇒ 这不是"位置偏一条"或"没上色"这类显示/几何问题，而是**记录来源（provenance）问题**：
现状是「按轨迹采样」，玩家要的是「按计划复制」。两者语义不同，采样法无法靠修补完全等价。

初判可选方向（待玩家拍板，尚未设计）：

- **C-1 真复制**：交接时把控制段对应区间（命令 + 时间表）**拷进本段自己的表**，此后本段不再采样。
  需要的机制：①「对应区间」的界定（交接点起、到下一个交接点止）；②时间表字段的逐条拷贝单元
  （`Order::wait_time` / `travel_time` / `timetable` 相关字段需逐项核对，勿复制缓存索引）；
  ③与"记录只读、不执行"的关系 ⇒ 拷进来的命令仍需标记为本段不可执行区段（即 `auto_ranges` 的语义）。
- **C-2 采样 + 白名单过滤**：保留采样，但只允许"落在控制段计划里的相邻两条之间"的站点被记录，
  源里没有的目的地一律丢弃（治"多出命令"，但治不了"漏"与"顺序错"）。
- **C-3 采样 + 事后对账**：采样后与源区间做一次 diff，超集部分标记/删除。

玩家已明确"我可以问"，故本轮先按 §五 的问题清单确认口径，再决定 C-1/C-2/C-3。

## 四、待验证（本轮需取的读数）

1. `AUTO-ORD` 行的 `own=/borrowed=/real=/anchor=/n=` —— 判 A-1 vs A-2（§3.1）。
2. 解挂后该侧的段数：`SEG=` 计数 / `GEO settle-decouple-*` 的 `segid` —— 判 R264-B 是否单段化。
3. 记录里"源计划没有的命令"实例：本轮日志中记录的目的地是否出现在控制段 `U-ORD` 序列里
   —— 为 §3.3 取一个实证样本（哪怕只有一条）。

## 五、要向玩家确认的问题（C 项口径）

1. 「对应部分」如何界定：从**交接点（GOTO_COUPLE 执行完）起**，到本段下一次交接（自己的 `OT_DECOUPLE` /
   下一个 `OT_GOTO_COUPLE`）为止？还是跟着控制段跑、控制段推进到哪就抄到哪？
2. 「时间」要抄哪些：站台等待时间（`wait_time`）、区间行驶时间（`travel_time`）、
   出发/到达时刻（timetable 的 start/loop 基准）、还是整张时间表一起？
3. 抄进来的命令是否应当**完全不可执行**（纯记录，灰字+后缀）——即它们只是"影子"，执行仍由控制段那份负责？
   如果本段将来拿到控制权，是否把这些影子转成真命令？
4. 若采样法继续保留（作为兜底），是否接受"多出的命令直接丢弃"，而不是像现在这样记进去？
5. 一个反例判据：本轮你看到的"多出来的那条命令"，它是**源计划没有的站**，还是**源计划有但顺序/次数不对**？

## 六、本轮结论（随时更新）

- R264-B 与 F 同源，F 为必须项（不做则 N2 永远在单段链上不可见 + 记录会执行）。
- R264-A 大概率是"两套下标体系混用"（A-1），需日志 `own/borrowed/real/anchor/n` 坐实。
- R264-C 是设计口径变更（采样 → 复制），需玩家回答 §五 五个问题后才动手。

## 七、第 264 轮实测读数与定性（2026-10-21，已坐实）

采样有效性：`build\openttd.exe` 16:41:04（52 036 096 B，第 263 轮续三 N1/N2 版）；
`build\R3R_debug.log` 16:45:34 / 590 120 B。

### 7.1 双表事实（耦合前，日志首段 dump）

- `build\R3R_debug.log:83` `R3RDUMP-CHAIN head=21 ... ordCount=9 ordbk=0 curReal=0 coType=16 realType=16`
  ⇒ 链头 21（机车）自己 9 条表：`0 GOTO_COUPLE depot8`、`1 站13`、`2 站14`、`3 路点17`、`4 库(65535)`、
  `5 站14`、`6 站13`、`7 路点16`、`8 GOTO_COUPLE depot12`（`:84-92`）。
- `:93-110` `R3RDUMP-CHAIN head=24 ... ordCount=17`（等待车底，17 条表）：
  `0 WAIT_COUPLE`、`1 站6`、`2 站2`、`3 站8`、`4 站12`、`5 DECOUPLE`、`6 站19`、`7 路点20`、`8 库`、
  `9 站19`、`10 站12`、`11 WAIT_COUPLE`、`12 站8`、`13 站2`、`14 站6`、`15 库8`、`16 DECOUPLE`。
- `:224` `ORD-AFTER-COUPLE head=21 n=17 real=1 impl=1 tt=1 ... borrowed=1 owner=24 u_has_orders=1`
  ⇒ 耦合后链头 21 借用 24 的 17 条表，`cur_real_order_index` 是**控制段表**的下标。

### 7.2 AUTO-ORD 读数：A-1 **证伪**

新版带 `anchor=` 的 6 行（`:4507 / :4513 / :4570 / :5787 / :6975 / :8135`）：

| 行 | 内容 |
| --- | --- |
| 4507 | `kind=depot dest=8 iv=[1,2) cursor=1 anchor=1 real=1 cur=depot own=0 borrowed=1 n=10` |
| 4513 | `kind=waypoint dest=15 iv=[1,3) cursor=2 anchor=1 real=1 cur=depot own=0 borrowed=1 n=11` |
| 4570 | `kind=station dest=6 iv=[1,4) cursor=3 anchor=1 real=1 cur=depot own=0 borrowed=1 n=12` |
| 5787 | `kind=station dest=2 iv=[1,5) cursor=4 anchor=2 real=2 cur=waypoint own=0 borrowed=1 n=13` |
| 6975 | `kind=station dest=8 iv=[1,6) cursor=5 anchor=3 real=3 cur=station own=0 borrowed=1 n=14` |
| 8135 | `kind=station dest=12 iv=[1,7) cursor=6 anchor=4 real=4 cur=station own=0 borrowed=1 n=15` |

判定：
1. `own=0 borrowed=1` 全部成立 ⇒ 写入的是**本段自己的 `orders_backup`**（初始 9 条，`:324` 的 `n=10` 是 append 后的长度）。
2. `real` 恒等于 `anchor`（1,1,2,3,4），且 `real` **远小于** `n`（10..15）⇒ **不存在 A-1 的跨表越界**，锚点数值落在目标表合法范围内。
3. 旧版（无 anchor）同样场景落在 `iv=[2,...)`（`:324/:330/:387/:1604/:2792/:3952`），即 N1 确实把区间整体左移了 1 条 —— 这个修复本身生效了。

### 7.3 R264-A 的真因：记录窗口左端多出 2 条（内容问题投影成位置问题）

对照 7.1 的源表看记录内容：

- `depot8`（第 1 条）＝ 本段自己 `idx0 GOTO_COUPLE depot8` 的**到达点**，控制段 17 条表里没有它，
  对机车段而言是重复记录（它本来就是"开进 8 号库挂车"这一条）。
- `waypoint15`（第 2 条）＝ **控制段表里不存在**的导航点（控制段只有路点 20 与 16）⇒ 正是 §3.3 所说
  "采样会抄进源里没有的命令"的**实证样本**。
- 之后的 `站6 / 站2 / 站8 / 站12` 才对应控制段 `idx1..4`。

⇒ 真正的命令序列被这两条不相干的记录顶到 `idx3` 起，玩家看到的"插入位置还是不对"就是这件事：
**不是 ±1 的偏移，而是窗口左端多记了 2 条，把后续正确命令整体后移。**

### 7.4 R264-B 铁证：解挂后 `auto_ranges` 与段行一起消失

- `:8219` `SEGTR-SNAP tag=decouple-v head=21 nseg=1 ctrl=21 borrowed=0`
- `:8220` `SEGTR-SNAP   i=1 id=0 front=21 star=1 ctrl=1 | live u=1 n="JRW_关西机场快速 01" gid=3 | bk u=0 n="" gbk=-1 | row none u=0 n="" g=-1 | auto n=0 first=[0,0)`

`row none` + `auto n=0` ⇒ 段 ID 归还、段行释放、`auto_ranges` 清零。三条命中条件（段头 / `r3r_segment_id != NONE` /
自己表指针 == `v->orders`）全灭 ⇒ N2 的灰字与后缀**在解挂后必然不可见**。

更严重的是同一个原因让记录**变成真命令**：日志尾部 `CRT veh=21 order=2 dir=1 origin=218,239 td=0 found=1`
⇒ 解挂后机车 21 按归还的表继续执行，`order=2` 正是那批记录之一。

⇒ **R264-B = F，且 F 同时是"记录被误执行"的防线，必须做。**

### 7.5 需向玩家确认（C 项）

见 §五。本轮已按玩家"可以问我"的授权提问；C-1/C-2/C-3 未定前不动 A 的最终修法。

修法草案（待确认后落地）：
- **F**：单段化保留 `auto_ranges` 的落脚处（段行回收前迁到该段链头自身的保留字段，或单段链继续持有段行），
  并在执行侧继续跳过这些区间（`R3RSkipAutoOrderRange` 现在要求 `v->orders == own`，单段化后要放宽）。
- **A′**：记录窗口以"控制段已推进到的位置"为锚，跳过耦合到达点（链头落点等于本段已执行的那条 GOTO_COUPLE 目标时
  不记），并对源表里没有的目的地不予记录（即 C-2 白名单）；这样窗口左端不再多出 2 条。

## 八、玩家答复（2026-10-21，口径已拍板）

> 本节内容由对话抢救而来。教训：口径决策必须当轮落盘，不得只留在对话里（见长期记忆「禁止答完即压缩上下文」）。

### 8.1 三问三答原文要点

| # | 问题 | 玩家答复 |
| --- | --- | --- |
| Q-A | 「插入位置不对」在界面上最接近哪种表现？（实测解挂后机车侧表从第 2 行起是：库8、路点15、站6、站2、站8、站12） | **「抱歉，我又看错了，现在看，插入位置符合预期」** ⇒ R264-A **撤回** |
| Q-C | C 项（自动填充命令的来源）要哪种口径？ | **C-2 采样 + 白名单**：仍按脚下道路采样，但记录前校验目的地是否存在于**控制段对应区间**，不存在就丢弃（能立刻消掉「路点15」这类多余项） |
| Q-F | 自动填充进来的命令，是否永远只作「记录」、任何情况下都不许被执行？ | **「是，永不执行」**；解挂单段化后也要继续跳过这些区间（即必修的 F 项，按此实现） |

### 8.2 由此定稿的判定

1. **R264-A 位置类缺陷关闭**：玩家确认当前（N1/N2 版，`build\openttd.exe` 16:41:04）插入位置符合预期 ⇒
   N1 的锚点修法（`cur_real` 就地，若该处仍是 `OT_GOTO_COUPLE` 则 +1）判定**有效**，不再追"±1 偏移"。
2. **§7.3 的"窗口左端多 2 条"不再按位置 bug 处理**，改按**内容缺陷**由 C-2 白名单消除：
   - `depot8`：本段自己 `idx0 GOTO_COUPLE depot8` 的到达点，控制段 17 条表里没有 ⇒ 白名单丢弃；
   - `waypoint15`：控制段表里不存在（源表只有路点 20/16）⇒ 白名单丢弃；
   - 之后的 `站6/站2/站8/站12`（对应控制段 `idx1..4`）保留。
3. **C 口径 = C-2**（不是 C-1 真复制、不是 C-3 事后对账）。C-1 的"抄时间表字段"需求本轮**放弃**，
   只保留"采样结果受控制段区间白名单约束"。
4. **"记录永不执行"是硬约束**：`auto_ranges` 不是显示装饰，而是执行侧必须继续跳过这些区间的凭据；
   解挂单段化（段行回收 / `auto_ranges` 清零）后也必须继续跳过 ⇒ **F 从"可选"升为"必修"**，
   且 F 的产物必须能把区间信息带过单段化（无段 ID 时也要有落脚处）。

### 8.3 落地清单（待实现，本轮未改码）

- **A′-C2（KI-399/KI-401 合并项）**：`R3RAutoRecordChainOrders()` 记录前先做白名单校验——
  目的地必须出现在控制段表的**对应区间**（交接点起至下一次交接），否则丢弃并打探针
  `AUTO-ORD-DROP kind= dest= reason=not-in-source-range`；同时跳过"链头落点 == 本段已执行的那条
  GOTO_COUPLE 目标"的重复项（`depot8`）。
- **F（KI-400，必修）**：单段化时把 `auto_ranges` 迁移到该段链头的保留字段（或单段链继续持有段行）；
  `R3RSkipAutoOrderRange` 放宽 `v->orders == own` 的条件，使单段化后仍跳过；灰字/后缀随之恢复。
- **顺序建议**：先 F（防记录变成真命令，属数据安全），再 A′-C2（消多余项）。

### 8.4 复测判据（C-2 + F 落地后）

1. 记录里不再出现 `waypoint15` 与重复的 `depot8`，`AUTO-ORD-DROP` 有对应行。
2. 解挂后机车侧表里记录行**仍为灰字 + 「（自动填充）」**（F 生效）。
3. 解挂后 `CRT veh=… order=<记录区间内>` **不再出现**（记录永不执行）。
4. 控制段 17 条表里存在的前 4 条（站6/站2/站8/站12）记录顺序与内容逐字不变。
5. 负判据：源表正常、无多余项的场景不产生任何 `AUTO-ORD-DROP`。

## 九、实现落地（2026-10-21，仅 `src\train_cmd.cpp`，未碰任何头文件）

### 9.1 F：单段化保留"记得区间的那一行"（KI-400）

- 病灶：`R3RSyncSegmentIds()` 对 `segs.size() < 2` 的链一律 `w->r3r_segment_id = R3R_SEGMENT_NONE` +
  `R3RSegmentFree(old_ids[i])`，段行被整行清空 ⇒ `auto_ranges_*` 随之蒸发（现场 `:8219 SEGTR-SNAP
  tag=decouple-v head=21 nseg=1 ctrl=21 … row none / auto n=0`），于是解挂后同一张表里的记录被
  当成玩家的命令驱动（`CRT veh=21 order=2`）。
- 改法：单段分支先扫描链上**已持有**的 ID，挑出"行里任一槽 `auto_ranges_count[s] != 0`"的那一个
  作为 `keep`（`R3RSegmentIdHeldElsewhere()` 兜底防跨链占用），全链写 `keep`，其余 ID 照旧释放；
  找不到就维持原行为（全 NONE、全释放）。**不铸新 ID**——新行是空的，救不了区间。
- 不变式未动：单段链依然不设 ★（`R3RResyncSegmentFronts()` 里 `should[0] == chain` 仍被 erase），
  故 `R3RSegmentHiddenHeads()`（走 ★）仍为空、车队列表不出现子行、界面无可见标记——这一行只是
  "区间的家"。
- 行存活后 settle 会把 `row->orders = ctrl->orders`（`R3RSyncSegmentTraits()`）同步成降级后那张表，
  `R3RSkipAutoRecordedOrders()` 的三条件（`id != NONE` / `own == v->orders` / 索引落在区间内）
  于是全部成立 ⇒ 解挂后继续跳过；灰字 + 「（自动填充）」由 `R3RIsAutoRecordedOrderRow()` 的同样
  三条件恢复。**因此不需要放宽 `own == v->orders`**（备忘 §8.3 的备选做法作废）。
- 新探针：`SEGID-KEEP tag=%s head=%d id=%u nseg=1 reason=auto-range`。

### 9.2 A′-C2：采样的白名单（KI-401）

- 新增两个文件内静态函数，放在 `R3RAutoRecordChainOrders()` 之前：
  - `R3RAutoOrderIsHandover(const Order *)`：`OT_GOTO_COUPLE` / `OT_WAIT_COUPLE` / `OT_DECOUPLE`；
  - `R3RAutoOrderOnControlLeg(chain, ctrl, seg, want)`：判定采样结果是否落在白名单内。
- 白名单 = "控制段此刻正在驱动的那一截计划"：借用期取 `chain->orders` + `chain->cur_real_order_index`
  （KI-04 的冻结值就是交接点），否则取 `ctrl->orders` / `ctrl->cur_real_order_index`；从光标**往回**
  找最近一条交接命令，其**之后**为 `begin`，**再向前**到下一条交接命令之前（不含）为 `end`；
  环状行走、步数上界 = 表长；整表无交接命令时视为一段（`whole_list`）。命中判定用
  `R3RAutoOrderSame()`（类型 + 目的地），与记录器去重口径一致。
- 调用点：`R3RAutoOrderForTile()` 成功之后、取 `R3RSegmentOwnOrderList()` 之前，直接 `continue` 丢弃。
- 丢弃探针：`AUTO-ORD-DROP kind=%s dest=%u seg=%u head=%d reason=not-in-source-range leg=[%u,%u) n=%u`。
- 为什么 `depot8` 与 `waypoint15` 都会消失：`depot8` 是**启动这次交接**的 `GOTO_COUPLE` 的到达点，
  它只存在于链头自己那份 9 条表（`orders_backup`）里，借用的 17 条表里没有；`waypoint15` 在两张表
  里都不存在。两者都被 `begin` 起点（交接命令之后）与"目的地必须在表内"两道同时挡住。
- 无排程可对照时（`ol == nullptr` / `num == 0`）**放行**：记录器此时只写段自己的表，不能因
  "查不到证据"而丢数据。

### 9.3 待复测（在 §8.4 之上追加一条）

6. 解挂单段化后应出现 `SEGID-KEEP tag=decouple-v head=… nseg=1`（不再是 `row none`），且随后
   `SEGTR-SNAP … auto n=` 非 0。

## 十、实机复测结果（2026-10-21，第 264 轮续三）

现场 `build\R3R_debug.log` 359 078 B / 5 084 行（17:17:52），exe 52 036 096 B（17:12:57，
晚于 `src\train_cmd.cpp` 17:11:01）⇒ 采样有效。玩家原话「目前列车的行为符合我的预期了」。

### 10.1 判据逐条

| 判据 | 实测 | 结论 |
|---|---|---|
| `SEGID-KEEP … nseg=1 reason=auto-range` | 恰 1 条（`tag=decouple-v head=21 id=1`） | 通过 |
| 解挂后 `SEGTR-SNAP` 不再是 `row none` / `auto n=0` | `:4026 row use … auto n=1 first=[1,4)` | 通过 |
| `AUTO-ORD-SKIP` 真的跳过区间 | `:3999 … from=1 to=5 iv=[1,5) order=0`，`:4027 real 1->5` | 通过 |
| 记录不再当命令执行 | 解挂后 `real=5`、`L-ORD 5` 起为源表真实命令；旧现场 `CRT … order=2` 形态消失 | 通过 |
| 白名单只丢源外项 | 仅 2 条 `AUTO-ORD-DROP`：`:308 kind=depot dest=8`、`:314 kind=waypoint dest=15`（`leg=[1,5) n=17`） | 通过 |
| 正确项未受影响 | 记录顺序 站6→2→8→12 == 源表 `L-ORD 1..4` | 通过 |
| 无崩溃/断言 | 尾部无 `ASSERT` / `CRASH` / `stray` | 通过 |

### 10.2 疑点澄清（不成立）

- `:4157 CRT veh=21 order=1 origin=262,237` **不是**"记录档被当命令执行"：`CRT` 的 `order=` 来自
  `yapf_rail.cpp:1358` 的 `v->current_order.GetType()`（类型，不是索引）；`order=1` = `OT_GOTO_STATION`，
  同一 tick `:4160 TRP veh=21 real=5 type=1 … ok=1 res=1` 表明跑的是 idx5（真实命令）。
- `CTR/DEPOT-ARR veh=24` 一侧 `real=0->6 tt=65535->6 n=17` 属解出方自己的 17 条表，与记录区间无关。

### 10.3 新发现并已修：KI-402（时刻表索引未随跳过同步）

- 现场：`:4149 ORD-IDX-WATCH site=depotstay-in … real=4->5 impl=4->5 tt=4->1`；
  之后 `TT-CHK-BREAK pre-processorders veh=21 real=5 tt=1 impl=5 n=13 curType=1` 重复 851 行。
- 根因（猜测→实证）：`R3RSkipAutoRecordedOrders()`（`train_cmd.cpp:6203`）只移动 real/impl，
  不碰 tt；解挂块在此之前又把 tt 置成 1（跳过后 real=5）⇒ 两者长期不一致，`R3RCheckTtSync()`
  每 tick 命中。危害是 `UpdateVehicleTimetable()` 用 tt 写到达时刻 ⇒ 时刻表记录到记录区间的订单上。
- 修法：跳过成功后补一行 `if (tt != INVALID) tt = cur_real_order_index;`（与 `:8608`/`:8827`/`:6886` 同款）。
- 状态：已改 + 已增量编译（`_tmp_inc_build.cmd`，未新建脚本）；游戏内复测判据＝不再出现
  `TT-CHK-BREAK pre-processorders veh=21` 刷屏。

### 10.4 仍未确认

- U-1：tt 被置成 1（而非 4）的确切赋值点——解挂块内 `:8827` 之后 real 会推进一跳、`u` 侧
  `:8934 wait_idx` 也可能落在记录区间内；本次只做"跳过处强制对齐"，未全链审计。
- U-2：`TT-CHK-BREAK` 探针本身无节流，任何不同步都会逐 tick 刷日志；是否加边沿节流留待观测。

## 十一、自动填充命令会不会影响客流表（2026-10-21，玩家提问）

玩家口径（本轮新增需求）：**希望会**——记录出来的那几条命令应当像真命令一样进入客流表（Departures）。

### 11.1 代码路径（只读取证）

| 事实 | 锚点 |
|---|---|
| 客流表遍历的是 `v->orders` 全表，**没有任何 R3R 过滤** | `src\departures.cpp:1145` `lod.v->orders->GetNext(lod.order)`；`:1603` 同款；`departures_gui.cpp` 零命中 `auto_range`/`R3R` |
| 终点/沿途站判定不看 travel time，记录命令会照常被当成停靠 | `departures.cpp:1149-1230`（`via_state.CheckOrder` 只问 cargo/停站属性） |
| 记录命令是普通订单、无时间 | `src\train_cmd.cpp:5745 R3RAutoOrderForTile()` → `MakeGoToStation` / `MakeGoToWaypoint` / `MakeGoToDepot`，travel/wait 全 0 |
| 无时间会让"预计时刻"变成不可预测并截断后续推算 | `departures.cpp:1191-1194`（`scheduled_tick = 0`）、`:1610-1611`（`require_travel_time` 直接 `break`） |
| 记录写进哪张表决定可见性 | `train_cmd.cpp:5966 R3RSegmentOwnOrderList(seg)`：段有自己的表⇒写自己的；借用期⇒写 `seg->orders_backup`（`:5982`），此时 `seg->orders` 仍指控制段那份表 |
| 段拿回自己的表后索引才跳过记录区间 | `train_cmd.cpp:6208` `if (own == nullptr || v->orders != own) return 0;` ⇒ 借用期连跳过都不需要（记录不在活动表里） |

### 11.2 结论（分两种状态，务必区分）

1. **段拥有自己的订单表（`own=1`）** —— 例如解挂单段化之后、或该段本来就有自己的表：
   记录条目物理存在于 `v->orders`（现场 `iv=[1,4)` 就在 head=21 的表里），客流表会读到它们 ⇒
   **会影响**：记录站会被算作沿途停靠站，进而影响"经由/终点"和车站出发板里这趟车的行。
2. **段处于借用期（`own=0`，第 264 轮现场 6 条 `AUTO-ORD` 全部 `own=0 borrowed=1`）** ——
   记录只写在该段自己的 `orders_backup`，而客流表读 `v->orders`（控制段那份 17 条表）⇒
   **看不到，不影响**。

### 11.3 已知副作用（若玩家要"可见且正确"，还需一轮改动）

- 记录条目 travel/wait = 0 ⇒ `departures.cpp:1191` 把 `scheduled_tick` 清 0（显示为无预计时刻），
  且 `:1610` 在部分路径上直接 `break`，会让"后面还有几站/下一班到站"的推算提前终止。
- 记录条目不携带货物/停站属性（MakeGoToStation 默认），因此只影响"停靠站集合与顺序"，
  不影响实际运量统计（LinkGraph / 车站货物历史只由真执行产生）。
- 未确认项 U-3：`departures.cpp:1145` 的 `order_iteration_limit = GetNumOrders()` 在表里多出 4 条
  记录后仍够用（记录占位会让 limit 同步变大，不构成截断），但记录与真实命令混排时"绕一圈"的
  遍历顺序尚未实机核对。
- 未确认项 U-4：多段链里非控制段的记录写在自己 backup，而客流表按链头表统计 ⇒
  若玩家希望"每条段各占一行"，那是第 152 轮 P2 系列的段行话题，不在本条范围。

## 十二、玩家澄清：问的是 LinkGraph（货流/客流图），不是 Departures（2026-10-21）

玩家原话：「我所说的客流表并不是车站的 Departures，而是说，假如这个段排程上有 a1-a2(auto)-a3，
那么游戏会不会因为这个段产生 a1 经由 a2 前往 a3 的客货流」。⇒ §十一 的 Departures 结论作废
（保留作历史），本节的链路才是本轮要答的问题。

### 12.1 代码路径（只读取证）

| 事实 | 锚点 |
|---|---|
| 货流边由 `LinkRefresher` 从**订单表逐跳**生成，每两个相邻"可装卸货站"一条边 | `src\linkgraph\refresh.cpp:476-483`（`cur`/`next` 都须是 `OT_GOTO_STATION`/`OT_IMPLICIT` 才调 `RefreshStats`）|
| `RefreshStats` 就是建边本体：起点站 `cur->GetDestination()` → 终点站 `next->GetDestination()` | `refresh.cpp:349-407`（`IncreaseStats(st, cargo, next_station, …)`）|
| 遍历起点＝`v->orders` 里 `cur_implicit_order_index` 那条，然后 `GetNext` 环绕整表 | `refresh.cpp:54`（`v->orders->GetNextDecisionNode(v->GetOrder(v->cur_implicit_order_index), …)`）、`:62`（`RefreshLinks(first, first, …)`）|
| `RunPerSegment` 只改"容量按段切"，**路线仍取自链头 `v->orders`** | `refresh.h:23/42`、`refresh.cpp:76-90`（pass 1 整链沿链头路线一遍）|
| 非 `GOTO_STATION` 的 hop 不建边（waypoint/depot 记录不产生边） | `refresh.cpp:469`（`if (!next->IsType(OT_GOTO_STATION) && !next->IsType(OT_IMPLICIT)) continue;`）|
| 建边还需：货车有该 cargo 容量 + 起点站能带该货离开 | `refresh.cpp:358-361`（`capacities[cargo]`）、`:477`（`cur->CanLeaveWithCargo(...)`）|
| 记录条目＝裸 `GOTO_STATION`（有真实目的地），无任何"记录"标记可被 refresh 识别 | `train_cmd.cpp:5745 R3RAutoOrderForTile()` |

### 12.2 结论（要分两半答）

1. **会**——只要记录条目在**活动订单表**里。记录站是 `OT_GOTO_STATION` 且目的地是真实站，
   `RefreshLinks` 把它当成一个正常 hop ⇒ 产生 **a1→a2 与 a2→a3 两条边**（不是 a1→a3 直达），
   货物随后会被 LinkGraph 路由成"经 a2 中转"。这正是玩家想要的形态。
2. **但前提同 §十一**：记录只写"段自己的表"。`own=0`（借用控制段计划，第 264 轮现场 6 条
   `AUTO-ORD` 全 `borrowed=1`）⇒ 记录在 `orders_backup`、`v->orders` 仍是控制段表 ⇒
   LinkGraph 完全看不到 ⇒ **不产生**。解挂单段化后段拿回自己的表 ⇒ 立刻开始产生。
3. 记录里的 `GOTO_WAYPOINT`/`GOTO_DEPOT` 项**不会**产生货流边（`refresh.cpp:469`），
   所以"经由 a2"只对记录成车站的那些条目成立——这正好也是 A′-C2 白名单放行的对象（站/场）。

### 12.3 风险与副作用（玩家若要"精确控制"时的落点）

- 记录条目没有时刻 ⇒ `travel_estimate` 走 `TimetableTravelTimeFlag::Invalid` 分支
  （`refresh.cpp:254-257` 遇 `GetTravelTime()==0 && !IsTimetabled()` 直接判 Invalid）
  ⇒ 用距离/速度估算，不报错，但会让该边的 `time_estimate` 与真实运行时长脱钩。
- 记录条目默认 load type ⇒ `CanLeaveWithCargo` 为真，空车段的首跳不建边（初始无 `HasCargo`），
  一旦经过某站上货，后续每跳都建边——即"空车前几跳不建边"属上游既有行为，不是 R3R 缺陷。
- 若要让"记录指挥的路线"与"真实执行路线"完全一致（即记录**不该**产生它不执行的边），
  唯一正确做法是执行侧本来就要跳过它们（KI-400/F 已做），而 refresh 侧**也应跳过**
  `auto_ranges` 区间。当前 refresh 侧零过滤 ⇒ 会产生"列车实际不去 a2，货流却认为经 a2"的偏差。
- 未确认项 U-5：`seen_hops` 的去重键是 `(cur_idx, next_idx)`（`refresh.cpp:457`），
  记录条目占位使索引整体后移，去重仍自洽；但若记录区间被跳过、`cur_implicit` 落在 5，
  `RefreshLinks(first=idx5)` 环绕回 idx1..4 时会**再建一遍**记录边（预期行为，非重复建边，
  因为 `seen_hops` 会拦住同一对索引）。
- 未确认项 U-6：`RunPerSegment` pass 2（每段沿自己路线）对"段行里带 auto_ranges 的段"
  是否会用**段自己的表**遍历订单——若用，则记录对非控制段也可能产生边，需取数确认。

