# R3R artic 形态乙实施日志：打散 → 覆写烘焙 → 组角色分层

日期：2026-09-06
状态：**已拍板（用户：落实乙）→ 本日志为动工前设计定稿，随后按 §11 顺序落地**
前置链：
- `R3R_artic_formA_detach_memo.md` §9 —— 形态A（摘 flag + 重录身份）实测失败已回滚（artic part 无独立规格、整列规格挂父车一次）。
- `R3R_artic_detach_snapshot_memo.md` —— 论证A：打散 + 整列状态快照均分覆写，方案已定稿、待实现。
- `R3R_artic_fakeflag_state_memo.md` —— 假 artic flag 论证（形态甲/乙两个候选 + 视觉烘焙前置），本日志落实其"形态乙"。

## 0. 一句话

形态乙 = 在 `IsArticulatedPart()`（subtype 真位）之外新增**组角色层**：
artic 组打散成普通车（数值用论证A 覆写守恒、内部节可重排/可翻转），再以显式
`ArticGroupHead / ArticGroupMember` 角色位重建"组行为"——渲染/遍历/组单元消费点改问
**组角色谓词**（真 artic 由 subtype 派生、打散组由 railflag 派生，二者共用同一语义层）；
运营/身份消费点保持问真位（打散组不被当作"焊死 part"挡住独立操作）。
用户效果：① 打散暂存状态 = railflag 角色位 + 覆写字段入档；② 假组节像真 artic 一样
被 GRF 回调（0x4D 位次 / cargo overlay / moving unit / 颜色 / refit 共享）消费。

## 1. 现状（代码实证，2026-09-06 核实）

- 段模型已落地：`CmdMakeSegment` / `CmdDemoteSegment`（vehicle_cmd.cpp:354/394），
  `SegmentFront` railflag=2，尾假引擎 `SetSegmentTailFakeEngine`（314）/
  `ClearSegmentTailFakeEngine`（329）。**两命令均未处理 artic 组**。
- `VehicleRailFlag`（train.h:27-51）：uint8_t 枚举，bitset 底层 uint32_t；最高现为
  23（ForceFlipReverse），24/25 空闲。railflags 随 train 存档（EnumBitSet 整存），新位免改 SL。
- 身份 API（ground_vehicle.hpp:330-463 全齐）：Set/Clear ArticulatedPart / Engine /
  Wagon / FreeWagon / FrontWagon / Front。`IsArticulatedPart()`（ground_vehicle.hpp:460、
  vehicle_base.h:1145）= subtype 位。
- 组遍历原语（vehicle_base.h:1154-1237）：`HasArticulatedPart` / `GetNextArticulatedPart` /
  `GetFirstEnginePart` / `GetLastEnginePart` / `GetNextVehicle` / `GetPrevVehicle` 全以真位实现；
  `GetNextUnit/GetPrevUnit/GetLastUnit`（train.h:273-303）叠跳 rear-dualheaded。
- 0x4D 位次（newgrf_engine.cpp:754-763）：回走 `u->IsArticulatedPart()` 数 before、
  前进 `u->HasArticulatedPart()` 数 after；缓存 NCVV_POSITION_IN_VEHICLE。
- 全仓 `IsArticulatedPart()` 消费点 **43 处**（grep 实证），分层见 §7。
- 论证A 覆写字段（weight/power/max_speed override）**尚未加**；`GetWeightWithoutCargo`
  （train.h:351）、`GetPower`（train.h:420）、`GetMaxWeight`（train_cmd.cpp:9958）现状以
  subtype 真位跳过 artic part；`GetMaxWeight` 注释：artic part 不计车体自重。
- artic part 现场 = 3 列三节铰接车（每列 3 个 vehicle 组成 1 个不可拆 artic 单元，父车+2 parts），
  是段模型 / couple / 翻转的主要对象。

## 2. 拍板范围（本次 = 论证A + 乙 一条流水线）

