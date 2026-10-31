# R3R 借用态入口统一（第 352 轮，2026-10-28）

对应 KI-525。玩家要求：「顺手把 7 个入口都改好。这个需要落盘」。

> 注：早期占位名曾写成 `R3R_borrow_entry_round303_memo.md` / KI-480，两者在本工作区都已被占用
> （第 303 轮 = 账本删除；KI-480 = 另一条已关闭的条目），故本文件与 KI 条目改用第 352 轮 / KI-525。

---

## 一、问题背景（第 351 轮遗留）

`R3RSegmentReturnOrders()` / `R3RReturnBorrowedOrders()` 在归还时读
`orders_backup_real_index` / `orders_backup_implicit_index`，缺失（`INVALID_VEH_ORDER_ID`）
就落 0：

```8135:8140:src/train_cmd.cpp
		VehicleOrderID own_real = chain->orders_backup_real_index;
		VehicleOrderID own_impl = chain->orders_backup_implicit_index;
		if (num == 0 || own_real == INVALID_VEH_ORDER_ID || own_real >= num) own_real = 0;
		if (num == 0 || own_impl == INVALID_VEH_ORDER_ID || own_impl >= num) own_impl = 0;
```

现场表现：解挂/车库编辑后，某辆车的命令索引「变回命令 1」。
日志 `ORD-RETURN-OWN ... real=2->0`（前一版夹紧方案会打出 `real=2->2`，是掩盖而非修复）。

根因不是「取回时没夹紧」，而是**存侧和取侧的规则不一致**：
7 个进入借用态的入口里只有 `R3RSyncDrivingOrders()` 的切表分支会停位置，
其余入口把 `r3r_orders_borrowed` 置真却不停位置 ⇒ 归还时必然读到 `INVALID`。

---

## 二、7 个入口清单（第 351 轮取证，行号为**改前**）

| # | 行号 | 函数 / 场景 | 改前是否停位置 | 语义分类 |
|---|---|---|---|---|
| 1 | `:8006` + `:8022` | `R3RSyncDrivingOrders` 切表分支（真耦合） | ✅ 唯一停位点 | A 首次借用 |
| 2 | `:8317` | 同函数 else 分支（链头无自有表，跟随 donor） | ❌ | B 继承跟随 |
| 3 | `:7722` | `R3RRebuildCouplePriorities`（读档重建） | ❌ **且主动清空索引** | C 读档 |
| 4 | `:4387` | 车库拖动：新车头接跟随指针 | ❌ | B 继承跟随 |
| 5 | `:4641` | 卖链头：`new_head` 接跟随指针 | ❌ | B 继承跟随 |
| 6 | `:2275` | 拆节换手（MoveChunk），`u` 接跟随 | ❌ 且 `:2296-2297` 强行清 0 | A 首次借用 |
| 7 | `:6969` | AUTO-ORD 懒建：段新建自己的表 | ❌ 故意 `INVALID` | D 新建表 |

四个类的处理口径不同，**不能一律停位置**：

- **A 首次借用**：自己的表仍在 `orders` 里，活索引即将被换成借来那张表的坐标 ⇒
  **必须 park**（这是「自己表上的位置」唯一可查之处）。
- **B 继承跟随**：自己本来就在跟随（或自己的表为空），`cur_real/cur_impl` **此刻已是
  follow 表的坐标**，park 到自己的表上是错的 ⇒ **绝不能 park**；但必须保证
  `orders_backup_*_index` 保持 `INVALID`，且将来归还时 `num == 0` 的落 0 是正确的。
- **C 读档**：索引来自存档，不能用活字段反推 ⇒ **只修判据、不动索引**。
- **D 新建表**：表刚建、从未跑过 ⇒ 位置是 0，**应写 0 而不是 `INVALID`**
  （`INVALID` 在归还侧的含义是「丢失」，不是「第 0 条」）。

---

## 三、真凶：入口 #3 把「原生共享排程」误判成「借用」

