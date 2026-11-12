# 第 420 轮（2026-11-11）：车站名标志 / 调度编辑下拉 / 整站可管调度池 / 排程删除与编辑 / 调度池显示列车名 / 分配接线失败

> 临时分析报告。本文件是本轮唯一可信状态载体：先落盘，再取证，结论只认文件。
> KI 条目：`R3R_KNOWN_ISSUES.md` 末尾「第 420 轮」小节（KI-597 ~ KI-602）。

## 一、玩家报告原文（2026-11-11）

> 我向你报告以下问题：
> 1，车站头顶的名称标志不见了，即使我检查了"透明选项"也没有
> 2，列车调度编辑界面按钮有问题，我希望对于 GOTO_STATION 类命令，是点一下那个"调节班次"按钮之后，只出现一个下拉框，然后出现两个选项"调节班次"和"等待分配"，这两个选项互斥，选择了任意一个选项之后，这个下拉框上面的按钮不要表现为被按下，而是在前往车站命令后面加一个括号，括号内写明"等待调节班次"或"等待分配调度"，就像前往车库命令的"等待调节班次"那样
> 3，我希望，车站的"整站"也能管理调度池和设为检修所。
> 4，我希望，在公司排程管理界面，能够直接删除有主但没有处于借用状态的排程，此时所有自有表指向此表的列车改为指向一个新建的空表。能直接编辑无主的排程
> 5，在车站的调度池管理界面，添加调度，显示出来的不是调度名称，而是列车名称。
> 6，我尝试了一下分配调度，发现接线似乎失败了

## 二、逐条初步定性与「猜测根因（待验证）」

### KI-597（问题 1）车站头顶的名称标志不见了

- 现象：地图上车站上方的站名标志消失，且与「透明选项」无关（玩家已排除该开关）。
- 猜测根因（待验证 A）：**与本轮无关的既有回归**——上一轮（第 419 轮）只改了 `src\station_gui.cpp` 的调度池窗口与 `src\r3r_schedule_gui.cpp` 的排程池窗口，理论上不触碰地图站名绘制；但「车站调度池」改动动了 `OnPaint`/`OnClick`，需先确认没有误删与站名标志共用的状态。
- 猜测根因（待验证 B）：站名标志的绘制判据上挂了 `Station::name` 或 `R3R` 站场（yard）相关字段，某个提交点把名字清空/站场重编号后标志被跳过（R3R 有站场重命名/删除功能，KI 里 SYRD 升到 v5）。
- 取证入口：`viewport.cpp` / `station_gui.cpp` 的站名标志绘制（搜 `stationnames`、`StationName`、`DrawStationName`、`STR_STATION_SIGN`）；`build\R3R_debug.log`；`_settings_client.gui.station_show_name` 类设置项。
- 严重度（待定，初判中）。

### KI-598（问题 2）GOTO_STATION 的「调节班次」按钮改下拉，两项互斥，且不在按钮上按下、改为命令文本后缀

- 玩家口径逐字：
  - 点「调节班次」按钮 ⇒ **只出现一个下拉框**（不直接生效），下拉里两项：「调节班次」「等待分配」；
  - 两项**互斥**（选一个即取消另一个）；
  - 选了之后，**下拉框上面的按钮不要表现为被按下**；
  - 而是在「前往车站」命令文本后加括号：「（等待调节班次）」或「（等待分配调度）」；
  - **参照「前往车库」命令现有的「等待调节班次」写法**（同一视觉风格）。
- 猜测根因（待验证）：现状 `WID_O_...` 是 `NWID_BUTTON_DROPDOWN` 或纯按钮 + `SetWidgetLoweredState`（本 fork 无 `WWT_CHECKBOX`，第 418 轮已确认用 lowered 表达开关），所以「按钮表现为被按下」正是当前实现；需改为 dropdown-only 语义 + 文本后缀。参照物是车库那条 `STR_ORDER_..._WAIT_...` 的拼装方式。
- 待确认口径 C-1：括号后缀的中文原文——玩家同时写了「等待调节班次」（车库对照）与「等待分配调度」（车站新项），但「调节班次」那项的括号文字玩家写的是「等待调节班次」；是否车站版也要用「等待调节班次」四字、「等待分配」对应「等待分配调度」七字？待玩家确认（暂按玩家字面实现：调节班次 ⇒「（等待调节班次）」，等待分配 ⇒「（等待分配调度）」，若两项同时成立则……见 C-2）。
- 待确认口径 C-2：若某站既能「调节班次」又要「等待分配」——两者是否真互斥到只能选一个（玩家说互斥），互斥后另一项状态如何处理（清空？保留但灰？）。暂按「互斥：选 A 清 B」实现。
- 严重度：中（交互与文案不符）。

### KI-599（问题 3）车站「整站」也要能管理调度池和设为检修所

- 现状猜测（待验证）：调度池/检修所（workshop）相关控件只在选中某个**场（yard）**时可用，选中「整站」（`R3R_YARD_NONE` / 整站视图）时被 disabled 或直接隐藏。
- 修法方向：解禁 —— 整站视图下也允许进入调度池管理与设为检修所；需确认整站语义下调度池挂在哪个实体上（站点级 pool？还是要求先建场）；见 C-3。
- 待确认口径 C-3：「整站」的调度池是站点级独立池，还是「整站」等价于「场 0」？
- 严重度：中（功能缺失）。

### KI-600（问题 4）排程池：可删有主但未借用的排程（车改指新建空表）+ 可直接编辑无主排程

- 玩家口径：删「有主但没处于借用状态」的排程 ⇒ 所有自有表指向它的列车改指向**一个新建的空表**；另外要能**直接编辑无主排程**。
- 猜测根因（待验证 A）：`WID_SCHED_DELETE` 的可用判据走 `IsR3RLooseSchedule()`（仅无主可删），有主的一律灰 ⇒ 需换成「未被借用」判据 + 删除时做车改指。
- 猜测根因（待验证 B）：`WID_SCHED_EDIT` 的判据走 `R3RScheduleEditableBy`（仅 owner 可编辑），无主排程 owner 为 Invalid ⇒ 被判不可编辑（也可能被 `IsOwnSchedule` 的「owner 为空算自己的」放行，需实证）。
- 风险点：删除有主排程必须**先**把所有 `v->orders` 指向它的车改成指向新空表（否则悬垂指针，参照 KI-93 崩溃族），并且要处理 `orders_backup` 借用与共享链（`AddToShared`/`RemoveFromShared`）。
- 待确认口径 C-4：新空表是否也作为「无主排程」进入池子（应该是），并自动选中？
- 严重度：高（删除有主排程若不搬指会崩/丢排程）。

### KI-601（问题 5）车站调度池「添加调度」显示的是列车名称而不是调度名称

- 猜测根因（**高度可疑，直指第 419 轮**）：我在第 419 轮把标签函数提取成 `R3RScheduleLabelOf(const OrderList *)`，回退逻辑沿用第 418 轮的 `ScheduleLabel()`：
  `名字为空 ⇒ 用 GetFirstSharedVehicle() 的车名 ⇒ 再退 STR_R3R_SCHEDULE_UNNAMED`。
  于是「有主但没命名」的排程在调度池里显示成**列车名**。玩家认为应显示「调度名称」（无名时应显示无名/编号标签）。
- 待确认口径 C-5：无名排程的显示文本用什么（`STR_R3R_SCHEDULE_UNNAMED`「Unnamed schedule」/「未命名排程」？还是「调度 N」编号？）；且车名回退是否要**全局**取消（排程池窗口也一起改，保持两处一致）。
- 严重度：中（显示错误，误导）。

### KI-602（问题 6）分配调度「接线似乎失败了」

- 玩家口径：「我尝试了一下分配调度，发现接线似乎失败了」——即第 419 轮新写的 `R3RScheduleAssignWindow` 点选后**没有真的把车的 `orders` 指过去**（或点了没反应/报错）。
- 猜测根因（待验证 A）：`OnClick` 里行命中用 `GetScrolledRowFromWidget` 但列表行的 `resize_y` 与实际绘制行高不一致 ⇒ 点不中（`sel == Scrollbar::npos`）。
- 猜测根因（待验证 B）：`Command<Commands::AssignR3RSchedule>::Post()` 被命令层拒绝——`CmdR3RAssignSchedule` 里可能有 `CheckOwnership`（公司不匹配）、或对「无主表」的特殊处理（`R3RMakeOrderListOwnerless` 相关），导致直接失败只弹 `STR_ERROR_CAN_T_DO_THIS`。
- 猜测根因（待验证 C）：窗口 `FinishInitNested(1)` 用了 `WindowClass::R3RSchedule` 的 number 1，与 `BringWindowToFrontById`/`FindWindowById` 的池窗口 number 0 无冲突，但若 `WindowDesc` 的 ini_key 或 `WindowClass` 复用导致 `OnInit` 里 `owner` 被覆盖成别的公司 ⇒ `OnClick` 的 `this->company != _local_company` 早退（我加了这条守卫！）。
- 取证入口：`build\R3R_debug.log`（`ORD-...`/`R3R-...` 探针）、`order_cmd.cpp` 的 `CmdR3RAssignSchedule` 全文、`r3r_schedule_gui.cpp` 的 `OnClick`。
- 严重度：高（第 419 轮主功能不可用）。

## 三、待玩家确认口径（C 系列，回填用）

| 编号 | 问题 | 现状猜测 |
|---|---|---|
| C-1 | GOTO_STATION 两项括号文案：「（等待调节班次）」「（等待分配调度）」是否就这么写 | 按字面实现 |
| C-2 | 「调节班次」与「等待分配」互斥后，被取消那项的数据是否清空 | 选 A 清 B |
| C-3 | 「整站」的调度池是站点级独立池，还是等价场 0 | 需确认 |
| C-4 | 删除有主排程后新建的空表是否进池并自动选中 | 是 |
| C-5 | 无名排程显示什么（未命名排程 / 调度 N），车名回退是否全局取消 | 全局取消车名回退 |
| C-6 | 「分配调度」到底指哪个控件？**三选一**：A=订单窗口标题「调节班次」那个下拉按钮里的第二项「等待分配调度」；B=排程池窗口里的「分配」；C=车站调度池里的坑位 +/-。并说明是"点了完全没反应"还是"弹红字" | A 最可能（§5.12：全语言文件无「分配调度」按钮） |

