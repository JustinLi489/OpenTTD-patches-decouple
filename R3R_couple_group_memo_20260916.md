# R3R 挂接分组 · 玩家反馈与修复备忘（2026-09-16）

对象：`D:\sourcecode of JGRPP`（JGRPP 魔改 R3R）。本备忘记录玩家 09-16 反馈的 7 条、
逐条核实结论、修复顺序与待拍板项。工作区清单 `R3R_KNOWN_ISSUES.md` 同步登记 KI-53..KI-58。

---

## 0. 工作目录迁移（第 7 条）

玩家要求把本次对话的工作目录迁到 `D:\sourcecode of JGRPP`。

- **做不到的部分**：对话/IDE 的工作区由 CodeBuddy 侧决定，agent 无法用工具切换；
  需要玩家在 IDE 里手动 `打开文件夹 → D:\sourcecode of JGRPP`（新会话即可在新工作区展开）。
- **已经做到的部分**：本轮起所有读写一律使用 `D:\sourcecode of JGRPP\...` 绝对路径，
  备忘/清单也统一落在该目录根部（`R3R_*.md`），不再依赖沙箱目录。
- 沙箱目录里只保留历史脚本（`_ki_*.ps1` 等），新工作不再产生文件在那里。

---

## 1. 玩家反馈原文（09-16 03:23 抄录）

1. 那三个按钮确实出现了。
2. 行首确实显示了段N，不过是齐刷刷的段1，而且下面还有一个 invalid parameter 1/1，不知道是显示什么。
3. 确实没有红字报错了。
4. 我希望管理挂接分组能够像管理路签一样放在列车管理列表。
5. 我希望列车能够同时属于不同的路签分组。
6. 我看到挂接分组的界面了，不过新建分组怎么和别的一样是灰的。
7. 能不能把这个对话的工作目录迁移到 D:\sourcecode of JGRPP。

---

## 2. 逐条核实结论

### 2.1 第 2 条：`invalid parameter 1/1` 是真 bug；「齐刷刷的段1」是语义问题

**(a) `invalid parameter` 根因（已定，真 bug）**

OpenTTD 语言串里 `{STRING}` 的参数必须是 **StringID**，而裸字符串要用 `{RAW_STRING}`：

- `src/strings.cpp` 的 `case SCC_STRING:` → `StringID string_id = args.GetNextParameter<StringID>();`
- `{RAW_STRING}`（`SCC_RAW_STRING_POINTER`）才走 `GetNextParameterString()`。

我们在下面这些串上直接传了裸字符串（`std::string` / `const char*`），
参数类型不匹配 ⇒ 线程内取参数抛 `std::out_of_range` ⇒ 被 `src/strings.cpp`（`FormatString` 的
`catch (std::out_of_range&)`）兜住，把 `(invalid parameter)` 写进输出，后续 `{NUM}` 照常渲染，
于是车库链状态第二行显示成 `(invalid parameter) 1/1`：

| 语言串（英/简各一份） | 现有写法 | 调用点 |
|---|---|---|
| `STR_DEPOT_CHAIN_GROUP_OWNER` | `{STRING} {NUM}/{NUM}` | `depot_gui.cpp:555`（传 `std::string name`） |
| `STR_COUPLE_GROUP_LIST_ITEM` | `{STRING}（{NUM} 个段）` | `couple_group_gui.cpp:255`（传 `cg->name`） |
| `STR_COUPLE_GROUP_QUERY_DELETE` | `确定要删除挂接分组“{STRING}”吗…` | `couple_group_gui.cpp:342`（传 `CoupleGroup::Get(id)->name`） |
| `STR_VEHICLE_INFO_COUPLE_GROUP` | `{BLACK}挂接分组：{LTBLUE}{STRING}` | `vehicle_gui.cpp:3213`（`GetCoupleGroupDisplayName()` 返回 `std::string`） |

另有 `depot_gui.cpp:935` 用 `GetString(STR_DEPOT_CHAIN_GROUP_OWNER, std::string(), …)` 量宽度，
同一处要一起改成 `{RAW_STRING}`（调用点不用改，改的是语言串的格式码）。

修法：以上 4 个串的 `{STRING}` 全部改成 `{RAW_STRING}`（英文 + 简体中文各一份）。
改语言 txt 后必须让 `strings.cpp` 重编（否则 exe 内版本号与新 `.lng` 不匹配 → 启动报
`No available language packs`，见 KI-15/16）。