```7709:7725:src/train_cmd.cpp   （改前）
	Train *owner = chain;
	if (chain->orders != nullptr) {
		for (Train *s : segs) {
			if (s != chain && s->orders == chain->orders) { owner = s; break; }
		}
	}
	...
	chain->r3r_orders_borrowed = (owner != chain);
	chain->orders_backup = nullptr;
	chain->orders_backup_real_index = INVALID_VEH_ORDER_ID;
	chain->orders_backup_implicit_index = INVALID_VEH_ORDER_ID;
```

两个错误叠加：

1. 判据 `s->orders == chain->orders` 检测的是**共表**（原生 `Ctrl` 共享排程 /
   段间共享），**不是借用**。真借用的唯一权威是跟随指针 `r3r_follow_orders`，
   而该分支（改前 `:7685`）早已 `return`，走不到这里。
2. 在「共表 = 借用」的误判下把备份索引无条件清零。

事实链：读档把共表链标成 `borrowed=true` + 索引 `INVALID` → 后续任何提交点
（解挂 / 车库编辑 / 占位）走归还 → 读到 `INVALID` → **落 0**。
这与现场 54/57 号车吻合：全日志仅 `ORD-BORROW-PARK veh=0` 一条，
说明它们从未走过入口 #1（唯一停位点）。

---

## 四、修法（本轮已落地）

### 4.1 新增统一停位原语

```cpp
static bool R3RParkOwnOrderProgress(Train *chain);
```

语义：把「自己那张表上的位置」停进 `orders_backup_*_index`，四道守卫（任一命中即
`return false`，不覆盖）：

1. `chain == nullptr`；
2. `orders_backup != nullptr`（旧机制已停表 + 停位，见入口 #4 下方 else 分支）；
3. `r3r_follow_orders != nullptr`（已是跟随者，`cur_*` 是借来那张表的坐标，写进自己的备份是污染）；
4. `orders == nullptr`（自己没有表，就没有「自己表上的位置」；保留 `INVALID`，归还时 `num == 0` 落 0 才正确）；
5. `orders_backup_real_index != INVALID_VEH_ORDER_ID`（已停过，保留借用开始那一刻的值）。

返回 `bool` 供调用点决定是否打探针。

### 4.2 逐入口改法（实际落地）

| # | 落地改法 | 探针 tag |
|---|---|---|
| 1 | 内联块**保留**（该处还要按 `R3RSegmentOwnOrderList()` 打条数，改调原语会把已有语义拆散）；行为逐字不变 | 无（原有 `num=`） |
| 2 | 调 `R3RParkOwnOrderProgress(chain)`。本分支前提是链头无可跑表 ⇒ 必然 no-op；显式调用是为让「每个入口都先停位置」可审计 | `donor` |
| 4 | 同 #2，前提 `src_head->orders == nullptr && !src_head->IsR3RFollower()` ⇒ 必然 no-op | `depot-drag` |
| 5 | 同 #2，但**可能真写入**：`new_head` 可能自带一张从未跑过的表 | `sell-head` |
| 6 | 因定义顺序无法调原语（本函数在前置声明之前），**内联同款守卫**；park 必须在 `DeleteVehicleOrders(u)` **之前**；随后 `u` 接过 `v` 的进度（原为硬清 0） | `movechunk` |
| 3 | `borrowed = false`（无 backup、无 follow ⇒ 必然不是借用）；`owner` 仍用于优先级排序；索引照旧清零 | — |
| 7 | 备份索引写 **0** 而不是 `INVALID`（新表从未跑过 = 第 0 条） | — |

入口 #6 的索引接手：

```2311:2321:src/train_cmd.cpp
			/* 硬清 0 会让接过跟随关系的新链头从表头重跑，表现就是「命令跳回第一条」。 */
			if (const_cast<Train *>(u)->r3r_orders_borrowed) {
				u->cur_real_order_index = v->cur_real_order_index;
				u->cur_implicit_order_index = v->cur_implicit_order_index;
			} else {
				u->cur_real_order_index = 0;
				u->cur_implicit_order_index = 0;
			}
```

### 4.3 归还侧（同时落地）

`INVALID` 的静默落 0 **保留**（#7 之后已无合法来源），但新增告警：

