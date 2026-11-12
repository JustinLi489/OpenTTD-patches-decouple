# R3R 稳定版发布记录（2026-11-13）

本轮**零源码改动**，只执行四件事：git 源码提交（不 push）、打稳定 tag、无探针口径发行版编译、发行版压缩包打包。

## 一、源码提交

| 项 | 值 |
| --- | --- |
| commit | `275d234ccfb10f4d8d427f0898c647963bf0f482` |
| message | `R3R: rounds 407-429 checkpoint (2026-11-13)` |
| 规模 | 63 files changed, +14901 / −296 |
| 上一提交 | `6335aa70c2` 2026-11-08 18:28（tag `r3r-stable-2026-11-08`） |
| push | **未 push**（按要求只在本地） |

入库内容：

- 源码：42 个改造文件（`src/train_cmd.cpp`、`src/vehicle_base.h`、`src/order_*.{h,cpp}`、`src/station_*`、`src/sl/*`、`src/pathfinder/yapf/yapf_rail.cpp`、`src/lang/{english,simplified_chinese}.txt` 等）
- 新增源码：`src/r3r_schedule_gui.cpp`、`src/r3r_schedule_gui.h`、`src/widgets/r3r_schedule_widget.h`
- 构建脚本：`R3R_inc_guard.ps1`、`R3R_release_build.cmd`、`R3R_release_fullbuild.cmd`（+57 / −4）
- 文档：19 份 `R3R_*.md` 备忘 + `R3R_KNOWN_ISSUES.md`（含第 407~429 轮条目）

刻意**未**入库（避免污染仓库）：`*.zip`、`R3R_debug.log` / `R3R_perf.log`、`_qc_*.txt`、`diag_*.ps1`、`*.r3r_bak` / `*.bak`、`ninja`、`INCREMENTAL`、`build1.zip`。

## 二、稳定 tag

```
r3r-stable-2026-11-13 -> commit 275d234c
```

- **轻量 tag**（`objecttype=commit`），与既有 `r3r-stable-2026-11-01/04/08` 口径一致。
- 回退：`git checkout r3r-stable-2026-11-13 -- src`（**不要** `git reset --hard`）。

## 三、发行版编译（无探针运行口径）

入口：`R3R_release_fullbuild.cmd`（既有唯一发布入口，未新建任何脚本），全量重建 709 步，`-j2`（KI-24）。

| 判据 | 实测 |
| --- | --- |
| 完成标记 | `build-release\R3R_release_fullbuild.done` = `EXIT_CODE=0` |
| 链接行 | 日志尾部 `[709/709] Linking CXX executable openttd.exe` |
| 错误计数 | `error C` / `fatal error` / `FAILED:` / `build stopped` = **0** |
| 产物 | `build-release\openttd.exe` = 2026-11-13 03:10:26，**23 724 032 B**（v1.0.5 为 23 588 352 B） |
| 版本串 | `Version string: r3r-stable-2026-11-13, Release: 0.73.1`（configure 自动套用本轮 tag） |
| 优化选项 | `FLAGS = /Zi /O2 /Ob2 /DNDEBUG -std:c++20 -MT ...`；`build.ninja` 内 `/Od` 计数 **0** |
| 外部库 | `DEFINES` 含 `WITH_ZLIB WITH_LIBLZMA WITH_ZSTD WITH_LZO WITH_PNG WITH_OPUSFILE WITH_OPENGL` |
| 探针宏 | `R3R_PROBES_DEFAULT=0` + `R3R_PROBES=1` |
| 语言包 | `lang\*.lng` 66 个，均为本轮 02:39 重新生成（`simplified_chinese.lng` 229 687 B） |

### 「无探针」口径澄清

`R3R_PROBES_DEFAULT=0` 的含义是**运行时默认关闭**：不采集、不写 `build\R3R_debug.log`、无常驻开销；探针代码仍编入二进制，排查时可用 `R3R_DBG=1` 打开。这与 v1.0.1~v1.0.5 全部历史发布包一致（实测旧 exe 内仍可检索到 `COUPLE-OK` / `RESV-GHOST` / `ARTIC-SKIP-R3R` / `R3R_DBG` 字面量）。

若需要「探针代码完全不编入」（`-DR3R_PROBES=0`）的版本，需重新 configure + 全量重编，本轮按既有发布口径未做。

## 四、发行版打包

| 项 | 值 |
| --- | --- |
| 文件 | `R3R-v1.0.6-win64-2026-11-13.zip` |
| 大小 | 16 333 518 B（v1.0.5 = 16 086 011 B） |
| 条目 | **135**（v1.0.5 = 136） |
| 顶层目录 | `R3R-OpenTTD-0.73.1-win64\` |

包内构成：

- `openttd.exe`（本轮 release 产物）
- 上游原有文档：`changelog.md`、`jgrpp-changelog.md`、`COPYING.md`、`README.md`
- 资源目录：`ai\`、`baseset\`、`game\`、`lang\`、`scripts\`

### 刻意不放入 `README_R3R.txt`

按玩家 2026-11-13 明确要求（"不用添加你写的教程，玩家抱怨 AI 味道很大，虽然我不理解，但是我到时候会写手写的"），本版发行包**不附任何 AI 撰写的说明文档**；v1.0.5 内的 `README_R3R.txt`（5 195 B）因此消失，条目数 136→135，其余结构逐项对应。

## 五、边界与未做

- 未 push 远端（按要求）。
- 打包文档未包含玩家手写说明（待玩家补充后重打即可，目录留存在 `build-release\pkg_20261113\`）。
- 本轮未做游戏内复测；KI-623 / KI-624（第 429 轮）仍为「已实现 + 已编译，游戏内复测待做」。
- 发行版与内测版差异仅优化级别与探针默认值；内测版 `build\openttd.exe` 仍为 2026-11-13 02:21:02（52 867 072 B）。
