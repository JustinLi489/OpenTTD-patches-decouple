# 第 458 轮：玩家三条追问的答复与落盘（2026-11-18）

玩家本轮原话（逐条保留）：

> 「KI-656，你的意思是这只是显示问题？我有点怀疑这不仅是显示问题。但是可以按照你推荐的修」
>
> 「KI-657，讲大白话，我听不懂」
>
> 「KI-653 改装信息报错，C:\Users\冯洁敏\Pictures\Screenshots，11月17日那张截图，你能否用OCR读取文字。
> 当时涉及的grf是中国火车包，触发操作是普通的购买了一列火车（怀疑跟之前的ctrl+左键反转有关），
> 此外，我跑了一遍日志，操作为：购买列车跳报错，设为段，存档，读档，发现读档前后数据变为三倍
> （可能对应三节铰接式）」
>
> 「剩余的应该就是你自己的读数了吧。」「请把这些落盘」

---

## 一、KI-653 新证据（OCR 实取）

### 1.1 截图与 OCR

- 文件：`C:\Users\冯洁敏\Pictures\Screenshots\屏幕截图 2026-11-17 165924.png`
  （38 022 B，LastWriteTime 2026-11-17 16:59:24）
- 工具：`R3R_shot_ocr.ps1 -Count 1`（`Windows.Media.Ocr`，`zh-Hans-CN`）
- OCR 原始输出（逐字，含误识）：

```
[01] NewGRFI'#ä@ 火 车 0.4 、
、 2 " 供 了 钅 昔 误 的 信
[02] 贮 买 列 表 中 DF3 〔 丙
丙 燃 〕 ． 的 貨 物 / 改 装 信
信 与 贮 买 后 的
[03] 信 不 同 。 这 可 能 会
 与 致 自 动 更 新 芄 自 动 替
替 换 时 无 法
[04] 正 确 改 装 ] 比 载
```

### 1.2 OCR 还原（按已知语言串对齐，非新证据）

由 `src\lang\*.txt` 的 `STR_NEWGRF_BUGGY` + `STR_NEWGRF_BUGGY_ARTICULATED_CARGO`
（繁中原文：「購買清單中「{1:ENGINE}」的貨物或改裝資訊與購買後的資訊不同。這可能會導致
自動更新或自動替換時無法正確的改裝該車輛」）对齐，弹窗实际内容为：

> **NewGRF「中国火车 0.4」提供了错误的信息**
> **购买清单中「DF3（…内燃?）」的货物/改装信息与购买后的信息不同。**
> **这可能会导致自动更新或自动替换时无法正确改装该车辆。**

⇒ **命中判据确认**：这不是泛化的 NewGRF 报错，而是
`CheckConsistencyOfArticulatedVehicle()`（`src\articulated_vehicles.cpp:338-374`）的
**`GRFBug::VehRefit` + `STR_NEWGRF_BUGGY_ARTICULATED_CARGO`** 分支（`:371-372`）。
`ShowNewGrfVehicleError()` 内有 `if (!grfconfig->grf_bugs.Test(bug_type))` ⇒ 每 GRF 每局只弹一次。

### 1.3 玩家补充的三项输入（关键）

| 项 | 值 | 作用 |
|---|---|---|
| 涉事 GRF | **中国火车包（弹窗显示 `中国火车 0.4`）** | 决定 `IsArticulatedPart` 结构与 CB36 回调 |
| 触发操作 | **普通购买一列火车**（玩家怀疑与 Ctrl+左键反转有关） | 钉死触发时机 = **造车命令内**，不是读档 |
| 复现链 | **购买 → 弹报错 → 设为段 → 存档 → 读档 → 数据变三倍（疑对应三节铰接）** | 把 KI-653 与 KI-656 串成同一条链的两个症状 |

### 1.4 定性（本轮新增，可证伪）

**H-653-A（强）——弹窗发生在「购买那一刻」，因此与读档/打散无关，只能来自造车期容量口径差。**
机制链：
1. 造车走 `CmdBuildRailVehicle` / 造车厢路径 → `AddArticulatedParts()` 生成真 artic 部件 →
   `ConsistChanged(CCF_ARRANGE)` → 逐节 `new_cap = e_u->DetermineCapacity(u)`（`src\train_cmd.cpp:614`，CB36 实算）。
2. GRF 的 CB36 对**新造部件**返回 0（部件容量由父车承担，这是正常口径）；上游此时把该节 `cargo_cap` 清 0。
3. **KI-641 补丁**（`src\train_cmd.cpp:614-626`）的保护条件：
   `u->IsArticGroupMember() && !u->IsArticGroupHead() && new_cap == 0 && u->cargo_cap > 0`
   ⇒ 对新造**真** artic 部件同样成立（`Train::IsArticGroupMember()` = `IsArticulatedPart() || 组角色位`，
   见 `src\train.h:212-215`），把部件旧容量**保留**下来。
4. 于是实车某节的某货种容量非 0，而购买列表侧 `purchase_default_capacity` 该货种为 0 ⇒
   `carries_more = true` ⇒ 弹窗。

**该假设的可证伪点**：若 `BUY-REFIT` 探针行显示 `more=0` 却仍弹窗，则 H-653-A 被否，
需转向 ①/②（refit 掩码并集/交集）分支，即 `VehRefit` 不只由 `carries_more` 触发。

**H-653-B（强）——「读档后数据×3」与 KI-656 是同一个缺陷，不是第二条。**
依据：`CheckConsistencyOfArticulatedVehicle()` 只在造车命令里调用（`src\train_cmd.cpp:2327` 造车厢 /
`:3641` 造机车），读档路径根本不走 ⇒ 读档不会弹窗；玩家描述的"报错→设为段→存档→读档→×3"
说明读档后的 ×3 来自**「设为段」时的打散**被存档带出，而"弹窗"是打散**之前**的另一症状。
两者共享同一个根：**打散后每节的容量/override 口径与真铰接不一致**。

**H-653-C（待验证，玩家怀疑）——Ctrl+左键反转是否参与。**
反转（`ReverseTrainSwapVehicles` / 逻辑翻）不改 `cargo_cap`、不改 override 数值，只改方向/★/段边界；
按代码它不可能直接造成 ×3。但"反转 → 设为段"两条命令叠在同一列车上时，打散快照取的是反转后的链
（`GetWeightSnapshot()` 沿 `Next()` 求和），求和口径若已含重复计入，会把 3× 烘进 override。
⇒ 需日志区分：反转前/后各一次 `CARGOWEIGHT-VEH` 的 `ov=` 读数。

### 1.5 需要的日志（下一次复跑即可）

`build\R3R_debug.log` 中按顺序检索：
- `BUY-REFIT eng= … more=` / `BUY-REFIT-PART` / `BUY-REFIT-PP`（购买那一刻，判定 H-653-A）
- `LEGACY-ARTIC-DEARTIC` / `R3R-CLONE-GROUP-MISMATCH`（是否有读档打散）
- `CARGOWEIGHT-VEH … ov=`（三节各自 override 是否为正常份额）
- `SEGSAVE-PARK`（与 KI-658 同族，判定场景是否叠加）

---

## 二、KI-656：不是"只是显示问题"——两层机制分开说

玩家质疑成立。上一轮把三条读数合并成"显示问题"是不准确的，必须拆成两层：

### 2.1 容量（装载能力 +1）：**真实运力，不是显示**
- 打散代码 `R3RDearticulateOneGroup()` **从不写 `cargo_cap`**。
- 容量每次 `ConsistChanged()` **逐节重算**（`src\train_cmd.cpp:614` → `DetermineCapacity` → **NewGRF CB36**），
  实参是该节车，回调能看见 artic 角色 / `first_engine` / consist 内位置（0x40/0x41）。
  而打散改的**正是**这层身份 ⇒ 同一节车在"真 artic 部件"与"打散组成员"两种身份下，
  CB36 返回的容量可以不同。
