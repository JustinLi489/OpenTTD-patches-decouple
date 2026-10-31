# 第 327 轮：三个问题（路点挂接启动 / 自动填充命令插入 / 共享调度计划的自动填充）

> 状态：**取证进行中**（本文件随取证即时追加，结论只认本文件与 `R3R_KNOWN_ISSUES.md`）
> 建档时间：2026-10-26 22:40 前后

## 一、玩家报告（原话三条，不改写）

1. 「这个场景里面的路点挂接启动有问题」
2. 「这个场景里的自动填充命令的插入有问题」
3. 「别的场景发现的关于共享调度计划的自动填充命令有问题」

## 二、输入

- 日志：`D:\temp\codebuddy-dropped-files\4b0b616e-41d2-40d1-b857-46b6d5da9f51\R3R_debug.log`
- 规模：217 969 B / 1 518 行；mtime `2026-10-26 22:32:48`
- 产生者：**待确认**。候选=本轮刚编出的 `build\openttd.exe` @ 2026-10-26 22:17:36（含 KI-503 修法 2 + 两处签名加固）。若确认，则问题 2/3 是**修法 2 之后仍存在**的现象。

## 三、初测指标（`Select-String -SimpleMatch` 计数）

| 标记 | 命中 |
|---|---|
| `AUTO-ORD` | 7 |
| `AUTO-ORD-DROP` | 5 |
| `AUTO-ORD-STALE` | 0 |
| `AUTO-ORD-SKIP` | 0 |
| `COUPLE-OK` | 1 |
| `COUPLE-FAIL` | 1 |
| `CPL-PAIR` | 455 |
| `CPL-HIT` | 0 |
| `COUPLE-WAIT-OFFDEST` | 2 |
| `DECOUPLE-FIRE` / `DECOUPLE-DONE` | 1 / 1 |
| `ORD-AFTER-COUPLE` | 1 |
| `ORD-IDX-WATCH` | 133 |
| `SEGTR-SNAP` | 40 |
| `RESV-WATCH` / `RESV-GHOST` | 0 / 0 |
| `SPD-DROP` | 7 |
| `LOCO-MOVE` | 5 |

初步判读（待验证）：

- `CPL-PAIR = 455` 而 `CPL-HIT = 0`、`COUPLE-OK = 1` / `COUPLE-FAIL = 1` ⇒ 大量挂接尝试一次都没命中 ⇒ 与问题 1「路点挂接启动」高度相关（挂接目标在路点、机车到不了/不上路）。
- `AUTO-ORD-SKIP = 0` 但 `AUTO-ORD-DROP = 5` ⇒ 跳过钩子本轮一次都没动过手，而记录侧把 5 条候选按签名/白名单丢掉了。这与问题 2/3 的「插入」有关，且说明**问题形态已从「跳过玩家命令」变成「记录被丢弃 / 插入位置不对」**（KI-503 修法 2 的副作用面）。

## 四、问题 1：路点挂接启动有问题（已定位直接原因）

### 4.1 场景结构（日志 load 段 `R3RDUMP` / `R3RDUMP-CHAIN` / `R3RDUMP-ORDER`）

7 条链，全部在库里（`tb=80` = `TRACK_BIT_DEPOT`）：

| 链头 | 车节 | 位置 | 命令表 |
|---|---|---|---|
| 0 | 0..5（6 节） | 库 1,15 | 6 条：`GOTO_WAYPOINT(15)`、`GOTO_STATION(7)`、`DECOUPLE`、`WAIT_COUPLE`、`GOTO_WAYPOINT(15)`、`GOTO_COUPLE(7)` |
| 6 | 6..26（21 节） | 库 1,15 | **33 条**，`idx0 = WAIT_COUPLE`（`realType=17`） |
| 27 | 27..47（21 节） | 库 1,15 | 同上 33 条 |
| 48 | 48..50（3 节） | 库 5,6 | — |
| 51 | 51..53（3 节） | 库 62,117 | — |
| 54 | 54..56（3 节） | 库 1,15 | 机车链（后挂上 0 号链） |
| 57 | 57..59（3 节） | 库 1,15 | 6 条：`6,16,6,6,16,2`（见 4.3） |

**关键读数**：链 6 / 27 在 load 时 `ordCount=33`、`curReal=0`、`realType=17`（计划第 0 条是 `WAIT_COUPLE`），但 `coType=0`（`current_order` = `OT_NOTHING`）。

### 4.2 成功样本：54 挂上 0 号链（证明「目标侧 current_order==17」是前提）

- `CPL-PAIRSCAN tick=11532 act=54 state=locked ord=16 destStn=0 actTile=1,15 actSpd=0 pool=5 s_chain=54 s_target=4 best=0 paired=1 near=27 nearDist=0 nearWhy=not-target nearOrd=0 nearStop=1`
- `CPL-PAIRLOCK tick=11532 act=54 tgt=0 tgtOrd=17 tgtStopd=0 tgtFront=1`
- `COUPLE-OK loco=54 rear=26 tx=1 ty=15`

⇒ 命中条件是「目标 `current_order == OT_WAIT_COUPLE(17)`」；0 号链当时确实拿到了 17，所以被选中并挂上。

### 4.3 失败样本：57 找不到目标，挂接不启动

- `LOCO-ORD veh=57`：`0 type=6 (GOTO_WAYPOINT)`、`1 type=16 (GOTO_COUPLE)`、`2/3 type=6`、`4 type=16`、`5 type=2 (GOTO_DEPOT)`；`real=1`（当前执行第 1 条 `GOTO_COUPLE`）。
- `COUPLE-WAIT-OFFDEST veh=57 tile=16,14 mf=16,14 real=1 isDepot=0 destDepot=0 destStn=0 destTile=1669 onDepotTile=0 onStnTile=1`
- `DEPOT-ARR veh=57 ... destTx=5 destTy=13 destDepot=0`（目标调度点 (5,13)，地图 128×128，`destTile=1669 = 5 + 13*128`）
- `CPL-PAIRSCAN tick=11996 act=57 state=... pool=4 s_chain=55 s_target=4 paired=-1 near=27 nearDist=16 nearOrd=0 nearStop=1`
  ⇒ **候选池 4 条链（6 / 27 / 48 / 51）全部被 `s_target` 淘汰（not-target）**。
- 日志尾部 YAPF 挂接寻路同样全灭：`CPL-SCAN found=0 closed=5 nodes=18 enter=13 noplat=1 nores=6 noconsist=6`

### 4.4 结论（第一版判定，**已被 4.5 修正**）

第一版判定是「所有可挂目标都不被认作合法目标、57 永不成对」，依据是：
- `CPL-PAIRSCAN tick=11996 act=57 ... s_target=4 paired=-1 nearOrd=0`（候选池 4 条全 not-target）；
- `CPL-SCAN ... found=0 noconsist=6`。

`R3RIsCoupleTarget()` 要求 ①段头 ★ ②是链头 ③无真 artic ④`cur_speed==0` ⑤持 `OT_WAIT_COUPLE`。当时 ⑤ 看似不满足（链 6 / 27 的 `current_order` 是空）。
**该判定已被后续日志推翻**，见 4.5：57 在 `tick=12428` 配对成功。

### 4.5 修正（**判定：57 配对成功，物理挂接未完成**）

玩家 2026-10-26 追问「veh57 是否成功与某列车配对」，逐行核对结果如下（行号为日志物理行）：

| 行 | 内容 | 判读 |
|---|---|---|
| `:328` | `CPL-PAIRLOCK tick=11532 why=scan act=54 ... tgt=0 tgtTile=1,15 tgtOrd=17 tgtFront=1 tgtLen=107 tgtPairR=54` | 54 锁 0 号链 |
| `:425` | `COUPLE-OK loco=54 rear=26 consist=0 co=1 real=11 type=1 tx=1 ty=15` | 54 挂上 0 号链成功（**全日志唯一一次挂接成功**） |
| `:566` | `COUPLE-FAIL loco=57 order=16 tx=16 ty=14 x=271 y=232` | 57 早期一次失败 |
| `:1209` | `CPL-PAIRLOCK tick=12428 why=scan act=57 actHead=57 actTile=16,14 actSpd=0 actOrd=16 actFront=1 actLen=11 tgt=0 tgtHead=0 tgtTile=12,15 tgtSpd=0 tgtOrd=17 tgtFront=1 tgtLen=107 tgtPairR=57` | **57 配对成功**：目标=0 号链（链头 veh=0），`tgtPairR=57` 双向锁定 |
| `:1354`→`:1359` | `CPL-ENTRY veh=57 tile=16,14` → `CPL-PATHFOUND veh=57 found=1` | 锁定后寻路**成功** |
| `:1475`→`:1478` | `RESV-EXT-PRE veh=57 tile=14,14 ... spd=30` / `CPL-ENTRY veh=57 tile=15,14` → `CPL-PATHFOUND veh=57 found=0` | 往前走两格后寻路转为失败 |
| `:1479` | `CPL-SCAN tick=12627 veh=57 found=0 closed=5 nodes=18 enter=13 noplat=1 nores=6 noconsist=6` | 候选全被 `noconsist` 拒 |
| `:1483`/`:1516` | `CPL-PAIRSCAN tick=12667/12676 act=57 state=steady ... pool=0 s_chain=0 paired=-1 wait=-1 near=-1 nearWhy=none` | 目标已锁定并移出候选池（`pool=0`），`paired=-1` 是「本轮没重新选」而非失败 |
| `:1485` | `SVC-DEPOT-SKIP veh=0 cur=17 real=3(17) spd=0 tile=12,15 tag=couple-protocol` | 0 号链受「挂接协议」保护、`current_order=17` 在等 57 |
| `:1486`-`:1512` | `RESERVECONSIST veh=0..26` 连续 27 行 | **0..26 已是同一条链**（0..5 + 6..26 已合并，横跨 12,15 → 5,15） |
| `:1513` | `SKIP-STOPPED veh=27 order=0 real=17 spd=0 tile=1,15 nord=33` | 27 号链仍在库 1,15、`current_order` 空（未参与） |
| `:1517`/`:1518` | `DEPOTBLK veh=57 tile=14,14 spd=30 destRaw=0` / `LOCO-MOVE veh=57 tile=14,14 j=208 spd=30 track=01 dir=5 db=1 stuck=0 dtile=1932` | 57 正以 spd=30 **倒车**（db=1）逼近 12,15 |

