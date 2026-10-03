# 第 195 轮备忘：控制段的"选拔"与"特质提升为显性"（**先落笔需求，再取证**）

现场日志：`build\R3R_debug.log`（详见 §1 基本盘）
对照二进制：`build\openttd.exe`（2026-10-02 01:09:41 那版，含第 194 轮 KI-298 收尾）

> 本轮按用户 2026-10-01 追加的优先级规则执行：**拿到报告的第一时间先写/更新临时分析报告与 KI 条目，
> 之后才继续深挖代码/根因/改码/构建**。所以本文件是"先落 §0"，§1 之后是读到日志后补写的。

---

## 0. 需求（本轮任务卡）——先落笔

| 项 | 内容 |
| --- | --- |
| 玩家原话①（逐字） | 「你看，我说你肯定没按照我的规则好好选拔控制段」 |
| 玩家原话②（逐字） | 「请你先把我的需求写入日志，然后再干别的」 |
| 玩家原话③（逐字） | 「现在的机制不是完成R3R行为之后把控制段的特质提升为显性然后把自己的特质盖到链头上面，这样子应该不会有任何问题啊」 |
| 玩家要我做的第一件事 | 把**需求**先写进记录（本文件 + `R3R_KNOWN_ISSUES.md` 的 KI 条目），**再**去分析/改码 |
| 玩家的判断 | 现场仍然出问题，根因是「**控制段没被好好选拔**」（选错了控制段），不是"特质搬运的方向或顺序"写错了 |
| 玩家复述的机制（= 他认定的规则） | 完成一次 R3R 行为（耦合 / 解挂 / 车库编辑）之后：**(a) 先"选拔"出控制段**；**(b) 把控制段的特质"提升为显性"**；**(c) 再把（控制段那套）特质"盖到链头上面"**。这样链头显性 = 控制段自己那套，就不会有任何问题 |
| 本轮要交付 | ①按规则先写需求（本 §0）；②读新日志取证；③回答"控制段现在是怎么被选拔的、是否符合这条规则"；④若不符，给出修法并编译 |

### 0.1 我的读法（待玩家确认，别当结论）

- 「**选拔控制段**」= 决定"哪一段当控制段"这一步。玩家此前（第 152 轮）已拍板 **P1 = 甲**：控制段 == 链头所在段，即 `r3r_priority` 最小者必为链头段，且要在**四个提交点**（耦合 / 翻转 / 解挂 / 车库编辑）末尾重排优先级来强制这条不变式。本轮的"没好好选拔"疑指这条不变式在某个提交点没被强制执行，或排优先级的输入（`r3r_priority`）本身被算错。
- 「**提升为显性**」= 控制段自己那一套（车号 / 名 / 分组）要进**段行 `row`**（显性值）。注意控制段承载车的**活字段 `live`** 在借用态下是"上一次借入的残留"，**不是**它自己那套，所以"提升为显性"必须取它的 `bk`（自己那套），不能取 `live`。这与第 194 轮订正过的判据一致（见 `R3R_name_return_round193_memo.md` §5）。
- 「**盖到链头上面**」= 链头的活字段（号 / 名 / 组）改写成控制段那一套。即现有 `R3RBorrowControlTraitsLive()` 的"穿上"方向，但**前提是控制段选对了**。
- 若①②③按此读法成立，则本轮问题的定位方向应是**控制段选拔**，而不是第 193/194 轮一直在修的特质搬运细节。§2 起按日志逐条核对。

### 0.2 与既有条目的关系

- KI-298（第 193/194 轮，已修待复测）：`R3RBorrowControlTraitsLive()` 的"脱下"分支 + 判据订正 + 分组侧补归还。
- 第 152 轮 P1=甲：控制段 == 链头所在段（强制不变式）。本轮若确认"选拔"没做到，则属该条目的**落地缺口**，不另开重复条目，只登记新现场证据。
- 若证明确实是选拔步骤缺失/顺序错，则本轮 = 把"选拔"这一步补成提交点末尾的**单一强制入口**。

---

## 1. 基本盘与现场证据

### 1.1 基本盘

| 项 | 值 |
| --- | --- |
| 现场日志 | `build\R3R_debug.log` |
| 体积 / 行数 | 113 514 B / 1 821 行 |
| 最后写入 | 2026-10-02 01:18:20 |
| 跑它的二进制 | `build\openttd.exe`（2026-10-02 01:09:41 那版，含第 194 轮 KI-298 收尾；`CTRL-UNBORROW` 在本日志中确实出现，可证 193 的"脱下"分支已编进去） |
| 场景 | 读档即入 6 列车：head=0（T8701/2-国内段，u=1）、6（T8701/2-国际段1，u=2）、24（南宁站调机，u=3）、27（DF4D-3058，u=4）、30（T8701/2-国际段2，u=5）、48（D19Er-XXXX，u=6） |
| 本轮动作序列 | 车库拖动（0 与 6 并成一条）→ 耦合（24 挂上）→ 解挂 → 耦合（27 挂上）→ 解挂 |
| 段行 `row` 与读档 | 单段且未借用的链在读档/单段快照里都是 `row none`（= 段行为不存在，正常）；多段链与借用态才有 `row use ...` |

### 1.2 探针命中（`SEGTR-SNAP` / `SEGTRAIT-SYNC` / `CTRL-UNBORROW` / `INVAR-CTRL` / `DEPOT-XFER-ID`）

**A. 读档基线（行 1–24，六列车各两遍 `load-raw` + `load`）**——全部 `nseg=1 ctrl=<自身> borrowed=0`、`row none`、`live u=N n=<自己的名字> gid=65534`、`bk u=0 n="" gbk=-1`。即**读档时每列车自己就是控制段**，无借用、无段行。

**B. 车库拖动（把 head=0 的国内段拖到 head=6 的国际段1 上）**

```
191 DEPOT-XFER-ID depot-edit head=0 ctrl=6 unit=2 own_bk=1 hasname=1 own_name=1
196 SEGTRAIT-SYNC  tag=depot-edit head=0 seg=2 unit=2 group=65534 borrowed=0 ctrl=6
197 INVAR-CTRL     tag=depot-edit head=0 nseg=2 ctrl=6 ctrl_pri=1 head_pri=2 borrowed=1
223 SEGTR-SNAP     tag=depot-edit head=0 nseg=2 ctrl=6 borrowed=1
224   i=1 id=1 front=0 star=1 ctrl=0 | live u=2 n="T8701/2-国际段1" gid=65534 | bk u=1 n="T8701/2-国内段" gbk=-1 | row use u=1 n="T8701/2-国内段" g=-1
225   i=2 id=2 front=6 star=1 ctrl=1 | live u=0 n="" gid=65534 | bk u=2 n="T8701/2-国际段1" gbk=65534 | row use u=2 n="T8701/2-国际段1" g=65534
```

**C. 耦合（南宁站调机 24 挂上 [0,6] 那条）**

