# 第 466 轮：`R3RDUMP ov_/cw/te` 实测判读（三节铰接单元成段后的 3× 读数）

时间：2026-11-19。输入：玩家读档后的 `build\R3R_debug.log`（26 行，第 465 轮探针版 exe 产生）。
本轮**零源码改动、零构建**，只做判读 + 落盘。

## 一、日志原文（判读依据）

```
ARTIC-BAKE-NORMALISE head=0 n=3 w=185 p=1524 spd=80 (overrides missing in savegame)
ARTIC-BAKE-NORMALISE head=3 n=3 w=185 p=1524 spd=80 (overrides missing in savegame)
R3RDUMP idx=0 ... ARTH=1 ARTM=0 SEGF=0 ov_w=63 ov_p=508 ov_s=80 cw=63  te=84410
R3RDUMP idx=1 ... sub=02(ARTICULATED_PART) ARTM=1 ov_w=61 ov_p=508 ov_s=80 cw=0 te=0
R3RDUMP idx=2 ... sub=02(ARTICULATED_PART) ARTM=1 ov_w=61 ov_p=508 ov_s=80 cw=0 te=0
R3RDUMP idx=3 ... SEGF=1(段) ov_w=63 ov_p=508 ov_s=80 cw=185 te=247871
R3RDUMP idx=4 ... sub=08(ENGINE) ARTM=1 ov_w=61 ov_p=508 ov_s=80 cw=0 te=0
R3RDUMP idx=5 ... sub=08(ENGINE) ARTM=1 ov_w=61 ov_p=508 ov_s=80 cw=0 te=0
```

（idx 行其余字段见日志，`st:`/`tf:` 与第 465 轮一致；两条 `ARTIC-BAKE-NORMALISE` 都带
`(overrides missing in savegame)` ⇒ 读的是旧 exe 写的档，归一在内存里重烘。）

## 二、三分支判读结果

第 465 轮定的三分支：

| 分支 | 判据 | 本轮命中 |
| --- | --- | --- |
| 1 | `ov_w=63/61/61` 且 `cw=185`、`te=1×` ⇒ 内存正确，3× 在显示/别处 | **head=3（段链）命中** |
| 2 | `ov_w=65535`/`185`，或 `cw=555` ⇒ 归一没落盘/被覆盖 | 未命中 |
| 3 | `ov_w=63` 但 `cw=555` ⇒ `CargoChanged` 没跑 | 未命中 |

**段链（head=3, SEGF=1）结论：内存侧正确。**
`ov_w = 63+61+61 = 185`（= 头车记录重量，1×）、`ov_p = 508×3 = 1524`（1×）、`ov_s = 80`；
`cw = 185`（`CargoChanged` 已跑）、`te = 247871`（与 `cw` 成正比，1×）。

⇒ REQ-465-A 的三条候选解释里，②归一写错对象、③`ConsistChanged` 被跳过 **均排除**；
剩下 ①**显示的「3×」走的是不经过 `weight_override` 的路径**。
⇒ 若玩家在新 exe 下仍看到 3×，必须指明是哪个窗口的哪个数字（列车详情窗 / 车队列表 /
车库行 / 购买列表），因为内存三个字段（`cw`/`te`/`cached_power`）都已是 1×。

## 三、新缺陷（本轮最重要发现）：归一误烘焙真铰接组 ⇒ 该组只剩 1/3

- **现象**：`head=0`（`SEGF=0`，散链）的成员 `sub=02` 带 `GVSF_ARTICULATED_PART` 位，
  即它是**真铰接组**（父车 + 2 个 artic 部件，从未被去铰接化），但归一把它一起烘了：
  `ov_w=63/61/61`、`ov_p=508/508/508`。
- **后果**：该组 `cw = 63`（应为 185）、`te = 84410`。`84410 : 247871 = 63 : 185`，
  两个数字严格按 `cached_weight` 比例 ⇒ 重量与最大牵引力都掉到 **1/3**。
- **猜测根因（待验证）**：`CargoChanged()`（`ground_vehicle.cpp:133-142`）对铰接部件
  按 `GetEnginePartsCount()` 分摊父车重量：原态 `185/3 = 61(+2 余数归头) = 63` 合计 185；
  一旦父车 `weight_override=63`，再分摊一次 `63/3 = 21`，合计只剩 63。
  ⇒ **烘焙 + 真铰接位 = 双重分摊**，这是结构性互斥，不是算术 bug。
- **为什么以前没暴露**：合成本意只服务「去铰接化后每节都是独立车」的段组
  （`R3RDearticulateOneGroup()` 会 `ClearArticulatedPart()`）；而 `R3RNormaliseArticGroupBakes()`
  的判据只有「带 `ArticGroupHead` 角色位」，第 463 轮之后老档里**真铰接组也带角色位**，
  于是被一并命中。
- 记账：**KI-662**（中，读数错、影响手感与玩家判断，不丢数据）。编号原写 KI-661，
  与 `R3R_KNOWN_ISSUES.md:25352` 的「0 容量机车：散链 2、成段 1」重号，已改为 KI-662；
  KI-661 此后恒指载客 2/1 那条。

## 四、下一轮的修法（待拍板，本轮未动码）

1. **收窄归一判据**：组内（`v` 到 `nxt` 前）任一辆 `IsArticulatedPart()` ⇒
   `ARTIC-BAKE-SKIP ... reason=real-artic-parts`，**不烘焙**。
   去铰接化后的段组其成员 `sub` 不含 `GVSF_ARTICULATED_PART`，天然不会被跳过（正判据可用）。
2. **自愈存量**：命中「真铰接 + 已有烘焙值」时把三字段清回 `UINT16_MAX` 并
   `head->ConsistChanged(CCF_SAVELOAD)`，否则被旧 exe 存过档的组永久停在 1/3。
   安全性：去铰接化组必无 artic 位 ⇒ 清理不会伤到合法烘焙。
3. 不改 `CargoChanged()` 的分摊逻辑（那一处对「未烘焙的真铰接组」是正确行为）。

