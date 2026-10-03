# R3R 临时分析报告 · 第 202 轮：命令级「临时挂接分组」对同公司挂接完全失效

- 日期：2026-10-03
- 现场日志：`build\R3R_debug.log`（行 1–1600+ 已读）
- 对照二进制：`build\openttd.exe`（2026-10-02 22:17:08，51 607 552 B，含第 201 轮 KI-304/KI-305）
- 玩家原话：「我们在尝试使用**临时挂接分组**的时候遇到了一些问题」
- 台账条目：**KI-306**（本轮新增）
- 涉及文件：`src\couple_group.cpp`（唯一需要改的文件，**未碰任何 `src\*.h`**）

---

## 一、需求

让「临时挂接分组」（订单窗口里给「前往挂接 / 等待挂接」命令设置的那个分组属性）**真的起作用**。

它的自述语义写在 `src\lang\simplified_chinese.txt:4775`：

```
STR_ORDER_COUPLE_TEMP_GROUP_TOOLTIP :执行这条命令期间临时加入的挂接分组：除了本段本来就有的分组，
                                      再额外拥有该分组，因此可以挂上属于它的车底；挂接成功后这个临时身份立即消失
```

即：**命令期间给这一列临时加一个"真分组身份"，用来限定/放行它能挂上的车底**。玩家实测：设了与没设完全一样。

---

## 二、现场证据（日志锚点原文）

### 2.1 玩家确实操作了「临时挂接分组」窗口，并建立了 4 个分组（但都没放车段）

```
586:[R3R] CG-SHOW  req owner=0 valid=1 local=0
588:[R3R] CG-RBG   owner=0 company=0 iterated=0 visible=0 sel=-1
589:[R3R] CG-INIT  owner=0 owner_valid=1 company=0 company_valid=1 groups=0
590:[R3R] CG-PAINT ... can_manage=0 groups=0 segments=0 sel=-1 add_disabled=1
591:[R3R] CG-RBG   owner=0 company=0 iterated=1 visible=1 sel=0
592:[R3R] CG-PAINT ... can_manage=1 groups=1 segments=0 sel=0  add_disabled=0
594:[R3R] CG-SHOW  req owner=0 valid=1 local=0
596:[R3R] CG-INIT  ... groups=1
599:[R3R] CG-RBG   owner=0 company=0 iterated=2 visible=2 sel=1
600:[R3R] CG-PAINT ... groups=2 segments=0 sel=1 add_disabled=0
601:[R3R] CG-RBG   owner=0 company=0 iterated=3 visible=3 sel=2
602:[R3R] CG-PAINT ... groups=3 segments=0 sel=2 add_disabled=0
603:[R3R] CG-RBG   owner=0 company=0 iterated=4 visible=4 sel=3
604:[R3R] CG-PAINT ... groups=4 segments=0 sel=3 add_disabled=0
```

读法：
- `groups=0 → 4`：连续建了 4 个挂接分组（就是玩家准备用来当「临时挂接分组」的那些名字）。
- 每一行 `segments=0`：**没有任何车段被放进这些组**（"添加段"按钮一直是可用状态 `add_disabled=0`，但全日志没有一条 `CG-CLICK-ADD`，说明玩家没点它）。
- 所以这是一次**纯粹针对"临时挂接分组"的测试**：只需要若干个有名字的空组，好在订单窗口的下拉框里选。
- 订单窗口侧也是通的：`order_gui.cpp:3651-3656` 的 `WID_O_COUPLE_TEMP_GROUP` → `CoupleTempGroupDropDownList()`（`order_gui.cpp:869-905`）按 `R3RCoupleGroupIsVisibleTo()` 列出全部可见组，UI 不缺组、不缺入口。

⇒ **"窗口/下拉框"这一层没有问题；问题必然落在"选中之后有没有生效"这一层。**

### 2.2 设了（或没设）临时分组，机车的行为完全一样：锁死在车库里那列车底上

