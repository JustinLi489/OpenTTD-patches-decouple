# 第 290 轮 临时分析报告：段内 GRF 属性独立性 + "设为段"后重量三倍 + 四条待报问题

- 日期：2026-10-23
- 触发：玩家在本轮开头一次性预告了 4 类问题（本条先把口径与待取证项落盘，避免上下文压缩丢失）
- 状态：**登记 / 取证中**，本轮尚无结论、无源码改动、无构建
- 关联 KI：`R3R_KNOWN_ISSUES.md` 末尾「第 290 轮（2026-10-23）」小节（KI-458 / KI-459）

---

## 0. 玩家本轮预告的四类问题（原话要点，逐条占位）

1. **两个"可能是同步造成的"错误** —— 玩家原话「我稍后会为你报告」。⇒ 本轮只登记，等现场日志；按既有口径，"同步"= 多人游戏 desync / 或"同一行为在两处状态不同步"。**待报**。
2. **一个 GRF 回调的错误** —— 待报。注意与 §1（段内 GRF 属性独立性）大概率同源，若同源需合并为一条根因。
3. **一个数据的错误** —— 待报。
4. **GRF 属性按段独立**（要改行为）+ **"设为段"后重量翻三倍**（bug）—— 见 §1、§2。

登记为待补项，编号供玩家回填：

| 编号 | 待补内容 | 玩家答复 |
|---|---|---|
| Q-290-1 | 同步错误 #1 的现象/复现/是否联机/是否有 `desync` 日志 | 待填 |
| Q-290-2 | 同步错误 #2 同上 | 待填 |
| Q-290-3 | GRF 回调错误：哪个 GRF、哪条回调（callback 36 属性 / 0x40 position-in-consist / 长度 / 载量 / 视觉）、错成什么样 | 待填 |
| Q-290-4 | 数据错误：哪个数值（重量/功率/容量/速度/寿命）与期望差多少 | 待填 |

---

## 1. R290-A（需求）：**每个段的 GRF 属性独立，只受本段段头影响**

- **玩家口径（原话要点）**：每个段的 GRF 属性都是独立的，只受本段段头影响。
- **现状（已知，来自既有实现）**：
  - 只有 `position in consist` 一族（NewGRF 变量 `0x40` / `0x41`，`newgrf_engine.cpp` 的 `PositionHelper`）在历史轮次里改成了"段内计数"（段首 = 最近的前置 `VehicleRailFlag::SegmentFront`，无则物理链头）。
  - 但**其它依赖"链"的 GRF 读取面仍是整链语义**，至少包括：
    1. `Engine::GetPower`/`GetWeight`/`GetCapacity` 等属性在 `ConsistChanged()` 里对整链统一累加/取极值（`cached_power`、`cached_weight`、`cached_max_speed`）；
    2. NewGRF `CBID_VEHICLE_*`（尤其 `CBM_VEHICLE_LENGTH`、`CBM_VEHICLE_LOAD_AMOUNT`、`CBM_VEHICLE_REFIT_CAPACITY`）与变量 `0x42`（consist 里同 ID 的车数）、`0x4A`（?）等按整链或按"同 ID 连续段"回答；
    3. `Train::ConsistChanged` 里"多引擎/双头"分支（`IsMultiheaded`、`IsRearDualheaded`）按链头/链尾身份决定，段与段之间会互相影响。
  - ⇒ **本需求 = 把"段"提升为 GRF 读侧的边界**，与既有 `position in consist` 的做法一致，但覆盖面要大得多。
- **猜测根因（待验证）**：不是"某个属性算错"，而是**缺少统一的"段上下文"入口**：现在每个读取点各自沿 `Next()`/`Previous()` 走到链头，走不到段头。修法方向 = 抽一个 `R3RSegmentHeadOf(v)`（或复用 `R3RGetSegmentHeadOf`）+ 把 GRF 读侧的"consist 起点/长度/同 ID 计数"三类查询改为以段为界。
- **需要的数据**：哪条 GRF、哪个属性/回调受段外影响（例：段 B 的属性随段 A 的车变化）。没有这个就无法判定"独立"的边界该切在哪。
- **风险**：`cached_*` 是全局链级缓存，改段级会牵动 `ConsistChanged()` 与 `InvalidateNewGRFCache*` 两条链，且与联机 desync 面直接相关（与 Q-290-1/2 可能同源）。

---

## 2. R290-B（bug）：**对某个 GRF 车执行"设为段"后重量翻了三倍**

- **实测值**：约 ×3（玩家口述，未给具体数字）。
- **期望值**：执行 `MakeSegment` 前后**总重不变**（升段是身份/边界操作，不是新增车辆；`CmdMakeSegment` 的设计目标明确写的是"statistics conserved via baked overrides"）。
- **执行链（`src\vehicle_cmd.cpp:754-813`，已读原文）**：
  ```
  R3RStopChainInDepot(t);
  [IsFreeWagon] R3RPromoteFreeWagonChainToFront(t);   // 链头假引擎身份
  DearticulateChainWithSnapshot(t);                   // 拆真铰接组 → 独立车 + 烘焙 override
  t->SetSegmentFront();                               // ★
  SetSegmentTailFakeEngine(t);                        // 链尾假引擎
  t->ConsistChanged(CCF_ARRANGE);                     // 重算 cached_*
  ```
- **猜测根因（待验证，按可能性排序）**：
  1. **假引擎把车厢重量替换成引擎重量**：`SetSegmentTailFakeEngine()` 让链尾车厢 `SetEngine()`，而重量读取对 `IsEngine()` 的车走"引擎档案重量"（GRF 里引擎重量常是车厢的数倍）⇒ 尾部那一节变重；
  2. **去铰接烘焙时重量未均分/重复烘焙**：`R3RDearticulateOneGroup()` 给组内每节写"烘焙 override"（power/weight 均分余数），若某条路径把**整组重量**写给每一节（而不是除以节数），3 节组就 ×3 —— 与"三倍"这个具体倍数最吻合；
  3. **`ConsistChanged` 重复累加**：拆开后节数变多，若 `cached_weight` 的累加把"artic 部件原本 0 重"变成"每节都按父车重量"，也会成倍。
- **判据（取证用）**：
  - 升段前后各打一次总重：`UPGRADE-BEFORE-SPLIT` / `UPGRADE-AFTER-SPLIT` 已有 dump（`R3RDumpUpgradeDbg`），但**要看它是否已含 weight**；
  - 逐节 dump `IsEngine()`、`engine_type`、`RailVehInfo(engine_type)->weight`、`gcache.cached_weight`、以及烘焙字段（若存在）。
- **关联**：与 R290-A 同源可能性高（都指向"段内的属性/重量按什么粒度累加"）。

### 2.1 已取证（代码事实，仅读未改）

- `src\vehicle_cmd.cpp:503-543` `R3RDearticulateOneGroup()` 原文：
  ```cpp
  const uint16_t total_weight = v->GetWeightWithoutCargo();
  const uint16_t total_power  = v->GetPowerSnapshot();
  const uint16_t max_speed    = GetVehicleProperty(v, PROP_TRAIN_SPEED, RailVehInfo(v->engine_type)->max_speed);
  const bool wagon_type = RailVehInfo(v->engine_type)->railveh_type == RailVehicleType::Wagon;
  ... n_total = 1 + 连续 artic parts 数
  w_q = total_weight / n_total;  w_r = total_weight % n_total;
  v->SetArticGroupHead();  v->weight_override = w_q + w_r;   // 父车拿余数
  成员：ClearArticulatedPart(); SetWagon()/SetEngine(); SetArticGroupMember();
        weight_override = w_q;                                // 每个成员拿均分
  ```
  ⇒ **均分本身数学守恒**（`(n-1)*q + (q+r) == total`），所以"×3"不可能由除法/余数写错产生。
- `src\train.h:480-495` `GetWeightWithoutCargo()`：`weight_override != UINT16_MAX` **优先**；否则 `!IsArticulatedPart()` 时才加 `GetVehicleProperty(this, PROP_TRAIN_WEIGHT, RailVehInfo(engine_type)->weight)`。⇒ 重量**不看** `IsEngine()`/`IsWagon()` 身份，只看 override 与 `IsArticulatedPart()`。
- `src\train_cmd.cpp:22879-22891` `GetMaxWeight()` 同款门禁（override 优先）。
- `src\saveload\vehicle_sl.cpp:209-211` 三个 override 是**存档字段**（`SLV_R3R_ARTIC_OVERRIDE`），⇒ 一旦烘焙错误会被写进存档、跨读档持续。

- **由此收窄的三条嫌疑（替代 §2 里较泛的三条）**：
  1. **`n_total` 被算成 1（或远小于真实节数）** ⇒ 每节都拿"整组重量"，总重 = 真实节数 × 组重。三节组正好 ×3。触发条件怀疑：`v->GetNextArticulatedPart()` / `nxt->IsArticulatedPart()` 在**已被上一轮升段拆过一次**的链上返回 nullptr（`ArticGroupMember` 角色位为真但 subtype 已不是 artic part）⇒ `n_total=1`、`w_q=total`，于是"再次设为段"或"解段后再设为段"会逐轮翻倍/翻三倍；
  2. **`total_weight` 本身已是整链合计**：`GetWeightWithoutCargo()` → `GetVehicleProperty(v, PROP_TRAIN_WEIGHT, ...)` 会跑 NewGRF **callback 36**，某些 GRF 在该回调里按"consist"返回整组/整列重量 ⇒ 拆分后每节各自再走一次回调（若 override 丢失/被清）就成倍；
  3. **`SetSegmentTailFakeEngine()` 让链尾 `SetEngine()` 后，`ConsistChanged()` 里"多引擎"分支重复计入**（假引擎不是 `IsFrontEngine()`，理论上不该触发，但 powered-wagon 额外重量那条分支未核）。
- **下一步取证（待玩家数据后执行）**：在 `R3RDearticulateOneGroup()` 里加只读探针 `DEARTIC-SPLIT head=%d n=%u w_total=%u w_q=%u w_r=%u p_total=%u`，并在 `CmdMakeSegment` 前后打每节 `weight_override/IsArticulatedPart/GetWeightWithoutCargo` ⇒ 一次即可区分"n_total 算错"与"total_weight 本身翻倍"。

---

## 3. 本轮待办（按铁律顺序）

1. ✅ 先落盘（本文件 + KI-458/KI-459）
2. 取证 ①：`R3RDumpUpgradeDbg` 是否已有重量字段；`R3RDearticulateOneGroup` 的 override 烘焙代码（weight/power 如何分配）
3. 取证 ②：`SetSegmentTailFakeEngine` 是否 `SetEngine()`，以及 `ConsistChanged` 里重量对 `IsEngine()` 的依赖
4. 取证 ③：GRF 读侧"链头"入口清单（为 R290-A 划边界）
5. 等玩家报 Q-290-1..4 后再合并根因、再改码

---

## 4. 未确认项

- U-290-A：玩家说的"某个 GRF"是哪个（决定是否要按 GRF 特性而非通用逻辑修）。
- U-290-B：×3 是"总重"还是"单车显示重"；是升段瞬间变化还是下次读档/提交点才变化。
- U-290-C：四条问题里，GRF 回调错误与 R290-A 是否同一处（若是，合并为一条修复）。

---

## 5. 玩家答复 Q-290-3 / Q-290-4（2026-10-23）

### 5.1 Q-290-4（重量 ×3）—— 玩家原话要点

- 那个 GRF 是**朋友自制的**，**源码一时难以得到**（⇒ 不能靠读 .nfo/.nml，只能靠运行时探针 + 有 .grf 时的 grfcodec 反查）。
- 车是**三节铰接**。
- **所有重量集中在中间那节**（⇒ 父车与另一节不带重量）。
- 升段之后**重量变为原先三倍**。
- **只执行了一次"设为段"**（⇒ 直接推翻 §2.1 嫌疑 1「重复升段/n_total 被算成 1 的二次拆解」作为主因；"三倍"= 节数这一巧合必须由"每节各计一次整组重量"来解释）。

### 5.2 Q-290-3（GRF 回调）—— 玩家原话要点

