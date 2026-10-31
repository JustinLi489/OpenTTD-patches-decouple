# R3R 第 326 轮：自动填充命令干扰列车正常执行指令（临时分析报告）

- 日期：2026-10-26
- 玩家报告原话：「我发现，这个自动填充的命令，会对列车正常的执行指令有影响」
- 输入日志：`D:\temp\codebuddy-dropped-files\d17de043-a3d6-4f54-846e-169bdffd9bf7\R3R_debug.log`
- 提取物：`D:\temp\chk_auto.txt`（AUTO-ORD 全家族共 8 行）
- 本轮状态：**根因已定性（记录侧把「借用表的 cur_real_order_index」当成「自己表的插入位置」）；修法 2 已落地（anchor 借用期改取自己表尾部）并已构建，游戏内复测待做。** 详见 §八。

---

## 一、日志锚点（原文）

`D:\temp\chk_auto.txt` 全部内容（行号 = 日志行号）：

```
  298: AUTO-ORD-DROP kind=depot dest=0 seg=1 head=0 reason=not-in-source-range leg=[1,2) n=33
  614: AUTO-ORD-DROP kind=depot dest=0 seg=3 head=54 reason=not-in-source-range leg=[1,2) n=33
  615: AUTO-ORD-DROP kind=depot dest=0 seg=1 head=54 reason=not-in-source-range leg=[1,2) n=33
  691: AUTO-ORD-DROP kind=depot dest=0 seg=1 head=54 reason=not-in-source-range leg=[1,2) n=33
  832: AUTO-ORD seg=3 head=54 slot=0 kind=station dest=0 iv=[1,2) cursor=1 anchor=1 real=1 cur=station own=0 borrowed=1 n=4
  880: AUTO-ORD seg=1 head=0 slot=0 kind=station dest=0 iv=[0,1) cursor=1 anchor=0 real=0 cur=station own=0 borrowed=1 n=7
 1377: AUTO-ORD-SKIP seg=3 head=54 slot=0 from=1 to=2 iv=[1,2) order=0
 1509: AUTO-ORD-DROP kind=station dest=0 seg=1 head=0 reason=not-in-source-range leg=[4,5) n=33
```

周边上下文（按行号摘录）：

```
 830: ORD-IDX-WATCH site=ordlist-insert-in tag=- veh=54 real=-1->1 impl=-1->1 tt=-1->1 n=-1->33 artic=-1->0 ... front=1 spd=61
 831: ORD-IDX-WATCH site=ordlist-insert-out tag=- veh=54 real=-1->1 impl=-1->1 tt=-1->1 n=-1->33 ... front=1 spd=61
 832: AUTO-ORD seg=3 head=54 slot=0 kind=station dest=0 iv=[1,2) cursor=1 anchor=1 real=1 cur=station own=0 borrowed=1 n=4
```

```
1357: DEPOT-ARR veh=54 spd=0 real=2(15) curType=4 stuck=0 ... tx=12 ty=15
1358: DECOUPLE-FIRE consist=54 tx=12 ty=15 real=2 mode=2 num=1 segs=2 eff=0 plat=30/0
1365: ORD-IDX-WATCH site=syncdriving-in tag=keep veh=54 real=-1->2 ... n=-1->33
1366: ORD-RETURN-OWN veh=54 num=4 real=2->0 impl=2->0 bk=65535/65535 orphan=0
1367: ORD-IDX-WATCH site=syncdriving-out-owner ... veh=54 real=-1->0 ... n=-1->4
1373: ORD-IDX-WATCH site=settle-in tag=decouple-v veh=54 real=1->1 impl=1->1 tt=1->1 n=33->4 artic=0->0
1374: SEGID-KEEP tag=decouple-v head=54 id=3 nseg=1 reason=auto-range
1377: AUTO-ORD-SKIP seg=3 head=54 slot=0 from=1 to=2 iv=[1,2) order=0
1379: SEGTR-SNAP   i=1 id=3 front=54 star=1 ctrl=1 | live u=6 n="HXN5B-XXXX" gid=65534 | bk u=0 n="" gbk=-1 | row use u=6 n="HXN5B-XXXX" g=65534 | auto n=1 first=[1,1)
1380: ORD-IDX-WATCH site=settle-out tag=decouple-v veh=54 real=1->2 impl=1->2 tt=1->2 n=33->4
```

