# 挂接寻路预留「在不安全道岔截止」（2026-11-24）

> 状态：**M1 已落地（仅 `src\pathfinder\yapf\yapf_rail.cpp`）+ 已编译（EXIT_CODE=0）；游戏内复测待做**
> 输入：玩家报告 + `build\R3R_debug.log`
> 口径：P1 = KI-680（已改），P2 = KI-681（玩家要求先别修）；REG-504-A 五维对齐表见 §11.5

## 一、玩家报告原文（保留）

> 见 build/R3R_debug.log，我遇到了两个问题：
> 第一个问题是挂接寻路的预留在不安全的道岔截止了，
> 第二个问题是挂接寻路的预留超出站台在不安全的道岔截止了。

拆成两条待查现象：

- **P1**：挂接寻路的预留**在不安全的道岔上截止**（预留终点落在非安全停车位，例如道岔格）。
- **P2**：挂接寻路的预留**先超出站台**，再在**不安全的道岔**上截止（即预留越过了站台末端继续往前铺，最后停在道岔不安全位）。

两条的共同点：**预留终点不是安全停车位**。区别只在 P2 多了一段「越过站台继续铺」。

## 二、猜测根因（待验证）

### G1（主假设）：KI-563「部分预留收终点」未校验安全位

`src\pathfinder\yapf\yapf_rail.cpp` 单段两遍法分支，第二遍逐格订完后：

```
2098:	reserved = this->R3RReservePathSequence(r3r_resv_path, r3r_booked);
2099:	/* KI-563：部分预留时把预留终点收到"最后一格已订"，否则后续逻辑
2100:	 * 以为整条到会合点都在自己名下。原生 PBS 的预留终点本来就是
2101:	 * 逐段推进的。 */
2102:	if (reserved && r3r_booked > 0 && r3r_booked < r3r_resv_path.size()) {
2103:		const R3RCoupleStep &r3r_stop = r3r_resv_path[r3r_booked - 1];
2104:		this->SetReservationTarget(pNode, r3r_stop.first, r3r_stop.second);
2105:	}
```

`r3r_stop` 取的是**最后成功订上的那一格**，纯粹按「订没订上」选，
**没有**再过一次「这一格是不是安全停车位（`IsSafeWaitingPosition` / `FindSafePositionOnNode`）」。

⇒ 如果第 `r3r_booked` 格失败的原因是「敌方预留 / 信号 / 道岔被占」，
那么第 `r3r_booked-1` 格很可能正好是**道岔格本身**（或道岔前唯一可停格），
于是预留终点就落在不安全道岔上 = P1。

### G2（P2 专用）：摸路越过站台末端

第一遍摸路 `rp()`/`reach()` 只读、不做安全位裁剪，
站台段还带「整段跳过」逻辑（`R3RSkippedStationStretchContains` 一族，KI-163 修复）。
若站台末格被跳过、或 `r3r_resv_path` 里站台之后的格被逐格压入，
第二遍就会把预留**铺出站台**，直到撞上道岔失败 ⇒ P2。

### G3（排除项，待证）

非上述两处，而是 `R3RReservePathSequence()` 自身在失败时的
`booked` 计数语义错（把失败格也计入 `booked`，或 `booked` 停在失败格而非最后一格成功格）。

## 三、待验证判据

| # | 判据 | 数据源 |
| --- | --- | --- |
| V1 | 日志里 `CPL-RESERVE ... reserved=1` 且 `booked < path`，随后预留终点格是道岔（`tracks` 多比特/`choice` 分叉） | `build\R3R_debug.log` 的 `CPL-RESERVE` / `RP cur=` |
| V2 | 该终点格不是安全停车位 | `RP cur=` 的 `td`/`tid` 与 `IsSafeWaitingPosition` 反推；必要时加探针 |
| V3 | 摸路序列 `RP cur=` 中出现站台格之后仍有格子（P2） | `RP cur=` / `CPL-TRACE` 序列 |
| V4 | `R3RReservePathSequence()` 的 `booked` 是否等于「最后成功格的序号+1」 | 代码 + `booked=` 读数 |

## 四、取证记录（已做实）

日志：`build\R3R_debug.log`，2986 行 / 298 593 B / mtime 2026-11-24 17:49:21，`CPL-` 307 行、`RESV` 252 行。
**注意**：`search_content` 工具读不到该文件（`build\*.log` 被 .gitignore），必须用 `Get-Content` + `Select-String`。

### 4.1 现场：veh=15，目标车底在 1424,1874（与记忆 72821122 同一现场）

```
1924: CPL-ENTRY veh=15 tile=1474,1884 orderType=16 dontReserve=0
1932: CPL-BEST  veh=15 tile=1424,1874 td=0 cost=25854
```

目标端点始终正确（`CPL-BEST` 一直是 `1424,1874`），**不是**记忆 72821122 猜的「终点甩到平行线」。

### 4.2 决定性证据：安全位判定落在道岔格上

```
2181: CPL-RES-TERM-EXEMPT veh=15 nth=3/23 tile=1457,1874 td=13 booked=4
2220: CPL-ENTRY veh=15 tile=1457,1874 orderType=16 dontReserve=0
2222: CPL-ORIGIN-TD veh=15 tile=1457,1874 tdb=0x3030 tbits=0x30
2223: CPL-ORIGIN-FOLLOW veh=15 td=4  trk=4 ok=0 nt=-1,-1
2224: CPL-ORIGIN-FOLLOW veh=15 td=5  trk=5 ok=0 nt=-1,-1
2225: CPL-ORIGIN-FOLLOW veh=15 td=12 trk=4 ok=0 nt=-1,-1
2226: CPL-ORIGIN-FOLLOW veh=15 td=13 trk=5 ok=0 nt=-1,-1
```

`1457,1874` 的 `tbits=0x30` = `TRACK_LEFT|TRACK_RIGHT`，即**双槽道岔**；
四个 trackdir `Follow` 全部 `ok=0`（走不出去）。
`IsSafeWaitingPosition(..., include_line_end=true)` 把「跟随失败」按死端处理 ⇒ 判为安全位。

⇒ **本次全日志唯一被识别为安全位的格，就是这个道岔格**（`nth=3/23`，`booked=4`）。

### 4.3 预留实际铺出来的序列（跨线横跳）

```
2197: CPL-RES-OK veh=15 total=23 booked=23 stop_safe=0 term_exempt=1
      tiles=1459,1875/0;1458,1875/0;1457,1875/4;1457,1874/5;1456,1874/0;...（截前 16 格）
2384: CPL-RES-OK veh=15 total=12 booked=12 stop_safe=0 term_exempt=0
      tiles=1449,1874/0;...;1439,1874/0;1424,1874/0
2469: CPL-RES-OK veh=15 total=4  booked=4  stop_safe=0 term_exempt=0
      tiles=1441,1874/0;1440,1874/0;1439,1874/0;1424,1874/0
```

- 路径确实是 `1875 线 → 1457,1874 道岔横切 → 1874 线 → 1439 → 站台格 1424`（`1439` 之后直接跳 `1424` 是站台整段跳过）。
- 全日志 **0 条 `CPL-RES-SAFE-STOP`、0 条 `CPL-RES-FAIL`、0 条 `CPL-RES-PARTIAL`**——本次没有截断，也没有失败。

### 4.4 判定

| 现象 | 判定 | 锚点 |
| --- | --- | --- |
| P1 预留在不安全道岔截止 | **成立（机制做实）**。`1457,1874` 被 `IsSafeWaitingPosition(include_line_end=true)` 误判为安全位；一旦它**不是**路径上最后一个安全位，就会在此 `CPL-RES-SAFE-STOP` 截断 ⇒ 预留终点停在道岔上 | 2181 / 2222-2226 / 代码 `yapf_rail.cpp:903` |
| P2 预留超出站台后在不安全道岔截止 | **成立（同一根因的两段）**。`REG-399-C` 的「最后一个安全位豁免」（`term_exempt`）把这个伪安全位放过 ⇒ 预留穿过道岔继续铺 23 格直到会合点站台格，视觉上就是「越过道岔/超出站台」；截断点最终仍显现在这个道岔上 | 2197 `term_exempt=1 booked=23`；代码 `yapf_rail.cpp:904-919` |

**根因收敛为一条**：
`IsSafeWaitingPosition()` 的 `include_line_end=true` 把「四向跟随全失败」的道岔格当作合法停车位；
再叠加 `REG-399-C` 的 term_exempt 豁免，使它既可能成为**错误的截断点**（P1），
也可能成为**被越过的点**（P2）。G1（未校验安全位）与 G2（摸路越站台）**均被证伪**——
`R3RReservePathSequence` 有安全位判定（`yapf_rail.cpp:786` / `:903`），
G3（`booked` 语义）也被 `booked-before` 与 `CPL-RES-PARTIAL-TILE` 探针排除。

### 4.5 待验证（剩余 1 项）

- V5：`IsSafeWaitingPosition()` 里 `include_line_end` 具体在什么条件下返回 true，
  以及挂接预留这一侧是否应当传 `false`（原生驾驶预留传 `true` 的理由是「尽头站台可停」，
  但道岔死端不是可停位）。

### 4.6 V5 已核实（`src\pbs.cpp:2777-2800`）

```cpp
2792:	if (!ft.Follow(tile, trackdir)) {
2793:		/* Last tile of a terminus station is a safe position. */
2794:		if (include_line_end) return true;
2795:	}
2800:	if (ft.new_td_bits == TRACKDIR_BIT_NONE) return include_line_end;
```

即 `include_line_end=true` 时，「跟随失败」与「无出边」**都**直接算安全。
`1457,1874` 的四个 trackdir 全 `ok=0`（4.2）⇒ 必然被判安全，与格上是不是道岔无关。
**函数本身没有"道岔不可停"的概念**，它只判断"车能不能从这里继续开"。

## 五、两条独立的根因（已分开）

### R1 → P1：伪安全位落在双槽道岔

`yapf_rail.cpp:786`（扫 `r3r_last_safe`）与 `:903`（截断判定）都用
`IsSafeWaitingPosition(coupler, tile, td, /*include_line_end=*/true, forbid_90deg)`。
`1457,1874` 是 `1875 → 1874` 的横切道岔，四向跟随失败 ⇒ 判安全。
当它**不是**路径最后一个安全位时，`:921` 走 `stopped_at_safe = true` 并 `break`，
预留终点收到 `booked-1`，列车被允许**停在这个道岔上** ⇒ P1。

### R2 → P2：站台整段跳过让「到达」逻辑跨过被车底占用的平台

`CPL-BEST` 全程是 `1424,1874`（车底**西端**头车），而机车从**东侧**（1457 → 1439）接近。
`CPL-RES-OK` 的 `tiles=...;1439,1874/0;1424,1874/0` 说明站台段被整段跳过
（`1439` 直接到 `1424`），跳过区间 `1425..1438` 正是目标车底占用的平台。
于是 YAPF 认为「从东侧可以一步抵达西端 1424」⇒ 预留**铺过整条站台**，
终点越过站台落在 1424 ⇒ P2（视觉上"预留超出站台"）。

> 两条互相独立：R1 决定「停在哪个道岔」，R2 决定「预留往哪一端铺」。
> 同一现场同时命中（先 R2 选出西端终点，路径横切 1875→1874 时又在 R1 的道岔上留了安全位）。

## 六、修法方向（M1 已被玩家拍板并实施，见 §十二）

- **M1（对 R1）**：挂接这一侧的截断判定改为「格必须是单轨（`KillFirstBit(GetTrackBits(tile)) == TRACK_BIT_NONE`）**且** `IsSafeWaitingPosition(..., false, ...)`」，
  即道岔格一律不作截断点，继续往前找真正的单轨安全位；若直到会合点都没有，
  退回 `REG-399-C` 的终点例外（本来就要订满）。
- **M2（对 R2）**：站台整段跳过的回收（`rp()` 的 skip 补订）必须以「不越过目标车底所在区间」为前提；
  或让 `CYapfDestinationTrainRailT` 的到达判据在跳段帧上失效（跳段不得直接判到达）。
- **M3（保守）**：只做 M1，P2 先靠 M1 把终点限制在 1439 一侧，观察是否自愈。

## 七、复测判据

1. 同场景不再出现 `CPL-RES-TERM-EXEMPT ... tile=<双槽道岔>`。
2. 出现 `CPL-RES-SAFE-STOP` 时其 `tile` 必为单轨格（`tbits` 为单比特）。
3. `CPL-RES-OK` 的 `tiles=` 中不出现「从站台前格直接跳到站台远端（车底另一端）」的步。
4. 机车不再停在双槽道岔格上（`CPL-ENTRY ... tile=<道岔>` 不出现）。
5. 负判据：正常尽头站台挂接仍能到达（`REG-399-C` 的豁免不被误杀）。


## 八、早期疑点及其处置（原「五、未确认」，编号与 §五 重复已改）

- U-1：P1/P2 是否同一列车、同一次挂接尝试，还是两次独立现场。
  → **已答**：同一现场、同一列车 `veh=15`、同一目标车底（`CPL-BEST` 恒 `1424,1874`）。
  证据：`2181 CPL-RES-TERM-EXEMPT veh=15`、`2197 CPL-RES-OK veh=15`、`2384`、`2469` 全部同一 veh。
- U-2：失败原因到底是「敌方预留」「信号」「道岔被占」中的哪一种。
  → **已答，三者都不是**：全日志 0 条 `CPL-RES-FAIL` / `CPL-RES-PARTIAL`，预留全程成功
  （`booked == total`）；问题不是"订不上"，而是"订到了不该停的格"。
- U-3：是否与 KI-563 修法 D（截断判据改成第一个安全停车位）本应覆盖但漏了的分支同源。
  → **已答，不同源**：KI-563 处理的是**写入侧**（订不上时如何收），本条是**判据侧**
  （`IsSafeWaitingPosition` 把道岔死端算成安全位）。修法 D 不检查格本身是不是道岔。

## 九、P1 归属判定（玩家问：原生问题还是 R3R 引入？）

**答：函数是原生的，误用是 R3R 的 —— 两边各占一半，但需要修的是 R3R 侧。**

### 9.1 判据本身是原生的（逐条实测）

- `IsSafeWaitingPosition()` 定义在 `src\pbs.cpp:2777`，**函数体是上游 JGRPP 的 PBS 代码**，
  `include_line_end` 是它的原生形参（`pbs.h:240`）。
- `pbs.cpp:2791-2795` 上游原文：
  ```cpp
  2791:	/* End of track? */
  2792:	if (!ft.Follow(tile, trackdir)) {
  2793:		/* Last tile of a terminus station is a safe position. */
  2794:		if (include_line_end) return true;
  2795:	}
  ```
  注释 `Last tile of a terminus station...` 就是上游原话，不是 R3R 写的。
- 全代码库 **7 个调用点全部传 `true`**，`true` 是上游统一口径，不是 R3R 的特例：
  `train_cmd.cpp:17451`、`train_cmd.cpp:19287`、`train_cmd.cpp:19353`、`pbs.cpp:1565`、
  `yapf_rail.cpp:238`（原生 `FindSafePositionProc`）、`yapf_destrail.hpp:188`、
  `yapf_costrail.hpp:733` / `:740`、`rail_cmd.cpp:702`。
  ⇒ **「跟随失败 ⇒ 尽头 ⇒ 可停」这条规则是原生的，不能算 R3R 引入。**

### 9.2 R3R 侧的偏离：只搬了 `safe`，没搬配套的 `free`

原生在**判「这格能不能当终点」**时是**成对**用的（`src\pathfinder\yapf\yapf_destrail.hpp:186-190`）：

```cpp
186:	inline bool PfDetectDestination(TileIndex tile, Trackdir td)
188:		return IsSafeWaitingPosition(Yapf().GetVehicle(), tile, td, true, !TrackFollower::Allow90degTurns()) &&
189:				IsWaitingPositionFree(Yapf().GetVehicle(), tile, td, !TrackFollower::Allow90degTurns());
```

即：**安全 ∧ 空闲** 才算合法终点。

R3R 在 `yapf_rail.cpp` 的两个调用点**只调了前一个**：
- `:786`（反向扫「路径上最后一个安全位」，决定 `r3r_last_safe`）
- `:903`（决定是否在此截断 / `REG-399-C` 豁免）

且注释 `:884-891` 明确写了**故意不调** `IsWaitingPositionFree`：
>「IsWaitingPositionFree 不排除自己，先问它会把自有格误判成占用、把终点收到车尾后面」

改成用「`ReserveSingleTrack` 能不能订上」来代替 free 检查。
问题在于：**「订得上」对道岔格不构成否决** —— 道岔空着、没被谁预留，就订得上。
原生靠 `IsWaitingPositionFree` 挡不住的东西，R3R 用「订得上」同样挡不住，
而「是不是道岔」两边都没判（`IsSafeWaitingPosition` 内部没有任何道岔概念）。

⇒ **可归因为 R3R 的，是「截断点没有沿用原生的成对判据」这一处**；
`IsSafeWaitingPosition` 本身不背这个锅。

### 9.3 输入也是挂接侧独有的

原生驾驶预留（`ExtendTrainReservation`）判的是「列车自己前方能停哪」；
挂接侧判的是「**目标车底占住的那条线**前方某格能不能停」。
本现场的输入本身就是原生不会遇到的：
- 目标车底占满 `1424..1437,1874`；
- 站台整段跳过让 `1439,1874 → 1424,1874` 一步到底（KI-681）；
- 道岔格 `1457,1874` 被挤出「四向 `Follow` 全 `ok=0`」的形态（见 §十）。

⇒ 结论：**没有任何一方单独该背锅，但修复点只能在 R3R 侧**（`yapf_rail.cpp` 两个调用点），
`pbs.cpp` 的 `IsSafeWaitingPosition` 一行不该动（动它会波及驾驶预留、拆轨保护、YAPF 目的地检测共 7 处）。

## 十、撤回「道岔死端」这个说法（玩家指出用词不成立）

我之前写的「道岔死端」是自造词，**不准确，撤回**。准确描述分两层：

1. **函数在问什么**：`IsSafeWaitingPosition` 只在问「车从这一格往前走得出去吗」，
   `Follow` 失败 ⇒ 上游把它解释成「到线路尽头了（end of line）」⇒
   尽头站台最后一格可以停（理由：前面本来没路，停这不挡人）。
   **它从来不问「这一格本身是不是道岔」，也不问「这一格是不是空着能进」。**

2. **本现场 `1457,1874` 的实况**（`build\R3R_debug.log` 本轮原文）：
   - `2222: CPL-ORIGIN-TD veh=15 tile=1457,1874 tdb=0x3030 tbits=0x30`
     `0x30 = TRACK_BIT_LEFT|TRACK_BIT_RIGHT`，即**两股斜线交叉**的一格（玩家肉眼看到的道岔），
     不是单轨尽头；
   - `2223: CPL-ORIGIN-FOLLOW veh=15 td=4  trk=4 ok=0 nt=-1,-1 nbits=0x100 st=0 skip=0`
   - `2224: CPL-ORIGIN-FOLLOW veh=15 td=5  trk=5 ok=0 nt=-1,-1 nbits=0x10  st=0 skip=0`
   - `2225: CPL-ORIGIN-FOLLOW veh=15 td=12 trk=4 ok=0 nt=-1,-1 nbits=0x2000 st=0 skip=0`
   - `2226: CPL-ORIGIN-FOLLOW veh=15 td=13 trk=5 ok=0 nt=-1,-1 nbits=0x1   st=0 skip=0`
     ⇒ 四个 trackdir 全 `ok=0`（`nt=-1,-1` 即 `new_tile` 无效）。

3. **关键：代码在这条路上不做任何复核就返回**。`pbs.cpp:2792-2795` 是
   `if (!ft.Follow(...)) { if (include_line_end) return true; }` ——
   **直接 return，连 `new_td_bits` 都不看**。所以：
   - 「`Follow` 失败」既可能是「前面真没路」，也可能是「这个 trackdir 出不去」
     （弯道限制 / 出口不通 / 90° 弯禁止）；
   - 代码**没有区分**这两种情况，一律按「尽头」处理。
   本现场四行 `nbits` 非零（0x100 / 0x10 / 0x2000 / 0x1）而 `nt=-1,-1`，
   说明不是「无轨道连接」意义上的空位，更可能是「该方向出不去」；
   但**具体是弯道还是出口不通，本轮没取到证据，记为 U-4，不下结论**。

⇒ 所以玩家「肉眼确定这个道岔不是安全停车位」是对的，而代码判的是另一件事
（能不能往前开），两套标准根本不是一回事。这也是 §9.2 那个缺口为什么会漏过：
`IsSafeWaitingPosition` 天生判不出「不是安全停车位」。

### 本轮新增未确认

- U-4：`1457,1874` 四个 trackdir 的 `Follow` 失败具体原因（弯道限制 / 出口不通 / 90° 弯禁止），
  四行 `nbits` 非零但语义未定 —— 需要读 `CFollowTrackT::Follow` 的失败分支并配该格四方邻格轨道再判。
  是否影响修法：**不影响 M1**（M1 直接要求「单轨格」即可绕开全部四向失败形态）。

## 十一、玩家质疑（2026-11-24）「挂接寻路不是已经向原生对齐了吗」

> 玩家原话：「你确定吗，我记得我们的挂接寻路已经向原生寻路做了对齐的。挂接寻路和原生寻路
> 在行为上有偏差，就代表它们某处有没对齐的地方。我需要你向我明确：造成行为偏差的原因
> 出在哪处的代码偏差」

**待办（REG-504-A）**：把原生侧与挂接侧的「安全停车位」判据做**逐参数对齐表**，
定位到具体行号的代码偏差，而不是停在「R3R 只用了 IsSafeWaitingPosition」这种笼统表述。

对齐表必须覆盖 5 个维度（缺一不可）：
1. `include_line_end` 实参；
2. `forbid_90_deg` 实参（原生 `yapf_destrail.hpp:188-189` 传 `!TrackFollower::Allow90degTurns()`，
   挂接侧传的是什么**未核实**）；
3. 是否配套 `IsWaitingPositionFree`；
4. 调用的判定位置（终点检测 `PfDetectDestination` vs 路径截断 `r3r_last_safe`/`:903`）；
5. 被判定的车辆（`Yapf().GetVehicle()` 原生 = 自己；挂接侧 = `r3r_coupler`）。

**已核实部分**（见 §九 9.2）：维度 1 两侧同为 `true`；维度 3 原生成对、挂接侧只用一个；
维度 4 位置不同（原生是终点检测，挂接侧是路径截断，语义本就不同，不可直接对比）。

### 11.5 五维对齐表结论（REG-504-A 收口）

| # | 维度 | 原生（`yapf_destrail.hpp:186-190`） | 挂接侧（`yapf_rail.cpp`） | 判定 |
|---|---|---|---|---|
| 1 | `include_line_end` | `true` | M1 起 `:805-813`（反扫）、`:942-943`（正扫）传 `false` | **有意偏离**：原生只当"当期预留终点"（下一 tick 从车头重算），挂接侧当"整条路径截断开关"；故配 `r3r_single_track_tile` 收紧，语义见 §12.1 |
| 2 | `forbid_90_deg` | `!TrackFollower::Allow90degTurns()` | `:759` `_settings_game.pf.forbid_90_deg` | **同源同值**：`CYapfCoupleRail`（Allow90）与 `CYapfRail_No90` 成对按同一设置切换 ⇒ `Allow90degTurns() == !pf.forbid_90_deg`。§九 对维度 2 的怀疑**撤回** |
| 3 | `IsWaitingPositionFree` | 与维度 1 成对 `&&` | 不配套，用"`ReserveSingleTrack` 订得上"代替（`:909-913` 注释自陈） | **有意偏离**，但"订得上"对空着的道岔不构成否决 ⇒ 本现场漏判的直接原因（M1 用更硬的几何判据替代其否决力，理由见 §12.2） |
| 4 | 判定位置 | `PfDetectDestination`（YAPF 终点检测，当期一次） | `r3r_last_safe` 反扫（`:805-813`）+ 正扫截断（`:942-943`） | **语义不同**，不可直接对比；这是行为分叉的根本来源 |
| 5 | 被判定的车辆 | `Yapf().GetVehicle()` | `r3r_coupler = Train::From(Yapf().GetVehicle())`（`:758`） | **一致** |

⇒ 偏差收敛到两处：维度 3（缺 `IsWaitingPositionFree`）+ 维度 4（把"当期终点判据"当"整条路径截断开关"）。M1 不引入 `IsWaitingPositionFree`（它会因平台整格位语义误判会合点），改用"单轨格"这一更强的几何限制。


## 十二、M1 落地与构建自证（2026-11-24）

**玩家拍板原话**：「那么你先按M1修复吧」。

### 12.1 改动清单（仅 `src\pathfinder\yapf\yapf_rail.cpp`，未碰 `pbs.cpp` / 任何 `src\*.h`）

| # | 位置 | 改动 |
| --- | --- | --- |
| 1 | `:760-779`（新增，紧跟 `:759` `r3r_forbid_90deg`） | 文件内 lambda `r3r_single_track_tile(TileIndex)`：`IsPlainRailTile` → `GetTrackBits` → `tb != TRACK_BIT_NONE && KillFirstBit(tb) == TRACK_BIT_NONE` |
| 2 | `:805-813`（倒扫「最后一个安全位」，原 `:786`） | `r3r_single_track_tile(...) && IsSafeWaitingPosition(..., false, ...)` |
| 3 | `:942-943`（正扫截断判据，原 `:903`） | 同 2，`include_line_end` 由 `true` 改 `false` |
| 4 | `:954/:966` 两行探针 | 末尾补 `tbits=%d`（`CPL-RES-TERM-EXEMPT` / `CPL-RES-SAFE-STOP`），供复测判据 1/2 自证 |

两个判据点**必须同一条尺子**：只改正扫会让 `r3r_last_safe` 仍算在道岔上 ⇒ `i == r3r_last_safe` 永不成立 ⇒ `term_exempt` 变死代码（等于悄悄去掉 REG-399-C 的自锁修法）。故倒扫一并收紧。

### 12.2 刻意未做：`IsWaitingPositionFree` 配对（维度 3 仍是偏差）

`pbs.cpp` 的 `IsWaitingPositionFree` **不排除自己/同链的位**（只豁免 `R3RReservationHeldByCouplePartner`），而挂接侧是「先订后判」，循环里的格大多已被自己上一 tick 铺过 ⇒ 成对调用会在 `i==0` 即失败、`booked=1`，比不判更差（`yapf_rail.cpp:909-913` 原注释已警示此点）。要成对必须先给 `pbs.cpp` 加自订豁免，波及 7 个调用点，超出 M1 范围；且「第三方占用」本就被 `ReserveSingleTrack` 的失败分支覆盖，`free` 真正多出来的只有 TraceRestrict slot / `TRPRF_RESERVE_THROUGH` 等 NewGRF 条件，与本次现场无关。⇒ 登记为遗留偏差，另开轮次。

### 12.3 已知收窄副作用（有意接受）

`r3r_single_track_tile` 要求 `IsPlainRailTile` ⇒ 道口（`TileType::Road`）、隧道/桥信号入口上的信号**也不再作截断点**。原生在这两类格上会判「安全位」，但挂接侧少截断一次只是多订一段（兜底=订到会合点），不越权、不自锁；最常见形态（单轨信号位）不受影响。

### 12.4 构建自证

- 入口：`R3R_fullbuild.cmd inc detach`（复用既有脚本，未新建 `.cmd`）
- `build\R3R_incbuild.guard.log`：`incremental is safe`
- `build\R3R_incbuild.log`：`[5/5] Linking CXX executable openttd.exe`，`error`/`FAILED`/`build stopped` 计数 0
- `build\R3R_incbuild.done`：`EXIT_CODE=0`
- 时间戳链：`src\...\yapf_rail.cpp` 18:27:44 → `yapf_rail.cpp.obj` 18:28:45 → `build\openttd.exe` 2026-11-24 18:30:22（54 380 544 B）
- `read_lints`：0 条
- 产物字面量自证：`findstr /m /c:"booked=%u tbits=%d" build\openttd.exe` 命中 ⇒ 新探针/新判据已链入

### 12.5 复测判据（沿用 §七，补两条）

1. 不再出现 `CPL-RES-TERM-EXEMPT ... tile=1457,1874`（原日志 2181 行那种）；**任何** `CPL-RES-TERM-EXEMPT` 的 `tbits` 都应为单比特。
2. `CPL-RES-SAFE-STOP ... tbits=` 必为单比特（`1/2/4/8`，不是 `0x30`/`3`/`0xc`）。
3. `CPL-RES-OK` 的 `tiles=` 不再横跨 1875→1874 平行线（P2 是否自愈的观察点）。
4. `CPL-ENTRY`/`CPL-RESERVE` 的起点不再落在双槽道岔。
5. 负判据：正常尽头站台挂接仍能成功（`COUPLE-OK`），`REG-399-C` 的尽头豁免不被误杀。
6. 负判据：普通单轨线路的挂接预留 `booked=` 不显著变长（少截断只发生在道岔/道口/隧道桥上）。

### 12.6 未做 / 未确认（本轮收尾状态）

- **P2（KI-681）未动**：玩家口径「P2 这个应该是 R3R 引入的东西了，但是先别修，我先看看」⇒ M2 不做；M3（只做 M1、观察 P2 是否自愈）仍在观察，§12.5 第 3 条即其判读点。
- **U-4 未取数**：`1457,1874` 四向 `Follow` 全部失败的具体原因（弯道限制 / 出口不通 / 90° 弯禁止）仍未定位；M1 不依赖它（单轨判据已绕开全部四向失败形态，故"为什么失败"只影响 `tbits` 之外的旁证）。
- **未实机复测**：§12.5 六条判据全部待玩家跑日志。
- **回归面**：`pbs.cpp` 一字未动 ⇒ 驾驶预留 / 拆轨保护 / YAPF 目的地检测 / `ReserveTrackUnderConsist` 行为逐字不变；改动只影响挂接寻路（`CYapfCoupleRail`）的预留长度与终点格。

### 12.7 复测结果（2026-11-24，负判据命中 ⇒ M1 未生效）

**样本有效性**：`build\R3R_debug.log` 305 729 B @2026-11-24 18:37:36，晚于 M1 版 `build\openttd.exe`（54 380 544 B @18:30:22）⇒ 样本出自 M1 exe，不是旧 exe。

**全日志计数**

| 探针 | 次数 |
|---|---|
| `CPL-RES-OK` | 3 |
| `CPL-RES-TERM-EXEMPT` | 0 |
| `CPL-RES-SAFE-STOP` | 0 |
| `COUPLE-FAIL` | 30 |
| `COUPLE-OK` | 2 |

**§12.5 判据 1/2 = 无数据**：`term_exempt=0`、`safe_stop=0` ⇒ M1 的截断判据在这条路线上**一次都没成立**，`booked` 恒等于 `total`。与修复前对照（旧日志 `2197: CPL-RES-OK ... term_exempt=1`），M1 确实生效（不再把道岔当安全位），但代价是**唯一的安全位也没了**。

**三次预留（行 2162 / 2356 / 2441）**

```
2162: CPL-RES-OK veh=15 total=23 booked=23 stop_safe=0 term_exempt=0 tiles=1459,1875/0;1458,1875/0;1457,1875/4;1457,1874/5;1456,1874/0;...
2356: CPL-RES-OK veh=15 total=12 booked=12 stop_safe=0 term_exempt=0 tiles=...;1449,1874/0;...;1439,1874/0;1424,1874/0
2441: CPL-RES-OK veh=15 total=4  booked=4  stop_safe=0 term_exempt=0 tiles=1441,1874/0;1440,1874/0;1439,1874/0;1424,1874/0
```

