# R3R 第 176 轮临时分析报告：真铰接列车混入耦合 / "谁把列车焊死"

- 日期：2026-09-30
- 分支：feature/decouple
- 本轮改动文件：`src\train_cmd.cpp`（唯一），未碰任何 `src\*.h`、未碰 `src\lang\*`、未新建任何 `.cmd`
- 对应条目：`R3R_KNOWN_ISSUES.md` 第 176 轮（KI-266 订单一层硬闸门 / KI-267 焊死源头）
- 本轮产物：`build\openttd.exe` 2026-09-30 23:58:47（51 559 424 B）

---

## 零、玩家的两条要求（原文要点）

1. **订单一层硬闸门**：列车下一条命令是 `GOTO_COUPLE` / `WAIT_COUPLE` / `DECOUPLE`
   （下称 R3R 命令）时，先检查自身是否存在**真铰接式**；如果存在，就跳到该条命令
   **后面的第一条非 R3R 命令**。
2. **追查并修复"谁把列车焊死"**：找出把含真铰接的列车焊成整节并打上段标记（★）的路径，
   在源头加打散闸门。

玩家同轮澄清：第 175 轮那次"鼻对鼻挂不上"是纯几何问题；此前"参与挂接的列车都无真铰接"
的假设**是错的**。

---

## 一、取证：★ 与真 artic part 出现在同一条链上

证据文件：`build\R3R_debug.log`（190 099 B / 2 823 行，第 174 轮 exe 的复测）。

### 1.1 关键锚点

| 行号 | 原文（节选） | 读法 |
|---|---|---|
| 2525 | `FOLD-U … veh=6 p=-1 n=7 subtype=0x09(front=1 eng=1 wagon=0 freeW=0 artic=0) AH=0 AM=0 SF=1 dir=3` | 车底链头 6：**SF=1（★ SegmentFront）**、`subtype=0x09`（front+engine，即 R3R 造的**假引擎**）、`artic=0`（它自己不是真 artic part）、`AH=0 AM=0`（无解铰接角色位） |
| 2526 | `FOLD-U … veh=7 p=6 subtype=0x02(…) artic=1 AH=0 AM=0 SF=0` | **veh=7 是真 artic part**（subtype 位 `IsArticulatedPart()` 为真），不带 ★ |
| 2527 | `FOLD-U … veh=8 … artic=1 …` | 同 |
| 1820 | `CPL-WAITCLEAR veh=6 head=27 tile=58,26 segFront=1 primary=1` | **在本次合并之前**，车底 6 已经 `segFront=1`（★ 早就有了） |
| 1879 | `CHAIN-ATTRS head=27 n=21 FE=2 SEG=2` | 合并链 21 节、2 个段 |
| 1912 | `CPL-GEO-DONE v=27 u=6 merged=1` | 机车 27 与含真 artic 的车底 6 **耦合成功** |

第 175 轮统计另记：`FOLD-U` dump 里 `veh=19/20/22/23` 等 `subtype=0x02 artic=1`，
即 6 个三节单元（18 节）**整条车底都是真 artic part**。

### 1.2 判读

- 一条链上同时存在 **「真 artic part」** 与 **「★ + 假引擎头」** ⇒ 玩家口中的"整节列车被焊死"。
- 1820 行证明 ★ **不是**本次耦合授予的（耦合前就有）；本次耦合只是又把这条"焊死的段"
  当作正常车底挂了一回。

---

## 二、根因推断：★ 的 5 个授予点里，4 条没有打散前置

全代码库给链授予 ★（`SetSegmentFront()`）的位置只有 5 处（`Get-ChildItem -Recurse | Select-String` 全量核对）：

| # | 位置 | 打散前置 | 判定 |
|---|---|---|---|
| 1 | `CmdMakeSegment`（`vehicle_cmd.cpp:646`） | **有**：`DearticulateChainWithSnapshot(t)` 之后才 `t->SetSegmentFront()` | 这条路径把真 artic 打散成「独立车 + 解铰接角色位」，故走它的列车不会焊死 |
| 2 | 车库拖动 merged-on 标记（`train_cmd.cpp`，本轮前 `:3177`） | **无** | 拖一条真铰接列车进别列 ⇒ 直接打 ★/⊗ |
| 3 | 解挂给解出部分发 ★（`train_cmd.cpp`，本轮前 `:7626`，即「KI-62 解出的段保持段身份」） | **无** | 挂一次、解一次 ⇒ 解出的真铰接车底带 ★ |
| 4 | `Couple()` 提交点 `merged_first->SetSegmentFront()` | **无** | 被挂方（含真 artic）直接成为段 + 假引擎头 |
| 5 | 翻转 `R3RFlipChainBySegments`（`keeps_segment` 分支）与翻转回滚恢复 ★ | 只在已耦合链上操作 | 由 #4 保护 |

