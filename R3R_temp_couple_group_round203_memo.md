# R3R「临时挂接分组」第 203 轮 临时分析报告

主题：两列列车同时加入同一个临时挂接分组，闸门已放行（compatible=1），却始终不耦合（0 条 COUPLE-OK）。
上一轮：`R3R_temp_couple_group_round202_memo.md`（KI-306 已修·已编译；KI-307 / KI-308 未修）。

---

## 一、玩家需求

> "我使用两个列车同时加入临时挂接分组的方式，尝试让它们耦合，但它们一直不耦合，这不正常，我希望您能修复这个问题。还有，你似乎有需要我拍板的问题，请你把问题列出来。"

拆解：
1. 场景预期：两列车（机车 + 等待车底）**都声明同一个临时挂接分组** ⇒ 应当耦合成功。
2. 实际：一直不耦合。
3. 需要：修复 + 列出需拍板事项。

---

## 二、现场证据

日志 `build\R3R_debug.log`，**48 826 B / 671 行**，由含第 202 轮 KI-306 修复的 exe 产生（证据：出现了 KI-306 新增的 `CG-GATE` 探针，旧日志没有）。

### 2.1 现场原文（L303–L321，逐字）

```
303:SEGTR-SNAP tag=depot-edit head=0 nseg=2 ctrl=6 borrowed=1
304:SEGTR-SNAP   i=1 id=1 front=0 star=1 ctrl=0 | live u=2 n="T8701/2-南宁-河内（嘉林）1" gid=65534 | bk u=1 n="T8701/2-南宁-凭祥" gbk=-1 | row use u=1 n="T8701/2-南宁-凭祥" g=-1
305:SEGTR-SNAP   i=2 id=2 front=6 star=1 ctrl=1 | live u=0 n="" gid=65534 | bk u=2 n="T8701/2-南宁-河内（嘉林）1" gbk=65534 | row use u=2 n="T8701/2-南宁-河内（嘉林）1" g=65534
306:NOABSORB-SEG veh=0 n=0 len=27
307:DEPOT-ARR veh=0 spd=0 real=0(17) curType=17 stuck=0 tileDepot=1 tx=1 ty=15 destTx=-1 destTy=-1 destDepot=0 destResv=0 tileEqDest=0 depotDir=-1 enterDir=2 tt=0 impl=0
308:SKIP-STOPPED veh=27 order=0 real=17 spd=0 tile=1,15 parked=0 front=1 nord=33 idx=0 tt=0
309:SKIP-STOPPED veh=48 order=0 real=1 spd=0 tile=5,6 parked=0 front=1 nord=14 idx=0 tt=0
310:SKIP-STOPPED veh=51 order=0 real=6 spd=0 tile=62,117 parked=0 front=1 nord=11 idx=0 tt=0
311:NOABSORB-SEG veh=54 n=0 len=3
312:DEPOT-ARR veh=54 spd=0 real=0(16) curType=16 stuck=0 tileDepot=1 tx=1 ty=15 destTx=5 destTy=13 destDepot=0 destResv=0 tileEqDest=0 depotDir=-1 enterDir=2 tt=0 impl=0
313:CG-GATE compatible=1 coupler=54 target=0 cdecl=1 tdecl=1 cmask=0x1 tmask=0x1
314:[R3R] CPL-PAIR act=54 tgt=0 dist=0 actTile=1,15 tgtTile=1,15
315:SKIP-STOPPED veh=57 order=0 real=6 spd=0 tile=1,15 parked=0 front=1 nord=6 idx=0 tt=0
316:CPL-ENTRY veh=54 tile=1,15 orderType=16 dontReserve=0
317:CPL-ORIGIN veh=54 origin=1,15 td=8 vehDir=5 vehTd=8 dontReserve=0
318:CPL-PATHFOUND veh=54 found=0
319:TRP veh=54 mf=1,15 real=0 type=16 dest=1669 spd=0 fp=0 look=0 one=0 mstuck=0 fto=0 => ok=0 res=0
320:DEPOT-ARR veh=54 spd=0 real=0(16) curType=16 stuck=1 tileDepot=1 tx=1 ty=15 destTx=5 destTy=13 destDepot=0 destResv=0 tileEqDest=0 depotDir=-1 enterDir=2 tt=0 impl=0
321:CPL-SKIP veh=54 tile=1,15 orderType=16 dontReserve=0 retryIn=8
```

