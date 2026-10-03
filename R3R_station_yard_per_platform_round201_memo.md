# R3R 第 201 轮 临时分析报告：站场按「逐个站台」设置（KI-304）+ 清除「前半/后半」UI 残留接线（KI-305）

- 日期：2026-10-02
- 分支：feature/decouple（工作区 d:\sourcecode of JGRPP）
- 状态：**根因已确认 / 修法已定 / 实现中**（按玩家规则「明确了需求/现象/根因/修法就立即写入备忘，再执行下一步」，本文档随施工进度增补）

---

## 一、本轮玩家原话（作业单）

> 你正在修复openttd的解挂补丁包R3R版本的问题。之前，我要求你修复了两个不应存在的UI和一个选择站台停靠位置的异常
> 这两个（前半，后半）UI已经被消灭，选择停靠位置也暂无发现异常，当然，这两个（前半，后半）UI相关的接线也可以顺便消灭（不要误伤别的）
> 然后，我要求你修复关于R3R的站场功能。我希望，设为站场，是逐个站台进行站场的设置，但是现状是，一次性建设的站台都算作一个站台

拆成三项：

| 项 | 内容 | 本轮处理 |
| --- | --- | --- |
| 结项 | KI-303 的「前半 / 后半」两个 UI 已被消灭；KI-302 的停靠位置异常暂未复现 | 仅登记口径，不重复施工 |
| 任务 A | 把已无 UI 入口的「前半 / 后半」**接线**一并清除，**不得误伤其它功能** | 本轮新任务（KI-305） |
| 任务 B | R3R 站场功能：**设为站场必须逐个站台设置**；现状是「一次性建设的站台都算作一个站台」 | 本轮新任务（KI-304） |

---

## 二、任务 B（KI-304）现象 —— 玩家口径

玩家原话：「我希望，设为站场，是逐个站台进行站场的设置，但是现状是，一次性建设的站台都算作一个站台。」

**已明确的期望**：站场的**划分 / 设置粒度 = 单个站台**。玩家应当在站场界面里对「站台 A」「站台 B」「站台 C」逐个指定，而不是被系统按某种粗糙单位（一次建设动作 / 整站）合并。

**两个歧义的取证结论**：

1. 「站台」的实体定义 = 沿站台轴（`GetRailStationAxis`）连续、同站且轴兼容（`IsCompatibleTrainStationTile`）的 tile 串。一个平台在站场列表里占一行。
2. 「现状把它们算作一个站台」的判定依据落在 `R3REnumeratePlatforms()`（枚举函数）—— 见第四节。

---

## 三、任务 A（KI-305）现象 —— 玩家口径

第 200 轮（KI-303）只删了 UI 与两个下拉框的**入口**，当时**刻意保留**了下列「接线」，并在 KI-303 条目里登记为「明确保留 / 遗留」：

- 命令 `MOF_DECOUPLE_ORDERS` 与其在 `CmdModifyOrder` 里的白名单分支；
- `Order::SetDecoupleFirstOrdersType()` / `SetDecoupleSecondOrdersType()` 两个 setter；
- `ODOF_*` 枚举（`ODOF_KEEP_ORDERS` / `ODOF_KEEP_ORDERS_NO_LOAD` / `ODOF_INHERIT_ORDERS` / `ODOF_WAIT_FOR_COUPLE`）与 `Order` 里的 `decouple_first_orders` / `decouple_second_orders` 两个字段、`GetDecoupleFirstOrdersType()` / `GetDecoupleSecondOrdersType()`；
- `src\train_cmd.cpp` 里读这两个字段决定「解出方 / 留下方各自继承哪一份排程」的**解挂策略解析器**；
- `src\lang\english.txt` / `simplified_chinese.txt` 里已无引用的 `STR_ORDER_DECOUPLE_*` 系列语言串。

本轮玩家授权：「这两个（前半，后半）UI 相关的接线也可以顺便消灭（**不要误伤别的**）」。

