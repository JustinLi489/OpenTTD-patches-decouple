# R3R 跨公司挂接：决策备忘（第 33 轮建立 / 第 34 轮决策全部回收）

> 本文件是「跨公司挂接」的**唯一决策台账**。第 33 轮我把待拍板点只发在对话里、没有落盘，
> 工作记忆被截断后清单丢失过一次。**此后一切跨公司结论先写这里，再动代码。**

## 0. 脉络

- 起因：玩家问「运费结算是不是也按段？是的话就可以研究跨公司」。
- 第 32 轮已落地**按段结算**（同公司内，见 KI-71）：运费记到各段段头车的 `profit_this_year`，
  总量守恒、**公司总账零改动**。
- 第 33 轮盘点跨公司所需零件，结论登记为 **KI-74**。
- 第 34 轮：**D1~D6 全部回收完毕**（§2 / §3），实施计划见 §7。**代码尚未改动。**

## 1. 源码依据（改动前必读）

| 事实 | 落点 |
|---|---|
| **钱按"链头公司"结** | `~CargoPayment()`（`economy.cpp:1473-1490`）：`AutoRestoreBackup cur_company(_current_company, this->front->owner)` ⇒ `route_profit` 从链头公司扣、`profit_this_year` 记在链头车上。**KI-71 的按段入账没有跨公司搬钱** |
| 跨公司账本已存在 | `CargoPacket::RegisterDeferredCargoPayment(CompanyID, VehicleType, Money)`（`cargopacket.cpp:259-263`）+ `PayDeferredPayments()`（`:265-281`，**最终交付**时 `SubtractMoneyFromCompany(cid, CommandCost(exp, -payment))` 逐条结清，调用点 `economy.cpp:1524`）；`CargoPacket::Reduce()`（`:247-257`）按剩余量**等比例**削减未结额 ⇒ **一票货天然支持"多公司各有应收"** |
| 一票货分段分成已有 | `feeder_share`/`GetFeederShare(count)`（`cargopacket.h:193`）+ `_settings_game.economy.feeder_payment_share`（`economy.cpp:1548`，全局百分比） |
| 链路图**不认公司** | `src/linkgraph/` 全目录搜 `company` 只命中 GUI 图例；`IncreaseStats(...)` 无 owner 参数 ⇒ 多家运力混加在同一条边、货的分配是全局的 |
| 跨公司**物理前提** | 两处 `w->owner != v->owner` 扫描闸门挡住跨公司挂接：`train_cmd.cpp:6147`（depot 路径）、`:6184`（扫街兜底）；白名单单点 `R3RCoupleAllowed`（`couple_group.cpp:162`，声明 `couple_group.h:109`）**只看分组掩码、不看 owner**；几何路径 `GetCouplePosition` 亦无 owner 过滤（`R3RCanCoupleNow`，`train_cmd.cpp:6024`）⇒ 放开跨公司**不必动数据模型** |
| **授权位现状（第 34 轮核对）** | `CoupleGroup::flags`（`couple_group.h:51`）与 `enum CoupleGroupFlag { CGF_SHARED = 1 << 0 }`（`:45-47`）**已存在但全代码库零使用点、无命令、无 UI**；**`CGF_CROSS_COMPANY` 位尚不存在，需新增**（改 `couple_group.h` ⇒ 触发全量重编，记忆 66636022） |

## 2. 玩家已拍板（全部回收，原文保留）

| 点 | 玩家原话 | 结论 |
|---|---|---|
| **D1** | 「D1 用甲吧」 | **甲**：多公司**共同承运同一票货**，按"谁跑了哪一段"分钱（复用 deferred payment 账本）。放弃乙（各公司独立链路图、只在换装点交接） |
| **D2** | 「D2 走 2 吧」＋第 34 轮核对「对，就是这个意思」 | 链路图**共用一张图，但给边加公司维度：分别记录各公司的运力与运量、各公司各算各的账**（1=保持现状混加，3=每家一张完全独立的图） |
| **D3** | 「各段各自保留所有权，控制权你应该清楚吧」 | **所有权不分家**：挂接不改 `Vehicle::owner`，各段的车永远属于原公司（资产/折旧/卖车归属不变）。**控制权归控制者** = 链头、`r3r_priority` 最小的段、出计划的那一段 |
| **D4** | 第 34 轮选 **①** | **连挂期间他公司段「只读冻结」**：外公司不能出售/改造该段、不能单独改它的配置；**原公司随时可以解挂把自己的车收回**；期间的运行费/成本仍记在**控制者（链头）**那边，与分账口径一致 |
| **D5** | 「在信号里，这个跨公司挂接的列车属于控制者的列车」 | **路权/信号/闭塞按控制者算**：物理上就是一列车，登记身份按链头（控制者）那一方。⇒ **不需要改代码**，只需写进规则与 UI 文案 |
| **D6-①** | 第 34 轮选 **①** | **必须同组 + 该组开启 `CGF_CROSS_COMPANY` 位**才允许跨公司挂接；不开位的组跨公司挂不上（视同「没有等待挂接的列车」：原地等待、可继续找别的候选、不刷屏） |
| **D6-②** | 第 34 轮选 **①** | **交付即分账**：货到终点当场按各段归属把钱分给各公司，复用现有 deferred payment 账本，**不新增界面** |
| **D6-③** | 第 39 轮「那就按A来吧」 | **落地 `CGF_SHARED`（组共享）**，补上 D6-① 缺失的「两家公司如何进入同一个组」这一环：组的**车主**可开「允许他公司加入」，开后**他公司可把自己的段指派进本组**、并能在分组列表里看到本组；关掉时**把他公司的段移出本组**（收回授权，不改变所有权、不解开已挂好的列车）。**绝不放开车主校验**——任何公司永远只能动自己的车（D3）。另：共享只决定「能不能进组」，「允许跨公司挂接」仍是挂接闸门，两者要同时开才挂得上 |
| **D6-④** | 第 43 轮「那两个按钮是可以合并的对吧，那么就先合并再实测吧」 | **两个开关合并为一个**：`CGF_SHARED` 与 `CGF_CROSS_COMPANY` 在玩家侧只是一个复选框（`CoupleGroup::CGF_ALLOW_OTHERS` = 两位之和，**两位保留、恒同置同清**，读判据统一走 `AllowsOthers()`）。依据 = KI-91 的核实结论：四种组合里只有 `{共享=1, 跨公司=1}` 放行，`{1,0}` 是「能进组但永远挂不上」的死状态、`{0,1}` 不可达 ⇒ 合并后**没有玩家可感知的状态被砍掉**。代价 = `D6-③` 的「与关系」描述作废（不再是两个开关的与，而是**一个开关做两件事**）。**归档兼容**：存档字段不变，旧档**任一位为 1 即视为已开放**；**关闭语义仍复合**（清两位 + 驱逐他公司段，不改变所有权、不解开已挂好的列车） |

## 3. 大白话问答留档（已回收，供日后向玩家复述）

**D1**——一票货从 A 到 D，路上由两家公司的车接力。甲 = 只有一张运单，钱按"每家实际跑了哪几段"分；
乙 = 每家一套自己的货流账，到换装点像两家公司各开一张运单交接。**选甲**。

**D4**——A 的车拖着 B 的一段车厢跑。**选：把那段车对外公司"上锁"**（B 之外的人不能卖、不能单独改配置），
但 B 自己随时能把它摘下来收回；这段时间烧的油钱按 D1 的口径记在控制者头上。

