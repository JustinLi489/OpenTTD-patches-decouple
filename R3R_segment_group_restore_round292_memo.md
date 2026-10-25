# 第 292 轮（2026-10-24）：段与「普通列车分组（载具组）」的还原

## 零、本轮输入（玩家原话，先落盘后取证）

1. **第 291 轮修法 (e) 复测通过**：玩家原话「好了，现在涂装正常了，你可以见R3R_debug.log」。
   - 日志：`build\R3R_debug.log`（500 725 B / 8 905 行，mtime 2026-10-24 20:19:02）。
   - 产生它的 exe：`build\openttd.exe`（52 307 456 B，mtime 2026-10-24 20:15:46）= 第 291 轮修法 (e) + (a) 补 `group_id_backup` 的那一版。
   - ⇒ 采样有效，KI-462 可改「复测通过」。
2. **新问题报告（标题级，细节待补）**：玩家原话「新的问题是，段关于普通列车分组（载具组）的还原」。
   - 关键词拆解：
     - 「段」= R3R 的 segment（★ 段头 + 段区间）；
     - 「普通列车分组（载具组）」= 上游 JGRPP 的 **Vehicle Group**（`Group`，车库/车队列表里那个分组，`Vehicle::group_id`），不是 R3R 的「挂接分组 CoupleGroup」；
     - 「还原」= restore / 归属回退，即段在某个提交点（重联、解挂、车库编辑、读档）之后，某一节或整段的车组归属没有回到它自己那套，或者回到了不该回的那套。
   - ⚠ 玩家尚未给出具体现象（哪个方向错、什么时候错、界面还是统计错），本轮先按「待验证」登记，不预设结论。

## 一、待验证项（发现即登记，后续逐条更新成立/证伪）

- **U-1 现象方向未定**：是「该段的组丢了（变成无组/公司涂装）」还是「该段的组错成别的段/链头的组」？两者修法相反，必须先定。
- **U-2 涉及哪个提交点**：重联（`Couple`）/ 解挂（`DecoupleTrain`）/ 车库拖动（`R3RSyncChainAfterDepotEdit`）/ 读档（`R3RRebuildCouplePriorities`）—— 未知。
- **U-3 与第 291 轮 (a) 的口径是否冲突**：本轮 (a) 让**涂装**按段读（`R3RLiveryGroupOf` 先读 `group_id_backup` 再读段行），而 `R3RNormaliseChainGroups()`（第 159 轮「分组属于整列车」）让**存储的 group_id** 仍是整链归一化 —— 「显示按段、存储按整链」这一分裂是当前设计的既有事实，新问题很可能就落在「存储侧要不要改成按段」。
- **U-4 是否与第 158 轮 KI-254（车号/名称跟链头）同族**：KI-254 修的是 unitnumber/name 的借用与回填；组（group_id）走的是另一条链（`R3RNormaliseChainGroups` + `group_id_backup` 停放），未一起收口。

## 二、取证计划（按铁律：每组工具调用后立刻把结论写回本节）

1. 在 `build\R3R_debug.log` 里检索与分组相关的既有探针（`GRP-NORM` / `SEGTRAIT` / `INVAR-CTRL` / `DEPOT-XFER-ID` / `NAME-XFER` 等），看本轮会话有没有分组归一化的痕迹。
2. 读源码里分组相关的全部读写点，画出「谁在什么时候改 group_id」的清单。
3. 与玩家确认具体现象（界面表现 + 复现步骤），再定修法。

### 2.1 取证记录

**采样有效性**：`build\R3R_debug.log` 8 905 行 / 500 725 B，mtime `2026-10-24 20:19:02`；产生者是 `build\openttd.exe`（52 307 456 B，mtime `2026-10-24 20:15:46`）= 第 291 轮 (e) 版 ⇒ 采样有效，KI-462 涂装判据通过。

**日志里与「分组」相关的探针只有 2 行**（`Select-String -Pattern 'GRP-NORM|SEGTRAIT|group='`）：

