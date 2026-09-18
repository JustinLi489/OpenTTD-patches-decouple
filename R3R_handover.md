# R3R 工作交接（第 38 轮 · 2026-09-17）

> **本文件是「换会话 / 换工作目录 / 换人」时的第一入口。** 只做索引 + 当前状态 + 待办，权威细节一律以被索引的文件为准。
> 立文件因由：2026-09-17 第 37 轮 C 盘被占满（曾 0 字节可用，KI-83），文档一度被同步到 C 盘旧会话目录 `c:\Users\冯洁敏\CodeBuddy\20260914233818` 以求展示，造成工作出现两个落点。第 38 轮已全部合并回唯一工作区。

---

## 0. 一句话现状

R3R（JGRPP 魔改）「挂接分组 + 跨公司挂接」功能 **P1 / P2 / P3 三步均已编码完成并全量重编通过**，**卡在玩家实测**（P1 §8.3、P2 §9.4、P3 §10.5 三张清单，尚未有任何一条被玩家确认）。代码侧无阻塞项。

---

## 1. 唯一工作区（不要再分裂）

| 项 | 值 |
|---|---|
| **唯一工作区** | `d:\sourcecode of JGRPP`（源码 / 构建 / 备忘 / 台账全在这里） |
| 分支 | `feature/decouple` |
| HEAD | `0d9ff990fc`（2026-09-15 20:19，*段右边界标记 `SegmentBack`*） |
| 上一提交 | `708326ada7`（09-15 splice-pair fold test）、`2db952d332`（09-10~09-14 WIP 备份） |
| 已废弃落点 | `c:\Users\冯洁敏\CodeBuddy\20260914233818` —— **已于第 38 轮迁移完毕并删除**，内含内容全部是工作区文件的旧镜像 + 会话临时脚本，无独有源码 |
| 清理前 C 盘 | 116.5 GB 已用 / **1.3 GB 可用**（占满主因不是本工作区：`AppData` 42.7 GB、`Documents` 12 GB 等应用缓存，见 §8） |

**规则：所有文档一律写 `d:\sourcecode of JGRPP` 根目录。** 不要为了「结果视图能显示」把文档复制到 C 盘 —— 那是第 37 轮 C 盘爆盘的直接原因。

---

## 2. 权威文件地图（先读顺序）

