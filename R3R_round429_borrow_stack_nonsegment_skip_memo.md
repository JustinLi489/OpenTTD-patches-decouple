# 第 429 轮：排程借用层边界核查 + 非段链跳过 R3R 命令

玩家 2026-11-13 原话（两条）：

> 1，检查自有表，分配表和借用表的边界。因为我没有实测但是提出潜在的问题点：A车自有调度a，分配了调度b，随后去挂接B车并借用了调度c，随后被解挂重新变为控制段，它应当返还调度b，在跑完调度b之后，再返还为调度a。我查看到了这其中可能存在的紊乱的排程借用返还
>
> 2，当列车执行r3r命令时，假如里面含有非段部分，我们应当直接跳过该调度命令，而不是简单的回滚。因为我发现了很多起新手玩家不会设置段，结果用非段列车解挂出头车的错误示范。我需要把这条路堵死

## 一、现状取证（行号锚点）

### 1.1 三层身份的实际实现

| 层 | 载体 | 代码 |
| --- | --- | --- |
| 自有表 a | `orders`（修法 D 后**恒留在此**，不再搬进 backup） | `train_cmd.cpp:7985-7994` |
| 借用指针 | `r3r_follow_orders` + `r3r_follow_owner` + `r3r_orders_borrowed` | `R3RBeginBorrow()` `:6913-6942` |
| 池借用标记 | `r3r_follow_pool` + `orders_backup_timetable_index` | `:7801-7803` |

`R3RBeginBorrow()` 末尾（`:6939-6941`）：

```cpp
chain->r3r_follow_orders = other;
chain->r3r_follow_owner = owner;
chain->r3r_orders_borrowed = true;
```

**这是纯赋值，不是入栈**。

### 1.2 书签在多层场景下的行为（正确）

`R3RRefreshOrderBookmark()` 的守卫 `:6852`：

```cpp
if (v->GetEffectiveOrders() != own) return false;
```

⇒ 车主自己那张表 a 的坐标只在「驱动表 == a」时写入；A 正跑 b 时再借 c，书签仍停在 a 的坐标，**底层 a 的位置不会被打脏**。

### 1.3 已确认的两个真实缺陷（需求 1）

**缺陷 P-1（跟随指针被覆盖 ⇒ 池坑位放错）**
- `TrainDispatchHandler()` `:7801` 先 `R3RBeginBorrow(v, pool_orders=b, host, "dispatch", true)` ⇒ `r3r_follow_orders = b`。
- 随后挂接 → `Couple()` 内的借用入口再调 `R3RBeginBorrow(chain, c, ...)` ⇒ `r3r_follow_orders = c`，**b 的指针丢失**。
- 归还端 `R3RReturnDispatchPool()` `:7843-7844`：

```cpp
const OrderList *const borrowed = v->r3r_follow_orders;
const uint32_t pool_schedule = (borrowed != nullptr) ? borrowed->index.base() : ...;
```

⇒ 会拿 **c** 的索引去 `R3RDispatchPoolRelease()`，释放的是别人/不存在的坑位，b 的 `issued` 永久泄漏。

**缺陷 P-2（单层跟随 ⇒ 无法 a←b←c 逐层归还）**
- `R3RReturnBorrowedOrders()` `:7625-7676` 的跟随分支只做 `chain->r3r_follow_orders = nullptr;`，一次退回 **`orders` 层 = a**，中间的 b 被整层跳过。
- 玩家要求的顺序 `解挂 → 还 c → 跑完 b → 还 a` 在当前结构下**结构上不可能**。

## 二、待验证 / 待取证

- V-1：`Couple()` 内那处借用的确切参数与是否带 `refresh`（KI-526 第 8 入口）。
- V-2：`GetEffectiveOrders()` 的解析优先级，确认"跟随指针 > orders"。
- V-3：是否已有任何"多层"兼容态字段（如 `orders_backup` 被挪作二层）。

## 三、需求 2 现状

`R3RSkipCoupleOrdersForRealArtic()` `:22518-22576` 只对 **真铰接** 生效（`:22528` `R3RChainHasRealArticPart`）。非段车厢链（无 ★/⊗ 的散车厢）**没有**这道闸门，仍会走 `GOTO_COUPLE/WAIT_COUPLE/DECOUPLE`，失败后靠 `CPL-*`/`DECOUPLE-*` 回滚收场。

## 四、结论（需求 1：玩家提出的紊乱**成立**，且比描述的更糟）

借用入口 `Couple()` `:13211`：

```cpp
R3RBeginBorrow(v, couple_owner->GetEffectiveOrders(), couple_owner->index, "couple");
```

`refresh` 默认 false，函数体 `:6939-6941` 是**纯赋值**：

```cpp
chain->r3r_follow_orders = other;
```

