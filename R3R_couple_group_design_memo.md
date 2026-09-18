# R3R 挂接分组 + 段挂接序号（排程归属）设计备忘

> 第 18 轮（2026-09-15 起）设计并落地步骤 2~3，第 19 轮续做步骤 4~6，第 20~24 轮按玩家反馈加固 UI / 多组归属，第 25~26 轮收紧硬约束与段身份语义。状态：**步骤 2（数据底座 + 存档）、步骤 3（命令）、步骤 4（专用窗口 + 语言串）、步骤 5（E1~E6 硬约束接线；第 25~26 轮补齐「统一闸门 `R3RCanCoupleNow` + 段身份谓词 `R3RIsCoupleTarget`」）、步骤 6（可见性）均已落地并编译链接通过；步骤 7 的「全量重编 + 源码/构建一致性核对 + 备忘与 KI 状态更新」已完成（见 §14.3），仅剩玩家侧游戏内实测（§14.4）**。
> 本轮任务 = 先写本备忘，再按 §9 步骤有序实现，每步独立可编译、单步不超上下文预算。
> 实施细节与实际偏差见 §12（第 18 轮）、§13（第 19 轮）、§14（第 25~26 轮加固 + 步骤 7 收尾核对）。

## 0. 一句话结论

- **「谁出调度计划」这条规则已实现**：`Vehicle::r3r_priority`（链内段序号，最小者=命令归属段）+ 挂接时被动方序号恒小于主动方 ⇒ **本轮不改判定**，只补可视化 + 存档健壮性 + 文档（§2）。
- **本轮真正的新增主体 = 挂接分组识别系统**：独立分组池 + 专用窗口 + 存档 + 联机命令，作为挂接的**硬约束白名单**（§3~§7）。
- **必须同时留好的接口：未来跨公司挂接**（§10）。硬性设计约束。

## 1. 需求（用户拍板原文，不得改写）

| # | 问题 | 用户回答 |
|---|---|---|
| q-0 | 「链里最早的段」按什么判定 | 挂接序号最小（每次挂接递增一个段序号，本链里第一个被挂进来的段（计数最小）拥有计划）。**注意：被挂的挂接序号小于挂车的挂接序号** |
| q-1 | 「挂接分组」用什么当存储载体 | 新建独立挂接分组（单独的分组池 + 专用窗口 + 存档 + 联机命令） |
| q-2 | 挂接分组怎么参与识别 | 硬约束（白名单）：只有同挂接组才允许挂接，不同组即使贴上了也不挂、原地等待。补充：**不符合组别，按照没有等待挂接的列车处理** |
| q-3 | 本轮做到哪一层 | 先写备忘，再按步骤有序进行，尽量估算好不超上下文 |
| 追加 | 未来规划 | **未来我将会设计跨公司挂接，请你务必留好接口** |
| 一轮 | 缺分组兜底 | 找不到满足条件的编组 → 原地等待（现状）；多候选取最近 |
| 一轮 | 排程归属 | 谁是这个链里面最早的段，就执行谁的调度计划 |

## 2. 现状盘点 A：段挂接序号与排程归属（已实现，本轮不改判定）

| 环节 | 落点（已核实） |
|---|---|
| 序号字段 | `Vehicle::r3r_priority`（`vehicle_base.h:381`，1..n 连续，**NOSAVE**）、`Vehicle::r3r_orders_borrowed`（`:382`，NOSAVE） |
| 取段集合 | `R3RGetSegmentHeads`（`train_cmd.cpp:3789`，按 ★ `SegmentFront` 切段） |
| 取归属段 | `R3RGetLowestPriority`（`:3807`）、`R3RGetPriorityHead`（`:3816`） |
| 挂接合并 | `R3RMergePriorities`（`:3832`，passive 段数作 offset 抬高 active 侧 ⇒ **被挂方序号恒小于挂车方**）、`R3RRenumberPriorities`（`:3850`，压缩成连续 1..n） |
| 挂接交接点 | `Couple()`：`5593-5594` 采段集合 → `5668-5670` 合并+重编号+取 `couple_owner` → `5678-5681` 链头 `orders_backup=orders`、`orders=couple_owner->orders`、`r3r_orders_borrowed=true` |
| 解挂/归还点 | `DecoupleTrain`：`4455-4495`（`u_inherited_running` + 两侧各自 `R3RRenumberPriorities`）、`R3RSyncDrivingOrders` |
| 借用列表释放 | `order_cmd.cpp:3485`（借用状态下先还备份再删自己的排程，**绝不 free 借用来的列表**） |
| 读档重建 | `R3RRebuildCouplePriorities`（`train_cmd.cpp:3891`，声明 `train.h:688`），入口 `sl/vehicle_sl.cpp:498` |

换算到用户要求：序号最小者拥有计划 = `R3RGetPriorityHead`；被挂方 < 挂车方 = `R3RMergePriorities` 的 passive offset；每次挂接递增重编号 = 每次 Couple 重编号。**三条均满足**。

本轮只做（不碰判定）：
1. 可见性：车辆窗口 / depot 行显示「所属挂接组 + 命令归属：第 k 段 / 共 N 段」（§6、§9-步骤 6）；
2. 存档健壮性：`r3r_priority`/`r3r_orders_borrowed` 是 NOSAVE，靠 `R3RRebuildCouplePriorities` 读档尽力重建（KI-01 未复测）；**挂接组字段不允许照抄 NOSAVE，必须进存档**（§4.3、KI-48）。

## 3. 现状盘点 B：挂接识别的全部候选入口（接入点清单）

「挂接分组」必须在**每一个候选筛选点**生效，否则会出现「yapf 选它、拼接时又不认」的裂缝。

| # | 位置 | 作用 | 改法 |
|---|---|---|---|
| E1 | `pathfinder/yapf/yapf_destrail.hpp:286-442`（`TrainFitStation` / `R3RIsCarOnlyFormation` / `WAIT_COUPLE` / `CheckOrderLoad·CargoType·NumberOfWagons·Slot`） | yapf couple 目标点判据（**主筛选**） | 追加 `R3RCouplePairAllowed(coupler, target)` |
| E2 | `pathfinder/yapf/yapf_rail.cpp:141-158`（`FindSafeCouplePositionProc` 的 `notWC` 判定） | 回溯路径上的占用 tile 是否算障碍 | 同组 = 可通行目标；不同组 = 当障碍/不存在 |
| E3 | `yapf_rail.cpp:649-655 / 1493-1505`（`stFindNearestCoupleTrain` / `FindNearestCoupleTrain` 入口） | couple 寻路总入口 | 不改逻辑，仅确认 E1/E2 覆盖 |
| E4 | `train_cmd.cpp:5856 GetCouplePosition(v, reverse)` | 几何命中（贴死瞬间取目标车底） | 命中后校验同组；不同组返回 `nullptr` |
| E5 | `train_cmd.cpp:5901 TrainCoupleHandler(v)`（含 `5951+` 邻域扫描兜底、`R3REDGE_COUPLEFAIL` 闸门） | 实际发起挂接 | 同上；不同组走「无目标」分支 |
| E6 | `train_cmd.cpp:9909-9917`（depot 内抵达即挂） | depot 内 GOTO_COUPLE | 同上 |
| E7 | `train_cmd.cpp:10315 / 10328`（主循环） | 每 tick 尝试 | 由 E4/E5 统一覆盖 |