| 文件 | 作用 | 权威性 |
|---|---|---|
| `R3R_KNOWN_ISSUES.md` | **总台账**。§一 当前状态摘要、§二 `KI-01..KI-83` 总表（权威口径）、§三 决策记录、§四 各轮复测判读、§五 性能（已搁置）、§六 配套工具、§七 更新约定 | **最高**，与其它文件冲突时以它为准 |
| `R3R_crosscompany_design_memo.md` | 跨公司挂接**决策台账**。§2 玩家拍板原文、§4 改动清单、§7 分步计划、§8.3 / §9.4 / §10.5 三张实测清单、§10.8 全量重编记录、§10.9 踩坑小结 | 高（功能语义） |
| `R3R_couple_group_design_memo.md` | 「挂接分组」功能设计备忘（§14.4 玩家实测会话 1 核对、§15.4 补测动作） | 高（分组功能） |
| `R3R_per_segment_linkgraph_memo.md` | 「按段刷新 + 按段结算」备忘（§5 玩家实测清单，对应 KI-70/71/72/73） | 高（货运/运费） |
| `R3R_multi_couple_decouple_orders_memo.md` | 多次解挂的**排程归属**问题与四种模型（§10 / §11 T8701 场景） | 高（排程语义） |
| `R3R_segment_decouple_plan.md` / `R3R_splice_pair_plan.md` / `R3R_freewagon_direct_segment_memo.md` | 段体系 / 拼接对 / free-wagon 直连段 的规划与落地 | 中（历史规划） |
| `R3R_NEXT_STEPS.md` | 早期待办与性能轮次数据（含 KI-14 各轮） | 中（部分已被 KNOWN_ISSUES 取代） |
| `R3R_archive_20260914_wip\` | 第 38 轮归档：`_d_small.txt` / `_d_train.txt`（09-10~09-14 WIP 的完整 diff）、`_ki_ctx_out.txt`、`R3R_perf.log`。**内容已由 git 提交 `2db952d332` 保底**，归档仅为留证 | 低（只读留证） |
| `R3R_WIP_diff_清单.md` | 该批 WIP 的差异清单（18 文件 / +1234 −205，与 `2db952d332` 逐一对上） | 低（只读留证） |
| `R3R_第30轮_车库第二次挂车修复.md` | KI-67 / KI-68 的独立备忘（内容已被 KNOWN_ISSUES 收录） | 低（重复留证） |

---

## 3. 代码与构建现状

| 项 | 值 |
|---|---|
| **内测（Debug）产物** | `build\openttd.exe` @ **2026-09-17 16:23**，50 633 728 B。全量重编 691 步 `EXIT_CODE=0`（`build\R3R_fullbuild.done`），日志 `build\R3R_fullbuild.log`。**该次重编合并执行了 KI-66** ⇒ 这就是含 P1+P2+P3 三步的版本 |
| 内测树构建类型 | **Debug**（`/Od /Ob0 /RTC1 -MTd -D_DEBUG`），**探针默认 ON**（KI-23 / KI-28）⇒ 比 Release 慢 5~20 倍属正常，别当性能回归 |
| KI-15 卫生 | `stale_obj_count=0`（`newest_src=vehicle_base.h @ 16:02:21` < `oldest_obj=alloc_func.cpp.obj @ 16:03:20`）⇒ **无陈旧/混合对象** |
| 产物自证 | exe 内可检索到 `R3R-PAY-FINAL` / `R3R-PAY-TRANSFER`，且 `ADVANCE: veh=` 已消失（`_tmp_verify_p3.cmd`） |
| **发行树（Release）产物** | `build-release\openttd.exe`（RelWithDebInfo `/O2 /Ob2 /DNDEBUG`，探针默认 OFF）。第 33 轮全量重编通过；闸门 `[5c]` 已验证 `WITH_ZLIB/LIBLZMA/ZSTD/LZO/PNG/OPUSFILE` 全在，**无 KI-25 退化** |
| 运行时日志 | `R3R_perf.log` 由 exe 写在**自身所在目录**（即 `build\`），当前约 5.7 MB；`R3R_debug.log` 同目录。取数与 A/B 用 `R3R_AB_probe.cmd`（详见 §7） |

---

## 4. ⚠ 未提交改动（本轮最大风险项）

`git status` 实测：**25 个已跟踪文件改动 + 9 个新文件**，全部未提交。新文件：

```
src/couple_group_type.h        src/couple_group.h/.cpp
src/couple_group_cmd.h/.cpp    src/couple_group_gui.h/.cpp
src/sl/couple_group_sl.cpp     src/widgets/couple_group_widget.h
```

改动文件：`src/CMakeLists.txt`、`sl/CMakeLists.txt`、`widgets/CMakeLists.txt`、`command_table.cpp`、`command_type.h`、`window_type.h`、`sl/saveload.cpp`、`train.h`、`train_cmd.cpp`、`train_gui.cpp`、`vehicle.cpp`、`vehicle_base.h`、`vehicle_cmd.cpp`、`vehicle_gui.cpp`、`vehicle_gui_base.h`、`depot_gui.cpp`、`group_gui.cpp`、`economy.cpp`、`economy_base.h`、`linkgraph/refresh.cpp`、`linkgraph/refresh.h`、`pathfinder/yapf/yapf_destrail.hpp`、`pathfinder/yapf/yapf_rail.cpp`、`lang/english.txt`、`lang/simplified_chinese.txt`。

**建议第一步就做一次 checkpoint commit**（例如 `R3R: checkpoint - 挂接分组 + 跨公司挂接 P1/P2/P3（待实测）`），把「未提交量 = 34 文件」清零。理由：本工作区的历史已证明丢改动代价极高（09-10~09-14 的 WIP 靠事后补的 `2db952d332` 才救回来）；且 C 盘爆盘已经威胁过一次写盘。

---

## 5. 待玩家实测（当前唯一卡点，按优先级）

所有清单的**完整判据**在 `R3R_crosscompany_design_memo.md`，此处只列条目：

**P1 — 允许跨公司挂接（台账 §8.3，4 条）**
1. 分组窗口勾选/取消「允许跨公司挂接」⇒ 状态立刻跟随、切组跟随、**别人公司的组置灰**。
2. **同公司挂接行为必须与改前逐位相同**（硬回归线）。
3. 跨公司：同组且已勾选 ⇒ 能挂上；取消勾选或移出组 ⇒ **贴上去也不挂、不刷屏、不死循环**（本步最重要的「不倒退」判据）。
4. 挂上后钱仍全记在链头 = **已知且预期**的中间状态，别当 bug 报。

**P2 — 连挂期间他公司段只读冻结（台账 §9.4，复测 1~4）**
- 复测 1：A 卖给**自己**的车应成功；A `Ctrl` **整链出售** ⇒ 必须报错且 B 的段完好（日志 `R3R-FROZEN-SELL`）。
- 复测 2：A 对链做 refit（含车库 refit）⇒ 必须报错；B refit 自己的段 ⇒ 放行（日志 `R3R-FROZEN-REFIT`）。
- 复测 3：B 解挂收回段后，A 再出售/refit ⇒ 恢复正常（`R3RChainSpansCompanies` 变 false）。
- 复测 4：跨公司连挂时车辆窗口 refit 按钮置灰。

**P3 — 跨公司运费分账（台账 §10.5，复测 1~5）**
- **复测 1 = 回归门**：同公司多段链跑一票 ⇒ 公司总收入 / 各车 `profit_this_year` / 货物流面板与改前**逐位一致**。
- 复测 2：A 机车挂 B 车底，A 的站装货、B 的站卸货 ⇒ 双方银行都增加，**两者之和 == 单公司承运同票金额**（日志 `R3R-PAY-FINAL ... payee=<B>`）。
- 复测 3：中途站中转 ⇒ `R3R-PAY-TRANSFER` 收款方 = 跑那一腿的公司。
- 复测 4（并存对照）：同场景分别用同公司/跨公司跑一遍，核对「公司总账」与「各车统计之和」两组数字相等。
- 复测 5（存档）：跨公司链运输途中存读档再交付 ⇒ **不得有任一公司多拿一次运费**（P3 未新增存档字段，理论上安全，但仍需实证）。

**另有待实测（非本次三步，历史上已挂起）**：KI-67（`R3RRelocateFrontIdentity` 覆盖 `cur_real_order_index`）已修待复测、KI-68（解挂出的段头未重建 tick 缓存 ⇒ 库内第二次挂不上）已修待复测、KI-66（`ADVANCE` 探针口径）已修。

---

## 6. 仍未闭合的已知问题（详见 `R3R_KNOWN_ISSUES.md` §二）

- **KI-81（低，刻意搁置待裁决）**：P3 **只分钱、不分权与统计** —— `delivered_cargo`、货物流面板、补贴判定、工业独占权仍按链头公司算。改成按车 owner 会引入「链头 A 持独占权 + 挂 B 的车 ⇒ 工业按 B 判拒收、货改投城镇」的玩法级回退，故不做。
- **KI-72**：按段刷新/结算的开放点（见 `R3R_per_segment_linkgraph_memo.md`）。
- **KI-70 / KI-71 / KI-73**：CargoDist 逐跳站对模型不认连挂、按段结算、`train_cmd.cpp:4474` C4150 兜底 `delete` 不完整类型（已加 `#include "economy_base.h"`）。均需随 P1~P3 一起回归。
- **KI-06 / KI-08 / KI-09 / KI-12 / KI-13 / KI-22 等**：折叠判据、artic 边界、解挂路径的历史条目，状态见总表（多为「已修」或「部分防护」）。
- **性能维度**：2026-09-14 已拍板**降级为「低」并搁置**，KI-19/KI-20 的 A/B 倍率**全部作废**（根因是 Debug vs Release 而非 R3R 代码回归，见 §五）。不要再拿它们当回归证据。

