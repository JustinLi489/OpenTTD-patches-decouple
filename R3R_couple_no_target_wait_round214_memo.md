# R3R 第 214 轮：删除「找不到挂接目标就回滚普通寻路」（KI-316 变体 (a) ⇒ 回退为原地等待）

- 日期：2026-10-03
- 改动文件：`src\train_cmd.cpp`（唯一；未碰任何 `src\*.h`、未碰 `src\lang\*.txt`）
- 关联条目：KI-316（本轮回退）、KI-317（回退后按玩家口径改判为设计行为）、KI-322（本轮的决策来源，已执行）、KI-320 / M2（保留不动）、KI-323（本轮新增残余）
- 一句话结论：**删除第 212 轮加的「CPL 全图无候选 ⇒ 改走普通寻路驶向订单目的地」整块回退**；无处可挂时机车**停在原地等待**（丢预留 + Stuck + 每轮重试 CPL），从而停在**机待线**上等车来。

---

## 一、玩家口径（本轮唯一决策依据）

玩家 2026-10-03 原话：

> 全图找不到符合条件的车就立刻回滚？这可不行，这不是我想要的。我想要的是如果没有车等挂机车就在机待线等到有车。我认为应该删除

拆成两条可执行语义：

1. 「全图找不到符合条件的车」**不得**触发任何「换寻路器 / 换动作」的替代 —— 尤其不得驶向订单目的地；
2. 正确行为 = **在机待线原地等**；等有车（合法 `OT_WAIT_COUPLE` 目标）出现后，CPL 下一轮重试即可自行订路过去，无需玩家干预。

## 二、被删对象的精确定位（改动前）

- 主体：`src\train_cmd.cpp:13443-13497` 的 `if (path_found == INVALID_TRACK)` 分支内，`if (res_dest.tile != INVALID_TILE && !res_dest.okay) { … }` **整块**（`:13469-13494`）。
  - 探针原文：`COUPLE-NOTARGET-FALLBACK veh=%d tile=%d,%d dest=%d,%d ord=%d`（`:13484`）。
  - 关键调用：`DoTrainPathfind(consist, r3r_fb_tile, dest_enterdir, tracks, r3r_fb_found, do_track_reservation, &res_dest, &final_dest)`（`:13472`）。
  - 成功即 `HandlePathfindingResult(true)` + `CTTRF_RESERVATION_MADE` + `return { best_track, result_flags }`（**绕过**后面的 `MarkTrainAsStuck()`）。
- 随之成为死代码的节流表：声明 + 注释 `:11624-11631`（`_r3r_couple_fb_news`）、`TrainCoupleHandler` 里的清表 `:11735-11737`。
- 第 213 轮的两处放宽（`:13463` 的门 `(!consist->current_order.IsType(OT_GOTO_COUPLE) || r3r_couple_depot)`，以及下方「路径找到但订不下」块）**保留不动** —— 它们是 M2（车库型直去车库）与站台型正路所需。

## 三、改动（唯一）

1. 删除 `:13469-13494` 整块；分支尾部保留原样四条：`MarkSingleSignalDirty()` → `FreeTrainTrackReservation()` → `mark_stuck ? MarkTrainAsStuck()` → `return { FindFirstTrack(tracks), result_flags }`，即**回到第 212 轮之前的语句序列**。
2. 删除 `_r3r_couple_fb_news` 的声明 + 文档注释（原 `:11624-11631`）与其清表（原 `:11735-11737`）。
3. 重写该分支注释：说明「回退已在第 214 轮按玩家口径删除、正确行为是原地等待」。

## 四、删除后的行为（逐条）

| 场景 | 行为 |
|---|---|
| 站台型 `GOTO_COUPLE`，全图无合法目标 | CPL 返回 `INVALID_TRACK` ⇒ 丢预留 + `Stuck` ⇒ **停在原地**；`CPL-PATHFOUND found=0` 每轮重试；订单保留 |
| 同上，但机车已停在机待线 | 同上 ⇒ **在机待线等车**（= 玩家要的语义） |
| 机车不在等待位（订单目的地 ≠ 当前位置） | **不会自行前往**订单目的地（本轮口径的必然代价，见 §五 R-2） |
| 车库型 `GOTO_COUPLE`（M2） | 不变：`COUPLE-DEPOT-DIRECT`，直接开去车库 |
| 有合法目标出现 | CPL 命中 ⇒ 正常订路开过去（无需玩家干预） |
| 候选就在眼前却一直挂不上（M3） | 不变：`COUPLE-SKIP-COMMIT-FAIL`，4 个窗口 ≈ 1 分钟后跳过订单 |