⇒ ① 预留仍横跨 `1875→1874`（§12.5 判据 3 未自愈，符合 P2 未修的预期）；② 从第二次起 `1439,1874` 一跳 `1424,1874`，**把等待车底脚下的平台整格订进自己的路径**（KI-681 一字未变）。

**§12.5 判据 5（负判据）＝命中**：挂接止步于"拼接端点对"，链本身是健康的。

```
2477: CPL-HIT site=open loco=15 v=62 len_v=2 len_mf=2 min_diff=1 need=2 fit=2 dx=-1 dy=0 maxd=1 overlap=1 spd=160 dir=1
2482: FOLDCHK COUPLE n=57 worst_gap=221
2482: FOLDCHK-DIR COUPLE dot=223
2482: SPLICE-GAP-REJECT COUPLE prev=17 x=23015 y=29992 head=18 x=22792 y=29992 exp=2 dist=223
     FOLDCHK COUPLE-FLIP-U n=57 worst_gap=7 + OWNER-MOVE from=62 to=18
     FOLDCHK COUPLE-FLIP-V n=57 worst_gap=213 → FOLDCHK-ACCEPT（被判为"还在收拢"）
     SPLICE-GAP-REJECT COUPLE-FLIP-V prev=15 x=23007 head=18 x=22792 exp=2 dist=215
     FOLDCHK COUPLE-FLIP-BOTH n=57 worst_gap=1 但 COUPLE-PREFLIGHT head=17 last=15 gap=221 dirfold=1
2782: COUPLE-OK loco=17 rear=18 tx=1438 ty=1874
```

**判定**：命中对是 `15 ↔ 62`（相邻 1px，`overlap=1`），拼接却用 `prev=17 ↔ head=18`，两者横向相距 223px ≈ 14 格（`1438,1874` − `1424,1874`）。等待链头 `18` 物理上在它自己的尾端（`2777: veh=18 tile=1424,1874 ... starB=1`）⇒ "命中在 62、代码拼在 18"必然错开整条车底。这是 KI-330/KI-340 同族的**命中端≠拼接端**（记忆 49951234 修法 B），M1 把它从"够不到"变成了"够到了但拼不上"。

⇒ 结论：**M1 属于"机制正确、症状未解决"**——它把现场从"卡在道岔前订不出去"推进到"能走到平台边并命中"，但没有让挂接成功；玩家看到的"修复没生效"成立。

**新疑点 REG-504-B**：`booked==total` 且 `term_exempt==0` 同时出现时，现有探针无法区分 (a) 正常"订到终点"、(b) "全路无截断点、坠落到订满"。需给 `CPL-RES-OK` 补 `trunc=`（是否发生过截断）与 `lastsafe=`（最后一个被判安全的格），否则下一轮仍无法判读。

**待拍板（C-1 / C-2）**

- **C-1** 现场负判据的具体形态（选一）：(1) 机车停在道岔/平台前不动；(2) 机车开到平台边、与车底贴住但挂不上，重试后又退开；(3) 车底那侧仍显示黑色预留；(4) 其它（请描述）。
- **C-2** M1 去留：(a) 回滚 M1（恢复 `include_line_end=true`）；(b) 保留 M1，接着修"命中端≠拼接端"（治本但动 `Couple()` 端点选择）；(c) M1 收窄——单轨门禁保留，但**禁止把目标车底脚下的格订进路径**（遇目标链的格即截断），先止住"订到会合点"。

> 注：C-1/C-2 未回填前不动源码。下一轮的改动方向取决于 C-1（是"不动"还是"拼不上"）与 C-2（治截断还是治端点）。

---

## §13 为什么挂接寻路"不走 TryReservePath"（2026-11-24 玩家提问，纯代码取证，零改动）

**玩家原话**：「我发现了一个问题：为什么挂接寻路不走 TryReservePath」

### 13.1 事实纠正：它不是"不走"，而是"分成两支"

`CYapfFollowCoupleRailT::FindNearestCoupleTrain()`（`yapf_rail.cpp:1640`）的预留段：

```2129:2162:src/pathfinder/yapf/yapf_rail.cpp
		if (!dont_reserve) {
			bool reserved = false;
		size_t r3r_booked = 0;
		if (r3r_single_seg) {
			...
			if (!r3r_resv_path.empty()) {
				const R3RCoupleStep &r3r_last = r3r_resv_path.back();
				this->SetReservationTarget(pNode, r3r_last.first, r3r_last.second);
				reserved = this->R3RReservePathSequence(r3r_resv_path, r3r_booked);
				...
			}
		} else {
			reserved = this->TryReservePath(nullptr, pNode->GetLastTile(),
					/*atomic*/ true, /*check_target_free*/ true, /*record_answer*/ false);
		}
```

- `r3r_single_seg == true` → `R3RReservePathSequence()`（`:2145`，定义 `:744`）
- `r3r_single_seg == false` → `TryReservePath()`（`:2160`，定义 `:1078`）

所以 `TryReservePath` 那一支**存在但几乎进不去**。

### 13.2 真正的可达性：`else` 支是事实死路

`r3r_single_seg` 全文件只出现三处（`grep r3r_single_seg` → 3 hits）：

| 位置 | 内容 |
|---|---|
| `:1877` | `bool r3r_single_seg = false;`（初值 false） |
| `:2118` | `r3r_single_seg = true;`（**唯一赋值**） |
| `:2132` | `if (r3r_single_seg)` |

`:2118` 位于 `if (best_td != INVALID_TRACKDIR)`（`:2033`）之内、`if (!dont_reserve)`（`:2035`）之内。因此：

- `!dont_reserve` 且 `best_td` 有效 ⇒ 恒置 true ⇒ 走 `R3RReservePathSequence`
- `!dont_reserve` 且 `best_td == INVALID_TRACKDIR` ⇒ 保持 false ⇒ 走 `TryReservePath`，**但 `:2126` 已把 `next_trackdir = INVALID_TRACKDIR`**，`:2171` 是 `return reserved ? next_trackdir : INVALID_TRACKDIR;` ⇒ **无论 `TryReservePath` 成功与否，返回值都是 `INVALID_TRACKDIR`**。
- `dont_reserve == true` ⇒ 整块 `if (!dont_reserve)` 跳过（`:2129`），那一支连碰都碰不到。

⇒ 现场之所以"看不到 `TryReservePath`"，是因为单段摸路成功时它被 `if (r3r_single_seg)` 挡掉了；`else` 支只剩"算不出出发 trackdir"的兜底，且兜底必报失败。

### 13.3 为什么单段支必须绕开 `TryReservePath`（设计原因，四条）

1. **`check_target_free=true` 会自我否决**。`TryReservePath` 入口 `:1105` 有
   `if (check_target_free && !IsWaitingPositionFree(veh, res_dest_tile, res_dest_td, false, &st)) return false;`
   而挂接的目标格（`KillFirstBit` 后的 `consist_head_tile` / `pNode->GetLastTile()`）**本身就压着等待车底**。`:2160` 能跑通是靠第 503 轮引入的 `pbs.cpp` partner-air 豁免；单段支不依赖这条豁免，直接跳过判定。
2. **`atomic=true` 与 KI-563 的粒度口径冲突**。`:2160` 传 `atomic=true` = 格级失败整段回滚（全有或全无）；而 `:2133-2141` 的注释写明单段支要的是"订到第一个失败格即停、保留已订格"，并在部分预留时把预留终点收缩到最后一格已订（`:2149-2152`）。两者语义相反。
3. **两者走的不是同一条线**。`TryReservePath` 沿 `res_dest_node->parent` 节点链 + `IterateTiles` 展开（`:1132-1134`），那是 **YAPF 代价最优搜索树**，其 segment 跨站台合并（`:1871-1872` 注释：按节点链订"会把车底整段一起订进去"）。`rp()` 摸路是从 `best_start` 沿 `best_td` 的**物理 `TrackFollower` 遍历**，止于 `tg`（会合点）、跳过站台/车库格（`:2059-2065`）。两者不同源，这正是 KI-340「命中端≠拼接端」的土壤。
4. **`record_answer=false`**：挂接路径的几何每 tick 随车底变化，不能进答案本（`R3RAnswerCommit` 只在普通链 `:2415` 调用）。

### 13.4 由此浮出的新疑点 REG-504-C（未修，待拍板）

`best_td == INVALID_TRACKDIR` 时落到 `:2160`，`TryReservePath` 仍可能**真的把整条路径订下去**（副作用），随后 `:2171` 返回 `INVALID_TRACKDIR`。上层 `ChooseTrainTrack()` 见"无路径"会走 KI-316 的 `FreeTrainTrackReservation()` + `MarkTrainAsStuck()`，把刚订的预留再清一次 —— 净效果是"空转一次订+清"，不造成残留，但属于逻辑瑕疵（订了却报没订）。

- 判据：现场若出现 `CPL-TRACE-RESULT ... best=-1` 且同一 tick 有 `CPL-RESERVE ... reserved=1 next=-1`，即命中。
- 修法（待拍板，未落地）：把 `:2160` 改成 `reserved = false;` 并补注释说明 else 支已失效；或真正修 `best_td` 缺失路径（属于 KI-504 E2/E4 的收尾：`rp()` 由 `ExtendTrainReservation` 取代）。
- 与之配套的长期方向（`yapf_rail.cpp:2046-2056` 注释原文）：`E2 replaces this with ExtendTrainReservation, E4 deletes rp() altogether` ⇒ `R3RReservePathSequence` 与 `rp()` 都是过渡态，`TryReservePath` 那支是 S2 改造前的残留。

---

## §14 挂接寻路为什么不用 `FindSafePositionOnNode`（2026-11-24 玩家追问，纯代码取证，零改动）

**玩家原话**：「挂接寻路为什么不用 FindSafePositionOnNode」

### 14.1 调用点全集：挂接链路一次都没调

`grep "FindSafePositionOnSafePositionNodes\(|FindSafePositionOnNode\(|FindSafePositionProc\("` → 全文件 5 处出现，**无一在 `CYapfFollowCoupleRailT`（`:1632` 起）内**：

| 行号 | 位置 | 归属 |
|---|---|---|
| `:236` | `bool FindSafePositionProc(TileIndex, Trackdir)` 定义 | `CYapfFollowRailT` 的私有回调 |
| `:1022` | `inline bool FindSafePositionOnNode(Node *node)` 定义 | 同上 |
| `:1048` | `void FindSafePositionOnSafePositionNodes()` 定义 | 同上 |
| `:1052` | 内部调 `FindSafePositionOnNode(*it)` | 同函数体 |
| `:2258` | 调用点 ① | `CYapfFollowAnySafeTileRailT`（`YapfTrainFindNearestSafeTile` 专用） |
| `:2401` | 调用点 ② | `CYapfFollowRailT::ChooseRailTrack()`（普通寻路） |

⇒ 挂接链路的安全位判定是**自己实现的**，落在 `R3RReservePathSequence()`（`:744`）里两处：

```804:812:src/pathfinder/yapf/yapf_rail.cpp
				/* R3R ... 反向扫描最后一个安全位 */
				if (r3r_single_track_tile(steps[k].first) &&
						IsSafeWaitingPosition(r3r_coupler, steps[k].first, steps[k].second, false, r3r_forbid_90deg)) {
```

```944:949:src/pathfinder/yapf/yapf_rail.cpp
			if (!IsRailStationTile(stile) && r3r_single_track_tile(stile) &&
					IsSafeWaitingPosition(r3r_coupler, stile, stdir, false, r3r_forbid_90deg)) {
```

### 14.2 四条不能复用的理由

1. **判据口径相反（M1 的核心）**。`FindSafePositionProc` 是
   `IsSafeWaitingPosition(veh, tile, td, /*include_line_end*/ true, !Allow90degTurns())`（`:238`）——**把线路尽头和道岔都算安全位**。而挂接 M1 要的正是把这两类剔除：`r3r_single_track_tile()` 只认单轨格，`include_line_end` 传 `false`。若挂接改调 `FindSafePositionOnNode`，等于把 KI-680/681 的病根请回来，M1（`:775/:811/:947`）整套白做。
2. **终点语义不同**。`FindSafePositionOnNode` 回答的是「这条预留最远能订到哪个**安全停车位**」——它的消费者是 `ChooseRailTrack()` 的 `res_dest_node`（`:2392-2400` 回溯 safe_position_nodes），随后交给 `TryReservePath` 铺路（`:2413`）。挂接的终点不是「停下来等」，而是「开到**会合点**对接车底」，终点已被 `CYapfDestinationTrainRailT::PfDetectDestination`（`yapf_destrail.hpp:352`）定死在车底邻格（`CPL-BEST` / `CPL-GATE` 四类拒绝）。它要的不是「最远安全位」，而是「沿会合点路线、把不该订的尾巴切掉」。
3. **展开方式对不上**。`FindSafePositionOnNode` 沿 `res_dest_node->parent` **节点链** + `IterateTiles` 展开，那是 YAPF 代价最优搜索树（`§13.3` 第 3 条：节点链会跨站台合并、按它订「会把车底整段一起订进去」）。挂接的 `r3r_resv_path` 是 `rp()` 用 `TrackFollower` **物理遍历**摸出来的、止于 `tg`、跳过站台/车库格。给一套物理格序列去找节点链上的安全位，索引根本对不齐。
4. **语义耦合：安全位判定必须和「逐格订、遇阻停」绑在一起**。挂接要 KI-563 的原生粒度——订到第一个失败格即停、保留已订格（`:2133-2141`），并在部分预留时把终点收缩到最后一格已订（`:2149-2152`）。这是**边订边判**（判据里带着 `ReserveSingleTrack()` 的成败）。`FindSafePositionOnNode` 是**先回溯定终点、再一次性铺路**（`TryReservePath` 的 `atomic` 全有或全无），结构性拆不开。

### 14.3 一句话

`FindSafePositionOnNode` 是**普通寻路给 `TryReservePath` 准备输入的**；挂接既不走 `TryReservePath`（`§13`：`:2160` 那支实为死路），也就没有消费这套输入的地方，于是自己在 `R3RReservePathSequence` 里另铸了一把尺子（`r3r_single_track_tile` + `include_line_end=false`）。**两把尺子并存正是当前"道岔不再截断"与"预留仍横跨 1875→1874"同时出现的根源**——M1 只改了挂接那把，普通那把（`:238` 仍 `include_line_end=true`）没动。

### 14.4 顺带记一笔注释行号漂移（低，无需修码）

`yapf_rail.cpp:904-906` 的注释写 `FindSafePositionProc()（:162）用的都是它`，但该函数实际定义在 `:236`（`:162` 早已不是它）。纯注释漂移，不影响行为，本轮未动。

---

## §15 「为什么挂接和原生要在 ChooseTrainTrack 就分支」——玩家方案评估（2026-11-24，纯分析，零改动）

**玩家原话（情绪保留）**：「我愤怒了，我咆哮着：为什么挂接和原生要在 ChooseTrainTrack 就分支。不能这样吗：R3RAnswerReplay 如果是挂接寻路就禁用。SetDestination 如果是挂接寻路就寻到车底所在格（可以做到端点静态）。我们让挂接寻路进了 ChooseRailTrack 再分支不行吗？你可以诚实的给我否定的答案，但那样我一定会骂你诚实但愚蠢。」

### 15.1 先纠正三个前提（其中两个已实现，不该再作为「改动项」）

| 玩家方案的前提 | 实际状态 | 证据 |
|---|---|---|
| ① 挂接要禁用 `R3RAnswerReplay` | **已天然满足** | `R3RAnswerReplay` 全文件只被 `ChooseRailTrack` 调一次（`:2361`）；`FindNearestCoupleTrain` 全程不碰，且 `:2160` 那支显式传 `record_answer=false`。 |
| ② `SetDestination` 挂接要寻到车底所在格，端点可静态 | **已实现，且比玩家设想更激进** | `FindNearestCoupleTrain` 在 `:1705` 调的就是 `Yapf().SetDestination(v)`，与 `ChooseRailTrack:2369` **同一个** `CYapfDestinationTrainRailT`；第 502 轮（KI-676, S4）已加 **静态终点接管** `Yapf().R3RStaticTargetTile()` / `R3RStaticTargetVehicle()`（`:1743-1749`），命中 S2b 快照就整块跳过动态解析。同一段注释（`:1690-1699`）还记着 KI-429「等待区路点截断」已整机制删除，终点判定回到普通口径。 |
| ③ 两条路只在 `ChooseTrainTrack` 分支 | **不准确：真正的分发点在下面一层** | `DoTrainPathfind`→`YapfTrainChooseTrack`（`train_cmd.cpp:17155-17159`）vs `DoTrainCouplePathfind`→`YapfTrainCoupleTrack`（`:17172-17175`）；对应 `stChooseRailTrack`（`yapf_rail.cpp:2315`）与 `stFindNearestCoupleTrain`（`:1632`），实现在 `:2526` 与 `:2922`。 |

⇒ 玩家要的两件事里，**②已经做完了**（甚至已经做到静态快照），**①本来就不存在**。所以「进 `ChooseRailTrack` 再分支」剩下的唯一实质内容 = **把预留那一段也合并进 `ChooseRailTrack`**。

### 15.2 技术上可行（我不否认）

`CYapfCoupleRail` 与 `CYapfRail` 的 `Node`/`Key` 同构、`SetDestination(v)` 同源、`FindPath(v)` 同引擎，差异集中在 follow 类的 `PfFollowNode`（`:1616-1624` 多一条「车库格不穿透」）与预留段。**把 `couple_mode` 作为 `ChooseRailTrack` 的一个 bool 传进去，让 `:2411-2417` 的预留块按模式二选一，是能编过的。**

### 15.3 真实阻力（五条，按硬度排序）

**D-1（最硬，无法用 flag 抹平）：两段预留没有一行共享代码，合并只会搬家不会减少分支。**
- 原生（`:2388-2417`）：`SetReservationTarget` → 回溯 `node->parent` 建 `safe_position_nodes` → `FindSafePositionOnSafePositionNodes()` → `TryReservePath(target, ...)` → `R3RAnswerCommit`。
- 挂接（`:2129-2162`）：`R3RReservePathSequence(r3r_resv_path)`（定义 `:744`），含反向扫 `:804-812` + 正向扫 `:944-949`，判据 `r3r_single_track_tile` + `include_line_end=false`，边订边停 + 收缩终点（`:2149-2152`）。
- 合并后必然长成 `if (couple_mode) { …~200 行… } else { …~30 行… }`。`ChooseTrainTrack` 里的分支没有消失，只是**从 `yapf_rail.cpp:2526` 挪进 `yapf_rail.cpp:2411`**。这是搬家，不是简化。

**D-2：返回值契约不同，不能在同一个 return 点合流。**
- 原生：`next_trackdir = prev->GetTrackdir()`（`:2409`），语义是「从 origin 迈出的**第一步**」。
- 挂接：`best_td`/`next_trackdir` 是「摸路的**出发方向**」，且 `:2171` 是 `return reserved ? next_trackdir : INVALID_TRACKDIR` —— **预留成败直接决定返回值成败**。原生那条即使 `TryReservePath` 失败也照常返回 `next_trackdir`（只是不进答案本）。

**D-3：`dest` 输出会被 `ChooseTrainTrack` 的后续流程二次消费，挂接不能进那条链。**
`train_cmd.cpp:19391` 用 `res_dest.tile / res_dest.trackdir` 递归调用 `TryLongReserveChooseTrainTrack()`（`:18116` → 再进 `ChooseTrainTrack` `:18223/18249`）。挂接路径**不能**进 LR 递归（与 `record_answer=false` 同源理由：几何每 tick 随车底变）。合并后必须给 `dest` 附一个「挂接不许 LR」的语义标记，于是 `ChooseTrainTrack` 里**要多出一个分支**来读这个标记 —— 分支净增。

**D-4：`FindNearestCoupleTrain` 的入口挂着一整套现场取证产物，搬进 `ChooseRailTrack` 会被别的调用者误触发。**
`R3RCoupleScanReset()`（`:1646`）、`R3RCoupleScanHold` 节流（`:2903-2911`）、`CPL-ENTRY/CPL-ORIGIN/CPL-PATHFOUND/CPL-SCAN` 探针族、`_r3r_couple_path_blocked`（`:1703`）。`ChooseRailTrack` 还被 `CheckReverseTrain`（`:2526` 同一 `stChooseRailTrack`）与 desync 双跑（`:2324-2327` 第二实例 `DisableCache(true)` 重跑一次）间接共享 —— 挂接探针会被**每个需要反向判定的普通列车**唤醒一次。

**D-5：`OT_GOTO_COUPLE` 在 `ChooseRailTrack` 里已经有一个各自的解释，两者定义冲突。**
`:2346-2354` 现存一段 `if (v->current_order.IsType(OT_GOTO_COUPLE) && !v->current_order.GetCoupleIsDepot())` 的 R3R 特例，注释写「when coupling, start the path from the loco itself」。这段在 `YapfTrainCoupleTrack` 接管挂接寻路之后，**很可能已是死代码**（挂接不再走这里），但没人敢删——因为它同时覆盖「车库型挂接 `GetCoupleIsDepot()` 必须不走这条捷径」这个活语义。若把挂接也搬进来，这个函数里就会有**两套 `OT_GOTO_COUPLE` 语义并排**，这正是 KI-504 E2/E4 想收口的东西。

### 15.4 结论

- **不是「不能」**：技术上能编过、能跑通，玩家方案的两条前提已经实现（§15.1）。
- **是「收益为负」**：合并后 `ChooseRailTrack` 变成带 5 个挂接特例的巨型函数，分支从「2 个入口各一分支」变成「1 个函数内 2 段互斥的 200 行」，同时 D-3 让 `ChooseTrainTrack` 净增一个分支。
- **真正该收口的方向不是「合并入口」，而是删除两遍法**：`yapf_rail.cpp:2046-2056` 注释已写明 `E2 replaces this with ExtendTrainReservation, E4 deletes rp() altogether`。E4 一旦完成，`r3r_resv_path`+`R3RReservePathSequence`+`rp()` 一起消失，此时 `FindNearestCoupleTrain` 与 `ChooseRailTrack` 的预留段**才会真正兼容**，合并入口才成立。在此之前合并 = 把过渡态焊死在 `ChooseRailTrack` 里，E4 反而更难做。

### 15.5 待拍板

- C-3：是否接受「先做 E4（删 `rp()`/两遍法），再合并入口」的顺序？还是要求**现在**就把入口合并（我按 D-1~D-5 逐条加注释落地，预期 `ChooseRailTrack` +200 行、`ChooseTrainTrack` +1 分支）。

---

## §16 「铺预留铺到不安全位置」的罪魁祸首定位（2026-11-24，纯代码取证，零改动）

**玩家原话**：「挂接寻路铺预留铺到了不安全的位置，哪个函数是罪魁祸首」

### 16.1 必须先分成两种「不安全」，混在一起就会误伤

| 形态 | 症状 | 罪魁函数 | 是否在预留层 |
|---|---|---|---|
| ① **路径本身错** | 预留铺到与车底平行的另一条股道（现场 1875/1876 vs 车底 1874） | `CYapfDestinationTrainRailT::PfDetectDestination`（`yapf_destrail.hpp`）+ 站台段判据 | **否**。预留只是忠实铺了 YAPF 给的错路径，改预留函数一辈子也修不好 |
| ② **路径对但预留越界** | 预留覆盖道岔 / 咽喉 / 不该停的路段，尽头不是安全位 | `R3RReservePathSequence`（`yapf_rail.cpp:744`） | **是**，本问对象 |

### 16.2 形态②的罪魁：`R3RReservePathSequence`，具体是「兜底初值 + M1 单轨判据」的组合

正向循环（`:817-982`）本来只有三条出路：

```
:820   ReserveSingleTrack(stile, stdir);        // 订
:821   if (res_fail_tile != INVALID_TILE) ...   // 订失败 ⇒ 停
:947   if (!IsRailStationTile(stile) && r3r_single_track_tile(stile) &&
           IsSafeWaitingPosition(..., false, ...))   // 安全位 ⇒ 截断
:949       if (i == r3r_last_safe) { term_exempt = true; continue; }  // 终点例外
:968       stopped_at_safe = true; break;
```

而 `r3r_last_safe` 由**反向扫**给出，初值是 `steps.size()`：

```798:815:src/pathfinder/yapf/yapf_rail.cpp
		size_t r3r_last_safe = steps.size();
		for (size_t k = steps.size(); k-- > 0; ) {
			if (IsRailStationTile(steps[k].first)) continue;
			if (r3r_single_track_tile(steps[k].first) &&
					IsSafeWaitingPosition(r3r_coupler, steps[k].first, steps[k].second, false, r3r_forbid_90deg)) {
				r3r_last_safe = k;
				break;
			}
		}
```

**失效链（这一点是关键，且是 M1 自己引入的）**：

1. `:810` M1 把判据收紧成「必须是**单轨格**」——道岔（`LEFT|RIGHT`、`0x30`）一律不算安全位。这是对的，注释 `:760-778` 写得很清楚。
2. 但 `:798` 的兜底初值 `steps.size()` 原本只表示「没有安全位」，现在它同时承载了「**所有候选安全位都是道岔**」这个新情况。
3. `:805` 反向扫还额外 `continue` 跳过全部站台格。站场里路径常常是「咽喉（全是道岔）→ 站台（全跳过）→ 会合点」，于是**反向扫可能一路扫回 `k=0` 都没命中，`r3r_last_safe` 保持 `steps.size()`**。
4. `r3r_last_safe == steps.size()` ⇒ `:949` 的 `i == r3r_last_safe` **恒假**（`i` 最大到 `steps.size()-1`）⇒ `term_exempt` 永远是 `false`。
5. 于是 `:947` 那个 `if` 在整条路径上**一次都不成立** ⇒ `stopped_at_safe` 恒 `false`、循环**从不 `break`** ⇒ 把 `rp()` 摸出来的每一格**全部订满到会合点**。

**⇒ 结论：M1 之后，「终端例外」已经结构性退化为「全程无截断」。** `:923-925` 注释自称的兜底（"全程没有任何安全位时循环照旧订满"）在道岔密集的站场里从"罕见兜底"变成了"常态路径"，而它的语义是**不截断**——这正是"铺到不安全位置"。

### 16.3 一眼判读（不需要再猜）

看 `CPL-RES-OK` 一行的三个字段组合：

| 组合 | 含义 |
|---|---|
| `stop_safe=1 term_exempt=0` | 正常：截在第一个安全位 |
| `stop_safe=0 term_exempt=1` | 正常终点例外：最后一个安全位之后订满到会合点 |
| **`stop_safe=0 term_exempt=0 booked=total`** | **病态**：全程没找到任何单轨安全位 ⇒ 一格没截 ⇒ 铺满。这就是嫌疑行 |
| `booked<total` 且 `CPL-RES-PARTIAL` | 他人预留/信号挡住，KI-563 行为正常 |

同时 `CPL-RES-SAFE-STOP` / `CPL-RES-TERM-EXEMPT` **一行都没有**，就是形态②的直接证据。

### 16.4 修法方向（待拍板，未落地）

- **D-1（最小改动，推荐先做）**：把 `:798` 的兜底与「终点例外」解耦。新增 `bool r3r_found_safe`（反向扫命中即置真）。循环里改成：
  - 命中 `i == r3r_last_safe` 且 `r3r_found_safe` ⇒ `term_exempt`（现有语义，保留）；
  - **`!r3r_found_safe` 时不得订满**：改为在 `steps.size()` 前最后一格停（即 `steps.size()-1`），或更保守——停在**最后一个非站台、非道岔单轨格**；都不存在则停在 `steps.size()-1`，保证"会合点格自己不订"（会合点由车底占着，本来也订不上）。
- **D-2（更贴原生）**：`r3r_found_safe == false` 时直接放弃本次预留（`return false`，`booked=0`），让 `TrainCoupleHandler` 下一 tick 重摸。代价=道岔密集站场可能永远订不上，不推荐单独用。
- **D-3（治根，与 §15 的 E4 合并做）**：删掉两遍法，改走 `ExtendTrainReservation`，安全位判据回到单一份。届时 `r3r_last_safe` / `term_exempt` 整块代码消失，本节问题结构性不存在。

### 16.5 一句话

**形态②的罪魁是 `R3RReservePathSequence` 里 `:798` 的兜底初值 `steps.size()`**——M1 把「道岔不算安全位」收紧之后，这个初值就从"罕见兜底"变成了"道岔密集站场的常态"，使 `:949` 的终点例外恒不触发、`:947` 的截断恒不成立，于是整条摸路被订满到会合点。**不是 M1 的判据错了，是 M1 只改了尺子、没改"量不到长度时怎么办"。**

### 16.6 待拍板

- C-4：形态② 是否按 **D-1** 落地（改 `:798-815` + `:947-981`，约 15 行，风险低、可现场验证）？
- C-5：形态①（平行股道）是否需要同时取证 `yapf_destrail.hpp` 的站台判据？该问题与预留层无关，须另开一轮。

---

## §17 玩家否决「终点例外」与「单轨格判据」（2026-11-24，口径修正，仍未改码）

### 17.1 玩家原话（逐条留档，不得摘要丢失）

> 「第一步，没问题。第二步，我们不是把等挂列车的预留当空气吗，它也是安全的啊，你为什么要加例外，例外加例外就等于意外了。第三步，我觉得你的理解是错误的。，所以我认为不是单轨格也要能当安全位」

三句对应：①「订到安全位就停」认可；②**终点例外不该存在**（理由：等待列车所在位置本身安全）；③**M1 的单轨格限制是错的**（理由：非单轨格也应能当安全位）。

### 17.2 取证：`IsSafeWaitingPosition` 里根本没有「车」这一维

`pbs.cpp:2777-2833` 全文判据只有五种，**没有任何一条检查格上是否停着车、是否有预留、是不是本车要挂的目标**；签名也没有「忽略某车」的参数：

```
2779  if (IsRailDepotTile(tile)) return true;
2782  if (HasBlockSignalOnTrackdir(tile, trackdir)) return true;
2784  if (IsTunnelBridgeSignalSimulationEntranceTile(tile) && IsTrackAcrossTunnelBridge(...)) return true;
2792  if (!ft.Follow(tile, trackdir)) { if (include_line_end) return true; }   // 尽头
2800  if (ft.new_td_bits == TRACKDIR_BIT_NONE) return include_line_end;
2805-2819 下一格单一路且有 PBS 信号 → 条件安全
```

⇒ **「把等挂列车的预留当空气」发生在寻路/仲裁阶段，不在安全位判据里。** 判据对「会合点」与「任意一个空道岔」给出**完全相同**的答案。

**这是 §16 遗漏的关键一环**：终点例外之所以必须存在，不是「会合点不安全」，而是**判据看不见「这格是我的目的地」**。

### 17.3 玩家的第②步成立，且比我给的 D-1 更根本

- **终点例外 = 给「判据失明」打的补丁**；M1 又让补丁失效（`steps.size()` 初值，§16.2）⇒ 玩家说的「例外加例外 = 意外」逐字成立。
- D-1 是「补丁的补丁」（找不到安全位就退一步兜底），玩家的方案是**删掉补丁的存在理由**。
- 结论：**D-1 作废，§16.4 整节降级为历史记录。**

