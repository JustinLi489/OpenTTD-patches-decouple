# R3R 稳定版备忘 · 2026-10-03

## 1. 本次稳定基线

| 项 | 值 |
| --- | --- |
| git commit | `acc2a4fb6ab53b272073be39a7a96a71c31e88fe` |
| 分支 | `feature/decouple` |
| 规模 | 72 files changed, 16995 insertions(+), 383 deletions(-) |
| 覆盖轮次 | 第 152 ~ 218 轮（2026-09-29 之后累积至今） |
| git tag | `r3r-stable-2026-10-03`（附注标签，指向本备忘的提交） |
| 上一稳定版 | tag `r3r-stable-2026-09-29` → commit `8d10a666f63c4608e4d1af898184811f974212f9`（其前一个提交 `cb997d36c2`） |
| 离线快照 | `build\r3r_stable_2026-10-03\`（openttd.exe + 27 个改动源码 + README.txt） |

提交信息：

```
R3R: 2026-10-03 stable checkpoint - segment traits, couple-pair gates, real-artic rejection (rounds 152-218)
```

## 2. 本批改动主题

1. **段身份与三特质统一走段行**（KI-247/248/253/254）：段单位号/名称/分组先读段行再回退；
   车队列表为隐藏段追加子行与图例；`R3RSegmentHiddenHeads` 在链头段≠控制段时把链头纳入首个子行。
2. **车号 / 车名借用-返还扩展到车库拖动与读档**（KI-261/263）：`R3RBorrowControlTraitsLive()`；
   `name_backup` 进 `R3VP` 稀疏存档块，修掉"耦合态存档→读档→解挂名字丢失"。
3. **挂接分组**：父/子组成员关系（读侧展开，存档只存显式掩码）、直接建子组、取消选中、
   「临时挂接分组」支持 `OT_WAIT_COUPLE`（KI-225/233/256）；站场改为每站台独立场
   （KI-201~204 相关轮次）。
4. **真铰接（真 artic）全面闸门**：配对/提交/翻转/车库/解挂均拒绝焊死链
   （`R3RChainHasRealArticPart`、`FOLDCHK-REFUSE-REAL-ARTIC`、`FLIP-REFUSE-REAL-ARTIC`、
   `CPL-REFUSE-REAL-ARTIC`、`DEPOT-SEG-SKIP-REAL-ARTIC`、`DECOUPLE-SEG-SKIP-REAL-ARTIC`）；
   读档迁移对「★/⊗ + 真铰接」的旧档链自动打散（`LEGACY-ARTIC-DEARTIC`，KI-266/267/273/274）。
5. **挂接穿模（clipping）治理**：`R3R_RESPACE_MAX_ERROR = 8` 给 respace 加错误上界（KI-265 之 R170-A）；
   提交前硬闸门 `COUPLE-REFUSE-STILL-FOLDED`；`FOLDCHK-DIR-OVERRIDE` 前提收紧为
   「不在拼接面 + 两车方向相反才豁免」（第 172 轮）；`RESPACE-BADCHAIN` 探针。
6. **订单索引与归属**：`ORD-PUSH` 把借用期进度回灌所有者（KI-215/215b）、解挂两侧的
   排程归属与等待点规则（KI-215/237）。
7. **站场（station yard）**：每站台独立场、重命名/删除/回落目标（`shared_with`）、
   `SYRD` 存档版本迁移。
8. **探针**：`COUPLE-PREFLIGHT` / `COUPLE-POSTFOLD` / `RESPACE-BADCHAIN` /
   `dump_vehicle` 增补假铰接标志 `H/M/S/E/Z/O`（KI-268，第 177 轮）。

> 其中第 218 轮（`R3R_pair_precheck_round218_memo.md`）**只取证、未改码**：结论是"把可行性验证
> 提前到配对阶段"可行且已有先例（`R3RCoupleTargetAtOrderStation`，第 212 轮），并发现现有
> 配对期真铰接闸门只检查候选方、不检查机车自己，与提交点"任一参与方"的口径不对称 —— 该缺口
> 正是第 172 轮穿模生成链的入口。对应 KI-327（配对期静态预检）/ KI-328（配对拒绝出口），
> 均**待实现**。

## 3. 二进制

| 树 | exe | 大小 | 时间 | 说明 |
| --- | --- | --- | --- | --- |
| `build\`（内测版） | `openttd.exe` | 51 672 576 B | 2026-10-03 19:18:30 | Debug + 探针默认 ON，已编自 `acc2a4fb6a` 全部改动 |
| `build-release\`（发行版） | `openttd.exe` | 22 755 328 B | 2026-09-20 13:53:31 | **旧版**，本次已后台启动全量重编 |

内测版 SHA256：

```
D47B0C0BBDC605BB1025FE433462190C6CAFE3B1DE02587F66AB84DA4C8AF43A
```

每次构建都会重新生成 `rev.cpp` 并重链，因此重建后哈希必变（`ninja -n` 恒含
`FindVersion` + `rev.cpp.obj` + `ottdres.rc.res` + `Linking` 四条边）。

发行版构建入口（存量脚本，未新建）：

```
R3R_release_fullbuild.cmd
  -> build-release\  (RelWithDebInfo + 强制 /O2 /Ob2, -DNDEBUG, -DR3R_PROBES_DEFAULT=0)
  三道硬闸门: 无 /Od、外部库真的编进去 (-DWITH_ZLIB/LIBLZMA/ZSTD/LZO/PNG/OPUSFILE)、探针 OFF
  删光 .obj 后 ninja -j2 (KI-24: 8GB 机器 -j4 会被换页冻死)
  日志 build-release\R3R_release_fullbuild.log
  完成标记 build-release\R3R_release_fullbuild.done (EXIT_CODE=0 才算成功)
