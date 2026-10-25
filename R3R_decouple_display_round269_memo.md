# R3R 第 269 轮：KI-409 修法 B 失败（列车死等不走）取证与改判

对象：`D:\temp\codebuddy-dropped-files\c26a77d5-bcb3-47c5-950e-84e7da6b891b\R3R_debug.log`
（323 787 B / 4 587 行 / 19:57:56，产生者 = 第 268 轮修法 B 的 `build\openttd.exe`）
玩家原话：**「这回列车直接走不动了，又不铺预留」**

## 一、现场读数（结论句：修法 B 造成 100% 永久 hold，release 一条都没有）

`Select-String DECOUPLE-INTERLOCK` 分组计数：

| 类别 | 条数 |
| --- | --- |
| `set` | 1 |
| `wait` | 1 |
| `hold` | **64** |
| `release` | **0** |

锚点原文：

- `4137: DECOUPLE-INTERLOCK set head=21 follower=24 seam_car=47 tile=272,237`
- `4174: DECOUPLE-INTERLOCK wait head=21 follower=24 seam=272,237 seam_car=47 dist=0 res=0 age=0 left=0`
- `4278-4349: DECOUPLE-INTERLOCK hold head=21 follower=24 dist=32..36 age=16 left=1 steps=5 sig=1 siggreen=0 sigres=0 reason=signal-red`（64 行）

判定：

1. **`siggreen=0` 恒定**、`sigres=0` 恒定 ⇒ 两个「或」项永远为假 ⇒ 只能等
   `R3R_DECOMP_WAIT_LIMIT = 3600` tick 兜底 ⇒ 玩家看到的「走不动」。
2. `left=1` 且 `dist` 32→33→35→36 单调增长 ⇒ **前车（head=21）正在正常驶离**，
   不是前车卡死；问题在后车（follower=24）的放行判据。
3. `steps=5 sig=1` ⇒ `R3RSeamSignalState()` 确实在两车之间找到了信号机，
   判据走到了信号机分支，不是 `no-signal-between`。

## 二、修法 B 错在哪（结论句：`GetSignalStateByTrackdir` 不是"这面灯对我绿"的可靠读数）

1. `sig_td` 是 `R3RSeamSignalState()` 沿 seam 车行进方向扫出来的 trackdir，
   而 `GetSignalStateByTrackdir(tile, trackdir)` 在 PBS 语义下返回的是
   **该 trackdir 所属方向那一侧信号**的状态；PBS 信号机的 `SignalState` 字段
   反映的是"信号机后方是否有**任何**列车预订/占用"，不是"是否对我这台车放行"。
   只要前车（head=21）的 PBS 长预留还压在信号机后方的轨道上，
   这面灯对所有人都读 Red —— 而这条链恰恰**永远**是这种形态（前车刚解挂、
   正在驶离、预留很长）。
2. 更致命的组合：`sigres=0` 已经被第 268 轮 §七证过是**结构性恒假**
   （`train_cmd.cpp:15883` 的 `origin.okay` 短路，预留不会前伸到信号机）。
   ⇒ 两个「或」项同时恒假，修法 B 退化成纯死等。

## 三、连带缺陷：hold 日志刷屏（`age` 没有递增）

64 行 `hold` 全部是 `age=16`，且 `(age % 16) == 0` 恒成立 ⇒ 同一状态每 tick 打一行。
说明 hold 分支里 `age`（或 `seam.age`）**没有被 `++`**，节流形同虚设。
第 268 轮只改了打印内容、没核对 `age` 的推进，属实现疏漏，须一并修。

## 四、修法 C（本轮改判）：问「信号机后一格还归前车吗」

**结论句：放行条件改为「信号机后方那一格不再由前车（head 链）持有」——
不读灯色字段、不要求预留前伸，前车一让开必然成立，因此不可能死等。**

- 取下一格：`CFollowTrackRail ft; ft.Follow(sig_tile, sig_td);` → `ft.new_tile / ft.new_td`
- 判归属：`Train *owner = GetTrainForReservation(ft.new_tile, ft.new_td);`
  `owner == nullptr || owner->First() != seam.head` ⇒ 放行
- 语义：信号机后的轨道已经腾出来了 ⇔ 这面灯对它已放行 ⇔ 前车确实越过了信号机
- 兜底不变：`sig_state != 0`（无信号机时不 hold 的位置保持第 267 轮口径）

> 待验证项 REG-269-A：`GetTrainForReservation()` 对「无预留但被车占用」的格返回什么，
> 若它对停着的车也返回该车，则判据天然覆盖"前车还停在信号机后"的情形，更安全。

## 五、实现落地（与 §四 的三处偏离，均已写进代码注释）

**结论句：修法 C 已落地，判据不再有任何依赖"灯色/自有预留"的或项；且 §四 第 61 行的
「`sig_state == 0` 保持放行」被我改成 **hold**——那是"同时起步"老问题的原型，不能放。**

1. 新增 `static bool R3RSeamBeyondSignalHeldByLeader(consist, leader, sig_tile, sig_td, out_tile)`
   （`src\train_cmd.cpp`，紧跟 `R3RSeamSignalReserved` 之后）：
   - `CFollowTrackRail ft(consist, consist->GetIndirectCompatibleRailTypes()); ft.Follow(sig_tile, sig_td)`
     取"另一个 block 的第一格" `ft.new_tile`；跟丢/非法格一律返回 false（不阻塞）；
   - **先查车体占用**：`GetFirstVehicleOnTile(tile, VehicleType::Train)`，其 `->First()->index == leader->First()->index`
     ⇒ true。这一条同时回答了 **REG-269-A**（不依赖 `GetTrainForReservation()` 对"无预留但占车"的返回约定，
     实测哪种语义都不会误判）——所以 §四 的待验证项**已降级为不再影响正确性**（仍记 U-269-1：
     修正法 C 的"归属推断"完全绕开了它）；
   - 站台/路点：`HasStationReservation()` + `GetTrainForWholeTileReservation()`，owner 是前车链 ⇒ true；
   - 隧道/桥/道口（非 plain rail，没有可问的预留位）⇒ false（不阻塞，避免造出永久假等待）；
   - 普通轨道：`HasBit(GetRailReservationTrackBits(tile), TrackdirToTrack(td))` + `GetTrainForReservation()`，
     owner 是前车链 ⇒ true；
   - **别的列车持有那一格不算等待理由**（本互锁只管配对的前后两半），杜绝"替别人挡路"变成死等。
2. 放行判据（`R3RDecoupleInterlockWait` 内）：`cleared = (sig_state == 2) || (sig_state == 1 && !beyond_held)`。
   - `sig_state == 2`：沿后车行进方向跟丢（前车拐走/进库/超 32 格）⇒ `reason=leader-gone`；
   - `sig_state == 1` 且后一格不归前车 ⇒ `reason=signal-cleared`；
   - **`sig_state == 0` 改为 hold**（`reason=no-signal-between`）：两车之间还没有信号机、前车尾车就压在
     前面同一 block 里，放行正是第 266 轮报的"同时起步"。它会随前车驶远自然变 1 或 2，另有
     `R3R_DECOMP_WAIT_LIMIT = 3600` 兜底，不会死等。
