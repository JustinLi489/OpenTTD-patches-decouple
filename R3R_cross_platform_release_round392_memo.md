# 第 392 轮临时分析报告：R3R 单一 Win11 x64 发行版能否覆盖 Win10 / macOS / Linux

- 日期：2026-11-04
- 轮次：392（承接第 391 轮 KI-558）
- 状态：**论证阶段，未改任何源码、未构建**
- 玩家原话：「R3R 的 win11 单一发行版本无法覆盖 win10、mac、linux 的玩家，想知道解决方案。先论证，后决定」

---

## 一、事实基线（已取证，非推测）

| 编号 | 事实 | 证据位置 |
|---|---|---|
| F-1 | 发布版 = MSVC x64 + Ninja + `RelWithDebInfo` + `/O2 /Ob2 /DNDEBUG`，产物在 `build-release\openttd.exe` | `R3R_release_build.cmd:130-138` |
| F-2 | 依赖库全部**静态**链入：triplet = `x64-windows-static`，覆盖 zlib / liblzma / zstd / lzo / png / opusfile，脚本内还有硬闸门（缺任一 `-DWITH_*` 就中止） | `R3R_release_build.cmd:138`、`:157-170` |
| F-3 | CRT 也走静态（CMakeLists `if(MSVC)` 段把 `-static` 目标切到 `/MT`），脚本另做 `ucrtbased/vcruntime140d/msvcp140d` 字面量检查 | `CMakeLists.txt:315-319`、`R3R_release_build.cmd:198` |
| F-4 | DPI manifest = `True/PM` + `PerMonitorV2,PerMonitor`，`supportedOS` 列到 Windows 10 GUID（Win11 沿用同一 GUID） | `os/windows/openttd.manifest:12-25` |
| F-5 | 上游跨平台分支**完整保留**：`if(APPLE OR UNIX)`、SDL2/Allegro、Fcitx、Iconv、AudioToolbox 一个都没删 | `CMakeLists.txt:51-52`、`:129-146`、`:178-180`、`:206-219`、`:419-421` |
| F-6 | Win32 专有 API（`windows.h` / `GetTickCount` / `OutputDebugString` / `_stricmp` 等）**只出现在平台层**：`src/os/windows/*`、`src/video/win32_v.cpp`、`src/music/win32_m.cpp`、`src/sound/win32_s.cpp`、`src/fileio.cpp`。R3R 的列车/排程/段/挂接逻辑代码（`train_cmd.cpp`、`couple_group.cpp`、`order_gui.cpp`、`depot_gui.cpp` 等）零命中 | 全仓搜索，命中文件清单见本轮取证 |
| F-7 | R3R 探针日志用标准 C 的 `fopen/fprintf`（`r3r_perf.h` 的 `R3RDbgWrite`），无 Win32 文件 API | `src/r3r_perf.h:250-271`、`:404-406` |

**F-1~F-7 的直接推论**：源码层面没有把项目钉死在 Windows 上；有问题的只是"我目前只编了一个 Windows 包"这件事。

---

## 二、三条轴分开论证（关键：这三件事的性质完全不同）

### 轴 1：Windows 版本（Win10 ← 同一 exe）

结论：**这不是"包覆盖不了"，而是"需要定性"的兼容性问题。目前证据不支持"必须出 Win10 专包"。**