## 五、复测判据（修法落地后）

1. 读档日志出现 `ARTIC-BAKE-SKIP head=0 n=3 reason=real-artic-parts`，
   **不再**出现 `ARTIC-BAKE-NORMALISE head=0`。
2. `R3RDUMP idx=0` 的 `ov_w=65535`（或被清理后再烘焙的值）、`cw=185`、`te≈247871`。
3. `idx=3`（段链）仍为 `ARTIC-BAKE-NORMALISE head=3 ... cw=185 te≈247871`（不回归）。
4. 负判据：原生 GRF 铰接车（无角色位、无 ★/⊗）一字不动。

## 六、未确认项

- U-1：玩家看到的「3×」具体窗口与字段名（本轮已判定内存侧 1×，必须由玩家指认界面）。
- U-2：`idx=4` 同时带 `sub=08(ENGINE)` + `ARTH=1` + `ARTM=1`（既是组头又是成员）
  是否属正常 R3R 角色分布，本轮未查；若异常另开条目。
- U-3：`power` 侧玩家报 1×，本轮 `ov_p` 合计 1524（1×）自洽，暂无矛盾。

## 七、C-466-1 回填（2026-11-19 玩家答：列车详情窗口）

玩家原话：「三倍我是在列车详情窗口看到的」。⇒ **U-1 部分关闭**：窗口已锁定为列车详情窗，
字段名与具体文字仍缺（见 §7.3 的三问）。

### 7.1 详情窗两条读取路径（源码取证，只读，零改动）

| 页签 | 绘制入口 | 数字来源 | 是否读 `weight_override` |
| --- | --- | --- | --- |
| 详细信息 `TDW_TAB_INFO` | `train_gui.cpp:400 TrainDetailsInfoTab()`，每节车一行 | 引擎原型 `RailVehInfo` / `Engine`（`STR_VEHICLE_INFO_*`） | **否** |
| 总计 `TDW_TAB_PERF` | `train_gui.cpp:800-819` | 逐节 `u->GetWeightWithoutCargo()` 累加（`:804-808`），再 `GetTrainEstimatedMaxAchievableSpeed()` | **是**（`train.h:496-508`：`weight_override != UINT16_MAX` 时直接返回它） |

⇒ 「总计」页的合计在本轮读数下必然是 `63 + 61 + 61 = 185`（1×），**不可能**显示 555。
⇒ 玩家看到的 3× 只可能落在「详细信息」页：该页每节车各画一行，行内重量/功率/限速全部取
**原型值**（不经过 override、不经过 `cached_weight`），三节车就出现三行一模一样的
`185 t / 1524 hp / 80 km/h`。

### 7.2 猜测根因（待验证）

「详细信息」页对**同一节的每辆物理车**都按原型渲染，而 R3R 的去铰接化/真铰接组在视觉上是
「一节车厢 = 3 辆物理车」⇒ 三行同值被读成「三倍」。这不是内存错（`cw`/`te`/`ov_*` 全 1×），
而是**显示口径**问题：该页从不读 `weight_override`，也不区分「这一组是同一个运营单元」。

若真是此因，则：
- 正判据：三行数字**逐字相同**（都 185 t / 1524 hp / 80 km/h），不是一行 555；
- 修法（未落地，待拍板）：信息页对「真铰接组 / 去铰接化段」只画一行（组头），
  或行内重量改走 `GetWeightWithoutCargo()` + `GetPower()`（与总计页同源）。

### 7.3 需要玩家回填的三问（C-466-1a/b/c）

- **C-466-1a**：哪个页签——「详细信息」还是「总计」？
- **C-466-1b**：那行完整文字（例：`重量 555 t, 功率 4560 hp, 最高速度 240 km/h`），
  或三行各自的数字。
- **C-466-1c**：是三行一样的数字，还是**一行**就写着 3 倍？

判定表（玩家答完直接对号）：

| 玩家读数 | 判定 | 下一步 |
| --- | --- | --- |
| 三行都 185 / 1524 / 80 | 显示口径（§7.2 成立） | 改信息页只画一行或改数据源 |
| 一行 555 t（或 4560 hp / 240 km/h） | 显示路径在累加原型 ⇒ 同一修法，但要查是谁累加 | 同上，另查累加点 |
| 总计页 555 t | 与本轮 `train.h:502` 矛盾（override 没被读到） | 说明窗口读的是另一份缓存，需新探针 |

## 八、C-466-1 收口（2026-11-19 玩家更正，术语与结论同时修正）

### 8.1 玩家原话要点（决定性，改写本轮定性）

1. 「列车哪来的第二个详情页」——不是 `VehicleDetailsWindow` 的两个页签，而是**点击列车弹出的小
   窗口本身**（地图/车库/预览里点车；小窗口带启停、编辑排程、命令回车库等按钮），
   其**上半部分的总览**显示重量；下面一节一节的列表**不显示重量**。
   ⇒ §7.1/§7.2 的「信息页逐节画原型」推断**作废**，我此前把窗口认错了，向玩家致歉。
2. 「三倍是错误的描述」——**现行数据问题是散链 1/3，不是假铰接 3 倍**。
3. 「假铰接的各项数据（除了载客外）都正常」。

### 8.2 术语对照（此后统一用玩家口径，避免再对不上）

| 玩家说法 | 含义 | 本轮日志里的组 | 实测 |
| --- | --- | --- | --- |
| 假铰接 / 拆开的段 | R3R 去铰接化后的段组（成员 `sub=08(ENGINE)`，无 `GVSF_ARTICULATED_PART`） | `head=3`（`SEGF=1`） | `cw=185`、`te=247871` **正常** |
| 散链 | 原生铰接组，从未去铰接化（父车 + `sub=02` 部件） | `head=0`（`SEGF=0`） | `cw=63`、`te=84410` **掉到 1/3** |

⇒ 与 §二 判读一致：内存侧「正常」的是段组，「1/3」的是散链。我此前把两者说反，**1/3 才是真缺陷**。
⇒ §7 的「3× 显示路径」不再是待办；**唯一的活缺陷 = 散链被误烘焙 ⇒ 双重分摊 ⇒ 1/3**。