### 3.1 C-3（KI-599 整站检修所语义）

现状实现口径：**整站检修所 = 只管「没有划进任何场」的站台**。已划进场 1/2/3 的站台，
其检修所开关仍由各自场管；整站开关对它们无效。

两个候选：

- **A（现状）**：整站是「剩余站台」的容器。站台集合被场划分完毕，整站拿剩下的。语义清晰、可组合、
  改动量为零。缺点：玩家若想让「全站都不接货」，得多点几次（每个场一次 + 整站一次）。
- **B（合并判定）**：整站检修所 = 全站总开关，任一场或整站开了就算该站台是检修所。
  需要改 `R3RIsWorkshopTile()` / `R3RIsYardWorkshop()` 的判定链为「或」语义，并且**检修收费要合并**
  （否则同一站台会被两个实体各收一次，或都不收）。风险：收费口径与场归属账目要重新定义。
  若选 B 还需再定一条：整站开关**关闭**时，各场开关是否仍各自生效（建议「或」，即整站关只是不额外覆盖）。

需要你回答：选 A 还是 B；若选 B，收费按「站台计一次」还是「场计费 + 整站兜底」。

建议：**A**。理由是场本身就是站台的划分，整站作为「未划分部分」不会产生双重收费与双重归属。

### 3.2 C-6（KI-602「分配调度」点不动，指哪个控件）

关键事实（§5.12 已实证）：**整个语言文件里没有任何按钮叫「分配调度」**。含这四个字的只有三处，
全是订单**标记**——`STR_ORDER_R3R_WAIT_DISPATCH` =「等待分配调度」。

三个候选，症状与对应修法完全不同：

| 候选 | 控件 | 点不动的真实原因 | 修法 |
|---|---|---|---|
| **A** | 订单窗口里标题为**「调节班次」**的下拉按钮，展开后第二项才是「等待分配调度」 | 选中订单**不是「前往车站」**类型时，`order_gui.cpp:3779` / `:4366` 直接 `break`，**零提示零反馈**；若是车站订单但排程上已有别的分离机制，则走 `:3035` 冲突守卫，会弹红字 | 把静默 `break` 改成「非车站订单时按钮置灰 + 提示」，或点下去弹专用提示 |
| **B** | 排程池窗口的「分配」 | 命令层 6 条裸 `CMD_ERROR`（非链头 / 借用中 / 有停放排程 / 跟随车 / 跨公司私有 / 车型不符），UI 只显示通用「不能执行这个命令」 | 按具体原因给专用提示；或放宽其中某条（如允许「借用中」的车重新分配） |
| **C** | 车站调度池的坑位 +/- | 命令层干净，失败只有 `NO_SUCH_ENTRY`；坑位本身不接线（`limit` 只在借出时判定），看起来"点了没变化"是正常的 | 不需要修，或加提示说明坑位只影响借出上限 |

需要你回答：**A / B / C 三选一**；再补一句「点下去是**完全没反应**，还是**弹了红字**」；
若是 A，请说一下点之前选中的那条订单是什么类型（前往车站 / 前往车库 / 前往路点 / 等待挂接 / 解挂）。

建议：先按 **A** 查。因为 A 是唯一「静默无反馈」的路径，最符合"点不动"的描述。

### 3.3 C-7（KI-597 站名牌消失，三个现场问答）

判据链已取全（§5.8）：站名牌显示的**唯一闸门**是
`ShouldShowBaseStationViewportLabel()`（`station_gui.cpp:3122-3129`），
且该函数与 `_display_opt` 自上游以来**从未被 R3R 改过**。三个 false 出口：

1. 显示选项里「车站名」被关掉；
2. 「隐藏竞争对手的告示牌」被勾上，而你在看**别人公司**的站；
3. 路点被设了 `WPF_HIDE_LABEL`（只影响路点，不影响车站）。

三个问题：

1. 是**全部车站**的牌子都不显示，还是**某些车站**？
2. **新开一局**（空地图、随便建个站）是否同样不显示？
3. 站名牌消失的同时，**城镇名 / 路牌 / 告示牌**是否正常显示？

为什么问这三个：①区分「全局开关」与「单站数据」；②区分「选项/环境」与「存档/代码」；
③如果只有车站牌子没了而城镇名、路牌都在，说明不是「Signs 总开关」，而是 `ShowStationNames` 或
`ShowCompetitorSigns` 这一条；如果连城镇名都没了，那就是「透明选项 → Signs」总开关被关。

后果：若三个都正常（开关都开着、新局也不显示），则升级为真 bug，
需要查 `sign.kdtree_valid()` 与 `_viewport_sign_kdtree_valid` —— 二者为假时 `UpdateVirtCoord()`
既不 Remove 也不 Insert，sign 会停在旧数据，这属于引擎侧图形剔除表问题，不是 R3R 的判定问题。

建议：先按「显示选项 → 车站名」+「隐藏竞争对手告示牌」两个开关自查，再答上面三条。

### 3.4 C-8（KI-600 无主排程怎么编辑 / 怎么删）

**硬约束**：本 fork 的订单窗口 `ShowOrdersWindow(vehicle)` **必须绑定一辆车**
（窗口号就是 `VehicleID`），所以「编辑无主排程」不能只是把按钮放开，必须给一个编辑宿主。

三个方案：

| 方案 | 做法 | 代价 | 后果 |
|---|---|---|---|
| E-1 | 借本公司任一停靠车辆当宿主临时查看 | 小 | **语义不干净**，玩家会误以为改了那辆车。已建议放弃 |
| E-2 | 新开一个**独立的排程编辑窗口**，宿主从「车」改成「(公司, OrderList*)」 | **大**：`OrderWindow` 全篇用 `Vehicle::Get(window_number)`，订单各类下拉/校验/回显都读车（车型、货种、车库、站台可用性），要抽一层 `OrderHost` 抽象 | 真正满足「先编好再分配」，无主排程可以反复编辑、保存、再分配给任意车 |
| E-3 | 点「编辑」时**先把排程分配给一辆车**（走 `AssignR3RSchedule`）再开普通订单窗口 | **最小**：几乎零新代码 | 编辑即占用 ⇒ 排程立刻归属那辆车。若中途取消分配，编辑内容归属要再定一次；与「先编好再分配」的本意冲突 |

若选 E-3，还需要你定一条：**分配宿主怎么选**（自动选本公司第一辆空闲车 / 弹一个选车下拉 / 让你先选中一辆车再点编辑）。

另需一并拍板**删除侧**（KI-600 前半：可删「有主但没被借用」的排程）：
删除时若该排程正被某车 `r3r_orders_borrowed` 借用，直接删会留悬垂 `OrderList*`（KI-93 家族崩溃）。
两条路：①先拒绝删除并提示「该排程正被 N 辆车借用」；②自动把借用者解绑或迁移到新空表。

建议：若你能接受「编辑即占用」，选 **E-3 + 删除侧先拒绝提示**（改动最小、无崩溃面）；
若「无主先编好再分配」是硬需求，选 **E-2**，我会把 `OrderWindow` 的宿主抽象单独估一轮工期。

### 3.5 拍板结果（玩家原话要点，2026-11-11）

| 编号 | 玩家答复（原话要点） | 落地口径 |
|---|---|---|
| **C-3** | 「保持现状」 | 整站检修所 = 只管**没有划进任何场**的站台；各场开关仍各自生效。**不改判定、不改收费**。KI-599 的待拍板项关闭 |
| **C-6** | 「实际上，根据你的描述，我猜测，你理解错误了。我当时是对『调节班次』这个按钮的 GUI 有修正意见，现在这个 GUI 已经符合我预期」 | **撤回 C-6**，KI-602「分配调度点不动」**不存在该缺陷**，条目改「非缺陷（玩家澄清：是对按钮 GUI 的意见，现已满足）」。不再深挖 AssignR3RSchedule 的拒绝面 |
| **C-7** | 「全部车站的牌子都不显示，包括火车站汽车站与机场、码头；新开一局同样不显示；但城镇名与手动放置的标志正常」 | **升级为真问题**（与存档无关、与公司归属无关、与 Signs 总开关无关）。**已结案·非缺陷**（2026-11-11 玩家复查确认是自己关了显示开关）⇒ KI-597 关闭，见 §3.6 结案 |
| **C-8** | 「E2」 + 「删除时若该排程正被某车 `r3r_orders_borrowed` 借用，选 1」 | 编辑侧走 **E-2**（独立排程编辑窗口，宿主 `(company, OrderList*)`，`OrderWindow` 抽 `OrderHost` 抽象）；删除侧走 **方案 1**（**拒绝删除** + 提示「该排程正被 N 辆车借用」），不自动解绑、不迁移 |

### 3.6 C-7 追加取证（第 420 轮续二）：**§5.8 的「唯一闸门」结论有漏项**

玩家现场把范围收到最紧：**所有设施类型**（火车站/汽车站/机场/码头）全部不显示 + **新开一局同样** + 城镇名与手动标志正常。
这三条合起来排除了：存档数据、公司归属、`ShowSigns` 总开关、路点 `WPF_HIDE_LABEL`。

#### 漏项 1（关键）：`_facility_display_opt` —— 站名牌的第二个闸门

`src\viewport.cpp:2147-2151`（绘制站名牌的循环内）：

```cpp
StationFacilities facilities = st->facilities;
if (facilities.None()) facilities = STATION_FACILITY_GHOST;   // 幽灵站
if (!facilities.Any(_facility_display_opt)) break;            // ← 设施位与开关无交集 ⇒ 不画
```

同一判定在点击命中复制了一份：`src\viewport.cpp:5255-5259`。

⇒ **即使 `DisplayOption::ShowStationNames` 为真、`ShouldShowBaseStationViewportLabel()` 返回 true，
只要 `_facility_display_opt` 里不含该站的设施位（火车/汽车/巴士/机场/码头/幽灵），这块牌子就是不画。**

