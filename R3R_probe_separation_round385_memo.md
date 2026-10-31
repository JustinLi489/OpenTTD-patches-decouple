# 第 385 轮（2026-11-01）：探针编译期分离 + v1.0.4 发布

## 一、目的

发布版（`build-release\openttd.exe`）里**一个探针都不该存在**：不是运行时返回 false，
而是编译期整块消失（连同格式化字符串），且任何环境变量都无法把它打开。
内测版（`build\openttd.exe`）行为必须逐字不变，因为它是取数/复测的唯一工具。

## 二、实现（仅 `src\r3r_perf.h` 一个头文件）

| 位置 | 内容 |
|---|---|
| `r3r_perf.h:100-106` | `R3R_PROBES_DEFAULT`：无显式 `-D` 时按 `_DEBUG` 推导（Debug=1，其它=0） |
| `r3r_perf.h:130-132` | `R3R_PROBES` 主开关，默认取 `R3R_PROBES_DEFAULT`，可被 `-DR3R_PROBES=1/0` 覆盖 |
| `r3r_perf.h:144-157` | `R3RDbgOn()`：ON 走原 lambda（`R3R_DBG` 环境变量优先）；OFF `return false;` |
| `r3r_perf.h:169-182` | `R3RPerfOn()`：同上，OFF `return false;` |
| `r3r_perf.h:224-` | `R3RDbgBulkOn()`：同上，OFF `return false;` |
| `r3r_perf.h:266-285` | `R3RFopenDbg()`：OFF 下 `(void)mode; return nullptr;`，**不产生任何文件** |
| `r3r_perf.h:403-` | `R3RDbgWrite(...)`：OFF 下 `#define R3RDbgWrite(...) ((void)0)` |

要点：

1. **ON 分支逐字保留原代码**，没有重写、没有重排参数，所以内测版的日志格式与顺序不变。
2. OFF 分支下 `R3RDbgOn()` 是常量 `false`，`if (!R3RDbgOn()) return;` 折叠成 `if (true) return;`，
   后续整块（含 `fprintf` 与格式串）被 MSVC 丢弃 —— 这是"复制到 exe 里也搜不到"的原因。
3. `R3RDbgWrite` 在 OFF 下不求值实参 ⇒ 必须确认调用点的实参没有副作用（见 §四 V5）。

## 三、两棵树的开关取值

