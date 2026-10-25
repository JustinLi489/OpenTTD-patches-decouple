# 第 259 轮 临时分析报告（2026-10-21）

用户原话（摘要）：自动命令"难以描述地难绷"；三键其实可按下，多段式时不能编辑符合预期；
解挂应不耗点（无等待/无行驶时间），等待挂接应有等待时间无行驶时间，前往挂接两者都有；
自动填充与自动分离也要会填 R3R 命令；挂接扫描又是等得久 + 刁钻条件挂不上；发行版又测出预留问题。
指令：**先干主干，节外生枝的进备忘一阵子清算**。

## 零、输入指纹

| 项 | 值 |
| --- | --- |
| 文件 | `D:\temp\codebuddy-dropped-files\5454df73-b980-470e-8e4c-3be7f7c01cf4\R3R_debug.log` |
| 大小 | 476 847 B |
| 行数 | 6 691 |
| mtime | 2026-10-21 00:35:38 |
| 标签计数 | `AUTO-ORD` 72 行、`CPL-PAIR` 115 行、`CLEAR-STN` 78 行、`settle-couple` 98 行、`settle-decouple-u/v` 各 25 行 |

## 一、主干问题 1（已实证）：自动命令把同一个站反复追加，10 秒内长出 14 条重复命令

证据（同一进程、连续行，`seg=1 head=21`）：

```
L566: AUTO-ORD seg=1 head=21 kind=station dest=6 begin=2 count=3 cursor=3
L569: AUTO-ORD seg=1 head=21 kind=station dest=6 begin=2 count=4 cursor=4
L572: AUTO-ORD seg=1 head=21 kind=station dest=6 begin=2 count=5 cursor=5
L575: AUTO-ORD seg=1 head=21 kind=station dest=6 begin=2 count=6 cursor=6
L578: AUTO-ORD seg=1 head=21 kind=station dest=6 begin=2 count=7 cursor=7
L581: AUTO-ORD seg=1 head=21 kind=station dest=6 begin=2 count=8 cursor=8
L584: AUTO-ORD seg=1 head=21 kind=station dest=6 begin=2 count=9 cursor=9
L587: AUTO-ORD seg=1 head=21 kind=station dest=6 begin=2 count=10 cursor=10
L590: AUTO-ORD seg=1 head=21 kind=station dest=6 begin=2 count=11 cursor=11
L593: AUTO-ORD seg=1 head=21 kind=station dest=6 begin=2 count=12 cursor=12
L596: AUTO-ORD seg=1 head=21 kind=station dest=6 begin=2 count=13 cursor=13
L599: AUTO-ORD seg=1 head=21 kind=station dest=6 begin=2 count=14 cursor=14
L618: AUTO-ORD seg=1 head=21 kind=station dest=6 begin=2 count=15 cursor=15
L620: AUTO-ORD seg=1 head=21 kind=station dest=6 begin=2 count=16 cursor=16
```

**结论句**：`begin` 恒为 2、`count/cursor` 同步 +1 ⇒ 每次调用都判定"这是新的一条"，走了 `R3RAutoOrderAppend()` 而不是"相同则 cursor++ 跳过"。即 `R3RAutoOrderSame()`（或游标比对分支）对 `MakeGoToStation()` 产物恒返回 false，或游标每次调用从 0 重算。
**影响**：自动区间无限膨胀；一趟车经过同一站 N 次就长 N 条；下一圈"整段替换"永远无法收敛 ⇒ 自动命令功能整体不可用（用户"难绷"的直接来源）。
**待验证（H-1）**：`MakeGoToStation()` 默认构造的 `Order` 里哪个字段每次不同（怀疑 `travel_time`/`max_speed`/`GetUnloadType` 等隐式差异），或 `R3RAutoOrderSame()` 只比较了 destination 而 `R3RAutoOrderHash()`/比对路径不一致。
**复测判据**：同一站连续经过 3 次以上，`AUTO-ORD` 只应在首次出现 `count` 增长，之后应为 `skip`（不打印新增）或 `count` 不变。

## 二、主干问题 2：三键"不灰但无反应"= 符合预期，KI-389 结案

