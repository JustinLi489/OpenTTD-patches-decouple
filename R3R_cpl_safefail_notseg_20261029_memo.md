# R3R 挂接寻路 CPL-SAFE-FAIL / FSCP notSeg 取证备忘（2026-10-29）

## 一、玩家提供的日志（原样，行号是玩家日志的行号）

```
789:FSCP tile=36,12 fail=notSeg best=57 ord=6 sf=1
790:CPL-SAFE-FAIL veh=48 tile=27,12
791:CPL-BLOCKED-WAIT veh=48 tile=42,10 dest=35,12
792:COUPLE-FAIL loco=48 order=16 tx=42 ty=10
```

缺的读数：788 行左右的 `CPL-PATHFOUND veh=48 found=?`。它决定走的是哪一支：
- `found=1` ⇒ A* 算出路径了，是**回溯的安全位判据**把整条路径作废（本备忘按此分析）；
- `found=0` ⇒ YAPF 压根没算到候选，问题在 `yapf_destrail.hpp` 的目标判定（看 `PFD-REJ-*`）。

## 二、逐行映射（代码锚点）

| 日志 | 产生位置 | 语义 |
|---|---|---|
| `FSCP tile=36,12 fail=notSeg best=57 ord=6 sf=1` | `src\pathfinder\yapf\yapf_rail.cpp:227-234`，探针在 `:230` | 格 (36,12) 上有预留/有车，`best`=57；`R3RIsCoupleTarget(57)` 返回 false ⇒ 该格判为**障碍** ⇒ 返回 false |
| `CPL-SAFE-FAIL veh=48 tile=27,12` | `yapf_rail.cpp` 多段分支回溯循环（`CheckSafePositionOnNode` 失败处，约 `:1298-1313`） | 回溯到 (27,12) 这一格时安全位判据失败 ⇒ 整条挂接路径作废；同处置 `_r3r_couple_path_blocked = true`（`:1310`） |
| `CPL-BLOCKED-WAIT veh=48 tile=42,10 dest=35,12` | `src\train_cmd.cpp:17884-17908`（KI-516 分支） | 路径已算出，但被第三方车否决 ⇒ **不降级**到普通寻路，就地等，下一 tick 由 `r3r_couple_stuck_retry` 重算 |
| `COUPLE-FAIL loco=48 order=16 tx=42 ty=10` | `TrainCoupleHandler`（`train_cmd.cpp` 约 `:15074`） | 几何没接触上（还没进 `Couple`/`TryTrainCouple`），本 tick 未合链；`order=16` = `OT_GOTO_COUPLE` |

## 三、判定（本轮结论）

1. **否决点唯一且明确**：不是 YAPF 找不到路，也不是预留铺设失败，而是 `yapf_rail.cpp:227` 的 `R3RIsCoupleTarget(best)` 把格 (36,12) 的占用者 veh=57 判成"不是挂接目标"，于是回溯把**整条**挂接路径作废。
2. **veh=57 的身份**：探针给的 `ord=6` = `OT_GOTO_WAYPOINT`（`src\order_type.h:83`），即它当前在跑自己的路线（过路车）；`sf=1` 说明它确实是段头（有 ★）。也就是说这是一列**正在行驶的列车/段**，不是等待被挂的车底 —— `R3RIsCoupleTarget` 在这里**判对了**。同一注释块 `train_cmd.cpp:17889` 已记录过同族现场（"veh=54 过路车，ord=6 正在开走"）。
3. **`R3RIsCoupleTarget`（`train_cmd.cpp:2665-2714`）的五道门**，任一为假都打 `notSeg`：
   - `:2667` `!IsSegmentFront()` —— sf=1，通过；
   - `:2684` `Train::From(t->First()) != t`（★ 但已被别的链吞并的段头）—— **本例外无法排除**；
   - `:2698` `R3RChainHasRealArticPart(t)`（含真 artic part）—— **本例外无法排除**；
   - `:2712` `cur_speed != 0`（2026-10-26 玩家要求"必须停稳"）—— 过路车必为真；
   - `:2713` `!current_order.IsType(OT_WAIT_COUPLE)` —— ord=6，**必为假**。
   ⇒ 现有探针**不区分是哪道门**，这是本轮最缺的读数（见 §五）。