依据：
1. Win10 与 Win11 同为 NT 10.0 内核家族，同一 PE、同一 x64 ABI、静态 `/MT`、无外部 CRT 依赖（F-2/F-3）⇒ 不存在运行库层面装不上的问题。
2. manifest 的 `supportedOS` 到 Win10 GUID 为止（F-4），Win11 走同一 GUID，不存在"显式声明只支持 Win11"导致 Win10 拒绝加载的机制。
3. 静态导入 Win10 之后新增的 API 会让进程**启动即失败**（缺 DLL/入口点），而不是"少了某个 GUI"。现象是"游戏能开、某一个界面看不到"，这与"二进制不兼容"的形态不符。
4. **反面证据（重要）**：第 388/389 轮朋友那台机器能跑 v1.0.4 并产出 `crash.sav` 与崩溃日志——说明该 exe 在非本机环境是能进入主循环的。若该机是 Win10，则"Win10 跑不了这个包"这个前提本身需要修正。
5. 真正高概率的根因是**显示/配置/资源层面的差异**，按概率排序：
   - a) 分辨率 + DPI 缩放：窗口按固定像素布局，容器 `EqualSize` 会被最宽子项撑宽（本仓已有前例 KI-262：订单窗口被 5×120px 撑到 600px）。小屏/125%~150% 缩放下最右侧控件被挤出可视区 ⇒ 玩家描述成"找不到这个 GUI"。
   - b) `openttd.cfg` 里的 `gui.*` / `interface.scale` / 字体设置不同（不同机器配置不同，同一存档也一样）。
   - c) 语言不同：中文 lang 缺串时控件显示空白或 `(invalid parameter)`，看起来像"没有"。
   - d) 视频后端差异：Win10 那台若 OpenGL 不可用而回落到 8bpp 软件渲染，R3R 新增的带 alpha / 非调色板 sprite 可能不可见（**未验证，列为疑点**）。
   - e) 玩家场景/存档不同：依赖数据的 GUI（站场面板、挂接分组树、段子行）只在存在 yard / 分组 / 多段链时才出现。

### 轴 2：macOS / Linux

结论：**物理上不可能靠一个 exe 覆盖，必须重编译；但"能不能编"与"编出来谁维护"是两个不同问题。**

- 不可覆盖的硬原因：可执行格式（Mach-O / ELF vs PE）、图形后端（Cocoa / SDL2 vs Win32 GDI）、音频后端（AudioToolbox / SDL2 / ALSA vs XAudio2）、系统字体与 Unicode 栈（CoreText / fontconfig vs Uniscribe）全部不同。只能 Wine/CrossOver/虚拟机这类间接路径，不适合作为正式分发方案。
- 可编的有利条件：F-5/F-6/F-7 ⇒ 上游跨平台代码与 CMake 分支完整，R3R 改动在游戏逻辑层，探针用标准 C 文件 IO。
- 可编的不确定项（首次编 Linux/mac 必然踩到的）：
  - U-1 MSVC 宽松语法（`#pragma warning`、`__declspec`、非标准 `for` 作用域、`long` 长度假设）在 GCC/Clang 下报错；`-fpermissive` 之类救不了全部。
  - U-2 依赖可用性：Linux 需要 `libsdl2-dev / libpng-dev / liblzma-dev / libzstd-dev / liblzo2-dev / libopusfile-dev / libfreetype-dev`；macOS 走 Homebrew 或 vcpkg，arm64/x64 双架构（Apple Silicon 需 arm64 或 Rosetta）。
  - U-3 macOS 分发门槛：未签名 + 未公证（notarization）的 .app 会被 Gatekeeper 拦截，需要 Apple Developer 账号（$99/年）或教玩家右键打开/`xattr -d`。
  - U-4 存档与 NewGRF 兼容性：`.sav` 本身跨平台，但压缩后端版本（zstd/lzma）与 `R3VP/R3SG/SYRD` 等 R3R 自定义块必须三平台编自同一 commit，否则会出现"别的平台读不了存档"的伪跨平台 bug。
  - U-5 探针日志路径：`R3R_debug.log` 写在进程工作目录（`r3r_perf.h`），Linux 上工作目录通常是 `/` 或 `~`，需要确认可写，否则探针静默失效。

### 轴 3：分发物料（最容易被忽略、却最常被误判成"平台问题"）

一个 OpenTTD 发行包 ≠ 一个 exe。缺任何一项，玩家看到的现象都像"这个版本缺东西"：
- base graphics / sound / music set（OpenGFX / OpenSFX / OpenMSX）——缺了只能进"无图形"界面；
- 语言包（`.lng` 已随构建生成，但若只发 exe 而玩家旧版 data 目录里的 lang 是旧的，新串会显示成编号）；
- R3R 依赖的 NewGRF（若玩法依赖特定 GRF，必须一并说明或打包）；
- Windows 上的 `baseset/`、`lang/` 目录结构与 `openttd.cfg` 生成位置（`Documents\OpenTTD`）。