**D6**——现在两家公司的车**根本挂不上**（代码里硬拦）。
**选：分组上加一个"允许跨公司"开关**，只有同一个挂接分组、且该组开了这个开关，才挂得上；
**选：钱在**货到终点那一刻**按各段归属当场分账给各公司，不做账期、不做公司间明细界面。

**D6-③（第 39 轮补问）**——玩家问「如何把别人公司的段放入我们公司的挂接分组」。核对后发现**根本没有任何路径**：
①`CmdSetCoupleGroup` 开头 `CheckOwnership` 只让自己的车、还要求组也是自己的；②分组界面按「只有车主看得见」过滤，别家公司在列表里根本看不到我们的组；③掩码唯一写入点只服务①②，所以两家段的分组掩码交集**恒为空**，`CGF_CROSS_COMPANY` 那道门**永远执行不到**（台账空白，已登记 KI-84）。
**选择 A（不选"放开车主校验"）**：给组加一个**车主可控的「允许他公司加入」**开关，即把代码里预留却零使用的 `CGF_SHARED` 落地。
正解形态是「**A 开共享 → B 自愿把自己的段挂进来**」，而不是「A 单方面把 B 的段拉进自己的组」——后者等于替 B 做授权决定，违背 D3「挂接不转移车主」。

## 4. 改动清单（落地时照此开工）

1. **授权位（新增）**：`couple_group.h` 的 `CoupleGroupFlag` 加 `CGF_CROSS_COMPANY = 1 << 1`。
2. **授权判据（单点）**：`R3RCoupleAllowed`（`couple_group.cpp:162`）内部——`coupler`/`target` 分属不同公司时，
   在"分组掩码相容"之外**额外要求交集里的组至少有一个开了 `CGF_CROSS_COMPANY`**；
   上层 5 个调用点（E1 `yapf_destrail.hpp:437`、E2 `yapf_rail.cpp:170`、E4/E5/E6 `train_cmd.cpp:5978/6047/6077`）**一行不动**。
3. **拿掉两处 owner 硬拦**：`train_cmd.cpp:6147`、`:6184` 的 `w->owner != v->owner` 改为走上面的白名单单点。
4. **分账落地（D1=甲）**：`~CargoPayment` 结算时按各段 `owner` 分组——**同公司**仍按 KI-71 记段头 `profit_this_year`；
   **跨公司**的份额用 `RegisterDeferredCargoPayment(段头公司, ...)` 挂到货物包上，交付时自动结清。
   记账基准仍用 `visual_profit + visual_transfer` 差值（KI-71 的修正，勿回退到 `route_profit`）。
5. **D4 只读冻结**：他公司段的出售/改造/单独 refit 入口加拦截（`CmdSellVehicle` / 车库改造 / depot 拖出单节）。
6. **UI**：①挂接分组窗口加「允许跨公司」勾选框（新增 `CmdSetCoupleGroupFlags` + lang 串，**注意**：改 lang txt 后必须强制重编 `strings.cpp`，记忆 52814982）；
   ②车辆窗口显示「所属公司 / 控制者公司」；③车库链状态列显示归属段所属公司。
7. **存档**：`flags` 已在池 chunk 内（`sl/couple_group_sl.cpp`），新位无需动存档格式。
8. **D2 链路图公司维度**：边/记录加公司维度、各公司分别记账（独立大项，见 §7-P3）。
9. **D6-③ 组共享（新增，第 39 轮）**：`CGF_SHARED` 由「预留」转**生效**；新增判据 `R3RCoupleGroupIsJoinableBy(group, company)`（= 车主本人 **或** 组已共享）供 `CmdSetCoupleGroup` 使用（`CheckOwnership` **保留**）；`R3RCoupleGroupIsVisibleTo` 对共享组放开；新增命令 `CmdSetCoupleGroupShared`（**仍 owner-only**）与 `R3RSetCoupleGroupShared`（关共享时驱逐他公司段）；GUI 新增「允许他公司加入」勾选框、列表标记「（已共享）」、加/移段按钮改用「可加入」判据而改名/删除/两个开关仍 owner-only。**`flags` 已在池 chunk 内 ⇒ 不改存档格式**。

## 5. 未做的相关项（登记）

- 链路图公司维度（D2 方案 2）**未实现**：多家运力仍混加在同一条边。
- 公司间收发明细 UI、契约/账期**未实现**（玩家已明确不做 → 不做）。
- 连挂期间他公司段的只读冻结**未实现**（D4 已定，待 P2 实现）。

## 6. 第 34 轮：拍板回收状态

| 点 | 状态 | 落地点 |
|---|---|---|
| D1 = 甲 | **已回收** | §4-4 |
| D2 = 「走 2」（已二次核对通过） | **已回收并固化** | §4-8 |
| D3 = 所有权各段保留 / 控制权归控制者 | **已回收** | §4-2 / §4-6 |
| D4 = ① 只读冻结 + 原公司可随时解挂收回 | **已回收** | §4-5 |
| D5 = 信号 / 闭塞按控制者算 | **已回收（无需改代码）** | §4-6 文案 |
| D6-① = 同组 + `CGF_CROSS_COMPANY` | **已回收** | §4-1 / §4-2 |
| D6-② = 交付即分账 | **已回收** | §4-4 |
| D6-③ = 组共享 `CGF_SHARED`（方案 A） | **已回收** | §4-9 / §11 |

⇒ **决策层面已无阻塞**，KI-75 可关闭；实施跟踪转入 **KI-76**（§7 的 P1~P5）。

## 7. 分步实施计划（每步独立可编译、可验证、可回退）

| 步 | 内容 | 改动面 | 是否触发全量重编 | 验证点 |
|---|---|---|---|---|
| **P1** | 授权位 + 分组窗口「允许跨公司」开关 + 放开两处 owner 硬拦（**能挂上，但钱仍全归控制者**，需明确告知玩家） | `couple_group.h` / `couple_group.cpp` / `couple_group_gui.cpp` / `couple_group_cmd`（如需新命令）/ `command_type.h` / `train_cmd.cpp` / 双语 lang | **是**（改 3 个头文件 + lang） | 开位后两公司车能挂上、关位挂不上；同公司行为零变化 |
| **P2** | D4 只读冻结（他公司段禁售/禁改/禁单独 refit；原公司可随时解挂收回） | `vehicle_cmd.cpp` / `vehicle_gui.cpp` / `depot_gui.cpp` | 否（纯 .cpp 可增量） | 跨公司连挂期间对他公司段执行售出/改造应被拒；解挂收回正常 |
| **P3** | D1=甲 分账：`~CargoPayment` 按 `owner` 分账，跨公司走 `RegisterDeferredCargoPayment` | `economy.cpp` / `economy_base.h` | 是（改 `economy_base.h`） | 一票货由两家跑完，两家账面各有进项；公司总账守恒；同公司链数值与 KI-71 逐位一致 |
| **P4** | UI：车辆窗口「所属公司 / 控制者公司」、车库链状态列公司列 | `vehicle_gui.cpp` / `depot_gui.cpp` / lang | 是（lang） | 目视 |
| **P5** | D2 链路图公司维度（边记录各公司运力/运量、分别记账） | `linkgraph/*`（大项，需另立设计） | 是 | 两家公司分别看到自己的运力与货流 |
| **P6** | **D6-③ 组共享**：`CGF_SHARED` 落地（可加入判据 + 可见性放开 + 共享开关命令 + GUI + 双语文案）。**这是让 P1 的跨公司闸门真正可达的前提**，不做则 D6-① 永远走不到 | `couple_group.h` / `couple_group.cpp` / `couple_group_cmd.h` / `couple_group_cmd.cpp` / `command_type.h` / `couple_group_gui.cpp` / `widgets/couple_group_widget.h` / 双语 lang | **是**（改 2 个头文件 + lang） | 甲开共享后乙能在分组列表看到该组、把自己的段指派进去；随后两车能挂上（`CG-CROSS-ALLOW`）；关共享后乙的段被移出、再指派被拒 |

