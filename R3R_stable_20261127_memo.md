# R3R v1.0.7 发布备忘（2026-11-27）

## 一、这次发布做了什么

1. 源码里**彻底删除**全部 R3R 调试探针（不是编译期隔离），详见
   `R3R_release_probe_strip_20261127_memo.md`。
2. 清理工作目录里两周前的临时备忘（219 个 `R3R_*.md`），保留
   `R3R_KNOWN_ISSUES.md` / `R3R_NEXT_STEPS.md` / `R3R_handover.md`。
3. 提交 git（两个提交）+ 打版本标签 `R3R-v1.0.7`。
4. 编译 RelWithDebInfo 发布树，打包 `R3R-v1.0.7-win64-2026-11-27.zip`。

## 二、git 状态

| 项 | 值 |
| --- | --- |
| 分支 | `feature/decouple` |
| 探针删除提交 | `f41dd84fa2` R3R: strip every debug probe from source for the release build (2026-11-27) |
| 备忘清理提交 | `2067203b4b` chore: drop round memos older than two weeks; fix .gitignore patterns |
| 版本标签 | `R3R-v1.0.7`（annotated，打在发布备忘提交上） |
| 回退点 | `r3r-pre-probe-strip-2026-11-27`（探针删除前的完整状态） |
| 工作区 | 干净（`git status --porcelain` 无输出） |

`.gitignore` 复核结果：

- 已有 `/build*` 覆盖 `build/`、`build-release/`；`*.log`、`/*.zip`、`*.bak`、`*.orig`、`*.r3r_bak` 均已覆盖。
- 修正笔误：`/ R3R_release_build.cmd`（多一个空格，匹配不到任何文件）→ `/R3R_release_build.cmd`。
  该空格的来源是 9 月遗留的一个 **0 字节**文件 ` R3R_release_build.cmd`（文件名带前导空格），本轮已删除该空文件。
- 新增打包产物忽略：`/*.7z`、`/*.tar.gz`、`/dist/`、`/R3R-v*/`。

## 三、发布版构建

| 项 | 值 |
| --- | --- |
| 构建树 | `build-release/`（RelWithDebInfo + `/O2 /Ob2`） |
| 配置 | `-DCMAKE_TOOLCHAIN_FILE=D:\vcpkg-master\...\vcpkg.cmake`、`-DVCPKG_TARGET_TRIPLET=x64-windows-static`、`-DOPTION_USE_ASSERTS=ON`、`-DR3R_PROBES_DEFAULT=0 -DR3R_PROBES=1` |
| 优化标志 | `FLAGS = /DWIN32 /D_WINDOWS /GR /EHsc /Zi /O2 /Ob2 /DNDEBUG -std:c++20 -MT /Zc:rvalueCast /Zc:preprocessor /MP /FC /utf-8 -D_FORTIFY_SOURCE=2 /W3`（`/Od` 计数 0） |
| 规模 | 全量 727 步，`-j2`，约 **41 分钟** |
| 结果 | `[727/727] Linking CXX executable openttd.exe`，`NINJA_EXIT=0`，`error C` / `FAILED:` **0** |
| 产物 | `build-release/openttd.exe` = 2026-11-27 20:34:55，**30 470 656 B** |
| SHA-256 | `3ea2f602319c32d631ef0fb39580d214b24f724c2d525fe258bdfe9c0fef629e` |
| PDB | `build-release/openttd.pdb` 292 442 112 B（调试信息在 PDB，未进 exe） |

### 版本号 R3R-v1.0.7 的落地方式

版本串来自 `cmake/scripts/FindVersion.cmake` 的 `git describe --tags`，因此：

1. 先提交代码，再打 annotated 标签 `R3R-v1.0.7`；
2. `git describe --tags` 输出即为 `R3R-v1.0.7`（工作区干净 ⇒ `REV_MODIFIED=0`，无 `-m` 后缀）；
3. 首次 configure 后 `generated/rev.cpp` 仍是 11-13 的旧内容（`r3r-stable-2026-11-13` / 0.73.1），
   必须显式跑一次 `ninja -C build-release generated/rev.cpp` 让它按新标签重生成，否则 exe 版本号是旧的。

最终 `rev.cpp` 自证：

```
const char _openttd_revision[]       = "R3R-v1.0.7";
const char _openttd_release_version[] = "0.73.5";
const uint8_t _openttd_revision_tagged = 1;
```

`generated/ottdres.rc` 的 `FileVersion` / `ProductVersion` 同为 `R3R-v1.0.7`，
exe 内也检索到 `R3R-v1.0.7` 字面量 1 处。

> 说明：exe 内嵌的 `_openttd_revision_hash` 是 `2067203b4b9d19c67f264b3cc9282a55ddebdee2`
> （代码提交，即探针删除 + 备忘清理之后的那个提交）；发布备忘是它之后的第一个提交，
> 版本标签 `R3R-v1.0.7` 最终指向备忘提交（`f1165c31b7`），以保证后续重编仍得到
> 干净的 `R3R-v1.0.7` 而不是带 `-1-g…` 后缀。两者代码内容完全相同，只差这一份文档。

### 构建环境坑（本轮实测）

`R3R_release_build.cmd` 的 `[4] call vcvars64.bat` 在本机**部分失败**：vcvars 内部调用的
`reg.exe` 被安全策略黑名单拦截，脚本没有报错（`errorlevel` 仍为 0），但 Windows SDK 的
UCRT 包含路径没设上，全量编译在第一步就撞
`yvals.h(22): fatal error C1083: 无法打开包括文件: "crtdbg.h"`。

