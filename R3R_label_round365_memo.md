# 第 365 轮（2026-11-20）：KI-666 修复未生效——`_sl_r3r_legacy_numbering` 被 `Load_SLXI()` 清零

## 一、玩家现场（原话照录）

> 「我注意到轨道仍然读档时变为建造列表的第一项，也就是非电气化的很低速的轨道，说明你的修复还没生效」

- 即第 363 轮 KI-666（旧 R3R 档 rail/road type 全部回落默认类型）的修复**在实机上无效**：
  读旧档后全图轨道仍是 `RAILTYPE_RAIL`（购买菜单第一项、非电气化、低速），道路同理。
- 本轮被测 exe：`build\openttd.exe` 2026-11-20 00:37（含第 363 轮 KI-666 + 第 364 轮 44 条警告修复）。

## 二、修复现状（三处代码都在，没有漏编）

| 位置 | 内容 | 状态 |
|---|---|---|
| `src\sl\saveload_common.h:494` | `extern bool _sl_r3r_legacy_numbering;` | 在 |
| `src\sl\saveload.cpp:112` | 定义 `bool _sl_r3r_legacy_numbering;` | 在 |
| `src\sl\saveload.cpp:4314-4319` | base 版本 ∈ [367,368] ⇒ `_sl_version += 2` 且 **`_sl_r3r_legacy_numbering = true`** | 在 |
| `src\sl\extended_ver_sl.cpp:783-787` | `loadUV()` 内 `if (_sl_r3r_legacy_numbering && UV ∈ [367,368]) UV -= 2;` | 在 |
| `src\sl\extended_ver_sl.cpp:306-318` | `SlXvResetState()` —— **末尾 `_sl_r3r_legacy_numbering = false;`** | **← 元凶** |

## 三、根因（时序已确定，非猜测）

`SLXI` 是标准 chunk，其加载函数 `Load_SLXI()`（`src\sl\extended_ver_sl.cpp:647`）
在**第一行逻辑里**调用 `SlXvResetState()`：

```647:654:src/sl/extended_ver_sl.cpp
static void Load_SLXI()
{
	if (_sl_is_faked_ext || !_sl_is_ext_version) {
		SlErrorCorrupt("SLXI chunk is unexpectedly present");
	}

	SlXvResetState();      // ← 这里把 _sl_r3r_legacy_numbering 清回 false
	_sl_is_ext_version = true;
```

而 `loadUV()`（同文件 `:770` 起，SLXI 的子块之一）在 `SlXvResetState()` **之后**才执行，
它看到 `_sl_r3r_legacy_numbering == false` ⇒ **remap 分支永不进入** ⇒
`_sl_xv_upstream_version` 保留旧档原值 **368**。

时序：

1. `src\sl\saveload.cpp:4285` 读文件头 base version = 368（带 `SAVEGAME_VERSION_EXT`）。
2. `:4314-4319` 命中 `[SLV_R3R_LEGACY_FIRST, SLV_R3R_LEGACY_LAST]` ⇒ `_sl_version += 2` 且
   **`_sl_r3r_legacy_numbering = true`**，打 `Remapping legacy R3R savegame version 368 to 370`。
3. 开始按 chunk 列表加载，第一个就是 `SLXI` ⇒ `Load_SLXI()` ⇒ `:653 SlXvResetState()`
   ⇒ **`_sl_r3r_legacy_numbering = false`**（`extended_ver_sl.cpp:317`）。
4. `loadUV()` 判 `if (_sl_r3r_legacy_numbering && ...)` ⇒ **false** ⇒ 不 remap ⇒ `_sl_xv_upstream_version = 368`。
5. `RAIL` / `ROTT`（table chunk）经 `GeneralUpstreamChunkLoadInfo::GetLoadVersion()` 取到 **368** ⇒
   `IsSavegameVersionBefore(LabelOrientationUnification=367)` **为假** ⇒ `std::ranges::reverse(lo.label)` **不执行**。
6. 旧档里 label 其实是**旧字节序** ⇒ `GetRailTypeByLabel`/`GetRoadTypeByLabel` 全部失配 ⇒
   `AfterLoadLabelMaps()` 把整图类型退回默认（铁路 = 第一项非电气化、道路 = 第一类）。

⇒ 第 363 轮把 remap 放在 `loadUV()` 里是对的，但**没有考虑 `SlXvResetState()` 会在它之前把这个标志清掉**。

## 四、为什么第 363 轮没发现

