# 第 335 轮（2026-10-27）：掉头后「一次性长预留」在 R3R 内测版消失

## 一、玩家报告（原话）

> 是这样的，列车在尽头式路点处执行完换向指令（甚至不用这样，让它自然调头）的时候，它都会铺设一次长预留，但是，R3R 现在的内测版本里面，这个行为表现不出来了，这肯定是 R3R 自己的锅

要点：**原版（JGRPP 0.72.4 口径）在掉头/换向后必然重新铺一次「整条路径」的长预留**；R3R 内测版看不到了。玩家自己指出：两种触发方式（订单换向、自然调头到尽头）**都不出现** ⇒ 倾向共性机制，不是 `ForceFlipReverse` 专属路径。

## 二、已确认事实（源码 + blame，非推断）

### 2.1 上游掉头语义 = 主动清空整条预留

```5313:5318:src/train_cmd.cpp
	/* Clear path reservation in front if train is not stuck. */
	if (!consist->flags.Test(VehicleRailFlag::Stuck) && !no_near_end_unreserve && !no_far_end_unreserve) {
		FreeTrainTrackReservation(consist);
	} else {
		consist->lookahead.reset();
	}
```

`git blame -L 5305,5320`：

| 行 | 提交 | 作者/日期 |
| --- | --- | --- |
| 5313 注释 | `1838e8939f0` | JGRPP 上游 2021-01-24 |
| 5314 条件 | `6b8d03dc2d8` | JGRPP 上游 Tyler Trahan 2026-03-16 |
| 5315 `FreeTrainTrackReservation` | `6b8d03dc2d8` | 同上 |
| 5317 `lookahead.reset()`（else 分支） | `6b8d03dc2d8` | 同上 |

⇒ **这四行全是上游原句，R3R 一个字都没改**。所以「原版掉头后有长预留」在代码里的成因是确定的：

1. `ReverseTrainDirection()` 入口（`:5315`）把整条路径预留全部释放（`FreeTrainTrackReservation`）；
2. 下一 tick 车重新进 `TrainController` → `ChooseTrainTrack`，此时 **`res_tracks == TRACK_BIT_NONE`**；
3. 于是**不会**撞上 `:17129` 的早退（`if (res_tracks != TRACK_BIT_NONE && ...) return ... "early-haverserved"`）；
4. 顺势走到 `ExtendTrainReservation` / yapf 全路径搜索，一次性把预留铺到终点 ⇒ 玩家看到的那一次「长预留」。

**这一条把因果链锁死了**：长预留的出现条件不是「掉头」本身，而是「掉头把 `res_tracks` 清成空」。

### 2.2 R3R 侧已知会「把脚下格重新订上」的入口

`Train::ReserveTrackUnderConsist()`（`src\train_cmd.cpp:19073` 定义）为链上每节车所在格订轨道位，现有调用点（不全，含被截断的）：

- `:10311-10312` 解挂后 `u->ClearReservationUnderConsist(); u->ReserveTrackUnderConsist();`
- `:19425` crashed train 重订
- `:21814` 站台相关分支（`lookahead.reset()` 之后紧接 `ReserveTrackUnderConsist()`）
- 另有「R3R platform-waiter gate」每 tick 调用（`:8433-8434` 注释自陈，KI-203/KI-208 家族）

## 三、猜测根因（待验证，按优先级）

**G-1｜掉头清空后，脚下格被 `ReserveTrackUnderConsist()` 立刻订回 ⇒ 又撞早退**
`FreeTrainTrackReservation` 清的是「路径预留」，但 R3R 的保护层会在同一 tick / 下一 tick 把链上各节车所在格的轨道位重新订上。若 `ChooseTrainTrack` 的 `res_tracks` 把这笔算作「本格已有优先轨」（取决于是不是与 `enterdir` 可达位相交），则 `:17129` 立刻 `early-haverserved` 返回，`ExtendTrainReservation` 一行不跑 ⇒ 长预留消失。

**G-2｜`Stuck` 位为真 ⇒ 上游自己就不 Free**
`:5314` 的条件里带 `!Stuck`。R3R 大量路径会 `MarkTrainAsStuck`（KI-316、KI-221/222 等），若掉头瞬间 `Stuck` 仍为 1，则走 else 只 `lookahead.reset()`，**旧预留原样留着** ⇒ 下一 tick 同样撞早退。判据现成：`REVERSEDIR` 探针第 5245 行就打了 `stuck=%d`。

