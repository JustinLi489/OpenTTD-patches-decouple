# 第 300 轮：玩家三条新报告（2026-10-25）

> 铁律执行：收到报告**第一时间**落盘（本文件 + `R3R_KNOWN_ISSUES.md` 的 KI-474/475/476），
> 之后才深挖。本轮先落盘骨架与已取锚点，后续每一组取证结果立刻追加。

## 〇、玩家原话（逐条保留）

> 「奇怪，这次又复现不出来那个预留截不住了，我倒是发现了一些新问题：
> 1，如日志，你应该能发现列车在第二轮运行的时候挂不上车了，于是直接跳过了挂接的命令
> （具体情况可以细说，但是我不想先解决这个）。
> 2，你知道的，上一轮我说复制列车的时候一号二号后面是五号六号。
> 3，复制的列车，在公司载具列表里面的显示有些问题」

拆成三条：

| 编号 | 内容 | 玩家态度 |
|---|---|---|
| P1 | 第二轮运行时挂不上车 ⇒ **直接跳过挂接命令** | **先不解决**（只登记，可细说） |
| P2 | 复制列车的车号：一号、二号之后直接是**五号、六号** | 待修/待解释 |
| P3 | 复制出来的列车在**公司载具列表**里显示有问题 | 待修/待取证 |

**重要前提**：这次**没有复现**"预留停不到路点"（KI-473 现象）。⇒ KI-473 的复现条件仍未知，
不得据此宣布已修/未修。

## 一、输入校验（采样有效性）

| 项 | 读数 |
|---|---|
| 日志 | `build\srv\R3R_debug.log`，1 019 041 B / **11 492 行** / mtime 2026-10-25 **11:57:37** |
| 产生者 | `build\srv\openttd.exe` 52 326 400 B / **11:37:59** |
| 结论 | 日志由**第 298 轮探针版 exe** 产生 ⇒ 不含第 299 轮修法甲的新探针（`ORD-RETURN-OWN`/`ORD-BORROW-PARK` 全日志 0 条，属预期，**不是**修复失效的证据） |

坑：`ripgrep`（search_content 工具）对这份日志**恒返回 0 条**——日志混有 GBK 中文名（如
`JRW_鍏宠タ鏈哄満蹇€?01`），rg 判为二进制直接跳过。**必须用 `findstr /n` 或 `Select-String`**。

## 二、P2 已取锚点（克隆与车号）

日志 `build\srv\R3R_debug.log`：

- `:369  CLONE-MARKERS src=21 dst=51 cars=24 srcSF=1 dstSF=1 srcSB=1 dstSB=1 diff=0`
  ⇒ 源链 `head=21`（24 节），克隆品 `head=51`，段标记 ★/⊗ 一致复制成功。
- `:370-394  CLONE-SRC veh=21..47`（24 行）：`ov=65535`（源链 override 无效），
  角色位 `AH/AM` 成对（`eng=623` 三节一组 + `eng=622` 三节一组 ×6 + `eng=620` 三节一组）。
- `:397-409  CLONE-DST veh=51..`（24 行）：`ov=18/17/17/16...`（打散铰接的烘焙值），
  链序 `51→52→53→54→…` 与源链逐节对应。

**P2 待取证（下一组立即补）**：
- 克隆品的 `unitnumber` 实际取值序列 —— 玩家口述"1、2 之后是 5、6"意味着
  **号池分配被跳号/被别的车占用**；
- 怀疑点 A：克隆走 `CmdBuildRailVehicle`/`CmdBuildRailWagon` 逐节建车，每节都会向号池领号，
  但克隆结束后 `CloneVehicleName()` 只处理链头名字，**车号（unitnumber）未按源链逐节照搬**
  ⇒ 中间节的临时号被丢弃、留下的号出现空洞（"跳号"）。
- 怀疑点 B：`R3RRestoreUnitNumber()` / `R3RUnitNumberParkedByOther()` 的
  "号停在别人 backup 里"判据在克隆期把刚领的号又放回池子里，产生 1,2,5,6 的形态。

