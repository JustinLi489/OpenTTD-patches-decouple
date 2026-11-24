# 第 499 轮：通用路径改动全量回退（2026-11-24）

## 一、玩家指令（原话）

> 「我希望，将所有影响通用路径的更改全部回退」

基线口径沿用第 498 轮：`3ce0c55699`（2026-11-19 18:35:09 +0800，「R3R: sync working tree (rounds 431-466)」）。上游 0.73.5 合并 `fafab88eb1` 的第一父即该基线，故 R3R 自有改动 = `fafab88eb1..工作树`。

## 二、判定为「影响通用路径」的改动（回退目标）

依据第 498 轮 KI-686 与备忘 §十五：

| # | 位置 | 现状 | 回退目标 |
| --- | --- | --- | --- |
| 1 | `src/pbs.cpp` `IsWaitingPositionFree()` | `if (TrackOverlapsTracks(reserved, track) && !R3RIsCouplePartnerWaitingTile(v, tile)) return false;` | 原生 `if (TrackOverlapsTracks(reserved, track)) return false;` |
| 2 | `src/pbs.cpp` `IsSafeWaitingPosition()` | 函数最前面 `if (R3RIsCouplePartnerWaitingTile(v, tile)) return true;` | 删除该条（连同只读复刻 `R3RSafeWaitingWhy()` 的同构条） |
| 3 (A) | `src/pathfinder/yapf/yapf_rail.cpp` `TryReservePath()` | 去 `unsafe_pos` 形参；`IsWaitingPositionFree` 无条件；任一格失败即整条回滚 | 恢复基线签名与逐格语义（`unsafe_pos` 形参回归、`!unsafe_pos` 守卫回归） |
| 4 (C) | `src/pathfinder/yapf/yapf_rail.cpp` `ReserveSingleTrack()` | 站台分支 `r3r_res_stop = true; return false;` | 原生 `return tile != this->res_dest_tile || td != this->res_dest_td;` |
| 5 | `src/train_cmd.cpp` `TryPathReserveWithResultFlags()` | 最前面 `if (R3RIsParkedWaiting(...)) return TPRRF_NONE;` 短路订路 | 删除该门禁（停稳等待者恢复走原生订路） |

**D（`UnreserveSingleTrack()` 跳过配对者格）不在回退范围**：它只在释放时跳过，且判据 `R3RIsCouplePartnerWaitingTile` 仅对 `OT_GOTO_COUPLE` 生效，不改变通用列车的订路判据；并且它是 1/2 的对称配套——1/2 回退后，此处若保留也不会被非挂接列车触发（`v` 必须是 GOTO_COUPLE 机车）。稳态下回退 1/2 后 D 对通用路径零影响。

## 三、待玩家拍板的边界项

**B（答案本 `R3RAnswerFreeReservation`）**：第 498 轮归类为「只影响挂接/配对」，但复查代码时注意到 `record_answer` 默认值为 `true`（`yapf_rail.cpp:988`），即**所有非挂接列车也在记录答案**，答案失效时对正在跑的车调用 `FreeTrainTrackReservation()`。这本身就落在通用路径上。

- 若按字面「全部回退」⇒ 把 `record_answer` 默认值改为 `false`，只在挂接路径显式传 `true`。
- 本轮先按此方案执行（收窄作用域 = 对通用列车零影响），挂接修复保留；若玩家要求连挂接侧答案本一并删除，再另行处理。

## 四、回退后预期代价（须在 KI 中登记）

1. 第 491 轮 KI-676 修法 H2（同一到发线判据）依赖 2 的豁免链：回退 2 后，配对者占着的格不再被当作「安全等待位」，`CPL-SAFE-FAIL` 类失败可能回归 ⇒ 这是玩家明确接受的方向（宁可失败也不改通用判据）。
2. 回退 1/2 后，`RESV-EXEMPT-PASS` / `RESV-PAIR-AIR` 两条探针的命中会归零，属预期。
3. 回退 4 (C) 后，普通列车进站不再因 `r3r_res_stop` 提前收手，`return tile != res_dest_tile || td != res_dest_td` 原生语义恢复。
4. 回退 5 后，停稳等待者重新预订出站路线 ⇒ KI-208「幽灵预留先发出再销毁」现象必然回归（该修复的判据本身就是「不许订」）。