`_facility_display_opt` 是 **JGRPP 上游**的「按设施类型分别显示站名」开关（`src\transparency_gui.cpp:32`、`src\transparency.h:50`），
菜单入口在**设置（齿轮）下拉**里，紧跟「车站名」之后 6 项：火车 / 汽车 / 巴士 / 船 / 机场 / 已拆除的站名（`src\toolbar_gui.cpp:317-324`）。

**默认值**（`src\table\settings\misc_settings.ini:83-88`）：
```ini
def = StationFacilities({StationFacility::Train, StationFacility::TruckStop, StationFacility::BusStop,
                         StationFacility::Airport, StationFacility::Dock, STATION_FACILITY_GHOST})
```
即**六项全开**。位序与 `_facility_display_opt_modes`（`misc_settings.ini:16`：`{"TRAIN","TRUCK_STOP","BUS_STOP","AIRPORT","DOCK","","GHOST"}`）
及枚举 `StationFacility`（`src\station_type.h:54-62`：Train=0/TruckStop=1/BusStop=2/Airport=3/Dock=4/GHOST=6）**逐位对齐，无错位**。
`DisplayOption`（`src\openttd.h:44-52`：ShowTownNames=0/ShowStationNames=1/ShowSigns=2/FullAnimation=3/空/FullDetail=5/ShowWaypointNames=6/ShowCompetitorSigns=7）
与 `_display_opt_modes`（`misc_settings.ini:14`）同样逐位对齐，`def`（`:72`）含 `ShowStationNames` 与 `ShowCompetitorSigns`。

#### 漏项 2：这两组开关都**存在 `openttd.cfg` 里，不在存档里**

两者都是 `SDTG_MMANY`（`SLE_UINT8` + `full = ..._modes`）⇒ **新开一局沿用同一份 cfg，因此「新开局也不显示」与"是存档问题"完全不矛盾**，
恰恰是「cfg 里的开关被关掉」的特征签名。

#### 结案（2026-11-11，玩家复查）

**玩家原话要点**：「C7已确认是 我的问题」——即**显示开关确实是关着的**，
`_display_opt`(`ShowStationNames`) 与/或 `_facility_display_opt` 六项被取消勾选。
⇒ **非代码缺陷，KI-597 关闭**。不查 `sign.kdtree_valid()`、不加探针、不改任何代码。

留给将来的判据（备用，不主动执行）：站名牌全灭而城镇名/标志正常时，
先看设置下拉「车站名」+ 6 个设施子项；两者都开仍不显示，再查
`ShouldShowBaseStationViewportLabel()`（`station_gui.cpp:3122-3129`）的返回值
与 `viewport.cpp:2151` 的 `_facility_display_opt` 交集。

### 3.7 C-8 = E-2 落地方案（依赖面已测绘，2026-11-11）

#### 3.7.1 测绘结论（全部为绝对路径 + 行号，来自 `src\order_gui.cpp` 实测）

**三个前提必须先更正**（影响方案形状）：

1. 类名是 **`OrdersWindow`**（`order_gui.cpp:1795` `struct OrdersWindow : public GeneralVehicleWindow`），不是 `OrderWindow`。
2. 全篇**只有 1 处** `Vehicle::Get()`（`:2538`，自动替换后重置指针）。真正取车靠基类缓存：
   `window_gui.h:1220-1224` `struct GeneralVehicleWindow : public Window { const Vehicle *vehicle; }`。
   ⇒ 抽象成本比预想低：**只要把 `this->vehicle` 的读点收敛到一个 `OrderHost`，其余全是机械替换**。
3. `GetVehicleFromWindowNumber` 全树 0 命中；`Train::Get()` 等在 order_gui.cpp 0 处。唯一车型强转是 `:103` `Train::From(v)->First()->GetNextVehicle()`（`CountCoupleSegments`）。

**A. 取车入口（5 处）**：`:195`（CargoTypeOrdersWindow 构造存车）、`:2409`（唯一构造 `OrdersWindow(desc, v)`）、
`:2538`（`this->vehicle = Vehicle::Get(this->window_number)`）、`:5327`（`BringWindowToFrontById(VehicleOrders, v->index)`）、
`:5335-5338`（**按 `v->owner` / `v->IsGroundVehicle()` 选 3 个 WindowDesc** ⇒ 宿主换掉后不能再靠车选 desc）。

**B. 命令参数 = 18 处**（全部把 `v->index` / `v->tile` 传给订单命令，这是 E-2 最硬的骨头）：

| 位置 | 命令 | 传的字段 |
|---|---|---|
| `:88` | `CmdModifyOrder`（局部 helper `ModifyOrder`） | tile, index |
| `:287` `:292` | `CmdModifyOrder`（货种批量） | tile, index |
| `:318` | `CmdModifyOrder`（目的地） | index, order_id+1, GetDestination() |
| `:1983` | `CmdInsertOrder`（经 `InsertOrderCmdData`） | index |
| `:2296-2297` | `CmdSkipToOrder` | tile, index, cur_implicit_order_index |
| `:2308` | `CmdDeleteOrder` | tile, index |
| `:2333` | `CmdCloneOrder` | tile, CO_COPY, index, other_shared->index |
| `:2348` `:2351` | `CmdOrderRefit` | tile, index |
| `:2371` | `CmdDuplicateOrder` | tile, index |
| `:2389` | `CmdReverseOrderList` | tile, index |
| `:4234` | `CmdCreateGroupFromList` | VehicleListIdentifier(VL_SINGLE_VEH,…) |
| `:4238` | `CmdModifyOrder(MOF_LABEL_TEXT)` | tile, index |
| `:4267-4268` | slot/counter follow-up `Payload::Make(index, …)` | index, tile |
| `:4587` | `CmdDuplicateOrder` | tile, index |
| `:4617` | `CmdMoveOrder` | tile, index, from, to |
| `:4768` | `CmdInsertOrdersFromVeh` | tile, index, v->index, sel |
| `:4783-4784` | `CmdCloneOrder` | tile, CO_SHARE/CO_COPY, index, v->index |
| `:4560-4563` `:4595` `:4599` | `FiosOrderListInfo(this->GetVehicle(), …)` | **接口签名本身吃 `const Vehicle*`** |

**C. 写回路径**：order_gui.cpp **零处**直写 `v->orders`、**零处** `SetActiveOrderList`/`AssignOrder`；
全部经命令层。唯一 `Vehicle::orders` 写入点是 `order_cmd.cpp:4286-4355` `CmdR3RAssignSchedule`（`:4341` `v->orders = list;`）。

**D. 与车无关、可直接复用的纯 `Order*` 基础件**：`:781` `R3RYardLabel`、`:803` `R3RYardDropDownList`、
`:828` `CoupleSideDropDownList`、`:845` `CoupleTempGroupLabel`、`:869` `CoupleTempGroupDropDownList`（只需 `Owner`）、
`:910` `DepotActionStringIndex`、`:977` `GetDepotOrderGoToString`，以及全部静态字符串表（`:56-78`、`:576-780`、`:925-941`）。
另有 `:2573-2605` `OnOrderMove`、`:3180-3209` `DrawOccupancyListWidget`、`:4843-4861` `OnMouseDrag`、`:1912-1933` 命中测试 = 纯索引算术。
排程池窗口 `r3r_schedule_gui.cpp:121-459` **已完全无车**（只持 `CompanyID` + `OrderListID` 向量），是现成的参照实现。

**E. 必须来自车的属性 + 兜底**（摘要，完整表见本轮子代理输出）：

| 属性 | 位置 | 无主排程怎么办 |
|---|---|---|
`v->type` | `:2414` 等 | `OrderList` 无车型字段 ⇒ `std::optional<VehicleType>`，空时灰化车型相关控件 |
`v->owner` | 7 处（`:205/:2421/:2615/:3094/:3517/:3571/:4873`） | `OrderList::GetR3RScheduleOwner()`（`order_cmd.cpp:4152-4157`：`r3r_owner` 优先，否则 `first_shared->owner`），再退窗口 `this->owner` |
`v->index` | `:2433` `:5327` + 18 处命令 | **不可兜底**：窗口号必须换语义（新 `WindowClass` + 编码 `OrderListID`），命令层需 `OrderListID` 变体 |
`v->tile` | 18 处 | 传 `INVALID_TILE` |
`cur_real/implicit_order_index` | `:1025/:1029/:2293/:2297` | `INVALID_VEH_ORDER_ID` ⇒ 不画箭头、skip 灰化 |
`IsGroundVehicle()` | 5 处 | 由 `vehtype` 推导 |
`IsOrderListShared()` | `:2323/:2620/:3175` | 换 `OrderList::IsShared()`（`order_base.h:1821`） |
`FirstShared()` | `:2331` | 换 `OrderList::GetFirstSharedVehicle()`（`order_base.h:1827`），nullptr 提前 return |
`engine_type`/`IsEngineRefittable` | `:2397-2399` | 无主 ⇒ `can_do_refit=false`（refit/autorefit 灰化） |
`cargo_cap`/`cargo_type` | `:3549-3551/:3616-3618` | 乘客车禁 Through ⇒ 无车时**保守禁止**（否则放松限制） |
标题栏 | `:3353` `STR_ORDERS_CAPTION` 用车 index | 换 `R3RScheduleLabelOf(ol)`（`r3r_schedule_gui.cpp:65-72` 已实现） |

**F. 现有无主排程判据（可直接复用）**：
`order_base.h:1840` `IsR3RLooseSchedule()`（`first_shared==nullptr && num_vehicles==0`）；
`order_cmd.cpp:4181-4186` `R3RScheduleEditableBy()`（**static，未导出**）；`order_cmd.cpp:4168-4174` `R3RScheduleAccessibleBy()`（已导出，额外放行 public）；
`r3r_schedule_gui.cpp:404` 是当前唯一入口：`first = ol->GetFirstSharedVehicle(); if (first == nullptr) break;` ⇒ **E-2 就是把这个 break 换成打开新窗口**。

#### 3.7.2 分阶段实施计划（每阶段独立可编译、可回退）

