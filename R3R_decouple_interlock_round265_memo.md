# R3R 第 265 轮：解挂启动互锁改用「后车自己能铺出预留」为放行条件

## 一、玩家口径（2026-10-21，原话要点）

> 「我希望我们把重点转向抓预留问题。关于列车分离，也就是两个动车组分离，我希望后面的列车
> 启动条件不是前车离开站台，而是后车能够铺设预留到路径上的下一个安全停车位。」

⇒ 需求变更：把 KI-335 的几何判据（前车驶离 N 格）换成**能力判据**
（后车自己能否对这一 tick 的路线完成 PBS 预留、一直订到下一个安全停车位）。
几何判据不再作为放行条件，只保留为兜底（前车已走远这一事实仍然说明它不会再挡路）。

## 二、现状取证（只读）

| 事实 | 锚点 |
|---|---|
| 登记：解挂成功后按 (kept=前车, follower=后车) 记一条 seam，只在「后车在轨道车站格」且后车不是纯车底时登记 | `src\train_cmd.cpp:2312 R3RRecordDecoupleSeam()`，`:9220` 调用点 |
| 骨架常量：`R3R_DECOMP_CLEAR_TILES = 4`（切比雪夫距离）、`R3R_DECOMP_WAIT_LIMIT = 900` | `train_cmd.cpp:2300`、`:2302` |
| 判据本体：前车 `seam_car` 已不在站台格、或 `dist >= 4`、或 `seam_car` 已消失 ⇒ 清条目放行；超时 900 tick 强制放行 | `train_cmd.cpp:2386`、`:2394` |
| 自清理：后车是 `OT_WAIT_COUPLE` 或纯车底 ⇒ 直接撤条目；后车已离开站台格 ⇒ 撤条目 | `train_cmd.cpp:2356`、`:2361` |
| 消费点：`TrainLocoHandler()` 里 `r3r_try_decouple_arrival()` 之后、`WAIT_COUPLE` 停驻门之前；命中即 `cur_speed = subspeed = 0; return true;` | `train_cmd.cpp:19733-19737` |
| 互锁与存档：seam 存进 R3DS 稀疏块（kind=0） | `train_cmd.cpp:16222 Save_R3DS()`、`:16249 Load_R3DS()` |

**结论句（现状）**：后车在互锁期内**根本没有机会寻路**——互锁命中就 `return true`，`TrainController()`/
`TrainCheckIfLineEnds()`（唯一的路线预留点）在本 tick 不会执行。所以"能否铺出预留"这件事现在被
结构性地挡住了，必须把尝试预留的动作搬到互锁检查内部。

## 三、猜测根因/方案（待验证 → 已实现后改为结论）

**猜测**：把放行条件改成"尝试一次 `TryPathReserve(claim_tile=true, mark_stuck=false)`，
成功即放行" 是可行的；失败（前车仍压着要走的轨/前方无路）就保持停住，并**撤销**这次尝试可能留下的
部分预留，避免留下幽灵位（沿用 KI-346 的 `R3RReleaseReservationSafe` 纪律）。

需要确认的三点：
- U-1：`TryPathReserve()` 在 `cur_speed == 0`、`vehstatus` 为 Stopped 且刚解挂的状态下是否会
  因为"已有旧 path"而直接返回 true（假放行）。若是，需要在登记 seam 时先释放后车的旧路径与预留。
- U-2：`TryPathReserve(…, mark_stuck=false)` 失败时是否已经把 `path` 写坏（`path.clear()` 后由
  下一次重试重算，应无副作用），以及是否会留下部分预留（PBS 回滚由 `FollowTrainReservation` 内部处理）。
- U-3：每 tick 重试的开销。只有被登记的一对受影响，且最多 `R3R_DECOMP_WAIT_LIMIT` 个 tick；
  若实测有影响，退化为每 4 tick 重试一次。

## 四、落地要点（本轮）

1. 新增 `R3RDecoupleInterlockCanReserve(Train *consist)`：在互锁命中且仍应等待时调用一次
   `consist->TryPathReserve(true, false)`；成功打 `DECOUPLE-INTERLOCK release … reason=reserved`，
   失败打节流的 `DECOUPLE-INTERLOCK block … reason=no-route`（每 seam 一次，`reported` 已有的
   两态字段继续用）。
2. 消费点改成：命中互锁 ⇒ 先试预留；成功则 `_r3r_decomp_seams.erase()` 并**不 return**（本 tick 正常起步）；
   失败才清速度并 return。
3. 几何判据（`dist >= R3R_DECOMP_CLEAR_TILES` / 前车离开站台 / 前车消失）保留为放行条件之一：
   前车都走远了就没必要再试预留，直接放行。**但不再要求几何条件先满足**。
4. 文档同步：`R3R_KNOWN_ISSUES.md` 新增 KI-405，把 KI-335 标注"判据已按第 265 轮口径改写"。

## 五、待复测判据

1. 解挂后若后车能立刻订到路（正常场景，前车已在出站方向移动）⇒ 日志出现
   `DECOUPLE-INTERLOCK release … reason=reserved`，后车与前半**几乎同时**起步，且不存在"白等 4 格"。
2. 前车仍横在后车要走的轨道上 ⇒ 后车保持停住，出现 `DECOUPLE-INTERLOCK block … reason=no-route`，
   且 `RESERVECONSIST`/`RESV-AUDIT-STRAY` 不因此增加（不留幽灵位）。
