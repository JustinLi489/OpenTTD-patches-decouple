# R3R 第 179 轮临时分析报告：克隆段标记镜像收口到全链 + KI-245 同类点清零

- 日期：2026-10-01
- 口径来源：玩家指示「`R3R_clone_isdigit_round178_memo.md`，`R3R_KNOWN_ISSUES.md`，请你根据这两个继续未完成的任务」
- 本轮性质：**只做第 178 轮自己列出的遗留项**，逐条结清；不引入新功能、不改任何 `.h`、不改存档格式
- 改动文件：`src\vehicle_cmd.cpp`（唯一）
- 结果：已实现 + 已编译（`build\openttd.exe` 2026-10-01 01:33:57，51 561 472 B）；游戏内复测待玩家

---

## 0. 结论速览

| # | 第 178 轮遗留项 | 本轮结论 | 状态 |
|---|---|---|---|
| 1 | §五.2 / 备忘 §7.2：假引擎链的**中间车厢**带 ★/⊗ 也不镜像 | **是个真缺陷**，不止"中间车厢"——"段头是真机车的**中间**段"三处镜像全不进；已收口到"全链每一节" | 已修（KI-271） |
| 2 | §五.4 / 备忘 §7.4：`crash-20260928T185855Z.log` "另一条路径，只登记不改" | **它就是第 154 轮已修的 KI-245**（同一份崩溃、同一条栈），不是未修项 | 结清（无代码改动） |
| 3 | §五.3 / 备忘 §7.5：名/号借用与克隆的交互 | 克隆得到"车组名 1"**符合预期**（界面显示的就是控制段名） | 不改码，留档 |
| 4 | KI-245 条目里"同类隐患（未改，仅备案）" | `CloneVehicleName()` 的 `TinyString::c_str()` 已清零；全仓复核确认再无同类点 | 已修（KI-272） |

---

## 1. 第 178 轮遗留项的原文锚点

`R3R_KNOWN_ISSUES.md`「第 178 轮」小节 §五：

```
8597: 2. **假引擎链的中间车厢**若带 ★/⊗ 也不镜像（`R3RIsCarOnlyFormation()` 只对引擎车成立，属 KI-223 起的既有边界）；正常形态下中间车不带这两位，第 4 条兜底也只覆盖链头。
8598: 3. **名/号借用与克隆的交互未核**：… "克隆一条**正处于借用状态**的链头"会得到"车组名 1"，是否合乎预期未经玩家拍板。
8599: 4. **另一条历史崩溃未动**：`crash-20260928T185855Z.log`（`R3RSyncControlTraits (train_cmd.cpp:4576)` → `R3RSettleChainSegments` → `Couple`）是**另一条路径**，本轮只登记不改。
```

`R3R_clone_isdigit_round178_memo.md` §7 与之同源（第 2/4/5 条）。

---

## 2. KI-271：克隆丢中间段 ★/⊗ 的取证与修法

### 2.1 修前的镜像分布（三个地方，各有前提）

`CloneVehicle()`（`src\vehicle_cmd.cpp`）里只有三处会碰 ★/⊗：

1. **构建循环内的 KI-223 分支**：`if (R3RIsCarOnlyFormation(src))` —— `R3RIsCarOnlyFormation()` 等价于
   `v->IsEngine() && RailVehInfo(v->engine_type)->railveh_type == RailVehType::Wagon`，
   即"车厢改出来的**假引擎**"。只有这种车才会 `SetEngine()` + `R3RMirrorSegmentMarkers()`。
2. **去铰接组头**：`src_walk->flags.Test(VehicleRailFlag::ArticGroupHead)` 的分支里，额外对组头镜像一次。
3. **链头兜底**（第 178 轮新增）：构建循环之后，对 `v_front` / `w_front` 镜像一次。

### 2.2 缺口：三处都进不去的那一类车

一个车若同时满足

- 不是假引擎链（`R3RIsCarOnlyFormation()` 为假，如**真机车**、真车厢），
- 没有 `ArticGroupHead`（没被去铰接），
- 不是链头（`v_front` 之外）,

则三处镜像**全都不进**。最典型的形态就是**多段链里段头是"真机车"的中间段**——例如双机重联、
或任何"段头是普通机车而不是假引擎"的段。克隆出来那一段丢了 ★，**不再成段**（无法被单独降级、挂接时不再被当作段边界）。

注意这不是"边角形态"：第 178 轮 KI-270 修的正是"**段＝真机车**"这一形态，只是当时只修了**链头**那一个。
所以 §五.2 把它写成"正常形态下中间车不带这两位"是把缺陷当成了边界。

### 2.3 修法：把镜像**收口到唯一一处**（不做加法，做减法）

`CloneVehicle()` 构建循环之后本来就有一个 lock-step 步进循环：