```
329 SEGTRAIT-SYNC-SEC tag=couple head=24 seg=1 unit=1 group=65534 parkedname=1
330 INVAR-CTRL        tag=couple head=24 nseg=3 ctrl=6 ctrl_pri=1 head_pri=3 borrowed=1
359 SEGTR-SNAP        tag=couple head=24 nseg=3 ctrl=6 borrowed=1
360   i=1 id=3 front=24 star=1 ctrl=0 | live u=2 n="T8701/2-国际段1" gid=65534 | bk u=3 n="南宁站调机" gbk=-1 | row use u=3 n="南宁站调机" g=-1
361   i=2 id=1 front=0  star=1 ctrl=0 | live u=0 n="T8701/2-国际段1" gid=65534 | bk u=1 n="T8701/2-国内段" gbk=-1 | row use u=1 n="T8701/2-国内段" g=65534
362   i=3 id=2 front=6  star=1 ctrl=1 | live u=0 n="" gid=65534 | bk u=2 n="T8701/2-国际段1" gbk=65534 | row use u=2 n="T8701/2-国际段1" g=65534
```

**D. 解挂（24 那半摘下）**

```
879 CTRL-UNBORROW decouple-v head=24 unit=3 own_bk=0 hasname=1 own_name=0 own_g=65534
880 SEGTR-SNAP    tag=decouple-v head=24 nseg=1 ctrl=24 borrowed=0
881   i=1 id=0 front=24 star=1 ctrl=1 | live u=3 n="南宁站调机" gid=65534 | bk u=0 n="" gbk=-1 | row none u=0 n="" g=-1
883 DEPOT-XFER-ID decouple-u head=0 ctrl=6 unit=2 own_bk=1 hasname=1 own_name=1
884 SEGTRAIT-SYNC tag=decouple-u head=0 seg=2 unit=2 group=65534 borrowed=0 ctrl=6
885 INVAR-CTRL    tag=decouple-u head=0 nseg=2 ctrl=6 ctrl_pri=1 head_pri=2 borrowed=1
911 SEGTR-SNAP    tag=decouple-u head=0 nseg=2 ctrl=6 borrowed=1
912   i=1 id=1 front=0 star=1 ctrl=0 | live u=2 n="T8701/2-国际段1" gid=65534 | bk u=1 n="T8701/2-国内段" gbk=-1 | row use u=1 n="T8701/2-国内段" g=65534
913   i=2 id=2 front=6 star=1 ctrl=1 | live u=0 n="" gid=65534 | bk u=2 n="T8701/2-国际段1" gbk=65534 | row use u=2 n="T8701/2-国际段1" g=65534
```

**E. 第二次耦合（DF4D-3058 / 27 挂上 [0,6]）**

```
1341 INVAR-CTRL tag=couple head=27 nseg=3 ctrl=6 ctrl_pri=1 head_pri=3 borrowed=1
1370 SEGTR-SNAP tag=couple head=27 nseg=3 ctrl=6 borrowed=1
1371   i=1 id=3 front=27 star=1 ctrl=0 | live u=2 n="T8701/2-国际段1" gid=65534 | bk u=4 n="DF4D-3058" gbk=-1 | row use u=4 n="DF4D-3058" g=-1
1372   i=2 id=1 front=0  star=1 ctrl=0 | live u=0 n="T8701/2-国际段1" gid=65534 | bk u=1 n="T8701/2-国内段" gbk=-1 | row use u=1 n="T8701/2-国内段" g=65534
1373   i=3 id=2 front=6  star=1 ctrl=1 | live u=0 n="" gid=65534 | bk u=2 n="T8701/2-国际段1" gbk=65534 | row use u=2 n="T8701/2-国际段1" g=65534
```

**F. 第二次解挂（国际段1 被摘下，留下 [27][0..5]）**

```
1651 DEPOT-XFER-ID decouple-v head=27 ctrl=0 unit=1 own_bk=4 hasname=1 own_name=1
1652 SEGTRAIT-SYNC tag=decouple-v head=27 seg=1 unit=1 group=65534 borrowed=0 ctrl=0
1653 INVAR-CTRL    tag=decouple-v head=27 nseg=2 ctrl=0 ctrl_pri=1 head_pri=2 borrowed=1
1664 SEGTR-SNAP    tag=decouple-v head=27 nseg=2 ctrl=0 borrowed=1
1665   i=1 id=3 front=27 star=1 ctrl=0 | live u=1 n="T8701/2-国内段" gid=65534 | bk u=4 n="DF4D-3058" gbk=-1 | row use u=4 n="DF4D-3058" g=-1
1666   i=2 id=1 front=0  star=1 ctrl=1 | live u=0 n="T8701/2-国际段1" gid=65534 | bk u=1 n="T8701/2-国内段" gbk=-1 | row use u=1 n="T8701/2-国内段" g=65534
1667 CTRL-UNBORROW decouple-u head=6 unit=2 own_bk=0 hasname=1 own_name=0 own_g=65534
1687 SEGTR-SNAP    tag=decouple-u head=6 nseg=1 ctrl=6 borrowed=0
1688   i=1 id=0 front=6 star=1 ctrl=1 | live u=2 n="T8701/2-国际段1" gid=65534 | bk u=0 n="" gbk=-1 | row none u=0 n="" g=-1
```

### 1.3 逐条对照玩家规则（§0 的 (a)(b)(c)）

| 提交点 | (a) 选拔出的控制段 | 链头所在段 | `ctrl_pri` vs `head_pri` | (b) 控制段"提升为显性" | (c) 盖到链头 |
| --- | --- | --- | --- | --- | --- |
| 车库拖动 191/223 | `ctrl=6`（所属段 id=2，front=6） | **段 id=1，front=0（≠控制段）** | 1 vs 2 | ✅ 行 225 `row u=2 国际段1` = 段 2 自己那套 | ✅ 行 224 链头 `live u=2` = 控制段那套 |
| 耦合 330/359 | `ctrl=6`（段 id=2） | **段 id=3，front=24（≠控制段）** | 1 vs 3 | ✅ 行 362 `row u=2` | ✅ 行 360 `live u=2` |
| 解挂 885/911 | `ctrl=6`（段 id=2） | **段 id=1，front=0（≠控制段）** | 1 vs 2 | ✅ 行 913 `row u=2` | ✅ 行 912 `live u=2` |
| 耦合 1341/1370 | `ctrl=6`（段 id=2） | **段 id=3，front=27（≠控制段）** | 1 vs 3 | ✅ 行 1373 `row u=2` | ✅ 行 1371 `live u=2` |
| 解挂 1653/1664 | `ctrl=0`（段 id=1） | **段 id=3，front=27（≠控制段）** | 1 vs 2 | ✅ 行 1666 `row u=1` | ✅ 行 1665 `live u=1` |

**§1 的三条硬结论（只到此为止，机制解释见 §2）**

