# R3R 已知问题清单（KNOWN ISSUES）

> **长期规则（用户 2026-09-12 拍板）**：R3R 新功能开发产生的**任何已知问题**——未验证项、已知缺陷、硬伤、待复测点、被搁置的方向、工具链陷阱——必须在**同一轮内**写入本清单，不得只留在对话或代码注释里。
>
> 状态取值：`未修` `部分防护` `未验证` `待复测` `已搁置` `已修` `待实现` `作废`
> 严重度：`高`（崩溃 / 卡死 / 丢数据）`中`（行为不符）`低`（瑕疵 / 工具链卫生）

本文件于 **2026-09-15 重写**（原文件编码损坏，无法逐处修复）。条目编号与历史状态全部保留，不删除条目；修复后只改状态并注明轮次。

务必按照UTF-8编码进行编辑！！！

---

## 一、当前状态摘要（先读这节）

| 项 | 值 |
|---|---|
| 工作树 HEAD | `0d9ff990fc`（2026-09-15 20:19；第 18 轮起新提交） |
| 未提交改动 | 第 14~17 轮（见历史）+ **第 18/19 轮「挂接分组」全套**。新增：`src/couple_group_type.h`、`couple_group.h/.cpp`、`couple_group_cmd.h/.cpp`、`couple_group_gui.h/.cpp`、`src/sl/couple_group_sl.cpp`、`src/widgets/couple_group_widget.h`。改动：`src/CMakeLists.txt`、`src/sl/CMakeLists.txt`、`src/sl/saveload.cpp`、`src/widgets/CMakeLists.txt`、`src/command_table.cpp`、`src/command_type.h`、`src/window_type.h`、`src/vehicle_base.h`、`src/vehicle_gui.cpp`、`src/depot_gui.cpp`、`src/train_gui.cpp`、`src/train_cmd.cpp`、`src/pathfinder/yapf/yapf_destrail.hpp`、`src/pathfinder/yapf/yapf_rail.cpp`、`src/lang/english.txt`、`src/lang/simplified_chinese.txt`。**第 18/19 轮触碰了多个 `src/*.h`（`vehicle_base.h` 加字段、`couple_group.h` 加 `R3RGetChainScheduleOwner` 等）⇒ 触发 KI-15**：已按 KI-15 于 2026-09-16 01:15 完成第一次全量重编（691 步，`EXIT_CODE=0`）。**但 01:43~01:53 工作区又改过 3 个 `src/*.h` + 2 个 lang txt**（`widgets/depot_widget.h` 加 `WID_D_COUPLE_GROUPS`、`widgets/couple_group_widget.h`、`couple_group_gui.h`；`lang/english.txt`、`lang/simplified_chinese.txt`），其间两次「增量」产出（01:52 / 01:54）既是**混合对象**又踩了 KI-16（语言包版本不匹配）⇒ **01:54 的 exe 是坏 exe，已作废**。**已于 02:28 完成第二次全量重编**（删全部 `*.obj`，692 步，`EXIT_CODE=0`）⇒ 混合对象与语言包两个风险均已消除。**第 20~26 轮续改（同属「挂接分组」工作流）**：`git status --porcelain -- src` 实测相对 `0d9ff990fc` 共 **20 改 + 9 新** —— 改动 `CMakeLists.txt`、`command_table.cpp`、`command_type.h`、`depot_gui.cpp`、`group_gui.cpp`、`lang/english.txt`、`lang/simplified_chinese.txt`、`pathfinder/yapf/yapf_destrail.hpp`、`pathfinder/yapf/yapf_rail.cpp`、`sl/CMakeLists.txt`、`sl/saveload.cpp`、`train.h`、`train_cmd.cpp`、`train_gui.cpp`、`vehicle_base.h`、`vehicle_gui.cpp`、`vehicle_gui_base.h`、`widgets/CMakeLists.txt`、`widgets/depot_widget.h`、`window_type.h`；新增 `couple_group.h/.cpp`、`couple_group_cmd.h/.cpp`、`couple_group_gui.h/.cpp`、`couple_group_type.h`、`sl/couple_group_sl.cpp`、`widgets/couple_group_widget.h`（与 §二 各轮条目一一对应）。**第 25~26 轮又碰 `src/train.h` + `yapf_destrail.hpp` ⇒ 再次触发 KI-15**：已于 **19:23:23** 完成全量重编（691 步，`EXIT_CODE=0`，见下一行）⇒ 陈旧/混合对象风险已再次消除。**第 31~33 轮「按段刷新 + 按段结算」**（同属 R3R 工作流）再改：`src/linkgraph/refresh.h`、`src/economy_base.h`（**两个头文件** ⇒ 按 KI-15/KI-16 触发全量重编）、`src/linkgraph/refresh.cpp`、`src/economy.cpp`、`src/vehicle.cpp`、`src/train_cmd.cpp`（第 33 轮 KI-73 修复，仅 `.cpp` ⇒ 增量）。**第 39~43 轮「跨公司挂接 / 组共享」**再改（全部落在既有清单内，无新增、无删除文件）：`src/couple_group.h`、`src/couple_group.cpp`、`src/couple_group_cmd.h`、`src/couple_group_cmd.cpp`、`src/couple_group_gui.cpp`、`src/widgets/couple_group_widget.h`、`src/command_type.h`、`src/lang/english.txt`、`src/lang/simplified_chinese.txt`（另第 41 轮动过 `src/table/settings/gui_settings.ini`，不属本清单）。**其中第 39 / 43 轮改动含 `src/*.h` ⇒ 两次均按 KI-15/KI-16 走全量重编**（第 39 轮 `[701/701] Linking`、第 43 轮 `[706/706] Linking`，`EXIT_CODE` 均为 0；第 43 轮明细见上一行末）；第 41 轮只动 `.ini` + lang 正文，按生成头顺序做增量重编（`[16/16]` 含 `strings.cpp.obj` 重编，`EXIT_CODE=0`）|
| 内测树 exe | `build\openttd.exe` @ **2026-09-16 19:23:23**，50 621 952 B（`_tmp_full_build.cmd` **全量重编**：查 `openttd.exe` 进程 → 删 `build\**\*.obj` → `TEMP/TMP` 指 `build\tmp` → `vcvars64` → `ninja -C build -j2 openttd`；日志 `build\R3R_fullbuild.log` 末行 `[691/691] Linking CXX executable openttd.exe`，完成标记 `build\R3R_fullbuild.done` = `EXIT_CODE=0`）。**KI-15 合规已核对**：最新源码 `train_cmd.cpp` 18:52:33 / `yapf_destrail.hpp` 18:52:27 / `yapf_rail.cpp` 18:51:59 / `train.h` 18:51:56，最早 obj 18:54:54 ⇒ 源码全部早于 obj，**无陈旧 / 混合对象**；`findstr` 实测 exe 内含 `notSeg` / `target-not-segment` / `CG-PAINT` / `CG-RBG`（`R3RIsCoupleTarget` 这类纯函数名不命中属正常）⇒ **测的那版 = 刚改的那版**，可交付实测）。**第 30 轮增量重编**：`openttd.exe` @ **2026-09-16 21:06:19**（`_tmp_inc_build.cmd`；仅改 `train_cmd.cpp` / `vehicle_cmd.cpp` 两个 `.cpp`，未碰任何 `src/*.h` ⇒ 不触发 KI-15/KI-16，增量合规；日志 `build\R3R_incbuild.log` 末行 `[4/4] Linking CXX executable openttd.exe`，完成标记 `build\R3R_incbuild.done` = `EXIT_CODE=0`）。**第 37 轮 P3 全量重编（与 KI-66 合并为同一次，即当前最新内测产物）**：`build\openttd.exe` @ **2026-09-17 16:23**，50 633 728 B，691 步 `EXIT_CODE=0`；`stale_obj_count=0`（`newest_src=vehicle_base.h @ 16:02:21` < `oldest_obj=alloc_func.cpp.obj @ 16:03:20`）⇒ KI-15 卫生合格；产物自证 `R3R-PAY-FINAL` / `R3R-PAY-TRANSFER` 命中、`ADVANCE: veh=` 已消失（`_tmp_verify_p3.cmd`）。详见 §4-10 与台账 §10.8。**第 41 轮增量重编**：`build\openttd.exe` @ **2026-09-17 19:07:54**，50 639 872 B（`_tmp_inc_build.cmd`）。本轮仅改 `src/table/settings/gui_settings.ini` + `src/lang/simplified_chinese.txt`（+ 工作区外 `openttd.cfg`），**未碰任何 `src/*.h`** ⇒ 不触发 KI-15；因改了语言 txt，按 KI-16 惯例先删 `strings.cpp.obj`（语言包版本）与 `settings_table.cpp.obj`（它是唯一 `#include "table/settings.h"` 的 TU，默认值翻转必须重编它）后增量重编 ⇒ 日志 `build\R3R_incbuild.log` 末行 `[16/16] Linking CXX executable openttd.exe`，完成标记 `build\R3R_incbuild.done` = `EXIT_CODE=0`。**KI-16 自检通过**：`build\lang\english.lng` 与 `simplified_chinese.lng` 头 4 字节 = `EB 76 DC 0E` = `0x0EDC76EB` == `build\generated\table\strings.h` 的 `LANGUAGE_PACK_VERSION`（本轮 `strings.h` 未重新生成 —— `english.txt` 未改，版本号不变，故无「No available language packs」风险）。**第 42 轮增量重编**：`build\openttd.exe` @ **2026-09-17 19:29:45**，50 639 872 B（`_tmp_inc_build.cmd`；仅改 `autoreplace_cmd.cpp` / `vehicle.cpp` / `train_cmd.cpp` **三个 `.cpp`**，未碰任何 `src/*.h` ⇒ 不触发 KI-15、未改语言 txt ⇒ 不触发 KI-16；日志 `build\R3R_incbuild.log` 末行 `[5/5] Linking CXX executable openttd.exe`，完成标记 `build\R3R_incbuild.done` = `EXIT_CODE=0`）。语言包自检：`english.lng` / `simplified_chinese.lng` 的 `ident`@0 = `0x474E414C`('LANG')、`version`@4 = `0x0EDC76EB` == `strings.h:6973` 的 `LANGUAGE_PACK_VERSION`。**注（口径修正）**：崩溃日志里的 `Build date` 取自 `rev.cpp` 的 `__DATE__ __TIME__`（`build/generated/rev.cpp`），**不等于 exe 链接时间** —— 第 41 轮那次增量构建就没有重新生成 `rev.cpp`，因此 `crash-20260917T111642Z.log` 显示 `Sep 17 2026 17:40:42` 而实际跑的是 19:07:54 的 exe（该次崩溃第 42 轮已定位并修复，见 KI-89）。判断"测的那版"**一律以 `openttd.exe` 文件时间戳 + obj 时间戳为准，不要看 Build date**。**第 43 轮全量重编（两个跨公司开关合并，改 `src/*.h` + 双语 lang ⇒ 按 KI-15/KI-16 必须全量）**：`build\openttd.exe` @ **2026-09-17 20:20:48**，50 637 824 B（`_tmp_full_build.cmd`：查 `openttd.exe` 进程 → 删 `build\**\*.obj` → `TEMP/TMP` 指 `build\tmp` → `vcvars64` → `ninja -C build -j2 openttd`；日志 `build\R3R_fullbuild.log` 末行 `[706/706] Linking CXX executable openttd.exe`，完成标记 `build\R3R_fullbuild.done` = `EXIT_CODE=0`；日志内 `error C` / `fatal error` / `FAILED:` 计数 0；obj 总数 619）。**KI-15 卫生核对通过**：`newest_src = couple_group_gui.cpp @ 19:46:15` < `oldest_obj = alloc_func.cpp.obj @ 19:48:50` ⇒ `stale_obj_count = 0`，无陈旧/混合对象。**KI-16 自检通过**：`strings.cpp.obj`（`build\CMakeFiles\openttd_lib.dir\src\strings.cpp.obj`）@ 20:15:31；`build\lang\english.lng`（19:49:51）与 `simplified_chinese.lng`（19:50:03）头 4 字节 = `4C 41 4E 47 EB 76 DC 0E`（'LANG' + `0x0EDC76EB`）== `build\generated\table\strings.h` 的 `LANGUAGE_PACK_VERSION = 0xEDC76EB`。**注**：本轮只改既有串的**正文**、未增删串，故 `LANGUAGE_PACK_VERSION` 与第 41/42 轮相同（`strings.h` 未重新生成，时间戳仍 17:36:00 —— 版本号一致即无「No available language packs」风险，口径见记忆 52814982）。**产物自证**：`findstr` 可检索到新探针串 `CG-OPEN` 与 `allow_others` ⇒ 测的那版 == 刚改的那版。**第 44 轮增量重编（KI-92，仅改 `src\train_cmd.cpp` ⇒ 增量合法）**：`EXIT_CODE=0`、日志末行 `[3/3] Linking CXX executable openttd.exe`、`train_cmd.cpp.obj` @ 20:59:26 > 源码 @ 20:58:09、`build\openttd.exe` @ **2026-09-17 21:00:25**（50 637 824 B）；未改头文件/语言文件 ⇒ 无 KI-15 全量要求、语言包版本沿用 `0xEDC76EB` |
| 内测树构建 | **Debug**（`/Od /Ob0 /RTC1 -MTd -D_DEBUG`，探针默认 ON，见 KI-23 / KI-28） |
| 发行树 | `build-release\`（RelWithDebInfo `/O2 /Ob2`，探针默认 OFF） |
| 性能维度 | **已降级为「低」并搁置**（2026-09-14 用户拍板），见 §三-1 / §五 |
| 当前焦点 | 功能缺陷修复（折叠 / 解挂排程归属 / 段边界标记），**不是**性能 |
| 最近一轮（第 45 轮） | **KI-93 加固：车库「卖光本库车辆」连环删除导致的悬垂 `OrderList` 崩溃**（`crash-20260917T130637Z.log`）。栈 = `CmdDepotSellAllVehicles` → `CmdSellRailWagon` → `delete sell_head` → `Vehicle::PreDestructor` → `DeleteVehicleOrders` → `IsOrderListShared()` / `RemoveFromShared()` 读已释放列表（调试堆毒值 `0xDDDD…` 被当成 shared 登记）。**根因**：路线 A 的「借用」不注册为共享链（`num_vehicles` 仍为 1 ⇒ `IsOrderListShared()==false`）⇒ `OrderList::FreeChain` / `DeleteVehicleOrders` 的「非共享即可销毁」判定在**借用者与主人任一方先被删**时真删列表，另一方悬垂；`CmdDepotSellAllVehicles` 逐辆下单，先卖 owner 段再销毁借用者必命中（第 44 轮的入口交接钩子挡不住这种中间态）。**加固（仅 `.cpp` ⇒ 增量合法）**：①`order_cmd.cpp` 新增静态 `R3RFindOrderListReferrer(ol, except)`（遍历 `Vehicle::Iterate()` 找仍引用该列表的其它活车）；②`OrderList::FreeChain(false)` 把判定提到**清空 orders 之前**，命中即把 `first_shared` 重指活车并 `return`（列表**原样移交**、不 `delete`）；③`DeleteVehicleOrders` 非共享分支命中时改走 `v->orders=nullptr; ol->FreeChain(false);`，且**不**打 `UpdateDeparturesWindowVehicleFilter`（列表仍活着）；④`Vehicle::PreDestructor` 释放 `orders_backup`、`afterload.cpp` 清 `OT_NOTHING` 一律**先解绑再 `FreeChain`**（判定会遍历活车，必须先摘掉自己的指针）；⑤借用归还块加例外：读档后借用已是**原生共享链**（`IsShared()` 且在链中）则走 `RemoveFromShared()`，不硬摘指针（否则别的成员被留成指向将死车辆）。**编译**：`_tmp_inc_build.cmd` 增量 `EXIT_CODE=0`、末行 `[5/5] Linking CXX executable openttd.exe`、`build\openttd.exe` @ **2026-09-17 21:28**（50 637 824 B），`order_cmd.cpp.obj` @ 21:25:42 / `vehicle.cpp.obj` @ 21:25:57 早于 exe ⇒ KI-15 合规。**未实测（清单见 KI-93）**：车库卖光耦合链、卖 owner / 卖借用者后半程、读档后卖光、出发板一致性。**零头文件改动**：`order_base.h` 曾短暂加过公开 setter，已完全撤回原样 |
| 上一轮（第 44 轮） | **KI-92 结案：车库内手动解耦泄漏「调度借用」**（玩家 2026-09-17 实报「车库内手动解耦后机车段没有变回自己的调度命令，仍保持耦合时刻的调度命令」，并问「机车与列车属不同公司有无关系」）。①**现场定因**：日志 `ARRANGE-IN dh=-1 dst=-1 sh=0 src=9 mc=1` 表明解耦走的是 `TrainDepotMoveSegment`（`depot_gui.cpp`）→ `Command<MoveRailVehicle>` → `ArrangeTrains` 拆链，**不是** `DecoupleTrain`；该路径原先零 R3R 交接 ⇒ `r3r_priority` / `r3r_orders_borrowed` / `orders_backup` 全停在耦合那一刻，日志尾 `SKIP-STOPPED veh=0 ... real=17` 与 `veh=9 ... real=17`（同一车库 38,27）证明机车与车底仍指向**同一份 `OrderList`**；**与公司无关**（命令层无跨公司分支，同公司路径一致）。②**修复**：新增静态辅助 `R3RSyncChainAfterDepotEdit(Train *chain)`（`train_cmd.cpp:4068`，= `R3RRenumberPriorities(chain)` + `R3RSyncDrivingOrders(chain, false)`；前向声明 `:2294`，因为是 static 而 `CmdMoveRailVehicle` 在文件更上方），在 `CmdMoveRailVehicle` 的 Execute 分支 `:2615-2616` 对 `src_head` / `dst_head` 各调一次（**Execute 块内 ⇒ 试算阶段不改状态**），在 `CmdSellRailWagon` 的 `NormaliseTrainHead(new_head)` 之后、`delete sell_head` 之前 `:2731` 调一次（先归还借用再释放列表 ⇒ 顺带消掉「卖掉 owner 段 → 机车持悬垂指针」的隐患）。③**编译**：仅改 `.cpp` ⇒ 增量合法，`EXIT_CODE=0`、`[3/3] Linking CXX executable openttd.exe`、`train_cmd.cpp.obj` 20:59:26 > 源码 20:58:09、`build\openttd.exe` @ **2026-09-17 21:00:25**（50 637 824 B）；未改头文件与语言文件。④**未实测**：拖出车底 / 拖出机车 / 整列（不拆）重排 / 卖掉车底 四场景，见 KI-92 |
| 更早（第 43 轮） | **KI-91 结案：两个跨公司开关合并为单一开关**（玩家 2026-09-17「那两个按钮是可以合并的对吧，那么就先合并再实测吧」）。①**数据层**：新增 `CoupleGroup::CGF_ALLOW_OTHERS = CGF_SHARED \| CGF_CROSS_COMPANY` 与 `bool AllowsOthers() const`（`couple_group.h:60-73`），两位**保留**、恒同置同清，三个读判据全部改走 `AllowsOthers()`（`R3RCoupleGroupMasksAllowCrossCompany` `couple_group.cpp:137`、`IsVisibleTo` `:391`、`IsJoinableBy` `:407`）⇒ **存档字段不变，旧档「任一位为 1 即视为开放」自动兼容、无需迁移**；②**命令层**：`SetCoupleGroupFlags` + `SetCoupleGroupShared` 合并为单一 `Commands::SetCoupleGroupAllowOthers`（`CmdDataT<CoupleGroupID, bool>`，`DEF_CMD_TUPLE_NT`），执行体 `R3RSetCoupleGroupAllowOthers`（`couple_group.cpp:148-186`：开 = `flags \|= CGF_ALLOW_OTHERS`；关 = 清两位 **且** 逐段驱逐他公司段并返回驱逐数，**不解开已挂列车**），owner-only 与 `CG-OPEN group=%u allow_others=%u evicted=%u` 探针不变；③**GUI**：两个复选框 → 一个（`WID_CG_SHARED_TEXT` / `WID_CG_SHARED` 删除，`WID_CG_CROSS_COMPANY` 注释改 D6-1+D6-3），`couple_group_gui.cpp:418-421` 用 `AllowsOthers()` 驱动 lowered/disabled、`:509` 改投新命令、`CG-PAINT` 只留 `allow_others << 30`；④**语言**：`STR_COUPLE_GROUP_CROSS_COMPANY(_TOOLTIP)` 改为「允许他公司加入并跨公司挂接」（tooltip 写明关掉会把他公司段移出组、不改所有权、不解开已挂列车），旧串 `STR_COUPLE_GROUP_SHARED(_TOOLTIP)` 保留但**已无引用**。**效果**：死状态 `{共享=1, 跨公司=0}`（能进组但永远挂不上、`COUPLE-FAIL` 刷屏）**从此不可达**。⑤**编译**：改 `src/*.h` + 双语 lang ⇒ 按 KI-15/KI-16 **全量重编**，见「内测树 exe」行的第 43 轮记录（`EXIT_CODE=0`、`[706/706] Linking`、`stale_obj_count=0`、语言包版本一致、产物可检索 `CG-OPEN`/`allow_others`）。**本轮未实测**：单复选框开/关、关闭时驱逐他公司段且不解开已挂列车、旧档兼容（任一位=1 显示为已开），清单见 `R3R_crosscompany_design_memo.md` §12.6；关联修订 KI-85 / KI-90 |
| 更早（第 42 轮） | **KI-89 结案：跨公司耦合链进库触发的 `vehicle.cpp:191` 断言崩溃已定位并修复**。玩家 2026-09-17 两次崩溃（`crash-20260917T111642Z.log` / `crash-20260917T112613Z.log`）断言一致 = `Assertion failed at line 191 of ...\vehicle.cpp: c == Company::Get(this->owner)`，栈 = `Vehicle::NeedsAutorenewing`(vehicle.cpp:191) ← `GetNewEngineType`(autoreplace_cmd.cpp:306) ← `CmdAutoreplaceVehicle`(:965) ← `CallVehicleTicks`(vehicle.cpp:1845)，命令上下文 `cmd: 0xA0 CmdAutoreplaceVehicle`（`cc:0`）。真因 = 上游 auto-replace/autorenew 家族假设「一条物理链 = 一个公司」：`CallVehicleTicks` 用链头 owner 设 `_current_company` 并只传单一 `Company*`，而 `GetNewEngineType` 对链上每个 unit **无条件**调 `v->NeedsAutorenewing(c)`（`e == Invalid` 也调，故玩家即使没设任何替换规则也会命中）⇒ 跨公司链上他公司那一节断言成立失败。修复 = 三处**静默**守卫（均返回无消息 `CMD_ERROR`，经 `ShowAutoReplaceAdviceMessage`（vehicle.cpp:1425）对 `INVALID_STRING_ID` 早退 ⇒ 不产生 AutorenewFailed 新闻、不刷报错）：①`CmdAutoreplaceVehicle`（autoreplace_cmd.cpp:954-967）在 `IsChainInDepot()` 之后对非 free-wagon 链沿 `Next()` 逐节查 owner，混公司即拒绝（同时杜绝整链重建把他公司车卖掉）；②`Vehicle::NeedsServicing`（vehicle.cpp:315-328）逐 unit 循环改取 `vc = (v->owner == this->owner) ? c : Company::Get(v->owner)`；③`CmdTemplateReplaceVehicle`（train_cmd.cpp:11413-11420）重建整链前同样逐节查 owner。增量重编 3 个 `.cpp` `EXIT_CODE=0`（见下方「内测树 exe」行）。**本轮未实测**（跨公司链进库不再崩、同公司链 auto-replace / template-replace 行为逐位不变，均待玩家复测）。另登记玩家本轮提出的两条挂接分组界面备忘 = **KI-90**（乙打开甲的分组窗口时「新建挂接分组」+ 两个跨公司开关应置灰）与 **KI-91**（两个跨公司开关能否合并为一个），均**待实现**、本轮未动源码 |
| 再上一轮（第 41 轮） | **KI-87 结案：新订单默认改为「沿途各站都停」，并按【实际行为】统一文案**（玩家 2026-09-17 裁决，选候选 (A)）。①`src/table/settings/gui_settings.ini` 的 `gui.new_nonstop` 由 `def = true` 改 `def = false`（生成物 `build\generated\table\settings.h:895` 已成 `false`）⇒ 新建的车站订单（`order_gui.cpp:1544`）与车库订单（`:1491`/`:1876`）默认 `ONSF_STOP_EVERYWHERE`（沿途各站都停）；②`src/lang/simplified_chinese.txt:1449` 由「设定命令时默认选择"直达"命令：{STRING}」改为「新建命令时默认选择"不停车"命令：{STRING}」，与订单列表的「不停车前往」同口径（`{STRING}`/`{STRING2}` 仅占位符写法差异，`STR_CONFIG_SETTING_VALUE = {ORANGE}{STRING1}` 使两者都渲染为橙色的开/关值）；③玩家本机 `C:\Users\冯洁敏\Documents\OpenTTD\openttd.cfg:319` 的遗留 `new_nonstop = true` 已按字节级改写为 `false`；④增量重编 `EXIT_CODE=0`，产物见「内测树 exe」行。**本轮未实测**（游戏内新建订单是否确显示「前往」）；**残留边界**（老 `openttd.cfg` 里已存在的该键不会自动翻转）登记为 KI-88。详见 §二 KI-87 / KI-88 |
| 最近一轮（第 38 轮） | **多工作目录合并 + 交接文档建立**。C 盘旧会话目录 `c:\Users\冯洁敏\CodeBuddy\20260914233818` 经逐文件 MD5 比对后迁入工作区并删除（结论：无独有源码，权威版本一直在本工作区）；新建 **`R3R_handover.md`**（换会话/换目录第一入口：权威文件地图、未提交改动风险、P1/P2/P3 待实测清单、工具链 15 条铁律、C 盘占满真因）；归档目录 `R3R_archive_20260914_wip\`（09-10~09-14 WIP 的完整 diff 留证，已由 `2db952d332` 保底）。**KI-83 结案（已修）**。**代码侧零改动**，产物仍是 `build\openttd.exe` @ 2026-09-17 16:23。**下一步 = 先 checkpoint commit（未提交量 25 改 + 9 新），再跑 P1/P2/P3 实测**，见 §4-10 |
| 最近一轮（第 28~37 轮） | **第 28 轮**：玩家实测会话 1（`build\openttd.exe` @ 19:23:23，日志 320 行）已核对，`R3R_couple_group_design_memo.md §14.4` 四项已填（①段身份保持 = 功能侧通过；②散链不可瞄准 = 未触发；③硬闸门 = 条件②③通过、①未触发；④分组白名单 = 判定在位、拒绝分支与窗口显示未触发），并校正三处探针口径（`reject=` 串名、`CPL-GATE` 行尾 `grp=` 语义、`ADVANCE` 探针非缺陷 → 新增 KI-66）；剩余三个补测动作见该备忘 §15.4。**第 29 轮**：玩家澄清场景「库内应有两次挂车，第二次在列车进库解挂之后机车应重新与段耦合，实际没有」已定位真因 = `R3RRelocateFrontIdentity` 身份迁移把 `cur_real_order_index` 覆盖成 0（KI-67），已修并增量重编（`openttd.exe` 20:47，`EXITCODE=0`），**待玩家复测**。**第 30 轮**：复测日志证实 KI-67 已生效（`ADVANCE: veh=2 order=5 real_before=3`，不再被覆盖成 0），卡点后移到下一环 = KI-68（解挂出的车底段头提升为 front engine 后未重建车辆 tick 缓存 ⇒ 该段永不被 `Tick`、`current_order` 永远 `OT_NOTHING` ⇒ 机车侧 `reject=target-not-wait` 永久拒绝，即玩家看到的「库里第二次挂不上」）。已修（`DecoupleTrain` + `R3RPromoteFreeWagonChainToFront` 各补 `InvalidateVehicleTickCaches()`），增量重编 `EXITCODE=0`（`openttd.exe` 21:06:19），**待玩家复测**。**第 31 轮**：玩家提问「A/B 两地去 C，C 连挂后共同去 D，客货流怎么算」⇒ 只做代码调研、未改代码，结论登记为 KI-70（CargoDist 是逐跳站对模型、不认连挂；连挂经「链头 orders / 整列容量 / 谁在跑这条腿」三处影响货运；非链头段不刷自己的腿 ⇒ 上游边衰减消失）。**第 32 轮**：玩家拍板「方案 A 按段刷新 + 运费按段结算」⇒ 落地 `LinkRefresher::RunPerSegment`（KI-70）+ `CargoPayment` 按段入账（KI-71），另开 KI-72 记开放点；改动涉及 `src/linkgraph/refresh.h` / `src/economy_base.h`（**两个头文件**）⇒ 按 KI-15/KI-16 规则走 `build-release` 删全部 `*.obj` 全量重编，**待重编完成 + 玩家实测**。**第 33 轮**：Release 全量重编**已完成**（`build-release\openttd.exe` @ 04:08:50，22 735 360 B，691 步，`EXIT_CODE=0`；日志 `build-release\R3R_release_build.log`，证据存档 `openttd_r33_full.exe` / `R3R_release_build_r33_full.log`；闸门 `[5c]` 通过 = `WITH_ZLIB/LIBLZMA/ZSTD/LZO/PNG/OPUSFILE` 全在，无 KI-25 退化）。同时从重编日志中发现 **`train_cmd.cpp:4474` C4150**（KI-69 兜底路径 `delete` 不完整类型 ⇒ `~CargoPayment()` 不执行 ⇒ 悬垂 + 池泄漏 + 运费不结算）⇒ 已加 `#include "economy_base.h"`，登记 **KI-73**，并增量重编。玩家实测清单见 `R3R_per_segment_linkgraph_memo.md` §5。 |

**重要**：2026-09-14 曾有「回退到 `f3ebaad870` 09-09 基线」的动作，**已于 2026-09-15 01:12 整体撤销**（工作树恢复为 `2db952d332`，即 09-10~09-14 的 WIP：路线 A 排程借用 + artic 角色快照 + `r3r_perf` 探针）。因此历史记录里「回退后 = 未修」的字样**都不代表当前代码**。

---

## 二、KI 总表（权威口径）

| ID | 问题 | 来源 | 状态 | 严重度 |
|---|---|---|---|---|
| KI-01 | 路线 A 借用关系（`orders_backup*` / `r3r_orders_borrowed`）**全 NOSAVE**：连挂状态存档 → 读档 → 解挂，链头取不回自己那条排程 | §14.6(2) / 35644846 | **已修（2026-09-14 回归排查轮，随 `2db952d332` 保留；待复测）**。`src/sl/` 零命中证实全 NOSAVE；读档后由 `R3RRebuildCouplePriorities()`（`train_cmd.cpp:3883`，入口 `AfterLoadVehiclesPhase2`，`vehicle_sl.cpp:498`）从**仍存活的 `orders` 指针**反推所有者（链头 `orders` == 某非链头段的 `orders` ⇒ 该段即 owner），重建 `r3r_priority` 与 `r3r_orders_borrowed`。链头自己被停放的排程无法恢复（存档时已无引用、未被写出），故 `orders_backup` 保持 `nullptr`，并在 `R3RSyncDrivingOrders`（`train_cmd.cpp:3930`）归还分支加空备份保护。**待复测**：带「连挂状态」存档 → 读档 → 解挂，确认交接不丢、`COUPLE-FAIL` 不刷屏 | 高 |
| KI-02 | `orders_backup` 单层无栈：连挂不先解挂再挂 B 会覆盖并丢失机车自有 `OrderList` | 18491399 / 35644846 | **部分防护**：读档路径已不会因 `orders_backup == nullptr` 误清链头排程；**同一会话内「挂了 A 不解挂再挂 B」仍会覆盖**（单层指针未改栈，属原设计遗留，修法待拍板） | 高 |
| KI-03 | 借用不注册为共享链（`IsOrderListShared() == false`）：`DeleteVehicleOrders` 已加归还守卫，但 `OrderList::DeleteOrder` 删到空表 / 清空排程按钮等路径未逐一核 | §14.6(1) | 部分防护 | 中 |
| KI-04 | 借用期间非驱动段 `cur_real_order_index` 冻结，仅靠「主人继承进度」补回；中间态进度显示可能不准 | §12.5 / §14.6(3) | 未修 | 低 |
| KI-05 | DECOUPLE 触发点与配置不符（旧日志在 42,28 触发）待复测；解挂后机车 `GOTO_COUPLE` 找不到车底 ⇒ `COUPLE-FAIL` 每 tick 刷屏 | 31435600 / 18491399 | **部分防护（2026-09-15 第 15 轮口径收窄）**。九月的 `yapf_rail.cpp:150` 自身豁免（depot 锁死修复）与 `reach()` / `rp()` 的 `cur != st` 放行**逐行核对仍在、未被性能支线破坏**。第 15 轮实测 3 次 `COUPLE-FAIL` 全部为瞬态并自行恢复，无刷屏、无锁死；但「目的地没有车底」这一机制仍在（归 KI-34）。**仍待复测**：(a) 带连挂状态存档 → 读档 → 解挂；(b) DECOUPLE 触发点是否仍在 42,28 | 中 |
| KI-06 | artic 端点角色错位：命中判据「机车鼻 ↔ 车底尾」vs 拼接判据「机车尾 ↔ 车底头」⇒ FOLDCHK 反复回滚、下 tick 重试的无限循环 | 79051681 / 21823468 / 92405143 | **已修（2026-09-15 第 15 轮实证）**。第 12 轮「只看 direction」判据实测**无效**（`FOLDCHK-DIR` 44 次全部判折叠 ⇒ 三候选全被拒 ⇒ 每 tick 回滚）。根因：该谓词既不区分 artic 父子对（`dot=+4`），对「整链被统一翻向」也无 flip-invariance。**P4**（`R3RCheckChainFoldedDirection` 跳过 `b->IsArticGroupMember()` 的块内对）+ **P1**（候选 2 结构守卫不再提前 `return false`，改为落到候选 3 实测）落地后，`test4.sav` 实测：`FOLDCHK-DIR` 44 → 2、`FOLDCHK-ACCEPT` 0 → 1、候选 2 首次 `ACCEPT`、`COUPLE-OK` 出现、`CPL-ENTRY` 28 → 1（每 tick 回滚循环消失）、KI-33 未复现。第 15 轮 5 会话约 24 分钟长程复测：3 处真错翻全被 `SPLICE-GAP-REJECT` 拦下，候选 3 一次成功，`CPL-ENTRY` 仅 4 次 | 高 |
| KI-07 | artic 组「假爸爸身份」换向仅为设想（FOLDCHK 只看位置 + direction，改标记无效） | 65693747 | 待实现 | 低 |
| KI-08 | 不成段车厢链未拒绝 `couple` / `uncouple`（解挂侧仍走原生逐车启发式） | 17190446 / 72616043 | 待实现 | 中 |
| KI-09 | consist 概念层拆除残留：`IsConsistGroup` 全部使用点（yapf / 寻路 / waiter / depot 列表 / `engine.cpp:900`）未换等价谓词 | 50765346 | **作废**：`IsConsistGroup` 全代码库 0 命中，consist 概念层已彻底拆除 | — |

| KI-10 | newGRF position-in-segment（`PositionHelper` 段内计数）未验证；fold-fix 时「几何头 ≠ 身份头」，图像可能仍不符 | 96042581 | 未验证 | 低 |
| KI-11 | 逻辑翻 v / 换端重排（多节真引擎、只翻 u、双翻、身份迁新链头）仅编译验证，缺交互回归 | 30805247 | 未验证 | 中 |
| KI-12 | depot 出发时单次 `COUPLE-FAIL` 后自恢复 | 83488206 | 待复测 | 低 |
| KI-13 | 车站就地掉头「优先向前跑」 | 55558532 问题2 | 已搁置（用户拍板不做） | 低 |
| KI-14 | 帧率下降 / 延迟增加（探针洪水 + `PositionHelper` 图形路径 + 长链高频遍历） | 用户 09-12；探针 `src/r3r_perf.h` | **已搁置（2026-09-14 用户拍板降为「低」）**。①探针洪水已修（闸门 + 收编 `fopen`）；②③未验证部分随性能维度一并搁置。真正成本经第 9/10/11 轮定位为**原生逐车移动 × 存档规模**（`ctrl ≈ 移动车数 × 链长 × 1.9 µs`；42 555 车 / 911 链 / 链均 46.7 节），R3R 自有桶（`plat+resv+edgeGate`）仅 ~9~15 %。详见 §五-1 | 低 |
| KI-15 | 改任意 `src/*.h` 不触发增量重编（ninja `deps=msvc` 解析不到中文 `注意: 包含文件:`）→ 必须全量重编，否则混合 obj 随机崩溃 | 66636022 / 52814982 | 未修（工具链须知，只能遵守；改 `src/*.h` 后删全部 `*.obj` 全量重编，约 25~40 min） | 高 |
| KI-16 | 改语言 txt 后 `strings.cpp` 可能不重编 → 启动报 "No available language packs (invalid versions?)" | 52814982 | 未修（同上；强制重编 `strings.cpp` 后再链接） | 低 |
| KI-17 | 探针计费不完整：多数裸 `fopen("R3R_debug.log")` 站点未走 `R3RDbgWrite`，`dbgWrite=0ms/s` 不能当作「探针不贵」的证据 | 本轮复盘 §4 | 部分防护（第 6 轮 + 09-14 收编：9 处裸 `fopen` 已进 `r3r_perf.h`；余下冷路径保留）。**读账本必须先去掉嵌套**：`resv ⊂ plat`、`coll ⊂ ctrl`、`ctrl/spd/vp/plat/coupleH ⊂ loco`，占比时不得把 `loco` 与子桶相加 | 低 |
| KI-18 | `DEPOT-ARR` 每 16 行一次的链快照 + 未接闸门的 `CRT`，是闸门后残余日志体积主源（约 65 %） | 第 2 / 6 / 7 轮实测 | 部分防护（`fopen` 已移到签名闸门之后，只省 syscall 不减体积）；体积再降需给 `CHAIN` 子行加采样开关（用户未要求，暂留） | 低 |
| KI-19 | 同存档 A/B：vanilla `GL trains` 8~9 ms vs R3R 40~88 ms | 用户 09-13；KI-14 第 11 轮；KI-20 第 5 轮 | **已搁置（根因已改判）**：第 5b/5c 轮证伪「车厢链被提升为列车」归因——vanilla 自数 907 台前车全为真引擎，与 R3R 911 条链几乎相同（入口数相同）。差值在 handler 内部，即「每 tick 站台重预留」+ 原生整链遍历，详见 §五-2 | 低 |
| KI-20 | KI-19 的对照未钉死（是否同一存档、同一运行状态） | 用户 09-13 要求再做一次 A/B | **已修（第 5b/5c 轮复测并改判）**：vanilla 9.14 ms / R3R 88.07 ms，入口数 907 vs 911，差值在 handler 内部；取数程序 `R3R_AB_probe.cmd` / `.ps1` 见 §六 | 低 |
| KI-21 | A/B 取数程序 `data_R3R.txt` 解析失败（`-ExeDir` 尾反斜杠被 CRT 当转义引号等一串坑） | 2026-09-13 第 1 轮 | 已修（去尾反斜杠 + `Clean-Path` 兜底 + stderr 分流 + 报告 GBK + `window_quality` 回退，端到端复跑通过） | 低 |
| KI-22 | 截图无法直接读取（本模型无视觉能力，`read_file` 打不开 `.png`） | 2026-09-13 | 部分防护（`R3R_shot_ocr.ps1` 走 Windows.Media.Ocr 兜底；小字号数字误识率高，**只可定性、不可取数**） | 低 |
| KI-23 | 性能调查基础失效：R3R exe 是 **Debug（未优化）** 构建（`/Od /Ob0 /RTC1 -MTd -D_DEBUG`，断言全开），而对照的官方 JGRPP 0.72.4 是 Release（`/O2 /Ob2 /DNDEBUG`）→ 一切 A/B 倍率都建立在「未优化 vs 优化」上 | 09-13 实测 `build.ninja`；用户 09-13 澄清 | 已修（第 13 轮另建 `build-release`：`RelWithDebInfo` + 强制 `/O2 /Ob2`，`NINJA_EXIT=0`）；**结论**：Debug vs Release 在这种重循环虚调用密集代码上 5~20× 属常态，与是否使用 R3R 功能无关 | 低 |
| KI-24 | 本机 8 GB 内存不足，`ninja -j 4` 编译 `/O2` 时构建在 546/978 处完全冻结（4 个 `cl.exe` 被换出物理内存） | 09-13 实测（cl CPU 冻结 + PerfOS 采样） | 已修（降到 `-j 2`；`TEMP`/`TMP` 指向 D 盘，避免 LTCG 临时文件爆掉仅剩 0.7 GB 的 C 盘） | 低 |
| KI-25 | `build-release` 静默丢掉全部外部库（漏 `-DCMAKE_TOOLCHAIN_FILE=…/vcpkg.cmake`）→ 任何压缩存档都读不了（`loader for 'zstd'/'lzma' is not available`），截图 PNG 与 OpusFile 音乐一并失效 | 用户 09-13 实测报错 | 已修（脚本加工具链文件 + `[4b]` 缓存修复 + `[5c]` 硬闸门缺任一 `WITH_*` 即 `exit 92`，坏 exe 不再可能产出） | 低 |
| KI-26 | `resid` 未达 <1 ms/帧；「列车延迟降到 1.1×」缺一个可比的 Release 口径，且探针在 Release 里仍全开 | 09-14 实测（`build-release/R3R_perf.log`） | 已搁置（随性能维度搁置，见 §三-1） | 低 |
| KI-27 | 探针口径偏差：`ctrl` 也会在换端辅助函数中运行（`sub` 可略超 `loco`），`resid` 只能当「未解释部分的上界」读 | 09-14 步骤 2 | 部分防护（已文档化） | 低 |
| KI-28 | 探针开关只有一层时无法做运行时 A/B | 用户 09-14 拍板 | 已修（拆两层：`R3RDbgOn()` 管日志、`R3RPerfOn()` 管计时器；默认按构建类型自动推导——Debug=ON / Release=OFF；运行时 `R3R_DBG=0` / `R3R_PERF=1` 可覆盖） | 低 |
| KI-29 | `build-release` 全量重编被自身闸门拦死（`EXIT_CODE=94`）：`CMakeLists.txt` 从未引用 `R3R_PROBES_DEFAULT` | 09-14；`R3R_NEXT_STEPS.md` §6 | 已修（`CMakeLists.txt` 新增 `add_definitions(-DR3R_PROBES_DEFAULT=…)`，正反向闸门均验证 OK） | 低 |
| KI-30 | `R3R_fullrebuild.cmd` target 2「跑不了」：`:DBG` 分支无日志；致命的是批处理解析错误——`echo ... (KI-28).` 里的 `)` 提前闭合 `if (...)` 块 → `. was unexpected at this time.` 终止整个批处理，ninja 从未启动 | 用户报「target 2 跑不了」 | 已修（两阶段：外层预检 + `__go` 子进程重定向日志/退出码；去掉 `echo` 文本里的括号；`EXIT_CODE=%RC%>>` 误当句柄重定向改为前置重定向）。**已端到端跑通**：`[687/687]`、`NINJA_EXIT=0`、`error C`/`error LNK` 零命中 | 中 |
| KI-31 | `R3R_fullrebuild.cmd` 双击 `[2]` 会静默跑去编 release：子进程识别绑在 `%MODE%` 上，而走菜单时 `%MODE%` 为空 → 子进程落入 `:MENU`，`set /p` 读不到输入 → `SEL` 空 → 默认执行 TARGET 1（且提示全写进日志、窗口无声挂住） | 用户 09-14 晚报 | 已修（`__go` 改为第一个参数并置顶短路；外层去掉 `%MODE%` 并给子进程 stdin 接 NUL；新增「子进程落地校验」`findstr "child process"`；TARGET 1 结果段改为真读 done 文件）。临时副本桩测 + 反向实验 1:1 复现均通过 | 中 |
| KI-32 | 09-10 崩溃修复随「回退」丢失，该崩溃重新生效：`CmdDemoteSegment` → `R3RDestroyCarOnlyFormation` 清掉 primary 身份却**不关 `VehicleView` 窗口**；`Vehicle::PreDestructor` 只在 primary 时关窗 → 卖出该车后窗口视口仍跟随旧 index → `UpdateNextViewportPosition` 空指针 → 读 `vehicle_flags`（偏移 0x30）崩溃 | 记忆 38203048；本轮回退核验 | **已修（09-15 复核：随 `2db952d332` 恢复而回归）**：四件套均在树内（`train_cmd.cpp:1767`、`train_cmd.cpp:5048`、`train_cmd.cpp:10642`、`vehicle.cpp:1221/1224`）。**规则**：R3R 任何「取消 primary 身份」的路径都必须同步关闭该车的 `VehicleView` 等窗口 | 高 |
| KI-33 | `test4.sav` 复测中 `TrainController` 断言崩溃：`src\train_cmd.cpp:8627` 的 `assert(chosen_track & (bits \| GetReservedTrackbits(gp.new_tile)))`，现场 `Train 1 c:0 st:FE vs:Ds trk:0x02` @ `39,33`（即 KI-06 的耦合点）；崩前是每 tick `CPL-*` 死循环 | 09-15 `test4.sav` 复测；`crash-20260914T180049Z.log` | 已修（第 14 轮把断言源头消除：`exitdir` 非法时取直行 `chosen_track`，过不去就原地不动；**第 15 轮实测确认未复现**——5 会话约 24 分钟无 assert、无 crash）。判定为 KI-06 死循环的下游表征，随折叠修复一并消失 | 高 |
| KI-34 | 解挂后机车按自己的排程回 depot 执行 `GOTO_COUPLE`，而车底已留在别处 → 反复 `COUPLE-FAIL`。与 KI-05 的区别：KI-05 是 yapf 自身占用 tile 被当障碍（depot 锁死、无路径，已修）；本条**路径能走通、单纯目的地没有车底**，属解挂后排程归属 | 09-15 第 13~15 轮；`R3R_debug.log` L256/276/368-415 | 部分防护（口径收窄）：第 14 轮已改解挂恢复点按「谁拥有排程」判定（`u_owns_running`，须在 `R3RSyncDrivingOrders(v)` 之前取）；第 15 轮实测 3 次 `COUPLE-FAIL` 全为瞬态并自行恢复，无刷屏无锁死；但「目的地没有车底」这一机制仍在（KI-01/02/03/05 同族） | 中 |
| KI-35 | 候选3 末位兜底接受非相邻拼接：`SPLICE-GAP-LAST-RESORT` 命中时合并链在收拢前瞬时违反「相邻瓦片」不变式，链上可能留下 >1 瓦片间隙 | 09-15 第 14 轮（`train_cmd.cpp:5467-5477`） | 部分防护（`TrainController` 断言已在源头消除 → 表现为「直行/原地」而非崩溃）；第 15 轮该路径**未被激活**（`SPLICE-GAP-LAST-RESORT=0`），收拢过程待长程复测 | 中 |
| KI-36 | `SpliceFolded` 只看拼接对：容差 8px 与旧整链判据同值；artic 拼接对 `exp=2~5 / dist=0` 落在容差内属预期，但真实错位 ≤8px 的拼接对仍会被接受（方向判据仍是硬否决） | 09-15 第 14 轮（`train_cmd.cpp:5232-5246`） | 部分防护（`dist=19` 已能拦；第 15 轮无 ≤8px 边界错位样本） | 低 |
| KI-37 | 段右边界标记 `SegmentBack`（`VehicleRailFlag` bit15）新落地，depot 拖动 / 拼接语义随之改变；**未实测**（旧存档兼容、与真 artic 链混用、与 fold-fix 回滚交互均未验证） | 2026-09-15 第 16 轮 | 未验证 | 中 |
| KI-38 | `R3RFlipChainBySegments` 段内反转时右边界迁移按「单辆车」判定（`new_front == s.blocks.back().front()`），而标记实际落在段尾**块尾车**上，artic 块时二者不是同一辆车 → 迁移不生效，标记遗留在新段首块内部 | 2026-09-15 第 16 轮（`train_cmd.cpp:5095`） | 已修（第 16 轮：改判 `s.blocks.back().back()` → 迁到 `s.blocks.front().back()`；全为单辆块时与旧实现逐字节等价），待复测 | 中 |
| KI-39 | `DecoupleTrain` 只 `u->ClearSegmentFront()` 清解出链链头 ★（`train_cmd.cpp:4587`），不清解出链链尾的 `SegmentBack` → 单个段解出后留下「孤儿右边界标记」（无 ★ 配对） | 2026-09-15 第 16 轮 | 未验证（按当前代码推理，`TrainDepotGetSegmentFront` 对这类链一律返回 `nullptr`，等同「不是段」；与 depot 拖动 / 拼接分支的交互未实测） | 低 |
| KI-40 | 换端重排 `R3RRelocateFrontIdentity` → `CopyVehicleConfigAndStatistics` 复制了 `service_interval`，但**不复制** `date_of_last_service`（`reliability` / `max_age` / `build_year` / `breakdown_*` 也一律不迁）→ 新链头拿「自己的上次服役日期」配「原链头的服役间隔」，`NeedsServicing()` 时机出现一次跳变 | 2026-09-15 第 16 轮（任务 1 数据继承审计，见 `R3R_data_inheritance_audit_memo.md` §2.2） | 未修 | 低 |
| KI-41 | 换端重排后旧链头对象残留 `profit_this_year` / `profit_last_year`（未清零，`vehicle_base.h:911-914`）→ 年度结算走 `IterateFrontOnly` 故当前无害，但该对象日后再次成为链头并再迁身份时会把陈旧利润**重复计入** | 2026-09-15 第 16 轮（同上 §2.3） | 未修 | 低 |
| KI-42 | depot 错误提示把 `VehicleType` 枚举值当字符串偏移硬编码（`STR_ERROR_CAN_T_BUY_TRAIN + to_underlying(...)`）——枚举顺序 / 字符串相邻性一旦变动即**静默显示错误文案** | 2026-09-15 第 17 轮（`depot_gui.cpp`） | **已修（第 17 轮）**：6 处全部改用 JGRPP 的 `GetCmdBuildVehMsg(v)` / `GetCmdBuildVehMsg(*begin)`（`vehicle_func.h`，按 `VehicleType` 显式映射，不依赖枚举顺序与字符串相邻） | 低 |
| KI-43 | depot 列车列表「链状态」标记列（`STR_DEPOT_CHAIN_LOOSE` 散链 / `STR_DEPOT_CHAIN_SEGMENTS` 段：N），第 19 轮在其上**加了第二行**（组名 + `k/N` 归属，见 KI-50） | 2026-09-15 第 17 轮（任务 2），第 19 轮扩展（步骤 6） | 未验证（编译 + lint 通过；RTL 布局、超长本地化文案截断、非列车车库回归未测；第 19 轮新增：**列车行高是否真的容得下两行 small 字体**（已把列车 `min_height` 提到 `matrix.top + 2×small`，未实测）、超长组名截断后的观感） | 低 |
| KI-44 | depot 拖动「段内任意一节车厢」时整段统一高亮：`DrawTrainImage` 高亮帧与 `HighlightDragPosition` 落点预览宽度改以 `SegmentBack` 为右边界（无标记时兜底「下一段首 / 链尾」，与 `TrainDepotDetachSegment` 同规则），depot 侧改以段首 ★ 作为被框选对象，`CountDraggedLength` 按整段计长，`OnCTRLStateChange` 不再因松开 ctrl 缩短段拖动 —— **未经游戏内实测** | 2026-09-15 第 17 轮（任务 3/4） | 未验证（编译 + lint 通过；拖动跨度、与 `_cursor.vehchain` 的交互未测；段右边界截断对**其它** `DrawTrainImage` 调用方——`vehicle_gui.cpp` 车辆列表、虚拟列车模板窗口——同样生效，属预期但未验证） | 中 |
| KI-45 | 新建独立「挂接分组」体系（独立池 + 专用窗口 + 存档 + 联机命令）作为挂接**硬约束白名单**：同组才允许挂接，不同组即使贴上也**视同「没有等待挂接的列车」**（原地等待、不刷屏） | 2026-09-15 第 18 轮（用户第二轮拍板 q-1/q-2），备忘 `R3R_couple_group_design_memo.md` §3~§7/§9 | **代码完成（步骤 2~6 全部落地，编译 + 链接 + lint 通过，第 18/19 轮，见备忘 §12 / §13）**：数据底座 + `CGPP`/`CGVR` 存档 + 4 条联机命令 + 专用窗口 + E1/E2/E4/E5/E6 硬约束接线（E3/E7 确认被覆盖，无需改）+ 可见性（车辆窗口 / depot / 分组窗口）。**行为变更仅在「至少一方已显式指派组」时生效**（未指派时 `INVALID == INVALID` 恒真 ⇒ 老存档零变化）。**第 25~26 轮补强**：寻路侧统一谓词 `R3RIsCoupleTarget`（`train.h` / `train_cmd.cpp:1826`）+ 到达侧统一闸门 `R3RCanCoupleNow`（`train_cmd.cpp:5915`，四条件）已接线（详见 KI-60/61/62）。**步骤 7 收尾**：全量重编 + 源码/构建一致性核对已完成（exe @ 19:23:23，691 步 `EXIT_CODE=0`，无陈旧 / 混合对象，见 §一）；**游戏内实测仍待玩家**（清单见 `R3R_couple_group_design_memo.md` §14.4） | 中 |
| KI-49 | 步骤 6 可见性三项（车辆窗口两行「挂接分组 / 命令归属」、depot 第二行、分组窗口归属标记）**全部未经游戏内实测** | 2026-09-16 第 19 轮（步骤 6） | 未验证（编译 + 链接 + lint 通过；待测：车辆窗口新增行是否与既有逐行显示开关（`vehicle_*_line_shown`/`ReInit`）联动正常、单段未指派链是否**零显示**、depot 两行是否溢出、分组窗口标记是否被右边界截断、**组改名变长后车辆窗口宽度不会自动重算**（按上游 `STR_VEHICLE_INFO_GROUP` 同款做法，用**实际组名**算宽，改名变长后需重开窗口）） | 低 |
| KI-50 | depot 链状态列**口径变更 + 组名截断策略**：为让第二行 `k/N` 与第一行同口径，「段：N」的 N 由**★ 个数**（链头段之外的段数）改为**链中全部段数（含链头段）**；组名超宽时在整 UTF-8 字符处截断并加「..」 | 2026-09-16 第 19 轮（步骤 6，`depot_gui.cpp` `DrawChainStateTag`/`ShortenCoupleGroupName`） | 未验证 + **待玩家拍板**（旧显示「段：1」现在显示「段：2」；「散链」判据不变。理由：必须与车辆窗口「共 N 段」一致，否则同一条链两处数字不同） | 低 |
| KI-51 | 玩家报「看不到挂接分组 UI」+「无法升级为段」两问：经查**命令层无故障**——`build\R3R_debug.log` 中 `MAKESEG-CMD … exec=1` 28 次、`UPGRADE-BEFORE-SPLIT`→`AFTER-SPLIT` 的 `SF` 由 0→1 共 17 次（另 11 次为重复点同一条已是段的链）；真因是**玩家当时运行的 exe 早于界面定稿**：`src/widgets/depot_widget.h` 01:43 / `src/depot_gui.cpp` 01:51 / `src/couple_group_gui.cpp` 01:52 / 双语 `lang/*.txt` 01:51 全部晚于该次会话（日志止于 01:23:56），语言包 02:01 重生成，`build\openttd.exe` **02:28:03** 才完成全量重编（691 步，`NINJA_EXIT=0`） | 2026-09-16 第 19 轮收尾（诊断；第 2 次收尾加 2 条入口探针，见下） | **待复测**（须用 `build\openttd.exe` @ **2026-09-16 04:08:54**（含 `CG-SHOW`/`CG-WIN` 探针）重启复验：列车车库窗口**最底行**是否出现 `设为段`/`降级`/`挂接分组` 三按钮；先点按钮再点车是否打上 ★；行首链状态列是否显示「段：N」、第二行 `组名 k/N`）。**教训**：玩家报 UI 类问题先比对「日志 mtime vs 相关源码 mtime vs exe mtime」，再怀疑代码。**第 2 次收尾补证（2026-09-16 04:0x）**：① 玩家 03:17 会话所用 02:28 exe 的 `build\R3R_perf.log`（止于 03:23）末行 `chains=3 vehs=18 maxChain=9 segs=3` ⇒ 三条链各带一个 ★，**「设为段」在该 exe 上确实生效**（`R3R_debug.log` 中 `MAKESEG-CMD` 64 行 = 32 次命令、`UPGRADE-BEFORE-SPLIT` 32 次、无一次拒绝）⇒「无法升级为段」属**可见性/语义**问题（KI-50 口径变更 + KI-52 作用对象语义），非命令故障；② 为把「按钮没点到 / 窗口没建出来 / 窗口已存在」三类失败区分开，`couple_group_gui.cpp` 加 2 条探针：`ShowCoupleGroupWindow()` 首行写 `CG-SHOW req owner=.. valid=.. local=..`（每次请求都写），窗口构造首行写 `CG-WIN open arg=.. arg_valid=.. owner=.. owner_valid=.. local=..`（真正建窗口才写）⇒ 点按钮后两行皆无 = 按钮/点击链路问题，只有 `CG-SHOW` = `BringWindowToFrontById` 命中（窗口已存在或被前置）；③ 已实测 exe 内含这两条字符串（`findstr /C:"CG-SHOW"` / `CG-WIN`）。复测仍需确认：depot 最底行三按钮、车辆窗口「挂接分组 / 命令归属：第 k 段 / 共 N 段」两行、失败时 `R3R_debug.log` 的 `CG-*` 行 | 低 |
| KI-52 | 「设为段」的**作用对象语义待玩家拍板**：`CmdMakeSegment`（`vehicle_cmd.cpp:545`）无条件 `while (t->Previous() != nullptr) t = t->Previous();` 回溯到**整条物理链的链头**再打 ★（注释即 "Resolve the clicked vehicle to its independent chain front"），故玩家点链中间某节车厢时，段标记落在**链头**而非被点的那节（易被误读为"没升级成功"） | 2026-09-16 第 19 轮（诊断自日志 `UPGRADE-AFTER-SPLIT` 明细 + 复读源码） | **待玩家拍板**（若期望"以点击处为界把后半段独立成段"，属新语义，需另立步骤） | 低 |
| KI-46 | **跨公司挂接接口预留**未实现（用户追加要求「务必留好接口」）：分组带 `Owner`、匹配判据单点 `R3RCouplePairAllowed`、权限单点 `CoupleGroupIsManageable`、**段归属公司（`Vehicle::owner`）与组归属公司分离**、`flags` 预留位、窗口可见性单点 —— 实现时若绕过任一点，将来跨公司改造须动数据模型 + E1~E7 七个调用点 | 2026-09-15 第 18 轮，备忘 §10 | **部分完成**：`Owner owner` + `flags`（`CGF_SHARED`）+ `R3RCoupleGroupIsManageable` + `R3RCoupleGroupIsVisibleTo` 已在数据底座落地；段公司（`Vehicle::owner`）与组公司分离已在命令里分开读；**（第 26 轮复核更新）匹配判定已收敛为单点 `R3RCoupleAllowed`**（声明 `couple_group.h:109` / 定义 `couple_group.cpp:162`，内部 `R3RCoupleGroupMasksCompatible` 交集判据在 `couple_group.cpp:115`），**已接线 5 处** —— `yapf_destrail.hpp:437`(E1)、`yapf_rail.cpp:170`(E2)、`train_cmd.cpp:5978`(E4，经 `R3RCanCoupleNow`)、`train_cmd.cpp:6047`(E5)、`train_cmd.cpp:6077`(E6)；设计稿里的旧名 `R3RCouplePairAllowed` 全代码库 **0 命中**（已并入 `R3RCoupleAllowed`），日后跨公司 / 跨组策略只改这一个内部 | 低 |
| KI-47 | 挂接组字段挂在段头车（★）上时，必须在**段头身份迁移点同步搬迁**：`R3RRelocateFrontIdentity`、`R3RFlipChainBySegments` 的 ★/假引擎身份迁移、`CmdMakeSegment`/`CmdDemoteSegment`、`Couple()` 布 ★、`DecoupleTrain` 清 ★ —— 漏任一处 → 组归属漂移到错误车辆（与 KI-38 同族） | 2026-09-15 第 18 轮，备忘 §4.4 | **已修（第 27 轮复核定性：设计免疫，无需在迁移点显式搬迁）**。第 20 轮 S4 已把单值 `Vehicle::couple_group` 换成位掩码 `couple_groups`，配套两条规则：**读** = 从 `R3RGetCoupleGroupCarrier(v)` 起沿 `R3RNextSegmentVehicle` 扫**整段**取并集（`couple_group.cpp:125`），**写** = 先 `R3RNormaliseCoupleGroupsOfSegment`（`couple_group.cpp:84`，段内并集归位到 carrier）再改位。因此「段头身份迁移」（`R3RRelocateFrontIdentity` / `R3RFlipChainBySegments` 的 ★ 迁移 / `CmdMakeSegment` / `CmdDemoteSegment` / `Couple` 布 ★ / `DecoupleTrain` 清 ★）**只需保证段的车集不变**即可——掩码留在段内任意车上都能被读到，漏迁不会丢归属（`R3RFlipChainBySegments` 恰是「段内倒序、车集不变」，`MakeSegment` 切分只把段变小、掩码所在车必落在其中一段）。组删除另有 `R3RUnassignCoupleGroup`（`couple_group.cpp:271`）**全库清位**兜底。**残留边界（低）**：掩码可能停在段内非 carrier 车上，若该车被**单独售出**（`DeleteVehicle`），其携带的组归属随之消失；彻底消除需在切段/删除点也做一次归位（低成本修法：导出 `R3RNormaliseCoupleGroupsOfSegment` 并在 `CmdMakeSegment`/`CmdDemoteSegment` 前后调用），**因需改 `src/*.h` 触发 25~40 min 全量重编，且严重度低，未做，登记为 KI-64** | 中 |
| KI-48 | 挂接组 ID 若照抄 `r3r_priority`/`r3r_orders_borrowed` 的 NOSAVE 做法，读档后组归属全丢（KI-01 同族）；必须写入存档（Vehicle 描述表 + 新分组池 chunk），并在读档时校验「池已删的失效 ID → 置 Invalid」 | 2026-09-15 第 18 轮，备忘 §4.3 | **已落地（第 18 轮步骤 2）**：`src/sl/couple_group_sl.cpp` 双 chunk —— `CGPP`（`CH_TABLE`，`NamedSaveLoad` 表 name/owner/flags，池项）+ `CGVR`（`CH_SPARSE_TABLE`，仅存「有组且组有效」的段头车的 `Vehicle::couple_group`）；车辆表布局**零改动**（不污染上游表）；`Load_CGVR` 末尾调 `AfterLoadCoupleGroups()` 清悬空 ID。**游戏内存读档实测未做** | 中 |
| KI-53 | depot/车辆窗口显示 `(invalid parameter) k/N`：R3R 语言串误用 `{STRING}` 传**裸字符串**（`{STRING}` 要求 StringID 参数，裸串必须用 `{RAW_STRING}`），`FormatString` 取参抛 `out_of_range` 后被兜底成 `(invalid parameter)`。修法：`STR_DEPOT_CHAIN_GROUP_OWNER` / `STR_COUPLE_GROUP_LIST_ITEM` / `STR_COUPLE_GROUP_QUERY_DELETE` / `STR_VEHICLE_INFO_COUPLE_GROUP`（英/简各一份）4 处 `{STRING}`→`{RAW_STRING}` | 2026-09-16 第 20 轮（玩家反馈第 2 条）；备忘 `R3R_couple_group_memo_20260916.md` §2.1 | 已修（第 20 轮源码 4 处已改；**第 26 轮 19:23:23 的全量重编已让 `strings.cpp` 随新 `strings.h` 重编**，语言包版本同步 ⇒ `(invalid parameter)` 应消失），**待玩家复测** | 中 |
| KI-54 | 车库链状态第一行「段：N」的口径是**整条链的段数**（含链头段），不是「第几段」；玩家上次会话 `chains=2 vehs=9 maxChain=6 segs=2`（两条链各 1 个 ★ 且在链头）故全部显示「段：1」，属显示语义而非 bug | 2026-09-16 第 20 轮（玩家反馈第 2 条前半）；备忘 §2.1(b) | 已修（采用方案 A：第 20 轮 S3 第一行改「第 self/总 段」，新串 `STR_DEPOT_CHAIN_SEGMENT_POS` + `R3RGetSegmentPosition()`，`depot_gui.cpp` 宽度预算已同步），**待玩家复测** | 低 |
| KI-55 | 挂接分组窗口「新建分组」置灰、新建的分组不出现：真因 = `Window::InitializeData()`（`window.cpp:1602`，由 `FinishInitNested()` 调用）在**构造函数体之后**把 `Window::owner` 重置为 `INVALID_OWNER(255)` ⇒ `R3RCoupleGroupIsVisibleTo(cg, 255)` 恒假（左列表永远空）、`R3RCoupleGroupIsManageable` 恒假（按钮灰）。命令其实建组成功了，只是以 owner=255 的窗口看不到。 | 2026-09-16 第 20/21 轮（玩家反馈第 6 条）；探针 `CG-PAINT owner=255 owner_valid=0` 实证；上游惯例 `waypoint_gui.cpp:105`、`vehicle_gui.cpp:1059`、`timetable_gui.cpp:438` 均在 `FinishInitNested()` **之后**写 owner | 已修（第 21 轮）：新增成员 `CoupleGroupWindow::company`，构造函数只写该成员；新增 `OnInit()` 内 `this->owner = this->company;` 并 `RebuildGroups()`；`OnPaint` 的 `can_create` 用 `company` 兜底。**待复测** | 高 |
| KI-56 | 挂接分组窗口「指派段」按钮忽亮忽灰、点不动：`OnClick(WID_CG_GROUPS)` 点到列表下方空白会把 `selected_group` 清成 -1 ⇒ `can_manage=false` ⇒ `OnPaint` 每帧把各管理按钮置灰，点在灰帧上自然无反应（日志 `CG-PAINT groups=1 can_manage=1/0` 交替实证）。 | 2026-09-16 第 22 轮（玩家反馈第 7 条） | 部分防护（真因见 KI-58）：点列表空白改为保留当前选择；`IsSelectedGroupManageable()` 改用 `CoupleGroup::GetIfValid()` 容忍失效选择；探针 `CG-PAINT` 增打 `sel/sel_cg/sel_cg_owner/add_disabled/lowered`；另补：新建组后自动选中该组（列表按 id 升序，取最后一项），创建完即可直接「指派段」。**该修复未命中真因，见 KI-58** | 中 |
| KI-57 | 删除挂接分组会连带把挂接分组窗口一起关掉：`CmdDeleteCoupleGroup` 里 `CloseWindowById(WindowClass::CoupleGroup, group.base())` 把**组 ID** 当作窗口号，而该窗口是 `window_number=0` 的单例 ⇒ 删除第一个组（ID=0）即误关窗口。 | 2026-09-16 第 22 轮（玩家反馈第 7 条） | 已修：移除该 `CloseWindowById`，仅保留 `InvalidateWindowClassesData(WindowClass::CoupleGroup, 0)` 刷新列表。**待复测** | 中 |
| KI-58 | 挂接分组窗口左侧组列表在「1 个组」与「0 个组」之间逐帧横跳 ⇒「指派段」等管理按钮随之忽亮忽灰、点不动（点击落在灰帧上时，Disabled widget 的点击会被引擎直接丢弃）。真因：`RebuildGroups()` 用 `Window::owner` 做 `R3RCoupleGroupIsVisibleTo` 过滤，而组 owner=`_current_company`(0)，`OWNER_NONE=0x10`、`INVALID_OWNER=0xFF` ⇒ 只要 `Window::owner` 是 INVALID_OWNER，**所有组**都被滤光（groups=0）；`IsSelectedGroupManageable()` 用同一字段，同病。日志 `CG-PAINT` 中 `can_manage=1/0`、`groups=1/0`、`sel=0/-1` 逐帧交替即此。 | 2026-09-16 第 23 轮（玩家反馈"指派段还是按不下去"）；与 KI-55 同源（都是误用 `Window::owner`） | 已修：`RebuildGroups()` 与 `IsSelectedGroupManageable()` 一律改用自有成员 `this->company`，不再读 `Window::owner`；新增 `CG-RBG` 边沿探针（记录 owner/company/iterated/visible）与 `CG-CLICK-ADD` 点击探针。**待复测** | 高 |
| KI-59 | 挂接分组窗口「指派段」按下去完全没反应（拾取模式永远启动不了），但可见性/启用状态都已正常（`CG-CLICK-ADD manage=1 groups=1`）。真因：该按钮是 `WWT_PUSHTXTBTN`，含 `WWB_PUSHBUTTON` 位（widget_type.h:106-109），`DispatchLeftClickEvent`（window.cpp:719）在调用 `OnClick` **之前**先 `HandleButtonClick()`→`LowerWidget()`，所以 `OnClick` 内 `IsWidgetLowered()` **恒为 true**；原实现用 `ToggleWidgetLoweredState()`（= `SetLowered(!IsLowered())`，window_gui.h:523）判断，于是每次都被翻成 false，永远走 `ResetObjectToPlace()` 分支。探针 `CG-CLICK-ADD ... lowered=1` 连续 35 次全为 1、无一次 0，即为实证。另有：`HandleButtonClick` 会 `SetTimeout()`，超时后 `RaiseButtons(true)` 会抬起全部 pushbutton，故 lowered 标志本身也不是可靠的「正在拾取」判据。 | 2026-09-16 第 24 轮（玩家反馈"还是不行欸"）；上游正确范式见 `departures_gui.cpp:617` 用 `_thd.GetCallbackWnd() == this` | 已修：`OnClick(WID_CG_ADD_SEGMENT)`、`OnVehicleSelect`、`OnPlaceObjectAbort` 全部改用引擎拾取状态 `_thd.GetCallbackWnd() == this` 判断（不再读 lowered 标志），探针改打印 `picking=`；另在 `OnPaint` 每帧按拾取状态 `SetWidgetLoweredState()` 重新压住按钮（抵消 `HandleButtonClick` 的点击超时后 `RaiseButtons(true)` 把按钮弹回），使按钮外观与真实拾取状态一致；`OnVehicleSelect` 增加 `CG-PICK veh=` 探针。**待复测** | 高 |
| KI-56 | 一个段**不能同时属于多个挂接分组**（现为单值 `Vehicle::couple_group`，再次指派会顶掉旧组）。需改集合语义：加入/移出幂等、白名单 `R3RCoupleGroupsAllowCouple` 改「交集非空即允许、交集为空视同无等待列车」、分组窗口与车辆/车库显示同步支持多组 | 2026-09-16 第 20 轮（玩家反馈第 5 条）；备忘 §2.3、§5.2 | 已修（第 20 轮 S4：`Vehicle::couple_groups` 位掩码 `uint64_t` 替代单值 `couple_group`，读=段内并集、写=normalise 到段首，白名单 `R3RCoupleGroupMasksCompatible` 交集非空即相容），**待玩家复测** | 中 |
| KI-57 | 挂接分组管理**未接入列车列表窗口**（当前入口只有 depot 工具行的「挂接分组」按钮 + 专用窗口）。目标：像列车分组那样在列车管理列表里管理；复用其 UI 形态但**不复用** `Group` 体系 | 2026-09-16 第 20 轮（玩家反馈第 4 条）；备忘 §2.3、§5.3 | 已修（第 20 轮 S5：`ADI_COUPLE_GROUP_MGMT` + `BuildActionDropdownList()` 在列车列表动作菜单追加「管理挂接分组」，`VehicleListWindow`/`CompanyGroupWindow` 均接），**待玩家复测** | 低 |
| KI-58 | 会话工作目录仍在 CodeBuddy 沙箱 `c:\Users\冯洁敏\CodeBuddy\20260914233818`，未迁到 `D:\sourcecode of JGRPP`；agent 无法切换 IDE 工作区，需玩家侧「打开文件夹」；本轮起所有读写已统一改为 `D:\sourcecode of JGRPP\` 绝对路径 | 2026-09-16 第 20 轮（玩家反馈第 7 条）；备忘 §0 | 部分完成（待玩家侧切换工作区） | 低 |
| KI-59 | 挂接分组存档字段升级带来**旧存档兼容代价**：`CGVR` 稀疏表字段由 `group`(SLE_UINT16) 改为 `groups`(SLE_UINT64)（`sl/couple_group_sl.cpp`，配合第 20 轮 S4 位掩码）。读旧存档**不会崩溃**，但旧字段名不被识别 ⇒ 所有段回落为「未分组」，需重新指派（本机测试存档可重建）。若日后要真正兼容需写 `SLE_FILE_` 条件读或另存新 chunk | 2026-09-16 第 20 轮 S4；备忘 §5.2 | 已接受（一次性代价，未做旧字段迁移） | 低 |
| KI-60 | **自动挂接缺少硬约束，出现两类不该发生的耦合**（玩家实测 `build/R3R_debug.log` 12:31）：①机车在车库直接耦合了**被玩家停住（stopped）**、持有 WAIT_COUPLE 的车底；②耦合了**同挂接分组、但没有等待挂接命令**的车底。真因：三条被动方目标解析路径（`GetCouplePosition`、depot 瓦片扫描、开阔轨道 8 邻域扫描）的判据都是 `R3RIsCarOnlyFormation(u) \|\| u->current_order.IsType(OT_WAIT_COUPLE)` —— 「纯车厢段」短路**整体豁免了 WAIT_COUPLE 要求**，且**三条路径都完全不检查停止/运行状态**；主动方在 `train_cmd.cpp:9892`（`Stopped && cur_speed == 0`）确实被挡，被动方却无人把关。日志现场：`FOLDCHK ... dist=0` → `COUPLE-OK loco=0 rear=8 consist=3 ... tx=38 ty=27`（车库同 tile 零距离直挂，不经寻路终点判据，故 `yapf_destrail.hpp` 侧的豁免不是唯一漏洞）。 | 2026-09-16 第 25 轮（玩家实测两场景后给出的期望：双方均须启动、一方 GOTO_COUPLE、一方 WAIT_COUPLE、挂接分组相同） | 已修：新增统一硬闸门 `R3RCanCoupleNow()`（`train_cmd.cpp`，置于 `GetCouplePosition` 之前）。四条件 = 主动方 `OT_GOTO_COUPLE` / 被动方 `OT_WAIT_COUPLE`（**取消 car-only 豁免，P7 语义收紧**）/ 双方 `!vehstatus.Test(VehState::Stopped)` / `R3RCoupleAllowed` 分组白名单。三条解析路径（site 标识 `"geo"`/`"depot"`/`"scan"`）全部改走该闸门；被拒候选等同「此处没有等待挂接的车底」（继续找别的候选，找不到就原地待命走常规 COUPLE-FAIL，不刷屏）。新增边沿触发探针 `CPL-GATE`（tag `R3REDGE_COUPLEGATE`），打印 `reject=` 原因与双方 order/stop/group 状态。**待复测**（**第 28 轮实测 · 会话 1**：条件② **通过** —— `CPL-GATE reject=target-not-wait site=depot act=0 tgt=3 aOrd=16 tOrd=0 grp=1`（日志行 37）拦下且同状态只 1 行、不刷屏，目标转为 `curType=17`(WAIT_COUPLE) 后立即 `COUPLE-OK loco=0 rear=8 consist=3 co=1`（行 58）；条件③ **通过** —— 车库内（行 58）与站台（行 266 `COUPLE-OK loco=2 rear=3 consist=3 co=1 real=4 tx=39 ty=31`）两次成功耦合，全程无死循环、无 depot 锁死；条件① **未触发**（会话内 0 条 `reject=target-stopped`），待补测。详见备忘 §14.4 / §15） | 高 |
| KI-61 | **寻路侧的「纯车厢段」豁免未同步收紧**（KI-60 的孪生遗留）：`yapf_destrail.hpp:370-371`（站台预留扫描）与 `:441`（`PfDetectDestination` 主判据 `if (R3RIsCarOnlyFormation(t)) return true;`）、`yapf_rail.cpp:152`（back-walk 安全位置 `fail=notWC`）仍把「纯车厢段」当作合法挂接目标。后果：机车仍可能把**没有 WAIT_COUPLE 的纯车厢段**规划为目的地并开过去，到跟前被 KI-60 的 `CPL-GATE` 拒绝 ⇒ 行为正确（不挂）但会白跑一趟 + 可能 COUPLE-FAIL。 | 2026-09-16 第 25 轮 KI-60 的连带项 | 已修（2026-09-16 第 26 轮）：四处全部改走统一谓词 `R3RIsCoupleTarget(t)` = `t->IsSegmentFront() && t->current_order.IsType(OT_WAIT_COUPLE)` —— 站台预留扫描、`PfDetectDestination`（旧 Target 1 的 car-only 短路已删除）、`yapf_rail.cpp` back-walk（`fail=notWC` → `fail=notSeg`，打印加 `sf=`）。因改了 `yapf_destrail.hpp`/`train.h`，已按记忆 66636022 做全量重编（删全部 obj）。**待复测**（**第 28 轮实测 · 会话 1**：**未触发** —— 全日志 0 条 `FSCP`、0 条 `reject=target-not-segment`，该会话没有「未升段的纯车厢链」场景；补测动作见备忘 §15.4(a)） | 中 |
| KI-62 | **耦合判定的「段身份」语义缺失**（玩家第 26 轮实测：车库内车底段被解挂后显示为散链，之后机车再也挂不上它）。两个互相咬合的缺陷：①`DecoupleTrain` 末尾 `u->ClearSegmentFront()`（`train_cmd.cpp:4589`）把解出段的段首标记 ★ 抹掉 —— 而 ★ 是全代码库判定「这条链是段」的**唯一**依据（`depot_gui.cpp:542-546` 注释明确：是否由段组成必须看段首标记，不能看段数），于是解出的段在车库列表退回「散链」；②寻路侧目的地判据用 `R3RIsCarOnlyFormation`（= 假引擎 + 车厢，**不含** ★ 要求），把「没升过段的纯车厢链 / 被抹掉 ★ 的段」一律当作合法挂接目标。日志实证：`DECOUPLE-DONE u=8 co=1`（co=1 即 `R3RIsCarOnlyFormation` 为真）之后 `U-ORD 0 type=17`（已插入 WAIT_COUPLE）却始终未挂上。 | 2026-09-16 第 26 轮（玩家要求：散链不能作为挂接寻路的目的地；段解挂后应仍是段） | 已修：新增统一谓词 `R3RIsCoupleTarget(t)`（`train.h` 声明 / `train_cmd.cpp` 定义）= `t->IsSegmentFront() && t->current_order.IsType(OT_WAIT_COUPLE)`，四处共用（寻路 3 处 + `R3RCanCoupleNow` 闸门，闸门新增拒绝原因 `target-not-segment`）；`DecoupleTrain` 不再无条件清 ★，改为 `if (R3RIsCarOnlyFormation(u)) u->SetSegmentFront();` —— 解出的车底段保持段身份，而 `GetSegmentHeadFromRear` 本就跳过链头 ★，独立段不会被误当自带段。已全量重编。**待复测**（**第 28 轮实测 · 会话 1：功能侧通过** —— 解出的车底段（`DECOUPLE-DONE u=3 co=1 real=3`，★ 保留、索引钉到 `WAIT_COUPLE`）在机车回 37,28 跑完自己计划的 `GOTO_COUPLE` 后，**无需重新「设为段」**即挂回：`A2-FLIP-U` 回滚 → `A3-FLIP-V-ONLY` 被 `SPLICE-GAP-REJECT` 拦下 → `A3-BOTH` → `FOLDCHK worst_gap=1` → `COUPLE-OK loco=2 rear=3 consist=3 co=1 real=4`；`R3R_perf.log` 末行 `chains=2 vehs=9 maxChain=6 segs=2` ⇒ 解出链 `[8..3]` 仍带 ★。**车库列表「第 k/N 段」显示仍待玩家目视确认**） | 高 |
| KI-63 | 本清单 §二 表格存在**编号重复**：`KI-56` / `KI-57` / `KI-58` / `KI-59` 各出现**两次**（较早一组为第 20 轮条目、较晚一组为第 22~24 轮条目）—— 属 2026-09-15 重写文件后多轮追加时的编号冲突；**两组条目内容各自有效、一一对应**，但引用时须连同「第几轮」一起说，否则会指错条目 | 2026-09-16 第 26 轮（步骤 7 收尾核对时发现） | **待整理**（不删除任何条目；后续追加一律从 **KI-64** 起顺延，避免再冲突） | 低 |
| KI-64 | 挂接组掩码的「归位」只发生在**写入**时（`R3RNormaliseCoupleGroupsOfSegment`）；★ 迁移（`R3RFlipChainBySegments` 段内反转）之后掩码可能停在段内**非 carrier** 车上。读路径按段并集扫描 ⇒ 读/写都正确，唯一残留边界是**该车被单独售出**时组归属随之消失；`CmdMakeSegment` / `CmdDemoteSegment` 切段时也不做归位（切分后掩码落在哪一段取决于它所在的车，语义上自洽但不显式） | 2026-09-16 第 27 轮（步骤 7 收尾复核，KI-47 的残留边界） | 未修（低收益 + 需改 `src/*.h` 触发全量重编；修法见 KI-47 列） | 低 |
| KI-65 | 步骤 7 收尾**静态核对结论**（非缺陷，记录口径）：① 工作树 `git status --porcelain -- src` = **20 改 + 9 新**，与 §一 摘要逐项吻合；② 最新源码 mtime `train_cmd.cpp` 18:52:33 < `build\openttd.exe` 19:23:23 ⇒ **无陈旧/混合对象**，测的那版 = 刚改的那版；③ 新文件 `couple_group*.cpp` / `sl/couple_group_sl.cpp` **lint 0 诊断**；④ 单点接线逐点复核在位：`R3RIsCoupleTarget`（声明 `train.h:694` / 定义 `train_cmd.cpp:1826`，四处共用 `yapf_rail.cpp:156`(E2)、`yapf_destrail.hpp:374`/`:448`(E1)）、`R3RCanCoupleNow`（`train_cmd.cpp:5915`，三站点 `"geo"`:5978 / `"depot"`:6047 / `"scan"`:6077）、闸门拒绝原因 `target-not-segment`（`:5924`）、边沿探针 `CPL-GATE`（枚举 `R3REDGE_COUPLEGATE`:4043，打印 :5946）、`R3RCoupleAllowed`（`couple_group.cpp:162`）；⑤ KI-55/58 的窗口 owner 修复（`couple_group_gui.cpp:159/180/200/208/267/365`）在位。**结论**：步骤 1~7 中 agent 可执行部分**全部完成**，剩余仅 §14.4 的玩家游戏内实测 | 2026-09-16 第 27 轮 | 已核查（口径留档） | 低 |
| KI-66 | `src/vehicle_base.h` 的 `IncrementRealOrderIndex()` 里遗留标注 `DEBUG (R3R — remove)` 的临时调试打印（`ADVANCE: veh=.. order=.. real_before=.. implicit_before=..`，触发条件 = 当前索引或 `current_order` 为 `GOTO_COUPLE` 却仍被推进）。第 28 轮玩家实测日志行 86 / 301 各命中一次。**第 29 轮更正口径**：行 86（车库第一次挂车，无身份迁移，机车正停在自有 index 0 = `GOTO_COUPLE` 上挂车）确为解挂后「恢复自有计划并跳过已完成的 `GOTO_COUPLE`」的**预期**行为（记忆 18491399 的 3958 行语义）；但**行 301**（车库内第二次挂车，走身份迁移分支）的 `real_before=0` **不是**预期值 —— 正确值应为 3，它是 KI-67 索引被覆盖后留下的症状。探针本身仍应删 | 2026-09-16 第 28 轮（读玩家实测日志时确认口径）/ 第 29 轮更正 | **已修（第 37 轮）**：按本条建议，与 P3 的头文件改动（`economy_base.h`）**合并为同一次全量重编**完成删除，结果见 KI-80 / 台账 §10.8。`src/vehicle_base.h` 16:02 的版本已无该块；产物自证 `ADVANCE_PROBE REMOVED`（`_tmp_verify_p3.cmd`） | 低 |
| KI-67 | **车库内「第二次挂车」不发生（玩家实测必现）**：`R3RRelocateFrontIdentity`（`train_cmd.cpp:5185`）里 `to->CopyVehicleConfigAndStatistics(from)` 原本排在「订单位置搬运」**之后**，而该函数内部经 `Vehicle::CopyConsistPropertiesFrom`（`base_consist.cpp:34-36`）会把 `cur_real_order_index` / `cur_implicit_order_index` / `cur_timetable_order_index` 从 `from` **再复制一遍**到 `to`；此刻 `from` 的三个索引已被本函数清零（`from->cur_real_order_index = 0`）⇒ 刚搬到 `to` 的正确索引被覆盖成 0。后果链：`Couple()` 记 `v->orders_backup_real_index = 0`（应为机车当时真正在执行的那条 `GOTO_COUPLE` 的索引 3）→ `DecoupleTrain` → `R3RSyncDrivingOrders` 归还后从 index 0 起 `IncrementRealOrderIndex()` → 1 → 机车跳过自己的 `GOTO_COUPLE`，直接奔 waypoint 出库 ⇒ 玩家预期「进库解挂后机车回库与段重新耦合」不发生（段那头已是 `WAIT_COUPLE` 原地等，白等）。**触发条件** = 该次挂车走了身份迁移分支（`head != v`，如 `A3-BOTH` 逻辑翻 v 后 `head=2 != v=0`）；第一次挂车（`head == v`，无迁移）不受影响，故现象表现为「只在第二次挂车后出错」。现场：`build\R3R_debug.log` 行 296 `DECOUPLE-FIRE consist=2 tx=38 ty=27 real=6` → 301 `ADVANCE: veh=2 order=5 real_before=0` → 310 `LOCO-AFTER-DECOUPLE veh=2 real=1` → 316 `DEPOT-ARR veh=2 real=1(6) destTx=38 destTy=35` → 318 `CT veh=2 tile=38,28`（机车驶离车库） | 2026-09-16 第 29 轮（玩家实测会话 1 复盘；用户澄清场景 = 库内两次挂车，第二次在进库解挂之后） | **已修（第 29 轮）**：`to->CopyVehicleConfigAndStatistics(from);` 上移到「订单位置搬运」**之前**并注释顺序约束（注释已写明「若排在之后则 to 恒为 0」及其后果）。增量编译 `EXITCODE=0`（`build\R3R_incbuild.log` = `[3/3] Linking CXX executable openttd.exe`，无 `error C`/`error LNK`；`openttd.exe` 20:47 > `train_cmd.cpp` 20:44）。**待复测**：库内两次挂车场景 —— 第一次（无迁移）行为应不变；第二次（解挂后再挂）应见 `ADVANCE real_before=3`，机车留在车库跑完 `GOTO_DEPOT` 后卷绕回 `GOTO_COUPLE`，与原地 `WAIT_COUPLE` 的段重新耦合 | 高 |
| KI-68 | **车库解挂后「车底段永不被 Tick」⇒ 机车第二次挂车必失败（第 30 轮定位，KI-67 的下一环）**：`DecoupleTrain` 把解出部分的链头提升为新 front engine（引擎分支 `u->SetFrontEngine()`、纯车厢分支 `R3RCreateCarOnlyFormation(u)` 内 `SetFrontEngine()`），但**没有调用 `InvalidateVehicleTickCaches()`**。`CallVehicleTicks`（`vehicle.cpp`）只在 `_tick_caches_valid == false` 时重建 `_tick_train_front_cache`，而 `Train::Tick()`（`train_cmd.cpp:10525`）里 `TrainLocoHandler` 只对 `IsFrontEngine()` 执行 ⇒ 新提升的 front **既不在缓存里、缓存又不重建** ⇒ 该车底段从此**再不被 Tick**：`ProcessOrders` 从不运行，`current_order` 永远停在 `OT_NOTHING`，机车侧统一闸门 `R3RCanCoupleNow` 永久以 `reject=target-not-wait` 拒绝 ⇒ 玩家现象「库里第二次挂车挂不上」。日志实证（`build\R3R_debug.log`）：`DECOUPLE-DONE u=8 co=1 real=0` 之后 u=8 **零**日志行（无 `DEPOT-ARR` / 无 `SKIP-STOPPED` / 无任何订单处理），且 `U-ORD 0 = type 17`（排程在、索引对）而闸门读到的 `tOrd=0`（`current_order` 未装载）—— 三者互相印证「没被 Tick」。旁证：同一场景下车站解挂的 `u=3`（解挂前已是 front）能正常装载 `WAIT_COUPLE`，差异只在「提升前是否已在缓存中」。**同族**：`CmdMakeSegment` 的散链升段路径 `R3RPromoteFreeWagonChainToFront`（`vehicle_cmd.cpp:270`）同样把非 front 车提升为 front engine 却未重建缓存。 | 2026-09-16 第 30 轮（接 KI-67 复测日志：`ADVANCE real_before=3` 已证实 KI-67 修复生效，卡点后移至此环） | **已修（第 30 轮）**：①`train_cmd.cpp` `DecoupleTrain` 身份处理末尾（`NormaliseTrainHead(u); NormaliseTrainHead(v);` 之后）新增 `InvalidateVehicleTickCaches();` + 长注释（写明后果链与「镜像 `TryTrainCouple` 同款修复」）；②`vehicle_cmd.cpp` `R3RPromoteFreeWagonChainToFront` 末尾新增同样调用 + 注释。**规则化**：凡「把非 front 车提升为 front engine」的路径（`DecoupleTrain` / `TryTrainCouple` 身份迁移 / `R3RPromoteFreeWagonChainToFront`）都必须紧跟一次 `InvalidateVehicleTickCaches()`。增量编译 `EXITCODE=0`（`build\R3R_incbuild.log`：`[2/4] vehicle_cmd.cpp.obj` → `[3/4] train_cmd.cpp.obj` → `[4/4] Linking CXX executable openttd.exe`；仅改 `.cpp`，未碰 `src/*.h` ⇒ 不触发 KI-15/KI-16），`openttd.exe` 21:06:19 晚于两个源码。**待复测**：库内两次挂车场景 —— 解挂后车底段应出现自己的处理行（`DEPOT-ARR` / `SKIP-STOPPED` 等），`curType` 转为 17（`WAIT_COUPLE`），机车到达后 `CPL-GATE` 应通过并出现 `COUPLE-OK` | 高 |
| KI-69 | **站台装卸途中被 R3R 改写订单 ⇒ `CargoPayment` 永不结算 ⇒ 下一轮进站 `economy.cpp:1535` 断言崩溃**：`Vehicle::BeginLoading()`（`vehicle.cpp:3538`）→ `PrepareUnload(front_v)` 在**前端车**上建一份 `CargoPayment`（`economy.cpp:1527` push `st->loading_vehicles`、`:1535 assert(cargo_payment == nullptr)`、`:1540 CargoPayment::Create(front_v)`），只有**这辆车自己**在 `current_order` 仍为 `IsAnyLoadingType()` 时走到 `Vehicle::LeaveStation()`（`vehicle.cpp:3588`，`:3590 assert(IsAnyLoadingType)`、`:3592 delete cargo_payment`）才会结算并销毁它。R3R 的解挂/挂接/身份迁移会在装卸途中把前端车的 `current_order` 换掉（`DecoupleTrain` 的 `v->current_order.Free()`、`Couple()` 的 Free、`R3RRelocateFrontIdentity` 的身份迁移），于是 `TrainController`（`train_cmd.cpp:3296-3303`）的 `IsAnyLoadingType()` 分支永不命中 ⇒ **payment 永久泄漏在车上、`loading_vehicles` 留下 stale 条目**；之后该车（再次成为前端车）进站 `TrainEnterStation`→`BeginLoading`→`PrepareUnload` 时断言失败。**实测现场**：`build\R3R_debug.log` 行 387 `DEPOT-ARR veh=2 ... curType=3`（OT_LOADING，站台 39,33 装货中）→ 行 390 `DECOUPLE-FIRE consist=2 tx=39 ty=33`（**装货途中解挂**）→ 行 410 `LOCO-AFTER-DECOUPLE veh=2 curType=0`（订单被清空、不再是 loading）；`crash-20260916T175303Z.log` 上下文 `(Train 2, c:0, st:FE, ..., [tile: 867 (39 x 33), type: 50 (Station)], CP)`（**CP = 旧 payment 仍在车上**），栈 `TrainEnterStation(train_cmd.cpp:8026) → Vehicle::BeginLoading(vehicle.cpp:3538) → PrepareUnload(economy.cpp:1535)`。**注意**：这不是「货物分配」（CargoDist/link graph，管「货分给谁拉」），而是**装卸结算**层；`economy.cpp:2446-2467` 已有的 R3R defensive 只跳过 stale 条目、不清理 payment，故只挡住了 `LoadUnloadVehicle` 的断言、挡不住这一条。**修法约束（重要）**：`LeaveStation()` 自身 `assert(IsAnyLoadingType())`，所以「订单已被改掉之后再去补调 LeaveStation」会立刻踩另一个断言——必须在**改写 `current_order` 之前**完成结算（或在改写处同步清理 payment + 从 `loading_vehicles` 摘除）。同族风险点：`Couple()`（v 装货中被挂）、`R3RRelocateFrontIdentity`（身份迁移把 front 换人）、`R3RSyncDrivingOrders`。附带损失：该次装卸收益不结算、`CargoPayment` 池泄漏（池尽则 `:1539 CanAllocateItem` 断言也会炸）。 | 2026-09-16 崩溃日志 `crash-20260916T175303Z.log`（游戏内 1920-03-08、加载后 3427 state ticks、约第 3 个循环） | **已修（第 31 轮 2026-09-17，允许型）** —— 新增 `R3RSettleLoadingBeforeChainEdit(chain, site)`（`train_cmd.cpp`，定义在 `DecoupleTrain` 之前）；`DecoupleTrain` 在 `TryTrainDecouple(v, u)` **成功拆分之后**、任何订单/身份改写之前对 `v`、`u` 各调一次（放在成功之后是为了避免 `TryTrainDecouple` 回滚白结账）；`R3RRelocateFrontIdentity(from, to)` 入口再防御性调一次。正常路径直接走原生 `Vehicle::LeaveStation()`（完整结算：删 payment、摘 `loading_vehicles`、复位 `LoadingFinished`/`CargoUnloading`/`load_unload_ticks`、`current_order` 转 `OT_LEAVESTATION`，下一 tick 由 `train_cmd.cpp:10366-10369` 正常 Free 并按新订单出发——等价于「先办完离站手续再换计划」）；订单若已被改写则走兜底路径（手工 `delete cargo_payment` + 摘除 `loading_vehicles` 残留 + 复位标志），因为 `vehicle.cpp:3590` 的断言不允许在非装卸订单上补调 `LeaveStation()`。探针：`R3R_debug.log` 新增 `SETTLE-LOAD site=<decouple-front\|decouple-rear\|relocate-front> veh= co= tx= ty=`。**未覆盖且已论证不可达**：`Couple()` 侧不需要结算（`R3RCanCoupleNow` 要求目标车 `current_order` 为 `OT_WAIT_COUPLE`，装卸中的车组不可能成为挂接目标）；若将来放开该闸门，必须同步补调用。**待复测**：站台装卸途中解挂 → 应出现 `SETTLE-LOAD` 且不再崩溃；连解三轮循环。 | 高 |
| KI-70 | **R3R 连挂/解挂与 CargoDist（链路图）的交互面从未验证**，本轮代码调研发现四条交互：①链路图的边只有两个数据源——**订单预测** `LinkRefresher::Run`（`linkgraph/refresh.cpp:27`，`v->orders == nullptr` 直接 return；沿**链头自己的 orders** 从 `cur_implicit_order_index` 递归刷新每跳，容量=整条物理链所有车 `refit_cap` 之和，构造器 `refresh.cpp:81-87` 遍历 `v->Next()`）与**实测上报** `VehicleIncreaseStats`（`vehicle.cpp:3393-3415`，由 `Vehicle::BeginLoading()` 调用，边=`front->last_loading_station → front->last_station_visited`，每节车 `refit_cap` 作 capacity、车内实载作 usage），主动刷新只在**链头**触发（`economy.cpp:2388`、`vehicle.cpp:3625` 均传 front）；②因此连挂期间**非链头段既不驱动也不刷新自己的腿**（例：A→C），该边只会在 `LinkGraph::Compress()`（`linkgraph.cpp:55-73`，每 `COMPRESSION_INTERVAL` 把 capacity/usage 折半）下逐轮衰减 → 若干轮后消失 → **上游货源不再被派往该腿**（表现：A 站货积压、评分下降）；③换端/身份迁移换掉链头后，实测上报口径 `front->last_loading_station → last_station_visited` 会把实际由某段完成的腿记到新链头名下（容量估计/统计归属漂移，不崩溃）；④`orders == nullptr` 的等待段彻底不参与分配（`refresh.cpp:30`），其 `GetNextStoppingStation()` 为空集（`vehicle_base.h:857-862`）⇒ 永远拉不到已指派下一跳的货。另注：装货侧的下一跳过滤恒取链头（`economy.cpp:1542` / `1995`），所以**整列（含被挂段车厢）能接哪些货 = 命令归属段那份计划覆盖的下一跳**。 | 2026-09-17 第 31 轮（玩家提问「A/B 两地去 C，C 连挂后共同去 D，客货流怎么算」） | **代码完成 + Release 全量重编通过（第 32~33 轮，待实测）**：玩家拍板选「方案 A 按段刷新」，`LinkRefresher::RunPerSegment` 已实现并替换 `economy.cpp:2388`/`vehicle.cpp:3625` 两处调用点；按段运费结算见 KI-71（同轮已实现）。实现要点（与最初设计有一处**关键修正**）：**Pass 1 = 整链一次**——用链头路线刷新、容量算整链。因为 `IncreaseStats` 走 `EdgeUpdateMode::Refresh`（= 取最大值保底，**不是累加**，见 `linkgraph.cpp` 的 `IncreaseEdgeCapacity` 注释「refreshing keeps a minimum capacity」），若逐段分别刷同一条腿，只会留下容量最大的那一段，3 段 ×50 会从 150 掉到 50；整链一次即旧行为，容量数值零变化。**Pass 2 = 逐段**：该段自持 `orders` 且与链头不同时，用它自己的路线 + 本段容量再刷一次，保住「段自己的腿」（例 A→C）不因 `Compress()` 折半而消失。待实测：①普通无★列车观测量零变化；②连挂数日后 A→C 边是否仍在、A 站是否积压；③解挂后该边是否随重新跑车恢复 | 中 |
| KI-71 | **按段运费结算**（玩家第 32 轮明确提出「运费结算也按段进行」）。现状：整链共用一个 `CargoPayment`（挂在前车），`PayTransfer`/`PayFinalDelivery` 逐包累加 `visual_profit`/`visual_transfer`/`route_profit`，`~CargoPayment()` 统一 `front->profit_this_year += (visual_profit + visual_transfer) << 8` 并 `SubtractMoneyFromCompany(route_profit)` ⇒ 公司收支不变，但**全部记在链头**，被挂段的车厢跑了半程却一分钱不进自己账。 | 备忘 `R3R_per_segment_linkgraph_memo.md` §3（第 32 轮） | **已实现 + Release 全量重编通过（第 32~33 轮，待实测）**。落地：`economy_base.h` `CargoPayment` 加 3 个 NOSAVE 字段（`r3r_recipient` / `r3r_recipient_base` / `r3r_booked`）+ `R3RSetPaymentRecipient(Vehicle*)`；`economy.cpp` 加静态 `R3RGetSegmentHeadOf(v)`（沿 `Previous()` 回溯到链头或 `IsSegmentFront()`）、在两个付款调用点（`v->cargo.Stage` ≈1549、`v->cargo.Unload` ≈2149）前后包 scope、`~CargoPayment` 改为「只把 `(visual_profit+visual_transfer)<<8 - r3r_booked` 记给链头」。**两处对设计草案的修正**：①用 `visual_*` 差值而非 `route_profit` 差值记账（`PayTransfer` 只加 `visual_transfer`、不动 `route_profit`，按 route_profit 记账会把中转费记到下一个被记账的段头上）；②**立即入账**到段头 `profit_this_year` 而不是延迟到 `~CargoPayment` 分账表（避免「付款对象活到 `LeaveStation`、期间车辆被卖」导致的指针失效 / index 复用错账；段头已不存在则该份留在链头）。总量守恒：整链账目 == `(visual_profit+visual_transfer)<<8`，与旧代码一模一样；`route_profit`/公司总收入零改动。无★普通列车（`head == front`）不开 scope ⇒ 行为逐位等价。**副作用（需玩家实测确认）**：①连挂列车的链头显示利润下降（各段账要解挂后才在车辆列表单独可见）；②收入动画仍按整链一个数字播放，与分段入账口径不一致；③`R3RRelocateFrontIdentity`/`CopyVehicleConfigAndStatistics`（会搬 profit）与解挂归还路径未复核——已入账到段头的钱不会再被搬走，但换端重排瞬间的付款边界未实测 | 中 |
| KI-72 | **按段刷新的开放点**（第 32 轮实现留下的口子，均未验证）：①挂车方自留计划 `front->orders_backup` 的**归属段未映射**，故 `RunPerSegment` 的 Pass 2 只覆盖「段自己持有且与链头不同」的 `orders`——A→C + GOTO_COUPLE 这类备份计划当前不会被按段刷新。**归属结论（第 32 轮查清）**：`R3RSyncDrivingOrders` 里 `chain->orders_backup = chain->orders`（只在「开始借用」即 owner != chain 时写入，存的是**链头自己那份**计划）⇒ `orders_backup` 恒属于**链头所在的那一段**；要接线就是「Pass 2 里对链头这一段额外用 `orders_backup` 当路线再刷一遍、容量限该段」，但这需要把 `RunScoped` 的路线来源从 `route_v->orders` 改成可传入的 `const OrderList *`（本轮未做）；②刷新成本：Pass 1 = 1 次订单遍历（= 旧行为），Pass 2 = 「自持计划的段数」次，段数多时装卸瞬间开销线性上升，帧率未实测（KI-14 血泪史）；③**已规避**：原设计「逐段刷同一条腿」会因 `EdgeUpdateMode::Refresh` 取最大值而丢掉除最大段以外的运力，Pass 1 已改为整链一次；④`RunPerSegment` 对非火车（公路/船/飞机）应与旧 `Run` 逐位等价（无★标记、`seg->orders == front->orders` 使 Pass 2 不触发），待回归确认。 | 备忘 §2、§4（第 32 轮） | **未验证**：需①普通无★列车跑线观测量零变化；②A/B→C 连挂去 D 场景连挂数日后 A→C 边仍在、A/B 站不积压；③3~5 段长链跑一圈看帧率与装卸耗时；④①`orders_backup` 接线（待拍板） | 中 |
| KI-73 | **KI-69 的兜底路径 `delete v->cargo_payment` 因类型不完整而不执行析构（MSVC `C4150`）**：`R3RSettleLoadingBeforeChainEdit()`（`train_cmd.cpp`）兜底分支写的是 `delete v->cargo_payment; // ~CargoPayment 会把 v->cargo_payment 置空`，但 `train_cmd.cpp` 只（间接）包含 `economy_type.h`、**没有** `economy_base.h` ⇒ `CargoPayment` 在该 TU 里是**不完整类型**，编译器按 `::operator delete` 释放而**不调用 `~CargoPayment()`**。后果三重：①`front->cargo_payment` **不会被置空**（悬垂指针，「注释所写的语义」根本没发生）；②`CargoPayment` 池槽不经 `PoolItem::operator delete` → `Tpool->FreeItem()` 归还（池条目泄漏；`economy.cpp:1600` 有 `assert(CargoPayment::CanAllocateItem())`，而 `economy.cpp:1596` 的 `assert(front_v->cargo_payment == nullptr)` 会被这个悬垂指针在下一个循环直接踩爆 —— 正是 KI-69 要防的那条）；③析构里的资金结算（`SubtractMoneyFromCompany(route_profit)` 与 `profit_this_year` 入账）**完全不执行** ⇒ 该次装卸的运费不扣不记（白赚）。触发条件 = 走到兜底分支（订单已被改写、不能补调 `LeaveStation()`；KI-69 现场即「装货途中在站台被解挂」）。 | 2026-09-17 第 33 轮（`build-release/R3R_release_build.log` 的 `C4150` 警告；警告里的 `train_cmd.cpp(4474)` 是**加 include 之前**的行号，加完后该 `delete` 语句在 `:4480`） | **已修（第 33 轮）** —— `train_cmd.cpp` 顶部加 `#include "economy_base.h"`（附注释说明原因）。仅 `.cpp` 改动 ⇒ 增量重编即可，不触发 KI-15 全量；修复后增量重编 `NINJA_EXIT=0`，**交付实测版 `build-release\openttd.exe` @ 2026-09-17 04:15:07（22 735 360 B）**，而 `openttd_r33_full.exe` @ 04:08:50 是修复**前**的全量产物（只可对照、勿用于验证）。**未实测**：需走到兜底分支（站台装卸途中解挂）确认 `SETTLE-LOAD site=decouple-*` 之后无悬垂、无崩溃、账目正常，并核对池无泄漏。 | 高 |
| KI-74 | **跨公司运费分账（待研究）**。盘点结论：①**按段结算已落地**（见 KI-71，同公司内把运费记到各段段头，总量守恒、公司总收入零改动）；②跨公司的**账本骨架其实已经存在**——`CargoPacket::RegisterDeferredCargoPayment(CompanyID, VehicleType, Money)` 把「某公司应收的运费」挂在**货物包**上（key = `CargoPacketDeferredPaymentKey(包index, 公司, 车型)`，全局表 `_cargo_packet_deferred_payments`，`cargopacket.cpp:259-263`），`PayDeferredPayments()`（`:265-281`）在**最终交付**时逐条 `SubtractMoneyFromCompany(cid, CommandCost(exp, -payment))` 结清（调用点 `economy.cpp:1524`，紧随 `route_profit += profit`）；`CargoPacket::Reduce()`（`:247-257`）按剩余量**等比例**削减未结额 ⇒ **一票货天然支持「多个公司各有应收」**。JGRPP 原本用它给**基础设施所有者**付费（注释「For Infrastructure patch. Handling transfers between other companies」，注册点 `economy.cpp:1550-1551`，以 `this->front->owner` 注册）；③"一票货分段分成 + 防重复付款"也已存在：`feeder_share` / `GetFeederShare(count)`（`cargopacket.h:193`）配 `_settings_game.economy.feeder_payment_share`（`economy.cpp:1548`）。**缺的不是账本，是规则**：(a) 链路图**完全不认公司**（`src/linkgraph/` 全目录搜 `company` 只命中 GUI 图例，`IncreaseStats(...)` 无 owner 参数）⇒ 多家运力被混加在同一条边上、货物分配是全局的，要「各公司各算」必须给图加公司维度或分图；(b) 没有承运授权/契约实体（唯一沾边的是城镇独占 `MayLoadUnderExclusiveRights`，`economy.cpp:1908-1911`）；(c) 没有公司间定价与账期（`feeder_payment_share` 是全局百分比，且交付即结清）；(d) 没有合同/账单/公司间收发明细 UI；(e) **物理前提（第 33 轮补核）**：跨公司挂接目前被两处 `w->owner != v->owner` 扫描闸门挡住（`train_cmd.cpp:6147` depot 路径、`:6184` 扫街兜底路径），而白名单单点 `R3RCoupleAllowed`（`couple_group.cpp:162`）只看分组掩码、**不看 owner**，几何路径 `GetCouplePosition` 亦无 owner 过滤（`R3RCanCoupleNow`，`train_cmd.cpp:6024`，只查 GOTO_COUPLE/WAIT_COUPLE/★/分组）⇒ 要放开跨公司，只需把这两条 owner 过滤改成走「白名单 + 授权」，**不必动数据模型**。 | 2026-09-17 第 33 轮（玩家提问「运费结算是不是也按段？是的话就可以研究跨公司」） | **已拍板、待实现（第 34 轮）**：目标形态 = **甲「多公司共同承运一票货、按腿分成」**（复用 deferred payment 账本）。玩家 D1~D6 全部回收：D2=链路图共用一张图但给边加公司维度、各公司分别记账；D3=各段各自保留所有权、控制权归控制者（链头/priority 最小段）；D4=连挂期间他公司段只读冻结（外公司不能卖/改，原公司随时可解挂收回，运行费记控制者）；D5=信号/闭塞按控制者算（**无需改代码**）；D6=同组 + `CGF_CROSS_COMPANY` 位才允许跨公司挂接 + 交付即分账（不做账期/明细 UI）。分 5 步实施（P1 授权放开 / P2 只读冻结 / P3 分账 / P4 UI / P5 链路图公司维度），见台账 `R3R_crosscompany_design_memo.md` §7。**注意**：`CGF_CROSS_COMPANY` 位尚不存在需新增（改 `couple_group.h` ⇒ 全量重编，建议与 KI-66 合并做） | 中 |
| KI-75 | **跨公司挂接尚有 3 项未拍板/待核对**：①**D2** 玩家答「走 2」（本人重建的语义：链路图共用一张图、但给边加公司维度并各公司分别记账；1=保持现状混加、3=完全分图）——**编号↔内容的映射未经玩家二次核对**，固化前不得当依据；②**D4** 连挂期间他公司段的保护策略（他公司能否出售/改造该段、原公司能否随时解挂收回、期间运行费与折旧算谁）；③**D6** 跨公司挂接的授权方式（分组 `CGF_CROSS_COMPANY` 位 or 免授权）与结账方式（交付即分账 or 账期/账单 + 公司间明细 UI）。台账 §3 已给推荐默认：D4→①「只读冻结 + 原公司可随时解挂收回」、D6→①「白名单 + 授权位 + 交付即分账」。**已拍板部分**：D1=甲（见 KI-74）、D3 所有权各段各自保留 + 控制权归控制者、D5 信号/闭塞按控制者（链头）算 | 2026-09-17 第 33 轮末（玩家回答原文；清单因未落盘曾在对话中断档，已补建台账 `R3R_crosscompany_design_memo.md`）／第 34 轮追问回收 | **已回收，本条关闭（第 34 轮）**：①D2 玩家二次核对通过 —— 确认为「链路图共用一张图、边上分别记录各公司运力与运量、各公司各算各的账」；②D4 玩家选 ①「只读冻结 + 原公司可随时解挂收回，期间运行费记控制者」；③D6 玩家选 ①「同组 + `CGF_CROSS_COMPANY` 位才允许跨公司」＋ ①「交付即分账，不新增界面」。实施跟踪转入 **KI-76** | 中 |
| KI-76 | **跨公司挂接实施（P1~P5）**。台账 `R3R_crosscompany_design_memo.md` §7 的分步计划：**P1** 授权放开（新增 `CGF_CROSS_COMPANY` 位 + `R3RCoupleAllowed` 单点加跨公司判据 + 分组窗口「允许跨公司」勾选框 + 拿掉 `train_cmd.cpp:6147/6184` 两处 `w->owner != v->owner` 硬拦；此步**只做到"能挂上"，钱仍全归控制者**，须明确告知玩家）／**P2** D4 只读冻结（他公司段禁售、禁改、禁单独 refit，原公司可随时解挂收回）／**P3** D1=甲 分账（`~CargoPayment` 按 `owner` 分账，跨公司份额走 `RegisterDeferredCargoPayment`，记账基准仍是 `visual_profit+visual_transfer` 差值）／**P4** UI（车辆窗口「所属公司 / 控制者公司」、车库链状态列）／**P5** D2 链路图公司维度（独立大项，需另立设计）。**成本提示**：P1 与 P3/P4 都要改 `src/*.h` 或 lang ⇒ 触发 **25~40 分钟全量重编**（记忆 66636022），建议 P1 与 KI-66（删 `vehicle_base.h` 调试打印）合并为同一次全量 | 2026-09-17 第 34 轮（D1~D6 全部回收后登记；见 KI-74/KI-75） | **P1~P3 已全部落地并编译通过（第 35~37 轮），待实测**（P1 = 第 35 轮 / P2 = KI-78 第 36 轮 / P3 = KI-80 第 37 轮；P4、P5 未做）。P1 落地清单：①`couple_group.h` 新增 `CGF_CROSS_COMPANY = 1 << 1` + 三个新 API（`R3RCoupleGroupMasksAllowCrossCompany` / `R3RGetCoupleGroupFlags` / `R3RSetCoupleGroupCrossCompany`）；②`R3RCoupleAllowed`（白名单单点）改为**两道门**：先分组掩码兼容（原逻辑不动），再 `coupler->owner != target->owner` 时要求共享组带 `CGF_CROSS_COMPANY`（两个未分组段的隐式组恒不放行跨公司，因为「没分组就没有可勾选的开关」）；③`CmdSetCoupleGroupFlags`（`command_type.h` 新枚举 + `couple_group_cmd.h/.cpp`，`CmdDataT<CoupleGroupID, bool>`，走 `R3RCoupleGroupIsManageable` 权限校验 + `InvalidateWindowClassesData`）；④分组窗口新增「允许跨公司挂接」行（`WID_CG_CROSS_COMPANY_TEXT` 文本 + `WID_CG_CROSS_COMPANY` **`WWT_BOOLBTN`**，`OnPaint` 镜像 `flags & CGF_CROSS_COMPANY` 并随 `can_manage` 置灰，`OnClick` 发命令、探针报文的 bit30 带该状态）；⑤**拿掉 `train_cmd.cpp` 两处 `w->owner != v->owner` 硬拦**（depot 扫描路径与扫街兜底路径，几何路径原本就无 owner 过滤），改由 `R3RCanCoupleNow -> R3RCoupleAllowed` 统一裁决，被拒候选视同「此处没有等待挂接的列车」；⑥语言串 `STR_COUPLE_GROUP_CROSS_COMPANY(_TOOLTIP)` 双语。预检 5 个翻译单元 `ALL_QUICKCHECK_OK`；P1 的头文件改动已被后续 P2/P3 的全量重编覆盖（第 36/37 轮），P1~P3 现同处一版已编译产物（`build\openttd.exe` @ 2026-09-17 16:23）。**本步只做到「能挂上」**：钱仍全部记在控制者（分账=P3，未做）。**待实测**：勾选/取消勾选即时生效；同组+勾选时跨公司机车能挂上；同组未勾选或不同组时「贴上去也不挂、原地等待、不刷屏」；同公司挂接行为与改前逐位相同。 | 中 |
| KI-77 | **`src/widgets/*.h` 的注释必须是纯 ASCII**：`cmake/scripts/` 的 widget 解析器生成 `build/generated/script/api/script_window.hpp`（以及 ai/game/template 的 `*_window.sq.hpp`）时会**在非 ASCII 字符处截断注释并插入换行**，把 `///< R3R (D6-①): label...` 变成 `///< R3R (D6-` + 换行 + `): label...`，生成出 `enum ... { ... , ): label of ... }` ⇒ 编译在**生成头**里报语法错误（`script_window.hpp:1345`），且报错行指向 `WindowDesc` 构造（`couple_group_gui.cpp:77`）极易被误判成布局括号不匹配。判别：`error C` 的 `file` 指向 `build\generated\script\api\script_window.hpp`。规则：widget 头里写 `R3R (D6-1)` 这类 ASCII 说法，中文/圈号只放 `.cpp`/`.h`（非 widget）注释与 `lang/*.txt`。已实测：改回 ASCII 后 `Generating script_window.hpp` 重新生成、5 个 TU 全通过。 | 2026-09-17 第 35 轮（新增 `WID_CG_CROSS_COMPANY_TEXT` 时踩到；`findstr` 复核 `src/widgets/*.h` 现存注释 **零**非 ASCII 命中，与规则自洽） | 已修（注释改 ASCII；规则已记录） | 中 |
| KI-78 | **D4「只读冻结」（P2）**：连挂期间外公司不得出售/改造/单独 refit 别人的段，原公司可随时解挂收回，期间运行费与折旧记控制者。判据=**该车所在链上同时存在本公司与他公司车**（`R3RChainSpansCompanies()`，依据 D3「挂接不转移车主」）⇒ 他公司的车对当前公司**只读**；车主自己（`v->owner == company`）恒不冻结，车主随时可解挂收回。落点：`CmdSellVehicle`（Train 分支开头逐节查，`SellChain` 查整链、单车只查被卖那一节，命中⇒ `STR_ERROR_CAN_T_SELL_TRAIN`）+ `CmdRefitVehicle`（`only_this` 直查；整车用与 `RefitVehicle()` 完全一致的 `GetVehicleSet(set, v, num_vehicles == 0 ? UINT8_MAX : num_vehicles)` 集合逐个查，命中⇒ `STR_ERROR_CAN_T_REFIT_TRAIN`）+ 车辆窗口 `WID_VV_REFIT` 置灰；**车库无需改**（车库列表按 owner 过滤，玩家点不到他公司车，其余入口都带 `CheckOwnership`）。日志：`R3R-FROZEN-SELL` / `R3R-FROZEN-REFIT`（后者仅显式 refit 写，auto_refit 由装卸循环驱动、可能每 tick 重试，不写以防刷屏）。 | 台账 `R3R_crosscompany_design_memo.md` §3-D4 选① / §9；KI-75、KI-76 | **已修（第 36 轮，全量重编 EXIT_CODE=0，待实测）**。全量重编 `build\R3R_fullbuild.log` 691 步通过、`build\openttd.exe` @ 2026-09-17 08:00（源码 mtime 07:10/07:26 全早于 exe ⇒ 无混合 obj），产物自证已能检索到 `R3R-FROZEN-SELL` / `R3R-FROZEN-REFIT` 两条探针串。**注**：该 exe 已被第 37 轮 P3 的全量重编取代（现交付产物 = `build\openttd.exe` @ **2026-09-17 16:23**，仍含这两条探针，见 KI-80） | 中 |
| KI-79 | **R3R 新增代码的两个格式化/类型坑（第 36 轮 P2 首次全量重编暴露）**：①`to_underlying()` 只对**枚举**有效 —— 0.73 里 `CompanyID` 是 `PoolID<CompanyIDTag>`（**类**）、`Owner` 只是 `using Owner = CompanyID`，所以 `to_underlying(_current_company)` 会报 `error C2672: 未找到匹配的重载函数` 并附 `xutility: "std::underlying_type_t<CompanyID>" 未能使别名模板专用化`，正确写法是 `.base()`（与 `v->index.base()` 同源）；②`Train::From()` 在 `vehicle_cmd.cpp` / `vehicle_gui.cpp` 里**不可用**（这两个 TU 拿不到该重载），故 `couple_group.h` 新增的 `R3RChainSpansCompanies` / `R3RIsFrozenForeignSegment` 一律取 `const Vehicle *`（配 `struct Vehicle;` 前置声明），调用方直接传 `Vehicle *`、遍历用基类 `Next()`，不再需要 train.h。附带教训：quickcheck 只覆盖"真正改过的 TU"——本轮 5 个 P1 TU 全绿，炸在第 676 步的 `vehicle_cmd.cpp`，所以新改的 .cpp 必须自己进预检清单（已加 `_tmp_qcheck_freeze.cmd`）。 | 2026-09-17 第 36 轮（P2 首次全量重编 `build\R3R_fullbuild.log` 捕获；已按此改写并复编通过） | 已修 | 低 |
| KI-80 | **P3 跨公司运费分账（D1=甲 / D6-①「交付即分账」）**。语义：一票货由 A、B 共同承运时，**谁的车跑的那一腿运费进谁的银行账户**，A+B 之和 == 改前链头独得的金额（只换收款人，不造钱不吞钱）。判据 = 正在装卸的那辆车的 owner（`v->owner != front->owner` 才置 payee），同公司链/所有非火车/单车恒等于链头 owner ⇒ 逐位不变。落点：①`economy_base.h` `CargoPayment` 新增 NOSAVE `CompanyID r3r_payee_company` + 内联 `R3RGetPayeeCompany()`（Invalid ⇒ 回退 `front->owner`）；②`economy.cpp` `R3RSetPaymentRecipient` 记/清 payee；③`PayFinalDelivery` 跨公司腿改走 `cp->RegisterDeferredCargoPayment(payee, front->type, profit)` **且不进 `route_profit`**（if/else 防双付），随后既有 `cp->PayDeferredPayments()` 当场结清；④`PayTransfer` 注册对象由 `front->owner` 改为 payee；⑤探针 `R3R-PAY-FINAL` / `R3R-PAY-TRANSFER`（仅跨公司且 `R3RDbgOn()` 时写）。**未新增任何存档字段**（`sl/economy_sl.cpp` 未动）⇒ 运输途中存读档不会重复付款。预检 `_tmp_qcheck_p3.cmd` = `ALL_QUICKCHECK_OK`；因改了 `economy_base.h` 按 KI-15/KI-16 触发全量重编。 | 台账 `R3R_crosscompany_design_memo.md` §7-P3 / §10；KI-74（D1）、KI-76 | **已实现（第 37 轮，待实测）**：复测 1~5 见台账 §10.5（其中**复测 1 是回归门**——同公司链必须与改前逐位一致）；**全量重编通过**（与 KI-66 合并为同一次）：`build\R3R_fullbuild.done` = `EXIT_CODE=0`，691 步；产物 `build\openttd.exe` @ 2026-09-17 16:23（50 633 728 B）；619 个 obj 全部晚于最新源码（`newest_src=vehicle_base.h @ 16:02:21` < `oldest_obj=alloc_func.cpp.obj @ 16:03:20`，`stale_obj_count=0` ⇒ KI-15 卫生合格）；产物自证 `R3R-PAY-FINAL` / `R3R-PAY-TRANSFER` 两串命中，`ADVANCE: veh=` 已消失（`_tmp_verify_p3.cmd`）。细节见台账 §10.8 | 中 |
| KI-81 | **P3 刻意未做的「权/统计」归属（边界，待玩家裁决）**：跨公司链的**交付统计**（`delivered_cargo`、货物流面板 `AddCargoDelivery`）、**补贴（subsidy）判定**、**工业独占权**（`exclusive supplier/consumer`、`MayLoadUnderExclusiveRights`）**仍然按链头公司（`front->owner`）计算**，即 `PayFinalDelivery` 传给 `DeliverGoods()` 的公司参数没有跟着 payee 走。理由：改成按车辆 owner 会引入「链头 A 持独占权、挂的是不持有该权的 B 的车 ⇒ 工业按 B 判拒收、货改投城镇」这种**玩法级回退**，风险远大于收益。**当前口径：P3 只分钱、不分权与统计**（钱按腿分，权/统计按链头）。若玩家要求「谁的车交付就记谁的统计与补贴」，须单独立项（可能需给工业独占与补贴判定单独传"权利公司"与"交付公司"两个参数）。 | 2026-09-17 第 37 轮（`PayFinalDelivery` 改动的代码注释里已写明该取舍） | **未实现（刻意搁置，待玩家裁决）** | 低 |
| KI-82 | **两个必须记住的坑（第 37 轮 P3 全量重编暴露）**：①**`src/economy_base.h` 里不能写「会解引用 `Vehicle`」的内联成员函数体** —— 该头只包含 `vehicle_type.h`，`Vehicle` 仅**前置声明**（字段 `Vehicle *front` 一直没问题，因为那只需不完整类型），但成员函数**体**里一碰 `front->owner` 就是 `error C2027: 使用了未定义类型"Vehicle"`（报错点 `economy_base.h(72)`）；正确写法 = 头里只留声明 `CompanyID R3RGetPayeeCompany() const;`，定义放 `economy.cpp`（那里 `vehicle_base.h` 已完整）。**关键在于只有部分 TU 会炸**：`economy.cpp` 编得过，`src/saveload/economy_sl.cpp` 炸 ⇒ 改这类头文件后**必须把「包含该头的其它 TU」纳入预检**（已把 `economy_sl.cpp` 固定进 `_tmp_qcheck_p3.cmd`）。②**构建日志是 GBK，`ripgrep`/`search_content` 看不见含中文的行** —— 本机 cl 输出中文（`注意: 包含文件:` / `错误 C2027:`），`build\R3R_fullbuild.log` 是 ANSI(936) 混编码，用 ripgrep 搜 `error C\d+` **只**能命中纯 ASCII 的 `FAILED:` 行，制造出"FAILED 但完全没有诊断信息"的假象（害得先怀疑 cl.exe 被 OOM 杀掉，白跑一轮单 TU 复现）；**诊断构建失败必须用 `findstr /C:"error C" <log>`**（GBK 感知），真凶立刻现形。 | 2026-09-17 第 37 轮（`build\R3R_fullbuild.done` = `EXIT_CODE=1`，失败步 `[350/691] economy_sl.cpp.obj`；`_tmp_qcheck_ecosl.cmd` 单 TU 复现；findstr 复核旧日志确认错误行一直都在） | 已修（accessor 改 out-of-line 定义于 `economy.cpp`；`_tmp_qcheck_p3.cmd` 两个 TU 均 `ALL_QUICKCHECK_OK`）。第 37 轮全量重编 `EXIT_CODE=0` 已端到端验证（KI-80）。**本轮新增同类坑**：换 `.cmd` 探针脚本时必须先停掉正在跑的全量重编再改（否则日志被覆盖、证据丢失），且 `cmd /c "含空格路径.cmd 参数"` 的引号会被剥离 ⇒ 探针脚本一律**不带参数**，详见台账 §10.9 | 中 |
| KI-83 | **C 盘已 0 字节可用（第 37 轮实测）**，直接后果：①任何需要写 C 盘的 agent 侧动作都会失败 —— 本轮把更新后的台账同步到工作区镜像（`c:\Users\冯洁敏\CodeBuddy\20260914233818`）时报 `There is not enough space on the disk.`，并因此**无法用结果视图展示文档**（该视图只接受工作区内文件，而工作区在 C 盘）；②凡落在 C 盘的 `TEMP`/`TMP`/链接临时文件都会失败 —— 记忆 76468255 早记「C 盘仅剩 0.7 GB，脚本内须把 TEMP/TMP 改指 D 盘 `build-release\tmp`」，现已恶化到 0。**不影响游戏运行与本次交付物**（源码、`build\`、全部备忘都在 D 盘）。处置建议：清 C 盘（`%TEMP%`、`%LOCALAPPDATA%\Temp`、构建中间产物、浏览器缓存），或长期把 agent 工作区与 TEMP 迁到 D 盘。 | 2026-09-17 第 37 轮（`copy` 同步失败；`dir c:\` 显示 `0 bytes free`） | **已修（第 38 轮）**：旧会话目录 `c:\Users\冯洁敏\CodeBuddy\20260914233818` 经 MD5 逐文件比对后已迁移并删除（无独有源码；权威文档以工作区版本为准）；C 盘可再生成缓存已清理（`CrashDumps` 57.5 MB、`pip\Cache` 14.5 MB、`NVIDIA\DXCache`+`GLCache` 1 332 MB，逐项核对为 0 文件；用户数据与 `Windows\Installer` 一律未动，合计释放约 1.4 GB）。**同时建立 `R3R_handover.md` 作为「换工作目录 / 换会话」的第一入口**，明确「所有文档一律写 `d:\sourcecode of JGRPP` 根目录、不要再为展示而复制到 C 盘」的规则。C 盘占满的真因是应用缓存（`AppData` 42.7 GB），非本工作区；长期对策 = `TEMP/TMP` 固定指 D 盘（铁律见交接文档 §7）。详见 `R3R_handover.md` §8 | 中 |
| KI-84 | **跨公司挂接在现装里「不可达」：「把别公司的段放进本公司的挂接分组」没有任何实现路径（台账 `R3R_crosscompany_design_memo.md` D6-① 的空白）**。D6-① 的**判据**（同组 + 该组带 `CGF_CROSS_COMPANY`）已实现且收口到单一闸门，但**「两家公司如何建立同组关系」从未定义** ⇒ `CGF_CROSS_COMPANY` 那道门在真实游戏里**永远执行不到**（unreachable），玩家侧表现 =「勾了允许跨公司仍然挂不上」。**三道独立的门（均已核对代码）**：① `couple_group_cmd.cpp:145` `CmdSetCoupleGroup` 开头 `CheckOwnership(t->owner)` ⇒ 命令只能操作**自己公司**的车，A 无法把 B 的段写进任何组；② `couple_group.cpp:150` 同一命令再要 `R3RCoupleGroupIsManageable(cg,_current_company)`（`:359` = `cg->owner==company`，严格 owner-only），且 `R3RCoupleGroupIsVisibleTo`（`:352` = `owner==OWNER_NONE \|\| owner==company`）⇒ B 在分组界面**看不到** A 的组；`CmdSetCoupleGroupFlags`（`:174`，管 `CGF_CROSS_COMPANY` 开关的那个命令）同样 owner-only；③ **结构性**：`couple_groups` 掩码的唯一写入点 = `R3RAddCoupleGroupToSegment`（`couple_group.cpp:169`，只能写①+②放行的组），已核对无其它写入者（`CmdMakeSegment` / `CmdDemoteSegment` / `DecoupleTrain` / `sl/couple_group_sl.cpp` 存档读**都不搬移掩码**；`R3RUnassignCoupleGroup`（`:343`）删组时是**全库逐车清位**）⇒ A 的段只可能带 A 公司组的 bit、B 的段只可能带 B 公司组的 bit ⇒ `R3RCoupleGroupMasksCompatible`（`:115`，`:120` 任一为 0 则要求 `a==b`）求交**恒空** ⇒ `R3RCoupleGroupMasksAllowCrossCompany`（`:125`）拿到的 `shared==0` 直接 false ⇒ **跨公司判据根本没机会跑**。**已预留但零使用**的钩子：`CGF_SHARED`（注释原文 "Reserved: the group may also be referenced by other companies"，全库无使用点）；`IsVisibleTo` 的 `OWNER_NONE` 公共组分支也**双死**（`CmdCreateCoupleGroup` 恒写 `_current_company`，且即便造出 `OWNER_NONE` 组，`IsManageable` 仍要求 `owner==company` ⇒ 谁都无法往里加段）。**与 D3 的关系（拍板要点）**：**「A 单方面把 B 的段拉进 A 的组」违背 D3「挂接不转移车主」**（等于替 B 做授权决定）⇒ 正解是「A 开共享 → **B 自愿把自己的段挂进来**」，即应落 `CGF_SHARED`（对**他公司可引用/可加入**的授权），**不能**简单放开 `CheckOwnership`。**待拍板（三选一）**：**(A) 落 `CGF_SHARED`（推荐）** = 新增可引用判据 `owner==c \|\| (flags&CGF_SHARED)` 供 `CmdSetCoupleGroup` 使用（`CheckOwnership` 保留；改名/删组/开关位仍 owner-only），可见性放开为 `owner==c \|\| (flags&CGF_SHARED)`，GUI 对「可见不可管」的组只放行加段、其余按钮置灰；**(B) 最小改动** = 直接把 `CGF_CROSS_COMPANY` 语义扩为「同时允许他公司加入本组」（不新增开关，代价 = 无法只共享不跨公司，但这本就是同一件事）；**(C) 组对象多公司共有** = 改数据模型（组归属变多公司/别名），改动最大、不推荐。附带不对称点：`CmdRemoveCoupleGroup`（把自己段移出组）**无** `IsManageable` 校验，放开引用后需一并审视（车主移除自己的段属车主权利，宜保留放行）。**本轮代码侧零改动**（仅登记）。 | 2026-09-17 第 39 轮（玩家提问「如何把别人公司的段放入我们公司的挂接分组」触发核对：`couple_group.h/.cpp`、`couple_group_cmd.cpp`、`couple_group_gui.cpp`、`company_cmd.cpp::CheckOwnership`） | **已修（第 39 轮：玩家拍板方案 A，D6-③ 落地，见 `R3R_crosscompany_design_memo.md` §11；全量重编 `EXIT_CODE=0` 已于 2026-09-17 18:10 通过）。游戏内可达性复测见 §11.4** | 中 |
| KI-85 | **P6 组共享（`CGF_SHARED`）整体处于"未验证"状态，并带三处已知边界**。已落地面：`CGF_SHARED` 由预留转生效、新增 `R3RCoupleGroupIsJoinableBy`（车主本人 **或** 组已共享）供 `CmdSetCoupleGroup` 使用（`CheckOwnership` 原样保留）、`R3RCoupleGroupIsVisibleTo` 对共享组放开、新增 owner-only 命令 `CmdSetCoupleGroupShared`、`R3RSetCoupleGroupShared` 关共享时驱逐他公司段、GUI 新增「允许他公司加入」勾选框与「（已共享）」标记、加/移段按钮改用可加入判据。**(1) 关共享驱逐是新增的数据变更行为**：推导安全（掩码只在挂接那一刻被 `R3RCoupleAllowed` 查询，驱逐**不解开**已挂好的列车，也不改 `Vehicle::owner`），但**未实测**。**(2) 旧存档兼容未实测**：旧档 `flags` 中 `CGF_SHARED=0`（未共享），不会出现"意外共享"，但**未实测**。**(3) 越权提示未做专门文案**：乙对甲的车点「指派段」被 `CheckOwnership` 拒时仍是通用串（`STR_ERROR_OWNER_NOT_YOURS`）；另**刻意不做**公司间邀请/审批 UI（共享是全公司广播式，符合 D6-①「不新增界面」口径）。**共享与「允许跨公司挂接」是「与」关系**（只共享 = 进得了组但挂不上），**刻意不做联动**。**第 43 轮修订（KI-91 合并）**：两个开关已合并为单一 `CGF_ALLOW_OTHERS`，故「与关系」表述作废，改为「**同一个开关做两件事**」（原文保留以留痕）；已落地面里的符号随之改名 —— `CmdSetCoupleGroupShared` → `CmdSetCoupleGroupAllowOthers`、`R3RSetCoupleGroupShared` → `R3RSetCoupleGroupAllowOthers`（`couple_group.cpp:148-186`，关开关时的逐段驱逐逻辑原样保留）、GUI「允许他公司加入」勾选框与 `WID_CG_SHARED` 已删除。**(1)(2)(3) 三个未验证边界的语义不变**（关开关的驱逐行为、旧档兼容、越权提示），**仍待复测**；但本条 (1) 描述的「只共享、不跨公司挂接」场景**已不再是可操作路径**（勾上即两件事同时生效），复测请按 `R3R_crosscompany_design_memo.md` §12.6 清单执行 | 2026-09-17 第 39 轮（D6-③ 实施；代码与台账见 `R3R_crosscompany_design_memo.md` §11）；第 43 轮按 KI-91 合并修订（§12） | **未验证（代码已落地：第 39 轮全量重编 `[701/701] Linking` / `EXIT_CODE=0`；第 43 轮合并后再次全量重编 `[706/706] Linking` / `EXIT_CODE=0`、`build\openttd.exe` @ 2026-09-17 20:20:48、语言包版本 `0xEDC76EB` 一致；游戏内复测未做，清单见 §11.4 + §12.6）** | 低 |
| KI-86 | **共享铁路设施下，不能在别公司车库对「本公司自己的链」执行「设为段 / 降级」（玩家 2026-09-17 第 40 轮实报）**。根因（两处、同一行模式）：`CmdMakeSegment` 与 `CmdDemoteSegment`（`vehicle_cmd.cpp`）开头的 `if (t == nullptr \|\| !IsTileOwner(t->tile, _current_company)) return CMD_ERROR;` 判的是**车库地块归属**而不是**车辆归属** ⇒ 开了共享之后，自己的链停在别人车库里，地块不属于自己 ⇒ 两个命令**静默** `CMD_ERROR`（返回裸 `CMD_ERROR`，玩家连提示都看不到）。对照：同窗口的普通按钮已用共享感知判据（`depot_gui.cpp:1044` 的 `IsInfraTileUsageAllowed(this->type, _local_company, tile)`），车库列表 `BuildDepotVehicleList`（`depot.cpp`，基于 `VehiclesOnTile`）会列出**所有公司**停在该库的车 ⇒ 玩家能在别公司车库看到并拖动自己的链，唯独段工具被拦。 | 玩家 2026-09-17 第 40 轮在线报告（"在我开启了共享铁路设施之后，不能在其他公司把自己公司的链执行设为段（也可能无法执行降级操作）"） | **已修（第 40 轮）**：两处改为 ①`CheckOwnership(t->owner)`（被操作的车必须属于本公司；`CmdMakeSegment` 的该检查放在 `Previous()` 回溯到**独立链头之后**，因为该命令作用于整条链 —— 这同时堵住了旧代码的越权洞：在自己车库里可对**他公司**的车发命令）+ ②`CheckInfraUsageAllowed(VehicleType::Train, GetTileOwner(t->tile), t->tile)`（共享感知，与 `order_cmd.cpp:1149` 的车库订单同款判据；未开共享时等价于原 `IsTileOwner`，且给出可显示的 `STR_ERROR_OWNED_BY` 而非静默失败）。仅改 `.cpp` ⇒ 增量重编 `EXIT_CODE=0`（`build\R3R_incbuild.log` = `[2/3] vehicle_cmd.cpp.obj` → `[3/3] Linking CXX executable openttd.exe`；`build\openttd.exe` 18:54:28 晚于 `vehicle_cmd.cpp` 18:51:14）。**待实测**：①开共享后，在甲车库对乙自己公司的链「设为段 / 降级」均应成功；②未开共享、自有车库内行为不变；③在自己车库里对他公司的车点段工具应被拒（提示"归他人所有"）。 | 中 |
| KI-87 | **列车「默认调度命令」的类型与文案存在歧义，需玩家裁决（本轮仅登记，代码零改动）**。玩家报告"列车的默认调度命令应当是直达类型的而不是什么不停车类型的"。现状（**均为上游继承**，`git diff HEAD` 已确认 R3R 未改动 lang 与 settings）：①新订单类型由 `_settings_client.gui.new_nonstop`（`gui_settings.ini`，默认**开**，值存在 `openttd.cfg`、不在存档）与 `_settings_game.order.nonstop_only` 决定；`GetOrderCmdFromTile`（`order_gui.cpp`，车库 ≈1478 / 车站 ≈1542 / 路点 ≈1575）在二者任一为真时给新订单设 `ONSF_NO_STOP_AT_INTERMEDIATE_STATIONS`，而车库订单恒走 `Order::MakeGoToDepot` 的**默认参数**（同为"跳过中间站"，与设置无关）；②文案在本 fork 中文里**自相矛盾**：设置名 `STR_CONFIG_SETTING_NONSTOP_BY_DEFAULT` = "设定命令时默认选择'直达'命令"，订单列表却把同一类型显示为"不停车前往"（`STR_ORDER_GO_NON_STOP_TO`；`src/lang/extra/simplified_chinese.txt` 的反向路点串又写作"直达"）⇒ "直达"与"不停车"在本中文包里指**同一个类型**，故玩家的措辞无法唯一确定诉求。三种候选落点：**(A)** 新订单默认为普通「前往」（沿途各站都停）= 关掉 `gui.new_nonstop` 的效果（只改 `gui_settings.ini` 默认值仅对新配置生效，已有 `openttd.cfg` 必须在设置里手关）；**(B)** 行为不变，只把订单文案统一为「直达前往」等，与设置名一致；**(C)** 玩家本意即"跳过中间站" = 现状（与"不要不停车"自相矛盾，已排除）。 | 玩家 2026-09-17 第 40 轮在线报告（本轮已就 A/B 向玩家提问，未擅自改）；第 41 轮玩家裁决 | **已修（第 41 轮；玩家选 (A)，并按【实际行为】统一文案）**：①`src/table/settings/gui_settings.ini` 的 `gui.new_nonstop` 由 `def = true` 改为 `def = false`（生成物 `build/generated/table/settings.h:895` 已成 `false`）⇒ 新建的车站订单（`order_gui.cpp:1544`）与车库订单（`:1491`/`:1876`）默认走 `ONSF_STOP_EVERYWHERE`（沿途各站都停），路点订单（`:1503`/`:1512` 的 `new_nonstop != _ctrl_pressed`）默认不再置 `ONSF_NO_STOP_AT_ANY_STATION`，回落到 `order_serialisation.cpp:210` 的 `ONSF_NO_STOP_AT_DESTINATION_STATION`（路点本就不停车）；②`src/lang/simplified_chinese.txt:1449` 文案由「设定命令时默认选择"直达"命令：{STRING}」改为「新建命令时默认选择"不停车"命令：{STRING}」，与订单列表的「不停车前往」口径一致（英文原文 = `New orders are 'non-stop' by default: {STRING2}`；`{STRING}`/`{STRING2}` 只是占位符写法差异 —— `STR_CONFIG_SETTING_VALUE = {ORANGE}{STRING1}`，两者都渲染为橙色的「开/关」值，且本中文包其它 bool 设置如 `STR_CONFIG_SETTING_WARN_LOST_VEHICLE` 同样用 `{STRING}`）；③玩家本机 `C:\Users\冯洁敏\Documents\OpenTTD\openttd.cfg:319` 的遗留 `new_nonstop = true` 已按字节级（Latin1 `ReadAllBytes`）改写为 `false`（`:198 nonstop_only = false` 未动）；④增量重编 `EXIT_CODE=0`（`build\R3R_incbuild.log` = `[14/16] settings_table.cpp.obj` → `[15/16] strings.cpp.obj` → `[16/16] Linking CXX executable openttd.exe`，`build\openttd.exe` @ 2026-09-17 19:07:54）；⑤语言包自检通过 —— `build\lang\english.lng` 与 `simplified_chinese.lng` 头 4 字节 = `EB 76 DC 0E` = `0x0EDC76EB` == `build\generated\table\strings.h:6973` 的 `LANGUAGE_PACK_VERSION`（无「No available language packs (invalid versions?)」风险，口径见记忆 52814982）。**残留边界见 KI-88** | 低 |
| KI-88 | **默认值翻转只对「配置里没有该键」的情况生效**。`gui.new_nonstop` 是客户端设置（`SettingFlag::NotInSave, NoNetworkSync`，**不是** `NotInConfig`）⇒ 任何跑过旧 exe 的机器都会在 `openttd.cfg` 留下 `new_nonstop = true`，而 `IniLoadSettings`（`settings.cpp:666-711`）**只在键缺失时**才用 ini 的 `def`，**不会回改已存在的值** ⇒ 老配置升级本 fork 后新建订单**仍会**默认跳过中间站，必须手关设置或删掉那一行。本次只改了开发者本机的 cfg（KI-87 ③），**未做任何代码级迁移**（可选做法：换新的 `var` 键名、或在 `IniLoadSettings` 前加一次性迁移，均未采纳）。**未实测**：①删键/新配置后，游戏内新建车站订单是否确实显示为「前往」而非「不停车前往」；②`order.nonstop_only`（`openttd.cfg:198 = false`）为真时仍会全局强制不停车，与本设置叠加的正确性未测；③JSON 排程导入路径（`order_serialisation.cpp:512/1365`）会按 `new_nonstop` 校验/套用默认值，改默认后导出→导入是否仍自洽未测。 | 2026-09-17 第 41 轮（KI-87 落地的残留边界） | **部分防护 + 待复测** | 低 |
| KI-89 | **跨公司耦合链进库触发 `vehicle.cpp:191` 断言崩溃**（玩家 2026-09-17 两次实崩，本轮根因定位并修复）。现场：`crash-20260917T111642Z.log` / `crash-20260917T112613Z.log`，断言均为 `Assertion failed at line 191 of ...\vehicle.cpp: c == Company::Get(this->owner)`，栈 = `Vehicle::NeedsAutorenewing`(vehicle.cpp:191) ← `GetNewEngineType`(autoreplace_cmd.cpp:306) ← `CmdAutoreplaceVehicle`(:965) ← `CallVehicleTicks`(vehicle.cpp:1845)，命令 `cmd: 0xA0 CmdAutoreplaceVehicle, payload: 0`，`company: 0`。探针 `build\R3R_debug.log` 末尾完全吻合：`COUPLE-OK loco=0 … co=1 real=4`（跨公司耦合成功，`co=1` = 车底属另一家公司）→ `DEPOT-ARR veh=0 … destDepot=1 tileEqDest=0`（机车带整链入库，**全程无任何 COUPLE/DECOUPLE 动作**）→ 下一帧 `SKIP-STOPPED veh=0 order=5 real=2 spd=0 tile=42,28` 后即崩。**真因** = 上游 auto-replace 家族假设「一条物理链只属于一个公司」：`CallVehicleTicks`（vehicle.cpp:1829-1861）对每辆进库车 `AutoRestoreBackup cur_company(_current_company, v->owner)` 后只把**链头** owner 传下去（`CmdAutoreplaceVehicle` 的 `c = Company::Get(_current_company)`，autoreplace_cmd.cpp:969），而 `GetNewEngineType` 对链上**每个 unit 无条件**调 `NeedsAutorenewing(c)`（:301 取替换引擎、:306 兜底 `e = v->engine_type` 同样调）⇒ 他公司那一节的 `c != Company::Get(v->owner)` 断言失败。**注意这不是 R3R 新引入的路径**：`VehicleEnteredDepotThisTick`（vehicle.cpp:1307-1320）把**每辆**进库车都塞进 `_vehicles_to_autoreplace`，`CallVehicleTicks` 随即无条件下达 `CmdAutoreplaceVehicle`，故「没设替换规则」也照样命中。**修复（第 42 轮，增量重编 `EXIT_CODE=0`，待实测）**：三处静默守卫 —— ①`CmdAutoreplaceVehicle`（autoreplace_cmd.cpp:954-967）在 `if (!v->IsChainInDepot()) return CMD_ERROR;` 之后，对非 `free_wagon` 链沿 `Next()` 逐节查 owner，出现异公司即 `return CMD_ERROR`（顺带堵住整链重建把他公司车辆卖掉丢数据的隐患）；②`Vehicle::NeedsServicing`（vehicle.cpp:315-328）逐 unit 循环改为 `const Company *vc = (v->owner == this->owner) ? c : Company::Get(v->owner);`，`EngineReplacementForCompany(vc, …)` 与 `v->NeedsAutorenewing(vc, false)` 都用 `vc`；③`CmdTemplateReplaceVehicle`（train_cmd.cpp:11413-11420）重建整链前逐节查 owner，混公司即拒绝。`free_wagon` 分支**故意不动**：该分支只替换被点的那一节，`_current_company` 即该节 owner，恒自洽（且上游已把「非链头的多节链」`free_wagon && t->First()->IsFrontEngine()` 判为 `CMD_ERROR`）。**为什么静默**：`CMD_ERROR = CommandCost(INVALID_STRING_ID)`，`ShowAutoReplaceAdviceMessage`（vehicle.cpp:1425）对 `INVALID_STRING_ID` 直接 `return` ⇒ 不会弹 AutorenewFailed 新闻。**残留**：①守卫是「拒绝」而非「支持」—— 跨公司链停库时不会被 autoreplace / autorenew / template-replace 处理，因此**原公司自己那几节**也一并跳过（要真正支持需按节拆公司分别重建，属未立项方向）；②`CmdAutoreplaceVehicle` 只从传入车辆向后扫（`free_wagon` 分支与 `CheckOwnership` 已排除「非链头且链有头」的入参，故到不了这里，但若将来新增入口需复核）；③`Vehicle::NeedsServicing` 的 `c = Company::Get(this->owner)`（vehicle.cpp:277）、`train_cmd.cpp:11107`、`tbtr_template_vehicle.cpp:110` 三处仍按「单车自己的 owner」取公司，跨公司链上语义正确（逐车判定） | 玩家 2026-09-17 第 42 轮（两份崩溃日志 + 探针日志） | **已修（第 42 轮；增量重编 3 个 `.cpp`，`EXIT_CODE=0`，`build\openttd.exe` @ 2026-09-17 19:29:45；游戏内复测未做）** | 高 |
| KI-90 | **挂接分组界面：以乙的身份打开甲的分组窗口时，「新建挂接分组」与两个跨公司开关必须置灰（玩家 2026-09-17 提出的 UI 备忘，待实现）**。**根因是权限基准不一致**：三个入口都把**容器窗口的公司**当参数传进去 —— `depot_gui.cpp:1133 ShowCoupleGroupWindow(this->owner)`、`vehicle_gui.cpp:2755`、`group_gui.cpp:1221`（车库窗口的 `owner` = 车库地块归属），开共享设施后乙打开甲的库 ⇒ 窗口内 `this->company = 甲`、`RebuildGroups()` 按甲过滤（列出甲建的组）；但所有命令判据用的是 `_current_company = 乙`。于是：①`can_create`（couple_group_gui.cpp:400 `Company::IsValidID(this->company) || Company::IsValidID(this->owner)`）**几乎恒真** ⇒ 「新建挂接分组」永远可点，而 `CmdCreateCoupleGroup` 建的是**乙的**组（`couple_group_cmd.cpp:66 CoupleGroup::Create(_current_company)`）⇒ 建完在甲列表里看不见（被 `RebuildGroups` 的 company 过滤掉），表现为「点了没反应」；②`can_manage = R3RCoupleGroupIsManageable(cg, this->company)`（couple_group_gui.cpp:277；判据 `couple_group.cpp:395-399` = `cg->owner == company`）用**甲**判定 ⇒ 选中甲自己的组时 `can_manage = true` ⇒ 两个开关 `WID_CG_CROSS_COMPANY`(:421) / `WID_CG_SHARED`(:428) **显示为可点**，但命令 `CmdSetCoupleGroupFlags`(`couple_group_cmd.cpp:191`) / `CmdSetCoupleGroupShared`(:223) 一律 `R3RCoupleGroupIsManageable(cg, _current_company)` 用**乙**判定 ⇒ 返回无消息 `CMD_ERROR` ⇒ **点了毫无反应且无任何提示**（正是玩家看到的「应当显示灰色」）。**待实现方案（需先与玩家确认语义，二选一）**：**(A) 他公司窗口整体只读** —— `can_create` 收紧为 `this->company == _local_company && Company::IsValidID(this->company)`（或更彻底：`ShowCoupleGroupWindow` 在 `company != _local_company` 时不开窗 / 开窗但把标题标注为「XX 公司的挂接分组」+ 全部编辑控件置灰）；两个开关置灰条件改为 `!can_manage || this->company != _local_company`。**(B) 乙在甲的库里管理自己的组** —— 「新建」保留可点但必须建在乙名下（现状已是），代价是必须把窗口的 `company` 与「编辑权限基准」拆成两个字段（`company` 决定列表过滤、「可管公司」恒取 `_local_company`），并补提示文案。**推荐 (A)**：与 D6-①「跨公司只走共享授权、不新增管理界面」口径一致，改动面最小。**附带一并修的隐患**：`ShowCoupleGroupWindow` 是**单例窗口**（`BringWindowToFrontById(WindowClass::CoupleGroup, 0)`，couple_group_gui.cpp:633）——若乙先打开自己的分组窗口，再去甲的库里点按钮，只会把**已有窗口**提到前台，**不会切换 `company`** ⇒ 界面停留在乙自己的组、玩家以为在看甲的。修 (A) 时应顺带把「已存在但 company 不同」的情形处理掉（切 `company` 或直接不开）。**改动面**：`couple_group_gui.cpp`（OnPaint 判据 + IsSelectedGroupManageable 的基准参数）、`depot_gui.cpp` / `vehicle_gui.cpp` / `group_gui.cpp` 的入口（是否仍允许传他公司）、可能加 lang 文案（只读/标题）⇒ **改 lang 触发 KI-16 语言包流程；改 `src/widgets/*.h` 触发 KI-77（widget 注释须 ASCII）** | 玩家 2026-09-17 第 42 轮（"乙公司进入甲公司的挂接分组界面时，新建挂接分组和那两个允许跨公司的开关应当显示灰色的"）；代码核对见 `couple_group_gui.cpp` / `couple_group_cmd.cpp` / `couple_group.cpp` | **已修（第 74 轮 2026-09-20，按推荐方案 (A)「他公司窗口整体只读」落地；仅改 `src/couple_group_gui.cpp` 一个 .cpp，未碰任何 `src/*.h` ⇒ 增量构建合法）**。落地内容：①新增 `CoupleGroupWindow::GetCompany()`（public 只读访问器，供单例窗口判别用）；②`OnPaint()` 开头引入 `const bool can_edit = (this->company == _local_company);`，再把 `can_manage`（重命名/删除/合并后的单个「允许他公司」开关）、`can_join`（加段/减段）、`can_create`（新建）**三者全部与 `can_edit` 相与** ⇒ 他公司窗口整体只读、按钮如玩家所愿置灰，而**左右两个列表仍可查看**（保留「看别人建了什么组」的能力）；`can_create` 同时保留 KI-55 的「company 必须有效」（构造函数已保证非法 owner 回落 `_local_company`，两条件不冲突）；③`ShowCoupleGroupWindow()` 先把非法 `owner`（公共/中立地块的车库会把非公司 owner 传进来）归一为 `_local_company`，再对**单例**窗口（`WindowClass::CoupleGroup`, id 0）比对 `GetCompany()`：相同只提到前台（原行为），**不同则 `CloseWindowById(WindowClass::CoupleGroup, 0)` 后按新公司重建** ⇒ 消除本条「附带隐患」里「乙先开自己的分组窗口、再去甲的库点按钮，界面仍停在乙自己的组、玩家以为在看甲的」。**未做（有意为之，留待后续）**：窗口标题未加「XX 公司的挂接分组」标注 —— 需要新增语言串，会触发 KI-16 的语言包生成流程，按本条「改动面最小」原则跳过；方案 (B)（乙在甲的库里管理自己的组、把「列表过滤公司」与「可管公司」拆成两个字段）未实现，**若要改口径为 (B)，只需回退 ② 里的相与条件**（`can_manage` / `can_join` / `can_create` 各一处）。 | 中 |
| KI-91 | **「允许跨公司挂接」（`CGF_CROSS_COMPANY`）与「允许他公司加入」（`CGF_SHARED`）能否合并为一个开关（玩家 2026-09-17 提出的 UI 备忘）**。**核实结论：可以合并，且推荐合并** —— 因为跨公司耦合真正可用的组合**只有一个**。必要链条：乙的段要经甲建的组 G 与甲的段耦合，必须同时满足 (i) 乙的段带 G 的 bit —— 而写入掩码的唯一入口 `R3RAddCoupleGroupToSegment` 之前要过 `R3RCoupleGroupIsJoinableBy`（`couple_group.cpp:401-409` = `owner == company \|\| (flags & CGF_SHARED)`）⇒ **必须 `CGF_SHARED=1`**；(ii) 耦合判据 `R3RCoupleAllowed`（`:227-245`）在 `coupler->owner != target->owner` 时要求 `R3RCoupleGroupMasksAllowCrossCompany`（`:125-137`）在共有组上找到 `CGF_CROSS_COMPANY` ⇒ **必须 `CGF_CROSS_COMPANY=1`**。故四种组合里：`{共享=1, 跨公司=1}` = 唯一放行；`{1,0}` = 乙能把段放进甲的组（ADD_SEGMENT 可用、日志有 `CG-JOIN-foreign`）但**永远挂不上**（`COUPLE-FAIL` 刷屏），是名符其实的「死状态」；`{0,1}` = 乙连组都看不见加不进（`IsVisibleTo`/`IsJoinableBy` 都要求 SHARED）⇒ **不可达**；`{0,0}` = 现状默认。英文 tooltip 自己也这么写：`STR_COUPLE_GROUP_SHARED_TOOLTIP`（english.txt:6146）末句 "To actually couple across companies, 'Allow cross-company coupling' must be enabled as well."。**合并方案（推荐，改法二选一）**：**(a) 保留两位、GUI 只给一个复选框**，写盘时同置同清（`CmdSetCoupleGroupFlags` 里 `cross_company ? (set CROSS \| set SHARED) : (clear CROSS \| 走 R3RSetCoupleGroupShared(false) 的驱逐路径)`）—— 存档格式不变、旧档可不迁移，代价是「能加入但不能耦合」这一状态从此不可达（本来就是死状态）；**(b) 数据层合并成一位**（删 `CGF_CROSS_COMPANY` 或删 `CGF_SHARED`，读档时 `cross = cross_bit && shared_bit`）—— 更干净但**动存档字段与读档兼容**，风险高。**必带的三项配套**：①**关闭语义必须复合**：`R3RSetCoupleGroupShared(false)`（`couple_group.cpp:157-200`）会**驱逐他公司段**（把非本公司的段移出该组，日志 `CG-SHARED … evicted=N`），合并后关掉唯一开关必须走「清 CROSS + 走驱逐」，否则会留下 `{SHARED=0, CROSS=1}` 的不可达残渣；②**GUI 文案合并**：两个复选框 → 一个（例如沿用 `STR_COUPLE_GROUP_CROSS_COMPANY` "Allow cross-company coupling"，把 `STR_COUPLE_GROUP_SHARED` 并入 tooltip），lang 双语（`english.txt:6143-6146` / `simplified_chinese.txt`）同步 ⇒ **触发 KI-16 语言包流程**；③**widget 铺面调整**：`src/widgets/couple_group_widget.h:25-28` 的四项（两组 TEXT+checkbox）减半 ⇒ **触发 KI-77（widget 注释须 ASCII）**。**关联**：本条目与 KI-84（D6-①/D6-③ 的可达性）、KI-85（`CGF_SHARED` 未验证）、KI-90（他公司视角权限）同源；`R3R_crosscompany_design_memo.md` §11 的 D6-③ 决策若要撤销一半需在此同步 | 玩家 2026-09-17 第 42 轮（"那两个允许跨公司的开关能否合并为一个，这两个可以先写备忘"）；代码核对 `couple_group.cpp:125-245` / `couple_group_cmd.cpp:187-232` / `couple_group_gui.cpp:415-428` / `english.txt:6143-6146` | **已修（第 43 轮，玩家拍板「先合并再实测」；代码 + 双语 lang 落地，全量重编 `EXIT_CODE=0`，游戏内复测未做）**。落地走的是本条目推荐的 **(a) 保留两位、GUI 只给一个复选框**：①数据层新增 `CoupleGroup::CGF_ALLOW_OTHERS = CGF_SHARED \| CGF_CROSS_COMPANY` + `bool AllowsOthers() const`（`couple_group.h:60-73`），三个读判据全部收口到 `AllowsOthers()`（`couple_group.cpp:137` / `:391` / `:407`）⇒ **存档字段与读档布局零改动**，旧档「任一位为 1 即视为已开放」（本条要求 (ii) 的兼容口径）；②命令层 `SetCoupleGroupFlags` + `SetCoupleGroupShared` 合并为单一 `Commands::SetCoupleGroupAllowOthers`（`couple_group_cmd.h`，`CmdDataT<CoupleGroupID, bool>`），执行体 `R3RSetCoupleGroupAllowOthers`（`couple_group.cpp:148-186`）= 开则 `flags \|= CGF_ALLOW_OTHERS`、关则清两位**并走驱逐路径**（逐段 `R3RIsCoupleGroupCarrier` + `R3RRemoveCoupleGroupFromSegment`，日志 `[R3R] CG-OPEN group=%u allow_others=%u evicted=%u`）⇒ 本条「必带配套 ①关闭语义必须复合」已落实，**不会留下 `{SHARED=0, CROSS=1}` 残渣**；③GUI 两个复选框 → 一个（`WID_CG_SHARED_TEXT` / `WID_CG_SHARED` 从 `src/widgets/couple_group_widget.h` 删除，`WID_CG_CROSS_COMPANY` 注释改为 D6-1+D6-3），`couple_group_gui.cpp:418-421` 用 `AllowsOthers()` 驱动 lowered/disabled（**widget 注释全 ASCII，KI-77 通过**）；④文本合并按配套 ② 执行，但**未沿用** `STR_COUPLE_GROUP_SHARED` 并入 tooltip 的做法，而是把 `STR_COUPLE_GROUP_CROSS_COMPANY(_TOOLTIP)` 改成「允许他公司加入并跨公司挂接」，旧串 `STR_COUPLE_GROUP_SHARED(_TOOLTIP)` **保留但已无引用**（与 `SET_AS_FRONT_WAGON` 遗留串同处理，避免重排语言包编号）⇒ **KI-16 流程已走**（只改串正文、未增删串 ⇒ `LANGUAGE_PACK_VERSION` 仍 `0xEDC76EB`）。**效果**：`{共享=1, 跨公司=0}` 死状态与 `{共享=0, 跨公司=1}` 不可达态**同时消失**，四种组合退化为「唯一开关」。**未采纳 (b)**（数据层删位），理由 = 动存档兼容风险高而收益为零 | 中 |

| KI-94 | **`UpdateVehicleTimetable` 断言崩溃（`timetable_cmd.cpp:958`，`assert_msg(real_timetable_order == real_current_order, ...)`）**。玩家实报："旧问题已解决，现在是新的断言崩溃点，timetable_cmd 的 958 行"。现场 `build\R3R_debug.log`（23:18 那版 exe）：`COUPLE-OK loco=2 rear=6 consist=6 co=1 real=4 type=2` → 三行 `DEPOT-ARR veh=2 spd=0`：`real=4(2) curType=0` / `real=4(8) curType=3`（real 指向的订单在库里已变成 `OT_IMPLICIT`）/ `real=5(2) curType=2` → `CRT veh=2 ... found=1` → 两行 `NOCAB-SET this=2 db=1` → 崩溃。机制：`cur_timetable_order_index` 与 `cur_real_order_index` 失步（tt 停在 4、real 已到 5），而 `timetable_cmd.cpp:951-959` 的 `else` 分支只豁免 `OT_CONDITIONAL`，于是"到达/离站触发 `UpdateVehicleTimetable`"的那一刻 Debug 版断言把游戏打崩。R3R 有多条路径各自改写订单索引（耦合借用 `orders_backup_real/implicit_index`、`R3RSyncDrivingOrders` 采纳车组进度、`DecoupleTrain` 把索引钉到 `WAIT_COUPLE`、`R3RRelocateFrontIdentity` 迁移身份、`SkipToNextRealOrderIndex` 的 `GOTO_COUPLE` 锁不推进 tt），而 `UpdateRealOrderIndex()`（`vehicle_base.h`）**只推进 real、不碰 tt**，且 `order_cmd.cpp:4478` 每个 `ProcessOrders` tick 都会调它 —— real 一旦从隐式订单被推过去，tt 就落后一位。 | 2026-09-17 第 46 轮（玩家实报断言崩溃） | **已修 + 已插桩（待复测）**：①`timetable_cmd.cpp` 原断言改成"自愈 + 取证"——先写一行 `TT-DESYNC veh= trav= real= tt= impl= n= curType= realType= ttType= types=...`（`types=` 是整条订单类型序列）到 `R3R_debug.log`，再把 `v->cur_timetable_order_index = v->cur_real_order_index; real_timetable_order = real_current_order;` 继续执行（与函数出口 `scope_guard` 在 travelling 时的动作一致；非条件订单失步没有"故意钉住"的语义，归 real 是正确的，不会把等待时间记到别的订单上）；②`train_cmd.cpp` 新增静态探针 `R3RCheckTtSync(v, tag)`（只在失步时写一行 `TT-CHK-BREAK <tag> veh= real= tt= impl= n= curType=`），已挂在 `couple-waitcouple`（耦合采纳车组进度）、`decouple-u`、借用归还块，以及**每次 `ProcessOrders()` 之前**（tag `pre-processorders`，紧贴崩溃点）；③`DEPOT-ARR` 探针追加 `tt=` / `impl=` 两字段，用来判断失步发生在"进库处理"之前还是之中。仅动 `.cpp`（`timetable_cmd.cpp` / `train_cmd.cpp`）⇒ 按 KI-15 增量合规；`_tmp_inc_build.cmd` 末行 `[3/3] Linking CXX executable openttd.exe`、`EXIT_CODE=0`。**复测要点**：跑同一场景（机车 2 挂车底、进 42,28 库、出库），在 `R3R_debug.log` 里找 `TT-DESYNC` / `TT-CHK-BREAK`——`pre-processorders` 命中说明失步在 tick 开始前就存在（往上找最后一条改索引的 R3R 块），只有 `TT-DESYNC` 命中说明失步就发生在 `ProcessOrders` 内部（重点看 `UpdateRealOrderIndex()` 那次推进）。 | 高 |
| KI-93 | **车库「卖光本库车辆」（`CmdDepotSellAllVehicles`）触发的悬垂 `OrderList` 崩溃已加固（`crash-20260917T130637Z.log`）**。栈 = `CmdDepotSellAllVehicles` → `CmdSellRailWagon` → `delete sell_head` → `Vehicle::PreDestructor`（`vehicle.cpp:1215+`，`DeleteVehicleOrders(this)` 于 `:1244`）→ `DeleteVehicleOrders`（`order_cmd.cpp`）→ `IsOrderListShared()` / `RemoveFromShared()` 解引用**已释放**的 `OrderList`（调试堆毒值 `0xDDDD…` 被当成 shared 登记读）。根因与 KI-92 残留 ③ 同源但更深：路线 A 的「借用」**不注册为共享链**（同一份 `OrderList` 的 `num_vehicles` 仍是 1、`IsOrderListShared()` 恒 false），于是「非共享 ⇒ 可销毁」这条判定在①`OrderList::FreeChain(false)` 与②`DeleteVehicleOrders()` 两处都会在**借用者与主人任意一方先被删**时真删列表，给另一方留下悬垂指针；`CmdDepotSellAllVehicles` 是**逐辆下单**，只要先卖掉 owner 段、后销毁借用者（或反之）就必命中——第 44 轮的 `R3RSyncChainAfterDepotEdit`（KI-92）只覆盖 `CmdMoveRailVehicle` / `CmdSellRailWagon` 两条 depot 编辑路径的**入口处交接**，挡不住逐辆连环删除时的中间态。**第 45 轮三处加固（全部只动 `.cpp` ⇒ 按 KI-15 增量合法）**：①`order_cmd.cpp` 新增文件内静态辅助 `R3RFindOrderListReferrer(const OrderList *ol, const Vehicle *except)`（定义紧邻 `OrderList::FreeChain` 之前，`:586`）：遍历 `Vehicle::Iterate()`（带 `except` 时限定同 `type`），返回第一个 `w->orders == ol \|\| w->orders_backup == ol` 且 `w != except` 的**活车**，否则 `nullptr`；②`OrderList::FreeChain(bool keep_orderlist)` 把该判定提到**清空 `orders` 之前**（仅 `keep_orderlist == false` 时）：命中即把 `this->first_shared` 重指到「仍在使用它的活车」（`other->IsPrimaryVehicle() ? other : other->First()`，为空回退 `other`）并**直接 `return`**，列表**原样移交**、不 `delete` —— 判定必须早于清空，否则接手方的排程会被抹成空表；③`DeleteVehicleOrders()` 的非共享分支命中同一判定时改为 `v->orders = nullptr; ol->FreeChain(false);`（借 `FreeChain` 顺手修好 `first_shared`，并且**不再**调 `UpdateDeparturesWindowVehicleFilter(ol, true)` —— 列表还活着，打「已离开」会污染出发板）；④`Vehicle::PreDestructor()` 释放 `orders_backup` 与 `saveload/afterload.cpp` 的 `OT_NOTHING` 清理都改为**先解绑再 `FreeChain`**（`OrderList *backup = v->orders_backup; v->orders_backup = nullptr; backup->FreeChain(false);`）——判定要遍历活车，调用方**必须先摘掉自己的指针**，否则会把「正在销毁的自己」当成仍在引用者而永远移交不出去。**另加一处例外**：`DeleteVehicleOrders()` 里原本无条件的「借用归还」（`v->orders = v->orders_backup`）现改为 `r3r_borrow_is_shared = v->orders != nullptr && v->orders->IsShared() && (v->PreviousShared() != nullptr \|\| v->FirstShared() == v)` 为真时**跳过**：因为**活着存下来的借用会以原生共享链形式读回**（KI-01：读档后由 `R3RRebuildCouplePriorities` 反推 owner），此时该车是那条共享链的成员，必须走 `RemoveFromShared()`，硬摘指针会把别的成员留成指向将死车辆的共享链；`v->r3r_orders_borrowed = false` 在两种分支之后统一执行。**未改动的关联点（仍按原语义）**：`R3RRebuildCouplePriorities` / `R3RSyncDrivingOrders` / `R3RSyncChainAfterDepotEdit` 的优先级与归属规则、`orders_backup` 单层结构（KI-02） | 2026-09-17 第 45 轮（`crash-20260917T130637Z.log`；KI-92 残留 ③；「借用不注册为共享链」见 KI-03 / 记忆 18491399、35644846） | **部分防护（第 45 轮：加固已落地且编译通过；按 §七 口径「编译通过不算已修」，待实测后才可改判『已修』）**。改动面 = `order_cmd.cpp`（新增 `R3RFindOrderListReferrer` + `FreeChain` 移交分支 + `DeleteVehicleOrders` 判定/例外）、`vehicle.cpp`（`PreDestructor` 先解绑）、`saveload/afterload.cpp`（先解绑），**未碰任何 `src/*.h`**（曾短暂加过 `order_base.h` 的公开 setter，已完全撤回原样，避免 KI-15 全量重编）；`read_lints` 三文件 + `order_base.h` 均干净；编译交给后台 `_tmp_inc_build.cmd`（增量，日志 `build\R3R_incbuild.log`，完成标记 `build\R3R_incbuild.done`），**已通过**：末行 `[5/5] Linking CXX executable openttd.exe`、`EXIT_CODE=0`；`build\CMakeFiles\openttd_lib.dir\src\order_cmd.cpp.obj` @ 21:25:42 / `vehicle.cpp.obj` @ 21:25:57 均早于 `build\openttd.exe` @ **2026-09-17 21:28**（50 637 824 B）⇒ 测的这版 = 刚改的这版（KI-15 合规）。**待实测（全部未做）**：①车库内「卖光本库车辆」对一列「机车+车底」耦合链不再崩；②卖掉 owner 段后借用者仍能跑、排程不丢；③卖掉借用者段后 owner 排程不丢；④带连挂状态存档 → 读档 → 卖光，不崩且 `RemoveFromShared` 路径不被误走；⑤出发板窗口（`UpdateDeparturesWindowVehicleFilter`）与移交后的列表一致，无「假离开」 | 高 |
| KI-92 | **车库内手动解耦（拖拽）不解开「调度借用」：机车段继续执行耦合时借来的车组排程，自己那份排程仍留在 `orders_backup` 里（玩家 2026-09-17 第 44 轮实报）**。现场（`build\R3R_debug.log`）：机车 `veh=0`（eng 494）与车底段 `veh=9`（eng 325）耦合后，日志出现 `ARRANGE-IN dh=-1 dst=-1 sh=0 src=9 mc=1`（= 在车库列表把车底**整段**拖到空行 → `TrainDepotMoveSegment` → `Command<MoveRailVehicle>` → `ArrangeTrains` 拆链），随后 `MAKESEG-CMD veh=9`（分离出的零动力段自动升段，`depot_gui.cpp:232-238`）；日志尾 `SKIP-STOPPED veh=0 order=0 real=17 spd=0 tile=38,27` 与 `SKIP-STOPPED veh=9 order=0 real=17 spd=0 tile=38,27` **同为 index 17** ⇒ 两条链仍指向**同一份 `OrderList`**（借用写法：owner = 车底段，机车链头只借用指针、**不**共享列表）。**根因**：路线 A 的「调度借用」只在 `Couple`（起借）与 `DecoupleTrain`（`R3RRenumberPriorities` + `R3RSyncDrivingOrders` 归还）两处维护，而 `CmdMoveRailVehicle` 与 `CmdSellRailWagon`（均在 `train_cmd.cpp`）在 `ArrangeTrains` 拆/合链之后**没有任何 R3R 交接**，`r3r_priority` / `r3r_orders_borrowed` / `orders_backup` 全部停在耦合那一刻。后果三重：①机车继续跑车组排程（玩家所见）；②其自有排程永不归还（`orders_backup` 单层，只会被下一次耦合覆盖）；③`CmdSellRailWagon` 卖掉「车底（= owner）」时会 `DeleteVehicleOrders(owner)` → **列表被真删，而机车仍持有指针**（悬垂；`order_cmd.cpp:3479-3491` 的借用保护只对「借用者自己被删」生效）。**与公司无关**：命令层没有跨公司分支，同公司复现路径完全一致（玩家问的「不同公司有无关系」= 无） | 2026-09-17 第 44 轮（玩家实报 + `build\R3R_debug.log` 现场；借用机制见 `R3R_crosscompany_design_memo.md` §12 与记忆 35644846、18491399） | **已修（第 44 轮，增量重编通过：`EXIT_CODE=0`、日志末行 `[3/3] Linking CXX executable openttd.exe`、`build\CMakeFiles\openttd_lib.dir\src\train_cmd.cpp.obj` @ 20:59:26 > 源码 `src\train_cmd.cpp` @ 20:58:09、产物 `build\openttd.exe` @ **2026-09-17 21:00:25**（50 637 824 B）；仅改 `.cpp` ⇒ 按 KI-15 增量合法，无头文件改动；**待实测**）**：新增静态辅助 `R3RSyncChainAfterDepotEdit(Train *chain)`（`train_cmd.cpp`，定义紧接 `R3RSyncDrivingOrders` 之后）= `R3RRenumberPriorities(chain)` + `R3RSyncDrivingOrders(chain, false)`（renumber 只压紧编号、保持相对序、平局按物理序 ⇒ 「没拆链的编辑」不改变 owner，而「拆到只剩机车」时 priority 2→1 ⇒ `owner == chain` ⇒ 归还 `orders_backup`）；在 `CmdMoveRailVehicle` 的 Execute 分支 `NormaliseTrainHead` 之后对 `src_head` / `dst_head` 各调一次（**放在 Execute 块内** ⇒ 试算阶段不改状态），在 `CmdSellRailWagon` 的 `NormaliseTrainHead(new_head)` 之后、`delete sell_head` 之前调一次（先归还借用、再让被卖部分释放列表 ⇒ 顺带消掉上面③的悬垂）。**未实测**：①拖出车底后机车应显示自己的排程、排程面板与后备索引正确；②拖出机车本体同理；③整列（不拆）在车库内换行/重排后行为与改前逐位一致；④卖掉车底后机车不悬垂、不崩。**已知边界（未覆盖 / 待玩家确认）**：(a) 本轮只修了「拖拽（`CmdMoveRailVehicle`）」与「卖车（`CmdSellRailWagon`）」两条 depot 编辑路径，直接改段结构的 depot 工具（`CmdMakeSegment` / `CmdDemoteSegment`）**未加同步** —— 降级只撤销假引擎与 ★、**不删** `OrderList`，故不会悬垂，但「降级掉 owner 段之后机车应否收回自己的排程」这一语义尚未定义，待复测；(b) **depot 内手动拼接**（把一整列车拖到另一列车上，`dst != nullptr`）按 `R3RRenumberPriorities` 的平局规则（平局按物理序）会判定「被拖入的一方在后 ⇒ 原链头为 owner」，即**链头继续驱动自己的排程、被拖入列车的排程挂起但不丢**（再次拖开即恢复）—— 这是刻意选的最保守语义，与订单耦合的「车组计划优先」不同；若希望 depot 手动拼接也按「后挂入者优先」，需另立规则并同步改这两处钩子。**第 45 轮补充**：上面残留 ③（卖掉 owner 段后借用者持悬垂指针）已由 **KI-93** 从「销毁判定」层面兜住（`OrderList::FreeChain` / `DeleteVehicleOrders` 的移交判定 + 调用方先解绑），`CmdDepotSellAllVehicles` 逐辆连环删除的中间态也被覆盖 | 高 |

---

## 三、决策记录

### 3-1 2026-09-14 拍板：性能降级为「低」并暂时搁置，转入功能缺陷修复

- **依据**：R3R : JGRPP 的**总处理时长比**已从约 **2 : 1** 降到 **25 : 18**（≈1.39×）。剩余缺口（约 1.1×）经 KI-14 第 9/12 轮结案确认为**原生逐车移动 × 存档规模**（`ctrl ≈ 移动车数 × 链长 × 1.9 µs`；42 555 车 / 911 链 / 链均 46.7 节），R3R 自有计时桶仅占 ~9~15 %（`plat+resv+edgeGate`）。继续压缩必须动「站台重预留 / 停稳早退」这类**与功能语义强耦合**的路径（该块本身就是 KI-05/06 的修复），性价比与风险不划算。
- **搁置对象**：KI-14（②③未验证部分）、KI-19、KI-26，以及 KI-17/18/27（账目卫生）——即**整个性能维度**。
- **不搁置（已完成、长期有效）**：KI-23/24/25/29（构建工具链）、KI-28（探针开关）、KI-15/16（构建规范）。
- **口径备注（防重踩 KI-23/KI-25 覆辙）**：25 : 18 的取数前提是两版**同为 Release**、**同存档**、**同运行状态**、读**同一仪表**；日期 2026-09-14。日后若恢复性能工作，必须先复核该口径。
- **搁置边界**：≠「不再碰任何与性能相关的代码」。修 KI-05/KI-06 会**顺带**消掉 KI-14 ①的死循环刷屏开销，属功能修复的正收益，**不算「恢复性能工作」**。
- **遗留资产**：09-14 性能支线改动（两层开关 `src/r3r_perf.h`、`resid` 四子桶、`nl=`、裸 `fopen` 收编）与 KI-29 的 `CMakeLists.txt` 修补**尚未经一次成功的全量重编验证**，且改到了 `src/*.h`（按 KI-15，下次必须全量重编）。日常功能测试请带 `R3R_DBG=0`，否则探针白付约 5.2 ms/帧。

### 3-2 2026-09-14（回退轮）拍板：回退到 09-09 基线 —— **已于 2026-09-15 01:12 整体撤销**

- **原始决定**：09-10~09-14 的改动「像狗啃过一样」——反复打补丁、多数未验证、且长期停在「混合 obj / 未编进手上 exe」的状态。用户决定放弃全部工作区改动，回到已验证的 09-09 checkpoint 重新出发。
- **回退目标**：`f3ebaad870 R3R: 2026-09-09 checkpoint`（分支 `feature/decouple`）。选择依据：该 commit 保留了已拍板确认的「consist 概念层拆除」（`consist_group.{cpp,h}` 已删、`IsConsistGroup` 全代码库 0 命中），是 `d824c8ec38` 之后唯一带正常 message 的检查点。
- **WIP 已完整备份**：工作区改动提交为 `2db952d332`（18 文件），分支 `r3r_backup_20260914`。取回方式 = `git reset --hard r3r_backup_20260914`。
- **回退后源码核对**：`r3r_perf.h` 不存在；`r3r_priority` / `r3r_orders_borrowed` / `R3RSyncDrivingOrders` / `R3RRebuildCouplePriorities` 全代码库 0 命中；`IsConsistGroup` 0 命中。仍存在：`R3RFlipChainBySegments` 三候选（`train_cmd.cpp:4655/4707/4747`）、`R3RCheckChainFold`（`4072`）、`R3RCheckChainFoldedDirection`（`4115`）、`R3RIsCarOnlyFormation`（`1772`）、`R3RDestroyCarOnlyFormation`（`1751`）、`PositionHelper`（`newgrf_engine.cpp`）、`CmdMakeSegment`/`CmdDemoteSegment`（`vehicle_cmd.cpp:530/617`）。
- **回退时的构建卫生**：两棵树 `*.obj` 已清零；两棵树 exe 分别改名 `openttd_20260914_wip.exe` 备份，故下一次构建必然是全量（属预期）。09-15 00:23 回退树全量重编通过（981 步，`NINJA_EXIT=0`，`build\openttd.exe` 50 171 904 B @ 00:22:35；`obj count=619`、`stale obj=0`）——第一份与回退后源码严格一致、无混合 obj 的干净基线 exe。
- **回退的副作用**：① `R3R_release_build.cmd` 的 `[5d]` 硬闸门会因 `CMakeLists.txt` 的 R3R 补丁被丢弃而报 `EXIT_CODE=94`（KI-29 失效）；② 09-10「VehicleView 悬空跟随」崩溃修复（四文件）随之丢失 → 该崩溃重新生效，立为 **KI-32**；③ 探针体系（`r3r_perf.h` + KI-14/17/26/27/28）连同文件一并消失。
- **⚠️ 撤销**：**2026-09-15 01:12 工作树被整体恢复为 `2db952d332`**（09-10~09-14 WIP：路线 A 排程借用 + artic 角色快照 + `r3r_perf` 探针；同批文件 mtime 全 = 01:12:28，是 `reset/checkout --hard` 的特征），`git status` 干净、HEAD = 该提交。**「回退到 09-09 基线」已被撤销**。01:13 起全量重编，01:40:10 产出 `build\openttd.exe` 50 474 496 B，版本串 `r3r-wip-2026-09-14 (0)`。上面「回退后 = 未修」的对照**全部失效**，不再代表当前代码。

### 3-3 2026-09-14 复测口径警示：「这问题之前不是解决了吗、怎么又冒出来」

**不是回归，是「以为修了但没复测闭环」+「修了但没编进手上的 exe」**。四件硬证据：

1. `build\openttd.exe` = 09-13 02:10（50 222 080 B），`findstr` 实测二进制内**有** `COUPLE-FLIP-BOTH` / `COUPLE-FLIP-U`，即 KI-06 的三候选逻辑翻转确实编进去了；
2. 但 `src\train_cmd.cpp`、`src\train.h` 已是 09-14 18:43（比 exe 新一天），且 09-14 既改过 `train.h`（KI-01 回归修复）又改过 `CMakeLists.txt`（KI-29）⇒ 按 KI-15 **必须全量重编**，而当时是「零次成功的全量重编」；
3. `git status`：`src/train_cmd.cpp`、`src/train.h` 仍为 ` M` 未提交，`train_cmd.cpp` 最后一次 commit 仍是 `f3ebaad870`——09-10~09-14 全部修复只存在于工作区；
4. KI-06 在本清单中**状态自始至终是 `未修`、从未标 `已修`**；09-06 决策 P5 明确「artic 端点错位死循环不做专项处理，认为候选 4 逻辑翻转即解开」——属「以为解决」，无复测闭环。

⇒ 结论：**先做 P1（候选 2 不提前 `return`）→ 再跑成一次全量重编 → 给 exe 打版本标记，使「测的那版 = 刚改的那版」**；在此之前不要再用日志做 KI-06 归因。

### 3-4 2026-09-14 回归排查：「九月修过的又冒出来」的根因 = 新增的路线 A 借用状态全 NOSAVE

- **九月修复仍在、未被性能支线破坏**（逐行核对通过）：`yapf_rail.cpp:150` 的自身豁免（`if (best->index == Yapf().GetVehicle()->index) return true;`，KI-05 的 depot 锁死修复）、`reach()` / `rp()` 的 `cur != st` 放行（KI-05）均原样存在；性能支线对 yapf 只做了 `fopen → R3RFopenDbg` 收编，无逻辑改动。
- **真正的回归 = 性能/功能支线新增的整合块「路线 A couple priority / 排程借用」**（`r3r_priority` / `r3r_orders_borrowed` / `orders_backup*`，`vehicle_base.h:379-382`）。**决定性证据**：在 `src/sl/` 全量搜索这三个符号 **0 命中**，即这些运行时状态**全是 NOSAVE**。后果链：连挂时存档 → 读档后每条链的 `r3r_priority` 回落默认 1、`r3r_orders_borrowed` 清为 `false` → `DecoupleTrain` 的整个交接块因 `r3r_orders_borrowed == false` **静默回退到旧路径** → 机车取回自己那条排程 → 机车回 depot 执行 `GOTO_COUPLE` 却找不到车底 → **每 tick `COUPLE-FAIL` 刷屏（KI-05 旧现象）**，同时交接丢失（= KI-02）。**判据**：现象只在「连挂状态下存档 → 读档」后出现；不存档的同一会话内不发生。
- **修复（该轮落地，`train.h` + `train_cmd.cpp` + `sl/vehicle_sl.cpp`）**：读档后从**仍然存活的排程指针**重建借用/优先级关系——链头正在借用 ⇒ 它的 `orders` 指针等于某个**非链头段**的 `orders`，该段即「命令所有者」；所有者取 priority 1，其余按物理顺序递增；`r3r_orders_borrowed = (owner != chain)`。链头自己被停放的那条排程**无法恢复**（存档时已无任何车辆引用它，故未被写出），因此 `orders_backup` 保持 `nullptr`，并在 `R3RSyncDrivingOrders()` 的归还分支**加空备份保护**。入口：`AfterLoadVehiclesPhase2`（`vehicle_sl.cpp:498`）→ `if (part_of_load) R3RRebuildCouplePriorities(t);`（仅真实读档，NewGRF reload 不动活动状态）。
- **编译状态**：单文件增量编译 `NINJA_EXIT=0`、lint 0 错误（唯一 `C4267` 为存量告警）；但因改动 `src/train.h`，按 KI-15 必须全量重编。
- **未验证**：读档 → 解挂的交接行为、`COUPLE-FAIL` 是否消失（见 KI-01 / KI-05）。

---

## 四、09-15 各轮复测判读与落地

### 4-1 第 13 轮：`test4.sav` 同存档复测 —— 「只看 direction」判据无效 + 引出新崩溃 KI-33

- **存档**：`test4.sav`（`C:\Users\冯洁敏\Documents\OpenTTD\save\`），现场 `chains=2 vehs=9 maxChain=6 segs=0 artic=6 waitCouple=1`（3 列三节铰接车 / 9 vehicle），1920-01-21 载入 → 1920-02-07（约 46 s / 1175 tick）必崩。
- **口径事故（务必记住）**：01:12:28 工作树被整体恢复为 `2db952d332`。故 01:40 产出的 exe（50 474 496 B，版本串 `r3r-wip-2026-09-14 (0)`，Build `Sep 15 2026 01:15:03`）实际 = **WIP 树 + 探针 ON**，而非回退树。

| 探针 | 修复前（00:22 exe，09-09 基线，判据 `worst_gap>8 \|\| DIR`） | 修复后（01:40 exe，WIP 树，判据只看 DIR） |
|---|---|---|
| `FOLDCHK-DIR` | 0（被 `\|\|` 短路吞掉，一次都没执行） | 44 |
| `FOLDCHK-ACCEPT` | 0 | 0 |
| `CPL-ENTRY` / `CPL-PATHFOUND` | 6 / 6 | 28 / 28 |
| `RESERVECONSIST` | 30 570 | 72 |
| `COUPLE-OK` / `COUPLE-FAIL` | 5 / 2 | 1 / 1 |

- **结论 1：判据换了，死循环通路原封不动**。`RESERVECONSIST` 刷屏只是换成 `CPL-*` + `FOLDCHK-DIR` 刷屏（同一耦合点 `39,33` 每 tick 重来）。注意 `RESERVECONSIST` 30 570 → 72 **不能当收敛**：修复后那次 46 秒就崩了，两次运行时长不可比。
- **结论 2：方向判据本身是错的（不是判太多，是「判错」）**。① 零容差 `dot > 0` 把残余未贴合间隙当折叠（`FOLDCHK-DIR COUPLE` 报 `dot=27`，这是 `ArrangeTrains` 只重链不重算位置的正常中间态）；② 判据**不是 flip-invariant**：候选 2 报出的最差对是链内相邻、仅差 4px 的 artic 父子对——翻向前 `dot=-4`（健康）、翻向后 `dot=+4`（被判折叠）⇒ 只要「只反 direction、不动位置」，链上每一对的符号都会翻转，判据必然全否。
- **结论 3（新崩溃，立为 KI-33）**：同一次跑 46 秒后崩于 `train_cmd.cpp:8627` 断言，崩前正是那串 `CPL-*` 死循环。含义：**光靠「判据否决 + 回滚 + 下一 tick 重试」并不能保证畸形链不被 `TrainController` 拿去开车**。
- **本轮修复（用户拍板「保留 WIP 树 + P1&P4 + 我编好后你手动跑」）**：仅改 `src/train_cmd.cpp`（增量 24/24 步，`LNK` 成功，无新 warning），exe = `build\openttd.exe` **50 474 496 B @ 2026-09-15 02:13:57**，版本串 **`r3r-wip-2026-09-14-m`**（与 01:40 那版可据版本串区分）。
  - **[P4]** `R3RCheckChainFoldedDirection` 循环内加 `if (b->IsArticGroupMember()) continue;`（`IsArticGroupMember()` = `IsArticulatedPart() || VehicleRailFlag::ArticGroupMember`，`train.h:175`，同时覆盖真 artic 部件与 R3R 假引擎组员）。依据：逻辑翻 v 反转全部 `direction` 而位置不动，于是**每一对**的 `dot` 符号都跟着翻——健康的 `-4 px` 组内对变成 `+4` 被读成折叠；而实测两候选的 worst dot **都是同一个 4**（机车自己的部件对），全链再无其它 `dot>0` 的对 ⇒ 这个 4px 假阳性就是 KI-06 死循环仅剩的支柱。跨段拼接点（`b` 是下一组的父车/组头，非组员）照旧检查。
  - **[P1]** 候选 2 的结构守卫不再提前 `return false`：`v_flip_head != v && !v_flip_head->IsEngine()` 时改为「回滚候选 2 → 继续落到候选 3」，由候选 3 自己的同一守卫给结论。新增日志 `FOLDCHK-SKIP COUPLE-FLIP-V / COUPLE-FLIP-BOTH v_head_not_engine`。
  - **P2（全局符号一致性判据）有意放弃**：本存档数据**反证**它会误判——候选 2 只翻 v，v 的组内对由负变正、u 未翻其组内对仍为负，全链必然「正负混杂」，健康的候选 2 会被直接否掉。故 P4 只保留「跳过组内对」这一条，行为改动最小且可解释。
- **本轮复测预期（同存档）**：① `COUPLE-FLIP-V/BOTH` 的 `FOLDCHK-DIR` 行应消失，候选 2 应 `ACCEPT`；② `FOLDCHK-ACCEPT > 0`；③ `COUPLE-OK` 很快出现，不再每 tick 重复 `CPL-*@39,33`；④ 不应再崩 `train_cmd.cpp:8627`；⑤ 若出现 `FOLDCHK-SKIP`，说明真的走到了候选 3 结构不可达分支，需回头核对。

### 4-2 第 13 轮实测结果（02:13 exe）：预期 ①~③ 全部兑现，KI-06 结案、KI-33 未复现

① 候选 2 的 `FOLDCHK-DIR` 行消失（只剩 `COUPLE` / `COUPLE-FLIP-U` 两条跨段真否决），被 `FOLDCHK-ACCEPT` 放行；② `FOLDCHK-ACCEPT = 1`；③ `CPL-ENTRY` 全程 **1 次**（上一版 28 次）、`COUPLE-OK` 紧随出现；④ 运行 ≥136 秒**无 crash 文件**，KI-33 未复现；⑤ `FOLDCHK-SKIP = 0`（候选 2 结构可行，P1 分支未被触发）。

| 探针 | ① 修复前（00:22 exe） | ② 01:40 exe | ③ 02:13 exe（WIP + P1&P4） |
|---|---|---|---|
| `FOLDCHK-DIR` | 0 | 44 | **2** |
| `FOLDCHK-ACCEPT` | 0 | 0 | **1** |
| `FOLDCHK-SKIP` | 0 | 0 | 0 |
| `CPL-ENTRY` / `CPL-PATHFOUND` | 6 / 6 | 28 / 28 | **1 / 1** |
| `COUPLE-OK` / `COUPLE-FAIL` | 5 / 2 | 1 / 1 | 2 / 5 |
| `RESERVECONSIST` | 30 570 | 72 | 88 |
| 崩溃（`train_cmd.cpp:8627`） | 无 | **有**（46 s） | 无（≥136 s） |

**事件链（行号 = `build\R3R_debug.log`）**：

1. **L132 `CPL-ENTRY`**：机车 0 在耦合点（`39,33`）进 `TryTrainCouple`。上一版此处起就每 tick 循环，本版全程只有 **1 次**。
2. **L173** 一次 `COUPLE-FAIL loco=0 order=16 tx=37 ty=28`（机车尚未到达耦合点，属正常重试，非折叠否决）。
3. **L199-200 `FOLDCHK-DIR COUPLE dot=27 A idx=2 (632,506) dir=3 ↔ B idx=3 (632,533) dir=3`** —— 基线候选被拒，**跨段拼接点**，`dot=27` 就是「只重链不重算位置」的残余间隙。
4. **L226-227 `FOLDCHK-DIR COUPLE-FLIP-U dot=17 A idx=2 (632,506) dir=3 ↔ B idx=6 (632,523) dir=7`** —— 候选 1 被拒，仍是跨段点且两侧 `dir` 相反。
5. **L240-241 `FOLDCHK-ACCEPT COUPLE-FLIP-V worst_gap=25 residual gap (chain still closing up), not a fold`** —— **候选 2 首次被 ACCEPT**。决定性细节：候选 2 的 `FOLDCHK-DIR` **不存在**，即上一版唯一的否决项（artic 父子对 `A idx=0 (632,514) dir=7 ↔ B idx=1 (632,510) dir=7 dot=+4`）已被 P4 跳过；剩下的只是残余间隙，被 `FOLDCHK-ACCEPT` 记下并放行。
6. **L242 `COUPLE-OK loco=0 rear=8 consist=3 co=1 real=1 type=1 tx=39 ty=32`** —— 挂车成功。
7. **L256 `DECOUPLE-FIRE consist=0 tx=39 ty=32 real=1`**；**L276 `CRT-FOLD`**（解挂后换端折叠检查，单次即过，无循环）。

**结论**：

1. **KI-06 已修（实证）**：`FOLDCHK-DIR` 44→2、`FOLDCHK-ACCEPT` 0→1、`CPL-ENTRY` 28→1，「每 tick 回滚重试」的循环消失。
2. **P4 精准生效且未削弱跨段检查**：被跳过的只有 artic 父子对（`dot=+4` 假阳性）；真正的跨段点（`dot=27` / `dot=17`）照旧否决。
3. **KI-33 未复现**（≥136 s 无 crash）⇒ 判定为 KI-06 死循环的下游表征，随折叠修复一并消失。
4. **尾部遗留（已登记 KI-34，与折叠无关）**：挂车成功（L242）→ 解挂（L256）后，机车 0 取回自己的排程（`LOCO-ORD 0 type=16` = `GOTO_COUPLE`）→ 回到 **38,27 车库**，而目标车底（veh 3-8）已停在 **39,32/39,33** → 反复 `COUPLE-FAIL` + `RESCHECK v=0 at=38,27 resAhead … = 0x0`（L368/377/390/403），并伴随 `SKIP-STOPPED veh=0 order=5 real=16 spd=0 tile=38,27` 与持续的 `RESERVECONSIST veh=3..8`。日志行数被 `R3REDGE_COUPLEFAIL` 闸门按 128 帧窗口限流，**实际尝试频率 > 日志行数**。属解挂后排程归属问题（记忆 18491399 / 67811241）。

### 4-3 第 14 轮落地（KI-06 判据收敛 + KI-33 源头消除断言 + KI-34 解挂恢复点）

**对象**：`build\openttd.exe` @ 2026-09-15 16:38:34；`src\train_cmd.cpp` 16:37:33 → obj 16:38:02 → exe 16:38:34（仅 `.cpp` 改动，增量编译即可，不触发 KI-15）。改动量 +185 / −40，相对 HEAD `2db952d332`，**尚未提交**。

1. **KI-06 判据收敛**：`TryTrainCouple` 新增 `SpliceFolded(splice_prev, tag)` lambda（`train_cmd.cpp:5232`），把「是否折了」拆成两个互不干扰的判据 —— `ChainFolded`（`:5197`）**只有方向能否决**（`R3RCheckChainFoldedDirection`），旧 `worst_gap > 8` 降级为纯诊断（打 `FOLDCHK-ACCEPT`）；`SpliceFolded` **只查拼接对本身** `(splice_prev, splice_prev->Next())`（并要求 `Next()->Previous() == splice_prev` 以防未真正连上），`expected = (len_a+len_b)/2`、`dist = max(|Δx|,|Δy|)`，`|dist-expected| > 8` 才判折（打 `SPLICE-GAP-REJECT`）。四个判定点全接上：`COUPLE`(`:5257`)、`COUPLE-FLIP-U`(`:5334`)、`COUPLE-FLIP-V`(`:5392`)、候选3 末位兜底(`:5475`)。
   **为何弃用整链 `worst_gap`**：它必有两类误报 —— (a) artic 父子对渲染在父车**精确 x/y**（`dist=0` 对名义 `exp=2~5`）；(b) `ArrangeTrains` 只重连链、不重算位置，刚重链进来的对保留拼接前间距。判据收窄到「合并新造的那一对」后，这两类误报不可能命中，而真错翻必命中。
   **实测判据**（`test4.sav` 09-15，机车鼻顶车组尾，全 `dir=3`）：`COUPLE-FLIP-V` 拼接对 `idx0(632,514) ↔ idx3(632,533) exp=2 dist=19`（真错翻）；`COUPLE-FLIP-BOTH` 拼接对 `idx0(632,514) ↔ idx8(632,515) exp=2 dist=1`（自洽）。接受 19px 那个会把链序变成「机车尾 → 车组鼻 → 反向穿车组身体」，而**每个局部对方向仍是负的**，方向判据看不见 → 六节车厢叠在同一瓦片而机车单飞。
2. **候选3 从「假定不可行」改为「实测不可行」**：旧代码在候选2 遇结构守卫时**直接 `return false`**，顺带把候选3 判死，而候选3 从未被试过。现改为：候选2 回滚 + `FOLDCHK-SKIP … fall through to candidate 3`，落到候选3 由**同一个守卫**（`:5426`）给结论（`FOLDCHK-SKIP … give up this tick` 后放弃）。
3. **候选3 末位兜底：非相邻拼接照样接受**：候选3 是最后一个候选，回滚只会「状态不变 + 下 tick 重试」= KI-06 死循环本身。故 `:5467-5477` 方向判据仍**硬否决**（真折绝不提交），**只有拼接间距从「否决」降为「记账」**（`SPLICE-GAP-LAST-RESORT`，→ KI-35）。
4. **KI-33 源头消除（不再崩溃）**：`TrainController`（`train_cmd.cpp:8876`）原 `dbg_assert(IsValidDiagDirection(exitdir));` 改为 if/else：`exitdir` 非法（R3R 合并链尚未收拢、`prev` 隔 ≥2 瓦片）时取 `chosen_track = _connecting_track[enterdir][enterdir]`（**直行**），再交给 `chosen_track &= bits` 与 `TRACK_BIT_NONE` 分支 —— 过不去就原地不动，不再断言中止。
5. **KI-34 相关：解挂恢复点改按「谁拥有排程」判定**：`DecoupleTrain`（`train_cmd.cpp:4463~4534`）新增 `const bool u_owns_running = u_inherited_running || (u->orders != nullptr && u->orders == v->orders);`（**必须在 `R3RSyncDrivingOrders(v)` 之前取**，否则借用指针归还后比较失真）；恢复 `WAIT_COUPLE` 整块由 `if (u_inherited_running)` 改为 `if (u_owns_running)`；而「列表里没有 `WAIT_COUPLE` 就在最前插一条」的分支收窄为**只对 `u_inherited_running` 成立**，**不再改写玩家自己的排程**。现场：让「u 保留自己的排程」成为常态 → 恢复逻辑恒假 → 解出方不再被钉上一条凭空插入的 `WAIT_COUPLE`，其排程与当前索引保持原样（这正是第 15 轮观测到的“解挂后机车仍按自己排程回 depot”这一现象的来源）。

### 4-4 第 15 轮复测判读：第 14 轮三个待复测点全部通过

**对象**：第 14 轮 exe（16:38:34）。5 个会话、累计约 24 分钟，覆盖 depot 出发 / 站台挂车 / 解挂 / 换端。

| 复测项 | 判定 | 依据 |
|---|---|---|
| KI-33（`TrainController` 断言崩溃） | **未复现 → 保持「已修」** | 5 会话约 24 分钟无 `assert`、无 `crash-*.log` |
| KI-06（耦合折叠死循环） | **保持「已修」** | `CPL-ENTRY` 不再逐 tick 重复；`FOLDCHK-ACCEPT > 0` |
| KI-35（候选3 末位兜底） | 路径**未被激活** | `SPLICE-GAP-LAST-RESORT = 0`（本次场景无需走候选3 兜底） |
| KI-34（解挂后 `GOTO_COUPLE` 找不到车底） | **口径收窄 → 部分防护** | 3 次 `COUPLE-FAIL` 全部为**瞬态并自行恢复**，无刷屏、无 depot 锁死；但「目的地没有车底」这一机制仍在 |
| KI-36（`SpliceFolded` 8px 容差） | 无 ≤8px 边界错位样本 | `dist=19` 类已被拦；容差本身维持不移 |


### 4-5 第 16 轮落地：新特性「段右边界标记 `SegmentBack`」

**背景**：此前段只靠段首 ★（`VehicleRailFlag::SegmentFront`）单边界定 —— 段尾要么是链尾，要么是下一段的段首。这在「临时解出中间段 / depot 拖动单段」时无法表达「本段到此为止」。

1. **新增标记**：`VehicleRailFlag::SegmentBack`（`train.h`，bit15），落点 = 段**最后一辆车的末尾**（按块尾车，artic 块取块尾车）。
2. **落点规则**（`train_cmd.cpp`）：
   - 建立/拼接段时按块边界打标（`SegmentFront` 与 `SegmentBack` 成对）；
   - `R3RFlipChainBySegments` 段内反转时**两侧标记同时迁移**；
   - `TrainDepotGetSegmentFront` 依 ★ 定位段首，并按 `SegmentBack` 判定段尾。
3. **本轮修掉/新建的条目**：
   - **KI-38（已修）**：`R3RFlipChainBySegments`（`:5095`）的右边界迁移原按「单辆车」判定（`new_front == s.blocks.back().front()`），而标记实际落在段尾**块尾车**上；artic 块时二者不是同一辆车 → 迁移不生效，标记遗留在新段首块内部。改判为 `s.blocks.back().back()` → 迁到 `s.blocks.front().back()`；全为单辆块时与旧实现逐字节等价。
   - **KI-37（未验证）**：标记本体是新的存档 / 语义状态 —— 旧存档兼容、与真 artic 链混用、与 fold-fix 回滚交互**均未实测**。
   - **KI-39（未验证）**：`DecoupleTrain` 只 `u->ClearSegmentFront()` 清解出链链头 ★（`:4587`），不清解出链链尾的 `SegmentBack` → 单段解出后留下「孤儿右边界标记」。按当前代码推理 `TrainDepotGetSegmentFront` 对这类链一律返回 `nullptr`（等同「不是段」），但未实测。
4. **风险提示**：按 KI-15，本轮改动涉及 `src/train.h` ⇒ **下一次构建必须全量重编**，否则出现混合 obj。

### 4-6 第 17 轮落地：depot 链状态标记列 + 段拖动整段高亮 + 错误文案清理

**对象**：`build\openttd.exe` @ 2026-09-15 21:15:18，50 480 640 B（`R3R_quickcheck.cmd` 两个 TU + `_tmp_inc_build.cmd` 增量链接均 `NINJA_EXIT=0`）。改动仅 `src/depot_gui.cpp` / `src/train_gui.cpp` / `src/lang/english.txt` / `src/lang/simplified_chinese.txt`（+130 / −28），**未触碰 `src/*.h`**，故不受 KI-15 约束。未提交。

1. **任务 2（新增）depot 链状态标记列**：
   - 新增 `DepotWindow::tag_width` 与 `DrawChainStateTag()`：沿 `Next()` 数 `IsSegmentFront()`，`0` → `STR_DEPOT_CHAIN_LOOSE`（散链），否则 `STR_DEPOT_CHAIN_SEGMENTS`（`段：{NUM}` / `Segments: {NUM}`，`{NUM}` 为段数）。
   - 该列插在列车行首（与 header 列、count 列、图像区并列），**全链路同步**：`text` / `image` / `flag` / 第二处 `image` 的 `Rect` 缩进；`GetVehicleFromDepotWndPt` 的命中判定（`xm < tag_width` 视为点中车辆本体、counter 判定减去 `tag_width`、`x -= tag_width + header_width`）；`OnResize` 的 `base_width` 与 `hscroll->SetCapacity`。
   - 宽度取 `STR_DEPOT_CHAIN_LOOSE` 与 `STR_DEPOT_CHAIN_SEGMENTS`（`GetParamMaxValue(100, ...)`）两个 bounding box 的较大值 + `hsep_normal`；**非列车车库 `tag_width = 0`**，整列消失，零回归影响面。
   - 两条新字符串按 KI-16 的规矩**追加在语言文件末尾**，不动既有 ID；本轮先 `touch src/strings.cpp` 再重编，避免生成头 `strings.h` 陈旧。
   - 语义待确认：`segments == 0`（「单机车」或「造出来的散链」）一律显示「散链」，是否符合玩家预期需实测拍板（→ KI-43）。
2. **任务 3/4（新增）拖动段内任意车厢 → 整段统一高亮**：
   - **背景**：拖动语义本来就对（`TrainDepotMoveVehicle` 以段首 ★ 为单位搬运整段），但**视觉只从被抓车厢画到链尾**，与「整段被拖动」不一致。
   - `train_gui.cpp`：新增 `GetSegmentTail()`（判 `IsSegmentBack`，缺失时兜底「下一段首 / 链尾」，与 `TrainDepotDetachSegment` 同规则）；`DrawTrainImage` 新增 `sel_tail` / `sel_frame_done`，高亮帧延伸到段尾即停；`HighlightDragPosition`（插入位置的灰色预览）宽度同样以段尾封顶。
   - `depot_gui.cpp`：新增 `DepotWindow::GetDraggedSegmentFront()`；`DrawVehicleInDepot` 把**段首 ★ 的 index 当作 `DrawTrainImage` 的 `selection`**（抓段内哪节车厢都一样）；`CountDraggedLength` 按整段求和；`DepotClick` / `OnCTRLStateChange` 的 `_cursor.vehchain` 均改为 `_ctrl_pressed || GetDraggedSegmentFront() != nullptr`（**松开 ctrl 不会再把段拖动缩回单节**）。
   - 复用既有 `TrainDepotGetSegmentTail()`（由 `TrainDepotDetachSegment` 内联的段尾循环抽出，全为单辆块时逐字节等价）。
   - 散链 / 非 ★ 链头行为**不变**（`_cursor.vehchain` 仍取决于 ctrl），即「拖散链单节只搬单节」。
3. **KI-42（已修）错误文案**：`depot_gui.cpp` 中 6 处 `STR_ERROR_CAN_T_BUY_TRAIN + to_underlying(...)` 改为 `GetCmdBuildVehMsg(...)`。
4. **验证状态与复测清单**：编译 / 链接 / lint 均通过，**交互层零验证**（本模型无视觉能力，`R3R_shot_ocr.ps1` 只能定性，见 §六）。复测清单：① 散链 / 单段 / 多段链在 depot 列表的标记是否正确；② 抓段内第 2、3 节拖动，高亮是否覆盖整段、落点预览宽度是否 = 整段；③ 按 / 松 ctrl 不改变段拖动跨度；④ 拖动散链车厢行为不变（仍只搬单节，除非 ctrl）；⑤ 非列车车库（公路 / 船 / 飞机）行内容无位移。

### 4-7 第 19 轮（2026-09-16）：挂接分组 步骤 4 + 5 + 6

**对象**：`build\openttd.exe` @ 2026-09-16 00:39:24，50 595 328 B（`_tmp_inc_build.cmd` 增量编译 + 链接，`NINJA_EXIT=0`；`R3R_quickcheck.cmd` 两 TU 编译通过）。**注意**：本轮触碰了 `src/vehicle_base.h`（第 18 轮）与 `src/couple_group.h`（第 19 轮新增 `R3RGetChainScheduleOwner`）⇒ **触发 KI-15**，故本轮结尾按 KI-15 做了**全量重编**：`R3R_fullrebuild.cmd debug`（删全部 `*.obj`，691 步，00:43:07 起 / 01:15 完成，`EXIT_CODE=0`，exe @ 01:14:40 / 50 609 152 B）⇒ 混合对象风险已消除。完整设计与落地细节见备忘 `R3R_couple_group_design_memo.md` §13。

1. **步骤 4 —— 专用窗口 + 语言字符串**：`couple_group_gui.cpp` / `widgets/couple_group_widget.h`；`WindowClass::CoupleGroup` + `_couple_group_desc`（左组列表 / 右成员段列表 / 新建·改名·删除·移出按钮），组列表按 `R3RCoupleGroupIsVisibleTo` 过滤；全部新字符串按 KI-16 **追加在语言文件末尾**，本轮先 `touch src/strings.cpp` 再编。
2. **步骤 5 —— 硬约束接线（「不同组 = 视同没有等待挂接的列车」）**：`R3RCoupleAllowed` 从零调用点变为 5 处 —— `yapf_destrail.hpp:434`（E1，`TrainFitStation` 之前）、`yapf_rail.cpp:166`（E2，`FindSafeCouplePositionProc` 选定 `best` 后）、`train_cmd.cpp:5878`（E4，`GetCouplePosition` 命中后返回 `nullptr`）、`train_cmd.cpp:5955` / `5988`（E5/E6，`TrainCoupleHandler` 两处候选扫描 `continue`）。E3/E7 经确认被上述覆盖、无需改。**刻意不加探针**（每寻路每 tile 都会判、拒绝是常态，加了就是刷屏源，且与 KI-14 边沿闸门相冲）。未指派任何组时判据恒真 ⇒ **老存档零变化**。
3. **步骤 6 —— 可见性**：新增共用查询 `R3RGetChainScheduleOwner()`（链头 + 每个 ★ 为一段，`r3r_priority` 最小者为 owner，输出 owner / 1-based index / total）；`vehicle_gui.cpp` 车辆详情窗口按既有 `vehicle_*_line_shown` + `ShouldShow*Line` + `ReInit()` 逐行机制新增「挂接分组」与「命令归属：第 k 段 / 共 N 段」两行（单段且未指派 ⇒ 一行不显示）；`depot_gui.cpp` `DrawChainStateTag` 在链状态列加第二行（组名 + `k/N`，`ShortenCoupleGroupName` 按整 UTF-8 字符截断到 `tag_name_budget`，故 depot 宽度不随组名增长；列车 `min_height` 提到两行 small 字体）；`couple_group_gui.cpp` 右列表给命令归属段追加「（命令归属 k/N）」标记。**口径变更见 KI-50**。
4. **顺带修掉**：`train_cmd.cpp:3710` 的 `C4267`（`size_t → uint` 隐式窄化，`static_cast<uint>`）。
5. **复测清单（步骤 7，游戏内）**：① 未指派任何组的老存档 —— 挂接 / 解挂 / 车辆窗口 / depot 显示**完全不变**（最重要：零回归）；② 把两条车底分入不同组后贴在一起 —— 不挂接、不刷屏、机车继续找同组候选或原地等待；③ 把等待车底与机车分入同组 —— 正常挂接；④ 连挂后 depot 两行数字与车辆窗口「第 k 段 / 共 N 段」是否一致；⑤ 组改名 / 删除后车辆窗口与 depot 是否同步刷新（已知：改名变长需重开车辆窗口）；⑥ 非列车车库与 RTL 布局无位移。

### 4-8 第 25~26 轮（2026-09-16）：硬约束统一闸门 + 段身份语义 + 步骤 7 收尾核对

**对象**：`build\openttd.exe` @ **2026-09-16 19:23:23**，50 621 952 B（`_tmp_full_build.cmd` 全量重编，691 步，`EXIT_CODE=0`）。改动触及 `src/train.h` + `src/pathfinder/yapf/yapf_destrail.hpp` ⇒ 按 KI-15 删全部 `*.obj` 重编；时间戳核对：最新源码 `train_cmd.cpp` 18:52:33 / 最早 obj 18:54:54 ⇒ **无陈旧 / 混合对象**。完整细节见备忘 `R3R_couple_group_memo_20260916.md` §6，设计与收尾见 `R3R_couple_group_design_memo.md` §14。

1. **玩家实测两类越权耦合（KI-60）** —— 根因：三条被动方目标解析路径的判据都是 `R3RIsCarOnlyFormation(u) || u->current_order.IsType(OT_WAIT_COUPLE)`，「纯车厢段」短路**整体豁免了 `WAIT_COUPLE` 要求**，且三条路径都**不检查双方停止状态**（主动方在 `train_cmd.cpp:9892` 确实被挡，被动方无人把关）。落地：新增统一硬闸门 `R3RCanCoupleNow(coupler, target, site, why*)`（`train_cmd.cpp:5915`），四条件 = 主动方 `OT_GOTO_COUPLE` / 被动方 `OT_WAIT_COUPLE`（取消 car-only 豁免）/ 双方 `!vehstatus.Test(VehState::Stopped)` / `R3RCoupleAllowed` 分组白名单（`:5931`）；三条解析路径（`site` = `"geo"` `:5978`、`"depot"` `:6047`、`"scan"` `:6077`）全部改走闸门，被拒候选等同「此处没有等待挂接的车底」；新增边沿触发探针 `CPL-GATE`（tag `R3REDGE_COUPLEGATE`，`:4043`）。
2. **寻路侧豁免未同步收紧（KI-61）** —— 落地：统一谓词 `R3RIsCoupleTarget(t)` = `t->IsSegmentFront() && t->current_order.IsType(OT_WAIT_COUPLE)`（声明 `train.h` / 定义 `train_cmd.cpp:1826`），四处共用：站台预留扫描 `yapf_destrail.hpp:374`、`PfDetectDestination` `:448`（旧 Target 1 的 car-only 短路已删除）、back-walk `yapf_rail.cpp:156`（`fail=notWC` → `fail=notSeg` 并加打 `sf=`）、到达闸门（新拒绝原因 `target-not-segment`）。
3. **段身份语义（KI-62）** —— `DecoupleTrain` 末尾不再无条件清 ★，改为 `if (R3RIsCarOnlyFormation(u)) u->SetSegmentFront();`（`train_cmd.cpp:~4620`）。★（`VehicleRailFlag::SegmentFront`）是全代码库判定「链是段」的**唯一**依据（`depot_gui.cpp:542-546` 注释明确「不能看段数」），被抹掉即退回「散链」且不再被寻路瞄准。副作用核对（全部安全）：`GetSegmentHeadFromRear`(3732) / `GetSegmentBoundaryFromHead`(3759) / `R3RGetSegmentHeads`(3821) 均自 `GetNextVehicle()` 起遍历 ⇒ 链头 ★ 天然跳过；`R3RFlipChainBySegments`(5065) 段收集带 `w != chain` 排除链头；`CmdDemoteSegment` 仍可正常降级。
4. **`R3RCoupleAllowed` 单点复核（配合 KI-46）** —— 5 处接线点实测确认：`yapf_destrail.hpp:437`(E1)、`yapf_rail.cpp:170`(E2)、`train_cmd.cpp:5978` / `6047` / `6077`(E4/E5/E6，E4 经 `R3RCanCoupleNow`)；定义在 `couple_group.cpp:162`（内部交集判据 `R3RCoupleGroupMasksCompatible` `:115`）；旧名 `R3RCouplePairAllowed` 全代码库 **0 命中**。
5. **步骤 7 收尾（已完成部分）** —— 备忘与 KI 状态同步（本条 + §一摘要 + KI-45 / 46 / 53 / 60 / 61 / 62 / 63）+ 全量重编与一致性核对。**剩余只有玩家侧游戏内实测**，四组判据见 `R3R_couple_group_design_memo.md` §14.4：① 段解挂后车库应显示「第 k/N 段」且无需重升段即可再挂；② 纯车厢散链不得被寻路瞄准（`FSCP ... fail=notSeg ... sf=0` / `CPL-GATE reject=target-not-segment`）；③ 闸门三态（被停住不挂 / 无 `WAIT_COUPLE` 不挂 / 配对且同组正常挂）不刷屏、不死循环、不锁死 depot；④ 分组白名单与三处显示不回归。

**未做 / 遗留（不在本轮范围）**：解挂侧仍不拒绝散链（KI-08 —— `GetSegmentHeadFromRear` 返回空时回退原生 `GetDecoupleVehicleAuto` 逐车启发式，散链仍可能被 `DECOUPLE` 拆开）；`CG-PICK` 探针在当前源码中已不存在（`OnVehicleSelect` 直接干活、无探针），属命名沿革、非回归。

### 4-9 第 31~33 轮：按段链路刷新 / 按段运费结算 / Release 全量重编核验（2026-09-17）

**触发**：玩家提问「A、B 两地分别有车前往 C，在 C 连挂后共同前往 D，客货流怎么算」。

**决策者行**

| 决策 | 结论 | 轮次/依据 |
|---|---|---|
| 货运模型 | CargoDist 是**逐跳站对（link graph）模型，不认「连挂」**：节点=车站（按货种），边=相邻两站 | 第 31 轮调研 ⇒ KI-70 |
| 连挂影响货运的三条通道 | ①**链头 `orders`** 决定整列能接哪些货 ②**整列容量**决定该腿上报的运力 ③**谁在跑这条腿**决定边会不会在 `Compress()` 下衰减消失 | 同上 |
| 修复方案 | 玩家拍板 **方案 A「按段刷新」**：段被物理拖着**实际跑了** C→D，就算作它会跑 C→D（**不看段自己的排程里有没有 D**） | 第 32 轮玩家拍板 |
| 结算口径 | 玩家要求**运费也按段结算** | 第 32 轮玩家拍板 |
| 关键约束 | `IncreaseStats` 是 `EdgeUpdateMode::Refresh` = **取最大值保底而非累加** ⇒ 同一条腿必须**整链刷一次**，不能按段分刷（否则 3 段 ×50 会从 150 掉到 50） | 实现阶段自查，已规避 |

**落地**（设计与改动清单见 `R3R_per_segment_linkgraph_memo.md`）

1. **按段刷新（KI-70）**：`LinkRefresher` 把「路线来源」与「容量范围」拆开 —— `vehicle` 恒为链头（路线），新增 `scope_begin`/`scope_end` + `NextInScope()`。新增 `RunPerSegment`两遍走：**Pass 1** 用链头路线 + 整链容量刷一次（== 旧行为）；**Pass 2** 对「自带 `orders` 且与链头不同」的段，用**该段自己的路线**再刷一次、容量限本段，从而保住 A→C 这类「来路边」不被折半抹掉。段边界沿用 `Train::IsSegmentFront()`★。调用点 `economy.cpp:2388`、`vehicle.cpp:3625` 改走 `RunPerSegment`。
2. **按段运费结算（KI-71）**：`CargoPayment` 加 3 个 NOSAVE 字段（`r3r_recipient`/`r3r_recipient_base`/`r3r_booked`）+ `R3RSetPaymentRecipient(Vehicle*)`；在 `Stage`（中转费）与 `Unload`（交付费）前后开/关 scope，**立即**把该车贡献的 `visual_profit+visual_transfer` 差值入账到**该车所属段的段头** `profit_this_year`；`~CargoPayment` 只把「总额 − 已分出去的」记给链头 ⇒ **总量守恒**，不动 `route_profit`、不动公司总收入。
3. **口径修正**：按段记账用 `visual_*` 差值而非 `route_profit` 差值（`PayTransfer` 只加 `visual_transfer`，按 `route_profit` 记会把中转费错记到下一段）。
4. **构建（两轮）**：改了两个**头文件**（`src/linkgraph/refresh.h`、`src/economy_base.h`）⇒ 按 KI-15/KI-16 走 `build-release` 删全部 `*.obj` **全量重编**（691 步）⇒ `EXIT_CODE=0`（第 1 轮产物 `openttd.exe` @ 04:08:50 / 22 735 360 B，存档 `openttd_r33_full.exe` + `R3R_release_build_r33_full.log`）；随后按 5. 修 KI-73（仅 `.cpp`）**增量重编** ⇒ **交付实测版 `openttd.exe` @ 04:15:07 / 22 735 360 B**。两轮 `[5c]` 闸门均通过（无 KI-25 退化），未混入 Debug CRT；**KI-15 合规核验**：623 个 `*.obj` 最早写入 03:34:56 > 最新源码头文件 `src\linkgraph\refresh.h` @ 03:26:12 ⇒ `STALE_OBJ_COUNT=0`，无陈旧 / 混合对象。
5. **当轮修掉的新缺陷**：重编日志 C4150 暴露 KI-69 兜底路径 `delete` 不完整类型 ⇒ 析构不执行 ⇒ 登记并修复 **KI-73**。

**未做/遗留**：KI-72 ①「`front->orders_backup`（链头段自留计划）是否也纳入 Pass 2」待玩家拍板（归属已查清：恒属链头那一段，接线需让 `RunScoped` 的路线来源可传入 `const OrderList *`）；KI-72 ② 多段长链刷新成本（帧率）待实测。

**证据/入口**：`R3R_per_segment_linkgraph_memo.md`（含 §5 玩家实测清单 A~E 五组）；**交付实测版 = `build-release\openttd.exe` @ 2026-09-17 04:15:07（22 735 360 B）**；第 1 轮全量证据存档 `build-release\R3R_release_build_r33_full.log` + `build-release\openttd_r33_full.exe`（修复 KI-73 前，仅对照）。

⚠ **Debug 内部测试版尚未同步（本轮提交时的状态，2026-09-17 05:1x）**：`build\openttd.exe`（`CMAKE_BUILD_TYPE=Debug`、探针 ON、玩家日常玩的那个，见 KI-28 与 `R3R_fullrebuild.cmd` 头注释）时间戳仍是 **02:23:20**，**不含本轮按段刷新（KI-70）/按段结算（KI-71）**。原因：本轮改了两个头文件 ⇒ `build` 侧 619 个 obj 全失效，按 KI-15/KI-16 规矩必须 `R3R_fullrebuild.cmd debug` 删全部 obj **全量重编（约 25~40 分钟）** 才算同步。**并且**：`R3R_debug.log` 等探针日志只由 Debug 版产出，`build-release` 配置是 `R3R_PROBES_DEFAULT=0`（探针关闭）⇒ **本轮要取探针证据，必须先重编 Debug 版**；`build-release\openttd.exe`（04:15:07）只能用来试行为手感。

---

### 4-10 第 38 轮（2026-09-17）：多工作目录合并 + 交接文档建立 + C 盘清理

**触发**：玩家指出「曾在另一个工作目录里进行解挂版本开发，现在要转移回工作区（因 C 盘老是莫名其妙被占满）」。对应 **KI-83**。

**落点与结论**：

| 项 | 结论 |
|---|---|
| 旧落点 | `c:\Users\冯洁敏\CodeBuddy\20260914233818`（第 37 轮为「结果视图能显示」而同步文档的镜像目录） |
| 是否有独有源码 | **没有**。全目录只有 `.md` / `.ps1` / `.cmd` / `.txt` / `.log`，无 `src/`、无构建树 |
| 逐文件 MD5 比对 | 只有 2 个文件在工作区有同名版本 —— `R3R_KNOWN_ISSUES.md`（385 行 ↔ 工作区 **390 行**）、`R3R_crosscompany_design_memo.md`（15 534 B ↔ 工作区 **33 425 B**），**工作区版本都更新更全**；逐行 diff 后 C 盘侧独有内容仅 3 行草稿措辞，均已被工作区版本取代 ⇒ **无信息丢失** |
| 迁入 | `R3R_WIP_diff_清单.md`、`R3R_第30轮_车库第二次挂车修复.md` → 工作区根；`_d_small.txt`、`_d_train.txt`、`_ki_ctx_out.txt`、`R3R_perf.log` → `R3R_archive_20260914_wip\`（MD5 全部校验通过） |
| 删除 | 旧会话目录整体删除（18 个 `_*.ps1`、5 个 `_*.cmd`、若干日志/差异扫描件、2 个 `_r3r_*_preview.md` 旧预览、5 个 0 字节失败写入文件） |
| 新建 | **`R3R_handover.md`** —— 换工作目录/换会话第一入口 |
| 代码侧 | **零改动**（未碰任何 `src/*`）⇒ 不触发 KI-15 / KI-16，产物 `build\openttd.exe` @ 2026-09-17 16:23 继续有效 |

**C 盘占满的真因（登记备查，勿再误判为本工作区所致）**：C: 116.5 GB 已用 / 1.3 GB 可用，其中 `AppData` 42.7 GB（剪映 4.9 GB、WPS 3.6 GB、金山 3.0 GB、百度 4.3 GB、腾讯 2.7 GB、Microsoft 6 GB、GitHubDesktop 1.3 GB、NVIDIA 1.3 GB 等）、`Documents` 12 GB、`.workbuddy` 2.2 GB。**本工作区（源码 + `build` + `build-release` + 全部备忘）在 D 盘**，但 agent 的 `%TEMP%` 在 C 盘 ⇒ C 盘一满就会让编译 / 写盘 / 展示动作随机失败。长期对策 = `TEMP/TMP` 固定指 D 盘（`build\tmp` / `build-release\tmp`，即记忆 76468255 的 KI-24 铁律）。

**长期规则（本轮确立）**：所有 R3R 文档一律写 `d:\sourcecode of JGRPP` 根目录；**不得再为了「结果视图能显示」而把文档复制到 C 盘**（该动作本身就是 C 盘爆盘的直接原因）。工作区变更或功能阶段收尾时，更新 `R3R_handover.md` 顶部时间与轮次，不要把状态写散到多处。

**待办（本轮移交）**：①未提交改动已累积到 **25 改 + 9 新**（`couple_group*` 全套），建议先做 checkpoint commit；②P1/P2/P3 实测清单仍未有任何一条被玩家确认，见 `R3R_handover.md` §5。

---

## 五、性能维度历史与取数仪表（已搁置，仅存档备查）

### 5-1 结案口径（2026-09-14）

- **总处理时长比**：R3R : JGRPP ≈ **25 : 18（≈1.39×）**，已从约 2 : 1 下降。取数前提：两版**同为 Release**、**同存档**、**同运行状态**、读**同一仪表**。
- **剩余缺口的归属**：**原生逐车移动 × 存档规模** —— `ctrl ≈ 移动车数 × 链长 × 1.9 µs`。样本：42 555 车 / 911 链 / 链均 46.7 节。
- **R3R 自有计时桶**：`plat + resv + edgeGate` 合计仅 **~9~15 %**，故继续优化的性价比与风险不成立 → 见 §三-1。
- **参考样本**：`build-release/R3R_perf.log`（Release，`/O2 /Ob2`，探针在 Release 下默认关闭）。

### 5-2 KI-19 / KI-20 的 A/B 复测与改判

| 项 | vanilla JGRPP 0.72.4 | R3R | 说明 |
|---|---|---|---|
| `GL trains` handler 自身耗时 | 9.14 ms | 88.07 ms | 同存档、同运行状态 |
| 进入 handler 的列车数 | 907 | 911 | **几乎相同** |

- **改判**：原归因「R3R 把车厢链提升成了列车、导致入口数暴涨」被**证伪**。vanilla 自数 907 台前车**全为真引擎**，与 R3R 的 911 条链同量级 ⇒ 差值发生在 **handler 内部**，即「每 tick 站台重预留」+ 原生整链遍历，不是「多出来的假列车」。
- **决定性干扰（KI-23）**：上述 88 ms 是 **Debug（未优化）** exe 的数字。Debug vs Release 在这种重循环 / 虚调用密集代码上 5~20× 属常态，与是否使用 R3R 功能无关。⇒ **KI-14 各轮 / KI-19 / KI-20 的一切 A/B 倍率均不可当作 R3R 代码回归的证据**。
- **KI-14 第 1~12 轮的逐轮数据**（探针洪水收敛、`R3RDbgEdge` 边沿闸门、`DEPOT-ARR` 链快照、`resid` 四子桶等）保留在 `R3R_NEXT_STEPS.md` 与各轮 `R3R_perf.log` / `R3R_debug.log` 中，本清单不再逐轮抄录。

### 5-3 探针仪表要点（读账本前必看）

- **两层开关（KI-28）**：`R3RDbgOn()` 管日志、`R3RPerfOn()` 管计时器；默认按构建类型自动推导（Debug = ON / Release = OFF），运行时可用 `R3R_DBG=0` / `R3R_PERF=1` 覆盖。
- **日志写入**：新代码统一走 `R3RDbgWrite` / `R3RFopenDbg`（`src/r3r_perf.h`）；存量裸 `fopen("R3R_debug.log")` 的收编**未完成**（KI-17）。
- **桶的嵌套关系（KI-17 / KI-27，占比计算必须先去掉嵌套）**：
  - `resv ⊂ plat`、`coll ⊂ ctrl`；
  - `ctrl / spd / vp / plat / coupleH ⊂ loco`；
  - `ctrl` 也会在换端辅助函数中运行，`sub` 可略超 `loco`；
  - `resid` 只能当「未解释部分的上界」读。
- **日志体积（KI-18）**：残余体积主源是 `DEPOT-ARR` 的链快照（每 16 行一次）与 `CRT`；`fopen` 已移到签名闸门之后（只省 syscall），体积再降需给 `CHAIN` 子行加采样开关。

---

## 六、配套工具

| 工具 | 位置 | 用途 / 约束 |
|---|---|---|
| `R3R_AB_probe.cmd` | **工作区根目录** `d:\sourcecode of JGRPP\`（**GBK**） | A/B 取数入口：菜单 `[1]` 采集 R3R / `[2]` 采集 JGRPP 0.72.4 / `[3]` 生成报告 / `[4]` 改路径 / `[0]` 退出 |
| `R3R_AB_probe.ps1` | 与 `.cmd` **同级**（**纯 ASCII**，注释里的中文都不行） | 由 `.cmd` 用 `%~dp0` 定位；数据工作目录 `D:\r3r_probe\AB`（`config.txt` / `ab_save.sav` / `data_*.txt` / `report.txt`） |
| `R3R_fix_gbk.ps1 <file>` | 工作区根目录 | 编码复核：合格输出 `ASCII-OK` / `GBK-ALREADY`；输出 `CONVERTED->GBK` 说明刚救回一次。**改完脚本必跑** |
| `R3R_shot_ocr.ps1` | 工作区根目录 | 截图 OCR 兜底（`Windows.Media.Ocr`，zh-Hans-CN）；小字号数字误识率高，**只可定性、不可取数**（KI-22） |
| `R3R_fullrebuild.cmd` / `R3R_release_build.cmd` / `R3R_check.cmd` / `R3R_quickcheck.cmd` | 工作区根目录 | 构建与校验（KI-29/30/31 的修复均在脚本内） |

**编码铁律**：在本环境写盘一律存 **UTF-8**，而 PowerShell 5.1 按 ANSI(936) 读无 BOM 的 `.ps1`、`cmd.exe` 在 `chcp 936` 下读 UTF-8 的 `.cmd` 会从行中间开始执行垃圾 ⇒ **`.ps1` 必须纯 ASCII，中文只放 `.cmd`（存 GBK）**。

---

## 七、更新约定（长期规则）

- 用户 **2026-09-12 拍板**：R3R 新功能开发中产生的任何已知问题——未验证项、已知缺陷、硬伤、待复测点、被搁置的方向、工具链陷阱——**必须在同一轮内**写入本清单，**不得只留在对话或代码注释里**。
- **条目格式**：`ID | 一句话描述 | 来源（记忆 ID 或备忘章节） | 状态 | 严重度`。
- **状态取值**：`未修 / 部分防护 / 未验证 / 待复测 / 已搁置 / 已修 / 待实现 / 作废`。
- **严重度**：`高`（崩溃、卡死、丢数据）/ `中`（行为不符）/ `低`（瑕疵）。
- **修复后不删除条目**：改状态为 `已修` 并注明轮次；用户拍板搁置的标 `已搁置` 并注明拍板日期。
- **判定「已修」必须有实测证据**：编译通过不算；须给出探针计数前后对照或复测会话记录（见 §四-2 / §四-4 的做法）。
- **每次改动 `src/*.h` 后**：先更新本清单，再执行全量重编（KI-15）。


---

## 附：第 47 轮（2026-09-18）新增条目

| ID | 描述 | 来源 | 状态 | 严重度 |
|---|---|---|---|---|
| KI-95 | **`Station::loading_vehicles` 残留「已不存在车辆」的引用，导致存档在 `SlFixPointers()` 的 `STNN` 通道报 `Referencing invalid Vehicle` 而**完全读不进去**（`test_multi_company.sav`，玩家实报）。**定位链**：报错出自 `src/sl/saveload.cpp` 的 `IntToReference()` → `case REF_VEHICLE` 分支（`Vehicle::IsValidID(index)` 失败 → `SlErrorCorruptWithChunk("Referencing invalid Vehicle")`）；探针实测 `pool_size=0`、`raw_index=1`（zero_based=0）、`chunk=STNN`。`pool_size=0` 不是"车辆没加载"——读档期车辆是用 `Vehicle::CreateAtIndex()` 按索引创建的，`VEHS` 未列出的索引本就不存在，所以它只说明"这个索引对应的车真的没有"。**`STNN` 里唯一的 `REF_VEHICLE` 字段就是 `Station::loading_vehicles`**（`SLE_CONDREFVEC(Station, loading_vehicles, REF_VEHICLE, ...)`；`Ptrs_STNN()` 中其余 `SlObjectPtrOrNullFiltered` 处理的是货包 `REF_CARGO_PACKET` 与 waypoint，都不可能报 "invalid **Vehicle**"），故落点唯一。**根因**：`loading_vehicles`（本站正在装卸的车辆）正常进出三方——`economy.cpp` `PrepareUnload()` 压入、`vehicle.cpp` `Vehicle::LeaveStation()` / `Vehicle::PreDestructor()` 摘除——**都用 `vehicle->last_station_visited` 反查站点**。R3R 的拆链 / 编组 / 段升级降级 / 卖车路径若在"装卸途中"改写了链头身份或直接删除链头车辆，压入方与摘除方算出的站点就对不上（摘除打到别的站、成为空操作），条目永久残留；存档把这个指向已释放车辆的裸索引写进文件，下次读档就在指针修正阶段被判死。**处置（自愈防护，不改任何正常车辆行为）**：`src/sl/station_sl.cpp` 新增两个文件内静态函数——①`R3RPurgeInvalidLoadingVehiclesRaw()`（读档期：此刻表内元素尚未经 `SLA_PTRS` 转换，存的是 `ReferenceToInt` 的结果即"索引+1"的裸序号，因此**只做存在性判断、绝不解引用任何指针**，绝对安全）挂在 `Load_STNN_table()` 末尾与非表路径 `Load_STNN()` 末尾（**必须早于 `SlFixPointers()`/`Ptrs_STNN()`**）；②`R3RPurgeInvalidLoadingVehicles()`（存档期：此刻表内是真实指针，用 `Vehicle::Iterate()` 活车集合过滤悬垂指针，比对只用指针值、不解引用，对已释放车辆同样安全）挂在 `Save_STNN()` 开头。两者命中时各写一行 `SL-STNN-PURGE station=<id> raw=<n> (loader/saver) removed` 到 `build\R3R_debug.log`，便于后续定位真正的泄漏路径。**正常条目必定落在有效索引/活车集合内，一条都不会被误删**。仅改 1 个 `.cpp` ⇒ 按 KI-15 增量合规（未碰任何 `src/*.h`）。**实测证据（已修）**：`_tmp_inc_build.cmd` 末行 `[3/3] Linking CXX executable openttd.exe`、`build\R3R_incbuild.done` = `EXIT_CODE=0`、产物 `build\openttd.exe` @ **2026-09-18 05:33:09**（50 647 552 B）；`findstr SL-STNN-PURGE build\openttd.exe` 命中；无头加载 `-g Documents\OpenTTD\save\test_multi_company.sav -v null:until_exit` → `R3R_debug.log` 出现两行 `SL-STNN-PURGE station=0 raw=1 (loader) removed`，`R3R_slref.log` **不再出现任何 `INVALID_REF_VEHICLE`**，进程 50 秒后仍存活并打印 `LOADCENSUS-PHASE2END bad=0` 与 `DRAW-STATION-NORES tile=39,31` ⇒ **已进入游戏主循环，存档可正常加载**。**残留（未修，需后续轮次）**：(a) R3R 究竟哪条链编辑/删除路径留下了该残条目**尚未定位**（需要在真实运行里抓 `SETTLE-LOAD` 与摘除探针的现场日志，`R3R_debug.log` 的 `SL-STNN-PURGE` 行可作线索）——本轮只是兜底自愈，不等于根因已消除；(b) 存档侧的 `R3RPurgeInvalidLoadingVehicles()` **尚未实测**（需在游戏内实际存一次档、再读回验证，本轮无头加载只能覆盖读档侧）；(c) 诊断期在 `src/sl/saveload.cpp`（`CHUNKLOAD***` / `AFTER_LOADCHUNKS` / `BEFORE_PTRS` / `INVALID_REF_VEHICLE`）与 `src/os/windows/win32.cpp`（`ShowInfoI`）留下的写盘探针**仍在树里**，只影响加载一次的日志体积、对运行时性能无影响，待后续轮次决定去留。 | 2026-09-18 第 47 轮（玩家实报"存档读不进去" + `R3R_slref.log` 探针 + 崩溃日志） | **已修（自愈防护 + 读档侧实测通过）**；根因路径未定位、存档侧待实测 | 高 |

---

## 附：第 48 轮（2026-09-18）新增条目

| ID | 描述 | 来源 | 状态 | 严重度 |
|---|---|---|---|---|
| KI-96 | **车库内把已耦合的链拖开拆成独立列车后，拖出部分「排程被清空」且「车号被改掉」**（玩家实报：拖出来的列车没有调度命令，且车号从「Train 1」变成「Train 3」）。**现象 A（排程丢失）**：`CmdMoveRailVehicle` 的「front engine gets trashed」分支（`train_cmd.cpp` `:2579-2631`）按 vanilla 逻辑把 `src` 的 `orders` 转交 `src_head`，随后对 `src` 调 `DeleteVehicleOrders`。R3R 路线 A 下机车挂车时把自己的排程停进 `orders_backup`、当前跑的是车组排程（`r3r_orders_borrowed = true`），于是 ①转交出去的是**借来的**车组排程，②`DeleteVehicleOrders(src)` 又把 `src` 自己那份（及链表）清掉；当 `src_head == src`（被拖出的车恰好就是结果链头）时更会**自赋值后自删**，列车出库即空排程。**现象 B（车号被改）**：`Couple` 时被动车组把自己的号交给合并链首、自身号被置 0，却没留备份（`unitnumber_backup` 之前只给机车侧写过），拆开后车组只能从号池重新领号（`GetFreeUnitNumber`）⇒ 旧号作废、显示为新号。**修复（只改 `.cpp` ⇒ 按 KI-15 增量合规，未碰任何 `src/*.h`）**：①`train_cmd.cpp` `:2600`：转交前若 `src->r3r_orders_borrowed`，先把 `src->orders` 由 `orders_backup` 换回（清两个 backup index、清借用位），再**只在** `src_head->orders == nullptr` 时转交并 `AddToShared`；②`DeleteVehicleOrders(src)` 包进 `if (src_head != src)`，杜绝自赋值后自删；③`R3RSyncChainAfterDepotEdit`（`:4113`）在原有 `R3RRenumberPriorities` + `R3RSyncDrivingOrders` 之后补两步——**车号取回**：`!r3r_orders_borrowed && unitnumber_backup != 0 && IsFrontEngine()` 时先 `ReleaseID(当前号)` 再写回 `unitnumber_backup` 并 `UseID`；**排程兜底**：`!r3r_orders_borrowed && orders == nullptr && (IsFrontEngine() || orders_backup != nullptr)` 时，优先取自己的 `orders_backup`，否则沿链找第一个仍有排程的车作 donor，采纳其 `orders` 与三个 index（含 `cur_timetable_order_index`）、`DeleteUnreachedImplicitOrders()`、`AddToShared(donor)`、清 backup、`InvalidateVehicleOrder`；④`vehicle_cmd.cpp` `R3RPromoteFreeWagonChainToFront`（`:274`）：`unitnumber == 0` 时先看 `unitnumber_backup`，有则用它（`UseID`），否则才退回 `GetFreeUnitNumber`；⑤`Couple`（`train_cmd.cpp` `:5984`）：借出车号的一方（`u`）若 `unitnumber_backup == 0` 先把本号存入 backup，被动方（`v`）继续存 `v->unitnumber_backup = v->unitnumber`，保证双方都留档，拆链/解耦时可各取回自己的号。**编译**：仅改 `train_cmd.cpp` / `vehicle_cmd.cpp` 两个 `.cpp`，`_tmp_inc_build.cmd` → `build\R3R_incbuild.done` = `EXIT_CODE=0`；产物 `build\openttd.exe` @ **2026-09-18 06:11**（50 647 552 B）；KI-15 陈旧对象核对：源码 `train_cmd.cpp` / `vehicle_cmd.cpp` 均 06:07 < obj 06:09 < exe 06:11 ⇒ 无混合对象。**实测（未做，待复测）**：在 `net7probe.sav` 场景里把已耦合的车底从机车拖出，确认①拖出部分携带正确排程、②车号与耦合前一致（不再跳号）。**残留（未修）**：`unitnumber_backup` 与 `orders_backup*` 仍是 **NOSAVE**（`src/saveload` 零命中）⇒ 读档后备份丢失，本轮的车号/排程取回逻辑只在本会话内有效，跨存档仍会退化（与 KI-02 同根，见记忆 35644846）。 | 2026-09-18 第 48 轮（玩家实报"拖出来没命令、车号被改"） | **已修（编译通过；运行时未验证）** | 中 |
| KI-97 | **KI-96 的 NOSAVE 残留：借用关系跨存档后「差一半」——借位能重建、被停放的机车排程永久丢失、车号备份归零。** 三个字段 `orders_backup` / `orders_backup_real_index` / `orders_backup_implicit_index` / `unitnumber_backup` / `r3r_orders_borrowed` / `r3r_priority` 在 `src/saveload/**` 与 `src/sl/**` **零命中**（已 grep 确认）= 全部 NOSAVE。读档后 `R3RRebuildCouplePriorities()`（`train_cmd.cpp:3987-4009`）按「链头 `orders` 指针 == 某非头段的 `orders` 指针」重建 `r3r_orders_borrowed` 与优先级（指针确实活得下来，见注释 3973-3978），但显式把 `chain->orders_backup = nullptr`（4006）⇒ **机车自己的排程再也找不回来**。随后解挂时 `R3RSyncDrivingOrders()` 的"归还被停放的排程"分支（`train_cmd.cpp:4041`）因 `orders_backup == nullptr` 只能"保留机车正在跑的东西"（即车底的排程，见注释 4042-4045）⇒ 玩家可见：**挂车状态下存档 → 读档 → 解挂，机车不回自己的路线，继续跑车底那份排程**。同理 `unitnumber_backup` 读档归 0 ⇒ KI-96 新加的取号守卫（`DecoupleTrain` 换号块与 `R3RPromoteFreeWagonChainToFront` 的取回分支）双双失效 ⇒ **改号症状跨存档必现**。**新发现（纠正一处代码注释）**：`train_cmd.cpp:3980-3983` 写"the head's own parked schedule ... was never written out"，按现在的 `Save_ORDL()` 实现（`src/sl/order_sl.cpp:566-574`：`for (OrderList *list : OrderList::Iterate())`，**遍历整个 OrderList 池、不筛有没有车辆引用**）这句不成立——被 `orders_backup` 独占引用的列表仍会被当作正常条目写进存档，读档后 `Load_ORDL` 也照样把它建出来，只是没有任何车辆的 `orders`/`orders_backup` 指向它 ⇒ 变成**永不释放的孤儿 OrderList**（不崩，内存小泄漏；"挂车→存档→读档→再挂车"每循环一次多攒一条，ORDL 条目缓慢膨胀）。这个纠正直接给了廉价修法：**存档格式零改动**，在 afterload 阶段扫一遍 OrderList 池，把"无任何活车引用"的列表按启发式（同 owner、订单序列与机车当前所跑不同）配回 `orders_backup`；彻底方案才是把 `orders_backup` 系列接入 `vehicle_sl.cpp`（新 savegame 版本）。**当前处置**：记录+分析，未改代码。 | 2026-09-18 第 48 轮（承接 KI-96 的残留分析） | **方案已拍板（段级排程 + 挂接计数入存档）；未实现** | 中 |
| KI-98 | **挂接「视觉包围盒重叠」与「判定接触」两套几何不对齐**（玩家提问，纯分析，未改代码）。引擎里有两套长度/位置：物理长度 `gcache.cached_veh_length`（全部挂接/碰撞几何都用它）与渲染包围盒（`Train::UpdateDeltaXY` 算出的 `bounds`，由 `cached_veh_length` + `direction` + R3R 的 `flip_offs` 推出）。四条"算不算贴上"的判据互不相同：①`GetCouplePosition`（`train_cmd.cpp` ~6221）要求中心距**精确**等于 `(len_v+1)/2 + (len_u+1)/2`；②`TrainCoupleHandler` 的 3×3 邻域扫描（6317-6353，**只在 `cur_speed == 0` 时跑**）用 `max(|dx|,|dy|) <= 半长和` 兜底；③`CheckTrainCollision`（8503-8566，**运行中唯一**的挂接触发）先按 `VehiclesNearTileXY(x, y, 7)` 取候选，再过 8px 哈希门，最后 `d² <= min_diff²`，`min_diff = (len_v+1)/2 + (len_mf+1)/2 - 1`；④`R3RCoupleSpliceFolded`（5555-5569）只对拼接点那一对 `(v_last, u_head)` 判 `|dist - (len_a+len_b)/2| <= 8`。四条里没有一条用"渲染包围盒相交"。因此：**(a) 视觉贴住 ≠ 判定接触**——停车态有 3×3 扫描兜底，但**运动中只有 `CheckTrainCollision` 这一条**，候选框半宽 7（`vehicle.cpp` `VehiclesNearTileXY`：`pos_rect` 为 ±7 的方框，`pos_rect.Contains({v->x_pos, v->y_pos})`）+ `min_diff` 由名义长度算出，容差只剩末尾 1 个单位 ⇒ 机车停早一点点就完全不命中（这正是注释 6318-6327 所述"visual bounding boxes overlapping, but logical centre distance > 8px"的成因）。**(b) 判定接触 ≠ 视觉贴住**——artic 子件与父车**共用同一 `x_pos/y_pos`**（注释 5501-5505 实测 `exp=5 dist=0`、`exp=4 dist=0`），而 `min_diff`/`expected` 用的是子件自己的短 `cached_veh_length` ⇒ 名义中心距与视觉位置彻底脱钩，对短件算出的 `min_diff` 很小，"机车视觉上已经插进 artic 组里却判为没接触"。**(c) 端点角色不一致**（记忆 79051681 的死循环根因）：`GetCouplePosition` 用"机车鼻尖 ↔ 车底尾车"，拼接语义是"机车尾车 `v_last` ↔ 车底头车 `u_head`"，`CheckTrainCollision` 用 `moving_front`（链头/鼻尖）对**车底任意一节**——三者端点不同 ⇒ 命中触发防折叠、拼接对不成立、回滚、下 tick 再命中。**(d) 折叠判定已降级为诊断**：5516-5527 明确 `ChainFolded` 只认 `R3RCheckChainFoldedDirection()`，距离扫描仅出 `FOLDCHK`/`FOLDCHK-ACCEPT` 日志（因 KI-06 的 ArrangeTrains 只重接线不重算位置、以及 artic `dist=0` 两大原因），`worst_gap > 8` 不再否决；代价是"链里除拼接点以外的真折叠"无人检测。**待定改动方向（四选，尚未拍板）**：A. 候选半径按 `(len_v+len_mf)/2 + 2` 动态给，并把 3×3 扫描也用于运动态（限速/N tick 节流，沿用 KI-14 的 `R3RDbgEdge` 思路）；B. 新增"真包围盒 AABB 相交"测试，**只用于候选筛选/触发**，不动折叠与拼接的物理语义；C. `SpliceFolded` 从"仅拼接点"改为扫全部段边界对，`dist` 改用带方向的投影距离而非 `max(|dx|,|dy|)`；D. 统一三处端点角色定义。 | 2026-09-18 第 48 轮（玩家提问"挂接边界框重叠"） | **修法 D 待重新拍板（用户倾向简化方案）；未实现** | 中 |
| KI-99 | **（用户 2026-09-18 拍板的存档方案）把「挂接计数」与「每个段依附的调度命令」一并写入存档。** 这是 KI-97 NOSAVE 残留的正式解法，方向＝备忘 `R3R_multi_couple_decouple_orders_memo.md` §10 的**模型 4（段级排程）**：不再让 Couple/Decouple 用裸指针搬 `orders` 并靠单层 `orders_backup` 兜底，而是**每个段各持一份排程**（Couple 只做引用不搬运），同时把「挂了/解了几节」的交接计数显式化。配套需要接线的既有但零使用点字段：`OrderDecoupleOrdersFlags`（`ODOF_KEEP_ORDERS/KEEP_ORDERS_NO_LOAD/INHERIT_ORDERS/WAIT_FOR_COUPLE`，`order_type.h`）、`GetDecoupleFirst/SecondOrdersType`、`decouple_first/second_orders`、`GetNumCouple`（`order_base.h`）。落点预计 `src/sl/vehicle_sl.cpp`（或新建段表块）＋ `src/vehicle_base.h`（段表结构）＋ `train_cmd.cpp`（Couple/Decouple 改为段级归属）⇒ **会碰 `src/*.h`，须按 KI-15/记忆 66636022 删全部 `*.obj` 全量重编**，且新增字段需要提升 savegame 版本。收益：一次性消掉 KI-97 的四个爆雷场景（跨存档丢排程、跨存档改号、孤儿 OrderList、借位重建依赖指针相等）；代价：改动面覆盖路线 A 的核心交接逻辑。 | 2026-09-18 第 48 轮（用户拍板） | **已拍板；未实现** | 中 |
| KI-100 | **「边界框重叠」根因三点（本轮用户实测线索确认，构成 KI-98 修法 D 的依据）。** **A. `ArrangeTrains()`（`train_cmd.cpp` 2316+）只重接线、不重算位置**——函数体只有 `RemoveFromConsist` / `InsertInConsist`，没有任何一处写 `x_pos/y_pos`；耦合成功后两条链各自保持拼接前的物理位置。**B. 位置重排只在「车动了」时才发生**——重排由 `UpdatePosition()` 负责，其调用点全在运动/出库/建造路径（`train_cmd.cpp` `:2953` 前进一像素、`:6690` 出库、`:1653/1872/1952` 建造），**耦合提交点（`Couple()` → `ArrangeTrains()`）从不调用它**；因此耦合后若列车不再移动（depot 内、`GOTO_COUPLE` 完成即 `current_order.Free()`、站台停稳），重叠状态会**永久固化**在画面上。**C. 三处端点角色不一致**：`GetCouplePosition`（`:6221`）用 **`v`（链头/机车本体）** ↔ `z`（`z = reverse ? u->Last() : u`）；`CheckTrainCollision`（`:8503`）用 `moving_front`（当前运动方向的鼻尖）↔ 候选链**任意一节**；`TryTrainCouple` 拼接用 **`v->Last()`（车尾）↔ `u`（车底头）**；`TrainCoupleHandler` 的 3×3 兜底（`:6317`）又是自己一套 `v` ↔ `z`。⇒ 命中判定与拼接判定**从来不是同一对车**，每种接近方式都留下一个错位量且不被纠正（单节机车时 `v == v->Last()` 差值最小，仍因 `(len+1)/2` 向上取整残留约 1 个单位；多节机车链时可达整节甚至数节）。**用户实测**：无论耦合时用哪一组端点，两列车边界框与图像都会重叠——C 解释了"为什么全都重叠"，A+B 解释了"为什么一直重叠不散开"。 | 2026-09-18 第 48 轮（用户实测补充） | **已记录（作为修法 D 依据）** | 中 |
| KI-101 | **列车「位置重排」函数画像（第 49 轮核实，纠正一个关键误判）。** `Vehicle::UpdatePosition()`（`vehicle_base.h:963-967`）**只做一件事**：`if (type < CompanyEnd) UpdateVehicleTileHash(this, false)`——刷 tile 哈希，**完全不碰 `x_pos/y_pos`**；`Vehicle::UpdatePositionAndViewport()`（`vehicle.cpp:2924`）＝它 + `UpdateViewport(true)`。因此「耦合时调一次 UpdatePosition 把物理位置与图像统一」**无效**，几何量一个像素都不会变。运行期真正写列车 `x_pos/y_pos` 的只有两处：①`TrainController`（`train_cmd.cpp:9046`，主循环 `:9073` `for (prev = v->GetMovingPrev(); v != nomove; prev = v, v = v->GetMovingNext())`，在 `:9629-9631` 对沿途每一节执行 `v->x_pos = gp.x; v->UpdatePosition()`）；②`ReverseTrainSwapVeh`（`:2984` `std::swap(a->x_pos, b->x_pos)`）与建造/出库初始化（`1619/1846/1907/6644`）。但 `GetNewVehiclePos`（`vehicle.cpp:2958`）**只把单车沿「自身」`GetMovingDirection()` 推进 1 个单位**，不参考 `prev`、不按速度缩放 ⇒ `TrainController` 是「整列同量推进」的刚性平移，**不是重排/归位函数**；零速或只传半边链调用它只会误推进，且 `nomove` 会截断循环（其文档明写 `@param nomove Stop moving this and all following vehicles.`）让后半段保持原位，**不可用作耦合后的重排手段**。链上名义间距由 `Train::CalcNextVehicleOffset()`（`train.h:361`，`(len+rounding)/2 + (len_next+1-rounding)/2`）定义；`CheckTrainsLengths()`（`train_cmd.cpp:202-226`）在读档时校验 `max(|Δx|,|Δy|) == CalcNextVehicleOffset()` 这一不变式，`ReverseTrainDirection` 内两处 `differential = base->CalcNextVehicleOffset() - last->CalcNextVehicleOffset()`（`3233/3293`）是「位移补偿」的现成先例，可直接借用为耦合后闭合接缝的做法。 | 2026-09-18 第 49 轮 | **已记录（纠正误判）** | 低 |
| KI-102 | **「耦合后重叠 1 像素、疑仅铰接车」的机制假说（第 50 轮，代码核实 + 待实测）。** 用户实测：耦合时边界框/图像重叠**只有 1 像素**，且**疑似仅铰接式列车**出现。据此两条旧修法都不对症：修法 D（三处端点角色不一致）量级是「整节车」，解释不了 1px；合缝平移 S1 只把 1px 重叠换成 1px 缝隙，且无法解释「仅铰接」。新定位：`Train::CalcNextVehicleOffset()`（`train.h:361-369`）取整 `uint8_t rounding = this->IsDrivingBackwards() ? 1 : 0;` 配合 `(len+rounding)/2 + (len_next+1-rounding)/2`，**只有相邻两节长度奇偶性不同时 r=0/1 才差 1**（同偶或同奇时两者得数完全相同；奇偶混合时差 1）。长度恒为 8 的普通车队因此免疫，铰接车（长度被 `shorten_factor` 改过、父车与 artic part 奇偶性混合）命中 ⇒ 与「仅铰接车」吻合。诱因链：`IsDrivingBackwards()` 是**标志位**（`vehicle_base.h:447`），它同时决定 `GetMovingNext/Prev/Front/Back`（`453-483`）、`GetMovingDirection()`（`483`）与上述取整（`train.h:367`）；而 R3R 逻辑翻转第 1 步 `R3RReverseChainDirections`（`train_cmd.cpp:5290-5291`，注释「就地反 direction，位置不动」）只反 `direction`，`Couple` 提交点的 DB 处理又是**条件式**（「链头残留 DB 才逐节 Reset」，记忆 55558532）而非不变式重建 ⇒ 合并链上「标志位 / direction / 像素取整」三者可能不自洽。**可证伪预言**：找一列普通车厢但含**奇数长度**车厢的列车做耦合，若同样出现 1px 重叠 ⇒ 确认取整说；若不失 ⇒ 假说错，退回端点角色/DB 一致性方向。旁证：同一取整还用于站台停车位置（`train_cmd.cpp:531/668/695`）与 `GetTileMarginInFrontOfTrain`，标志位不自洽时停车点也会有 1px 级偏移。 | 2026-09-18 第 50 轮（用户实测 + 代码核实） | **未修（假说，待探针实测）** | 低 |
| KI-103 | **KI-102 的根因核实与修法 (a)/(a′) 预期（第 50 轮）。** 真凶=**标志位不同源**：渲染/包围盒链走 `direction XOR VehicleRailFlag::Flipped`（`train_cmd.cpp:2796-2812` UpdateDeltaXY 的 `flipped`/`flip_offs`/`half_shorten`、`1474` GetImage、`vehicle.cpp:561/1724` GetMapImageDirection 取 `this->direction`、`vehicle.cpp:4273` 与 `4499-4502` 烟雾/灯光），间距链走 `VehicleFlag::DrivingBackwards`（`train.h:367` CalcNextVehicleOffset、`vehicle_base.h:471/483` GetMovingNext/GetMovingDirection、`train_cmd.cpp:531/668/695/10029`）。`R3RReverseChainDirections`（`5038-5044`）同时反 `direction` 与 `Flipped`（渲染侧 XOR 因子整体翻转、互相抵消⇒玩家看不到），但**没动 `DrivingBackwards`**（间距侧）。`flip_offs`/`half_shorten` 只依赖 `flipped` 布尔、不依赖 direction 数值，故这是两链间唯一暴露的差 ⇒ **奇数 `cached_veh_length` 差 1px**；长度 8 车 `flip_offs=0`、`half_shorten=(0+f)/2=0` ⇒ 完全免疫，完美解释「1px + 仅铰接车」。**修法 (a)**＝删 `5042` 行 `w->flags.Flip(VehicleRailFlag::Flipped);`，预期 ①1px 消失（渲染侧与间距侧重新同源）；②被逻辑翻的那半条链**渲染整体掉头 180°**（精灵+包围盒+烟雾+灯光+`GetCursorImageOffset`1418/`GetDisplayImageWidth`1448 一致掉头，内部仍自洽，**不是错位**），可见性取决于「翻转→耦合」时间窗口与车底对称性，回滚经 `R3RUndoLogicalFlip` 对称恢复不漂移；③**隐性收益**：撤回对 `Flipped` 的语义借用——`Flipped` 在 vanilla 是「车辆倒装真值」而非纯视觉补偿位（`train_cmd.cpp:3650` depot 换端命令即 `Flip(Flipped)` 且不动 direction；`autoreplace_cmd.cpp:540`、`vehicle_cmd.cpp:1552/1978` 按它复制倒装状态；`articulated_vehicles.cpp:512`、`train_cmd.cpp:1650/1871/1951` 建造回调按概率设它），删除后这些读到真值、不再与 R3R 假状态叠加（注：`newgrf_engine.cpp:959` 用的是另一个位 `Reversed`，不受本行影响）。**更优候选 (a′)**＝若 `R3RFlipChainBySegments` 的重链(SetNext)+★迁移+身份迁移已表达翻转，`5041` 的 direction 反转亦属冗余，连它一起删可做到「1px 消失且视觉零变化」，但须先核 `GetMovingDirection()` 系消费者（`vehicle_base.h:483`、`train_cmd.cpp:530/668/695/911/10029`）是否依赖 direction 被反。**待用户拍板**：「逻辑翻转期间玩家看不见」是否为硬需求。改动仅 `.cpp`，可增量编译（改 `src/*.h` 才需全量重编）；链接前须关掉运行中的 openttd.exe 防 LNK1168。 | 2026-09-18 第 50 轮（代码核实） | **未修（(a)/(a′) 待拍板；核实见 KI-104）** | 低 |
| KI-104 | **【2026-09-18 第 52 轮修正：本条"（a′）已核前提"的结论被实测证伪，见 KI-105 —— 取消 direction 反转不是无害的；下列代码级事实仍有效，但结论按 KI-105 为准】** **(a)/(a′) 逐条核实（第 51 轮，代码级事实）。** ①`R3RReverseChainDirections`(5038-5044) 只改 `direction`(5041) 与 `VehicleRailFlag::Flipped`(5042)；调用点仅 5291(`R3RFlipChainBySegments` 第1步) 与 5110(`R3RUndoLogicalFlip`)，成对 ⇒ 回滚严格抵消。②渲染不变量：`Train::GetImage`(1474) `if(Flipped) direction=R(direction)`，输入=`this->direction`(`vehicle_base.h:561`，Train 未覆写) ⇒ render(d,f)=f?R(d):d 在 (d→R(d), f→¬f) 下**恒等** ⇒ 现状"逻辑翻转不可见"成立(5032-5033 注释正确)；**(a)** 只删 5042 ⇒ render→R(render) **可见掉头 180°**；**(a′)** 5041+5042 全删 ⇒ render 不变。③**(a′) 并非"只变 direction"**：direction/Flipped 都不变，但改了两个真消费者输入——`R3RCheckChainFoldedDirection` 第 `5004` 行读 `a->direction`（注释 4989-4992 明说该检查依赖"翻转会让所有 dot 翻号"，artic 假阳性 +4 即因此、才有 5003 跳过）；`Couple` 方向统一循环(5923-5934) `if(w->direction!=v->direction) w->flags.Flip(Flipped);`——注释 5928 明说折叠修正路径**不触发**，删 direction 反转后变**触发**（改走普通拼接补偿路，终态 (d=Dv,f=¬f) 与现状同、渲染一致，路径/终值变）。④5931 证明 5041/5042 是"改 direction 必配 Flipped 保渲染"的**成对惯用法**。⑤1px 复核：`UpdateDeltaXY`(2790-2815) 的 `flip_offs = flipped&&(len&1)`、`half_shorten = (8-len+flipped)/2` 中 `flipped` 是**加性数值**（非方向开关）；现状翻转连加性项一起改 ⇒ 奇数长度车 bounds 差 1px（len=8 免疫），间距侧只读 `DrivingBackwards` 不动 ⇒ 重叠；(a)/(a′) 均把加性项复原 ⇒ 1px 消失。⑥`Flipped` 消费者全清单：`train_cmd.cpp:1418/1448`(光标图偏移)、`1474`、`2796`、`3650`(depot 换端 `Flip(Flipped)`)、`5931`；`vehicle.cpp:4273/4499`(烟雾/特效)、`4704`(dump)；**`newgrf_engine.cpp:1075-1077`(GRF 变量 0x48：Flipped⇒`CUSTOM_VEHICLE_SPRITENUM_REVERSED`)**；`autoreplace_cmd.cpp:540`、`vehicle_cmd.cpp:1552/1558/1978-1982`、`articulated_vehicles.cpp:512`(倒装真值)、`tbtr_template_vehicle*`。⇒ (a)/(a′) 共同收益=撤回该借用。建议先试 (a)（一行、只扰动 Flipped 的 6 处、FOLDCHK-DIR 与 5930 循环不变），目视确认掉头可否接受；否则再上 (a′) 并复测 5004 与 5930。 | 2026-09-18 第 51 轮（代码核实） | **未修（待拍板：先 (a) 还是 (a′)）** | 低 |
| KI-105 | **【2026-09-18 第 52 轮实测证伪 → 已回退；`R3RReverseChainDirections` 已恢复旧实现（逐节 `direction = ReverseDir(direction)` + `Flip(Flipped)`）】** 玩家现象：**机车一路穿模压进车底、随后一片重叠**（旧版 exe 同场景耦合正常，玩家确认）。日志证据链（`build\R3R_debug.log` 726 行）：`COUPLE-FAIL` → `FOLDCHK` FLIP-U/FLIP-V/FLIP-BOTH 反复回滚（248 行同一候选因 seam `dy=+1` 以 `dot=+1` 被否决）→ 直到 seam 恰好 `dy=0`（`dist=0`，两车同点）才 `COUPLE-OK`，其 `worst_gap=2` → 紧接着 `CRT-FOLD veh=2 tile=39,31 dir=3 backTile=39,33 rel=0,2 dot=2` → 整列以尾端为"前"跑。**根因**：`direction` 的反转是 `R3RCheckChainFoldedDirection`（按 `a->direction` 算点积，依赖"逻辑翻转把相邻对点积符号整体翻过来"，见该函数 4989-4992 原注释）与 TrainController moving-front 的**输入**，不是纯渲染账目；取消反转后**健康候选被判成折叠**，于是机车一路压进去直到几何上 dy 归零才勉强通过 ⇒ 穿模。**结论**：要消除 `Flipped` 借用污染（1px 重叠 / GRF 变量 0x48 / 真值记录），只能改渲染与查询层，或给这两个消费者补"逻辑朝向"读法，**不能靠取消 direction 反转**。原条目正文（试装记录）：**(a′) 试装落地（2026-09-18 第 52 轮，用户拍板"图像反转是我不能接受的，试 a′"）。** 改动=仅 `src/train_cmd.cpp`：`R3RReverseChainDirections`（`5028-5080`）体内两行 `w->direction = ReverseDir(w->direction)`（原 5041）与 `w->flags.Flip(VehicleRailFlag::Flipped)`（原 5042）删除，函数变空操作；保留函数名与两个调用点（`5291` `R3RFlipChainBySegments` 第 1 步、`5110` `R3RUndoLogicalFlip`）以保证回滚天然对称。5028-5037 的契约注释与函数体注释同步改写为 (a′)（含被撤销的 Flipped 借用清单，可一键还原那两行）。预期收益：渲染逐像素不变（不再依赖"direction 反向 + Flipped 抵偿"）、`UpdateDeltaXY` 的加性项 `flipped` 不被扰动（消奇数长度车 1px 重叠）、NewGRF 变量 0x48 / 光标图偏移 / 建造·自动替换·蓝本复制的倒装真值不再被污染。已知需实测的连带项（详见 KI-104 ③）：①`R3RCheckChainFoldedDirection`(`4975`, 点积读 `5004` 行 `a->direction`) 回到"自然朝向"语义——原注释 4989-4992 明说该检查依赖"逻辑翻转会把每个相邻对点积符号整体翻过来"，故其实测到的 artic 对 `+4` 假阳性预期随之消失（`5003` 的跳过仍可留作保险）；②`Couple` 方向统一循环(`5923-5934`，`5931`) 由注释 5928 所述"折叠修正路径不触发"变为**触发**（补偿后的终态与旧实现一致、渲染一致，但代码路径/中间值不同）；③三个候选的失败回滚仍严格对称（同一函数双向变空）。编译：`_tmp_inc_build.cmd` 增量、`build\R3R_incbuild.done = EXIT_CODE=0`、`[3/3] Linking CXX executable openttd.exe`。状态=**已改已编译、运行时未验证**；待复测：test4.sav 折叠现场（安全网：FOLDCHK/FOLDCHK-DIR 不刷屏、移动后无 TrainController 崩溃、无六车叠一格）+ 目视比对车组端头图与翻转前一致 + 多段链换端后图像不变形。还原=把上述两行加回并恢复注释。 | 2026-09-18 第 52 轮 | **未验证（已装入 exe 待实测）** | 中 |

---

## 附：第 53 轮（2026-09-18）新增条目

| ID | 描述 | 来源 | 状态 | 严重度 |
|---|---|---|---|---|
| KI-106 | **耦合接缝恒紧 1px（铰接车底 2-8-2 观测到像素重叠，原版 4px 车未观测到）—— 已定位到 `CheckTrainCollision` 的 `- 1`，并已加探针待实测确认。** **现场（`build\R3R_debug.log`，旧 exe）**：`FOLDCHK COUPLE-FLIP-BOTH n=9 worst_gap=1 A idx=0 x=632 y=510 B idx=8 x=632 y=511 exp=2 dist=1`（`exp` 出自 `R3RCheckChainFold`，`train_cmd.cpp:4938` 的 `(a->cached_veh_length + b->cached_veh_length) / 2`，`gap = \|max(dx,dy) − exp\|`）；紧随 `COUPLE-OK` 的 `CPL idx=` 逐节 dump 为 **502 / 506 / 510 ‖ 511 / 516 / 521 / 523 / 528 / 533**。逐对中心距 4、4、**1**、5、5、2、5、5 —— **除接缝 510↔511 外每一对都恰好等于 `exp`**，只有接缝比 `exp=2` 紧 1px ⇒ 那 1px 的唯一落点就是接缝，且必然表现为两节包围盒重叠 1px。**候选根因（代码）**：①`CheckTrainCollision`（`train_cmd.cpp:8566`）的触摸/重叠耦合阈值 `min_diff = (L_a+1)/2 + (L_b+1)/2 - 1`，比"包围盒刚好相切"的名义中心距 `(L_a+L_b)/2` **少 1px**；R3R 把原版 `>=` 改成 `>`（8572 注释）后，机车在"已经重叠 1px"的位置就触发 `Couple()` 并把 `moving_front->cur_speed = 0` 冻结位置。②精确路径 `GetCouplePosition`（`train_cmd.cpp:6259`，判据 `max(\|Δx\|,\|Δy\|) == (v_length+1)/2 + (u_length+1)/2`）**只在 `TrainCoupleHandler` 内被调用，而后者在 `v->cur_speed != 0` 时直接早退**（`6281-6287`）——行驶中的机车**永远走不到**精确判据，只能被①在半途截停 ⇒ **1px 重叠是结构性必然，不取决于长度奇偶**。③注：`CalcNextVehicleOffset()`（`train.h:361`）的奇偶取整、`Train::UpdateDeltaXY()`（`train_cmd.cpp:2790`）的 `flip_offs`/`half_shorten` 经算式核对对这些偶长度（2/4/8）**均精确**，不是本次这 1px 的来源（先前怀疑已排除）。**已加探针（本轮，仅改 `src/train_cmd.cpp`，符合 KI-15 增量；`build\R3R_incbuild.done = EXIT_CODE=0`）**：新增边沿标签 `R3REDGE_CPLGEO` / `R3REDGE_CPLHIT`（枚举 `4181` 区）；`GetCouplePosition` 命中点写 `CPL-GEO`（含 `vlen/ulen/dx/dy/diff/need/rev`）；`CheckTrainCollision` 两处耦合触发点（`site=depot` 与 `site=open`）写 `CPL-HIT`（含 `len_v/len_mf/min_diff/need/fit/dx/dy/maxd/overlap/spd/dir`，`overlap = (len_v+len_mf)/2 − maxd`，正值即重叠像素数）；`COUPLE-OK` 的 `CPL idx=` 行追加 `len=` 与 `gap=`（`gap = 实际中心距 − (len_a+len_b)/2`，负值即重叠）。**待复测**：复现场景后核对（a）日志里出现的是 `CPL-HIT` 还是 `CPL-GEO`；（b）`CPL idx=` 行的 `len=` 是否与预期（机车 [2,6,2]、车底 [2,8,2]×2）一致——旧日志由 `exp` 反推得机车段中心距 4、4 而车底段为 5、5、2、5、5，提示**机车自身可能并非 2-8-2**，需 `len=` 实测确认。**未解**：原版 4px 车"不重叠"的对照尚无法解释（同样 `min_diff = M−1`，理论上也应偏 1px）；疑点是 4px 场景下机车**先停稳**在精确点从而走 `CPL-GEO`，须由探针数据分辨。 | 2026-09-18 第 53 轮（第 59 轮实测通过·倒车 + 正向双场景） | **已修：倒车（坑 3）与正向两条路径均实测零重叠。第 59 轮日志 `CPL-HIT` 0 次、`CPL-GEO` 4 次（倒车 `db=1 vn=2 z=3`／正向 `db=0 vn=0 z=8`），两处均 `diff=2==need=2` 滚动中命中（`spd=68`／`51`）、`FOLDCHK worst_gap=0`、耦合后 9 节 `CPL idx gap` 全 0。** | 低 |

### KI-106 复查（第 55 轮，2026-09-18）：1px 与车辆类型无关；**下一小节的"固有咬合量"定性已作废，改为"可修行为（修法1）"**，原词"咬合量"

**A/B 对照日志（同一 R3R exe、同场景，仅换车底）**

- 原版车底：`build\R3R_debug_vanilla_noarticulated.log:195`
  `[R3R] CPL-HIT site=open loco=3 v=2 len_v=8 len_mf=8 min_diff=7 need=8 fit=8 dx=0 dy=7 maxd=7 overlap=1 spd=51 dir=3`
- GRF 铰接车底：`build\R3R_debug_withgrf_articulated.log:188`
  `[R3R] CPL-HIT site=open loco=0 v=8 len_v=2 len_mf=2 min_diff=1 need=2 fit=2 dx=0 dy=1 maxd=1 overlap=1 spd=58 dir=3`

⇒ `overlap = (len_v+len_mf)/2 − maxd` 在 **len=8（原版车）与 len=2（GRF 铰接）下恒为 1px**；`min_diff = (L_a+1)/2 + (L_b+1)/2 − 1` 恒比名义相切距离少 1。**结论：这 1px 与长度、铰接与否、GRF 与否全都无关**，是 R3R 耦合门（`train_cmd.cpp:8555` depot 分支 / `8612` 开放线路分支，`8618` 的 `>` 判定，注释 `8613-8617` 已说明为何弃用原版 `>=`）的固有"咬合量"。用户先前"仅铰接车重叠"的观察已由本轮 A/B 排除。

**接缝 `gap=-1` 同源、同量级，且两日志逐项一致**

- 原版车：`…noarticulated.log:253-260` `FOLDCHK COUPLE-FLIP-BOTH n=4 worst_gap=1` → `CPL idx=3 len=8 gap=-1`，其余 3 对 `gap=0`（`n=4` = 机车 1 + 车厢 3）。
- 铰接车：`…articulated.log:253-263` `FOLDCHK COUPLE-FLIP-BOTH n=9 worst_gap=1` → `CPL idx=0 len=2 gap=-1`，其余 8 对 `gap=0`（`n=9` = 3 + 6）。

除接缝外每一对中心距都精确等于 `(len_a+len_b)/2` ⇒ 排布链 `CalcNextVehicleOffset()` 无误差，那 1px 只来自耦合停位，与几何排布无关（印证 KI-106 ③ 的排除）。

### 第 55 轮根因更新（2026-09-18）：1px 不是固有咬合量，是**精确路径在移动中被早退跳过**

**代码依据**
- `GetAdvanceDistance()` 返回 **192（对角）/ 256（正交）的 progress 单位**，每 256/192 progress 前进 **1 个地图像素**。
- `TrainLocoHandler` 移动循环 `train_cmd.cpp:10773-10784`：`j -= adv_spd; TrainController(...)` → **每转一圈恰好推进 1 像素**，且耦合检查在**移动之后**（10778）。
- 因此 `diff = max(|dx|,|dy|)` 在趋近中**逐像素递减 1**，必然有一帧恰好等于 `need = (L_v+1)/2 + (L_u+1)/2`。但该帧的 `TrainCoupleHandler` 被 `6306` 的 `if (v->cur_speed != 0) return false;` **直接挡住**（白走 1px），下一步 `diff = need-1` 才由 `10786` 的 `CheckTrainCollision` 接管（`min_diff = need-1`）→ **恒定 1px 重叠**。逐字复现两份日志：`need=8 → maxd=7`、`need=2 → maxd=1`。
- ⇒ 兜底的 `-1` 只是第二道防线；病根是**第一道防线（精确路径）在移动中失效**。第 54 轮"步长可能为 2、故属固有量"的前提**错误，作废**。

**修法1（用户倾向）：最小改动**
- `TrainCoupleHandler` 开头的早退改为仅 depot 保留：`if (v->track == TRACK_BIT_DEPOT && v->cur_speed != 0) { erase; return false; }`，让开放线路 `else` 分支移动中每像素执行 `GetCouplePosition`；命中后 `10780-10782` 的 `cur_speed=0; progress=0; break;` 会**跳过 10786 兜底**，位置停在 `need` → 零重叠。.cpp-only，符合 KI-15 增量。

**必须同时处理的坑（可行性风险）**
1. **depot 分支无距离判据**（`6316-6345` 只查 `R3RCanCoupleNow` 就取 `u = w->First()`，不比距离）——放开早退会让 depot 内移动中的机车与同 tile 车底在**任意位置**立即耦合。必须只放开开放线路分支。
2. **低速滑行漏检**：`10745 if (j < adv_spd)` 分支只在 `cur_speed == 0` 时检查耦合（`10747`）；`cur_speed != 0` 且 `j < adv_spd` 的 tick 两处都不进。建议把 `10756-10767` 的检查从 `if (cur_speed == 0)` 内提出。
3. **倒车位置用错**：`GetCouplePosition(v)` 读 `v->x_pos/y_pos`，调用处传 `consist`（链头机车）；倒车时接近目标是 `GetMovingFront()`，算出的 `diff` 差整条链长 → 等值判据永不成立。现状被早退掩盖。
4. **折叠判据是 1px 级的**（KI-105 现场：seam `dy=+1` 使 `dot=+1` 否掉候选）。把耦合瞬间从 `need-1` 挪到 `need` 会改变 `R3RCheckChainFold` / `R3RCheckChainFoldedDirection` 的输入 → **必须回归** test4.sav 折叠场景 / nose-to-nose / 多段链换端。**最大不确定性来源**。
5. **两道门松紧不一致（新发现）**：精确路径走完整 `R3RCanCoupleNow`（`6250`），而兜底 `8534` 块**只查 order 类型、无门**。门拒绝时精确路径不给、兜底照样耦合 → 这类场景仍是 1px。顺带等于 KI-60"门覆盖全部目标解析"的一个缺口，建议单独记录。

**性能**：`GetCouplePosition` 首件事是 `FollowTrainReservation(v,&other)`（成本 ∝ 预留长度）。放开后从"每像素一次早退"变成"每像素一次预留回溯"。缓解：`10778` 有 `IsType(OT_GOTO_COUPLE)` 前置，只有去耦合的机车走此路，数量极少；实施时应顺带加廉价前置门（先比 `v->tile` 与耦合目标 tile 距离）以压掉高速段的无用回溯。

**覆盖率盲区（修法1 保证不了 100%）**：折叠修正失败回滚（当帧返回 false，之后等值判据再不成立）、坑 5 的门拒绝、坑 3 倒车、机车在 `need-1` 被停住 —— 这些都退到兜底，仍 1px。要彻底消灭需把兜底也改成"先沿来向回退 `diff - need` 像素再 `Couple()`"（动位置，风险更高），建议先上修法1 实测覆盖率再决定。

**验证判据**：`CPL-GEO` 出现、`CPL-HIT site=open` 消失、`CPL idx=… gap=` 全 0、`FOLDCHK worst_gap=0`；`CPL-HIT site=depot` 应照旧不变（depot 零扰动，同时是坑 1 的判据）。

### 第 56 轮落地记录（2026-09-18）：修法1 已实施并编译通过（仅 `src/train_cmd.cpp`）

**改动**
1. **核心**：`TrainCoupleHandler` 的移动早退由 `if (v->cur_speed != 0) return false;` 改为「仅 depot 分支保留」——`const bool r3r_rolling = v->cur_speed != 0; if (r3r_rolling) { erase(throttle); if (v->track == TRACK_BIT_DEPOT) return false; }`。开放线路滚动中每像素跑一次 `GetCouplePosition`，在 `diff == need` 的那一像素命中并 `Couple()`，随后 `10780-10782` 清零速度并 `break`（**跳过 `10786` 的 `CheckTrainCollision` 兜底**）⇒ 目标零重叠。
2. **9-tile 兜底扫描限定停稳**：改为 `if (u == nullptr && !r3r_rolling)`。该扫描上界是 `(v_length+1)/2 + (z_length+1)/2`（宽松），滚动中放开会在任意距离耦合，与修法1 目标相反。
3. **移动中未命中不报失败**：`if (u == nullptr) { if (r3r_rolling) return false; ... }`——滚动中"未到精确像素"是绝大多数移动步的正常状态，不进 `COUPLE-FAIL` 分支（省掉边沿 hash 开销，也避免误导）。
4. **depot 必须保留早退的理由**：depot 分支无距离判据（`R3RCanCoupleNow` 通过即 `u = w->First()`），滚动中放开会与同 tile 车底在任意位置耦合（穿模）。

**探针（本轮新增/增强）**
- `CPL-GEO` 行追加 `spd=` 与 `db=`（`IsDrivingBackwards()`）：`spd != 0` 即**移动中精确命中**，是修法1 生效的直接证据。
- 新增边沿标签 `R3REDGE_CPLGEOCOMMIT` → 日志行 `CPL-GEO-DONE v= u= merged= spd= x= y=`：`Couple()` 之后以 `u->First() == v->First()` 判是否真正合并；`merged=0` 即折叠修正回滚（未提交）。
- 保留 `CPL-HIT site=open|depot` 作为"仍走兜底"的对照。

**已确认的机制（读码）**
- `CheckTrainCollision` 的耦合（`8578` depot / `8652` open）与精确路径（`6435`）调用**同一个 `Couple()`**，两条路都进 `TryTrainCouple` 折叠修正 ⇒ 修法1 不引入新的耦合语义，只改变**调用时机与位置**。
- 停稳分支 `10766` 是 `CheckTrainCollision || TrainCoupleHandler`（兜底在前），**无需改序**：停在 `need-1` 时两者结果相同，停在 `need` 时兜底不触发、精确路径命中。

**仍走兜底（未修）的盲区**
- `TryTrainCouple` 折叠修正回滚：移动中命中后 `Couple()` 未提交，而 handler 仍返回 true ⇒ 机车停在精确点，下一 tick 从停稳重试（形态与修法1 前同量级、不更糟，但仍是"停住＋每 tick 重试"）；由 `CPL-GEO-DONE merged=0` 可观测。
- **坑 3（倒车）未修**：`GetCouplePosition` 用 `v->x_pos/y_pos`（链头），倒车时接近目标是 `GetMovingFront()`，`diff` 差整条链长 ⇒ 等值判据不成立、退化成兜底 1px。已有 `db=` 探针可判是否命中该场景。
- **坑 5（门松紧）**：精确路径走 `R3RCanCoupleNow`，兜底 `8630` 只查 order 类型 ⇒ 门拒绝时仍会耦合且带 1px。

**待用户实测（车库 + 站台挂车）**
1. 站台（开放线路）：应出现 `CPL-GEO … spd=<非0>` 与 `CPL-GEO-DONE … merged=1`；`CPL-HIT site=open` 应消失；`CPL idx=… gap=` 应全 0。
2. 车库（depot）：行为应与修法1 前**完全一致**（`CPL-HIT site=depot` 照旧，早退未动）。若 depot 出现 `CPL-GEO`，说明 depot 早退条件判断有误。
3. 性能：`PERF` 行的 `r3r_ch_calls/r3r_ch_ns`（`TrainCoupleHandler`，现含滚动中逐像素调用）是否显著上升；上升过多则需补廉价前置门。
4. 若出现 `CPL-GEO-DONE merged=0` 或机车停住不进不退，即为折叠回滚死循环，需后续专项处理。

**范围约定**：本轮只验证车库/站台挂车，不涉及挂接端点/折叠场景；端点类新问题留待后续轮次。

### 第 57 轮实测（2026-09-18）：站台挂车零重叠验证通过（修法1 目标达成）

**日志**：`build\R3R_debug.log`（337 行 / 20 KB，一轮完整跑：车库耦合 → 站台解挂 → 站台追车耦合 → 车库解挂 → 车库再耦合；**游戏未崩溃**）

**修法1 生效的直接证据（站台 / 开放线路 39,31）**
```
188: [R3R] CPL-GEO site=geo v=0 vlen=2 u=3 z=8 ulen=2 rev=1 dx=0 dy=2 diff=2 need=2 spd=60 db=0
264: [R3R] CPL-GEO-DONE v=0 u=3 merged=1 spd=60 x=632 y=509
254: COUPLE-OK loco=2 rear=3 consist=3 co=1 real=4 type=2 tx=39 ty=31 x=632 y=501
255-263: CPL idx=2..3 九节全部 gap=0
```
- `spd=60` ⇒ **滚动中命中**（不再是被 `cur_speed != 0` 早退跳过的帧）——修法1 生效。
- `diff=2 need=2` ⇒ 在精确像素上命中。
- `merged=1` ⇒ `Couple()` 合并提交成功（非折叠回滚）。
- `CPL idx=… gap=` **九节全 0** ⇒ **零重叠**，KI-106 的 1px 咬合量消失。

**兜底路径已被绕过**：全程 `CPL-HIT` **0 次**（`site=open`、`site=depot` 均无）；`COUPLE-FAIL` 仅 1 次（`172: loco=0 order=16 tx=37 ty=28`）且非刷屏——机车先停在 37,28 等待点找车底未果（单次边沿报告，节流正常），随后寻路到 39,33 追上。

**depot 对照成立**：两次车库耦合（`38,27`，行 5-21 / 320-330）均 `COUPLE-OK` 且**无任何 `CPL-GEO` 行** ⇒ depot 分支仍走 `u = w->First()` 无距离判据直接耦合，早退保留生效、行为零变化。

**折叠修正**：站台命中后初次拼接 `FOLDCHK COUPLE worst_gap=30`（端点角色错位，即记忆 79051681 场景）→ A2 只翻 u（`worst_gap=8`）回滚 → A3 只翻 v 被 `SPLICE-GAP-REJECT`（`exp=2 dist=24`）拒绝回滚 → A3 双翻 → `FOLDCHK COUPLE-FLIP-BOTH worst_gap=0` → `COUPLE-OK`。三候选全部走完，**折叠在移动中一次解开，无死循环**。

**仍未覆盖 / 新观察**
- **坑 3（倒车 + 开放线路）未覆盖**：站台命中 `db=0`；唯一 `db=1` 的 veh=2（`273-284 NOCAB-SET/NOCAB-LIMIT db=1 spd=0`）发生在 **depot 分支**，不经 `GetCouplePosition`。⇒ 倒车入位耦合仍未实测。
- `NOCAB-*` 系列（机车倒车时被"无驾驶室"逻辑限速 `spd=0`）为既有行为，本轮不影响耦合结果，但与坑 3 场景强相关，后续测倒车耦合时需一并盯。
- 性能：本轮未核对 PERF 行 `r3r_ch_calls/r3r_ch_ns`；但日志规模正常（337 行，无刷屏），未见逐像素调用导致的日志膨胀。

**结论**：本场景（车库 + 站台、正向入位、车底反向）**修法1 目标达成**——站台耦合零重叠、无崩溃、depot 零扰动。待测项收窄为：①倒车入位耦合（坑 3）；②`Couple` 折叠回滚未提交（`merged=0`）的停住死循环场景；③PERF 逐像素开销核对。

**KI-106 遗留的长度疑点已由 `len=` 实测澄清**

- 原版车：**`len=8`**（不是先前口头说的"4px"）。
- GRF 铰接机车：**`len=2,6,2`**（一个 3 节 artic 组，`AH=1/AM=1/AM=1`，`engType=494`；不是 2-8-2；与先前由 `exp` 反推的"中心距 4,4"吻合：`2/2+6/2=4`）。
- GRF 铰接车底：**`len=2,8,2` × 2 组**（`{3,4,5}`、`{6,7,8}`，各 `AH=1/AM=1/AM=1`，`engType=325`；与 `exp` 5,5,2,5,5 吻合）。

**`CPL-GEO` 在两份日志中零命中**：两次耦合都走 `CPL-HIT`（碰撞门），`GetCouplePosition` 的精确几何路径完全没被走到 —— 实测印证 KI-106 ② 的结构性论证（行驶中机车走不到精确判据）。

**注（非缺陷，勿误判）**：`…noarticulated.log:50-55` depot 内耦合出现 `CPL … len=8 gap=-8`，那是 depot 里整链共点（`x=232 y=852` 全部相同）的正常快照，不是重叠。

**处置（本轮结论）**

- 状态改判 **非缺陷（符合预期行为）**，退出 bug 跟踪；严重度降为低。
- 若日后仍要求"零重叠"停位，唯一干净落点是让停位走精确几何（放开 `TrainCoupleHandler` 的 `cur_speed != 0` 早退，或在 `Couple()` 提交点沿来向回退 1px），属行为变更，需单独拍板，不在本轮范围。
- 探针 `CPL-GEO` / `CPL-HIT` / `CPL idx=… len=… gap=…` 全部走 `R3RDbgEdge` 边沿触发（每次耦合事件最多各 1 行），开销可忽略，暂留；如需清理可一次移除（仅 `src/train_cmd.cpp`）。

### 第 58 轮修复（2026-09-18）：倒车入位恒重叠 —— 修法1 的度量端取错（坑 3 实证 + 已修）

**用户实测**：倒车挂接仍然重叠。

**日志**：`build\R3R_debug.log`（本轮 254 行）

```
197: COUPLE-FAIL loco=0 order=16 tx=38 ty=35 x=616 y=574
212: [R3R] CPL-HIT site=open loco=0 v=3 len_v=2 len_mf=2 min_diff=1 need=2 fit=2 dx=0 dy=-1 maxd=1 overlap=1 spd=0 dir=3
217: FOLDCHK COUPLE ... A idx=2 x=632 y=534 ... B idx=3 x=632 y=533 ... dist=1
218: COUPLE-OK ...
221: CPL idx=2 x=632 y=534 ... gap=-1        ← 接缝重叠 1px
```

**计数**：`CPL-GEO` **0 次**、`CPL-HIT` **1 次**、`COUPLE-FAIL` 2 次、`REVERSEDIR` 2 次（`db=1`）。

**根因**：`GetCouplePosition`（`train_cmd.cpp:6259`）用**链头 `v->x_pos`** 量距离，但机车 `db=1`（倒车）时**以链尾领前**——`vehicle_base.h:459` 的 `GetMovingFront()` 定义为 `IsDrivingBackwards() ? Last() : First()`，日志 `FOLDCHK A idx=2` 正是这个链尾。后果三层：

1. 名义接触点被推到整条机车链之外，`diff` 与 `need` 恒不相等 ⇒ `diff == need` 那一帧**永不存在** ⇒ 修法1 的精确命中在倒车路径上完全失效（本轮 `CPL-GEO` 零命中，与正向第 57 轮 `spd=60` 命中形成直接对照）。
2. 机车因此一路倒到底，直到 `CheckTrainCollision` 的兜底阈值 `min_diff = need - 1` 触发，才被撞停并耦合 ⇒ **恒定 1px 重叠**（`gap=-1`，`min_diff=1 need=2`）。
3. 车底近端的选取同样错端：旧码按方向差判定（同向 ⇒ `z = u->Last()`），而倒车时机车实际接近的是 `u`（链头），日志 `FOLDCHK B idx=3` 已自证。

**修复（仅 `src/train_cmd.cpp` 的 `GetCouplePosition`）**

- 度量端改为 `Train *v_near = v->GetMovingFront();`（倒车自动取链尾）。
- 车底近端改为**几何最近端**：`end_dist(v_near, u) <= end_dist(v_near, u->Last()) ? u : u->Last()`；`reverse` 随之取 `z == u->Last()`（该 `reverse` 仅本函数内部使用，无外部消费者，改动不外溢）。
- `diff` / `need` / `v_length` / `u_length` 全部改用 `v_near` 与 `z`。
- `CPL-GEO` 探针新增 `vn=`（接近端 index）便于下次核对。
- **兼容性论证**：正向场景（`db=0`）下 `v_near == v`，且"最近端"仍解析为 `u->Last()`（与旧方向规则结论一致）⇒ 第 57 轮已验证的零重叠路径行为不变。

**编译**：`_tmp_inc_build.cmd` 增量 → `EXIT_CODE=0`，`[3/3] Linking CXX executable openttd.exe`，`build\openttd.exe` @ 2026-09-18 19:51:22（50 647 552 B）；`src/train_cmd.cpp` lint 干净。

**待复测（同一倒车场景）**：`CPL-GEO` 应出现且 `db=1`、`vn=<链尾 index>`、`diff == need`、`spd > 0`（滚动中命中）；`CPL-HIT` 应回到 0；`CPL idx=… gap=` 应**全 0**；`FOLDCHK worst_gap ≤ 0` 且 `COUPLE-OK` 正常。**若 `CPL-HIT` 在倒车场景仍出现，说明兜底仍被走到，需再查兜底阈值 `need - 1` 的取整与 `GetMovingFront()` 是否与 `TrainLocoHandler` 的实际移动端一致。**

### 第 59 轮实测（2026-09-18）：**1px 已消灭** —— 倒车 + 正向双场景零重叠（KI-106 关闭）

**日志**：`build\R3R_debug.log`（645 行 / 40 205 B / 19:56:17，一轮跑两个场景：车库耦合 → 站台**倒车**入位耦合 → 站台**正向**入位耦合 → 车库再耦合）

**探针计数**：`CPL-GEO` **4 次**、`CPL-HIT` **0 次**、`COUPLE-OK` 5 次、`FOLDCHK` 11 次、`SPLICE-GAP` 1 次、`REVERSEDIR` 4 次、`COUPLE-FAIL` 4 次（均为等待点的一次性边沿报告，非刷屏）。

**倒车路径（坑 3，第 58 轮修复目标）**

```
216: [R3R] CPL-GEO site=geo v=0 vn=2 vlen=2 u=3 z=3 ulen=2 rev=0 dx=0 dy=2 diff=2 need=2 spd=68 db=1
221: FOLDCHK COUPLE n=9 worst_gap=0
222: COUPLE-OK loco=0 rear=8 consist=3 co=1 real=4 type=2 tx=39 ty=33 x=632 y=543
223-231: CPL idx=0..8 → 543 / 539 / 535 ‖ 533 / 529 / 525 / 523 / 519 / 515   gap 全 0
232: DB-CLEAR head=0 staleNocab=0 last=8 lastLead=1
233: [R3R] CPL-GEO-DONE v=0 u=3 merged=1 spd=68 x=632 y=543
```

- `vn=2`：接近端确实是**链尾**（修复前此处读链头 `v=0`）——修复生效的直接证据。
- `z=3` / `rev=0`：车底近端取到 `u`（链头）；旧码按同向判定会取 `u->Last()=8`，即错端。
- `diff=2 == need=2`、`spd=68`：**滚动中**在精确像素命中（修复前该场景 `CPL-GEO` 零命中、`spd=0` 走兜底）。
- 接缝 `idx2(535) ↔ idx3(533)` 距离 **2 = exp**，逐节 `gap` 全 0 ⇒ 1px 重叠消失。

**正向路径（回归对照）**

```
499: [R3R] CPL-GEO site=geo v=0 vn=0 vlen=2 u=3 z=8 ulen=2 rev=1 dx=0 dy=2 diff=2 need=2 spd=51 db=0
564: FOLDCHK COUPLE-FLIP-BOTH n=9 worst_gap=0
565: COUPLE-OK loco=2 rear=3 consist=3 co=1 real=4 type=2 tx=39 ty=31 x=632 y=505
566-574: CPL idx=2,1,0,8,7,6,5,4,3 → 505 / 509 / 513 ‖ 515 / 519 / 523 / 525 / 529 / 533   gap 全 0
575: [R3R] CPL-GEO-DONE v=0 u=3 merged=1 spd=51 x=632 y=513
```

- `vn=0`、`z=8`、`rev=1`：与旧方向规则结论**逐项一致** ⇒ 正向路径无回归（第 58 轮的兼容性论证获实测确认）。
- 候选链按 `直拼(504 worst_gap=26) → FLIP-U(531 worst_gap=8) → FLIP-V(545 worst_gap=18) → FLIP-BOTH(564 worst_gap=0)` 逐级尝试-回滚，最终由 `COUPLE-FLIP-BOTH` 零间隙提交。

**`gap` 分布复核（排除误判）**：全日志 `gap=0` 23 次；其余 `gap=-4`（18）/`gap=-2`（6）**全部**落在 `x=616 y=436 tile=38,27` 的 depot 内整链共点快照（所有车同一坐标），属既有已知假象（见本节上文注记），**不是重叠**。开放线路（站台/倒车）两处接缝均为 `gap=0`。

**新观察（低 · 非缺陷）**：正向场景 `546: FOLDCHK-ACCEPT COUPLE-FLIP-V worst_gap=18 residual gap (chain still closing up), not a fold` 放行后，`547: SPLICE-GAP-REJECT COUPLE-FLIP-V prev=0 … dist=20` 又将同一候选拒绝 ⇒ 折叠放行判据（`FOLDCHK-ACCEPT` 的残余间隙豁免）与拼接端点判据（`SPLICE-GAP`）宽严不一致，FLIP-V 白试一轮才落到 BOTH。最终结果正确（`worst_gap=0`），仅多一次尝试-回滚，暂不处理；若日后尝试链变长可考虑统一两处判据。

**结论**：**KI-106 关闭**（状态 `已修`，严重度降为低）——倒车（坑 3）与正向两条路径均零重叠、`CPL-HIT` 兜底归零。剩余未覆盖项仅"`Couple` 折叠回滚未提交（`merged=0`）的停住场景"与 PERF 逐项核对，与本 1px 无关。

### KI-107（第 60 轮，2026-09-18）：站台耦合后 `YapfTrainCheckReverse` 断言崩溃（`ReverseTrackdir` / `track_func.h:249`）

**来源**：玩家实报崩溃日志 `C:\Users\冯洁敏\Documents\OpenTTD\crash-20260918T121130Z.log`（exe `r3r-stable-2026-09-15-m (2)`，Build Sep 17 2026 19:50:20，`DBG_ASSERTS WITH_ASSERT` 全开，触发于 `assert_str_error` → `FatalErrorI`）。

**调用栈**：

```
[05] ReverseTrackdir                (src\track_func.h:249)
[06] YapfTrainCheckReverse          (src\pathfinder\yapf\yapf_rail.cpp:1379)
[07] CheckReverseTrain              (src\train_cmd.cpp:8161)
[08] Couple                         (src\train_cmd.cpp:6151)
[09] TrainCoupleHandler             (src\train_cmd.cpp:6478)
[10] TrainLocoHandler               (src\train_cmd.cpp:10840)
[11] Train::Tick                    (src\train_cmd.cpp:10933)
[12] CallVehicleTicks               (src\vehicle.cpp:1695)
```

**根因**：`YapfTrainCheckReverse()` 原第 1379 行把**链尾**的 trackdir 直接喂给 `ReverseTrackdir()`：

```cpp
Trackdir td = moving_front->GetVehicleTrackdir();
Trackdir td_rev = ReverseTrackdir(moving_back->GetVehicleTrackdir());   // 崩溃点
```

`Train::GetVehicleTrackdir()`（`train_cmd.cpp:11048` 起）有**两条不触发内部断言**就返回 `INVALID_TRACKDIR` 的路径：(1) `vehstatus` 带 `VehState::Crashed`（11050）；(2) `track == TRACK_BIT_WORMHOLE`（隧道/桥）时，`GetAcrossTunnelBridgeReservationTrackBits()` 与 `GetAcrossTunnelBridgeTrackBits()` 都解不出有效轨（11067 的上游守卫），或 `TrackExitdirToTrackdir()` 因轨向与桥洞方向不符而返回 `INVALID_TRACKDIR`（11068）。其余"无轨道位"路径会先在 `TrackDirectionToTrackdir()` 内部断言（`track_func.h:458`），与本次崩溃点 249 不符 ⇒ 现场必属 (1)(2) 之一。

而 `CheckReverseTrain()`（`train_cmd.cpp:8150`）**只守了前端**（`moving_front->track != TRACK_BIT_NONE`），链尾毫无守卫 ⇒ 断言正好炸在耦合提交（`Couple` → `CheckReverseTrain`）之后。这也是"耦合场景专属崩溃"的原因：`Couple()` 刚做完段合并/身份迁移，链尾可能正处于隧道/桥或撞毁等"轨向不可判定"状态。

**现场证据**：崩溃上下文 `CallVehicleTicks: veh: 0: (Train, c:0, st:E, vs:D, trk: 0x02, tile 39,31 (Station), front: 2: (Train 1, st:FE, trk: 0x20, tile 39,30 (Railway)))` —— ticked 车 `veh 0` 已沦为**链内普通引擎**（`st:E`，无 `G`/front 位），链头是 `veh 2`；`build\R3R_debug.log`（mtime 20:05:33）中 `COUPLE-OK loco=2 rear=12` 后的 12 节链快照 `2,1,0,5,4,3,11,10,9,14,13,12` 与该上下文（veh 0 在 39,31 `trk=0x02`、veh 2 在 39,30 `trk=0x20`、链头=veh 2）逐项吻合 ⇒ 崩溃发生在**这条 12 节合并链**的耦合/续耦合之后。日志最后写入 20:05:33 而崩溃时刻 20:11:30，且崩溃前无 `COUPLE-OK` 行 ⇒ 该 tick 的 `Couple()` 走的是 `u->orders == nullptr`（不打印 `COUPLE-OK`）分支后仍在 6151 崩溃，与"车组仍带 `GOTO_COUPLE`、`TrainCoupleHandler` 反复调用 `Couple()` 同一链"的时序一致。

**修复（仅 `.cpp`，符合 KI-15 增量合规）**：

1. `src/pathfinder/yapf/yapf_rail.cpp` — `YapfTrainCheckReverse()`：拆出 `td` / `td_back`，新增守卫 `td == INVALID_TRACKDIR || td_back == INVALID_TRACKDIR || moving_front->track == TRACK_BIT_NONE || moving_back->track == TRACK_BIT_NONE` → 写 `CRT-NOTD` 探针（`veh/db` + 两端 `idx/tile/trk/dir/crashed/td`）后 `return false`（**任一链端没有可用 trackdir 就绝不建议折返**），`ReverseTrackdir(td_back)` 移到守卫之后。同文件 `YapfTrainFindNearestDepot()`（1507-1518）同类加固：`td_back == INVALID_TRACKDIR` → `return FindDepotData()`。
2. `src/station_cmd.cpp` — `FreeTrainStationPlatformReservation()`（1528）与 `RestoreTrainReservation()`（1557）：`TrackdirToExitdir(ReverseTrackdir(...))`（`track_func.h:396/249` 双断言）之前加 `td_front/td_back != INVALID_TRACKDIR` 判定，无效端跳过（该端本来也压不住站台预留）。
3. `src/train_cmd.cpp` — `Train::GetVehicleTrackdir()`：结尾分支在**完全没有轨道位**时 `TrackDirectionToTrackdir(FindFirstTrack(this->track), ...)` 会先在 `track_func.h:458` 断言；改为 `FindFirstTrack()` 无效时按朝向 `DiagDirToDiagTrackdir(DirToDiagDir(this->GetMovingDirection()))` 猜一个（与隧道/桥分支的 "educated guess" 同构），使所有调用点天然安全；`track & TRACK_BIT_WORMHOLE` 分支同样补 `IsValidTrack` 守卫（返回 `INVALID_TRACKDIR`，由调用点守卫兜住，不再在 458 断言）。

**编译**：`_tmp_inc_build.cmd` 增量 `EXIT_CODE=0`、日志末行 `Linking CXX executable openttd.exe`、`build\openttd.exe` @ 2026-09-18 20:22:35（50 647 552 B）。注释同步后已重编一次（`[4/4]` 同款流程）。

**未验证 / 待复测**（本轮只做"断言路径封堵 + 编译通过"，**未复现现场**）：

- `CRT-NOTD` 是否出现；若出现，读该行的**链尾 `trk=`** 即可确认真凶：`0x40`（`TRACK_BIT_WORMHOLE`）⇒ 隧道/桥 (2)；`crashed=1` ⇒ 撞毁 (1)。本轮分析只能排除其余路径，无法区分 (1)(2)。
- 功能回归点：站台边缘**倒车**入位耦合、**正向**入位耦合、nose-to-nose、多段链换端后 `CRT found=1` 正常、`COUPLE-OK`/`REVERSEDIR`/`FOLDCHK` 时序不回归（对照 KI-106 第 59 轮基准：`CPL-GEO` 4 次、`CPL-HIT` 0 次、`gap` 全 0）。
- 残留风险：第 3 条把"无轨道位"的返回值从"断言崩溃"改成"按朝向猜"，若日后出现朝向异常的折返建议，需回看 `CRT-NOTD` 是否被走过。
- 崩溃前的"车组带 `GOTO_COUPLE` 继续被扫为目标"的调度侧时序未深究 —— 本次只保证不再因 trackdir 失效而崩，不保证该重复 `Couple()` 语义本身合理。

**状态**：**部分防护（编译通过，实测待做）** | 严重度：**高**（崩溃）

### KI-108（第 61 轮，2026-09-18）：站台内 `ClearPathReservation` 被喂 `INVALID_TRACKDIR` 断言崩溃（`TrackdirToExitdir` / `track_func.h:396`）

**来源**：玩家实报 `C:\Users\冯洁敏\Documents\OpenTTD\crash-20260918T130500Z.log`（exe `r3r-stable-2026-09-18-m (2)`，Build Sep 18 2026 20:19:18，`DBG_ASSERTS WITH_ASSERT` 全开）；同一现象在 21:12:46（`crash-20260918T131245Z.log`，Build 同为 20:19:18）**再次复现**，两次崩溃上下文逐项相同 ⇒ 该场景可复现。

**崩溃签名**：

```
Assertion failed at line 396 of D:\sourcecode of JGRPP\src\track_func.h: IsValidTrackdirForRoadVehicle(trackdir)
Within context:
  0: TrainController: veh 3 (trk:0x02, tile 867 (39 x 33) Station), front: veh 2 (trk:0x02, tile 7A6 (38 x 30) Railway)
  1: CallVehicleTicks
```

（上下文只列 `DebugContext` scope：`ClearPathReservation()` 是文件内 `static` 且内部无 `SCOPE_INFO`，故不单独成帧，仅 `TrainController` 可见。）

**根因**：`train_cmd.cpp:9547`

```cpp
ClearPathReservation(v, v->tile, v->GetVehicleTrackdir(), true);
```

链内车处于**轨道与朝向不匹配**状态（`track = TRACK_BIT_Y (0x02)`、`direction = DIR_N`，见 `R3R_debug.log` 的 `CRT-NOTD ... back(idx=3 tile=39,33 trk=0x2 dir=0 td=255)`，`td=255` 即 `INVALID_TRACKDIR`）时，`Train::GetVehicleTrackdir()` 走 `TrackDirectionToTrackdir(track, this->GetMovingDirection())`（`track_func.h:456`，不匹配时**断言不触发**、直接返回 `INVALID_TRACKDIR`）；而 `v->tile` 是站台瓦片 ⇒ 进入 `ClearPathReservation()` 站台分支 `:6957 TrackdirToExitdir(track_dir)`（`track_func.h:396` 断言 `IsValidTrackdirForRoadVehicle`）崩溃。该状态无匹配轨道，`track & TRACK_BIT_WORMHOLE` 的早退也不成立；KI-107 只堵了 `CheckReverseTrain` / `FreeTrainStationPlatformReservation` / `RestoreTrainReservation` 等**外部**调用点，这条**内部**调用点漏了。

**修复（仅改 `src/train_cmd.cpp` ⇒ 符合 KI-15 增量合规）**：

1. `Train::GetVehicleTrackdir()` 主路径：`td == INVALID_TRACKDIR` 时改为返回 `TrackToTrackdir(track)`，并落 `VEHTD-MISMATCH` 探针（新增边沿标签 `R3REDGE_VEHTD`，payload = veh/tile/track/direction），使**所有**调用点天然安全；
2. `ClearPathReservation()` 入口兜底：`track_dir == INVALID_TRACKDIR` 时按 `v->track & TRACK_BIT_MASK` 推 `TrackToTrackdir(FindFirstTrack(tbits))`；bits 为空（wormhole/depot/无轨道）直接 `return`；命中写 `VEHTD-CLEARRES`；
3. `TrainMovedChangeSignal()` 两处 `FindFirstTrackdir()`：结果 `== INVALID_TRACKDIR`（该 `dir` 下无任何可达 trackdir）时跳过——第一处不取 `TrackdirToExitdir` 也不调 `UpdateSignalsOnSegment`，第二处不做长预留隧桥检查（同为 `track_func.h:396` 断言类的兜底）。

**编译**：`_tmp_inc_build.cmd` 增量 → `build\R3R_incbuild.done` = `EXIT_CODE=0`；`train_cmd.cpp.obj` @ 22:01:26 晚于源码 @ 21:57:30；`build\openttd.exe` @ **2026-09-18 22:25:44**（50 656 256 B）；`findstr VEHTD-CLEARRES build\openttd.exe` 命中 ⇒ 补丁确在产物内。

**实测（本轮，仅冒烟）**：无头 `-g test_multi_company.sav -v null:until_exit` 跑 60 s（CPU 34 s，进程仍在主循环）⇒ 加载/运行无回归、无断言；日志新增 `LOADCENSUS-PHASE2END bad=0` / `DRAW-STATION-NORES tile=39,31`；未出现 `VEHTD-MISMATCH` / `VEHTD-CLEARRES`（该存档不含裂开的 R3R 链，未触碰该路径，故**不能**算现场验证）。

**待复测**（复现原场景：站台耦合后链车呈 `trk=0x02 dir=0`）：

- 断言崩溃消失；日志应出现 `VEHTD-MISMATCH`（或 `VEHTD-CLEARRES`），读其 `trk=` / `dir=` 可确认现场；
- 功能回归对照 KI-106 / KI-107 基线：站台耦合、倒车入位、多段链换端应保持 `CPL-GEO` 4 次、`CPL-HIT` 0 次、`gap` 全 0、`CRT found=1`。

**更深根因（未修，重要）**：`track` 与 `direction` 不一致本身是 R3R 链编辑（耦合 / 逻辑翻转 / 解挂）留下的**状态损坏**；本轮只是把"查询该状态"从崩溃改成"按 `track` 猜一个 trackdir"，**没有消除不一致状态**。若后续日志持续出现 `VEHTD-MISMATCH`，需回到链编辑提交点（`R3RFlipChainBySegments` / `Couple` / `ReverseTrainSwapVehicles`）补"提交后按 `track` 重算 `direction`"或加提交前校验。

**状态**：**部分防护（编译通过 + 无头冒烟无回归；现场复现待测）** | 严重度：**高**（崩溃）

---

### 4-11 第 62 轮（2026-09-18 夜 ~ 09-19 凌晨）：无头 census 复测（KI-107/108 现场仍未复现）+ 两处"改诊断/改标志"引入的玩家可见回归修复

**触发**：第 61 轮（KI-108）之后，为了复现 KI-107/108 的裂链现场，在 `GameLoop()` 里加了临时诊断（pause 打点 + 自动清暂停 + 每 256 tick `R3RStrandCensus()`）并做无头复测。**这一轮当时的记录漏写**（玩家当面指出"你应该是修完立刻忘记了"），此处补记；顺带补上玩家本次实报的两个回归。

**期间新增的两个崩溃（来自诊断/复测路径本身，不是新的游戏 bug）**：

1. `crash-20260918T153216Z.log`（23:32）：断言 `this == this->First()` @ `vehicle_base.h:669` ← `Vehicle::IsStoppedInDepot` ← **`R3RStrandCensus`**（当时 `train_cmd.cpp:5190`）← `GameLoop`。根因=**探针自己**对链内非头车调 `IsStoppedInDepot`。→ 已改为 `(w == v && wt->IsStoppedInDepot())`，只在链头上判（现 `train_cmd.cpp:5196`）；随后 01:02 的运行跑到 `CENSUS seq=6` 未再断言。记 **KI-111（已修）**。
2. `crash-20260918T161050Z.log`（00:11）：`0xC0000005` **写** 地址 `0x68` ← `Vehicle::RemoveFromShared`（`vehicle.cpp:4616`）← `DeleteVehicleOrders`（`order_cmd.cpp:3551`）← `Vehicle::PreDestructor`（`vehicle.cpp:1244`）← `~Train` ← `CmdSellRailWagon`（`train_cmd.cpp:2781`）← `CmdSellVehicle` ← `ReplaceChain`（`autoreplace_cmd.cpp:852`）← `CmdAutoreplaceVehicle` ← `CallVehicleTicks`（`vehicle.cpp:1853`）。即 **KI-93 家族的"自动替换"新入口**：跨公司多业主链被自动替换卖掉时共享 `OrderList` 簿记被踩。现场证据=同轮 census 打印 `head=2 own=0 n=9 multiown=1`（`ow0`: 2,1,0 / `ow1`: 8..3），即 42,28 库内一条跨公司 9 节链。记 **KI-112（未修）**。

**本轮修复的两个玩家可见回归（玩家实报）**：

- **KI-109 游戏无法暂停**：`src/openttd.cpp` 的诊断块**无条件**执行 `_pause_mode = PauseModes{}`（只放过 `PauseMode::SaveLoad`），玩家一按暂停当帧就被解除 ⇒ 游戏永远停不下来，且整个世界继续跑（连带表现为"列车还在动"）。修复：整块诊断改为**默认关闭**，仅当环境变量 `R3R_DIAG=1` 时生效（无头复测仍可用 `set R3R_DIAG=1` 打开 pause 清除 + census）；顺带避免玩家局里每 256 tick 写 census 日志。
- **KI-110 列车"自行启动"**：`src/train_cmd.cpp`（原 10585）在库内挂车块里**无条件** `consist->vehstatus.Reset(VehState::Stopped)`。该标志就是玩家的"开始/停止"状态；挂车失败时该块每 tick 重入 ⇒ 玩家按下停止后标志被反复抹掉，列车看起来自己启动了。修复：清标志只保留在**挂车尝试期间**（`R3RCanCoupleNow()` 会以 `active-stopped` 拒绝停着的挂车方），**挂车未成功则恢复 `Stopped`**；成功仍不恢复（维持原设计：合并后的链在下一 tick 按车底排程继续）。

**编译与产物**：`_tmp_inc_build.cmd` 增量（仅 `.cpp`，符合 KI-15）⇒ `build\R3R_incbuild.done` = `EXIT_CODE=0`；`.ninja_log` 末段为 `openttd_lib.dir/src/openttd.cpp.obj` → `openttd_lib.dir/src/train_cmd.cpp.obj` → `openttd.exe`（链接耗时 ~748 s）；两个源码 @ 2026-09-19 01:18:12，产物 `build\openttd.exe` @ **2026-09-19 01:30:19**（50 673 664 B）；`findstr /C:R3R_DIAG build\openttd.exe` 命中 ⇒ 补丁确在产物内。

**待办**：

1. 玩家用新 exe 复测：暂停键（含 P / 菜单暂停）必须真的停住；被停止的列车在库内等待挂车时不得自行启动、启动/停止按钮状态不得自行翻转。
2. **KI-112（未修，高）**：自动替换 × 跨公司链的 `RemoveFromShared` 写崩，仍待修（可考虑"R3R 多业主/借用链拒绝自动替换"守卫，或补齐 `OrderList` 借用+删除的交接）。
3. KI-107 / KI-108 的现场（`CRT-NOTD` / `VEHTD-MISMATCH` / `VEHTD-CLEARRES`）仍未复现，保持待测。
4. **KI-113（新，未定位）**：玩家实报"车厢莫名其妙分离"。本轮在备忘与工作区均**查无对应条目**，`src/` 内 grep `R3R\w*(Detach|Strand|Unhook|Stray|Separat|Loose|Lost)` 只命中只读看门狗 `R3RStrandCensus()` ⇒ **无法确认是否真的修过**。需玩家补细节（存档 / 发生时机 / 是否跨公司）后重新定位。

**状态**：KI-109 **已修**、KI-110 **已修**、KI-111 **已修**（均为 `.cpp` 增量，符合 KI-15）；KI-112 **未修**（高）；KI-113 **未定位**（待补细节）。

---

## 附：第 63 轮（2026-09-19）新增条目

### KI-114（第 63 轮，2026-09-19）：`Couple()` 的「方向统一循环」把链头朝向无条件写给整条合并链，使处在无法承载该朝向的轨道上的车辆变成 `(track, direction)` 不一致状态 —— KI-108「更深根因」所指的写入点，现已定位

**一句话**：`Couple()` 合并提交点有一段循环把 `v->direction`（合并链头的朝向）覆盖到 `merged_first` 起的每一辆车；当链跨越多块轨道时（`TRACK_X`→NE/SW、`TRACK_Y`→SE/NW、`TRACK_LEFT/RIGHT`→N/S），所在轨道承载不了链头朝向的那些车就被写成非法组合，直接违背引擎不变式「train 的 direction 恒等于其下方轨道」（`src/train.h:621-629` 注释），并触发 `GetVehicleTrackdir()` 的 `VEHTD-MISMATCH` 兜底。

**来源**：KI-108「更深根因（未修，重要）」＋ 本轮现场 `build\R3R_debug.log`（2026-09-19 01:38，第 62 轮 exe 运行）。

**证据（build\R3R_debug.log，2026-09-19 01:38）**：
- `A3-BOTH-FLIP-DONE merged_head=8`：折叠修正候选 3（双翻）成功。双翻之后每车朝向本来与自身轨道自洽 —— v(idx0/1/2) `4→0`，u 中 idx3-7（在 `TRACK_BIT_Y`）`3→7`、idx8 `4→0`（`R3RReverseChainDirections` 逐节 `ReverseDir`）。
- `COUPLE-OK loco=2 rear=3` 之后全链 9 车的 `CPL idx=` 一律 `dir=0`；其中 `idx=3..7` 的 `trk=0x2`（`TRACK_BIT_Y`，只允许 SE(3)/NW(7)）被写成 0 —— 正是该循环自 `merged_first=8` 起遍历 8→7→6→5→4→3 覆盖的结果。
- 紧随其后 `VEHTD-MISMATCH veh=3 tile=39,33 track=0x2 dir=0 mdir=0 -> coerced=1`。
- 表证据：`_track_direction_to_trackdir[TRACK_Y][DIR_N] == INVALID_TRACKDIR`；`_vehicle_subcoord` 亦表明 `TRACK_Y` 期望 SE/NW（`src/vehicle.cpp:5137-5170`）。

**机制/影响**：该不一致只在「有车进入新 tile」时才由 `VehicleEnterTileCoordinates()`（`src/vehicle.cpp:5172-5185`）按新 tile 的 track 重算 direction 而自愈。列车在耦合点静止期间，这几节货车的 direction 一直是错的：`GetVehicleTrackdir()` 只能走 KI-108 兜底「按 track 猜 trackdir」，而信号/预留/`R3RCheckChainFoldedDirection` 点积/渲染都建立在这个错值上 —— 这正是 KI-107 / KI-108 崩溃窗口的状态来源。耦合完成即写错、直到车动起来才逐步纠正，因此耦合/解挂后的静止期是风险窗口。

**修法（未修，待拍板；仅 `.cpp` 增量，符合 KI-15）**：
1. 首选（最小、贴合原意）：给该循环加「本车轨道能承载链头朝向」的门，不能承载则保留翻转后本来正确的自身朝向：
   ```cpp
   const TrackBits tbits = w->track & TRACK_BIT_MASK;
   if (tbits != TRACK_BIT_NONE &&
       TrackDirectionToTrackdir(FindFirstTrack(tbits), v->direction) == INVALID_TRACKDIR) {
       continue;
   }
   ```
   注意：必须先用 `TRACK_BIT_MASK` 屏蔽 `TRACK_BIT_DEPOT`(0x80) / `TRACK_BIT_WORMHOLE`(0x40)，否则 `FindFirstTrack()` 会取到 `Track::End`，`TrackDirectionToTrackdir()` 会断言崩溃。
2. 兜底：在链编辑提交点（`R3RFlipChainBySegments` / `Couple` / `ReverseTrainSwapVehicles`）之后，按各车 `track` 用「最接近链头移动方向」的 trackdir 重算 direction，即把 (1) 的语义推广到所有链编辑路径。

**状态**：**已修**（第 64 轮，2026-09-19）。**严重度**：中（状态不一致，本身不崩溃；但它是 KI-107/108 崩溃窗口的触发状态，且影响渲染与 trackdir 查询）。

**修法（第 64 轮，2026-09-19，`src/train_cmd.cpp` `Couple()`，见 `R3R_couple_direction_rule_memo.md`）**：不是给旧循环加「轨道能否承载」的门（那只让部分车 `continue`，仍是覆写逻辑），而是**整体废除 `w->direction = v->direction` 的整链覆写**，改为只看拼缝端面两节的环形差判据：`circ <= 1` 不动（一个字节都不改）、`circ == 2` 判事故撞毁（另见 KI-115）、`circ >= 3` 只对拼入段 `R3RReverseChainDirections`（逐节 `ReverseDir` + `Flip(Flipped)` 图像补偿，恒合轨）。
本项正是 KI-114 提出的「修法」的加强版；KI-114 证据里的 A3 双翻现场（`merged_head=8`、idx3..7 `dir=7`、idx8 `dir=0`）在新判据下 `circ=1` ⇒ 走「不动」分支，逐节朝向原样保留，即 KI-114 想要的结果。KI-108 的兜底（`GetVehicleTrackdir()` 返回 `TrackToTrackdir`）**保留**作为其它路径的保险。

**状态行**：KI-109/110/111 **已修**、KI-112 **未修**（高）、KI-113 **未定位**、KI-114 **已修**（第 64 轮）、KI-115 **已实现待实测**（第 64 轮，中）。（均为 `.cpp`，符合 KI-15）。

---

### KI-115（第 64 轮，2026-09-19）：直角（90°）挂车从「照旧强制拼接」改为「判为事故按撞毁处理」——耦合/撞毁边界移动

- **描述**：`Couple()` 新「端面方向判据」`circ = min(|a-b|, 8-|a-b|)`（`a = merged_first->Previous()` 主动方链尾端面、
  `b = merged_first` 拼入段端面）取 `circ == 2` 时，直接对该合并链调 `TrainCrashed(v)` +
  `AddTileNewsItem(GetEncodedString(STR_NEWS_TRAIN_CRASH, num_victims), NewsType::Accident, v->tile)` 并 `return`，
  **不再走排程交接**。此前该几何会被无条件拼接并写坏 `(track, direction)`（即 KI-114 病灶之一）。
- **来源**：用户 2026-09-19 拍板（`R3R_couple_direction_rule_memo.md` §1 问①）。用户认可「现实里没有直角挂车，
  直角插过去属于事故」，并明确**预期并接受**耦合/撞毁边界会因此改变。
- **状态**：已实现（待实测）。**严重度**：中（**语义/手感改动**：原本会「照旧拼上」的场景现在出事故毁车；不是崩溃，但不可逆）。
- **注意点**：
  1. 撞毁发生在**物理拼接之后**（`ArrangeTrains` 已重链），是「已连上的一列车被判事故」，不是「拒绝拼接」。
  2. 直角错位的方向点积 ≈ 0，**通常不会**触发折叠否决（`R3RCheckChainFoldedDirection` 只在 dot > 0 时否决），
     故该分支预期可达；若实测发现总是被折叠否决拦在前面（回滚后下 tick 重试），需把检测**提前到 `TryTrainCouple` 拼前**。
  3. 无图像/位置改动；`circ <= 1` 分支与旧行为逐位等价（旧循环此时本就是 no-op，因为 `w->direction == v->direction`）。
  4. `TrainCrashed` 需前置声明（定义在文件后段 `~8695`，`Couple` 在 `~5979`），已加 `static uint TrainCrashed(Train *v);`。
- **待实测**：直角场景应出现 `COUPLE-SEAM-CRASH circ=2 a=.. b=..` 与事故新闻，且**不再每 tick 刷屏重试**（对齐 KI-06/KI-107 的「折叠修正死循环」观感问题）。

---

## 附：第 65 轮（2026-09-19）新增条目

> 本轮目标：发行前审计 `build\R3R_debug.log`，并逐一排查「每帧调用 / 高频调用」的代码，把探针改成 release 编译可关闭。改动仅落在 `src/*.h` + `src/*.cpp`，**探针开启时逐位等价**（无行为改动）。

### KI-116（第 65 轮，2026-09-19）：`R3R_PROBES=0`（发行构建）下探针并未真正编译掉 —— 每帧全车扫描、每车每 tick 的 `unordered_map` 查找、以及**日志实参求值**仍在跑

**一句话**：探针层原本只有「运行时开关」，`R3RDbgOn()` / `R3RPerfOn()` 只是 `return false`，而调用点与其实参照旧求值、热点函数体也照旧执行到自己的早退为止。

**来源**：用户 2026-09-19「先检查一下有没有什么影响帧率的代码，也就是那些每帧调用的或者高频调用的，然后那些探针也记得调成 release 编译可关闭的」。

**残留开销（审计结论，四类）**：
1. `R3RPerfFrameTick(delta_ms)`（`src/window.cpp:3415`，每**渲染帧**调用；定义在 `src/r3r_perf.h`）—— 探针关闭时仍累加计数器，并在每 128 帧调 `R3RPerfDumpAndReset()`：一次**全车 `Train::Iterate()` 世界扫描** + `fopen("R3R_debug.log","rb")` + `fopen("R3R_perf.log","a")`（会在已发布的 exe 旁边凭空生成 `R3R_perf.log`）。
2. `R3RDbgEdge()`（`src/train_cmd.cpp`）—— 无任何开关，每次调用都做 `unordered_map::find`（miss 还 insert）；调用点是 `Train::ReserveTrackUnderConsist()`（每车每 tick，平台等待闸门）与 `CheckTrainCollision()`（每对碰撞候选每移动步）。
3. `R3RDbgWrite(...)` 是**普通 inline 函数**：即使函数体被折叠成空，**调用点的实参仍会被求值**。`src/couple_group_gui.cpp` 的绘制路径（`CG-PAINT`，每次窗口重绘）就传了 `GetWidget<NWidgetCore>(...)` 这类编译器无法判定为无副作用的表达式。
4. 冷路径但会「偷偷写日志」：`src/sl/station_sl.cpp` 的两个 purge 函数用**裸 `fopen("R3R_debug.log","a")`**，绕过门控。

**修法（已落地）**：
- `src/r3r_perf.h` 新增编译期总开关 `R3R_PROBES`（`#ifndef`，默认取 `R3R_PROBES_DEFAULT`：`build\` = 1，`build-release\` = 0）。`R3RDbgOn()` / `R3RPerfOn()` 仅在 `#if R3R_PROBES` 下含 `getenv` 逻辑，否则 `return false`（编译期常量）。
- `R3RDbgWrite` 在 `R3R_PROBES == 0` 时改为宏 `#define R3RDbgWrite(...) ((void)0)`，连**实参一起**编译掉。刻意**不用** `do {} while (0)` 形式：`((void)0)` 仍是合法单语句，无括号的 `if (...) R3RDbgWrite(...);` 与随后的 `else` 都不会被打断。
- `R3RPerfFrameTick()` 首行 `if (!R3RPerfOn()) return;`；`R3RPerfDumpAndReset()` 入口同样早退（双保险，`R3R_PROBES=0` 时整函数只剩 `ret`）。
- `R3RDbgEdge()` 首行 `if (!R3RDbgOn()) return false;`，把 map 查找彻底挡在后面。
- 无门控自增全部加门控：`fold_check` / `dump_chain` / `dump_ident` / `try_couple`（`src/train_cmd.cpp`）、`pos_helper` / `pos_steps` / `pos_max`（`src/newgrf_engine.cpp` `PositionHelper()`）。
- `src/sl/station_sl.cpp` 两处裸 `fopen("R3R_debug.log","a")` 改为 `R3RFopenDbg("a")`（调用点本就判 `nullptr`，行为不变）。

**状态**：已改（第 65 轮，2026-09-19；**`.h` 改动 ⇒ 必须全量重编**，见 KI-15）。**严重度**：低（不影响正确性，只影响发行版帧率与"后台写日志"）。

**残余 / 未做**：
1. `src/vehicle_base.h:32` 的 `#include "r3r_perf.h"` 经核实**未使用任何探针符号**，纯编译期牵连、运行时零成本。本轮**有意不动**（改它会再强制一次全量重编，收益只是编译时间），留待后续。
2. `src/openttd.cpp:1816-1837` 的 `R3R_DIAG` 暂停诊断块仍由**环境变量**（而非 `R3R_PROBES`）控制，每次游戏主循环有一次可预测分支；未设 `R3R_DIAG` 时 `R3RStrandCensus()` 不会执行。属"显式 opt-in 诊断"，保留。
3. **玩法类高热路径不是探针，本轮未动、也不应关掉**：`PositionHelper()` 的段内定位（真实功能）、`GetVehicleTrackdir()` 的 KI-108 兜底、`ReserveTrackUnderConsist()`、碰撞即耦合、平台等待闸门。
4. `build-release\` 在改过 `src/*.h` 后**增量构建不安全**（KI-15：ninja 读不到 cl 的中文 `注意: 包含文件:` 前缀）。**`R3R_release_build.cmd` 自己不会删 obj**，[6] 只跑 `ninja openttd`；改过头文件时必须先手工 `del /s /q build-release\*.obj`。副产物：`R3RDbgWrite` 变成宏后，**陈旧 obj 会以"未解析外部符号"在链接期报错**，这反而是一道保护。
5. `R3R_release_build.cmd` 的 [5c]/[5d] 硬闸门已实测有效：`build-release\build.ninja` 中 `-DR3R_PROBES_DEFAULT=0` 命中 623 条编译规则，`-DWITH_ZLIB/-DWITH_LIBLZMA/-DWITH_ZSTD/-DWITH_LZO/-DWITH_PNG/-DWITH_OPUSFILE` 齐备。

---

### KI-117（第 66 轮，2026-09-19）：`build\` 调试版在 KI-116 改了 `src/r3r_perf.h` 之后**未全量重编**，其 `openttd.exe` 是「陈旧 obj + 新头文件」的混合二进制

**一句话**：KI-116 改的 `src/r3r_perf.h`（mtime 2026-09-19 05:51:19）触发的是**全量重编**规则（KI-15）；`build-release\` 照做了（623/623 obj 晚于该头文件），但 `build\` **只重编了 6 个 obj**（613/619 obj 早于该头文件），所以 `build\openttd.exe` 现在是混合布局二进制 —— 按 KI-15 / 记忆 66636022 的经验，这类二进制会随机崩溃，拿它调试会凭空多出一轮假崩溃。

**来源**：第 66 轮发行版加固后的「构件一致性核查」（不属于任何既有 KI 的现场）。

**证据**：
- `src/r3r_perf.h` LastWriteTime = 2026-09-19 05:51:19。
- `build-release\`：623 个 `.obj`，晚于该头文件的 = 623（陈旧 0）；619 个编于 06:xx 小时、4 个编于 07:36（本轮最后的探针泄漏修复：`openttd.cpp` / `sl/saveload.cpp` / `sl/vehicle_sl.cpp` / `os/windows/win32.cpp`）；`openttd.exe` 链接于 07:40:20（22 707 712 B，RelWithDebInfo + `/O2 /Ob2`）。
- `build\`：619 个 `.obj`，晚于该头文件的仅 6（陈旧 613）；`openttd.exe` 06:58:48（50 675 712 B，Debug）。

**影响**：`build\openttd.exe` 既可能因 vtable / 结构体布局不一致而随机崩溃，又仍带旧头文件的探针语义（`R3R_DBG` 等在那份 exe 里照旧生效）。**发行版 `build-release\openttd.exe` 不受影响**，已完成无探针验证（见下节）。

**修法（未做，等真正要用调试版时再执行）**：在 `build\` 下 `del /s /q *.obj`，于 vcvars64 环境全量重编（约 25–40 分钟，见 KI-15）。**在此之前不要用 `build\openttd.exe` 判断崩溃真伪，也不要拿它的帧率做基线。**

**状态**：**已修（2026-09-22 第 94 轮：`build\` 已完成全量重编）**。证据：`build\R3R_fullbuild.done`
= `EXIT_CODE=0` @ 2026-09-22 04:37:01；620 个 `.obj` **全部**晚于 `src/` 下最新的头文件/语言文件
（`order_type.h` 03:48:46、`widgets/order_widget.h` 03:49:56、`lang/*.txt` 03:50、`train.h` 01:32、
`station_base.h` 09-19 23:09）→ 陈旧 obj 计数 = **0**，混合二进制问题消除；此后（04:37 之后）的改动
**全部是 .cpp**（`rail_cmd.cpp` 05:00、`sl/saveload.cpp`+`sl/vehicle_sl.cpp`+`pbs.cpp` 20:39–20:40、
`train_cmd.cpp` 21:33、`couple_group.cpp` 21:35、`yapf_rail.cpp` 21:42），故 `build\openttd.exe`
（21:52:43）不再是混合布局二进制，**可以**用于调试与崩溃真伪判定。**严重度**：中（会产生错误结论，
但产物本身可重建）。

---

### 发行版无探针验证（第 66 轮，2026-09-19）—— KI-116 关闭依据

**一句话**：`build-release\` 全量重编得到的 `openttd.exe` 已证明**不含任何探针**：任何环境变量都无法让它写出日志文件。

**证据**：
1. 二进制字符串扫描（`findstr /m /c:<s> build-release\openttd.exe`）：`R3R_debug.log` / `R3R_perf.log` / `R3R_slref.log` / `R3R_vehsraw.bin` / `R3R_DBG` / `R3R_PERF` / `R3R_DIAG` 全部 **found=0**。
2. 无头运行：先清空 `build-release\R3R_*`，再设 `R3R_DBG=1` `R3R_PERF=1` `R3R_DIAG=1` 运行 `openttd.exe -g test_multi_company.sav -v null:until_exit`，持续 60 秒（进程存活到 60 秒才被主动结束，无崩溃日志），结束后 `build-release\` 下 R3R 日志文件数 = **0**。
3. 构建门禁：`build-release\build.ninja` 含 `-DR3R_PROBES_DEFAULT=0`（623 条编译规则命中），六个 `-DWITH_*` 依赖齐备。
4. 时间戳核对：改动的源码（最晚 05:53 左右的编辑）早于 `build-release\openttd.exe`（07:40:20），且 `build-release\R3R_incbuild.done` = `EXIT_CODE=0`。

**残余字符串（已知、良性、不写任何文件）**：exe 内仍可见三类 —— `R3R_PROBES_DEFAULT`（宏名本身）、`INVALID_REF_VEHICLE`（`sl/saveload.cpp` 里位于 `if (r3rf != nullptr)` 块内的格式串，`r3rf` 恒为 `nullptr`，永不执行）、`COUPLE-FLIP`（作为 tag 实参传给 `ChainFolded()` / `SpliceFolded()` / `R3RDbgWrite()` 等**非内联**函数，函数实参无法被 `((void)0)` 宏丢弃）。三者在 `R3R_PROBES=0` 下都不产生文件写入。

**状态**：已修（第 66 轮，2026-09-19）。**严重度**：低（已闭环）。

---

## 附：第 67 轮（2026-09-19）新增条目

### KI-118（第 67 轮，2026-09-19）：车库「停放豁免」在目标车库里**无条件**清掉主动方的 `VehState::Stopped` —— 硬闸门的「双方均须启动」条件在库内等于失效，与 KI-60 宣示的期望不符

**一句话**：`R3RCanCoupleNow()`（`src/train_cmd.cpp:6353`）第 4 条判据是 `coupler->vehstatus.Test(VehState::Stopped)` → `reject=active-stopped`（`train_cmd.cpp:6365`），但 `TrainLocoHandler()` 的车库挂车块（`train_cmd.cpp:10555-10653`）为了给 R3R 自己"停在库里"的列车留出执行 GOTO_COUPLE 的通道（`MakeSegment` / `DemoteSegment` 会调 `R3RStopChainInDepot` 置 `Stopped`，`vehicle_cmd.cpp:328-338`；停放列车不跑 `ProcessOrders`，`current_order` 恒为 `OT_NOTHING`），会在**每次尝试前**把 `Stopped` 清掉、并从 `orders` 按 `cur_real_order_index` 取出 GOTO_COUPLE 填进 `current_order`（`10625-10631`、`10640-10641`）。于是**任何**停在「GOTO_COUPLE 目标车库」内的机车 —— 无论它是 R3R 停的、玩家自己按了「停止」的、还是**新建后从未启动**的 —— 都会每 tick 触发一次挂车尝试。

**来源**：玩家 2026-09-19 实测质疑（「我在发行版的车库里机车都没有启动，就只设定了一个前往车库挂接的命令，结果就自动吸附到车底上面和它耦合了」）；即 KI-60「双方均须启动」期望的现场反例。

**证据（源码 + 版本核对）**：
- 闸门条件：`train_cmd.cpp:6365` `else if (coupler->vehstatus.Test(VehState::Stopped)) reason = "active-stopped";`（其余条件为 `6359` active-not-goto / `6361` target-not-segment / `6363` target-not-wait / `6367` target-stopped / `6369` group-mismatch）。
- 豁免实现：`10640-10641` `const bool r3r_was_stopped = consist->vehstatus.Test(VehState::Stopped); consist->vehstatus.Reset(VehState::Stopped);` → `10642` `TrainCoupleHandler(consist)`；只有**未挂上**时 `10653` 才还原（KI-110）。该还原还额外要求 `consist->IsFrontEngine()`。
- 触发面：`Train::Tick()`（`11162-11167`）对 front engine **无条件**调 `TrainLocoHandler()`，`Stopped` 只影响 `running_ticks` 统计（`11163`）⇒ 停放列车照样每 tick 进入车库挂车块。
- 豁免门：`r3r_pending_depot_couple`（`10570-10574`）= `IsEngine() && r3r_pend->IsType(OT_GOTO_COUPLE) && GetCoupleIsDepot() && IsRailDepotTile(tile) && 目的地 == 当前库`；`10579` 的「停着就 bail out」门正是被它跳过。
- **排除旧 exe**：发行包 `R3R-v1.0.1-win64-2026-09-19.zip` 内 `openttd.exe` = 2026-09-19 **07:40**（22 707 712 B，与 `build-release\openttd.exe` 同尺寸同时间戳），晚于 `src\train_cmd.cpp` **05:50:36** ⇒ 该豁免确实在玩家运行的构建内，不是 KI-51 那类"exe 早于源码"的问题。

**已核实的边界**：被动方（车底）仍须过 `IsSegmentFront()` + `OT_WAIT_COUPLE` + `!Stopped` + 挂接分组相容；`CheckTrainStayInDepot` 对 `R3RIsCarOnlyFormation` 免于"零功率自动停止"（`6802-6806`）、对 `OT_WAIT_COUPLE` 原地停放（`6818-6820`）⇒ 库里等待的车底通常 `Stopped` 为 0，可以成为合法目标。因此"未启动也挂上"失效的**只有主动方**这一侧。

**影响**：玩家无法用「停止/启动」按钮阻止库内机车挂车；「新建机车 + 一条指向本库的 GOTO_COUPLE」会立即吸附。KI-60 的四条件在实际车库场景中退化为三条件。

**修法（方向 1 已采用并落地，第 68 轮，2026-09-19；其余方向留档）**：代码无法区分"R3R 因工具而停"与"玩家主动停 / 从未启动"——两者是同一个 `VehState::Stopped`。可选方向：
1. 让 R3R 自己的停放改用**独立标志**（如 `r3r_parked`），豁免只认该标志；玩家按「停止」即不挂车。语义最干净，但要碰 `src/*.h` ⇒ 全量重编，且需梳理全部 `R3RStopChainInDepot` 调用点。
2. 保留现状，在 UI / 文档中明确「库内 GOTO_COUPLE 不看启动状态」。
3. 只豁免"库里确实存在合法车底候选"的情形（≈ 现状，无实质改善）。

**状态**：**部分防护（第 68 轮，2026-09-19）**：采用方向 1，代码已落地，编译与实测待做。**严重度**：中（行为与已声明的规则不符；不崩溃，但玩家无法用"停止"表达不欲挂车）。

**第 68 轮落地（方向 1：独立标志 `r3r_parked`）**：
- `src/train.h`：新增 `Train::r3r_parked`（SAVED），语义 =「本车的 `Stopped` 是 R3R 车库编辑工具停的，不是玩家意图」。
- `src/sl/saveload_common.h`：新增 `SLV_R3R_PARKED`，`SAVEGAME_VERSION` 推进到它（避免复用已存在的 `SLV_R3R_ARTIC_OVERRIDE` 导致同版本号存档布局错位）。
- `src/sl/vehicle_sl.cpp`：`_train_desc` 追加 `NSL("r3r_parked", SLE_CONDVAR(Train, r3r_parked, SLE_BOOL, SLV_R3R_PARKED, SL_MAX_VERSION))`。
  - 附带发现（未修、不属本轮范围）：R3R 既有的 `Train::weight_override / power_override / max_speed_override` 只登记在 `src/saveload/vehicle_sl.cpp` 的 `_vehicle_train_sl_desc`，而该文件整体位于 `namespace upstream_sl`，仅在 `_sl_upstream_mode`（读原生 OpenTTD 存档）时使用；当前 JGRPP 格式的表 `src/sl/vehicle_sl.cpp` 内并无这三个字段，即它们在正常 R3R 存档中**不会持久化**（`_sl_upstream_mode` 下 `_sl_version = MAX_LOAD_SAVEGAME_VERSION < SLV_R3R_ARTIC_OVERRIDE`，条件也永不成立）。
- `src/vehicle_cmd.cpp` `R3RStopChainInDepot()`：置 `Stopped` 处同时置 `t->r3r_parked = true`（函数开头「已经停着就直接 return」的行为保留——已停车的 `Stopped` 属于玩家意图，不该被贴 R3R 标签）。
- `src/vehicle_cmd.cpp` `CmdStartStopVehicle()` execute 分支：玩家显式 start/stop 视为接管，清 `Train::From(v)->r3r_parked`。
- `src/train_cmd.cpp`：豁免门 `r3r_pending_depot_couple`（`10571` 起）加入 `consist->r3r_parked`；挂车成功分支（`r3r_coupled`）清 `consist->r3r_parked`。
- 行为矩阵：R3R 工具停的（`r3r_parked=1` + 订单含指向本库的 `GOTO_COUPLE`）→ 豁免、就地挂车；玩家按停止的 / 新建未启动的（`r3r_parked=0`、`Stopped=1`）→ 被 `Stopped && cur_speed==0` bail out，不再挂车；运行中到达目标库执行 `GOTO_COUPLE` 的（`Stopped=0`）→ 仍走 `current_order.IsType(OT_GOTO_COUPLE)` 分支正常挂车。
- 待复测：见 `R3R_station_yard_and_fixA_memo.md` §1.6（5 条）。

---

## 附加：第 69 轮（2026-09-19）新增条目

### KI-119（第 69 轮，2026-09-19）：widget 头文件枚举行内注释里的**非 ASCII（中文）字符**会被 `GenerateWidget.cmake` 渲染成换行，破坏生成的 `script_window.hpp`，使全量重编在第一步即崩

**一句话**：`GenerateWidget.cmake`（`cmake/scripts/GenerateWidget.cmake`，经 `src/script/api/CMakeLists.txt:15-24` 的 ninja 自定义命令调用，输入 `src/widgets/*_widget.h`，输出 `build/generated/script/api/script_window.hpp`）逐行搬运枚举行；`src/widgets/station_widget.h:35` 的 `WID_SV_R3R_YARDS` 行内注释含中文「站场」，生成产物中该中文被替换为**一个换行**，于是 `enum StationViewWidgets` 的成员被劈成两行（第 1 行结尾 `... station yard (`，第 2 行顶格以 `) management window.` 开头），枚举体结构被破坏，其后各枚举块级联报 `error C2011: "XXXWidgets": "enum" 类型重定义` 以及 `C2059` / `C2143` / `C3927` 等语法错误。

**来源**：第 69 轮站场下拉框（`WID_O_R3R_YARD` / `WID_O_SEL_TOP_YARD`）开发；用户选择"全量重编"后首次复现。

**证据**：
- 失败日志 `build\R3R_fullbuild.log` 第一条错误为 `script_window.hpp(2853): error C2059: 语法错误:"）"`，首个 C2011 为 `script_window.hpp(2857): "StationYardWidgets"`，随后几十条级联；`ninja: build stopped: subcommand failed`，编译在 47/619 个 obj 处中断。
- 生成产物 `build\generated\script\api\script_window.hpp:2852-2853` 实测断成：`WID_SV_R3R_YARDS = to_underlying(::StationViewWidgets::WID_SV_R3R_YARDS), ///< R3R: open the station yard (` + 换行 + `) management window.`。
- 源头 `src/widgets/station_widget.h:35` 原为 `WID_SV_R3R_YARDS, ///< R3R: open the station yard (站场) management window.`。
- 全量扫描 `src\widgets\*.h`（逐行正则 `[^\x00-\x7F]`）确认**仅此一处**含非 ASCII；该文件无 BOM、头部字节 `2F 2A 0D 0A`，是合法无 BOM UTF-8。

**影响**：任何人在 `src/widgets/*_widget.h` 的枚举成员注释（`///< ...` 或块注释）里写中文，都会让**全量重编在生成 `script_window.hpp` 后立即失败**；且报错位置（生成文件）与真正源头（widget 头文件）相距很远，极易被误判为生成器或脚本缺陷。

**修法（已修，第 69 轮）**：将 `src/widgets/station_widget.h:35` 的注释改为纯 ASCII（`///< R3R: open the station yard management window.`）。`src/widgets/` 是被 `GenerateWidget.cmake` 扫描的唯一目录；`src/table`、`src/script/api`、`src/script/api/template` 已核实为零非 ASCII。

**状态**：**已修**（第 69 轮，2026-09-19）。**严重度**：高（构建中断；且发生在全量重编首步，会阻塞其后全部验证）。

**规约（长期）**：`src/widgets/*_widget.h` 的枚举注释**一律只用 ASCII**；中文说明写进 `src/lang/*.txt` 或工作区备忘，不要写进 widget 头文件。

### KI-120（第 69 轮，2026-09-19）：`Station::R3REnumeratePlatforms()` 使用了**全仓库不存在的宏 `TILE_AREA_LOOP`**，全量重编时 MSVC 报 19 条级联错误

**一句话**：`src/station_cmd.cpp:2086` 写成 `TILE_AREA_LOOP(t, ta) { ... }`，但 `TILE_AREA_LOOP` 在仓库中不存在（全量检索仅命中该行 1 处，`#define TILE_AREA_LOOP` 命中 0 处）；MSVC 遂把该宏名当函数调用，报 `C3861: "TILE_AREA_LOOP": 找不到标识符` 与 `C2065: "t": 未声明的标识符`，随后 17 条语法级联（C2059/C2044/C4430/C2365/C3927/C3484/C3613/C2146/C2143/C2447），该 TU 编译失败。

**来源**：第 69 轮站场下拉框开发中，用户选择"全量重编"后暴露。此前只做增量构建，而 `src/station_cmd.cpp` 一直未改动过，所以这段代码**从未被真正编译过**。

**证据**：
- `build\R3R_fullbuild.log:675-693`：首条错误 `station_cmd.cpp(2086): error C2065: "t": 未声明的标识符`，紧接 `C3861: "TILE_AREA_LOOP": 找不到标识符`；`ninja: build stopped`，全量重编在 563/620 个 obj 处中断。
- 同文件 2159 行及 2164 行使用的是正确写法（`for (TileIndex t : tiles)` / `for (TileIndex s = t; IsCompatibleTrainStationTile(s, t); s += TileOffsByDiagDir(south))`）。

**影响**：构建中断。也说明"只做过增量构建"的模块可能藏有同类编译错误——本次全量重编已把全部 620 个 obj 逐一重编成功，可确认现已不存在其它同类问题。

**修法（已修，第 69 轮）**：改为对 `TileArea` 的标准索引遍历：

```cpp
for (TileIndex t = ta.tile; t < ta.tile + ta.w * ta.h; t++) {
```

**状态**：**已修**（第 69 轮，2026-09-19）。**严重度**：高（构建中断）。

**规约（长期）**：`src/` 下新增代码若引用了新宏或新工具函数，必须至少跑一次**会真正编译到该 TU** 的构建（全量重编，或删除该 .cpp 的 .obj 后重编），不能只依赖"只重编改动过的 .cpp"的增量构建。


---

## 附：第 69 轮构建验证与状态更新（2026-09-19 21:12）

**验证事实（时间戳链）**
- `src/` 全部头文件最新 mtime = `src/widgets/station_widget.h` @ 20:33:58（KI-119 修复后的纯 ASCII 版本）。
- `build\` 共 620 个 `.obj`，**全部晚于上述头文件**（`late=620/620`）：最早 obj @ 20:35:09、最晚 @ 21:09:01；`src/station_cmd.cpp` @ 21:01:34 → `station_cmd.cpp.obj` @ 21:06:33；`build\openttd.exe` @ **21:12:23**（50 747 904 B）晚于全部 obj。
- ⇒ `build\` 已是**一次完整、内部一致的全量重编产物**（20:35 起删光 obj 重建），不存在 KI-15 / KI-117 的「陈旧 obj + 新头文件」混合布局。

**过程说明（重要工具链陷阱）**：20:57 的第一次全量重编以 `EXIT_CODE=1` 中断，19 条错误全部落在 `src/station_cmd.cpp`（`R3REnumeratePlatforms()` 内 `out` 未声明等，旧行号 2091-2096）。修复该函数后**重跑 `ninja` 续编**，21:12 以 `EXIT_CODE=0` 完成。

- 因此 `build\R3R_fullbuild.done` 里残留的 `EXIT_CODE=1` 是**陈旧标记**，不能作为判定依据；同轮的 `build\R3R_incbuild.done` = `EXIT_CODE=0` 才是收口记录。
- **规约（长期）**：`R3R_fullrebuild.cmd` 失败后重跑即可续编（ninja 有状态），但**判定成功必须看最后一次 ninja 的退出码**，不要只看 `.done` 标记文件。

**条目状态更新**
- **KI-117**（`build\` 混合陈旧 obj）：**已修（第 69 轮）** —— 本次全量重编后 620/620 obj 一致，`build\openttd.exe` 可重新用于崩溃真伪判断与帧率基线。
- **KI-118**（车库停放豁免）：由「部分防护」升为**已修（编译已验证）** —— `r3r_parked` 全链路（`src/train.h` / `src/sl/saveload_common.h` / `src/sl/vehicle_sl.cpp` / `src/vehicle_cmd.cpp` / `src/train_cmd.cpp`）已进入 `build\openttd.exe` @ 21:12:23；**运行时 5 条待复测项仍未做**（见 `R3R_station_yard_and_fixA_memo.md` §1.6）。
- **KI-119**（widget 头枚举行内注释非 ASCII）：**已修并验证** —— 修复后的 `station_widget.h`（20:33:58）参与本次全量重编，`script_window.hpp` 生成正常，未再出现 `C2011` 级联。
- **KI-120**（`TILE_AREA_LOOP` 不存在）：**已修并验证** —— `src/station_cmd.cpp:2086` 现为标准 `TileArea` 索引遍历，该 TU 于 21:06:33 编译成功；620 个 obj 全部重编成功，可确认不存在其它同类「从未被编译过」的错误。
- **新增待办（未修，中）**：`build-release\openttd.exe` 仍是 09-19 **07:40:20** 的旧版（22 707 712 B），**不含**车站站场（`SYRD`）/ `r3r_parked` / KI-116~120 的全部改动 ⇒ 若要出下一版发行包，必须先跑 `R3R_fullrebuild.cmd [1]` 全量重编 `build-release\`（须先关掉 CodeBuddy，KI-24）。

**仍待实测（需用户交互，AI 无法代跑）**
1. 站场：车站窗口建场（划入 A/B/共用场）、订单选场下拉、双语字符串显示；场满 → 回落共用场；未划场平台不被带场订单使用；车站扩建/拆除后场 tile 修剪无悬垂。
2. KI-118 的 5 条（`R3R_station_yard_and_fixA_memo.md` §1.6）：工具停在库里的列车仍能自动挂；新建未启动机车不再吸附；玩家按「停止」的机车不挂车；工具停的列车存档/读档后仍能挂；运行中到达目标库执行 `GOTO_COUPLE` 无回归。
3. 老存档零回归（未划任何场的存档行为逐字节一致）。（第 70 轮起站场改为「每车站动态 N 场」，本清单第 1 条的口径随之更新为 N 场，见 KI-121。）

---

## 附：第 70 轮（2026-09-19）新增条目（站场：固定三场 → 动态 N 场；全量重编收口）

- **KI-121（第 70 轮，2026-09-19）：站场由「固定三场（A/B/共用）」重构为「每车站动态 N 场」，`SYRD` 升 v2（含 v1 迁移），订单目的地场下拉 / 站场管理窗口 / 双语字符串同步改造。状态：已实现、编译通过；运行时未验证。严重度：中（行为变更，涉及存档格式）。**
  - 模型（`src/station_base.h`）：`Station::r3r_yards`（`std::vector<Station::R3RStationYard>`，每场 `{ std::vector<TileIndex> tiles; bool shared; }`）；场 ID = 下标 + 1，`R3R_YARD_NONE = 0` 表示「整站（不限定场）」，`R3R_MAX_YARDS = 65534`（受订单 16 位存储上限约束）；`R3R_YARD_A/B/SHARED = 1/2/3` 仅作 v1 兼容常量保留，逻辑上已无引用。
  - **场 ID 稳定规约**：场只追加、从不删除；「删场」= `R3RClearYard()` 清空其 tile 而不移动下标，以免已有订单的场 ID 失效。
  - 存档（`src/sl/station_yard_sl.cpp`，本轮**新增文件**，已加入 `src/sl/CMakeLists.txt`）：为不改动 `SYRD` 字段布局，v2 把所有场数据编码进旧成员 `r3r_yard_a`（`SLE_VARVEC(..., SLE_UINT32)`），`r3r_yard_b` / `r3r_yard_shared` 恒空，仅 v1 迁移时读取。编码 = `[word0 = #SYRD_V2_MAGIC][word1 = 场数 N(低16位)][每组: 共享标志, tile 数 M, M 个 tile]`；`yard_a` 不以 MAGIC 开头 ⇒ 走 v1 迁移（A→场1、B→场2、共用→场3(shared)，恒等映射，旧档里订单引用的场 ID 继续有效）。保存时用 `r3r_yard_a` 当编码缓冲，写完即 `clear()` 释放。
  - 其余落点：命令参数升为 `uint16_t`（`src/station_cmd.h` 的 `SetR3RStationYard` / `SetR3RStationYardShared`）；订单窗口 `WID_O_R3R_YARD` 按**目的地站**动态列出「整站 + 各场」；车站窗口新增站场管理窗口（`WID_SY_YARD_SEL` 场下拉、`WID_SY_ASSIGN` 把选中平台划入/移出某场、`WID_SY_NEW_YARD` 追加空场、`WID_SY_TOGGLE_SHARED` 共享开关、`WID_SY_LOCATE`）；双语串 `STR_R3R_YARD_NAME_N(_SHARED)` / `STR_ORDER_R3R_YARD_NAMED(_SHARED)` 等（中英文案同步）。
  - **待复测（需用户交互，AI 无法代跑）**：①建场后订单下拉能列到场、选场后列车只在该场平台停靠；②把平台划入/移出场的即时生效与回落行为；③共享场的回落语义（本场停满 → 回落共享场）；④**老档迁移**：无 `SYRD` 的旧档、以及 v1 三场 `SYRD` 旧档，加载后场 1/2/3 与原 A/B/共用一一对应；⑤**存读往返**：游戏内建场 → 存档 → 读档，场数据与订单引用逐字节一致（本轮**完全未测**）；⑥车站扩建/拆除后 `R3RPruneYardTiles()` 修剪生效、无悬垂 tile。

- **KI-122（第 70 轮，2026-09-19）：`src/sl/station_yard_sl.cpp` 首次进入编译，暴露两类「从未被编译过」的错误（嵌套类型未全限定 + `TileIndex` 强类型无隐式转换）。状态：已修（编译验证）。严重度：中（阻塞构建）。**
  - (a) `Station::R3RStationYard` 是 `Station` 的**嵌套类型**，在自由函数里必须写全限定名 `Station::R3RStationYard`。原始错误：`station_yard_sl.cpp(69) C4430 缺少类型说明符 - 假定为 int` + `C2143 语法错误` + `C2065 "y": 未声明的标识符`，并级联到 70/71/72/90/92/106/107/108/132/133/134 行。
  - (b) `TileIndex` 是**强类型**（`StrongType` + integer mixin，**没有到 `uint32_t` 的隐式转换**，且 `const` 对象上取 `&` 亦不成立）：`in[p++] & 0xFFFF` → `C2678 二进制"&": 没有找到接受"const _Ty"类型的左操作数的运算符`（note 指出 `TileIndexIntegerMixin::mixin<...>::operator &` 的 `this` 指针转换丢失限定符）+ `C2737 "n": 必须初始化 const 对象`；`const uint32_t m = in[p++];` → `C2440 无法从"const _Ty"转换为"uint32_t"`。
  - 修法：一律经 `.base()` 显式取底层值（`in[0].base() != SYRD_V2_MAGIC`、`in[p++].base() & 0xFFFF`、`in[p++].base()`）。
  - 影响：本轮首次全量重编在 `[427/707]` 处 `ninja: build stopped: subcommand failed.`（`build\R3R_fullbuild.done = EXIT_CODE=1`，742 行日志中错误全部落在该文件）。

- **KI-123（第 70 轮，2026-09-19）：`Order::GetDestination()` 返回 `DestinationID`，不是 `StationID`，不能直接喂给 `Station::GetIfValid()`。状态：已修（编译验证）。严重度：中（阻塞构建）。**
  - 现象：`src/order_cmd.cpp(2471)`、`src/order_gui.cpp(805)` 与另一处同型写法编译失败，报错落在 `src/pool_type.hpp(390/401) error C2664`，模板参数 `_T0 = DestinationID`（`SpecializedStation<Station,false>::GetIfValid<DestinationID>` → `GetRawIndex(size_t)` 无法从强类型转换）。
  - 修法：`Station::GetIfValid(order->GetDestination().ToStationID())`。
  - **规约（长期）**：本 fork 起 `Order::GetDestination()` 是 `DestinationID`，凡把它当站/航点/库 ID 使用时，必须显式 `ToStationID()` / `ToWaypointID()` / `ToDepotID()`。

**第 70 轮构建验证与产物（证据链）**
- 全量重编脚本 `_tmp_full_build.cmd`（先删光 `build\**\*.obj`，再 `ninja -C build -j 2 openttd`；日志 `build\R3R_fullbuild.log`，标记 `build\R3R_fullbuild.done`）。第 1 次 `EXIT_CODE=1`，中断于 `[427/707]`（KI-122）。
- 续编脚本 `_tmp_resume_build.cmd`（**不删 obj**，`ninja -C build -j 2 -k 0 openttd`；日志 `build\R3R_resume.log`，标记 `build\R3R_resume.done`）。**`-k 0` 是刻意选择**：一次跑完所有剩余 TU 并一次性报出全部错误，避免「错一个 → 等十几分钟 → 再错一个」的多轮往返（本轮 3 个文件的编译错误因此只用 3 次续编就收口）。
- 收口：`build\R3R_resume.done = EXIT_CODE=0`；`build\openttd.exe` @ **2026-09-19 22:51:53**（50 779 136 B）。
- 一致性（防 KI-15 / KI-117 混合布局）：`build\` 共 **620** 个 `.obj`，最早 @ 22:15:07、最晚 @ 22:50:05，**全部晚于 `src/` 最新头文件**（`src/widgets/order_widget.h` @ 22:12:34）⇒ 本次是一次完整、内部一致的全量重编。
- 冒烟测试：`build\openttd.exe -g "<Documents>\OpenTTD\save\test_multi_company.sav" -v null:until_exit` → 退出码 **0**，`build\R3R_debug.log`（357 行）仅剩 `DRAW-STATION-NORES` 诊断行，无异常。
- **工具链陷阱（新增，重要）**：同一构建脚本**并发运行**会互相截断同一个 `*.log` / `*.done`，导致「日志里明明还在报错、源码却已经是对的」这种自相矛盾的现场（本轮真实踩到：第 1 次续编被后台化后仍在跑，其失败日志与第 2 次续编日志混在同一文件）。判定前**必须先 `tasklist | findstr /i "ninja cl.exe"` 确认无残留构建进程**，且以**最后一次 ninja 的退出码**为准。
- **未做**：①`SYRD` 存/读往返（需游戏内建场，见 KI-121 待复测第 ⑤ 项）；②`build-release\` 仍是 09-19 07:40:20 旧版，**不含**本轮 N 场改动，若要出发行包须先跑 `R3R_release_build.cmd` 全量重编（且须先关掉 CodeBuddy，KI-24/KI-25）。

## 附：第 71 轮（2026-09-19）新增条目（站场：目的地场选不中 + 共享场改为「成对/成组回落」）

- **KI-124（第 71 轮，2026-09-19）：订单窗口「目的地场」下拉无论选哪个场都报「不能执行这个命令……」（`STR_ERROR_CAN_T_MODIFY_THIS_ORDER`），选项根本提交不出去。状态：已修（第 71 轮已全量重编验证，`EXIT_CODE=0`；**游戏中实际点选仍待用户复测**）。严重度：中（新增功能完全不可用）。**
  - 现场：玩家在订单窗口点 `WID_O_R3R_YARD` 下拉、选中任一场后，界面右下角提示「不能执行这个命令……」。
  - 根因（`src/order_cmd.cpp`）：`CmdModifyOrder` 开头会按订单类型校验 `ModifyOrderFlags` 白名单，`OT_GOTO_STATION` 分支（`:2089`）列举了该类型允许改的 `mof`，**未包含第 70 轮新增的 `MOF_R3R_YARD`**，于是合法请求在参数校验阶段就 `return CMD_ERROR`。注意总闸门 `if (mof >= MOF_END) return CMD_ERROR;`（`:2067` 附近）**不是**原因：`MOF_R3R_YARD` 排在 `MOF_DECOUPLE_BOUNDARY` 之后、`MOF_END` 之前，能过。
  - 修法：白名单追加 `&& mof != MOF_R3R_YARD`。
  - **规约（长期）**：本 fork 每新增一个 `ModifyOrderFlags`，必须同时在 `CmdModifyOrder` 里把**全部可用的订单类型分支**加进白名单，否则 UI 会以「不能执行这个命令」的形式静默失败（且不会有任何编译期提示）。

- **KI-125（第 71 轮，2026-09-19）：共享场由「全场共享同一个池」改为「每个场各自指定一个共享回落场」，以表达「1/2 场共享一个 A 场、3/4 场共享另一个 A 场」。状态：已实现（第 71 轮已全量重编验证，`EXIT_CODE=0`）；`SYRD` v3 存读往返与 UI 交互未实测。严重度：中（行为与存档格式变更）。**
  - 玩家口径：**不是**所有场共享一个 A 场，而是**若干个场各自把 A 场当作自己的回落场**——例如 1 场与 2 场都回落到 A 场，3 场与 4 场都回落到另一个 A 场。
  - 数据层（`src/station_base.h`）：`R3RStationYard::shared`（bool，全局共享池）→ `uint16_t shared_with`（本场停满时的回落目标场 ID；0 = `R3R_YARD_NONE` = 不回落）。回落**只做一层、不链式展开**（A 场若也有回落不会继续展开）。
  - 接口：`R3RAddYard()` 去掉 `shared` 形参；新增 `R3RYardSharedWith()` / `R3RSetYardSharedWith()`；`R3RIsYardShared()` 语义改为「是否被别的场指定为回落目标」（**仅用于 UI 标签**，不再参与回落判定）；`R3RCollectSharedYardTiles()`（合并所有共享场）删除，改为 `R3RCollectYardTiles(uint16_t yard, out)`（收集单个场的有效铁路 tile，升序去重）。
  - 判据落点（`src/station_cmd.cpp` `R3RCollectYardDestinationTiles()`）：该场已满时并入 `tiles(st->R3RYardSharedWith(yard))`。**前提仍是「场不空」**：场里没有任何有效 tile 时 `out` 为空，调用方按「整站」处理（与第 70 轮一致）。
  - 命令：`Commands::SetR3RStationYardShared` 第三参由 `bool` 改为 `uint16_t`（`CmdDataT<StationID, uint16_t, uint16_t>`）；执行体允许「新建空场」（`yard == R3R_YARD_NONE`）或「设置某场的回落场」，并拒绝「回落到自己」与「回落到不存在的场」。
  - GUI（`src/station_gui.cpp` / `src/widgets/station_widget.h`）：`WID_SY_TOGGLE_SHARED`（开关按钮）→ `WID_SY_SHARED_SEL`（`NWID_BUTTON_DROPDOWN`），列表 = 「无」+ 除当前场外的各场，按钮文本 `共享: [k 场]` / `共享: 无`；新增 `SelectedSharedYard()` / `SetSharedYardOfSelected()` / `SharedYardLabel()`；`NewYard()` 不再带共享标志。
  - 存档（`src/sl/station_yard_sl.cpp`）：`SYRD` 升 **v3**（`#SYRD_V3_MAGIC = 0xFFFFFF02`），**字段布局与 v2 完全相同**，只是每组第一个 word 由「共享标志(0/1)」改存 `shared_with`。读 v2 时迁移为「所有非共享场回落到**第一个**共享场、共享场自身不回落」——旧语义是「非共享场可回落到任意共享场」，v3 只能表达一个目标，**只有一个共享场的存档迁移后与原语义完全等价**，多共享场的存档会退化为只认第一个（已在注释中写明）。v1 三场迁移改为「A(1)/B(2) 的 shared_with = 3，共享场(3) 的 shared_with = 0」。读档收尾规范化：`shared_with == 自己的 ID || shared_with > 场数` ⇒ 置 0。
  - 语言：新增 `STR_R3R_YARD_SHARED_NONE`（`无` / `none`）与 `STR_R3R_YARD_SHARED_SEL`（`共享: {STRING}` / `Shared: {STRING}`），改写 `STR_R3R_YARD_SHARED_TOOLTIP`；`STR_R3R_YARD_NAME_N_SHARED`（`[k 场·共享]`）语义变为「该场是别人的回落目标」，仍由 `R3RIsYardShared()` 驱动。
  - **触发全量重编**：`station_base.h` 改字段 ⇒ 按 KI-15 必须删全部 `*.obj` 全量重编；同时改了 `lang/*.txt`（`LANGUAGE_PACK_VERSION` 变化）⇒ 必须让 `strings.cpp` 一并重编（全量重编天然满足）。
  - **待复测（需用户交互）**：①订单窗口能选中目的地场，且列车只在该场平台停靠；②「1 场、2 场 → 都设回落 A 场」后，1 场停满时列车改停 A 场；3/4 场互相不受影响；③把回落目标改回「无」后回落立即消失；④v3 存档往返（建场 → 存档 → 读档）后回落关系逐字节一致；⑤**v2 旧档**（第 70 轮建的场）读入后回落关系按迁移规则还原；⑥无场的车站行为与改动前一致。

- **第 71 轮构建证据**：因改动 `src/station_base.h` 与 `src/lang/*.txt`，按 KI-15 / 记忆 52814982 执行**全量重编**（删全部 `*.obj`，`_tmp_full_build.cmd`，`-j2`）。结果：`[707/707]` 全部完成，`build\R3R_fullbuild.done` = `EXIT_CODE=0`；`build\openttd.exe` @ 2026-09-19 23:43:05（50 782 208 B）；`build\generated\table\strings.h` 已含 `STR_R3R_YARD_SHARED_NONE = 0xC63` / `STR_R3R_YARD_SHARED_SEL = 0xC64`（确认 `strings.cpp` 随新语言包重编，不会出现 KI-16 的版本失配）。

## 附：第 72 轮（2026-09-20）玩家演示现场报告（**本轮只登记，不改代码**）

来源：用户在给朋友演示 R3R 版本（`r3r-stable-2026-09-18-m (2)`，Release 0.73.1，Build date Sep 19 2026 06:03，`R3R_PROBES_DEFAULT=0`）时集中踩到的问题，共 12 条。除 KI-137 已在第 71 轮修复外，其余全部为**未修**，锚点/栈帧均取自本轮实际读取的代码或崩溃日志。

- **KI-126（第 72 轮，2026-09-20）：R3R 的「逻辑反转」会改变 NewGRF 的 `position_in_articulated_parts`（articulated 组内位置），导致铰接式列车图像紊乱。状态：已修（**第 72 轮，仅改 `src/train_cmd.cpp` 一个 .cpp**；视觉复测待在 `build\` 里做）。严重度：中（图像乱，不影响运行）。**
  - 现象：对含铰接组（artic bits）的列车做逻辑翻（`R3RFlipChainBySegments`）后，组内各节取到的「articulated 组内位置」值变化，端头/中间节选图错乱。
  - 锚点：`src/newgrf_engine.cpp` 中「position in articulated parts」的取数（沿 `Previous()`/`IsArticGroupMember()` 数前置节数）+ 其 GRF 缓存；`src/train_cmd.cpp` `R3RFlipChainBySegments()` 的「逐段内部 artic 块倒序重链（`SetNext` 双向维护）」步骤（见记忆 41007633）。
  - 疑似根因：逻辑翻**真的改写了物理链序**（artic 块整体倒序），所以「沿链数出来」的位置值必然变化；同时重链后没有失效对应的 NewGRF 位置缓存（`NCVV_POSITION_*` 类），缓存值与新链序不一致，问题被放大成「图像紊乱」。
  - 待办方向：逻辑翻后对受影响车辆 `InvalidateNewGRFCacheOfChain()`；或让「组内位置」在 R3R 逻辑翻时保持业务语义（与记忆 96042581 的 position-in-segment 改造同源，需统一口径）。
  - **修复（第 72 轮，仅改 `src/train_cmd.cpp`，未碰任何 `src/*.h` ⇒ 增量构建合法）**：按待办方向的**第一条**做（缓存失效），落点选在 `R3RReverseChainDirections()` —— 它是 R3R 全部「逻辑方向改写」的唯一出口（`R3RFlipChainBySegments` 第 1 步 / `R3RUndoLogicalFlip` 回滚 / `Couple` 里挂车拼缝反向那处直接调用），在它的循环之后补两次失效：
    ```cpp
    chain->InvalidateNewGRFCacheOfChain();
    chain->InvalidateImageCacheOfChain();
    ```
    并在 `R3RFlipChainBySegments` 第 4b 步 `R3RReassignArticGroupRoles(new_head)` 之后**再补一次** `new_head->InvalidateNewGRFCacheOfChain()`。
  - **为什么是这两处**：①第 1 步结束时会翻转 `direction` + `Flipped`，而 `Flipped` 决定 `Train::GetImage()` 里那次「反向补偿」、`direction` 是 GRF 变量 `0x48`（reversed）与一切按方向取图的回调的输入；②第 4b 步会改组角色位，而角色位正是 `0x4D`（`newgrf_engine.cpp` 的 `position in articulated vehicle`：沿 `IsArticGroupMember()` 的 `Previous()` 数前置节数、沿 `HasArticulatedPart()` 的 `Next()` 数后续节数）的**唯一**取数依据 —— 第 1 步的失效发生在这步之前，所以在角色迁移后再补一次，保证「新链序 + 新角色位」与缓存一致。
  - **本轮新查到的关键事实（决定这个修法不是空操作）**：`Train::ConsistChanged()` 确实会失效 GRF 变量缓存（`train_cmd.cpp:348-349`、`355-356`、`447-448` 的 `InvalidateNewGRFCache()`），**但它不碰图像缓存** —— 全 `train_cmd.cpp` 里 `InvalidateImageCache*` 只在原生换端路径 `UpdateStatusAfterSwap()` 里出现过，R3R 的三条逻辑翻转路径一次都没有调用。`InvalidateImageCache()` 清的是 `cur_image_valid_dir` 与 `vcache.cached_veh_flags` 里的 `VCF_IMAGE_CURVATURE` / `VCF_REDRAW_ON_*`，这些正是「图像」侧的缓存，所以缺口真实存在（原先「`ConsistChanged` 会兜住」的假设只对 GRF 取数一侧成立）。
  - **没走第二条待办方向的原因**：把 `0x4D` 改成「段内位置」会改变真 artic 组的语义 —— `0x4D` 的遍历本来就被「是否属于同一 artic 组」限住，而 R3R 逻辑翻时每个 artic 块**整体保持连续、块内顺序不动**（`R3RFlipChainBySegments` 第 3 步是 `blocks.rbegin()` 倒序、块内 `for (Train *q : *it)` 维持原序），故真 artic 组的 `0x4D` 值本身不需要改；真正需要变的是 de-articulated 组，而那部分已由第 4b 步的角色迁移负责。若将来发现某个 GRF 依赖「跨段」的 `0x4D`，再按记忆 96042581 的口径统一改造。
  - **验证状态**：**本轮未编译、未实测**（用户要求本轮只改代码、不做构建）。待复测：加载 chinaset 等对铰接组逐节选图的 GRF，对含铰接组的列车做一次逻辑翻（车库/站台挂车的防折叠路径最容易复现），确认端头节仍取端头图、中间节仍取中间图；若有回归，请给出复现场景（车型 GRF + 是否 de-articulated）。

- **KI-127（第 72 轮，2026-09-20）：手动掉头会强制物理换端，**无视** `allow trains to flip when reversing = None`，并伴随整列图像反转。状态：已修（**第 72 轮，仅改 `src/train_cmd.cpp` 一个 .cpp**）。严重度：中（与设置项承诺不符 + 衍生崩溃 KI-130）。
  - 现象：设置「允许反向时掉头」= None 时手动掉头，列车仍被物理反转，图像整列看起来「反了」。
  - 锚点：`src/train_cmd.cpp:3437-3461`（`ReverseTrainDirection`）。R3R 为「订单/按钮调向必须真正换端」引入 `force_end_swap`（`VehicleRailFlag::ForceFlipReverse`，由 `train_cmd.cpp:8460`、`8522` 等设置，见记忆 60885272），命中后**直接**清 `DrivingBackwards` → `ReverseTrainSwapVehicles(consist)`，该分支排在 `... || _settings_game.difficulty.train_flip_reverse_allowed == TrainFlipReversingAllowed::None || ...`（`:3462`）之前，因此「None」这条「只倒车不掉头」的语义被 R3R 完全绕过。
  - 附带风险：`force_end_swap` 分支里对每节调 `UpdateStatusAfterSwap(u, false)`（`:3450`）会走 `VehicleEnterTile`，在 depot tile 上即引爆 KI-130。
  - 待办方向：None 时应保留「倒车不掉头」的原生语义（或至少在 UI/设置说明里显式声明 R3R 覆盖了该设置）；`force_end_swap` 分支需加 depot/隧桥 tile 豁免。
  - **修复（第 72 轮，仅改 `src/train_cmd.cpp`，未碰任何 `src/*.h` ⇒ 增量构建合法）**：按待办方向的**第一条**做 —— 把 `ReverseTrainDirection()` 里的 `const bool force_end_swap` 改成可重新赋值的 `bool`，并在调试打印之后、真正分派「换端 / 倒车」之前插一段降级：
    ```cpp
    if (force_end_swap && _settings_game.difficulty.train_flip_reverse_allowed == TrainFlipReversingAllowed::None) {
        force_end_swap = false;
        ... fprintf(dbg, "REVERSEDIR-NOFORCE veh=%d reason=flip_reverse_allowed_None\n", ...);
    }
    ```
    降级后自然落到下面原有的 `else if (... train_flip_reverse_allowed == None ...)` 分支 —— 正是原生的「只倒车不掉头」，与设置承诺一致；「调向」的语义仍然成立（列车确实朝反方向走了），只是按 None 的规定用倒车实现。日志标签 `REVERSEDIR-NOFORCE` 只在真的发生降级时写一行，不会刷屏。
  - **覆盖面**：`ForceFlipReverse` 的四个设置点（车辆视图按钮、waypoint 到达 `:8462-8473`、station 到达 `:8528-8542`、`GOTO_COUPLE` 相关 `:10947-10949`）全部经 `ReverseTrainDirection` 单一入口，故这一处降级对四条路径同时生效；`Reversing` 延迟路径（速度非 0 时先置 `Reversing`、停车后再调 `ReverseTrainDirection`）也吃同一处判断。
  - **待办方向的第二句已由 KI-130 覆盖**：`force_end_swap` 分支在 depot tile 上的崩溃已在 KI-130 里用「`UpdateStatusAfterSwap()` 对 `IsRailDepotTile()` 跳过 `VehicleEnterTile()`」堵住；隧桥（wormhole）本来就由该函数里的 `TRACK_BIT_WORMHOLE` 判断跳过。故本条不再重复改动。
  - **不做的事**：没有删掉「按钮/订单调向必须换端」这条设计（记忆 60885272 的用户拍板继续有效）—— 只有玩家显式选择 `None` 时才让位给设置项；`All` / `EndOfLineOnly` 下行为与改动前完全一致。
  - **验证状态**：**本轮未编译、未实测**。待复测：①把「允许反向时掉头」设为 None，手动点车辆视图的调向按钮 ⇒ 列车应倒车驶离、不换端，日志出现 `REVERSEDIR-NOFORCE`；②设回 All ⇒ 按钮仍物理换端（`REVERSEDIR ... force_swap=1`）；③None 下「到达站点/路径点调向」的订单应变成倒车出站；④None 下不再出现 KI-130 的 `map_func.h:433`（该崩溃要求走换端分支）。

- **KI-128（第 72 轮，2026-09-20）：「神秘自动添加命令」——耦合完成后列车突然多/换了一条调度命令，并以极慢速度驶出站台（与正常耦合后立刻全速加速完全不同）。状态：部分防护（**第 73 轮**：慢速驶出的根因已修；「神秘命令」经核实就是 route A 借用对方排程的既有设计，已加 `ORD-AFTER-COUPLE` 逐条快照探针，待复测确认）。严重度：中。**
  - 玩家怀疑：为列车设置了「去站台前端」的命令，而列车 `moving_front` 距站台前端太远，于是全程按「需要重新对齐站台」的低速逻辑爬行。
  - 锚点：`src/train_cmd.cpp` `Couple()` 的排程交接块（`orders` 归属交换 + 清 `current_order` + 跳过 `WAIT_COUPLE`，见记忆 18491399 的 `4925-4944`；本树行号已偏移）；`R3RSyncDrivingOrders()`；以及 `InsertOrder(...)` 的各调用点。
  - 说明：对玩家而言「自动添加命令」最可能就是**交接时把对方排程整份接过来了**（`v->orders = u->orders` 一类），命令是「凭空出现」的；也可能是耦合后 `WAIT_COUPLE` 被跳过导致当前索引落到某条 GOTO 上。
  - 待办方向：加一条耦合前/后的订单表快照探针（`ORD-AFTER-COUPLE n=… cur=…`），先确认「多出来的是哪条命令」，再查低速来源（`NotYetInPlatform`/`BeyondPlatformEnd` 与站台对齐逻辑，`vehicle_base.h:3745-3755` 附近）。
  - **修复（第 73 轮，仅改 `src/train_cmd.cpp` 一个 `.cpp`，未碰 `src/*.h` ⇒ 按 KI-15 增量构建合法）**：
    - **「低速驶出站台」根因（已修）**：耦合是在**装卸途中**改写链头身份的，而站台状态绑在**旧列车**上：(a) `CargoPayment` 由 `PrepareUnload` 绑在旧链头，合并后只有**新链头**的 `HandleLoading` 才可能释放它 ⇒ 泄漏（`Station::loading_vehicles` 登记与装卸指示条同理）；(b) 装载进度文本特效 `fill_percent_te_id` 停在站台（即 KI-133 的「幽灵进度条」）；(c) **站台对齐标志**（`NotYetInPlatform`/`BeyondPlatformEnd`/`AdvanceInPlatform`）描述旧编组，其中 `AdvanceInPlatform` 一旦残留，下一次停站会**立刻**重新进入 `OT_LOADING_ADVANCE`（`Vehicle::HandleLoading` → `AdvanceLoadingInStation`），而该状态把速度压在 `through_load_speed_limit` 上 ⇒ 就是「耦合后极慢爬出站台」。
      修法：在耦合提交点（`Couple()` 里 `R3RMergePriorities` 之前、链头身份改写之前）调用 KI-69 已有的 `R3RSettleLoadingBeforeChainEdit(v, "couple")` 结清装卸状态，再**显式清掉整条合并链的 `VehicleRailFlag::AdvanceInPlatform`**（`Vehicle::LeaveStation()` 只清 `BeyondPlatformEnd`/`NotYetInPlatform`，`AdvanceInPlatform` 必须显式重置；任一节都可能带着它，故按 `Next()` 遍历整链）。解挂侧早已接入同一 helper，本轮把耦合侧补齐。
    - **「神秘自动添加命令」（部分防护）**：该观感来自 **route A 的排程借用**——`Couple()` 把命令所有者（passive 优先级最低的段头）的整份排程搬到合并链头上（`v->orders = couple_owner->orders`，继承其 `cur_real_order_index`，并在下一条是 `WAIT_COUPLE` 时跳过），对玩家而言命令像是「凭空多出来」。属既有设计，**本轮不改语义**，只加探针：提交后写一行 `ORD-AFTER-COUPLE head=… n=… real=… impl=… tt=… co=… dest=… borrowed=… owner=… u_has_orders=…`，随后**逐条**打印 `ORD idx=… type=… dest=…`。
  - **验证状态**：**本轮未编译、未实测**。复测：①在站台上（装卸未完成时）耦合 ⇒ 应立刻全速驶离，不再爬行；②把 `ORD-AFTER-COUPLE` + 逐条 `ORD idx=` 与耦合前订单表对照贴回；③顺带确认 `SL-STNN-PURGE`（KI-95）无新条目。
  - **顺带覆盖 KI-133**：结清装卸会一并 `HideFillingPercent`，「幽灵进度条」主症状应随之消失（若仍残留，说明还有「非装卸途中的链头变更」路径）。
  - **修复（第 73 轮，仅改 `src/train_cmd.cpp` 一个 `.cpp`，未碰 `src/*.h` ⇒ 按 KI-15 增量构建合法）**：
    - **「低速驶出站台」根因（已修）**：耦合是在**装卸途中**改写链头身份的，而站台状态是绑在**旧列车**上的：
      (a) `CargoPayment` 由 `PrepareUnload` 绑在旧链头上，合并之后只有**新链头**的 `HandleLoading` 才可能释放它 ⇒ 会泄漏（`Station::loading_vehicles` 登记与装卸指示条同理）；
      (b) 装载进度的文本特效 `fill_percent_te_id` 停在站台（就是 KI-133 的「幽灵进度条」）；
      (c) **站台对齐标志**（`NotYetInPlatform` / `BeyondPlatformEnd` / `AdvanceInPlatform`）描述的是旧编组，其中 `AdvanceInPlatform` 一旦残留，下一次停站会**立刻**重新进入 `OT_LOADING_ADVANCE`（`Vehicle::HandleLoading` → `AdvanceLoadingInStation`），而该状态把速度压在 `through_load_speed_limit` 上 ⇒ 就是「耦合后列车以极慢速度爬出站台」。
      修法：在耦合提交点（`Couple()` 里 `R3RMergePriorities` 之前、链头身份被改写之前）调用 KI-69 已有的 `R3RSettleLoadingBeforeChainEdit(v, "couple")` 结清装卸状态，然后**显式清掉整条合并链的 `VehicleRailFlag::AdvanceInPlatform`**（`Vehicle::LeaveStation()` 只清 `BeyondPlatformEnd` / `NotYetInPlatform`，`AdvanceInPlatform` 必须显式重置；任一节都可能带着它，故按 `Next()` 遍历整链）。解挂路径早已接入同一个 helper，本轮把耦合侧补齐。
    - **「神秘自动添加命令」（部分防护，仍需实测数据）**：命令「凭空出现」这一观感来自 **route A 的排程借用**——`Couple()` 把命令所有者（passive 优先级最低的段头）的整份排程搬到合并链头上（`v->orders = couple_owner->orders`，同时继承其 `cur_real_order_index`，并在下一条是 `WAIT_COUPLE` 时跳过），所以对玩家而言车队计划里的命令「突然多出来了」。这是既有设计而非新缺陷，因此**本轮不改语义**，只加快照探针：耦合提交后写一行 `ORD-AFTER-COUPLE head=… n=… real=… impl=… tt=… co=… dest=… borrowed=… owner=… u_has_orders=…`，随后**逐条**打印 `ORD idx=… type=… dest=…`。
  - **验证状态**：**本轮未编译、未实测**（用户要求只改代码）。复测：①在站台上（装卸未完成时）耦合车底 ⇒ 耦合后应立刻全速驶离，不再出现 `through_load_speed_limit` 的爬行；②把 `ORD-AFTER-COUPLE` + 逐条 `ORD idx=` 与耦合前的订单表对照，确认「多出来的命令」确实等于被借用方的排程（若是则本条按设计收口，若不等请把两段日志贴回）；③顺带确认 `SL-STNN-PURGE`（KI-95）不再出现新条目。
  - **顺带覆盖**：本条与 KI-133（幽灵进度条）**落点相同**——结清装卸会一并 `HideFillingPercent`，故 KI-133 的主症状应随之消失（若 KI-133 仍残留，说明还有「非装卸途中的链头变更」路径）。

- **KI-129（第 72 轮，2026-09-20）：无法正常复制「纯由假引擎组成的链」（复制的车组出现异常）。状态：已修（**第 73 轮**）。严重度：中。**
  - 现象：对「整条链都是假引擎（无真机车）」的车组执行复制，得到的结果异常。
  - 锚点：`src/vehicle_cmd.cpp` `CmdCloneVehicle()`（复制/克隆路径）+ R3R 的假引擎身份规则（链头 `GVSF_FRONT/ENGINE`、段尾假引擎，见记忆 51063123）。
  - 疑似根因：克隆按「每节都当独立车辆」重建，复制出来的链头**没有重建假引擎身份**（或反之把假引擎当普通车），得到一条「没有头」的链。另外「没有任何乘务/无链头假引擎」的组合本来就该被拒绝（与 KI-136 同源）。
  - 待办方向：`CmdCloneVehicle` 里对 R3R 段/假引擎链显式校验并拒绝，或按段重建身份；与 KI-136 一并处理。
  - **修复（第 73 轮，仅改 `src/vehicle_cmd.cpp` 一个 `.cpp` ⇒ 增量构建合法）**：`CmdCloneVehicle()` 的重建循环有两处与 R3R 假引擎不兼容，均已改掉：
    - **①链头判定用错谓词（真因）**：原代码用 `!v->IsFrontEngine()` 判断「这是普通车厢，挂到正在拼的车后面」，否则走 `else` 把自己记为链头 `w_front`。但 R3R 的**段头是「假引擎」**——一节带 `GVSF_ENGINE|GVSF_FRONT` 的车厢位于链**中间**，`IsFrontEngine()` 为真 ⇒ 它被当成链头、`w_front` 被它覆盖，于是 `AddVehicleToGroup` 指到错误的车、refit 循环也从错误的车开始（「机车 + 车底」克隆结果异常）。改为按「**它是不是源链的链头**」判断：`v != v_front`（源链头恒为 `v_front`，与原生语义一致且与假引擎无关）。
    - **②克隆出的车没重建假引擎身份**：新建的车恒是普通散车厢，旧补丁只调了 `SetFrontEngine()`——那是「**散车厢 + front 位**」，既不是引擎、也不被 `R3RIsCarOnlyFormation()` 认作编组头。改为按源车镜像重建：`SetEngine()` + `ClearWagon()` + `ClearFreeWagon()`，并**按源车决定是否 `SetFrontEngine()`**（段尾假引擎故意不带 front 位，不能无条件设置）。重建必须**在 `MoveRailVehicle` 之后**：先给引擎位会让 `MoveRailVehicle` 把它当机车而拒绝挂接。
    - **③只允许链头调 `ConsistChanged()`**：链中段的假引擎不能就地调 `ConsistChanged()`，改为置 `r3r_identity_rebuilt = true`，循环结束后对 `w_front` 统一调一次 `ConsistChanged(CCF_ARRANGE)`。
  - **验证状态**：**本轮未编译、未实测**。复测：①复制「纯假引擎（无真机车）」车组 ⇒ 复制品仍是无真机车的编组（链头带假引擎身份、可作为等待编组被连挂），不再出现「没有头」的链；②复制「机车 + 车底」⇒ 链头正确、车组归属正确、refit 从链头开始；③复制「多段链」⇒ 各段前/后假引擎身份与原车一致。
  - **仍未做**：KI-136（「不带真引擎的链」拒绝自主移动 / 拒绝 couple-uncouple）本轮未动，与本条同源但属独立条目。

- **KI-130（第 72 轮，2026-09-20）：手动掉头（反向）时在**车库 tile** 上崩溃：`map_func.h:433 assert IsValidDiagDirection(dir)`。状态：已修（**第 72 轮**）。严重度：高。**
  - 崩溃日志：`C:\Users\冯洁敏\Documents\OpenTTD\crash-20260919T174619Z.log`（朋友演示现场；`r3r-stable-2026-09-18-m (2)` / 0.73.1 / Build date Sep 19 2026 06:03 / `R3R_PROBES_DEFAULT=0`）。
  - 上下文：`CallVehicleTicks: veh: 29: (Train 6, c:0, st:FE, vs:D, vf:FTpBa, vcf:zd, gvf:, tf:RrJ, trk: 0x10, tile: BAA (42 x 23), type: 10 (Railway))`。
  - 命令日志最后一条：`1920-03-26 … cmd: 031 CmdReverseTrainDirection | 29, false, true`（即玩家手动给车 29 掉头，同 tick 就崩）。
  - 栈（日志行号对应 09-18 构建，工作树可能已偏移）：
    `map_func.h:433 IsValidDiagDirection(dir)` ← `signal.cpp:1110 UpdateSignalsInBuffer` ← `vehicle.cpp:2721 VehicleEnterDepot` ← `rail_cmd.cpp:4892 VehicleEnterTile_Rail` ← `vehicle.cpp:3021 VehicleEnterTile` ← `train_cmd.cpp:2953 UpdateStatusAfterSwap` ← `train_cmd.cpp:3459 ReverseTrainDirection` ← `train_cmd.cpp:10518 TrainLocoHandler` ← `train_cmd.cpp:11167 Train::Tick` ← `vehicle.cpp:1695 CallVehicleTicks`。
  - 疑似根因：掉头路径（KI-127 的 `force_end_swap` 分支，`:3437-3461`，尤其 `:3450` 的 `UpdateStatusAfterSwap(u, false)` 与 `ReverseTrainSwapVehicles` 之后的 `AdvanceWagonsAfterSwap`）在**车辆正处于车库 tile** 时，用「换端后」的 direction/`moving_front` 去进入 tile → `VehicleEnterDepot` → `UpdateSignalsInBuffer(GetTunnelBridgeDirection/DiagDir…)` 拿到了非法 DiagDirection（很可能是 `DiagDirection::invalid` 或由 depot tile 方向与非轨道方向组合出的越界值）。
  - 待办方向：①`ReverseTrainDirection` 的 `force_end_swap` 分支对「车辆在 depot tile / 隧桥 wormhole」时跳过 `UpdateStatusAfterSwap`，或改用非「进 tile」的刷新方式；②`UpdateSignalsInBuffer` 入口加 `IsValidDiagDirection` 前置断言/防御返回（同时保留断言以便定位）；③复现：车库里手动倒车（对照 KI-127）。
  - **修复（第 72 轮，仅改 `src/train_cmd.cpp` 一个 .cpp，未碰任何 `src/*.h` ⇒ 按 KI-15 增量构建合法）**：落点选在**所有 swap 的统一出口** `UpdateStatusAfterSwap()`（`src/train_cmd.cpp:2931-2945`），而不是只堵 `force_end_swap` 一处——`ReverseTrainSwapVehicles()` 的每条路径最后都会经这里调 `VehicleEnterTile()`，堵一处即全覆盖。
    ```cpp
    /* Call the proper EnterTile function unless we are in a wormhole. */
    if (!(v->track & TRACK_BIT_WORMHOLE)) {
        /* R3R (KI-130): never re-run the tile entry logic for a vehicle standing
         * on a rail depot tile ... (见源码注释全文) */
        if (!IsRailDepotTile(v->tile)) VehicleEnterTile(v, v->tile, v->x_pos, v->y_pos);
    } else {
    ```
    为什么这样修：`VehicleEnterTile_Rail()` 在 depot tile 上的「Entering depot」分支（`rail_cmd.cpp:4848+`）会在 `v->GetMovingNext() == nullptr` 时对 `consist`（= `v->First()`）调 `VehicleEnterDepot()`；而换端刚清掉 `DrivingBackwards`，此时车辆「面朝车库」且正站在车库停车坐标上 ⇒ 被读成「正在进入车库」，于是对**仍停在普通轨道上的链头**执行车库簿记，用 `DiagDirection::Invalid` 去刷信号 → `TileOffsByDiagDir()` 断言（就是崩溃栈里的 `map_func.h:433`）。车库登记本来就由 `TrainController` 真正入库时的那次 `VehicleEnterDepot()` 完成，换端后不需要在这里重做。
    `IsRailDepotTile()` 在本文件已有使用（`train_cmd.cpp:6802`），无需新增 include。
    **未修部分**：KI-127（`force_end_swap` 绕过 `train_flip_reverse_allowed = None` 语义）本轮未动，仍为「未修」。`UpdateSignalsInBuffer` 入口的防御（待办②）也没有加——本轮按「堵唯一入口 + 保留断言以便继续定位」取舍，若后续再出现同类崩溃再加。
    **验证状态**：**本轮未编译、未实测**（用户要求本轮只改代码、不做构建）。复现步骤仍是「车库 tile 上手动倒车（KI-127 场景）」，期望不再落到 `map_func.h:433` 断言。

- **KI-131（第 72 轮，2026-09-20）：某次测试中出现「列车各种属性翻三倍」，怀疑与探针开关状态相关。状态：部分防护（**第 73 轮**：探针副作用嫌疑经代码审查**排除**；已加 `CHAIN-ATTRS` 属性快照探针，待复测定位「×3」来源）。严重度：中。**
  - 现象：列车属性（未记清是哪几项）整体变成 3 倍。
  - 现场线索：演示用的 exe `Defines` 里明确写着 `R3R_PROBES_DEFAULT=0`，说明探针在**默认关闭**的构建下也会被显式打开（见 `src/r3r_perf.h` 的 `R3RDbgWrite` / `R3RDbgEdge` 门控）。
  - 疑似根因（两条互相独立，需分别排除）：①**探针有副作用**——关掉探针后某个 `R3RDbgWrite(...)` 实参表达式（含缓存失效/`SetBit`/`Invalidate*`/自增）不再求值，于是行为随探针开关变化；②链编辑（耦合/段升级）把「节」重复计入属性统计（容量/功率/重量分别乘 3，正好等于三节铰接组或三段链）。
  - 待办方向：先做「同一存档、同一操作，探针开/关各跑一遍」的 A/B 对照；同时 grep 全部探针调用点的实参是否含副作用表达式（长期规约：探针实参**不得**有副作用）。
  - **第 73 轮审查结论（「探针副作用」嫌疑 → 排除）**：`src/r3r_perf.h` 里 `R3RDbgWrite` 在 `R3R_PROBES=1`（`build\`）下是 **inline 函数**（实参**恒被求值**，只有函数体内的 `if (!R3RDbgOn()) return;` 决定是否落盘），在 `R3R_PROBES=0`（`build-release\`）下才是 `((void)0)` 宏（实参连同副作用一起消失）。
    - ⇒ **同一构建内开关 `R3R_DBG` 不会改变实参求值**；只有「探针构建 vs 发布构建」的对比才可能差异，且前提是某个实参含副作用。
    - 逐点核查：全树 `R3RDbgWrite(` / `R3RDbgEdge(` 调用点（`train_cmd.cpp` 34 处 + 其它 7 文件）实参**全部是纯读取**（`index.base()` / `tile.base()` / `x_pos` / `GetNumOrders()` / `R3RDbgTagHash()` 等），**没有任何 `++` / `--` / `SetBit` / `Invalidate*` / 赋值**。
    - 唯一的控制流用法 `R3RCheckChainFold()` 里的 `if (!R3RDbgEdge(...)) return worst_gap;` 是**提前返回同值**（`worst_gap` 在闸门之前已算完；闸门之后只算探针载荷，函数末尾 `return worst_gap;`）⇒ 行为等价，仅少写日志。
    - **结论：本条「探针开关影响行为」不成立**，×3 更可能是「属性被按段重复累加」，故改用下面的探针直接量属性。
  - **新增探针（第 73 轮，仅改 `src/train_cmd.cpp`）**：耦合提交点写一行 `CHAIN-ATTRS head=… n=… FE=… SEG=… pow=… wt=… len=… spd=…`，随后**每段一行** `SEG idx=… pow=… wt=… len=… spd=…`。
    - **判读方法（本条关键）**：`Train::ConsistChanged()` 把**整条链**的汇总值写进**每一节**车的 `gcache`，所以「把所有段头的 `gcache` 相加」这种做法（UI 面板 / GRF / 任何按段遍历的统计）对三段链会**正好 3 倍**。若各 `SEG` 的 `pow/wt/len/spd` 与链头相同（都等于整链总量）⇒ 证实「乘 3 来自按段累加」，据此去修那个累加点；若各段显示的是**自己那一段**的量 ⇒ 问题在 `ConsistChanged` 的调用时机（曾在**段头**上被调用）。
  - **验证状态**：**本轮未编译、未实测**。复测：复现「属性翻三倍」时把 `CHAIN-ATTRS` 的 1 行头 + n 行 `SEG` 贴回即可定位。

- **KI-132（第 72 轮，2026-09-20）：跨公司耦合后「调度命令的归属公司」与「列车显示所有者」不一致：命令属于粉色公司的段，链头所属段是红色公司的，而绿色显示的列车所有者也是红色公司——**红色公司却能修改粉色公司段的调度命令**（权限越界）。状态：部分防护（**第 73 轮**：越权路径已封死；「排程归属该判给谁」的口径仍待用户拍板，现场探针已就位）。严重度：中（多人/多公司场景的越权与数据归属混乱）。**
  - 锚点：`src/train_cmd.cpp` `Couple()` 的排程交接块（`orders` 整体移交，未校验 `orders`/`orders_backup` 原属公司与新链头公司是否一致）；`Command<>` 的权限检查走「车辆所有者」（`CheckOwnership(v->owner)` 类），而排程对象自身带有 `owner`。
  - 疑似根因：耦合只搬 `OrderList` 指针、不改其 `owner`，于是「命令属于 A 公司、车属于 B 公司」；UI/命令权限按车判 → B 能改 A 的命令（或反之，取决于谁在头）。
  - 待办方向：跨公司耦合时明确策略（拒绝？还是把排程归属重写为新链头公司？），并在 `CmdModifyOrder`/订单窗口侧按 `orders->GetOwner()` 二次校验；需与用户确认期望口径。
  - **更正一个前提**：`OrderList`（`src/order_base.h`）**没有 `owner` 字段**（本轮已核对类定义：只有 `type` / `first_shared` / `next_shared` / `num_vehicles` 之类），所以「按 `orders->GetOwner()` 二次校验」**不可实现**。排程的「归属公司」只能**通过持有该 `OrderList` 指针的车辆**推导。
  - **修复（第 73 轮，改 `src/order_cmd.cpp` + `src/train_cmd.cpp`，两个 `.cpp` ⇒ 增量构建合法）**：
    - 新增文件内静态辅助 `R3ROrdersSharedWithOtherCompany(const Vehicle *v)`：取 `v->orders`，沿**物理链**（`v->First()` → `Next()`）查找**持有同一个 `OrderList` 指针、且 `owner` 与 `v` 不同**的车；命中即说明这条排程是跨公司共享的。
    - 在**全部 10 处**订单编辑命令的 `CheckOwnership(v->owner)` 之后统一插入 `if (R3ROrdersSharedWithOtherCompany(v)) return CMD_ERROR;`（一次批量落点，覆盖 `CmdDuplicateOrder` / `CmdSetRouteOverlayColour` / `CmdInsertOrder`（含 `CmdInsertOrderIntl` 入口）/ `CmdDeleteOrder` / `CmdSkipToOrder` / `CmdMoveOrder` / `CmdReverseOrderList` / `CmdModifyOrder` / `CmdOrderRefit` / `CmdBulkOrder`）。这样**红色公司即便拥有链头也无法修改粉色公司的那份排程**，越权路径封死；且**同公司场景完全不受影响**（同公司时不可能存在「同一 `OrderList` + 不同 owner」）。
    - 另加 `COUPLE-XCOMPANY` 探针（`Couple()` 排程交接块之前，只读、每次跨公司耦合写一行）：`COUPLE-XCOMPANY head=… head_own=… owner=… owner_own=… owner_ords=… head_ords=… borrows=…`，用于把玩家报告的现场与真实耦合对上。
  - **本轮故意不改的（需用户拍板口径）**：`Couple()` 的 route A 排程借用本身**保持原样**（跨公司耦合时链头仍会借用并执行对方排程）。可选口径：①**允许借用**（现状）+ 本轮编辑拦截（推荐：运输不中断、越权被堵）；②**拒绝跨公司耦合**（`Couple()` 直接 `CMD_ERROR`，最干净但会打断「用自家机车拖别人车底」的玩法，需改 `Couple()` 的返回值路径）；③借用但只靠本轮的编辑拦截（指挥权仍归原公司，但列车按对方计划跑，语义最怪）。请择一后再动 `Couple()`。
  - **验证状态**：**本轮未编译、未实测**。复测：①（多公司存档）复现跨公司耦合 ⇒ `R3R_debug.log` 出现 `COUPLE-XCOMPANY` 行，请贴回以确认 owner 组合；②用链头所属公司打开订单窗口改命令 ⇒ 应报「不能执行这个命令」（越权被拦）；③**回归重点**：同公司耦合与普通列车改订单**必须不受影响**；④共享排程（Ctrl+共享）正常编辑不受影响。

- **KI-133（第 72 轮，2026-09-20）：「幽灵耦合进度条」：在站台耦合时，车站头顶会挂一个进度条，列车开走后也不消失。状态：已修（**第 73 轮**，仅改 `src/train_cmd.cpp` 一个 .cpp；按待办方向③「挂到链头变更的统一钩子」落地）。严重度：低（纯显示残留）。**
  - 现象：进度条挂**在车站头顶**（而不是挂在车上），列车离开后仍在。
  - 锚点（这是本题的关键证据）：`src/economy.cpp:2526-2535` 的装卸指示条 = 文本特效 `front->fill_percent_te_id`，**创建位置取 `moving_front->x_pos/y_pos/z_pos+20` 且只在创建那一次取一次**；而 `HideFillingPercent(&...fill_percent_te_id)` 只在这些地方被调用：`Vehicle::PreDestructor`（`vehicle.cpp:1168`）、`Vehicle::LeaveStation`（`vehicle.cpp:3680`）、装载推进（`vehicle.cpp:3751`）、坠毁（`train_cmd.cpp:8735`）、以及掉头调用点（`train_cmd.cpp:3699/8456/8528`）。**全都没有覆盖「装卸途中 R3R 换链头/拆链/合并」这条路径。**
  - 疑似根因：`front`（=`First()`，即链头）在装卸途中被 R3R 改动（耦合让 `u` 成为新链头、拆链把链头切走等）后，**新链头的 `fill_percent_te_id` 是 INVALID_TE_ID**（于是原地又建一个新的），而**旧链头持有的那个 TE 没有任何人 Hide** → 旧 TE 永久停在创建时的坐标（也就是站台/车站附近）→ 「幽灵进度条」。设置项 `gui.loading_indicators: 2`（永远显示）会放大该现象。
  - 待办方向：①在 R3R 所有会改变「链头身份」的路径上，对被换下的旧链头调 `HideFillingPercent`（并与 KI-132 同一处理点）；②`Vehicle::PreDestructor`/`LeaveStation` 里改为遍历整链（或按 TE 归属）清理；③更稳妥：把 `fill_percent_te_id` 的清理挂到「链头变更」的统一钩子。
  - **修复（第 73 轮，`src/train_cmd.cpp`）**：按方向③落地。`R3RSettleLoadingBeforeChainEdit(chain, site)`（KI-69 建的统一「链头身份即将改变」钩子，已被 `couple` / `decouple-front` / `decouple-rear` / `relocate-front` 四个现场调用）在沿链遍历的**循环首行**新增无条件 `HideFillingPercent(&v->fill_percent_te_id);`。
    - 为什么要**无条件**：原逻辑是 `if (v->cargo_payment == nullptr) continue;`，只有 `LeaveStation()`（在 payment 非空分支里）才会顺带隐藏进度条。若旧链头的 payment 已经结清（装卸已结束但订单还没处理）或该车本就没有 payment，进度条特效就会漏掉——这正是「列车开走也不消失」的剩余路径。特效是纯贴图（创建位置只在建时取一次 `moving_front->x_pos/y_pos`），此刻链头身份正在变，锚点必然失效，所以整链无条件摘除是安全且正确的。
    - 覆盖范围：耦合提交点、解挂前半/后半、`R3RRelocateFrontIdentity()`（换端正迁）四处；仓库内段升降不在装卸态，无需处理。
    - 验证状态：**本轮未编译、未实测**（用户要求本轮只改代码、不做构建）。待复测：站台耦合（尤其 `gui.loading_indicators = 2` 永远显示）后把车开走，站台上不应再留着进度条。

- **KI-134（第 72 轮，2026-09-20）：「神秘自动添加命令」（KI-128）触发之后，机车与车底之间的间距会被拉开。状态：已修（**第 73 轮**，仅改 `src/train_cmd.cpp` 一个 .cpp；根因是「拼接只重链、不重排像素」的残留间隙）。严重度：中。**
  - 现象：自动命令出现后，机车与车底不再紧贴，出现可见间隙。
  - 疑似根因：列车开始按新接手的排程「重新对齐站台/前端」时，机车单独先动而车底尚未被拖动（或反之），R3R 的链几何没有在换端/换命令后重新贴合 → 间隙。
  - 待办方向：与 KI-128 一起用「耦合前后链几何快照」探针（每节 `x/y/tile/direction` + `gap`）确认是「先动后拖」还是「拼接点(标称 vs 实际)」问题。
  - **根因（第 73 轮定位，代码自证，无需复现）**：既不是「先动后拖」，也不是 KI-128 的站台状态——是**拼接点标称 vs 实际**问题，且**不会自愈**。
    - `ArrangeTrains()` 只重接 `Next()`/`Previous()` 链序，**不动任何车的像素坐标**（`TryTrainCouple` 里 09-14 的注释已写明：「A pair that has just been joined logically therefore keeps the spacing it had before the splice」）。
    - 接受判据 `R3RSpliceFolded` 是「拼接对实际中心距落在标称 `(len_a+len_b)/2 ± 8px` 之内」，**不是**「必须等于标称」。机车是靠 `GetCouplePosition` 的命中容差停下来的，所以停下来时拼接对天然差着几像素 ⇒ 合并链**永久保留**这个间隙。
    - 「会自愈」的旧假设是错的：`GetNewVehiclePos()`（`src/*.cpp`）对任何车都只前进 **1 像素**，而 `TrainController(v, nomove)` 的循环是把传入车及其后面每一节**各推 1 像素** ⇒ **整列刚性平移**，已存在的间隙既不会被拉近也不会被抹平。所以只要列车一按 KI-128 接手的排程动起来，「机车与车底不再紧贴」就一直看得见。（顺带说明 `CheckTrainsLengths()` 里那条 `abs(u.pos-w.pos) != u->CalcNextVehicleOffset()` 断言就是给这种残留间距准备的。）
  - **修复（第 73 轮，`src/train_cmd.cpp`）**：新增文件内静态 `R3RRespaceChainAfterEdit(Train *head, const char *tag)`，在 `Couple()` 里「接缝方向统一」之后、排程交接之前对 `v->First()` 调一次（`tag = "couple"`）。
    - 做法：沿 `GetMovingNext()` 走链，对每一对 `(a, b)` 比较 `dist = max(|Δx|,|Δy|)` 与标称 `a->CalcNextVehicleOffset()`；`dist > 标称` 时把 `b` 及其之后整段**往前推**（`TrainController(b, nullptr, false)`，一次 1 像素，最多 64 像素/对），直到贴合。
    - 安全性：①推之前先用 `GetNewVehiclePos(b)` 预判这一步是否真的在**减小**该对距离，不会的话立即停（artic 部件共用父车坐标、方向未统一的车都不会被推反）；②`dist <= 标称`（含 artic 的 0 距离、以及过近的对）一律跳过——`TrainController` 只能前进，不能后退；③传 `reverse = false`，不触发反向逻辑；④`b` 是链中车而非 moving front，`TrainController` 里所有 `IsMovingFront()` 守卫的路径（寻路预留、`TrainMovedChangeSignal`）都不会执行，只有信号释放、`UpdateDeltaXY/UpdatePosition/UpdateViewport` 这类通用更新。
    - 为什么不改判据：把 ±8px 收紧成「必须等于标称」会拒绝掉所有「机车差几像素停下」的正常挂车；正确做法是**接受后把链拉紧**，保持行为不变。
    - 新探针：`[R3R] RESPACE-AFTER-EDIT couple head=<idx> px=<n>`（仅当真的推动过才写一行）。
    - 验证状态：**本轮未编译、未实测**。待复测：耦合成功后（尤其折叠修正路径）立刻截图/看 `gap`，接缝应直接贴合；后续 `CPL` 探针里相邻节 `gap` 应等于标称；行驶中不应再出现可见间断。若 `RESPACE-AFTER-EDIT ... px=0` 或无该行，说明现场本就没有残留间隙，KI-134 需要另找根因（届时贴回 `CPL` 与 `FOLDCHK` 行）。

- **KI-135（第 72 轮，2026-09-20）：公路车辆相关崩溃：**购买汽车**与**点击汽车车库**都会崩，断言在 `gfx.cpp:2059`。状态：已修（**第 72 轮，崩溃已堵住；「为什么该矩形会退化成 0 尺寸」的根因仍待探针产出**）。严重度：高（公路车辆不可用）。**
  - 锚点：`src/gfx.cpp:2059` = `assert(width > 0);`（`FillDrawPixelInfo`，紧邻 `:2060 assert(height > 0);`）⇒ 有控件/绘制请求拿到了 **width ≤ 0**。
  - 疑似根因：两个入口都通向**车库窗口**，而 R3R 改过与之共用的车库控件树（`src/widgets/depot_widget.h` 删了 `WID_D_SET_AS_FRONT_WAGON`，见记忆 50765346；公路车库复用同一套 depot 控件/`NWidget` 矩阵），很可能是对公路车辆场景出现 0 宽的嵌套控件（或 `SetMinimalSize`/矩阵列被算成 0）。
  - 待办方向：拿到完整栈（该断言会打印 widget 树）；先在 `FillDrawPixelInfo` 的断言前加「窗口号 + 控件号 + 宽高」临时探针定位到具体控件，再查 R3R 对 `depot_widget.h`/`roadveh_gui.cpp` 的改动。
  - **修复（第 72 轮，5 个 .cpp，全部未碰 `src/*.h` ⇒ 增量构建合法）**：
    - 落点：`src/roadveh_gui.cpp` `DrawRoadVehImage()`、`src/train_gui.cpp` `DrawTrainImage()`（**只有这两个函数用 `FillDrawPixelInfo()` 做裁剪，因此也只有它们能踩到 `gfx.cpp:2059`**），`src/ship_gui.cpp` `DrawShipImage()`、`src/aircraft_gui.cpp` `DrawAircraftImage()` 加同款保护（后两者不裁剪，只是让「退化矩形不画」成为统一不变式，顺带避免 `CentreBounds()` 算出无意义的绘制坐标）。
    - **为什么必须在调用点拦**：`assert(width > 0)` 在 `FillDrawPixelInfo()` **内部**，先于它 `return false` 执行，所以现成的 `if (!FillDrawPixelInfo(&tmp_dpi, r)) return;` 救不了；只有进函数时提前 `return` 才能挡住。
      ```cpp
      DrawPixelInfo tmp_dpi;
      int max_width = r.Width();
      /* R3R (KI-135): a degenerate rect trips the assertion inside
       * FillDrawPixelInfo() before it can return false ... */
      if (max_width <= 0 || r.Height() <= 0) {
          static int r3r_degenerate_logged = 0;
          if (r3r_degenerate_logged < 16) {
              r3r_degenerate_logged++;
              R3RDbgWrite("GUI-DEGEN-RECT roadveh left=%d top=%d right=%d bottom=%d\n", ...);
          }
          return;
      }
      if (!FillDrawPixelInfo(&tmp_dpi, r)) return;
      ```
    - 诊断日志：4 个新调用点复用 `r3r_perf.h` 的 `R3RDbgWrite()`（`roadveh_gui.cpp` / `train_gui.cpp` / `depot_gui.cpp` 各补一处 `#include "r3r_perf.h"`）；每处用文件内 `static` 计数上限 **16 行**，防止窗口重绘把 `R3R_debug.log` 刷爆。`R3R_PROBES=0` 的 Release 里该宏折叠成 `((void)0)`，**连实参一起消失**，所以发布版没有这段代码（也就没有日志可看——要看日志必须用 `build\` 的探针构建）。
    - 布局层诊断（用于找根因）：`src/depot_gui.cpp` `DrawVehicleInDepot()` 在算出 `image` 后加一条 `GUI-DEGEN-RECT depot type=%d cell=… image=… tag=… header=… count=… cols=…`，一次就把「车型 / 整格矩形 / 抠掉 tag+header+count 后的真实绘图矩形 / 三个宽度 / 列数」全打出来。
    - **根因（仍未定，探针已就位）**：路上车辆的车库格宽 = `resize.step_width = count_width + header_width + tag_width + padding.width + extend_left + extend_right`（`depot_gui.cpp:968/976`），而 `DrawWidget()` 里先 `ir = r.WithWidth(step_width, rtl)`（`num_columns != 1` 时）再 `Shrink(framerect)`，然后 `DrawVehicleInDepot()` 还要 `Indent(header_width)`（路上车 `tag_width = count_width = 0`）⇒ 绘图矩形宽 = `padding.width + extend_left + extend_right - 2*framerect`，只要有几像素的 `framerect`/`padding` 口径差异就会归零或变负。headless 构建里 `InitDepotWindowBlockSizes()`（`depot_gui.cpp:336`）把 block size 置 `{}`，是「无头测试也能踩到」的旁证。
    - **一条排除性推理**：购买汽车窗口（`src/build_vehicle_gui.cpp`）走的是 `DrawVehicleEngine` / `DrawEngineBadgeColumn`，**不调用 `Draw*Image`**，所以「购买界面崩」最可能不是购买窗口自己画的，而是**同 tick 里车库窗口的重绘**踩到同一断言（两个入口都通到车库窗口）。
    - **验证状态**：**本轮未编译、未实测**（用户要求本轮只改代码、不做构建）。待复测：点开汽车车库、购买汽车，均不应再崩；若仍在 `gfx.cpp:2059` 崩，说明还有第三个调用点未堵；若不再崩但 `build\R3R_debug.log` 里出现 `GUI-DEGEN-RECT`，请把该行贴回——它直接给出退化矩形的几何，可据此修布局本身。

- **KI-136（第 72 轮，2026-09-20）：仍然没有杜绝「不带真引擎的链独自移动」。状态：部分防护（**第 73 轮**：连挂侧本就已封死；「无真引擎链自主移动」已封死；只剩「不成段链拒绝解挂」待用户拍板）。严重度：中。**
  - 现状：连挂侧只做了 `GetCouplePosition` 的拒绝（`train_cmd.cpp:5104-5110`，记忆 18491399 提到的位置），解挂侧仍回退原生逐车启发式（`train_cmd.cpp:3811-3823`），且**没有任何地方禁止一条纯假引擎链自己跑起来**。
  - 待办：按原规则补「不成段/无真引擎链拒绝 couple/uncouple 且不允许自主移动」，与 KI-129（复制纯假引擎链）合并处理。
  - **第 73 轮核查结论（三条子项分开看）**：
    1. **「不成段链拒绝 couple」= 早已实现，无需再动**。`R3RCanCoupleNow()`（`src/train_cmd.cpp`，KI-60/KI-62）要求目标必须 `IsSegmentFront()`（★）**且**当前订单是 `OT_WAIT_COUPLE`，否则分别以 `target-not-segment` / `target-not-waiting` 拒绝；`GetCouplePosition()` 全链扫不到合格目标就返回。散链（free wagon，无 ★）与未经 `MakeSegment` 的普通车厢链**根本不可能被连挂**。注释里还明确写着 car-only formation「is NOT a substitute any more」。
    2. **「无真引擎链不允许自主移动」= 本轮已封死**。根因不是「没人检查」，而是**物理层不给零动力链停车**：`Train::UpdateAcceleration()` 是 `Clamp(power / weight * 4, 1, 255)`——下界恒为 1（俗称 1 hp 下限），所以任何链只要拿到一个普通 GOTO 订单就能自己蠕动；`CheckTrainStayInDepot()` 那条 `cached_power == 0` 自动停车又被 R3R 有意豁免（豁免原因见下），于是零动力链真的能自己爬出车库、沿站台挪。
    3. **「不成段链拒绝解挂」= 未动，待拍板**。见文末第 73 轮小节的说明：解挂侧的回退分支是**上游原生逐车启发式**（普通列车也能按它解挂），直接改成「拒绝」会让原生 DECOUPLE 对普通列车失效，而且简单 `return nullptr` 会踩已登记的硬伤（DECOUPLE 钩子 `(index,tile)` 纯函数、索引不推进就每 tick 重触发 ⇒ 列车卡死）。需用户在两个口径间选一个。
  - **修复（第 73 轮，`src/train_cmd.cpp` 单文件）**：在 `TrainLocoHandler()` 里、紧接既有的 `OT_WAIT_COUPLE` 停车闸门之后，新增
    `if (R3RIsCarOnlyFormation(consist) && consist->gcache.cached_power == 0) { …consist->cur_speed = 0; consist->subspeed = 0; return true; }`（另附 `CARONLY-PARK` 探针，仅在「被拦下时速度非 0」才写一行，避免每 tick 刷屏）。
    - 为什么是「停在这里」而不是改物理或改判据：这就是既有的 `OT_WAIT_COUPLE` 停车闸门的位置与手法（`return true` 且**不打 `VehState::Stopped`**）。因此 ①订单/寻路预留、站台装卸、时刻表在它之前都照常跑完，零动力链仍能被搜到、仍能装卸；②**不会**把它标成 `Stopped`，从而不会踩 `R3RCanCoupleNow` 的 `target-stopped` 拒绝——这正是 `CheckTrainStayInDepot()` 必须保留零动力豁免的原因（标 Stopped 会让等待编组的车底永远挂不上）。
    - 为什么加 `cached_power == 0` 而不是只看 `R3RIsCarOnlyFormation()`：后者只判「链头的引擎类型是 Wagon」。R3R 的换端重排会让链头身份迁移，理论上存在「链头是车厢、但链中仍有真机车」的链（多机车链），那种链**必须还能自己开**；`cached_power` 是 `ConsistChanged()` 汇总整链的动力，`== 0` 才精确等于用户口径的「**不带真引擎的链**」。
    - 合法移动该链的途径全部不受影响：真机车挂上来后合并链的链头是真机车（`R3RIsCarOnlyFormation` 为假）；真机车从车库出库时由 `NormalizeTrainVehInDepot()` **重接链**把它带走（不依赖它自己的速度）；`R3RStopChainInDepot()` 等车库命令同理。
    - 新探针：`CARONLY-PARK head=<idx> ord=<订单类型> tile=<x,y> spd=<被拦下前的速度>`。
    - 验证状态：**本轮未编译、未实测**。待复测：给一条零动力车底链（例如解挂后停在站台的假引擎车底）手动下一个 GOTO 订单 ⇒ 它**不应**移动、也不应离库；真机车耦合/牵引流程与「站台等待编组」必须完全不受影响；异常时贴回 `CARONLY-PARK` 行。


- **KI-137（第 72 轮，2026-09-20）：共享场：一个场不能被多个站场共享。状态：已修（**第 71 轮 KI-125 已完成**，本条为玩家复测确认点）。严重度：中。**
  - 玩家口径与 KI-125 一致：「1/2 场共享一个 A 场、3/4 场共享另一个 A 场」——也就是**同一个 A 场要被多个场同时指定为回落目标**。
  - 现状实现（第 71 轮）：`R3RStationYard::shared_with` 逐个场存回落目标，`R3RSetYardSharedWith()` 不做「目标只能被一个场占用」的限制（只禁止回落到自己/不存在的场），所以**一个场可以被任意多个场同时作为共享回落目标**，符合玩家口径。相关实现在 `src/station_base.h` / `src/station_cmd.cpp` / `src/station_gui.cpp`（下拉「无 + 除自己外的各场」）。
  - **已知设计限制（非本条诉求）**：回落目标必须是**同一个车站内**的场（`CmdR3RSetStationYardShared` 只带 `StationID` + 该站内的场 ID），**不支持跨车站共享**。若玩家后续要「跨站共享」，需另行扩展。
  - **重要证据：演示用的 exe 早于本条修复**。演示版本 `r3r-stable-2026-09-18-m (2)` 的 Build date 是 **2026-09-19 06:03**，而第 71 轮 KI-125 的修复构建 `build\openttd.exe` 是 **2026-09-19 23:43:05**——玩家现场用的是**改动前的 exe**（当时还是「全局单一共享池」语义）。因此本条**应判定为「第 71 轮已修、待用新构建复测」**，而非「新发现的未修问题」。
  - 待复测：正是 KI-125 的复测清单（第 71 轮小节末尾）。

## 附：第 72 轮修复记录（2026-09-20，**本轮未编译**）

本轮按用户要求「只改代码、不做构建」，因此下面所有改动**都没有编译、没有实测**，状态是「已改源码，待构建复测」。

**本轮修掉 / 处理的问题**

| ID | 状态 | 改动文件 | 一句话 |
| --- | --- | --- | --- |
| KI-130 | 已修（源码） | `src/train_cmd.cpp` | swap 统一出口 `UpdateStatusAfterSwap()` 对 depot tile 跳过 `VehicleEnterTile()`，不再用 `DiagDirection::Invalid` 刷信号 |
| KI-135 | 已修（源码，崩溃堵住；根因待探针） | `src/roadveh_gui.cpp`、`src/train_gui.cpp`、`src/ship_gui.cpp`、`src/aircraft_gui.cpp`、`src/depot_gui.cpp` | 4 个 `Draw*Image` 入口对 0/负尺寸矩形提前 return + `GUI-DEGEN-RECT` 诊断；`DrawVehicleInDepot()` 加布局诊断 |
| KI-137 | 已修（第 71 轮，待新构建复测） | 无（第 71 轮 KI-125 已实现） | 「一个场被多个场同时指定为回落目标」原生支持，无需再改代码 |

**改动清单（6 个文件，全部 `.cpp`，未碰任何 `src/*.h`）**

1. `src/train_cmd.cpp` — KI-130。
2. `src/roadveh_gui.cpp` — KI-135（保护 + 日志 + `#include "r3r_perf.h"`）。
3. `src/train_gui.cpp` — KI-135（保护 + 日志 + `#include "r3r_perf.h"`）。
4. `src/ship_gui.cpp` — KI-135（仅保护，无日志；该函数不裁剪，无断言风险）。
5. `src/aircraft_gui.cpp` — KI-135（仅保护，无日志）。
6. `src/depot_gui.cpp` — KI-135（布局诊断 + `#include "r3r_perf.h"`）。

**构建指引（下次编译时照此执行，见 KI-15 / 记忆 66636022）**

- 本轮改动**全是 `.cpp`** ⇒ 允许**增量构建**（`_tmp_inc_build.cmd`，`-j2`；本机内存有限，见 KI-24）。注意改完 `.cpp` 后要核对目标 `.obj` 的 `LastWriteTime` 晚于本次编辑，且链接成功后才算生效。
- 若下一轮同时改到任何 `src/*.h`（含 `r3r_perf.h`），则**必须删全部 `*.obj` 全量重编**（本机 ninja `deps=msvc` 解析不到中文 `注意: 包含文件:` 前缀，改头文件不触发重编，会链出混合布局二进制 → 随机崩溃）。
- **要看 `GUI-DEGEN-RECT` 日志必须用 `build\` 的探针构建**（`R3R_PROBES_DEFAULT=1`）；`build-release\` 的 `R3R_PROBES=0` 会把这些日志连同实参一起编译掉（KI-135 那条日志在发布版里不存在）。

**复测清单**

1. **KI-130**：车库 tile 上手动词动车（KI-127 场景）⇒ 不应再落到 `map_func.h:433`；顺带确认出库/入库/掉头后的信号与出库寻路正常。
2. **KI-135**：点开汽车车库、购买汽车 ⇒ 不再崩。若仍崩在同一断言，说明还有第三个 `FillDrawPixelInfo` 调用点未堵，请把新栈贴回；若不崩但日志出现 `GUI-DEGEN-RECT`，请把该行贴回（它给出退化矩形的几何，用于修布局本身）。
3. **KI-137 / KI-125**：1 场、2 场都设「共享 → A 场」，把 1 场停满 ⇒ 列车改停 A 场；3/4 场互不影响；回落改回「无」后立即失效；v3 存档往返一致；v2 旧档按迁移规则还原。

**本轮未动的问题（仍为未修/未实现）**：KI-126（逻辑翻后 artic 组内位置导致图像紊乱）、KI-127（`force_end_swap` 绕过 `train_flip_reverse_allowed = None`；KI-130 只是堵住它的崩溃副作用）、KI-128（神秘自动添加命令 + 低速驶出）、KI-129（复制纯假引擎链）、KI-131（属性翻三倍 / 探针副作用嫌疑）、KI-132（跨公司耦合后调度归属与权限越界）、KI-133（幽灵耦合进度条）、KI-134（自动命令后机车与车底间隙）、KI-136（不带真引擎的链拒绝自主移动，P7）。

> 注：其中 **KI-126 / KI-127 / KI-128 / KI-129 / KI-131 / KI-132 / KI-133 / KI-134 / KI-136 已在第 73 轮处理**（两个两个地推进），详见文末「第 73 轮」小节；仅余 KI-136 的「不成段车厢链拒绝解挂」口径待用户拍板（见该小节末）。

## 附：第 73 轮修复记录（2026-09-20，**本轮未编译**）

用户要求「**两个两个地**解决第 72 轮登记的十几个问题」，且**只改代码、不做构建**。本轮改动**全部是 `.cpp`**（`src/train_cmd.cpp`、`src/vehicle_cmd.cpp`、`src/order_cmd.cpp`），**未碰任何 `src/*.h`** ⇒ 按 KI-15 下次可**增量构建**。

**已成对处理的问题**

| 对次 | ID | 状态 | 改动文件 | 一句话 |
| --- | --- | --- | --- | --- |
| 第 1 对 | KI-126 | 已修（源码） | `src/train_cmd.cpp` | 逻辑翻转后补 `InvalidateNewGRFCacheOfChain()` / `InvalidateImageCacheOfChain()`（artic 组选图错乱） |
| 第 1 对 | KI-127 | 已修（源码） | `src/train_cmd.cpp` | `force_end_swap` 在 `train_flip_reverse_allowed = None` 时降级为原生「只倒车不掉头」 |
| 第 2 对 | KI-128 | 部分防护（源码；慢速根因已修、命令探针待复测） | `src/train_cmd.cpp` | 耦合前 `R3RSettleLoadingBeforeChainEdit(v, "couple")` + 清全链 `AdvanceInPlatform`；加 `ORD-AFTER-COUPLE` 逐条快照 |
| 第 2 对 | KI-129 | 已修（源码） | `src/vehicle_cmd.cpp` | 克隆：链头判定改 `v != v_front`；按源车镜像重建假引擎身份；`ConsistChanged` 延后到循环后 |
| 第 3 对 | KI-131 | 部分防护（源码；探针副作用嫌疑已排除、属性探针待复测） | `src/train_cmd.cpp` | 审查全部探针调用点（实参无副作用）⇒ 排除嫌疑；加 `CHAIN-ATTRS` + 逐段 `SEG` 属性快照 |
| 第 3 对 | KI-132 | 部分防护（源码；越权已堵、归属口径待拍板） | `src/order_cmd.cpp`、`src/train_cmd.cpp` | 新增 `R3ROrdersSharedWithOtherCompany()` 并插入全部 10 处订单命令；加 `COUPLE-XCOMPANY` 探针 |
| 第 4 对 | KI-133 | 已修（源码） | `src/train_cmd.cpp` | `R3RSettleLoadingBeforeChainEdit()` 循环首行对整链**无条件** `HideFillingPercent(&v->fill_percent_te_id)`（挂到「链头变更统一钩子」） |
| 第 4 对 | KI-134 | 已修（源码） | `src/train_cmd.cpp` | 新增 `R3RRespaceChainAfterEdit()`，`Couple()` 里接缝方向统一后把「实际距 > 标称距」的每一对往前推平（拼接只重链不重排像素，且整列刚性平移⇒间隙永不自愈） |
| 第 5 对 | KI-136 | 部分防护（源码；连挂侧本就已封死、「无真引擎链自主移动」已封死，仅「不成段链拒绝解挂」待拍板） | `src/train_cmd.cpp` | `TrainLocoHandler()` 里对「`R3RIsCarOnlyFormation()` 且 `cached_power == 0`」的链直接停车（复用 `WAIT_COUPLE` 停车闸门，不打 `Stopped`）；加 `CARONLY-PARK` 探针 |

**新探针一览（全部只读；每次耦合或每次判断最多写一行，不按 tick 刷屏）**

| 标签 | 落点 | 用途 |
| --- | --- | --- |
| `REVERSEDIR-NOFORCE` | `ReverseTrainDirection()` | KI-127：确认「强制换端」被 `None` 降级 |
| `ORD-AFTER-COUPLE` + `ORD idx=` | `Couple()` 提交后 | KI-128：确认耦合后接手的排程内容（逐条） |
| `CHAIN-ATTRS` + `SEG idx=` | `Couple()` 提交后 | KI-131：量整链与各段属性，定位「×3」 |
| `COUPLE-XCOMPANY` | `Couple()` 排程交接前 | KI-132：确认跨公司耦合现场与 owner 组合 |
| `RESPACE-AFTER-EDIT` | `R3RRespaceChainAfterEdit()` | KI-134：耦合后拉紧了几个像素（`px=0` / 无该行 = 现场本无残留间隙） |
| `CARONLY-PARK` | `TrainLocoHandler()` 零动力链停车闸门 | KI-136：仅在「被拦下前速度非 0」时写一行 ⇒ 该链此前确实在自己跑（常态停车不写） |

**构建指引**

- 本轮全是 `.cpp` ⇒ 可**增量构建**（`_tmp_inc_build.cmd`，`-j2`，内存受限见 KI-24）。改完务必核对目标 `.obj` 的 `LastWriteTime` 晚于本次编辑，且链接成功（`[N/N] Linking CXX executable openttd.exe`）才算生效。
- 若下一轮改到任何 `src/*.h`（含 `r3r_perf.h`）⇒ **必须删全部 `*.obj` 全量重编**（本机 ninja 解析不到中文 `注意: 包含文件:`，改头文件不触发重编 → 混合布局二进制 → 随机崩溃，见 KI-15 / 记忆 66636022）。
- 新增探针在 `R3R_PROBES=1` 的 `build\` 里会被求值（`R3RDbgWrite` 是 inline 函数），在 `R3R_PROBES=0` 的 `build-release\` 里连实参一起消失 ⇒ **要看新探针必须用 `build\`**。

**复测清单（按对次）**

1. **KI-126**：耦合/折叠修正后，看被翻转的 artic 组是否仍按「段内位置」选到正确的端头图（此前会闪成普通车厢图）。
2. **KI-127**：设「允许反向时掉头 = None」后手动点调向 ⇒ 应倒车、不换端且日志出现 `REVERSEDIR-NOFORCE`；设回 `All` 行为不变。
3. **KI-128**：站台上（装卸未完成时）耦合 ⇒ 立刻全速驶离、不再爬行；把 `ORD-AFTER-COUPLE` + 逐条 `ORD idx=` 与耦合前订单表对照贴回。
4. **KI-129**：复制「纯假引擎链」/「机车 + 车底」/「多段链」三类 ⇒ 复制品身份（段头假引擎、段尾假引擎、链头）与原车一致。
5. **KI-131**：复现「属性翻三倍」时贴回 `CHAIN-ATTRS` 的 1 行头 + n 行 `SEG`。
6. **KI-132**：多公司存档复现跨公司耦合 ⇒ 贴回 `COUPLE-XCOMPANY`；再用链头公司改订单 ⇒ 应被拒；**回归**：同公司耦合与普通列车改订单必须不受影响。
7. **KI-133**：把 `gui.loading_indicators` 设为「永远显示」，在站台装卸途中耦合 ⇒ 车开走后站台上**不应**再留进度条；贴回是否仍残留（若残留，说明还有非装卸途中的链头变更路径）。
8. **KI-134**：耦合成功瞬间（尤其走折叠修正的现场）接缝应**直接贴合**，行驶中不再出现机车与车底之间的可见间断；贴回 `RESPACE-AFTER-EDIT` 行（`px=` 值）与该次 `CPL` 相邻节 `gap`。
9. **KI-136**：给一条零动力车底链（解挂后停在站台/车库的假引擎车底）**手动下一个 GOTO 订单** ⇒ 它不应移动、也不应自己离库；真机车耦合、牵引出库、站台等待编组必须完全不受影响；若出现 `CARONLY-PARK` 行请贴回（说明拦下时它正在动）。

**第 72 轮清单已全部处理完毕**（KI-126 ~ KI-137）。唯一仍需用户拍板的一项如下：

**待拍板：KI-136 的「不成段车厢链拒绝解挂」口径**
连挂侧的拒绝早就有了（`R3RCanCoupleNow` 要求目标带 ★ 且订单为 `WAIT_COUPLE`），本轮把「零动力链自主移动」也封死了。剩下的是解挂侧：无 ★ 边界时 R3R 会回退到**上游原生逐车启发式**（普通列车也靠它解挂）。两个候选口径：

- **候选 A（推荐，零回归）**：保持现状。R3R 只保证「解挂点必须是段边界（★）」，无 ★ 的普通车厢链继续走原生逐车启发式 —— 即「不成段链拒绝解挂」这一条**不实施**，理由是该回退分支正是原生 JGRPP 对普通列车的解挂能力，废掉它会改变非 R3R 玩法。
- **候选 B（严格 R3R 语义）**：无 ★ 边界的 `OT_DECOUPLE` 一律**跳过并记日志**（新增 `DECOUPLE-SKIP-NOSEG`），同时把订单索引推进过该条。**注意**：不能只让 `GetDecoupleVehicle()` 返回 `nullptr` —— DECOUPLE 钩子是 `(cur_real_order_index, tile)` 的纯函数，索引不推进就每 tick 重触发，列车会当场卡死（已登记的硬伤，见记忆 67811241 硬伤 4）。实施 B 必须连「跳过并推进索引」一起改。

请告知选 A 还是 B；不选则按 A 处理（本轮已按 A 落地，未改解挂侧）。

---

## 第 74 轮（2026-09-20）：探针隔离性审计 + 新报告登记

### KI-138（新登记）：朋友报告的「发行版本莫名其妙跳命令」（待补充现场）
- 描述：朋友实报**发行版**（`build-release` 产物）仍出现「莫名其妙跳命令」。
- 来源：用户 2026-09-20 口述。
- 状态：**待复现 / 未定位**。严重度：中（行为不符）。
- **前置结论：探针开关不是原因** —— 见下「探针隔离性审计」：机械扫描实参/块体副作用 **0 命中**，且发行版把探针整块编译掉、任何环境变量都无法打开。
- 已知最可能的三个机制（下轮优先排查，全部与「排程交接」有关，与探针无关）：
  1. **KI-128 的既有设计（路线 A）**：`Couple()` 会把被挂车组的排程借给机车（`orders` 交接）。耦合完成后玩家看到的是「车组那份排程」而非机车原来那份 —— 现场观感就是「命令自己跳了 / 换了一条」。若现场发生在**耦合或解挂前后**，先按 KI-128 比对（`R3R_KNOWN_ISSUES.md` 内该条与记忆 18491399）。
  2. **解挂后订单索引未推进（记忆 67811241 硬伤 4）**：DECOUPLE 的触发是 `(cur_real_order_index, tile)` 的**纯函数**，而 `current_order.Free()` 与 `IncrementRealOrderIndex()` 都写在 `if (v->orders_backup != nullptr)` 的 hand-over 守卫里。没有 hand-over（例如「路线全写机车排程、车底链不写排程」的写法）时索引停在原地 ⇒ 钩子每 tick 仍成立 ⇒ **反复触发**，后面的段被级联解掉，看上去就是「命令被跳过去」。
  3. 次要嫌疑：`IncrementRealOrderIndex()` 回绕把已完成的 `GOTO_COUPLE` 又执行（记忆 18491399 ③）。
- 待用户补充（下轮定位用）：排程原文（每条类型 + 目的地）、发生时机（耦合后 / 解挂后 / 读档后 / 纯直行中）、是「少了一条」还是「多了一条 / 换了目的地」、发行版 exe 的构建日期；以及现场 `R3R_debug.log`。注意：**发行版没有探针**，要抓现场需发一个临时版（`-DR3R_PROBES=1 -DR3R_PROBES_DEFAULT=0`，运行前设 `R3R_DBG=1`）。
- **本轮追加的重要旁证（构建产物层面，优先级高）**：当前 `build-release\openttd.exe` @ **2026-09-19 07:40:20**（22 707 712 B），**早于第 70/71 轮（09-19 23:43）与第 72/73 轮（09-20）的全部修复**；而 `build\openttd.exe` 已是 **2026-09-20 11:51:18**。朋友若是拿这个旧 exe 复现，那么「跳命令」很可能已经在那几轮里被处理（尤其 KI-128 的排程交接、KI-133 装卸结清、KI-134 接缝拉紧）。**结论：在复核 KI-138 之前必须先重编发行版**（`R3R_release_build.cmd` 全量重编，须先关 CodeBuddy，见 KI-24 / KI-25），否则是拿缺了四轮修复的旧二进制定位，会白跑一轮。
- **发行版无探针的机械证据（本轮实测，两条命令即可复核）**：`build-release\build.ninja` 里 `-DR3R_PROBES=1` 出现 **0 次**、`-DR3R_PROBES_DEFAULT=0` 出现 **623 次**；`build-release\CMakeCache.txt` 内 `R3R_PROBES_DEFAULT:UNINITIALIZED=0`。配合 `src/r3r_perf.h:129-130` 的 `#ifndef R3R_PROBES` ⇒ `#define R3R_PROBES R3R_PROBES_DEFAULT`，发行版的 `R3R_PROBES` 恒为 0，**任何环境变量（`R3R_DBG` / `R3R_PERF` / `R3R_DIAG`）都打不开探针**。

### 探针隔离性审计（用户要求：确认探针开关完全不会误伤正常功能）
审计范围：`src/r3r_perf.h` 全部开关 + 全仓所有探针调用点。结论：**开关只影响日志与计时，不改变任何运输行为；发行版可证无探针。**

| 层 | 机制 | 关闭时行为 |
| --- | --- | --- |
| 编译期总闸 | `#ifndef R3R_PROBES` ⇒ `#define R3R_PROBES R3R_PROBES_DEFAULT`；`_DEBUG` ⇒ 1，否则 0 | `build-release` 显式 `-DR3R_PROBES_DEFAULT=0` 且未定义 `R3R_PROBES`（已核 `build-release\build.ninja` 的 `DEFINES`）⇒ 发行版 **=0**，任何环境变量都打不开 |
| 日志写入 | `R3RDbgWrite(...)` = `((void)0)` | **连实参一起消失**（09-19 修；否则调用点仍会求值实参，GUI 路径的 `GetWidget<NWidgetCore>()` 因此存活） |
| 运行开关 | `R3RDbgOn()` / `R3RPerfOn()` = `return false`（常量） | MSVC 把每个 `if (...)` 块整体折叠掉 |
| 写盘入口 | `R3RFopenDbg()` 返回 `nullptr` | 全仓 100 个调用点统一为 `FILE *x = R3RFopenDbg(...); if (x != nullptr) {...}`；**0 处**裸用 `fprintf(R3RFopenDbg(...))` ⇒ 无空指针、无写盘 |
| 危险诊断 | `_pause_mode = PauseModes{}`（暂停看门狗，清暂停会让「以为暂停了」的存档继续跑） | 双重门：`#if R3R_PROBES` **且** `R3R_DIAG=1`（默认关）。发行版整块编译掉；交互游戏下永不运行（KI-109） |
| 每帧钩子 | `R3RPerfFrameTick()` | 首行 `if (!R3RPerfOn()) return;` ⇒ 不计数、不 dump、不生成 `R3R_perf.log` |

机械扫描结果（本轮重跑，全仓）：
- 全部 `R3RDbgWrite(` / `R3RDbgEdge(` 调用点，实参中出现 `++` / `--` / `ConsistChanged` / `Invalidate*` / `Set*` / `Clear*` / `DecoupleTrain` / `ReverseTrain*` / `ReserveTrack*` / `MakeSegment` / `SetPixelPosition` ⇒ **0 命中**（实参无副作用）。
- 全部 `if (R3RDbgOn() | R3RPerfOn() | R3RDbgEdge(...))` 块体中出现 `->字段 =` / `ConsistChanged` / `Invalidate*` / `SetFrontEngine` / `ClearFrontEngine` / `SetSegmentFront` / `AddToShared` / `DeleteVehicleOrders` ⇒ **0 命中**（块体只有日志与计数器）。
- 编译期 `#if R3R_PROBES` 仅 3 处：`r3r_perf.h`（开关本身）、`sl/vehicle_sl.cpp`（16 KB raw-byte dump，纯写盘）、`openttd.cpp`（暂停看门狗，见上表）。
- 抽样确认每 tick 都会调用的 `R3RCheckTtSync()` 为**纯只读**：两处 early-return + `R3RFopenDbg()==nullptr` 即返回，不写任何索引。

遗留清理项（**不是缺陷**：已 gate，发行版不执行，可延后删）：「TEMPORARY DIAGNOSTIC」写盘块 —— `sl/vehicle_sl.cpp`（LOADCENSUS-ENTER / LOADCENSUS-PHASE2END / R3RDUMP，每次读档做一次全车辆扫描）、`sl/saveload.cpp`（CHUNKLOAD\*\*\* / AFTER_LOADCHUNKS / BEFORE_PTRS / INVALID_REF_VEHICLE / SLERROR）、`os/windows/win32.cpp`（SHOWINFO）。开启探针时只增加读档耗时与日志体积。

### 第 74 轮清单状态汇总
- 第 72 轮清单（KI-126 ~ KI-137）**全部有源码修复**；未落地项仅两类：①**复测**（KI-128/129/131/133/134/136 等，见文末复测清单 1~9）；②**两项待拍板**（KI-132 跨公司归属口径、KI-136 解挂侧 A/B）。
- 新增 **KI-138**（朋友实报「跳命令」），状态待复现，等用户补充现场。
- **KI-90 本轮结案**：这条从第 42 轮起挂着的「他公司挂接分组窗口应整体置灰」UI 项，本轮按清单里自己写明的推荐方案 **(A)「他公司窗口整体只读」** 落地（只改 `src/couple_group_gui.cpp`），详见 KI-90 条目。结论：**至此清单中已不存在「有明确修法却尚未落地」的条目**。剩余的只有四类，都不是「能修而未修」：①**工具链规则类**（KI-15 头文件依赖失效、KI-16 改语言 txt 需重编 `strings.cpp`——是「怎么做」的规约而非代码缺陷）；②**KI-64**（低严重度 + 低收益，且需改 `src/*.h` ⇒ 触发全量重编，性价比不足）；③**KI-102/103/104**（早期假设，已被后续轮次的实测结论取代）；④**需要玩家输入才能推进的项**——复测类（`KI-128/129/131/133/134/136` 等，需按文末复测清单动手跑）、口径拍板类（`KI-132` 跨公司归属口径、`KI-136` 解挂侧 A/B，均已按 A 预落地、待确认）、新报缺陷 `KI-138`（需现场信息）。

### 第 74 轮构建证据（增量构建，`EXIT_CODE=0`）
- 编译前核对：`build\openttd.exe` @ 2026-09-19 23:43:05 为上次全量重编产物；本轮**只有 8 个 `.cpp` 比 exe 新**（`train_cmd.cpp` / `order_cmd.cpp` / `vehicle_cmd.cpp` / `depot_gui.cpp` / `train_gui.cpp` / `roadveh_gui.cpp` / `ship_gui.cpp` / `aircraft_gui.cpp`），**无任何 `src/*.h` 变更** ⇒ 按 KI-15 / 记忆 66636022 增量构建**合法**（无需删 obj 全量重编）。
- **第 1 次增量构建 `EXIT_CODE=1`（新错误，已修）**：`src/train_cmd.cpp(6440/6446)` `error C2039: "cached_max_speed": 不是 "GroundVehicleCache" 的成员`，连带两条 `fprintf` 实参不足警告（C4473）。这是 **KI-131 的 `CHAIN-ATTRS` 探针**新引入的编译错误（本轮之前该探针从未编译过）：JGRPP 把「编组最高速度」存在 `VehicleCache::cached_max_speed`（`vehicle_base.h:105`），**不在** `GroundVehicleCache` 里。修法（`src/train_cmd.cpp`，仅 2 行）：`v->gcache.cached_max_speed` / `w->gcache.cached_max_speed` → `v->GetDisplayMaxSpeed()`（`train.h:266` 即 `return this->vcache.cached_max_speed;`，语义等价且对 `Train` 恒有效）。
- **第 2 次增量构建 `EXIT_CODE=0`**：`[1/5]` 版本生成 → `[2/3] Building CXX object CMakeFiles\openttd_lib.dir\src\train_cmd.cpp.obj` → `[3/3] Linking CXX executable openttd.exe`；日志 `build\R3R_incbuild.log`，标记 `build\R3R_incbuild.done = EXIT_CODE=0`。
- 产物：`build\openttd.exe` @ **2026-09-20 11:51:18**（50 782 208 B）；版本串 `r3r-stable-2026-09-19-m`，Release 0.73.1。
- **第 3 次增量构建（KI-90 落地后）`EXIT_CODE=0`**：`[2/3] Building CXX object ...\src\couple_group_gui.cpp.obj` → `[3/3] Linking CXX executable openttd.exe`；最终产物 `build\openttd.exe` @ **2026-09-20 11:55:51**（50 782 208 B）。
- 一致性核对（防 KI-15 混合布局）：改后重跑「比 exe 新的 `src/**/*.{h,cpp,hpp,txt}`」统计 = **0** ⇒ 本轮 9 个 `.cpp` 全部已重编进 exe。
- **警告：发行版未同步**。`build-release\openttd.exe` 仍是 **2026-09-19 07:40:20**（22 707 712 B），**不含第 70~74 轮的任何修复**。若要给朋友新的发行版，必须跑 `R3R_release_build.cmd`（全量 667 步，须先关 CodeBuddy，见 KI-24 / KI-25 的内存与闸门说明）。**【第 75 轮已解决】** 2026-09-20 13:53 已用 `R3R_release_fullbuild.cmd` 全量重编成功（`EXIT_CODE=0`），新 `build-release\openttd.exe` = **22 755 328 B** @ 13:53:31，含第 70~75 轮修复且 `R3R_PROBES_DEFAULT=0`，详见文末「第 75 轮 · 状态」。
- **教训（可复用）**：探针代码挂在 `R3R_PROBES=1` 且**长时间不编译**时会悄悄腐化（引用了被上游重命名的成员）。**每轮改完探针必须真的编译一次**，否则下一次构建才会暴露，且会挡住真正需要验证的功能修复。本例的两条 `fprintf` 实参警告是「成员不存在导致该实参被丢弃」的连带现象，修掉成员名后同时消失。

## 附：第 75 轮（2026-09-20）：发行版「双击全量构建」脚本落地 + cmd 解析两处硬伤

### 新增工具（交付玩家发行版用，取代旧的增量脚本）
- `R3R_release_fullbuild.cmd`（双击入口，GBK/无 BOM）：预检 → 复用 `build\vcpkg_installed` → `vcvars64` → configure（RelWithDebInfo + 强制 `/Zi /O2 /Ob2 /DNDEBUG`、`-DR3R_PROBES_DEFAULT=0`）→ **三道硬闸门** → 删 `build-release` 下全部 `*.obj`（KI-15 合规）→ `ninja -j2 openttd` → 产物核验 → 写 `build-release\R3R_release_fullbuild.done` → `pause` 保留窗口。
- `R3R_release_fullbuild_run.ps1`（纯 ASCII 助手，KI-16 合规）：`-Action precheck|build|verify`。`precheck` 查可用内存（<1.2 GB 告警 KI-24）与 `openttd.exe` 是否在跑（在跑则 exit 96，防 LNK1168）；`build` 用 `Tee-Object` 留全量日志；`verify` 两道闸门 = 新鲜度（exe 必须 < 10 分钟，防「链接根本没发生却祝福旧二进制」）+ 体积（> 35 MB 判为 Debug）。
- `R3R_release_build.cmd` 加了运行时可见横幅（用 `echo` 而非 `rem` —— `@echo off` 下 `rem` 看不见）指向新脚本；原增量脚本保留可用。
- 三道硬闸门（缺任一即非零退出，绝不放行）：① `build.ninja` 不得含 `FLAGS = .../Od`，且必须含 `/O2`；② `-DWITH_ZLIB / -DWITH_LIBLZMA / -DWITH_ZSTD / -DWITH_LZO / -DWITH_PNG / -DWITH_OPUSFILE` 六个宏必须都在（KI-25）；③ `-DR3R_PROBES_DEFAULT=0` 必须在（发行版无探针，KI-138 的核对前置）。
- 退出码约定：90=xcopy 91=vcvars64 92=外部库 93=工具缺失 94=探针标志 96=预检 97=ninja/目录或 verify 99=/Od。`--check` 干跑模式只做预检 + configure + 三道闸门，不删 obj、不编译。

### KI-139（已修）：`R3R_release_fullbuild.cmd` 的 cmd 解析两处硬伤
- ① **嵌套 `if` 的 `else` 绑错**：`if "%RC%"=="0" if "%VF%"=="0" (成功) else (失败)` —— cmd 把 `else` 绑到**内层** `if`，一旦 RC≠0 外层为假就**两个分支都不执行** ⇒ 失败时 done 文件根本不写、真退出码丢失。修法：拆成 `set "OKBUILD="` + `if "%RC%"=="0" if "%VF%"=="0" set "OKBUILD=1"`，再 `if defined OKBUILD ( … ) else ( … )`；另加 `set "FINAL=%RC%"` + `if "%FINAL%"=="0" set "FINAL=97"`，避免「ninja 成功但 verify 失败」被误记为 0。
- ② **块内 `echo` 的未转义括号会移位块边界**：把 `echo   BUILD FAILED (ninja=%RC%, verify=%VF%)` 或 `echo … (KI-15)` 写在 `( … )` 块里时，cmd 的括号计数被打乱 —— 实测成功分支会**连带打印 else 分支的行**、失败分支只剩半行且不写 done 文件。修法：块内 `echo` 文本一律不放括号。
- 实证：沙箱（手写 `build.ninja` + 桩 cmake + 假 obj）跑通两条真实路径 —— 成功路径 done=`EXIT_CODE=0` 且产物存在；失败路径 done=`EXIT_CODE=1`、打印 `BUILD FAILED   ninja=1   verify=1`、输出无错位。`--check` 干跑实测 `EXIT_CODE=0 / MODE=CHECK_ONLY`（三道闸门全过）。
- 附带：`verify` 里「扫 `ucrtbased.dll` 判 Debug」的判据**已删除** —— 本项目两个构建树都用静态 CRT（`x64-windows-static`），实测 50 MB 的 Debug exe 也**不含**该串，该判据恒为假；改用新鲜度 + 体积（Release ~22 MB / Debug ~50 MB）。

### 状态（第 75 轮收口：**已实测跑通一次完整构建**）
- `R3R_release_fullbuild.cmd`：**已验证**。2026-09-20 在同一台机器上真跑了一次**全量**构建（先删 `build-release` 下全部 `*.obj`，共 709 步），日志 `build-release\R3R_release_fullbuild.log` 末两行为 `[707/708] Building CXX object ...win32_main.cpp.obj` → `[708/708] Linking CXX executable openttd.exe`；标记 `build-release\R3R_release_fullbuild.done = EXIT_CODE=0`（该 done 文件只在 ninja 与 verify **双双通过**时才写 0 —— 见上 KI-139 ①，故它本身就是「构建成功」的凭据）。
- 产物：`build-release\openttd.exe` @ **2026-09-20 13:53:31**（**22 755 328 B** ≈ 21.7 MiB，落在 Release 区间，远低于 Debug 的 ~50 MB）；版本串 `r3r-stable-2026-09-19-m`，Release 0.73.1（日志 `-- Version string:` 行）。
- 三道闸门**独立复核通过**（直接 grep `build-release\build.ninja`，不依赖脚本自述）：①`FLAGS` 行为 `/DWIN32 /D_WINDOWS /GR /EHsc /Zi /O2 /Ob2 /DNDEBUG -std:c++20 -MT ...`，且 `FLAGS = .*/Od` **零命中**；②`DEFINES` 行同时含 `-DWITH_ZLIB -DWITH_LIBLZMA -DWITH_ZSTD -DWITH_LZO -DWITH_PNG -DWITH_OPUSFILE`（KI-25 全过，压缩存档 / PNG / 音乐可用）；③含 `-DR3R_PROBES_DEFAULT=0`。
- ⇒ 发行版现已**包含第 70~75 轮全部修复，且探针默认关闭**。
- 与 KI-138 的衔接：**前置条件已满足**，可用这个新 exe 复核朋友的「莫名其妙跳命令」。上文（第 74 轮）「exe 仍是 2026-09-19 07:40:20 旧产物、复核前先重编」一句**已作废**。
- 遗留：`R3R_release_fullbuild.cmd` 的**失败分支**仍只有沙箱仿真证据（成功分支现已实测）。

## 附：第 76 轮（2026-09-20）：逻辑反转不再打乱铰接组内顺序（KI-140）

### KI-140（已修）：打散态铰接组在逻辑反转后被整体倒序，GRF `0x4D` 随之对调、图像错乱
- **现象（玩家复现）**：6 节铰接式列车（已升为段 / 已打散）在换端或耦合折叠修正触发「逻辑反转」后，肉眼可见组内第 2 与第 5 节的图像对调（1/6、3/4 两处图像相同或为空，看不出差别）。玩家判定「位置变量变了」。
- **取数依据**：GRF 判断「此节在铰接组里排第几」用的是 NewGRF 变量 `0x4D`（position in articulated vehicle，`src/newgrf_engine.cpp` 的 `case 0x4D`）：`artic_before` 沿 `IsArticGroupMember()` 向 `Previous()` 数、`artic_after` 沿 `HasArticulatedPart()` 向 `Next()` 数。**它不存车上，每次问就按当前链序现算** —— 链序一变，值就变。
- **根因**：`R3RFlipChainBySegments()`（`src/train_cmd.cpp`）第 2 步收集「铰接块」时判据写的是 subtype 位的 `p->IsArticulatedPart()`。但打散态铰接组的 subtype 位早已被 `ClearArticulatedPart()` 清掉（`src/vehicle_cmd.cpp` 的 `DearticulateChainWithSnapshot()`），只剩新增的角色位 `VehicleRailFlag::ArticGroupMember`。该判据对打散组**全部返回 false** ⇒ 6 节各自成块 ⇒ 第 3 步「段内块倒序」把**组内顺序也一起翻了过来** ⇒ `0x4D` 按新名次重算（6 节即 1↔6 / 2↔5 / 3↔4 全对调）⇒ GRF 按新名次选图。1/6、3/4 两个位置在 GRF 里取到同一张（或空）图，所以只有 2↔5 肉眼可见。
- **判据不统一的旁证**：同一份角色语义层里，其余取数点（`train.h` 的 `IsArticGroupHead` / `InArticGroup`、折叠探测 `R3RCheckChainFoldedDirection`、`0x4D` 自身）全都走 `IsArticGroupMember()`，唯独这个分块循环用了 subtype 位。
- **修复（按用户两条硬性要求）**：要求① `position_in_articulated_parts` 保持翻转前的铰接内顺序；要求② 逻辑反转照常完成、不被铰接式阻挡。落点只在 `src/train_cmd.cpp`：
  - **第 2 步块收集判据** `p->IsArticulatedPart()` → `p->IsArticGroupMember()`（`src/train.h:181`，= subtype 位 **或** `ArticGroupMember` 角色位）。打散组因此与真 artic 组**同构**：块 = 组头 + 其后连续组员，块内顺序**恒不变**（满足①），块仍作为整体参与段内倒序（满足②——组整体在段内换到另一头，组内一节不动）。
  - **删除第 4b 步 `R3RReassignArticGroupRoles()`**（连同函数本体，原位留说明性注释）。它本是用来把「被倒序后的组头角色」迁回组首的；块内顺序不再颠倒后已无用，**而且必须删而不能留**：该归一化按「链上连续带角色位的车」识别组，段内倒序会把两个原本相邻的打散组前后对调，归一化便把它们误当成同一个连续块合并（后一组的组头被改成 member、两组 `0x4D` 全部错位），反而制造新的错乱。
  - `R3RCaptureArticRoles()` / `R3RUndoLogicalFlip()` 的角色位快照与还原**保留**（即那条「角色位与链序错配 ⇒ `0x4D` 沿 `Previous()` 走空指针崩溃」的防护），注释已改为「防御性还原」：逻辑反转不再改动角色位，空快照（真 artic 组）等价于全链清除、对打散组为精确还原。
- **修复后不变量**：真 artic 组行为与改动前等价（两种判据对其完全相同）；打散组新增「块内不拆」语义；`new_head` 与新段首 ★ 现在落在「原链尾所在铰接块的首车」= 组头车，而非组内任意一节 —— 比修复前更符合「父车必须在其 parts 之前」这条硬不变式。
- **构建证据（增量，`EXIT_CODE=0`）**：只改 `src/train_cmd.cpp`、无 `src/*.h` 变更 ⇒ 按 KI-15 / 记忆 66636022 增量合法。日志 `build\R3R_incbuild.log` 末行 `[3/3] Linking CXX executable openttd.exe`；标记 `build\R3R_incbuild.done = EXIT_CODE=0`；产物 `build\openttd.exe` @ **2026-09-20 22:02:47**（50 789 376 B），`train_cmd.cpp.obj` @ 21:57:48（早于 exe）。`read_lints(src/train_cmd.cpp)` 零诊断；全仓已无 `R3RReassignArticGroupRoles` 调用点（仅剩两处说明性注释）。
- **状态 = 部分防护（编译通过 ≠ 实测通过）；第 77 轮已修正 —— 块判据改回 subtype 位、角色位迁移复活并修好、并新增 `SegmentFlipped` 名次镜像，详见下文第 77 轮小节**：待游戏内复测 ① 6 节铰接组在逻辑反转（换端 / 耦合折叠修正）后组内各节图像与翻转前一致（重点看 2/5 节）；② 段内含多个打散组（尤其两个相邻打散组）反转后各自组头正确、无跨组合并；③ 真 artic 组（未做 `MakeSegment`）行为无回归。**发行版 `build-release\openttd.exe` 尚未同步本修复**，需跑 `R3R_release_fullbuild.cmd`。

## 附：第 77 轮（2026-09-20）：段内倒序彻底化 + `SegmentFlipped` 名次镜像（KI-140 修正、KI-141、KI-142）

### KI-140（第 77 轮修正）：块判据改回 subtype 位 —— "段内彻底倒序"才是目标语义
- **玩家口径纠正（2026-09-20）**：KI-140 追求的"打散组内链序不变"**不是**目标 —— 要的是**段内彻底倒序**：6 节打散组 `1..6` 逻辑反转后链序就是 `6..1`（原话「345678 变成 345876」，即被翻的那一段整体倒过来）。KI-140 用角色层判据把打散组当原子块，等价于"块在段内换位、组内一节不动"，与目标不符。
- **目标拆成两条、必须同时成立**：① 链序真的要倒过来（同时让 GRF 侧看到的名次仍是原名次 —— 由 `SegmentFlipped` 镜像实现，见 KI-141）；② 物理位置 / direction / 图像一个像素不动。
- **落点（`src/train_cmd.cpp` `R3RFlipChainBySegments()`）**：
  - 第 2 步块收集判据 `p->IsArticGroupMember()` → 回到 subtype 位的 `p->IsArticulatedPart()`：只有**真 artic 组**（父车 + 其后连续真 artic part）是原子块；打散组（`ArticGroupHead` + 其后 `ArticGroupMember`）的每一节各成一块 ⇒ 段内倒序时组内链序真正翻转。
  - 第 2b 步（新增）：在重链**之前**、按原链序记录每个打散组的车辆序列（`ArticGroupHead` 起，其后连续 `ArticGroupMember` 止；只吃 member 位、遇下一个 head 位即停）。
  - 第 4b 步（新增，即 KI-140 删掉的迁移，复活并修好）：先清全链角色位，再按第 2b 步记录的组序列把**原组尾**标为 `ArticGroupHead`、其余（含原组头）标为 `ArticGroupMember`（新组首 = 原组尾）。
    - 与 KI-140 删掉的 `R3RReassignArticGroupRoles()` 的**关键差别**：迁移按"翻转前记录好的组序列"做，不做"链上连续带角色位的车"这种事后识别。旧实现正是在这里会把段内倒序后前后对调的两个相邻打散组误并成一个（后一组组头被降级、两组 `0x4D` 全错位）。⇒ 现在**任意数量、包括相邻的**打散组都能各自正确迁移。
- **`R3RCaptureArticRoles()` / `R3RUndoLogicalFlip()` 的角色位快照还原从"防御性"变回"必需"**：回滚若不还原，角色位会留在翻转后的分布上，直接触发「链头残留 `ArticGroupMember` 且 `Previous()==null` ⇒ `0x4D` 沿 `Previous()` 走空指针虚调用」那类崩溃。注释已改回。
- 第 3 步的 `new_head->InvalidateNewGRFCacheOfChain()` 仍然必需（链序变了 + 名次基准变了）。

### KI-141（新增，待实测）：段内倒序后 GRF 变量 `0x40`/`0x41` 与 `0x4D` 按新名次取数 ⇒ 用 `SegmentFlipped` 镜像
- 段内倒序改变了每节车在段内 / 组内的名次，而 `0x40`/`0x41`（position / length in consist）与 `0x4D`（position in articulated vehicle）都是"每次问就按当前链序现算、不存车上"（`src/newgrf_engine.cpp`），链序一变值就变 ⇒ GRF 选图错位（KI-140 的现象换了成因又回来了）。
- **修法**（新增车旗 `VehicleRailFlag::SegmentFlipped = 26`，`src/train.h`，自反标记）：
  - `R3RFlipChainBySegments()` 第 1b 步在**被翻转链的每节车**上 `FlipSegmentFlipped()`；`R3RUndoLogicalFlip()` 对同一条链再翻一次还原（自反，无需进快照）。
  - `PositionHelper()`（`0x40`/`0x41`）：`IsSegmentFlipped()` 为真时把 `chain_before` / `chain_after` 两个字节对调（高字的段长度不变）。
  - `case 0x4D`：`IsSegmentFlipped() && HasDearticulatedGroupRole()` 时把 `artic_before` / `artic_after` 对调。**真 artic 组不带角色位、组内顺序从不改变，故不参与镜像** —— 这就是"管辖范围"的边界。
- 玩家已确认 `0x40`/`0x41` **一并**纳入管辖（不只 `0x4D`）。
- 存档：`VehicleRailFlags` 在 `XSLFI_TRAIN_FLAGS_EXTRA >= 1` 时按 `SLE_UINT32` 存，bit 26 与既有 bit 24/25 同字段，无需升存档版本。
- **待实测**：① 6 节打散组逻辑反转后 GRF 选图与翻转前逐节一致（重点 2/5 节）；② 段内含多个（含相邻）打散组时各组名次正确、无跨组合并；③ 真 artic 组（未做 `MakeSegment`）无回归；④ 连翻两次（翻转 → 回滚 / 再次翻转）后名次回到原值。

### KI-142（新增，待复测 / 待定）：段降级把被翻转过的打散组重新铰接时，父车角色落到原组尾
- `RearticulateChain()` 按 `ArticGroupHead` 决定父车；翻转后组首 = 原组尾 ⇒ 降级得到的真 artic 组父车换到另一端（链序 `8,7,6` 而父车是 8）。运输统计（按父车引擎记录取）与 `0x4D` 基准都可能与翻转前不同。
- **现状：本轮不做专项处理，列为待复测**。若要修，方向是在 `RearticulateChain()` 入口先用 `IsSegmentFlipped()` 把该组链序倒回原状（depot 内允许重排），或在降级命令里显式告知。

### 构建（第 77 轮）
- 改动文件：`src/train.h`（新车旗 + 访问器）、`src/train_cmd.cpp`（第 1b / 2 / 2b / 4b 步、`R3RUndoLogicalFlip`、注释）、`src/newgrf_engine.cpp`（`PositionHelper` 与 `case 0x4D` 的镜像）。
- **改了 `src/train.h` ⇒ 按 KI-15 / 记忆 66636022 必须删全部 `*.obj` 全量重编**（增量非法）。

## 附：第 78 轮（2026-09-21）：双头机车被「段尾假引擎」误升格 ⇒ `NormaliseDualHeads()` 死循环冻结（KI-143）

### KI-143（已修 / 待实测）：`SetSegmentTailFakeEngine()` 给双头机车后半节戴上引擎位，`NormaliseDualHeads()` 两半互推成死循环
- **现象（玩家两次实报，两份日志都断在同一处）**：
  - 场景 A：`build\R3R_debug_dual_headed_when_in_segment.log`（双头机车所在链升为段后，在车库再把另一辆车搬上去）。
  - 场景 B：`build\R3R_debug_dual_headed_try_coupling.log`（双头机车所在段与另一列段连挂）。
  - 两份日志**都停在 `ARRANGE-IC-DONE` 之后、`ARRANGE-NDH-DONE` 之前**（A 末行 42、B 末行 119），即死在 `ArrangeTrains()` 末尾的 `NormaliseDualHeads()`（`src/train_cmd.cpp`）里。
  - **没有生成 `crash-*.log`**（`Documents\OpenTTD` 最新一份仍是 09-19）⇒ 不是访问违例，而是**进程卡死**：死循环占满该帧，游戏无响应。玩家口径的「崩溃」= 卡死。
- **现场数据（两份日志同构）**：`MAKESEG UPGRADE-BEFORE/AFTER-SPLIT` 显示链头 `subtype=0x29`（front+engine+multihead）、链尾 `subtype=0x20 → 0x28`（multihead，升格后多了 engine 位），且两端 `eng=` 是同一个引擎 ID。即「双头机车前半节 + 2 节同型号车厢 + 后半节位于链尾」，链尾那节就是双头机车的**后半节**。
  - 场景 A 链：`0(0x29) → 2 → 3 → 1(0x20→0x28)`，随后 `ARRANGE-IN dh=0 dst=1 sh=4 src=4 mc=0`（把 veh4 搬到 veh1 之后）。
  - 场景 B 链：`4(0x29) → 6 → 7 → 5(0x20→0x28)`，随后 `ARRANGE-IN dh=4 dst=5 sh=0 src=0 mc=1`（把整条链 0 连挂到 veh5 之后）。
- **根因链**：
  1. 双头机车前半节 = `MULTIHEADED + ENGINE + FRONT`（0x29）；后半节 = `MULTIHEADED`，**故意不带 ENGINE 位**（`IsRearDualheaded()`，`src/ground_vehicle.hpp`）。
  2. `SetSegmentTailFakeEngine()`（`src/vehicle_cmd.cpp`，由 `CmdMakeSegment` 调用）的守卫只有 `last == seg || last->IsEngine()` ⇒ 后半节 `IsEngine()` 为假，**照样被执行 `SetEngine() + ClearWagon() + ClearFreeWagon()`** ⇒ 0x20 变 0x28，成了「没有 FRONT 位的假前车」。**该车就此同时满足 `IsMultiheaded() && IsEngine()` —— 而这条组合在良构双头机车里只允许出现在前半节。**
  3. `NormaliseDualHeads()` 的稳定前提正是「后半节不是引擎」：`for (u = t; u->Next() != nullptr && !u->Next()->IsEngine(); u = u->Next()) {}`，两半节相邻时 `u` 正好落在另一半上、被紧接的 `if (u == t->other_multiheaded_part) continue;` 放行。后半节一旦带引擎位，这个 `u` 再也落不到另一半上（后半节自己就是「下一个引擎」）⇒ 每遇到一半就把另一半摘下来插到自己身后，下一次迭代再反过来 ⇒ **两半节互相往对方身后搬，`t = t->GetNextVehicle()` 永远在两者之间打转，死循环**。`RemoveFromConsist` / `InsertInConsist` 只摘不删、链长不变，所以永远不会自行跳出（已按场景 B 的链序逐步推演确认：`t` 在 4→5→4→5… 间无限循环）。
  4. **旁证（同一次误升格还破坏了 vanilla 自己的守卫）**：`CmdMoveRailVehicle`（`src/train_cmd.cpp:2509`）本来有 `if (!move_chain && dst != nullptr && dst->IsRearDualheaded() && src == dst->other_multiheaded_part) return CommandCost();`，语义正是「不许把双头后半节搬到自己后面」。场景 A/B 里 `dst` 恰是被误升格的那一节，`IsRearDualheaded()` 已变假 ⇒ 守卫失效，命令得以进入 `ArrangeTrains()`。
- **修复（两处，均为 `.cpp`，无 `src/*.h` 变更）**：
  - **根因修复** `src/vehicle_cmd.cpp` `SetSegmentTailFakeEngine()`：升格前加 `if (last->IsRearDualheaded()) return;`（★ 的 `SetSegmentBack()` 仍在前一步照打，段的右边界不受影响）。双头后半节本来就永远不能带头，不该也不需要假引擎身份。
  - **防御** `src/train_cmd.cpp` `NormaliseDualHeads()`：循环入口加 `if (t->other_multiheaded_part == nullptr || t->other_multiheaded_part->IsEngine()) continue;`。判据「配对的另一半也带引擎位」在**任何良构双头机车里恒为假**（良构时另一半一定不是引擎），只有「后半节被误升格」这种畸形态才为真；命中即整段跳过，把「死循环冻结」降级为「保持原样」。顺带挡掉空指针（原先 `RemoveFromConsist(nullptr)` 会崩）。
- **为什么不是别的落点（已逐个排除）**：全仓只有 `SetSegmentTailFakeEngine()` 会在「可能是双头后半节」的车身上打引擎位 ——
  - `R3RCreateCarOnlyFormation()`（`src/train_cmd.cpp`）只作用于 `IsFreeWagon()` 链头，而后半节 subtype 0x20 **没有 FREE_WAGON 位**；
  - `src/vehicle_cmd.cpp` 的克隆/销毁路径（`dst->SetEngine()`）前面有 `R3RIsCarOnlyFormation()` 把关，要求 `railveh_type == Wagon`，真·双头后半节的 `engine_type` 是引擎，进不来；
  - 连挂路径里的 `merged_first->SetSegmentFront()` / `seg_tail->SetSegmentBack()` 只打 ★ 边界、不动 subtype 位。
- **修复后不变量**：`IsMultiheaded() && IsEngine()` 只属于双头前半节；后半节恒为 `IsRearDualheaded()`。段尾升格对双头后半节为 no-op（★ 边界照旧）。
- **编译证据（增量，`EXIT_CODE=0`）**：只改 `src/train_cmd.cpp` + `src/vehicle_cmd.cpp`、无 `src/*.h` 变更 ⇒ 按 KI-15 / 记忆 66636022 增量合法。日志 `build\R3R_incbuild.log` 末行 `[4/4] Linking CXX executable openttd.exe`；标记 `build\R3R_incbuild.done = EXIT_CODE=0`；产物 `build\openttd.exe` @ **2026-09-21 00:18:56**（50 791 936 B）。`read_lints` 两文件零诊断。
- **状态 = 部分防护（编译通过 ≠ 实测通过）**。待游戏内复测：① 场景 A（双头机车链升为段后，在车库再搬入别的车）不再冻结；② 场景 B（双头机车链参与连挂）不再冻结，且连挂结果里双头两半仍相邻、后半节无 FRONT 位；③ 段尾恰为双头后半节时该段仍能正常降级（`ClearSegmentTailFakeEngine()` 因 `railveh_type != Wagon` 早退，本来就不会误清，行为不变）；④ 普通单头机车链 / 纯车厢链升段与连挂无回归。
- **登记（未解，**不是本轮缺陷**，但双头 + 段还有更深的设计问题）**：段内彻底倒序（第 77 轮 KI-141 口径）会把「前半节 … 后半节」翻成「后半节 … 前半节」，即把 `IsRearDualheaded()` 的车顶到段首 —— 而 vanilla / R3R 都硬性规定后半节不能带头（`STR_ERROR_REAR_ENGINE_FOLLOW_FRONT`）。**「双头机车跨段」是否应当禁止升段，或升段时只允许整段翻转后仍由前半节带头，需要玩家拍板**，本轮不处理。
- 备注：本次两场景的坏状态只存在于命令执行过程中（`MakeSegment` / `ArrangeTrains` 当场卡死，从未落盘），故**无需对旧存档做修复**。

## 附：第 79 轮（2026-09-21）：段的自动吸附 / 双头段「拖不出来」/ 倒车机制澄清

> 计划全文见工作区 `R3R_segment_autocouple_dualhead_plan.md`（含两种修复入口的逐步推演与 T1~T5 复测用例）。

### KI-144（已实现 / 已编译 / 待实测）：已成形段被 depot 每 tick 钩子自动吸收自由车厢链（新购车厢被并进段）

- 来源：第 79 轮玩家问题一；机制定位见计划文档 §1 / §2。
- 机制链：`CmdBuildRailWagon`（`src/train_cmd.cpp` ≈1659-1679）只把新车厢并进 `IsFreeWagon()` 链 ⇒ 购买瞬间不进段；真正入口是 `TrainLocoHandler` 的 `NormalizeTrainVehInDepot(consist, true)`（`src/train_cmd.cpp:10949-10951`，条件 `track == TRACK_BIT_DEPOT && consist->IsEngine() && !current_order.IsType(OT_GOTO_COUPLE)`）——段头即使是假引擎（`R3RCreateCarOnlyFormation` 打的）`IsEngine()` 也为真，于是**段每 tick 主动吞掉同 tile 的自由车厢链 / FrontWagon 链**（`NormalizeTrainVehInDepot`，1691-1725）。且吸收落点是 `MoveRailVehicle(v, u)`（dst = 链头 u）⇒ `InsertInConsist(dst=u, src=v)`（`src/train_cmd.cpp:2371`）把车厢插在**段首正后方**，不是链尾。
- 修复（已落地，纯 `.cpp` ⇒ 增量合法）：`src/train_cmd.cpp` 新增文件内静态 `R3RChainHasSegment()`（全链扫 `IsSegmentFront()`，现 `1708-1714`）+ `NormalizeTrainVehInDepot()` 入口守卫 `if (R3RChainHasSegment(u)) { …; return; }`（现 `1728-1731`，同时覆盖 tick 钩子与建车 `vehicle_cmd.cpp:217` 两个调用点；后者 u 是新造机车无 ★，不受影响）。不改 `CmdBuildRailWagon` 的自由车厢自动连挂、不改非段列车的原生吸收行为。
- 探针（KI-14 口径，边沿触发）：守卫命中即写 `NOABSORB-SEG veh=<段链头> n=<同 tile 自由车厢节数> len=<段链长>`；edge tag `R3REDGE_NOABSORBSEG`，键 = 链头车号、payload = 自由车厢节数 ⇒ 段在 depot 里连站多 tick 也只出一行（emitter `R3RProbeNoAbsorbSeg()`，`src/train_cmd.cpp` ≈4428）。
- 状态：**已实现 / 已编译 / 待实测**。严重度：中（行为不符；段被灌入非成员车辆后还会引发 KI-145）。

### KI-145（已实现 / 已编译 / 待实测；**其 P2-B 已于第 82 轮撤回，见 KI-152**）：双头机车所在段，段后车厢被夹进 ★..SegmentBack ⇒ depot 判为段内、单独拖不出来

- 来源：第 79 轮玩家问题二；机制定位见计划文档 §1 / §3。
- 机制链：`NormaliseDualHeads()`（`src/train_cmd.cpp:2079-2105`，除 KI-143 守卫外为上游原样）把后半节搬到「前半节之后、**下一个引擎之前的最后一辆**」之后；`[A★, B(SB), C]` 因此变 `[A★, C, B(SB)]`，C 被夹在 ★ 与 `SegmentBack` 之间；而 `TrainDepotGetSegmentFront()`（`src/depot_gui.cpp:160-172`）只按 ★/SB 回溯判归属 ⇒ C 判段内 ⇒ `TrainDepotMoveVehicle()`（`src/depot_gui.cpp:257-264`）改走 `TrainDepotMoveSegment` 搬整段 ⇒ 拖不出来（玩家图例 `[《——ABC——》]`）。
  - 实测推演：`ArrangeTrains()` 末尾无条件跑 `NormaliseDualHeads(*src_head); NormaliseDualHeads(*dst_head);`（2379-2380），故「玩家拖动」与「tick 钩子吸收」两条入口都会触发同一夹心。
- 修复（已落地，纯 `.cpp` ⇒ 增量合法）：
  - **P2-A（主修）** `NormaliseDualHeads()` 内新增 `R3RIsInsideSegment()`（沿 `Previous()` 先遇 ★ 为段内、先跨 `SegmentBack` 为段外），段内一律改为 `RemoveFromConsist(other) + InsertInConsist(t, other)` 紧贴前半节。覆盖「拖到段后」（`[A★,B(SB),C]` no-op，C 留在 SB 之后）与「拖到两半之间」（`[A★,C,B(SB)]` → B 上移 → `[A★,B(SB),C]`，C 被挤出段外）两种入口；非段双头保持上游行为。
  - **P2-B（防御，治已落盘旧档）** `TrainDepotGetSegmentFront()` 增补特例：被夹在两半之间的车（`Previous()` 是双头前半节且其 `other_multiheaded_part == Next()`）判段外 ⇒ 旧档 `[《——ABC——》]` 也能拖出 C，并借拖动触发的 `ArrangeTrains` 就地自愈。
- 探针（KI-14 口径，边沿触发）：P2-A 命中即写 `NDH-SEG-KEEP front=<前半节> rear=<后半节> moved=<0/1>`；edge tag `R3REDGE_NDHSEGKEEP`，键 = 两半节车号、payload = 是否真的搬过（`moved=1` 正是「夹心车被挤出段外」那一次）（emitter `R3RProbeNdhSegKeep()`，`src/train_cmd.cpp` ≈4451）。
- 已知限制：更深的夹心（`A, x, C, y, B`）在 P2-B 下仍判段内；段边界三套口径（`GetLastChainVehicle` / 「到下一个 ★ 或链尾」/ 翻转时「块首块尾」+ depot 缺 SB 时回退链尾）的收敛列为 **P2-C，本轮不做**。
- 待复测：T1（三种段旁买车厢不被吸收）、T2（拖到段后可拖出）、T3（拖进两半之间自动挤出）、T4（旧坏档自愈）、T5（非段双头 / 普通机车捡车 / 升降段 / 连挂解挂无回归，含 GRF 选图与打散组角色位目测）。
- 状态：**已实现 / 已编译 / 待实测**。严重度：中（行为不符，玩家无法单独拆出车厢）。

### 构建（第 79 轮）
- 改动文件：`src/train_cmd.cpp`（P1 helper + 入口守卫 + `NOABSORB-SEG` 探针；P2-A + `NDH-SEG-KEEP` 探针；`R3RDbgEdgeTag` 两个新 tag；两个探针 emitter 的前置声明与 `R3R_PROBES` 门控宏）、`src/depot_gui.cpp`（P2-B 特例）。
- **无 `src/*.h` 变更**（`R3RDbgEdgeTag` 枚举本就定义在 `src/train_cmd.cpp` 内，未动 `src/r3r_perf.h`）⇒ 按 KI-15 / 记忆 66636022 增量构建合法，无需删 `*.obj`。
- 编译证据（增量，`EXIT_CODE=0`）：`build\R3R_incbuild.log` 末行 `[3/3] Linking CXX executable openttd.exe`；标记 `build\R3R_incbuild.done = EXIT_CODE=0`；时间戳 `src/train_cmd.cpp` 02:10:53 < `train_cmd.cpp.obj` 02:14:01 < `build\openttd.exe` **2026-09-21 02:16:50**（50 792 960 B）。`read_lints` `src/train_cmd.cpp` 零诊断。
- 探针可裁剪性：调用点走 `R3R_PROBE_NOABSORB_SEG` / `R3R_PROBE_NDH_SEG_KEEP` 宏，`R3R_PROBES=0` 时展开为 `((void)0)`（`#if` 分支里连 emitter 定义一起消失），发布版仍是可证明「无探针」（2026-09-19 release audit 口径）。
- **状态 = 已编译（编译通过 ≠ 实测通过）**。待游戏内按计划 §6 跑 T1~T5；实测通过前不得按「已修」对外表述。

### KI-146（已搁置）：双头机车跨段（两半节分居不同段）/ 段内彻底倒序后由后半节带头

- 来源：玩家 2026-09-21 拍板；同时结案 KI-143 末尾「是否应当禁止升段 / 只允许整段翻转后仍由前半节带头，需要玩家拍板」那一项。
- 口径：游戏不需要「双头机车两头分居不同段」的能力 ⇒ 本轮起**不提供支持**，也不再开发校验 / 拒绝逻辑；段内彻底倒序把后半节顶到段首（KI-141 / KI-143 口径）本轮不动。若日后要兜底，落点 = 升段 / 翻转后检测新段首是否 `IsRearDualheaded()`，命中即回滚（`STR_ERROR_REAR_ENGINE_FOLLOW_FRONT`）。
- 状态：已搁置。严重度：低（畸形场景，且无落盘途径）。

### 本轮澄清（非缺陷，只记录口径）：倒车与「引擎位」无关

- `IsEngine()`（subtype 的 `GVSF_ENGINE` 位）与「能否带队（cab）」是两个概念：`Train::CanLeadTrain()`（`src/train_cmd.cpp:12580-12591`）对 `IsRearDualheaded()` 直接返回 true，NewGRF `ExtraEngineFlag::HasCab` 是给无动力车厢带队用的第二条路径 ⇒ **双头后半节天然可带队**。
- 倒车 = `DrivingBackwards`：`ReverseTrainDirection()` 的分支（`src/train_cmd.cpp:3499`）在「已置 `DrivingBackwards` / `TrainFlipReversingAllowed::None` / `consist->Last()->CanLeadTrain()`」三选一时选择倒退，只把方向解释取反（`SetMovingDirection()`，`src/train.h:680`：`direction = IsDrivingBackwards() ? ReverseDir(d) : d`），链序与链头不变、动力仍由全链引擎（含双头前半节）提供；`None` 时连车辆视图掉头按钮的 `force_end_swap` 也降级为倒退（3499 上方 KI-127 注释）。物理掉头分支才是 `ReverseTrainSwapVehicles(consist)`（3496）。
- ⇒ 第 78 轮 KI-143「禁止给后半节引擎位」不影响倒车能力，只影响 `NormaliseDualHeads()` 的稳定性判定。

## 附：第 80 轮（2026-09-21）：解挂后脱离方向 / 段耦合被卖后编号泄漏 / 耦合后 30 km/h 慢行（KI-147~KI-149）

本轮三个问题均由玩家报告；改动集中在 `src/train_cmd.cpp` 与 `src/vehicle.cpp`（**未动任何 `src/*.h`**，按 KI-15 / 记忆 66636022 增量构建合法）。

### KI-147（已修，待实测）：编号池 ID 泄漏 —— 两个段耦合着被卖出后列车编号持续上涨

- 来源：玩家 2026-09-21 报告。朋友用 WIN10 跑最近提交源码的发行版时编号一路涨到 65500；常态下也有小额泄漏，触发场景＝「两个段耦合着被一起卖掉」。
- 根因（三条互相独立的泄漏路径，全部落在 R3R 的 `unitnumber_backup` 交换机制上）：
  1. `DecoupleTrain()` 解挂还原块（`train_cmd.cpp`）：车厢侧 `u` 在此之前刚通过 `GetFreeUnitNumber()` + `UseID()` 取了**全新**编号，随后还原块把 `u->unitnumber` 覆写成「从机车手里拿回来的原编号」⇒ 那个新编号再没有任何车辆持有，也永远不会被 `ReleaseID()`。**每次解挂泄漏 1 个**（与 4963-4964 旧注释「harmless, 未加 undo」同源）。
  2. `Couple()` 合并处（`train_cmd.cpp`）：`unitnumber_backup` 是单层槽位。「挂 A 不解挂又挂 B」时 A 的编号被 B 的编号直接覆盖，A 占的池位成为孤儿。**每次重复挂车泄漏 1 个**。
  3. 被覆盖/被丢弃的备份号所属列车若随后被删除，`Vehicle::PreDestructor()` 只 `ReleaseID(this->unitnumber)`（活号），备份号无人释放 ⇒「两段耦合着被卖出」正是同时命中 1+3 的场景，泄漏量随耦合/解挂次数累积。
- 修法（`src/vehicle.cpp` + `src/train_cmd.cpp`，全部是「先验条件再释放」的保守写法，绝不动活号）：
  - `Vehicle::PreDestructor()`：仅当 `type == Train && IsFrontEngine() && unitnumber_backup != 0 && unitnumber_backup != unitnumber` 时释放备份号。非链头车存的是链头号的副本（不是池位借出号），故必须加 `IsFrontEngine()` 与「不等于活号」双保险，否则会误释放别人的号。
  - `DecoupleTrain()` 还原块：先把车厢侧那个刚分配、即将被丢弃的新号 `ReleaseID` 回池（要求 `u->unitnumber != 0 && u->unitnumber != v->unitnumber`），再把 `u->unitnumber_backup` 清零（该副本同样构成对同一池位的第二次引用）。
  - `Couple()`：覆盖 `unitnumber_backup` 之前，若槽位里已有一个「不同于活号」的旧备份号，先 `ReleaseID` 再清零。
- 状态＝已修（编译通过；**未实测**）。实测点：① 造两段耦合的列车 → 车库「卖光本库车辆」→ 看后续新车编号是否连续、池位是否回收；② 机车连续挂两次（中途不解挂）再卖出 → 编号应回池；③ 解挂↔耦合循环若干次后，编号不应单调上涨。
- 严重度：中（不崩溃，但长局内编号错乱/耗尽）。
- 残留：`unitnumber_backup` 单层槽位本身仍是「挂两次丢一次排程/编号」的结构性问题（记忆 35644846 ①），本轮只堵住泄漏，未改成栈。

### KI-148（已修，待实测）：耦合后被套上「闯过信号灯之后」的 30 km/h 慢行状态，直到下一个信号灯才解除

- 来源：玩家 2026-09-21 报告。
- 根因：`Couple()` 在合并链头身份改写处显式 `v->lookahead.reset()`（原意＝丢掉机车自己那份「指向耦合前所在位置」的陈旧前瞻，避免跟随预留时把 pathfind 起点认到旧终点，见 REVERSEDIR 系列旧记录）。但 `lookahead == nullptr` 在真实制动模式下恰好等价于「刚闯过红灯」：`Train::GetCurrentMaxSpeedInfoInternal()` 在 `UsingRealisticBraking() && lookahead == nullptr` 时执行 `advisory_max_speed = std::min(advisory_max_speed, 30)`，列车被限 30 km/h。而 `FillTrainReservationLookAhead()` 只在信号/道口检查（如 `IsTooCloseBehindTrain()`）里被调用，正常区间行驶不触发 ⇒ 直到下一个信号灯才重建前瞻、限速才消失，与玩家描述完全一致。
- 修法：`Couple()` 提交段（`CheckReverseTrain()` 之后、`MarkDirty()` 之前）在 `TBM_REALISTIC` 下**立刻重建**前瞻：`v->lookahead.reset(); FillTrainReservationLookAhead(v);`。位置选在 `R3RRespaceChainAfterEdit` / 方向归一 / `DrivingBackwards` 清理之后，`v` 的 moving front 已是合并后的正确链头；重建（而非保留旧的）同时保住了原 reset 的意图。
- 状态＝已修（编译通过；**未实测**）。实测点：耦合成功后车速表上限应立刻是机车正常限速而非 30；下一个信号灯前后上限不应有跳变。
- 副作用评估：若耦合当下前方尚无预留（下一 tick `ProcessOrders` 才预留），此刻重建出的前瞻会「以预留末端为终点」，但列车此时本就停着（`cur_speed == 0`，下一 tick `TryPathReserve` 会再次重建），不产生新的限速死锁。

### KI-149（已修 rev.2，待实测）：解挂后未限制脱离方向 —— 机车原地掉头会撞上刚解下的等待车底

- 来源：玩家 2026-09-21 报告：「需要限制列车解挂之后的列车脱离方向，否则会导致列车碰撞」。
- 根因：解挂把尾部分离后，被解下的部分（`u`）就地声明 `OT_WAIT_COUPLE` 停在**机车正后方的同一股道**上（车钩缝隙 1~2 px）。同一 tick 内 `TrainLocoHandler` 的 `DECOUPLE` 钩子之后紧接 `ProcessOrders()` + `CheckReverseTrain()`：若机车下一条命令指向前进方向的反面（终点站/枢纽掉头，如 T8701 同登回南宁），`ReverseTrainDirection()` 会把机车掉头**沿原股道倒着开回去**。`CheckReverseTrain()` / `YapfTrainCheckReverse` 只判轨道与信号，不判停在轨道上的车辆 ⇒ 直接撞上刚解下的部分。
- 修法（`src/train_cmd.cpp`，纯世界状态判据，无新增字段、无存档影响；**rev.2 按玩家 2026-09-21 口径改写为「缝＝双向不可通行」**）：新增文件内 `enum class R3RSeamEnd { None, Front, Back }` 与 `R3RWaitingCoupleAtEnd(const Train *v, bool at_front)`（原 `R3RWaitingCoupleBehind` 的参数化推广）—— 扫描 `Train::IterateFrontOnly()`，认定「另一条链 + 非 crash/非 virtual + `current_order` 为 `OT_WAIT_COUPLE` + 其任一端（moving front / moving back）在 z 方向 ±5 内 + 位于我该端出口方向的**外侧**（`TrackdirToExitdir(端 trackdir；尾端先 ReverseTrackdir)` 与位移点积 > 0）+ 距离已到贴合（`(len_a+1)/2+(len_b+1)/2` 再放 2 px）」＝「刚解下的等待车底正贴着我这一端」，由 `R3RWaitingCoupleSeam(v)` 依次测尾端/鼻端返回 `Back` / `Front`。
- 口径与落点（两条对称规则，缝对两侧各自都是「不可通行的路」）：**缝在车尾 ⇒ 不许掉头**（`TrainLocoHandler` 自动掉头判据追加 `R3RWaitingCoupleSeam(consist) != R3RSeamEnd::Back`——掉头之后正是朝车尾方向倒着开进缝里）；**缝在车头 ⇒ 不许向前预留进路**（出站预留分支 `flags.Test(VehicleRailFlag::LeavingStation)` 追加 `R3RWaitingCoupleSeam(consist) != R3RSeamEnd::Front`——被解下的部分贴住鼻端时不得规划穿过它）。两条都只是把该方向从「可走」改成「被占」，列车因此改走站台另一端出站，而不是原地等待。
- 明确口径（避免误解）：判据只依赖「贴合 + 位于该端外侧 + 对方在等挂」，不写任何 R3R 新标记，故等待车底被挂走/拖离后下一 tick 自动恢复；若**两端同时**被等待车底贴住则前后都不许动（物理上确实无法通过），需人工把其中一侧挂走。
- 状态＝已修（rev.2；编译通过；**未实测**）。实测点：① 终点站解挂 → 下一命令指回程 → 机车不得原地掉头穿过等待车底；② 等待车底被另一台机车挂走后的下一 tick，原机车应能正常掉头出发；③ 同站台邻股道另有一列 `WAIT_COUPLE` 车底时，不得误拦掉头（方向点积 + 贴合距离双重过滤）；④ 解挂后若**前方**另有等待车底贴住鼻端，列车不得向其预留进路（应改走另一端或先反转离开）；⑤ 探针 `KI149-SEAM veh=<n> seam=front|back other=<n> tile=<x>,<y>` 在 `R3R_debug.log` 中每次状态变化只出现一行（`R3REDGE_KI149SEAM`，边沿触发，KI-14 口径）。
- 副作用评估：仅 `TrainLocoHandler` 的掉头判据与出站预留分支两处（`Couple()` 内对 `CheckReverseTrain()` 的调用未改），不影响耦合成功后的 `Reversing` 标志语义；探针在 `R3R_PROBES=0` 构建下整条消失。

### 构建（第 80 轮）

- 改动文件：`src/train_cmd.cpp`、`src/vehicle.cpp`（均非 `src/*.h`）⇒ 增量构建合法，无需删 `*.obj`。
- 编译结果（2026-09-21 03:08）：增量构建通过 —— `build\R3R_incbuild.done` = `EXIT_CODE=0`，`build\R3R_incbuild.log` 末行 `[3/3] Linking CXX executable openttd.exe`；时间戳链 `train_cmd.cpp.obj` 03:04:33 → `build\openttd.exe` **03:08:40**（50 792 960 B）；`read_lints`（train_cmd.cpp / vehicle.cpp）零诊断。
- 首轮编译失败一次并已修正：`DIAGDIR_NE/SE/SW` 在本仓是**限定枚举**（必须写 `DiagDirection::NE` 等），且 `TrackdirToExitdir()` 对 `INVALID_TRACKDIR` 会断言（KI-108 家族）⇒ 缝判据里先判该端 `trackdir == INVALID_TRACKDIR` 再解析 exitdir（rev.2 保留）。
- rev.2 追加（2026-09-21，同一轮内）：`R3RWaitingCoupleBehind` → `R3RSeamEnd` + `R3RWaitingCoupleAtEnd` + `R3RWaitingCoupleSeam`，新增鼻端规则的出站预留第二落点，新增边沿触发探针 `KI149-SEAM`（tag `R3REDGE_KI149SEAM`，`R3R_PROBES=0` 下整条消失）；仅 `src/train_cmd.cpp`，增量构建合法。
- rev.2 编译结果（2026-09-21 03:36）：增量构建通过 —— `build\R3R_incbuild.done` = `EXIT_CODE=0`；时间戳链 `train_cmd.cpp.obj` 03:32:23 → `build\openttd.exe` **03:36:16**（50 792 960 B）；`read_lints`（train_cmd.cpp）零诊断；`grep` 确认无遗留旧名 `R3RWaitingCoupleBehind`。
- **编译通过 ≠ 实测通过**：KI-147 / KI-148 / KI-149 的三组实测点（见各条）全部待游戏内复测，尤其 KI-149 需确认「不误拦正常掉头」。

## 附：第 81 轮（2026-09-21）：KI-147 rev.3 —— 编号池仍泄漏（三条遗漏路径补齐 + 三条诊断探针）

- 来源：玩家 2026-09-21 复测「卖出耦合的 1 号、2 号列车后，再买车从 3 号起」⇒ 第 80 轮的三条释放路径没覆盖全部泄漏点。
- 本轮定位到另外三条，共同点＝「号码所属车辆**即将失去 front 身份**」的那一刻：一旦身份被剥离，`Vehicle::PreDestructor()` 的备份回收（只对链头生效）就再也轮不到它，池位永久泄漏。第 80 轮只覆盖了「有车号在 `unitnumber_backup` 里被覆盖/被丢弃」的情形，漏了「活号被直接抹掉」和「备份号滞留在降级车身上」两类。

### rev.3 修法（仅 `src/train_cmd.cpp` / `src/vehicle_cmd.cpp` / `src/vehicle.cpp`，未动任何 `src/*.h`）

1. `Couple()` 收尾（`train_cmd.cpp`，约 6763-6772）：`u->unitnumber = 0;` 之前，若 `u->unitnumber != 0 && u->unitnumber != v->unitnumber` 则先 `ReleaseID`。第 80 轮只处理了「`unitnumber_backup` 被覆盖」，漏了「被挂上的链自身没有排程（`u->orders == nullptr`），或排程挂在同链别的段上（`couple_owner != u`）导致 orders 交接整块没跑」时，`u` 的车号被直接抹掉这条路径。
2. `R3RRelocateFrontIdentity()`（`train_cmd.cpp`，约 5963-5972）：换端重排把主车身份迁往新链头时，`unitnumber_backup` 一并搬走（`to->unitnumber_backup = from->unitnumber_backup; from->unitnumber_backup = 0;`），并先把 `to` 自带的旧备份（若不同于新活号）归还池。原先只搬 orders / 各 index / 活号，备份号滞留在 `from` 上，而 `from` 随后被清掉 front 位降为链内普通车 ⇒ 永久泄漏。
3. 降级路径（`vehicle_cmd.cpp`，`CmdDemoteSegment` 的纯车厢段一键解散处，约 712 / 745）：释放活号之后再释放 `unitnumber_backup`（要求非 0 且不同于活号）。
4. `Vehicle::PreDestructor()`（`vehicle.cpp`，约 1199）：备份回收的前置条件由 `IsFrontEngine()` 放宽为 `IsPrimaryVehicle()`，把「车厢链头（FrontWagon 假引擎）」一并覆盖。
5. 诊断探针（`R3R_PROBES=0` 下整条消失）：
   - `UNIT-NEW id=<n> type=<t> owner=<c> free_next=<n>` —— `CmdBuildVehicle` 成功购车后落一行，`free_next` 是池子下次会分到的号。
   - `UNIT-DROP id=<n> head_id=<n>` —— 修法 1 那条丢弃路径命中时落一行。
   - `UNIT-RELBK bk=<n> id=<n>` —— `PreDestructor` 回收备份号时落一行。

### 状态与实测点（第 81 轮）

- 状态＝已修（对象编译通过；**未全量重编、未实测**）。严重度：中（不崩溃，长局内编号错乱/耗尽）。
- 编译校验：临时脚本只编 `train_cmd.cpp.obj` / `vehicle.cpp.obj` / `vehicle_cmd.cpp.obj` 三个对象、不链接 ⇒ 首轮抓到一个真实错误（`(uint)v->owner`：`Owner` 是强类型，必须写 `v->owner.base()`，已修），第二轮 `NINJA_EXIT=0`。
- 实测点：① 卖光耦合链后买新车，看 `R3R_debug.log` 的 `UNIT-NEW ... free_next=1`；② 若仍不是 1，用 `UNIT-DROP` / `UNIT-RELBK` 两行核对是哪条路径没回收；③ 车库「卖光本库车辆」、换端重排后卖出、纯车厢段解散三条支线各复测一次。
- 残留：`unitnumber_backup` 单层槽位的结构性问题（记忆 35644846 ①）仍未改成栈。

### 构建方式变更：脱离编辑器 / CodeBuddy 的全量重编（第 81 轮）

- 新增 `R3R_fullbuild.cmd`（**纯 ASCII**：cmd.exe 在 936 代码页下读 UTF-8 中文 .cmd 会从行中间执行）与双击入口 `R3R_fullbuild_detach.cmd`。`R3R_fullbuild.cmd detach` 用 `Start-Process -WindowStyle Minimized` 另开一个独立窗口跑构建，父窗口 / 编辑器 / CodeBuddy 关掉都不影响；日志 `build\R3R_fullbuild.log`、结论 `build\R3R_fullbuild.done`（`EXIT_CODE=<n>`）。
- 闸门：`build\build.ninja` 缺失=98、`openttd.exe` 在跑（链接会 LNK1168）=97、`build.ninja` 带 `-DR3R_PROBES_DEFAULT=0`（发行树）=95、`vcvars64.bat` 失败=96；`-j2`、`TEMP/TMP` 指向 `build\tmp`（8 GB 机器 + C 盘紧张，KI-24）。全量删 `*.obj` 后重建，符合 KI-15。
- 旧 `_tmp_full_build.cmd` / `R3R_fullrebuild.cmd`（菜单式、结尾 `pause`）保留不动。

## 附：第 82 轮（2026-09-22）：三处崩溃 / 拖拽修复（KI-150~KI-152，并撤回 KI-145 的 P2-B）

- 来源：玩家 2026-09-21 实机复测第 79~81 轮成果后提交的问题清单（本轮处理其中第 一 / 四 / 六 项）；崩溃现场 = `Documents\OpenTTD\crash-20260921T092110Z.log`（depot 断言）、`crash-20260921T095822Z.log` 与 `crash-20260921T100031Z.log`（pbs 断言，同一现场）。
- 只改 3 个 `.cpp`（`src/vehicle_gui.cpp`、`src/depot_gui.cpp`、`src/pathfinder/yapf/yapf_rail.cpp`），未动任何 `src/*.h` ⇒ 增量构建合法（KI-15）。
- 构建：`_tmp_inc_build.cmd` ⇒ `build\R3R_incbuild.done` = `EXIT_CODE=0`，日志末行 `[5/5] Linking CXX executable openttd.exe`；时间戳链 `yapf_rail.cpp` 00:48:29 / `depot_gui.cpp` 00:49:36 / `vehicle_gui.cpp` 00:53:29 → `build\openttd.exe` **2026-09-22 00:59:25**（50 799 104 B）。`read_lints` 三文件零诊断。
- **编译通过 ≠ 实测通过**：下面三条的实测点全部待在游戏内复测。

### KI-150（已修，待实测）：车库里点「非链头车辆」打开车辆视图 ⇒ `vehicle_base.h:669` 断言崩溃

- 描述：`DepotWindow` 车辆列表在「散车分行显示」时会把**散车链的非链头车厢**当作独立行（`BuildDepotVehicleList()`，`src/vehiclelist.cpp`）。点这类行的 R3R 链态标签列会走 `DepotGUIAction::ShowVehicle`（`src/depot_gui.cpp:795`），双击则走 `WID_D_MATRIX` 分支，两者都把该车厢直接交给 `ShowVehicleViewWindow()` ⇒ 视图窗口对「非链头」调用 `IsStoppedInDepot()`，命中 `assert(this == this->First())`（`vehicle_base.h:669`）。栈：`IsStoppedInDepot` ← `VehicleViewWindow::UpdatePlanes` ← `VehicleViewWindow` ← `ShowVehicleViewWindow` ← `DepotWindow::OnClick`（`depot_gui.cpp:1099`）。
- 来源：崩溃日志 `crash-20260921T092110Z.log`（`Version: r3r-stable-2026-09-19-m (2)`，`difficulty.train_flip_reverse_allowed: none`）。
- 修法（集中防御 + 车库侧拦截）：
  1. `ShowVehicleViewWindow()`（`src/vehicle_gui.cpp`，约 4910）：对 Train 先 `v = v->First()` 再开窗 —— 与仓库既有约定 `VehicleClicked()`（`src/vehicle_gui.cpp:4937`）一致，从根上消除「车辆视图开在链内车辆上」这一整类崩溃，任何调用点都受保护。
  2. `src/depot_gui.cpp`：`DepotGUIAction::ShowVehicle` 分支与 `WID_D_MATRIX` 双击分支在调用前加 `result.vehicle->First() != result.vehicle` 拦截（双击分支保留既有 `IsFreeWagon()` 拦截）⇒ 非链头车厢行既不打开视图、也不再触发 `DepotClick`。
- 状态＝已修（已编译）。严重度：高（点一下就崩）。
- 实测点：① 「散车分行显示」下点中间那节车厢的链态标签列 ⇒ 不崩、无视图窗口；② 双击该行 ⇒ 不崩；③ 正常机车 / 段链头 / 多节真引擎 打开视图仍正常。

### KI-151（已修，待实测）：耦合寻路沿「平台轴」扫描到非轨道格仍预留轨道 ⇒ `pbs.cpp:145` 断言崩溃

- 描述：`FindNearestCoupleTrain()` 的 `rp` lambda 在 `IsRailStationTile(f3.new_tile)` 判定里，会沿平台轴正 / 反两个方向走到 `tg`（待挂车底的记录头格），并把**沿途每个格子**按 `plat_track = AxisToTrack(axis)` 预留。当该轴线上存在**不属于本平台的格子**（现场 `tile: 1C3C (60 x 56), type: 00 (Clear)`，纯地形、无轨道）时，`TryReserveRailTrack()` 的断言 `(TrackdirBitsToTrackBits(GetTileTrackdirBits(tile, TRANSPORT_RAIL, 0)) & TrackToTrackBits(track)) != 0` 失败 ⇒ 崩。栈：`yapf_rail.cpp:924`（反向平台扫描的 `TryReserveRailTrack(p, plat_track)`）← `yapf_rail.cpp:933`（单出口递归）← `FindNearestCoupleTrain()`（第 72 轮起 R3R 新增的耦合寻路）。
- 来源：崩溃日志 `crash-20260921T095822Z.log` / `crash-20260921T100031Z.log`（`veh: 7`，`tile: 1B3A (58 x 54)` Railway）。
- 修法（`src/pathfinder/yapf/yapf_rail.cpp`）：lambda 内新增 `reserve_if_present(tile, track)` —— 先取该格 `GetTileTrackdirBits(..., TRANSPORT_RAIL, 0)` 转 `TrackBits`，**只有确实含该轨道时才预留**；四处预留（正向平台扫描、反向平台扫描、单出口回溯自身、多出口回溯自身）全部改走它。只修「预留不存在的轨道会断言」这一条，**不改任何可达性 / 路径判定**（`if (tt == tg) { ... return true; }` 语义不变）⇒ 不回归正常耦合路径。
- 状态＝已修（已编译）。严重度：高（断言崩溃）。
- 实测点：① 复跑现场场景（机车从 58,54 出发、目标车底记录头格 60,56 一带，`train_flip_reverse_allowed: none`）⇒ 不再崩，机车照常耦合或安全放弃并在下 tick 重试；② 常规站台耦合（机车与车底同一平台）⇒ 预留行为与修复前一致。
- 残留（未修，另记）：该崩溃暴露的是「`consist_head_tile` 可能落在**另一条平台 / 已是纯地形**的格子上，而平台轴直连启发式仍把 `tt == tg` 当命中」这一**误判**，本轮只做安全预留、未收紧判据。若复测出现「机车朝不可能方向反复尝试耦合」，则把该分支命中条件收紧为「沿途格子必须都带 `plat_track`」。

### KI-152（已修，待实测）：双头机车段内的夹心车厢被允许单独拖出 ⇒ 段被从内部撕开（**撤回 KI-145 的 P2-B**）

- 描述：第 79 轮 KI-145 的 P2-B 在 `TrainDepotGetSegmentFront()` 里加了「`[A★, C, B(SB)]` 形态下把 C 判为段外」的逃生口，使 C 可被单独拖出。玩家复测认定**这是错的**：夹在双头两半节之间的车厢**不应**能单独拖出，那等于把段从内部撕开（后 / 前半节被拆到两条不同的拖动结果里）。⇒ 第 79 轮「段后车厢被夹住、拖不出来」的抱怨（KI-145）与第 82 轮「夹心车厢能被拖出」的抱怨方向相反，以本轮为准。
- 来源：玩家 2026-09-22 问题清单第（四）项；被撤回项 ＝ KI-145（第 79 轮）/ `R3R_segment_autocouple_dualhead_plan.md` §3.2 的 P2-B 与 §6 的 T2/T3/T4 期望。
- 修法（`src/depot_gui.cpp`，`TrainDepotGetSegmentFront()`）：删除 P2-B 特例（含 `other_multiheaded_part == v->Next()` 判据的提前 `return nullptr`），改为注释说明「★..SegmentBack 之间的车辆是**正常段成员**，受整体拖动限制」。P2-A（`NormaliseDualHeads()` 段内紧贴，`src/train_cmd.cpp`）保留不动。
- 语义后果（**有意为之**）：`[A★, C, B(SB)]` 里拖 C ＝ 拖整段；整段拖动后 `ArrangeTrains` → `NormaliseDualHeads()` 把后半节上移贴前半节 ⇒ 链变 `[A★, B(SB), C]`，C 落到段外，随后即可单独拖出。⇒ 旧存档的坏布局仍能自愈，只是需要**先整段拖一次**。
- 状态＝已修（已编译）。严重度：中（行为不符：段被从内部撕开）。
- 实测点：① `[《——ABC——》]`（含已落盘旧档）里拖 C ⇒ 整段一起动、C 不脱离；② 整段拖到空行 ⇒ 链变 `[《——AB——》], C` 且 C 可单独拖出；③ 正常（无双头）段的单独拖出行为不变。
- 已知限制（沿用 KI-145）：若 C 被夹得更深（`A, x, C, y, B`），P2-A 仍会把 B 上移、C 保持被夹（P2-C 未做）。

### 本轮未完成

- 玩家清单中除第 一 / 四 / 六 项外的其余编号项，在本次会话上下文压缩后**已不可考**；且 `Documents\OpenTTD` 下 09-21 之后只有上述三个崩溃日志（无「换向按钮」相关崩溃证据）⇒ 剩余项的排查需玩家重新列出后继续。

## 附：第 83 轮（2026-09-22）：车库「0 号车」/ 解耦再耦合卡死 / SKIP-STOPPED 诊断 / 清除 force-flip（KI-153~KI-155）

- 来源：玩家 2026-09-22 确认的第 83 轮工作项清单（四项**并行推进**，不等第 82 轮 KI-150~152 的实测结果）。
- 改动文件（12 个）：`src/train_cmd.cpp`、`src/train.h`、`src/train_cmd.h`、`src/vehicle_cmd.cpp`、`src/autoreplace_cmd.cpp`、`src/depot_gui.cpp`、`src/vehicle_gui.cpp`、`src/vehicle.cpp`、`src/order_cmd.cpp`、`src/tbtr_template_vehicle_func.cpp`、`src/tbtr_template_gui_create.cpp`、`src/script/api/script_vehicle.cpp`。
- **因为动了 `src/train.h` + `src/train_cmd.h`，按 KI-15 必须全量重编**（本机 ninja `deps=msvc` 解析不到中文 `/showIncludes` 前缀，改头文件不会触发依赖重编，陈旧 obj 会链接成混合布局二进制 ⇒ 随机崩溃）。构建方式：`R3R_fullbuild.cmd detach`（独立最小化窗口，编辑器关掉也继续），日志 `build\R3R_fullbuild.log`、结论 `build\R3R_fullbuild.done`（`EXIT_CODE=<n>`）。
- **本轮编译结果：692/692 全量重编成功，`build\R3R_fullbuild.done` = `EXIT_CODE=0`，`build\openttd.exe` @ 2026-09-22 02:02:51（50 798 592 B）**；12 个改动文件全部通过，无编译错误。
- **本轮冒烟测试（无头加载）**：`openttd.exe -g test_multi_company.sav -v null:until_exit` 存活 90 s 未退出、stderr 为空、`build\R3R_debug.log` 末段无 `ASSERT` / `INVALID_REF` / `SIGSEGV`，且已进入渲染主循环（`DRAW-STATION-NORES tile=39,31`）⇒ 新 exe 可启动、可读旧档（`ForceFlipReverse` 保留槽位未破坏 `Train::flags` 位布局）。这不等于 KI-153/155 的行为实测通过。
- **编译通过 ≠ 实测通过**：下列三条的实测点全部待在游戏内复测。

### KI-153（已修，待实测）：车库内挂接/解挂后出现「0 号车」陈旧行、图像不刷新；解耦再耦合后列车出不了库

- 描述（两个症状，同一处入口）：
  - **(a)「0 号车」陈旧行 + 图像不刷新**：车库内挂接（`Couple()`）把被吸收链的头车身份/车号交出去后，该头车的 `unitnumber` 变 0；解挂（`DecoupleTrain()`）又让新释放的部分拿到全新的 identity。已打开的车库窗口（`DepotWindow`，列表缓存由 `BuildDepotVehicleList()` 在 `src/vehiclelist.cpp` 构造）不会因为这次成员变化自动重建 ⇒ 列表里留着一条「Train 0」幽灵行，被合并/被拆链的车辆图像也停在旧帧，直到某次无关的重绘才消失。
  - **(b) 解耦再耦合后无法出库**：链路是 `R3RStopChainInDepot()`（`src/vehicle_cmd.cpp:334`）——R3R 的 depot 工具把链钉在库里等库内 `GOTO_COUPLE` 时会**同时**置 `r3r_parked = true` 与 `VehState::Stopped`。玩家在库里手工解耦再耦合后，这条「等待库内挂接」的前提已不存在，但 `r3r_parked` 若残留，`TrainLocoHandler` 的库内挂接豁免（`r3r_parked && 未完成的库内 GOTO_COUPLE`）会继续成立，而挂接扫描再也找不到可挂对象 ⇒ 每 tick 重新置 `Stopped`；同时 `CheckTrainStayInDepot()`（`train_cmd.cpp:7336`）的 `OT_GOTO_COUPLE` 到站守卫持续 `return true` ⇒ 列车被永久钉在库里。
- 来源：玩家第 83 轮清单第（一）（二）项。
- 修法（3 处，全部在 `src/train_cmd.cpp`）：
  1. 新增文件内静态 `R3RInvalidateDepotWindowsForChain(const Train *v)`（约 4347）：沿链找到第一个车辆所在的轨道车库格，`InvalidateWindowData(WindowClass::VehicleDepot, tile.base())`，再 `InvalidateVehicleListWindows(VehicleType::Train)`（车辆列表窗口缓存同一套分组）。
  2. `DecoupleTrain()` 收尾（`v->MarkDirty(); u->MarkDirty();` 之后）对 `v` 与 `u` 各调一次；`Couple()` 提交点（`InvalidateWindowClassesData(WindowClass::TrainList, 0)` 之后）对合并链 `v` 调一次 ⇒ 幽灵行与陈旧 sprite 在同一帧消失。
  3. `R3RSyncChainAfterDepotEdit()` 收尾（约 4320）：`chain->r3r_parked = false;` —— 玩家手工编辑链即视为「我接管了」，丢掉停库等待态；真正还在执行库内挂接的链会在下一 tick 由机车运行路径重新置回，所以不会关掉自动挂接功能。同一位置补「链头 `unitnumber == 0` 时从**链自己业主**的 `freeunits[VehicleType::Train]` 取号」的兜底（刻意不用 `GetFreeUnitNumber()`——它读全局 `_current_company`，在车辆 tick 中无意义）。
- 状态＝已修（已编译，待实测）。严重度：中（陈旧行 = 显示错误；出不了库 = 卡死）。
- 实测点：① 车库内挂接后 ⇒ 无「Train 0」行、被吸收链消失、图像立即正确；② 车库内解耦 ⇒ 解出部分立即出现且带正确车号/图像；③ 解耦后立刻再耦合 ⇒ 列车能正常出库执行订单；④ 未做任何 depot 编辑的库内自动挂接场景不受影响（仍能被自动挂上并带出库）。
- 残留（未修）：`R3RSyncChainAfterDepotEdit` 只在 depot 编辑入口调用，若别处（卖出、自动吸车钩子）留下 `r3r_parked` 残留，本修法不覆盖；后续若复现「又一辆出不了库」，优先看 `r3r_parked` 与 `current_order` 是否仍是 `OT_GOTO_COUPLE`。

### KI-154（诊断增强，**未修**）：列车停住不执行订单（日志 `SKIP-STOPPED`）

- 描述：`TrainLocoHandler()`（`src/train_cmd.cpp`）在 `consist->vehstatus.Test(VehState::Stopped) && consist->cur_speed == 0` 且**不是**「库内等待挂接」时直接 `return true`，本 tick 不处理订单、不寻路、不出库。这是设计行为（停稳的列车不应被订单往前推），但它同时也是所有「订单看起来被神秘跳过」的头号嫌疑点，而旧探针只记了 `veh/order/real/spd/tile`，无法反推**为什么**这列车应当是停的。
- 来源：玩家第 83 轮清单第（三）项（玩家被要求提供复现场景/存档）。
- 本轮处理：把 `SKIP-STOPPED` 探针补齐到「决定它是否该停的全部输入」，并把 `r3r_parked` 并入 `R3RDbgEdge()` 的去重 payload（该状态翻转时立即出一行）：
  `SKIP-STOPPED veh=%d order=%d real=%d spd=%d tile=%d,%d parked=%d front=%d nord=%d idx=%d tt=%d`
  ——`parked`=被 depot 工具停库、`front`=是否链头（非链头永远出不去）、`nord`=订单条数（-1=无排程）、`idx`=`cur_real_order_index`、`tt`=`cur_timetable_order_index`（TT-CHK 相关）。
- 状态＝诊断增强（**不是修复**，行为未改）。严重度：中（行为不符）。
- 已知可疑前提（第 83 轮子代理定位 Q1/Q2）：`r3r_parked` 残留、`current_order` 仍为未被消费的 `OT_GOTO_COUPLE`、`cur_real_order_index` 越界、`orders == nullptr`、`IsFrontEngine()` 为假、`HasDepotReservation` 未清。
- 实测点：复现「停住不执行」时抓 `build\R3R_debug.log` 中该车最后一条 `SKIP-STOPPED`，按 `parked/front/nord/idx/tt` 直接定位原因；若 `nord=-1` 或 `idx` 越界则属排程丢失，另开条目。

### KI-155（已修，待实测）：清除玩家「换向」按钮的 force-flip 残留代码（`ForceFlipReverse` 整条链路）

- 决策依据：`R3R_order_reverse_force_memo.md` §0 记录「被弃用但尚未删除的代码」，枚举项恰好是 `VehicleRailFlag::ForceFlipReverse` 标志、其消费点、3 个订单侧 Set 点、以及「按钮路径」；用户第 83 轮清单第（四）项＝删除该项。
- 弃用理由（沿用备忘）：整链 `ReverseTrainSwapVehicles` 物理换端对**多段链**是非法的——段的先后次序不允许颠倒（记忆 97996690 / 31054337），force 路径绕过段序保持。
- 修法：
  1. `src/train_cmd.cpp`：删除 `ReverseTrainDirection()` 入口的 `force_end_swap` 消费（含 `REVERSEDIR` 调试行里的 `force_swap=%d`）、删除 force 分支（清 `DrivingBackwards` + `ReverseTrainSwapVehicles` 三连）与 KI-127 的 `flip_reverse_allowed == None` 丢弃闸门 ⇒ **恢复上游原生二分支**（`if (DrivingBackwards || flip_reverse_allowed == None || Last()->CanLeadTrain())` 倒车，否则换端）；删除 `NormaliseDualHeads()` 里的标志复位；删除 4 个 Set 点（`CmdReverseTrainDirection` 的按钮参数、`TrainEnterStation` 的 waypoint 分支与 station 分支、`TrainLocoHandler` 的 waypoint fallback 分支）。
  2. 删除 `CmdReverseTrainDirection()` 的第三参数 `force_flip_reverse`（`src/train_cmd.h`：`CmdDataT<VehicleID, bool, bool>` → `CmdDataT<VehicleID, bool>`），8 个调用点同步：`autoreplace_cmd.cpp:544`、`depot_gui.cpp:1473`、`vehicle_gui.cpp:4663`、`vehicle.cpp:4183`、`tbtr_template_vehicle_func.cpp:348`、`tbtr_template_gui_create.cpp:470`、`script/api/script_vehicle.cpp:244`、`order_cmd.cpp:4349`；其中**只有 `vehicle_gui.cpp` 传的是 force=true**。
  3. `src/train.h`：`ForceFlipReverse = 23` **保留槽位**，注释改为「RETIRED，勿复用」——`Train::flags` 是存档字段，槽位后移会让 `ArticGroupHead = 24` 等后续位错位，破坏旧档；`grep` 确认全树已无任何引用点（只剩这一行注释）。
  4. `src/vehicle_cmd.cpp`：`MAKESEG` 调试行的 `flip=%d` 字段随之删除。
- **未删除任何 UI 控件**：§0 的枚举清单里只有「按钮路径」（即 `CmdReverseTrainDirection` 里 `if (force_flip_reverse) v->flags.Set(...)`），没有删除 widget 的条目；备忘同时说明订单「到达调向」的语义仍然成立——列车确实朝反方向走，只是按 `train_flip_reverse_allowed` 的原生含义用**倒车**而不是换端来实现（这正是该设置项的原生语义）。⇒ 车辆视图「掉头」按钮与订单「到达调向」标志**都保留**，行为回到原生。若玩家要的是**连按钮也一起删**，需明确是哪一个（订单窗口 `WID_O_REVERSE_AT_STATION` / 车辆视图 `VCT_CMD_TURN_AROUND`），另开条目处理。
- 状态＝已修（已编译）。严重度：中（行为不符：整链物理换端违反段序保持，对多段链是非法操作）。
- 实测点：① 车辆视图点「掉头」⇒ 单段列车正常换端、多段链按原生规则（可能倒车）而不是被整链物理旋转；② 订单勾选「到达调向」⇒ 到达后朝反方向驶出，段序不颠倒；③ `train_flip_reverse_allowed = none` ⇒ 只倒车不换端（原生语义）；④ 旧存档读入不因 `flags` 位错位而错乱。


## 第 84 轮（2026-09-22）：第 83 轮复测反馈 + 新增需求

> 详见工作区备忘 `R3R_第84轮_复测反馈与新增需求备忘.md`。本轮仅**记录口径与需求**，截至写入时未改任何源码。

### KI-153（部分防护，第 84 轮复测：① 通过 / ② 未通过）

- 复测结论：修复 1（KI-153a：库内挂接/解挂后无「Train 0」陈旧行、图像立即正确）＝**玩家确认通过**；修复 2（KI-153b：解耦再耦合后能出库）＝**未通过，表现与修复前一致**。
- 现场（`build/R3R_debug.log` 末段）：38,27 库内 `ARRANGE-IN` → `COUPLE-OK`（9 节、`SEG=2`）→ `ORD-AFTER-COUPLE ... n=7 real=1 borrowed=1 owner=3 u_has_orders=1` → 离库途中 `REVERSEDIR ... db=0` → `REVERSEDONE db=1 mvfront=8`（一次整链物理换端）→ 末行 `DEPOT-ARR ... tileDepot=0 tx=38 ty=30 destTx=39 destTy=31` / `DRAW-STATION-NORES tile=39,32`（即该次运行**已出库**）。
- 待玩家澄清「表现与修复前一致」确切所指：(a) 仍被钉在库里出不来；(b) 能出库但离库过程/结果不对（如上述整链换端、订单被跳）；(c) 其它。抓到的日志是否即失败现场亦待确认（日志可能被后续会话覆盖）。
- 状态＝部分防护（① 通过、② 未修，待定位）。严重度：中（行为不符）。

### KI-155（已修，第 84 轮复测：④ 通过；③ 口径变更，UI 部分被 KI-156 取代）

- 复测结论：④ `train_flip_reverse_allowed = none` 只倒车不换端 ＝**玩家确认通过**；③ 段序保持 —— 玩家不再采用「保留 UI + 修行为」，改为**彻底删除「换向」UI**（见 KI-156），**本条上方「车辆视图『掉头』按钮与订单『到达调向』标志都保留」的 UI 结论被取代**；订单侧 `HasReverseAt*` 语义与 `ForceFlipReverse` 槽位保留结论不变。
- 状态＝已修（④ 通过）。严重度：低。

## 第 85 轮（2026-09-22）本轮改动状态总览

- 本轮改动文件：`src/order_base.h`、`src/order_type.h`、`src/order_cmd.cpp`、`src/order_gui.cpp`、`src/widgets/order_widget.h`、`src/train_cmd.cpp`、`src/rail_cmd.cpp`、`src/vehicle_gui.cpp`、`src/lang/english.txt`、`src/lang/simplified_chinese.txt`；含 `src/*.h` ⇒ 已按 KI-15 删全部 `*.obj` 全量重编。
- 编译证据：`R3R_fullbuild.cmd detach` → `build\R3R_fullbuild.log` 末行 `[707/707] Linking CXX executable openttd.exe`、`build\R3R_fullbuild.done = EXIT_CODE=0`（2026-09-22 04:37）；`build\openttd.exe` @ 04:36:18（50 793 984 B）；`rail_cmd.cpp.obj` @ 04:28、`vehicle_gui.cpp.obj` @ 04:32（均晚于改动）。
- 冒烟验证：`openttd.exe -g "…\Planingbury Transport, 2030-01-25.sav" -v null` 无头加载 60 秒未退出、stdout/stderr 无致命错误（含语言包版本，即 KI-16 未复发）。**仅证明「能起来、能读档、不崩」，不等于功能已验收。**
- KI-153b（车库「仅头一节出库」）：**部分防护（待实测）** —— 见 §85.1。
- KI-156（删「换向」UI + 订单能力）：**已实现 + 已全量重编**（待游戏内目测）。
- KI-157（列表口径）：**已实现 + 已全量重编**（待游戏内目测）。

### §85.1 KI-153b（**放宽已回滚，第 86 轮**）出库「逐节激活」判定此前由「精确相等」放宽为「到达或越过」

- **第 86 轮结论：该放宽实测无效（玩家复测仍卡在车库口），已按玩家要求整块回滚 `src/rail_cmd.cpp`，恢复 vanilla 的 `fract_coord_leave == fract_coord` 精确判定。** 回滚后新现场证据见 §86.1。


- 玩家现场线索：**列车头一节的边界框出了车库，其余车节全部仍在库内** ⇒ 不是「整列被钉死」，而是 vanilla 的出库逐节激活级联在头车那一步就断了。
- 机制（原语未改）：库内车节 `v` 沿出库方向移动时，只有 `fract_coord_leave == fract_coord`（`fract_coord_leave = _fractcoords_enter[dir] + (CalcNextVehicleOffset()+1) * _deltacoord_leaveoffset[dir]`）这一个**精确像素坐标**才会把 `v->GetMovingNext()` 从 `VehState::Hidden` 唤醒并置 `track = AxisToTrackBits(DiagDirToAxis(dir))`。`src/rail_cmd.cpp` `VehicleEnterTile_Rail()`。
- 本轮修改（仅 `src/rail_cmd.cpp`）：改为按出库轴做「到达或越过」判定 ——
  `leave_axis_passed(got, want, off) = (off<0 ? got<=want : off>0 ? got>=want : false)`，两轴取或。
  依据：`_fractcoords_enter[dir]` 恒位于 `fract_coord_leave` 的**外侧**（NE 10→10-len、SE 4→4+len、SW 4→4+len、NW 10→10-len，len≥2 ⇒ 起点判定恒为 false），故**正常出库的车触发步与旧代码完全相同**；只有两类异常状态会提前触发：①一 tick 内跨过多像素、**跳过**该唯一坐标（长车 + 靠近库门口的相邻车，实测 `_fractcoords` 允许 leave 落在 fract=1..13，靠近边界极易被跨过）；②链被重新链接（挂接/解挂/换端重排身份）后车节**已经停在越过了激活点的位置**。两者都会让后面所有车节永久 Hidden 在库内。
- 已知边界：若**头车已完全驶出车库 tile**，`VehicleEnterTile_Rail` 对非车库 tile 直接 return，本放宽无法再救——若实测仍出现「头车出库、其余不出」，下一步是在 `TrainLocoHandler` 的出库循环里加**看门狗兜底**（移动前端已在库外 + 仍有 Hidden 车节持续 N tick ⇒ 强制逐节唤醒），本次未做（需新增 per-consist 计数，避免动 `Vehicle` 存档布局）。
- 取证结论：`build\R3R_debug*.log` 四份日志中**没有任何**「链头 tileDepot=0 + 后段 tileDepot=1」的混合快照（0 命中），`CARONLY-PARK` 从未触发（`train_cmd.cpp:~11461` 零功率纯车厢停驻豁免不是本 bug 成因）；`build\R3R_debug.log` 尾部那次 38,27 出库**全部车节都出来了**（末行 `CHAIN` 三块 tileDepot 均为 0）⇒ 日志不是失败那次，无法据此定论。
- 状态＝部分防护（第 85 轮，仅 .cpp 改动、随全量重编生效），**待玩家实测**。严重度：高（卡死）。

### §85.2 KI-157 本轮落点（列表口径）

- 新增两个文件内静态辅助（`src/vehicle_gui.cpp`，不改 `.h`）：`R3RControlSectionVehicle()`（控制段＝挂接序数最小的段＝链头所在段，其段头＝物理链头；仅对 `VehicleType::Train && IsPrimaryVehicle()` 生效，避免影响车库散车行）与 `R3RFastestOverAgeVehicle()`（链内 `max_age - age` 最小者，平局取靠前者）。
- 应用点：`VST_AGE` / `VST_TIME_TO_LIVE` 的显示值与 `VehicleAgeSorter` / `VehicleTimeToLiveSorter` 排序值（口径一致）；行内车名 / 组名（`STR_VEHICLE_NAME` / `STR_GROUP_NAME`）与持有公司色条改用控制段车。
- 说明：对 R3R 挂接链而言链头即控制段头，故「编号/车名/公司」在正常存档下**显示不变**（该改动是防御性的：一旦身份被搬到非链头车，列表仍显示控制段）。「年限」是**实际行为变化**。

### §86.1 KI-153b 新现场证据（2026-09-22 04:50，放宽版运行时抓取）

- 现场（`build\R3R_debug.log` 尾，卡死块连续重复 26 次后日志结束）：列车 9 节全部 `tile=38,27`（车库格），除链头外 `trk=0x80`（`TRACK_BIT_DEPOT`）；链头 `idx=2`（假引擎）`x=616 y=447 tile=38,27 dir=7 trk=0x2 spd=30 prog=0`，即**已脱离车库 track bit、停在车库格底部边缘**；`TTB-PROBE fold-geom veh=2 tile=38,28 x=616 y=448 ... first=2 mvfront=3 db=1 speed=30`；其前状态为 `REVERSEDONE db=1 mvfront=3`、`NOCAB-SET this=2 db=1 last=3 lastSub=0x04 lastEng=0 lastLead=0`、`NOCAB-LIMIT this=2 db=1 spd=0 last=3 lastLead=0 order=1`。
- 关键推论（推翻 §85.1 假设）：vanilla 的逐节唤醒**只发生在 `VehicleEnterTile_Rail()` 内、且只有该车 `tile` 仍是车库格时才可能触发**（函数开头对非车库格直接 return）。本现场链头 `tile` 已是 38,28、`track` 已是真实轨道位 0x2，所以“在车库格里到达 leave 坐标”这一步**根本不会被调用** ⇒ 放宽 fract 判定救不了它，实测无效得到了解释。
- 新疑点（下一步定位顺序）：① 该编组是**纯车厢编组**（`NOCAB`：链尾 `CanLeadTrain()==false` ⇒ `TCF_NO_DRIVING_CAB`、倒车限速 32），且刚 `REVERSEDONE db=1`（车库出口把整车反向、改为倒着开出去）；倒车出库时“移动前端”在链尾（`mvfront=3`，`y=436`＝库内 staging 位），而真正压在边界上的是链头 idx2 —— **满足 leave 判定的那一节与移动前端不是同一节**，逐节解堆叠的次序假设可能因此不成立。② 是否要求移动前端在库内按 `length` 递增逐节唤醒，而 `db=1` 时链遍历方向与 `_fractcoords_enter/leave` 公式的前提相反。
- 下一步候选（**尚未实现**，待拍板）：不再依赖 `VehicleEnterTile_Rail` 的 fract 顶点判定，改在出库路径上加**看门狗**：仅当“本 tick 该列车正在离开（`cur_speed != 0` 或刚起步）**且**移动前端已在本车库格之外”**且** “链内仍有 `vehstatus & VehState::Hidden` 且 `track == TRACK_BIT_DEPOT` 的车节”持续 N(≈3) tick 时，按链序逐节 `vehstatus.Reset(VehState::Hidden)` + `track = AxisToTrackBits(DiagDirToAxis(depot_exit_dir))` + `UpdateIsDrawn()`。**关键约束：停在库里的列车本来就全体 Hidden + `0x80`，那是正常态**，所以看门狗必须带“正在离开”判别，否则会破坏正常停车——这也是本轮不直接动手的原因。
- 状态＝放宽已回滚（vanilla 精确判定），根因**未修**，症状「仅头一节出库」仍可复现。严重度：高（卡死）。

### KI-158（已修，待重编验证，低）：R3R 假引擎的「年限」恒为 0 年

- 现象（玩家 2026-09-22 提问）：假引擎（R3R 把车厢升格成的段头/链头）在年限一栏恒显示 0 年。
- 根因：`max_age` 只在建造时赋值＝`Engine::GetLifeLengthInDays()`＝`(info.lifelength + extend_vehicle_life) * DAYS_IN_LEAP_YEAR`（`src/engine.cpp:511-515`）。**货车引擎的 `lifelength` 为 0**（购买界面专门对 rail wagon 不画「设计/寿命」行，`src/build_vehicle_gui.cpp:1071`），而 R3R 的假引擎是**车厢原地 `SetEngine()` 升格**（`R3RCreateCarOnlyFormation` / `MakeConsist` 路径），`engine_type` 仍指向那个**货车引擎**，也从未改写 `max_age`（全仓 R3R 只有两处 `max_age =`，都在原生建造路径 `train_cmd.cpp:2054 / 12064`）⇒ 假引擎 `max_age == 0`。**代码里没有任何 `max_age == 0` 特判**；vanilla 之所以没暴露，是因为普通车厢从不单独成为列表行（行取链头，链头是真引擎）——R3R 把车厢升格成链头后这个 0 就露出来了（`(age + DAYS_IN_YEAR < 0)` 为假 ⇒ 同时命中红字分支）。
- 附带放大：第 85 轮给列表加的链内口径 `R3RFastestOverAgeVehicle()` 取 `max_age - age` 最小者，只要链内有假引擎就会被它以“0 − age”恒取胜 ⇒ 整行显示 0 年。
- 修法（第 86 轮，仅 `src/vehicle_gui.cpp`，.cpp 增量合法）：`R3RFastestOverAgeVehicle()` 跳过 `max_age.base() == 0` 的车节（永不超龄，不参与“谁先超龄”比较）；若全链都无寿命限制（纯车厢编组），回退显示链头＝第 85 轮以前的显示口径。
- 遗留问题（待拍板）：纯车厢编组（无任何真引擎）语义上“无寿命限制”，列表仍会显示 0 年/红字；是否改文案（如新增“无寿命限制”串）由玩家决定，本轮不改。
- 状态＝已修（第 86 轮），待增量重编 + 目测。严重度：低（瑕疵）。

### KI-156（已实现，第 85 轮；待全量重编 + 目测）：彻底删除订单窗口「换向」UI

- 描述：玩家 2026-09-22 要求「彻底消灭这个叫做『换向』的 UI」。
- **第 85 轮拍板**：连订单能力一起删除；车辆视图「掉头」按钮保留。
- **第 85 轮实施（已完成）**：`src/widgets/order_widget.h` 删 `WID_O_REVERSE_AT_STATION`；`src/order_gui.cpp` 删全部 NWidget 项 / 刷新 / 禁用 / OnClick；`src/order_base.h` 删 `HasReverseAtStation|Depot|Waypoint` 与全部 setter；`src/order_type.h` 的 `MOF_REVERSE_AT_STATION` 保留为空槽占位（防后续枚举值漂移）；`src/order_cmd.cpp` 删白名单 / 参数校验 / 执行分支，并还原被它改写的 via 目的地判定与 `ShouldStopAtStation` 路点分支；`src/train_cmd.cpp` 删站点 / 路点 / 兜底三处消费块；`src/rail_cmd.cpp` 删入库 `reset_reverse` 一行；`src/lang/english.txt` + `simplified_chinese.txt` 删 3 条 tooltip 串。**保留**：`WID_O_REVERSE`（JGRPP 原生路点换向，消费点在 `train_cmd.cpp`）、车辆视图 `WID_VV_TURN_AROUND` / `VCT_CMD_TURN_AROUND`。
- 残余：`VehicleRailFlag::ForceFlipReverse` 槽位保留占位（写入点已随本删除全部退休），与 KI-155 一致。
- 约束：本轮改了 `src/widgets/*.h` 与 `src/*.h` ⇒ 必须删全部 `*.obj` 全量重编（KI-15 / 记忆 66636022）；改了 `src/lang/*.txt` ⇒ 必须强制重编 `strings.cpp`（KI-16 / 记忆 52814982），否则启动报「No available language packs」。
- 状态＝已实现（第 85 轮），待全量重编 + 游戏内目测（订单窗口无「换向」按钮、旧档不崩、路点原生换向仍在）。严重度：低（瑕疵）。

### KI-157（待实现，低）：车辆列表「年限 / 编号 / 车名 / 持有公司」改取链内口径

- 描述（玩家 2026-09-22）：① 「年限」应显示**全链中距超限（超龄）剩余时间最短**的那节车；② 列车编号 / 车名 / 名义持有公司 应显示为**「命令控制段」**的（持有公司若实现困难可保持现状）。
- **第 85 轮口径拍板**：命令控制段 = **挂接序数最小**的段，同时也是**主导整条链调度命令**的段。即判定用结构口径（挂接序数最小），而不是「当前恰好持有 `orders` 的那一段」；列表取该段的编号 / 车名 / 名义持有公司；「年限」取链内**最快超龄**者（= 全链「距超限剩余时间最短」的那节车）。
- 落点：`src/vehicle_gui.cpp` `BaseVehicleListWindow::DrawVehicleListItems()` —— `VST_AGE` 2074-2078、`VST_TIME_TO_LIVE` 2129-2133、排序器 `VehicleAgeSorter` 73 / 排序枚举 `VST_AGE` 107；行内车名 `STR_VEHICLE_NAME`（2243-2257 一带）、持有公司色条 2179-2185；编号位宽 `GetUnitNumberDigits()` 288-294 / 325-354。可复用先例：同函数 2221 `vehgroup.GetOldestVehicleAge()`（「取链内最老车」已存在）。
- 状态＝待实现（口径已定，见上）。严重度：低（瑕疵）。


## 第 87 轮（2026-09-22）：车库内合链朝向归一 —— 「靠近库门的一端领车」

> 需求来源：玩家 2026-09-22 对话拍板 —— 车库内合链后，由**离库门最近的那一端**当
> moving front（领车端）。库内各股道是平行的，没有「谁比谁更靠外」的偏序，
> 所以判定只能建立在库轴几何（`GetRailDepotDirection()`）上。

### KI-159（已实现，待游戏内目测，中）：库内合链的带路端改由库轴几何决定

- 改动前三条事实（核实过）：
  1. 库内整链朝向由「库 tile 出口朝向 + 链头 `DrivingBackwards`」完全导出：
     `Train::ConsistChanged` 的 `ConsistChangeFlag::DepotDirection` 分支
     （`src/train_cmd.cpp:311-334`），`CCF_ARRANGE` 才含该位、`CCF_TRACK` 不含；
  2. `GetMovingFront()` = `DrivingBackwards ? Last() : First()`，`moving-*` 家族只读 DB、
     与逐节 `direction` 无关（`vehicle_base.h`）；
  3. `Couple()` 合链结尾**不做朝向归一**（原来只有 `R3RRespaceChainAfterEdit` +
     `FillTrainReservationLookAhead` + `MarkDirty`），而紧随其后的「清 DB」块会
     **无条件把 `DrivingBackwards` 清零**（＝机车端领车）—— 与车库出口几何相反时，
     出库时先动的就是机车那端，而不是靠门那端。
- 修法（`src/train_cmd.cpp`，仅 `.cpp`，增量合法）：
  1. 新增文件内静态 `R3RNormaliseDepotMergeDirection(Train *v)`（定义在 `Couple()` 之前）：
     以库 tile 中心为投影原点、`TileIndexDiffCByDiagDir(GetRailDepotDirection(tile))` 为库轴，
     取 `tail_leads = exit_offset(Last()) > exit_offset(head)`（库外方向的投影更大），
     写 `VehicleFlag::DrivingBackwards` 后 `v->ConsistChanged(CCF_ARRANGE)` —— 后者让全链
     按库轴统一朝向并刷新 NewGRF 缓存。
     守卫：`IsRailDepotTile(v->tile) && IsWholeTrainInsideDepot(v)`；链尾还在普通轨道上时
     **绝不改朝向**，否则会造出非法的 `(track, direction)`（KI-114 的病灶）。
  2. 调用点在 `R3RRespaceChainAfterEdit()` **之前**：后者沿 `GetMovingNext()`（读 DB）把
     链接缝推紧，DB 没摆正会把车往错误方向推。
  3. 拼缝端面判据（第 64 轮 / `R3R_couple_direction_rule_memo.md`）对库内合链**跳过**：
     库内每节车 `direction` 只可能是库轴或其反向，不会出现 90° 拼缝；而
     `R3RReverseChainDirections` 会 `Flip(Flipped)` 作渲染补偿，紧接着按库轴重写
     `direction` 时这份补偿会让被翻转的那半截整体渲染反向。
     条件写成 `IsWholeTrainInsideDepot(v) ? nullptr : merged_first->Previous()`。
  4. 原「清 DB」块（把 `DrivingBackwards` 清零那处）守卫加 `&& !IsRailDepotTile(v->tile)`：
     库内不清，否则会把刚摆好的「靠门端领车」推翻；非库场景保持原行为（机车端领车）。
- 探针：`build/R3R_debug.log` 新增 `[R3R] DEPOT-MERGE-DIR head=<idx> tail=<idx> tail_leads=<0|1> head_off=<n> tail_off=<n>`。
- 编译证据：`_tmp_inc_build.cmd` → `build\R3R_incbuild.log` 末行 `[3/3] Linking CXX executable openttd.exe`、
  `build\R3R_incbuild.done = EXIT_CODE=0`、`build\openttd.exe` @ 2026-09-22 06:41:10（50 793 984 B）。
- 待实测（本机无视觉，需玩家目测）：
  1. 库内机车挂车底 ⇒ `DEPOT-MERGE-DIR` 的 `tail_leads` 应与「哪端靠门」一致，出库先动的是靠门那节；
  2. 库内两列车一正一反（`direction` 相差 180°）合链 ⇒ 全链被统一到库轴朝向，出库无「原地掉头」的突兀；
  3. 链尾伸出库门外的合链 ⇒ **不触发**本归一（守卫拦截），行为与改动前一致；
  4. 回归：站台/线路合链的 `COUPLE-SEAM ... circ=... act=...` 逐位不变
     （本改动只在 `IsWholeTrainInsideDepot()` 为真时生效）。
- 已知边界/风险（未实测）：库内被统一 `direction` 的那半截，若原本 `Flipped=1`
  （多机车后节 / GRF 反向概率 / 玩家单节反转）渲染仍按 `Flipped` 反向，属预期；但若同一编组
  同时经历过折叠修正路径 `R3RFlipChainBySegments`（`Flipped` 被 Flip 过），随后统一 `direction`
  会让它渲染翻面 —— 留意同一编组是否同时出现 `COUPLE-FLIP-*` 与 `DEPOT-MERGE-DIR`。
- 状态＝已实现（第 87 轮），待游戏内目测。严重度：中（行为不符）。


## 第 88 轮（2026-09-22）：库内 GOTO_COUPLE 耦合寻路 —— 预留断言防护与两处"疑似缺陷"澄清

> 现场：`veh=7` 停在车库地块 `58,54`（`track=TRACK_BIT_DEPOT`、`vehDir=5`、`vehTd=8`、
> depot 轴为 `TRACK_X`），排程为 `OT_GOTO_COUPLE`（dest `60,48`）；等待车列 `veh=1..6`
> （`WAIT_COUPLE`）停在平台 `60,48..60,51`（`TRACK_Y`）。第三列车 `veh=0` 途经
> `60,54→60,44` 进库，短时占用 `60,53`。

### KI-160（已修，中）：耦合预留扫到"不含该轨道"的地块会撞 `pbs.cpp:144/145` 断言

- 落点：`src/pathfinder/yapf/yapf_rail.cpp` `CYapfFollowCoupleRailT::FindNearestCoupleTrain`
  的单段耦合特判里，`rp()` 预留共四处直接调用 `TryReserveRailTrack(tile, track)`：
  ①站台轴线性补扫 `p += delta` 两处（`plat_track`）、②递归返回后补预留当前块 `cur`、
  ③补预留下一块入口轨道。
- 病灶：这些 `track` 有的取自站台轴 `plat_track`、有的取自出发 `trackdir`，都可能落到
  **根本不含该轨道**的地块上（站台端头、已拆轨地块、另一轴地块），于是命中
  `TryReserveRailTrack` 顶部的 `assert_msg_tile(...)`（`src/pbs.cpp:144-145`）。
- 修法（仅 `.cpp`，增量合法）：新增局部 lambda
  `reserve_if_present(TileIndex t, Track trk)` —— 先取
  `TrackdirBitsToTrackBits(GetTileTrackdirBits(t, TRANSPORT_RAIL, 0))`，只有该地块
  真的带这条 `track` 才调 `TryReserveRailTrack`；上述四处调用点全部换用。
- 编译证据：`build\openttd.exe` @ 2026-09-22 07:25:09（50 798 592 B），
  源码 `src\pathfinder\yapf\yapf_rail.cpp` @ 07:20:20。
- 待实测：站台端头 / 拆轨地块场景不再触发 `pbs.cpp:144` 断言。
- 状态＝已修（第 88 轮）。严重度：中（断言中止）。

### KI-161（已核实为非缺陷）：`CPL-TRACE-RESULT best=0` 出发 `trackdir` 与机车朝向相反

- 日志：`CPL-ORIGIN veh=7 origin=58,54 td=8 vehDir=5 vehTd=8`、
  `CPL-TRACE st=58,54 tdb=0x101`（候选 `TD_X_NE=0` 与 `TD_X_SW=8`）、
  `CPL-TRACE-RESULT st=58,54 best=0`、`CPL-RESERVE veh=7 reserved=1 next=0`。
  选择循环用 `FindFirstBit` 取到 bit0（`td=0`），与机车实际朝向 `td=8` 相反，
  一度怀疑出发方向取错。
- 结论：**不是缺陷**。`YapfTrainCoupleTrack` 返回的是 `Track` 而非 `Trackdir`，而
  `TrackdirToTrack(td) = td & 0x7`，`TRACKDIR_X_NE(0)` 与 `TRACKDIR_X_SW(8)` 都映到
  `TRACK_X(0)`，两者返回值逐位相同。`reach()` 从 `td=0` 起步只是先在起点做一次
  掉头（`REACH cur=58,54 td=0 depth=0` → `cur=58,54 td=8 depth=1`），其后的
  `REACH`/`RP` 轨迹与从 `td=8` 起步完全一致（`58,54 → 59,54 td=5 → … → 60,57 → … → 60,48`），
  预留也按这条轨迹走，故机车行进方向正确。
- 状态＝已核实非缺陷（第 88 轮）。

### KI-162（**第 88 轮更正：判为缺陷**，中）：`CPL-SAFE-FAIL ... fail=tryReserve`（60,53）

> 第 88 轮初版把 5 次失败全部归因于第三列车 `veh=0`，**该结论是错的**，此处更正。
- 日志实证（5 次失败全在 `60,53`，`td=10`、`res=0x2`）：
  - `849 / 874 / 907` 行：`FSCP-RES tile=60,53 tk=1 owner=1`，且**没有** `FSCP-VEH` 行
    ⇒ 该 `Y` 预留的持有者是 `veh=1`，即**本次要挂的那个 `WAIT_COUPLE` 车组本身**
    （`877-884` 行 `RESERVECONSIST veh=1..6 tile=60,51..60,48`，对应 `CPL-HEAD best=60,48 head=60,51`）；
  - `938 / 966` 行：`owner=0` ＋ `FSCP-VEH tile=60,53 veh=0 ... ord=6`
    ⇒ 只有这两次才是第三列车压在 `60,53` 上的真实障碍。
- 机制：`FindSafeCouplePositionProc`（`yapf_rail.cpp:110`）的"耦合目标白名单"只覆盖
  **目标车辆实体占据**的地块（`:142` 的 `best != nullptr` 分支 → `R3RIsCoupleTarget` → `return true`）。
  `60,53` 在前三次**没有任何车**（无 `FSCP-VEH`），只是**被目标车组预留**，
  所以落到 `:190` 的 `else if (GetReservedTrackbits(tile) != TRACK_BIT_NONE)` 分支 →
  `TryReserveRailTrack(tile, TrackdirToTrack(td))`。而 `TryReserveTrack`
  （`src/rail_map.h:255`）**只看预留位、不看 owner**：`res=0x2`(bit1) 与要的 `1<<2=0x4`
  不同 → `(res & bits)==0` 通过 → `res|bits=0x6` → `TracksOverlap(0x6)`
  （`src/track_func.h:608`，既非 `HORZ(0x3)` 也非 `VERT(0xC)`）⇒ 交叉，`return false`。
  即：**持有 `GOTO_COUPLE` 的机车 `veh=7` 被它自己的耦合目标车组的预留挡住了**，
  与该函数注释声明的意图（`:105-108`"Tracks the waiting consist's own reservation so the
  coupling locomotive drives right up to it"）直接矛盾 —— 白名单漏了"**仅预留、无车**"这一情形。
- 但不能简单"放行"：`SetTrackReservation`（`src/rail_map.h:239`）带
  `dbg_assert(!TracksOverlap(b))`，一个地块的预留位**表示不了 `Y|UPPER` 这种交叉组合**，
  即同一地块上交叉的两条预留本质无法共存，"谁后到谁拿不到"。
  故两条候选根因待定：**(a)** 该地块不该被目标车组的 `Y` 预留占用太久 ——
  `RESERVECONSIST` 只补扫了 `60,48..60,51`，`60,53` 的 `Y` 预留不在其中，
  说明它是更早的 PBS 路径预留残留（等待车组朝南、预留铺到前方 `60,53`），
  需抓该预留的建立时机；**(b)** 耦合寻路不该要求穿过交叉轨的地块（路径/落点问题）。
- 状态＝**未修（第 88 轮更正结论；上一版"已核实非缺陷"作废）**。严重度：中（机车停住不动）。
  复现判据：`FSCP-RES ... owner=<目标车组头>` 且该地块无 `FSCP-VEH` 行。

#### KI-162 第 89 轮：加"挡路者身份/状态"探针（待用户复测）

- 目的：把"**停稳后保留的前方预留**"与"**正在撤离中的残留预留**"分开——用户猜测
  后者（前车尚未完全撤离耦合机车的路径，机车就尝试预留而失败），两者在旧日志里
  只能看到 `owner=N`，无法区分。
- 落点：`src/pathfinder/yapf/yapf_rail.cpp` 的 `FindSafeCouplePositionProc` 中
  `fail=tryReserve` 分支（`FSCP-RES` 行扩展），另加 `#include "../../pbs.h"`。
  仅 .cpp 改动，增量编译合法。
- 新字段（`FSCP-RES` 行内）：
  `otile/odir` = 挡路者链头所在地块与朝向；`ospd` = 其 `cur_speed`；
  `ostop` = `VehState::Stopped`；`ord/oroi` = 其当前订单类型与 real index；
  `oend/oendtd` = `FollowTrainReservation()` 得到的**该车自己预留的终点**；
  `othru` = `TrainReservationPassesThroughTile(owner, tile)`（其预留是否真穿过当前地块）；
  `octgt/ocplok` = `R3RIsCoupleTarget(owner)` / `R3RCoupleAllowed(本车, owner)`；
  `cpl` = 耦合机车（`GOTO_COUPLE` 方）所在地块。
- 判据（下一次复测即可定性）：
  1. `ostop=1` ＋ `othru=1` ＋ `octgt=1` ⇒ 挡路者是**已停稳的耦合目标车组**，它只是
     还持有穿过该地块的前方预留 ⇒ **不是"撤离中的时序问题"**，而是"目标的预留长期
     占着交叉轨 / 耦合路径不被允许穿过目标的预留"；
  2. `ostop=0`，或 `oend` 在连续几行里逐次向北收缩 ⇒ 确为**撤离中的残留预留**（用户猜测）
     ⇒ 应在耦合寻路侧加"等目标预留收缩后再预留"的重试/等待；
  3. `octgt=0` ⇒ 挡路者不是本车的耦合目标（第三方车），拒绝本属正常（第 88 轮已区分）。
- 构建：增量 `_tmp_inc_build.cmd` → `build\R3R_incbuild.done=EXIT_CODE=0`，
  `build\openttd.exe` @ 2026-09-22 08:01:58（50 798 592 B）。

#### KI-162 第 89 轮实测结论（探针已定性，修复未做）

- 探针证据（`build\R3R_debug.log` 第 829-948 行，5 次 `FSCP-RES`）：
  - 前 3 次（`830/861/894`）：`owner=1 otile=60,51 odir=3 ospd=0 ostop=0
    ord=17 oroi=3 oend=60,54 oendtd=1 othru=1 octgt=1 ocplok=1 cpl=58,54`
    —— `ord=17`=`OT_WAIT_COUPLE`，车组（veh 1..6）头在 `60,51`、**速度 0**，
    却把自己的预留铺到 **`60,54`（车体以南 3 格）**；`octgt/ocplok=1` 说明它
    正是本车的合法耦合目标。该地块上**没有车体**（无 `FSCP-VEH` 行）。
  - 后 2 次（`920/946`）：`owner=0 otile=60,53 ospd=77/60 ord=6 octgt=0`
    —— 即用户所指的"前方那台车"（`OT_GOTO_WAYPOINT`）正从 `60,53` 向南驶离，
    但它只是**短暂**因素：它走后（`1010` 行于 `60,54` 掉头）`60,53` 的 `Y`
    预留**仍由静止的车组持有**。
- 判定：命中第 89 轮判据 1（非判据 2），**用户的"前车没撤完"只解释后 2 次，
  不是根因**；根因是"等待被耦合的车组自己持有朝南的前方预留"。
- 机制链：车组头朝南 ⇒ JGRPP 原生 lookahead 前方预留在 `60,52..60,54` 的 `Y`
  （`RESERVECONSIST` 只给车体 `60,48..60,51` 预留，故该段预留不是 R3R 放的）
  ⇒ 耦合机车 `veh=7` 从库 `58,54` 出来必须在 `60,53` 走 `UPPER(0x4)`
  ⇒ `TracksOverlap(Y|UPPER)` 交叉，`TryReserveRailTrack` 失败
  ⇒ `FSCP ... fail=tryReserve want=2 res=0x2` → `CPL-SAFE-FAIL`
  ⇒ `CPL-TRACE-RESULT best=0`（无安全耦合位置），机车留在库里不动。
- 修复候选：**(A)** 车组进入 `OT_WAIT_COUPLE` 且停稳时把 lookahead 预留收缩到
  车体范围（`SetTrainReservationLookaheadEnd`/`AdvanceTrainReservationLookaheadEnd`），
  等待被挂的车本就不会自行前进，前方 3 格预留无意义 —— 倾向此方案；
  **(B)** 耦合寻路把"目标自己的前方预留地块"视为可通行且不预留
  （FSCP else 分支 + 最终预留阶段都要放行，配合 `dontReserve`），改动面更大，
  且 PBS 交叉轨预留无法共存，需整条链路一致。
#### KI-162 第 90 轮订正（**第 89 轮的机制结论与「方案 A」一并作废**）

- **用户质疑正确：车底（`veh=1..6`，`WAIT_COUPLE`，速度 0）没有、也不该有前方预留。**
  全日志（`build\R3R_debug.log`）中 `60,53` 只出现 4 次，全部是本车 `veh=7` 的耦合试探
  （`844/883/909/934`）；车组的 `RESERVECONSIST` 永远只有 `60,48..60,51`（自己的车体），
  **从未**覆盖 `60,52 / 60,53 / 60,54`。
- **第 89 轮误读的根因＝探针 API 语义用错**：`GetTrainForReservation(tile, track)`
  （`src/pbs.cpp:1393`）**不是**"查这块轨道的预留登记者"，而是"沿该轨道的预留路径
  （`FollowReservation`，忽略单向信号）向两端走，再用 `CheckTrainsOnTrack` 找**路径尽头
  停着的车**"，并且对站台格还有"扫描整条站台找车"的特例（`pbs.cpp:1414-1421`）。于是从
  `60,53` 的 `Y` 预留出发，路径尽头撞上停在站台上的 `veh=1`，探针就打出 `owner=1` ——
  那只是"预留链末端站着的车"，**不是预留的主人**。同理 `FSCP-RES` 的 `oend=60,54` 是
  `FollowTrainReservation(oh)` 从 `veh=1` 站位沿**别人的**预留走到的尽头，并不代表
  `veh=1` 的预留铺到了 `60,54`。
- **真凶＝`veh=0`（另一台机车）的行驶前瞻预留**：`veh=0` 在 `60,51` 站台解挂后持
  `ord=6`（`OT_GOTO_WAYPOINT`）、`dir=3` 向南经 `60,52 → 60,53 → 60,54` 出站再掉头
  （`803 CRT veh=0 order=6 dir=3 origin=60,52 found=1`、`999 REVERSEDIR veh=0 tile=60,54`、
  `1003 DEPOT-ARR veh=0 tx=60 ty=54 destTx=60 destTy=44`），其前瞻预留覆盖 `60,52..60,54`
  的 `Y`。**铁证**：同一现象在 `934-936` 行给出 `owner=0 ospd=68 ord=6` 并且
  `FSCP-VEH tile=60,53 veh=0 track=0x2 dir=3 front=1` —— 那一刻 `60,53` 的 `Y` 上
  **真的站着 `veh=0`**；同一个 tile 的同一份预留，前后两条日志分别报 `owner=1` 与
  `owner=0`，本身就说明 `owner` 字段不能当预留归属用。
- 因此 `60,53` 的冲突是**正常行车冲突**（两台车抢同一道岔地块），不是"被耦合目标自己的
  预留挡住自己"；`veh=0` 当时正在移动（`spd=68`），随后在 `60,54` 掉头进库，`60,53` 会
  自行让出。日志到 `1010` 行结束，**没有任何证据表明 `veh=7` 最终失败或锁死**。
- 处置：第 90 轮已**回滚** `src/train_cmd.cpp` 中那段 11 行 `FreeTrainTrackReservation`
  （它建立在一个错误前提上；留着只会误导后续诊断）。同时作废第 89 轮"命中判据 1
  （车组自己持有朝南的前方预留）"的判定与"机制链"整段。
- 下一步（待复测）：确认 `veh=0` 让出 `60,53` 后 `veh=7` 能否走到
  `CPL-RESERVE reserved=1 → COUPLE-OK`；**若确实长时间失败**，方向应是"把正在撤离/占道
  的他车预留当作暂时阻塞并等待重试"（第 89 轮判据 2 的方向），而**不是**回头去清目标
  车组的预留。
- 探针口径修正（下轮起）：`FSCP-RES owner=` 应按"预留登记者"理解前先自查——该字段来自
  `GetTrainForReservation`，仅表示预留路径尽头所停的车；判断归属要看 `FSCP-VEH` 行
  （该 tile 上真有车）与车组自身的 `RESERVECONSIST` 覆盖范围。
- 状态＝**未修（第 90 轮：判为正常行车冲突，待复测；第 88/89 轮"车组自己持有前瞻预留"
  的结论与"方案 A"作废）**。严重度：中（若复测确认长时间锁死）／低（若只是短暂等待）。

#### KI-162 第 91 轮：新增"真正预留者"探针 `FSCP-REACH`（**第 91 轮实测：判定为正常行车冲突，非缺陷**）

- 目的：`FSCP-RES owner=` 已被证实**不能**表示预留归属（第 90 轮：它来自 `GetTrainForReservation`，
  即"预留路径尽头所停的车"）。本轮改为直接回答"**哪条链自己的预留真的经过该地块**"。
- 落点：`src/pathfinder/yapf/yapf_rail.cpp`，`FindSafeCouplePositionProc` 的 `fail=tryReserve`
  分支内、`FSCP-RES` 循环之后新增一个块：遍历 `Train::Iterate()`，只取链头（`First() == this`），
  用既有 helper `TrainReservationPassesThroughTile(tr, tile)`（沿该链 own reservation 枚举）筛选，
  命中即打印 `FSCP-OWNER`；一条链都不命中时打印一次 `FSCP-OWNER-NONE`（函数内 `static` 去重，
  避免每 tick 刷屏）。
- 字段：`veh / ord / roi / pos / dir / spd / stopped / seg / cplok / ctgt / end / endtd / isGtr / cpl`。
  其中 `isGtr=1` 表示该车恰好也是 `GetTrainForReservation` 会返回的那辆车——用来直接对照旧
  `owner=` 字段，确认两者差异。
- 判读（三种互斥结果）：
  1. `FSCP-OWNER veh=0 ... pos=60,52/60,53` ⇒ 该 `Y` 预留确实属于出站机车 `veh=0`，
     第 90 轮结论成立，`veh=0` 让开后应出现 `CPL-RESERVE reserved=1 → COUPLE-OK`；
  2. `FSCP-OWNER veh=1`（车组头）⇒ 车组自持前瞻预留，第 89 轮结论复活，按"方案 A
     （等车收缩 lookahead 预留）"修；
  3. `FSCP-OWNER-NONE` 且该 tile 无 `FSCP-VEH` 行 ⇒ 预留是**没有任何活车认领的残留**
     （第 88/89/90 轮均未覆盖的第三种可能），应在 lookahead 收缩/拆链路径上查残留清理。
- 【同轮订正】上述判读 2 **已被实测证伪**：`FSCP-OWNER` 本身也不能区分归属（原因见下），
  `veh=1` 命中只是"蹭"了 `veh=0` 的预留，并不代表车组自持前瞻预留，**不要**据此走方案 A；
  判读 3 的 `REACH-NONE` 仍然有效。
- 构建：增量 `_tmp_inc_build.cmd` → `build\R3R_incbuild.done=EXIT_CODE=0`，
  `build\openttd.exe` @ 2026-09-22 09:06（50 798 592 B）；`findstr FSCP-OWNER` 命中 exe。
- **实测结论（`build\R3R_debug.log`，2026-09-22 09:1x，共 952 行）＝判定为非缺陷（正常行车冲突）**：
  - 5 次 `CPL-SAFE-FAIL`（`755/790/817/851/878` 行；同 tick 的 `FSCP-RES` 在
    `752/787/814/848/875` 行，全部在 `60,53`）期间占道的 `veh=0` 是一台
    **第三方机车**（`ord=6`＝`OT_GOTO_WAYPOINT`、`dir=3` 向南、`ctgt=0` 不是本车耦合目标），
    从站台 `60,51` 起步南下经 `60,52` 到 `60,54` 掉头进 `60,44` 的车库
    （日志末尾 `REVERSEDIR veh=0 tile=60,54`、`DEPOT-ARR veh=0 destTx=60 destTy=44`）；
    速度轨迹 `spd=8→45→72→76→58→0` 说明它一直在走，不是停死。
  - `848/850`、`875/877` 行给了 `FSCP-VEH tile=60,53 veh=0` ＋ `FSCP-RES ... owner=0 otile=60,53`：
    `60,53` 上**物理站着 `veh=0`**，预留链上游端也落在 `60,53`（即它脚下），归属明确。
  - **决定性证据**：`932 CPL-RESERVE veh=7 reserved=1 next=0`——`veh=0` 一离开 `60,53` 推进到
    `60,54`，`veh=7` 的下一次尝试**立即预留成功**，其后只有 `veh=0` 掉头、进库两行，
    无任何失败或锁死。第 90 轮的预测被证实。
  - 结论：耦合寻路要求穿过交叉轨地块是**正常**的（`60,53` 的 `Y` 是唯一通路），冲突随占道车
    离开自行消解；第 88/89 轮"车组自持前瞻预留、应按方案 A 收缩预留"的结论**再次作废**。
- **探针口径第二次修正（本轮订正，务必记住）**：`FSCP-OWNER` 改名 `FSCP-REACH`，因为
  `TrainReservationPassesThroughTile()` **同样不能**表示归属——`pbs.cpp:776 FollowReservationEnumerate`
  只沿"**连续有预留的轨道**"走（`pbs.cpp:822` 用 `GetReservedTrackbits` 判断，预留位是全局的、
  不带车主），于是一辆停在**别人预留段后面**的车会顺着别人的预留"走"到该地块并被列入命中。
  实测：`753/754` 同时列出 `veh=0` 与 `veh=1`，而车组 `veh=1` 自己的预留只覆盖 `60,48..60,51`
  （紧随的 `RESERVECONSIST` 行），它命中 `60,53` 纯属"蹭"了 `veh=0` 的预留。
  ⇒ **判归属只能用 `FSCP-VEH`（该 tile 上真有车）＋ 该车 `ord/dir/spd`。**
  `FSCP-REACH-NONE`（无任何链的连续预留可达该 tile）才可能表示陈留/孤儿预留。
- 探针澄清：`yapf_destrail.hpp:435-444` 的 `hasResButNoTrain` 打印**有歧义**——`has_res == 0` 的
  站台格同样会走到 `t == nullptr` 分支而打印（站台格 `60,48` / `60,61` 从日志第 19 行起就反复
  出现，且都伴随 `hasRes=0`；该打印本身也在刷屏，属 KI-14 范畴），**不能**据此判定孤儿预留。
- 构建：增量 `_tmp_inc_build.cmd` → `build\R3R_incbuild.done=EXIT_CODE=0`；
  `build\openttd.exe` @ 2026-09-22 09:18（50 798 592 B）；`findstr FSCP-REACH` 命中 exe。
- 遗留：本轮日志止于 `932 CPL-RESERVE reserved=1`，**未录到 `COUPLE-OK`** 及"耦合后能否正常
  出站"，需下一次复测确认收尾。
- 状态＝**已核实非缺陷（第 91 轮实测：正常行车冲突，占道车离开后自动预留成功）**。严重度：低（数秒等待）。

### 本轮待游戏内目测

1. 本场景（库内 `58,54` 持 `GOTO_COUPLE` → 平台 `60,48` 的 `WAIT_COUPLE` 车列）
   机车应自行出库、经 `60,54` 一线靠向 `60,48`，最终与车列**物理耦合成功**；
2. 耦合瞬间之前的 `CPL-SAFE-FAIL` 只允许在**真有别的车**压在路径上时出现
   （判据：有 `FSCP-VEH` 行 / `owner` 不是本车组的耦合目标）；**第 90 轮订正**：判据
   应改为"有 `FSCP-VEH` 行（该 tile 上真有车）"，因为静止的 `WAIT_COUPLE` 车组本就
   不可能持有前瞻预留（`FSCP-RES owner=` 只是预留路径尽头的车，会误报成车组）；
3. 站台端头 / 拆轨地块的耦合预留不再触发 `pbs.cpp:144` 断言（KI-160）；
4. 回归：车站/线路上的耦合（不涉及 depot 起点）`CPL-*` 轨迹与第 87 轮一致。

### KI-162 第 92 轮（新日志复测：同形复现，结论不变；补两条「读日志口径」订正）

- 新证据（`build\R3R_debug.log`，2026-09-22 09:23，共 1052 行）：`CPL-PATHFOUND veh=7 found=0` ×29
  （行 37..769）→ `found=1` ×6（846..996）→ `CPL-SAFE-FAIL veh=7` ×5（854..977）→
  `CPL-RESERVE veh=7 reserved=1 next=0`（1031）。与第 91 轮同形，**再次判定为非缺陷**。
- 口径订正 ①（`found=0` 阶段的真因，本轮新定，**不是**预留问题）：`PFD tile=60,48` 共 210 行，
  其中 `hasRes=1` 从**行 340** 起就出现，但直到**行 835**才第一次出现可接受形式
  `t=1 co=1 wc=1 ordType=17 fit=1`（`t=1` 是平台扫描找到的链头 `veh=1`，与紧随其后
  `PFD tile=60,48 td=9 trackbits=0x2 hasRes=1` 并存）。佐证：`SKIP-STOPPED veh=1` 只在
  行 2..235 出现，起始 `tile=59,42`，末尾已到 `tile=60,51` —— 即**车列头当时还在开往平台途中**。
  ⇒ 那 29 次 `found=0` 是"目标车列尚未就位、YAPF 看不到任何合法耦合目标"，此时
  `train_cmd.cpp:8665` 每次都会 `MarkTrainAsStuck(veh=7)`（PERF-TICK 最后一个活动窗口
  `stk=19.1/s` 即此计数）。**不要**把它读成"寻路 bug / 预留被自己挡住"。
- 口径订正 ②（**PERF 尾部全 0 ≠ 卡死，更不是崩溃**，重要，务必先排除）：`R3RPerfFrameTick()`
  挂在 `window.cpp:3415`（`UpdateWindows()` 内，**每渲染帧**一次），与游戏 tick 无关。游戏一旦暂停
  （暂停键 / 失焦自动暂停 / 认输提示），渲染帧照走、`frames=128 fps≈61` 照打，但所有 tick 计数器
  必然全 0。实测：最后一个活动窗口＝`R3R_perf.log:8157`（`loco=20.0/s plat=6.7/s resv=6.7/s
  stk=3.3/s mov=1.9/s dbgWrite=2.9/s`），其后自 8159 起全部 `loco=0.0/s mov=0.0/s
  tryCouple=0.0/s dbgWrite=0.0/s`。两个可交叉验证的伪影：`glTrains=0.78ms` 在活动窗口与暂停窗口
  **完全相同**（陈旧平滑值，**不能**当"trains tick 仍在跑"的证据）；`nl` 从活动时 ~31ms/s 跳到
  暂停时 ~48ms/s（因 `loco_ns=0`，`nl = R3RGetTrainsMs()*fps - loco_ms` 退化）。
  ⇒ 见到"全 0 + fps 60"先查游戏是否暂停，再谈卡死。
- 良性项复述：`RESERVECONSIST ... ok=0 resNow=1`（`train_cmd.cpp:9270`）＝该 tile 已（被自己）
  预留，`TryReserveRailTrack` 对已预留返回 false，`resNow` 是调用后复查 `HasReservedTracks` ⇒ 正常。
- 状态＝**已核实非缺陷（第 92 轮：同形复现第 91 轮结论）**。严重度：低。
- 遗留（不变）：日志仍止于 `CPL-RESERVE reserved=1`，**未录到 `COUPLE-OK`** 与"出库→靠拢→
  物理耦合"收尾；下次复测**不要暂停**，让帧跑满再抓 `R3R_debug.log` 尾部。

### KI-162 第 92 轮补记（**状态订正：降级为"机制非缺陷，但应有现象未验证"**）

- 玩家实报"没看到应有现象"，复核确认**玩家是对的**：本日志里 veh=7 的"出库→靠拢→耦合"零证据。
  `COUPLE-OK` 全日志仅 1 次（行 269，早前另一次耦合）；veh=7 侧只有
  `CPL-RESERVE veh=7 reserved=1 next=0`（行 1031，全日志唯一）。1031 到 EOF 仅 20 行：
  `CHKRES`（硬编码 `38,37..39,30`，与本场景无关，恒 `0x0`，**探针失效误导，待清理**）、
  车列自身 `RESERVECONSIST`×6、`DRAW-STATION-NORES`、`veh=0` 进库、再一轮 `RESERVECONSIST`×6；
  **无** `CT veh=7 <新tile>`、**无** `DEPOT-ARR veh=7 tileDepot=0`、**无** `FOLDCHK`/`COUPLE-OK`。
- 硬证据（第 92 轮复核新增）：`CT veh=7`（`train_cmd.cpp:8644`，`TrainLocoHandler` 在排程含 GOTO_COUPLE 时
  逐次打印，**未过 edge-gate**）全日志共 **35 次**，坐标**恒为 `59,54`**（仅 `stuck` 0/1 两态），行号 14..978
  ⇒ 本场景机车在整个可观测期内**一步未挪**，`CPL-RESERVE` 是它唯一推进到的节点。`FOLDCHK`/`COUPLE-OK`
  各 1 次且落在行 267/269（距末尾 700+ 行，**非本场景**）。末尾 20 行仍含 `RESERVECONSIST`×6×2 轮
  （`ok=0 resNow=1`＝该格已被自己预留，**正常**，勿判失败）、`DRAW-STATION-NORES tile=60,58`、
  `REVERSEDIR veh=0 tile=60,54 dir=3 order=6` → `REVERSEDONE db=1 mvfront=0`、
  `DEPOT-ARR veh=0 destTx=60 destTy=44`（＝另一台 veh=0 在做订单调向＋回库，与 veh=7 无关）。
- 真因＝观察窗口被切断（新口径：`PERF-TICK` 的 `mov` = `r3r_mov_iters`，>0 才有真位移；
  `loco>0` 只代表 handler 被调用）：8126 恢复 → **8128–8142（≈17 s）`loco≈228/s` 但
  `mov=ctrl=coupleH=stk=0`（仿真在跑、全车静止、耦合逻辑零调用）** → 8144–8150 `mov=120→172/s`
  （车列/veh=0 行驶）→ 8152–8158 `coupleH=20.6/s`、`stk=19.1/s`（29×found=0 与 5×SAFE-FAIL 全压
  在这 1–2 窗口）→ 末了 `CPL-RESERVE reserved=1` → **8160–8208（25 窗口 ≈52 s）全计数为 0
  ＝暂停到 EOF**。⇒ 预留成功后仿真只再跑约 2–4 秒即停，**"预留后车是否动"在本日志是空白，不是反证**。
- 判据澄清：`CPL-*`/`CT veh=`/`PFD` 未过 edge-gate（每 tick 打），`RESERVECONSIST`/
  `SKIP-STOPPED`/`FOLDCHK` 走 `R3RDbgEdge`（≈1 行/tag/128 帧）。故"1031 后无新行"不能当
  "车没动"的判据，硬判据只有 `PERF-TICK` 的 `loco=/mov=/ctrl=`。
- 全局（同一 perf.log，4104 窗口、仅 499 窗口 `loco>0`）：`coupleH>0` 99 窗口、`stk>0` 24 窗口
  （含连续多窗口 `stk≈38/s` 的簇）⇒ 耦合尝试＋每 tick 标 stuck 是**跨分钟**长期行为。
- 复测协议：连续跑 ≥60 s 不暂停 → 先验 `loco>0 && mov>0` → 再找
  `CT veh=7 <新tile>` → `DEPOT-ARR veh=7 tileDepot=0` → `FOLDCHK` → `COUPLE-OK`。
  若 60 s 内 `loco>0 && mov>0` 而 4 条一条不出、只有 `stk>0` 反复 ⇒ 转"预留成功但车未被驱动"
  缺陷（查 `TrainLocoHandler` 是否消费 `CTTRF_RESERVATION_MADE` 的 `TrackdirToTrack(0)`=TRACK_X
  与库出口轨道是否一致；与 KI-161 的 trackdir 语义结论不冲突）。若现 `loco>0 && mov=0` 长时段
  ⇒ 单独记"仿真在跑但全车静止、耦合零调用"现象。
- 状态＝**机制非缺陷，应有现象未验证（第 92 轮补记）**。严重度：中（若复测证实长时不动）／低。

### KI-163（**已修，高**）：`reach()`/`rp()` 用"轴向盲扫 32 格"判"是否已到达车底"，命中同轴线的无关地块 → 预留开向错误方向

- 现场（`build\R3R_debug.log`，2026-09-22 新日志，1758 行）：veh=7 在库内 `58,54` 持 `orderType=16`
  (GOTO_COUPLE)，目标车底在 `60,48..60,51`（`tg=60,51`）。但 `RP cur=` 逐格显示预留一路**向南**：
  `58,54 → 59,54 → 59,55 → 59,56..59,63 → 59,57 → 60,57`，停在 `(60,58)` 另一个车站门口；
  `CPL-BEST tile=60,48 td=9 cost=6613`（正确终点）被丢弃，最终 `CPL-RESERVE veh=7 reserved=1 next=0`，
  `best_td=0`。玩家两条观察（①`(60,51)-(60,54)` 之间的神秘占用 ②veh=7 预留去了错的地方）**均成立**；
  此前一轮引用的 `59,54` 是出库后第一格，不是起点（`CPL-ENTRY veh=7 tile=58,54` / `CPL-ORIGIN
  origin=58,54 td=8 vehDir=5 vehTd=8` / `SKIP-STOPPED veh=7 tile=58,54` 三处互证）。
- 根因（`src/pathfinder/yapf/yapf_rail.cpp` 的 `reach()`/`rp()` 两个 R3R 溯源闭包）：判"`tg` 是否在被
  `TrackFollower` 一步跳过的站台段上"时，沿站台轴 `TileOffsByAxis` **正反两个方向各盲扫 32 格**，
  只做坐标加减：既不校验中间格是否为同一站台、是否为轨道，也**不限制在跟随器真正跳过的范围内**。
  现场 `cur=60,57`、`tg=60,51` 同在 `x=60`，`-delta`（向北）走 6 格就撞上 `tg` → 误判"已到达" →
  回溯立即停在该分支上（`best_td` = 那条向南的分支）→ `rp()` 顺带把向南的 13 格预留下去。
  注意 `TrackFollower` 的跳段方向**恒为 `exitdir`**（`follow_track.hpp:414-425`，`CanEnterNewTile`
  内 `new_tile += step * (length-1)`，`tiles_skipped = length-1`），与"轴向盲扫"毫无关系。
- 修复（**仅 `yapf_rail.cpp`，未碰任何 `src/*.h`，符合 KI-15 增量合规**）：新增文件内 `static`
  辅助函数 `R3RSkippedStationStretchContains(old_tile, new_tile, exitdir, tiles_skipped, target)`，
  用跟随器自身状态**精确重建**被跳过的站台段：步长 = `TileOffsByDiagDir(exitdir)`，格数 =
  `tiles_skipped + 1`，逐格要求 `IsRailStationTile`（`GetPlatformLength` 的
  `IsCompatibleTrainStationTile` 已保证同站、同轴、兼容轨型），并在 `new_tile` 处终止（循环恒有界）。
  `reach()` 与 `rp()` 均改为调用它；`rp()` 中"补齐车头到车底之间站台预留"的循环也改用同一 `step`
  （原来用轴向 `delta`，方向同样可能是反的），并去掉原来那两段 `for (int i = 0; i < 32; ++i)`。
- 构建：`_tmp_inc_build.cmd` → `build\R3R_incbuild.done=EXIT_CODE=0`；`build\openttd.exe`
  @ 2026-09-22 10:07:51（覆盖前 09:18 版）；仅重编 `yapf_rail.cpp.obj` 后链接。
- 状态＝**已修（编译通过；2026-09-22 收口：用户确认"整个换挂的实验很成功"，本轮未再见本条反例）**。
  严重度：高（预留开向错误方向 + 每次耦合都多标 stuck，
  直接压制 KI-162 的"出库→靠拢→耦合"收尾）。
- 复测判据：同场景（库内 `58,54` 出发 → 平台 `60,48` 的 `WAIT_COUPLE` 车列）应看到 `RP cur=`
  朝 `60,54 → 60,53 … 60,48` 一线（正确方向）推进，不再出现 `59,5x → 59,6x` 的向南长串；
  `CPL-BEST` 的终点（`60,48`）与最终 `best_td`/`RP` 方向一致；进而接上 KI-162 遗留的
  `FOLDCHK` → `COUPLE-OK` 收尾验证。
- 关联：本条目解释了 KI-162 第 92 轮补记中"`CPL-RESERVE` 后车不动"的一部分原因（预留方向错误 →
  机车被引向错站台），故 KI-162 的"应有现象未验证"需在本修复后重测再定性。




### KI-164（**已修，高**）：`OT_GOTO_STATION` 到达判定只看"是否在任意车站地块"，耦合点恰是另一车站 → 挂上即解挂

- 现场（`build\R3R_debug.log`，2026-09-22）：`COUPLE-OK loco=7 rear=6 consist=1 co=1 real=4 type=1 tx=60 ty=51`
  （第 1186 行），仅 33 行后即 `DECOUPLE-FIRE consist=7 tx=60 ty=51 real=4`（第 1219 行）——**同一地块
  `60,51`、同一真实索引 `real=4`**，随后 `LOCO-AFTER-DECOUPLE veh=7 curType=0 real=1 tile=60,51`（第 1241 行）
  机车独自开走。玩家观察："机车挂上车底后在同一格立刻被解挂"。
- 订单现场（`ORD-AFTER-COUPLE head=7 n=12 real=4`，第 1194-1206 行；类型映射见 `order_type.h:81-95`，
  `15=OT_DECOUPLE`/`16=OT_GOTO_COUPLE`/`17=OT_WAIT_COUPLE`）：`idx0 type=17`（等待点）→ `idx1 type=1 dest=0`
  → `idx2 type=15`（DECOUPLE）→ `idx3 type=17`（本次被挂的等待点）→ **`idx4 type=1 dest=2`（当前真实订单：
  去 2 号车站）** → `idx5 type=15`（DECOUPLE）→ …。即车底是一条 `WAIT_COUPLE/GOTO_STATION/DECOUPLE`
  循环排程，本次在 **0 号车站**的等待点 `60,51` 被挂上，真实索引随之跳到"去 **2 号车站**"。
- 根因（`src/train_cmd.cpp` 解挂钩子前的到达判定，~11370）：`OT_GOTO_STATION` 分支只写
  `IsTileType(consist->tile, TileType::Station)`，**不比较是哪一座车站**。耦合点 `60,51` 本身是 0 号车站的
  地块，于是"去 2 号车站"的订单被判为"已到达"→ 紧随其后的 `idx5` DECOUPLE 当 tick 触发，把刚拼上的
  车底又解下去。相邻的 `OT_GOTO_DEPOT` 分支早在 2026-09-04 就因同型 bug（在 42,28 旧库里被判为
  "已到达 38,27"）改成按 depot 索引比较，车站分支当时漏改同一处理。
- 修复（**仅 `train_cmd.cpp`，未碰任何 `src/*.h`，符合 KI-15 增量合规**）：车站分支改为索引比较，与 depot
  分支对称：`at_order_dest = IsTileType(consist->tile, TileType::Station) && cur_real->GetDestination().ToStationID() == GetStationIndex(consist->tile);`
  同一车站的多格站台返回同一 `StationID`，正常到站不受影响；目标车站已删除（`INVALID_STATION`）时不再
  算作到达。`Order::GetDestination().ToStationID()` 见 `order_type.h:46`。
- 本轮附带清理探针：`vehicle_sl.cpp` 的 `VEHS_ENTER`/`VEHS_DONE`/`r3r_veh_count`/`R3R_vehsraw.bin` 字节转储/
  `LOADCENSUS-*`；`saveload.cpp` 的 `INVALID_REF_VEHICLE`/`CHUNKLOAD`/`AFTER_LOADCHUNKS`/`BEFORE_PTRS`；
  `pbs.cpp` 的 `UNRESERVE-STATION`；`yapf_rail.cpp` 的 `DEPOTCHK`(硬编码 38,27)/`CPL-FOLLOW`/`CHKRES`；
  `yapf_destrail.hpp` 的 `PFD-SCAN`/`hasResButNoTrain`。保留 `REACH`/`RP`/`CPL-*`/`FSCP-*`/`FOLDCHK`/`RT`
  （KI-163 复测用）、`SL-STNN-PURGE`（KI-95）、`SLERROR`、`R3RDUMP`（仅读档一次 + 受 `R3RFopenDbg` 闸门）。
- 构建：`_tmp_inc_build.cmd` → `build\R3R_incbuild.done=EXIT_CODE=0`，日志末行 `[7/7] Linking CXX executable
  openttd.exe`；`build\openttd.exe` @ 2026-09-22 20:45:49（50 791 936 B）。6 个改动单元源文件 mtime 均晚于
  对应 obj，增量重编覆盖完整；`yapf_destrail.hpp` 唯一包含者是 `yapf_rail.cpp`（全仓 grep 确认），无需全量重编。
- 状态＝**已修（编译通过；2026-09-22 收口：用户确认"整个换挂的实验很成功"，本轮未再见"挂上即解挂"）**。
  严重度：高（挂车被立刻撤销，R3R 挂车/解挂全流程不可用）。
- 复测判据：同场景（0 号车站 `60,51` 等待点被机车 7 挂上）应看到 `COUPLE-OK ... real=4` 之后**不再**出现
  同地块 `DECOUPLE-FIRE`；列车应驶向 2 号车站，抵达**该站**后才在 `idx5` 触发解挂。若仍在 0 号车站触发，
  说明该站 `StationID` 与订单 dest 意外相等（等待点与目的地本就是同一站），需打印两者编号再定。
- 关联：与 KI-162/KI-163（耦合寻路 / 预留方向）同场景，本条是其下游的"挂上就掉"——KI-163 修复后本条
  才暴露为一个独立的到达判定缺陷。备忘：`R3R_decouple_after_couple_memo.md`。




### KI-165（**已修，中**）：挂车候选不校验订单目的地 → 机车开去"没有挂车命令"的另一个车站并挂上车底

- 现场（`build\R3R_debug.log`，2026-09-22 21:11 版，1338 行）：机车 `veh=0` 的订单里**没有任何指向
  2 号车站 `(60,58)-(60,61)` 的挂车命令**，它却开过去挂上了那里的等待车底：
  `COUPLE-OK loco=0 rear=1 consist=1 co=1 real=7 type=1 tx=60 ty=58 x=968 y=933`（第 1098 行），
  前置 `[R3R] CPL-HIT site=open loco=0 v=6 len_v=8 len_mf=8 min_diff=7 need=8 fit=8 dx=0 dy=7 maxd=7
  overlap=1 spd=80 dir=3`（第 1034 行）+ 随后的 `FOLDCHK`/`FLIP` 合并链（1039-1097 行）。
  该车第 99 行还在 0 号车站语境下自合并（`COUPLE-OK loco=0 rear=6 consist=1 real=1 tx=59 ty=42`），
  之后 `real=7` 的真实订单是 **`type=16`（`OT_GOTO_COUPLE`）且目的地＝0 号车站 `(60,48)-(60,51)`**；
  同一时刻 `veh=7` 反复 `[R3R] CPL-GATE reject=target-not-wait …` → `COUPLE-FAIL loco=7 order=16
  tx=58 ty=55`（另有一批 `act=7 tgt=0 aOrd=16 tOrd=1`），说明"目的地车站当时没有车底"。
- 订单语义（`order_gui.cpp` "go to and couple"）：`OrderClick_GoToCouple()` 之后玩家选站，落成
  `MakeGoToCouple(st->index, …)`（选库时 `MakeGoToCouple(depot_id, …, true)`），订单列表显示
  `STR_ORDER_GO_TO_COUPLE_STATION`/`…_DEPOT` —— **目的地车站/车库是订单的一部分**，注释原文
  "the destination is the station or depot the consist waits at"。
- 根因（候选解析层从未读订单目的地）：
  1. `src/pathfinder/yapf/yapf_destrail.hpp` 的 `CYapfDestinationTrainRailT::PfDetectDestination()`
     只要求"该格是车站/车库地块 **且** 上面有被预留的等待车底"，**不比较是哪一座站**；类里的
     `dest_order`（耦合机车订单的副本）只被当作 `dest_tile` 这一类 **A\* 估值**用，注释也写着
     "heuristic destination"。于是**全地图任何一座**停着等待车底的站台都成为合法终点，搜索自然
     取最近的那个。
  2. 到达闸门 `R3RCanCoupleNow()`（`train_cmd.cpp`，KI-60 的四条硬条件）同样**没有**目的地这一条；
     FSCP 回溯安全判定（`yapf_rail.cpp` 的 `CheckSafePositionOnNode`）也没有。
  3. 结果：寻路把机车引到 2 号车站站台（KI-163 修复后预留方向正确，但方向正确的**是错站**），
     随后 `CPL-HIT site=open` 的碰撞式耦合路径直接 `Couple()` 成功 → 玩家看到"去了没命令的车站挂车"。
- 修复（**代码改动只在 .cpp**：`couple_group.cpp` + `train_cmd.cpp`；`yapf_destrail.hpp` 仅加注释）：
  把"订单目的地"并入既有的**耦合许可** `R3RCoupleAllowed()`（`couple_group.cpp`）——它是三个解析层
  共同询问的唯一函数：①寻路目标判定 `yapf_destrail.hpp:449` ②FSCP 回溯安全判定 `yapf_rail.cpp:171`
  ③到达闸门 `train_cmd.cpp:7024`。新增规则（就地比较地块索引，与 KI-164 的到达判定同思路）：
  - 车站订单：候选**站在别座车站的地块上** → 拒绝；
  - 车库订单：候选**站在别座车库的地块上** → 拒绝；
  - 候选在车库内、或停在车库门口衔接轨（已不是车库地块）、或不在任何站台上 → 保持原有宽口径
    （尊重"车底常等在库门口轨道"与库内吸收这两条既有工作流）。
  这样"寻路规划的目标"与"最终执行的耦合"重新一致。闸门侧的 `reject=dest-mismatch` 由同一分支内
  的**仅供取标签**的镜像谓词 `R3RCoupleTargetAtOrderStation()`（`train_cmd.cpp`）决定，行为一律由
  `R3RCoupleAllowed()` 裁定，避免两处实现各自成为行为权威。
- 构建：`_tmp_inc_build.cmd` → `build\R3R_incbuild.done=EXIT_CODE=0`，末行 `[3/3] Linking CXX
  executable openttd.exe`；`build\openttd.exe` @ 2026-09-22 21:52:43（50 791 936 B）。
  mtime 核对：`train_cmd.cpp.obj` 21:33:58 > `train_cmd.cpp` 21:31:13；`couple_group.cpp.obj`
  21:35:18 > `couple_group.cpp` 21:34:21；`yapf_destrail.hpp` 本轮只加注释（无代码改动），但其唯一
  包含者 `yapf_rail.cpp.obj` 仍**强制删除后重编**（21:42:49）以彻底排除陈旧 obj 疑虑（沿用 KI-164
  已验证的"单包含者头文件不必全量重编"约定）。首次编译曾失败于 `couple_group.cpp` 缺
  `depot_map.h`/`station_map.h`（C3861），已补 include。
- 状态＝**已修（编译通过；2026-09-22 游戏内实测通过 + 用户确认"整个换挂的实验很成功"）**，已随
  稳定版 `r3r-stable-2026-09-22` 冻结（快照 `build\r3r_stable_2026-09-22\`）。严重度：中
  （无崩溃/丢数据，但会在玩家未下令的车站抢占车底，属行为不符）。
- 复测判据：同场景（`veh=0` 持"挂 0 号车站"的 `GOTO_COUPLE`，车底已被 `veh=7` 拉去 2 号车站）应
  **不再**出现 `CPL-HIT site=open loco=0 …`（第 1034 行型）与 `COUPLE-OK loco=0 … tx=60 ty=58`；
  预期改为"目的地车站没有车底 → 无候选 → `COUPLE-FAIL`（KI-14 边沿闸门控频）"，或车底真在 0 号
  车站时正常挂上。若出现 `[R3R] CPL-GATE reject=dest-mismatch`，即为本条过滤器生效的直接证据。
- 残留（本轮明确不做，另行立项）：
  (a) `CPL-HIT` 两条碰撞式耦合路径（`train_cmd.cpp` ~9391 `site=depot`、~9465 `site=open`）**故意
      不加过滤**：它们是"撞上就挂、别撞车"的兜底，若也按目的地拒绝，会把"挂错站"变成
      `TrainCrashed`。过滤器放在寻路+FSCP 两层后，机车正常情况下不会被引到别站车底上。
  (b) 目的地车站当时没有等待车底时，机车仍只会**原地等**（`COUPLE-FAIL`），不会"先开到目的站台再
      等"；后者需要"无车底的合法终点"这一寻路层改造，属更大的独立改动。
  (c) 过滤器看候选**链头**所在格；若一条车底恰好横跨两座相邻车站，仅凭链头可能误判（罕见）。
  (d) 老存档里若存在目的地未设置（`INVALID` 车站）的 `GOTO_COUPLE`，新规则会让它再也挂不上
      任何车站候选——该形态无法由 UI 产生，暂按"订单本身已损坏"处理。
