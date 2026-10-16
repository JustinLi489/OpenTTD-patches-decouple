# 第 228 轮临时分析报告：KI-334 双头成对探针 + KI-335 解挂启动互锁

- 轮次：第 228 轮（2026-10-10）
- 前置：第 227 轮复测通过（`R3R_clone_segment_orders_round227_memo.md` §七），按玩家「假如通过，请慢慢修复我之前报告的那一串问题」的指令继续清第 223 轮五问清单。
- 本轮范围：**KI-334（补只读探针）+ KI-335（实现）**；KI-321 过期描述清理已在第 227 轮收尾（本轮只复核）。
- 改动面：**仅 `src\train_cmd.cpp`**（未碰任何 `src\*.h`，故增量构建合法）。
- 硬性约束遵守：未新建任何 `.cmd`；未启动游戏（需要跑日志时叫玩家）。

---

## 一、KI-334：双头机车"错位"——本轮只补日志面

### 1.1 为什么不能直接改码
玩家原话是"似乎会出现机车错位"（2026-10-08 第 3 条），**没有现场日志**。KI-334 条目已登记的机制假设是：

> 最可能机制 = 逻辑翻转 / 间距重排路径不识别双头两半的成对关系，在链头落在双头组内、或段边界切在双头两半之间时，把两半翻成不一致朝向 / 把对接间距当普通缝写坏。

读码复核后，这个假设的**代码事实部分逐字成立**：

- `R3RReverseChainDirections()` 对链上**每一节无条件**做 `direction = ReverseDir(direction)` → `flags.Flip(Flipped)` → 逐节 `UpdateDeltaXY()`，**全文不含 `IsMultiheaded` / `IsRearDualheaded` / `other_multiheaded_part` 任何判断**。
- 对照：代码库在别处**确实知道**双头需特殊对待，例如 `SetSegmentTailFakeEngine()` 里 `if (last->IsRearDualheaded()) return;`。
- `R3RRespaceChainAfterEdit()` 逐对按 `CalcNextVehicleOffset` 的**名义间距**重排；双头对接是**刚性**的（两半是 `other_multiheaded_part` 焊死的一对）。

但"假设成立" ≠ "这就是现场那一次错位的成因"：双头两半若本来就朝向一致、翻转后仍一致，逐节翻转就**不会**出问题。缺的正是"日志能不能看出这两节是一对、翻转后是否还相邻且朝向一致"。既有日志面**看不到**这一点 —— `FOLD-V`/`FOLD-U` 只有逐节 `dir`，读者无法从一堆 `dir=5,dir=5,dir=1,dir=5` 里认出"哪两节必须焊死"。

### 1.2 本轮补的东西（三处、纯只读）

| 位置 | 标签 | 作用 |
|---|---|---|
| `R3RReverseChainDirections()` 末尾 | `DUALHEAD rev-dir-post` | **逻辑翻**出口：两半朝向是否仍一致、是否仍相邻 |
| `ReverseTrainSwapVehicles()` 末尾 | `DUALHEAD swapv-post` | **物理翻**出口（`ReverseTrainSwapVeh` 只换数据不重链，`v` 仍是链头，可走完整个链） |
| `R3RRespaceChainAfterEdit()` 末尾 | `DUALHEAD respace-post` | **间距重排**出口：区分"两半被分开"与"两半被拉长" |
| `FOLD-V`/`FOLD-U` 逐节 dump | 每行加 `mh=` `rdh=` `omp=` | 会话开头即可读出"哪两节是一对"（`omp` 无则 -1） |

`DUALHEAD` 行字段：

```
DUALHEAD <tag> front=<前半> pos=<下标> dir= flip= db= engType= segF= |
              rear=<后半> pos=<下标> dir= flip= db= engType= segF= |
              sep=<两半之间夹了几节> adj= <star=两半之间是否夹了★> |
              px=<前半像素> / <后半像素>
```

- 每对只从**引擎那一半**打一行；`other_multiheaded_part == nullptr` 时打 `orphan=1`（双头被拆散的异常形态）。
- `sep=0` 就是"紧贴"（`Next()` 或 `Previous()` 直接是伙伴）；`sep>0` 说明中间夹进了别的东西 —— **这本身就是错位的直接证据**。
- 边沿触发：key = `(tag, 前半车号, 后半车号)`，payload 含两半 `dir/flip/db/segF` + `star_between` + `sep+1`。所以**稳定不动的一对每 128 帧窗口只写一行**，而"朝向变了 / 不再相邻了 / 中间插了 ★"都会各自单独成行。