1. **`(b)`/`(c)` 两个搬运步骤在现场每一步都是对的**：五处 `row` 全部等于控制段自己的备份那一套，五处链头 `live` 全部等于控制段那一套（= 该段的 `bk`）。⇒ 玩家原话③描述的机制在现场**确实被执行了**，问题只在 **(a) 选拔**。
2. **五个提交点全部满足 `ctrl_pri=1 < head_pri`（2 或 3）**，即**控制段从来不是链头所在段**：`INVAR-CTRL` 行的五个样本无一例外。这才是"控制段没被好好选拔"的客观形态——不是没选拔，而是**选拔的结果恒为"非链头段"**。
3. **`(c)` 的代价是链头自己的那套被压进 `bk`**，而链头显示的就是控制段那套：`1371` 里 `DF4D-3058`（机车自己）只剩在 `bk`/`row` 里，链头活字段显示的是 `T8701/2-国际段1`；`1651` 反过来，链头 `27` 显示 `T8701/2-国内段`。⇒ 玩家看到"选错控制段"的直观后果就是**链头身份被别的段覆盖**。


## 2. 控制段现在是怎么被"选拔"的（代码口径）

### 2.1 (a) 选拔：只有一条判据 = `r3r_priority` 最小

```
4538: static Train *R3RGetLowestPriority(const std::vector<Train *> &segs)   // 取 r3r_priority 最小者
4547: static Train *R3RGetPriorityHead(Train *chain)                          // = 上面那个 + 段头收集
4577: static Train *R3RGetControlSegment(Train *chain)                        // = R3RGetPriorityHead(chain)
```

- `R3RGetControlSegment()` 与 `R3RGetPriorityHead()` **完全同义**，实现就是"段头集合里 `r3r_priority` 最小的那一个"，**没有任何第二判据**：不看链头在哪个段、不看 `orders` 指针最终落在谁手上、不看 `r3r_orders_borrowed`。
- 口径来源（注释 4531-4545、4568-4569）：第 152 轮玩家拍板 **P1=甲**"控制段 == 链头所在段"，第 158 轮玩家**改判为乙**"控制段 = 命令所有者段、链头只是承载者"，第 159 轮按乙落地。⇒ **"甲"那条不变式（控制段==链头段）在代码里是被显式作废的**，所以现场"控制段从来不是链头段"这个形态本身**不是**实现 bug（见 §3.1 第 1 点）。

### 2.2 `r3r_priority` 是"耦合时刻的角色排名"，不是"排程归属"

| 提交点 | 优先级怎么变 | 落点 |
| --- | --- | --- |
| 耦合 | `R3RMergePriorities(passive_segs_now, active_segs_now)`：**被动方（等待车底）原秩不动，主动方（挂上来的机车）全体 `+= passive.size()`** | `train_cmd.cpp:5360-5366`（调用点 `10388-10389`） |
| 耦合收尾 | `R3RRenumberPriorities(v)`：按**当前数值的相对次序**压实成 `1..n` | `5378-5392`（调用点 `10389`） |
| 解挂 | 两半各自 `R3RRenumberPriorities(v)` / `(u)`：**只压实各自相对次序** | `7636-7637` |
| 车库编辑 | `R3RRenumberPriorities(chain)`：**只压实** | `5881`（随后 `5887` 才调 borrow） |
| 读档 | `R3RRebuildCouplePriorities()` 重建 | `5439` / `5456-5458` |

⇒ 两条硬事实：

1. **耦合后控制段必然落在"被挂的那一方"**（被动方），链头（主动方机车）按定义拿不到控制段——这正是 §1 五个样本 `ctrl_pri=1 < head_pri` 的来历，是 5360-5366 那一行的**直接推论**，不是偶发。
2. 除了耦合那一刻，**再没有任何一步按"谁真正拿着排程"重算过排名**：解挂/车库编辑/读档一律只是"把还剩的段按老次序压实"。⇒ 当持有排程的那一段**被解挂切走**、或排程按 ODOF 归还/继承**换了手**之后，排名**原地保留**，选拔出来的控制段有可能已经**没有排程**。

### 2.3 (b) 提升为显性 / (c) 盖到链头：都实现了

- **(c) 穿上/脱下**：唯一入口 `R3RBorrowControlTraitsLive(chain, tag)`（`5740`，注释 `5717-5739` 说明它专治"车库拖动漏搬"）：
  - `ctrl == chain` ⇒ 走**脱下**分支（第 193 轮 / KI-298，`5760-5804`）：把停放/借出的号、名、组统统取回；
  - 否则要求 `chain->r3r_orders_borrowed` 为真（`5808`）且控制段仍在本链上（`5810-5815`），才把控制段的号/名/组穿到链头活字段。
- **(b) 显性（段行）**：`R3RSyncSegmentTraits(chain, tag)`（`4963`，注释 `4934-4962`）把**控制段那一行**的三个特质取 **`traits = (ctrl == chain || !chain->r3r_orders_borrowed) ? ctrl : chain`**（`4975`）——即"借用态下取链头活字段（那正是控制段那套）"；排程与三个索引则取控制段段头本人（`4978-4982`）；非控制段（含链头段自己）由 `R3RSyncHiddenSegmentTraits()` 从各自的 `*_backup` 填行。

⇒ §0 的 (a)(b)(c) 三步在代码里**都有**；现场 (b)(c) 也**全对**（§1.3）。问题集中在 (a) 的**判据**与**时机**。

### 2.4 (a) 的两个缺口（本轮定位）

**缺口 A：选拔判据与排程归属脱钩（§2.2 推论 2）。** 没有任何一步校验 `R3RGetControlSegment(chain)->orders` 与链头实际在跑的表是否一致；`r3r_priority` 只记录"耦合时谁是被动方"。

**缺口 B："穿上"与"脱下"用的是两套判据，且不是同一个提交点末尾的单一入口。** 这是现场 F 真正的形态（逐行）：

| 时刻 | 行 | 内容 | 判据 |
| --- | --- | --- | --- |
| 提交点 | 1651 | `DEPOT-XFER-ID decouple-v head=27 ctrl=0 unit=1 own_bk=4` | `R3RBorrowControlTraitsLive(v,"decouple-v")`：`r3r_orders_borrowed==1` ⇒ **穿上** 国内段 的号 `1` |
| 提交点 | 1653 | `INVAR-CTRL tag=decouple-v head=27 nseg=2 ctrl=0 ctrl_pri=1 head_pri=2 borrowed=1` | 同上 |
| 提交点 | 1664-1665 | `SEGTR-SNAP ... i=1 id=3 front=27 live u=1 n="国内段" ... bk u=4 n="DF4D-3058" row use u=4 n="DF4D-3058"` | 链头 = 借来的那套，自己那套在 `bk`/`row` |
| 解挂后段 | 1726 | `NAME-RESTORE veh=27 side=head` | `DecoupleTrain()` 归还块（`7878-7881`）凭 **`!v->name_backup.empty()`** ⇒ **名无条件还回** |
| 解挂后段 | 代码 `7851`（无日志） | `v->unitnumber = v->unitnumber_backup; v->unitnumber_backup = 0;` | 归还块（`7828-7852`）凭 **`v->unitnumber_backup != 0`** ⇒ **号静默还回 4** |
| 解挂后段 | 1747-1753 | `LOCO-AFTER-DECOUPLE veh=27 real=0 idxnext=6` + `L-ORD 0..5 = GOTO_DEPOT / GOTO / DECOUPLE / WAIT_COUPLE / GOTO_DEPOT / GOTO_COUPLE` | v 手上是**它自己的 6 条表**（`u` 的 29 条表在 `U-ORD`） |

