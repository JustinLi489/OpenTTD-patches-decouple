# R3R 第 198 轮临时分析报告：解挂后"凭空冒出列车 7"

- 日期：2026-10-02
- 现场日志：`build\R3R_debug.log`（229 504 B，2026-10-02 03:33:22，最新一条为 `PERF-FINAL`）
- 现场二进制：`build\openttd.exe`（51 613 184 B，2026-10-02 03:26:16，含第 195/196/197 轮修复）
- 源码：`src\train_cmd.cpp`（915 456 B，2026-10-02 03:24:13）
- 结论状态：**根因已定位，已实施修复（A/B/C 三处），已增量编译通过，游戏内复测待做**
- 关联：KI-300

## 一、玩家现象（原话）

> 原本整个场景都没有列车 7，但是在一次 R3R 行为（挂接/解挂）之后，冒出来了一个列车 7，同时它的名称也出现了一个问题。

## 二、证据锚点（日志原文，逐行）

### 2.1 前一次解挂（v=27 机车 / u=6 国际段1）留下的状态 —— 车 0 的号就已停在备份里

```
1669:SEGTR-SNAP tag=decouple-v head=27 nseg=2 ctrl=0 borrowed=1
1670:SEGTR-SNAP   i=1 id=3 front=27 star=1 ctrl=0 | live u=1 n="T8701/2-国内段" gid=65534 | bk u=4 n="DF4D-3058" gbk=-1 | row use u=4 n="DF4D-3058" g=-1
1671:SEGTR-SNAP   i=2 id=1 front=0 star=1 ctrl=1 | live u=0 n="T8701/2-国际段1" gid=65534 | bk u=1 n="T8701/2-国内段" gbk=-1 | row use u=1 n="T8701/2-国内段" g=65534
```

- 链头 27（机车）：活号 **1**（穿的是控制段借给它的一套）、自己的号 4 停在 `unitnumber_backup`。
- 车 0（**控制段**，段 id=1，"国内段"）：活号 **0**、`unitnumber_backup == 1`、段行(row) = `1 / T8701/2-国内段`。
- 即：**车 0 自己的号 1 此刻正被链头 27 穿在身上**，车 0 保留着备份（号池里的位没放）。

### 2.2 本次解挂（v=27 / u=0，u 是 car-only 车底）

```
2121:DEPOT-ARR veh=27 spd=0 real=2(15) curType=4 stuck=0 ...
2122:DECOUPLE-FIRE consist=27 tx=60 ty=21 real=2 mode=2 num=1 segs=1 eff=0 plat=9/0
2123:ARRANGE-IN dh=-1 dst=-1 sh=27 src=0 mc=1
2127:TDTRY ok=1 v=27 u=0 err=0xFFFF v_head=27 u_head=0 borrowed=1 u_eng=0 v_eng=1 u_next=1
2130:CTRL-UNBORROW decouple-v head=27 unit=4 own_bk=0 hasname=1 own_name=0 own_g=65534
2131:SEGTR-SNAP tag=decouple-v head=27 nseg=1 ctrl=27 borrowed=0
2132:SEGTR-SNAP   i=1 id=0 front=27 star=1 ctrl=1 | live u=4 n="DF4D-3058" gid=65534 | bk u=0 n="" gbk=-1 | row none u=0 n="" g=-1
2133:CTRL-UNBORROW decouple-u head=0 unit=7 own_bk=0 hasname=1 own_name=0 own_g=65534
2134:SEGTR-SNAP tag=decouple-u head=0 nseg=1 ctrl=0 borrowed=0
2135:SEGTR-SNAP   i=1 id=0 front=0 star=1 ctrl=1 | live u=7 n="T8701/2-国内段" gid=65534 | bk u=0 n="" gbk=-1 | row none u=0 n="" g=-1
2143:DECOUPLE-DONE u=0 co=1 real=3 tx=60 ty=20 x=968 y=327
```

判读：

- `2127 u_eng=0` ⇒ 走 `DecoupleTrain()` 的 **car-only formation** 分支（`train_cmd.cpp:7604-7631`）。
- `2133 unit=7 own_bk=0` ⇒ 解出段（车 0）**活号 7、备份已被清 0**；`2135` 段内只有一行（单段链不建段行，`row none`），界面读活字段 ⇒ 列表里出现"**列车 7 / T8701/2-国内段**"。
- 号 1（车 0 自己的）**不在车 0 手上**，随后被归还方 `ReleaseID` 丢回号池。

## 三、根因（三处错位，串成一条链）

