# 第 435 轮：对铰接式列车 Ctrl+左键不再反转图像

## 一、玩家报告（原话）

「我服了，现在对铰接式列车ctrl+左键直接不反转图像了」

⇒ 关键基准：**此前**对（真）铰接式列车执行 Ctrl+左键，图像会反转（换端）；**现在**（第 434 轮 exe，`build\openttd.exe` 2026-11-13 23:04:46）不反转了。

## 二、已知代码锚点（先落盘，待验证）

- Ctrl+左键「点中同一节车」（不是拖拽）的落点：`src\depot_gui.cpp:1755-1756`

```cpp
if (result.wagon != nullptr && result.wagon->index == sel && _ctrl_pressed) {
    Command<Commands::ReverseTrainDirection>::Post(STR_ERROR_CAN_T_REVERSE_DIRECTION_RAIL_VEHICLE, Vehicle::Get(sel)->tile, Vehicle::Get(sel)->index, true);
}
```

⇒ 第三参 `force=true`（强制换端，`VehicleRailFlag::ForceFlipReverse` 消费路径）。
- 反向动作本体：`ReverseTrainDirection()`（`src\train_cmd.cpp`，force 分支在入口处消费标志，走 `ReverseTrainSwapVehicles` 三连 + `UpdateStatusAfterSwap`），日志标签 `REVERSEDIR ... force_swap=1` 与 `RF` 首尾对调打印。
- 图像侧：`Train::UpdateDeltaXY` 的 `bounds/x_offs/y_offs` 由 `cached_veh_length + direction + flip_offs` 推出（见长期记忆「视觉包围盒错位」条）。

## 三、猜测根因（待验证，先登记）

- **G-435-A（首选）**：第 434 轮只改了三处——`src\train.h` 的 `Train::GetPower()` override 分支 0→1、`src\train_cmd.cpp` 的 car-only 自走闸门判据换成 `R3RChainHasRealTraction()`、`src\vehicle_cmd.cpp` 的 `SEG-POWER` 探针。三处都不碰 `direction`/图像/`ReverseTrainDirection`。**若为真**，则"不反转"要么是本轮之前就存在、只是玩家这次才注意，要么来自同一次全量重编带出的既有状态（护栏删了 621 个 obj，全量重编）。
- **G-435-B**：`CmdReverseTrainDirection` 的命令层提前 `return CMD_ERROR`（校验失败 ⇒ 图像不动、只弹错误），原因可能在"车在库内/在链中/含真铰接组/段边界"某道守卫。
- **G-435-C**：命令执行成功但**图像没刷新**（`direction` 变了、sprite/缓存/局部重绘没跟上），表现为"看起来没反转"。
- **G-435-D**：Ctrl+左键根本没进到 `:1755` 分支（`result.wagon->index == sel` 不成立，或 `sel` 已被前一次拖拽清空），于是被当作拖拽（原地重挂）处理，自然没有反转。

## 四、疑点（REG-435-A）

- 疑点：玩家用词是"铰接式列车"，未说明是**真铰接 GRF 车**（`IsArticulatedPart()` / `HasArticulatedPart()`）还是 R3R 的**打散铰接组**（`ArticGroupHead/Member`）还是**段**。
  - 为什么存疑：三条路径的守卫不同（真铰接组有 `FLIP-REFUSE-REAL-ARTIC` 门禁；打散组走 `R3RDearticulateOneGroup`；段走 `R3RFlipChainBySegments`）。
  - 还需数据：该车 `dump_vehicle` 的 `st:`（是否含 `A`）与 `tf:`（是否含 `H`/`M`/`S`/`E`），以及当次日志里的 `REVERSEDIR`/`FLIP-REFUSE-REAL-ARTIC`/`REVERSEDIR ... force_swap=` 行。

## 五、复测判据（待补）

1. Ctrl+左键铰接式车，图像反转（与第 433 轮 exe 行为一致）。
2. 日志出现 `REVERSEDIR ... force_swap=1` 与首尾对调的 `RF` 行。
3. 负判据：普通（非铰接）列车 Ctrl+左键行为逐字不变；无 Ctrl 的普通点击/拖拽行为不变。

## 六、取证记录（2026-11-13，第 435 轮）

### 6.1 采样有效性

- `build\R3R_debug.log`：28.4 MB，LastWriteTime **2026-11-13 23:15:53**，晚于第 434 轮 exe（`build\openttd.exe` 23:04:46）⇒ **是本轮报告的现场**。

### 6.2 硬证据：`ARTIC-FLIP` 全部成对出现（翻转一次立刻被翻回）

```
200345: ARTIC-FLIP head=3  n=3 real_artic=1 flipped=1 seg=0
200375: ARTIC-FLIP head=3  n=3 real_artic=1 flipped=0 seg=0
200515: ARTIC-FLIP head=3  n=3 real_artic=0 flipped=1 seg=0
200516: ARTIC-FLIP head=3  n=3 real_artic=0 flipped=0 seg=0
200517: ARTIC-FLIP head=3  n=3 real_artic=0 flipped=1 seg=0
200518: ARTIC-FLIP head=3  n=3 real_artic=0 flipped=0 seg=0
200940: ARTIC-FLIP head=9  n=3 real_artic=1 flipped=1 seg=0
200943: ARTIC-FLIP head=9  n=3 real_artic=0 flipped=0 seg=0
200948: ARTIC-FLIP head=0  ... flipped=1 / 200949: ... flipped=0
...（全日志 23 对，无一例外，最后一对 201027 flipped=1 之后日志结束）
```

- 判决：**每一次 `flipped=1` 都被紧随其后的 `flipped=0` 抵消**，净效果 = 图像不变。
- `flipped=` 打印的是 `group_head->flags.Test(VehicleRailFlag::Flipped)`，在 flip 循环**之后**（`src\train_cmd.cpp:5879-5882`）⇒ 每行代表一次真实执行过的命令。
- ⇒ 玩家看到的"不反转"不是"没执行"，而是**成对执行互相抵消**。

### 6.3 命令入口只有一处，且一次鼠标释放只调用一次

- 唯一 Post 点：`src\depot_gui.cpp:1755-1756`（`OnDragDrop` 的 `WID_D_MATRIX` 分支，条件 `result.wagon->index == sel && _ctrl_pressed`）。
- `OnDragDrop` 的唯一调用点：`src\window.cpp:2171`（`HandleDragAndDrop`，`!_left_button_down` 分支），一次鼠标释放调一次。
- 结论：**一次 Ctrl+左键 = 一次命令**是当前实现的预期路径 ⇒ 成对的 `1/0` 对应**两次独立点击**（玩家第一次没看到变化，又点了一次）。

### 6.4 关键异常：`real_artic` 打散后没有持久生效

- 200345 `real_artic=1`（打散了）→ 200375 **仍是 `real_artic=1`**（若是同一个组，`ClearArticulatedPart()` 之后应为 0）。
- 对照 `R3RDearticulateOneGroup()`（`src\vehicle_cmd.cpp:528-593`）：`n_total` 靠 `GetNextArticulatedPart()` 起头计数；父车与所有 part 的 `ClearArticulatedPart()` / `SetEngine|SetWagon()` 都在循环里，**正常应打散成功**。
- ⇒ **疑点 REG-435-B（新）**：是否存在"打散执行了但下一次点击又变回真铰接"的路径（读档重建 / `ConsistChanged` / 段结算重算）。200515 起 `real_artic=0` 说明某个时刻生效过，但在 200345→200375 之间没有。

### 6.5 猜测根因（本轮新增，待玩家数据验证）

- **G-435-E（首选）**：**镜像 + Flipped 互相抵消**。`src\train_cmd.cpp:5872-5877` 对组内每节同时做 `flags.Flip(Flipped)` 与 `FlipArticGroupFlipped()`（镜像 NewGRF 变量 0x4D「position in articulated vehicle」）。对使用 0x4D 选图的 NewGRF 车，两处改动方向相反 ⇒ 选回**同一张 sprite** ⇒ 视觉上完全不变（这正是第 391 轮注释写的设计目的 "keeps the NewGRF faces in their pre-flip order"）。若成立，则"不反转图像"是**设计副作用**，不是本轮回归。
- **G-435-F**：玩家其实按了两次（一次操作看不出来、再点一下正好抵消）⇒ 与 G-435-E 叠加时最典型。
- **G-435-G**：`real_artic` 打散失效（REG-435-B）导致只有父车一节真的换了脸，其余两节由 parts 绘制，整组看起来没变。

## 七、需要玩家提供的数据（本轮阻塞点）

1. 该车 `dump_vehicle <id>` 的 `st:` 行是否含 `A`，`tf:` 行是否含 `H`/`M`（区分真铰接 / 打散组）。
2. 是**点了一次**就看到"没反转"，还是点了几次？
3. 期望效果：整组每节图像左右镜像（当前代码意图）还是整组前后颠倒（运营方向换端）？
4. 若方便，附上该车所用 GRF 名称（判断是否使用 0x4D / RailFlips）。

结论状态：**已定位到"成对翻转互相抵消"这一事实层，根因（G-435-E/F/G）尚未判定**。

## 八、玩家答复回填（2026-11-13，第 435 轮续）

玩家原话（逐字）：

> 「我是对散链状态和成段状态的同一节铰接车都操作了几次，都是没变化。我期望的效果是每节图像镜像+整组前后颠倒。而实际上，修复功率0之前的表现就已经符合我需求了，所以我的意思是你是否引入了一个东西让前几天的代码失效了」

### 8.1 由答复直接确立的事实

1. **判据 2（§七.2）答案 = 多次**，且**散链 / 成段两种状态都试过** ⇒ 排除"只点了一次"与"段状态特例"。
2. **判据 3（§七.3）答案 = 每节图像镜像 + 整组前后颠倒**（两者都要）。这与当前实现的设计**一致**：`flags.Flip(Flipped)`（镜像方向）+ `FlipArticGroupFlipped()`（镜像 0x4D 组内序号）组合的语义正是"整组原地掉头"。
3. **判据 1（`dump_vehicle` 的 st:/tf:）仍未取**；但日志已给出等价信息：
   `200345 ARTIC-FLIP head=3 n=3 real_artic=1 flipped=1 seg=0` → `200375 ... real_artic=1 flipped=0`（同一 head=3 又打散一次）
   `200515 起 real_artic=0`（打散已持久）**之后依旧每次 n=3、flipped=1/0 成对**。
   ⇒ **打散成功、整组 3 节全部翻转、命令确实执行**；REG-435-B 只是前两次的过渡态，不是"整组只翻一节"。
4. 新增关键事实：`n=3`（不是 1）⇒ `for (Train *w = group_head; ...) if (!w->IsArticGroupMember()) break;` 的早退**没有**发生，组内三节都翻到了。

### 8.2 玩家关键指认（决定排查方向）

「修复功率0之前的表现就已经符合我需求了」⇒ **基准 = 第 434 轮 exe 之前的版本**（即第 433 轮 exe `build\openttd.exe` 2026-11-13 22:14:39，或更早的 432/431），
⇒ **必须优先证明"第 431~434 轮这四个提交/改动里，有哪一处让翻转失效"**，而不是继续在"第 391 轮设计副作用"上打转。

### 8.3 git 时间线（实证，`git log --oneline` / `git status --short`）

- `7b83cb770a` (tag `r3r-stable-2026-11-13`) = 第 430 轮发布记录；其父 `275d234ccf` = rounds 407-429 checkpoint。
- **第 430 轮之后的改动全部仍在工作区未提交**，`git diff --stat` 只有 8 个文件：
  `R3R_KNOWN_ISSUES.md` / `src/articulated_vehicles.cpp`(6) / `src/couple_group.cpp`(33) / `src/pathfinder/yapf/yapf_rail.cpp`(26) /
  `src/sl/vehicle_sl.cpp`(25) / `src/train.h`(18) / `src/train_cmd.cpp`(170) / `src/vehicle_cmd.cpp`(10)
  ⇒ 排查面收敛到 **431（空指针）/432（pbs545）/433（分组+寿命+STATS-DRIFT）/434（功率 0）** 四轮。
- `git log -3 -- src/newgrf_engine.cpp src/vehicle.cpp` = 最近一次改动仍是 `22dc2b6833`（2026-11-04，第 391 轮含 artic group flip）
  ⇒ **`newgrf_engine.cpp` 的 0x4D 镜像代码自 11-04 起文本未变**，故"代码文本层面的新回归"不在该文件。

### 8.4 猜测根因（待验证）

- **G-435-H（本轮首选，新增）**：**陈旧 obj 导致"第 391 轮的 0x4D 镜像代码"直到第 434 轮全量重编才第一次真正进 exe**。
  支撑：第 434 轮护栏日志判 `REMOVED 621 object file(s) - upgrading this build to a FULL rebuild`（改 `src\train.h` 触发）；
  而 2026-11-04 第 391 轮同样改过 `train.h`（新增 `ArticGroupFlipped`），若当时**没有**全量（记忆 66636022 记录过依赖表 `#deps 0 (STALE)`），
  则 `newgrf_engine.cpp.obj` 一直是 11-04 之前的旧目标码 ⇒ 11-04~11-13 期间运行的其实是"只有 Flipped、没有 0x4D 镜像"的旧行为（玩家认为正确）；
  434 全量后镜像首次生效 ⇒ 视觉表现变为"玩家不认"。
  反证/待验：需确认 11-04 那次构建是否全量；以及 434 全量前后 `newgrf_engine.cpp.obj` 的 mtime 落差。
- **G-435-I（并列）**：提交点（`front->ConsistChanged(CCF_ARRANGE)` 或 depot 编辑后的 settle）把 `Flipped` 又清/翻回来 ⇒ 命令日志显示 flipped 变化，但提交后被归一。
  待验：全库搜 `Flipped` 的**写入**点（`Flip` / `SetFlipped` / `ClearFlipped` / `Reset`）。
- G-435-E（镜像+Flipped 互相抵消）：仍可能，但**只在"GRF 每节 sprite 对镜像+序号交换后恰好等价于原图"时**成立；与玩家"之前正常"的指认相比，优先级低于 G-435-H。

### 8.5 下一步取证清单

1. 全库 `Flipped` 写入点排查（G-435-I）。
2. `Train::GetImage()`（`train_cmd.cpp:1981-2001`）确认 Flipped→direction 的路径确实覆盖 depot 绘制。
3. 431~434 四轮 diff 逐条比对是否触碰 `flags` / `ConsistChanged` / 打散。
4. 若 G-435-H 成立，修法方向 = **让 0x4D 镜像与 Flipped 解耦**（或用 `ArticGroupFlipped` 明确区分"运营换端"与"段逻辑翻转"两种语义），使 Ctrl+左键回到"每节镜像 + 整组前后颠倒"。

## 九、根因确认（2026-11-13，第 435 轮续二）

### 9.1 G-435-H 已证伪，G-435-E 的具体形态已坐实

- `git log -S 'IsArticGroupFlipped' -- src/newgrf_engine.cpp` = **`22dc2b6833` 2026-11-04 16:59（rounds 386-391 checkpoint，含 "artic group flip"）**
  ⇒ 0x4D 组镜像代码是**第 391 轮（KI-558）**引入的。
- `build\R3R_fullbuild.done` = **2026-11-05 21:29**（改 `src\train.h` 后的全量）⇒ 该代码 **11-05 起就已真正进 exe**，
  故"434 轮全量才第一次编进去"（G-435-H）**不成立**。
- 431~434 四轮的 `git diff` 全部核对完毕，**只有三处非翻转改动**：
  `src\articulated_vehicles.cpp`（铰接部件 max_age，第 433 轮）、
  `src\train.h`（`Train::GetPower()` 的 `power_override==0 → 1` 地板，第 434 轮/KI-630）、
  `src\vehicle_cmd.cpp`（`SEG-POWER` 只读探针，第 434 轮）。
  其余 `train_cmd.cpp`(170) / `couple_group.cpp`(33) / `sl\vehicle_sl.cpp`(25) / `yapf_rail.cpp`(26) 均不触碰 `Flipped`/`ArticGroupFlipped`/`GetImage`/`ConsistChanged` 的翻转语义。
  ⇒ **没有"新代码把旧功能改坏"，是第 391 轮的镜像实现本身对"打散组"算错了。**

### 9.2 真根因（代码级，已定）

`src\newgrf_engine.cpp:880-912` 的 0x4D 分支：

```cpp
uint8_t artic_before = 0;
for (const Vehicle *u = v; u->IsArticGroupMember(); u = u->Previous()) artic_before++;
uint8_t artic_after = 0;
for (const Vehicle *u = v; u->HasArticulatedPart(); u = u->Next()) artic_after++;
...
if (t->HasDearticulatedGroupRole() && (t->IsSegmentFlipped() != t->IsArticGroupFlipped())) {
    const uint8_t tmp = artic_before; artic_before = artic_after; artic_after = tmp;
}
```

- 真铰接组：`artic_after` 数的是 `HasArticulatedPart()`（subtype 位）⇒ 正确得到 `n-1-rank`，swap 是完美镜像。
- **打散组（`ArticGroupHead`/`ArticGroupMember` 显式角色）**：`R3RDearticulateOneGroup()` 已把每节 `ClearArticulatedPart()`
  ⇒ `HasArticulatedPart()` **恒为 false** ⇒ `artic_after` **恒为 0**。
  于是 swap 变成 `(rank, 0) → (0, rank)`：**低字节（GRF 读的 position）全部归 0**，
  即整个组每一节都向 GRF 要"第 0 节（组头）"的脸。
- 后果：`Flipped` 的镜像仍然生效，但"整组前后颠倒"被抹平成"全部按第一节绘制"；
  对左右对称的中间车厢（以及同型多节动车组）而言，**视觉上就等于完全没变化** —— 与玩家"操作了几次都没变化"逐字吻合。
- 这也解释了 `ARTIC-FLIP n=3`（三节都翻到了）与"看不出变化"并存。

**口径回答（玩家问"是否引入了一个东西让前几天的代码失效"）**：
是的，但时间点不是第 434 轮，而是 **第 391 轮（2026-11-04，KI-558）"铰接组整体翻转"**：它新增的 0x4D 组镜像在真铰接上正确，
在"打散组"上把 rank 压成 0。11-05 全量后进入 exe；玩家"修复功率 0 之前"使用的若是更早的 exe（发布树/11-05 前的内测 exe），
看到的就是"只翻 Flipped"的老行为，所以主观上是"前几天还好好的"。

### 9.3 修法（本轮已实施）

只改 `src\newgrf_engine.cpp` 的打散组分支：swap 之前**把 `artic_after` 按组角色重算**为
"从 v 起到组尾（含 v）的节数 − 1"，使 swap 真正得到 `n-1-rank`：

- 3 节组 `[H, M, T]` 验证：H `before=0→2`、M `1→1`、T `2→0`（对称镜像 ✓），高字节互补值同时被修正（原来是恒 0 的错值）。
- 真铰接组（无显式角色位）走原分支，**逐字节不变**。
- 同一算法同时服务两条路径：Ctrl+左键（链序不变 ⇒ 镜像 = 前后颠倒）与段逻辑翻转（链序已反 ⇒ 镜像 = 还原 pre-flip 脸），二者需求一致。

### 9.4 待复测判据

1. 车库 Ctrl+左键一节铰接车：整组每节图像镜像 **且** 前后颠倒（车头外观移到另一端），再点一次完全复原。
2. `ARTIC-FLIP` 每次 `n=节数`、`flipped` 交替 1/0；存档往返后外观保持。
3. 负判据：普通单车/非铰接车厢 Ctrl+左键行为逐字不变。
4. 负判据：段逻辑翻转（`R3RReverseChainDirections`）后图像不出现"全组变成第一节"的塌缩。

### 9.5 实施与构建自证（2026-11-13 23:33）

- 改动：`src\newgrf_engine.cpp` 0x4D 分支内，`if (t->HasDearticulatedGroupRole() && (t->IsSegmentFlipped() != t->IsArticGroupFlipped()))` 块开头新增
  `sections_from_here` 计数（`for (u = v; u != nullptr; u = u->Next()) { if (u != v && !u->IsArticGroupMember()) break; ++n; }`），
  `artic_after = n - 1`，随后沿用原 swap。真铰接组不进该块。
- 构建：`R3R_fullbuild.cmd inc detach`；`build\R3R_incbuild.guard.log`=`GUARD: incremental is safe`；
  `[3/3] Linking CXX executable openttd.exe`；`build\R3R_incbuild.done`=`EXIT_CODE=0`（23:33:37）；错误计数 0；
  `src\newgrf_engine.cpp` 23:31:34 → `newgrf_engine.cpp.obj` 23:32:19 → `build\openttd.exe` 23:33:28（52 886 016 B）；`read_lints` 0 条。
- 未做：游戏内复测（需玩家操作）；发布树（`build-release`）未编（本轮属内测修复）。

## 十、GRF 回调口径核查（第 436 轮，2026-11-14）

### 10.1 玩家问题（原话）

> 我对此仍然有不满之处，我发现，对一些货真价实的铰接式（而非中国包那种铰接式空图像车厢凑长度的列车）执行这些操作，依然有问题。因为，图像都置flip flag并且跑到相反车厢去了（首跑到尾），但是似乎也就仅限于图像了，因为各种回调，尤其是荷兰列车包测试时候表现出来的列车长度，造成了图像错误。因此，我问你，对于所有的grf回调，position in articulated parts 跟 position in articulated parts from end 都互换了吗

### 10.2 代码事实（逐条，带行号）

| # | 事实 | 锚点 |
|---|---|---|
| F1 | 0x4D 返回 `artic_before \| artic_after << 8`：低字节 = 组内从前数秩，高字节 = 从后数秩（+1 已含自身时由 `HasArticulatedPart` 循环给出） | `src\newgrf_engine.cpp:880-929`，返回在 `:926` |
| F2 | 0x4D 的镜像（两字节互换）**只在** `HasDearticulatedGroupRole() && (IsSegmentFlipped() != IsArticGroupFlipped())` 成立时执行；**真铰接组（只有 subtype 位、无显式角色位）一字不动** | `src\newgrf_engine.cpp:903-924`，注释 `:891-893` 自陈"real artic group is an atomic block whose internal order never changes" |
| F3 | Ctrl+左键（force 换端）对真铰接组**先打散**：`R3RDearticulateOneGroup(group_head)` ⇒ 父车 `SetArticGroupHead()`、部件 `ClearArticulatedPart()` + `SetArticGroupMember()` ⇒ 之后 `HasDearticulatedGroupRole()` 成立，才走 F2 的镜像 | `src\train_cmd.cpp:5861-5866`（打散）+ `:5872-5877`（逐节 `Flip(Flipped)` + `FlipArticGroupFlipped()`）；`src\vehicle_cmd.cpp:549/556/562` |
| F4 | 0x40/0x41（`PositionHelper`，position/length in consist）的镜像条件**只有** `IsSegmentFlipped()`，**不含** `IsArticGroupFlipped()` | `src\newgrf_engine.cpp:511-515` |
| F5 | refit capacity 路径传 `whole_chain=true`：从物理链头数、不在段边界停、**且跳过镜像** ⇒ 0x40/0x41 是原生读数；但 0x4D 没有 `whole_chain` 概念，**同一回调内 0x4D 仍被镜像** | `src\newgrf_engine.cpp:462-465`、`:490`、`:511` 的 `!whole_chain` 短路；注释 `:432-438`（KI-540） |
| F6 | 长度不是变量而是回调结果：`Train::ConsistChanged()` 对每节调 `PROP_TRAIN_SHORTEN_FACTOR`（grf_version>=8，即 CB36）或 `CBID_VEHICLE_LENGTH`(0x11)，仅在 `CCF_ARRANGE`（含 `Length`）时写回 `cached_veh_length` | `src\train_cmd.cpp:527-541`；`CCF_ARRANGE` 定义 `src\train.h:70-84` |
| F7 | 打散**不改 `engine_type` 顺序**：`R3RDearticulateOneGroup` 只改 subtype/railveh_type/weight/power/speed override，每节仍是原来那节的引擎 | `src\vehicle_cmd.cpp:500-566` |
| F8 | 打散组的 0x4D 第 435 轮已修正为真镜像（`sections_from_here - 1`），3 节组 `[H,M,T]` ⇒ `0→2 / 1→1 / 2→0` | `src\newgrf_engine.cpp:904-923` |

### 10.3 结论：**没有全部互换**（回答玩家原问）

- **0x4D（position in articulated parts / from end）**：只对"有显式组角色位"的被打散组互换（F2+F3+F8）。真铰接组本身在打散**之前**求值时是不互换的；Ctrl+左键这条路径之所以看到"图像跑到对面"，正是因为 `:5861` 先打散、`:5875` 再打 `ArticGroupFlipped`，随后 `ConsistChanged(CCF_ARRANGE)` 里 sprite 重算 0x4D 拿到互换值。
- **0x40/0x41（position/length in consist）**：Ctrl+左键**不互换**（F4 只看 `IsSegmentFlipped()`），因为翻组不置段翻标志。而且它是"consist 位置"，物理链序确实没变——按语义它也不该随组翻互换。
- **长度（0x11 / CB36）**：长度是"每节按自己的引擎 + 回调结果"（F6+F7），**不是**"位置"的派生物。即使回调里读 0x4D 拿到互换值，`engine_type` 顺序没换 ⇒ 各节的长度属性/容量/载重缓存仍挂在原来的引擎上。反过来，若 GRF 的长度回调读 0x40/0x41（consist 位置），则完全拿不到互换值。
- 因此"位置互换了"与"长度跟着换"是两件事；荷兰包表现出的"仅图像、长度按原方向"与 F4/F7 完全吻合。

### 10.4 疑点（先登记，待实证）

- **REG-436-A**：荷兰列车包 0x11/CB36 里到底读哪个变量未知（0x4D / 0x40 / 0x41 / 只读属性 `PROP_TRAIN_SHORTEN_FACTOR` 静态值 / 用 `CBID_VEHICLE_ARTIC_ENGINE` 定义各节）。这决定修法方向：
  - 若读 0x4D ⇒ 理论上第 435 轮后长度应已跟着换；现状不符 ⇒ 需另找原因（如缓存/`CCF_ARRANGE` 未跑到）。
  - 若读 0x40/0x41 ⇒ 属 F4 缺口。
  - 若是静态属性（各节引擎长度固定）⇒ 属 F7，**任何变量镜像都救不了**，只能物理换引擎顺序。
- **REG-436-B**：refit capacity 回调里 0x4D 被镜像、0x40/0x41 不镜像（F5）⇒ 同一回调内两个位置变量口径相反。是否已被中国包/其它 GRF 踩到未知；若踩到，表现为"容量按镜像位置算但节数按物理位置算"。
- **REG-436-C**：真铰接组在**没有**被 R3R 打散的路径上（整链逻辑翻转 `R3RFlipChainBySegments` 的 atomic 块、物理换端 `ReverseTrainSwapVehicles`）是否真的"组内顺序从不改变"。若某条路径重排了组内链序，则 F2 的不镜像是对的（链序已变、0x4D 自然变）；若链序没变而方向变了，则 0x4D 会停在旧值 ⇒ 漏镜像。

### 10.5 修法候选（未实施，待玩家拍板）

- **方案 A（治本，推荐）**：Ctrl+左键对真铰接组改为"打散 + **物理反转组内链序** + 逐节 `Flipped`"，并撤销 `ArticGroupFlipped` 的 0x4D 镜像补偿。这样 `engine_type` 顺序、`cached_veh_length`、0x4D、sprite 四者同向变化，**长度自然跟随位置**，对"静态属性给长度"的 GRF 也成立。代价：车辆 ID 在链上顺序改变（货物/车号仍跟车走）、需重算段边界与 `★/⊗`、需 `ConsistChanged(CCF_ARRANGE)` 重算全部缓存。
- **方案 B（治标，最小）**：把 0x40/0x41 的镜像条件扩为 `IsSegmentFlipped() \|\| IsArticGroupFlipped()`（真铰接组在打散后也置组翻位）。只对"长度由 0x40/0x41 决定"的 GRF 有效；对"静态属性给长度"的 GRF 无效；且会把 consist 位置语义弄歪（翻组并未改变它在 consist 中的位置）。
- **方案 C**：保留真铰接结构、改用记忆 65693747 的"假爸爸身份"（不打散，只迁组头角色 + 镜像）。改动面最大，且 `HasArticulatedPart()` 的链序假设仍需验证。

### 10.6 待玩家确认（编号回填，避免重问）

- **C-1**：荷兰包该车的 `dump_vehicle`（debug 版）里 `st:` 是否含 `A`、`tf:` 是否含 `H`/`M`？（区分"真铰接组""打散组"）
- **C-2**：该车 Ctrl+左键后，是"整组每节图像前后颠倒"（首图到尾部）还是"只左右镜像"？长度是"每节都按原节不变"还是"变了一部分"？
- **C-3**：该 GRF 的 0x11 回调是否读 0x4D / 0x40 / 0x41？（可用 GRF 调试窗口看变量，或提供 GRF 名称+版本以便按已知实现推断）

## 十一、方案 A 拍板（第 436 轮，2026-11-14）

### 11.1 玩家决策（原话）

> A感觉可以，就按A修吧

⇒ 采纳 §10.5 方案 A：Ctrl+左键对**真铰接组**改为「打散 + 物理反转组内链序 + 逐节换向」，让 `engine_type` 顺序、`cached_veh_length`、0x4D、sprite 四者同向变化，**长度自然跟随位置**；不再依赖 `ArticGroupFlipped` 的 0x4D 补偿镜像来"假装"位置换了。

### 11.2 设计要点（待代码落实，先落盘）

- 反转表达式的选择：现状走的是"原生车库换端口径"（链序不变 + `Flip(Flipped)` + 0x4D 镜像补偿）；方案 A 改走"R3R 逻辑翻口径"（**组内链序倒序 + 每节 direction 反转**），因为只有后者能让"哪一节在第 k 位"这件事对所有 GRF 变量与引擎侧长度同时成立。
- `Flipped`：**不再对每节 `Flip(Flipped)`**（链序已倒序 + direction 已反，再翻 Flipped 是重复镜像）；车原本的 `Flipped` 值原样保留。
- 组角色位：打散后 `ArticGroupHead` 必须随链头搬到新的组首（原组尾），`ArticGroupMember` 全组保持；0x4D 靠链序自然得到 `n-1-rank`，**不需要** XOR 镜像 ⇒ 该路径应清除 `ArticGroupFlipped`。
- 段边界：组内不能出现 ★/⊗ 中间态；若组跨段或组内已有 ★，先拒绝或先规范化（待读代码确认）。
- 位置：逻辑翻不动 x_pos/y_pos（与 R3R 既有"逻辑翻位置不动"铁律一致），但**必须**恢复物理间距的单调性（组内倒序后相邻节仍按原像素相接）；方向反转后 `UpdateDeltaXY` 的 bounds 重算，需 `ConsistChanged(CCF_ARRANGE)`。

