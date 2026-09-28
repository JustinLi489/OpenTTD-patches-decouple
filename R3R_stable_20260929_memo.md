# R3R 稳定版收口备忘 —— 2026-09-29

> 用途：把「2026-09-23 ~ 09-29 这 7 天（第 95 ~ 151 轮）的全部改动」正式固化下来 —— 冻结了哪个版本、
> 这一大批改了什么、哪些还没验、下一步从哪起手。配套清单：`R3R_KNOWN_ISSUES.md`（本文件只做索引与结论）。
> 快照目录：`build\r3r_stable_2026-09-29\`（`README.txt` 在目录内，含回退步骤与二进制可信度核验）。

---

## 1. 一句话结论

**第 95 ~ 151 轮的全部改动一次性提交入库，并冻结为 `r3r-stable-2026-09-29`。**
本次提交把 09-26 快照留下的「git 级标记未做」补齐 —— 从这一版起，回退可以只靠 git，不必再依赖目录覆盖。

## 2. 冻结了什么

| 项 | 值 |
|---|---|
| git commit | `8d10a666f63c4608e4d1af898184811f974212f9`（branch `feature/decouple`，2026-09-29 00:22:19 +0800，52 files / +12166 −618） |
| git tag | `r3r-stable-2026-09-29`（附注标签） |
| 上一稳定版 | `build\r3r_stable_2026-09-26`（**工作区快照，未单独提交**，基线 = `cb997d36c2`） |
| 再上一稳定 | `1ce54c475f` = tag `r3r-stable-2026-09-22` |
| 快照目录 | `build\r3r_stable_2026-09-29\`（`src\` 42 个改动文件 + `openttd.exe` + `README.txt`） |
| exe | `build\openttd.exe` @ 2026-09-29 00:01:27（51 401 216 B，内测版 Debug，探针默认 ON） |
| exe SHA256 | `0F9220B39265F524252A70EAB20BDD061605C6BD674680A1F8DCD02BEEF3A843` |
| 二进制可信度 | 最新 obj = `yapf_rail.cpp.obj` @ 09-28 23:55:01；`src\` 下最新头/语言文件 = `simplified_chinese.txt` @ 17:08:33 ⇒ 无陈旧 obj；链路 `yapf_rail.cpp`(23:52:42) → obj(23:55:01) → exe(00:01:27)；增量护栏报 `incremental is safe`、`[3/3] Linking`、`EXIT_CODE=0`、errs 0 |

> 快照放的是 `build\` **内测版**（探针 ON），因为这 7 天的实测都在它上面做的。
> 需要发行版按 `R3R_fullrebuild.cmd` 选 [1] 单独编（发行版经 `R3R_PROBES` 编译期开关证明不含探针）。

## 3. 这次提交必须带上的一件事：`src\r3r_resv_probes.h`

这个文件是**新增**的，而且已被**已跟踪的头文件 `#include`**：

```
src\pbs.h:18          #include "r3r_resv_probes.h"
src\rail_map.h:21     #include "r3r_resv_probes.h"
src\bridge_map.h:16   #include "r3r_resv_probes.h"
（另有 road_map.h / station_map.h / tunnel_map.h 同类引用）
```

它此前一直是「未跟踪」状态 —— 也就是说**在本次提交之前，这个仓库 clone 出来是编不过的**。
本次一并入库（同时入库 `_tmp_inc_build.cmd`、`R3R_inc_guard.ps1`、`R3R_fullbuild*.cmd`、
`R3R_release_fullbuild*` 这些一直在用但从未入库的工作流脚本）。

## 4. 这 7 天把哪些东西做到能用了

按主题归类（详细条目与现场证据见 `R3R_KNOWN_ISSUES.md`）：

