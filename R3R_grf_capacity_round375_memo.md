# GRF 载入后运载量变成 1/3：取证备忘（第 375 轮）

## 1. 玩家原话（2026-10-30）

> 「算了，这个问题暂时搁置。我发现，我们的grf载入又出现了一些问题。在载入我们大名鼎鼎的中国火车包的时候，它的运载量变成了原先的三分之一，我想知道这又是为什么」

前一条"共享排程 × 自动填充"（KI-539）按玩家指示**搁置**，状态已改为「已搁置」。

## 2. 现象

- GRF：中国火车包（chinaset 系，玩家称"大名鼎鼎"）
- 症状：载入后**运载量 = 原先的三分之一**
- 关键未知（决定后续方向，见 §5）：
  - 是"某一节显示 1/3"还是"整列总容量 1/3"；
  - 是"造出来就 1/3"还是"做过 R3R 升段/耦合/解挂之后才 1/3"；
  - 新开档还是旧档；是否换过 exe。

## 3. 猜测根因（待验证；按铁律先落盘再取证）

- **G-1（首选）**：`PositionHelper`（NewGRF 服务变量 `0x40 position in consist` / `0x41 length of same id`）被 R3R 改成"段内计数"（既有改动，见记忆 96042581，落点 `src\newgrf_engine.cpp`）。中国火车包若按 position 选容量/车型（铰接车常见做法：父车 + 2 节，按位置 0/1/2 取不同 capacity），段内计数把读数从 3 段变 1 段 ⇒ 容量按 1/3 取值。
- **G-2**：打散铰接（`DearticulateChainWithSnapshot` + 烘焙 override，见第 170/225 轮）把 3 节铰接单元拆成 3 个独立车；原包容量挂在父子 articulation 链上，拆开后容量重算 ⇒ 1/3。
- **G-3**：假引擎（`SetEngine` / `GVSF_FRONT`）使某节被当作引擎，容量计算走 engine 分支。
- **G-4**：与 R3R 无关——GRF 本身版本/参数（capacity 参数、refit 表）或 JGRPP 侧 `capacity` 读取变化。

## 4. 待验证判据

- D1：容量读取路径是否被 R3R 触碰 —— `Engine::GetCapacity` / `Vehicle::cargo_capacity` / NewGRF callback `0x15`（cargo capacity）/ 服务变量 `0x40`、`0x41`。
- D2：`PositionHelper` 当前实现与全部调用点；段化后 `chain_before` / `chain_after` 的实际取值（是否恒为 1）。
- D3：是否存在 R3R 代码**写** `cargo_capacity`（烘焙/迁移）。
- D4：中国火车包是否真的用 `0x40`/`0x41`（需 GRF 侧证据或玩家提供同款车在原版 JGRPP 的对照读数）。

## 5. 需要玩家补充（编号答复即可回填）

- C-1：具体车种名 + 原容量 / 现容量数字（例："25T 硬座 90 → 30"）。
- C-2：**造出来就是 1/3**，还是要经过 R3R 升段 / 耦合 / 解挂 / 克隆之后才变 1/3？
- C-3：新建存档还是旧档；当前用的是哪个 exe（`build\openttd.exe` 时间戳）。
- C-4：是否只有中国火车包异常，原版车与其它 GRF 正常？

## 6. 取证记录

### 6.1 F-1：`PositionHelper` 确实被 R3R 改成"段内计数"（但只对有 ★ 的链生效）

`src\newgrf_engine.cpp:430-515`：

- `:452-459` 先沿 `Previous()` 回走到本段段首（遇 `IsSegmentFront()` 即停，一路无 ★ 则退到物理链头）；
- `:461-472` 只在 `[seg_front, 本段下一个 ★)` 范围内数 `chain_before` / `chain_after`；
- `:514` 返回 `chain_before | chain_after << 8 | (chain_before + chain_after + consecutive) << 16`（高 16 位=长度）；
- 注释 `:427-428` 自陈：**"Chains without any SegmentFront marker keep the original whole-chain semantics"**。