### 8.3 新疑点（立刻登记，未取数）

- **U-4（猜疑，待验证）**：玩家称假铰接组「除载客外都正常」⇒ 去铰接化的段组在**载客/容量**侧
  仍有异常（可能 `cargo_cap`/`cached_capacity` 或信息页容量行）。本轮未取数，先登记；
  若后续确认，即 `R3R_KNOWN_ISSUES.md:25352` 的 **KI-661**（0 容量机车：散链 2、成段 1）现场确认，
  并入该条、不新开编号。

### 8.4 本轮落地的修法（玩家 2026-11-19 授权「你可以上手改了」）

与 §四 同案，两条一起做（都是 `src/train_cmd.cpp` 内的归一函数，不动 `CargoChanged`）：

1. **收窄归一判据**：组内（`v` 到该组末节）任一辆 `IsArticulatedPart()` ⇒ 跳过烘焙，
   打 `ARTIC-BAKE-SKIP ... reason=real-artic-parts`。
2. **自愈存量**：组内确有真 artic 部件、但成员已被旧 exe 写上烘焙值（任一字段 != `UINT16_MAX`）
   ⇒ 三字段清回 `UINT16_MAX` + `head->ConsistChanged(CCF_SAVELOAD)`，打 `ARTIC-BAKE-HEAL`。
   ⇒ 老档里停在 1/3 的散链**读档即恢复 185 / 1524 / 80**，不需要玩家手动重装车。
3. 不碰 `CargoChanged()`（`ground_vehicle.cpp` 的均分对「未烘焙的原生铰接组」是正确行为）。

### 8.5 复测判据（新 exe）

1. 读档日志出现 `ARTIC-BAKE-SKIP head=0 ... reason=real-artic-parts`（或 `ARTIC-BAKE-HEAL` 一次后同样跳过），
   **不再出现** `ARTIC-BAKE-NORMALISE head=0`。
2. `R3RDUMP idx=0` 的 `ov_w=65535`、`cw=185`、`te≈247871`。
3. `idx=3`（段组）仍 `ARTIC-BAKE-NORMALISE head=3 ... cw=185 te≈247871`（不回归）。
4. 负判据：原生 GRF 铰接车（无角色位、无 ★/⊗）一字不动。

### 8.6 构建自证（第 467 轮，2026-11-19）

- 入口：`R3R_fullbuild.cmd inc`（复用既有脚本，未新建任何 `.cmd`）。
- 护栏：`build\R3R_incbuild.guard.log` = `GUARD: incremental is safe (no header/lang file is newer than the newest object)`。
- 结果：`build\R3R_incbuild.log` 尾部 `[3/3] Linking CXX executable openttd.exe`、
  `build\R3R_incbuild.done` = `EXIT_CODE=0`。
- 时间戳链：`src\train_cmd.cpp` 2026-11-19 02:50 → `build\CMakeFiles\openttd_lib.dir\src\train_cmd.cpp.obj` 02:54
  → `build\openttd.exe` 02:56（53 030 400 B）。目标目录是 **`openttd_lib.dir`**，
  不是 `openttd.dir`（此前记错过路径，`dir` 会报「找不到文件」）。
- `read_lints` 0 条；`findstr` 命中 `ARTIC-BAKE-HEAL head=%d n=%u` /
  `ARTIC-BAKE-SKIP head=%d n=%u reason=real-artic-parts`。
- 发布树 `build-release` **未编**（按约定，仅用户明确要发布版时才编）。

### 8.7 编号与术语收尾

- 本条缺陷编号 = **KI-662**（KI-661 已属 `R3R_KNOWN_ISSUES.md:25352`「0 容量机车：散链 2、成段 1」，
  正是玩家说的「假铰接除载客外都正常」的那一项）。
- 「散链」= 原生铰接组（未去铰接化，成员带 `GVSF_ARTICULATED_PART`），本轮掉到 1/3 的就是它；
  「假铰接 / 段组」= 去铰接化后的段，各项正常。
- 我此前两处错判已作废：①把窗口认成 `VehicleDetails` 的两个页签（实际是点车弹出的小窗口上半总览）；
  ②把「3 倍」当待查缺陷（玩家已更正为「1/3」）。

### 8.8 实机复测结果（2026-11-19，玩家原话「我实测，目前数据一切正常」）

⇒ **KI-662 游戏内复测通过**，散链重量/牵引力恢复到 1×，本轮修法（SKIP + HEAL + 不动 `CargoChanged`）收口，状态改「已修·实机复测通过」。
⇒ 玩家同日指示下一步：「可以开始收拾机车载客」= 转入 **KI-661**（0 容量机车：散链 2、成段 1）。
   该条与 KI-662 同源但方向相反——散链载客显示 2、成段显示 1，而机车本该 0；
   本轮已确认「假铰接（段组）除载客外各项数据均正常」⇒ 缺陷面收窄到**载客/容量**这一列。
⇒ 交接点：KI-661 现场锚点与我此前的现场观察记在 `R3R_KNOWN_ISSUES.md:25352` 条目内，下一轮先取数再改码。

## 九、KI-661「机车载客」开工（第 468 轮，2026-11-19）

玩家原话：「我实测，目前数据一切正常，可以开始收拾机车载客」⇒ KI-662 收口（§8.8），转 KI-661。

### 9.1 现场定义（沿用 §一 的同一列车）

同一组三节铰接车：**散链显示载客 2，设为段后变 1**；不随存档往返变化
⇒ 与 override / 烘焙层无关（KI-661 原判据，第 464 轮已确认），只看角色位与逐车 `cargo_cap`。

### 9.2 已查代码（只读，未改动）

