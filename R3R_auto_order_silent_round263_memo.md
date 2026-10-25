# R3R 自动记录仍然静默失效 —— 第 263 轮取证备忘

日期：2026-10-21　主题：KI-399（借用期自动记录没有写进段的私有表）

## 一、输入

- 日志：`D:\temp\codebuddy-dropped-files\6b4670f9-4f49-44ae-bf30-5aac570abf1f\R3R_debug.log`
  - 大小 **52 033 536 B**（52 MB）
  - LastWriteTime **2026-10-21 15:29:44**
- 当前构建产物：`build\openttd.exe` LastWriteTime **2026-10-21 15:28:16**
  （= 第 262 轮续那次增量构建，含 `AUTO-ORD-NEWLIST` 懒建表分支）

## 二、已确认事实（带行号锚点）

### 事实 1：整份日志里 `AUTO-ORD` 命中 **0 条**

`search_content "AUTO-ORD"` → Found 0 matching results。

这**不是**"建了表但内容没写进去"，而是**整个记录循环一次都没走到写入点**。依据是
`src\train_cmd.cpp` 里 AUTO-ORD 家族每一处都是无条件写：

| 位置 | 探针 | 触发条件 |
|---|---|---|
| :5859 | `AUTO-ORD-NOLIST ... reason=pool-full` | 池满、建表失败 |
| :5868 | `AUTO-ORD-NEWLIST seg=%u head=%d` | 懒建表成功（本轮新加） |
| :5890 | `AUTO-ORD-DROP seg=%u head=%d slot=%u iv=[%u,%u) sane=%d` | 手工编辑导致区间作废 |
| :5929 | `AUTO-ORD-FULL seg=%u head=%d slots=%u anchor=%u` | 区间槽位用尽 |
| :5983 | `AUTO-ORD seg=%u head=%d slot=%u kind=%s dest=%u iv=[%u,%u) cursor=%u own=%d borrowed=%d n=%u` | **每次真正写入一条订单** |

结论句：五条探针 0 命中 ⇒ `R3RAutoRecordChainOrders()` 在到达 `:5973-5978`
的 `R3RAutoOrderErase/Append` 之前就已经退出（return 或 continue）。

### 事实 2：日志尾部仍在大量刷屏，与自动记录无关

尾部 30 行只有 `CLEAR-PATH` / `UNRES-RAIL` / `RESV-REL who=CLEAR-STN ...` /
`LR-TRACE` / `DECOUPLE-INTERLOCK clear ...` / `ORD-IDX-WATCH site=depotstay-in`。
52 MB 体量来自这些既有探针，说明列车在正常跑、解挂等提交点也在发生，
**不是"列车没动所以没东西可记"**。

## 三、猜测根因（待验证，按用户规则单独登记）

### H1（最可能）：节流键用**链头 tile**，记录信号却是**段头 tile**

```5826:5833:src/train_cmd.cpp
	const uint32_t key = chain->index.base();
	const uint32_t tile = chain->tile.base();
	const auto last = _r3r_auto_order_chain_tile.find(key);
	if (last != _r3r_auto_order_chain_tile.end() && last->second == tile) return;
	_r3r_auto_order_chain_tile[key] = tile;

	const std::vector<Train *> segs = R3RGetSegmentHeads(chain);
	if (segs.size() < 2) return;   /* Whole chain is one segment: nothing to record. */
```

而真正判断"经过什么"的是**每个段头自己的格**：

```5844:5845:src/train_cmd.cpp
		Order want;
		if (!R3RAutoOrderForTile(seg->tile, &want)) continue;
```

长编组 + 低速时，车节跨格是**逐节错开**的：链头（控制段／机车）先跨进下一格，
之后链头在这一格内要继续走十几甚至几十 tick 才再跨格，而尾部段头正是**在这段窗口里**
才跨进站台格。此时 `last->second == chain->tile` 恒真 ⇒ 函数在 `:5829` 直接 return，
段头的那次跨格**从未被看见**。

⇒ 这解释了"一条 AUTO-ORD 都没有"：凡是段头跨格的 tick，链头 tile 几乎都不会同时变。

**待验证判据**：给函数加一个分类计数探针
`AUTO-ORD-SKIP path=throttle|noseg|noctrl|notile|norow`，
复跑后若 `path=throttle` 占绝大多数 ⇒ H1 成立。

### H2：链实际只有一段（`segs.size() < 2`），或 `R3RGetControlSegment()` 返回 nullptr
按玩家场景（机车挂车底）应为 2 段，但若这次测试的车底没有 ★ 段边界，
`R3RGetSegmentHeads()` 只返回链头一个 ⇒ `:5833` 直接 return，一条日志都不会有。

### H3：段头从未停在站台／路点／车库格
`:5845` 的 `R3RAutoOrderForTile()` 只认 `IsRailWaypointTile` / `IsRailStationTile` /
`IsRailDepotTile`（`src\train_cmd.cpp:5710-5725`），且该 continue **没有日志**。

## 四、疑点（D-263-A）：日志的产生者到底是不是 15:28:16 那个 exe

