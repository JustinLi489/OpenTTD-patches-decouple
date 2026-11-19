# R3R 合并上游 JGRPP 0.73.5 + 工作区改名 D:\R3R（2026-11-19）

## 一、盘点结论（只读，已实证）

| 项 | 值 |
| --- | --- |
| 本仓库分支 | `feature/decouple`，工作树**干净**，`ahead 5`（相对 origin） |
| HEAD | `3ce0c55699` R3R: sync working tree (rounds 431-466) |
| 已配远程 | `origin` = J*uustinLi489/OpenTTD-patches-decouple*；`upstream` = JGRennison/OpenTTD-patches |
| 分支基点 | `jgrpp-0.73.1` = `611aabd7ba`，且 `merge-base HEAD jgrpp-0.73.5` **就是它** |
| 目标 | `jgrpp-0.73.5` = `5389ea187f`（本仓库已有该 tag） |
| `D:\JGRPP` | `jgrpp` 分支 = `jgrpp-0.73.5-112-g1454103c7d`；其 **0.73.5 tag = `5389ea187f`，与本仓库逐字相同** |
| 上游缺口 | `0.73.1..0.73.5` = **562 个提交** |
| 我方分歧 | 相对 0.73.1，`src/` **121 个文件、+45 979 / −3 734** |

⇒ 结论 1：**不需要** `D:\JGRPP` 参与合并，本仓库的 `jgrpp-0.73.5` tag 已经是官方发布点；`D:\JGRPP` 的 `jgrpp` 分支比 0.73.5 还新 112 个提交（那是 0.73.6 开发线），**不要**拿它当合并目标。
⇒ 结论 2：这是"基点 0.73.1 → 0.73.5"的标准三方合并，`git merge jgrpp-0.73.5` 可直接用。

## 二、冲突试算（`git merge-tree --write-tree`，只写对象库，未动工作区）

冲突文件共 **18 个**（每个在试算输出里都有 stage 1/2/3 三条）：

| # | 文件 | 预判处理 |
| --- | --- | --- |
| 1 | `src/articulated_vehicles.cpp` | 上游改动 + 我方 R3R 打散逻辑 ⇒ **并集** |
| 2 | `src/depot_gui.cpp` | 并集（我方 ★/⊗、段拖动判据多） |
| 3 | `src/economy.cpp` | 并集 |
| 4 | `src/fios_gui.cpp` | 上游为主（我方改动少） |
| 5 | `src/framerate_gui.cpp` | 上游为主（我方 R3R_perf 探针） |
| 6 | `src/group_gui.cpp` | 并集 |
| 7 | `src/lang/simplified_chinese.txt` | 并集（字符串表，按上下文拼接） |
| 8 | `src/order_gui.cpp` | 并集（我方场/解挂 UI 多） |
| 9 | `src/saveload/vehicle_sl.cpp` | **上游优先**（存档兼容），R3R 块再补 |
| 10 | `src/settings_type.h` | **上游优先** |
| 11 | `src/sl/order_sl.cpp` | **上游优先** |
| 12 | `src/sl/saveload.cpp` | **上游优先**（SLV 版本号推进绝不能丢） |
| 13 | `src/train_cmd.cpp` | 并集（我方 ~4.6 万行级改动，最难） |
| 14 | `src/vehicle.cpp` | 并集 |
| 15 | `src/vehicle_base.h` | **上游优先 + 我方字段追加**（Vehicle 布局/SLV 字段顺序最敏感） |
| 16 | `src/vehicle_gui.cpp` | 并集 |
| 17 | `src/vehicle_gui_base.h` | 并集 |
| 18 | `src/window.cpp` | 上游优先（我方只加过空指针保护） |

## 三、合并策略（明确否决的做法）

- **否决** `git merge -X ours` 一刀切：会把上游在 `sl/*.cpp`、`saveload/vehicle_sl.cpp`、`settings_type.h`、`vehicle_base.h` 里的 **SLV 版本推进/字段迁移**整段丢掉，直接毁掉存档兼容（这是最贵的错误）。
- **否决** 让 `D:\JGRPP` 当合并源（它是 0.73.6 开发线）。
- 做法：`git merge --no-ff --no-commit jgrpp-0.73.5` → 逐文件按上表处理 → 全文编译 → 报错逐个修（上游 API 改名/签名变化会在这里暴露）。

## 四、安全措施

1. 合并前打**双保险**：`backup/pre-merge-0735` 分支 + `r3r-stable-2026-11-19-premerge` 标签，都指向当前 HEAD。
2. 合并中止随时可用 `git merge --abort` 回到干净树。
3. 合并结果**单独提交**（不混入 R3R 功能改动），提交信息注明 0.73.1→0.73.5。

## 五、工作区改名 `D:\sourcecode of JGRPP` → `D:\R3R`（本轮先只做评估，未执行）

已知约束：

1. **build 树里写满绝对路径**：`build/CMakeCache.txt`（`CMAKE_HOME_DIRECTORY`、vcpkg 工具链路径）与 `build/build.ninja` 全部是旧路径 ⇒ 改名后**必须**删除 `CMakeCache.txt` + `build.ninja` 重新 configure，等于一次全量重编（16~20 分钟，`-j2`）。
2. **IDE 占用**：当前会话的工作区就是该目录，Windows 下进程的当前目录/打开的文件会阻止目录改名 ⇒ 改名必须在 IDE 关闭时执行，或先 `robocopy` 到新路径再由玩家在新路径重开 IDE。
3. **脚本路径**：`R3R_fullbuild.cmd` / `R3R_inc_guard.ps1` 用 `%~dp0`，路径无关，无需改；但 `R3R_AB_probe.cmd` 里 `%WORK%` 与 `config.txt` 的 `r3rexe` 是硬编码绝对路径，改名后需同步。
4. 建议顺序：**先合并到能编译通过 → 再改名 → 改名后重新 configure 并一次全量编译**（合并本身改了大量头文件，反正躲不过一次全量）。

## 六、状态

