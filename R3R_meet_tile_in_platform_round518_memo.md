# R3R 第 518 轮临时报告：会合点落在站台段内部 ⇒ YAPF 永不可见（found=0）

日期：2026-11-26
状态：已定性 + **方案 A 已落地并编译通过（2026-11-26 03:59，手工增量）**；游戏内复测待做（判据见 §八）。

---

## 一、输入与有效性

| 项 | 值 |
|---|---|
| `build\R3R_debug.log` | 259 662 B，mtime 2026-11-26 03:42:52 |
| `build\openttd.exe` | 54 409 728 B，mtime 2026-11-26 03:36:25 |
| `src\pathfinder\yapf\yapf_destrail.hpp` | 53 243 B，mtime 2026-11-26 03:34:38 |

源码 03:34:38 → exe 03:36:25 → 日志 03:42:52 ⇒ **这份日志就是第 518 轮（`R3RComputeMeetTile` 改为「紧贴车底近端车格一格」，即 meet 1493→…→1438）的首次复测**，采样有效。

对照：第 517 轮版本 meet=1439；本轮 meet=1438。

---

## 二、现场读数（逐条锚点）

### 2.1 目标与会合点

```
:1420  CPL-SNAP veh=15 near=1437,1874 meet=1438,1874 head=1424,1874 tail=1437,1874 rej=-1
```

- 目标车底 = veh 18，占 `1424..1437,1874`；`rej=-1` 表示订单/车底约束全部通过（不是被订单拒）。
- 会合点 = `1438,1874`（KI-684 注释口径：紧贴车尾 1437 的下一格）。

### 2.2 1438 是站台格，站台末端 = 1438

```
:1461  PFX-GEO t=1436,1874 rail=0 rtype=255 tb=0x0 tdb=0x101 res=0x0 stn=1
:1462  PFX-GEO t=1437,1874 rail=0 rtype=255 tb=0x0 tdb=0x101 res=0x0 stn=1
:1463  PFX-GEO t=1438,1874 rail=0 rtype=255 tb=0x0 tdb=0x101 res=0x0 stn=1
:1464  PFX-GEO t=1439,1874 rail=1 rtype=1   tb=0x1 tdb=0x101 res=0x0 stn=0
```

⇒ 站台区间至少到 1438；**1439 才是站台外的普通轨道格**。与第 509 轮 `PFX-GEO` 结论一致。

### 2.3 扫描恒定失败，且 761 个被检查格全部被 LINE 拒

```
:2084  CPL-SCAN tick=41839 veh=15 found=0 closed=95 limit=10000 nodes=856 enter=761
       noplat=0 nores=0 noconsist=0 consist=0 rej=0/0/0/0/761 acc=0/0 accwp=0
       last=1406,1869 lastveh=18 why=LINE
:2119  ... 同上，last=1406,1869
:2153 :2186 :2237 :2289 :2324 :2356 :2392 :2439 :2453  ... 同上
       其后 last 稳定为 1397,1871
:2210  COUPLE-FAIL loco=15 order=16 tx=1455 ty=1874
:2248  COUPLE-FAIL loco=15 ...
:2403  COUPLE-FAIL loco=15 ...
```

- `enter=761` / `rej[4]=761` / `consist=0` / `acc=0`：**每一个被检查的站台格都被 LINE（排号）判据拒绝，零接受**。
- 全日志无 `CPL-BEST`、无 `CPL-RES-OK`、无 `COUPLE-OK`。
- `last=1406,1869` / `1397,1871`：搜索的最后一格在站台**西侧**（1424 以西），并且反复相同 ⇒ 搜索树稳定地穿过站台到达西侧连接区后穷尽。

### 2.4 会合点上的另一列车预留（干扰项，非本次根因）

```
:955  RESV-HAVERES veh=75 tile=1438,1874 ent erdir=0 res=0x1 tracks=0x1 spd=76 real=3 order=1
```

veh 75 另有自己的行驶预留经过 1438/1439，但它不是 found=0 的原因（见 §四 的机制证明：失败发生在 `PfDetectDestination` 是否被调用这一层，早于预留检查）。

---

## 三、源码事实（第 518 轮口径）

`src\pathfinder\yapf\yapf_destrail.hpp`：