### 11.3 待验证 / 风险（REG-436-D）

- REG-436-D-1：组内倒序后相邻节的 `x_pos/y_pos` 是否需要重算——R3R 既有 `R3RFlipChainBySegments` 在段内做"倒序重链"时有成熟做法，应复用而非新写。
- REG-436-D-2：`ArticGroupFlipped` 在**段逻辑翻转**路径（`R3RFlipChainBySegments`）里仍被使用，不能全局删除；方案 A 只针对 Ctrl+左键这条路径取消补偿。若要彻底简化，需另立一轮核对段翻路径的 0x4D 语义。
- REG-436-D-3：真铰接组被打散后**不再能恢复**为真 artic（`ClearArticulatedPart()` 不可逆），已有行为，方案 A 不改这一点；但打散与链序倒序的顺序必须先打散再倒序（否则 `SetNext` 会穿过 artic 部件）。
- REG-436-D-4：`CBID_VEHICLE_ARTIC_ENGINE` 依赖真 artic 结构，打散后不再被调用；若某 GRF 靠它定义各节长度，方案 A 也无法恢复该回调 —— 但引擎顺序已随位置改变，长度至少与位置一致（这正是本方案相对现状的改进）。

## 十二、方案 A 落地（第 436 轮，2026-11-14，已实现 + 已编译）

### 12.1 代码落点（仅 `src\train_cmd.cpp`，未碰任何 `src\*.h`）

`CmdReverseTrainDirection` 的 `reverse_single_veh` 分支（`:5843-5992`）替换原来的"纯图像镜像"块：

1. `:5855-5862` 组头定位与两个判据（`is_group`、`real_artic`）保持不变，第 394 轮 KI-565 的 `IsArticGroupMember()` 自检修复保留。
2. `:5868-5873` 只有**真铰接组**才 `R3RDearticulateOneGroup(group_head)` —— 刻意不用 `DearticulateChainWithSnapshot()`，因为它会把整条链上其它铰接组一起打散；本次只允许这一个组被打散（REG-436-D-3 的顺序要求：先打散、后倒序，否则 `SetNext` 会穿过 artic 部件）。
3. `:5884-5892` 收集组内成员（沿 `Next` 走到下一个非 `IsArticGroupMember()` 为止），同时检测"★ 落在组中间"。
4. `:5894-5904` 兜底：`group.size() < 2` 或组内嵌 ★ ⇒ 退回原纯图像路径（`Flip(Flipped)` + `FlipArticGroupFlipped()`），并打 `ARTIC-FLIP-FALLBACK head= n= real_artic= seg_inside=`，使"走了哪条路"可从日志自证。
5. 主路径（`:5905-5985`）：
   - `:5916-5923` 逐节 `direction = ReverseDir(...)`；`Flipped` **不动**（链序已倒 + 方向已反，再翻等于抵消），并按 `IsSegmentFlipped()` 对齐 `ArticGroupFlipped`（0x4D 靠链序自然得到 `n-1-rank`，不再做补偿镜像）。
   - `:5926-5936` **逆序重链**：`prev->SetNext(w)`，`x_pos/y_pos` 完全不动；`before==nullptr` 时先 `w->Previous()->SetNext(nullptr)` 断旧链头。
   - `:5939-5945` 全清 `ArticGroupHead/Member` 后按新序重设：`old_tail`（新组首）为 `Head`，其余为 `Member`。
   - `:5948-5951` ★/⊗ 随新节移动（原 `head_was_front`/`tail_was_back` 各搬到对面）。
   - `:5954` `R3RMoveSegmentOwner(old_head, old_tail)`（段拥有者跟新段头）。
   - `:5958-5975` 组覆盖链头时迁身份：`IsFrontEngine()` 走 `R3RRelocateFrontIdentity(old_head, old_tail, head_was_front)`（第三参避免二次搬运、把刚迁好的身份清掉）；纯车厢/假引擎头走 `unitnumber`/`name`/`group_id` 及各自 backup 的三对 `std::swap`，并 `front = old_tail->First()`。
   - `:5977-5979` 逐节 `UpdateDeltaXY()` + `InvalidateNewGRFCacheOfChain()` + `InvalidateImageCacheOfChain()`。
   - `:5981-5984` 探针 `ARTIC-FLIP head= n= real_artic= old= new= chain_head=`。
6. `:5988` 出口统一 `front->ConsistChanged(CCF_ARRANGE)`（长度回写在 `src\train_cmd.cpp:527-541`），随后三个窗口 `SetWindowDirty` 不变。

### 12.2 与 §11.2 设计要点的偏差核对

- "若组跨段或组内已有 ★，先拒绝" ⇒ 实现取"退回旧路径"而非报错（用户操作不应失败）。
- "必须恢复物理间距的单调性" ⇒ 实现沿用 R3R 铁律「逻辑翻不动位置」，与 §11.3 REG-436-D-1 记录的残余风险一致：短节（<8/8）因 `half_shorten` 锚点语义，画面相对 `x_pos` 镜像，可能出现 1~几像素接缝偏差。这与 `R3RFlipChainBySegments` 现有口径同源，不是本方案新引入。
- `Flipped` 语义：不翻是对的。`Train::GetImage()` 在 `Flipped` 为真时会把 direction 再反一次；本路径已直接反 `direction`，再翻 `Flipped` 会抵消视觉翻转。`R3RReverseChainDirections` 之所以同时翻（KI-214 注释），是因为那条路径要求"外观不变、只改逻辑"，与本路径目的相反。

### 12.3 构建自证（2026-11-14）

- 入口：既有 `R3R_fullbuild.cmd inc detach`（未新建任何 `.cmd`）；构建前 `tasklist /fi "imagename eq openttd.exe"` 为空 ⇒ 无占用。
- 护栏 `build\R3R_incbuild.guard.log` = `GUARD: incremental is safe (no header/lang file is newer than the newest object)` ⇒ 增量合法（本轮只改 `.cpp`）。
- 日志尾部：`[2/3] Building CXX object CMakeFiles\openttd_lib.dir\src\train_cmd.cpp.obj` → `[3/3] Linking CXX executable openttd.exe`，`MODE=inc EXIT_CODE=0`；`error C*` / `fatal error` / `FAILED:` / `build stopped` 计数 **0**。
- 时间戳链：`src\train_cmd.cpp` 00:36:38 → `train_cmd.cpp.obj` 00:45:28 → `build\openttd.exe` 00:46:48（52 886 016 B）；`build\R3R_incbuild.done` 00:46:53 内容 `EXIT_CODE=0`。
- `read_lints` `src\train_cmd.cpp`：0 条。

### 12.4 复测判据（游戏内，待做）

1. 真铰接组（`dump_vehicle` 的 `st:` 含 `A`）Ctrl+左键后：图像前后颠倒**且**各节在 consist 里的位置真的换了（`ARTIC-FLIP ... real_artic=1 old=<原组头> new=<原组尾>`）。
2. 荷兰包/依赖静态长度的 GRF：翻转后不再出现"图像与长度互相矛盾"；长度至少与位置一致（`cached_veh_length` 随 `engine_type` 顺序变）。
3. `ARTIC-FLIP-FALLBACK` 不应在普通单组、无 ★ 的链上出现；出现即需查 `seg_inside`。
4. 组覆盖链头时（`head_was_front=1`）：车号/名称/分组/排程都跟到新链头，界面上不出现"编号或名字留在旧节"。
5. 负判据：`group.size()<2` 的单节、以及组内含 ★ 的链，行为与第 435 轮逐字相同（走 `ARTIC-FLIP-FALLBACK`）。
6. 存档往返后 `ArticGroupHead/Member` 分布仍与链序一致（读档不重新发明角色位）。

## 十三、第 437 轮：实机崩溃 `vehicle_gui.cpp:4346` 断言（2026-11-14，方案 A 漏项）

### 13.1 现场

- 玩家原话：「你的改动应该漏掉了关键的一环：vehicle_gui.cpp 4346行断言崩溃了。」
- 崩溃点原文（`src\vehicle_gui.cpp:4341-4347`）：
  ```cpp
  void StartStopVehicle(const Vehicle *v, bool texteffect)
  {
      /* R3R: car-only consists (free-wagon chains, no engine) cannot be started/stopped by themselves. */
      if (v->type == VehicleType::Train && Train::From(v)->IsFreeWagon()) return;   // :4344
      assert(v->IsPrimaryVehicle());                                               // :4346  ← 炸在这
      Command<Commands::StartStopVehicle>::Post(...);
  }
  ```

### 13.2 代码事实（逐条核对过，不是推断）

- F1 `Train::IsPrimaryVehicle()` = `IsFrontEngine() || IsFrontWagon()`（`src\train.h`）。**不含** `IsFreeWagon()`。
- F2 `StartStopVehicle` 唯一的 R3R 守卫是 `IsFreeWagon()`（`:4344`），所以「既非 FrontEngine 也非 FrontWagon、也不是 FreeWagon」的车会毫无阻挡地走到 `:4346` 断言。这正是**链内普通车**的形态。
- F3 `NormaliseSubtypes(chain)`（`src\train_cmd.cpp:3682-3694`）只给链头两种结局：`IsWagon()` ⇒ `SetFreeWagon()`；否则 `assert(IsEngine())` ⇒ `SetFrontEngine()`。**它给不出 FrontWagon**，所以「纯车厢成为链头」= FreeWagon = 非 primary。
- F4 `R3RRelocateFrontIdentity(from,to)`（`:11854`）入口断言 `from->IsFrontEngine()`；中途 `NormaliseSubtypes(to)`（`:11998`）按 F3 给 `to` 上身；末尾只搬 orders/号/名/借用/优先级，**不搬 subtype 的 engine/front 语义**。

### 13.3 根因（方案 A 的漏项：只搬"身份"，没搬"谁是列车"）

方案 A 重链后链头从 `old_head` 变成 `old_tail`，我的代码（`:5958-5975`）只处理了两条分支：

1. `old_head->IsFrontEngine()` ⇒ `R3RRelocateFrontIdentity(old_head, old_tail, head_was_front)`。
2. 否则 ⇒ swap `unitnumber/name/group_id`（纯车厢/假引擎头）。

**漏掉的是 subtype 层的 front 位移交**，后果分两种：

- 情形甲（真铰接组的尾巴是车厢/artic part）：`old_head` 是引擎 ⇒ 走 1，`Relocate` 里的 `NormaliseSubtypes(old_tail)` 按 F3 发现 `old_tail->IsWagon()` ⇒ **`SetFreeWagon()`**。于是翻转后：`old_head` 被清 front 位（普通链内引擎/车厢）、`old_tail` 是 FreeWagon —— **整条链没有任何 `IsPrimaryVehicle()` 的车**。列车从车辆列表里消失，任何残留引用（窗口、跟随、列表选中行）传进 `StartStopVehicle` 就撞 `:4346`。
- 情形乙（组头是假引擎、尾巴是普通车厢）：`old_head->IsFrontEngine()` 为真（假引擎带 FrontEngine），同样走 1、同样落 `SetFreeWagon(old_tail)`。且 `old_head` 的**假引擎位（`SetEngine`）也没被撤销**，会在链中留下一节"不是链头却仍是引擎"的畸形车 —— 这是 `NormaliseSubtypes` 只清 front 位、不清 engine 位造成的。

⇒ 崩溃对象几乎必然是**原链头 `old_head`**：翻转后它是普通链内车（既非 FrontEngine 也非 FrontWagon、也不是 FreeWagon），UI 那一侧还留着它的引用（`Relocate` 只关了 `from` 的 7 类窗口，**没有关车辆列表/GUI 选中项/队伍列表缓存**，也没有把新链头登记成 primary），点"停止/启动"即断言。

### 13.4 漏项的完整清单（"关键的一环"到底是什么）

方案 A 只做了三件事：① 重链；② 迁 `ArticGroupHead/Member` 角色；③ 迁列车身份字段（号/名/组/排程）。**缺的是 R3R「段/列车前端」语义层**：

1. 新链头必须获得 **FrontEngine**（纯车厢则必须先 `SetEngine()+ClearWagon()` 成为假引擎头，即 `R3RCreateCarOnlyFormation(old_tail)`；真引擎则 `SetFrontEngine()`）。
2. 旧链头若原本是假引擎头，必须 `R3RDestroyCarOnlyFormation(old_head)`（`ClearFrontEngine()+ClearEngine()+SetWagon()+SetFreeWagon()+关窗口`），否则链中残留假引擎。
3. 两者顺序受 `R3RRelocateFrontIdentity` 的 `assert(from->IsFrontEngine())` 约束：**必须先 Relocate（from 还是 FrontEngine 时）→ 再 Destroy 旧头 → 再 Create 新头**；反过来会先炸 Relocate 的断言。
4. 迁完之后 `front->ConsistChanged(CCF_ARRANGE)` 才会按新链头重算长度/功率（KI-632 的正题）。

### 13.5 修法（待玩家拍板，未落地）

- 修法 A（推荐，改动最小、与既有原语一致）：
  在 `:5975` 的 `front = old_tail->First();` 之前插入：
  ```cpp
  if (!old_tail->IsFrontEngine()) {
      R3RCreateCarOnlyFormation(old_tail);   // 已 SetFrontEngine + ConsistChanged
  }
  ```
  并在 Relocate 之后、Create 之前，对 `old_head` 补一次「撤销假引擎」：仅当 `R3RIsCarOnlyFormation(old_head)` 时调 `R3RDestroyCarOnlyFormation(old_head)`。
  风险点：`R3RCreateCarOnlyFormation` 会自己跑一次 `ConsistChanged(CCF_ARRANGE)`，与出口那次重复（幂等，仅多一次重算）；`R3RDestroyCarOnlyFormation` 会 `CloseWindowById` 五类车辆窗口（本来就该关，因为该车已不是前端）。
- 修法 B（保守）：整块拒绝——`old_tail` 不是引擎时直接退回第 435 轮的纯图像路径（`ARTIC-FLIP-FALLBACK ... seg_inside=0 reason=tail-not-engine`），保证零新崩溃面，代价是"尾巴是车厢的真铰接组"永远得不到长度修正（KI-632 只修一半）。
- 两条修法都必须补一条**统一出口不变量**：`EDGE: assert(front->IsPrimaryVehicle())`（或 `if (!front->IsPrimaryVehicle()) front = ...;` 的兜底赋值），放在 `ConsistChanged(CCF_ARRANGE)` 之前，把"链头不是 primary"从偶发 UI 崩溃变成当场可定位的日志/断言。

### 13.6 复测判据（第 437 轮）

1. 对"含真铰接、尾巴是车厢"的组 Ctrl+左键后：不崩，且 `dump_vehicle` 里新链头 `tf:` 含 `F`（FrontEngine）、`st:` 含 `A`（若它是 artic part 则仍是 A；若是普通车厢则应被打成假引擎头）。
2. 车辆列表里该列车仍以**一行**出现（primary 仍在），没有出现"列车消失"或"多出一个车底"。
3. 点该列的"停止/启动"不崩；`StartStopVehicle` 之后 `vehstatus` 真的翻转。
4. 对纯车厢链 / 假引擎头车底组 Ctrl+左键：`old_head` 的 `tf:` 里 `F` 被清、`st:` 不再含 `E`（假引擎位撤销），新链头 `F`/`E` 齐全。
5. 负判据：单节组、组内含 ★ 的链仍走 `ARTIC-FLIP-FALLBACK`，不出现新的 front 位改动。
6. `LOG`：`ARTIC-FLIP` 行建议追加 `tail_eng=%d`（新链头是否本是引擎）与 `front_fixed=%d`（是否补了假引擎），便于区分情形甲/乙。

### 13.7 玩家定位（2026-11-14，原话）

> 「我想想，这应该是因为你铰接式的头忘记转移了对吗，或者说你链头忘记转移了」

判定：**后半句成立，前半句只对一半**。精确化如下（两层要分开看）：

1. **组角色位（`ArticGroupHead` / `ArticGroupMember`）—— 搬了**。`:5939-5945` 全清后按新序重设，`old_tail` 成为 `ArticGroupHead`，其余为 `Member`。所以"铰接式的头"这一层不是漏项。
2. **链头身份（`GVSF_FRONT`，即 `IsPrimaryVehicle()`）—— 漏了**。这才是崩因。而且不是"忘了一行代码"那么直白：我是把它**委托**给了 `R3RRelocateFrontIdentity` 内部的 `NormaliseSubtypes(to)`（`:11998`），而 `NormaliseSubtypes` 对链头只给两个结局（`:3683-3688`）：
   - `IsWagon()` ⇒ **`SetFreeWagon()`**（这是车厢做头时唯一能拿到的位，`IsPrimaryVehicle()` 不认它）
   - 否则 ⇒ `assert(IsEngine())` ⇒ `SetFrontEngine()`

   铰接组打散后 `old_tail` 是普通车厢（`IsWagon()` 为真），于是拿到 `FreeWagon`；同时 `old_head`（现在是链内车）被循环 `ClearFrontEngine()`。**整链因此没有任何 primary 车**：原链头降级、新链头也没升级。

3. 为什么这恰好炸在 `vehicle_gui.cpp:4346`：`IsPrimaryVehicle()` 在 OpenTTD 里只认 `FrontEngine || FrontWagon`，而 `StartStopVehicle` 的 R3R 守卫只挡 `IsFreeWagon()`（`:4344`）——**降级后的原链头既不是 FreeWagon 也不是任何 front 位**，正中守卫盲区；UI 那侧还留着对它的引用（`R3RRelocateFrontIdentity` 只关了 `from` 的 7 类窗口，没关车辆列表选中行/跟随），点"停止/启动"即断言。

4. 反证：第 435 轮的纯图像路径不崩，正因为它**不重链、链头不变**——链头身份从未需要转移。所以这条崩溃是方案 A 专有的，且"链头身份"是重链时必须同步搬的第四样东西（前三样：链序、组角色、列车身份字段）。

⇒ 修法仍是 §13.5 的修法 A：`R3RRelocateFrontIdentity` 之后补 `if (!old_tail->IsFrontEngine()) R3RCreateCarOnlyFormation(old_tail);`，并对 `old_head` 补 `R3RDestroyCarOnlyFormation()` 撤销假引擎位。语义上等于把"链头身份"这一环显式搬过去，而不是交给 `NormaliseSubtypes` 猜。

### 13.8 落地（第 437 轮，2026-11-14，已实现 + 已编译）

玩家拍板：**按修法 A 补**（原话「那就按这个补来看看吧」）。

改动只落在 `src\train_cmd.cpp` 的 `CmdReverseTrainDirection` 组翻转分支（原 `:5956-5984`），三件事：

1. **前置采样**（在任何改写之前，因为 Relocate 会清掉判据本身）：
   ```cpp
   const bool head_was_front_engine = old_head->IsFrontEngine();
   const bool head_was_caronly      = R3RIsCarOnlyFormation(old_head);
   ```
2. **subtype 层身份收尾**（Relocate/swap 之后）：
   - `head_was_front_engine` 为真（原链头是真引擎或假引擎头，即"这本来就是列车"）：
     - `head_was_caronly` ⇒ 手动撤旧头假引擎位：`ClearFrontEngine + ClearEngine + SetWagon + ClearFreeWagon`。**刻意不调 `R3RDestroyCarOnlyFormation()`** —— 它还会从 `old_head` 往后清 `SegmentBack`，而第 4 步刚把 ⊗ 布好，会误清。
     - 新链头 `!IsFrontEngine()` 时：`IsWagon()` ⇒ `R3RCreateCarOnlyFormation(old_tail)`（假引擎头 + `ConsistChanged`）；否则（双头后半等）⇒ `SetFrontEngine()`。
   - `head_was_front_engine` 为假（原链头就是 FreeWagon，即"不成段车厢链"）：`old_tail` 若 `IsWagon()` ⇒ 把 `FreeWagon` 位从旧头搬到新头。**这种链故意保持无 primary**——`:4344` 的 `IsFreeWagon()` 守卫本来就挡它，语义正确。
3. **诊断**：`ARTIC-FLIP` 行追加 `primary=%d`；新增 `ARTIC-FLIP-NOFRONT head= old= new= wagon= engine= front=`（只在"本来该有 primary 却没有"时打）。**没有加 `assert(front->IsPrimaryVehicle())`**——那会把偶发问题变成第二次崩溃，改用日志暴露。

编译自证：`R3R_fullbuild.cmd inc detach`；done `EXIT_CODE=0`（01:07）；`[3/3] Linking CXX executable openttd.exe`；`src\train_cmd.cpp` 01:04 → `build\openttd.exe` 01:07（52 892 160 B）；`read_lints` 0 条。

### 13.9 第 437 轮修复**未生效**（2026-11-14 玩家复测）

玩家原话：「还是一样的错误。」

现场核查：`R3R_fullbuild.cmd` 于 01:07 产出 EXIT_CODE=0，`build\openttd.exe` 52 892 160 B。但 `d:\sourcecode of JGRPP\R3R_debug.log`（63.5 KB）里 **`ARTIC-FLIP` 零命中**，且内容是 `LOADCENSUS total=4` + 两辆车（veh=17 / veh=20）的 `SKIP-STOPPED` 刷屏 ⇒ 该日志**不是**本次铰接复测会话（本次会话必然打 `ARTIC-FLIP` 或 `ARTIC-FLIP-FALLBACK`）。

⇒ 疑点 **REG-437-A**：崩溃现场缺失。三种可能未区分：
1. 玩家还在跑旧 exe（01:07 之前的版本）；
2. 崩溃发生在**进入 `CmdReverseTrainDirection` 之前**（例如 UI 断言在按键那一刻就炸，命令根本没执行）；
3. 崩溃点已不是 `vehicle_gui.cpp:4346`，而是另一处（同一句"还是一样的错误"可能指同一类现象、不同断言）。

**必须取到的数据**：①crash 的 `crash-*.log`（根目录 `search_file` 当前 0 个 ⇒ 断言弹窗未落盘，需要玩家抄 Assertion 文本或截图）；②若弹窗是 `Assertion failed` 形式的 `FatalError`，抄 `file:line` 与表达式；③新会话的 `R3R_debug.log` 尾部。

---

## 十四、第 438 轮：修法对照（玩家三问，2026-11-14）

玩家原话：

> 还是一样的错误。我得问你了，你的修法是不是这样的：
> 对真铰接，就打散，假铰接不动
> 铰接组内物理反转车厢顺序，同时每节车厢置flip flag
> 铰接的名义父车转移到现在的铰接式最前面

### 14.1 逐条对照（代码事实）

| 玩家表述 | 实际实现 | 判定 |
|---|---|---|
| ① 对真铰接就打散，假铰接不动 | `:5868-5873` `if (real_artic) R3RDearticulateOneGroup(group_head);`，判据 `:5862` `group_head->Next()->IsArticulatedPart()` | **一致**。且刻意只打散**本组**（不用 `DearticulateChainWithSnapshot`，否则会打散全链其它组） |
| ② 组内物理反转车厢顺序 + 每节置 flip flag | `:5925-5936` 反转链序（`SetNext` 重挂，位置不动）；但**没有置 `Flipped`**，而是 `:5916-5917` `w->direction = ReverseDir(w->direction)` + `:5922` 按 `IsSegmentFlipped()` 同步 `ArticGroupFlipped`。注释 `:5913-5915` 明写「Flipped 位故意不动，否则镜像会抵消视觉翻转」 | **不一致（关键偏差）** |
| ③ 名义父车转移到现在的组首 | `:5939-5945` 全清 `ArticGroupHead/Member` 后按新序重设，`old_tail`（原组尾）成为新 `ArticGroupHead` | **一致** |

### 14.2 偏差②为什么可疑（猜测根因 R-2，待验证）

我在**第 436 轮**把这一步从"纯视觉 `flags.Flip(Flipped)`"（第 435 轮的方案）改成了"物理 `direction` 反转"。理由是希望 `cached_veh_length` / NewGRF 变量 0x4D / 贴图一起跟着动。

但 `direction = ReverseDir(direction)` 是**改物理朝向而 `x_pos/y_pos` 不动**。整链换向（`ReverseTrainSwapVehicles`）之所以自洽，是因为它**同时交换位置**。只反转组内顺序 + 反转每节朝向、位置全不动 ⇒ 组内变成"沿链方向镜像但位置没镜像" ⇒ 相邻车的接面与 `bounds` 全部错位，`UpdateDeltaXY()`（`:6035`）随后按新 direction 重算包围盒 ⇒ 图像错位/重叠，且相邻车朝向相反却被判为同一列。

玩家模型里的"置 flip flag"（`VehicleRailFlag::Flipped`）是**纯渲染镜像、不动 direction、不动位置**，几何上永远安全。**两种做法不等价**，这是本轮必须优先分辨的分叉点。

### 14.3 未确认项

- **U-1**：本次崩溃的断言位置（是否仍是 `vehicle_gui.cpp:4346` / `IsPrimaryVehicle()`）。
- **U-2**：玩家说的"一样的错误"是指**崩溃**还是**图像/行为错误**（两者修法完全不同，前者查 primary，后者查 14.2）。
- **U-3**：`R3RRelocateFrontIdentity` 路径本身有没有在 `old_head` 上留下非法中间态（它内部会 `NormaliseSubtypes(to)` 先写一次 `FreeWagon`，随后我才 `R3RCreateCarOnlyFormation` 覆盖）。

---

## 十五、第 438 轮：根因定位（玩家给出模型与现场，2026-11-14）

### 15.1 玩家的模型与原话

原话：「前-铰接1-铰接2-铰接3-后。那么现在操作之后，是这样的 前-铰接3-铰接2-铰接1-后。但是，问题在于，铰接式的父车应当永远在铰接1，`position_in_articulated_parts` 的值从小到大应当永远是铰接1往铰接3的方向，可是它的 primary 是会变的。」

现场三条：
1. 图像：原本「前-1-2-3-后」操作后变成「前-[一节]-后」；
2. debug 面板：原本 `idx 0→2`，操作后 `idx 2→0`；
3. 点「列车停止红叉」崩溃，断言与上次**完全相同**（`vehicle_gui.cpp:4346` `assert(IsPrimaryVehicle())`）。

### 15.2 引擎事实：物理换向**从不重链**

```5116:5147:src/train_cmd.cpp
static void ReverseTrainSwapVeh(Train *v, int l, int r)
{
	for (a = v; l != 0; l--) a = a->Next();   // 按链位取 a
	for (b = v; r != 0; r--) b = b->Next();   // 按链位取 b
	if (a != b) {
		std::swap(a->track, b->track);
		std::swap(a->direction, b->direction);
		std::swap(a->x_pos, b->x_pos);        // ← 位置交换
		std::swap(a->y_pos, b->y_pos);
		std::swap(a->tile,  b->tile);
		std::swap(a->z_pos, b->z_pos);
	}
}
```

```5160:5172:src/train_cmd.cpp
void ReverseTrainSwapVehicles(Train *v)
{
	int r = CountVehiclesInChain(v) - 1;
	int l = 0;
	do { ReverseTrainSwapVeh(v, l++, r--); } while (l <= r);   // 首尾配对交换数据
	for (Train *u = v; u != nullptr; u = u->Next()) UpdateStatusAfterSwap(u);
}
```

注释 `:5173-5175` 自己写明：「ReverseTrainSwapVeh **只换数据不重链**，故 v 仍是链头」。

⇒ **引擎的物理掉头 = 链位不变、把位置/朝向数据首尾对调**。实体池 index 不变、链序不变、subtype 不变、`position_in_articulated_parts` 不变、链头不变。它会自然满足玩家的全部三条期望（父车永远在铰接1 / idx 恒 0→2 / primary 不变）。

### 15.3 我的实现错在哪（三方对照）

第 436 轮我做的是「重挂 `SetNext` 反转链序 + 每节 `direction = ReverseDir()` + 位置 `x_pos/y_pos` 刻意不动」（`:5913-5936`），即**把引擎物理换向的两个要素各取反了一半**：

| 要素 | 引擎正确做法 | 第 436 轮实现 | 后果 |
|---|---|---|---|
| 链序 | **不动** | 反转 | `position_in_articulated_parts` 跟着反转 ⇒ 玩家看到的 `idx 2→0`（**必然，不是独立 bug**）；父车被迫从 铰接1 迁到 铰接3（`:5944-5945`） |
| 位置 `x_pos/y_pos` | **首尾对调** | 不动 | 三节车的世界坐标全叠在原处、朝向却两两相反 ⇒ 图像呈「前-[一节]-后」（**必然**） |
| `direction` | 跟着位置数据一起 swap | 单独 `ReverseDir` | 朝向与位置矛盾；`:6035` `UpdateDeltaXY()` 按新 direction 重算包围盒 ⇒ 接面错位 |
| 链头/primary | **不动** | `R3RRelocateFrontIdentity` 迁到 old_tail（`:5969`） | UI 仍持旧链头引用 ⇒ 点红叉走 `StartStopVehicle()` ⇒ `assert(IsPrimaryVehicle())` 在 `:4346` 炸（**必然**） |

⇒ 三条现场症状是**同一个错误的三个投影**，不是三个 bug。

### 15.4 玩家模型需要修正的一点

玩家第 ② 条「组内物理反转车厢顺序 + 每节置 flip flag」中，**「反转车厢顺序」这一半不需要做，也不能做**：物理掉头在引擎里靠交换位置数据实现，链序一经反转就与空间顺序去同步（铰接1 空间上在前、链上却在尾），这是无法用 `flip flag` 补偿的。`flip flag`（`VehicleRailFlag::Flipped`）是纯渲染镜像，不能修改链序带来的语义错位。

同理第 ① 条「真铰接就打散」在正确做法下**也不再必要**：链序与 subtype 都不动，part 仍挂在原父车后面，`IsArticulatedPart()` 关系天然合法。

### 15.5 拟采用的修法（待玩家拍板，方案 B）

组翻转分支改为：**只在组内做首尾配对的数据交换，不重链、不打散、不迁 primary**。

- 组在链中的绝对链位区间 `[start, end]`（`start = CountVehiclesInChain(front)` 到 `group_head` 的距离，`end = start + group.size() - 1`）；
- `for (l = start, r = end; l < r; ++l, --r) ReverseTrainSwapVeh(front, l, r);`
- 之后对组内每节 `UpdateStatusAfterSwap(w)`；
- **删除**：`:5868-5873` 打散、`:5925-5936` 重链、`:5938-5945` 组角色迁移、`:5947-5954` 段标记迁移、`:5956-6033` 身份迁移整块；
- `Flipped` 位保持不动（`UpdateStatusAfterSwap` 负责派生）。

预期：玩家三条期望全部满足，且 `:4346` 断言不可能再触发（没有任何 primary 迁移）。

### 15.6 待确认