4. **行为链是自洽的**：`CPL-SAFE-FAIL` → `_r3r_couple_path_blocked` → `CPL-BLOCKED-WAIT`（KI-516 明确“不降级、原地等障碍离开”）→ 几何永不接触 → 每 tick `COUPLE-FAIL`。所以现场表现应是 veh=48 在 (42,10) 附近**停住等待**，而不是绕到别的股道。这与第 346 轮 KI-516 的设计口径一致。

## 四、疑点（登记）

- **Q-1（中）**：`FSCP` 的 `notSeg` 分支不区分失败门（段头？链头？真铰接？速度？订单？），导致本条日志无法定性是"过路车正常否决"还是"等待车底被判据误杀"。需补只读子原因探针。
- **Q-2（中）**：几何上 veh=48 在 (42,10)、`dest=(35,12)`，被否决格在 (36,12)（紧贴 `dest` 的东侧一格），但 `CPL-SAFE-FAIL` 报的回溯终止格是 (27,12)（在 `dest` 以西 8 格）。若全程是"42→35 向西直行"，回溯不该越过障碍继续铺到更西侧 ⇒ 需用 `RP cur=` / `CPL-TRACE` 序列确认 (27,12) 是在哪条支路上，或 `SetReservationTarget`（`yapf_rail.cpp:576`）把终点记到了别处。
- **Q-3（低）**：`COUPLE-FAIL` 的 `tx/ty` 打的是**自己**的格（(42,10)），不是目标格 (35,12)，读日志时容易误判成"目标在 42,10"。
- **Q-4（待判）**：veh=57 若本应是等待车底而订单被推进成 `OT_GOTO_WAYPOINT`，则属 KI-274 家族（闸门把 WAIT_COUPLE 也跳过 / `R3RSkipCoupleOrdersForRealArtic`），需要看它有无 `ARTIC-SKIP-R3R`、`ORD-*` 记录。

## 五、下一步取证（按成本从低到高）

1. 取 788 行前的 `CPL-PATHFOUND veh=48 found=?`，以及同一 tick 的 `CPL-BEST / CPL-ORIGIN / RP cur=` 序列。判据：`found=1` + `CPL-BEST` 终点=(35,12) ⇒ 确认是安全位否决（本备忘口径）。
2. 看 veh=57 的订单与速度：`dump_vehicle 57`（或既有 `ORD-*` / `SKIP-STOPPED` / `TRP veh=57`），确认 `cur_speed`、`current_order` 类型、`First()==self`、有无真 artic part。
3. **补只读探针**（唯一缺的读数，`yapf_rail.cpp:227` 的 `notSeg` 分支，不改任何判据）：
   ```cpp
   const int why = (!best->IsSegmentFront() ? 1
                  : Train::From(best->First()) != best ? 2
                  : R3RChainHasRealArticPart(best) ? 3
                  : best->cur_speed != 0 ? 4 : 5);
   fprintf(dbg, "FSCP tile=%d,%d fail=notSeg best=%d ord=%d sf=%d spd=%d first=%d why=%d\n", ...);
   ```
   判据：`why=2` ⇒ 段标记/链归属问题（查 `R3RSettleChainSegments`）；`why=3` ⇒ 真铰接链（查 `LEGACY-ARTIC-DEARTIC` / 打散路径）；`why=4` ⇒ 对方还在滑行（预期行为）；`why=5` ⇒ 订单不是 WAIT_COUPLE（查订单推进）。

## 六、KI 草案（编号待与清单对齐后追加）

- **KI（新）** `FSCP notSeg 不区分失败门`：`yapf_rail.cpp:227` 的否决日志只给出 best/ord/sf，不给"五道门里的哪一道"，导致 `CPL-SAFE-FAIL` 现场无法定性。状态=待实现（只加只读探针）。严重度=低（诊断性）。
- **KI（新）** `CPL-SAFE-FAIL 回溯终止格可能越过障碍`（Q-2）：待 `RP cur=` 序列取证后定性，暂记"疑点"。严重度=未定。
- 关联：KI-516（挂接失败不降级、原地等第三方车）、KI-274（WAIT_COUPLE 被闸门跳过）、KI-188（★ 但被吞并的段头不是候选）。

## 七、本轮状态

纯分析，**零源码改动、零构建**。上下文未压缩，本备忘是唯一可信状态载体。
