# 第 183 轮：列车名字乱传播 + 29 条调度命令里「连续四条 R3R 命令」仍异常

日期：2026-10-01　状态：**取证登记中（尚未定性完毕、未改任何源码）**
现场日志：`build\R3R_debug.log`（13 416 行 / 846 984 B，mtime 2026-10-01 13:23:27）
产生该日志的 exe：`build\openttd.exe`（2026-10-01 12:40:19，即第 182 轮 KI-279 的构建）

---

## 一、玩家报告原文（复述，逐字）

> 我已取得日志，目前，还有以下问题：第一个是有一个列车名字乱传播，第二个是那个 29 条的调度命令那个连续四条 R3R 命令还是出现了一些问题

拆成两条独立缺陷：

- **问题 ①「列车名字乱传播」**：某列车的名字在挂车 / 解挂过程中被错误地传播到别的车（链头顶上了不属于自己的名字，或解挂时把不相干的名字还了回去），玩家在界面上看到「名字跑到别的车上 / 贴错车」。
- **问题 ②「29 条调度命令里连续四条 R3R 命令仍有问题」**：那条含 **29 条订单**的排程中存在 **idx18..21 连续四条 R3R 命令**（`DECOUPLE / WAIT_COUPLE / DECOUPLE / WAIT_COUPLE`），这四条的执行结果仍然不对（命令被静默跳过 / 停死 / 排列异常）。

（注意：这两条都是玩家**在第 181 轮续 + 第 182 轮修复后的新 exe 上复测**得到的结果，因此不能简单套用第 181 轮续 KI-277 / 第 182 轮 KI-279 的结论，必须用本日志重新取证。）

---

## 二、日志可用性佐证

| 项 | 值 |
|---|---|
| `build\R3R_debug.log` mtime | 2026-10-01 13:23:27 |
| `build\openttd.exe` mtime | 2026-10-01 12:40:19（第 182 轮 KI-279 构建） |
| 结论 | 日志晚于 exe ⇒ **是本轮修复版 exe 产生的现场**，可用 |

探针计数（全文）：

| 探针 | 次数 |
|---|---|
| `NAME-XFER` | 16 |
| `NAME-RESTORE` | 12 |
| `UNIT-PARK` | 2（line 12 / 4945，均为 `veh=6 bk=2`） |
| `UNIT-RESTORE` | 2（line 382 / 5554，均为 `veh=0 id=1`，**无 `side=head-cleared`**） |
| `COUPLE-OK` | 16 |
| `DECOUPLE-DONE` | 14 |
| `COUPLE-FAIL` | 330（尾部刷屏，集中在 `loco=48 tx=58,67` 与 `loco=24 tx=20,9`） |
| `ARTIC-SKIP-R3R` | 0 |

---

## 三、初步取证（现场锚点）

### 3.1 名侧全表（问题 ① 的取数面）

`NAME-XFER` / `NAME-RESTORE` 逐条（行号:原文）：

