# R3R 克隆多段链后「隐藏段排程未继承」诊断与修复（第 227 轮）

- 日期：2026-10-10
- 玩家原话：「我目前发现了另外一个问题。那就是复制多段式列车之后列车的某些段的调度继承有问题」
- 现场日志：`build\R3R_debug.log`（613 行 / 57 322 B，LastWriteTime 2026-10-10 08:05:17）
- 所用 exe：`build\openttd.exe` 2026-10-10 07:58:47（51 680 768 B；含第 225 轮 KI-332 段特质照搬 + 第 226 轮 KI-333 隐藏段名字去重）
- 本轮状态：**已修且已复测通过** —— 根因已做实（日志实锤）+ 修法已落地（`R3RCopyClonedSegmentTraits()` 补段排程深拷贝）+ 增量构建通过（exe 2026-10-10 08:26:10，探针 `CLONE-ORD` 已入 exe）+ **2026-10-10 08:29 实机复测通过**（判据 ①②③⑥ 通过，④⑤⑦ 无样本；见第七节）
- 关联条目：本条新建 **KI-337**；同族既有 KI-332（克隆段特质照搬）、KI-333（克隆隐藏段名字去重）
- **编号勘误**：首次登记时误用 `KI-334` —— 该号已被「第 223 轮 第 3 条：双头机车挂接时机车错位」占用；本条正名为 **`KI-337`**，本文件内凡出现 `KI-334` 处，若指"克隆隐藏段排程"一律按 `KI-337` 读。

---

## 一、现场脚本（日志逐行复原）

### 1.1 克隆前：两条独立车底被拖进同一车库行、耦合为一个两段链

| 行号 | 原文（节选） | 含义 |
| --- | --- | --- |
| 151 | `R3RDUMP-CHAIN head=23 … n=8 … prio=1 … ordCount=7 … coType=16 realType=16 tile=964,133 isDepot=1` | 源 A：链头 23，8 节，**7 条排程** |
| 136 | `R3RDUMP-CHAIN head=22 … n=8 … prio=1 … ordCount=14 … coType=17 realType=17 tile=964,133 isDepot=1` | 源 B：链头 22，8 节，**14 条排程** |
| 161/165 | `ARRANGE-IN dh=23 dst=25 sh=22 src=22 mc=1` | 把 22 链挂到 23 链尾（dst=25 为 23 链尾车），合并成 head=23 / n=16 |
| 169 | `DEPOT-PARK veh=22 bk_real=0 bk_impl=0 unit=5` | **22 的 14 条排程被停放进 `22->orders_backup`**，索引停放 0/0 |
| 171 | `GRP-PARK veh=22 g=1` | 22 的分组同时停放 |
| 175 | `CGRP-NORM-UNION head=23 nseg=2 mask=2` | 合并链 = 2 段 |
| 176-179 | `SEGID-SYNC tag=depot-edit head=23 nseg=2` / `INVAR-CTRL … head=23 nseg=2 ctrl=23 ctrl_pri=1 head_pri=1 borrowed=0` | 段 9（=23，控制段=链头）、段 10（=22 隐藏段） |
| 197-199 | `SEGTR-SNAP tag=depot-edit head=23` `i=1 id=9 front=23 … live u=6 n="关西机场快速01"` / `i=2 id=10 front=22 … live u=0 n="" gid=1 | bk u=5 n="纪州路快速01" gbk=1` | 段 10 的三特质已停放在 22 的 backup（u=5 / "纪州路快速01"），**但段行 orders 未列出** |

### 1.2 克隆（日志 203 / 491-565）

