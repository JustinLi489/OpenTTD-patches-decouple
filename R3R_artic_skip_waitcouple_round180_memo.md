# R3R 第 180 轮 临时分析报告：两段式列车「等待挂接」却立刻跳到第二条命令

- 日期：2026-10-01 ｜ 轮次：第 180 轮（KI-273 / KI-274）
- 现场：`build\R3R_debug.log`（1 794 行 / 107 799 B，2026-10-01 01:48:16）
- 现场 exe：`build\openttd.exe`（51 561 472 B，2026-10-01 01:33:57）⇒ 日志晚 15 分钟，**由当前 exe 产生**（含第 176 轮 KI-266/KI-267）
- 本轮状态：**只取证，未改任何源码**；修法属第 176 轮 §六.2 明留给玩家拍板的事项。

---

## 一、玩家报告

> 「一列两段式列车等待挂接，然后立刻跳到第二条命令了。我怀疑要不然打散铰接式没做好，要不然拒绝 R3R 命令收太紧了。」

---

## 二、现场锚点（行号 = `build\R3R_debug.log`）

### 2.1 盘面：两条「真铰接 + ★」的存量焊死链

- 行 8 起 `R3RDUMP`：`idx=0 … ARTH=1 ARTM=0 SEGF=1 …`（veh0 = 假铰接组头 + ★ 段首）
- 行 14 起 `R3RDUMP`：veh6 同为 `SEGF=1`；veh1/veh2（及 veh7 起）`sub=02` = `GVSF_ARTICULATED_PART` ⇒ **真铰接部件**
- 行 59 `R3RDUMP-CHAIN head=0 … stopped=1 inDepot=1 …`
- 行 66 `R3RDUMP-CHAIN head=6 … stopped=1 inDepot=1 … realType=17`
- 行 67 起 `head=6` 订单表，`i=0` 即 `type=17` = **`OT_WAIT_COUPLE`**

⇒ veh0..5 与 veh6..23 = 各自带 ★、各自含真铰接、均停驻的链 = 第 176 轮 §六.1 的「旧档已焊死链」。

### 2.2 车库拖动：把 0..5 并到等待链 6..23 之前

```
539: DEPOT-BEFORE-HEAD-RANK head=6 tmax=1 amin=3
540: DEPOT-BEFORE-HEAD sel=3 head=6 block=0 tail=5
541: ARRANGE-IN dh=0 dst=5 sh=6 src=6 mc=1
545: ARRANGE-IN dh=0 dst=5 sh=6 src=6 mc=1
549: DEPOT-PARK-SHARED veh=6 shared_head=6 share=2
550: UNIT-PARK veh=6 bk=2
552: DEPOT-SEG-SKIP-REAL-ARTIC src=6 block_head=6 block_tail=23 -> ★/⊗ withheld (genuine articulated part)
553: DEPOT-XFER-ID depot-edit head=0 ctrl=6 unit=2 own_bk=1 hasname=1 own_name=1
554: CGRP-NORM-UNION head=0 nseg=2 mask=0
555: SEGID-SYNC tag=depot-edit head=0 nseg=2
556: SEGFRONT-RESYNC-CHECK runs=1 fixed=0
```

- 行 540 镜像语义（第 132 轮补记 KI-213）＝把**目标链 6..23** 移到拖拽块 0..5 尾车之后 ⇒ 行 541/545 两次 `src=6`；合并链头 = 拖拽块所在链链头 = **veh0**，n=24。
- 行 552 = 第 176 轮 KI-267 源头闸门，**确实生效**（扣住新 ★/⊗）；但 ★ 本来就在（2.1 已证 veh6 `SEGF=1`）⇒ 拦不住 ⇒ 行 555 `nseg=2`（玩家说的"两段式"）。
- 行 553 ⇒ 链头 veh0 借用控制段(6) 的订单表。

### 2.3 闸门触发（一行即玩家所报现象）

```
583: GEO settle-depot-edit n=23 veh=23 tile=1,11 x=20 y=184 dir=5 db=0 artic=1 starF=0 starB=0 segid=2 clen=2 dist=-1 nom=-1
584: GEO-END settle-depot-edit head=0 n=24 capped=0
585: ARTIC-SKIP-R3R veh=0 tile=1,11 from=0 to=1 type_from=17 type_to=1 idx_r3r=1 cur_r3r=0
587: DEPOT-ARR veh=0 spd=0 real=1(1) curType=0 stuck=0 tileDepot=1 tx=1 ty=11 …
588: DEPOT-ARR veh=0 spd=0 real=1(1) curType=1 stuck=0 tileDepot=1 tx=1 ty=11 destTx=4 destTy=10 …
```