日志 mtime 15:29:44 只比 exe 编译完成晚 88 秒，而日志有 52 MB ——
若是单次会话，刷屏速率约 600 KB/s，极端；更像是**追加写**、累积了多轮运行。
若这一轮玩家跑的是**旧 exe**（不含懒建表分支），那 `AUTO-ORD-NEWLIST` 天然为 0，
本轮取证结论会全部失真。

**必须确认**：日志打开方式是 `"w"`（覆盖）还是 `"a"`（追加）。判据未取到 ⇒ 先记为疑点，
下轮第一步解决（也可直接看日志第一行的启动横幅时间戳）。

## 五、下一步（按优先级）

1. 确认日志打开模式 / 起始行时间戳 ⇒ 关闭 D-263-A。
2. 加 `AUTO-ORD-SKIP` 分类计数探针（只读，不改行为），复跑一次即可定位 H1/H2/H3。
3. 若 H1 成立，修法：把节流键从"链头 tile"改成"逐段的 seg tile 快照"
   （或让 `:5829` 的 return 只在**所有段头 tile 都没变**时成立）。

---

## 六、⚠ 推翻事实 1：`search_content` 对工作区外文件是**假阴性**

`search_content` 搜 `D:\temp\codebuddy-dropped-files\...\R3R_debug.log` 时，
连日志**第一行**的 `SEGLOAD-PARK` 都报 `Found 0 matching results`。
⇒ 该工具没有真正扫描这个文件（在 workspace 之外）。
**教训：工作区外的日志一律用 PowerShell `Select-String -SimpleMatch`，且不能用 `$` 变量
（本环境会把 `$` 吃掉，`$p='...'` 会变成 `='...'` 报错）。**

PowerShell 复测真值：

| 关键词 | 命中 |
|---|---|
| `AUTO-ORD` | **6** |
| `SEGLOAD-PARK` | 2（两次读档 ⇒ 日志为追加写，含两个会话） |
| `load-rebuild` | 2 |

## 七、真实结论：**写入成功了，而且写进了段自己的私有表**

6 条原文（`Select-String` 行号锚点）：

```text
307   AUTO-ORD seg=1 head=21 slot=0 kind=depot   dest=8  iv=[2,3) cursor=1 own=0 borrowed=1 n=10
313   AUTO-ORD seg=1 head=21 slot=0 kind=waypoint dest=15 iv=[2,4) cursor=2 own=0 borrowed=1 n=11
370   AUTO-ORD seg=1 head=21 slot=0 kind=station dest=6  iv=[2,5) cursor=3 own=0 borrowed=1 n=12
1587  AUTO-ORD seg=1 head=21 slot=0 kind=station dest=2  iv=[2,6) cursor=4 own=0 borrowed=1 n=13
2775  AUTO-ORD seg=1 head=21 slot=0 kind=station dest=8  iv=[2,7) cursor=5 own=0 borrowed=1 n=14
3935  AUTO-ORD seg=1 head=21 slot=0 kind=station dest=12 iv=[2,8) cursor=6 own=0 borrowed=1 n=15
```

- `seg=1 head=21`：段 ID 1、段头车 21，**全程同一个段**；
- `iv=[2,3)`→`[2,8)`、`cursor=1..6`、`n=10..15`：表被稳定追加 6 条，没有回退、没有重复；
- `own=0 borrowed=1`：**正是"写进段自己的 `orders_backup`"的形态**。

代码注释自己写明了这件事（`src\train_cmd.cpp:5980-5982`）：

```text
/* (KI-398) Which list is this? `own=0` means the segment is still borrowing
 * the control segment's plan, so the orders land in its own `orders_backup`
 * and the orders window (which shows `orders`) cannot display them yet. */
```

⇒ **玩家看到的"没写入表"是显示层现象，不是写入失败**：
记录落在 `seg->orders_backup`，而订单窗口只画 `v->orders`
（借用期它指向控制段的表），所以界面上一条都看不见。

同时 `AUTO-ORD-TRUNC / -DROP / -FULL / -NEWLIST` 全部 **0 条** ⇒ 表存在且没被丢弃，
懒建表分支确实没被触发（说明段本来就有一张 `orders_backup`）。

## 八、为什么 3935 行之后再没有 AUTO-ORD

`DECOUPLE-DONE` 只有 1 条，在 **4059 行**：`u=24 co=0 real=6`。
3935（最后一条记录）→ 4059（解挂）之间再无记录，之后 50 万行也没有。

⇒ 记录不是"停摆"，而是**解挂结束了这一轮**：段独立后它自己成为控制段
（`:5841 seg == ctrl ⇒ continue`），按现行设计不再给自己记录。

## 九、真正待办（按玩家口径"记录 = 段将来独立时的私有行程"）

### T1（显示）借用期订单窗口看不到段的 `orders_backup`
这是"没写入"的直观来源。可选做法：
- (a) 订单窗口在 `v->r3r_orders_borrowed` 时允许切到 `orders_backup` 查看；
- (b) 给段行加一个只读的"行程记录"入口（车队列表子行已经存在，可挂右键）。