⇒ 同一提交点内，**"穿上"用 `r3r_orders_borrowed`、"脱下"用"有没有 `*_backup`"**，两套判据对同一条链给出相反结论，谁后跑谁赢；最终活字段回到了 27 自己那套（号 4 / 名 DF4D-3058），而 `r3r_orders_borrowed`、`ctrl=0`、`INVAR-CTRL/SEGTR-SNAP` 说的还是"借用中、控制段是 0 段"。
⇒ 客观后果：**(1)** 提交点的探针与段表判断（"现在谁是控制段"）与最终活字段不一致，第 193/194 轮那套判据在这种链上会给出**假阳性/假阴性**；**(2)** v 明明拿回了自己的排程（排程归属 = 27 段），控制段却仍是**没有排程的 0 段**——这就是"没被好好选拔"的可复现形态。

（另注：`1651` 的 `own_bk=4` 属"号侧"，`1271 NAME-XFER` 的 `keep_u=1` 属"名侧"；名侧第 181 轮起不再 `u->name.clear()`，所以名的凭证是"活字段仍等于等待方名字"，号的凭证是 `*_backup`——**两套凭证本来就不同源**，这是缺口 B 的根。）

## 3. 结论

**3.1 玩家规则的 (b)/(c) 没有缺失，现场五处全对。** §1.3 已逐行核对：五处 `row` 全等于控制段自己那套、五处链头 `live` 全等于控制段那套。⇒ 玩家原话③的机制在现场**确实被执行**，本轮问题不能记在 (b)/(c) 上。

**3.2 「控制段没被好好选拔」的客观形态 = (a) 的判据与排程归属脱钩（缺口 A）。**

- 耦合规则 `R3RMergePriorities()`（`5360-5366`）把被动方整体排在主动方之前 ⇒ 耦合后控制段**必然**落在"被挂的那一方"，链头（主动方机车）按定义拿不到控制段。§1.3 五处 `ctrl_pri=1 < head_pri` 全是这一行的直接推论。
- 除耦合与读档外，**没有任何一步按"谁真正拿着排程"重算过排名**（§2.2 表）：解挂/车库编辑只是把"还剩的段按老次序压实"。⇒ 持有排程的那一段被解挂切走、或排程按 ODOF 归还/继承换了手之后，`r3r_priority` 说的还是"耦合时谁是被动方"，控制段有可能**已经不再持有排程**。
- 这是**判据层**的问题（`R3RGetControlSegment()` 只有一条判据），不是搬运层的问题；修它要动"耦合后跑车底排程"的既有语义，故按 §4 修法② 挂待拍板。

**3.3 现场可见的破绽由缺口 B 制造（本轮可复现、可最小修复）。**

同一提交点内，同一条链的两个相反结论各由一套凭证得出，谁后跑谁赢：

| 步骤 | 位置 | 凭证 | 结论 |
| --- | --- | --- | --- |
| (c) 穿上 | `R3RSettleChainSegments()`→`R3RBorrowControlTraitsLive()`（`5282` / `5808`） | `v->r3r_orders_borrowed == 1` | 链头穿控制段那套（1622 行 `unit=1 own_bk=4`） |
| 归还 | `DecoupleTrain()` 归还块（`7828-7890`，在 settle **之后**） | `v->unitnumber_backup != 0` / `!v->name_backup.empty()` | 链头换回自己那套（1726 `NAME-RESTORE veh=27 side=head`，号静默回 4） |

⇒ 该提交点结束时：**活字段 = 27 自己那套**，而 `r3r_orders_borrowed=1`、`ctrl=0`、`INVAR-CTRL`/`SEGTR-SNAP`（1653/1664-1666）说的还是"借用中、控制段是 0 段"。第 194 轮订正的判据③（借用态 **链头 live == 控制段 bk**）在 settle 快照里成立（`live u=1 国内段` == i=2 的 `bk u=1 国内段`），**在归还块跑完后当场变假**。这正是玩家"选错控制段"的直观后果：链头身份被别的段覆盖 / 又被换回来，段表与探针各说各话。

**3.4 附带事实（不作为缺陷）。** `ORD-RETURN-ORPHAN veh=27` + `veh=0`（1648-1649）是 KI-239 的孤儿表归还在本场景的正常触发：解出的 6 段把"国际段1"那张表带走后，链上 v=27 与 0 段各自把自己借来的表还回，随后 `R3RSyncDrivingOrders()` 按 owner(=0 段) 重新起借 —— 该路径与口径一致，本轮不改。

**3.5 与既有条目的关系。** 缺口 B 是 KI-298 的**同族缺口**（193/194 只补了 `ctrl == chain` 这一支的"脱下"），也是 KI-294/KI-297 号名的第三处"两套凭证"现场；缺口 A 与第 194 轮如实记录的"未确认项"（控制段从 A 改判到 B 时承载者活字段没重新同步）同源。

## 4. 修法

### 4.1 修法①（本轮落地，缺口 B）：把"归还块"的门禁统一到借用标志

- 位置：`DecoupleTrain()` 归还块（改码前 `src\train_cmd.cpp:7826-7890`；改码后 `7846-7924`）。
- 改法：在归还块开头取一次**同步之后**的借用态 `const bool head_borrows_now = v->r3r_orders_borrowed;`，归还块（号 + 名两段）的每个写字段分支都加门禁 `!head_borrows_now`。借用态下**一个字段都不写**，只留痕：`UNIT-RESTORE-SKIP-BORROW veh= u= borrowed=1 unit= own_bk=` / `NAME-RESTORE-SKIP-BORROW veh= u= borrowed=1 hasname= own_name=`（新的探针名，避免与 `NAME-RESTORE-SKIP`（另一支）混淆）。
- **为什么必须"重新取一次"而不是复用外层条件**：外层 `if (v->r3r_orders_borrowed)`（改码后 `7530`）读的是**耦合期**留下的标志，位置在 `R3RRenumberPriorities()`/`R3RSyncDrivingOrders()`（`7636-7639`）与 settle（`7767`）**之前**；这两步会把借用关系重新判定一遍（KI-239 孤儿表归还后可能重新起借，或 `owner == chain` 时当场归还），所以外层值只决定"要不要进这个块"，**决定字段怎么写**的必须是重判之后的当前值。
- 落地位置（改码后行号）：`7846` 取标志；`7847` 号归还分支；`7885` 新增 `UNIT-RESTORE-SKIP-BORROW` 分支；`7904` 名侧门禁 + `NAME-RESTORE-SKIP-BORROW`；`7912`/`7916` 原两个归还分支顺次成为 `head_borrows_now == false` 时的分支。
- 理由（口径，不是风格）：
  1. 归还块的原始语义是"Couple 把链头自己的号/名借给了对方，解挂时各归各位"（T8701 #2 restore），它成立的前提是**链头自己就是命令所有者**。该前提在借用态下不成立。
  2. `R3RSyncChainAfterDepotEdit()`（`5902`）已经用 `!chain->r3r_orders_borrowed` 给同一件事（"取回自己的号"）上了门禁，`R3RBorrowControlTraitsLive()`（`5808`）也一样 —— 只有解挂这一处的"脱下"还在凭 `*_backup` 非空无条件执行，构成缺口 B，故本轮把它对齐。
  3. 借用态下链头的活字段**就是** (c) 的产物（控制段那套）；凭 `*_backup` 非空再"还"一次，等于用另一套凭证推翻 (c)，并让判据③当场失效。
