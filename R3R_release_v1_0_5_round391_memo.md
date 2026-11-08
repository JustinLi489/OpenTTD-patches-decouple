# R3R v1.0.5 发布打包记录（第 391 轮，2026-11-04）

触发：用户「编译似乎完成了，请你检查。如果确认成功，请你把它打包放入压缩包作为 R3Rv1.0.5」。

## 一、构建自证（发布树 build-release）

| 项 | 读数 |
| --- | --- |
| 入口 | `R3R_release_fullbuild.cmd`（复用既有脚本，未新建任何 .cmd） |
| 完成标记 | `build-release\R3R_release_fullbuild.done` = `EXIT_CODE=0` |
| 编译步数 | 日志尾 `[693/693] Linking CXX executable openttd.exe` |
| 产物 | `build-release\openttd.exe` 23 329 792 B，2026-11-04 17:41:22 |
| 优化选项 | `FLAGS` 含 `/O2 /Ob2 /DNDEBUG`（RelWithDebInfo 覆盖 /Ob1） |
| 探针 | `build-release\build.ninja` 中 `R3R_PROBES_DEFAULT=0` 命中 625 处 |
| 六库 | `-DWITH_ZLIB / -DWITH_LIBLZMA / -DWITH_ZSTD / -DWITH_LZO / -DWITH_PNG / -DWITH_OPUSFILE` 各命中 624 处 |
| 源码新鲜度 | 最新源码 `src\newgrf_engine.cpp` 16:08:56 < 构建启动 17:11；exe 17:41:22 晚于全部源码 |
| 二进制探针核对 | 包内 exe 中 `COUPLE-PREFLIGHT` / `R3R_perf.log` / `R3R_debug.log` / `SPD-DROP` / `LOCO-MOVE` 全部 **False**（探针整块移除，非运行时关闭） |

说明：干跑（`--check`）已于 17:03:24 通过（`MODE=CHECK_ONLY`），其代价主要是 cmake configure（vcpkg 工具链 find_package 六库 + 生成 693 步 ninja 图），不含任何 `cl.exe`；`CMakeCache.txt` 保持 9/14、`vcpkg_installed` 保持 9/13，未做 clean configure、未复制 vcpkg。

## 二、打包

方式沿用 v1.0.4：暂存目录复制 + 替换 exe/lang/README，再压缩。

| 项 | 值 |
| --- | --- |
| 暂存目录 | `build-release\pkg_20261104\R3R-OpenTTD-0.73.1-win64\`（由 `pkg_20261101` robocopy /E 而来） |
| 替换项 | `openttd.exe`、`lang\*.lng`（66 个，简体中文 226 292 B @17:12）、`README_R3R.txt` |
| 条目数 | 136（与 v1.0.4 一致） |
| 输出包 | `D:\sourcecode of JGRPP\R3R-v1.0.5-win64-2026-11-04.zip`，16 086 011 B |
| 包内 exe | 23 329 792 B，SHA256 `C9172456D36A05C3D5B3B57539BA331884D8608BBD03247CC0DB91EAF922C124AC`（与 `build-release\openttd.exe` 逐字节一致） |
| 包 SHA256 | `72E59B8D1AF0C58AAFF855E35BB34DCE4EA50AA9103E6DF776C0E0D8B213BF1D80` |

`README_R3R.txt` 已改写为 v1.0.5 版：版本/日期、构建性质（探针编译期移除、不产 R3R_*.log、`R3R_DBG`/`R3R_PERF` 无效）、存档兼容（相对 v1.0.4 无存档块变动；旧版读新版不保证）、第 386~391 轮主要变化、已知问题、`源码提交 22dc2b6833 / tag r3r-stable-2026-11-04`。

## 三、本版写入 README 的变化清单（对应 KI）

- KI-551 删除「等待区路点预留截断」整机制。
- KI-555 timetable_cmd.cpp 空指针：分离状态收集改「实际持有该表的全部车辆」+ 两道防崩（崩溃面已消除，根因修法未做）。
- KI-557 / AUD-390 改表后索引同步：9 个入口统一到「该表全部持有者」；AUD-390-1 跟随者备份恢复回控制段表；AUD-390-4 时刻表/派发按实际持有者广播。
- KI-552 公路铰接车 `Disconnecting road vehicle.`：图像侧已修，崩溃侧本轮未复现。
- KI-558 车库 Ctrl+左键铰接组翻转（真铰接就地打散为假铰接 + 组翻转标记，双头仍拒）。

## 四、已知问题（已写入包内 README，未隐瞒）

- KI-553：通过式路点在 `TrainEnterStation` 仍可能被停住，第 386 轮修复后复测未通过。
- KI-554：上述修复之后「预留在路点停止」依然存在，第二层根因定位中。
- KI-556：读档红框 `STR_BROKEN_VEHICLE_LENGTH`（列车几何与名义长度不自洽），尚未修。
- KI-552 崩溃侧：未复现，按未收口对待。

## 五、未做 / 边界

- 未实机启动该 exe 验证（按既有约束：AI 不自行启动游戏等重型进程，需玩家自行试跑）。
- 未提交 git：本轮只产出压缩包与暂存目录，`pkg_*`、`*.zip` 按仓库惯例不入库（此前发行 zip 亦未入库）。
- 发布树 `build-release\vcpkg_installed`、`CMakeCache.txt` 均未重建，本轮为纯源码重编。