```
7986: SEGTRAIT-RECOVER seg=24 unit=2 group=65534 name=1
7987: SEGTRAIT-SYNC tag=couple head=21 seg=2 unit=2 group=3 borrowed=0 ctrl=24
7988: INVAR-CTRL tag=couple head=21 nseg=2 ctrl=24 ctrl_pri=1 head_pri=2 borrowed=1
```

（7986/7987 探针在 `src\train_cmd.cpp`：RECOVER 打的是段头 `group_id_backup.base()`，SYNC 打的是段行同步后的 `row->group_id`。）

**初步读数（判定：现场与「还原」相关，方向待玩家确认）**：

- 读档那一刻段 24 的停放分组是 `65534`。
- 同一个段 24 在**耦合提交点**被 `SEGTRAIT-SYNC` 同步成 `group=3`（真实自定义组）。
- 两条读数合起来说明：**读档恢复的段分组与耦合后写回的段分组不是同一套**，正是"段的普通列车分组还原"问题的形状。

**数值已确认**：`65534 = 0xFFFE = DEFAULT_GROUP`（`src\group_type.h:19`：「Ungrouped vehicles are in this group」）⇒ 读档侧是"段 24 原本属于**默认组**"，耦合侧把它写成 `group=3`（真实自定义组）。问题定性候选：**耦合把这个段的组并进伙伴的组，之后没有任何路径把它还回去**。

**代码锚点（`src\train_cmd.cpp`）**：

- `R3RRecoverSegmentTraits()`（:5944-5987）打印 `SEGTRAIT-RECOVER`。语义：把**同段内后续车**上停放的 `unitnumber_backup` / `name_backup` / `group_id_backup` 搬到段头上（只在段头自己缺这一项时搬）。⇒ 65534 原本在段 24 的**段内某节普通车**上，段头自己没有（D-2）。
- `R3RSyncSegmentTraits()`（:5882-5921）打印 `SEGTRAIT-SYNC`。语义：把控制段段头（或借用期链头）的活字段 `group_id` 镜像进控制段行 ⇒ 行被写成 3（D-1/D-3 的上游）。
- `R3RParkTrainGroupID()`（:3667-3673）打印 `GRP-PARK`；`R3RRestoreTrainGroupID()`（:3676-3696）打印 `GRP-RESTORE`，守卫 `IsFrontEngine() || IsFrontWagon()`。**全日志这两串一次都没出现** ⇒ 本次会话停放/取回都没跑过，段重新成为链头时**没有**把组取回（D-3，猜测根因·待验证）。
- `R3RNormaliseChainGroups()`（第 159 轮口径「分组属于整列车」）：耦合提交点把整链 `group_id` 归一化到控制段分组，并把各段头原有分组停进 `group_id_backup`。⇒ 若玩家期望"按段"，这是**口径变更需求**而非实现 bug（D-4）。

## 三、玩家答复（2026-10-24，按编号回填，原话保留）

- **C-1 现象方向** = **①「丢了」**。玩家原话：「C1,段的组变成未分组了。」
  ⇒ 段的组掉成 `DEFAULT_GROUP`（65534 / 未分组），不是串成别的组、也不是纯界面问题。
- **C-2 出问题的提交点** = **重联时就已串味，解挂后掉成未分组**。玩家原话：「C2,重联时，这个段预览的分组就已经跑到控制段的分组去了，解挂之后，就变成未分组了」
  ⇒ 两个阶段两个症状：①重联提交点：段的分组被**控制段的分组覆盖**（= `R3RNormaliseChainGroups()` 的既定归一化，第 159 轮口径）；②解挂提交点：段**没有取回自己原来的组**，落到默认组。
  ⇒ 与 D-3 的猜测根因逐字吻合：`GRP-RESTORE` 零条 = `R3RRestoreTrainGroupID()` 从未被调用，解挂后无人把 `group_id_backup` 还回活字段。
