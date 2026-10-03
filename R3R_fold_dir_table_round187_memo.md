# 第 187 轮临时分析报告：折叠判据的方向→像素偏移表用错了坐标系（KI-287）

- 日期：2026-10-01
- 现场日志：`build\R3R_debug.log`（本轮取证区间 9186–9207 行）
- 涉及源码：`src\train_cmd.cpp` → `R3RCheckChainFoldedDirection()`（定义约 7940–8070 行）
- 涉及引擎事实：`src\map.cpp:263`（`_tileoffs_by_dir`）、`src\map_func.h`（`TileIndexDiffCByDir`）、`src\intro_gui.cpp:82`
- 结论一句话：**判据里手写的 `dir_dx/dir_dy` 是「屏幕坐标 + y 向下」的罗盘表，而 `x_pos`/`y_pos` 是「世界（等轴测）坐标轴」上的像素；两者在 DIR_NE/SW 的 dx、DIR_E/W 的 dy 上符号相反或归零。于是沿 y 轴排列的链一直正常，沿 x 轴排列的链上真折叠被读成健康（`dirfold=0`）放行。**

---

## 一、现场锚点（逐字）

```
9188  [R3R] CPL-GEO site=geo v=24 vn=24 vlen=2 u=23 z=23 ulen=2 rev=0 dx=2 dy=0 diff=2 need=2 spd=80 db=0
9189  ARRANGE-IN dh=24 dst=26 sh=23 src=23 mc=1
9193  FOLDCHK COUPLE n=27 worst_gap=6 A idx=26 x=386 y=152 tile=24,9 dir=5 trk=0x1 db=0 B idx=23 x=394 y=152 tile=24,9 dir=1 trk=0x1 exp=2 nom=2 nom0=2 nom1=2 dist=8
9194  COUPLE-PREFLIGHT head=24 last=26 fold=0 dirfold=0 gap=6 pair=(-1 dir=15, -1 dir=15) mixed=0 onsplice=0 splice=(26 dir=5, 23 dir=1) spd=80 db=0
9196  [R3R] COUPLE-SEAM-FLIP circ=4 a=26 dirA=5 b=23 dirB=1
9197  GEO respace-before n=0 veh=24 ... x=392 y=152 dir=5 ... clen=2 dist=3 nom=3
9198  GEO respace-before n=1 veh=25 ... x=389 y=152 dir=5 ... clen=4 dist=3 nom=3
9199  GEO respace-before n=2 veh=26 ... x=386 y=152 dir=5 ... clen=2 dist=8 nom=2   ← unfixable
9200  GEO respace-before n=3 veh=23 ... x=394 y=152 dir=5 ... clen=2 dist=5 nom=5
9201  GEO respace-before n=4 veh=22 ... x=399 y=152 dir=5
9202  GEO respace-before n=5 veh=21 ... x=403 y=152 dir=5
```

链的物理顺序与位置：

| 链序 | 车号 | x_pos | y_pos |
|---|---|---|---|
| 0 | 24 | 392 | 152 |
| 1 | 25 | 389 | 152 |
| 2 | 26 | **386** | 152 |
| 3 | 23 | **394** | 152 |
| 4 | 22 | 399 | 152 |
| 5 | 21 | 403 | 152 |

→ 24→25→26 沿 **−x** 单调推进，`26→23` 突然 **+x** 折回，之后 23→22→21 继续 **+x**。
这是一条**在 26↔23 处掉头的折链**（机车段与车底段鼻对鼻）。
`26` 的 `dir=5`（`DIR_SW`）朝向正是 **+x**，而它的后继 `23` 就在 **+x** 侧 ⇒ 后继落在本车鼻尖一侧 = 真折叠。

## 二、判据为什么没抓住它

判据（`R3RCheckChainFoldedDirection`）对每一对相邻非 artic 车算

```
dot = (b->x_pos - a->x_pos) * dir_dx[a_dir] + (b->y_pos - a->y_pos) * dir_dy[a_dir];
if (dot > 0) 判为折叠
```

代入现场对 `(a=26, b=23)`：`Δ = (394-386, 152-152) = (+8, 0)`，`a_dir = DIR_SW = 5`。

- 手写表：`dir_dx[5] = -1`、`dir_dy[5] = 1` ⇒ `dot = 8×(−1) + 0×1 = −8` ⇒ **负 ⇒ 「健康」**。
- 引擎表：`_tileoffs_by_dir[DIR_SW] = (+1, 0)` ⇒ `dot = 8×(+1) + 0×0 = +8` ⇒ **正 ⇒ 折叠**。