| 位置 | 行为 | 对本缺陷的意义 |
| --- | --- | --- |
| `train_gui.cpp:597-641` `GetCargoSummaryOfArticulatedVehicle()` | 默认 `do { add_unit(v); } while (v = v->Next()) && v->IsArticGroupMember()`（`:638-640`），每车累加 `u->cargo_cap`（`:627`） | 散链会把父车 + 2 个 `art` 部件各算一份 ⇒ 天然 ≥2 份 |
| 同上 `:607` / `:632-636` | 仅 `R3RDetailsWholeConsistCapacity(v)`（`:387-390`）为真时整链汇总 | 只覆盖「混合链头（wagon 原型升引擎）」，**不覆盖散链/段链** |
| `train_gui.cpp:669-675` `TDW_TAB_TOTALS` | 逐车 `max_cargo[v->cargo_type] += v->cargo_cap` | 总计页同样逐车累加，散链同样多算 |
| `train_gui.cpp:546-590` `R3RDbgCargoSummary()` | 打 `R3R-CAPSUM` / `R3R-CAPUNIT ... cap= role=AH AM art` | **现成判读工具**，但只在 `:634` 的 whole_consist 分支调用 ⇒ 散链打开容量页不会打 |

### 9.3 两条猜测（待验证，先落盘）

- **G-468-1 显示重复**：散链把「父车 + 2 部件」各算一份 ⇒ 2；成段后部件被 `SetWagon()`、
  容量按组头只算一份 ⇒ 1。数据真值一直是 1 份，缺陷只在显示侧。
- **G-468-2 数据真错**：`ConsistChanged()` 容量分支（`train_cmd.cpp:614-623` 一带
  `Engine::DetermineCapacity`）在组角色迁移后把部件容量清零/翻倍，与显示无关。

### 9.4 取数方案（下一步）

1. 把 `R3RDbgCargoSummary(v, summary)` 的调用从「仅 whole_consist」放宽为「链上含
   `ArticGroupHead`/`ArticGroupMember` 即打一次」（只读探针，不改任何数值）；
2. 玩家对同一列车打开「详细信息 → 容量页」（散链态、成段态各一次）；
3. 判读：`R3R-CAPUNIT` 各节 `cap=` 全为 1 ⇒ 显示累加多算（G-468-1，修显示侧汇总去重）；
   出现 `cap=0`/`cap=2` 的异常节 ⇒ 数据真错（G-468-2，改 `ConsistChanged` 的容量重算）。

### 9.5 一句话结论（本轮）

KI-661 的显示侧与数据侧已分开定位到两个具体函数，但**尚无实测读数可分岔**；
下一步是「一条只读探针 + 两张窗口读数」，在拿到之前不改任何容量代码。

### 9.6 探针已落地并编译通过（第 468 轮）

改动**只有一个文件、且只增不减语义**：`src\train_gui.cpp`。

1. `GetCargoSummaryOfArticulatedVehicle()` 非 whole_consist 分支：
   `const Train *const dbg_head = v;` 先存链头，扫描链上是否有 `IsArticulatedPart()`
   **或** `IsArticGroupHead()`（≤128 节，外层 `if (R3RDbgOn())` 包住，探针关闭时零开销），
   命中则在原 `do { add_unit(v); } while (... IsArticGroupMember())` 结束后调一次
   `R3RDbgCargoSummary(dbg_head, summary)`。去重仍走原 `(head, 容量指纹)` 24 槽表
   ⇒ 重绘不刷屏，但**每一行**（每个 `head=`）各留一次，正好对应界面上一行。
2. `R3RDbgCargoSummary()` 增加 `linecap=`（该行 `summary` 各 item 容量之和 = 界面真正印的数字），
   与原 `sumcap=`（逐车 `u->cargo_cap` 原值合计）并列输出，便于一眼看出
   「显示层有没有少加/多加」。

构建自证（复用 `R3R_fullbuild.cmd inc detach`，未新建 .cmd）：

- 护栏 `build\R3R_incbuild.guard.log` = `GUARD: incremental is safe (...)`；
- `build\R3R_incbuild.done` = `EXIT_CODE=0`（2026-11-19 03:12）；
- 日志 `[2/3] Building CXX object ...\src\train_gui.cpp.obj` → `[3/3] Linking CXX executable openttd.exe`；
- 时间戳链：`src\train_gui.cpp` 03:09 → `train_gui.cpp.obj` 03:11 → `build\openttd.exe`
  2026-11-19 03:12（53 003 040 B）；
- `read_lints` 0 条；`findstr "linecap=%u" build\openttd.exe` 命中。

### 9.7 待玩家做的一次读数（拿到即定案）

同一列车（就是散链 2 / 成段 1 的那列）：

1. **散链态**：点开列车 → 详细信息 → 容量页 停 1~2 秒 → 再点「总计」页；
2. **设为段后**：同样两页各停 1~2 秒；
3. 把 `build\R3R_debug.log` 的尾部（含 `R3R-CAPSUM` / `R3R-CAPUNIT`）发回。

判读表：

| 读数形态 | 判定 | 下一步修法 |
| --- | --- | --- |
| 散链各节 `cap=` 合计 == 界面 2，且 `art=1` 的节确实各带 1 | 数据里就是 2 份 ⇒ **G-468-1**（显示/数据在散链侧多算） | 显示侧按「每组只算组头」去重；若真值应为 0 还要查 `DetermineCapacity` |
| 散链 `cap=` 合计 == 1 而界面 2 | `sumcap != 界面` ⇒ 显示层重复累加（同一步件被两个 `head=` 行重复计入） | 修 `do..while` 的跨行重复（`IsArticGroupMember` 在段态会吃掉后续成员） |
| 成段后出现 `cap=0` 的节（原本非 0） | 数据真错 ⇒ **G-468-2** | 查 `ConsistChanged` / `DetermineCapacity` 在 `SetWagon()` 后的重算 |

**注**：段态每节都是一行，而 `do..while (IsArticGroupMember())` 会让第 k 行把第 k..n 节都算进去
（滚动后缀和）。这本身就是一个已识别的显示重复嫌疑（第 468 轮新增观察，见 §9.8），
但**在拿到读数前不改**——原生 artic 下这个循环是必需的（部件不单独成行）。

### 9.8 新增疑点 REG-468-A（滚动后缀和）