- **V-1**：`ReverseTrainSwapVeh` 是 `static`（`:5116`）。`CmdReverseTrainDirection` 在同文件（`:5812`），可直接调用，无需改可见性。
- **V-2**：真 artic（`subtype` 位）组换位后，`cached_veh_length` 是否需要额外 `ConsistChanged` —— 分支末尾 `:6046` 已有 `front->ConsistChanged(CCF_ARRANGE)`，覆盖。
- **V-3**：玩家描述「图像反向，因为图形对称所以我没看出来是谁」⇒ 与「三节叠成一节」并存，说明位置全叠 + 朝向镜像是同时发生的，与 15.3 表格一致。

### 15.7 澄清：`ReverseTrainSwapVeh` 到底改什么、不改什么

玩家问：「铰接1 和铰接3 应当**从不改变位置**，只是交换必要的回调数据吗？」

**答案：说反了。位置必须改（首尾对调），不改的是实体身份、链位与链序。**

交换（`:5133-5140`）：

- `track`、`direction`
- `x_pos`、`y_pos`、`tile`、`z_pos` ← **世界坐标在这里换手**
- `gv_flags`（经 `SwapTrainFlags()`：`GVF_GOINGUP` / `GVF_GOINGDOWN` / `GVF_CHUNNEL` 按对方重算）
- `vehstatus` 的 `Hidden` 位

不交换（跟着实体走）：

- `index`（实体池编号）、`engine_type`、`subtype`（⇒ `IsArticulatedPart()` 与 `position` 位不变）
- `cargo`、`cargo_type/subtype`
- `unitnumber`、`name`、`group_id`
- `Next()` / `Previous()` 的指向（⇒ 链位、链序、`IsArticGroupHead`、`position_in_articulated_parts` 全部不变）
- `flags.Flipped`（`ReverseTrainSwapVeh` 不碰它）

交换后的收尾 `UpdateStatusAfterSwap(v, reverse=true)`（`:5061-5091`）：

- `InvalidateImageCache()`
- `direction = ReverseDir(direction)`（**在已 swap 过的方向之上再反转一次**，这是引擎既有语义，整链掉头也走这条，久经验证）
- `UpdateIsDrawn()`
- 非虫洞且非机务段 tile 时重跑 `VehicleEnterTile()`；虫洞分支额外修 `TRACK_BIT_WORMHOLE` 的误置

⇒ 玩家口中的「回调数据」其实是**从 `x_pos/y_pos/direction/track` 派生**出来的（NewGRF 变量 0x4D、`cached_veh_length`、贴图偏移）。真正被交换的是位置量本身，不是回调量。这也解释了为什么 `:6046` 的 `ConsistChanged(CCF_ARRANGE)` 与 `InvalidateImageCacheOfChain()` 足以让回调层跟上。

---

## 十六、第 439 轮：方案 B 落地（2026-11-14，玩家「改来试试看吧」）

### 16.1 改动

`CmdReverseTrainDirection()` 的 `reverse_single_veh` 组分支整体重写（原 `:5861-6043` → 现 `:5861-5905`）。删除五块：

| 删除内容 | 原行号 | 理由 |
|---|---|---|
| `real_artic` 判定 + `R3RDearticulateOneGroup()` 打散 | `:5861-5873` | 链序与 subtype 都不动，part 仍挂在原父车后，无需打散 |
| `group.size()<2 \|\| boundary_inside` 的 cosmetic fallback | `:5894-5904` | 不重链 ⇒ 段边界不再被破坏，不需要降级 |
| `direction = ReverseDir()` + `ArticGroupFlipped` 同步 | `:5913-5923` | direction 由 `ReverseTrainSwapVeh` 连带位置一起换 |
| `SetNext()` 重链 + 组角色迁移（`ArticGroupHead` 给 old_tail） | `:5925-5945` | 链序不动 ⇒ 组头恒在原处 |
| 段标记迁移 / `R3RMoveSegmentOwner` / `R3RRelocateFrontIdentity` / 车号车名 swap / `old_tail->SetFrontEngine()` | `:5947-6033` | 链头不动 ⇒ 无身份迁移，primary 恒不变 |

新实现（`:5883-5904`）：组内首尾配对 `ReverseTrainSwapVeh(front, l, r)` + 组内 `UpdateStatusAfterSwap(w)`，探针改为 `ARTIC-FLIP head= n= span=l..r primary=`。

### 16.2 编译自证

`src\train_cmd.cpp` 01:36:58 → `build\openttd.exe` 01:39:34（52 886 528 B，较上版 52 892 160 B 减少 5 632 B，与删除 ~170 行一致）；`read_lints` 0 条。

### 16.3 新发现的硬约束（重要）：depot 内 `direction` 不是自由变量

```83:83:src/train.h
static constexpr ConsistChangeFlags CCF_ARRANGE{ConsistChangeFlag::Length, ConsistChangeFlag::Capacity, ConsistChangeFlag::DepotDirection};
```
```410:413:src/train_cmd.cpp
/* Normalise direction of all train parts. */
if (allowed_changes.Test(ConsistChangeFlag::DepotDirection) && IsRailDepotTile(this->tile)) {
	u->direction = normalised_direction;
}
```

分支末尾 `:5907` 的 `front->ConsistChanged(CCF_ARRANGE)` **包含** `DepotDirection`，所以在 depot 内它会把**每节**的 `direction` 统一覆写成 depot 的归一化方向（`:393-396` 取 `GetRailDepotDirection()`，`DrivingBackwards` 时再取反）。

⇒ 方案 B 的两半在 depot 里命运不同：

- `x_pos` / `y_pos` / `tile` / `z_pos` 的交换**保留**（`ConsistChanged` 不改位置）；
- `direction` 的交换**被抹平**（随后被统一为 depot 口方向）。

这正是原版 `reverse_single_veh` 只翻 `VehicleRailFlag::Flipped`（纯贴图镜像）的原因：depot 里所有车必须同向，翻向只能靠镜像位表达。

⇒ 方案 B 的实际可见效果 = **组内各节交换停车位、朝向统一**。它是否等于玩家期望的"整组掉头"，必须实测；若不等价，则 depot 内唯一合法的翻转载体就是 `Flipped` 位（第 435 轮路线），矛盾点转为"如何让 NewGRF rank 也反"，而非位置。

### 16.4 待测清单（玩家复测）

1. 图像：组内三节是否仍在原三个位置、无重叠；
2. debug 面板 `position_in_articulated_parts` 是否仍 `0→2`；
3. 点"停止红叉"是否不再触发 `vehicle_gui.cpp:4346`；
4. `R3R_debug.log` 里是否出现 `ARTIC-FLIP head=… span=a..b primary=1`。

---

## 十七、第 440 轮：补上 `Flipped` 逐节翻转（方案 B 的"看得见"那一半）

### 17.1 现场（玩家复测第 439 轮 exe）

`build\R3R_debug.log` 里 5 次点击 = 5 行，逐字长这样：

```
ARTIC-FLIP head=0 n=5 span=0..4 primary=1
```

三个读数各自排除了一个错误方向：

- `n=5 span=0..4` —— 组确实是 5 节铰接组，而且它就是整条链；
- `primary=1` —— 链头身份没迁走，第 438 轮选方案 B（不重排链序）的目的达到，按停止键不再 `assert(IsPrimaryVehicle())` 崩；
- **分支确实执行了** —— 不是旧 exe、不是没进 else。

也就是说代码跑对了，但玩家看不见（"图像反转又没了"）。

### 17.2 根因：`ReverseTrainSwapVeh()` 不碰 `Flipped`，而 `direction` 交换会被抹平

`ReverseTrainSwapVeh()` 只交换 `track / direction / x_pos / y_pos / tile / z_pos / hidden / gv_flags`，**不碰** `VehicleRailFlag::Flipped`。而分支末尾 `train_cmd.cpp:5933` 的 `front->ConsistChanged(CCF_ARRANGE)` 含 `DepotDirection`（§16.3），会把每节 `direction` 统一抹成库口方向 ⇒ 刚交换过来的 `direction` 立刻失效。

剩下的可见效果只有"哪一节车体画在哪一格"。对同型多节动车组来说，换位置 + 朝向不变 = **肉眼零变化**。

"图像朝向"的真正载体是 `Flipped`：`Train::GetImage()` 见到它就把 `direction` 取反后再选 sprite（水平镜像），与上游 `reverse_single_veh` 翻转单节用的是同一个位。

### 17.3 修法（`src\train_cmd.cpp:5917-5926`）

在配对交换 + `UpdateStatusAfterSwap()` 之后，对组内每一节补：

```5917:
uint n_flipped = 0;
for (Train *w : group) {
	w->flags.Flip(VehicleRailFlag::Flipped);
	w->UpdateDeltaXY();
	if (w->IsDrawn()) w->Vehicle::UpdateViewport(true);
	if (w->flags.Test(VehicleRailFlag::Flipped)) n_flipped++;
}
```

`UpdateDeltaXY()` + `UpdateViewport()` 是必需的：`Flipped` 参与包围盒推导（斜向 `flip_offs`、直线 `half_shorten`），不重算会让贴图与点击框错位（KI-214 同源）。

语义是两项叠加，缺一不可：

- **位置交换** = 整组前后颠倒；
- **逐节 `Flipped`** = 每节图像镜像；
- 两者叠加 = 把整组掉头，连点奇数次停在"已反转"态、偶数次回原样。

**刻意不动** `ArticGroupFlipped` / NewGRF 变量 `0x4D`：rank 由链序决定，链序没动，位置交换已经把"哪一节的贴图出现在哪一端"换过来了，再翻 `0x4D` 会二次抵消（第 435 轮同类事故）。

日志改为 `ARTIC-FLIP head= n= span= primary= flipped=`（`:5928`）。

### 17.4 构建自证（2026-11-14）

- 入口：`R3R_fullbuild.cmd inc detach`（复用既有，未新建 `.cmd`）；构建前确认 `openttd.exe` 未运行。
- `build\R3R_incbuild.log` 尾：`[2/3] Building CXX object …train_cmd.cpp.obj` → `[3/3] Linking CXX executable openttd.exe`。
- `build\R3R_incbuild.done` = `EXIT_CODE=0`（01:58:32）。
- 时间戳链：`src\train_cmd.cpp` 01:54:09 → `train_cmd.cpp.obj` 01:57:21 → `build\openttd.exe` 01:58:27（52 886 528 B）。
- `read_lints` 0 条。
- exe 字面量（ASCII 扫描）：`ARTIC-FLIP head=` idx=45144952、`flipped=%u` idx=45144999（紧邻 ⇒ 同一个格式化串）。

### 17.5 疑点与待测清单

**REG-440-A（疑点，待验证）**：同型多节组里"每节都镜像"在画面上是否真能被玩家读成"整组掉头"？若每节 sprite 左右对称度高（例如对称车厢），镜像后仍看不出差别，则"交换位置 + 逐节镜像"依然可能视觉零变化。判据：用外观明显不对称的车（端头车/带司机室的动车组）复测，若仍看不出，则需要第三条路（例如同时反 `ArticGroupFlipped` 并把 rank 一起反，即第 435 轮遗留的矛盾点）。

**REG-440-B（疑点，待验证）**：`n_flipped` 只是"翻完之后组内处于 Flipped=1 的节数"，不是"本次翻了几节"。若组内某节在本次点击前就已经是 Flipped=1，读数会与 `n` 相等但语义不同。判据：连点两次，第二次应仍打 `flipped=0`（全组回到未翻转态）。

待测：

1. 同场景点一次，组内各节贴图**逐节镜像**（用不对称车最明显）；
2. 日志 `ARTIC-FLIP … n=5 … flipped=5`；
3. 连点 2 次回到点击前外观；奇数次停在已反转态；
4. 贴图与点击框不错位（KI-214 不复现）；
5. `primary=1` 恒成立、按停止键不崩（第 439 轮判据不回退）；
6. 组内三/五节的位置交换仍生效（原来首尾互换的停车位仍对调）。

---

## 十八、第 440 轮二次修正：去掉换位，只留 `Flipped`（KI-635）

### 18.1 现场（玩家复测第 440 轮首版 exe，原话）

> 「现在终于有一点效果了。但是，我发现，对于这个 5 节铰接车，应该在第二节位置的图像在第四节的位置。更准确地说，就是图像方向对了，但图像位置反了，比如说 0~15，本来应该在 12 的图像跑到 3 去了」

逐句拆：

- **「终于有一点效果」** ⇒ 第 440 轮补的逐节 `Flipped` 生效，图像朝向真的反了；
- **「第二节的图像跑到第四节」** ⇒ 5 节的组里出现 **i ↔ (n-1-i) 的位置对调**，即位置做了关于组中心的镜像；
- **「方向对了，但位置反了」** ⇒ 两半里 **`Flipped` 是对的，换位是错的**。

### 18.2 根因：两个"关于组中心镜像"的变换叠在了一起

第 440 轮首版的分支同时做了两件事：

```5897:5900:（首版，已删除）
for (int l = start, r = end; l < r; ++l, --r) {
	ReverseTrainSwapVeh(front, l, r);
}
for (Train *w : group) UpdateStatusAfterSwap(w);
```

- 换位：第 i 节的 `x_pos/y_pos/tile/z_pos/direction/track` 送到第 (n-1-i) 格；
- `Flipped`：每节的 sprite 再水平镜像一次。

两者都是"镜像"，叠加后读数就是"整组被镜像了两遍"。第 439 轮（只有换位没有 `Flipped`）之所以视觉无变化，是因为同型多节车厢互换格子本来就看不出来；本轮补上镜像后，位置那半的错误才暴露出来。

补充事实（第 439 轮已证）：`ReverseTrainSwapVeh()` 换过来的 `direction` 会被 `:5912` 的 `front->ConsistChanged(CCF_ARRANGE)` 抹成库口方向（`DepotDirection`），所以换位那半连"朝向"都贡献不了，只剩下不该有的位置对调。

### 18.3 修法：位置、链序、tile 全不动，只逐节翻 `Flipped`

`src\train_cmd.cpp:5864-5910`：删掉 `ReverseTrainSwapVeh()` 配对循环与 `UpdateStatusAfterSwap()`，保留逐节：

```5896:5905:
uint n_flipped = 0;
for (Train *w : group) {
	w->flags.Flip(VehicleRailFlag::Flipped);
	w->UpdateDeltaXY();
	if (w->IsDrawn()) w->Vehicle::UpdateViewport(true);
	if (w->flags.Test(VehicleRailFlag::Flipped)) n_flipped++;
}
```

依据：**depot 内合法的掉头表达只有镜像这一种**。库里所有车必须同向，翻向不能靠换位 —— 上游 `reverse_single_veh` 对单节也只翻 `Flipped`（§16.3 / §十七 已论证）。所以"整组掉头"在 depot 里的正确形态是：**各节留在原格，sprite 全部水平镜像**。

链序、`ArticGroupHead`、primary 身份、`ArticGroupFlipped` / NewGRF `0x4D` 一律不动（第 435/438 轮的两条事故线都不碰）。

### 18.4 构建自证（2026-11-14）

- 入口：`R3R_fullbuild.cmd inc detach`（复用既有，未新建 `.cmd`）；构建前确认 `openttd.exe` 未运行。
- `build\R3R_incbuild.log` 尾：`[2/3] Building CXX object …train_cmd.cpp.obj` → `[3/3] Linking CXX executable openttd.exe`。
- `build\R3R_incbuild.done` = `EXIT_CODE=0`。
- 时间戳链：`src\train_cmd.cpp` 02:13:55 → `train_cmd.cpp.obj` 02:18:56 → `build\openttd.exe` 02:20:09（52 886 528 B，与首版同尺寸）。
- `read_lints` 0 条。
- 本轮未新增日志字面量（沿用 `ARTIC-FLIP head= n= span= primary= flipped=`），故不做 exe 串核对。

### 18.5 本轮疑点与新判据

**REG-440-C（本条修完后需重点看）**：mirror-only 之后，**端头不对称的车**（动车组头车、带司机室端）会呈现"车头在左、朝右"的形态。这在 depot 里是合法的（车必须朝库口），但如果玩家期望的是"车头真的换到另一端"，那 depot 内做不到 —— 需要把车开出去掉头。若实测发现"朝向反了但看起来别扭"，说明语义分歧仍在，需重新拍板（可选：不翻、或翻且改段序）。

待测：

1. 5 节组点一次：**各节位置不变**，只有贴图水平镜像；
2. 再点一次（偶数次）完全回到点击前；
3. 日志 `ARTIC-FLIP … n=5 … flipped=5`；
4. 贴图与点击框不错位、`primary=1`、按停止键不崩（第 439/440 轮判据不回退）；
5. 端头车车身仍在原端，只是面朝反方向。

---

## 十九、第 441 轮：包围盒没跟着 `Flipped` 走（玩家给两个方案）

### 19.1 现场（玩家复测第 440 轮二次 exe，原话）

> 「还是和刚才一样的效果。我注意了一下边界框，发现问题出在：边界框和没经历此操作的同类列车还是一样的，所以也就是说你现在只是给所有节列车置了一个 flip flag 而已，但是，我希望的是，不仅有 flip flag，而且边界框也随之改变。现在有两个方案，第一个是按照整个铰接式的中轴左右对称，第二个是按铰接式的新头反向排列」

拆解：

- 「还是和刚才一样」⇒ mirror-only 之后外观仍无可辨变化；
- 「边界框和没经历此操作的同类列车还是一样的」⇒ **`Flipped` 没有进入包围盒计算**，或进入了但 depot 内各节 bounds 本来就同值；
- 「只是给所有节置了一个 flip flag」⇒ 玩家已准确识别当前实现只动了 `VehicleRailFlag::Flipped`；
- 「边界框也随之改变」⇒ 诉求是 `bounds` / `x_offs` / `y_offs` / 实际占位一起翻。

### 19.2 玩家给的两个方案

| 方案 | 描述 | 几何含义 |
|---|---|---|
| **方案 1** | 按整个铰接式的**中轴**左右对称 | 组内各节位置关于组中心镜像：第 i 节 ↔ 第 (n-1-i) 节 |
| **方案 2** | 按铰接式的**新头反向排列** | 从新头（原尾端）起按链序反排，各节长度不同时与方案 1 不等价 |

两者在等长节上等价，在不等长节（机车+车底混编铰接）上不同：方案 1 保持"位置镜像"，方案 2 让每节落在"从新头量起等于自身长度"的新位置上。

### 19.3 猜测根因（待验证）

**猜测 441-A**：`Flipped` 只影响 sprite 选择与 `flip_offs`（像素微调），不改变 `bounds` / `x_extent` / `y_extent`；而 depot 内 `front->ConsistChanged(CCF_ARRANGE)` 含 `DepotDirection`，会把组内每节 `direction` 统一抹成库口方向 ⇒ 各节包围盒朝向与未操作列车完全一致，故玩家看到"边界框没变"。

**猜测 441-B**：真正决定"哪节画在哪一格"的是 `x_pos/y_pos`（铰接组由引擎按累计长度算出）。要让包围盒镜像，必须在翻 `Flipped` 之外让 `x_pos/y_pos`（进而包围盒）关于组中心重排 —— 即第 440 轮首版做过的换位那半边，但当时与 `Flipped` 叠加被读成"位置反"。本轮要把两者正确合成一次变换，而不是单独任一半。

两条都要用 `R3R_artic_ctrl_reverse_round435_memo.md` 后续小节的行号证据做实或证伪。

### 19.4 待验证判据

1. `Train::UpdateDeltaXY()` 是否读取 `VehicleRailFlag::Flipped`，读了影响哪些字段；
2. 铰接组各节 `x_pos/y_pos` 由谁写（`ConsistChanged` 是否重算、还是保持调用方给的值）；
3. `bounds` 与 `x_pos/y_pos` 的关系（`Vehicle::GetBoundingBox` / 绘制锚点）；
4. 方案 1 与方案 2 在"不等长节"上的差值是否需要区分。

### 19.5 取证：`Flipped` 被 `RailFlips` 挡在包围盒之外（结论 441-A 成立）

`src\train_cmd.cpp:4928-4989` 的 `Train::UpdateDeltaXY()` 第一句就把 `Flipped` 的效力掐掉了一半：

```4933:4936:
/* Set if flipped and engine is NOT flagged with custom flip handling. */
int flipped = this->flags.Test(VehicleRailFlag::Flipped) && !EngInfo(this->engine_type)->misc_flags.Test(EngineMiscFlag::RailFlips);
/* If flipped and vehicle length is odd, we need to adjust the bounding box offset slightly. */
int flip_offs = flipped && (this->gcache.cached_veh_length & 1);
```

后面 `dir = flipped ? ReverseDir(dir) : dir`（`:4938-4939`）、直线分支的 `half_shorten = (VEHICLE_LENGTH - len + flipped) / 2`（`:4950`）、斜向分支的 `flip_offs`（`:4960/4966/4974/4980`）**全部依赖这个 `flipped` 局部量**。所以：

- 若引擎带 `EngineMiscFlag::RailFlips`（`src\engine_type.h:193`："Rail vehicle has old depot-flip handling"），`flipped` 恒 0 ⇒ `Flipped` **只改 `Train::GetImage()` 选的 sprite，完全不进包围盒**；
- `Train::GetCursorImageOffset()`（`:1929`、`:1959`）有同一个 gate。

**这与玩家观察逐字吻合**：「边界框和没经历此操作的同类列车还是一样的」—— 因为对这类车，引擎设计上就是"GRF 自己管翻转、盒子不动"（注释称 `custom flip handling`）。

⇒ 之前两轮的所有尝试都卡在这一层：换位改的是"哪节在哪格"，`Flipped` 改的是"sprite 左右镜像"，而**包围盒从未翻过**，所以玩家看到的始终是"没掉头"。

### 19.6 本轮修法：让 `Flipped` 对 `RailFlips` 车也进包围盒（新增 `ForceFlipBounds`）

`VehicleRailFlag` 现有位：0..22 上游、23 `ForceFlipReverse`（已废弃、保留占位、禁复用）、24 `ArticGroupHead`、25 `ArticGroupMember`、26 `SegmentFlipped`、27 `ForceReserveOnce`、28 `FakeDualHead`、29 `ArticGroupFlipped`。**30 为空位**。

- `src\train.h:57` 之后新增 `ForceFlipBounds = 30`：R3R 在 depot 铰接组翻转时逐节置位，作用是"即使引擎带 `RailFlips`，也让 `Flipped` 参与包围盒推导"。随 `Train::flags` 存档，无版本变更。
- `src\train_cmd.cpp` 两处 gate 改为 `!RailFlips || flags.Test(ForceFlipBounds)`：`UpdateDeltaXY()`（`:4934`）与 `GetCursorImageOffset()`（`:1929`、`:1959`）。
- 组翻转分支（`:5890` 一带）逐节 `flags.Set(ForceFlipBounds)`（与 `Flipped` 同步置/清，保证连点两次回到原样），并补每节探针 `ARTIC-FLIP-CAR idx= dir= flip= rfl= bO= bE= bOf=`，让"盒子到底翻没翻"可直接读日志。
- **本轮不换位**：玩家第 440 轮已明确否掉"位置反了"，本轮只解决盒子不动。

待验：若日志显示 `rfl=0`（引擎本就不带 `RailFlips`）而 `bO/bE/bOf` 仍与未翻车相同，则 441-A 被证伪，需转向 441-B（换位式排列，即玩家方案 1/2）。

### 19.7 第 441 轮实施：改走方案 2（位置镜像），**放弃** `ForceFlipBounds`

19.6 的方案在写码阶段被自己否掉了，理由两条，都写在代码注释里：

1. **量级不对**。解除 `RailFlips` gate 只能让"每节自己的盒子左右挪 1px"：直线分支 `half_shorten = (VEHICLE_LENGTH - len + flipped) / 2`，满长（len=8）时恒为 0；斜向分支的 `flip_offs` 也只在 `cached_veh_length` 为奇数时给 ±1。玩家报的是"整组排列没有反向"，1px 的盒子偏移根本不可见。
2. **玩家要的本来就是位置变化**。原话「不仅有 flip flag，而且边界框也随之改变」，且两个候选方案（中轴左右对称 / 按新头反向排列）描述的都是**整组的空间重排**，不是单节盒子的微调。

同时澄清上一轮的一个误判：第 440 轮被否掉的不是"换位"本身，而是"换位 + `Flipped`"的**双重镜像**（KI-635：位置换了一次、sprite 又镜像一次，读成"方向对了但位置反了"）。所以正确形态是：**换位 + 不翻 `Flipped`**。

落地（`src\train_cmd.cpp` 铰接组分支，`:5893` 起）：

- 新增局部常量 `constexpr bool mirror_sprites = false;` —— `false` = 方案 2（只镜像位置）；改成 `true` 即方案 1（位置镜像 + 逐节 `Flipped` 镜像），供玩家对比时一行切换。
- 首尾成对 `std::swap` 五样：`x_pos` / `y_pos` / `tile` / `track` / `z_pos`。**不动** `direction`、**不动** `Flipped`、**不动**链序（真 artic 组内父车必须先于部件，反转链序会破坏 `IsArticulatedPart` 的父子假设）。
- 交换后逐节 `UpdateDeltaXY()` + `IsDrawn() → Vehicle::UpdateViewport(true)`，保证包围盒的 `offset` 与贴图/点击框跟着新位置走。
- 交换是自逆操作 ⇒ 连点两次必然回到原样。
- 新增逐节只读探针（每个受影响的节一行）：
  `ARTIC-FLIP-CAR idx= x= y= len= flip= db= bO=(x,y) bE=(w,h) bOf=(x,y)`
  —— 其中 `bO`=bounds.origin、`bE`=bounds.extent、`bOf`=bounds.offset。老探针 `ARTIC-FLIP head= n= span= primary= flipped=` 保留在最前面一行。
- 未动 `src\train.h`（不新增 `ForceFlipBounds`），因此本轮是纯 `.cpp` 增量。

### 19.8 复测判据（第 441 轮）

1. 点一次「掉头」后，`ARTIC-FLIP-CAR` 里首尾两节的 `x=`/`y=` 互换（中间节也随之反排），`bOf=` 跟着变 ⇒ 位置与包围盒都动了。
2. 游戏内观感：整组车头出现在原来的尾端位置，且各节**朝向不变**（都仍是库口方向）—— 这就是 depot 内掉头的正确表现（库里不允许出现两种朝向）。
3. 连点两次「掉头」⇒ `ARTIC-FLIP-CAR` 的 `x=/y=` 全部回到第一次之前的值，外观复原。
4. `primary=1`、按停止键不崩（KI-634 已收口的判据不得回退）。
5. 若玩家觉得"朝向也要反过来"，把 `mirror_sprites` 置 `true` 再编一次（同一处探针会显示 `flip=1`）。

### 19.9 构建自证（第 441 轮）

- 入口：`R3R_fullbuild.cmd inc detach`（复用既有脚本，未新建 `.cmd`）；构建前 `tasklist /FI "IMAGENAME eq openttd.exe"` 为空。
- `build\R3R_incbuild.done` = `EXIT_CODE=0`；日志尾 `[2/3] Building CXX object …train_cmd.cpp.obj` → `[3/3] Linking CXX executable openttd.exe`；错误计数（`error C` / `fatal error` / `FAILED:` / `build stopped`）= 0。
- 时间戳链：`src\train_cmd.cpp` 02:41:43 → `train_cmd.cpp.obj` 02:43:27 → `build\openttd.exe` 02:45:48（52 886 528 B）。
- `read_lints` 0；`findstr /M /C:"ARTIC-FLIP-CAR" build\openttd.exe` 命中。
- 本轮只改 `src\train_cmd.cpp`，未碰任何 `src\*.h`，故未触发 KI-183 的全量护栏。

### 19.10 REG-441-A 取证：depot 窗口按链序绘制，根本不读 `x_pos/y_pos`（决定性）

- 玩家第 441 轮原话：「我观察到的就是操作后什么反应都没有了，可能就是你说的『引擎可能在后继 tick 按链序把位置重算回去，使镜像只维持一帧』」→ 前半句（零反应）**成立**，但机制比"被 tick 重算回去"更彻底：**depot 界面从来不读位置**。
- 证据链（三条，逐字引用代码）：
  1. `src\vehicle_gui.cpp:2192-2196`：`DrawVehicleImage()` 对 `VehicleType::Train` 直接转 `DrawTrainImage(Train::From(v), r, selection, image_type, skip)`。
  2. `src\train_gui.cpp` 的 `DrawTrainImage()` 主循环：`for (; v != nullptr && (rtl ? px > 0 : px < max_width); v = v->Next()) { int width = Train::From(v)->GetDisplayImageWidth(&offset); ... v->GetImage(dir, image_type, &seq); seq.Draw(px + offset.x, y + offset.y, pal, ...); px += rtl ? -width : width; }` ⇒ 逐节横坐标 `px` 是**按链序累加出来的**，`x_pos/y_pos/bounds` 一次都没进这个函数。
  3. `src\vehicle.cpp` 进库路径（车进入 depot 那一刻）置 `VehState::Hidden` ⇒ **世界视口里根本不画库内的车**，所以"位置"这个通道在 depot 场景下连一个像素都影响不到。
- 推论（把前三轮的现象一次说清）：
  - 第 439 轮（方案 B：配对 `ReverseTrainSwapVeh` 交换 `x_pos/y_pos/tile/track/z_pos`）在 depot 里**视觉恒等于零**——交换的是没人读的字段。
  - 第 440 轮（方案 2：位置镜像、不翻 `Flipped`）同样**恒等于零**，且比 REG-441-A 更强：不需要后继 tick 重算，第一帧就看不见。
  - 第 436 轮（方案 A：打散 + 反转链序 + 改 `direction`）**是唯一看得见变化的一轮**，因为它动了链序（= depot 行的绘制顺序）；玩家当时"图像方向对了但位置反了"，就是链序变了而 `direction` 又被 `CCF_ARRANGE` 的 `DepotDirection`（`train.h:83` / `train_cmd.cpp:410-413`）抹平、只剩 `Flipped`/`0x4D` 的镜像在起作用的结果。
- 结论：**在 depot 里实现"整组掉头"的可见通道只有两条——①链序（决定 depot 行里谁画在第几位）②每节 sprite（`Flipped` / NewGRF 0x4D / rank）。位置与包围盒在本场景是死通道。** 玩家要的"按铰接式的新头反向排列"字面上就是①。
- 代价（拍板前必须知道）：①对**真铰接组**反转链序会把 artic 部件排到父车之前，必须先用 `R3RDearticulateOneGroup()` 打散（第 436 轮已做过）⇒ 该组永久变成"打散组"（`ArticGroupHead/Member` 角色位），引擎仍是原 engines，但 `CBID_VEHICLE_ARTIC_ENGINE` 父子回调链断掉（KI-632 的荷兰包长度问题同源）；②链序反转后新链头是原组尾，可能不是引擎 ⇒ 需要第 439 轮那套 primary/假引擎头移交；③反转后该组的"队首"换了对象，`R3RChainHasRealArticPart` / 段标记 / 排程属主都要跟着走。
- 本轮**未改源码**（方案 2 的位置交换代码仍在树里，已知无效且开库时可能有一帧位置错乱风险），等玩家对 §19.11 的三问拍板后一并处理。

