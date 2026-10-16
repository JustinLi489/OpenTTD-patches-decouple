# R3R 第 229 轮：库型 GOTO_COUPLE「先去车库后扫描」未执行 —— 取证与修法

- 日期：2026-10-10
- 数据源：`build\R3R_debug.log`（玩家新日志，旧日志已由玩家删除重建）
- 玩家原话：「**这次证实了一件事，前往车库挂接并没有执行好无论如何先去车库后扫描的原则**」
- 对应 KI：**KI-338**（见 `R3R_KNOWN_ISSUES.md` 末尾「第 229 轮」小节）
  - **编号勘误**：本备忘初稿误写 `KI-336` —— 该号已被「第 223 轮：子分组（A1）与兄弟子分组（A2）的车互相挂接成功」占用（`:10570`）；`KI-337` 也已用于「第 227 轮：克隆隐藏段排程未继承」。本条正名为 **`KI-338`**。
- 状态：**根因已做实 → 已改码 → 已增量编译通过 → 游戏内复测：主症通过**（2026-10-10 复测，判据 1 全中、判据 6 通过、判据 2 前半通过；详见 §八。**注意：§二 引用的日志行号是玩家删除重建前的旧日志，已失效**，新日志行号一律以 §八 为准）

---

## 一、结论（一句话）

库型挂接订单（`OT_GOTO_COUPLE` + `GetCoupleIsDepot()==true`）虽然已经在 `ChooseTrainTrack()` 里被
正确地"改走普通寻路、不再跑耦合寻路"，但**更下一层的 YAPF 起点覆盖没有跟着改**：

```
src\pathfinder\yapf\yapf_rail.cpp:1289
    if (v->current_order.IsType(OT_GOTO_COUPLE)) {
        origin = PBSTileInfo(v->tile, v->GetVehicleTrackdir(), false);
    } else {
        origin = FollowTrainReservation(v, nullptr, FollowTrainReservationFlag::OkayUnused);
    }
```

这条覆盖是**给站台型挂接写的**（注释自陈："FollowTrainReservation 会把本链在站台上的预留一路走到
远端，路径就会伸过站台")。它**不区分出发地是不是车库**：机车停在**自家车库**里时
`v->tile` 就是那个车库格，于是普通寻路被**从车库格内部**起算 —— 而"库格是拓扑死胡同"恰恰是当初
把库型挂接从耦合寻路里摘出去的**同一个理由**（`train_cmd.cpp:13731-13747` 注释）。结果是：

- 路径**能**搜到终点（`CRT … found=1`）；
- 但安全位回溯给不出可停留位 ⇒ `res_dest.okay` 保持 false ⇒ `ChooseTrainTrack()` 落到
  `13826` 的失败分支：标 stuck + 释放预留 + 返回 `TPRRF_NONE`（`ok=0 res=0`）；
- 于是**每 tick 重复**一样的事，机车永远开不出自家车库 —— "先去车库"这一步从未发生。

## 二、现场证据（单变量对照，这是本条最硬的一环）

> **行号失效提示**：本节锚点取自**修前的旧日志**（已被玩家删除重建）。新日志的同形态证据见 **§八**，
> 两处行号不同、内容对应（例如本节 `380 CRT veh=65 order=16 origin=1229,194` ＝ 新日志 `382 CRT … origin=1227,194`）。

全部锚点取自 `build\R3R_debug.log`，同一台机车 `veh=65`、同一个格 `1229,194`（**其自家车库**，
`tileDepot=1`）、同一个目的地 `destTx=964 destTy=133`（`dest=273348`）。**唯一变量 = `current_order` 的类型**：

### 2.1 type=16（`OT_GOTO_COUPLE`，目的地=车库 964,133）→ 永远开不出去

```
361: DEPOT-ARR veh=65 spd=0 real=0(16) curType=16 stuck=0 tileDepot=1 tx=1229 ty=194 destTx=964 destTy=133 destDepot=1 destResv=0 tileEqDest=0 depotDir=2 enterDir=0 tt=65535 impl=0
364: [R3R] CPL-PAIR act=65 tgt=61 dist=326 actTile=1229,194 tgtTile=964,133
378: TPR-WALK veh=65 mf=1229,194 origin=1229,194 td=0 okay=0 exit=0 new=1228,194 reach=1 spd=0 one=0 look=0
379: COUPLE-DEPOT-DIRECT veh=65 tile=1229,194 dest=964,133
380: CRT veh=65 order=16 dir=1 origin=1229,194 td=0 found=1
381: TRP veh=65 mf=1229,194 real=0 type=16 dest=273348 spd=0 fp=0 look=0 one=0 mstuck=0 fto=0 => ok=0 res=0
382: DEPOT-ARR veh=65 spd=0 real=0(16) curType=16 stuck=1 … tileEqDest=0 …
```

