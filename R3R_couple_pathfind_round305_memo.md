# 第 305 轮：删账本后复测，挂接寻路仍有问题（2026-10-25）

> 状态：取证中。按铁律"先落盘再取证"，本条在收到报告的第一时间建立，之后每组取证结论**立刻**追加。

## 0. 玩家报告原文

「我对删除账本后的日志进行了测试，依然发现一些问题，可能与挂接寻路有关，详情见R3R_debug.log」

## 1. 输入（已核实）

| 项 | 读数 |
|---|---|
| `build\R3R_debug.log` | 663,123 B / **6,658 行** / LastWriteTime **2026-10-25 23:33:49** |
| `build\openttd.exe` | 52,230,144 B / LastWriteTime **2026-10-25 23:25:53** |

⇒ 日志**晚于** exe 8 分钟，产生者就是第 304 轮"读档断言修复版"（两代预留账本已删 + 本轮号池修复）。

**正面结论（先记）**：本次会话**没有**出现 `vehicle.cpp:3094` 断言崩溃、日志完整写到 6658 行 ⇒ 第 304 轮读档修复在"能进游戏"这一点上未复发（但缺玩家旁证：是否用过同一份旧档，待问）。

## 2. 标签计数（`Select-String | Group-Object`，全 6658 行）

| 标签 | 次数 |
|---|---|
| `CPL-`（含 CPL-PAIR/SCAN/BEST/BACK/SKIP/SAFE-FAIL/ENTRY/ORIGIN/PATHFOUND/HEAD） | 1467 |
| `COUPLE-FAIL` | 12 |
| `COUPLE-OK` | **3** |
| `COUPLE-PREFLIGHT` | 3 |
| `RESV-GHOST` | 20 |
| `RESV-NOBOOK` | 2 |
| `RESV-AUDIT-STRAY` | **0** |
| `RESV-REAP` | **0** |
| `FOLDCHK` | **0** |
| `SPLICE-GAP-REJECT` | **0** |
| `COUPLE-DEST-EMPTY` | **0** |
| `CPL-PAIR` | 存在（含入 CPL-） |

⇒ 账本删除后**没有**重现 `RESV-AUDIT-STRAY`/`RESV-REAP`（孤儿预留走廊）——符合第 303 轮 A 项"读法甲=现状、差集≈0"的预期。问题面转向**挂接寻路本身**。

## 3. 现场读数（日志尾部 55 行，`build\R3R_debug.log:6604-6658`）

### 3.1 veh=51 的死循环（每 tick 同一套）

```
CPL-ENTRY veh=51 tile=252,234 orderType=16 dontReserve=0
CPL-ORIGIN veh=51 origin=252,234 td=8 vehDir=1 vehTd=8 dontReserve=0
PFD-ACCEPT kind=waiting tile=278,231 t=24 ordType=17 wc=1        (×3)
CPL-PATHFOUND veh=51 found=1
CPL-SCAN tick=3151 veh=51 found=1 closed=6 limit=10000 nodes=28 enter=22 noplat=13 nores=0 noconsist=4 consist=5 rej=2/0/0/0 acc=0/3 accwp=0 last=278,231 lastveh=24 why=acc-wait
CPL-HEAD best=278,231 head=265,231 on=0
CPL-BEST veh=51 tile=278,231 td=8 cost=17365
CPL-BACK pPrev=278,231 td=8 parent=259,232
CPL-BACK pPrev=259,232 td=11 parent=258,233
FSCP tile=259,233 fail=tryReserve td=10 want=2 type=1 tbits=0x25 res=0x1 plain=1 depot=0 st=0
FSCP-RES tile=259,233 tk=0 owner=21 otile=253,233 odir=1 ospd=0 ostop=0 ord=16 oroi=8 oend=259,233 oendtd=8 othru=1 octgt=0 ocplok=0 cpl=252,234
FSCP-REACH tile=259,233 veh=21 ord=16 roi=8 pos=253,233 dir=1 spd=0 stopped=0 seg=1 ... end=259,233 endtd=8 isGtr=1 cpl=252,234
FSCP-VEH tile=259,233 veh=47 track=0x1 dir=1 front=0 ord=0
CPL-SAFE-FAIL veh=51 tile=259,232
CPL-SKIP veh=51 tile=252,234 orderType=16 dontReserve=0 retryIn=7 holdAge=1
```

**读数判定**：
- 寻路**成功**找到候选（`found=1`，`why=acc-wait`，`lastveh=24`，目标格 `278,231`），
- 但"安全位校验"在回溯路径上的 `259,233` 处 `fail=tryReserve`；
- 该格 `tbits=0x25`（三轨位）、`res=0x1`，即 **TRACK_X(0) 这一位已被预留**；
- 预留主人 `owner=21`：`ord=16`（OT_GOTO_COUPLE）**也是挂接寻路的车**，`ospd=0`（已停稳）、`stopped=0`、`oend=259,233`（终点正是这一格）、`othru=1`；
- 同格上还有 `veh=47 track=0x1 dir=1 front=0 ord=0`（非链头、无订单？）。

⇒ **疑点 A（首要）**：挂接寻路的"安全位"（`FindSafeCouplePositionProc`）撞上了**另一辆同样在找挂接目标的车（veh=21）自己预留的终点格**。这类"两车互相把对方的预留格当成不可用"是否构成死锁？

### 3.2 veh=99 完全找不到候选

```
CPL-ENTRY veh=99 tile=352,235 orderType=16 dontReserve=0
CPL-PATHFOUND veh=99 found=0
CPL-SCAN tick=3150 veh=99 found=0 closed=67 limit=10000 nodes=248 enter=181 noplat=123 nores=6 noconsist=52 consist=0 rej=0/0/0/0 acc=0/0 accwp=0 last=-1,-1 lastveh=-1 why=none
```

**读数判定**：`nodes=248`、`closed=67`、`why=none` ⇒ **一个候选都没被接受**（`acc=0/0`）。对比 veh=51 的 `closed=6 nodes=28 acc=0/3`（6 个 closed 里就有 3 个 accepted），veh=99 是搜到 248 个节点、`noconsist=52`（52 次判"不是可挂目标"）却零接受。

⇒ **疑点 B**：`noconsist=52` 与 `CPL-PAIR act=99 tgt=75 dist=86/87/88`（`tgtTile=265,234`，每 tick 距离 +1 递增）同时存在——配对目标 veh=75 存在、但扫描时全部被判非目标。veh=99 正在**远离**目标（actTile 350→352，dist 86→88），说明它一边跑一边找，而距离在变大。

### 3.3 配对目标的异常

```
CPL-PAIR tick=3145 act=99 tgt=75 dist=86 actTile=350,235 tgtTile=265,234
CPL-PAIR tick=3147 act=99 tgt=75 dist=87 actTile=351,235 tgtTile=265,234
CPL-PAIR tick=3149 act=99 tgt=75 dist=88 actTile=352,235 tgtTile=265,234
LOCO-MOVE veh=99 tile=352,235 j=209 spd=30 track=01 dir=1 db=1 stuck=0 dtile=479497
```

⇒ **疑点 C**：`db=1`（DrivingBackwards）的车在跑，`dtile=479497` 恒等（终点没变），但 `found=0`。**倒车链 + 目标在反方向** 是否为已知的寻路盲区？

### 3.4 其它并发噪声

```
CRT veh=123 order=1 dir=1 origin=256,235 td=8 found=1   (每 tick 刷 6~7 行)
```

⇒ veh=123 每 tick 反复做 `TryPathReserve` 并 `found=1`，但日志尾部看不到它移动——**疑似预留成功但车不动**（也可能是别的探针缺行，暂记疑点）。

## 4. 疑点登记（按规则：一发现即落盘）

| 编号 | 疑点 | 期望 | 实测 | 状态 |
|---|---|---|---|---|
| U-A | 挂接寻路把"另一车的预留终点格"当不可用，导致 `CPL-SAFE-FAIL` | 两辆都在找挂接目标的车不应互相把对方**必然会让出**的格算死 | `FSCP tile=259,233 fail=tryReserve`、`owner=21`（同为 ord=16、ospd=0） | **机制已坐实（见 §4.1）** |
| U-B | veh=99 扫描 248 节点零接受 | 有配对目标（veh=75，dist 86）时至少应命中 | `acc=0/0`、`noconsist=52`、`why=none` | 待取证（G3/G4） |
| U-C | 倒车链（`db=1`）挂接寻路零命中 | 应与正向链等价 | `veh=99 db=1 found=0` | 待取证（G5） |
| U-D | veh=123 每 tick `CRT found=1` 却似乎不动 | `found=1` 后应移动或至少改变 origin | origin 恒 `256,235` | 待取证（G5） |
| U-E | 读档修复是否真被这份日志验证 | 用同一份旧档载入 | 日志无 `vehicle.cpp:3094` 崩溃 | **待问玩家**：这份档是不是"旧 R3R 存档" |

## 4. 结论（第一轮回填，2026-10-25）

### 4.1 `FSCP fail=tryReserve` 的确切机制（代码级，不再靠猜）

`src/rail_map.h:267-277`：

```cpp
inline bool TryReserveTrack(TileIndex tile, Track t)
{
	dbg_assert_tile(HasTrack(tile, t), tile);
	TrackBits bits = TrackToTrackBits(t);
	TrackBits res = GetRailReservationTrackBits(tile);
	if ((res & bits) != TRACK_BIT_NONE) return false;  // already reserved
	res |= bits;
	if (TracksOverlap(res)) return false;              // crossing reservation present
	SetTrackReservation(tile, res);
	return true;
}
```

代入现场 `FSCP tile=259,233 fail=tryReserve td=10 want=2 type=1 tbits=0x25 res=0x1 plain=1`：

| 变量 | 值 | 含义 |
|---|---|---|
| `want=2` | `TrackdirToTrack(td)` = `TRACK_UPPER`（`Track` 枚举 X=0/Y=1/UPPER=2/LOWER=3/LEFT=4/RIGHT=5） | veh=51 想订 **上侧斜轨** |
| `tbits=0x25` | `X(0x1) \| UPPER(0x4) \| RIGHT(0x20)` | 该格三条轨都在，`HasTrack` 与 `dbg_assert_tile` 都过 |
| `res=0x1` | 已被订的是 **X** | 不是 51 想订的那条 |
| 第一道 gate `res & bits` | `0x1 & 0x4 = 0` | **过** |
| 第二道 gate `TracksOverlap(0x5)` | X 与 UPPER 在交叉格上互相穿越 ⇒ **真** | **这里返回 false** |

⇒ **失败原因不是"该格没有这条轨"，也不是"同一条轨被别人占了"，而是"这一格别人的预留轨与我要订的轨在交叉点撞上了"**。这是 PBS 的正确语义（`TracksOverlap` 就是为了防交叉预留），本身不该改。

