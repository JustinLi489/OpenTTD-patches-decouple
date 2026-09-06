# R3R GOTO_COUPLE 死循环诊断备忘(artic 铰接车现场)

日期:2026-09-05
状态:根因已收敛,修复方案未定(待用户拍板)

## 0. 一句话版本

机车鼻尖已贴到车底尾部、满足"命中"判据;但真正拼车要求的是"机车尾对车底头",
两者隔着整列车,折叠检查永不通过 → 每 tick 命中一次、失败一次,死循环刷屏。

## 1. 目前在修什么问题

JGRPP 魔改(R3R)的 GOTO_COUPLE 自动挂车功能:
机车按订单开往等待挂接的车底,贴上去后应自动耦合成一列。
现在机车到达后永远挂不上,原地死循环(游戏卡住 + 日志爆炸)。

## 2. 用户提出的关键突破(重要!)

**现场没有任何普通散车厢。** 用户使用的是:
- 3 列"三节铰接式列车"(articulated rail vehicles),合计 9 个 vehicle 对象 = 3 个**不可拆物理单元**。
- 日志 R3R_debug.log 里的"段" idx0 / idx3 / idx6(每 3 个 vehicle 一段)正好对应这 3 列铰接车。
- 每段内部是 artic 组(父车 + 2 个 artic parts),代码里焊死不可拆;
- **段与段之间**才是真正允许拆/拼的边界。

含义:诊断时必须按"整组铰接车 = 一个单元"来读几何,不能把 9 个 vehicle
当作 9 节独立车厢逐节分析。artic 部件长度(cached_veh_length)很短,
导致名义耦合中心距极小,折叠容差只有几像素——现场一错位就必判折叠。

## 3. 铰接车(articulated)代码机制速查

### 3.1 生成与绑定:src/articulated_vehicles.cpp

- `AddArticulatedParts(Vehicle *first)` ~380 行:
  引擎若带 `VehicleCallbackMask::ArticEngine` 回调,则循环
  `GetNextArticulatedPart(i, engine_type, ...)` 逐节生成部件:
  - `v->SetNext(t)` 焊在上一节后面(链表不可拆);
  - 每节 `SetArticulatedPart()`(subtype 置 `GVSF_ARTICULATED_PART`);
  - `x_pos/y_pos/tile/direction/track/vehstatus` 初值全部复制父车
    (`t->x_pos = first->x_pos;` 等);
  - 不独立计为引擎/货车(IsEngine/IsWagon 均不成立)。

### 3.2 身份判定(src/tbtr_template_vehicle.h:153-169)

- `IsFrontEngine()` = GVSF_FRONT
- `IsEngine()` = GVSF_ENGINE
- `IsWagon()` = GVSF_WAGON
- `IsArticulatedPart()` = GVSF_ARTICULATED_PART ← artic 部件
- `IsMultiheaded()` / `IsRearDualheaded()` = GVSF_MULTIHEADED 相关

### 3.3 "真实车辆"遍历(src/train.h:273-303)

- `GetNextUnit()` / `GetPrevUnit()`:跳过 artic 部件与双头车后节,
  返回下一个"真实"车(整组铰接车在遍历里 = 1 个单元)。
- `GetLastUnit()`:沿 GetNextUnit 走到链尾。

### 3.4 与 R3R 的关系

- 段内翻转/★ 布点已按 artic 块(父车 + 连续 artic parts)整体处理,
  见记忆 41007633(R3RFlipChainBySegments)。
- 物理换端 ReverseTrainSwapVehicles 对 artic 组是否安全 = 待查疑点。

## 4. 发现的现象(证据:build/R3R_debug.log)

- 死循环样本区段 ~6211-6243:
  - A1:COUPLE-FLIP FOLDCHK `A idx=2 y=506 ↔ B idx=3 y=533 exp=2 dist=27`,gap≈25 > 8 → 折叠;
  - A2 / A3 依次也折叠(差 16-17px);
  - 之后回滚,下 tick 再命中再失败。
