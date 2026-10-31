# R3R 第 344 轮：挂接寻路「全面接线普通寻路」（玩家强令执行）

## 0. 玩家原话（口径，不得再以「已否决」为由搁置）

> 「算了，我只好死马当活马医了：我命令你，执行挂接寻路全面关于普通寻路的接线」

背景：同一方案 2026-10-27 曾被 AI 以三条理由否决，记录在 `R3R_KNOWN_ISSUES.md:18829`（第 343 轮 §27）：

1. 挂接寻路本就是 YAPF 另一实例（`CYapfCoupleRail::stFindNearestCoupleTrain` vs `CYapfRail::stChooseRailTrack`），差别只在终点函数 `CYapfDestinationTrainRailT`；
2. 普通寻路把等待车底的整格预留当障碍 ⇒ 必然 `path_found=false`；
3. 本次失败点在 `FSCP fail=tryReserve`（路已找到之后的安全位检查），换寻路器会在同一处失败。

**玩家本轮命令推翻了该否决。** 本文档只记录「如何执行」，不再重复否决意见。

## 1. 为什么「接线」在技术上确实能绕过 KI-358 / KI-510（对否决理由③的更正）

关键差异在 **back-walk 阶段的安全位判定函数**，不在寻路本身：

| | 普通寻路 `CYapfRail` | 挂接寻路 `CYapfCoupleRail` |
|---|---|---|
| back-walk 判定 | `FindSafePositionOnNode()`（`src/pathfinder/yapf/yapf_rail.cpp:410`） | `FindSafeCouplePositionProc()`，唯一调用点 `CheckSafePositionOnNode()`（`:427-435`） |
| 语义 | 返回值 false = **找到一个安全等待位** | 返回值 false = **此格不通** |
| 是否订路 | 不订（只判几何：signal / depot / waypoint / 90° / 尽头） | 逐格 `TryReserveRailTrack()`（`:199` 一带） |
| 遇到交叉预留冲突 | **不受影响**（不订 ⇒ 不冲突） | 整条路径作废 ⇒ `CPL-SAFE-FAIL` ⇒ 停在原地 |

⇒ 现场 `:1721 FSCP tile=14,15 fail=tryReserve tbits=0x9 res=0x8` 的形态（veh=54 占 `TRACK_Y`，veh=57 的路径要 `TRACK_X`，两轨在该岔道格几何交叉，PBS 禁止交叉预留）**只发生在挂接寻路的 back-walk 里**。普通寻路走的是 `FindSafePositionOnNode`，它会在 54 占用的 `14,15` **之前**挑一个安全位停下，把预留终点收缩到那里 —— 这恰好就是第 343 轮 §27 里方案 γ 想要的效果，而普通寻路**已经自带**该能力。

⇒ 否决理由③（"换寻路器会在同一处失败"）**不成立**，本轮据实更正。

## 2. 落地设计（不删能力，改为「CPL 优先 + 普通寻路兜底」）

不做「删掉 `DoTrainCouplePathfind` 改用 `DoTrainPathfind`」这种替换式改法（那会丢掉"以等待车底为终点"的能力，与玩家既有的 `OT_GOTO_COUPLE` 语义冲突）。改为**接线层**：

```
ChooseTrainTrack()  CPL 分支
  └─ DoTrainCouplePathfind()            // 现有：CYapfCoupleRail，终点 = 等待车底
       ├─ 成功 ⇒ 原样返回（逐字不变）
       └─ 失败（INVALID_TRACK）⇒ 新增普通寻路兜底
            └─ DoTrainPathfind()        // 现成函数，src\train_cmd.cpp:15999
                 ⇒ 得到「到最近安全位为止」的 track + path_found + dest + final_dest
                 ⇒ 走正常 reservation 返回路径（CTTRF_RESERVATION_MADE / lookahead）
```

要点：

- **只对挂接订单（`OT_GOTO_COUPLE`）且非车库生效**，与 KI-457 / KI-504 / KI-357 同域；其它订单逐字不变。
- CPL 成功时**一字不改**（负判据：正常挂接路径不变）。
- 兜底成功后列车会开到「54 之前的安全位」等待，`ProcessOrders` 不改订单，下一 tick 重试 CPL，54 让开后自然挂上。这与 KI-193 的 `COUPLE-DEST-IDLE-LIMIT` 语义相容。
- **不再把 `INVALID_TRACK` 直接交回上层**（那是当前 93 次 `CPL-SAFE-FAIL` 的来源）；只有「CPL 失败且普通寻路也失败」才回退到现有失败处置（`Free` + 条件 `MarkTrainAsStuck`）。

## 3. 落点（精确到函数）

- `src\train_cmd.cpp:16011-16014` `DoTrainCouplePathfind()` —— 接线点。当前签名 `Track DoTrainCouplePathfind(const Train *v, bool do_track_reservation)`，只回传 Track，**没有** `path_found` / `dest` / `final_dest` 出参 ⇒ 兜底必须扩签名（或改在调用点分流）。
- `src\train_cmd.cpp:15999-16003` `DoTrainPathfind()` —— 兜底用的普通寻路，签名齐全（`path_found`、`dest`、`final_dest`）。
- 调用点在 `ChooseTrainTrack()` 的 CPL 分支（待确认精确行号，见 §4 U-1）。