```cpp
if (num > 0 && (own_real == INVALID_VEH_ORDER_ID || own_impl == INVALID_VEH_ORDER_ID)) {
    R3RDbgWrite("ORD-RETURN-NOBK veh=%d num=%u real=%d impl=%d orphan=%d\n", ...);
}
```

`num > 0` 时读到 `INVALID` 属于异常，必须当场点名，不再静默变成 0。

---

## 五、为什么要「分类」而不是「7 处都停位置」

如果机械地在 #2/#4/#5 也写入，写进去的是**借来那张表的坐标**（或 0），
下次归还时会用这个值去索引**自己的**表 ⇒ 越界或错位，属于「用一个错盖另一个错」。
这正是上一版夹紧方案被否决的同一个毛病。

---

## 六、复测判据

1. 解挂后不再出现 `ORD-RETURN-OWN ... real=2->0` 这类「索引跳回 1」；
2. 真耦合（入口 #1）仍打出 `ORD-BORROW-PARK ... num=N`（**无 tag**），位置与改前一致；
3. 拆节换手（#6）打出 `tag=movechunk`，且 `ORD-RETURN-OWN` 取回的值 == 换手前的位置；
4. 读档后共表链**不再**出现 `ORD-RETURN-OWN`（borrowed=false，直接 return）；
5. 负判据：原生 `Ctrl` 共享排程的列车读档前后行为逐字不变；
6. 负判据：车库拖动/卖链头后仍能正常跑，无 `ORDERLIST` 相关断言；
7. `ORD-RETURN-NOBK` 应长期为 0（出现即说明还有漏存入口）；
8. `tag=depot-drag` / `tag=donor` 正常场景应为 0 条（no-op 分支），出现即说明判据被放宽了。

---

## 七、待验证 / 未确认

- U-1：现场 54/57 是否确为原生共享排程链（需日志 `ORD-AFTER-COUPLE own_shared=` 佐证）；
- U-2：入口 #3 改判 `false` 后，旧档中「修法 D 之前」的借用形态是否会被漏判
  （该形态 `orders_backup != nullptr`，走上方保态分支，理论不受影响）；
- U-3：`R3RSegmentReturnOrders` 与 `R3RReturnBorrowedOrders` 是否还有第三个归还入口；
- U-4：`tag=sell-head` 命中时写入的位置是否确为 `new_head` 自己那张表的位置（需现场一条日志）。

---

## 八、构建自证

- 护栏：`GUARD: incremental is safe (no header/lang file is newer than the newest object)`
  ⇒ 本轮只改 `src\train_cmd.cpp`，增量合法。
- 判决：`build\R3R_incbuild.done` = `EXIT_CODE=0`；
  日志尾部 `==== R3R build ended 2026/10/28 22:24:44  MODE=inc  EXIT_CODE=0`，无 `error C` / `FAILED` / `build stopped`。
- 时间戳链：`src\train_cmd.cpp` 21:56:20 → `build\openttd.exe` 22:02:33（52 344 832 B）⇒ 产物晚于源码。
  本轮那次 `inc` 运行 ninja 判「无活可干」（日志里只有 always-run 的 FindVersion 边，
  这是既有已知现象，见 KI-15），即 22:02:33 那次构建已经包含本轮全部改动。
- 产物自证（exe 内 ASCII 字面量，6/6 命中）：
  `ORD-RETURN-NOBK` / `ORD-BORROW-PARK` / `tag=movechunk` / `tag=depot-drag` / `tag=sell-head` / `tag=donor`。
- `read_lints`：`src\train_cmd.cpp` 0 条。

> 判断口径提醒：不能只看「done 文件存在」。本轮第一次尝试时 `done` 是旧文件（14:11），
> 差点被误判成已构建 —— 必须核对 `done` / `log` / `exe` 三个 mtime 是否属于本轮。

---

## 九、附：构建入口变更（2026-10-28，用户要求）

玩家把 `_tmp_inc_build.cmd` 当成临时垃圾删掉了（名字里有 `tmp`）。为避免再次发生，
本轮把**既有且稳定的** `R3R_fullbuild.cmd` 改造成全量/增量双模式，并删除名字同样带
`tmp` 的 `R3R_inc_build_tmp.cmd`：