同样的一串在 **397/399/400、415/417/418、436/438/439、454/466/468、521/523/524** 反复出现
（`CPL-PAIR dist=326 actTile=1229,194` 一直原地"扫描 326 格外的车底"），全程 `ok=0 res=0`。

读法：
- `379 COUPLE-DEPOT-DIRECT` ⇒ `r3r_couple_depot` 分支确实生效了（没有跑耦合寻路，设计正确）；
- `380 CRT order=16 origin=1229,194` ⇒ **普通寻路的起点被钉在车库格自己身上**（`v->tile`）；
- `381 found=1 但 ok=0 res=0` ⇒ 路径找得到、**预留做不出来**。

### 2.2 玩家在暂停期插入一条同目的地的 `OT_GOTO_DEPOT`（type=2）→ 立刻开出去

```
530: ORD-IDX-WATCH site=ordlist-insert-in … veh=65 … n=-1->7 …
532: ORD-IDX-WATCH site=cmd-skip-in     … veh=65 real=-1->1 impl=-1->1 … n=-1->8
534: … real=1->2 …  536: … 2->3 …  538: 3->4 …  540: 4->5 …  542: 5->6 …  544: 6->7 …
573: DEPOT-ARR veh=65 spd=0 real=0(2) curType=2 stuck=1 tileDepot=1 tx=1229 ty=194 destTx=964 destTy=133 … tileEqDest=0 …
579: TPR-WALK veh=65 mf=1229,194 origin=1229,194 td=0 okay=0 exit=0 new=1228,194 reach=1 spd=0 one=0 look=0
580: CRT veh=65 order=2 dir=1 origin=1227,194 td=0 found=1
581: CRT veh=65 order=2 dir=1 origin=1209,192 td=0 found=1
582: TRP veh=65 mf=1229,194 real=0 type=2 dest=273348 spd=0 fp=0 look=1 one=0 mstuck=0 fto=0 => ok=1 res=1
```

- `530 + 532..544` 是一次**暂停期 burst**：一条 `ordlist-insert-in` + 8 次 `cmd-skip-in`
  （`Commands::SkipToOrder`，只有订单窗口/车库的"跳过"按钮会发），把 n 从 7 推到 8，
  最后停在 `real=0`；index 0 由 GOTO_COUPLE 变成**同目的地的 GOTO_DEPOT**，原 GOTO_COUPLE 退到 index 1。
  ⇒ 这是**玩家手工塞一条 GOTO_DEPOT 来逼机车开去车库**（正是玩家说的"证实"）。
- `580 CRT order=2 origin=1227,194` ⇒ 同一台车、同一格，起点**变成预留末端 `1227,194`**（在车库外的线路上），
  随后 `582 ok=1 res=1` ⇒ **立刻订到路**。
- 之后 `847/848/949/992/995/1010/1066/1236/1237/1266/1267 CRT veh=65 order=2 origin=…` 一路横穿地图，
  `1293 DEPOT-ARR … tx=964 ty=133 … destTx=964 destTy=133 … tileEqDest=1 real=1(16) …`
  ⇒ **到达目标车库**，此时订单索引已推进到 index 1（那条原本的 GOTO_COUPLE，type=16），
  `1295+ CPL-PAIR act=65 tgt=61 dist=0 actTile=964,133` ⇒ 进库后才开始扫描并就地挂上，
  `1358 COUPLE-OK loco=65 rear=62 consist=61 co=0 real=1 type=1 tx=964 ty=133`。

### 2.3 结论

**同一个 `TRP`/`CRT` 代码路径，type=2 成功、type=16 失败；两行 `CRT` 唯一的差别就是 `origin`。**
而 `origin` 在这条路径上唯一的订单类型相关来源，就是 `yapf_rail.cpp:1289` 的那条覆盖。
⇒ 反证："先去车库后扫描"失败**不是**因为目的地解析错、不是 `dest_tile` 错
（`dest=273348` 两种类型都对）、**也不是** `CheckTrainStayInDepot` 把车留在了库里
（`12505` 的判据是"目标库 == 当前库"，964,133 ≠ 1229,194，所以它本来就该开出去 —— 它也确实在
每 tick 尝试 `TRP`，只是每次都失败）。