**结论**：
1. **配对（锁定目标）成功** —— `tick=12428` 与 0 号链（27 节，链头 veh=0）双向锁定（`tgtPairR=57`）。0 号链的 `current_order=17` 满足 `R3RIsCoupleTarget()`，所以第一版「永远 not-target」的判定错误。
2. **物理挂接（`COUPLE-OK loco=57`）未发生** —— 日志到 `:1518` 结束（1518 行，游戏仍在跑、非崩溃截断），57 仍在 14,14 倒车接近，距目标 bestDist=3，**没有失败报告也没有成功报告**。
3. 卡住的点前移到：**锁定后接近阶段 `CPL-PATHFOUND` 由 `found=1` 退化为 `found=0` + `noconsist=6`**（`:1479`）。`noconsist` 是 YAPF 挂接搜索「候选与目标不一致」的分类，需读 `yapf_destrail.hpp` 给它定性。
4. 因此 KI-504 的病象不是「不配对」，而是「**配上了、走近了却挂不上**」。27 号链（`:1513` `current_order` 空）只是没被选中，不构成阻塞。
5. 副作用发现（待查）：`tgtLen=107` / `actLen=11` —— 0 号链已 27 节，`tgtLen` 却只有 107px，若该值是整链长度则明显偏小（27 节最短也 >100px 但通常 ~200px+），需确认探针取的是单节还是整链。

### 4.6 玩家口述补正（2026-10-26，**本轮定性依据**）

玩家原话：

> 「好，既然你告诉我配对成功了，那么我告诉你，正确的预留已经成功建立了，但是 veh57 走错了岔道，走到了不是自己铺设预留的轨道上面去」

⇒ 三条已被玩家实证的事实（不再当作待验证项）：

1. **配对成立** —— 与 §4.5 的 `:1209 CPL-PAIRLOCK act=57 tgt=0 tgtPairR=57` 一致，本节维持。
2. **预留已正确建立** —— `CPL-PATHFOUND veh=57 found=1`（`:1359`）之后 57 确实订出了一条通往 0 号链（12,15）的合法路径，玩家在游戏内看到该预留成立。
3. **病灶＝岔道口选错轨道** —— 57 在行进中走进了**不属于自己预留**的那条岔道（另一条 track / 另一支 branch），而不是预留铺设的那条。

⇒ **KI-504 的定性改写**：问题不是「配不上/挂不上」，而是「**预留铺在 A 分支，车走了 B 分支**」。这与 §4.5 观察到的现象吻合：57 在 14,14 附近 `found` 由 1 退化为 0（`:1478`），因为它已经离开了预留覆盖的分支（不在预留轨道上时，YAPF 从当前格重新搜也搜不到通往目标的合法路径）。也解释了为何全日志没有 `COUPLE-OK loco=57`——它偏离预留后永远走不到 12,15 的拼接面。

**新增疑点登记（编号 Q1-5 起）**：

- **Q1-5（主线·待取证）** 预留路径与实走路径的第一个分叉点在哪一格、两个 trackdir 分别是什么？
  取证手段：日志里 57 的 `CPL-TRACE` / `RP cur=<tile> td=<n> trk=<n> bits=0xN`（YAPF 预留逐格 dump）与 `LOCO-MOVE veh=57 tile=x,y j=<n> spd=<n> **track=<bits> dir=<dir>** db=<0|1> dtile=<n>` 两串逐格对齐；分叉格必然满足「`RP cur=` 出现但 `LOCO-MOVE tile=` 没跟到同一格，或同格 `dir` 与预留 `td` 不一致」。
  已知尾部读数（`:1475`/`:1484`/`:1518`）：`RESV-EXT-PRE veh=57 tile=14,14 enterdir=0 tracks=0x11 reserved=0x0 doRes=0 spd=30` / `LOCO-MOVE veh=57 tile=14,14 j=196→208 spd=30 **track=01 dir=5** db=1 dtile=1932` —— 注意 `tracks=0x11`（该格有两个可用 track 位：bit0=X 与 bit4=Y）而车在 `dir=5`（DIR_NW）倒车行驶，这就是典型双槽岔道格，**很可能就是走错的那一格**。
- **Q1-6** 57 全程 `db=1`（`DrivingBackwards`）。倒车行驶时引擎用 `GetMovingDirection()` 而非 `direction` 选轨（KI-242 已为此修过 `YapfTrainCheckReverse`），需确认岔道选轨代码用的是哪个方向函数 ⇒ 若这里用错参照系，倒车链会在双槽格选到镜像分支，与「预留铺 A、车走 B」的现象完全一致。
- **Q1-7** `RESV-EXT-PRE ... reserved=0x0` 说明该格**尚未被 57 预留**（`reserved=0x0`），即 57 是带着「本格无预留」进入 14,14 的 —— 与玩家「走错岔道」一致：它抵达的格不在自己的预留集合内。

### 4.7 岔道选错的实证（2026-10-26，**Q1-5 已结案**）

玩家口述「预留已正确建立、车走错岔道」在日志里可逐格对齐，错误发生在 **`(15,14)` 这一格**：

**① destrail 铺出的正确路径（挂车专用寻路）**

```
:1362 CPL-BEST  veh=57 tile=5,15 td=0 cost=10242
:1363 CPL-BACK  pPrev=5,15  td=0 parent=15,15
:1364 CPL-BACK  pPrev=15,15 td=2 parent=16,14
:1365 CPL-TRACE st=16,14 tdb=0x101
:1415 CPL-RESERVE veh=57 reserved=1 next=0
```

回溯链是 `16,14 → 15,15 → 5,15`（`15,15` 是 y=15 站台段的入口，站台横跨 x=5..15）。
⇒ 正确走向是：在 `16,14` 就转向**西南**进 `15,15`；退一步，若沿 `15,14` 走，则必须在 `15,14` 转 `TRACK_LOWER`（`RP cur=15,14 td=3 trk=3 bits=0x29`）进 `15,15`。

**② 57 实际走的轨（`track=` 是 track bits）**

```
:1467 LOCO-MOVE veh=57 tile=15,14 j=210 spd=30 track=01 dir=5 db=1 stuck=0 dtile=1932
:1484 LOCO-MOVE veh=57 tile=14,14 j=196 spd=30 track=01 dir=5 db=1 stuck=0 dtile=1932
:1518 LOCO-MOVE veh=57 tile=14,14 j=208 spd=30 track=01 dir=5 db=1 stuck=0 dtile=1932
```

`track=01` = `TRACK_BIT_X`（= `trk 0`），`dir=5`=DIR_SW、`db=1` 倒车。即它在 `15,14` 选了 **TRACK_X 继续向西**进 `14,14`，**没有**选 `TRACK_LOWER`（`trk=3`，通向 `15,15`）。

`RP cur=15,14` 两行并存正好证明该格是双槽岔道口：
```
:1392 RP cur=15,14 td=0  depth=1 trk=0 bits=0x29   ← 实走这条
:1411 RP cur=15,14 td=3  depth=1 trk=3 bits=0x29   ← 应走这条
```
（`bits=0x29` = X | LOWER | RIGHT，三轨可用）

**③ 走错之后的连锁反应**

```
:1475 RESV-EXT-PRE veh=57 tile=14,14 enterdir=0 tracks=0x11 reserved=0x0 doRes=0 real=1 spd=30
:1476 CPL-ENTRY    veh=57 tile=15,14 orderType=16 dontReserve=0
:1478 CPL-PATHFOUND veh=57 found=0
:1479 CPL-SCAN tick=12627 veh=57 found=0 ... noplat=1 nores=6 noconsist=6
:1483 CPL-PAIRSCAN tick=12667 act=57 ... paired=-1 bestDist=3
```

到 `14,14` 时 `reserved=0x0` ⇒ **这一格没有 57 的预留位**（玩家看到的「走到不是自己铺设预留的轨道上」= 这一刻）。随后挂车寻路从错误位置重搜，`nores=6` / `noconsist=6` 全灭 ⇒ `found=0` ⇒ 永远到不了 `12,15` 的拼接面 ⇒ 全日志无 `COUPLE-OK loco=57`。

**④ 结论（Q1-5 结案）**

- 铺预留的寻路（`yapf_destrail`，走 `dest=1932=(12,15)`）与车实际跑路用的寻路（`yapf_rail` / `TryPathReserve`）**是两套独立搜索**，在 `(15,14)` 这个三轨岔道口各自选了不同分支。
- `:1272 TRP veh=57 mf=16,14 real=1 type=16 dest=1932 spd=0 ... => ok=1 res=1` ⇒ 跑车寻路也「成功预留」了一条路，但它的路径与 `CPL-BACK` 不一致（`TRP` 不含逐格路径，只有结果）。
- `RP` 把 destrail 展开过的**所有节点**（含 `14,14`、`15,15` 两条分支）都订了位（`:1391`-`:1414` 与 `REACH` 一一对应），比「只订最优路径」宽 —— 这解释了为何前期还能开动、以及为何别的车也被牵连。