```cpp
while (src_walk != nullptr && dst_walk != nullptr) {
    R3RMirrorSegmentMarkers(src_walk, dst_walk);   // ← 本轮新增（移到 while 体顶部）
    if (src_walk->flags.Test(VehicleRailFlag::ArticGroupHead)) { … }
    …
    src_walk = src_walk->GetNextVehicle();
    dst_walk = dst_walk->GetNextVehicle();
}
```

该 walk 已经用 `GetNextVehicle()` 走过**每一节真实车**（它跨过去铰接组成员与真 artic 部件，两侧同步前进），
组内成员由紧随其后的成员循环另行镜像 ⇒ 合起来覆盖"全链每一节"。于是：

- **新增**：`while` 体顶部一行 `R3RMirrorSegmentMarkers(src_walk, dst_walk);`
- **删除**：去铰接分支内那次组头镜像（已被顶部覆盖）
- **删除**：构建循环之后的链头兜底块（它的守卫 `v_front`/`w_front` 非空且为 Train，与 walk 的守卫**完全相同**
  ⇒ 纯冗余）

> **根因是"同一条规则写成三份"**：每多一个形态就得再补一份，第 178 轮补了第三份仍漏。故本轮只保留一份。

### 2.4 新增只读探针 `R3RDbgCloneMarkers()`（为什么必须要）

既有的 `R3RDumpUpgradeDbg()`（打 `CLONE-SRC` / `CLONE-DST`）**只在 `r3r_identity_rebuilt` 为真时打**，
也就是**只覆盖"源链走了去铰接"的情况**；而本轮修的多段真机车链根本没有组可拆 ⇒ 那两条日志不会出现
⇒ 没有新探针，本次修复在游戏内**无法自证**。

新探针（文件内 static，`src\vehicle_cmd.cpp`）：

```cpp
static void R3RDbgCloneMarkers(const Train *src, const Train *dst)
```

- 按 `Next()` 逐节统计 `srcSF/srcSB`（源链 ★/⊗ 数）与 `dstSF/dstSB`（克隆侧），并数 `diff`（逐节逐位不一致的车数）
- 步进上限 **512** 节：坏链不会把它变成挂死（只在克隆时跑一次，开销可忽略）
- 打印门槛：**源链"单段平凡"（★ 只有一个且 ⊗ 全无）且两侧计数完全一致 ⇒ 不打印**；其余一律打一行
  ```
  CLONE-MARKERS src=%d dst=%d cars=%u srcSF=%u dstSF=%u srcSB=%u dstSB=%u diff=%u[ REPORT-THIS-MISMATCH]
  ```
  ⇒ 「日志里没有 `CLONE-MARKERS`」永远等价于「这次克隆根本没有多段内容可复制」，不会与"没跑"混淆
- 调用点：lock-step 步进结束之后（`group_mismatch` 的卖车回滚分支之后，那条路径没有克隆存活）

### 2.5 自洽性复核（★ 会不会被下一次 `R3RResyncSegmentFronts()` 抹掉）

不会，沿用第 178 轮已核的结论：克隆**不复制** `r3r_segment_id`（单段链一律写 `R3R_SEGMENT_NONE`）
⇒ `R3RResyncSegmentFronts()` 的 `should` 为空、`if (should.empty()) return;` 提前返回、一个字节都不改
⇒ 置上的 ★ 存活；日后该链被耦合进多段链时 `R3RAssignSegmentIds()` 按 `R3RGetSegmentHeads()`（**读的就是 ★**）切 run，两者一致。

---

## 3. KI-272：清零 KI-245 备案的最后一个同类点

### 3.1 落点与形态

第 154 轮 KI-245 条目里那句"同类隐患（未改，仅备案）"指的就是：

```cpp
// src\vehicle_cmd.cpp  CloneVehicleName()   （备案时 :1424，现 :1488）
std::string new_name = src->name.c_str();
```

`src->name` 是 `TinyString`（`BaseConsist::name`），**空名字时 `c_str()` 返回 `nullptr`**
（`src\core\tinystring_type.hpp`：内部只有 `char *storage = nullptr`）。
把 `nullptr` 交给 `std::string` ⇒ `strlen(nullptr)` ⇒ `C0000005` 读地址 0、**无断言**——与 KI-245 同一个形态。

**可达性**：`CloneVehicleName()` 全仓只有一个调用点，在 `CloneVehicle()` 里且前面有
`if (!v_front->name.empty())` 守卫 ⇒ 当前不可达。但"靠调用点的守卫活着"不是可维护的不变式。

### 3.2 修法

```cpp
std::string new_name(static_cast<std::string_view>(src->name));
```

`TinyString::operator std::string_view()`（`tinystring_type.hpp`）对空串返回空 view、**永不接触空指针**；
与 `R3RSyncSegmentTraits` / `R3RSyncHiddenSegmentTraits` 里的用法一致。就地写注释说明来源与口径。