⇒ 真正的问题是：**为什么 veh=51 的挂接路径必须穿过一个已被别人订死的交叉格，而且一失败就整次挂接作废、每 tick 重试？**

### 4.2 谁是那个"别人"：`othru=1` ⇒ 活车，不是孤儿

- 本轮的 `FSCP-RES ... othru=1`、`oend=259,233`、`oendtd=8`、`ospd=0`、`ostop=0`、`ord=16`、`otile=253,233`。
- 对比 **KI-439**（第 278 轮，未修）的同类现场：那条是 `othru=0` + `FSCP-REACH-NONE` ⇒ 判定"**孤儿/陈旧位** + `GetTrainForReservation()` 误指"。
- 本轮 `othru=1` ⇒ `FollowTrainReservation()` 沿连续预留真的能走回 veh=21 ⇒ **是 veh=21 的活预留，不是孤儿位**。

⇒ **KI-477 与 KI-439 同族但不同因**：KI-439 的修法（F-278-A：`FSCP-REACH-NONE` 时清孤儿位放行）对本条**无效**，因为这条的位是有主的。

### 4.3 活锁结构（两个 COUPLE-OK 之后互相卡死）

时间线（行号来自 `build\R3R_debug.log`）：

| 行 | 事件 |
|---|---|
| `:1744` | `COUPLE-OK loco=99 rear=98 consist=75` —— 99 挂上 75 那列 |
| `:1957` | `COUPLE-OK loco=51 rear=50 consist=24` —— 51 挂上 24 那列 |
| `:2131` | `COUPLE-OK loco=21 rear=146 consist=123` —— 21 挂上 123 那列 |
| `:3752` | `COUPLE-FAIL loco=99 order=16 tx=253 ty=233`（99 开始找下一个目标，失败） |
| `:4426` 起 | `COUPLE-FAIL loco=51 order=16 tx=252 ty=234`（12 次里有 7 次是 51） |
| `:5244` 起 | `COUPLE-FAIL loco=21 order=16 tx=253 ty=233`（4 次） |

- 21 与 51 一个停在 **253,233**、一个停在 **252,234**（相邻格），**两辆都在找挂接目标（`order=16`）**；
- 21 持有从自己位置铺到 **259,233** 的预留（`oend=259,233`），而它 `cur_speed=0`、未 `Stopped`；
- 51 要挂的目标在 **278,231**（`CPL-BEST`/`CPL-HEAD`，`PFD-ACCEPT kind=waiting tile=278,231 t=24 ordType=17`），回溯路径必经 **259,232 / 259,233** 一带；
- 259,233 的 X 被 21 订死，51 想订 UPPER ⇒ `TracksOverlap` ⇒ 失败 ⇒ `CPL-SAFE-FAIL` ⇒ `FindNearestCoupleTrain()` 整次作废（`:4940/:5020/:5094/:5133/:5318…/:6658` 共 **21 次**，其中 51 占 20 次）；
- 21 在等挂接所以不动、51 因为 21 的预留过不去也动不了 ⇒ **双方都不推进，日志从 `:4940` 一直刷到末行 `:6658`（约 1700 行）**。

⇒ 这不是"某条预留算错了"，而是**两辆都在 `GOTO_COUPLE` 的车把对方当成路的对手**：一辆停车却继续持有预留，另一辆的挂接安全位试探被这条预留否决后**没有降级手段**（不像普通寻路可以绕路，挂接安全位是唯一一条回溯路径）。

### 4.4 三个候选修法（待拍板，先不动码）

- **M-A（治标，推荐先做）**：`FindSafeCouplePositionProc()` 在 `fail=tryReserve` 且冲突位的主人**是活着的挂接型列车（`R3RIsCoupleTarget` 或 `current_order` 是 `OT_GOTO_COUPLE`）且 `cur_speed == 0`** 时，不 `return false`，而是记 `FSCP-WAIT-LIVE` 后按"可通行"返回 true。理由：对面那辆也在等挂接，它一旦挂上就会挪走；把它的预留当成永久障碍只会造出本轮的活锁。**不做**无条件放行（真阻塞必须仍失败，见 KI-221/439 的负判据）。
- **M-B（治根）**：停稳（`cur_speed == 0`）且 `current_order` 是 `OT_GOTO_COUPLE` 的车**不应继续持有行进预留**（对照 KI-208 的 `RESV-NOBOOK` 只覆盖 car-only / `OT_WAIT_COUPLE`）。把闸门扩到"停稳 + `OT_GOTO_COUPLE` + 无可行目标"，让 21 放手，51 自然能过。**风险**：21 一旦挂上目标后需要立刻重订，须确认重订路径存在。
- **M-C（兜底）**：`CPL-SAFE-FAIL` 后置脏，强制下一次挂接寻路走"换目标"分支（本轮 `CPL-SCAN ... acc=0/3` 说明扫描里其实**有** 3 个 accepted 候选，只是回溯路径被 259,233 掐死）⇒ 有备选目标却因为一条回溯路径失败而全盘作废，是最可直接改善的一点。

### 4.5 与既有 KI 的关系（避免重复登记）

| KI | 状态 | 与本轮关系 |
|---|---|---|
| **KI-439** | 高，未修 | 同族（`CPL-SAFE-FAIL`），但那条是**孤儿位**（`othru=0` + `FSCP-REACH-NONE`）；本轮是**活车预留**（`othru=1`）⇒ 它的 F-278-A 修法救不了本条 |
| **KI-438** | 高，未修 | "截断态每 tick 重订长预留"——本轮 21 的 `oend=259,233` 正是这类长预留的产物（间接成因） |
| **KI-429/431/432/433/434/435/436/437** | 部分已修/部分未修 | 截断模式（`PFD-ACCEPT kind=waitwp`）本轮**没有出现**（只有 `kind=waiting`）⇒ 走的是"未进入截断模式"的常规路径，与那批条目的判据面不同 |
| **KI-477（本轮）** | 新增 | U-A 机制坐实：交叉格 `TracksOverlap` + 活车预留 ⇒ 挂接安全位试探失败 ⇒ 整次作废 ⇒ 活锁 |

## 5. 待取证计划（按配额逐组做，做完立刻回填）

- [x] G1：`CPL-SAFE-FAIL` 全集（21 条，`:4119` 一次 veh=99 + 其余 veh=51）—— 见 §4.3
- [x] G2：`COUPLE-OK` 3 次 / `COUPLE-FAIL` 12 次上下文 —— 见 §4.3（三次 OK 都在早期，之后全失败）
- [ ] G3：`CPL-SCAN` 参数分布（`acc`/`noconsist`/`why`）统计，定位 veh=99 类"零接受"是哪个判据淘汰
- [ ] G4：`CPL-PAIR` 来源与 tgt 车属性（veh=75），确认 U-B
- [ ] G5：`LOCO-MOVE`/`SPD-DROP` 对照，确认 U-C/U-D
- [ ] G6：确认 21 的预留为何在 `spd=0` 时仍在（`RP cur=` / `RESV-NOBOOK` 是否命中过 21）

## 6. 结论（当前）

1. **账本删除本身没有回归**：`RESV-AUDIT-STRAY` / `RESV-REAP` 全 0，符合第 303 轮 A 项预期。
2. **读档断言未复发**：6658 行日志无 `vehicle.cpp:3094`（待玩家确认是否用的旧档）。
3. **本轮暴露的是挂接寻路的活锁**（U-A）：两辆 `OT_GOTO_COUPLE` 车在相邻格互相否决，`TracksOverlap` 是合法语义、`CPL-SAFE-FAIL` 是"整次作废"的过强反应，且扫描里本有 3 个 accepted 候选（`acc=0/3`）被一条回溯路径连坐。修法候选 M-A/M-B/M-C 见 §4.4，**未改码**。

## 7. 复测判据（修法落地后）

1. 同场景不再出现连续 20 次 `CPL-SAFE-FAIL veh=51`；出现 `COUPLE-OK` 或至少 `CPL-RESERVE reserved=1`。
2. 负判据：真正的他车挡在预留终点格上（`othru=1` 且 `ospd>0` 或非挂接型）**仍须** `CPL-SAFE-FAIL`，不得放行。
3. `acc>0` 的扫描不再因为单点回溯失败而全盘作废（M-C 生效时同 tick 换目标）。
4. 21/51 中至少一辆恢复移动（`LOCO-MOVE` 出现）。
5. 账本零回归（`RESV-AUDIT-STRAY` 仍 0）。

## 8. 第二轮取证（G3~G6 部分完成，2026-10-25）

### 8.1 G3：`CPL-SCAN` 分布 —— U-B 定性（veh=99 零接受）

实测分布（全 6658 行）：

| veh | 形态 | 判定 |
| --- | --- | --- |
| `51` | `found=1 ... consist=5 rej=2/0/0/0 acc=0/3 why=acc-wait` | **有 3 个 accepted 候选**（`PFD-ACCEPT kind=waiting`），路径也算出来了（`CPL-BEST`），死在安全位回溯的一个交叉格上 ⇒ **单点否决导致整次作废** |
| `21` | `found=0 ... rej=6/0/0/0 acc=0/0 why=GROUP last=278,231 lastveh=24` | 找到过候选但被 **GROUP** 判据否决 ⇒ 21 与 24 的车组掩码不容 ⇒ 21 语义上就不该去挂 24 |
| `99` | `found=0 ... noconsist=52..68 consist=0 acc=0/0 why=none nodes=248..308 closed<=67` | **一个候选都没看到**（`consist=0`），不是被某条判据淘汰；早期（tick≈2907）曾 `consist=3 rej=3/0/0/0 why=GROUP lastveh=75` ⇒ 相对目标 75 被 GROUP 否决，之后连 75 的格都搜不到 ⇒ **U-B = "目标不可达 + 早期还被 GROUP 否决"**，与 U-A 是两个问题 |

⇒ U-B 结论：`veh=99` 不是"零接受 bug"，而是 **(a) 目标 75 的车组掩码不容（GROUP）+ (b) 之后目标移出可达范围**。`noconsist` 增而 `consist` 恒 0 说明搜索树里根本没走到候选所在站台。

### 8.2 G6：`FSCP fail=tryReserve` 的两类冲突源（重要修正 §4.2）

本次把 `FSCP` 与紧随的 `FSCP-RES` 逐行对上，发现冲突位的 owner 有**两种**，此前 §4.2 只写了一种：

