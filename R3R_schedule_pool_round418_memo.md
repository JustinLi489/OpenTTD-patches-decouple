# R3R 排程池窗口 5 项增强（第 418 轮，2026-11-10）

状态：**已实现（5 项增强 + C-5 两条通道），编译中**（2026-11-10）。需求原话、拍板、实现落点与两处构建阻塞见 §1~§7。

前置：第 417 轮的排程池（无主排程 + 指针分配）已实现并编译通过（`build\openttd.exe` @ 2026-11-10 10:41:46，52 738 048 B，`EXIT_CODE=0`），实机 6 条判据尚未复测。

## 1. 玩家原话（2026-11-10，逐条照录）

1. 管理排程的页面，能够重命名排程名称，也能编辑排程内容
2. 管理排程的页面，「分配」按钮的旁边有一处镂空，导致 GUI 显示紊乱
3. 管理排程的页面，我希望能够像预览共享调度计划列车的窗口一样能够预览自有表指向这张表的段（也就是说，有列车图像预览）
4. 管理排程的页面，我希望能同时看到其他公司的排程（在名称前面加色块用于区分）
5. 管理排程的页面，我希望能执行设置排程公有或私有的操作。在自己的排程公有后，其它公司的车站也可以分配自己公有的排程，其它公司的列车的自有表也可以指向这张公有的表

玩家另附：「如果必要，你可以向我询问细节」。

## 2. 需求 2 初步定位（唯一已做实的一条）

用户所说「分配按钮旁边的镂空」= 按钮行宽度与列表行宽度不一致导致的空白。

`src\r3r_schedule_gui.cpp:44-72` 的控件树：

| 行 | 控件 | 宽度来源 |
| --- | --- | --- |
| `:53-55` | `WID_SCHED_TARGET_SEL` 下拉 | `SetMinimalSize(180,12)` + `SetResize(1,0)` + `SetFill(1,0)` ⇒ **随窗口拉伸** |
| `:57-60` | `WID_SCHED_LIST` + 滚动条 | `SetMinimalSize(360,130)` + `SetResize(1,10)` ⇒ 撑出窗口宽度 ≈ 360+滚动条 |
| `:62-69` | 按钮行 `NWID_HORIZONTAL( EqualSize( NEW 80 / DELETE 80 / ASSIGN 80 ) + RESIZEBOX )` | **三个按钮各 80，合计 240；外层容器没有 `SetResize`/`SetFill`** ⇒ 右侧留出约 120px 空白 |
| `:71` | `WID_SCHED_HINT` | `SetMinimalSize(360,30)` + `SetResize(0,0)` |

⇒ 空白来自按钮容器宽度恒定 240 而窗口被列表撑到 360+，`RESIZEBOX` 只占固定尺寸，中间那段没有任何控件填充。修法（待动工，仅改控件树，零行为变更）：给 `EqualSize` 容器加 `SetResize(1,0)` + `SetFill(1,0)`，让三按钮均分整行宽度；或在其右侧插 `NWidget(NWID_SPACER), SetFill(1,0)`（等价，但按钮仍 240 靠左）。

## 3. 需求 1/3/4/5 的现状与依赖（静态读码，未做实）

- `OrderList`（`src\order_base.h`）**没有**名称字段、没有 owner、没有 public 标志。第 417 轮的「无主排程」只靠 `first_shared == nullptr && num_vehicles == 0` 反推，池条目本身不存归属。⇒ 需求 1（重命名）、4（按公司区分）、5（公有/私有）全都要新增**排程元数据**，并决定存在哪里（见 C-2）。
- 需求 1 的「编辑排程内容」：订单窗口 `OrderWindow` 是绑定 `Vehicle` 的（`ShowOrdersWindow(Vehicle)`），没有"无宿主编辑 OrderList"的入口。
- 需求 3 的「预览」：现有可复用的图像预览入口是 `VehicleListWindow`（`VL_SHARED_ORDERS`，按 `first_shared` 编码，见 `src\vehiclelist.h:28`、`src\vehicle_gui.cpp:3264`）；**无主表没有 `first_shared`**，天然列不出。
- 需求 5 的跨公司分配：`CmdR3RAssignSchedule` 目前的守卫含 `CheckOwnership(v->owner)`、表主人类型匹配；车站调度池 `CmdR3RAddR3RDispatchPoolEntry`（第 416 轮）也有「模板车必须是活车、有自己的表」等闸门。放开跨公司需要新的授权判据，不是放宽 `CheckOwnership` 就完事（会把「只能改自己的车」这条通用安全线一起打穿）。

## 4. 待玩家拍板（编号回填用）