- **C-3 期望口径** = **「分组属于段」**（选择后者）。玩家原话：「C3，我希望分组属于段，重联期间段在自己原来的组内部」
  ⇒ 决定性拍板：**重联期间**车队列表里该段也显示**自己的组**，不得显示控制段的组。因此这不只是"解挂时还回去"，而是要把第 159 轮「分组属于整列车」的口径改变为「分组属于段」。
- **C-4 复现最小步骤**：未提供（可不追问，C-1~C-3 已足以定向）。

## 四、由 C-1~C-3 得出的定性（结论行）

- 本问题是 **D-4 口径变更需求**（不是单纯实现 bug）：现状 `R3RNormaliseChainGroups()` 在重联提交点把整链 `Vehicle::group_id` 归一化为控制段分组，并把各段头原分组停放进 `group_id_backup`；玩家要求的是"每段各自持有自己的组"，即这套归一化必须**按段**而不是**按链**。
- 症状 ②（解挂后变未分组）是症状 ①（重联时被覆盖）的必然后果 + `R3RRestoreTrainGroupID()` 未被调用（D-3）。若把 ① 改成按段归一化，② 自然消失；`group_id_backup` 那套停放/取回机制**在新口径下应大部分变为不再需要**（这正是要复核的地方，不能留两套并存）。

## 五、落地（口径 C-3「分组属于段」）—— 全部 .cpp，未碰任何 src\*.h

### 5.1 改动清单

| # | 文件 | 位置 | 改动 |
|---|------|------|------|
| 1 | `src\group_cmd.cpp` | `SetTrainGroupID()` | 循环内加 `if (u != v && u->IsSegmentFront()) break;` —— 只改 `v` 所在那一段，不再把整条物理链推平 |
| 2 | `src\group_cmd.cpp` | `UpdateTrainGroupID()` | 同上（只归一化本段），注释改写为"group belongs to the segment" |
| 3 | `src\group_cmd.cpp` | 新增 `uint R3RSetSegmentGroupID(Train *v, GroupID new_g)`（约 :1103-1143） | 逐节写到下一个 ★ 为止：`UpdateNumEngineGroup()` 簿记 + `group_id` + 清 `colourmap` + 失效 NewGRF/图像缓存 + `UpdateViewport`，返回实际改动节数 |
| 4 | `src\train_cmd.cpp` | `R3RNormaliseChainGroups()`（约 :7836 起，整体重写） | 取消"整链推平到控制段"：逐段头遍历，`group_id_backup` 有效 ⇒ `R3RSetSegmentGroupID(t, parked)` 并清空备份（`GRP-RESTORE-SEG`），否则 `R3RSetSegmentGroupID(t, t->group_id)` 只补齐**本段内**各节；挂接分组仍全链 OR 后逐段广播（`CGRP-NORM-UNION` / `CGRP-NORM`，第 159 轮 q-2=肆 口径不变） |
| 5 | `src\train_cmd.cpp` | `R3RBorrowControlTraits()` | `own_group` 不再受 `borrowed` 影响：备份优先，否则链头自己的 `group_id` |
| 6 | `src\train_cmd.cpp` | `R3RSyncSegmentTraits()` | 写控制段行改用 `ctrl_group = (ctrl->group_id_backup != Invalid) ? ctrl->group_id_backup : ctrl->group_id`，不再镜像链头活字段 |
| 7 | `src\couple_group.cpp` | `R3RNormaliseChainGroupsAfterLoad()`（约 :856 起，重写） | 删除"整链推平+停放备份"，逐段：备份有效 ⇒ `R3RSetSegmentGroupID(s, parked)` 并清空（`GRP-RESTORE-SEG-LOAD`），否则补齐本段；随后仍做挂接分组 OR 广播 |
| 8 | `src\vehicle.cpp` | `R3RLiveryGroupOf()` | 多段链取值改为"段头自己的 `group_id_backup` → 段头 `group_id` → `R3RSegmentSectionGroupID(v)`"，注释改成"分组属于段" |

跨文件调用一律用 `.cpp` 内手写前置声明（`uint R3RSetSegmentGroupID(Train *, GroupID);`），避免改头文件触发 KI-183 全量重编。

