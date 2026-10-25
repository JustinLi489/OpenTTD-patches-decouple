# R3R 第 291 轮：涂装 2 动车 + 涂装 3 动车重联后，涂装 2 的中间车变成涂装 3（livery override 串味）

日期：2026-10-24
状态：取证中（本文按"先落盘再取证"铁律先写骨架，随取证回填）

## 一、玩家主诉（原话）

> 见 R3R_debug.log，现在是能够支持整列改装了，但是我的问题还没解决，我主诉，涂装 2 的动车和涂装 3 的动车重连（均为非默认涂装，2 主动挂 3），动车中间车由 livery override 改变涂装，那么涂装 2 的车中间车在重联之后变成了涂装 3

拆成可验证的三条：
1. **前置已达成**：整列改装可用了（KI-461 修法生效，见 §二 读数）。
2. **现象**：A 编组（涂装 2，主动挂）的**中间车**（非链头车厢）在 B 编组（涂装 3）被挂上之后，显示成涂装 3。
3. **机制声明**（玩家给的）：中间车的涂装不是自身引擎的涂装，而是由 **livery override**（跟随"前面的某节车"取涂装）决定的。

## 二、本轮日志已确认（KI-461 收口）

`build\R3R_debug.log` 1 417 352 B / 26 203 行 / 2026-10-24 19:36:36（由 19:18 版本 exe 产生）。

- `R3R-REFIT-OPT cargo=0 options=7` 共 **15 行**（`:1956 :3584 :4387 :6015 :7881 :8700 :10565 :11396 :13267 :14084 :16042 :19564 :21192 :23052 :24680`）⇒ **整列改装列表已恢复 7 项，KI-461 修法 B 实测通过**。
- `COUPLE-OK loco=21 rear=50 consist=24 co=0 real=1 type=1 tx=1889 ty=188 x=30234 y=3016`（`:16680`、`:25313`）⇒ 两次挂车都是**直拼**（全日志 0 条 `COUPLE-FLIP-*` / `COUPLE-SEAM-FLIP` / `SPLICE-GAP-REJECT`），合并链链头 = 21。

## 三、猜测根因（待验证，按铁律先落盘）

**G-1（主推）**：vanilla 的 livery override 是"车厢向**物理链头**（`v->First()`）的那个引擎要涂装"。R3R 的假铰接组会把中间车与"组头"挂钩，重联后若中间车在**新链上的 `First()` 落到了 B 编组的引擎**（或 `First()` 走到的是 B 编组的假引擎/段头），就会串成 B 的涂装。
- 支持：玩家观察到"只有中间车变、链头不变"＝典型 `First()` 语义受害面。
- 反证点：本次日志 `COUPLE-OK loco=21`，链头仍是 A 的机车 ⇒ 需要确认 livery override 取的到底是不是 `First()`，还是"前一个真引擎 / 前一个段头 / `Previous()` 方向最近的引擎"。

**G-2**：livery override 按"**consist 内第一个引擎**"或"**段头（★）**"取，而 R3R 在合并后重排了段边界/段序（即使物理链序单调），使 A 的中间车前面最近的"引擎/段头"变成 B 的。
- 支持：R3R 的 `IsSegmentFront()`/`ArticGroupHead` 是自造语义，vanilla 不认识。
- 待验证：合并后 A 的中间车 `Previous()` 方向的第一个 `IsSegmentFront()` 是谁。

**G-3**：不是"取错引擎"，而是 **`v->First()` 本身被 R3R 改了**——R3R 的"假铰接"让 `First()`（走 `Previous()` 到链头）与"段头"不一致，而 `GetCargoSubtypeText`/livery 相关缓存（`Engine::livery_override`? `NCVV` 缓存）在合并后没被 invalidate，残留了 B 的值。
- 待验证：合并提交点是否调了 `InvalidateNewGRFCacheOfChain()`。

## 四、待取证（本轮配额内做）

- [ ] 定位 vanilla/JGRPP 里 livery override 的实现与"参考车"的取法（哪个函数、走 `First()`/`Previous()`/`IsFrontEngine` 哪条）。
- [ ] 取合并后 A 编组中间车的链上邻接关系（用现成 `GEO settle-couple` / `CHAIN-ATTRS` / `R3R-REFIT-UNIT` 探针）。
- [ ] 判断 B 编组的引擎号在合并链里的位置（是否跑到 A 中间车的前方）。
- [ ] 复测判据与修法草案。

