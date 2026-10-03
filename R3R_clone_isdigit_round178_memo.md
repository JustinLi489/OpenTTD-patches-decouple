# R3R 第 178 轮 临时分析报告 —— 克隆「成段机车」当场断言崩溃 + 克隆丢 ★

- 日期：2026-10-01
- 轮次：第 178 轮
- 涉及条目：**KI-269**（崩溃，高）、**KI-270**（行为不符，中）
- 现场文件：
  - `build\R3R_debug.log`（254 行，2026-10-01 本次复现）
  - `%USERPROFILE%\Documents\OpenTTD\crash-20260930T165541Z.log`（崩溃报告）
- 改动文件：**仅 `src\vehicle_cmd.cpp`**（未碰任何 `src\*.h`、未碰 `src\lang\*.txt` ⇒ 增量合规）
- 构建：复用既有 `_tmp_inc_build.cmd`（**未新建任何 `.cmd`**），`EXIT_CODE=0`，
  `build\openttd.exe` @2026-10-01 01:08:36（51 559 424 B）

---

## 0. 结论速览

| # | 现象 | 根因 | 状态 |
|---|------|------|------|
| KI-269 | 复制「成段机车」时游戏弹"无法找出断言"并崩到桌面 | `CloneVehicleName()` 对**可能为负**的 `char` 调 `std::isdigit()`（MSVC `char` 默认有符号，中文名尾字节 `0x80..0xBF` ⇒ 负数）⇒ Debug CRT `_chvalidator` 断言 `0x80000003` | 已修（本轮） |
| KI-270 | 克隆出来的段**不再是段**（日志 `CLONE-DST veh=69 … SF=0`，源是 `SF=1`） | KI-223 加的 ★/⊗ 镜像被写在 `R3RIsCarOnlyFormation()` 分支里，只对"车厢假冒引擎"成立；本现场链头是真机车 ⇒ 分支不进。去铰接组的锁步块又只镜像**成员**、不镜像**组头** | 已修（本轮） |

两个缺陷互相独立：KI-269 是**任何**克隆一条"名字以非 ASCII 结尾"的列车都会崩（与成不成段无关），
KI-270 是**克隆段**时段的边界标记丢失（与名字无关）。玩家恰好两条一起踩到。

---

## 1. 现场材料（本次日志）

`build\R3R_debug.log` 尾部就是这次克隆的全部痕迹，行号原文：

```
244: MAKESEG CLONE-SRC head=24
245:   CLONE-SRC veh=24 p=-1 n=25 subtype=0x09 bits(front=1 wagon=0 engine=1 freeW=0 artic=0) rail(AH=1 AM=0 SF=1) eng=506
246:   CLONE-SRC veh=25 p=24 n=26 subtype=0x08 bits(front=0 wagon=0 engine=1 freeW=0 artic=0) rail(AH=0 AM=1 SF=0) eng=506
247:   CLONE-SRC veh=26 p=25 n=-1 subtype=0x08 bits(front=0 wagon=0 engine=1 freeW=0 artic=0) rail(AH=0 AM=1 SF=0) eng=506
248: END
249: MAKESEG CLONE-DST head=69
250:   CLONE-DST veh=69 p=-1 n=70 subtype=0x09 bits(front=1 wagon=0 engine=1 freeW=0 artic=0) rail(AH=1 AM=0 SF=0) eng=506   ← ★ 丢了
251:   CLONE-DST veh=70 p=69 n=71 subtype=0x08 bits(front=0 wagon=0 engine=1 freeW=0 artic=0) rail(AH=0 AM=1 SF=0) eng=506
252:   CLONE-DST veh=71 p=70 n=-1 subtype=0x08 bits(front=0 wagon=0 engine=1 freeW=0 artic=0) rail(AH=0 AM=1 SF=0) eng=506
253: END
```

读法（`subtype` 位：`0x01=GVSF_FRONT`、`0x08=GVSF_ENGINE`、`0x10=GVSF_ARTICULATED_PART`）：

- 源链 24/25/26 = 一条**三节铰接机车被"去铰接"**后的段：
  `artic=0`（没有真 `GVSF_ARTICULATED_PART`）而 `AH/AM` 有值 ⇒ 是**假铰接**（去铰接组），
  链头 24 是真机车（`front=1 engine=1`）并带 ★（`SF=1`）。
