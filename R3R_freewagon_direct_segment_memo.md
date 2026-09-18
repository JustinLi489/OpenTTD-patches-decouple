# R3R free-wagon → segment 直连(阶段一,depot 工具层)备忘

决策日期:2026-09-09。用户逐条拍板,本文件为防失忆的决策/落点记录。

## 背景与目标
当前耦合身份有三级:自由车厢链(free wagon)→ consist(零功率假引擎头,可持 schedule/被耦合)→ segment(段,★ + 头尾假引擎)。
物理上 consist 与段头完全同构(车厢保留自身 engine_type 但 `SetEngine`+`SetFrontEngine`),consist 只是"未打 ★/无尾假引擎"的段。
用户拍板:取消 consist 这一概念层,系统只剩 **free wagon / segment 两级**;consist 的持 schedule/可耦合能力并入段。

## 三项拍板(2026-09-09)
1. **consist 取消**:彻底删除 `IsConsistGroup` 概念与 depot「设为头车/设为前车」按钮;升段时头车厢假引擎化,所有 IsConsistGroup 判定最终改为按段识别(本轮阶段一只动 depot 工具层)。
2. **ArticGroup 焊死:不做**。保留现状移动语义(★ 已提供整段拖动/出售),不新增把中间车厢打 `ArticGroupMember` 的逻辑(规避 refit/车辆列表/显示连坐风险)。
3. **降级一步到位**:段 → 散车厢一步(清 ★、清头尾假引擎、删 schedule/unit number,直接回自由车厢链)。
4. **本轮范围 = 阶段一 depot 工具层**:删「设为前车」按钮;`MakeSegment` 支持散车厢链一键直升(头尾假引擎+★);`Demote` 一步回散车;depot 拖动段到空行改调段化。couple/decouple/寻路暂时保留 consist 机制不动(内部短暂存在),留待阶段二统一。
5. 每次修改源码必须编译;本次改了 .h 与语言 txt ⇒ 必须全量重编(删全部 *.obj 再构建,规则见 R3R_crash_194959_mixed_obj_memo.md / 记忆 66636022)。

## 行为语义(定稿)
- 升级(Make segment,一键):散车厢链 / consist / 真引擎列车,凡整车停在 depot 中即可。
  散车厢链(front 为 FreeWagon)先做身份化(假引擎头 + unit number + group 簿记,原 `CmdSetAsFrontWagon` 的 execute 内容),再按既有路径拆 artic 组、打 ★(`SetSegmentFront`)、尾假引擎化(`SetSegmentTailFakeEngine`)、`ConsistChanged(CCF_ARRANGE)`。
- 降级(Demote,一键):
  - 纯车厢段(链内无真动力:无 `railveh_type != Wagon` 的引擎)→ 直接解散为松散车厢链:清 ★、清尾假引擎、Rearticulate、删 orders、release unit number、`CountVehicle(-1)`、`DestroyConsistGroup`(撤头假引擎)、`UpdateTrainGroupID`、`ConsistChanged`。
  - 含真机车的段 → 只撤段边界:清 ★、清尾假引擎、Rearticulate,保留 schedule/number(恢复为普通列车)。
  - 无 ★ 的 consist(仍由 couple/decouple 内部产生,阶段二前存在)→ 保持既有 dissolve 行为(回松散车厢链)。
- depot 拖动:零功率组被拖到空行后(front 失掉假引擎)→ 直接 `MakeSegment` 一步段化,不再走「设为前车」。

## 代码落点(阶段一,2026-09-09)
- `src/command_type.h`:enum 删 `SetAsFrontWagon`,Make/Demote 注释更新。
- `src/vehicle_cmd.h`:删 `DEF_CMD_TUPLE(Commands::SetAsFrontWagon, ...)`。
- `src/vehicle_cmd.cpp`:
  - 原 `CmdSetAsFrontWagon`(含前置注释)整体替换为静态 helper `R3RPromoteFreeWagonChainToFront(Train *)`(做 CreateConsistGroup + SetFrontEngine + unit number/freeunits/group/CountVehicle/InvalidateVehicleListWindows;仅作用于 IsFreeWagon 链头)。
  - `CmdMakeSegment`:校验放宽为 `if (!t->IsFreeWagon() && (!t->IsEngine() || !t->IsFrontEngine())) return ...`;execute 在 `R3RStopChainInDepot` 后、artic 拆分前,若 `t->IsFreeWagon()` 先调 `R3RPromoteFreeWagonChainToFront(t)`。
  - 新增静态 helper `R3RChainHasRealLocomotive(const Train *seg)`(段内扫描到下一 ★ 边界止;`v->IsEngine() && RailVehInfo(v->engine_type)->railveh_type != RailVehicleType::Wagon`)。
  - `CmdDemoteSegment`:doc 重写(三段语义);★ 段 execute 改为「有真引擎→保留列车只撤边界 / 纯车厢→一键解散回松散车厢链」两分支;无 ★ consist dissolve 分支保留不变。
- `src/widgets/depot_widget.h`:enum 删 `WID_D_SET_AS_FRONT_WAGON`,注释更新。
- `src/depot_gui.cpp`:
  - NWidget 布局删除「设为前车」按钮,工具行只剩 Make segment / Demote。
  - OnClick 两工具共享放置槽(循环列表改为 2 项)。
  - `OnVehicleSelect` 删 SetAsFrontWagon 分支;Make segment 注释改为「散车厢/编组/列车均一键升段」;Demote 注释更新。
  - `TrainDepotMoveSegment`:`dest==Invalid && !seg->IsEngine()` 时由 `Commands::SetAsFrontWagon` 改发 `Commands::MakeSegment`。
  - abort(点击别处取消工具)循环列表去掉该 widget。