```
   84 NAME-XFER head=24 u=0  head_own=1 got_u=1 keep_u=1
  450 NAME-RESTORE veh=24 side=head
  601 NAME-XFER head=27 u=0  head_own=1 got_u=1 keep_u=1
  880 NAME-RESTORE veh=27 side=head
 1306 NAME-XFER head=27 u=6  head_own=1 got_u=1 keep_u=1
 1530 NAME-RESTORE veh=27 side=head
 1759 NAME-XFER head=50 u=6  head_own=0 got_u=1 keep_u=1     ← head_own=0
 2037 NAME-RESTORE veh=50 side=head                          ← 却是 side=head
 2439 NAME-XFER head=48 u=6  head_own=0 got_u=1 keep_u=1     ← head_own=0
 2760 NAME-RESTORE veh=48 side=head                          ← 却是 side=head
 2939 NAME-XFER head=29 u=23 head_own=0 got_u=1 keep_u=1     ← head_own=0
 3313 NAME-RESTORE veh=29 side=head                          ← 却是 side=head
 3743 NAME-XFER head=27 u=0  head_own=0 got_u=1 keep_u=1     ← head_own=0（27 本来没名字）
 4551 ORD-XFER keep veh=27 owner=6 cur_real=5 parked_real=9  ← 第二次耦合（仍在借用中）
 4552 NAME-XFER head=27 u=23 head_own=1 got_u=1 keep_u=1     ← head_own=1！身上是上一轮借来的名
 4553 COUPLE-SKIP-DECOUPLE head=27 stepped=1 real=21 type=17
 --- 第二轮（同构复现）---
 5018 NAME-XFER head=24 u=0  head_own=1 → 5622 NAME-RESTORE veh=24 side=head
 5939 NAME-XFER head=27 u=0  head_own=1 → 6353 NAME-RESTORE veh=27 side=head
 7652 NAME-XFER head=27 u=6  head_own=1 → 8090 NAME-RESTORE veh=27 side=head
 8441 NAME-XFER head=50 u=6  head_own=0 → 8921 NAME-RESTORE veh=50 side=head
 9825 NAME-XFER head=48 u=6  head_own=0 → 10362 NAME-RESTORE veh=48 side=head
10677 NAME-XFER head=29 u=23 head_own=0 → 11133 NAME-RESTORE veh=29 side=head
12006 NAME-XFER head=27 u=0  head_own=0
13044 ORD-XFER keep veh=27 owner=6 cur_real=5 parked_real=9
13045 NAME-XFER head=27 u=23 head_own=1
13046 COUPLE-SKIP-DECOUPLE head=27 stepped=1 real=21 type=17
（之后日志结束，无 NAME-RESTORE ⇒ 27 仍顶着名字退场）
```

**两个疑点（问题 ① 候选根因）：**

1. **`head_own=0` 的样本全部走了 `side=head`（备份归还），全文没有任何 `side=head-cleared`。**
   第 181 轮续 KI-277 补的那一支（`v->name.clear()` + `side=head-cleared`）在本日志 **12/12 次都未触发**，说明"链头本来没名字"的归还走的不是新分支。需要核对 `NAME-RESTORE side=head` 打印时 `v->name_backup` 为什么非空（谁把它填了）。

2. **第二次耦合时把「借来的名字」停成了链头自己的名字。**
   3743 行 27 本来没名字（`head_own=0`），借了 veh 0 的名字；到 4552 行 27 身上已经是借来的名字，此时 `head_own=1`，于是 `Couple()` 的 `if (v->name_backup.empty()) v->name_backup = v->name;` 会**把 veh 0 的名字写进 27 的 `name_backup`** ⇒ 之后解挂会把这个名字永久留给 27（名字从"车底"传播到"机车"）。13045 复现同一形态，且日志在 13046 之后结束、**没有再出现 `NAME-RESTORE`**。

### 3.2 29 条命令（问题 ② 的取数面）

- 29 条订单的表属于 `owner=6`（探针 `ORD-AFTER-COUPLE ... n=29 ... owner=6`），另一张 6 条表属于 `owner=0`（`n=6 ... owner=0`，见 3755 / 12018）。
- 由 `DECOUPLE-FIRE real=` 与 `COUPLE-OK real/type=` 反推出的表结构（**未逐条 dump 订单表核对**）：
  - `idx3 / 6 / 9 / 12 / 15 / 18` 均为 `DECOUPLE`（即每三条一组：`GOTO 站点` → `DECOUPLE` → `WAIT_COUPLE`），`idx0` 为 `WAIT_COUPLE`，末条 `idx28` 为 `TIMETABLE`。
  - **玩家说的"连续四条 R3R 命令" = `idx18 DECOUPLE` / `idx19 WAIT_COUPLE` / `idx20 DECOUPLE` / `idx21 WAIT_COUPLE`**（`idx17 = GOTO 5`）。
- 第一轮现场（关键序列）：