## 五、复测判据（初稿，待取证后细化）

1. 涂装 2 编组单独存在（未重联）时中间车显示涂装 2。
2. 重联（2 主动挂 3）后，A 的中间车仍显示涂装 2；B 的中间车仍显示涂装 3。
3. 解挂后两侧涂装各自复原，无残留。
4. 负判据：原生（无 R3R 段/假铰接）多编组重联行为逐字不变。

## 六、根因（已收口，2026-10-24 本轮取证结论）

### 6.1 G-1/G-2/G-3 全部被证伪或降级

- **G-1（first_engine 指向耦合伙伴）不成立**：`SEGFE-VEH head=21 veh=3..20 eng=622 first=623 ovr=1`（`build\R3R_debug.log:16751-16789`）。`first_engine` 的类型是 `EngineID`，A 的中间车与 B 的中间车都是 `eng=622 / first=623`（B 侧同一读数见 `:16470-16508`）⇒ 两侧 wagon override sprite set 与父引擎**逐字相同**，wagon override 这条链**无法**产生"A 变 B"的差异。
- **G-3（缓存未失效）不成立**：若只是缓存残留，A 的中间车会保留**自己的**旧涂装，恰好不会变成 B 的涂装。
- **G-2 降级为不相关**：段头/`First()` 语义只能影响"取哪一份 livery"，而 livery 的实际来源由下一条决定。

### 6.2 涂装真正的来源 = **分组涂装（group livery）**

`src\vehicle.cpp:3310-3319`（`GetEngineLivery`）：

```cpp
if (v != nullptr && !ignore_group) {
    const Group *g = Group::GetIfValid(v->First()->group_id);
    ...
    if (g->livery.in_use.Any({Livery::Flag::Primary, Livery::Flag::Secondary})) return &g->livery;
}
```
即：**只要列车所在分组设了涂装，就直接返回该分组的涂装，整条链所有车（含中间车）共用同一份**，且用的是 `v->First()->group_id`——**全链唯一**。

证据：`LIVERY-SCHEME` 探针全日志 **0 行**（该探针只在公司 Default livery 的 `in_use` 分支里打，见 `vehicle.cpp:3323-3326`）⇒ 玩家的两套涂装走的是分组涂装分支，而不是公司默认涂装 + scheme 替换。

### 6.3 分组被改写为"被动方（B）"的分组

`build\R3R_debug.log`：

```
:16961 SEGTRAIT-RECOVER seg=24 unit=2 group=65534 name=1
:16962 SEGTRAIT-SYNC tag=couple head=21 seg=2 unit=2 group=3 borrowed=0 ctrl=24
:16963 INVAR-CTRL tag=couple head=21 nseg=2 ctrl=24 ctrl_pri=1 head_pri=2 borrowed=1
```

（`:25593-25595` 为同一形态的第二次挂车）

- `ctrl=24`、`ctrl_pri=1 < head_pri=2` ⇒ 控制段（排程归属段）= **被挂方 B（车 24）**；
- `group=3` ⇒ 合并后**整条链的活分组 = 3 = B 的分组**；
- `SEGTRAIT-RECOVER seg=24 group=65534`（= `DEFAULT_GROUP`）是被并入段**停放**的旧值，不是它的活分组，容易被误读。

写入路径是第 159 轮 q-2=肆 拍板的"分组属于整列车"口径：`Couple()` 提交点 `GroupStatistics::CountVehicle(v,-1) → R3RNormaliseChainGroups(v) → CountVehicle(v,+1)`（`src\train_cmd.cpp:12825-12827`），以及借用期 `R3RBorrowControlTraitsLive()` 把控制段那一套（含 `group_id`）穿到链头活字段（`R3RSyncSegmentTraits` 的 `traits = chain` 路径将 `group=3` 镜像进段行）。

### 6.4 根因一句话

> 玩家的"涂装 2/3"是 **JGRPP 分组涂装**（per-group company livery）。`GetEngineLivery()` 用 `v->First()->group_id` 取涂装，**整条链只得一份**；而 R3R 在重联提交点按"排程归属段（控制段）"归一化整链 `group_id`，本现场控制段 = 被动方 B（`ctrl_pri=1 < head_pri=2`）⇒ 整链改成 B 的分组 ⇒ A 编组（含其中间车）统一取 B 的分组涂装 = 涂装 3。