**全量重编成本**：本机实测 25~40 分钟（667+ 步，记忆 66636022 / 76468255），且必须 `-j2`（8GB 内存）。
⇒ **P1 的头文件改动建议与其它等待中的头文件改动合并做一次**，例如 KI-66（删除 `vehicle_base.h` 里的临时调试打印）——**第 37 轮已照此办理**（P3 的 `economy_base.h` 改动与 KI-66 合并为同一次全量重编，见 §10.8）。

**进度（截至第 37 轮）**：**P1 已落地（第 35 轮，见 §8）→ P2 已落地（第 36 轮，见 §9）→ P3 已落地（第 37 轮，见 §10）**，三者同处一版已编译产物（`build\openttd.exe` @ 2026-09-17 16:23，`EXIT_CODE=0`，`stale_obj_count=0`）；**P4 / P5 未做**。剩余待办 = §8.3（P1 复测 1~4）、§9.4（P2 复测 1~4）、§10.5（P3 复测 1~5）的**玩家游戏内实测**。

---

## 8. 第 35 轮：P1 实施记录（授权放开）

玩家一句话「两处都按推荐来吧」= D4 选 ①（只读冻结 + 原公司可随时解挂收回）、D6 选 ①（同组 + `CGF_CROSS_COMPANY` 位才允许跨公司；交付即分账）。P1 只落 **D6 的程序侧一半**，且**只做到「能挂上」**——钱仍全部记在控制者，分账是 P3。

### 8.1 落地清单（全部已编译通过）

| # | 文件 | 内容 |
|---|---|---|
| 1 | `src/couple_group.h` | 新增 `CGF_CROSS_COMPANY = 1 << 1`（原 `CGF_SHARED` 保留为 reserved）；新增 3 个 API 声明：`R3RCoupleGroupMasksAllowCrossCompany(a, b)`、`R3RGetCoupleGroupFlags(group)`、`R3RSetCoupleGroupCrossCompany(group, allowed)`；`R3RCoupleAllowed` 的文档改写为「唯一闸门 + 也管公司边界（D3：挂接不改车主）」 |
| 2 | `src/couple_group.cpp` | 实现上述 3 个 API；`R3RCoupleAllowed` 改为**两道门**：①分组掩码兼容（原逻辑，逐位不变）；②`coupler->owner != target->owner` 时，必须**共享组**里至少有一个带 `CGF_CROSS_COMPANY` 的组。两个未分组段构成的隐式组**恒不放行跨公司**（没分组 = 没地方勾开关），故「没分组就只能同公司挂」，与 D6-① 口径一致 |
| 3 | `src/command_type.h` | `Commands` 枚举新增 `SetCoupleGroupFlags`（紧跟 `SetCoupleGroup`） |
| 4 | `src/couple_group_cmd.h/.cpp` | 新增 `CmdSetCoupleGroupFlags(flags, group, cross_company)`：`GetIfValid` + `R3RCoupleGroupIsManageable` 双重校验，`Execute` 期写位并 `InvalidateWindowClassesData(WindowClass::CoupleGroup, 0)`；`DEF_CMD_TUPLE_NT(..., CmdDataT<CoupleGroupID, bool>)`。**未改 `command_table.cpp`**（该文件无 couple group 条目） |
| 5 | `src/couple_group_gui.cpp` + `src/widgets/couple_group_widget.h` | 分组窗口底部新增一行「允许跨公司挂接」：`WID_CG_CROSS_COMPANY_TEXT`（`WWT_TEXT` 标签，带 tooltip）+ `WID_CG_CROSS_COMPANY`（**`WWT_BOOLBTN`**，本版本的复选框控件，不是 `WWT_CHECKBOX`——后者在 0.73 源码里不存在）。`OnPaint` 用选中组的 `flags & CGF_CROSS_COMPANY` 驱动 `SetWidgetLoweredState`、按 `can_manage` 置灰、并把它并入 `CG-PAINT` 探针报文的 **bit30**；`OnClick` 先查 `IsSelectedGroupManageable()` 再发命令 |
| 6 | `src/train_cmd.cpp` | **删除** depot 扫描路径与扫街兜底路径里的 `if (w->owner != v->owner) continue;`（原 `:6147` / `:6184` 附近），改为注释说明「候选一律进闸门，被拒 = 此处没有等待挂接的列车」。几何路径（`GetCouplePosition`/`R3RCanCoupleNow`）本来就无 owner 过滤，故三路现在**统一由 `R3RCoupleAllowed` 裁决**。**保留** `:1667` 的 `w->owner == v->owner`——那是 depot 内「新车出厂自动挂到同公司散车链」的便利逻辑，与授权口径无关 |
| 7 | `src/lang/english.txt` / `simplified_chinese.txt` | `STR_COUPLE_GROUP_CROSS_COMPANY`、`STR_COUPLE_GROUP_CROSS_COMPANY_TOOLTIP`（tooltip 明说「不改变车辆所有权」） |

### 8.2 编译与验证

