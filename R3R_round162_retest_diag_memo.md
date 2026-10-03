# R3R 待复测条目 + 2026-09-29 现场日志诊断报告

> 临时备忘（诊断报告）。配套条目写入 `R3R_KNOWN_ISSUES.md` 末尾「第 163 轮」小节。
> 玩家原话：「我们最近是不是有一个 KI 等待我的复测？我有回复：日志来了，务必记下诊断报告到临时备忘」。

## 0. 现场身份

| 项 | 值 | 结论 |
|---|---|---|
| 日志 | `build\R3R_debug.log`，493 828 B / 7 599 行，mtime **2026-09-29 23:14:51** | 新采 |
| exe | `build\openttd.exe`，51 536 896 B，mtime **2026-09-29 22:32:15** | = 第 161（KI-263）+ 162（KI-262）轮构建 |

⇒ 这份日志就是"第 161/162 轮"构建的现场回执。

### 事件时间线

```
 269  COUPLE-OK      loco=24 rear=23 consist=0 co=1 real=1  tx=1  ty=11
 703  DECOUPLE-DONE  u=0  co=1 real=4  tx=33 ty=9
1003  COUPLE-OK      loco=27 rear=23 consist=0 co=1 real=5  tx=34 ty=9
1338  DECOUPLE-DONE  u=6  co=1 real=7  tx=58 ty=26
2230  DECOUPLE-DONE  u=0  co=1 real=3  tx=60 ty=20
2667  COUPLE-OK      loco=27 rear=23 consist=6 co=1 real=8  tx=58 ty=27
3032  DECOUPLE-DONE  u=6  co=1 real=10 tx=58 ty=63
3408  COUPLE-OK      loco=50 rear=23 consist=6 co=1 real=11 tx=58 ty=63
3778  DECOUPLE-DONE  u=6  co=1 real=13 tx=60 ty=95
5205  COUPLE-OK      loco=48 rear=8  consist=6 co=1 real=14 tx=60 ty=90
（之后 152 次 COUPLE-FAIL，日志结束）
```

场景 = 3 台机车（24 / 27 / 48、50）反复挂/解一条 21 节车底链（`48,49,50` + 6 个三节铰接单元）。`consist=6` = 被挂的是 car-only formation（车厢-only 车组）。

## 1. 待复测条目清单（回答"是不是有一个 KI 等复测"）

**不止一个，最近三批都还挂着**（同一个 exe，可一次跑完）：

| 轮次 | 条目 | 严重度 | 本日志判定 |
|---|---|---|---|
| 第 161 轮 | **KI-263** 车库拖动 + 读档补穿号/名 | 中 | 判据 1 成立；判据 2/3/4 未覆盖 |
| 第 162 轮 | **KI-262** 站场标题 / `{STRING}` 参数 / 订单窗口过宽 | 低 | 全部无法从日志判定（纯 UI 目测） |
| 第 160 轮 | **KI-261** 名称借/还 + R3VP 存档 | 中 | 判据 1/2 成立；判据 3 未覆盖 |
| 第 159 轮 | 控制段不变式（乙口径） | — | 8/8 成立 |
| 第 157 轮 | 段头重同步 `SEGFRONT-RESYNC` | — | 不可观测（见 §2.4） |
| 第 158 轮 | **KI-253** 图像分离"像被狗啃" | 中 | **仍在复现**，另拿到提交后硬数据（§3 A） |

## 2. 判据逐条判定（带行号）

### 2.1 KI-263（第 161 轮）—— 判据 1 成立

```
 165  UNIT-PARK veh=6 bk=2
 167  DEPOT-XFER-ID depot-edit head=0 ctrl=6 unit=2 own_bk=1 hasname=1 own_name=1
 170  CTRL-PARK tag=depot-edit head=0 seg=1 unit=1
 172  INVAR-CTRL tag=depot-edit head=0 nseg=2 ctrl=6 ctrl_pri=1 head_pri=2 borrowed=1
```