- 行 583 合并后链上仍标 `artic=1` ⇒ 真铰接部件确实还在链里。
- 行 585：索引被**直接赋值 1**，丢掉索引 0 的 `type=17`（`WAIT_COUPLE`），落到 `type=1`（`OT_GOTO_STATION`）。
- 行 587→588：`curType=0`（未装载）→ `curType=1 destTx=4 destTy=10` ⇒ 列车当场改去 (4,10) 站台。

### 2.4 同场景天然对照组（闸门何时才咬到）

```
593: SKIP-STOPPED veh=30 order=0 real=17 spd=0 tile=1,11 parked=0 front=1 nord=29 idx=0 tt=65535
```

veh30..47（`un=5`）**同样含真铰接、排程首条同样 `WAIT_COUPLE`(`real=17`)、同样停在同库 1,11**，却因处在 `Stopped` 早退分支而**没被跳**。
⇒ 闸门只在"链头过了 `Stopped` 早退、本 tick 抵达调用点"时执行；**是这次车库编辑把合并链头推到了那个位置**（本日志 `SKIP-STOPPED veh=0` 直到第 1087 行才首现）。

### 2.5 下游后果（与 2.3 同源）

```
618: [R3R] CPL-GATE reject=target-not-wait site=depot act=24 tgt=30 aOrd=16 tOrd=0 aStop=0 tStop=1 grp=0
619: [R3R] CPL-GATE reject=target-not-wait site=depot act=24 tgt=0  aOrd=16 tOrd=1 aStop=0 tStop=0 grp=0
620: COUPLE-FAIL loco=24 order=16 tx=1 ty=11 x=20 y=184
625: COUPLE-DEST-EMPTY loco=24 tile=1,11 real=0 num=4 next=1 (waiting in place, order kept)
```

机车 24 排程首条 = `GOTO_COUPLE`(`order=16`)、目的地就是本库（行 591 `real=0(16) curType=16 … destDepot=1 tileEqDest=1`）。它扫到的两个目标都被拒：
- `tgt=0`（那列两段式）`tOrd=1` ⇒ `current_order` 已变 `GOTO_STATION`、不再"在等待"，**正是行 585 的直接后果**；
- `tgt=30` `tOrd=0` ⇒ 停驻链的 `current_order` 根本没装载（见 §六 R-3）。

⇒ 进入 `COUPLE-FAIL` + `COUPLE-DEST-EMPTY … (waiting in place, order kept)` 循环（行 620/625、640-643 等）。

### 2.6 现象重复发生

```
1570: ARTIC-SKIP-R3R veh=0 tile=1,11 from=0 to=1 type_from=17 type_to=1 idx_r3r=1 cur_r3r=0
1572: DEPOT-ARR veh=0 spd=0 real=1(1) curType=0 stuck=1 …
1573: DEPOT-ARR veh=0 spd=0 real=1(1) curType=1 stuck=1 …
```

借用层（KI-04 冻结索引）把索引回灌成 0 后闸门**再跳一次** ⇒ 只要该订单还在索引 0，就会被反复销毁，非一次性时序意外。

---

## 三、源码机制

| 环节 | 位置 | 说明 |
|---|---|---|
| 闸门本体 | `src\train_cmd.cpp:15837` `R3RSkipCoupleOrdersForRealArtic()` | 第 176 轮 KI-266 新增 |
| 命令判定 | `:15796` `R3RIsCoupleCommand()` | **不分主动/被动**：`OT_DECOUPLE`(15)/`OT_GOTO_COUPLE`(16)/`OT_WAIT_COUPLE`(17) 一视同仁 |
| 真铰接判据 | `:1870` `R3RChainHasRealArticPart()` | 只认 subtype 位（`IsArticulatedPart()`） |
| 跳过后直接赋索引 | `:15853-15871` | 不走 `IncrementRealOrderIndex`；`current_order.Free()` + `SetDestTile(INVALID_TILE)` |
| 调用点 | `:16065`（`TrainLocoHandler` 内） | **位于 `Stopped` 早退 `:16029` 之后** ⇒ 停驻车不执行 |
| 耦合目标判据 | `:1937` `R3RIsCoupleTarget()` | 含真铰接 ⇒ 恒 false |
| `CPL-GATE target-not-wait` | `:10651-10652` | 判 `target->current_order.IsType(OT_WAIT_COUPLE)` |