---

## 二、已确认事实

1. `veh=54` 在挂车期借用了所有者（`owner=6`）的 **33 条**计划表；它自己那张表是 **4 条**。
   - `:830` `ordlist-insert-in ... n=-1->33`、`:832` `n=4`、`:1373` `n=33->4` 三处互证。
2. 自动记录在 **借用期** 发生：`:832` `own=0 borrowed=1`（写入的是自己的 `orders_backup`，不是正在跑的借用表）。
3. 记录的区间是 `iv=[1,2)`，`anchor=1`，`real=1`。`anchor` 完全来自 `seg->cur_real_order_index`（借用表的索引）。
4. 解挂（`:1358 DECOUPLE-FIRE`）后 `veh=54` 换回自己的 4 条表（`:1373 n=33->4`），索引按借用表继承为 `real=1`（`:1373 real=1->1`）。
5. 提交点执行侧立刻跳过该区间：`:1377 AUTO-ORD-SKIP from=1 to=2 iv=[1,2) order=0`，`:1380 real=1->2`。
   `order=0` = `OT_GOTO_STATION`，即被跳过的那一格是一条**去车站**的命令。

## 三、猜测根因（待验证）

**记录侧把「正在驱动的借用表的 `cur_real_order_index`」直接当成「段自己那张表的插入位置」。**

代码证据（`src\train_cmd.cpp`）：

- `:6969-6975`：
  ```cpp
  VehicleOrderID anchor = 0;
  if (seg->cur_real_order_index != INVALID_VEH_ORDER_ID) {
      anchor = seg->cur_real_order_index;
      const Order *const here = ol->GetOrderAt(seg->cur_real_order_index);
      if (here != nullptr && here->IsType(OT_GOTO_COUPLE)) anchor = (VehicleOrderID)(anchor + 1);
  }
  ```
  这里的 `anchor` 取自驾用期的 `cur_real_order_index`（借用表编号），而 `ol`（`:6931 num = ol->GetNumOrders()`）是**自己的表**。两张表长度 33 vs 4，索引语义不可通用。
- `:7003-7004`：新区间直接落在 `begin = anchor`。
- `:7040` → `:6764-6775` `R3RAutoOrderAppend()`：`ol->InsertOrderAt(Order(o), at)`，**在自己表的 `anchor` 处插入**（不是追加到尾部），并且因为 `ol != seg->GetEffectiveOrders()`（借用期），`:6770-6774` 的索引 shift 被跳过。
- 执行侧 `:7139-7185` `R3RSkipAutoRecordedOrders()`：按 `row->auto_ranges_begin/count` 无条件把 `cur_real_order_index` 推到 `begin+count`。

⇒ 两个后果，都会"干扰正常指令"：

- **A. 插入位置错位**：`anchor` 是借用表的编号，却在自己表里插入。玩家自己表里位于 `anchor` 之后的命令全部后移一格，而任何按旧索引记住的位置（段行 `wait_idx`、解挂/挂接决策、`cur_real_order_index` 因为不 shift 而停留）都错位。玩家写的命令顺序被打乱。
- **B. 执行侧跳过真实命令**：解挂后索引按借用表继承（`:1373 real=1->1`），恰好落在自动区间 `[1,2)` 内 ⇒ `:1377` 把索引推到 2，`:1380` 落定。若该区间在自己表里覆盖的是玩家的真实命令（而非自动插入的那条），列车就会漏执行/漏停一条命令。日志 `order=0`（GOTO_STATION）说明被跳过的那一格确实是车站命令。

## 四、疑点（登记）