- 合并：**冲突已全部解决并提交**（2026-11-19）。
  - 合并提交 `fafab88eb1`（`Merge upstream JGRPP 0.73.5 into R3R (base 0.73.1), conflicts resolved`），父提交为 `3ce0c55699` 与 `5389ea187f`（= tag `jgrpp-0.73.5`）。
  - 解冲突明细（按文件）：
    - 上游优先 + 补回 R3R 字段：`src/sl/saveload.cpp`（保留 SLERROR 写盘探针）、`src/sl/order_sl.cpp`（chunk 表取并集，`R3SM` 用 `ChunkType::SparseTable`）、`src/saveload/vehicle_sl.cpp`（R3R 三个 override 字段改用 `VarTypes::U16` + `SaveLoadVersion::R3RArticOverride`）、`src/settings_type.h`（`git checkout --theirs` 取上游后再补三个 R3R 设置项）、`src/framerate_gui.cpp`（保留 `R3RGetTrainsMs()`，采纳上游注释文本）。
    - 版本枚举：`src/saveload/saveload.h` 在 `MaxVersion` 前插入 `R3RArticOverride`(369)、`R3RParked`(370)。
    - 并集：`src/train_cmd.cpp`（include 取并集；`AM_REALISTIC` → `AccelerationModel::Realistic`；碰撞尾部改用上游 `TrainsCrashed()`；`TrainLocoHandler` 尾部采纳上游 `else if (!TrainCheckIfLineEnds(moving_front)) { j = 0; }` 重构并保留 R3R 停驶/移动分支探针，删掉重复的 `TrainCheckIfLineEnds` + `GetMovingFront` 两行）。
    - `src/vehicle_gui.cpp`：保留 R3R 段子行/临时挂接分组徽标块（我方侧已用上游的 `tr_row` / `shown_name`），仅把 `SA_LEFT/SA_RIGHT` 换成 `AlignmentH::Start` / `AlignmentH::ForceRight`。
    - `src/lang/simplified_chinese.txt`：18 个冲突块统一取我方译文，并补入仅上游有的 STR（`STR_TERRAFORM_TOOLTIP_PLACE_ROUGH_AREAS_ON_LANDSCAPE` 与 5 条 DIRECTORY 错误串）；复核结果 6365 行 / 5071 个 STR 名 / **0 重复 / 0 冲突标记**，我方独有 `STR_ERROR_CAN_T_MAKE_SEGMENT`、`STR_ERROR_CAN_T_DEMOTE_SEGMENT` 均已保留。
  - 旧对齐枚举残留全库清理：`SA_LEFT`→`AlignmentH::Start`、`SA_RIGHT`→`AlignmentH::ForceRight`、`SA_RIGHT | SA_FORCE`→`AlignmentH::ForceRight`、`SA_CENTER`→`AlignmentH::Centre`（DrawString）/ `{AlignmentH::Centre, AlignmentV::Middle}`（DrawSpriteIgnorePadding），共 8 处，涉及 `couple_group_gui.cpp` / `depot_gui.cpp` / `order_gui.cpp` / `vehicle_gui.cpp`；`Select-String` 复核 0 残留。
  - **注意**：`TBM_REALISTIC`/`TBM_ORIGINAL` 上游仍在 `vehicle_type.h` 保留定义，**不要**当成旧 API 去改。
  - 辅助脚本（放在 `build/`，纯 ASCII）：`build\R3R_lang_merge.ps1`（lang 冲突块取我方 + 补上游独有 STR）、`build\R3R_sa_fix.ps1`（对齐枚举替换）。冲突现场备份：`build\R3R_lang_conflicted_backup.txt`、`build\R3R_vehicle_gui_conflicted_backup.cpp`。
- 编译：**第一次全量构建在 CMake 配置阶段失败（vcpkg baseline），已处置后重跑**。
- 改名：**未执行**。

## 七、构建阻塞：上游 vcpkg baseline 本地缺失（2026-11-19）

**现象**：合并后首次 `R3R_fullbuild.cmd detach` 在约 20 秒内终止，`build\R3R_fullbuild.log` 尾部为 CMake 重新配置输出 + `vcpkg install --triplet x64-windows-static` 失败；`build\R3R_fullbuild.done` 无正常 `EXIT_CODE=0`。构建没进入编译阶段。

**根因**：`vcpkg.json` 的 `"builtin-baseline"` 被上游 0.73.5 改成了 `f3e106653cc27d62a37a3763cd84b38bca07c6075`（`git log -- vcpkg.json` 显示来自 `62f87a05ea`/`1c987bb0d3` 两次 upstream merge，最新一次引入），而本地 `D:\vcpkg-master` 仓库**没有这个 commit 对象**（`git cat-file -t` 报 `could not get object info`），vcpkg 无法解析 baseline ⇒ 配置阶段直接失败。

**取证**：
- 我方 0.73.1 时期与合并前 HEAD `3ce0c55699` 用的 baseline 都是 `b2cb0da531c2f1f740045bfe7c4dac59f0b2b69c`。
- `git -C D:\vcpkg-master cat-file -t b2cb0da5...` = `commit`（本地有）。
- `git -C D:\vcpkg-master show b2cb0da5...:versions/baseline.json | Select-String soxr` **命中** ⇒ 旧 baseline 已收录上游新增的 `soxr` 依赖，回退 baseline 不会因为缺 port 而失败。
- `D:\vcpkg-master\versions\s-\soxr.json` 存在（1054 B）。
- `CMakeLists.txt:146 find_package(Soxr)` **无 REQUIRED**；`link_package(Soxr ...)` 与 `src/CMakeLists.txt:55-56 soundresampler_soxr.cpp CONDITION SSoxr_FOUND` ⇒ 即使 soxr 不可用也只是关闭新音频重采样器，不影响主功能。

**尝试过但被取消**：`git -C D:\vcpkg-master fetch --depth 1 origin f3e10653...`（联网拉上游 baseline commit，用户中断）。

**处置（已落地）**：`vcpkg.json` 的 `builtin-baseline` 回退为 `b2cb0da531c2f1f740045bfe7c4dac59f0b2b69c`，保留 `soxr` 依赖项。这样依赖解析完全离线可完成，已安装的 `build\vcpkg_installed` 里各库版本不变（不会触发依赖重编）。