"只有中间车变、链头没变"是**观察面**而非机制差异：中间车靠 wagon override 借用 623 端车的贴图、调色板仍来自 `GetEngineLivery(622, owner, first_engine=623, v)`，与链头同吃一份分组涂装。

### 6.5 修法候选

- **候选 α（推荐，单点、对症）**：把"涂装"与"分组归属"解耦——`GetEngineLivery()` 的 group 查找由 `v->First()->group_id` 改为**本车所属段的分组**（复用 `couple_group.h` 的 `R3RSegmentGroupID(v)`）。统计/车队列表仍按控制段统一（第 159 轮口径不变），涂装按段保留 ⇒ 重联后 A 保持涂装 2、B 保持涂装 3。
  - 风险：`vehicle.cpp` 需 include `couple_group.h`；纯车厢链（无段）需回退 `v->First()->group_id`。
- **候选 β**：重联不再统一 `group_id`（回到"分组属于段"）。与第 159 轮拍板冲突，且 GroupStatistics/列表会散开，不推荐。
- **候选 γ**：整链统一到**链头段**的分组。只是把串味方向反过来（A 吞掉 B），不解决问题。

### 6.6 待验证 / 疑点（按铁律登记）

- **U-1**：A 与 B 各自的分组 id 与"是否设了涂装"未直接读数（日志无 group livery dump）。若玩家两套涂装其实挂在**同一分组的两个 livery index** 上，则本结论需重审。
- **U-2**：`GRP-NORM` 探针 0 行（`R3RNormaliseChainGroups` 内有该日志，`train_cmd.cpp:7840`）⇒ 本现场 `group_id` 的改写更可能发生在借用（`R3RBorrowControlTraitsLive`）而不是归一化，两者都是同一口径，不影响根因结论，但改写点仍待定位。
- **U-3**：玩家称"链头不变"——按本结论链头也应变成涂装 3；需玩家确认链头是否真的没变（若真没变，说明端车另有独立贴图，不影响中间车的结论）。

## 七、"让 livery override 仅对各自的段生效"可行性评估（玩家 2026-10-24 提案）

### 7.1 结论

**方向正确、可以这样做，但必须分清三条独立的链**，其中只有第 3 条是本次现场的真凶：

| 链 | 现实现 | 键 | 是否已按段 | 是否需改 |
|---|---|---|---|---|
| ① wagon sprite override | `u->tcache.cached_override = GetWagonOverrideSpriteSet(engine, cargo, u->gcache.first_engine)`（`train_cmd.cpp:450`） | `gcache.first_engine` | **已是段内**（见 7.2） | 仅在两段引擎型号不同时才需改 |
| ② 2CC / 分组涂装 | `GetEngineLivery()` 的 `Group::GetIfValid(v->First()->group_id)`（`vehicle.cpp:3311`） | `v->First()` → **整链唯一** | **不是** | **必须改**（本次真凶） |
| ③ livery scheme 分类 | `GetEngineLiveryScheme(engine_type, parent_engine_type, v)`（`vehicle.cpp:3325`，`parent_engine_type` 来自 `newgrf_engine.cpp:406` 的 `first_engine`） | `first_engine` | 已是段内 | 不用改 |

### 7.2 关键反证：wagon override 现在就已经是"段内"的

`Train::ConsistChanged()`（`train_cmd.cpp:377-450`）的递推是：

```cpp
EngineID first_engine = this->IsFrontEngine() ? this->engine_type : EngineID::Invalid();
...
u->gcache.first_engine = this == u ? EngineID::Invalid() : first_engine;
if (u->IsEngine()) first_engine = u->engine_type;   // 沿 Next() 递推"最近的前置引擎"
```

现场读数 `SEGFE-VEH head=21 veh=3..20 eng=622 first=623 ovr=1` 说明 A 编组中间车的 `first_engine` 就是 **623（A 自己段头的引擎）**，合并后也没有变成 B 段的东西（B 段头同样是 623，型号相同）。⇒ **wagon override 这条链在这次现场从未串味**，改它不会改变你看到的现象。