## 五、执行计划

1. 取证：读取上述 5 处当前实现 + 基线 `3ce0c55699` 对应实现（逐函数比对）。
2. 改码：仅动 `src/pbs.cpp`、`src/pathfinder/yapf/yapf_rail.cpp`、`src/train_cmd.cpp`，不碰 `src/*.h`（若 `pbs.h` 的外链声明因 1/2 回退失去调用者，则保留声明不删，避免头文件改动触发全量）。
3. 编译：`R3R_fullbuild.cmd inc detach`；核对 done/log/obj/exe 四件套。
4. 回填 KI 与 §十五。

## 六、状态

- 阶段：5 处回退全部改完，`read_lints` 0 条，增量构建已通过（`EXIT_CODE=0`，exe 2026-11-24 14:15）。
- 本轮零新增功能，纯回退。

## 七、执行结果（代码级，逐处）

**B 的最终口径（与 §三 的初稿相反，以本行为准）**：不采用「`record_answer` 默认 `false`、挂接显式传 `true`」的折中，而是**把整条 `record_answer` 机制还原成基线的 `unsafe_pos` 形参**：

- 签名 `bool TryReservePath(PBSTileInfo *target, TileIndex origin, bool unsafe_pos = false)`；
- 前置守卫 `if (!unsafe_pos && !IsWaitingPositionFree(...)) return false;`；
- 失败整批回滚 `if (!unsafe_pos && this->res_fail_tile != INVALID_TILE) { ... }`；
- 答案本 `if (r3r_tok && !unsafe_pos) _r3r_answer_build.push_back(...)`；
- lambda 返回值 `return r3r_tok;`（`r3r_stop` 中介与 `r3r_at_dest` 对它的依赖一并删除）。

⇒ 基线口径下**挂接路径（`unsafe_pos=true`）既不检查等待位空闲、失败也不整批回滚、且不记录答案**。这是真正的「逐字回退」，代价比 §三 折中方案更大：挂接寻路失去答案本兜底，KI-676 H2 的连锁更彻底，见 §四.1。

| 项 | 文件 | 落点 | 结果 |
| --- | --- | --- | --- |
| 1 | `src/pbs.cpp` | `IsWaitingPositionFree()` | 恢复单条件 `if (TrackOverlapsTracks(reserved, track)) return false;`，删除配对者豁免与配套注释 |
| 2 | `src/pbs.cpp` | `IsSafeWaitingPosition()` + `R3RSafeWaitingWhy()` | 两处同构的 `coupler` 分支全部删除，二者恢复逐字同构 |
| 3 (A) | `src/pathfinder/yapf/yapf_rail.cpp` | `TryReservePath()` | 见上方 B 口径：形参/守卫/回滚/答案本/返回值全部回基线；两个挂接调用点恢复显式 `true`（站台 `:1787`、depot `:2306`），通用调用点 `:2406`/`:2586` 不传 |
| 4 (C) | `src/pathfinder/yapf/yapf_rail.cpp` | `ReserveSingleTrack()` | 删除 A-1「站台铺满一律收手」整块（含 `RESV-PLAT-REACH` / `RESV-PLAT-EXIT-STOP` 探针与 `r3r_res_stop`），函数尾恢复 `return tile != this->res_dest_tile || td != this->res_dest_td;`；成员 `r3r_res_stop` 声明一并删除（已无设置/读取点） |
| 5 | `src/train_cmd.cpp` | `TryPathReserveWithResultFlags()` | 删除 `R3RIsParkedWaiting()` 短路门禁整块（约 50 行），原位置改为一段回退说明注释；被删门禁里的 `RESV-NOBOOK` 探针随代码消失 |

**刻意保留（不属回退范围）**：