**后续若玩家希望跟上上游 baseline**：需要在有网络时执行
`git -C D:\vcpkg-master fetch --depth 1 origin f3e10653cc27d62a37a3763cd84b38bca07c6075`，
成功后把 `vcpkg.json` 的 baseline 改回 `f3e10653...`，再重新 configure（依赖若有版本提升会重编 vcpkg 库，耗时更长）。**当前不阻塞主流程，留作可选项**。

## 八、编译期 API 改名收敛（逐轮）

合并提交后进入「编译 → 读错误 → 修 → 重编」循环，逐个收敛上游 0.73.5 的枚举/类型现代化。已修点（按出现顺序）：

1. `src/pathfinder/yapf/yapf_rail.cpp` 8 处 `::TransportType::TRANSPORT_RAIL` → `TransportType::Rail`（L635/1603/1825/1839/1865/1896/1939/1969；L1839/L1969 在模板 `CYapfFollowCoupleRailT` 内，原报 `error C2838 成员声明中的限定名称非法`）。已复核全文件 0 残留。
2. `src/sl/couple_group_sl.cpp` chunk 表（L425-429）`CH_TABLE`/`CH_SPARSE_TABLE` → `ChunkType::Table`/`ChunkType::SparseTable`；同表尾部 `ChunkHandlerTable` 包装保持。
3. `src/sl/station_yard_sl.cpp` L428 `CH_SPARSE_TABLE` → `ChunkType::SparseTable`。
4. `src/pbs.cpp` L165 `GetTunnelBridgeTransportType(tile) == TRANSPORT_RAIL` → `== TransportType::Rail`。
5. `src/train_cmd.cpp` L20589 / L24940 探针里的 `GetTileTrackdirBits(x, TRANSPORT_RAIL, 0)` → `TransportType::Rail`（该函数第一参已改为 `TransportType`，见 `src/tile_track_func.h:34/39`）。
6. `src/sl/saveload.cpp` L505 `fprintf` 的 `(unsigned)string` → `(unsigned)string.base()`：`StringID` 已改为 `StrongType::Typedef<StringIDTag>`（`src/strings_id_type.h:19`），不再允许 C 风格强转，`base()` 取底层序号。
7. `EIT_*` 旧枚举全部消除（`EIT_IN_LIST`→`EngineImageType::InList`、`EIT_IN_DEPOT`→`EngineImageType::InDepot`），落点：`couple_group_gui.cpp:640`、`r3r_schedule_gui.cpp:618`、`vehicle_gui.cpp:2273`、`vehicle_gui.cpp:2455`、`depot_gui.cpp:881`、`depot_gui.cpp:1159`。

**判读注意**：`src/script/api/script_tile.hpp:105` 的 `TRANSPORT_RAIL` 是 Squirrel 脚本 API 的枚举常量（`ScriptTile::TRANSPORT_RAIL`），**不是** C++ `TransportType`，不要一并替换。`src/script/api/script_rail.hpp:195` 只是注释。`TBM_REALISTIC`/`TBM_ORIGINAL` 上游仍保留，亦不要改。

## 九、编译收敛第二轮（R3R 自有探针/UI 代码撞上上游现代化）

第八节那批修完后，错误点从「上游代码」转到「R3R 自己的补丁代码」，属于同一类现代化的后续：

8. `src/framerate_gui.cpp` `R3RGetTrainsMs()` 里 `_pf_data[PFE_GL_TRAINS]` → `_pf_data[PerformanceElement::GameLoopTrains]`（`PFE_*` 全部改为 `enum class PerformanceElement`；`src/train_cmd.cpp` 里剩余的 `PFE_GL_TRAINS` 只出现在注释中，不动）。
9. `src/r3r_schedule_gui.cpp` 两处 `DrawStringMultiLine(..., SA_TOP | SA_HOR_CENTER)` → 末参改 `{AlignmentH::Centre, AlignmentV::Top}`（`AlignmentH::Left/Centre/Right` + `AlignmentV::Top/Middle/Bottom`，见 `src/gfx_type.h`）。
10. `src/train_cmd.cpp` 的 `GVF_*` 位操作全部改枚举位集：`HasBit(w->gv_flags, GVF_GOINGUP_BIT) || HasBit(..., GVF_GOINGDOWN_BIT)` + 两次 `ToggleBit` → `gv_flags.Any({GroundVehicleFlag::GoingUp, GroundVehicleFlag::GoingDown})` + `gv_flags.Flip({...})`（落点 10254 附近、14538 附近的 `R3RNormaliseMixedChainDrivingBackwards`/反向块）；`SetBit(x->GetGroundVehicleFlags(), GVF_SUPPRESS_IMPLICIT_ORDERS)` → `x->GetGroundVehicleFlags().Set(GroundVehicleFlag::SuppressImplicitOrders)`（三处：10977、10978、14197）。`GetGroundVehicleFlags()` 返回引用，可直接 `.Set/.Reset/.Flip`。
11. `StringID` 强转残余（`StringID` = `StrongType::Typedef<StringIDTag>`，必须 `.base()`）：`train_cmd.cpp` 的 `R3RDumpAttachFail`（`(unsigned)error`）、`VALIDATE-FAIL` 探针（`(unsigned)cc.GetErrorMessage()`）、`TDTRY` 探针（`(uint64_t)(unsigned)ret.GetErrorMessage() << 1` 与 `err=0x%X` 实参）、`train_gui.cpp` 的 `R3R-NAME` 探针（`(int)v->GetEngine()->info.string_id`）、`vehicle_gui.cpp` 的 `R3R-REFIT-SUB`/`R3R-REFIT-SUB-KEEP` 探针（`(unsigned)subtype`、`(unsigned)list[pos].string`）。
12. `src/vehicle_gui.cpp` 的 `this->vli.type == VL_GROUP_LIST` → `VehicleListType::Group`（`VL_*` 宏全部改为 `enum class VehicleListType`，见 `src/vehiclelist.h:26`；`ALL_GROUP` 仍是宏，保留）。

**方法论**：`StringID` 强转这类错误一次只报几处，是因为 MSVC 按文件报错，而 R3R 探针散布在 `train_cmd/train_gui/vehicle_gui/vehicle_cmd/saveload` 等十来个文件里，只能靠编译器逐文件翻。凡是 `fprintf(..., %u/%d, (unsigned)/(int) <StringID 表达式>)` 的写法一律要 `.base()`。