真正会串的是 ②：`v->First()` 在合并后对所有车都是 21，`group_id` 又被归一化成 B 的分组，于是**全链共用 B 的一份分组涂装**（另见 §六）。

### 7.3 若要"按段"落地，具体清单

- **(a) 分组涂装按段（治本，单点）**
  `vehicle.cpp:3311` 的 `v->First()->group_id` → `R3RSegmentGroupID(v)`（`couple_group.h`，已有；无段/无行时自动回退 `v->First()`）。
- **(b) 配色缓存按段失效**
  `GetEngineColourMap` 走 `v->colourmap` 缓存（`vehicle.cpp:3335/3371`），必须确认 `ConsistChanged()` 对**每一节**都 `colourmap = PAL_NONE`（不能只清链头），否则段 A 的车会留着段 B 的 palette。
- **(c) wagon override 也按段（防御性，两段引擎型号不同时才可见）**
  **不能直接改 `gcache.first_engine`**：`train.h:495` / `train.h:585` 的 `RailVehInfo(this->gcache.first_engine)` 无条件解引用，段头若拿到 `Invalid` 会崩；且 `CBID_TRAIN_ALLOW_WAGON_ATTACH`（`train_cmd.cpp:3388-3398`）会临时清空它。
  ⇒ 新增独立字段 `GroundVehicleCache::segment_first_engine`（`ground_vehicle.hpp:45` 旁），在 `ConsistChanged()` 里与 `first_engine` 并行递推、**遇 `u->IsSegmentFront()` 复位为段头自身的 `engine_type`**，然后只把这一个字段喂给 `cached_override`。
- **(d) 待确认**：段头是假引擎时它的 `engine_type` 是否等于该段首节真引擎的型号（若是，按段取恰好等于"本段引擎"，语义自洽；若不是，需要额外记录段内首个真引擎）。

### 7.4 风险与负判据

- 单段链 / 原生无段链：`R3RSegmentGroupID` 回退 `First()`、`segment_first_engine` 与 `first_engine` 逐字相同 ⇒ **行为必须逐字不变**（这是主负判据）。
- 统计/车队列表仍按控制段统一（第 159 轮口径不动，见 §六.5 候选 α 的边界说明）。
- `colourmap` 若改成按段失效，需覆盖 `ConsistChanged` 的全部入口（couple/decouple/depot-edit/读档）。

## 八、玩家假设 H-462-B：B 的机车通过 wagon override 把「涂装 3 的 cargo subtype」覆盖到 A 的车厢（登记于 2026-10-24，待验证）

### 8.1 玩家原话要点（口径修正）

- **A 车 = 涂装 2、B 车 = 涂装 3；是 B 主动挂 A** ⇒ **链头是 B 的机车**（不是我上一轮假设的 A 主动）。
- 动车组的惯例：机车 + 车厢搭配 **livery override**（可覆盖车厢的很多属性）。
- 玩家推断：B 的机车也能把 A 的车厢 override 过去 ⇒ **B 链头的涂装 3 cargo subtype 被盖到 A 的车厢上**。

### 8.2 与既有读数的关系（关键）

若 B（623）主动挂 A，则 `SEGFE-VEH head=21 veh=3..20 eng=622 first=623`（`:16751-16789`）恰是**A 的车厢把 B 的机车当成 `first_engine`**——这与玩家假设**完全吻合**，不构成反证（我上一轮把它读成"A 侧 first 是自己的 623"是错的：623 是 B 的链头引擎，不是 A 的端车）。

⇒ 因此上一轮 §六.1 的"G-1 被证伪"**需撤回**：`first_engine` 指向耦合伙伴这件事在"B 主动挂 A"口径下是**事实**，只是当两侧引擎型号同为 623 时无法从 `first` 数值上区分来源。

### 8.3 待验证的技术分岔（H-462-B 的两条支路）

- **B-1（wagon override spriteset 支路）**：`GetWagonOverrideSpriteSet(engine, cargo, first_engine)` 的匹配键里是否含 **`cargo_subtype`**。若含，则 `cached_override` 会因 B 的 subtype 而选到涂装 3 的整组图 ⇒ 假设成立。
- **B-2（subtype 传播支路）**：`Vehicle::cargo_subtype` 是否在重联/`ConsistChanged`/`NormaliseSubtypes` 中被统一成链头（B）的值。若被统一，则 A 的车厢在 GRF 侧读到的 subtype 就是 3 ⇒ 无论 GRF 怎么实现都会画成涂装 3 ⇒ 假设成立且根因在 R3R 侧。
- **B-3（属性覆盖支路）**：`GetVehicleProperty()` 的 wagon-override 属性借用（powered wagon 同族）是否也带 subtype 语义。若只是数值属性则与涂装无关。

