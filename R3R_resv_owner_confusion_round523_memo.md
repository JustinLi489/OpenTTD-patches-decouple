# R3R 预留主人紊乱 —— 第 523 轮现场机制分析

- 采样：`build\R3R_debug.log`，311 511 B，mtime 2026-11-26 19:03:18（今日，有效）
- 场景：玩家自述「跑了一次预留主人紊乱的场景」
- 本轮**只取证 + 定性**，未改任何源码、未构建

---

## 一、现场角色（从日志反推）

| 车号 | 身份 | 依据（行号） |
|---|---|---|
| veh15 | **挂接订单机车**，`order=16`(OT_GOTO_COUPLE) `real=1`，满速 160 | `:2626` 起 CPL-NEXT；`:856` `COUPLE-FAIL loco=15 order=16 tx=1474 ty=1884` |
| veh18 | **等待中的纯车底链头**，`caronly=1` `otherOrder=0`，静止 `spd=0`，停在 1424,1874 一带 | `:2471` `TPR-BLOCKED veh=75 ... other=18 otherTile=1424,1874 otherSpd=0 otherOrder=0 otherFirst=18 caronly=1` |
| veh75 | 另一台机车，`real=1` 时挂上车底链（`consist=18`），随后 `real=3 order=6` 驶离 | `:490` `COUPLE-OK loco=75 rear=62 consist=18 co=1`；`:549` `ORD-AFTER-COUPLE head=75 n=19 real=1 borrowed=1 owner=18` |

veh15 与 veh18 是本次紊乱的主角；veh75 是旁证（同一批格上的第三辆车）。

---

## 二、紊乱过程（逐锚点）

### 阶段 1：veh15 无预留满速前进（`:2626`–`:2780`）

```
:2626 CPL-NEXT veh=15 tick=41329 mf=1474,1884 td=0 ... bits=0001 resv=0000 wholeOwner=-1 spd=41
:2666 CPL-NEXT veh=15 ... mf=1470,1882 td=12 ... bits=0021 resv=0000 wholeOwner=-1 spd=160
:2776 CPL-NEXT veh=15 ... mf=1460,1875 td=0 ... bits=0015 resv=0000 wholeOwner=-1 spd=160
```
`resv=0000 wholeOwner=-1` = 该格预留位为空、无主。同时：

```
:2627 RESV-HAVERES veh=15 tile=1473,1884 res=0x0 tracks=0x1 spd=41 ... foreign=0 nolook=1 look=0
:2772 RESV-HAVERES veh=15 tile=1460,1875 res=0x0 tracks=0x1 spd=160 ... foreign=0 nolook=1 look=0
```
即「格上无预留、`foreign=0`、`nolook=1`」——车照常以 160 冲过去。

### 阶段 2：进入 veh18 车底所占区间，仲裁把位判给 veh15（`:2846`–`:2966`）

```
:2846 RESV-ARB tile=1458,1875 station=0 a=veh18 wait=1 b=veh15 wait=0 picked=veh15
:2854 RESV-ARB tile=1457,1875 station=0 a=veh15 wait=0 b=veh18 wait=1 picked=veh15
:2860 CPL-NEXT veh=15 ... mf=1457,1875 ... bits=0030 resv=0020 wholeOwner=15 spd=160
:2863 RESV-ARB tile=1457,1874 a=veh15 wait=0 b=veh18 wait=1 picked=veh15
:2870 RESV-ARB tile=1456,1874 a=veh18 wait=1 b=veh15 wait=0 picked=veh15
:2964 RESV-ARB tile=1455,1874 a=veh18 wait=1 b=veh15 wait=0 picked=veh15
:2966 RESV-HAVERES veh=15 tile=1455,1874 res=0x1 tracks=0x11 spd=160 ... foreign=0 nolook=1
```
每次仲裁的 `picked` **恒为 veh15**，即使 `a=veh18 wait=1`（等待车底）排在前面。
`:2860` 的 `wholeOwner=15` 是直接后果：整格主人被判成 veh15 自己。

### 阶段 3：最后一格突然翻脸，判成别人的（`:3056`–`:3060`）

```
:3056 CPL-NEXT veh=15 ... mf=1455,1874 ... bits=0029 resv=0020 wholeOwner=-1 spd=160
:3058 RESV-FOREIGN-RES veh=15 tile=1455,1873 enterdir=3 res=0x20 tracks=0x20 spd=160 real=1
:3060 RESV-HAVERES veh=15 tile=1455,1873 res=0x20 tracks=0x20 spd=160 ... foreign=1 nolook=1
```
同一列车、同一条路上，**换到 1455,1873 这一格时 `foreign` 从 0 变成 1**。
注意 `:3056` 的 `wholeOwner=-1`（整格无主）与 `resv=0020`（轨位有预留）并存 —— 三套编码互不一致的典型样本。

### 阶段 4：撞击后停住（`:3136`–`:3180`）

```
:3136 COUPLE-FAIL loco=15 order=16 tx=1455 ty=1873
:3180 RESV-HAVERES veh=15 tile=1447,1874 res=0x0 tracks=0x4 spd=0 ... stuckretry=1 nolook=1
```
veh15 速度归零、`stuckretry=1`，`real` 仍为 1（挂接命令从未推进）。

### 旁证：veh75 被 veh18 挡在 1424,1874（`:2471`）

```
:2471 TPR-BLOCKED veh=75 tile=1424,1874 spd=3 mf=1424,1874 origin=1424,1874 okay=1 td=0 other=18 otherTile=1424,1874 otherMf=1424,1874 otherSpd=0 otherOrder=0 otherFirst=18 caronly=1
```
随后 veh75 绕进北侧平行线（`:2679` 1421,1873 / `:2705` 1419,1873）继续跑 —— 与备忘 §32.17 记录的"拐进北侧平行线"同族。

---

## 三、机制（已可确证的部分）

1. **归属判定不是"谁订的"，而是"沿预留位两端走查，遇到谁就是谁"**。
   `RESV-ARB` 的两个候选 `a` / `b` 就是两向走查各自撞上的车：`a=veh18`（等待车底压在该格上）、`b=veh15`（查询者自己的车头/预订链就在另一向）。
   ⇒ 同一格的"主人"由**两端当前站着谁**决定，而不是由"这条预留位属于哪条预订链"决定。