- 既有护栏只拦"**回落成 0**"（`new_cap == 0` 就保留旧值），**完全不拦"涨上去"**。
- 显示端 `src\train_gui.cpp:610-635` 沿 `Next()` 逐节累加 `cargo_cap` 且**不去重**
  ⇒ 若三节各自算出 1 人份容量，界面就是 3 人份；**这 3 人份同时也是真实装货上限**（`cargo_cap` 参与装载判定）。
⇒ 结论：+1（或 ×3）**会真的多装货**，属真实缺陷，不是 UI 修饰。

### 2.2 重量 / 功率 / 最大牵引力（×3）：**看 override 是否被清**
- 打散不是改数据而是**均分烘焙**：`R3RDearticulateOneGroup()`（`src\vehicle_cmd.cpp:549-566`）给组头
  `w_q+余数 / p_q+余数`、给每节 `w_q / p_q`，三节共用同一 `max_speed_override`。
- 耗用端在引擎/车厢身份门禁**之前**优先读 override（`src\train.h:500-506`、`src\train_cmd.cpp:25974-25980`）
  ⇒ **override 在则总量守恒**。
- override 是**存档字段**（`src\saveload\vehicle_sl.cpp:209-211`，起始 `SLV_R3R_ARTIC_OVERRIDE`）
  ⇒ "读档丢 override"不成立。
- 能放大成 N 倍只有两处：
  1. `R3RRearticulateOneGroup()`（`src\vehicle_cmd.cpp:713-715`）把三件 override 清回 `UINT16_MAX`；
     若"判定重新铰接"但部件并未真正变回 artic part ⇒ 每节回落成各自引擎值 ⇒ N 倍。
  2. 快照本身已被顶高：`GetWeightSnapshot()/GetPowerSnapshot()` 沿链求和，若上一次打散已把总量顶成 3×，
     再打散就把 3× 烘进新份额，三倍跟着链走（**级联放大**）。
- 判别：报三倍时三节 `CARGOWEIGHT-VEH` 的 `ov=` 若都是 `65535` ⇒ 命中"override 被清"；
  若 `ov` 是正常份额而界面仍三倍 ⇒ 三倍在读取端聚合。

### 2.3 玩家已批准的修法（本轮落地）

> 打散时对**非组头成员**显式把 `cargo_cap` 置 0。

理由：这与"真 artic 部件容量由组头承担"的原生口径一致 —— 打散后组的语义仍是"一组车共用一个容量口径"，
容量只该由组头那节贡献，其余节为 0。这样：
- 显示端逐节累加不会翻倍；
- 真实装载上限也回到 1×；
- `carries_more`（KI-653）的"实车侧多出容量"这一条来源同时被压制。

**实现约束（必须遵守，否则被下一次 `ConsistChanged` 覆盖）**：
置 0 必须发生在该链 `ConsistChanged()` **之后**，或改为在 `ConsistChanged()` 的容量分支里
对"打散组成员且非组头"直接令 `new_cap = 0`（即把护栏从"只拦回落"扩成"强制归零"）。
两者取其一，不得只写一次就以为结束。

### 2.4 复测判据（KI-656）

1. 三节铰接车「设为段 → 存档 → 读档」后，装载能力与打散前一致（不再 +1 / ×3）。
2. 三节 `CARGOWEIGHT-VEH` 的 `ov=` 均非 `65535`，且 `cached_total_weight` 与打散前相同。
3. 负判据：原生 GRF 铰接车（未打散）容量逐字不变。
4. 负判据：单节车 / 普通车厢链的 `cargo_cap` 逐字不变。

---

## 三、KI-657：大白话版（替换上一轮的术语写法）

### 3.1 现象（不用术语）

有一列车停在离目的地还有一段路的地方，**原地不动**。系统日志显示：
- 车**实际站在** `211,116`；
- 车**要去的站**是 `221,129`；
- 但系统给它**订的那条"专用通道"**却是从 `218,122` 一直走到 `218,138`。

也就是说：**这条通道的入口既不是车现在的位置，也不是它要去的地方**，而是离车约 22 格之外的一处，
终点还正好是它**上一次**订到的同一个格子（同一格被订了两遍）。

通道订好了、系统也报"成功"，但车一步没走。

### 3.2 为什么麻烦

R3R 的"订通道"是**排他**的：一条通道订下去，别的车就进不来。所以如果这列车订了一条自己**根本不会走**
的通道（入口离车 22 格），那么：
- 这 22 格沿线会被白白占住，别的车只能干等；
- 这列车自己也不会动，因为它的行进判据看的是"脚下这段能不能走"，而不是"22 格之外那段"。

**目前不能确定**的是：到底是"订错了位置"，还是"订对了但车被别的原因卡住"。这两种修法完全不同，
所以不能直接改代码。

### 3.3 三个方向（请挑一个）

**(a) 钉死"通道入口"的算法——只调查，不改行为（推荐）**
把"这条通道为什么从 `218,122` 开始"用只读日志印出来（谁下的命令、当时的起点、上一段终点、车的位置）。
代价：一次复跑；风险：**零**（只加日志）。
得到答案后，要么证明是 bug（改入口），要么证明是车的另一个问题（另查）。

**(b) 强制入口 = 车脚下那一格**
只要入口不是车自己的位置，就**不许订**。
代价：小改动；风险：**可能误伤"提前续订"**——列车进站前需要提前把前方通道订好，一刀切会让它
每走一格才订一格，可能出现"走走停停"或干脆订不到位。

**(c) 终点必须是"能安全停住"的格**
订到道岔中间、站台尽头被占的格子时，往前挪到安全格，挪不到就不订。
代价：中；风险：可能出现"订不到位 ⇒ 车停着等"，观感上比现在更频繁地不动。

**建议先做 (a)**：现在连"入口为什么在那里"都没钉死，直接改 (b)/(c) 有 50% 概率改错地方。

---

## 四、本轮落盘清单

1. 本文件（新建）= 三条答复的完整版 + OCR 原文 + 玩家输入。
2. `R3R_KNOWN_ISSUES.md` 追加「第 458 轮」小节：KI-653 升级为「新证据齐、H-653-A/B 待日志验证」，
   KI-656 状态改为「非显示问题（已定性到两层）+ 玩家已批准修法 + 已落地/已编译」，
   KI-657 状态维持「部分定性」并改用大白话 + 三选一。
3. `R3R_round456_five_issues_memo.md` §9.5 状态表同步刷新（原写"KI-653 未修，等玩家给报错原文"
   已过时）。

---

## 五、第 459 轮新证据：×3 只在读档后发生（2026-11-19）

### 5.1 玩家原话

> 「我向你报告，那个所谓的『数据翻三倍』每次都发生在读档之后，我认为其实我们已经可以有一个怀疑点的了」

### 5.2 这条证据的效力：三个候选压成一个

§2.2 列的两个放大点，都要求"打散/重新铰接"这个**动作**发生，而读档本身不做这个动作：

| 候选 | 是否需要玩家动作 | 与"只在读档后"是否相容 |
|---|---|---|
| ① `R3RRearticulateOneGroup()` 清 override | 是（重铰接/车库编辑/耦合） | **不相容 ⇒ 排除** |
| ② 快照已被顶高，再打散把 3× 烘进新份额 | 是（打散） | **不相容 ⇒ 排除**（唯一例外见 5.4） |
| G-456-2 同一链一次读档被 `ConsistChanged()` 调多次 | 否 | **唯一相容 ⇒ 主嫌** |