| 项 | 做 | 不做（本阶段） |
|---|---|---|
| 打散 | artic 组→普通车，保持一条链，仅 depot 升段入口 | 运行中动态解构；拆成多条独立链 |
| 状态暂存 | 角色位 + 数值覆写 + （视觉烘焙**预留字段**） | 视觉烘焙本体（阶段外，待 §10.2 实测后再定） |
| 组行为 | 渲染/遍历/组单元层按组角色消费（§7 分层） | depot 内组内节**自由拖出**（组仍是运营整体，见 §8） |
| 数值守恒 | weight/power/max_speed 逐节覆写（论证A 拍板规则） | 容量/速度曲线等其它属性覆写 |
| 还原 | CmdDemoteSegment 反向：清覆写 + 清角色位 + 还原 subtype | — |
| 换代/refit | 保持真位语义（打散组=独立车各自换代/refit 待实测） | 整组换代联动重做打散态 |

## 3. 数据模型

### 3.1 角色位（VehicleRailFlag 新增，train.h:50 后）

```cpp
ArticGroupHead   = 24, ///< R3R：打散后 artic 组的组首（父车角色）。真组由 subtype 派生，不带此位。
ArticGroupMember = 25, ///< R3R：打散后 artic 组的组内节（部件角色）。真 artic part 不带此位。
```

设计要点：
- **真 artic 组不打位**——真态完全由 subtype 位派生，零迁移；打散组才用位。
- 双位（Head+Member）而非单位：组首可 O(1) 识别（0x4D 停止点 / moving unit 起点 /
  颜色归属 / 计数一次），重排后"谁在首"由位迁移显式维护，不靠"第一个非 member"隐式推断。
- 位置语：Head 是父车角色（自身非 part 语义），Member 是部件角色。

### 3.2 组角色谓词（统一语义层）

基类（vehicle_base.h，紧邻 :1145）：对全部 ground vehicle 的**通用默认 = 真位派生**，
road/ship 打散不存在，天然只认真 artic：

```cpp
/// 该车是否 artic 组内"部件角色"（road/ship 恒=真位）。
inline bool IsArticGroupMember() const { return this->IsArticulatedPart(); }
/// 该车是否 artic 组"组首/父车角色"。
inline bool IsArticGroupHead() const
{
    return !this->IsArticulatedPart() && this->Next() != nullptr && this->Next()->IsArticulatedPart();
}
/// 该车是否处于 artic 组语义内（head 或 member）。
inline bool InArticGroup() const { return this->IsArticGroupHead() || this->IsArticGroupMember(); }
```

Train 覆写（train.h，紧邻 :164 railflag 封装区）：叠加 railflag 后仍兼容真态。

```cpp
inline bool IsArticGroupMember() const
{
    return Vehicle::IsArticulatedPart() || this->flags.Test(VehicleRailFlag::ArticGroupMember);
}
inline bool IsArticGroupHead() const
{
    return (!this->IsArticulatedPart() && this->Next() != nullptr && this->Next()->IsArticulatedPart())
        || this->flags.Test(VehicleRailFlag::ArticGroupHead);
}
```

设置/清除（train.h，与 SetSegmentFront 同风格）：

```cpp
void SetArticGroupHead()   { this->flags.Set(VehicleRailFlag::ArticGroupHead); }
void ClearArticGroupHead() { this->flags.Reset(VehicleRailFlag::ArticGroupHead); }
void SetArticGroupMember() { this->flags.Set(VehicleRailFlag::ArticGroupMember); }
void ClearArticGroupMember(){ this->flags.Reset(VehicleRailFlag::ArticGroupMember); }
```

**实现要点（易错）**：通用 Vehicle 代码（0x4D、economy.cpp、vehicle_gui/vehicle.cpp）拿不到
Train railflags，必须在这些点特判 `v->type == VehicleType::Train` 后走 Train 谓词，或先经
`Train::From()`。Train 上下文代码直接换谓词调用即可。

### 3.3 覆写字段（论证A，防打散后 ConsistChanged 重统计翻倍）

位置：`Train`（仅火车 artic 需要；road 不覆写）。字段：

```cpp
uint16_t weight_override;      // 车体自重覆写，INVALID(0xFFFF) = 回落 record
uint16_t power_override;       // 动力覆写（Wagon 身份节也要能出 P/N，R3R 假引擎先例 train.h:431）
uint16_t max_speed_override;   // 极速覆写
```

读点改 override 优先（实现顺序 = 先 override 再原逻辑，**override 分支须在身份判定前**）：
- 自重：train.h `GetWeightWithoutCargo`(351)；train_cmd.cpp `GetMaxWeight`(9958) 的 artic 跳过分支。
- 动力：train.h `GetPower`(420)。
- 极速：极速读取点编码时定位（PROP_TRAIN_SPEED / record max_speed），override 优先。