- **阶段 1（抽象，零行为变更）**：在 order_gui.cpp 引入
  `struct OrderHost { const Vehicle *veh; OrderList *orders; CompanyID company; std::optional<VehicleType> vtype; }`，
  把 `this->vehicle` 的只读点改为 `this->host`，`GetEffectiveOrders()` 改为 `this->host.orders`。
  `OrdersWindow` 保留旧构造（`host.veh = v`），窗口号语义不变 ⇒ **行为逐字不变，只为阶段 2 铺路**。
- **阶段 2（命令层放宽）**：为 `InsertOrderCmdData` 等 CmdData 增加 `OrderListID order_list`；
  各 `CmdXxxOrder` 的取表从 `v->orders` 改为「`veh` 有效→`v->orders`，否则→`OrderList::GetIfValid(order_list)`」；
  依赖车的守卫（`IsPrimaryVehicle`、`r3r_orders_borrowed`、`orders_backup`）在无主时报错文案改为专用提示。
- **阶段 3（新窗口）**：新增 `WindowClass::R3RScheduleEditor` + `R3RScheduleEditorWindow`（复用 `OrdersWindow` 的 widget 树与绘制函数，
  仅换窗口号编码与标题），`r3r_schedule_gui.cpp:404` 的 `break` 改为 `ShowR3RScheduleEditor(ol)`。
- **阶段 4（删除侧，玩家选 1）**：`r3r_schedule_gui.cpp:345` 的删除判据从 `IsR3RLooseSchedule()` 放宽为「loose 或（有主但未被借用）」；
  借用中 ⇒ 拒绝 + `STR_ERROR` 新串「该排程正被 N 辆车借用」。

#### 3.7.3 已识别风险

- `R1`：`window.cpp:1355` 对 `VehicleOrders` 有「换公司即关闭重建」豁免，新窗口号若不参与该豁免会跨公司泄漏。
- `R2`：`CloseWindowById(VehicleOrders, v->index)` 有 7 处（`vehicle.cpp:1258`、`vehicle_gui.cpp:4210/:4520`、`timetable_gui.cpp:1406`、`train_cmd.cpp:2385/:4470/:11584/:13498/:24575`），
  新窗口号语义必须与被删车解耦，否则会误关或漏关。
- `R3`：`FiosOrderListInfo(const Vehicle*, …)`（`:4560-4563/:4595/:4599`）接口本身吃车指针，阶段 1 先保留，阶段 3 需并行改造或对它禁用。
- `R4`：`CmdInsertOrder` 等命令的 `flags` 含 `CMDF_NO_AUTOREPLACE`/公司校验，无主表要确认 `_current_company` 与 `GetR3RScheduleOwner()` 一致。

#### 3.7.4 阶段 1 实施记录（2026-11-11，已编译通过，零行为变更）

**改动文件**：仅 `src\order_gui.cpp`（未碰任何 `src\*.h` ⇒ 增量合法）。

1. 新增文件内 `struct OrderHost`（插在 `struct OrdersWindow` 之前）：
   `const Vehicle *veh` / `OrderList *orders` / `CompanyID company` / `VehicleType vtype`，
   核心方法是 `OrderList *GetOrders() const`：
   `return this->veh != nullptr ? this->veh->GetEffectiveOrders() : this->orders;`
   —— **有主时逐次动态取**（借用态 `r3r_orders_borrowed` 会换表，绝不可缓存），
   无主时才用固定 `orders`。这是阶段 1 唯一的新语义落点，且当前恒走有主分支 ⇒ 行为逐字不变。
2. `OrdersWindow` 加成员 `OrderHost host;`，构造函数（`this->owner = v->owner;` 之后）填
   `host.veh = v; host.company = v->owner; host.vtype = v->type;`
   （`host.orders` 故意留空，有主时不参与）。
3. `OrdersWindow` 新增两个统一入口：
   `OrderList *GetOrders() const { return this->host.GetOrders(); }`
   `VehicleOrderID GetNumOrdersOfHost() const`（空表返回 0）。
4. 机械替换（全部为等价改写）：
   - `this->vehicle->GetEffectiveOrders()` → `this->GetOrders()`：**15 处**；
   - `this->vehicle->GetNumOrders()` → `this->GetNumOrdersOfHost()`：**22 处**；
   - 裸 `vehicle->GetNumOrders()`（`OrderGetSel` / `GetOrderFromPt`）→ 同上：**2 处**。
5. `CargoTypeOrdersWindow`（另一个 `GeneralVehicleWindow` 子类，窗口恒绑定车）也加同名
   `GetNumOrdersOfHost()` 一行转发，仅为让上述批量替换无需排除该文件唯一一处（:174）。

**踩坑（已修）**：`Vehicle::Orders()` 返回的是 `OrderIterateWrapper<const Order>`（供 range-for），
**不是** `OrderList*` ⇒ 写成 `this->vehicle->Orders()->GetNumOrders()` 报
`error C2039: "GetNumOrders": 不是 "OrderIterateWrapper<const Order>" 的成员`（`order_gui.cpp:173`）。
正确写法就是 `this->vehicle->GetNumOrders()`。编译一次失败后已改回，第二次通过。

**构建自证**：`R3R_fullbuild.cmd inc detach`；`build\R3R_incbuild.done` = `EXIT_CODE=0`（19:19:33）；
日志 `[2/3] order_gui.cpp.obj` → `[3/3] Linking CXX executable openttd.exe`；
`src\order_gui.cpp` 19:17:38 → `order_gui.cpp.obj` 19:18:03 → `build\openttd.exe` 19:19:15；`read_lints` 0 条。

**本阶段刻意未做**（留待后续阶段，均为阶段 3 才会暴露的缺陷点）：
- 6 处 `const Vehicle *v` 参数内的 `v->GetEffectiveOrders()`（`:1004` `R3RIsCoupleAirOrderRow`，
  `:1343/:1344/:1610/:1630/:1634` 在 `DrawOrderString()` 及其 helper 内）——这些函数签名吃车，
  必须到阶段 3 才能与无主窗口共存；
- `v->index` / `v->tile` 的 18 处命令参数（阶段 2）；
- `v->owner` 7 处、`v->tile` 高亮、`cur_*_order_index` 箭头（阶段 2/3 逐类兜底）。

#### 3.7.5 阶段 2 方案修正：**新增命令**，而不是改造现有命令（2026-11-11）

原计划（§3.7.2）想「给 10 个 `CmdData` 加 `OrderListID` 字段、让 handler 在车无效时改取 `OrderList`」。
实际读 `src\order_cmd.h`（全文仅 74 行）+ `command_type.h:795-801` 后修正：

**现有命令的参数形状是编译期定死的宏，不是可扩展结构**：

| `order_cmd.h` 行 | 命令 | payload |
|---|---|---|
| 40 | `ModifyOrder` | `CmdDataT<VehicleID, VehicleOrderID, ModifyOrderFlags, uint16_t, CargoType, std::string>` |
| 41/42/44/45/46/47/48/49 | `SkipToOrder`/`DeleteOrder`/`OrderRefit`/`CloneOrder`/`InsertOrdersFromVeh`/`MoveOrder`/`ReverseOrderList`/`DuplicateOrder` | 全为首参 `VehicleID` |
| 43 | `InsertOrder` | `DEF_CMD_DIRECT_LT(... , InsertOrderCmdData)`，自带 `VehicleID veh` |
| 67-71 | R3R 既有 5 个命令 | **首参统一是 `CmdDataT<uint32_t>` / `CmdDataT<uint32_t, uint32_t>`**（即 `OrderListID::base()`）|

⇒ 给旧命令加字段会改网络/命令协议，且要动 18 处既有调用点，回归面大。
**R3R 既有做法本来就是「新增 `OtherManagement` 命令 + `uint32_t` 装 `OrderListID`」**，沿用即可。

**新方案：只新增 5 个无主命令，不碰任何旧命令**（零回归）：

| 新命令 | payload | 对应旧命令 |
|---|---|---|
| `InsertR3RLooseOrder` | `InsertLooseOrderCmdData`（新结构：`uint32_t order_list; uint16_t sel_ord; OrderFields::Value new_order;` + 三个序列化方法） | `InsertOrder` |
| `ModifyR3RLooseOrder` | `CmdDataT<uint32_t, uint16_t, ModifyOrderFlags, uint16_t, CargoType, std::string>` | `ModifyOrder` |
| `DeleteR3RLooseOrder` | `CmdDataT<uint32_t, uint16_t>` | `DeleteOrder` |
| `MoveR3RLooseOrder` | `CmdDataT<uint32_t, uint16_t, uint16_t, uint16_t>` | `MoveOrder` |
| `DuplicateR3RLooseOrder` | `CmdDataT<uint32_t, uint16_t>` | `DuplicateOrder` |

**明确不做（无主时无意义，v1 直接灰化）**：`SkipToOrder`（没有车在跑）、`OrderRefit`（没有车可装货）、
`CloneOrder`/`InsertOrdersFromVeh`（跨车复制）、`ReverseOrderList`（可留待后续）、`SetRouteOverlayColour`（车辆属性）。

**阶段 4 零新命令**：`CmdR3RDeleteLooseSchedule`（`order_cmd.h:68`、`order_cmd.cpp:4252`）已存在，
只需把它的守卫从「仅 loose」（`r3r_schedule_gui.cpp:345`）放宽为「loose 或未被借用」，
借用中则返回新增错误串。

**合并执行（2+3+4 一次全量重编）需要一次改完的头文件**：
1. `src\command_type.h` —— `Commands` 枚举末尾（`:799` 之后）加 5 个值；
2. `src\order_cmd.h` —— 加 `InsertLooseOrderCmdData` 结构 + 5 行 `DEF_CMD_TUPLE_NT`；
3. `src\window_type.h` —— `WindowClass` 枚举加 `R3RScheduleEditor`（建议紧跟 `VehicleOrders` 附近或全枚举末尾，避免影响既有编号语义；该枚举不进存档）。
其余全在 `.cpp`：`order_cmd.cpp`（5 个 handler + 序列化 + 删除守卫）、`order_gui.cpp`（`OrderHost` 无主分支 +
新窗口）、`r3r_schedule_gui.cpp`（入口与删除提示）、`lang\english.txt`（1~2 条新串）。