**⇒ 病灶定性改为：两套寻路在岔道口不一致（destrail 选 LOWER/`15,15`，跑车寻路选 X/`14,14`），车按跑车寻路走，于是脱离挂车预留、再也挂不上。**

### 4.8 玩家口径确认 + 本轮任务（2026-10-26）

玩家原话：

> 「你可以加一条探针，探测挂接寻路和它发出的预留，此外，【yapf_rail 选了 X 分支】和我肉眼观测的事实相符」

⇒ 两条落定：

1. **Q1-8 结案（从推断升为实证）**：§4.7 ③ 里「`yapf_rail` 选了 X 分支」是**推断**（`TRP` 不打印逐格路径），现已由玩家肉眼观测确认为事实。⇒ 两套寻路分叉成立：destrail 订 LOWER（去 `15,15`）、rail 走 X（去 `14,14`）。
2. **本轮任务 T-1（代码）**：给 `yapf_rail.cpp` 的预留路径（跑车寻路 `rp()`）加只读探针，使其逐格可见、并能与 destrail 的 `RP cur=` 和 `LOCO-MOVE track=` 三方对齐。

**T-1 探针设计要求（本轮落地口径）**：

- **P1 `RPR`（rail reserve dump）**：`yapf_rail.cpp` 的 `rp()` 每格一行，含 `tile / td / trk / bits / res(是否真订上)`，标签与 destrail 的 `RP` 区分开（`RPR`=rail，`RP`=destrail）。
- **P2 `RESV-SPLIT`（分叉报警，命中即打）**：同一格里，若该格**已被某预留占着某 track** 而本次跑车寻路订的是**另一个 track** ⇒ 打一行 `RESV-SPLIT veh= tile= reserved=0xN chosen=0xN td=N`。这正是玩家描述的「走到不是自己铺设预留的轨道上」的机器可读形态，无需人工比对逐格列表。
- 两条都必须是**只读**（不改选轨、不改预留、不改任何返回路径），避免干扰现场。

**判读口径（复测时怎么用）**：

- 若出现 `RESV-SPLIT veh=57 tile=15,14 reserved=0x8 chosen=0x1`（LOWER vs X）⇒ 与 §4.7 完全吻合，坐实分叉格。
- 若 `RPR` 在 `15,14` 直接把 `trk` 记成 0 而 `RP` 同格有 `td=3 trk=3` ⇒ 同样坐实，且能看出 rail 侧是在何时放弃 LOWER 的（`res` 是否为 0）。
- 若两者都不出现 ⇒ 说明 rail 侧根本没走到 `15,14` 的预留判定（`reserve_if_present` 早退/该格无预留位），改追 `HasReservedTracks` 门。

### 4.9 关键纠错：`RP cur=` 是**跑车寻路**的探针，不是 destrail 的（2026-10-26）

`search_content` 实测：

```
src/pathfinder/yapf/yapf_rail.cpp:1081: std::function<bool(TileIndex, Trackdir, int)> rp = [&](TileIndex cur, Trackdir cur_td, int depth) -> bool {
src/pathfinder/yapf/yapf_rail.cpp:1085:   fprintf(dbg, "RP cur=%d,%d td=%d depth=%d trk=%d bits=0x%x\n", ...);
src/pathfinder/yapf/yapf_rail.cpp:1096:   auto reserve_if_present = [](TileIndex t, Track trk) { ... };
src/pathfinder/yapf/yapf_rail.cpp:1140:   reserve_if_present(p, plat_track);
```

⇒ §4.7 ③ 把 `RP cur=` 当成「destrail 铺出的预留路径」**是错的**。它对 `yapf_rail.cpp` 的回溯帧 dump，即**跑车寻路**自己的预留回调。

**对既有判定的影响（必须修正，不许再将错就错）**：

1. `:1392 RP cur=15,14 td=0 trk=0` 与 `:1411 RP cur=15,14 td=3 trk=3` **两行都出自 rail 侧**，不是「destrail 订 A / rail 订 B」。它们是同一格上 rail 回溯过的两个帧（X 与 LOWER 都展开过），**不能**据此说"destrail 订了 LOWER"。
2. `CPL-BACK pPrev=5,15 parent=15,15` / `pPrev=15,15 parent=16,14` 仍是 destrail 最优链的正确读数（`16,14 → 15,15 → 5,15`），这部分结论不变。
3. 「destrail 到底订了哪些格/哪条 track」目前**零直接证据** —— 须查 `yapf_destrail.hpp` 里的预留落订点。
4. 「车选了 X 而非 LOWER」仍是实测（`:1467/:1484 LOCO-MOVE track=01` dir=5 db=1），玩家肉眼也已确认；但「本该走 LOWER 的那条预留由谁订下」需重新取证。

⇒ **T-1 探针落点修正为两处**：`yapf_rail.cpp` 的 `reserve_if_present`（真正落订，只看"订没订上/订了哪条"），以及 `yapf_destrail.hpp` 的对应落订点；`RP cur=` 这种"回溯帧"dump 已经够了、不必再加。

### 4.10 §4.9 再修正：`RP cur=` 其实是**挂接寻路（CPL-*）自己**打的（2026-10-26，读码确认）

读 `yapf_rail.cpp:1060-1223` 全文后确认：

```
:1060-1064  CPL-TRACE st=.. tdb=0x..
:1067-1070  for (tdb ...) if (reach(st, td, 0)) { best_td = td; break; }
:1074        CPL-TRACE-RESULT st=.. best=..
:1081-1198  rp = [&](cur, cur_td, depth) { ... 1085 fprintf("RP cur=..") ... }
:1096-1099  reserve_if_present(t, trk)   -> TryReserveRailTrack(t, trk)
:1109-1112  reserve_dir_if_present(t, td) -> TryReserveRailTrackdir(v, t, td)
:1199        rp(st, best_td, 0)
:1214        CPL-RESERVE veh=.. reserved=.. next=..
```

⇒ `CPL-TRACE / CPL-BACK / CPL-RESERVE / RP cur=` **全部在同一函数内**，就是 §4.7 里那条 `CPL-RESERVE veh=57 reserved=1` 的挂接寻路。所以：

1. `RP cur=` **正是玩家要的「挂接寻路发出的预留」清单**，不是普通跑车寻路（普通 `TryPathReserve` 走的是 `CYapfReserveTrack` 的另一条路径，不在这里）。
2. **但 `RP cur=` 有一个致命缺陷，解释了 §4.7 的自相矛盾**：它在 `depth <= 15` 时于函数**最开头无条件打印**（`:1082-1088`），而真正落订发生在**递归返回 true 之后**（`:1149-1151`/`:1165-1167`/`:1175-1177`/`:1191-1193`）。⇒ 打印过的格**不一定订了**，被丢弃的分支照样打。
   - 这正是 `:1392 RP cur=15,14 td=0 trk=0` 与 `:1411 RP cur=15,14 td=3 trk=3` 两行同时存在的原因：两帧都走到过 `15,14`，但只有一条分支返回 true 并被订。
3. 因此「destrail 订 A、rail 订 B」这个表述**不成立**——只有一个寻路在订（本函数），歧义来自探针本身。
4. `LOCO-MOVE track=01`（车实际在 X 轨）与 `CPL-BACK` 的最优链（经 `15,15`，即需要 LOWER）仍然是两条独立实测 ⇒ **「算法选的路」与「车实际走的路」不一致这一事实不变**，只是「谁订了哪条」必须靠新探针才能读出。

**⇒ 新探针的必要性由此升级**：不是"补个好看的 dump"，而是**现有 `RP` 无法区分「走过」与「订下」**，必须打真实落订点（`reserve_if_present` / `reserve_dir_if_present` 内部）。

### 4.11 T-1 落地：`RP-BOOK` 探针（2026-10-26，仅 `src\pathfinder\yapf\yapf_rail.cpp`）

**改动（纯只读，未碰任何 `src\*.h`）**：

1. 新增 lambda `r3r_log_book(kind, tile, trk, td, will_book)`，插在 `reserve_if_present` 之前（原 `:1096` 处）。
2. `reserve_if_present`（平台段落订，原捕获 `[]`）改捕获 `[&]`，并在 `TryReserveRailTrack` **之前**记一行；`if (ok)` 的判定抽成局部 `bool ok`，行为逐字不变。
3. `reserve_dir_if_present`（车实际驶过格的 trackdir 落订）同样在 `TryReserveRailTrackdir` **之前**记一行。
4. 文件头补 `#include "../../rail_map.h"`（取 `GetRailReservationTrackBits`）。

**日志格式**：

```
RP-BOOK <plat|dir> veh=<id> tile=<x>,<y> td=<n| -1> trk=<n> bits=0x<该格全部轨> before=0x<该格原有预留轨> book=<0|1>[ SPLIT]
```

- `kind=plat`：站台段补订（`td=-1`，因为那里只有 `Track`）。
- `kind=dir`：车驶过格的 trackdir 落订（`td` 是真实 trackdir）。
- `book=0`：该格**没有**这条轨（`reserve_if_present` 的 `pbs.cpp:144` 保护把它跳过了）⇒ 打印了但没订。
- `before`：落订前该格已有的预留位。
- **`SPLIT` 尾缀 = 玩家报的那件事的机器可读形态**：`before != 0` 且 `before & 本次trk == 0` ⇒ 这一格已经被别的轨道占着，本次要订的是另一条。

