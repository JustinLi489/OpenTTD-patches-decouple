# R3R 稳定版备忘 · 2026-10-16

## 1. 本次稳定基线

| 项 | 值 |
| --- | --- |
| git commit | `fbbc12d1deb72aa843dbeac7dc0fd6d82e78ef56` |
| 分支 | `feature/decouple` |
| 提交时间 | 2026-10-16 23:12:01 +0800 |
| 规模 | 48 files changed, 13411 insertions(+), 336 deletions(-)（24 个 `src\` + 24 个文档） |
| 覆盖轮次 | 第 219 ~ 244 轮（2026-10-06 之后累积至今） |
| git tag | `r3r-stable-2026-10-16`（附注标签，指向本备忘的提交） |
| 上一稳定版 | tag `r3r-stable-2026-10-03` → commit `acc2a4fb6ab53b272073be39a7a96a71c31e88fe`（其备忘提交 `d57768902f`） |
| 离线快照 | `build\r3r_stable_2026-10-16\`（`openttd.exe` + 24 个改动源码 + `README.txt`） |

提交信息：

```
R3R: 2026-10-16 stable checkpoint - reservation ledger audit, native resv pathfinding fixes,
     fake-dualhead probes, depot couple fixes (rounds 219-244)
```

## 2. 本批改动主题

1. **非列车车库整数除零**（KI-329，第 219 轮）：`depot_gui.cpp` 的绘制与命中检测误用
   窗口级 `resize.step_width/step_height`（标题栏子件未 `SetResize` ⇒ `lcm(1,0)==0`，
   横向步长恒为 0），全部改用矩阵控件自身的 `resize_x/resize_y`，并补两道护栏。
2. **机车卡住 / 卡住显示**（KI-330/347/348，第 220~222、241~243 轮）：解挂缝豁免
   （同格 + `cur_speed==0` 不视为阻塞）落地并实机收口；`SPD-DROP` / `CPL-S0-CHK` /
   `LOCO-MOVE` 只读探针把"停车后不再重试预留"定性为结构性分支而非显示 bug；
   第 243 轮进一步证伪/证实红灯前原地踏步（`reverse_at_signals=false`）。
3. **克隆**（第 225~227 轮）：KI-332 段身份层（段 ID 行 / 段名 / 分组 / 优先级）随克隆继承
   （`R3RCopyClonedSegmentTraits` + `R3RSettleClonedChain` + `CLONE-TRAIT*` 探针）；
   KI-333 隐藏段名字去重；KI-337 克隆多段链后隐藏段排程未继承。
4. **双头机车 / 假双头**（KI-334/335/343，第 228、233~235 轮）：双头成对只读探针；
   解挂后两半启动互锁 `DECOUPLE-INTERLOCK`（后车等到前车离站台才起步，实机复测通过）；
   `FakeDualHead`（`VehicleRailFlag::FakeDualHead=28`）解耦"段端必须引擎"与"双头后半
   不能被打成引擎"两条互斥约束，`IsRearDualheaded()` 全部改判 `IsDualHeadRear()`。
5. **库型 `OT_GOTO_COUPLE`**（KI-338/339，第 229~233 轮）：从"到库后什么都不发生"
   → 冻结解除（track 01→20→10、spd 61→0→41→56→54）→ 新阻塞 `SPLICE-GAP-REJECT`
   （命中端与拼接端角色错位：命中用运动车头 ↔ 某节，拼接用 `Last()` ↔ `First()`）；
   第 233 轮落地"整列进库再合链"。
6. **预留台账 / 原生预留寻路**（第 237~243 轮，`pbs.cpp` +591 行为本批最大头）：
   - KI-345 孤儿预留走廊定性 + 自动 `reaped=20`（实机通过），并说明"为什么必须等 ~2 分钟"；
   - KI-346 **根因做实并修复**：`yapf_rail.cpp` 的 `rp()` 把"直行假想轨"
     `TrackdirToTrack(ReverseTrackdir(cur_td))` 当成"实际进入轨"（该式恒等于
     `TrackdirToTrack(cur_td)`），双槽格必订出一条车根本不走的轨 ⇒ 孤儿走廊 20 格，
     改用跟随器算出的 `FindFirstTrackdir` 实际轨道；
   - KI-360/361 账本式预留归属（层①②③）落地；
   - KI-362/363/364 账本式清理"自己养出三条地鼠"→ 第 243 轮续二十七升级为
     **按用途计数的所有权表**（`_r3r_resv_by_owner`）；
   - KI-368 `first_shared` 悬垂（use-after-free，崩溃点 `station_cmd.cpp:5124`）修复。
7. **审计 / 清理工具**：`RESV-AUDIT*` / `RESV-REAP` / `RESV-PURGE` / `RESV-GC`，
   手动清理控制台命令 `r3r_resv_purge`，`CLEAR-STN` 等探针。
8. **假铰接可见性**（KI-268，第 177 轮，随本批入库）：`dump_vehicle` 的 `tf:` 段补充
   `H=ArticGroupHead / M=ArticGroupMember / S=SegmentFront / E=SegmentBack /
   Z=SegmentFlipped / O=ForceReserveOnce`，让"真铰接 / 假铰接"可直接判读。

## 3. 二进制

| 树 | exe | 大小 | 时间 | 说明 |
| --- | --- | --- | --- | --- |
| `build\`（内测版） | `openttd.exe` | 51 868 160 B | 2026-10-16 21:17:44 | Debug + 探针默认 ON，已编自 `fbbc12d1de` 全部改动 |

内测版 SHA256：

```
01EBA20AD35AB21D07F1126559A7788455E77223D6689A1CCFE76FC79B2450A8
```

离线快照有效性自证（exe 不陈旧）：

- `build\openttd.exe` 21:17:44 **晚于** `src\pbs.cpp` 21:13:59、
  `src\pathfinder\yapf\yapf_rail.cpp` 21:01:33、`src\train_cmd.cpp` 18:22:01；
- 对应 obj 晚于其 `.cpp`：`pbs.cpp.obj` 21:16:16 > 21:13:59，
  `yapf_rail.cpp.obj` 21:01:56 > 21:01:33。

本次**未**启动 `build-release\` 全量重编（发行版入口 `R3R_release_fullbuild.cmd` 存量保留，
需要时再跑）。

## 4. 回退步骤

1. **源码**：`git checkout r3r-stable-2026-10-16 -- src`
   （**不要**用 `git reset --hard`；离线副本见 `build\r3r_stable_2026-10-16\src\`）
2. **二进制**：用快照目录里的 `openttd.exe` 覆盖 `build\openttd.exe`
3. **重编**：复用既有工作流脚本，严禁新建 `.cmd` 入口
   - 增量：`_tmp_inc_build.cmd`（自带 `R3R_inc_guard.ps1` 护栏；本机 msvc 依赖跟踪
     已恢复，改 `src\*.h` / `src\lang\*.txt` 后通常也能直接增量，护栏判定必须全量时
     它会自动清 obj）
   - 全量：`R3R_fullbuild.cmd`（仅内测树，自带 `detach` 可后台跑）或
     `R3R_fullrebuild.cmd`（双树菜单）

## 5. 未入库（刻意保留在工作区）

- `_tmp_*` / `_qc_*` / `_qcheck*` / `_qcheck_*` / `diag_*` / `cleanup*` / `start_batch_*` 等临时脚本
- `ninja`（可执行文件）、`INCREMENTAL`、`_r3r_targets.txt`、`r3r_resv_watch.txt`
- 所有 `*.log`（含 `R3R_debug.log` / `R3R_debug_round243.log` / `R3R_perf.log`）/ `*.sav`
- 发行 zip（`R3R-v1.0.1-win64-2026-09-19.zip` / `R3R-v1.0.2-win64-2026-10-03.zip`）
- `R3R_KNOWN_ISSUES.damaged_20260915.bak`
- 根目录那个 0 字节畸形文件名 `" R3R_release_build.cmd"`（名字前多一个空格，历史误建，可安全删除）
- 快照目录本身 `build\r3r_stable_*\`（既有先例：不入库）

本次**新增入库**：22 份 `R3R_*_memo.md` 分析文档 + `R3R_fake_dualhead_plan.md` 更新 +
`R3R_KNOWN_ISSUES.md`（+2118 行）+ 24 个 `src\` 源文件改动。

## 6. 下一批工作（本稳定点存在的原因）

**第 1 项 (C)「预留台账入档」**：当前台账 `_r3r_resv_by_owner` 是内存态（NOSAVE），
读档后台账为空、审计与清理失去归属依据。玩家已授权**只入档 ledger 一本**，
本轮先钉稳定点再动手（该改动要给存档新增块 `R3RL`，风险较高）。

- 范围：`R3R_desync_round244_memo.md` §16.5
- 蓝图：`R3R_desync_round244_memo.md` §16.6
  - `src\pbs.cpp`：新增 `#include "sl/saveload.h"`、`R3RResvLedgerSaveRec { uint32_t owner; uint8_t purposes; }`、
    `_r3r_resv_ledger_desc[]`、`Save_R3RL()` / `Load_R3RL()`；
    `static constexpr uint8_t R3R_RESV_SLOT_WHOLE_TILE = 7;`（与 `train_cmd.cpp:15044` 的 `7` 共用）
  - `src\sl\couple_group_sl.cpp`：注册表 `:341-346` 追加
    `{ 'R3RL', Save_R3RL, Load_R3RL, nullptr, nullptr, CH_SPARSE_TABLE }`，
    顶部手写 `void Save_R3RL(); void Load_R3RL();` 声明（**避免改 `pbs.h`**，
    以免触发全量重建）
  - 只改 `.cpp` ⇒ KI-183 护栏判定增量合法