⇒ 改登记为 **G-459-1（待验证）**：读档路径对同一条链重复调用 `ConsistChanged()`。

### 5.3 代码支撑（第 459 轮读码结论，未改任何代码）

`Train::ConsistChanged()`（`src\train_cmd.cpp:457`）**不清 `this` 自己的三项车况缓存**：

```470:472:src/train_cmd.cpp
	this->gcache.cached_total_length = 0;
	this->compatible_railtypes = {};
	this->tcache.cached_num_engines = 0;
```

只清 `cached_total_length` / `compatible_railtypes` / `cached_num_engines`，**`cached_weight` / `cached_power` / `cached_max_te` 不在其中**。

后段的成批清零从第二节开始，**`this` 被排除在外**：

```780:788:src/train_cmd.cpp
		for (Train *u = this->Next(); u != nullptr; u = u->Next()) {
			u->vcache.cached_max_speed = 0;
			u->gcache.cached_weight = 0;
			u->gcache.cached_max_te = 0;
			u->gcache.cached_axle_resistance = 0;
			u->gcache.cached_max_track_speed = 0;
			u->gcache.cached_power = 0;
			u->gcache.cached_air_drag = 0;
			u->gcache.cached_total_length = 0;
```

入口断言是 `IsFrontEngine() || IsFreeWagon() || IsFrontWagon()`（`:461`），R3R 的假引擎段头也可能满足 ⇒
"一条链上存在多个可调用点"不是假想。`:504` 的 `dbg_assert_msg(u->First() == this)` 保证 `this` 必须是链头，
所以重复调用只可能来自**读档流程对同一链头调用多次**，不会来自"中间节自调"。

读档路径的两处调用点（已核对，见 `src\sl\vehicle_sl.cpp`）：

1. `AfterLoadVehiclesPhase2()` 主循环内，对每条 `IsFrontEngine() || IsFreeWagon()` 的车调一次
   `ConsistChanged(CCF_SAVELOAD)`；
2. 紧随其后的 `R3RRebuildCouplePriorities(t)`（`part_of_load` 为真时）→ 若命中
   `R3RDearticulateLegacyArticSegment()` 则再补一次 `ConsistChanged(CCF_SAVELOAD)`。

⇒ 同一条链一次读档至少 2 次。**若汇总写法是累加，就是 1×+1×+1× = 3×。**

### 5.4 仍缺的最后一环（不得当结论用）

**`this->gcache.cached_weight` 的汇总写法未确认**：是 `+=` 还是"局部变量求和后 `=`"。
这一行决定了 G-459-1 成立与否：

- 若是 `+=` ⇒ 坐实，每次读档必 ×N；
- 若是 `=` ⇒ 累加不成立，三倍只能来自**读取端聚合**（`GetDisplayWeight` / 段级统计 / TE 汇总重复计数），
  那就要改查 `train_gui.cpp` 与 `GetTractiveEffort()` 一侧。

### 5.5 两条零成本判别

**判别甲（推荐，一次操作）**：读档后对三节各跑一次 `dump_vehicle <id>`，看 `st:` 段是否含 `A`
（`GVSF_ARTICULATED_PART`，见 KI-268 的 dump 口径）。

- **含 `A`** ⇒ 身份没变，仍是真 artic 部件 ⇒ 三倍只可能来自重复累加 ⇒ **命中 G-459-1**；
- **不含 `A`** ⇒ 身份在读档时被改（★/⊗ 链命中 `R3RDearticulateLegacyArticSegment` 被打散）
  ⇒ 回到候选①/②复核，且要查 `IsArticGroupHead` 是否真的落到组头。

**判别乙（决定性）**：`CARGEWEIGHT-VEH` 三节 `ov=` 全 `65535` 且 `CHAIN-ATTRS wt=`
≈ `3 × 单节 ew=` 而链上只有 3 节 ⇒ 重复累加坐实。

### 5.6 尚需玩家回答的一个问题（决定 cache 是否存档）

**读一次档变 3× 之后，再存档再读档，是变成 9× 还是仍然 3×？**

- **仍然 3×** ⇒ 每次读档都从干净状态重算，问题在"读档时算多了"，方向最干净；
- **变 9×** ⇒ `cached_weight` 是存档字段且被写回文件，问题会随存档级联放大，优先级要提到最高。

### 5.7 与第 458 轮修法的关系

**两层不同源，不要合并**：

- **容量 +1**：第 458 轮已落地"打散组非组头成员 `new_cap = 0`"，属身份层，与读档次数无关；
- **重量/功率/TE ×3**：本轮锁定的 G-459-1 属累加层，第 458 轮的修法**完全没覆盖**它。

⇒ 如果玩家观察到"容量已经正常了但重量还是三倍"，正好反证两层不同源、G-459-1 独立成立。

### 5.8 同日自我修正：G-459-1 被证伪，定位改到 `UpdateWeight()` 的均分

**§5.3~5.5 的"重复调用累加"假设不成立**，已读码确认：

- `GroundVehicle::UpdateWeight()` 末尾是**赋值**不是累加：
  `this->gcache.cached_weight = std::max(1u, weight);`（`src\ground_vehicle.cpp:182`）。
- `weight` 是循环外初始化的局部变量（`:108 uint32_t weight = 0;`、`:162 weight += current_weight;`）。
- ⇒ `ConsistChanged()` 被调 N 次得到同一个结果，**不会翻倍**。§5.2 的表格与 §5.5 判别甲/乙作废，
  仅保留"×3 只在读档后"这条事实本身。

**新的落点**：

```133:142:src/ground_vehicle.cpp
	for (T *u = T::From(this); u != nullptr; u = u->Next()) {
		uint32_t current_weight = u->GetCargoWeight();
		if (u->IsArticulatedPart()) {
			current_weight += articulated_weight;
		} else {
			uint16_t engine_weight = u->GetWeightWithoutCargo();
			uint part_count = u->GetEnginePartsCount();
			articulated_weight = engine_weight / part_count;
			current_weight += articulated_weight + (engine_weight % part_count);
		}
```

原生口径：父车拿 `ew / pc + ew % pc`，每个 artic part 继承 `articulated_weight`
⇒ **三节总和恒等于 `ew`（1×）**。要三倍只有两条路：`pc` 塌成 1，或 `ew` 变成整组值且 `pc == 1`。
两条都意味着"这节车不再被认成 artic 组的一部分" ⇒ **身份问题，不是算术问题**。

**这意味着玩家"每次都在读档后"的直觉仍然成立且更精确**：读档路径改了某节的 `IsArticulatedPart()`
或组角色位（`R3RRebuildCouplePriorities()` → `R3RDearticulateLegacyArticSegment()` 的打散行为、
`ConnectMultiheadedTrains` 的重链、`R3RCopyClonedSegmentTraits` 这类身份重建），
而 `UpdateWeight()` 只是把这个错误忠实地算成 3×。

**取数障碍**：`CARGOWEIGHT-VEH` 的门禁（`:121-125`）只对"带 override 或 R3R 组角色位"的链开日志，
纯原生 GRP 铰接车一条都不打 ⇒ 必须先放宽门禁（登记 A-459-1）。

**判据**：三节 `pc=3` ⇒ 正常；`pc=1` ⇒ 命中；再看三节 `ew` 是否相等且等于 `CHAIN-ATTRS wt=` 的 1/3。

## 六、第 462 轮：玩家新证据「×3 每次都发生在读档之后」——候选收敛为一条

### 6.1 玩家原话（2026-11-19）