### 19.11 三条可选修法（请玩家按 C-1 / C-2 / C-3 回填）

| 编号 | 做法 | 可见效果 | 代价 / 风险 |
| --- | --- | --- | --- |
| C-1 | **绘制层反转**：给该组打一个"已反转"标记（`VehicleRailFlag` bit 30 空闲，或复用组角色位），`DrawTrainImage()` 遇到标记时**按 `v->Last()` 反序绘制**该组；命中测试/拖拽/hover 同步反序；`x_pos/y_pos` 一律不动 | depot 行里整组立刻读作"车头到了原来尾端"；开出库后恢复正向（不持久） | 只改观感，不动运动几何，零崩溃面；需同步改 depot 命中、拖拽落点、`GetDisplayImageWidth` 累加方向；世界视口不涉及（库内车 Hidden） |
| C-2 | **物理反转链序**（第 436 轮路线 + 第 437/439 轮修复）：`R3RDearticulateOneGroup()` 打散 → 逆序重链 → 逐节 `flags.Flip(Flipped)`（**不再改 `direction`**，它会被 `DepotDirection` 抹平）→ 新组首接 primary/假引擎头 | 真的换头：车头成为原组尾，出库即以新头带车，持久 | 该组永久变为"打散组"（`ArticGroupHead/Member`），GRF 的 artic 父子回调链断掉（KI-632 荷兰包长度问题同源）；必须做完 primary/号/名/组/排程属主移交；`R3RChainHasRealArticPart` 相关闸门语义随之改变 |
| C-3 | **收口**：回滚方案 2 的位置交换，保留第 435 轮"只翻 `Flipped`" | depot 内每节贴图镜像但排列不变 | 承认 depot 内不做整组掉头；真换头请开出库掉头（上游语义） |

- 三问（请按编号答复）：
  - **C-1** 是否接受"只在 depot 里看起来整组反了、开出库恢复"这一纯观感语义？
  - **C-2** 是否接受"真铰接组被打散成独立车"（这是 R3R 升段/克隆已经在用的状态，但对纯 GRF 铰接车属于不可逆改型）？
  - **C-3** 若两者都不接受，是否就按上游语义收口（只翻 `Flipped`）？
- 备注：无论选哪个，**方案 2 的位置交换都必须删掉**——它在 depot 零效果，且开库瞬间可能让车节位置跳跃一帧。

### 19.12 玩家拍板 C-2（库内物理反序），2026-11-14 原话

> 「我认为我们需要库内物理反序。我们知道，车库内的列车摆放，总是固定一端朝前，另一端固定朝后。表现为总是有：前-铰接1-铰接2-铰接3-后。但实际上我需要能够有 前-铰接3-铰接2-铰接1-后。这里的反转不仅是物理上的，也是逻辑上的。只不过，我们的 primary_vehicle 永远是最朝前的车厢」

- 口径解读（据此实现）：
  1. **物理反序** = 组内链序真的倒过来（`SetNext` 重排），不是只翻 sprite；位置也按新链序沿轨道轴镜像，保证几何自洽。
  2. **逻辑反序** = 新组首/新段首/identity 一并迁到原组尾，组角色位（`ArticGroupHead/Member`）随新序重设。
  3. **`primary_vehicle` 恒为最朝前的车厢** ⇒ 反转后新链头（原组尾）必须拿 `IsPrimaryVehicle()`（`FrontEngine` + `orders`/号/名/profile），旧链头降为链内车。
- 实现骨架（替换 §19.7 的方案 2 分支）：
  - ① 收集组成员；若含真 artic 部件（`group_head->GetNextArticulatedPart() != nullptr`）先 `R3RDearticulateOneGroup(group_head)` 打散（真 artic 组内父车必须先于部件，反序必须靠打散解除这条约束；该函数返回组后首节）。
  - ② 组内 `SetNext` 逆序重接，并把链外 `before->SetNext(new_head)` / `new_tail->SetNext(after)` 接回；`previous` 同步维护。
  - ③ 位置沿轨道轴镜像：新序第 i 位取旧序第 n-1-i 位的 `x_pos/y_pos/tile/track/z_pos`（等价于首尾配对 `std::swap`，与方案 2 的写法相同）。
  - ④ 逐节 `flags.Flip(VehicleRailFlag::Flipped)`——depot 里 `direction` 会被 `DepotDirection` 抹平，sprite 朝向只能靠这个位；此时「链序反转 + sprite 镜像」在 depot 行里合成原行的完美镜像（这是第 440 轮首版缺的链序那一半）。
  - ⑤ identity 移交：仅当组首就是链头（`front == group_head`）且旧头 `IsFrontEngine()` 时，`R3RRelocateFrontIdentity(old_head, new_head)`；旧头若是车底假引擎再 `R3RDestroyCarOnlyFormation(old_head)`；新头原型是 `RailVehicleType::Wagon` 时 `R3RCreateCarOnlyFormation(new_head)`。
  - ⑥ 段标记随组边界镜像：组首带 ★ 则 ★ 移到新头、⊗ 对称。
  - ⑦ 出口保留 `front->ConsistChanged(CCF_ARRANGE)` + 三个窗口标脏 + `UpdateDeltaXY()`/`UpdateViewport()` 逐车刷新。
- 与第 436 轮方案 A 的唯一差别：当时逐节改 `direction`（被 `DepotDirection` 抹平，玩家看到"方向对了但位置反了"），本轮改 `Flipped` 并新增 ⑤ 的 primary 移交。
- 待验证的坑（写码前逐条自检）：`R3RRelocateFrontIdentity` 是否已清 from 的 `FrontEngine`；`R3RCreateCarOnlyFormation` 内部会 `ConsistChanged`，调用顺序必须在重接与 Relocate 之后；`previous` 字段可写性。

### 19.13 C-2 落地（2026-11-14，已实现 + 已编译，游戏内复测待做）

代码全部落在 `src\train_cmd.cpp` 的 `CmdReverseTrainDirection()` → `reverse_single_veh` 分支（约 5880-6060），未碰任何 `src\*.h`，增量合法。

落地与 §19.12 骨架的**偏差**（按代码事实修正）：

1. **⑤ 没有调 `R3RCreateCarOnlyFormation()` / `R3RDestroyCarOnlyFormation()`**。
   - `R3RCreateCarOnlyFormation()` 内部会跑一次 `ConsistChanged(CCF_ARRANGE)`，而此刻新旧两个链头都还带 `FrontEngine`，插一次整链重算没有收益；改为手写 `SetEngine/ClearWagon/ClearFreeWagon/SetFrontEngine`，让紧随其后的 `R3RRelocateFrontIdentity()` 里的 `NormaliseSubtypes()` 收尾（它有 `assert(chain->IsEngine())`，所以必须先置引擎位）。
   - `R3RDestroyCarOnlyFormation()` 会把车设成 `FreeWagon` 并清段右边界 —— 那是"整条链退回自由车厢"的语义，用在**链内车**上会破坏 `NormaliseSubtypes` 的不变式；改为手写 `ClearFrontEngine/ClearEngine/SetWagon`。
2. **纯自由车厢链单独分支**：`before == nullptr` 但旧头不是 `IsFrontEngine()`（FreeWagon 链）时走 `NormaliseSubtypes(new_head)` 把链头身份位挪到新头，不迁移任何列车级身份。
3. **`previous` 不能直接写**（编译错误 1）：`Vehicle::previous` 是 private，且还要同步 `OTTD_UPPER_TAGGED_PTR` 的"非链头"位标记。改用与 `R3RFlipChainBySegments` 第 3 步同一手法——借原前驱的 `SetNext(nullptr)` 清空。
4. **`first`/`last` 必须显式重刷**（写码时新发现的坑，非 §19.12 预判）：`Vehicle::SetNext()` 里的 `v->first = this->first` 取的是**调用方**的 first，倒序重接时新链头的 first 还指着旧链头，会沿每一步传播成错值 ⇒ 重接后对 `chain_first`（`before ? before->First() : new_head`）整链 `SetFirst/SetLast`。
5. **`R3RRelocateFrontIdentity` 必须显式传第三参**（编译错误 2）：默认值 `owner_moved_by_flip = false` 只写在**定义**处（`train_cmd.cpp:~11807`），前置声明在 5810 行无默认值，本调用点在定义之前 ⇒ `error C2660: 函数不接受 2 个参数`。已写 `R3RRelocateFrontIdentity(old_head, new_head, false)`。
6. **④ 除 `Flipped` 外还翻 `SegmentFlipped`**（`FlipSegmentFlipped()`）：链序反转必然改变段内/组内名次，`newgrf_engine.cpp` 按 `SegmentFlipped ^ ArticGroupFlipped` 判 0x4D 镜像，故只翻前者——翻两个等于没翻。
7. **组角色位按新链序重设**：全组 `ClearArticGroupHead/ClearArticGroupMember` 后 `new_head->SetArticGroupHead()`、其余 `SetArticGroupMember()`（打散组靠这两位识别父子，反序后不重设会指错车）。
8. 出口补 `new_head->InvalidateNewGRFCacheOfChain()`（名次/镜像/链序全变），探针 `ARTIC-FLIP` 追加 `real=/old=/new=/move=` 四个字段。

构建自证（两次）：

| 次序 | 结果 | 说明 |
|---|---|---|
| 第 1 次 | `EXIT_CODE=1` | `train_cmd.cpp(5937) error C2248: "Vehicle::previous" 无法访问 private 成员`；`train_cmd.cpp(5972) error C2660: "R3RRelocateFrontIdentity": 函数不接受 2 个参数` |
| 第 2 次 | `EXIT_CODE=0` | `[3/3] Linking CXX executable openttd.exe`；`src\train_cmd.cpp` 03:35 → `train_cmd.cpp.obj` 03:36 → `build\openttd.exe` 03:38（52 886 528 B）；`read_lints` 0 |

待复测判据（C-2 六条）：

1. 库内选中铰接组按反转 ⇒ 库内**行的节序真的变了**（`前-3-2-1-后`），不再是"点了没反应"。
2. 反转后 `primary_vehicle`（车号/名/排程/`M` 标记）落在**最朝前**的那节上，旧链头降为链内车且不再 `IsPrimaryVehicle()`。
3. 连点两次回到原样（逆序自逆）；车号/名/排程不丢不乱（`R3RRelocateFrontIdentity` 的 orders/index 搬迁生效）。
4. 真 artic 组第一次反转后日志出现一次 `DEARTIC-SPLIT`（之后该组永久为打散组，第二次反转不再打散），且 `dumo_vehicle` 可看到 `tf:` 含 `H`/`M`、`st:` 无 `A`（判据见 KI-268）。
5. 段边界 ★/⊗ 随组边界镜像：反序后段数不变，段头仍是物理最前那节；多段链只影响被选的这一组。
6. 负判据：组在链中间（前面还有机车）时链头/`primary` 不动；单节/非铰接车厢的反转行为与上一版逐字相同。

## 二十、第 443 轮：C-2 复测反馈两问（2026-11-14）

### 20.1 玩家原话（保留原文要点）

> 至少图像反转取得了初步成果，但是，目前依然有一下问题：
> 1，车库内预览还是显示异常，车号预览也显示异常。但是，假若关闭车库界面再重新打开，又能重新显示正常的车库预览和车号，只是停着的列车变成了启动的。
> 2，打开那个反转后的列车的详情界面，发现各项显示有异常，包括物理数据，运行费用，最高速度，最大牵引力，列车名字……

判读：图像反转（C-2 的节序互换）**已生效**，失败的是**反转后的收尾**——UI 缓存没刷新 + 列车级身份/统计没跟着走到新链头。

### 20.2 两条症状的共同结构

| 症状 | "重开界面就正常"说明什么 | 推断的落点 |
|---|---|---|
| 车库预览（图像+车号）异常，重开正常 | 显示用的是**窗口内缓存的旧数据**，重开窗口触发 `BuildDepotVehicleList()` 重建后才读到新状态 ⇒ 反转后**没有作废该窗口的数据** | C-2 出口缺 `InvalidateWindowData(WindowClass::VehicleDepot, depot->index)`（以及可能的 `SetWindowDirty`） |
| 重开后"停着的列车变成启动的" | 不是显示层问题——**新链头上真的没有 `vehstatus.Stopped` 位** | `R3RRelocateFrontIdentity()` 没搬 `vehstatus`（旧链头的停驶位留在已降级的车身上） |
| 详情界面：物理数据/运行费用/最高速度/最大牵引力全部异常 | 这些字段全部来自 `Train::GetDisplayMaxSpeed()/GetDisplayPower()/GetDisplayMaxTractiveEffort()/GetDisplayRunningCost()`，读的是链头的 `gcache.cached_*` | C-2 出口**没有调 `ConsistChanged()`** ⇒ `gcache` 还是新链头"作为车厢时"的旧值（车厢的 cached_power/max_te 为 0） |
| 详情界面：列车名字异常 | 标题走 `v->name`（空则回退引擎默认名），而新链头是原来的**车厢** | 两种可能：`R3RRelocateFrontIdentity` 的名字迁移没覆盖这条路径，或新车头是普通车厢导致回退名变成车厢名 |

### 20.3 猜测根因（待验证，先落盘）

- **G-1（问题 1 的显示层）**：`CmdReverseTrainDirection()` 的 `reverse_single_veh` 分支出口只做了 `InvalidateNewGRFCacheOfChain()` + `UpdateDeltaXY()` + 探针，**没有任何窗口作废调用**。车库窗口的 `vehicle_list`/`wagon_list` 是 `OnInit`/`RebuildVehicleList` 时填的快照（`BuildDepotVehicleList`），C-2 改的是车辆本身而不重建列表 ⇒ 窗口继续按旧快照画（图像按旧节序、车号取旧链头）⇒ 与"重开就好"完全吻合。**这条同时是上一轮 KI-637 的直接顺延**：同一个"depot 列表不重建"的根，上一轮只补了操作侧的守卫/解析，没补"编辑后作废窗口"。
- **G-2（问题 1 的状态位）**：`vehstatus.Stopped` 与 `FrontEngine` 是**同一类"前车专属"状态**，C-2 的 identity 搬迁走 `R3RRelocateFrontIdentity()`，需实证它是否搬 `vehstatus`/`current_order`/`cur_speed`。若不搬，新链头（原链尾车厢）在库里显示"启动"。
- **G-3（问题 2 的统计）**：`Train::ConsistChanged()` 是 `gcache`（power/max_speed/max_te/veh_length/refit capacity）唯一重算入口，C-2 从未调用它。反转前这些值算在旧链头（引擎）上；`primary` 换人后，详情窗口改读新链头的 `gcache`——那是一节车厢的陈旧值 ⇒ 物理数据/费用/速度/牵引力全错。
- **G-4（问题 2 的名字）**：`R3RRelocateFrontIdentity()` 内应含 name/name_backup 处置（与 KI-201/第 160 轮 NAME-XFER/NAME-RESTORE 同族），但 C-2 传的是 `owner_moved_by_flip=false`，可能走了"不搬名字"的分支。

### 20.4 取证计划（下一组工具调用，按序）

1. 读 `src\train_cmd.cpp` C-2 出口（约 6019-6110）：确认有无 `ConsistChanged` / `InvalidateWindowData` / `SetWindowDirty` / `vehstatus` 搬运。
2. 读 `R3RRelocateFrontIdentity()` 定义（约 11807 起）：列出它到底搬了哪些字段（orders/unitnumber/name/vehstatus/cur_speed/dispatch）。
3. 读 `Train::ConsistChanged()`（`train_cmd.cpp` 约 300-400）：确认它是否顺带同步 `vehstatus.Stopped`、`gcache` 的每个字段。
4. 读 `depot_gui.cpp` 的 `BuildDepotVehicleList` 调用点与 `InvalidateWindowData` 处理：确认车库窗口收到什么失效消息才重建列表（`WD_...` 值）。

### 20.5 预判修法（待取证后定稿）

- **S-1**：C-2 出口补 `ConsistChanged(CCF_ARRANGE)`（放在 identity 搬迁之后、`InvalidateNewGRFCacheOfChain` 之前/之后均可，需按 `gcache` 依赖顺序定）。
- **S-2**：C-2 出口补窗口作废：`InvalidateWindowClassesData(WindowClass::VehicleList)` + `InvalidateWindowData(WindowClass::VehicleDepot, depot_id)` + 若该车有详情窗口则 `SetWindowDirty(WindowClass::VehicleDetails, veh_index)`。注意车库窗口类名与 `WD_` 语义需实证（第 219 轮踩过 `WindowClass::VehicleOrders` vs `Orders` 的坑）。
- **S-3**：`vehstatus.Stopped`（以及 `cur_speed`/`subspeed`）在 identity 迁移时一起搬，或在 `ConsistChanged` 后按"链上任意一节 stopped ⇒ 新链头 stopped"归一。
- **S-4**：名字走既有 `R3RParkTrainName()` / `R3RRestoreTrainName()`（第 160 轮实现），确认 C-2 路径是否该以 `owner_moved_by_flip=true` 调用 `R3RRelocateFrontIdentity()`。

### 20.6 未确认（U 项）

- **U-1**：详情界面"运行费用/最高速度/最大牵引力"三项是读 `gcache` 还是每次现算 `RailVehInfo(engine_type)`。若读 `engine_type` 的静态信息（成本系数），那异常可能不是 `gcache` 而是"新链头 engine_type 是车厢"导致取到车厢的 `RailVehInfo`。两者修法不同（前者补 `ConsistChanged`，后者要保证新链头仍是引擎或按段/链取引擎信息）。
- **U-2**：玩家"图像反转取得初步成果"是否指库内行节序真的变了（C-2 判据 1 通过），还是只指列车在场景里的朝向变了。需玩家确认，影响是否需要继续排查 `DrawTrainImage` 侧。
- **U-3**：问题 1 的"车号预览异常"是显示成旧号、空白、还是重复号。三种成因不同（旧链头号 vs `unitnumber_backup` 冻结 vs 段行未更新）。

### 20.7 复测判据（本轮修完后）

1. 库内选中铰接组反转后，**不重开窗口**，预览图像与车号立即正确（判据：反转前后行内容按 `前-3-2-1-后` 即时切换）。
2. 反转后该列车在库里显示为**停止**（与反转前一致），不是"启动"。
3. 打开反转后列车的详情：物理数据/运行费用/最高速度/最大牵引力/列车名与反转前**一致**（对比同型号未反转列车）。
4. 负判据：普通（无铰接组）列车反转后详情界面各项与上一版逐字相同。
5. 负判据：组夹在链中间反转时链头不变、详情界面读的仍是原链头身份。

### 20.8 取证结论（第一批，2026-11-14）

**结论 A：G-1 大半证伪。** C-2 出口（`src\train_cmd.cpp:6059-6063`）**确实**写了收尾：

```6059:6063:src/train_cmd.cpp
			front->ConsistChanged(CCF_ARRANGE);
			SetWindowDirty(WindowClass::VehicleDepot, front->tile.base());
			SetWindowDirty(WindowClass::VehicleDetails, front->index);
			SetWindowDirty(WindowClass::VehicleView, front->index);
			DirtyVehicleListWindowForVehicle(front);
```

所以"没调 `ConsistChanged`"这条推断不成立。但有两处可疑：

- **A-1（疑点）**：`SetWindowDirty(WindowClass::VehicleDepot, front->tile.base())` 的第二个参数是**窗口号**。若车库窗口的 `window_number` 是 depot 的 `TileIndex`，那用 `front->tile` 只是"碰巧可能相等"——C-2 交换了 `tile`，`front`（新链头）的 tile 是原组尾的 tile，很可能**不等于**窗口号 ⇒ 这次 `SetWindowDirty` 空转，窗口根本没重绘。这正好解释"必须关掉重开才正常"。待实证：`depot_gui.cpp` 里 depot 窗口的 `window_number` 到底存什么。
- **A-2（疑点）**：`SetWindowDirty` 只重绘，**不重建 depot 的 `vehicle_list`/`wagon_list`**。行里的车号取的是列表项指针（C-2 前是旧链头）的 `unitnumber`；身份搬走后旧链头号可能变 0 ⇒ 车号显示异常。`DirtyVehicleListWindowForVehicle()` 刷的是 `VehicleList` 窗口，不是 depot 自己的列表。⇒ 需要 `InvalidateWindowData(WindowClass::VehicleDepot, <depot id>)` 让它走 `RebuildVehicleList()`。

**结论 B：G-4 坐实，且已经在代码注释里写着本场景。** `R3RRelocateFrontIdentity()`（`:11925`）的 `name_follows_identity` 判据是：

```11988:11988:src/train_cmd.cpp
	const bool name_follows_identity = owner_moved_by_flip && kept_to_name.empty() && !from->name.empty();
```

C-2 在 `:5988` 传的是 **`false`** ⇒ `name_follows_identity` 恒假 ⇒ 走"还原 to 自己的名字"分支，把刚拷进来的列车名当场抹成空，而 `from` 的名字又被 `R3RParkTrainName(from)` 停进备份（旧链头已降级为链中车，再没人取）⇒ 详情窗口标题回退成默认名（如"列车 N"），玩家看到的正是"列车名字异常"。

而 `:11977-11987` 的注释逐字描述的就是这条路径：*"身份迁移路径(owner_moved_by_flip)下 to 是刚从链内升上来的车厢/引擎,它自己本来就没有名字(名字属于旧链头 from),还原一个空快照等于把上面拷贝刚写进来的名字当场抹掉……2026-10-02 现场:……链头 50 变无名……界面显示默认名"列车 6""*。**⇒ C-2 应当传 `true`（这就是身份迁移路径），上一轮传 `false` 是错的。**

**结论 C：详情窗口的关闭是预期行为，不是 bug。** `R3RRelocateFrontIdentity()` 开头（`:11937-11943`）会 `CloseWindowById` 掉 `from` 的 VehicleView/Orders/Refit/Details/Timetable 等窗口。所以"反转后重新打开详情界面"读到的是**新链头**的数据——异常来自新链头本身的数据，不是陈旧缓存。⇒ 与结论 B 一致，也把 KI-639 的 G-3a/G-3b 缩小到"新链头的 `engine_type` 是车厢"这一条。

**结论 D（新疑点 A-3，KI-639 的关键）**：C-2 的 ⑤ 步（`:5979-5984`）给新链头做了 `SetEngine()/SetFrontEngine()`，但**没有改它的 `engine_type`**——链头因此变成"挂着车厢 `engine_type` 的假引擎"。`ConsistChanged()` 里凡是按链头 `engine_type` 取静态信息的地方（运行费用、最高速度基准、牵引力、默认名）都会取到车厢的值；凡按 `IsEngine()` 累加的地方还会把这节车厢当成一台引擎参与计算。这比"没调 `ConsistChanged`"更符合玩家描述（物理数据、运行费用、最高速度、最大牵引力、名字**一起**错，且重开窗口后照旧错）。待实证：`Train::ConsistChanged()` 对链头 `engine_type` 的依赖程度。

**下一批取证**：读 `Train::ConsistChanged()`（`src\train_cmd.cpp` 约 280-420），确认 gcache 各字段对链头 `engine_type`/`IsEngine()` 的依赖；读 `depot_gui.cpp` 里 depot 窗口 `window_number` 的定义与 `OnInvalidateData` 的 `WD_` 语义。

### 20.9 第二批取证结论（2026-11-14）

**结论 E：A-1 证伪，窗口号没错。** 车库窗口的 `window_number` 就是 depot 的 `TileIndex`，全工程既有写法一律 `InvalidateWindowData(WindowClass::VehicleDepot, tile.base())`。但 `SetWindowDirty` 只重绘、**不重建列表**，A-2 因此坐实：车库行/车号取自 `BuildDepotVehicleList()` 的快照（项是 `v->First()`），C-2 换了链头后快照里仍是旧链头指针。

**结论 F：`ConsistChanged(CCF_ARRANGE)` 一直都在（`:6059`），不是缺它。** `Train::ConsistChanged` 通读（`:374-619`）不写 `vehstatus`，只重算 `gcache`/`cached_tflags`/`driving_backwards`/货物与加速。⇒ "物理数据/速度/牵引力错"若在重开详情窗后依旧，只能是新链头 `engine_type` 仍是车厢（结论 D），不是算不出来。

**结论 G（KI-638 定案）**：`R3RRelocateFrontIdentity()` 只搬 orders / 订单位置 / 车号 / `current_order` / profit / 服役间隔 / 窗口 / 名字，**完全不碰 `vehstatus`**。C-2 换头后新链头 `vehstatus` 仍是一节车厢的值（Stopped=0）⇒ 车库行画成"运行中"，与玩家"停着的列车变成了启动的"逐字吻合；`running_ticks` 也据此开始累计。

**结论 H（KI-639 定案）**：`name_follows_identity` 判据被第 199 轮绑在 `owner_moved_by_flip` 上，C-2 不是翻转搬身份故只能传 `false` ⇒ 刚拷进新链头的名字被"还原 to 的空快照"当场抹掉，旧链头的名字又被 `R3RParkTrainName()` 停进已降级车身 ⇒ 两处都没名字。

### 20.10 落地（第 443 轮，仅 `src\train_cmd.cpp`）

1. **KI-638（Stopped 随身份迁移）**：C-2 的 ⑤ 身份迁移段之前采样 `const bool old_was_stopped = old_head->vehstatus.Test(VehState::Stopped);`；迁移后 `front = new_head` 时按快照 `Set/Reset`，并 `old_head->vehstatus.Reset(VehState::Stopped)`（旧链头降为链内车，按约定链内车该位恒 0，否则将来重新升为链头会带陈旧"已停"）。
2. **KI-639（名字跟随身份解耦）**：`R3RRelocateFrontIdentity()` 增加第 4 参 `bool name_follows_chain = false`，判据改为
   `const bool name_follows_identity = (owner_moved_by_flip || name_follows_chain) && kept_to_name.empty() && !from->name.empty();`
   折叠修正路径不传（默认 `false`，行为逐字不变）；C-2 调用点改为 `R3RRelocateFrontIdentity(old_head, new_head, false, true);`。函数头 Doxygen 补 `@param name_follows_chain`。
3. **列表重建**：把 C-2 出口的 `SetWindowDirty(WindowClass::VehicleDepot, ...)` 换成 `InvalidateWindowData(WindowClass::VehicleDepot, front->tile.base())`（`VehicleDepotWindow::OnInvalidateData` 无条件 `generate_list = true` ⇒ 走 `BuildDepotVehicleList()` 重扫，行内的预览图/车号/链头指针随之更新），并补 `InvalidateVehicleListWindows(VehicleType::Train)`（`InvalidateData(0)` ⇒ `BaseVehicleListWindow` 在 data==0 时 `ForceRebuild`，顺带覆盖车队/分组列表、`TraceRestrictSlots`、`DepartureBoard` 里挂着的陈旧 `VehicleID`）。两者用新局部 `bool head_changed` 门控：只有"组从链头开始翻、链头真的换人"时才重建，普通 `Flipped` 单节反转与组夹在链中间的情形完全不动。
4. 保留 `SetWindowDirty(WindowClass::VehicleDetails/VehicleView, front->index)`（旧链头的详情/视图窗已由 `R3RRelocateFrontIdentity` 内的 `CloseWindowById` 关掉，重开即读新链头）。

### 20.11 复测判据（第 443 轮 vs 20.7 的差异）

1. 库内反转铰接组后**不重开窗口**：预览图像与车号立即变成新节序（20.7-1）。
2. 反转后该列车在库里显示**停止**（20.7-2）；负判据：反转前本来就是"运行中"的列车（库内不会出现）不受影响。
3. 打开反转后列车详情：运行费用/最高速度/最大牵引力/名字与同型号未反转列车一致（20.7-3），且**名字非空、等于反转前那台机车的名字**（KI-639 的正判据）。
4. 负判据：普通无铰接组列车反转，详情窗各项与上一版逐字相同；组夹在链中间反转时链头不变、`VehicleDepot` 列表不重建（`head_changed=false`）。
5. 待复核项 U-4：新链头 `engine_type` 仍是车厢（C-2 的 ⑤ 只给 `SetEngine/SetFrontEngine`，没换 `engine_type`）。若复测发现"运行费用/最高速度"仍按车厢取静态值，则需另开一条：给新链头挑选链上第一台**真引擎**的 `engine_type`（或让 `GetDisplay*` 走段/链级取引擎信息）。

### 20.12 构建自证（第 443 轮）

- 入口：`R3R_fullbuild.cmd inc`（复用既有脚本，未新建任何 `.cmd`）；`< nul` 喂掉末尾 `pause`，避免留下等待按键的窗口占用日志（对照 KI-637 的构建踩坑）。
- `build\R3R_incbuild.guard.log` = `GUARD: incremental is safe`（只改 `src\train_cmd.cpp` 一个 `.cpp`，未碰 `src\*.h`）。
- 日志：`[1/3]` 复检 → `[2/3] Building CXX object CMakeFiles\openttd_lib.dir\src\train_cmd.cpp.obj` → `[3/3] Linking CXX executable openttd.exe`，尾行 `==== R3R build ended 2026/11/14 18:48:48 MODE=inc EXIT_CODE=0`。
- `build\R3R_incbuild.done` = `EXIT_CODE=0`（18:48）。
- 时间戳链：`src\train_cmd.cpp` 18:34 → `train_cmd.cpp.obj` 18:42 → `build\openttd.exe` 18:47（52 890 112 B）。
- `read_lints`（`src\train_cmd.cpp`）= 0 条。
- 本轮未新增日志字面量，故不做 exe 串核对（新建的 `head_changed` 是局部量、`InvalidateVehicleListWindows` 是既有函数）。

### 20.13 仍未做（本轮刻意不动）

1. **U-4 / G-3b 的下半段**：新链头 `engine_type` 仍是车厢。换 `engine_type` 会连带影响 NewGRF 变量、`cached_veh_length`、`UpdateDeltaXY` 与图像，必须在拿到"详情窗各项到底读哪条路径"的复测数据后再决定。
2. `cur_speed`/`subspeed` 未随身份迁移（库内 `cur_speed` 恒 0，暂无可观察差异）。
3. `R3RRelocateFrontIdentity` 的其它调用点（折叠修正、车库编辑）未逐一核对是否也该传 `name_follows_chain=true`：目前只按 C-2 的实测缺口打开，其余保持旧行为，避免扩大改动面。

## 二十一、第 444 轮：反转后「运载能力=无 / 引擎名称=(未定义字符串) / 物理性能偏差」取证

### 21.1 玩家原话（2026-11-15）

> 列车反转了之后，运载能力变为无，引擎名称变为”（未定义字符串）“，各项物理性能也有所偏差（这是一个例子，我只是借此表明这些属性出现了偏差）