**判读口径（复测时按这个读）**：

| 现场出现 | 判定 |
|---|---|
| `RP-BOOK dir veh=57 tile=15,14 td=0 trk=0 ... book=1`，且**没有** `td=3 trk=3` 的 `book=1` | 挂接寻路**自己**就在 `15,14` 选了 X ⇒ 病根在 `reach()/best_td` 的选轨，`CPL-BACK` 的 `15,15` 链被丢弃 |
| `td=3` 与 `td=0` 都有 `book=1`（同格两次落订） | 该格被订了两条轨，查为何第二次不被 `before` 挡住（`TryReserveRailTrack` 对已有预留不报错，会叠加） |
| 任一行的 `SPLIT` 为真 | 直接坐实「走到不是自己预留的轨道上」，并给出是哪条 vs 哪条 |
| 完全没有 `RP-BOOK` | 说明 `rp()` 没进到落订点（`best_td==INVALID` 或 `dont_reserve=1`），转查 `CPL-TRACE-RESULT best` |

**与 `RP cur=` 的分工（不要混读）**：`RP cur=` = 回溯**走过**的格（含丢弃分支）；`RP-BOOK` = 真正**订下**的格。两者对比即得"算法考虑过 A 却订了 B"。

**构建自证（2026-10-26 23:03）**：复用既有 `_tmp_inc_build.cmd`（未新建 `.cmd`）；
`build\R3R_incbuild.done` = `EXIT_CODE=0`；
`src\pathfinder\yapf\yapf_rail.cpp` 23:00:55 → `yapf_rail.cpp.obj` 23:02:30 → `build\openttd.exe` 23:03:27（52 297 728 B）；
`read_lints` 该文件 0 条；
exe 二进制内 `RP-BOOK` 字面量命中 1 次（`[System.IO.File]::ReadAllBytes` + ASCII 检索，`count=1`）⇒ 探针确实入包。

**复测方式**：用 `build\openttd.exe` 重跑 57 挂接场景，`build\R3R_debug.log` 中按 `RP-BOOK` 过滤，同时保留同一时段 `CPL-TRACE/CPL-BACK/CPL-RESERVE/LOCO-MOVE` 上下文（不要只截 `RP-BOOK` 行）。

**口径确认（玩家 2026-10-26 原话）**：「`yapf_rail` 选了 X 分支」与我肉眼观测的事实相符 ⇒ 该项由推断升为**实证**：`(15,14)` 岔道格上 `yapf_rail` 主动选了 X（`td=0`/`trk=0`），而非被外力扳到 X。故本轮探针的任务只剩"把选轨点与落订点在同一日志里对上"，用于区分「选轨错」与「同格重复落订」。

### 4.12 T-1 首版探针崩在 `rail_map.h:227`（2026-10-26，玩家报「垃圾探针」）

**玩家原话**：「垃圾探针，rail_map.h 227断言崩溃了」。

**读数与判定（已实证，不是猜测）**：

- `src\rail_map.h:225-232` 的 `GetRailReservationTrackBits(t)` 第一行就是 `dbg_assert_tile(IsPlainRailTile(t), t)`（`rail_map.h:227`）⇒ **只允许普通轨道格**。
- 我的 `r3r_log_book` 在 `yapf_rail.cpp:1107` 对**任意格**调它，而它被 `reserve_if_present`（`:1115`，站台段补订）/ `reserve_dir_if_present`（`:1130`，车驶过格）无条件调用，落点必然包含 `IsRailStationTile` 的站台格、`IsRailDepotTile` 的车库格、隧道桥格 ⇒ 一次就断言崩。

**定性：这是探针自身的缺陷，与 KI-504 的病根无关；KI-504 的既有结论一条不动。**

**同族教训（必须记住）**：这是 KI-203 附记 7b（`rail_map.h:168` `GetTrackBits()` 断言）的**同一类错误第二次发生**——
「R3R 里任何读预留/读轨位的路径，都必须先自证该格类型，再调用带 `@pre IsPlainRailTile` 的 inline getter」。
上轮加探针时只想着"多打点信息"，忘了 `GetRailReservationTrackBits` 不是安全的通用读数。

**修法（仅 `src\pathfinder\yapf\yapf_rail.cpp`，纯只读探针内）**：

按格型分派，绝不越界调用：

| 格型 | 读取方式 | 打印 |
|---|---|---|
| `IsPlainRailTile` | `GetRailReservationTrackBits(t)` | `resv=plain before=0x<位>` |
| `IsRailStationTile && HasStationRail` | `HasStationReservation(t)`（整格位，没有单轨信息） | `resv=stn before=0x1/0x0`，**不参与 `SPLIT` 判定** |
| 其他（车库/隧道桥/道口等） | 不读 | `resv=none before=0x0` |

`SPLIT` 只在 `resv=plain` 时计算（站台整格位与单轨位不同域，混算会误报）。

**第 2 版日志格式（复测按这个读）**：

```
RP-BOOK <plat|dir> veh=%d tile=x,y td=%d trk=%d bits=0x%x resv=<plain|stn|none> before=0x%x book=%d[ SPLIT]
```

- `resv=plain`：普通轨道格，`before` 是该格真实预留轨位，`SPLIT` 有意义。
- `resv=stn`：站台格，`before=0x1` 表示整格已被预留（没有单轨信息），**不参与 SPLIT**。
- `resv=none`：车库/隧道桥/道口等，`before=0x0`，即"该格无预留语义"。

**构建自证（第 2 版，2026-10-26 23:10）**：复用既有 `_tmp_inc_build.cmd`（未新建 `.cmd`）；`guard.log` = `GUARD: incremental is safe`；`[2/3] yapf_rail.cpp.obj` → `[3/3] Linking CXX executable openttd.exe`；`build\R3R_incbuild.done` = `EXIT_CODE=0`；`yapf_rail.cpp` 23:07:57 → obj 23:08:56 → `build\openttd.exe` 23:10:09（52 297 728 B）；`read_lints` 0；exe 内 `RP-BOOK` 命中 1 次、`resv=` 命中 5 次。

**风险自评（本轮教训沉淀）**：探针 = 只读代码，但"只读"不等于"安全"——凡是带 `@pre` 的 inline getter（`GetRailReservationTrackBits`/`GetTrackBits`/`HasStationReservation`…）都必须先判格型。后续任何 R3R 探针涉及轨位/预留读取，一律先过 `IsPlainRailTile` / `IsRailStationTile && HasStationRail` 这两道闸。

### 4.13 T-1 第 2 版首次实测读数：**「挂接寻路选了 X」被探针推翻**（2026-10-26）

**日志**：`D:\temp\codebuddy-dropped-files\8cebe425-41aa-421f-ac4e-9eacb8151437\R3R_debug.log`（218 269 B / 1 504 行，玩家 2026-10-26 提供）。

**计数**：`RP-BOOK` 16 行、`SPLIT` **0** 行。16 行全为 `veh=57`。

**全部 16 行（行号:原文）**：

```
1413: RP-BOOK dir  veh=57 tile=13,15 td=0  trk=0 bits=0x1  resv=plain before=0x0 book=1
1415: RP-BOOK plat veh=57 tile=12,15 td=-1 trk=0 bits=0x1  resv=stn   before=0x1 book=1
1416: RP-BOOK plat veh=57 tile=11,15 td=-1 trk=0 bits=0x1  resv=stn   before=0x1 book=1
1417: RP-BOOK plat veh=57 tile=10,15 td=-1 trk=0 bits=0x1  resv=stn   before=0x1 book=1
1418: RP-BOOK plat veh=57 tile=9,15  td=-1 trk=0 bits=0x1  resv=stn   before=0x1 book=1
1419: RP-BOOK plat veh=57 tile=8,15  td=-1 trk=0 bits=0x1  resv=stn   before=0x1 book=1
1420: RP-BOOK plat veh=57 tile=7,15  td=-1 trk=0 bits=0x1  resv=stn   before=0x1 book=1
1421: RP-BOOK plat veh=57 tile=6,15  td=-1 trk=0 bits=0x1  resv=stn   before=0x1 book=1
1422: RP-BOOK dir  veh=57 tile=14,15 td=0  trk=0 bits=0x9  resv=plain before=0x0 book=1
1423: RP-BOOK dir  veh=57 tile=13,15 td=0  trk=0 bits=0x1  resv=plain before=0x1 book=1
1424: RP-BOOK dir  veh=57 tile=15,15 td=2  trk=2 bits=0x15 resv=plain before=0x0 book=1
1425: RP-BOOK dir  veh=57 tile=14,15 td=0  trk=0 bits=0x9  resv=plain before=0x1 book=1
1426: RP-BOOK dir  veh=57 tile=15,14 td=3  trk=3 bits=0x29 resv=plain before=0x0 book=1
1427: RP-BOOK dir  veh=57 tile=15,15 td=2  trk=2 bits=0x15 resv=plain before=0x4 book=1
1428: RP-BOOK dir  veh=57 tile=16,14 td=0  trk=0 bits=0x1  resv=none  before=0x0 book=1
1429: RP-BOOK dir  veh=57 tile=15,14 td=3  trk=3 bits=0x29 resv=plain before=0x8 book=1
```

