# 共享排程 × 自动填充：现状取证（第 374 轮）

## 1. 任务来源（玩家原话）

> 「假如一个调度计划被多列车共享使用，那么这个调度计划假若被轮流自动填充会怎么样，我看现状是最后什么也没有填充进去」

## 2. 待答问题

- Q1：共享 `OrderList`（原生 Ctrl+共享 / `IsOrderListShared()`）被 N 列车共用时，R3R 的自动填充（`AUTO-ORD`）由谁写入、写进哪张表？
- Q2：多列车轮流触发自动填充时，彼此的写入是覆盖、丢弃、还是各写各的？
- Q3：玩家观察「最后什么也没填充进去」的机制解释是什么（是零写入、还是写后被别人冲掉、还是写进了不共享的那一份）？

## 3. 本轮已知的代码锚点（来自既有 KI，待逐条复核行号）

- `R3RAutoRecordChainOrders()`（`src\train_cmd.cpp`，KI-527 记录在 ~7048/~7124 一带）
- `AUTO-ORD-NEWLIST` 懒建空表 + 把 `real_index` 写占位 0（KI-527 修复点）
- `AUTO-ORD-DROP kind= dest= reason=not-in-source-range`（第 264 轮续的 C-2 白名单）
- `auto_ranges`（第 264 轮续 F 项：解挂单段化后会被清空 ⇒ `SEGTR-SNAP ... auto n=0 first=[0,0)`）
- `own_is_real = (seg->orders != nullptr)`（KI-527 修复：只有自己是表主人才信停放索引）
- 共享相关原生 API：`IsOrderListShared()` / `AddToShared()` / `RemoveFromShared()` / `GetFirstSharedVehicle()`

## 4. 待验证判据（先写死，取证后逐条打勾/打叉）

- D1：自动填充的写入目标是否恒为「链头自己的 `orders`」，而与这张表是否被别的车共享无关。
- D2：若该表被共享，链头 `orders` 与其它共享成员是**同一个 `OrderList*`**，那么写入是否对所有成员立即可见（即"轮流填充"应当是叠加而非互删）。
- D3：出现「什么也没填进去」的必要条件里，是否包含 `own_is_real==false`（链头借用别人的表）⇒ 走 `base=num` 分支把记录钉到表尾/被白名单全丢弃。
- D4：`auto_ranges` 的宿主是「段行的链头」而非「表」，共享表的 N 个成员是否共用同一区间记录 —— 若各记各的，第二轮就把第一轮标的区间判为非法而全部 `DROP`。

## 5. 现场证据

### 5.1 日志统计（`build\R3R_debug.log`，Select-String，避开 gitignore 假阴性）

| 探针 | 行数 |
|---|---|
| `AUTO-ORD`（含下列子族） | 9 |
| `AUTO-ORD-DROP` | 4 |
| `AUTO-ORD-NEWLIST` | **0** |
| `AUTO-ORD-KEEP` | 0 |
| `SEGTR-SNAP` | 33 |

`AUTO-ORD` 正文 5 行，**全部 `own=0 borrowed=1`**（链头都在借用别人的表）：

```
365 : AUTO-ORD seg=4 head=57 slot=0 kind=station dest=2 iv=[2,3) anchor=2 real=4 cur=waypoint own=0 borrowed=1 n=7
370 : AUTO-ORD seg=1 head=0  slot=0 kind=station dest=2 iv=[0,1) anchor=0 real=3 cur=other    own=0 borrowed=1 n=7
1679: AUTO-ORD seg=5 head=48 slot=0 kind=station dest=5 iv=[3,4) anchor=3 real=7 cur=waypoint own=0 borrowed=1 n=15
1694: AUTO-ORD seg=1 head=0  slot=0 kind=station dest=5 iv=[0,1) anchor=0 real=6 cur=other    own=0 borrowed=1 n=7
```

`AUTO-ORD-DROP` 4 行全部 `reason=not-in-source-range leg=[7,10)` / `leg=[7,7)`，`n=33` 与 `n=15`。

⇒ 现场**没有一个 `own=1`（写进自己表）的样本**，也没有 `AUTO-ORD-NEWLIST`。

### 5.2 源码锚点（决定性，`src\train_cmd.cpp`）

`R3RAutoRecordChainOrders()`（:7025）。

**闸门 1（:7038）—— 单段链直接不记录：**

```cpp
const std::vector<Train *> segs = R3RGetSegmentHeads(chain);
if (segs.size() < 2) return;   /* Whole chain is one segment: nothing to record. */
```

**闸门 2（:7045-7046）—— 只记录「非控制段」：**

```cpp
for (Train *const seg : segs) {
    if (seg == ctrl) continue;
```

**闸门 3（:7082）—— 共享表一字不写：**

```cpp
if (ol->IsShared()) continue;   /* Never write into a shared list. */
```

⇒ **玩家看到的「什么也没填进去」是这三道闸门的叠加结果，不是异常**：
①原生"多列车共享同一调度计划"的场景里，每列车都是**独立单段链**（`segs.size() == 1`）⇒ 在 :7038 就 return，连进入循环的机会都没有；
②即使链里有多段，只要段自己那张表是原生共享表 ⇒ :7082 直接 `continue`，按注释是**刻意**的（"Never write into a shared list"）。

**写入目标（:6788-6792）：**

```cpp
static OrderList *R3RSegmentOwnOrderList(const Train *seg)
{
	if (seg->orders != nullptr) return seg->orders;
	return seg->orders_backup;
}
```

注释（KI-465 修法 D）自陈：`orders` 恒为**本段自己的表**，借用只由 `r3r_follow_orders` 表达。⇒ 自动填充的写入对象是"段自己的私有表"，**从设计上就不是共享表**；共享表（`IsShared()==true`）在同一条链上有第二处同款闸门：