- 判据 1（拖动到列车前面后出现 `DEPOT-XFER-ID depot-edit`）：**成立**。链头 `head=0` 从控制段 `ctrl=6` 穿到号 `unit=2`（自己那个号已停进备份 `own_bk=1`，名字也穿了 `hasname=1 own_name=1`）；`170` 说明停放走新路径；`172` 不变式自洽。
- 判据 2（读档 `tag=load`）：**未覆盖**。全日志 `DEPOT-XFER-ID` 仅 1 条且 `tag=depot-edit` ⇒ 本次会话**没发生读档**。需玩家「存档 → 退出 → 读档」再看号/名。
- 判据 3（幂等 / 不把控制段的号放回号池）、判据 4（反例不误伤）：样本仅 1 条，判不了（只能确认无多余 XFER 行）。

### 2.2 KI-261（第 160 轮）—— 判据 1/2 成立

- `NAME-XFER = 5`，例：`5204 NAME-XFER head=48 u=6 head_own=0 got_u=1`、`3407 head=50 u=6 ... got_u=1`。
- `NAME-RESTORE = 3`（733 / 1368 / 3062），对应三次解挂（u=0 / u=6）。
- 判据 1（挂车时链头穿上被挂车组的名）：**成立**。判据 2（解挂时归还 + 解出方拿回车组名）：**成立**。
- 判据 3（存档往返 `name_backup` 仍在 R3VP）：**未覆盖**（无读档）。
- 判据 4（反例）：`5204` 的 `head_own=0` ⇒ 链头本来没名字也照借 `got_u=1`，方向正确。`XFER=5` 而 `RESTORE=3` 属正常（head=50 / head=48 挂上后日志即结束，未解挂）。

### 2.3 控制段不变式（乙口径）—— 8/8 成立

```
 172  INVAR-CTRL tag=depot-edit head=0  nseg=2 ctrl=6  ctrl_pri=1 head_pri=2 borrowed=1
 336  INVAR-CTRL tag=couple     head=24 nseg=3 ctrl=6  ctrl_pri=1 head_pri=3 borrowed=1
 695  INVAR-CTRL tag=decouple-u head=0  nseg=2 ctrl=6  ctrl_pri=1 head_pri=2 borrowed=1
1073  INVAR-CTRL tag=couple     head=27 nseg=3 ctrl=6  ctrl_pri=1 head_pri=3 borrowed=1
1330  INVAR-CTRL tag=decouple-v head=27 nseg=2 ctrl=0  ctrl_pri=1 head_pri=2 borrowed=1
2729  INVAR-CTRL tag=couple     head=27 nseg=2 ctrl=6  ctrl_pri=1 head_pri=2 borrowed=1
3468  INVAR-CTRL tag=couple     head=50 nseg=2 ctrl=6  ctrl_pri=1 head_pri=2 borrowed=1
5265  INVAR-CTRL tag=couple     head=48 nseg=2 ctrl=21 ctrl_pri=1 head_pri=2 borrowed=1
```

全部 `ctrl != head`、`ctrl_pri=1 < head_pri=2`、`borrowed=1` ⇒ **"控制段 = 命令所有者段（优先级最小者），链头只是承载者"逐条自洽**。`5265` 最直白：链头 48 只是被挂后的物理链头，控制段是 ★ 段头 **21**。

### 2.4 第 157 轮 `SEGFRONT-RESYNC` —— **不可观测，不能判"没触发"**

- 全日志 `SEGFRONT-RESYNC*` **0 命中**（字符串确实在 exe 里，已核对）。
- 原因不是没触发，而是**采样窗口**：`src\train_cmd.cpp` 该处 `checks % 64` 才打一行 `-CHECK`；本会话 settle 提交点只有约 **10 次**（5 couple + 5 decouple + 1 depot-edit）⇒ 结构上打不出来。
- ⇒ 判据不可观测。修法：(a) 改成"首次 + 字段变化时"打印；(b) 换判据。

### 2.5 KI-262（第 162 轮）—— 日志无法判定