### 5.2 停放/取回（`R3RParkTrainGroupID` / `R3RRestoreTrainGroupID`）复核结论

- **保留**，但职责收窄：C-3 之后提交点不再改写别段分组，唯一还会抹分组的是**上游**车库拖动分支
  `CmdMoveRailVehicle()` 里的 `R3RParkTrainGroupID(src); SetTrainGroupID(src, DEFAULT_GROUP);`（约 :4103-4126）
  —— 上游"变成前端列就进默认组"这句仍在，所以仍需先停一份、由 `NormaliseTrainHead()` 取回。
- `NormaliseTrainHead()` 里 `R3RRestoreTrainGroupID(head); UpdateTrainGroupID(head);` 的**顺序仍然必须如此**：
  先取回自己的（`num_vehicle -1/+1` 簿记在一起），再让 `UpdateTrainGroupID()` 把本段各节补齐；
  后者已只到下一个 ★ 为止，不会再跨段覆盖。注释已同步改写。
- `R3RSyncChainAfterDepotEdit()` 里 `GroupStatistics::CountVehicle(chain, -1/-1… +1)` 包住
  `R3RNormaliseChainGroups(chain)` 的写法**保留**：归还停放分组时链头所属组会变，而
  `R3RSetSegmentGroupID()` 只管 `num_engines`，`num_vehicle` 仍需这层配对簿记。
- 耦合/解挂提交点（`Couple()` :12771 附近、`DecoupleTrain()` :9664 附近）的注释已按 C-3 改写，代码不变。

### 5.3 兼容与迁移

- 旧档（第 144~291 轮）"整链推平"留下的 `group_id_backup` 在 `R3RNormaliseChainGroups()`（提交点）与
  `R3RNormaliseChainGroupsAfterLoad()`（读档）里逐段归还并清空 ⇒ 不会再出现"备份里烂着真分组"。
- `R3RRecoverSegmentTraits()` 不动：它负责把"停在段内普通车上的备份"搬到段头（现场 `SEGTRAIT-RECOVER seg=24 unit=2 group=65534` 的形态），是 C-3 归还在旧档上的前置步骤。
- `src\vehicle_base.h:381` 那段说明 `group_id_backup` 的注释仍是旧口径；因改头文件会触发 KI-183 全量重编，**本轮刻意不改**，留待下次必然全量时一并订正（已登记为欠账）。

### 5.4 构建状态

见 §5.5（构建自证）。

### 5.5 构建自证

复用既有入口 `_tmp_inc_build.cmd`（未新建任何 `.cmd`）。

**第一次构建失败（EXIT_CODE=1）**：`src\group_cmd.cpp(1134): error C2660` ——
`UpdateViewport` 不接受 1 个参数。

- 根因：`Train` 继承的 `SpecializedVehicle<...>` 自己声明了
  `inline void UpdateViewport(bool force_update, bool update_delta)`（`src\vehicle_base.h:1791`），
  它把基类的 `void Vehicle::UpdateViewport(bool dirty)`（`src\vehicle_base.h:1011`）**名字隐藏**掉了。
  `group_cmd.cpp:795` 那一处能写 `u->UpdateViewport(true)` 是因为循环变量是 `Vehicle *`；
  而 `R3RSetSegmentGroupID()` 的循环变量是 `Train *` ⇒ 只解析到两参版本。
- 修法：写成 `u->Vehicle::UpdateViewport(true);`（限定基类名），语义与 `SetTrainGroupID()` 完全一致
  （只把精灵标脏，不重算 delta/几何 —— 这一点很重要，R3R 不做位置重排）。
- 顺带说明：单独用 `ninja.exe` 编 obj **必须**先 `call vcvars64.bat`，否则 `stdafx.h(33): fatal error C1083: 无法打开包括文件 "stdint.h"`
  （缺 `INCLUDE`）。

**第二次构建通过**：