## 4. 未确认项（本轮必须先取数再改）

- **U-1**：`ChooseTrainTrack()` 内 CPL 分支的精确行号、`DoTrainCouplePathfind` 的实参形态、以及该分支拿到 `INVALID_TRACK` 之后走了哪条出口（是否与 `CPL-RET why=cpl-fail` 同一处）。**未取到不得改码。**
- **U-2**：兜底调用 `DoTrainPathfind` 时，`dest` / `final_dest` 是否会被 CPL 分支后续代码复用（若复用，兜底会污染 `dest_tile`，可能让订单误判到达）。
- **U-3**：兜底路径的终点会落在「安全位」而非「等待车底」，`TryPathReserveCore` 拿到 `CTTRF_RESERVATION_MADE` 后是否会把 `current_order` 判成已到达（必须确认不会推进 `OT_GOTO_COUPLE`）。
- **U-4**：`r3r_couple_depot`（车库型挂接）是否走同一分支，需排除。

## 5. 复测判据（草案）

1. **正判据**：57 同类场景下 `CPL-SAFE-FAIL` 之后**同 tick** 出现兜底生效痕迹（新增探针），车不再永久 `STUCK=1`，而是开到 54 之前的安全位停住（`spd` 归 0、`look` 非空、`one` 不再残留）。
2. **负判据 A（关键）**：正常挂接（CPL 成功）路径逐字不变 —— `CPL-PATHFOUND found=1` / `CPL-BEST` / `COUPLE-OK` 序列与改前一致，不出现兜底探针。
3. **负判据 B**：非挂接订单（直行/进站/库内调度）`TRP`/`CPL-*` 逐字不变。
4. **负判据 C**：车库型挂接（`COUPLE` 到库）行为不变。
5. **回归**：KI-509（`nores-couple-blocked`）、KI-359 P1（`RESV-NOLOOK-PASS`）、KI-357（`RESV-FOREIGN-RES`）三条既有判据均不得回退。

## 6. 实际落地（与 §2 设计的差异，据实记录）

取数后发现 §2 的「`DoTrainCouplePathfind` 内部回退」不可行：该函数（`train_cmd.cpp:16011`）签名只回传 `Track`，没有 `path_found`/`dest`/`final_dest` 出参，无法在函数内部表达「普通寻路成功/失败」。因此接线落在**调用点**：

- 位置：`src\train_cmd.cpp:17603` 的 `Track path_found = DoTrainCouplePathfind(consist, true);` 之后（`ChooseTrainTrack()` 的 CPL 分支内）。
- 逻辑：CPL 返回 `INVALID_TRACK` 且满足四道守卫时，调用现成的 `DoTrainPathfind()`（`:15999`，签名齐全）再算一次；`r3r_pf_found && track != INVALID_TRACK` 才把结果写回 `path_found`，从而绕开下面 `if (path_found == INVALID_TRACK)` 的 KI-316 处置（撤预留 + MarkTrainAsStuck）。
- 四道守卫（缺一不可）：
  1. `r3r_wait_cut == 0` —— 等待区截断态必须停在路点（否则回归 KI-433）；
  2. `!R3RCoupleSearchSkipped(consist)` —— 节流跳过不是失败（否则回归 KI-271 的铺→清→铺抖车）；
  3. `!IsRailDepotTile(consist->tile)` —— 车库格走 depot 寻路；
  4. `consist->current_order.IsType(OT_GOTO_COUPLE)` —— 只对本轮目标订单生效。
- 探针：`CPL-PF-FALLBACK veh= tile= ret= tracks= dest= final=`，按 `(veh, ret|tracks|tile)` 签名去重。
- 不改 `yapf_rail.cpp` 一行（避免那次 692 步 / 约 16~20 分钟的全量重编，也避免碰 desync 冻结区）。

## 7. 构建自证（2026-10-28）

复用既有 `_tmp_inc_build.cmd`（未新建任何 `.cmd`），内测树 `build\`，探针 ON：

- `build\R3R_incbuild.done` = `EXIT_CODE=0`
- 日志尾部：`[2/3] Building CXX object CMakeFiles\openttd_lib.dir\src\train_cmd.cpp.obj` → `[3/3] Linking CXX executable openttd.exe`
- `build\openttd.exe` @ `2026-10-28 02:25:28`，52 344 832 B（改前约 51.7 MB，增量来自新探针字面量）
- `read_lints` 0 条

## 8. 状态

- 2026-10-28：**已实现 + 已编译，游戏内复测待做**。
- 已知局限（必须对玩家讲清）：接线**不改变几何**。若 54 一直占着 `14,15` 的 `TRACK_Y`，普通寻路也只能把车停在 54 之前的安全位等待，与「原地钉死」在结果上差别有限；真正的差别是**车会走到离车底最近的可停位置并保持一段有效预留**，而不是在 `16,14` 用 `best_track` 自行选方向乱走（KI-479 那次沿 y=14 西行）。要真正挂上，仍需 54 让开或改变车体位置。