## 三、P3 已取疑点（载具列表显示）

- 已有探针 `CLONE-TRAIT`（第 225 轮）与 `SEGTR-SNAP`（段行快照）是唯一能判"列表里显示什么"的只读读数。
- 猜测根因（待验证）：克隆品 `:397` 之后虽有 24 节、`dstSF=1 dstSB=1`（只有 **1 段**），
  而源链同样是单段 ⇒ 若载具列表显示异常是"段名/段号/单位号取错行"，
  则应命中 `R3RSegmentSectionName/UnitNumber` 一族的回退口径（KI-254/KI-263 遗留），
  而**不是**克隆丢段。
- 待玩家补充：具体异常形态（重复出现？名字空白？号不连续？多出一行？图例错？）

## 四、P1 已取疑点（挂接被跳过）

- 全日志 `CLONE` 命中集中在 369-409；`COUPLE-FAIL`/`COUPLE-DEST-EMPTY`/`ARTIC-SKIP-R3R`/`CPL-GATE`/`CPL-REFUSE`
  在本轮 `findstr` 首批 40 条里**未出现** ⇒ 尚未定位锚点，需后续单独 grep。
- 猜测根因（待验证）：`R3RSkipCoupleOrdersForRealArtic()`（KI-274）对
  `WAIT_COUPLE` 一视同仁跳过 ⇒ 表现为"挂不上就直接跳过挂接命令"。
  若日志出现 `ARTIC-SKIP-R3R ... type_from=17`，则该猜测成立。
- 玩家已明确**本轮不修**，只登记。

## 五、待办（按玩家优先级）

1. 先把 P1 锚点抓齐（只为登记，不改码）。
2. P2 取证 + 解释"1,2,5,6"的号池路径。
3. P3 取证：需要玩家描述具体显示异常。

---

## 六、取证 1（2026-10-25 12:2x）：P2/P3 的**直接读数已拿到**

### 6.1 P2 结构性证据 —— 克隆品链头车号 = 5，源链 = 1

`build\srv\R3R_debug.log`（读档后第一条段行快照，源链 head=21）：

```
:4   SEGTR-SNAP tag=load-raw head=21 nseg=1 ctrl=21 borrowed=0
:5   SEGTR-SNAP   i=1 id=0 front=21 star=1 ctrl=1 | live u=1 n="JRW_关西机场快速 01" gid=3 | bk u=0 n="" gbk=-1 | row none u=0 n="" g=-1 | auto n=0 first=[0,0)
```

第一次克隆提交后（`:449` 附近）：

```
SEGTR-SNAP tag=clone head=51 nseg=1 ctrl=51 borrowed=0
SEGTR-SNAP   i=1 id=0 front=51 star=1 ctrl=1 | live u=5 n="" gid=3 | bk u=0 n="" gbk=-1 | row none u=0 n="" g=-1 | auto n=0 first=[0,0)
```

**硬结论 1（P2）**：源列车活车号 `u=1`，克隆品活车号 **`u=5`** ⇒ 玩家口述"一号二号后面是五号六号"
是**实测事实**，不是显示问题：3、4 两个号在克隆过程中被消耗掉了。

四次克隆的锚点（`:453 / :839 / :1225 / :1608`，`CLONE-TRAITS dst=51/75/99/123`）⇒ 四个克隆品链头
间隔 24 节，与 24 节的源链一致。

**猜测根因 P2（待验证）**：克隆走 `CmdBuildRailVehicle`/`CmdBuildRailWagon` **逐节建车**，
每节都向 `unitnumber` 号池领号（24 节就领 24 个号），而 R3R 只让链头保留号、非链头节的号被清零，
**清零时若走的是"直接写 0"而不是 `Vehicle::FreeUnitNumber()`，号就永远从池子里消失** ⇒
后续克隆从 5、6 开始，1、2 之后出现 3、4 的空洞。

### 6.2 P3 结构性证据 —— 克隆品链头**名字是空串**

同一条段行快照：