### 17.4 玩家的第③步成立，但 M1 也对一半——两者合起来才是完整判据

M1 注释（`:764-767`）自陈「判据本身与原生一致，偏差在**用法**」。这是给自己免责，实际是**判据缺维度**。但 M1 排掉的那个具体用例是对的：

| 格 | 有等待列车？ | 原生判据 | 结论 |
|---|---|---|---|
| `1457,1874` 空道岔（M1 现场，离车底 1424~1437 有 20 格） | **否** | 四向 Follow 全失败 → 误判「尽头」 | **不是安全位**（M1 对） |
| 会合点 `1438,1874`（车底头） | **是** | 站台格 → 被跳过 | **是安全位**（玩家对） |
| 若有道岔上停着等待列车 | **是** | 可能误判尽头也可能 false | **是安全位**（玩家对） |

⇒ 正确判据不是「单轨格」也不是「原生判据」，而是：

```
安全位 ⇔ ① 那格上停着「本车要去挂的等待列车」（目的地，最高优先）
       ② 或 原生判据成立 且 不是道岔（空道岔一律不算，M1 保留）
```

### 17.5 由该判据推出的**简化**：整个 `r3r_last_safe` / `term_exempt` / `r3r_single_track_tile` 三块全部删掉

关键推论：**挂接预留的唯一合法截断点就是会合点**。

理由：①中段信号若截断 ⇒ 机车开到那里前面没订 ⇒ 自锁（这正是 REG-399-C 当年加终点例外的原因）；②中段道岔/站台不该截断；③目的地永远存在（挂接必然有目标车底）。

⇒ 正向扫只剩两条出路，逻辑退化为：

```cpp
/* 伪码：订到会合点为止，中间一律不截断 */
for (i, step in steps) {
    if (<step 是挂接目标所在格>) { stopped_at_safe = true; break; }  // 不订这一格
    if (!ReserveSingleTrack(...)) break;                            // 订失败即停
    booked++;
}
```

- `r3r_last_safe`（`:798-815`）→ **删**
- 终点例外分支（`:949-981`）→ **删**（它变成「唯一的正常截断」）
- `r3r_single_track_tile`（`:775-779`）→ **删**（不再需要靠单轨来区分）
- `IsSafeWaitingPosition` 在本函数里 → **删**（挂接预留不需要原生安全位语义）

**净效果：代码变短、`steps.size()` 兜底初值消失、终点例外与 M1 的对抗消失。**

### 17.6 必须对玩家说清的事实（否则会误导）

会合点那一格**在预留阶段仍然订不上**：`TryReserveRailTrackdir` 不会因为「这是我的目标」就放行，那是车底的真实预留，占了别人的位。所以：
- 「有等待列车 ⇒ 安全位」只在**判定截断点**上成立；
- 真正**订**的时候最后一格会失败并 `break`，日志表现 `CPL-RES-PARTIAL` / `booked = total-1`。

这是正确行为。⇒ **「当空气」只发生在寻路阶段，不发生在预留阶段；预留必须诚实。**

### 17.7 待拍板

- **C-6**：是否按 §17.5 落地（删 `r3r_last_safe` / `term_exempt` / `r3r_single_track_tile` / `IsSafeWaitingPosition` 调用，改为「只认目的地截断」）？预期改动 40~60 行且**净减少**。
- **C-7**：「本车要去挂的等待列车」识别方式——用 `R3RIsCoupleTarget()` 逐车比对（需在 `R3RReservePathSequence` 可见），还是拿 `rp()` 摸到的 `target_tile` 直接比格号？前者精确、后者便宜。
- **C-5 仍待**：形态①（平行股道 1875/1876 vs 车底 1874）是否另开一轮查 `PfDetectDestination`。

---

## §18 落地：删除终点例外 + 单轨格判据（第 504 轮，已改码，待编译自证）

### 18.1 玩家拍板（原话要点，不得摘要丢失）

> 「C-6：就按这个落地」
> 「C-7：按照你推荐的来」

C-7 的推荐口径 = **用 `rp()`/`reach()` 摸到的最后一格直接判**，不新增「逐车比对挂接目标」的查询（省一次链遍历）。

### 18.2 关键实现事实（改码前核实，决定了「不能跳过最后一格」）

调用点 `yapf_rail.cpp:2142-2152`：

```cpp
if (!r3r_resv_path.empty()) {
    const R3RCoupleStep &r3r_last = r3r_resv_path.back();
    this->SetReservationTarget(pNode, r3r_last.first, r3r_last.second);   // 终点先设成会合点
    reserved = this->R3RReservePathSequence(r3r_resv_path, r3r_booked);
    if (reserved && r3r_booked > 0 && r3r_booked < r3r_resv_path.size()) {
        const R3RCoupleStep &r3r_stop = r3r_resv_path[r3r_booked - 1];
        this->SetReservationTarget(pNode, r3r_stop.first, r3r_stop.second);  // 部分预留 ⇒ 收缩
    }
}
```

⇒ **终点默认就是最后一格**；只有 `booked < size` 才收缩。
⇒ 若在最后一格**之前** break（跳过它），`booked = size-1 < size` ⇒ 触发收缩到 `size-2` ⇒ 车开不到会合点 ⇒ 下一 tick 路径只剩 1 格 ⇒ `booked=0` ⇒ 失败自锁。
⇒ 正确做法：**最后一格照样订，订完再 break**，使 `booked == size`、收缩分支不触发。

### 18.3 落地清单（仅 `src/pathfinder/yapf/yapf_rail.cpp`，未碰任何 `src/*.h`）

| 删除项 | 原位置 | 说明 |
|---|---|---|
| `r3r_single_track_tile` lambda + `r3r_forbid_90deg` | `:757-779` | M1 的单轨尺子，连带唯一消费者一起消失 |
| `r3r_last_safe` 反向扫（含站台跳格、M1 同尺子注释） | `:780-816` | 「最后一个安全位」概念整体删除 |
| `bool term_exempt` | `:816` | 终点例外标志删除 |
| 正扫 `IsSafeWaitingPosition(...)` 判据 + `i == r3r_last_safe` 分支 | `:858-944` | 约 74 行注释与代码，替换为 20 行 |

| 新增/改写 | 内容 |
|---|---|
| `bool stopped_at_goal = false;` | 取代 `stopped_at_safe` + `term_exempt` 两个标志 |
| 正扫尾部判据 | `if (i + 1 == steps.size()) { stopped_at_goal = true; ... break; }` |
| 日志 `CPL-RES-SAFE-STOP` | 保留原名（判读兼容），去掉 `sig=`/`tbits=`（后者在站台格会触发 `GetTrackBits` 的 `IsPlainRailTile` 断言，M1 原写法靠前置 `!IsRailStationTile` 才安全；新代码不再需要读位），追加 `goal=1` |
| 日志 `CPL-RES-TERM-EXEMPT` | **整条删除**（机制不存在） |
| 日志 `CPL-RES-OK` | 字段 `term_exempt=%d` → `goal=%d`；`stop_safe=` 保留 |

### 18.4 推演后的行为

1. **健康路径**：`total=N booked=N stop_safe=1 goal=1`，预留终点保持在会合点（收缩分支不触发）。
2. **中途被第三方占**：`ReserveSingleTrack` 失败 ⇒ 走 KI-563 A 分支，`CPL-RES-PARTIAL booked=k/N`，已订格保留并原地返回 —— 与原生的「订一段走一段」粒度一致，不越权。
3. **会合点被车底自身占着**：同样走 PARTIAL，`booked=N-1`，终点收缩到 `N-2` ⇒ 车停在会合点前一格。这与改前一致（改前也订不上最后一格），**不是回归**。
4. **中段信号不再截断** —— 这是本轮的行为变更主体。原生驾驶侧按「第一个安全位」收尾能自己往前长，挂接预留是一次性摸完再订，中途停下必自锁（REG-399-C 推导），所以这里必须一路订到目的地。
5. **中段道岔/站台不再参与任何判定** —— M1 要防的「道岔被误判成线路尽头」在这个结构下不可能发生，因为中段根本不判。

### 18.5 明确的副作用（须在报告里对玩家说清）

- `CPL-RES-TERM-EXEMPT` 不会再出现；若复测时仍搜到该串，说明跑的是旧 exe。
- `CPL-RES-SAFE-STOP` 的语义从「停在安全位」变成「停在目的地」，且**每一条成功预留都应有一行**（健康判据）。
- `tbits=` / `sig=` 两个字段消失，既有以它们为条件的判据需换成 `goal=1`。

### 18.6 复测判据

1. `CPL-RES-OK` 出现 `booked == total` 且 `goal=1`（不再有「`stop_safe=0 term_exempt=0 booked=total`」形态）。
2. **每条**成功预留都有且仅有一行 `CPL-RES-SAFE-STOP ... goal=1`，且 `nth = total-1`（最后一格）。
3. 日志中不再出现 `CPL-RES-TERM-EXEMPT`。
4. 中段有信号/道岔的形状不再提前 `CPL-RES-PARTIAL`（改前会被截断的那种）。
5. 负判据：中段真有第三方占位时，仍出现 `CPL-RES-PARTIAL` 且 `booked < total`，不把别人的格记成自己的。
6. 负判据：`COUPLE-OK` 正常出现，机车能开到会合点（不自锁）。
7. 负判据：解挂/普通驾驶路径不受影响（本函数只服务挂接摸路，见 18.7）。

### 18.7 影响面确认

`R3RReservePathSequence` 全工程只有 **1 个调用点**（`yapf_rail.cpp:2148`，挂接摸路 `CFollowTrainPath` 收尾）。解挂、普通驾驶走的是 `ExtendTrainReservation`（`train_cmd.cpp`），不受本轮影响。

### 18.8 构建自证（2026-11-24 已回填）

- 入口：复用工作区既有 `R3R_fullbuild.cmd`，参数 `inc detach`（未新建任何 `.cmd` / `_tmp_*`）。
- 护栏：`build\R3R_incbuild.guard.log` = `GUARD: incremental is safe (no header/lang file is newer than the newest object)` ⇒ 本轮只改 `.cpp`，未碰任何 `src/*.h`，增量合规。
- 步进：`build\R3R_incbuild.log` = `[1/5] FindVersion.cmake` → `[2/3] Building CXX object CMakeFiles\openttd_lib.dir\src\pathfinder\yapf\yapf_rail.cpp.obj` → `[3/3] Linking CXX executable openttd.exe`。
- 结果：`build\R3R_incbuild.done` = `EXIT_CODE=0`（mtime 2026-11-24 21:06:08，13 B）；日志内 `error C` / `fatal error` / `FAILED:` / `build stopped` 计数 **0**。
- 产物：`build\openttd.exe` LastWriteTime **2026-11-24 21:06:01**，54 380 544 B；源码 `src\pathfinder\yapf\yapf_rail.cpp` = 2026-11-24 21:01:11 ⇒ exe 晚于源码、obj 由本轮 `[2/3]` 重编。
- `read_lints`：0 条诊断。
- 发布树 `build-release`：**本轮未编**（按口径只编内测探针树），停留在上一版，不含本轮「只认目的地截断」改动。
- 状态：已改码 + 已编译通过；游戏内复测待做（判据见 §18.6）。

## 19. 第 504 轮首份实机日志判读（2026-11-24 21:17，玩家："情况有所改变但问题依然存在"）

日志：`build\R3R_debug.log` 8 338 行 / 864 978 B / mtime 2026-11-24 21:17:33（晚于新 exe 21:06:01，采样有效）。**注意：日志为追加模式，前段含旧 exe 会话残留**（第 2162/2356/2441 行三条 `CPL-RES-OK ... stop_safe=0 term_exempt=0` 是旧格式，无 `goal=` 字段）。

### 19.1 本轮判据 1/2/3 = **通过**（改动确实生效）

新 exe 段（第 5002 行起）逐条核对：

| 行号 | 内容 | 判定 |
|---|---|---|
| 5002 / 7544 | `CPL-RES-SAFE-STOP veh=15 nth=22/23 tile=1424,1874 td=0 booked=23 goal=1` | ✔ 恰一行/次，`nth = total-1` |
| 5205 / 7738 | `CPL-RES-SAFE-STOP ... nth=11/12 ... goal=1` | ✔ |
| 5291 / 7824 | `CPL-RES-SAFE-STOP ... nth=3/4 ... goal=1` | ✔ |
| 5003 / 7545 | `CPL-RES-OK ... total=23 booked=23 stop_safe=1 goal=1` | ✔ `booked==total` |
| 5206 / 7739 | `CPL-RES-OK ... total=12 booked=12 ... goal=1` | ✔ |
| 5292 / 7825 | `CPL-RES-OK ... total=4 booked=4 ... goal=1` | ✔ |

计数：`CPL-RES-PARTIAL` = 0、`CPL-RES-FAIL` = 0、`CPL-RES-TERM-EXEMPT` = 0。
⇒ 病态形态「`stop_safe=0 term_exempt=0 booked=total`」已消失；`CPL-RES-SAFE-STOP` 恒落在最后一格。

### 19.2 挂接本身成功

`COUPLE-OK` 6 行 = 两组场景各重跑 3 次（同坐标同车号逐字相同，属玩家重试）：

- `573 / 3423 / 6213`：`loco=75 rear=62 consist=18 co=1 real=1 type=1 tx=1505 ty=1875`
- `2782 / 5635 / 8160`：`loco=17 rear=18 consist=18 co=1 real=7 type=18 tx=1438 ty=1874`

末次合并链：`GEO-END settle-couple head=17 n=57 capped=0`、`SEGTR-SNAP tag=couple head=17 nseg=2 ctrl=62 borrowed=1`（段 1 front=17、段 2 front=62）。

### 19.3 疑点登记（未定性，待玩家指明现象）

- **REG-504-19-A**：挂接成功后链头 17 的当前订单仍是 `curType=16`（GOTO_COUPLE）指向 `1437,1874` —— 即车底所在格。`ORD-IDX-WATCH site=tick-out veh=17 ... curType=16`。是否导致"挂上了又去做挂接命令"待查。
- **REG-504-19-B**：`RESPACE-AFTER-EDIT couple head=17 px=0 stretch=1 unfixed=0 skip=0 front=1 passes=2 visited=56` —— `stretch=1` 表示拼接后被拉伸 1 处（KI-98/KI-214 包围盒族），可能是玩家看到的视觉错位。
- **REG-504-19-C**：`CPL idx=17 ... len=2 ... nom=4 nom0=4 nom1=4`、`idx=16 len=6 nom=4` —— 物理车长与名义间距不一致（KI-253 铰接塌缩族），`gap=0` 故未触发折叠判据。
- **REG-504-19-D**：`COUPLE-FAIL` 88 行全部 `loco=15 order=16 tx=1474 ty=1884`、`COUPLE-WAIT-OFFDEST` 91 行；尾部 `RPFAIL veh=17 tick=42348 tile=1451,1875 td=4 unsafe=0 nTry=2 nOK=1`、`mstuck=1`。预留失败点 1451,1875 与 KI-346 的换槽格形态同族，未取数。
- **REG-504-19-E**：`CPL-RES-OK` 的 tiles 为 `1459,1875/0 → 1458,1875/0 → 1457,1875/4 → 1457,1874/5 → 1456..1424,1874`（即机车在 1875 线、斜插入 1874 线再西行），路径形态与历史现场「平行股道 1875/1876」一致；`total=23` 而 x 跨度 35 格 ⇒ 存在跳格（站台跳段或截断），日志自身在第 16 格后被 `...` 截断，无法逐格核对。

### 19.4 玩家已确认现象 = 「挂接预留截止在不安全道岔」

**玩家原话（2026-11-24）**：「我注意到，挂接寻路的预留在不安全的道岔截止的问题依然存在」

⇒ 本轮 §18 的 C-7 改法（「摸到的最后一格就是目的地」）**只统一了口径，没有消除现象**。登记顺序：

- 已排除：判据打架（判据 1/2/3 全通过，见 §19.1）。
- 定性（猜测根因，待验证 ★先落盘★）：C-7 把 `stopped_at_goal` 写死为「`i + 1 == steps.size()` ⇒ 真」，等价于**宣布"YAPF 摸到的终点必然是安全等待位"**。一旦 YAPF 的 couple 到达判据本身把终点落在**双槽道岔格**上（`tracks=0x11 / 0x30` 那一类），预留就会理直气壮地铺满到该道岔格，`goal=1` 恒真、永不 `PARTIAL` ⇒ 玩家看到「在不安全的道岔截止」。
- 与 C-5 形态①的关系：C-5 说的是**路径走错平行股道**（1875/1876 vs 1874）；本条说的是**路径终点选错性质**（道岔当等待位）。两者可同时成立，但本条独立于股道选择。
- 待验证判据：①`CPL-RES-OK` 的最后一格 `tiles=` 的 trackdir 是否为双槽格（该格 tracks 含两个方向）；②改造前的老判据 `IsSafeWaitingPosition(steps.back(), ...)` 对同一格返回什么（若为 false 即坐实"我们把不安全位当目的地"）；③调用点为何不给 `steps.back()` 做目的地纠偏。

**取证纪律**：本轮只读代码 + 只加只读探针，不先改判定；先把"最后一格到底安不安全、由谁决定"钉死。

## 20. 第 505 轮取证（2026-11-24，现象=预留截止在不安全道岔）

### 20.1 六条 SAFE-STOP 全为同一终点，且终点是**车底头**不是会合端

```
5002: CPL-RES-SAFE-STOP veh=15 nth=22/23 tile=1424,1874 td=0 booked=23 goal=1
5205: CPL-RES-SAFE-STOP veh=15 nth=11/12 tile=1424,1874 td=0 booked=12 goal=1
5291: CPL-RES-SAFE-STOP veh=15 nth=3/4  tile=1424,1874 td=0 booked=4  goal=1
7544/7738/7824: 三段与上同（第二次重跑）
```

`tile=1424,1874` = `CPL-PAIR` 的 `tgtTile`（车底头）。**而同一 tick 的阻塞点用的是另一端**：

```
1930: CPL-PAIR tick=43821 act=15 tgt=18 dist=60 actTile=1474,1884 tgtTile=1424,1874
7663: CPL-BLOCKED-WAIT veh=15 tile=1451,1874 dest=1437,1874
```

`1437,1874` = 车底尾（车底占 1424..1437,1874）。机车自东侧（1451 方向）驶来，**先遇到的会合端是 1437/1438**，预留终点却落在 1424（穿过整个车底）。

⇒ 猜测根因（待验证）：`rp_impl` 的 `if (cur == tg) return true;` 让 tg 不进清单；唯一把 tg 压入清单的是 KI-569 站台跳段分支（`if (cur != tg) r3r_resv_path.push_back({tg, cur_td})`）。一旦整段长站台被 `TrackFollower` 跳格且 `R3RSkippedStationStretchContains` 命中，`steps.back()` 就从"会合端"漂到"车底另一端"，C-7 的 `goal=1` 于是理直气壮地把预留铺**穿**车底。**终点语义随跳段漂移**，这是 C-7 没覆盖的洞。

### 20.2 现场确有双槽格，且只订了一条轨

```
7670: RESV-EXT-PRE veh=15 tile=1449,1874 enterdir=0 tracks=0x9 reserved=0x1 doRes=0 real=1 spd=160
7671: RESV-NOLOOK-PASS veh=15 tile=1449,1874 enterdir=0 res=0x1 tracks=0x9 spd=160
7783: RESV-EXT-PRE veh=15 tile=1441,1874 enterdir=0 tracks=0x9 reserved=0x1 doRes=0 real=1 spd=160
7784: RESV-NOLOOK-PASS veh=15 tile=1441,1874 enterdir=0 res=0x1 tracks=0x9 spd=160
```

`tracks=0x9` = `TRACK_BIT_X(0x1) | TRACK_BIT_LOWER(0x8)` ⇒ **双槽格（岔口）**，`reserved=0x1` 只订 X 位，LOWER 留给别的车。`1438..1448,1874` 是纯直轨 `tracks=0x1`。⇒ 1449,1874 与 1441,1874 是这一带的两个岔口。

### 20.3 另有一条 SAFE 判失败发生在纯直轨上（疑点）

```
7662: CPL-SAFE-FAIL veh=15 tile=1442,1874
```

1442,1874 的 `tracks=0x1`（纯直轨，见 7777 行 `res=0x1 tracks=0x1`），却报"安全位失败"。⇒ **REG-505-20-A（疑点）**：期望值=纯直轨应为安全等待位；实测值=safe fail；为何失败待查（该判据的调用方/入参未取）。

### 20.4 斜插对（与 C-5 形态①同源地）

`CPL-RES-OK tiles=1459,1875/0;1458,1875/0;1457,1875/4;1457,1874/5;1456,1874…`
`1457,1875` 走 `TRACK_LEFT(4)`、`1457,1874` 走 `TRACK_RIGHT(5)` ⇒ 1875 线与 1874 线之间的斜向连接对。机车从 1875 线斜插进 1874 线再西行，这是路径本身的选择（C-5 形态①），与预留层无关。

### 20.5 卡点：需要玩家一个坐标

「截止在不安全的道岔」有两种读法，**修法完全不同**，不能猜：

- **读法 A（越界）**：预留本该止于会合端 1437/1438，却一路铺到 1424（穿过车底）—— §20.1 的推测。
- **读法 B（缺格）**：预留本该继续往前，却在某个岔口格（1449,1874 或 1441,1874）就断了，机车因此停住。

请给一句：**画面上那条黑色预留线的终点/断点落在哪一格（或哪两个岔口之间）**。拿到这一句就能直接定位，不必再猜。

## 21. 玩家给出坐标：预留铺到了 1460,1875（不安全位）

**玩家原话（2026-11-24）**：「注意 veh15/17（同一个段），它有一次挂接寻路铺设预留到了 (1460,1875)，这个位置不安全」

### 21.1 这条证据的定性价值

- 明确了**读法 B（缺格/错终点）**：不是"穿过车底铺到 1424"，而是"**某一次**预留的端点落在 `1460,1875`"。
- 关键限定词是**「有一次」** ⇒ 同一段（veh=15 与 veh=17 是同一段的两个车号，见 §19.3 借用层）在不同 tick 摸路，多数 tick 终点是 `1424,1874`（§20.1 的 6 条 SAFE-STOP），但**至少有一次**终点/覆盖范围到了 `1460,1875`。
- `1460,1875` 位于已知路径**起点以东**：§20.4 的 `CPL-RES-OK tiles=` 首格是 `1459,1875/0`。`1460,1875` 比首格更靠东 1 格 ⇒ 该次摸路的 `steps` 里包含了 `1460,1875`，且它出现在**清单靠前的位置**（起点侧），不是尾端。
- 因此本条的准确表述应为：**预留清单把机车"屁股后面/身后一格"或"起摸点东侧"的 `1460,1875` 也订了进去**。若 `1460,1875` 是道岔格（双槽/岔口），则它被订 = 堵住另一股道 = 玩家说的"不安全"。这与他上一句「在不安全的道岔截止」是同一件事的两种描述（一个说"截止在那里"，一个说"铺到了那里"）。

### 21.2 待验证判据（本轮取数目标）

1. **D-1**：`1460,1875` 在日志里出现在哪些行（`CPL-RES-OK` 的 tiles 串 / `CPL-RES-SAFE-STOP` 的 tile= / `RESV-EXT-PRE` / `RESV-NOLOOK-PASS` / `RP cur=`）—— 先确定它是"被订的格"还是"预留终点"。
2. **D-2**：`1460,1875` 的 `tracks` 位是多少（`0x1` 纯直轨 / `0x9`=X|LOWER / `0x11`=X|LEFT / `0x30`=LEFT|RIGHT 等）。判"不安全"必须靠这个数，不能靠印象。
3. **D-3**：该次摸路的 `st`（起点格）与 `best_start` 是哪一格。若 `st` 本身就是 `1460,1875` 而 `rp_impl` 的 `if (cur != st || depth != 0)` 守卫失效（例如 `depth` 不是 0、或 `best_start != st`），就会把起点格自己也订进去 —— 这是**猜测根因（待验证）**：`rp(best_start, best_td, 0)` 的 `depth` 恒为 0，但 `best_start` 可能**不等于** `st`（第 332 轮注释已写明"从 best_td 真正所属的那一格开始走"），此时 `cur != st` 成立 ⇒ 起点格被 push 进清单 ⇒ 预留包含 `1460,1875`。
4. **D-4**：`steps` 的首格与 `st` 是否一致；`CPL-RES-OK` 的 tiles 串首格（本次是 `1459,1875/0`）与 `1460,1875` 的关系。

**猜测根因（待验证，★已落盘★）**：`rp_impl` 里"起点格不订"的守卫写成 `if (cur != st || depth != 0)`，它假设"递归的 depth==0 那一格一定是 `st`"。但调用点是 `rp(best_start, best_td, 0)`，而 `best_start` 由 `best_td` 反查得出，**可能不在 `st` 上**（第 332 轮为此专门改过调用起点）。一旦 `best_start != st`，守卫双双失效（`cur != st` 为真 ⇒ 直接 push），于是**起点侧多订一格**，表现为预留覆盖到 `1460,1875`。修法（若成立）= 守卫改为"首调格不订"（用一个 `bool first` 或记录 `r3r_resv_path.empty()` 判定），而不是拿 `st` 比较；或显式要求 `best_start == st` 并在不等时把 `best_start` 当成不订的根。

## 22. 第 505 轮取数：`1460,1875` 的预留**不是挂接摸路铺的**（§21 猜测根因已证伪）

取数命令：`Select-String build\R3R_debug.log -Pattern '1460,1875'`（结果 41 行，三组 tick 呈周期重复，见 `build\_v5.txt`），以及 `CPL-RES-OK|CPL-RES-SAFE-STOP` 全量（`build\_v6.txt`）。

### 22.1 硬事实一：`1460,1875` 是**机车自己当前所在的格**

```
2092: CPL-ENTRY     veh=15 tile=1460,1875 orderType=16 dontReserve=0
2093: CPL-ORIGIN    veh=15 origin=1460,1875 td=0 vehDir=1 vehTd=0
2094: CPL-ORIGIN-TD veh=15 tile=1460,1875 tdb=0x101 tbits=0x1
2089: LOCO-MOVE     veh=15 tick=44047 tile=1460,1875 j=263 spd=160 track=01 dir=1 db=0 ahead=-9
2111: CPL-TRACE     st=1460,1875 tdb=0x101
```

`tracks/tbits = 0x1` ⇒ **纯 X 直轨，不是道岔**。`tdb=0x101` = bit0|bit8 = X 轨双方向。

### 22.2 硬事实二：摸路预留清单**不含** `1460,1875`

`CPL-RES-OK veh=15 total=23 ... tiles=1459,1875/0;1458,1875/0;1457,1875/4;1457,1874/5;1456,1874/0;…`（首格 `1459,1875`）。
六条 `CPL-RES-OK` / `CPL-RES-SAFE-STOP` 全量（`_v6.txt`）里，`tiles=` 串与 `tile=` 字段**没有任何一处出现 `1460,1875`**。
⇒ **§21 的"起点侧多订一格"猜测 = 证伪**。`best_start=1459,1875` 与 `st=1460,1875` 虽不相等，但 `rp()` 从 `best_start` 起走、`steps` 首格就是 `best_start`，没有多订一格。

### 22.3 硬事实三：`1460,1875` 上的预留来自**脚下自保**，且归属本车

```
2084: RESV-EXT-PRE    veh=15 tile=1460,1875 enterdir=0 tracks=0x1 reserved=0x1 doRes=0 real=1 spd=160
2085: RESV-NOLOOK-PASS veh=15 tile=1460,1875 enterdir=0 res=0x1 tracks=0x1 spd=160 real=1
2086: RESV-SELF-AIR   veh=15 tile=1460,1875 track=0 kind=rail
```

`RESV-SELF-AIR ... track=0 kind=rail` = 该格的预留位**判定为自己占**（`ReserveTrackUnderConsist()` 的脚下保护），`doRes=0` 表示已订不再订。
⇒ 玩家看到的 `1460,1875` 有预留，来源是**整车脚下自保**，与 `R3RReservePathSequence` / 摸路清单**无关**。

### 22.4 硬事实四：真正的岔口在**西邻一格 `1459,1875`**，且它是摸路清单首格

```
2090: CPL-NEXT veh=15 tick=44049 mf=1460,1875 td=0 tdOk=1 ... to=1459,1875 toType=1 rail=1 stn=0 stnRes=0 plain=1 bits=0015 resv=0000 wholeOwner=-1
2128: CPL-FIRST-TD st=1460,1875 hops=5 yapf=0 at=1459,1875 tdb=0x1515 ok=1 best=0 start=1459,1875 trk=0
```

`to=1459,1875` 的 `bits=0015` = **0x15 = TRACK_BIT_X(0x1) | UPPER(0x4) | LEFT(0x10)** ⇒ **三槽格（岔口）**。
`resv=0000` ⇒ 摸路时该格无人订，所以第一格 `1459,1875/0` 被订上了（只订 X 位，UPPER/LEFT 未订）。

⇒ **"不安全"的位置实体是 `1459,1875`（三槽岔口），不是 `1460,1875`（纯直轨）**。机车停在 `1460,1875`、车头朝西压着 `1459,1875` 这个岔口，于是预留线在视觉上"贴着/起自道岔"。

### 22.5 新增登记

- **REG-505-22-A（已定性）**：`1460,1875` 的预留 = `ReserveTrackUnderConsist()` 脚下自保（`RESV-SELF-AIR`），非挂接摸路。**若玩家判定该位不安全，问题在"机车停在此格"而非"预留铺到此格"** —— 机车长 2 格（`CPL idx` 见 `len=2`），停 `1460,1875` 时车头已压在 `1459,1875` 岔口上。
- **REG-505-22-B（已定性）**：`1459,1875` 是 `X|UPPER|LEFT` 三槽岔口，摸路把它当必经首格订了 X 位。**这是"预留贴着道岔"的几何来源**，且无法靠"少订一格"消除（车必须开过去），只能靠"目的地/停车点别落在岔口咽喉"。
- **REG-505-22-C（§21 猜测根因 = 证伪）**：`steps` 不含 `1460,1875`，起点守卫没有多订格。原猜测作废，保留记录以免重犯。
- **REG-505-22-D（疑点，未定性）**：`LOCO-MOVE ... ahead=-9` —— 前方可用距离为**负 9**（`look=0`、`endTile=-1,-1`）。机车 `spd=160` 仍在推进（`xp` 23375→23373→23365），说明它正在"顶着"某格前进。与"停在岔口咽喉"互为旁证，但 `ahead` 为负的成因未取数。

### 22.6 下一步只剩一个二选一（决定修法方向）

- **若玩家所指 = `1460,1875` 这格有黑色预留** ⇒ 修 `ReserveTrackUnderConsist()`：机车**停稳在岔口咽喉/站台格**时不该长期占脚下位，或"停稳 + 挂接订单"时交给挂接预留统一管理。
- **若玩家所指 = 预留线贴着 `1459,1875` 岔口** ⇒ 修**停车点/目的地**：挂接摸路的终点不该让机车停在岔口上（当前 6 条 SAFE-STOP 全部指向 `1424,1874` = 穿过整个车底的另一端，见 §20.1，这条本来就要修）。

## 23. 玩家严厉纠正（2026-11-24）：`1460,1875` 是**预留终点**，不是机车所在格

**玩家原话**：「我要严厉纠正你根据已知问题胡乱推断的毛病："铺到 1460,1875截止"的时候，机车veh15正踩着(1474,1884)」