用户明确：三键其实可以按下，只是**列车处于多段式状态时无法编辑**，这与预期一致。
⇒ KI-389 不是缺陷，第 258 轮加的 `TT-CMD` 探针保持（只有诊断价值，无行为影响），KI 状态改"符合预期/非缺陷"。不再做"重定向到 priority head"的重构。

## 三、待取证/待实现（按用户新口径）

| 编号 | 口径 | 现状 |
| --- | --- | --- |
| KI-391 | `OT_DECOUPLE` 不耗点：无等待时间、无行驶时间；`OT_WAIT_COUPLE` 有等待时间、无行驶时间；`OT_GOTO_COUPLE` 既有等待也有行驶 | **已实现**（见 §六） |
| KI-392 | 自动填充（AutofillTimetable）与自动分离（TimetableSeparation）也要给 R3R 命令（GOTO_COUPLE / WAIT_COUPLE / DECOUPLE）填时刻表 | **已实现**（见 §六） |
| KI-393 | 挂接扫描"等得久 + 刁钻条件挂不上"（`CPL-PAIR` 115 行） | 未取证 |
| KI-394 | 最新发行版（上传 github 那次）又测出预留问题（`CLEAR-STN` 78 行） | 未取证 |

## 四、本轮执行顺序（用户指定"先干主干"）

1. 修 KI-390（自动命令重复追加）——这是主干、且已有铁证。
2. 再按 KI-391 → KI-392（时刻表语义，代码面小、体验直接）。
3. KI-393 / KI-394 先落档，取数后再动（属于"节外生枝"的清算队列）。

## 五、构建自证

（待回填）

## 六、落地记录（KI-390 / KI-391 / KI-392）

### 6.1 KI-390 自动命令重复追加（`src\train_cmd.cpp`）

`R3RAutoRecordChainOrders()` 内两处：

1. 游标归一化后新增回卷——`cursor == count && count > 0` 且区间首条与 `want` 同 ⇒ `cursor = 0`，让后面的线性比对重新命中，不再落到"区间末尾追加"。
2. `if (cursor < count) { ... }` 之后新增 `else if (count > 0)`：`want` 与区间末条相同时 `continue`。

预期日志：同一 `seg`/`begin` 的 `AUTO-ORD` 行只在首次出现 `count` 增长，之后 `count`/`cursor` 稳定；循环线路收敛为一条。

### 6.2 KI-391 时刻表耗时语义

| 订单 | 等待时间 | 行驶时间 |
| --- | --- | --- |
| `OT_DECOUPLE` | 无（禁用 + 钳 0） | 无（禁用 + 钳 0，行显示"无行驶时间"） |
| `OT_WAIT_COUPLE` | 有（可编辑、autofill 会填） | 无（禁用 + 钳 0） |
| `OT_GOTO_COUPLE` | 有 | 有 |

实现分两层：

- 判据层（`src\order_base.h`）：`HasWaitTimetableTime()` / `HasTravelTimetableTime()` / `IsCompletelyTimetabled()`。
- 落点层：`ChangeTimetable()` 钳位（清旧档脏值 + 记账一致）、`CmdSetTimetableParams()` 白名单（等待时间放开 GOTO/WAIT_COUPLE、行驶时间对不适用类型拒写）、`UpdateVehicleTimetable()` 不测不填、`timetable_gui.cpp` 告警/禁用/文案三处。

### 6.3 KI-392 自动填充与自动分离

- 自动填充：`UpdateVehicleTimetable()` 的实测写回按 6.2 的谓词过滤 ⇒ 三类 R3R 命令各自只填适用的字段。
- 自动分离：`IsOrderUsableForSeparation()` 开头加 `if (!order->HasTravelTimetableTime()) return true;`（这是原先"含 R3R 命令 ⇒ 整表不可分离"的硬闸门）；配合 `IsCompletelyTimetabled()` 的补丁，含 R3R 命令的时刻表不再恒判不完整。累计时长求和天然是 0/仅等待。

### 6.4 未做（仍属"节外生枝"队列）

- KI-393 挂接扫描慢/刁钻条件挂不上：只落档，未取数。
- KI-394 发行版预留问题：只落档，未取数。
- 未加 `TT-CMD` 之外的时刻表探针；KI-390 的修复靠 `AUTO-ORD` 的 `count` 是否收敛来判读，已足够。