| 编号 | 问题 | 备选 |
| --- | --- | --- |
| C-1 | 「编辑排程内容」指什么？ | (a) 池窗口里内嵌只读订单列表，增删改走命令（工作量最大，要造"无宿主 OrderList"编辑通道）<br>(b) 复用现有订单窗口：选中表后，若它有控制列车就打开那列车的订单窗口编辑；无主表则提示「先分配给列车再编辑」（工作量最小、行为与既有完全一致）<br>(c) 只做只读预览，不开编辑 |
| C-2 | 排程元数据（名称/公司/公有标志）存哪里？ | (a) 独立稀疏块（像第 416 轮的 R3VP、SYRD 那样单开 chunk），**不动 ORDL 布局** ⇒ 旧档读回名字为空、默认私有，且旧版 exe 仍能读新档（推荐）<br>(b) 直接扩 `ORDL` chunk 并升版本 ⇒ 旧版 exe 读不了新档 |
| C-3 | 「预览段」用什么形态？ | (a) 加一个按钮打开现有 `VehicleListWindow`（有列车图像、可直接双击定位，但只对 `first_shared` 有效的表可用，无主表不可用）<br>(b) 在池窗口下半部内嵌一条横向车辆图像条（自绘缩略图，无主表也能显示"无人指向"），工作量中等<br>(c) 两个都要 |
| C-4 | 跨公司可见/可用的粒度 | (a) 私有=其它公司**完全看不到**（不满足需求 4 的"同时看到其他公司排程"）<br>(b) 私有=可见但灰显+不可分配；公有=可见且可分配（推荐）<br>(c) 私有=可见但只能看名字，命令内容也隐藏 |
| C-5 | 「其它公司的车站可以分配自己公有的排程」的确切含义 | (a) 车站调度池（第 416 轮的 `StationDispatchWindow`）里可以登记**别家公司公有**的排程当模板<br>(b) 只是指「别家公司的**列车**可以指向我的公有表」<br>(c) 两个都要 |

### 4.1 玩家拍板（2026-11-10，玩家原话：「四个问题的我的拍板分别是BAAA」）

| 编号 | 拍板 | 含义（本轮的实现口径） |
| --- | --- | --- |
| C-1 | **B** | 「编辑排程内容」= 复用现有订单窗口：选中一张表后，若它有控制列车就打开那列车的订单窗口编辑；无主表则提示「先分配给列车再编辑」。不新建"无宿主 OrderList 编辑通道"，不复制订单窗口代码。 |
| C-2 | **A** | 排程元数据（名称 / 归属公司 / 公有标志）走**独立稀疏块**，不动 `ORDL` 布局、不升存档版本：旧档读回名字为空、默认私有；新版存的档旧版 exe 仍能读。 |
| C-3 | **A** | 「预览指向这张表的段」= 复用现有车辆列表窗口（共享调度计划那套，带列车图像）。有主表用它的首车索引打开；无主表没有首车，按钮禁用并给提示。 |
| C-4 | **A** | 私有 = 其它公司**可见但灰显、不能分配**；公有 = 可见且可分配。 |
| C-5 | **A** | 公有后**两者都要**：①别家公司的列车可把自有表指向我的公有表；②别家公司的车站调度池可登记我的公有表当模板。 |

## 5. 影响面预判（动工前先记，避免漏改）

- 存档：新 chunk 或 ORDL 版本升级（取决于 C-2）。若走独立稀疏块，需在 `src\sl\` 注册 handler、`src\saveload\saveload_internal.h` 加 chunk id、`SaveLoad` 版本判断。
- 命令层：新增 `RenameR3RSchedule`、`SetR3RSchedulePublic`；改 `AssignR3RSchedule`（跨公司授权）、`CreateR3RLooseSchedule`（记 owner）、`DeleteR3RLooseSchedule`（仅 owner 可删）。
- 窗口：控件树加按钮（重命名/公有开关/预览），列表行加公司色块与名称前缀，`Rebuild()` 过滤逻辑按 C-4 改。
- 语言串：新增约 10 条 `STR_R3R_SCHEDULE_*`，中英双语同步；注意本 fork 的坑：`{VEHICLE}` 不能进 `{P ...}` 复数串（KI-586 复测记录第 2 条）、`src\widgets\*.h` 的 `///<` 注释必须纯 ASCII（KI-584）。
- 构建：改 `src\*.h` 会触发护栏清 obj 走全量（约 16~20 分钟），改 `src\lang\*.txt` 同样。

## 6. 复测判据（动工后填具体 6~8 条）

## 7. 实现落地（2026-11-10）