**(b) 「齐刷刷的段1」不是 bug**

第一行 `STR_DEPOT_CHAIN_SEGMENTS :段：{NUM}` 显示的是**整条链的段数**，不是"第几段"：
`R3RGetChainScheduleOwner()`（`couple_group.cpp`）从链头沿 `Next()` 数
`t == head || t->IsSegmentFront()`，链头无论有没有 ★ 都算一个段首。

玩家最近一次会话（`build/R3R_perf.log` 08:44 段）末行：`chains=2 vehs=9 maxChain=6 segs=2`
⇒ 世界上只有 2 个带 ★ 的车，两条链各 1 个 ★ 且都在链头 ⇒ 每条链只算 1 段
⇒ 所有格子都显示 `段：1`，第二行的 `无分组 1/1`（组名 + 归属段 k/N）也自洽。

**待玩家拍板的口径**（第 2 条前半的处理方式）：
- 方案 A（推荐，最小）：第一行改成"本行所属段序号 / 链内段数"，如 `第 1/1 段`，散链仍是 `散链`；
  这样多段链里各格数字会随位置变化，不会再出现"齐刷刷"的困惑。宽度预算同步改
  `depot_gui.cpp:928-935`。
- 方案 B：保持 `段：N`（链内段数）不改，只把第二行修好并加 tooltip 说明。

### 2.2 第 6 条：「新建分组」置灰 —— 已有修复，待新 exe 复测

- 置灰判定：`couple_group_gui.cpp` 的 `OnPaint()`：
  `SetWidgetDisabledState(WID_CG_NEW, !Company::IsValidID(this->owner));`
- 构造函数里已有回退分支（注释即写"depot tile 的 owner 可能不是公司，否则所有按钮都会置灰、
  也列不出任何分组"）：owner 无效时回退到本地公司。
- 证据：`build/R3R_debug.log` 最近会话探针 `CG-WIN … owner_valid=1`（窗口构造处打点）
  ⇒ **当前 exe（04:08 编译）下 WID_CG_NEW 应为可用**。玩家这条描述应来自 02:28 旧 exe 的
  03:23 会话。
- 状态：**已修，待复测**。若新 exe 下仍灰：下一步查 `OnPaint` 是否还有别的 disable 分支、
  以及 `ShowCoupleGroupWindow(owner)` 的 owner 传参链（车库工具按钮传的是 depot 的 owner）。

### 2.3 第 4、5 条：新需求（待实现）

**第 5 条：一个段可同时属于多个挂接分组**

现状：段 → 单一分组（`Vehicle` 上的 `couple_group` / `CoupleGroupID`），
`CmdAddCoupleGroup` 是"赋值"语义，重复指派会顶掉旧组。
改造方向：
- 数据模型：段的"所属组"由单值改为**集合**（`std::vector<CoupleGroupID>` 或位掩码；
  组上限需定，位掩码方案建议 16 组）。NOSAVE 语义与 KI-01 一致（读档尽力重建）。
- 命令层：`CmdAddCoupleGroup` / `CmdRemoveCoupleGroup` 改"加入/移出集合"，
  重复加入要幂等、去掉最后一组才回到"未分组"。
- 判定层：白名单 `R3RCoupleGroupsAllowCouple()`（同组才允许挂接）改为"**交集非空**即允许"；
  交集为空时视同"没有等待挂接的列车"（原地等待、可继续找别的候选、不刷屏、不死循环）。
- UI：分组窗口右侧段列表、`vehicle_gui` 的车辆信息行、车库链状态列都要能显示多组。

**第 4 条：挂接分组管理放进列车管理（列车列表）窗口**

- 目标形态：像现有"列车分组"那样，在列车列表窗口里有入口（下拉/管理按钮 → 管理窗口），
  不是只在车库工具行里放一个按钮。
- 复用与不复用：**复用 UI 形态（滑动列表 + 新建/改名/删除/指派），不复用 `Group` 体系**
  （挂接分组是独立主体，避免与路签/列车分组混用）。
- 落点预估：`vehicle_gui.cpp` 的 `VehicleListWindow` 工具栏（分组下拉处）加一个"挂接分组"入口；
  点击后打开 `couple_group_gui.cpp` 的窗口（可带 list-window owner 以便选中段联动）。