**G-3｜R3R 新增门禁把「掉头后的重铺」拦掉了**
KI-208 的 `RESV-NOBOOK`（`reason=parked-waiter`）、以及 `:18063-18067`「不清路径预留，因为会牵动 ReserveTrackUnderConsist」的处置，都是 R3R 新增的「不订 / 不清」决策。若掉头后落在这些门里（停在站台/等待挂接/`cur_speed==0` 的链），重铺会被整条拒绝 ⇒ 连「先清后铺」都不发生。

**G-4｜订单换向路径（`ForceFlipReverse` + `force_end_swap` 分支）与自然掉头走了不同分支**
若只有换向指令失效，指向此条；但玩家明确说**自然调头也不出现** ⇒ 本条降级为次要，只在 G-1/G-2/G-3 都被证伪时再查。

## 四、疑点（一发现即登记）

- **U-1**：`ChooseTrainTrack` 里的 `res_tracks` 到底怎么算——是否包含 `ReserveTrackUnderConsist()` 订下的「车实际所在轨」？若要求「与 `enterdir` 可达位相交」，则 G-1 是否成立取决于掉头后车所在的 track 与 enterdir 的关系（单轨格恒相交 ⇒ 必撞早退）。
- **U-2**：自然掉头与订单换向**都**消失 ⇒ 共性机制。但 R3R 的 `FreeTrainTrackReservation` 调用点被改过多处（`:17427/17668/17752/17772/17824/17834` 一带），是否存在某条分支在掉头场景下把 `Stuck` 置位后再 Free？需对读。
- **U-3**：玩家的「长预留」观感是否等价于「`ExtendTrainReservation` 铺到终点」还是「`DoTrainCouplePathfind` 铺的整条 couple 路径」？两者视觉上都长，但代码路径不同，判据要用日志区分（`RP cur=` 长串 vs `CPL-RESERVE`）。
- **U-4**：第 326/327 轮（自动填充/路点挂接启动）与第 328/332/333/334 轮的 `rp()`/`DoTrainCouplePathfind` 改动，是否顺带改变了「无预留时」的首铺行为？若是，本条可能只是那几轮改动的副产物，而非独立缺陷。

## 五、待验证判据（拿到 `build\R3R_debug.log` 后逐条对）

1. 找 `REVERSEDIR veh=… tile=… stuck=?` 行：
   - `stuck=1` ⇒ **G-2 成立**（上游自己没 Free）。
   - `stuck=0` ⇒ 看第 2 条。
2. 紧跟其后的第一条 `CRT veh=… tile=… td=… origin=… found=?`：
   - `found=0` 且同 tick 出现 `TRP … why=early-haverserved`（或 `RP` 只有 1 格）⇒ **G-1 成立**（脚下格被重订 / 早退）。
   - 完全没有 `CRT`/`TRP` 行 ⇒ **G-3 成立**（重铺被门禁拦掉，找 `RESV-NOBOOK reason=`）。
   - `found=1` 且 `RP cur=` 长串 ⇒ 长预留其实发生了，玩家的观感问题在别处（渲染/缓存），转 U-3。
3. 同一场景与 JGRPP 0.72.4 对照（A/B 取数工具 `R3R_AB_probe.cmd` 已在工作区）：原版应出现 `REVERSEDIR` 后首次 `CRT found=1` + 长 `RP` 串。
4. 反例（不得误伤）：正常运行中的列车（无掉头）不应凭空多出长预留；`RESV-NOBOOK` 的等待者仍不订路线。

## 六、修法方向（未落地，待判据回来再定）

- 若 G-1：让「掉头后的首铺」与「脚下保护」解耦 —— 早退判据不能只看 `res_tracks != TRACK_BIT_NONE`，而要看「这条预留是否覆盖到下一个决策点」（即真正需要的语义）；最小改动 = 在 `ReverseTrainDirection` 之后打一个「本轮允许重铺」的一次性标志（类似 `ForceReserveOnce` 的语义），让 `:17129` 放行一次 `ExtendTrainReservation`。
- 若 G-2：掉头前先 `flags.Reset(Stuck)`（或把 `:5314` 的 `!Stuck` 改成「Stuck 时也 Free 路径但保留脚下保护」），并核对 KI-316 的处置边界。
- 若 G-3：把 `RESV-NOBOOK` 的拒绝范围收窄到「真的不需要出路」的等待者，掉头后 `cur_speed==0` 但订单是行驶类的链必须放行。

## 七、本轮状态

- 已确认：上游掉头必 Free（`:5313-5318`，blame 佐证）。
- 未确认：G-1 / G-2 / G-3 三者谁是主因（缺 `REVERSEDIR` 附近现场日志）。
- 源码：**本轮未改任何 .cpp，未构建**。
