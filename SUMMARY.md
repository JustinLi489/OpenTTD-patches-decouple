# R3R 项目当前状态摘要(2026-09-06)

> 会话压缩前先读本文件。各专题细节见对应的 `R3R_*.md`。分支 `feature/decouple`,2026-09-06 已设稳定 checkpoint(见 git log),之后的新改动请勿覆盖。

## 0. 仓库状态
- 分支:`feature/decouple`,已设 checkpoint 提交(2026-09-06,源码+全部 R3R_*.md 备忘+本文件入库),领先 `origin/feature/decouple` 5 commits(未 push)。
- 未入库(有意保留):`R3R_tmp_build.cmd` / `R3R_tmp_repro.ps1` / `R3R_tmp_*.ps1` / `R3R_tmp_buildtest.log`(本地构建/复现工具,勿误删;`R3R_tmp_build.cmd` 与 `R3R_tmp_repro.ps1` 是常用入口)。
- 日志:`build/R3R_debug.log`(旧探针输出)。
- **当前 `openttd.exe` = 全量重编(597/597)无探针稳定版**:`test5.sav` 加载 45s 无崩溃(崩溃 194959 已修复,见 §7)。
- 构建环境:必须先 `call "D:\VISUAL STUDIO\MAIN PACK\VC\Auxiliary\Build\vcvars64.bat"` 再 `cmake --build`,裸 cmake 会缺 INCLUDE 报 cl 找不到 stdint.h。

## 1. 专题一:解挂在错误 depot(42,28 而非 38,27)—— 修复已实施,复测待核对
- 详见 `R3R_decouple_wrong_depot_diagnosis.md` + 记忆 31435600。
- 修复(train_cmd.cpp):GOTO_DEPOT 分支加目标匹配 `cur_real->GetDestination().ToDepotID() == GetDepotIndex(consist->tile)`。
- **复测疑点(2026-09-05 分析)**:旧日志 DECOUPLE-FIRE 在 42,28 与该配置矛盾,疑旧日志录制时命令6终点=42,28(配置修改前);需复测核对解挂点是否已改在 38,27。
- 附带问题:若机车解挂后回 38,27 库执行 GOTO_COUPLE 找不到车底(车底留 42,28)→ COUPLE-FAIL 死循环刷屏,待确认 GOTO_COUPLE 目标车底位置。

## 2. 专题二:反转链后死锁 —— 已修复(保持稳定,背景)
- `SwitchToNextOrder` 补 `case OT_GOTO_COUPLE`;`AdvanceOrdersFromVehiclePosition` 对 GOTO_COUPLE/GOTO_DEPOT 提前 return;yapf_destrail.hpp 三处 reservation/扫描放宽。详见 `R3R_reverse_chain_analysis_memo.md`。

## 3. 专题三:订单调向 force 换端 —— 已弃用(2026-09-04 拍板),代码保留
- 详见 `R3R_order_reverse_force_memo.md` §0 + 记忆 60885272。
- force 整链 `ReverseTrainSwapVehicles` 违反记忆 97996690 规则(多段须逐段反转),故弃用;train_cmd.cpp 三处 `Set VehicleRailFlag::ForceFlipReverse` + 消费点(~3084/3200-3247)代码未删。
- 此后再排程不依赖订单级 force 换端。

## 4. 专题四:按段解挂(段=SegmentFront★)—— M1/M2/M2-UI 已实现并编译
- 详见 `R3R_segment_decouple_plan.md`(决策 D1-D8)。
- M1:train.h 标记 + GetSegmentHeadFromRear + Couple 打标 + DecoupleTrain 段分支 + orders 恢复提升 + ClearSegmentFront。M2:depot 手动拖动按段吸附。M2-UI:depot 按钮 Make consist / Make segment / Demote。M3(复合解挂)未做。
- 段升级/降级与假引擎:MakeConsist 令链 first 为假引擎头;升级为段时若 last 无动力也升为假引擎;降级时撤销 last 假引擎(记忆 51063123)。