| 行号 | 原文（节选） | 含义 |
| --- | --- | --- |
| 491 | `CLONE-MARKERS src=23 dst=61 cars=16 srcSF=2 dstSF=2 srcSB=2 dstSB=2 diff=0` | 克隆品链头 61，16 节，段标记镜像成功 |
| 492 | `SEGID-SYNC tag=clone head=61 nseg=2` | 克隆品也是 2 段 |
| 511-513 | `SEGTR-SNAP tag=clone head=61 nseg=2` `i=1 id=11 front=61` / `i=2 id=13 front=69` | 段 11（=61）/ 段 13（=69 隐藏段） |
| **538** | `CLONE-TRAIT seg=0 src=23 dst=61 prio=1->1 grp=1->1 cgrp=0x2->0x2 name="关西机场快速01"->""` | 段 11 特质照搬 OK（名字留给 CloneVehicleName 重命名，故 dst 为空是当时正常） |
| **539** | `CLONE-TRAIT seg=1 src=22 dst=69 prio=2->2 grp=1->1 cgrp=0x2->0x2 name="纪州路快速01"->"纪州路快速01"` | 段 13 特质照搬 OK |
| **540** | `ORD-IDX-WATCH site=settle-in tag=clone veh=61 … n=-1->7 … ord=0->0x29E9EB2C8F0` | **只有链头 61 拿到 7 条排程**（=`CmdCloneOrder` 复制源链头 23 的活排程） |
| **（全段无 veh=69 的 ORD-IDX-WATCH）** | — | **段 13 的排程从未被同步**（`R3RSyncHiddenSegmentTraits` 刻意不碰隐藏段 orders） |
| 563-564 | `SEGTR-SNAP tag=clone` `i=1 … live u=7 n="关西机场快速02"` / `i=2 … bk u=0 n="纪州路快速02" gbk=1` | 名字去重（KI-333）生效；**段 13 的 `bk u=0`（对比源 22 的 `bk u=5`）** |

### 1.3 玩家把两段各拆开一条（depot 拖动，日志 566-613）

| 行号 | 原文（节选） | 含义 |
| --- | --- | --- |
| **582** | `ORD-IDX-WATCH site=depot-edit-in veh=69 real=-1->0 … n=-1->-1 artic=-1->0 ord=0000000000000000->0000000000000000 front=1` | **克隆品隐藏段 69 的排程 = nullptr，n=-1（没有排程）** |
| 586/589 | `site=settle-in/out … veh=69 … n=-1 … ord=0` | 拆分后仍无排程 |
| 587-588 | `SEGTR-SNAP tag=depot-edit head=69 nseg=1 ctrl=69` `live u=8 n="纪州路快速02"` | 69 成为独立单段链（名字/号都对，**独缺排程**） |
| **606** | `ORD-IDX-WATCH site=syncdriving-out-owner veh=22 … n=-1->14 artic=-1->0 ord=0000000000000000->0x29E9E9CD430` | **源隐藏段 22 拆出后恢复了 14 条排程**（从 `22->orders_backup`） |
| 609 | `ORD-IDX-WATCH site=settle-in tag=depot-edit veh=22 … n=14->14 ord=0x29E9E9CD430->0x29E9E9CD430` | 源 22 确凿持有 14 条 |
| 602-603 | `CGRP-NORM-UNION head=23 nseg=1` / `SEGTR-SNAP tag=depot-edit head=23 nseg=1 ctrl=23` | 源 A 侧拆分后单段 |

### 1.4 对照结论（一句话）

同一操作、同一时刻，**源车 22 拆出后 n=14，克隆品 69 拆出后 n=-1**。差别不在运动/寻路，而在克隆那一刻排程没被搬过来。

---

## 二、根因

克隆路径只处理了**链头**的排程（原生命令 `CmdCloneOrder` → CO_COPY/CO_SHARE，见 `src\order_cmd.cpp:3277-3373`，作用对象是 `w_front`），而 R3R 的**隐藏段自己那份排程**（停放在段头车的 `orders_backup` + `orders_backup_real_index` / `orders_backup_implicit_index` / `r3r_orders_borrowed`）**没有被搬给克隆品**。

落点：`src\couple_group.cpp` 的 `R3RCopyClonedSegmentTraits(const Train *src_chain, Train *dst_chain)`（第 225 轮 KI-332 新增）。该函数在其注释里已自陈设计边界：