**判定 1（纠错，关键）：挂接寻路在 `(15,14)` 订的是 `td=3 / trk=3`，不是 X。**
`(15,14)` `bits=0x29` = `X|LEFT|LOWER`（`0x01|0x08|0x20`）。日志两次落订都是 `trk=3`（`0x8` = LEFT 位），去向正是 `(15,15)`，与 `CPL-BACK pPrev=15,15 td=2 parent=16,14` 的回溯链**一致**。
⇒ 此前 KI-504 里写的「挂接寻路自己选了 X 分支」**不成立**，本条据此推翻；玩家肉眼说的"yapf_rail 选了 X"若指**跑车寻路**则仍成立（见判定 3），若指挂接寻路则被探针否证。**这正是加 `RP-BOOK` 的意义：区分"算法走过 X"与"算法订了 X"。**

**判定 2：整条路径被订了**两轮**，但同轨、幂等、无 `SPLIT`。**
同一格同一条轨出现两次，第二次 `before` 恰是第一次写下的位（`13,15` 0x0→0x1、`14,15` 0x0→0x1、`15,15` 0x0→0x4、`15,14` 0x0→0x8）⇒ `rp()` 对同一条候选路径执行了两遍（两次 `reach`/`best_td` 求解或两轮 reserve 循环）。因为轨相同，`before & trk != 0` ⇒ `SPLIT=0`，没有副作用，也**不支持**旧的"同格重复落订"假设。

**判定 3：车走的是 X，而那是跑车寻路（`yapf_rail` 的 TRP）选的，与挂接寻路无关。**
`:1467 LOCO-MOVE veh=57 tile=15,14 spd=30 track=01 dir=5 db=1` → `:1484 LOCO-MOVE veh=57 tile=14,14 ... track=01`（`track=01` = `TRACK_BIT_X`）。挂接寻路订的是 LEFT/LOWER 侧，车却沿 X 驶向 `14,14` ⇒ 落地即 `:1475 RESV-EXT-PRE veh=57 tile=14,14 enterdir=0 tracks=0x11 reserved=0x0 doRes=0 spd=30`（在一条自己没订过的轨上）。
⇒ **KI-504 的病根表述要从「挂接寻路选错轨」改为「挂接寻路订对了（LEFT→15,15），但跑车寻路按自己的目标/代价选了 X（→14,14），两者分岔后车开进无预留区」**。

**判定 4：站台段 `12,15..6,15` 七格 `resv=stn before=0x1`。**
`before=0x1` 说明**落订之前**整格位就已经是置位状态（站台整格位没有单轨信息，`before` 只是 `HasStationReservation`）。两种解释待区分：(a) 上一 tick 遗留未清；(b) 同 tick 内第一轮已订过（与判定 2 的两轮一致，(b) 更可能，因为第一轮 `plat` 行之后没有 `CLEAR-STN`）。
⇒ 不构成缺陷，但**站台整格位无法反映"订了哪条轨"**，这是 `resv=stn` 只给 0/1 的固有限制，已在本版日志格式里注明。

**判定 5：`(16,14)` 报 `resv=none`，`bits=0x1` 单轨 X。**
说明该格不是 plain rail 也不是站台（隧道口/桥格），`before` 无从读起 ⇒ 分派逻辑按设计返回 `none`，**第 2 版探针不再崩**，这是修法的直接验证。

**下一步取数（待读同一日志）**：
1. `TRP veh=57` 的 `dest=`/`ok=`/`res=` —— 跑车寻路自报的目标格与成败（旧日志是 `dest=1932`）。
2. `CPL-BEST / CPL-BACK / CPL-PATHFOUND / CPL-PAIRSCAN` —— 挂接寻路的终点与配对扫描结果。
3. 车沿 X 走完整轨迹（`LOCO-MOVE veh=57` 全部行），看它最终停在哪、是否回到 `(12,15)` 拼接面。
4. 是否有 `COUPLE-OK loco=57`（若无，即为"永远到不了拼接面"的复现）。

### 4.14 同一日志的关键时间线（2026-10-26，已取）

**计数**：`COUPLE-OK` 1 行（`loco=54`，另一次挂接，与 57 无关）、`COUPLE-FAIL` 2 行（`loco=57 order=16 tx=16 ty=14`）、`LOCO-MOVE veh=57` 8 行、`TRP veh=57` 3 行、`CPL-*` 488 行。

**时间线（行号:原文要点）**：

```
:392  TRP veh=57 mf=1,15 real=0 type=6 dest=1808 spd=0 ... => ok=1 res=1     (开局，57 在 1,15 线)
:441  COUPLE-OK loco=54 rear=26 consist=0 co=1 real=1 type=1 tx=1 ty=15     (54 挂上 26，与 57 无关)
:582  COUPLE-FAIL loco=57 order=16 tx=16 ty=14                              (第一次失败，57 在 16,14)
:926  COUPLE-FAIL loco=57 order=16 tx=16 ty=14                              (第二次失败)
:1238 TRP veh=57 mf=16,14 real=1 type=16 dest=1669(=(5,13)) ... => ok=0 res=0
:1292 LOCO-MOVE veh=57 tile=16,14 j=194 spd=5 track=01 dir=5 db=0 stuck=0 dtile=1932
:1302 TRP veh=57 mf=16,14 dest=1932(=(12,15)) fto=1 ... => ok=1 res=1       (目标改为目标车底所在格)
:1357 CPL-PATHFOUND veh=57 found=1
:1360 CPL-BEST veh=57 tile=5,15 td=0 cost=10242                            (挂接寻路的拼接位在 5,15)
:1413..1429  RP-BOOK 落订（见 §4.13）＝ 16,14 → 15,14(LEFT) → 15,15 → 14,15 → 13,15 → 站台 12,15..6,15
:1448 CPL-PAIRSCAN act=57 actTile=15,14 best=0 bestDist=4 paired=-1
:1451 LOCO-MOVE veh=57 tile=15,14 j=210 spd=30 track=01 dir=5 db=1 dtile=1932   ★ 车走 X，不是 LEFT
:1460 CPL-ENTRY veh=57 tile=15,14 orderType=16 dontReserve=0
:1461 CPL-ORIGIN veh=57 origin=15,14 td=0 vehDir=5 vehTd=0
:1462 CPL-PATHFOUND veh=57 found=0                                          ★ 从 15,14 重找即失败
:1463 CPL-SCAN ... found=0 closed=5 limit=10000 nodes=18 enter=13 noplat=1 nores=6 noconsist=6
:1464 RESV-WATCH SET-RAIL tile=14,14 track=0 phase=controller actor=59 head=57  (跑车寻路自己订 X)
:1469 LOCO-MOVE veh=57 tile=14,14 ... dtile=1932
:1481 RESV-EXT-PRE veh=57 tile=13,14 tracks=0x1 reserved=0x0 doRes=0 spd=30   (车在无预留的 X 轨上)
:1485 LOCO-MOVE 13,14 → :1489 12,14 → :1494 11,14 → :1497 10,14 → :1500 9,14
:1487 RESV-WATCH SET-STN tile=12,14 track=255 phase=controller actor=59 head=57  (12,14 也是站台格)
:1490 CPL-PAIRMAP role=target   v=0  vTile=12,15 vSpd=0  vOrd=17 vLen=107 vPairR=57
:1491 CPL-PAIRMAP role=requester v=57 vTile=12,14 vSpd=30 vOrd=16 vLen=11  vPairT=0
:1501 MAXSPD-BREAK veh=57 tile=9,14 spd=30 ... look=0 lookend=-1
:1504 CPL-PAIRMAP role=requester v=57 vTile=9,14 vSpd=9   (日志结束，车在减速，尚未挂上)
```

**判定 6：57 这一趟仍未挂上**（`COUPLE-OK` 只有 `loco=54`），且失败形态与旧日志一致：车离开了预留路径。

**判定 7：两套寻路的目标不是同一个点，这本身就是结构性分岔。**
- 挂接寻路的拼接位：`CPL-BEST tile=5,15`（`cost=10242`），路径走 y=15 站台线（跳站台段）到 `5,15`。
- 跑车寻路的目标：`TRP dest=1932=(12,15)`，即**目标车底所在的格**（`:1490 role=target v=0 vTile=12,15`），不是 `5,15`。
⇒ 车只需开到 `(12,15)`，它在 `15,14` 完全没有理由转到 y=15 线；而挂接寻路为了让车走到拼接面 `5,15`，强行在 `15,14` 订了 LEFT 南下。
**这是"预留的路径 ≠ 车要走的路"的机制性来源**，与"同格重复落订"无关。

**判定 8：车路过目标正北一格却不停，是因为轨道不连通，不是寻路错。**
`:1489 LOCO-MOVE veh=57 tile=12,14 track=01` —— `(12,14)` 就在 `(12,15)` 正北一格，且 `:1487 SET-STN tile=12,14` 说明它同属目标车站。车继续向西说明 `(12,14)` 与 `(12,15)` 之间**没有可通行的轨道连接**（`(12,14)` 一带只有 `tracks=0x1` 单 X 轨），必须绕到更西侧的垂直轨才能南下。
⇒ 车沿 y=14 线西行**在 yapf 眼里是正确路径**，而挂接寻路给的 y=15 线是"更短但车走不到"的路径。

**判定 9：`found=0` 的拒绝计数给出了拒绝面。**
`:1463 CPL-SCAN ... nodes=18 enter=13 noplat=1 nores=6 noconsist=6 consist=0` ⇒ 从 `15,14` 出发的 6 个候选因 `nores`（无预留）与 `noconsist`（无车底）被拒，`consist=0` 表示这段搜索里根本没摸到目标车底。**待查**：`nores`/`noconsist` 的具体判据落点（在 `yapf_destrail.hpp` 的 accept 闸门里），确认它们是否把 y=15 线判死。