**取证补充**：`src\order_gui.cpp` 对上述所有符号 **0 命中** ⇒ UI 入口确实已彻底删除，剩下的全是「无入口的接线」。

---

## 四、任务 B（KI-304）根因 —— 已确认

根因在 `src\station_cmd.cpp` 的 `R3REnumeratePlatforms()`（:2079-2093）：它用「线性 tile 范围」遍历车站的 `TileArea`：

```cpp
for (TileIndex t = ta.tile; t < ta.tile + ta.w * ta.h; t++) { ... }
```

`TileArea`（别名 `OrthogonalTileArea`，见 `src\tilearea_type.h`）是矩形：`tile` 为左上角，`w` 为 x 方向宽、`h` 为 y 方向高；地图按行主序存放（`tile = y * Map::SizeX() + x`）。线性递增 `t` 只在 **h == 1** 时才等价于矩形遍历；`h > 1` 时它只沿 `ta.tile` 所在的**那一行**前进 `w*h` 格，矩形里第 1..h-1 行（即其余站台）**根本不会被访问**（非站台的格被 `TileBelongsToRailStation` 过滤掉，不会报错，只是静默漏掉）。

- 站台沿 X（东西向）铺设、多条站台沿 Y 叠放时：矩形第一行只含**一条**站台 ⇒ 枚举结果只剩 1 个平台 ⇒ 站场窗口只画出一行，「一次性建设的站台都算作一个站台」。
- 站台沿 Y 铺设、多条站台沿 X 并列时：矩形第一行恰好含全部站台的北端 ⇒ 侥幸正确。
  ⇒ 该 bug 是**朝向相关**的，所以现场表现可能「有时对、有时错」。

**对照证据**：同一文件已有 15 处正确写法 `for (TileIndex t : ta)`（:125 / :705 / :741 / :1221 / :1943 / :2590 / :6402 等）。`TileIterator` 定义在 `src\tilearea_type.h`（`operator*` :142、`operator++` 为纯虚 :151），`OrthogonalTileIterator` 每行末尾执行 `tile += TileDiffXY(1 - w, 1)`，能正确处理二维矩形。

**平台走查函数本身正确，不是根因**：`TileOffsByDiagDir()` 的表（`src\map.cpp`）为 `NE=(-1,0)`、`SE=(0,1)`、`SW=(1,0)`、`NW=(0,-1)`，所以 `R3RIsPlatformNorthEnd()`（:2049）与 `R3RCollectPlatformTiles()`（:2056）里的 `axis == Axis::X ? NE : NW`（南侧对应 SW / SE）恰好是「沿站台轴走」，语义无误。

**命令层粒度本身也是对的**：`CmdR3RSetStationYard()`（:2305 起）已按单条平台收集与写入（`R3RCollectPlatformTiles` + 逐 tile `R3RSetTileYard`，:2323-2329 用 range-for 遍历）。所以「命令粒度」没问题，唯一坏掉的是**枚举**。

**同类 bug 排查**：全库检索 `xx.w * xx.h` 形式的线性遍历只有 `station_cmd.cpp:2086` 这一处是「遍历」语义，其余（`industry_cmd.cpp:1117`、`waypoint_cmd.cpp:400`、`object_cmd.cpp:749`、`station_cmd.cpp:2746`）都是「面积计算」，合法。

**历史关联**：`station_gui.cpp` :3377 附近有 2026-09-23 的注释，提到当时修过「list looked like it had a single platform（只看到一条站台）」——那轮只调了 GUI 呈现，没修这里的枚举本源，故本次仍复现。

---

## 五、任务 B（KI-304）修法 —— 已定案

一处修改：把 `R3REnumeratePlatforms()` 的线性循环换成 `TileArea` 的 range-for（与同文件其它 15 处一致）：

```cpp
for (TileIndex t : ta) {
    if (!st->TileBelongsToRailStation(t)) continue; // 跳过空洞 / 属于别的车站的 tile
    if (!R3RIsPlatformNorthEnd(t)) continue;        // 每个平台只取北端一次
    out.push_back(t);
}
```