- 不影响的东西：`u` 侧（解出的那半）的号与名取回不在此块内 —— 号的取回由 `R3RBorrowControlTraitsLive()` 的 `ctrl == chain` 分支（`CTRL-UNBORROW`，1667 行）完成，名的取回由 `R3RRestoreTrainName(u)`（`7877`）完成，两者都**照旧执行**。真归还态（`borrowed=0`，`owner == chain`，T8701 #2）完全走原逻辑，不允许回归。
- 可逆性：门禁只读一个布尔；不新增/删除任何池条目，不改变任何 `*_backup` 的值。

### 4.2 修法②（缺口 A，**未落地，待玩家拍板**）：把"选拔"收敛成提交点末尾的一个入口

- 形态：新增 `R3RReelectControlSegment(Train *chain)`，在 `R3RSettleChainSegments()` 的 `R3RSyncDrivingOrders()` **之前**调用：
  1. 先按**排程归属**定位控制段：链头实际驱动的那张表（`chain->orders`，或它 parked 在 `orders_backup` 时那张）在本链上的主人 —— 用 `R3ROrderListOwnedInChain()` 同款遍历找"`orders == 该表且 !r3r_orders_borrowed`"的段头；
  2. 命中 ⇒ 把该段的 `r3r_priority` 压到最小、再 `R3RRenumberPriorities()`（其余按物理序压实）；未命中（孤儿表 / 链上无人持有）⇒ 退回现状（`r3r_priority` 最小者）。
- 为什么不能现在直接改：它会把"耦合后跑车底排程"的语义改成"耦合后跑真正持有表的那一段"，`R3RSyncDrivingOrders()` 的 owner 判据、KI-215b 的进度回写、`R3RPushProgressToOwner()` 全部跟着变 —— 属**行为变更，需玩家拍板**（第 152 轮 P1=甲 / 第 158 轮改判乙 的同一类决定）。
- 本轮只登记，不改码；修法① 落地后缺口 A 也不会被"修好"，但它会被**隔离**：活字段与段表/探针一致，探针判读不会再被缺口 B 污染（这正是缺口 A 可以被单独评估的前提）。

### 4.3 明确不改的部分

- `R3RGetControlSegment()` 的乙口径定义（控制段 = 命令所有者段 = `r3r_priority` 最小者）——第 158 轮玩家拍板，本轮不动。
- (b)/(c) 两条搬运步骤、`R3RSettleChainSegments()` 的步骤顺序、`R3RMergePriorities()` 的耦合排名规则。
- KI-239 的孤儿表归还路径（§3.4）。

## 5. 复测判据

1. **新增留痕必须命中且只命中借用态**：F 场景（T8701：机车 27 + 0..5 段，第二次解挂）复跑时，`UNIT-RESTORE-SKIP-BORROW veh=27 u=6 borrowed=1` 与 `NAME-RESTORE-SKIP-BORROW veh=27 u=6 borrowed=1` 各命中 **1 次**，且**不再**出现 `NAME-RESTORE veh=27 side=head`（该行是缺口 B 的签名）。
2. **判据③在归还块跑完后仍成立**：`SEGTR-SNAP tag=decouple-v` 之后（同一提交点的下一帧快照同样成立）—— 链头 `live` == 控制段 `bk`（F 现场两层都应是 `u=1 n="T8701/2-国内段"`）。
3. **号侧对账**：解挂后链头 27 的活号仍是**控制段停放的那一个**（F 现场 = 1），`unitnumber_backup` 保持 4；解出的 u=6 活号 = 它自己的 2（`CTRL-UNBORROW` 已取回）；号池 1 / 2 / 4 仍各被占一次，**无重号、无泄漏**（对照 194 轮 KI-297 的「段号被邻居顶掉」签名不应再现）。
4. **非借用态回归（旧行为不许丢）**：任何 `INVAR-CTRL ... ctrl_pri == head_pri`（即 `borrowed=0`、链头自己就是控制段）的解挂，必须**照旧**出现 `NAME-RESTORE veh=X side=head`，且 `LOCO-AFTER-DECOUPLE` 里机车活号 = 解挂前它的 `unitnumber_backup`。
5. **判据④与表头一致性**：`SEGTR-SNAP` 每一段的 `row == 本段自己那一套`（第 194 轮订正口径 ②）在解挂后仍成立；同一提交点内 `INVAR-CTRL` 的 `borrowed` 必须与 `SEGTR-SNAP` 表头 `borrowed` 同值，且与 1 的两条 SKIP 留痕互不矛盾（有 SKIP ⇒ 该链 `borrowed=1`）。
6. **宏观**：全日志 `error` / `assert` / `fatal` 零命中；`ORD-RETURN-ORPHAN` 只出现在解挂/车库编辑点，且其后必随一次成功起借（`SEGTR-SNAP ... borrowed=1`）或归还（`CTRL-UNBORROW`），不出现"已归还却仍 `borrowed=1`"的组合。

---

# 第 196 轮：落地修正 A（重发号码牌 = 控制段重选）

> 本节先落盘再改码，防止上下文压缩丢掉设计。状态栏随构建/复测结果更新。

## 1. 玩家拍板

玩家 2026-10-02 原话：「第一件——要不要给号码牌加一个"每次有人换手就重新发一次"的动作 / 你这个想法不错，就按这个来。不过，一定要先写临时备忘，我怕你沉浸的干事情的时候突然压缩上下文」。

⇒ 落地 §4.2 修法②（缺口 A / KI-299a），**并且**：收到报告第一时间先写本备忘 + 更新 KI 条目，之后才动码。

## 2. 要解决的问题（缺口 A 的一句话）

`r3r_priority` 记录的是**耦合时刻的角色排名**（被动方在前、主动方整体后移，§3.2），此后除耦合/读档外**没有任何一步**按"谁真正拿着排程"重算过（解挂、车库编辑里的 `R3RRenumberPriorities()` 只是"按老次序压实"）。于是持表段被切走、或排程按 ODOF 归还/继承换了手之后，选出的控制段可能**已经没有排程** —— 选拔判据与排程归属脱钩。

## 3. 落地设计（本轮实现）

### 3.1 新函数

`src\train_cmd.cpp` 新增（放在 `R3RGetControlSegment()` 附近或 `R3RRenumberPriorities()` 之后，且必须在四个提交点之前有定义或前向声明）：

```
static Train *R3ROrderListHolderInChain(Train *chain, OrderList *ol);
static void   R3RReelectControlSegment(Train *chain, const char *tag);
```