### T2（归属）解挂后 `orders_backup` 是否被扶正成 `orders`
`R3RReturnBorrowedOrders()` 只处理**链头**（`:6588-6590` 首行即 `chain->r3r_orders_borrowed`），
若 head=21 在 4059 那次解挂后不是新链头，它的 backup 就仍然只是一个停放副本。
需确认 4059 之后 head=21 的角色 ⇒ 未验证项 **U-263-A**。

### T3（记录口径）每轮只记 6 条是否就是玩家期望
若玩家期望"借用期跑多少站就记多少站"，6 条对应 6 站是正常的；
若是"解挂后继续跑也要记"，那独立后已成为控制段，需要另行定义。

---

## 十、丢点定位：**段 ID 在提交点被重排，`auto_ranges` 元数据对不上（订单本体还在）**

用户追问「是不是还给段的时候又弄丢了」。逐行比对 `SEGTR-SNAP` 的段 ID 与 `auto` 列，
结论是 **订单本体没丢，丢的是"哪几条是自动记录的"这份标记**。

### 10.1 段 ID 的三次取值（同一会话，`D:\temp\...\R3R_debug.log`）

| 行 | 时点 | 快照 | `auto` |
|---|---|---|---|
| 5 / 8 | 读档 | `i=1 id=0 front=21 star=1 ctrl=1 … row none` | `n=0 first=[0,0)` |
| **287** | **耦合后** | `i=1 **id=1** front=21 star=1 ctrl=0 … bk u=1 n="JRW_关西机场快速 01" | **row use**` | `n=0 first=[0,0)` |
| 288 | 耦合后 | `i=2 id=2 front=24 star=1 ctrl=1 … row use` | `n=0` |
| **4020** | **解挂 settle** | `i=1 **id=0** front=21 star=1 ctrl=1 … row **none**` | `n=0 first=[0,0)` |
| 4051 | 解挂 settle | `i=1 id=0 front=24 … row none` | `n=0` |

而 6 条自动记录**全部**是 `AUTO-ORD seg=1 head=21`，对应的是 **:287 那次 id=1 的行**。
解挂后段 ID 退回 **0**，并且 `row none`（该 ID 连行都没有）⇒ 记录行与当前段 ID 脱钩。

### 10.2 订单本体没有丢（关键算术）

```text
第 307 行  AUTO-ORD seg=1 head=21 … own=0 borrowed=1 n=10   ← 表长
   …（6 次追加）…
第 3935 行 AUTO-ORD seg=1 head=21 … cursor=6 n=15            ← 表长 15

第 4019 行 ORD-IDX-WATCH site=settle-out tag=decouple-v veh=21 real=1->1 impl=1->1 tt=1->1
          n=17->15 artic=0->0 ord=000001CEE37C82A0->000001CEE37C9240
```

- `n=17 -> 15`：21 从**借用控制段的 17 条表**切回**自己的 backup 表**，长度正好 15；
- `ord=…82A0 -> …9240`：指针换成了另一张表（= 自己的 `orders_backup` 被扶正成 `orders`）；
- 15 与最后一次 `AUTO-ORD` 的 `n=15` **逐字吻合** ⇒ 那 6 条订单就在这张表里。

⇒ **不是"还给段的时候把表弄丢了"**。表还在，且已被扶正成该段的正式 `orders`。

### 10.3 真正丢的东西：段行上的 `auto_ranges`

`auto n=0 first=[0,0)` 表示该段行里没有任何"自动记录区间"。
KI-395 / KI-396 靠这份元数据识别"哪些订单是自动记录的、执行时要跳过"，
所以现在这 6 条会退化成**普通排程**被正常执行（可能是玩家想要的，也可能是意外）。
元数据之所以对不上，是因为**段 ID 在提交点被重排**（0 → 1 → 0），
而 `auto_ranges_begin/count/signature` 存在按 ID 索引的段行上。

### 10.4 疑点 D-263-B（待验证）

`row none` 说明段 0 当前没有行，但 `auto` 仍被打印成 `n=0`——
需要确认 `R3RSettleChainSegments()` 在解挂提交点是否：
① 释放旧 ID 的行（连带 `auto_ranges`），② 给新 ID 建空行，③ 是否有把旧行元数据迁移到新 ID 的代码。
若三步都没做，则"段 ID 一变，自动记录元数据必然丢"是结构性的，不是这一次的偶然。

### 10.5 仍未验证（U-263-A 保持）

解挂后留下的一半（`front=21`，n=15）在**订单窗口**里是否真的显示这 15 条——
玩家说"没写入表"，若他看的是**机车（控制段 24）**那一侧，那里本来就只有控制段的 17 条表，
记录（属于车底段 21）当然不在；若他看的是车底 21 仍看不到，才是显示层缺陷。
**这一条需要玩家确认看的是哪一侧的订单窗口。**

---

## 十一、D-263-B 定性：**成立且是结构性的（代码已核）**

### 11.1 单段链会把段 ID 直接收回，行整行清零

`src\train_cmd.cpp:5022 R3RSyncSegmentIds()`：

```5022:5039:src/train_cmd.cpp
	if (segs.size() < 2) {
		for (Train *w = chain; w != nullptr; w = w->Next()) w->r3r_segment_id = R3R_SEGMENT_NONE;
		for (size_t i = 0; i < old_ids.size(); i++) R3RSegmentFree(old_ids[i]);
		return;
	}
```