**注意**：2026-09-14 曾有一次「回退到 `f3ebaad870` 09-09 基线」的动作，**已于 2026-09-15 01:12 整体撤销**（工作树恢复为 `2db952d332`）。所以旧记录里「回退后 = 未修」的字样**不代表当前代码**。

---

## 7. 工具链与操作铁律（每条都是踩过的坑，务必复述）

**编译**
1. 裸 `cmake --build` 会因缺 `INCLUDE` 报 cl 找不到 `stdint.h` ⇒ 必须先 `call "D:\VISUAL STUDIO\MAIN PACK\VC\Auxiliary\Build\vcvars64.bat"`。
2. **改任意 `src/*.h` ⇒ 必须删光全部 `*.obj` 全量重编**（KI-15）。根因：本机 `deps=msvc`，ninja 只能解析英文 `Note: including file:`，而 cl 输出中文 `注意: 包含文件:` ⇒ `ninja -t deps` 显示 `#deps 0`，头文件改动**完全不触发**重编 ⇒ 混合 obj ⇒ 随机崩溃（vtable 越界 `call 0`）。`VSLANG=1033` 实测无效。
3. **改 `src/lang/*.txt` ⇒ 强制重编 `strings.cpp`**（KI-16）：否则 `strings.h` 里 `LANGUAGE_PACK_VERSION` 与 `build\lang\*.lng` 不匹配，启动报 *No available language packs (invalid versions?)*。
4. **`-j2`，不要 `-j4`**：本机 8 GB 内存 + CodeBuddy 自身占用，`AvailableMBytes` 可能只剩几百 MB，4 个 `/O2` 的 `cl.exe` 会被换出物理内存导致构建**完全冻结**。
5. **TEMP/TMP 一律改指 D 盘**（`build-release\tmp` / `build\tmp`）：C 盘空间不足以承载 LTCG 按 GB 计的链接临时文件。
6. **构建日志是 GBK**，`ripgrep` / `search_content` 看不见含中文的行 ⇒ 只能命中 ASCII 的 `FAILED:`，制造「失败但无诊断」假象。**查构建失败必须用 `findstr /C:"error C" <log>`**（KI-82）。
7. **`openttd.exe` 被占用会 `LNK1168`** ⇒ 链接前先关进程。
8. 换 `.cmd` 探针脚本前**先停掉正在跑的全量重编**，否则日志被覆盖、证据丢失。