### 4.15 补齐 `TRP veh=57` / `CPL-SCAN` 全部读数 + `nores` 判据落点（2026-10-26）

**`TRP veh=57` 全 3 行（原文）**：
```
:392  TRP veh=57 mf=1,15  real=0 type=6  dest=1808(=(16,14)) ... => ok=1 res=1   (开局，57 在 1,15 线，与本事无关)
:1238 TRP veh=57 mf=16,14 real=1 type=16 dest=1669(=(5,13))  ... => ok=0 res=0   (第一次 GOTO_COUPLE 寻路失败)
:1302 TRP veh=57 mf=16,14 real=1 type=16 dest=1932(=(12,15)) fto=1 ... => ok=1 res=1
```
⇒ 判读：跑车 PBS 的 GOTO_COUPLE 目标格被改过两次（`(5,13)` 失败 → `(12,15)` 成功）。`fto=1`（force-track-override / 换目标后重试）只在第三次出现。**跑车侧自始至终瞄准的是"目标车底所在格"，不是挂接侧的拼接位 `5,15`**（判定 7 的独立证据）。

**`CPL-SCAN` 全 2 行（原文，含 tick）**：
```
:1358 CPL-SCAN tick=12551 veh=57 found=1 closed=3  limit=10000 nodes=17 enter=14 noplat=5 nores=0 noconsist=6 consist=3 rej=0/0/0/0 acc=3/0 accwp=0 last=5,15  lastveh=0 why=acc-car
:1463 CPL-SCAN tick=12627 veh=57 found=0 closed=5  limit=10000 nodes=18 enter=13 noplat=1 nores=6 noconsist=6 consist=0 rej=0/0/0/0 acc=0/0 accwp=0 last=-1,-1 lastveh=-1 why=none
```
⇒ **判定 9 补全（重要）**：从 `16,14` 出发那一趟，挂接寻路**确实摸到了目标车底并接受**：`consist=3`（3 次扫到合法车底）、`acc=3/0`（3 次 car-only 接受）、`last=5,15 lastveh=0 why=acc-car`。所以 `CPL-BEST tile=5,15` 不是空转产物，而是"目标链（`veh=0`，`vLen=107`）沿 y=15 站台线一直铺到 `5,15`，被接受为拼接面"。
76 tick 后日志退到 `15,14` 重找（`:1462 CPL-PATHFOUND found=0`）时：`consist=0`、`nores=6` ⇒ **连目标都没摸到**，搜索只关了 5 个节点就死了。

**`nores` 判据落点（读码）**：`src\pathfinder\yapf\yapf_destrail.hpp:491-496`
```cpp
TrackdirBits tdb = TrackdirToTrackdirBits(td);
bool has_res = HasReservedTracks(tile, TrackdirBitsToTrackBits(tdb));
if (!has_res && !IsRailStationTile(tile)) { ++_r3r_cpl_scan_nores; return false; }
```
⇒ 候选目标格必须**本身被（任一列车的）PBS 预留位覆盖**，或者本身是站台格，否则直接不认。这解释了 `15,14` 重找时的 6 个 `nores`：车已经离开那条预留路线、`5,15` 一侧不再有任何预留位覆盖（`RESV-EXT-PRE veh=57 tile=13,14 reserved=0x0` 同证），于是"挂接寻路看得到的拼接面"随车一离开就消失了。

**本轮净结论（合并判定 1-9）**：
1. 探针第 2 版工作正常：`RP-BOOK` 16 行、`SPLIT=0`、`resv=plain/stn/none` 三态都出现过、**无 `rail_map.h:227` 断言**。
2. `(15,14)` 挂接寻路订的是 `td=3/trk=3`（LEFT → `15,15`），**不是 X**；"挂接寻路选了 X"被否证（玩家肉眼看到的 X 是**跑车寻路**的，见 `:1451 LOCO-MOVE track=01`）。
3. 病根 = **两套寻路目标不同** + **预留是"一次性快照"而不是"跟着车走的约束"**：
   - 挂接侧目标 `5,15`（长链最远可拼点），跑车侧目标 `12,15`（目标车底所在格）；
   - 挂接侧订了 y=15 线，跑车侧选了 y=14 线 X；
   - 车一离开挂接预留，`nores` 立即把同一目标判死 ⇒ 再也找不回来（`CPL-SCAN consist=0`）。

**修法候选（待玩家拍板，与 KI-504 原三条并列）**：
- **A（约束传递）**：把挂接寻路在岔道口订下的 track 作为强制约束传给跑车 `TryPathReserve`/`ChooseTrainTrack`（最贴近玩家原话"让跑车沿预留走"）。
- **B（目标对齐）**：让跑车侧 GOTO_COUPLE 的目标格直接取挂接侧 `CPL-BEST` 的拼接位（`5,15`），而不是目标车底当前所在格（`12,15`）——但这仍不保证跑车选同一条轨，需配合 A。
- **C（预留跟随）**：把"挂接预留"从一次性写入改成跟随车体的路径缓存（车每跨一格消费一格，剩余路径保留），避免 `nores` 随车离开而失效。
- **D（兜底）**：`nores` 闸门放宽为"目标格上有合法车底即可"（去掉 `has_res` 前提）——风险=挂接寻路到不可达格给出假路径，需配合可达性校验。

### 4.16 「为什么两套寻路不一致」的机制解释 + 判定 7 表述更正（2026-10-26）

**先更正一处口径（重要）**：`TRP veh=57 ... dest=%u` 打的**不是**"目标车底所在格"，而是 `consist->dest_tile`——见探针定义 `src\train_cmd.cpp:17761`（payload 里 `consist->dest_tile.base() << 16`）与 `:17771`（`(unsigned)consist->dest_tile.base()`）。而 `dest_tile` 在全文件只有 4 处赋值（`:16385` 订单回滚还原、`:17738` 最近车库、`:22485` 自动回库、`:22512`）：

```cpp
/* src\train_cmd.cpp:22509-22513  Train::CheckOrders() 附近 */
/* update destination */
if (this->current_order.IsType(OT_GOTO_STATION)) {
    TileIndex tile = Station::Get(this->current_order.GetDestination().ToStationID())->train_station.tile;
    if (tile != INVALID_TILE) this->dest_tile = tile;
}
```

⇒ **`OT_GOTO_COUPLE` 路径不写 `dest_tile`**，所以挂接期间它保持"上一条/最近一条到站命令算出来的站台基准格"。§4.14 判定 7 里"跑车瞄的是目标车底所在格（12,15）"这句要改成：**跑车瞄的是订单的站点格；只是本次恰好该站基准格与等待车底所在格同为 `(12,15)`**。结论方向不变（两个目标不同源、不同点），但归因必须落在"目标来源不同"，不能落在"车底格"。

**为什么两套寻路必然不一致（三条，架构层，不是某一行写错）**：

| | 挂接寻路（`yapf_destrail` + `rp()`） | 跑车寻路（`yapf_rail` / PBS） |
|---|---|---|
| 回答的问题 | 「我停在哪里能和那列车**拼上**」 | 「我怎么开到**我的目标格**」 |
| 目标格来源 | 沿目标链逐格扫描出来的**拼接面** `CPL-BEST tile=5,15` | 订单算出的 `dest_tile = (12,15)` |
| 额外约束 | 车长/容量/能否贴住/`nores` 预留位/`noconsist` 合法车底 | 轨道连通、道岔、信号、限速 |
| 计算时机 | 一次算完就落订（算完即结束） | 每 tick / 每次到岔道口重算 |

1. **输入不同**：`5,15` 和 `12,15` 是两个不同的点，中间只差几格，但对"该在哪个岔道口拐"的答案完全不同。挂接要 `(15,14)` 拐 LEFT 去 y=15 线，跑车要直行 X 沿 y=14 线 —— 两边各算各的，没人负责把两个答案对齐。
2. **预留不是命令**：PBS 的预留（`SetTrackReservation`）只是"这格我占了"的**占位标记**，不是"你只能走这里"的**许可/指令**。跑车寻路看到自己订过的格当可用，看到**没人订的格也当可用**（不视为障碍）⇒ 它完全可以开进一条自己从没预留过的轨道。现场 `RESV-EXT-PRE veh=57 tile=14,14 tracks=0x11 reserved=0x0` 就是这一条的直接证据。
3. **时间不同步**：挂接寻路订完就撒手，跑车寻路随后每 tick 重算；车一离开挂接订的那串格，`nores` 判据（`yapf_destrail.hpp:491-496` 要求候选格仍被预留位覆盖）立刻把同一目标判死，于是 `:1463 CPL-SCAN tick=12627 found=0 nores=6 consist=0` —— 连目标都摸不到了，也没有任何机制把车"拉回"那条预留线。

**一句话版本（给玩家）**：调度员（挂接寻路）算的是"哪个车钩能对接"，算完把那条线占住就去干活了；司机（跑车寻路）手里只有"终点 12,15 号站台"这张纸条，每秒钟自己重算一次最顺的路。两人各算各的，占位也不等于命令，所以司机照自己的图开，开进了没占过的轨道。

### 4.17 玩家质疑「为什么不走预留」→ 真因更正：跑车目标格被目标车底自己占着（2026-10-26）

**玩家原话要点**：机车在 `16,14`，挂接寻路向 `5,15`，列车实际向 `12,15`；按预留走才是最省最合逻辑的，司机却绕一大圈。

**新取证（日志原文）**：