- `R3RTakeStaticTargetSnapshot()`（约 :434-512）：从配对锁 `r3r_couple_target` 取车底，若 `!IsRailStationTile(t->tile)` 直接 `return`（`snap_valid=false`）；否则记录 `snap_stn / snap_axis / snap_row`。
- `R3RComputeMeetTile()`（约 :643-692）：第 518 轮口径 = 紧贴车底**近端车格再外一格**（现场 1438）。
- 精确终点判据（约 :719-727）：
  ```
  if (snap_valid && tile != snap_meet_tile) {
      ++_r3r_cpl_scan_rej[4];      // why=7 ⇔ LINE
      _r3r_cpl_scan_last_tile = tile; ...
      return false;
  }
  ```
  它是 `PfDetectDestination` 里**最前面**的一条，早于站台闸、预留检查与车底确认。

⇒ 只要 `PfDetectDestination` 没有被以 `tile == 1438,1874` 调用过，就必然 `consist=0 / acc=0 / why=LINE`。

---

## 四、根因（已确认）

**TrackFollower 进入站台格时会一步跳过整个连续站台段，因此落在站台段内部的会合点（1438）永远不会作为节点被 `PfDetectDestination` 看到。**

机制链：

1. 机车 veh 15 在 `1455,1874`（站台东侧，普通轨），目标车底占 `1424..1437,1874`，站台延伸到 1438。
2. 机车自东向西搜索，经 1454..1439（普通轨，逐格探测，`PFD-MEETMISS` 可见 1457,1874 / 1456,1874 / 1457,1873 / 1456,1873 等）。
3. 从 1439 进入 1438（**站台格**）时，`CFollowTrackT::Follow` 的站台跳段逻辑把 `new_tile` 推进到该站台段的另一端点（≈1424 或更西），`tiles_skipped = length-1`。
4. 于是 `PfDetectDestination` 的调用序列里**没有 1438**；所有被调用的格（跳段落点及其后的西侧格）都不等于 1438 ⇒ 全部 LINE 拒 ⇒ `rej[4]=enter=761`、`consist=0`。
5. 搜索继续到站台西侧 `1397,1871` 一带后穷尽 ⇒ `found=0`；`COUPLE-FAIL` 每 20 tick 复现。

反证（同时解释两轮的现象差异）：

- 第 517 轮 meet=1439（**站台外**，普通轨）⇒ 可见，`found=1`、`CPL-BEST tile=1439,1874` 成立（第 509 轮日志），但零 `COUPLE-OK`：1439 与车尾 1437 之间隔着空的站台格 1438，钩头不相邻。
- 第 518 轮 meet=1438（**站台内**）⇒ 物理位置正确，但 YAPF 不可见 ⇒ `found=0`，连路径都没有。

⇒ 这是一个**结构性两难**：站台末格（正确物理会合点）在 YAPF 里不可见；站台外一格可见但与车尾隔一格。

---

## 五、对第 509 轮两个猜测根因的答案（正式收口）

第 509 轮 §「猜测根因（待验证）」写：H-1 `end=` 实为 `rp()` 回溯链末格（轴向盲扫，`isGtr` 疑为轴向 greater 命中标记）；H-2 `end=` 即 YAPF 目标。

源码（`src\pathfinder\yapf\yapf_rail.cpp`）：

```
:486  const PBSTileInfo oend = FollowTrainReservation(oh, nullptr);   // FSCP-RES  oend=
:526  const PBSTileInfo tend = FollowTrainReservation(tr, nullptr);   // FSCP-REACH end=
:530  Train *g = GetTrainForReservation(tile, tk2); ... gtr = g->First();
:533  fprintf(... "end=%d,%d endtd=%d isGtr=%d" ..., tend.tile, tend.trackdir, gtr == tr);
:572  const PBSTileInfo cpl_end = FollowTrainReservation(cpl_first, nullptr);  // FSCP-CPL end=
```

结论：

- **H-1 证伪**：`end=` 不是回溯链末格，也不是轴向盲扫；它是 `FollowTrainReservation()` 的返回值 = **该列自己预留路径的终点**。
- **`isGtr` 语义**：`gtr == tr`（该格的预留持有者是否就是这条链），与轴向无关。
- **H-2 修正**：`1419,1873` 是 **veh=15 自己上一轮挂接寻路铺下的预留终点**（`FSCP-CPL end=` 同值可证），是「第 508 轮 meet 判定错 → 路径铺到平行线」的**后果**，不是本轮目标。第 509 轮把它读成“终点恒为 1419,1873”属于因果倒置。