```
CLONE-TRAIT seg=0 src=21 dst=51 prio=1->1 grp=3->3 cgrp=0x8->0x8 name="JRW_关西机场快速 01"->""
```

**硬结论 2（P3）**：克隆品的 `prio`、`grp`、`cgrp` 都正确复制，**唯独名字 `dst=""`**。
且 `row none` ⇒ 克隆品链头没有段行，名字只能落在活字段 `v->name`。

这与第 225 轮 KI-332 的设计意图直接冲突：`R3RCopyClonedSegmentTraits` 对**链头段刻意不搬名**，
把赋名交给 `CloneVehicleName()`；而 `CloneVehicleName()` 显然没给克隆品赋上名。

**猜测根因 P3（待验证，两条并存）**：
- 猜想 A：`CloneVehicleName()`（或它调用的 `CmdBuildRailVehicle` 的 `clone_name` 路径）
  读的是 `src->name` 的**共享/订单相关判据**，而 R3R 的段名可能在 `name_backup`/段行里 ⇒ 取到空。
- 猜想 B：克隆品确实被赋了名，但**随后被某个 R3R 提交点冲空**（例如 `R3RSettleChainSegments("clone")`
  第二次调用时按"段行优先"把活字段镜像成段行的空值）。

⇒ 两者可用同一探针区分：在 `CloneVehicleName()` 之后立刻打一次 `v->name`，再在 settle 之后打一次。

### 6.3 号池旁证（待验证）

`UNIT-PARK veh=72/96/120/144 bk=0`（`:293/:679/:1065/:1448`），间隔 24、`bk=0` 表示该车原本没有号。
`veh=72` 落在第一次克隆品链（51..74）内 ⇒ 需要确认这四行是"克隆过程中对**非链头节**做的号停放"，
若是，则非链头节的号被"停放"而不是"归还"，正好解释 3、4 的消失。

### 6.4 下一步（按顺序，未执行）

1. 读 `src\vehicle_cmd.cpp` 的 `CloneVehicle()` + `CloneVehicleName()`，确认名字与车号的赋值路径；
2. 读 `src\vehicle.cpp` 的 `unitnumber` 清零点，区分"直接置 0"与 `FreeUnitNumber()`；
3. 读 `R3RSettleChainSegments`/`R3RCopyClonedSegmentTraits` 的先后顺序，验证猜想 B。

---

## 七、取证 2（2026-10-25 续）：P2 根因**改判** —— 不是"克隆跳号"，是 3/4 号被"冻结"在链内车上

> 本节推翻 §6.1 的"猜测根因 P2（逐节领号）"，改为已实证的结论。

### 7.1 先证伪"逐节建车每节领号"

`src\vehicle_cmd.cpp` 的建车入口对 **Wagon 恒传 `unit_num = 0`**，只有非 Wagon
才 `GetFreeUnitNumber()`：

```
UnitID unit_num = (flags.Test(QueryCost) || flags.Test(AutoReplace) ||
        (type == VehicleType::Train && railveh_type == RailVehicleType::Wagon)) ? 0 : GetFreeUnitNumber(type);
```

日志侧完全吻合 —— **每次克隆只有一条 `UNIT-NEW`**，其余节全是 `id=0`：

```
:132  UNIT-NEW id=5 free_next=6
:518  UNIT-NEW id=6 free_next=7
:904  UNIT-NEW id=7 free_next=8
:1287 UNIT-NEW id=8 free_next=9
```

⇒ **一次克隆只领 1 个号**，§6.1 的猜想 A（24 节领 24 个号）**证伪**。

### 7.2 3、4 号从一开始就在存档里、且活字段是 0

读档最前面两行（`tag=load`，克隆还没发生）：

```
:2 SEGLOAD-PARK veh=45 unit=0 ubk=3 gid=3
:3 SEGLOAD-PARK veh=48 unit=0 ubk=4 gid=2
```

⇒ 3 号停在 `veh=45` 的 `unitnumber_backup`、4 号停在 `veh=48`；
两车的**活字段 `unitnumber` 都是 0**。`veh=45` 属链 21（`gid=3`）、`veh=48` 属链 24（`gid=2`），
二者都是链**中间**的一节（`POSCONSIST veh=45 ... before=21 after=2`）。