## 十、全量构建通过（2026-11-19 21:11）

- 入口：`R3R_fullbuild.cmd detach`（默认全量模式，**未**再新建任何 `.cmd`）。
- 结果：`[698/698] Linking CXX executable openttd.exe`，日志尾行 `==== R3R build ended 2026/11/19 周四 21:11:20.74  MODE=full  EXIT_CODE=0`。
- 判决文件：`build\R3R_fullbuild.done` 时间 21:11:20，内容 `EXIT_CODE=0`。
- 错误计数：`Select-String` 对 `error C[0-9]+ | error LNK | fatal error | FAILED:` **命中 0**。
- 产物：`build\openttd.exe` 2026-11-19 21:11:00，**54 360 064 字节**（合并前 53 兆级已随上游增长）。
- 全量步数 698（比合并前 692 增加 6 步，来自上游新增编译单元）。

**结论**：上游 JGRPP 0.73.5 合并后的编译期适配（第 `八`/`九` 节共 12 条）已全部收敛，内测树（probes ON）可正常产出二进制。**发布树 `build-release` 未编**（按既定口径只在用户明确要求发布时才编），其二进制停留在合并前版本。

**本轮未提交改动共 19 个文件**（相对合并提交 `fafab88eb1`）：
`src/pbs.cpp`、`src/pbs.h`、`src/orders.h`、`src/station_base.h`、`src/train_cmd.cpp`、`src/train_gui.cpp`、`src/vehicle_base.h`、`src/vehicle_cmd.cpp`、`src/vehicle_gui.cpp`、`src/vehiclelist.cpp`、`src/framerate_gui.cpp`、`src/r3r_schedule_gui.cpp`、`src/couple_group_gui.cpp`、`src/depot_gui.cpp`、`src/order_gui.cpp`、`src/pathfinder/yapf/yapf_rail.cpp`、`src/sl/couple_group_sl.cpp`、`src/sl/station_yard_sl.cpp`、`src/saveload/saveload.h`、`src/saveload/vehicle_sl.cpp`。

**两处不在第八/九节清单里的补充修复**：
- `src/order_gui.cpp`：`DrawString(... SA_CENTER ...)` 一类 `StringAlignment` 旧宏 → `{AlignmentH::Centre}`（同 `r3r_schedule_gui.cpp` 的 family）。
- `src/saveload/saveload.h` + `src/saveload/vehicle_sl.cpp`：合并时上游 `enum class SaveLoadVersion` 升到 `368 DoubleEndedShips`，把 R3R 原有的两个自定义版本号条目挤掉；已按「接在上游末尾」的方式补回 `369 R3RArticOverride`、`370 R3RParked`，并把 `src/saveload/vehicle_sl.cpp` 的 `SLV_R3R_ARTIC_OVERRIDE` 用法改为 `SaveLoadVersion::R3RArticOverride`。`static_assert(to_underlying(MaxVersion) == SL_MAX_VERSION)` 编译通过 ⇒ 版本常量自洽。
  **风险提示（待人工确认）**：若合并前 R3R 的这两个自定义版本号不是 369/370（`git show 3ce0c55699:src/saveload/saveload.h` 里查不到 `R3R` 字样，说明合并前该文件是纯上游内容，R3R 侧的 `SLV_R3R_*` 在 `src/sl/saveload_common.h`），则本处的补偿是「重新引入」而非「恢复原值」，需用一张合并前的 R3R 存档做读档验证。

**观察（非合并引入，未改动）**：`src/CMakeLists.txt` 同时 `add_subdirectory(saveload)` 与 `add_subdirectory(sl)`，两套 `*_sl.cpp` 都被编译（`build.ninja` 里同时存在 `src\saveload\vehicle_sl.cpp.obj` 与 `src\sl\vehicle_sl.cpp.obj`）。核对合并前 R3R 提交 `3ce0c55699` 也是同样两行 ⇒ 属既有工程结构，不是本次合并产物。

**仍待做**：
1. 游戏内启动 + 最小场景验证（探针 `build\R3R_debug.log` 能正常写出、存档能读写）。
2. `git status` 复核后按需提交（本轮未提交）。
3. 若运行期出现 `0.73.5` 行为差异（尤其 orders/saveload/vehicle 相关），按「读日志 → 定位 → 改 → 记 KI」流程继续。

---

## 十一、两套存档目录的判定，与 R3R 覆盖字段的真缺口（2026-11-19）

### 11.1 判定：`src/sl` 是活的，`src/saveload` 是死副本
证据与读数：
- `src/CMakeLists.txt` 同时有 `add_subdirectory(saveload)` 与 `add_subdirectory(sl)`，两套都编译、都链接：`build\CMakeFiles\openttd_lib.dir\src\saveload\saveload.cpp.obj` 与 `src\sl\saveload.cpp.obj` 都在盘上，且都早于 `build\openttd.exe`。
- 但 `src/saveload/saveload.cpp`（2 066 行 / 80 153 B）里 **`SaveOrLoad` 零命中**；`src/sl/saveload.cpp`（4 115 行 / 155 805 B）含完整实现。⇒ 旧目录的入口已被掏空，只剩描述表/辅助函数，故链接期没有重复符号 —— 这也解释了第 139 行「698 步全量、EXIT_CODE=0」为何两套目录并存也能过。
- 该结构在合并前就存在：`git show 3ce0c55699:src/CMakeLists.txt` 同时含两行 `add_subdirectory`；`git ls-tree` 统计 `3ce0c55699` 下 `src/saveload`=70 文件、`src/sl`=60 文件。⇒ **不是本次合并引入**，不要当成合并冲突残留去删。
- 合并前后 `src/sl/vehicle_sl.cpp` 关于 R3R 字段一致：`r3r_parked` 的 `NSL(...)` 在，三个覆盖字段不在。