```
3251 DECOUPLE-FIRE consist=29 tx=58 ty=19 real=17 mode=2 num=1 segs=1 eff=23
3283 DECOUPLE-DONE u=23 co=1 real=19 tx=58 ty=19        ← 被解出的 23 链停在 idx19(WAIT_COUPLE)
3329 LOCO-AFTER-DECOUPLE veh=29 curType=0 real=7 tile=58,19
3743 NAME-XFER head=27 u=0 head_own=0
3744 COUPLE-OK loco=27 rear=5 consist=0 real=4 type=6    ← 27 与 0..5 合并，跑 6 条表 idx4
3755 ORD-AFTER-COUPLE head=27 n=6 real=4 borrowed=1 owner=0
4486 ARRANGE-IN dh=27 dst=5 sh=6 src=6                  ← 与等待中的 6..23 再次合并
4551 ORD-XFER keep veh=27 owner=6 cur_real=5 parked_real=9
4552 NAME-XFER head=27 u=23 head_own=1
4553 COUPLE-SKIP-DECOUPLE head=27 stepped=1 real=21 type=17   ← idx20 的 DECOUPLE 被"跳过一支"
4554 COUPLE-OK loco=27 rear=23 consist=23 real=21 type=17     ← 落点直接到 idx21(WAIT_COUPLE)
4583 ORD-AFTER-COUPLE head=27 n=29 real=21 ... owner=6 u_has_orders=0
（此后到本轮结束，27 一直停在 idx21；没有任何 DECOUPLE-FIRE 为 idx20 触发）
```

⇒ 等待方 6..23 本来停在 `idx19 WAIT_COUPLE`，被 27 挂上后**应当执行 `idx20 DECOUPLE`**，但被 `R3RSkipUnfireableDecoupleOrder()` 当成"永远触发不了的 DECOUPLE"**静默跳过**，落点直接跳到 `idx21`。这就是第 181 轮续 KI-278 修复的**副作用面**：卡死没了，但那条命令**没有执行**（命令被吃掉）。

### 3.3 另一条不动现场：`SKIP-STOPPED veh=30`

```
  63 SKIP-STOPPED veh=30 order=0 real=17 spd=0 tile=1,11 parked=0 front=1 nord=29 idx=0 tt=65535
（此后 12456 / 12497 / 12543 / … / 13260 / 13306 / 13353 / 13401 反复出现，直到日志结束）
```

- `nord=29`：与 owner=6 的 29 条表同数；`real=17` 正是 `idx17 GOTO 5` 两侧的位置；`order=0`（当前订单为 OT_NOTHING）；车停在 `tile=1,11`（车库格）且 `spd=0`。
- 从日志第 63 行到末尾（13 401 行）该车**一步未动**，`SKIP-STOPPED` 是 `TrainCoupleHandler()` 对"停稳车辆"的早退，本身是设计内行为；但它与问题 ② 的位置（`idx17..21`）高度重合，需要确认它是否就是"29 条命令"这条链的另一半（是否被永久搁置）。**待确认项。**

---

## 四、初步定性（待源码核对后定稿）

- **问题 ① 候选根因（两名侧缺陷）**：
  - (a) 第二次耦合时链头身上是**借来的名字**，`head_own` 因此为 1，`Couple()` 会把这个借来的名字停进 `v->name_backup` ⇒ 解挂/车库路径再把它当成本车身份还回去，名字跨车传播（锚点：3743 → 4552、12006 → 13045，且之后无 `NAME-RESTORE`）。
  - (b) `head_own=0` 的样本（1759/2439/2939/8441/9825/10677）全部走 `side=head`，与第 181 轮续 KI-277 新增的 `side=head-cleared` 判据**对不上**，说明归还路径上 `v->name_backup` 的来源另有其人（谁填的、填的是什么名字，需要读 `R3RParkTrainName()` / 段行镜像 / 车库路径的写点）。
- **问题 ② 候选根因**：`R3RSkipUnfireableDecoupleOrder()` 在耦合落点（`Couple()` 提交点）把 `idx20 DECOUPLE` 静默跳过（锚点 4553 / 13046），使这条命令永不执行。需要判断：是"该 DECOUPLE 在语义上根本不该由这次耦合触发"，还是"落点判据（只有 `cur_real` 是 GOTO 才允许 DECOUPLE 触发）在这个 `WAIT_COUPLE → DECOUPLE` 的接续形态下错了"。

---

## 五、未确认项