> 「我向你报告，那个所谓的『数据翻三倍』每次都发生在读档之后，我认为其实我们已经可以有一个怀疑点的了」

### 6.2 这条证据的过滤力

§5.2 表里的三个候选，按「是否只在读档时发生」过筛：

| 候选 | 触发条件 | 与「每次都在读档后」是否相容 |
| --- | --- | --- |
| ① `R3RRearticulateOneGroup` 清 override | 玩家主动重铰接 | ✗ 需要主动动作 |
| ② 快照被顶高后再打散 | 玩家主动打散/升段 | ✗ 需要主动动作（唯一例外见下） |
| ③ 读取端聚合 / 重复累加 | 读档 | ✓（但 §5.8 已按读码证伪） |

唯一能让 ①② 也落在读档时刻的通道：**读档路径自己会打散**——
`src\train_cmd.cpp:2862` 的 `R3RDearticulateLegacyArticSegment()`（由 `R3RRebuildCouplePriorities()`
在 `part_of_load` 时调用，且只对"带 ★/⊗ 的真 artic 链"生效）里调 `DearticulateChainWithSnapshot(head)`
并紧跟 `head->ConsistChanged(CCF_SAVELOAD)`。

⇒ 于是剩下的嫌疑只有一句：**打散这套"烘焙层"（`weight_override` + 组角色位 + artic 位）
在读档后整体失效，三节退回各自 GRF 记录值 → 恰好 ×3。**

### 6.3 代码支撑（第 462 轮读码，未改任何代码）

1. `src\ground_vehicle.cpp:133-142`（见 §5.8）：身份在 ⇒ 三节总和恒 `= ew`（1×）。
   身份不在 ⇒ 每节各取自己的 `GetWeightWithoutCargo()`。
2. `src\ground_vehicle.cpp:182` 是赋值（`cached_weight = std::max(1u, weight)`）⇒ 多调无害，
   **"读档算了两次"这条彻底排除**（§5.8 的结论在 6.2 的过筛里也一致：③被删掉）。
3. `src\vehicle_cmd.cpp:531` 的 `R3RDearticulateOneGroup()` 用
   `const uint16_t total_weight = v->GetWeightWithoutCargo();` 做快照 ——
   而 `GetWeightWithoutCargo()` **优先返回已有 override**。⇒ 这套烘焙不是幂等的：
   若某节已有 override 又被当作组头重烘一次，分母会变小（`n_total` 由 `IsArticulatedPart()` 数，
   与 `HasArticulatedPart()` 用的角色位判据**不是同一个** ⇒ 存在"角色位在、artic 位已清"的
   混合态，此时 `HasArticulatedPart()==true` 但 `n_total==1`）。
4. 该函数**只烘焙 `weight_override`**（`vehicle_cmd.cpp:565-575`：组头 `w_q + w_r`、成员 `w_q`），
   **不烘焙 power / max_te** ⇒ 功率与最大牵引力的 ×3 不需要 override 参与，
   只要"三节各自算作引擎"就会在 `ConsistChanged` 里被累加成 3×。这与"重量需要 override 也一起坏"
   形成**不对称**，是很好的分离判据（见 6.5 之三）。
5. `src\sl\vehicle_sl.cpp:209-211` 三个 override 都是存档字段（`SLV_R3R_ARTIC_OVERRIDE` 起）
   ⇒ 正常往返不会丢；**但读档时这四个字段只在 `SlIsSavegameVersionBefore(SLV_R3R_ARTIC_OVERRIDE)`
   为假时才读**，写于更早 exe 的旧档里它们根本不存在。

### 6.4 ×3 的算术必要条件（把猜测收紧成一个可验命题）

要让三节合计恰为 3×，只能满足下面两条之一：

- **M1（身份+烘焙全丢）**：三节 `IsArticulatedPart()==false`、无 `ArticGroup*` 角色位、
  `weight_override==UINT16_MAX` ⇒ 每节按自己 GRF 记录重量计；中国包的 MUs 通常把每节原型的
  weight/power/te 都填成**整组值**（原生引擎里这种填法是无害的，因为 artic part 的权重被忽略）
  ⇒ 三节各一整组 = 3×，容量同样逐节相加 = +1。
- **M2（烘焙值本身被烘成整组）**：三节 `weight_override` 都 ≈ 整组重量
  （= 组头重烘分母塌成 1，或重烘时快照取到了已 3× 的值再除以 1）。

两者对 `CARGEWEIGHT-VEH` 的表现**不同**，故可用现有探针区分，不必先改代码。

### 6.5 三条零成本判别（都用现成 exe / 现成探针）

1. **身份**：读档后对三节各跑 `dump_vehicle <id>`：
   - `st:` 含 `A` ⇒ 仍是真 artic 件（M1/M2 都不成立，须换方向）；
   - `st:` 无 `A` 且 `tf:` 无 `H`/`M` ⇒ **M1**（角色位与 artic 位全丢）；
   - `st:` 无 `A` 但 `tf:` 有 `H`/`M` ⇒ 角色位在、artic 位丢 ⇒ 混合态（`n_total` 塌成 1 的温床，偏 M2）。
2. **烘焙值**：日志里该链有没有 `CARGOWEIGHT-VEH … ov=`：
   - 一条都没有 ⇒ 链上既无 override 也无角色位 ⇒ **M1 实锤**（探针门禁本身就是证伪器）；
   - 有且 `ov=65535` ⇒ 角色位在、override 丢；
   - 有且 `ov≈整组重量` ⇒ **M2**；`ov≈整组/3` ⇒ 烘焙正常，问题在别处。
3. **新档对照（最关键的一条）**：另开一个存档，**现场买一列同样的三节车**，`存档 → 读档`
   看是否也 ×3。
   - 也 ×3 ⇒ 与旧档/版本门无关，是**当前 exe 的读档路径**在破坏身份或烘焙 ⇒ 直接可修；
   - 只有老存档 ×3 ⇒ 命中 6.3 第 5 条（`SLV_R3R_ARTIC_OVERRIDE` 之前的存档没有这三个字段）
     ⇒ 属"旧档迁移"问题，修法与前者不同（读档时重烘一次即可），优先级也低一档。

### 6.6 与 KI-659（`sumpw=414` 可能只是探针口径）的关系

第 460 轮日志里 `MAKESEG-WEIGHT … sumpw=414=138×3` 在打散前后逐字相同 ——
这条**只能说明探针在逐节累加 `RailVehInfo(engine_type)->power`**，不能说明界面读数。
真正的判据是 `GroundVehicleCache::cached_power`（`ConsistChanged` 只取"代表引擎"那一份，
真 artic 阶段恒 1×）。所以：**`sumpw` 归 KI-659，`×3` 归本条**，两件事不要再混用一份读数。

### 6.7 待玩家回答（决定下一步是"修读档路径"还是"修旧档迁移"）

- **Q-462-1**：现场买一列同款三节车 → 存档 → 读档，会不会也 ×3？（= 6.5 之三）
- **Q-462-2**：读档后三节 `dump_vehicle <id>` 的 `st:` / `tf:` 各是什么？（= 6.5 之一）
- **Q-462-3**：这个存档最早是哪一版 exe 建的？（若跨过 `SLV_R3R_ARTIC_OVERRIDE` 之前的版本，
  M1 的可能性直接升到最高）

## 七、第 463 轮：玩家新证据「只有设为段的三节 ×3，散链不变」——假设换轨

### 7.1 玩家原话（2026-11-19）

> 「我发现，读档之后，只有设为段的三节铰接式功率与重量乘三，而散链则保持原属性」

### 7.2 这条证据做了什么