影响面：仅 `R3REnumeratePlatforms` 一个函数；不改数据结构、不改存档、不改命令、不改其它遍历。修复后每个平台各占一行，「划入 / 新建场」即可逐站台生效。

---

## 六、任务 A（KI-305）修法 —— 已定案

### 6.1 行为等价对账（为什么删掉解析器不改变行为）

解析器 `R3RApplyDecoupleOrdersStrategy()` 只在两个调用点被喂 `first_orders` / `second_orders`，而这两个值来自 `Order::GetDecoupleFirstOrdersType()` / `GetDecoupleSecondOrdersType()`。UI 已删 ⇒ 唯一写入口 `MOF_DECOUPLE_ORDERS` 无人调用 ⇒ 字段恒为 0 = `ODOF_KEEP_ORDERS` ⇒ 解析器恒走默认分支返回 `"keep"`（无任何副作用；`ODOF_WAIT_FOR_COUPLE` 的插单、`ODOF_KEEP_ORDERS_NO_LOAD` 的 `VehicleFlag::StopLoading` 都**永不**触发）。因此**删除解析器 ≡ 保留现状行为**。

### 6.2 两案对账与选择

字段 `decouple_first_orders` / `decouple_second_orders` 在 ORDR 里由 `NSL(...)` + `SLE_CONDVAR_X(..., XSLFI_DECOUPLE_ORDERS)` 序列化。

- **甲案（采纳）**：保留字段与序列化（恒写 0，休眠数据），只删「入口 + 解析器 + 语言串」。理由：`XSLFI_DECOUPLE_ORDERS` 是 `_sl_xv_feature_versions[]` 的**数组下标**，删它会让其后所有特性位整体前移 ⇒ 直接破坏存档兼容；删 NSL 条目又会让「带该特性位的老档」出现无法匹配的具名扩展字段。甲案对存档**零改动**，符合「不要误伤别的」。
- 乙案（不采纳）：连字段一起删 + 特性位迁移。收益仅是少 2 字节/订单，代价是存档风险，不做。

### 6.3 删除清单（逐符号）

1. `src\order_type.h`：删 `ModifyOrderFlags::MOF_DECOUPLE_ORDERS`（:381，末尾项，删后仅 `MOF_END` 前移 1；`MOF_END` 全库仅作 `mof >= MOF_END` 上界用，安全）；`OrderDecoupleOrdersFlags` 枚举**保留**并加注「休眠：已无 UI 入口，仅为仍被序列化的字段提供取值文档」。
2. `src\order_base.h`：删 `Order::GetDecoupleFirstOrdersType()` / `GetDecoupleSecondOrdersType()` / `SetDecoupleFirstOrdersType()` / `SetDecoupleSecondOrdersType()`（:927-948）；**保留** `OrderExtraInfo` 的两个 `uint8_t` 字段。
3. `src\order_cmd.cpp`：删白名单分支（`mof != MOF_DECOUPLE_ORDERS`）、参数校验块、执行块。
4. `src\train_cmd.cpp`：删 `R3RApplyDecoupleOrdersStrategy()` 整个函数；删 `first_orders` / `second_orders` 读取块、`decouple_v_tag` / `decouple_u_tag` 两个变量与两处调用、`DECOUPLE-ODOF` 探针行及其注释。
5. `src\lang\english.txt` / `simplified_chinese.txt`：删 7 条**已无引用**的串 —— `STR_ORDER_DECOUPLE_KEEP_ORDERS`、`_KEEP_ORDERS_NO_LOAD`、`_INHERIT_ORDERS`、`_WAIT_FOR_COUPLE`、`_FIRST_ORDERS_SEL`、`_SECOND_ORDERS_SEL`、`_ORDERS_TOOLTIP`。（同段落的 `STR_ORDER_DECOUPLE_BOUNDARY_TOOLTIP` 属**另一个**功能，保留。）
6. **不动**：`src\sl\order_sl.cpp` 的两条 `NSL`、`src\sl\extended_ver_sl.h/.cpp` 的 `XSLFI_DECOUPLE_ORDERS`。