**「不符合组别 = 按照没有等待挂接的列车处理」的落地方式**：不是在 E1 里报错，而是让该候选**从候选集合中消失**（E1 return false / E4 返回 nullptr / E2 当障碍），下游自然走到既有「找不到挂接目标」路径（`COUPLE-FAIL`、原地等待、下 tick 重试、`R3RDbgEdge` 闸门控噪）。**这是关键决定**：不新造第三条分支，复用现状「没车底」语义。

## 4. 数据模型

### 4.1 新 ID 类型（仿 `tracerestrict_id_type.h:16-28`）

```
// src/couple_group_type.h
struct CoupleGroupIDTag : public PoolIDTraits<uint16_t, 0xFFF0, 0xFFFF> {};
using CoupleGroupID = PoolID<CoupleGroupIDTag>;
```

16 位池 ID，全局唯一 ⇒ **跨公司天然可用**（ID 不按公司分段）。哨兵：`0xFFFF` = Invalid，`0xFFF0..0xFFFE` 预留 GUI 用。

### 4.2 分组池项（仿 `tracerestrict.h:39-60` / `:1438` / `:1541`）

```
struct CoupleGroup : CoupleGroupPool::PoolItem<&_couplegroup_pool> {
    std::string name;    ///< 组名（玩家可改）
    Owner owner;         ///< 归属公司 —— 跨公司扩展的锚点（§10）
    uint32_t flags = 0;  ///< 预留：Shared / CrossCompanyAllowed ...
};
using CoupleGroupPool = Pool<CoupleGroup, CoupleGroupID, 64>;
extern CoupleGroupPool _couplegroup_pool;
```

不做成员反向索引缓存（组数少），窗口打开时线性统计段数：链内 `IsSegmentFront()` 且 `couple_group == g` 的车数。

### 4.3 段 ↔ 分组的关联字段

- 新增 `Vehicle::couple_group`（`CoupleGroupID`，默认 `Invalid()`），与 `r3r_priority` 同处（`vehicle_base.h:381` 附近）。
- 语义：只有**段头车（★）**该字段有效；取用时沿链定位本段段首（无 ★ 则链头）。
- 集中两个 helper（**只此一处读写，禁止散点**）：
  - `CoupleGroupID R3RGetCoupleGroup(const Train *chain_or_head)`；
  - `void R3RSetCoupleGroup(Train *seg_head, CoupleGroupID)`。
- **必须进存档**：加到 `sl/vehicle_sl.cpp` 的 Vehicle 描述表；分组池另立 chunk（仿 `sl/tracerestrict_sl.cpp:18-48` 的 `NamedSaveLoad` 表）。步骤 2 需先读 `saveload.h` 的 `SLV_` 与 `sl/extended_ver_sl.cpp` 的 XSLFI 定义确定落法。
- **备选方案**（若步骤 2 发现 ★ 迁移点过多）：字段改为段内**全车复制**（段内同值），匹配时取链头车值 —— 免搬迁但费内存、需定义不一致时的仲裁。默认走「段头车携带 + 迁移点搬迁」（省内存、与 `r3r_priority` 语义一致）。

### 4.4 ★ 段头身份迁移点（搬迁/清理清单，**高风险**）

挂接组跟着「段」走，下列每处都必须同步搬迁或清理，否则归属漂移到错误的车上：

| 迁移点 | 位置 | 动作 |
|---|---|---|
| 挂接合并布 ★ | `Couple()` 提交段（`merged_first->SetSegmentFront()` 附近） | 被挂段保持其组；挂车段保持其组；新链头本身不是段时按其原组处理 |
| 解挂清 ★ | `DecoupleTrain`（`ClearSegmentFront` 附近） | 解出链头保留自己段的组 |
| 换端重排（段内反转、★ 迁移、真引擎链头身份迁移） | `R3RFlipChainBySegments` / `R3RRelocateFrontIdentity` | **组字段必须随身份一起搬** |
| 段生成 / 降级 | `vehicle_cmd.cpp` 的 `CmdMakeSegment` / `CmdDemoteSegment`（`R3RDestroyCarOnlyFormation` 路径） | 新建段：继承链头或置空（待拍板 §11-Q3）；降级：保留在保留的链头或清除（待拍板） |
| 读档 | `sl/vehicle_sl.cpp:498` 附近 | 组 ID 失效（池已删）→ 置 `Invalid()` |

## 5. 命令 / 联机 / 权限（单点化，为跨公司预留）

命令清单（`DEF_CMD_TUPLE`，落在新文件 `src/couple_group_cmd.cpp`，仿路签槽组命令写法）：

| 命令 | 语义 | 备注 |
|---|---|---|
| `CmdCreateCoupleGroup` | 新建组（名可空） | 归属公司 = 执行命令的公司 |
| `CmdRenameCoupleGroup` | 改名 | |
| `CmdDeleteCoupleGroup` | 删除组 | 先把引用它的段全部置 `Invalid()` |
| `CmdSetCoupleGroup` | 把某个段指派到某组 | 参数：段头车 `VehicleID` + `CoupleGroupID`（`Invalid()` = 移出） |
| `CmdSetCoupleGroupFlags`（预留，暂不实现） | Shared / CrossCompanyAllowed | §10 |

- **权限单点**：`static bool CoupleGroupIsManageable(CompanyID cid, const CoupleGroup *g)` —— 今天是 `g->owner == cid`；将来跨公司只改这一个函数（读 `flags` 授权位）。命令内部一律走它，不许在窗口层复制判断。
- 命令注册需在 `command_type.h` 的 `CommandType` 枚举 + `CommandCostAllowedResultTypes`（`:37-39`）登记新 ID Tag（如适用）。
- 联机：命令 + 池由存档系统同步，无需额外包；改池/改车辆组后统一 `InvalidateWindowClassesData(WC_COUPLE_GROUP)`（+ 车辆窗口类）。
- 窗口层不做「越权静默失败」：不可管理就禁用按钮/不显示可编辑控件。

## 6. 可见性（UI）

1. **专用窗口** `CoupleGroupWindow`（新 `WindowClass::WC_COUPLE_GROUP`，仿路签槽组窗口 / 原生 `GroupWindow`）：左列表（组名 + 段数），右列表（成员段：段头车号、段车数、当前归属段 `k/N`）。
2. **车辆窗口**：显示「挂接组：<名称>」「命令归属：第 k 段 / 共 N 段」（新增字符串）。
3. **depot 行**：KI-43 的链状态列（`depot_gui.cpp:395 tag_width` / `:485 DrawChainStateTag` / `:868-895` 列宽）由「散链 / 段：N」扩展为同时体现挂接组与归属段。**列宽必须用最宽串重算**（否则 RTL/截断回归，KI-43 未验证项）。
4. **语言文件**：新字符串**一律追加在 `src/lang/english.txt` 与 `simplified_chinese.txt` 末尾**，不动既有 ID（KI-16）；改完 `touch src/strings.cpp` 再编。