> 「Only the three traits are collected. The schedule and the order indices of a hidden segment are deliberately left alone.」

实际复制清单（逐车循环）只有：
- `name_backup` / `group_id_backup`（停放副本）
- 隐藏段活字段 `name`（链头不搬）、活字段 `group_id`
- 段头额外：`couple_groups`、`r3r_priority`、段行的 `name` / `group_id`

**没有** `orders_backup`、`orders_backup_real_index`、`orders_backup_implicit_index`、`r3r_orders_borrowed`。并且 `R3RSyncHiddenSegmentTraits()`（`src\train_cmd.cpp:5235`）同样刻意不写隐藏段的 orders（与上一处同一约定），所以克隆品隐藏段的**段行 orders 也是空**，没有任何兜底能把它补回来。

第 225 轮做 KI-332 时的口径「唯一例外 unitnumber/unitnumber_backup 不继承，其它全继承」中的"其它"当时理解成三特质，**漏掉了排程这一大类**；本条即该遗漏的表现。

### 2.1 为什么链头段没暴露

源链头 23 恰是控制段（`INVAR-CTRL … ctrl=23 head_pri=1 borrowed=0`），其活 `orders` 就是本段排程，因此 `CmdCloneOrder` 一次复制即覆盖；只有**隐藏段**（22→69）走的是 `orders_backup` 通道，而该通道在克隆里完全没接。

### 2.2 为什么"拆开"才看得见

克隆品 69 在链内时是隐藏段，引擎热路径不读它的排程（`R3RSyncHiddenSegmentTraits` 也不写），所以**在链内看不出异常**；一旦被拖出成独立链头，原生理应走 `R3RSyncChainAfterDepotEdit`/`R3RSyncDrivingOrders` 从 `orders_backup` 取回——但 `orders_backup` 是空的，于是得 0 条。

---

## 三、修法（已落地）

在 `R3RCopyClonedSegmentTraits` 的逐车循环里补齐"段自身排程"的**深拷贝**（不能共享指针，克隆品与源必须各自持有）：

```cpp
/* 与 src\order_cmd.cpp:3322-3343 (CO_COPY) 同款深拷贝，避免共享 OrderList。 */
const OrderList *src_own = nullptr;
uint src_real = INVALID_VEH_ORDER_ID, src_impl = INVALID_VEH_ORDER_ID;
if (s->orders_backup != nullptr) {                 /* 停放态（depot 拖动 / Couple 借用） */
    src_own  = s->orders_backup;
    src_real = s->orders_backup_real_index;
    src_impl = s->orders_backup_implicit_index;
} else if (d != dst_chain && s->orders != nullptr) { /* 隐藏段自持（未停放） */
    src_own  = s->orders;
    src_real = s->cur_real_order_index;
    src_impl = s->cur_implicit_order_index;
}
if (src_own != nullptr) {
    std::vector<Order> dst_orders;
    for (const Order *order : src_own->GetOrders()) {
        dst_orders.emplace_back(*order);
        TraceRestrictRemoveNonOwnedReferencesFromOrder(&dst_orders.back(), d->owner);
    }
    d->orders_backup = OrderList::Create(std::move(dst_orders), d);
    d->orders_backup->GetScheduledDispatchScheduleSet() = src_own->GetScheduledDispatchScheduleSet();
    for (DispatchSchedule &ds : d->orders_backup->GetScheduledDispatchScheduleSet()) ds.ResetStateAfterClone();
    d->orders_backup_real_index = src_real;
    d->orders_backup_implicit_index = src_impl;
    d->r3r_orders_borrowed = s->r3r_orders_borrowed;
}
```