4 条判据（站场标题不再 `(invalid parameter)` / 解挂与挂接下拉框标签正常 / 订单窗口宽度恢复 / 其它 plane 无回归）**全是 UI 目测**，日志零覆盖，需玩家目视确认。

## 3. 本轮新拿到的东西（仍未解决 / 需玩家确认）

### A. KI-253 图像分离**仍在复现**，并首次拿到"提交之后"的硬数据 🔴

链结构（日志 5177-5197，`GEO respace-after head=48`）：21 节 = `48,49,50` + 6 个三节铰接单元
`(21,22,23) (18,19,20) (15,16,17) (12,13,14) (9,10,11) (6,7,8)`。
每单元构成 = `[clen2 non-artic][clen8 artic][clen2 artic]`（中间那节是 artic part）。
`GEO` 的 `dist` 是"本车→`Next()` 的像素中心距"，`nom` 是 `Train::CalcNextVehicleOffset()`。

**（A1）单元内部间距正确，单元与单元之间系统性多出 ~19-20px：**

```
 n=5 veh=23 dist=21 nom=2     ← 23→18 应为 2，实际 21
 n=8 veh=20 dist=22 nom=2     ← 20→15
n=11 veh=17 dist=22 nom=2     ← 17→12
n=14 veh=14 dist=22 nom=2     ← 14→9
n=17 veh=11 dist=22 nom=2     ← 11→6
（对照单元内部）
 n=6 veh=18 dist=5 nom=5      ← 18→19 正确
 n=7 veh=19 dist=5 nom=5      ← 19→20 正确
```

⇒ **每一个铰接单元边界都裂开约 20px**，这正是"一节一节之间被啃掉"的观感来源。
`nom` 基于 `cached_veh_length`（2/8/2 ⇒ 名义 2px），而实测 pitch 21-22px，**必须先确认哪一侧是真值**（§5 建议 3）。

**（A2）含 ★ 的那个单元整体塌成一格，且是 `RESPACE-AFTER-EDIT` 引入的：**

```
（respace 之前）GEO-BEFORE couple  n=3 veh=21 x=968 y=1466
                                   n=4 veh=22 x=968 y=1461
                                   n=5 veh=23 x=968 y=1457      ← 5/4，正确
（respace 之后）GEO respace-after  n=3 veh=21 x=968 y=1456 dist=0 nom=5
                                   n=4 veh=22 x=968 y=1456 dist=0 nom=4
                                   n=5 veh=23 x=968 y=1456 dist=21 nom=2
5203  [R3R] RESPACE-AFTER-EDIT couple head=48 px=10 stretch=1 visited=20 capped=0
```

- 三节（21 是 ★ 段头，22/23 是其 artic parts）**y 全为 1456** ⇒ 21↔22 重叠 5px、22↔23 重叠 4px。
- `COUPLE-OK` 的逐节 dump 同证：`CPL idx=21 ... gap=-5 nom=5`、`CPL idx=22 ... gap=-4 nom=4`（gap 为负 = 盒子重合）。
- respace 之前它们是 1466/1461/1457（正确），**respace 之后才塌** ⇒ 责任人指向 `R3RRespaceChainAfterEdit()`。
- 其余 5 个单元内部间距在 respace 后仍正确（5/5），**只有含 ★ 的第一个单元塌**。

**（A3）头部拼接缝本身是瞬时的，已被 respace 收拢（不算残留）：**

```
4717  SPLICE-GAP-REJECT COUPLE-FLIP-BOTH prev=50 x=968 y=1455 head=21 x=968 y=1466 exp=2 dist=11
4718  SPLICE-GAP-LAST-RESORT            ← 11px 缝被"最后手段"强行接受
```

随后 `px=10` 把 50(1454)→21(1456) 收到 2px（正确）。⇒ 真正残留的是 A1 的单元缝与 A2 的塌陷。

### B. "段数塌缩"这个口径**需玩家确认**（可能是第 158 轮记错了）