### 7.3 冻结是**有意设计**，不是 bug

`src\train_cmd.cpp:4137-4148`（第 144 轮 / 需求壹"fork 壹"）：链内车失去链头身份时，
把车号停进 `unitnumber_backup`，注释原话是"**保持占用池位**"，**刻意不 `ReleaseUnitNumber()`**，
等它重新成为链头时再取回（防"编号被抢走"）。

于是号池的 `NextID` 停在 **5**：`GetFreeUnitNumber()` 只会发 5、6、7…，3、4 永远发不出去。
（`free_next=5` 是第一次 `UNIT-NEW` 之前就已经是 5 —— 3、4 早已被 UseID。）

### 7.4 玩家为什么"看不到 3、4"

- 链 21 / 24 读档即 `nseg=1`（`SEGTR-SNAP tag=load-raw`，★ 只有一个）；
- `veh=45/48` 只是链中间一节、**不是段首** ⇒ `R3RSegmentTraitRow()` 不给它建行（`row none`）；
- 于是 3、4 号**没有任何界面出口**（不显示在链头行、也不显示在段子行），却实实在在占着号池。

### 7.5 硬结论（P2 定稿）

> 玩家看到的"1、2 之后直接 5、6"＝`3`、`4` 被**第 144 轮的冻结号机制**扣在两列车内部的
> `unitnumber_backup` 上；因为那两节车已经不是段首、没有段行，号在界面上完全隐身。
> **克隆本身没有跳号**，它只是如实领取了号池给出的下一个号（5）。

### 7.6 修法候选（**待玩家拍板，本轮尚未改码**）

| 代号 | 做法 | 收益 | 代价 |
|---|---|---|---|
| **甲** | 停放号时 `ReleaseUnitNumber()` 回池；取回时若号已被占则领新号 | 号连续（1、2 → 3、4） | 第 144 轮要防的"编号被抢走"会回来 |
| **乙** | 号继续冻结，但给"持有冻结号的链内车"在列表补一行/把号显示到所属段行 | 不动号池语义，3、4 可见 | 列表会多出非段行；要改 GUI 口径 |
| **丙** | 克隆时跳过冻结号、让克隆品"继承"源链内的冻结号 | 号看起来连续 | 语义混乱（克隆品并没有那两节车），不推荐 |

---

## 八、取证 2：P3「载具列表显示异常」—— 克隆品名字**其实有**，问题是耦合后被车底身份覆盖

### 8.1 `CLONE-TRAIT ... ->""` 是中间态，不是最终值

`CloneVehicle()` 的执行顺序（`src\vehicle_cmd.cpp`）：

```
锁步遍历 → ConsistChanged(CCF_ARRANGE)
  → R3RSettleClonedChain(w)          // 第 1 次 settle（日志 :450 / :828 的 SEGTR-SNAP tag=clone）
  → R3RCopyClonedSegmentTraits(v,w)  // 打印 CLONE-TRAIT，此处 dst->name 尚未赋值 ⇒ 空
  → R3RSettleClonedChain(w)          // 第 2 次
  → R3RDbgCloneTraits(v,w)           // CLONE-TRAITS
  ... (refit / CloneOrder)
  → CloneVehicleName(v_front, w_front)   // :2747 这里才赋名
  → R3RUniquifyClonedSegmentNames(w) + R3RSettleClonedChain(w)
```

⇒ `:450 SEGTR-SNAP ... live n=""` 与 `CLONE-TRAIT name="…"->""` 都打在赋名**之前**。

### 8.2 克隆品自己的号/名是**对的**

第一次耦合后的快照（`:8119-8121`）：