### 1.3 判读口径（等玩家日志）

1. `DUALHEAD rev-dir-post` / `swapv-post` 上若出现 `dir_f != dir_r` 或 `sep != 0` ⇒ **逻辑翻/物理翻确实破坏了双头刚性**，KI-334 的假设坐实，可以直接改码（按 `IsMultiheaded`/`IsRearDualheaded` 把两半当原子块一起翻）。
2. 若三处 `DUALHEAD` 全部 `adj=1` 且 `dir_f == dir_r`，而现场仍有错位 ⇒ 成因不在翻转/重排，改查渲染面（KI-214 / KI-98 / KI-106 的包围盒族）。
3. `FOLD-V`/`FOLD-U` 里出现成对的 `mh=1` 而 `omp` 指向的目标不在相邻位置 ⇒ 折叠修正提交后链序被写坏。
4. 注意 `R3RDbgEdge` 的 128 帧窗口抑制：**日志里没有 ≠ 没触发**。要确认某一路径跑过，看同 tag 的其它行或 `PERF` 行的 `dbgEdge=`/`skipped=`。

---

## 二、KI-335：解挂启动互锁（本轮实现）

### 2.1 玩家口径（2026-10-08 拍板，逐字）

> 第 4 条「两列动车组解耦之后几乎是同时启动的……我期望的是前车完全离开之后后车再启动」
> 解除条件用「**前车完全离开站台 / 驶离 N 格**」，**不采用**"前车离开 seam"的最简口径；实现时必须带**负判据**（正常追尾、同站两列车不得被误停）。

"前车离开 seam"被否掉是有道理的，本轮实现时也验证了这一点：**列车本身几十格长**，两半在"互不接触接缝"之后很久仍然互相挡着 —— 用"我顶着缝"当判据会在观感上毫无作用。

### 2.2 归属确定（不需要猜）

R3R 的**解挂永远剥链尾**（`DecoupleTrain()` 把尾段摘出来给 `u`，留下的 `v` 继续持原链头）。所以：

- **前车 = `v`**（留着原链头的那一半）
- **后车 = `u`**（解出的尾段，等待）

这与玩家"前车先走、后车再走"逐字一致，不需要任何方向推断。

### 2.3 数据结构

```cpp
struct R3RDecompSeam {
    VehicleID head;      // 前车（留链头的那半），仅用于日志
    VehicleID seam_car;  // 前车紧挨接缝的那一节 —— "必须离开站台的那一节"
    TileIndex seam_tile; // 它在解挂瞬间的格
    uint64_t  tick;      // 解挂时的 _tick_counter
    bool      reported;  // "wait" 行是否已写过（防 30s 刷屏）
};
static btree::btree_map<VehicleID, R3RDecompSeam> _r3r_decomp_seams; // 键 = 后车车号
static const uint     R3R_DECOMP_CLEAR_TILES = 4;    // "驶离 N 格"
static const uint32_t R3R_DECOMP_WAIT_LIMIT  = 900;  // 安全阀：30 s @ 正常速度
```

**NOSAVE、纯内存态**。键是后车车号，所以"另一列车恰好用了同一个车号"只可能发生在**同一对象**上；任何陈旧条目在第一次被访问时就命中 `tick` 超时判据被擦掉，不会误伤。

### 2.4 为什么锚点取 `kept->Last()` 而不是链头（一个容易写错的地方）

第一版想用"前车链头 `v` 与 `seam_tile` 的距离 ≥ N"当判据，**这是错的**：机车本来就在站台的另一端，`v->tile` 与接缝格的切比雪夫距离一解挂就已经是几十格（列车长度），会立刻误判"前车已经驶离"、互锁完全失效。

正确锚点是**前车那节紧挨接缝的车**（`v->Last()`，也就是解挂时贴着 seam 的那一节）—— 它必须**真的从接缝格挪走**，才说明"前车完全离开站台"。所以登记时把 `seam_car = kept->Last()` 与它的 `tile` 一起存下来，之后只跟这一节比对。

### 2.5 登记条件（作用域收窄，坐实负判据）

登记**只在**下列条件全部成立时发生：

