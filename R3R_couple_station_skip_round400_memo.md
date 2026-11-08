# 第 400 轮 临时分析报告：挂接摸路「站台跳格」不作数，站台永不预订

日期：2026-11-08　范围：`src\pathfinder\yapf\yapf_rail.cpp`（只读取证，**尚未改码**）
关联：KI-569（本轮新增）、KI-563（第 399 轮终点例外）、KI-504/E1b（rp 只读口径）

---

## 一、现场现象（本轮日志锚点，行号待复核）

```
CPL-PAIRMAP role=target v=3 vHead=3 vTile=248,132
LOCO-MOVE veh=0 ... 248,126 → 248,127        (机车自北向南推进)
RP cur=248,127 td=? depth=0 trk=?
RP cur=248,128 td=? depth=1 trk=?
RP cur=248,129 td=? depth=2 trk=?
CPL-BEST tile=248,133 td=1 cost=...
CPL-RESERVE veh=0 reserved=1 next=... path=2 booked=2
RESV-END veh=0 endTile=248,133 endTd=1 endPos=95
CPL-RES-FAIL = 0   CPL-RES-PARTIAL = 0
```

- 机车 `veh=0` 停在 **248,127**，`cur_speed=0`，订单 `order=16`（= `OT_GOTO_COUPLE`）。
- 目标车底 `veh=3` 链头在 **248,132**；挂接终点 `tg=248,133`。
- 摸路（rp）**只走到 depth=2（248,129）就结束**，没有任何 depth=3；最终 `r3r_resv_path` 只有 **2 格**（248,128、248,129），且这两格都订上了（`booked=2`、无 FAIL/PARTIAL）。
- ⇒ 站台段 **248,130~248,133 一格都没订**。机车最多只能把车头停到 248,129 的末端，再往前一格没有预留可进 ⇒ 停车 ⇒ 下一 tick 从同一位置重算 ⇒ **自锁**，挂接永不成立。（与 KI-563 第 399 轮「终点段无人订 ⇒ 自锁」同一族，但**不同的断点**：那次是安全位截断，这次是摸路根本没把站台写进清单。）

## 二、根因（代码级，已逐行核实）

摸路/预订是**两遍法**：第一遍 `rp()` 只摸路、把"要订的格"压进 `r3r_resv_path`；第二遍 `R3RReservePathSequence()` 按该清单逐格预订。站台格在这两遍里被**两次漏掉**：

1. **压清单时就排除站台格**（`yapf_rail.cpp:1676-1681`）：

   ```cpp
   if (!IsRailStationTile(cur) && !IsRailDepotTile(cur)) {
       if (cur != st || depth != 0) r3r_resv_path.push_back(R3RCoupleStep{cur, cur_td});
   }
   ```
   ⇒ 站台/车库格永远不进清单（原意：站台整格位由列车整格预订保护，不逐格订）。

2. **站台跳格的"到达"判定不补任何格**（`yapf_rail.cpp:1693-1695`，本轮新增的收口分支）：

   ```cpp
   if (f3.is_station && f3.tiles_skipped > 0 &&
       R3RSkippedStationStretchContains(cur, f3.new_tile, f3.exitdir, f3.tiles_skipped, tg)) {
       return true;          // ← 直接宣布"到点了"，一格都不压
   }
   ```

   该分支在本现场被**恰好命中**：`TrackFollower::Follow(248,129, td)` 进入站台后一步跳到平台远端，`tiles_skipped=3`、`new_tile=248,133`；`R3RSkippedStationStretchContains()`（`yapf_rail.cpp:1237-1251`，逐步长 `TileOffsByDiagDir(exitdir)`、逐格要求 `IsRailStationTile`、命中 target 即真、到 `new_tile` 即 break）判定"被跳过的 248,130/131/132/**133** 里含 tg" ⇒ 真 ⇒ `rp_impl` 直接 `return true`。

   ⇒ 这与"236,129 是普通轨道、已压进清单"不同：**跳格发生的那一刻，路径其实还要穿过 4 个站台格才到 tg，但那 4 格既没压清单（规则 1）、也没别的分支补（规则 2）**。