- **期望**：段态三节各一行，各印自己的 `cargo_cap`。
- **实测/代码**：`GetCargoSummaryOfArticulatedVehicle(v)` 的 `do..while (v->IsArticGroupMember())`
  对「R3R 段组成员」也为真（`vehicle_base.h:1275-1278` 的 Train 重载），而调用方
  `DrawTrainDetails` 用 `v->GetNextVehicle()`（只跳真 `IsArticulatedPart()`）**会把每个成员单独列一行**
  ⇒ 第 1 行 = 全部三节之和、第 2 行 = 后两节之和、第 3 行 = 本节。
- **为什么存疑**：与「散链 2 / 成段 1」的读数方向相反（成段若为 2/1/0 则第一行仍是 2）；
  也可能玩家读到的是某一中间行。
- **待验证判据**：段态 `R3R-CAPSUM` 应出现 3 条 `head=` 不同的行，且 `n=` 递减（3→2→1）；
  若 `n=` 全为 1 ⇒ 该循环并未跨成员（此疑点证伪）。

### 9.9 读数到手：G-468-2 成立、G-468-1 证伪、REG-468-A 证伪（第 468 轮定案）

玩家 2026-11-19 回 `build\R3R_debug.log`（43 行，产生者 = 第 468 轮探针版
`build\openttd.exe` 53 003 040 B）。原文锚点（行号为该日志）：

```
:2   ARTIC-BAKE-SKIP head=0 n=3 reason=real-artic-parts
:10  ARTIC-BAKE-NORMALISE head=3 n=3 w=185 p=1524 spd=80 (overrides missing in savegame)
:27  R3R-CAPSUM head=0 n=3 sumcap=2 linecap=2 items=1
:28  R3R-CAPUNIT head=0 veh=0 eng=483 cap=0 carg=0 sub=0 carry=1 role=000
:29  R3R-CAPUNIT head=0 veh=1 eng=483 cap=1 carg=0 sub=0 carry=1 role=001
:30  R3R-CAPUNIT head=0 veh=2 eng=483 cap=1 carg=0 sub=0 carry=1 role=001
:35  R3R-CAPSUM head=3 n=3 sumcap=1 linecap=1 items=1
:36  R3R-CAPUNIT head=3 veh=3 eng=483 cap=0 carg=0 sub=0 carry=1 role=100
:37  R3R-CAPUNIT head=3 veh=4 eng=483 cap=0 carg=0 sub=0 carry=1 role=010
:38  R3R-CAPUNIT head=3 veh=5 eng=483 cap=1 carg=0 sub=0 carry=1 role=010
```

图形态对照（同日志 dump）：散链 `:12-26` 三节 `sub=09 ARTH=1` + `sub=02 ARTM=1`×2；
成段 `:31-34` `sub=09 SEGF=1 ARTH=1` + `sub=08 ENG=1 ARTM=1`×2。

**逐条判决**

1. **G-468-1（显示层重复累加）证伪**：两条链都满足 `sumcap == linecap`
   （散链 2==2、成段 1==1），即界面印出的数字与逐车 `cargo_cap` 原值合计完全一致，
   显示层没有多算也没有少算。玩家看到的「散链 2 / 成段 1」是**数据层差异**。
2. **G-468-2（数据真错）成立**：同一步件（`eng=483`，即同一原型）在两条链里 `cargo_cap` 不同：
   散链 `veh1/veh2` 各 `cap=1`（合计 2），成段后 `veh4 cap=0`、`veh5 cap=1`（合计 1）。
   即「去铰接化 + 角色重写」这条路径把其中一节的容量写成了 0。
3. **REG-468-A（滚动后缀和）证伪**：每条链各只出现 1 行 `R3R-CAPSUM`，`n=3` 各一次，
   没有出现 `n=` 递减的 3 行 ⇒ 该 `do..while` 在段态**并未**逐成员重复计行（外层按
   `GetNextVehicle()` 逐行、内层循环在这两个现场都只跑了一遍）。

**方向性结论（供下一轮改码）**

- 差异只在「段侧少一份」这一侧，散链 2 与成段 1 必有一侧是错的；现状读数**不足以**判定真值，
  因为散链把容量摊在**部件**上（父车 0 / 两部件各 1），成段后成员被 `SetWagon()`，
  `ConsistChanged()` 的容量重算（`src\train_cmd.cpp:614-623` → `Engine::DetermineCapacity`）
  只沿「组头/引擎」认账 ⇒ 只留下末节那 1。
- 若真值为 2（散链对）⇒ 修 `ConsistChanged()`/`DetermineCapacity` 让段组各节都被计入；
  若真值为 1（成段对）⇒ 修散链的显示/累加侧，避免部件容量被重复计。
  **在真值口径拍板前不动任何容量数值**（与既定「先取数再改码」一致）。
- 下一步取数：`src\train_cmd.cpp:614-623` 一带读 `Engine::DetermineCapacity` 对
  `IsArticGroupMember()`/`IsEngine()` 的判据；同时比 `R3RDearticulateOneGroup()`
  （`src\vehicle_cmd.cpp:554-566`）里 `SetWagon()` 前后 `cargo_cap` 的写入点，
  确认「cap 由 1 变 0」是重算覆盖还是从未写入。

### 9.10 猜测根因 G-468-3（待验证）：容量回调在去铰接化后不再认「成员」

已读代码（只读，未改）：

- 容量真源=`Engine::DetermineCapacity(v)`（`src\engine.cpp:237`），Train 分支最终落在
  `GetEngineProperty(PROP_TRAIN_CARGO_CAPACITY, ..., v)`（`:285`），即**NewGRF 回调带 `v`**；
  回调能看到车辆身份，三节同原型（`eng=483`）得出 0/0/1 只能是「按 `v` 的 artic 身份/位置算的」。
- `src\train_cmd.cpp:614-646` 是第 458 轮（KI-656）的裁决层：
  `if (u->IsArticGroupMember() && !u->IsArticGroupHead())` 时，若组头 `cargo_cap > 0`
  就**强制成员 0**（`:636-642`），否则走 KI-641 救援「回调给 0 而旧值 >0 就保留旧值」（`:643-645`）。