1. `IsRailStationTile(follower->tile)` —— **解挂发生在站台上**。车库解挂有自己的排程（`OT_WAIT_COUPLE` 停驻门等），不登记；野外解挂没有站台可让，也不登记。
2. `!R3RIsCarOnlyFormation(follower)` —— 纯车底链**本来就动不了**（P7 的停驻门恒按住它），再锁一层没有意义。

### 2.6 判据与自清理（任一命中即撤销登记并放行）

```
① 后车自己 current_order 是 OT_WAIT_COUPLE，或它变成纯车底链
   ⇒ 它本来就不动，撤销登记（与下面 WAIT_COUPLE 停驻门同一口径）
② 后车已不在站台格
   ⇒ 说明互锁已经完成使命（它已经动了）
③ Train::GetIfValid(seam_car) == nullptr            // 被卖掉 / 被并走
   或 !IsRailStationTile(seam_car->tile)            // "完全离开站台"
   或 Chebyshev(seam_car->tile, seam_tile) >= 4     // "驶离 N 格"
   ⇒ 放行
④ _tick_counter - tick >= 900
   ⇒ 超时强制放行（前车若自己是个 WAIT_COUPLE 停驻者、永远不走，也绝不能把后车锁死）
⑤ 以上都不成立 ⇒ return true（本 tick 不启动），且只在首次打一行 DECOUPLE-INTERLOCK wait
```

③ 是**几何判据**，正是玩家要的口径："完全离开站台"（它那一节不再在站台格上）**或**"驶离 N 格"（N = 4）。两条都是几何、都不依赖"是否还顶着接缝"。

### 2.7 挂载点与理由

`TrainLocoHandler()` 里：

- **必须在 `HandleLoading()` 之后** —— 否则"装卸结束的那一 tick"会漏判（那一 tick 里后车刚刚具备起步条件）。
- **必须在 `OT_WAIT_COUPLE` 停驻门之前** —— 这样"后车自己就是等待方"时由等待方门接管、同时命中 ① 把互锁登记顺手撤销；若放在后面，互锁会把一个本来就不动的等待车底多锁一层（无害但会把登记留到超时）。
- 命中时 `cur_speed = 0; subspeed = 0; return true;` —— **与该处既有两道停驻门逐字同形**（`OT_WAIT_COUPLE` 门、纯车底链门），所以不会跳过任何必要的 tick 记账：到这一点时 `HandleLoading`/`LeavingStation`/`r3r_try_decouple_arrival` 都已经跑过，只是"不许起步"。
- `DecoupleTrain()` 末尾、`return u;` 之前登记。

### 2.8 负判据为什么天然成立（对"正常追尾/同站两列车"不进位的论证）

互锁**不是**"站台上有别人就停"，而是**查后车车号是否在这次解挂亲手登记的表里**：`_r3r_decomp_seams.find(consist->index)`。因此

- 正常追尾的后车 —— 不是 `DecoupleTrain()` 刚指定的那一半 —— **查表不命中，直接 `return false`**；
- 两列互不相干的列车共用一个站台 —— 同上，任何一方查表都不命中；
- 唯一会被"停住"的对象是**同一个 tick 里刚被解出来的那一半的后车**，而且它最多被停 900 tick。

### 2.9 探针

| 行 | 位置 | 含义 |
|---|---|---|
| `DECOUPLE-INTERLOCK set head= follower= seam_car= tile=x,y` | `DecoupleTrain()` 提交点 | 登记了一对 |
| `DECOUPLE-INTERLOCK wait head= follower= seam=x,y seam_car= dist=` | 后车首次被压 | 互锁生效（只写一次） |
| `DECOUPLE-INTERLOCK clear head= follower= seam_car= dist= gone=` | 前车驶离/消失 | 放行（`gone=1` 表示前车接缝车已不存在） |
| `DECOUPLE-INTERLOCK timeout head= follower= age= dist=` | 安全阀 | 900 tick 到了仍未清，强制放行 |

---

## 三、未确认项 / 风险