### 8.4 判定优先级

- 若 **B-2 成立** ⇒ 真凶是 R3R 重联把 A 的 `cargo_subtype` 改了，修法在重联提交点（不要把 subtype 归一化，按段保留）。
- 若只有 **B-1 成立**（subtype 未变，但 wagon override 匹配了 B 的 subtype）⇒ 修法应按段切 `first_engine`（即 §七.3(c) 的 `segment_first_engine`），使 A 车厢的 override 父引擎回落到 A 段头。
- 若 **B-3 单独成立** ⇒ 与涂装无关，归入 KI-458 族。

### 8.5 状态

- 原判"待验证（猜测根因）"已于本节 8.6 取证完毕。

### 8.6 取证结论（2026-10-24 本轮）

**(1) wagon override 的匹配键不含 `cargo_subtype`。**
`src\newgrf_engine.cpp:44-65`：

```44:65:src/newgrf_engine.cpp
void SetWagonOverrideSprites(EngineID engine, CargoType cargo, const SpriteGroup *group, std::span<EngineID> engine_ids)
{
	WagonOverride *wo = &e->overrides.emplace_back();
	wo->group = group;
	wo->cargo = cargo;                       // 只有 cargo
	wo->engines.assign(engine_ids.begin(), engine_ids.end());
}
const SpriteGroup *GetWagonOverrideSpriteSet(EngineID engine, CargoType cargo, EngineID overriding_engine)
{
	for (const WagonOverride &wo : e->overrides) {
		if (wo.cargo != cargo && wo.cargo != CargoGRFFileProps::SG_DEFAULT) continue;
		if (std::ranges::find(wo.engines, overriding_engine) != wo.engines.end()) return wo.group;
	}
}
```

⇒ 匹配键 = `(cargo_type, overriding_engine=first_engine)`，**没有 subtype** ⇒ 支路 B-1（按 subtype 选 wagon override 组）**不成立**。

**(2) `cargo_subtype` 现场读数：探针无法给出结论，但也不支持"B 把 3 传给 A"。**
`R3R-CB72`（`newgrf_engine.cpp:1176-1199`）在本日志里绝大多数行是 refit 窗口的枚举（同一 idx 的 sub 从 0 连续刷到 30+），属 `RefitWindow::BuildRefitList()` 的临时改写，不能当实际值；早期真实行（`:219/:222/:226/:228/:231/:235`）对应 idx=21/22/24/25/46/49 **全部 `sub=0`**。
⇒ 支路 B-2 既未被证实，也没有任何读数显示"某车 subtype=2 或 3"。**涂装的载体不是 cargo subtype**。

**(3) 真正的泄露点在 `var 0x43`（Company information）+ `Livery`，且它按 `v->First()` 取。**

```723:728:src/newgrf_engine.cpp
case 0x43: // Company information
	if (!HasBit(v->grf_cache.cache_valid, NCVV_COMPANY_INFORMATION)) {
		v->grf_cache.company_information = GetCompanyInfo(v->owner, LiveryHelper(v->engine_type, v));
```

```398:406:src/newgrf_engine.cpp
static const Livery *LiveryHelper(EngineID engine, const Vehicle *v)
{
	} else if (v->IsGroundVehicle()) {
		l = GetEngineLivery(v->engine_type, v->owner, v->GetGroundVehicleCache()->first_engine, v, LIT_ALL);
```

而 `GetEngineLivery` 内部取分组涂装用 `v->First()->group_id`（`vehicle.cpp:3311`）。**⇒ A 的车厢在 self scope 下读 0x43，也已经拿到链头 B 的分组涂装。**

**(4) 第二条同源泄露：GRF 的 `parent` 作用域就是链头。**

```1389:1391:src/newgrf_engine.cpp
	self_scope(*this, engine_type, v, rotor_in_gui),
	parent_scope(*this, engine_type, ((v != nullptr) ? v->First() : v), rotor_in_gui),
	relative_scope(*this, engine_type, v, rotor_in_gui),
```