## 4. 打散流水线 `DearticulateChainWithSnapshot(Train *head)`

入口：`CmdMakeSegment` execute 分支头部（vehicle_cmd.cpp:365），先于 SetSegmentFront /
SetSegmentTailFakeEngine / ConsistChanged（沿用形态A §5.7 时序）。

对"被升段的 artic 组"（组首 + 连续成员，全链可能含多组或普通车，只动本组）：

```
1. 快照：W = 父车 GetWeightWithoutCargo()（artic 态=record 规格）
          P = 父车 GetPower()
          S = 父车规格极速
2. 逐节（首节=父车，次节起=parts）：
   - 摘真位：ClearArticulatedPart()；父车本就无 artic 位不动。
   - 重录身份：switch (RailVehInfo(engine_type)->railveh_type) {
                  Wagon/Multihead? -> SetWagon/ClearEngine……（保守真车，沿用形态A §4，
                  注意 Multihead 保守置 Engine 的既有疑问，先按形态A 规则）
                  default        -> SetEngine/ClearWagon
                }
   - 打角色位：组首 SetArticGroupHead()；成员 SetArticGroupMember()。
   - 写覆写：组首 weight = q + r；成员 weight = q（q=W/N, r=W%N，Σ守恒）；power 同规则；
     max_speed 每节 = S。
3. 既有逻辑照旧：SetSegmentFront / SetSegmentTailFakeEngine / ConsistChanged(CCF_ARRANGE)。
```

## 5. 还原流水线 `RearticulateChain(Train *seg)`

入口：`CmdDemoteSegment` seg 分支（vehicle_cmd.cpp:413），在 ClearSegmentFront +
ClearSegmentTailFakeEngine 之后、ConsistChanged 之前。

```
逐节（带 ArticGroupHead/Member 位的车）：
- 清覆写三个字段（= INVALID，回落 record）。
- 摘角色位：ClearArticGroupHead() / ClearArticGroupMember()。
- 还原 subtype：ClearWagon/ClearEngine（按当前身份）→ SetArticulatedPart()，
  父车（原 head）不 Set（回到"父车无 artic 位"原始态）。
- 组内其余普通节（混入的散车）不动。
```

注意：若打散后组内节序曾被重排（§8），还原按"原组块"遍历（组角色标记仍在），
不依赖原链序——head 恒为父车角色。降级还原仅限 depot 停稳独立段（既有校验）。

## 6. 与既有段规则配合

- 链尾假引擎：Dearticulate 先于 SetSegmentTailFakeEngine；Rearticulate 在
  ClearSegmentTailFakeEngine 之后（形态A §5.7，防 subtype 叠加）。
- 组首 = 打散组 head 角色位落在组首节。段翻转/换端后组内节序可倒序（打散后每节是
  普通车，无"父车必须先于儿子"焊死约束），角色位随重排迁移（§8）。
- GetLastChainVehicle（vehicle_cmd.cpp:294）遍历真车：打散组内节已是普通车（无真位），
  不再被 HasArticulatedPart 跳过 → 链尾假引擎作用到物理链尾（升段语义本来就该如此）。

## 7. 消费点分层映射（43 处，grep 实证）

分色原则：
- **组语义层（改调 IsArticGroupMember/Head/InArticGroup 或 Train 特判）**：artic 组
  是"一个单元"的消费点——乙态打散组要继续表现组行为的地方。
- **身份/运营层（保持 IsArticulatedPart 真位不动）**：假组节要当独立普通车操作的地方
  （depot 清单、换代、独立统计）；真 artic part 继续被排除。

