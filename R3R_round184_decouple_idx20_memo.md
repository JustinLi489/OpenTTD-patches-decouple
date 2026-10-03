# 第 184 轮（2026-10-01 第三轮）临时分析报告：`idx20 DECOUPLE` 应当触发（解出三节假铰接真引擎 veh27）

> 承接：第 183 轮 KI-281（`R3R_KNOWN_ISSUES.md` 末尾小节）+ 备忘 `R3R_round183_name_spread_and_four_r3r_memo.md`
> 本轮按用户 2026-10-01 追加的优先级规则：**先写备忘（临时报告 + KI），再继续取证与改码**。

---

## 一、玩家口径（原话）

> 四条中的第三条应该由veh6解下一个三节假铰接式真引擎也就是veh27，这是应该触发的

拆成三条可执行判据：

1. **"应该触发"** ⇒ `idx20 DECOUPLE` 必须被执行（不是"承认不可触发只留痕"，也不是静默跳过）。
2. **"由 veh6"** ⇒ 执行它的排程主人是 `veh6`（29 条表 `owner=6`，`veh6` 所在段是控制段）。
3. **"解下一个三节假铰接式真引擎，也就是 veh27"** ⇒ 解出的那一半是 `veh27` 所在的**三节**单元 `[27,28,29]`，形态为"假铰接 + 含真引擎"。

---

## 二、现场

- 日志 `build\R3R_debug.log`：13 416 行 / 846 984 B，mtime 2026-10-01 13:23:27。
- 产生它的 exe：`build\openttd.exe` 2026-10-01 12:40:19（= 第 182 轮 KI-279 构建）⇒ 现场有效，不含 KI-280/KI-281 任何改动。

---

## 三、已确证事实

### 3.1 订单表（29 条，`owner=6`）

`4551 ORD-XFER keep veh=27 owner=6 cur_real=5 parked_real=9`、`4583 ORD-AFTER-COUPLE head=27 n=29 … owner=6`；`4584-4612` 逐条：

| idx | 类型 | 参数 |
| --- | --- | --- |
| 16 | `OT_WAIT_COUPLE`(17) | — |
| **17** | `OT_GOTO_STATION`(1) | station **5** |
| **18** | `OT_DECOUPLE`(15) | — |
| **19** | `OT_WAIT_COUPLE`(17) | — |
| **20** | `OT_DECOUPLE`(15) | — |
| **21** | `OT_WAIT_COUPLE`(17) | — |
| **22** | `OT_GOTO_STATION`(1) | station **2** |

⇒ 玩家口中的"连续四条 R3R 命令" = `idx18..idx21`（DEC/WAIT/DEC/WAIT），**第三条 = `idx20`**。

### 3.2 合并链的三段结构（耦合提交后）

- `4553 COUPLE-SKIP-DECOUPLE head=27 stepped=1 real=21 type=17`
- `4554 COUPLE-OK loco=27 rear=23 consist=23 co=1 real=21 type=17 tx=58 ty=26`
- `4555-4581 CPL idx=` 逐节 dump ⇒ `[27,28,29] | [0,1,2,3,4,5] | [6..23]`
- `4614 CHAIN-ATTRS head=27 n=27 FE=2 SEG=3`
- `4623 INVAR-CTRL tag=couple head=27 nseg=3 ctrl=6 ctrl_pri=1 head_pri=3 borrowed=1`
  ⇒ 链头段 = `veh27` 段（`segid=1`，`pow=11831`，全链唯一有动力段）、**控制段 = `veh6` 段（`segid=2`）**，两者不同 = 第 152 轮 P1-甲 的对抗面。

### 3.3 第一轮已经把 `idx18` 用掉

- `3251 DECOUPLE-FIRE consist=29 real=17 mode=2 num=1 segs=1 eff=23`
- `3283 DECOUPLE-DONE u=23 co=1 real=19`
  ⇒ 第一轮解出的是**尾段**（`eff=23`），留下列车走到 `idx19 WAIT_COUPLE` 并停在同一站等待；本轮机车 `27` 挂上来后才产生 `idx19 → idx20` 的推进。

### 3.4 静默跳过的落点