2. **仲裁规则（plain rail 取"非等挂者"）在"挂接机车 vs 等待车底"这一组合下退化为恒返回查询者**。
   现场 8 次仲裁（`:2846/:2854/:2863/:2870/:2964` 等）`picked` 全是 `veh15`，而查询者正是 veh15。
   因为：查询者本身是挂接行驶车 ⇒ `wait=0` ⇒ 永远满足"非等挂者"条件。
   ⇒ 该规则能把真正属别人的位判给自己，且**不可能判错成第三方**，所以现场表现为"全部归我"。

3. **`owned=true` ⇒ `foreign=0` ⇒ 吃 `early-haverserved` 捷径**，车把 veh18 车底压着的格当成自己的位直接驶入（160 满速），直到最后一格走查只撞上 veh18（没撞上自己）才 `foreign=1`。

4. **同一列车在同一条路上 `foreign` 会中途翻转（0→1）**，因为走查结果随车头位置每 tick 变。
   ⇒ 上层无法靠 `foreign` 得到"这条路整体是不是别人的"这一信息，只能在撞击前最后一格才发现，已经来不及。

5. **判定错误的残留后果**：`wholeOwner=15` 的那几格（`:2860` 1457,1875 / 1457,1874）由 veh15 的预订链占着不放，而 veh18 车底压在同一批格上 ⇒ 两条链交叉占位；veh15 停住后 `COUPLE-FAIL` 每 tick 重试（`:856`…`:3136` 共 20 余次），`real` 永不推进。

---

## 四、疑点（待代码确认）

- **D-1**：`RESV-HAVERES ... res=0x0 ... foreign=0`（`:2627` 等）：该格预留位为空，为何仍打印捷径行？需确认 `res`/`tracks` 打的是哪一格（origin 还是 next）。
- **D-2**：`nolook=1` 从 `:2515` 起对 veh15 恒为 1 —— 是 X-2 遗留的 `no_lookahead` 状态未清，还是本次正常状态？
- **D-3**：`:2874 RESV-EXT-STNEDGE veh=15 origin=1424,1874/0 stn=0 edge=1423,1874 nextbits=0x1 steps=0 spd=160` —— `stn=0`（非站台）却算出 edge 并截断，KI-684 B 修法的站台边界判据可能在非站台格误触发。
- **D-4**：`RESV-ARB` 的调用者是谁（挂接归属判据 `train_cmd.cpp:18528-18548`，还是别的路径）——需确认每一次仲裁都换算成 `foreign`。
- **D-5**：`wait=1` 的判定是 `current_order.IsType(OT_WAIT_COUPLE)`；veh18 的 `otherOrder=0`（`:2471`）与 `wait=1` 矛盾，需确认 `wait` 取的是哪一方的订单。

---

## 五、待复测判据（修法确定后）

1. 同场景下 `RESV-ARB` 的 `picked` 不再恒等于查询者，且 `wholeOwner` 与真实预订者一致。
2. veh15 在同一条路上 `foreign` 不再中途翻转（0→1）。
3. veh15 不得以 >0 的速度进入 veh18 车底所占格（`COUPLE-FAIL` 不再刷屏）。
4. `RESV-EXT-STNEDGE` 在 `stn=0` 时不再出现（若 D-3 成立）。
5. 负判据：`RESV-ARB` 自身订的位仍判给自己（KI-330 不回退）。

---

## 六、逐格判决数据（玩家索证：「哪一格被误判成谁的了」）

数据源：`build\R3R_debug.log`（311 511 B）。`RESV-ARB` 全日志仅 5 条，无遗漏。

### 6.1 被改判的 5 格（全部判给查询者 veh15）

| 日志行 | tile | 该格轨位 tracks | 被订位 reserved | 走查端 a | 走查端 b | 判决 |
|---|---|---|---|---|---|---|
| `:2846` | 1458,1875 | 0x1 | 0x1 | veh18 wait=1 | veh15 wait=0 | **veh15** |
| `:2854` | 1457,1875 | 0x10 | 0x10 | veh15 wait=0 | veh18 wait=1 | **veh15** |
| `:2863` | 1457,1874 | 0x20 | 0x20 | veh15 wait=0 | veh18 wait=1 | **veh15** |
| `:2870` | 1456,1874 | 0x1 | 0x1 | veh18 wait=1 | veh15 wait=0 | **veh15** |
| `:2964` | 1455,1874 | 0x11 | 0x1 | veh18 wait=1 | veh15 wait=0 | **veh15** |

结论句：
- 5 格的仲裁输出**全部等于查询者 veh15**，其中 3 次 `a=veh18 wait=1` 排在首位仍被改判（`:2846/:2870/:2964`）。
- 改判的依据是 `station=0`（非站台）走「取非等挂者」分支，而查询者恒为 `wait=0` 的挂接机车 ⇒ 该分支在当前组合下**恒返回查询者**，不可能返回 veh18。
- 直接后果：`:2847/:2855/:2864/:2871/:2965` 连续 5 条 `RESV-NOLOOK-PASS`，`foreign=0` ⇒ 吃 `early-haverserved` 捷径，不查该格是否被别人占用/预留，以 `spd=160` 逐格通过。

### 6.2 第 6 格反转（1455,1873）：位在、是自己订的、却判成「无主且非自己」

| 日志行 | 读数 |
|---|---|
| `:3046` | `CPL-RES-OK veh=15 total=20 booked=20 ... tiles=1455,1874/4;1455,1873/5;1454,1873/0;...` ⇒ **1455,1873 的 track 5 是 veh15 自己刚订的** |
| `:3048` | `CPL-RET veh=15 tile=1455,1874 enterdir=0 tracks=0x11 res=0x1 ret=0x10 why=nores-final` ⇒ 1455,1874 要的 0x10 位没订到（该格只 reserved=0x1），路径作废重试 |
| `:3056` | `CPL-NEXT ... to=1455,1873 ... bits=0029 resv=0020 wholeOwner=-1` ⇒ 位在（resv=0x20），整格主人查不出（-1） |
| `:3057` | `RESV-EXT-PRE veh=15 tile=1455,1873 enterdir=3 tracks=0x20 reserved=0x20` |
| `:3058` | `RESV-FOREIGN-RES veh=15 tile=1455,1873 enterdir=3 res=0x20 tracks=0x20 spd=160` ⇒ **判成别人的** |
| `:3060` | `RESV-HAVERES ... tile=1455,1873 ... foreign=1 nolook=1` |
| `:3132` | `CPL-RES-FAIL veh=15 nth=0/19 tile=1455,1873 td=13 want=5 booked-before=0 failtd=13 tbits=41 res=32 plain=1 st=0 depot=0 owner=-1 selfheld=0` ⇒ **owner=-1 且 selfheld=0** |
| `:3135` | `CPL-NOOWNRESV veh=15 ... reason=no-own-reservation` |
| `:3150/:3151` | `SPD-DROP veh=15 ... prev=160` / `STUCK veh=15 tile=1455,1873 spd=0` |