| 树 | 配置 | `-DR3R_PROBES_DEFAULT` | 结果 |
|---|---|---|---|
| `build\` | Debug（`_DEBUG`/`-MTd`） | 未定义（`build.ninja` 里 0 命中） | 推导 = 1，**探针 ON** |
| `build-release\` | RelWithDebInfo | `-DR3R_PROBES_DEFAULT=0`（显式） | **探针 OFF** |

## 四、验证判据与结果

| # | 判据 | 方法 | 结果 |
|---|---|---|---|
| V1 | 内测树开关为 ON | `Select-String build\build.ninja -Pattern 'R3R_PROBES'` | 0 命中 ⇒ 走 `_DEBUG` 推导 = 1，**通过** |
| V2 | 内测 exe 含探针字面量 | `findstr /c:"COUPLE-PREFLIGHT" /c:"RESV-AUDIT" /c:"LEGACY-ARTIC-DEARTIC" build\openttd.exe` | 命中，**通过** |
| V3 | 发布 exe 不含探针字面量 | 逐字节读入 exe 后 `String.Contains()` 查 5 个断言串 | 全部 `False`，**通过** |
| V4 | 发布 exe 依赖完整（不是残缺包） | `build-release\build.ninja` 的 `DEFINES` 含 `-DWITH_ZLIB -DWITH_LIBLZMA -DWITH_ZSTD -DWITH_LZO -DWITH_PNG -DWITH_OPUSFILE` | 已确认命中，**通过** |
| V5 | 探针调用实参无副作用（OFF 下不求值） | 正则扫描全部 `src\**\*.{cpp,h}` 的 `R3RDbg*(...)` 调用，查 `++/--/+=/-=/*=//=` | 7 条命中全为注释文本，真实调用 **0 处**，**通过** |

V5 的必要性：`ON + R3R_DBG=0` 时实参仍会被求值，而 `OFF` 下不会。若某处写了
`R3RDbgWrite("x=%d", counter++)`，发布版就会少算一次 —— 这正是"分离"最容易引入的隐性行为差异。

### 4.1 V3 逐串实测读数（`[System.IO.File]::ReadAllBytes` + `ASCII.GetString`）

| 查询串 | `build\openttd.exe`（ON） | `build-release\openttd.exe`（OFF） |
|---|---|---|
| `COUPLE-PREFLIGHT` | True | **False** |
| `RESV-AUDIT` | False | False |
| `R3R_perf.log` | True | **False** |
| `LEGACY-ARTIC-DEARTIC` | True | **False** |
| `SPD-DROP` | True | **False** |

说明：`RESV-AUDIT` 在两棵树里都是 False，原因是该串所在代码（`RESV-AUDIT-WATCH/STRAY/OWNER/SUMMARY` 等
整段预留审计探针）已在第 384 轮台账移除时一并删除，现仅残留在 `src\train_cmd.cpp.r3r_bak` 备份文件中，
不属于本次分离的有效判据。其余 4 串构成 ON/OFF 的完整对照：内测 `True`、发布 `False`。

本次用的是进程内字节比较而不是 `findstr`：`Select-String -Encoding Byte` 在当前 PowerShell 5.1
不受支持（`ValidateSet` 只认 unicode/utf8/ascii/oem…），会整段报错并中断循环。

## 五、构建自证

### 5.1 内测树（`build\`，探针 ON）

- 入口：直接 `ninja -C build -j2 openttd`（续编被中断计划的剩余 65 步）
- 结果：`[66/66] Linking CXX executable openttd.exe`（含 1 步链接），错误计数 0
- `build\openttd.exe` = 52 451 840 B @ 2026-11-01 00:58:15
- 说明：这一轮只续编，未跑 `R3R_inc_guard.ps1`；安全依据见 §六。

### 5.2 发布树（`build-release\`，探针 OFF）

- 入口：`R3R_release_fullbuild.cmd`（全量，删 obj 后重编 703 步）
- 结果：`[703/703] Linking CXX executable openttd.exe`，
  `build-release\R3R_release_fullbuild.done` = `EXIT_CODE=0`，
  日志中 `error C` / `fatal error` / `FAILED:` / `build stopped` 计数 **0**
- `build-release\openttd.exe` = 23 317 504 B @ 2026-11-01 01:26:36
- 体积对照：发布 23.3 MB vs 内测 52.5 MB（Debug 信息 + 探针代码），与探针被整块剥离的判断一致

## 六、为什么这次续编不会造成"混合探针"二进制

`r3r_perf.h` 本轮改动只增加 `#else` 分支，`#if R3R_PROBES` 为真时展开的代码与改动前**逐字相同**。
因此任何"旧 obj + 新 obj"的组合在 ON 语义下都是同一份代码，不存在 ABI 或行为差异；
真正需要担心类布局变化的场景（KI-183）与本次改动无关。

## 七、结论

**探针分离有效**，五条判据全部通过：

1. 开关来源正确：内测树靠 `_DEBUG` 推导为 1，发布树靠显式 `-DR3R_PROBES_DEFAULT=0` 为 0，两树互不污染。
2. 效果可证伪：同一组断言串在内测 exe 中全部命中、在发布 exe 中**全部消失**（字节级 `Contains()`），
   证明是编译期整块剔除，而不是运行时 `return false`（后者格式串仍会留在只读数据段）。
3. 无行为漂移面：OFF 下 `R3RDbgWrite` 不求值实参，全库扫描确认没有任何带副作用的实参。
4. 内测行为不变：`#if R3R_PROBES` 为真时展开的代码与本轮改动前逐字相同。
5. 两树均构建成功、零错误，产物分别为 `build\openttd.exe`（52 451 840 B，探针 ON）
   与 `build-release\openttd.exe`（23 317 504 B，探针 OFF）。

推论：`build-release\openttd.exe` 可作为 v1.0.4 发行包的二进制——它不含任何 `R3R_perf.log`
写入路径（`R3RFopenDbg()` 在 OFF 下返回 nullptr），玩家侧不会产生调试日志文件。