```
SEGTR-SNAP tag=couple head=51 nseg=2 ctrl=24 borrowed=1
  i=1 id=3 front=51 star=1 ctrl=0 | live u=2 n="JRW_纪州路快速 01" gid=3 | bk u=5 n="JRW_关西机场快速 02" | row use u=5 n="JRW_关西机场快速 02" g=3
  i=2 id=4 front=24 star=1 ctrl=1 | live u=0 n="JRW_纪州路快速 01" gid=2 | bk u=2 n="JRW_纪州路快速 01" | row use u=2 n="JRW_纪州路快速 01" g=2
```

- 克隆品自己的身份 = **5 号 / "JRW_关西机场快速 02"**，停在 `name_backup`/`unitnumber_backup`（正确，`CloneVehicleName` 赋名成功）；
- 但**链头活字段**被换成控制段 24 的 **2 号 / "JRW_纪州路快速 01"**（`borrowed=1`）；
- 而段行 `id=3` 里存的又是克隆品自己的（`row use u=5 n="…02"`）。

⇒ **同一列车的"主行显示"与"段行显示"是两套身份**，且主行显示的是**被挂车底**的号与名。

### 8.3 三个候选解释（等玩家一句话定性，见 Q-2）

- 候选 ①：列表主行显示成「纪州路快速 01 / 2 号」，与源车重名重号（克隆品的 5 号只在子行/备份里）；
- 候选 ②：克隆品名字正常，但**段子行/图例**不对（第 155 轮 KI-247 遗留：子行只在 `GB_NONE` 显示、组列表不按组过滤）；
- 候选 ③：号显示为 0 或空白（若行/活字段都没取到）。

---

## 九、待玩家回答（本轮问题清单，答复后直接按编号回填）

- **Q-1（P2 口径）**：号池要按 **甲（回池，允许被抢号）**、**乙（冻结但显示出来）**、还是 **丙（克隆时跳过冻结号）** 修？
- **Q-2（P3 定性）**：克隆列车在载具列表里具体是哪种异常 —— ①名字/号变成被挂车底的（重名重号）；②段子行或图例不对；③号为空/显示 0；④其它（请描述）。
- **Q-3（P3 场景）**：出现显示异常时，那列车是**已与车底耦合**（挂在一起）还是**单独停在车库**？

> 说明：P2/P3 都**尚未改码、尚未构建**；P1（KI-476）按玩家要求只登记。

---

## 十、玩家答复（2026-10-25，P2/P3 定性，原话保留）

### 10.1 P2 玩家口径

> 「3，4号车是按照耦合执行了号码冻结吗，但是它们当时还是非段啊，我期望不应该有号码冻结啊」

- 玩家确认：`veh=45/48` 冻结 3、4 号时**它们不是段**（读档 `nseg=1`，★ 只在链头 21/24 上）。
- **玩家期望 = 非段车不冻结号**（冻结机制只应对"段"生效）。
- ⇒ 修法方向确定（本轮据此改码）：`R3RParkUnitNumber` 加"该车是段头"判据；非段、或段身份被撤销（`R3RDestroyCarOnlyFormation`）时改为 `ReleaseUnitNumber()` 归还号池。

### 10.2 P3 玩家口径

> 「克隆列车之后，我注意到，载具列表里面，第一个是ctrl（以共享调度）克隆应该放入相同组，但是克隆的列车被放入了未分组列车。第二个是虽然全部列车里面显示有6列车，图像正常，但是未分组里面的四列克隆的列车不显示。当时，那些克隆车在车库里面，但是，无论我怎么启动它，或者让它耦合，或者怎么样，它们都没在载具列表里面显示，但是又参与了载具的计数」

拆成三条硬需求：

| 编号 | 现象 | 需求 |
|---|---|---|
| **P3-a** | 克隆品被放进「未分组」，源链在组 3（cgrp 也正确） | 克隆应**继承源链的组** |
| **P3-b** | 「全部列车」6 行正常、图像正常，但「未分组」组视图里 4 列克隆品**不显示** | 未分组视图必须列出它们 |
| **P3-c** | 启动/耦合后依旧不显示，却**参与载具计数** | 列表过滤与计数口径必须一致 |

### 10.3 日志侧对应读数（克隆完成瞬间 `tag=clone`）