它**直接否掉**了 §6.4 的 M1 表述（"烘焙层整体失效"）：如果真是烘焙层 + artic 位整体失效，
散链（同样是三节铰接单元）也会退回逐节 GRF 记录值 ⇒ 也该 ×3。**散链不坏** ⇒ 问题不在
"读档把所有东西都弄坏了"，而在**只有段链才走的那条代码路径**上。

### 7.3 两条链的差别（关键结构事实）

| 链型 | 读档后 `IsArticulatedPart()` | `UpdateWeight()` 走哪一支 | 吃不吃 `weight_override` | 吃不吃各节 GRF 记录重量 |
| --- | --- | --- | --- | --- |
| 散链（无 ★/⊗） | **真**（artic part 位在） | `src\ground_vehicle.cpp:133-142` 的 artic 分支 | 不吃 | **不吃**（parts 用 `articulated_weight` 均分） |
| 段链（有 ★/⊗） | **假**（被打散，成员 `ClearArticulatedPart()`） | 同一函数的 else 分支 | **完全依赖它** | **吃**它 |

段链被打散的位置就是读档路径本身：`src\train_cmd.cpp:2862` 的
`R3RDearticulateLegacyArticSegment()`（由 `R3RRebuildCouplePriorities()` 在 `part_of_load` 时调用，
判据 = 链上有 ★/⊗ **且** `R3RChainHasRealArticPart()`）→ `DearticulateChainWithSnapshot()`
→ `head->ConsistChanged(CCF_SAVELOAD)`。

⇒ 一句结论：**散链的读数完全由 artic 位决定（没人动它，所以不坏）；
段链的读数完全由烘焙值 `weight_override` 决定 —— 所以坏的一定是烘焙值这一层。**

### 7.4 修订后的统一机制（比 §6.4 更窄，且同时解释 ×3 与 +1）

打散后的段链，烘焙层在读档往返后没有生效 ⇒ 三节各自回落到自己的 GRF 记录值：
- 重量 ×3、功率 ×3、最大牵引力 ×3：中国包给每个 artic part 原型的
  `weight/power/max_te` 都填成**整组值**（在原生引擎里这是完全无害的，因为 artic part 的这些
  数值本来就被忽略）⇒ 三节各一整组 = 3×。
- 容量 +1：同一口径（逐节 record 容量相加，而非整组值）。

⇒ **×3 与 +1 是同一个机制的两个面**，都落在"段链打散后失去权威总量"上，不是两条独立缺陷。

### 7.5 三个候选（按可行性排序，均可由现有探针区分）

- **H-463-A（最可能）烘焙值没落盘/没读回**：`src\sl\vehicle_sl.cpp:209-211` 的
  `SLE_CONDVAR(..., SLV_R3R_ARTIC_OVERRIDE, SL_MAX_VERSION)` 只在存档版本 ≥ 该常量时读写
  ⇒ 写于更早 exe 的存档读回来 `weight_override == UINT16_MAX`。
  判据：`CARGOWEIGHT-VEH … ov=65535` 且 `tf:` 仍有 `H`/`M`（角色位没被动）。
- **H-463-B 烘焙值被烘成整组**：`src\vehicle_cmd.cpp:531` 的快照 `GetWeightWithoutCargo()`
  **优先取已有 override**（不幂等），而入口判据 `HasArticulatedPart()`（角色位口径）与数分母的
  `nxt->IsArticulatedPart()`（subtype 口径）**不同源** ⇒ `n_total` 可能塌成 1，
  烘出"每节 = 整组"。判据：`ov≈整组重量`。
- **H-463-C 打散后又被清**：读档路径里若有清 override 的调用排在
  `DearticulateChainWithSnapshot()` **之后**（`R3RRearticulateOneGroup` 一类）。
  判据：`ov=65535` 且 `tf:` 里 `H`/`M` 也没了（`R3RRearticulate` 会连同角色位一起清）。

### 7.6 判别表（读档后对三节各跑 `dump_vehicle <id>`，再看日志 `CARGEWEIGHT-VEH`）

| `st:` 含 `A` | `tf:` 有 `H`/`M` | `ov=` | 判定 |
| --- | --- | --- | --- |
| 无 | 有 | 65535 | **H-463-A**（烘焙值没读回来，角色位在） |
| 无 | 有 | ≈整组重量 | **H-463-B**（烘错了，分母塌成 1） |
| 无 | 无 | 无该行 | **H-463-C** 或更早的身份重建（角色位也丢了） |
| 无 | 有 | ≈整组/3 | 三个候选都不成立 ⇒ 转向 `GetEnginePartsCount()` 与容量层 |
| **有 `A`** | — | — | 段链**没**被打散 ⇒ §7.3 前提错，须回头重取 |

### 7.7 为什么我们之前一直没看到散链的对照样本

`src\ground_vehicle.cpp:121-125` 的探针门禁只对"链上存在 override 或 R3R 组角色位"的链开日志。
散链（真 artic、无 ★、无角色位、无 override）**天然一条都不打** ⇒ 之前所有 `CARGOWEIGHT-VEH`
样本都是段链的，噪声为零但也无从对照。本轮玩家这句"散链不变"正好补上了这个对照。

### 7.8 修法方向（未落地，等 7.6 定案）

1. 若 H-463-A ⇒ **读档补烘**：在 `R3RRebuildCouplePriorities()` 里对"有 `ArticGroupHead` 但
   `weight_override == UINT16_MAX`"的组按记录值重烘一次（幂等、只补空值、不覆盖已有值），
   属旧档迁移，风险最低。
2. 若 H-463-B ⇒ **修烘焙的幂等性**：`R3RDearticulateOneGroup()` 的 `n_total` 判据统一到
   `IsArticulatedPart()`（与循环同源），并且快照不取已有 override（用记录值），避免二次烘焙放大。
3. 两者都要顺带把容量层按同一口径收口（否则 +1 会在修完 ×3 后仍然存在），即口径 B
   「组头持权威总量」（见 KI-658 候选 B）。

### 7.9 待玩家回答（编号续 462）

- **Q-463-1**：这三节段链，读档后 `dump_vehicle <id>` 的 `st:` 有没有 `A`、`tf:` 有没有 `H`/`M`？
- **Q-463-2**：日志里该链三行 `CARGEWEIGHT-VEH` 的 `ov=` 分别是多少？
- **Q-463-3**（若能回忆）：段链在**读档之前**（刚设段/刚打散时）是不是就已经 3×？
  这能一刀切开"打散当下就错"（H-463-B）与"读档才错"（H-463-A/C）。

---

## 八、第 463 轮读码定案（把 §7 的三候选收敛到一条）

### 8.1 口径自证（行号 + 原文要点）

**weight 累加口径**（`ground_vehicle.cpp:105-167`，`GroundVehicle::CargoChanged()`）：
- 对每节 `!IsArticulatedPart()` 的车算 `engine_weight`，`part_count = GetEnginePartsCount()`，
  且 `articulated_weight = engine_weight / part_count`；组头再加 `engine_weight % part_count` 余数；
- 真 artic 部件走 `IsArticulatedPart()` 分支，只加 `articulated_weight`，**不看自己的记录值**；
  ⇒ 真 artic 组的总重量 = **组头一条记录值**（部件记录被引擎刻意忽略）。
- 打散后（角色位在、`IsArticulatedPart()` 已清）：每节 `part_count == 1` ⇒
  各自全额吃「自己的 `weight_override`，没有就吃自己的记录值」。

**power 累加口径**（`ground_vehicle.cpp:76-98` `CalculatePower` → `cached_power`）：
- 逐节累加 `u->GetPower() + u->GetPoweredPartPower()`；
- `train.h:591-604`：`IsArticulatedPart()` ⇒ `GetPower()` 直接返回 0（"Power is not added for
  articulated parts"）⇒ 真 artic 组总功率 = **组头一条记录值**；