- 预检：`_tmp_qcheck_cross.cmd`（调用既有 `R3R_quickcheck.cmd`）逐个编译 `couple_group.cpp` / `couple_group_cmd.cpp` / `couple_group_gui.cpp` / `sl/couple_group_sl.cpp` / `train_cmd.cpp` ⇒ **`ALL_QUICKCHECK_OK`**。
- 踩坑（已登记 **KI-77**）：widget 头注释里写 `①` 会被 widget 解析器截断并插换行，生成的 `build/generated/script/api/script_window.hpp` 直接语法错误。**`src/widgets/*.h` 注释必须纯 ASCII**。
- 全量重编：改到 3 个头文件 + 2 个 lang ⇒ 按 KI-15/KI-16 必须删光 `*.obj` 全量（`_tmp_full_build.cmd`，内测树 `build\`，`-j2`，日志 `build\R3R_fullbuild.log`，完成标记 `build\R3R_fullbuild.done`）。**发行树 `build-release\` 暂不重编**——等行为实测通过后再出可发布的包，避免把未验证的改动推成「交付版」。

### 8.3 待实测（玩家操作清单）

1. 打开挂接分组窗口，选中一个自己公司的组：勾选/取消「允许跨公司挂接」，确认**勾选状态立刻跟随**、切换组时状态跟着切换、别人公司的组该行**置灰**。
2. **同公司**挂接：行为必须与改前逐位相同（这是硬回归线）。
3. **跨公司**：A 公司机车 + B 公司车底，两者**同在一个组且该组已勾选** ⇒ 贴上去能挂上；把勾选关掉、或把其中一方移出组 ⇒ **贴上去也不挂**，机车应原地等待并继续找其他候选，**不刷屏、不死循环**（这是本步最重要的「不倒退」判据，对应 KI-76 里「视同此处没有等待挂接的列车」）。
4. 挂上之后确认：**钱仍全部记在控制者（链头）** —— 这是**已知且预期**的中间状态，别当 bug 报（分账在 P3）。

### 8.4 本步之后的缺口（已登记）

- **P2 / KI-78**：只读冻结一行未写 ⇒ 现在外公司可以把挂在链里的他公司段卖掉/改造（凭空断链）。→ **第 36 轮已落地，见 §9**。
- **P3**：分账（`~CargoPayment` 按 owner + `RegisterDeferredCargoPayment`）。→ **第 37 轮已落地，见 §10**。
- **P4**：UI（车辆窗口「所属公司 / 控制者公司」、车库链状态列）。
- **P5**：D2 链路图公司维度（大项，需另立设计）。

---

## 9. P2 落地：连挂期间「他公司段只读冻结」（2026-09-17 第 36 轮）

### 9.1 语义（用户拍板 D4-①）

- 链上每个段的**车主始终保留全部权利**：随时可解挂、可收回、可自售自改。
- 对**非车主**（挂进链来的那一方，或控制着链头但车不是自己的那一方）而言，**他公司的段在整个连挂期间只读**：不能出售、不能 refit、不能当成「我的车」去改配置。
- 依据：挂接**不转移车主**（D3），所以「跨公司链」＝一条链上同时存在两个及以上公司的车；`R3RChainSpansCompanies()` 就是这个判决。
- 注意判据不是「链头 owner ≠ 该车 owner」——控制权与车主解耦后，链头是谁的车不代表谁在控制；真正的危险只在**链上确实混着两个公司**时出现，故直接以「链上存在本公司车 + 他公司车」判定。

### 9.2 实现（4 个文件）

| # | 文件 | 内容 |
|---|---|---|
| 1 | `src/couple_group.h` | 新增两个查询接口声明：`R3RChainSpansCompanies(const Train *v, Owner company)` 与 `R3RIsFrozenForeignSegment(const Train *v, Owner company)`（放在 `R3RGetSegmentPosition` 之后）。 |
| 2 | `src/couple_group.cpp` | 实现上述两接口：前者沿链（`Train::From(v->First())` + `Next()` 走到链尾）扫到「既有本公司车、又有他公司车」即 true（提前返回）；后者 = `v != nullptr && v->owner != company && R3RChainSpansCompanies(...)` ⇒ **只有真的跨公司连挂**才算冻结（单纯停在我方轨道/车库里的他车不冻结）。 |
| 3 | `src/vehicle_cmd.cpp` | ① 加 `#include "couple_group.h"`；② `CmdSellVehicle` 的 Train 分支开头加冻结循环（`SellChain` 时逐节查整链、单车时只查被卖的那一节，查到即返回），命中 ⇒ `return CommandCost(STR_ERROR_CAN_T_SELL_TRAIN)` 并写 `R3R-FROZEN-SELL`；③ `CmdRefitVehicle` 在 `only_this \|= ...` 之后、`RefitVehicle()` 之前加冻结检查（`only_this` 直接查该车；整车则用 `GetVehicleSet(vehicles_to_refit, v, num_vehicles == 0 ? UINT8_MAX : num_vehicles)` 取出**与 `RefitVehicle()` 完全一致的待改集合**再逐个查），命中 ⇒ `return CommandCost(STR_ERROR_CAN_T_REFIT_TRAIN)`，写 `R3R-FROZEN-REFIT`（**仅显式 refit 写日志**：`auto_refit` 由装卸循环驱动、可能每 tick 重试，不写以免刷屏）。 |
| 4 | `src/vehicle_gui.cpp` | 车辆详情窗口 `UpdateButtons`：`WID_VV_REFIT` 的禁用条件增加 `\|\| r3r_chain_spans_companies`，跨公司连挂时按钮置灰，与命令层判定一致（按钮只是提示，命令层才是硬门）。 |

### 9.3 边界与**刻意未改**的地方（已核实）

- **车库（`depot_gui.cpp`）不需要改**：车库列表按 `owner == _local_company` 过滤，玩家**永远点不到**他公司的车；拖动/出售用的 `sel` 必然是自己的车，`TrainDepotDetachSegment` / `TrainDepotMoveSegment` 发出的命令也都带 `CheckOwnership`，他公司车会被命令层挡掉。唯一真实漏洞是「我方链头带着他公司段被整链出售」，已由 `CmdSellVehicle` 的冻结循环堵住。
- **只堵「车主之外」的一方**：车主自售自改仍放行（D4-① 原话），因此本步**不**引入「连挂期间连自己的车也不能卖」的额外限制。
- **不动 `CmdDepotSellAllVehicles` / `CmdMoveRailVehicle` / `CmdConvertRailVehicle` / `CmdReverseTrainDirection`**：它们要么最终汇入 `CmdSellVehicle`、要么自带 `CheckOwnership`，重复设卡只会造成口径分叉。
- **已知残留（不在 P2 范围）**：车主卖掉自己链头时，借用出去的那份排程（`r3r_orders_borrowed` / `orders_backup` 单层）的归宿问题仍按 KI-01/KI-02 处置，与本步无关。

### 9.4 日志与复测清单

- 新探针：`R3R-FROZEN-SELL veh=%d owner=%d company=%d sell_chain=%d`、`R3R-FROZEN-REFIT veh=%d owner=%d company=%d only_this=%d`（默认只在 `R3RDbgOn()` 打开时写）。
- 复测 1：A 机车挂 B 车底（同组 + 允许跨公司）⇒ A 出售**自己**的车应成功；A 用 Ctrl 整链出售 ⇒ **必须报错且 B 的段完好无损**（日志出现 `R3R-FROZEN-SELL`）。
- 复测 2：同场景下 A 对链做 refit（含整车 refit / 车库 refit 全部）⇒ 必须报错；B 来 refit 自己的段 ⇒ 应放行（B 是车主）。
- 复测 3：B 解挂收回自己的段后，A 对残留链做出售/refit ⇒ 恢复正常（不再冻结，`R3RChainSpansCompanies` 变 false）。
- 复测 4：跨公司连挂时车辆窗口的 refit 按钮应置灰。
- 编译踩坑（已登记 **KI-79**）：`to_underlying()` 不能用于 `CompanyID`（它是 `PoolID` 类，必须用 `.base()`）；`Train::From()` 在 `vehicle_cmd.cpp` / `vehicle_gui.cpp` 里不可用 ⇒ 两个新接口统一取 `const Vehicle *`，调用方无需 train.h。预检脚本 `_tmp_qcheck_freeze.cmd` 覆盖 `couple_group.cpp` / `vehicle_cmd.cpp` / `vehicle_gui.cpp` 三个 TU，已 `ALL_QUICKCHECK_OK`；随后启动干净全量重编（`_tmp_full_build.cmd`，先删光 `*.obj`）。

- **P2 全量重编结果：通过**。`build\R3R_fullbuild.log` 691 步、`build\R3R_fullbuild.done` = `EXIT_CODE=0`；产物 `build\openttd.exe` @ 2026-09-17 08:00，而 `couple_group.cpp`(07:10) / `couple_group.h`(07:26) / `vehicle_cmd.cpp` / `vehicle_gui.cpp` 的 mtime 全部早于 exe ⇒ 无混合 obj（KI-15 卫生合格）。产物自证：`R3R-FROZEN-SELL` / `R3R-FROZEN-REFIT` 两条探针串已能在 exe 里检索到（核对脚本 `_tmp_verify_freeze.cmd`，纯 ASCII）。**下一步 = §9.4 复测 1~4 的玩家实测**。