⇒ 若 `v` 此刻正在跑池表 b（`r3r_follow_orders == b`、`r3r_follow_pool == true`），
这一次赋值把 **b 的指针永久丢掉**，且 `r3r_follow_pool` 仍为 true。

后果链：

1. 解挂 ⇒ `R3RSyncDrivingOrders(chain,false)` ⇒ owner==chain ⇒ `R3RReturnBorrowedOrders()`
   `:7625-7676` 跟随分支 `:7672` 只做 `r3r_follow_orders = nullptr` 并把索引写成
   `orders_backup_real_index`（= a 的书签）⇒ **直接退回 a，池表 b 整层跳过**。
2. 如果先走 `R3RReturnDispatchPool()`（池表跑完的拦截路径），`:7843-7844`：

```cpp
const OrderList *const borrowed = v->r3r_follow_orders;   // 此刻是 c
const uint32_t pool_schedule = (borrowed != nullptr) ? borrowed->index.base() : ...;
```

⇒ 拿 **c** 的索引去 `R3RDispatchPoolRelease()` ⇒ **b 的 issued 永久泄漏，还可能误放别人坑位**。

（书签本身没坏：`R3RRefreshOrderBookmark()` `:6852` 的守卫 `GetEffectiveOrders() != own`
挡住了覆盖，a 的位置一直是准的。坏的只有"层"。）

## 五、落地（需求 1，已实现）

最小侵入：加一个「池层挂起」槽 `OrderList *r3r_pool_orders`（只存指针，位置与 tt 已经
在 `orders_backup_*_index` 里且被守卫保护），四处接线：

| 位置 | 代码 | 日志 |
| --- | --- | --- |
| `src\vehicle_base.h` | `OrderList *r3r_pool_orders = nullptr;`（新增字段） | — |
| `src\sl\couple_group_sl.cpp` | R3VP 表加 `NSL("pool_orders", SLE_REF(Vehicle, r3r_pool_orders, REF_ORDERLIST))`；`R3RHasParkState()` 追加 `v->r3r_pool_orders != nullptr` | — |
| `Couple()` `:13249` | 借用前挂起：`if (v->r3r_follow_pool && v->r3r_follow_orders != nullptr && v->r3r_pool_orders == nullptr) v->r3r_pool_orders = v->r3r_follow_orders;`（已有挂起层不覆盖，与恢复分支一一对应） | `ORD-BORROW-STACK veh=%d pool=%u tag=couple` |
| `R3RReturnBorrowedOrders()` `:7605` | `was_follower && r3r_pool_orders != nullptr` ⇒ 换回池表 `r3r_follow_orders = pool`、`r3r_follow_pool = true`、`r3r_follow_owner` 按 `pool->GetFirstSharedVehicle()` 重算、**活索引一个字不动**、`return true`（不写 a 的书签、不清 borrowed） | `ORD-RETURN-TO-POOL veh=%d pool=%u real=%d impl=%d` |
| `TrainDispatchHandler()` `:7788` | 已有挂起层 ⇒ 拒绝二次接管、原地等（防 follow_orders 被二次覆盖） | `DISP-POOL-BUSY veh=%d st=%d` |

要点：池表坐标 = 自有表书签（`orders_backup_*`），耦合借用的 `refresh=false` 且被
`R3RRefreshOrderBookmark()` 的守卫 `GetEffectiveOrders() != own` 挡住，所以挂起层**不需要**
额外存索引，只存指针。跑完池表后 `R3RReturnDispatchPool()` 照旧调本函数回自有表 a。

## 六、落地（需求 2，已实现）

`R3RSkipCoupleOrdersForRealArtic()` `:22518` 已有完整的「跳过 R3R 命令」原语（沿 R3R 命令
连续段前跳到第一条普通命令、全是 R3R 时只丢当前命令）。把判据从"含真铰接"扩为
"含真铰接 **或** 链不是全段"：

- `R3RChainAllSegments` **改名为** `R3RChainAllWagonsSegmented(const Train*)` 并改判据
  （旧名/旧判据「链头必须是 ★」会误拦「机车 + 段车底」，已删）。
  新判据：**段外只允许**「原型不是 `RailVehicleType::Wagon`」且（`IsEngine()` 或 `IsDualHeadRear()`）；
  段外出现原型是车厢的车（含被 `MakeConsist` 提升成假引擎的散车厢链头）⇒ 含非段部分；
  逐节 `IsArticulatedPart()` 跳过。
- `:22600-22603`：
  ```cpp
  const bool gate_real_artic = R3RChainHasRealArticPart(consist);
  const bool gate_non_segment = !R3RChainAllWagonsSegmented(consist);
  if (!gate_real_artic && !gate_non_segment) return false;
  const char *const gate_reason = gate_non_segment ? "non-segment" : "real-artic";
  ```