⇒ 纯"新建车、购入列表"（链上没有 ★）时 `0x40/0x41` 与原生逐字一致 ⇒ **G-1 只有在车已经被 R3R 升段（打 ★）之后才可能生效**，与"载入 GRF 就 1/3"这个描述对不上（除非玩家指的是"载入存档"且存档里这些车带 ★）。

### 6.2 F-2：容量的真正来源（`src\engine.cpp:237-332`）

| 行 | 事实 |
|---|---|
| `:242` | `CanCarryCargo()` 为假 ⇒ 直接 0 |
| `:262-278` | 若引擎有 `RefitCapacity` 回调且（非默认货种或 `cargo_subtype != 0`）⇒ 走 `CBID_VEHICLE_REFIT_CAPACITY`（0x15），**返回值原样使用、不再乘任何系数** |
| `:285` | 否则走 `PROP_TRAIN_CARGO_CAPACITY` |
| `:288` | **`v == nullptr`（购入列表）且 `railveh_type == Multihead` ⇒ `capacity += capacity`（×2）** |
| `:320-329` | 非 `NoDefaultCargoMultiplier` 模式下，若 `cargo_type != default_cargo`，按 `CargoSpec::multiplier` 比例换算 |

⇒ 三条可解释"1/3"的通道：
1. **乘数换算**（`:320-329`）：车实际 `cargo_type` 与引擎默认货种不同时，容量按货种 multiplier 比值缩放。若乘客 multiplier=3、目标货=1，比值正好 1/3。
2. **`railveh_type` 被改**（`:288`）：R3R 的假双头/段升级会 `SetEngine()/ClearWagon()` 改 subtype 位；若把 `Multihead` 改掉，购入列表就少算一半（**只能解释 1/2，不能解释 1/3**）。
3. **`cargo_subtype != 0` 或 `cargo_type` 被改写**触发 0x15 回调（`:262-263`），回调内部若按 `0x40/0x41` 或 `cargo_subtype` 分支，会返回不同容量。

### 6.3 判据进展

| 判据 | 状态 |
|---|---|
| D1 容量读取路径是否被 R3R 触碰 | **部分**：`DetermineCapacity` 本体未见 R3R 改动；`PositionHelper` 有 R3R 改动 |
| D2 `PositionHelper` 实现与段内计数 | **已完成**（§6.1），需 ★ 才生效 |
| D3 R3R 是否**写** `cargo_capacity` | **已完成：不写**（见 §6.4） |
| D4 中国火车包是否用 `0x40/0x41` 或 0x15 回调 | **已确认**（玩家现场读数 `0,36,0 → 0,12,0`，见 §6.8） |

### 6.4 F-3（D3 结案）：R3R 不写 `cargo_cap`，容量只有一条权威计算链

- `src\train_cmd.cpp` 里 `cargo_cap = ...` 只出现在原生路径：建车（`:2142-2143`）、虚拟车（`:3385-3386`）、`ConsistChanged` 重算（`:24109-24110` / `:24176-24177`），**全部是 `v->cargo_cap = rvi->capacity`（属性初值）后由 `DetermineCapacity()` 覆盖**；没有 R3R 烘焙/迁移容量（R3R 只烘焙 weight / power / max_speed，容量没有 override 字段）。
- 权威计算点：`Train::ConsistChanged()` → `e_u->DetermineCapacity(u)`（`src\train_cmd.cpp:508-520`），落地条件 `allowed_changes.Test(ConsistChangeFlag::Capacity)`。
- `ConsistChangeFlag`：`CCF_ARRANGE` **含** Capacity（所以耦合/解挂/段编辑后重算），`CCF_SAVELOAD` **不含**。
- `git diff HEAD` 对 `src\engine.cpp` / `newgrf_analysis.cpp` / `cargotype.cpp` 为空（这些文件已随提交入库，且函数体内无任何 `R3R` 标记）⇒ **`DetermineCapacity` 本体是原生逻辑**。

⇒ R3R 对容量的唯一影响面 = **喂给 GRF 的输入变量**，即 §6.1 的 `0x40/0x41` 与 §6.5 的 `parent_scope`。

