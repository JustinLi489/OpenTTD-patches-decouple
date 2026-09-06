# R3R 段级反转(FlipChainBySegments)决策固化(2026-09-05)

## 最新修订(2026-09-05 晚,推翻当日 q-0 的"整链倒置")

用户澄清(覆盖下方 q-0 裁决): 段级反转采用**段序保持**语义——

- **段序保持、逐段各自反转**:段与段的先后次序(运营次序)不颠倒,只对每段
  内部做头尾互换。上例执行后链序仍为 `B3 B2 B1 | A2 A1`(A 段在前、B 段在后,
  各自内部头尾互换),**新总链头 = 原链头段(B 段)反转后的新段首 = B3**;
  不会像"整链倒置"那样新总链头跑到原链尾 A1。
- ★ 布点仍为"每段各一个新段首★":B3★、A2★。
- 折叠探测器(距离 + direction 点积)只看物理量;单段链下段序保持 ≡ 整链倒置
  (结果一致),多段链下位置序列不同,成败姿势以 FOLDCHK/FOLDCHK-DIR 实测为准。
- 失败回滚路径不变(尝试序列已含失败→回滚→下一次尝试)。

## 用户裁决(q-0,已被上节推翻,仅存档)

给定静止等待链(直轨,自西向东排列):
`W ← B3 B2 B1★ A2 A1 → E`(A1 为链头机车/假引擎头,方向朝东,★ = SegmentFront)

执行"段级逻辑反转"(位置不动、每节 direction 反、链序整链倒置)后,目标:

- **链序**: `B3 → B2 → B1 → A2 → A1`(整链反序,新总链头 = 原链尾 B3。等价选项 A)
- **★ 布点**: 不是只给 B3,而是**凡是段首都必须有★**:
  - 原 A 组 `A1 A2` 反转后变成 `A2 → A1`,其新段首 = **A2 → A2★**
  - 原 B 组 `B1★ B2 B3` 反转后变成 `B3 → B2 → B1`,其新段首 = **B3 → B3★**(旧 B1★ 迁移/清除)
- 最终标记: `B3★ → B2 → B1 → A2★ → A1`

## 段(segment)的既有定义

- 段头车带 `VehicleRailFlag::SegmentFront`(★)。
- 段区间 = 从★车沿 Next 到下一个★(不含)或链尾。
- 链头引擎段(第一个★ 之前)可以无★(如上面例子中的 A1A2 —— 但反转后它的新段首 A2 必须补★)。
- depot 里对段内任意车操作 = 整段操作;DECOUPLE 按段拆分。
- CmdMakeSegment(vehicle_cmd.cpp)升级成段:SetSegmentFront + SetSegmentTailFakeEngine(last 也变假引擎);
  CmdDemoteSegment 降级:ClearSegmentFront + ClearSegmentTailFakeEngine。

## 实现要点(待编码,非结论)——描述 q-0 整链倒置方案,已被上方"最新修订"推翻,仅存档

1. 新原语 `R3RFlipChainBySegments(Train *chain)`(取代折叠修正里对等待链的
   `ReverseTrainSwapVehicles` 物理整链镜像):
   - 前置: `chain->First()==chain`,静止等待链/consist。
   - 先记录旧链段划分:段头 = 每个 IsSegmentFront 车;引擎段 = 链头至第一个★ 前的连续车辆。
   - 整链逻辑反转(方向反 + 链序反,位置不动)。
   - 迁移:每个旧组的"新段首"(=反转后该连续组在新链序中的第一辆,即旧组尾部)
     打★;旧组头的★ 清除(若旧组头是引擎段且无★ 则只给新段首补★)。
   - 假引擎/primary 身份迁移按记忆 31054337 的①②③(consist 组两端假引擎,
     总链头 primary → 新总链头)。真引擎链不能用纯逻辑反转(引擎会失去链首)——
     折叠修正的第三次尝试中 v(真引擎机车)仍需物理 ReverseTrainSwapVehicles。
2. 替换点(记忆 97996690 违规点):
   - TryTrainCouple 折叠修正 attempt2(u 逻辑反转)、attempt3、回滚。
   - force 换端 / 订单调向(ReverseTrainDirection 物理换端)——玩家按钮语义另议。

## 实现状态(2026-09-05 晚,按"段序保持"再修订,已落地)

折叠修正方案已在 src/train_cmd.cpp 落地并编译通过(EXITCODE=0,openttd.exe 已更新):

- `R3RReverseChainDirections`(train_cmd.cpp ~3940):就地反转链上每节车的 direction,位置不动。
- `R3RCollectSegmentFronts`(~4031)/ `R3RUndoLogicalFlip`(~4047):回滚辅助(收集旧 ★、恢复链序后翻回方向并还原 ★ 分布)。R3RFlipChainBySegments 现直接返回链头供 u_merged_head 使用,回滚时原链头 u 处调用 R3RUndoLogicalFlip。
- `R3RFlipChainBySegments`(~4001,裁决修订后重写):先 `R3RReverseChainDirections` 全链翻方向;再按 ★ 记录旧组(链头引擎段/consist 组为第一组,artic 块随父车,组含引擎与否存入 has_engine);然后**段序保持、逐段内部 artic 块倒序重链**(SetNext 双向维护,新链头 = 原链头段反转后的首车);最后 ★ 迁移 = "每组新段首(= 旧组尾块之首车)打 ★、旧段首 ★ 清除",打 ★ 条件 = 旧段首带 ★ 或该组含引擎。
- `TryTrainCouple`(~4097)折叠修正三连已全部改用上述原语:
  - attempt2:只 `R3RFlipChainBySegments(u)` 后重拼;
  - attempt3:真引擎机车 v 仍物理 `ReverseTrainSwapVehicles(v)` + u 逻辑反转;
  - 各失败回滚 = `RestoreTrainBackup` 恢复链序 + `R3RUndoLogicalFlip` 翻回方向/★,无物理镜像摆动。
- `Couple`(~4247)成功路径以 `TryTrainCouple` 输出的 `merged_first`(逻辑反转后 = 原链头段反转后的新段首/新组头,单段链 ≡ 原链尾;普通拼接 = u)作为方向统一循环与 ★ 布点起点。
- 注意:反转后原 consist 假引擎头(u)位于其组尾,由 Couple 既有 `DestroyConsistGroup(u)`/`ClearFrontEngine(u)` 照常清理;primary 身份迁移属"换端重排翻 v"场景(记忆 31054337,另议),不在此路径。