| # | 文件:行 | 现状语义 | 分层判定 |
|---|---|---|---|
| 1 | newgrf_engine.cpp:757 | 0x4D artic_before 回走 | **组语义**（Train 特判走 IsArticGroupMember） |
| 2 | newgrf_engine.cpp:759 | 0x4D artic_after 前进 | **组语义**（Train 特判） |
| 3 | vehicle_base.h:1156 | HasArticulatedPart | **组语义**（Next->IsArticGroupMember） |
| 4 | vehicle_base.h:1174 | GetEnginePartsCount | **组语义** |
| 5 | vehicle_base.h:1188/1199 | GetFirstEnginePart 回走 | **组语义** |
| 6 | vehicle_base.h:1234 | GetPrevVehicle 回走 | **组语义** |
| 7 | train.h:356 | GetWeightWithoutCargo artic=0 | 保持真位（打散组无真位→自然实算+覆写） |
| 8 | train.h:423 | GetPower artic=0 | 保持真位（覆写层在身份判定前已接管） |
| 9 | train_cmd.cpp:314/316 | TCF_MOVING_UNIT_START | **组语义**（组首为 moving unit start） |
| 10 | train_cmd.cpp:346/381 | cached_num_engines 组内只计父车 | **组语义**（组首计、成员不计） |
| 11 | train_cmd.cpp:1918 | assert 禁插 artic 组中间 | 保持真位（真 artic 禁插；打散组可插=新自由；assert 只在真位路径触发） |
| 12 | train_cmd.cpp:2049 | 统计跳过 artic | 保持真位（真 artic 不计；打散组=普通车计入） |
| 13 | train_cmd.cpp:4118 | R3RFlipChainBySegments 组块整体收集 | **组语义→改为逐节倒序重排**（§8 治本） |
| 14 | train_cmd.cpp:9963 | GetMaxWeight artic 跳过车体 | 保持真位 |
| 15 | train_cmd.cpp:10030 | 动力归属特判 | 保持真位（打散组无真位自然归节） |
| 16 | vehicle.cpp:1043 | IsEngineCountable | **组语义**（组只算一次；逐点核对后定） |
| 17 | vehicle.cpp:3143 | 颜色随组首 | **组语义** |
| 18 | vehicle.cpp:4913 | 循环跳 artic | 按上下文核对（编码时定位） |
| 19 | vehicle_gui.cpp:389 | cargo overlay 组内合并 | **组语义** |
| 20 | vehicle_gui.cpp:1353 | 清单计数跳过 artic | 保持真位（depot/清单=运营层） |
| 21 | train_gui.cpp:141/364/378/465 | 车辆页 artic 统计/绘制 | **组语义** |
| 22 | economy.cpp:1900/2089 | 站台移动单元装载 | **组语义** |
| 23 | vehiclelist.cpp:90 | depot 清单过滤 | 保持真位（假组节要在清单里可见可操作） |
| 24 | vehicle_cmd.cpp:616 | refit 整组共享 | **组语义**（打散组 refit 仍整组同 cargo） |
| 25 | autoreplace_cmd.cpp:149/159/285/641/946 | 换代跳过 artic | 保持真位（打散组=独立车各自换代，待实测） |
| 26 | ground_vehicle.cpp:114 | CargoChanged artic 分支 | 按上下文核对（论证A §6.1） |
| 27 | roadveh_cmd.cpp:2372 / roadveh.h:234,259 | road artic | 保持真位（road 不打散，不动） |
| 28 | tbtr_template_vehicle.h:154/159 | 模板车 | 保持真位（虚拟车模板，不动） |

## 8. 角色迁移（重排/翻转/换端）

- **组内节序可重排**：打散组内节是普通车，depot 重排/段翻转可逐节倒序（不再"块整体
  搬移"）。组角色位随节走，重排后 **head 必须仍在组首**——重排/翻转代码负责把 head 位
  从旧组首迁到新组首（若原组首离开首位则摘其 head、给新首 SetArticGroupHead），
  成员位原地保留。执行点：段翻转 `R3RFlipChainBySegments`（train_cmd.cpp ~4095）与
  depot 拖拽换位逻辑（编码时定位 depot_gui.cpp）。
- **组不可劈（本阶段约定）**：打散组仍是段/运营的一个整体单元（fakeflag §5.5）——
  组内节不可被拖出组、外部散车不可拖入组中段。Head/Member 连续块完整性由 depot 操作
  约束维持；违反时（防御性）在打散组边缘做拼接时校验。
- 还原（降级）时按角色位遍历组块，不依赖原链序。

## 9. 存档 / 缓存刷新

- 角色位随 train railflags 自动入档（EnumBitSet<uint32> 整存）；覆写三字段入 Train SL
  （读档后仍须守恒，不能 NOSAVE）。
- 位次/视觉类缓存：打散、还原、重排后需 `ConsistChanged(CCF_ARRANGE)` +
  `InvalidateNewGRFCache()`（NCVV_POSITION_IN_VEHICLE 等）；depot/列表窗口
  InvalidateVehicleListWindows 按既有段命令习惯补。