## 五、残余与回归风险（须让玩家知情）

- **R-1（KI-316 本体回归 = 设计行为）**：第 210 轮报告的「停在机待线不动」本轮**撤销「缺陷」定性** —— 玩家已明确「这就是我要的」。若将来再出现「该去挂却没去」的现场，请优先看 `CPL-GATE` / `CPL-PATHFOUND found=?`：`found=0` 表示**全图确无合法目标**（含「目标被判为不可挂」），不是寻路 bug。
- **R-2（新增 KI-323，中）**：删除后机车**永不自行前往订单目的地**。若某玩法把 `GOTO_COUPLE` 的订单目的地当作「到那里去等」，而机车并不在等待位上，机车会**就地**等待而不是开过去。判据＝「订单目的地与机车当前格不同 + 无合法目标 + 机车不动」（注意 `COUPLE-NOTARGET-FALLBACK` 这个探针**已不存在**，不能再拿它当判据）。
- **R-3（KI-317 复归）**：`found=0` 型卡死又回到「完全没有自愈出口」（KI-289 的 `COUPLE-WAIT-HEAL` 仍够不着：入口需 `pf.reverse_at_signals` 周期命中或 `TPRRF_REVERSE_AT_SIGNAL`）。按第 214 轮口径，**「没有自愈」正是期望**（原地等），故不作为缺陷；若玩家希望「等超过 N 分钟就告警 / 跳过」，需另开需求。
- **R-4（M3 未动）**：「等太久就放弃」（≈1 分钟跳过订单）**仍在**。它与本轮删掉的「立刻回滚」是两个不同触发器（M3 只在「有候选却挂不上」时启动）。玩家上一轮的描述把两者混在一起，**本轮只删了 M1**；M3 是否也要删，待玩家明确。
- **R-5（第 213 轮 M2 保持）**：车库型直去车库未受本轮影响。

## 六、未做

- 未改 M2 / M3；
- 未重编 `build-release`（只重编 `build` 内测树）；
- 未做游戏内复测；
- KI-316 条目里「满 500 tick 后跳过命令」的过期表述已在 KI 侧标注（第 109 轮旧行为，第 112 轮起改为保留订单）。

## 七、复测判据（4 条）

1. 复跑第 210 轮场景：`veh=51` 停在 99,108 时**不再**出现 `COUPLE-NOTARGET-FALLBACK`（新 exe 内该串**不存在**），且机车**不移动**、订单保留；
2. 日志应仍见 `CPL-PATHFOUND veh=51 found=0` 周期性重试（＝原地等）；
3. 一旦把一台合法 `WAIT_COUPLE` 车底放进订单目的地附近 ⇒ 机车应在下一个重试窗口自行开过去并 `COUPLE-OK`；
4. 车库型 `GOTO_COUPLE` 仍出 `COUPLE-DEPOT-DIRECT`（M2 未回归）。

## 八、构建自证

复用既有 `_tmp_inc_build.cmd`（未新建任何 .cmd，遵守构建脚本规则）：

| 项 | 值 |
| --- | --- |
| 护栏 | `build\R3R_incbuild.guard.log` = `GUARD: incremental is safe (no header/lang file is newer than the newest object)` ⇒ 本轮只改 .cpp，增量合法 |
| 判决 | `build\R3R_incbuild.done` = `EXIT_CODE=0` |
| 源码 | `src\train_cmd.cpp` LastWriteTime = 2026-10-03 19:15:37 |
| 目标文件 | `build\CMakeFiles\openttd_lib.dir\src\train_cmd.cpp.obj` = 19:17:30（晚于源码） |
| 可执行 | `build\openttd.exe` = 19:18:30（51 672 576 B，晚于 obj） |
| 错误计 | 日志 `error C*` / `fatal error` / `FAILED:` / `build stopped` = 0 |
| lint | `read_lints` over `src\train_cmd.cpp` = 0 条 |

探针字面量核对（`Select-String -SimpleMatch` on `build\openttd.exe`）：

- `COUPLE-NOTARGET-FALLBACK` = **False**（M1 已彻底从二进制消失，源代码侧同样 0 命中）；
- `COUPLE-DEPOT-DIRECT` = **True**（M2 车库型直去车库未受影响）；
- `_r3r_couple_fb_news` 全库 0 命中（死表已删）。

⇒ M1 删除已生效且可被二进制层面证伪（旧 exe 该串为 True，新 exe 为 False）。