- 具体表现为**涂装出错**，**难以描述**（⇒ 视觉/涂装类回调，最可能是 NewGRF 变量 `0x40/0x41` position-in-consist 之外的**变体/涂装选择**（variational action 2 第 `0x42/0x47` 类变量、artic 组内位置、consist 长度）在拆组/升段后读到不同值）。

### 5.3 玩家建议（本轮执行口径）

> 调整一下要启用的探针，把要用的 grf 探针一次性加好，不确定的问我。

⇒ 本轮按此执行：**一次把 GRF/重量/涂装三类探针全部加好再编译一次**，不做"加一个探针→编一次→再补"的循环；有拿不准的**先用 Q-290-6..9 问玩家**，不猜。

### 5.4 新猜测根因（待验证，替代 §2.1 的嫌疑 1）

- **H-459-1（首选）**：`weight_override` 只被 `Train::GetWeightWithoutCargo()` / `GetMaxWeight()` 读取，**但 `Train::ConsistChanged()` 里 `gcache.cached_weight` 的累加若走的是另一条路径**（`RailVehInfo(engine_type)->weight` 或 `GetVehicleProperty` 直接读数），拆分后的三节会**各自**按引擎记录计重。该 GRF 的整组重量在引擎记录里被三节共享（GRF 常把整组重量写在同一 engine 记录上，靠 `IsArticulatedPart()` 只算一次），拆开后 `IsArticulatedPart()` 全变假 ⇒ **同一份整组重量被计三次** ⇒ **恰好 ×3**。这是与"三节 / 重量集中 / 只设一次段"三条事实**同时吻合的唯一简洁解释**。
- **H-459-2**：`R3RDearticulateOneGroup()` 的 `total_weight = v->GetWeightWithoutCargo()` 在父车不带重量时读到 0，override 全部写成 0；此后任何走 override 的读取都得 0，而走引擎记录的读取得整组重量 ⇒ 显示值取决于读侧，行为分裂（可能同时解释"变成三倍"和"某些地方又归零"）。
- **H-459-3**：`SetSegmentTailFakeEngine()` → `SetEngine()` 后，`ConsistChanged()` 的"多引擎/动力车厢"分支给该节**额外**加重量（`Powered wagons have extra weight added`，`train_cmd.cpp:22891` 一带），叠加成倍。
- **判据（一次日志即可三选一）**：`DEARTIC-SPLIT` 的 `w_total/n` 与 `SEG-WEIGHT-AFTER` 的逐节 `override/railveh_w`、以及 `CONSIST-WEIGHT` 的"每节贡献值"三者一对账即可。

### 5.5 探针清单（本轮要一次性加好的全部探针）

| 编号 | 探针行 | 落点 | 用途 |
|---|---|---|---|
| P1 | `DEARTIC-SPLIT head=%d n=%u w_total=%u w_q=%u w_r=%u p_total=%u` | `src\vehicle_cmd.cpp` `R3RDearticulateOneGroup()` 快照之后 | 看快照值与节数，判定"重量集中在中间节时 total_weight 读到什么" |
| P2 | `SEG-WEIGHT <pre\|after> head=%d total=%d` + 逐节 `v=%d eng=%d rw=%d ov=%d art=%d gh=%d gm=%d pw=%d eng_bit=%d` | `src\vehicle_cmd.cpp` `CmdMakeSegment()` 执行前后 | 升段前后总重与逐节来源对账（本次 ×3 的核心证据） |
| P3 | `GRF-WEIGHT v=%d prop=%d ret=%d def=%d` | `src\train.h` `GetWeightWithoutCargo()` / `GetMaxWeight()` 的 `GetVehicleProperty` 调用处（只读） | 直接看 GRF callback 36 返回的整组重量（是否三节都返回 W） |
| P4 | `CONSIST-WEIGHT v=%d cw=%d src=ov\|rec\|artic` | `src\train_cmd.cpp` `Train::ConsistChanged()` 重量累加循环内 | 判定 `cached_weight` 到底走 override 还是 engine record |
| P5 | `POSCONSIST v=%d pos=%u len=%u seg=%d` | `src\newgrf_engine.cpp` `PositionHelper` | 段内计数是否真的只在段内（KI-458 现状取证） |
| P6 | `GRF-CB v=%d cb=%d eng=%d art=%d gh=%d gm=%d` | NewGRF 车辆回调分派入口（涂装/长度/载量一类） | 涂装错误时看回调拿到什么上下文 |
| P7 | `SPRITE v=%d eng=%d spi=%d cargo=%d pos=%u` | 车辆取图入口（`Train::GetImage` 一侧） | 涂装错节/错图时定位取图参数 |

- 全部探针**只读**（`R3RFopenDbg("a")` + `fprintf`），不改任何判据/缓存/几何；沿用既有探针开关（`R3RDbgOn()`，无则按既有文件内约定）。
- **待玩家答复后才落地的不确定项**：
  - **Q-290-6**：涂装错误是"整列都用同一节的涂装"还是"某些节变成别的涂装"？升段前正常、升段后错，对吗？
  - **Q-290-7**：重量三倍是"点完设为段当场就变"，还是"下次读档/跑一段后才变"？
  - **Q-290-8**：能否给那个 `.grf` 文件本身（**不需要源码**）？有 .grf 我可以用 grfcodec 反查引擎重量字段与 callback 36，比探针更直接。
  - **Q-290-9**：本轮是否只加探针+编译、不改行为？（默认：是。）

---

## 5.6 玩家答复（Q-290-6..9，2026-10-23，原话要点）

- **Q-290-6（涂装形态）**：两列车都是**三节一套**——头车 = 涂装 N、中间车被 **livery override** 成涂装 N、尾部头车由**回调改变图像**但也是涂装 N。**涂装 2 的那列主动挂接涂装 1 的那列之后，其中间车厢涂装变为涂装 1。**
  ⇒ 这是 NML `livery_override`（wagon override）被**链头前端引擎**驱动造成的，**与升段路径无关**。代码锚点已核实：
  - `src\train_cmd.cpp:416-419`：`u->gcache.first_engine = (this == u) ? EngineID::Invalid() : first_engine;` 之后 `if (u->IsEngine()) first_engine = u->engine_type;` ⇒ `first_engine` 是**整条物理链**内"本车之前最近的一个引擎"，**链级而非段级**。
  - `src\train_cmd.cpp:450`：`u->tcache.cached_override = GetWagonOverrideSpriteSet(u->engine_type, u->cargo_type, u->gcache.first_engine);` ⇒ `livery_override` 用它查表。
  - `src\vehicle.cpp:3197-3201`：`GetEngineLiveryScheme()` 在 `UsesWagonOverride(v) || (v->IsArticGroupMember() && ...)` 时把 `engine_type` 换成 `parent_engine_type`(= `first_engine`)，注释自陈 "Wagonoverrides use the colour scheme of the front engine" ⇒ 2CC 配色同样跟链头前端引擎走。
  - **H-458-1（猜测根因，待验证）**：挂接后涂装 2 那列的中间车 `first_engine` 变成了涂装 1 那列的引擎（它自己那列的头车引擎在**链序**上已不在它之前——典型成因=耦合折叠修正的逻辑翻/整链倒置），于是 `GetWagonOverrideSpriteSet()` 命中不了原来的 `livery_override`，退回默认外观（=涂装 1）。修法方向与 KI-458 同源：把 `first_engine` 改成**段内**语义（`R3RSegmentFirstEngineOf(v)`）。

- **Q-290-7（×3 时机）**：**升段后立刻**（点完"设为段"当场变）。
  ⇒ 与读档/跑车/consist 变化无关，落点锁定 `CmdMakeSegment` 的 `DearticulateChainWithSnapshot` + `ConsistChanged(CCF_ARRANGE)` 这一小段。
- **Q-290-8（grf 文件）**：已提供 `D:\temp\codebuddy-dropped-files\a9404e6b-cfba-4a45-b070-fc2f4627f331\china_maglev_20261020.grf`（本工作区无 grfcodec，本轮不反编译，以运行时探针为准）。
- **Q-290-9（本轮范围）**：**先只加探针**，不改行为。

### 5.6.1 由此补上的关键代码事实（`GroundVehicle::CargoChanged`，`src\ground_vehicle.cpp:112-121`）

```cpp
for (T *u = T::From(this); u != nullptr; u = u->Next()) {
    uint32_t current_weight = u->GetCargoWeight();
    if (u->IsArticulatedPart()) {
        current_weight += articulated_weight;               // 真铰接件：拿父车分给它的份额
    } else {
        uint16_t engine_weight = u->GetWeightWithoutCargo();
        uint part_count = u->GetEnginePartsCount();         // 真铰接件计数，去铰接件必须=1
        articulated_weight = engine_weight / part_count;
        current_weight += articulated_weight + (engine_weight % part_count);
    }
    ...
    weight += current_weight;
}
```

⇒ 上游口径：**父车记录的引擎重量 = 整组重量**，按 `part_count` 均分。因此：

- 升段前（3 件真铰接）：父车 `engine_weight = W`、`part_count = 3` ⇒ 父 w/3、两件各 w/3 ⇒ 合计 W（正常）。
- 升段后**若 `weight_override` 已烘焙**（每节 q、父车 q+r，`part_count` 此时=1）⇒ 合计仍 W（正确）。
- 升段后**若 `weight_override` 仍是 `UINT16_MAX`** ⇒ 每节各按自己**引擎记录**计重。若该 GRF 的三节引擎记录都写着整组重量 W（GRF 常见写法，因为上游只算父车那一次），则 **W×3 = 正好三倍**。

⇒ **H-459-1 精确化为 H-459-1'（首选）**：×3 = 去铰接后 `weight_override` 未生效/未烘焙，三节各计一次整组重量。三条可能的分叉（探针一次分辨）：
  1. `R3RDearticulateOneGroup()` 根本没被调用（`R3RChainHasRealArticPart()` 为假，或 `CmdMakeSegment` 走了别的分支）；
  2. 被调用了，但 `n_total` 算成 1（`w_q = W`），三节各拿 W；
  3. 被调用且烘焙正确，但 `weight_override` 之后被某条路径重置为 `UINT16_MAX`（如 `RearticulateChain` 的逆向路径）。

## 5.7 定稿探针清单（一次加完全部，按 Q-290-6/7 重新排序）

| 编号 | 探针行 | 落点 | 回答什么 |
|---|---|---|---|
| **W1** | `SEG-WEIGHT <before\|after> head=%d n=%d cw=%u` + 逐节 `SEG-VEH ... ov= art= rw= w= pw= fe= gh= gm=` | `src\vehicle_cmd.cpp` `CmdMakeSegment()` 执行前后 | **×3 主证据**：升段前后总重 + 每节 `weight_override` / 引擎记录重量 / `GetWeightWithoutCargo()` 三者对账 |
| **W2** | `DEARTIC-SPLIT head=%d n=%u w_total=%u w_q=%u w_r=%u p_total=%u` + `DEARTIC-PART i= eng= art= w=` | `src\vehicle_cmd.cpp` `R3RDearticulateOneGroup()` 算完 `n_total`/`w_q`/`w_r` 之后 | 分叉 1/2：去铰接到底有没有跑、`n_total` 是 3 还是 1、烘焙值是多少 |
| **W3** | `CARGOWEIGHT head=%d i=%d idx=%d art=%d ew=%u pc=%u contrib=%u total=%u` + `CARGOWEIGHT-END head=%d total=%u cached=%u` | `src\ground_vehicle.cpp` `CargoChanged()` 循环内（仅 Train、仅含 `ArticGroupMember` 或 `weight_override != UINT16_MAX` 的链） | 分叉 3：最终 `cached_weight` 到底把谁算了几次 |
| **W4** | `GRF-WEIGHT eng=%u v=%d fe=%u orig=%d cb=%d ret=%d` | `src\newgrf_engine.cpp` `GetEngineProperty()` 的 `PROP_TRAIN_WEIGHT` 分支（只读） | GRF 是否用 callback 36 出重量、三节各返回多少 |
| **L1** | `LV-OVR head=%d idx=%d eng=%d fe=%d engb=%d art=%d gm=%d gh=%d uwo=%d ovr=%d pw=%d` | `src\train_cmd.cpp` `Train::ConsistChanged()` 第 450 行附近（每链一次，链签名变化才打） | **Q-290-6 主证据**：`first_engine` / `cached_override` / `UsesWagonOverride` / 配色归属 |
| **L2** | `LV-SCHEME idx=%d eng=%d par=%d uwo=%d gm=%d rv=%d ret=%d` | `src\vehicle.cpp` `GetEngineLiveryScheme()` 的 Train 分支 | 配色方案到底取了哪个引擎 |
| **L3** | `POSCONSIST idx=%d pos=%u len=%u seghead=%d chainhead=%d` | `src\newgrf_engine.cpp` `PositionHelper()` 出口（每车每 (pos,len) 只打一次） | 段内计数是否只在段内（KI-458 现状取证） |