| 项 | 读数 |
|---|---|
| 护栏 | `GUARD: incremental is safe (no header/lang file is newer than the newest object)` |
| 日志尾部 | `[2/2] Linking CXX executable openttd.exe` |
| `build\R3R_incbuild.done` | `EXIT_CODE=0` |
| 错误计数 | 日志无 `error C*` / `fatal error` / `FAILED:` / `build stopped` |
| `src\train_cmd.cpp` → obj | 20:40 → 20:48 |
| `src\group_cmd.cpp` → obj | 20:49 → 20:49（改完后单独编 obj 验证，再随整体链接） |
| `src\couple_group.cpp` → obj | 20:35 → 20:48 |
| `src\vehicle.cpp` → obj | 20:36 → 20:48 |
| `build\openttd.exe` | **2026-10-24 20:51:19，52 307 456 B** |
| `read_lints`（4 文件） | 0 条 |

**复测判据（游戏内，待做）**：

1. 段 A 属于组 X、段 B 属于组 Y，重联成一条链后：车队列表里 A 仍显示 X、B 仍显示 Y（不再全变控制段的组）。
2. 重联后再解挂：两半各自回到自己原来的组（X / Y），不出现"未分组"。
3. 多段链的车库拖动（把别链并进来 / 拖出去）后，各段分组不变；`GRP-RESTORE-SEG` 只在旧档有停放副本时出现。
4. 负判据：单段链（普通列车）重联/解挂/车库编辑行为与第 291 轮逐字相同，日志无 `GRP-NORM ... nseg=` 之外的变化。
5. 负判据：同一段内各节分组始终一致（`GRP-NORM` 的 `nfix=0` 常态）。
6. 挂接分组（CoupleGroup，`couple_groups` 位掩码）仍按并集广播：`CGRP-NORM-UNION` 行不变（第 159 轮 q-2=肆 口径不回退）。

## 六、首次复测：新问题「解挂后编号变成 65535」（2026-10-24，先落盘后取证）

### 6.1 玩家原话

> 你修改了之后，出了新的问题，解挂之后有个列车编号跑到 65535 去了

- 现场文件：`D:\temp\codebuddy-dropped-files\d3194fe9-ba1c-476c-a456-68d09154b440\R3R_debug.log`（750 145 B，mtime 2026-10-24 20:58）。
- 触发动作：**解挂**之后；表现：某一列**编号**变成 `65535`。
- 玩家未说明是哪个界面/哪一列（见 §6.4 C-1），本轮先登记，**未改任何源码**。

### 6.2 疑点（发现即登记）

- **REG-292-D1｜`65535` 语义歧义**：`65535 == 0xFFFF == UINT16_MAX`，本代码库里至少对应三种对象，必须先分清：
  - `GroupID::Invalid()`（`group_id_backup` 的哨兵值就是它）；
  - `unitnumber`（`uint16_t`，0 = 无号）；
  - `VehicleID::Invalid().base()`。
  本轮改动只碰过 `group_id` / `group_id_backup` 一条链，而"列车编号"在既有术语里通常指 `unitnumber` ⇒ 二者必须先用日志区分，不得先假定。
- **REG-292-D2｜本轮新探针在玩家日志里疑似缺席**：初查该日志按大写检索 `GRP-NORM` / `GRP-RESTORE-SEG` / `UNIT-` 均 0 命中，而旧探针在场（`SEGTRAIT-SYNC tag=decouple-v head=21 seg=1 unit=1 group=3`，日志第 6069 行；`SEGTRAIT-RECOVER seg=24 unit=2 group=65534 name=1` 在第 1031 行）。两种可能：①该日志由**不含本轮改动**的 exe 产生；②探针被 `R3RDbgEdge` 节流或落在筛选之外。⇒ 在核对 exe 版本与大小写/筛选之前，**不得据"日志没有"反推代码没跑**。

### 6.3 猜测根因（待验证，成立/证伪后回填）