---

## 10. P3 落地：跨公司运费分账（2026-09-17 第 37 轮）

### 10.1 语义（D1=甲 + D6-①「交付即分账，不新增界面」）

- 一票货由 A、B 两家共同承运时，**谁的车跑的那一腿，运费就进谁的银行账户**；A 拿 A 段腿的钱、B 拿 B 段腿的钱，两者之和 == 改前「链头独得」的金额（只换收款人，不凭空造钱）。
- 判据 = **正在装卸的那一辆车（`v`）的 owner**：`v->owner != front->owner` 才算跨公司腿。同公司链（含所有非火车车辆、单车）恒等于 `front->owner` ⇒ 逐位不变。
- 结算方式 = **复用既有的 deferred payment 账本**：`CargoPacket::RegisterDeferredCargoPayment(公司, 车型, 金额)` + 紧接着的 `cp->PayDeferredPayments()`（`SubtractMoneyFromCompany(cid, CommandCost(exp, -money))` ⇒ 钱当场进该公司），因此是「交付/中转当时结清」，不做账期、不做账单 UI（D6-①）。
- 统计口径不变：`profit_this_year` 仍按段头入账（KI-71 机制），跨公司时「哪家公司拿多少钱」只由新的 payee 决定。

### 10.2 实现（2 个文件）

| # | 文件 | 内容 |
|---|---|---|
| 1 | `src/economy_base.h` | `CargoPayment` 新增 **NOSAVE** 字段 `CompanyID r3r_payee_company = CompanyID::Invalid()`；新增 `CompanyID R3RGetPayeeCompany() const` 的**声明**（`Invalid` ⇒ 回退 `this->front->owner`），**定义放在 `economy.cpp`** —— 该头里 `Vehicle` 只有前置声明，写成内联函数体会 `error C2027`（第 1 次全量重编踩到，见 §10.7）。放在既有 `r3r_recipient` / `r3r_recipient_base` / `r3r_booked` 之后。 |
| 2 | `src/economy.cpp` | ① `R3RSetPaymentRecipient(v)`：开作用域时记 `r3r_payee_company = (v != nullptr && v->owner != this->front->owner) ? v->owner : CompanyID::Invalid()`，`v == nullptr` 关作用域时清回 `Invalid`（**只在真的跨公司时才置位**，因此任何 owner 异常的车都不会被牵扯进来）；② `PayFinalDelivery`：`payee == front->owner` ⇒ 原路 `route_profit += profit`；否则 `cp->RegisterDeferredCargoPayment(payee, front->type, profit)` 且**不**进 `route_profit`；③ `PayTransfer`：`RegisterDeferredCargoPayment` 的第一参由 `front->owner` 改为 payee；④ `~CargoPayment` 补注释（跨公司份额已在交付时付清、不在 route_profit 里）；⑤ 加 `#include "r3r_perf.h"` 与两条探针（见 §10.5）。 |

### 10.3 资金守恒论证（为什么不会造钱、也不会吞钱）

- **同公司链（回归门）**：三条路径都以 `payee == front->owner` 收场 ⇒ `route_profit` / `visual_profit` / `visual_transfer` / `r3r_booked` / 银行入账与改前**逐位相同**。
- **跨公司链 · 交付腿**：`DeliverGoods()` 返回的 `profit` 不再进 `route_profit`，而是以 deferred payment 记到 payee 名下、**同一次调用里**由 `PayDeferredPayments()` 结清 ⇒ 钱从「链头公司」改付「该腿公司」，**游戏内货币总量不变**。
- **跨公司链 · 中转腿**：`PayTransfer` 注册的那笔 feeder 分成改付 payee；该笔钱本来就与 `feeder_share` 在最终交付时对冲（`profit -= cp->GetFeederShare(count)`），**只换收款人**。
- **链头公司的入账仍只在析构里发生一次**（`SubtractMoneyFromCompany(_current_company, CommandCost(exp, -route_profit))`），而 `route_profit` 已剔除跨公司腿 ⇒ 不存在「B 拿一次 + 链头再拿一次」的双付。代码里用 if/else（而不是先加后减）就是为了让这个不变量一眼可见。
- **存档安全**：本步**没有新增任何存档字段**（`sl/economy_sl.cpp` 未动）。`route_profit` 是既有 SLE 字段、且在跨公司分支里是「就地少加」⇒ 任何时刻存档、读回都正确；`r3r_payee_company` 是瞬时作用域状态（与既有 `r3r_recipient` 同类，NOSAVE），最坏情况只是该次装卸的下一笔回落给链头，**不会重复付款**。

### 10.4 刻意未改（边界，重要）

- **`DeliverGoods()` 的「交付公司」参数仍然传链头公司**：交付统计（`delivered_cargo`、货物流面板 `AddCargoDelivery`）、**补贴（subsidy）判定**、**工业独占权**（`exclusive supplier / consumer`、`MayLoadUnderExclusiveRights` 一族）全部保持原样。理由：一旦改成「按车辆 owner 算」，就会出现「链头 A 持有独占权、却挂着一辆不持有的 B 的车 ⇒ 工业按 B 判拒收、货改投城镇」这种**玩法级回退**，风险远大于收益。⇒ **P3 只分「钱」，不分「权/统计」**；若要「谁的车交付就记谁的统计与补贴」，须单独立项（已登记 **KI-81**）。
- **折旧 / 运行费**不在本步：仍按链头（`front->owner`）走，与 D4（运行费记控制者）一致，未改。
- **不做**账期、账单、公司间明细 UI（D6-① 明确不做）。

### 10.5 日志与复测清单

- 新探针（只在 `R3RDbgOn()` **且**发生跨公司支付时写，同公司链不写、不刷屏）：
  - `R3R-PAY-FINAL front=%u front_owner=%d payee=%d profit=%lld count=%u`
  - `R3R-PAY-TRANSFER front=%u front_owner=%d payee=%d profit=%lld count=%u`
- **复测 1（回归门，最重要）**：同公司多段链跑一票到站 ⇒ 公司总收入、各车 `profit_this_year`、货物流面板与改前**逐位一致**。
- **复测 2（分账）**：A 机车挂 B 车底（同组 + 允许跨公司），从 A 的站装货、到 B 的站卸货 ⇒ B 银行增加、A 银行增加，**两者之和 == 单公司承运同一票的金额**；日志应出现 `R3R-PAY-FINAL ... payee=<B>`。
- **复测 3（中转）**：跨公司链中途站中转 ⇒ `R3R-PAY-TRANSFER` 的收款方 = 跑那一腿的公司。
- **复测 4（并存对照）**：同一场景分别用「同公司」与「跨公司」跑一遍，核对「公司总账」「各车统计之和」两组数字相等。
- **复测 5（存档）**：跨公司链运输途中存档、读档，再等交付 ⇒ 不得有任一公司多拿一次运费。

### 10.6 编译

- `economy_base.h` 被改 ⇒ 按 **KI-15/KI-16** 必须**全量重编**（`_tmp_full_build.cmd`：先删光 `*.obj`，`-j2`，结果写 `build\R3R_fullbuild.log` + `.done`）。
- 预检：`_tmp_qcheck_p3.cmd`（先删 `build\CMakeFiles\openttd_lib.dir\src\economy.cpp.obj` 再单 TU 编译）⇒ **`ALL_QUICKCHECK_OK`**（`NINJA_EXIT=0`）。
- 全量重编结果与产物核对见 **KI-80**。