⇒ KI-686 的 R-4（`RESV-GHOST couple veh=17` 误清 veh=75 合法预留）与本条独立，仍待验证；本条不推翻 R-4。

---

## 六、疑点登记

- **REG-518-A**：`enter=761` 与 `rej[4]=761` 完全相等、`noplat=0 / nores=0`，说明被检查的格**全部**是站台格且全部落在不同排/不同站。若跳段机制如 §四 所述，落入的应是站台段**两端以外的落点**（普通轨），那些格应计入 `noplat` 而非 `enter`。⇒ 需确认 `enter` 的精确计数语义（是“进入站台格”还是“站台段落点被当作站台格”），否则 §四 的最后一步需要微调表述（不影响主结论：1438 从未被探测）。
- **REG-518-B**：`PFD-MEETMISS` 有 `n=` 计数上限（现场只看到前 40 行），无法直接证明“1438 未被探测”；本报告的证明依赖 `last=1397,1871`（已越过 1438 向西）与 `consist=0`。若要完全闭合，建议给 `PFD-MEET`/`MEETMISS` 加“是否见过 meet 所在站台段”的标记。
- **REG-518-C**：`RESV-HAVERES veh=75 tile=1438,1874 spd=76` 与 `CPL-SCAN` 同 tick 共存；veh 75 是否会在会合点附近长期占用，使“即使找到路径也订不到 1438”，尚未取数（本轮 found=0 更早失败，未走到预留阶段）。

---

## 七、修复候选（方案 A 已落地，B/C 未采纳）

**共同前提**：不要再把「YAPF 接受格 == 物理会合点」当唯一解（第 509 轮修订 16 已封 S5 路线）。

- **方案 A（推荐，改动最小）**：`PfDetectDestination` 的精确判据放宽为
  `tile == snap_meet_tile` **或**「`snap_meet_tile` 是站台格 且 `R3RStaticIsSameLine(tile)`（同站/同轴/同排）」。
  即：平行线（y=1873/1875）仍被拒（KI-684 不回退），但站台同排的跳段落点可接受；`dest_tile` 仍设为 meet，让 A* 尽量靠拢；物理停位/预留终点继续用 `R3RStaticTargetTile()`（S4 已接好）。
  代价：接受格可能是站台跳段落点（西端），需要 TrainCoupleHandler 的几何命中兜底（原本就是它提交）。
- **方案 B（更保守，不改判据）**：`R3RComputeMeetTile` 取**站台外一格**（1439）+ 在挂接接近阶段允许“再多走一格进入 1438”的短距推进（把 `TryPathReserve` 的终点从 1439 延长一格，或对 GOTO_COUPLE 的停位判定给 1 格容差）。风险：触及 PBS/停位语义。
- **方案 C（治本但最大）**：给挂接寻路单独一条“站台段内可见”的探测通道（在 `Follow` 跳段时把中间格也喂给 `PfDetectDestination` 仅做目标匹配，不建节点）。改动在 `follow_track.hpp`/`yapf_rail.cpp`，影响面大。

建议先 A（单点改动 + 已有探针可直接判定），失败再 B。

---

## 八、复测判据（方案 A 落地后）

1. `CPL-SCAN ... found=1 consist>=1 acc>=1`，`why=` 不再是 `LINE`，`rej[4]` 明显下降。
2. 出现 `CPL-BEST tile=` 且其 y 坐标 = 1874（不得为 1873/1875/1876）。
3. 出现 `CPL-RES-OK` / `CPL-RESERVE`，且 `CPL-RES-SAFE-STOP` 的 tile 落在 1438/1439,1874。
4. 出现 `COUPLE-OK loco=15`（本轮全日志为零）。
5. 负判据：1457,1873 / 1456,1873 等平行线格**不得**成为接受格或预留终点；`RESV-GHOST couple` 不新增指向 y=1873 的行。
6. 负判据：第 517 轮 `meet=1439` 的两难现象（停在 1439 不接触）不得复现。

---

## 九、落地（方案 A，2026-11-26 03:53）

唯一改动文件：`src\pathfinder\yapf\yapf_destrail.hpp`（`PfDetectDestination`，第 503 轮 S5 精确判据所在块）。