**`R3ROrderListHolderInChain(chain, ol)`** —— "这张表在本链上的主人是哪一段"：

- `ol == nullptr` ⇒ 返回 nullptr（无表可判）；
- 只遍历**段头**（`chain` 本身 + `IsSegmentFront()` 的车）；
- 第一遍找"自持者"：`s->orders == ol && !s->r3r_orders_borrowed` ⇒ 立即返回该段头（真共享环上多段同时满足时取物理最前的那一个）；
- 第二遍找"停放者"：`s->orders_backup == ol` ⇒ 记下第一个，返回它（KI-214 形态：主人把表停在 `orders_backup`）；
- 都没有 ⇒ nullptr（孤儿表 / 本链无人持有）。

**`R3RReelectControlSegment(chain, tag)`** —— 重发号码牌：

1. 门禁：`chain == nullptr` ⇒ return；`R3RGetSegmentHeads(chain).size() < 2` ⇒ return（单段链里链头就是唯一段，重选无意义，也不打日志，避免刷屏）；
2. `cur = R3RGetLowestPriority(segs)`（当前号码牌指向的控制段）；
3. 取**锚表** `ol`（与 `R3RSyncDrivingOrders()` 的 owner 交付口径对齐）：`cur->orders`；为空则 `cur->orders_backup`；仍为空则 `chain->orders`；`ol == nullptr` ⇒ return（无表可判，退回现状）；
4. `holder = R3ROrderListHolderInChain(chain, ol)`；
5. `holder == nullptr || holder == cur` ⇒ return（号码牌已经指对，或表无主 —— 无主那种交给 KI-239 的孤儿归还路径处理，本函数不插手）；
6. 否则**重发**：`holder->r3r_priority = 0;` 然后 `R3RRenumberPriorities(chain);`（0 必最小 ⇒ 持有人排到第 1，其余按物理序压实），并写探针
   `CTRL-REELECT tag=%s head=%d nseg=%u old=%d old_pri=%u new=%d new_pri=%u borrowed=%d`。

### 3.2 调用点（四个提交点，全部在"改 owner 的下游步骤"之前）

顺序约束（不可颠倒）：

- 必须在 `R3RRenumberPriorities()` **之后**（rank 已连续，`holder->r3r_priority = 0` 才是唯一最小）；
- 必须在 `R3RSyncDrivingOrders()` / `Couple()` 的借用移交块 **之前**（否则改的是 owner，而链头手上还是旧表）。

| 提交点 | 落点（改码前行号） | 说明 |
| --- | --- | --- |
| 车库编辑 | `R3RRenumberPriorities(chain)`（`5881`）之后、`R3RSyncDrivingOrders(chain,false)`（`5882`）之前 | `tag="depot-edit"` |
| 解挂 | `R3RRenumberPriorities(v)`/`(u)`（`7636-7637`）之后、两次 `R3RSyncDrivingOrders`（`7638-7639`）之前 | v 与 u **各调一次**，`tag="decouple-v"` / `"decouple-u"` |
| 耦合 | `R3RMergePriorities()`+`R3RRenumberPriorities(v)`（`10422-10423`）之后、`Couple()` 内借用移交块之前 | `tag="couple"` |
| 读档 | **不改**（`R3RRebuildCouplePriorities()`） | 该路径本来就已经是"按 orders 指针反推 owner"（非停放分支 `5448-5459`：`owner->r3r_priority = 1` → 其余按物理序），与重选同口径；而停放分支（`5437-5443`）必须保留 KI-169a / KI-214 的停放语义（链头 `r3r_orders_borrowed = true` 后直接压实）。硬塞一次重选反而会把"链头停着自己的表"误判成"链头就是主人"，故**不动**，只在这里登记口径一致 |

### 3.3 与既有机制的交互

- **KI-215b 进度回写**：解挂点 `R3RPushProgressToOwner(v/u)`（`7763-7764`）排在重选**之后**跑，因此会写到**重选后的** owner —— 与"进度回写到真正持表人"同向，属预期。
- **（c）穿上 / 归属表**：`R3RBorrowControlTraitsLive()` 与 `R3RSettleChainSegments()` 都读 `R3RGetControlSegment()`，重选后它们自动跟随新 owner（这正是"排程跟控制段走"不变式）。
- **`R3RCheckControlSegment()`（INVAR-CTRL）**：重选后 `ctrl` 换成 holder，探针行会反映新形态，便于取数。
- **`R3RNormaliseChainGroups()`** 取控制段掩码（`5460`）等"读控制段"的点都会跟着变 —— 属预期（玩家已拍板行为变更）。
- **缺口 B（`KI-299b`，上一轮已修）**：本轮**不回归**它，两轮改动互不覆盖（缺口 B 是"借来态不许脱"的门禁；本轮是"号码牌按持表人重发"）。

### 3.4 明确不改

- `R3RGetControlSegment()` 的乙口径定义本身（仍 = `r3r_priority` 最小者），只改"谁被排到最小"；
- `R3RMergePriorities()` 的耦合排名规则（耦合**当下**的被动方优先仍然是第一发牌依据，重选只在此后有人真正换手时纠正）；
- `R3ROrderListOwnedInChain()` / KI-239 孤儿归还路径。

## 4. 风险与未确认项（本轮必须登记）

- **R-1**：原生共享表（真共享环）上多段同时 `orders == ol` 时，`R3ROrderListHolderInChain()` 取"物理最前的段头"，是否符合"共享表的门面段"直觉，**未验证**。
- **R-2**：锚表取 `cur->orders_backup`（KI-214 停放形态）时，若该停放副本同时被另一段的 `orders` 引用，归属可能有歧义，**未验证**。
- **R-3**：重选会改 `INVAR-CTRL`、段行宿主（`R3RSegmentTraitRow()` 按控制段判）、`SEGTR-SNAP` 快照内容 ⇒ 第 194 轮订正的判据③（链头 `live` == 控制段 `bk`）**必须重新取数**，不能拿旧日志套。
- **R-4**：重选后若 owner == 链头，`R3RSyncDrivingOrders()` 会走 `owner == chain` 分支**当场归还**借来的表（链头跑回自己停放的排程）。这是设计意图，但会改变"耦合后继续跑车底排程"的观感 —— 复测时要专门确认这是玩家想要的。
- **R-5**：`R3RMergePriorities()` 之后到重选之间若还有别的读 `R3RGetPriorityHead()` 的代码（耦合块内 `R3RGetPriorityHead()` 用于决定合并链 owner），需在实现时逐一核对，不能只看三个函数。

## 5. 复测判据