- 本现场组头 `veh3 cap=0` ⇒ **清零分支没走**；日志也没有 `CAP-MEMBER-ZERO` ⇒ 不是它写的 0。
  剩两条路：(i) 回调在成段态对 veh3/veh4 返回 0；(ii) veh4 的 `cargo_cap` 在更早的一次
  `ConsistChanged()`（`SetWagon()` 之后、救援条件成立之前）就已经是 0，救援因「旧值 >0」不成立而跳过。

**猜测（待验证）**：散链期 GRF 把容量挂在**部件**上（父车 0 + 两部件各 1 = 2，即真值 2）；
去铰接化后 `ClearArticulatedPart()` + `SetWagon()/SetEngine()`（`src\vehicle_cmd.cpp:556-561`）
抹掉 artic 身份，回调对前两节返回 0，只有末节返回 1 ⇒ 总量 1。
KI-656 的假设「组头已含成员贡献」在本 GRF 不成立（组头恒 0），KI-641 的救援又要求旧值 >0 ⇒ 缺的 1 无人补。

**待验证判据**（需要一组只读探针，尚未落地）：在 `ConsistChanged()` 的
`uint16_t new_cap = e_u->DetermineCapacity(u)` 前后，对 `IsArticGroupMember()` 的节各打一行
`CAP-CB veh= cb=%u old=%u ghead=%d ghcap=%u`。
- 若 `cb=0 && old=0` ⇒ 回调返回 0 且无旧值可救 ⇒ 走「容量守恒」修法；
- 若 `cb=1 && old=0` 但最终写 0 ⇒ KI-656 分支被误触发，改判据即可。

**修法方向（未拍板，等真值口径）**：容量守恒——`R3RDearticulateOneGroup()`
（`src\vehicle_cmd.cpp:528-566`）在还是 artic 时快照 `total_cap = Σ cargo_cap`（本现场 =2），
提交点把它当作整组目标总量；`ConsistChanged()` 里对该组只做「总量对齐」而不是逐节裁决。
难点：不改 `src\*.h` 就没有可存档字段承载 `total_cap`，读档后会退回回调值（本现场=1），
需先用探针确认读档后是否真的退回，再决定用内存态 map（NOSAVE）还是别的承载方式。

### 9.11 玩家口径拍板（2026-11-19，决定性，推翻 §9.10 的守恒方向）

两问两答（原话保留）：

- 问「这列 3 节铰接车的载客真值应该是多少？（散链读数 2，成段后读数 1）」
  → 答：**「应当不载客，容量为 0」**
- 问「下一步怎么走？」
  → 答：**「先加探针」**

**口径变更（重要）**：

1. 真值 = **0**。散链的 `2` 与成段的 `1` **都不是目标值**，两者都是缺陷形态；
   §9.10 里「容量守恒 = 2」的方向**作废**，不可再作为修法依据。
2. 因此 KI-656（第 458 轮）「组头承担全部容量、成员可归零」的设计前提在本条也不成立——
   正确形态是**整组 0**。清零方向对，但当前实现连组头都没归零（`veh3 cap=0` 只是回调恰好返 0），
   且成员 `veh5` 仍留 1，说明裁决层只对「组头 cap>0」的形态生效。
3. 散链 `2` 的来源是 GRF 把容量挂在**部件**上（父车 0 + 两部件各 1），原生 JGRPP 会照单全收；
   玩家口径意味着 R3R 侧需要有「本类车不载客」的判据并压成 0，而不是跟随 GRF 的部件容量。
4. 下一步（玩家已选）= **先加只读探针**，把「回调返回值 vs 属性值 vs 旧值 vs 角色位」一次取全，
   再定归零的落点。**在探针读数到手前仍不改任何容量数值。**

**待探针回答的问题（CAP-CB 设计依据）**：

- Q1：`Engine::DetermineCapacity(u)` 对 `veh3/veh4` 到底是返回 0（回调不认）还是返回 1 但被下游改写？
- Q2：`RailVehInfo(engine_type)->capacity`（GRF 属性原值）是 0 还是 1？——决定「该不该载客」的客观依据。
- Q3：散链期 `veh1/veh2` 的 `cap=1` 是否来自同一个属性/回调（即 GRF 属性本身就是 1）。
- Q4：`veh5` 的 1 是回调给的还是 KI-641 救援保留的旧值（看 `old=`）。

### 9.12 探针 CAP-CB 已落地并编译通过（第 468 轮续，只读）

**落点**：`src\train_cmd.cpp`，`ConsistChanged()` 里 `uint16_t new_cap = e_u->DetermineCapacity(u);`
之后紧邻插入（KI-641 那段裁决注释之前），单一文件、只增日志、零数值变更。

**输出格式**（一行一车）：

```
CAP-CB head=%d veh=%d cb=%u old=%u rcap=%u prop=%u ct=%u eng=%d rvt=%d art=%d ah=%d am=%d
```

字段含义：

| 字段 | 来源 | 判读用途 |
| --- | --- | --- |
| `cb` | `DetermineCapacity(u)` 返回值 | 回调/属性算出来的原值，**没经过 KI-641/KI-656 裁决** |
| `old` | `u->cargo_cap` | 上一轮已存下的值；`cb` 与 `old` 相同 ⇒ 无改写，不同 ⇒ 裁决层动过手 |
| `rcap` | `u->refit_cap` | 判断是否被 refit 影响 |
| `prop` | `RailVehInfo(u->engine_type)->capacity` | GRF 属性原值，**该不该载客的客观依据** |
| `ct` | `u->cargo_type` | 货种（多货种 GRF 时区分） |
| `eng` / `rvt` | `IsEngine()` / `railveh_type` | 判断是引擎还是车厢 |
| `art` / `ah` / `am` | `IsArticulatedPart()` / `IsArticGroupHead()` / `IsArticGroupMember()` | 判断走哪条裁决分支 |