`VehicleScopeResolver` 的所有变量（0x42 consist cargo info、0x43 company info、0x46 motion counter、0x47 cargo info、0x72 cargo subtype…）都是对着 scope 里的 `v` 求值 ⇒ **GRF 在 A 车厢的 spriteset 里请求 parent 变量，读到的是链头 B 的整车属性**。这就是玩家直觉里"B 的车头把涂装 3 的属性盖到 A 的车厢上"的**确切代码位置**。

**(5) 结论（修正 §六）**

> 玩家假设的**方向成立、载体需要修正**：真正被"链头覆盖"的不是 cargo subtype，而是 **Livery（涂装）本身**——它经 `var 0x43` 与 `parent_scope = v->First()` 两条路同时泄露到 A 的车厢，GRF 随后按这个涂装值选用涂装 3 的 spriteset（即玩家说的 livery override）。`wagon override` 的 spriteset 匹配本不看 subtype，所以"subtype 被覆盖"这一层不必成立也能产生现象。

⇒ **§六.1 的"G-1（first_engine 指向耦合伙伴）被证伪"予以撤回**：在"B 主动挂 A"口径下 `A 车厢 first=623 = B 的机车`是正常且必然的事实（B 就是链头），只是当两侧机车同为 623 时数值上看不出差异。

### 8.7 由此确定的落地口径（对应 §七 的 (a)(c) 之外新增 (e)）

- **(a) 必需**：`GetEngineLivery` 的分组查找按段（`R3RSegmentGroupID(v)`）——直接切断 `0x43` 的串味。
- **(e) 可选但更彻底**：`parent_scope` 初值由 `v->First()` 改为**本段段头**（无段回退 `First()`）——让所有 GRF parent 变量（0x42/0x43/0x46/0x47/0x72…）都按段，语义与 R3R 已有的"0x40/0x41 position-in-consist 按段计数"（记忆 96042581）一致。
  - 风险：影响面比 (a) 大（所有读 parent 的 GRF），单段链/原生链必须逐字不变；建议先单独做 (a) 复测，再决定 (e)。

## 九、(a) 落地记录与 (e) 待办（2026-10-24 玩家拍板「先做 a，后面按需做 e」）

### 9.1 改动（仅 `src\vehicle.cpp`，未碰任何 .h ⇒ 增量构建合法）

```cpp
/* 新增于 GetEngineLivery() 之前 */
static GroupID R3RLiveryGroupOf(const Vehicle *v)
{
	if (v == nullptr) return GroupID::Invalid();

	const Vehicle *const chain_head = v->First();
	if (v->r3r_segment_id == chain_head->r3r_segment_id) return chain_head->group_id;

	return R3RSegmentSectionGroupID(v);
}
```

- 调用点：`vehicle.cpp` 原 `:3311` 的 `Group::GetIfValid(v->First()->group_id)` → `Group::GetIfValid(R3RLiveryGroupOf(v))`；新增 `#include "couple_group.h"`。

### 9.2 两个设计取舍（都是踩过的坑）

1. **不用 `R3RSegmentGroupID(v)`**：它对"本段行里 group 为 Invalid"的段回退 `First()->group_id`（`couple_group.cpp` 的实现），等于把同一 bug 留了个空洞 —— 一个**没设分组的段**仍会继承控制段涂装。`R3RSegmentSectionGroupID(v)`（`couple_group.cpp:1193-1200`）的回退目标是"本段段头自己的活字段"，结构上不可能回到链头。
2. **守卫用 `r3r_segment_id` 而不是扫链数段**：`vehicle_base.h:386` 约定单段链的该字段恒为 `R3R_SEGMENT_NONE`（全车相同），原生非 R3R 列车同理 ⇒ 守卫天然保证"单段/原生链走旧路径、逐字不变"，且是 O(1)。`GetEngineLivery()` 在渲染热路径上，不能按链扫段数。

### 9.3 顺带覆盖到的东西（正面副作用）