结论句：
- 1455,1873 的预留位 `0x20` 确实存在，而且是 veh15 自己在 `:3046` 订下的，但归属查询返回 `owner=-1`（走查不到任何车），`selfheld=0` ⇒ 上层把「查不出主人」直接等价于「不是我的」⇒ `foreign=1` ⇒ 挂接寻路要求自有预留 ⇒ 失败停车。
- 与 6.1 对照：同一列车同一条路，前 5 格「能查出 veh18 和 veh15 两端」时判给自己，第 6 格「一端走查断链」时判给别人。⇒ 归属结论取决于走查能否撞上车，而不是取决于位是谁订的；订位账本（CPL-RES-OK tiles）与归属查询（GetTrainForReservationArbitrated）互相矛盾，`:3046` vs `:3132` 就是同一格的直接反例。

### 6.3 为什么 5 格会「两端都有车」

- veh15 的挂接寻路终点是 `1424,1874`（`:2550-2561` `PFD-MEETMISS ... meet=1438,1874`、`:1981` `CPLRES ... destTile=1424,1874`），而 veh18 就停在 `1424,1874` 一带（`:2471` `TPR-BLOCKED veh=75 ... other=18 otherTile=1424,1874 otherFirst=18 caronly=1`）。
- 因两列车在同一条 1874 轨道走廊上首尾相接，沿 veh15 自己的预留链走查必然走到 veh18 ⇒ 每次仲裁的 `a`/`b` 必然是「veh18(等挂) + veh15(查询者)」这一对，而不是第三方。
- ⇒ 这不是偶发误判，而是「挂接机车 + 目标车底同走廊」这一组合下的**结构性恒判**。

---

## 七、为什么没有一次性预留到安全位（第 524 轮，玩家新问）

问题：veh15 从 `(1474,1884)` 出发执行 `GOTO_COUPLE`，为何不一次性把预留铺到安全位，而是"边走边铺"？

### 7.1 结论

**不是"铺了又被切碎"，而是"一次都没铺成"**。挂接寻路的整条路径在安全位判据处整体作废，一格预留都没订下来（`TRP ok=0 res=0`）；此后列车没有任何"整段预留"，只能靠 PBS 的逐格机制（`TrainController` 每进一格订一格）前进。外观是"边走边铺"，本质是"裸走 + 每格现场订一格"。

### 7.2 日志链（`build\R3R_debug.log`，tick≈41323，veh15）

```
:2580 CPL-HEAD-STATIC veh=15 best=1424,1874 snap=1438,1874 meetvalid=1 meet=1438,1874
:2581 CPL-BEST        veh=15 tile=1424,1874 td=0 cost=20520
:2582 CPL-BACK        pPrev=1424,1874 td=0 parent=1442,1874 ptd=0
:2583 FSCP            tile=1424,1874 skip=multihead-blocked n=2 h=18,75 hasT=1
:2584 CPL-SAFE-FAIL   veh=15 tile=1424,1874
:2585 CPL-BLOCKED-WAIT veh=15 tile=1474,1884 dest=1431,1874
:2586 TRP veh=15 ... mstuck=0 fto=0 ... => ok=0 res=0
```

⇒ 寻路本身是成功的（`CPL-BEST tile=1424,1874`），失败发生在**回溯安全位**这一步：`FSCP` 在目标格 `1424,1874` 上数到 **2 个链头**（`h=18,75`）⇒ 判"不安全" ⇒ `return false` ⇒ 整条挂接路径被否。

随后列车没有停车，而是逐格通过：

```
:2626 CPL-NEXT     veh=15 ... to=1473,1884 ... bits=0001 resv=0000 wholeOwner=-1 spd=41
:2627 RESV-HAVERES veh=15 tile=1473,1884 res=0x0 ... nolook=1
:2628 RESV-SIGPASS veh=15 ... track=0 dir=0 doResIn=0 spd=41 reserved=0 ownRes=0 coType=16
:2629 CPL-SKIP     veh=15 ... retryIn=2
```

⇒ 逐格推进时该格 `resv=0000`（没有整段预留）、`reserved=0`（连这一格也没订），仍以 `spd=41` 通过。

### 7.3 代码锚点

- `src\pathfinder\yapf\yapf_rail.cpp:330-346`：`n_tile_heads >= 2` ⇒ 打 `FSCP ... skip=multihead-blocked` 并 `return false`（KI-562 口径「多链头 ⇒ 本格不是安全位置 ⇒ 挂接寻路摸不到终点 ⇒ 机车原地不动」）。
- `src\train_cmd.cpp:~18896`：`DoTrainCouplePathfind(consist, true)`，注释自陈「always reserve the couple path — the loco needs a reservation to move, and it must extend right up to the consist. Without this the loco only reserves on force_res ticks and then moves without one.」
- `src\train_cmd.cpp:~18900-18940`：`CPL-SAFE-FAIL` 且 `_r3r_couple_path_blocked` 为真时只打 `CPL-BLOCKED-WAIT` 并**不降级普通寻路**（KI-516 口径），但也没有把"预留失败"转成"停车"。
- `src\train_cmd.cpp:18655-18670` 一带（`RESV-SIGPASS`）：注释明说「灯已变绿、这一格的预留还没订，真正的路径预留要等下面的 `ExtendTrainReservation()`」。

### 7.4 与"归属紊乱"的关系