- 论证只覆盖了"table chunk 用 UV ⇒ `_sl_version` 平移无效"，因此补了 UV remap；
- 没有验证"设置标志的时点（base 版本解析）"与"`SlXvResetState()` 的调用时点（SLXI chunk 开头）"的先后 ——
  恰恰是后者覆盖了前者；
- 也没有实机 `-d sl=1` 日志做正面确认（`Remapping legacy R3R upstream savegame version` 这一行**从未出现过**）。

## 五、修法（本轮落地）

**把 `_sl_r3r_legacy_numbering` 从 `SlXvResetState()` 的复位清单里移除**，
改为在读档入口 `SlLoad()` 里（`SlXvResetState()` 之后、解析版本之前）显式复位一次：

- `src\sl\extended_ver_sl.cpp:317` 删除 `_sl_r3r_legacy_numbering = false;`，并加注释说明
  「该标志由 base 版本解析（saveload.cpp 的 legacy 平移块）置位，SLXI 的 reset 不得清它」。
- `src\sl\saveload.cpp` 的 `SlLoad()` 中 `:4243 SlXvResetState();` 之后补 `_sl_r3r_legacy_numbering = false;`。

为什么不能反过来（在 `Load_SLXI()` 里重新推导）：`SlLoad()` 把平移前的 base 版本放在局部变量
`original_sl_version` 里，`Load_SLXI()` 拿不到；用 `_sl_version` 反推会与"新档恰好等于 369/370"混淆。

副作用评估：
- 读档：旧档 ⇒ 标志在 SLXI 之后仍为 true ⇒ remap 生效 ⇒ 366 ⇒ 反转 ⇒ 正确。
- 新档（base=371）⇒ 平移块不命中 ⇒ 标志 false ⇒ 不 remap ⇒ `UV=371 ≥ 367` ⇒ 不反转 ⇒ 正确。
- 上游 JGRPP 档（无 EXT 标志）⇒ 平移块不进入 ⇒ false ⇒ 正确。
- 存档路径：`SlXvSetCurrentState()` 内部也调 `SlXvResetState()`，标志可能残留上一次读档的 true；
  保存流程不读该标志（`saveUV()` 只写 `SL_UPSTREAM_VERSION`），但为干净起见在 `SlSave()` 入口同样复位一次。

## 六、复测判据

1. 读旧档（如 `T8701-2_4.sav`）后全图轨道保持原 rail type（电气化/单轨/磁悬浮/GRF 自定义轨型），
   道路（含 tram）保持原 road type，**不再**回落为购买菜单第一项。
2. `-d sl=1` 的日志里**必须**出现 **两行**：
   - `Remapping legacy R3R savegame version 368 to 370`
   - `Remapping legacy R3R upstream savegame version 368 to 366`
   （第 2 行此前从未出现，是本次修复是否生效的唯一直接证据。）
3. 车库/购买列表里新建同类轨道时，默认选中该线路原有类型。
4. 新存一档再读回：不出现上述两行，轨型/路型保持。
5. 负判据：上游 JGRPP 档（base=362，无 EXT）可读、轨型正确、无 remap 行。
6. 负判据：无 `TOO_NEW_FEATURE_VERSION` / 读取失败。

## 七、未确认 / 边界

- **U-1**：需要玩家提供一次 `-d sl=1` 的读档日志，确认第 2 行出现（本轮无法自行取数：AI 不自行启动游戏）。
- **U-2**：KI-666 的判据 3（购买列表默认选中）与判据 4 从未被验证过，本次一并复测。
- **U-3**：`SL_UPSTREAM_VERSION = MAX_LOAD_SAVEGAME_VERSION = 371` 而"上游真实版本"只到 368 的历史遗留
  仍在（第 363 轮已论证无害），本轮不动。
- 若修复后仍回落，则需转查 `RAIL`/`ROTT` chunk 的加载顺序（是否在 `SLXI` 之前）——
  那时 `_sl_xv_upstream_version` 还是 `SL_MIN_VERSION`（< 367 ⇒ 反转），说明问题在**写入侧**而非读取侧。

## 八、落地记录与构建自证（2026-11-20）

### 8.1 实际改动（三个 .cpp，未碰任何 .h）