### 6.5 F-4：`parent_scope` 也被改成"段头"（`src\newgrf_engine.cpp:1393-1396`）

```cpp
parent_scope(*this, engine_type, ((v != nullptr) ? R3RSegmentHeadOf(v) : v), rotor_in_gui),
```

- `R3RSegmentHeadOf()`（`src\couple_group.cpp:1247-1252`）→ `R3RGetCoupleGroupCarrier(Train::From(v))`；注释 `:1240-1242` 自陈：**单段链（全部 `R3R_SEGMENT_NONE`）与非 R3R 车返回自己的链头，行为逐字节不变**。
- ⇒ GRF 里凡是用 `VSG_SCOPE_PARENT` 的变量（`0x63`/`0x44` 家族）在**多段链**上会拿到"段头"而不是"链头"。

### 6.6 F-5：用 `0x40/0x41` 的 GRF **一定**走实时回调（不走预计算表）

`src\newgrf_analysis.cpp:410` 的 refit-capacity 白名单只有 `0xC, 0x1A, 0x47, 0x7D, 0x7E`；`:313` 规定 `ACOM_CB_REFIT_CAPACITY` 里出现 **非 SELF 作用域变量**即 `NON_WHITELIST_FOUND` ⇒ `refit_cap_whitelist_ok = false` ⇒ `Engine::DetermineCapacity()` 每次都实时问 GRF。

⇒ 若中国火车包的容量回调用 `0x40/0x41`（或 parent 变量），R3R 的段内化会**立即**改变容量，不存在"预计算缓存把差异藏起来"的可能。

### 6.7 疑点 REG-375-A（先登记，不当结论）：`0x40` 的长度高位比"总节数"少 1

`src\newgrf_engine.cpp:514`：

```cpp
return chain_before | chain_after << 8 | (chain_before + chain_after + consecutive) << 16;
```

`chain_before` 数到 `seg_front` 为止（不含 v），`chain_after` 从 `v` 开始数 v 之后的节数 ⇒ 总节数 = `before + after + 1`。所以：

- `0x41`（`consecutive = true`）⇒ 长度 = 段长度 ✓
- `0x40`（`consecutive = false`）⇒ 长度 = 段长度 **- 1** ✗

**存疑点**：`git log -S 'chain_before + chain_after + consecutive'` 把该写法归到上游 `svn r15617`（2009，"cache variables 40-43"），`-S 'chain_before + chain_after + 1'` 在本仓库历史里零命中 ⇒ 这**可能不是 R3R 引入的**，而是上游既有行为（也可能是我对上游循环结构的记忆有误）。
⇒ 处理：**不作为本轮结论**，只登记；若后续证实是 R3R 偏离，则任何依赖 `0x40` 长度做除法的 GRF 都会被系统性影响（长度 3→2、4→3…），这也是"容量比例异常"的一个候选通道。

### 6.8 F-6（决定性现场读数，2026-10-30 玩家）：`0,36,0 → 0,12,0`，原版非铰接车不受影响

玩家原话：

> 「我发现了，中国火车包原本的容量是0，36，0，成段之后现在变成了0，12，0，而原版非铰接式就没有这个问题」

判读（三条互相独立的证据同时指向通道 A）：

1. **严格线性、比例为 1/3**：`36 = 12 × 3`，`12 = 12 × 1`。容量与该车所在**连续同型号节数**成正比 ⇒ 该 GRF 用 `0x40`/`0x41` 的**长度高位（bit 16-23）**算总容量，而不是 `PROP_TRAIN_CARGO_CAPACITY` 的死值。
2. **`0,36,0` 的分布**证实"单元总容量集中在一节显示"：三节单元里只有中间节携带 36，头尾为 0。成段后每段只剩 1 节 ⇒ 每节按"单元长度 = 1"算 ⇒ 12。
3. **原版非铰接车不变**：单节车无论是否成段，连续同型号节数恒为 1 ⇒ `PositionHelper` 的段内化对它无影响。这正是通道 A 的判据（段化只改变"跨车"计数）。

排除项：