两件事同源但不是一个 bug，叠加后才是"160 冲进去再卡住"：

1. **安全位判据失败**（本节）= 没有整段预留 ⇒ 车只能逐格裸走。
2. **归属裁决紊乱**（§二/§六）= 逐格裸走时，每格的 `GetTrainForReservationArbitrated` 都判 `foreign=0` ⇒ 每格都绿灯 ⇒ 一路 160，直到 `1455,1873` 判 `foreign=1` 才停。

若只有 (1)，车会慢慢蹭到车底前停住；若只有 (2)，车会带着整段预留撞上去。两者同时成立才出现"满速冲过 5 格、第 6 格急停"。

### 7.5 修法候选（未落地）

- **A（治 (1)，最小）**：把"终点"与"安全位"分开 —— 多链头格若含配对目标，可作为路径**终点**，但回溯安全位应在目标前一格（`snap=1438,1874`，`CPL-HEAD-STATIC` 已经算出来了）开始判，而不是从 `1424,1874` 判。
- **B**：严格执行 KI-562 原意（多链头 ⇒ 机车原地不动）。但现场 veh75 被 veh18 挡死（`:2471 TPR-BLOCKED ... other=18 otherTile=1424,1874`），两台车互等 ⇒ 直接执行会死锁，需先解开 veh75/veh18 同格。
- **C（治 (2)，兜底）**：允许"无整段预留"行驶的路径必须每格重新咨询归属，一旦 `foreign=1` 立即停车，不得以 160 通过。

### 7.6 疑点（本轮新增，待确认）

- **U-1**：`1424,1874` 的 `h=18,75` 为何是**两个链头**。`:490 COUPLE-OK loco=75 rear=62 consist=18` 说明 veh75 曾挂上 consist=18，理论上应合并成一条链、`First()` 同一个；现场却数出两个 ⇒ 要么该次挂接后又被拆开，要么 `tr->First()` 对两者返回不同值（链未真正合并）。
- **U-2**：`CPL-SAFE-FAIL` 之后 `best_track` 取到什么值；`TRP ... mstuck=0` 是谁把 `mark_as_stuck` 传成 false ⇒ 决定了"预留失败为何不停车"。
- **U-3**：`EXT-ENTRY` / `EXT-WALK` / `EXT-RES` 全日志 **0 命中**。需确认当前 `build\openttd.exe`（2026-11-26 05:34，54 409 728 B）是否含这三个探针；若含而零命中，则 `ExtendTrainReservation()` 从未被调用，与"挂接 `path_found` 无效、无整段预留"的结论一致。

---

## 八、为什么不能"等 veh75 完全脱离再订整段预留"（玩家第 3 问，第 523 轮）

> 玩家原话：「但是为什么 veh15 不能等到 veh75 完全脱离再订整段预留」

（轮次口径：§七 标题里写的「第 524 轮」是笔误，本轮 = 523。）

### 8.1 一句话

**它确实在等 —— 每个 tick 都重试一次 —— 但它没有把"这次没订上"翻译成"停车"，所以"等"退化成"边等边开"；等车开出去 20 格以后，它等待的那个时机已经被自己开过去了。** 另外即使真的停住等，本现场也解不开（见 8.4 的 b、c 两层）。

### 8.2 第一层：代码里"等"= 每 tick 重试，不是"停车 + 条件触发"

- `src\pathfinder\yapf\yapf_rail.cpp:1829`：每次挂接寻路入口先把 `_r3r_couple_path_blocked = false` 清零；`:2037-2046` 在 `CPL-SAFE-FAIL` 处再置 true。⇒ 这是个**单次寻路**的作用域标志，不是"挂起的等待任务"。
- `src\train_cmd.cpp:18949`：`path_found == INVALID_TRACK && R3RCouplePathBlockedByThirdParty() && OT_GOTO_COUPLE` ⇒ **不降级普通寻路**，只打 `CPL-BLOCKED-WAIT` 就返回。返回之后**没有任何制动动作**（没有 `cur_speed = 0`、没有 `subspeed`、没有 `mark_as_stuck`）。
- 所以下一个 tick 又会从头跑一次挂接寻路（`CPL-SKIP ... retryIn=2` 只是节流），车该走还走。
- 唯一会真正"停车等人"的分支是 `src\train_cmd.cpp:20114-20148` 的 `R3R-RES-ONCE hold`，前提 `HasForceReserveOnce()` + `OT_GOTO_COUPLE` + `R3RCouplePathBlockedByThirdParty()`。本现场该标志没被置起/已被消费：`:2586 TRP veh=15 ... mstuck=0 **fto=0** ... => ok=0 res=0` ⇒ 判据不成立 ⇒ **hold 分支根本没进**（U-6）。

### 8.3 第二层：现场读数证明"等"被自己开过去了

`:2585 CPL-BLOCKED-WAIT veh=15 tile=1474,1884 dest=1431,1874` 之后，车并没有停，而是逐格推进 30+ 行：

```
:2626 CPL-NEXT veh=15 ... to=1473,1884 ... bits=0001 resv=0000 wholeOwner=-1 spd=41
:2629 CPL-SKIP veh=15 ... retryIn=2
:2632/:2637/... LOCO-MOVE veh=15 ... 1473,1884 → 1472,1884 → 1472,1883 → 1471,1883 → … → 1457,1875
:speed 41 → 58 → 138 → 142 → 160
```

⇒ 它一边"每 tick 重试挂接寻路"，一边以 `spd=160` 沿走廊推进。等它开到 `1457,1875` 时，挂接寻路的**起点**已经变了，`1424,1874` / `1438,1874`（`:2580 CPL-HEAD-STATIC snap=1438,1874`）早已被越过。所谓"等 veh75 脱离"在物理上不存在可等待的时刻点 —— 它是行驶中重试，不是原地守候。

### 8.4 第三层：即使停住等，本现场也解不开

**b. ~~veh75 本身也被挡死 ⇒ 等 = 三方死锁。~~【本条已订正作废，见 §九 9.1】** 原据 `:2471 TPR-BLOCKED ... other=18` 判"veh75 被 veh18 卡死"，但 `:490 COUPLE-OK loco=75 rear=62 consist=18` 证明两者**同属一条链**，veh75 随时可带走整链 ⇒ 无死锁，`1424,1874` 可以让出来。