- 克隆 69/70/71：去铰接组重建**成功**（`AH=1` + 两个 `AM=1`，逐位与源相同），
  **唯独链头 69 的 `SF` 是 0**。

旁证：日志里只有**一对** `MAKESEG`（CLONE-SRC/CLONE-DST）。这符合玩家描述——第一次复制
"成段车底"（假引擎链，没有去铰接组）时 `r3r_identity_rebuilt` 不会被置位，那对 dump 根本不打印，
所以第一次克隆在日志里是**完全静默**的。第二次复制"成段机车"才既有 dump、又崩。

崩溃报告 `crash-20260930T165541Z.log`（时间 2026-09-30 16:55:41Z，exe
`r3r-stable-2026-09-29-m (2)` / Build 09-30 05:29:02）关键帧：

```
CloneVehicleName + 142   (src\vehicle_cmd.cpp:1449)
CmdCloneVehicle  + 5302  (src\vehicle_cmd.cpp:2324)
...
DepotWindow::OnVehicleSelect
异常码: 80000003  (EXCEPTION_BREAKPOINT = Debug CRT 断言)
```

`advance_order_on_clone: true`（与本次无关，仅记录）。

---

## 2. KI-269：崩溃根因与修法

### 2.1 根因

`CloneVehicleName()` 干两件事：①若名字末尾不是数字就补 `" 1"`；②若已有数字就把序号 +1。
两处都用了 `std::isdigit()`，而**实参直接来自 `std::string`**：

```cpp
if (!std::isdigit(*new_name.rbegin())) new_name += " 1";
...
if (pos != std::string::npos && std::isdigit(new_name[pos])) ...
```

MSVC 的 `char` 默认是**有符号**字符类型。玩家给这台机车起了中文名，UTF-8 编码下
名字最后一个字节落在 `0x80..0xBF`（续字节），读进 `char` 就是**负数**。
例：`名` = `E5 90 8D`，`.back()` = `0x8D` = **-115**。

C 标准规定 `std::isdigit(int)` 的实参必须是 `EOF` 或 `[0..UCHAR_MAX]` 内的值，
传负数（≠ EOF）是 **未定义行为**。Debug CRT 对这个 UB 有专门的钩子 `_chvalidator`：
它断言实参在 `[-1, 255]` 区间内，越界就 `__debugbreak()` ⇒ 异常码 `0x80000003`，
游戏表现就是"无法找出断言"然后退出。Release CRT 不做这个检查，所以这个坑只在
**内部测试版（Debug，探针 ON）** 上炸——正好是本项目日常用的那个 exe。

这也解释了玩家为什么能 100 % 复现：崩溃的不是"成段机车"这个身份，而是**这台机车的名字**。
只要被克隆的车名字尾字节 ≥ 0x80 就会崩，与成段/铰接/去铰接全都无关。

### 2.2 修法（`src\vehicle_cmd.cpp`，`CloneVehicleName()`）

```cpp
std::string new_name = src->name.c_str();
if (new_name.empty() || !std::isdigit(static_cast<unsigned char>(new_name.back()))) {
    new_name += " 1";
}
...
if (pos != std::string::npos && std::isdigit(static_cast<unsigned char>(new_name[pos]))) {
```

两点：

1. 实参先 `static_cast<unsigned char>`，把 `0x8D` 还原为 `141`，进入 `[0..255]` 合法区间。
   对原本就合法的 ASCII 数字（`0x30..0x39`）**逐位恒等**，行为不变。
2. `*new_name.rbegin()` → `new_name.back()` 并加 `new_name.empty()` 前置判断（`rbegin()` 在空串上
   解引用同样是 UB；虽然上游调用点有 `!v_front->name.empty()` 守卫，函数自身还是补上自证）。

对中文名的**语义**结果：`isdigit(0x8D)` 现在正常返回 0（非数字）⇒ 走"补 ` 1`"分支 ⇒
中文名克隆后变成 `原名 1`。这正是"名字末尾不是数字就补序号"的既有设计意图。

---

## 3. KI-270：克隆丢 ★ 的取证与根因

### 3.1 全部相关代码点

`CloneVehicle()`（`src\vehicle_cmd.cpp`）里能碰到 ★/⊗ 的地方一共三处，逐一看：