- `CHAIN-ATTRS head=48 n=21 SEG=2`；`R3RGetSegmentHeads` 只看到 1 个额外 ★（21），另一段是链头本身。
- 段构成 = `[48,49,50]` + `[21..8]`（18 节、含 6 个铰接单元）。
- 第 158 轮把"21 节 2 段"记成"段划分塌缩"；**但若这条车底本来就是「1 个机车组段 + 1 个 18 节车组段」，SEG=2 就是对的**——"6 个三节铰接单元"是 **artic 单元**而不是 R3R 段。
- ⇒ 请玩家确认：这条车底原本应是 **1 段** 还是 **6 段**？若 1 段，KI-253 的"塌缩"描述应撤回，只保留 A1/A2 的几何问题。

### C. `GEO` 里 `segid` 恒为 0 —— **探针时序，不能判"段 ID 缺失"**

- `GEO-*` dump 全部跑在 `R3RSettleChainSegments()` **之前**；段 ID 由 settle 里的 `R3RSyncSegmentIds()` 才分配（`R3R_SEGMENT_NONE = 0`）。
- 全日志最后一条 GEO 是 5202（settle 之前）⇒ 无法观测提交后的 segid。建议 settle 之后补一条带 `r3r_segment_id` 的 dump（§5 建议 2）。

### D. 折叠修正本轮走的是"候选 3 双翻 + LAST-RESORT 收尾"

```
FOLDCHK-ACCEPT = 2   SPLICE-GAP-REJECT = 3   SPLICE-GAP-LAST-RESORT = 1
A3-FLIP-V = 2        A2-ROLLBACK = 2         A1-ROLLBACK-DONE = 0
```

`COUPLE-FLIP-U` 的 `FOLDCHK` 报 `worst_gap=20`，worst pair 是 `idx=20 ↔ idx=15`（就是 A1 的单元缝）；`COUPLE-FLIP-V` 报 `worst_gap=69`（`idx=50 ↔ idx=6`、`dist=71`）。⇒ 折叠判据一直在把 **A1 那组 20px 单元缝**当"假折叠"来源，与 KI-253 是同一件事。

### E. 未定性：末段 152 次 `COUPLE-FAIL`

- 末尾形态：`CPL-SKIP veh=24 tile=20,9 orderType=16 retryIn=8`、`COUPLE-FAIL loco=24 order=16 tx=20 ty=9`、`COUPLE-FAIL loco=27 order=16 tx=58 ty=49`。
- `order=16` = `OT_GOTO_COUPLE`；`tx/ty` 打的是**自身格**；`TRP veh=24 mf=20,9 ... spd=0 fp=0` ⇒ 机车静止在库里等目标。
- 与第 148 轮的"候选车底尚未到位（`found=0`）"正常形态一致 ⇒ **暂判为正常等待，不是卡死**；但日志到此结束，需玩家确认末段是不是"一直挂不上"。
- 另：`SVC-DEPOT-SKIP veh=0 cur=17 real=3(17) tag=couple-protocol` 是 KI-220 的自动回库守卫（`cur=17` = `OT_WAIT_COUPLE`），正常。

### F. 零命中清单（探针在 exe 里，但本场景确实没打印）

`SEGFRONT-RESYNC*`（§2.4 已解释）、`SEGTRAIT-RECOVER`、`SEGROW-RECONCILE`、`SEGID-SYNC`、`UNIT-FREEZE`、`RESV-AUDIT-STRAY`、`A1-ROLLBACK-DONE`、`CRASH`、`ASSERT`。
`UNIT-FREEZE` 未出现 ⇒ 本会话没有走到"号位被别的活车占用"的兜底路径。
健康项：`DB-NORMALISE = 2`（KI-217 的混合 DrivingBackwards 归一化守卫生效）、`CHAIN-ATTRS2 = 5`（head=48 `len=80` > 0 ⇒ 第 158 轮"链长为 0"的怀疑被排除，注意 `CHAIN-ATTRS` 会打在 `ConsistChanged()` 之前、不能当结论，`CHAIN-ATTRS2` 才是提交后的数）。
`CGRP-NORM-UNION = 16` 但**掩码全 0**（本场景无挂接分组）⇒ 第 153/155 轮的分组并集判据在本场景**不可观测**，需另做带分组的场景。