**c.【本条已降级为待验证，见 §九 9.2】** 车推进到走廊尾端后，同一套探针打出的是另一类失败（注意：以下读数**全部采于车已开到 `1455,1873` 之后**，不能反推"一开始就停住等"也会如此）：

```
:3207 FSCP tile=1447,1874 fail=tryReserve td=2 want=2 type=1 tbits=0x5 res=0x1 plain=1 depot=0 st=0 **self=0**
:3208 FSCP-RES tk=0 **owner=-1**
:3209 FSCP-REACH-NONE ... (no chain reaches this tile along a contiguous reservation)
:3210 FSCP-CPL veh=15 pos=1455,1873 **spd=0**
:3211 CPL-SAFE-FAIL veh=15 tile=1442,1874
:3212 CPL-BLOCKED-WAIT veh=15 tile=1455,1873 dest=1437,1874
```

`self=0`（这一格是不是"我自己"判不出来）、`owner=-1`（预留的主人找不到）、`REACH-NONE`（沿连续预留回溯不到任何链）—— 这三项**与"格上有没有别人"完全无关**，是 §二/§六 描述的**归属紊乱**本身：位在、`res=0x1` 非空，却认不出主人。veh75 走不走开，这一层都不变。⇒ **"何时订"不是变量，"订了还算不算自己的"才是。**

### 8.5 结论 / 修法含义

| 层 | 现象 | 是否"等 veh75"能解 |
|---|---|---|
| a | 重试但不停车 | 否（等没有物理载体） |
| b | ~~veh75 被 veh18 挡死~~ | **撤回**（两者同链，veh75 可走） |
| c | `self=0 / owner=-1 / REACH-NONE` | **待验证**（读数采于越位后，不能反推停住等） |

⇒ 正确顺序应当是（已按 §九 订正）：

1. **（订正）先治 (a) 做实验**：把 `CPL-BLOCKED-WAIT` 接上制动（复用 `R3R-RES-ONCE hold` 或直接清 `cur_speed/subspeed`）。死锁风险已随 b 撤回而解除，可直接做。
2. **再修 (c)**：`FSCP-RES owner=-1` + `self=0` 是独立缺陷（自己订的位回溯不到自己），与是否停车无关，最终必须单独收口。
3. **最后治"整段预留"本体**：§7.5 修法 A（终点 / 安全位分离，回溯从 `snap=1438,1874` 起判，而不是从 `1424,1874` 判），这才是"一次性铺到车底前"的正解。

### 8.6 新增疑点（按"有疑点即落盘"规则登记）

- **U-4**：`:2583` 的 `skip=multihead-blocked`（`n_tile_heads>=2`）与 `:3207` 的 `fail=tryReserve` 是两类完全不同的失败（一个是"不安全位置"，一个是"订不上/认不出主人"）。需确认它们是否发生在同一条回溯路径的不同帧，还是不同 tick 的不同寻路。
- **U-5**：`:3210 FSCP-CPL ... spd=0` 说明这一刻车**确实停了**；谁让它停的（撞上 veh18 车底？`CPL-NEXT` 失败？）未取数，与 §7.6 U-2 的 `mstuck=0` 一并查。
- **U-6**：`fto=0` 在本现场恒成立 ⇒ `R3R-RES-ONCE hold` 对该场景是**死代码**；需判定是"判据过严（`HasForceReserveOnce` 不该是前提）"还是"该标志本应被置起而漏置"。
- **U-7**（承 U-1）：若 `:490 COUPLE-OK loco=75 rear=62 consist=18` 之后 veh75 与 veh18 真属同一条链，则 `h=18,75` 的 `n_tile_heads=2` 本身就是**链身份错误**的产物，而不是几何事实 ⇒ 这会进一步说明"多链头判据挡路"其实是归属错误的第二次显形。

---

## 九、§八 第三点订正（玩家驳斥，第 523 轮续）

> 玩家原话：「你的第三点不成立，veh75完全可以脱离，然后第一二点我不明白，用大白话讲一下」

### 9.1 撤回：b「死锁」不成立

§8.4-b 用 `:2471 TPR-BLOCKED … other=18 otherTile=1424,1874` 推出"veh75 被 veh18 卡死 ⇒ 纯等待是死锁"，**结论错误**，撤回。

玩家是对的，日志本身也给了反证：`:490 COUPLE-OK loco=75 rear=62 consist=18` ⇒ **veh75 已经挂上了 consist=18**，两者是**同一条链**（同一列车），不是"两台车互相挡"。veh75 只要想走，整链一起走，`1424,1874` 立刻空出来。"`other=18` 挡着"只是它**当时**停在那里，不是它走不了。

⇒ 连带作废的推论：§8.5 表格 b 行、§8.5 第 2 点里"会把 (b) 的死锁固化"这句。**给 `CPL-BLOCKED-WAIT` 接制动的死锁风险实际不存在**（至少本现场不存在），该修法的风险等级从"必须先解开同格才能做"下调为"可直接做实验"。

### 9.2 降级：c「停住等也解不开」证据不足，改为待验证

`self=0` / `owner=-1` / `REACH-NONE`（`:3207-:3210`）这批读数**全部采于车已经开到 `1455,1873` 之后**（`:3210 FSCP-CPL veh=15 pos=1455,1873 spd=0`）。这是"车先裸跑了 20 格"这个前提下的另一种失败形态，**不能反推**"假如它一开始就停住等，也会这样"。

⇒ 本轮**没有任何一条"停车重试"的样本**，所以"停住等能否自愈"在证据上是**未测**，不是"已证伪"。登记为 U-8。

### 9.3 订正后的结论

不是「等也没用」，而是「**它从来没真正等过**」。§八的 a（只写日志不制动）与 b（等待窗口被自己开过去）成立且互为因果；c（等也解不开）撤回为待验证。

⇒ 修法顺序相应改为：
1. **先做最小实验**：把 `CPL-BLOCKED-WAIT` 接上制动（或复用 `R3R-RES-ONCE hold`），让它在 `1474,1884` 真的停住。风险已解除（9.1）。
2. 复跑同一场景，看停住之后 veh75 走开的那一 tick，挂接寻路能否**一次订成**（`TRP ok=1 res=1` + `CPL-PF-FALLBACK` 消失 + 出现 `EXT-*`/长预留行）。
3. 若仍订不成，再查 §7.5 修法 A（终点/安全位分离）与归属紊乱（§二/§六）——这两条与"是否停车"无关，最终都要修。

