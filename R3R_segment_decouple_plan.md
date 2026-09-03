# R3R「按段解挂」技术方案（评审稿）

> 目标版本：HEAD（fc585e4bc8 之后，含折叠修复 `86f5a73107` 一系）。
> 参考资料：`stash@{0}`（OT_TURN_SEGMENT WIP）、`stash@{1}`（选端挂车 group 框架）。
> 状态：**M1/M2/M2-UI 已实现并编译通过**（openttd.exe，Debug / Ninja + MSVC 14.51，2026-09-02 增量构建验证，86/86 链接成功）；M3 未做。

---

## 1. 需求对齐（以澄清回答为准）

| 项 | 结论 |
|---|---|
| 入口 | OT_DECOUPLE 订单自动解挂 **与** depot 手动拖车解挂，都要按段工作 |
| 语义 | 解挂点 = **耦合边界**。4节(A) 挂 5节(B) 成 9 节后，解挂恢复 [A4][B5]，而不是"数 N 节" |
| 谁走谁留 | 不预设谁是"机车段"；由每段各自恢复耦合前身份（orders / unit number / 编号），再由调度（排程）决定后续动作——HEAD 的 orders 交换机制已天然支持 |
| 复合解挂 | 预留多段（A+B+C）解挂的接口，本期只论证不完整实现 |
| 实现方式 | 先出方案，确认后再动代码 |

---

## 2. 现状（HEAD 代码事实，全部核对过）

### 2.1 耦合 `Couple(Train *v, Train *u)`（train_cmd.cpp ~3793）
- `v` = 执行 GOTO_COUPLE 的机车链；`u` = 等待被挂的车组（FreeWagon / ConsistGroup / 带 WAIT_COUPLE 的普通列车，三者都收）。
- 合并：`ArrangeTrains(&v, v_last, &u_head, u, true)` → u 链接到 v 链尾；**u_head（即 u 原链头）成为合并链中的"被挂段首车"**。
- orders 契约：`v->orders_backup = v->orders`（机车自身排程备份）；整车采用 `u->orders`（车组排程）；unit number 同理（机车备份到 `unitnumber_backup`，整车继承 u 的编号）。
- 折叠修复：统一 u 全部车辆方向为 v、清双链 lookahead、清 reserved。
- 合并后 `u` 清 FrontEngine/FreeWagon、GroupID 归 DEFAULT、`DestroyConsistGroup(u)`（若是车组）。

### 2.2 解挂链（train_cmd.cpp）
```
TrainLocoHandler(~7853)               停车 + at_order_dest(站台/车库格)
  └─ 门控：cur_real 是 DECOUPLE，或 next real 是 DECOUPLE
       └─ DecoupleTrain(consist, allow_in_depot=true)
            ├─ CanDecouple / depot 内豁免
            ├─ GetDecoupleVehicle(v)     ← 按订单 num_decouple 找解挂点
            │     ├─ 0 → GetDecoupleVehicleAuto(v)  按引擎分布估算（3463）
            │     └─ 沿 GetNextVehicle() 数 N 节（含多机头成对逻辑）
            ├─ TryTrainDecouple(v, u)    拆链 + 回滚
            ├─ 尾部成形：u 是引擎→FrontEngine；否则 CreateConsistGroup + unit number（consist 化）
            ├─ orders 交换恢复（3604-3696）：
            │    u->orders = v->orders（整车排程给解出段）
            │    v->orders = v->orders_backup（机车恢复自己排程）
            │    u 跳到其排程中当前 DECOUPLE 之后最近的 WAIT_COUPLE
            │    unit number 各自归还
            └─ u->ClearReservationUnderConsist + ReserveTrackUnderConsist（占道等待）
```

### 2.3 订单参数布局（order_base.h / order_type.h）
- `flags` bit0 = `OrderDecoupleFlags`（ODF_DECOUPLE）；bit1..7 = `GetNumDecouple()`（0 = auto"minimal unit"）。
- 订单额外信息 `OrderExtraInfo` 已含 **`decouple_first_orders` / `decouple_second_orders`**（`ODOF_KEEP_ORDERS / KEEP_ORDERS_NO_LOAD / INHERIT_ORDERS / WAIT_FOR_COUPLE`，order_type.h:145），Getter/Setter 与 savegame 支持（`XSLFI_DECOUPLE_ORDERS`，order_sl.cpp:147）**都已就位，但没有任何执行端消费它**——是预留的"解挂后两段各自 orders 策略"字段。
- slot 类订单用 extra `xdata`（GetXDataLow 等）。