### 23.1 我错在哪（方法论错误，必须登记）

- 我把 §22 里 `CPL-ENTRY veh=15 tile=1460,1875` / `CPL-ORIGIN veh=15 origin=1460,1875` / `LOCO-MOVE veh=15 tile=1460,1875`（tick=44047 / 41545 / 42049 三组）当成"**同一时刻**机车在 1460,1875"，并据此宣布"1460,1875 = 机车脚下自保位，不是挂接摸路铺的"。
- 实情：那些行是**另外几次**挂接尝试的采样（机车在那些 tick 确实移动到 1460,1875 附近），而玩家观察到"预留止于 `1460,1875`"的那一次，机车在 **`1474,1884`**。**两个不同 tick 的读数被我用同一句话缝在一起 ⇒ 结论整条作废。**
- ⇒ 登记 **REG-505-23-X（取证纪律，已犯）**：凡"某格 X 的角色"结论，必须限定在**同一 tick 同一组行**内，禁止把 tick=A 的 tile 读数与 tick=B 的预留终点拼成一句话。玩家原话里"正踩着(1474,1884)"是**同一瞬间**的强约束，必须优先于任何跨 tick 归纳。
- §22.1~22.4 的"脚下自保 / 不是摸路铺的"**结论撤回**（保留原文仅作错误记录）。§22.5 的 REG-505-22-A/B/C/D 中 A/B 依赖该错误前提，一并降级为"待重判"。

### 23.2 纠正后的问题陈述（这才是要修的东西）

现场（与 §20.1 同一 tick 组互证）：

```
1930: CPL-PAIR tick=43821 act=15 tgt=18 dist=60 actTile=1474,1884 tgtTile=1424,1874
```

- 机车 veh=15 在 **`1474,1884`**（东南侧），目标车底头 `1424,1874`，会合端（车底尾）`1437,1874`。
- 玩家观察：**预留从机车前方一路往西铺，铺到 `1460,1875` 就截止**。
- `1460,1875` 距机车 14 格，距会合端 `1437,1874` 还有 23 格，距车底头 `1424,1874` 还有 36 格。
- ⇒ **预留既没到会合端、更没到车底头，而是断在 1875 线中途的 `1460,1875`。** 这既不是 §20.1 的"读法 A（越界到 1424）"，也不是"读法 B（岔口缺格）"，而是**第三种：预留中途断裂**（暂记"现象 C / 断流"）。

### 23.3 现象 C 的直接待验证点（按优先级）

1. `1460,1875` 之后那一格是谁？`1460,1875 → 1459,1875`（`bits=0x15` = X|UPPER|LEFT 三槽格，见 §22.4）—— **断点紧邻一个三槽岔口**。若 `1459,1875` 的 X 位被占，扩展就在 1460,1875 止住。⇒ 必须取该 tick 的 `RESV-EXT-PRE tile=1459,1875 ... reserved=?` 与 `RESV-EXT-RES ... ok=`。
2. `RESV-EXT-PRE veh=15 tile=1460,1875 enterdir=0 tracks=0x1 reserved=0x1 doRes=0` 的 `reserved=0x1` 归属是谁（`doRes=0` 说明已订；`RESV-SELF-AIR` 说"自己"，但那条出现在别的 tick）⇒ 需同一 tick 的 `RESV-GHOST`/owner 行。
3. `CPL-RES-OK` 的三份清单全部被日志 `...(truncated)` 截断（§20 已见），**末格不可见** ⇒ 必须从 `RP cur=` 逐行序列里数到断点，而不能拿 `total=` 反推。

**取证纪律（本轮）**：只做**同一 tick 内**的对齐读；任何跨 tick 拼接必须先写出 tick 号并明确声明"这是两次采样"。

## 24. 断点成因已取到（2026-11-24）：`1459,1875` 是 **`doRes=0` 主动不订**，不是"被占用"

取数命令：`Select-String ... -Pattern 'RESV-EXT-PRE|RESV-EXT-RES|RESV-NOLOOK|RESV-SELF-AIR' | Where Line -match '1459,1875|1460,1875'` → `build\_v10.txt`（81 行，三个重复周期的同一组四行）。

### 24.1 原文读数（同一组行，行号已核）

周期 1（日志 2084-2091）：

```
2084: RESV-EXT-PRE   veh=15 tile=1460,1875 enterdir=0 tracks=0x1  reserved=0x1 doRes=0 real=1 spd=160
2085: RESV-NOLOOK-PASS veh=15 tile=1460,1875 enterdir=0 res=0x1 tracks=0x1 spd=160 real=1
2086: RESV-SELF-AIR  veh=15 tile=1460,1875 track=0 kind=rail
2091: RESV-EXT-PRE   veh=15 tile=1459,1875 enterdir=0 tracks=0x11 reserved=0x0 doRes=0 real=1 spd=160
```

周期 2（4924-4931）、周期 3（7473-7479）**逐字相同**（只是行号不同）。

### 24.2 两条硬结论

- **结论 1（`1460,1875` = 已订，且被判定为"自己的"）**：`tracks=0x1`（纯 X 直轨）/ `reserved=0x1` / `doRes=0` ⇒ 走到这格时 X 位**已经有预留**，所以不需要再订；紧接 `RESV-NOLOOK-PASS`（不需要看就通过）+ `RESV-SELF-AIR`（判为自身/已授权，`kind=rail`）⇒ 扩展**没有停在这里**，它是穿过去的。
- **结论 2（断点真因 = `1459,1875` 的 `doRes=0`）**：`tracks=0x11`（**X|LEFT 两槽，不是 §22.4 写的 `0x15` 三槽 —— §22.4 该处读数作废**）/ `reserved=0x0`（**该格当时是空的**）/ **`doRes=0`（算出来"不需要订"）**。
  ⇒ **现象 C 的直接成因不是"X 位被占"，而是扩展在这一格主动放弃预订**。`reserved=0x0` 排除了"被别人占"的可能，`doRes` 是决策变量，不是结果变量。
- ⇒ §23.3 待验证点 1 的答案：**不是占位，是弃订**（`REG-505-24-A`）。

### 24.3 待验证点（下一步唯一要做的取数/读码）

1. **`doRes` 在代码里的判定条件**：找到 `RESV-EXT-PRE` 探针所在函数，读 `doRes` 的计算式，确认 `1459,1875` 命中哪一条"不需要订"的分支（候选：`reserved` 已含目标位 / `HasReservedTracks` 判给自己 / `reserved != 0` 的"有别人订过" 保守跳过 / 方向位不在 `tracks` 里）。**这是唯一还没闭合的环节。**
2. `spd=160` ⇒ 这三次采样是**机车高速通过**时的扩展，与玩家观察的那次（`CPL-PAIR tick=43821 actTile=1474,1884`、`CPLRES ... spd=0`）**不是同一 tick**。⇒ 断点 `1460,1875` 到底对应"高速通过"还是"停着摸路"仍未对齐，需在 `1474,1884` 那组 tick 里找同样含 `doRes=0` 的 `RESV-EXT-PRE`（`REG-505-24-B`）。
3. `CPLRES ... destTile=1474,1884`（= 机车自身格，`root=0`、`nTry=19 nOK=19`）与 `CPL-PAIR ... tgtTile=1424,1874` 的矛盾仍未解释（`REG-505-24-C`）。

## 25. **现象 C 的第一版结论被推翻**：预留没有断，同 tick 组内 `booked=23` 全成

取数：直接顺序读 `build\R3R_debug.log:1925–2173`（**同一段连续日志**，tick 43821 → 44081，中途无跳转）。

### 25.1 机车实际是"自己沿着这条曲线开过去"的（逐格原文）

```
1930: CPL-PAIR tick=43821 act=15 tgt=18 dist=60 actTile=1474,1884 tgtTile=1424,1874
1933: LOCO-MOVE veh=15 tick=43827 tile=1474,1884 j=221 spd=15 ... ahead=-9
1939: RESV-EXT-PRE veh=15 tile=1473,1884 ... reserved=0x1  doRes=0 spd=3
1945: RESV-EXT-PRE veh=15 tile=1472,1884 ... reserved=0x10 doRes=0 spd=131
1955: RESV-EXT-PRE veh=15 tile=1472,1883 ... reserved=0x20 doRes=0 spd=156
1961: RESV-EXT-PRE veh=15 tile=1471,1883 ... reserved=0x10 doRes=0 spd=160
...（1471,1882 / 1470,1882 / 1470,1881 / 1469,1881 / 1469,1880 / 1468,1880 / 1467,1880 / 1466,1880 / 1465,1880 / 1465,1879 / 1464,1879 / 1464,1878 / 1463,1878 / 1463,1877 / 1462,1877 / 1462,1876 / 1461,1876 / 1461,1875，全部 reserved≠0）...
2084: RESV-EXT-PRE veh=15 tile=1460,1875 enterdir=0 tracks=0x1 reserved=0x1 doRes=0 spd=160
2090: CPL-NEXT veh=15 tick=44049 mf=1460,1875 td=0 tdOk=1 ... to=1459,1875 ... plain=1 bits=0015 resv=0000 wholeOwner=-1
2091: RESV-EXT-PRE veh=15 tile=1459,1875 enterdir=0 tracks=0x11 reserved=0x0 doRes=0 spd=160
```

⇒ 从 `1474,1884` 到 `1460,1875` 的**每一格都已被预留**（`reserved` 非 0），机车是在这条自己已被预留的线上开过去的。**这条 18 格黑线不是 couple 寻路"铺到 1460,1875 就停"，而是它一路把车带到 1460,1875。**（`REG-505-25-A`：撤回 §23.2「预留中途断裂」的字面结论。）

### 25.2 到 1460,1875 之后立刻重算 couple 路径，且**23 格全部订成功**

```
2102: CPL-PATHFOUND veh=15 found=1
2103: CPL-SCAN tick=44059 veh=15 found=1 ... last=1424,1874 lastveh=18 why=acc-car
2105: CPL-BEST veh=15 tile=1424,1874 td=0 cost=18342          ← 目的地正确（车底头）
2106-2110: CPL-BACK 1424,1874 → 1442,1874 → 1450,1874 → 1451,1874 → 1456,1874 → parent=1460,1875
2111: CPL-TRACE st=1460,1875 tdb=0x101
2112-2127: REACH 1459,1875 → 1458,1875 → 1457,1875 → 1457,1874 → 1456,1874 → … → 1445,1874 (depth 0..15)
2128: CPL-FIRST-TD st=1460,1875 hops=5 yapf=0 at=1459,1875 tdb=0x1515 ok=1 best=0 start=1459,1875 trk=0
2130-2145: RP cur= 与 REACH 逐格一致（1459,1875 … 1445,1874）
2146: RESV-WATCH SET-RAIL tile=1459,1875 track=0 phase=yapf-couple actor=15 head=15
2162: CPL-RES-OK veh=15 total=23 booked=23 stop_safe=0 term_exempt=0
      tiles=1459,1875/0;1458,1875/0;1457,1875/4;1457,1874/5;1456,1874/0;…;1445,1874/0;…(truncated)
2163: CPL-RESERVE veh=15 reserved=1 next=0 path=23 booked=23
```

- **`total=23 booked=23`、`CPL-RESERVE reserved=1`** ⇒ 这一段预留**一条都没被拒**，从 `1459,1875` 连续向西铺 23 格（`1459→1458→1457→1456…→1445…`，23 格正好到车底尾 `1437,1874` 一带）。
- ⇒ **`1459,1875` 的 `doRes=0` 与断流无关**（§24.2 结论 2 作废）：`doRes=0` 只是"这一次 `ChooseTrainTrack` 不负责订路"（`_settings_game.pf.reserve_paths==false`，见 `train_cmd.cpp:18291`），真正订路的是 couple 寻路的 `TryReserveRailTrack`（`phase=yapf-couple`）。
- ⇒ **`1459,1875` 的 `reserved=0x0` 也被随即推翻**：同一 tick 组内 `4 格之后`（tick=44060）它就被 `SET-RAIL track=0` 订上了，`wholeOwner=-1` 说明订之前确实无主。

### 25.3 因此真正待解释的问题换成两条（`REG-505-25-B/C`）

- **`REG-505-25-B`（主问题）**：玩家在 **`actTile=1474,1884`（tick=43821）** 那一刻看到的、从 `1474,1884` 连续延伸到 `1460,1875` 的那 18 格黑线，**是谁订的**？该 tick 组内 `EXT-PRE` 全部 `doRes=0`（本函数不订）、且 couple 寻路的 `SET-RAIL/RES-OK` 段还没开始（首次出现在 tick=44059）⇒ 只能是**更早一次挂接预留留下的**（`tick<43821` 的某次 `CPL-RESERVE`），或者是别的机制（`ReserveTrackUnderConsist` / 孤儿）。这与"预留看起来被拉成一条长线到 1460,1875"直接对应。
- **`REG-505-25-C`（次问题）**：`CPL-BACK` 的 parent 链里 `1456,1874 → 1460,1875` 是一步跨 4 格（x 1456→1460 且 y 1874→1875），而 `CPL-FIRST-TD hops=5`；`REACH/RP` 实际展开的是 `1459,1875→1458,1875→1457,1875→1457,1874→1456,1874`（5 跳到 `1456,1874`，与 `hops=5` 吻合）。**这段"先北一格再回南"的绕行**穿过两个 `bits=0x30`（LEFT|RIGHT）双槽格 `1457,1875` / `1457,1874` —— 就是玩家说的"预留贴着道岔/拐进岔区"的几何来源，且它多占了 `1458,1875`、`1457,1875` 两个本可直接直线的格。

### 25.4 下一步（不再猜，只取这两条）

1. 取 `tick < 43821` 的最后一次 `CPL-RESERVE veh=15` / `CPL-RES-OK veh=15`（含 `tiles=`）与 `ReserveTrackUnderConsist` 相关行，确认那 18 格的 owner（`REG-505-25-B`）。
2. 读 `yapf_rail.cpp` 的 couple tracerestrict：为何从 `1460,1875`（`tbits=0x1`，只有 X）出发的路径必须绕 `1457,1875/1457,1874`（`0x30` 双槽）才回到 1874 线，而不是直接走 `1459,1875` 的 X 位到 `1458,1874`（`REG-505-25-C`）。

## 26. **闭环**：那条到 `1460,1875` 的黑线不是本次摸路铺的（`REG-505-26-A`）

三份全量互证（`build\_v9.txt` 62 行 = `CPLRES|RESV-TERM|RESV-WATCH` 全量；`SET-RAIL tile=14[6-7][0-9],187` 命中 **0** 条）。

### 26.1 `SET-RAIL/SET-STN` 全量（本会话唯一记录预留新增的地方）

```
502: SET-RAIL tile=1499,1887 track=0 phase=choose-track actor=15
504: SET-RAIL tile=1484,1884 track=0 phase=yapf-resv    actor=15
505: SET-STN  tile=1474,1884 track=255 phase=yapf-resv  actor=15
2146: SET-RAIL tile=1459,1875 track=0 phase=yapf-couple actor=15
```

⇒ **`1473,1884 / 1472,1884 / 1472,1883 / 1471,1883 … 1461,1875 / 1460,1875` 这 18 格，本会话一次 `SET-RAIL`/`SET-STN` 都没有。**（`pbs.cpp:569-572` 注释自陈：所有分支都走 setter，setter 自己报；只有 **ReadSavegame / tile conversion / track removal** 这类旁路不报。）
⇒ 这 18 格的预留位**不是本会话订的**，只能是 **读档带入**（存档 `_m[t].m2` bit8-11 携带）或轨道改造旁路。

### 26.2 `CPLRES veh=15` 全量只有两条，且目的地都不是 `1460,1875`

```
506:  CPLRES veh=15 tick=40727 origin=1498,1887 destTile=1474,1884 nTry=19 nOK=19 vTile=1500,1887 vDest=1471,1880
3730: CPLRES veh=15 tick=38457 origin=1498,1887 destTile=1474,1884 nTry=19 nOK=19 vTile=1500,1887 vDest=1476,1882
```

⇒ **没有任何一次摸路把 `destTile` 定在 `1460,1875`。** 摸路对 veh=15 只做过两件事：`1498,1887 → 1474,1884`（19 格，两个周期逐字相同）和（tick=44059）`1460,1875 → 1424,1874`（23 格）。**§21/§23 里"摸路铺到 1460,1875"的说法到此彻底作废。**

### 26.3 因此玩家看到的黑线 = **旧预留的尾段 + 本次摸路还没开始之间的那一段**

时间线（同一组连续日志，全部 tick 已标）：
1. `tick=43821`：机车在 `1474,1884`（`CPL-PAIR actTile=1474,1884`），`look=0`、`ahead=-9`、`endPos=-1`（**无前瞻**）。
2. `tick=43827..44047`：机车沿 `1474,1884→…→1460,1875` 以 `spd=3→131→156→160` 前进，**逐格 `EXT-PRE reserved≠0` + `NOLOOK-PASS` + `SELF-AIR`**（`pbs.cpp:591-599`：`R3RReservationHeldBySelf` 判为"自己的预留"，不写、直接放行）。
3. `tick=44059`：才第一次 `CPL-PATHFOUND/CPL-BEST 1424,1874/CPL-RES-OK total=23 booked=23`，`SET-RAIL 1459,1875`。

⇒ 玩家在 `1474,1884` 那一刻看到的黑线，**东段（1474,1884…1460,1875）是存档带入的旧位，西段（1459,1875…）此刻还是空白**。所以黑线"止于 1460,1875"是**旧位的末端**，不是摸路的终点。

### 26.4 "不安全"的机制（这才是要修的东西）

`Train::ReserveTrackUnderConsist()`（`train_cmd.cpp:21104-21160`）只订**每节车当前所在格**，`doRes=0` 的 `ExtendTrainReservation` 不订新路，所以：

- 机车**踩着一条来源为旧存档的预留**以 `spd=160` 前进，而 `lookahead == nullptr`（`look=0 / ahead=-9 / endPos=-1`）⇒ **没有任何制动前瞻**；
- 一直冲到 `1460,1875`（旧位末端）才开始重算 couple 路径、才填 `booked=23`；
- 若旧线中途经过 `bits=0x30` 双槽换线格或三槽岔口（本段在 `1457,1875|1457,1874` 换线），**这段高速行驶在库里/岔区时就是"没有前瞻的盲冲"** —— 与玩家"这个位置不安全"的直觉一致。

### 26.5 待验证（下一步只这两件，不再扩）

1. **`REG-505-26-A` 的直接自证**：读档后第一 tick 的 `RESV-AUDIT` / `RESV-AUDIT-STRAY` 是否把这 18 格列为 stray（或 `RESV-REAP` 清掉过）。若审计判为"有主（= 旧车号已被 veh15 复用）"，则旧位会**永久存活**并被当成 SELF 放行 —— 这就是要修的根。
2. `REG-505-24-C`：`CPLRES veh=15 destTile=1474,1884`（一个 `SET-STN` 站台格、且是机车自己所在/即将到达的格）与 `CPL-BEST 1424,1874` 的关系 —— 前者是"安全停靠位"候选还是真终点，需读 `CPLRES` 探针处的 `destTile` 变量来源（`yapf_rail.cpp` 的 `FindSafePositionOnSafePositionNodes` 返回值）。

## 27. **玩家两处决定性纠正：§26 全节作废（2026-11-24）**

### 27.1 玩家原话（逐字，不可改写）

1. 「当我观察那一刻（机车主动方踩着1474，1884，向1460，1875发预留并在那截断的那一刻），**1459,1875 以西（朝车底方向）是空的**。」
2. 「此外，我对于你可能存在的错误推理提出纠正：**1474，1884往1460，1875的预留为观察时那个游戏会话产生，我十分确定**」

### 27.2 这两句话推翻了什么

| §26 的原结论 | 状态 | 推翻理由 |
| --- | --- | --- |
| §26.1「那 18 格本会话一次 `SET-RAIL`/`SET-STN` 都没有 ⇒ 不是本会话订的」 | **作废** | 见 27.3：日志没有 ≠ 没发生 |
| §26.3「东段是存档带入的旧位，西段此刻还空白 ⇒ 黑线止于 1460,1875 是旧位末端」 | **作废** | 玩家：西侧确实是空的，但**东段是本会话订的** ⇒ 「止于 1460,1875」是**本次订路真的被截断**，不是旧位尾端 |
| §26.4「踩着旧存档预留盲冲」 | **作废**（整个"旧位"前提消失） | 同上 |
| §26.5 待验证 1（审计是否把这 18 格列 stray） | **降级/作废** | 前提是旧位，若本会话订的就不该是 stray |

### 27.3 误判来源：`R3RResvWatchLog()` 的**未 watch 格聚合限行**（`pbs.cpp:249-332`）

代码自证（同一函数内两段注释 + 实现）：

- `pbs.cpp:263-264`：`const bool watched = R3RIsWatchedResvTile(tile); if (!watched && R3RResvPhaseIs(r3r_resv_phase, "misc")) return;`
  ⇒ **未 watch 的格 + 原生阶段 `misc` ⇒ 一行都不打**。
- `pbs.cpp:276-288` 注释自陈（第 274 轮续，KI-419②）：未 watch 格原来按 `(tile, track, kind, phase)` 每键首行 + 1/64 重复行，**改动后改成按 `(kind, phase)` 聚合**，因为"一次路线预订走一千格，单会话 6578 行、占日志 54%"。
- `pbs.cpp:323-330` 实现：`agg.n++` 后**只有 `agg.n == 1` 才打具体格坐标**，之后只在 `agg.n % 256 == 0` 打一条 `RESV-WATCH-SUM ... n= tiles= capped= last=x,y` —— **摘要只给 `last` 一格的坐标，其余全丢**。

⇒ **推论（本轮纪律变更）**：对任何未 watch 的格，「在日志里搜不到 `SET-RAIL tile=x,y`」**既不能证明本会话没订过，也不能证明订过**。这类格只能靠两类行间接看：①同 `(kind, phase)` 的**第一条**具体格（`agg.n==1`）；②`RESV-WATCH-SUM` 的 `n=`/`tiles=`/`last=`。
⇒ 记入取证陷阱清单：**`REG-505-27-A`（方法）**：今后凡涉及"某格本会话有没有被订"，必须先确认该格是否 watched（`R3RIsWatchedResvTile`），未 watch 者一律按"日志不可判"处理，不得下"没订过"的结论。

### 27.4 由此重新成立的问题（取代 §26 的提问）

观察时刻（`tick≈43821`，机车 `actTile=1474,1884`）：

- 黑线 = `1474,1884 → … → 1460,1875`，**本会话产生**，且**在 `1460,1875` 截断**；
- `1459,1875` 以西**没有任何预留**（玩家目视确认）。

⇒ 必须解释的是**两个**新问题：

- **`REG-505-27-B`（订路者）**：`1474,1884…1460,1875` 这 18 格由哪条代码路径订下？候选池已收窄：
  (a) 常规 PBS 预留（`ChooseTrainTrack`，`phase=yapf-resv`）——§26.1 里那两条 `SET-RAIL 1484,1884 phase=yapf-resv` / `SET-STN 1474,1884 phase=yapf-resv` 说明该 phase 本会话确实活跃过；
  (b) `ExtendTrainReservation()`（`RESV-EXT`，`phase=yapf-ext` 之类）——它专门"往前延伸预留"，最符合"从脚下延伸一条长线然后停"的形态；
  (c) `ReserveTrackUnderConsist()`（`phase=consist`?）——只订当前格，不可能铺 18 格，**可排除**（除非连续多 tick 前进逐格叠加，见下）。
  其中 (a)/(c) 有一个共同形态：**机车每 tick 前进一格就订脚下那一格**（`ReserveTrackUnderConsist` 或 PBS 重订），连续 18 个 tick 就会留下 18 格连续预留 —— 这同样产生"从 `1474,1884` 到 `1460,1875` 的连续线"，且**不需要任何一次订路覆盖全程**。§26.3 时间线里 `tick=43827..44047` 机车恰好逐格走完这 18 格，与该形态**完全吻合**，必须优先验证。
- **`REG-505-27-C`（截断）**：为什么停在 `1460,1875`？若为 (a)：目的地就在附近且已到；若为 (b)：扩展在下一格被拒（`TryReserveRailTrack` 返回 false 或方向不合法）；若为 (c)：只是"还没走到"，谈不上截断（但玩家看到的是"发预留并在那截断"，需与 (c) 区分）。

### 27.5 立即可做的取数（不猜）

1. 搜 `RESV-WATCH-SUM` 全量，读 `kind/phase/n/tiles/last/actor/head` —— 若存在 `kind=SET-RAIL phase=yapf-resv` 且 `n` 达数百、`tiles` 数十，即证明常规预留本会话铺了大量格，18 格被聚合掉。
2. 读 `R3RIsWatchedResvTile()` 的 watch 列表（`r3r_resv_probes.h` / `train_cmd.cpp`），确认 `1474,1884…1460,1875` 是否 watched —— 这一步决定 §26.1 那"0 条"到底是什么含义。
3. 确认 `ExtendTrainReservation()` 与外层调用点的 `r3r_resv_phase` 字面量，以便在日志里按 phase 过滤（`RESV-EXT-PRE` 已存在，`EXT-ENTRY/EXT-WALK/EXT-RES` 见第 471 轮探针）。

## 28. **取数结果：那 18 格是本会话常规寻路订的，日志只是被"聚合限行"吞掉**（`REG-505-27-B` 基本落定）

### 28.1 关键代码事实：watch 列表默认只有两格

`src\pbs.cpp:65-89` `R3RWatchedResvTiles()`：

```cpp
69:  /* Reported by the player (KI-205 "60,16" / KI-207 "57,49"). */
70:  v.push_back(TileXY(60, 16));
71:  v.push_back(TileXY(57, 49));
72:  FILE *f = fopen("r3r_resv_watch.txt", "r");
```

⇒ 运行期只有 `60,16`、`57,49`（外加可选的 `r3r_resv_watch.txt` 里的格）。**`1474,1884`…`1460,1875` 这 18 格全部是"未 watch 格"** ⇒ 全部走 `pbs.cpp:276-330` 的聚合分支 ⇒ 只有每个 `(kind, phase)` 对的**第一条**带坐标，其余静默到第 256 次才出一条只给 `last` 的摘要。

### 28.2 日志里 `RESV-WATCH` 全部只有 18 行（`build\_v12.txt` 全量）

```
RESV-WATCH SET-RAIL   tile=1499,1887 track=0   phase=choose-track actor=15 head=15
RESV-WATCH SET-RAIL   tile=1484,1884 track=0   phase=yapf-resv   actor=15 head=15
RESV-WATCH SET-STN    tile=1474,1884 track=255 phase=yapf-resv   actor=15 head=15
RESV-WATCH CLEAR-DEPOT tile=1500,1887 track=255 phase=controller actor=15 head=15
RESV-WATCH CLEAR-RAIL tile=1499,1887 track=0   phase=controller actor=15 head=15
RESV-WATCH CLEAR-STN  tile=1484,1875 track=255 phase=controller actor=75 head=75
RESV-WATCH CLEAR-RAIL tile=1473,1884 track=0   phase=choose-track actor=15 head=15
RESV-WATCH SET-RAIL   tile=1459,1875 track=0   phase=yapf-couple  actor=15 head=15
RESV-WATCH CLEAR-RAIL tile=1419,1873 track=0   phase=release-ghost actor=17 head=17
（以上 9 类各再出现一遍 = 18 行，两个会话周期逐字相同）
```

`RESV-WATCH-SUM` 全量**只有 1 条**：

```
RESV-WATCH-SUM CLEAR-RAIL phase=controller n=256 tiles=143 capped=0 last=1418,1873 actor=75 head=75
```

⇒ SET-RAIL 各 phase 的计数都没到 256（所以没有 SET 侧摘要），但**"没到 256"绝不等于"只订了几格"**：每个 `(SET-RAIL, yapf-resv)` 对从第 2 次起就全部静默。

### 28.3 三条正面证据：走廊里的格确实被本会话订过

1. **`SET-RAIL tile=1484,1884 phase=yapf-resv actor=15`** —— `yapf-resv` 是本会话 `actor=15` 的**第一次**常规 YAPF 铺路（更东 10 格）。其后 `yapf-resv` 铺的所有格（含走廊 18 格）全部静默。这与玩家"本会话产生"**一致**，与 §26.1 的"没订过"**相反**。
2. **`CLEAR-RAIL tile=1473,1884 track=0 phase=choose-track actor=15`** —— `1473,1884` 正好是黑线第二格（`1474,1884` 向西一格）。**能被 CLEAR 的格必然先被 SET 过**，而它的 SET 从来没打出来（被 `(SET-RAIL, choose-track)` 的首条 `1499,1887` 占位吞掉）。⇒ 走廊格 `1473,1884` **铁证被本会话订过**。
3. **`SET-STN tile=1474,1884 track=255 phase=yapf-resv actor=15`** —— 机车自己所在的站台格被整格预留（track=255），phase 同样是 `yapf-resv`。

### 28.4 结论：18 格 = 本会话常规寻路（`choose-track` / `yapf-resv`）铺的，不是存档带入

- `REG-505-26-A` / `REG-505-27-B`：**订路者 = 常规 PBS 预留路径**（`Train::TryPathReserve` → `ChooseTrainTrack` 的 lookahead 三处 + YAPF `TryReservePath` 逐格），phase 记作 `choose-track` / `yapf-resv`，`actor=head=15`。
- §26 整节（"存档带入"）**确认作废**，且作废原因不是"我推理错了方向"，而是**工具口径**：未 watch 格按 `(kind, phase)` 聚合，第一条之后无坐标。此点登记为取证陷阱 `REG-505-27-A`（已写入 §27.3）。

### 28.5 由此产生的新疑点与修正

- **`REG-505-28-A`（口径修正，重要）**：备忘 §24/§25 反复引用的「`_settings_game.pf.reserve_paths == false` ⇒ doRes 常为 0」**与本节证据冲突**。`src\train_cmd.cpp:18291`：

  ```cpp
  bool do_track_reservation = _settings_game.pf.reserve_paths || (flags & CTTF_FORCE_RES);
  ```

  而 `choose-track` phase 确实在 `1499,1887` 订了轨（`RESV-WATCH SET-RAIL ... phase=choose-track`）⇒ **本会话 `do_track_reservation` 为 true**，即 `reserve_paths==true`（或有 `CTTF_FORCE_RES`）。⇒ `RESV-EXT-PRE` 里的 `doRes=0` **不是**这个变量，§24.2「doRes=0 ⇒ 本函数不订路」的解释需要重新核对 `ExtendTrainReservation()` 里 `doRes` 的实参来源。**在核对前，不得再用「reserve_paths==false」做任何推断。**
- **`REG-505-28-B`（新主问题，取代 27-C）**：既然常规预留是"逐格铺一条路径"，那**为什么这条路径停在 `1460,1875`**（西侧一格不订）？三条候选，需用探针分辨：
  (a) YAPF 判目的地到达 —— 目标/安全位就定在 `1460,1875`；
  (b) `TryReserveRailTrack` 在 `1459,1875` 失败 —— 但 §24 实测 `1459,1875 tracks=0x11 reserved=0x0`（空、有轨道），**排除占用**；
  (c) 路径本身只到这里（YAPF 节点终点），`1459,1875` 属于下一段（couple 预留）的起点，两段在 `1460→1459` 处**首尾相接**。
  ⇒ (c) 与 §25 的 `CPL-RES-OK` 23 格（首格 `1459,1875`）**在几何上完美契合**：常规段止于 `1460,1875`、couple 段起于 `1459,1875`，合起来是一条连续线。若 (c) 成立，则"截断"只是**两段预留的接缝**，玩家看到的"发预留并在 1460,1875 截断"= 常规段已铺完、couple 段尚未开始的那一瞬。

