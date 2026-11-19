# R3R 分配规则「多选调度」落地备忘（第 451 轮续三 / 2026-11-16）

> 性质：临时分析报告（本轮唯一可信状态载体）。结论只认本文件与 `R3R_KNOWN_ISSUES.md`。
> 关联：`R3R_dispatch_pool_rules_round451_memo.md`（第 451 轮：规则程序从池条目搬到「每场一份程序 + 独立窗口」）、
> `R3R_KNOWN_ISSUES.md` 的「第 451 轮续二」小节（4 条实机缺陷：标题 `(invalid parameter)` / 同级操作弹红框 / 结果类控件全灰 / 要多选）。

---

## 一、玩家需求原文

> 「4，我希望允许或不允许分配可以多选调度。」

即：一条 `Result` 指令（允许/拒绝分配调度）不再只能指向一份调度，而是可以同时勾选多份；
勾中任意一份即命中该 Result 行，与既有「第一条命中的 Result 生效」语义叠加。

---

## 二、落地清单（按文件）

### 1) `src\r3r_dispatch_program_gui.cpp`（窗口本体，主战场）

| 位置 | 内容 |
| --- | --- |
| `:91` `DispatchPoolEntrySchedule(e)` | 池条目 → 排程下标；旧档条目 `schedule == R3R_DISPATCH_NO_SCHEDULE` 时回退 `Vehicle::Get(e.host)->orders->index.base()`，与 `station_cmd.cpp` 的 `R3RDispatchEntryScheduleIndex` 同构 |
| `:102` `R3R_DISPATCH_MASK_BITS = 32` | 掩码位宽上限（受 `R3RDispatchInstruction::extra` 单字段 32 位所限） |
| `:106` `DispatchCollectSchedules(st, yard, out)` | 按池顺序去重收集「本场有哪些调度」，GUI 与求值器必须给出**同一去重顺序** |
| `:120` `DispatchMaskLabel(schedules, mask)` | 掩码 → `"A, B"` 文本；空则 `STR_R3R_DP_NO_SCHEDULE` |
| `:133` `DispatchInstructionLine(ins, schedules)` | 指令行文本；Result 行改用 `DispatchMaskLabel()` 渲染多份 |
| `:177` `~DispatchProgramWindow()` | 主窗口析构时 `CloseWindowById(WindowClass::DispatchSchedulePick, window_number, false)`，防孤儿窗口 |
| `:346` `SetScheduleMask(uint32_t)` | 同值早退，否则 `Apply()` + `SetDirty()`。**原 `SetSchedule(uint32_t)` / `NextSchedule()` 两个单选用法已删** |
| `:358` `EnsureResultRow()` | 无选中行时追加一条 Result 指令；`WID_DP_SCHEDULE` / `WID_DP_RESULT_KIND` 点击时先调它 ⇒ 空程序也能直接建结果行（治「全灰按不下去」） |
| `:445` / `:474` | `DrawWidget`：调度按钮与指令列表都先 `DispatchCollectSchedules()` 再渲染 |
| `:526` / `:544` | 点击：调度按钮 → `EnsureResultRow()` 后 `ShowDispatchSchedulePicker(this)`；结果类型 → `EnsureResultRow()` 后 `ToggleResult()` |
| `:619` `struct DispatchSchedulePickWindow` | 多选窗口（自绘 `[X]/[ ]` 列表 + 滚动条），窗口号与父窗口同为 `PackDispatchProgramWindowNumber(station, yard)` |
| `:642` `CurrentMask()` | **勾选状态一律活读父窗口当前选中行的 `ins->extra`**（命令被拒 / 读档重载后本地副本会过期） |
| `:680-683` `:709-714` | 绘制/点击都用 `CurrentMask()`；点击后 `parent->SetScheduleMask(newmask)` |
| `:757` `ShowDispatchSchedulePicker()` | 已开着则 `BringWindowToFrontById` + `SetWindowDirty`，不新建 |
| `:502-515` 列表选行 | 换选中行后：新行不是 Result ⇒ `CloseWindowById(DispatchSchedulePick)`；是 Result ⇒ `SetWindowDirty`。避免「多选窗口还开着但编辑对象已换人」 |
| `:398` `OnInvalidateData()` | 程序回读后额外 `SetWindowDirty(DispatchSchedulePick)`（勾选是活读，回读后要重画） |

多选窗口控件树：`CLOSEBOX / CAPTION / SHADEBOX / STICKYBOX / PANEL(LIST, SetScrollbar) / VSCROLLBAR / RESIZEBOX`，
描述 `_dispatch_schedule_pick_desc`（窗口名 `r3r_dispatch_schedule_pick`，240×180，`WindowPosition::Automatic`）。
标题串用 `STR_R3R_DP_SCHEDULE_TOOLTIP`（**不能用** `STR_R3R_DP_SCHEDULE`，后者是 `"调度: {STRING}"` 带参串，会再犯 `(invalid parameter)`）。

### 2) `src\window_type.h`

`WindowClass` 末尾新增 `DispatchSchedulePick,`（注释注明窗口号与 `#DispatchProgram` 同款打包）。追加在末尾 ⇒ 既有枚举值不变。

### 3) `src\station_cmd.cpp`（求值侧）

- `:2510` `R3RDispatchBuildScheduleSlots(pool, out)`：与 GUI `DispatchCollectSchedules()` 同一去重规则。
- `:2529` `R3RDispatchProgramRun(prog, slot, e, stats)`；`:2578` Result 判定改为
  `if (slot >= 0 && slot < 32 && HasBit(ins.extra, static_cast<uint>(slot)))` ⇒ 命中多份中任意一份即触发该行。
