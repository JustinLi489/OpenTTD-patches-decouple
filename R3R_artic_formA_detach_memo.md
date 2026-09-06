# R3R artic 形态A备忘:段升级/降级时摘 artic flag 重录普通车

日期:2026-09-05
状态:已实现→用户实测失败(重量/图像翻倍)→已回滚,备忘待重写方案

## 0. 一句话

对铰接列车(artic)做"段升级"时,不卖车不重买,直接把组内 artic part 的
`GVSF_ARTICULATED_PART` 摘掉,并按自身 `engine_type` 的 `railveh_type`
补记 Wagon/Engine 身份,使其变成可被 R3R 段逻辑按普通车处理的串;
"段降级"时反向标回 artic。反向靠新增 VehicleRailFlag 位记录。

## 1. 背景

- 现场 3 列三节铰接车 = 3 个不可拆物理单元;artic part 不算 IsEngine/IsWagon,
  GetNextUnit 等"真车遍历"把整组当 1 个单元 → couple/uncouple/段翻转按"组"
  处理,与 R3R"段=普通车列"语义不兼容;artic 短车使折叠容差仅几像素→死循环。
- 方案B(卖artic买3辆普通车/降级反向)已弃,现金流与对象重建成本高。
- 形态A(拍板):只改 subtype/flag,不产生买卖、车辆对象不变。

## 2. 落点(全部已核实)

| 函数 | 位置 | 动作 |
|---|---|---|
| CmdMakeSegment | src/vehicle_cmd.cpp ~354-375 | execute 分支 SetSegmentFront 前,若链含 artic part → DearticulateChain(t) |
| CmdDemoteSegment | src/vehicle_cmd.cpp ~394-445 segment 分支(seg!=nullptr) | ClearSegmentFront 前后,若链带 DeArticulatedPart 标志 → RearticulateChain(seg) |
| subtype API | src/ground_vehicle.hpp | ClearArticulatedPart~344; SetWagon/ClearEngine~349-364 |
| 新标志位 | src/train.h VehicleRailFlag(最高现为23 ForceFlipReverse) | 加 DeArticulatedPart = 24 |
| 现有★/尾假引擎 | src/vehicle_cmd.cpp GetLastChainVehicle~294、SetSegmentTailFakeEngine~314、train.h:160-164 | 摘 flag 后 GetLastChainVehicle 不再跳过这些车,尾假引擎作用到新真链尾 |

## 3. 关键事实:artic part 的原始字段(还原基准)

src/articulated_vehicles.cpp AddArticulatedParts(~380-428):
- `t->subtype = 0` 后仅 SetArticulatedPart → 摘 flag 前的原始 subtype = 只有 artic 位;
- engine_type = GetNextArticulatedPart 回调返回的独立 EngineID(≠父车);
- spritenum/cargo_type/cargo_cap/refit_cap/railtypes/track 均自带;x/y/tile/direction 复制父车。
→ 摘 flag 不改 engine_type/sprite/cargo/链序,只改 subtype + 记录位;
  还原 = 清 Wagon/Engine 位 + SetArticulatedPart + 清记录位 = 回到原始。

## 4. 转换步骤(设计稿)

升段 DearticulateChain(Train *head):遍历 Next 链,对每辆 IsArticulatedPart:
```
v->ClearArticulatedPart();
v->flags.Set(VehicleRailFlag::DeArticulatedPart);   // 记录"原为 artic part"
switch (RailVehInfo(v->engine_type)->railveh_type) {
  case Wagon:  v->SetWagon(); v->ClearEngine(); break;
  default:     v->SetEngine(); v->ClearWagon(); break;  // Engine/Multihead/RailCar 保守真引擎
}
```
之后由既有 CmdMakeSegment 走 SetSegmentFront/SetSegmentTailFakeEngine/ConsistChanged(CCF_ARRANGE)
(ConsistChanged 重算位置/重量/动力/first_engine 等)。

降级 RearticulateChain(Train *head):遍历,对每辆带 DeArticulatedPart 的车:
```
if (v->IsWagon()) v->ClearWagon();
if (v->IsEngine()) v->ClearEngine();
v->SetArticulatedPart();
v->flags.Reset(VehicleRailFlag::DeArticulatedPart);
```
随后 ClearSegmentFront/ClearSegmentTailFakeEngine/ConsistChanged 照旧。

## 5. 危险点 / 待回归核对

1. 重量/动力口径:CargoChanged(ground_vehicle.cpp:103-145)与 GetWeightWithoutCargo/
   GetPower(train.h:351/420)对 artic part 计 0;摘 flag 后按 engine_type 实算,
   part 若被重录 Engine 且 railveh_type 有动力属性会给整列加动力——用后需实测编组值。
2. GroupStatistics/车辆列表计数:artic part 原本不计入,摘后计入;CCF_ARRANGE 与
   ConsistChanged 是否完整刷新待实测(必要时手动 CountVehicle/InvalidateVehicleListWindows)。