要点与风险：
1. **链头段**（`d == dst_chain`）的活 `orders` 由 `CmdCloneOrder` 负责，此处只补它的 `orders_backup`（源链头借用态时它自己那份排程也停在那里），不得重复写活字段。
2. **深拷贝**必须走 `OrderList::Create`，禁止 `d->orders_backup = s->orders_backup`（会把两个独立列车绑到同一张表，后续任一方删除即悬垂，等同 KI-93 那类教训）。
3. `orders_backup` 在驻放时已排除共享链（depot 拖动 `DEPOT-PARK` 有 `!shared_orders` 守卫），故深拷贝无共享语义冲突。
4. 需同步到**段行**：若采纳"段行 orders 为正式宿主"的长期方向（第 152 轮备忘），此处也应在 `R3RSettleClonedChain` 之后的 settle 里把隐藏段行的 orders 指到 `d->orders_backup`；当前口径下至少保证活字段/停放副本正确。
5. `unitnumber_backup` **仍刻意不继承**（KI-332 口径不变，克隆品应领自己的号）。

---

## 四、未确认项（U）

- **U-1**：源链头处于**借用态**（`borrowed=1`）时，克隆品链头段的 `orders_backup`（= 链头自己那份排程）是否也会丢——按现实现推论**会丢**，但本轮现场源链头非借用态，无直接日志。
- **U-2**：`share_orders == true`（走 CO_SHARE）时，隐藏段是否应"共享"源段排程而非深拷贝；本轮现场是 CO_COPY 路径。
- **U-3**：`R3RSegmentRecord::orders`（段行）是否也该由克隆写入。现口径"行只存指针、不拥有排程"，`R3RSyncHiddenSegmentTraits` 刻意不写；若将来切到"排程跟段行走"，本修法要一并调整。
- **U-4**：顺序索引 `cur_real/implicit/timetable_order_index` 与 `artic` 计数是否也都需要按段镜像（日志 `ORD-IDX-WATCH` 只覆盖链头，隐藏段索引无对照数据）。
- **U-5**：多段链**超过 2 段**时是否每段都丢（按根因推论是，但现场只验证了第 2 段）。

---

## 五、复测判据（修法落地后）

1. 同场景复跑：`CLONE-TRAIT` / 段级 `ORD-IDX-WATCH` 应覆盖**每一段**（不只 veh=61），且克隆品隐藏段 `orders_backup` 非空（`ord != 0`、`n == 源段条数`）。
2. 克隆后把隐藏段拖出成独立链 ⇒ 该段排程**条数与源段一致**（本现场应为 14），不再出现 `n=-1 / ord=0`。
3. 负判据：源链排程**不受影响**（源 22 仍 14 条、源 23 仍 7 条；`DEPOT-PARK` 状态不变）。
4. 负判据：克隆品与源**不共享** OrderList（`FirstShared() !=` 源；删掉克隆品后源排程仍可用）。
5. 多段（≥3 段）链克隆后逐段拖出，每段排程均正确。
6. 克隆品链头号/名仍为新分配的号（`unitnumber_backup` 不继承的口径不回归），KI-332/KI-333 判据不回退。
7. 无排程的普通链克隆行为逐字不变（不新增空 OrderList）。

---

## 六、落地与构建自证（第 227 轮）

### 6.1 代码落点（仅 `src\couple_group.cpp`，未碰任何 `src\*.h`）

`R3RCopyClonedSegmentTraits()` 的段头分支（`:1265` 起 `if (d == dst_chain || d->IsSegmentFront())`）在写完 `couple_groups` / `r3r_priority` 之后、写段行之前，插入段排程深拷贝块（`:1270-1327`）：