**脚本编码**
9. 新建 `.ps1` **必须纯 ASCII**（连注释里的中文都不行）：本工具按 UTF-8 写盘，而 PowerShell 5.1 按 ANSI(936) 读无 BOM 的 `.ps1`，中文注释会把代码吃掉。含中文的 `.cmd` **必须存 GBK**。改完跑 `R3R_fix_gbk.ps1 <file>` 复核（合格输出 `ASCII-OK` / `GBK-ALREADY`）。
10. `cmd /c "含空格路径\脚本.cmd 参数"` 的引号会被剥离 ⇒ **探针脚本一律不带参数**。路径拼 `-ExeDir` 时要先去掉尾部反斜杠，否则 CRT 把 `\"` 当转义引号。
11. `PowerShell` 里 `$out.Add('{0:D2}' -f $i, $x)` 必须写成 `$out.Add(('{0:D2}' -f $i, $x))`，否则逗号被当方法实参分隔符。

**取数与观测**
12. `R3R_AB_probe.cmd`（GBK）/ `R3R_AB_probe.ps1`（纯 ASCII）**两个脚本都必须在工作区根目录**（`%~dp0` 靠同级找 `.ps1`）；数据目录是 `D:\r3r_probe\AB`（硬编码）。配置键：`save` / `r3rexe` / `vanexe`。
13. `R3R_perf.log` 写在 **exe 所在目录**，脚本用 `%~dpi` 从 `r3rexe` 推出并搬运 ⇒ 换 exe 目录（如 `build` → `build-release`）无需改脚本。
14. **解析器只认「正在跑」的窗口**：`fps>1 且 loco>0.1`，否则回退最像在跑的窗口并输出 `window_quality` / `log_state`。**暂停态采数会被判 `not_running` 并打中文警告** —— 不是解析 bug。
15. 本模型**无视觉能力，读不了 `.png`**；可用 `R3R_shot_ocr.ps1`（Windows.Media.Ocr，`zh-Hans-CN`）OCR 截图，但小字号数字误识率高，**只能定性不能取数**。

---

## 8. 第 38 轮（本轮）做了什么

### 8.1 迁移（C 盘旧会话目录 → 工作区）

对 `c:\Users\冯洁敏\CodeBuddy\20260914233818` 逐文件与工作区做 MD5 比对，结论：**只有 2 个文件在工作区有同名版本，且工作区版本都更新更全**（`R3R_KNOWN_ISSUES.md` 385 行 ↔ **390 行**；`R3R_crosscompany_design_memo.md` 15 534 B ↔ **33 425 B**）；差异逐行核对过，C 盘侧的独有内容只有 3 行草稿措辞，**均已被工作区版本取代，无信息丢失**。其余文件是会话临时脚本与旧预览。

独有且有保留价值的已迁入（MD5 全部校验通过）：