| 现场 | 冲突位 | `tbits` / `res` / `want` | `FSCP-RES` | 语义 |
| --- | --- | --- | --- | --- |
| `veh=99`（`:4115-4119` 一带） | `259,233` | 交叉格（`tbits` 含 X 与对角两族），`res` 只有 X，`want` 是**对角/另一族** | `owner=99`、`otile=253,233`、`oend=260,233`、`othru=1`、`cpl=253,233` | **自己挡自己**：owner 车号 == 挂接车车号，冲突位就在它自己预留链的路上 |
| `veh=51`（`:5310-5320` 一带） | `258,234` | 同上形态（`tbits`=X+对角，`res` 只有 X） | `owner=21`、`otile=253,233`、`oend=259,233`、`othru=1`、`cpl=252,234`（=51 所在格） | **前车挡我**：21 停稳（`ospd=0`、`stuck=1`）却仍持有一条穿过该交叉格的预留 |

机制不变（`TryReserveTrack` 第二道闸门 `TracksOverlap(res | want)` 为真），但**冲突源有两类**：
- 一类是挂接车**自己的既有预留**在同一交叉格上订的是另一族轨道 ⇒ 新路要转斜，旧路是直的 ⇒ 自己的陈旧预留挡自己的新路；
- 一类是**前方同走廊的停稳挂接车**（21）仍持有穿过交叉格的预留 ⇒ 互锁。

### 8.3 疑点登记（REG-305-B/C，一发现即落盘）

- **REG-305-B（疑点）**：`veh=21` 在 `SPD-DROP` 行里已 `stuck=1`，按 `TrainLocoHandler` 的 `found=0` 处置应走 `FreeTrainTrackReservation`，但后续 `FSCP-RES` 仍把它报成 owner。期望值＝预留应已释放；实测值＝仍在。⇒ 要么释放没覆盖到那一段（起始 `origin` 与预留链不匹配），要么释放后当 tick 又被重订。**需要一次专门取数**（见 §8.4 F-A）。
- **REG-305-C（疑点）**：`FSCP-RES` 的 `owner` 由 `GetTrainForReservation()` 顺着"连续已订位"走到尽头取车，第 91 轮的更正说它只保证"链上某辆车"，**不保证是订位者**。所以"owner=99"也可能是"99 恰好站在别人订的位上"。⇒ 在未加 `selfthru=` 之前，**不得**断言"自己挡自己"已是定论（§8.2 第一行据此标为待验证）。

### 8.4 修法收敛（三选，仍待拍板）

| 编号 | 内容 | 风险 | 价值 |
| --- | --- | --- | --- |
| **F-A** | `FSCP-RES` 增 `selfthru=`（本车自己的预留是否穿过该格）+ `ostuck=` / `oord=` / `owait=`；**零行为变更** | 极低 | 一次取数就能把 §8.3 两个疑点全部定性，避免再猜 |
| **F-B** | `CPL-SAFE-FAIL` 后强制 `FreeTrainTrackReservation(consist)` 再等下一 tick 重试 | 中（若释放逻辑本身有洞，这里只是补一刀；不影响他车） | 直接解开"自己的陈旧预留挡自己的新路" |
| **F-C** | 停稳的 `OT_GOTO_COUPLE` 车不得持有**向前**预留（与 `RESV-NOBOOK` 同族，但那条只覆盖 `WAIT_COUPLE`/纯车底） | 中（需确认不影响正常进站/挂接的最后一段） | 治本，同时解互锁 |

**建议顺序**：F-A 先（本轮只加探针、可立即编译），拿到 `selfthru` 再决定 F-B/F-C 是否都要。

### 8.5 待玩家拍板（按编号回填即可）

- **C-1**：是否先只加 **F-A** 探针（`FSCP-RES` 增 `selfthru=`/`ostuck=`/`oord=`/`owait=`，零行为变更）再动修法？
  - 选项：`C-1a` 只加探针，等下一份日志再修（稳）／`C-1b` 探针 + F-B 一起上（快）／`C-1c` 探针 + F-B + F-C 一起上（最彻底，风险最高）。
- **C-2**："自己挡自己"若被 `selfthru=1` 证实，是否可以**直接允许**在同一交叉格上"已有本车一族预留 + 想订另一族"时跳过该轨（不 reserve、不改图）？
  - 选项：`C-2a` 允许（我倾向此项）／`C-2b` 不允许，改为先 `FreeTrainTrackReservation` 再重算路径。
- **C-3**：停稳的 `OT_GOTO_COUPLE` 机车是否**一律不持有向前预留**（F-C）？这会改变"机车原地待挂"时的铁轨黑色预留预览显示（可能反而更接近原生）。
  - 选项：`C-3a` 一律不持有（治本）／`C-3b` 只在"本次寻路失败且已 stuck"时释放（最小）。

### 8.6 玩家拍板（2026-10-25）：本轮问题搁置

**玩家原话**：「算了，我决定不对玩家在路点等待前往挂车的行为兜底，也就是说，这个问题搁置」。

⇒ 口径确认：

1. **不为「在路点等待 → 前往挂车」这条行为链做兜底**。§4.3 的活锁现场（21/51/99 在走廊里排队执行 `OT_GOTO_COUPLE`、`CPL-SAFE-FAIL` 刷 20 次）属于该口径之下，**不再修**。
2. §8.5 的三问 C-1/C-2/C-3 **全部作废**，无需再答。
3. §8.4 的 F-A（探针）/F-B（`CPL-SAFE-FAIL` 后强制释放）/F-C（停稳挂接车不持向前预留）**全部冻结，不落码**，也不再编译。
4. 已取证的结论全部保留（U-A 交叉格 `TracksOverlap` 机制、U-B = GROUP 否决 + 目标不可达、REG-305-B/C 两个疑点），只作为记录，供将来撞上同一现场时直接引用。
5. KI-477 状态已同步改为 **已搁置**。

**本轮交付边界**：零源码改动、零构建。累计本轮（第 305 轮）仅有两份文档：本备忘 + `R3R_KNOWN_ISSUES.md` 的 KI-477。

---

## 8.7 §8.6 撤回 + 第三轮取证：与"路点 vs 车站"无关，指向配对锁（2026-10-25）

### 8.7.1 玩家第二次拍板（推翻 §8.6）

**玩家原话**：「不对，我把路点换成了车站，但是同样的问题依旧存在，说明这不是路点的问题，而是挂接寻路本身的问题。我的想法是先只加探针，或者你能推断出来什么也可以跟我说，如果没有可推断的就请你直接上探针了」。

⇒ 据此：

1. §8.6 的「搁置」**作废**，KI-477 状态改回**取证中**（已在 `R3R_KNOWN_ISSUES.md` 同步）。
2. §8.5 的 C-1/C-2/C-3 **解冻**，§8.4 的 F-A/F-B/F-C 解冻。
3. 本轮口径 = **C-1a 变体：只加只读探针、零行为变更**；但已在下面给出三条有证据的推断，故按"有推断就先说、同时上探针"执行。
4. **关键排除**：等待点是 `OT_WAIT_COUPLE` 的车站还是路点，与 `R3REnsureCouplePair()` / YAPF 的四道闸门**没有任何耦合**——这条链里没有任何一处读"等待点类型"。所以"换车站后依旧"是**预期**，不构成新信息；真正的变量是配对锁的建立时机。

### 8.7.2 现场锚点（本次日志，行号取自 `build\R3R_debug.log`）

| 行 | 内容（原文） | 时间 |
|---|---|---|
| `:3684-3686` | `CPL-WHY node=278,234 act=99 tgt=75 ch=99 th=75 pairTgt=1048575 pairReq=1048575 pairOK=0 ignOK=1 aStn=21 tStn=65535 destStn=12 isDepot=0 aStop=0 tStop=0 n=1..3` | 寻路拒绝瞬间 |
| 早期 | `CPL-SCAN tick=2890 ... found=0 closed=81 limit=10000 noconsist=72 consist=0 ... why=none` | tick 2890 |
| 早期 | `CPL-SCAN tick=2906 ... found=0 consist=3 rej=3/0/0/0 why=GROUP lastveh=75` | tick 2906 |
| 早期 | `COUPLE-FAIL loco=99 ...` | tick 2890~2906 |
| 之后 | `CPL-PAIR act=99 tgt=75 ... dist=10`，随后 10→9→8→8 | **tick 2922** |

### 8.7.3 三条推断（有证据，非猜测）

**推断 1：`rej[0]` 的标签 GROUP 是误标，真因是 `pair-mismatch`。**

- `R3RCoupleAllowed()` 只做一件事：`if (!R3RCouplePairMatches(coupler, target)) return false; return R3RCoupleAllowedIgnoringPair(coupler, target);`
- YAPF 侧把 `R3RCoupleAllowed` 的两个失败原因**合并**记进 `rej[0]`，而 `rej[0]` 的名字叫 `GROUP`（`names[4] = {"PFD-REJ-GROUP","PFD-REJ-FIT","PFD-REJ-TARGET","PFD-REJ-ORDER"}`）。
- `CPL-WHY` 直接给出现场读数：`pairOK=0` + `ignOK=1` ⇒ 通过"忽略 pair"的完整检查（dest 站、车组掩码、车底是否纯车底、装货/货种/车数）**全通**，唯一卡住的就是 pair 不匹配。
- ⇒ **§8.1 的 U-B 结论（"21/99 的车组掩码不容"）被推翻**：现场根本没有车组掩码冲突，是配对锁缺失。这是本轮最重要的纠正，避免继续按"车组掩码"方向猜。

**推断 2：配对锁是在寻路失败之后才建立的（时序倒挂）。**

- 寻路失败（`CPL-SCAN tick=2890/2906`、`COUPLE-FAIL loco=99`）在前；
- `CPL-PAIR act=99 tgt=75` 首次出现在 **tick 2922**，比失败晚 16~32 tick；
- ⇒ KI-182 设计原则「订单一开始运行就必须已知目标」在该现场**未成立**：至少存在 16 tick 以上的窗口，机车在跑 `OT_GOTO_COUPLE` 的 YAPF 搜索，但 `r3r_couple_target` / `r3r_couple_requester` 都是 `INVALID`。
- 该窗口内 YAPF 的所有候选都会被 pair 闸门全拒 ⇒ `found=0` / `COUPLE-FAIL`，与"等待点类型"无关。

**推断 3：`tStn=65535` 不是缺陷，是被 `R3RCoupleTargetAtOrderStation` 的空值放行分支吃掉的正常值。**

- `R3RCoupleTargetAtOrderStation()` 的第一行是 `if (!IsRailStationTile(target->tile)) return true;`（目标不在站格 ⇒ 该闸门直接放行）；
- 所以 `tStn=65535` 只是"候选车不在站格上"，此时闸门视为通过，`ignOK=1` 自洽；
- ⇒ 不要再把 `tStn=65535` 当疑点追。