口径：这是**一个例子**，玩家用它说明"链头身份换成车厢之后，凡是按链头 engine_type 读取的属性整体失真"，不是三个孤立 bug。故本轮按"读取路径"系统性取证，而不是逐个打补丁。

### 21.2 已实证（代码锚点，逐条自证）

**A. C-2 只换身份位，不换 `engine_type`**（`src\train_cmd.cpp` C-2 分支，见 §20.9 记录）
新链头只执行 `SetEngine()` / `SetFrontEngine()` / `ClearWagon()` / `ClearFreeWagon()`，`engine_type` 仍是原车厢值；迁移调用为 `R3RRelocateFrontIdentity(old_head, new_head, false, true)`。⇒ 出现"`IsEngine()==true` 但 `RailVehInfo(engine_type)->railveh_type==Wagon`"的混合态。

**B. `ConsistChanged()` 把"链头的车厢引擎"当成整链的引擎**（`src\train_cmd.cpp:374-619`）
- `:380 rvi_v = RailVehInfo(this->engine_type)` ⇒ 链级静态量（含 `rvi_v->pow_wag_power`，动力车厢功率加成的开关）：**未完成**
- `:381 first_engine = IsFrontEngine() ? this->engine_type : INVALID_ENGINE` ⇒ 逐节 `gcache.first_engine` 全部指向**一台车厢引擎**，影响 wagon-override sprite set / 2CC / 部分 GRF 回调：**未完成**
- `:515 new_cap = e_u->DetermineCapacity(u)` ⇒ 逐节按**自身** engine_type 算，与链头无关：见 C

**C. 单节容量不会被链头身份改写**（`src\engine.cpp` `Engine::DetermineCapacity`）
容量按 `this->index`（该车自己的 engine_type）取 `PROP_TRAIN_CARGO_CAPACITY` 并走 refit 回调。⇒ "运载能力=无"**不是** `cargo_cap` 被清零，而是显示层的汇总问题（见 D）。

**D. 详情窗"运载能力"是链头起、遇到非 `ArticGroupMember` 就停的汇总**（`src\train_gui.cpp` 运载能力页）
汇总循环形如 `do { item->capacity += v->cargo_cap; ... } while ((v = v->Next()) != nullptr && v->IsArticGroupMember());`
⇒ 新链头若是车厢（`cargo_cap` 常为 0）且后一节不是 `ArticGroupMember`（真 artic 部件、或单节链），汇总立刻终止 ⇒ 条目为空 ⇒ 回退到 `STR_VEHICLE_INFO_NO_CAPACITY`（"无"）。这与"整列真的不能装货"不同，是**显示口径**问题，可单独修。

**E. 引擎名称回退到 `STR_UNDEFINED`**（`src\strings.cpp` 的 `SCC_ENGINE_NAME`）
引擎名为空/引擎未启用时回退 `Engine::info.string_id`；`STR_UNDEFINED = 0x0002` 的显示文本正是"(未定义字符串)"。⇒ 无名字的铰接部件/车厢引擎被抬成链头后，界面上就是"(未定义字符串)"。

### 21.3 猜测根因（待验证，未落码）

- **G-4a**："物理性能偏差"主因 = B 的两条：`rvi_v`（`pow_wag_power` 门控）与 `first_engine` 污染。验证判据：反转前后 dump 同一列车的 `gcache`（power/weight/te/max_speed）与逐节 `gcache.first_engine`；若功率在"有动力车厢的车组"上掉一个可计算的量，即成立。
- **G-4b**："运载能力=无"主因 = D 的循环终止条件，与容量值无关。验证判据：反转后详情窗容量页为空，但 `dump_vehicle` 逐节 `cargo_cap` 仍非 0。
- **G-4c**："引擎名称未定义"主因 = E，且与"该车厢引擎是否真有名字"耦合（NewGRF 车厢/部件常无名字）。验证判据：换一台有名字的机车做链头（或把该节换回原链头）名字即恢复正常。

### 21.4 候选修法（待拍板，均未落码）

1. **甲（显示层，最小面）**：新增"链级代表引擎"解析——从链头沿 `Next()` 找第一台 `railveh_type != Wagon` 的车，返回其 `engine_type`；只供详情窗的引擎名/容量汇总/静态性能读取使用，不动 `engine_type` 本身。风险最低，但只治 UI。
2. **乙（ConsistChanged 层）**：把 `rvi_v` 与 `first_engine` 的取值改成"链上第一台真引擎"，让 `gcache` 与 `gcache.first_engine` 回到"整列车有一台真机车"的正确语义。会影响物理与 GRF 回调，需大范围复测。
3. **丙（身份层，最彻底）**：C-2 时把新链头的 `engine_type` 改成链上真引擎。**已知高危**：连带动 `cached_veh_length`/`UpdateDeltaXY`/图像/NewGRF 变量/存档，且会出现"一台真机车变成两节"的视觉错误，倾向不做。

倾向：先 甲（可立即验证），再评估 乙（真正影响物理的那一层）。

### 21.5 本轮未做

未改任何源码、未构建。原因是三条现象分属三个不同读取面，需要先定"甲/乙"口径再动，否则容易把显示修补与物理语义修补混在一处。

### 21.6 甲（显示层）落地（2026-11-15，玩家指令「先做甲试试看」）

**只改 `src\train_gui.cpp`，未碰任何 `src\*.h`，增量编译合法。**

判据（两个文件内 static，插在 `TrainDetailsInfoTab` 之前）：

- `R3RConsistRepresentativeEngine(const Train *v)`：`!v->IsFrontEngine() || RailVehInfo(v->engine_type)->railveh_type != Wagon` ⇒ 原样返回；否则沿 `Next()` 找链上第一台非 Wagon 的 `engine_type`。⇒ 对普通车辆/普通车厢单元恒等于自身 `engine_type`，负判据天然成立。
- `R3RDetailsWholeConsistCapacity(const Train *v)`：`v->IsFrontEngine() && RailVehInfo(v->engine_type)->railveh_type == Wagon` ⇒ 混合态链头。

三处改动：

1. **引擎名**（对应玩家说的"引擎名称变为(未定义字符串)"）：`TrainDetailsInfoTab` 里两处 `PackEngineNameDParam(v->engine_type, EngineNameContext::VehicleDetails)` 改为 `PackEngineNameDParam(R3RConsistRepresentativeEngine(v), ...)`，分别覆盖 Wagon 分支（`STR_VEHICLE_DETAILS_TRAIN_WAGON_VALUE[_AND_SPEED]`）与引擎分支 case 0（`..._ENGINE_BUILT_AND_VALUE[_AND_SPEED]`）。字符串侧确认：`STR_VEHICLE_DETAILS_TRAIN_ENGINE_BUILT_AND_VALUE` = `{LTBLUE}{ENGINE}...`（english.txt:4801/4802），`{ENGINE}` 在 `SCC_ENGINE_NAME` 末尾回退 `e->info.string_id`（strings.cpp:2200），隐藏铰接部件的 `string_id` 就是 `STR_UNDEFINED`(0x0002) ⇒ "(未定义字符串)" 的完整链路成立。
2. **容量汇总**：`GetCargoSummaryOfArticulatedVehicle` 抽出 `add_unit` lambda；混合态链头改为 `for (u = v; u; u = u->Next())` 整列汇总并直接返回。原因：容量页空 ⇒ `STR_VEHICLE_INFO_NO_CAPACITY`（"无"）在 `DrawTrainDetails` 与 `GetTrainDetailsWndVScroll` 两处都会出现；混合态链头那一节常是 `cargo_cap == 0` 的车厢/部件，逐单元汇总必然为空。
3. **行数一致**：`GetTrainDetailsWndVScroll` 与 `DrawTrainDetails` 的外层单元循环在混合态链头处 `break`，保证"只画一行整列汇总"且滚动条行数与绘制逐行对齐（两处必须同时改，否则行数错位）。

**刻意未做（留给乙）**：

- `TrainDetailsInfoTab` 的 `get_speed()` 仍用链头 `rvi`（`PROP_TRAIN_SPEED` 按车厢取），可靠性行 `v->GetEngine()->reliability` 亦然。
- 物理层 `gcache`（功率/牵引力/重量/运行费用）与 `gcache.first_engine` 一字未动 —— 那是乙的 `ConsistChanged()` 修复面。
- 未新增任何日志/探针，因此不需要 exe 字面量核对。

**复测判据（甲）**：

1. 库内反转（C-2）后打开详情窗：引擎名行不再是"(未定义字符串)"，而是被抬链的那台机车型号名。
2. 同一列车容量页不再显示"无"，且数值等于整列所有载货车厢之和（与反转前对比应相等）。
3. 滚动条能拉到最底，最后一行内容不被截断（行数两处一致）。
4. 负判据：普通未反转列车、纯车厢链（`IsFrontEngine()` 为假）、原生 artic 组，详情窗逐字不变。
5. 负判据：非列车（公路/船/飞机）详情窗不受影响（本改动只在 `train_gui.cpp`）。

**构建自证（甲）**：复用 `R3R_fullbuild.cmd inc detach`（未新建任何 .cmd）；`build\R3R_incbuild.guard.log` = `GUARD: incremental is safe (no header/lang file is newer than the newest object)`；日志 `[2/3] Building CXX object ...\train_gui.cpp.obj` → `[3/3] Linking CXX executable openttd.exe`；`build\R3R_incbuild.done` = `EXIT_CODE=0`（build ended 2026/11/15 01:15:13 MODE=inc）；时间戳 `src\train_gui.cpp` 01:11:35 → `train_gui.cpp.obj` 01:12:57 → `build\openttd.exe` 01:14:54（52 891 136 B）；`read_lints` 0 条。本轮未新增日志字面量，故不做 exe 串核对。

**状态（甲）**：已实现 + 已编译；游戏内复测待做（判据见上 5 条）。乙（`ConsistChanged` 的 `rvi_v` / `first_engine`）未动。

---

## 第 444 轮续二：玩家新提案「最前车厢当父车 + 1 功率 + GRF 仍认原父车」与现行甲的差异

**玩家原话（2026-11-15）**：

> 还是有问题。能不能这样：永远让铰接式最前面的车厢当父车（可以直接给它赋予1功率变引擎）。但是各类grf回调还是认原先的父车，从后往前数。顺便讲一下现行方案与这个的区别

### 22.1 事实核对：提案的一半已经是现状

1. **「最前面的车厢当父车」= C-2 已经做了。** C-2 反转提交对整组先清 `ArticGroupHead/Member`，再把反转后的组首 `SetArticGroupHead()`、其余 `SetArticGroupMember()`，并通过 `R3RCreateCarOnlyFormation()` 给组首 `SetEngine() + ClearWagon() + ClearFreeWagon() + SetFrontEngine()`。即：反转后物理最前节**已经是「父车 + 引擎身份」**，唯一没动的是 `engine_type`（刻意的，见 22.2）。
2. **「赋 1 功率变引擎」= `Train::GetPower()` 早已有这条地板。** `src\train.h:596-602`：`if (this->IsEngine() && RailVehInfo(this->engine_type)->railveh_type == RailVehicleType::Wagon) power = std::max<uint16_t>(power, 1);`（KI-630，第 434 轮，为"纯车底链无法启动"落的）。`gcache.cached_power` 的累加走 `GroundVehicle::CalculatePower()` → `u->GetPower()`，故"链头至少 1hp"在物理层**已经生效**。
3. **「引擎」在 OpenTTD 里不由功率决定。** `RailVehicleType`（`src\engine_type.h`）只有 `Singlehead / Multihead / Wagon` 三值；"是否引擎"看 `VehicleRailFlag::FrontEngine/Engine` 位，"显示成什么"看 `engine_type` 指向的 `Engine` 原型（`info.string_id` / `power` / `weight` / `running_cost` 全是原型属性）。⇒ 给 1 功率不会让详情窗换出一个原型名。

**结论：玩家提案真正的新内容只有第三句**——"各类 GRF 回调还是认原先的父车，从后往前数"。

### 22.2 甲 vs 新提案：改的层不同

| 维度 | 甲（已编译，未通过复测） | 玩家新提案（≈ 乙） |
|---|---|---|
| 改动位置 | `src\train_gui.cpp` 的两三个读取点 | `Train::ConsistChanged()` 的链级量锚点 |
| 链头 `engine_type` | 不变（仍是车厢/part 原型） | 不变（改它=丙，已知高危） |
| `gcache.cached_power/max_te/weight/running_cost` | 不动 | 按"原父车"重算 |
| `gcache.first_engine`（`:381`） | 不动（仍是车厢引擎） | 指向原父车 ⇒ `GetPoweredPartPower()` 恢复 |
| GRF 回调看到的父车 | 不变（新链头持有 `ArticGroupHead`） | 需额外镜像回"原父车、从后往前数" |
| 风险 | 零（不碰结构/存档/GRF） | 中高（`first_engine` 是 wagon-override / 2CC / 精灵 / 容量的总开关） |
| 能治什么 | 只管我改的那 3 行显示 | 能治物理 + GRF 偏差 |

一句话：**甲是"读值时的翻译"，链上数据一个字没改；新提案是"把链级静态量的锚点从链头换回原父车"**。前者零风险但覆盖窄，后者治本但要全量复测。

### 22.3 共同硬边界：名字来自原型，不是从别处"借"

无论甲还是新提案，引擎名最终都是某个 `Engine::info.string_id`。若反转组里**所有**原型都是 `STR_UNDEFINED`（隐藏铰接部件 / 无名车厢），任何"换锚点"都取不到名字，只能显示占位（如"铰接车组"）。这条必须先确认，否则会把架构改造做完仍然显示"(未定义字符串)"。

### 22.4 疑点（立即登记，待验证）

- **H-1（最可能）**：`R3RConsistRepresentativeEngine()` 要求 `railveh_type != Wagon`；若该 GRF 的父车原型本身也是 `Wagon`（只有 `Singlehead` 才算"非 Wagon"），全链无候选 ⇒ 兜底返回自身 ⇒ 仍 `STR_UNDEFINED`。验证：`dump_vehicle <链头id>`，看链头 `tf:` 是否含 `H`、`st:` 是否含 `A`，并逐节核对 `railveh_type`。
- **H-2**：玩家打开的详情窗那节不是链头 ⇒ `!v->IsFrontEngine()` ⇒ 甲的函数按设计原样返回（它只翻译混合态链头）。验证：确认打开的是否该列车首行（链头）。
- **H-3**：`DrawTrainDetails` 的可靠性 / 最高速度 / 运行费用行仍直接读 `v->GetEngine()` / `get_speed()`，甲未覆盖；玩家说的"还是有问题"可能就是这几行。

### 22.5 待拍板

- **C-1（甲+，建议先做）**：代表引擎判据放宽为"链上第一台 `string_id != STR_UNDEFINED` 的车"（不限 `railveh_type`），并把速度 / 可靠性 / 费用 / 牵引力行统一走它。只改 `train_gui.cpp`（纯 .cpp 增量，约 30 分钟），风险零；治不了物理与 GRF。
- **C-2（乙，玩家提案完整版）**：改 `ConsistChanged` 的 `rvi_v` / `first_engine` 锚点 + `newgrf_engine.cpp` 组内计数镜像。能治物理与 GRF，但需全量复测（功率/牵引力/重量/费用/速度/图像/存档）。
- 建议顺序：先 C-1 拿"名字能不能出来"的定性，再决定是否付 C-2 的代价。

**本轮状态**：零源码改动、零构建（仅取证与登记）。

### 22.6 玩家答复（2026-11-15）与拍板：走 C-2

**玩家原话**：

> H1，我买的列车的父车原型是engine。H2，那条链只有一个铰接式，就是我反转的那个
> 我希望做C2

**答复落点**：

- **H-1 证伪**：父车原型是 engine（`railveh_type != Wagon`）⇒ 链上**确实存在**可当"代表引擎"的原型，`R3RConsistRepresentativeEngine()` 的判据不是空的。甲仍失败的根因因此不在此。
- **H-2 成立（关键）**：那条链只有一个铰接组、就是被反转的那个 ⇒ 反转后**链头就是真 artic part**（`subtype` 带 `GVSF_ARTICULATED_PART`）。
- **由此得出一条新的、此前未登记的硬事实（判据 H-4）**：`Train::GetPower()`（`src\train.h:592`）的条件是 `if (!this->IsArticulatedPart() && ...)` ⇒ **真 artic part 作为链头时走 `return 0`，连 KI-630 的 1hp 地板都在该分支之外，压根不生效**。同理 `IsArticulatedPart()` 还会让该车在 `GetNextUnit()/GetPrevUnit()`/段判定/独立出售等路径上被当成"不是真实单元"。⇒ 「给最前车厢 1 功率变引擎」在**真 artic part 形态下物理上不成立**，必须先把它变成"假铰接"（`ArticGroupHead/Member` 角色位）才谈得上功率。
- 这一条同时解释了甲为什么"还是有问题"：甲的显示层翻译能修名字/容量几行，但链头的 `IsArticulatedPart() == true` 让功率恒 0、`GetEngine()` 仍指向隐藏部件原型，物理与 GRF 面全部落在甲覆盖之外。

**玩家拍板**：做 **C-2**（乙，物理/结构层）。取出口径：

1. C-2 反转遇到真 artic 组时，先把该组**去铰接化**（复用既有 `DearticulateChainWithSnapshot` / `R3RDearticulateOneGroup`：拆成独立车 + `ArticGroupHead`/`ArticGroupMember` 角色位 + 烘焙 override），让新链头不再是 `IsArticulatedPart()` ⇒ `GetPower()` 的 1hp 地板才对它生效。
2. `Train::ConsistChanged()` 的链级静态量锚点从"链头 `engine_type`"改为"链上代表引擎"（`rvi_v` 的 `:380`、`first_engine` 的 `:381`），使 `pow_wag_power` / `gcache.first_engine` / 运行费用 / 重量等回到父车的语义。
3. `newgrf_engine.cpp` 的组内计数（0x40/0x41/0x4D）保持"仍认原父车、从后往前数"，需要按 `SegmentFlipped ^ ArticGroupFlipped` 核对镜像是否覆盖去铰接化后的新角色位分布。
4. 甲的显示层三处改动**保留**（作为兜底：即使物理层换成父车原型，显示取值的入口一致，不会回退成"(未定义字符串)"）。

**回归护栏（必须一并满足）**：①未反转的普通列车、纯车厢链、原生（未去铰接的）artic 车，`ConsistChanged` 结果与上一版逐值相等；②存档往返不丢角色位与烘焙 override；③`GetPoweredPartPower()` 的动力车厢功率不得因 `first_engine` 变化而反向减少（原父车是 engine 时应恢复）；④不能出现"链头 `IsArticulatedPart()==false` 但 `ArticGroupHead` 缺失"的半截状态（会让 0x4D 计数越界，参照第 170 轮 `previous==nullptr` 崩溃）。

**新登记疑点**：

- **H-4（已由玩家答复做实，见上）**：真 artic part 链头功率恒 0，1hp 地板不生效。
- **H-5（待验证）**：`DearticulateChainWithSnapshot` 目前只在升段命令（`vehicle_cmd.cpp`）与读档迁移（`R3RDearticulateLegacyArticSegment`）被调用；若在 C-2 反转提交点直接调用，`ConsistChanged(CCF_ARRANGE)` 必须在其**之后**执行，否则 `cached_veh_length` / 段边界仍按真 artic 旧值。
- **H-6（待验证）**：去铰接化不可逆（真 artic → 假 artic）。反转后再反转（二次 C-2）不应二次拆解（须幂等守卫：只对 `subtype` 仍带 `GVSF_ARTICULATED_PART` 的组动手）。

### 22.7 实施：C-2 落地（2026-11-15，第 445 轮）

**先纠正本方案自己在 §22.5 / §22.6 里的一处口径错误**（本轮取证最重要的更正）：

- 事实：C-2 反转提交点（`src\train_cmd.cpp` 反转块末尾）**已经把父车角色搬到物理最前的车厢**：`new_head->SetArticGroupHead();` 然后 `for (i = 0; i + 1 < n_cars; i++) group[i]->SetArticGroupMember();`，并对新链头补 `SetEngine()/ClearWagon()/ClearFreeWagon()/SetFrontEngine()`。
- 推论：因此"代表引擎 = 持 `ArticGroupHead` 的那一节"在 C-2 之后**恒等于链头本身**。按 §22.6 第 2 条字面写出来的锚点改动是**空操作**，不可能修掉玩家看到的 "(未定义字符串)" / "运载能力=无"。
- 真正可用的判别信号是**原型有没有名字**：被打散的各节 `engine_type` 指向隐藏铰接部件原型 ⇒ `Engine::Get(id)->info.string_id == STR_UNDEFINED`；原父车（反转后在链尾）是唯一"有名字 + `railveh_type != Wagon`"的那一节。
- 这同时解释甲为什么"还是有问题"：甲的触发条件是 `railveh_type == Wagon`，而本 GRF 的铰接部件原型不是 `Wagon`（H-1 只说父车是 engine），故甲在新链头上直接早退，名字与容量两条都没修。

**落地（两个 .cpp，未碰任何 `src\*.h`）**

1. `src\train_cmd.cpp` 新增 `static Train *R3RConsistReferenceEngine(Train *head)`（定义在 `ConsistChanged()` 之前、`R3RLeadingEndHasDrivingCab()` 之后）：
   - 链头原型**有名字** ⇒ 直接返回链头（普通链、天然顺序的打散组、原生 artic 车全部一字不动 = 护栏①）；
   - 否则在链头的同一打散组内（走到第一台非 `ArticGroupMember` 即停）找第一个有名字的节，优先 `railveh_type != Wagon`，否则任一有名字的节；全无则退回链头。
2. `Train::ConsistChanged()` 三处：
   - `const RailVehicleInfo *rvi_v = RailVehInfo(r3r_ref_engine->engine_type);`（原 `this->engine_type`）⇒ `pow_wag_power` 等回到父车语义；
   - `EngineID first_engine = this->IsFrontEngine() ? r3r_ref_engine->engine_type : EngineID::Invalid();`
   - 循环内 `if (u->IsEngine()) first_engine = ...` 加条件：**"角色是引擎、但原型是无名隐藏部件"的节不得成为后继车的 `first_engine` 锚**，改写成代表引擎（`u->engine_type == r3r_ref_engine->engine_type` 时保留自身）。这一条必须写在第一趟循环里而不是事后改 `gcache`——`tcache.cached_override = GetWagonOverrideSpriteSet(..., u->gcache.first_engine)` 在第二趟循环（`ConsistChanged()` 内）就已取值，事后改 `first_engine` 不会刷新 override。
   - 调试：仅当代表引擎 ≠ 链头时打一行 `C2-REF head= ref= refEng= refType= powWag= n= db=`，按 (head, ref) 去重，不刷屏。
3. `src\train_gui.cpp`（甲，保留作兜底并本轮扩展）：
   - 新增 `R3RDetailsNeedsRepresentativeEngine(v)`：链头原型是 `Wagon`（原判据）**或**原型无名字（新增，覆盖 C-2 反转后的隐藏部件）；
   - `R3RConsistRepresentativeEngine()` 改用该判据，并优先挑"有名字的非 `Wagon`"节；
   - `R3RDetailsWholeConsistCapacity()` 直接复用该判据 ⇒ 名字与容量都走父车，且容量走"整链汇总成一行"的分支（`DrawTrainDetails` / `GetTrainDetailsWndVScroll` 两处的 break 条件同步生效）。

**回归护栏对照**：① 未反转普通链、纯车厢链、原生 artic 链的链头原型都有名字 ⇒ 三处改动全部不触发，`ConsistChanged` 逐值不变；② 不改任何存档字段、不动角色位 ⇒ 存档往返无变化；③ `first_engine` 从"无名隐藏部件"变成"父车"只会让 `cached_override` / `GetPoweredPartPower()` 从"查不到 override"回到"查父车"，不会减少；④ 未触碰 `ArticGroupHead/Member` 的授予与撤销，半截态不可能由本改动引入。

**新登记疑点（本轮未改）**

- **H-7（待评估，低）**：`tcache.cached_tflags` 的 `TCF_MOVING_UNIT_START` 判据仍是角色位（`!u->IsArticGroupMember()`）。反转后组边界落在"组的物理尾"（= 原父车）而不是物理头；`driving_backwards` 时反之。影响面仅 `IsMovingUnitStart()` 的两处消费者（`economy.cpp` 平台超长 / pull-through 装载分块）。与名字、容量、功率三条症状无因果关系，本轮不动；若后续要改，正确语义是"沿运动方向的同组第一节"。
- **H-8（待评估，低）**：`rvi_v` 换成父车原型后 `pow_wag_power` 取父车的值；若父车 `pow_wag_power != 0` 而组内某节原型是 `Wagon`，`PoweredWagon` 位会按父车语义点亮。判为"恢复原生 artic 行为"而非新错，但需实测功率不涨。
- **H-4 的处置**：C-2 已先去铰接化，新链头 `IsArticulatedPart() == false` 且 `SetEngine()` 后 `IsEngine()` 为真 ⇒ `GetPower()` 走正常分支，1 hp 地板与烘焙的 `power_override` 都生效。H-4 只在"未去铰接"时成立，本轮无需改 `train.h`。

**构建自证（第 445 轮）**

- 第一次编译在 `train_cmd.cpp(625)` 报 `error C2039: "cached_num_engines" 不是 "GroundVehicleCache" 的成员` —— `C2-REF` 探针引用了不存在的字段，`R3R_incbuild.done` = `EXIT_CODE=1`（01:42:48）。`ground_vehicle.hpp:31-52` 里没有该字段，改用 `cached_power` 后重编通过。
- 复用既有入口 `R3R_fullbuild.cmd inc detach`（未新建任何 `.cmd`）；护栏 `build\R3R_incbuild.guard.log` = `GUARD: incremental is safe (no header/lang file is newer than the newest object)`。
- 日志 `build\R3R_incbuild.log`（1118 B，01:45:35）：`[2/3] Building CXX object ... train_cmd.cpp.obj` → `[3/3] Linking CXX executable openttd.exe`；`(Select-String 'error C|fatal error|FAILED:|build stopped|error LNK').Count` = **0**。
- `build\R3R_incbuild.done` = `EXIT_CODE=0`（2026-11-15 01:45:35）。
- 时间戳链：`src\train_gui.cpp` 01:41:39 → `train_gui.cpp.obj` 01:42:44；`src\train_cmd.cpp` 01:43:46 → `train_cmd.cpp.obj` 01:44:31；`build\openttd.exe` 01:45:28（52 891 136 B）晚于全部源码与 obj。
- `read_lints` 两文件 0 条；exe 内命中 `C2-REF head=%d ref=%d` 字面量。
- 发布树 `build-release` 本轮**未编**（按口径只编内测探针树）。

### 22.8 C-2 复测结果：引擎名仍"(未定义字符串)"（第 446 轮，取证中）

**玩家原话**：

> 反转后详情窗「引擎名称」仍然是(未定义字符串)，与改动代码之前没有任何区别，请问这是什么情况，能讲讲根因和你打算怎么修吗

**定性**：C-2 的 `ConsistChanged` 锚点改动**没有影响到名字行**。名字行与 `ConsistChanged` 无因果关系（`rvi_v`/`first_engine` 只喂 wagon-override 与动力车厢功率，不喂详情窗的 `{ENGINE}` 参数）。故"名字仍错"与"C-2 是否成功"是**两个独立问题**，不能反推 C-2 失败。

**疑点登记（按规则先落盘、待验）**：

- **N-1（最可能）**：详情窗名字行的 `EngineID` 本来就不走 `R3RConsistRepresentativeEngine()`。上一轮改的是 `TrainDetailsInfoTab` 里"两处 `PackEngineNameDParam`"，但若该行实际由 `STR_VEHICLE_DETAILS_TRAIN_ENGINE_BUILT_AND_VALUE` 之外的另一条串/另一处代码渲染（如窗口标题、`GetTrainDetailsWndVScroll`、或 `DrawTrainDetails` 的另一分支），改动就是打在空处。**期望**：名字行必须落到 `R3RConsistRepresentativeEngine(v)->engine_type`；**待验**：逐处列出所有 `PackEngineNameDParam` 与 `{ENGINE}` 消费点。
- **N-2**：`R3RDetailsNeedsRepresentativeEngine(v)` 返回假 —— 判据要求 `v->IsFrontEngine()`，若详情窗的 `window_number` 不是链头（车库点选某节车厢、或选中的是链内非链头），判据直接早退，`R3RConsistRepresentativeEngine` 返回自身。**待验**：玩家点选的是哪一节；`IsFrontEngine()` 在 C-2 后是否真在新链头上。
- **N-3**：`R3RConsistRepresentativeEngine()` 走完遍历仍没找到有名字的节 ⇒ 退回链头。原因可能是 `if (!u->IsArticGroupMember()) break;` 在 C-2 后的角色分布上过早终止（例如原父车的 `Member` 位没被授予，或组边界在新链头与第二台机车之间断开）。**待验**：C-2 后逐车 `ArticGroupHead/Member` 的实际分布。
- **N-4**：链上确实存在有名字的节，但详情窗渲染用的是缓存/另一字段（如 `Vehicle::name`、`GetEngineName()`），改 `engine_type` 不改变它。
- **N-5**：玩家跑的不是本轮 exe（mtime 01:45:28）—— 但玩家明确说"与改动代码之前没有任何区别"，若为旧 exe 也会呈现该形态，须先排除。

**取数需求**：`C2-REF` 是否出现（出现=代表引擎≠链头，说明 C-2 已把锚点换到父车；不出现=代表引擎==链头）+ 名字行渲染点 + C-2 后角色位分布。

**取证结论（第 446 轮，代码级）**

- **疑点 N-1 排除**：名字行确实就在 `src\train_gui.cpp:348`（Wagon 分支）与 `:358`（`case 0` 引擎分支）两处 `PackEngineNameDParam(R3RConsistRepresentativeEngine(v), ...)`，上一轮的改动打在正确位置。串 `STR_VEHICLE_DETAILS_TRAIN_ENGINE_BUILT_AND_VALUE`（`english.txt:4801` = `{LTBLUE}{ENGINE}{BLACK}   Built: ...`）确认玩家说的"引擎名称"就是 `{ENGINE}`。
- **根因（N-2，已坐实到代码路径）**：`R3RDetailsNeedsRepresentativeEngine()` 第一句 `if (!v->IsFrontEngine()) return false;`。而 `TrainDetailsInfoTab` 走 engine/wagon 分支的判据是 `rvi->railveh_type == Wagon`（`:347`），**完全不看 `IsFrontEngine()`** —— 即"点开的车不是前置引擎"时，窗口照样画引擎名行，但代表引擎解析被 `IsFrontEngine()` 一句话打回原值。C-2 反转后玩家在车库点的是哪一节、以及 `SetFrontEngine()` 是否落在被点那节，全都不受控 ⇒ 该早退一旦成立，输出与改动前逐字相同，正是玩家看到的"没有任何区别"。
- **同源缺陷（N-3）**：`src\train_cmd.cpp` 的 `R3RConsistReferenceEngine()` 循环第一句是 `if (!u->IsArticGroupMember()) break;`，名字检查在其后。C-2 提交点 `train_cmd.cpp:6123-6124` 的写法是 `new_head->SetArticGroupHead(); for (i; i + 1 < n_cars; i++) group[i]->SetArticGroupMember();`，原父车（反转后在链尾）**恰好在 `i == n_cars-1` 之外**……需按实际 `group`/`n_cars` 语义复核；若父车不带 `Member`，循环在检查它的名字之前就 break ⇒ 代表引擎退回链头 ⇒ 这也是 `build\R3R_debug.log` 里 **`C2-REF` 一行都没有**的解释。
- **修法（不依赖"玩家点的是哪一节"）**：把两张判据的因果翻过来——**"原型无名"是与角色位无关的事实**，先无条件判它；只有"原型是 Wagon（有名）"这一支才继续要求 `IsFrontEngine()`。代表引擎的遍历起点改为链头 `v->First()`（窗口可能开在任意一节上）。

