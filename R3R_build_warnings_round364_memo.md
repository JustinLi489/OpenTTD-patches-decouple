# 第 364 轮（2026-11-20）构建警告清理：40 条语言串 + 4 条 C4267

关联 KI：`R3R_KNOWN_ISSUES.md` 的 **KI-667**。

## 一、输入与取证

- 玩家：「我注意到 station_yard_sl.cpp 附近也有一些报错」。上一轮指令：把 `build\R3R_incbuild.log` 里的 warning 改完再构建。
- 该日志由 2026-11-19 那次全量构建产生，共 698 步，`EXIT_CODE=0`（**没有 error，只有 warning**）。
- 日志里的**语言串 warning 行含中文，ripgrep 按 GBK 无法解码**（`search_content` 用 `warning C4` 搜该日志返回 0 命中，但带 `contextAround` 时那几行被整段跳过）。⇒ 读这份日志的告警必须用 `findstr`（ANSI）或 GBK 解码，不能用 rg。这条经验对后续所有 `R3R_incbuild.log` 取证有效。
- 清点结果（`findstr /N /C:"warning C" /C:"error C" /C:"fatal error"`）：

| 类别 | 条数 | 落点 |
| --- | --- | --- |
| `strgen` 参数模板不匹配 | 40 | `src\lang\simplified_chinese.txt`（日志第 86~126 行） |
| `warning C4267`（size_t → const unsigned int） | 4 | `src\sl\station_yard_sl.cpp(124/156/212/278)`（日志第 462~465 行） |
| `error C*` / `fatal error` / `LNK` | 0 | — |

## 二、A 组：17 条真实翻译缺陷

警告形态 `STR_xxx: Param idx #N '<lang cmd>' doesn't match with template command '<en cmd>'`。逐条对英文模板比对，中文要么缺占位符（游戏内显示为空或 `(invalid parameter)`），要么多占位符（读到上一条字符串的残留参数）。

| STR | 英文模板（要点） | 中文原状 | 修法 |
| --- | --- | --- | --- |
| `STR_GRAPH_X_LABEL_MONTH_YEAR` | `{TINY_FONT}{STRING}{}{NUM}` | 多一个 `{STRING}` | 去掉多余者 |
| `STR_NEWS_SERVICE_SUBSIDY_OFFERED` | 末尾 `{UNITS_YEARS_OR_MINUTES}` | 硬写「为期一年」 | 改 `为期 {UNITS_YEARS_OR_MINUTES} 的财政补贴` |
| `STR_NEWS_SERVICE_SUBSIDY_AWARDED_{HALF,DOUBLE,TRIPLE,QUADRUPLE}` | idx0 `{RAW_STRING}`，末尾 `{UNITS_YEARS_OR_MINUTES}` | idx0 `{STRING}`，硬写「今后一年中」 | idx0 改 `{RAW_STRING}`；「今后 {UNITS_YEARS_OR_MINUTES} 中」 |
| `STR_LAND_AREA_INFORMATION_CARGO_ACCEPTED` | `{BLACK}接受货物：{LTBLUE}{RAW_STRING}` | 缺 `{RAW_STRING}` | 补回 |
| `STR_TOWN_DIRECTORY_CAPTION` / `STR_INDUSTRY_DIRECTORY_CAPTION` | 末尾 `({COMMA} of {COMMA})` | 只有「城镇」「工业设施」 | 补 `（{COMMA} / {COMMA}）` |
| `STR_REPLACE_REMOVE_WAGON` | `({STRING1})` | 缺 | 补 `（{STRING1}）` |
| `STR_VEHICLE_INFO_RELIABILITY_BREAKDOWNS` | 3 个 `{COMMA}` | 2 个 | 补 `（最高：{COMMA}%）` |
| `STR_VEHICLE_DETAILS_TRAIN_ARTICULATED_RV_CAPACITY` | `{BLACK}运力：{LTBLUE}{RAW_STRING}` | 缺 `{RAW_STRING}` | 补回 |
| `STR_ORDER_GO_TO_NEAREST_DEPOT_FORMAT` | `{STRING} the nearest {STRING}` | `{STRING} {STRING} {STRING}` | 改 `{STRING}最近的{STRING}` |
| `STR_ORDER_GO_TO_STATION` | `{STRING} {STATION}` | 3 个占位 | 删多余 `{STRING}` |