## 10. 危险点 / 待实测

1. 覆写字段 INVALID 语义与 0 值区分（W 可能为 0？artic 无动力车 P=0 → 0 是合法值，
   INVALID 用 0xFFFF）。
2. 动力给 Wagon 身份节（GetPower 在身份判定前输出 P/N）——depot/GUI/挂车副作用，
   R3R 假引擎先例（train.h:431）同类。
3. 组语义谓词在通用代码（0x4D / economy / vehicle_gui）只对 Train 生效——road/ship
   不得误走（特判 type）。
4. 打散组内节 cached_veh_length / depot 排布 / 渲染观感（artic 短车转整节，couple 死循环
   曾因短车容差极小，见 couple 备忘）——需实测 depot 布局与出库运行。
5. 还原后 artic 组若曾被数值覆写读档……（还原只清覆写字段+角色位+subtype，无需重算）。
6. IsEngineCountable / 计数口径（vehicle.cpp:1043）到底按组还是按车——列入编码时逐点核。
7. depot GUI 对"组不可劈"的表达（禁止把组内节拖出的 UI 校验点）。
8. 多 artic 组同链（现场 6 节=2 组）：打散只动被升段的一组；升段命令按段链操作时
   链上多组的识别（§4 逐组处理 or 只处理链首组）——编码时按命令语义定。

## 11. 实施顺序

| 步 | 内容 | 验证 |
|---|---|---|
| 1 | train.h：VehicleRailFlag +24/25；角色位 Set/Clear；Train 版谓词覆写（3.2） | 编译 |
| 2 | vehicle_base.h：基类谓词默认实现（3.2） | 编译 |
| 3 | Train：覆写三字段 + SL 入档 + GetWeightWithoutCargo/GetPower/极速读取 override 优先 | 编译 |
| 4 | vehicle_cmd.cpp：`DearticulateChainWithSnapshot` + `RearticulateChain` + 两命令接入 | 编译 |
| 5 | 组语义消费点分层改造（§7 表，Train 上下文直改、通用代码 Train 特判） | 编译 |
| 6 | 角色迁移接入段翻转/重排（§8） | 编译 |
| 7 | depot 不可劈校验 + 窗口失效刷新 | 编译 |
| 8 | 全量回归 + 存档兼容核对 + 用户实测（危险点 §10） | EXITCODE=0 + 实测 |

## 12. 关联

- `R3R_artic_fakeflag_state_memo.md`（形态甲/乙论证 + 视觉烘焙前置，阶段外）
- `R3R_artic_detach_snapshot_memo.md`（论证A 数值覆写拍板规则）
- `R3R_artic_formA_detach_memo.md`（失败前身与回滚；形态A §4 重录身份规则被本方案沿用）
- 记忆 51063123（段两端假引擎）/ 97996690（段序保持翻转）/ 31054337（primary/假引擎迁移）
- `R3R_artic_couple_memo.md` / `R3R_couple_fold_catchup.md`（artic 块不可拆死结的既有语义，
  本方案是治本方向）

## 13. 实施记录（2026-09-06 落地）

按 §11 步骤 1→7 已全部完成，编译 EXITCODE=0。落地方式为
**基类组角色谓词 virtual 化**（vehicle_base.h 对全部 ground vehicle 的通用默认 = 真位派生；
Train 在 train.h 覆写叠加 railflag 位）+ 组遍历原语内部判定改为组谓词，逐点改造明细：

### 13.1 与原 §7 表的两处判定修订（编码时逐点核对结论）

1. **§7 #4 GetEnginePartsCount 判回真位**（推翻表内"组语义"）。全仓唯一外部用户 =
   `ground_vehicle.cpp:118`（CargoChanged 重量分摊 articulated_weight = engine_weight /
   part_count）。若组语义化，打散组 head 的 GetWeightWithoutCargo 已返回 baked 均分值
   （W/N+余数），再被 N 均分会二次稀释 → 打散组重量错误。真位下打散组 head 无真 parts，
   count=1 → articulated = 自身 override，逐节独立相加 Σ 守恒。vehicle_base.h 已加注释说明。