```
667:[R3R] CPL-PAIR act=54 tgt=0 dist=0  actTile=1,15  tgtTile=1,15
795:[R3R] CPL-PAIR act=48 tgt=0 dist=46 actTile=42,10 tgtTile=1,15
801:[R3R] CPL-PAIR act=54 tgt=0 dist=0  actTile=1,15  tgtTile=1,15
840:[R3R] CPL-PAIR act=48 tgt=0 dist=46 actTile=42,10 tgtTile=1,15
...（48 / 54 两台机车交替刷同一行，直到日志末尾）
```

读法：
- `CPL-PAIR` = KI-182 的一对一配对锁（`R3RCouplePairMatches()`，`couple_group.cpp:421-428`），即"机车已经把某列车底锁定为挂接目标"。
- `tgt=0` 是那条 **27 节、执行 `WAIT_COUPLE`、停在车库 1,15 的车底**（`NOABSORB-SEG veh=0 n=0 len=27`）。
- `act=48 actTile=42,10`：机车 48 在 42,10（它执行的是**车站**目的地的挂接），却把目标锁到了 **46 格之外、停在自己车库里的 veh=0**。
- 两台机车（48、54）反复指向同一个目标，且各自 `dist` 一直不变 ⇒ 它们**永远不会挂上**，只会一直 `COUPLE-FAIL`。

⇒ 与玩家口述吻合：设了「临时挂接分组」也没能改变候选筛选，机车照样锁上"不该挂的那一列"。

---

## 三、代码取证（逐行）

唯一判据函数：`src\couple_group.cpp:279 R3RCoupleAllowedIgnoringPair(coupler, target)`。
注释自陈它是**三个解析层级共用的唯一判据**（`couple_group.cpp:295-301`）：yapf 目的地测试、回溯安全测试、到点闸门。所以这里一改，三处同时生效。

### 3.1 临时分组确实被合并进了掩码（这段代码是好的）

```
348:	CoupleGroupMask coupler_groups = R3RGetEffectiveCoupleGroupsOfSegment(coupler);
349:	CoupleGroupMask target_groups  = R3RGetEffectiveCoupleGroupsOfSegment(target);
350:	if (order.IsType(OT_GOTO_COUPLE)) {
351:		const CoupleGroupID temp_group = order.GetCoupleTempGroup();
352:		if (R3RIsValidCoupleGroup(temp_group)) coupler_groups |= R3RCoupleGroupBit(temp_group);
353:	}
...
361:	if (target != nullptr) {
362:		const Train *wait_head = Train::From(target->First());
363:		if (wait_head != nullptr && wait_head->current_order.IsType(OT_WAIT_COUPLE)) {
364:			const CoupleGroupID wait_group = wait_head->current_order.GetCoupleTempGroup();
365:			if (R3RIsValidCoupleGroup(wait_group)) target_groups |= R3RCoupleGroupBit(wait_group);
366:		}
367:	}
```

（此处 `order` 就是 `coupler->current_order`，见 `couple_group.cpp:302`。）

### 3.2 但合并出来的掩码**在同公司路径上从来没被读过**

```
394:	/* R3R (D6-①): the company boundary is a second, independent gate. ... */
400:	if (coupler->owner != target->owner) {
401:		return R3RCoupleGroupMasksAllowCrossCompany(coupler_groups, target_groups);
402:	}
403:	return true;          // ← 同公司：一律放行，coupler_groups / target_groups 到这里直接被丢弃
```

`369-392` 那段注释是**第 109 轮 KI-195** 留下的自陈，逐字承认了本缺陷：

> 「连带影响：命令级的「临时挂接分组」(`GetCoupleTempGroup`) 对同公司挂接**不再有任何作用**（本来就是为了通过这道白名单）；它仍然有效的地方是公司边界……」

即：KI-195 为了让"忘记加分组"不再表现为"到了站台找不到挂接目标"，把 `R3RCoupleGroupMasksCompatible(coupler_groups, target_groups)` 这道白名单**整个删掉**了。而 `348-367` 的临时分组合并当初就是**为了通过那道白名单**才写的 —— 白名单一删，同公司路径上它就彻底成了死代码：算出来、没人看。