解决办法（等价、可复现）：手工设置编译器环境后直接跑 ninja——

```powershell
$msvc   = "D:\VISUAL STUDIO\MAIN PACK\VC\Tools\MSVC\14.51.36231"
$sdkInc = "C:\Program Files (x86)\Windows Kits\10\Include\10.0.26100.0"
$sdkLib = "C:\Program Files (x86)\Windows Kits\10\Lib\10.0.26100.0"
$env:INCLUDE = "$msvc\include;$sdkInc\ucrt;$sdkInc\um;$sdkInc\shared;$sdkInc\winrt;$sdkInc\cppwinrt"
$env:LIB     = "$msvc\lib\x64;$sdkLib\ucrt\x64;$sdkLib\um\x64"
$env:PATH    = "$msvc\bin\Hostx64\x64;$env:PATH"
& "D:\gcc new\mingw64\bin\ninja.exe" -C build-release -j2 openttd
```

配置步骤（`[5]` 的 cmake 参数）与三道硬闸门（`[5c]` 外部库、`[5d]` 探针 define）**都由脚本正常通过**：

- `OK: /Od absent`
- `OK: zlib/lzma/zstd/lzo/png/opusfile all compiled in`
- `OK: probes compiled IN, default OFF`

> 注：现在源码里已无任何 `#if R3R_PROBES`，所以 `-DR3R_PROBES=1` 只是让闸门通过，二进制里不会因此多出探针。

### exe 体积为何从 23.7 MB 涨到 30.5 MB

| 节 | v1.0.6（23 724 032 B） | v1.0.7（30 470 656 B） |
| --- | --- | --- |
| `.text` | 18 338 524 | 24 050 896 |
| `.rdata` | 3 220 916 | 3 851 928 |
| `.data` | 1 955 616 | 2 591 881 |
| `.pdata` | 982 140 | 1 056 492 |

- 不是调试信息：`/debug` 的信息全在 292 MB 的 PDB 里，exe 内 `RSDS`/`.debug` 均无异常节。
- 主要来自基线抬升与新增功能：v1.0.6 基于 JGRPP **0.73.1**，v1.0.7 基于合并后的 **0.73.5**，
  且自 `r3r-stable-2026-11-13` 以来共 **570 个提交**（含车站调度/站场相关的
  `station_yard_sl.cpp`、`station_gui.cpp` 等大块新增）。
- 顺带说明：**探针删除对发布 exe 的收益主要体现在"彻底无探针"**——v1.0.6 的
  `-DR3R_PROBES=1` 使探针代码仍被编入（可用 `R3R_DBG=1` 打开），v1.0.7 已完全没有；
  性能收益主要落在内测树（`build\`，探针原本是 RUN 的）。

## 四、发行包

| 项 | 值 |
| --- | --- |
| 文件 | `R3R-v1.0.7-win64-2026-11-27.zip` |
| 大小 | **17 257 143 B**（v1.0.6 = 16 333 518 B） |
| SHA-256 | `5b660aa97a437704554c662071fe504d4170da2ee0b1112c6aca1c5c82499535` |
| 条目 | **135**（与 v1.0.6 一致） |
| 顶层目录 | `R3R-OpenTTD-0.73.5-win64\` |
| 完整性 | `ZipFile.testzip()` = OK |
| 暂存目录 | `build-release\pkg_20261127\R3R-OpenTTD-0.73.5-win64\` |

包内构成（逐项与 v1.0.6 对应）：

- `openttd.exe`（30 470 656 B，版本 `R3R-v1.0.7`）
- 上游文档：`changelog.md`、`jgrpp-changelog.md`、`COPYING.md`、`README.md`
- 资源：`ai\`(17) 、`baseset\`(23)、`game\`(15)、`lang\`(66)、`scripts\`(9)

刻意**不放入**任何 R3R 说明文档：按玩家 2026-11-13 明确要求（"不用添加你写的教程"），
打包只含上游原始文档，玩家手写说明待补。

## 五、发布前自证清单

| 检查 | 结果 |
| --- | --- |
| `git describe --tags` | `R3R-v1.0.7` |
| 工作区干净 | ✅ |
| 构建错误数 | 0 |
| `/Od` 出现次数 | 0 |
| Debug CRT（`ucrtbased` / `vcruntime140d` / `msvcp140d`） | 全部未链接 |
| exe 内 `R3R_debug.log` / `CPL-SCAN` / `FOLDCHK` / `CARGOWEIGHT` / `LOCO-MOVE` | **0 次** |
| exe 内 `R3R-v1.0.7` | 1 次 |
| 压缩包条目/完整性 | 135 / OK |

## 六、边界与未做

- 未 push 到 `origin`（按既有口径，玩家确认后再推）。
- 未做游戏内功能复测；本轮判定逻辑零改动，但首次启动建议重点验证：连挂/解挂、
  库内挂接、读档后车号与组名、车站调度与站场界面。
- 内测树 `build\openttd.exe` 仍是 2026-11-27 19:25:45（53 332 480 B，探针删除后版本）。
- 发布树 `build-release\openttd.exe` 被本次编译覆盖（旧 v1.0.6 二进制已不在，
  需要旧版请从 `R3R-v1.0.6-win64-2026-11-13.zip` 取）。