### 2.4 手动解挂（depot）
- depot 拖车走原生 `CmdMoveRailVehicle`（train_cmd.cpp:2220），**无任何段概念**。
- R3R 至今的 depot 交互未发现"解挂几节对话框"——待你确认"现在解挂按节数"具体指哪个入口（推测 = 订单的 auto/节数语义，而非 depot 拖车）。

### 2.5 stash@{0} 可用的"段"资产（均 WIP 未提交）
- `VehicleRailFlag::SegmentFront`（复用原 VRF_HAVE_SLOT 空位，随 flags 存档，**无需 SL 升级**）。
- `Train::slot_id`（NOSAVE，段首车携带的路由槽）。
- `Couple()` 对 **有动力**(TrainHasPower) 的被挂段 `u->SetSegmentFront()` + `R3RSealSegmentSlot(u)`。
- `DecoupleTrain` 段分支：`FindDecoupleOrder` → `FindSegmentBySlot`（slot 定位，须==尾段否则报错）/ `GetTailSegmentHead`（链尾倒找最后一个 SegmentFront = 尾段头）→ 解尾段；无段则回退旧按节数逻辑。解出后 `R3RUnsealSegmentSlot(u)`。
- `OT_TURN_SEGMENT`（=18）订单：让停在原地的列车/段反转链指针原地掉头；配套 `MOF_DECOUPLE_SLOT / MOF_TURN_SEGMENT_SLOT`、setting `r3r_slot_seal`。

---

## 3. 段模型设计

### 3.1 定义
**段（segment）** = 一次耦合动作产生的、独立身份的车组单元。

链条上每个**被挂入**的段在其首车打一个 `SegmentFront` 标记；**链头段（含机车）不标记**，隐含为首段。

例（多段）：机车 A 在站1挂 B、在站2挂 C → 链 `[A…][B₁★…][C₁★…]`，★ = SegmentFront。
解挂 = 在某个 ★ 处断开：★ 车及其后整段被解出；★ 标记随解出段清除（该段恢复"纯链"身份，可再被别的机车当新段挂）。

### 3.2 关键决策 D1：哪些耦合组算"段"？（需你拍板）
- stash 做法：`TrainHasPower()`（链内有 `IsEngine && GetPower()>0`）才标 SegmentFront——无动力 freight 不算段，回退旧按节数逻辑。
- 按你的例子（5 节多半是无动力车组），5 节**也应是一个可识别段**，否则"4+5 → 解回 4/5"在 B 无动力时根本不成立。
- **推荐**：只要 `Couple()` 合并进来就算一段（不加 TrainHasPower 条件）；解出后是否有动力由现有 consist 化机制兜底（已能处理无动力解出段）。TrainHasPower 作为可选项留作"禁止无动力段"开关。

### 3.3 谁走谁留 = 现有 orders 契约（无需新机制）
解挂后：
- 头段（v，机车段）：恢复 `orders_backup`（耦合前排程 + 编号）→ 调度自然安排它下一步去挂别的车/回库；
- 解出段（u，最后被挂段）：继承整车排程 + 编号，并跳到下一个 WAIT_COUPLE → 留在站台/车库等下一台机车。

这个契约对"解最后一个耦合边界"**已经正确**，是本次复用的核心资产。

---

## 4. 技术设计

### 4.1 M1 核心：段标记 + 订单自动解挂按边界（改动集中在 train_cmd.cpp / train.h）

**(a) 耦合打标**：`Couple()` 在 `DestroyConsistGroup(u)` 前后、`NormaliseTrainHead(v)` 之前加：
```cpp
u->SetSegmentFront();   // u 合并后仍是"被挂段首车"
```
> 注意：`u` 此时可能正被 `ArrangeTrains` 挂接为 `v_last->next`；须在结构已就位、方向统一后打标。与折叠修复（方向统一、lookahead 清除）无冲突。

**(b) 解挂点改为"尾段边界"**：新增（移植 stash）：
```cpp
static Train *GetTailSegmentHead(Train *v);   // v->Last() 倒找 IsSegmentFront
```
`GetDecoupleVehicle`/`DecoupleTrain` 改为：
1. 若订单带"段模式"（见 4.3）且链上有 SegmentFront → `u = GetTailSegmentHead(v)`（默认：解最后一段）；
2. 链上无 SegmentFront（机车自带普通车厢、无耦合段）→ **回退现有按节数逻辑**（原版行为不破坏）；
3. slot 指定段 → 移植 `FindSegmentBySlot` + "非尾段拒绝"（报错，见 stash）。

