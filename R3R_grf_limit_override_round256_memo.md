# 第 256 轮：新增「无视 NewGRF 连挂 / 启停限制」开关（临时分析报告）

日期：2026-10-20
状态：已实现 + 已编译（游戏内复测待做；见 §七 构建自证、§八 当前状态）

---

## 一、需求（玩家原话）

> 添加一个设置选项，可以暂时无视各种 grf 作者设置下的列车的 start_stop 和 can_attach_wagon 限制，这样就可以避免「grf 作者只设置了图像级别的假重联，而我想玩能发配和歌山的真重联」了（或者有了这个你就可以逼迫你喜欢的 grf 作者设计适配 R3R 的组件了，笑）

- 动机 A：某些 GRF 用 `can_attach_wagon`（0x1D）禁止某些车厢组合，只提供「图像级假重联」；玩家想强行拼成真正的重联/固定编组。
- 动机 B：某些 GRF 用 `start_stop`（0x31）锁死车辆（例如只允许整组一起启停、或禁止某节单独启动），会挡住 R3R 的段级调度。
- 非目标：不改任何物理/编组/调度语义，只放开这两个 NewGRF 回调的否决权。

## 二、取证：两个限制的唯一判定现场（静态读码）

| 限制 | 回调 | 唯一求值点 | 证据 |
| --- | --- | --- | --- |
| 车厢连挂 | `CBID_TRAIN_ALLOW_WAGON_ATTACH = 0x1D` | `src/train_cmd.cpp:2852`，位于 `static CommandCost CheckTrainAttachment(Train *t)`（:2798-2908） | 全树 `GetVehicleCallbackParent(CBID_TRAIN_ALLOW_WAGON_ATTACH, ...)` 仅此一处；`CheckTrainAttachment` 被 `ValidateTrains()`（:2932 src / :2934 dst）与 R3R 耦合提交点（:10398 `CheckTrainAttachment(head).Succeeded()`）调用 |
| 启动/停止 | `CBID_VEHICLE_START_STOP_CHECK = 0x31` | `src/vehicle_cmd.cpp:1408`，位于 `CmdStartStopVehicle()`（:1373 起）的 `if (evaluate_startstop_cb)` 块（:1406-1434） | 全树 `GetVehicleCallback(CBID_VEHICLE_START_STOP_CHECK, ...)` 仅此一处 |

补充取证：
- `CheckTrainAttachment` 对「自由车厢链」（`!t->IsEngine()`，:2808-2821）只校验 `max_train_length`，不评估回调；对真铰接件（`IsArticulatedPart()`）与双头后半（`IsDualHeadRear()`）也跳过回调（:2843）。⇒ 新增开关只影响「引擎头链 + 普通车厢」这一段判定。
- `CmdStartStopVehicle` 的硬规则在回调之前/之外：`CheckVehicleControlAllowed`（:1381）、已毁（:1384）、`STR_ERROR_TRAIN_START_NO_POWER`（:1388）、飞机空中不能停（:1398-1399）。⇒ 开关只跳过回调，不放开这些硬规则。
- 设置体系：JGRPP 新增设置走 `src/table/settings/game_settings.ini` 的 `[SDT_BOOL]` + `src/settings_type.h` 字段 + `src/settingentry_gui.cpp` 页面注册；因为这些是 JGRPP 专有设置，需 `SettingFlag::Patch`（:42「不从上游 PATS 加载」）与 `patxname`（存档按名保存，老档缺失时保持默认，无需 `settings_sl_compat` 迁移）。
- 语言串位置：JGRPP 专有设置串在 `src/lang/extra/english.txt` / `src/lang/extra/simplified_chinese.txt`（英文用 `{STRING2}`、中文用 `{STRING}`），不写主语言文件。

## 三、设计决定

玩家说「一个设置选项」，但两种限制的风险面完全不同（放开连挂可能拼出 GRF 没有图像的组合；放开启停通常无害且是 R3R 段级调度的必需），故拆成**两个相邻开关**，仍属同一功能：

1. `vehicle.ignore_grf_attach_limits`（默认 false）
   - 语义：不评估 `CBID_TRAIN_ALLOW_WAGON_ATTACH`，等价于回调恒返回「允许」。
   - 边界：`max_train_length` 仍然校验（长度是游戏规则，不是 GRF 限制）；真铰接/双头后半本来就跳过回调，行为不变。
2. `vehicle.ignore_grf_startstop_limits`（默认 false）
   - 语义：不评估 `CBID_VEHICLE_START_STOP_CHECK`。
   - 边界：无动力不能启动、飞机空中不能停、公司权限等一律照旧；`AutoReplace` 的 `evaluate_startstop_cb=false` 语义不变（开关只是额外短路，不改变何时愿意评估）。

放置：
- `cat = SC_EXPERT`（只影响编组合法性，属高级/专家项）。
- GUI 注册在 `Settings → Limitations` 页，紧跟 `vehicle.max_train_length`（同属「限制」语义）。
- 存档：`patxname` 按名写入 savegame；旧档没有这两项 ⇒ 取 `def = false`，行为与升级前逐字一致。

## 四、落地清单（代码）

| 文件 | 改动 |
| --- | --- |
| `src/settings_type.h` | `VehicleSettings` 末尾追加两个 `bool` |
| `src/table/settings/game_settings.ini` | `vehicle.non_leading_engines_keep_name` 之后追加两个 `[SDT_BOOL]` |
| `src/settingentry_gui.cpp` | `limitations` 页 `vehicle.max_train_length` 之后注册两项 |
| `src/train_cmd.cpp` | `CheckTrainAttachment` 的回调判定条件追加 `&& !_settings_game.vehicle.ignore_grf_attach_limits` |
| `src/vehicle_cmd.cpp` | `if (evaluate_startstop_cb)` → 追加 `&& !_settings_game.vehicle.ignore_grf_startstop_limits` |
| `src/lang/extra/english.txt` / `simplified_chinese.txt` | 新增 4 条串（2 主串 + 2 帮助串） |