### 28.6 下一步（仍不猜，只取这一条）

观察时刻 `actTile=1474,1884` 的同时段内，取 **`CHOOSE-TRACK`/`TRP`（`R3RLogReserveAttempt` 系列）** 与 **`EXT-ENTRY/EXT-WALK/EXT-RES`** 在该 tick 组的行，看：
- 常规预留这一次的**目的地/终点格**是否就是 `1460,1875`；
- `ExtendTrainReservation()` 的 `origin/target` 与 `RESV-EXT-PRE` 的 `doRes` 实参来源（读 `train_cmd.cpp` 该函数签名与调用点）。

判定 (a)/(c) 谁成立，即可收口"为什么停在这里"。

## 29. **答案：订路者就是 `ExtendTrainReservation()`，且 `1459,1875` 是一个真实空洞**（`REG-505-28-B` 收口）

### 29.1 决定性取证：全日志 `doRes=1` 只有 3 行，且三行逐字相同

`build\_v14.txt`（筛选 `RESV-EXT-PRE veh=15 .*doRes=1`）**全量内容**：

```
1:RESV-EXT-PRE veh=15 tile=1473,1884 enterdir=0 tracks=0x1 reserved=0x0 doRes=1 real=1 spd=0
2:RESV-EXT-PRE veh=15 tile=1473,1884 enterdir=0 tracks=0x1 reserved=0x0 doRes=1 real=1 spd=0
3:RESV-EXT-PRE veh=15 tile=1473,1884 enterdir=0 tracks=0x1 reserved=0x0 doRes=1 real=1 spd=0
```

⇒ **整份日志里 `veh=15` 只有这 3 次"真的去订路"的预留扩展**，全部发生在 `tile=1473,1884`、`spd=0`（机车**停着**）、`real=1`、且该格当时 `reserved=0x0`（**尚未预留**）。

对照 `build\_v13.txt`（`RESV-EXT-PRE veh=15 tile=14*`）的**紧邻下一行**：

```
RESV-EXT-PRE veh=15 tile=1473,1884 enterdir=0 tracks=0x1 reserved=0x1 doRes=0 real=1 spd=3
```

⇒ 同格从 `reserved=0x0` 变为 `reserved=0x1`、`doRes` 从 1 变 0、`spd` 从 0 变 3 ⇒ **这 3 行 `doRes=1` 就是那条预留的诞生点**，`1473,1884` 是它的逐格起点。

### 29.2 为什么视觉上从 `1474,1884` 开始、到 `1460,1875` 断

- `1474,1884` 是**站台格**，由 `RESV-WATCH SET-STN tile=1474,1884 track=255 phase=yapf-resv actor=15`（整格 `track=255`）预留 ⇒ 属于"脚下那一格"，视觉上就是黑线东端。
- 逐格订的部分从 `1473,1884` 起向西：`1473,1884 → 1472,1884 → 1472,1883 → 1471,1883 → …`（8 方向台阶，`_v13.txt` 逐行可查），一直订到 `1460,1875`。
- **`1459,1875` 在 `_v13.txt` 全量 102 行里始终 `tracks=0x11 reserved=0x0`** —— 该格**有轨道（X + LEFT 双槽，即岔口本体）却一行预留都没有**；而 `1460,1875` 是 `tracks=0x1 reserved=0x1`（已订）。
- `1458,1875` 的 `reserved=0x1` 出现在 **`spd=131` 之后的 tick**（`_v13.txt:24`），观测时刻（`spd=0`）还不存在 ⇒ 与玩家「`1459,1875` 以西是空的」**逐字吻合**。

⇒ 观测瞬间黑线 = `1474,1884`(站台整格) + `1473,1884…1460,1875`(本次扩展补订的 14 格) + `1459,1875`(空洞) + 以西全空。玩家的目视描述**完全准确**。

### 29.3 收口结论（三层）

| 问题 | 答案 | 证据 |
| --- | --- | --- |
| 这 18 格本会话有没有被订 | **有**，是本会话 `veh=15` 静止时订的 | `_v14.txt` 3 行 `doRes=1 reserved=0x0`；下一行同格 `reserved=0x1` |
| 谁订的 | **`ExtendTrainReservation()`**，不是 couple 寻路 `rp()`、也不是站台 `ReserveTrackUnderConsist()` | 全日志 `doRes=1` 仅此 3 行；`rp()` 的 23 格首格是 `1459,1875`（§25），与本案 18 格无交集 |
| 为什么停在 `1460,1875` | 该次扩展的迭代末端在 `1460,1875`，**`1459,1875` 被漏订（空洞）**，再往西本来就没预留 | `1459,1875` 全程 `reserved=0x0`；`1458,1875` 的空→有发生在 `spd>0` 之后 |

### 29.4 「不安全」的机理（这才是真正要修的点）

`1459,1875` 是 `tracks=0x11`（**X | LEFT 双槽 = 岔口本体**）。路径必须沿 X 从 `1460,1875` 穿到 `1458,1875`，但**岔口格本身没有被预留**：

1. 其它列车/路径可以把 `1459,1875` 的 `TRACK_LEFT` 支线合法订走（该格 `reserved=0x0`，对任何订路者都是"空闲岔口"）；
2. 于是本列车开到 `1460,1875`、下一步要进 `1459,1875` 时，岔口已被别人占 ⇒ `TrainController/TryPathReserve` 只能刹停；
3. 更糟：`ExtendTrainReservation` 的语义是"把预留往前延长"，它把**自己已走过的格全部保留**（`NOLOOK-PASS`/`SELF-AIR` 直接放行不重订），但**空洞格既不订也不报**，所以列车会带着一条"看起来很长、实际在岔口处断开"的预留继续行驶。

⇒ 与 KI-346（`rp()` 把进入轨算成 `TrackdirToTrack(cur_td)` 导致双槽格订错轨）**同族但不同点**：KI-346 是"订了错的轨"，本条是"**根本没订**"。

### 29.5 仍未确认（明确列出，不填猜）

- **`U-1`**：`ExtendTrainReservation` 在 `1460,1875 → 1459,1875` 这一步为什么既不订、又不报？候选：`R3RReservationHeldBySelf()` 误判为"自己的预留"（`SELF-AIR` 探针就在这一带，`train_cmd.cpp` 内 `_v13.txt:23` 紧跟 `SELF-AIR`）；或 `TryReserveRailTrackdir` 返回 true 但 setter 因 `HasReservedTracks` 双槽判定短路。
- **`U-2`**：`doRes=1` 只出现 3 次、其余 99 条全是 `doRes=0` ⇒ 说明该函数绝大多数调用只"走一遍不订"，符合 `do_track_reservation` 由 `reserve_paths`/`CTTF_FORCE_RES` 决定的形态；但本会话确有 `choose-track` 的 `SET-RAIL 1499,1887` ⇒ `reserve_paths` 真实值待用控制台/设置面板确认（`REG-505-28-A` 仍未结）。
- **`U-3`**：`_v13.txt:28/31/33` 的 `tracks=0x11 reserved=0x1`、`tracks=0x9 reserved=0x1` 只订了 X、未订另一槽，属"只订走的轨"的正常形态，还是同族漏订，需在修复 `1459,1875` 后一并复核。

### 29.6 建议修法（待玩家拍板，本轮**未改任何源码**）

- **A（治标）**：`ExtendTrainReservation` 在遇到 `reserved=0x0` 且 `doRes==0` 的格时**不得静默放行**，必须打一行 `RESV-GAP veh= tile= tracks= reserved=` 并把该格补订（用与 `rp()` 修好后的 `FindFirstTrackdir` 同一口径），保证"路径连续 ⇒ 预留连续"。
- **B（治根）**：与 KI-346 一起，把"预留格集合"和"跟随器实际走的路"做一次**提交前的连续性断言**（挂车/到站两个提交点），断一格即拒并打日志。
- **C（诊断先行）**：在 `ReserveTrackUnderConsist` / `ClearReservationUnderConsist` 与 `ExtendTrainReservation` 之间加"该 tick 预留集合快照 diff"探针，直接定位是"没订"还是"订了又被清"。

## 30. 玩家拍板"先加探针" + 给出正确基准线（2026-11-25）

### 30.1 玩家原话（逐字）

1. 「我向你澄清，你所说的那次 … 描述的没有问题。」（确认 §29.2 的四段拆解无误：`1474,1884` 站台整格 + `1473,1884…1460,1875` 14 格 + `1459,1875` 空洞 + 以西全空）
2. 「**如果它是正常的 extend，他应该到(1457,1874)截止**」
3. 「**我认为，我们应当先添加探针**」

### 30.2 这条基准线是本轮唯一的判据锚

玩家给出了"正常 extend 的终点"= **`1457,1874`**，而不是 `1460,1875`。

⇒ 判定口径（`REG-505-30-A`）：
- 实测末端 `1460,1875` ⇔ 期望末端 `1457,1874`，**短了 3 格**（`1460,1875 → 1459,1875 → 1458,1875 → 1457,1875? → 1457,1874` 沿 X 线继续）。
- 所以 `ExtendTrainReservation` 在 `1460,1875` 之后的第 1~3 步内提前退出，且**退出的那一步没有留下任何失败日志**。
- 注意 `1457,1874` 正是 §22 已知的岔口（`CPL-RES-TERM-EXEMPT / CPL-ORIGIN-TD tdb=0x3030 tile=1457,1874`，见 KI-680 §现场证据）⇒ 玩家给的这个点与"预留要在岔口前正确收住"的语义一致：正常 extend 应当**停在那格**，而不是提前 3 格停、把 `1459,1875`（双槽岔口本体）留在空洞里。

### 30.3 本轮任务（玩家已拍板，不再讨论方案）

**先加探针，不改任何判定**。目标是把 `ExtendTrainReservation()` 逐格循环的四个退出点全部显式化：

| 退出点 | 期望能抓到的东西 |
| --- | --- |
| `while (ft.Follow(...))` 为假 | 跟随器在 `1460,1875` 之后无路可走（`ft.m_new_tile`/`m_new_trackdir` 状态） |
| `r3r_visited` 命中（自环/回访） | 是否把下一格判成"已访问"而提前 `break` |
| `TryReserveRailTrackdir` 返回 false | 该格预留失败（但 §24 实测 `1459,1875 reserved=0x0`，不是被占） |
| 目标/lookahead 长度满足 | 是否本来就只要求铺这么长（这才能解释"正常应该到 1457,1874"之外的偏差） |

### 30.4 现有可复用探针清单（先查再用，避免重复造）

第 471 轮已给同一函数加过三处只读探针（见记忆与备忘 §471）：`EXT-ENTRY`（origin）/ `EXT-WALK`（old→new/bits/skip/stn/plain/exit/cur/choice/target）/ `EXT-RES`（tile/td/track/ok/ord）。**本轮第一步必须先确认它们是否已在日志里命中 `veh=15`**：
- 若命中 ⇒ 直接读 `EXT-WALK` 的最后几行就能看出循环在哪个环节停下，无需再加码。
- 若未命中（被 `R3RDbgEdge` 128 帧窗口抑制、或该 exe 未含该探针）⇒ 才新增/放宽探针。

### 30.5 新增探针的设计原则（若确需新增）

1. **只读**：不插任何 `return`/`break`/`continue`，不改变 `ft` 状态，不改 `r3r_visited`。
2. **零刷屏**：沿用 `R3RDbgEdge` 边沿触发，但 key 必须含 `tile`，否则同一列车同状态只留一行、下一格又变了就丢。
3. **必须落在 `break` 之后**：在循环出口处打一条 `EXT-STOP veh= tile= reason=<follow-fail|visited|reserve-fail|target-reached|len-limit> next=x,y next_ok=0xN res=0xN`，这是唯一能直接回答"为什么停"的行。
4. 若 `reason` 仍是空，再补 `EXT-FOLLOWER`（`ft.m_new_tile/m_new_trackdir/m_new_track/m_err`）。

### 30.6 待验证

- `U-30-1`：正常 extend 的"应该到 `1457,1874`"这一基准，是玩家从**同一次观测**的相邻样本推的，还是从"挂接预留应当覆盖到岔口前"的原则推的？——两者都指向同一格，暂按同一格处理，但若实测 `EXT-STOP` 报出合法 `reason=target-reached`，则需回头确认基准线本身。
- `U-30-2`：`EXT-WALK` 若存在，其 `choice=` 字段（`KillFirstBit` 后的选择）是否在 `1460,1875` 附近出现了双槽格分支丢失。

---

## 31. 探针侦察结论：第 471 轮的 EXT-* 已不存在，函数自带 EXTEND-BACKOUT（2026-11-25）

按 §30.4「先查再用」执行，三条取证（全部只读，零改动）：

### 31.1 第 471 轮的 `EXT-ENTRY / EXT-WALK / EXT-RES` 已经不在源码里

`search_content` 在 `src\train_cmd.cpp` 搜 `EXT-ENTRY|EXT-WALK|EXT-RES|R3REDGE_EXTENTRY` ⇒ **0 命中**。
⇒ 这三处探针在 471 轮之后的某次重构/回退中被移除了。**不要再按"日志没有 EXT-WALK"推断"没走 Extend"**。

### 31.2 当前 exe 里也没有这些字面量

`findstr /m /c:EXT-ENTRY /c:EXT-STOP /c:EXT-WALK build\openttd.exe` ⇒ `build\_v18.txt` **为空**。
⇒ 与 31.1 自洽：当前 `build\openttd.exe` 不含第 471 轮探针。

### 31.3 但 `ExtendTrainReservation()` 现在自带一套更完整的出口探针

`src\train_cmd.cpp:17220-17593`，现成的、无需新增的只读探针：

| 标签 | 位置 | 字段 |
| --- | --- | --- |
| `EXTEND-BACKOUT` | :17533（回滚入口，按 `(veh,next,reason,owner)` 节流：前 4 次全打，之后每 256 次一行） | `veh origin= stop= next= reason= err= owner= whole= track= need=0x%X resv=0x%X led= n=` |
| `EXTEND-BACKOUT-SKIP` | :17583 | 桥/隧道近端头格 `tile skipped resvAfter` |
| `EXTEND-GOAL-EXEMPT` | :17388（第 406 轮会合豁免） | `veh tile pair whole track need resv` |
| `STALE-OWN-BIT` | :17398 | 自己的陈旧位被清 `veh tile track led cleared` |
| `LOOP-CLOSED` | :17437（`R3REDGE_WALKLOOP`） | `veh origin= tile= steps= track=` |
| `LOOP-MEMO` | :17235（KI-384 记忆化重放） | `veh origin= tile= age=` |

`r3r_backout_reason` 的取值域（:17249 起）：`follow-fail`（默认，即 `while (ft.Follow(...))` 为假）/ `oneway-signal` / `90deg-turn` / `choice-reserved` / `wait-at-pbs` / `safe-reserve-fail` / `safe-not-free` / `reserve-fail`。
⇒ **§30.5 想要的 `EXT-STOP reason=<...>` 语义，`EXTEND-BACKOUT` 已经全覆盖**（连 `led=`「这格是谁写的」都有）。

### 31.4 本次日志按这些标签过滤 ⇒ 全部 0 命中

`build\_v19.txt` = `EXTEND-BACKOUT | findstr veh=15`、`STALE-OWN-BIT`、`LOOP-CLOSED`、`EXTEND-GOAL-EXEMPT` 四路合并 ⇒ **文件为空**。

三种可能（**未区分，必须下一步先判别**，属疑点 `REG-505-31-A`）：
1. 该日志（2986 行）根本不覆盖 `veh=15` 订那 18 格的那一 tick（日志是片段，不是全程）；
2. 探针被 `R3RDbgOn()` / 标签白名单关掉（第 274 轮就发生过一次「EXTEND-BACKOUT 被白名单静默关掉」的同类事故）；
3. `veh=15` 的这 18 格**不是** `ExtendTrainReservation` 铺的（但 §29 已用 `RESV-EXT-PRE veh=15 doRes=1 @1473,1884` 三条逐字相同的行把它钉成诞生点，故此可能性最低）。

判别只需一条统计：日志里 `EXTEND-BACKOUT`（不限 veh）、`RESV-EXT-PRE`、`extend-backout`（小写，出现在 `UNRES-RAIL` 的 phase 里）各多少行。
- 若 `EXTEND-BACKOUT` 总数 > 0 而 `veh=15` 为 0 ⇒ 可能性 1（日志片段不含该 tick），要玩家补全日志或重跑；
- 若三者全 0 而 `RESV-EXT-PRE` 也为 0 ⇒ 可能性 2（探针被关），先查 `R3R_PROBES` / 标签白名单，再谈加探针；
- 若 `RESV-EXT-PRE` > 0 而 `EXTEND-BACKOUT` = 0 ⇒ 说明 `veh=15` 那次 extend **成功返回**（没有回滚），则 §30.2 的"短 3 格"不是被回滚掉的，而是**根本没走到那 3 格**（`ft.Follow` 直接失败），需要的是 `EXTEND-BACKOUT` 之外的"成功出口"探针（当前函数成功返回有 3 个出口：:17416 选择格、:17457 安全等待位、:17496 线路尽头）。

### 31.5 待办（按 31.4 的分流）

- 分支 A（日志是片段）⇒ 让玩家重跑同场景并保留完整 `R3R_debug.log`。
- 分支 B（探针被关）⇒ 先修探针开关，不加新码。
- 分支 C（extend 成功返回但短 3 格）⇒ 才需要按 §30.5 新增「成功出口」探针（`EXT-SUCCESS-EXIT veh= tile= kind=choose|safe|eol cur= choice= target= len=`），因为现有探针只在**失败回滚**路径上打日志。

---

## 32. 计数结果定案：走的是「成功出口」，`EXTEND-BACKOUT` 一条都没有（2026-11-25）

玩家批准后跑了一条只读 `findstr`，四个标签各写一个文件（`build\_c1.._c4.txt`）：

| 标签 | 文件 | 结果 |
| --- | --- | --- |
| `EXTEND-BACKOUT`（失败回滚主行） | `build\_c1.txt` | **空（0 行）** |
| `RESV-EXT-PRE`（每次订路入口） | `build\_c2.txt` | **有**，前 6 行全部 `veh=15` |
| `extend-backout`（小写，`UNRES-RAIL` 的 phase 字段） | `build\_c3.txt` | **空（0 行）** |
| `EXT-WALK`（第 471 轮探针） | `build\_c4.txt` | **空（0 行，源码已无此探针，自洽）** |

`build\_c2.txt` 前 6 行原文（逐字）：

```
RESV-EXT-PRE veh=15 tile=1473,1884 enterdir=0 tracks=0x1  reserved=0x0  doRes=1 real=1 spd=0
RESV-EXT-PRE veh=15 tile=1473,1884 enterdir=0 tracks=0x1  reserved=0x1  doRes=0 real=1 spd=3
RESV-EXT-PRE veh=15 tile=1472,1884 enterdir=0 tracks=0x10 reserved=0x10 doRes=0 real=1 spd=131
RESV-EXT-PRE veh=15 tile=1472,1883 enterdir=3 tracks=0x20 reserved=0x20 doRes=0 real=1 spd=156
RESV-EXT-PRE veh=15 tile=1471,1883 enterdir=0 tracks=0x10 reserved=0x10 doRes=0 real=1 spd=160
RESV-EXT-PRE veh=15 tile=1471,1882 enterdir=3 tracks=0x20 reserved=0x20 doRes=0 real=1 spd=160
```

### 32.1 判定（`REG-505-32-A`）

1. **31.4 的分支 C 命中**：`ExtendTrainReservation` 在本次 `veh=15` 的整段订路里**从未回滚过**（`EXTEND-BACKOUT` 与小写 phase 双零）。
2. 因此「日志没有 extend 的失败记录」是**真实事实、不是探针被关**：同一次运行里 `RESV-EXT-PRE` 打得出来，说明 `R3RDbgOn()` 打开；`EXTEND-BACKOUT` 的节流是"前 4 次全打"，不可能被节流吃掉。
3. `doRes=1` 只有**第一行**（`1473,1884 reserved=0x0 spd=0`），之后全 `doRes=0` ⇒ 后续每行只是"车在路上、报告已有预留"，并不做新预约。**这 18 格是一次预约一次性铺完的**，与 §29 结论一致。
4. ⇒ **"短 3 格"不是被回滚掉的，而是 `ExtendTrainReservation` 主动停在 `1460,1875` 并成功返回**。它有三个成功出口（`src\train_cmd.cpp`）：
   - `:17416` **选择格**：`target_seen || KillFirstBit(ft.new_td_bits) != TRACKDIR_BIT_NONE` 成立 ⇒ 返回 `okay=false`，**把余下路程交回 pathfinder**；
   - `:17457` **安全等待位**（`IsSafeWaitingPosition && wp_free && TryReserveRailTrackdir`）⇒ `okay=true`；
   - `:17496` **线路尽头**（`ft.err == Owner || NoWay`）⇒ `okay=true`。
5. `1460,1875` 的读数是 `tracks=0x11`（X | LEFT 双槽）⇒ `KillFirstBit != NONE` 恒成立 ⇒ **最可能命中 `:17416`「选择格」出口**：extend 认为"这里是个岔口，剩下的交给寻路器"，于是收手。这在语义上**不是 bug**，`1457,1874` 与 `1459,1875` 之间那 3 格本来就该由 yapf 在 choice 之后续订。
6. ⇒ 疑点转向**交接**：extend 在 `1460,1875` 交棒后，`ChooseTrainTrack`/yapf 为什么没有把 `1459,1875` 订下来（该格全程 `reserved=0x0`，见 §29）。这解释了玩家看到的"黑线在 1460,1875 截断、以西全空"。

### 32.2 现有探针对这一层是盲的（为什么必须新增）

- `EXTEND-BACKOUT` 只在**失败回滚**分支（`:17524`）打；
- `:17416` / `:17457` / `:17496` 三个**成功出口一行日志都没有**；
- ⇒ 只靠现有探针永远分不清"停在 choice"与"停在 safe-wait"与"线路尽头"，也无法拿到 choice 格的 `new_td_bits`。这正是 §30.5 第 3 条要求补的那一行。

### 32.3 待办（下一步，`EXT-EXIT`，只读、零判定改动）

在**三个成功出口各加一行**（不改任何 branch 条件、不动 `ft` 与 `r3r_visited`）：

```
EXT-EXIT veh=%u origin=%d,%d/%d tile=%d,%d/%d kind=choose|safe|eol cur=%d nextbits=0x%X target=%d skip=%u steps=%u choice=0x%X\n
```

- `kind=choose` 打在 `:17416` 之前（`new_tracks`/`enterdir` 填好之后），并把 `KillFirstBit` 后的候选轨道位打出 ⇒ 直接看 `1460,1875` 到底是"岔口"还是"目标 tile"；
- `kind=safe` 打在 `:17457` 之前（`IsSafeWaitingPosition` 为真的那一格）；
- `kind=eol` 打在 `:17496` 之前；
- 节流沿用 `R3RDbgEdge`，key 必须含 `tile`（§30.5 第 2 条），否则同一列车只留第一格。

判据（复测一次即可收口）：
- 若 `EXT-EXIT ... tile=1460,1875 kind=choose nextbits=0x11` ⇒ 结论锁定"extend 正当收手于双槽格"，转而查 yapf 交接（`RP cur=` 第一格为何不是 `1459,1875`）；
- 若 `kind=safe`/`kind=eol` ⇒ 说明 extend 把 `1460,1875` 误判成安全等待位/线路尽头，**这才是 extend 侧的 bug**，且与玩家基准 `1457,1874` 直接冲突；
- 若三行全无 ⇒ 回到 §31.4 分支 B（探针被关）或"这 18 格不是 extend 铺的"，需推翻 §29。

### 32.4 探针已落地（源码改完、lint 0，**尚未构建**）

只改 `src\train_cmd.cpp`（未碰任何 `src\*.h` ⇒ 符合增量合规）：

1. `R3REDGE_EXTGOALEXEMPT` 之后新增 `R3REDGE_EXTEXIT`（`:9382`，`R3REDGE_COUNT` 之前）。
2. **选择格出口**（原 `:17416`）：在 `*new_tracks`/`*enterdir` 赋值之后、`return PBSTileInfo(ft.new_tile, ft.old_td, false)` 之前插 `EXT-EXIT ... kind=choose`。
3. **安全等待位出口**（原 `:17457`）：在 `TraceRestrictExecuteResEndSlot(v)` 之前插 `EXT-EXIT ... kind=safe`（放在 TraceRestrict 之前，保证纯读、不改变其执行序）。
4. **线路尽头出口**（原 `:17496`）：在 `return PBSTileInfo(ft.old_tile, ft.old_td, true)` 之前插 `EXT-EXIT ... kind=eol`，并附 `err=`。

统一字段：`veh origin=x,y/td tile=x,y td result_nextbits=0xX stn= skip= steps= [err=]`，
边沿 key = `(veh<<40) | (tile.base()<<8) | kind`（kind 1=choose/2=safe/3=eol）⇒ **同一列车不同 tile 都会各留一行**，不会被"每车只留第一格"吃掉。

`read_lints` = 0 条。构建方式（待玩家确认游戏已关）：`R3R_fullbuild.cmd inc detach`。

**判别口径（一次复测即可收口）**：看 `EXT-EXIT veh=15` 里 `tile=1460,1875` 那一条的 `kind`：
- `kind=choose nextbits=0x11` ⇒ extend 正当收手于双槽岔口，问题在交接（yapf 没续订 `1459,1875`），转查 `RP cur=` 第一格；
- `kind=safe` ⇒ extend 把 `1460,1875` 误判为安全等待位（与玩家基准 `1457,1874` 直接冲突，**extend 侧 bug**）；
- `kind=eol` ⇒ extend 判成线路尽头（同上，且 `err=` 会给出 Owner/NoWay）；
- `td=` 还能顺带回答 §30.6 的 `U-30-2`（是不是在双槽格丢了分支）。

### 32.5 构建已启动（2026-11-25，玩家批准「现在编」）

- 入口：`R3R_fullbuild.cmd inc detach`（后台最小化，唯一既有入口，未新建任何 .cmd）。
- `build\R3R_incbuild.guard.log` 尾部逐字：`GUARD: incremental is safe (no header/lang file is newer than the newest object)`
  ⇒ 本轮只改 `src\train_cmd.cpp`，**未碰任何 `src\*.h` / `lang\*.txt`**，护栏放行增量（不会退化为 16~20 分钟全量）。
- `build\R3R_incbuild.log` 尾部逐字（换行被终端折断，按语义复原）：`[2/3] Building CXX object CMakeFiles\openttd_lib.dir\src\train_cmd.cpp.obj`
  ⇒ 已进入 `train_cmd.cpp` 重编，尚未到 `[3/3] Linking`。
- 观察时刻 `build\R3R_incbuild.done` 尚不存在（`NO_DONE`）。

### 32.6 构建完成自证（2026-11-25 02:46，玩家回「构建疑似完成，你确认一下」）

一条只读 `cmd` 把 5 项核对写进 `build\_chk.txt`（该文件已读，逐字如下）：

| 项 | 实测 | 判定 |
| --- | --- | --- |
| `build\R3R_incbuild.done` 内容 | `EXIT_CODE=0` | **通过** |
| `build\R3R_incbuild.done` mtime | `2026/11/25 02:46`（13 B） | — |
| `build\openttd.exe` | `2026/11/25 02:46`，**54 380 544 B** | **晚于源码** |
| `src\train_cmd.cpp` | `02:42`（1 399 983 B） | 基准 |
| `train_cmd.cpp.obj` | `02:44`（16 211 592 B） | **晚于 .cpp** |
| 日志 Linking 行 | `[3/3] Linking CXX executable openttd.exe`（命中 1 次） | **通过** |
| `findstr /m /c:EXT-EXIT build\openttd.exe` | `build\openttd.exe`（**命中**） | **三个成功出口探针已入库** |

⇒ 时间链 `train_cmd.cpp 02:42 → obj 02:44 → exe 02:46` 单调成立，无陈旧 obj；`EXT-EXIT` 字面量在 exe 内可检索 ⇒ 本轮 exe 就是带 `EXT-EXIT` 的那一版。

**待玩家用这版 exe 复测，判别口径照 §32.4**：只看 `EXT-EXIT veh=15` 里 `tile=1460,1875` 那一条的 `kind`——`choose` ⇒ 交棒问题（转查 `RP cur=` 第一格）；`safe`/`eol` ⇒ extend 侧误判安全位/线路尽头（真 bug）。

### 32.7 疑点 REG-505-32-B：玩家复测日志里**找不到 `EXT-EXIT`**（2026-11-25）

**期望值**：本轮 exe（`build\openttd.exe` @02:46，含 `EXT-EXIT` 字面量，`findstr` 已命中）跑同一场景时，`build\R3R_debug.log` 里应至少出现一条 `EXT-EXIT veh=15 ... tile=1460,1875`。
**实测值**：玩家报告「日志里找不到 EXT-EXIT」。**判定：暂不能定性为探针 bug**，先排除「日志/exe 不配套」。

四个待排查项（按命中概率排序，各自一条只读证据即可定性）：

| # | 假设 | 判别证据 | 若成立则 |
| --- | --- | --- | --- |
| H1 | 玩家看的是**旧日志**（`build\R3R_debug.log` 的 mtime 早于本轮 exe 启动） | 日志 mtime 是否晚于 02:46 / 日志内 `RP cur=`、`COUPLE-OK` 是否为本次场景 | 只需确认日志文件路径（exe 的日志写在**自身所在目录**） |
| H2 | 玩家跑的**不是 `build\openttd.exe`**（别处旧副本/发布树） | 日志 mtime 与 `build\openttd.exe` mtime 对不上；或 `build-release\openttd.exe` 被启动 | 换 `build\openttd.exe` 重跑 |
| H3 | **探针总开关关着**（`R3RDbgOn()` false ⇒ 全部 R3R 探针都不出） | 日志里连 `RP cur=` / `FOLDCHK` / `RESV-AUDIT` / `CPL-` 全都为 0 | 开调试日志后重跑（不是 `EXT-EXIT` 本身的问题） |
| H4 | **`ExtendTrainReservation()` 本次根本没进**（判据格不在 extend 路径上） | `EXT-ENTRY`/`EXT-WALK`/`EXT-RES`（第 471 轮已有）也全为 0，但 `RP cur=` 有 | 推翻 §29「这 18 格是 extend 铺的」，改查 long-reserve 递归（`:18509`） |
| H5 | 三个出口**没覆盖实际走的那条**（代码缺口：`goto` 跳出 / 早退分支） | `EXT-ENTRY` 有、`EXT-EXIT` 无，且能对上 `EXT-WALK` 的最后一步 | 属本轮探针设计缺陷，需在缺失出口补一行 |

**关键区分点**：H1/H2/H3 是「数据没进来」，H4/H5 是「数据该进来却没打」——只要先在日志里搜 `RP cur=` 与 `EXT-ENTRY` 各出现几次，就能一刀切开。$\Rightarrow$ 已请玩家回这四个计数。

