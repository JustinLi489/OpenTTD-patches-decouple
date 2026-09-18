# PXLB 仓库分析备忘(朋友的解挂/连挂实现,含"跨公司连挂")

> 2026-09-10 分析。目的:摸清朋友实现,为后续"提需求/对照 R3R"做准备。
> 分析对象为 git 已提交内容(HEAD = origin/px-patch)。代码以实际行为主,部分是沿关键路径抽样阅读的推断,已标注 [待核实]。

## 0. 仓库元信息
- 路径:`D:\PXLB`。remote = `pulsexlb/OpenTTD-patches`,另一上游 = JGRennison/OpenTTD-patches → 即 JGRPP 的 fork。
- 当前分支 `px-patch`,相对 merge-base(611aabd7 = origin/jgrpp HEAD)有 **281 commits、187 文件**改动;核心改动集中在:
  - `src/train_cmd.cpp`(+2582)、`order_cmd.cpp`(+1496)、`order_gui.cpp`(+1151)、`vehicle.cpp`(+273)
  - `vehicle_base.h`(+86)、`order_base.h`(+210)、`order_type.h`(+57)、`train.h`(+49)、`saveload/vehicle_sl.cpp`
  - 提交信息中英混杂:英文部分多半来自上游 rebase/merge,中文部分是本人;系统命名 JGRPP "couple order"(与 R3R 各自独立并行开发,概念撞车)。

## 1. 它做的是什么
在 JGRPP 上做了一套 **"调度化编组/分解"系统**:
- 新增 4 个 order 类型:`OT_GOTO_COUPLE`(去某站与符合条件的车对接)、`OT_WAIT_COUPLE`(在站等待被对接)、`OT_DECOUPLE`(按节数拆开,两段各自设定后续命运)、`OT_EXECUTE_SCHEDULE`(列车去执行另一个 order list / "调度计划")。
- 耦合发生在**车站/轨道接触**,由移动中的列车"homing"去贴等待列车,接触即合,碰撞检测里做一体判断;不是 depot 内人工工具(区别于 R3R 现阶段的 depot 层方案)。
- 解挂后两段可各自:保留原 orders(或"保留但不装卸")、装载后等待对接、空车等待对接、去执行某调度;由此支持"货列到枢纽自动拆成几截,再各自被不同车头接走/重编组"这类现实编组玩法。
- 进一步支持**玩家自定义 order list(调度计划)**,可公开给其他公司,`OT_EXECUTE_SCHEDULE` + linkgraph/客货流(8e369877、5b05c08b 等把客货流接入"执行调度")。

## 2. 核心架构概念(PXLB 与 R3R 的差异根源)

### 2.1 First(物理链头)与 Primary(consist 主车)分离
`vehicle_base.h:263` 新增 `Vehicle *primary`(NOSAVE),`:778` `Primary() = primary ?: first`。
- 存档用 `uint8_t consist_primary`(:264,SAVE XSLFI_TRAIN_PRIMARY)记录哪辆车是主车,`saveload/vehicle_sl.cpp` 恢复。
- 含义:**主车(orders/unitnumber/group/利润/调度计划归属的载体)可以不是物理链头,甚至可在链中**。许多逻辑从"first"改为"primary"(如 linkgraph 维护 commit 006bd87e)。
- `IsPrimaryVehicle()` 放宽为 `IsFrontEngine() || IsFrontWagon()`,→ **纯车厢(无引擎)也能当主车**携带 orders/在站等待(=解挂后的无动力后段能够"独立存在并等待对接")。
- R3R 对照:R3R 里"front/身份头"目前与物理头绑定较紧(记忆 30805247/41007633 中 R3R 是靠整链重链把身份头留在链头)。PXLB 直接把主车下沉到任意位置,再逐点改成按 Primary 运算——这是两者架构级差异。

### 2.2 couple/decouple 期间的状态位(order_type.h、train.h)
- `train.h`:移动侧 `couple_target`;等待侧主车 `couple_claimant`/`couple_claim_cost`(claim 仲裁)与 `couple_body_hold`(等待车松开自己 body 之后的预留,让接近者用常规预留一路进来)。
- 主车 `decouple_part`(0/1/2:整列/留在站的部分/被拆走的部分)与 `JustDecoupled`(解挂后、离开站前禁止做自动反转)。
- order 条件变量新增 `OrderConditionVariable::DecouplePart`,供 order condition 依据"我是拆出的第几段"分支。