`src\couple_group.cpp:1037 R3RSegmentFree()`：

```1037:1046:src/couple_group.cpp
void R3RSegmentFree(uint16_t id)
{
	R3RSegmentRecord *rec = R3RSegmentGet(id);
	if (rec == nullptr) return;
	*rec = R3RSegmentRecord{};
}
```

⇒ `auto_ranges_begin/count/signature`、`auto_num_ranges` **随行一起被 `R3RSegmentRecord{}` 覆盖**，
既没有"迁到新 ID"，也没有"单段链仍保留一份"的旁路。与 §10.1 的 `0 → 1 → 0` +
`row none` 逐字对应：解挂后 21 那侧 `SEGTR-SNAP nseg=1` ⇒ 走上面这条早退 ⇒ 记录行消失。

结论（D-263-B **成立**、非偶发）：**只要一条段链掉回单段（解挂、车库摘段、耦合后余一段），
它的 `auto_ranges` 一定丢**；订单本体不受影响（`R3RSegmentFree` 只清记录，不碰 `OrderList`）。

### 11.2 次生后果：自动记录的命令会被真的执行

`src\train_cmd.cpp:6071 R3RSkipAutoRecordedOrders()` 开头：

```6071:6078:src/train_cmd.cpp
static uint16_t R3RSkipAutoRecordedOrders(Train *v)
{
	if (v == nullptr || v->r3r_segment_id == R3R_SEGMENT_NONE) return 0;
	...
	R3RSegmentRecord *const row = R3RSegmentGet(v->r3r_segment_id);
	if (row == nullptr) return 0;
```

单段链的 `r3r_segment_id == R3R_SEGMENT_NONE` ⇒ 第 1 行就 `return 0` ⇒ KI-395/396 的
"记录只是参考、执行时跳过"在这一侧完全失效：那 15 条里被自动写进去的 6 条会被当普通命令跑。

### 11.3 新疑点 D-263-C（待验证）：解挂提交点没有刷新订单窗口

`R3RInvalidateOrderWindows()`（`:5809`）在整个 `train_cmd.cpp` 里**只有两处调用**：
`:5991`（`R3RAutoRecordChainOrders` 的 `changed`）与 `:6046`（裁剪函数）。
解挂/`R3RSettleChainSegments`/`R3RSyncSegmentIds` 都不调用它，而解挂发生在车辆 tick 内、
不是命令收尾（不会自动 `InvalidateWindowClassesData`）。

⇒ 解挂瞬间"表从借用切回自有"这件事**没有任何 UI 通知**，玩家若那时开着订单窗口，
看到的仍是解挂前的旧内容（旧表长度/旧订单）。**判据**：把订单窗口关掉再打开，
若 15 条立刻出现，则 U-263-A 的答案是"显示没刷新"，不是"没写进去"。

### 11.4 修法选项增补（接 KI-400）

| 选项 | 内容 | 代价 |
|---|---|---|
| E | 单段链不释放段行：`R3RSyncSegmentIds` 的 `segs.size() < 2` 分支保留旧 ID 在车上（不设 NONE），只把行留着 | 与"单段链无身份"的第 152 轮不变式冲突，`r3r_segment_id != NONE` 会牵动一堆"是否查表"的分支 |
| F | 释放前把 `auto_ranges_*` 存进"无 ID 备份"（Vehicle 上的 3 个字段 / 链头旁挂），下次拿到 ID 再回填 | 多一份存档字段，但不动段身份语义 |
| G | 单段链也保留一个"影子行"（固定 ID 0 专用，不进 ID 池），只存 `auto_ranges` | 改动小，但 ID 0 语义要重新定义（现在 0 = NONE） |
| H | 解挂/结算后补一次 `R3RInvalidateOrderWindows()`（只治 §11.3 的"看不见"） | 一行，纯显示层，先做无风险 |

建议顺序：**H（立刻，止血观感）→ F（保元数据）**；E/G 都要动第 152 轮的身份不变式，暂不碰。

---

## 十二、玩家拍板 + 复测：D-263-C 证伪，问题比"元数据丢失"更深

### 12.1 语义拍板（硬性，2026-10-21）

> 「这些自动记录的命令，肯定只能作为记录而不被执行。」

⇒ 记录的定位是**只读日志**，永远不许成为可执行命令。推论：

1. 因为记录必须"写进表里、窗口看得见"（玩家的原始诉求），它就只能靠 `auto_ranges` 在执行层被跳过
   （`R3RSkipAutoRecordedOrders`，`:6071`）；
2. 一旦元数据随单段化被清（§11.1 已证），那 6 条就变成真命令 ⇒ **直接违反拍板**；
3. 所以 §11.4 的 **F/E/G 从"可选修法"升级为"必须做"**，H 单独做不够。

### 12.2 复测结果：解挂后窗口里没有记录，且刷新三次无效

> 「我又测试了一遍，确认解挂之后段的排程确实没有出现自动记录的排程，甚至刷新了窗口三次」