2. **§7 #26 ground_vehicle.cpp:114 CargoChanged 判保持真位**（论证A §6.1 印证）。打散组
   成员非真 artic part，走"独立真实车"分支：GetWeightWithoutCargo override 分支返回
   baked 份额、cargo_weight 各自相加 → 总重 Σ(W_i 均分 + cargo_i) 守恒。若改问
   IsArticGroupMember 会把成员误拉进 artic 分摊路径二次稀释。

### 13.2 落地改动一览（压缩会话后逐文件实测核对）

- **vehicle_base.h**：`IsArticGroupMember/IsArticGroupHead/InArticGroup` 声明为 virtual
  （通用默认 = 真位派生，road/ship 不打散天然只认真 artic）；`HasArticulatedPart`
  （Next->IsArticGroupMember）、`GetFirstEnginePart`（回走谓词）、`GetPrevVehicle`
  （回走谓词）内部判定组语义化；`GetEnginePartsCount` 保持真位（见 13.1-1）。
- **train.h**：三谓词 override（叠加 railflag 位）；Train 版内部一致性修正（IsArticGroupHead
  也认真位 Next part / 本节点头位）；步骤1 角色位 Set/Clear + 步骤3 override 读点
  （GetWeightWithoutCargo / GetPower / 极速读点，override 分支在身份判定前）。
- **train_cmd.cpp**：TCF_MOVING_UNIT_START（314/318）、cached_num_engines 组首计成员不计
  （350/385）改组谓词；`R3RFlipChainBySegments`（§8）打散组逐节收集（真位 artic 块仍整块）、
  翻转后 `R3RReassignArticGroupRoles(new_head)`（4092-4120）归一角色：连续角色块首 =
  清 member + SetHead，其余节 = 清 head + SetMember；真位保持点（1922 assert / 2049 统计 /
  9963 GetMaxWeight / 10030 动力归属）未动。
- **vehicle_cmd.cpp**：`DearticulateChainWithSnapshot`（327）组判定必须直接看
  `Next()->IsArticulatedPart()` 真位（组语义化后的 HasArticulatedPart 会把已打散组当
  真组再次均分 → 已修并注释）；refit subtype 组内共享（737）改组谓词；
  CmdMakeSegment(486)/CmdDemoteSegment(541) 接入，窗口刷新 InvalidateWindowData(Depot)。
- **newgrf_engine.cpp**：0x4D artic_before 回走问组谓词（757）；artic_after 经
  HasArticulatedPart（已组语义化）自动覆盖。
- **economy.cpp**：ReserveConsist 装载循环 IsArticulatedPart → IsArticGroupMember（1900）；
  2089 IsMovingUnitStart（TCF 组语义化后天然正确，未改）。
- **vehicle.cpp**：IsEngineCountable（1043）、livery 颜色随组首（3143）改组谓词；
  GetVehicleSet 组块含成员（4913，refit/卖车整组一次性纳入）。
- **vehicle_gui.cpp**：cargo overlay 组内合并（389）改组谓词；清单计数（1353）、
  vehiclelist.cpp:90 保持真位（运营层：成员独立可见）。
- **train_gui.cpp**：选择延伸（141）、cargo summary 合并（364）、长度合并（378）、
  细节行合并绘制（465）改组谓词。
- **depot_gui.cpp**：零改动——432/1042 等全部经 HasArticulatedPart/GetFirstEnginePart
  （组语义化后自动覆盖）：点击组内任一节经 GetFirstEnginePart 归到组首 → 拖拽整组、
  拖拽长度延伸整组、tooltip 计数整组算 1 → §8"组不可劈"由谓词层自动达成（§11 步7 校验
  需求消失）。
- **autoreplace / 模板车 / road**：保持真位（§7 #25/#27/#28），未动。

### 13.3 遗留（非本阶段范围，记录备查）

- depot 内无"单节拖出打散组"UI 通道（谓词层已整组化）；若未来开放组内单拖换位，
  需在 CmdMoveRailVehicle 加 head 位迁移（§8 换位逻辑）。
- ReverseTrainDirection force 物理换端（ReverseTrainSwapVehicles）对打散组：链序不变、
  角色位无需迁移；artic 块安全性属既有 R3R 待查项（记忆 21823468），不在本任务。
- §10 危险点 4（cached_veh_length / depot 排布观感）与 6（计数口径）已按组语义落地，
  是否合用户预期需实测确认；步骤8 全量回归 + 存档兼容核对待用户执行。