设 `u = 车 0`（被解出的 car-only 车底，且**它就是控制段**），`v = 车 27`（原链头，穿着 `u` 的号 1）。

1. **取号太早**：`DecoupleTrain()` 在 settle 之前就调 `R3RRestoreUnitNumber(u)`（`train_cmd.cpp:7626`）。此刻 `v` 还穿着 `u->unitnumber_backup == 1`，于是 `R3RUnitNumberUsedByOther(u, 1)` 为真。
2. **备份被销毁**：`R3RRestoreUnitNumber()` 在"查重命中"分支里**把 `unitnumber_backup` 清 0**（`train_cmd.cpp:2866-2870`，注释假设是"旧档/手工改档被真的占用"）。备份一丢，车 0 再也取不回 1。
3. **抢先领新号**：紧接着 `if (u->unitnumber == 0)` 直接领新号（`train_cmd.cpp:7627-7630`），号池里当时最小的空闲号就是 **7**。

随后 settle 阶段（`7900` / `7901`，早于此处的 7626 不可能，故顺序反了）：

- `R3RBorrowControlTraitsLive(v,"decouple-v")` 让 27 脱下借来的号 1 → 它的号块（`train_cmd.cpp:5888-5897`）此时发现**没人用 1**（车 0 已是 7），且 `R3RUnitNumberParkedByOther(27, 27, 1)` **只扫 27 自己那条链**（`train_cmd.cpp:5832-5840`），车 0 已被切走不在链上 ⇒ 判 false ⇒ `ReleaseID(1)`，**号 1 被丢回池**；27 恢复自己的 4（日志 2130）。
- `R3RBorrowControlTraitsLive(u,"decouple-u")`（`2133`）：车 0 的 `unitnumber_backup` 已是 0 ⇒ 号块整块跳过（`own_bk=0`）。
- `DecoupleTrain()` 末尾的"归还块"（`train_cmd.cpp` 内 `head_borrows_now` 三分支）此时 `v->unitnumber_backup` 已被 settle 清零、`v->unitnumber == 4 != u->unitnumber == 7` ⇒ 三分支全不成立、整块空转。

⇒ 终态：车 0 = 号 7（新号），号 1 空转回池。**"凭空冒出一个列车 7"**。

## 四、关于"名称也出现问题"

- 日志里解出段的名终结为 `T8701/2-国内段`（`2135`），与它自己的段行值 `row use u=1 n="T8701/2-国内段"`（`1671`）**一致**，即**名侧在本次现场看起来是对的**。
- 但名侧与号侧**共用同一个"取回被借用态挡下"的机制**（`R3RRestoreTrainName()` 只在 `name.empty()` 时取回，见 `DecoupleTrain()` 尾部 `if (u->name.empty()) R3RRestoreTrainName(u);`）。本现场 `u->name` 非空（穿的是自己的名），所以未触发。
- ⇒ **待确认项 R-1**：若玩家看到的名字问题是"列车 7 这一行的名字显示为别的段名 / 空名"，请提供截图或日志锚点；本轮修复后号与名将同时归位（车 1 / 国内段），名侧若复现再单独取证。

## 五、修法（已实施）

### A. `R3RUnitNumberParkedByOther()` 改为全池扫描（`train_cmd.cpp:5832`）

"某个号仍停在**别的活车**的 `unitnumber_backup` 里"这件事不该受"是否同一条链"限制 —— 解挂恰恰会把物主切到链外。签名由 `(const Train *chain, const Train *self, uint16_t num)` 收为 `(const Train *self, uint16_t num)`，内部 `for (Vehicle *w : Vehicle::Iterate())` 全池比对 `index`。只在"归还号"这条事件级路径调用（两处），代价可忽略。

### B. `R3RRestoreUnitNumber()` 查重命中时**保留**备份（`train_cmd.cpp:2866`）

不再 `head->unitnumber_backup = 0`，只打 `UNIT-RESTORE-DEFER veh=%d id=%u` 后返回。把"取回"让给紧随其后的 settle —— `R3RBorrowControlTraitsLive(ctrl==chain)` 的号块（`5888-5897`）会在**借用方已脱下之后**执行"`unitnumber_backup != 0 && unitnumber == 0`"⇒ 直接把号还给本车，且内层 `chain->unitnumber != 0` 为假 ⇒ 不会 ReleaseID 错号。
若那条路没跑到，`NormaliseTrainHead()` 的兜底仍会发一个合法新号（号不会丢）。

### C. `DecoupleTrain()` 的 car-only 分支**只有真没有备份时才领新号**（`train_cmd.cpp:7627`）