**结论（2026-11-25，玩家回报三个计数）**：`RP cur=` **出现很多次**、`EXT-ENTRY` **0 条**、`EXT-EXIT` **0 条**，且玩家确认日志时效性（就是本轮 exe 刚跑的）。

⇒ **H1/H2/H3 全部排除**（探针总开关正常、日志是本轮的）。

### 32.8 判据失效纠正（2026-11-25，重要）：`EXT-ENTRY` / `EXT-WALK` / `EXT-RES` 在当前源码里**根本不存在**

**取证（`search_content` 于 `src\train_cmd.cpp` 单文件，pattern `EXT-ENTRY|EXT-WALK|EXT-RES|EXT-EXIT`）**：命中 **3 条，全是 `EXT-EXIT`**（`:17422 kind=choose`、`:17470 kind=safe`、`:17518 kind=eol`）。`EXT-ENTRY` / `EXT-WALK` / `EXT-RES` **零命中**。

交叉印证（读 `:17221-17340`）：函数体 `:17299 r3r_visited.insert(...)` 之后**紧接** `:17300 while (ft.Follow(...))`，中间没有任何日志语句。而第 471 轮记忆写的是「EXT-ENTRY 在 insert 之后、while 之前」⇒ 那三个入口/过程探针**已被后续轮次（第 249/251/252 轮对 `ExtendTrainReservation` 的 KI-371/KI-384 重构）删除**，本轮只剩我新加的 `EXT-EXIT`。

⇒ **§32.7 里「`EXT-ENTRY` = 0 就说明函数没被调用」这条判据作废**，是判据本身无效，**不是** extend 没跑。上一行据此得出的「H4 成立」结论一并撤回。

**改用三个真实存在的判据**（都在本轮 exe 里）：

| 日志字面量 | 出处 | 判读 |
| --- | --- | --- |
| `EXT-EXIT` | `:17422/:17470/:17518`（**三个成功出口**） | 0 条 ⇒ 没走成功出口（成功出口 = 遇 choice / 安全位 / 线路尽头） |
| `EXTEND-BACKOUT`（字面量已核准，`:17561`） | 失败回溯出口（`:17531` `extend-backout` 段） | 有 ⇒ extend **被调用过**、且试铺失败；行内 `stop=`（最后接受的格）/`next=`（跨不过去的格）/`reason=`/`owner=`/`need=`/`resv=` 直接给出缺口位置与拦路人 |

`EXTEND-BACKOUT` 的节流：按 `(veh, next tile, reason, owner)` 计数，**前 4 次全打**、之后每 256 次一条 ⇒ 同一现场最多丢细节，不会一条都没有。它也包在 `if (R3RDbgOn())` 里，与 `EXT-EXIT` 同级 ⇒ 两者计数可比。

⇒ 判别式：`EXTEND-BACKOUT` 或 `LOOP-MEMO`（`:17236`，闭环 memo 重放，会**提前 return** 而跳过三个成功出口）任一非 0 ⇒ extend 确实在跑，只是本次没走成功出口（那么"18 格谁铺的"要按 §29 重审）；两者也都为 0 且 `EXT-EXIT` 为 0 ⇒ 才轮到"调用点未触发"（`do_track_reservation` 恒 false / `:18659` 之前的 return）。

**下一步（不必重跑，玩家日志已在手）**：请玩家在 `build\R3R_debug.log` 搜 `EXTEND-BACKOUT` 与 `LOOP-MEMO`，报"有没有 + 各几条"。若有 `EXTEND-BACKOUT`，把那几行原文贴来——`stop=`/`next=`/`reason=`/`owner=` 就是 §29 那个岔口的直接答案。

### 32.9 结论（2026-11-25，玩家回报）：H4-a 成立 —— `ExtendTrainReservation()` 本次**未被调用**

玩家回报：`EXTEND-BACKOUT` **0 条**、`LOOP-MEMO` **0 条**（另加此前 `EXT-EXIT` 0 条、`RP cur=` 大量）。

⇒ 三个真实判据**全部为零**。穷举 `ExtendTrainReservation()` 函数体的全部去路，每条都必然留下痕迹：

| 去路 | 代码位置 | 探针 | 实测 |
| --- | --- | --- | --- |
| 成功出口 ① 遇 choice | `:17419-17430` | `EXT-EXIT kind=choose` | 0 |
| 成功出口 ② 安全位 | `:17467-17478` | `EXT-EXIT kind=safe` | 0 |
| 成功出口 ③ 线路尽头 | `:17515-17522` | `EXT-EXIT kind=eol` | 0 |
| 试铺失败回溯 | `:17527` 起 backout 段 | `EXTEND-BACKOUT` `:17561` | 0 |
| 闭环 memo 命中提前 `return` | `:17229-17241` | `LOOP-MEMO` `:17236` | 0 |

唯一理论上能"静默返回"的只有 memo 分支（`:17235` 的 `LOOP-MEMO` 包在 `R3RDbgEdge` 边沿闸门里，可能被抑制），但它 `return PBSTileInfo(..., false)` 带 **`okay=false`**，会让调用点落到 `:19207` 「A path was found, but could not be reserved」分支 ⇒ 与"18 格预留已铺好、车照常开"的现场直接矛盾，**排除**。

⇒ 只剩一个解释：**`:18659` 的 `if (do_track_reservation)` 为假，函数一次没被调**。而这恰好对应 `:18431-18440` 那段注释描述的捷径——「下一格已经有我的预留，直接开进去就行」，第 343/329 轮的 `r3r_couple_stuck_retry` / lookahead 豁免正是为了**绕过** `ExtendTrainReservation() + DoTrainCouplePathfind()`。

**⇒ §29 前提推翻**：1455,1874 岔口那 18 格预留**不是本次 extend 铺的**。§29 关于"extend 自铺不归"的推论、§30 的"预留弧先钉后扩"读法、以及 §31/§32 针对 extend 的三个探针解读，全部降级为"路径不成立"。

**新问题（必须回答）**：`do_track_reservation == false` 时，那 18 格来自哪里？三个候选：

| 候选 | 机制 | 判别 |
| --- | --- | --- |
| C-1 上一 tick 的 extend 残留 | 前几 tick 走过 `do_track_reservation=true`，铺下后本 tick 走捷径 | 需看岔口前后若干 tick 有无 `EXT-EXIT`（本日志为 0 ⇒ 同一次会话内从未铺过 ⇒ 排除） |
| C-2 yapf 长预留 `rp()` | `TryPathReserve` 主路径的 `rp()` 递归，绕过 `ExtendTrainReservation` | 查 `RP cur=` 是否覆盖 1455,1874 → 1460,1875 这条弧 |
| C-3 `ReserveTrackUnderConsist()` | 车下脚下保护每 tick 重订 | 只覆盖车下 1 格，无法解释 18 格 ⇒ 排除 |

⇒ C-2 成为唯一可行方向：**那 18 格极可能出自 yapf 的 `rp()` 长预留**（第 240 轮 KI-346 就在 `rp()` 里修过「进入格订错 Track」，同族位置）。

下一步取证（一次读取即可）：读 `:18540-18665`，确认 `do_track_reservation` 的置位条件，以及为假时 `tracks`/`origin` 从哪来——这条捷径若直接沿用旧预留，则"18 格"必须是**上游某个更早的 `TryPathReserve`** 铺的。

### 32.10 代码取证结果（2026-11-25）：捷径在 `:18551` 的 `early-haverserved`，且它**没有日志**

读 `src\train_cmd.cpp:18530-18660`，找到那条约定的真身：

```18550:18553
	/* Do we have a suitable reserved track? */
	if (res_tracks != TRACK_BIT_NONE && !r3r_couple_stuck_retry && !r3r_couple_split_res && !r3r_couple_foreign_res && !r3r_couple_no_lookahead) return r3r_chosen_ret(FindFirstTrack(res_tracks), result_flags, "early-haverserved");
```

**这就是 extend 不被调用的机制**：它在 `:18658 if (do_track_reservation)` **之前**无条件 `return`，所以本 tick 既不调 extend、也不碰 yapf。四个豁免变量定义如下（都在同一作用域内）：

| 变量 | 定义位置 | 含义 | 何时为真 |
| --- | --- | --- | --- |
| `r3r_couple_stuck_retry` | `:18437-18438` | 挂接订单 + 非车库 + **车已停住**（`cur_speed == 0`） | 只对停住的车成立 |
| `r3r_couple_split_res` | `:18439` 一带 | 本车预留被拆开/不完整 | 预留残缺时 |
| `r3r_couple_foreign_res` | `:18439` 一带 | 格上有别人的预留 | 有外来位时 |
| `r3r_couple_no_lookahead` | `:18533-18534` | 挂接订单 + 非车库 + **`lookahead == nullptr`** | 无前瞻状态时 |

**⇒ 行驶中的挂接机车（`cur_speed > 0`）且 lookahead 非空、格上有自己的预留 ⇒ 四个豁免全假 ⇒ 走 `early-haverserved`，天然不调 extend。** 这与玩家日志"`EXT-*` 全 0、`RP cur=` 大量"完全自洽：机车在跑，走的是"沿用已有预留直接开"的捷径。

**关键缺陷（探针盲区，本轮新发现）**：`early-haverserved` 这条 return **一个字都不打**。全文件唯一相关日志是它上一段 `:18543 RESV-NOLOOK-PASS`，但那条被 `r3r_couple_no_lookahead` 门控 —— 恰恰是本场景（lookahead 非空）**不会**触发的条件。所以这条捷径在日志里完全隐形，第 471 轮到现在所有"extend 没跑"的困惑都源自这个盲区，不是 extend 有问题。

**⇒ 结论链闭合**：
1. 1455,1874 那 18 格**不是 extend 铺的**（§32.9 已证 extend 一次没调）；
2. 也不是本 tick 的 yapf（`early-haverserved` 直接 return，根本没调 `ChooseTrainTrack`）；
3. ⇒ 只能是**更早某个 tick** 的预留（更早的 extend 或更早的 yapf 长预留）留下来，本 tick 只是"看到已有预留就开进去"。

**⇒ 真正要查的问法变了**：不是"这次为什么拐弯"，而是"**那一整段预留是谁、在哪一 tick 铺下的，为什么铺成北侧平行线**"。而这必须先把 `early-haverserved` 变成可见的。

**落地动作（已完成，2026-11-25）**：在 `:18549` 之后、`:18551` 之前插入签名去重探针 `RESV-HAVERES`：

```18550:18576
	if (R3RDbgOn()) {
		static std::map<uint32_t, uint64_t> r3r_haveres_seen;
		const uint64_t hr_sig = ((uint64_t)(uint32_t)res_tracks << 40)
				| ((uint64_t)(uint32_t)tile.base() << 12)
				| ((uint64_t)(consist->cur_speed & 0xFF) << 2)
				| ((uint64_t)(consist->lookahead != nullptr ? 1 : 0));
		...
		R3RDbgWrite("RESV-HAVERES veh=%d tile=%d,%d enterdir=%d res=0x%X tracks=0x%X spd=%d real=%d order=%d stuckretry=%d split=%d foreign=%d nolook=%d look=%d\n", ...);
	}
```

只读、无行为变更、只改 `src\train_cmd.cpp`（增量合法，未碰 `.h`）。`read_lints` = 0 条。字段含义：`stuckretry/split/foreign/nolook` 四个豁免的实时取值 + `look`（lookahead 是否存在）——**四个全 0 且 `res != 0` ⇒ 就是它静默 early return 了**，这正是本场景的预期读数。

**复测判读（新版 exe）**：

| 读数 | 结论 |
| --- | --- |
| `RESV-HAVERES` 出现且 `tile=1455,1874` 四豁免全 0 `res` 含自己的 X 位 | 确认走 `early-haverserved`，extend 结构性不会被调用；改查"这 18 格是哪一 tick 铺的"（往前翻 `RP cur=` 首次出现 1421..1460,1873 的那一 tick） |
| `RESV-HAVERES` 出现但 `stuckretry=1` 或 `nolook=1` | 豁免放行，说明是"停住"或"无前瞻"触发；与 §24.14 的停驶分支同类 |
| `RESV-HAVERES` 完全不出现 | 说明连 `:18551` 都没走到（更上游 `:18551` 之前的 return 拦下了），需继续往上找 |
| 同时出现 `EXT-EXIT` | 说明那一 tick 确实调了 extend（与既有全 0 的旧日志矛盾 ⇒ 旧日志来自更早的 exe） |

**构建状态（2026-11-25 03:17）**：首次 `R3R_fullbuild.cmd inc detach` 被脚本闸门拦下——`build\R3R_incbuild.done` = `EXIT_CODE=97`（`build\R3R_incbuild.done` mtime 03:17），即 **`openttd.exe` 仍在运行**，链接会 LNK1168；`build\openttd.exe` 仍是 02:46 那版、`src\train_cmd.cpp` 已改（03:16）。⇒ 需玩家先关闭游戏再重跑一次增量。

**构建自证（第 2 次，2026-11-25 03:26 成功）**：玩家关闭游戏后（`tasklist` 确认无 `openttd.exe`）重跑 `R3R_fullbuild.cmd inc detach`：

- `build\R3R_incbuild.done` = `EXIT_CODE=0`
- 日志尾部 `[2/3] Building CXX object CMakeFiles\openttd_lib.dir\src\train_cmd.cpp.obj` → `[3/3] Linking CXX executable openttd.exe`，`==== R3R build ended 2026/11/25 3:26:03.89 MODE=inc EXIT_CODE=0`
- `build\openttd.exe` mtime = `2026-11-25T03:25:52` > `src\train_cmd.cpp`(03:16)
- `read_lints` = 0 条；只改 `.cpp`，护栏未升级为全量（走了增量）
- exe 字面量核对 `RESV-HAVERES` **未做**（玩家中途取消了 findstr 扫描，54 MB 较慢；该串是独立字面量、`R3RDbgWrite` 走 fmt 格式化，属可接受省略）

### 32.11 现场读数到手（2026-11-25）：**§32.10 的判读表写反了**，`nolook=1` 是「禁止吃捷径」

玩家给出 `RESV-HAVERES` 原文（`veh=15`，岔口 1455,1874）：

```
RESV-HAVERES veh=15 tile=1455,1874 enterdir=0 res=0x1 tracks=0x11 spd=160 real=1 order=16 stuckretry=0 split=0 foreign=0 nolook=1 look=0
```

同时 `RESV-NOLOOK-PASS` 也确实出现。玩家补充的关键信息：**该场景没有开启真实制动**。

**先纠错（重要）**：`nolook=1` **不是**「被豁免放行」，恰好相反——它是「**不许**吃捷径」的理由。`:18577` 的条件是

```18577:18577
if (res_tracks != TRACK_BIT_NONE && !r3r_couple_stuck_retry && !r3r_couple_split_res && !r3r_couple_foreign_res && !r3r_couple_no_lookahead) return r3r_chosen_ret(FindFirstTrack(res_tracks), result_flags, "early-haverserved");
```

`!r3r_couple_no_lookahead` 为假 ⇒ 整条 `if` 为假 ⇒ **不吃 `early-haverserved`**。`RESV-NOLOOK-PASS`（`:18543`）只是同一条件上的**纯探针**，它本身不 return。§32.10 判读表里写的「`nolook=1` ⇒ 没有前瞻状态才放过的」是错的，本行已订正；真正放行发生在它**下面**的某个出口。

**关键结构性事实（这次的新证据）**：`lookahead` 只在 `train_braking_model == TBM_REALISTIC` 下才会被创建。全文件 `FillTrainReservationLookAhead()` 的每一个调用点都被这一前提门控——`:14567`、`:18769`、`:19305`、`:20259`、`:20542`、`:20624`、`:21755`、`:21868` 一带逐条可见；另有 `Train::UsingRealisticBraking()`（`train.h:400`）= `tcache.cached_tflags & TCF_RL_BRAKING`，`GetCurrentMaxSpeedInfoInternal()`（`:1865`）同样以 `AccelerationModel::Realistic` 为前提。

⇒ **关闭真实制动时 `lookahead` 结构性恒为 `nullptr`**，于是：

- `look == 0` 恒成立（不是偶发，是必然）；
- `r3r_couple_no_lookahead`（`:18533`：`OT_GOTO_COUPLE && !couple_is_depot && lookahead == nullptr`）**恒为真**；
- 第 338 轮 KI-359 修法 P1 那条「没有前瞻就不许吃捷径，继续走 extend+yapf 取 `CTTRF_RESERVATION_MADE`」的保护，在关闭真实制动的存档里**退化成「永远不许吃捷径」**——设计意图（异常态保守）与实际效果（常态全量重算）方向相反。

**由此产生新疑点 REG-505-32-D（必须回答）**：既然本 tick 明确不吃捷径、应当继续往下走，为什么 `EXT-*` 仍然全 0？

`tracks=0x11`（TRACK_X|TRACK_LEFT）两位 ⇒ `KillFirstBit(tracks) != TRACK_BIT_NONE` ⇒ 不进 `:18582` 的单轨快返回，只能继续到 yapf 与 `:18659 if (do_track_reservation)`。而 `ExtendTrainReservation()` 只在 `do_track_reservation` 为真时调用 ⇒ 三个候选：

| 候选 | 机制 | 判别 |
| --- | --- | --- |
| P1 `do_track_reservation == false` | 这次调用只是「看看走哪条轨」，yapf 不订路、不建前瞻 | 日志应有 yapf 的 `RP cur=` 但**没有** `CTTRF_RESERVATION_MADE` 相关输出；且每 tick 重复 |
| P2 yapf 每次重算且都选北侧平行线 | 关闭真实制动导致每 tick 重算，但 yapf 判到达/终点选错（KI-163/KI-346 家族） | `RP cur=` 在 1421..1460,1873 上持续、逐 tick 重复同一串 |
| P3 `do_track_reservation == true` 但 extend 内部静默 | 与 §32.9 的五条去路表冲突（都该留痕） | 基本可排除 |

**待判读**：把 `RP cur=` 在岔口前后的原文摘几行（尤其 `cur=1455,1874` 之后紧跟的是 `1455,1873` 还是 `1456,1874`），再对 `RESV-HAVERES` 下一行的时间序——凡 `RP cur=` 覆盖到 1873 线，P1/P2 即可分开。

**修法候选（未落地，需玩家拍板）**：

- **A（最小、我认为最正确）**：把 `r3r_couple_no_lookahead` 加上「仅真实制动下生效」的前提，即 `train_braking_model == TBM_REALISTIC && lookahead == nullptr`。理由：真实制动关闭时 `lookahead == nullptr` 是**正常态**而非异常态，用它当"不许吃捷径"的判据等于永久禁用捷径。
- **B**：保留现状（关闭真实制动就每 tick 重算），但必须同时修 `do_track_reservation` 为真，否则重算不落地（P1 情形下等于空转）。
- **C**：给「挂接订单 + 无前瞻」补一条独立的、与 brake model 无关的锚：不再依赖 lookahead 是否存在，而依赖「本车预留是否覆盖下一路口」的自证。

**结论一句话**：玩家的猜测方向对了一半——`nolook` 确实是这次的关键变量，但它不是"放过了"，而是"被禁止走捷径"；而 `nolook` 恒真的根因是**真实制动未开启时 lookahead 结构性不存在**。剩下要钉的只有一件事：`do_track_reservation` 在这次调用里是否为真。

### 32.12 二次纠错（2026-11-25）：**§32.9「extend 未被调用」判据失效，结论撤回**

顺着 §32.11 的 `do_track_reservation` 往下查，三处硬事实推翻了 §32.9：

**(1) `EXTEND-BACKOUT` / `EXT-ENTRY` 这两个探针在当前源码里根本不存在。** 全文件 `EXT-` 命名只剩三处，全是 `EXT-EXIT`：

```
17422: R3RDbgWrite("EXT-EXIT veh=%u kind=choose origin=...
17470: R3RDbgWrite("EXT-EXIT veh=%u kind=safe origin=...
17518: R3RDbgWrite("EXT-EXIT veh=%u kind=eol origin=...
```

⇒ 「`EXTEND-BACKOUT` 0 条」「`EXT-ENTRY` 0 条」是**探针已被删除后的空集**，不是"没发生"。它们不能作为任何证据。

**(2) 唯一还有意义的 `EXT-EXIT`，本身被边沿闸门节流。** 第 471 轮加这三处探针时就写明它们包在 `if (R3RDbgOn() && R3RDbgEdge(...))` 里（见记忆 20887596 / 第 471 轮续条目）。`R3RDbgEdge` 是"每状态每 128 帧窗口最多一行"的抑制器 ⇒ 0 条既可能是"没调"，也可能是"被抑制"。

**(3) `do_track_reservation` 的真实来源决定了 extend 应当被调用。** `:18319` 是唯一初始化：

```18319:18319
bool do_track_reservation = _settings_game.pf.reserve_paths || (flags & CTTF_FORCE_RES);
```

而 `TrainController` 的主入口 `:20601` 恒带 `CTTF_FORCE_RES`：

```20601:20601
ChooseTrainTrackResult result = ChooseTrainTrack(consist, new_tile, exitdir, reachable, CTTF_FORCE_RES | (mark_as_stuck ? CTTF_MARK_STUCK : CTTF_NONE));
```

⇒ 只要走的是 TrainController 这条路，`do_track_reservation` **恒为真** ⇒ `:18684 if (do_track_reservation)` 成立 ⇒ `ExtendTrainReservation()` **会被调用**。

而本场景 `tracks=0x11`（两位）⇒ 不进 `:18582` 的单轨快返回块（那块里的 `:18601 single-restrict-wait` / `:18658 single-track-nores` 两个静默出口都用不上）⇒ 出口只剩 `:18577`（已排除，`nolook=1`）与 `:18684`。

**⇒ §32.9 的结论必须撤回**：`ExtendTrainReservation()` **极可能一直被调用**，只是 `EXT-EXIT` 被 `R3RDbgEdge` 抑制了。§32.9 据此推出的「那 18 格不是 extend 铺的」「改查 yapf `rp()` 候选 C-2」这两条，都建立在失效证据上，一并降级为待验证。

**⇒ 现在真正一致的读法（新主线）**：

1. 关闭真实制动 ⇒ `lookahead` 结构性恒空 ⇒ `nolook=1` 恒真 ⇒ **每 tick 都不吃 `early-haverserved`**（第 338 轮 KI-359 的保护在这里退化成"永久禁用捷径"）；
2. 每 tick 都走到 `:18684` 调 `ExtendTrainReservation()`，一直跟随/延伸预留；
3. ⇒ **那 18 格北侧平行线预留，是每 tick 重算的 extend 结果本身**，而不是陈年残留；
4. ⇒ 问题性质回到"**extend 跟随的目标/方向本身错了**"，而不是"谁留下了旧预留"。

**这与记忆 72821122 完全同族**：那份记录的是同一个 `veh=15`、同一个挂接寻路（`orderType=16`、`CCouplePf`），现象是"YAPF 终点被甩到与真实目标平行的另一条站台/到发线"（当时是 1424,1875 / 1424,1876；这次是 1421..1460,1873）。**同一个现场、同一类错位，只是年份不同。**

**⇒ 下一步取证（下一轮，不占用本轮配额）**：读 `ExtendTrainReservation()` 的跟随循环（`:17300-17560` 一带），重点看两件事——① `target_seen` / `FindFirstTrackdir` 的选轨规则在"目标线被占满 + 平行线同站台"时凭什么判到达；② `KillFirstBit` 选 `choice` 时是否只用 bit 序而不看"该轨是否通向 dest"。而现场数据侧只需玩家再给一组：`CPL-BEST`、`CPL-TRACE-RESULT`、`RP cur=` 在 `1455,1874` 前后的原文各 3~5 行。

**已确定的答案（可以直接告诉玩家）**：这次的关键变量确实是 `nolook`，但它**不是把车"放过"了**，而是让车**再也吃不到"沿用已有预留直接开"的捷径**；而它恒真的根因是**真实制动没开时，引擎根本不创建前瞻状态（`lookahead` 结构性恒空）**。所以这条现场不是"懒人捷径 + 陈旧预留"，相反是"每 tick 全量重算 + 重算结果一直拐向北侧平行线"。

### 32.13 对照实验（2026-11-25 玩家执行）：开真实制动**现象完全不变** ⇒ 上面 §32.11/§32.12 的 nolook 主线作为根因**全部证伪**

玩家原话要点：把这个存档的**真实制动打开**、重跑，**预留现象不变**（仍拐向北侧平行线），同时读数变为：

```
RESV-HAVERES veh=15 tile=1455,1874 enterdir=0 res=0x1 tracks=0x11 spd=160 real=1 order=16 stuckretry=0 split=0 foreign=0 nolook=0 look=1
```

**判定一：两种配置下都命中了 `early-haverserved` 捷径。** `:18577` 条件 `res != NONE && !stuckretry && !split && !foreign && !nolook`：

| 配置 | stuckretry | split | foreign | nolook | 条件 | 出口 |
| --- | --- | --- | --- | --- | --- | --- |
| 关真实制动 | 0 | 0 | 0 | 1 | 假 | 继续往下 |
| 开真实制动 | 0 | 0 | 0 | **0** | **真** | **`return FindFirstTrack(res_tracks)` = `TRACK_X`** |

`res=0x1` 只含 `TRACK_BIT_X` ⇒ `FindFirstTrack` = `TRACK_X` ⇒ **这次调用车沿 X 直行，不拐弯、不调 extend、也不进 yapf 下半段**。

**判定二（决定性）：`lookahead` 有无对现象没有影响。** ⇒ 把 `nolook` 当根因这条主线**证伪**。`nolook`/`look` 只是两次运行的表象差异（关真实制动时前瞻状态结构性缺失、开时存在），与"预留拐弯"没有因果关系。

**判定三：那 18 格不可能由本次 `ChooseTrainTrack` 调用铺下。** 开真实制动那次已确定吃捷径、直接 return，连 `:18684` 都没到；关真实制动那次即便走到 extend，也无法解释"现象逐字相同"。⇒ 18 格的铺主在**另一个调用者**。

**判定四（收敛到唯一候选）：`RP cur=` 只能来自挂接寻路。** 本 tick 吃捷径 ⇒ 不进 `ChooseTrainTrack` 下半段 ⇒ 常规 `YapfTrainChooseTrack` 的 `rp()` 不会跑。剩下的 `rp()` 调用者只有**挂接寻路**（`DoTrainCouplePathfind` → `YapfTrainCoupleTrack` → `CCouplePf`），由 `TrainCoupleHandler` 在 `cur_speed>0` 时每 tick 触发。

**⇒ 新主线（方向与上一轮相反，但证据更硬）**：18 格北侧平行线（1421..1460,1873）预留是**挂接寻路 `rp()` 铺的**，与 `ExtendTrainReservation()`、与 PBS lookahead、与 §32.9 的 `do_track_reservation` **全都无关**。

这与记忆 72821122 **同族、同一台车、同一场景**：那次记的正是 `veh=15`、`orderType=16`、`CCouplePf`，`CPL-ENDTILE-PAIR pHead=1424,1874 pTail=1437,1874 end=1438,1874`，而预留铺在 `1424,1875` / `1424,1876`。当时写的初步判断逐字成立：**1874 线 1424..1437 被车底占满后，站台判据在平行轨上同样成立**，于是 yapf 把"到达点"甩到平行线、并把预留铺在那里。这次是 1873 线，机制相同。

**待验证（下一轮取证，不占用本轮配额）**：

1. `yapf_destrail.hpp` 的 `PfDetectDestination`：判"到达目标车底"是否只依赖"站台格 + 兼容轨型"，不校验**是否同一条到发线**（即 y 坐标 / 平台连续性）；
2. `yapf_rail.cpp` 的 `rp()`：铺预留时用的是"终点格"还是"真实到达格"，是否因此铺到平行线；
3. 记忆 72821122 的前提"1874 线被占满"在此次是否成立（需 `CPL-PAIR` / `CPL-ENDTILE-PAIR` 原文）。

**现场侧请求（玩家提供后可直接收口）**：`CPL-BEST veh=15`、`CPL-PAIR veh=15`、`CPL-ENDTILE-PAIR veh=15` 各 1 行，以及 `RP cur=` 在 `cur=1455,1874` 之后紧跟的 3 行。

---

### 32.14 现场日志逐行对账（2026-11-25 03:49）：**1873 那三格的真身是 veh=17，不是 veh=15**；S2b 同排约束实测生效

**版本自证（先排除"老 exe"这一可能）**

| 项 | 读数 |
| --- | --- |
| `build\openttd.exe` | 2026-11-25 03:25 |
| `build\R3R_debug.log` | 2026-11-25 03:49，677 935 B |
| 日志内 S2b/S4 探针 | `CPL-SCAN`、`PFD-STATIC`、`CPL-HEAD-STATIC` **均有输出** |

⇒ 该 exe 已含第 501/502 轮的"静态终点快照 + 同排约束"，**不是**记忆 72821122 那一轮的老版本。

**事实 1（§32.13 待验证第 1 条被实测否定）：挂接寻路的到达点钉在 1874 排，没甩到平行线**

```
PFD-STATIC kind=car-only tile=1424,1874 t=18 row=1874 stn=0
CPL-SCAN ... rej=0/0/0/0/19 acc=5/0 ... last=1424,1874 lastveh=18 why=acc-car
```

`row=1874` 就是 S2b 的同排约束；`last=1424,1874` = 车底 18 的车头格。**当前 exe 上不存在"站台判据在平行轨同样成立 ⇒ 到达点甩到 1873/1875"**。

**事实 2（veh=15 的预留逐格无 1873）**

```
CPL-RES-OK veh=15 total=23 booked=23 stop_safe=1 goal=1
  tiles=1459,1875/0;1458,1875/0;1457,1875/4;1457,1874/5;1456,1874/0;1455,1874/0;1454,1874/0;...
CPL-RES-OK veh=15 total=12 ... tiles=1449,1874/0;...;1439,1874/0;1424,1874/0
CPL-RES-OK veh=15 total=4  ... tiles=1441,1874/0;1440,1874/0;1439,1874/0;1424,1874/0
```

前 3 格在 **1875 排**（veh=15 自己车尾斜跨道岔所在），其余全在 **1874 排**；全日志 `RP cur=` **无一行落在 1873**。
`total=12` / `total=4` 那两次出现 `1439,1874 → 1424,1874` 的跳格，正是车底 18 占据的 1425..1438 被正确跳过。

**事实 3（决定性）：玩家报的"1421/1420/1419,1873"是 veh=17 的预留，且会重生**

```
2902:RESV-GHOST couple veh=17 tile=1419,1873 track=0
2903:RESV-GHOST couple veh=17 tile=1420,1873 track=0
2904:RESV-GHOST couple veh=17 tile=1421,1873 track=5
2905:RESV-GHOST couple veh=17 tile=1421,1874 track=4
2906:RESV-GHOST couple veh=17 tile=1422,1874 track=0
2907:RESV-GHOST couple veh=17 tile=1423,1874 track=0
2908:RESV-GHOST-SUMMARY couple veh=17 cand=6 freed=6
```

同一组 6 格在 **5855-5862 逐字重复一次**。几何是**发夹形**：
`1421,1873 →(西) 1420,1873 → 1419,1873`，再经道岔 `1421,1874`，再 `1422,1874 → 1423,1874`（东）。

而 veh=17 当时的行进目标是**向东**：
`RESV-TERM veh=17 real=8 type=6 tile=1451,1875 td=4 ... vtile=1438,1874`
`CPLRES veh=17 tick=43026 origin=1452,1875 destTile=1453,1875 vTile=1438,1874 vDest=1453,1875 gap=14 oSrc=other`