---

## 3. 执行顺序（逐步，每步独立可编译/可验证/可回退）

| 步 | 内容 | 状态 |
|---|---|---|
| S1 | 本备忘 + `R3R_KNOWN_ISSUES.md` 登记 KI-53..KI-58 | 第 1 轮完成 |
| S2 | 语言串 `{STRING}`→`{RAW_STRING}`（英/简）4 处 + 追 `strings.cpp` 增量重编 | 第 1 轮完成 |
| S3 | 车库链状态第一行改「本行段序号 / 链内段数」（方案 A） | 第 2 轮完成 |
| S4 | 挂接分组多组归属（第 5 条：位掩码 + 命令 + 判定 + UI） | 第 2 轮完成 |
| S5 | 列车列表窗口的挂接分组管理入口（第 4 条） | 第 2 轮完成 |
| S6 | 第 6 条复测 + 第 7 条工作目录迁移（玩家侧操作） | 待玩家复测 |

---

## 4. 环境与构建注意（沿用既有教训）

- 改任意 `src/*.h` ⇒ 必须全量重编（本机 ninja 头依赖跟踪失效，见 KI-15/16 与记忆 66636022）。
- 改 `src/lang/*.txt` ⇒ strgen 会重生成 `build/generated/table/strings.h`，必须让
  `strings.cpp` 重编（`touch src/strings.cpp`），否则 `.lng` 版本不匹配 → 启动
  `No available language packs (invalid versions?)`。
- 只改 `*.cpp` / `*.txt` 时增量构建足够；exe 被游戏占用会 `LNK1168`，重编前先退游戏。
- 日志探针：`build/R3R_debug.log`（事件）、`build/R3R_perf.log`（每 128 帧一行聚合）。

---

## 5. 第 2 轮实现（S3 / S4 / S5，2026-09-16）

### 5.1 S3 车库链状态第一行 = 本行段序号（方案 A，已落地）

- 新语言串 `STR_DEPOT_CHAIN_SEGMENT_POS`：英 `Segment {NUM}/{NUM}`、简 `第 {NUM}/{NUM} 段`
  （原 `STR_DEPOT_CHAIN_SEGMENTS :段：{NUM}` 删除，全代码库无残留引用）。
- 新 API `R3RGetSegmentPosition(v, &index, &total)`（`couple_group.cpp`）：
  `index` = **本行车辆所属段**的 1-based 序号，`total` = 链内段数。
  实现先用 `R3RGetCoupleGroupCarrier(v)` 找到本行所在段的段首，再沿链数段首并记下位置。
- `depot_gui.cpp::DrawChainStateTag()`：参数名 `head`→`v`（它本来就是"本行的车"，
  不是链头，旧名有误导）；第一行散链仍是 `散链`，分段链改画 `第 self/总 段`。
- 第二行 `STR_DEPOT_CHAIN_GROUP_OWNER` 文案加说明：英 `{RAW_STRING} (owner {NUM}/{NUM})`、
  简 `{RAW_STRING}（归属 {NUM}/{NUM}）`，回答玩家"不知道是显示什么"。
- 宽度预算同步改 `depot_gui.cpp` 的 `UpdateWidgetSize`（`STR_DEPOT_CHAIN_SEGMENT_POS` 两个 `{NUM}`）。
- 备注：若玩家更想要旧的「链内段数」口径（方案 B），只需把这一处 `GetString` 换回段数即可回退。

### 5.2 S4 一个段可同时属于多个挂接分组（位掩码，Q5）

**数据模型**：`Vehicle::couple_group`（`CoupleGroupID`）→ `Vehicle::couple_groups`
（`CoupleGroupMask = uint64_t`，一位一组）。为什么是 64 位而不是"16 组"或 `std::vector`：

- 组池是 `Pool<CoupleGroup, CoupleGroupID, 64>`（`couple_group.h`）⇒ 活着的组 ID 恒在 `0..63`，
  64 位正好一一对应，不会再有"上限不够"的隐患；`std::vector` 则会显著抬高每车内存
  （世界可有上万节车）并让存档/比较变复杂。

**新 API（`couple_group.h/.cpp`）**