日志给出的正是 −8 的结果：`pair=(-1 dir=15, -1 dir=15)`（没有一个对写回折叠端点）、`dirfold=0`。
唯一的 8px 豁免没帮上忙：`pair_dist=max(8,0)=8`、`pair_expected=(2+2)/2=2`、`|8−2|=6 ≤ 8` 满足，但豁免还要求 `a->direction == b->direction`，而 `26 dir=5 ≠ 23 dir=1` ⇒ **不豁免**，点积必须算 —— 于是全看这两张表哪张对。

## 三、两张表的坐标系差异（根因）

`x_pos`/`y_pos` 是**世界（等轴测）坐标轴**上的像素，不是屏幕像素。证据：`src\intro_gui.cpp:82` 写作
`RemapCoords(v->x_pos, v->y_pos, v->z_pos)` —— `RemapCoords` 的入参就是世界坐标 `(x, y, z)`；
世界轴与屏幕轴之间差着一个 45° 的等轴测映射（`RemapCoords` 内 `sx = 2(y−x)`、`sy = x+y−z`）：

```
世界 +x == 屏幕左下(SW)      世界 +y == 屏幕右下(SE)
世界 −x == 屏幕右上(NE)      世界 −y == 屏幕左上(NW)
```

引擎权威表 `src\map.cpp:263`（`_tileoffs_by_dir`，经 `TileIndexDiffCByDir()`）恰好按这个映射命名：

| Direction | 引擎（世界轴） | 手写表 | 差 |
|---|---|---|---|
| DIR_N  | (−1, −1) | (0, −1)  | dx |
| DIR_NE | (−1,  0) | (1, −1)  | **dx 反号、dy 归零** |
| DIR_E  | (−1,  1) | (1,  0)  | **dx 反号、dy 归零** |
| DIR_SE | ( 0,  1) | (1,  1)  | dx |
| DIR_S  | ( 1,  1) | (0,  1)  | dx |
| DIR_SW | ( 1,  0) | (−1, 1)  | **dx 反号、dy 归零** |
| DIR_W  | ( 1, −1) | (−1, 0)  | **dx 反号、dy 归零** |
| DIR_NW | ( 0, −1) | (−1, −1) | dx |

手写表其实是一张**屏幕坐标系**的罗盘表（把 `x_pos/y_pos` 的差分当成屏幕差分来用），
与 `x_pos/y_pos` 的真实轴系不符。

**为什么一直没暴露**：本工程历次现场链条都是**沿 y 排列**的（`58,63`、`60,90`、`58,23` 等），
那些链上相邻车的 `Δx = 0`，`dot` 只由 `dy` 列决定；而 `dy` 两表在 `DIR_SE(+1)/DIR_NW(−1)` 上**恰好相同**：

- `COUPLE head=50 tile=58,63`（日志 3681 段）：全链 `dir=3`（DIR_SE），`y` 递减 ⇒ 两表都给 `dot = −Δy < 0` ⇒ 健康，一致。
- `COUPLE-FLIP-BOTH head=48 tile=60,90`（日志 5050 段）：全链 `dir=7`（DIR_NW），`y` 递增 ⇒ 两表都给 `dot = −Δy < 0` ⇒ 健康，一致。

只有当链**沿 x 排列**（`Δy = 0`、`dot` 只由 `dx` 列决定）且方向落在 `DIR_NE/SW/E/W` 时，
两表才分道扬镳 —— 本轮的 `tile 24,9` 现场正是这个组合。

补充说明：本轮 x 链上 `24→25`（`Δx=−3`）与 `25→26`（`Δx=−3`）两对其实**手写表也会算成正**（`dot = (−3)×(−1) = +3`），
但它们的 `pair_dist = 3 == pair_expected = (2+4)/2 = 3` 且 `direction` 相同 ⇒ **被 8px 豁免挡在前面**，没走到点积。
唯一走到点积的就是 `26→23`（方向不一致），于是 bug 精确地从这一个缝里漏出。

## 四、修法（已落地）

`src\train_cmd.cpp`，**仅此一个文件，未碰任何 `src\*.h` / `lang\*.txt`**：

1. 删掉 `R3RCheckChainFoldedDirection()` 开头手写的 `dir_dx[8]` / `dir_dy[8]` 两张表，改为在函数头写明坐标系与历史错误（防复发）。
2. 点积改为取引擎权威表：