**守卫与开销**：外层 `if (R3RDbgOn() && (u->IsArticGroupMember() || u->IsArticulatedPart()))`，
静态计数器上限 400 行（重绘/多列车不刷屏），`R3R_PROBES=0` 的发布树整块消失。

**构建自证**：`R3R_fullbuild.cmd inc detach`（复用既有入口，未新建 .cmd）；护栏判增量安全；
`build\R3R_incbuild.done` = `EXIT_CODE=0`；`train_cmd.cpp.obj` 03:53 → `build\openttd.exe`
2026-11-19 03:55（53 030 400 B）；read_lints 0；exe 命中 `CAP-CB head=%d veh=%d cb=%u ...
rcap=%u prop=%u ...`（字面量逐段可检索）。

**待玩家做的一次复跑**（同一列车，散链态 → 设为段）：

1. 散链态：点开该列车详细信息容量页停 1~2 秒；
2. 设为段后：同样再停 1~2 秒；
3. 若可能，再加一个动作：**读档一次再打开容量页**（用于回答「读档后是否退回」这个问题）；
4. 把 `build\R3R_debug.log` 的 `CAP-CB` 段发回。

**判读表**：

| CAP-CB 形态 | 结论 | 归零落点 |
| --- | --- | --- |
| `prop=1`（散链部件与段组一致） | GRF 属性本身就是 1 ⇒ 原生 JGRPP 也会载客；玩家要 0 ⇒ R3R 必须主动压 0 | 需要「本类车不载客」判据（不能靠回调） |
| `prop=0` 但 `cb=1` | 属性 0、回调按位置给 1 ⇒ 归零只需让回调不生效 | 在 `DetermineCapacity` 之后对 `IsArticGroupMember()` 直接置 0 |
| `cb=1 && old=0` 而界面 0 | KI-656 清零分支已生效，只是散链侧没进同一分支 | 把散链成员也纳入同一清零判据 |
| `cb=0 && old=1`（`veh5` 形态） | KI-641 救援把旧值留住了 ⇒ 救援条件需加「组目标容量 = 0 时不许救」 | 改 `:643-645` 的救援护栏 |

### 9.13 复跑读数为 0 条 CAP-CB：探针被 tag 白名单吞掉（第 5 次同族事故）

**现场**：`build\R3R_debug.log` 7 328 B / 2026-11-19 04:04，产生者 `build\openttd.exe`
2026-11-19 03:55（53 037 728 B）——该 exe 内 `findstr /C:"CAP-CB head="` 命中，
即探针**确实编进去了**。但 `findstr /C:"CAP-CB"` 对该日志命中 **0 条**；
日志里 `CAP` 字样只有既有的 `R3R-CAPSUM` / `R3R-CAPUNIT`（显示层，走 `R3RFopenDbg`）。

**根因（已读码实证）**：`R3RDbgWrite()`（`src\r3r_perf.h:408`）第二道门是
`if (!R3RDbgTagAllowed(fmt)) return;`（`:411`），而 `R3RDbgTagAllowed()`（`:322-405`）
是**首 token 前缀白名单**，只放行：

```
RESV / UNRES / EXTEND / LOOP / R3R-RES / COUPLE / CPL / FOLD / SPLICE / ARTIC / SKIP-
UNIT / CLONE / ORD / CTRL / AUTO- / OWNER / NAME / RESERVE / DECOUPLE / SPD / BUY
WP / DUALHEAD / TTB / DEPOT / SEGID / SEGROW / LEGACY / CGRP / GRP / R3R-
```

`CAP-CB` 不在其中（末尾只有通用前缀 `R3R-` 能兜住，见 `:400`）⇒ 整块被**静默丢弃**，
不是"分支没走到"。

**连带纠正（重要，推翻 §9.10 的一条依据）**：第 458 轮（KI-656）的
`CAP-MEMBER-ZERO`（`src\train_cmd.cpp:661`）**同样不在白名单**，它历史上也一次都写不进去。
因此 §9.10 里「日志没有 `CAP-MEMBER-ZERO` ⇒ 清零分支没走」这条推断**作废**——
该分支是否触发过，截至本轮**没有任何证据**，必须靠改名后的探针重新判。

**同族事故回顾（这是第 5 次）**：`src\r3r_perf.h:384-390` 自己写着
"This is the third time the trap fires (after 第 274 轮 and 第 296 轮 above)"，
本轮再加两个案例（第 289 轮的 `EXT-PRE`、本轮的 `CAP-CB`）。
新增任何 `R3RDbgWrite` 之前，**必须把首 token 对着 `:335-400` 逐条比对**，
否则「日志 0 条」永远是假阴性。

**修法（零逻辑变更）**：把两条 tag 改成命中通用 `R3R-` 前缀——

- `CAP-CB` → `R3R-CAPCB`
- `CAP-MEMBER-ZERO` → `R3R-CAPMEMBERZERO`

这样一次复跑就能同时看到「回调值/旧值/属性值」与「清零分支是否动过手」。

**构建自证（本轮改名后）**：`R3R_fullbuild.cmd inc detach`（复用既有入口，未新建 .cmd）；
`build\R3R_incbuild.done` = `EXIT_CODE=0`；`build\openttd.exe` **2026-11-19 04:10:31**；
read_lints 0；exe 命中 `R3R-CAPCB head=%d veh=%d cb=%u old=%u rcap=%u prop=%u ct=%u eng=%d rvt=%d art=%d ah=%d am=%d`
与 `R3R-CAPMEMBERZERO veh=%d head=%d cap=%u->0 cb=%u`。

### 9.14 复跑读数：`cb` 全 0、`prop` 全 1，非零值全部来自 KI-641 救援

现场：`build\R3R_debug.log` 5 259 B @2026-11-19 04:13，产生者 exe 04:10:31（§9.13 改名版）。
6 行 `R3R-CAPCB`（:1/:2/:11/:12/:15/:16），两次 ConsistChanged 读数逐字一致：