- `4583 ORD-AFTER-COUPLE head=27 n=29 real=21 impl=21 tt=21 co=0 dest=4294967295 borrowed=1 owner=6 u_has_orders=0`
  ⇒ 合并列车**直接落在 `idx21 WAIT_COUPLE`**，`idx20 DECOUPLE` 被 `R3RSkipUnfireableDecoupleOrder()` 吃掉（这正是第 183 轮 KI-278 那次修复的副作用面：为解"耦合后停在不可执行 DECOUPLE 上卡死"，改成一律跳过）。

---

## 四、两个缺口（根因推断，待下一轮定稿）

### 缺口 A —— 触发面：`at_order_dest` 覆盖不到"同一停靠点的第二条 DECOUPLE"

`TrainLocoHandler` 的停稳分支（`train_cmd.cpp` ~16363-16402）里：

- `at_order_dest` 只在 `cur_real` 为 `OT_GOTO_STATION` / `OT_GOTO_DEPOT` / 库内 `OT_DECOUPLE` 三类时有取值分支；
- `cur_real` = `OT_WAIT_COUPLE` 时**没有任何分支**，`at_order_dest` 恒 `false`。

⇒ 两条后果：① 合并提交点把索引放到 `idx21 WAIT_COUPLE` 时，闸门不可能识别出"列车其实正停在 `idx17 GOTO 5` 的终点"；② 即使把索引留在 `idx20`，只要 `cur_real` 不是上面三类，闸门同样恒假。

⇒ 要满足玩家口径，锚点必须从"只看 `cur_real`"扩成"**回溯最近一条已完成的旅行命令（GOTO_STATION/DEPOT）的终点站**"，即允许"同一个停靠点上连续多条 DECOUPLE"依次触发（`idx18` 用 `idx17` 的锚点，`idx20` 复用同一个锚点）。

### 缺口 B —— 解出边界：`GetDecoupleVehicle()` 只能切"尾部"

- `GetDecoupleVehicle()`（`:6384`）返回的是**尾部切点**：`GetSegmentHeadFromRear()`（TailSegments）/ `GetSegmentBoundaryFromHead()`（HeadBoundary）；
- 玩家指认要解出的是**链头段** `veh27` `[27,28,29]`（`segid=1`），而"解出链头段"在现有实现里只能表达成"**保留链头段、解掉它后面的全部**"（即 `HeadBoundary num=1`：边界在段1与段2之间 ⇒ 释放 `[0..5]`+`[6..23]`），这与玩家字面"解下 veh27"在**归属方向**上相反。
- ⇒ 需要玩家/设计侧再定一句口径：`idx20` 到底**解出 `[27,28,29]`**（则留下的是两段车底）还是**解出 `[27,28,29]` 之后的部分**（则 `veh27` 段留下）。本轮先把两种解释都写清，避免改错方向。

> 补充旁证（支持"解出的就是三节单元"这一读法）：第一轮 `idx18` 已解出过尾段（`eff=23`），玩家对第一轮无异议；`idx20` 若再解尾段，解出的会是 `[0..5]` 或 `[6..23]`（6 节 / 18 节，都不是"三节"）。**只有把 `[27,28,29]` 作为解出对象，"三节"才成立** ⇒ 缺口 B 里第一种解释（解出 `[27,28,29]`）更贴合玩家原话。

---

## 五、未确认项

1. `idx20` 的解出边界与**排程归属**：解出后谁拿 29 条表继续跑（第 152 轮 P1-甲 / KI-201 附记 3 同族问题）。
2. 现场 `veh=30`（`nord=29` 却永不推进，`SKIP-STOPPED … nord=29`）与本节 29 条表是否为同一张表的另一半 —— **未确认**。
3. `idx20` 触发后 `idx21 WAIT_COUPLE` 归哪一半（两条 R3R 命令不能抢同一半）。

---

## 六、复测判据（草案）

1. 同一场景出现 `DECOUPLE-FIRE … real=20`（不再只有 `idx18` 那一条）；
2. 解出链的链头 = `27`，且三节 `segid` 完整不裂（`segid` 与 ★/⊗ 一致）；
3. `idx19` / `idx21` 两个 `WAIT_COUPLE` 各归各自的一半，不出现同一半被两条命令争用；
4. 健康场景（单条 DECOUPLE、库里解挂、无 WAIT_COUPLE 夹层）零回归。