### 3.3 两句注释/文档互相矛盾（说明这是遗漏，不是设计变更）

- `couple_group.cpp:331-337`（KI-170 原文）与 `lang\simplified_chinese.txt:4775`（给玩家看的 tooltip）：临时分组**能限定/放行**能挂的车底。
- `couple_group.cpp:383-385`（KI-195）：临时分组对同公司**不再有任何作用**。

玩家看到的是 tooltip，所以实测感受必然是"设了等于没设"。

---

## 四、根因

**`R3RCoupleAllowedIgnoringPair()` 里，第 109 轮 KI-195 删掉了分组白名单闸门，而命令级「临时挂接分组」的合并（`coupler_groups |= temp_group` / `target_groups |= wait_temp_group`）是依赖那道白名单生效的。**于是在**同公司**挂接这条（也是最常走的）路径上：

1. `348-367` 算出的掩码被 `403: return true;` 直接丢弃；
2. 临时分组只剩下"跨公司授权"（`400-402`）一条作用，同公司挂接完全不受它影响；
3. 候选筛选退化为"只要在 `WAIT_COUPLE` 就都能挂"（加上 `322-328` 只排除**别的车站**、**不排除车库**这一点），于是机车会锁上停在车库/远处的任意一列车底（现场 `CPL-PAIR act=48 tgt=0 dist=46 actTile=42,10 tgtTile=1,15`）；
4. 玩家没有任何手段再把这台机车"指"到该挂的那一列上 —— 这正是"临时挂接分组用不了"。

---

## 五、修法

**只改 `src\couple_group.cpp` 一个 `.cpp`（不碰 `src\*.h`，增量合规）。**

在 KI-195 的"同公司默认不设门槛"之上，补一条**窄**闸门 —— **只有命令显式声明了临时分组时，才恢复分组白名单**：

```cpp
/* 记录两侧是否真的声明了临时分组（而不是掩码里是否恰好有位） */
const bool coupler_declared_temp = <GOTO_COUPLE 且 GetCoupleTempGroup() 有效>;
const bool target_declared_temp  = <WAIT_COUPLE 且 GetCoupleTempGroup() 有效>;

if (coupler->owner != target->owner) {            // 跨公司：不动
    return R3RCoupleGroupMasksAllowCrossCompany(coupler_groups, target_groups);
}
/* 同公司：任一方显式声明了临时分组 ⇒ 分组必须相容；
 * 双方都没声明 ⇒ 维持 KI-195 的"一律放行"（不回归老毛病）。 */
if (coupler_declared_temp || target_declared_temp) {
    return R3RCoupleGroupMasksCompatible(coupler_groups, target_groups);
}
return true;
```

要点：

1. **不声明 = 行为逐字节不变**。KI-195 想解决的"忘记把车底加进分组 ⇒ 到了站台找不到挂接目标、沿站台乱跑"完全不受影响（只有玩家主动设了临时分组才会启用白名单）。
2. **声明 = 白名单重新生效**，且用的是"有效集合（真分组 ∪ 临时分组，含父组，见 `348-349`/`R3RGetEffectiveCoupleGroupsOfSegment()`）"，即：
   - 机车声明"滚木" ⇒ 它能挂上的车底必须与 `自己的真分组 ∪ 滚木` 有交集 ⇒ **正好挂不上没分组的车库车底（`target_groups == NONE` ⇒ `R3RCoupleGroupMasksCompatible()` 走 `126: a==b` ⇒ false）**，也就是现场那 27 节 `tgt=0` 会被直接剔出候选集，`CPL-PAIR` 不再锁它。
   - 等待侧声明（KI-225 的对称语义"我在等人按这个分组来接我"）同样成立。