- `src/lang/english.txt`、`src/lang/simplified_chinese.txt`:更新 Make/Demote 按钮与 tooltip 文案;SET_AS 字符串与 CAN_T_SET_AS 错误文案留在语言文件中(无引用,不动 `###length` 组结构以免 strgen 对齐风险)。

## 未动 / 待办(阶段二起)
- couple/decouple 内部仍 `CreateConsistGroup`/`DestroyConsistGroup`/`IsConsistGroup`(consist 作为内部等待态存在)。
- 未来 P7(记忆 17190446):「不成段车厢链拒绝 couple/uncouple」需在 couple/decouple 统一阶段补。
- IsConsistGroup 全部使用点(yapf/寻路/平台 waiter/depot 列表/engine.cpp 900 特殊引擎)换等价谓词与删除 consist_group 文件 ⇒ 后续对话分阶段做。
- 用户所有 R3R couple/decouple/artic 规则见既有 R3R_*.md;勿违反「不碰物理车辆顺序 / 不碰位置 direction 图像」两条禁忌。

## 阶段二批次进度(2026-09-09)
- 批次A 完成:engine.cpp StartupEngines 删除 CreateConsistGroupEngine 调用;consist_group.cpp 重写仅留 Create/Destroy/IsConsistGroup,IsConsistGroup=纯结构判定(IsEngine && railveh_type==Wagon)。engine.cpp 顶部 include consist_group.h 仍留待批次C 清。
- 批次B 完成(增量编译 EXITCODE=0,openttd.exe 已更新):GOTO_COUPLE 目标判定统一为「段形态(IsConsistGroup)或 primary+WAIT_COUPLE」,裸散车链一律拒(P7):
  ① train_cmd.cpp GetCouplePosition(~5003) 去掉 IsFreeWagon 接受;② TrainCoupleHandler depot 分支(~5061)去 IsFreeWagon;③ 8 邻格扫描(~5090-5091)改为统一谓词(原另需 WAIT_COUPLE 检查合并);④ yapf_rail.cpp FindSafeCouplePositionProc(~150) notWC 判定加 IsConsistGroup(best) 豁免;⑤ yapf_destrail.hpp(369/429/435)与平台 waiter(8934)本就一致未改。
- 验证过无回归面:解挂纯车厢后段=CreateConsistGroup(isCG),真引擎后段=primary+后续 WAIT_COUPLE,均在允许目标内;NormalizeTrainVehInDepot 出厂自动挂只扫 FreeWagon/FrontWagon 链,段形态是 primary 不会被扫,故未动(如需彻底拦散车再在 9009 调用点处理)。
- 批次C(2026-09-09 实施中,涉 .h 全量重编由 Task Scheduler 任务 R3R_batchC_rebuild 执行,脚本 build_batch_c.ps1,日志 build\R3R_batchC_full_rebuild.log):已删 src\consist_group.{h,cpp} 与 CMakeLists 条目;三函数迁 train.h 声明 + train_cmd.cpp(NormalizeTrainVehInDepot 之后)实现,更名 R3RCreateCarOnlyFormation / R3RDestroyCarOnlyFormation / R3RIsCarOnlyFormation(car-only formation=纯车厢零功率假引擎列车,即原"consist group"概念);全部使用点已改:train_cmd.cpp(Decouple 产出/COUPLE-OK 日志 token isCG→co/u_was_consist→u_was_caronly/GetCouplePosition/平台 waiter/CheckTrainStayInDepot 豁免/depot couple/8 邻格)、vehicle_cmd.cpp(R3RPromoteFreeWagonChainToFront/CmdDemoteSegment 两分支/clone 路径)、yapf_destrail.hpp(PFD-SCAN/PFD 日志 token cg→co/PfDetectDestination/站点扫描/TrainFitStation 注释)、yapf_rail.cpp(FSCP);include 清理:engine.cpp/order_cmd.cpp/order_gui.cpp/vehiclelist.cpp/vehicle_cmd.cpp/train_cmd.cpp/yapf_destrail.hpp;train.h 删除两个无引用死字段(consist_front_vehicle/virtual_train)与"R3R consist group"注释。lint 全清。重编通过后再更新本条。

## 编译记录
- 本次涉及 .h(command_type.h / vehicle_cmd.h / depot_widget.h)与语言 txt ⇒ 必须全量重编(删 build 下全部 *.obj 再 vcvars 构建,约 25-40 分钟)。
- 环境:先 `call "D:\VISUAL STUDIO\MAIN PACK\VC\Auxiliary\Build\vcvars64.bat"` 再 `cmake --build . --target openttd -- -j2`(Ninja);openttd.exe 在运行会 LNK1168,须先关进程。
- 启动方式:schtasks / schtasks.exe 手写引号在含空格路径(`sourcecode of JGRPP`)上易错(实测 `Invalid syntax /sc missing`),改用 PowerShell `Register-ScheduledTask`+`Start-ScheduledTask`(包装脚本 `build\_r3r_fullbuild_sched.ps1`,输出落 `_r3r_fullbuild_sched.log`),任务进程由 Task Scheduler 托管可存活本 shell。
- **2026-09-09 04:50 全量重编通过**:`CMAKE_EXIT=0 / BUILD_WRAPPER_EXIT=0`;关键 obj 时间戳均晚于本次源码改动(depot_gui.cpp.obj 04:34、vehicle_cmd.cpp.obj 04:46、strings.cpp.obj 04:44 —— 语言文件改动后 strings.cpp 已重编,exe 与新 .lng 版本号对齐);`openttd.exe` 04:50:38 链接完成,`_r3r_cmake.log` 无编译错误,5 个改动源文件 lint 归零。