- 裸用的 `R3R_DISPATCH_NO_SCHEDULE` 全部限定为 `Station::R3R_DISPATCH_NO_SCHEDULE`（该常量是 `Station` 的静态成员）。

### 4) `src\station_base.h`

`:945` 的 `extra` 注释订正为「Result：允许/拒绝的调度掩码（bit i = 去重槽位 i…）；Counter：计数器编号」——
原注释写的是「调度（OrderList 池下标）」，与多选后的语义相反，留着会误导下一轮。

---

## 三、关键设计决定与理由

1. **自绘勾选列表，而非下拉**：调度名不是 `StringID`（是 `std::string`），`NWidgetCore::SetString` 与下拉菜单只吃 `StringID`；
   与本窗口既有口径一致（所有动态文案都在 `DrawWidget` 里 `DrawString` 自绘）。
2. **点击即下发命令，无「确定」按钮**：与既有「插一条指令 = 一次命令」风格一致；代价见 REG-452-C。
3. **掩码活读父窗口**：`SetScheduleMask()` 走 `Apply()`，命令可能在 `Execute` 阶段被拒（例如同场程序被别的公司改动）；
   本地 `this->mask` 只作父窗口已消失时的兜底，避免「界面显示 A、服务器实际 B」。
4. **位宽 32**：`extra` 是单个 `uint32_t`，且指令 blob 是定长 24B（3 word）。要突破 32 需改存档布局，本轮不做。

---

## 四、疑点与已知缺口（REG / U 编号，供后续轮次直接引用）

| 编号 | 内容 | 状态 |
| --- | --- | --- |
| REG-452-A | **旧存档语义不兼容**：多选之前 `extra` 存的是「池下标 / `R3R_DISPATCH_NO_SCHEDULE`」，现在按位掩码解释。旧程序里 `extra = 3` 的 Result 会变成「槽位 0 和 1 都允许」，而不是「调度 3」。存档侧无版本字段（`SYRD` 之外程序是定长 blob，见 `CmdR3RSetDispatchProgram` 的 24B stride），**无法在加载期区分新旧**。 | 已登记 KI-644（低）；建议口径：旧测试档重新在多选窗口勾一次即可 |
| REG-452-B | 池内去重调度数 > 32 时，第 33 份起无法勾选（`R3R_DISPATCH_MASK_BITS` 上限），界面上表现为「点了没反应」。 | 已登记 KI-645（低） |
| REG-452-C | 每点一次 = 一条 `Commands::SetR3RDispatchProgram`（整份程序 blob 重写）⇒ 连续勾 N 份产生 N 条命令，撤销栈会一步步回退。 | 已登记 KI-645（低），共条目 |
| U-452-1 | `top` 滚动偏移与点击行号映射（`OnClick` 用 `(y - top) / LineHeight()`）在窗口缩放后是否仍对齐，需实机点一次验证。 | 未验证 |
| U-452-2 | 勾选列表按 `DispatchCollectSchedules()` 顺序展示；若别的列车在窗口开着时把新调度放进池，列表会随 `SetDirty` 变长——滚动位置是否跳变未验证。 | 未验证 |

---

## 五、复测判据（实机）

1. 打开某场的「分配规则」，程序为空时直接点「调度」按钮 ⇒ 自动出现一条 Result 行，多选窗口弹出（不再全灰）。
2. 多选窗口同时勾 2 份调度 ⇒ 指令行显示 `允许分配调度 A, B`；再取消一份 ⇒ 变成只剩一份。
3. 关掉多选窗口再点开 ⇒ 勾选状态与指令行一致（验证 `CurrentMask()` 活读，不是本地缓存）。
4. 关闭规则主窗口 ⇒ 多选窗口跟着关掉（不留孤儿窗口）。
5. 负判据：单份调度的旧程序，逻辑行为与改造前一致（只命中那一份）。
6. 负判据：掩码为空（一份都没勾）时该 Result 行永不命中，与 `R3R_DISPATCH_NO_SCHEDULE` 的旧行为对齐。

---

## 六、构建自证

- 入口：`R3R_fullbuild.cmd inc detach`（复用既有入口，未新建任何 `.cmd`）。
- 本轮改了 `src\station_base.h`（仅注释）⇒ 护栏判 `REMOVED 622 object file(s) - upgrading this build to a FULL rebuild`
  （guard.log 原文：`GUARD:   newer: src\station_base.h  (2026-11-16 17:52:29)`）。
- 结果：`[694/694] Linking CXX executable openttd.exe`；`build\R3R_incbuild.done` = `EXIT_CODE=0`（2026-11-16 18:17:39）；
  `error C*` / `error LNK` / `fatal error` / `FAILED:` / `build stopped` 计数 **0**。
- 时间戳链：`station_base.h` 17:52:29 → `station_cmd.cpp.obj` 18:12:45；`r3r_dispatch_program_gui.cpp` 17:54:08 → obj 18:11:01
  （均 obj 晚于源码）；`build\openttd.exe` **2026-11-16 18:17:18（53 017 600 B）** 晚于全部源码与 obj。
- `read_lints` 0 条（两个改过的 .cpp）；exe 内可检索到 `r3r_dispatch_schedule_pick`。
- 发布树 `build-release` 未编（只编内测探针树）。