## 7. 与既有硬伤的关联（实现时必须一起看）

| 关联项 | 影响 |
|---|---|
| KI-01（`r3r_priority` NOSAVE，读档 best-effort 重建） | 挂接组**不能**沿用 NOSAVE 做法（KI-48） |
| KI-02（`orders_backup` 单层无栈） | 挂接组不依赖 `orders_backup`；但「多次挂接/解挂」场景下归属与借用仍会叠加出问题 |
| KI-04（非驱动段 order index 冻结） | 只影响排程推进/显示，不影响组匹配 |
| KI-05 / KI-34（`COUPLE-FAIL`） | 「不同组 = 视同没车底」会**增加**这类日志场景 ⇒ 必须落在 `R3REDGE_COUPLEFAIL` 边沿闸门内，否则刷屏 |
| KI-37/38/39（`SegmentFront`/`SegmentBack` 未实测、迁移有瑕疵） | 段识别本身不稳 ⇒ 「段 ↔ 分组」在其之上，必须先确认 ★ 语义（尤其 `R3RFlipChainBySegments` 的 ★ 迁移） |
| KI-43（depot 链状态列未实测） | 本轮在同一列继续加内容，建议先复测 KI-43 |
| `IsOrderListShared()`（`vehicle_base.h:834`） | 挂接组指派**不碰** `orders`，天然规避；将来若用「共享排程」实现归属，必须走 `AddToShared/DeleteVehicleOrders` |

## 8. 不在本轮范围（登记，不实现）

- 段级排程（每段一份 `orders`，Couple 只引用不搬）—— 改动最大，见 `R3R_multi_couple_decouple_orders_memo.md` §10 模型 4。
- 「不成段车厢链拒绝 couple/uncouple」（记忆 17190446，P7）—— 只在连挂侧做了部分 `GetCouplePosition` 判定，解挂侧未做。
- 跨公司挂接的**行为**（§10 只留接口，不实现语义）。

## 9. 分步实施计划与上下文预算

| 步骤 | 内容 | 产物 | 编译代价 | 预算 |
|---|---|---|---|---|
| 1 | 本备忘 + KI 登记 | `R3R_couple_group_design_memo.md` + KI-45~48 | 无 | **本轮已完成** |
| 2 | 数据底座：`couple_group_type.h`、`couple_group.h/.cpp`（池 + 增删改 + 段数统计）、`Vehicle::couple_group`、存档（Vehicle 表 + 池 chunk）、`command_type.h` 登记 | 可编译（功能未接线） | **改 `src/*.h` ⇒ 全量重编 25-40 min（KI-15）** | **✅ 已完成（第 18 轮，见 §12）** |
| 3 | 命令 + 权限单点 + 联机刷新（`couple_group_cmd.cpp`） | 命令可用（尚无 UI） | 仅 .cpp 增量 | **✅ 已完成（第 18 轮，见 §12）** |
| 4 | 专用窗口 + 语言字符串（末尾追加 + `touch src/strings.cpp`） | 可交互指派组 | 仅 .cpp 增量 | **✅ 已完成（第 19 轮，见 §13）** |
| 5 | 匹配硬约束接入 E1/E2/E4/E5/E6，「不同组 = 视同无等待车底」+ 闸门控噪 | 硬约束生效 | 仅 .cpp 增量 | **✅ 已完成（第 19 轮，见 §13）** |
| 6 | 可见性：车辆窗口 + depot 链状态列扩展（KI-43 之上） | 一眼看出组与归属 | 仅 .cpp 增量 | **✅ 已完成（第 19 轮，见 §13）** |
| 7 | 收尾：备忘更新 / KI 状态 / 全量重编 + 游戏内实测 | 实测记录 | 视改动 | **agent 侧已完成（第 27 轮，§14.6）**：「备忘 + KI 状态 + 全量重编 + 源码/构建一致性核对 + 单点接线逐点复核」全部完成（§14.3 / §14.6，KI-65）；**游戏内实测仍待玩家**（§14.4） |

**顺序约束**：步骤 2 必须独占一轮（全量重编最贵，且它落地后才是"字段是否存在"的基线）；步骤 3~6 建议每步做完即编译验证，步骤 7 做一次全量重编收口。

## 10. 跨公司挂接：接口预留（用户追加要求，硬约束）

今天**不实现**跨公司语义，但下列接口点必须现在就按"公司不是一个常数"来写：

1. **池项带 `Owner owner`**（§4.2）：组归属公司是数据，不是隐含约定；**不要**在窗口/命令里假设 `group->owner == _local_company`。
2. **匹配判据单点函数**（唯一允许决定"能不能挂"的地方）：
   ```
   bool R3RCouplePairAllowed(const Train *coupler, const Train *target);
   ```
   今天语义：`coupler` 所属段的组 == `target` 所属段的组（且组有效）。**将来跨公司只改这个函数内部**（读 `flags` 的授权位 / 双方公司关系 / 白名单），E1~E7 七个调用点一行不动。
3. **权限单点函数** `CoupleGroupIsManageable`（§5）：改名/删除/指派的越权判断集中于此。
4. **段归属与公司分离**：段的公司看 `Vehicle::owner`，组的公司看 `CoupleGroup::owner`。**两处必须分开读**，现在就写成两个变量（不要写 `if (group->owner == v->owner)` 这种把两者绑死的判断），否则跨公司时语义无从表达。
5. **UI 预留只读视图**：窗口过滤按 owner，但**过滤逻辑收敛成一个函数** `bool CoupleGroupIsVisibleTo(const CoupleGroup *g, CompanyID cid)`（今天 = 同公司），为将来「查看他公司组（只读）」留口。
6. **`flags` 字段现在就建**（`uint32_t`，`Shared` / `CrossCompanyAllowed` 位），即使本轮无 UI 开关 —— 避免将来改存档结构与车辆字段（后者又要全量重编）。
7. **命令参数的 `CompanyID` 不要隐含**：命令从 `CommandContext` 的 `company` 取公司，**不要**在命令里用 `_local_company`（联机下会错）。
8. 已知**不**属于接口预留范围（属行为设计，将来另立备忘）：跨公司挂接的收入/费用分摊、路权与信号占用、公司间车辆所有权（挂接后车辆 `owner` 归属谁）、他公司段被挂走时的解锁等待。

## 11. 待用户拍板项（开放问题）