| 主题 | 关键条目 | 落点 |
|---|---|---|
| 站场（yard） | 重命名 / 删除 / 每场各自回落目标 `shared_with` / 删场重映射订单 / SYRD v5 带场名 | `station_base.h` `station_cmd.cpp` `station_gui.cpp` `sl\station_yard_sl.cpp` |
| 订单层 | KI-124（`MOF_R3R_YARD` 进白名单）、KI-125（共享场语义纠正） | `order_cmd.cpp` `order_gui.cpp` `order_base.h` |
| 挂接分组 | 父子组成员关系（读侧展开）、直接建子组、取消选中、临时挂接分组支持 `OT_WAIT_COUPLE`（KI-225/KI-233） | `couple_group*.*` |
| 预留生命周期 | KI-203/207（记账 + 安全释放 + 站台/路点整格位清扫）、KI-208/209/210/211/211d（归属判定、`RESV-NOBOOK`）、只读孤儿审计 | `pbs.cpp` `pbs.h` `r3r_resv_probes.h` |
| 挂/解挂与订单 | 段级折叠修正、artic 角色位快照、ORD-PUSH（KI-215/215b）、KI-164 按 `StationID` 判到达、KI-165 目的地并入 `R3RCoupleAllowed()`、KI-237 段头车号取回 | `train_cmd.cpp` `train.h` |
| 车库 | KI-212 拒绝落到段内部、KI-213 拖到链头之前、KI-93 车库卖光崩溃防护 | `depot_gui.cpp` `train_cmd.cpp` `vehicle_cmd.cpp` `order_cmd.cpp` |
| YAPF | KI-163 被跳过的站台段改按真实跳段重建、KI-194/196 候选闸门、**KI-242（本轮）折叠否决参照系** | `pathfinder\yapf\yapf_rail.cpp` `yapf_destrail.hpp` |
| 构建/发行 | `R3R_PROBES` 编译期开关（KI-116，发行版可证明无探针） | `cmake\scripts\GenerateWidget.cmake` 等 |
| 其他 | newGRF「段内位置」按段计数 | `newgrf_engine.cpp`（更早轮次） |

## 5. 本轮（第 151 轮）的唯一新改动 = KI-242，且**待游戏内复测**

`src\pathfinder\yapf\yapf_rail.cpp` 的 `YapfTrainCheckReverse()` 折叠否决：

```cpp
// 旧：用裸 direction —— db=1（倒车）时参照系是反的
const TileIndexDiffC delta = TileIndexDiffCByDir(moving_front->direction);
// 新：用实际行进方向
const Direction front_dir = moving_front->GetMovingDirection();
const TileIndexDiffC delta = TileIndexDiffCByDir(front_dir);
```

现场（`build\R3R_debug.log` 1812 / 1818）：veh27 的 `direction=3`(SE) 但 `db=1`（倒车），
实际朝北行驶，车头是它的尾部 29(60,20)；`rel = 27(60,21) − 29(60,20) = (0,+1)`。

- 裸 `direction=3` → delta `(0,+1)` → dot **+1** ⇒ 误判「折叠」⇒ `return false`（**永不建议掉头**）
- 实际方向 `7`(NW) → delta `(0,−1)` → dot **−1** ⇒ 尾车本在行进方向后方，这条链是健康的

后果：列车失去唯一的掉头机会（`ProcessOrders()` 只在 `OT_NOTHING` 那一 tick 放行），
只能等「卡死列车」兜底计时器，期间持续 `ok=0 res=0`。
这也解释了为什么整份日志里 `CRT-FOLD` 只出现一次 —— 之后 `CheckReverseTrain` 根本不再被调用。

参照系取值已逐字节核对：`src\direction_type.h:24`（8 向）+ `src\map.cpp:263` 的 `_tileoffs_by_dir`
（`SE`=3→`(0,+1)`、`S`=4→`(+1,+1)`、`NW`=7→`(0,−1)`）；写法与 KI-241 在 `src\train_cmd.cpp:12468`
的接缝判据一致（同样用 `GetMovingDirection()`），不是新发明的参照系。

**安全性**：db=0 时 `GetMovingDirection() == direction` ⇒ 行为逐字节不变（真折叠链照旧被否决）；
db=1 时按行进方向取符号 ⇒ 健康倒车链不再被误否决，真折叠的 db=1 链（dot>0）仍被否决。

**复测判据 5 条**（原文见 `R3R_KNOWN_ISSUES.md` 末尾「第 151 轮（KI-242）」小节）：
① 解耦那一 tick 之后应**立刻**出现 `REVERSEDIR veh=27 ... db=1` + `REVERSEDONE db=0`，不再等卡死计时器，
且 `CRT-FOLD` 若出现应为 `dir=7` 且 `dot<=0`；② 随后 `TRP veh=27 mf=60,21 => ok=1 res=1` 并往南
（`destTx=60 destTy=31`）走；③ 普通倒车运行列车行为不变（无无谓掉头）；④ 站台耦合与第 150 轮（KI-240/241）
的接缝判据不受影响；⑤ 第 150 轮那种「目的地在北方」的场景重跑时，机车应掉头向南而不是硬闯。