- 全部**只读**（`R3RFopenDbg("a")` + `fprintf`），不改判据/缓存/几何；只改 `.cpp`，不碰 `src\*.h`（增量构建合规）。
- 日志体积控制：W1/W2 只在 `CmdMakeSegment`/去铰接时打一次；L1 用链签名边沿触发；L3 用 (index,pos,len) 去重；W3/W4 只在"含 ArticGroupMember 或带 override 的链"上打。

## 5.8 落地实录（第 290 轮，2026-10-24 01:0x，7 组探针 + 一次增量编译）

玩家口径 Q-290-9「先只加探针」⇒ 本轮**零行为变更**，只加只读打印。

### 5.8.1 实际行格式（与 §5.7 表格命名不完全一致，以本节为准）

| 组 | 实际 tag | 落点（文件:函数） | 字段 |
|---|---|---|---|
| W1 | `MAKESEG-WEIGHT` | `src\vehicle_cmd.cpp` `CmdMakeSegment()`，紧跟既有 `MAKESEG` 逐节 dump 之后 | `head= n= sumw= sumpw= cw= dmw=`（`sumw`=各节 `GetWeightWithoutCargo()` 之和，`sumpw`=各节 `weight_override` 之和，`cw`=链头 `gcache.cached_weight`，`dmw`=`GetDisplayMaxWeight()`） |
| W1' | `MAKESEG` 逐节行 | 同上（既有行，本轮**追加字段**） | 每节追加 `ov= rw= pw= w=`（override / 引擎记录重量 `RailVehInfo(engine).weight` / `weight_override` / 实测 `GetWeightWithoutCargo()`） |
| W2 | `DEARTIC-SPLIT` | `src\vehicle_cmd.cpp` `R3RDearticulateOneGroup()` 算完 `n_total/w_q/w_r` 之后 | `head= n= wtot= wq= wr= ptot= pq= wagon=` |
| W2' | `DEARTIC-PART` | 同上，逐节（上限 64） | `head= i= idx= art= gm= gh= eng= ov= rw= w=` |
| W3 | `CARGOWEIGHT-VEH` / `CARGOWEIGHT` | `src\ground_vehicle.cpp` `CargoChanged()` 循环内 + 末尾 | 逐节 `head= idx= eng= art= gm= gh= ov= ew= pc= cw= acc=`；末尾汇总 `head= n= total= old= dmw=` |
| W4 | `GRF-WEIGHT` | `src\newgrf_engine.cpp` `GetEngineProperty()` 的 `callback != CALLBACK_FAILED` 块内 | `eng= v= ret= orig= signed=`（`ret`=cb36 实际返回值，`orig`=原值） |
| L1 | `SEGFE-VEH` | `src\train_cmd.cpp` `Train::ConsistChanged()` 第 450 行 `cached_override` 赋值之后 | `head= veh= eng= first= ovr= cargo= segf= segb= artgm=`（**只打 `first != Invalid && first != 本车引擎` 的行**，即"确有父引擎偏移"的行） |
| L2 | `LIVERY-SCHEME` | `src\vehicle.cpp` `GetEngineLiveryScheme()` 的 Train 分支、`engine_type = parent_engine_type` 之前 | `v= own= parent= useov= artgm= front= head_eng=` |
| L3 | `POSCONSIST` | `src\newgrf_engine.cpp` `PositionHelper()` 出口 return 之前 | `veh= eng= before= after= flip= consec= head=`（前 500 行封顶） |

体积封顶：L1 ≤800 行、L2 ≤500 行、L3 ≤500 行；W2' ≤64 节、W3 扫描 ≤512 节。

### 5.8.2 构建自证（第 290 轮）

- 复用既有 `_tmp_inc_build.cmd`（**未新建任何 .cmd**）。
- 第 1 次失败（`EXIT_CODE=1`）：`src\ground_vehicle.cpp(150): error C2039: "weight_override": 不是 "RoadVehicle" 的成员` —— `GroundVehicle<T,Type>::CargoChanged()` 是模板，`u` 是 `T*`，`weight_override` 是 `Train` 独有成员。修法=`const Train *r3r_tu = Train::From(u);` 后走 `r3r_tu->`（仅 `Type==Train` 时该块才会执行）。
- 第 2 次：`GUARD: incremental is safe`、`[6/6] Linking CXX executable openttd.exe`、`build\R3R_incbuild.done=EXIT_CODE=0`。
- 第 3 次（给 W3 加全局行数上限 `r3r_wlines < 4000` + 订正注释后）：`GUARD: incremental is safe`、`[3/3] Linking CXX executable openttd.exe`、`EXIT_CODE=0`；`ground_vehicle.cpp.obj` @00:04:49、`build\openttd.exe` @2026-10-24 00:06:13（52 298 752 B）——**这才是最终 exe**。
- 时间戳：源码 `newgrf_engine.cpp` 0:00:17 / `train_cmd.cpp` 0:00:25 / `vehicle.cpp` 0:00:27 / `ground_vehicle.cpp` 0:04:0x / `vehicle_cmd.cpp` 23:57:57 **均早于** 对应 obj（0:01:16 / 0:02:03 / 0:02:06 / 0:04:49 / 0:02:08）**均早于** exe。
- 错误计数 `error C`/`fatal error`/`FAILED:`/`build stopped` = 0；`read_lints` 五文件 0 条。
- 产物自证：exe 内 7/7 命中 `GRF-WEIGHT eng=` / `SEGFE-VEH head=` / `LIVERY-SCHEME v=` / `POSCONSIST veh=` / `CARGOWEIGHT-VEH head=` / `DEARTIC-SPLIT head=` / `MAKESEG-WEIGHT `。

### 5.8.3 复现与判读（请玩家按此跑两条路径）

需要 `R3R_debug.log` 与 `R3R_perf.log` 同级。启动 exe 时**必须**带 `R3R_DBG_TAGS=all`（`src\r3r_perf.h` 的标签白名单默认只放 RESV/UNRES/EXTEND/LOOP/R3R-RES，本轮 7 组探针不在默认集内；`R3RDbgWrite` 才走白名单，而本轮探针走 `R3RFopenDbg`，其开关是 `R3R_DBG_RAW`，默认开）——因此**默认即可出日志**；若玩家改过 `R3R_DBG_RAW=0` 需去掉。

- 路径 A（涂装漂移，H-458-1）：把涂装 2 的列车主动挂接涂装 1 的列车 → 看 `SEGFE-VEH` 里被挂方中间车的 `first=` 是否变成挂接方引擎；再看 `LIVERY-SCHEME` 的 `parent=` 是否=该 `first=`、`own=` 仍是中间车自己的引擎。
  - 若 `first` 漂移 + `LIVERY-SCHEME` 取 parent ⇒ **H-458-1 成立**（整链 `first_engine` 语义），与"升段"无关。
  - 若 `first` 未漂移而涂装仍变 ⇒ 改怀疑 GRF 自己的 cb36 变量（看 `GRF-WEIGHT` 同族的 cb 调用是否也整链）。
- 路径 B（×3，H-459-1'）：
  - `DEARTIC-SPLIT` 不出现 ⇒ 分叉 1（去铰接没跑）。
  - 出现但 `n=1` / `wq=wtot` ⇒ 分叉 2（均分算错）。
  - `n=3` 且 `wq≈wtot/3`，但 `CARGOWEIGHT` 的 `total` 是三倍、逐节 `ov=65535`（未烘焙生效）⇒ 分叉 3（烘焙后又被重置，或 `GetWeightWithoutCargo()` 没读到 override）。
  - `GRF-WEIGHT` 显示三节 `eng` 不同但 `ret` 都返回整组重量 ⇒ GRF 侧写法（三节引擎记录都写整组重量），这属于**GRF 与去铰接的交互口径**，需另定策略。

---

## 5.9 复测判读（第 290 轮，2026-10-24，玩家回两份日志；玩家原话「关于重量乘三我竟然复现不出来了」）

输入：`build\R3R_debug.log`（有 3 次"设为段"操作）与 `build\R3R_debug_1.log`（**0 次** `MAKESEG-CMD` / `DEARTIC-SPLIT`，纯读档+跑车）。两份都由 `build\openttd.exe` @2026-10-24 00:06:13（探针版）产生。

### 5.9.1 命令内守恒：烘焙没坏（H-459-1/2/3 全部证伪）

`MAKESEG-WEIGHT` 三批，BEFORE 与 AFTER **逐字相同**：

| 行号 | 内容 |
|---|---|
| `R3R_debug.log:339` / `:353` | `head=51 n=3 sumw=52 sumpw=156 cw=52 dmw=55` |
| `R3R_debug.log:618` / `:682` | `head=51 n=12 sumw=200 sumpw=600 cw=200 dmw=218` |
| `R3R_debug.log:824` / `:964` | `head=24 n=24 sumw=392 sumpw=1176 cw=392 dmw=430` |

`DEARTIC-SPLIT` 13 次全部 `n=3`，均分正确（商+余数=被除数）：`:341 head=51 n=3 wtot=52 wq=17 wr=1`、`:628 head=54 n=3 wtot=48 wq=16 wr=0`、`:827~:873` 同族。
⇒ **"设为段"命令本身守恒**，没有重复累加、没有整组重量写每节。

### 5.9.2 ×3 的真实形态：去铰接组"每节各按自己的重量计"，不是"烘焙放大"

`R3R_debug.log:76-100`（读档期，`head=21 n=24`）逐节全部 `art=0 ov=65535 pc=1 cw=ew`（`eng=623/620` 节 `ew=52`、`eng=622` 节 `ew=48`）⇒ `:100 CARGOWEIGHT head=21 n=24 total=1176 old=0`。
烘焙生效后同一链 `:897-920` 变成 `ov=18/17/16`、`total=392 old=392`。
`1176 = 3 × 392`，且 `sumpw` 恒等于 `3 × sumw`。

### 5.9.3 "复现不出来"的解释（关键）

- `R3R_debug_1.log` **全程 ×3 稳定存在**：`:100 1176`、`:208 1176`、`:438/:529/:657/:748/:839 1176`、`:924 1176`，挂成 48 节后 `:1143 n=48 total=2352`（=2×1176）并一直保持。该日志**没有任何**"设为段"操作。
- 即 ×3 的存活条件是 `art=0 + 角色位 + ov=65535`（**存档带入的去铰接组，但 override 没烘**）。`R3RDearticulateOneGroup()` 只对"当时仍是真铰接（`art=1`）"的组烘焙；对已 `art=0` 的组，再点"设为段"是**幂等跳过（不重新烘焙）** ⇒ 坏链永远修不回来，玩家用同一操作当然复现不出"变化"。
- `R3R_debug.log` 之所以显示 392，是因为中途该链被**重新合回真铰接**（`:728~:750` 出现 `art=1 pc=3/2/1`）后再被烘焙。
- 「猜测根因（待验证）」：把 `art` 从 0 恢复成 1 的只可能是 `RearticulateChain` 家族（降段/撤销段），但本轮无 DEMOTE 探针，日志不能直接证明；判据=下次带 `DECOUPLE/降段` 标签补一行探针。

### 5.9.4 GRF 侧事实（cb36）

`GRF-WEIGHT eng=621/622 ret=48 orig=0 signed=0`（恒定 48，每节都返回）；`eng=620/623` 无回调行、走记录值 52。
⇒ 该 GRF 本意是**每节 48t/52t**；引擎"铰接组只算父车"的规则把 3 节压成 1 节（1/3），R3R 的烘焙把 1/3 再均分回去（总量=饺接态值）。
⇒ **×3 本质是"GRF 每节带重量"与"引擎铰接只算父车"两条口径相加的结果**，不是纯 R3R 数值 bug。目标值（24 节该显示 392 还是 1176）需玩家拍板 = U-290-1。