**本轮改动（第 446 轮）**：

1. `src\train_gui.cpp`：`R3RDetailsNeedsRepresentativeEngine()` 改为「`v->GetEngine()->info.string_id == STR_UNDEFINED` ⇒ 无条件 true；否则仅当 `IsFrontEngine() && railveh_type == Wagon` ⇒ true」；`R3RConsistRepresentativeEngine()` 遍历起点由 `v` 改为 `Train::From(v->First())`，并加一次性只读探针 `R3R-NAME v= fe= eng= sid= rep= head=`（每车一行，手写 16 项查重表，避免新增 include）。
2. `src\train_cmd.cpp`：`R3RConsistReferenceEngine()` 把"检查名字/回传"提到 `IsArticGroupMember()` 边界判定**之前**，边界判定改为在该节检查完之后 `break`，保证组尾那一节（C-2 后的原父车）不会因角色位缺失被跳过。

**复测判据（第 446 轮新增 3 条）**：①日志出现 `R3R-NAME` 且 `sid=2`（=STR_UNDEFINED）时 `rep != eng`、`rep` 等于原父车的 engine_type；②详情窗引擎名显示父车型号，与反转前相同；③负判据：普通未反转列车不出现 `R3R-NAME`（链头原型有名 ⇒ 第一句即返回）。

**构建自证（第 446 轮，已通过）**：首跑因 `openttd.exe`（PID 12340）占用被闸门拦下（`build\R3R_incbuild.done` = `EXIT_CODE=97`，02:04:53，闸门只写 done 不写 log）；玩家关闭游戏后重跑 `R3R_fullbuild.cmd inc detach`：`GUARD: incremental is safe`、`[2/4] train_gui.cpp.obj` → `[3/4] train_cmd.cpp.obj` → `[4/4] Linking CXX executable openttd.exe`、`done` = `EXIT_CODE=0`（02:13:27）、error/FAILED/LNK 计数 **0**、`src\train_gui.cpp` 02:04:09 → obj 02:12:06、`src\train_cmd.cpp` 02:04:04 → obj 02:12:12、`build\openttd.exe` 02:13:17（52 891 136 B）晚于全部源码与 obj、`read_lints` 0、exe 命中 `R3R-NAME v=%d fe=%d eng=%d sid=%d rep=%d` 与 `C2-REF head=%d ref=%d`；发布树未编。

### 22.9 第 446 轮修法复测仍负 ⇒ 换轴重想（第 447 轮）

**玩家原话**：`判据依然为负，我们需要重新思考`

**先定性**：连续两轮（甲 = `railveh_type == Wagon` 判据、第 446 轮 = 解除 `IsFrontEngine()` 早退）都零效果，且两次的**失效形态完全相同**（与改动前逐字一致）。两次修法都只在"选哪个 `EngineID` 传给详情窗"这一层打转 ⇒ 该层大概率**不是**文本的真正来源。继续微调判据属重复行为，停止。

**猜测根因 H-R（待验证，本轮第一优先）**：`{ENGINE}` 的文本由 **`Engine::name`（缓存字符串）** 决定，而不是 `info.string_id`。

- 对 **NewGRF 车辆**，`info.string_id` 恒为 `STR_UNDEFINED`，名字由 `grffile` 关联（`0xD000 + local_id`）经 `ResetName` 生成。
- 若成立，则第 446 轮的两句判据行为是：①`if (v->GetEngine()->info.string_id == STR_UNDEFINED) return true;` 对**每一条 NewGRF 链恒真**（不是只对隐藏部件真）；②遍历条件 `info.string_id != STR_UNDEFINED` 对**全 NewGRF 链一节都找不到** ⇒ `named == nullptr` ⇒ `return v->engine_type` ⇒ **与改动前逐字相同**。
- 这一条能同时解释：为什么甲、乙两轮都零效果；为什么玩家看到"没有任何区别"；以及为什么 `C2-REF` 一行都没有。
- **可证伪的判据**：若 H-R 成立，第 446 轮的 `R3R-NAME` 探针应当在**每辆车**上打一行（每进程 16 行），且 `sid=2`、`rep==eng` 恒成立。若玩家日志里 `R3R-NAME` 一行都没有 ⇒ H-R 被证伪，问题在更外层（详情窗没走这个函数）。

**备选根因 H-R2（与 H-R 互斥，待判）**：`{ENGINE}` 的实现在 `strings.cpp` 的 `SCC_ENGINE` 分支里直接 `GetString(e->info.string_id)`。若成立，则 NewGRF 引擎**全部**会显示"(未定义字符串)"，与"普通列车名字正常"矛盾 ⇒ 除非普通列车走的是 `{WAGON}`/另一条串。此假设必须用代码排除，不能靠猜。

**修法必须换轴（三选一，待 H-R/H-R2 判定后落地）**：

- **丙-1（首选，改动小且可逆）**：判据从 `info.string_id` 换到"**渲染文本**"——即解析 `Engine::name`（或等价的 `GetEngineName()`），把"名字为空 / 等于 `STR_UNDEFINED_STRING` 的渲染结果"作为"该原型不可作为展示引擎"的判据；遍历时同样按 `name` 找第一个可展示的节。这样不依赖 NewGRF 与内置引擎的 string_id 语义差异。
- **丙-2（结构性特征）**：判据换成原型身份——`IsArticulatedPart()` 原型 / `cached_veh_length` 异常短 / `railveh_type` 组合；避开名字来源之争，但仍需 丙-1 的"可展示"判据来挑代表节。
- **丙-3（治本，风险最高）**：反转后不把隐藏部件留在链头——把链头 `engine_type` 换成父车原型。危险点：父车原型带 `ArticEngine` 回调，`ConsistChanged` 会**再造部件**，等于撤销 C-2 的打散；且缓存的容量/功率/费用全部随之改变。仅在 丙-1/丙-2 都失败时才考虑。

**本轮取证清单（3 条，做完再落结论）**：①`src\strings.cpp` 的 `SCC_ENGINE` 分支原文；②`Engine::name` 的赋值点（`ResetName`/`engine.cpp`）；③`src\train_gui.cpp:348/:358` 两条串的 `{ENGINE}`/`{WAGON}` 归属。

**必须向玩家索取的现场数据（没有它无法判定，别再猜）**：`build\R3R_debug.log` 里 `R3R-NAME` 与 `C2-REF` 两类行的**有无**（不是数量），以及详情窗那一行的**完整标签文字**（是「引擎名称」还是「车厢」）。

### 22.10 取证结案：判据错维（第 447 轮）

**三条硬证据（已读原文）**：

1. **`{ENGINE}` 的真实实现**（`src\strings.cpp:2172-2202`，`SCC_ENGINE_NAME`）是三段式：

```2182:2201:src/strings.cpp
					if (e->info.callback_mask.Test(VehicleCallbackMask::Name)) {
						uint16_t callback = GetVehicleCallback(CBID_VEHICLE_NAME, static_cast<uint32_t>(arg >> 32), 0, e->index, nullptr);
						...
					}

					GetStringWithArgs(builder, e->info.string_id, {});
```

   即：①`!e->name.empty() && e->IsEnabled()` ⇒ 用缓存名；②否则若设了 `VehicleCallbackMask::Name` ⇒ 用 `CBID_VEHICLE_NAME` 回调（**context 从 `arg >> 32` 传进去**，即 `EngineNameContext::VehicleDetails`）；③否则兜底 `GetString(info.string_id)` ⇒ `STR_UNDEFINED(2)` 就渲染成"(未定义字符串)"。

2. **`Engine::IsEnabled()` 与 string_id 无关**（`src\engine.cpp:153-156`）：`return info.string_id != STR_NEWGRF_INVALID_ENGINE && info.climates.Test(landscape);` ⇒ 铰接部件原型也**是** enabled，不能当作"是否可展示"的判据。

3. **`PackEngineNameDParam`**（`src\engine_type.h:253`）= `engine_id.base() | (context << 32) | (extra_data << 40)`，只有低 32 位是 EngineID ⇒ 传参本身没问题。

**结论：前两轮判据全部错维。** "(未定义字符串)" 的**充要条件**是：该原型的 `name` 为空 **且** 没设 `Name` 回调 **且** `info.string_id == STR_UNDEFINED`。前两轮分别用 `railveh_type == Wagon` 与 `IsFrontEngine()` 当判据，与这三段分支**毫无关系**。

**真根因 H-R3（成立）**：目标 GRF 的车名由 **`CBID_VEHICLE_NAME` 回调**按 `EngineNameContext::VehicleDetails` 生成，因此该 GRF **所有车辆原型的 `info.string_id` 都是 `STR_UNDEFINED`**。于是第 446 轮的遍历条件 `info.string_id != STR_UNDEFINED` 对**全链每一节都不成立** ⇒ `named == nullptr` ⇒ `return v->engine_type` ⇒ **与改动前逐字相同**。这同时解释：为什么甲、乙两轮都零效果，为什么 `C2-REF` 一行都没有，为什么玩家看到"没有任何区别"。

**修法（丙-1，复用 `{ENGINE}` 自己的判定逻辑）**：新增判据函数「该原型能否渲染出名字」，三条任一满足即"可展示"——`!e->name.empty()` / `e->info.callback_mask.Test(VehicleCallbackMask::Name)` / `e->info.string_id != STR_UNDEFINED`。用它替代全部 `string_id == STR_UNDEFINED` 判据（`train_gui.cpp` 两处 + `train_cmd.cpp` 一处）。这样无论 GRF 用 Action 4 名字还是 `CBID_VEHICLE_NAME` 回调、无论内置还是 NewGRF，都能命中父车。

**为什么这次不是又一次微调**：前两轮改的是"**什么时候**去找代表引擎"（门禁条件），本轮改的是"**怎么算找到**"（可展示性的定义）。后者是前两轮无效的直接原因。

**探针升级**：`R3R-NAME` 增加 `cb=`（Name 回调位）与 `nm=`（可展示），一次复测即可确认父车 `nm=1` 而部件 `nm=0`。

**复测判据（6 条，与 `R3R_KNOWN_ISSUES.md` 的 KI-640「C-2 复测判据」一致）**：

1. 反转后详情窗"引擎名称"显示父车（机车型号）名，不再"(未定义字符串)"；
2. "运载能力"显示整列合计而非"无"，且与反转前逐项相等；
3. 与同型号未反转列车比，功率/最大牵引力/重量/最高速度/运行费用逐项相等；
4. 日志出现 `C2-REF head=<新链头> ref=<原父车> refType= powWag=`，`ref` 恰为反转前原父车，且同车号对只一行；
5. 负判据：普通未反转链、纯车厢链、原生 GRF artic 组**不出现** `C2-REF`，读数逐字不变；
6. 反转后存档 → 读档，名字/容量/功率/段边界不变。

### 22.11 第 447 轮续：丙-1 落地（判据改为"能渲染出名字"）

**改动（仅两个 `.cpp`，未碰任何 `src/*.h`）**

1. `src\train_gui.cpp`：新增 `static bool R3REngineHasDisplayName(EngineID)`，三条回退逐条对应 `SCC_ENGINE_NAME`（`src\strings.cpp:2172-2202`）——`!e->name.empty() && e->IsEnabled()` / `e->info.callback_mask.Test(VehicleCallbackMask::Name)` / `e->info.string_id` 有效（非 `STR_UNDEFINED` 且非 `STR_NEWGRF_INVALID_ENGINE`）。`R3RDetailsNeedsRepresentativeEngine()` 与 `R3RConsistRepresentativeEngine()` 的遍历条件全部改用它（原 `info.string_id == STR_UNDEFINED` 判据已从这两个分支清零）。
2. `src\train_cmd.cpp`：同一判据（前置声明 + 定义，定义放在 `R3RConsistReferenceEngine()` 之后），三处替换——`R3RConsistReferenceEngine()` 的链头早退、组内挑"有名字的节"的过滤条件、`ConsistChanged()` 第一趟给 `first_engine` 选原型的条件（原 `Engine::Get(u->engine_type)->info.string_id == STR_UNDEFINED`）。
3. 探针升级：`R3R-NAME` 增加 `cb=`（`VehicleCallbackMask::Name` 位）/`nm=`（可展示）；`C2-REF` 增加 `headNm=`/`refNm=`。一次复测即可判"父车可展示、部件不可展示"。

**为什么前两轮无效（一句话）**：判据错维。目标 GRF 的车名由 `CBID_VEHICLE_NAME` 生成，`info.string_id` 对该 GRF 的**每一节**都是 `STR_UNDEFINED`，于是"跳过无名节"的条件把父车一起跳掉 ⇒ 找不到候选 ⇒ 返回链头 ⇒ 与改动前逐字相同。

**构建自证（第 447 轮续）**

- 入口：`R3R_fullbuild.cmd inc detach`（复用既有入口，未新建 `.cmd`）。
- `build\R3R_incbuild.done`：`EXIT_CODE=0`，`=== R3R build ended 2026/11/15 2:25:18 MODE=inc`。
- 日志尾部：`[4/4] Linking CXX executable openttd.exe`；`error C*/fatal error/FAILED:/build stopped` 计数 **0**。
- 时间戳：`src\train_cmd.cpp` 2:22:05 → `train_cmd.cpp.obj` 2:23:50；`src\train_gui.cpp` 2:22:27 → `train_gui.cpp.obj` 2:23:43；`build\openttd.exe` **2:25:04（52 891 136 B）** 晚于两者。
- `read_lints`：两文件 0 条。
- exe 字面量：`headNm=%d refNm=%d` 命中、`cb=%d nm=%d` 命中。

**状态**：已实现 + 已编译，游戏内复测待做（判据见 §22.10 的 6 条；第 4 条改为"出现 `C2-REF head=… ref=… headNm=0 refNm=1`"）。

**若本轮仍为负，需要玩家提供两样东西（缺一不可，否则不开新取证轴）**：

1. `build\R3R_debug.log` 里 `R3R-NAME` 行原文：有没有 `nm=1` 的节、`rep=` 是否等于该节（一条也行）。
2. 详情窗那一行的**完整标签文字**：是"引擎名称"（`{ENGINE}`）还是"车厢"。

判读表：`R3R-NAME` **一行都没有** ⇒ 详情窗并未走 `TrainDetailsInfoTab`（渲染路径假设本身错了，须另开取证轴）；有 `R3R-NAME` 但 `rep=` 已变、窗口仍显示未定义 ⇒ `{ENGINE}` 的参数不是我们以为的那个 `PackEngineNameDParam`。

### 22.12 第 448 轮：第三次"一模一样" ⇒ 把"零可观测变化"当证据（转入执行体核查）

**玩家原话**：`你的改码后，依然是一模一样的问题`

**三次失败的共同形态（这是本轮唯一硬证据）**：

| 轮次 | 改的东西 | 判据/探针 | 玩家实测 |
| --- | --- | --- | --- |
| 445 甲 | `railveh_type == Wagon` 门禁 | 无探针 | 还是有问题 |
| 446 乙/C-2 | 解除 `IsFrontEngine()` 早退 + 起点改 `First()` | `R3R-NAME`、`C2-REF` | 判据依然为负 |
| 447 丙-1 | 判据换成"能渲染出名字"（三条回退） | `R3R-NAME` 加 `cb=`/`nm=`、`C2-REF` 加 `headNm=`/`refNm=` | **一模一样** |

三轮改动分属三个不同层次（门禁 / 起点 / 可展示性定义），**若其中任何一轮真的被执行过，行为必然至少变一次**。"三次都逐字相同"这件事本身，比任何代码推断都强：要么**执行的不是这份 exe**，要么**详情窗根本不经过这些函数**。继续改判据属重复行为，停止。

**新疑点 D-1（最高优先，需先排除）：执行体不一致。** `build\openttd.exe` 是 2026-11-15 02:25:04（52 891 136 B）；若玩家启动的是发布树 `build-release\openttd.exe`、游戏安装目录里的副本、或此前拷出去的稳定版，则三轮改动一次都不会生效，且 `R3R-NAME` 永远不会出现在日志里——与观测完全一致。**判别只需一件事**：`build\R3R_debug.log` 的 mtime 是否晚于 02:25:04，以及该日志里是否出现过只在第 447 轮才存在的字面量（`R3R-NAME`、`cb=`、`nm=`、`headNm=`）。

**新疑点 D-2：渲染路径假设错误。** 若执行体确实是最新（日志晚于 02:25:04）却**一行 `R3R-NAME` 都没有**，则说明点开详情窗时根本没有调用 `R3RConsistRepresentativeEngine()`。可能原因：①点开的不是 `TrainDetailsInfoTab`（例如看的是车组窗口/编组窗口里的名字）；②该 GRF 用 Action 4 覆盖了详情窗整条字符串；③窗口开在"车厢"分支而该分支用的是 `{WAGON}` 而非 `{ENGINE}`，两处显示的是不同来源。

**新疑点 D-3：显示的是"整串 STR_UNDEFINED"而非"引擎名解析为空"。** 若那行是 `SetString(STR_UNDEFINED)` 级别的参数缺失，则同一行的 **Built / Value 也会一起空**，而不是只有引擎名那一格异常。这需要玩家给出那一行的完整文字（含 Built 与 Value 两格），一眼可判。

**本轮定性**：不改任何源码。前三次失败说明我在**错误的层**上反复打补丁；在确认 D-1/D-2 之前，任何代码改动都是盲改。

**取证顺序（严格按序，任一步得到否定结论即停止后续）**：

1. `build\R3R_debug.log` 的 mtime 与大小 ⇒ 判定玩家是否真的跑了 02:25:04 那个 exe。
2. 该日志中 `R3R-NAME` / `C2-REF` / `headNm=` 的有无 ⇒ 判定函数是否被调用。
3. 若 1 为真、2 为无 ⇒ 转入"找真正的渲染点"（从详情窗控件的 `SetString` 反向搜，而不是从 `{ENGINE}` 正向推）。

**仍需要玩家给的一句话**：详情窗那一行的完整文字（是只有"引擎名称"格是 `(未定义字符串)`，还是 Built/Value 也一起空）。

**第 1 步取证结果（2026-11-15 02:30 日志）**：

- `build\R3R_debug.log` mtime **2:30:35**、333 978 B、3556 行，**晚于** `build\openttd.exe` 的 **2:25:04** ⇒ **D-1（执行体不一致）排除**，玩家跑的就是本轮编出来的 exe。
- `build-release\openttd.exe` 存在但停在 **11/13 3:10:26**（23 724 032 B），未编，不参与本轮。
- 日志计数：`R3R-NAME` = **0**、`C2-REF` = **0**、`headNm=` = **0**、`ARTIC-SKIP` = **0**。
- ⇒ **D-2 成立**：点开详情窗时 `R3RConsistRepresentativeEngine()` 一次都没被调用，也没有任何 `ConsistChanged` 的 C-2 探针。三轮改动"零可观测变化"由此得到解释——它们所在的代码路径在玩家的操作里从未执行。
- **下一步必须先排除 D-4（探针被编译剔除）**：若 `R3RDbgWrite` 受宏控制而本树为 OFF，则"0 条"也可能只是看不到。判据=日志里是否还有**任何**老探针行（`RESPACE-AFTER-EDIT` / `CHAIN-ATTRS` / `TTB-PROBE` / `SPD-DROP`）。老探针有 ⇒ D-2 坐实；老探针也无 ⇒ 整个探针系统被剔，此前所有"日志没有"的结论一律作废。

### 22.13 第 448 轮 D-4 排除 + 新证据（探针过滤器把 `C2-REF` 吞了）

**D-4 排除（探针层在跑）**：`build\R3R_debug.log` 计数 `SPD-DROP`=**26**、`DEPOT-ARR`=**27**、`SEGID`=**60**。老探针大量存在 ⇒ `R3RDbgWrite` 未被宏剔除，本树探针层正常工作。

**新证据 1（本环境第 4 次踩同一个坑）**：`src\r3r_perf.h` 的 `R3RDbgTagAllowed()` 是**标签白名单**，只放行首 token 命中 `RESV/UNRES/EXTEND/LOOP/R3R-RES/COUPLE/CPL/FOLD/SPLICE/ARTIC/SKIP-/UNIT/CLONE/ORD/CTRL/AUTO-/OWNER/NAME/RESERVE/DECOUPLE/SPD/WP/DUALHEAD/TTB/DEPOT/SEGID/SEGROW/LEGACY/CGRP/GRP` 或以 `"R3R-"` 开头（`n>=4 && strncmp(fmt,"R3R-",4)==0`）的行。于是：

| 探针 | 首 token | 是否放行 | 日志中 0 的含义 |
|---|---|---|---|
| `R3R-NAME v=…` | `R3R-NAME` | **放行**（"R3R-" 前缀） | **真 0** |
| `C2-REF head=…` | `C2-REF` | **被吞**（"C2-R" 不匹配任何项） | **假 0，证据作废** |
| `RESPACE-AFTER-EDIT` | `RESPACE-AFTER-EDIT` | 被吞 | 假 0 |
| `CHAIN-ATTRS` | `CHAIN-ATTRS` | 被吞 | 假 0 |
| `TTB-PROBE` | `TTB-PROBE` | 放行（"TTB"） | 真 0 |

⇒ §22.12 里写的"`C2-REF`=0 ⇒ 没有任何 `ConsistChanged` 的 C-2 探针"**不成立**，`R3RConsistReferenceEngine()` 是否执行过至今**无数据**。同理此前多轮"日志里没有 RESPACE-AFTER-EDIT/CHAIN-ATTRS ⇒ 没跑"的推论也一律作废。

**新证据 2（`R3R-NAME` 的 0 仍不可判）**：`R3RConsistRepresentativeEngine()` 的探针写在**早退之后**（`:313` 早退 `if (!R3RDetailsNeedsRepresentativeEngine(v)) return v->engine_type;`，`:328` 才打日志）。所以 `R3R-NAME`=0 有且只有两种解释，且两者导出的修法完全相反：

- **(a) 函数从未被调用** —— 该次会话没打开详情窗，或详情窗绘制不经过 `TrainDetailsInfoTab`（例如那一行来自别的窗口/别的 tab）。修法必须换地方。
- **(b) 函数被调用但每次都在 `:313` 早退** —— 意味着 `R3REngineHasDisplayName(v->engine_type)` 返回 true。可玩家明明看到 `(未定义字符串)`，两者矛盾，只有在"`callback_mask.Test(Name)` 撒谎"时自洽。

**猜测根因 G-5（待验证，本轮主要怀疑对象）**：`R3REngineHasDisplayName()` 的 fallback 2 直接 `if (callback_mask.Test(VehicleCallbackMask::Name)) return true;` —— 这只证明"这个原型挂了 name 回调"，**不证明回调能返回有效名**。`src\strings.cpp:2182-2201` 的真实逻辑是：

```
if (callback_mask.Test(Name)) {
    callback = GetVehicleCallback(CBID_VEHICLE_NAME, context, 0, e->index, nullptr);
    if (callback == 0x40F) → GRF 文本栈，成功；
    else if (callback < 0x400) → GRFSTR_MISC_GRF_TEXT + callback，成功；
    /* 其它（含 callback == 0、callback >= 0x400 且 != 0x40F）→ 继续往下 */
}
GetStringWithArgs(builder, e->info.string_id, {});   // string_id == STR_UNDEFINED ⇒ "(未定义字符串)"
```

即"有回调但回调没给出名字"时，SCC_ENGINE_NAME 会**落到 `string_id`** 并渲染出 `(未定义字符串)`。隐藏铰接部件原型正属此类 ⇒ `R3REngineHasDisplayName` 误判 true ⇒ `R3RDetailsNeedsRepresentativeEngine()` 返回 false（当 `IsFrontEngine()==false`）或 true（当 `IsFrontEngine() && Wagon`）——

- 若详情窗开在**非链头**节（`IsFrontEngine()==false`）：返回 false ⇒ 早退 ⇒ 不遍历、不打日志 ⇒ 与 (b) 的观测完全吻合；
- `R3RConsistReferenceEngine()`（`train_cmd.cpp`，喂给 `ConsistChanged` 决定 `rvi_v`/`first_engine`/capacity）**同样**因这个误判在链头就 `return head` ⇒ **gcache 仍按隐藏部件原型计算** ⇒ 一次解释三症状里的两个（运载能力=无、物理性能偏差）。

**判定性实验（一次复测切开 (a)/(b) 与 G-5）**，三处探针/判据改动，不做任何"再微调门禁"：

1. `R3RConsistRepresentativeEngine()` 的探针**前移到函数第一行**（早退之前），字段补 `needs=`，这样 (a)/(b) 一眼可分。
2. `TrainDetailsInfoTab()` 入口加一行节流探针 `R3R-DINFO v= line= rvi= fe=` ⇒ 证明详情窗绘制到底走不走这个函数。
3. `R3REngineHasDisplayName()` 的 fallback 2 从"有回调就算有名字"改为**真实渲染测试**：直接调 `GetString(STR_ENGINE_NAME, PackEngineNameDParam(...))`，与 `GetString(STR_UNDEFINED)` 比对。这是与 `SCC_ENGINE_NAME` **逐字同一路径**的判据，不再依赖对回调的猜测。`train_cmd.cpp` 里的同名函数同步修改。
4. `C2-REF` 改名 `R3R-C2REF`（跨过白名单），`headNm=` 等字段保留。

**预期**：复测若路径正确，日志必现 `R3R-DINFO` + `R3R-NAME`（`needs=1 rep≠v`），且详情窗显示原父车名、运载能力恢复；若 `R3R-DINFO` 仍为 0 ⇒ 详情窗不经过 `TrainDetailsInfoTab`，下一步改从 `VehicleDetailsWindow` 的 `UpdateVehicleDetails()`/`SetString` 反向搜，不再动 `{ENGINE}` 侧。

**构建自证（第 448 轮续）**：复用 `R3R_fullbuild.cmd inc detach`（未新建 `.cmd`）；`tasklist /FI "IMAGENAME eq openttd.exe"` 先确认无进程；护栏 `GUARD: incremental is safe`；`[2/4] train_gui.cpp.obj` → `[3/4] train_cmd.cpp.obj` → `[4/4] Linking CXX executable openttd.exe`；`build\R3R_incbuild.done` = `EXIT_CODE=0`（`=== R3R build ended 2026/11/15 2:40:32 MODE=inc`）；错误计数 **0**；`src\train_cmd.cpp` 02:36:58 → `train_cmd.cpp.obj` 02:38:22、`src\train_gui.cpp` 02:37:20 → `train_gui.cpp.obj` 02:38:17、`build\openttd.exe` **02:40:09（52 918 272 B）** 晚于全部源码与 obj；`read_lints` 两文件 0 条；exe 字面量 5/5 命中（`R3R-DINFO v=`、`cb=%d nm=%d needs=%d`、`R3R-REPICK v=`、`R3R-C2REF head=`、旧 `C2-REF head=` 已消失）；发布树未编（仍为 11/13 3:10:26 那一版）。

**一句话结论**：前三轮不是"判据选错了链节"，而是**判据本身被 `callback_mask` 欺骗**（隐藏铰接部件挂了 Name 回调却不给名字）+ **唯一的验证探针被标签白名单吞掉**，两者叠加造成"改了三次、日志里什么都没有、画面一模一样"的假象。本轮把判据换成与 `SCC_ENGINE_NAME` 逐字同路的真实渲染测试，并把探针移到早退之前、跨过白名单。

### 22.14 第 449 轮：名字行已修复 ⇒ 转入其余属性（本轮起点）

**玩家原话（2026-11-15）**：「现在"(未定义字符串)"已经消失，但列车其它属性依然错误」。

**已确认通过的部分（第 448 轮判据有效）**：

- 真实渲染测试（`GetString(STR_ENGINE_NAME, …) != GetString(STR_UNDEFINED)`）确实切开了 `callback_mask` 撒谎的情形 —— 这是 G-5 假设成立的实机证据。
- ⇒ `{ENGINE}` 渲染侧（`TrainDetailsInfoTab` 的 `R3RConsistRepresentativeEngine()` / `R3RDetailsNeedsRepresentativeEngine()`）**不再动**，已经不是本届缺陷的现场。

**剩余症状（玩家口径："列车其它属性依然错误"）**，按详情窗行拆分：

| 行 | 期望（C-2 换向后） | 现状（未实测，玩家口径"错误"） |
|---|---|---|
| 运载能力 | 整列合计（各节 `cached_capacity` 之和） | 不是合计（缺/偏小） |
| 功率 | 代表引擎原型的 `power` | 与同型号未反转列车不一致 |
| 牵引力 | 代表引擎原型的 `tractive_effort` | 同上 |
| 重量 | 各节 `cached_weight` 之和 | 同上 |
| 最高速度 | 代表引擎原型的 `max_speed` | 同上（疑为 `get_speed()` 未动，见 H-9） |
| 运行费用 | `cached_running_cost` / `rvi_v->running_cost` | 同上 |

**关键结构性事实（本轮取证轴）**：详情窗这六行**都不读** `first_engine` 渲染串，而读两处：

1. `GroundVehicleCache` / `VehicleCache` 的 `cached_capacity` / `cached_weight` / `cached_power` / `cached_max_speed` / `cached_running_cost` —— 由 `ConsistChanged()` 一次算定；
2. `TrainDetailsInfoTab()` 内直接 `RailVehInfo(v->engine_type)` 或 `GetVehicleProperty(v, …)` 的分支 —— 若该分支对"链头原型无名"的链**仍取链节自己的 `engine_type`**，则与 `ConsistChanged` 的锚点改动不同步。

⇒ 名字行修好只证明「**渲染锚点**」找到了父车；**不等于**「**数据锚点**」也找到了父车。两者是两套独立代码，必须分别验证。