`★` 的四个授予点与各自护栏（第 176 轮 §三 已固化）：

1. `CmdMakeSegment`（升段）→ **唯一有打散前置**（`DearticulateChainWithSnapshot`，`vehicle_cmd.cpp:735` 调用）✅
2. `Couple()` 提交点 → 由 `TryTrainCouple()` 入口硬否决（`:8830`，`CPL-REFUSE-REAL-ARTIC`）拦
3. 车库拖动 merged-on 标记 → `:2989` 采样 + `:3207` 判断（本日志行 552）
4. 解挂给解出部分发 ★ → `:7633`（`DECOUPLE-SEG-SKIP-REAL-ARTIC`）

⇒ **四点已全封**，故 2.1 的 ★+真铰接**不可能**由当前 exe 产生 ⇒ 只能是**存档带入的存量**（第 176 轮 §六.1 原文：「旧存档里已经焊死的链不会自动复原」）。

---

## 四、对玩家两个怀疑的逐条回答

### (a)「打散铰接式没做好」——**成立，但不是做坏，而是第 176 轮明确"没做"**

第 176 轮 §六.2 原文：

> **不做自动打散**：本轮对缺打散前置的路径选择"不发 ★ / 拒绝耦合"，不是"自动 `DearticulateChainWithSnapshot`"。原因：跨文件打散入口不存在（`R3RDearticulateOneGroup` 是 `vehicle_cmd.cpp` 的文件内 static），且在车库拖动中途改车辆身份风险高。若玩家希望"含真铰接的链被自动规范化成假铰接后正常参与耦合"，**需另开一轮做跨文件打散入口**。

第 176 轮 §六.1 原文：

> **旧存档里已经焊死的链不会自动复原**：本轮只堵"产生路径"。已在档的"真 artic + ★"由判据 1~3 隔离……是否加"读档时对含真铰接的链自动打散/清 ★"（落点候选 `R3RResyncSegmentFronts`，`:4711` 已有"真 artic part 不携带 ★"的局部处理）**属需玩家拍板的下一步**。

⇒ 你现场的 veh0 / veh6 就是 §六.1 那批存量；这是那条待办第一次在实机咬人。**打散能力本身没坏**（升段会先打散，四个授予点全封）。

### (b)「拒绝 R3R 命令收太紧」——**也成立，且是更精确的一条**

`R3RIsCoupleCommand()`（`:15796`）把三类命令一视同仁"整段跳过"，但语义不同：

- `OT_GOTO_COUPLE`(16) **主动**：机车去挂别人 —— 跳过它符合闸门动机（别让真铰接链跑耦合寻路）。
- `OT_DECOUPLE`(15) **主动**：跳过有争议但可接受。
- `OT_WAIT_COUPLE`(17) **被动**：**"就在这儿停着等人来挂"** —— **不订路、不驱动任何寻路**，闸门"防止订路"的动机对它**完全不适用**；跳过它 = 命令列车开走，与该订单语义**恰好相反**。

第 176 轮 §五 判据 2 把这个结果写成了**期望**：

> **车底侧**（含真铰接的等待车底排了 `WAIT_COUPLE`）：同样出现 `ARTIC-SKIP-R3R`（`type_from=17`），并且其它机车对它的 `R3RIsCoupleTarget` 恒为假 ⇒ 不会再有 `COUPLE-OK loco=… rear=6` 这类把真铰接车底挂上去的记录。

⇒ 行 585 是**按设计发生**的。所以不是"收太紧"的 bug，而是**设计本身在这一点上收得太紧**：为阻止"被挂上"，顺手把"在原地等"这条被动语义也毁掉了。

### 一句话结论

> **两层都对且互为因果**：(a) 存量真铰接链没被打散（第 176 轮选"不发 ★/拒绝耦合"而非自动打散，并把"读档自动打散/清 ★"列为待拍板）→ 这些链带 ★ 和真铰接进了本局；(b) 订单闸门为隔离它们，把**被动**的 `WAIT_COUPLE` 也整段跳过 → "我在等人来挂"被改成"去 (4,10) 站台"，列车当场开走；机车 24 的 `CPL-GATE target-not-wait` / `COUPLE-DEST-EMPTY` 循环就是它的下游。

---

## 五、完整触发链

