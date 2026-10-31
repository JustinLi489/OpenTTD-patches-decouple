# R3R v1.0.4 发行交付记录（2026-11-01，第 385 轮）

本文件是一次**发布轮次**的交付台账：源码提交、tag、两棵构建树的产物、发行包与其校验值，
以及这次发布刻意留下的未完成项。行为层面的取证在 `R3R_probe_separation_round385_memo.md`，
缺陷/设计口径在 `R3R_KNOWN_ISSUES.md` 末尾「第 385 轮」小节（KI-549 / KI-550）。

---

## 一、版本标识

| 项 | 值 |
|---|---|
| 发行版号 | v1.0.4（2026-11-01 测试发行版） |
| 上游版本 | OpenTTD / JGRPP 0.73.1 |
| 源码提交 | `6df9322001`（branch `feature/decouple`） |
| git tag | `r3r-stable-2026-11-01` |
| 上一版 | v1.0.3（`r3r-stable-2026-10-18`，提交 `94bcd67019`） |
| 覆盖轮次 | 第 304 ~ 385 轮 |

提交信息：`R3R: v1.0.4 release checkpoint 2026-11-01 (rounds 304-385)`。
本轮批次 59 个文件（含两个已删除的临时 `.cmd`、源码 27 项改动、KI 文档与本轮备忘）。

---

## 二、两棵构建树

| 树 | 配置 | 探针 | 产物 | 大小 | 时间 |
|---|---|---|---|---|---|
| `build\` | Debug（`_DEBUG`、`-MTd`） | ON（`_DEBUG` 推导） | `build\openttd.exe` | 52 451 840 B | 2026-11-01 00:58:15 |
| `build-release\` | RelWithDebInfo（`/O2 /Ob2`） | OFF（`-DR3R_PROBES_DEFAULT=0`） | `build-release\openttd.exe` | 23 317 504 B | 2026-11-01 01:26:36 |

构建自证：

- 内测续编：`ninja -C build -j2 openttd` → `[66/66] Linking CXX executable openttd.exe`，错误计数 0。
- 发布全量：`R3R_release_fullbuild.cmd` → `[703/703] Linking CXX executable openttd.exe`，
  `build-release\R3R_release_fullbuild.done` = `EXIT_CODE=0`，
  日志中 `error C` / `fatal error` / `FAILED:` / `build stopped` 计数 **0**。
- 发布树 `build-release\build.ninja` 的 `DEFINES` 含 `-DR3R_PROBES_DEFAULT=0`，且
  `-DWITH_ZLIB / -DWITH_LIBLZMA / -DWITH_ZSTD / -DWITH_LZO / -DWITH_PNG / -DWITH_OPUSFILE` 齐全
  （不是 v1.0.2 那种缺依赖的残包）。

---

## 三、发行包

| 项 | 值 |
|---|---|
| 文件名 | `R3R-v1.0.4-win64-2026-11-01.zip`（工作区根目录） |
| 大小 | 16 079 004 B |
| 生成时间 | 2026-11-01 01:34:16 |
| ZIP 条目数 | 136（与 v1.0.3 逐条结构一致） |
| 顶层目录 | `R3R-OpenTTD-0.73.1-win64\` |
| 暂存目录 | `build-release\pkg_20261101\`（由 `pkg_20261018` 复制后替换 exe/README/lang） |

包内顶层：`ai/ baseset/ game/ lang/ scripts/ changelog.md COPYING.md jgrpp-changelog.md
openttd.exe README.md README_R3R.txt`。
`lang\` 为本次构建产物（`build-release\lang\*.lng` 66 个全量覆盖，
`simplified_chinese.lng` 226 292 B @ 2026-11-01 01:01:02，与构建树 SHA256 一致）。
上游文档 `changelog.md`(656 283) / `jgrpp-changelog.md`(210 283) / `README.md`(34 969) /
`COPYING.md`(18 287) 上游未改动，直接沿用上一版。

### 3.1 校验值（SHA256）

```
zip     D8ED5C72B3A33571325A7C95C712994F888B617D5F6AA8AD4CB58D382C8874EB30
exe     EFEFAB49D201F8E9D2A5070955CA99AA9D7DDA924E10630BA3B10BF5D7993412FE
```

### 3.2 包内 exe 的探针核查

对 `pkg_20261101\...\openttd.exe` 逐字节读入后 `String.Contains()`：

| 断言串 | 结果 |
|---|---|
| `COUPLE-PREFLIGHT` | False |
| `R3R_perf.log` | False |
| `SPD-DROP` | False |

⇒ 包内是探针 OFF 的发布版，玩家侧不会生成 `R3R_*.log`。诊断交给作者另行提供的 Debug 版。

---

## 四、README_R3R.txt（包内说明）要点

- 标题：`R3R OpenTTD 0.73.1 (Windows x64) —— 2026-11-01 测试发行版（v1.0.4）`
- 「关于本构建（重要）」明确写成**编译期整块移除**，与运行时关闭区分开，并提示
  `R3R_DBG / R3R_PERF / R3R_DBG_BULK / R3R_DIAG` 对发布版全部无效。
- 存档兼容：移除 R3RL / R3DS 两代台账块；新版读旧版正常，旧版读新版不保证。
- 变化列表按第 304 ~ 384 轮归纳（预留走廊、挂接、解挂、自动填充、掉头长预留、读档断言、台账移除）。
- 源码提交 `6df9322001` 与 tag `r3r-stable-2026-11-01` 已回填（草稿在 `build\_readme_r3r_new.txt`）。

---

## 五、未完成项

1. **远程推送失败**：`git push origin feature/decouple` 返回
   `Failed to connect to github.com port 443`（21 076 ms 超时）。
   现场核查：`git config` 无 `http.proxy`、环境无 `HTTPS_PROXY`/`HTTP_PROXY`、
   `Test-NetConnection github.com -Port 443` 为 False ⇒ 本机当前网络无法出站，非仓库或凭据问题。
   本地提交与 tag 已完成，网络恢复后执行：
   `git push origin feature/decouple` + `git push origin r3r-stable-2026-11-01`。
   在此之前**发布包已可分发**，但 `README_R3R.txt` 里的 tag 在远端尚不可解析。
2. **KI-550 保持不变**：`src\vehicle_base.h:32` 与 `src\pbs.h:17` 对 `r3r_perf.h` 的无效 include 未清理
   （玩家 2026-11-01 决定本轮不动，避免因改头文件触发额外全量重编）。
3. **未做游戏内验收**：本次只到"构建通过 + 探针分离可证伪"为止，未启动游戏跑场景。

---

## 六、回退方式

```powershell
# 回到上一版源码
git checkout r3r-stable-2026-10-18 -- src

# 或整体回到本版之前的提交
git switch --detach 3b87101
```

改过 `src\*.h` 后必须全量重编（KI-183 口径），不要走增量。