```cpp
if (snap_valid && snap_meet_tile != INVALID_TILE && R3RStaticTargetValid()) {
    const bool meet_unreachable_in_platform = IsRailStationTile(snap_meet_tile) &&
            IsRailStationTile(tile) &&
            GetStationIndex(snap_meet_tile) == snap_stn &&
            GetRailStationAxis(snap_meet_tile) == snap_axis &&
            R3RStaticIsSameLine(tile);
    if (tile != snap_meet_tile && !meet_unreachable_in_platform) { ... reject(LINE) ... }
    ...
```

语义：
- 会合点是**站台格**（= 落在连续站台段内部、TrackFollower 一步跳段后不可见）时，退化为「同站 + 同轴 + 同排」接受；
- 会合点在站台外（第 517 轮那种 1439 之类）时判据**逐字不变**，仍要求精确命中；
- 平行线（y=1873/1875）因为 `R3RStaticIsSameLine` 的排号比较必假而**仍被拒**，KI-684 不回退；
- 非站台格（车库/普通轨）不放行，`IsRailStationTile(tile)` 显式兜住（`R3RStaticIsSameLine` 对非站台格返回 true，不能单独用）。

顺带订正 `R3RComputeMeetTile` 注释里误用的编号：KI-684 → **KI-687**（纯注释）。

## 十、构建自证（手工增量，2026-11-26 03:59）

改的是 `.hpp`，但只改了一个 inline 函数体内部（无签名/无类型布局变化），且 `yapf_destrail.hpp` 在 `src` 下**只有 `src\pathfinder\yapf\yapf_rail.cpp:16` 一个 include**（其余命中均为注释）。故按记忆 17778948 的手工增量路线执行，未走 `R3R_inc_guard.ps1`（护栏会因 .hpp 新于 obj 判全量）：

1. 删除 `build\CMakeFiles\openttd_lib.dir\src\pathfinder\yapf\yapf_rail.cpp.obj`（确认 `Test-Path` = False）；
2. `cmd /c 'call vcvars64.bat >nul && ninja.exe -C build openttd.exe > build\R3R_inc_manual.log 2>&1'`；
3. 结果：`[2/3] Building CXX ... yapf_rail.cpp.obj` → `[3/3] Linking CXX executable openttd.exe`，日志尾部 `NINJA_EXIT=0`，`error C*` 计数 0；
4. 时间戳链：`yapf_destrail.hpp` 03:53:28 → `yapf_rail.cpp.obj` 03:56:54 → `build\openttd.exe` 03:59:13（54 409 728 B）；
5. `read_lints` 对该 hpp 0 条诊断。

注意：本轮**未新增任何日志字面量**，故无法用 exe 串扫描自证；判定"新代码是否生效"只能靠 §八 的行为判据（若新分支真的救了现场，应出现 `found=1` 与 `COUPLE-OK`）。

## 十一、复测后的下一步

- 若判据 1~4 达成但**未出现** `COUPLE-OK`：说明卡在 `TrainCoupleHandler` 的几何命中/拼接面（见 KI-686 与 KI-684 的 SPLICE-GAP 家族），下一轮转查命中端与拼接端角色是否错位。
- 若出现"停在西端 1424 附近不接触车底"：即方案 A §七 已预判的代价，转方案 B/C（把物理停位收紧到会合点而非接受格）。

---

## 十二、第 519 轮复测（2026-11-26 04:09 现场）：方案 A 已生效，卡在拼接层

**输入**：`build\R3R_debug.log` 293 444 B / LastWriteTime 2026-11-26 04:09:29，晚于本轮 exe（03:59:13）⇒ 采样有效，是新 exe 产生的。玩家口径："KI-684 的修复 A 没有生效"。

### 12.1 判据①达成（目的地判据确实放行了）

```
1402: CPL-SNAP veh=15 near=1437,1874 meet=1438,1874 head=1424,1874 tail=1437,1874 rej=-1
1458: CPL-SCAN tick=41154 veh=15 found=1 closed=855 limit=10000 nodes=701 enter=616
      noplat=0 nores=0 noconsist=0 consist=5 rej=0/0/0/0/611 acc=5/0 accwp=0
      last=1424,1874 lastveh=18 why=acc-car
2141: CPL-BEST veh=15 tile=1438,1874 td=0 cost=0
```

与第 518 轮（`found=0 consist=0 rej=0/0/0/0/761 why=LINE`）逐字对比：`found` 0→1、`why` LINE→acc-car、`rej[4]` 761→611（降到 0 的那部分正是站台同排格被放行）、`acc` 0→5、best 落到 **1438,1874 cost=0**。⇒ **方案 A 在目标匹配层完全按设计生效，不再被 LINE 拒**。