`R3RAutoFinaliseChainOrders()`（:7288）：

```cpp
if (row == nullptr || ol == nullptr || ol->IsShared()) continue;
```

⇒ 「不写共享表」是记录侧与收尾侧**两处一致的硬约束**，不是漏判。


## 6. 结论

### 6.1 判据逐条

| 判据 | 结论 | 依据 |
|---|---|---|
| D1 写入目标恒为「本段自己的表」 | **成立** | `R3RSegmentOwnOrderList()` :6788 只看 `seg->orders` / `seg->orders_backup`，与共享无关 |
| D2 共享表写入对全成员可见 | **永不发生** | `IsShared()` = `num_vehicles > 1`（`order_base.h:1742`）；记录侧 :7082 与收尾侧 :7288 都显式 `continue` |
| D3 `own_is_real==false` ⇒ `base=num` | **成立但已不是主因** | :7158 `own_is_real = (seg->orders != nullptr)`；KI-527 已把"钉表首"改为"钉表尾" |
| D4 区间记录按段行、按表各记各的 | **成立** | `auto_ranges` 宿主是 `R3RSegmentRecord`（按 `r3r_segment_id`），而 signature 用 `R3RAutoOrderHash(ol, ...)` 校验同一张表 |

### 6.2 玩家场景的两种解读与结果

**解读 A：原生共享订单（Ctrl+共享，N 列车同一 `OrderList*`，`num_vehicles > 1`）**

- 每列车都是**独立单段链** ⇒ `R3RGetSegmentHeads(chain).size() == 1` ⇒ :7038 **直接 return**，一次都不进循环；
- 即便某个成员因耦合变成多段链，它 `R3RSegmentOwnOrderList()` 得到的若正是那张共享表 ⇒ :7082 `continue`；
- 收尾 `R3RAutoFinaliseChainOrders()` :7288 同样跳过。
⇒ **结果恒等于零记录**，与玩家观察逐字一致。这是三道闸门的硬结果，不是偶发失败。

**解读 B：耦合后多段共用控制段排程（R3R 自己的"借表"）**

- 这是自动填充的**设计场景**，现场日志 5 条 `AUTO-ORD` 全是 `own=0 borrowed=1`，说明它确实在工作；
- 但落点是段的**私有影子表**（`orders`/`orders_backup`），不是控制段那张正在跑的表；
- :7244-7246 注释自陈：「`own=0` 表示本段仍在借用控制段计划，命令落进它自己的 `orders_backup`，而订单窗口（显示 `orders`）看不到它们」。
⇒ **记录写进去了，但界面上看不到** ⇒ 玩家主观感受仍是"什么也没填进去"。

### 6.3 假如「真的允许轮流写共享表」会发生什么（推演，未实现）

1. **区间互相擦除**：`auto_ranges` 按段行存，但 `signature = R3RAutoOrderHash(ol, begin, count)` 校验的是**同一张共享表**。A 写入后表内容变了，B 那一行在 :7101 校验必失配 ⇒ B 下个 tick 把 A 的区间 `AUTO-ORD-DROP sane=1` 清掉（注意现场 4 行 DROP 是 `reason=not-in-source-range`，**不是**这条，说明现在根本没走到这一步）。
2. **锚点互相踩**：`anchor` 依赖 `seg->cur_real_order_index` / `orders_backup_real_index`，共享表下这些索引是**每车各一份**，N 列车在同一张表上各有各的"下一条"，各自往后插 ⇒ 同一张表被从 N 个不同位置插入，玩家的手写命令被打散（正是 KI-503 当年遇到的形态）。
3. **`num_vehicles` / `first_shared` 不再可信**：若某段懒建新表后又被 `AddToShared`，`Create(nullptr, seg)` 的 `first_shared` 会指向段而非链头（:7060-7061 注释已提示这条耦合风险）。
⇒ 结论：**"轮流写共享表"在当前设计下不是"没实现"，而是刻意禁止**；要支持它，必须先给共享表一个"全成员共用的区间记录 + 单调游标"，否则一开就是互相擦除。

### 6.4 一句话回答玩家

现状「什么也没填充进去」有两层且都是**设计使然**：
① 共享计划（`num_vehicles > 1`）被代码显式排除在自动填充之外（记录侧、收尾侧两处 `IsShared()` 闸门）；
② R3R 内部"借控制段排程"的场景确实在记，但记进段的私有影子表，订单窗口显示的是正在跑的那张表，所以**看不见**。

## 7. 复测判据 / 修法方向

- 若要"共享计划也能自动填充"，最小改动点与代价：
  1. 放开 :7082/:7288 的 `IsShared()` 闸门 —— **不可取**（6.3 的擦除会立刻出现）；
  2. 改为「共享表按 `OrderList*` 存一份区间记录（而不是按段行）」+ 以 `first_shared` 车的索引为唯一锚点 —— 可行，但要改 `R3RSegmentRecord` 的槽位语义与存档（R3SG 段行），且要解决"谁有资格写"（建议只允许 `first_shared` 那一票写）；
  3. 折中（推荐先做取证/体验修正）：**在订单窗口直接显示段的影子表**（或在窗口顶部标一行"自动记录在 <段号> 的备份表里，共 N 条"），让记录从"看不见"变成"看得见但不可执行"（KI-396 的既定口径就是"只作参考不执行"）。
- 复测判据（若做 3）：多列车共享同一计划跑一圈 ⇒ 段行 `auto_ranges_count > 0` 且窗口能点开看到这些记录；负判据：共享表本身的 `GetNumOrders()` 不变。