| 步骤 | 代码 | 作用 |
| --- | --- | --- |
| 选源 | 优先 `s->orders_backup`（停放态，取 `orders_backup_real_index`/`orders_backup_implicit_index`）；否则**非链头**且 `s->orders != nullptr` 且 `!s->r3r_orders_borrowed` 时取活字段（`cur_real_order_index`/`cur_implicit_order_index`） | 链头活排程仍归 `CmdCloneOrder`，此处只补"段自己那份" |
| 深拷贝 | `std::vector<Order>` 逐条 `emplace_back(*order)` + `TraceRestrictRemoveNonOwnedReferencesFromOrder(&orders.back(), d->owner)` → `OrderList::Create(std::move(orders), d)` | 克隆品**自持**新表，杜绝共享指针（KI-93 悬垂） |
| 调度计划 | `GetScheduledDispatchScheduleSet() = src_own->...` 后逐项 `ResetStateAfterClone()` | 与 CO_COPY 同款 |
| 索引/借用位 | `d->orders_backup_real_index/src_impl` 照抄源；`d->r3r_orders_borrowed = true` | 与"段头重新成链头时 `R3RReturnBorrowedOrders()` 的期望状态"一致 |
| 断言 | `assert(d->orders_backup == nullptr)` + `assert(OrderList::CanAllocateItem())` | 新克隆车不该带停放排程；池子可分配 |
| 守卫 | `if (src_own != nullptr && src_own->GetNumOrders() > 0)` | 无排程普通链不新增空表（判据 7） |
| 探针 | `CLONE-ORD dst=… src=… n=… real=… impl=… borrowed=… bk=…`（`R3RDbgWrite`） | 补 `ORD-IDX-WATCH` 只看活 orders 的盲区，使**每个段头**都留一行可复核 |

> 相对第三节草案的差异：加了 `!s->r3r_orders_borrowed` 守卫（防把链头借来的活 orders 当"隐藏段自持"复制）；加了 `GetNumOrders() > 0` 守卫；`orders_backup_real_index` 的正名（草案误写 `orders_backup_index` / `orders_backup_index_i`，实际字段见 `vehicle_base.h`）。

### 6.2 构建自证（复用既有 `_tmp_inc_build.cmd`，未新建任何 `.cmd`）

- `build\R3R_incbuild.guard.log` = `GUARD: incremental is safe (no header/lang file is newer than the newest object)`
- `build\R3R_incbuild.log`：`[3/3] Linking CXX executable openttd.exe`；无 `error C####` / `fatal error` / `FAILED:` / `build stopped`
- `build\R3R_incbuild.done` = **`EXIT_CODE=0`**
- 时间戳链：`src\couple_group.cpp` **08:25:52** → `couple_group.cpp.obj` **08:26:08** → `build\openttd.exe` **2026-10-10 08:26:10（51 680 768 B）**（exe 晚于全部源码）
- `read_lints`（`src\couple_group.cpp`）= **0 条**
- 产物自证：`findstr "CLONE-ORD dst=" build\openttd.exe` 命中（探针字符串已入二进制）

### 6.3 复测指引（用本 exe 跑第 1 节同场景）

1. 克隆那列两段链 → 日志应出现**两条** `CLONE-ORD`：链头段 `n=7`、隐藏段 `n=14`（本现场源 23=7 / 源 22=14），且 `bk` 非 0、`borrowed=1`。
2. 把克隆品隐藏段拖出成独立链 → 该段排程条数 = 14（对比第 1.3 节旧日志的 `veh=69 … n=-1 / ord=0`，现在应为 `n=14 / ord=0x…`）。
3. 负判据：源侧 `703? DEPOT-PARK` 不变、源 22 仍 14 / 源 23 仍 7。
4. 负判据：克隆品与源不共享表（`FirstShared()` 两者不同；删掉克隆品后源排程仍可用）。
5. 多段（≥3 段）链克隆后逐段拖出，每段 `n` 与源对应段一致。
6. `unitnumber_backup` 仍不继承（KI-332 口径不回归），KI-332/KI-333 判据不回退。
7. 无排程普通链克隆逐字不变（无 `CLONE-ORD`、无空 OrderList）。

### 6.4 仍未做 / 未确认

- ~~游戏内复测（上述 7 条判据）未做~~ ⇒ **已于 2026-10-10 08:29 复测，见第七节**。
- 第四节 U-1~U-5 仍未证实（链头借用态克隆、CO_SHARE、段行 orders 宿主、索引按段镜像、≥3 段）。
- 段行（`R3RSegmentRecord::orders`）**仍刻意不写**（`R3RSyncHiddenSegmentTraits` 口径不变）——当前以"停放副本"为段排程宿主，与第 152 轮"段行作正式宿主"的长期方向尚未打通。