**(c) 解出段收尾**：解出后 `u->ClearSegmentFront()`（+ 可选 slot unseal），其余（orders 交换 / consist 化 / 占道 / unit number）全部复用现有 `DecoupleTrain`。

### 4.2 DECOUPLE 订单参数语义（需定 D2）

现状 num_decouple 在 R3R 语义下已无意义（auto 按引擎数猜，正是 bug 来源）。候选方案：

- **方案 A（推荐，向后兼容）**：保留 `num_decouple` 存储位（0 = auto）。新增**订单模式**存入 extra（`xdata` 空位，参考 stash 的 SetDecoupleSlot）：
  - `auto`（默认）= 按段：有耦合段解尾段，无则回退旧节数；
  - `count-N` = 旧显式节数（给脚本/旧档）；
  - `segment-slot` = 按 slot 定位段（M3）。
  - UI（order_gui）显示详情改为按模式：`STR_ORDER_DECOUPLE_DETAILS_SEGMENT`（如 "Decouple rear segment (5 units)"）。
- 方案 B：直接把 bit1..7 重新定义为"解第 K 段"。破坏旧档，不推荐。

### 4.3 depot 手动解挂按段（M2，需先定 D3）
depot 车辆列表拖车 = `CmdMoveRailVehicle`。两条实现路线：
- **路线 1（命令层）**：拖动时把选中范围"吸附"到段边界——拖头段 A 即整段、拖 B₁ 即整个 B 段；需要动 `CmdMoveRailVehicle`/其 UI 调用端。
- **路线 2（UI 层）**：depot 选中列车后提供"解挂段"下拉（列出每段：段1 4节 / 段2 5节…），点击即分离；不碰命令层。
- 待你确认现有 depot 交互后再细化（此文件在实现前可补一节）。

### 4.4 复合解挂预留（M3 论证，本期不做）
- 链上多个 ★ 时，"解最后 K 段" = 解挂点取从尾数第 K 个 SegmentFront。接口用现有 `u` 语义即可（u..尾）。
- 难点在 orders/unit 归属：>1 段的排程恢复没有现成契约（orders_backup 只备份一层）。论证方向：段身份应携带自己的 orders 快照（类似 slot_id 存段首车），或复合解挂只允许发生在"组段最后一段仍带 WAIT_COUPLE 排程"的受限场景。
- **本期只把 ★ 标记做成可枚举**（`GetSegmentHead` 遍历助手），不解 K 段逻辑。

### 4.5 slot / seal / TURN_SEGMENT（stash 元素取舍，D4）
- slot 的动机：被挂段耦合期间摘除其路由占用（`r3r_slot_seal`），避免信号把 9 节整列误当"段在等号志"；解挂恢复。若你的信标逻辑不需要，可整个砍掉（只留 SegmentFront），M1 更小。
- TURN_SEGMENT：解"头段"需先把头段掉到尾部，属配套能力，与段本身解耦。建议 M3。

### 4.6 存档与兼容
- `SegmentFront` 用 flags 位 = 自动随 Vehicle flags 存档，**旧档打开后无标记**（老档里已耦合的列车没有段边界，按 4.1(b) 回退旧逻辑，行为不劣化）✓。
- slot_id（若做）：NOSAVE，需 `SAVELOAD` 版本升级或在 load 后重建（耦合时本来就从 `TraceRestrictGetVehicleSlots` 抓，可设计为惰性重建）。
- extra `xdata` / ODOF 字段已有 savegame 特征，新增模式位需确认 XSLF 版本号（order_sl.cpp）。

---

## 5. 改动清单