- **Q1**：同一个段被指派到 A 组，之后该段与 B 组的段挂接被禁止 —— 那么**新组的自动继承**怎么算？（候选：①挂接成功后合并链的两段**不改变**各自组，匹配仍按各自组；②链头段的组成为整链的"当前组"。本文按 ① 设计，但 depot 显示需要能看出"这条链里有两组"。）
- **Q2**：**默认组 / 空组**语义：`Invalid()`（未指派）的段能不能和 `Invalid()` 的段互挂？（候选：①能，保持现状兼容；②不能，必须显式指派 —— 用户原话「不符合组别，按照没有等待挂接的列车处理」暗示未指派=不匹配，但这会**直接破坏现有存档与既有玩法**，需明确拍板。）
- **Q3**：段由 depot「Make Segment」新建时，挂接组从链头继承还是置空？
- **Q4**：段降级（`CmdDemoteSegment`）后组归属保留还是清除？
- **Q5**：删除分组时，引用它的段是置空（本文默认）还是也一并删除段？

## 12. 实施记录（第 18 轮：步骤 2 + 步骤 3）

### 12.1 新增文件

| 文件 | 内容 |
|---|---|
| `src/couple_group_type.h` | `struct CoupleGroupIDTag : PoolIDTraits<uint16_t, 0xFFF0, 0xFFFF>`、`using CoupleGroupID = PoolID<CoupleGroupIDTag>`、`INVALID_COUPLE_GROUP{0xFFFF}`、`NEW_COUPLE_GROUP{0xFFFE}` |
| `src/couple_group.h` / `.cpp` | 池 `CoupleGroupPool = Pool<CoupleGroup, CoupleGroupID, 64>`（`_couplegroup_pool`）；`struct CoupleGroup : CoupleGroupPool::PoolItem<&_couplegroup_pool>` = `std::string name` + `Owner owner` + `uint32_t flags`（`CGF_SHARED` 预留位）；`MAX_LENGTH_COUPLE_GROUP_NAME_CHARS = 32` |
| `src/couple_group_cmd.h` / `.cpp` | 4 条命令 + 段↔组读写 helper |
| `src/sl/couple_group_sl.cpp` | `CGPP` + `CGVR` 两个 chunk（见 12.3） |

### 12.2 数据底座 API（`couple_group.h`，唯一读写口）

```
bool         R3RIsValidCoupleGroup(CoupleGroupID);                       // 组存在？
bool         R3RCoupleGroupsCompatible(CoupleGroupID a, CoupleGroupID b);// Q2 判据（今天 = a == b）
CoupleGroupID R3RGetCoupleGroupOfSegment(const Train *v);                // 取「段」的组（沿 ★ 定位承载车）
void         R3RSetCoupleGroupOfSegment(Train *v, CoupleGroupID);        // 写「段」的组
bool         R3RCoupleAllowed(const Train *coupler, const Train *target); // §10-2 匹配单点（★ 尚无调用点）
uint         R3RCountSegmentsInCoupleGroup(CoupleGroupID);               // 线性统计，无反向索引
const char  *R3RGetCoupleGroupName(CoupleGroupID);
void         R3RUnassignCoupleGroup(CoupleGroupID);                      // 删除组前清引用
bool         R3RCoupleGroupIsVisibleTo(const CoupleGroup *, Owner);      // §10-5 可见性单点
bool         R3RCoupleGroupIsManageable(const CoupleGroup *, Owner);     // §5/§10-3 权限单点
void         AfterLoadCoupleGroups();                                    // 读档清悬空 ID
```

**承载车规则**：`R3RGetCoupleGroupCarrier()` = 本段段头 ★ 车；本段无 ★ 时（链头段）取链头车。组的载体随「段」走（Q3 方向），段序变化不影响归属。

**Q2 已定**：`INVALID == INVALID` ⇒ 未指派的段之间仍可挂接（**保持现有存档与既有玩法不变**）；「未指派 vs 已指派」不匹配 —— 即用户原话「不符合组别 = 按没有等待挂接的列车处理」只在**至少一方已显式指派**时生效。

**Q5 已定**：删除组 = 先把引用它的段全部置 `INVALID_COUPLE_GROUP`（`R3RUnassignCoupleGroup`），不删段。

### 12.3 存档

- `CGPP`（`CH_TABLE`）：`NamedSaveLoad` 表 `name`(SLE_SSTR) / `owner`(SLE_UINT8) / `flags`(SLE_UINT32)，按池 index 迭代。
- `CGVR`（`CH_SPARSE_TABLE`）：**只存「组有效」的车辆的 `couple_group`**；`SlIterateArray` 的 index = `VehicleID`，加载时 `Vehicle::GetIfValid` 兜底跳过。
- **刻意不改车辆表布局**：`Vehicle::couple_group` 不进 `sl/vehicle_sl.cpp` 的 Vehicle 描述表 ⇒ 上游车辆表布局零变化，旧档天然无此 chunk（= 全隐式组）。
- 加载 `CGVR` 结束后调 `AfterLoadCoupleGroups()` 清悬空引用（KI-48）。

### 12.4 命令（`couple_group_cmd.cpp`，`DEF_CMD_TUPLE_NT`）

| 命令 | 分类 | 参数 | 备注 |
|---|---|---|---|
| `Commands::CreateCoupleGroup` | OtherManagement | `std::string name` | 组名非空且 < 32 字符；同公司内**不允许重名**（`STR_ERROR_NAME_MUST_BE_UNIQUE`）；池可分配才允许 |
| `Commands::RenameCoupleGroup` | OtherManagement | `CoupleGroupID, std::string` | 走权限单点 |
| `Commands::DeleteCoupleGroup` | OtherManagement | `CoupleGroupID` | `R3RUnassignCoupleGroup` → `delete` → `CloseWindowById` |
| `Commands::SetCoupleGroup` | VehicleManagement | `VehicleID, CoupleGroupID` | `INVALID_COUPLE_GROUP` = 移出组；车辆公司用 `CheckOwnership(t->owner)` 判（**不绑组公司**，§10-4） |

所有命令：`flags.Test(DoCommandFlag::Execute)` 才落状态，否则只做校验（Query 模式）；成功后 `InvalidateWindowClassesData(WindowClass::CoupleGroup, 0)`。

### 12.5 登记点（改动到的既有文件）

| 文件 | 改动 |
|---|---|
| `src/vehicle_base.h` | `#include "couple_group_type.h"` + `CoupleGroupID couple_group = INVALID_COUPLE_GROUP;`（在 `r3r_orders_borrowed` 之后） |
| `src/command_type.h` | `Commands` 枚举 4 项 + `CoupleGroupIDTag` 加入 `CommandCostAllowedResultTypes` |
| `src/window_type.h` | `WindowClass::CoupleGroup` |
| `src/sl/saveload.cpp` | `extern` 声明 + `_couple_group_chunk_handlers` 注册 |
| `src/command_table.cpp` | `#include "couple_group_cmd.h"` |
| `src/CMakeLists.txt` / `src/sl/CMakeLists.txt` | 新文件登记 |

### 12.6 尚未做（第 18 轮当时的下一轮清单，除第 3 项外均已在第 19 轮完成）

1. ✅ **E1~E6 接线**（步骤 5）—— 第 19 轮完成，见 §13.2。
2. ✅ `CoupleGroupWindow` + 语言字符串（步骤 4）+ 车辆窗口/depot 列显示（步骤 6）—— 第 19 轮完成，见 §13.1 / §13.3。
3. ❌ `CmdSetCoupleGroupFlags`（§10-6 的接口预留）未实现，`CGF_SHARED` 位已存在但无使用者（保留为下一轮/跨公司改造时再做）。