3. 第二遍只能订清单里的 2 格 ⇒ `booked=2 == path.size()=2` ⇒ 不触发部分预留收缩（`yapf_rail.cpp:1756-1759`）⇒ 报"预留成功"、预留终点停在 248,129（`:1750-1751` 取 `r3r_resv_path.back()`），而 `RESV-END` 又显示 248,133 —— **读数自相矛盾**，本身就是"清单与终点不同源"的旁证。

结论：**"站台跳格命中 tg"被当成"已到达"，但站台段本身不在预订清单里 ⇒ 该分支在语义上只完成了一半（认路成功、路线未铺）。**

## 三、修法 R-400-1（已落地，最小改动）

在站台跳格分支里，**把 tg 自己压一格**再返回：

```cpp
if (f3.is_station && f3.tiles_skipped > 0 && R3RSkippedStationStretchContains(...)) {
    /* 跳格只证明"路线经过 tg"，站台格不进清单 ⇒ 必须补压 tg 自己，
       让第二遍把它订下来。 */
    if (cur != tg) r3r_resv_path.push_back(R3RCoupleStep{tg, cur_td});
    return true;
}
```

**为什么一格就够**：`ReserveSingleTrack(tile, td)` 对站台格走的是整段平台预订——
`yapf_rail.cpp:550-557` → `ReserveRailStationPlatform(tile, TrackdirToExitdir(ReverseTrackdir(td)))`（`:525-542`）：
从 `tile` 出发沿 `dir` **反向**逐格 `SetRailStationReservation(true)`，直到不再 `IsCompatibleTrainStationTile(tile, start)` 或撞上 `origin_tile`。
传 `td = cur_td`（行进方向的 trackdir）⇒ `ReverseTrackdir` 后是**回退方向** ⇒ 从 tg 往回订到平台头 ⇒ **整段平台（248,130~248,133）一次订满**，正是机车要开上去的那一段。且不会重复订（只压 tg 一格，不会像逐格压那样让第二格撞上"自己刚订的位"被判 `HasStationReservation` 失败）。

**边界与风险（复测要盯）**：
- 若平台已被**等待车底自己**整格预订（它正站在 248,132 上），`ReserveRailStationPlatform` 第一格即 `HasStationReservation` → 返回 false → `res_fail_tile` 被置 → 第二遍走 `booked>0` 的 **PARTIAL** 分支（保留 248,128/129，返回 true）。此时行为与改动前**完全相同**（不引入回归），但也不会修好——那属于"目标车底占着站台整格位"的另一类问题，需另案（参考 KI-563 自订豁免族）。
- 安全位截断（`:755-786`）可能把站台格判成 `IsSafeWaitingPosition` 而截断；但站台格若已是**最后一个**安全位会走 `term_exempt`（`:756-772`）继续订满，与第 399 轮的口径一致，不额外扩展预留。
- 必须验证 `cur == tg` 的边界（tg 恰是 `cur` 时不压，避免压进重复/自身格）。

## 四、未确认项

- **U-400-1**：248,130~248,133 这四格站台当前**是否已被 veh=3 整格预订**。本会话 `CPL-RES-FAIL=0` 只说明"没试过站台格"，不能反推"站台是空的"。R-400-1 落地后第一次复测若立刻出现 `CPL-RES-FAIL/PARTIAL` 且 `tile=248,13x`、`type=1(Station)`、`selfheld=0`，即证实此疑点。
- **U-400-2**：`RESV-END endTile=248,133` 与 `booked=2`（只到 248,129）矛盾，需确认 `SetReservationTarget` 在本路径上被谁写成 248,133（`:1727-1729` 的 `rp_last_tile` 分支 vs `:1750-1751` 的 `back()` 分支）。
- **U-400-3**：`tg=248,133` 与目标车底链头 248,132 差一格（机车鼻尖应停在 248,132 北侧），挂接点算法是否按"车底占格 + 1"给点，未核。

## 五、复测判据（R-400-1 落地后填）