```cpp
const Direction a_dir = a->IsDrivingBackwards() ? ReverseDir(a->direction) : a->direction;
const TileIndexDiffC a_step = TileIndexDiffCByDir(a_dir);
int dot = (b->x_pos - a->x_pos) * a_step.x + (b->y_pos - a->y_pos) * a_step.y;
```

`IsDrivingBackwards()` 那一支仍然自洽：`ReverseDir()` 在世界轴上正好是偏移取反。

同文件 `14341` 行 `YapfTrainCheckReverse()` 里已经是这个写法（`TileIndexDiffCByDir(moving_front->GetMovingDirection())`），
本次只是把唯一一处**例外**收拢到同一口径。

**行为变更面（有意收窄）**：只有「不享 8px 豁免」且「方向落在 NE/SW/E/W 的 x 链 / 方向落在 E/W 的 y 链」这些对会改判。
享受豁免的对在算点积之前就 `continue` 了，逐字不变；`DIR_SE`/`DIR_NW` 的 y 链判据也逐字不变。

## 五、构建自证

- 复用既有 `d:\sourcecode of JGRPP\_tmp_inc_build.cmd`（**未新建任何 `.cmd`**）。
- 护栏：`GUARD: incremental is safe (no header/lang file is newer than the newest object)`。
- `build\R3R_incbuild.done` = `EXIT_CODE=0`。
- 时间戳：`src\train_cmd.cpp` 18:35:39 → `build\CMakeFiles\openttd_lib.dir\src\train_cmd.cpp.obj` 18:37:12 → `build\openttd.exe` **18:39:15（51 566 080 B）**。
- `read_lints` 对 `src\train_cmd.cpp` = 0 条；全树检索确认 `dir_dx`/`dir_dy` 与本表的字面量**仅剩注释**，无第二处副本。
- 注意：本轮**未新增日志字符串**（改动是坐标系），故 exe 内无新串可自证，差异只能由复测日志的 `dirfold=` / `pair=` / `onsplice=` 数值证明。

## 六、复测判据（同一存档同一场景）

1. 复跑 `tile 24,9` 场景，`FOLDCHK COUPLE` 那一对该仍是 `A idx=26 x=386 dir=5` / `B idx=23 x=394 dir=1 dist=8`。
2. `COUPLE-PREFLIGHT` 应从 `fold=0 dirfold=0 pair=(-1 dir=15, …) onsplice=0` 变为
   **`dirfold=1`、`pair=(26 dir=5, 23 dir=1)`、`onsplice=1`**（因为该对正是 `v_last=26`）。
3. 紧随其后应出现 `COUPLE-REFUSE-STILL-FOLDED`（KI-265 提交前硬闸门）并回滚，**不再**直接走
   `COUPLE-SEAM-FLIP circ=4 a=26 dirA=5 b=23 dirB=1` 提交。
4. `GEO respace-before n=2 veh=26 … dist=8 nom=2` 这条「结构性不可修」签名不应再出现（或出现后立刻被回滚掉，不进入可见状态）。
5. 沿 y 排列的既有健康场景（`58,63` / `60,90` 那两条链）判据逐字不变，不应新出现 `COUPLE-REFUSE-STILL-FOLDED`。
6. 车库拖动、解挂路径不受影响（本函数只在耦合的折叠修正与提交前闸门被调用）。

## 七、未确认项 / 未做

- **U-1**：本轮的诊断是**离线代入日志数值**得出的（用日志里的 `x_pos/y_pos/direction` 手算两张表的点积），
  **尚未**用「加一行临时探针同时打印两张表的 dot」在实机上直接对照。修法本身是"换成引擎权威表"，
  正确性不依赖该对照；但若复测后判据仍不触发，第一件事就是补这个对照探针。
- **U-2**：8px 豁免（KI-201 附记 4/8）本身是否过宽未动。本轮 x 链上 `24→25`、`25→26` 两对
  在手写表下会被算成"折叠"，正是被豁免挡住的；换成引擎表后它们本来也就是健康，所以豁免在这两对上已无必要 ——
  但**没有收紧**它（影响面涉及 KI-06/KI-265 的一串既有结论，须独立轮次评估）。
- **U-3**：`DIR_N/DIR_S/DIR_SE/DIR_NW` 在纯 x 链上的新数值与旧数值不同（dx 列变了），
  这些方向的现场样本在本日志里没有，未做实测比对。
- **U-4**：承 KI-284 / KI-285 / KI-286 的复测（"等待挂接被跳过""车底自己蠕动""车长>站台"）仍未做，
  与本条互不阻塞。