1. **KI-334 仍未定性**：本轮只补日志面，机制假设等玩家日志证实/证伪（判读口径见 §1.3）。**没有改任何行为**。
2. **KI-335 的解挂发生在"前车自己在等挂"的场景**：`v` 若持 `OT_WAIT_COUPLE` 永不离开，后车会被压满 900 tick 后由安全阀放行（日志 `timeout`）。这是**有意的**（宁可晚放也不能锁死），但若玩家觉得 900 tick 太长/太短，改一个常量即可。
3. **换端场景（`DrivingBackwards`）**：本实现**不依赖**方向 —— 记录的是"哪半是后车"（`u`），比对的是"前车那节接缝车的格"。所以倒车链上语义仍然正确（`u` 依然是解出的尾段）。
4. **存档往返**：`_r3r_decomp_seams` 是 NOSAVE。同进程内存档再读回，`_tick_counter` 不回退，条目仍有效且语义正确（车号不变、几何不变）；跨进程重启（关游戏再开）后条目消失 ⇒ **互锁不会跨会话生效**（这正是期望：重新载入的存档里"解挂瞬间"已经过去，不该再停）。
5. **未做**：游戏内复测（需玩家跑）；KI-334 的行为修复（等日志）。

---

## 四、构建自证

复用既有 `_tmp_inc_build.cmd`（**未新建任何 `.cmd`**），护栏 `build\R3R_incbuild.guard.log` = `GUARD: incremental is safe (no header/lang file is newer than the newest object)`。

- `build\R3R_incbuild.log` = `[4/4] Linking CXX executable openttd.exe`；`error C####` / `fatal error` / `FAILED:` / `build stopped` 计数 = **0**；
- `build\R3R_incbuild.done` = **`EXIT_CODE=0`**；
- 时间戳链：`src\train_cmd.cpp` 2026-10-10 08:41:21 → `train_cmd.cpp.obj` 08:42:22；同批重编 `src\couple_group.cpp` 08:34:09 → `couple_group.cpp.obj` 08:42:14 → `build\openttd.exe` **2026-10-10 08:44:40（51 680 768 B）**（晚于全部源码）；
- `read_lints`（`src\train_cmd.cpp`）= **0 条**；
- `findstr` 在 exe 内命中 `DUALHEAD` / `DECOUPLE-INTERLOCK` / `respace-post` / `swapv-post` / `rev-dir-post`。

同文另见 `R3R_KNOWN_ISSUES.md` 的「第 228 轮」小节。

---

## 五、复测判据（请玩家按此判读）

### KI-335
1. 解挂后立刻：日志出现 `DECOUPLE-INTERLOCK set`，随后（前车未走时）出现**一条** `DECOUPLE-INTERLOCK wait`，后车 `cur_speed` 恒 0、**画面可见后车不动**。
2. 前车驶离站台后：出现 `DECOUPLE-INTERLOCK clear ... dist=N gone=0`，**同一帧或下一帧**后车起步按订单走。
3. **超时兜底**：若前车 900 tick 内不动，出现 `DECOUPLE-INTERLOCK timeout`，后车随后仍能走。
4. **负判据 A（正常追尾）**：普通跟车的后车**不得**出现任何 `DECOUPLE-INTERLOCK` 行。
5. **负判据 B（同站两列车）**：两列独立列车共用一个站台时，双方都**不得**出现任何 `DECOUPLE-INTERLOCK` 行、都不得停顿。
6. **负判据 C（车库/野外解挂）**：车库内解挂、野外解挂**不产生** `DECOUPLE-INTERLOCK set`。

### KI-334（只补日志，行为应逐字不变）
7. 双头机车挂接场景下，`FOLD-V`/`FOLD-U` 每行应能读出成对的 `mh=1` / `omp=<另一半车号>`。
8. 三处出口各应出现 `DUALHEAD <tag>` 行；**健康**场景应恒为 `adj=1` 且两半 `dir` 相同。
9. **负判据**：没有双头机车的普通场景**不得**出现任何 `DUALHEAD` 行。
10. `DUALHEAD orphan=1` 若出现，说明双头被拆散 ⇒ 立刻回报。

---

## 六、实机复测取证（2026-10-10）

**数据源**：`build\R3R_debug.log`（1983 行 / 169 969 B，`2026-10-10 08:54:55`，GBK），产生者 = `build\openttd.exe` `2026-10-10 08:44:40`（51 680 768 B）—— 即本轮带探针的 exe，非陈旧日志。

### 6.1 KI-335：判据 ①② 通过（玩家已确认），③ 未触发，⑥ 未覆盖

