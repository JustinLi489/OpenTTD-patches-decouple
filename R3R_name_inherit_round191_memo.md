# R3R 第 191 轮：列车名称 / 编号继承（读档族）—— 现场待取

- 日期：2026-10-01
- 状态：**待取证**（本轮只写备忘 + 加探针，**不改任何行为**）
- 相关 KI：KI-293（编号）、KI-294（名称清空）、KI-295（名称乱继承）
- 前序轮次：第 152 / 155 / 156 / 159 / 160 / 161 / 163 / 177 / 180 / 185 轮

---

## 一、玩家报告（2026-10-01，原话要点）

| 项 | 玩家口径 |
| --- | --- |
| 时机 | **解挂后（站台解耦）** + **存档 → 读档后** |
| 编号症状 | 「**编号问题是耦合的列车存档再读档出现的**」 |
| 名称症状 | 「**名称清空问题和乱继承问题在一次读档内就存在**」 |
| 对象 | 名字和编号**都有** |
| 现场 | 现有 `build\R3R_debug.log` **无法指认**，需要新跑一次现场 |
| 探针要求 | 「**每次列车进行 R3R 行为，就把有关段的编号和名称打印出来**」 |
| 附带授权 | 可以清除无用探针（第 190 轮搁置相关的除外） |

⇒ 拆成三个独立症状，分列 KI-293 / KI-294 / KI-295：

| 症状 | 触发面 | 与「借用层」的关系 |
| --- | --- | --- |
| KI-293 车号错 | 耦合态**存档** → **读档** | `unitnumber` / `unitnumber_backup` / 段行 `row->unitnumber` |
| KI-294 名称被清空 | **一次读档内** | `name` / `name_backup` / 段行 `row->name` 三者之一为空 |
| KI-295 名称乱继承 | **一次读档内** | 某段拿到了别的段的名字 |

三者共同的机制层：**第 152/159 轮明确「段行是号/名的正式宿主」**（`R3RSegmentRecord::unitnumber` / `::name`），车上的活字段只是**承载者**，`*_backup` 是**停放副本**。任何"读档重建"只要搞错这三者谁是权威，就同时产生"清空"与"乱继承"两种表象。

---

## 二、已确认的机制与代码锚点（本轮已读实的部分）

### 2.1 段行（正式宿主）是存档的

- `src/sl/couple_group_sl.cpp` 有 **R3SG** chunk：一段一行，`Save_R3SG()` 写**所有** `in_use` 行，`Load_R3SG()` 用 `R3RSegmentGetOrCreate(id)` 按**存档里的 ID** 逐行还原。
- `R3RHasParkState()` **含 `name_backup`**，所以 `name_backup` 走 **R3VP** 稀疏块入档（第 160 轮加的）。
- 因此本症状**不能**用"段行/名字没存档"解释。

### 2.2 段 ID 的分配器在读档后**不重建 free list**

```863:913:src/couple_group.cpp
/** R3R (第 152 轮): IDs which were released and may be handed out again (LIFO). */
static std::vector<uint16_t> _r3r_free_segment_ids;

R3RSegmentRecord *R3RSegmentGetOrCreate(uint16_t id)   // 读档路径
{
	if (id == R3R_SEGMENT_NONE || id > R3R_SEGMENT_ID_MAX) return nullptr;
	if (id >= _r3r_segments.size()) _r3r_segments.resize(static_cast<size_t>(id) + 1);
	...
}

uint16_t R3RSegmentAlloc()
{
	if (!_r3r_free_segment_ids.empty()) { ... }         // 读档后必空
	else { id = static_cast<uint16_t>(_r3r_segments.size()); _r3r_segments.emplace_back(); }
}
```

`R3RSegmentTableReset()` 清空 free list，`Load_R3SG()` 只 `GetOrCreate` 不回收 → 读档后**新分配的 ID 从表尾长出来**，不会覆盖已加载行。**这一点是安全的**，但它意味着读档后每次升/解段都会把表越推越大（行泄漏的下游）。

### 2.3 读档唯一入口的调用顺序

```5391:5393:src/train_cmd.cpp
	R3RBorrowControlTraitsLive(chain, "load");
	R3RSettleChainSegments(chain, "load");
```

而 `R3RSettleChainSegments()` 本体：