## 13. 实施记录（第 19 轮：步骤 4 + 步骤 5 + 步骤 6）

### 13.1 步骤 4：专用窗口 + 语言字符串（`src/couple_group_gui.cpp`、`src/widgets/couple_group_widget.h`、语言文件）

| 项 | 落点 |
|---|---|
| 窗口类 | `WindowClass::CoupleGroup`，`_couple_group_desc`（440×210，可缩放，`WindowDefaultFlag::Construction`） |
| 左列表 | `WID_CG_GROUPS`：组名 + `STR_COUPLE_GROUP_LIST_ITEM`（含段数），`group_scroll` |
| 右列表 | `WID_CG_SEGMENTS`：成员段 = 段头车号 + 段车数（`CountSegmentVehicles`，以 ★ 切段）+ **归属标记**，`segment_scroll` |
| 按钮 | `WID_CG_NEW` / `RENAME` / `DELETE` / `REMOVE_SEGMENT` → `CmdCreateCoupleGroup` / `CmdRenameCoupleGroup` / `CmdDeleteCoupleGroup` / `CmdSetCoupleGroup(Invalid)` |
| 可见性过滤 | 组列表只收 `R3RCoupleGroupIsVisibleTo(cg, company)` 的组（§10-5 单点） |
| 语言串 | 全部按 KI-16 追加在 `english.txt` / `simplified_chinese.txt` **末尾**，不动既有 ID；本轮先 `touch src/strings.cpp` 再编 |

### 13.2 步骤 5：硬约束接线（「不同组 = 视同没有等待挂接的列车」）

| # | 文件 / 位置 | 改法 |
|---|---|---|
| E1 | `pathfinder/yapf/yapf_destrail.hpp`（`PfDetectDestination`，`TrainFitStation` 之前） | `if (!R3RCoupleAllowed(Train::From(Yapf().GetVehicle()), t)) return false;` —— 不同组的等待车底**从目标集里消失** |
| E2 | `pathfinder/yapf/yapf_rail.cpp:166`（`FindSafeCouplePositionProc`：`best`/`second_best` 选定后、返回安全位置之前） | 提前 `return false` —— 不同组车底占用的 tile 等同普通障碍，调用方仍报 `CPL-SAFE-FAIL` |
| E4 | `train_cmd.cpp:5878` `GetCouplePosition`（`R3RIsCarOnlyFormation` / `OT_WAIT_COUPLE` 判定之后） | 命中后校验：`if (!R3RCoupleAllowed(v, u)) return nullptr;` |
| E5 | `train_cmd.cpp:5955`（depot 邻域扫描）与 `5988`（邻车扫描）两处候选 | `if (!R3RCoupleAllowed(v, w->First())) continue;` —— 走「此处无等待车底」分支，机车保持 `GOTO_COUPLE` 重试或另寻候选 |
| E6 | depot 内 GOTO_COUPLE 抵达即挂 | 同 E5（共用 `TrainCoupleHandler` 的候选扫描） |
| E3 / E7 | `yapf_rail.cpp` couple 寻路总入口；`train_cmd.cpp` 主循环每 tick 尝试 | **未改动**，确认被 E1/E2/E4/E5 覆盖（原设计即如此） |

**刻意不加探针**：E1/E2 对每次寻路的每个车站/车库 tile 都会执行，拒绝是**常态**而非异常，加探针会变成刷屏源（与 KI-14 的边沿闸门设计相冲）。不同组的下游结果是常规的 `COUPLE-FAIL` / `CPL-SAFE-FAIL` / 「nothing to couple to」，已在既有闸门内。

**接线后的行为变化**：未显式指派任何组时（`INVALID == INVALID`）判据恒为真 ⇒ 老存档、老玩法**零变化**；只有「至少一方已显式指派」时白名单才生效。

### 13.3 步骤 6：可见性

| 位置 | 内容 |
|---|---|
| `src/couple_group.h/.cpp` | 新增 `R3RGetChainScheduleOwner(v, &owner, &index, &total)`：从 `v->First()` 沿链推进，段起于**链头**与每个 ★，`r3r_priority` 最小者为命令归属段，输出 owner / 1-based `index` / `total`。车辆窗口、depot 标签、分组窗口三处共用 |
| 车辆窗口（`vehicle_gui.cpp` `VehicleDetailsWindow`） | 新增两行，走既有 `vehicle_*_line_shown` / `ShouldShow*Line` / `ReInit()` 逐行显示机制：`STR_VEHICLE_INFO_COUPLE_GROUP`（组名，未指派时显示 `STR_DEPOT_CHAIN_NO_GROUP`）与 `STR_VEHICLE_INFO_SCHEDULE_OWNER`（「命令归属：第 k 段 / 共 N 段」）。单段且未指派组的链**一行都不显示**（老存档车辆窗口零变化） |
| depot 行（`depot_gui.cpp` `DrawChainStateTag`） | 在 KI-43 的链状态列上**加第二行**（同为 small 字体）：`STR_DEPOT_CHAIN_GROUP_OWNER` = 组名 + `k/N`；`tag_width` 预留 = 状态行宽 + 「k/N」部分宽，组名按 `tag_name_budget`（= 状态行宽）用 `ShortenCoupleGroupName()` 在**整 UTF-8 字符**处截断并加「..」，因此窗口宽度不随组名增长；列车行 `min_height` 至少容纳两行 small 字体 |
| 分组窗口（`couple_group_gui.cpp`） | 右列表成员段若为命令归属段，追加 `STR_COUPLE_GROUP_OWNER_MARK`（「（命令归属 k/N）」） |

**口径变更（需玩家拍板，已登记 KI-50）**：depot 链状态列的「段：N」原来统计的是 **★ 个数**（= 链头段之外被挂上来的段数），为了让第二行的 `k/N` 与它同口径，本轮改为 `R3RGetChainScheduleOwner` 的 `total` = **链中全部段数（含链头段）**。即：一条机车 + 一段挂上来的车底，原来显示「段：1」，现在显示「段：2」；「散链」的判据不变（`total <= 1`）。

**未验证项**：全部 UI 改动仅编译 + lint 通过，**游戏内未实测**（RTL 布局、超长组名截断、depot 行高是否够两行、非列车车库回归、读档后显示是否重建）→ KI-49 / KI-50。

## 14. 实施记录（第 25~26 轮加固 + 步骤 7 收尾核对）

### 14.1 背景：玩家实测暴露三类越权

| # | 现象 | 违反的核心功能 | 对应 KI |
|---|---|---|---|
| a | 车库内机车直接耦合了被玩家**停住**的、持有 `WAIT_COUPLE` 的车底 | 核心功能 3（硬约束白名单） | KI-60 |
| b | 机车耦合了同组、但**没有等待挂接命令**的车底 | 核心功能 3 | KI-60 |
| c | 车底**段**在车库内被解挂后显示为**散链**，机车再也挂不上它 | 核心功能 2（分组归属随段走）+ 段身份语义 | KI-62 |
| d | 仍把「无 `WAIT_COUPLE` 的纯车厢段」当作寻路目的地（白跑一趟才被闸门拒绝） | 核心功能 3（「视同没有等待列车」应在寻路层就生效） | KI-61 |