| 文件 | 改动 |
|---|---|
| `src/train.h` | `VehicleRailFlag::SegmentFront`（位2）+ `Is/Set/ClearSegmentFront`；可移植 stash 的 `slot_id`（仅当 D4 做 slot） |
| `src/train_cmd.cpp` | `Couple()` 打标；新增 `GetTailSegmentHead`（+按需 `FindSegmentBySlot`）；`GetDecoupleVehicle`/`DecoupleTrain` 段分支与回退；解出段清标 |
| `src/order_base.h` | （D2 方案 A 时）DECOUPLE 模式位 Getter/Setter；可移植 stash 的 DecoupleSlot 存取 |
| `src/order_gui.cpp` | DECOUPLE 详情行按模式显示；添加/编辑交互（batch3 局部） |
| `src/lang/*.txt` | 新字符串（"Decouple segment"、slot 相关错误等） |
| `src/sl/` | 仅当 slot/模式位需要持久化扩展 |
| `src/settings.ini` + `settings_type.h` | 仅当引入 `r3r_slot_seal` 类开关 |
| `src/order_cmd.cpp` | `OrderModify` 校验与 MOF（模式/slot 变更） |

### 改动顺序建议
1. train.h flag → 2. Couple 打标 → 3. 段枚举助手 → 4. DecoupleTrain 段分支（先不碰 UI）→ 5. order 模式位 + order_gui 显示 → 6.（编译 + 单测 4+5 场景）→ 7. depot 手动（M2）→ 8. M3 扩展。

---

## 6. 验证场景（回归/单测）
1. **基础**：A(4, 机车带排程) GOTO_COUPLE 挂 B(5, WAIT_COUPLE) → 9 节开至解挂点 → DECOUPLE → 断在边界 → A 回自己排程、B 回到 WAIT_COUPLE 原地等待，双方各 4/5 节、编号正确。
2. **原版不破坏**：普通机车+车厢（无耦合段）→ DECOUPLE auto 仍按旧逻辑解。
3. **depot 手动**：9 节入 depot → 按段分离 A 或 B。
4. **方向/折叠回归**：同向/反向挂车均不折叠、解挂后不二次掉头搁浅。
5. 存档往返（Save/Load 后标记仍在；旧档升级无崩溃）。

---

## 7. 决策定稿（用户确认 2026-09-02，D1 于 09-02 修订）
- **D1（修订）**：★ 扩展为**凡 Couple 挂上的链都打标**——打标判定移到假引擎销毁前，条件 = `曾为 consist（u_was_consist）|| 含真引擎`。理由：耦合的 5 节纯车厢 consist 组（耦合时假引擎销毁变普通车厢）在 depot/运行侧也必须可识别边界，否则 depot 无法按段拖出、也无法 consist 化恢复。
- **D2**：**方案 B**——`GetNumDecouple()` 的 bit1..7 直接重定义为"解下最后 N 个段"（0 = auto = 解 1 段；N ≥ 2 = 复合解挂预留位）。不兼容旧档字段语义（R3R 无原版/JGRPP 存档兼容义务）。无耦合段时回退到原生 minimal-unit 启发（`GetDecoupleVehicleAuto`）。
- **D3**：手动解挂 = **手动拖动**（段吸附），归 M2。
- **D4**：slot/seal / TURN_SEGMENT **推迟**，不做进本期。

### M1 完成情况
- `train.h`：`VehicleRailFlag::SegmentFront = 2`（复用原 VRF_HAVE_SLOT 空位）+ `Is/Set/ClearSegmentFront()`。
- `train_cmd.cpp`：
  - 新增 `TrainHasEngine` / `GetSegmentHeadFromRear`（数第 N 个尾段首车，缺段取最前段、无段返回 null）。
  - `GetDecoupleVehicle`：有耦合段 → 按段定位解挂点；无 → 回退 auto。
  - `Couple()`：合并完成（`NormaliseTrainHead(v)` 后）按 D1 修订打标（`u_was_consist || TrainHasEngine(u)`，判定前记录 `u_was_consist`）。
  - `DecoupleTrain`：orders/unit 恢复块从"仅无引擎分支"提升为两类解挂共用（**修复了按段解下引擎段时排程不恢复的缺口**）；解出后 `u->ClearSegmentFront()`（多段解出时保留 u 之后段内标记）。
- `order_base.h`：`Get/SetNumDecouple` 文档改为"段"语义。
- 语言文件：`STR_ORDER_DECOUPLE_DETAILS(_AUTO)` 改为 segments/rear segment 文案 + 简体中文新增 couple/decouple 订单串翻译。
- 待验证：编译 + §6 场景 1/2/5（场景 3 depot 手动拖拽属 M2）。