| 函数 | 语义 |
|---|---|
| `R3RCoupleGroupBit(id)` | `id` 的单比特；`INVALID_COUPLE_GROUP`/越界 → 0 |
| `R3RGetCoupleGroupsOfSegment(v)` | 段的分组集合（**对本段所有车取并集**后再剔除失效位） |
| `R3RAddCoupleGroupToSegment(v,id)` | 加入（幂等） |
| `R3RRemoveCoupleGroupFromSegment(v,id)` | 移出该组，其余组保留 |
| `R3RClearCoupleGroupsOfSegment(v)` | 全部移出 → 回到隐式组 |
| `R3RCoupleGroupMasksCompatible(a,b)` | 白名单判定：两边都空 ⇒ 相容；一边空 ⇒ 不相容；否则交集非空即相容 |
| `R3RCountSegmentsInCoupleGroup(id)` | 该组的段数（`INVALID` 时数未分组段） |
| `R3RCountCoupleGroups(mask)` / `R3RGetCoupleGroupsNameList(mask)` | UI 用：组的个数 / 名字用 `+` 连接的列表 |
| `R3RGetSegmentPosition(v,&i,&t)` | 见 5.1 |

**"分组随段走"的关键补强（本来会丢）**：分组值写在段首车上，但**段首车会变**
（挂接折叠修正 `R3RFlipChainBySegments` 会把 ★ 迁到反转后的新段首）。原单值实现没管这件事，
值会留在"旧段首"（翻转后成了段尾）上，段就读不到了。本轮改为：

- **读**：`R3RGetCoupleGroupsOfSegment()` 对本段所有车取并集。段的车厢集合在挂接翻转中不变
  （只变段内顺序），所以并集语义天然免疫"段首变更"。
- **写**：先 `R3RNormaliseCoupleGroupsOfSegment()` 把本段的组并到段首车、其余车清零，
  再改段首车。这样"移出/清空"是确定性的，也不会让值在段内扩散。
- 新增 `R3RNextSegmentVehicle()`（遇下一辆 ★ 即止）作为段内遍历原语。

**命令层**：`CmdSetCoupleGroup` 语义由"赋值/置空"改为"**加入**"（`INVALID_COUPLE_GROUP`
参数直接 `CMD_ERROR`）；新增 `CmdRemoveCoupleGroup(vehicle, group)`，
`command_type.h` 里 `RemoveCoupleGroup` 追加在 `SetCoupleGroup` 之后（不动既有枚举值）。
UI 的"移出所选段"改用 `Commands::RemoveCoupleGroup`，不再用"置空"表达。

**存档**：`CGVR` 稀疏表的字段 `group`(SLE_UINT16) → `groups`(SLE_UINT64)
（`sl/couple_group_sl.cpp`）。缺少值不再存。**后果：字段名变了，旧存档读进来不会被 crash，
但所有段会回到"未分组"**（表式加载对文件中不存在/不认识的字段是跳过 + 保默认值）。
这是一次性代价，已在 KI-59 登记；本机测试存档可重新指派。

**UI 同步**：分组窗口右栏改按"该组 ∈ 段的集合"筛选；段行尾追加 `STR_COUPLE_GROUP_SEGMENT_ALSO_IN`
（英 `also in: {RAW_STRING}`／简 `同属 {RAW_STRING}`）显示该段的其他组；
`vehicle_gui` 车辆信息行、车库链状态第二行都改用 `R3RGetCoupleGroupsNameList()`（多组显示为 `A+B`）。

### 5.3 S5 列车管理列表里的挂接分组入口（Q4）

- `vehicle_gui_base.h`：`ActionDropdownItem` 末尾追加 `ADI_COUPLE_GROUP_MGMT`（不改既有值）。
- `vehicle_gui.cpp`：`BuildActionDropdownList()` 在"追踪限制槽/计数器"之后追加
  `STR_VEHICLE_LIST_MANAGE_COUPLE_GROUPS`（英 `Manage couple groups`／简 `管理挂接分组`），
  仅 `vli.vtype == VehicleType::Train` 时出现（挂接分组是列车专属概念）；
  `GetActionDropdownSize()` 同步量宽度。
- 两个宿主窗口都接：`VehicleListWindow`（`vehicle_gui.cpp`）与 `CompanyGroupWindow`（`group_gui.cpp`）
  的 `OnDropdownSelect` 加 `case ADI_COUPLE_GROUP_MGMT: ShowCoupleGroupWindow(this->owner);`。