- 边界：`_r3r_consist_resv` 与老化计数本轮**不动**
- 待验证项 **V-1**：`R3RL` 是四字符块 ID，**不保证以文本落在 exe 中**，
  不能用 `findstr` exe 自证；判据改为「新存档文件字节含 `R3RL`」+「双进程联机
  读档后台账不再为空」（§十五方案）

## 7. 本版已知未修项（复测时优先看）

- **库型挂接 `SPLICE-GAP-REJECT`**（KI-340，高）：命中端与拼接端角色错位，
  修法 A/B/C 仍未拍板；`FOLDCHK-SKIP v_head_not_engine` 每 tick 刷屏。
- **KI-334 双头机车逻辑翻**：只覆盖 `respace-post` 一条出口，翻转/物理翻零样本，
  收口条件=必须让"含双头机车的链真的走一次逻辑翻或物理翻"。
- **KI-330 解挂缝**：已落地豁免并实机通过，但"解挂后两半同格共存"的上游落点算法未改。
- **KI-368 关联**：`first_shared` 悬垂已修，但 KI-367 方案 1 的止血效果需复测确认。
- **KI-214**：耦合后视觉包围盒偏移（`idx23↔idx24` 假三节接缝）仍为未修。
- **KI-327 / KI-328**（第 218 轮）：配对期静态可行性预检 + 配对拒绝出口，仍**待实现**。
- 真铰接列车的原生行为（无 ★/⊗）不受本批闸门影响，应在复测中保持逐字不变。