### 10.7 全量重编第 1 次失败与修复（踩坑记录）

- **现象**：`build\R3R_fullbuild.done` = `EXIT_CODE=1`，`[350/691] FAILED: ...\src\saveload\economy_sl.cpp.obj`，而**日志里看不到任何编译器诊断**（只有 `FAILED:` 与随后的 `/showIncludes` 输出）⇒ 第一反应是 cl.exe 被 OOM 杀掉（本机 8 GB）。
- **真因（`_tmp_qcheck_ecosl.cmd` 单 TU 复现）**：`src/economy_base.h(72): error C2027: 使用了未定义类型"Vehicle"`。P3 新加的 `R3RGetPayeeCompany()` 当时写成**头文件内联函数体**，而 `economy_base.h` 只包含 `vehicle_type.h`（`Vehicle` 仅前置声明）⇒ 函数体里 `this->front->owner` 无法编译。**只有一部分 TU 会炸**：`economy.cpp`（`vehicle_base.h` 完整）编得过，`src/saveload/economy_sl.cpp` 炸。
- **修复**：头里改成纯声明 `CompanyID R3RGetPayeeCompany() const;`，定义搬到 `economy.cpp`（紧接 `R3RSetPaymentRecipient`）。
- **为什么第一遍 grep 没看见**：`R3R_fullbuild.log` 是**GBK/ANSI(936)** 混合编码（cl 中文输出），ripgrep（`search_content`）看不到含中文的行，只命中了纯 ASCII 的 `FAILED:` ⇒ 制造了"没有诊断信息"的假象。**以后诊断构建失败一律用 `findstr /C:"error C" build\R3R_fullbuild.log`**。已登记 **KI-82**。
- **教训（并入 KI-82）**：改「被多个 TU 共同包含的头」时，预检清单要覆盖**其它**包含者（本轮已把 `economy_sl.cpp` 固定进 `_tmp_qcheck_p3.cmd`）；`economy_base.h` 里**不要**写需要完整 `Vehicle` 的内联函数体。
- **处置**：修复后两个 TU 预检 `ALL_QUICKCHECK_OK`，按 KI-15/KI-16 重新**删光 `*.obj` 全量重编**（第 2 次）。

### 10.8 全量重编第 2 次（最终）：通过，且合并执行了 KI-66

- **执行方式**：第 2 次全量重编起初已跑起来，但为「既然已付全量重编的代价，就把等待中的头文件改动一起做掉」而**主动中止**（`taskkill /F /IM ninja.exe /T`）：因此它留下的日志尾部停在 `/showIncludes` 输出中途，**没有任何 `error C` / `FAILED:`**，`R3R_fullbuild.done` = `EXIT_CODE=1` —— 这是**人为中断**，不是编译失败（诊断口径见 KI-82②：只看 `EXIT_CODE` 会误判）。随后合并 **KI-66**（删除 `src/vehicle_base.h` 的 `IncrementRealOrderIndex()` 里遗留的 `ADVANCE: veh=.. order=.. real_before=..` 临时调试打印）后，重新删光 `*.obj` 一次跑完。
- **结果**：`build\R3R_fullbuild.done` = `EXIT_CODE=0`，日志末行 `[691/691] Linking CXX executable openttd.exe`；产物 `build\openttd.exe` @ **2026-09-17 16:23**（50 633 728 B）。
- **KI-15 卫生核对**（`_tmp_verify_p3.cmd`，纯 ASCII，无参数）：`newest_src=vehicle_base.h @ 16:02:21`（即 KI-66 那次改动）、`oldest_obj=alloc_func.cpp.obj @ 16:03:20`、`obj_total=619`、**`stale_obj_count=0`** ⇒ 无陈旧/混合对象，测的那版 == 刚改的那版。
- **产物自证**：`R3R-PAY-FINAL` / `R3R-PAY-TRANSFER`（P3 分账探针）与 `R3R-FROZEN-SELL` / `R3R-FROZEN-REFIT`（P2 冻结探针）**均可检索到**；`ADVANCE: veh=` **已检索不到** ⇒ KI-66 的删除确实进了产物。
- **口径澄清（避免下次误报）**：同一核对脚本对 `CGF_CROSS_COMPANY` 报 `MISSING` 属**预期** —— 它是枚举标识符，代码里不存在同名字符串字面量，不会出现在二进制里；而 `R3R-FROZEN-SELL` 一类是 `R3RDbgWrite()` 的字面量参数，所以能搜到。判据是「有没有字面量」，不是「功能在不在」。
- **结论**：P1~P3 **全部落地且同处一版已编译产物**；agent 侧可执行部分到此结束，剩余 = §8.3 / §9.4 / §10.5 的**玩家游戏内实测**（P3 的复测 1 是回归门）。

### 10.9 本轮踩坑小结（可复用）

- 全量重编期间**不要为了合并改动而 kill ninja**然后直接跑第二次：第一次的日志会被覆盖（本例中第 1 次失败的原始日志就是这样丢的，只靠 `_tmp_qcheck_ecosl.cmd` 单 TU 复现才拿回 `C2027`）。若要保留失败证据，先把 `R3R_fullbuild.log` 另存一份再重启。
- `cmd /c "含空格的路径.cmd 参数"` 会被 cmd 的引号剥离规则拆开（只有「整条就是一个可执行文件名」时引号才保留）⇒ 探针脚本一律**不带参数**（或把参数写进脚本），本条已按此改写 `_tmp_prog.cmd`。
- **本轮留下的可复用工具**（均在仓库根，纯 ASCII，无参数直接跑）：`_tmp_prog.cmd`（进度屏障探针 —— 等 `build\R3R_fullbuild.done` 最多 240 s，报告 ninja 是否在跑 / `*.obj` 计数 / 日志尾 3 行）；`_tmp_verify_p3.cmd`（产物核验 —— exe 时间戳与体积 + 「最新源码 vs 最旧 obj」的 `stale_obj_count` + exe 内探针串命中，即 KI-15 卫生检查自动化）；`_tmp_builderr.cmd`（GBK 解码构建日志，只打 `error C` / `FAILED:` / 日志尾，落实 KI-82 的诊断口径）。

## 11. P6 落地：组共享 `CGF_SHARED`（2026-09-17 第 39 轮，玩家拍板「那就按A来吧」）

### 11.1 语义（D6-③）

- **车主授权模型**：组的车主可开/关「允许他公司加入」（`CGF_SHARED`）。开了以后**别家公司能把自己的段指派进本组**，也能在分组列表里看到本组；关掉即收回授权。
- **绝不交所有权**（D3）：任何公司**永远只能指派自己公司的车**——`CmdSetCoupleGroup` 的 `CheckOwnership(t->owner)` **原样保留**。共享只放宽「哪个组可以接收我的段」，不放宽「我能动谁的车」。
- **两个开关是「与」关系**：`CGF_SHARED` 决定「能不能进组」，`CGF_CROSS_COMPANY` 仍是挂接判据的闸门；两者都开才挂得上（只共享不跨公司 = 进得了组但挂不上）。**刻意不做联动**（开一个自动开另一个），否则玩家看不出是哪个开关在起作用。
- **关共享时驱逐他公司段**：非共享组对他公司不可见，若把他公司的段留在组里，那个段就"卡在一个自己既选不中、也退不出的组"里（`CmdRemoveCoupleGroup` 需要先选中该组）。所以 `R3RSetCoupleGroupShared(group,false)` 会把该组从**所有非车主段**上摘掉。不改变所有权、**不解开已经挂好的列车**（掩码只在挂接那一刻被查询）。