1. **取数**：出现 `CTRL-REELECT` 时，`old != new` 且 `new_pri == 1`；同一提交点若号码牌本来就指对，则**不出现**该行（不刷屏、不空转）。
2. **归属对账**：`CTRL-REELECT new=X` 之后，同一提交点的 `INVAR-CTRL ... ctrl=X`，且 `R3ROrderListHolderInChain()` 认定 X 确实持表（人肉核对 `SEGTR-SNAP` 的 `bk`/`live`）。
3. **单段链与非分歧场景零回归**：单段链不出现 `CTRL-REELECT`；号码牌本就指对的解挂/耦合/车库编辑，段序、段号、段名、`borrowed`、`NAME-RESTORE`/`UNIT-RESTORE` 留痕与第 195 轮完全一致。
4. **缺口 B 不回归**：第 195 轮的两条 `*-SKIP-BORROW` 留痕照旧命中，`NAME-RESTORE veh=27 side=head` 仍不出现（借用态下）。
5. **跑车语义**：重选把 owner 改成非链头时，提交点后链头仍处于借用态（跑控制段的表，`borrowed=1`）；重选把 owner 改成链头时，应出现当场归还（`CTRL-UNBORROW` / `ORD-RETURN-*`），且跑的是链头自己停放的排程 —— 两者都要在日志里能一对一读出来。
6. **宏观**：全日志 `error` / `assert` / `fatal` 零命中；不再出现"控制段没有排程"的形态（`INVAR-CTRL ... ctrl=X` 而 X 手上既无 `orders` 也无 `orders_backup`）。

## 6. 构建自证

- 复用既有 `d:\sourcecode of JGRPP\_tmp_inc_build.cmd`（**未新建任何 .cmd**）；
- `build\R3R_incbuild.guard.log`（02:03:53）= `GUARD: incremental is safe (no header/lang file is newer than the newest object)` ⇒ 增量合法（本轮只改 `src\train_cmd.cpp`，未碰任何 `src\*.h`）；
- `build\R3R_incbuild.log`（02:05:57）尾部 = `[3/3] Linking CXX executable openttd.exe`；`error C` / `fatal error` / `FAILED:` / `build stopped` 计数 **0**；
- `build\R3R_incbuild.done`（02:05:59）= `EXIT_CODE=0`；
- 时间戳三元组：`src\train_cmd.cpp` **2026-10-02 02:03:38** → `train_cmd.cpp.obj` **02:04:27** → `build\openttd.exe` **02:05:45**（51 609 600 B）；
- `read_lints`（`src\train_cmd.cpp`）= **0 条**；
- exe 串自证：`build\openttd.exe` 内含 **`CTRL-REELECT`**。

⇒ **状态：已实现 + 已编译通过，游戏内复测待做**（判据见 §5 六条）。

## 7. 本轮明确未做

- **读档点不加重选**（理由见 §3.2 表格末行）：该路径 (`R3RRebuildCouplePriorities()`) 本来就已经按 orders 指针反推 owner，与重选同口径；停放分支必须保留 KI-169a / KI-214 语义。
- **`R3RMergePriorities()` 的耦合排名规则不动**（第一发牌依据仍是"被动方在前"）。
- **`R3ROrderListOwnedInChain()` / KI-239 孤儿归还路径不动**（无主表不由本函数插手）。
- §4 的 R-1 ~ R-5 五个未确认项**全部未验证**，需靠复测取数（尤其 R-5：耦合块内其余读 `R3RGetPriorityHead()` 的代码未逐一核对）。

---

# 第 197 轮：缺口 A 复测取证（玩家："感觉修了，但没修干净"）

> 本节先落盘任务卡，取证结果、结论与修法随后补入（防止上下文压缩丢失任务目标）。

## 1. 玩家原话

「感觉修了，但是又感觉没修干净。请你先写临时备忘，然后再查看日志，然后执行修复」——2026-10-02。

⇒ 顺序固定：**先写本备忘（本节）→ 再读日志取证 → 再修复**。修复必须同时更新本备忘（本节 §4/§5）与 `R3R_KNOWN_ISSUES.md`。

## 2. 现场

- `build\R3R_debug.log` = **277 341 B / 4340 行**，写入时间 **2026-10-02 02:13:02**；
- `build\openttd.exe` = **2026-10-02 02:05:45 / 51 609 600 B** = 第 196 轮含缺口 A 修复的产物（exe 内已自证含 `CTRL-REELECT`）；
- ⇒ 日志**晚于** exe 7 分 17 秒，确认玩家跑的就是本轮新 exe，日志可直接作为缺口 A 的第一手复测证据。

## 3. 任务卡（已逐条打勾）

- ✅ **T1 取证**：`CTRL-REELECT` 是否出现、是否符合判据（`old != new`、`new_pri == 1`）、出现次数与 tag 分布 —— **0 次，且应当为 0**（见 §4-T1）；
- ✅ **T2 取证**：`INVAR-CTRL` 的 `ctrl=` 与持表人是否一致（判据③，第 194 轮口径需重新取数，见 §4 R-3）—— **8 处全绿**（见 §4-T2）；
- ✅ **T3 回归**：缺口 B（KI-299b）两条 `*-SKIP-BORROW` 留痕照旧；`NAME-RESTORE side=head` 在借用态下不出现 —— **无回归**（见 §4-T3）；
- ✅ **T4 定性**："没修干净"具体是什么形态（待日志给出）—— **判据③在"号"上破**（见 §4-T4）；
  （原重点怀疑方向，均排除：）
  - R-3 重选后段行宿主/`SEGTR-SNAP` 形态变化；
  - R-4 重选后 owner == 链头时的"当场归还"是否符合预期；
  - R-5 耦合块内其余读 `R3RGetPriorityHead()` 的代码未逐一核对的遗留；
  - 重选**只在四个提交点**跑，中途"换手"（如借用期 owner 被再次编辑）仍可能失配；
- ✅ **T5 修复**：改码（只改 `src\train_cmd.cpp`）→ 复用既有 `_tmp_inc_build.cmd` 增量编译 → 核对 `GUARD: incremental is safe` / `[n/n] Linking` / `EXIT_CODE=0` / obj 晚于源码 / `read_lints` → 写复测判据 —— **已完成**（见 §5）。

## 4. 取证记录（已完成）

统计口径：`build\R3R_debug.log` 全文 `Select-String -SimpleMatch` 计数。

| 探针 | 次数 | 判读 |
| --- | --- | --- |
| `CTRL-REELECT` | **0** | 见 T1 |
| `INVAR-CTRL` | 8 | 见 T2 |
| `SEGTR-SNAP` | 8 | 判据②全绿 |
| `UNIT-RESTORE-SKIP-BORROW` | 1 | T3 回归项 |
| `NAME-RESTORE-SKIP-BORROW` | 1 | T3 回归项 |
| `NAME-RESTORE` | 1 | 仅 `side=head-cleared` |
| `OWNER-MOVE` | 7 | 折叠换 ★ 的归属搬迁（去重边） |
| `ORD-RETURN-ORPHAN` | 2 | 1354/1355 行，KI-239 正常触发 |
| `error` / `assert` / `fatal` | 0 / 0 / 0 | 干净 |