---

## 七、未确认项

- 玩家站场的实际朝向（X 向 / Y 向）未取现场，但修法对两种朝向都成立；修后应能逐站台列出。
- 甲案保留的休眠字段若将来 UI 回归需重新接线；已在备忘与 KI 条目写明。
- 「站台」（platform）在 R3R 里的定义 = 沿站台轴连续、同站同轴兼容的 tile 串（`IsCompatibleTrainStationTile`）。
- 若玩家另指的「一次性建设」入口不是站场窗口而是订单「目的地场」下拉，需再取证（当前证据指向站场窗口的列表）。

---

## 八、复测判据

**任务 B（KI-304）**

1. 一次建设出的多站台车站，打开站场窗口应看到**与平台数相同**的行（每条平台一行，形如 `平台 k:(x, y)`）。
2. 把平台 1 划入 1 场、平台 2 划入 2 场后，`SYRD` 存档往返划分不变。
3. 两种朝向（东西向平台 / 南北向平台）都能逐站台列出。
4. 不出现「所有平台被算成一个」；无平台时不崩。

**任务 A（KI-305）**

1. 全库检索 `MOF_DECOUPLE_ORDERS` / `SetDecoupleFirstOrdersType` / `SetDecoupleSecondOrdersType` / `GetDecoupleFirstOrdersType` / `GetDecoupleSecondOrdersType` / `R3RApplyDecoupleOrdersStrategy` 应为 0 命中（仅备忘/KI 文档除外）。
2. 解挂流程 `DECOUPLE-*` 行与删除前逐字段一致（不再出现 `DECOUPLE-ODOF` 行属预期）。
3. 老档读入不报错（`XSLFI_DECOUPLE_ORDERS` 与两条 NSL 仍在）。
4. `read_lints` 0 条；新 exe 内不再含 `DECOUPLE-ODOF` 字面量，其它解挂探针仍在。

---

## 九、构建自证（2026-10-02 22:17 回填）

- 构建脚本：复用既有 `_tmp_inc_build.cmd`（**未新建任何 .cmd**）。
- 护栏判定：`R3R_inc_guard.ps1` 输出「4 header/lang file(s) are newer than the newest object file」——`src\lang\english.txt` / `src\lang\simplified_chinese.txt` @21:45:49、`src\order_type.h` @21:28:13、`src\order_base.h` @21:27:00，均晚于最新 obj @20:39:37 ⇒ `REMOVED 620 object file(s) - upgrading this build to a FULL rebuild`，故本轮实际走**全量**（702 步）。
- 结果三件套：`[702/702] Linking CXX executable openttd.exe`；`build\R3R_incbuild.done` = **EXIT_CODE=0**（2026-10-02 22:17:29）；日志 `build\R3R_incbuild.log` 中 `error C` / `fatal error` / `FAILED:` / `build stopped` 计数 **0**。
- 时间戳链（源码 → obj → exe）：
  - `src\train_cmd.cpp` 21:45:47 → `train_cmd.cpp.obj` 22:11:59
  - `src\station_cmd.cpp` 21:26:00 → `station_cmd.cpp.obj` 22:10:25
  - `src\order_cmd.cpp` 21:37:04 → `order_cmd.cpp.obj` 22:08:03
  - `build\generated\table\strings.h` 21:48:32 → `strings.cpp.obj` 22:10:46
  - **`build\openttd.exe` @2026-10-02 22:17:08（51 607 552 B），晚于全部 obj 与源码**。