### 2.2 事实表

| # | 事实 | 值 | 出处 |
|---|---|---|---|
| F1 | 车54 = HXN5B 机车（3 节），**停在车库 (1,15)** | `inDepot=1 isDepot=1 depotIdx=0 spd=0 stopped=1` | L201 `R3RDUMP-CHAIN head=54` |
| F2 | 车54 当前真订单 = `OT_GOTO_COUPLE`(16) | `coType=0 realType=16`；`curType=16 real=0(16)` | L201 / L312 |
| F3 | 车54 订单目的地 = **站台 (5,13)**，**不是车库** | `destTx=5 destTy=13 destDepot=0 depotDir=-1`；`TRP dest=1669`（128 宽地图 ⇒ (5,13)） | L312 / L319 |
| F4 | 车0 = 等待车底链，**也在车库 (1,15)** | `curType=17`（`OT_WAIT_COUPLE`）、`tx=1 ty=15` | L307 |
| F5 | 两车**都在同一个临时挂接分组** ⇒ 闸门放行 | `compatible=1 cdecl=1 tdecl=1 cmask=0x1 tmask=0x1` | **L313** |
| F6 | 配对扫描成功且**距离 0** | `CPL-PAIR act=54 tgt=0 dist=0 actTile=1,15 tgtTile=1,15`（全日志 19 次，全部 dist=0） | L314/326/333/…/667 |
| F7 | 耦合寻路**每次 found=0** | `CPL-ENTRY tile=1,15` → `CPL-ORIGIN origin=1,15 td=8` → **`CPL-PATHFOUND found=0`**（11 次全 0，0 次 found=1） | L316-318/336-338/…/439-441 |
| F8 | 预留失败并被标 stuck，机车**从未离开 (1,15)** | `TRP … => ok=0 res=0` → `stuck=1` | L319 / L320 |
| F9 | **全日志 0 条 `COUPLE-OK`** | — | 全文检索 |
| F10 | 反证 KI-306 已生效：`CPL-PAIR act=48` **一条也没有**（旧日志有） | — | 全文检索（只有 act=54） |
| F11 | 车48 未声明分组 ⇒ 被正确拒绝 | `CG-GATE compatible=0 coupler=48 target=0 cdecl=0 tdecl=1 cmask=0x0 tmask=0x1` | **L443** |
| F12 | 车48 仍在 (42,10) 刷挂接失败 | `COUPLE-FAIL loco=48 order=16 tx=42 ty=10`（13 次） | L461/493/519/…/652 |

### 2.3 复测判据对照（第 202 轮 KI-306 的回测）

- 判据③「设临时分组 **并把目标车底真的加进该组** ⇒ 正常 COUPLE-OK 与 compatible=1」
  → **`compatible=1` 已达成（F5），但 `COUPLE-OK` 未出现（F9）**。
- 结论：**KI-306 的修复本身是对的、已生效（F5/F10/F11 三重证明）**，本轮的"不耦合"是 KI-306 之下、之前被闸门挡住而未能暴露的**下一层**问题。故本条不是 KI-306 未修好。

---

## 三、根因（KI-306 修复后的残余，两层缺一不可）

### 根因 1：车库格在**耦合寻路**里是死胡同 ⇒ 目标在库里就永远 reach 不到

`src\pathfinder\yapf\yapf_rail.cpp:770-771`

```cpp
/* R3R: a depot tile is a dead end - never extend a path through it. */
if (IsRailDepotTile(old_node.GetLastTile()) && old_node.parent != nullptr) return;
```

- 起点节点（`parent == nullptr`，即 `origin=1,15` 自身）是放行的，所以寻路能**离开**车库；
- 但**任何**扩展都不能**进入**车库格。
- 目标车底 0 的整条链**就在车库格 1,15 上**（F4）⇒ 搜索空间里根本不存在可达的目标节点 ⇒ `found=0`。
- 注意：**`dist=0` 也救不了它**（F6 与 F7 同时成立）——问题不在距离，在拓扑可达性。

### 根因 2：车库内"就地挂接"分支被**订单目的地必须是本车库**挡住

`src\train_cmd.cpp:16647-16663`