### M2 depot 手动拖动按段（已实现 2026-09-02）
**决策（用户确认）**：
- **D5**：★ 覆盖 = D1 修订版（凡耦合链，含曾 consist 的纯车厢组）。
- **D6**：depot 分离 = **物理装配、不带排程**（不做 orders 迁移/备份；含引擎段 → front engine；纯车厢段 → 恢复 consist 身份，订单后续由 make consist/R3R 排程自行安排）。

**实现（`depot_gui.cpp`，GUI 层组合现有命令，不改命令层）**：
1. `TrainDepotGetSegmentFront(v)`：从 `v` 向链头方向找最近的 `IsSegmentFront()`，无则返回 null（链头引擎区、无段链均回退原生行为）。
2. `TrainDepotDetachSegment(seg)`：切离段（2026-09-02 抽取为独立 helper）——段范围 = `[front .. 下一边界或链尾]`，若段后有 `rest`（中间段）先 `MoveRailVehicle(rest→anchor 尾, MoveChain)` 把尾随车辆接回原链，段由此独立成链；`TrainDepotMoveSegment(front, wagon)` 调用它后再 `MoveRailVehicle(front→目标, MoveChain)` 移动整段；
   - 目标为空行（`dest==Invalid`）且段无引擎时：`SetAsFrontWagon` 恢复 consist 身份（零动力段回到"可调度车厢组"）。
3. `TrainDepotMoveVehicle`：拖动前分流——源车属段 → 走段移动；否则原样。
4. **交互变更（有意）**：段内车辆拖动一律按整段，不再支持单节抽出/插入（段 = 运营单元，单节整理应在耦合前完成）；非段车（含链头引擎区）保留原生行为。
5. **卖出整段**（2026-09-02 补齐）：`OnDragDrop` 卖出区（`WID_D_SELL` / `WID_D_SELL_CHAIN`）命中段内车时，先 `TrainDepotDetachSegment` 切离段、再 `SellVehicle(段首, SellChain)`——卖出段内任意一节 = 卖出整个段；尾随车辆与后续段留在原链，不再出现"单节拆段/跨段链卖出"。

**边界/限制（记录）**：
- 若 `front` 恰为链头（理论上 R3R 耦合不产生）：`anchor==null`，跳过回接步，整链随 `MoveChain` 移动（近似原生）。
- 两次命令非原子：第一步失败 → 原链不变可重试；第二步失败 → 停留在 `[A][R] + [S独立]` 的可用中间态，无数据损坏。
- 命令失败时：切离（第一步）失败 → 原链不变可重试；移动/卖出（第二步）失败 → 停留在 `[A][R] + [S独立]` 的可用中间态，无数据损坏。卖出整段的第二步失败同理（段已独立、未删除）。
- 段 ★ 标记：拖动/卖出不增删 ★；运行期 DECOUPLE 解出段后 `ClearSegmentFront`（M1）；depot 分离的段保留 ★ 以便再次挂接后仍可按原边界解挂。
- **生命周期防御（2026-09-02）**：`GetSegmentHeadFromRear` 改为跳过链头本体（`v->GetNextVehicle()` 起遍历）——depot 分离出的段作为独立列车重新挂接/运行时，其链头可能残留 ★；若不解挂逻辑会把机车本体当"尾段"解出（崩溃/死锁）。跳过后：带 ★ 的独立链无段可解 → 回退原生 minimal-unit 启发，安全。多段复合解挂、段 ★ 生命周期的最终清理仍留 M3 复核。

### M2-UI depot 段身份转换按钮（2026-09-02，已编译通过）

把"链身份"的升降级操作做进 depot 按钮，形成完整层级：**松散车厢链 →（Make consist）→ 编组 →（Make segment）→ 独立段**；`Demote` 每次向下降一级。

**决策（用户确认）**：
- **D7**：`Demote` 一步降一级——独立段（★，单独停放）→ 恢复为普通编组/列车（仅清 ★，保留排程）；编组（零动力 consist）→ 解散为松散车厢链（清排程 + 归还 unit number）。松散车厢链为最低层，再点报错。
- **D8**：`Make segment` 的输入 = **独立编组或带机车列车**（须为独立链首、停在 depot 内、可带排程）；松散车厢链不可直接"设为段"，须先 `Make consist`。操作幂等（已 ★ 再点无副作用）。