| 调用 | 行为 | 日志 / 判决 |
|---|---|---|
| `R3R_fullbuild.cmd` | 全量（删全部 obj） | `build\R3R_fullbuild.log` / `.done` |
| `R3R_fullbuild.cmd detach` | 全量，独立最小化窗口 | 同上 |
| `R3R_fullbuild.cmd inc` | 增量（先跑护栏） | `build\R3R_incbuild.log` / `.done` / `.guard.log` |
| `R3R_fullbuild.cmd inc detach` | 增量，独立最小化窗口 | 同上 |

`inc` 模式先执行 `R3R_inc_guard.ps1`：一旦 `src\*.h` 或 `lang\*.txt` 新于最新 obj，
护栏自己删光 obj，本次自然退化为全量 ⇒ **`inc` 永远安全**，改 `.cpp` 时只是省时间。
脚本保留原有全部闸门（`openttd.exe` 在跑 97 / 缺 `build.ninja` 98 / 发布树 95 / vcvars 失败 96）。
长期记忆已同步更新（构建入口条目）。

---

## 十、复测失败取证（2026-10-28，第 353 轮）

### 10.0 采样有效性

日志 `build\R3R_debug.log` 里出现 `ORD-RETURN-NOBK`（只有第 352 轮新 exe 才打这个串），
⇒ **这份日志确实出自含 KI-525 修复的 exe**，采样有效，下面的"未解决"不是拿旧 exe 下的结论。

### 10.1 玩家报告

「排程进度丢失和填充位置错误仍然没有解决」——两条都复现，且都能落到具体行。

### 10.2 进度丢失：位置停到了**错的车上**

现场一（第一次耦合→解挂）：

| 行 | 探针 | 读数 |
|---|---|---|
| 252 | `ORD-BORROW-PARK` | `veh=0 bkr=0 bki=0 num=6` |
| 487 | `ORD-AFTER-COUPLE` | `head=54 n=33 real=1 borrowed=1 owner=6 own_shared=0 own_first=54 u_follow=1` |
| 1152 | `ORD-RETURN-NOBK` | `veh=54 num=4 real=65535 impl=65535 orphan=0` |
| 1153 | `ORD-RETURN-OWN` | `veh=54 num=4 real=2->0 impl=2->0 bk=65535/65535` |
| 1157 | `ORD-PUSH` | `head=0 owner=6 real=0->3` |
| 1211 | `DECOUPLE-DONE` | `u=0 co=1 real=3 tx=12 ty=15` |

现场二完全同构（行 2619/2620/2624/2677）：`veh=57 num=7 real=65535` → `real=5->0`。

**结论（已实证，不是猜测）**：停位打在了 `veh=0`（车底链头、`num=6` 是**车底自己那张表**），
而归还发生在 `veh=54`（耦合后的链头、`num=4` 是**机车自己那张表**）。
`veh=54` 的 `orders_backup_real_index` 从头到尾是 `65535`，所以归还必然落 0。

⇒ **KI-525 点名的 7 个入口不是全部。还有第 8 个入口：`Couple()` 里把机车自己的表停进
`v->orders_backup` 的那一步，只搬了指针、没停 `v` 自己的 `cur_real/cur_implicit`。**
入口 #1（`R3RSyncDrivingOrders` 切表）停的是**当时被切表的那条链**（这里是 `veh=0`），
而承载 `orders_backup` 的是 `v=54` —— 两者不是同一辆车，这就是"修了 7 个入口仍然丢"的原因。

> 疑点 REG-353-A：`ORD-PUSH head=0 owner=6 real=0->3` 说明 `veh=0` 的进度是**在归还之后**
> 才被回灌到 owner 的（0→3），而 `ORD-RETURN-OWN veh=54` 在同一时刻已经把 vm 落 0。
> 即"回灌"与"归还"顺序相反：先归还（落 0）、后回灌（0→3 已经是脏值）。
> 需确认 `R3RPushProgressToOwner` 与 `R3RSegmentReturnOrders` 在 `DecoupleTrain` 里的先后。