3. cached_veh_length / first_engine / depot 排布:Depot 布局按 real vehicle 排,
   摘 flag 后 artic parts 变为独立槽位,depot 显示/拖动可能随之改变——这正是目的,
   但需确认不出现错位或重复占位。
4. 只有 depot 停稳 + 独立链头可升/降级(CmdMakeSegment/Demote 已校验),不涉及运行中列车。
5. 若 GRF 的 artic part railveh_type=Multihead,保守置 Engine 是否安全待实测(罕见)。
6. 存档兼容:新增 VehicleRailFlag 位若需跨版本,check 二进制兼容(新位只活在内存 flag,
   随 train flags 存档,应无碍;留意旧档读取)。
7. 与"段升级/降级时链尾 last 假引擎"既有规则(记忆 51063123)的顺序:
   Dearticulate 必须先于 SetSegmentTailFakeEngine,否则尾假引擎会给"artic part"身份的车
   加假引擎再摘 flag,导致该车带 engine 位 + 记录位 → 还原后多出假引擎身份。
   反之降级 Rearticulate 应在 ClearSegmentTailFakeEngine 之后,避免给带假引擎的普通车
   再 SetArticulatedPart 造成 subtype 叠加。

## 6. 不做的范围(本次)

- 不实现"不成段车厢链拒绝 couple/uncouple"(待办,记忆 17190446)。
- 不改 GetCouplePosition/R3RCheckChainFold 端点语义(死循环修复另议)。
- 不处理运行中 artic 列车的动态解构(仅 depot 命令入口)。

## 7. 关联

- 记忆 51063123(段升级/降级 + 链尾假引擎)、17190446(未来拒绝规则)、
  21823468/79051681(artic 死循环)、97996690(段序规则)。
- 相关文件:R3R_artic_couple_memo.md、R3R_artic_fake_parent_identity.md

## 8. 实现落点更新(2026-09-05 已改代码)

- src/train.h:27-51 VehicleRailFlag 新增 `DeArticulatedPart = 24`。
- src/vehicle_cmd.cpp:
  - `DearticulateChain(Train *head)` / `RearticulateChain(Train *head)`
    新增于 ClearSegmentTailFakeEngine(~339)之后;
  - CmdMakeSegment execute 分支头部调用 DearticulateChain(t)(先于
    SetSegmentFront / SetSegmentTailFakeEngine / ConsistChanged);
  - CmdDemoteSegment seg 分支调用 RearticulateChain(seg)
    (在 ClearSegmentFront + ClearSegmentTailFakeEngine 之后、
    ConsistChanged 之前)。
- 待编译验证(EXITCODE=0)后跑回归,核对 §5 各危险点。

## 9. 用户实测失败与回滚(2026-09-05 晚)

用户测试形态A后反馈两症状:
1. 图像全部变成第一节(父车)的图像;
2. 三节铰接列车分开后,每一节都继承父车全部属性,
   重量等数据整体翻了三倍。

根因(代码实证,非参数可修):
- articulated_vehicles.cpp:380-516 AddArticulatedParts:rail artic part
  由独立 EngineID(GetNextArticulatedPart 回调)生成,有自己的
  spritenum/image_index;但该 record 在 OpenTTD 语义里只是父车的
  "焊死延伸节"占位 —— train_cmd.cpp:9962-9965 Train::GetMaxWeight 明确
  "Vehicle weight is not added for articulated parts":artic part 的自重/
  规格从不独立统计,整列规格(重量/载客/功率)由 GRF 按"整列一次"
  设计并挂在父车那一节上。
- 形态A把 artic parts 摘 flag 变独立普通车后,ConsistChanged 会对每节
  做全量独立统计:每节都按自身 engine record 计重。若该 GRF 的 part
  record 与父车同规格(重量字段相同,常见于父子共用/同款 record),
  分开后总重 = N×父车重 → 翻 N 倍;图像同理全变父车/同款图。

结论:artic parts 不是独立车辆规格,物理解构不能靠"摘 flag + 重录身份"
实现;形态A 是原理性不可行,不是落点 bug。

处置:已回滚全部形态A改动(train.h 枚举位 + vehicle_cmd.cpp 两个 helper
与两处调用),重新编译 EXITCODE=0,代码回到形态A前状态。
注意:运行过形态A exe 的存档中已解构车辆无法靠回滚代码还原
(subtype 已改且可能已入档),需用旧存档或卖掉重买。
待办(2026-09-06 已定稿):形态A 直接拆解的根因被定位为"artic part 无每节独立规格、
整列规格只按一次挂在父车"。替代方案「论证A:打散成普通车 + 整列状态快照按规则
均分/继承赋予」已拍板定稿,见 R3R_artic_detach_snapshot_memo.md——
本节"原理性不可行"结论被该方案推翻。