```5192:5209:src/train_cmd.cpp
	R3RSyncSegmentIds(chain, tag);
	R3RResyncSegmentFronts(chain, tag);
	R3RBorrowControlTraitsLive(chain, tag);
	R3RBorrowControlTraits(chain, tag);
	R3RSyncSegmentTraits(chain, tag);
	R3RSyncHiddenSegmentTraits(chain, tag);
	R3RCheckControlSegment(chain, tag);
```

⇒ **读档时号/名要经过 7 步**，其中第 4/5/6 步都在"猜谁是谁的"，任何一个环节拿错源头就同时产生"清空 + 乱继承"。

### 2.4 `R3RSegmentReconcileRows("load")` 会用**借来的活字段**填空行

```1187:1204:src/couple_group.cpp
		if (row->unitnumber != 0 || !row->name.empty() || row->group_id != GroupID::Invalid()) continue;

		/* ... a chain head which is currently the borrower of another segment's
		 * traits (r3r_orders_borrowed) carries the **control** segment's
		 * unitnumber / name / group_id in its live fields, so those must never be
		 * copied in as this segment's own values -- only the parked copies count. */
		const bool borrowed = head->r3r_orders_borrowed;
		if (head->unitnumber_backup != 0) {
			row->unitnumber = head->unitnumber_backup;
		} else if (!borrowed) {
			row->unitnumber = head->unitnumber;
		}
		if (!head->name_backup.empty()) {
			row->name = head->name_backup;
		} else if (!borrowed) {
			row->name = head->name;
		}
```

判据本身对借用的处理是**刻意**的（`borrowed` 时绝不采信活字段）。但注意它只保护**链头**：`claimed[id]` 取的是"段的段头（★ 车，或首段的链头）"，**非链头段的段头如果自己也在借用态**（`R3RRecoverSegmentTraits` 之后仍可能），这里没有任何 `borrowed` 保护 —— 它读的 `head->name_backup` / `head->name` 可能是**另一段停放在它车上的副本**。

### 2.5 ⚠️ `R3RRecoverSegmentTraits()` 会**抢走本段内其它车的 `*_backup`**

```5025:5058:src/train_cmd.cpp
static void R3RRecoverSegmentTraits(Train *seg)
{
	if (seg == nullptr) return;
	if (seg->unitnumber_backup != 0 && !seg->name_backup.empty() &&
			seg->group_id_backup != GroupID::Invalid()) {
		return;                       // 三段全有 ⇒ 不动
	}

	bool moved = false;
	for (Train *u = seg->Next(); u != nullptr; u = u->Next()) {
		if (u->IsSegmentFront()) break;         // 下一段不是我们的
		if (u->IsArticulatedPart()) continue;

		if (seg->unitnumber_backup == 0 && u->unitnumber_backup != 0) { ...; u->unitnumber_backup = 0; }
		if (seg->name_backup.empty() && !u->name_backup.empty()) {
			seg->name_backup = u->name_backup;
			u->name_backup.clear();             // ← 原车的名字原地消失
			moved = true;
		}
		...
	}
}
```

**它是"搬迁"而不是"读取"**：本段头只要缺一样，就把本段**后续任意一节车**的同名副本**赋值并清空原车**。

风险面（本轮的 KI-294 / KI-295 首选嫌疑）：

1. 它只认 `*_backup`，**不看那辆车是不是"另一个段的段头"**、也不看那辆车是不是**控制段的承载者**；
2. 判据是"本段头三段是否齐全"，而 `group_id_backup` 常常是 `Invalid()`（绝大多数车没有自定义组） ⇒ **它几乎每段都会进循环**；
3. `u` 的扫描范围到下一个 ★ 为止 —— 但**铰接块、控制段的车、链头**都可能在这区间内；
4. 搬走之后被搬车的 `name_backup` 清空 ⇒ 该车对应的段在 `R3RSyncHiddenSegmentTraits()` 里变成 `have_parked_name=0` ⇒ 行被写成**活字段**（可能是借来的那套）⇒ 表象就是**"某段名字变成别段的"+"某段名字空了"**。

调用点只有一处：`R3RSyncHiddenSegmentTraits()`（`:5115`），而它在**每个提交点**都跑（含 `tag="load"`）。

---

## 三、候选根因排序（待新现场日志判定）