### 10.3 填充位置错误：anchor 取的是**跟随期**的 cur_real

| 行 | 探针 | 读数 | 判读 |
|---|---|---|---|
| 769 | `AUTO-ORD` | `seg=3 head=54 iv=[1,2) cursor=1 anchor=1 real=1` | 位置对 |
| 806 | `AUTO-ORD` | `seg=1 head=0 iv=[0,1) cursor=1 anchor=0 real=0` | 位置对 |
| 2302 | `AUTO-ORD` | `seg=4 head=57 iv=[5,6) cursor=1 anchor=5 real=4 cur=other` | 位置对 |
| **2328** | `AUTO-ORD` | `seg=1 head=0 iv=[0,1) cursor=1 **anchor=0 real=3** cur=other` | **位置错**：当前进度 3，记录却插在表首 |

⇒ 同一个入口在 `real=0` 时 anchor=0 是巧合正确，在 `real=3` 时就把记录塞到表首。
**猜测根因（待验证）**：`R3RAutoRecordChainOrders()` 用的 `anchor` 是 `head->cur_real_order_index`
（跟随期被 `R3RSyncDrivingOrders` 回灌的**借来表**坐标），而不是本段自己表上的进度
（`orders_backup_real_index` / 段行坐标）。两个坐标空间混用 ⇒ 数值巧合时对、不巧合时错。

### 10.4 填充被大量丢弃

10 条 `AUTO-ORD-DROP ... reason=not-in-source-range leg=[a,b)`，leg 分别为 `[1,2)` `[4,5)` `[7,10)`，
`n=33`。同一时刻 `AUTO-ORD` 只有 4 条。⇒ 原本该填的绝大多数项被白名单判据丢掉。
`leg=[1,2)` 这种 1 条宽的区间本身可疑（对应"交接点起至下一次交接"的判定），
需确认 `R3RAutoRecordChainOrders()` 计算区间时用的是**控制段的交接点**还是**本段的**。

### 10.5 下一步（按顺序）

1. 定位并补上第 8 个入口（`Couple()` 的 `v->orders_backup` 处停 `v` 自己的进度）；
2. 复核 `R3RPushProgressToOwner` 与 `R3RSegmentReturnOrders` 的调用顺序（REG-353-A）；
3. 把 `AUTO-ORD` 的 `anchor` 换成与本段自己表同一坐标空间；
4. 复核 `leg=` 区间来源，`[1,2)` 这类 1 条宽区间必须能解释。

### 10.6 复测判据（第 353 轮）

①`ORD-RETURN-NOBK` 恒为 0；②`ORD-RETURN-OWN` 的 `real` 不再变成 `0`（除非本来就是 0）；
③`AUTO-ORD` 的 `anchor` 恒等于该段自己表上的当前进度；④`AUTO-ORD-DROP` 只剩真正跨段的项；
⑤负判据：原生共享排程链读档前后逐字不变。

---

## 十一、第 353 轮修法落地

### 11.1 KI-526：补第 8 个借用入口（`Couple()`）

**定位**：`src\train_cmd.cpp` 的 `Couple()` 里，"跟随指针一接上"那三行

```cpp
v->r3r_follow_orders = couple_owner->GetEffectiveOrders();
v->r3r_follow_owner  = couple_owner->index;
v->r3r_orders_borrowed = true;
```

**就是第 8 个入口**：它直接把 `v` 从"跑自己的表"切成"跑 owner 的表"，而
`cur_real_order_index` / `cur_implicit_order_index` 在同一刻变成 owner 表的坐标，
自己表上的位置再没有别处可查。第 352 轮补的七处（movechunk / depot-drag / sell-head /
donor / 两个 `R3RSyncDrivingOrders` 出口 / 懒建）都不覆盖这里。

**现场闭环**（`build\R3R_debug.log`，第 353 轮复测）：