---

## 七、实机复测（2026-10-10 08:29:39）

- 复测日志：`D:\temp\codebuddy-dropped-files\f97696fa-24a5-46d5-91ae-cc10e7f1565d\R3R_debug.log`（613 行 / 57 439 B / `LastWriteTime` 08:29:39）
- 所用 exe：`build\openttd.exe` **2026-10-10 08:26:10（51 680 768 B）** = 本轮修好的那个（exe 早于日志 3 分钟，产物对得上）
- 与失败轮（08:05:17 / 57 322 B）的字节差 = **+117 B、行数相同（613）** ⇒ 玩家跑的是**同一套操作**，差异只由新探针与恢复的排程产生。

### 7.1 场景（与失败轮同型，链头/隐藏段角色互换）

- 源链：head=**22** `纪州路快速01`（控制段，活排程 **14** 条）、隐藏段 head=**23** `关西机场快速01`（停放排程 **7** 条）。
- 克隆品：head=**61**（segid 11 = 61-68）、隐藏段 head=**69**（segid 13 = 69-76），共 16 节。

### 7.2 判据逐条（行号为复测日志行号）

| 判据 | 结果 | 证据 |
| --- | --- | --- |
| ① 隐藏段各留一行 `CLONE-ORD`、`bk≠0`、`borrowed=1` | **通过** | `513 CLONE-ORD dst=69 src=23 n=7 real=0 impl=0 borrowed=1 bk=0000019871302EF0` |
| ② 拖出隐藏段后条数 = 源 | **通过** | `570 ARRANGE-IN sh=61 src=69 mc=1` → `584 ORD-IDX-WATCH site=syncdriving-out-owner veh=69 … n=-1->7 ord=0->…2EF0`（失败轮为 `n=-1->-1 ord=0`） |
| ③ 负判据：源链排程不受影响 | **通过** | `606 … veh=23 … n=-1->7`（自身 7 条仍在）；`603 SEGTR-SNAP tag=depot-edit head=22 … live u=5 n="纪州路快速01"` |
| ④ 负判据：克隆品与源不共享 OrderList | **未覆盖** | 日志无 `FirstShared()` 样本；`bk` 与拆出后的 `ord` 末 12 位十六进制相同只说明**同分配粒度**，不构成"共享"证据 |
| ⑤ ≥3 段链逐段拖出 | **未覆盖** | 源链只有 2 段 |
| ⑥ 克隆链头号/名仍新分配、KI-332/KI-333 不回退 | **通过** | 克隆品 `纪州路快速02`／`关西机场快速02`（`580/588`） vs 源 `纪州路快速01`／`关西机场快速01`（`603/611`） |
| ⑦ 无排程普通链克隆逐字不变 | **未覆盖** | 本场景源链两段都有排程 |

### 7.3 两个判读要点（防将来误读）

1. **链头段不出现 `CLONE-ORD` 是设计内**，不是漏拷贝：链头（`d == dst_chain`）的活排程由原生 `CmdCloneOrder` 负责（`540 veh=61 n=-1->14`），本修复只补"隐藏段自己那份"。
2. **`CLONE-ORD` 只在 `src_own->GetNumOrders() > 0` 时打**：源链头若处在借用态（`s->r3r_orders_borrowed` 为真且无 `orders_backup`），链头段也不会留行 —— 这属 U-1 场景，判据需另设。
3. 全日志 **0 条** `INVARIANT-BROKEN` / `MISMATCH` / `assert` / `error` / `crash` / `REFUSE` / `REJECT`。

### 7.4 复测结论

**通过** —— 玩家原报告的「复制多段式列车之后列车的某些段的调度继承有问题」已消除；判据 ④⑤⑦ 因场景不含对应样本而未覆盖，属"未验"而非"失败"。