```cpp
if (consist->track == TRACK_BIT_DEPOT && consist->IsEngine()) {
	/* ... 注释说明：只有订单目标就是本车库时才允许走库内直挂 ... */
	const bool couple_targets_this_depot = r3r_pending_depot_couple ||
			(consist->current_order.IsType(OT_GOTO_COUPLE) &&
			consist->current_order.GetCoupleIsDepot() &&
			IsRailDepotTile(consist->tile) &&
			consist->current_order.GetDestination().ToDepotID() == GetDepotIndex(consist->tile));
	if (couple_targets_this_depot) {
		/* ... 16664-16718：materialise 订单 → 清 Stopped → TrainCoupleHandler() → 成功/失败后 cur_speed=0 原地重试 ... */
		consist->cur_speed = 0;
		consist->progress = 0;
		...
		if (!consist->IsFrontEngine()) return true;
	} else if (!consist->current_order.IsType(OT_GOTO_COUPLE)) {
		NormalizeTrainVehInDepot(consist, true);
	}
}
```

车54 的订单是「**站台 (5,13) 挂接**」（F3）⇒ `GetCoupleIsDepot() == false` ⇒ `couple_targets_this_depot == false`：

- ① **库内就地挂接**：整块跳过（不做）；
- ② **耦合寻路**：见根因 1，进不了车库格；
- ③ **出库**：`16719-16721` 的兜底要求 `!IsType(OT_GOTO_COUPLE)`，而车54 的订单**就是** `OT_GOTO_COUPLE` ⇒ 连 `NormalizeTrainVehInDepot()` 也不执行 ⇒ 不给出库。

**⇒ 三条路全部被切断。机车 54 于是在车库 (1,15) 上：每 8 tick 重试一次配对与耦合寻路（`CPL-SKIP retryIn=8`），每次 `found=0`，每次把自己标 `stuck=1`，既挂不上、也开不出去，永远停在原地。**

### 3.1 一句话因果链

> 两车都声明了同一个临时分组 ⇒ **闸门放行**（`compatible=1`）⇒ **配对成功**（`tgt=0, dist=0`）⇒ 但目标在**车库格**上 ⇒ YAPF 把车库格当死胡同 ⇒ `found=0`；而唯一能绕开寻路的「库内就地挂接」又被「订单目的地必须是本车库」挡住（本车订单目的地是站台）⇒ **不耦合且不出库，死锁**。

### 3.2 为什么第 202 轮没暴露这层

第 202 轮的现场是**闸门**先挡住（旧 exe 没有白名单闸门 ⇒ 同公司恒放行 ⇒ 另一条路径），本轮玩家按判据③**把目标车底真的加进了分组**，闸门这一层通了，死锁才第一次完整暴露。

---

## 四、修法候选

### 修法 A（推荐·治本）：把车库直挂判据从"订单目的地 == 本车库"放宽为"**我的挂接目标就在本车库**"

```cpp
const bool couple_targets_this_depot = r3r_pending_depot_couple ||
		(consist->current_order.IsType(OT_GOTO_COUPLE) &&
		IsRailDepotTile(consist->tile) &&
		/* 新增：KI-182 的配对锁指向的目标此刻就在我这台机车所在的车库里 */
		(consist->current_order.GetCoupleIsDepot()
			? consist->current_order.GetDestination().ToDepotID() == GetDepotIndex(consist->tile)
			: R3RPairTargetIsInThisDepot(consist)));
```

- 语义：**"我在车库里，而我要挂的那一列恰好也在同一个车库里" ⇒ 就地挂**。
- 关键：判据从"订单声明"改为"**事实**（配对锁的位置）"，因此**不会**重犯 16648-16657 注释里那个旧 bug（"订单指向别处、库内没有我的目标"时仍会照常开出去，因为那时锁定的目标不在本库）。
- 对玩家场景：54 的锁就是 0，0 就在 1,15 ⇒ 立刻走 `TrainCoupleHandler()` ⇒ `COUPLE-OK`。

### 修法 B（治标，= KI-307 甲）：站台/路点目的地的挂接候选，排除停在车库格/无轨道格上的车底

- 效果：54 不会被锁到库内车底；48 也不会被锁到 46 格外的库内车底。
- 但**玩家想要的耦合仍然不会发生**（54 会去 (5,13) 找，找不到就 `COUPLE-DEST-EMPTY` 跳过订单）。
- ⇒ 单独采用 B 无法满足本轮需求，只能作为 A 的补充。