### 7.1 逐条需求落点

| 需求 | 落点 | 说明 |
| --- | --- | --- |
| ② 镂空 | `src\widgets\r3r_schedule_widget.h` | 按钮行 `NWID_HORIZONTAL` 加 `SetResize(1,0)`+`SetFill(1,0)`，控件 `CHECKBOX PUBLIC / NEW / DELETE / ASSIGN` 均分整行；新增按钮后行宽不再固定 240。 |
| ① 重命名 | `src\r3r_schedule_gui.cpp`（`WID_SCHED_RENAME` + `ShowQueryString` → `OnQueryTextFinished`）、`src\order_cmd.cpp` 的 `CmdR3RRenameSchedule` | 上限 32 字符（`QueryStringFlag::LengthIsInChars`），UTF-8 最坏 32×4=128 字节 = `R3R_SCHEDULE_NAME_MAX_BYTES`；回调用 `query_schedule`（`OrderListID`）定位排程，因为查询框期间池内索引可能变动。 |
| ① 编辑内容 | `WID_SCHED_EDIT` → `ShowOrdersWindow(first)` | 按 C-1=B：有控制列车才可点，无车（无主表）置灰。 |
| ③ 预览 | `WID_SCHED_PREVIEW` → `ShowVehicleListWindow(first)` | 按 C-3=A：复用共享命令车辆列表（`VL_SHARED_ORDERS`，自带列车图像）；自绘预览面板 `show_preview` / `PreviewText()` / `STR_R3R_SCHEDULE_PREVIEW_TEXT`、`..._PREVIEW_NONE` 全部删除，提示面板高度从 4 行收到 2 行。 |
| ④ 其它公司可见 + 色块 | `Rebuild()` 收集可见表（自家 + 公有），行首 `DrawRectangle` 公司色块 | 私有表仍对别家可见但灰显、不可分配（C-4=A）。 |
| ⑤ 公开开关 | `WID_SCHED_PUBLIC`（checkbox，`SetWidgetLoweredState` 反映 `IsR3RSchedulePublic()`）+ `CmdR3RSetSchedulePublic` | 仅 owner 可切。 |

命令 UI 文案：`Rename / Orders / Trains / Public`（`STR_R3R_SCHEDULE_RENAME/EDIT/PREVIEW/PUBLIC` 及各自 `_TOOLTIP`），中英同步；行文本去掉 `{BLACK}`，颜色改由 `DrawString(..., TC_WHITE)` 参数控制（`{BLACK}{RAW_STRING}` 组合不生效）。

### 7.2 C-5 的两条通道

- ① 别家列车把自有表指向我的公有表：`CmdR3RAssignSchedule` 的授权判据改为 `R3RScheduleAccessibleBy(ol, company)`（owner 或 public），不再单靠 `CheckOwnership(v->owner)` 这条"只能改自己的车"安全线。
- ② 别家车站调度池可登记我的公有表：`src\station_cmd.cpp` 的 `CmdR3RAddR3RDispatchPoolEntry` 在 `R3RDispatchHostUsable()` 之后新增闸门——模板车 owner != 车站 owner 时要求 `R3RScheduleAccessibleBy(host_orders, st->owner)`，否则返回新串 `STR_ERROR_R3R_DISPATCH_POOL_NOT_SHARED`（中英已加）。
  说明：该命令原先对"别家模板车"实际上是不设防的（只要 `CheckOwnership(st->owner)` 过、模板车可用就收），等于私有排程可被别家借走；本轮把它收紧成"必须公开"。

### 7.3 本轮撞到并修掉的两个阻塞缺陷

1. **命令枚举越界（严重，会让全游戏命令分发错乱）**：`src\command_type.h` 的 `enum class Commands : uint8_t` 在本轮新增两条命令后达到 `End = 256`，编译期 C4369（`'256' cannot be represented in 'unsigned char'`）⇒ `Commands::End` 回绕为 0（`IsValidCommand(cmd)` 恒 false）、`SetR3RSchedulePublic(256)` 回绕为 0 与 `BuildRailLong` 撞号。修法：底层类型改 `uint16_t`。安全性核对：`CommandContainer::Serialise` 原本就按 `uint16` 上线（`src\command.cpp`），存档不存 `Commands`，`EnumIndexArray<CommandInfo, Commands, Commands::End>` 随 underlying type 自动扩容 ⇒ 无二进制/网络格式变化。
2. **`SLE_STR` 存 `std::string` 编译失败**：`src\sl\order_sl.cpp` 的 `R3SM` 块里名称字段原写 `SLE_STR(R3RScheduleMeta, name, SLE_STR, 0)`，模板静态断言不过（`SlVarWrapper<std::string, ...>` 无对应特化）。正确写法 `SLE_SSTR(R3RScheduleMeta, name, SLE_STR)`（`SL_STDSTR`，表块可用）。