### 11.2 实现（8 个文件）

| # | 文件 | 改动 |
|---|---|---|
| 1 | `src/couple_group.h` | `CGF_SHARED` 由「Reserved」转生效（注释改为 D6-③）；新增 `R3RCoupleGroupIsJoinableBy()` / `R3RSetCoupleGroupShared()` 声明；`R3RCoupleGroupIsVisibleTo` 文档补共享语义 |
| 2 | `src/couple_group.cpp` | `R3RCoupleGroupIsVisibleTo` 对共享组放开；新增 `R3RCoupleGroupIsJoinableBy`（`owner==company \|\| (flags & CGF_SHARED)`）；新增 `R3RSetCoupleGroupShared`（关共享时逐**段**驱逐他公司段，用 `R3RIsCoupleGroupCarrier` 只认段载体，返回驱逐数） |
| 3 | `src/command_type.h` | `Commands` 枚举加 `SetCoupleGroupShared` |
| 4 | `src/couple_group_cmd.h` | 声明 + `DEF_CMD_TUPLE_NT` 注册（`CmdDataT<CoupleGroupID, bool>`） |
| 5 | `src/couple_group_cmd.cpp` | `CmdSetCoupleGroup` 的组校验 `IsManageable` → `IsJoinableBy`（`CheckOwnership` 不动），跨公司加入时打 `CG-JOIN-foreign`；新增 `CmdSetCoupleGroupShared`（**owner-only**，打 `CG-SHARED ... evicted=`）；引入 `r3r_perf.h` |
| 6 | `src/couple_group_gui.cpp` | 新增「允许他公司加入」勾选框（owner-only）；新增 `IsSelectedGroupJoinable()`；加/移段按钮改用可加入判据；列表给他公司的组加「（已共享）」后缀；`CG-PAINT` 探针补 bit31 |
| 7 | `src/widgets/couple_group_widget.h` | 新增 `WID_CG_SHARED_TEXT` / `WID_CG_SHARED`（注释纯 ASCII，遵守 KI-77） |
| 8 | `src/lang/english.txt` + `simplified_chinese.txt` | 新增 `STR_COUPLE_GROUP_SHARED` / `_TOOLTIP` / `STR_COUPLE_GROUP_LIST_SHARED`（改 lang 后**必须强制重编 `strings.cpp`**，记忆 52814982） |

**存档**：`flags` 已是 `CoupleGroup` 池对象的 `SLE_UINT32` 字段（`sl/couple_group_sl.cpp`）⇒ **新位无需动存档格式**；旧存档读进来 `CGF_SHARED=0`（即"未共享"），语义正确。

### 11.3 刻意未改（边界）

- **`CmdRemoveCoupleGroup` 不加校验**（它只有 `CheckOwnership`）：这正是他公司成员「**退出**别家组」的正常路径，加校验反而会把他公司的段锁死在组里。
- **不动 `R3RCoupleAllowed` / `R3RCoupleGroupMasksAllowCrossCompany`**：闸门逻辑 P1 已收口到单点，本轮只负责让掩码交集**可能非空**，不碰判据本身。
- **不做公司间邀请/审批 UI**：共享是全公司广播式（谁都能进），符合 D6-①「不新增界面」的口径。

### 11.4 复测清单（玩家游戏内操作）

1. **可达性（本轮核心）**：甲建组 → 勾「允许他公司加入」+「允许跨公司挂接」→ 切乙公司打开挂接分组窗口 ⇒ **甲的组出现在列表里并带「（已共享）」**；选中它 →「指派段」→ 点乙自己的车 ⇒ 成功；日志出现 `CG-JOIN-foreign`。
2. **挂接**：乙的段进组后，甲的车去挂它 ⇒ 挂上（`CG-CROSS-ALLOW`），钱/成本仍全记在链头（分账见 §10）。
3. **越权仍被拦**：乙试图把**甲的车**指派进任何组 ⇒ 被拒（`STR_ERROR_OWNER_NOT_YOURS`）；乙对甲组的改名/删除/两个开关 ⇒ 按钮置灰。
4. **关共享的驱逐**：甲取消共享 ⇒ 日志 `CG-SHARED ... shared=0 evicted=N>0`；乙的段从组里消失、列表里也看不到该组；**已挂好的列车不解开**（继续跑）。
5. **同公司零回归**：不共享时一切与第 37 轮产物一致（列表、按钮、挂接、解挂）。

### 11.5 编译

- 改 2 个头文件（`couple_group.h`、`widgets/couple_group_widget.h`）+ 2 个 lang txt ⇒ **触发全量重编**，按 KI-15 / KI-24 规程：删全部 `*.obj`、`vcvars64.bat`、`-j2`、TEMP 指向 D 盘，预计 25~40 分钟。
- **单 TU 预检（先做）**：5 个受影响 TU（`couple_group.cpp`、`couple_group_cmd.cpp`、`couple_group_gui.cpp`、`sl/couple_group_sl.cpp`、`train_cmd.cpp`）+ 追加 `command_table.cpp`（新命令模板实例化最易出错的那个）全部 `EXIT=0` / `ALL_QUICKCHECK_OK`。
- **结果：成功**。`_tmp_full_build.cmd` 后台运行（17:39:06 启动，18:10:43 链接完成），日志 `build\R3R_fullbuild.log` 末行 `[701/701] Linking CXX executable openttd.exe`，`build\R3R_fullbuild.done` = `EXIT_CODE=0`，日志内 `error C|fatal error|LNK####|FAILED:` 计数 **0**。产物 `build\openttd.exe` 50 637 312 B / 2026-09-17 18:10:43。
- **语言包一致性（本次重点，防记忆 52814982 的「No available language packs」）**：`build\generated\table\strings.h` 17:36:00 → `strings.cpp.obj` 18:05:36（**obj 晚于生成头，顺序正确**）；`build\lang\english.lng` 头部 `ident=0x474E414C`（"LANG"）、`version=0xEDC76EB`，与 `strings.h` 的 `LANGUAGE_PACK_VERSION = 0xEDC76EB` **逐字节一致**。
- 游戏内复测仍待玩家执行（§11.4 五条），故 KI-85 状态维持「未验证」。

## 12. D6-④ 落地：两个开关合并为一个（2026-09-17 第 43 轮，玩家「先合并再实测」）

### 12.1 依据（KI-91 的核实结论）

玩家问「那两个按钮是可以合并的对吧」。核实四种组合后只有一种可用：

| 组合 | `CGF_SHARED`(加入) | `CGF_CROSS_COMPANY`(挂接) | 结果 |
|---|---|---|---|
| 甲 | 1 | 1 | **唯一放行**：乙的段进得了组、也挂得上 |
| 乙 | 1 | 0 | **死状态**：乙的段进得了组（`CG-JOIN-foreign`），但 `R3RCoupleGroupMasksAllowCrossCompany` 永远 false ⇒ `COUPLE-FAIL` 刷屏 |
| 丙 | 0 | 1 | **不可达**：`IsVisibleTo` / `IsJoinableBy` 都要求 SHARED，乙根本看不见也加不进该组 |
| 丁 | 0 | 0 | 默认（现状） |

⇒ 合并**不会砍掉任何玩家可感知的状态**，且英文字符串自己就写着「要真正跨公司挂接还得开另一个」。
玩家原话：「那两个按钮是可以合并的对吧，那么就先合并再实测吧」。