- 日志 8112-8207:RESERVECONSIST(轨道预留)相关语句刷屏(由反复尝试触发)。
- 旧日志的其它位置(6428/6432 等)与 R3R_debug 配置顺序需复核,
  见记忆 31435600(解挂点规则 R3R_debug.log 复测)。

## 5. 推导出的根因

`TryTrainCouple` 死循环 = **命中判据与拼接判据端点角色错位**:

1. **命中判据**(`GetCouplePosition`, src/train_cmd.cpp 4532-4565):
   当机车鼻尖(v 的 x_pos/y_pos)与车底目标端 z 的中心距
   `diff == (v_length+1)/2 + (u_length+1)/2` 时,判定"已到耦合位",返回 u。
   - reverse 时 z = u->Last()(车底尾部),否则 z = u(车底头部)。
   - 这是"鼻尖 ↔ 端面"的几何,标称距离只有几像素(artic 短车 exp≈2)。

2. **拼接判据**(TryTrainCouple 内,~4097 起):
   拼车语义是 **机车尾 v_last ↔ 车底头 u_head** 对接,
   要求两端几乎贴死(gap ≤ 8px),否则 R3RCheckChainFold 判"折叠"回滚
   (见 3905 R3RCheckChainFold / 3948 R3RCheckChainFoldedDirection,
   ChainFolded lambda ~4215)。

3. **矛盾现场**:机车鼻尖已顶到车底尾(横跨 39,32/39,33 的车底),
   中心距恰好满足命中公式;但若按"尾对头"来拼,则隔着整列车,
   永远差 27px(A1)/16-17px(A2/A3),FOLDCHK 永不通过。

4. 结果:每 tick 命中→尝试→折叠→回滚→下 tick 再命中……
   无限循环 + 每帧轨道预留/日志刷屏。

5. artic 放大了问题:artic 部件长度短 → exp 小(≈2px) → 容差仅几像素,
   任何微小的错位都立即判折叠。

## 6. 修复方向(备选,未定)

- A. 命中后让机车继续前移、真正"尾对头"贴死再拼;或
  统一命中与拼接的端点角色(命中应测"机车尾 ↔ 车底头"而非"鼻尖 ↔ 端面")。
- B. R3RCheckChainFold / R3RCheckChainFoldedDirection(3905-3950):
  对 artic 组**内部对**跳过、或按整组长度计算(现逐 vehicle 独立算,
  artic 组边界 gap 易虚高 → 误判折叠)。
- C. 增加尝试次数上限,失败即停(停车报警/放弃),避免无限重试刷屏;
  探针日志改 edge-trigger(仅在状态变化时打),防日志爆炸。

## 7. 待核 / 下一步

1. 与用户确认现场列车的真实编组:3 列三节铰接车,哪列是"机车/主引擎",
   哪列是"车底/consist"?9 vehicle = 3×(父车+2 parts)是否准确。
2. GetCouplePosition 命中的端点(z)与拼接端点(v_last/u_head)
   在 artic 组场景下分别落在哪个 vehicle,逐组核对。
3. 复核 GOTO_COUPLE 目标车底位置(42,28 / 38,27 矛盾点,记忆 31435600)。
4. 物理 ReverseTrainSwapVehicles 在 artic 组上的行为检查。
5. 修复后回归:正常解挂→机车入库→再 GOTO_COUPLE 挂上→开走,全程无死循环。

## 8. 关联记忆

- 79051681:R3R 耦合死循环根因(artic 现场)
- 21823468:R3R 耦合死循环修复方向与 artic 机制
- 31435600:R3R 解挂点规则与 R3R_debug.log 复测
- 41007633:TryTrainCouple 折叠修正落地(artic 块处理)
- 97996690:R3R 用户规则(段 = SegmentFront★,逐段反转,段序保持)