共同根因：**被动方（等待被挂的车底）此前完全无人把关** —— 三条被动方候选解析路径（`GetCouplePosition` / depot 瓦片扫描 / 开阔轨道 8 邻域扫描）的判据都是
`R3RIsCarOnlyFormation(u) || u->current_order.IsType(OT_WAIT_COUPLE)`，「纯车厢段」短路**整体豁免了 `WAIT_COUPLE` 要求**，且三条路径都**不检查双方的停止状态**。
玩家给出的期望被固化为闸门四条件：**双方均须启动 + 一方 `GOTO_COUPLE` + 一方 `WAIT_COUPLE` + 分组相容**。

### 14.2 落地：从「多点判据」收敛为「单点谓词 + 单点闸门」

| 层 | 落点 | 内容 |
|---|---|---|
| 寻路（E1~E3） | `src/train.h:694`（声明）/ `src/train_cmd.cpp:1826`（定义）`R3RIsCoupleTarget(t)`；`yapf_destrail.hpp:374` / `:448`、`yapf_rail.cpp:156` | 段判定收敛为一个谓词 `IsSegmentFront() && OT_WAIT_COUPLE`；旧 Target 1 的 car-only 短路删除；back-walk 探针 `fail=notWC` → `fail=notSeg` 且加打 `sf=` |
| 到达（E4~E6） | `src/train_cmd.cpp` `R3RCanCoupleNow(coupler, target, site, why*)`（`~5915`） | 四条件硬闸门；三条解析路径（`site="geo"` `~5978`、`"depot"` `~6047`、`"scan"` `~6077`）全部改走闸门；新增拒绝原因 `target-not-segment`；探针 `CPL-GATE`（边沿触发，tag `R3REDGE_COUPLEGATE`） |
| 段身份（核心功能 2） | `src/train_cmd.cpp` `DecoupleTrain` 末尾（`~4620`） | `u->ClearSegmentFront();` → `if (R3RIsCarOnlyFormation(u)) u->SetSegmentFront();`：解挂后**保留 ★**。★ 是全代码库判定「链是段」的唯一依据（`depot_gui.cpp:542-546`：必须看段首标记，不能看段数） |

**语义合流（§11 判据不变）**：`R3RIsCoupleTarget` 要求 ★ **且** `WAIT_COUPLE`；核心功能 3 的「不同组视同没有等待列车」在寻路层表现为**同样的不命中**，两条规则天然合流，无需额外分支。

**保留 ★ 的副作用核对（全部安全）**：`GetSegmentHeadFromRear`(`:3732`) / `GetSegmentBoundaryFromHead`(`:3759`) / `R3RGetSegmentHeads`(`:3821`) 均自 `GetNextVehicle()` 起遍历 ⇒ 链头 ★ 天然跳过；`R3RFlipChainBySegments`(`:5065`) 段收集带 `w != chain` 排除链头；`CmdDemoteSegment` 仍可正常降级。

### 14.3 步骤 7 收尾：全量重编 + 一致性核对（已完成）

| 核对项 | 结果 |
|---|---|
| 为何全量 | 本轮改到 `src/train.h`、`src/pathfinder/yapf/yapf_destrail.hpp` ⇒ 本机 ninja 跟踪不到头依赖（KI-15） |
| 脚本 | `_tmp_full_build.cmd`：查 `openttd.exe` 进程 → 删 `build\**\*.obj` → `TEMP/TMP` 指 `build\tmp` → `vcvars64` → `ninja -C build -j2 openttd` |
| 结果 | `build\R3R_fullbuild.log` 末行 `[691/691] Linking CXX executable openttd.exe`；`build\R3R_fullbuild.done` = `EXIT_CODE=0` |
| 时间戳 | 最新源码 `train_cmd.cpp` 18:52:33 / `yapf_destrail.hpp` 18:52:27 / `yapf_rail.cpp` 18:51:59 / `train.h` 18:51:56；最早 obj 18:54:54 ⇒ 源码全早于 obj，**无陈旧 / 混合对象** |
| 产物 | `build\openttd.exe` @ 19:23:23，50 621 952 B |
| exe 内容自证 | `findstr` 对 `notSeg` / `target-not-segment` / `CG-PAINT` / `CG-RBG` 均命中 ⇒ **测的那版 = 刚改的那版** |
| 文档同步 | 本 §14 + `R3R_couple_group_memo_20260916.md §6`（细节）+ KI §一 摘要与本轮条目 |

### 14.4 待玩家游戏内实测（步骤 7 剩余部分）

四项，判据以 `build\R3R_debug.log` 为准：

1. **段身份保持（KI-62，最高优先）**：车库内解挂车底段 ⇒ 车库列表该链显示「第 k/N 段」而非「散链」；随后机车应能**无需重新「设为段」**再次挂上它。
2. **散链不可瞄准（KI-61）**：未升过段的纯车厢链旁，机车寻路不得以其为目的地（`FSCP ... fail=notSeg ... sf=0`）；若开到跟前，`CPL-GATE reject=target-not-segment`。
3. **硬闸门三态（KI-60）**：① 被动方被玩家停住 ⇒ 不挂；② 被动方无 `WAIT_COUPLE` ⇒ 不挂；③ 双方已启动 + `GOTO_COUPLE`/`WAIT_COUPLE` 配对 + 分组相容 ⇒ 正常挂。三态均不得刷屏、不得死循环、不得锁死 depot。
4. **分组白名单（KI-45~57 系列）**：不同组车底贴上 ⇒ 视同「没有等待挂接的列车」，原地等待或继续找同组候选；车辆窗口 / depot 链状态列 / 分组窗口的显示与操作不回归。

**关键日志判据速查**（`build\R3R_debug.log`，探针为边沿触发，每状态每 128 帧最多一行）：

| 关心的行为 | 期望日志 |
|---|---|
| 挂接闸门放行 | `COUPLE-OK loco=.. rear=.. consist=..`（`FOLDCHK` 的 `worst_gap` 正常） |
| 被动方被停住 / 无 WAIT_COUPLE / 不是段 | `CPL-GATE reject=target-stopped` / `reject=target-not-wait` / `reject=target-not-segment`，`site=geo\|depot\|scan` |
| 分组不相容 | `CPL-GATE reject=group-mismatch`（行尾 `grp=0` = `R3RCoupleAllowed` 判否），不刷屏 |
| 主动方未在挂接 / 主动方被停住 | `CPL-GATE reject=active-not-goto` / `reject=active-stopped` |
| 寻路尽头拒绝散链 | `FSCP ... fail=notSeg ... sf=0` |
| 解挂后车底仍是段 | `DECOUPLE-DONE u=.. co=..`（`co` = `R3RIsCarOnlyFormation`）＋车库列表该链续显「第 k/N 段」、★ 未丢 |
| 分组窗口刷新 | `CG-RBG owner=.. company=.. iterated=.. visible=..`、`CG-PAINT ... sel=.. sel_cg=.. can_manage=..` |
| 窗口没建出来 / 被前置 | 点「挂接分组」后：只有 `CG-SHOW` = 窗口已存在被前置；`CG-SHOW` + `CG-WIN` = 正常新建；**两行皆无 = 按钮/点击链路故障** |