```
:450 SEGTR-SNAP tag=clone head=51 nseg=1 ctrl=51 borrowed=0
:451   i=1 id=0 front=51 star=1 ctrl=1 | live u=5 n="" gid=65534 | bk u=0 n="" gbk=-1 | row none
```

- `gid=65534` = `DEFAULT_GROUP` ⇒ **直接对应 P3-a**；
- 对照源链 `:5 live u=1 n="JRW_关西机场快速 01" gid=3` ⇒ 源组 = 3，克隆品没继承；
- 但**解耦之后** `:3280 live u=5 n="JRW_关西机场快速 02" gid=3` / `:3006 head=99 ... gid=3` ⇒ 组与名又变成正确的 3。
  ⇒ 克隆品体内其实带着正确信息（`group_id_backup`/段行副本），只是**克隆完成时的活字段没被写上**。

### 10.4 待取证清单（对应 §10.2 三条）

- P3-a：`CloneVehicle()` 里 `AddVehicleToGroup()` 是否被 `settings.copy_clone_add_to_group` 门禁掉；
- P3-b/P3-c：`vehiclelist.cpp` 的 `VL_GROUP_LIST` 过滤用哪个 `GroupID` 常量、"未分组"视图查的是 `DEFAULT_GROUP`(0xFFFE) 还是 `GroupID::Invalid()`(0xFFFF)，以及子行/段行是否参与过滤。

---

## 十一、取证 4：P3-b/P3-c 根因坐实 —— 克隆裸写 `group_id` 绕过 `GroupStatistics`（2026-10-25）

> 结论先行：**P3-a / P3-b / P3-c 是同一个 bug 的三种表现**，不是三个独立问题。
> 克隆品"进了组 3"（成员字段正确）但**"未分组"的计数没减**（统计量没搬），于是
> 计数说它在未分组、列表说它不在 —— 玩家看到的就是"被放进未分组 / 未分组里不显示 / 却参与计数"。

### 11.1 两条口径（代码锚点）

| 口径 | 代码 | 读什么 |
|---|---|---|
| 列表过滤 | `src/vehiclelist.cpp:187` `GroupIsInGroup(R3RSegmentGroupID(v), group)` | 车/段行的 **`group_id` 成员字段** |
| 组树计数 | `src/group_cmd.cpp:1204 GetGroupNumVehicle()` → `GroupStatistics::Get()`（`:79`，`IsDefaultGroupID` 时读 `company->group_default[type]`） | **`num_vehicle` 统计量** |

### 11.2 `num_vehicle` 全工程只有两处增减

- `src/vehicle_cmd.cpp:241` 建车成功：`CountVehicle(v, +1)`；此刻 `group_id` 还是 `DEFAULT_GROUP`
  ⇒ 这一列车**先被记在"未分组"**；
- `src/group_cmd.cpp:776 AddVehicleToGroup()`：`CountVehicle(-1)`（旧组）→ `SetTrainGroupID()` → `CountVehicle(+1)`（新组）
  ⇒ 这是**唯一**把一列车从"未分组"搬到某组的配对动作。

**反例（关键）**：`SetTrainGroupID`(:1068)、`R3RSetSegmentGroupID`(:1119)、`UpdateTrainGroupID`(:1156)
全都只调 `UpdateNumEngineGroup()`（只维护 `num_engines`），**一个都不动 `num_vehicle`**。

### 11.3 克隆路径漏掉了那一次"搬家"

- `src/couple_group.cpp:1336`（`R3RCopyClonedSegmentTraits`）：`d->group_id = s->group_id;` —— **裸赋值**。
- 结果：克隆品 `group_id = 3`（列表口径正确），但 `group_default[Train].num_vehicle` 仍 **+1**（计数口径仍算未分组）。
- `vehicle_cmd.cpp:2680` 的 `AddVehicleToGroup` 即便执行也救不回来：它进来时 `v_front->group_id` 已经是 3，
  `CountVehicle(-1)` 扣的是 **组 3**、`CountVehicle(+1)` 加的还是 **组 3** ⇒ 未分组那 +1 永久残留。