- **T1（`CTRL-REELECT` = 0）**：本会话**应当**为 0。`R3RReelectControlSegment()` 的锚表 `ol` 取 `cur->orders`，而本会话八处 `INVAR-CTRL` 的 `ctrl_pri=1 < head_pri` 全部成立、且每个提交点上 `cur->orders` 都非空 ⇒ `R3ROrderListHolderInChain()` 第一趟（`s->orders == ol && !s->r3r_orders_borrowed`）就撞上 `cur` 自己，`holder == cur` ⇒ 正确退回现状。函数只在"控制段自己没表"时才该出手，本会话没有这种形态。
- **T2（`INVAR-CTRL` 8 处，行号 191 / 334 / 715 / 1171 / 1359 / 2247 / 3227 / 4275）**：`ctrl=` 依次为 6 / 6 / 6 / 6 / **0** / 6 / 6 / **23**；`ctrl_pri=1 < head_pri(2 或 3)` 八处全成立 ⇒ 第 195 轮"控制段从来不是链头所在段"的老结论未变（甲/b/c 两条搬运步骤在每段都成立，问题只在 (a) 选拔）。
- **T3（回归项）**：1432-1433 行 `UNIT-RESTORE-SKIP-BORROW veh=27 u=6 borrowed=1 unit=1 own_bk=4` / `NAME-RESTORE-SKIP-BORROW veh=27 u=6 borrowed=1`（各 1 次，**无回归**）；`NAME-RESTORE` 只出现 3658 行 `veh=50 side=head-cleared`（KI-277 分支），第 195 轮点名要消失的 `side=head` 未出现。
- **T4（"没修干净"的具体形态）**：判据③在**"号"**这一项上破。锚点：
  - `4299`: `i=1 id=1 front=48 star=1 ctrl=0 | live u=2 n="T8701/2-国际段1" gid=65534 | bk u=6 n="" gbk=-1 | row use u=6 n="" g=-1`
  - `4300`: `i=2 id=3 front=23 star=1 ctrl=1 | live u=0 n="" gid=65534 | bk u=0 n="T8701/2-国际段1" gbk=-1 | row use u=2 n="T8701/2-国际段1" g=65534`
  - 上级 `4275`: `INVAR-CTRL tag=couple head=48 nseg=2 ctrl=23 ctrl_pri=1 head_pri=2 borrowed=1`
  ⇒ 名对上了（链头 `live n` == 控制段 `bk n`），**号对不上**（链头 `live u=2` vs 控制段 `bk u=0`）。
- **T4 根因链**：
  - `4074`: `OWNER-MOVE from=6 to=23 orders=29 prio=1 borrowed=0` —— 折叠换 ★ 把车底段的归属从引擎 6 迁到**车厢 23**；
  - `4069`: `FOLD-U veh=23 ... subtype=0x04(front=0 eng=0 wagon=1 freeW=0 artic=0) ... engType=371` —— 23 是车厢，`unitnumber` 恒 0、`unitnumber_backup` 从未被写过；
  - `R3RMoveSegmentOwner()` 刻意不搬 `unitnumber_backup`（指定由 `R3RRecoverSegmentTraits()` 兜底），但那个兜底只被 `R3RSyncHiddenSegmentTraits()` 调用，而它开头 `if (seg == ctrl) continue;` ⇒ **控制段永远拿不到兜底**；
  - `R3RBorrowControlTraitsLive()`（`src\train_cmd.cpp:5904`）借号门禁 = `ctrl->unitnumber_backup != 0` ⇒ 一个数都没借到，链头继续穿上一轮借来的旧号 2。
  - 玩家可见现象：链头（D19Er 48）显示成 `u=2 国际段1`，自己那套号 6 停在 `bk`；控制段 23 自身 `live u=0 n=""`（既无号也无名）。

## 5. 结论与修复（已完成）

- **修法（一处门禁）**：`R3RSettleChainSegments()` 内，`R3RResyncSegmentFronts()` 之后、`R3RBorrowControlTraitsLive()` 之前插入：若 `ctrl != nullptr && ctrl != chain`，先跑 `R3RRecoverSegmentTraits(ctrl)`（只搬 `*_backup`；区段扫描止于下一个 ★，不会偷走本段内普通车正在用的身份），并在**确实补到东西**时留探针 `CTRL-RECOVER <tag> head= ctrl= unit= name=`。位置是硬约束：`R3RRecoverSegmentTraits()` 靠 `IsSegmentFront()` 划界，必须在 `R3RResyncSegmentFronts()` 之后；补出的 `ctrl->unitnumber_backup` 又正是紧接的 `R3RBorrowControlTraitsLive()` 的输入，故必须在它之前。`ctrl == chain` 跳过（那时不需要借号）。号池记账不受影响：`R3RUnitNumberUsedByOther()` / `R3RUnitNumberParkedByOther()` 都遍历整链，同链内"停放位置"变化不改变结论。
- **明确不改**：`R3RReelectControlSegment()` 的锚表口径（本会话证明它只在"控制段无表"时才该出手）；`R3RMoveSegmentOwner()` 不搬 `unitnumber_backup` 的决定（号牵号池记账，搬位置比搬值更危险）；`R3RGetControlSegment()` 定义；第 195 轮缺口 B 门禁。
- **未确认项（如实登记）**：S-1 控制段是车厢（`R3RArrangeIn()` 把与边界相邻的车厢提成链头）是更上游的设计问题，本轮只补号底稿、未动 ★ 选举；S-2 若旧 ★ 身上的滞留号已被别的车当活号穿走，`R3RRecoverSegmentTraits()` 只取 `*_backup`，此时链头仍会穿旧号——本轮现场没有这种形态（6 身上那份是停放态）。
- **构建自证**：复用既有 `_tmp_inc_build.cmd`（**未新建 .cmd**，只改 `src\train_cmd.cpp` 一个文件、未碰 `src\*.h`）；`GUARD: incremental is safe`；`build\R3R_incbuild.log` 尾部 = `[3/3] Linking CXX executable openttd.exe`、`error C`/`fatal error`/`FAILED:`/`build stopped` 计数 **0**；`build\R3R_incbuild.done` = `EXIT_CODE=0`；时间戳三元组 `src\train_cmd.cpp` 03:24:13 → `build\CMakeFiles\openttd_lib.dir\src\train_cmd.cpp.obj` 03:24:57 → `build\openttd.exe` 03:26:16（51 613 184 B）；`read_lints` 0 条；exe 串自证含 `CTRL-RECOVER`。
- **复测判据**：①新出现 `CTRL-RECOVER`（车厢当控制段时 ≥1 次且 `unit` 非 0）；②每个多段提交点满足判据③ = **链头 `live u/n` == 控制段 `bk u/n`**（第 197 轮那处必须由破转立）；③每个多段 `SEGTR-SNAP` 仍满足判据② = `row == 本段自己那一套`；④第 195 轮两条 SKIP 留痕不回归；⑤`NAME-RESTORE` 只允许 `side=head-cleared`；⑥全日志无 `error`/`assert`/`fatal`、无"控制段既无 `orders` 也无 `orders_backup`"形态。
- **状态**：已修（改码 + 编译通过），**游戏内复测待做**（用新 `build\openttd.exe` 重跑同一套动作，重点是"'国际段1' 车底段被折叠换 ★ 后再耦合"这一路）。