### 2.1 为什么第 170 轮的 `FOLDCHK-REFUSE-REAL-ARTIC` 没拦住

该门禁位于**折叠修正**入口（`TryTrainCouple` 内、候选遍历之前），只有几何折叠时才会走到。
现场 1912 行 `CPL-GEO-DONE v=27 u=6 merged=1` 说明这一次几何**不折叠**：
不折叠 ⇒ 不进折叠修正 ⇒ 门禁不触发 ⇒ 一路提交成功并打 ★。

即：**焊死不是某条冷僻支线造成的，而是耦合主路径的正常行为**，只要被挂方含真 artic part。

### 2.2 结论

> **焊死根因** = R3R 的「耦合」（#4）与「车库编辑」（#2）/「解挂保持段身份」（#3）
> 三条路径都会把链提升为段（★ + 假引擎头），而这三条路径**都没有打散前置**；
> 只有 `CmdMakeSegment`（#1）会先 `DearticulateChainWithSnapshot`。
> 因此一条**从未走过升段命令**的真铰接列车（玩家直接买/克隆出来的铰接车底，
> 或存档里 `u_real=1` 的那条车底）只要被挂上一次，就带着真 artic part 拿到了 ★。

---

## 三、本轮实现

### 3.1 KI-266 订单一层硬闸门（玩家要求 1）

| 符号 | 行号 | 说明 |
|---|---|---|
| `R3RIsCoupleCommand(const Order *)` | `:15796` | 判定 `OT_GOTO_COUPLE`(16) / `OT_WAIT_COUPLE`(17) / `OT_DECOUPLE`(15) |
| `R3RSkipCoupleOrdersForRealArtic(Train *)` | `:15837` | 闸门本体 |
| 调用点 | `:16065` | `TrainLocoHandler` 内、`R3REnsureCouplePair(consist)` **之前** |

算法：

```
if (consist == nullptr || consist->orders == nullptr) return false;
if (!R3RChainHasRealArticPart(consist)) return false;        // 正常车底零影响
idx = cur_real_order_index; 若越界则 0
idx_is_couple = R3RIsCoupleCommand(GetOrder(idx));
cur_is_couple = R3RIsCoupleCommand(&current_order);
if (!idx_is_couple && !cur_is_couple) return false;          // 下一条不是 R3R 命令，不动
若 idx_is_couple：从 idx 起向后走过整段连续 R3R 命令，落在第一条普通命令 target
   （整张表全是 R3R 命令 ⇒ 只丢弃当前命令并原地停车，打 all-r3r=1）
cur_real_order_index = cur_implicit_order_index = target;
若 cur_timetable_order_index 有效则同步；
current_order.Free();  SetDestTile(INVALID_TILE);
```

设计要点：

- 索引**直接赋值**，不走 `IncrementRealOrderIndex()` —— 那条路对「已加载的 GOTO_COUPLE」
  是刻意的 no-op（见 `SkipToNextRealOrderIndex`），正是我们要离开的状态。
- `current_order.Free()` 必须做：否则本 tick 后续 `ProcessOrders()` 仍会用残留的
  `GOTO_COUPLE` 驱动耦合寻路。
- 放在 `R3REnsureCouplePair()` 之前：跳过命令的链连"耦合目标锁"都不会建立。
- 探针：`ARTIC-SKIP-R3R veh=… tile=x,y from=… to=… type_from=… type_to=… idx_r3r=… cur_r3r=…`

### 3.2 KI-267 源头闸门（玩家要求 2）

| 位置 | 行号 | 改动 | 探针 |
|---|---|---|---|
| `TryTrainCouple()` 入口 | `:8830` | `v` 或 `u` 含真 artic ⇒ `return false`（此处尚未 `ArrangeTrains`，无状态需回滚） | `CPL-REFUSE-REAL-ARTIC v=… u=… v_real=… u_real=…` |
| `R3RIsCoupleTarget()` | `:1937` | 加 `if (R3RChainHasRealArticPart(t)) return false;` ⇒ 寻路（`yapf_rail.cpp` / `yapf_destrail.hpp` 全走本函数）瞄不到含真铰接的等待车底 | — |
| 车库拖动 merged-on 标记 | 采样 `:2989` + 判断 `:3207` | 拖动块含真 artic ⇒ **不发 ★/⊗** | `DEPOT-SEG-SKIP-REAL-ARTIC src=… block_head=… block_tail=…` |
| 解挂给解出部分发 ★ | `:7633` | 解出方含真 artic ⇒ **不发 ★** | `DECOUPLE-SEG-SKIP-REAL-ARTIC veh=…` |

要点：