### 11.2 真缺口：R3R 铰接覆盖字段在活框架里根本没落盘
- 活表 `src\sl\vehicle_sl.cpp` 的 `_train_desc`：`gv_flags`(:1410) → CONDNULL 系列(:1411-1412) → `reverse_distance`/`speed_restriction`/`signal_speed_restriction`/`critical_breakdown_count`(:1413-1416) → `r3r_parked`(原 :1419)，**没有** `weight_override` / `power_override` / `max_speed_override`（全文件仅 :778 一行调试打印引用它们）。
- 死表 `src\saveload\vehicle_sl.cpp:210-212` 有这三条（`SaveLoadVersion::R3RArticOverride`），但它所在目录是死副本 ⇒ 既不写也不读。
- ⇒ 第 144 行那种「把 R3R 版本号补回 `src/saveload/saveload.h`」的补偿**对运行期无效**，属自我安慰；那一节的"风险提示"应当升级为"已证实的无效补偿"。
- 后果：去铰接组烘焙的功率/重量/限速（`R3RDearticulateOneGroup` 写入 `Train::*_override`，`train.h:292-294`）存档往返后回到 `UINT16_MAX`，段组统计丢失。

### 11.3 关键约束：不能复用 `SLV_R3R_ARTIC_OVERRIDE`(369) 当新字段的版本门
已有 R3R 存档的版本号是 370（`SAVEGAME_VERSION = SLV_R3R_PARKED`）。把三条字段挂在 `[369, MAX)` 上，读 369/370 旧档时会在"文件里没有这些字节"的位置读 3 个 uint16 ⇒ 记录错位/损坏。**必须新开版本号。**

### 11.4 落地（本轮改动，仅 2 个文件）
- `src\sl\saveload_common.h`：新增 `SLV_R3R_ARTIC_OVERRIDE_SAVED`（放在 `SLV_R3R_PARKED` 之后、`SL_MAX_VERSION` 之前 ⇒ 数值 371、`SL_MAX_VERSION`=372、`MAX_LOAD_SAVEGAME_VERSION`=371）；`SAVEGAME_VERSION` 由 `SLV_R3R_PARKED` 改为 `SLV_R3R_ARTIC_OVERRIDE_SAVED`（仍等于 `MAX_LOAD_SAVEGAME_VERSION`，`saveload.cpp:4306` 那句"bumping SAVEGAME_VERSION needs no change here"成立）。
- `src\sl\vehicle_sl.cpp`：`_train_desc` 在 `critical_breakdown_count` 之后、`r3r_parked` 之前插入三条 `SLE_CONDVAR(Train, *_override, SLE_UINT16, SLV_R3R_ARTIC_OVERRIDE_SAVED, SL_MAX_VERSION)`。
- 刻意**不**把字段挪到 `gv_flags` 后面（死表的顺序）：读 371 档时读写对称，而 369/370 档因 `from=371` 整段跳过，位置放在 r3r_parked 之前的兼容空洞之后最不易踩到 CONDNULL 对齐。

### 11.5 待验证
- 旧档（370）读入不得错位、`r3r_parked` 仍能读；新档（371）往返三项 `*_override` 一致。
- `src/saveload` 目录的最终处置（建议：从 `src/CMakeLists.txt:23` 移除 `add_subdirectory(saveload)` 并整目录删除；本轮未做，需人工确认无第三方引用）。

## 十一、两套存档目录的定性 + R3R 覆盖字段的真缺口（2026-11-19）

### 11.1 定性：`src/sl` 是活的，`src/saveload` 是**已摘掉入口的死副本**

| 证据 | 读数 |
| --- | --- |
| `src/CMakeLists.txt` | `:23 add_subdirectory(saveload)`、`:24 add_subdirectory(sl)` —— 两套都在编译 |
| 磁盘 obj | `build\CMakeFiles\openttd_lib.dir\src\saveload\saveload.cpp.obj` 与 `...\src\sl\saveload.cpp.obj` 都存在，且都早于 `build\openttd.exe` ⇒ 两套都进了链接 |
| 入口符号 | `src\saveload\saveload.cpp`（2 066 行 / 80 153 B）里 **`SaveOrLoad` 零命中**；`src\sl\saveload.cpp`（4 115 行 / 155 805 B）含完整实现 ⇒ 旧目录入口已被掏空，所以链接期没有重复符号，0.73.5 合并后全量 698 步 `EXIT_CODE=0` 与"两套目录并存"并不矛盾 |
| 合并前是否已如此 | `3ce0c55699` 的 `src/CMakeLists.txt` **同时**含两行 `add_subdirectory`；`src/saveload`=70 文件 / `src/sl`=60 文件 ⇒ 双目录结构是**合并前就存在**的历史遗留，不是本次合并引入 |

结论：`src/saveload/` 既不被调用、也不该再改；它只是"编译期负担 + 误改陷阱"。是否删除（连同 CMake 两行、`/FORCE` 之类兼容残留）需要单独拍板，本轮不动。

### 11.2 真缺口：R3R 的铰接覆盖字段在活框架里**从来没存过**

- 活表 `src\sl\vehicle_sl.cpp` 的 `_train_desc`：`gv_flags`(:1410) 之后是 `SLE_CONDNULL_X(2, CHILLPP)` / `SLE_CONDNULL(11, 2..144)` / `reverse_distance` / `speed_restriction` / `signal_speed_restriction` / `critical_breakdown_count`，然后是 `r3r_parked`(:1419)。**没有** `weight_override` / `power_override` / `max_speed_override`（全文件只有 `:778` 一行调试 printf 引用它们）。
- 死表 `src\saveload\vehicle_sl.cpp:210-212` 有这三条（`SaveLoadVersion::R3RArticOverride`..`MaxVersion`），但它所在目录是死副本 ⇒ 既不读也不写。
- 合并前同样缺：`git show 3ce0c55699:src/sl/vehicle_sl.cpp` 只有 `r3r_parked`，没有三条覆盖；旧目录那份有。
- ⇒ 第 143/144 行记录的那次"把 `369 R3RArticOverride` / `370 R3RParked` 补回 `src/saveload/saveload.h`"是**对死副本的补偿**，运行期无效；`SLV_R3R_ARTIC_OVERRIDE` 在活框架里也没有任何字段挂在它上面。
- 后果：`R3RDearticulateOneGroup()` 烘焙进 `Train::*_override` 的功率/重量/限速在**存档往返后全部丢失**（回落到 `UINT16_MAX` ⇒ 走原型值），去铰接组守恒被破坏。