1. 存档带入两条「真铰接 + ★」链（veh0..5、veh6..23），均停驻。
2. 把块 0..5 拖到链 6..23 之前（行 540-548）⇒ 合并 head=0、n=24、**nseg=2**。
3. 链头 veh0 借用控制段 6 的订单表（行 553），该表 `i=0` = `WAIT_COUPLE`（行 66-67）。
4. 源头闸门只扣新 ★/⊗（行 552 生效），但 ★ 本来就在 ⇒ 两段链成立。
5. 车库编辑那一 tick，合并链头**不在** `Stopped` 早退态 ⇒ 走过 `:16029`，抵达 `:16065`。
6. `R3RChainHasRealArticPart(veh0)` 为真 ⇒ 闸门执行，索引 0 → 1（行 585）。
7. 后果：命令变 `GOTO_STATION`(4,10)（行 588）；不再广告 `WAIT_COUPLE`；机车 24 挂接被拒（行 618-619）；`COUPLE-FAIL`/`COUPLE-DEST-EMPTY` 循环（行 620/625）。
8. 借用层回灌索引为 0 ⇒ 闸门再跳（行 1570）⇒ 反复。

---

## 六、未确认项

- **R-1**：车库编辑那一 tick 合并链头为何不在 `Stopped` 早退态。已排除：KI-153 清理块（`:5696-5709`）**只**清 `r3r_parked`、不动 `VehState::Stopped`。未定位。（不影响结论：只要链头走过早退，被动命令就会被毁。）
- **R-2**：行 59/66 的 `R3RDUMP-CHAIN` 是日志开头快照，可能与拖动帧不同帧；若"合并前 veh0 已非 Stopped"，也解释得通。需后续加一条链头 `vehstatus` 只读探针定论。
- **R-3**：`tgt=30 tOrd=0`（`OT_NOTHING`）说明**停驻链的 `current_order` 未装载 `WAIT_COUPLE`**；这是否使**所有**停驻等待车底都不被认作耦合目标（=第 176 轮判据 2 之外的另一条拒绝面），本轮未展开。

---

## 七、修复候选（待玩家拍板）

| 方案 | 内容 | 代价/风险 | 效果 |
|---|---|---|---|
| **A 订单层（最小）** | `R3RSkipCoupleOrdersForRealArtic` 只对**主动**命令生效：遇到 `OT_WAIT_COUPLE`(17) **不改索引**、原地停车返回（与"整表全 R3R"分支的停驻语义一致）。仅动 `train_cmd.cpp`，约 5 行 | 极低 | 止住"莫名开走"；但等待链**依旧永远挂不上**（`:1937` 仍拒）⇒ 从"跑掉"变成"干等" |
| **B 源头（治本）** | 做跨文件打散入口（抽出/暴露 `vehicle_cmd.cpp` 的 `R3RDearticulateOneGroup`），在**车库拖动合并**与**读档**两条边对含真铰接链自动 `DearticulateChainWithSnapshot`，之后照常发 ★/参与耦合 | 中（改写车辆身份，需护栏+探针） | 玩家真正想要的动作成立 |
| **C 读档迁移（折中）** | 只在读档时对"真 artic + ★"链自动打散/清 ★（第 176 轮 §六.1 的落点候选 `R3RResyncSegmentFronts`，`:4711` 已有局部处理） | 中（一次性改写存档态） | 存量一次清掉；新产路径已全堵 |

**推荐**：先做 **A**（无条件、零风险，止住"等待却开走"这个最刺眼的症状），再按玩家选择做 **B** 或 **C** 让"能挂上"真正成立。只做 A 不动 B/C ⇒ 玩家依然挂不上，只是不再乱跑。

---

## 八、复测判据（游戏内，待玩家）

1. **被动命令必须保住**：含真铰接、排程首条为 `WAIT_COUPLE` 的链，**不得**再出现 `from=0 to=1 type_from=17` 的 `ARTIC-SKIP-R3R`；出现 `type_from=16` 的仍算正常（第 176 轮判据 1）。
2. **车库拖动**：把块并到含真铰接的等待链前 ⇒ 仍打 `DEPOT-SEG-SKIP-REAL-ARTIC`，但链头订单索引**不得**因此改变。
3. **第 176 轮判据 7 不退**：机车 48 那条 `GOTO_COUPLE` 照旧被跳、不再刷 47 行 `COUPLE-FAIL`。
4. **第 176 轮判据 6 不退**：全假铰接（打散组）的正常耦合/解挂/翻转行为逐字不变，日志不出现 `ARTIC-SKIP-R3R` / `*-REFUSE-REAL-ARTIC` / `*-SEG-SKIP-REAL-ARTIC`（判据 1/2 修改后需重述这一条）。
5. 若走 **B/C**：打散后同场景应能出现 `COUPLE-OK loco=24 rear=…`，且 `CPL-GATE target-not-wait` 不再针对该目标。