### 12.2 语义（D6-④）

- **一个开关做两件事**：`CoupleGroup::CGF_ALLOW_OTHERS = CGF_SHARED | CGF_CROSS_COMPANY`（两位**保留**，注释写明 D6-①+D6-③）。开 = 两位同置（他公司可加入 **且** 可跨公司挂接）；关 = 两位同清。
- **读判据全部收口到 `AllowsOthers()`**：`R3RCoupleGroupMasksAllowCrossCompany`（`couple_group.cpp:137`）、`R3RCoupleGroupIsVisibleTo`（`:391`）、`R3RCoupleGroupIsJoinableBy`（`:407`）。
- **关闭语义是复合的**（沿用 D6-③）：`R3RSetCoupleGroupAllowOthers`（`:148-186`）清两位后**逐段驱逐**他公司段（`R3RIsCoupleGroupCarrier` + `R3RRemoveCoupleGroupFromSegment`）并返回驱逐数；**不改变所有权、不解开已挂好的列车**（掩码只在挂接那一刻被 `R3RCoupleAllowed` 查询）。
- **存档字段不变**：`flags` 仍是同一个 `SLE_UINT32`（`sl/couple_group_sl.cpp`）。旧档**任一位为 1 即视为已开放** ⇒ 不需要迁移、也不会出现「半开」残渣。
- **权限不变**：命令仍 owner-only（`R3RCoupleGroupIsManageable`），`CGF_*` 从不交所有权（D3）。

### 12.3 实现（7 处文件）

| # | 文件 | 改动 |
|---|---|---|
| 1 | `src/couple_group.h` | 新增 `CGF_ALLOW_OTHERS = CGF_SHARED \| CGF_CROSS_COMPANY` 与 `bool AllowsOthers() const`；`R3RSetCoupleGroupShared` / `R3RSetCoupleGroupFlags` 声明合并为 `uint R3RSetCoupleGroupAllowOthers(CoupleGroupID, bool)`；所有注释改为「一个开关」口径 |
| 2 | `src/couple_group.cpp` | `R3RSetCoupleGroupAllowOthers`（置/清两位 + 关时驱逐）；`R3RCoupleGroupMasksAllowCrossCompany` / `IsVisibleTo` / `IsJoinableBy` 改用 `AllowsOthers()` |
| 3 | `src/command_type.h` | `Commands::SetCoupleGroupFlags` + `SetCoupleGroupShared` → 单一 `Commands::SetCoupleGroupAllowOthers` |
| 4 | `src/couple_group_cmd.h` | 声明 + 注册合并为一个 `DEF_CMD_TUPLE_NT`（`CmdDataT<CoupleGroupID, bool>`，`CommandType::OtherManagement`） |
| 5 | `src/couple_group_cmd.cpp` | `CmdSetCoupleGroupAllowOthers`（owner-only 不变），日志 `[R3R] CG-OPEN group=%u allow_others=%u evicted=%u` |
| 6 | `src/couple_group_gui.cpp` | 复选框由两个 → 一个：`couple_group_gui.cpp:418-421` 用 `AllowsOthers()` 驱动 lowered+disabled；`:509` 改投 `SetCoupleGroupAllowOthers`；`CG-PAINT` 探针只留 `allow_others << 30`；删除 `WID_CG_SHARED` 的 onclick 分支 |
| 7 | `src/widgets/couple_group_widget.h` + 双语 lang | 删 `WID_CG_SHARED_TEXT` / `WID_CG_SHARED`（`WID_CG_CROSS_COMPANY` 注释改为 D6-1+D6-3）；`STR_COUPLE_GROUP_CROSS_COMPANY(_TOOLTIP)` 改为「允许他公司加入并跨公司挂接」并写明关掉会驱逐他公司段；旧串 `STR_COUPLE_GROUP_SHARED(_TOOLTIP)` **保留但已无引用**（与 `SET_AS_FRONT_WAGON` 遗留串同处理，避免动语言包编号） |

### 12.4 归零的已知状态（写进台账）

- 死状态 `{SHARED=1, CROSS=0}` **从此不可达** ⇒ KI-91 的「`COUPLE-FAIL` 刷屏」诱因消失。
- KI-90 依赖的 `WID_CG_SHARED` 已删除 ⇒ 该条的落点从「两个开关置灰」收敛为「单个开关 + 新建按钮置灰」，其余判据不变。
- KI-85 的「共享与跨公司是『与』关系」描述作废，改为「同一个开关」；其余未验证项（关共享驱逐、旧档兼容、越权提示）语义不变。

### 12.5 编译（全量，按 KI-15 / KI-16）

- 改 2 个 `src/*.h` + 双语 lang ⇒ 必须**删全部 `*.obj` 全量重编**（`_tmp_full_build.cmd`，`-j2`，TEMP 指 D 盘）。
- **结果：成功** ⇒ `build\R3R_fullbuild.done` = `EXIT_CODE=0`，日志 `build\R3R_fullbuild.log` 末行 `[706/706] Linking CXX executable openttd.exe`（`error C` / `fatal error` / `FAILED:` 计数 0）；产物 `build\openttd.exe` **50 637 824 B / 2026-09-17 20:20:48**。
- **KI-15 卫生**：`newest_src = couple_group_gui.cpp @ 19:46:15` < `oldest_obj = alloc_func.cpp.obj @ 19:48:50`，obj 619 个 ⇒ `stale_obj_count = 0`。
- **KI-16 语言包**：`strings.cpp.obj` @ 20:15:31；两个 `.lng` 头 4 字节 = `4C 41 4E 47 EB 76 DC 0E` == `strings.h` 的 `LANGUAGE_PACK_VERSION = 0xEDC76EB`（本轮只改串正文，版本号未变，故 `strings.h` 未重新生成亦无风险）。
- **产物自证**：`findstr` 可检索 `CG-OPEN` / `allow_others`。

### 12.6 复测清单（玩家游戏内操作）

1. **界面**：挂接分组窗口只剩**一个**复选框「允许他公司加入并跨公司挂接」，勾选状态随所选组变化；未选中组或非本公司的组仍置灰。
2. **开**：甲勾上 → 乙能看见该组、能把自己的段加进去（日志 `CG-JOIN-foreign`）；两车挂上（`CG-CROSS-ALLOW`）。
3. **关**：甲取消勾选 → 日志 `[R3R] CG-OPEN group=N allow_others=0 evicted=M`；乙的段从组里消失、组也从乙的列表消失；**已经挂好的列车继续跑、不被解开**。
4. **旧档兼容**：读入一份「只开了旧 `CGF_SHARED`（或只开了旧 `CGF_CROSS_COMPANY`）」的存档 ⇒ 复选框显示为**已勾选**（任一位为 1 即视为开放）。
5. **同公司零回归**：不勾选时列表、按钮、挂接、解挂与第 42 轮产物一致。
6. **死状态不可达**（对照项）：无法再制造「能加入但挂不上」的组合。

### 12.7 刻意未改

- **不动数据布局**：没有删除任何一位（改法 (b)「删位」需动存档读档兼容，风险高，未采纳）⇒ 旧档、旧命令语义的兼容代价为零。
- **不动 `R3RCoupleAllowed` / 挂接闸门**：P1 已收口到单点，本轮只让掩码的产生与读取都走同一个开关。
- **不删遗留语言串**：`STR_COUPLE_GROUP_SHARED` / `_TOOLTIP` 保留无引用，避免重排语言包编号。