### 修法 C（补漏）：让 `OT_GOTO_COUPLE` 机车能从车库正常出库

- 现在：订单是 `OT_GOTO_COUPLE` ⇒ 16719 兜底跳过 ⇒ 不 `NormalizeTrainVehInDepot` ⇒ 出不去。
- 改：把兜底条件放宽为"订单不是 GOTO_COUPLE，**或** 订单是 GOTO_COUPLE 但目标不在本库"。
- 效果：不会再有"机车卡死在车库"，但它解决的是**B 场景**（目标不在库内）的出库，对玩家场景（目标在库内）应由 A 处理。

### 修法 D（组合）

A + C。A 解决"目标同库"（玩家场景）；C 解决"目标不在库"（不卡死）。
B 是可选收紧，视 P-2 拍板。

---

## 五、未确认项

- **U-1**：车0 的整条链是否**全部**在车库格 1,15 上？`R3RDUMP-CHAIN head=0 … n=6 isDepot=1 depotIdx=0`（L99）与 `NOABSORB-SEG veh=0 n=0 len=27`（L306）数目不一致，疑"库内多链 / 一库容纳长链"的显示差异。若目标链只有一部分在库里，就地挂接的几何仍需复核。
- **U-2**：车48 的 `COUPLE-FAIL loco=48`（F12）看起来只是"未声明分组被正确拒绝"（F11）的必然表现，属**正常拒绝**而非 bug；待玩家确认 48 是否本应参与本轮测试。
- **U-3**：修法 A 落地后，"订单目的地是**另一个车库**"的机车（`GetCoupleIsDepot()==true` 但目的地 != 当前库）必须**保持原判据**，不能因为"恰好在老家车库遇到任何等待车底"就挂上——A 的写法已用三元分支保证，需复测确认。
- **U-4**：`TrainCoupleHandler()` 在"两车同在车库格"下的几何/提交路径（`GetCouplePosition`、`R3RCanCoupleNow`）未复核；第 202 轮之前该路径曾正常工作（KI-110/KI-118），但本轮未实测。
- **U-5**：修法 A 是否会让"库内挂接"抢在"先去站台"之前发生——这正是 P-1 要拍板的语义变更。

---

## 六、需玩家拍板

- **P-1（核心）**：是否接受修法 A 的语义变更 —— 订单是「**去某处**（站台/路点/别的车库）挂接」，但**我的挂接目标恰好就在我当前所在的车库里**时，**就地先挂上**（不再开出去）。
  - 甲：接受（== 修法 A，玩家场景可修复）。
  - 乙：不接受（则玩家场景无法修复，只能靠 B 让机车不再死锁，改为"开出去后找不到目标 ⇒ 跳过订单"）。
- **P-2**：是否同时采纳修法 B（站台/路点目的地的挂接候选**不收**停在车库格上的车底，= KI-307 甲）？它会让候选更"干净"，但也会让"库内远景车底被当目标"这种情况消失。
- **P-3**：是否同时采纳修法 C（GOTO_COUPLE 机车目标不在本库时**允许正常出库**）？
- **P-4**：修法 A 命中时，如果**同一个车库里有多列合格等待车底**（多候选），选哪一个？（当前 `R3REnsureCouplePair` 的规则 = 最近距离优先，需确认是否沿用。）

---

## 七、复测判据（修法落地后用）

1. 同场景（两车同库、同临时分组、机车订单为站台挂接）应出现 **`COUPLE-OK loco=54`**。
2. 不再无限刷 `CPL-PATHFOUND veh=54 found=0` / `CPL-SKIP veh=54 retryIn=8`；54 不再停在 1,15。
3. **KI-306 判据①②不回退**：未声明分组的 48 仍应 `CG-GATE compatible=0` 且无 `CPL-PAIR act=48`。
4. 反例不误伤：订单指向**别的车库**、或库内**没有**自己的挂接目标时，机车仍照常开出去（不出现"老家车库乱挂"）。
5. 不出现新的 `stuck=1` 常驻；`RESV-AUDIT` 无异常预留。

---

## 八、本轮已完成 / 未完成

- 已完成：现场取证（F1–F12）、根因定位（两层，含精确行号）、修法候选与判据、需拍板清单。
- 未完成：修法落地（等 P-1/P-2/P-3/P-4 拍板）、构建验证、游戏内复测。