## 三、代码链（供复核）

| 位置 | 内容 | 结论 |
|---|---|---|
| `src\train_cmd.cpp:12505` | `CheckTrainStayInDepot`：GOTO_COUPLE + 库目的地只在"到达目标库"时留下 | **正确**（车库型被要求开出去） |
| `src\train_cmd.cpp:13747` | `r3r_couple_depot = IsType(OT_GOTO_COUPLE) && GetCoupleIsDepot()` | 正确 |
| `src\train_cmd.cpp:13763` | `if (IsType(OT_GOTO_COUPLE) && !r3r_couple_depot)` → 跑耦合寻路 | 正确（车库型**跳过**耦合寻路） |
| `src\train_cmd.cpp:13806` | `if (res_dest.tile != INVALID_TILE && !res_dest.okay && (!IsType(OT_GOTO_COUPLE) \|\| r3r_couple_depot))` → 用普通寻路补订 | 正确（车库型**走**普通寻路 —— 设计意图"和 OT_GOTO_DEPOT 完全一样"） |
| `src\train_cmd.cpp:13826` | 失败分支：标 stuck + 释放 + 返回无预留 | 本条失败就是落在这里 |
| **`src\pathfinder\yapf\yapf_rail.cpp:1289`** | **`if (v->current_order.IsType(OT_GOTO_COUPLE)) origin = PBSTileInfo(v->tile, v->GetVehicleTrackdir(), false);`** | **← 就是这里没跟着 `r3r_couple_depot` 一起改** |
| `src\pathfinder\yapf\yapf_destrail.hpp:368/381` | 耦合寻路的 `PfDetectDestination` 里也按 `IsType(OT_GOTO_COUPLE)` 过滤 | **不在本条路径上**（库型根本不跑耦合寻路），本轮不动 |

## 四、修法

```cpp
// src\pathfinder\yapf\yapf_rail.cpp  约 1289
if (v->current_order.IsType(OT_GOTO_COUPLE) && !v->current_order.GetCoupleIsDepot()) {
    /* R3R: when coupling, start the path from the loco itself. …（原注释保留） */
    origin = PBSTileInfo(v->tile, v->GetVehicleTrackdir(), false);
} else {
    origin = FollowTrainReservation(v, nullptr, FollowTrainReservationFlag::OkayUnused);
}
```

理由：
1. **零风险面**：站台型挂接（`GetCoupleIsDepot()==false`）判据逐字不变，覆盖保留；
2. **库型挂接变成 OT_GOTO_DEPOT 的同一条路径** —— 这正是 `train_cmd.cpp:13731-13747` 已经写下的设计意图
   （"像 OT_GOTO_DEPOT 一样由普通寻路开到车库"），而 §2.2 的日志已经**实测证明**这条路径在本车本格本目的地可用；
3. 顺带修掉一个隐性坏账：库型挂接**在库里**时起点被钉在 `v->tile`（车格），而不是预留末端，
   等于每次重订都从车格重算、丢掉既有预留的连续性；改成 `FollowTrainReservation` 后与
   `OT_GOTO_DEPOT` 一致（`580 origin=1227,194`、`581 origin=1209,192` 就是预留末端沿伸的形态）。

## 五、未确认项 / 边界

- **U-1**：本条只证明"库里起步"这一形态。库型挂接**在库外线路上行驶中**时，旧覆盖同样会把起点钉在
  车格而不是预留末端；改动后行为与 `OT_GOTO_DEPOT` 一致（预期更稳），但本轮日志**无样本**，需复测观察。
- **U-2**：`CheckTrainStayInDepot` 要求"库型挂接必须到达目标库才留下" —— 若玩家把库型挂接的目的地
  漏填/填成自家库，会退化为"原地等"，不在本轮范围（另案）。
- **U-3**：本轮日志里 index 0 被玩家手工换成 GOTO_DEPOT 之后，**到达目标库时机车是停在 index 1
  （原 GOTO_COUPLE）上完成扫描与挂接的**（`1293 real=1(16)` → `1358 COUPLE-OK … type=1`），
  说明"进库后由 index 1 那条 GOTO_COUPLE 接管并就地挂接"是通的 —— 这正是"先到库、再扫描"应有的闭环，
  修复后 index 0 那条 GOTO_COUPLE 本身就应直接做到这件事（不再需要手工插 GOTO_DEPOT）。