- `var 0x43`（Company information）走 `LiveryHelper()` → `GetEngineLivery()`，所以 GRF 在 A 车厢的 spriteset 里读 `0x43` 也拿到了 A 段的涂装 —— 等价于玩家说的"livery override 只对各自的段生效"。
- `GetEngineColourMap()` 的 `v->colourmap` 缓存是逐车字段，`Train::ConsistChanged()` 在逐车循环里 `u->colourmap = PAL_NONE`（`train_cmd.cpp:477`）⇒ 重联/解挂后逐节失效，不需要额外改缓存失效。

### 9.4 构建自证

- 复用 `_tmp_inc_build.cmd`（未新建 .cmd）；`GUARD: incremental is safe`（只重编 1 个 .cpp）。
- `[2/3] Building CXX object CMakeFiles\openttd_lib.dir\src\vehicle.cpp.obj` → `[3/3] Linking CXX executable openttd.exe`。
- `build\R3R_incbuild.done = EXIT_CODE=0`。
- `src\vehicle.cpp` @2026-10-24 20:05:56 → `vehicle.cpp.obj` @20:06:39 → `build\openttd.exe` @20:08:08（52 307 456 B）。
- `read_lints` 0 条。

### 9.5 复测判据（玩家侧）

1. 未重联时：A 中间车 = 涂装 2，B = 涂装 3（基线不变）。
2. B 主动挂 A 后：**A 的中间车仍是涂装 2**，B 段仍涂装 3；链头（B 的机车）保持涂装 3。
3. 解挂后两段各自复原，无残留。
4. 负判据：普通（无段 / 单段）列车套分组涂装时，行为与改前逐字一致。

### 9.6 (e) 未做，保留待办

`parent_scope`（`newgrf_engine.cpp:1390` 的 `v->First()`）仍指向整链头，因此 GRF 若在车厢 spriteset 里读 **parent** 作用域的 `0x42/0x43/0x46/0x47/0x72`，读到的仍是 B 的整车属性。玩家已拍板"后面按需做"：

- 触发条件：第 1 条复测后若仍有"部分车厢残留别段涂装/图像"，即说明该 GRF 走的是 parent 作用域而非 self 作用域，此时再做 (e)。
- 做法：`parent_scope` 的 vehicle 由 `v->First()` 改为"本段段头（无段回退 `First()`）"，与 R3R 已有的"`0x40/0x41` position-in-consist 按段计数"同族。
- 风险：影响所有读 parent 变量的 GRF ⇒ 必须验"单段链/原生链逐字不变"。

## 十、(a) 复测未通过 ⇒ 启动修法 (e)（2026-10-24 玩家原话「好吧，车厢的涂装还是被链头覆盖了，我们只好启动修法e了」）

### 10.1 判定

- (a) 已落地并编译（`R3RLiveryGroupOf()`，见 §九），玩家复测**中间车仍被链头涂装覆盖**。
- ⇒ `self` 作用域的 `var 0x43`（`LiveryHelper` → `GetEngineLivery`）**不是**唯一泄漏点，甚至不是实际生效的那条；GRF 走的是 `parent` 作用域（`newgrf_engine.cpp:1390` 的 `v->First()`）。
- 与 §8.6(4) 的取证一致：`parent_scope` 的 v 恒为链头，所有 parent 变量（0x42/0x43/0x46/0x47/0x72）在 A 车厢里都读成 B 的整车属性。

### 10.2 (e) 口径

- `parent_scope` 的 vehicle 由 `v->First()` 改为**本段段头**（无段/单段回退 `v->First()`）。
- 复用 `(a)` 的守卫口径：单段链的 `r3r_segment_id` 恒为 `R3R_SEGMENT_NONE`（全车相同）⇒ 单段/原生链逐字不变。
- 实现放 `couple_group.cpp`（新访问器），`newgrf_engine.cpp` 侧手写前向声明以保持 .cpp-only 改动（避免改 `src/*.h` 触发全量重编）。

### 10.3 改动落点（仅三个 .cpp，未碰任何 .h ⇒ 增量合法）

1. `src\couple_group.cpp`（约 :1222，插在 `R3RSegmentStoreTraits()` 之前）新增公开访问器：

```cpp
const Vehicle *R3RSegmentHeadOf(const Vehicle *v)
{
	if (v == nullptr) return nullptr;
	if (v->type != VehicleType::Train) return v->First();
	return R3RGetCoupleGroupCarrier(Train::From(v));
}
```

   复用文件内既有模板 `R3RGetCoupleGroupCarrier()`：沿 `Previous()` 回溯找最近的 `IsSegmentFront()`（★），找不到则回退 `v->First()`；nullptr 安全。