### 8.7.4 为什么锁会晚到 / 会被丢（本轮探针要回答的唯一问题）

`R3REnsureCouplePair()` 的候选漏斗一共 6 个可能吞掉候选的出口 + 1 个"根本没跑"的可能：

| # | 出口 | 判据 | 备注 |
|---|---|---|---|
| 0 | **没扫描** | `_r3r_pair_scan_tick` 8 tick 退避命中 | 现场 tick 2890→2906 只有 16 tick，**最多只够 2 次扫描机会**；若这两次恰好都在退避里，就完全没扫过 |
| 1 | 链头过滤 | `t->First() != t` 或 `t->First() == coupler->First()` | |
| 2 | 非目标 | `!R3RIsCoupleTarget(t)` | 订单不是 `WAIT_COUPLE` |
| 3 | 停驶位 | `t->vehstatus.Test(VehState::Stopped)` | **注意这是 `vehstatus` 的 Stopped 位，不是 `cur_speed==0`**；装货/进站会让它置位 |
| 4 | 目的地站 | `!R3RCoupleTargetAtOrderStation(coupler, t)` | 目标在站格且站不等于订单目的地 |
| 5 | 被他人锁 | `locker != nullptr && !R3RCouplePairOutranks(...)` | 高优先级抢占 |
| 6 | 忽略-pair 检查 | `!R3RCoupleAllowedIgnoringPair(...)` | 再分 `dest` / `group` |

稳态侧还有一条：`partner != nullptr` 但 `Stopped` / `!IsCoupleTarget` / `!AllowedIgnoringPair` 为真 ⇒ **每 tick 丢锁再重扫**，形成"锁一闪一闪"，寻路随机撞在无锁窗口上。

### 8.7.5 本轮探针（只读，零行为变更，只改 `src\train_cmd.cpp`）

刻意**不碰** `src\pathfinder\yapf\yapf_destrail.hpp`——改头文件会触发 KI-183 护栏判全量（692 步 ≈ 16~20 分钟），而现有 `CPL-WHY`（`pairOK`/`ignOK`）已经在头文件里，够用。本轮要补的全在 `train_cmd.cpp` 的配对层：

| 探针 | 位置 | 字段 |
|---|---|---|
| `CPL-PAIRSCAN` | `R3REnsureCouplePair()` 每个出口（含退避早退） | `tick act state order destStn isDepot actTile pool s_chain s_target s_stop s_dest s_lock s_ign s_ignd best bestDist paired wait near nearDist nearWhy nearTile nearOrd nearStop` |
| `CPL-PAIRDROPS` | 稳态判 `partner` 失效、`R3RUnpairCoupleTargets()` **之前** | `tick act tgt why tgtOrd tgtTile tgtStop` |

`state` 取值：`backoff` / `none` / `refused` / `locked` / `steady`。
`near*` = 距离 coupler 最近的**被拒候选**快照（回答"目标明明在，是被谁拦的"）。

### 8.7.6 复测判据

1. **能定死"没扫描"还是"扫了被拦"**：若出现 `CPL-PAIRSCAN ... state=backoff` 且 `wait` 覆盖整段失败窗口 ⇒ 推断 2 的成因是退避；若出现 `state=none` 且 `s_stop>0` / `s_dest>0` ⇒ 是闸门 3/4。
2. `CPL-WHY` 的 `pairOK=0` 必须能与同一 tick 附近的 `CPL-PAIRSCAN state=*` 对上（同一 tick 内不得同时出现 `state=locked`）。若同时出现，说明 `R3RPairCoupleTargets` 写入的锁与 YAPF 读的锁不是同一对（登记新疑点）。
3. `CPL-PAIRDROPS` 若高频出现 ⇒ 锁在抖动，直接指向"每 tick 丢锁"；若为 0 ⇒ 锁稳定，问题只在建立延迟。
4. 负判据：`state=steady` 期间普通挂接必须照常 `COUPLE-OK`，不得因探针改动行为（本轮零行为变更，`diff` 只应有 printf/计数器）。

### 8.7.7 构建自证（第 305 轮续）

- 复用既有入口 `_tmp_inc_build.cmd`（未新建任何 `.cmd`）。
- 护栏：`GUARD: incremental is safe (no header/lang file is newer than the newest object)` —— 本轮只改 `src\train_cmd.cpp`，增量合法。
- 编译：日志 `:521` = `[3/3] Linking CXX executable openttd.exe`。
- 时间链：`src\train_cmd.cpp` 2026-10-25 23:59:43 → `build\CMakeFiles\openttd_lib.dir\src\train_cmd.cpp.obj` 2026-10-26 00:00:21 → `build\openttd.exe` 2026-10-26 00:01:29（52 230 144 B）。
- `build\R3R_incbuild.done` = `EXIT_CODE=0`。
- `read_lints` `train_cmd.cpp` = 0 条。
- 产物自证：`findstr /m CPL-PAIRSCAN build\openttd.exe` 命中、`CPL-PAIRDROPS` 命中。

**状态**：已实现 + 已编译，游戏内复测待做。

### 8.7.8 未确认项（本轮登记，待新日志判读）

- `REG-305-D`：`_r3r_pair_scan_tick` 的 8 tick 退避是否足以吃掉整段失败窗口（现场 2890→2906 只有 16 tick，最多 2 次机会）。判据：`CPL-PAIRSCAN state=backoff` 的 `wait` 是否覆盖 2890~2922。
- `REG-305-E`：`R3RIsCoupleTarget(t)` 为假的候选有多少（订单不是 `WAIT_COUPLE` 的整车会被计进 `s_target`）。若 `s_target` 很大，说明扫描面被稀释，选中延迟。
- `REG-305-F`：`t->vehstatus` 的 `Stopped` 位在装货/进站时是否被置上（若置上则候选整段被 `s_stop` 吃掉，与 `cur_speed==0` 无关）。
- `REG-305-G`：`CPL-PAIR ... dist=0 actTile==tgtTile` 那批（act=21/51/99, tgt=24/75/123）是否为解挂后同格残留导致配对层把"同格另一段"配上；本轮探针的 `actTile`/`nearTile` 可直接判读。

## 8.8 第四轮判读（新日志，2026-10-26 00:07）：确认「扫了被拦」，而且是两把不同的刀

### 8.8.1 输入

- `build\R3R_debug.log` = 466 962 B / 4 317 行 / 2026-10-26 00:07:06。
- 产生者 `build\openttd.exe` = 52 230 144 B / 00:01:29 ⇒ **就是 §8.7 那版探针 exe**，采样有效。
- 时间跨度 tick≈2320~2957（约 26 秒）⇒ **抓得偏短**，两条失败线都没跑到收尾（见 REG-305-I/J）。

### 8.8.2 计数（`Select-String` 全量）

| 标签 | 次数 | 备注 |
|---|---|---|
| `CPL-PAIRSCAN` | 93 | `backoff=34` / `steady=28` / `locked=28` / `none=3` / `refused=0` |
| `CPL-PAIRDROPS` | **0** | 锁一次都没被丢 |
| `CPL-WHY` | 6 | |
| `CPL-SCAN` | 3 | 与 `COUPLE-FAIL` 一一对应 |
| `COUPLE-FAIL` | **1** | 只有 `loco=99` 一次 |
| `COUPLE-OK` | 3 | 全在 tick 2338 早期（99/51/21 各一次） |

⇒ `refused=0`：`R3RPairCoupleTargets()` **从未拒绝**任何候选；`CPL-PAIRDROPS=0`：配对锁**不抖**。§8.6/8.7 的"锁抖动/配对函数拒绝"两个方向可以彻底关掉。

### 8.8.3 关键行（原文锚点）

**第一把刀＝闸门 2（`R3RIsCoupleTarget` 为假：目标还没进 `WAIT_COUPLE`）**

```
:3680 tick=2890 act=99 state=none ord=16 destStn=12 pool=5 s_chain=138 s_target=5
      s_stop=0 s_dest=0 s_lock=0 s_ign=0 best=-1 near=75 nearDist=90
      nearWhy=not-target nearTile=161,235 nearOrd=1 nearStop=0
:3715 tick=2898 act=99 state=none ... s_target=5 ... near=75 nearDist=39
      nearWhy=not-target nearTile=213,235 nearOrd=1
:3825 CPL-SCAN ... consist=3 rej=3/0/0/0 why=GROUP lastveh=75
:3826 COUPLE-FAIL loco=99 order=16
```

`pool=5 / s_target=5` ⇒ 全图只有 5 条候选链头，**5 条全被 `!R3RIsCoupleTarget(t)` 拦掉**（`s_target` 记的是"被这条闸门拒掉的数量"）。
`nearOrd=1` 是决定性读数：最近候选 **75 的 `current_order` 还是 1（`OT_GOTO_STATION`）**，而闸门要求 17（`OT_WAIT_COUPLE`，见 `train_cmd.cpp:2422`）⇒ 它当时还在执行"开到等待点"，**还没进入等待态**。

**32 tick 后锁就建立**（同一目标变成合法）：

```
tick 2922  CPL-PAIR act=99 tgt=75 dist=10 tgtTile=265,234
tick 2940  CPL-PAIR act=99 tgt=75 dist=1  tgtTile=265,234 (actTile=265,233)
:4266 tick=2952 act=99 state=locked best=75 bestDist=5 paired=1
```

⇒ tick 2890~2906 的 `found=0` / `COUPLE-FAIL` 是**目标态未就绪窗口内的假失败**，不是"没有路"。

**第二把刀＝闸门 5（目标已被别人锁住）**

```
:4295 tick=2956 act=51 state=none ord=16 destStn=12 pool=5 s_target=4 s_lock=1
      ... near=75 nearDist=17 nearWhy=locked nearOrd=17
:4304-4306 CPL-WHY node=... act=51 tgt=75 pairTgt=INVALID pairReq=99 pairOK=0 ignOK=1
       aStn=21 tStn=12 destStn=12
:4308 CPL-SCAN ... why=GROUP lastveh=75 ; CPL-SKIP retryIn=8
```

同一 tick 里 75 已是 `order=17`（合法目标），但 `s_lock=1` + `pairReq=99` ⇒ **99 先到先得**，51 整批候选被 pair 闸门拒。这是 KI-182「一目标一请求者」的**设计行为**，不是 bug；但 51 与 99 的 `GOTO_COUPLE` 目的地都是**同一个车站 12**，而该站只有一个等待车底 75 ⇒ **供给不足**，51 在本次抓取内不可能完成订单。

### 8.8.4 结论（直答玩家"是没扫描还是扫了被拦"）