⇒ veh=17 的车头在 1438,1874、车尾在 1452,1875，要往 1453,1875 走，却有一段预留被铺到**反方向**的 1419..1421,1873。
R3R 在挂接提交点（`R3RReleaseChainReservations`，第 125/126 轮修法）把这 6 格判为孤儿并释放 ⇒ **"清掉→再寻路→又订回来"，这才是玩家说的"现象不变"**，与真实制动开关无关。

**事实 4（终局归因）**

```
COUPLE-OK   loco=17 rear=18 consist=18  (×2)
COUPLE-FAIL loco=15 order=16 tx=1474 ty=1884  (持续 N 次)
```

`COUPLE-FAIL` 的 `tx/ty` 打的是 `v->tile`（自己所在格，见记忆 42642809），不是目标。⇒ **目标车底 18 被 veh=17 挂走了**，veh=15 是结构性失败，与它的预留路径无关。

**判定**

1. §32.13 的 lookahead/nolook 主线：**彻底证伪，不再复用**（本轮不必重测制动开关）。
2. §32.13 待验证第 1 条（`PfDetectDestination` 缺同排校验 ⇒ 到达点甩平行线）：**在当前 exe 上被实测否定**，S2b 已生效，本条可关闭。
3. 待验证第 2/3 条（`rp()` 用终点格还是真实到达格；1874 线被占满的前提）**无需再做**——挂接寻路这次铺的 23 格与目标格完全自洽。
4. **新主线（唯一活口）**：`veh=17` 的**常规**寻路（非挂接寻路）把预留铺成"西行 1873 → 道岔 1421,1874 → 东行 1874"的发夹形，与它自身目标方向相反；孤儿化由提交点清扫兜底，所以症状表现为"黑色预览反复出现又消失"。

**待验证（下一轮，按配额取数）**

- V-1：`veh=17` 的 `CPL-TRACE` / `CPL-TRACE-RESULT` / `RP cur=` 序列（`origin≈1452,1875` 一次），看它为何选 1419..1421,1873 这条反向支路；
- V-2：这 6 格是否属 KI-346（`rp()` 平台段 `plat_track=AxisToTrack(axis)`）同族——`1421,1873 track=5` 是双槽格，与 KI-346 的"双槽格订错轨"形态一致，**高度可疑**；
- V-3：请玩家确认画面上看到的"预留"具体是哪一格/哪一段（1873 三格 vs 别的格），以排除观感误判。

**本轮零源码改动、零构建。**

---

## 32.15 V-2 结案（2026-11-25）：`1421,1873 track=5` **不是** KI-346 订错轨，该格是 LEFT|RIGHT 双轨格

### 32.15.1 探针字段口径（先纠正误读）

- `RESV-HAVERES` 的两个 16 进制字段都不是格上轨道：`res=` 是 `GetReservedTrackbits(tile) & DiagdirReachesTracks(enterdir)`（`train_cmd.cpp:18410` 一带），`tracks=` 是 `ChooseTrainTrack()` 传入的**可进入轨**参数（`:18565` 打印）。⇒ 不能由 `tracks=0x20` 反推"格上只有一根轨"，§32.14 V-2 的"双槽格"前提**未被该行证实**（也就不能被它证伪）。
- `RESV-GHOST` 的 `track=` 打的是**被释放的那一位**：`track=5`=`TRACK_RIGHT`、`track=4`=`TRACK_LEFT`、`track=0`=`TRACK_X`（`track_type.h:19-29`）。`TRACK_BIT_VERT = TRACK_BIT_LEFT | TRACK_BIT_RIGHT = 0x30` 是**合法不重叠**组合（`track_type.h:52`）⇒ 同一格 LEFT 与 RIGHT 可以合法共存。

### 32.15.2 结构推导（不依赖巧合）

`TryReserveTrack()` 是 `res |= bits`（`src\rail_map.h:270-275`），`UnreserveTrack()` 是 `res &= ~bits`（`:288-289`），`SetTrackReservation()` 的 `CHG-RAIL` 打印 `FindFirstTrack(new_bits)`（`:252-253`）。于是两条 `CHG-RAIL` 只能这样解释：

| 日志行 | old → new | `FindFirstTrack(new)` | 含义 |
|---|---|---|---|
| `:5564 CHG-RAIL tile=1421,1873 track=4 phase=yapf-resv actor=75 head=75` | `0x20(RIGHT)` → `0x30(LEFT\|RIGHT)` | 4 = LEFT ✔ | `veh=75` 只是**加订** LEFT，没有覆盖 ghost |
| `:5857 CHG-RAIL tile=1421,1873 track=4 phase=release-ghost actor=17`（紧跟 `:5858 RESV-GHOST ... track=5`） | `0x30` → `0x10(LEFT)` | 4 = LEFT ✔ | ghost 摘掉的正是 RIGHT |

⇒ 两条 `CHG-RAIL` **全部自洽，无需"订错轨"假设**。**V-2 结论：否定**——`1421,1873` 不是 KI-346（`rp()` 平台段 `plat_track=AxisToTrack(axis)` 订错轨）同族；它是道岔双轨格，两个 actor 各订一根轨。

### 32.15.3 新发现：ghost 的**第二条**危害通道（比"黑色预览"严重）

- `RESV-HAVERES veh=75 tile=1421,1873 / 1420,1873 / 1419,1873`（`:2116` 一带，同组在 `:4868/:4897/:4942` 再现）⇒ 一列**普通** `OT_GOTO_WAYPOINT`（`order=6`, `spd=80`）列车在 ghost 的三格上全部命中 `RESV-HAVERES`。
- 三条 R3R 归属豁免 `stuckretry / split / foreign` 只覆盖 `OT_GOTO_COUPLE` 域（`train_cmd.cpp:18437/18446/18480`），`nolook`（`:18533`）也只对 `OT_GOTO_COUPLE` 成立 ⇒ `veh=75` 这行五个标志 **全 0** ⇒ 命中 `early-haverserved`（`:18577`）**直接 `return FindFirstTrack(res_tracks)`**，即把**别人的（`veh=17` 的 ghost）预留当成本车预留**，取的是 ghost 的 `RIGHT`。
- 直接损失凭证：`:5565 RPFAIL veh=75 tick=42954 origin=1404,1870 tile=1424,1873 unsafe=0 nTry=5 nOK=4`、`:5566 CPLRES veh=75 ... nTry=23 nOK=22` ⇒ `veh=75` 的常规预留**因 ghost 少订一格（22/23）**。
- ⇒ 修正 §32.14 的定性：ghost 不只是"黑色预览 + 提交点清掉"，它还是**一个能卡住别的列车的真实占位者**，并且会让普通订单列车跳过自己的寻路。

### 32.15.4 仍未做 / 新增待验

- V-1（`veh=17` 为何订这条反向发夹）**仍未做**；本轮把 V-2 关掉后，活口只剩它。
- 新增 V-4：`veh=75` 是否真的把车头压在 ghost 的 `RIGHT` 轨上（需 `RP cur=`/`TPR-WALK` 在 `1421,1873` 的 td）；
- 新增 V-5：ghost 是否导致 `veh=75`/`veh=15` 停车（`RPFAIL nOK=22/23` 的后果）。

**V-2 取证所用日志：`build\R3R_debug.log`（677 935 B，`build\openttd.exe` @03:25）:5540-5569、`:2116` 一带、`:5855-5862`。本轮零源码改动、零构建。**

### 32.15.5 取证陷阱（新增登记，勿再犯）

- **`RESV-WATCH` 只覆盖"关注格"**：`R3RResvWatchLog()`（`src\pbs.cpp:249-275`）先判 `R3RIsWatchedResvTile(tile)`，非关注格还要再看 phase 是否 `"misc"`。关注表 = 内建 `TileXY(60,16)`、`TileXY(57,49)` + 工作目录 `r3r_resv_watch.txt`（`pbs.cpp:65-98`）。实测全日志 `RESV-WATCH`+`SET-RAIL` 合计只有 **10 行**（`_c_watch.txt`）⇒ **"日志里没有 `SET-RAIL tile=X`"绝不能推出"该格没被订过"**。同理 `UNRES-RAIL` 也只在关注格逐行打、其余按 phase 聚合。
- **由此产生一个疑点（`REG-506-A`，待验，不下定性）**：`1421,1873` **在**关注表内（`:5564`/`:5857` 都能打 `RESV-WATCH`），但全日志没有它的 `SET-RAIL`。期望=若 ghost 由本会话某条相位非 `misc` 的路径订下，应留一行 `SET-RAIL`；实测=0 行。两种解释未区分：①ghost 是**随存档带入的存量**（读档即存在，本次会话从未"从 NONE 订上去"）；②该次写入发生于日志开始前或 `R3RDbgOn()` 打开前。判别切口=下一轮读档后**第一帧**先查该格 `reserved=`（`RESV-AUDIT` 或 `RESV-HAVERES`），非 0 即选①。
- **`CHG-*` 只打新 bits 的第一根轨**（`pbs.cpp:246-247` 注释自陈）⇒ 同一条 `CHG-RAIL` 在不同 old/new 组合下可能打出相同 `track=`，必须按 §32.15.2 的 old→new 结构推导还原，不能只看数字。

## 32.16 玩家假设成立（2026-11-25）：1421/1420/1419,1873 **不是孤儿预留**，是「解挂后两条链同格/同线叠加」的产物

**玩家原话要点**：「这三格的预留绝非孤儿预留。我猜测，让你判定它为孤儿预留的原因是 veh75 刚从 veh18 上面解挂下来。此时，一条黑线预留上面是站着两条链的，又是我们没有考虑过的情况，所以自然地出现了预留紊乱。」

**判定：假设成立，且日志逐环节闭环。** 上一轮把它记成 `RESV-GHOST`（孤儿/无主）是**归属判定的读数**，不是事实；这三格确有活的归属者。

### 32.16.1 四条实锤（`build\R3R_debug.log`，同一会话）

| # | 锚点 | 读数 | 说明 |
|---|---|---|---|
| 1 | `:1762` | `DECOUPLE-FIRE consist=75 tx=1424 ty=1874 real=4 mode=0 num=0 segs=1 eff=18 plat=57/0` | **75 从 18 上解挂**（eff=18 是解出方链头） |
| 2 | `:1767` | `TDTRY ok=1 v=75 u=18 ... v_head=75 u_head=18 borrowed=1 u_eng=0 v_eng=1 u_next=19` | 75=机车（v_eng=1），18=车底链头（u_eng=0，19 跟随） |
| 3 | `:1924`/`:1925` | `R3R-RES-ONCE grant head=75 side=v ... tile=1424,1874` / `head=18 side=u ... tile=1424,1874` | **同一格两条链各拿一次 grant** ← 玩家说的"一条预留上站两条链" |
| 4 | `:5564` | `RESV-WATCH CHG-RAIL tile=1421,1873 track=4 phase=yapf-resv actor=75 head=75` | **75 亲口在这格上加订 LEFT**（track=4）⇒ 该格有两笔 |

补充：`:1926 CPL-PAIRUNLOCK tick=41317 why=decouple act=75 actTile=1424,1874 tgt=18 tgtTile=1424,1874 tgtLen=216` —— 解挂瞬间两条链**物理同格**，且 18 链长 216px（`:5853 tgtLen=216`）覆盖 1424..1429,1874 一带。

### 32.16.2 精确形态：不是"一格一笔被抢"，是"一格两根轨各归一条链"

- `:5564` 的 `CHG-RAIL track=4` 按 §32.15.2 的 old→new 推导 = `0x20(RIGHT)` → `0x30(LEFT|RIGHT)`，即 **75 加订 LEFT，没碰原有的 RIGHT**；
- `:5855-5858` 的 `RESV-GHOST couple veh=17 ... 1421,1873 track=5`，`track=5 = TRACK_RIGHT` ⇒ 清扫摘掉的正是 **17 那一根 RIGHT**，75 的 LEFT 留下（`old 0x30 → new 0x10`，与 §32.15 推导一致）。

⇒ 所以 §32.14「1873 三格真身是 17」只对**一半**：**RIGHT 归 17，LEFT 归 75**。上一轮把它整体当 ghost，是把"两根轨两笔账"压成了"一格一笔"。

### 32.16.3 由此定性 `REG-506-A`（原"存档带入 vs 日志前写入"二选一，两都不是）

三格之所以**全程没有 `SET-RAIL`**，第三种解释成立且唯一自洽：**这两笔预留都不是本会话从 `NONE` 订上去的**——`0x20(RIGHT)` 是解挂前那条长链（75+18 一体时的出站路线）铺下的、解挂后随链拆分被记到 17 名下；`0x10(LEFT)` 是 75 在已有 `0x20` 上做的 `|=` 加订（`:5564`），因 `old != 0` 只打 `CHG-RAIL`。⇒ `SET-RAIL`（old==0 才打）自然一行都没有。**不需要再等"下一轮读档第一帧"去判存档带入。**

### 32.16.4 "预留紊乱"的实际后果（已取到凭证）

- `:2116 RESV-HAVERES veh=75 tile=1421,1873 enterdir=3 res=0x20 tracks=0x20 spd=80 real=2 order=6 stuckretry=0 split=0 foreign=0 nolook=0` —— 75 是**普通 GOTO_WAYPOINT**（order=6）列车，它把这格要走的 RIGHT 判成"自己的"（`foreign=0`），于是走 `early-haverserved` 不 extend；
- 紧接着 `:5565 RPFAIL veh=75 ... tile=1424,1873 ... nTry=5 nOK=4`、`:5566 CPLRES ... nTry=23 nOK=22`、`:5567 ANSWER-STORE veh=75 n=22` —— **23 格只落 22**，少的那一格就是它"以为已订好、其实属于 17"的那格；
- 时相背景：`:5549 REVERSEDIR veh=75 tile=1402,1870 dir=1 order=6 spd=0` → `:5553 REVERSEDONE db=1 mvfront=77` → `:5557 RESV-BEGIN ... spd=0 real=3 order=6` → `:5558 R3R-RES-ONCE grant head=75 why=turn` ⇒ 75 在**掉头（turn=1）**后重新铺路，恰好要横穿 17 的既有预留。

### 32.16.5 新疑点（未定性，下一轮先查这一条）

**`REG-506-B`（待验，代码层）**：`RESV-HAVERES` 的 `foreign` 判定在 `order != OT_GOTO_COUPLE` 时是否恒为 0。

- 期望：普通列车遇到"别人订的轨"应 `foreign=1` → 不进 `early-haverserved`、照常跑 yapf/报阻塞；
- 实测：`:2116` 里 75 是 `order=6`(GOTO_WAYPOINT)，该格 RIGHT 属 17，却打出 `foreign=0`；
- 若期望成立，则"紊乱"的真正入口是**归属判定对非挂接订单放行**，而非预留本身写错。取证点=`src\train_cmd.cpp` 的 `R3RResvForeign()` 及 `:18437/18446/18480/18533` 四个豁免标志的门禁条件（上一轮已记"四个标志只在 OT_GOTO_COUPLE 域内成立"，本轮需要确认**不成立时**返回 0 还是 1）。

### 32.16.6 对 V-1 的改写（提问方式变了）

原 V-1 =「veh=17 为何把预留铺成西行 1873 → 道岔 1421,1874 → 东行 1874 的反向发夹」。按本轮结论，该问法要拆成两问：

- **V-1a**：17 那笔 RIGHT（`:5855-5858` 被清掉的 6 格）是**解挂前**哪条链、哪个相位铺的？（候选：75+18 一体的出站路线，`:1762` 前的 `RESV-BEGIN`）
- **V-1b**：75 掉头后（`:5558 why=turn`）为何把路线走到 17 的 RIGHT 上——是几何唯一（1874 线被 18 链占满 1424..1429），还是 yapf 把 17 的预留当空位？

**本轮零源码改动、零构建。**

## 32.17 玩家控制实验（A/B 对照）：**三格预留与"紊乱"无因果关系**，"孤儿"定性彻底关闭，真问题回到「挂接寻路路径本身」

### 32.17.1 玩家报告的实验设计（原话要点）

- 观测基线：调机 `veh=75` 刚从车底 `veh=18` 解挂，**立刻往 x 减小方向脱离站台**；此时机车（`veh=15/16/17` 那条链）从 x 增大方向开来，准备去挂车底。
- 玩家的**假设**（未发生）：若机车的预留铺到车底上、与车底的预留连成一条，则**一条预留上会站三条链**（75 的 + 18 的 + 15 的），此时主人判定必然紊乱。
- 玩家的**控制实验**：同一场景，先让调机与车底**完全分离**，使两者预留**不相接**；再让机车执行挂接命令。
- 实验结果两条：
  1. **机车这次没有出现"预留铺到道岔终止"**（即无 `RPFAIL nTry>nOK`、无中途截断），预留**顺畅一次性铺到与车底的预留相接**；
  2. **机车预留仍然超出站台**，且**仍然包含 `1419/1420/1421,1873` 那三格**。

### 32.17.2 判定（本轮最重要的两条）

**判定 1（关闭）：三格不是"孤儿预留"，也不是"紊乱产物"。** 排除紊乱后仍然出现 ⇒ 三格是**挂接寻路正常路径的一部分**（或正常铺设的输出）。§32.15 的"孤儿"定性、§32.16 的"紊乱导致三格"归因，**一并撤销**。§32.16 里"RIGHT 归 17、LEFT 归 75"仍是事实记录，但它解释的是**记账归属**，不是三格出现的**原因**。

**判定 2（保留但降级）：紊乱确实存在，但它只造成"铺设被截断"这个次要症状。** 对照关系明确：
- 有紊乱（三链/两链叠在同一段线上）⇒ `:5565 RPFAIL nTry=5 nOK=4`、`:5566 CPLRES nTry=23 nOK=22`、`:5567 ANSWER-STORE n=22` ⇒ **少订一格、路径中途告败**；
- 无紊乱（玩家实验）⇒ 一次性 `nOK == nTry`、铺到相接。

⇒ KI-683 的机理（`early-haverserved` 把别人的预留当自己的、`foreign=0`）**仍成立**，但它是**症状放大器**，不是三格的成因。

### 32.17.3 真问题（新主攻方向）：挂接寻路的预留**绕过站台、绕进并行线的三格**

这是本 issue 最早那条线索的回归（见记忆 ID 72821122 的同族现场：`veh=15` 挂接时 YAPF 终点被甩到与真实目标平行的 1875/1876 线）。现在被玩家实验**剥离掉紊乱干扰**后重新确认为独立缺陷：

- **现象**：机车从 x 增大方向开来、要挂 x 减小方向的车底，预留却先往 x 更小的 `1419..1421,1873` 走，再折回——形态就是"反向发夹"，且这段在**站台范围之外**。
- **不依赖**：不依赖幽灵、不依赖双链叠加、不依赖解挂残留。
- **待判**：是 ①1874 线被车底链占满（`tgtLen=216`，1424..1429 一带密集）导致 yapf 认为东侧无位、被迫从并行线绕行；还是 ②挂接寻路的"到达判据"在并行线上同样成立（与记忆 72821122 的"平行轨判到达"同族）；还是 ③站台跳段（`tiles_skipped`）导致的路径穿线。

### 32.17.4 待玩家提供 / 待验（编号，答完直接回填）

- **C-1（必需）**：这次"干净实验"的日志（`build\R3R_debug.log`）能否给我？它是**唯一无紊乱的对照样本**，能一次性回答"那三格这次是谁订的、走的是哪根轨、`nTry/nOK` 各是多少、有没有 `RESV-GHOST`"。
- **C-2（口径）**："超出站台"具体指——(a) 预留越过站台末端沿本线继续延伸，还是 (b) 预留跑到**并行的邻线**（1873 线）上？两者修法完全不同。
- **C-3（可选）**：1873 线是什么（并行到发线 / 另一座站台 / 侧线）？这决定"绕行"是否本来就是合法拓扑。

### 32.17.5 与既有条目的关系

- 记忆 `72821122`（veh=15 挂接终点甩平行线 1875/1876）：**同族，应合并处理**，本条是它的"剥离紊乱后的复现"。
- 记忆 `96481259`（KI-346，`rp()` 把进入格 Track 写成 `TrackdirToTrack(cur_td)`）：**仍是最可疑的代码落点**——它正好能造出"订了一根车不走的轨"，而玩家"预留超出站台/绕进并行线"的观感与该形态吻合。**建议下一轮优先复核该修是否真的覆盖了挂接寻路（`CCouplePf`）这条路径**，而非只覆盖常规 `rp()`。

**本轮零源码改动、零构建、未取新日志。**

## 32.18 玩家答复 C1/C2/C3（2026-11-25）：形态改写为「**预留越过站台末端、被轨型带进 1873 机走线**」

### 32.18.1 三答原文要点

- **C-1**：日志存在，但本次实验的日志是**追加**到既有 `build\R3R_debug.log` 尾部的（⇒ 取证方式 = 读文件尾部新段，不能拿整份日志的头部当本场景）。
- **C-2**：「超出站台」= **预留越过站台末端、顺着本线继续延伸**（**不是**跑到平行的邻线/邻站台）。
- **C-3**：`1873` 线是**无站台的机走线**（loco running line）；玩家补充：「假如预留要超出站台，根据轨型预留最终会上到 1873 去」。

### 32.18.2 判定改写（撤销 §32.17.3 的两种猜测）

- **撤销**：「yapf 把终点甩到平行站台轨 1875/1876」（§32.17.3 的 ②）——玩家明确否定了"平行线"口径。
- **撤销**：「反向发夹 / 反向支路」这一几何描述——B/西端并不是"折回"，而是**本线继续向前延伸**：`1874` 站台线过了站台末端，轨型把它接到 `1873` 机走线。
- **成立形态**：机车的挂接预留**没有在站台末端（或车底端）停住**，而是沿本线越过站台末端继续铺，被道岔/轨型带到无站台的 `1873` 机走线上，于是：
  - 「超出站台」= 预留长度越界（C-2 口径）；
  - 「包含 1419/1420/1421,1873 三格」= 那段越界部分**在机走线上**（C-3 口径）。
- ⇒ §32.17.2 的判定 1（三格与紊乱无因果关系、是挂接寻路正常输出）**不变**；变的只是"它为什么在那里"——不是平行线误判，而是**站台外延长段的正常拓扑**。

### 32.18.3 真问题收窄为一句

> **挂接寻路的预留终点（或 rp() 的铺路止点）算到了站台末端之外，而不是算在「机车自身长度 + 到车底的间隙」所要求的位置。**

三种待判机理（下一轮按日志尾段逐条排除）：

1. **M-1 终点判据越界**：`CPL-BEST`/`CPL-ENDTILE`/`FindSafePositionOnSafePositionNodes` 选中的"安全位置"落在站台末端之外（车底已占满 1424..1437,1874，站台上无位 ⇒ 安全位置外溢）。这是记忆 `72821122` 的同族，但落点应改述为「本线站台外的机走线」。
2. **M-2 rp() 补铺过头**：`rp()` 里"补齐车头到车底之间站台预留"的循环，步数按 `tiles_skipped`/几何算，越过站台末端仍继续订（KI-346 家族的双槽格取轨错误会让这种越界不易察觉）。
3. **M-3 站台跳段**：`TrackFollower` 一步跳过整段站台（`tiles_skipped`），跳段后 `new_tile` 已在站台外，若终点判据按 `new_tile` 比较就会"到站台外"。

### 32.18.4 取证清单（日志尾段，编号 C-4 ~ C-6 供回填）

- **C-4**：本场景在 `build\R3R_debug.log` 的**起止行号**（玩家不用给，我用文件行数与「追加段」边界自定；先取尾部 3000 行）。
- **C-5**：`CPL-ENTRY` / `CPL-BEST` / `CPL-ENDTILE` / `CPL-TRACE-RESULT` 四行里，`best`/`end` 的 tile 是否 ≥ 站台末端（即已在 1873 上或沿本线越过 1874 的站台边界）。
- **C-6**：`RP cur=` 在 `1419..1421,1873` 三格上的 `td/trk/bits`——判「订了不走的轨（KI-346 形态）」还是「车真打算走那条路（M-1 形态）」。

**本轮仍零源码改动、零构建。**

## 32.19 尾段取证（2026-11-25）：找到「终点有两个来源」的直接读数 —— 猜测根因 **G-1**

### 32.19.1 日志规模与定位

`build\R3R_debug.log` 现共 **9832 行**（末尾 `:9832 WP-ENTER veh=75 ...`），玩家本轮实验为**尾部追加段**；下文锚点全部落在 `:1900~:9870` 区间，可视为**无紊乱干扰**的干净样本（本轮场景机动 car 与车底已分离）。

### 32.19.2 决定性读数：`CPL-BEST` 给出**两个互斥终点**

| 锚点 | 读数 | 判定 |
|---|---|---|
| `:1999` | `CPL-BEST veh=15 tile=1424,1874 td=0 cost=25854` | 终点 = **车底头端格**（被车底自己占着），代价极高 |
| `:2362/:2388/:2443/:2471/:2589` | 同 tile，`cost=21610/21110/20610/20300/17000` | 同一终点被反复报出，cost 单调递减 = YAPF 逐步逼近但**永远到不了** |
| `:2663` | `CPL-BEST veh=15 tile=1438,1874 td=0 **cost=0**` | **正解**：车底东端之外一格，零代价 |
| `:5618` | `CPL-BEST veh=15 tile=1438,1874 td=0 cost=0` | 同一正解第二次出现 |

⇒ 挂接寻路**同时**存在「1424,1874（车底头，cost 大）」与「1438,1874（车底尾外，cost=0）」两种终点，说明**终点判据有两条来源，互相不一致**。车底占 `1424..1437,1874`，机车从东侧（`1457,1874`→`1442,1874`）逼近，正解只可能是 `1438,1874`。

### 32.19.3 `FindSafeCouplePosition` 的 end 直接就是站台外的机走线

```
:2366 FSCP-RES    tile=1447,1874 tk=0 owner=15 otile=1457,1874 ... ord=16 oend=1419,1873 oendtd=0 othru=1
:2367 FSCP-REACH  tile=1447,1874 veh=15 ord=16 pos=1457,1874 ... end=1419,1873 endtd=0 isGtr=1
:2368 FSCP-CPL    tile=1447,1874 veh=15 ... end=1419,1873 endtd=0
:2369 CPL-SAFE-FAIL veh=15 tile=1442,1874
```

- `end=1419,1873` = 车底头 `1424,1874` **再往西 5 格**，已越过站台末端、落在 **1873 机走线**上 —— 与玩家 C-2/C-3 口径逐字吻合；
- 同一形态在 `:2392/:2393`、`:2447/:2448`、`:5424/:5425` 反复出现（`pos` 从 1457→1456→1451→1458,1875，即机车边逼近边重算，end 恒为 `1419,1873`）；
- `isGtr=1` 是本轮**最值得读的一个字段**：疑似「目标比参考点更远（greater）」的方向标志，若成立则 end 是**按"更远那一侧"选的**，天然会越过车底落到站台外。

### 32.19.4 猜测根因（**G-1，待验证**）

> **安全位置（`end`）以等待链的"头端"为锚、且按"更远（`isGtr=1`）"方向外推**，于是车底头 `1424,1874` 被占时，安全位置被推到车底之外的站台末端外（`1419,1873` 机走线）；`rp()` 再沿这条安全位置路径铺预留，就铺出了 `1419/1420/1421,1873` 三格，并"超出站台"。

支撑：①`CPL-BEST` 存在 `cost=0` 的 `1438,1874`（说明**正确终点是可算出来的**，只是没被采用）；②`end=1419,1873` 恒等于"车底头再往西"，而不是"车底尾再往东"；③`CPL-SAFE-FAIL tile=1442,1874` 表明那次安全位置根本没成功，后续退回的却是这条越界路线。

与既有条目的关系：这是记忆 `79051681`（命中端/拼接端角色错位：鼻尖↔尾 vs 尾↔头）在**寻路终点**一侧的同族；也是记忆 `72821122` 的同族，但落点应按 C-3 改述为「本线站台外的机走线」，**不是平行站台**。

### 32.19.5 反向证据：三格被反复"归给 17"后清扫

```
:2900 UNRES-RAIL tile=1419,1873 track=0 phase=release-ghost actor=17
:2901 RESV-WATCH CLEAR-RAIL tile=1419,1873 track=0 phase=release-ghost actor=17 head=17
:2902-2904 RESV-GHOST couple veh=17 tile=1419,1873 / 1420,1873 / 1421,1873 track=5
:2905 RESV-GHOST couple veh=17 tile=1421,1874 track=4
:5854-5859 同形态第二次（CPL-WAITCLEAR veh=18 head=17 ... nth=4 后一次性清 1419/1420/1421,1873 + 1421,1874）
```

- 三格**每轮角色相同**：由 'couple 清扫' 按归属判定记到 `veh=17`（等待车底链头）名下并释放；
- 但真正铺它们的是 `veh=15` 的 FSCP 安全位置路径（`:2366/:2367`）⇒ **归属字段（`veh=17`）≠ 实际订者（`veh=15`）**；
- ⇒ 与 §32.16/§32.17 一致：`RESV-GHOST` 的 owner 是"归属判定的读法"，不能当"谁铺的"的证据。这一点在**无紊乱**的本轮样本里同样成立，进一步印证 KI-683 的机理是**判定缺陷**而非紊乱产物。

### 32.19.6 下一步取证（代码，下一轮）

- **G-1a**：`src\pathfinder\yapf\yapf_rail.cpp` 的 `FindSafeCouplePosition` / `CheckSafePositionOnNode` 里 `end`（`oend`）如何确定；`isGtr` 的确切语义与它选端的方向。
- **G-1b**：为什么 `CPL-BEST cost=0 (1438,1874)` 已经算出来却没被采用（是否被 `1424,1874` 那次覆盖/后者优先级更高）。
- **G-1c**：`rp()` 的铺路止点是否就是 `end`；若是，则"超出站台"是 end 越界的**直接后果**，一处修好两处消除。

## 32.20 玩家纠正（2026-11-25）：终点构想 = 「车底近端**预留**所在格，按轨型往外一格」

### 32.20.1 玩家原话要点（逐字保留）

- 我上轮把 `CPL-BEST tile=1438,1874 td=0 cost=0` 称作"正解"，**玩家否定**。
- 玩家给出的正确构想（两分支）：
  1. **车底近端未超出站台** ⇒ 取「车底所在站台的**近端格**」往外一格；
  2. **车底近端已超出站台** ⇒ 取「车底近端**所在格**」往外一格。
- 统一表述（玩家原话）：「**车底近端预留所在格按轨型往外一格**」。
- 玩家的直接结论：终点「**不应该是 `1438` 那个有车底预留占着的站台格，而应该是 `1437` 那个无车底预留的站台外一格**」。

### 32.20.2 撤销的旧判断

§32.19.2 / §32.19.3 把 `1438,1874 cost=0` 当作"正解、只是没被采用"——**撤销**。按玩家口径它只是一个**被车底预留污染的候选**。

### 32.20.3 猜测根因 **G-2**（比 G-1 更具体，待验证）

> `cost=0` 那一格被丢弃，是因为**它还压在车底自己的预留上**——终点候选落在"与机车相接的那一侧预留"上，被终点/安全位置判据判成不可用；寻路于是退回 `1424,1874`（车底**头**端，`cost` 递减却永不到达）那条，而那条路线继续向远端延伸，越过站台末端后被轨型带到 `1873` 机走线 ⇒ **既"超出站台"、又"包含三格"**，两个症状同一个根。