1. **构建循环内的"假引擎车厢"分支**（本轮改动前 ~:2154）
   条件是 `R3RIsCarOnlyFormation(Train::From(v))`，里面才写
   `if (src->IsSegmentFront()) dst->SetSegmentFront(); if (src->IsSegmentBack()) dst->SetSegmentBack();`
   —— 这就是 KI-223 加的镜像。
2. **去铰接组的锁步块**（~:2240）
   `R3RDearticulateOneGroup(dst_walk)` 之后，只对**成员**做
   `if (sm->IsSegmentFront()) dm->SetSegmentFront(); if (sm->IsSegmentBack()) dm->SetSegmentBack();`
   —— **组头自己**没有被镜像。
3. 其它地方没有。

### 3.2 根因

`R3RIsCarOnlyFormation()` 的定义（`src\train_cmd.cpp`）等价于
`IsEngine() && RailVehInfo(engine_type)->railveh_type == Wagon`，
即"**车厢**冒名顶替成引擎"的那种形态。

本现场链头 24 是**真机车**（`front=1 engine=1`，eng 506 是 `railveh_type != Wagon` 的机车），
`R3RIsCarOnlyFormation(24) == false` ⇒ 第 1 处分支**根本不进**。
而第 2 处的锁步块对**组头**（= 本现场的段首 24）没有镜像动作。
两处都盖不到 ⇒ 69 的 ★ 永远是 0。

顺带解释"为什么成员没丢"：成员 25/26 的 `SF=0`，本来就没有 ★，镜像与不镜像结果一样，
所以从日志上完全看不出成员这条线也是"只 set 不 clear"的半吊子写法。

对照玩家第一次克隆"成段车底"为什么没暴露：那种链头是**假引擎**（车厢做头），
`R3RIsCarOnlyFormation == true` ⇒ 第 1 处分支正常生效。**只有把"真机车"做成段**，
才会踩到今天这条路径。

### 3.3 修法（仅 `src\vehicle_cmd.cpp`）

新增共用静态函数，把"镜像 R3R 段边界标记"收敛成一处：

```cpp
static void R3RMirrorSegmentMarkers(const Train *src, Train *dst)
{
	if (src == nullptr || dst == nullptr) return;
	if (src->IsSegmentFront()) { dst->SetSegmentFront(); } else { dst->ClearSegmentFront(); }
	if (src->IsSegmentBack())  { dst->SetSegmentBack();  } else { dst->ClearSegmentBack();  }
}
```

**双向**镜像（有则置、无则清）而不是 KI-223 原来的"只置不清"：克隆的语义是**逐位复刻**，
新建车两位本来都是 0，所以"清"的那一半在常见情况下是空转；一旦源那边没有 ★（例如克隆一列
普通列车），"只置不清"会让任何残留位留下来，而"双向"能自愈。

四个落点：

| # | 位置 | 改动 |
|---|------|------|
| 1 | 构建循环的假引擎分支（原 KI-223 两行） | 改为调用 `R3RMirrorSegmentMarkers(src, dst)` |
| 2 | 锁步块 `R3RDearticulateOneGroup(dst_walk)` 之后 | **新增**：`R3RMirrorSegmentMarkers(src_walk, dst_walk)`（补上组头，本轮崩溃场景的正面修法） |
| 3 | 锁步块成员循环（原两行 `if`） | 改为调用 `R3RMirrorSegmentMarkers(sm, dm)` |
| 4 | 构建循环之后、KI-129 的 `ConsistChanged(CCF_ARRANGE)` 之前 | **新增兜底**：`R3RMirrorSegmentMarkers(Train::From(v_front), Train::From(w_front))` |

第 4 条覆盖一个前三条都盖不到的形态：**"段就是一个普通真机车（★，且完全没有去铰接组）"**。
它既不是假引擎链（第 1 条不进），也没有 `ArticGroupHead`（第 2 条不进），
在本轮改动之前克隆它会退回成一列**不成段**的普通列车。幂等，与前三条不冲突。

探针：`R3RDumpUpgradeDbg()` 的 `rail(AH=… AM=… SF=…)` 追加 `SB=%d`
（新格式 `rail(AH=%d AM=%d SF=%d SB=%d)`，其余字段逐字不动）。
这样 ⊗ 的镜像也能从日志核对——原dump只到 `SF`，⊗ 一直是"盲区"。