3. `TryPathReserve()` 仍在原节奏上跑，但**不再参与判据**：它只负责把"能订到哪就订到哪"的黑色预留画出来。
   放行那一刻 block 已腾空，引擎本 tick 就能把预留铺过信号机 ⇒ 第 268 轮要的「黑线 + 绿灯」随之出现。
4. hold 节流重写（`R3RDecompSeam` 增 `hold_calls` / `last_log_age` / `last_sig`）：
   `hold_calls <= 1 || 读数签名变化 || age >= last_log_age + 128` 才打印；读数签名 =
   `sig_state | beyond_held<<2 | leader_left<<3`。彻底不再依赖 `age % N`（其中 `age` 实测会停在 16）。
5. 日志字段：`hold` / `release` 两行都把 `sigres=` 降级为**只读诊断**（保留 `R3RSeamSignalReserved` 的调用，
   便于下次复测一眼看出"预留是否仍铺不到信号机"），新增 `beyond=x,y beyondheld=`。

## 六、构建自证（第 269 轮）

- 复用既有 `_tmp_inc_build.cmd`（**未新建任何 `.cmd`**）；构建前 `tasklist` 确认 `openttd.exe` 未运行。
- `build\R3R_incbuild.guard.log` = `GUARD: incremental is safe`（只改 `src\train_cmd.cpp`，未碰 `src\*.h`/`lang`）。
- `build\R3R_incbuild.log`：`[2/3] Building CXX object ...train_cmd.cpp.obj` → `[3/3] Linking CXX executable openttd.exe`。
- `build\R3R_incbuild.done` = `EXIT_CODE=0`。
- 时间戳：`src\train_cmd.cpp` 20:13:01 → `train_cmd.cpp.obj` 20:14:52 → `build\openttd.exe` **2026-10-21 20:16:20（52 036 096 B）**。
- `read_lints(src\train_cmd.cpp)` = 0 条。
- exe 字面量核对命中：`signal-cleared` / `leader-gone` / `signal-blocked` / `no-signal-between` /
  `beyondheld=` / `DECOUPLE-INTERLOCK hold ... n=%u reason=%s`（旧 `siggreen=%d sigres=%d` 字段仍在，属诊断）。