**AWARDED 组的 idx0 是真 bug（警告不报它）**：英文 `{RAW_STRING}`、中文 `{STRING}`。C++ 侧是
`GetEncodedString(STR_NEWS_SERVICE_SUBSIDY_AWARDED_*, std::move(company_name), ...)`（`std::string`），
而 `{STRING}` 会走 `GetNextParameter<StringID>()`；`StringParameters` 是强类型 variant，取值类型不符会抛
`std::out_of_range`（见 `src/strings_type.h` 的 `GetNextParameter` / `GetNextParameterString` 实现）。
故中文这两支的 idx0 必须与英文一致写 `{RAW_STRING}`。

## 三、B 组：23 条 strgen 检查器假阳性

现象：`STR_R3R_*` / `STR_ORDER_R3R_*` / `STR_ORDER_COUPLE_TEMP_GROUP_*` / `STR_COUPLE_GROUP_*` /
`STR_DEPOT_CHAIN_GROUP_OWNER` / `STR_VEHICLE_INFO_COUPLE_GROUP` 报
`Param idx #0 'STRING' doesn't match with template command 'RAW_STRING'`，
但中英两侧**逐字都写 `{RAW_STRING}`**。

根因（`src\strgen\strgen_base.cpp` 的 `CheckCommandsMatch`）：

```cpp
if (TranslateCmdForCompare(templ.consuming_commands[i]) != lang.consuming_commands[i]) {
```

`TranslateCmdForCompare` 把 `STRING1..7` / `RAW_STRING` 统一映射成 `STRING`。这里只对**模板（英文）侧**调用了它，
语言侧仍是解析出来的原命令 ⇒ 两侧都写 `{RAW_STRING}` 时，模板侧变 `STRING`、语言侧是 `RAW_STRING`，指针不等 ⇒ 报警。
日志里打印 `'STRING'` 也是同一个原因（打印时同样过了 `TranslateCmdForCompare`）。

修法：两侧对称归一化：

```cpp
if (TranslateCmdForCompare(templ.consuming_commands[i]) != TranslateCmdForCompare(lang.consuming_commands[i])) {
```

**为什么不去改语言文件**：把中文的 `{RAW_STRING}` 改成 `{STRING}` 确实能让这条警告消失，但运行时参数是
`const char*`/`std::string`，`{STRING}` 会按 `StringID` 取参 ⇒ `std::out_of_range`。等于用崩溃换静默。
**为什么对称归一化不会新增掩盖**：现状已经把模板侧归一化了，即「英文 `RAW_STRING` + 中文 `STRING`」本来就不报；
对称化只是让「两侧同为 `RAW_STRING`」也不再误报，掩盖范围不扩大。代价是 `STRING` 与 `STRING1..7` 的差异同样不区分
（与现状一致）。

## 四、C4267：`station_yard_sl.cpp` 4 条

`TileIndex` 是 `StrongType<TileIndex, uint32_t>`，构造函数形参是 `const unsigned int`，而传入的是 `size_t`
（`name.size()` / `pool.size()` / `program.size()` / `y.tiles.size()`）⇒ C4267「可能丢失数据」。

修法：`static_cast<TileIndex>(static_cast<uint32_t>(x))`，四处（124 / 156 / 212 / 278）。

安全性：解码侧已有硬上限 —— `SYRD_MAX_NAME_BYTES`（`MAX_LENGTH_STATION_NAME_CHARS * 4`）、
`SYRD_MAX_POOL_ENTRIES = 4096`、`R3R_DISPATCH_PROGRAM_MAX`，且每个循环都有 `p < in.size()` 截断保护
（`R3RUnpackName` / `R3RUnpackPool` / `R3RUnpackProgram`），写侧真正超 4G 条目的场景不存在。
`out.push_back(static_cast<TileIndex>(st->r3r_yards.size() & 0xFFFF))`（271 行）未报警（MSVC 能推断位与后的值域），
为「只改报警处」原则未动。

## 五、改动清单

- `src\strgen\strgen_base.cpp`：`CheckCommandsMatch` 的循环比较两侧归一化（+4 行注释）。
- `src\lang\simplified_chinese.txt`：13 行（17 条 STR 里 OFFERED 1 行、AWARDED 4 行、其余 8 行）。
- `src\sl\station_yard_sl.cpp`：4 行显式窄化。

未碰 `src\lang\english.txt`（模板基准）、未碰其它语言文件、未改任何存档字段或格式。

## 六、构建与复核

- 入口：`R3R_fullbuild.cmd inc detach`（复用既有脚本，未新建 `.cmd`）；构建前 `tasklist` 确认 `openttd.exe` 未运行。
- 因改了 `src\lang\*.txt`，`R3R_inc_guard.ps1` 会判「语言文件新于最新 obj」⇒ 自动转**全量**（约 16~20 分钟）。
- 自证三件套与「警告归零」核对结果见下（构建结束后回填）。