- 打散后每节都会返回自己的记录功率（或 override）⇒ 逐节全额累加。

**override 优先级**：`train.h:496-515`（weight）、`train.h:569-607`（power），都是
`if (…_override != UINT16_MAX) 用 override; else 用记录值`。所以**override 在不在，直接决定
×1 还是 ×N**。

**烘焙**（`vehicle_cmd.cpp:528-566`，`R3RDearticulateOneGroup()`）：`weight`、`power`、
`max_speed` **三样都烘**，按 `n_total` 均分、余数给组头（`n_total` 由
`nxt->IsArticulatedPart()` 计数）。

**存档**：`saveload/vehicle_sl.cpp:209-211` 三个字段都是
`SLE_CONDVAR(…, SLV_R3R_ARTIC_OVERRIDE, SL_MAX_VERSION)`；`sl/saveload_common.h` 的
`SAVEGAME_VERSION = SLV_R3R_PARKED`（比它晚一档）⇒ **现版 exe 存出的档一定带这三字段**。
读档侧全库搜不到任何清空 override 的代码；唯一清空点 `RearticulateChain()`
（`vehicle_cmd.cpp:698-711`）只被降级路径 `vehicle_cmd.cpp:951` 调用，读档不走。

### 8.2 结论修正（三条）

1. **撤销 §6.3 第 4 条的错误表述**：那里写"只烘 `weight_override`，不烘 power/max_te"，
   与 `vehicle_cmd.cpp:550-566` 不符。三样都烘 ⇒ "重量 3× 而功率 1×" 不能当分离判据。
2. 反过来说，**重量与功率同时 3× 正是 override 缺失的签名**：GRF 给每个 artic part 原型写的是
   **整组值**（原生引擎里这些字段被 `IsArticulatedPart()` 拦掉，所以无害）；一旦 override 没了，
   三节各自全额累加 ⇒ 重量×3、功率×3。散链永远是 1×，因为它走的是摊分/清零分支。
3. **H-463-B 定量排除**：二次烘焙时 `n_total` 塌成 1，快照 `GetWeightWithoutCargo()` 取到的是
   头车自己的 override（≈W/3）⇒ 每节都拿 W/3，总量仍是 W，**不会放大**。要烘成 3W 必须
   "快照=记录值 W 且 n_total=1"，而这只在"override 已被清空、角色位仍在"时成立，全库唯一的
   清空点与此同路径会同时恢复 artic 位 ⇒ 该状态不存在。
4. **H-463-C 排除**：读档路径零清空点（见 8.1）。
5. ⇒ 只剩 **H-463-A 的唯一可行形态：档里根本没有这三个字段**，即该存档由
   `SAVEGAME_VERSION < SLV_R3R_ARTIC_OVERRIDE` 的旧 exe 写出（或段身份是从那种档继承来的）。

### 8.3 修法（已按此落地，见 §8.6）

不追"谁能存出旧档"，直接**按引擎自己的口径做读档归一**：

- 新增 `R3RArticBaseWeight()/R3RArticBasePower()`：override-free 的记录口径，逐字镜像
  `GetWeightWithoutCargo()` / `GetPower()` 的非 override 分支（含 PoweredWagon 附加重量、
  multiheaded 半功率、wagon 型引擎的 1 hp 地板）；
- 新增 `R3RNormaliseArticGroupBakes(head)`：遍历每个 `ArticGroupHead` 组，把组内"当前实际贡献
  之和"与组头记录总量比较，**不等才重烘**（按成员数均分、余数给组头，含 `max_speed`），
  然后 `ConsistChanged(CCF_SAVELOAD)`；
- 调用点只有读档入口 `R3RRebuildCouplePriorities()`（紧跟 `R3RDearticulateLegacyArticSegment`），
  活链不受影响；口径本来就对的组恒为 no-op ⇒ 幂等、可反复读档。

### 8.4 复测判据

1. 旧档读档后，那三节的段车重/功率回到 1×（列车窗口数值与"散链"同款单位一致）；
2. 日志出现且仅出现一次 `ARTIC-BAKE-NORMALISE head=… n=3 w=3W->W p=3P->P`；
3. 再存再读，第二次不出现该行（`w`/`p` 已相等 ⇒ 无操作）；
4. 负判据：散链读档不出现该行，且重量/功率逐字不变；正常段链（新造）读档也不出现。

### 8.5 仍未做

- 容量 +1（KI-656）：容量层不走 override，本修法不覆盖，仍按 KI-658 口径 B「组头持权威总量」；
- `max_speed_override` 只在需要重烘时顺带写，未做独立校验（缺它时结果与记录值等价，无观感差异）。

### 8.6 落地与构建自证（本节随实现回填）

- 已改 `src/train_cmd.cpp`：新增 `R3RArticBaseWeight()/R3RArticBasePower()/R3RNormaliseArticGroupBakes()`
  （定义 :2864 起，实现 :2936-2992），调用点 `R3RRebuildCouplePriorities()` 内 :8056（紧跟
  `R3RDearticulateLegacyArticSegment(chain)`）。只读档入口，幂等。
- 构建：`build\R3R_incbuild.done` = `EXIT_CODE=0`；`build\openttd.exe` @2026-11-19 01:56:59
  （53 030 400 B）晚于 `src\train_cmd.cpp` @01:52:51 ⇒ 玩家复测用的就是本修复版。

## 九、第 463 轮实测读数（2026-11-19）——对称性假设被证伪，问题分裂成三层

### 9.1 玩家原话（逐字）

> 编译已经成功，我实测，现在成段的仍然有重量变三倍，功率保持一倍（对应的，最大牵引力也变三倍）
> 此外，原本容量为0的机车，不成段的有2人载客量，成段后有1人载客量，这个载客量变化不随读档改变

### 9.2 四条读数（这是本轮唯一可信的实测事实）

| # | 读数 | 散链 | 成段 | 是否随读档改变 |
|---|------|------|------|----------------|
| R1 | 重量 | 1× | **3×** | （×3 只在读档后出现，第 462 轮已证） |
| R2 | 功率 | 1× | **1×** | 同上 |
| R3 | 最大牵引力 | 1× | **3×** | 同上（TE 由 weight×power 派生，跟着重量走） |
| R4 | 载客量（0 容量机车） | **2** | **1** | **不随读档，恒定**（第 463 轮新增，与存档无关） |

### 9.3 结论修正：§8.2 第 2 条的「对称性」是错的

§8.2 曾断言「override 缺失 ⇒ 重量与功率同时 3×」，并把它当作唯一签名。实测 **R2 功率 1×**
直接否证该断言。所以「本轮的 `R3RNormaliseArticGroupBakes` 没生效」与「power 本来就不会 3×」
两件事必须分开判，不能互证。

### 9.4 猜测根因（待验证）——三条互不依赖的机制

**G-464-1（重烘没触发，最可能）**：`R3RNormaliseArticGroupBakes()` 的 baked 判据是
`weight_override != UINT16_MAX || power_override != UINT16_MAX`（`train_cmd.cpp:2956-2962`，
任一节命中即整组跳过）。只要存档里 **有一节**带回旧值（哪怕只有 `max_speed`/部分字段），
整组被判「已烘」⇒ 一次都不重烘 ⇒ 重量仍 3×。
判别：日志里 `ARTIC-BAKE-NORMALISE` **是否出现**。没出现 ⇒ 本猜测成立。