- 两处 `ARTIC-SKIP-R3R` 日志追加 `reason=%s`。
- **DECOUPLE 也要堵**（`~:23394` 求值块）：`nothing_to_release || non_segment_chain` 一并走
  `R3RSkipUnfireableDecoupleOrder()` 收场（否则每 tick 重触发），日志 `DECOUPLE-SKIP ... reason=non-segment|nothing-to-release`。

## 七、构建自证（第 429 轮，回填）

入口 `R3R_fullbuild.cmd inc detach`（未新建任何 `.cmd`）。

**第一次全量**：护栏 `build\R3R_incbuild.guard.log` 判 `1 header/lang file(s) are newer` /
`newer: src\vehicle_base.h` / `REMOVED 621 object file(s) - upgrading this build to a FULL rebuild`（改头文件，预期）。
结果 **失败**：`src\train_cmd.cpp(22604)` / `(23402)` `error C3861: "R3RChainAllWagonsSegmented"：找不到标识符`。

**根因（重要）**：上一轮只把**调用点**改成新名，函数体仍留着旧名 `R3RChainAllSegments`
（`:22541`），且旧判据是「链头必须是 ★」，会把「机车 + 全是段的半挂车底」这种唯一正确用法误拦。
本轮把旧函数整体改名为 `R3RChainAllWagonsSegmented` 并改判据：
段外只允许「原型不是 Wagon」且（`IsEngine()` 或 `IsDualHeadRear()`）——用
`RailVehInfo(v->engine_type)->railveh_type == RailVehicleType::Wagon` 识别"原型是车厢"，
从而把被 `MakeConsist` 提升成假引擎的散车厢链头（`IsEngine()` 为真、原型是 Wagon）也拦下。
另在 `ORD-RETURN-TO-POOL` 分支补 `InvalidateVehicleOrder(chain, 0)`（换驱动表，窗口要刷新）。

**第二次增量**：`[26/26] Linking CXX executable openttd.exe`、
`build\R3R_incbuild.done` = `EXIT_CODE=0`、`error C`/`fatal error`/`FAILED:`/`build stopped` 计数 **0**；
`src\train_cmd.cpp` 02:17:18 → obj 02:17:52 → `build\openttd.exe` **2026-11-13 02:21:02（52 867 072 B）**；
`read_lints` 三文件 0 条。
exe 字面量自证（`findstr`）：`ORD-RETURN-TO-POOL veh=%d pool=%u real=%d impl=%d`、
`ORD-BORROW-STACK veh=%d pool=%u tag=couple`、`DISP-POOL-BUSY veh=%d st=%d`、
`non-segment` / `real-artic`、`DECOUPLE-SKIP consist=%d tx=%d ty=%d real=%d segs=%u reason=%s` 全部命中。
注：`reason=non-segment` 整串在 exe 里不存在是正常的——代码里 `reason=%s` 与 `"non-segment"`
是两个独立字面量，按 `non-segment` 单独检索即可。

## 七之二、踩坑记录（供后人）

1. **改名只改调用点不改定义** ⇒ `error C3861`，且因为改头文件导致全量，白等 20 分钟。
   教训：`replace_all` 改函数名时必须先 `search_content` 确认定义与调用点都命中（定义在
   使用点之前，否则还要补前向声明）。
2. 判据不能写成「链头必须是 ★」：牵引机车不属于任何段，这是正常用法。
3. 假引擎头 `IsEngine()` 为真，必须再查原型 `RailVehInfo(...)->railveh_type`。

## 八、复测判据（交给玩家）

需求 1（a→b→c 三层）：
1. 列车 A 跑池表 b 时被挂接，日志应出现 `ORD-BORROW-STACK veh=A pool=b`。
2. 解挂后 A 重新成为控制段，日志应先出现 `ORD-RETURN-TO-POOL veh=A pool=b`，**不得**直接跳回 a。
3. 池表 b 跑完后 A 才回到自有表 a（`ORD-RETURN-OWN` / 书签索引与自有表长度吻合）。
4. 全程 `DISP-POOL-BUSY` 出现即说明有二次接管被正确拒绝（不是错误）。

需求 2（非段链）：
5. 含未成段散车厢的列车排 GOTO_COUPLE / WAIT_COUPLE / DECOUPLE ⇒ 日志
   `ARTIC-SKIP-R3R ... reason=non-segment` 并直接跳到后续普通命令，不出现 `COUPLE-FAIL` 刷屏。
6. 同一列车排 DECOUPLE ⇒ `DECOUPLE-SKIP ... reason=non-segment`，且**不会**解出车头。
7. 负判据：「机车 + 全是段的半挂车底」照旧能挂/解，日志 reason 不出 `non-segment`。
8. 负判据：真铰接链仍走 `reason=real-artic`，行为与第 180 轮读档打散一致。