**实测记录（第 28 轮填，四行对应上面四项）**：

| # | 项目 | 结果 | 关键日志 / 现场 | 备注 |
|---|---|---|---|---|
| 1 | 段身份保持（KI-62） | **功能侧通过**（车库列表显示待玩家目视） | 第一次解挂 `DECOUPLE-FIRE consist=0 tx=39 ty=33 real=1` → `DECOUPLE-DONE u=3 co=1 real=3`（解出方 ★ 保留、索引落在 `WAIT_COUPLE`）；机车回 37,28 执行自己的 GOTO_COUPLE 后 **无需重新「设为段」** 即挂回同一条链：`COUPLE-FAIL` → 三候选（`A2-FLIP-U` 回滚 → `A3-FLIP-V-ONLY` 被 `SPLICE-GAP-REJECT` 拦下 → `A3-BOTH`）→ `FOLDCHK worst_gap=1` → `COUPLE-OK loco=2 rear=3 consist=3 co=1 real=4 tx=39 ty=31`；本轮 `R3R_perf.log` 末行 `chains=2 vehs=9 maxChain=6 segs=2` ⇒ 解出链 `[8..3]` 仍带 ★（是段，非散链） | ★ 的诉求（解挂后仍是段）成立；「车库列表显示第 k/N 段而非散链」需玩家目视确认（本轮无截图） |
| 2 | 散链不可瞄准（KI-61） | **未触发，待复测** | 全日志 **0** 条 `FSCP`、**0** 条 `reject=target-not-segment` —— 本次会话没出现「未升段的纯车厢链」场景 | 下轮动作：放一条**未升段**的车厢链，让机车带着 GOTO_COUPLE 靠近它，看 `FSCP ... fail=notSeg ... sf=0` / `CPL-GATE reject=target-not-segment` |
| 3 | 硬闸门三态（KI-60） | **②③ 通过；① 未触发** | ② 无 `WAIT_COUPLE` 不挂：`[R3R] CPL-GATE reject=target-not-wait site=depot act=0 tgt=3 aOrd=16 tOrd=0 aStop=0 tStop=0 grp=1` + `COUPLE-FAIL loco=0 order=16`（**只 1 行、不刷屏**），随后目标拿到 `curType=17` 立即放行 → `COUPLE-OK loco=0 rear=8 consist=3 co=1 real=1`；③ 车库内与站台各一次成功耦合（上表 + `COUPLE-OK loco=2 ... tx=39 ty=31`），全程无死循环、无 depot 锁死（`CRT found=1` 正常出库）；① 被动方被玩家停住：本次 **0** 条 `reject=target-stopped`，未构造该场景 | 需补「把车底停住再让机车来挂」一次 |
| 4 | 分组白名单 + 可见性不回归（KI-45~57） | **判定在位；拒绝分支与窗口显示未触发** | `CPL-GATE ... grp=1` ⇒ `R3RCoupleAllowed` 同组相容（本会话双方同组，故未走到拒绝分支）；未出现 `CG-SHOW` / `CG-WIN` / `CG-RBG` / `CG-PAINT`（玩家未打开分组窗口） | 下轮动作：不同组贴上（看 `reject=group-mismatch`）+ 点开「挂接分组」与车辆窗口、车库列表核对显示 |

### 14.5 已知遗留（本轮未做，已登记）

- **解挂侧不拒绝散链（KI-08）**：`GetSegmentHeadFromRear` 返回空时回退原生 `GetDecoupleVehicleAuto` 逐车启发式 ⇒ 散链仍可能被 `DECOUPLE` 拆开（日志 `DECOUPLE-FIRE` 走此路径）。本轮只做连挂侧（E1~E6）；「解挂侧严禁散链」留待后续。
- **`R3RCouplePairAllowed` 命名退役**：§10-2 承诺的「匹配判定单点」现名 `R3RCoupleAllowed`（`couple_group.cpp:162`，内部 `R3RCoupleGroupMasksCompatible` 交集判据），旧名全代码库 0 命中；日后跨公司 / 跨组策略只改这一个内部。
- **`CmdSetCoupleGroupFlags` 未实现**（§12 末，`CGF_SHARED` 位空置）—— 保留给跨公司改造。
- **文档沿革校正**：KI-59 记载的 `CG-PICK` 探针在当前源码中已不存在（`OnVehicleSelect` 直接干活、无探针），属命名沿革，非回归。

## 14.6 第 27 轮：步骤 7 的 agent 侧收尾核对（静态，已完成）

步骤 7 拆成「agent 可执行部分」与「玩家游戏内实测（§14.4）」两块。本轮把前者做完，结论如下（同时登记为 KI-65）：

| 核对项 | 方法 | 结果 |
|---|---|---|
| 工作树规模 | `git -C "d:\sourcecode of JGRPP" status --porcelain -- src` | **20 改 + 9 新**，与 KI §一 摘要逐项吻合（新增 7 个 `couple_group*` + `sl/couple_group_sl.cpp` + `widgets/couple_group_widget.h`） |
| 建置一致性 | 源码 mtime vs `build\openttd.exe` mtime | 最新源码 `train_cmd.cpp` **18:52:33** < exe **19:23:23** ⇒ **无陈旧 / 混合对象**，「测的那版 = 刚改的那版」成立（KI-15 合规） |
| 新文件静态检查 | lint（`couple_group.cpp` / `couple_group_cmd.cpp` / `couple_group_gui.cpp` / `sl/couple_group_sl.cpp`） | **0 诊断** |
| 寻路侧单点谓词 | 全库搜索 `R3RIsCoupleTarget` | 声明 `train.h:694`、定义 `train_cmd.cpp:1826`，四处共用：`yapf_rail.cpp:156`(E2)、`yapf_destrail.hpp:374`(E1 站台预留扫描) / `:448`(E1 `PfDetectDestination`) |
| 到达侧单点闸门 | 全库搜索 `R3RCanCoupleNow` / `CPL-GATE` | 定义 `train_cmd.cpp:5915`，三站点 `"geo"`:5978 / `"depot"`:6047 / `"scan"`:6077；拒绝原因 `target-not-segment`(`:5924`)；边沿枚举 `R3REDGE_COUPLEGATE`(`:4043`)、打印 `:5946` |
| 分组白名单单点 | 全库搜索 `R3RCoupleAllowed` | `couple_group.cpp:162`（内部 `R3RCoupleGroupMasksCompatible` 交集判据 `:115`），旧名 `R3RCouplePairAllowed` 0 命中 |
| 窗口 owner 修复（KI-55/58） | 读 `couple_group_gui.cpp` | `this->company` 成员 + `OnInit()` 写 `this->owner`（`:159/180`）、可见性用 `company`（`:200/208`）、可管理性用 `company`（`:267`）、建组按钮 `can_create` 双 `IsValidID` 兜底（`:365`） |