⇒ **建议：把"发一个 exe"改成"发一个 zip/安装包（CPack）"**，这是当前性价比最高的第一步，且对 mac/Linux 同样适用。

---

## 三、方案选项（待玩家拍板，本轮不实施）

| 方案 | 内容 | 覆盖 | 成本 | 风险 |
|---|---|---|---|---|
| **A. 只收口 Windows** | ①把 Win10 那个 GUI 缺失定性（UI/DPI/语言/后端，而非 ABI）；②显式声明最低 Windows 版本（如 Win10 1809+）；③出 CPaack zip 含 baseset/lang | Win10 + Win11 | 低（1~2 轮） | mac/Linux 玩家仍不能玩 |
| **B. 三平台 CI 矩阵**（推荐目标态） | GitHub Actions：`windows-2022`(MSVC) / `ubuntu-22.04`(apt+SDL2) / `macos-14`(brew)；同一 commit 出三个 artifact；CPack 打包 | 三平台 | 中高（首次需修可移植性 U-1/U-2，约 3~8 轮；后续每版自动） | macOS 签名/公证（U-3）；CI 只能跑 dedicated/无头，GUI 仍要真人复测 |
| **C. 源码 + 社区打包** | 不发原生包，只发源码 tag + 构建说明，让 Homebrew / Flathub / AUR 打包者接手 | 三平台（滞后） | 最低 | 玩家门槛高、版本不同步、R3R 魔改没人接 |

**建议路径**：先 A（把 Win10 这一格钉死，同时把发包形式升级成 zip）；确认玩家盘子后再上 B。B 的第一阶段可以只出 Linux x64（成本最低、无签名问题），macOS 放到第二阶段。

---

## 四、疑点登记（REG-392-*）

- **REG-392-A**：朋友的机器是否真是 Win10？该机已能运行 v1.0.4 并产出 `crash.sav`（第 388/389 轮）⇒ 若是 Win10，则"Win11 包在 Win10 上不可用"这一前提**部分自证伪**，应改为"某个 GUI 不可见"。需要：`winver` 截图 / `Windows 版本` 文字 + 具体 GUI 名称 + 屏幕分辨率与缩放比例 + 该机 `openttd.cfg` 的 `gui.*` 段。
- **REG-392-B**：该 GUI 是"整个窗口/按钮不存在"还是"存在但看不见/被裁掉/文字空白"？三种形态指向完全不同的根因（窗口未注册 / 布局被挤出 / 字符串缺失）。
- **REG-392-C**：该机视频后端是否为 OpenGL（`openttd.cfg` 的 `video_driver`），是否回落到 8bpp 软件渲染。若回落，新增 GUI 的 alpha sprite 可能不可见（未验证）。
- **REG-392-D**：第 391 轮及之前是否有任何 R3R 代码引入 Win11-only API（本轮只做了 `windows.h/GetTickCount/...` 的文件级扫描，未做 API 级扫描）。待办：对 `src/os/windows/*` 全量列 API 名与最低 OS 版本。

---

## 五、待玩家决定的问题（编号回填）

- **C-1** 目标覆盖范围：只要 Win10+Win11（走 A）？还是必须原生 mac/Linux（走 B）？还是愿意只发源码（走 C）？
- **C-2** 若走 B：接受 GitHub Actions 公共仓库（或私有仓库的免费额度）作为构建机吗？每版发布频率大概多少（决定 CI 值不值得做）？
- **C-3** 若走 B：macOS 包是否要求签名+公证（$99/年 + 每次发版走 notarytool）？不签则首次打开需玩家手动绕过 Gatekeeper。
- **C-4** 是否同意把发布形态从"单个 exe"升级为"zip 安装包（含 baseset/lang/README）"？

---

## 六、本轮结论（一句话）

「一个 Win11 包通吃三平台」不成立，但**当前证据也不支持"Win10 必须单独出包"**：Win10 那一格更像 GUI/显示层面的可修缺陷，mac/Linux 才是真正需要重新构建的两格——先定性 Win10（1~2 轮），再决定是否为 mac/Linux 建三平台 CI（3~8 轮）。