- 发布树 `build-release\` 本轮**未编**（按 2026-10-19 口径，只在用户明确要发布版时才编）。

## 七、待复测判据（第 269 轮，5 条）

1. 同场景解挂后，后车**必须动起来**：日志出现 `DECOUPLE-INTERLOCK release ... reason=signal-cleared`
   （或 `leader-gone`），且之后能看到 `CRT veh=<后车> found=1`。
2. 放行瞬间玩家要能看到「黑色预留铺到并越过出站信号机」+ 信号机点绿（修法 C 不再依赖它，但它是第 268 轮口径）。
3. 负判据（不得回退 KI-335 本意）：`release` 行里 `beyondheld=1` 一行都不应出现；
   前车尾车仍压在信号机后一格期间必须 `hold reason=signal-blocked`。
4. 负判据（不得回退 KI-409 第 267 轮）：全程不得出现 `reason=no-signal-between` 之后的"立即起步"；
   即两车之间还没信号机时不得放行（除非命中 3600 tick timeout）。
5. 日志体积：`hold` 行数应与"读数变化次数 + age/128 心跳"同量级（同场景应 ≪ 第 268 轮的 64 行/场景），
   不得再出现整屏 `age=16` 的重复行。

> 疑点 U-269-2（待验证，不影响本轮改动）：第 268 轮现场 64 行 `hold` 的 `steps=` 读数未被记录成
> "前车尾车距信号机几格"，无法反推 `sig_state` 为何从 0 变 1 之后不再推进；本轮已补 `beyond=x,y`，
> 下次复测可用 `beyondheld` 直接判定是"车体占用"还是"预留持有"。

---

## 八、修法 C 的逃生阀（第 269 轮续，2026-10-21）

**结论句：修法 C 把 `sig_state == 0` 改成 hold 后，在"出站侧根本没有 PBS 信号机"的线路上会
恒为 0 ⇒ 后车死等 3600 tick（约 2 分钟），必须补一条只在"连一面信号机都找不到"时生效的兜底。**

### 8.1 缺口是怎么来的

- `sig_state` 来自 `R3RSeamSignalState()`：沿后车行进方向最多 `steps` 格找 PBS 信号机。
- 返回值语义：`2` = 32 格内跟丢（前车拐走/进库）；`1` = 找到信号机；`0` = **两车之间（32 格内）
  连一面信号机都没有**。
- 第 266 轮把 `0` 判成"同时起步的老问题" ⇒ 第 269 轮修法 C 把它改成 hold。
- 但在**没有信号机的线路**（普通闭塞/纯区间）上 `0` 是常态，永远转不成 1/2 ⇒ 只能等
  `R3R_DECOMP_WAIT_LIMIT = 3600` tick 超时（30 tick/s ≈ 2 分钟不动），正是玩家第 267 轮
  报的"列车直接走不动了"换个场景复发。

### 8.2 落地（仅 `src\train_cmd.cpp`，未碰 `src\*.h`）

```cpp
static const uint R3R_DECOMP_SIGNALLESS_TILES = 8;   // ≈一列车身长
...
const bool leader_far = (sig_state == 0) && (dist >= R3R_DECOMP_SIGNALLESS_TILES);
const bool cleared = (sig_state == 2) || (sig_state == 1 && !beyond_held) || leader_far;
```

- **只有 `sig_state == 0` 才看距离**：有信号机时判据逐字不变（纯 block 归属），绝不用距离代替。
  这是与第 266 轮被否掉的"驶离 N 格"口径的本质区别（那条是无条件距离阈值）。
- `sig_state == 1 && beyond_held == true`（前车尾车正压在信号机后一格）仍**严格 hold**。
- 放行日志新增第三个 reason 值：`reason=no-signal-leader-far`（另两个是 `leader-gone` /
  `signal-cleared`），复测时一眼可分辨是哪条分支放行的。
- 顺带把三处注释（函数头 @param、`leader_left` 说明、`dist` 说明）改成与现状一致，避免后来人
  按"dist 不参与判据"的旧注释误判。

### 8.3 为什么取 8

- 8 格 ≈ 一列车身长，肉眼可见"前车确实开走了"，不会在视觉上产生"同时起步"。
- 比 8 更小的值在长编组场景会把"前车刚滑出半个车身"当成已让开；比 8 更大只会更保守。
- 该常量只在无信号机线路生效，不影响任何有信号机场景。

### 8.4 构建自证（2026-10-21 20:30，内测树 `build\`）

- 复用既有 `_tmp_inc_build.cmd`（**未新建任何 `.cmd`**）；构建前 `tasklist` 确认 `openttd.exe` 未运行。
- `build\R3R_incbuild.log`：`[2/3] Building CXX object ...train_cmd.cpp.obj` → `[3/3] Linking CXX executable openttd.exe`。
- `build\R3R_incbuild.done` = `EXIT_CODE=0`；日志 error 计数 0。
- 时间戳：`src\train_cmd.cpp` 20:22:39 → `train_cmd.cpp.obj` 20:28:57 → `build\openttd.exe`
  **2026-10-21 20:30:18（52 036 096 B）**。
- `read_lints(src\train_cmd.cpp)` = 0 条。
- exe 字面量命中 `no-signal-leader-far` / `beyondheld=%d`；静态函数名
  `R3RSeamBeyondSignalHeldByLeader` 不保留在 exe 里（正常现象，不是漏编）。
- 发布树 `build-release\` 本轮**未编**（按 2026-10-19 口径）。

### 8.5 复测判据（修订 §七 第 4 条）

1. **有信号机的线路**：仍不得在 `sig_state == 0` 时放行，只允许 `hold reason=no-signal-between`。
2. **没有信号机的线路**：允许 `release reason=no-signal-leader-far`，且该行 `dist >= 8`；
   `hold` 阶段 `dist` 应由小到大递增，不得在 `dist < 8` 时放行。
3. 判据 1（后车动起来）、2（黑线+绿灯）、3（`beyondheld=1` 不得出现在 release 行）、
   5（`hold` 行数同量级）**逐字不变**。
4. 若同一场景里 `no-signal-leader-far` 出现而玩家反馈"又抢跑"，说明该线路上其实有信号机、
   只是 32 格内没扫到 ⇒ 应加大 `R3RSeamSignalState()` 的 `steps`，**不要**调小本常量。

---

## 九、玩家质疑：为什么解挂后不直接调 TryPathReserve，而要一堆判据阻止后车前进

**（2026-10-21，玩家原话：「等一下，为什么解挂之后后车不直接调用 try path reserve，而是要一堆复杂的过程来阻止它前进」）**

### 9.1 先纠正一个事实前提

**后车每个 tick 都在调 `TryPathReserve()`**，从来没有被禁用：

```2672:2674:src/train_cmd.cpp
	if (leader_left || age < R3R_DECOMP_PROBE_INTERVAL || (age % R3R_DECOMP_PROBE_INTERVAL) == 0) {
		r3r_res = (int)TryPathReserve(consist, false, false);
		sig_state = R3RSeamSignalState(consist, seam_car, &sig_steps, &sig_tile, &sig_td);
```

它负责"能订多远订多远"（画黑色预留）。被截停的是**运动**，不是预留：

```20062:20066:src/train_cmd.cpp
	if (R3RDecoupleInterlockWait(consist)) {
		consist->cur_speed = 0;
		consist->subspeed = 0;
		return true;
	}
```

### 9.2 为什么不能拿 TryPathReserve 的返回值当放行判据（两次实测的反面证据）

`TryPathReserve()` 回答的问题是"**我的预留铺到下一个安全停车位了吗**"，不是"**前方让开了吗**"。两者在解挂场景下不等价，两个方向都不等价：

**方向一：它会恒真 ⇒ 后车立刻起步（抢跑）。**
`R3RTryPathReserveCore()` 的早退：

```16003:16014:src/train_cmd.cpp
	/* If we have a reserved path and the path ends at a safe tile, we are finished already. */
	if (origin.okay && (moving_front->tile != origin.tile || first_tile_okay)) {
		...
		return TPRRF_RESERVATION_OK;
	}
```

后车静止在站台上、脚下那一格就是安全位 ⇒ `origin.okay` 为真 ⇒ 直接返回 `RESERVATION_OK`，**根本不看前车在哪**。PBS 允许跟车预留，前车的 block 是前车的，不构成"我订不通"。所以这条判据恒真 = 没有判据，就是第 265 轮之前玩家报的"两列动车组几乎同时启动"。

**方向二：强制它往前伸（`first_tile_okay=false`，第 265 轮做法）则会恒假 ⇒ 死锁。**
`first_tile_okay=false` 让上面那个 if 在 `moving_front->tile == origin.tile` 时不早退，继续走 `ChooseTrainTrack()` 去延伸。但 PBS 的预留终点**结构上**就停在"下一个安全停车位"，而出站信号机前一格就是安全位；前车还压着下一个 block 时 `ChooseTrainTrack()` 必返 none。第 268 轮现场 64 条 `hold` + `release` 0 条就是它恒失败，玩家原话"列车直接走不动了"。

### 9.3 所以现在这套判据到底在问同一件事

三条分支其实是同一个问题的三种线路形态：

> **前车尾车（`seam_car`）是否还占着"我下一步要进的那个 block"？**

| 分支 | 形态 | 问法 |
| --- | --- | --- |
| `sig_state == 2` | 32 格内跟丢前车 | 前车显然走了 |
| `sig_state == 1` | 两车之间有信号机 | 信号机**后一格**还归前车吗（`beyond_held`） |
| `sig_state == 0` | 32 格内没有信号机 | 没有 block 边界可问 ⇒ 退回几何：前车驶离 ≥ 8 格 |

复杂感来自"引擎不提供这个信息"：PBS 没有 API 回答"我下一步要进的 block 现在归谁"，只能靠"信号机后一格归属"（`R3RSeamBeyondSignalHeldByLeader`）去逼近；没有信号机时连逼近都做不到，只能用距离。

### 9.4 可以简化，但要先定口径

如果玩家的期望是"**解挂后两半各走各的、根本不该互相等**"，那正确做法不是简化判据，而是**去掉互锁**（`R3RRecordDecoupleSeam` 不登记），代价是恢复 KI-335 的原始现象（同时起步/同 block 内零预留跟随）。反过来若要保留"前车先走"，就绕不开 9.3 那个问题。**这两种口径必须由玩家拍板，不能由实现者折中。**

### 8.6 实机反馈（2026-10-21 20:3x，第 269 轮续的逃生阀版）：**行为仍不符预期**

玩家原话：「行为还是不符合我的预期，请你讲一下你做了什么吧」。

**待补现场（缺这三项无法改判）**：

1. 期望行为 vs 实际行为各是什么（后车"根本没动"？"动了但没铺黑线"？"起步太早/太晚"？）。
2. 新日志 `build\R3R_debug.log`（用 20:30:18 那版 exe 产生）。
3. `DECOUPLE-INTERLOCK` 全部行（`set` / `wait` / `hold` / `release` / `timeout`）按次数计数。

**按现版本代码，可能落到的三类情形与各自的日志签名**（拿到日志即可直接对号）：

| 情形 | 日志签名 | 说明 |
| --- | --- | --- |
| 后车一直不动，再 3600 tick 后走 | `hold reason=signal-blocked beyondheld=1` 或 `reason=no-signal-between` 持续到 `timeout` | 前车尾车一直压着信号机后一格，或 `sig_state` 恒 0 而 `dist < 8` |
| 放行了但没黑线 | `release reason=signal-cleared` / `leader-gone` 后 CRT 才有预留 | 预留由 `TryPathReserve` 负责，放行瞬间才订 ⇒ 有一帧无预留 |
| 起步太早（又抢跑） | `release reason=no-signal-leader-far dist=8` | 逃生阀在线路其实有信号机（只是 32 格内没扫到）时误开 |

**本轮只改了「何时允许起步」这一个判据，未改**：解挂本身、前车行为、`TryPathReserve` 的预留铺设策略、`R3RSeamSignalState()` 的扫描步数。若玩家期望的是"后车应立刻画出黑线"或"前车压信号机时也应当能走"，则属于本判据范围之外的新需求，需另立条目。

---

## 十、新方案可行性论证（2026-10-21 玩家提出）

**玩家原话**：「解挂后后车先尝试用预留占用整个站台，占用成功之后再允许启动，向前进方向发出预留。」

### 10.1 核心机制：站台整格预留是「先到先得」的独占单 bit

站台/路点的预留**没有 owner 字段、不区分 track，整格共用 1 个 bit**：

```583:601:src/station_map.h
inline bool HasStationReservation(TileIndex t)
{
	dbg_assert_tile(HasStationRail(t), t);
	return HasBit(_me[t].m6, 2);
}
inline void SetRailStationReservation(TileIndex t, bool b)
```

而订位入口是纯"抢"语义——**只有没人占才成功**：

```998:1008:src/pbs.cpp
		case TileType::Station:
			if (HasStationRail(tile) && !HasStationReservation(tile)) {
				SetRailStationReservation(tile, true);
				...
				return true;
			}
			break;
```

⇒ **「把站台每一格都订一遍，全部成功」在语义上严格等价于「站台上没有任何别的车占着任何一格」**。这个判据不需要问 owner、不需要找信号机、不需要 8 格距离，也就是 §9.3 里唯一做不到的那件事，这里直接做到了。

### 10.2 但「占用」不是一把锁（三条必须承认的事实）

1. **挡不住已经停着的车。** 引擎判"站台占用"只看物理站位：`CheckTrainsOnTrack()` 遍历 `VehiclesOnTile` 比 `t->tile == tile`；`FollowTrainReservation()` 的站台回扫（`pbs.cpp:1812-1822`）也是对着 `CheckTrainsOnTrack` 逐格找车。⇒ 占位只让**想订位的车**订不到，对停在上面不动的车零约束。
2. **整格 bit 无 owner，事后无法自证"这是我的"。** 我们额外占的空站台格没有对应的 rail reservation 路径，`GetTrainForWholeTileReservation()` 回溯会返回 nullptr ⇒ 被 `R3RReservationAudit()` 判为 STRAY，再经 `R3R_RESV_REAP_AUDITS=3` 次由 `RESV-REAP` 清掉（KI-345/346 家族）。⇒ **必须把占位记进 `R3RResvLedger`**（已有 `R3RTrainForLedgerBit()`，pbs.cpp:924-930 的 booker 校验就是它的消费者），否则判据会抖动（成功→被 reap→失败）。
3. **释放责任在我们自己。** 引擎只释放"列车自己路径上"的站台格（`SetRailStationPlatformReservation`，且被 `IsRailStationTile` 守卫，路点还被排除）。我们多占的格必须显式释放，否则就是 KI-203 的孤儿位（玩家见过的"只在路点那一格"的幽灵预留）。

### 10.3 三个必须先拍板的口径

| 编号 | 问题 | 选项 |
| --- | --- | --- |
| **C-1** | 占「整站」还是「本到发线」？ | 整站＝同 `station_id` 所有站台格；前车在别的到发线上也必须等（严格符合 KI-335「前车必须先完全离开站台」），代价是一列车独占整个车站。本到发线＝只用 `IsCompatibleTrainStationTile` 沿轴枚举，另一条到发线上的前车不阻塞。 |
| **C-2** | 自己脚下那格怎么算？ | (a) 跳过自己链节所在格（`GetFirstVehicleOnTile(tile)` 是自己就跳过）；(b) 「已置位且回溯判定归自己」也算成功。后者依赖 §10.2-2 的账本。 |
| **C-3** | 占用失败时的行为？ | 每 N tick 重试（推荐 4）；是否保留 3600 tick 逃生阀 + 之后强制释放。**必须有超时**，否则后车被卖掉/删订单时孤儿位会长期挡住整个站。 |

### 10.4 与现方案（§9.3 信号机判据）的对比

| | 现方案 | 新方案 |
| --- | --- | --- |
| 依赖 | 两车之间必须有信号机 | 只要脚下是站台 |
| 无信号机线路 | 退回"前车离开 8 格"（玩家报过抢跑/死等） | 无需退化 |
| 语义 | "越过信号机后一格归谁"（间接） | "站台是否已归我独占"（直接） |
| 多到发线车站 | 判据与站台无关 | C-1 决定严格程度 |

⇒ 新方案在**判据强度与可解释性**上优于现方案，是可行方向。

### 10.5 已知风险 / 待验证

- **R-1** 长站台每 tick 全格尝试：按 tick 节流到 4 可接受；枚举用 `GetStationPlatforms()` 或沿轴 `IsCompatibleTrainStationTile`（同 KI-93 那次轴向盲扫的教训：**不要**做"沿轴 ±32 格坐标加减"，必须逐格校验格类型）。
- **R-2** 「占用成功」只保证站台空，**不保证前方 block 通**。玩家口径要求"占用成功之后再向前发预留"，若 `TryPathReserve` 仍订不出去（前车就停在站台外同一 block 的下一格），后车会"已放行但零预留、原地不动"。这是引擎行为，不是判据失效。
- **R-3** 占位期间会挡住**其它车**订这个站（`TryReserveRailTrack` 返回 false）。若后车长时间不走，该站会被长期占死 ⇒ 与 C-3 的超时释放绑定。
- **U-1** 后车静止在站台时，自己脚下那格是否恒被 `ReserveTrackUnderConsist()` 订着（决定 C-2 是否可以用"跳过自己的格"）。
- **U-2** 前车驶离站台格时引擎释放该格 bit 的时机（离开该格即刻，还是整个 block 出清）——决定判据的"紧不紧"。
- **U-3** 多站台车站里「同 station_id 不同平台」的格枚举方式（`GetStationPlatforms` vs `IsCompatibleTrainStationTile` 沿轴），以及路点是否也纳入（路点不是 `IsRailStationTile`，现有释放链排除它）。

---

## 十一、口径拍板（2026-10-21，玩家逐条作答）

**玩家原话（存档原文，不得改写）**：

> C1，占用本到发线。C2，后车自己脚下那格，这应该就是属于后车自己的啊，难道前车离开的时候不会把到发线上属于自己的预留全部销毁吗。C3，占用失败，每4tick重试，但不保留超时强制放行同时释放占位，因为列车在站台上不可能被销毁，除非撞毁，我们只需要考虑撞毁

### 11.1 三条口径定稿

| 编号 | 玩家选择 | 实现含义 |
| --- | --- | --- |
| **C-1** | **占用本到发线**（不是整站） | 只用 `IsCompatibleTrainStationTile` 沿站台轴枚举"与自己脚下同一 platform 的格"；同 `station_id` 的其它到发线**不占、不检查**。前车若停在别的到发线 ⇒ 不阻塞后车 ⇒ 两车可同时起步（这是本口径的**已知且被接受**的后果）。 |
| **C-2** | **后车脚下那格属于后车自己**（选项 (a)：跳过自己的格） | 枚举时对"格上停着本链车节"的格直接计为成功、不尝试订位。理由＝该格被 `ReserveTrackUnderConsist()` 恒订着，`TryReserveRailTrack()` 必然返回 false，若不特判则判据恒失败。 |
| **C-3** | **每 4 tick 重试；不设超时强制放行，也不因此释放占位** | 删除原方案的"3600 tick 逃生阀"；占位持久持有直到判据成立。玩家的理由＝"列车在站台上不可能被销毁，除非撞毁" ⇒ 只需考虑撞毁这一条异常出口。 |

### 11.2 C-2 那个反问必须先回答（玩家的前提是对的，但只对一半）

玩家问：「后车自己脚下那格应该属于后车自己啊，难道前车离开的时候不会把到发线上属于自己的预留全部销毁吗」。

答：**会销毁，但"属于自己"这个粒度在站台上表达不出来** —— 这是本方案唯一的真陷阱，必须在实现时避开：

1. 站台格的预留是**整格 1 bit**（`station_map.h:595-601`，无 owner、不区分 track），同一到发线上的两列车**共用同一个 bit**。所以"前车的那格"和"后车的那格"在存储上不是两个对象，而是两个各自独立的 bit（每格一个），不存在"到发线一个总 bit"。
2. 引擎的站台释放走 `SetRailStationPlatformReservation()`，其调用点全部以 `IsRailStationTile()` 为守卫，**释放粒度是"沿平台轴把整条平台一起清"**（不是只清自己站着的那一格）。⇒ **前车离开时会连带清掉后车自己脚下那格的 bit**。
3. 后果对判据**无害**（bit 被清 = 更"空" = 更容易订到 = 更早放行，而这一时刻恰好就是我们要放行的时刻）；但后果对**实现**有两处必须处理：
   - (a) 后车"占位"期间，前车若离开，会把后车占的位一起清掉 ⇒ 后车必须**每 4 tick 重订**（C-3 的重试正好覆盖这一点），不能假设"占过一次就一直有效"。
   - (b) 判据**不能用"上次占用成功"缓存**，必须每次重跑（好在 C-3 已定 4 tick 重试）。
4. 仍然待实证的一点：引擎释放是"清整条平台"还是"只清自己所在格"——若真是整条平台，则"后车脚下那格属于后车"在**物理储层**上并不成立（随时会被前车清掉），只在**判据语义**上成立（后车下一 tick 重订即恢复）。这条不影响实现方案，但影响日志判读，记为 U-4。

### 11.3 由此定稿的实现草案（待写码）

```
每 4 tick（R3R_DECOMP_INTERVAL 同源），对后车 consist：
  ok = true
  for (每一格 t in 本到发线，从脚下沿轴枚举，逐格 IsCompatibleTrainStationTile 校验):
      if (格上有本链车节) continue;                      // C-2
      if (GetFirstVehicleOnTile(t, Train) != nullptr) { ok = false; break; }   // 别人站着
      if (!HasStationReservation(t)) { SetRailStationReservation(t, true); 记账; continue; }
      if (GetTrainForWholeTileReservation(t) 归本链) continue;                 // 已归我
      ok = false; break;                                                     // 别人占着
  如果 ok ⇒ 放行（删除互锁登记）；否则 cur_speed=0 继续等
```

- 记账：占的格必须进 `R3RResvLedger`（`R3RTrainForLedgerBit()` 是现成消费者，见 `pbs.cpp:924-930`），否则被 `RESV-AUDIT` 判 STRAY 后由 `RESV-REAP` 清掉（KI-345/346 家族），判据抖动。
- 释放：判据成立放行时、以及解挂缝被移除时，必须把"自己占的、且不在自己路径上的"格显式释放（引擎不会替我们放）；撞毁路径（C-3 唯一承认的异常）要走同一条释放。
- 删除：`R3RSeamSignalState()`、`R3RSeamBeyondSignalHeldByLeader()`、8 格距离逃生阀、3600 tick 超时，全部从互锁里移除。

### 11.4 新增待验证

- **U-4**：`SetRailStationPlatformReservation()` 的释放粒度是"整条平台"还是"单格"？（决定 11.2-4 的日志判读，不影响实现。）
- **U-5**：`ReserveTrackUnderConsist()` 是否每 tick 重新订后车脚下那格（决定 C-2 跳过判据是否需要"格上无车且未订"的额外分支）。
- **U-6**：撞毁（`VehState::Crashed`）后本方案占的位由谁释放 —— 需确认 R3R 现有清扫链（`R3RReleaseChainReservations`）能否覆盖"车已不在站台上"的情形。

### 11.5 实证回填（同日取证，三处锚点）

**U-4 结论 = 整条平台一起清，不是单格。** 玩家那个反问的答案是"会销毁，而且销毁得比你预期的更多"：

```845:858:src/pbs.cpp
void SetRailStationPlatformReservation(TileIndex start, DiagDir dir, bool b)
{
	TileIndex     tile = start;
	TileIndexDiff diff = TileOffsByDiagDir(dir);

	assert_tile(IsRailStationTile(start), start);
	assert_tile(GetRailStationAxis(start) == DiagDirToAxis(dir), start);

	do {
		SetRailStationReservation(tile, b); // R3R (KI-207): traced inside the setter
		MarkTileDirtyByTile(tile, VMDF_NOT_MAP_MODE);
		tile = TileAdd(tile, diff);
	} while (IsCompatibleTrainStationTile(tile, start));
}
```

`do-while (IsCompatibleTrainStationTile)` ⇒ **沿站台轴把整条到发线一起写**。`pbs.cpp:140-142` 的账本注释也直说 "SetRailStationPlatformReservation() clearing a whole platform"。

⇒ 两点后果：
1. 「后车脚下那格属于后车」在**判据语义**上成立（别人不该占它），在**物理存储**上不成立（前车离开时把它一起清掉）。C-2 的"跳过自己格"仍然必须，但理由要改写：不是"它归我所以不用订"，而是"它此刻被
   `ReserveTrackUnderConsist()` 订着、再订必然 false，跳过它以免判据恒失败"。
2. **逆向的好消息**：后车自己开走时，引擎同样会把整条平台清掉 ⇒ **不需要我们写"释放占位"的逻辑**。11.3 草案里"自行释放"一条可以降级为"仅撞毁兜底"。

**U-6 部分结论 = 撞毁在判据里已被自动豁免。** `IsRailStationPlatformFree()` 第 880 行 `vehstatus.Test(VehState::Crashed)` ⇒ `continue`，撞毁车不算"占用" ⇒ 前车撞毁时后车**自动放行**，与 C-3「只需要考虑撞毁」的口径天然吻合。剩下的只是"撞毁车占的 bit 没人清"这一条卫生问题，需要走 R3R 现有清扫兜底。

**关键发现：引擎已有正好对口的只读函数，且当前零调用点。**

```860:888:src/pbs.cpp
/**
 * Check whether a rail station platform is free (only occupied by \a v itself).
 * R3R: used when clearing platform reservations — a platform holding a waiting
 * consist (WAIT_COUPLE) must NOT be unreserved, otherwise the consist loses its
 * reservation and the coupling locomotive pathfinds straight through it.
 */
bool IsRailStationPlatformFree(const Train *v, TileIndex start, DiagDirection dir)
{
	...
	do {
		for (const Vehicle *u : Vehicle::Iterate()) {
			if (u->type != VehicleType::Train || u->vehstatus.Test(VehState::Crashed)) continue;
			const Train *t = Train::From(u);
			if (t->First()->index == v->First()->index) continue;
			if (t->tile == tile) return false;
		}
		tile = TileAdd(tile, diff);
	} while (IsCompatibleTrainStationTile(tile, start));
	return true;
}
```

这个函数逐条命中三条拍板口径：

| 口径 | 函数里的对应实现 |
| --- | --- |
| C-1 本到发线 | `do-while (IsCompatibleTrainStationTile)` 沿站台轴枚举，天然不含同站其它到发线 |
| C-2 自己脚下那格归自己 | `if (t->First()->index == v->First()->index) continue;` 整条链都排除 |
| C-3 只考虑撞毁 | `vehstatus.Test(VehState::Crashed) ⇒ continue` |

**全 src 只有 `pbs.h:138` 的声明与 `pbs.cpp:870` 的定义，没有任何调用点**（检索 zero call sites）⇒ 一个已经写好、已经断言过前置条件、当前闲置的只读工具。

**两处实现陷阱（写码时必须避开）**：
1. **必须双向各调一次。** `IsRailStationPlatformFree(v, start, dir)` 只从 `start` 往 `dir` 一个方向走。后车若停在本到发线中间（解挂场景常态），单向调用只覆盖半条线，前车在前方那半条时会被漏判 ⇒ `dir` 与 `ReverseDiagDir(dir)` 两次都真才算空。
2. **`start` 必须满足 `GetRailStationAxis(start) == DiagDirToAxis(dir)`**（函数内有 `assert_tile`）。后车可能停在本到发线**端点格**上而"更空的方向"在身后——两个方向都要用同一 axis 推出的 `DiagDir`，不能随手拿前进方向当 `dir`（前进方向可能是纵向，与站台轴垂直时直接断言炸掉）。

### 11.6 由此收敛的实现形态（取代 11.3 草案）

C-1/C-2/C-3 拍板 + 11.5 取证之后，实现分成"判据"与"占位"两层，判据层不再依赖占位：

```
每 4 tick（C-3）:
  /* 判据层：只读，零副作用 */
  d1 = 沿站台轴的正向 DiagDir；d2 = ReverseDiagDir(d1)
  if (!IsRailStationPlatformFree(consist, consist->tile, d1)) return 等;
  if (!IsRailStationPlatformFree(consist, consist->tile, d2)) return 等;

  /* 占位层：C-1 口径下把本到发线每一格订下（挡住别的车进这条到发线） */
  for (每一格 t in 本到发线，逐格 IsCompatibleTrainStationTile 校验):
      if (格上有本链车节) continue;                          // C-2
      if (!HasStationReservation(t)) SetRailStationReservation(t, true);  // 记账走 ledger
  放行
```

- 判据层用现成函数（`IsRailStationPlatformFree`），成本是"本到发线格数 × 全车数"，比"逐格试订位再回溯 owner"轻得多，也不会因为别人清位而抖动。
- 占位层保留（符玩家原话"用预留占用整个站台"），但它**不再承担判据职责**：它的唯一作用是让**其它车订不进这条到发线**。⇒ 判据不可能因为占位被 reap 而失效（这是我前一轮最担心的风险，至此消解）。
- 释放：后车开走时引擎的整平台清已覆盖（11.5-U-4 结论）；**只剩撞毁一条需要 R3R 兜底**。
- 仍需注意：占位格必须进 `R3RResvLedger`，否则会被 `RESV-AUDIT` 判 STRAY 再由 `RESV-REAP` 清掉（KI-345/346 家族）；清掉只影响"挡别的车"，不影响判据。

---

## 十二、C-1 的适用范围澄清（2026-10-21 玩家追问）+ 开工

**玩家原话**：「C1，我们不是在讲列车解挂分离吗，那前车怎么会跑到另一条到发线呢。如果没问题，就可以动手了」

### 12.1 玩家直觉对的部分：解挂双方必然同一条到发线

解挂是把**一条链**在站台上切成两半，两半物理上首尾相接、必然停在同一组站台格上（同 `station_id`、同平台轴、同 `IsCompatibleTrainStationTile` 链）。⇒ 解挂场景里**不存在**"前车在另一条到发线"这种形态。玩家这一问的直觉成立。

### 12.2 C-1 真正的差别不在解挂双方，而在两处

| 场景 | 整站口径 | 本到发线口径 |
| --- | --- | --- |
| **第三方列车**停在同站另一条到发线 | 后车也要等它离开整站 | 后车不等它（正确：它不是本链的延续，与"前车"无关） |
| 前车解挂后**开出**本到发线、经道岔转入同站另一条到发线 | 继续等它出整站 | 它离开本到发线第一格即放行 |

⇒ 所以 C-1 不是"前车会不会跑到别的到发线"的问题，而是"**要不要被同站另一条轨道上的无关列车拖住**"的问题。玩家选「本到发线」= 不被无关列车拖住，这是更精确的口径，与解挂语义一致。

### 12.3 一个必须承认的副作用（已被玩家接受）

解挂后如果前车在**同站另一条到发线**上（第三方场景）或前车开走后本到发线又驶入别的车，后车都**不会**被阻塞。也就是说本口径保证的是"**本到发线**上属于我自己的空间已经归我"，不保证"整个车站没有别的车"。这正是玩家要的。

### 12.4 开工决定

三条口径 + 上述澄清确认，按 §11.6 动手实现。实现清单（本轮）：

1. `R3RDecoupleInterlockWait()` 判据层替换为双向 `IsRailStationPlatformFree()`。
2. 删除信号机判据链：`R3RSeamSignalState()`、`R3RSeamBeyondSignalHeldByLeader()`、8 格距离逃生阀、3600 tick 超时。
3. 每 4 tick 重试（`R3R_DECOMP_INTERVAL` 同源节流），不设强制放行。
4. 占位层：本到发线逐格订位 + 记账；撞毁兜底释放（C-3 唯一承认的异常出口）。

### 12.5 实现落地（2026-10-21 已改码 + 已编译）

**实际改动（仅 `src\train_cmd.cpp`，未碰 `src\*.h`）**：

1. 新增 `static bool R3RPlatformClearForConsist(const Train *consist)`：非站台格返回 true；否则 `GetRailStationAxis()` → `AxisToDiagDir()`，**正反两向各调一次** `IsRailStationPlatformFree()`。
2. `R3RDecoupleInterlockWait()` 判据段整段替换为「每 4 tick 查一次 `R3RPlatformClearForConsist()`；为真则 `TryPathReserve()` + release，否则 hold」。删掉 `R3RSeamSignalState`/`R3RSeamBeyondSignalHeldByLeader`/`R3RSeamSignalReserved` 的调用、删 `leader_far` 逃生阀、删 3600 tick timeout。
3. 常量：删 `R3R_DECOMP_SIGNALLESS_TILES`；`R3R_DECOMP_WAIT_LIMIT` 退化为 map 剪枝常量（不再放行任何车）。
4. 新日志：`DECOUPLE-INTERLOCK release … reason=platform-clear` / `… hold … reason=platform-busy`。

**与 §12.4 清单的唯一偏差：占位层本轮没做。** 理由必须写清：判据已由只读函数给出，
占位只剩"挡住别的车进本到发线"这一个附加作用；而它没有 ledger 主人，逐格 `SetRailStationReservation()` 会被 `RESV-AUDIT` 判 STRAY、3 次后被 `RESV-REAP` 清掉，等于新增孤儿位来源（KI-345/346 家族）。先让他「等得对」，占位留作后续可选项。

**构建自证（21:51，内测树）**：`R3R_incbuild.done` = `EXIT_CODE=0`；error 计数 0；
`src\train_cmd.cpp` 21:49:21 → `train_cmd.cpp.obj` 21:50:08 → `build\openttd.exe` **21:51:29（52 036 096 B）**；
`read_lints` 0；exe 命中 `reason=platform-clear` / `reason=platform-busy`。发布树未编。

三个信号机静态函数（`R3RSeamSignalState`/`R3RSeamSignalReserved`/`R3RSeamBeyondSignalHeldByLeader`）现已无调用点，保留未删（MSVC 不报错），后续可清理。

### 12.6 补丁：`res=0` —— 站台判据不等于可放行（同日现场回填）

玩家 2026-10-21 现场原话：「到发线的站台一空，后车就兴冲冲地启动了，都不向整个站台发出预留吗。还有就是它决定出站的时候都不看站台到出站信号灯能不能全部铺上自己的预留吗」

`build/R3R_debug.log` 只有一次解挂，四行：

```text
DECOUPLE-INTERLOCK set     head=21 follower=24 seam_car=47 tile=272,237
DECOUPLE-INTERLOCK hold    head=21 follower=24 dist=0 age=0 left=0 n=1 reason=platform-busy
DECOUPLE-INTERLOCK wait    head=21 follower=24 seam=272,237 seam_car=47 dist=0 age=0 left=0
DECOUPLE-INTERLOCK release head=21 follower=24 seam_car=47 dist=8 age=8 left=1 res=0 reason=platform-clear
```

**`res=0` = `TPRRF_NONE`**（`train.h:98-99`：`TPRRF_NONE=0` / `TPRRF_RESERVATION_OK=0x01`；`TryPathReserve()` 内联版返回 `flags & TPRRF_RESERVATION_OK`，即 bool）⇒ 放行那一刻预留**失败**。§12.5 实现只把 `res` 当日志字段，没当条件 ⇒ 后车零预留起步，正是玩家看到的现象。

⇒ **§12.5 第 2 条的判据只够一半**：站台空只回答"该不该让"，不回答"能不能走"。修法加第二道门：

```cpp
if (R3RPlatformClearForConsist(consist)) {
    r3r_res = TryPathReserve(consist, false, false) ? 1 : 0;
    if (r3r_res != 0) { release; erase; return false; }
    hold_reason = "platform-clear-no-route";   // 站台空了但订不到路
}
```

- `first_tile_okay=false` 保住了 `train_cmd.cpp:15969` `origin.okay && (moving_front->tile != origin.tile || first_tile_okay)` 那道"脚下格不算终点"的检查，不会假放行。
- 门②成功即"脚下 → 出站侧第一个安全位整段订上（站台剩余格一并订入）"，这就是玩家第二问要的那个检查。
- ②失败 ⇒ hold + `reason=platform-clear-no-route`，4 tick 后重试，无超时（C-3）。

**构建自证（22:02）**：`_tmp_inc_build.cmd`（复用，未新建）；`[2/3] Building CXX object … train_cmd.cpp.obj` → `[3/3] Linking`；
`R3R_incbuild.done` = `EXIT_CODE=0`，error 计数 0；`src\train_cmd.cpp` 22:00:40 → obj 22:01:26 → `build\openttd.exe` **22:02:40（52 036 096 B）**；`read_lints` 0 条；
二进制串核对 `platform-clear-no-route`=True、`reason=platform-clear`=True（`reason=platform-busy` 改为 `reason=%s` 后不再以该前缀出现，属正常）。发布树未编。

**待观察**：若长时间停在 `platform-clear-no-route`，说明前车压在出站侧同一 block（查 `TPR-BLOCKED` / `TPR-WALK`），属"该等"，不是回归。

### 12.7 复测通过（同日第二份现场日志）

玩家：「现在列车的行为符合我的预期了，你看看日志」。三态链一次跑全：

```text
L4137 set     head=21 follower=24 seam_car=47 tile=272,237
L4172 hold    dist=0  age=0  left=0 res=-1  n=1   reason=platform-busy
L4173 wait    seam=272,237 seam_car=47 dist=0 age=0 left=0
L4204 hold    dist=8  age=8  left=1 res=0   n=111 reason=platform-clear-no-route
L4267 release dist=16 age=12 left=1 res=1   reason=platform-clear
```

`res` 三值 −1 → 0 → 1 把两道门的分工完整暴露出来：上一份日志缺的就是中间那个 0（当时直接 release）。
`platform-clear-no-route` 期间后车 `TPR-WALK … okay=0 new=272,237 reach=1` 反复尝试、
前车 veh=47 同时从 272,237 起逐格 `CLEAR-PATH` ⇒ 卡的是出站 block，不是站台判据。
release 后 `TRP veh=24 … ok=1 res=1` / `LR-TRACE … cur=177`，带预留起步。
全文无 timeout / STRAY / REAP。

**遗留判读陷阱**：`n=111` 与 `age=8` 不自洽（age 只走到 12，其间跨 63 行日志）。
与第 268 轮"age 停了不动"同源 ⇒ `_tick_counter` 在解挂后不能当经过时间用；
节流本身没问题（签名含 `res`），但读日志要用 `n × 4 tick` 估等待，不要用 `age`。

### 12.8 第 271 轮：预留在车前被销毁（同日第三份现场）

玩家：「列车明明已经向某处铺设了预留，但是走过去之前又被神秘销毁，导致列车无法达速运行」

同一文件的放大版（8838 行，预留相关 81%）给出完整链条：

```text
L7193 CPL-PATHFOUND    veh=21 found=0
L7292 RESV-LEDGER-FORGET veh=21 why=free-path released=95 kept=0 phase=choose-track
L7295 CPL-SKIP         veh=21 tile=205,273 orderType=16 retryIn=8 holdAge=0
L7450 RESV-LEDGER-FORGET … released=85 …
L7508 RESV-LEDGER-FORGET … released=58 …
```

`found=0` 的车（21）正在奔向待挂车底（24），每次搜索失败就把 58~95 格已铺预留连账本一起清空，
形成「铺满 → 走几格 → 清零 → 再铺」。tick 3036 才 `found=1`。

两条通道：

1. `YapfTrainCoupleTrack()` 把节流跳过（`R3RCoupleScanHold`）直接返回成 `INVALID_TRACK`，
   与"真没车底"同值；`ChooseTrainTrack()` 按 KI-316 撤预留 + `MarkTrainAsStuck`。
2. 真 `found=0`（`CPL-SCAN … consist=5 rej=5 why=GROUP`）时也清，而车还在动 ⇒ 自残。

修法：`yapf_rail.cpp` 增 `_r3r_couple_skipped_tick` + `R3RCoupleSearchSkipped()`；
`train_cmd.cpp` 的 `INVALID_TRACK` 分支加两道 early-return（节流跳过 / `cur_speed != 0`），
保持现状不撤预留。KI-316 的"停车等待"只在 `cur_speed == 0` 时生效。

**未动**：`why=moved-gone`（24 条）、`free-path-hold`（14 条站台格）、`RESV-REL CLEAR-STN`（154 次整平台）。
它们量级小且各有明确 owner 检查，本轮不碰，避免一次改太多。

**观察项（未修）**：`CPL-SCAN … noplat=200 nores=6 noconsist=83 consist=5 rej=5 why=GROUP`
说明候选车底被 GROUP 规则拒 5 次 —— 这是 21 迟迟挂不上的另一半原因，属寻路规则而非预留，
待确认后再动。

### 12.9 第 271 轮续：`train_cmd.cpp:18097` 断言（同轮回归，已修）

玩家：「train_cmd 18097行断言报错了，我们应该在触动这座屎山」。

**断言原文**（`train_cmd.cpp:18097`，`TrainController` 里挑轨的调用点）：

```cpp
ChooseTrainTrackResult result = ChooseTrainTrack(first, gp.new_tile, enterdir, bits, CTTF_MARK_STUCK | CTTF_NON_LOOKAHEAD);
assert(IsValidTrack(result.track));          // ← 18097 炸在这
```

**根因（§12.8 那两条 early-return 自己造的）**：`ChooseTrainTrack()` 的 `best_track`
（`train_cmd.cpp:15258` 初值 `INVALID_TRACK`）**只在 15310 的单轨快速路径上被赋值**；
15275 `KillFirstBit(tracks) == TRACK_BIT_NONE` 不成立（多轨岔口）时它一路保持 `INVALID_TRACK`。
§12.8 加的两条 early-return 写的是 `return { best_track, result_flags }`：

```cpp
if (R3RCoupleSearchSkipped(consist)) return { best_track, result_flags };   // 节流跳过
if (consist->cur_speed != 0)         return { best_track, result_flags };   // 行驶中
```

注释里那句「此时它仍是 `FindFirstTrack(tracks)`」是**错的**——那正是 KI-316 原返回
（15496 行 `return { FindFirstTrack(tracks), result_flags };`）的写法，early-return 没有照抄。
于是在岔口格子上，只要"节流跳过"或"车还在动"，返回的就是 `INVALID_TRACK` ⇒ 18097 断言。

**修法（只改返回值，最小面）**：两条 early-return 之前算一个
`const Track keep_track = (best_track != INVALID_TRACK) ? best_track : FindFirstTrack(tracks);`，
返回改用 `keep_track`，并加 `keep_track != INVALID_TRACK` 守卫；两者都无效时**落回**
KI-316 原处置（与 212 轮之前完全一致）。不动预留、不动 `stuck`、不动订单、不碰头文件。

```cpp
const Track keep_track = (best_track != INVALID_TRACK) ? best_track : FindFirstTrack(tracks);
if (keep_track != INVALID_TRACK && R3RCoupleSearchSkipped(consist)) return { keep_track, result_flags };
if (keep_track != INVALID_TRACK && consist->cur_speed != 0)         return { keep_track, result_flags };
```

**为什么不是"改断言"**：18097 的下一条 18100 还要求
`chosen_track & (bits | GetReservedTrackbits(gp.new_tile))`，拿 `INVALID_TRACK`
过 18097 只会在 18098 `TrackToTrackBits(INVALID_TRACK)` 或后续换轨逻辑里出错，
所以必须让 `ChooseTrainTrack()` 交出**本格可达的**轨道位。

**构建（2026-10-21 22:55）**：`_tmp_inc_build.cmd`（复用，未新建 .cmd）。
guard = `incremental is safe`；`[2/3] train_cmd.cpp.obj` → `[3/3] Linking CXX executable openttd.exe`；
`build\R3R_incbuild.done` = `EXIT_CODE=0`；
`src\train_cmd.cpp` 22:52:46 → obj 22:54:22 → `build\openttd.exe` 22:55:33（52 036 096 B）；
`read_lints` 0 条。发布树未编。

**复测判据**：
1. 岔口格子上不再出现 18097 断言；
2. `CPL-SKIP` 之后仍无 `RESV-LEDGER-FORGET … why=free-path`（§12.8 判据不回退）；
3. 挂接成功路径（`COUPLE-OK`）不变，岔口挑轨行为与 212 轮一致（同 `best_track` 回退口径）。