**D-263-C（解挂后窗口未刷新）→ 证伪。** 关窗重开三次等价于强制重画，仍看不见 ⇒ 不是 UI 通知问题。

与 §10.2 的 `n=17->15` 直接冲突，必须重新定性。三个候选（按可能性排序）：

| 候选 | 内容 | 现状 |
|---|---|---|
| **C1** | 玩家打开的不是 21 那列车（看的是解出的 24 侧，或车队列表的段子行——子行只显示名字/组/单位号，**从不显示排程**，见第 155 轮 KI-247） | 未排除，最可能是"段"这个词的歧义 |
| **C2** | 21->orders 的 15 条里其实不含那 6 条自动记录 | 算术上站不住：§12.3 |
| **C3** | 扶正成 `orders` 之后又发生一次换表/截断（settle 后期另有覆盖） | 未验证 |

### 12.3 C2 的算术复核（关键：`n=` 是追加前还是追加后）

备忘 §七引的第 307 行是 `AUTO-ORD … cursor=` 为 0~5、末次 `cursor=6 n=15`。若"原表长 9 + 6 条 = 15"，
则 `n=10` 必是**第一次追加之后**的读数（9+1=10），整条链自洽 ⇒ **那 6 条确实在 15 条里**，C2 不成立。
⇒ 若玩家的"没有出现"为真，则要么是 C1（看错对象），要么是 C3（扶正后又被换掉/截断）。

**这一步必须用日志/探针实证，不能再靠推理。**

### 12.4 需要的证据（下一组取证目标）

1. `ORD-IDX-WATCH` 全量行：`veh=21` 在 settle-out 之后的**每一次**长度与指针变化（看是否有 C3 的第二次换表）；
2. 解挂那一刻 `v->orders` / `u->orders` 的内容级 dump（现有探针只打长度/索引，**不打订单内容**）——若没有，需补一条只读探针 `ORD-DUMP`（veh + 每条 type/dest）；
3. C1 的排除需要玩家回答：解挂后你点开的是哪一列（机车 24 / 车底 21 / 车队列表里的段子行）。

### 12.5 本次复测日志（`build\R3R_debug.log`，295 390 B / 2026-10-21 15:57:58）读数

**场景与上一次不同**（表长全变了，玩家重存了档）：

```
:3   ORD-IDX-WATCH site=load-rebuild veh=21 ... n=-1->9   ord=->00000238EDEEF930
:10  ORD-IDX-WATCH site=load-rebuild veh=24 ... n=-1->17  ord=->00000238EDEEE2B0
:412 ORD-IDX-WATCH site=depotstay-in  veh=21 ... n=17->17 ord=00000238EDEEE2B0->...   ← 21 借 24 的表(17)
:385 AUTO-ORD seg=1 head=21 slot=0 kind=station dest=6 iv=[2,5) cursor=3 own=0 borrowed=1 n=12
```

1. **21 自己的表只有 9 条**（上次那局是 15 条）；**24（机车/控制段）的表 17 条**；
2. 21 借用 24 的表（`ord=…E2B0` 与 24 的一致，`n=17`），自己的 9 条停在 `orders_backup`（`…F930`）；
3. `AUTO-ORD` 仍是 `own=0 borrowed=1`，写进的是 **21 自己的 9 条表**，长度按 9→…→12 增长
   ⇒ 本次记录条数 = 12 − 9 = **3 条**（上次是 6 条，场景不同，不影响结论）。

⇒ 若解挂后 21 收回自己的表，窗口里应有 **12 条**（9 原有 + 3 条记录）。玩家"看不到"就只剩
**C1（看错对象）/ C3（扶正后又被换表）** 两条路。

### 12.6 排除一个嫌疑：`DeleteUnreachedImplicitOrders()`

`R3RReturnBorrowedOrders()` 末尾（`:6618`）确实调了 `chain->DeleteUnreachedImplicitOrders()`，
但该函数（`src\vehicle.cpp:3414-3430`）**只删 `OT_IMPLICIT` 类型的订单**：

```3418:3419:src/vehicle.cpp
		if (order->IsType(OT_IMPLICIT)) {
			DeleteOrder(this, this->cur_implicit_order_index);
```

而 `R3RAutoOrderForTile()` 构造的是普通 `OT_GOTO_STATION`/`OT_GOTO_WAYPOINT`（`flags=0`，非 implicit）
⇒ **该函数不会删掉自动记录**。除非后续证到记录被打了 `ODF_IMPLICIT`，此嫌疑排除。

> 待验证：`R3RAutoOrderAppend()` 里 `want` 是否被设了任何 implicit 位（若设了，本条立刻翻案）。

### 12.7 决定性读数：解挂把「17 条控制段表」判给了机车 24，记录留在 21 自己表里

```
:4073 DECOUPLE-ADV dec_idx=5 v_real=1 u_real=6
:4074 DECOUPLE-DONE u=24 co=0 real=6 tx=273 ty=237
:4075  U-ORD 0  type=17   ← WAIT_COUPLE
:4076-4091  U-ORD 1..16  type=1/1/1/1/15/1/6/2/1/1/17/1/1/1/2/15
:4092 CGRP-NORM-UNION head=21 nseg=1 mask=0
:4093 GRP-NORM head=24 old=65534 new=65534
```