### 9.4 新增疑点

- **U-8**：**从未采样到"停住重试"的过程**。现有全部 `FSCP` / `FSCP-RES` / `FSCP-REACH-NONE` 读数都在行驶态（`spd>0`）或越位后（`pos=1455,1873`）。需装刹车后重采，才能判定停住等能否自愈。
- **U-9**：撤掉 b 之后，`n_tile_heads=2`（`h=18,75`）与 `:490 COUPLE-OK loco=75 consist=18`（两者同链）**互相矛盾**：同一条链不该数出 2 个链头。U-7 从"可能"升级为本现场**必须解释**的矛盾——要么链没真正合并，要么 `First()` 在 veh75/veh18 上返回不同对象。这条与"能不能订整段"直接相关：若链身份是错的，多链头判据挡的是一个**假障碍**。

---

## 十、玩家拍板口径：订不到整段 ⇒ 必须刹车等待（第 523 轮续三）

> 玩家原话：「如果订不到整一段，必须刹车等待。你认为这个能解决第一第二点吗」

### 10.1 口径升级（已拍板，不再是"建议做实验"）

「整段预留失败 ⇒ 制动等待」由玩家定为**设计口径**。§9.3 第 1 条从此由"最小实验"升为"必须实现的语义"。§8.5 第 2 点里"必须先给例外否则死锁"的前提已随 §9.1 撤回（veh75 无死锁），但仍需超时退避（见 10.5）。

### 10.2 对第一点（有重试、无制动）：**完全解决**

第一点说的就是"没装刹车"。装上了就是治它本身，无残留。停住后每 tick 从**同一格**重试，才构成真正意义的等待。

### 10.3 对第二点（等待窗口被自己开过去）：**只解决一半，且只解决"起点漂移"这一半**

第二点有两个成分：
- **(i) 窗口漂移**：起点每 tick 前移，`snap=1438,1874` 被越过。
- **(ii) 窗口里能不能订成**：取决于"窗口出现那一刻，失败原因是否已经消失"。

刹车对 (i) **必然解决**（起点钉死）。对 (ii) **不保证**——因为现场至少三类失败原因，刹车只对前两类有效：

| 失败原因 | 现场证据 | 刹车 + veh75 走开后 | 结论 |
|---|---|---|---|
| A 目标格被别的车占 | `CPL-BEST 1424,1874` 上停着 consist=18 | 格空了 ⇒ 消失 | **能解** |
| B 多链头判据挡路 | `:2583 skip=multihead-blocked`（`h=18,75` 数出 2 个链头） | 那格清空 ⇒ `n_tile_heads` 归 0/1 ⇒ 判据放行 | **能解，但依赖 veh75 真的开走** |
| C 归属紊乱 | `self=0 / owner=-1 / REACH-NONE` | 与格上有没有车无关 | **不能解** |

⇒ 一句话：**刹车把"不存在的时刻"变成"存在的时刻"，但不负责保证"那个时刻能成功"。** C 那一层最终仍要单独修（它也是 §二/§六 的主题）。

### 10.4 最大风险：veh75 "能走" ≠ "会走"（U-10）

§9.1 只证明了 veh75 **有能力**脱离（同链、可带走整链）。但**它自己要不要走**没取数。两种可能：

- 若 veh75 的订单是正常 GOTO_STATION/DEPOT（它已有 consist=18，任务已完成挂接）⇒ 会自己开走 ⇒ 刹车方案成立。
- 若 veh75 的订单是 GOTO_COUPLE / WAIT_COUPLE 且目标指向 veh15 ⇒ **双方互等**，刹车把"漂移"换成"永久死锁"，比现状更糟。

⇒ **这一条没验证前不得单独上刹车**，必须先看 veh75 的 `current_order`。

### 10.5 建议的落地形态（三条缺一不可）

1. **制动**：`src\train_cmd.cpp:18949` `CPL-BLOCKED-WAIT` 分支里清 `cur_speed` / `subspeed`（或复用已有 `R3R-RES-ONCE hold`，见 U-12）。
2. **超时退避**：停住 `N` tick 后强制放行、恢复行驶。理由：(a) 防永久死锁（10.4）；(b) 防挡路（U-11：`1474,1884` 后面可能有来车）；(c) 防"配置错误导致全线永久停摆"。`N` 建议取现场 veh75 离场耗时的 2~3 倍（需实测，暂取 600 tick 量级）。
3. **豁免**：配对目标就在本链**正前方同一走廊**且中间无第三车时不停车（否则变成"我停下来等我自己"）。这条在 §8.5 因死锁顾虑提出过，死锁理由虽已撤回，但"自身正前方无第三方障碍"这个豁免仍然必要。

### 10.6 新增疑点

- **U-10（阻塞性）**：veh75 的 `current_order` / `cur_speed` / 是否 GOTO_COUPLE 指向 veh15 —— 决定刹车是"解药"还是"永久死锁"。**必查**。
- **U-11**：veh15 停在 `1474,1884` 是否挡住后续车（该格是否为单线走廊的必经点）。
- **U-12**：已有 `R3R-RES-ONCE hold`（`src\train_cmd.cpp:20114-20148`）本身就是"清速度 + 等"的现成实现，能否直接把它从 `HasForceReserveOnce()` 前提下解放出来复用，而不是在 `18949` 另写一段刹车。若能复用，改动量最小、语义最一致。
- **U-13**：超时退避的 `N` 该取多少——需实测 veh75 从开始占格到离场的时间。

---

## 十一、玩家四条新口径（第 523 轮续四，2026-11-26）

> 玩家原话：「第二点iiC，预留紊乱我指的是一条预留上踩着三条链，两条链的情况是不紊乱的。对于你来说不考虑就是了 / veh75你不用担心，它就是会走开的。/ 我不要超时退避 / 然后刹车能走真实刹车吗，就是玩家开了真实制动就走慢慢刹车，没开就走原版的反物理刹车」