- 与"管理路签"并列 ⇒ 玩家第 4 条"像管理路签一样放进列车管理列表"达成。

### 5.4 构建

改动了 `vehicle_base.h` / `vehicle_gui_base.h` / `command_type.h` / `couple_group*.h`
⇒ 按 KI-15/16 必须**全量重编**。本轮用一次性脚本 `_tmp_full_build.cmd`
（删全部 `build\*.obj` → `TEMP/TMP` 指到 `build\tmp` → `vcvars64` → `ninja -C build -j2 openttd`），
日志 `build\R3R_aibuild.log`、完成标记 `build\R3R_aibuild.done`（内含 `EXIT_CODE=`）。
`-j2` 是因为本机空闲内存只有约 380 MB（KI-24 同因）。

---

## 6. 第 3 轮（第 25 / 26 轮）：硬约束统一闸门 + 段身份语义（KI-60 / 61 / 62）

### 6.1 触发（玩家实测）

| # | 现象 | 判定 |
|---|---|---|
| a | 车库内机车**直接耦合**了被玩家停住（`Stopped`）却持有 `WAIT_COUPLE` 的车底 | 真 bug（KI-60） |
| b | 机车耦合了**同挂接组、但没有等待挂接命令**的车底 | 真 bug（KI-60） |
| c | 车底段在车库内被解挂后，车库列表显示为**散链**，之后机车再也挂不上它 | 真 bug（KI-62） |
| d | 机车仍把「没有 `WAIT_COUPLE` 的纯车厢段」规划为寻路目的地（白跑一趟后才被闸门拒绝） | 连带遗留（KI-61） |

共同根因：**被动方（等待被挂的车底）此前完全无人把关**。三条被动方候选解析路径（`GetCouplePosition` / depot 瓦片扫描 / 开阔轨道 8 邻域扫描）的判据都是
`R3RIsCarOnlyFormation(u) || u->current_order.IsType(OT_WAIT_COUPLE)` —— 「纯车厢段」短路**整体豁免了 `WAIT_COUPLE` 要求**，
且三条路径都**不检查双方的停止状态**；主动方在 `train_cmd.cpp:9892`（`Stopped && cur_speed == 0`）确实被挡，被动方却无人把关。
玩家给出的期望即闸门四条件：**双方均须启动、一方 `GOTO_COUPLE`、一方 `WAIT_COUPLE`、挂接分组相同**。

### 6.2 落地

| 项 | 落点 | 内容 |
|---|---|---|
| 统一硬闸门 | `train_cmd.cpp` `R3RCanCoupleNow(coupler, target, site, why*)`（`~5915`，置于 `GetCouplePosition` 之前） | 四条件：主动方 `OT_GOTO_COUPLE` / 被动方 `OT_WAIT_COUPLE`（**取消 car-only 豁免**）/ 双方 `!vehstatus.Test(VehState::Stopped)` / `R3RCoupleAllowed` 分组白名单。拒绝原因 `target-not-segment` · `target-not-wait` · `stopped` · `group`；探针 `CPL-GATE`（边沿触发，tag `R3REDGE_COUPLEGATE`），打印双方 order / stop / group 状态 |
| 三条解析路径接线 | `GetCouplePosition`（`site="geo"`，`~5978`）、depot 邻域扫描（`"depot"`，`~6047`）、邻车扫描（`"scan"`，`~6077`） | 全部改走闸门；被拒候选**等同「此处没有等待挂接的车底」** ⇒ 继续找别的候选，都没有则原地待命走常规 `COUPLE-FAIL`，不刷屏、不死循环 |
| 段身份谓词单点 | `train.h:694` 声明 / `train_cmd.cpp:1826` 定义 `R3RIsCoupleTarget(t)` | `= t->IsSegmentFront() && t->current_order.IsType(OT_WAIT_COUPLE)`。四处共用：站台预留扫描（`yapf_destrail.hpp:374`）、`PfDetectDestination`（`:448`，旧 car-only 短路已删除）、back-walk（`yapf_rail.cpp:156`，`fail=notWC` → `fail=notSeg` 并加打 `sf=`）、到达闸门（`reject=target-not-segment`） |
| 段身份保持 | `train_cmd.cpp` `DecoupleTrain` 末尾（`~4620`） | 原 `u->ClearSegmentFront();` 改为 `if (R3RIsCarOnlyFormation(u)) u->SetSegmentFront();`。**★（`SegmentFront`）是全代码库判定「链是段」的唯一依据**（`depot_gui.cpp:542-546`：必须看段首标记，不能看段数），清掉即退化为散链、既被显示成「散链」也不再是合法挂接目标 |