**G-464-2（功率天然不放大）**：`R3RDearticulateOneGroup()` 对成员 `SetWagon()`
（`vehicle_cmd.cpp:557-561`，`wagon_type` 时），而功率累加只认同类引擎 ⇒ 三节里只有组头
出 P，两节成员出 0 ⇒ 功率 1×。即 **R2 是正常形态，不是缺陷**，不必修。
判别：`dump_vehicle` 三节的 `tf:` 里 `W`（Wagon）标记；与 `st:` 无 `A`。

**G-464-3（容量是第三条独立机制）**：R4「2 → 1 且不随读档」说明它既不经过 override，也与
存档无关，只由「角色位 / parts count」派生（真 artic 时按 part 分别计入，打散后被按
`GetEnginePartsCount()` 或组内均分回收）。这是 KI-656 的容量层，本轮的烘焙归一按设计不覆盖。
判别：对同一辆车 `dump_vehicle` 看 `capacity`/`cargo` 字段；真 artic 与打散各取一次对照。

### 9.5 需要玩家提供（两条即可定案 G-464-1）

1. `build\R3R_debug.log` 里搜 `ARTIC-BAKE-NORMALISE`：**有 / 没有**，有则给出该行原文
   （`head=%d n=%u w=%u p=%u spd=%u`）；
2. 读档后对那三节各跑一次 `dump_vehicle <id>`，给出 `st:` / `tf:` / `ov=` 三段
   （重点：三节的 `weight_override`/`power_override` 是否 `65535`）。

### 9.6 复测判据（下一版）

1. 旧档读档后重量回到 1×，且日志恰好出现一次 `ARTIC-BAKE-NORMALISE ... n=3`；
2. 再存再读不重复出现该行；
3. 散链读档不出现该行、数值逐字不变；
4. 容量 R4 单独一轮收口（不在本轮修法范围内）。

## 十、联合推导：三条读数互相锁死，只剩一种状态自洽

### 10.1 推导（只用 R1/R2/R3，不需要新数据）

已知代码：

- 重量：`GetWeightWithoutCargo()`（`src/train.h:496-508`）override 优先，否则
  `!IsArticulatedPart()` 取 GRF 记录；打散后每节都 `IsArticulatedPart()==false`
  ⇒ **override 缺失时每节各出一份整组重量 W**（`CargoChanged` else 分支，
  `src/ground_vehicle.cpp:138-141`，`part_count==1`）。
- 功率：`GetPower()`（`src/train.h:569-606`）override 优先；缺失时非 artic 节取记录值
  （只有 `IsEngine() && railveh_type==Wagon` 才有 `max(power,1)` 地板）。
- 最大牵引力：`CalculatePower()`（`src/ground_vehicle.cpp:76-98`）**只对 `current_power > 0`
  的节**累加 `u->GetWeight() * u->GetTractiveEffort()`。

代入 R1/R2/R3：

| 读数 | 反推出的状态 |
|------|--------------|
| R1 重量 3× | 三节的 `weight_override` **全部** = UINT16_MAX（缺失） |
| R2 功率 1× | 三节 `power_override` **存在**且合计 = P（若缺失则三节各出 P ⇒ 3×） |
| R3 TE 3× | 三节 **都**满足 `current_power > 0` 且各自重量 = W ⇒ 3 × (W × te) |

⇒ **唯一自洽状态：`weight_override` 缺失、`power_override` 存在（部分烘焙）。**

### 10.2 由此得到两条判决

**判决 1：G-464-2（「功率 1× 是天然正常形态」）被证伪。**
若成员 `power==0`（只有组头出 P），则 `max_te` 只由组头贡献 = 1×，与 R3 的 3× 矛盾。
所以三节**都有非零功率**，1× 是因为 override 被均分成 P/3 而不是"成员不出力"。

**判决 2：G-464-1（归一没触发）成立，且原因已定位到判据写法。**
`R3RNormaliseArticGroupBakes()` 的 baked 判据是
`weight_override != UINT16_MAX || power_override != UINT16_MAX`（`src/train_cmd.cpp:2956-2966`，
**任一节任一字段**命中即整组跳过）。本案里 `power_override` 存在 ⇒ 整组恒被判 baked
⇒ 一次都不重烘 ⇒ 重量永远停在 3×。第 463 轮修法对本案无效的机制到此闭合。

### 10.3 仍未解释的一环：字段为什么会一有一无

`weight_override` 与 `power_override` 由**同一个函数同批写入**（`src/vehicle_cmd.cpp:550-566`）、
**同一版本门写入存档**（`src/sl/vehicle_sl.cpp:209-211`），理论上不该部分存在。剩余两种嫌疑：

- P-1：某条路径只写/只清一个字段。全库赋值点只有 `vehicle_cmd.cpp:550/563/713` 与
  `train_cmd.cpp:2976/2980`；其中 713（`RearticulateChain()`）三字段齐清 ⇒ 需逐点复核 550/563
  是否被 `wagon_type` 等分支跳过。
- P-2：旧 exe 写的档里字段序/版本门与实际不一致，读回时 `weight_override` 落默认
  UINT16_MAX 而 `power_override` 有值。

定案只需一条数据：读档后 `dump_vehicle <id>` 的 `ov=` 行（三个 override 各是多少）。

### 10.4 下一版修法（已可定，不依赖 P-1/P-2）

把 baked 判据从「整组任一字段 ⇒ 跳过」改成**逐字段判定**：

- 组内任一节 `weight_override == UINT16_MAX` ⇒ 重烘 weight（按 `R3RArticBaseWeight(ref)` 均分）；
- 组内任一节 `power_override == UINT16_MAX` ⇒ 重烘 power；
- 两者独立，互不牵连 ⇒ 部分烘焙必被补齐，且对已完整烘焙的组仍是零改动（幂等）。

`ref` 用 `R3RConsistReferenceEngine(head)` 而不是 `head`，以便 C-2 换端后仍取到真父车原型。

预期：重量回 1× ⇒ TE 随之回 1×（`max_te` 每节只占 W/3）。

### 10.5 KI-661（容量 2→1）与烘焙层无关，另案处理

R4 不随读档变化 ⇒ 不进 override 路径。机制在 `Train::ConsistChanged()` 的容量块
（`src/train_cmd.cpp:614-646`，KI-656）：真 artic 时两节 part 各带 1（散链合计 2）；打散后
「组内非组头」的 `cargo_cap` 被强制清 0，只剩组头那一份 ⇒ 1。修法方向：组头自身容量为 0 时，
应把成员原有容量并到组头（而不是清零），才能做到 2→2。

## 十一、第 465 轮：读玩家 build\R3R_debug.log 后，§10.2 判决 2 被自己推翻

### 11.1 硬事实（日志锚点）

日志 `build\R3R_debug.log`（含两次读档，各 2 条）：

- `:152` `ARTIC-BAKE-NORMALISE head=0 n=3 w=185 p=1524 spd=80 (overrides missing in savegame)`
- `:160` `ARTIC-BAKE-NORMALISE head=3 n=3 w=185 p=1524 spd=80`
- `:185` `ARTIC-BAKE-NORMALISE head=0 n=3 w=185 p=1524 spd=80`
- `:190` `ARTIC-BAKE-NORMALISE head=3 n=3 w=185 p=1524 spd=80`

⇒ **第 463 轮修法确实执行了**（每次读档、每组各一次），且烘焙值按 `n=3` 均分后为
weight 63/61/61（总 185）、power 508/508/508（总 1524）、max_speed 80：
三项都是**正确的 1×**。

对照组（打散当场，`:141-149`）同样正确：
`DEARTIC-SPLIT head=3 n=3 wtot=185 wq=61 wr=2 ptot=1524 pq=508`，
`DEARTIC-PART` 逐节 `ov=63/61/61`，
`MAKESEG-WEIGHT UPGRADE-AFTER-SPLIT sumw=185`。