- **R-1**：`NAME-RESTORE side=head` 打印时 `v->name_backup` 由谁写入？`head_own` 探针的确切定义（是 `!v->name.empty()` 还是含 `name_backup`）需读源码确认，否则 3.1 的疑点 1 可能被我读反。
- **R-2**：`veh=30`（tile=1,11 / real=17 / nord=29）到底是哪条链、属于哪张表，与 owner=6 的 29 条表是否同一张。
- **R-3**：`idx18..21` 这四条命令**玩家期望的正确行为**是什么（`idx20` 的 `DECOUPLE` 应当在何时、由谁触发解下哪一半）——需要向玩家确认一次，再决定"让它能触发"还是"承认它不可触发但必须留痕/报警"。
- **R-4**：`COUPLE-FAIL` 330 次（`loco=48 @58,67`、`loco=24 @20,9`）是否为独立缺陷（`CPL-PATHFOUND found=0` 属"候选车底未到位"的正常形态）还是与问题 ② 同源。

---

## 六、下一步与复测判据（草案）

1. 读源码核对 `head_own` 探针定义 + `NAME-RESTORE` 两支的打印条件 + `v->name_backup` 的全部写点 ⇒ 定稿问题 ① 根因。
2. 读 `R3RSkipUnfireableDecoupleOrder()` 与 `Couple()` 提交点落点判据 ⇒ 定稿问题 ② 根因（并结合 R-3 向玩家确认期望）。
3. 修完后复跑同场景，判据草案：
   - ① `NAME-XFER head=27 u=23`（第二次耦合）时不应把借来的名字停进 `name_backup`；解挂/结束时 27 不得显示 veh 0 或 veh 23 的名字；
   - ② `idx20 DECOUPLE` 要么被真正执行（出现对应 `DECOUPLE-FIRE/DONE real=20`），要么明确留痕（不再静默 `stepped=1` 直接跳到 `idx21`）；
   - ③ `veh=30` 工况定性（若为搁置则需解释为何 `nord=29` 却永不推进）。

---

## 七、玩家第二轮指认（同日、同一现场）与新增取证

玩家原话：

> 我报告，我的两个场景都和 veh6 有关。第一个问题是当别的段和 veh6 耦合的时候，veh6 作为控制段，显性了自己的名称，但是其他段和 veh6 解挂之后还是继承的 veh6 的名字。然后，是那个连续 4 条 R3R 命令的第三条执行有问题

### 7.1 指认 ①「veh6 是控制段；别的段与它解挂后仍继承它的名字」

**指认与现场完全对上**：`veh6` 就是那张 29 条表的**控制段**（`UNIT-PARK veh=6 bk=2` line 12 / 4945、`4551 ORD-XFER keep veh=27 owner=6`、`ORD-AFTER-COUPLE ... owner=6`）。日志里"别的段"就是 `27 / 50 / 48 / 29` 这些链头，它们反复挂在 `u=6`（veh6 所在链）上：

```
1306 NAME-XFER head=27 u=6  head_own=1 → 1530 NAME-RESTORE veh=27 side=head
1759 NAME-XFER head=50 u=6  head_own=0 → 2037 NAME-RESTORE veh=50 side=head
2439 NAME-XFER head=48 u=6  head_own=0 → 2760 NAME-RESTORE veh=48 side=head
2939 NAME-XFER head=29 u=23 head_own=0 → 3313 NAME-RESTORE veh=29 side=head
```

**读码后定稿：三处写点全都会把「借来的名字」停成「自己的名字」**

| 代码锚点 | 函数 / 调用场景 | 关键语句 | 为什么出事 |
|---|---|---|---|
| train_cmd.cpp:10254-10258 | `Couple()` 借名（耦合提交点） | `head_had_own = !v->name.empty();`<br>`if (v->name_backup.empty()) v->name_backup = v->name;`<br>`v->name = u->name;` | 停在 backup 里的是**当前活字段**；第二次耦合时活字段已经是上一次借来的名字（`4552 NAME-XFER head=27 u=23 head_own=1`）⇒ **veh 0 的名字被停成 27 "自己的"名字** |
| train_cmd.cpp:2762-2768（调用点 3203 车库身份迁移、8863 `R3RRelocateFrontIdentity`） | `R3RParkTrainName()` | `if (v->name_backup.empty() && !v->name.empty()) v->name_backup = v->name; v->name.clear();` | 身份迁移若发生在借名之后，被停进 backup 的是**借来的**名字；之后它就成了这节车的"原名" |
| train_cmd.cpp:5648-5655 | `R3RBorrowControlTraitsLive()`（车库拖动 / 读档边） | `if (chain->name_backup.empty()) chain->name_backup = chain->name; chain->name = ctrl->name_backup;` | 同上；这本来就是"veh6（控制段）的名字显性到链头"的正规通道，此时链头自己的名字必须先被停 —— 若链头此刻正顶着别人的借名，就把借名一起停进去了 |