**已知需同步处理的点**：`WindowClass` 新值会触发所有「switch 全覆盖无 default」的地方编译报错
（`window.cpp` 的类名/关闭豁免表等），编译器会逐条指出，属于预期内的机械补齐；
另需决定该窗口是否参与 `window.cpp:1355` 的「换公司豁免」（阶段 3 再定）。

#### 3.7.6 阶段 2 实施取证：**旧 handler 不可复用，v1 收窄为 4 个命令**（2026-11-11）

读完 5 个旧 handler 后确认：它们全部以 `Vehicle *` 为轴，且校验逻辑**内联**在 handler 里，不能靠传 nullptr 复用。

| 旧 handler | 位置 | 车依赖 |
|---|---|---|
| `CmdInsertOrder` | `order_cmd.cpp:1235-1241` | 只有 3 行，真正逻辑在 `CmdInsertOrderIntl`（`:1708`）；而 `PreInsertOrderCheck`（`:1309`）**第一句就是** `if (v == nullptr \|\| !IsCompanyBuildableVehicleType(v) \|\| !v->IsPrimaryVehicle()) return CMD_ERROR;` |
| `PreInsertOrderCheck` | `:1309-1700`（约 390 行） | `v->GetNumOrders()`（`:1523`）、`v->HasUnbunchingOrder()`（`:1355/:1448/:1525`）、`v->HasConditionalOrder()`（`:1449`）等约 20 处 |
| `CmdDeleteOrder` | `:2057-2082` | 薄壳；核心 `DeleteOrder(Vehicle*, sel)`（`:2109`）维护**全部持有者**的 `cur_real/implicit/timetable` 索引、取消装载、`R3RForEachOrderListHolder`、`InvalidateVehicleOrder` |
| `CmdMoveOrder` | `:2270-2380` | `MoveOrders` 后同样对全部持有者重映射索引 + 重写条件订单的 skip 目标 |
| `CmdModifyOrder` | `:2583-3100`（约 500 行） | 先按订单类型筛 `mof`（`:2609-2667`），再逐 `mof` 分支，内含 `v->IsGroundVehicle()`（`:2673/:2677`）、`v->type != Train`（`:2681`）等 |

**关键结论**：无主表**没有任何持有者**（`first_shared == nullptr`、`num_vehicles == 0`），
所以「索引重映射 / 取消装载 / 窗口失效」这一整块**全部不需要**，每张表的编辑退化为一次 `OrderList` 原语调用。
`OrderList` 已备齐所需原语（`order_base.h`）：
`InsertOrderAt(Order &&, VehicleOrderID)`（`:1813`）、`DeleteOrderAt(VehicleOrderID)`（`:1814`）、
`MoveOrders(from, to, count)`（`:1815`）、`GetOrderAt()`（`:1732`）、`GetNumOrders()`（`:1802`）、`IsShared()`（`:1821`）。

**v1（本轮实现）只做 4 个命令**：

| 命令 | 实现 | 行数估计 |
|---|---|---|
| `CmdR3RInsertLooseOrder` | `Order new_order{}; MemberPtrsTie(new_order, Order::GetCmdRefFields()) = data.new_order;` 然后 `list->InsertOrderAt(std::move(new_order), pos)` | ~35 |
| `CmdR3RDeleteLooseOrder` | `list->DeleteOrderAt(sel)` | ~20 |
| `CmdR3RMoveLooseOrder` | `list->MoveOrders(...)` + 只对**表本身**重映射条件订单的 skip 目标（复刻 `:2361-2370`，无车部分） | ~40 |
| `CmdR3RDuplicateLooseOrder` | 复制 `*list->GetOrderAt(sel)` 插到 `sel+1` | ~30 |

**v2（下一小步）才做 `ModifyOrder`**：`CmdModifyOrder` 的 500 行需要按 `mof` 逐分支剥离车依赖
（`IsGroundVehicle` → `vtype`；`CheckOwnership(v->owner)` → `R3RScheduleEditableBy`；`v->GetOrder` → `list->GetOrderAt`），
工作量与本轮 4 个命令之和相当，故单独一步，避免一次改太多。

**v1 的已知取舍（需在代码里写注释说明）**：
- 无主表插入订单**不走 `PreInsertOrderCheck`**（它硬性要求车），因此「同一表只能有一个 unbunch 订单」
  「条件订单跳转目标必须存在」这类**语义校验在编辑期不执行**。缓解：`CmdR3RAssignSchedule`
  把排程分配给车时是最后关口；如果那里也不校验，则留给运行期容忍（订单表本就允许手写非法组合）。
- 无主表**不做目的地引用计数注册**（与 `CmdR3RCreateLooseSchedule` 的 `list->Initialize(nullptr)` 一致，
  见 `order_cmd.cpp:4222` 与 `:4269` `R3RMakeOrderListOwnerless` 的注释），故删单/移单也**不** `UnregisterOrderDestination`。

## 四、取证计划（按序）

1. GI-597：站名标志绘制链路（`viewport.cpp` 站名、`station_gui.cpp` 相关 disabled/隐藏），对比第 419 轮 diff。
2. KI-602：`CmdR3RAssignSchedule` 全文 + 新窗口 `OnClick`，先判「命令被拒」还是「点击没命中」。
3. KI-601：确认 `R3RScheduleLabelOf` 的车名回退在两个窗口的调用点。
4. KI-598：在 `order_gui.cpp` 找「前往车库 等待调节班次」的现成实现与 `GOTO_STATION` 的「调节班次」控件现状。
5. KI-599：station_gui 的整站/场 两态下调度池、检修所控件的 disabled 判据。
6. KI-600：`CmdR3RDeleteLooseSchedule` / `CmdR3RSchedule...` 的删除与编辑命令，检查搬指与借用处理。

## 五、取证记录（逐步追加）

### 5.1 KI-601 已实证（2026-11-11，第一批取证）

- `src\station_gui.cpp:3765` 车站调度池列表行：
  `DrawString(ir.left, ir.right, y, GetString(STR_R3R_DISPATCH_POOL_ENTRY, host_name, num_orders, e.issued, e.limit));`
  其中 `:3764` `const std::string host_name = GetString(STR_VEHICLE_NAME, e.host.base());`
  ⇒ **列表画的确实是模板车的车名**（不是调度名）。玩家报告成立。
- `src\station_gui.cpp:3837-3851` 「添加」下拉候选同样只用车名（`:3845` `std::string text = GetString(STR_VEHICLE_NAME, v->index.base());`）。
- 结论：KI-601 根因确认 = 显示层直接取 `STR_VEHICLE_NAME`，从未取过 `OrderList` 的排程名。
- 修法方向：列表与下拉都改取 `R3RScheduleLabelOf(<host 的 orders>)`（需要跨文件访问，见 5.4 的可行性结论），
  保留车名作为副信息（`STR_R3R_DISPATCH_POOL_ENTRY` 的参数顺序可能要改）。

### 5.2 KI-602 候选面已收窄（命令层全文）

`src\order_cmd.cpp:4286-4355` `CmdR3RAssignSchedule(flags, veh_index, ol_index)` 的拒绝路径：
| 行 | 条件 | 说明 |
|---|---|---|
| 4288/4289 | `!Vehicle::IsValidID` / `!OrderList::IsValidID` | 索引越界 |
| 4293 | `v == nullptr \|\| list == nullptr` | 指针失效 |
| 4298 | `!IsCompanyBuildableVehicleType(v) \|\| !v->IsPrimaryVehicle()` | 非链头 |
| 4300 | `CheckOwnership(v->owner)` 失败 | 非本公司车 |
| **4302** | **`v->r3r_orders_borrowed \|\| v->orders_backup != nullptr \|\| v->IsR3RFollower()`** | **该车正在借用别人的表/正在跑调度池表 ⇒ 直接拒** |
| 4307 | `!R3RScheduleAccessibleBy(list, v->owner)` | 目标表不公开且非自有 |
| 4313 | `first_shared->type != v->type` | 跨车型共享 |
| 4315 | `v->orders == list` | 已经是这张表 ⇒ 静默成功 |
- 关键推断（待验证）：`:4302` 会把「已经接过调度池排程的列车」（`r3r_follow_pool` ⇒ `IsR3RFollower()` 为真）
  与「正处于耦合借用状态的列车」一并拒掉。玩家若是对这类车点「分配」，必然失败且只弹 `STR_ERROR_CAN_T_DO_THIS`。
- `:4317` Execute 分支本身写得完整（脱离旧表 → `R3RMakeOrderListOwnerless` → 索引归零 → `Initialize`/`AddToShared`），
  且末尾**没有** `InvalidateWindowClassesData(WindowClass::R3RSchedule)`（列表不会立即刷新）——属 KI-596 同族遗漏。
- GUI 侧待核：`src\r3r_schedule_gui.cpp:630-649` `OnClick`，行命中用
  `this->scroll->GetScrolledRowFromWidget(pt.y, this, WID_SCHED_AS_LIST, WidgetDimensions::scaled.framerect.top)`
  （`:634`），行高在 `DrawWidget` 取 `list_widget->resize_y`（`:583`）、`UpdateWidgetSize` 也写的是同一控件的 `resize.height`（`:566`）
  ⇒ 两者一致，**命中面暂判无问题**（待游戏内确认）。

### 5.3 KI-598 现状已实证（与玩家要求逐条对照）

- 按钮：`src\order_gui.cpp:2823-2826` 每帧 `sep_btn->SetLowered(order->HasR3RStationSeparation() || order->HasR3RWaitDispatch());`
  ⇒ **两个标记任一为真都表现为「被按下」**，正是玩家要求去掉的观感。
- 点击：`:3770-3781`（`WID_O_R3R_SEPARATION`）弹出 `DropDownList`，两项都带 `MakeDropDownListCheckedItem`
  （`:3775` `STR_ORDER_R3R_SEPARATION` idx0、`:3776` `STR_ORDER_R3R_WAIT_DISPATCH` idx1），`ShowDropDownList(..., -1, ..., DDSF_SHARED)`。
  注意 `else` 分支（`:3778-3781`）在「不需要下拉」时直接 toggle 分离标记——这是旧行为残留。