## 6. 回退（本版起 git 就够）

```bat
:: 1) 只回退源码（推荐；不动 HEAD、不动 build\）
git checkout r3r-stable-2026-09-29 -- src

:: 2) 整体切到该标签（分离头，看完回来）
git checkout r3r-stable-2026-09-29
git checkout feature/decouple

:: 3) 只回退二进制
copy /y "build\r3r_stable_2026-09-29\openttd.exe" build\openttd.exe
```

⚠ 不要用 `git reset --hard`：会连未提交的工作区改动一起丢掉。
⚠ `build\` 被 `.gitignore` 忽略，git 回退不会动 obj/exe，需按第 3 条另行处理。
⚠ 回退后若这次动了 `.h` / `lang\*.txt`，先跑 `R3R_inc_guard.ps1` 判断是否要全量重编（KI-183），
再用 `R3R_fullrebuild.cmd`（全量）或 `_tmp_inc_build.cmd`（增量）。

## 7. 下一步候选（未做，别当成已完成）

1. **KI-242 游戏内复测**（5 条判据）—— 这是本次提交里唯一「已编译未实测」的行为改动。
2. **KI-214 视觉包围盒错位**：耦合/逻辑翻转后 `bounds`/`flip_offs` 与像素位置不同步，玩家已多次提出；
   排查入口 = `FOLDCHK` / `CPL idx=` 逐节像素 dump（注意 `R3RDbgEdge` 128 帧窗口会抑制行，
   「日志没有」不等于「没触发」）。
3. **不成段车厢链（尚未升级为段的车厢链）应拒绝 couple / uncouple** —— 长期待办，
   当前测试输入不含该形态，故本轮仍未动；实现时要与「段升级给链尾加假引擎、降级撤销」配合。
4. 09-26 那份快照没有独立 git 标记（状态已被本次提交覆盖），如需单独标记要自行 checkout `cb997d36c2` 再叠加目录。

## 8. 本次未入库的东西（刻意）

工作区仍有大量临时件：`_tmp_*.ps1` / `_tmp_*.cmd` / `diag_*.ps1` / `cleanup*.ps1` / `start_batch_*.ps1` /
`R3R_debug.log` / `R3R_perf.log` / `*.sav` / `R3R-v1.0.1-win64-2026-09-19.zip` 等。
本次只提交「源码 + 已被源码引用的头文件 + 一直在用的工作流脚本 + 文档」。
另：根目录有一个 **0 字节的畸形文件名** `` ` R3R_release_build.cmd` ``（名字最前面多一个空格），
属历史误建、未入库，可安全删除；真正在用的发行构建脚本是 `R3R_release_build.cmd`。

## 9. 提交里那 52 个文件是怎么定的

- **全部已跟踪的改动**（45 个）：`git add -u`，即上表 §4 的源码 + `R3R_KNOWN_ISSUES.md` +
  `R3R_flip_chain_decision.md` + `R3R_fullrebuild.cmd` + `cmake\scripts\GenerateWidget.cmake`。
- **必须补入的 1 个**：`src\r3r_resv_probes.h`（§3，不入库编不过）。
- **一直在用但从未入库的 6 个工作流脚本**：`_tmp_inc_build.cmd`、`R3R_inc_guard.ps1`、
  `R3R_fullbuild.cmd`、`R3R_fullbuild_detach.cmd`、`R3R_release_fullbuild.cmd`、
  `R3R_release_fullbuild_run.ps1`。
- **明确排除**：所有 `_tmp_*` / `diag_*` / `cleanup*` / `start_batch_*` 临时脚本、`ninja` 可执行文件、
  各 `*.log`、`*.sav`、`R3R_KNOWN_ISSUES.damaged_20260915.bak`、发行 zip，以及上面那个畸形空文件。
  （仓库既无任何 `.zip` 入库先例，故发行包不入库。）