### 11.3 约束：不能复用 `SLV_R3R_ARTIC_OVERRIDE`(369) 当新字段的版本门

现存 R3R 存档的版本号是 **370**（`SAVEGAME_VERSION = SLV_R3R_PARKED`），而 369/370 档的列车记录里**没有这三个 uint16 的字节**。若把它们挂在 `[369, MAX)` 上，读旧档会在不存在的数据位置读 3×2 字节 ⇒ 整条列车记录错位。必须开新版本号。

其余边界已核：`MAX_LOAD_SAVEGAME_VERSION = SL_MAX_VERSION - 1` 自动跟随；`:4321` 的"太新"判据、`:4322` 的 patchpack 区间（220–286）都不受影响；`SL_UPSTREAM_VERSION` 只是 XSLFI 子块自写自读的标记（`extended_ver_sl.cpp:774/786`），跟着上移无副作用。

> **订正（2026-11-19，见 §12）**：本节「现存 R3R 存档的版本号是 **370**」**记错了**。实测（`build\R3R_savhdr.txt`，15 个存档）旧档一律是 `raw=0x8170` ⇒ `ext=True`、`base=`**368**；370 只是合并后被重排的新编号。这不影响本节结论（新字段仍需开新版本号 371，且 368 旧档平移后 = 370 仍 < 371 ⇒ 照旧跳过），但 §12 的平移修法正是因这个错记而被漏掉的。

### 11.4 落地（本轮）

1. `src/sl/saveload_common.h`：新增 `SLV_R3R_ARTIC_OVERRIDE_SAVED`（371），`SAVEGAME_VERSION` 改为它；`SLV_R3R_ARTIC_OVERRIDE` 的注释改为"保留边界、无字段使用"。
2. `src/sl/vehicle_sl.cpp`：`_train_desc` 中 `r3r_parked` **之前**插入三条 `SLE_CONDVAR(Train, weight_override/power_override/max_speed_override, SLE_UINT16, SLV_R3R_ARTIC_OVERRIDE_SAVED, SL_MAX_VERSION)`（放在 `critical_breakdown_count` 之后，避开 chilpp 的 `CONDNULL` 兼容占位区）。
3. `src/sl/saveload.cpp:4302` 的过时注释订正（原说 `SAVEGAME_VERSION` 是 `SLV_R3R_ARTIC_OVERRIDE`）。
4. `src/saveload/*` 的死副本只做一处同步（见 11.5），其余补偿原样保留。

**复测判据**：①去铰接组列车存档→读档后列车窗口的功率/重量/最高速度与存档前一致（不再回落到原型值）；②370 旧档仍能正常读入（新字段缺席 ⇒ 保持 `UINT16_MAX`，不得错位/报错）；③新档写出的版本值为 371；④原生（未去铰接）列车存档往返逐项不变。

### 11.5 补记：死副本的版本门同步到 371（避免口径自相矛盾）