2. `src\newgrf_engine.cpp`（:1373 手写声明、:1395 调用）：

```cpp
	parent_scope(*this, engine_type, ((v != nullptr) ? R3RSegmentHeadOf(v) : v), rotor_in_gui),
```

3. `src\vehicle.cpp`（:3313 手写声明，(a) 的 `R3RLiveryGroupOf()` 补 `group_id_backup` 优先）：

```cpp
static GroupID R3RLiveryGroupOf(const Vehicle *v)
{
	if (v == nullptr) return GroupID::Invalid();
	const Vehicle *const chain_head = v->First();
	if (v->r3r_segment_id == chain_head->r3r_segment_id) return chain_head->group_id;
	const Vehicle *const seg = R3RSegmentHeadOf(v);
	if (seg != nullptr && seg->group_id_backup != GroupID::Invalid()) return seg->group_id_backup;
	return R3RSegmentSectionGroupID(v);
}
```

### 10.4 关键坑（(a) 首版为何无效）

`R3RNormaliseChainGroups()`（第 159 轮决策「分组属于整列车」）在重联提交点会把**整链**分组归一化到控制段分组，并把各段头原有分组**停放到 `Vehicle::group_id_backup`**。因此对重联链：

- 段头活字段 `group_id`、段特质行（`R3RSegmentStoreTraits` 的镜像，`R3RSegmentSectionGroupID` 读它）**都已经是控制段分组**；
- 只有 `group_id_backup` 还留着该段原本的车组身份。

⇒ 首版 `R3RLiveryGroupOf()` 退回 `R3RSegmentSectionGroupID(v)` 时仍读回伙伴涂装，这就是"改了却没用"的第二重原因（第一重是作用域选错）。现版本改为**先读 `seg->group_id_backup`**，行只作"从没有过自己分组"的兜底（此时正确语义=无分组 → 公司涂装）。

### 10.5 构建自证（第 291 轮 · 修法 e）

- 复用既有 `_tmp_inc_build.cmd`（未新建任何 .cmd）；构建前 `tasklist` 确认 `openttd.exe` 未运行。
- `build\R3R_incbuild.guard.log` = `GUARD: incremental is safe (no header/lang file is newer than the newest object)`。
- `build\R3R_incbuild.log`：`[2/5] couple_group.cpp.obj`、`[3/5] newgrf_engine.cpp.obj`、`[4/5] vehicle.cpp.obj`、`[5/5] Linking CXX executable openttd.exe`。
- `build\R3R_incbuild.done` = `EXIT_CODE=0`（20:15:49）；错误计数（`error C`/`fatal error`/`FAILED:`/`build stopped`）= 0。
- 时间戳：`vehicle.cpp` 20:12:35 → obj 20:14:15；`couple_group.cpp` 20:12:09 → obj 20:14:08；`newgrf_engine.cpp` 20:12:16 → obj 20:14:08；`build\openttd.exe` **20:15:46（52 307 456 B）**，晚于全部源码与 obj。
- `read_lints` 三个文件 = 0 条。
- 本轮未新增日志字面量，故不做 exe 字符串核对。

### 10.6 复测判据（4 条）

1. **A 中间车**：A 车组（涂装 1）与 B 车组（涂装 2）重联后，链中 B 段各节仍显示涂装 2，A 段各节显示涂装 1（不再被链头统一）。
2. **B 段**：解挂后两段各自涂装与本段车组一致，不回退、不串味。
3. **单段/原生链逐字不变**：单段链（全部车 `r3r_segment_id == R3R_SEGMENT_NONE`）与原生 JGRPP 铰接/多节车图像完全不变。
4. **(a) 正向不退化**：`var 0x43`（self 作用域）路径的涂装仍按段读取。

### 10.7 仍未做 / 未确认

- 未确认该 GRF 到底读 `0x42` 还是 `0x43`/`0x46`（当前做法覆盖全部 parent 变量，故无需逐一定位）。
- `relative_scope` 仍是 `v`（相对作用域语义本就以本车为基准），未动。
- 现车 `colourmap` 缓存：`ConsistChanged()` 逐车重置 `PAL_NONE`，重联/解挂后会自动重算，无需额外失效。