1. **解出的 u=24（机车）拿走的是那张 17 条的表**（`U-ORD` 连续 0..16 共 17 行），
   也就是 `ORD-IDX-WATCH` 里 `n=17`、指针 `…E2B0` 的那张 —— 玩家的完整排程（含 WAIT_COUPLE 17 / DECOUPLE 15）；
2. **21（车底、留下）收回自己那张表**：`tag=decouple-v veh=21 n=17->15 ord=…E2B0->…F930`
   ⇒ 长度 **15** = 原有 9 + 自动记录 **6**（`AUTO-ORD … n=12` 只是中途第 4 次的读数，后面还有 n=13/14/15，前一条命令被 `-First 60` 截断了）；
3. 21 侧 `nseg=1`（单段链，`CGRP-NORM-UNION head=21 nseg=1`）；
4. ⇒ **自动记录的 3 条，只在 21 的表里，绝不在 24 的 17 条里。**

**由此得到一条之前没写清的判据**：自动记录归属的是**段（21）**，不是"这列车"。
解挂后机带走控制段的表、车底带走自己的表，于是**从机车那一列看，永远看不到任何记录**。
玩家说"段的排程没有出现自动记录的排程"——若他点的是机车 24，这就是 100% 可预期的结果（C1）。

### 12.8 修正后的因果链（替换 §10 的旧表述）

```
自动记录 → 写进「段的 own 表」= 21 的 orders_backup（9→12）
        ↳ 借用期这个表不在窗口上（窗口看 21->orders = 控制段 24 的 17 条）
解挂   → R3RReturnBorrowedOrders 把 backup 扶正给 21（12 条，含 3 条记录）
        ↘ 机车 24 拿走 17 条控制段表（U-ORD 0..16）
⇒ 记录"只属于 21"，且 21 那 12 条要看 21 的窗口；24 的窗口永远没有记录。
```

**结论**：`auto_ranges` 元数据丢失（§11）与"玩家看不见"（§12）是**两件独立的事**：
- 前者让那 3 条会**被执行**（违反 §12.1 拍板），必须修（F/E/G）；
- 后者很可能只是**看错了对象**（C1），需玩家一句话确认；
- 若玩家确认点的就是 21 而仍看不到 12 条，则升级为 C3（扶正后又被覆盖），下一步只能加订单内容 dump 探针。

### 12.9 三行读数锁死（本次日志，最终定性）

```
:304  settle-out tag=couple    veh=21 n=9 ->17  ord=…F930 -> …E2B0   ← 耦合：借走 24 的表(17)，自己 9 条停进 backup
:4036 settle-out tag=decouple-v veh=21 n=17->15  ord=…E2B0 -> …F930   ← 解挂：换回自己的表，已是 15 条(9+6)
:4067 settle-out tag=decouple-u veh=24 n=17->17  ord=…E2B0 -> …E2B0   ← 机车：原封不动拿走 17 条
```

**订单本体与「记录」都没有丢，全部在 21 那张 15 条的表里。** 三件事彻底确定：

1. 记录确实写进表（`AUTO-ORD` 6 条，`own=0 borrowed=1`，落点 `…F930`）；
2. 解挂时确实扶正（`n=17->15` + 指针换回 `…F930`）；
3. **24 与 21 是两张不同的表**：24 = 17 条（玩家原排程），21 = 15 条（9 原有 + 6 记录）。

⇒ 玩家"看不到记录"的唯一剩余解释：**在 24 那一列找记录**（那列结构上永远没有），
或**在 21 那列看见了但认不出来**——后者是真正的产品缺陷：

### 12.10 真正的交付缺口：记录没有任何可视标识

那 6 条记录与普通订单**长得完全一样**（`OT_GOTO_STATION`，同色同格式），且目的地很可能与原有 9 条重复
（`AUTO-ORD kind=station dest=6`）。玩家要在 15 条里靠肉眼认出"哪 6 条是自动录的"是不可能的。
⇒ 「调度没有写入表里面」的真实含义很可能是「**表里看不出哪些是自动录进去的**」。

修法（新增，优先级高于 F）：

| 选项 | 内容 | 改动面 |
|---|---|---|
| **I（强烈建议）** | 订单窗口对落在 `auto_ranges` 区间内的订单行加标记（前置 `[auto]` 或灰色/斜体），并在窗口底部显示「自动记录 n 条」 | `order_gui.cpp` 一行判断 + 一个语言串；只读 `auto_ranges`，不动语义 |
| F | `R3RSegmentFree` 前把 `auto_ranges_*` 存到无 ID 备份，下次拿 ID 回填 | 保执行层跳过（§11.2） |
| H | 解挂/结算补 `R3RInvalidateOrderWindows()` | 一行，习惯性正确 |

I 与 F 必须一起做：I 让玩家**看见**，F 让它们**不被执行**（§12.1 拍板）。

---

## 十三、玩家澄清（2026-10-21）：写对了，但**插错一条**

### 13.1 事实更正（推翻 §12 的 C1/C2/C3）