- `src/pbs.h:104` 的 `R3RIsCouplePartnerWaitingTile()` 外链声明 —— 不删，避免改头文件触发全量重编（依赖表已失效）。
- `src/pbs.cpp` 的 `R3RIsCouplePartnerWaitingTile()` 定义与 `R3RReservationHeldByCouplePartner()` —— 仍被 `CYapfReserveTrack::UnreserveSingleTrack()`（D 项）与 `TryReserveRailTrack()` 的挂接放行使用；注释已订正为「不再是通用判据的豁免，只剩释放侧/挂接侧调用者」。
- `src/train_cmd.cpp` 的 `R3RIsParkedWaiting()` 定义、`_r3r_waiter_nobook_logged` 集合（现只剩 `erase` 调用点）、以及 `TryPathReserveWithResultFlags()` 里紧随其后的 `R3RShouldActAsWaitingForCouple` / `R3RShouldHoldForCouple` 两个门 —— 前者按玩家口径「不删定义、只摘调用点」保留以便只读复现；后两者只对 `OT_GOTO_COUPLE` 生效、不属通用路径，第 498 轮亦未列入 5 项。
- `RESV-PLAT-OVERFLOW` 只读探针（`r3r_plat_exit_tile` 仍有赋值点与读取点）保留。

**探针影响**：`RESV-EXEMPT-PASS` / `RESV-PAIR-AIR` 的命中将归零（§四.2）；`RESV-NOBOOK` 不再产生；`RESV-PLAT-REACH` / `RESV-PLAT-EXIT-STOP` 不再产生；`CPLRES` 的 `ans=` 字段改回基线名 `unsafe=`（行尾 `nAtDest=` 探针字段保留）。

## 八、构建自证

复用既有入口 `R3R_fullbuild.cmd inc detach`（未新建任何 .cmd）：

| 判据 | 实测 |
| --- | --- |
| 护栏 | `build\R3R_incbuild.guard.log` = `GUARD: incremental is safe (no header/lang file is newer than the newest object)` |
| 链接 | `build\R3R_incbuild.log` 尾部 `[5/5] Linking CXX executable openttd.exe` |
| 退出码 | `build\R3R_incbuild.done` = `EXIT_CODE=0`（2026-11-24 14:16） |
| 错误计数 | 日志内 error 0 条 |
| obj 晚于 .cpp | `yapf_rail.cpp` 14:09 → obj 14:12；`pbs.cpp` 14:10 → obj 14:12；`train_cmd.cpp` 14:10 → obj 14:13 |
| exe 晚于 obj | `build\openttd.exe` 2026-11-24 14:15（54 492 672 B） |
| lint | `read_lints` 三文件 0 条 |
| exe 字面量 | `RESV-PLAT-REACH`、`RESV-PLAT-EXIT-STOP` 已从 exe 消失；`RESV-PAIR-AIR`（保留项 D）仍在 |

**注意**：`RESV-NOBOOK` 仍能在 exe 中命中——保留的 `R3RIsParkedWaiting()` 定义、`RESV-NOBOOK-LOOKAHEAD` 与紧随其后的两个挂接门都会打出该前缀，故它**不能**作为第 ⑧ 项回退的自证，只能以源码 diff 为准。

## 九、结论与后续

1. 玩家口径「所有影响通用路径的改动全部回退」已 100% 执行：`IsWaitingPositionFree()`、`IsSafeWaitingPosition()`（含 `R3RSafeWaitingWhy()`）、`TryReservePath()`、`ReserveSingleTrack()`、`TryPathReserveWithResultFlags()` 五处通用入口逐字回到基线 `3ce0c55699`。
2. 挂接侧保留项（外部链接声明、`UnreserveSingleTrack()` 跳过配对者格）对普通列车零影响。
3. 必然回归项已登记 KI-687：**KI-208 幽灵预留**、**挂接路径失去答案本兜底**、**`RESV-EXEMPT-PASS`/`RESV-PAIR-AIR` 命中变窄**。这三项属回退代价，不再作为新 bug 追查。
4. 未做：游戏内实机复测（需玩家跑场景）；`build-release` 发布树未重编（按口径只在明确要求时编）。