- **H1（优先，与改动直接相关）｜段行分组被写成 `GroupID::Invalid()`**：`R3RSyncSegmentTraits()`（`src\train_cmd.cpp:5912`）现在写 `ctrl_group = (ctrl->group_id_backup != Invalid) ? ctrl->group_id_backup : ctrl->group_id`；若两者都是 Invalid（或段行原有值被 Invalid 覆盖），段行就存 `0xFFFF`，任何把它拼进列表列的地方都会显示 `65535`。解挂正好跑 `decouple-v` / `decouple-u` 两个 settle 点。
  - 判据：日志出现 `SEGTRAIT-SYNC ... group=65535` / `dump_vehicle` 段行 group=65535 / `GRP-NORM ... g=65535`。
- **H2｜`unitnumber` 变成 65535**：与分组无关，属取号/还号链（`R3RRestoreUnitNumber` / `NormaliseTrainHead` 号池），本轮未动；若成立则是第 195 轮 `UNIT-RESTORE-SKIP-BORROW` 之后的存量问题被本轮段划分变化触发。
  - 判据：日志 `SEGTRAIT-SYNC ... unit=65535` / `UNIT-RESTORE veh=… id=65535` / 车辆窗口标题 `#65535`。

### 6.4 待玩家确认（编号 C-1..C-3，答完直接回填本节）

- **C-1**：`65535` 出现在哪个界面哪一列 —— 车队列表的「编号/单位号」列、车辆窗口标题 `#N`、还是「分组」列 / 分组下拉框？
- **C-2**：变成 65535 的是**解出来的那半**还是**留下的那半**？是链头那一节还是某个段头？
- **C-3**：本次复测用的 `build\openttd.exe` 是否为 2026-10-24 20:51:19 那一版（含本轮 8 处改动）？若不是，本轮改动可能根本不在场上。

### 6.5 取证结论（2026-10-24，已落盘）

**关键常量（`src\group_type.h:17-19`）**：

```17:19:src/group_type.h
static constexpr GroupID NEW_GROUP{0xFFFC};   // 65532
static constexpr GroupID ALL_GROUP{0xFFFD};   // 65533
static constexpr GroupID DEFAULT_GROUP{0xFFFE}; // 65534  ← "未分组"
```

`GroupID::Invalid()` = `0xFFFF` = **65535**（`PoolID` 的哨兵）。⇒ 玩家看到的 `65535` 与日志里的 `65534` 是**两个不同的哨兵**，但都属"分组编号"这一列。

**现场铁证（同一段 24，耦合 → 解挂）**：

- 读档（日志 `:1-2`）：`SEGLOAD-PARK veh=45 … gid=3 gbk=65534` / `veh=48 … gid=2 gbk=65534`
  ⇒ **`group_id_backup` 里存的就是 `DEFAULT_GROUP`(65534)**，即"未分组"被当成了"停放的真实分组"。
- 耦合后（日志 `:1085`）：`SEGTR-SNAP i=2 id=2 front=24 star=1 ctrl=1 | live u=0 … gid=2 | bk u=2 … gbk=65534 | row use … g=65534`
  ⇒ 段 24 的**活**字段是组 **2**（正确），但**备份**是 65534，且**段行已被写成 65534**。
- 解挂后（日志 `:6123`）：`SEGTR-SNAP i=1 id=0 front=24 star=1 ctrl=1 | live u=2 … gid=65534 | bk … gbk=-1 | row none … g=-1`
  ⇒ 段 24 的活分组从 **组 2 变成 65534（未分组）**，备份被清空，段行被回收（`row none`）。

**判定 H1 成立、H2 暂不成立**：本轮"备份优先"口径把 `DEFAULT_GROUP` 这个**合法但表示未分组**的值当成了"停放的真实分组"，在提交点（耦合 `:1085` 的段行写入、解挂 `:6123` 的活字段归还）把它覆盖到真正的组上 ⇒ 段的分组被抹成未分组；而 `row none` 之后任何走"段行兜底"的读点只能拿到 `GroupID::Invalid()` = **65535**，这正是玩家看到的那个编号。

⇒ 这是**本轮引入的回归**（第 291 轮及以前备份只写不读，不会毒化活字段/段行）。修法方向（待落地）：