### 12.2 判据②③④的实际落点：拼接端点错位（新缺陷）

```
2144: CPL-HIT site=open loco=15 v=62 len_v=22 len_mf=22 min_diff=1 need=2 fit=2 dx=-1 dy=0 maxd=1 overlap=1 spd=160 dir=1
2149: FOLDCHK COUPLE n=57 worst_gap=221 / FOLDCHK-DIR COUPLE dot=223
      SPLICE-GAP-REJECT COUPLE prev=17 x=23015 y=29992 head=18 x=22792 y=29992 exp=2 dist=223
2279: FOLDCHK COUPLE-FLIP-U n=57 worst_gap=7 / FOLDCHK-DIR COUPLE-FLIP-U dot=9
      OWNER-MOVE from=62 to=18 orders=19 prio=1 borrowed=0
2297: FOLDCHK COUPLE-FLIP-V n=57 worst_gap=213 + FOLDCHK-ACCEPT COUPLE-FLIP-V worst_gap=213
      residual gap (chain still closing up), not a fold
2298: SPLICE-GAP-REJECT COUPLE-FLIP-V prev=15 x=233007... head=18 x=22792 y=29992 exp=2 dist=215
2375: FOLDCHK COUPLE-FLIP-BOTH n=57 worst_gap=1 / COUPLE-PREFLIGHT head=17 last=15
      fold=0 dirfold=1 gap=221 pair=(-1 dir=15, -1 dir=15) mixed=0 onsplice=0
      splice=(15 dir=5, 62 dir=5) spd=160 db=0
2456: RESPACE-AFTER-EDIT couple head=17 px=00 stretch=1 unfixed=0 skip=0 front=1 passes=2 visited=56 capped=0
2459: COUPLE-OK loco=17 rear=18 consist=18 co=1 real=1 type=1 tx=1438 ty=1874 x=23016 y=29992
```

三个读数互相锁定：

1. **命中端在近端**：`CPL-HIT ... v=62 dx=-1 maxd=1`（1px 贴住目标链的**近端**车 62）。x=23015/16 → tile 1438；x=22792 → tile 1424。
2. **拼接端被拿去配链头**：`SPLICE-GAP-REJECT ... prev=17 head=18 dist=223`。`head=18` 是目标链**远端**（1424,1874），与机车链尾 17（1438,1874）相距 223px ≈ 14 格 ⇒ 期望 2px 实际 223px。`dist=215`（FLIP-V）同理。
3. **错端候选被拼接面挡住（原判"强行提交"已更正，见 §12.5）**：`FOLDCHK-ACCEPT ... worst_gap=213 residual gap (chain still closing up), not a fold` 确实放过了候选 2 的折叠判据，但紧接着 `:2298 SPLICE-GAP-REJECT COUPLE-FLIP-V ... dist=215` 把它否决，`:2299 OWNER-MOVE from=17 to=15` + `:2300 A3-ROLLBACK-DONE` 完整回滚 ⇒ **213px 并没有被提交**。

⇒ 玩家看到的"没生效"不是目的地判据回退，而是**第二层缺陷：命中端（车 62 / 近端）与拼接端（车 18 / 远端）不是同一端**，223px/215px 让候选 0 与候选 2 被 `SPLICE-GAP` 拒。真正提交的是 **候选 3（`COUPLE-FLIP-BOTH`，整链倒置）**，它顺手把 `u_first` 换成近端车 62 而对齐成功（详见 §12.5）。

### 12.3 与前几轮已知缺陷的关系

- 同族：记忆 49951234（第 232 轮 KI-340）"命中端与拼接端角色错位"，当时形态是 `prev=66 x=15459 ↔ head=61 x=15428 dist=31`（尾距头 31px≈4 节）。本轮把同一缺陷放大到 223px（目标链更长）。
- 不同族：KI-687（本轮）只改"目标格能否被接受"，与"拼接用哪一端"无耦合关系。
- 因此 KI-687 状态改为「**已修并实测生效**」（第 519 轮逐行复核确认挂接最终成功，见 §12.5）；KI-688 降级为**健壮性残留**（拼接层靠候选 3 兜底、`FOLDCHK-ACCEPT` 无上界）。

### 12.4 下一步取数/修法方向（未拍板）