⇒ §10.2 判决 2「归一没触发 / 判据恒跳过」**被证伪**。判据没拦它，它跑了，值也对。

### 11.2 全库 override 写入点只有 3 处，读档后无人覆盖

`grep 'weight_override\s*=' src` 结果（power/max_speed 同址）：

| 位置 | 作用 |
|------|------|
| `src\vehicle_cmd.cpp:550-566` | 打散时烘焙（weight/power/speed 同批） |
| `src\vehicle_cmd.cpp:713-715` | `RearticulateChain()` 三字段齐清（读档不走） |
| `src\train_cmd.cpp:2976-2982` | 本次归一烘焙 |

没有第四处。⇒ 读档结束后三节的 `weight_override` **就是 63/61/61**，
`GetWeightWithoutCargo()`（`src\train.h:496-515`）override 优先 ⇒ 返回 63/61/61；
`GetWeight()`（`src\train.h:627-630`）= `GetWeightWithoutCargo() + GetCargoWeight()` ⇒ 也走 override；
`CalculatePower()`（`src\ground_vehicle.cpp:93`）用 `u->GetWeight()` ⇒ TE 应当也是 1×。

**所以「重量 3× / TE 3×」不可能来自 override 层。** 矛盾必须落在别处。

### 11.3 新怀疑 G-465-1（待验证）：读数走的是不看 override 的路径

已排除：`GetWeightWithoutCargo`、`GetWeight`、`CalculatePower`、`CargoChanged`
（`ground_vehicle.cpp:133-142`，打散后 `IsArticulatedPart()==false` ⇒ else 分支
`engine_weight = GetWeightWithoutCargo()` = 63，`part_count = 1`，累计 185）。

仍可能不看 override 的读数入口，按优先级：

1. **`Train::GetMaxWeight()`**（`src\vehicle.cpp:5386` 的 `GetDisplayMaxWeight()` 靠它求和）
   —— 若它用 `RailVehInfo(engine)->weight` 或 `GetWeightWithoutCargo()` 之外的来源，
   详情窗口的「最大重量」就会显示 555 = 3×，与 override 无关。
2. **缓存未重算**：`gcache.cached_weight` / `cached_max_te` 在烘焙前的
   `load-rebuild` 那次 ConsistChanged 已按 555 算好；`:2991` 的
   `head->ConsistChanged(CCF_SAVELOAD)` 是否真的重跑 `CargoChanged`+`UpdatePowerCache`，
   需逐行确认（若该函数内部按 `IsArticulatedPart()`/`HasArticulatedPart()` 分叉或提前 return，
   缓存会停在 555）。
3. 详情窗口的 max-TE 显示若读 `gcache.cached_max_te`，则同 (2)。

### 11.4 取数缺口

`CARGOWEIGHT` / `CARGOWEIGHT-VEH` 在日志里 **0 条** ⇒
`R3RDbgBulkOn()` 未开（该探针是 bulk 门），因此看不到烘焙后 `CargoChanged` 的
`total=` / `dmw=`，无法在第 11.3 的两条候选间立刻定案。

定案只需其一：①开 bulk 再读一次档（看 `CARGOWEIGHT head=3 ... total=` 是 185 还是 555，
`dmw=` 是 185 还是 555）；②读档后 `dump_vehicle 3/4/5` 的 `ov=`（确认 63/61/61）；
③告知「重量变三倍」具体看的是哪个界面（详情窗口的体重/最大重量、还是实际爬坡表现）。

### 11.5 进一步取数（同轮）：存档版本门不是原因，新矛盾 REG-465-A

- `src\sl\saveload_common.h:468`：`SAVEGAME_VERSION = SLV_R3R_PARKED`（= `SLV_R3R_ARTIC_OVERRIDE` + 1）
  ⇒ **新 exe 存出来的档一定会带这三个字段**（`src\saveload\vehicle_sl.cpp:209-211` 的门限满足）。
  日志里两次读档都打 `(overrides missing in savegame)` ⇒ 这两次读的**都是旧 exe 写的档**
  （或者打散动作发生在旧 exe 的会话里），这与玩家「每次都 3×」一致：只要不重存一次档，
  每次读回来都是 UINT16_MAX，靠归一在内存里补。
- `R3RArticBaseWeight()`（`src\train_cmd.cpp:2887-2897`）= 185，`R3RArticBasePower()`
  （`:2900-2910`）= `RailVehInfo(483)->power` = 1524，二者都是**单位（整组）值**。
  ⇒ 若 override 全缺：weight 555（3×）、power 4572（3×）。
  ⇒ 玩家实测功率 1× ⇒ 归一写进去的 `power_override = 508 × 3 = 1524` **是生效的**。
  ⇒ 同一循环里同一批车写的 `weight_override = 63/61/61` 没有理由是坏的。

**REG-465-A（未解，必须取数）**：日志自证归一执行且写的是 63/61/61，玩家实测重量仍是 3×。
说明「重量」这个读数不经过 `weight_override`，或者归一写的对象与显示的链不是同一批。
已知不走 override 的路径已逐条排除（`GetWeightWithoutCargo`/`GetWeight`/`GetMaxWeight`/
`CargoChanged`/`CalculatePower` 全走）。

**本轮动作**：给 `R3RDUMP idx=` 行加五个只读字段
（`src\sl\vehicle_sl.cpp:767-776`）：
`ov_w=%u ov_p=%u ov_s=%u cw=%u te=%u`
= `weight_override` / `power_override` / `max_speed_override` / `gcache.cached_weight` /
`gcache.cached_max_te`。复跑同一场景即可分辨：
- `ov_w=63/61/61` 且 `cw=185`、`te=(1×值)` ⇒ 内存正确，3× 在显示/别处，转查界面；
- `ov_w=65535` 或 `185`，或 `cw=555` ⇒ 归一没落盘/被覆盖，继续查写侧；
- `ov_w=63` 但 `cw=555` ⇒ `CargoChanged` 没跑（归一末尾的 `ConsistChanged` 被跳过）。

### 11.6 构建自证（第 465 轮）

- 复用既有入口 `R3R_fullbuild.cmd inc detach`（未新建任何 .cmd）。
- `build\R3R_incbuild.guard.log` = `GUARD: incremental is safe`（无头/语言文件新于最新 obj）。
- 日志尾：`[2/3] Building CXX object ...\src\sl\vehicle_sl.cpp.obj` → `[3/3] Linking CXX executable openttd.exe`；
  `build\R3R_incbuild.done` = `EXIT_CODE=0`（2026/11/19 02:22:46）。
- 时间戳链：`src\sl\vehicle_sl.cpp` 02:20:19 → `vehicle_sl.cpp.obj` 02:21:15 → `build\openttd.exe` 02:22:38（53 030 400 B）。
- `read_lints` 该文件 0 条；exe 内可检索到新字面量 `cw=%u te=%u`（`ov_w=%u` 等被拆成独立字面量属正常）。
- 仅改 `src\sl\vehicle_sl.cpp` 一个 .cpp，未碰任何 `src\*.h` ⇒ 增量合规。

### 11.7 待办口径（复测取数）

玩家用 2026-11-19 02:22 的 exe **读同一个档**（不必重新打散；若打散过就更要读档复现），
取回 `build\R3R_debug.log` 中新格式的 `R3RDUMP idx=3/4/5` 行，按 §11.5 三分支判定 REG-465-A。
**在拿到 `ov_*`/`cw`/`te` 实际读数之前，不改 `R3RNormaliseArticGroupBakes()` 的 baked 判据**（避免盲改）。