- **U-4**：未排查 `yapf_destrail.hpp:368/381` 对**站台型**挂接是否还有同类"目的地类型不敏感"的问题（本轮零样本）。

## 六、复测判据

1. **正判据**：造一条"机车停在自家车库 + `GOTO_COUPLE` 目的地写**另一个**车库"的订单，机车应**立刻开出去**
   并在同 tick 出现 `TRP veh=N … type=16 … => ok=1 res=1`；日志里对应 `CRT veh=N order=16` 的
   `origin=` **不再是车格/库格**（应等于预留末端或至少不等于 `v->tile`）。
2. **正判据**：到达目标库后 `DEPOT-ARR … tileEqDest=1`，随后 `CPL-PAIR … dist=0` → `COUPLE-OK`，
   **不再需要玩家手工插入 GOTO_DEPOT**（即日志中不再出现"靠 `ordlist-insert-in` + `cmd-skip-in` burst 才开动"）。
3. **负判据 A（站台型挂接不回归）**：站台型 `GOTO_COUPLE` 的 `CRT order=16 origin=` 仍等于机车自己那格
   （覆盖保留），挂接成功、`found=1 res=1` 与第 228 轮表现一致。
4. **负判据 B（普通单：GOTO_DEPOT 不回归）**：`CRT order=2` 的 `origin=` 与行为逐字不变。
5. **负判据 C**：`OT_GOTO_STATION`/`OT_GOTO_WAYPOINT` 等其它订单类型的 `CRT origin=` 不变。
6. **无卡死**：修复后不再出现"同一个 `veh=N … type=16 … dest=<车库格> => ok=0 res=0` 反复刷屏
   而机车原地不动（stuck 累加）的形态。

## 七、构建自证

见 `R3R_KNOWN_ISSUES.md` 的 KI-338 条「构建自证」段（复用既有 `_tmp_inc_build.cmd`，未新建任何 `.cmd`）。

---

## 八、实机复测取证（2026-10-10，玩家删除重建后的新日志）

- 数据源：`build\R3R_debug.log`（**148 344 B，2026-10-10 09:49:09**，约 1 595 行）。生产者 = 含第 229 轮修复的 `build\openttd.exe`（2026-10-10 09:22:51，51 680 768 B）。
- **本节行号一律以该新日志为准**；§二 的旧行号已失效。

### 8.1 判据 1 **完全命中**（修复生效的铁证）

同一车 `veh=65`、同一**自家车库格** `1229,194`（`tileDepot=1`）、同一目的地 `964,133`（`dest=273348`）：

```
375: DEPOT-ARR veh=65 spd=0 real=0(16) curType=16 stuck=0 tileDepot=1 tx=1229 ty=194 destTx=964 destTy=133 destDepot=1 destResv=0 tileEqDest=0 depotDir=2 enterDir=0 tt=65535 impl=0
381: COUPLE-DEPOT-DIRECT veh=65 tile=1229,194 dest=964,133
382: CRT veh=65 order=16 dir=1 origin=1227,194 td=0 found=1
383: CRT veh=65 order=16 dir=1 origin=1209,192 td=0 found=1
384: TRP veh=65 mf=1229,194 real=0 type=16 dest=273348 spd=0 fp=0 look=1 one=0 mstuck=0 fto=0 => ok=1 res=1
```

对照：
- **修前**（§二 / KI 条目原文）：`CRT veh=65 order=16 … origin=1229,194`（**车格**）→ `TRP … type=16 => ok=0 res=0`，反复刷屏。
- **修后**（本节）：`CRT … origin=1227,194`（**预留末端，不再是车格**）→ `TRP … type=16 => ok=1 res=1`。
- 且 `origin=1227,194` 与 §2.2 里玩家手工插的 `OT_GOTO_DEPOT` 形态 `580 CRT order=2 origin=1227,194` **逐字一致** ⇒ 库型挂接确实已并入 `OT_GOTO_DEPOT` 的同一条路径。

⇒ 判据 1 的两项（"立刻开出去 + 同 tick `type=16 => ok=1 res=1`"、"`CRT order=16` 的 `origin=` 不再是车格/库格"）**全中**。

### 8.2 判据 2：**前半通过**，后半（后扫描→挂接）本日志**未走完**