- 判据一律只认 subtype 位（`R3RChainHasRealArticPart`，`:1870`），**绝不用角色层**
  （`ArticGroupHead` / `ArticGroupMember`）——打散后的假铰接每节都挂角色位，
  用角色层会把好链也拒掉。
- 车库拖动那条**必须在 `ArrangeTrains()` 之前**采样"块内含真 artic"：
  合并后 `src->Last()` 已是合并链尾，块不再整体可寻址。

---

## 四、构建自证

复用既有 `_tmp_inc_build.cmd`（未新建任何 `.cmd`）：

```
build\R3R_incbuild.guard.log : GUARD: incremental is safe (no header/lang file is newer than the newest object)
build\R3R_incbuild.log (tail): [3/3] Linking CXX executable openttd.exe
build\R3R_incbuild.done      : EXIT_CODE=0

src\train_cmd.cpp            2026-09-30 23:51:50
train_cmd.cpp.obj            2026-09-30 23:53:19   (> 源码 ✓)
build\openttd.exe            2026-09-30 23:58:47   (51 559 424 B, 晚于 obj ✓)

read_lints(src\train_cmd.cpp) : 0 条

exe 探针自证 4/4 命中:
  ARTIC-SKIP-R3R
  CPL-REFUSE-REAL-ARTIC
  DEPOT-SEG-SKIP-REAL-ARTIC
  DECOUPLE-SEG-SKIP-REAL-ARTIC
```

---

## 五、复测判据（待玩家用新 exe 复跑）

1. 含真铰接的机车排了 `GOTO_COUPLE` ⇒ 刚走到该命令时出现
   `ARTIC-SKIP-R3R … type_from=16 type_to=<普通命令>`，且不再有对目标的 `COUPLE-FAIL` 重试循环。
2. 含真铰接的等待车底排了 `WAIT_COUPLE` ⇒ 出现 `ARTIC-SKIP-R3R … type_from=17`；
   其它机车不会再把它选为耦合目标（`R3RIsCoupleTarget` 恒假）。
3. 保险层：人为凑近 ⇒ `CPL-REFUSE-REAL-ARTIC v=… u=… v_real=0 u_real=1`，链序与 ★ 不变。
4. 车库拖动含真铰接的块进别列 ⇒ `DEPOT-SEG-SKIP-REAL-ARTIC`，该块不显示为段。
5. 从含真铰接的链上解下含真铰接的部分 ⇒ `DECOUPLE-SEG-SKIP-REAL-ARTIC`，解出部分不带 ★。
6. **无回归**：全假铰接（打散组）的正常耦合/解挂/翻转行为逐字不变；日志**不应**出现
   上述四类探针。
7. 第 175 轮的"永久挂不上"（机车 48 的 `GOTO_COUPLE`，47 行 `COUPLE-FAIL`）应由判据 1 消除。

---

## 六、未确认项 / 未做

1. **旧存档里已经焊死的链不会自动复原**。本轮只堵"产生路径"。已在档的"真 artic + ★"
   被判据 1~3 隔离（不再参与耦合、不再是耦合目标），但其**段划分仍失真**（KI-253 现象：
   段数塌缩、几何未展开）。是否加"读档时对含真铰接的链自动打散 / 清 ★"需玩家拍板——
   落点候选 `R3RResyncSegmentFronts`（`:4711` 已有"真 artic part 不携带 ★"的局部处理，
   但它只挪 ★ 不拆真 artic）。
2. **本轮不做自动打散**，只做"拒绝耦合 / 不发 ★"。原因：跨文件打散入口不存在
   （`R3RDearticulateOneGroup` 是 `vehicle_cmd.cpp:411` 的文件内 static），
   且在车库拖动中途改车辆身份（烘焙 weight/power/speed 覆盖 + 角色位）风险高。
   若玩家要"含真铰接的链被自动规范化成假铰接后正常参与耦合"，需另开一轮把
   `DearticulateChainWithSnapshot` 抽成可跨文件复用的入口。
3. **实机未复测**：以上 7 条判据全部待跑。
4. **现场"第一次焊死"的日志已不可考**：`build\R3R_debug.log` 每次运行覆盖，
   现有 190 KB 日志中**没有** `MAKESEG-CMD`（说明该车底从未走过升段命令）、
   也没有 `R3RDUMP`（读档 dump）。因此"★ 最初由哪一次操作授予"只能从代码路径推断
   （见第二章），无法从日志逐字回溯。本轮加的 4 个探针正是为了下一次能当场抓到。
5. 未动 `R3RFlipChainBySegments` / `R3RUndoLogicalFlip` 的 ★ 迁移路径
   （由 `TryTrainCouple` 入口否决保护），未动 `R3RResyncSegmentFronts` 的派生逻辑。
6. 未提交 git（工作区仍为未提交改动）。