## 4. 需要玩家拍板 / 补测的动作

1. **KI-253 的段口径**：那条 21 节车底原本应是 **1 段**还是 **6 段**？（决定 KI-253 的"塌缩"描述是否撤回）
2. **补一次读档**：存档 → 退出 → 读档，用来点亮 KI-263 判据 2（`DEPOT-XFER-ID load`）和 KI-261 判据 3（`name_backup` 经 R3VP 往返）。
3. **KI-262 目测 4 条**：站场标题、解挂/挂接下拉框标签、订单窗口宽度、其它 plane。
4. **末段 152 次 `COUPLE-FAIL`** 是否属于"有车底一直挂不上"？
5. 若可行，给一条**带挂接分组**的场景，用来验 `CGRP-NORM-UNION` 的并集语义。

## 5. 下一步取数建议（3 条，按性价比排序）

1. **`R3RRespaceChainAfterEdit()` 补"入/出成对 dump"**：入参快照 + 出参快照各一次整链 `x/y/clen/artic/★`。当前只有出参（`GEO respace-after`），A2 的塌陷因此只能"对比 GEO-BEFORE"间接看出，无法定位到具体哪一步写的坐标。
2. **settle 之后补一条带 `r3r_segment_id` / ★⊗ / `IsControlSegment` 的 dump**（现在 `segid` 只能看到 settle 之前的 0）。
3. **把"铰接单元边界"的 nominal 口径弄清楚**：`nom` 现在 = `CalcNextVehicleOffset()`（`cached_veh_length` 口径，单元间名义 2px），实测 21-22px。建议 dump 里同时打印"真实的单元长度/相邻单元真实间隙"，先定 21-22px 是 bug 还是 `cached_veh_length` 口径不对。

---

## 附：本报告用到的探针行号索引

| 行号 | 内容 |
|---|---|
| 165 / 167 / 170 / 172 | KI-263 判据 1（`UNIT-PARK` / `DEPOT-XFER-ID depot-edit` / `CTRL-PARK` / `INVAR-CTRL depot-edit`） |
| 336 / 695 / 1073 / 1330 / 2729 / 3468 / 5265 | 控制段不变式 8 条 |
| 5204 / 3407 | `NAME-XFER` 名称借 |
| 733 / 1368 / 3062 | `NAME-RESTORE` 名称还 |
| 5160-5175 | `GEO-BEFORE couple head=48`（respace 之前，21/22/23 = 1466/1461/1457） |
| 5176-5198 | `GEO respace-after head=48`（respace 之后，21/22/23 = 1456/1456/1456） |
| 5202-5205 | `GEO-END` / `RESPACE-AFTER-EDIT px=10` / `NAME-XFER head=48` / `COUPLE-OK loco=48` |
| 4717-4718 | `SPLICE-GAP-REJECT dist=11` → `SPLICE-GAP-LAST-RESORT` |

---

## 6. 玩家测试执行清单（除「把日志给我」以外还要做什么）

> 一句话总纲：**日志是自动的、能自证的；缺的从来不是日志，而是「界面上的文字」和「你心里的原始意图」。**
> 所以除日志以外，你要补的是 6.2~6.4 的**文字答复**与 6.5 的**存档**。

### 6.0 开测前 3 件事（不做会导致日志判错）

1. **核对 exe**：应为 `build\openttd.exe`，51 536 896 B / 2026-09-29 22:32:15（第 161+162 轮构建）。若你手上是别的版本，先说一声，否则我按错基线判。
2. **先把现有 `build\R3R_debug.log` 改名备份**（例如 `R3R_debug_prev163.log`）再开测 ⇒ 新一轮日志只含本轮，我能与第 163 轮逐行对齐。不备份的话新旧事件混在一起，我无法判「是否又复现」。
3. **沿用第 163 轮同一存档、同一条 21 节车底场景**，别换档、别中途手动改车底。理由：本轮全部硬数据（`GEO` / `FOLDCHK` / `CHAIN-ATTRS`）只有与上一份同场景对比才有意义。