### 11.4 与现场读数逐条对齐

| 玩家看到的 | 解释 |
|---|---|
| 「克隆被放入未分组」（P3-a） | 组树"未分组"节点计数 +4（2 原有 + 4 克隆），玩家按计数判定 |
| 「未分组里 4 列不显示」（P3-b） | 列表按 `group_id=3` 过滤 ⇒ 未分组视图里当然没有 |
| 「却参与了载具计数」（P3-c） | 即上面这条分叉本身 |
| 「全部列车 6 列、图像正常」 | `ALL_GROUP` 不做组过滤 |
| 日志 `:450 tag=clone head=51 live gid=65534` → 稍后 `gid=3` | 65534 只是 `DEFAULT_GROUP` 归一，不是 bug 本体；真 bug 是统计没跟上 |

### 11.5 P2 附带的正面结论（修法可行性）

`FreeUnitIDGenerator::NextID()`（`src/vehicle.cpp:3068`）取的是 **bitmap 里第一个空位**，不是单调自增。
⇒ 冻结一旦改成 `ReleaseID()` 回池，3、4 会立刻重新被发出去，P2 走"非段不冻结、回池"确实能恢复连续编号。

### 11.6 修法草案（**待拍板，本轮仍未改码**）

1. **P3（一处修三条）**：`R3RCopyClonedSegmentTraits` 写 `d->group_id` 时补统计配对 ——
   对每段链头依次 `CountVehicle(d, -1)` → 赋值 → `CountVehicle(d, +1)`；
   或更稳的做法：复制完先把克隆品 `group_id` 复位成 `DEFAULT_GROUP`，让 `vehicle_cmd.cpp:2680` 的原生
   `AddVehicleToGroup` 完成搬家（并把该调用从 `copy_clone_add_to_group` 开关下解绑，符合玩家"克隆应继承组"口径）。
2. **P2**：`src/train_cmd.cpp:4144` 冻结块加判据 —— 只有 `Train::From(src)->IsSegmentFront()`（★）才停放；
   否则走 `ReleaseUnitNumber(src)` 回池，并打 `UNIT-RELEASE veh= id=` 探针。
3. **顺带审计**：`couple_group.cpp` / `train_cmd.cpp` 里其余裸 `group_id =` 写入点是否同样漏统计
   （重点看 `R3RParkTrainGroupID` / `R3RRestoreTrainGroupID` 有没有绕开 `R3RSetSegmentGroupID`）。

---

## 十二、第 301 轮：P2 与修-1 落地（2026-10-25）

### 12.1 玩家拍板（原话）
- 「P2 按照你推荐的来修（也就是修复 2）」→ 冻结判据 + 非段回池；
- 「修复 1 也可以执行」→ 克隆分组统计配对；
- 「修复 3 是什么作用」→ 我答复：纯审计、不改行为；玩家未要求本轮执行。

### 12.2 P2 落地（仅 `src\train_cmd.cpp`）
1. `CmdMoveRailVehicle()` 的"src 失去链头身份"块（原 :4144，现 :4202-4214）由**无条件冻结**改为按 ★ 分流：
   - `src->IsSegmentFront()` 为真 ⇒ 旧行为（`unitnumber_backup = unitnumber; unitnumber = 0;`，探针 `UNIT-PARK`）；
   - 为假 ⇒ `src->ReleaseUnitNumber()` 回池，探针 `UNIT-RELEASE veh= id= reason=not-segment`。
2. 新增三个文件内 static（插在 `R3RUnitNumberUsedByOther()` 之后）：
   - `R3RSegmentHeadOwnsNumber(self,num)`：号是否仍被某个**段首**停在 backup 里（合法物主）；
   - `R3RReleaseParkedUnitNumber(v,why)`：回收前双重校验（无活车穿着 + 无段首持有），
     通过才 `freeunits[type].ReleaseID(parked)`；否则只丢记号、不动池位（`UNIT-DROP ...-owned-elsewhere`）；
   - `R3RReleaseStaleNonSegmentUnitNumbers(chain)`：逐链扫成员，非 ★ 且活号为 0 且有 backup ⇒ 回收。