- **C（cargo multiplier）被排除**：multiplier 是全局货种缩放，会让原版车一起变 1/3，且不会呈现"只有中间节有值"的分布。
- **B（`parent_scope` = 段头）被排除**：父作用域切换不会产生严格 3:1 的线性关系，也不解释"头尾为 0"。
- 也**不是"载入 GRF 就变"**：玩家本轮把触发条件细化为"**成段之后**"，与 §2 的初始描述不同，以本轮为准（初始描述的"载入"是"载入该包的车"的省略说法）。

⇒ **通道 A 成立，KI-540 定性完成**。根因 = `PositionHelper()`（`src\newgrf_engine.cpp:430-515`）把 `0x40/0x41` 从"整链"改成"段内"计数（KI-458 / 第 290 轮的段化改造），而中国火车包用这两个变量的长度高位把它当作"同型号连续车厢数 × 单车容量"。

补充：`0,36,0` 究竟是 `0x40` 还是 `0x41` 无法从读数区分（三节同型号时两者相等），但**修法相同**，不影响结论。

## 7. 结论

**结论（已确证）**：`DetermineCapacity` 的算法是原生的；R3R 不写 `cargo_cap`；R3R 影响容量的唯一途径是"喂给 GRF 的变量"。本问题由**通道 A** 造成：

| 通道 | 机制 | 结论 |
|---|---|---|
| **A. `0x40/0x41` 段内化** | `PositionHelper`（`newgrf_engine.cpp:430-515`）把 position/length 从整链改成本段内；`:427-428` 自陈"无 ★ 的链保持整链语义" | **成立，就是它**（§6.8 现场读数） |
| B. `parent_scope` = 段头 | `newgrf_engine.cpp:1395` | 排除（无法解释严格 3:1 与"头尾为 0"） |
| C. cargo multiplier 换算 | `engine.cpp:320-329` | 排除（会让原版车一起变，且玩家已证原版非铰接车正常） |

**机理**：段化后每个段是独立的计数单元 ⇒ 一个原本 3 节的"单元"被拆成 3 个"1 节单元" ⇒ GRF 算出的单元容量从 `12×3` 掉到 `12×1`。这**不是** `cargo_cap` 被写坏，而是**输入给 GRF 的读数变了**。

## 8. 修法

**目标**：容量查询时让 `0x40/0x41` 按**整链**回答；**选图（sprite）回调仍按段内**，保住 KI-458 的初衷（多段耦合后不出现"亲嘴图"）。

**方案 1（推荐，最小语义改动）——按回调场景切换口径**

- 落点：`src\newgrf_engine.cpp` 的 `PositionHelper()` 增加第三参 `bool whole_chain`；`case 0x40` / `case 0x41` 判定当前是否处于容量查询。
- 判据来源：`VehicleGetVariable(v, object, ...)` 已经拿到 `VehicleScopeResolver`，可从其 `ro` 取 `GetCallback()`；当 `callback == CBID_VEHICLE_REFIT_CAPACITY` 时传 `whole_chain = true`。
- 缓存：整链模式**必须绕过** `grf_cache.position_consist_length` / `position_same_id_length`（同一辆车在选图与容量两种口径下会算出不同值，缓存会串味）。
- 影响面：只改容量；`0x40` 的选图路径（position in consist 选端头/中间脸）逐字节不变。

**方案 2（更粗，但一行级）——`0x41` 永不影响段化**

- `case 0x41` 直接传 `whole_chain = true`。理由：`0x41`（同型号连续车厢数）的经典用途就是容量/装载组，`0x40` 才是选图。
- 风险：若有 GRF 用 `0x41` 选图，多段耦合后的端头图会退回段化前的表现（即 KI-458 要修的那个"亲嘴图"回归）。**不推荐**，除非玩家确认该包不用 `0x41` 选图。

**方案 3——不改，GRF 侧适配**

- 让中国火车包改用 `PROP_TRAIN_CARGO_CAPACITY` 死值或改用 `parent`/`cargo_subtype` 表达单元容量。缺点：所有依赖 `0x40/0x41` 的老 GRF 都会中招，等于把 R3R 的改动成本转嫁给 GRF 作者。

