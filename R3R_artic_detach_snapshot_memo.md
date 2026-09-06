# R3R 论证A备忘：解除铰接 = 打散成普通车 + 整列状态按规则快照赋予

日期：2026-09-06
状态：方案论证定稿（分摊规则已由用户拍板），待实现
前置：`R3R_artic_formA_detach_memo.md` §9 —— 形态A（摘 flag + 重录身份）实测失败并已回滚

## 0. 一句话

形态A 失败根因**不是"摘 flag"本身**，而是摘 flag 后每节独立按自身 GRF record 全量统计，
而 artic part 的 record 从不承载"每节独立规格"——整列规格（自重/动力）由 GRF 按**整列一次**
挂在父车那一节上（`Train::GetMaxWeight` 对 artic part 计 0 车重）。

论证A = 升段时把 artic 组打散成普通车（**保持一条链**，只改 subtype/flag，不拆链），
并在摘 flag 的同一流程里把**整列口径的状态**快照下来，按规则写进每节的新覆写字段，
使打散后的 ConsistChanged 重统计结果守恒。

→ 形态A 备忘 §9 的"物理解构原理性不可行"结论**被推翻**。

## 1. 拍板规则（2026-09-06 用户确认）

打散对象：artic 组 = 父车 + 其后连续 artic parts，共 N 节。
打散时机：仅 depot 升段（CmdMakeSegment），打散后**保持一条链**（不产生多条独立链）。

| 属性类 | 打散后每节 | 状态源 |
|---|---|---|
| 自重（总量型） | 原整列自重 W ÷ N | 快照 |
| 动力（总量型） | 原整列动力 P ÷ N | 快照 |
| 极速（标定型） | 原整列极速 S，每节原封不动 | 快照 |
| engine_type / sprite / cargo_type / cargo_cap / refit_cap / 坐标 / direction / 年龄 | 直接继承 | 本来就在每节实例字段上，摘 flag 后原样保留 |

均分含余数：`q = W / N`，`r = W % N` → **父车 = q + r，其余各节 = q**，Σ 精确守恒（用户拍板）。
动力 P 同规则。

## 2. 代码事实（全部已核实）

- `AddArticulatedParts`（articulated_vehicles.cpp:380-516）：part 由独立 EngineID 生成，
  spritenum/cargo_type/cargo_cap/refit_cap 按 part record 逐节落字段（415-422），
  engine_type 保留；摘 flag 不改这些字段，**不需要缓存"继承类"状态**。
- `Train::GetMaxWeight`（train_cmd.cpp:9958-9973）：`IsArticulatedPart()` 跳过车体自重，
  cargo 货重仍计 → 整列车体自重 = 非 artic 节贡献，artic 组内只有父车贡献一次。
- `Train::GetWeightWithoutCargo`（train.h:351-366）：artic part 恒 0。
- `Train::GetPower`（train.h:420-438）：artic part 恒 0；R3R 已有"假引擎 Wagon 报 ≥1hp"
  特判（431-433）先例。
- 翻倍根源：artic 状态下 artic part 规格从不独立统计；摘 flag 后每节按自身 record 实算。
  父子同款 record 时 → 自重/图像 × N。

## 3. 快照口径

artic 组在链内的 artic 状态统计恒为"只有父车贡献"，故组口径可直接取父车节：

- `W_group = 父车 GetWeightWithoutCargo()`（artic 状态 = record/property weight）
- `P_group = 父车 GetPower()`（artic 状态 = record/property power）
- `S = 父车规格极速`（优先 PROP_TRAIN_SPEED / record max_speed；具体读取点实现时核实）

链含多个 artic 组或多真车时：只对**被升段的那一组**内部均分，组外车辆不动。

## 4. 实现落点（待实现）

| 项 | 位置 | 动作 |
|---|---|---|
| 记录位 | train.h VehicleRailFlag（最高现为 23 ForceFlipReverse） | 重新加回 `DeArticulatedPart = 24`（上次已回滚） |
| 覆写字段 | Vehicle（或 Train） | 新增 `weight_override / power_override / max_speed_override`（uint16，INVALID = 不启用、回落 record） |
| 打散 | vehicle_cmd.cpp CmdMakeSegment execute 分支头部 | `DearticulateChainWithSnapshot(t)`：先快照组口径 → 逐节摘 flag + 重录身份（形态A §4 规则）→ 逐节写覆写（父车 q+r，其余 q）→ 既有 SetSegmentFront/尾假引擎/ConsistChanged 照旧 |
| 还原 | vehicle_cmd.cpp CmdDemoteSegment seg 分支 | `RearticulateChain(seg)`：清覆写 + 还原 subtype（形态A §4 规则） |
| 自重读取 | train.h `GetWeightWithoutCargo`（351）、train_cmd.cpp `GetMaxWeight`（9958） | override 优先，否则原逻辑 |
| 动力读取 | train.h `GetPower`（420） | override 优先，且**须置于身份判定之前**（Wagon 身份节也要能输出 P/N） |
| 极速读取 | 极速读取点（待核实函数） | override 优先 |
| 存档 | vehicle/train SL | 新字段入档（读档后仍需 override 才能守恒，不能 NOSAVE） |

ConsistChanged 的缓存（cached_weight 等）由上述读取函数自然得出，无需额外处理。

## 5. 与原形态A 两个症状的关系

1. 图像"全变父车"：父子同款 record 时，打散后各节本就同款 sprite —— **N 节同款普通车
   外观是预期行为，不再是 bug**（GRF 语义本来就是 N 节同款铰接单元）。
2. 重量 ×N：由覆写修掉，Σ 守恒（父车 q+r，part 各 q）。

## 6. 危险点 / 待实测

1. 动力均分给 Wagon 身份节：`GetPower` override 分支在身份判定前生效；Wagon 带动力在
   depot/挂车/GUI 的副作用（R3R 假引擎特判 431-433 是同类先例）。
2. artic part 短节转普通车整节：`cached_veh_length` / depot 排布 / 渲染观感，需实测
   （artic 短车曾使折叠容差仅数像素，见 couple 死循环备忘）。
3. 极速覆写是否需要：父子同款 record 时极速天然一致；异 record 且 part max_speed 缺失时
   才需要 override。方案按用户拍板一律写 S，免去分支判断。
4. 存档版本兼容：新字段 + 记录位。
5. 降级还原时序沿用形态A §5.7（Dearticulate 先于尾假引擎；Rearticulate 在其后）。
6. 运行中 artic 不解构（仅 depot 升段入口，用户拍板范围）。

## 7. 不做范围

- 不把 artic 组拆成多条独立链（用户拍板：打散仅出现在升级为段时，保持一条链）。
- cargo_cap / 容量不均分不覆写（直接继承，忠实 §1 规则表）。
- 不处理运行中列车的动态解构。

## 8. 关联

- `R3R_artic_formA_detach_memo.md`：本方案的失败前身（摘 flag + 重录身份）与回滚记录。
- `R3R_artic_couple_memo.md` / `R3R_artic_fake_parent_identity.md`：artic 组整块段的既有语义。
- `R3R_segment_decouple_plan.md`：段模型（SegmentFront 等）。