> 「确实发现了自动添加的命令，但是，它们添加错地方了……我本来期望它们添加在刚好执行完的
> 『前往挂接』命令后面的，但是它们却往后整体偏移了一条命令，到了『前往挂接』的下一条命令后面。」

⇒ §12.2 的 C1（看错列）/C2（不在表里）/C3（被覆盖）**全部作废**。数据侧完全正确：
15 条 = 9 原有 + 6 记录，扶正、指针、长度都对。

**唯一真缺陷 = 插入锚点偏移 1 条。** 期望位置：紧接刚执行完的 `OT_GOTO_COUPLE`；
实际位置：跨过它后面的那一条，落到再下一条之后。

### 13.2 新需求（两条）

| # | 需求 | 落点 |
|---|---|---|
| **N1** | 记录必须插在「刚执行完的 GOTO_COUPLE」之后（即 `cur_real_order_index + 1`），不得跨过下一条命令 | `R3RAutoOrderAppend` 的 `InsertOrder` 落点表达式 |
| **N2** | 自动记录的命令在订单窗口里**显示为灰色** + 后缀「（自动填充）」 | `order_gui.cpp` 订单行绘制 + 新语言串 |

N2 取代 §12.10 的 I 方案（原设想 `[auto]` 前缀），并满足 §12.1 拍板：
**看得见（灰字标注）但绝不执行（auto_ranges 跳过）**。

### 13.3 待验证（下一轮取证，不再猜）

- `AUTO-ORD … slot=0 iv=[2,5) cursor=3`：`slot` 与 `iv` 各是什么基准？`iv` 的 `[begin,end)` 是插入允许区间还是已插入的区间？
- 偏移来源候选：
  - **A**：记录发生在 `cur_real_order_index` 已被推进到下一条之后 ⇒ 直接 `+1` 天然偏 1；
  - **B**：插入用了 `begin + cursor` 而 `begin` 本身就是 `cur+1` ⇒ 再 `+1` 偏 1；
  - **C**：`InsertOrder` 语义是「插到指定 index 的**后面**」（上游习惯）⇒ 传 `cur` 得 `cur+1`，传 `cur+1` 得 `cur+2`。
- 判据：日志里 `AUTO-ORD` 的 `iv=` 首值若恒等于 `cur_real+2`，则命中 A 或 C。
- N2 落点：订单窗口列出订单的那段（`order_gui.cpp` 的 `DrawOrderList` / `OrderGetString`），
  需要按行下标查 `auto_ranges_begin/count` 决定灰色与后缀；还要处理「记录落在单段链后元数据丢失」的回退（§11.1 的 F）。

### 13.4 根因（代码已核，N1 成立）

```5905:5909:src/train_cmd.cpp
		VehicleOrderID anchor = 0;
		if (seg->cur_real_order_index != INVALID_VEH_ORDER_ID) {
			anchor = (VehicleOrderID)(seg->cur_real_order_index + 1);
		}
		if (anchor > num) anchor = num;
```

```5794:5794:src/train_cmd.cpp
	ol->InsertOrderAt(Order(o), (VehicleOrderID)at);
```

`InsertOrderAt(o, at)` 是**插到 at 这个位置**（新订单成为 index `at`，原 `at` 及其后全部后移）。

于是：

- 设「前往挂接」在 index `k`。玩家期望记录成为 `k+1`（紧贴其后）。
- 代码 `anchor = cur_real_order_index + 1`，而**挂接完成后 `cur_real_order_index` 已经推进到 `k+1`**
  （`ProcessOrders` 收尾即推进，日志 `v_real=1` 与 `anchor=2` = `1+1` 正是这个形态）
- ⇒ `anchor = k+2` ⇒ 记录被插到 index `k+2`，**跨过了 `k+1` 那条命令** ⇒ 与玩家描述逐字一致
  「往后整体偏移一条，到了『前往挂接』的下一条命令后面」。

**结论：`+1` 是多余的一步。** 因为 `cur_real_order_index` 的语义已经是「下一条待执行」，
而不是「刚执行完的那条」。

### 13.5 次因（同一处，一并记下）

切到 borrowed 分支时插入目标是 `orders_backup`（本例 9 条），而 `cur_real_order_index` 是
**共用表（17 条）的索引**。两张表内容不同 ⇒ 用同一数字当插入下标存在语义错位。
本现场恰好看不出（两表前缀一致、偏移恒为 1），但它会放大任何偏移，修 N1 时必须一并处理。

### 13.6 N1 修法（待实现，需先拍板口径）

| 方案 | 做法 | 评价 |
|---|---|---|
| **N1-a（推荐）** | `anchor` 先看 `ol->GetOrderAt(cur_real)`：仍是 `OT_GOTO_COUPLE` ⇒ `cur_real+1`；否则 `cur_real` | 两种状态都覆盖，改动 4 行，不引入新字段 |
| N1-b | 在 `ol` 里反向找最后一个 `OT_GOTO_COUPLE`，插在它后面 | 对 borrowed 也稳，但若段自己的表里没有挂接命令则找不到锚点 |
| N1-c | 记录 `cur_real` 在**推进前**的旧值（在 ProcessOrders 里存一个 `last_executed`） | 最准确，但要加字段 + 存档 + 全链路维护 |