### 11.1 C 类重定义 ⇒ §10.3 表 C 行作废，本现场只剩 A/B

玩家口径：「预留紊乱」专指**一条预留上踩着三条链**；只有两条链（自己 + 一条被踩的链）属正常占格，**不是紊乱**。⇒ 本 AI **不按 C 类处理本现场**。

连带结论（重要，收敛了 §10.3）：
- §10.3 的 (ii)「窗口里能否订成」只剩 **A（格被占）/ B（多链头判据）** 两类。
- 这两类都随 veh75 离场而消失（A 格空、B 该格链头数归 0）。
- ⇒ **刹车即足**：只要车停住、veh75 会走，窗口出现那一刻就能订成。§9.2 的 U-8（停住能否自愈）在逻辑上已被判定为「能」，只剩实机确认。

### 11.2 U-10 关闭（veh75 会走开）

玩家确认「veh75 不用担心，它就是会走开的」。⇒ §10.4 列的**永久死锁风险正式解除**，u-10 从「阻塞性必查」改为「已关闭」。
⇒ **刹车可单独落地，无前置条件、无需先查 veh75 的 `current_order`。**

### 11.3 超时退避作废（玩家明确不要）

§10.5 第 2 条**作废**。刹车语义定为**无限等待**：一直停到订得成整段为止。

连带：
- §10.5 第 3 条「正前方同走廊豁免」也随之**不再必要**——它原本只用于防「永远订不成 ⇒ 永久停摆」，而该顾虑已被 11.1（只剩 A/B）+ 11.2（veh75 会走）消掉；且「整段预留」的起点就是自己脚下，自己不会挡自己。⇒ 实现简化为**纯刹车**，不加豁免、不加计数、不加退避。
- 10.6 的 U-13（超时 `N` 取值）作废。

### 11.4 真实刹车要求（玩家新需求，决定实现形态）

- 玩家开了 **真实制动**（realistic braking / 真实加速模型）⇒ 要**渐进减速**，不许瞬间归零（否则是反物理）。
- 玩家没开 ⇒ 走**原版的反物理刹车**（瞬时清零），与原版行为一致。

⇒ 实现必须按加速度模型分叉，不能一边倒清 `cur_speed`。

**取证结论（见 11.5，已查证）**

### 11.5 代码落点取证

> **订正**：本节上一版本写的「`R3R-RES-ONCE hold` 里是 `cur_speed = 0; subspeed = 0;`」**是我凭记忆写错**，实测该段（`:20134-:20152`）**完全不动速度**。已按实读改写。

**A. `R3R-RES-ONCE hold`（`src\train_cmd.cpp:20134-20152`）不是刹车，它只"不放行、不消费"**

实读结论：该分支的全部动作 = 节流后打一行 `R3R-RES-ONCE hold head=… why=couple-path-blocked`，**没有一行碰 `cur_speed/subspeed`**。它的语义是：

- 不把 `force_proceed` 顶到 `TFP_SIGNAL`（不越过红信号），本次挂接寻路按普通规则跑；
- 不消费 `ForceReserveOnce`，把一次性能力留给障碍离开后真正需要的那次（`:20121-:20126`）。

车之所以停住，是因为寻路被挡后 `TRP ok=0 res=0` 由引擎 `MarkTrainAsStuck` 原生惩罚，**不是 R3R 主动刹的**。

⇒ **不能拿它当"停住等整段"的实现**：本现场的病灶恰恰是"寻路偶尔成功一格就把车推走"（`:20113-:20115` 注释的 KI-516 现场 `R3R-RES-ONCE used → TRP ok=1 res=1 → STUCK state=0` 被推走一格），而 hold 分支既不清速度也不阻止"下一次成功"，挡不住这个。

**B. 现成的「停住」形态在第 321 轮 KI-499（`:1965-:1977` 注释记载）**

> 落点：`TrainLocoHandler()` 的速度计算块 —— 判据成立即 `cur_speed = subspeed = 0` 且**不调 `UpdateSpeed()`**，令 `j = 0 < adv_spd` 走原生停驶分支（不移动、不扩预留，但仍会跑 `CheckTrainCollision()` / `TrainCoupleHandler()`，所以停在目标旁边仍能挂上）。

⇒ 这个形态**恰好就是本需求要的"停住等"**：停驶分支每 tick 仍然尝试挂接命中，速度恒 0，起点不漂移。**但它是瞬停** ⇒ 只满足"真实制动关"那一支，真实制动开着时属于反物理。

**C. 真实制动的判据是 `Train::UsingRealisticBraking()`（现成函数）**

- `src\ground_vehicle.cpp:357` 已经在用：`bool using_realistic_braking = Train::From(this)->UsingRealisticBraking();`
- `src\train_cmd.cpp:1992` 的 `realbrk=` 探针同源。
- ⇒ 我上一版写的「用 `AM_REALISTIC` 判」**也是错的**：`AM_REALISTIC` 是真实**加速模型**，`UsingRealisticBraking()` 才是真实**制动**，两者在代码里被分开处理（`:1988-:1992` 注释明说「AM_REALISTIC 非 realistic braking 时…」）。

**D. 「慢慢刹」引擎里已经有了，不用我手写减速曲线**

`src\ground_vehicle.cpp:504-521`（`DoUpdateSpeed`）：

```504:521:src/ground_vehicle.cpp
	if (this->cur_speed > max_speed) {
		if (use_realistic_braking && accel.braking >= 0) {
			...
			brake_overheated = true;
		}
		tempmax = std::max(this->cur_speed - (this->cur_speed / 10) - 1, max_speed);
	}
```

⇒ 真实制动下 `cur_speed` 每 tick 按 `cur_speed - cur_speed/10 - 1` 递减（约 -10%/tick），`max_speed=0` 时会一路降到 0。**这就是"慢慢刹"**，是引擎既有行为。

**E. 已知坑：只压 `max_speed` 会停在 2 的蠕行**

`src\train_cmd.cpp:1967-1971` 有现场记录（第 321 轮 KI-499 的踩坑原文）：

> `Train::UpdateSpeed()` 在 AM_REALISTIC 且**非刹车**时把 `min_speed` 传成 2（`src\ground_vehicle.cpp:525-532`，源码注释原文 "min_speed overrules the maximum speed"——先 min(上限) 再 max(下限)，下限后执行），故上限 0 只能压到 2，现场表现为 backoff 期 actSpd 恒 2 的蠕行。