1. 所有"备份是否有效"的判定从 `!= GroupID::Invalid()` 收紧为 `!= GroupID::Invalid() && != DEFAULT_GROUP`；
2. 提交点（`R3RNormaliseChainGroups` / `R3RNormaliseChainGroupsAfterLoad` / `R3RRestoreTrainGroupID`）遇到 `parked == DEFAULT_GROUP` 时**丢弃备份、保留 live**（陈旧备份不得覆盖玩家后来设的组）；
3. 读点（`R3RSyncSegmentTraits` / `R3RBorrowControlTraits` / `R3RLiveryGroupOf`）同样排除 `DEFAULT_GROUP`，并把最后的兜底从 `GroupID::Invalid()` 改成 `DEFAULT_GROUP`，界面就不会再出现 65535。

## 七、第 293 轮：修法落地 + 构建自证（2026-10-24）

### 7.1 统一判据函数

新增"停放副本是否为真分组"的唯一判据，两条主线各一份（`static`，未新增头文件声明 ⇒ 不触发全量）：

- `src\train_cmd.cpp:3675` `static bool R3RIsParkedGroupUsable(GroupID g) { return g != GroupID::Invalid() && !IsDefaultGroupID(g); }`
- `src\couple_group.cpp:889` 同义函数（文件内私有，供 `R3RNormaliseChainGroupsAfterLoad()` 等使用）

口径一句话：**`DEFAULT_GROUP(65534)` 是"未分组"，不是"一个分组"；`Invalid(65535)` 是哨兵，任何界面/段行都不许出现。**

### 7.2 逐点改动清单

| 位置 | 改法 |
| --- | --- |
| `src\train_cmd.cpp:3685` `R3RParkTrainGroupID()` | 未分组（DEFAULT_GROUP）**不再停进副本**，从源头不产生毒化副本 |
| `src\train_cmd.cpp:3705` `R3RRestoreTrainGroupID()` | 副本若是 DEFAULT_GROUP ⇒ 只清副本并打 `GRP-DROP-DEFAULT`，**保留活值**，绝不归还 |
| `src\train_cmd.cpp:5858` `R3RBorrowControlTraits()` | 链头段自己的组：`R3RIsParkedGroupUsable(backup) ? backup : live`；写行时把 `Invalid` 归一成 `DEFAULT_GROUP` |
| `src\train_cmd.cpp:5937` `R3RSyncSegmentTraits()`（控制段行） | 同上；`ctrl_group = (raw == Invalid) ? DEFAULT_GROUP : raw` |
| `src\train_cmd.cpp:6079` `R3RSyncSegmentTraits()`（其它隐性段行） | 同上（本轮最后一处补丁） |
| `src\train_cmd.cpp:5985/6004/6011` `R3RRecoverSegmentTraits()` | 三处"副本是否已有值"判定改走 `R3RIsParkedGroupUsable()`（否则 DEFAULT_GROUP 副本会挡住真正该取回的那一份） |
| `src\train_cmd.cpp:7581` 借用段 unborrow | 副本是 DEFAULT_GROUP ⇒ 只丢副本 + `GRP-DROP-DEFAULT tag=ctrl-unborrow`，不改活字段、不清 `unborrowed` 标记 |
| `src\train_cmd.cpp:7869` `R3RNormaliseChainGroups()` | `GRP-DROP-DEFAULT head=…` 丢副本；归还分支只认真分组；补齐分支把 `Invalid` 归一到 `DEFAULT_GROUP` |
| `src\couple_group.cpp:909` `R3RNormaliseChainGroupsAfterLoad()` | 读档先丢 DEFAULT_GROUP 副本（`GRP-DROP-DEFAULT-LOAD`），再归还/补齐；补齐侧同样归一 `Invalid` ⇒ `DEFAULT_GROUP` |
| `src\couple_group.cpp:1166` `R3RSegmentGroupID()` | 兜底**不再返回 `GroupID::Invalid()`**；活字段是 `Invalid` 时归一到 `DEFAULT_GROUP` |
| `src\couple_group.cpp:1215` `R3RSegmentSectionGroupID()` | 同上（子行/分组列表读点） |
| `src\couple_group.cpp:1297` `R3RSegmentStoreTraits()` | 行值优先活字段（≠DEFAULT_GROUP），其次**可用**副本，否则写 `DEFAULT_GROUP`；绝不写 `Invalid` |
| `src\couple_group.cpp:1503` `R3RRecoverSegmentTraits()`（load 侧） | 同口径收紧 |
| `src\couple_group.cpp:1330` 克隆特质复制 | 只复制**可用**副本 |
| `src\vehicle.cpp:3333` `R3RLiveryGroupOf()` | 涂装归属读点同样跳过 `DEFAULT_GROUP`/`Invalid` 副本与活值 |