1. **扫了，被拦**。`CPL-SCAN`/`CPL-WHY` 三次全部出现在"扫描已进行"之后，`pool=5` 非 0（不是没找到候选）。
2. **两把不同的刀**：闸门 2（`not-target`，目标态未就绪，`nearOrd=1`）与闸门 5（`locked`，争用，`pairReq=99`）。
3. 逻辑上的先后是 **因 → 果**：正因为目标 75 还在 `GOTO_STATION`（`nearOrd=1`），第一次 YAPF 才 `found=0`；等它 32 tick 后进入 `WAIT_COUPLE` 才建立锁 ⇒ **闸门 2 的窗口就是"锁晚到"的物理来源**，`CPL-PAIRSCAN` 的 `state=none` 与 `CPL-PAIR` 的 `tick=2922` 是同一件事的两端。
4. **不是几何/路点问题**，与"等待点换成车站"无关（代码链里没有任何该读取，本轮读数也不含任何路点信息）。

### 8.8.5 新登记疑点

- `REG-305-H`（新，需下一版探针）：`near=75` 的 `nearTile` 在 8 tick 内从 `161,235` → `213,235`（52 格/8 tick），远超物理速度（8 px/tick ⇒ 0.5 格/tick）⇒ 要么**车号 75 在不同 tick 指向两条不同链**（索引回收），要么 `near` 快照取指有误。**在定论前不要把 tick 2890 的 75 与 tick 2922 的 75 当同一目标**。建议下一版给 `near` 补 `nearFirst=`/`nearSeg=`/`nearAge=` 之类身份位。
- `REG-305-I`（新）：99 在 tick 2940 距目标仅 **1 格**（`actTile=265,233` vs `tgtTile=265,234`）、且持有锁，却**没有** `COUPLE-OK`/`COUPLE-FAIL`，随后 `dist` 1→5→7 继续东行；日志在 tick 2957 结束 ⇒ 无法判定"绕圈回来挂"还是"开过去了"。需更长抓取。
- `REG-305-J`（新）：51 在 tick 2956 之后 `CPL-SKIP retryIn=8`、`spd` 26→46 继续跑，仍 `ord=16`；本次抓取未见收敛。

### 8.8.6 下一轮复测判据（最小集）

1. **必须跑到收尾**：抓取要覆盖 `loco=99` 与 `loco=51` 各自的 `COUPLE-OK` 或 `COUPLE-FAIL`，当前 26 秒不够。
2. `state=none` 时**先读 `nearOrd`**：`=1` ⇒ 目标态未就绪（时序）；`=17` 且 `nearWhy=locked` ⇒ 争用；`=17` 且 `nearWhy` 是别的 ⇒ 出现第三把刀。
3. 负判据：`state=steady` 期间正常挂接仍须 `COUPLE-OK`（本次 3 次 `COUPLE-OK` 全在 tick 2338，说明正常路径未被探针影响）。

### 8.8.7 待玩家拍板（修法口径，本轮未改行为）

- **A（治时序）**：`GOTO_COUPLE` 开始执行时若"没有任何合法目标"，不要立刻 `COUPLE-FAIL` 刷屏，而是保持沉默 + 按 8 tick 退避重扫（当前 `COUPLE-FAIL` 是每 tick 的 YAPF 失败侧打印，与配对层退避不同步）。
- **B（治争用）**：`destStn` 相同的多个请求者，按"目标数量"分配而不是先到先得（需要一个站点等待车底计数）。
- **C（治根）**：让等待方在**开始驶向等待点时就进入 `WAIT_COUPLE`**（而不是停稳后才切），从源头消灭 32 tick 空窗。

## 8.9 玩家两条构想的逐条核定（2026-10-26，纯代码+日志核对，零改动）

**玩家原话**：
> 我对你的推断提疑问，因为我记得我有构想：
> 1，没有符合配对条件的列车就一直原地等待，但是这里列车就直接越过了信号灯
> 2，配对时，遵循先到先得原则

### 8.9.1 构想 2「先到先得」＝ 现状**已实现**（口径＝"先进入订单"，不是"先到达"）

代码锚点 `src\train_cmd.cpp:8383-8389`：

```8383:8389:src/train_cmd.cpp
static bool R3RCouplePairOutranks(Train *challenger, Train *holder)
{
	const uint64_t mine = R3RCoupleEnterTick(challenger);
	const uint64_t his = R3RCoupleEnterTick(holder);
	if (mine == 0 || his == 0) return false;
	return mine < his;
}
```

`R3RCoupleEnterTick()`（`:8367-8374`）= 链头**进入 `OT_GOTO_COUPLE` 那一刻的 `_tick_counter`**（`R3RFindCoupleEnterStamp`）。
⇒ 抢锁判据是 `mine < his`（严格更早进入则夺锁）；**相等或任一未知戳 ⇒ 保持原持有者**（即"看游戏心情"，来源 KI-182 的玩家原话）。
⇒ 所以"先到先得"在语义上是"**先挂上这条订单的人先得**"，不是"先开到车底旁边的人先得"。若玩家要的是后者（几何先到），现状不满足。

日志印证：t2922 `CPL-PAIR act=99 tgt=75` → t2956 `act=51 state=none ... s_lock=1 nearWhy=locked`，`CPL-WHY ... pairReq=99` ⇒ 51 挑战失败，99 保锁，**与构想 2 一致**。

**疑点 REG-305-K（新）**：日志没有打印 `R3RCoupleEnterTick` ⇒ 无法直接证实"99 的进入戳确实早于 51"。若 51 其实更早进入、只是因为戳为 0 或相等而被"保锁"规则挡下，那玩家的构想 2 在**边界情形**下不成立。判据：下一版 `CPL-PAIRSCAN` 增 `entTick=`（本车）与 `nearEntTick=`（被拒候选）两字段。

### 8.9.2 构想 1「没有合格配对目标就原地等待」＝ 现状**只覆盖"到达订单目的地之后"**，途中不覆盖

两处代码事实：

1. `TrainCoupleHandler()` 的"无候选"分支（`src\train_cmd.cpp:14433-14496`）：只有 `R3RCoupleOrderDestinationReached(v)` 为真时才 `return false`（订单**不推进**，原地等，打 `COUPLE-DEST-EMPTY ... (waiting in place, order kept)`）；**未到目的地**时走 `else`（`:14458-14494`），只打 `COUPLE-WAIT-OFFDEST` 心跳，然后**同样 `return false`，但没有任何刹车**。
2. `TrainLocoHandler` 的移动分支（`src\train_cmd.cpp:21904-21935`）：对 `OT_GOTO_COUPLE` **只多写一条 `LOCO-MOVE` 探针**（`:21911-21929`，只读），随后照常 `TrainCheckIfLineEnds(moving_front)` → `TrainController(...)`。整条移动分支里**没有"无配对目标 ⇒ 停车/不预留"的判据**；唯一的停车点在 `:21939-21944`（`TrainCoupleHandler` 返回真 = 已经挂上了）。

⇒ **现状语义**：`GOTO_COUPLE` 机车把"配对"当作**到达目的地之后**才发生的事；途中它就是一个普通"开往目的地"的列车，照常寻路、预留、按信号行驶。所谓"没有合格目标"只影响目的地之后的等待，不影响途中。
⇒ 这正是本日志的表现：`act=99` 在 t2890 起 `found=0`（挂接目标搜索失败），却仍在 t2890→t2955 从 `249,233` 一路东行到 `271,233`（`TRP ok=1 res=1` 有预留）。

### 8.9.3 关于"越过了信号灯"的定性（两个层面要分开）

- **合法性层面：不是闯红灯。** 日志里该车的普通路径预留是成功的（`TRP ok=1 res=1`，`dontReserve=0`），它是**按预留通过**信号灯，不是无预留强闯。`found=0` 指的是 **YAPF 的挂接目标搜索**（`FindNearestCoupleTrain`）没找到可挂的目标，与"前进路径是否合法"是两套寻路。
- **与玩家构想层面：确实不符。** 玩家要的是"没有合格目标就原地等"，现状是"开到目的地再等"。这不是实现 bug，而是**设计口径没有覆盖"途中"这一段**。

### 8.9.4 修法口径（待玩家拍板，本轮零改动）

| 口径 | 行为 | 代价 / 风险 |
|---|---|---|
| **A（现状）** | 途中照常开往订单目的地，到点后无车底才原地等 | 与构想 1 不符；但保证"目的地车底还没就绪"时仍能抵达 |
| **B（构想 1 原样）** | 任何时刻扫不到合格候选就立刻停车等（含途中，含信号灯前） | 会退化成"车底稍晚就绪 ⇒ 机车在荒郊野岭停死"；且需解决"停下后如何重新起步"（现无此路径，KI-348 已证 `cur_speed==0` 时移动分支根本不执行 ⇒ 必须由 `TrainCoupleHandler` 主动推） |
| **C（中间）** | 途中**有**候选 ⇒ 减速/停车等配对；途中**完全没有**候选 ⇒ 继续开往目的地（＝现状 + 提前停车） | 改动集中在移动分支的一处判据；仍需复用 `R3REnsureCouplePair` 的配对锁判定，避免"有候选但已被别人锁住"时误停 |

**建议**：若玩家的本意是"不要在没目标时越过信号灯往前冲"，选 **C**；若要严格 B，必须先补"停车后的重启路径"（否则会引入新的死锁，与 KI-348 同类）。

### 8.9.5 本轮取证清单（行号锚点）

| 结论 | 锚点 |
|---|---|
| 抢锁＝按进入订单的 tick 严格更早 | `src\train_cmd.cpp:8383-8389`、`R3RCoupleEnterTick` `:8367-8374` |
| 无候选且**已到**目的地 ⇒ 原地等、订单保留 | `src\train_cmd.cpp:14439-14457` |
| 无候选且**未到**目的地 ⇒ 只打心跳、照常走 | `src\train_cmd.cpp:14458-14495` |
| 移动分支对 GOTO_COUPLE 只加探针、不判"无目标停车" | `src\train_cmd.cpp:21904-21935` |
| 唯一停车点＝已挂上 | `src\train_cmd.cpp:21939-21944` |

## 8.10 玩家追问的核定（2026-10-26）：不是"到站才配对"，而是"它根本没在目的地停"

**玩家原话**：
> 不是这样吗，GOTO COUPLE 命令的前面会搭配一个 GOTO STATION（后期就不让它前面有 GOTO WAYPOINT 了），那你的意思是不是现状是到了 STATION 才会开始尝试配对吗

### 8.10.1 三个动作要分开答（这是 §8.9.2 表述不够精确的地方，此处修正）