3. 负判据：正常跟车的后车、同一站台的另一列车不出现任何 `DECOUPLE-INTERLOCK` 行。
4. 负判据：车库内解挂、野外解挂仍然不登记（无 `set` 行）。
5. 超时兜底仍是 900 tick，且超时后不再尝试预留。

## 六、未确认项

- U-4：`release reason=reserved` 之后本 tick 是否还需要再调一次 `TrainController`（正常流程会走到，
  但若 `cur_speed` 被前置逻辑清零则可能空跑一 tick）——实测判据 1 的"同时起步"即可分辨。
- U-5：后车旧 path 是否在解挂时已被 `DecoupleTrain()` 清掉（需读 `DecoupleTrain` 尾部）。

## 七、实现落地（2026-10-21，仅 src\train_cmd.cpp，未碰任何 .h）

### 7.1 U-1 的取证结论（关键，决定了不需要主动清 path）

`R3RTryPathReserveCore()` 里已有这么一段（约 `train_cmd.cpp:15679`）：

```
/* If we have a reserved path and the path ends at a safe tile, we are finished already. */
if (origin.okay && (moving_front->tile != origin.tile || first_tile_okay)) {
    ... return TPRRF_RESERVATION_OK;
}
```

⇒ 传 `first_tile_okay = false` 时，「终点就是脚下这一格」**不算完成**，必然继续走到
`ChooseTrainTrack()` 去往前订。所以用 `TryPathReserve(consist, false, false)` 就能得到
玩家要的语义（"能不能铺到**下一个**安全停车位"），无需在登记 seam 时主动释放后车的旧
path/预留（少一处破坏性动作，也就少一处幽灵预留风险源）。

- 失败回滚由 `ChooseTrainTrack()` 内部负责（`train_cmd.cpp:15745-15748` 释放 depot 预留
  后返回失败），`mark_as_stuck = false` 保证不给后车打 `Stuck`。
- 后车不是 `OT_WAIT_COUPLE`、也不是纯车底（`R3RDecoupleInterlockWait()` 开头已排除），
  因此不会撞上 KI-208 的 parked-waiter 门禁。

### 7.2 改动清单

1. `R3R_DECOMP_PROBE_INTERVAL = 4`（新常量，紧跟 `R3R_DECOMP_WAIT_LIMIT`）。
2. `R3RDecoupleInterlockWait()` 参数 `const Train *` → `Train *`（要真的写 path/预留）。
3. 函数尾部（clear / timeout 两道几何放行之后）插入：

```
const uint age = _tick_counter - seam.tick;
int r3r_res = -1;
if (age < R3R_DECOMP_PROBE_INTERVAL || (age % R3R_DECOMP_PROBE_INTERVAL) == 0) {
    r3r_res = (int)TryPathReserve(consist, false, false);
    if ((r3r_res & (int)TPRRF_RESERVATION_OK) != 0) {
        R3RDbgWrite("DECOUPLE-INTERLOCK release head=%d follower=%d seam_car=%d dist=%u age=%u reason=reserved\n", ...);
        _r3r_decomp_seams.erase(it);
        return false;   // 本 tick 直接起步
    }
}
```

4. `DECOUPLE-INTERLOCK wait` 行追加 `res=%d age=%u`（`res=-1` 表示本 tick 未到尝试点）。
5. 消费点与 `R3RRecordDecoupleSeam()` 的注释改写为第 265 轮口径。

### 7.3 判据优先级（现在的顺序）

1. 后车是 `OT_WAIT_COUPLE` / 纯车底 ⇒ 撤条目放行（不归互锁管）；
2. 后车已不在站台格 ⇒ 撤条目；
3. 前车链头已消失 ⇒ 撤条目；
4. **几何**：前车 `seam_car` 消失 / 离开站台 / `dist >= 4` ⇒ 撤条目（快速放行）；
5. **超时**：`age >= 900` ⇒ 撤条目（兜底）；
6. **能力（新）**：`TryPathReserve(consist, false, false)` 成功 ⇒ 撤条目、本 tick 起步；
7. 否则保持停住（`cur_speed = subspeed = 0`，`return true`）。

### 7.4 待验证（游戏内）

见上文 §五。额外关注：`release reason=reserved` 是否与前半起步**同 tick 或早 1~2 tick**；
若出现「后车先动、前半还压在站台咽喉」的情况，说明判据放得太宽，需要把"前车是否仍在
站台格"重新并入能力判据（即要求 `!IsRailStationTile(seam_car->tile) || reserved`）。

## 八、构建自证（2026-10-21）

复用既有 `_tmp_inc_build.cmd`（未新建任何 .cmd）：

| 项 | 值 |
|---|---|
| 护栏 | `GUARD: incremental is safe (no header/lang file is newer than the newest object)`（17:50:18）|
| ninja | `[2/3] Building CXX object …train_cmd.cpp.obj` → `[3/3] Linking CXX executable openttd.exe` |
| `build\R3R_incbuild.done` | `EXIT_CODE=0`（17:52:25）|
| 错误计数 | `error C*` / `fatal error` / `FAILED:` / `build stopped` = **0** |
| 时间戳 | `src\train_cmd.cpp` 17:50:04 → `train_cmd.cpp.obj` 17:50:52 → `build\openttd.exe` 17:52:10（52 036 096 B）|
| read_lints | 0 条 |
| exe 字面量 | `DECOUPLE-INTERLOCK release head=%d follower=%d seam_car=%d dist=%u age=%u reason=reserved` 命中 |

状态＝**已实现 + 已编译**，游戏内复测待做。