⇒ 真实制动支**不能**简单 `max_speed = 0` 了事：必须让它走「刹车」路径（`min_speed` 传 0）而不是「非刹车」路径。确切放手点未核，登记 U-14。

**F. 现成可参照的"停车类目标"处理**

`src\train_cmd.cpp:1594-1597`：停车类目标（`end_speed == 0`）时把上限压到 `REALISTIC_BRAKING_MIN_SPEED`(=5，`:84`），让车继续减而不被"地板 15"拦住——这是"路点停车"已在用的真实减速写法，可作为本需求的参照模板（同族逻辑，不用新造）。

### 11.6 实现口径（据 11.1~11.5 收敛）

> **2026-11-26 订正（落地时）**：本节的判据原先写成 `train_acceleration_model`，**错**。真实制动是独立开关
> `train_braking_model == TBM_REALISTIC`，现成读法就是 `Train::UsingRealisticBraking()`
> （`src\train.h`：`tcache.cached_tflags & TCF_RL_BRAKING`），与加速模型无关；四种组合都可能出现。
> 落点也不是 `CPL-BLOCKED-WAIT`（那只是节流日志），而是 `TrainLocoHandler()` 里的
> `R3RShouldWaitForCouplePair()` 分支（本次改动前 `src\train_cmd.cpp:24935`）。

- `UsingRealisticBraking() == false` ⇒ `cur_speed = 0; subspeed = 0; j = 0;`（原版反物理，逐字对齐原版语义）
- `UsingRealisticBraking() == true` ⇒ **不清零**：把本次 `MaxSpeedInfo` 两字段压成 0 交给
  `Train::UpdateSpeed()`，走引擎自己的真实制动步长（`GroundVehicle::DoUpdateSpeed()` 的
  `cur_speed > max_speed` 分支：`tempmax = max(cur_speed - cur_speed/10 - 1, max_speed)`，每 tick 约 -10%）；
  唯一补丁是最后 2 km/h —— Realistic 加速模型下 `GetAccelerationStatus()` 此刻是 `AS_ACCEL` ⇒ `min_speed = 2`，
  引擎停不到 0，故 `cur_speed <= 2` 时清零并 `j = 0`
- 无超时、无豁免、无计数（11.3）

### 11.7 疑点

- **U-14**：真实模型下"阻止前进但不改速度"是否会与 `TrainController`/`ChooseTrainTrack` 的既有刹车距离判定打架（例如引擎认为还能走、R3R 认为必须停）——需在实现时确认放手点，避免出现"速度不降但车不动"或反之。
- **U-15**：`R3R-RES-ONCE hold` 那段是否已有 `AM_REALISTIC` 分支（若有，直接复用即可，改动更小）；本轮取证只读到硬清零，需再确认上下 20 行。

### 11.8 落地（2026-11-26，「现在落地吧」）

只改 `src\train_cmd.cpp`，未碰任何 `src\*.h`（凑 KI-15 增量合规），三处：

1. `R3RDbgEdgeTag` 新增 `R3REDGE_BRAKEHOLD`（插在 `R3REDGE_EXTEXIT` 之后、`R3REDGE_COUNT` 之前）：
   key = 车号、payload = `cur_speed` ⇒ 刹车曲线的每个台阶留一行，匀速段不产生行。
2. `CPL-HOLD-NATIVE` 行尾追加 ` braking=%d`（`UsingRealisticBraking()`），字段顺序与既有字段全部不变。
3. `TrainLocoHandler()` 的 `R3RShouldWaitForCouplePair()` 分支按 11.6 分两支：
   - 真实制动支：`cur_speed != 0` 时 `j = consist->UpdateSpeed({0, 0})`，随后 `cur_speed <= 2` 则清零 + `j = 0`；
     已停住（`cur_speed == 0`）时**不再调** `UpdateSpeed()`，否则它会每 tick 把 0 抬回 2、读数抖动。
     刹车期间 `j` 仍可能 `>= adv_spd`，于是照原生移动分支滑行一格（这是"慢慢刹车"应有的位移），
     该分支照旧跑 `TrainController()` / `TrainCoupleHandler()`，锁一建立判据立刻为假。
   - 非真实制动支：原样 `cur_speed = subspeed = 0; j = 0;`。
   探针：真实支每档速度变化写 `CPL-BRAKE-WAIT veh= tick= spd= tile= order=`。

**未解决/遗留**：U-14 未实测（刹车期进入移动分支是否与 `TrainController` 相互干扰）；
`ShowVisualEffect` 两支都不调（与改动前的 hold 分支一致，刹车期间无刹车烟）。
**复测判据**：①开真实制动时同一现场日志出现 `CPL-HOLD-NATIVE ... braking=1`，随后 `CPL-BRAKE-WAIT` 的
`spd` 逐档下降（每档约 -10%）直到 0，而不是首行即 0；②关真实制动时仍立即 `spd=0` 且无 `CPL-BRAKE-WAIT`；
③真实支刹停后不再有速度读数抖动（0/2 交替）；④veh75 离开那一 tick 能一次订成整段，日志出 `TRP ok=1 res=1`
与后续 `CPL-PAIRSTATE state=locked`，不需再靠"重试窗口"。

**构建自证（2026-11-26 21:59）**：入口 `R3R_fullbuild.cmd inc detach`（既有脚本，未新建任何 `.cmd`）；
护栏 `build\R3R_incbuild.guard.log` = 「GUARD: incremental is safe」；日志 5 步
`[4/5] Building CXX object … train_cmd.cpp.obj` → `[5/5] Linking CXX executable openttd.exe`；
`build\R3R_incbuild.done` = `EXIT_CODE=0`（21:59:27）；`src\train_cmd.cpp` 21:54:47 →
`train_cmd.cpp.obj` 21:57:28 → `build\openttd.exe` 21:59:21；`read_lints` 0 条；
`findstr /m /c:CPL-BRAKE-WAIT build\openttd.exe` 命中（新探针字面量已进二进制）。
状态＝**已实现 + 已编译**，游戏内复测待做。