- 玩家原话：「**关于335，后车确实等到前车离开站台才起步**」。
- 闭环三行（`follower=22`）：

  ```
  1397 DECOUPLE-INTERLOCK set head=23 follower=22 seam_car=25 tile=1086,139
  1424 DECOUPLE-INTERLOCK wait head=23 follower=22 seam=1086,139 seam_car=25 dist=0
  1432 DECOUPLE-INTERLOCK clear head=23 follower=22 seam_car=25 dist=4 gone=0
  ```

  `dist` 由 `0` → `4`（= `R3R_DECOMP_CLEAR_TILES`），`gone=0` ⇒ 放行来自**几何判据**而非"前车被卖掉"。
- 登记条件成立的原因：`1370 DECOUPLE-DONE u=22 co=0 real=4 tx=1085 ty=139` —— 后车在**站台格**且 `co=0`（非纯车底）。
- `timeout` 0 条（判据 ③ 未触发）。
- **判据 ⑥ 的精确读法**：本会话两次解挂，只有 `u=22` 登记；第一次 `767 DECOUPLE-DONE u=12 co=1 tx=1087 ty=140` 未登记。`R3RRecordDecoupleSeam()`（`src\train_cmd.cpp:2299`）有**两道都无日志的早退**：`2302 !IsRailStationTile(follower->tile)` 与 `2304 R3RIsCarOnlyFormation(follower)`。日志能确定的只有 `co=1`（纯车底），而 `740 DECOUPLE-FIRE consist=11 … plat=9/0` 说明当时整列有 9 节压在站台格上 ⇒ **未登记极可能是 `2304` 纯车底豁免**；但日志无法区分这两道门禁 ⇒ 仍记为**判据 ⑥ 未覆盖**（需专门的库内/野外解挂样本），只不过"纯车底链不登记"这条设计内豁免已获现场支持。

### 6.2 KI-334：只覆盖 `respace-post` 一条出口，且样本健康 ⇒ 假设仍未证实/证伪

- 双头场景确实出现：`1876 FOLDCHK COUPLE n=16 A idx=96 B idx=103 dist=0` → `1880 RESPACE-AFTER-EDIT couple head=96 … visited=15` → `1883 COUPLE-OK loco=96 rear=102 consist=79`。
- `respace-post` 两对**全健康**（判据 ⑧ 在此出口成立）：

  ```
  1881 DUALHEAD respace-post front=96 pos=0 dir=5 flip=0 db=0 engType=20 segF=1 | rear=103 pos=1 dir=5 flip=0 db=0 engType=20 segF=0 | sep=0 adj=1 star=0
  1882 DUALHEAD respace-post front=79 pos=8 dir=5 flip=0 db=0 engType=20 segF=1 | rear=95 pos=9 dir=5 flip=0 db=0 engType=20 segF=0 | sep=0 adj=1 star=0
  ```

  两对 `adj=1 sep=0`（两半紧贴、无车夹入）、两半 `dir` 相同、`star=0`（★ 落在前半、双头缝里没有段头）⇒ **重排没有拆双头**。
- **翻转 / 物理翻零样本**：0 条 `DUALHEAD rev-dir-post`、0 条 `DUALHEAD swapv-post`、0 条 `orphan=1`。因为本轮 `COUPLE-FLIP-U`/`BOTH` 只作用于两条小链（`607/654` idx 2–10、`1225/1272` idx 11–19），其 `FOLD-V`/`FOLD-U` 逐节 dump **每行都是 `mh=0 rdh=0 omp=-1`**（`574/577-584`、`1192/1195-1202`）⇒ 翻转路径从未遇到双头。判据 ⑦ **未覆盖**。
- 旁证：`571/626/1189/1244 SPLICE-GAP-REJECT … exp=8 dist=64` 属 KI-334 无关的已知接缝场景。
- **收口条件**：需构造"含双头机车的链真的走一次逻辑翻 / 物理翻"（如让双头机车以鼻对鼻姿态逼出 `COUPLE-FLIP-*`），再判 `DUALHEAD rev-dir-post`/`swapv-post` 是否仍 `adj=1` 且 `dir_f == dir_r`；若届时三出口皆健康而仍报错位，则转入渲染面包围盒族（KI-214 / KI-98 / KI-106）。

同文另见 `R3R_KNOWN_ISSUES.md` 的「第 228 轮」小节「实机复测取证（2026-10-10）」段与「待复测判读结果」表。