### 5.9.5 涂装线（KI-458）

`LIVERY-SCHEME` 两份日志 **0 命中**（`R3R_debug_1.log` 里 `SEGFE-VEH|LIVERY-SCHEME` 合计 193 行，全是 `SEGFE-VEH`）。
⇒ 本会话根本没走 `GetEngineLivery()`→`GetEngineLiveryScheme()`（公司 Default 涂装方案无 2CC `in_use`）。
⇒ **H-458-1 在本轮既未证实也未证伪**；涂装若真漂移，只能来自 2CC colour mapping 回调（`CBID_VEHICLE_COLOUR_MAPPING`）或 `first_engine` 参与 GRF 变量，需把探针改到该回调。

### 5.9.6 下一步（待玩家拍板，未改码）

- 修法候选 A（推荐，治存量）：读档迁移——对"有 `ArticGroupHead/Member` 角色位 + 无 `art` 位 + `weight_override == UINT16_MAX`"的组补一次烘焙（同第 180 轮 `LEGACY-ARTIC-DEARTIC` 手法）。
- 修法候选 B：把 `CmdMakeSegment` 对已 `art=0` 的组改成"若 `ov==UINT16_MAX` 则重新烘焙"（幂等化，修不了读档即坏的链，但能让"再点一次设为段"起效）。
- 修法候选 C（口径）：若玩家要 1176（每节 48t），则去铰接不烘焙、改按每节 GRF 重量；这会与"升段前后总重不变"的设计目标冲突，需明确取舍。

---

## 5.10 玩家拍板（2026-10-24，原话要点）

1. **目标值 = 392**：「24 接应当显示 392」⇒ §5.9.3 的 24 节链正确总量是 **392**（`art=1` 只算父车 / 烘焙后的口径），`1176` 是错的。⇒ **U-290-1 关闭**，候选 C（改口径不烘焙）**否决**；R3R 现有烘焙口径正确，不需改行为。
2. **存量的坏链不治**：「存量的坏链就不用治了」⇒ **候选 A（读档迁移补烘焙）取消**，B（把"设为段"对 `art=0 && ov==UINT16_MAX` 幂等化）也**不列为必修**。⇒ KI-459 收口为"设计口径已确认 + 存量不追溯"，只在文档留档，不再投入改码。
3. **涂装要我重看 cargo subtype**：「涂装你没看出问题吗，你应该看 cargo subtype」⇒ 玩家指出涂装漂移的判据在 **`cargo_subtype`**（OpenTTD 选涂装的入参之一：`GetEngineLivery(engine_type, owner, cargo_type, cargo_subtype, random_bits)`），不是 `GetEngineLiveryScheme()` 的 `parent_engine_type`。⇒ §5.9.5「H-458-1 未证实未证伪」的结论作废，改按 `cargo_subtype` 取数，见 §5.11。

## 5.11 涂装线改按 cargo_subtype 重查（进行中）

待取证清单（只读）：
- `GetEngineLivery()` 的四/五个入参在 `src\vehicle.cpp` 里的实际用法，`cargo_subtype` 参与哪一步分支（`_engine_liveries` / `GetCargoSubtypeText` / `livery->in_use`）。
- `cargo_subtype` 的**全部写入点**（`cargo_subtype =` / `SetCargoSubtype` / refit 路径 / `ConsistChanged` / `CargoChanged`），判断 R3R 的拆铰接（`R3RDearticulateOneGroup`）、段尾假引擎（`SetSegmentTailFakeEngine`）、Couple/Decouple 是否会把它写成 0。
- 注意区分：R3R 自有的 `subtype`（`GVSF_ARTICULATED_PART` 等 `VehicleSubtype` 位）与 GRF 的 `cargo_subtype`（`Vehicle::cargo_subtype`，uint8）是**两个不同字段**，玩家指的显然是后者（`cargo subtype`）。

### 5.11.1 已确认的代码事实（本轮取证，只读）

- **涂装方案与本 bug 无关（玩家指路正确）**：`GetEngineLivery()` 的签名是 `(engine_type, company, parent_engine_type, v, livery_setting, ignore_group)`，**没有** cargo_subtype 入参（`src\vehicle_func.h:123`）；其内部只用 `GetEngineLiveryScheme(engine_type, parent_engine_type, v)`，而后者取的是 `v->cargo_type`（`src\vehicle.cpp:3192` `CargoType cargo_type = v == nullptr ? INVALID_CARGO : v->cargo_type;`）。⇒ §5.9.5 用 `LIVERY-SCHEME` 探针找涂装漂移是**找错了层**，0 命中不代表没问题。
- **`cargo_subtype` 的语义就是"涂装 refit 变体"**：`src\vehicle_base.h:354` 原文 `uint8_t cargo_subtype = 0;   ///< Used for livery refits (NewGRF variations)`。
- **建真铰接部件时会被清 0**：`src\articulated_vehicles.cpp:490` `v->cargo_subtype = 0;`（`AddArticulatedParts()` 内）。
- `src\train_cmd.cpp:2968` 的 `u->cargo_subtype = v->cargo_subtype;` 属于 `AddRearEngineToMultiheadedTrain()`（双头后半），是正常复制，不是嫌疑。
- 全库 `cargo_subtype` 的写入点只有：refit 路径（`vehicle_cmd.cpp:1204/1261`）、clone 路径（`vehicle_cmd.cpp:1951/1956/2006`，逐节照搬源车）、`linkgraph/refresh.cpp:162/170`、`order_cmd.cpp:4227/4234`、`articulated_vehicles.cpp:490`、`train_cmd.cpp:2968`、`vehicle_gui.cpp:845/864/1057/1097`（都是 refit 试算后还原）。⇒ **R3R 自己没有任何一处主动写 `cargo_subtype`**，所以漂移只能来自"打散/重设引擎后，读到的 `cargo_subtype` 不再是原来那一份"（部件由 `AddArticulatedParts` 清 0 vs 父车的 refit 值）。
- **现场日志**：`SEGFE-VEH` 共 33+ 行，形如 `head=21 veh=3 eng=622 first=623 ovr=1 cargo=0 segf=0 segb=0 artgm=0`，`cargo=` 恒 0、`first=` 恒等于链头引擎（如 head=24 全链 `first=623`、head=51 全链 `first=620`）⇒ 该探针**没有**记录 `cargo_subtype`，需要新增一个 cargo/subtype 探针才能判读。

### 5.11.2 GRF 侧怎么用 cargo_subtype（player 指路的落点）

- **变量 0x72 = 本车 `cargo_subtype`**：`src\newgrf_engine.cpp:1176` `case 0x72: return v->cargo_subtype;`。GRF 就是靠它给同一 engine 选不同涂装/变体 sprite。
- **另有"整链众数"变量**：`src\newgrf_engine.cpp:686-696`（`GetBestCargoSubtype`）沿 `v->Next()` 数每种 `cargo_subtype` 的出现次数并取**众数**（`common_subtypes[u->cargo_subtype]++` → `max_element`），空车返回 `UINT8_MAX`。⇒ **同一条链里 `cargo_subtype` 不一致时，链级读数是"多数派"**，而逐车 0x72 是各自的值。
- **真铰接部件的 `cargo_subtype` 恒为 0**：`src\articulated_vehicles.cpp:490` `v->cargo_subtype = 0;`（`AddArticulatedParts()` 建每个部件时写死）。而 `cargo_type` 是从父车复制的。⇒ 任何一个 refit 过的铰接组，组内 `cargo_subtype` 天然是 `{父车: N, 部件: 0, 部件: 0}`。

### 5.11.3 猜测根因（待验证）：打散铰接丢掉了父车的 cargo 身份

`R3RDearticulateOneGroup()`（`src\vehicle_cmd.cpp:522-586`）逐节只搬四样东西：
`SetArticGroupHead()/SetArticGroupMember()` + `SetWagon()/SetEngine()` + `weight_override` + `power_override` + `max_speed_override`。
**完全没有**搬 `cargo_type` / `cargo_subtype` / `cargo` 载体。

于是：
- 铰接态：整组物理上是一个单元，外观/涂装由组头（refit 过的 `cargo_subtype = N`）决定，部件虽各自为 0 但渲染上隶属同一组；
- 打散后：3 节各读各的 0x72 ⇒ 部件读到 **0**（旧变体/默认涂装），只有父车读到 N ⇒ 视觉上“组里某一节还是原涂装、另两节退回默认”。
- 若 GRF 还用了链级众数（`GetBestCargoSubtype`），`{N,0,0}` 的众数是 **0**，整链涂装直接判成默认变体 —— 用户说的“涂装漂移”正合此形。

同时 `SetSegmentTailFakeEngine()`（段尾假引擎）也走 `SetEngine()`，同样不继承 cargo 身份，属同族风险。

**待验证判据**：给 `DEARTIC-PART` 与 `CARGOWEIGHT-VEH` 行加 `cargo=%u/%u`（cargo_type/cargo_subtype）探针后，若打散前组内是 `{N,0,0}`、打散后仍是 `{N,0,0}` 而外观变了 ⇒ 是链级众数/渲染路径；若打散后父车与部件被改成同一个值 ⇒ 是某处统一了 cargo。

**修法方向（待拍板，本轮未改码）**：在 `R3RDearticulateOneGroup()` 里把组头的 `cargo_type`+`cargo_subtype` 复制给每个被拆出的部件（真铰接时它们本来就是同一外观单元）；`SetSegmentTailFakeEngine()` 同理继承链尾原车的 cargo 身份。

### 5.11.4 「livery_override / wagon override」这条路上 cargo_subtype 的真实位置

- `GetWagonOverrideSpriteSet(engine, cargo, overriding_engine)`（`src\newgrf_engine.cpp:56`）的**挑选条件只有两个**：`wo.cargo != cargo && wo.cargo != SG_DEFAULT` 与 `wo.engines` 是否含 `overriding_engine`。**没有** cargo_subtype。
- 调用点两处：`newgrf_engine.cpp:1383` `GetWagonOverrideSpriteSet(v->engine_type, v->cargo_type, v->GetGroundVehicleCache()->first_engine)`（**cargo 用 `v->cargo_type`，引擎用 `first_engine`**）；`:1372`（Self 情形）；纯查用 `:1873`（`DumpVehicleSpriteGroup`，正好是 dump_vehicle 的 sprite 组输出）。
- 拿到 override spriteset 之后，**sprite group 内部再由 NewGRF 变量决定画哪张图**，其中 `case 0x72: return v->cargo_subtype;`（`:1176`）。
  ⇒ 所以「涂装」实际上分两层：**选哪一组 spriteset** 由 `first_engine`+`cargo_type` 决定（H-458-1 说的那一层）；**组内画哪张图** 由 `cargo_type`/`cargo_subtype`（0x72）等变量决定（玩家指的这一层）。
- 结论：玩家说的「看 cargo subtype」= 若 GRF 的 spriteset 用 0x72 分支（livery refit 变体），那么**只要车的 `cargo_subtype` 从 N 变成 0，外观就变**，而这与段/链结构无关、也与 `GetEngineLiveryScheme` 无关 —— 这解释了为什么 `LIVERY-SCHEME` 探针 0 命中却仍有肉眼可见的涂装变化。
- **待验证**：①该 GRF 是否真用 0x72（可在 `DumpVehicleSpriteGroup` 的 sprite group dump 里看是否出现 `var 72` 分支）；②打散/设为段前后，同一车的 `cargo_subtype` 是否从 N 变 0；③`AddArticulatedParts()` 的 `cargo_subtype = 0` 是否是唯一来源。

---

## 5.12 玩家提出的一般化质疑（2026-10-24，必须落盘）

玩家原话：「我们的假铰接式没有继承真铰接式除了次序焊死之外的所有特性吗，理论上不会出现这样的问题啊」。

这条把问题从「某个字段没搬」升级为**设计口径质疑**：假铰接（`ClearArticulatedPart()` + `SetArticGroupHead()/SetArticGroupMember()` + 烘焙 `weight/power/max_speed_override`）本应与真铰接**只在「次序焊死（不可单独移动/卖出/重排）」这一点上不同**，其余读侧行为（重量、功率、速度、容量、渲染/涂装、GRF 变量、退款/造价、检修、装载）应逐项等价。

