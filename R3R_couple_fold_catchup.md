# R3R 防折叠"谁反翻谁"追平策略(2026-09-05 定稿)

> 续作先读本文件。配套:记忆 41007633(折叠修正落地)、97996690(段规则)、
> R3R_flip_chain_decision.md(段序保持反转语义)、R3R_couple_loop_3933_diagnosis.md(artic 端点错位)。
> 用户原话:"防折叠就按照谁还反就翻谁来追平吧。先写入记忆文件,再上手。"

## 1. 需求(用户指令)

把 `TryTrainCouple` 里僵化的防折叠序列

- A1 直接拼 v_last↔u_head;
- A2 只翻 u;
- A3 翻 v(物理)+ 翻 u(逻辑);全败回滚、下 tick 重试

改为 **"谁反就翻谁"追平**:折叠后判定哪条链仍处于反(错误)向,只翻那条
(只有 u 反 → 只翻 u;只有 v 反 → 只翻 v;两条都反 → 双翻)。
目标:打破旧序列在 (v反,u正)/(v正,u反) 等方向组合下"永远翻不出 (正,正)"的死锁。

## 2. 前置核实结论(用户要求先核实,已查代码)

- 代码中**不存在**"禁止单独翻转某一条链"的约束。
  已允许的能力:等待链 u 逻辑翻转 `R3RFlipChainBySegments`(位置不动、段序保持逐段反转);
  机车 v 物理翻转 `ReverseTrainSwapVehicles`(整链空间镜像,链序不变)。
- 旧序列的僵硬点 = 从不单独物理翻 v:A3 总是"翻 v + 翻 u"捆绑,缺"只翻 v"候选。
- `ReverseTrainSwapVehicles`(train_cmd.cpp 2790)对称交换链首尾车辆位置+方向,
  链序完全保持 → artic 组(父车+连续 artic parts)在链序上不拆散,整组随镜像;
  artic 现场 A3 已实际用过该函数、多次 SWAP/回滚无崩溃(fold-geom 崩溃是另一历史问题)。

## 3. 方向状态模型(用户确认)

- 每条链有"正/反"(反 = 链头方向与拼接健康姿势相反)。v、u 各自正/反。
- 拼接成功唯一姿势 = 合并链健康(链序 == 物理相邻、方向点积<0),即 (正,正)。
- 翻转 u 只翻 u;翻转 v 只翻 v;双翻 = 两条都翻(相对原始状态)。
- 旧僵化序列只有"只翻 u / 双翻"两个候选:
  (v反,u反)→翻u→(v反,u正)→双翻→(v正,u反)→回滚→(v反,u反):永不到 (正,正);
  (v反,u正) 场景双翻=(v正,u反)、翻u=(v反,u反) 也全折,死锁。缺"只翻 v"是根因之一。

## 4. 落地实现(已改 train_cmd.cpp TryTrainCouple)

折叠修正从"嵌套 A2/A3"重构为**平铺三候选 + 逐候选验证回滚**:

| 候选 | 动作 | 成功路径 merged_first | 失败回滚 |
|---|---|---|---|
| 候选1 | 只逻辑翻 u(`R3RFlipChainBySegments`)后重拼 | u_flip_head | RestoreBackup src+dst + `R3RUndoLogicalFlip(u, old_seg_fronts)`(方向/★ 还原) |
| 候选2 | 只物理翻 v(`ReverseTrainSwapVehicles`)后重拼(u 原样) | u(未翻) | RestoreBackup src+dst + `ReverseTrainSwapVehicles(v)`(镜像撤销) |
| 候选3 | 双翻:物理翻 v + 逻辑翻 u 后重拼 | u_flip_head | RestoreBackup src+dst + SWAPV 撤销 + R3RUndoLogicalFlip |

- 顺序语义 = "谁反翻谁":单翻某条若通过 FOLDCHK = 只有那条反;双翻通过 = 两条都反;
  全候选折叠 = 回滚到原状 `return false`(下 tick 重试,与旧行为一致)。
- FOLDCHK 裁决不变:`ChainFolded` = `R3RCheckChainFold(head,tag)>8 || R3RCheckChainFoldedDirection(head,tag)`;
  日志标签:`COUPLE`(A1)/`COUPLE-FLIP-U`(候选1)/`COUPLE-FLIP-V`(候选2)/`COUPLE-FLIP-BOTH`(候选3)。
- 各候选**失败必须把该候选自己的翻转完整撤销**再试下一候选,不允许状态叠加。
- 末尾 `CheckTrainAttachment` 失败统一回滚沿用既有 `v_flipped`/`u_flipped` 标志路径
  (`if (v_flipped) ReverseTrainSwapVehicles(v); if (u_flipped) R3RUndoLogicalFlip(...);`)。
- `u_old_seg_fronts` 在 u 原样时收集一次,候选1/候选3 翻 u 的回滚共用(候选1 回滚已还原 u)。

## 5. 范围说明(不在本次改动)

- **artic 端点错位死循环**(机车鼻贴车底尾、命中↔拼接端点角色错位,见
  R3R_couple_loop_3933_diagnosis.md 候选 A/B/C)不由"谁反翻谁"解决:该类现场
  所有方向组合都折叠,新逻辑同样回滚 return false、下 tick 重试。A/B/C 修复待用户拍板。
- "不成段车厢链拒绝挂/解挂"等未来规则(记忆 17190446)与此无关。

## 6. 测试关注点

- 正常 A1 直拼成功路径不受影响(无折叠不进入候选)。
- (v反,u正) 死锁场景:应靠候选2 只翻 v 成功——旧序列不可能成功。
- (v反,u反) 场景:候选1/2 失败 → 候选3 双翻成功。
- 观察日志折叠对在候选序列下如何收敛,确认无新增崩溃(fold-geom 探针继续盯)。