- **D-1（读数矛盾）**：`:1377` 用 `iv=[1,2)`（`count=1`）跳过，而同一提交点的 `:1379 SEGTR-SNAP ... auto n=1 first=[1,1)` 显示 `begin=1, begin+count=1` 即 `count=0`。两者必有一处打印/取值错误，需查 `SEGTR-SNAP` 的 `first=` 计算来源（可能读的是段行镜像而 skip 读的是 `row`）。
- **D-2（校验对象也不一致）**：`:298/:614/:615/:691` 的 `AUTO-ORD-DROP ... leg=[1,2) n=33` 是 C-2 白名单校验，`n=33` 说明它校验的是**借用表**（33 条）；而记录实际写进的是自己的 4 条表。校验与写入不是同一张表，"来源区间"结论不可直接套用到写入表上。
- **D-3（借用期 own=0）**：记录落在 `orders_backup`，而 `R3RAutoOrderAppend` 的索引 shift 只对 `ol == GetEffectiveOrders()` 生效 ⇒ 借用期的记录既不调整自己表索引，也不调整任何缓存，落地时刻（解挂）只靠继承来的 `real` 撞运气。

## 五、待验证清单

1. `R3RSegmentOwnOrderList()` 在借用期返回哪个表；记录器 `ol`（`:6931` 之前）具体取的是 `orders_backup` 还是有效表。
2. `veh=54` 那 4 条表在记录前后的实际内容（需要 `ORD` 全表 dump 或在 `AUTO-ORD` 行补 `list=[type:dest,...]`），确认插入点 1 是否落在玩家命令中间。
3. `SEGTR-SNAP` 的 `auto n=/first=` 来源（D-1）。
4. `:1377` 之后 `veh=54` 是否真的漏停该站（后续日志中 `DEPOT-ARR`/进站/`WAIT` 行为）。

## 六、修复方向（待拍板，未落地）

- **修法 1（治根，推荐）**：借用期不写自己的表。自动记录只进一个"暂存区间"（随时序 append），解挂/换回自己表时再按**自己表的真实** `cur_real_order_index` 一次性落到尾部，绝不 Insert 到中间。
- **修法 2（最小改动）**：`anchor` 在借用期恒取 `ol->GetNumOrders()`（自己表尾部追加），并在借用期不设置可执行跳过区间（只记录、不 skip），解挂后由真实索引重新判定。
- **修法 3（保守）**：`R3RSkipAutoRecordedOrders()` 增加签名/身份校验之外的硬条件——只在 `ol == GetEffectiveOrders()` 且该格确实是自动插入的订单（可用 `order` 与 `want` 比对）时才跳过，否则清空该区间（宁可多跑一条记录，不可吞玩家命令）。

## 七、复测判据（修好后）

1. 解挂后 `settle-out` 的 `real` 不再被推到 `begin+count`，或推之前必须打印被跳过那条订单的完整字段并证明它是自动记录。
2. 玩家手写表的命令条数/顺序在 `AUTO-ORD` 前后不变（除新增记录条目外），且索引位移与记录条目数一致。
3. `AUTO-ORD-SKIP` 命中时该格 `order` 与记录时的 `want` 同类型同目的地。
4. 负判据：无自动记录的普通列车零 `AUTO-ORD-SKIP`。

---

## 八、修法落地（第 326 轮续，2026-10-26）

玩家口径：只做「不影响正常指令」，采用修法 2（最小改动）。

### 8.1 已落地改动（仅 `src\train_cmd.cpp`，未碰任何 `src\*.h`）

1. **anchor 借用期改取自己表尾部**（`R3RAutoRecordChainOrders()`，原 `:6969-6975`，现 `:6958-6989`）：

```cpp
const bool follows_other_plan = seg->IsR3RFollower() || seg->GetEffectiveOrders() != ol;
VehicleOrderID anchor = 0;
if (follows_other_plan) {
    anchor = num;                        /* KI-503: 借用表的索引对自己表无意义，追加到尾部 */
} else if (seg->cur_real_order_index != INVALID_VEH_ORDER_ID) {
    anchor = seg->cur_real_order_index;  /* 原有 KI-397 语义不变 */
    const Order *const here = ol->GetOrderAt(seg->cur_real_order_index);
    if (here != nullptr && here->IsType(OT_GOTO_COUPLE)) anchor = (VehicleOrderID)(anchor + 1);
}
if (anchor > num) anchor = num;
```