- 选择：`:4363-4371` `OnDropdownSelect`，index==0 ⇒ 切 `MOF_R3R_STATION_SEPARATION`，index==1 ⇒ 切 `MOF_R3R_WAIT_DISPATCH`，
  **各自独立 toggle，不互斥**（可同时为真）。玩家要求「互斥单选」。
- 结论：KI-598 是「现状=两个独立 lower 开关 + 独立 toggle」→「目标=单选互斥 + 文本后缀」的改写，不涉及存档格式（沿用 xflags bit5/bit6）。
- 待办：找 GOTO_DEPOT「等待调节班次」的文本后缀现成写法（`STR_ORDER_...` 拼装点），复用同一风格。

### 5.4 KI-599 现状（station_gui 调度池窗口）

- `src\station_gui.cpp:3650-3662` `Rebuild()`：`this->yard == Station::R3R_YARD_NONE` 时**直接不收集任何行**
  （只有 `yard != NONE` 才遍历 `st->r3r_yards[yard-1].pool`）⇒ 整站视图下列表恒空。
- `:3671-3677` `GetSelectedEntry()` 同样 `yard == NONE` 即 `return nullptr` ⇒ 所有行级操作（+/- 坑位、删除）在整站下全部空转。
- `:3831` 「添加」也 `if (this->yard == Station::R3R_YARD_NONE) break;`。
- 因此 KI-599 不是「按钮被 disabled」，而是**整站状态下根本没有数据源**——需要先定「整站」的池挂在哪个实体（C-3）。

### 5.5 KI-600 现状（命令层判据）

- `CmdR3RDeleteLooseSchedule`（`order_cmd.cpp:4239-4258`）**硬闸门**：
  `:4246` `if (list->GetFirstSharedVehicle() != nullptr || list->GetNumVehicles() != 0) return CMD_ERROR;`
  ⇒ 有主（哪怕没被借用）一律不能删。玩家要求放开到「有主但未借用」，并搬指到新空表。
- `CmdR3RCreateLooseSchedule`（`:4201-4230`）已能建空表（`copy_from == 0`），`Initialize(nullptr)` 保证无主。
- 编辑判据 `R3RScheduleEditableBy`（`:4181-4186`）= `owner == Invalid || owner == company`
  ⇒ **无主排程本来就允许编辑**（命令层没问题）。待核的是 GUI 侧 `WID_SCHED_EDIT` 的可用判据是否另行拦了无主（见下一批取证）。

### 5.6 KI-597 排除项（第二批取证）

- `git status --short` 的改动清单里**没有 `src\viewport.cpp`** ⇒ 站名标志的视口绘制代码本轮（以及近期轮次）未被改动。
- `git diff -U0 -- src/station_base.h src/station_cmd.cpp | Select-String 'sign|facilit|FACIL'` **零命中**
  ⇒ 站名牌（`BaseStation::sign` / `UpdateVirtCoord`）与 `Station::facilities` 的计算链路都没有被 R3R 改动碰过。
- 结论：KI-597 **不是本轮回归，也不是 station 结构/设施被改写导致**。剩下三种可能（按概率）：
  1. `_display_opt`（游戏选项 → 显示 → 车站名）被关；玩家查的「透明选项」是另一处（`DisplayOption::ShowStationNames` vs 透明对话框的 Signs）；
  2. `st->sign` 的 kdtree 条目被移除后未重插（`UpdateVirtCoord` 的 `kdtree_valid` 分支）——若某处直接改了 `st->tile`/`st->coord` 而未调 `UpdateVirtCoord`；
  3. 站牌存在但位置被推到视野外（R3R 站场把站中心 tile 挪走 ⇒ `sign.center` 仍按旧 tile 算，理论上只是错位不会消失）。
- 待玩家确认（C-7）：是**全部车站**还是**某个/某些车站**；是否新开一局也复现；站名牌消失时城镇名/路牌/告示牌是否正常。

### 5.7 KI-598 参照物已定位（文本后缀写法）

- `src\order_gui.cpp:1139-1142`（`OT_GOTO_DEPOT` 分支）：
  ```cpp
  /* Show unbunching depot in both order and timetable windows. */
  if (order->GetDepotActionType() & ODATFB_UNBUNCH) {
      AppendStringInPlace(line, STR_ORDER_WAIT_TO_UNBUNCH);
  }
  ```
  串本身自带前置空格/括号（`STR_ORDER_WAIT_TO_UNBUNCH`），直接 `AppendStringInPlace` 就能得到玩家要的观感。
- `OT_GOTO_STATION` 分支在 `:1063-1104`，末尾（`:1102` 之前）是插入点；注意该分支同时服务 `timetable` 与订单窗口两种渲染，
  后缀必须两端都显示（玩家要求「命令后面加括号」，车库那条注释也写 in both windows）。
- 实现方案：新增两个串（如 `STR_ORDER_R3R_WAIT_SEPARATION` / `STR_ORDER_R3R_WAIT_DISPATCH`），
  在 `:1102` 前按 `order->HasR3RStationSeparation()` / `HasR3RWaitDispatch()` 追加；`order_base.h` 的 flag 保持 bit5/bit6 不变（无存档格式变更）。

### 5.8 KI-597 判据链已完整取出（结论：代码链路无改动，属显示开关/环境）

站名牌「显示与否」的唯一闸门 = `src\station_gui.cpp:3122-3129`：

```cpp
bool ShouldShowBaseStationViewportLabel(const BaseStation *bst)
{
	if (!_display_opt.Test(Station::IsExpected(bst) ? DisplayOption::ShowStationNames : DisplayOption::ShowWaypointNames)) return false;
	if (_display_opt.Test(DisplayOption::ShowCompetitorSigns) && (_local_company != bst->owner && bst->owner != OWNER_NONE)) return false;
	if (Waypoint::IsExpected(bst) && HasBit(... WPF_HIDE_LABEL) && ...) return false;
	return true;
}
```

消费点：`src\station_cmd.cpp:598`
`this->sign.UpdatePosition(ShouldShowBaseStationViewportLabel(this) ? ZoomLevel::SpriteMax : ZoomLevel::End, ...)`
⇒ 返回 false 时 zoom 传 `ZoomLevel::End`（=任何缩放都不显示），这就是「站名牌消失」的实现机制，**没有第二条隐藏路径**。

- `git diff -U1 -- src/station_gui.cpp | Select-String 'ShouldShowBaseStationViewportLabel|ViewportLabel|_display_opt'` **零命中**
  ⇒ 闸门函数与 `_display_opt` 的判定自 JGRPP 上游以来**从未被 R3R 改过**。
- 三个 false 出口逐条排除：
  1. `DisplayOption::ShowStationNames` 未勾选 —— **显示选项 → 车站名**，不是「透明选项」对话框里的 Signs；
  2. `ShowCompetitorSigns` 为真 且 `_local_company != bst->owner && bst->owner != OWNER_NONE` —— 该选项语义是「隐藏别公司的站名牌」，
     若玩家在看别的公司的站（或 `_local_company` 不是站 owner），会被这一条吃掉；
  3. 路点 `WPF_HIDE_LABEL`（只影响路点，不影响车站）。
- 结论：KI-597 **不是代码回归**，最可能是 1 或 2。待玩家回答 C-7（见 §六）。
- 若 C-7 确认两个开关都正常仍不显示，则升级为真 bug，届时需要查 `sign.kdtree_valid()` 与 `_viewport_sign_kdtree_valid`
  （二者为假时 `UpdateVirtCoord` 既不 Remove 也不 Insert，`sign` 会停留在旧数据）。

### 5.9 KI-600 GUI 侧判据与硬约束（工程量评估）

`src\r3r_schedule_gui.cpp`：
| 位置 | 现状 | 玩家要求 |
|---|---|---|
| `:345` | `WID_SCHED_DELETE` 仅当 `sel->IsR3RLooseSchedule()` 才可用 | 有线但未借用也要能删 |
| `:429-431` | OnClick **再拦一次**：`GetFirstSharedVehicle() != nullptr \|\| GetNumVehicles() != 0` ⇒ 直接 break | 同上 |
| `:348` | `WID_SCHED_EDIT` 需要 `sel_own && sel_has_vehicle`（`:339` `sel_has_vehicle = sel->GetFirstSharedVehicle() != nullptr`） | 无主排程也要能编辑 |
| `:399-405` | OnClick 里 `first == nullptr` 就 break，然后 `ShowOrdersWindow(first)` | —— |

**硬约束**：编辑 = `ShowOrdersWindow(vehicle)`，即本 fork 的订单编辑窗口**必须绑定一辆车**。
⇒「编辑无主排程」不能简单放开 disabled，必须给出一个编辑宿主：
- 方案 E-1（最小改动）：选中无主/无车排程时，`WID_SCHED_EDIT` 改为「借用本公司任一已停靠车辆临时查看」——语义不干净，放弃；
- 方案 E-2（推荐）：给无主排程开一个**只读+可编辑的独立订单窗口**，复用 `OrderWindow` 但把 `vehicle` 换成 `(company, OrderList*)`——
  改动面大（OrderWindow 全篇用 `Vehicle::Get`）；
- 方案 E-3（推荐且最小）：**把该排程分配给一辆车**（即先走 `AssignR3RSchedule`）再打开订单窗口，操作上等价于"编辑无主排程"，
  但会立即让该排程变成"有主"，与玩家「无主可编辑」的本意（先编好再分配）冲突。
- 待玩家拍板（C-8）：接受 E-3 的语义（编辑即占用，可再解绑）？还是必须真正无主编辑（E-2，成本高）？

`CmdR3RDeleteLooseSchedule`（`order_cmd.cpp:4246`）的放开方案（KI-600 后半）：
1. 先 `CreateR3RLooseSchedule` 建一张空表 `empty`；
2. 遍历所有 `orders == list` 的活车（含 `orders_backup == list` 的借用者！），把 `v->orders` 改成 `empty`（`AddToShared`/索引归零/`Initialize`）；
   **借用者**（`r3r_orders_borrowed`）必须先 `R3RRestoreOwnOrders` 语义处理，否则会破坏 KI-04 的借用层；