| 文件 | 去向 |
|---|---|
| `R3R_WIP_diff_清单.md` | 工作区根（09-10~09-14 WIP 差异清单） |
| `R3R_第30轮_车库第二次挂车修复.md` | 工作区根（KI-67/68 独立备忘） |
| `_d_small.txt`、`_d_train.txt` | `R3R_archive_20260914_wip\`（WIP 完整 diff，已由 `2db952d332` 保底） |
| `_ki_ctx_out.txt`、`R3R_perf.log` | `R3R_archive_20260914_wip\`（留证） |

### 8.2 清理（已执行，逐项核对）

| 目标 | 清理前 | 结果 |
|---|---|---|
| `c:\Users\冯洁敏\CodeBuddy\20260914233818`（旧会话目录） | 0.44 MB | **已删除**（`Test-Path` = False；含 18 个 `_*.ps1`、5 个 `_*.cmd`、若干 `_*.txt` 扫描件、2 个 `_r3r_*_preview.md` 旧预览、5 个 0 字节失败写入文件） |
| `AppData\Local\CrashDumps` | 57.5 MB | **已清空**（0 文件） |
| `AppData\Local\pip\Cache` | 14.5 MB | **已清空**（0 文件） |
| `AppData\Local\NVIDIA\DXCache` + `GLCache` | 1 332 MB | **已清空**（0 文件；DirectX/OpenGL 着色器缓存，下次跑游戏自动重建） |
| `C:\Windows\Temp` | 0.9 MB | 未清（沙箱回收站拒绝，收益可忽略） |
| `AppData\Roaming\CodeBuddy CN\logs` | 74.1 MB | 未清（当前会话正在写，文件加锁） |

**未动**任何用户数据与不可再生成项：`C:\Windows\Installer`（1.13 GB，卸载/修复要用）、`AppData\Local\Packages`（2.1 GB）、剪映 / WPS / 金山 / 百度 / 腾讯 / Microsoft（合计约 24 GB）、`~\.codebuddy`（含记忆与历史）。

**可选清理（属其它工具的数据，本轮未动，需你点头）**：`~\.workbuddy\traces` 961 MB + `logs` 25 MB（另一 agent 工具的遥测与日志，文件均已陈旧）。

**结论**：C 盘占满**不是本工作区造成的**，靠清缓存也补不回来（本轮清掉约 1.4 GB，而 `AppData` 本身 42.7 GB）。真正的对策是 §7 铁律 5（`TEMP/TMP` 固定指 D 盘）+ 按需清理上面列出的应用缓存。

### 8.3 C 盘占满的真因（不是本工作区）

| 目录 | 占用 |
|---|---|
| `AppData`（总计） | **42.7 GB** —— 剪映 4.9 GB、WPS 3.6 GB、金山 3.0 GB、百度 4.3 GB、腾讯 2.7 GB、Microsoft 6 GB、GitHubDesktop 1.3 GB、NVIDIA 1.3 GB… |
| `Documents` | 12 GB |
| `.workbuddy` | 2.2 GB |

本工作区（源码 + 两个构建树 + 全部备忘）**在 D 盘**，与 C 盘爆盘无因果关系；但由于 agent 的 `%TEMP%` 在 C 盘，**C 盘一满就会让编译/写盘/展示动作随机失败**。这是把 `TEMP` 改指 D 盘（铁律 5）的动机。

---

## 9. 建议的下一步

1. **做 checkpoint commit**，把 34 个未提交文件的改动固化（见 §4）。
2. **跑 P1/P2/P3 实测清单**（§5）。建议顺序：先 P3 **复测 1（同公司回归门）** —— 它是唯一「一旦挂了说明整条链都坏了」的判据；再 P1 第 3 条（不刷屏不死循环）；最后才是分账数字类复测。
3. 实测结果按 §7 铁律取数（`findstr` 读日志 / 只认 running 窗口 / 别用 ripgrep 读 GBK 日志），并回填到 `R3R_crosscompany_design_memo.md` 的对应小节 + `R3R_KNOWN_ISSUES.md` §一 摘要。
4. 长期：把 agent 工作区与 `TEMP` 固定在 D 盘；定期清理 §8.3 列出的应用缓存。

---

*本文件由第 38 轮建立。工作区发生目录变更、或某个功能阶段收尾时，请在本文件顶部更新时间与轮次，不要把状态写散到多处。*