⇒ 若真铰接与假铰接在某处不等价，那就是一条**独立的实现缺口**，与「`cargo_subtype` 有没有被搬」是两个问题：即便 `cargo_subtype` 一样，只要某读侧判断的是 `IsArticulatedPart()` 而不是角色位，假铰接就会走到另一条分支。

**待办（只读取证，本轮进行）**：把 `IsArticulatedPart()` / `GetNextArticulatedPart()` / `HasArticulatedPart()` / `VehicleSubtype::ArticulatedPart` 在 `src\` 的**全部判定点**列成表，逐点标注「假铰接（`art=0` + `ArticGroupHead/Member`）是否等价」，等价项 = 已继承，不等价项 = 缺口。已知/预期的缺口候选（先落盘，待逐条证）：
- **重量/功率/速度**：真铰接由 `GetWeightWithoutCargo()` 跳过 `IsArticulatedPart()` 实现「只算父车」；假铰接改为 `art=0` + 烘焙 override 的**等价补偿**（口径等价、机制不同），已验证总量守恒（§5.9.1）。
- **渲染/涂装**：`GetWagonOverrideSpriteSet(v->engine_type, v->cargo_type, gcache.first_engine)` 与 `var 0x72 = v->cargo_subtype` 两条路径**都只看本车字段**，不看 `IsArticulatedPart()`；但真铰接部件的 `cargo_type` 是 `AddArticulatedParts()` 从父车复制的（`articulated_vehicles.cpp:490` 只把 `cargo_subtype` 写 0）⇒ 待验证 refit 是否也逐节写 artic 部件的 `cargo_subtype`。
- **其他候选缺口**（待查）：`Vehicle::ConsistChanged` 中的长度/容量汇总、`CheckTrainCollision`/`GetTrainPixelPos` 的几何、`CBID_VEHICLE_LENGTH`、`GetDisplayMaxWeight`、检修/`GetRefitCapacity`、`SellVehicle` 退款比例。
- **修法方向（同上，待拍板）**：`R3RDearticulateOneGroup()` 把组头 `cargo_type`/`cargo_subtype` 分发给拆出的部件；`SetSegmentTailFakeEngine()` 继承原车；`R3RPromoteFreeWagonChainToFront()` 造假引擎头时同样继承。

### 5.12.1 实测代码对照表（分层：假铰接到底"继承"了什么）

口径先说清：**真铰接的"焊死"不是靠一个开关，而是靠 `VehicleSubtype::ArticulatedPart` 这一个 bit 同时承担了四种职责**——① 语义（我是组员）② 遍历（父车之后跟着我、跨过我找真实单元）③ 统计（重量/功率/速度对我跳过）④ 身份（我是部件、不可独立存在）。R3R 打散时**把这一个 bit 拆成了两套东西**（角色位 + 烘焙值），所以"是否等价"必须**逐职责**回答，不能笼统答"继承了"。

| 层 | 真铰接的实现 | 假铰接的实现 | 是否等价 | 证据 |
|---|---|---|---|---|
| 语义层（是不是组员/组头） | `IsArticulatedPart()` | `IsArticGroupMember()` / `IsArticGroupHead()` 虚函数，Train 侧回落到 `ArticGroupMember/ArticGroupHead` 标志位；基类默认实现回落 `IsArticulatedPart()` ⇒ 通用代码通过虚函数看到同一语义 | **等价** | `vehicle_base.h:1193/1204/1210` |
| 遍历层（组当一单元走） | `Next()` 串父车后 + 各调用点自行 `IsArticulatedPart()` 判断 | `HasArticulatedPart()` 改看 `Next()->IsArticGroupMember()`；`GetNextArticulatedPart()` 返回 `Next()` | **等价** | `vehicle_base.h:1218/1228` |
| 数值层（重量/功率/最高速） | 统计时 `if (!IsArticulatedPart()) weight += ...` ⇒ 只算父车 | `art=0`（每节都被算）+ 每节带 `weight_override/power_override/max_speed_override`，值 = 组总量按节均摊（余数给组头）⇒ 求和等价 | **数值等价、机制不同**（`GetWeightWithoutCargo()` 两条分支互斥：override 优先，`override==UINT16_MAX` 时才落 `!IsArticulatedPart()` 旧路） | `train.h:484-489`（`GetWeightWithoutCargo`）、`train_cmd.cpp:22908-22911`、`train.h:276-278`（字段）、`vehicle_cmd.cpp:544-559`（烘焙写入） |
| 统计分摊的**反向**依赖 | `GetEnginePartsCount()` 数真 `IsArticulatedPart()` 的节数，`GroundVehicle::CargoChanged` 用它把 cargo 重量摊给**组头** | **刻意不等价**：假铰接成员 `art=0` ⇒ `count` 只会数到真 artic 部件 ⇒ 成员各自带自己那份烘焙值、不参与再分摊 | **故意不等价（正确）** | `vehicle_base.h:1234-1246`（注释自陈理由） |
| 身份层（不可单独移动/卖/重排） | `IsArticulatedPart()` 把关所有命令/车库/GUI | 用 `SegmentFront/SegmentBack` + 角色位 + 命令层闸门模拟 | **等价（这是"次序焊死"本身）** | 既有 KI 体系 |
| **货物/涂装层** | 部件的 `cargo_type` 由 `AddArticulatedParts()` 从父车复制、`cargo_subtype` 写 0；运行时 refit/`CargoChanged` 对整组逐节处理 | **没有任何 override/继承通道**：`cargo_type`/`cargo_subtype` 是普通逐车字段，打散后每节各持一份、没人把组头的值分发下去 | **缺口（唯一）** | `articulated_vehicles.cpp:490`；`R3RDearticulateOneGroup()`（`vehicle_cmd.cpp:522-586`）全程不碰 cargo 字段 |

⇒ **对玩家原话的回答：不是"只有次序焊死不同"。** 实际是三层等价（语义/遍历/身份）、一层用数值补偿（重量功率速度）、一层刻意不等价（`GetEnginePartsCount` 的反向分摊，属于修 bug 必需的例外）、**一层完全没管（cargo 身份 ⇒ 涂装）**。所以"理论上不会出现这样的问题"这个推论在 cargo 层不成立。

**必须同时落盘的两条防误判**（否则会去改错地方）：
1. **拆组那一刻 cargo 不会变**。`R3RDearticulateOneGroup()` 不写 `cargo_type`/`cargo_subtype`，`ClearArticulatedPart()` 也只动 subtype 位。所以"一开始打散就掉涂装"不成立，掉涂装必然发生在**打散之后的某个事件**上（refit / 装载 / `ConsistChanged` / `CargoChanged`）。
2. **真铰接部件的 `cargo_subtype` 本来就是 0**（`articulated_vehicles.cpp:490` 写死）。所以"组头是 N、部件是 0"在真铰接下也存在；差别在于**真铰接每帧被父车带着一起重算**，而假铰接的成员是独立车，`cargo_subtype` 一旦被某条路径清 0 就永久停在那里，没有任何机制把它拉回组头的值。

**待验证（取数清单，未做）**：
- V-1：`DEARTIC-PART` 探针加 `cargo=%u/%u`（type/subtype），看打散前后是否逐字不变 ⇒ 若变，说明是我方写入；若不变，缺口 100% 在上表最后一行。
- V-2：`DumpVehicleSpriteGroup` 查该 GRF 的 spriteset 是否出现 `var 72` 分支 ⇒ 决定涂装是否真由 `cargo_subtype` 驱动（若不用 0x72，本条与涂装无关，要另找）。
- V-3：refit 写点 `vehicle_cmd.cpp:1204/1261` 对 `art=0` 的假铰接成员是否会**只写链头/只写组头**（真铰接时代靠 `HasArticulatedPart()` 遍历整组，现在每节都是独立单元）。

**修法方向（与重量同构，待拍板，未落地）**：
- **A 无条件分发**：`R3RDearticulateOneGroup()` 把组头的 `cargo_type`+`cargo_subtype` 写给每个成员；`SetSegmentTailFakeEngine()`、`R3RPromoteFreeWagonChainToFront()` 同伴。
- **B 保守分发**：仅当成员 `cargo_subtype == 0` 且组头 `!= 0` 时复制（保留成员曾单独 refit 过的证据）。
- **C 读侧回落（最彻底）**：在 `newgrf_engine.cpp:1176` 的 `case 0x72` 对 `IsArticGroupMember()` 回落到组头的 `cargo_subtype`（不改任何字段、旧档读入天然正确，代价=成员不能单独 refit）。

#### 5.12.1 续：V-3 已取数（2026-10-24，结论=refit 不是清零源，反而会重写）

- `RefitVehicle()`（`vehicle_cmd.cpp:1176-1251`）**R3R 早已改用语义层**：`:1178` `if (!v->IsArticGroupMember()) actual_subtype = new_subtype;` ⇒ 组员沿用组头的 `actual_subtype`，与真铰接（部件沿用父车）**等价**；`:1204`（试算临时写）与 `:1261`（Execute 终写）都是**逐节** `v->cargo_subtype = actual_subtype` ⇒ refit 会把整组分到同一个 subtype，**不会**把成员清 0。
- `SetSegmentTailFakeEngine()`（`vehicle_cmd.cpp:722-752`）只对**链尾已有那节**做 `SetEngine()/ClearWagon()/ClearFreeWagon()`，**不新建车、不碰 cargo 字段**；`R3RPromoteFreeWagonChainToFront()`（`:278`）同样只改身份标志位。
- ⇒ **R3R 自身没有任何一处把 `cargo_subtype` 写成 0**（全库写点清点：refit `:1204/:1261`、clone `:1951/:1956/:2006`、`articulated_vehicles.cpp:490`（仅真 artic 部件）、`train_cmd.cpp:2968`（双头后节继承前半）、`linkgraph/refresh.cpp:162/170`、`order_cmd.cpp:4227/4234`、`vehicle_gui.cpp:845/864/1057/1097`）。
- ⇒ **缺口性质修正**：不是"某处清零了"，而是**假铰接成员是一辆独立车、没有任何机制在状态变化时把组头的 cargo 身份重新同步给它**（真铰接靠"父车带着部件每帧重算"隐式同步）。所以触发点必须是"成员自己的 cargo 字段被某条非 refit 路径改写"，或**涂装其实不是 `cargo_subtype` 驱动而是 `gcache.first_engine` 驱动**（`GetWagonOverrideSpriteSet` 的第三个参数，`newgrf_engine.cpp:56/1383`）——后者在打散/耦合改变链结构时会变，是与 KI-458 完全不同的一个候选根因。
- **下一步判据（V-2/V-3 收口）**：`DumpVehicleSpriteGroup` 看该 GRF spriteset 是否含 `var 72`；若含，再比打散前后逐节 `cargo_subtype` 读数（`DEARTIC-PART` 加 `cargo=%u/%u`）。两者都不动 ⇒ 转查 `first_engine`。

---

## 5.13 玩家拍板（2026-10-24）：cargo 身份跟随 ArticGroupHead —— 已定，按此实现

玩家原话：「我们直接让货物跟涂装（cargo subtype）去跟 artic 组头吧，这应该就是缺口」。

**口径落定**：`cargo_type` 与 `cargo_subtype` 都以 **ArticGroupHead 的值**为准，成员车不再各自为政。理由 = 这正是真铰接的隐式行为（父车持有 cargo 身份、部件由父车带着重算），而假铰接拆出的是独立车，缺的正是这条"跟随"通道；§5.12.1 已证语义/遍历/数值三层都已等价，只有这一层没有。

**实现分工（两段，缺一不可）**：
1. **读侧回落（治存量）**：凡"决定外观"的读取点，成员车一律改成读组头的值。必须做的原因 = 玩家现有存档里的链**早就打散过了**，只补写侧对它们完全无效。
   - `newgrf_engine.cpp` 的 `case 0x72:` → 组头的 `cargo_subtype`（组件内变体）。
   - `newgrf_engine.cpp:1383` 的 `GetWagonOverrideSpriteSet(engine, cargo, first_engine)` 第二参 → 组头的 `cargo_type`（选哪组 spriteset）；`:1389` 的 fallback 同理。
   - `train_cmd.cpp:450` 的 `u->tcache.cached_override` 同理（缓存路径，否则只有非缓存路径被修好）。
2. **写侧分发（治增量）**：`R3RDearticulateOneGroup()` 在把部件变成独立车时，把组头的 `cargo_type`/`cargo_subtype` 写给每个成员，使新打散的链字段本身就一致（装载、容量、`GetBestCargoSubtype()` 等非渲染读点随之正确）。

**硬约束（关键，否则会误伤原生铰接）**：回落**只对假铰接成员生效**，判据必须是 `IsArticGroupMember() && !IsArticulatedPart()`。真铰接部件按上游设计 `cargo_subtype` 恒为 0（`articulated_vehicles.cpp:490`）且**故意**与父车不同（部件画默认变体），若对它也回落就等于改写原生 GRF 行为 ⇒ 绝对禁止。组头判定同样要求 `IsArticGroupHead() && !IsArticulatedPart()`。

**不做（本轮范围外）**：`gcache.first_engine`（H-458-1 那条"中间车被隔壁头车带跑"的路径）不动；`GetBestCargoSubtype()` 的链级众数不动；`cargo_type` 的容量/装载语义不改。

**探针**：`DEARTIC-PART` 行追加 `cargo=%u/%u`（cargo_type/cargo_subtype），用于证明写侧分发前后数值与「成员==组头」。

### 5.13.1 构建自证（2026-10-24）

- 复用既有 `_tmp_inc_build.cmd`（未新建任何 `.cmd`）；启动前 `tasklist` 确认 `openttd.exe` 未运行。
- `build\R3R_incbuild.guard.log` = `GUARD: incremental is safe (no header/lang file is newer than the newest object)`（本轮只改 3 个 `.cpp`，未碰任何 `src\*.h` 与 `src\lang\*.txt`）。
- 日志：`[2/5] newgrf_engine.cpp.obj`、`[3/5] train_cmd.cpp.obj`、`[4/5] vehicle_cmd.cpp.obj`、`[5/5] Linking CXX executable openttd.exe`。
- `build\R3R_incbuild.done` = `EXIT_CODE=0`；`error C|fatal error|FAILED:|build stopped` 计数 **0**。
- 时间戳：`newgrf_engine.cpp` 13:35:33 → obj 13:37:30；`train_cmd.cpp` 13:35:47 → obj 13:37:32；`vehicle_cmd.cpp` 13:35:58 → obj 13:37:35；`build\openttd.exe` **2026-10-24 13:38:51（52 298 752 B）**晚于全部源码。
- `read_lints` 三文件 0 条。
- 产物自证：`findstr` 在 `openttd.exe` 中命中 `cargo=%u/%u` 与 `DEARTIC-PART head=%d` 各 1 处。

### 5.13.2 复测判据（游戏内）

1. **正判据（玩家原始现象）**：A 列（涂装 2）中间车挂上 B 列（涂装 1）之后，中间车涂装**不再**变成 1。
2. **`DEARTIC-PART` 读数**：打散产生的每一行 `cargo=` 都与组头那一行一致（写侧分发生效）。
3. **存量修复**：旧存档里那条已经漂移的链，读入后外观**立即**回到组头的涂装（读侧回落生效，不需要重新 refit）。
4. **负判据（硬性）**：原生 GRF 铰接车（`art=1` 的真部件）外观**逐字不变**——真部件的 `cargo_subtype` 仍为 0、仍画默认变体，不被父车值覆盖。
5. 非渲染读点不回归：装载/容量/`GetBestCargoSubtype()` 行为不变（若出现货种错乱即说明写侧分发过界，需回退到只做读侧）。

### 5.13.3 回退（2026-10-24，玩家实测反馈）

**玩家原话**：「好像得回退了（我不确定），我承认我有点太激进了，这回列车直接只有一个改装选项了」。

**现象**：应用 5.13 的改动后，列车（refit）窗口只剩**一个**改装选项，原本是多种货种可选。

**判定：这是 5.13.2 判据 5 说的"写侧分发过界"，必须回退。** 机制推断（待复测确认）：refit 可用货种列表在 NewGRF 侧由 `cargo_type`（部分 GRF 还叠加 `cargo_subtype` / `var 72` 的 spriteset 变体）参与筛选；我方把**所有成员的 `cargo_type` 统一成组头值**之后，整条链对"可按货种区分的候选集"退化成只有组头那一种 ⇒ 选项只剩一个。读侧回落（0x72 / wagon-override 的 cargo）同理会收窄 GRF 能看到的变体。

**结论**：这条修法的前提假设（"组头对整组拥有唯一 cargo 身份"）与**改装（refit）语义相冲突**——真铰接之所以能这样，是因为部件本来就没有独立 cargo 容量；而假铰接的成员是**各自有容量的独立车**，它们的 `cargo_type` 是真实的业务数据，不是渲染字段。**不能用"跟随组头"去覆盖它。**

**回退范围（全部撤销，恢复 upstream 行为）**：
1. `newgrf_engine.cpp`：删除 `R3RArticGroupHeadOf()`；`case 0x72` 恢复 `return v->cargo_subtype;`；`GetWagonOverrideSpriteSet` 第二参与 fallback spriteset 恢复 `v->cargo_type`。
2. `train_cmd.cpp`：删除 `R3RArticGroupHeadOf()`；`ConsistChanged()` 的 `u->tcache.cached_override` 恢复 `u->cargo_type`。
3. `vehicle_cmd.cpp`：删除 `R3RDearticulateOneGroup()` 里的 `p->cargo_type = v->cargo_type; p->cargo_subtype = v->cargo_subtype;` 两行。

**保留**：`DEARTIC-PART` 行的 `cargo=%u/%u` 探针（纯只读，零行为影响），作为下一轮重新取证的唯一手段。

**下一步方向（重新设计，未动手）**：如果涂装问题仍在，正确做法应改为**只读渲染层、绝不写 `cargo_type`**：即仅在 `case 0x72`（纯渲染变量）对假铰接成员回落到组头，且必须先证伪"该 GRF 用 `var 72` 决定 refit 选项列表"（若用，则连这条也不能做）。或转查 H-458-1（`gcache.first_engine`）。

---

## 5.14 回退后补的只读探针（2026-10-24，玩家要求「顺手把必要探针加上去」）

**约束**：全部纯只读、零行为变更、不改 `src\*.h`（保增量构建合法）、不进任何热路径的高频刷屏。

> **落地修订**：本节原拟名 `PR-1 R3R-REFIT-CHAIN` / `PR-2 R3R-CB15`，实际落地按 5.14.2 拆成 `R3R-REFIT-VEH` + `R3R-REFIT-LIST/OPT`（入/出两侧）与 `R3R-CB72`（`case 0x72` 边沿去重，取代原 `0x15` 方案——`BuildRefitList` 走的是 `GetCargoSubtypeText` → `CBID_VEHICLE_CARGO_SUFFIX`，问的是 `var 72` 而非 refit 容量回调）。**以 5.14.2 的探针名为准。**

**探针 PR-1 `R3R-REFIT-VEH` + `R3R-REFIT-LIST`/`R3R-REFIT-OPT`**（落点：`BuildRefitList()` 入/出两侧）——把"这条链当前对改装窗口呈现什么"一次性 dump 出来：
- 入口侧逐节 `index`、`cargo_type`、`cargo_subtype`、`IsArticGroupHead()`、`IsArticGroupMember()`、`IsArticulatedPart()`；
- 出口侧 `win=`/`sel=`/`nveh=`/`cargoes=` 与逐货种 `options=`（即最终进列表的选项数）；
- 目的：判定"只剩一个选项"到底是①链级掩码退化，还是②逐节掩码在 UI 取交集时退化，还是③GRF 回调自己按 `cargo_type/var72` 只回一种。

**探针 PR-2 `R3R-CB72`**（落点：`newgrf_engine.cpp` 的 `case 0x72`）——记录 GRF **看见**的 `v->cargo_type`/`v->cargo_subtype`：
- 按 `index & 4095` 槽位边沿去重（仅当该车 `cargo_subtype` 变化时打），防止窗口每帧刷新刷屏；
- 目的：证明"refit 期间 `cargo_type`/`cargo_subtype` 是候选值的临时载体"这一机制（上一轮回退的直接依据），并给出该 GRF 实际被问到的候选集合作对照。

**探针 PR-3 `DEARTIC-PART ... cargo=`**（已存在，保留）——拆组瞬间逐节 `cargo_type`/`cargo_subtype`，与组头对比。

**判读口径**：若 `R3R-CB72` 显示 GRF 被逐个候选 `sub=` 问过、而 `R3R-REFIT-OPT` 的 `options=` 只剩 1 ⇒ 退化发生在掩码/交集计算（与 cargo 身份无关，转查别处）；若 `R3R-REFIT-VEH` 里各节 `cargo=` 出现同组头同值、且 `R3R-CB72` 的 `sub=` 在不同 `refit_cyc` 下恒定同一值 ⇒ 坐实"整链 `cargo_type`/`var72` 统一会压掉候选集"这一 5.13.3 机制推断。

### 5.14.1 回退真因的代码级证据（已读到源头，无需再猜）

`vehicle_gui.cpp:1005 BuildRefitList()` —— 改装窗口的选项列表在这里生成，机制是**逐节取交集**：

```1051:1070:src/vehicle_gui.cpp
					CargoType temp_cargo = v->cargo_type;
					uint8_t temp_subtype  = v->cargo_subtype;

					v->cargo_type = cargo_type;

					for (uint refit_cyc = 0; refit_cyc < MAX_REFIT_CYCLE; refit_cyc++) {
						v->cargo_subtype = refit_cyc;

						/* Make sure we don't pick up anything cached. */
						v->First()->InvalidateNewGRFCache();
						v->InvalidateNewGRFCache();

						StringID subtype = GetCargoSubtypeText(v);
```

- `:1041` 先塞一条"保持当前子类型"（`subtype = UINT8_MAX`），这是**唯一必然存在**的那条。
- 之后用 `refit_cyc = 0,1,2,…` **临时改写 `v->cargo_type` / `v->cargo_subtype`**，再问 GRF（`GetCargoSubtypeText` → `CBID_VEHICLE_CARGO_SUFFIX` 回调），把返回的每个 subtype 名字加进列表；`:1072-1078` 对多节链与前一节的列表做**交集**，`subtype == STR_EMPTY` 就立即截断。
- ⇒ 我上一轮的 `case 0x72` 回落正好覆盖了这个临时值：**无论 `refit_cyc` 是多少，GRF 看到的 `cargo_subtype` 都是组头那个固定值**，返回同一个名字 ⇒ 循环在第一个 `STR_EMPTY`/重复项处断掉 ⇒ 每个货种只剩 `:1041` 那一条"保持当前" ⇒ 玩家看到"只有一个改装选项"。这与"选项变少但仍有多个"的形态差别，正是因为它把所有候选压成了同一个值。

**由此证伪：`var 72` 读侧回落这条路线不能做。** 它不是"纯渲染变量"——`BuildRefitList` 恰恰把它当作"枚举候选 subtype"的输入通道。同理，`newgrf_engine.cpp:1383/:1389` 与 `tcache.cached_override` 的 cargo 回落也落在同一个 `VehicleResolverObject` 输入上（refit 枚举用的就是 `WagonOverride::None/Cached` 之外的解析路径），一样会污染。

**下一轮唯一还站得住的方向**：只在**绘制专用入口**（`GetCustomEngineSprite` 那条 `WagonOverride::Cached` + `CBID_NO_CALLBACK` 的路径，`newgrf_engine.cpp:1402`）做回落，且**不得**改动 `cached_override` 的缓存键与任何回调路径的 cargo 输入。可行性待 PR 探针取数后判定。

### 5.14.2 探针落地（2026-10-24，纯只读）

| 探针 | 文件/落点 | 输出 | 用途 |
|---|---|---|---|
| `R3R-CB72` | `newgrf_engine.cpp` 的 `case 0x72`（`v != nullptr` 且 `Train`） | `idx= sub= cargo= art= gm= gh=` | 按 `index&4095` 槽位**边沿去重**，只在该车 `cargo_subtype` 变化时打；证明 refit 窗口在临时改写该值（正常应看到 `sub=0→1→2…`） |
| `R3R-REFIT-VEH` | `vehicle_gui.cpp` 的 `BuildRefitList()` 逐节循环内 | `head= idx= cargo=%u/%u art= gm= gh=` | 列表输入侧基线：各节 cargo 身份是否漂移 |
| `R3R-REFIT-LIST` / `R3R-REFIT-OPT` | 同上，函数末尾 | `win= sel= nveh= cargoes=` / `cargo= options=` | 每种货种最终选项数；回退后应恢复多选项 |
| `R3R-REFIT-DONE` / `R3R-REFIT-UNIT` | `vehicle_cmd.cpp` 的 `RefitVehicle()` 执行分支末尾 | 整链逐节 `cargo=%u/%u` | refit 落地后是否会漂移 |
| `DEARTIC-PART ... cargo=` | `vehicle_cmd.cpp` 拆组探针（既有） | 逐节 cargo | 拆组瞬间的组头/成员对比 |

三支新探针均只在低频路径触发（窗口重建、用户 refit、单个值变化），无每帧刷屏风险；实现后 `read_lints` 0 条。

## 5.15 探针自身导致崩溃（2026-10-24，玩家实测：「你怎么还能探针让游戏卡死的」）

### 5.15.1 现场证据（崩溃日志，一次定位，无需猜）

`C:\Users\冯洁敏\Documents\OpenTTD\crash-20261024T055606Z.log`（19 451 B，写于 13:56:08）——产生者与 5.14.2 的 exe 同一版（`build\openttd.exe` 13:51:56）。

```
:9   Exception:  C0000005   Fault type: 0 (read)   Fault addr: 000000000000003C
:16  0: DoCommandP: ... cmd: 0x2B CmdRefitVehicle, payload: 25, 0, 1, false, false, 1
:29  [00] openttd 0x...  RefitVehicle + 2850 (D:\sourcecode of JGRPP\src\vehicle_cmd.cpp:1276)
:30  [01] openttd 0x...  CmdRefitVehicle + 2044 (D:\sourcecode of JGRPP\src\vehicle_cmd.cpp:1384)
:37  [08] openttd 0x...  RefitWindow::OnClick + 1089 (src\vehicle_gui.cpp:1615)
:57  RAX: 0000000000000000
:64  Bytes at instruction pointer: 80 78 3C 00 0F 85 7A 02 ...
```

`80 78 3C 00` = `cmp byte ptr [rax+0x3C], 0`，`Vehicle::type` 的偏移正是 `0x3C`，而 `RAX = 0`
⇒ **`v == nullptr` 时读了 `v->type`**。崩溃行 `:1276` 就是我加的 `R3R-REFIT-DONE` 探针的守卫条件：
`if (flags.Test(DoCommandFlag::Execute) && v->type == VehicleType::Train && R3RDbgOn())`。

### 5.15.2 根因（我的探针 bug，与被测行为无关）

```1158:1177:src/vehicle_cmd.cpp
static CommandCost RefitVehicle(Vehicle *v, bool only_this, uint8_t num_vehicles, ...)
{
	...
	for (; v != nullptr; v = (only_this ? nullptr : v->Next())) {
```

`RefitVehicle()` 的**函数参数 `v` 被循环自身复用并推进**，循环正常结束后 `v == nullptr`。我在 `:1276`
（循环之后）解引用 `v` ⇒ 必然空指针。触发条件=玩家在改装窗口点一次改装（`CmdRefitVehicle` → Execute 分支）。

**性质定级**：这是我新加探针引入的崩溃，不是 KI-458 的行为回归、也不是回退引入的。上一版（无本探针）不会在此处崩。

**教训（写进长期纪律）**：
- 探针不得引用「被业务循环消费掉的可变指针」；必须在**最后一个可信解引用点之前**取快照（`Vehicle *const snap = v;`）。
- 在循环之后的代码里，唯一可信的车辆来源是循环产出的容器（本案 `refit_result`），而不是循环变量/原参数。
- 同一函数内若有 `for (; v != nullptr; v = v->Next())` 这类「把参数当游标」的写法，任何追加代码都必须先确认 `v` 的当前值。

### 5.15.3 修法（2026-10-24）

1. `vehicle_cmd.cpp` 的 `RefitVehicle()`：函数入口（`:1160` 之前）取快照 `Vehicle *const r3r_probe_veh = v;`（此时 `v` 必非空），
   `:1276` 与 `:1279` 的 `v` 一律改用 `r3r_probe_veh`（`->type` / `->First()`）。
2. 顺带修掉 `R3R-REFIT-DONE` 打错的值：原来打 `new_subtype`，但该参数为 `0xFF`（"自动选最合适子类型"）时无意义，
   改为同时打 `actual_subtype`（循环里最终确定值）与 `new_subtype`。
3. `vehicle_gui.cpp` 的 `R3R-REFIT-VEH`：原来**每节车一次 `fopen`/`fclose`**，改装窗口每点一次货种就开关几十次文件，
   在 Windows 上制造明显卡顿（玩家「卡死」观感的另一半来源）。改为循环外开一次、循环内只 `fprintf`。

其余两支探针（`R3R-CB72`、`R3R-REFIT-LIST/OPT`）经复核无同类隐患：前者已先判 `v != nullptr` 再解引用，后者不出现在循环变量作用域之外。

## 5.16 探针首轮取数（2026-10-24 14:03，玩家已复跑）

**输入有效性**：`build\R3R_debug.log` 538 401 B / 14:03:01，产生者为 14:00:33 的 exe（= §5.15 崩溃修复版）⇒ **采样有效，探针全部在位**。

**命中计数**：`R3R-REFIT-LIST` 6 / `R3R-REFIT-OPT` 6 / `R3R-REFIT-VEH` 102 / `R3R-REFIT-DONE` 2 / `R3R-REFIT-UNIT` 48 / `R3R-CB72` 7 704。

### 5.16.1 铁证 1：`var 72` 回落路线永久证伪（H-458-2 的 var72 分支关闭）

```
R3R-CB72 idx=24 sub=0 cargo=0 art=0 gm=0 gh=1
R3R-CB72 idx=24 sub=1 cargo=0 ...
R3R-CB72 idx=24 sub=2 ... -> 3 -> 4 -> ... -> 39（逐行连续递增，cargo 恒 0）
```

`idx=24` 是链头（`gh=1`），`sub` 在**同一批调用里 0→1→…→39 连续递增**、`cargo` 恒 `0`。
⇒ 改装窗口 `BuildRefitList()` 确实用"临时改写 `v->cargo_subtype` + `refit_cyc` 递增 + 问 GRF"的方式枚举候选子类型。
⇒ **任何在 `case 0x72` 里把 `cargo_subtype` 回落成组头值的做法，都会让每一轮问到的子类型相同 ⇒ GRF 每轮返回同名 ⇒ 列表立刻截断为 1 条。** 与 §5.14.1 的回退依据完全一致，此路线今后**不得再尝试**。

### 5.16.2 铁证 2：塌缩发生在"多节取交集"，不在 GRF

```
R3R-REFIT-LIST win=24 sel=24 nveh=255 cargoes=1 auto=0   ← 整链
R3R-REFIT-OPT  cargo=0 options=1                          ← 只有 1 个选项
R3R-REFIT-LIST win=24 sel=25 nveh=1   cargoes=1 auto=0    ← 单节
R3R-REFIT-OPT  cargo=0 options=7                          ← 7 个选项
R3R-REFIT-LIST win=24 sel=49 nveh=1   cargoes=1 auto=0    ← 单节
R3R-REFIT-OPT  cargo=0 options=7                          ← 7 个选项
```

6 次窗口重建的读数：`sel=24`（整链，`nveh=255`）**3 次全是 `options=1`**；`sel=25`、`sel=49`（`nveh=1`，单节）**各 1 次 `options=7`**。
⇒ 该 GRF 对单节**能给出 7 种子类型**，所以"只有 1 个"不是 GRF 只支持一种，而是**多节链把它们交集成 1 条**。
⇒ 塌缩位置确定在 `BuildRefitList()` 的多节循环（`vehicle_gui.cpp:1072-1091` 的"每节问一次、取交集、`STR_EMPTY` 即截断"），**不是** `case 0x72`、**不是** cargo 身份层。

### 5.16.3 改装落地

```
R3R-REFIT-DONE head=24 cargo=0 sub=1 actual=1   ×2
```

两次改装都把 `sub` 落到 `1`（`new_subtype` 与 `actual_subtype` 一致 ⇒ 未走 `0xFF` 自动分支）。
`R3R-REFIT-UNIT` 48 行 = 2 次 × 24 节，待核"是否整链同值"。

### 5.16.4 疑点与待验证

- **REG-290-A（待查）**：`nveh=255` 是 `this->num_vehicles` 的真实值还是 `UINT8_MAX` 哨兵（表示"整列"）？若为哨兵，则"255"与"节数"无关，只是"选了整列"的标记——**只影响表述，不影响结论**（`sel=24` 与"整链"的对应关系由 `sel` 与 `options=1` 的三次一致读数支撑）。
- **H-458-3（新，猜测根因待验证）**：多节塌成 1 的直接机制可能是——①某节（假铰接成员）`cargo_subtype` 与组头不同 ⇒ 该节对每个 `refit_cyc` 返回的子类型文本互不相同 ⇒ 交集为空 ⇒ 落到"保持当前"那 1 条；②或 `STR_EMPTY` 在**第一轮**就命中（某节该货种根本不支持子类型）。
  判据：`R3R-REFIT-VEH` 102 行里，`sel=24`（`options=1`）那几次的逐节 `cargo=%u/%u` 若**存在与链头不同的 `sub`**（如组头 `sub=1`、某成员 `sub=0`）⇒ 支持 ①。
- **下一步（本轮唯一取证方向）**：取 `R3R-REFIT-VEH` 全部 102 行，按 `head=` 分组比对逐节 `cargo/sub`，定位"哪一个 idx 的 sub 与组头不同"。

### 5.16.5 铁证 3：`R3R-REFIT-VEH` 逐节读数——链结构与 refit 覆盖面

链结构（`head=24`，共 24 节，每 3 节一个假铰接组）：

```
idx=24 gh=1 gm=0 | idx=25 gh=1 gm=1 | idx=26 gh=0 gm=1   <- 组 1（3 节）
idx=27 gh=1 gm=0 | idx=28 gh=1 gm=1 | idx=29 gh=0 gm=1   <- 组 2
... 每 3 节同形 ...  idx=42/43/44                       <- 组 7
idx=48 gh=1 gm=0 | idx=49 gh=1 gm=1 | idx=50 gh=0 gm=1   <- 组 8（idx 45/46/47 不在本链）
```

⇒ 8 个假铰接组 × 3 节 = 24 节，`art=0` 全链（无真铰接部件），`gh` 在"组长"与"组第 2 节"上为 1、第 3 节为 0（这是 `IsArticGroupHead` 的既有语义，不是新问题）。

**改装前**：24 节全部 `cargo=0/0`（sub 一致为 0）——**链上没有任何节间 `sub` 差异**，而这一次窗口读数仍是 `options=1`。
⇒ **塌缩与"节间 sub 不一致"无关**，`H-458-3 ①` 被排除。

**改装后的真实落地值**（`R3R-REFIT-UNIT`，在 Execute 分支末尾按 `v->First()` 重新逐节 dump，**这才是权威**）：

```
第 1 次改装（日志 :4242-4265）：i=0/1/2 (idx=24/25/26) sub=1 ；i=3..23 (idx=27..50) 全部 sub=0
第 2 次改装（日志 :6821-6844）：仅追加 i=21/22/23 (idx=48/49/50) sub=1 ；i=3..20 (idx=27..44) 仍 sub=0
```

⇒ **两次改装各自只写了"被选中的那一组 3 节"**：第 1 次选的是 `sel=25`（属组 1，`idx 24/25/26`），第 2 次选的是 `sel=49`（属组 8，`idx 48/49/50`）；其余各节**逐字未动**。

- 对应窗口读数：`R3R-REFIT-LIST sel=25 nveh=1`、`sel=49 nveh=1`（`nveh=1` = 只改装这一节），而 `sel=24 nveh=255` = 整列哨兵。
- ⇒ 写入侧**没有 bug**：`GetVehicleSet(vehicles_to_refit, v, 1)` 把"这一节"扩成它所在的假铰接组（3 节），这正是 `num_vehicles=1` 的设计语义（vanilla 里"1 节"对真铰接车也是整组）。
- ⇒ **KI-460 原判「`RefitVehicle()` 整链写入会漏节 / `GetVehicleSet()` 对 `UINT8_MAX` 少算尾部」证伪、撤回**（本人第二次误读：上一版把两次 24 行的 dump 串起来看成"一次改装 24 节"，其实日志里是两条独立命令各 24 行）。

**登记（更正后）**：
- **KI-460（撤回，不是缺陷）**：refit 落地覆盖面 = 与窗口 `num_vehicles` 一致（1 ⇒ 本组 3 节；255 ⇒ 全链）。证据：`:4242-4265` 与 `:6821-6844` 两组 dump 逐字显示"只有选中组变 1"。
  - 真正的病症由此改判：**玩家只能用"单节改装"逐组刷，于是 8 个组可以各自 sub 不同 ⇒ 涂装/货种子分裂**（这正与 KI-458 的"涂装漂移"症状一致）。
  - 玩家之所以不用整列改装：`sel=24 nveh=255` 的整链列表**塌缩成 1 个选项**（`R3R-REFIT-OPT sel=24 ... options=1`），而单节 `sel=25/49` 是 `options=7`。
- **REG-290-A（已解）**：`nveh=255` = `:1168` 的 `num_vehicles == 0 ? UINT8_MAX : num_vehicles`，即"整列"哨兵；`sel=25/49` 时 `nveh=1` 是单节。不影响结论。

### 5.16.6 结论（本轮，可据此动手）

1. `var 72` 回落路线：**永久证伪**（CB72 的 `sub` 0→39 连续递增是直接证据）。
2. 列表塌缩为 1：**不是 GRF 限制**（单节 7 个），**不是节间 sub 不一致**（改装前全链一致仍是 1），**不是扫描面不全**（`R3R-REFIT-VEH` 102 行 = 4×24 + 2×3，全链都进了循环）⇒ 落在 `BuildRefitList()` 的多节交集实现本身（`vehicle_gui.cpp:1081-1109`）。
3. **~~新缺陷 KI-460：refit 只写了第一组~~ → 已撤回到 `KI-460【撤回：不是缺陷】`**：写入覆盖面 = 窗口 `num_vehicles`，两次实测都是 `nveh=1`（只改装选中那一组 3 节），行为正确。涂装分裂的真因是"整列改装不可用 ⇒ 只能逐组刷"（KI-461）。

**修法优先级（更新）**：KI-460 出局；当前唯一阻塞是 **KI-461（整列列表塌缩成 1 项）**。等 `R3R-REFIT-SUB` 数据出来再决定改法（见 §5.16.7）。

### 5.16.7 探针 `R3R-REFIT-SUB`（已实现+已编译，待玩家取数）

**为什么要它**：现在只知道"整列 1 项 / 单节 7 项"，但不知道**是哪一节**把表截断的。`BuildRefitList()` 的 intersect 分支在 `subtype == STR_EMPTY` 时按 `refit_cyc` 截断，`cyc==0` 直接把整表截成"保持当前"1 项 ⇒ 只要揪出第一个在 `cyc=0` 返回 `STR_EMPTY` 的单元即可定性。

**探针位置**：`src\vehicle_gui.cpp`，`BuildRefitList()` 内 `StringID subtype = GetCargoSubtypeText(v);` 之后（`first_vehicle` 分支之前），`cyc < 8` 时每单元每 cyc 一行：

```
R3R-REFIT-SUB idx=%d cargo=%u cyc=%u fv=%d sid=%u empty=%d sf=%d
```

- `fv` = 本次是否走 append（首单元）分支；`sid` = 原始 `StringID`；`empty` = 是否 `STR_EMPTY`；`sf` = `IsSegmentFront()`（区分段头，便于判断塌缩点是否总是"非段头"）。
- 开销：整列建表时 24 节 × ≤8 cyc = ≤192 行/次，且只在 `R3RDbgOn()` 且打开改装窗口时写；**与 `R3R-REFIT-VEH` 共用同一个 `FILE*`**（循环外开关一次，不再每节 fopen）。

**判据**：
- 若某 `idx` 首次出现 `empty=1 cyc=0` ⇒ 该节就是塌缩源头，接着查"这一节的 0x72 为什么在 subtype=0 时无后缀"（GRF 差异 / 假铰接组身份 / 段位置）。
- 若 24 节的 `sid` 序列**逐字相同**却仍 `options=1` ⇒ 改判 intersect 分支自身的逻辑缺陷（代码 bug），按 KI-461 修法 C（严格交集，不做 `cyc` 截断式清空）。

**构建自证**：仅改 `src\vehicle_gui.cpp`（未碰 `src\*.h`）⇒ 复用 `_tmp_inc_build.cmd`，guard=`incremental is safe`，`[3/3] Linking`，`EXIT_CODE=0`，`build\openttd.exe` @2026-10-24 14:11:13（52 307 456 B），`read_lints` 0，exe 字节级命中 `R3R-REFIT-SUB idx=`。首编译因 `HasBit(Train::From(v)->flags, ...)` 报 C2672，改 `Train::From(v)->IsSegmentFront()` 后通过（`Train::flags` 是 `EnumBitSet`，没有 `HasBit` 重载）。

**请玩家做什么**：`sel=24`（整列）**打开一次改装窗口**即可（不用点改装），然后把 `build\R3R_debug.log` 给我。

### 5.17 KI-461 根因做实（2026-10-24 第 2 次复测，`R3R-REFIT-SUB` 取数）

**日志**：`build\R3R_debug.log`（由 14:11:13 版 exe 产生）。整列建表 3 次（`:1607` / `:3217` / `:5857`），单节建表 1 次（`:4317`，只有 idx 24/25/26 三节 ⇒ 那次 `nveh=1`）。

**逐单元读数（`:1607-1660`，`cyc=0..7` 的 `sid`）**：

| 单元 | cyc0 | cyc1 | cyc2 | cyc3 | cyc4 | cyc5 | cyc6 | cyc7 | 有 `SUB` 行？ |
| --- | --- | --- | --- | --- | --- | --- | --- | --- | --- |
| idx 24/25/26（组 1） | 131442 | 131443 | 131444 | 131445 | 131446 | 131447 | 131442 | 131442 | 8 行 |
| idx 27..44（组 2~7） | — | — | — | — | — | — | — | — | **一行都没有** |
| idx 48/49/50（组 8） | **131436** | 131437 | 131438 | 131439 | 131440 | 131441 | 131436 | 131436 | 8 行 |

- `empty=0` 全为 0 —— **没有任何单元在 cyc 0 返回 `STR_EMPTY`** ⇒ §5.16.7 的"STR_EMPTY 截断"假设**不成立**。
- 组 2~7 连 `cyc=0` 都没有（`VEH` 行有、`SUB` 行无）⇒ 它们根本没进 `refit_cyc` 循环，即被循环外的前置门禁跳过（`callback_mask` 不含 CargoSuffix 的单元不参与交集，属 vanilla 设计）。
- **真正的差异：组 1 与组 8 对同一 `(cargo=0, subtype=0..5)` 返回不同的字符串块，且恰好相差 6**（131442..131447 vs 131436..131441）⇒ 组内一致、组间不一致。

**塌缩机制（与 `vehicle_gui.cpp:1081-1109` 交集分支逐行对得上）**：append 阶段（首单元 idx24）建出 `[keep(255)] + (0,131442) … (5,131447) + (6,131442) + (7,131442)`；随后组 8 的 idx48 走 intersect 分支，`pos` 命中同 `subtype` 但 `list[pos].string != subtype` ⇒ **逐条 `erase(pos)`**，8 条被删光 ⇒ 只剩"保持当前"1 项。`options=1` 完全解释。

**结论**：不是 GRF 不给选项（单节 7 项）、不是扫描面不全（24 节都有 `VEH`）、不是 `STR_EMPTY` 截断（`empty=0`），而是 **vanilla 交集按"字符串相等"判同；遇到"同一 subtype 在不同组映射到不同文案"的多组列车必然塌缩**。R3R 的假铰接组正好造出这种列车。

**修法（定为 B，最小改动）**：整列改装时**按 `(cargo, subtype)` 求交集、忽略字符串差异**，显示文案沿用列表首项（链头单元）的 `string`；`STR_EMPTY` 截断语义保留。写回端本来就把同一个 `subtype` 广播全链，文案差异不影响落地。

**疑点 REG-290-B（不阻塞修法，留档）**：为什么组 8 的字符串块比组 1 低 6？两组 `gh/gm`/`sf`/cargo/subtype 全同。候选 ①GRF 按车型或涂装选文案块（组 8 引擎型号与组 1 不同）；②GRF 读了 R3R 改过的段内位置变量——但本链只有 1 个 ★（`sf=1` 仅 idx24），全链位置 0..23 各不相同而组内 3 节读数一致 ⇒ **与位置无关，倾向 ①**。要收口需玩家给出该 GRF 名称或组 8 的引擎型号。

### 5.17.1 修法落地（2026-10-24，`src\vehicle_gui.cpp` `BuildRefitList()` 交集段）

改动只有一处，落在交集分支：

```cpp
if (pos < list.size() && list[pos].string != subtype) {
    if (v->IsArticGroupHead() || v->IsArticGroupMember()) {
        /* R3R 假铰接：同一 subtype 在不同组可能映射不同文案块，
         * 不因文案差异删项，沿用链头组的显示文案。 */
        if (r3r_refit_dbg != nullptr) fprintf(r3r_refit_dbg, "R3R-REFIT-SUB-KEEP idx=%d cargo=%u cyc=%u keep=%u got=%u\n", ...);
    } else {
        list.erase(list.begin() + pos);   // vanilla 严格行为，原生铰接/普通车不变
    }
}
```

- **为什么限定假铰接**：原生真铰接车的 `GetCargoSubtypeText()` 全组一致，不会走到这个分支；不改普通分支可保证原生车逐字不变（复测判据 ⑤）。
- **`STR_EMPTY` 截断**原样保留在分支之前，未动。
- **写回端无需改**：`RefitVehicle()` 本来就把选中的 `subtype` 广播给 `GetVehicleSet()` 的全部单元，文案不参与写入。

**新增探针**：`R3R-REFIT-SUB-KEEP idx=%d cargo=%u cyc=%u keep=%u got=%u`（每次因文案差异而保留时一行）。判据：整列建表时应对 `idx=48/49/50` 各出现若干行（被保留的次数）。

**构建自证（2026-10-24 19:18）**：仅改 `src\vehicle_gui.cpp`（未碰 `src\*.h`）⇒ 复用 `_tmp_inc_build.cmd`（未新建 `.cmd`）；guard=`GUARD: incremental is safe`；日志 `[2/3] vehicle_gui.cpp.obj` → `[3/3] Linking CXX executable openttd.exe`；`build\R3R_incbuild.done` = `EXIT_CODE=0`（19:18:49）；`error C` / `fatal error` / `FAILED:` 计数 0；`src\vehicle_gui.cpp` 19:16:02 → `build\openttd.exe` 19:18:46（52 307 456 B）；`read_lints` 0；`findstr` 命中 `R3R-REFIT-SUB-KEEP` / `R3R-REFIT-SUB idx=` / `R3R-REFIT-OPT`。

### 5.17.2 待玩家复测（2026-10-24 要求）

`sel=24`（整列）打开改装窗口，**并实际点一次整列改装**，回传 `build\R3R_debug.log`。判据：
1. `R3R-REFIT-OPT sel=24 ... options=7`（>1 即算过）；
2. 出现 `R3R-REFIT-SUB-KEEP idx=48/49/50 ...`；
3. 改装后 `R3R-REFIT-UNIT` 的 24 行是**同一个 `subtype`**（整列广播）；
4. 外观上 8 个组涂装一致（KI-458 现象消失）；
5. 负判据：原生真铰接车的改装项数与修复前逐字不变。