3. 确认 `list->GetNumVehicles() == 0` 后才 `FreeChain`。
⇒ 复用第 419 轮 `CmdR3RAssignSchedule` 的 Execute 前半（脱离旧表 → 挂新表）即可，但**必须处理借用者**，否则是 KI-93 族悬垂指针高危。

### 5.10 KI-599 根因已实证：整站在数据层根本不存在

站场窗口（`src\station_gui.cpp:3140-3460`）的 disabled 判据：
```
:3444 YARD_SEL      !can_edit
:3445 ASSIGN        !can_edit || !has_sel
:3448 SHARED_SEL    !can_edit || !sel_yard_valid || 检修所
:3449 WORKSHOP      !can_edit || !sel_yard_valid
:3451 RENAME        !can_edit || !sel_yard_valid
:3452 DELETE        !can_edit || !sel_yard_valid
:3454 DISPATCH      !sel_yard_valid          <-- 「调度池」按钮
```
`sel_yard_valid` 要求 `sel_yard != R3R_YARD_NONE`（`:3320-3325` `SelectedYard()` 的守卫）
⇒ **整站（`R3R_YARD_NONE`）时检修所/调度池按钮全是灰的**。

更底层：`Station::R3RStationYard`（`src\station_base.h:939-972`）只存在于 `r3r_yards`（`:973`）里，
`R3R_YARD_NONE` 没有任何对应的存储实体；`station_cmd.cpp` 的
`R3RDispatchPoolCount/Add/Pick` 与 `R3RIsYardWorkshop` 全部以 `yard != R3R_YARD_NONE` 为前提，
越界即返回 0/-1/false。
⇒ **KI-599 不是放开 disabled 就能解决，必须给「整站」真的加一份数据**：
```cpp
struct R3RStationYard { ... };            // 现有
/* 新增：整站（R3R_YARD_NONE）的调度池与检修所标志 */
std::vector<R3RStationYard::R3RDispatchPoolEntry> r3r_whole_pool{};
bool r3r_whole_workshop = false;
```
- 存档：SYRD 需从 v5 升 **v6**（`src\sl\station_yard_sl.cpp`），把这两项作为新块追加在末尾，旧档默认空/false；
- 消费点：所有 `R3RDispatchPool*` accessor 在 `yard == R3R_YARD_NONE` 时改指向 `r3r_whole_pool`；
  `R3RIsYardWorkshop(NONE)` 返回 `r3r_whole_workshop`；
- UI：`WID_SY_WORKSHOP` / `WID_SY_DISPATCH` 的 disabled 去掉 `sel_yard_valid`（保留 `can_edit`），
  `WID_SY_SHARED_SEL` 在整站下仍禁用（共享回落对整站无意义，C-4）；
- 语义风险：整站检修所 = **该站所有平台都不接货**，影响面比单场大（需玩家确认，C-3）；
- 未决：整站池与各场池是否要「合并显示」（C-3）。

### 5.11 KI-602 取证待办（尚未完成）

命令层拒绝面已列全（§5.2），还差两条实证：
1. `src\r3r_schedule_gui.cpp` 的 `OnClick` 是否真的把行号解析成列车（行高/`GetScrolledRowFromWidget` 已初判一致，§5.2 末）；
2. 玩家点的「分配调度」到底是**排程池窗口的「分配」**（`AssignR3RSchedule`）还是**车站调度池的坑位 +/-**
   （`R3RDispatchPoolSetLimit`）——两者是完全不同的命令，必须先分清（C-6）。
   若是前者：`:4302` 的 `IsR3RFollower()` 会拒掉「正在跑池表」的车；
   若是后者：坑位（`limit`）只在 `Pick` 时按 `issued < limit` 判定，加坑位本身不"接线"。

**取证 1 已完成（第 420 轮）**：`r3r_schedule_gui.cpp` 的 `R3RScheduleAssignWindow::OnClick`
（`:630-649`）用 `this->scroll->GetScrolledRowFromWidget(pt.y, this, WID_SCHED_AS_LIST,
WidgetDimensions::scaled.framerect.top)` 取行号 → `this->rows[sel]` → `Vehicle::GetIfValid()`
→ `Command<Commands::AssignR3RSchedule>::Post(v->index.base(), ol->index.base())`。
行高来自 `WID_SCHED_AS_LIST` 的 `nested_widget->resize_y`（`:582-583`），与
`OnResize()` 的 `scroll->SetCapacityFromWidget(...framerect.Vertical())` 用的是同一个度量
⇒ **行号 ↔ 列车映射正确，不存在「点 A 车却分配给 B」的错位**。
另外 `:639-642` 在 `v == nullptr || ol == nullptr` 时是 `this->Close()`（不是静默 break），
`:645` 在 `company != _local_company` 时静默 return —— 后者是"点了没反应"的唯一候选，
但它只在**看别人的排程**时成立。⇒ KI-602 的拒绝面只剩命令层（§5.2），不在 GUI 解析。
取证 2（C-6 分不清「分配」指哪个按钮）仍需玩家一句话。

### 5.12 KI-602 取证 2 已完成（第 420 轮续，三条候选路径的完整拒绝面）

**关键新事实：全语言文件里根本没有叫「分配调度」的按钮。**
`src\lang\simplified_chinese.txt` 全文搜「调度/排程」，含「分配调度」字样的只有三处，全是**订单标记**：

| 行 | 键 | 文本 |
|---|---|---|
| 4009 | `STR_ORDER_R3R_WAIT_DISPATCH` | `等待分配调度` |
| 4014 | `STR_ORDER_R3R_WAIT_DISPATCH_SUFFIX` | `（等待分配调度）` |
| 4006 | `STR_ORDER_R3R_SEPARATION_TOOLTIP` | 说明里提到「等待分配调度」 |

⇒ 玩家说的「分配调度」极可能不是排程池的「分配」，而是**订单窗口里那个标记**（其所在按钮标题是
`STR_ORDER_R3R_SEPARATION` =「调节班次」，下拉第二项才是「等待分配调度」）。因此 C-6 需扩成三选一。

**候选 A：订单窗口 `WID_O_R3R_SEPARATION`（标题「调节班次」，下拉第 2 项 = 等待分配调度）**
- 控件定义 `order_gui.cpp:5004-5005`：`NWID_BUTTON_DROPDOWN`，`SetMinimalSize(30, 12)`，`SetStringTip(STR_ORDER_R3R_SEPARATION, ...)`。
- `OnClick` `:3772-3785`：`sep_order == nullptr || !sep_order->IsType(OT_GOTO_STATION)` ⇒ **直接 `break`，零提示、零反馈**
  （`:3779`）。这是"点不动"最强的静默候选：只要当前选中的不是「前往车站」订单（例如前往车库/路点/等待挂接/解挂），
  点该按钮完全无反应。
- `OnDropdownSelect` `:4364-4379`：同样的 `:4366` 静默 `break`。
- 命令层 `CmdModifyOrder`：前置白名单 `order_cmd.cpp:2611` **已含** `MOF_R3R_STATION_SEPARATION` 与 `MOF_R3R_WAIT_DISPATCH`；
  `:3027-3037` 对 `MOF_R3R_STATION_SEPARATION` 有冲突守卫
  `if (data != 0 && R3RSeparationConflictOnOrders(v)) return CommandCost(STR_ERROR_SEPARATION_MUTUALLY_EXCLUSIVE);`
  （有红字提示，非静默）；`:3039-3046` 对 `MOF_R3R_WAIT_DISPATCH` **无**该守卫。
- 位存储 `order_base.h:676-715`：bit 5 / bit 6 独立；setter 走 `GetXFlagsRef()` → `CheckExtraInfoAlloced()`
  （`:161-175`）自动分配 extra，**不会因 `extra == nullptr` 失败**。⇒ 写入层健康，排除"标志写不进"。
- 已知脆弱点（不是当前 bug）：`OnDropdownSelect` 用的是列表**行索引** `index` 而非 item 的 `value`。
  当前两项恰为 0/1 且顺序固定 ⇒ 正确；若将来增删项必须同步 value，否则静默错位。

**候选 B：排程池窗口的「分配」（`Commands::AssignR3RSchedule`）**
`CmdR3RAssignSchedule`（`order_cmd.cpp:4286-4355`）拒绝面 6 条：
1. `:4288-4289` 车辆/表 ID 无效；
2. `:4298` `!IsCompanyBuildableVehicleType(v) || !v->IsPrimaryVehicle()` —— **段头/隐藏段子行（非 primary）必拒**；
3. `:4300` `CheckOwnership`；
4. `:4302` `v->r3r_orders_borrowed || v->orders_backup != nullptr || v->IsR3RFollower()` ——
   **刚耦合过/排程被停放/正在跟随的车必拒**（这是最常见的现场）；
5. `:4307` `!R3RScheduleAccessibleBy(list, v->owner)` —— 别人的私有排程；
6. `:4313` `first_shared->type != v->type` —— 排程类型不一致（如汽车表分配给列车）。
以上全部是裸 `CMD_ERROR`（无字符串），UI 显示的只有 `Post(STR_ERROR_CAN_T_DO_THIS, ...)` 通用「不能执行这个命令」。
GUI 侧已排除行映射错位（§5.11 取证 1）。

**候选 C：调度池坑位（`Commands::SetR3RDispatchPoolLimit`）**
`CmdR3RSetDispatchPoolLimit`（`station_cmd.cpp:2771-2786`）：`st == nullptr || owner == OWNER_NONE` → CMD_ERROR；
`yard > st->R3RNumYards()` → CMD_ERROR；`CheckOwnership`；`R3RDispatchPoolSetLimit` 失败 →
`STR_ERROR_R3R_DISPATCH_POOL_NO_SUCH_ENTRY`。**命令层干净，且"改坑位"本身就不接线**（`limit` 只在 `Pick` 时按 `issued < limit` 判定）。

⇒ 结论：KI-602 的"点不动"若指候选 A，则是**设计上的静默早退**（订单类型不符）；若指候选 B，则是命令层 6 条拒绝之一（有通用红字）。
需玩家答 C-6（已改为三选一）+ 若方便提供 `build\R3R_debug.log`（`R3R` 侧无该命令的探针，但可看 `ORD-` 系列有没有被写）。

## 六、实现落地