**结论**：设计备忘 §11 的步骤 1~6 全部 ✅；步骤 7 的「备忘更新 / KI 状态 / 全量重编 / 源码—构建一致性核对」全部完成，**只剩 §14.4 的玩家游戏内实测**（需要人在游戏里点按钮、看窗口与日志，agent 无法代做）。

**附带定性（KI-47 → 已修 + KI-64）**：「分组随段走」不必在段头身份迁移点显式搬迁——掩码靠**读时整段并集 + 写时归位**实现，只要迁移不改变「段的车集」即安全（`R3RFlipChainBySegments` 段内倒序、`MakeSegment` 只切小段，均满足）。唯一残留边界：掩码停在段内非 carrier 的车若被**单独售出**，其组归属随之消失；彻底消除需导出归位函数并在切段点调用（要改 `src/*.h` → 全量重编），按「低收益」登记为 KI-64，未做。

## 15. 第 28 轮：玩家实测会话 1 的日志核对（§14.4 首轮填表）

### 15.1 会话证据

| 项 | 值 |
|---|---|
| 被测 exe | `build\openttd.exe` @ **2026-09-16 19:23:23**（含 §14 全部改动；`findstr notSeg/target-not-segment` 命中） |
| 日志 | `build\R3R_debug.log` **320 行**（止于 19:49:50）、`build\R3R_perf.log`（止于 19:52:17） |
| 载入 | `LOADCENSUS-LEAVE total=9 bad=0` / `PHASE2END bad=0`（9 节、无坏链） |
| 结束态 | `chains=2 vehs=9 maxChain=6 segs=2 artic=0 waitCouple=1` |

### 15.2 场景时间线（全部一次跑通，无死循环、无刷屏、无 depot 锁死）

| # | 行号 | 事件 | 判据 |
|---|---|---|---|
| 1 | 5~34 | 车库内两次「设为段」（veh 3 / veh 0）：`MAKESEG-CMD … exec=1` → `UPGRADE-BEFORE/AFTER-SPLIT`：★(`SF=1`) 落在新段头、两端假引擎（`subtype=0x08/0x09`，`eng=1`）与 artic 角色（`AH/AM`）同步重排 | 段升级链路正常 |
| 2 | 35~58 | 车库内挂接：先 `CPL-GATE reject=target-not-wait` + `COUPLE-FAIL`（目标 `tOrd=0`），目标转为 `curType=17` 后 `ARRANGE-IN` → `FOLDCHK worst_gap=4` → `COUPLE-OK loco=0 rear=8 consist=3 co=1 real=1`（合并 9 节，★ 在 0/3/6） | 闸门② + 放行 |
| 3 | 76~87 | 出库至 39,33 后按车组计划解挂：`DECOUPLE-FIRE consist=0 tx=39 ty=33 real=1` → `DECOUPLE-DONE u=3 co=1 real=3` | 解挂点 = 真实命令终点 + `next=DECOUPLE` ✓；解出方 ★ 保留、索引钉 `WAIT_COUPLE` ✓ |
| 4 | 101~192 | 机车恢复自有计划（`L-ORD 0 type=16`），跳过已完成的 `GOTO_COUPLE`（`ADVANCE: veh=0 order=3 real_before=0`），反向出站 → 37,28 执行 `GOTO_COUPLE` → `CPL-RESERVE … reserved=1` → `COUPLE-FAIL`（原地待命，不刷屏） | 归属段机制：机车只跑自己的计划 |
| 5 | 201~266 | 第二次挂接：`A2-FLIP-U`（回滚）→ `A3-FLIP-V-ONLY` 被 `SPLICE-GAP-REJECT prev=0 … dist=19` 拦下（相邻段残隙，拒绝并入）→ `A3-BOTH` → `FOLDCHK worst_gap=1` → `COUPLE-OK loco=2 rear=3 consist=3 co=1 real=4 tx=39 ty=31` | 折叠修正三候选 + 残隙拒绝全部按设计工作 |
| 6 | 276~295 | 合并链按**车组**计划继续（`SKIP-STOPPED` 跳过 42,28、`real=5`/`6`）驶入库内 38,27 | 被挂方（序号最小段）出计划 ✓ |
| 7 | 296~320 | 最后一次解挂：`DECOUPLE-FIRE consist=2 tx=38 ty=27 real=6`（index 6 = `DECOUPLE`）→ `DECOUPLE-DONE u=8 co=1 real=0`；机车 `LOCO-AFTER-DECOUPLE veh=2 … real=1` + `L-ORD`（自有计划）→ 回 38,35；车底 `U-ORD 0 type=17` 原地等挂 | 两侧各自归位 ✓ |

### 15.3 本轮发现的三处**文档/探针口径**校正（非代码缺陷）

1. `CPL-GATE` 的拒绝串在源码里是 `active-not-goto` / `target-not-segment` / `target-not-wait` / `active-stopped` / `target-stopped` / `group-mismatch`（`train_cmd.cpp:5922-5932`，逐条为 `reason = "…";`）。§14.4 速查表原写的 `reject=stopped` / `reject=no-wait-couple` / `reject=group` 三个名字**不存在**，已按源码改正。
2. `CPL-GATE` 行尾 `grp=` 是 **`R3RCoupleAllowed()` 的布尔返回值**（1=相容），**不是**分组掩码本身；分组不相容时应期待 `grp=0` 且 `reject=group-mismatch`。
3. `ADVANCE: …` 是 `src/vehicle_base.h` `IncrementRealOrderIndex()` 里标注 `DEBUG (R3R — remove)` 的临时调试打印，触发条件为「当前索引或 `current_order` 是 `GOTO_COUPLE` 却被推进」。本轮两处（行 86 / 301）都紧跟在解挂后「恢复自有计划并跳过已完成的 `GOTO_COUPLE`」这一步，属**预期行为**（记忆 18491399 的 3958 行语义），不是索引越权。移除该探针要改 `src/*.h` ⇒ 触发全量重编（KI-15），登记为 KI-66，待与其它头文件改动合并进行。

### 15.4 结论与下一步

- 步骤 1~6 ✅、步骤 7 的 agent 侧 ✅；§14.4 四项已填（1 功能侧通过 / 2 未触发 / 3 ②③ 通过、① 未触发 / 4 判定在位、拒绝分支与窗口显示未触发）。
- 下轮请玩家补三个动作即可收口：**(a)** 放一条**未升段**的纯车厢链，让机车带 `GOTO_COUPLE` 靠近它（验 KI-61）；**(b)** 把待挂车底**用停机按钮停住**再让机车来挂（验 KI-60 条件① 的 `reject=target-stopped`）；**(c)** 把两条链指到**不同**挂接组后贴上，并点开「挂接分组」窗口 / 车辆窗口 / 车库列表核对显示（验 KI-45~57 的拒绝分支与可见性三项）。