**猜测根因 G-6（待验证）**：`R3RConsistReferenceEngine()` 在第 448 轮改成"名字可渲染才返回"，但 `ConsistChanged()` 里除 `rvi_v` / `first_engine` 之外，还有若干处直接用 `u->engine_type`（第一趟循环里的 `IsEngine()` 分支、`pow_wag_power` 赋值、`cached_max_speed` 取 `get_speed()`），以及 `train_gui.cpp` 内的 `rvi->railveh_type == Wagon` 分支会**自己再取一次 `RailVehInfo`**。若这些点没有一并改锚，则"容量不合计 + 物理属性偏差"会同时存在，且与名字行互不影响。

**猜测根因 G-7（待验证，容量专项）**：运载能力"不是合计"更可能是另一个独立缺陷 —— 详情窗 `TrainDetailsInfoTab()` 的容量显示走 `R3RDetailsWholeConsistCapacity()` 或者 `GetCapacityOfVehicle` 分支，其**入口条件**（例如 `IsFrontEngine()` 或 `railveh_type == Engine`）对 C-2 反转链不成立，于是退回只显示本节容量。

**本轮取证顺序（严格按序，每步即落盘）**：

1. 读 `build\R3R_debug.log` 的 `R3R-C2REF` 行 ⇒ 看 `headNm= refNm= refEng= refType= powWag= n=`，判定 `R3RConsistReferenceEngine()` 在**链头**上是否已解析到有名字的父车（`ref!=head`、`refNm=1`）。
2. 读 `ConsistChanged()` 里 `cached_capacity` / `cached_power` / `cached_max_speed` / `cached_running_cost` 的赋值段 ⇒ 逐行确认取的是 `rvi_v` 还是 `u->engine_type`。
3. 读 `train_gui.cpp` 里这六行的取数分支 ⇒ 确认是否各有一份独立锚点。
4. 只改"数据锚点"，不动 `{ENGINE}` 侧；改完按既有三件套自证并追加本小节。

**复测判据（沿用第 448 轮 6 条，第 4 条收紧）**：

1. `R3R-DINFO` 必现（详情窗确实经过 `TrainDetailsInfoTab`）；
2. `R3R-NAME … needs=1 nm=0 cb=1`（判据真的在否掉隐藏部件）；
3. `R3R-REPICK rep≠eng repNm=1`（换到了父车原型）；
4. **画面四项一致**（名字 + 运载能力 + 功率/牵引力 + 最高速度，与同型号未反转列车逐项相同）；
5. 普通未反转链 `needs=0` 且各项**逐字不变**；
6. 存档往返后仍不变（`FakeDualHead`/`ArticGroupHead` 不丢）。

### 22.15 第 449 轮取证结果：三项发现（两项为硬结论，一项为疑点）

**REG-449-A（疑点，必须登记）：`build\R3R_debug.log` 不能作为本轮判据来源。**
本轮按 §22.14 第 1 步读日志：文件仅约 23 行、末尾是新一轮空会话痕迹，**没有任何一行 `R3R-C2REF`，也没有 `R3R-NAME` / `R3R-DINFO`**。
⇒ 按第 448 轮刚立的纪律，"没有该行"**不能**推成"`R3RConsistReferenceEngine()` 未解析到父车"：日志是空会话（详情窗从未打开 / 新一轮启动），探针自然一行不打。
⇒ **结论：第 449 轮的取证第 1 步判定为「数据缺失」，不是「判据为负」。** 需要玩家重跑一次"反转后点开详情窗"的场景再取日志，否则后续任何"参考引擎解析对不对"的推断都没有实证支撑。这一条尤其重要，因为第 445~447 轮连续三轮的失败恰恰就是"把没有日志当成反面证据"。

**C-449-1（硬结论，代码级，不需实机）：`TrainDetailsInfoTab()` 根本不含玩家说的那五项属性。**
`src\train_gui.cpp:384-476` 全文只有 3 个行号分支：
- `line 0`：`{ENGINE}` 名字 + 建造年 + 价值 + 最高速度（`STR_VEHICLE_DETAILS_TRAIN_ENGINE_BUILT_AND_VALUE_AND_SPEED` / `..._WAGON_VALUE_AND_SPEED`）；
- `line 1`：可靠性 + 故障次数；
- `line 2`：当前状态（运行/故障/需维修）；
- `default: NOT_REACHED()`。
**没有**"运载能力 / 功率 / 牵引力 / 重量 / 运行费用"任何一行。
⇒ 玩家口径的"列车其它属性"落在**另外两处**，与第 445~448 轮改动的文件基本无交集：
1. `src\vehicle_gui.cpp` 的 `WID_VD_TOP_DETAILS`（详情窗上部通用区）：`STR_VEHICLE_INFO_MAX_SPEED`、`STR_VEHICLE_INFO_WEIGHT_POWER_MAX_SPEED`、`STR_VEHICLE_INFO_WEIGHT_POWER_MAX_SPEED_MAX_TE`（:`3626-3628` 的尺寸预留即这三条的取数分支）＋ `GetRunningCostString()`（:`3630`）；另含 `STR_VEHICLE_INFO_WEIGHT_RATIOS`（:`3658`，功率重量比 / TE 重量比）。
2. `src\train_gui.cpp` 的 `TDW_TAB_CAPACITY`（`_cargo_summary`）与 `TDW_TAB_PERF`（`STR_VEHICLE_DETAILS_TRAIN_TOTAL_WEIGHT`、`..._MAX_SPEED`、`..._POWER_WEIGHT_RATIO`）。
⇒ **这解释了"名字修好了、其余一模一样"：第 445~448 轮的改动面（`{ENGINE}` 渲染锚点 + `ConsistChanged` 的 `rvi_v`/`first_engine`）在结构上不经过这五项的取数行。** 不是"改错了"，是"没改到"。

**C-449-2（硬结论＋新猜测 G-8）：「运载能力不是合计」这条路已经通了，剩余偏差只能来自数据层。**
`src\train_gui.cpp:371-374` 的 `R3RDetailsWholeConsistCapacity(v)` 就是 `return R3RDetailsNeedsRepresentativeEngine(v);` —— **同一个判据**。
而第 448 轮已把 `R3RDetailsNeedsRepresentativeEngine()`（:`301-314`）改成"`R3REngineHasDisplayName(v->engine_type)` 为假 ⇒ 无条件 `return true`"。
C-2 反转链的链头正是"隐藏铰接部件"（原型 `string_id == STR_UNDEFINED`、画不出名字）⇒ `needs` 必为 `true` ⇒ `whole_consist` 必为 `true` ⇒ `DrawTrainDetails` 在 `:599` / `:705` 两处 `break` 只画一行、`GetTrainDetailsWndVScroll` 的 `num` 也按一行算，且那一行是 `:517-538` 的 `add_unit` 对**整列**累加 `u->cargo_cap`。
⇒ 汇总分支**已经生效**，所以"容量不是合计"的剩余可能只有两条：
- **G-8a**：`u->cargo_cap` 本身被 `ConsistChanged()`/`CargoChanged()` 算小了（数据锚点仍取错原型的 `capacity`）；
- **G-8b**：`:518` 的 `if (!u->GetEngine()->CanCarryCargo()) return;` 把某些节整节跳过（`GetEngine()` 用的是**该节自己的** `engine_type`，隐藏铰接部件原型若 `capacity == 0` 且无 refit 选项就会被丢弃）；
- 次可能 **G-8c**：玩家看的是 `WID_VD_TOP_DETAILS` 或 `TDW_TAB_PERF` 的某一读数，而非 `TDW_TAB_CAPACITY`。
三者都待实机/日志区分，暂不改码。

**本轮未做（刻意）**：不碰 `{ENGINE}` 侧、不改 `ConsistChanged`、不改 `get_speed()`（H-9 仍挂）、不改 `newgrf_engine.cpp` 组内名次镜像、不碰 U-4（换 `engine_type`，高风险）。

**下一步（严格按序，每步落盘）**：
1. 读 `src\vehicle_gui.cpp` 的 `WID_VD_TOP_DETAILS` 绘制体（`DrawVehicleDetails` 一带，紧接 `:3595-3666` 的 `UpdateWidgetSize` 之后），逐行抄出"重量/功率/最高速度/牵引力/运行费用"各自的取数字段（`gcache.cached_power` vs `RailVehInfo(v->engine_type)` vs `GetDisplayPower()`），确认是否与 `ConsistChanged` 的锚点改动不同步。
2. 读 `ConsistChanged()` 中 `cached_capacity` / `cached_power` / `cached_max_speed` / `cached_running_cost` 的赋值段与 `CargoChanged()` / `PowerChanged()`，判定 G-8a。
3. 判定 `R3RConsistReferenceEngine()` 的实际返回：**必须由玩家重跑一次反转场景**（REG-449-A），在此之前不写任何"解析成功/失败"的结论。
4. 只改"数据锚点"，改完按三件套自证并追加 §22.16。

### 22.16 第 449 轮：玩家已回日志（`build\R3R_debug.log` 44 行 / 3078 B / 03:03:33）——读数与判定

**REG-449-A 已解除**：日志不再是空会话。本次是一次完整的"反转 → 打开详情窗"会话，可作判据来源。原文锚点（行号为该日志行号，逐字）：

```
15: ARTIC-FLIP head=3 n=4 span=0..3 primary=1 flipped=4 real=1 old=0 new=3 move=1
16: R3R-C2REF head=3 ref=0 refEng=363 refType=0 powWag=0 n=0 db=0 headNm=0 refNm=1
17: R3R-DINFO v=3 line=0 rvi=0 fe=1 eng=364
18: R3R-NAME v=3 fe=1 eng=364 sid=65535 cb=0 nm=0 needs=1
19: R3R-REPICK v=3 rep=363 eng=364 repNm=1
20: R3R-DINFO v=3 line=1 rvi=0 fe=1 eng=364
21: R3R-DINFO v=3 line=2 rvi=0 fe=1 eng=364
```

**C-449-3（硬结论，G-6/G-7 证伪）：数据锚点**已经**切到父车 363，不是没切。**
`R3R-C2REF`（探针位于 `src\train_cmd.cpp:654-674`，字段由 `:665-672` 定义）输出 `ref=0 refEng=363 refNm=1` ⇒ `r3r_ref_engine` = veh0（engine_type=363，有名字）、`head=3`（engine_type=364，`headNm=0` 画不出名字）。⇒ §22.15 列的 G-6（"`ConsistChanged` 仍按旧锚点"）与 G-7（"`get_speed()` 取错原型"中的"锚点未切"部分）**不成立**，整条数据锚点切换链路是生效的。**"其余五项仍错"必须另有原因，不许再往"参考引擎没找到"方向改码。**

**C-449-4（硬结论，H-9 由猜测升为定位）：信息 tab 走的是"引擎分支"，`line 0` 的速度与价值仍读链头原型 364。**
`R3R-DINFO` 三行的 `rvi=0`（= `RailVehInfo(v->engine_type)->railveh_type`）。若 `RailVehicleType::Engine == 0`（待核枚举定义），则 `TrainDetailsInfoTab()` 走 `else`（引擎）分支 ⇒ `line 0` 用 `STR_VEHICLE_DETAILS_TRAIN_ENGINE_BUILT_AND_VALUE_AND_SPEED`，其中 **名字**已被 `R3R-REPICK` 换成 363，但 **`v->value` 和 `get_speed()`** 仍按 `v->engine_type=364` 取 ⇒ 同一行内"名字属父车、价值/最高速度属隐藏铰接部件"的混合读数。这正是玩家看到的"名字对了、其它属性仍不对"的最小可解释形态。
⇒ 下一步必须核 `RailVehicleType` 枚举顺序，并逐行确认 `:384-476` 中 `v->value` / `get_speed()` / 可靠性 三处的取数对象。

**C-449-5（硬结论，取证范围收窄）：本次会话**没有**打开"容量/性能"tab。**
`R3R-DINFO` 只出现 `line=0/1/2`（信息 tab 的 3 行），没有任何 capacity/perf 相关行。⇒ 玩家口中"运载能力不是合计"若指 `TDW_TAB_CAPACITY`，则本次读数**不覆盖**该 tab；若玩家看的是信息 tab，那信息 tab **结构上就没有容量行**（§22.15 C-449-1）。两种情形都不支持"容量汇总分支失效"的假设。

**REG-449-B（疑点，必须登记，防重犯 D-3 型"假 0"）：`R3R-C2REF` 的 `n=0` 不能当作"功率归零"。**
`n=%u` 打的是 `this->gcache.cached_power`（`:670`），而该行位于 `ConsistChanged()` 中段（`:654`），`PowerChanged()`/`CargoChanged()` 在其**之后**才跑 ⇒ 此处 `cached_power` 很可能尚未写入或已被清零。**在拿到 `PowerChanged()` 之后的读数之前，禁止引用 `n=0` 作为证据。**

**C-449-6（读数事实，非结论）：`DEARTIC-SPLIT` 的 `wtot/ptot` 在反转后从 66/476 变为 16/119，与"整列只剩 1 节"无关。**
`:6 DEARTIC-SPLIT head=0 n=4 wtot=66 wq=16 wr=2 ptot=476 pq=119` vs `:24 head=3 n=1 wtot=16 wq=16 wr=0 ptot=119 pq=119`。字段语义见 `src\vehicle_cmd.cpp:575-586`：`wtot=total_weight`、`n=n_total`（组内节数）。首次拆解把 66 均分成 18/16/16/16（正好 66），之后每节已独立持有 `weight_override`，故再拆时 `wtot=16`（自己那 16）属**正确**读数，`wq=16 wr=0` 也就是"1 节吃掉全部余数"。⇒ **不能据此断言"反转把整列重量算成 1 节"**；本轮自查已把这条初步假设降级为无效。
- 附带成立的一条（仅记录，不追）：`:8-:10` 显示 idx=1/2 同时 `gm=1 gh=1`（既是成员又是组头），`:25-:26` 显示 idx=3/2 都是 `gh=1`。`gh=IsArticGroupHead()` 多打属**存量角色位污染**，与 KI-640 六项属性的因果未验证，暂登记不修（避免再开一条并行战线）。

**本轮判定汇总**：
| 项 | 判定 | 依据 |
|---|---|---|
| 参考引擎是否解析到父车 | **已解析（363）** | `:16 refEng=363 refNm=1` |
| 名字行为何已修好 | 渲染锚点 `R3R-REPICK` 生效 | `:19/#22/#23/#44` |
| 链头为何画不出名字 | `sid=0xFFFF` + `cb=0`（无回调） | `:18` |
| 其余五项是否显示层问题 | 至少"价值/最高速度"是（信息 tab 引擎分支） | `:17/:20/:21 rvi=0` |
| 容量 tab 是否失效 | 本次无读数，不能判 | `:17-:23` 只有信息 tab |
| `cached_power=0` | **疑点，不可用** | 探针早于 `PowerChanged()` |

**下一步（严格按序，每步落盘）**：
1. 核 `RailVehicleType` 枚举（`src\rail.h` 或 `vehicle_type.h`）确认 `0` 是否 `Engine`。
2. 读 `src\vehicle_gui.cpp` 的 `WID_VD_TOP_DETAILS` 绘制体，逐行抄出"重量/功率/最高速度/牵引力/运行费用"的取数字段——这是玩家肉眼所见的主区域，且日志零覆盖。
3. 把 `R3R-C2REF` 探针**后移**到 `PowerChanged()`/`CargoChanged()` 之后（或增打 `cached_capacity`/`cached_weight`/`cached_max_speed`/`cached_running_cost`），才能对"五项数值"取证；在此之前不写任何"数值归零"类结论。
4. 只改"显示取数的对象"，不改 `ConsistChanged` 结构、不碰 `{ENGINE}` 侧。

### 22.17 第 449 轮续（二）：枚举订正 + 上部详情区取数字段抄录

**C-449-7（硬结论，订正 §22.16 的措辞）：`RailVehicleType` 的 `0` 不是 `Engine`，是 `Singlehead`。**
`src\engine_type.h:33-37` 原文：

```
enum class RailVehicleType : uint8_t {
	Singlehead, ///< indicates a "standalone" locomotive
	Multihead,  ///< indicates a combination of two locomotives
	Wagon,      ///< simple wagon, not motorized
};
```

⇒ `0=Singlehead`、`1=Multihead`、`2=Wagon`。于是本轮日志的两个读数必须重读：
- `:17 R3R-DINFO v=3 line=0 rvi=0` ⇒ 链头原型 364 的 `railveh_type == Singlehead`（**独立机车**），不是 Wagon。
- `:16 R3R-C2REF … refType=0` ⇒ 父车原型 363 也是 `Singlehead`。

**这同时解释了两件历史事**：①§22.7 记的"甲失败根因 = 甲的判据是 `railveh_type == Wagon`，而本 GRF 铰接部件原型不是 Wagon"在这里读数为 `Singlehead`，完全吻合；②`R3RDetailsNeedsRepresentativeEngine()` 的旧判据（`rvi->railveh_type == Wagon`）对本链恒假 ⇒ 该链能否走"代表性引擎"分支**完全取决于**第 448 轮新加的"原型画不出名字"判据，不存在第二条兜底。**结论不变**（C-449-4 仍成立：信息 tab 走引擎分支，因为 `Singlehead` 不是 `Wagon`），但**枚举名必须写成 Singlehead，不许再写 Engine**。

**C-449-8（硬结论，代码级）：`WID_VD_TOP_DETAILS`（玩家肉眼主视区）的全部数值都取链头的 `v` 与链头 `gcache`，没有任何代表性引擎重定向。**
`src\vehicle_gui.cpp:3786-3883` 逐行：

| 行 | 显示项 | 取数对象 |
|---|---|---|
| :3791/:3795 | 运行费用 | `v->GetDisplayRunningCost()` |
| :3799 | 最高速度 | `PackVelocity(v->GetDisplayMaxSpeed(), v->type)` |
| :3805 | 重量/功率/最高速度 | `gcache->cached_weight`、`gcache->cached_power`（`gcache = v->GetGroundVehicleCache()`） |
| :3807 | 重量/功率/速度/牵引力 | 同上 + `gcache->cached_max_te` |
| :3826/:3828 | 功率重量比 / TE 重量比 | `Train::From(v)->gcache.cached_power`、`cached_max_te`、`cached_weight` |
| :3838 | 列车长度 | `gcache->cached_total_length` |
| :3867-3872 | 可靠性/故障 | **遍历整链** `for (w = v; w; w = w->Next())`，仅 `IsEngine() || IsMultiheaded()` 的节参与 |

⇒ 与第 445~448 轮改动面的关系：
- `cached_weight` / `cached_power` / `cached_max_te` / `cached_total_length` 由 `ConsistChanged()` + `PowerChanged()` 写，**数据锚点已切到 363**（`:16 refEng=363`），所以"看起来应该已经对了" —— 若实机仍错，则错在 `PowerChanged()` 内部（下一步核）。
- `GetDisplayRunningCost()` / `GetDisplayMaxSpeed()` 是 **`v` 自己的成员函数**，完全不经过 `ConsistChanged` 的锚点分支；若它们的实现按 `RailVehInfo(v->engine_type)`（=364）取值，则**这两个数必然还是错的**，且第 445~448 轮在结构上不可能修到它们。这就是"名字修好、其余仍错"最直接的候选解释。
- 可靠性行已经过 R3R 改造（遍历整链），但 `w->GetEngine()->reliability` 对每节仍取**该节自己的**原型 ⇒ 若 364 无名字原型与 363 的可靠性不同，平均值仍偏；这是次要项。

**下一步（继续，仍不改码）**：核 `GetDisplayRunningCost()` / `GetDisplayMaxSpeed()` 的定义（`src\vehicle_base.h` / `src\ground_vehicle.hpp` / `src\train.h`），确认它们是读 `gcache` 还是读 `RailVehInfo(engine_type)`。这一条决定"上部区两个数值要不要重定向"。

### 22.18 第 449 轮续（三）：根因收敛——信息 tab 的 line 0/1 是唯一"名用父车、数用隐藏部件"的混合行

**C-449-9（硬结论）：上部区（`WID_VD_TOP_DETAILS`）的两个"嫌疑值"其实都读缓存，不是读原型。**
- `Train::GetDisplayMaxSpeed()` = `this->vcache.cached_max_speed`（`src\train.h:344`），而 `vcache.cached_max_speed` 由 `ConsistChanged()` 在 `src\train_cmd.cpp:677` 写入（`max_speed` 是循环内 `GetTrainVehicleMaxSpeed(u, rvi_u, this)` 取 min）；`GetTrainVehicleMaxSpeed()`（`src\train_cmd.cpp:278-293`）的取值优先级是 **`u->max_speed_override`（去铰接化烘焙值）> `GetVehicleProperty(u, PROP_TRAIN_SPEED, rvi_u.max_speed)`**。
- `Vehicle::GetDisplayRunningCost()` = `this->GetRunningCost() >> 8`（`src\vehicle.cpp:4862-4867`）；`Train::GetRunningCost()`（`src\train_cmd.cpp:22862-22878`）遍历 `GetNextVehicle()`，**每节按自己的 `GetEngine()` 累加**。
- ⇒ 这两个值在反转前后**逐节集合相同**（同一批车，只是链序与链头变了），所以从算法上看反转不应该改变它们；若实机真变了，只可能是 `max_speed_override`/`GetVehicleProperty` 的回调读到了"位置/链头"类 NewGRF 变量（次要嫌疑）。
⇒ **上部区不是本轮的首要嫌疑**，先不要改它。

**C-449-10（硬结论，根因收敛）：`TrainDetailsInfoTab()`（`src\train_gui.cpp:384-441`）的 line 0 / line 1 是唯一仍整行读 364 原型的地方，且与已修好的名字同处一行。**
原文（`:386`、`:407-410`、`:426-441`）：

```
const RailVehicleInfo *rvi = RailVehInfo(v->engine_type);        // 364
auto get_speed = [&]() -> uint16_t {
    const bool show_speed = !UsesWagonOverride(v) && (_settings_game.vehicle.wagon_speed_limits || rvi->railveh_type != RailVehicleType::Wagon);
    return show_speed ? GetVehicleProperty(v, PROP_TRAIN_SPEED, rvi->max_speed) : 0;
};
...
case 0:  name_param = PackEngineNameDParam(R3RConsistRepresentativeEngine(v), ...);   // 363（已修）
         speed = get_speed();                                                          // 364
         draw(STR_VEHICLE_DETAILS_TRAIN_ENGINE_BUILT_AND_VALUE_AND_SPEED, name_param, v->build_year, v->value, speed);
case 1:  draw(STR_VEHICLE_INFO_RELIABILITY_BREAKDOWNS, ToPercent16(v->reliability), ToPercent16(v->GetEngine()->reliability), v->breakdowns_since_last_service);
```

逐项归属（链头 v=3、engine 364；代表车 = 363 那节）：

| 项 | 现状取数 | 归属 |
|---|---|---|
| 引擎名 | `R3RConsistRepresentativeEngine(v)` | **363**（第 448 轮已修） |
| 建造年 `v->build_year` | 链头 veh3 | 364 |
| 价值 `v->value` | 链头 veh3 | **364** |
| 最高速度 `get_speed()` | `rvi`=364 + `GetVehicleProperty(v=veh3,…)` | **364** |
| 可靠性（line 1） | `v->reliability` / `v->GetEngine()->reliability` | **364** |

⇒ **这就是玩家"名字已对、其它属性依然错误"的直接来源**：同一行里名字来自父车 363、数字来自隐藏铰接部件 364，视觉上就是"型号对、数值不对"。`Singlehead != Wagon`（C-449-7）保证它一定走 `:426` 的引擎分支、一定渲染速度，所以这一行必然出现。

**修法（下一轮落地，最小改动，只动 `train_gui.cpp`）**：
1. 在 `TrainDetailsInfoTab()` 入口取一次 `const Train *rep = R3RConsistRepresentativeEngine(v);`（现有函数，已具备"链头原型无名字则找同组有名字节"的逻辑）。
2. `get_speed()` 内改成 `RailVehInfo(rep->engine_type)` + `GetVehicleProperty(rep, PROP_TRAIN_SPEED, rep_rvi->max_speed)`。
3. `v->value` → `rep->value`、`v->build_year` → `rep->build_year`、line 1 的 `v->reliability` / `v->GetEngine()->reliability` → `rep->reliability` / `rep->GetEngine()->reliability`。
4. 负判据：`rep == v`（普通未反转链、或链头本身就有名字）时上述全部退化为原表达式，**逐字节不变**。
5. 不动 `WID_VD_TOP_DETAILS`、不动 `ConsistChanged`、不动 `get_speed` 的 `UsesWagonOverride` 判据。

**仍未确认（需实机）**：上部区的重量/功率/牵引力/长度是否也错。若玩家报"只剩这几项错"，则下一步才查 `PowerChanged()` 与 `max_speed_override` 的烘焙值。

### 22.19 第 449 轮续（四）：修法已落地 `src\train_gui.cpp`（唯一改动文件）+ 构建自证

**落地内容（三处，全部在 `src\train_gui.cpp`，未碰任何 `src\*.h`）**

1. **新增 `R3RConsistRepresentativeUnit(const Train *v)`**（紧邻 `R3RConsistRepresentativeEngine()` 之前）：把原来只返回 `EngineID` 的查找逻辑升级为返回**单元指针**——`R3RDetailsNeedsRepresentativeEngine(v)` 为假直接 `return v`；否则沿 `Train::From(v->First())` 用 `Next()` 走，优先取原型 `railveh_type != RailVehicleType::Wagon` 的有名字节，否则退回第一个有名字节，全无则 `return v`。
2. **`R3RConsistRepresentativeEngine()` 去重**：原内联的遍历 11 行删掉，改成 `const EngineID rep = R3RConsistRepresentativeUnit(v)->engine_type;`。行为逐字节等价（`named == nullptr` 时原式就是 `v->engine_type`，而新函数此时返回 `v`），探针 `R3R-NAME` / `R3R-REPICK` 位置与文案不变。
3. **`TrainDetailsInfoTab()` 改为按代表单元取数**：
   - 入口新增 `const Train *const rep = R3RConsistRepresentativeUnit(v);` 与 `const RailVehicleInfo *const rep_rvi = RailVehInfo(rep->engine_type);`（`rvi` 仍取 `v->engine_type`，分支判定不动）。
   - `get_speed` lambda 拆成 `get_speed_of(u, u_rvi)` + 两个薄包装：`get_speed()`（= `get_speed_of(v, rvi)`，给 wagon 分支）与引擎分支直接 `get_speed_of(rep, rep_rvi)`。
   - `case 0`：`rep->build_year` / `rep->value` / `get_speed_of(rep, rep_rvi)`。
   - `case 1`：`ToPercent16(rep->reliability)` / `ToPercent16(rep->GetEngine()->reliability)`；`v->breakdowns_since_last_service` 保留（那是本车的运行态计数，不是原型属性）。
   - 探针 `R3R-DINFO` 文案扩为 `... eng=%d rep=%d`，行内可直接看出该行有没有被重定向。
   - **wagon 分支（`:434` 起）除名字外一字未动**：本轮目标是"引擎行同源"，改 wagon 会扩大风险面。

**负判据（重要）**：`rep == v` 时 `rep->value` / `rep->build_year` / `rep->reliability` / `get_speed_of(rep, rep_rvi)` 与改前的 `v->...` / `get_speed()` **逐表达式等价**；普通未反转链、原生铰接车（父车有名字）均落在这一支。