## 5. 用户规则(记忆 97996690):多段链 first/last 反转必须"逐段各自反转"—— 已落地主体
- 语义:段与段次序不颠倒,只逐段内部头尾互换,★ 迁各段反转后的新段头。
- **已落地(2026-09-05/06,记忆 41007633/92405143)**:TryTrainCouple 折叠修正重写为"谁反翻谁"三候选平铺(候选1 只逻辑翻 u → 候选2 只物理翻 v → 候选3 双翻),每候选失败必须自撤销再试下一候选,不许状态叠加;`R3RFlipChainBySegments`(train_cmd.cpp ~4001)按段序保持重写:翻方向→按★记录组(artic 块随父车、组含引擎记 has_engine)→段内 artic 块倒序重链(SetNext 双向)→★迁新段头;`R3RUndoLogicalFlip`(~4047)回滚。逻辑翻原语(就地把每节 direction 反转、位置不动)。决策文档:`R3R_couple_fold_catchup.md` / `R3R_flip_chain_decision.md`。
- **尚未合规**:ReverseTrainDirection 的 force 换端路径(~3200-3247)仍整链 ReverseTrainSwapVehicles(force 已弃用,优先级低)。段级空间翻转(ReverseTrainSwapVehicles 段级化)待做。
- primary/假引擎迁移属"换端重排翻 v"场景(记忆 31054337):期望=新总链头=原链尾,带假引擎列执行 段级反转+假引擎迁移+primary 迁移;真引擎列只做段反转+primary 迁移。未实现。

## 6. artic 铰接车机制与已知问题(2026-09-05,重要背景)
- artic 组=父车+部件(GetNextArticulatedPart 生成),焊死不可拆、不可挂/解挂点;artic part 不算 IsEngine/IsWagon;遍历"真实车辆"用 train.h GetNextUnit/GetPrevUnit/GetLastUnit 跳过 artic 与双头车后节;artic 部件 cached_veh_length 很短。详见记忆 21823468 + `R3R_artic_couple_memo.md`。
- 已知死循环(未修复,待用户拍板):机车鼻尖贴车底尾时每 tick 命中耦合点→TryTrainCouple 全候选→FOLDCHK 折叠→回滚→下 tick 重试;artic 短车使命中判据与拼接判据端点错位(命中=鼻尖↔尾,拼接=机车尾↔车底头)。诊断与 A/B/C 候选:`R3R_couple_loop_3933_diagnosis.md` + 记忆 79051681/21823468。
- artic 假爸爸身份换向设想(未实现):`R3R_artic_fake_parent_identity.md` + 记忆 65693747。
- 其它:artic 现场物理 ReverseTrainSwapVehicles 多次 SWAP 无崩溃(artic 组不拆散)。

## 7. 崩溃 194959 + 构建依赖失效根因(2026-09-06,已闭环)
- 详见 `R3R_crash_194959_mixed_obj_memo.md` + 记忆 66636022。
- 根因:改 src/*.h 后 **587 陈旧 obj**(旧头编译)与 7 个新 obj 混链 → vtable 布局不一致 → call 0 崩溃(买三节铰接列车)。
- 环境根因:ninja deps=msvc 只认英文 `Note: including file:` 前缀,中文系统 cl 输出 `注意: 包含文件:` → 实测 `ninja -t deps` = `#deps 0`;`set VSLANG=1033` 实测无效(cl 14.51 仍中文)。
- **铁律:改任何 `src/*.h` 后必须删 `build\CMakeFiles\openttd_lib.dir` 下全部 `*.obj` 全量重编(597 文件约 25-40 分钟);仅改 .cpp 可增量(~1min/文件)**。判别:obj LastWriteTime 是否全晚于所改头文件。此与 §8 语言 txt 问题同根(记忆 52814982)。

## 8. 构建注意事项(2026-09-06 合并更新)
1. 必须先 vcvars64(见 §0)。
2. 改 `src\lang\*.txt` → strings.cpp 可能不重编 → "No available language packs";touch src\strings.cpp 后重建(记忆 52814982)。
3. 改任何 `src/*.h` → 必须删全部 *.obj 全量重编(见 §7,最易踩坑)。
4. 链接时 openttd.exe 被占用会 LNK1168,先关游戏进程。

## 9. 下一步建议(按优先级)
1. GUI 实测"购买三节铰接列车"复验崩溃 194959 修复(test5.sav 加载已过)。
2. 复测专题一:核对 DECOUPLE-FIRE 是否在 38,27(而非 42,28)、COUPLE-FAIL 是否消失。
3. artic 端点错位耦合死循环:用户从 A/B/C 候选拍板修复方向(记忆 21823468/79051681)。
4. 用户规则剩余合规项:ReverseTrainDirection force 路径段级化(force 已弃用,低优先);换端重排翻 v 的 primary/假引擎迁移(未实现)。
5. 全部通过后:清理 build/R3R_debug.log 探针输出代码、push feature/decouple。
