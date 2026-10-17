# 第 248 轮（2026-10-18）发行准备：v1.0.3 小白鼠版

## 一、本轮目标（玩家原话）

> 还是准备发行版让我的朋友当小白鼠吧（狗头，笑），当然，配套的标记稳定版本 + git 提交 + 制作压缩包准备上传 github release

即四件事：①出可运行的发行 exe ②git 提交 ③打稳定 tag ④打 zip 备用上传 GitHub Release。

## 二、取证：改了什么、编到哪了

| 项 | 实测 | 判定 |
|---|---|---|
| 未提交源码 mtime | `src/couple_group.cpp` 10-16 23:43:54、`yapf_rail.cpp` 23:43:49、`pbs.cpp` 23:45:12、`couple_group_sl.cpp` 23:56:26、`train_cmd.cpp` 23:56:38 | 均属第 245~247 轮 |
| `build\openttd.exe`（Debug/内测） | 51 882 496 B @ **2026-10-17 00:00:54** | **已包含**这批改动（exe 晚于全部源码） |
| `build-release\openttd.exe`（发行） | 23 057 408 B @ **2026-10-03 22:00:33** | **不含**这批改动，落后两个版本 ⇒ 必须重编 |
| `build-release\R3R_release_fullbuild.log` | 130 734 B @ 2026-10-03 22:01:08 | 上次全量构建记录 |
| 已有 tag | `r3r-stable-2026-10-16`（最近）、`-10-03`、`-09-29` … | 命名沿用 `r3r-stable-YYYY-MM-DD` |
| 已存在发行包 | `R3R-v1.0.1-win64-2026-09-19.zip`、`R3R-v1.0.2-win64-2026-10-03.zip` | 本次顺延 **v1.0.3-win64-2026-10-18** |
| 上一版打包目录 | `build-release\pkg_20261003\R3R-OpenTTD-0.73.1-win64\` | 本次复制为 `pkg_20261018\...` 后替换 exe/说明 |
| 本地 git | `feature/decouple`，**ahead 3**（未推送） | 新提交后 ahead 4；push 属对外动作，待玩家明确 |

## 三、已确认的关键事实（决定发行口径）

1. **发行版无探针、且运行期无法打开**。`R3R_PROBES=0` 时探针代码被 `#if R3R_PROBES` 编译期删除，`R3RDbgOn()` 是常量 `false`（`src/train_cmd.cpp:1725-1731`、`r3r_perf.h`），`R3R_DBG/R3R_PERF/R3R_DIAG` 环境变量对它无效。`R3R_release_fullbuild.cmd` 的第 3 道 gate 就是强制 `-DR3R_PROBES_DEFAULT=0`。
   ⇒ 给朋友"当小白鼠"若需要日志，**不能靠发行包**，只能另给 `build\openttd.exe`（Debug+探针，10-17 已编好）；或改 gate 做一版 `PROBES=1` 的 Release 包（需重编，本轮未做）。
   ⇒ 本轮决定：**正式包按既有口径（探针 OFF）**，并在说明里写清"要日志请换诊断包"。
2. `R3R_release_fullbuild.cmd` 是**全量**构建（删除所有 .obj → 693 步 → LTCG 链接，25-50 分钟，`-j2`）。它自带 pre-flight（RAM / openttd.exe 未运行）与 3 道硬 gate。
3. 本轮改动全在 `.cpp`（未碰任何 `src\*.h` / `lang\*.txt`），源码层面本可增量；但脚本为安全起见一律全量删 obj。

## 四、本轮改动的对外可见内容（用于 README「主要变化」）

- **预留账本进存档（KI-371 / R3RL）**：轨道预留的"位→主人+用途"账本随存档保存，读档后不再出现归属丢失（根治跨存档的预留泄漏/幽灵预留）。
- **解挂缝互锁与孤儿预留年龄进存档（R3DS）**：解挂后"等前车离开"的互锁状态、孤儿预留的老化计次随存档延续，避免读档后重复放行或提前回收。
- **段 ID 分配改"最低空闲槽"（第 246 轮 / U-4）**：原 LIFO 空闲表使段 ID 取决于释放顺序，两台机器释放顺序不同就会分到不同 ID（ID 会进存档与段行）⇒ 改为"存活集合的纯函数"，消除一处联机不同步源。
- **耦合目标搜索节流改按 `_tick_counter`（第 246 轮 / 第 4 项）**：原按"调用次数"递减，双方寻路流量不同时节流在不同 tick 到期，导致预订路径不同 ⇒ 改为按 tick 的全局一致窗口。

## 五、执行顺序与状态

- [x] 取证（源码/两树 exe/tag/打包目录/探针口径）
- [x] Release 全量重编（`R3R_release_fullbuild.cmd`，2026-10-18 01:34:13 启动 → 02:00:05 写 done）
- [x] 校验 exe：`EXIT_CODE=0`、`[693/693] Linking`、23 140 864 B @ 2026-10-18 01:59:55（晚于最新源码 10-16 23:56:38）、SHA256 `B5FA0983A929CCFFD89566D277A5B082AB89992A32F71525B365F7C5244C7C20`
- [ ] git 提交（仅源码 5 + 文档，**不 add** `_tmp_*` / `*.log` / `*.zip` / `ninja` / `INCREMENTAL`）
- [ ] 打 tag `r3r-stable-2026-10-18`
- [ ] 复制 `pkg_20261003` → `pkg_20261018`，替换 exe、改写 `README_R3R.txt`
- [ ] 打包 `R3R-v1.0.3-win64-2026-10-18.zip`
- [ ] 回填 exe 体积/SHA256 与构建自证到本文

## 五之二、构建自证（2026-10-18 实测）

| 项 | 实测 |
|---|---|
| 脚本 | `R3R_release_fullbuild.cmd`（复用既有，未新建 `.cmd`） |
| 硬 gate | `/Od` 缺席 / `/O2` 在 / 6 个外部库 define 全在 / `-DR3R_PROBES_DEFAULT=0` 在 ⇒ 全过 |
| 全量步数 | 删除全部 `.obj` 后 `[693/693] Linking CXX executable openttd.exe` |
| 耗时 | 01:35:45 ninja 起步 → 01:59:55 exe 落盘（编译 ~24 min，LTCG 链接 ~4 min） |
| done | `EXIT_CODE=0`（verify 成功分支；`verify` 自带"exe 必须 10 分钟内新链"与"体积 < 35 MB"两道门） |
| 产物 | `build-release\openttd.exe` 23 140 864 B / 22.1 MB |
| 探针 | 编译期删除；发布包不产日志、环境变量无效 |
| 源码新鲜度 | 最新源码 `src\train_cmd.cpp` 2026-10-16 23:56:38 < exe 01:59:55 ⇒ 无陈旧 obj |


## 六、风险与注意事项

- 8 GB 机器：`-j2` + IDE 同时存在有页换冻结史（KI-24），构建期避免再起重型进程。
- 构建期间**不要**读 `build-release\openttd.exe` 或复制该目录（LTCG 链接会覆盖它）。
- 打包时先确认 `R3R_release_fullbuild.done` 里 `EXIT_CODE=0`，且 exe mtime 晚于全部源码 mtime。
- 发行包不入库（仓库无 zip 入库先例），zip 放工作区根目录。
- 本地 ahead 3 未推送；**push 与 GitHub Release 上传均未执行**，等玩家发话。