- 先确认规则：`CPL-HIT` 的 `v` 命中的是目标链的**哪一端**，拼接必须用同一端（命中链尾 ⇒ 应走 `Couple(目标链, 机车链)` 的等价角色交换，或把拼接对改成 `(机车链首, 命中车)`）。
- 副产物：`FOLDCHK-ACCEPT ... residual gap` 这条豁免把 213px 当"链还在收拢"，需要加一个上界（例如 gap 超过 2 个车长就不许 ACCEPT），否则任何错端都会静默合成畸形链。
- 复测判据改为：①`CPL-SCAN found=1 acc>=1`（已达成）②`SPLICE-GAP-REJECT` 的 `dist-exp` 必须 ≤ 2 且 `prev` 与 `CPL-HIT` 的 `v` 同端 ③`FOLDCHK-ACCEPT` 不得再出现 gap>2 个车长的残差 ④`COUPLE-OK` 之后 `RESPACE-AFTER-EDIT` 的 `stretch=0/front=0`。

### 12.5 自我更正：候选 3 在同一 tick 成功，213px 候选被回滚（逐行复核 2288-2380 / 2456-2677）

§12.2 原先的结论"三个翻转候选全部否决 / 被 FOLDCHK-ACCEPT 放行后合成 213px 链"**与日志不符**（该结论已就地替换为"错端候选被拼接面挡住"）；逐行复核得到的时间序如下（全部为 `build\R3R_debug.log` 行号）：

| 行 | 事件 | 读数 |
| --- | --- | --- |
| 2141 | `CPL-BEST` | `tile=1438,1874 td=0 cost=0`（目标匹配层正确） |
| 2144 | `CPL-HIT` | `loco=15 v=62 dx=-1 dy=0 maxd=1 overlap=1` ⇒ 命中目标链**东端** 62 |
| 2149 | 候选 0（直拼）被拒 | `SPLICE-GAP-REJECT COUPLE prev=17 head=18 exp=2 dist=223` |
| 2279 | 候选 1（`COUPLE-FLIP-U`） | `FOLDCHK ... worst_gap=7` → 回滚 |
| 2297 | 候选 2（`COUPLE-FLIP-V`） | `FOLDCHK ... worst_gap=213` + `FOLDCHK-ACCEPT ... residual gap` |
| 2298 | 候选 2 被拼接面否决 | `SPLICE-GAP-REJECT COUPLE-FLIP-V prev=15 head=18 dist=215` |
| 2299-2300 | 候选 2 完整回滚 | `OWNER-MOVE from=17 to=15` + `A3-ROLLBACK-DONE` |
| 2311 | 候选 3 双翻完成 | `FLIP-DONE head=62 nseg=1 whole=1` |
| 2367 | 候选 3 拼接 | `A3-BOTH-FLIP-DONE merged_head=62` |
| 2370 | 实际拼接对 | `ARRANGE-IN dh=17 dst=15 sh=62 src=62` ⇒ `(15@23007, 62@23006) dist=2` |
| 2375 | 候选 3 折叠检查 | `FOLDCHK COUPLE-FLIP-BOTH n=57 worst_gap=1` |
| 2456 | 提交后重排 | `RESPACE-AFTER-EDIT couple head=17 px=0 stretch=1 unfixed=0 skip=0` |
| 2459 | **挂接成功** | `COUPLE-OK loco=17 rear=18 consist=18 co=1 real=1 type=1 tx=1438 ty=1874` |
| 2609 | settle 后几何 | `GEO-END settle-couple head=17 n=57 capped=0`，逐节 `dist==nom`、x 23016→22792 单调无缝 |
| 2669-2677 | 正常起动 | `spd=6` → `53` → `76`，`real=8 curType=6 dest=1453,1875` |

结论修正三条：

1. **候选 3 的整链倒置把角色自动对齐了**：`whole_chain_invert=true` 使目标链链首由 18（西端 1424）变成 62（东端 1437），与 `CPL-HIT` 命中的近端正好同一端 ⇒ `dist=2` 通过。⇒ "翻转只改链序不改像素位置，故永远对不上"这条推论**不成立**（它只对"段序保持"的候选 1/2 成立）。
2. **213px 从未进入合并链**：候选 2 的 `FOLDCHK-ACCEPT` 被 `SPLICE-GAP-REJECT`(:2298) 兜住并回滚；最终链 57 节逐节 `gap=0`。
3. **`FOLDCHK-ACCEPT` 无上界仍是真实隐患，但危害被 `SPLICE-GAP` 二次拦截**：它只在"错端距离恰好落进 SPLICE-GAP 容差"时才会放行畸形链（本现场 215px 远超容差，故拦住）。⇒ KI-688 严重度应由"高（每次必畸形）"降为"中（健壮性/双闸门容差不协调）"。