副作用评估：
- 只改判定条件，不改链序/位置/调度；关闭开关时执行路径与改动前逐字相同（短路条件是 `false`）。
- 打开开关时 `CheckTrainAttachment` 仍走「断开—重连」循环，只跳过回调，故不会破坏链结构。
- 打开连挂开关后 R3R 的耦合闸门（`FOLDCHK` 等）仍全部生效：本开关只放开 GRF 否决，不放开 R3R 自己的合法性判定。

## 五、未确认 / 风险（待验证）

- U-1：打开 `ignore_grf_attach_limits` 后，GRF 若在 `can_attach_wagon` 回调里做**副作用**（修改寄存器/缓存）是否会因为不再调用而产生可见差异？按 NewGRF 规范回调应是纯函数，风险低。
- U-2：某些 GRF 用 `start_stop` 回调实现「重联组必须整组启停」，强行单节启动后可能出现图像/运动异常（非崩溃，属 GRF 自身假设被打破）。
- U-3：`CmdStartStopVehicle` 的 `evaluate_startstop_cb` 在 `AutoReplace` 下本就可被关掉，说明引擎已容忍「不评估回调」的路径，风险面有先例。
- U-4：多人游戏必须两端设置一致（`Patch` 设置随存档/网络同步）；不一致时表现为客户端预测与服务端校验差异，属既有设置体系行为。

## 六、复测判据

1. 默认（两开关关闭）：既有存档读入、编组、启停行为与第 255 轮 exe 逐字一致；日志无新增差异。
2. 只开 `ignore_grf_attach_limits`：用原本被 GRF 拒绝的车厢组合（图像级假重联车）可在车库/站台经 R3R 挂上，不再弹 GRF 的错误串；`max_train_length` 仍然拦截超长列车。
3. 只开 `ignore_grf_attach_limits`：R3R 自己的耦合判据（折叠/真铰接拒绝/双头语义）不受影响，正常挂车仍成功。
4. 只开 `ignore_grf_startstop_limits`：原本被 `start_stop` 锁死的车辆可用「启动/停止」按钮切换；无动力机车仍报 `STR_ERROR_TRAIN_START_NO_POWER`；飞行中的飞机仍报 `STR_ERROR_AIRCRAFT_IS_IN_FLIGHT`。
5. 两个开关都打开：不崩溃、不 desync（联机两端一致时）。
6. 存档往返：开关状态随存档保存/读回；老档（无该字段）读入后为 false。

## 七、构建自证

复用既有 `_tmp_inc_build.cmd`（未新建任何 .cmd）。因动了 `src\settings_type.h` 与两个 lang txt，KI-183 护栏按设计升级为全量：

- `build\R3R_incbuild.guard.log`：`GUARD: newest object = 2026-10-20 14:47:20`；`newer: src\lang\extra\simplified_chinese.txt (18:03:17)` / `english.txt (18:03:15)` / `src\settings_type.h (18:02:52)`；`GUARD: REMOVED 620 object file(s) - upgrading this build to a FULL rebuild`。
- 结果：`[703/703] Linking CXX executable openttd.exe`，`build\R3R_incbuild.done` = `EXIT_CODE=0`，日志中 `error C` / `fatal error` / `FAILED:` / `build stopped` 计数 **0**。
- 产物：`build\openttd.exe` = **52 004 352 B @ 2026-10-20 18:30:05**，晚于全部改动源码（settings_type.h 18:02:52、game_settings.ini 18:02:54、settingentry_gui.cpp 18:02:57、vehicle_cmd.cpp 18:02:59、train_cmd.cpp 18:03:08、两个 lang txt 18:03:15/18:03:17）。
- obj 时间戳：`train_cmd.cpp.obj 18:26:04`、`vehicle_cmd.cpp.obj 18:26:21`、`settingentry_gui.cpp.obj 18:23:54`，均晚于对应 .cpp。
- 生成表：`build\generated\table\settings.h`（18:06:45）第 588/589 行出现
  `SDT_BOOL(GameSettings, vehicle.ignore_grf_attach_limits, SettingFlags({SettingFlag::Patch}), false, STR_CONFIG_SETTING_IGNORE_GRF_ATTACH_LIMITS, ... , SC_EXPERT, nullptr, false, "vehicle.ignore_grf_attach_limits")` 与 startstop 同构行 ⇒ ini 语法与 `patxname` 解析通过。
- 字符串：`build\generated\table\strings.h`（18:06:41）第 5049-5052 行 = `0x88E4`/`0x88E5`/`0x88E6`/`0x88E7`；`build\lang\english.lng` 命中 “Ignore NewGRF wagon attachment restrictions”，`build\lang\simplified_chinese.lng` 命中 “无视 NewGRF 车厢连挂限制”。
- `findstr` 在 exe 内命中两个 `patxname` 字面量（`vehicle.ignore_grf_attach_limits` / `vehicle.ignore_grf_startstop_limits`）。
- `read_lints` 对 4 个 C++ 文件 = 0 条。

## 八、当前状态

- 代码：已实现，已编译（内测树 `build\openttd.exe`，探针 ON）。**发布树 `build-release` 未编**（按 2026-10-19 口径，只在明确要求发布时才编）。
- 游戏内复测：**未做**（按约束 AI 不启动游戏）。复测判据见 §六，重点验证「默认关闭时行为逐字不变」与「打开后能挂上原本被 GRF 拒绝的组合」。