**保留 ★ 的副作用核对（全部安全）**：`GetSegmentHeadFromRear`（`train_cmd.cpp:3732`）、`GetSegmentBoundaryFromHead`（`:3759`）、
`R3RGetSegmentHeads`（`:3821`）均从 `GetNextVehicle()` 起遍历 ⇒ 链头 ★ 天然跳过；`R3RFlipChainBySegments`（`:5065`）的段收集带
`w != chain` 排除链头；`CmdDemoteSegment` 仍可正常降级。

### 6.3 构建与一致性核对（步骤 7 收口）

- 改动触及 `src/train.h`、`src/pathfinder/yapf/yapf_destrail.hpp` ⇒ 按 KI-15 必须**全量重编**（本机 ninja 跟踪不到头依赖）；
  用一次性脚本 `_tmp_full_build.cmd`（`tasklist` 查 `openttd.exe` → 删全部 `build\**\*.obj` → `TEMP/TMP` 指 `build\tmp` → `vcvars64` → `ninja -C build -j2 openttd`）。
- 结果：`build\R3R_fullbuild.log` 末行 `[691/691] Linking CXX executable openttd.exe`；`build\R3R_fullbuild.done` = `EXIT_CODE=0`。
- **KI-15 合规判据（时间戳关系）**：最新源码 `train_cmd.cpp` 18:52:33 / `yapf_destrail.hpp` 18:52:27 / `yapf_rail.cpp` 18:51:59 / `train.h` 18:51:56；
  最早 obj `settingsgen.cpp.obj` 18:54:54 ⇒ **源码全部早于 obj**，无陈旧/混合对象。产物 `build\openttd.exe` @ 19:23:23 / 50 621 952 B。
- **exe 内含本两轮新增串**（`findstr` 实测）：`notSeg` · `target-not-segment` · `CG-PAINT` · `CG-RBG` 均**命中**；
  纯函数名 `R3RIsCoupleTarget` 不命中属正常（函数名不是字符串字面量）⇒ **测的那版 = 刚改的那版**。
- 文档沿革校正：KI-59 里提到的 `CG-PICK` 探针在当前源码中**已不存在**（`OnVehicleSelect` 直接干活、无探针），属命名沿革，非回归。

### 6.4 待复测（玩家侧，游戏内）

1. **段身份（KI-62，最高优先）**：车库内解挂车底段 → 车库列表该链应显示「第 k/N 段」而非「散链」；随后机车应能再次挂上它（**无需重新「设为段」**）。
2. **散链不可瞄准（KI-61）**：未升过段的纯车厢链旁，机车寻路不得以其为目的地（`build\R3R_debug.log`：`FSCP ... fail=notSeg ... sf=0`；若开到跟前，`CPL-GATE reject=target-not-segment`）。
3. **硬闸门（KI-60）**：① 被动方被玩家停住 ⇒ 不挂；② 被动方无 `WAIT_COUPLE` ⇒ 不挂；③ 双方已启动 + 一方 `GOTO_COUPLE` + 一方 `WAIT_COUPLE` + 分组相容 ⇒ 正常挂。三种情况均不得刷屏、不得死循环、不得锁死 depot。
4. **分组白名单（KI-45 系列）**：不同组的车底贴上 ⇒ 视同「没有等待挂接的列车」，原地等待或继续找同组候选。

### 6.5 已知遗留（不在本轮范围）

- **解挂侧仍不拒绝散链（KI-08）**：`GetSegmentHeadFromRear` 返回空时会回退原生 `GetDecoupleVehicleAuto` 逐车启发式，散链仍可能被 `DECOUPLE` 拆开（日志 `DECOUPLE-FIRE` 走此路径）。
- **`R3RCouplePairAllowed` 命名已并入 `R3RCoupleAllowed`**：设计稿 §10-2 承诺的「匹配判定单点」现为 `couple_group.cpp:162`（内部 `R3RCoupleGroupMasksCompatible` 交集判据），旧名全代码库 0 命中。