新增疑点 **U-2（待验证）**：`:2375 COUPLE-PREFLIGHT head=17 last=15 fold=0 dirfold=1 gap=221 pair=(-1 dir=15, -1 dir=15) mixed=0 onsplice=0 splice=(15 dir=5, 62 dir=5)` —— 提交前硬闸门自报 `gap=221 dirfold=1`，而同一 tick settle 后链上 `gap=0`。说明该探针的 `gap` 测量对象/时机与 `FOLDCHK` 的 `worst_gap` 不同（疑似测的是 ARRANGE 之前的物理链或全链最远端点距），需在下一轮核对 `R3RCheckChainFoldedDirection` 的 out 参数取值点，否则"闸门读数与真实链不符"会持续误导判读。

新增待查 **U-3**：本轮日志已证明挂接成功、列车正常驶离（2669-2677），但玩家仍报"没生效"⇒ 需要玩家指明观察对象（视觉错位？后续行车？另一列车？还是第 519 轮之后的新现场），否则无法定位"观感未修好"的真实落点。本轮不再自行猜测。

### 十三、第 520 轮（2026-11-26）：玩家复报「KI-684 并没有修复成功，预留超出了站台」

- 玩家原话（逐字）：「KI684并没有修复成功，预留超出了站台」。
- **与本备忘既有结论的关系**：本备忘 §一~§十二整份讨论的是**挂接寻路 / 拼接端点**（`CPL-SCAN found=0` → 方案 A → 候选 3 兜住 → `COUPLE-OK`），**没有一行涉及"预留轨迹是否越出站台"**。⇒ §12.5 的"挂接成功"不能用来反驳本条；挂接成功 ≠ 预留不越界。§12.5 的 U-3 由此得到回答：玩家观察的是**预留**，不是挂接结果。
- **本轮取数**（`build\R3R_debug.log`）：该文件已被新一轮会话覆盖，开头为读档段（`R3R-VER-REMAP` / `SEGLOAD-PARK` / `R3R-CAPCB` / `SEGTR-SNAP` / `ORD-IDX-WATCH site=load`；veh=15 @1500,1887、veh=18 长链）：
  - `EXT-STOP|reason=off-platform` = **0 命中** ⇒ 第 514 轮 A 方案的站台硬边界在本现场一次都没触发（判据②不达成）。
  - `CPL-ENDTILE|CPL-RES-OK|CPL-RESERVE|COUPLE-OK` = **0 命中**。
  - `RP cur=|EXT-WALK|EXT-ENTRY|EXT-RES|RESERVECONSIST` = **0 命中**。
  ⇒ 该日志尚未跑到预留/挂接段，本轮**无法**给出"预留轨迹 vs 站台范围"的对照读数。
- **疑点 U-4（待验证）**：第 514 轮的站台硬边界只挂在 `R3RReservePathSequence()`（单段挂接路径）。若现场预留由 `ExtendTrainReservation()` / `TryPathReserve` 等常规路径产生，则"站台硬边界"根本不在该路径上。
- **猜测根因 G-1（第 520 轮已定位，待拍板）**：修复**覆盖面不足**——只覆盖挂接单段预留，未覆盖常规行车预留/站台整格位预留。**落点已确认**：越界那 6 格由 `src\train_cmd.cpp:18312 ChooseTrainTrack()`（入口 `R3RResvPhaseGuard("choose-track", ...)`，`:18317`）的行驶 lookahead 预留铺出，与挂接单段预留 `R3RReservePathSequence()` 是两条路径；证据与完整推导见 `R3R_resv_beyond_platform_round509_memo.md` §28.10.1 与新增 §29。验证判据：拿到含 `EXT-WALK`/`RP cur=` 的日志，看预留串是否延伸到站台 `pHead..pTail` 之外，且同一 tick 是否出现 `EXT-STOP reason=off-platform`。
- **待玩家提供**：①跑到预留段、可搜到 `CPL-*`/`RP cur=`/`EXT-WALK` 的日志；②越界的具体坐标（越出哪一端、越出几格）。
- 本轮**零源码改动、零构建**。