3. `R3RRebuildCouplePriorities()` 入口（**只被 `AfterLoadVehiclesPhase2()` 调用** ⇒ 读档一次）补
   `R3RReleaseStaleNonSegmentUnitNumbers(chain)`。理由：现场 3、4 号在**读档那一刻**就已经在
   `unitnumber_backup`（`SEGLOAD-PARK veh=45 ubk=3` / `veh=48 ubk=4`），只修"以后不再冻结"治不了存量。

### 12.3 修-1 落地（仅 `src\couple_group.cpp`）
`R3RCopyClonedSegmentTraits()` 的裸赋值改成统计配对：

```cpp
if (d == dst_chain && d->IsPrimaryVehicle() && d->group_id != s->group_id) {
    const GroupID from = d->group_id;
    GroupStatistics::CountVehicle(d, -1);
    d->group_id = s->group_id;
    GroupStatistics::CountVehicle(d, 1);
    R3RDbgWrite("CLONE-TRAITS-GRP dst=%d from=%u to=%u\n", ...);
} else {
    d->group_id = s->group_id;
}
```

- 只对链头做（`num_vehicle` 是"每列车一个"口径），`from == to` 时净零、不动统计；
- 与 `vehicle_cmd.cpp` 的 `AddVehicleToGroup(v_front->group_id, ...)` 不冲突：那条路径进来时克隆品
  `group_id` 已是目标组 ⇒ 它自己的 `-1` / `+1` 净零。

### 12.4 修复-3 审计结论（读码得出，**本轮未改行为**）
- `R3RParkTrainGroupID(src)` → `SetTrainGroupID(src, DEFAULT_GROUP)`（:4244 一带）→ 稍后
  `NormaliseTrainHead()` → `R3RRestoreTrainGroupID(src)` 取回。`SetTrainGroupID` / `R3RSetSegmentGroupID`
  都**只动 `num_engines`**，`num_vehicle` 全程没变 ⇒ 只要"取回"一定发生，成员字段回到原组、统计也从未
  偏离，**不是 bug**。
- 残留**疑点**（记为待验证，不修）：若某条路径只 `SetTrainGroupID(DEFAULT_GROUP)` 而没有配对的
  `R3RRestoreTrainGroupID`（例如 `NormaliseTrainHead` 提前返回），就会留下"成员=未分组、统计=原组"的分叉。
- **重要边界（必须告知玩家）**：修-1 只保证**新克隆**不再造成分叉；现场档里那 4 个幽灵计数
  （未分组 +4）是存档里的 `num_vehicle` 存量值，读档不会自愈（`GRP-RESTORE-SEG-LOAD` 走
  `R3RSetSegmentGroupID`，同样只动 `num_engines`）。要让旧档计数归零，需要一次"按 `group_id`
  重建统计"的对账（未实现，登记待办）。

### 12.5 构建自证（2026-10-25 13:51）
- 复用既有 `_tmp_inc_build.cmd`（**未新建任何 .cmd**）；启动前确认 openttd.exe 未运行；
- 只改两个 `.cpp`（`train_cmd.cpp` / `couple_group.cpp`），未碰 `src\*.h` ⇒ 增量合法；
- 日志尾部 `[4/4] Linking CXX executable openttd.exe`；
- `build\R3R_incbuild.done` = `EXIT_CODE=0`（13:51:52）；
- `build\openttd.exe` @ 2026-10-25 13:51:48（52 326 400 B）；
- `train_cmd.cpp.obj` @ 13:49:25 / `couple_group.cpp.obj` @ 13:49:23，均晚于源码
  （`train_cmd.cpp` 13:47:51、`couple_group.cpp` 13:47:47）；
- exe 字面量 3/3 命中：`CLONE-TRAITS-GRP`、`UNIT-RELEASE veh=`、`UNIT-DROP veh=`；
- `read_lints` 两文件 0 条。