- 判据用 `IsR3RFollower()`（= `r3r_follow_orders != nullptr`，修法 D 的借用标志）**并或上** `GetEffectiveOrders() != ol`（兜住"自己的表停在 `orders_backup` 而 `orders` 为空"的旧档/畸形态，此时索引同样不指向 `ol`）。
- 效果：同一次交接里同一批记录全部落在**自己表尾部的同一个区间**（`anchor == num == begin + count` ⇒ `:7003` 命中已存在区间并延长），不再往玩家命令中间插。
- 保留原有非借用路径（`anchor = cur_real_order_index`，KI-395/397 语义逐字不变）。

2. **区间可信度加固（沿用本会话早前两处，防止旧档/手改表触发越权跳过）**：
   - `:6941-6947` 放弃区间时要求 `signature != 0` 且 `R3RAutoOrderHash` 复算一致，否则一律 `AUTO-ORD-DROP`；
   - `:7177-7189` `R3RSkipAutoRecordedOrders()` 跳过前同样复算签名，不一致即清空区间并打 `AUTO-ORD-STALE`，不跳过。

### 8.2 刻意不做

- 不在借用期"只记录不登记区间"：跳过钩子自身有 `v->GetEffectiveOrders() != own ⇒ return 0` 的前置门，借用期根本不会跳过；解挂后记录条目位于自己表尾部、且在签名校验下可证为自动项，跳过它们符合 KI-400「自动填充命令永不执行」的口径。若改成不登记，解挂后这些记录会被当真正命令执行，反而退回 KI-400。
- 不动 C-2 白名单（`:6819 R3RAutoOrderOnControlLeg()`）：它校验的是**控制段正在驱动的那段计划**（借用表 33 条），语义正确，与写入表不同属预期（原疑点 D-2 澄清，非缺陷）。
- 不动修法 1（暂存后统一落地）：改动面大，且修法 2 已消除"插到命令中间"这一根因。

### 8.3 构建自证（2026-10-26 22:17）

- 复用既有 `_tmp_inc_build.cmd`（**未新建任何 .cmd**）。
- 护栏 `build\R3R_incbuild.guard.log`：因上一轮遗留的 `src\r3r_resv_probes.h`（21:08:56）比最新 obj（21:08:11）新，判 `REMOVED 620 object file(s) - upgrading this build to a FULL rebuild` ⇒ 本轮走**全量 692 步**（与本轮纯 `.cpp` 改动无关，本轮未碰任何 `.h`/`lang`）。
- 日志：`[692/692] Linking CXX executable openttd.exe`（`build\R3R_incbuild.log:734`）；`build\R3R_incbuild.done` = `EXIT_CODE=0`；`error C*` / `fatal error` / `FAILED:` / `build stopped` 计数均 0。
- 时间戳：`src\train_cmd.cpp` 21:42:08 → `train_cmd.cpp.obj` 22:09:40 → `build\openttd.exe` 22:17:36。
- `read_lints`：`src\train_cmd.cpp` 0 条。
- 本轮未新增日志字面量（沿用 `AUTO-ORD` / `AUTO-ORD-DROP` / `AUTO-ORD-STALE`），故不做 exe 串核对。

### 8.4 复测时看什么

1. `AUTO-ORD` 行在借用期应出现 `iv=[N,N+1)` 且 `N == n`（自己表长度），即追加在尾部；不应再出现"`anchor` 远小于 `n`"的中间插入。
2. 解挂提交点 `ORD-IDX-WATCH site=settle-in/out`：`real` 不应被推到某个中间区间；`AUTO-ORD-SKIP` 只允许落在 `iv` 起点 ≥ 玩家命令条数的尾部区间。
3. 玩家手写表条数/顺序在记录前后不变（除尾部新增记录）。
4. 负判据：无自动记录的普通列车零 `AUTO-ORD-SKIP`；签名不匹配时应看到 `AUTO-ORD-STALE` 而不是 `AUTO-ORD-SKIP`。