1. 同场景 `RP cur=` 序列应新增/等价出现"站台段被订"的证据：`CPL-RES-OK` 的 `tiles=` 里出现 `248,13x`，或 `CPL-RES-FAIL` 明示站台格。
2. `booked` 从 2 变为覆盖到站台（≥3），且机车能开进 248,130 以上，`LOCO-MOVE veh=0` 出现 248,130+。
3. 最终 `COUPLE-OK`，`cur_speed` 不再长期为 0。
4. 负判据：普通（非站台）线路的挂接路径 `CPL-RES-OK tiles=` 逐字不变。
5. 负判据：目标车底占着站台整格位时仍按 PARTIAL 停在其前，不越权。

---

## 六、落地记录（2026-11-08，仅 `src\pathfinder\yapf\yapf_rail.cpp`，未碰任何 `src\*.h`）

### 6.1 改动 1（原计划）：站台跳格分支补压 tg 一格

在 `yapf_rail.cpp` 站台跳格分支（原 :1694-1696）`return true` 之前插入：

```cpp
if (cur != tg) r3r_resv_path.push_back(R3RCoupleStep{tg, cur_td});
```

并在上方"站台/车库不订"的注释里补一句"唯一例外是站台跳格分支补压的 tg 自己（KI-569）"，避免下次读代码误判为自相矛盾。

### 6.2 改动 2（落地时新发现，必须同改）：安全位截断会吃掉补压的站台格

只做 6.1 是**不够**的：`IsSafeWaitingPosition()` 对"跟随失败"按 `include_line_end` 判安全（`pbs.cpp:2740-2743`「尽头站台的最后一格是安全位」），而补压的 `tg` 用的 `cur_td` 在 `tg` 上未必是合法 trackdir ⇒ `Follow(tg, cur_td)` 失败 ⇒ 判安全。原来 `r3r_last_safe` 是"从后往前第一个安全位"，`tg` 一进来就占了它，**平台前那一格（248,129）就退化成中段截断点** ⇒ 循环在那里 `CPL-RES-SAFE-STOP` ⇒ 站台段照旧一格未订，修法等于白做。故同时改两处（同函数 `R3RReservePathSequence`）：

1. 反向扫描 `r3r_last_safe` 时跳过站台格：`if (IsRailStationTile(steps[k].first)) continue;`
2. 正向循环的安全位判据加同一条前置：`if (!IsRailStationTile(stile) && IsSafeWaitingPosition(...))`

口径依据：站台格**只可能**出现在终点段（`rp_impl` 唯一会压站台格的地方就是 6.1 这一处，且只压 `tg` 一格），它是要开过去的目的地、不是等待位；平台能不能进去由 `ReserveSingleTrack()` 的整段平台预订去判（被占则 `CPL-RES-PARTIAL`，停平台前，不越权）。因此这两条前置的作用域天然只有终点段，不会放宽任何中段截断。

### 6.3 构建自证

- 入口：复用既有 `R3R_fullbuild.cmd inc detach`（未新建任何 `.cmd`）；构建前 `tasklist` 确认 `openttd.exe` 未运行。
- 护栏：`build\R3R_incbuild.guard.log` = `GUARD: incremental is safe (no header/lang file is newer than the newest object)` ⇒ 合法增量。
- 日志 `build\R3R_incbuild.log`：`[2/3] Building CXX object ...\yapf_rail.cpp.obj` → `[3/3] Linking CXX executable openttd.exe`；`build\R3R_incbuild.done` = `EXIT_CODE=0`（03:17:52）。
- 时间戳：`src\pathfinder\yapf\yapf_rail.cpp` 03:13:55 → `yapf_rail.cpp.obj` 03:15:46（8 963 750 B）→ `build\openttd.exe` 03:17:46（52 488 192 B）。
- `read_lints`：该文件 0 条。
- 本轮未新增 `fprintf` 字面量（纯注释 + 既有探针），故不做 exe 串核对。

### 6.4 状态

已实现 + 已编译，**游戏内复测待做**（判据见 §五）。风险点未变：若平台已被等待车底整格占用，`ReserveRailStationPlatform()` 首格即失败 ⇒ `CPL-RES-PARTIAL` ⇒ 与改动前等价（不回归、也不修好），属 U-400-1 的另一案。