### 2.3 耦合运行时主流程(train_cmd.cpp,行号按 PXLB HEAD)
1. 接近与选择:`DoTrainCouplePathfind`(4569 附近,yapf_rail.cpp 的 `YapfTrainCoupleTrack`)给 GOTO_COUPLE 车找通往"合格等待车"的路;
2. **claim 仲裁**(6331-6388):等待车只被"最近/最便宜路径"的接近者认领;更优挑战者可抢 claim,被抢者 `couple_target=INVALID` 去找下一辆;
3. `ValidateCoupleCandidate`(6402)逐条过滤:双方 order 类型 / claim 未被抢占 / 等待车未 Stopped / **`IsTrainCouplingAllowed(moving->owner, carrier->owner)`** / 负载条件(空/满)/ cargo 种类 / 车厢节数(=n)/ TR slot 占用 / 指定站 / 站适配与 `IsCoupleArrangementValid`;
4. 物理接触触发:碰撞检测 `CheckTrainCollision`(7243-7305)对"刚好接触且通过 ValidateCoupleCandidate"的情形**按 couple 处理而非撞车**(7291 `Couple(...)`),并 `moving_front->cur_speed=0`;
5. `Couple()`(约 6600-6834):
   - 先物理对齐:`ReverseTrainForCouple`(注释"merge 后无方向调整";对齐后物理头方向决定合并列方向);
   - `TryTrainCouple` 做链手术(MakeTrainBackup/ArrangeTrains——名字与 R3R 同名函数撞车,但内容不同源);
   - 簿记:两段 primary 归一、`ProcessOrders(v)`、`DeleteVehicleOrders(u)`、释放 u 的 unit number、GroupStatistics、`AdvanceWagonsAfterCouple`、站 loading 队列中摘除 u、TR slot 从 u 迁移到存活主车(6789-6797)、`last_station_visited`/装卸站归属按节保留并 `PropagateLastLoadingStation`(客货流账目)、lookahead 重建、`TryPathReserve`、`CheckReverseTrain`(可置 Reversing 交给普通换向流程);
   - `adopt_waiting_schedule` 选项(6671/6815-6825):合并后采用等待侧调度并立即把下一命令装载好再预留。

### 2.4 解挂主流程(DecoupleTrain,6150-6260 附近)
- 支持主车不在物理链头的结构:若车头侧无引擎,主车会转到尾段(FrontEngine/FrontWagon 角色互换),两侧各自独立成可调度列;
- 拆出部分挂 `decouple_part=2`,留站部分 `=1`;`TryTrainDecouple` 做链手术;NewGRF `CanDecouple` 校验、TR slots 继承等。

### 2.5 真实倒车 / 方向模型
- 有多轮提交处理"整列真实反转"(物理翻转+走行方向),以及 `ReverseTrainNoSwapVehicles`/图像不反转 类分支;耦合发生在真实倒车语义下,方向由 v 驾驶方向定(6682-6688 注释)。
- R3R 与此同向但独立实现(R3R 的"逐段反转/换端重排/折叠修正"是 R3R 自己的;PXLB 没做 R3R 的段(segment/★/假引擎)概念)。

## 3. "跨公司连挂"的证据与机制
- 开关:`economy_settings.ini:637` `allow_coupling_other_company_trains`(bool,**默认 false**,SettingFlag::Patch),`settings_type.h:914`;GUI 里归在 sharing 相关的经济设置组(settingentry_gui.cpp:1256 附近)。
- 判定:`infrastructure_func.h:28` `IsTrainCouplingAllowed = (owner1==owner2 || 开关打开)`;用在耦合校验(6412)、`Couple()` 入口(约 6667,搜索到的 3 个调用点中的两处)等处。
- 合并后**每节车 owner 各自保留**(vehicle->owner 原字段),没有"整链收编成一方 owner"的做法;所有公司级结算沿 OpenTTD 原生 per-vehicle owner 路径走:
  - 月度维护/折旧、运行成本:原版按每节车 owner 归入各自公司账单;运行收益按卸货/交付节 owner 记账 → B 的车厢挣的钱进 B。
  - 利润/统计统计到存活主车 v 的 owner A(DeleteVehicleOrders/GroupStatistics/ReleaseUnitNumber 只对 u 的主车身份执行,车厢个体不变)。
  - `couple_target->Primary()` 是 B 侧主车,其 orders 被整体吸收(adopt 或丢弃),A 的 orders 存活。
- "调度计划跨公司共享"配套:玩家创建的 orderlist 带 owner + `is_public`,`IsVisibleToCompany()` 让公共计划可被别公司车 `OT_EXECUTE_SCHEDULE` 执行 → 跨公司的不是只有"物理连挂",还有"跨公司列车跑同一套调度"。