```
:1429 RP-BOOK dir veh=57 tile=15,14 td=3 trk=3 bits=0x29 resv=plain before=0x8 book=1   ← (15,14) 有三种轨：X(0x1)+LOWER(0x8)+RIGHT(0x20)，挂接订的是 trk=3(LOWER)，即向西南下到 y=15 线
:1410 RP cur=15,15 td=2 ... → :1411 RP cur=14,15 → :1412 RP cur=13,15                     ← 挂接路径沿 y=15 向西
:1431 CPL-PAIRMAP tick=12556 role=target    v=0  vTile=12,15 vLen=107 vOrd=17
:1432 CPL-PAIRMAP tick=12556 role=requester v=57 vTile=16,14 vSpd=12
:1488 CPL-PAIRSCAN tick=12806 act=57 actTile=12,14 actSpd=30 best=0 bestDist=1 paired=-1   ← 差一格，找到目标，但 paired=-1（没配上）
:1489 LOCO-MOVE veh=57 tile=12,14 track=01 dir=5 db=1 dtile=1932                          ← 从 (12,14) 直行 X 继续向西，没停
:1493 CPL-PAIRSCAN tick=12876 actTile=11,14 ... best=0 bestDist=2 paired=-1                ← 已经开过去了
:1491 CPL-PAIRMAP tick=12812 role=requester v=57 vTile=12,14（同一 tick 车底 v=0 在 12,15）
:1501 MAXSPD-BREAK veh=57 tile=9,14 spd=30 ... look=0 lookend=-1 ... db=1                   ← lookahead=null，无路可走，兜底降速
:1504 CPL-PAIRMAP tick=13068 role=requester v=57 vTile=9,14 vSpd=9                          ← 冲过头 3 格后减速
```

**判定（推翻 §4.16 的表述）**：
1. 机车不是「懒得走预留」，而是**在 `(15,14)` 根本没选 LOWER**（否则就是挂接预留那条路）。
2. 跑车寻路的 `dest_tile = (12,15)`，而 `(12,15)` **正是目标车底 `veh=0` 自己的车头格**（`:1431 role=target vTile=12,15`）。对普通 PBS/YAPF 来说，被列车占住的格是**不可进入**的 ⇒ 「到 `(12,15)`」这个目标在跑车寻路眼里**不可达**。
3. 于是唯一能下到 y=15 线的分支（`15,14` 的 LOWER → `14,15` → `13,15`，尽头就是被占的 `12,15`）被当作「到不了目标」的分支剪掉，只剩 y=14 的 X 直行；直行同样到不了，YAPF 给了「能开到的最远处」⇒ 一路冲到 `9,14`，`look=0` 减速。
4. 亲眼数据吻合：在 `12,14`（车底正北一格）`best=0 paired=-1` —— 目标找得到、几何配不上（横向相邻、朝向垂直，不是端对端），所以不停。

**净结论**：矛盾不在「两套目标不同」（§4.16 那层只是表象），而在 **GOTO_COUPLE 的跑车目标格取错**：它取了「目标车底当前占的那一格」（`12,15`），这格对普通寻路是不可进入的，于是带着正确预留的那条分支反而被剪掉。挂接寻路取 `5,15`（穿过车底的拼接面）同样不可行驶。
**正确目标应是「车底端部外侧的第一个空格」**（本例 `13,15` 或 `5,15` 外侧的 `4,15`/`6,15`，取决于从哪一端挂），这样既在物理可达范围内，又能让车鼻顶到车底端部完成命中。

**修法候选（更新，替代 §4.16 的 A~D 优先级）**：
- **A'（首选）**：GOTO_COUPLE 跑车目标格改为「拼接位外侧空格」，来源直接用挂接侧 `CPL-BEST` 邻格，而不是 `dest_tile`。
- **B'**：YAPF 侧把「目标车底所在格」视作可进入（像 destrail 一样），风险=与碰撞/预留语义冲突，不建议。
- **C'（配合）**：`nores` 闸门（`yapf_destrail.hpp:491-496`）放宽为「拼接位本身或其一格邻域有车底即可」，避免车一动目标就失效。

### 4.18 疑点：配对中的等待车底，对跑车寻路为什么仍是障碍（玩家 2026-10-26）

**玩家原话**：「自己配对的列车对于前往挂接的列车来说不应该是空气吗，怎么现在又占位了」

**已知现场事实**（来自 §4.17 取证）：
- `:1431 CPL-PAIRMAP role=target v=0 vTile=12,15`，`:1432 role=requester v=57 vTile=16,14` ⇒ 这一对**已配对**（`vPairR/vPairT` 互指，`tick=12556`）。
- 跑车寻路目标 `dtile=1932=(12,15)`（`:1489`），恰是车底 `veh=0` 的车头格。
- 跑车在 `(15,14)` 不走 LOWER（`trk=3`，挂接预留正是这条路），改沿 y=14 直行 → `12,14`（`:1488 best=0 paired=-1`）→ `11,14` → `9,14`（`:1501 look=0` 兜底降速）。

**存疑点 Q18-A（本条主线）**：挂接寻路（`yapf_destrail`）**明确把目标车底当「可命中、不阻挡」**——否则它不可能订出沿 y=15 线穿过车底的那串预留（`:1410-1412`）。说明 R3R 代码里存在「配对目标是空气」的语义；但跑车侧（PBS + `FollowTrainReservation`）看起来**没有同一豁免**，同一个对象在两套寻路里一个是空气、一个是墙。需定位三点：
- (a) 车底阻挡判定的实际落点：`src\pbs.cpp` 的 `CheckTrainsOnTrack`（或 `src\train_cmd.cpp` 的 `FollowTrainReservation` 同名判据）；
- (b) R3R 是否已有「配对目标豁免」（`R3RIsCoupleTarget` / 配对表 `vPairR` / `R3RPairCoupleTargets` 之类），以及为什么在跑车侧未生效（是没接、还是接了被别的条件挡掉）；
- (c) 若本来就没有豁免，则 §4.17 的修法 A′（改目标格为拼接位外侧空格）与本节修法 D（配对双方对彼此透明）是两条独立路径，必须选一条或组合。

**猜测根因（待验证）**：挂接寻路的「空气」语义是通过 `yapf_destrail` 的 end-node 判据实现的——把目标链识别为**命中终点**而不参与阻挡，这属于「搜索阶段的目标识别」；而跑车寻路走的是 PBS 预留 + 引擎的列车占位表，前者不参与后者的阻挡计算 ⇒ 「空气」只在挂接寻路那一个搜索器内部成立，出了它就不再成立。若成立，则**修 D 的落点必须是跑车侧阻挡判据里加「同配对号免阻塞」**，而不是指望挂接侧的语义自动继承。

**待验证判据**：在 `pbs.cpp`/`train_cmd.cpp` 的阻挡判定里找到（或确认不存在）「other_train 属于本车配对目标 ⇒ 不算阻塞」的分支；并确认 `CPL-PAIRMAP` 打出的配对关系（`vPairR=57`）在同 tick 的跑车寻路里可被读到。

#### 4.18.1 取证结果（2026-10-26，5 组调用）

**结论 1（确定，代码级）：代码里根本没有「配对目标 = 空气」这条语义。** 跑车的「能不能开过去」落在 `Train::TryPathReserveWithResultFlags()` 的 TPR 分支：

```
/* src\train_cmd.cpp:18303-18375 */
Vehicle *other_train = nullptr;
PBSTileInfo origin = FollowTrainReservation(consist, &other_train);
...
if (other_train != nullptr && other_train->index != consist->index) {      // :18310
    if (other_train->tile == consist->tile && consist->cur_speed == 0) {   // :18356 KI-330 解挂缝
        ... TPR-SEAM-EXEMPT ...                                            // :18363 只打日志，不 veto
    } else {
        if (mark_as_stuck) MarkTrainAsStuck(consist);                      // :18372
        return TPRRF_NONE;                                                 // :18373  ← 判"被堵死"
    }
}
```
豁免只有两条：**自己**（`:18310` 的 `index != consist->index`）、**解挂缝**（`:18356` 同格 + `cur_speed==0`）。配对关系（`Vehicle::r3r_couple_target` / `r3r_couple_requester`，`couple_group.h:249-289` 的 `R3RCouplePairMatches()` / `R3RGetCouplePairPartner()`）**在这条链上一处都没被调用**（`r3r_couple_*` 只出现在 `couple_group.cpp` 的配对管理与 `train_cmd.cpp` 的探针里）。

⇒ 「空气」是在**挂接搜索器**（`yapf_destrail` / `R3RCoupleAllowed`）里实现的：它把目标链识别成**可命中的终点**，所以搜索敢穿过它。而跑车的预留确认是**引擎原生逻辑**，只认「是不是我自己」，配对对象在它眼里就是实体列车。两个语义不共享。

**结论 2（重要，修正 §4.17 的过头断言）：本日志里没有 `TPR-BLOCKED veh=57`，所以「目标车底占住预留终点格 ⇒ 被判堵死」这一条在本次现场**没有直接证据**。全日志 `veh=57` 的 TPR 行只有两条 `TPR-WALK`（`TPR-BLOCKED` 是 `R3RDbgEdge` 边沿触发 + 128 帧窗口抑制，缺失不能当反证，但也不能当证据）。

**结论 3（新的、日志里直接可见的出口）：`reachable == TRACK_BIT_NONE`。**
```
:1237 TPR-WALK veh=57 mf=16,14 origin=16,14 td=8 okay=1 exit=2 new=17,14 reach=0 spd=0 one=0 look=1
```
探针注释（`:18407-18414`）自己写明：没判阻塞时只剩两个出口——(a) `reachable == TRACK_BIT_NONE`（什么都不打）或 (b) 进了 `ChooseTrainTrack` 但订不到。本条就是 (a)：`reach=0` ⇒ 连 `ChooseTrainTrack` 都没进（`:18434` 的 `if (reachable != TRACK_BIT_NONE)`）⇒ 无新预留 ⇒ `lookahead` 空 ⇒ 后面 `:1501 MAXSPD-BREAK look=0` 掉速。