- 前半（**通过**）：全日志 `ordlist-insert-in` = **0 命中**、`cmd-skip-in` = **0 命中** ⇒ **不再需要玩家手工插 GOTO_DEPOT 逼车出库**（对比 §2.2 的 `530/532..544` burst 已消失）。
- 车随后**自己按 `GOTO_COUPLE` 一路开到目标库**（CRT origin 逐段推进：`563 1184,185 → 646 1183,174 → 881 1174,138 → 1052 1137,138 → … → 1520 968,134`），末端：
  ```
  1530: CPL-PAIR act=65 tgt=61 dist=15 actTile=978,134 tgtTile=964,133
  …
  1560: CPL-PAIR act=65 tgt=61 dist=1  actTile=965,133 tgtTile=964,133
  1565: CPL-PAIR act=65 tgt=61 dist=0  actTile=964,133 tgtTile=964,133
  1569/1573/1577/1581/1585: … dist=0 actTile=964,133 tgtTile=964,133（连续 dist=0）
  ```
  同步旁证（目标库里有**另一条链**也在原地等挂，与本次交织）：
  `1562/1567/1571/1575/1579/1583 SKIP-STOPPED veh=23 order=16 real=16 spd=0 tile=964,133 parked=0 front=1 nord=7 idx=0`、
  `1561/1566/… SKIP-STOPPED veh=22 order=17 real=17 spd=0 tile=964,133 parked=0 front=1 nord=14 idx=0`、
  `NOABSORB-SEG veh=61 n=0 len=4` 反复。
- **本日志中未观察到 `COUPLE-OK loco=65`**（全日志 `COUPLE-OK` 仅 `779 loco=2 rear=3`、`1306 loco=11 rear=12`，两者是**另一场景**：`order 16 dest=1073,139/1083,139`，与本车无关），也**没有** `COUPLE-FAIL loco=65`。
- ⇒ 判据 2 的"`dist=0` → `COUPLE-OK`"这一步**本日志没有走完**。**待玩家确认**：(a) 日志是被截断（例如中途读档——日志 `1586+` 出现 `SEGLOAD-PARK …` 读档段）；还是 (b) 真的卡在库里没挂上。**若 (b) 成立**，这是"先去车库"修好后**暴露的下一环节（后扫描）新现象**，应**另立条目**取证，本轮不臆断根因。

### 8.3 判据 6（无卡死）**通过**

全日志 `type=16` 的 `veh=65` 只出现 **1 条 `TRP`**（即 `384 … ok=1 res=1`），不存在"同一车 `type=16 => ok=0 res=0` 反复刷屏、原地不动"的形态。
其余含 `type=16` 的行均与 veh=65 无关：`R3RDUMP-ORDER`（`108/111/152/158/1693/1696/1737/1743`，订单快照）、`LOCO-ORD`/`L-ORD`（`501/504/517/520/545/548/953/956/1174/1177`）、以及 `499/536/624 TRP veh=2` 与 `1172 TRP veh=11`（另一条链，`1073,139`，**站台型**，不在本修复范围）。

### 8.4 负判据 3/4/5 初查

- **判据 3（站台型挂接不回归）——旁证通过**：`483/484 CPL-ENTRY/CPL-ORIGIN veh=2 … origin=1073,139 td=8`、`1133/1134 … veh=11 … origin=1073,139`，**起点仍按车格起算**（覆盖保留），且二者最终 `779/1306 COUPLE-OK` ⇒ 站台型挂接行为未被本次改动影响。
- **判据 4（`GOTO_DEPOT` 不回归）——本轮无样本**：新日志**没有** `CRT order=2`（玩家本次没插 GOTO_DEPOT），未覆盖。
- **判据 5（其它订单类型）——本轮无样本**：未见 `CRT order=` 其它类型，未覆盖。

### 8.5 小结

| 判据 | 结论 |
|---|---|
| 1 正判据（开出去 + `type=16 ok=1 res=1` + origin 非车格） | **通过**（§8.1，硬证） |
| 2 正判据（到库后 dist=0 → COUPLE-OK，无手工插单） | **前半通过**（无手工插单）；**后半未走完**，待玩家确认日志是否截断（§8.2） |
| 3 负判据 A（站台型覆盖保留） | **旁证通过**（§8.4） |
| 4 负判据 B（GOTO_DEPOT 不回归） | 本轮**无样本** |
| 5 负判据 C（其它订单类型） | 本轮**无样本** |
| 6 无卡死 | **通过**（§8.3） |

⇒ **KI-338 主症（库型挂接开不出自家车库）＝游戏内复测通过**；"后扫描"环节如有卡住按 §8.2 另立条目。