| 序 | 候选 | 对应症状 | 判据（新日志里看什么） |
| --- | --- | --- | --- |
| C1 | `R3RRecoverSegmentTraits()` 跨段/跨角色抢 `name_backup`（2.5） | 294 / 295 | `SEGTRAIT-RECOVER` 行出现，且伴随 `SEGTRAITS-SNAP` 里前一段 `bk_name=""`、后一段拿到该名字 |
| C2 | `R3RSegmentReconcileRows()` 用**非链头**段头的借来活字段填空行（2.4） | 295 | `SEGROW-RECONCILE tag=load rebuilt>0` 且 rebuilt 的那一行 `row_name` == 控制段的名字 |
| C3 | 读档后 `R3RSegmentAlloc()` 从表尾长新 ID，与 R3SG 老行并存的**行错配**（2.2） | 295 | 同一段头在 `load-raw` 与 `load` 两次快照里 `segid` 不同 |
| C4 | 耦合态存档里 `unitnumber_backup` 丢失（R3VP 稀疏判据） | 293 | 存档侧快照 `SEGSAVE-PARK` 里找不到该车的 `bk_unit`，而读档侧 `load-raw` 也是 0 |
| C5 | `R3RBorrowControlTraitsLive(chain,"load")` 用错源头（穿活字段而非 `ctrl->*_backup`） | 293 / 294 | `load-raw`（穿之前）与 `load`（穿之后）快照对比，链头 `live_unit/live_name` 变化方向不对 |

**注意**：C4 必须先证伪"存档就没写"，否则会误修读档侧。

---

## 四、本轮交付：只读探针（玩家指定形式）

玩家要求：「每次列车进行 R3R 行为，就把有关段的编号和名称打印出来」。

### 4.1 新探针 `SEGTR-SNAP`（src/train_cmd.cpp）

新增 `static void R3RLogSegmentTraits(const Train *chain, const char *tag)`，**只读**，对每条链打印：

```
SEGTR-SNAP tag=<tag> head=<vehid> nseg=<n> ctrl=<vehid|0> borrowed=<0|1>
SEGTR-SNAP   i=<段序> id=<segid> front=<vehid> star=<0|1> ctrl=<0|1>
             | live u=<u> n="<s>" gid=<g> | bk u=<u> n="<s>" gbk=<g> | row <none|use|off> u=<u> n="<s>" g=<g>
```

字段含义（判读口径）：

- `live` = 段头车的**活字段**（`unitnumber` / `name` / `group_id`）——**承载者**手上那一套；
- `bk` = 同一辆车的**停放副本**（`unitnumber_backup` / `name_backup` / `group_id_backup`）——**这一段的真身**；
- `row` = 段行（`R3RSegmentRecord`）——**正式宿主**（`none` = 该车 `r3r_segment_id==NONE` 没有行；`off` = 有 ID 但行不在用）；
- 健康态：`row == bk`（该段自己那套），且控制段的 `live == row`；
- `bk_name=""` 而 `row_name!=""` ⇒ 停放副本丢了（清空症状）；
- `row_name` == 另一段的 `bk_name` ⇒ 乱继承。

落点（**已实现，行号为改动后**）：

1. `R3RSettleChainSegments()` **末尾**（`src\train_cmd.cpp:5300`，覆盖 `couple` / `decouple-v` / `decouple-u` / `depot-edit` / `load` 五个封闭提交点，即"每次 R3R 行为"）；
2. `R3RRebuildCouplePriorities()` 里 `R3RDearticulateLegacyArticSegment(chain)` **之后**、`R3RBorrowControlTraitsLive(chain,"load")` **之前**，tag 用 `load-raw`（`src\train_cmd.cpp:5421`）⇒ 得到"只经存档恢复、还没被重建"的原始态，与 `load` 形成**前后对比**。

### 4.2 存档侧快照（src/sl/couple_group_sl.cpp）

为回答"存档里到底写了什么"，在写出/读入各两处各加一段**只读** dump（共四条）：

| 标签 | 位置 | 一句话 |
| --- | --- | --- |
| `SEGSAVE-PARK` | `Save_R3VP()` | 写档时每辆有停放态的车：`veh / unit / ubk / n / nbk / gid / gbk / seg / borrowed / prio` |
| `SEGLOAD-PARK` | `Load_R3VP()` | 同字段，读回来的样子 |
| `SEGSAVE-ROW` | `Save_R3SG()` | 写档时每个活段行：`id / use / u / n / g / borrowed / orders` |
| `SEGLOAD-ROW` | `Load_R3SG()` | 同字段，读回来的样子 |

⇒ **"存→读"两侧各自留底**：`SEGSAVE-*` 与 `SEGLOAD-*` 不一致 ⇒ 存档往返本身丢/改数据（KI-293 的 C4 成立）；两者一致而链上 `SEGTR-SNAP` 的 `row` 不同 ⇒ 元凶在**读档重建**。