| 动作 | 是否"到站才开始" | 锚点 |
|---|---|---|
| **扫描候选 + 尝试挂接** | **不是**，从订单开始执行的第一个 tick 就在做 | `:3680 tick=2890 act=99 ... pool=5 s_target=5`，此刻 99 还在 `249,233`，离目的地 `265,234` 还有 16 格 |
| **原地停住等车底** | **是**，只在 `R3RCoupleOrderDestinationReached()` 为真后成立 | `:14439-14457`（判据 `:14205-14215`：库格按 depot id、站格按 `HasStationTileRail && GetStationIndex==ToStationID`） |
| **按信号正常行驶** | 全程 | 移动分支 `:21904-21935` 对 `OT_GOTO_COUPLE` 只加只读探针；`TRP ok=1 res=1` 说明是按预留通过，不是闯红灯 |

⇒ 玩家的直觉"到 STATION 才配"**只有中间那一项成立**；扫描是全程的。这也解释了为什么 `found=0` 会在路边反复出现——那是"沿途每 tick 扫、扫不到合法目标"，不是"还没到站所以没扫"。

### 8.10.2 本轮真正抓到的问题：99 越过了自己的配对目的地（REG-305-I 由"无法判定"升为"已实证"）

目的地换算（`TileIndex` 在本图 `MapLogX=11`，即 2048 宽）：

```
4294967295-... 不适用；直接用
dtile=479497  →  X = 479497 & 2047 = 265，Y = 479497 >> 11 = 234  ⇒ 目的地 = 265,234
```

`265,234` 同时是日志里 `CPL-PAIR act=99 tgt=75 tgtTile=265,234` 的位置，即车底 75 所在格 ⇒ **99 的 `GOTO_COUPLE` 目的地就是存放车底的站台格**，与玩家"前面配一个 GOTO STATION"的说法一致。

但轨迹是**一路向东穿过它**：

```
4118 LOCO-MOVE veh=99 tile=260,233 j=193 spd=30 track=01 dir=1 db=1 stuck=0 dtile=479497
4151 LOCO-MOVE veh=99 tile=263,233 ...
4198 LOCO-MOVE veh=99 tile=265,233 j=211 spd=30 ...   ← 已到目的地西侧 1 格
4210 LOCO-MOVE veh=99 tile=266,233 ...                 ← 越过了
4275 LOCO-MOVE veh=99 tile=270,233 ...
4285 LOCO-MOVE veh=99 tile=271,233 j=201 spd=30 ...    ← 日志结束仍向东
```

`stuck=0`、`spd=30`、`dir=1`、**`db=1`（DrivingBackwards 倒车）** ⇒ 不是被卡住，是**主动开过去**了。

另有一处真的停过，但停下原因不是"等配对"：

```
3840-3845 CPL-S0-CHK veh=99 tile=253,233 real=8 order=16 stuck=1 fp=0 prog=0
          wait=0..5 parked=0 stop=0 track=01 dtile=491
```

`stuck=1` ⇒ 那一次是**预留失败被标 Stuck**（KI-348 同族），`wait_counter` 在涨但 `stop=0`，与"到点等车底（`COUPLE-DEST-EMPTY ... waiting in place`）"是两回事。

### 8.10.3 猜测根因（待验证，REG-305-L）

`R3RCoupleOrderDestinationReached()` 取的是 `const Train *front = v->GetMovingFront();`（`:14209`），而 `GetMovingFront()` 在 `db=1` 时返回链的 **Last()**（按 `VehicleFlag::DrivingBackwards` 选端）。日志里 99 的 `db=1` 恒定 ⇒ 判据用的是"倒车方向意义上的前端"，若真实车头已进入站台格而 Last() 仍在站外，`HasStationTileRail(tile) && GetStationIndex(tile)==station` 判假 ⇒ **不倒车时到点、倒车时永远判不到点** ⇒ 于是继续按普通寻路向 `dest_tile` 行驶并冲过目的地。

**待验证判据**：
1. 打印 `db`/`GetMovingFront()->index`/`front->tile` 与 `ToStationID`，确认在 265,233 / 266,233 时 front 的 tile 是否仍在站外。
2. 若 265,234 是站台格，检查 `HasStationTileRail(265,234)` 是否为真（站台格应真）。
3. 负判据：`db=0` 的机车在同类场景应能正常到点并打 `COUPLE-DEST-EMPTY`。

### 8.10.4 附带疑点（REG-305-M）

同一 `veh=99`、同为 `order=16`，`dtile` 在行 3840 是 `491`、行 4118 起是 `479497`（=265,234）。两次之间订单目标被重算过一次，或 3840 那次打的是尚未写好的旧值。需确认 `dest_tile` 的写入时机，排除"先指向错误格再纠正"。

### 8.10.5 对修法口径的影响

- §8.9.4 的 **A（维持现状：开到目的地再等）** 这个选项**前提不成立**——现状不是"开到目的地再等"，是"倒车时到不了目的地"。
- 修法应先做 **D（治到点判据）**：`R3RCoupleOrderDestinationReached()` 改成同时接受"任意一节车身在目标站台/库格内"（或至少对 `db=1` 取 `First()` 一侧），再谈 B/C。
- 玩家的构想 1 在这条线上**依然有效**：只要到点判据成立，`COUPLE-DEST-EMPTY` 的原地等待就会接管；现在被"判不到点"整段绕开了。

## 8.11 玩家给出的"经典格式规格"核定（2026-10-26，纯核对，零改动）

**玩家原话**：
> 对于一个经典格式：前往等待区车站->前往作业车站挂接，列车应当在等待区车站停好停稳，然后再跳到前往挂接命令，再开始扫描配对，直到符合条件的列车抵达作业车站停稳，才配对成功，列车才启动前去挂接。请问我描述的有无问题，你发现实测中这个怎么脱钩了

### 8.11.1 规格拆成 5 步，逐条对现状

| 步骤 | 玩家规格 | 现状 | 判定 |
|---|---|---|---|
| 1 | `GOTO_STATION` 等待区车站 | 正常 | 一致 |
| 2 | **停好停稳** | 到站判据成立的那个 tick 就推进订单，停稳窗口 = 0~1 tick | **部分脱钩**（无"停稳等待"窗口） |
| 3 | 跳 `GOTO_COUPLE` 后**才开始扫描** | 是的，订单第一个 tick 就扫（`tick=2890 act=99 ... pool=5`，此时还在 `249,233`） | **一致** |
| 4 | 等"符合条件列车抵达作业车站**停稳**" | 扫描全程在扫，但判据不含"对方已停稳"这一条（见 8.11.3） | **部分脱钩** |
| 5 | **配对成功才启动前去挂接** | **一进命令就开车，边开边扫** | **严重脱钩（根本分歧）** |

### 8.11.2 根因：`GOTO_COUPLE` 在代码里被明文定义为 **travel order**（行驶型），不是等待型

```15849:15859:src/train_cmd.cpp
			case OT_GOTO_STATION:
			case OT_GOTO_WAYPOINT:
			case OT_GOTO_COUPLE:
				/* R3R: GOTO_COUPLE is a travel order. It must be selectable
				 * when advancing, e.g. after a waypoint/station order is
				 * completed. Without this case it fell into the default
				 * branch and was skipped, so the locomotive jumped straight
				 * to the order after the GOTO_COUPLE (e.g. GOTO_DEPOT),
				 * failed to find a path and got stuck permanently. */
				this->v->current_order = *order;
				return UpdateOrderDest(this->v, order, 0, true);
```

⇒ 上一行 `GOTO_STATION` 完成的**同一个 tick**，`SwitchToNextOrder()` 就把 `current_order` 换成 `GOTO_COUPLE` 并调 `UpdateOrderDest()` 写目的地；下一个 tick `ProcessOrders` → `TryPathReserve` 立刻为"作业车站"寻路 ⇒ **车就开了**。玩家设想的"停稳 → 待命 → 等对方 → 才启动"在现状里不存在这段等待态。

配套的另一条（方向相反的守卫）：

```15907:15907:src/train_cmd.cpp
		if (v->current_order.IsType(OT_GOTO_DEPOT) || v->current_order.IsType(OT_GOTO_COUPLE)) return;
```

`AdvanceOrdersFromVehiclePosition()` 对 `GOTO_COUPLE` **直接 return**，即"车到了目的地也不推进订单"——完成权归挂接逻辑。所以现状是「**必须开过去**（它靠开过去才挂得上）+ **到了不推进**（等挂上）」，两头都不是"等待"。

附带一条历史证据：同一段 `:15910-15922` 是第 286 轮 KI-453 的修复注释，说的是**同样的病在路点方案上发过一次**：

> 原生判据 `HasStationTileRail(moving_front->tile)` 在运动前端刚进格那一 tick 就成立，于是 `OT_GOTO_WAYPOINT` 立刻被推到 `OT_GOTO_COUPLE`：`R3RTrainStopsAtWaypoint()` 当场变假，look-ahead 里「在路点格末以 0 速停住」这个唯一目标随之消失，列车在路点格内重新加速（第 285 轮现场 55 → 19 → 30 → Stuck 硬刹）。

⇒ 玩家说"后期就不让它前面有 `GOTO_WAYPOINT` 了"，正是因为这条路点当等待区的方案踩过这个坑；换成 `GOTO_STATION` 后，同样的"进格即推进"问题在站台侧仍然存在（站台判据不同，但**推进的即时性一样**）。

### 8.11.3 "对方停稳"这一条判据——现状没有

`R3RIsCoupleTarget()`（`src\train_cmd.cpp:1879` 一带）判的是**订单类型**：目标当前订单必须是 `OT_WAIT_COUPLE`。
`R3RCoupleAllowed()` / `R3RCoupleGroupMasksAllowCrossCompany()` 判公司、分组、装载、货种、车数。
**没有任何一处判"目标车底 `cur_speed == 0`（已停稳）"。**

⇒ 玩家规格第 4 步"直到符合条件的列车抵达作业车站**停稳**"——现状只要对方订单进了 `WAIT_COUPLE` 就认，可能对方还在滑行进站（KI-477 本体讨论的 32 tick 空窗就是这个：等待方要停稳后才切 `WAIT_COUPLE`，反过来若已切但仍在滑行，挂接方会算它合法）。

**疑点 REG-305-N（新）**：需要取数确认"目标已被判合法但 `cur_speed != 0`"是否存在。判据：`CPL-PAIR` 行补 `tgtSpd=`；若出现 `tgtSpd>0` 却 `s_target=1`，则规格第 4 步确实脱钩。

### 8.11.4 第 5 步的实测脱钩证据（上一轮已取，复用）

```
4118 LOCO-MOVE veh=99 tile=260,233 j=193 spd=30 track=01 dir=1 db=1 stuck=0 dtile=479497
4198 LOCO-MOVE veh=99 tile=265,233 j=211 spd=30 ...   ← 已到目的地（265,234）西侧 1 格
4285 LOCO-MOVE veh=99 tile=271,233 j=201 spd=30 ...    ← 日志结束仍向东
```