| 行 | 探针 | 读数 |
|---|---|---|
| 487 | `ORD-AFTER-COUPLE` | `head=54 n=33 real=1 borrowed=1 owner=6 own_shared=0 own_first=54` |
| 1152 | `ORD-RETURN-NOBK` | `veh=54 num=4 real=65535 impl=65535 orphan=0` |
| 1153 | `ORD-RETURN-OWN` | `veh=54 num=4 real=2->0 impl=2->0 bk=65535/65535` |
| 2619/2620 | 同构第二次 | `ORD-RETURN-NOBK veh=57` → `ORD-RETURN-OWN veh=57 num=7 real=5->0` |

`num=4`（自己的表 4 条）、`real=65535`（停放索引从没被写过）⇒ 归还必然落 0 ⇒
玩家看到"命令跳回第一条"。

**改法**：在那三行之前插

```cpp
if (R3RParkOwnOrderProgress(v)) {
    R3RDbgWrite("ORD-BORROW-PARK veh=%d bkr=%d bki=%d tag=couple\n", ...);
}
```

`R3RParkOwnOrderProgress()` 自带四道守卫（`orders_backup` 已非空 / 已是跟随者 /
`orders` 为空 / 备份索引已非 INVALID），所以二次耦合与没有自己表的链自动跳过，
不会覆盖借用开始那一刻的位置。必须排在改活索引**之前**。

### 11.2 KI-527：`anchor` 的占位值问题（10.3 的"猜测根因"证伪）

原猜测是"用了活索引（借来表坐标）当 anchor"。**实测代码已经用
`seg->orders_backup_real_index`（7124 行），不是活索引** ⇒ 猜测证伪。

真正的缺口在 10.3 表里 `seg=1 real=3` 那一行：`seg=1` 是链中段，自己没有表，
`AUTO-ORD-NEWLIST` 给它懒建了一张空表，并按 KI-525 的语义把
`orders_backup_real_index` 写成 **占位 0**（7048 行，注释自陈"这张表是刚建的空表、
从未跑过"）。于是 7125 行的 `base = own = 0`，anchor 被钉在表首 ⇒ 记录插到最前面，
而段实际已经在借来表上跑到第 3 条。

**改法**（7124 起）：只有"`orders_backup` 是本段自己的表"时停放索引才可信，
用 `seg->orders != nullptr` 区分；懒建表（`seg->orders == nullptr`）退回 `num`
（= 最后一条自动记录之后）：

```cpp
const bool own_is_real = seg->orders != nullptr;
const VehicleOrderID base = (own_is_real && own != INVALID_VEH_ORDER_ID && own <= num) ? own : num;
```

同一节注释里原先写的"懒建时该字段是 INVALID"与 7048 的实际写法矛盾，已一并订正。

### 11.3 未修（登记待办）

- **REG-353-A**：日志顺序显示 `ORD-RETURN-*`（1152/1153）先于 `ORD-PUSH`（1157），
  但代码里 push（10260/10261）在 settle（10264/10265）之前。⇒ 1157 那条 push 属于
  **另一轮**调用或另一对 v/u，不是顺序颠倒。本轮不据此改码，待下一份日志带
  `veh=` 对确认。
- **KI-528**：10 条 `AUTO-ORD-DROP reason=not-in-source-range leg=[1,2)/[4,5)/[7,10)`
  且同刻只落 4 条 `AUTO-ORD`。`leg` 的 `[1,2)` 一类 1 条宽区间尚未解释，白名单是否过紧
  待判。

### 11.4 构建自证（第 353 轮）

复用既有入口 `R3R_fullbuild.cmd inc detach`（未新建任何 `.cmd`）：

| 项 | 读数 |
|---|---|
| `build\R3R_incbuild.done` | `EXIT_CODE=0`，22:38:06 |
| 日志尾部 | `[3/3] Linking CXX executable openttd.exe` |
| 错误计数 | `error C` / `fatal error` / `FAILED:` / `build stopped` = 0 |
| `src\train_cmd.cpp` | 22:34:26 |
| `train_cmd.cpp.obj` | 22:36:00（晚于源码 ✓） |
| `build\openttd.exe` | 22:37:55，52 344 832 B（晚于 obj ✓） |
| `read_lints` | 0 条 |

本轮只改 `src\train_cmd.cpp`（未碰任何 `src\*.h`），护栏判 `incremental is safe`，走增量 3 步。