**构建自证（内测树 `build\`，探针 ON）**
- 入口：`R3R_fullbuild.cmd inc detach`（复用既有入口，未新建任何 `.cmd`）；构建前 `tasklist` 确认 `openttd.exe` 未运行。
- `build\R3R_incbuild.guard.log` = `GUARD: incremental is safe (no header/lang file is newer than the newest object)`。
- `build\R3R_incbuild.log` 尾部 = `[3/3] Linking CXX executable openttd.exe`；`error C*/fatal error/FAILED:` 计数 **0**。
- `build\R3R_incbuild.done` = `EXIT_CODE=0`（2026-11-15 03:11:54）。
- 时间戳：`src\train_gui.cpp` 03:09:58 → `build\openttd.exe` **2026-11-15 03:11:41（52 918 272 B）**。
- `read_lints`（仅 `train_gui.cpp`）**0 条**。
- exe 字面量自证：`Select-String ... -Pattern 'eng=%d rep=%d' -Quiet` = **True**。
- 发布树 `build-release\` **未编**（按用户口径只在明确要求时编），仍停留在第 448 轮版本，**不含**本轮改动。

**复测判据（实机，玩家侧）**
1. 对"父车原型是 engine + 只有一个铰接组 + C-2 反转"的那条链打开详情窗：名称、建造年、价值、最高速度、可靠性 **全部为父车（363）那一套**，不再出现"型号对、数值不对"。
2. 日志出现 `R3R-DINFO ... eng=364 rep=363`（或等价的 head 无名字、rep 为父车编号），且该行与名字行同一 `v=3 line=0`。
3. 普通未反转列车、原生 NewGRF 铰接车：`R3R-DINFO ... rep==eng`，六项读数与第 448 轮版**逐字不变**。
4. 重量/功率/牵引力/长度（上部区）若仍错 ⇒ 属另一条链路（`PowerChanged()` / `max_speed_override` 烘焙值），不在本轮范围，需新开取证。

### 22.20 第 450 轮：玩家报「最大牵引力 / 装载能力 / 运行费用」仍不同 —— 结论（已定性，未改码）

**玩家原话**：「这次也是有部分进展。但依然有以下不同：最大牵引力，装载能力，运行费用」

**结论一句话**：剩下这三项不是"取数对象又选错了"，而是**这三项的统计算法都是"逐节累加"，而反转（C-2 去铰接化）把原本被跳过的铰接部件变成了独立引擎节** ⇒ 同一列车在反转前后"参与累加的节数"从 1 变成 4。重量/功率/最高速度之所以已经正确，是因为去铰接化时对它们做了**烘焙守恒**；这三项没有烘焙，也没有"组成员跳过"的口径。

**证据链 A：真铰接部件在三个统计算法里都被跳过**
| 统计项 | 实现 | 真铰接时的行为 | 去铰接化（反转）后的行为 |
|---|---|---|---|
| 运行费用 | `src\train_cmd.cpp:24676-24704` `Train::GetRunningCost()`，`do { ... } while ((v = v->GetNextVehicle()) != nullptr);`，每节取 `v->GetEngine()` 的 `running_cost` + `GetVehicleProperty(v, PROP_TRAIN_RUNNING_COST_FACTOR, ...)` | `src\vehicle_base.h:1367-1374` `GetNextVehicle()` 里 `while (v->HasArticulatedPart()) v = v->GetNextArticulatedPart();` ⇒ **跳过全部真部件，只算父车 1 份** | 部件已被 `ClearArticulatedPart()` 改成 `ArticGroupMember`，`HasArticulatedPart()` 为假 ⇒ **4 节各算 1 份** |
| 装载能力 | `src\train_gui.cpp:540-578` `GetCargoSummaryOfArticulatedVehicle()`，`:565` `item->capacity += u->cargo_cap;`，循环条件 `:577` `while ((v = v->Next()) != nullptr && v->IsArticGroupMember());` | 真部件是 `IsArticulatedPart()`、**不是** `IsArticGroupMember()` ⇒ 循环在父车后立刻停 ⇒ **只有父车那一节** | 角色位 `ArticGroupMember` 为真 ⇒ 循环吃到组尾 ⇒ **整组容量汇总**（`:570-573` 的 `whole_consist` 分支更是直接全链） |
| 最大牵引力 | `src\ground_vehicle.cpp` `PowerChanged()`：`max_te = Σ_{power>0} GetWeight() * GetTractiveEffort()`；`GetTractiveEffort()` 取本车 `engine_type` 原型的 TE（无 override） | 只有父车 `power>0`，父车 `GetWeight()`=整组重量 ⇒ **整组重量 × 父车 TE** 一项 | 去铰接化只烘焙 `weight/power/max_speed`（`src\vehicle_cmd.cpp` 打散点），**不烘焙 TE** ⇒ 每节 `power>0`、每节 `weight`=自己那份、每节按**自己原型的 TE** 累加 ⇒ 合计改变 |

**证据链 B：为什么另外三项没变**（同一次打散里被烘焙/取极值）
- 重量：`weight_override` 按份额烘焙，合计守恒 ⇒ 不变。
- 功率：`power_override` 按份额烘焙，合计守恒 ⇒ 不变。
- 最高速度：`max_speed_override` 全组同值 ⇒ 取 min 不变。
⇒ 与玩家"只有这三项不同"逐项吻合，**不是巧合**，可以收盘。

**统一修法（待拍板，未落地）**：口径 = 「**去铰接组成员节（`IsArticGroupMember() && !IsArticGroupHead()`）不计入显示统计**」，把这三项拉回与真铰接等价的读数：
1. 运行费用：`src\vehicle_gui.cpp` 上部区 `v->GetDisplayRunningCost()` 改调新增的显示专用函数（复制 `GetRunningCost()` 逻辑 + 跳过成员节 + 保留 `cur_speed==0` 的 depot/stopped 除算）。**不改** `Train::GetRunningCost()` 本体（那是真实扣费，属行为变更）。
2. 装载能力：`GetCargoSummaryOfArticulatedVehicle()` 的 `add_unit` 对成员节 `capacity += 0`（跳过），并**取消**上一轮加的 `whole_consist` 汇总一行（`:570-573`）——那正是本轮"装载能力不同"的直接来源。
3. 最大牵引力：显示层新增 `R3RDisplayMaxTe(v)`：无角色位时直接返回 `gcache.cached_max_te`（逐字节不变）；有角色位时把成员节重量并入其组头，再 `Σ (组头总重 × 组头 TE)`，与真铰接的"整组重量 × 父车 TE"精确相等。

**把握程度**：运行费用、装载能力 = 已由源码逐行坐实（高）；最大牵引力 = 算法与烘焙缺失已坐实，"部件原型 TE 与父车不同"这一步属**推断（待 `R3R-C2REF`/新探针实测）**，若本 GRF 部件原型 TE 恰好等于父车，则该合计会等于整组重量×TE 而**不该**有差异，需实测反证。

**待拍板（C-450）**：
- C-450-1：三项是否统一按「成员节不计入显示统计」口径修（推荐），还是只修运行费用+容量、牵引力另议？
- C-450-2：装载能力是否撤销上一轮的 `whole_consist` 汇总（回到"组头一节"），这会让反转后显示与反转前一致但**不是**整列真实总容量。
- C-450-3：是否允许同时修正真实扣费（`GetRunningCost`），即承认去铰接化导致实际运行费用变高属 bug。

**本轮未改任何源码、未构建**（只取证 + 落盘）。

### 22.21 第 450 轮续：玩家给出精确读数，22.20 的"变大"方向被**证伪**，根因改写

**玩家原话（2026-11-15，决定性读数）**：「运行费用从某个值变为0，装载能力从某个值变为0，最大牵引力从显示的25变成7」

**为什么推翻了 §22.20**：§22.20 预测三项都会因为"参与累加的节数 1→4"而**变大/变多**。实测却是全部**变小或归零**（费用 0、容量 0、牵引力 25→7）。方向相反 ⇒ 真实机制不是"多累加了几节"，而是"**能参与统计的只剩链头那一节，而链头正是隐藏铰接部件原型 364**"。

**修订后的统一根因（同一条，覆盖三项）**：C-2 反转把链头从父车 363 换成了隐藏部件 364；R3R 此前把"打散组"做成与真铰接**读数等价**（第 170 轮记忆 ID 22799093 原文：「打散组…在语义组层与真 artic 读数相同（`HasArticulatedPart()`/`GetNextVehicle()` 跨过两者）」）⇒ 统计遍历从 364 出发时**跳过后面的 363/365/366**，于是：
| 统计项 | 实测 | 机制（待源码逐行确认） |
|---|---|---|
| 运行费用 | 某值 → **0** | `Train::GetRunningCost()` 从 `this`=364 起，`GetNextVehicle()` 跳过组成员 ⇒ 只算 364 一节；364 是隐藏部件原型，其 `running_cost` 本身为 0 ⇒ 0 |
| 装载能力 | 某值 → **0** | 容量汇总以 364 为起点/首个单元，隐藏部件原型 `cargo_cap`=0（或 `CanCarryCargo()` 假），且上一轮的 `whole_consist` 汇总一行把它当成整链唯一读数 ⇒ 0 |
| 最大牵引力 | **25 → 7** | `PowerChanged()` 的 `max_te = Σ_{power>0} GetWeight()*GetTractiveEffort()` 只剩 364 一节 ⇒ 7 = 364 的烘焙份额重量 × 364 的 TE |
| （重量/功率/速度） | 正确 | 由 `ConsistChanged` 全链 `Next()` 累加 / 取 min，不走会被跳过的遍历 ⇒ 不受影响 |

**算术旁证（支持"7 是份额"）**：若部件原型 TE≈父车 TE，则 25 是"整组重量 × TE"，7 是"单节份额重量 × TE"⇒ 7 ≈ 25 / 3.6，与 4 节组内份额同量级；且 0 只能由"只剩隐藏部件那一节"解释，不能由"多算几节"解释。

**修法方向（改为"代表单元 = 组头 363"，上一轮的"成员不计入"口径不再够用）**：
1. 运行费用：显示层从 **363（组头）** 起算 `GetRunningCost()` 口径（真 artic 也是"父车一份"），不得从链头 364 起算。
2. 装载能力：显示层以 **363 为起点/首单元**，并取消 `whole_consist` 把整链压成一行的分支；还原"父车那一节"的口径（与反转前逐字节一致）。
3. 最大牵引力：显示层改为 `整组重量（gcache.cached_weight） × 组头 363 的 GetTractiveEffort()`；在"整链恰好一个组"的玩家场景下与 25 精确相等；无角色位的普通链仍直接返回 `gcache.cached_max_te`（逐字节不变）。
4. 不改 `Train::GetRunningCost()` 本体与 `PowerChanged()`（真实扣费/物理不变）——除非 C-450-3 拍板"承认去铰接化导致实际费用变高是 bug"。

**待验证（必须先看源码，不许再猜方向）**：
- V-1：`src\vehicle_base.h` 的 `GetNextVehicle()` / `HasArticulatedPart()` 是否真的"跨过 `ArticGroupMember` 角色位"（这决定费用/牵引力是否是"只剩一节"）。
- V-2：打散时（`src\vehicle_cmd.cpp` 的烘焙段）是否把成员节的 `cargo_cap` 清 0；`GetCargoSummaryOfArticulatedVehicle()` 的起点单元与 `whole_consist` 是否就是 0 的来源。
- V-3：`7` 的确切构成（364 的 `cached_weight`×TE 是否=7），可用 `R3R-C2REF` 或新探针一次实测锁定。

**待拍板（取代 C-450）**：
- C-451-1：确认统一口径 = "详情窗按组头 363 显示整组读数"（推荐），还是要求"反转前后**逐位**相同"（则容量必须回到父车单节口径，可能不是整列真实总容量）。
- C-451-2：最大牵引力是否接受 `整组重量 × 组头 TE`（数学上等于反转前，除非该 GRF 引入 TE 的位置相关变量）。
- C-451-3：是否同时修正真实扣费（`GetRunningCost` 本体），还是只改显示。

**本轮仍未改任何源码、未构建**。

### 22.22 取证（第 450 轮续二）：V-1 成立，`7` 的算式锁定为「只有父车那一节贡献 TE」

**V-1 成立（`src\vehicle_base.h:1300-1374`）**
- `:1300-1303` `HasArticulatedPart()` = `Next() != nullptr && Next()->IsArticGroupMember()`，注释自陈「group-role view is used here … de-articulated groups are traversed as one unit exactly like real ones」。
- `:1367-1374` `GetNextVehicle()` = `while (v->HasArticulatedPart()) v = v->GetNextArticulatedPart(); return v->Next();`
⇒ 从链头 364（`ArticGroupHead`）出发时，Next=363 是 `ArticGroupMember` ⇒ 一路跳到最后成员 ⇒ 返回组尾之后的 `nullptr`。**`GetNextVehicle()` 族（含 `Train::GetRunningCost()`）在这条链上只算 364 一节。**
- `:1316-1322` `GetEnginePartsCount()` 明确只数"真 artic 部件"，成员节返回 1（烘焙份额自带）⇒ `CargoChanged` 的重量分摊对成员不重复，旁证重量守恒。

**V-3 部分锁定（`src\ground_vehicle.cpp:83-97`）**：`CalculatePower()` 用的是 `u = u->Next()`（**全链 4 节都参与**，不跳成员），`max_te += u->GetWeight()*u->GetTractiveEffort()` 仅当 `u->GetPower()>0`。
⇒ 25→7 不是"少算了节"，而是"**只有父车 363 那一节的 TE 非零**"：`7 ≈ (整组重量/4) × TE_parent ≈ 25/4`，另三节是隐藏部件原型（TE=0）却各占一份烘焙重量，乘积为 0。
⇒ 反转前 25 = 363 的 `GetWeight()` 返回**整组重量** × 父车 TE（真 artic 时部件 power=0 不参与）。
⇒ 「重量/功率正确」也随之解释干净：它们在同一循环里按全链累加，不受 TE 影响。

**物理副作用（新登记，重要）**：`cached_max_te` 是**真实加速用的值**，所以现在这条链的实际牵引力也只剩 28%（7/25），不只是显示。修显示还是连物理一起修，属 C-451-3 同类问题，须拍板。

**下一步只剩两点待证**：容量 0 的确切产生点（`GetCargoSummaryOfArticulatedVehicle` 起点/`whole_consist`/`cargo_cap` 是否清 0）、`Train::GetRunningCost()` 从 364 起算时到底跳成几节。

### 22.23 取证（第 450 轮续三）：容量 0 **不是显示口径问题**，`cargo_cap` 被反转提交点重算

**V-4 成立（日志已锁 `whole_consist` 分支）**
- `build\R3R_debug.log` 内 `R3R-NAME v=3 fe=1 eng=364 sid=65535 cb=* nm=* needs=1`：链头车 3 的**原型 364 是隐藏铰接部件原型**（`sid==STR_UNDEFINED`），`IsFrontEngine()==1`、无名，`R3RDetailsNeedsRepresentativeEngine(3)` 恒真 ⇒ `whole_consist==true`。
- 同日志 `R3R-C2REF head=3 ref=0 refEng=363 refNm=1`：代表单元（父车）是 `veh=0`、原型 363、有名字。
- 因此 `src\train_gui.cpp:570-573` 走的是 `for (u = v; u; u = u->Next()) add_unit(u)` —— **整条物理链都在汇总**，不存在"只算了链头那一节"的口径错误。

**V-5 成立（`cargo_cap` 会被重写，属数据级）**
- `src\train.h:83`：`CCF_ARRANGE{Length, Capacity, DepotDirection}` —— **含 `Capacity`**。
- `src\train_cmd.cpp:614-623`：`new_cap = e_u->DetermineCapacity(u)`；当 `allowed_changes.Test(ConsistChangeFlag::Capacity)` 为真时**直接写 `u->cargo_cap = new_cap`**。
- 反转/翻转的提交点一律 `ConsistChanged(CCF_ARRANGE)` ⇒ 每节车的 `cargo_cap` 都会按**新角色**（谁是 `ArticGroupHead`、谁是 `ArticGroupMember`）重新问一次 NewGRF（`GetEngineProperty(..., PROP_TRAIN_CARGO_CAPACITY, property, v)`，`src\engine.cpp:285`）。

**矛盾消解（关键推理）**：`whole_consist==true` 已经扫全链，父车 0 的 `CanCarryCargo()` 也不可能为假（→ `DetermineCapacity` 会在 `engine.cpp:242` 提前返回 0，那样反转前容量也恒为 0，与"从某值变 0"矛盾）。所以只要父车 `cargo_cap` 仍是原值 C>0，`add_unit` 必然产出 `cargo` 合法的条目 ⇒ 页面不可能显示 `none`。
⇒ **唯一自洽解释**：父车 0 的 `cargo_cap` 在 `ConsistChanged(CCF_ARRANGE)` 里被重算为 0，于是 `train_gui.cpp:553` 的 `new_item.cargo == INVALID_CARGO && new_item.subtype == STR_EMPTY` 把每一节都跳过 ⇒ `summary` 空 ⇒ `train_gui.cpp:521` 走 `STR_VEHICLE_INFO_NO_CAPACITY`（"none"）。
⇒ 与 TE 完全同构：**"未烘焙 + 提交点重算"** 的字段（`cached_max_te`、`cargo_cap`、`GetRunningCost`）在角色翻转后全部按新角色重算，烘焙过的 `cached_weight`/`cached_power` 才不受影响。这也正好解释了玩家"重量、功率正确，TE、容量、运行费用都不对"的分布。

**疑点登记（REG-450C-1，待取一行读数）**：静态代码无法判定 GRF 的 capacity 回调是否角色敏感（`GetEngineProperty` 传 `v`，NML 可读 articulation/consist 位置变量）。需要一个读数：**反转后父车 `veh=0` 的 `cargo_cap` 实际值**。
- 期望（若 V-5 成立）：`cap=0`，且 `carry=1`（原型属性容量>0）、角色 `AM=1`。
- 若实测 `cap=C>0` 而页面仍 `none` ⇒ V-5 证伪，回到"显示层再查"（此时只能用 `IsValidCargoType`/`GetCargoSubtypeText` 分支解释）。

**探针（本轮落地，只读）**：`src\train_gui.cpp` 新增 `R3RDbgCargoSummary()`，仅对 `whole_consist` 链、且 (链头, 容量指纹) 变化时打印一次：
`R3R-CAPSUM head= n= sumcap= items=` + 每节 `R3R-CAPUNIT head= veh= eng= cap= cargo= sub= carry= role(AH/AM/AP)=`。

**构建自证（第 450 轮续三）**
- 入口：`R3R_fullbuild.cmd inc detach`（复用既有脚本，未新建任何 `.cmd`）；护栏 `GUARD: incremental is safe`（只改 `.cpp`，未碰 `src\*.h`）。
- `[2/3] train_gui.cpp.obj` → `[3/3] Linking CXX executable openttd.exe`；`build\R3R_incbuild.done` = `EXIT_CODE=0`（2026/11/15 03:31:56）。
- 时间戳链：`src\train_gui.cpp` 03:29:38 → `train_gui.cpp.obj` 03:30:24 → `build\openttd.exe` 03:31:44（52 918 272 B）。
- `read_lints` 0 条；`openttd.exe` 内命中字面量 `R3R-CAPUNIT`（`CAPUNIT_FOUND`）。
- 发布树 `build-release` 未编（按口径：非用户明确要求不编）。

**复测步骤（需玩家操作）**
1. 用新 exe（`build\openttd.exe` 03:31:44）进游戏，打开这条**反转后**列车的详情窗口 → 切到「装载能力」页（这一步即触发探针）。
2. 再打开**未反转**的同类列车详情窗口（对照组）。
3. 退出，把 `build\R3R_debug.log` 发回。
- 判读：找到 `R3R-CAPSUM head=<该链头>` 及其下每行 `R3R-CAPUNIT`；若 `veh=0`（父车、原型 363）那行 `cap=0` 且 `carry=1` ⇒ **V-5 成立，容量是真丢**（与 TE 同族，须连物理一起修）；若 `cap>0` 而页面仍 `none` ⇒ V-5 证伪，改查显示层（`IsValidCargoType` / `GetCargoSubtypeText` 分支）。

### 22.24 第 450 轮续四（2026-11-15）：玩家回「三项偏差依然存在」——停止等待拍板，改按「守恒」治根

**玩家原话**：「目前最大牵引力，装载能力，运行费用的偏差依然存在」
- 本轮**未附新日志**，无法从读数判断是否已跑 03:31:44 那个带 `R3R-CAPSUM` 的 exe（REG-450C-1 仍未取到 `veh=0 cap=`）。

**判定（口径从"只改显示"升级为"治根守恒"）**：三项的根因已在 §22.22/§22.23 坐实为同一条 —— **去铰接化（C-2 反转提交点）只烘焙了 `weight/power/max_speed`，而 `cached_max_te`、`cargo_cap`、运行费用这三项依赖"角色 + 原型属性"，角色翻转后由 `ConsistChanged(CCF_ARRANGE)` 按新角色重算，于是父车 363 的数据被算成了"成员节"或干脆被遍历跳过**。因此只改显示无法同时满足"反转前后逐位一致"，必须让这三项在角色层**等价于真铰接**（真铰接的读法是：组头一节代表整组，部件不参与统计）。

**落地设计（三处，均只改 `.cpp`，避免 `src\*.h` 触发全量）**

| 项 | 落点 | 判据 | 目标值 |
|---|---|---|---|
| 最大牵引力 | `src\ground_vehicle.cpp` 的 `CalculatePower()` | 累加 `u->GetWeight()*u->GetTractiveEffort()` 时，若 `u` 是 `ArticGroupMember`（非组头）则 TE 改取**组头**的 `GetTractiveEffort()` | `Σ weight_i × TE_head` = `整组重量 × 父车 TE` = 25 |
| 运行费用 | `src\train_cmd.cpp` 的 `Train::GetRunningCost()` | 遍历由 `GetNextVehicle()`（会跳过整组成员⇒只剩链头 364）改为全链 `Next()`，并跳过 `IsArticGroupMember() && !IsArticGroupHead()` | 只算组头 363 一份（与真 artic 一致） |
| 装载能力 | `src\train_cmd.cpp` 的 `ConsistChanged()` 容量段（`new_cap` 写入点） | 对 `IsArticGroupMember() && !IsArticGroupHead()` 的节**不重算**（保留打散前的 `cargo_cap`） | 父车 363 保住原值 C，容量页恢复非 0 且等于反转前 |

**为什么这样改不会伤普通车**：三条判据都要求 `ArticGroupHead/ArticGroupMember` 角色位存在；真 artic 组与普通链无角色位 ⇒ 分支不进入，逐字节走老路。

**已知风险（登记 REG-450D-1）**：①`GetRunningCost()` 被真实扣费调用，改它 = 承认"去铰接化使实际费用变高"是 bug（原本待 C-450-3 拍板，本轮按"偏差必须消失"推定）；②容量"跳过重算"意味着打散组的 refit/换货种后容量不再自动更新 —— 需复测 refit 一次确认；③`CalculatePower` 是模板，需 `if constexpr (std::is_same_v<T, Train>)` 包住角色位分支，否则 RoadVehicle/Ship/Aircraft 实例化报错。

**状态**：待实现（本轮先落盘，再按上面三处改码 + 增量构建）。

### 22.25 取证（续四之二）：三处落点行号确认 + 改为「全部落在 `src\train_cmd.cpp`」

**行号锚点（已读原文）**
- 容量：`src\train_cmd.cpp:614-623`，`new_cap = e_u->DetermineCapacity(u)`；`allowed_changes.Test(ConsistChangeFlag::Capacity)` 时才写 `u->cargo_cap = new_cap`（reverse 提交点传 `CCF_ARRANGE` ⇒ 会写）。
- 权重/牵引力：`src\train_cmd.cpp:708-712`，`this->CargoChanged()`（内部跑到 `GroundVehicle::PowerChanged()` 并写 `gcache.cached_max_te`）→ 紧接着 `this->UpdateAcceleration()`。⇒ **在 `CargoChanged()` 之后、`UpdateAcceleration()` 之前覆写 `cached_max_te` 即可同时修好显示与真实加速**，无需动 `src\ground_vehicle.cpp`（避免模板 + 全量构建）。
- 运行费用：`src\train_cmd.cpp:24676-24705`，`do {...} while ((v = v->GetNextVehicle()) != nullptr)`。
- 代表引擎：`src\train_cmd.cpp:393` `R3RConsistReferenceEngine(Train *head)`（同文件 static，可直接复用）；名字判据 `R3REngineHasDisplayName()`（`:433`，含 name 缓存与真跑 {ENGINE} 的多级回退）。
- 字段确认：`src\ground_vehicle.cpp:62-70` 写 `gcache.cached_power` / `gcache.cached_max_te`；量纲换算 `max_te *= GROUND_ACCELERATION; max_te /= 256;`（`:96-97`）。

**方案定稿（只改 `src\train_cmd.cpp` 一个文件）**
1. `ConsistChanged()` `:614` 后：成员节（`IsArticGroupMember() && !IsArticGroupHead()`）若 `DetermineCapacity()` 给出 0 而原 `cargo_cap > 0`，保留原值（只挡"被算成 0"，refit 得到合法新值仍照常写入）。
2. `ConsistChanged()` `:709` 之后：链上存在 `ArticGroupHead/Member` 角色位时，`gcache.cached_max_te = cached_weight * R3RConsistReferenceEngine(this)->GetTractiveEffort() * GROUND_ACCELERATION / 256`（= 真 artic 的「整组重量 × 父车系数」）。
3. `Train::GetRunningCost()`：遍历改 `Next()`，组成员与组头只由代表引擎那一节出面计价（其余角色节 `continue`），起点仍为 `this`（不改原有调用语义）。
4. 三处都加 `R3RDbgWrite` 证据行（`R3R-CAPKEEP` / `R3R-MAXTE` / `R3R-RUNCOST`），只在命中时打。

### 22.26 落地（第 450 轮续四之三）：三处守恒修法已实现 + 增量构建通过

**改动（仅 `src\train_cmd.cpp`，未碰任何 `src\*.h`）**
1. `ConsistChanged()` 容量段（现 `:614`）：成员节被 NewGRF 容量回调算成 0 时保留原 `cargo_cap`（只挡破坏性 0，refit 得到合法值仍写入）。
2. `ConsistChanged()` `CargoChanged()` 之后（现 `:712-731`）：链上存在 `ArticGroupHead/Member` 角色位时，`gcache.cached_max_te = cached_weight * R3RConsistReferenceEngine(this)->GetTractiveEffort() * GROUND_ACCELERATION / 256`，即"整组重量 × 父车系数"。
3. `Train::GetRunningCost()`（现 `:24709-`）：有角色位时改走物理链 `Next()`，组成员与组头只由代表引擎那一节出面计价，其余角色节跳过；无角色位链仍走 `GetNextVehicle()` 原文。

**刻意不加探针**：`R3RDbgWrite()` 的 tag 白名单在 `src\r3r_perf.h` 的 `R3RDbgTagAllowed()`（`RESERVE/DECOUPLE/SPD/WP` 等），新增 tag 会被吞掉（第 4 次踩此坑），而要放开白名单必须改 `.h` ⇒ 触发全量重编。本轮改为靠玩家直接读三项数值验证。**若复测仍有偏差，下一步必须同时做「改 `r3r_perf.h` 白名单 + 接受一次全量构建」再取 `R3R-CAP/KEEP`、`R3R-MAXTE`、`R3R-RUNCOST`。**

**构建自证（三件套齐全）**：`R3R_fullbuild.cmd inc detach`；第一次 `EXIT_CODE=1`，`train_cmd.cpp(24724): error C2664 无法将参数 1 从 const Train* 转换为 Train*`（`GetRunningCost()` 是 const 方法），改为 `const_cast<Train *>(this)` 后重跑；护栏判增量安全；`[3/3] Linking CXX executable openttd.exe`；`build\R3R_incbuild.done = EXIT_CODE=0`（2026-11-15 17:43:48）；`src\train_cmd.cpp` 17:41:51 → `train_cmd.cpp.obj` 17:42:18 → `build\openttd.exe` 17:43:40（52 918 272 B）；`read_lints` 0 条。发布树 `build-release` **未编**（停留在 r3r-stable-2026-11-13-m）。

**复测判据（同一列车反转前后对比，三项应逐位一致）**
1. 最大牵引力：反转前/后读数相同（本案应为 25，不再是 7）。
2. 装载能力：容量页不再显示 `none`，数值等于反转前。
3. 运行费用：反转前后相同（不再掉到链头节的空费率）。
4. 负判据（必测）：普通非铰接列车、真铰接列车（无角色位）三项数值与旧版逐字相同；加速手感不变。
5. 负判据：对打散组 refit 一次，容量仍按货种正常变化（验证"只挡破坏性 0"这一收窄条件确实没把 refit 锁死）。

### 22.27 更正与补证（同一轮收尾）：`R3R-` 前缀**能过**白名单，探针可用；三项守恒的等价性推演

**1. 更正 22.26 的"探针会被吞"说法（自我纠错）**

`src\r3r_perf.h:396` 是白名单最后一条：`if (n >= 4 && std::strncmp(fmt, "R3R-", 4) == 0) return true;` —— **任何以 `R3R-` 起首的 tag 恒放行**。`n` 的采样上限是 8（`:334`），`"R3R-CAPSUM"` 取到 `"R3R-CAPS"` 恰好满足 `>= 4`。

⇒ 22.26 写的"新增 tag 会被吞"只对**不带 `R3R-` 前缀**的旧 tag 成立（历史上被吞的是 `C2-REF`，见第 445 轮）。本树已有的两支探针 `R3R-CAPSUM`（`src\train_gui.cpp` 定义侧）与 `R3R-CAPUNIT`（逐节侧）在 `build\openttd.exe`（17:43:40 版）里**会写盘**。

⇒ **复测不需要改白名单、不需要全量重编**：打开该列车的详情窗并翻到容量页，`build\R3R_debug.log` 就会出现 `R3R-CAPSUM`（汇总行）与逐节 `R3R-CAPUNIT`（每节一条），可直接读出反转前后的总容量。

**2. 为什么这次数值会真的相等（逐条对齐真铰接的算式）**

| 项 | 真铰接的算法 | 本轮的算法 | 是否同式 |
| --- | --- | --- | --- |
| 重量 | `CargoChanged()` 把父车 `GetWeightWithoutCargo()`（部件不加自身 weight，只取父车分摊份额）按 `GetEnginePartsCount()=1+n` 摊成每节 `W/(1+n)`，链总重恒为 `W` | 烘焙写入的 `weight_override` 就是同一份 `W/(1+n)`，且成员节 `GetEnginePartsCount()=1`（不再二次分摊）⇒ `gcache.cached_weight` 两种形态都等于 `W` | ✅ 同式 |
| 最大牵引力 | `CalculatePower()` 只让 `GetPower()>0` 的父车贡献 `GetWeight()*GetTractiveEffort()`；父车 `GetWeight()` 返回其**记录值**（分摊只作用于 `cached_weight`）⇒ `W × TE_parent` | `cached_weight × R3RConsistReferenceEngine()->GetTractiveEffort()` = `W × TE_parent` | ✅ 同式（这就是 7 = `(W/4)×TE` 被修回 25 = `W×TE` 的原因） |
| 运行费用 | `GetNextVehicle()` 从组头一步跳到组尾之后 ⇒ 只计组头一节 | 改走 `Next()`，但仅代表引擎（=原父车原型那一节）出面计价，其余角色节跳过 ⇒ 同样只计一节；`IsMultiheaded` 减半读的也是同一节 subtype 位 | ✅ 同价 |
| 装载能力 | 整组容量记在父车、部件为 0 | 该节被 NewGRF 容量回调算成 0 时保留原 `cargo_cap` ⇒ 链上总和守恒 | ✅ 同值 |

**3. 新增遗留风险 REG-450D-2（低）**：`cargo_cap` 的"只挡破坏性 0"对**合法 refit 到 0 容量**的货种会挡一次（下一次 refit 到非 0 货种会正常写入；若目标货种真为 0 容量则永远停在旧值）。复测判据 5 即为此设。

**4. 本轮未做**：游戏内复测（需玩家）；`build-release` 未编（仍是 `r3r-stable-2026-11-13-m`）；`R3R-MAXTE` / `R3R-RUNCOST` 两支新探针未加（若界面读数仍有争议再加，加时走 `R3R-` 前缀即可，无需动白名单）。

### 22.28 实机复测通过（2026-11-15，KI-641 收口；含 KI-640 六项）

**玩家原话**：「目前，反转后的列车各类指标终于正常了，也就是说，你的改动生效了」

**判定**：**通过（成功收口）**。玩家用 `build\openttd.exe` 2026-11-15 17:43:40（52 918 272 B，第 450 轮续四版）实机复测，反转前后详情窗各指标一致 ⇒ §22.26 的「源头守恒」三条落地全部生效。

**覆盖到的判据**
1. 最大牵引力：反转前后相同（`cached_max_te = cached_weight × TE_ref` 替代 `Σ weight_i×TE_i` 的失效算法）—— **通过**
2. 装载能力：容量页不再 `none` 且与反转前一致（`CCF_ARRANGE` 重算成 0 时保留原 `cargo_cap`）—— **通过**
3. 运行费用：反转前后相同（`GetRunningCost()` 改走 `Next()` + 仅代表引擎出面计价）—— **通过**
4. 连带 KI-640 六项（引擎名称 / 建造年 / 价值 / 最高速度 / 可靠性 / 容量 none）—— 玩家表述为「各类指标终于正常」⇒ **一并视为通过**

**未单独验证（不在本次结论内）**
- 判据 4 负判据：普通非铰接列车 / 真铰接车的三项读数与旧版逐字相同（玩家未专门对比）。
- 判据 5 负判据：对打散组 refit 一次后容量仍随货种变化（`REG-450D-2` 的「合法 refit 到 0 容量会被挡一次」仍未实测）。
- 加速手感：`cached_max_te` 同时喂 `UpdateAcceleration()`，本次等于真实牵引力也回到 25（不是只改显示）—— 玩家未反映手感异常，视为无反证。

**状态**：KI-641 = 已修 + 实机复测通过；KI-640 = 已修 + 实机复测通过。本轮**零源码改动、零构建**，只回填文档。发布树 `build-release` 仍未编，停在 `r3r-stable-2026-11-13-m`（不含本轮与第 449/450 轮修复）；若之后要出发布版，需另跑发布树构建。