---

## 九、第 180 轮实现落地：选项 C（读档迁移，2026-10-01）

玩家拍板「**只做 C**」：只在读档时对「真铰接 + ★/⊗」链自动打散。**A（订单闸门）与 B（车库拖动边）不实现**，KI-274 的设计口径维持原样。

### 9.1 改动（全部 .cpp，未碰任何 `src\*.h`）

| 文件 | 改动 |
|---|---|
| `src\vehicle_cmd.cpp` | `DearticulateChainWithSnapshot()` 去掉 `static` 改为外部可见（并加注释说明新调用方），定义仍在原处（升段命令旁） |
| `src\train_cmd.cpp` | 新增 `static void R3RDearticulateLegacyArticSegment(Train *head)`（插在 `R3RChainHasRealArticPart` 之后、`R3RIsCoupleTarget` 之前）；紧邻处手写 `void DearticulateChainWithSnapshot(Train *head);` 前向声明 |
| `src\train_cmd.cpp` | `R3RRebuildCouplePriorities()` 开头（`if (chain == nullptr) return;` 之后、KI-169a 停放分支之前）调用 `R3RDearticulateLegacyArticSegment(chain);` |

### 9.2 迁移逻辑与判定

```cpp
static void R3RDearticulateLegacyArticSegment(Train *head)
{
	if (head == nullptr) return;
	bool has_marker = false;   // ★ 或 ⊗
	for (const Train *v = head; v != nullptr; v = v->Next()) {
		if (v->IsSegmentFront() || v->IsSegmentBack()) { has_marker = true; break; }
	}
	if (!has_marker || !R3RChainHasRealArticPart(head)) return;   // 非 R3R 段 ⇒ 一律不动
	DearticulateChainWithSnapshot(head);
	head->ConsistChanged(CCF_SAVELOAD);
	R3RDbgWrite("LEGACY-ARTIC-DEARTIC head=%d n=%u (load migration, round 180 option C)\n",
			(int)head->index.base(), n);
}
```

- **只在读档跑**：唯一调用方是 `R3RRebuildCouplePriorities()`，而它只被 `src\sl\vehicle_sl.cpp:521`（`if (part_of_load) R3RRebuildCouplePriorities(t);`）调用 ⇒ 活链永不经此路径。
- **打散而非清 ★**：保留 ★/⊗（段结构、耦合目标资格全靠它），只把真铰接拆成「打散组」（`R3RDearticulateOneGroup`：烘焙均分重量/功率 + `ArticGroupHead`/`ArticGroupMember` 角色位 + override）。理由：闸门判据是 `R3RChainHasRealArticPart()`（`train_cmd.cpp:15840`），**清 ★ 不会让闸门闭嘴**——只有打散才能让该链重新变成合法段（能等待、能被挂、能被翻）。
- **只认 R3R 段**：原生铰接车（GRF 自带 artic）没有 ★/⊗，`has_marker` 为假 ⇒ 一字不动。
- **顺序**：迁移 → 优先级重排 → `R3RSettleChainSegments(chain, "load")`（段 ID/段行在那里才建），故打散后段结构照旧成立。
- `ConsistChanged(CCF_SAVELOAD)` 必须补：打散后 `cached_veh_length` 与 NewGRF 缓存对不上（这一句同时刷新 `InvalidateNewGRFCacheOfChain`）。注意枚举名是 `CCF_SAVELOAD`（`train.h:82`），**不是** `CCF_LOADSAVE`。

### 9.3 构建自证（复用 `_tmp_inc_build.cmd`，未新建任何 .cmd）

- 护栏 `build\R3R_incbuild.guard.log` = `GUARD: incremental is safe (no header/lang file is newer than the newest object)` ⇒ .cpp-only 改动，**未触发 KI-183 全量**。
- `build\R3R_incbuild.log` 尾 = `[4/4] Linking CXX executable openttd.exe`；`build\R3R_incbuild.done` = `EXIT_CODE=0`。
- `src\train_cmd.cpp` 02:16:11 → `train_cmd.cpp.obj` 02:18:15；`src\vehicle_cmd.cpp` 02:15:45 → `vehicle_cmd.cpp.obj` 02:18:17；`build\openttd.exe` **2026-10-01 02:20:17（51 561 472 B）**，晚于两个 .obj 与两个 .cpp。
- `read_lints`（两文件）= 0 条；exe 内命中新字面量 `LEGACY-ARTIC-DEARTIC`（1 处）。
- 构建前已确认 `openttd.exe` 未在运行（避免 LNK1168）。