### 6.1 第一次构建：改出 8 条新警告（已回退重改）

第一次构建跑到 `[91/708] Generating table/strings.h` 时，`findstr /N /C:"warning" build\R3R_incbuild.log` 显示 8 条：

```
86: src\lang\simplified_chinese.txt (965): warning: STR_NEWS_SERVICE_SUBSIDY_AWARDED_HALF: Param idx #1 'UNITS_YEARS_OR_MINUTES' doesn't match with template command 'STRING' (info)
87: ... (965): Param idx #4 'STRING' doesn't match with template command 'UNITS_YEARS_OR_MINUTES' (info)
88~93: DOUBLE / TRIPLE / QUADRUPLE 同形，各 2 条
```

- **根因**：参数编号是**位置**语义。英文模板是
  `{RAW_STRING}(#0) {STRING}(#1) {STRING1}(#2) {STRING1}(#3) {UNITS_YEARS_OR_MINUTES}(#4)`，
  我上一版中文把 `{UNITS_YEARS_OR_MINUTES}` 写在了"今后 …中将"处（#1），三个 `{STRING}` 因此整体后移成 #2/#3/#4 ⇒ #1 与 #4 双向错位。
- **修法**：改成 `…{RAW_STRING}！{}{}将 {STRING} 从 {STRING} 运送到 {STRING}，今后 {UNITS_YEARS_OR_MINUTES} 将获得…`
  —— 四个消费参数严格保持 `RAW_STRING, STRING, STRING, STRING, UNITS` 的顺序（`STRING1` 经 `TranslateCmdForCompare` 归一为 `STRING`，位置对即可）。
- **处置**：`taskkill /PID <ninja> /T /F` 停掉这次注定作废的构建（语言包已按错序生成），改完语言串后重新起 `R3R_fullbuild.cmd inc detach`。
- 副产物：这次日志**只有**这 8 条警告，说明 A 组 17 条真实缺陷与 B 组 23 条假阳性确已全部消除，`strgen` 侧修复生效（strgen 随全量重建）。

### 6.2 构建自证（第二次，成功）

- 入口：`R3R_fullbuild.cmd inc detach`（复用既有脚本，未新建 `.cmd`）；构建前 `tasklist /FI "IMAGENAME eq openttd.exe"` 为空。
- 护栏：`build\R3R_incbuild.guard.log` = `GUARD: 1 header/lang file(s) are newer than the newest object file` / `newest object = 2026-11-20 00:11:57` / `newer: src\lang\simplified_chinese.txt (2026-11-20 00:12:04)` / `GUARD: REMOVED 34 object file(s) - upgrading this build to a FULL rebuild`。
- 结果：`[708/708] Linking CXX executable openttd.exe`；日志尾 `==== R3R build ended 2026/11/20 周五 0:37:54.14 MODE=inc EXIT_CODE=0`；`build\R3R_incbuild.done` = `EXIT_CODE=0`。
- **警告归零**：`find /c "warning" build\R3R_incbuild.log` = **0**（语言串与编译器警告均为 0）；`find /c "error C"` = 0；`find /c "Linking CXX executable"` = 3（strgen 等两个辅助目标 + openttd.exe）。
- 时间戳：`src\sl\station_yard_sl.cpp` 00:06:23 → `.obj` 00:24；`src\strgen\strgen_base.cpp` → `.obj` 00:17；`build\lang\simplified_chinese.lng` 00:13（231 454 B）；`build\openttd.exe` **2026-11-20 00:37（54 363 136 B）**晚于全部源码与 obj。
- `read_lints`：`station_yard_sl.cpp`、`strgen_base.cpp` 均 0 条。

结论：44 条警告全部消除，且过程中「改出 8 条新警告」已修正；内测树（探针 ON）已更新，发布树 `build-release` 未编（仍停留在上一版）。

## 七、未做 / 边界

- 未统一 `STRING` 与 `STRING1..7` 的语义差异检查（`StringsType::GetFormat` 返回 `StringID`，不会抛异常，但 `STRING1`
  允许被引用串消费参数，`{STRING}` 不行；上游其它语言普遍如此，改动面大且无现场问题）。
- 未清理 `STR_NEWS_SERVICE_SUBSIDY_AWARDED_*` 之外、同样存在 `{STRING}`/`{RAW_STRING}` 混用的其它语言串
  （只在警告清单范围内动，避免扩大回归面）。
- `src\strgen\strgen_base.cpp` 是上游工具代码，未来合并上游时该行可能冲突，需保留「两侧对称归一化」。