```cpp
R3RRestoreUnitNumber(u);
if (u->unitnumber == 0 && u->unitnumber_backup == 0) { ... NextID()/UseID ... }
```

有备份时故意留 `unitnumber == 0` 的窗口，等 settle（或末尾 `NormaliseTrainHead(u)`）取回；这正是第 148 轮续（KI-237）那条注释想要的效果，只是当时被"先领新号"抢了先。

## 六、修法组合后的预期执行序列（本次现场）

1. `7626` `R3RRestoreUnitNumber(u)`：`u->unitnumber==0`、`backup==1`，1 被 27 穿着 ⇒ `UsedByOther` ⇒ **保留备份**、打 `UNIT-RESTORE-DEFER`、返回（u 仍为 0）。
2. `7627` 门禁：`backup != 0` ⇒ **不领新号**。
3. `7900` settle(v)：27 脱下号 1；`R3RUnitNumberParkedByOther(27, 1)` 全池可见 `u->unitnumber_backup == 1` ⇒ **不 ReleaseID**；27 恢复 4（日志仍是 `CTRL-UNBORROW decouple-v ... unit=4`）。
4. `7901` settle(u)：号块命中（`backup=1 != 0`、`unitnumber=0 != 1`）⇒ 内层 `unitnumber != 0` 为假 ⇒ 不 ReleaseID ⇒ **`u->unitnumber = 1`** + `UseID(1)` + 备份清 0。
5. `8076` `NormaliseTrainHead(u)` 兜底（`unitnumber != 0` 提前返回，无副作用）。

⇒ 界面应仍显示 **列车 1 / T8701/2-国内段**，且号池里不再凭空出现 7。

## 七、未做 / 待确认

- R-1：名称问题的具体表现（见第四节）；本轮未改名侧代码。
- R-2：`R3RUnitNumberParkedByOther` 全池扫描后，是否会让"备份里残留的号"永不释放（号池位泄漏）。语义上"停放保留池位"是本项目一贯口径，且比"误释放正在使用的号"安全；仅作记录。
- R-3：`DecoupleTrain()` 中 `u->unitnumber == 0` 的窗口内（`7902-8075`）是否有子系统的强依赖。已核：`R3RSettleChainSegments`（写段行在 `CTRL-UNBORROW` 之后，取到的已是 1）、`R3RNormaliseChainGroups`、`GroupStatistics` 均不读 unitnumber 作前置条件。

## 八、复测判据（4 条）

1. 同一场景（车 27 挂车底后解挂出车 0 段）复现时：日志出现 `UNIT-RESTORE-DEFER veh=0 id=1`，**不再**出现车 0 的 `unit=7`。
2. 解挂后 `SEGTR-SNAP tag=decouple-u` 那一行应为 `live u=1 n="T8701/2-国内段"`。
3. 全程无 `UNIT-RESTORE veh=0`（车 0 的取回应发生在 settle 里，不经过该函数）或无异常新号；车辆列表里**不出现"列车 7"**。
4. 车库拖动（DEPOT-PARK/DEPOT-XFER-ID）/ 读档 / 耦合三条既有路径无回归：正常"取回自己的号"仍打 `UNIT-RESTORE`，且不误 ReleaseID 别人的号。

## 九、第 198 轮构建自证（2026-10-02 17:14）

- 入口：复用既有 `_tmp_inc_build.cmd`（**未新建任何 .cmd**；只改 `src\train_cmd.cpp` 一个文件，未碰 `src\*.h`）。
- `build\R3R_incbuild.guard.log` = `GUARD: incremental is safe (no header/lang file is newer than the newest object)`。
- `build\R3R_incbuild.log` 尾部 = `[3/3] Linking CXX executable openttd.exe`；`error C` / `fatal error` / `FAILED:` / `build stopped` 计数 **0**。
- `build\R3R_incbuild.done` = `EXIT_CODE=0`（写入时间 2026-10-02 17:14:04）。
- 时间戳三元组：`src\train_cmd.cpp` 17:06:20（917 680 B）→ `build\CMakeFiles\openttd_lib.dir\src\train_cmd.cpp.obj` 17:11:59（10 495 504 B）→ `build\openttd.exe` 17:13:55（51 613 184 B）。
- `read_lints` 0 条；exe 串自证：`UNIT-RESTORE-DEFER` 命中 1、`UNIT-RESTORE veh=` 命中 1。
- 构建前已确认 `openttd.exe` 未在运行（避免 LNK1168/exitCode 97）。
- 结论：**已实现 + 已编译通过；游戏内复测待做**（判据见第八节 4 条）。