| 链 | 车 | cb | old | prop | art | ah | am | eng | rvt |
| --- | --- | --- | --- | --- | --- | --- | --- | --- | --- |
| 0（真铰接组） | 1 | 0 | 1 | 1 | 1 | 0 | 1 | 0 | 0 |
| 0（真铰接组） | 2 | 0 | 1 | 1 | 1 | 0 | 1 | 0 | 0 |
| 3（已烘焙） | 4 | 0 | 0 | 1 | 0 | 1 | 1 | 1 | 0 |
| 3（已烘焙） | 5 | 0 | 1 | 1 | 0 | 0 | 1 | 1 | 0 |

显示层（同日志）：`R3R-CAPSUM head=0 sumcap=2` / `head=3 sumcap=1`；
`R3R-CAPUNIT` 逐节 cap=0/1/1 与 0/0/1 —— 与探测读数一致。

**四条判定（全部实证）**：

1. `prop=1`（四节原型都声明 capacity=1）⇒ GRF 本身给了容量，**不能**指望属性归零。
2. `cb=0`（四节都是 0）⇒ `DetermineCapacity()` 在当前状态下恒返 0，原生 JGRPP 读数就是 0。
3. 非零值**唯一**来源 = KI-641 救援分支 `new_cap = u->cargo_cap`（`train_cmd.cpp:677-679`）把旧值粘住；
   `veh1/veh2/veh5` 的 1 全是它粘下来的（`cb=0 && old=1` 正是判读表的最后一格）。
4. KI-656 的清零分支条件是「组头 `cargo_cap>0`」，而两个组头 `veh0`/`veh4` 的 cap 都是 0
   （它们是 head，被 `!IsArticGroupHead()` 跳过，`new_cap=cb=0`）⇒ **该分支一次都没生效**。
   这同时解释了「日志从来没有 `R3R-CAPMEMBERZERO`」：既是条件不成立，也是旧 tag 被白名单吞。

**结论**：目标 0 的落地 = 撤掉 KI-641 在 capacity 上的「救旧值」。
撤回后本现场自然得 0（`veh0/3/4` 本来就是 cb=0，`veh1/2/5` 不再被粘），
与玩家口径「应当不载客，容量为 0」一致，且与原生 JGRPP 行为对齐（回调说 0 就是 0）。
风险面：只有「回调明确返 0 却硬保留旧值」的场合会变，那正是本条要消除的形态。

**落地（`src\train_cmd.cpp`，单文件）**：删掉 `else if (new_cap == 0 && u->cargo_cap > 0) new_cap = u->cargo_cap;`，
改打 `R3R-CAPDROP veh=%d head=%d cap=%u->0 cb=%u`（上限 200 行）；KI-656 清零分支与注释保留，
注释里「组头回 0 时救援仍生效」一句已改写为「成员也保持 0」。

**构建自证**：`R3R_fullbuild.cmd inc detach`（复用既有入口）；`build\R3R_incbuild.done` = `EXIT_CODE=0`；
`train_cmd.cpp.obj` 04:17 → `build\openttd.exe` **2026-11-19 04:19**（53 030 400 B）；read_lints 0；
exe 命中 `R3R-CAPDROP veh=%d head=%d cap=%u->0 cb=%u`。

**复测判据（请玩家用 04:19 版）**：
1. 同一列车的容量页，散链与成段两种形态**都应读 0**（不再出现 2 / 1）；
2. `R3R-CAPSUM` 应变成 `sumcap=0`，`R3R-CAPUNIT` 逐节 cap 全 0；
3. 日志应出现 `R3R-CAPDROP`（记录被丢弃的旧值 1），且每节最多一次/每次重算；
4. 负判据：普通客车/货车（未去铰接）容量**逐字不变**，能照常装货；
5. 负判据：若某 GRF 的容量回调本身返非 0，则该车照旧有容量（走 `new_cap` 正常写入，不经过本条）。

### 9.15 玩家复测：KI-661 收口（成段/散链读数正常 + 机车载客消失 + 附带修掉 NewGRF 容量报错）

**玩家原话（2026-11-19）**：「读档前后，成段或散链数据都保持正常，同时，也没有"机车载客"的问题出现。
此外，我意外发现，那个改装报错也消失了。」

**判定**：§9.14 的判据 1/2/3/4 一次性全部通过，KI-661 状态改「已修（2026-11-19 §9.14 落地，§9.15 实机复测通过）」。
撤掉 KI-641 容量救援后，`cargo_cap` 恒等于 `DetermineCapacity()` 的返回值（本现场 = 0），
不再有「回调说 0、账面留 1」的分歧，故成段/散链、读档前后一致。

**附带收益的机制（"改装报错消失"不是巧合）**：`ConsistChanged()` 里
`allowed_changes` **不含** `ConsistChangeFlag::Capacity` 的提交点走的是校验分支
（`src\train_cmd.cpp:704-706`）：

```cpp
/* Verify capacity hasn't changed. */
if (new_cap != u->cargo_cap)
    ShowNewGrfVehicleError(u->engine_type, STR_NEWGRF_BROKEN, STR_NEWGRF_BROKEN_CAPACITY, GRFBug::VehCapacity, true);
```

旧行为下救援把 `cargo_cap` 粘成 1，而同一辆车在"不许改容量"的提交点里 `new_cap` 又是 `cb=0`
⇒ `0 != 1` ⇒ 每读档/每次重算都会弹 `STR_NEWGRF_BROKEN_CAPACITY`（玩家口中的"改装报错"）。
撤掉救援后两边都取 `cb`，等式自洽，报错自然消失。
⇒ 反证成立：那条报错**本来就是救援分支造成的自相矛盾**，而不是 GRF 真的写错了容量。
（也说明 §9.13 的复盘顺序正确：先用探针确证 `cb=0`，才敢认定"账面 1"是人为的。）

**未做/边界**：`prop=1`（GRF 原型属性声明容量 1）仍在，本次只是让运行时容量遵从回调；
若玩家后续希望连属性层也为 0（例如让车库/购买列表就不显示容量），需要另开一条判定，不在 KI-661 范围。