## 4. 与 R3R 对照(供后续需求对齐)
| 维度 | PXLB(朋友) | R3R(我们) |
|---|---|---|
| 耦合操作入口 | order 驱动,站内接触自动合(GOTO_COUPLE/W AIT_COUPLE) | depot 工具(Make/Demote Segment)+ GOTO_COUPLE/DECOUPLE order |
| 等待侧状态 | 真实列车带 orders 在站等(W AIT_COUPLE) | 等待 consist/GOTO_COUPLE 目标 |
| 链结构 | First/Primary 分离,主车可不在物理头;纯车厢可当主车 | 物理链头承载身份;段(★/假引擎)概念;折叠修正/回滚 |
| 翻转/换端 | 真实倒车+耦合前物理对齐;防 auto-reverse 标志 | 逻辑翻(段序保持)/物理翻/换端重排,身份迁移 |
| 跨公司 | 显式支持(开关+per-vehicle owner 结算) | 无此概念 |
| claim/仲裁 | 有(最近者抢 claim) | 无(R3R 现场靠回滚下 tick 重试) |
| 存档 | XSLFI_TRAIN_PRIMARY 持久化主车 | segment ★ 走既有 flag 存档?[R3R 待核] |
| 客货流/linkgraph | 按节装卸站归属,支持 cargodist | 未涉及 |

## 5. 观察:成熟度与风险点
做得扎实的地方:
- 耦合是"碰撞检测内 touch→couple"的一体设计(7243-7305),与自碰撞判定(7257 用 Primary 判同链)不打架;耦合点有精确的 `CoupleJointOffset`(6848)长度取整模型。
- claim 有存活校验(6337-6350),order 离开/崩溃/换目标都会释放,不是纯 flag。
- 解挂后无动力段能"独立存在等待对接"是体系里最花心思的部分(纯车厢主车、站 queue、装卸归属)。
- 提交历史显示对 mixed primary 下的崩溃类问题做过系统性修复(如 "primary != first 出售崩溃"、"linkgraph 从 primary 遍历"、station loading queue 的 assert)。

需要向朋友提问/风险清单(用户提需求时的弹药):
1. **公司级结算边界**:入 depot 一次性服务账单、StationShare/Fees、收购/破产公司接管、公司破产时混合链中"别家车厢"怎么处理?[未见专门处理迹象,待核实]
2. **所有权 UX/操作盲区**:A 玩家列里挂着 B 的车厢时——B 玩家在哪看到/控制自己的车厢?depot 内 A 的列能否被 B 的车头"偷接"?卖车/收购、换轨距 depot(conversion)对 mixed chain 行为?autoreplace 会不会把别家车替换掉?[全部待核实,最可能是整套系统的软肋]
3. **NewGRF/变量**:per-owner 的 Var26 之类在 mixed chain 上的呈现;`CanDecouple`/`IsCoupleArrangementValid` 对跨公司如何限制。
4. **信号/预留**:PBS 预留按主车 owner 走,别家车在 A 的预留内,若两家在共享线路上(infra sharing)信号归属/死锁语义如何?
5. **性能/稳定性**:真实倒车物理翻转+站内自动耦合对 large map/AI 的行为;wait couple 车在繁忙站被多次抢 claim 的抖动。

## 6. 关键行号索引(PXLB HEAD 工作树)
- order 类型: `src/order_type.h`(OT_GOTO_COUPLE/OT_WAIT_COUPLE/OT_DECOUPLE/OT_EXECUTE_SCHEDULE)
- order 属性/解析: `src/order_base.h`、`src/order_cmd.cpp`(ProcessOrders 分支、执行调度)
- 车辆结构: `src/vehicle_base.h:263/264/453/744/778/863/1520`(primary/consist_primary/Primary/IsMovingFront/IsExecutingSchedule)
- 存档: `src/saveload/vehicle_sl.cpp`(XSLFI_TRAIN_PRIMARY)
- 跨公司: `src/infrastructure_func.h:28`、`src/table/settings/economy_settings.ini:637`、`src/settings_type.h:914`
- 耦合运行时: `src/train_cmd.cpp` 4569/5290-5460(approaching)/6100-6260(DecoupleTrain)/6297-6388(filters+claim)/6402-6460(Validate)/6600-6834(Couple)/7243-7305(接触即合)
- 寻路: `src/pathfinder/yapf/yapf_rail.cpp`(YapfTrainCoupleTrack)
- UI/文案: `src/order_gui.cpp`/`orderlist_gui.cpp`、`src/lang/english.txt:973-991` 一带(拆后每段选项、按 cargo/数量/站 对接)

## 7. 后续动作
- 用户提需求前建议先想清楚:**要不要把 PXLB 的站内自动耦合/跨公司/orderlist-schedule 概念移植进 R3R,还是只在 R3R 现有 GOTO_COUPLE/DECOUPLE order 上对标做增强(如 claim、per-owner、等待车独立成列)**。
- 待用户给出具体需求后再深入对应模块代码;本节按需更新。