**结论 4（时间线补丁）：机车确实冲到了 y=15 线最西端。**
```
:385 TPR-WALK veh=57 mf=1,15 origin=1,15 td=8 okay=0 exit=2 new=2,15 reach=5 spd=0 one=0 look=0
```
`mf=1,15` = 车头在西端 x=1，下一格 `(2,15)` 有轨道（`reach=5`），朝东。⇒ 它从 `(16,14)` 一路沿 y=14 向西冲过头，绕过整个车底，又回到 y=15 线的西端从车底尾部方向靠近；这也解释了玩家看到的「绕一大圈」。

**猜测根因更新（状态：结论 1 成立；「被配对车底占位」部分证伪/降级为待验证）**：
- 成立：**配对语义没有接进跑车的预留/阻挡体系**，这是设计缺口，与本次现象方向一致（玩家直觉「应该是空气」指的就是这个缺口）。
- 待验证：本次 `look=0` 的直接起因是 `reach=0`（结论 3）还是 `TPR-BLOCKED`（结论 1 的 veto，无日志）。两者都指向「车与预留线脱节」，但修法不同：前者补配对豁免 + 终点格，后者只需修终点格。

#### 4.18.2 玩家拍板（2026-10-26）：「探针可以加，然后把自己配对的列车视作空气也可以加（也就是 D+日志）」

即：**同时**做 D（配对豁免）与出口探针，不先只做探针。落地清单：
- D：`train_cmd.cpp` 的 TPR veto 分支（`:18310` 起）追加「`other_train` 是我这一对」豁免，走 `R3RGetCouplePairPartner(consist)`（已公开，`couple_group.h:282`），命中打 `TPR-PAIR-EXEMPT`，不 veto、继续落 `:18434` 的 `ChooseTrainTrack`。
- 日志：新增**状态变化触发**（不是 128 帧窗口抑制）的出口探针 `TPR-EXIT`，四态 `veto / reach0 / choose / ok`，用来一次复测就区分「被配对车底判堵死」与「前方无轨（`reach=0`）」——这正是 §4.18.1 结论 2/3 留下的待判定项。
- 判据：复测后若 `TPR-EXIT state=veto pair=1` 出现且随后转为 `choose/ok`，说明 D 生效且确曾命中该路径；若始终只有 `state=reach0`，则本次与配对占位无关，转 A′。

**修法 D（新增候选，落点明确）**：在 `:18310` 的豁免里追加「`other_train` 是我这一对」：
```cpp
const Train *other_first = Train::From(other_train)->First();
const bool pair_partner = (R3RGetCouplePairPartner(consist) == other_first);
```
为真时不 veto（照 `:18356` 的写法只打一行 `TPR-PAIR-EXEMPT`），继续往下走 `:18434` 的 `ChooseTrainTrack`。安全性依据是 `:18348-18354` 的既有注释：豁免不给任何特权，能不能真占格仍由 PBS 互相排斥决定。

##### 4.18.3 落地实现（已改码，`src\train_cmd.cpp`，未碰任何 `src\*.h`）

1. **D = 配对豁免**（`:18412` 起，`TryPathReserveWithResultFlags()` 的 veto 分支内）：
```cpp
const Vehicle *const tpr_other_first = Train::From(other_train)->First();
const Train *const tpr_pair = R3RGetCouplePairPartner(consist);
const bool tpr_pair_hit = (tpr_pair != nullptr && tpr_other_first != nullptr &&
        tpr_other_first->index == tpr_pair->index);
if (tpr_pair_hit) { tpr_pair_waived = true; }            // 不 veto，继续往下
else if (other_train->tile == consist->tile && ...) { /* KI-330 解挂缝豁免，原样 */ }
else { tpr_note(1, ...); MarkTrainAsStuck(...); return TPRRF_NONE; }
```
`R3RGetCouplePairPartner()` 只在**双向活锁**时返回对方链头 ⇒ 只会豁免那一个配对对象；且它接受任意一节车，内部已归一到链头，故比较用 `Train::From(other_train)->First()`。

2. **`TPR-EXIT` 探针**（`tpr_note` lambda，`:18321`）：**状态变化触发**，不受 `R3RDbgEdge` 的 128 帧窗口抑制（这正是上一轮"日志里没有 `TPR-BLOCKED` 就无法判定"的成因）。四态 `ok / veto / reach0 / choose`，`pair=1` 是修饰位。

3. **两处刻意的设计修正（先实现后自查发现，已改掉）**：
   - **不把"配对豁免"单列成一个状态**。豁免之后本调用还会继续走到 `ok` 或 `reach0/choose`，若同时打两行就是**每 tick 两行**的刷屏（正好刷在被查的机车上）。改成局部 `bool tpr_pair_waived`，只把 `pair=1` 附在真正的出口行上 ⇒ 一次调用恒一行。
   - **去重键用"阻挡车的链头"而不是 `FollowTrainReservation` 报回来的那一节**。同一链内被报回来的车节并不稳定，按车节做键会退化成每 tick 一行。

4. **判据**（复测后照此判）：
   - 出现 `TPR-EXIT veh=57 state=ok pair=1` 或 `state=choose pair=1` ⇒ D 生效，配对车底确实曾被当作阻塞；再看 `look=`/车速是否恢复。
   - 只出现 `state=reach0 pair=1` / `state=reach0 pair=0` ⇒ 与配对占位无关，直接转修法 A′（GOTO_COUPLE 跑车的目标格改为拼接位外侧空格）。
   - 只出现 `state=veto pair=0` ⇒ veto 挡的是**别的**车，与配对锁无关，另起分支。

##### 4.18.4 构建自证（2026-10-26）

- 改动文件仅 `src\train_cmd.cpp`（未碰任何 `src\*.h`、未改 `src\lang\*.txt`）⇒ 增量合法。
- 复用既有入口 `_tmp_inc_build.cmd`（**未新建任何 `.cmd`**）；`build\R3R_incbuild.guard.log` 判 `incremental is safe`。
- `build\R3R_incbuild.done` = `EXIT_CODE=0`；日志 `[2/3] train_cmd.cpp.obj` → `[3/3] Linking CXX executable openttd.exe`。
- 时间链（共两次构建；第二次含注释/顺序微调，为最终版）：`src\train_cmd.cpp` 23:44:35 → `train_cmd.cpp.obj` 23:45:45 → `build\openttd.exe` 23:46:40（52 316 672 B）→ `R3R_incbuild.done` 23:46:43，obj/exe 均晚于源码；日志 `[2/3] Building CXX object ...train_cmd.cpp.obj` → `[3/3] Linking CXX executable openttd.exe`。
- `read_lints`（`src\train_cmd.cpp`）= 0 条。
- exe 字面量核对：`TPR-EXIT veh=` 命中、`state=%s pair=%d` 命中。
- **未做**：游戏内复测（本次改动含行为变更，必须由玩家实跑取日志）。

---

**疑点登记**（本区随时追加，编号 Q1-x）：

- **Q1-1（已基本排除）** 链 6/27 的 `coType=0` 是否只是读档首 tick 的瞬时态？
  → 排除。`nearOrd=0` 是 `tick=11996` 打出的读数（读档后约 1.2 万 tick），属持续态。
- **Q1-2（待验证·主线）** 库内（`inDepot=1`）等待车底的 `ProcessOrders` 在什么路径上会把 `current_order` 设成 `WAIT_COUPLE`？为什么计划 `idx0` 是 17 却没落到 `current_order`？（怀疑：`Stopped`/库内分支早退，或 `R3RSkipCoupleOrdersForRealArtic`/`R3RSkipUnfireableDecoupleOrder` 一类的跳跃把它推过去了，而 `ordCount` 未变 ⇒ `realType` 仍是 17。）
- **Q1-3** 57 的 `GOTO_COUPLE destStn=0` 究竟指向 (5,13) 的路点还是车站？`onStnTile=1` 说明它站在某个站/路点上但不是 (5,13)，故 `COUPLE-WAIT-OFFDEST` 属正常报告，不是缺陷（**这条不是问题 1 的病灶**）。
- **Q1-4** 57 全程 `db=1`（`DrivingBackwards`），且日志尾部它已在移动（`actSpd=30`、`actTile=15,14`）。说明「不启动」的时段主要在 11996 前后，之后它开走了——是本轮**已经换了目标但仍找不到**，还是走完了 `GOTO_COUPLE` 的寻路段才失败，需结合 `COUPLE-FAIL` 的时间戳确认。


## 五、问题 2：本场景自动填充命令插入有问题（取证中）

待填：
- 7 条 `AUTO-ORD` 的 `iv=[begin,end)` / `anchor` / `n` / `borrowed` / `own` / `cur`；
- 5 条 `AUTO-ORD-DROP` 的 `kind/dest/reason`；
- `ORD-IDX-WATCH` 中 `site=ordlist-insert-in` / `decouple-*` 的读数。

**疑点登记**（编号 Q2-x）：

## 六、问题 3：共享调度计划的自动填充（取证中）

待填：
- 共享订单表（`IsOrderListShared` / `first_shared`）与自动记录的交互；
- 是否有 `own=0 borrowed=1` 之外的新形态。

**疑点登记**（编号 Q3-x）：

## 七、复测判据（随结论补全）

待填。