### 4.3 探针成本

- 提交点是事件级（不是每帧），单列车一至两行 × 段数；
- 存档/读档各一次；
- 不影响任何行为（不写车辆字段、不改段行）。

---

## 五、复测判据

1. 新 exe 跑一次「耦合 → 存档 → 读档」：日志里 `SEGSAVE-ROW` 与 `SEGTR-SNAP tag=load` 的 `row` 段逐段一致 ⇒ 段行本身没坏；不一致即 KI-293 的落点在段行。
2. `SEGTR-SNAP tag=load-raw` 与 `tag=load` 对比：哪一段的 `bk_name` / `row_name` 在哪一步被改，就是 KI-294/295 的现场。
3. 出现 `SEGTRAIT-RECOVER` 且其"被搬走"的车属于**另一段或控制段** ⇒ C1 成立（直接证据）。
4. `SEGROW-RECONCILE tag=load` 的 `rebuilt` 行，其 `row_name` 等于控制段名字 ⇒ C2 成立。
5. 站台解挂（`decouple-v` / `decouple-u`）两条快照里，解出方的段 `row_name` 应等于该段解挂前的 `row_name`。

---

## 六、未确认项

- R-1：玩家报的"名称清空"是否只发生在**多段链**（隐藏段路径）还是也会出现在单段链（那条路径只走 `R3RSyncSegmentTraits`）？——快照里看 `nseg=1` 的列车。
- R-2：第 160 轮之前的旧档里 `name_backup` 缺失是**已知会丢名字**的；玩家这次的档是否可能仍来自那个窗口？——看 `SEGSAVE-PARK` 有没有 `bk_name`。
- R-3：`R3RRecoverSegmentTraits()` 的 `group_id_backup` 判据（`Invalid()` 是默认值）是否让它在**每一段**都无条件进入循环 —— 需要在日志里数 `SEGTRAIT-RECOVER` 的频率。
- R-4：读档后段的号/名如果已经错，**解挂**（玩家报的另一个时机）是否会把它固化/放大 —— 靠 `decouple-v` / `decouple-u` 快照判断。

---

## 七、本轮不做

- 不改 `R3RRecoverSegmentTraits()` 的搬迁范围（先看日志确认它是否真的是元凶）；
- 不改读档重建顺序；
- 不清除任何现有探针（需玩家点选，清单见对话）。

---

## 八、构建自证（2026-10-01）

- 复用既有 `_tmp_inc_build.cmd`（**未新建任何 `.cmd`**）；护栏输出
  `GUARD: incremental is safe (no header/lang file is newer than the newest object)`。
- 第一次编译 **失败**（`EXIT_CODE=1`），只有类型错误、无逻辑错误，已修：
  `UnitID` 在本仓是 `typedef uint16_t`（`src\transport_type.h:14`），**不是**强类型，故
  `unitnumber.base()` 非法（`error C2228`，`train_cmd.cpp(5230)` / `couple_group_sl.cpp(171)`）
  ⇒ 4 处改为 `(unsigned)v->unitnumber` 直接取值。`group_id` 走 `PoolID` 强类型，`.base()` 合法。
  *教训：本仓 `*.base()` 只对 `PoolID<...>` 家族成立（`VehicleID`/`GroupID`/`StationID`/`OrderID`…），
  `UnitID` 是裸 `uint16_t`。*
- 修后：`build\R3R_incbuild.log` = `[4/4] Linking CXX executable openttd.exe`；
  `build\R3R_incbuild.done` = `EXIT_CODE=0`；`error C* / fatal error / FAILED: / build stopped` 计数 = **0**。
- 时间戳：`src\train_cmd.cpp` 22:09 / `src\sl\couple_group_sl.cpp` 22:09
  → 两个 `.obj` 22:10 → `build\openttd.exe` **2026-10-01 22:13（51 608 064 B）**。
- `read_lints`（两文件）= 0 条。
- exe 字面量自证：**5/5 命中** `SEGTR-SNAP` / `SEGSAVE-PARK` / `SEGLOAD-PARK` / `SEGSAVE-ROW` / `SEGLOAD-ROW`。
- **本轮未改任何行为**：新增代码全是只读 `fprintf`（不写车辆字段、不写段行、不动读档顺序），
  唯一被改动的既有行是探针内部的格式串取值方式。