---

## 4. 为什么这样修是自洽的（★ 与段 ID 的关系）

第 157 轮把 ★ 的权威搬到了"段 ID"上：`R3RResyncSegmentFronts()`（`src\train_cmd.cpp`）
按 `w->r3r_segment_id` 把链上的段划分成 run，再逐车**重算** ★。
所以必须确认：我手工置上的 ★ 会不会被下一次 resync 抹掉。

不会，理由是它的**入口守卫**：

```cpp
4700: const uint16_t id = w->r3r_segment_id;   /* 逐车读段 ID 组成 should 列表 */
...
4717: if (should.empty()) return;              /* 整链没有段 ID ⇒ 直接返回，一个字节都不改 */
```

- 克隆**不会**复制 `r3r_segment_id`：`CmdCloneVehicle()` 的构建循环与链头块里都没有这个字段
  （该字段由 `R3RAssignSegmentIds()` 在"链里真有 ≥2 段"时才分配，单段链一律写 `R3R_SEGMENT_NONE = 0`）。
- 所以克隆链上每辆车的 `r3r_segment_id` 都是 `NONE` ⇒ `should` 为空 ⇒
  resync **提前返回**，既不会添加也不会删除任何 ★ ⇒ 我置上的 ★ 存活。
- 反过来，当这条克隆链**后来**被耦合进一条多段链时，`R3RAssignSegmentIds()` 会按
  `R3RGetSegmentHeads()`（**读的就是 ★**）切 run 并分配 ID，此时段 ID 表接管，
  与 ★ 一致 —— 不存在"flag 与 ID 打架"的窗口。

同一机制也解释了日志里另一个"本该出现却没出现"的现象：构建循环里 `CmdMoveRailVehicle()`
有一段"车库拖动补段标记"逻辑（`train_cmd.cpp:3194` 附近，条件含 `TrainHasEngine(src)`），
理论上会给刚建出来的中间车补 ★；但刚建出来的部件此时还是**真 artic 部件**
（没有 `GVSF_ENGINE` 位）⇒ `TrainHasEngine` 为假 ⇒ 没补。日志里 `CLONE-DST veh=70/71 SF=0`
正是这一点的实证。

---

## 5. 构建自证

- 复用既有入口 `_tmp_inc_build.cmd`（**未新建任何 `.cmd`**）
- `build\R3R_incbuild.guard.log` = `GUARD: incremental is safe (no header/lang file is newer than the newest object)`
- `build\R3R_incbuild.log` 尾部 = `[3/3] Linking CXX executable openttd.exe`
- `build\R3R_incbuild.done` = `EXIT_CODE=0`
- 日志中 `error C* / fatal error / FAILED: / build stopped` 计数 = **0**
- 时间链：`src\vehicle_cmd.cpp` 01:06:09 → `vehicle_cmd.cpp.obj` 01:07:13 → `build\openttd.exe` **01:08:36（51 559 424 B）**
- `read_lints(vehicle_cmd.cpp)` = 0 条
- exe 字面量自证：命中新探针格式串 `SF=%d SB=%d`（对照组 `MAKESEG` 仍在）

---

## 6. 复测判据（游戏内，待玩家）

1. **崩溃**：拿一台名字以中文（或任何非 ASCII 字节）结尾的机车做克隆 ⇒ **不再崩溃**
   （这是本次崩溃的直接判据；顺带用纯 ASCII 名字克隆一次，确认序号 +1 行为不变）。
2. **KI-270 正面**：克隆"成段机车" ⇒ `MAKESEG CLONE-DST` 的链头行出现 `SF=1`，
   与 `CLONE-SRC` 逐位一致；克隆出来的链**仍然是段**（可被"降级"、可被别的机车挂接）。
3. **不回退**：克隆"成段车底"（假引擎链）⇒ `CLONE-SRC/CLONE-DST` 逐行一致（KI-223 行为不退）。
4. **⊗**：若源链上有车带 ⊗，则克隆对应车 `SB=1`（新探针字段）。
5. **不造假段**：克隆一列普通（不成段）列车 ⇒ 全链 `SF=0 SB=0`。
6. **回归**：克隆后正常开出/加挂/改名不出现新异常。