### 7.4 存档

新增 `CH_RIFF` 块 `R3SM`：每条有名字/公开状态的排程写 `OrderListID + name + flags`；读取时按 pool 存活过滤，名字超长按字节截断；旧档无该块 ⇒ 名字空、默认私有（C-2=A：不动 `ORDL`、不升版本）。

### 7.5 待复测（8 条）

1. 按钮行铺满，无镂空；窗口拉宽/缩窄后按钮均分、不被裁切。
2. 重命名：中文名（≤32 字）落盘不截断、列表立刻变字；清空名字回到「N orders」默认摘要。
3. 命令按钮：有控制列车时打开该表所属列车的订单窗口；无主表按钮灰。
4. 列车按钮：打开共享命令车辆列表且带列车图像；无主表灰。
5. 公开开关：勾上后别家公司能看到并分配该表；取消后立即变灰不可分配。
6. C-5②：别家公司尝试把「我的私有表列车」登记进自己的车站调度池 ⇒ 报 `该列车属于其它公司, 其排程未公开`；公开后可登记成功。
7. 存档往返：名字/公开标志/owner 全部保留；旧档（无 `R3SM`）读入名字为空、默认私有、不崩。
8. 负判据：别家公司点自家以外的私有表的重命名/公开 ⇒ 命令被拒且界面状态不变。

> 跟踪表见 `R3R_KNOWN_ISSUES.md` 第 418 轮小节末「复测清单（第 418 轮，待打勾）」；本轮构建完成时 8 条全部为「待复测」，实机跑完按行回填「通过 / 不通过 + 证据行」。
> 判据 6 的前置：`CmdR3RAddR3RDispatchPoolEntry` 的闸门只在 `模板车 owner != 车站 owner` 时触发，同公司登记不报错属预期。

### 7.6 构建自证（2026-11-10 23:34）

- 入口：`R3R_fullbuild.cmd inc detach`（复用既有入口，未新建任何 .cmd）。
- 第一次（全量 704 步）倒在 `src\sl\order_sl.cpp`：`SLE_STR` 存 `std::string` 模板静态断言（见 §7.3-2）；同时暴露命令枚举 `'256' cannot be represented in 'unsigned char'`（见 §7.3-1）。两处修完后第二次 `inc` 护栏判 `GUARD: incremental is safe`，`[101/101] Linking CXX executable openttd.exe`，`build\R3R_incbuild.done = EXIT_CODE=0`，日志 error C*/error LNK/fatal error/FAILED: 计数 0。
- 时间戳链：`src\r3r_schedule_gui.cpp` 23:25:01 → `r3r_schedule_gui.cpp.obj` 23:26:35 → `build\openttd.exe` 23:34:08（52 759 040 B）；`src\sl\order_sl.cpp` 22:56:30 → `order_sl.cpp.obj` 23:11:50 → exe 23:34:08。
- 语言串自证：`build\generated\table\strings.h` 含 `STR_R3R_SCHEDULE_RENAME=0xCB0`、`_EDIT=0xCB2`、`_PREVIEW=0xCB4`、`_PUBLIC=0xCB6`、`_UNNAMED=0xCB8`、`_RENAME_TITLE=0xCB9`、`STR_ERROR_R3R_DISPATCH_POOL_NOT_SHARED=0xC9A`，且 `_PREVIEW_TEXT`/`_PREVIEW_NONE` 已归零；`build\lang\{english,simplified_chinese}.lng` 22:59 重生成，中文包含「重命名/命令/列车/公开/未命名排程/该列车属于其它公司, 其排程未公开」。
- `read_lints`：`r3r_schedule_gui.cpp`、`sl/order_sl.cpp`、`station_cmd.cpp`、`command_type.h`、`widgets/r3r_schedule_widget.h` 全部 0 条。

### 7.7 本轮额外发现（构建期拦下的两个工具链事实）

- 本 fork **没有 `WWT_CHECKBOX`** 控件（`src\widget_type.h` 里不存在），开关一律用 `WWT_TEXTBTN` + `SetWidgetLoweredState()` 表达"按下=开"，已按此实现公开开关。
- 本 fork 文本颜色是 `TextColour::White / TextColour::Grey`（`src\gfx_type.h` 的 `enum class TextColour`），**没有** `TC_WHITE/TC_GREY` 宏；`DrawString(..., TextColour)` 直接用枚举。