### 7.3 构建自证（内测树，探针 ON）

复用既有 `_tmp_inc_build.cmd`（**未新建任何 .cmd**；构建前 `tasklist` 确认 openttd.exe 未运行）：

| 项 | 值 |
| --- | --- |
| 护栏 | `GUARD: incremental is safe (no header/lang file is newer than the newest object)`（只改 3 个 .cpp） |
| 链接行 | `[5/5] Linking CXX executable openttd.exe` |
| `build\R3R_incbuild.done` | `EXIT_CODE=0`（2026-10-24 21:14:40） |
| 错误计数 | `error C*` / `fatal error` / `FAILED:` / `build stopped` = **0** |
| `src\train_cmd.cpp` → obj | 21:10:30 → 21:12:26 |
| `src\couple_group.cpp` → obj | 21:10:19 → 21:12:23 |
| `src\vehicle.cpp` → obj | 21:10:36 → 21:12:30 |
| `build\openttd.exe` | **2026-10-24 21:14:23，52 314 624 B**（晚于全部源码与 obj） |
| `read_lints`（3 文件） | 0 条 |

### 7.4 本轮复测判据（替换 §6.4 的三问，游戏内验证）

1. **正判据（本 bug）**：同样的"耦合 → 解挂"操作后，两半的分组编号都不再出现 `65535`；解出的那一半回到它自己的组，留下的那一半也保持原组。
2. **正判据（日志）**：旧档（备份里带 65534）读档或提交点应出现 `GRP-DROP-DEFAULT(-LOAD) … bk=65534`，且**不得**出现随后的 `GRP-RESTORE(-SEG/-LOAD) … group=65534`。
3. **正判据（段行）**：`SEGTR-SNAP` 的 `row use … g=` 只出现真实组号或 65534，**永不出现 -1/65535**。
4. **负判据（真分组不被吞）**：段属于组 2/组 3 的旧档，读档 + 解挂后仍分别是 2/3，不被抹成未分组。
5. **负判据（回归）**：单段普通列车（无分组）耦合/解挂/车库编辑后，`groups` 子菜单与车队列表显示与第 291 轮逐字相同（仍是"未分组"）。
6. **仍未覆盖**：`R3RSegmentGroupID(nullptr)` 与 `R3RSegmentSectionGroupID(nullptr)` 仍返回 `Invalid`（调用方已全部保证非空；若将来有 GUI 传空指针会重现 65535，属可接受的设计选择）。

### 7.5 未做 / 欠账

- `src\vehicle_base.h:381` 的字段注释仍是第 292 轮旧口径（"整链推平"），改头文件会触发全量重编，**本轮刻意未动**，留待下次必须全量的轮次一并改注释。
- 存量（第 292 轮 exe 期间产生）的脏段行/脏备份：读档 `R3RNormaliseChainGroupsAfterLoad()` 会丢副本，但**已经被写成 65534 的段行**只在段头有真分组时才被改写；玩家用新 exe 存档再读一次即可自愈。
- 玩家三问 C-1/C-2/C-3 仍无答复（见 §6.4）；本轮按日志证据 `:1085`/`:6123` 独立判定，不依赖答复。