### 6.1 必须做的 5 个动作（按序，一次会话可跑完）

| # | 动作 | 做完请用一句话告诉我 |
|---|---|---|
| 1 | 目测 KI-262 四条（见 6.2） | 哪些正常、哪些仍是坏的 |
| 2 | 车库拖动：把车底拖到某列车**前面** | 目标列车的车号/名称是否变成车底那一套 |
| 3 | **存档 → 退出到主菜单 → 读档**（能整个退出游戏再重进更好，可同时验 R3VP） | 读档后车号/名称是否仍是那套、有没有变回自己原来的 |
| 4 | 挂上车底 → 存档 → 读档 → **解挂** | 解挂后**车底**是否拿回车底自己的名字（而不是机车名）、机车是否拿回自己的名字 |
| 5 | 观察铰接单元接缝与整体图像（见 6.3） | 三选一的定性答复 |

### 6.2 KI-262 目测四条（日志**完全**看不到，只能靠眼睛）

1. 车站窗口的**站场标题栏**：显示 `站场 N` 这类正常文本，**不是** `(invalid parameter)`。
2. 订单窗口 → 解挂订单的「前半/后半排程」下拉框：显示中文标签，**不是** `{STRING}` 或空白。
3. 挂接订单的「挂接侧」下拉框：同上。
4. 订单窗口**整体宽度**：不再被撑得过分宽（应回到约 300px 那一档，而不是 600px）。

### 6.3 观感两条 + 拍板一条（决定我要不要动几何代码）

- **KI-264 单元缝**：6 个三节铰接单元**之间**，肉眼能不能看到约 20px 的缝/「断开感」？三选一：
  ① 明显有缝　② 没有缝、是我口径算错　③ 有缝但在单元**内部**而不是单元之间。
- **KI-253 塌陷**：含 ★ 的第一个单元（靠机车那头）三节是否**重叠挤在一起**？
- **★最重要的一条拍板**：那条 21 节车底，**设计上原本应该是 1 段还是 6 段？**（决定第 158 轮"段划分塌缩"的描述是否撤回）

### 6.4 另外两条

- 末段 152 次 `COUPLE-FAIL`：是「确实有车底停在旁边、就是一直挂不上」，还是「那几台机车正常在等」？
- 若能给一条**带挂接分组**的场景（两个段各自加进不同挂接分组再挂），用来验 `CGRP-NORM-UNION` 的并集语义（本轮掩码全 0、不可观测）。

### 6.5 交给我时给什么（除日志）

1. `build\R3R_debug.log` —— **原样**，别删别截断；若又刷到 GB 级，先告诉我（我有截断先例，会保头部+样本）。
2. 6.1~6.3 的**文字答复**。记不住顺序没关系，按「操作 → 期望 → 实际」三行写即可。
3. 相关存档 `.sav`（读档类问题请给**读档前**那一份）。
4. 若崩溃：`crash-*.log`（通常在 exe 同目录或 `Documents\OpenTTD`），并说明崩溃前最后一步操作。
5. 截图：**可选、优先级最低**。我没有视觉能力，读 png 要跑 OCR（小字号数字误识率高）；只有「文字说不清」时才需要。

### 6.6 千万别做

- 别中途换 exe（换了日志就不是同一构建的，判据全废）。
- 别一边测一边手动编辑车底/段结构（会毁掉与第 163 轮的对比基线）。
- 别清空或删除日志（**没有日志等于没测**）。
| 5258 / 5265 / 5266 | `CHAIN-ATTRS SEG=2` / `INVAR-CTRL ctrl=21` / `CHAIN-ATTRS2 len=80` |