### 3.3 全仓复核（结论：除这一处外再无同类点）

把所有 `name` / `name_backup` 的 `.c_str()` 站点逐个过了一遍：

| 站点 | 判读 | 安全性 |
|---|---|---|
| `train_cmd.cpp:4839` `R3RBorrowControlTraits`：`own_row->name == own_name.c_str()` | 两侧都是 `std::string` | 安全 |
| `train_cmd.cpp:4905` `R3RSyncSegmentTraits`：`traits->name == row->name.c_str()` | RHS 为 `std::string::c_str()`（恒非 null）；LHS 是 `TinyString`，走 `TinyString::operator==(const char*)`，该运算符把 `nullptr` 判为 `empty()` | 安全（本身就是 KI-245 的修复行） |
| `train_cmd.cpp:5044` `R3RSyncHiddenSegmentTraits`：同理 | 同上 | 安全 |
| `train_cmd.cpp:5588` `R3RBorrowControlTraitsLive`：`chain->name == ctrl->name_backup.c_str()` | RHS **确实可能返回 nullptr**（两侧都是 `TinyString`），但 LHS 走的是 `TinyString::operator==(const char*)`，nullptr 被判为 `empty()` | 安全 |
| `couple_group.cpp` 里的 `CoupleGroup::name.c_str()` / `R3RSegmentRecord::name.c_str()` | 这两个 `name` 都是 `std::string`（不是 `TinyString`），`c_str()` 恒非 null | 安全 |
| **`vehicle_cmd.cpp:1488`** `CloneVehicleName()` | **唯一**把 `TinyString::c_str()` 直接喂给 `std::string` 的地方 | **本轮已消除** |

**唯一的危险形态**是「`TinyString::c_str()` → `std::string` 构造/比较」；
「`TinyString` ← `std::string::c_str()`」以及「`TinyString::operator==(const char*)`」都是安全的。

**纪律（沿用 KI-245，重申）**：R3R 新代码凡碰 `BaseConsist::name`（车辆名）或 `name_backup`，
一律用 `TinyString` 自己的比较运算符或 `std::string_view` 转换，**禁止把 `.c_str()` 交给 `std::string`**。

---

## 4. 第 178 轮 §五.4 结清：它不是"另一条未修路径"，就是 KI-245

### 4.1 核对过程

第 178 轮 §五.4 写的是 `crash-20260928T185855Z.log`，栈 `R3RSyncControlTraits (train_cmd.cpp:4576)` →
`R3RSettleChainSegments` → `Couple`。而第 154 轮 KI-245 条目的"玩家报告"原文是：

```
- **玩家报告**：①游戏发生**无断言崩溃**（`C:\Users\冯洁敏\Documents\OpenTTD\crash-20260928T185855Z.log`，
  崩溃时 exe Build date Sep 29 2026 02:21:33＝第 152 轮段表那一版）；…
- **崩溃栈**：`R3RSyncControlTraits`(train_cmd.cpp:4576) → `R3RSettleChainSegments`(:4602) →
  `Couple`(:9276)，最内层 `std::basic_string::_Equal` → `_Narrow_char_traits::length` → `strlen`，
  寄存器 `RCX=0`、异常 `C0000005` 读地址 `0x0`。
```

**同一份崩溃文件、同一条栈** ⇒ 第 178 轮 §五.4 说的"另一条历史崩溃"就是 KI-245。

### 4.2 当前源码状态

- 函数已在后续轮次改名为 **`R3RSyncSegmentTraits`**（`src\train_cmd.cpp:4877` 起）
- KI-245 的修复行现在位于 **`:4905`**：
  ```cpp
  if (!(traits->name == row->name.c_str())) { row->name = traits->name; changed = true; }
  ```
  该行**上方注释里直接写着这次崩溃的文件名**（`crash 2026-09-28T185855Z, this line`）
- 另一份字节级相同的崩溃 `crash-20260928T185454Z.log`（早 4 分钟）同属这一条

⇒ **第 178 轮 §五.4 是口径失误**：原句"另一条路径"的本意是"**与克隆崩溃无关**的另一条"，
但它并不属于"未修项"，第 154 轮就已修。本轮在清单里把该条划掉并留档，以免后续轮次再把它当待办捡起来。

---

## 5. 第 178 轮 §五.3（名/号借用与克隆）结论：符合预期，不改码

- 第 160 轮的车名借用（`NAME-XFER`）只在 `Couple()` 里发生：`v->name` 变成借来的**控制段名**，
  自己的原名停在 `v->name_backup`。