`dtile=479497` 按本图 `MapLogX=11` 换算 = `265,234`。全程 `spd=30`、`dir=1`、`db=1`、`stuck=0`
⇒ **`GOTO_COUPLE` 期间一直在行驶**，完全不是"停着等配对成立才启动"。

并且它不是没到过目的地：`265,233` 距 `265,234` 仅 1 格，却没停 —— 因为 `R3RCoupleOrderDestinationReached()` 取 `GetMovingFront()`（`:14209`），`db=1` 时返回链尾 ⇒ 判不到点（REG-305-L）。

### 8.11.5 玩家描述本身的三处需要澄清（这是"有无问题"的答复）

1. **"等待区车站"与"作业车站"是否同一个站？**
   日志里 99 的两条命令目的地是同一个（`tStn=12 destStn=12`）。若两站相同（只是不同站场/站台），配对成立后几乎原地起步，风险小；若两站不同，配对成立后还要走一段路并**重新寻路 + 重新预留**，途中可能被别人占道，失败率比"提前开到位"高——这是现状选择"边开边等"的工程理由。

2. **"停稳 → 跳命令 → 等对方"期间挂接方是静止的，那"启动"必须由谁发起？**
   现状 `cur_speed == 0` 时 `TrainLocoHandler` 的移动分支根本不执行（KI-348 已实证：`j < adv_spd` 恒真，`TrainCheckIfLineEnds`/`TrainController` 永不调用）。所以"配对成立 → 启动"这一下**必须由 `TrainCoupleHandler` 主动发起寻路**，不能指望 `ProcessOrders` 自动跑起来。这是必须新写的路径，否则停下就起不来。

3. **会不会死锁？** 现状的不对称是刻意的：`OT_WAIT_COUPLE` = 被动等待方（原地、不寻路、第 127 轮 `RESV-NOBOOK` 已封它的订路），`OT_GOTO_COUPLE` = 主动接近方。若按玩家规格把 `GOTO_COUPLE` 也做成"等待型"，双方就都停着不动 ⇒ **死锁**。只有当"对方不需要挂接方就位也能抵达并停稳"（对方由别的排程/是 `WAIT_COUPLE` 被动方）时才不死锁——即玩家的规格**只在"对方是被动方"时成立**。

### 8.11.6 改法（待玩家拍板，本轮零改动）

| 口径 | 行为 | 改动面 / 风险 |
|---|---|---|
| **D（玩家的规格）** | `GOTO_COUPLE` 进入后若 `cur_speed == 0`（在等待区已停稳）⇒ 不寻路、原地待命；扫到"合法目标 + 目标已停稳"⇒ 才 `TryPathReserve` 开往作业车站 | 要改 3 处：①`UpdateOrderDest`/`TryPathReserve` 前加"待命"门；②`TrainCoupleHandler` 里补"配对成立 ⇒ 主动发起预留"（解决 KI-348 起不来）；③`R3RIsCoupleTarget` 增"目标已停稳"判据。风险＝待命期间占着等待区站台/占用预留、且若对方永不到位会一直占位（需超时兜底，可复用 `KI-193` 的 500 tick 计数器） |
| **E（现状 + 补 8.11.3）** | 仍边开边等，但候选判据补"目标已停稳" | 改动小；只解决第 4 步，不解决第 5 步 |
| **F（现状 + 修 REG-305-L）** | 只修 `GetMovingFront()` 到点判据，让它真能停在作业车站等 | 最小改动；能得到"至少会在目的地停住等"，但仍是"先开过去再等"，不是玩家规格 |

**建议次序**：先 F（已实证的 bug，必须修）→ 再 E（判据补全）→ 最后 D（口径变更，需玩家明确"等待区与作业站是否同站"和"超时兜底"两件事）。

## 8.12 玩家拍板（2026-10-26）：第 2 步撤回，第 4、5 步为硬要求

**玩家原话**：
> 好，关于你的五个步骤，第二个我承认我有描述错误，但是，我强调，第四步必须停稳且符合条件，第五步必须先有配对再起动

### 8.12.1 口径定稿（不再讨论，按此实现）

| 步骤 | 玩家口径 | 状态 |
|---|---|---|
| 2「停好停稳才跳命令」 | **撤回**（玩家承认描述有误） | 不再作为约束；现状"到站同 tick 推进"可保留 |
| 4「对方抵达作业车站**停稳**且符合条件」 | **硬要求** | `R3RIsCoupleTarget()` 必须增加`目标 `cur_speed == 0`` 判据（及整链停稳） |
| 5「**先有配对再起动**」 | **硬要求** | `GOTO_COUPLE` 进入后若无配对 ⇒ **不寻路、不动**；配对成立后才发起寻路 |

⇒ §8.11.6 的三选项表作废，**只走 D 的 4+5 两条**，不含第 2 步的改动。

### 8.12.2 两条要求的代码落点（初步，行号待本轮取证确认）

**要求 4（停稳）**：候选闸门 `R3RIsCoupleTarget(target)`（`src\train_cmd.cpp:1879` 一带，现判据＝`IsType(OT_WAIT_COUPLE)` + 公司/分组/装货/货种/车数）新增：
- `target->cur_speed == 0`（该节已停）
- 更严口径：目标链**任一节**仍在移动即不算（`for (w = target->First(); ...) if (w->cur_speed != 0) return false;`，成本＝O(链长)，仅在 YAPF 候选评估时调，可接受）
- **副作用**：对方进站滑行期间 `found=0`，挂接方继续等——正是玩家要的行为。

**要求 5（先配对再启动）**：`GOTO_COUPLE` 订单进入时（`SwitchToNextOrder` → `UpdateOrderDest`，`src\train_cmd.cpp:15858-15859`；以及 `ProcessOrders` 的 `OT_GOTO_COUPLE` 分支）必须有一个**"未配对即不寻路"的门**：
- 位置优先选 `TryPathReserveWithResultFlags()` 入口（第 127 轮 KI-208 的 `RESV-NOBOOK` 门禁就在这，已有先例与探针），条件 `current_order.IsType(OT_GOTO_COUPLE) && !R3RHasPairedCoupleTarget(v)` ⇒ 直接 `return TPRRF_NONE`（不写预留、不标 Stuck）
- 配对成立点（现 `TrainCoupleHandler` 内 `CPL-PAIR` 打出处 / `R3RPairCoupleTargets`）之后，**必须主动发起一次寻路**——否则 `cur_speed==0` 时移动分支不执行（KI-348），车永远不动。

### 8.12.3 必须解决的三个工程问题（不解决就是死锁）

1. **"启动"谁发起**：`cur_speed == 0` ⇒ `TrainLocoHandler` 的移动分支（`TrainCheckIfLineEnds`/`TrainController`，唯一订路点）永不执行（KI-348 已实证）。所以配对成立那个 tick 必须由 `TrainCoupleHandler` 主动调用一次寻路/预留，不能等 `ProcessOrders` 自己跑。
2. **配对是"逻辑选定"还是"物理接近"**：玩家语义＝先逻辑选定（`CPL-PAIR` 锁目标），再行驶去贴。所以 `R3RPairCoupleTargets` 的时机要前移到"扫到合法目标且目标停稳"那一刻，而不是等机车开到旁边。
3. **超时兜底**：配对目标若被别的事件（卖掉/开走/解挂）作废，需自动解除配对并回到"重新扫描"状态，否则永久静止。可复用 KI-193 的 500 tick 计数范式。

### 8.12.4 待取证项（本轮）

- [x] `R3RIsCoupleTarget()` 完整体：**确认现无 `cur_speed` 判据**（见 8.12.5）
- [x] 配对 API：`R3RGetCouplePairPartner()` O(1) 查询；`R3REnsureCouplePair()` 独立于寻路（见 8.12.5）
- [x] 寻路门禁形态：`R3RIsParkedWaiting()` + `RESV-NOBOOK`（见 8.12.5）

### 8.12.5 取证结果（2026-10-26，三组工具调用，零改动）

**① 第 4 步（对方必须停稳）：判据确实完全没有**

```2388:2423:src/train_cmd.cpp
bool R3RIsCoupleTarget(const Train *t)
{
	if (t == nullptr || !t->IsSegmentFront()) return false;
	...
	if (Train::From(t->First()) != t) return false;
	...
	if (R3RChainHasRealArticPart(t)) return false;
	return t->current_order.IsType(OT_WAIT_COUPLE);
}
```

四条判据：段头、真链头、不含真 artic、订单是 `OT_WAIT_COUPLE`。**没有任何速度判据** ⇒ 对方只要订单切到 `WAIT_COUPLE` 就算合法，哪怕它还在滑行进站。
⇒ **落点＝`:2422` 之前插一条"整链停稳"判据**。注意这是 YAPF 候选评估的热点，建议 O(1) 近似＝`t->cur_speed == 0`（链头即被 tick 的那节，其它节速度由链头同步），必要时再加深链遍历。

**② 第 5 步（先配对再启动）：基础设施全在，只是门的**位置**恰好相反**

```17157:17179:src/train_cmd.cpp
static bool R3RIsParkedWaiting(Train *consist, const char **reason)
{
	if (consist->cur_speed != 0) return false;            // ← ①只有已停下的车才进门

	if (consist->current_order.IsType(OT_WAIT_COUPLE)) { *reason = "parked-waiter"; return true; }
	if (R3RIsCarOnlyFormation(consist))               { *reason = "parked-caronly"; return true; }

	if (consist->current_order.IsType(OT_GOTO_COUPLE) &&
			R3RGetCouplePairPartner(consist) == nullptr &&
			R3RCoupleOrderDestinationReached(consist)) {   // ← ②要求"已到目的地"
		*reason = "parked-no-target";
		return true;
	}
	return false;
}
```

⇒ 现状语义被这两条钉死为「**先开到目的地，再等配对**」：
- ②`R3RCoupleOrderDestinationReached` 使门只在到站后生效 ⇒ 途中一路正常寻路（正是 99 越过 `265,234` 的原因）；
- ①`cur_speed != 0` 使门对"还在动的车"完全无效 ⇒ 刚推进到 `GOTO_COUPLE` 那一刻拦不住它，车已经在加速了。

**配对扫描独立于寻路，这是第 5 步能成立的先决条件**：
- `R3REnsureCouplePair(Train*)` 定义 `:13421`，调用点 `:21116`、`:21192`，位于 `TrainLocoHandler()` 内、**早于**寻路/预留（`:17209` 注释自陈 "在 TrainLocoHandler() 里独立运行，早于预留"）；
- 查询 O(1)：`R3RGetCouplePairPartner(v)`（`:17172`/`:17219`/`:13709` 均用它判"是否有锁"）。