- 改动：`src\saveload\vehicle_sl.cpp:206-212` 三条 `SLE_CONDVAR(... weight_override / power_override / max_speed_override ...)` 的起点由 `SaveLoadVersion::R3RArticOverride`(369) 改为 `SaveLoadVersion::R3RArticOverrideSaved`(371)，并加注释说明 369/370 档里没有这三组字节。
- 理由：11.4 第 4 条原写"死代码不追改"，但留着 369 会让两套表口径互相矛盾——将来若有人把 `src/saveload` 重新接回入口，或按死表反推"369 已有数据"去写迁移代码，就会在 370 旧档上多读 6 字节导致整条 `VEHS` 记录错位。改后死表与活表在**字段集合、顺序、门限**三方面逐项一致（死表仍是 `SaveLoadTable`/`VarTypes::U16` 写法，活表是 `NSL`/`SLE_UINT16` 写法）。
- 影响面：`src/saveload/*` 不定义 `SaveOrLoad`（`git grep` 只有 `src\sl\saveload.cpp` 定义），运行期零影响；仅使该目录重新编译一个 obj。
- 仍未做：删掉整个 `src\saveload\` 死副本 + `src/CMakeLists.txt` 两行 + 一次全量重编（需单独拍板，`src\sl` 里若仍有对 `saveload/*.h` 的 include 需先解耦）。

## 十二、读档兼容修复：合并前的 R3R 旧档（base=368）读不进去（2026-11-19 续三）

### 12.1 现象与报错闭环

- 玩家复跑后仍报「无法读取旧存档」（合并后内测 exe）。
- `build\R3R_debug.log`：读档失败行 `SLERROR ... stringid=4237 ... chunk=0 arr=0`；`build\generated\table\strings.h` 里 `0x108D = 4237 = STR_GAME_SAVELOAD_ERROR_TOO_NEW_SAVEGAME`。`chunk=0` ⇒ 死在文件头版本校验，还没进 chunk。

### 12.2 存档头实测（关键读数）

`build\R3R_savhdr.txt`（本轮把 `Documents\OpenTTD\save\*.sav` 最近 15 个按 mtime 倒序解析前 8 字节：`OTTX` + 大端 16 位版本）：

| 形态 | 读数 | 个数 |
| --- | --- | --- |
| R3R 旧档 | `raw=0x8170` ⇒ `ext=True`、`base=368` | 13 / 15（含 `T8701-2_4.sav`、`南昌铁路枢纽示意, 2026-01-01.sav`） |
| 上游 JGRPP 档 | `raw=0x016A` ⇒ `base=362`、`ext=False` | 2 / 15 |

⇒ 「旧档 = 368 + EXT 位」是唯一形态，没有 370 的档。

### 12.3 合并前编号（`git show 3ce0c55699:src/sl/saveload_common.h`）

上游列表止于 `366 SLV_DEPOTS_UNDER_BRIDGES`，其后是 R3R 自己的 `367 SLV_R3R_ARTIC_OVERRIDE`、`368 SLV_R3R_PARKED`，`SAVEGAME_VERSION = SLV_R3R_PARKED`(368)、`MAX_LOAD_SAVEGAME_VERSION = 368`。

合并 0.73.5 后上游新增 `367 SLV_LABEL_ORIENTATION_UNIFICATION`、`368 SLV_DOUBLE_ENDED_SHIPS` ⇒ R3R 两个版本被挤到 369/370（本轮再加 371）⇒ **同一个整数换了含义**，这才是旧档读不了的根源。

### 12.4 两层根因

1. **直接被拒**：`src\sl\saveload.cpp` 的 ext 分支守卫要求 `_sl_version >= SLV_R3R_ARTIC_OVERRIDE`(369)，旧档 base=368 < 369 ⇒ `SlError(TOO_NEW)`。
2. **放行也会毁档（更危险）**：`SLV_R3R_PARKED` 现在是 370，旧档 `_sl_version=368` 会让 `IsSavegameVersionBefore(370)` 为真 ⇒ `Train::r3r_parked`（`src\sl\vehicle_sl.cpp:1428`）不读，而文件里这个字节**存在**（合并前 `SAVEGAME_VERSION=368` 已写出）⇒ `VEHS` 列车记录整体错位 1 字节，后续字段全乱。所以不能只放宽守卫。

### 12.5 修法：读档时把 367/368 平移为 369/370

1. `src\sl\saveload_common.h` 新增三个常量（带注释说明合并前后编号错位）：
   `SLV_R3R_LEGACY_FIRST = SLV_DEPOTS_UNDER_BRIDGES+1`(367)、`SLV_R3R_LEGACY_LAST = SLV_DEPOTS_UNDER_BRIDGES+2`(368)、`SLV_R3R_LEGACY_SHIFT = SLV_R3R_ARTIC_OVERRIDE - SLV_R3R_LEGACY_FIRST`(2)。
2. `src\sl\saveload.cpp` 的 ext 分支在守卫**之前**平移：
   ```cpp
   if (_sl_version >= SLV_R3R_LEGACY_FIRST && _sl_version <= SLV_R3R_LEGACY_LAST) {
       const SaveLoadVersion legacy_version = _sl_version;
       _sl_version = (SaveLoadVersion)((uint)_sl_version + SLV_R3R_LEGACY_SHIFT);
       Debug(sl, 1, "Remapping legacy R3R savegame version {} to {}", legacy_version, _sl_version);
   }
   ```
3. `SAVEGAME_VERSION`(=371) / `MAX_LOAD_SAVEGAME_VERSION` 不动。

### 12.6 为何平移安全（逐项已证）

1. **上游 367/368 全树零字段门**：`grep SLV_LABEL_ORIENTATION_UNIFICATION|SLV_DOUBLE_ENDED_SHIPS src` 只命中 `extended_ver_sl.cpp` 的 XSLFI 特性表，没有任何 `IsSavegameVersionBefore()` 落在它们上面 ⇒「把 368 当 370 看」不会多读/少读一个字节。
2. **上游 side（table chunk）用 `_sl_xv_upstream_version`**（`SlExecWithSlVersion`，旧档随 XSLFI_UPSTREAM_VERSION 记 368，等价 ≤366）⇒ 不受 `_sl_version` 平移影响。
3. **新增的 `weight/power/max_speed_override` 门在 371**，旧档 368→370 仍 <371 ⇒ 照旧跳过，与合并前 `_train_desc`（`git show 3ce0c55699:src/sl/vehicle_sl.cpp`：只有 `r3r_parked`、无三条 override）的落盘布局逐字一致。
4. **非 ext 档**（上游 JGRPP 的 `base=362`）走 `_sl_upstream_mode` 的 else 分支，一字未动。

### 12.7 构建自证（2026-11-19）

- 复用既有 `R3R_fullbuild.cmd inc detach`（未新建任何 `.cmd`）；改 `src\sl\saveload_common.h` ⇒ 护栏判全量。
- `build\R3R_incbuild.log` 尾部 `[698/698] Linking CXX executable openttd.exe` + `==== R3R build ended 2026/11/19 23:22:31.99 MODE=inc EXIT_CODE=0`；`build\R3R_incbuild.done` = `EXIT_CODE=0`；错误计数 0。
- 时间戳：`saveload_common.h` 22:56:46 → `saveload.cpp` 22:57:12 → `saveload.cpp.obj` 23:08:03 → `build\openttd.exe` 23:22:11（54 361 600 B，晚于全部源码）；`read_lints` 0 条。发布树未编。
- **过程教训（玩家已批评）**：本轮先起全量构建、到 666/698 才发现还要改 `saveload_common.h`，导致全量白跑一遍。此后凡触及 `src\*.h` 的修复，必须**在启动构建前**把全部头文件改动定稿并复核。

### 12.8 待复测判据

1. 用新 `build\openttd.exe` 打开 `T8701-2_4.sav` 等 11-19 之前的 R3R 旧档，不再弹「存档版本太新」，能进主界面。
2. 进入后列车「停放」状态（`r3r_parked`）仍在，无「Referencing invalid Vehicle」之类错位报错。
3. 旧档里原本已耦合/去铰接的列车读入后链序、车号、段标记与存档前一致。
4. 存新档再读回正常（`raw=0x8171`，base=371）。
5. 上游 JGRPP 档（base=362、无 EXT 位）照旧能读。

**未做**：`src\saveload\` 死副本仍在（沿用 §11.5 的口径只同步版本门）；`T8701-2_4.sav` 等旧档的实机读档验证尚未由玩家执行。

> **订正（2026-11-19 续四，见 §13）**：本节第 2 条括号里写的「旧档随 XSLFI_UPSTREAM_VERSION 记 368，等价 ≤366」**判断正确，但结论用错**——
> 正因为"等价 ≤366"，才必须把它显式改写成 ≤366；`_sl_version` 平移根本到不了这里，而 label 方向门恰恰读这个数。
> 本轮新增 §13 修掉。

## 十三、读档兼容修复（续四）：旧档 rail/road type 全部回落默认类型 — KI-666

### 13.1 玩家报告（原始症状）

> 「这次存档能读取了，但是我注意到，存档内所有的铁轨类型都变成了最低等级的非电气化的铁路，所有的道路也变成了购买下拉菜单的第一类道路（估计基建也一样），这造成的后果很严重」

即：第 363 轮的 `_sl_version` 平移解决了「读不进去」，但读进来的地图**类型表全废**。

### 13.2 取证：RAIL/ROTT 走的是 UV，不是 `_sl_version`

1. `src\sl\labelmaps_sl.cpp`（全文 24 行）只注册 `MakeUpstreamChunkHandler<'RAIL'|'ROTT', GeneralUpstreamChunkLoadInfo>()`；
2. `src\sl\saveload.h:158` 与 `:203`：`SlExecWithSlVersion(F::GetLoadVersion(), ...)`；
3. `src\sl\saveload.cpp:4749-4755`：
   ```cpp
   SaveLoadVersion GeneralUpstreamChunkLoadInfo::GetLoadVersion()
   {
       ChunkType chunk_type = _sl.chunk_block_modes[_sl.current_chunk_id];
       return (chunk_type == ChunkType::Table || chunk_type == ChunkType::SparseTable) ? _sl_xv_upstream_version : _sl_version;
   }
   ```
   `RAIL`/`ROTT` 的 `ChunkHandler` 构造是 `ChunkHandler("RAIL", ChunkType::Table)`（`src\saveload\labelmaps_sl.cpp:27/74`）⇒ 取 **UV**。
4. `SlExecWithSlVersionStart()`（`src\sl\saveload.cpp:4735`）直接改写全局 `_sl_version`，作用域结束再还原 ⇒ 旧框架里所有
   `IsSavegameVersionBefore()` 在 chunk 加载期间看到的就是 UV。

⇒ **§12.6 第 2 条的「不受 `_sl_version` 平移影响」在字面上成立，但恰好意味着这条路径什么都没被修。**

### 13.3 取证：label 反转的真实判据与旧档 UV 的取值

- `src\saveload\labelmaps_sl.cpp`（`namespace upstream_sl`，与 `src\sl\labelmaps_sl.cpp` 是两套符号）：
  - `:67`（RAIL）、`:106`（ROTT）：
    `if (IsSavegameVersionBefore(SaveLoadVersion::LabelOrientationUnification)) std::ranges::reverse(lo.label);`
  - `LabelOrientationUnification = 367`（`src\saveload\saveload.h:435`，0.73.5 新增，"Unify the orientation in which labels are written"）。
- 合并前 `SL_UPSTREAM_VERSION = MAX_LOAD_SAVEGAME_VERSION = SLV_R3R_PARKED = 368`（`git show 3ce0c55699:src/sl/saveload_common.h`）；
  `saveUV()`（`src\sl\extended_ver_sl.cpp:786`）无条件写这个值；
  子块 `UV` 的注册类型是 `XSCF_NULL` ⇒ **每个 ext 档都带**。
- ⇒ 旧档 `_sl_xv_upstream_version = 368` ⇒ `IsSavegameVersionBefore(367)` = **false** ⇒ 不反转
  ⇒ `_railtype_list` / `_roadtype_list` 里的 label 字节序与写入时相反（`"RAIL"` 读成 `"LIAR"`）
  ⇒ `AfterLoadLabelMaps()` 里 `GetRailTypeByLabel`/`GetRoadTypeByLabel` 全部找不到 ⇒ 整图回落到默认类型。

这与玩家三条症状（最低级非电气化铁路、第一种道路、基建同样）逐字吻合。

### 13.4 修法（3 处，全在 `src\sl\`）

| # | 文件 | 位置 | 内容 |
| --- | --- | --- | --- |
| ① | `src\sl\saveload_common.h` | `SLV_R3R_LEGACY_SHIFT` 之后 | 新增 `extern bool _sl_r3r_legacy_numbering;` + 完整注释（UV 与 label 方向的关系） |
| ② | `src\sl\saveload.cpp` | `:110` 定义；`:4313` 平移块内 | `bool _sl_r3r_legacy_numbering;`；平移成立时 `= true` |
| ③ | `src\sl\extended_ver_sl.cpp` | `SlXvResetState()` `:306`；`loadUV()` `:770` | 复位 `= false`；读入 UV 后、too-new 检查前，若 legacy 且 `UV ∈ [367,368]` ⇒ `UV -= 2` 并打 `Debug(sl, 1, "Remapping legacy R3R upstream savegame version {} to {}")` |

判据语义：合并前上游版本列表止于 `366 SLV_DEPOTS_UNDER_BRIDGES`，旧档里根本不存在 367/368 两个上游特性 ⇒ 拉回 366 才是"旧档的真实上游状态"。

### 13.5 为什么不是其它位置

- **不能改 `GetLoadVersion()`**：它对 table chunk 返回 UV 是通用语义，改它会波及所有 table chunk。
- **不能改 `loadUV` 的 too-new 上界**：与本问题无关。
- **不能把 `SL_UPSTREAM_VERSION` 改成 368**：会让现有 exe 的 too-new 检查（`UV > SL_UPSTREAM_VERSION`）拒读合并后写出的档（那些档 UV=371）。
- **必须在 `loadUV` 而不是 chunk 侧**：UV 只在这里落地，且它早于 RAIL/ROTT 被读（否则本轮症状不会出现，会是"反转反而没错"）。

### 13.6 边界

- 上游 JGRPP 档（`raw=0x016A`、`base=362`、`ext=False`）走 `_sl_upstream_mode`，`_sl_r3r_legacy_numbering` 恒 false ⇒ 一字不动。
- 合并后新写的档：`saveUV()` 写 371 ≥ 367 ⇒ 本来就不反转，且不置 legacy 标志 ⇒ 不受影响。
- 旧档若无 UV 子块（理论上不存在）⇒ UV = `SL_MIN_VERSION` < 367 ⇒ 反转正确。
- 已知遗留：`SL_UPSTREAM_VERSION`(=371) 与"真实上游版本"(=368) 语义不一致，属"断言所有上游特性都具备"，无害；修它要连带改 too-new 检查，本轮不动。

### 13.7 构建自证（2026-11-19 续四）

见 `R3R_KNOWN_ISSUES.md` 的 KI-666（本轮先落盘修法与判据，构建结果回填于该条目）。**过程约束**：本轮触及 `src\*.h`（`saveload_common.h`），
已按 §12.7 教训**在起构建前**把三处改动全部定稿并复核完毕。