| 文件 | 位置 | 改动 |
| --- | --- | --- |
| `src\sl\extended_ver_sl.cpp` | `SlXvResetState()` 末尾（原 `:317`） | **删除** `_sl_r3r_legacy_numbering = false;`，改为注释说明该标志由 base 版本解析置位、SLXI 的 reset 不得清它 |
| `src\sl\extended_ver_sl.cpp` | `loadUV()` 的 remap 分支 | 新增探针 `R3R-VER-REMAP upstream %u -> %u`；`else` 分支新增 `R3R-VER-NOREMAP upstream=%u legacy_flag=%d`（区分"当前档"与"标志丢了"） |
| `src\sl\saveload.cpp` | `DoLoad()` 的 `SlXvResetState();` 之后 | 显式 `_sl_r3r_legacy_numbering = false;`（每次读档复位一次） |
| `src\sl\saveload.cpp` | `DoSave()` 的 `_sl_version = SAVEGAME_VERSION;` 之前 | 显式 `_sl_r3r_legacy_numbering = false;`（保存永不走 legacy 编号） |
| `src\saveload\labelmaps_sl.cpp` | `RAIL` / `ROTT` 两个 `Load()` | 把 `IsSavegameVersionBefore(...)` 提到循环外（结果不变），循环内计数，末尾打 `R3R-LABEL-REV kind=rail|road n=%u legacy=%d ver=%u` |

（§五 写的函数名 `SlLoad()`/`SlSave()` 是笔误，实际函数是 `DoLoad()`/`DoSave()`。）

### 8.2 为什么要加探针

第 363 轮之所以"看起来修了但没生效"却无人发现，是因为**没有任何正面日志**证明 remap 分支被走到过
（原代码只有 `Debug(sl, 1, ...)`，而 `-d sl=1` 平时不开）。本轮三个探针走 `R3RDbgWrite`（恒定写入
`build\R3R_debug.log`，前缀 `R3R-` 已过白名单），判读不依赖启动参数：

- 旧档读入 ⇒ 期望 `R3R-VER-REMAP upstream 368 -> 366` **+** `R3R-LABEL-REV kind=rail n=<轨型数> legacy=1 ver=366`
  **+** 同形态的 `kind=road`。
- 新档读入 ⇒ 期望 `R3R-VER-NOREMAP upstream=371 legacy_flag=0` **+** `R3R-LABEL-REV ... legacy=0 ver=371`。
- 若出现 `R3R-VER-NOREMAP upstream=368 legacy_flag=0` ⇒ 标志又在中途被清（回归），修法失效。

### 8.3 构建自证（内测树，探针 ON）

- 入口：`cmd /c start "R3R build" /min "d:\sourcecode of JGRPP\R3R_fullbuild.cmd" inc`
  （`Start-Process -ArgumentList 'inc detach'` 在本环境被 shim 吞掉、实际没起进程，已改用 `start`）。
- 构建前 `openttd.exe` 未运行；`build\R3R_incbuild.guard.log` = `GUARD: incremental is safe (no header/lang file is newer than the newest object)` ⇒ 增量合法。
- `build\R3R_incbuild.done` = `EXIT_CODE=0`（@01:18）。
- 时间戳链：`src\sl\saveload.cpp` / `extended_ver_sl.cpp` @00:57、`src\saveload\labelmaps_sl.cpp` @00:58
  → 三个 obj @01:10（`openttd_lib.dir\src\sl\saveload.cpp.obj` 5 922 350 B、
  `extended_ver_sl.cpp.obj` 1 291 747 B、`src\saveload\labelmaps_sl.cpp.obj` 882 397 B）
  → `build\openttd.exe` @2026-11-20 01:17（54 364 672 B）。
- `read_lints` 三文件 0 条。
- exe 字面量自证：`findstr /M /C:"R3R-LABEL-REV"` 与 `/C:"R3R-VER-NOREMAP"` 均命中 `build\openttd.exe`。
- 注：本轮 `build\R3R_incbuild.log` 为 0 字节（ninja 输出重定向块缓冲未 flush），**不能**用"日志为空"判定失败；
  以 `.done` 的 `EXIT_CODE=0` + exe 大小/时间戳 + obj 时间戳为准。

### 8.4 状态

已实现 + 已编译，**游戏内复测待做**。复测只需玩家读一次旧档并把 `build\R3R_debug.log` 交回，
按 §六 判据 1/2/4 看三行探针即可定性；若 `R3R-LABEL-REV legacy=1` 已出现但轨型仍回落，
则问题在写入侧（见 §七 末条），届时转查 `RAIL`/`ROTT` 的写出路径。