⇒ 车停着不动也能每 tick 扫全图并建锁，所以"停住等配对"不会卡死（前提是配对成立后要主动唤醒寻路，见 8.12.3 第 1 条）。

**③ 门禁模板＝第 127 轮 KI-208 的 `RESV-NOBOOK`**（`:17267-17277`）：

```17267:17277:src/train_cmd.cpp
	const char *r3r_nobook_reason = nullptr;
	if (R3RIsParkedWaiting(consist, &r3r_nobook_reason)) {
		R3RLogReserveAttempt(consist, mark_as_stuck, first_tile_okay, TPRRF_NONE);
		if (_r3r_waiter_nobook_logged.insert(consist->index).second) {
			R3RDbgWrite("RESV-NOBOOK veh=%d ... reason=%s\n", ...);
		}
		return TPRRF_NONE;
	}
```

只拒绝**路线预留**：不碰 `ReserveTrackUnderConsist()`（车底脚下保护）、不碰既有预留、不碰 `TrainCoupleHandler()`。

**④ 最小改动方案（两条，互不重叠）**

| 要求 | 改动 | 风险 |
|---|---|---|
| 第 4 步 | `R3RIsCoupleTarget()` `:2422` 前加 `if (t->cur_speed != 0) return false;` | 对方进站滑行期 `found=0`（预期行为）；负判据＝对方停稳后必须恢复可锁 |
| 第 5 步 | `R3RIsParkedWaiting()` 的第三条分支**去掉 `R3RCoupleOrderDestinationReached(consist)`**，并把该分支移到 `:17159` 的 `cur_speed != 0` 早退**之前**（作为独立 return，不受车速门限制） | 车会停在"收到命令时的位置"（可能不在等待区车站）；需补"配对成立后主动唤醒寻路"（KI-348）与"锁失效自动回退扫描"（可复用 KI-193 的 500 tick 计数器） |

**⑤ 第 5 步的两个必答工程问题（不答就是死锁或静止 bug）**

1. **配对成立后谁发起第一次寻路**：`cur_speed == 0` ⇒ 移动分支不执行（KI-348）⇒ 必须在 `R3RPairCoupleTargets()` 成功处（`:13633`）主动调一次 `TryPathReserve`，否则锁建立了车也不动。
2. **锁失效**：目标被卖掉/被别的机车先挂走/订单被跳过后，`R3RUnpairCoupleTargets()` 已经有一堆调用点（`:10098/:12450/:13433/:13544/:14443/:14574`），但要确认"锁被清后回到未配对态、门重新拦、扫描重新开始"这一圈闭合，否则会出现"锁没了但车已经在半路冲出去"。

### 8.12.6 实现记录（2026-10-26，玩家「好，可以按照你推荐的来修」之后落地）

只改 `src\train_cmd.cpp` 一个文件（未碰任何 `src\*.h`），六处编辑全部围绕两条硬要求 + 一个此前埋着的判据 bug。

| # | 位置（当前行号） | 改了什么 | 为什么 |
|---|---|---|---|
| ① | `R3RIsCoupleTarget()` `:2388`，插在 `:2435` 的 `return t->current_order.IsType(OT_WAIT_COUPLE);` 之前 | 新增 `if (t->cur_speed != 0) return false;` | **第 4 步**：原四条判据（段头／真链头／不含真 artic／订单类型）**没有任何速度判据** ⇒ 对方订单一切到 `WAIT_COUPLE` 就算合法目标，哪怕它还在滑行进站。取 O(1) 的链头 `cur_speed`（这是 YAPF 候选评估热点，不能深链遍历）。 |
| ② | `R3RCoupleOrderDestinationReached()` `:14235` | 由「只看 `GetMovingFront()->tile`」改为**整链遍历**（`for (u = v->First(); u; u = u->Next())` 逐节比 station id / depot id） | **REG-305-L 的根因**：`DrivingBackwards` 时 `GetMovingFront()` 返回**链尾** ⇒ 倒车链的"到点"判据恒假 ⇒ KI-193 的原地等待/500 tick 兜底全部失效（veh=99 一路开到 `271,233`）。仍按 station/depot **id** 比（不比 tile），故与站台多格、进站方向、db 无关；成本 O(链长)，只在 `TrainCoupleHandler()` 与预留门里被问。 |
| ③ | `R3RIsParkedWaiting()` `:17212` | 新增**首个分支**：`OT_GOTO_COUPLE && R3RGetCouplePairPartner()==nullptr && !IsRailDepotTile(tile)` ⇒ `reason="parked-no-pair"; return true;` —— 位于 `cur_speed != 0` 早退**之前** | **第 5 步**：旧第三支（`parked-no-target`）既要求"已到目的地"又被速度门挡住，语义被钉成「先开到目的地，再等配对」。提到速度门之前 ⇒ 一进 `GOTO_COUPLE` 就拦；删掉目的地条件 ⇒ 不必先跑过去。 |
| ④ | 同上函数文档注释 | `parked-no-target` 段改写为 `parked-no-pair`，写明「在速度门之前判定／不再要求目的地／拒路后 `GetCurrentMaxSpeed()` 归零自行刹停／配对扫描不经预留故不死锁」＋车库内豁免的理由 | 旧注释与新语义不符，留着会误导下一轮判读。 |
| ⑤ | `R3REnsureCouplePair()` 配对成功处，`r3r_ps_report("locked", ...)`（`:13656`）与 `_r3r_pair_scan_tick.erase(...)` 之后，`:13672` | `if (coupler->cur_speed == 0) TryPathReserveWithResultFlags(coupler, false, false);` | **KI-348 的必然配套**：第 5 步把车按在 `cur_speed == 0`，而 `j < adv_spd` 停驶分支永不调 `TrainController()` ⇒ 锁建立了车也起不来。这里踢一脚订路（`GetCurrentMaxSpeed()` 随之非 0，同 tick 的 `UpdateSpeed()` 就加速）。失败**不** `MarkTrainAsStuck`（否则更起不来）。`TryPathReserveWithResultFlags` 在 `src\train.h:104` 有声明，调用点在其定义（`:17304`）之前也能编。 |
| ⑥ | D-6b 门注释（`:17357`–`:17379`，**代码未动**） | 说明该门现已被上游 `parked-no-pair` 覆盖，只对**车库格**还有独立作用；日志口径改为 `RESV-NOBOOK ... reason=parked-no-pair`，`CPL-WAIT-NOBOOK` 不再出现 | 不删代码（库内预留语义保守），但必须让下一轮知道"没有 `CPL-WAIT-NOBOOK`"不等于该门没生效。 |

**为什么第 5 步不会死锁（三条互锁的判据，都已核对）**：
1. 配对扫描 `R3REnsureCouplePair()` 在 `TrainLocoHandler()` 内、早于预留，且不经 `TryPathReserve*` ⇒ 车停着也每 tick 扫全图建锁；
2. `R3RShouldActAsWaitingForCouple()`（D-6b，`:17302`）与 `R3RShouldHoldForCouple()`（`:13739`）都带 `R3RGetCouplePairPartner(v) != nullptr ⇒ 放行` ⇒ 锁一建立，所有 NOBOOK 门立刻失效；
3. 尾部 `:13672` 主动订一次路，绕开 KI-348。

**顺带核对的 4 个调用点（③ 的整链语义放大后无回归）**：
- `:14328` 空闲限速门：倒车链以前恒"未到点"⇒ 每 tick 全扫（贵），现在按原意降频到每 `R3R_COUPLE_DEST_IDLE_LIMIT` tick；配对扫描不在此函数内，不受影响；
- `:14391` `r3r_off_dest`：到点后不再受 `R3R_COUPLE_RETRY_BACKOFF_MAX` 节流，只是 9 格触摸扫描跑得勤一点；
- `:14484` `COUPLE-DEST-EMPTY`：倒车链现在能正确进入"原地等待 + 清过期锁"，不再误走 `else` 分支；
- `:14598` `COUPLE-COMMIT-FAIL` 计数：倒车链现在能计数并在 `R3R_COUPLE_COMMIT_FAIL_LIMIT` 后兜底推进，不再永久卡在坏挂接上。

**构建自证**：复用既有 `_tmp_inc_build.cmd`（未新建任何 `.cmd`），`tasklist` 确认 `openttd.exe` 未运行；护栏 `GUARD: incremental is safe`；日志尾部 `[3/3] Linking CXX executable openttd.exe`；`build\R3R_incbuild.done` = `EXIT_CODE=0`（2026-10-26 01:01:00）；`error C|fatal error|FAILED:|build stopped` 计数 **0**；`src\train_cmd.cpp` 00:58:31 → `build\openttd.exe` **01:00:50（52 237 824 B）**；`read_lints` 0 条；`findstr parked-no-pair build\openttd.exe` 命中且与 `parked-waiter`/`parked-caronly` 同处常量池、紧邻 `RESV-NOBOOK veh=%d ... reason=%s` 格式串。

**状态**：已实现 + 已编译；游戏内复测待做（判据见 §8.12.7）。

### 8.12.7 复测判据（6 条）

1. **第 4 步（正）**：等待车底还在滑行进站时，挂接方日志持续 `CPL-PATHFOUND found=0` 且**不出现** `COUPLE-OK`；车底停稳（`cur_speed` 归零）后下一次候选扫描出现 `found=1`，随后 `COUPLE-OK`。
2. **第 4 步（负）**：正常停稳在站台的等待车底，挂接时机与改动前一致（不得因为取 `cur_speed` 而长期锁不上；若一直 `found=0` 而对方确实停稳，说明判据取错了节）。
3. **第 5 步（正）**：机车一进入 `OT_GOTO_COUPLE` 且尚无配对，出现 `RESV-NOBOOK ... reason=parked-no-pair`（每车只打一次），且**不出现** `CPL-PATHFOUND found=0` 的每 tick 刷屏式寻路、不再 `MarkTrainAsStuck` 蹭行。
4. **第 5 步（唤醒）**：`CPL-PAIR ... dist=` 紧后出现路线预留（`RP cur=`/`CPL-TRACE` 之类），且车确实起步（`LOCO-MOVE` 或速度恢复），**不得**出现"配上了永远不动"。
5. **REG-305-L（倒车链到点）**：倒车（`DrivingBackwards`）的挂接车能够触发 `COUPLE-DEST-EMPTY` / `COUPLE-COMMIT-FAIL`（说明"到点"判据终于为真），不再一路开过目的地。
6. **负判据（不误伤）**：车库内挂接（`IsRailDepotTile`）不被 `parked-no-pair` 拦；`WAIT_COUPLE` 等待方行为逐字不变（仍是 `parked-waiter`）；普通非挂接列车无任何 `RESV-NOBOOK`。