- 克隆这条链时 `CloneVehicleName()` 读的是 `v_front->name` ⇒ 得到"车组名 1"。
- **判为符合预期**：玩家在界面上看到的名字**就是**控制段名（第 160 轮之后活字段即"显示名"），
  克隆"所见即所得"地复制显示名是自洽的；链自己的原名留在 `name_backup` 里跟着**原链**走，不会被克隆带走。
- 若玩家日后要求"克隆后沿用本链原名"，是一行改动（改读 `name_backup`，空则回退活字段），等拍板。

---

## 6. 构建自证（2026-10-01）

- 复用既有入口 `_tmp_inc_build.cmd`（**未新建任何 `.cmd`**）
- `build\R3R_incbuild.guard.log` = `GUARD: incremental is safe`
- `build\R3R_incbuild.log` 尾部 = `[3/3] Linking CXX executable openttd.exe`
- `build\R3R_incbuild.done` = `EXIT_CODE=0`；日志 `error C*` / `fatal error` / `FAILED:` / `build stopped` 计数 = **0**
- `src\vehicle_cmd.cpp` 01:18:47 → `vehicle_cmd.cpp.obj` 01:19:41 → `build\openttd.exe` **2026-10-01 01:33:57（51 561 472 B）**
- `read_lints`（`vehicle_cmd.cpp`）= **0** 条
- exe 字面量自证：**命中** `CLONE-MARKERS src=%d dst=%d cars=%u` 与 `REPORT-THIS-MISMATCH`；
  对照组 `CLONE-SRC` / `CLONE-DST` 仍在（说明是增量产物而非陈旧 exe）

---

## 7. 复测判据（游戏内，待玩家）

1. **KI-271 正面**：克隆一条**多段链**、其中间段头是**真机车** ⇒ 日志出现 `CLONE-MARKERS` 且 `diff=0`
   （`srcSF == dstSF`、`srcSB == dstSB`）；克隆出来的**每一段仍是段**（每段可单独降级、挂接时仍被当作段边界）。
2. **不造假段**：克隆普通（不成段）列车 ⇒ **不出现** `CLONE-MARKERS`，且全链 `SF=0 SB=0`。
3. **第 178 轮行为不退**：克隆"成段机车"（链头真机车）与"成段车底"（假引擎链）⇒
   `CLONE-SRC` / `CLONE-DST` 逐行一致，链头 `SF=1` 仍在。
4. **⊗**：源链上有车带 ⊗ ⇒ 克隆对应车 `SB=1` 且 `diff=0`。
5. **崩溃面**：名字以中文（或任何非 ASCII 字节）结尾的机车 ⇒ 克隆不崩；纯 ASCII 名字 ⇒ 序号 +1 行为不变。
6. **若见 `REPORT-THIS-MISMATCH`**：说明源链自身标记就不自洽，或存在本轮仍未覆盖的形态；
   请把 `CLONE-MARKERS` 一行连同前后 20 行一起回传（`build\R3R_debug.log`）。

---

## 8. 未确认项 / 边界 / 仍未做

1. **`SegmentFlipped`(bit26) / `ForceReserveOnce`(bit27) 仍有意不镜像**（理由同第 178 轮 §五.1，未变）：
   前者是"逻辑翻转过的段"的贴图标记、不属段身份；后者是"刚耦合完成才授予的一次性预留许可"，克隆继承它反而是错的。
2. **真 artic 部件本身不带 ★/⊗**：R3R 的段机制只在真实车上工作，故不需要也不应镜像部件自身。
3. **无实机复测**：§7 的 6 条判据全部待玩家用新 exe 复跑；本轮只做到"已实现 + 已编译"。
4. **第 178 轮 §五.1/§五.5、第 177 轮各条复测判据仍然有效**，本轮不改变其口径。
5. 本轮**未**动 `r3r_segment_id` 的复制与否（仍是不复制）、未动 `R3RResyncSegmentFronts()`、
   未动任何 `.h`、未改存档格式。

---

## 9. 关联文件

- 已知问题清单：`R3R_KNOWN_ISSUES.md` 末尾「第 179 轮」小节（KI-271 / KI-272 + §五.4 结清）
- 直接前轮：`R3R_clone_isdigit_round178_memo.md`（第 178 轮：KI-269 克隆改名崩溃 / KI-270 克隆丢 ★）
- 更前轮：`R3R_artic_flag_debug_round177_memo.md`（第 177 轮，真假铰接的 dump 判读）
- 相关代码：`src\vehicle_cmd.cpp` 的 `R3RDbgCloneMarkers()` / `R3RMirrorSegmentMarkers()` /
  `CloneVehicleName()` / `CmdCloneVehicle()`；`src\train_cmd.cpp` 的 `R3RIsCarOnlyFormation()` /
  `R3RResyncSegmentFronts()`；`src\core\tinystring_type.hpp`（`c_str()` 与 `operator==` 的空串口径）