### 6.1 KI-598（订单「前往车站」的两个标记 → 文本后缀）已实现

文件：`src\order_gui.cpp`、`src\lang\english.txt`、`src\lang\simplified_chinese.txt`。

- 按钮不再变按下态：`sep_btn->SetLowered(false);`（两标记都只用文本表达）。
- `OnClick` 主区与箭头**统一只弹下拉**（不左键直接切换、不右键第二套语义）：
  ```cpp
  DropDownList list;
  list.push_back(MakeDropDownListCheckedItem(sep_order->HasR3RStationSeparation(), STR_ORDER_R3R_SEPARATION, 0, false));
  list.push_back(MakeDropDownListCheckedItem(sep_order->HasR3RWaitDispatch(), STR_ORDER_R3R_WAIT_DISPATCH, 1, false));
  ShowDropDownList(this, std::move(list), -1, WID_O_R3R_SEPARATION, 0, {}, DDSF_SHARED);
  ```
- `OnDropdownSelect` 改**互斥**：选 A 清 B；再选同一项即取消（与玩家「两者互斥」口径一致）。
- 文本后缀：`OT_GOTO_STATION` 的订单描述统一在 `order_gui.cpp` 拼行处（订单窗口与时刻表共用同一行生成）追加，
  与 `STR_ORDER_WAIT_TO_UNBUNCH` 同风格：
  ```cpp
  if (order->HasR3RStationSeparation()) AppendStringInPlace(line, STR_ORDER_R3R_WAIT_SEPARATION_SUFFIX);
  else if (order->HasR3RWaitDispatch())  AppendStringInPlace(line, STR_ORDER_R3R_WAIT_DISPATCH_SUFFIX);
  ```
- 新增串（带前导空格）：`STR_ORDER_R3R_WAIT_SEPARATION_SUFFIX` = `{SPACE}(Wait for auto sep.)` / `（等待调节班次）`；
  `STR_ORDER_R3R_WAIT_DISPATCH_SUFFIX` = `{SPACE}(Wait for dispatch)` / `（等待分配调度）`。
- flag 位（bit5/bit6）与 `ModifyOrder(MOF_R3R_*)` 通道不变 ⇒ **无存档格式变更**。
- 同步改 `STR_ORDER_R3R_SEPARATION_TOOLTIP` 文案，说明「互斥、再点取消、状态看命令文本后缀」。

### 6.2 KI-601（调度池显示排程名而非列车名）已实现

文件：`src\station_gui.cpp`。新增两个文件内 static 辅助：

- `R3RDispatchScheduleLabel(VehicleID host)`：优先取该模板车**排程名**（`orders->GetName()`），空则回退车名；
- `R3RDispatchPickerLabel(const Vehicle *v)`：`调度名（车名）`，两者同名时只显示一次。

三处消费点统一改用它：
1. 池列表行（原 `host_name = 车名`）；
2. 「添加调度」下拉（原 `v->name`）；
3. 删除确认框（原 `v->name`）。

### 6.3 KI-599（整站也能管调度池 / 检修所）已实现，**存档升 v7 而非 v6**

> 口径修正：§5.10 写的「SYRD 从 v5 升 v6」已过时 —— v6 早在第 416 轮就被**各场调度池**
> 占用（`SYRD_V6_MAGIC = 0xFFFFFF05`）。本轮因此新增 **v7**（`0xFFFFFF06`）。

数据层（`src\station_base.h` / `src\station_cmd.cpp`）：

- `Station` 新增 `std::vector<R3RStationYard::R3RDispatchPoolEntry> r3r_whole_pool{};` 与 `bool r3r_whole_workshop = false;`；
- 新增 `R3RDispatchPoolRef(uint16_t yard)`（const / 非 const 两版）：
  `R3R_YARD_NONE` ⇒ `&r3r_whole_pool`；越界 ⇒ `nullptr`；否则 `&r3r_yards[yard-1].pool`；
- `R3RDispatchPoolPrune / Count / Add / Remove / SetLimit / Pick` 全部改经 `R3RDispatchPoolRef` 取池；
- `R3RDispatchPoolAcquire / Release` 先扫各场池、再扫整站池（`= 整站池同样参与坑位收发`）；
- `R3RIsYardWorkshop(NONE)` 返回 `r3r_whole_workshop`；`R3RSetYardWorkshop(NONE, w)` 直接写该标志
  （整站没有「停满回落」语义，`shared_with` 不动）；
- `R3RIsWorkshopTile(tile)` 改为 `R3RIsYardWorkshop(R3RGetYardOfTile(tile))` ⇒ **没划进任何场的站台**归整站检修所；
- 三个命令（`SetR3RStationYardWorkshop` / `CmdR3RAddDispatchPoolEntry` / `Remove` / `SetDispatchPoolLimit`）
  去掉 `yard == R3R_YARD_NONE` 的拒绝，只保留 `yard > R3RNumYards()`；
  整站检修所的平台计数 = `R3REnumeratePlatforms()` 里 `R3RGetYardOfTile(t) == R3R_YARD_NONE` 的平台数。

存档（`src\sl\station_yard_sl.cpp`）：

- `SYRD_V7_MAGIC = 0xFFFFFF06`；
- 追加「整站块」在**所有场组之后**：`word 整站标志(bit0 = 检修所)` + `word 池条目数 P` + `P × 3 word`（与 v6 池编码完全相同）；
- 抽出 `R3RPackPool()` / `R3RUnpackPool()` 供 v6 各场池与 v7 整站池共用（解码端纯数值钳制，不查车辆池）；
- 稀疏写出条件放宽：`r3r_yards.empty() && r3r_whole_pool.empty() && !r3r_whole_workshop` 才跳过
  ⇒ 一个场都没建、只在整站池登记了模板车的车站也能存住；
- v6/v5/v4/v3/v2 全部向后可读（`has_whole` 仅在 v7 时置真；读档开头无条件清空整站字段）。

UI（`src\station_gui.cpp`）：

- 站场窗口 `OnPaint` 引入 `sel_ok = (sel_yard <= num_yards)`：`WID_SY_WORKSHOP` / `WID_SY_DISPATCH`
  改用 `sel_ok`（不再要求 `sel_yard_valid`）；`WID_SY_SHARED_SEL` / `RENAME` / `DELETE` 仍要求具体场；
- `SelectedYardIsWorkshop()` / `ToggleWorkshopOfSelected()` 去掉 `sel_yard == R3R_YARD_NONE` 的早退；
- 调度池窗口新增 `GetPool()`（整站也能取到池）；`Rebuild()` 不再把「整站」强制改写成 1 场；
  `DrawWidget` / 池行绘制 / `WID_SD_ADD`（disabled、OnClick、OnDropdownSelect）全部改走 `GetPool()`；
- 池窗口的场下拉**新增「整站」一项**（值 0），`YardLabel(NONE)` 返回 `STR_R3R_YARD_NAME_NONE`（即「(整站)」）
  ⇒ 从整站打开池窗口后还能切回整站，也不再出现「一个场都没建时下拉是空的」。

**仍待玩家确认（C-3）**：整站检修所的语义被实现为「只管没划进任何场的站台」；
若玩家希望的是「该站所有平台（含各场的）都不接货」，需要改为合并判定 + 合并收费。

### 6.4 仍未实现（等口径）

- **KI-600**：排程池「可删有主但未借用的排程」+「可直接编辑无主排程」——等 C-8（E-1/E-2/E-3 择一）。
  删除侧还要顺带处理 `r3r_orders_borrowed` 借用者，否则会留下悬垂 `OrderList*`（KI-93 家族）。
- **KI-602**：等 C-6 分清「分配调度」到底是 `AssignR3RSchedule` 还是 `R3RDispatchPoolSetLimit`，再补 §5.11 两条取证。
- **KI-597**：已定性为显示开关/环境问题（代码链路无回归），等 C-7 的三个现场问答。

## 七、构建自证（已回填，2026-11-11 18:36 完成）

本轮改动含 `src\station_base.h`（加字段）+ 两个 lang txt，按护栏规则必然退化为**全量重编**。

**护栏**：`build\R3R_incbuild.guard.log` 报 3 个 header/lang 文件新于最新 obj
（`src\station_base.h` 18:03:18、`english.txt` 17:54:08、`simplified_chinese.txt` 17:54:10），
判定 `REMOVED 621 object file(s) - upgrading this build to a FULL rebuild` ⇒ 本轮确为全量。

| 判据 | 读数 |
|---|---|
| 日志末行 | `[703/703] Linking CXX executable openttd.exe` |
| `build\R3R_incbuild.done` | `EXIT_CODE=0`（18:36:57） |
| 错误计数（`error C*` / `fatal error` / `FAILED:` / `build stopped`） | **0** |
| `build\openttd.exe` | **2026-11-11 18:36:34**，52 782 080 B |
| 源码 mtime（均早于 exe） | `station_base.h` 18:03:18 / `station_cmd.cpp` 18:01:00 / `station_gui.cpp` 18:02:13 / `order_gui.cpp` 17:53:43 / `sl\station_yard_sl.cpp` 17:59:44 / 两个 lang 17:54 |
| obj mtime（均晚于对应 .cpp、早于 exe） | `station_gui.cpp.obj` 18:32:17 / `station_cmd.cpp.obj` 18:32:13 / `order_gui.cpp.obj` 18:29:23 / `station_yard_sl.cpp.obj` 18:21:13 / `strings.cpp.obj` 18:32:36 |
| 新串入库（`build\generated\table\strings.h`） | `STR_ORDER_R3R_WAIT_SEPARATION_SUFFIX=0xC85`、`STR_ORDER_R3R_WAIT_DISPATCH_SUFFIX=0xC86`、`STR_R3R_DISPATCH_POOL_YARD_NONE=0xC95`、`STR_R3R_YARD_NAME_NONE=0xC5B` |
| `read_lints` | 0 |

**口径说明**：本轮只编了内测树 `build\`（探针 ON），未编发布树 `build-release\`。
发布树仍停留在本轮之前那一版，**不含** KI-598 / KI-599 / KI-601 的任何改动；
若需要发布版须另跑 `R3R_fullbuild.cmd`（发布树入口）后再验证。