### 32.20.4 代码落点（本轮已读到，支持 G-2，但未定分支）

`src\pathfinder\yapf\yapf_rail.cpp:252` 的 `FindSafeCouplePositionProc`：

- `:264` `if (HasReservedTracks(tile, tracks))`：该格有覆盖 `tracks` 的预留才进"占位者判定"分支；
- `:356` `if (best->index == Yapf().GetVehicle()->index) return true;`：**只豁免询路机车自己**（注释写明这是修 `FSCP ... fail=notWC best=<self>` 锁死的那一处）；
- `:357~` KI-62 判据：占位者必须是"真正的等待挂接段"才放行（`R3RIsCoupleTarget` / `R3RCoupleAllowed`），否则不放行；
- `:409` `else if (GetReservedTrackbits(tile) != TRACK_BIT_NONE)`：有预留但**位不覆盖**时走第 340 轮的"自持豁免"分支。

⇒ 只要 `1438,1874` 的预留归属解析结果不是"机车自己"、也不满足"等待挂接段"，该格就 `return false`。这就是"把被车底预留占住的那一格丢掉"的实现形态。

**但** `:356` 与 KI-62 都可能命中放行，所以"被丢"未必发生在 `FindSafeCouplePositionProc`，也可能发生在安全位置节点择优（`:960 FindSafePositionOnSafePositionNodes`，其中 `:1642` 是第 502 轮 S4 的静态终点接管 KI-676）。⇒ 二分：

- **G-2a**：`FindSafeCouplePositionProc` 在 `1438` 判 false（占位者解析没命中任何豁免）；
- **G-2b**：`FindSafePositionOnSafePositionNodes` 在多个安全节点间择优时把 `1438` 排掉、换成 `1424`。

### 32.20.5 新疑点 **Q-1**（坐标口径待玩家一句话对齐）

玩家写「`1437` 无车底预留的站台外一格」，但 `CPL-ENDTILE` 记录 `pTail=1437,1874`、车底占 `1424..1437,1874` ⇒ `1437` 是**车底尾格**，不是"无预留的外侧一格"。两种可能：

- **(a)** 车底实际只占 `1424..1436`，`1437` 才是外侧一格（`pTail` 可能按"最后一节中心"而非"最后占位格"记）；
- **(b)** 站台末端在 `1438` 或更东，`1437` 是站台外一格，而车底尾车有一半悬在站台外。

**待确认（Q-1）**：站台东端（近端）的**末格**是 `1437` 还是 `1438`？车底真正占到的**最后一格**是哪一个？

### 32.20.6 下一步取证

- 读 `yapf_rail.cpp:264~410` 全段，确认"占位者=等待车底链"时的放行/拒绝条件；
- 读 `:960~` 与 `:1640~`，确认终点择优逻辑与 `cost=0` 落选路径；
- 日志侧：查是否有 `FSCP tile=1438,1874` 行（若该格被判 false，应有 `FSCP ... fail=`）。

**本轮零源码改动、零构建。**

## 32.21 代码侧确证（2026-11-25，第二轮）：终点实现里**不存在"往外一格"**

### 32.21.1 先订正一条伪读数（重要）

`CPL-ENDTILE` **在本机现役日志里根本不存在**：
`search_content pattern="CPL-ENDTILE" path=build\R3R_debug.log` ⇒ **0 匹配**（同文件 `ENDTILE` 184 条全是 `endTile=` / `RESV-END` 等无关串）。

⇒ §32.20.5 引用的「`CPL-ENDTILE` 记录 `pTail=1437,1874`」属**误引**（来源是记忆条目 72821122 对另一份/更早日志的描述，不是本轮样本）。**该读数作废**。

- 影响：Q-1 中原本用来推断「1437 是车底尾格」的那条依据消失；
- 保留：Q-1 本身仍成立（站台东端末格是 `1437` 还是 `1438`、车底实际末格是哪个，仍待玩家一句话对齐）。

### 32.21.2 已确证：终点 = `consist_head_tile`，**没有任何 +1**

`src\pathfinder\yapf\yapf_rail.cpp`：

- `:1767`（常规分支）
  `const TileIndex res_target_tile = (consist_head_tile != INVALID_TILE) ? consist_head_tile : pNode->GetLastTile();`
  ⇒ 预留目标就是 `consist_head_tile` **本身**；
- `:1831`（单段摸路分支）
  `TileIndex tg = (consist_head_tile != INVALID_TILE) ? consist_head_tile : pPrev->GetLastTile();`
  ⇒ 摸路终点同样是 `consist_head_tile` **本身**。

`consist_head_tile` 的解析在 `:1695-1733`（第 502 轮 S4 / KI-676 区）：

- `:1696` 先用 `GetTrainForReservation(pNode->GetLastTile(), ...)` 取占位链，`:1697` 若解出的是**机车自己**则丢弃；
- `:1699-1702` 否则退回「该格上真正停着的别的链」（要求 `IsFrontEngine` 且 `First() != self`）；
- `:1724` `if (best_on_consist) consist_head_tile = pNode->GetLastTile();` —— YAPF 停在车底本身上时直接取该格；
- `:1726-1729` 否则比较 `DistanceManhattan(v->tile, t->tile)` 与 `DistanceManhattan(v->tile, h->tile)`，**取曼哈顿距离更近的一端**（`t->tile` 尾车 / `h->tile` 头车）。

**结论**：整条链上没有任何"按轨型往外一格"的计算。玩家的构想（车底近端预留所在格**按轨型往外一格**）在当前实现里**结构性缺失**，不是某个条件判错而已。

### 32.21.3 G-2 升格为 G-2′（猜测根因，待验证）

> **G-2′**：终点候选被钉在「车底某一端**所在的那一格**」上，而不是「那一格沿轨型再往外一格」。车底近端那一格通常正是车底自己预留覆盖的格，于是：
> 1. 端点判据（`IsWaitingPositionFree` / `FindSafeCouplePositionProc`）要在一格"自己人占着"的格上放行，代价与风险都高；
> 2. 一旦该端判 false，择优退回到**另一端**（现场 `1424,1874`）⇒ 路径必须**穿过整条车底**，预留自然越过站台末端，再被道岔轨型带到 `1873` 机走线。
>
> ⇒ 「超出站台」与「包含三格 1873」两个症状**同一个根**：终点选错端 + 终点缺"往外一格"。

端选择本身另有一个可独立出错的点（**G-2c**，待验证）：`:1726-1729` 用的是**曼哈顿距离**到 `h->tile`/`t->tile`，而 `Tile` 是该节车的**所在格**、不是该端**最外沿格**；车底首尾跨格/压在岔上时曼哈顿距离可能偏向错误的一端 ⇒ 直接把 `consist_head_tile` 定成远端 `1424`。这正是现场绝大多数 tick 的最优终点都是 `1424,1874` 的原因形态。

### 32.21.4 `cost=0` 的真实含义（修正上一轮的共同前提）

`:1749` 的 `CPL-BEST ... cost=%d` 打在 `pNode->cost`：

- `cost=0` 不是"某条候选路径代价为零"，而是**最优节点就是起点自身**；
- 代码在 `:1755` 对此有专门特判：
  ```
  if (pNode->parent == nullptr) {   // zero-length couple path
      ...
      return INVALID_TRACKDIR;      // :1762
  }
  ```
  注释写明：机车已与等待车底同格，**没有路径要回溯、也不铺任何预留**，交给 `ChooseTrainTrack` 停车 + 碰撞/挂接处理器去物理合并。

⇒ 对 `1438,1874 cost=0` 的正确读法是「**该 tick 认为已经在车底近端同格，走零长路径特判、一条预留都不订**」，而不是「走完判据后才被丢掉」。
玩家说的"被车底预留占住所以丢"在代码上对应**另一条路**：零长特判交给挂接处理器后若物理合并没成立，下一 tick 重新寻路，此时 `consist_head_tile` 解析成远端 `1424` ⇒ 才产生那条穿过车底、越过站台的预留。

⇒ **可判别的前置事实**：只要 `consist_head_tile` 被解析成 `1424`（远端），现场必然出现"预留越过站台"。修法优先级应放在 **端点解析（G-2c）+ 终点往外一格（G-2′）**，而不是松绑 `FindSafeCouplePositionProc` 的放行条件。

### 32.21.5 `FSCP-REACH ... isGtr=1` 语义澄清（不是终点判据）

`isGtr` 只是 `gtr == tr`（`FollowTrainReservation()` 回溯出的链头 `gtr` 是否与 `GetTrainForReservation()` 报的名义 owner `tr` 相同），表示"这格预留的名义 owner 就是这条链"。

⇒ `end=1419,1873 isGtr=1` 的正确含义是：**等待车底（veh=17）自己的预留终点已经伸到 `1419,1873`**，且 owner 解析一致。也就是说 `1873` 上的预留**本来就是车底那条预留铺到的**，不必然是机车新订的。这与 KI-683（`RESV-GHOST`/`UNRES-RAIL` 里 `owner` ≠ 实际订者）是两件独立的事，不能互相作为证据。

### 32.21.6 取证清单更新（下一步）

已确认无需再做：

- ~~读 `:264~410` 确认"占位者=等待车底"的放行条件~~ ⇒ 已读；`:356` 只豁免自己，`:361/:375/:376` 是 KI-62 放行链；
- ~~`isGtr` 语义~~ ⇒ 已确证（§32.21.5）；
- ~~`cost=0` 落选路径~~ ⇒ 已确证为零长特判（§32.21.4）。

仍待做（按优先级）：

1. **代码**：确认 `consist_head_tile` 是否还有其它写入点（S4 静态接管在 `:1642` 一带），确认端选择只有 `:1726-1729` 一处；
2. **日志**：取 `CPL-BEST tile=1424,1874` 与 `tile=1438,1874` 两类行各自的配对行 `CPL-HEAD best=/head=/on=` —— 若 `on=0` 且 `head=1424`，G-2c 成立（端选错）；若 `on=1`，则是 YAPF 停错端；
3. **玩家确认 Q-1**：站台东端末格 `1437` vs `1438`、车底实际最后一格（一句话即可）；
4. 若落地修法：方向是「`consist_head_tile` 之后再沿 `pNode->GetLastTrackdir()`/轨型 `TrackFollower` 前进一步得到外侧格」，并让 `res_target_tile` / `tg` 都改用外侧格 —— 但**本轮不改码**。

**本轮（第二轮）零源码改动、零构建。**

## 32.22 决定性取证（2026-11-25 第三轮）：`CPL-HEAD-STATIC` 23 条读数——**根因不是"缺 +1"，是 `best_on_consist` 采了远端**

### 32.22.1 先说明：现场根本没走动态解析

- `CPL-HEAD`（动态分支探针，`yapf_rail.cpp:1738`）在日志中 **0 匹配**；
- `CPL-HEAD-STATIC`（第 502 轮 S4 静态接管探针，`yapf_rail.cpp:1673`）**23 条**。

⇒ 现场 100% 走 **S4 静态终点接管**（KI-676）分支：`r3r_snap_tile != INVALID_TILE` ⇒ 直接进 `:1656` 那块，动态解析（`:1683~`）一次都没执行过。
⇒ §32.20.4 里对动态分支（`:264/:356/:361/:409`）的二分（G-2a/G-2b）**不适用于本样本**，应整体作废。

### 32.22.2 23 条读数（全量，字段 = `best / snap / farvalid / head / on`）

```
20 条：best=1424,1874  snap=1424,1874  farvalid=1  head=1424,1874  on=1
 3 条：best=1438,1874  snap=1424,1874  farvalid=1  head=1437,1874  on=0
    （行号：:2662 / :5617 / :8179）
```

关键事实：

1. **`snap=1424,1874` 在所有 23 条里恒定**，`farvalid=1` 恒定；
2. `R3RStaticTargetFarTile()` 的定义是 `Train::Get(snap_veh)->Last()->tile`（`yapf_destrail.hpp:494-499`）；
3. 因此：**车底链头格 = `snap` = `1424,1874`，车底链尾格 = `far` = `1437,1874`**，车底占 `1424..1437,1874`；
4. `on=1` 表示 YAPF 最优节点落在车底自己身上（`best` 格上有 `snap_veh`）；
5. `on=0` 时执行 `:1664-1665` 的曼哈顿比较：`v=1438`，`d(1438,1437)=1 < d(1438,1424)=14` ⇒ `head = far = 1437,1874`。

### 32.22.3 G-2c **成立**，并定位到唯一一行

```cpp
/* yapf_rail.cpp:1662-1669 */
if (best_on_consist) {
    consist_head_tile = pNode->GetLastTile();          // ← 无条件采纳 YAPF 停的那一格
} else if (r3r_snap_far != INVALID_TILE &&
        DistanceManhattan(v->tile, r3r_snap_far) < DistanceManhattan(v->tile, r3r_snap_tile)) {
    consist_head_tile = r3r_snap_far;
} else {
    consist_head_tile = r3r_snap_tile;
}
```

- `on=0` 分支（3 条）**结果正确**：给出 `1437,1874`，**正是玩家口径要求的目标值**；
- `on=1` 分支（20 条，占 **87%**）**无条件**采用 `best` 格 = `1424,1874`（车底**远端**，远离机车的另一端），**完全绕开了"取近端"的比较**。

⇒ **根因 G-2c（已由读数证实）**：`best_on_consist` 只回答"YAPF 最优点是否压在车底身上"，**没有回答"压在车底的哪一端"**。一旦 YAPF 停在车底远端 `1424`，代码就把会合点定在 `1424`，于是：

- 路径必须**从机车一侧穿过整条车底** → 预留沿站台全长铺设 → **越过站台末端**；
- 越过末端后按道岔轨型继续 → **被带到 `1873` 机走线**（现场 `FSCP-RES oend=1419,1873`、`FSCP-REACH end=1419,1873 isGtr=1`，正是这条自铺预留的终点）。

⇒ 「超出站台」+「包含三格 1873」= **同一个根**，与"缺一个 +1 格"无关。

### 32.22.4 修正 §32.21 的两处判断（诚实订正）

- §32.21.2 说"终点缺'往外一格'，玩家构想在结构层面缺失" ⇒ **不准确**。`on=0` 分支已经算出 `1437`（玩家指定的那个值）。缺的不是 +1，而是 **`on=1` 时不做端的比较**。
- §32.21.3 的 **G-2′**（"终点选错端 + 终点缺往外一格"）⇒ 前半（选错端）**成立**，后半（缺 +1）**撤销**。以 **G-2c** 为准。

> 仍需 Q-1 一句确认：玩家说 `1437` 是"无车底预留的站台外一格"，但本读数显示车底链尾车**所在格**就是 `1437`。⇒ 二者不矛盾的唯一解释是：**"车底占格" ≠ "车底预留占格"**（车底停在 `1437`，而它铺出站路径的预留压到 `1438`）。请玩家确认这一点，它决定 `1437` 是否可直接作为终点，还是仍要再往外一格取 `1438`。

### 32.22.5 修法方向（三档，未落地；本轮不改码）

- **M-1（最小、直指本根因）**：`:1662` 的 `best_on_consist` 分支也做端比较 ——
  即 `best_on_consist` 为真时，若 `pNode->GetLastTile()` 与 `v->tile` 的曼哈顿距离**大于**另一端（`snap`/`far` 中较近者），就不要无条件采纳，改取近端。等价于：**`best_on_consist` 只用来确定"这一格是车底"，不用来决定"哪一端"**。
  风险：KI-200 的硬约束「路径终点与预留目标必须同格」被打破（YAPF 停在 1424、目标改成 1437）⇒ 必须同时让 YAPF 的 destination 判定**只在近端成立**，否则回溯会出现"终点格 ≠ 目标格"的跨车底走法。
- **M-2（治根，符合玩家构想）**：S2b 快照**直接存"会合点"**（车底近端那一格，按玩家口径"近端预留所在格按轨型往外一格"），并让 `PfDetectDestination` 只认这一格作为终点。`snap_tile` 现在存的是车底链头 `1424`，**方向恰好反了**，这是 S4 实现层面的方向性错误（第 502 轮把"车底"当成了"会合点"）。
- **M-3（不建议）**：松绑 `FindSafeCouplePositionProc` / `IsWaitingPositionFree` 的放行条件。§32.22.3 已证明失败不在那里。

### 32.22.6 结论（一句话）

> 预留之所以"越过站台并上到 1873"，是因为 **S4 静态终点接管把车底链头 `1424`（远端）当成了会合点**，而车底真实近端是 `1437`；`best_on_consist` 分支缺少端判定，导致 87% 的寻路都走远端、必须穿过整条车底。**修 `:1662` 的端判定 + 让 YAPF 终点只认近端，即可一处修好、两处消除。**

**本轮（第三轮）零源码改动、零构建。**

## 32.23 玩家更正（2026-11-25 第四轮）：目标终点是 `1439,1874`，不是 `1437`

### 32.23.1 玩家原话

> 「啊，我刚才描述错误了，1437 是站台近端往内一格，我想要的应该是 1439，非常抱歉」

⇒ §32.20 里"应为 1437"的读数**作废**；§32.22 的全部判读基准随之校正。

### 32.23.2 重建后的坐标口径（Q-1 已由玩家回答，关闭）

| 格 | 含义 | 依据 |
|---|---|---|
| `1424..1437,1874` | 车底占用的车格（链头 `1424`、链尾 `1437`） | `snap=1424` + `R3RStaticTargetFarTile()=Last()->tile=1437` |
| `1438,1874` | **站台近端（东端）末格**；同时也是**车底近端预留所在格**（被车底自己的预留占住） | 玩家"1438 有车底预留占着" + 本轮"1437 是站台近端往内一格" ⇒ 末格 = 1438 |
| `1437,1874` | 站台近端**往内一格**（车底链尾车脚下） | 本轮原话 |
| **`1439,1874`** | **目标终点** = 车底近端预留所在格 `1438` 按轨型往外一格 | 本轮原话"我想要的应该是 1439" |

- **Q-1 关闭**：站台近端末格 = `1438`，车底实际最后一格 = `1437`。
- §32.22.4 里那条"待玩家确认"的悬空问题（1437 到底是站台内还是站台外）**已有确切答案：站台内**。

### 32.23.3 对 §32.22 判读的校正（差几格，别记错）

- `CPL-BEST tile=1438,1874 cost=0` —— `1438` 是**车底近端预留所在格**，距目标 `1439` **差 1 格**；
- `CPL-HEAD-STATIC ... head=1437,1874 on=0` —— `1437` 是**车底链尾车所在格**，距目标 `1439` **差 2 格**；
- ⇒ 所以「终点缺一个 +1」这个说法**只在以 `1438` 为基准时成立**；以代码实际给出的 `1437` 为基准，缺的是 **+2**。

### 32.23.4 根因改写为 G-3（两段缺失，而非一段）

> **G-3（猜测根因，待验证）**：从"车底"推出终点的链条上缺了**两步**，代码两步都没做：
> 1. **车格 → 该端预留边界格**：`consist_head_tile = Train::Get(snap_veh)->Last()->tile`（`yapf_rail.cpp:1669`）拿到的是**车格** `1437`，不是这端**预留**伸到的格 `1438`；
> 2. **预留边界格 → 按轨型往外一格**：全链**没有任何**沿 trackdir 前进一格的运算（§32.21.2 已逐行确证），所以即便第 1 步取到 `1438`，也不会自己走到 `1439`。
>
> 两步缺失叠加，终点恒比目标少 2 格；终点落在车底自己预留覆盖的格上（`1438`）或更内（`1437`），于是 `IsWaitingPositionFree`/`FindSafeCouplePositionProc` 必须在"自己人占着"的格上放行 —— 这正是 §32.22.3 观察到的 87% 走远端 `1424`、穿过整条车底、越站台后被轨型带到 `1873` 的上游成因。

### 32.23.5 修法口径更新（M-2 细化，仍未落地）

按玩家统一表述「**车底近端预留所在格按轨型往外一格**」，实现必须显式做两步：

1. 取车底近端（靠机车那一端，`Last()` 或 `First()`，由曼哈顿近端比较决定，`yapf_rail.cpp:1664`）；
2. 求该端的**预留边界格** —— 从该端向该端朝向 `FollowTrainReservation()` / `GetLastReservedTile()` 走到底，得到 `1438`（不是车格 `1437`）；
3. 再从边界格沿**该端轨型/trackdir** 用 `TrackFollower` 前进一步得到 `1439`；
4. 让 `res_target_tile`（`:1767`）与单段摸路 `tg`（`:1831`）都用这个 `1439`，并让 YAPF 的 destination 判定只在该格成立 —— 否则违反 KI-200「路径终点与预留目标同格」。

**Q-2（新增，待玩家确认）**：`1438 → 1439` 之间是同一轨型直连吗（有无道岔/曲线/站台边界）？这决定第 3 步是"`TrackFollower` 走一步"还是"按 `Trackdir` 取相邻格"。若该处正好是站台末端接普通轨道，一步即 `1439`；若有岔口，则"按轨型往外一格"可能不止 +1 格坐标。

**本轮（第四轮）零源码改动、零构建。**

## 32.24 Q-2 关闭 + 口径最终复述（2026-11-25 第五轮）

### 32.24.1 玩家答复（原话）

> 「1438 → 1439 之间是同一轨型直连，而不同轨型的连接，我们可以搁置」
> 「你好像被我绕晕了，真是不好意思，请你再复述一遍我想要的和现行的」

⇒ **Q-2 关闭**：`1438 → 1439` 同轨型直连 ⇒ 修法第 ③ 步就是「从边界格沿该端 trackdir 用 `TrackFollower` 前进一步」，即坐标 +1 格，无需考虑岔口换轨。
⇒ 「不同轨型的连接」整体**搁置**（不纳入本轮口径、也不作为修法前置）。

### 32.24.2 订正上一轮的一处错误结论（重要）

§32.22.3 结尾写「`on=0` 分支给出 `1437,1874`，**正是玩家口径要求的目标值**」——**这句错了**，必须撤回。

- 当时把玩家目标误当成 `1437`；玩家已更正目标为 `1439`。
- 因此 `on=0` 分支给出的 `1437` **不是目标值**，它距目标仍差 **2 格**。
- 结论不变的部分：`on=0` 分支的**方向是对的**（取近端、不取远端）；错的是**粒度和位置**（停在车格，没到车底近端预留边界，也没再往外走一格）。

### 32.24.3 想要的 vs 现行的（最终口径，逐格对照）

坐标基线（全部同一行 `y=1874`）：

| 格 | 含义 | 来源 |
|---|---|---|
| `1424,1874` | 车底**远端**（背离机车那端）的链头车所在格 | `CPL-HEAD-STATIC snap=1424,1874` 全 23 条恒定 |
| `1425..1436` | 车底中间车格 | 同上推算 |
| `1437,1874` | 车底**近端**（靠机车那端）的链尾车所在格 = 站台近端**往内一格** | 玩家第四轮更正 + `R3RStaticTargetFarTile()=Last()->tile` |
| `1438,1874` | **站台近端末格**；也是**车底近端预留所在格** | 玩家口径 + YAPF 曾 3 次把最优点落在该格（`best=1438`） |
| **`1439,1874`** | **想要的目标终点** = `1438` 沿同轨型往外一格 | 玩家第四轮 + 第五轮确认直连 |

**想要的行为（一句话）**：机车挂车寻路的终点 = **车底近端那一端的「预留所在格」，再沿同轨型往外一格 = `1439,1874`**。该格上**没有车底的车、也没有车底的预留**，机车可以从东侧正常开进去完成挂接。

**现行的行为（一句话）**：代码把终点定在**车底某一端「车节所在的那一格」**，而且**从不沿轨型再往外走**，于是只可能落在 `1424` 或 `1437` 两个值之一：

| 分支 | 出现次数 | 落点 | 与目标差 |
|---|---|---|---|
| `best_on_consist=1`（`yapf_rail.cpp:1662`，无条件采纳 YAPF 最优点） | **20 / 23** | `1424,1874`（远端） | **15 格** |
| `best_on_consist=0`（`:1664-1665` 曼哈顿比较取近端） | **3 / 23** | `1437,1874`（近端车格） | **2 格** |
| 目标 | — | `1439,1874` | 0 |

⇒ 现行的两个毛病，**互相独立**：

1. **端选错**（20/23）：`best_on_consist` 只回答"这格是不是车底"，不回答"是车底的哪一端"，YAPF 停在远端就把会合点定成远端 `1424`。后果 = 路径从机车侧**穿过整条车底**，预留沿站台全长铺出，越过站台末端后被道岔轨型带到 `1873` 机走线（`FSCP-RES oend=1419,1873`、`FSCP-REACH end=1419,1873 isGtr=1` 就是这条自铺预留的终点）。
2. **粒度不够、且缺最后一步**（3/23，以及任何端选对之后仍然存在）：即便端选对了，取的是**车格** `1437` 而不是**预留边界格** `1438`；而且全链没有任何"沿 trackdir 前进一格"的运算 ⇒ 永远到不了 `1439`。

### 32.24.4 两个可观测的先行事实（供修法定位，已由日志证实）

1. YAPF 的到达判定**本来就能在 `1438` 成立**（3 条 `best=1438`）；是代码把它改写成了 `1437`。⇒ 把目标改到 `1439` 时，**必须同步把 destination 判定改到 `1439`**，否则 YAPF 会在 `1438` 就报到达（这正是 KI-200「路径终点与预留目标同格」的具体落点，不是额外的锦上添花）。
2. `snap` 快照存的 `1424` 是**车底链头（远端）**，方向恰好与"会合点"相反。⇒ 若走 M-2「快照直接存会合点」，需要改的是**快照的内容**，不是修 `snap` 的消费者。

### 32.24.5 修法口径（合并 G-3 + M-1/M-2，仍未落地）

四步，缺一不可：

1. **定近端**：按曼哈顿距离选靠机车那一端（`:1664` 已有此逻辑，但只有 `on=0` 一条分支会走到；需让 `on=1` 也过这一步）；
2. **车格 → 近端预留边界格**：`1437 → 1438`（要的是这一端**预留铺到哪一格**，不是车的 `tile`）；
3. **边界格 → 沿同轨型往外一格**：`1438 → 1439`（Q-2 已确认直连，`TrackFollower` 走一步即 +1 格）；
4. **终点与判定同格**：`res_target_tile`（`:1767`）、单段摸路 `tg`（`:1831`）、以及 destination 判定三者**都用 `1439`**。

**本轮（第五轮）零源码改动、零构建。**

## 32.25 S5 修法落地（2026-11-25，第 503 轮）

### 32.25.1 一句话

按玩家口径把挂接寻路终点从「车底某端车节所在格」改成「车底**近端预留边界格**沿同轨型**再往外一格**」，并把 destination 判定、启发锚、预留目标、摸路终点四处**统一到同一格**。

### 32.25.2 落点（全部在快照建立时一次算完，O(站台剩余长度)）

新增 `CYapfDestinationTrainRailT::R3RComputeMeetTile(near_tile, far_tile)`：

1. 车底两端 = `t->tile`（链头）与 `t->Last()->tile`（链尾）；
2. 近端 = 曼哈顿距离更近 `v->tile` 的那一端（与回溯阶段原口径一致，不再依赖 `best_on_consist`）；
3. 从近端沿 `GetRailStationAxis()` 的轴、朝**背离车底**的方向逐格前进，用 `IsCompatibleTrainStationTile(next, cur)` 判断是否还在同一站台；
4. **第一格不再是同站台格** = 站台近端末格之外（现场 `1439,1874`），即会合点。

算不出（车底不在站台 / 两端同格 / 方向判不出）⇒ 返回 `INVALID_TILE`，整条链**退回改动前行为**，不引入新拒绝。

### 32.25.3 四处同格

| 位置 | 改动 |
|---|---|
| `yapf_destrail.hpp` 成员 | 新增 `TileIndex snap_meet_tile`（`R3RTakeStaticTargetSnapshot()` 里赋值）+ 访问器 `R3RStaticMeetTile()` |
| `PfDetectDestination(TileIndex, Trackdir)` | 新增**最高优先级**精确闸：有会合点时只接受 `tile == snap_meet_tile`，其余格全 reject。刻意放在 `IsRailStationTile/IsRailDepotTile` 闸**之前**——会合点在站台外，放在后面永远到不了 |
| `SetDestination()` | `dest_tile`（A* 启发锚）从 `snap_tile`（车底链头 `1424`，方向与会合点相反）改为会合点 `1439` |
| `yapf_rail.cpp` 回溯阶段 | 新增 `r3r_meet != INVALID_TILE` 分支并**置于最前**：直接 `consist_head_tile = r3r_meet`，跳过 `best_on_consist` / 曼哈顿端选整块 ⇒ `SetReservationTarget()`（`:1783`）与单段摸路 `tg`（`:1847`）自动同格 |

日志同步扩字段：`CPL-HEAD-STATIC` 增加 `meetvalid= / meet=x,y`，并新增 `PFD-MEET kind= tile= t= n=`（前 16 次）用于确认 YAPF 确实在会合点报到达。

### 32.25.4 预期观测量

- `PFD-MEET tile=1439,1874` 出现 ⇒ 到达判定已收到会合点；
- `CPL-HEAD-STATIC ... meetvalid=1 meet=1439,1874 head=1439,1874 on=0` ⇒ 端选已由会合点接管；
- `FSCP-RES oend=` 不再落在 `1419,1873`，预留不再穿过整条车底；
- 若出现 `found=0` 且无 `PFD-MEET`，说明 `1439` 当时不可达（被占/信号阻断），属预期回退边界，不是回归。

### 32.25.5 编译

改了 `yapf_destrail.hpp`（类布局变化）⇒ 必须**全量重编**。注意：单目标预编译会让 `R3R_inc_guard.ps1` 误判为「header 不比最新 obj 新」，本轮已踩到一次，改用 `R3R_fullbuild.cmd detach` 全量。单目标编译（`yapf_rail.cpp.obj`）已通过，`EXITCODE=0`。

### 32.26 第 508 轮：会合点断段把 `station_map.h:31` 断言崩了（KI-685）

玩家报告「station_map.h 31行断言崩溃了」。断言 = `GetStationIndex(t)` 的 `IsTileType(t, Station)` 前置条件。

- 现场：`build\R3R_debug.log` 尾部 `PFX-GEO t=1439,1874 ... stn=0` + `PFD-MEET kind=car-only tile=1439,1874 t=18 n=1/n=2` ⇒ 会合点落在站台外普通轨道格（正是 §32.24/§32.25 定下的目标 `1439`）。
- 根因：`yapf_costrail.hpp` 的会合点强制断段复用了 `EndSegmentReason::Station`。该位除驱动 `ESRF_POSSIBLE_TARGET → PfDetectDestination → target_seen`（所需）外，还驱动 `:943 Station platform-length penalty → BaseStation::GetByTile(n.GetLastTile())`，而后者内部就是 `GetStationIndex` ⇒ 非站台格必崩。**新语义本身导致旧消费点失衡。**
- 修法：`:943` 加 `&& IsRailStationTile(n.GetLastTile())`。真站台代价逐字不变；会合点跳过站台惩罚但仍置 `target_seen`。
- 排除：`yapf_destrail.hpp` 9 处 `GetStationIndex` 均有 `IsRailStationTile` 守卫；陈旧 obj 也已排除（`follow_track.hpp` 早于相关 obj）。
- 详见 `R3R_station_map31_round508_memo.md`、`R3R_KNOWN_ISSUES.md` 第 508 轮 KI-685。