- `read_lints`：train_cmd.cpp / station_cmd.cpp / order_cmd.cpp / order_base.h / order_type.h 共 **0 条**。
- 产物自证：新 exe 内 **搜不到** `DECOUPLE-ODOF`（已删），`DECOUPLE-DONE` **仍在**（解挂主流程未动）；`build\generated\table\strings.h` 仍有 `STR_ORDER_DECOUPLE_BOUNDARY_TOOLTIP`（0xEBD）与 `STR_R3R_YARD_*`，被删的 7 条 `STR_ORDER_DECOUPLE_{KEEP_ORDERS,KEEP_ORDERS_NO_LOAD,INHERIT_ORDERS,WAIT_FOR_COUPLE,FIRST_ORDERS_SEL,SECOND_ORDERS_SEL,ORDERS_TOOLTIP}` **已全部消失**，`build\lang\*.lng` 内亦无对应文本。
- 工具链坑（本轮踩到，如实登记）：首次以后台方式启动构建时，第二个实例在**第一个实例仍在写日志**的情况下重入，立刻写出 `EXIT_CODE=0` 的**伪完成标记**（21:47:42，ninja 实际才 211/702）；同时护栏因「已有新 obj」改判 `incremental is safe`。已删除该伪标记，并以日志进度（`[702/702]`）+ obj/exe 时间戳为准确认真实完成。**教训：同一棵树不得并发跑两个 `_tmp_inc_build.cmd`；`.done` 的 `EXIT_CODE` 必须与日志 `[n/n] Linking`、exe mtime 三者交叉验证。**

## 十、第 201 轮施工结果

### 10.1 改动文件清单（全部本轮完成，工作区未提交）

| 文件 | 改动 | 目的 |
| --- | --- | --- |
| `src\station_cmd.cpp` | `R3REnumeratePlatforms()` 由线性 tile 遍历改为 `for (TileIndex t : ta)` 二维遍历（现 :2089-2093，附注释） | **KI-304 根因修复**：多平台车站不再只枚举一条平台 |
| `src\order_type.h` | 删 `ModifyOrderFlags::MOF_DECOUPLE_ORDERS`；`OrderDecoupleOrdersFlags` 加注「休眠」 | KI-305 |
| `src\order_base.h` | 删 `Get/SetDecoupleFirstOrdersType`、`Get/SetDecoupleSecondOrdersType`；**保留** `OrderExtraInfo` 两字段 | KI-305 |
| `src\order_cmd.cpp` | 删 `MOF_DECOUPLE_ORDERS` 白名单分支、参数校验块、执行块 | KI-305 |
| `src\train_cmd.cpp` | 删 `R3RApplyDecoupleOrdersStrategy()` 整函数、`first_orders`/`second_orders` 读取块、`decouple_v_tag`/`decouple_u_tag`、`DECOUPLE-ODOF` 探针行（三处留说明注释） | KI-305 |
| `src\lang\english.txt`、`src\lang\simplified_chinese.txt` | 各删 7 条无引用串（保留 `STR_ORDER_DECOUPLE_BOUNDARY_TOOLTIP`） | KI-305 |

未改：`src\sl\order_sl.cpp`（`XSLFI_DECOUPLE_ORDERS` 与两条 `NSL` 保留，保存档兼容）、`src\station_gui.cpp`、`SYRD` 相关（KI-304 无需迁移）。

### 10.2 施工过程中发现的问题（如实登记）

上一会话的 KI-305 删除**只做了一半**：`first_orders` / `second_orders` / 两个 tag 变量已删，但 `DECOUPLE-ODOF` 的 `fprintf` 仍在引用它们 ⇒ 工作区一度处于**编不过**的状态。本轮补齐（删除该 `fprintf` 及其注释，留一条说明注释）后恢复可编译并全量构建通过。

### 10.3 收尾状态

- KI-304：**已修（已编译）**，游戏内复测待做（判据见第八节任务 B 的 4 条）。
- KI-305：**已修（已编译）**，游戏内复测待做（判据见第八节任务 A 的 4 条）。
- 两者条目已写入 `R3R_KNOWN_ISSUES.md` 的「二十一、第 201 轮」小节，含本轮的构建自证块与工具链坑登记。