### 9.4 对第 180 轮现场的预期效果

- 该存档里 `veh0..5` 与 `veh6..23` 两条「★ + 真铰接」链在**读档那一刻**被打散，各打一行 `LEGACY-ARTIC-DEARTIC head=<链头> n=<节数>`。
- 之后 `R3RChainHasRealArticPart(链头)` 恒为假 ⇒ `R3RSkipCoupleOrdersForRealArtic()`（`:15840`）不再命中 ⇒ 行 585 那种 `type_from=17 type_to=1` 的 `ARTIC-SKIP-R3R` **不再出现**，等待链真的在原地等。
- `CPL-REFUSE-REAL-ARTIC`（`:8831`）与 `FLIP-REFUSE-REAL-ARTIC`（`:8453`）对该链解除 ⇒ 机车 24 有机会挂上（应出现 `COUPLE-OK loco=24 rear=…`），`CPL-GATE reject=target-not-wait` 不再以该链为目标。
- **KI-274 本身仍在**（口径未改）：若将来又出现「真铰接但无 ★/⊗」的链，闸门照旧会跳它的被动 `WAIT_COUPLE`。C 只是让存量不再落入该口径，不改口径。

### 9.5 风险与边界

1. **读档期改写车辆身份**：打散改变 `cached_veh_length`、烘焙重量/功率、角色位。已在打散后立刻 `ConsistChanged(CCF_SAVELOAD)`，但**无实机验证**。
2. **运动中的链也会被迁移**：判定不看速度/所在格（`DearticulateChainWithSnapshot` 语义上不需要停驻），一个正在跑的存量段会在读档后被打散；位置不动，理论上安全，异常时按 `LEGACY-ARTIC-DEARTIC` 行定位。
3. **一次性**：只对存档里已存在的链生效；`CmdMakeSegment` 仍按第 176 轮口径先打散再升段（源头已封），不会再产出该组合。
4. **不可逆**：打散会改写存档态（保存即固化）。**旧档请先备份 .sav 再读**。
5. **未覆盖**：`R3RDearticulateOneGroup` 的前提是「父车后至少有一个真部件」；§未确认项 R-3 那类停驻链（`tOrd=0`）若挂在非段边界，本轮未加额外防护。
6. **未做**：A 与 B **未实现**，KI-274 保持未修。

### 9.6 复测判据（改用本 exe，游戏内）

1. **迁移发生**：读入该存档，日志出现 `LEGACY-ARTIC-DEARTIC head=0 n=24`（或对应链头）**恰好一次**；同档二次读档仍恰好一次（不是每帧刷）。
2. **等待保住**：全程**不得**再出现 `from=0 to=1 type_from=17` 的 `ARTIC-SKIP-R3R`；等待链停在原库 `1,11` 不动。
3. **可被挂**：机车 24 应能挂上（`COUPLE-OK loco=24 rear=…`），`CPL-GATE reject=target-not-wait` 不再出现；`COUPLE-FAIL`/`COUPLE-DEST-EMPTY` 循环消失。
4. **原生铰接车不受影响**：GRF 自带铰接车（无 ★/⊗）读档后**不得**出现 `LEGACY-ARTIC-DEARTIC`，外观/编组/性能逐字不变。
5. **段结构保持**：迁移后该链仍是多段（`CHAIN-ATTRS … SEG=` 与迁移前一致），仍可被拖动、解挂、翻向。
6. **第 176 轮判据 7 不退**：机车 48 那条 `GOTO_COUPLE` 照旧被跳。
7. **引擎稳定性**：读档后 5 分钟无崩溃、无 `assert`；接缝间距/图像无异常（关注是否出现第 180 轮 §未确认项所述 8px 残差）。

### 9.7 仍未做

1. 无实机复测（本轮只有编译自证）。
2. A/B 未实现；KI-273 的「存量链未打散」由 C 在读档边收口，但**运行时新产生**的未打散路径仍按第 176 轮口径被堵、不被迁移。
3. §未确认项 R-1/R-2/R-3 未继续深挖。