```

## 4. 回退步骤

1. **源码**：`git checkout r3r-stable-2026-10-03 -- src`
   （**不要**用 `git reset --hard`；离线副本见 `build\r3r_stable_2026-10-03\src\`）
2. **二进制**：用快照目录里的 `openttd.exe` 覆盖 `build\openttd.exe`
3. **重编**：改过 `src\*.h` 后必须全量重编（KI-15）——
   复用 `R3R_fullrebuild.cmd`（双树菜单）或 `R3R_fullbuild.cmd`（仅内测树，
   自带 `detach` 参数可后台跑）。严禁新建 `.cmd` 入口。

## 5. 未入库（刻意保留在工作区）

- `_tmp_*` / `_qc_*` / `_qcheck*` / `diag_*` / `cleanup*` / `check_task*.ps1` /
  `poll_batch_c.ps1` / `start_batch_*.ps1` / `cpu_delta.ps1` / `sys_info.ps1` /
  `test_vcvars.ps1` / `vcvars_test.*` / `rebuild_R3R.cmd` 等临时脚本
- `ninja`（可执行文件，不入库）、`INCREMENTAL`、`r3r_resv_watch.txt`
- 所有 `*.log` / `*.sav` / 发行 zip / `R3R_KNOWN_ISSUES.damaged_20260915.bak`
- 根目录那个 0 字节畸形文件名 `" R3R_release_build.cmd"`（名字前多一个空格，历史误建，可安全删除）

本次**新增入库**：44 份 `R3R_*_memo.md` / `R3R_segment_identity_decision.md` 分析文档，
以及 27 个 `src\` 源文件改动 + `R3R_KNOWN_ISSUES.md`。

## 6. 本版已知未修项（复测时优先看）

- **KI-327 / KI-328（未实现）**：配对期静态可行性预检 + 配对拒绝出口，必须同批上线。
- **KI-326 关联（未拍板）**：提交点几何预检（拼接点间距容差）。
- **第 172 轮形态**：`FOLDCHK-DIR-OVERRIDE` 已收紧，但"拒绝后每 tick 北推 1px"的重试循环本身仍在。
- **KI-214**：耦合后视觉包围盒偏移（`idx23↔idx24` 假三节接缝），仍为未修。
- 真铰接列车的原生行为（无 ★/⊗）不受本批闸门影响，应在复测中保持逐字不变。