---

## 7. 未确认项 / 边界 / 仍未做

1. **`SegmentFlipped`(bit26) 与 `ForceReserveOnce`(bit27) 有意未镜像。**
   前者会让"克隆一条被逻辑翻转过的段"的贴图与源不一致（不影响段身份，也不影响任何判据）；
   后者是"刚耦合完成才授予的一次性预留许可"，克隆继承它反而是错的。故不入镜像集合。
2. ~~**假引擎链的中间车厢**若带 ★/⊗ 也不镜像~~（`R3RIsCarOnlyFormation()` 只对引擎车成立，
   这是 KI-223 起就有的边界）。正常形态下中间车不带这两位，第 4 条兜底也只覆盖链头 ⇒ 未动。
   **【第 179 轮已修，KI-271】**：这条边界本身是个真缺陷——"段头是真机车的**中间**段"三处镜像全不进，
   克隆会把它变成非段。修法＝把镜像收口到构建后 lock-step 步进的**每一节**，删掉两处重复，
   并新增 `CLONE-MARKERS` 只读探针。详见 `R3R_clone_markers_round179_memo.md`。
3. **无实机复测**：上面 6 条判据全部待玩家用新 exe 复跑。本轮只做到"已实现 + 已编译"。（仍然成立，第 179 轮不改变。）
4. ~~**另一条历史崩溃未动**~~：`crash-20260928T185855Z.log` 的栈是
   `R3RSyncControlTraits (train_cmd.cpp:4576)` → `R3RSettleChainSegments` → `Couple`，
   与本次的 `CloneVehicleName` 无关，属**另一条路径**，本轮只登记不改。
   **【第 179 轮结清】**：它不是未修项——**就是第 154 轮已修的 KI-245**（同一份崩溃文件、同一条栈），
   修复行现位于 `train_cmd.cpp:4905`（函数已改名为 `R3RSyncSegmentTraits`），该行注释即引用这份崩溃。
   原文"另一条路径"的本意只是"与克隆崩溃无关的另一条"。
5. **名/号借用与克隆的交互未核**：第 160 轮的车名借用（`NAME-XFER`）只在 `Couple()` 里发生，
   克隆走的是另一条路（`CloneVehicleName()`），两者不冲突；但"克隆一条**正处于借用状态**
   的链头"时 `v_front->name` 是借来车组名，克隆会得到"车组名 1"——是否合乎预期未经玩家拍板。
   **【第 179 轮结论】**：判为**符合预期、不改码**——界面上显示的名字就是控制段名，克隆"所见即所得"复制显示名是自洽的；
   链自己的原名仍停在 `name_backup` 跟着**原链**走。若日后要求"克隆后沿用本链原名"，是一行改动，等拍板。
   另：`CloneVehicleName()` 里 `std::string new_name = src->name.c_str();` 这个 KI-245 同类点
   已在第 179 轮一并清零（KI-272）。
6. `R3R_KNOWN_ISSUES.md` 的"第 177 轮 §六.3 边界：dump 只到 AH/AM/SF、不含 SegmentBack"
   因本轮给 dump 补了 `SB=` 而**作废**，已在 KI-270 条目里注明。

---

## 8. 关联文件

- 已知问题清单：`R3R_KNOWN_ISSUES.md` 末尾「第 178 轮」小节（KI-269 / KI-270）
- 相关前轮备忘：`R3R_artic_flag_debug_round177_memo.md`（第 177 轮，真假铰接的 dump 判读）
- 相关代码：`src\vehicle_cmd.cpp` 的 `CloneVehicleName()` / `R3RMirrorSegmentMarkers()` /
  `CmdCloneVehicle()`；`src\train_cmd.cpp` 的 `R3RIsCarOnlyFormation()` /
  `R3RResyncSegmentFronts()` / `R3RAssignSegmentIds()`
- **后续轮次**：`R3R_clone_markers_round179_memo.md`（第 179 轮——把本备忘 §7 的第 2/4/5 条遗留项逐条结清：
  KI-271 镜像收口到全链每一节、KI-272 清零 `CloneVehicleName()` 的 KI-245 同类点、§五.4 与 KI-245 的同一性核对，
  并新增 `CLONE-MARKERS` 只读探针）