**实现**：
1. `command_type.h` / `vehicle_cmd.h`：新增 `Commands::MakeSegment` / `Commands::DemoteSegment`（`CMD_CLIENT_ID` + `CommandType::VehicleManagement` + `CmdDataT<VehicleID, ClientID>`，与 `SetAsFrontWagon` 同型）。
2. `vehicle_cmd.cpp`：
   - `CmdMakeSegment`：点击车辆沿 `Previous()` 解析到链首；要求 `IsEngine() && IsFrontEngine() && IsStoppedInDepot()`；Execute 时 `SetSegmentFront()` + 失效化 depot 数据（`InvalidateWindowData(VehicleDepot)`）。
   - `CmdDemoteSegment`：先向链头方向扫描 `IsSegmentFront()`——
     - **找到段 ★**：仅当该段独立停放（`seg->Previous()==nullptr && seg->IsEngine() && IsStoppedInDepot()`）→ `ClearSegmentFront()`；已耦合在其他链上的段报错（提示先拖到空行）。
     - **无 ★**：要求链首 `IsConsistGroup(front)`（即编组）且停库 → 解散：`DeleteVehicleOrders(front)`（松散链不可带排程）→ 归还 unit number（`Company::Get(owner)->freeunits[Train].ReleaseID` + 置 0）→ `GroupStatistics::CountVehicle(front, -1)` → `ClearSegmentFront()` → `DestroyConsistGroup(front)`（假引擎恢复为车厢）→ `UpdateTrainGroupID(front)`（松散链归 DEFAULT_GROUP）→ `InvalidateVehicleListWindows` + depot 失效化。
   - 两命令仅在 `DoCommandFlag::Execute` 时变更状态，其余（test/MP）只做校验。
3. `widgets/depot_widget.h`：新增 `WID_D_MAKE_SEGMENT`、`WID_D_DEMOTE_SEGMENT`、`WID_D_SHOW_SEGMENT_TOOLS`（平面容器）。
4. `depot_gui.cpp` 布局：列车底部按钮拆为两行——上排（全类型）：Build / Clone / Departures / Vehicle List / Stop-Start / Resize；下排（仅列车，`WID_D_SHOW_SEGMENT_TOOLS` 平面）：`Make consist` / `Make segment` / `Demote`。`SetDisplayedPlane(type==Train ? 0 : SZSP_NONE)`，列车窗口默认高度 123→138。
5. `depot_gui.cpp` 交互：三个工具按钮共享 object-placement 槽（`SetObjectToPlaceWnd(…, HT_VEHICLE)`）——`OnClick` 按下其一先抬起其余两者再 toggle；`OnVehicleSelect` 按按钮状态分别 `Post(STR_ERROR_CAN_T_MAKE_SEGMENT / STR_ERROR_CAN_T_DEMOTE_SEGMENT, tile, v->index, INVALID_CLIENT_ID)`；`OnPlaceObjectAbort` 一并抬起三者。
6. 语言（`english.txt` / `simplified_chinese.txt`）：新增 `STR_DEPOT_MAKE_SEGMENT` / `STR_DEPOT_MAKE_SEGMENT_TOOLTIP`、`STR_DEPOT_DEMOTE_SEGMENT` / `STR_DEPOT_DEMOTE_SEGMENT_TOOLTIP`、`STR_ERROR_CAN_T_MAKE_SEGMENT`、`STR_ERROR_CAN_T_DEMOTE_SEGMENT`（同时补上此前中文本缺失的 `STR_DEPOT_SET_AS_FRONT_WAGON` 一组）。

**边界/限制（记录）**：
- `Demote` 只作用于**独立停放**的单元：已耦合在列车上的段/编组会报错，需先在 depot 拖到空行使其独立再操作（这也避免与 M2 拖拽语义混淆）。
- 命令层不新增排程迁移：段→编组/列车保留排程；编组→松散链删除排程（最低层无排程概念）。
- 编组解散的簿记与既有代码一致：`DestroyConsistGroup` 只切 subtype 标志，unit number / group statistics / orders 由本命令负责（对照 `train_cmd.cpp` 3941-3958 吸收段路径）。
- 段 ★ 在此处是"身份标记"而非"耦合边界"，与 §3 运行期耦合段语义一致——demote 清 ★ 后该链在 depot 按普通编组/列车处理；耦合过之后再按 D1 重新打标。

**验证**：增量构建通过（86/86 链接 openttd.exe，0 error，含 strgen 语言文件再生）；按钮/命令行为需运行自检（§6 场景扩展：make consist → make segment → demote 逐级往返，松散链直连 make segment/demote 应报错）。