**归还端只认「backup 非空」**（train_cmd.cpp:7660-7672，整块位于 `if (v->r3r_orders_borrowed)`（7346）之内）：

```cpp
7659 if (u->name.empty()) R3RRestoreTrainName(u);
7660 if (!v->name_backup.empty()) { v->name = v->name_backup; v->name_backup.clear(); ... side=head }
7664 else if (!u->name.empty() && v->name == u->name) { v->name.clear(); ... side=head-cleared }
```

⇒ 只要 `name_backup` 被上面三处任一污染，解挂时走的必是 `side=head`，**第 181 轮续 KI-277 专为"链头本来没有名字"新增的 `side=head-cleared` 支路永远轮不到**。这与现场 12 次 `NAME-RESTORE` **全部 `side=head`、0 次 `side=head-cleared`** 完全吻合；对照号侧（第 182 轮 KI-279）已出现 2 次 `UNIT-RESTORE veh=0 id=1 side=head-cleared`（解挂两轮各一次），名侧一次都没有 ⇒ **名侧那条支路在现场等价于死代码**。

⇒ 玩家所见「其他段和 veh6 解挂之后还是继承 veh6 的名字」= 那些段（27/50/48/29/24…）在耦合或链头身份迁移期间**把 veh6 的名字停成了自己的 `name_backup`**，解挂时又把它当成自己的名字穿回去。

补充：归还块被闸门 `if (v->r3r_orders_borrowed)`（train_cmd.cpp:7346）包住 ⇒ **"没有借用排程"的解挂根本不执行名字/号归还**，链头继续顶着借来的名字。这是同一症状的第二条通路（现场第二轮 13045 之后到日志结束都没有 `NAME-RESTORE`，与此一致）。

### 7.2 指认 ②「连续四条 R3R 命令的第三条」

日志 `U-ORD` 订单表镜像（29 条表，第一份 dump 在 line 437-443）逐条证实四条连续 R3R 的位置：

```
437 U-ORD 16 type=17   (OT_WAIT_COUPLE)
438 U-ORD 17 type=1    (OT_GOTO_STATION)
439 U-ORD 18 type=15   (OT_DECOUPLE)     ← 第 1 条
440 U-ORD 19 type=17   (OT_WAIT_COUPLE)  ← 第 2 条
441 U-ORD 20 type=15   (OT_DECOUPLE)     ← 第 3 条 = 玩家所指"第三条"
442 U-ORD 21 type=17   (OT_WAIT_COUPLE)  ← 第 4 条
443 U-ORD 22 type=1    (OT_GOTO_STATION)
```

（`15=OT_DECOUPLE`、`17=OT_WAIT_COUPLE`、`1=OT_GOTO_STATION`；line 867-873 / 1517-1520 等多处同构重复。）

⇒ 玩家所指"第三条" = `idx20 DECOUPLE`，正是 `4553 COUPLE-SKIP-DECOUPLE head=27 stepped=1 real=21 type=17` / `4554 COUPLE-OK ... real=21` 静默跳过的那个（第二轮 `13046` 同构复现）。**玩家指认与日志落点逐字一致**，KI-281 的定位由"推测的 idx20"升级为"已证实的 idx20"。

---

## 八、本轮范围声明

本备忘只做**问题登记 + 现场取证 + 读码定性**，未修改任何源码；KI 条目（第 183 轮小节）同步登记为「取证中 / 未修」（KI-280 根因已定稿，待改码）。