**建议**：走方案 1。它精确对应"容量该按整链、选图该按段"这两个互不冲突的需求，不需要 GRF 白名单，也不会让选图回归。

**待玩家拍板**：是否按方案 1 落地（需改 `src\newgrf_engine.cpp` 一个 .cpp，不碰 .h ⇒ 可走增量构建）。

---

## 9. 方案 1 落地（2026-10-30，玩家拍板"按修法 1 试试吧"）

只改 `src\newgrf_engine.cpp` 一个 .cpp，未碰任何 `src\*.h` ⇒ 增量构建合规。

**9.1 `PositionHelper()` 增第三参**

```cpp
static uint32_t PositionHelper(const Vehicle *v, bool consecutive, bool whole_chain = false)
```

三处按 `whole_chain` 分叉：

| 位置 | 段内口径（`whole_chain == false`，原文行为） | 整链口径（`whole_chain == true`，新增） |
|---|---|---|
| 段头定位 | Train 时沿 `Previous()` 回走，停在最近的 `IsSegmentFront()` | 直接 `v->First()`，不看 ★ |
| 计数终点 | `u->Next()->IsSegmentFront()` 时 `break`（段边界不计入） | 不 break，走完整条物理链 |
| 翻转镜像 | `IsSegmentFlipped()` 时交换 `before`/`after`（还原 pre-flip rank） | **不交换**：整链口径要的就是物理链序 |

返回式不变：`chain_before | chain_after << 8 | (chain_before + chain_after + consecutive) << 16`。

**9.2 调用点判定，并绕过缓存**

`case 0x40` / `case 0x41` 各加一行前置分支：

```cpp
if (object->ro.callback == CBID_VEHICLE_REFIT_CAPACITY) return PositionHelper(v, false, true);  // 0x40
if (object->ro.callback == CBID_VEHICLE_REFIT_CAPACITY) return PositionHelper(v, true,  true);  // 0x41
```

- 判据来源：`ScopeResolver::ro`（public 引用）→ `ResolverObject::callback`（public 字段），同文件 `:919` 已有同款写法（`object->ro.callback == CBID_VEHICLE_START_STOP_CHECK` 等），非新机制。
- **直接 return，完全不碰 `grf_cache`**：`position_consist_length` / `position_same_id_length` 是按车缓存的单一值，不区分口径，读写任一侧都会串味。容量回调是"每次编组变化一次"，不在 sprite 热路径上。
- parent scope 递归共享同一个 `ResolverObject`（`ro` 是引用）⇒ 嵌套取别的车时 `callback` 仍是容量回调，口径一致。

**9.3 探针**

`PositionHelper()` 内新增 `POSCAP veh= eng= before= after= consec= head=`（上限 500 行，`R3RDbgOn()` 门禁）；原 `POSCONSIST` 行改为仅在 `!whole_chain` 时打，避免两种口径混在同一串里。

**9.4 影响面与不改的东西**

- 选图路径（`CALLBACK_FAIL` / 默认 sprite 选择 / `0x40` 端头脸判定）逐字节不变 —— 仍是段内语义，KI-458 的"亲嘴图"修复不受影响。
- `DetermineCapacity()`（`src\engine.cpp`）一行未动：它本来就是"问 GRF 要容量"，现在喂进去的读数回到原版值，问题自然消失。
- 未改 `newgrf_analysis.cpp` 的 refit-capacity 白名单：`0x40/0x41` 不在白名单里，仍然必走实时回调，正是我们要的。

**9.5 复测判据**

1. 中国火车包同一车种 refit 容量回到 `0,36,0`（成段前后一致）。
2. `build\R3R_debug.log` 出现 `POSCAP veh=… before=0 after=2 head=…`（3 节单元里中间那节）与 `POSCAP … before=0 after=0`（头/尾），即整链读数。
3. 多段耦合后**端头/尾车图像仍是段内端面**（KI-458 不回归）。
4. 原版非铰接车容量与图像均无变化。
5. 单段车 / 无 ★ 链：容量与之前逐字节相同（`whole_chain` 与段内口径此时本就等价）。