先按 **N1-a** 实现，并在 `AUTO-ORD` 日志里补 `cur=`（当前订单类型）与 `real=`，下次一跑即可自证。

### 13.7 N2 待定项

- 灰字要对**所有**落在 `auto_ranges` 里的行生效，而不只是刚记的那几条；
- 需要一个新语言串（英文「(auto)」/ 中文「（自动填充）」），属改 `src/lang/*.txt`；
- 与 §11.1 联动：单段化清 `auto_ranges` 后灰字会消失、记录变真命令 ⇒ **F 必须与 N2 同期做**。

### 13.8 落地（N1-a + N2，已改码）

**N1（插入位置，`src\train_cmd.cpp`）**

- `R3RAutoOrderKindName()` 加空指针保护（返回 `"none"`），供日志新增的 `cur=` 字段使用。
- `R3RAutoRecordChainOrders()` 的锚点按 §13.6 的 N1-a 改写：

```5905:5922:src/train_cmd.cpp
		VehicleOrderID anchor = 0;
		if (seg->cur_real_order_index != INVALID_VEH_ORDER_ID) {
			anchor = seg->cur_real_order_index;
			const Order *const here = ol->GetOrderAt(seg->cur_real_order_index);
			if (here != nullptr && here->IsType(OT_GOTO_COUPLE)) anchor = (VehicleOrderID)(anchor + 1);
		}
		if (anchor > num) anchor = num;
```

  - `cur_real_order_index` 已推进 ⇒ 就地 `cur_real` 即「刚执行完那条之后」；
  - 仍停在 `OT_GOTO_COUPLE` 上（尚未被 tick 过）⇒ `cur_real + 1`，插到挂接命令之后；
  - borrowed 时 `cur_real` 是共用表索引，落在本表长度之外 ⇒ `GetOrderAt` 返回 `nullptr`，走 clamp 到 `num`（追加到末尾），不会越界。
- `AUTO-ORD` 探针补 `anchor=` / `real=` / `cur=`，下次跑局可直接核对
  「anchor 是否等于 real（或 real+1 且 cur=couple）」。

**N2（灰字 + 后缀，`src\order_gui.cpp` + 语言串）**

- 新增 `static bool R3RIsAutoRecordedOrderRow(const Vehicle *v, int order_index)`：
  从链头沿 `Next()` 走，只看段头（链头或 `IsSegmentFront()`）；用
  「`r3r_orders_borrowed` ? `orders_backup` : (`orders` ? `orders` : `orders_backup`)」
  求出该段**自己的表**，必须与窗口显示的 `v->orders` 是同一指针，再查
  `R3RSegmentGet(r3r_segment_id)` 的 `auto_ranges_begin/count` 是否覆盖该下标。
  ⇒ 记录还停在 `orders_backup`（借用期）时窗口看不到，也就不会被误标；
  解挂/结算把表扶正后同一批行自动变灰。
- `DrawOrderString()`：`auto_recorded = !timetable && R3RIsAutoRecordedOrderRow(v, order_index)`；
  颜色与 `OT_IMPLICIT` 共用一个分支（未选中 `Grey`、选中 `Silver`，`NoShade`）；
  行文本末尾追加 `STR_ORDER_R3R_AUTO_FILLED`。
- 新串：`src\lang\english.txt` = `{SPACE}(auto-filled)`，
  `src\lang\simplified_chinese.txt` = `（自动填充）`。

**未做（仍登记）**：F（单段化清 `auto_ranges` 后记录退化成真命令）本轮未动；
H（解挂/结算补 `R3RInvalidateOrderWindows()`）未动。

### 13.9 构建自证（2026-10-21）

- 复用既有 `_tmp_inc_build.cmd`（未新建任何 .cmd）。护栏：`2 header/lang file(s) are newer than the newest object file`
  （`src\lang\english.txt` 16:18:01 / `simplified_chinese.txt` 16:18:06，最新 obj 15:28:16）
  ⇒ `REMOVED 620 object file(s) - upgrading this build to a FULL rebuild`，共 702 步。
- 日志尾：`[702/702] Linking CXX executable openttd.exe`；`build\R3R_incbuild.done` = `EXIT_CODE=0`；
  `error C/fatal error/FAILED:/build stopped` 计数 **0**。
- 时间戳：`src\order_gui.cpp` 16:18:32 → obj 16:35:16；`src\train_cmd.cpp` 16:17:53 → obj 16:38:40；
  `build\openttd.exe` **16:41:04（52 036 096 B）** 晚于全部源码与 obj。
- 语言产物：`build\generated\table\strings.h:3878` = `STR_ORDER_R3R_AUTO_FILLED = 0xF1F`；
  `build\lang\simplified_chinese.lng` 命中「自动填充」。
- 代码字面量：exe 内命中 `cursor=%u anchor=%u real=%d cur=%s`（AUTO-ORD 新探针）。
- `read_lints` 两文件 0 条。
- 注意：语言串在 exe 内不是明文，不能用 findstr 搜「(auto-filled)」判定入库，只能看 `strings.h` / `.lng`。