3. **三处调用点同时受益**（yapf 目的地测试 / 回溯安全测试 / 到点闸门），因为它们共用这一个判据；被拒候选按既有语义上报为"这里没有等待的车底"，机车会继续找别的候选，找不到则走 KI-193 的 `COUPLE-DEST-EMPTY`（等待 500 tick 后跳过命令），不会卡死。
4. **跨公司分支一字不动**：仍只看 `R3RCoupleGroupMasksAllowCrossCompany()`。
5. 顺带把 `369-392` 的 KI-195 注释与 `331-346` 的 KI-170 注释更新为"本轮起：声明了临时分组就重新启用白名单"，消除文档与行为的矛盾。

### 5.1 配套探针（强烈建议，便于下轮自证）

在放行/拒绝出口加**节流**日志，沿用本项目风格：

- 仅当**同公司且启用了白名单**时打：`CG-GATE same-company whitelist coupler=%d target=%d cdecl=%d tdecl=%d cmask=0x%X tmask=0x%X compatible=%d`。

> 说明：现场日志**没有**任何探针记录"订单上到底有没有设临时分组"（`CPL-PAIR` 只打车号与坐标），所以本轮证据链在"玩家是否真的选了一个组"这一点上是**间接**的（由 2.1 的 `CG-*` 序列 + 4 个空组的用途推断）。加探针后，下一份日志即可直接确认。

---

## 六、复测判据

1. **不设临时分组**：普通挂接行为与第 201 轮**逐字一致**（`CPL-PAIR` / `COUPLE-OK` 频次与坐标不变），不出现新探针行。
2. **设临时分组 = 一个空组**（组里没任何车段）：机车**不再**锁上没分组的车底；现场应表现为 `CPL-PAIR act=48 tgt=0` / `act=54 tgt=0` **消失**，取而代之的是"找不到候选"（`CPL-PATHFOUND found=0` 或 500 tick 后的 `COUPLE-DEST-EMPTY`）。
3. **设临时分组 + 把目标车底真的加进该组**（在分组窗口用"添加段"把 `veh=0` 那一列放进同一个组）：机车应能正常 `COUPLE-OK`，即"临时分组真的能把机车指到该挂的那一列上"。
4. 新增探针应显示 `cdecl=1` / `compatible=0|1` 与最终拒/放一致。
5. **跨公司挂接**回归：跨公司仍要求共享一个 `AllowsOthers()` 的组（现场无该场景，需另测）。
6. `read_lints` 0；增量构建 `EXIT_CODE=0` + `[n/n] Linking`；`build\openttd.exe` 晚于 `src\couple_group.cpp`。

---

## 七、未确认项 / 顺带登记

- **U-1（登记为 KI-307，未改行为，待玩家拍板）**：`couple_group.cpp:322-328` 的车站目的地过滤**只排除"别的车站"，不排除"任何车库/无轨道格"**（KI-165/KI-288 的明确设计："Candidates in a depot, or off any permanent way, are untouched"）。现场 `CPL-PAIR act=48 ... actTile=42,10 tgtTile=1,15` 就是"车站目的地的机车锁上了躺在车库里的车底"。**这是本轮症状的放大器**：即使不设任何分组，机车也会被车库里的车底吸引走。是否收紧（例：车站目的地不收车库候选）需要玩家拍板，本轮**不动**。
- **U-2（登记，未改）**：`CPL-PAIR` 显示 48 与 54 **交替**锁定同一个 `tgt=0`。配对锁的 `r3r_couple_requester` 是单值字段（`couple_group.cpp:421-428`），两台机车不可能同时合法持有 ⇒ 说明锁在被反复改写（抢锁/重设）。与本轮根因无关，另记待查。
- **U-3**：本次日志里"订单是否带临时分组"无探针可证（见 5.1）。加上探针后由玩家复跑确认。
- **U-4**：本轮**未**核查车库编辑路径（`depot_gui.cpp` / `CmdMoveRailVehicle`）里是否存在"临时分组"概念的使用（按语义应当没有，因为它是命令级属性）。
