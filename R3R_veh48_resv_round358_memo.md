# 第 358 轮：KI-533 探针首次实测取证（veh=48 预留链）

## 一、采样身份

| 项 | 读数 |
|---|---|
| 日志 | `build\R3R_debug.log`，192 244 B，2 305 行，2026-10-29 15:13:15 |
| exe | `build\openttd.exe`，52 344 832 B，2026-10-29 15:09:41 |
| 结论 | 产生者 = 第 357 轮探针版 ⇒ **采样有效**，探针已在运行时生效 |
| 命中计数 | `veh=48` 479 行；`RESV-BEGIN`+`RESV-END`+`JUNC-CHOOSE` 共 **6** 行 |

## 二、探针原始行（全文，带日志行号）

```
:660  RESV-BEGIN veh=57 tick=13412 tile=35,12 mf=35,12 td=8 spd=2 real=3 order=6 one=1 look=1 prevEnd=35,12 prevEndPos=367
:674  RESV-END  veh=57 tick=13412 from=35,12 endTile=42,13 endTd=8 endPos=479 curPos=368 endBits=0101 junction=0 real=3 spd=2
:690  RESV-BEGIN veh=48 tick=13413 tile=42,10 mf=42,10 td=8 spd=0 real=2 order=16 one=0 look=1 prevEnd=42,10 prevEndPos=602
:762  RESV-BEGIN veh=48 tick=13445 tile=42,10 mf=42,10 td=0 spd=0 real=2 order=16 one=0 look=0 prevEnd=-1,-1 prevEndPos=-1
:903  RESV-BEGIN veh=48 tick=13485 tile=42,10 mf=42,10 td=0 spd=0 real=2 order=16 one=1 look=0 prevEnd=-1,-1 prevEndPos=-1
:1227 RESV-END  veh=48 tick=13505 from=42,10 endTile=40,10 endTd=0 endPos=37 curPos=0 endBits=2121 junction=1 real=2 spd=0
```

## 三、veh=48 逐格移动时间线（`LOCO-MOVE`，本轮新增字段）

```
tick=13445 tile=42,10 spd=5  db=0 look=1 endTile=42,10 endTd=8 endPos=602 curPos=603 endBits=0101
tick=13516 tile=42,10 spd=23 db=1 look=1 endTile=40,10 endTd=0 endPos=37  curPos=0   endBits=2121
tick=13551 tile=41,10 spd=73 db=1 look=1 endTile=40,10 endTd=0 endPos=37  curPos=15  endBits=2121
tick=13560 tile=41,10 spd=64 db=1 look=1 endTile=40,10 endTd=0 endPos=37  curPos=20  endBits=2121
tick=13589 tile=40,10 spd=38 db=1 look=1 endTile=40,10 endTd=0 endPos=37  curPos=31  endBits=2121
tick=13644 tile=40,10 spd=5  db=1 look=1 endTile=40,10 endTd=0 endPos=37  curPos=37  endBits=2121
tick=13714 tile=39,10 spd=71 db=1 look=0 endTile=-1,-1  endTd=-1 endPos=-1 curPos=-1 endBits=0000
tick=13721 tile=39,10 spd=71 db=1 look=1 endTile=27,12  endTd=0  endPos=196 curPos=1 endBits=0101
tick=13731 tile=39,11 spd=85 db=1 look=0 endTile=-1,-1  endTd=-1 endPos=-1 curPos=-1 endBits=0000
tick=13744 tile=38,11 spd=72 db=1 look=0 endTile=-1,-1  endTd=-1 endPos=-1 curPos=-1 endBits=0000
```

线路：`42,10 → 41,10 → 40,10 → 39,10(track 08, dir=6) → 39,11(track 04) → 38,11`，全程 `db=1`（倒车驶出），最后一行 `tick=13744 tile=38,11 spd=72`，与第 356 轮停位**逐字一致**（复现成功）。

## 四、逐条判据结果

| 判据 | 结论 | 证据 |
|---|---|---|
| R48-2「成功的预留终止在道岔」 | **已证实** | `:1227 RESV-END endTile=40,10 endBits=2121 junction=1`；同期 `LOCO-MOVE` 五次读数 `endTd=0 endBits=2121` 与该 dict 一致。**注意：道岔是 `40,10`，不是先前口述的 `39,10`**；`39,10` 是斜向换轨（track 08→04）的普通弯，不是多轨格 |
| R48-1「预留覆盖到哪一格」 | **已证实** | 成功预留仅覆盖 `42,10 → 40,10`（`endPos=37`，约 4~5 格）。车在 `curPos` 追平 `endPos=37`（`tick=13644`）后**预留归零**，但车继续驶过 `39,10 / 39,11 / 38,11` 三格时 `look=0` ⇒ **无预留状态下通过道岔** |
| R48-3「同一道岔二次预留」 | **未证实（数据不支持原假设）** | `:903` 那次 `one=1 prevEnd=-1,-1`（前一次 lookahead 已被清空），与上一条 `RESV-END.tile=40,10` 不同格（起点 `42,10`）。本会话未见"终点=某道岔、下一次又在该道岔起订"的成对形态。**玩家主诉的"在道岔订了第二次预留"本轮未复现** |
| R48-4「第三个道岔拐错方向」 | **无数据** | `JUNC-CHOOSE` **零命中**（6 条探针行里一条都没有）⇒ 探针落点未覆盖实际走过的裁决路径，见疑点 REG-358-B |

## 五、疑点登记（按"有疑点即落盘"规则）

### REG-358-A（高）：`RESV-END` 只捕获 1/3 的成功预留

- 期望：每次成功的预留尝试都应留下一条 `RESV-END`。
- 实测：veh=48 `RESV-BEGIN` 3 条（13413 / 13445 / 13485），`RESV-END` 仅 1 条（13505，对应 13485 那次 `one=1`）。
- 矛盾点：`13445` 那次 `BEGIN` 明显成功——同 tick 的 `LOCO-MOVE` 立刻出现 `look=1 endTile=42,10 endPos=602`，但**没有 END 行**。
- 可疑原因（待验证，未取数）：`TryPathReserveWithResultFlags()` 内存在"已有 lookahead 时早退 / 复用旧预留"的分支，成功但未走到我插入的出口。
- 待验证判据：把 `RESV-END` 也挂到早退分支，或改用 `lookahead != nullptr` 的最终状态在函数尾统一打一行。

### REG-358-B（高）：`JUNC-CHOOSE` 零命中

- 期望：车实际经过 `40,10`（`junction=1`）与 `39,10`（track 08→04 换轨）时，`ChooseTrainTrack()` 应被调用并命中出口 lambda。
- 实测：全日志 0 条。
- 可疑原因（待验证）：
  1. 出口 lambda 只挂在 `ChooseTrainTrack()` 的某一个 return 上，实际返回走了别的路径；
  2. 该 lambda 被 `bool *r3r_chosen_ret` 非空条件包裹，或 `R3RDbgOn()` 之外的 `static` 去重表在首次就吞掉；
  3. 本轮预留由 `ForceReserveOnce`（`one=1`）驱动，可能绕过 `ChooseTrainTrack` 的常规调用点。
- 待验证判据：在 `ChooseTrainTrack()` 入口无条件打一行（含 `enterdir`、`tracks`），先确认函数是否被调用；再决定是否把探针移到 `YapfTrainChooseTrack` / `TrainCheckIfLineEnds`。

## 六、本轮判定汇总（一句话）

**KI-533 的"成功预留终止在道岔"已被日志实锤（`40,10`，`junction=1`，`endBits=2121`），且首次拿到"预留耗尽后裸奔通过三个格"的逐个像素证据；玩家口述的"二次预留"与"第三个道岔拐错方向"两条本轮均未复现，其中后者是因为 `JUNC-CHOOSE` 探针零命中（REG-358-B）。**

## 七、日志尾段：veh=48 确实静止在 `38,11`

- 最后一条 `LOCO-MOVE` = `tick=13744 tile=38,11 spd=72`；此后 **零条** `LOCO-MOVE`。
- 但 `CPLSPD veh=48` 继续刷到 `tick=13773`，全程 `tile=38,11`，`spd` 18→16→14→12→10→8→7→6→5→5，
  `db=1`、`gap=-1`（`gap=-1` = 当前无预留）、`n` 累加到 232。
- `RESV-BEGIN veh=48` 最后一条是 `tick=13485`（`tile=42,10`）⇒ **停在 `38,11` 之后连一次预留尝试都没有**。
- 日志尾段（`13773` 之后）全部是 `veh=57` 的 `SVC-DEPOT-SKIP` 与 `POSCONSIST`，与会话结束前无关。

⇒ 症状与第 356 轮完全同形：车在 `38,11` 停在非目标格（`destTile=923 = 27,7`），速度残值 5 不再归零、
无预留、无重试。这属独立于道岔的"卡住"面，与 KI-348 同族但成因不同（此处 `spd != 0`）。

## 八、下一步

1. 修 REG-358-A / REG-358-B 的探针覆盖（仍只读、不动行为、不碰 `.h`）。
2. 取"`38,11` 之后为何不再发起预留"的证据：在 `TrainLocoHandler` 的停驶/滑行分支补一行状态探针
   （`spd`、`j`、`adv_spd`、`Stuck`、`cur_speed` 与是否进移动分支），与 KI-348 的 `SPD-DROP`/`LOCO-MOVE` 口径对齐。
3. ✅ 已由玩家标注：第三个道岔 = **`38,11`**，应走 **LOWER** 分支却走了 **X** 分支（见 §九）。

## 九、第 359 轮：R48-4 得证（`38,11` 拐错分支）

**玩家原话**：「veh48 在（38，11）走错岔道，原来应该走 LOWER 分支，却走了 X 分支」

**日志锚点（同一会话，第 358 轮采样，无需复跑）**

```
tick=13731 tile=39,11 spd=85 track=04 dir=6 look=0 endTile=-1,-1 endBits=0000   ← 来向：UPPER 斜轨
tick=13744 tile=38,11 spd=72 track=01 dir=5 look=0 endTile=-1,-1 endBits=0000   ← 走了 X（水平）
```

**判读**

- OpenTTD TrackBits：`X=0x01` / `Y=0x02` / `UPPER=0x04` / `LOWER=0x08` / `LEFT=0x10` / `RIGHT=0x20`。
- `tile=38,11 track=01` ⇒ 车站在 **TRACK_BIT_X** 上，与玩家口述"却走了 X 分支"**逐字吻合**。
- 期望的 **LOWER=0x08** 没有出现 ⇒ 该格至少是 `X|LOWER` 双轨道岔，车选了 X。
- 该次通过时 `look=0`（无预留）、`endBits=0000` ⇒ **拐向是在无预留状态下决定的**。

**坐标订正（重要）**

第 358 轮把 `40,10` 当成"第三个道岔"是**误判**。正确分工：

| 格 | 角色 | 证据 |
|---|---|---|
| `42,10` | 起点（挂接/等待格） | `RESV-BEGIN` ×3 全在此格 |
| `40,10` | 预留终点（多轨，`junction=1`） | `RESV-END endTile=40,10 endBits=2121 junction=1` |
| `39,10` | 普通斜向换轨（track 08→04） | `LOCO-MOVE track=08 dir=6` → 下一格 `track=04` |
| **`38,11`** | **真正拐错的那个道岔** | `LOCO-MOVE track=01`（X），应为 `0x08`（LOWER） |

**猜测根因（已修正一次，先落盘）**

原推断「拐向根本没经过 `ChooseTrainTrack`」**已被代码证伪**：

```
src\train_cmd.cpp:20971-20974
if (prev == nullptr) {
    /* Currently the locomotive is active. Determine which one of the available tracks to choose */
    ChooseTrainTrackResult result = ChooseTrainTrack(first, gp.new_tile, enterdir, bits,
                                                     CTTF_MARK_STUCK | CTTF_NON_LOOKAHEAD);
```

即机车每进入一个新格（`prev == nullptr`）**都会**调 `ChooseTrainTrack`，且带上 `CTTF_NON_LOOKAHEAD` 标志。
`veh=48` 在 `tick=13744` 进入 `38,11` 时必然调用过它 ⇒ `JUNC-CHOOSE` 零命中只能说明**探针落点错**
（REG-358-B 从"疑似绕开"升级为"确证漏采"），不说明函数没被调用。

⇒ 新方向（待验证）：`CTTF_NON_LOOKAHEAD` 是**无预留时的专用入口**，该分支里 `ChooseTrainTrack` 若
YAPF 寻路失败就退回"取第一条可用轨"，于是多轨道岔选了 `X` 而不是来向延续的 `LOWER`。
若成立，任何"预留耗尽后仍需过道岔"的场合都会按轨道枚举顺序拐弯，属通用缺陷而非本车个例。

**待验证判据**

1. `38,11` 的轨道位图是否确为 `X|LOWER`（需一格 dump 或拆轨目视）。
2. `TrainController` 在 `lookahead == nullptr` 且 `GetTileTrackdirBits` 多比特时的选轨表达式（取 `FindFirstTrackdir`？取 `ReverseTrackdir` 派生？）。
3. 该选轨是否受 `ChooseTrainTrack` 的 `enterdir` 约束——若无，需补一条"无预留时按来向延续分支"的规则。

## 十、第 359 轮续：`38,11` 走上 X 的机制已定位（三处返回值互不一致）

**逐字证据（`build\R3R_debug.log`，同一会话，无需复跑）**

```
:2005  RESV-EXT-PRE   veh=48 tile=38,11 enterdir=0 tracks=0x9 reserved=0x8 doRes=0 real=2 spd=87
:2006  RESV-FOREIGN-RES veh=48 tile=38,11 enterdir=0 res=0x8 tracks=0x9 spd=87 real=2
:2272  CPL-RESERVE   veh=48 reserved=1 next=2 path=5 booked=5
:2273  RESV-CPL-XTILE veh=48 tile=38,11 tracks=0x9 resv=0x8 ret=0x2 accept=0x9
:2274  CPL-RET        veh=48 tick=13730 tile=38,11 enterdir=0 tracks=0x9 res=0x8 ret=0x1 why=nores-final spd=87 real=2
:2280  DEPOTBLK      veh=48 tile=38,11 track=01 eng=1 isdepot=0 ... spd=74 destRaw=2
:2281  LOCO-MOVE     veh=48 tick=13744 tile=38,11 j=206 spd=72 track=01 dir=5 db=1 stuck=0 look=0 endBits=0000
:2282  RESV-LA-REBUILD veh=48 tile=38,11 spd=64 order=16 own=1 look=1 end=2
```

**判定 1（事实）：`38,11` 确实是 `X|LOWER` 双轨格**
`tracks=0x9` = `X(0x01) | LOWER(0x08)`。玩家口述的两个分支都存在。

**判定 2（~~事实~~ **已作废，见 §十一**）：~~LOWER 已被"别人的预留"占着~~
`reserved=0x8` 且同格打出 `RESV-FOREIGN-RES` ⇒ ~~LOWER 上有预留、不属于本车~~。
~~所以本车根本订不到 LOWER —— 这才是"想走却走不过去"的物理原因。~~
⇒ **玩家纠正：该预留就是 veh48 自己的（§十一）**，`RESV-FOREIGN-RES` 属归属判据误报（REG-359-D）。

**判定 3（事实，核心）：同一格、同一 tick，三处组件给出三个不同答案**

| 来源 | 结论 |
|---|---|
| `CPL-RESERVE` | `reserved=1 path=5 booked=5`（自认订路成功） |
| `RESV-CPL-XTILE` | `ret=0x2`（Y） |
| `CPL-RET ... why=nores-final` | `ret=0x1`（X）—— **实际执行** |

出口是 `src\train_cmd.cpp:18238`：

```18238:18238:src/train_cmd.cpp
		return r3r_chosen_ret(best_track, result_flags, "nores-final");
```

此时 `best_track` 是**残留/兜底值**（= `FindFirstTrack(tracks)` 的最低位置 X），不是任何一次寻路或预留的产物。

**判定 4（补充）：YAPF 并非"只会走 X"**
通向 `21,7` 的回溯里两条分支都在：
`:2263 RP cur=38,11 td=3 trk=3 bits=0x9`（LOWER）与 `:2154 RP cur=38,11 td=0 trk=0 bits=0x9`（X）。
⇒ 不是寻路能力问题，是**最终交付值**的问题。

**疑点登记（按"有疑点即落盘"规则）**

- **REG-359-A（中）**：`LOCO-MOVE` 由 `39,11` 直接到 `38,11`（`dx=-1, dy=0`），OpenTTD 只有对角邻接。
  极可能两行分别打的是"链头 `tile`"与"`db=1` 时移动前端 `tile`"，参照物不同。后续所有以"格"为单位的判据都要先确认参照物。
- **REG-359-B（高）**：同 tick `CPL-RESERVE reserved=1` 与 `CPL-RET res=0x8 why=nores-final` 矛盾。
  要么两次读的是不同格的预留，要么 `reserved=1` 指的是"应当订"而非"已订下"。二者语义必须分清，否则 KI-480 系的判据全部失准。
- **REG-359-C（中）**：`RESV-CPL-XTILE ret=0x2`（Y）与最终 `ret=0x1`（X）不同
  ⇒ `XTILE` 与 `nores-final` 用的不是同一个 `best_track` 流转链。

**结论一句话（**第 1 句已作废，改写见下；§十一**）**

~~车走 X 不是因为它"判断该走 X"，而是因为 **LOWER 被别的车预订、本车在这格拿不到预留**，
最后从 `nores-final` 兜底出口拿到**最低位 `TRACK_X`** 并物理开上去——~~
⇒ 改写：**LOWER 的预留是本车自己的（§十一 / REG-359-D），本车有资格沿它走；但归属判据 `GetTrainForReservation` 误判成"别人的",
屏蔽了本该返回 LOWER 的 `early-haverserved` 出口，最后从 `nores-final` 兜底拿到最低位 `TRACK_X` 并物理开上去。**
与 KI-480 / KI-479 同一族：**"预留失败不得继续前进"的闸门没覆盖到这个出口**。

**判定 5（事实，根因行已定位）：X 来自"跨格答案被拒后的最低位回退"**

```17927:17951:src/train_cmd.cpp
		if (path_found != INVALID_TRACK) {
			const TrackBits r3r_accept_bits = (TrackBits)(tracks | (GetReservedTrackbits(tile) & DiagdirReachesTracks(enterdir)));
			if (HasTrack(r3r_accept_bits, path_found)) {
				best_track = path_found;
			} else {
				...
				const Track r3r_same_tile = FindFirstTrack(tracks);
				best_track = (r3r_same_tile != INVALID_TRACK) ? r3r_same_tile : best_track;
			}
		}
```

对账 `:2273` 的 `RESV-CPL-XTILE ... tracks=0x9 resv=0x8 ret=0x2 accept=0x9`：

| 量 | 值 | 含义 |
|---|---|---|
| `path_found` | `0x2` = `TRACK_Y` | couple 寻路给出的答案（**不是本格能走的轨**） |
| `r3r_accept_bits` | `0x9` | `tracks \| (resv & reaches)`，本格可采纳集合 |
| `HasTrack(0x9, 0x2)` | false | ⇒ 走 else 分支 |
| `FindFirstTrack(0x9)` | `0x1` = `TRACK_X` | ⇒ **`best_track = X`，就是这一行** |
| 玩家期望 `LOWER` | `0x8` | 在 `tracks` 里；`res=0x8` 是**本车自己的预留**（§十一，原写"被 foreign 占用"已作废） |

`best_track` 初值 `INVALID_TRACK`（`:17294`），本分支内唯一赋值点就是这里（`18185` 那条在 couple 订单下被排除），
所以 `nores-final`（`:18238`）交出的 `ret=0x1` **可以逐字归因到 `17950-17951` 的 `FindFirstTrack`**。

**缺陷性质（可直接写成修复口径）**

`FindFirstTrack(tracks)` 只回答"本格有哪条轨"，不回答"这条轨能不能从 `enterdir` 出去、是不是我要去的那条"。
在多轨岔口上它恒取最低位 ⇒ 对 `X|LOWER` 双轨格恒取 `X`。
而且即便回退到 `LOWER` 也只是碰巧对——`LOWER` 上正是**本车自己的预留**（§十一），
正确做法是识别它并沿它走（走 `early-haverserved`），而不是靠最低位兜底碰运气。
正确口径应是：**跨格答案被拒且没有与 `enterdir` 匹配的可采纳轨时，按 KI-316 处置（`FreeTrainTrackReservation` + `MarkTrainAsStuck`）停住等待**，
而不是交出一条与目标无关的轨继续开。

**剩余待验证（不影响上面的归因）**

1. `src\train_cmd.cpp:18208-18211` 的守卫（KI-480 口径）为何未接住：因该 call 有自己的预留（`CPL-RESERVE reserved=1 booked=5`），
   `FollowTrainReservation(IgnoreLookahead).tile != front->tile` ⇒ 判定为"有自己的预留可走"而放行。
   ⇒ **判据是"全车有没有预留"，不是"我要走的这条轨有没有预留"**，这就是 REG-359-B 的真实含义（需再确认 `reserved` 字段语义）。
2. `path_found = TRACK_Y` 是哪个格的答案（跨格参照物，见 REG-359-A）。

## 十一、玩家纠正（第 359 轮）：LOWER 上的预留**就是 veh48 自己的** —— 判定 2 与结论作废

**玩家原话（决定性一句，照录）**

> 「关于你的" LOWER 上已经有一条预留，而且这条预留不属于本车（veh=48）"是错误的，
> LOWER上面的预留就是veh48的（至少这上面的预留往一个方向延伸过去能碰到veh48，
> 因此veh48没有理由不沿着自己的预留走最舒适的路）」

⇒ **判定 2（`RESV-FOREIGN-RES`）与"结论一句话"中"LOWER 被别的车预订、本车拿不到预留"一段全部作废。**
`38,11` 的 `reserved=0x8` 是 veh48 自己的预留，本车**完全有理由、也有资格**沿 LOWER 走。

**新增疑点（升级为根因候选）**

- **REG-359-D（高）：`RESV-FOREIGN-RES` 归属判据误报。**
  玩家实测：`38,11` LOWER 的预留沿一个方向延伸可碰到 veh48 ⇒ 主人是 veh48；
  但本机探针打出 `RESV-FOREIGN-RES veh=48 tile=38,11 res=0x8` ⇒ `owned` 被判成 false。

**判据原文（`src\train_cmd.cpp:17455-17472`）**

```17455:17472:src/train_cmd.cpp
	bool r3r_couple_foreign_res = false;
	if (consist->current_order.IsType(OT_GOTO_COUPLE) && !consist->current_order.GetCoupleIsDepot() &&
			res_tracks != TRACK_BIT_NONE) {
		bool owned = false;
		if (IsPlainRailTile(tile)) {
			for (TrackBits b = res_tracks; b != TRACK_BIT_NONE && !owned; b = KillFirstBit(b)) {
				const Train *const owner = GetTrainForReservation(tile, FindFirstTrack(b));
				owned = (owner != nullptr && owner->First() == consist->First());
			}
		} else if (IsRailStationTile(tile)) {
			...
		} else {
			/* 车库/道口/隧道桥：非 plain rail 或整格位，保持原判据不动。 */
			owned = true;
		}
		r3r_couple_foreign_res = !owned;
	}
```

**回溯实现（`src\pbs.cpp:2036-2052`）**

```2036:2052:src/pbs.cpp
Train *GetTrainForReservation(TileIndex tile, Track track)
{
	assert_msg_tile(HasReservedTracks(tile, TrackToTrackBits(track)), tile, "track: {:X}", track);
	Trackdir  trackdir = TrackToTrackdir(track);
	RailTypes rts = GetRailTypeInfo(GetTileRailTypeByTrack(tile, track))->indirect_compatible_railtypes;
	for (int i = 0; i < 2; ++i, trackdir = ReverseTrackdir(trackdir)) {
		if (Train *t = R3RFollowReservationToOwner(tile, trackdir, rts)) return t;
	}
	return nullptr;
}
```

**两条候选机制（待验证，二者都会让 `owned=false`）**

| 候选 | 机制 | 需要的取数 |
|---|---|---|
| **D-1** | `R3RFollowReservationToOwner()` 两个方向都回溯不到车主（预留链在中间断了一格、或 `indirect_compatible_railtypes` 不匹配）⇒ 返回 `nullptr` ⇒ 按"stray"当非本车 | 在该判据处加探针打 `owner=nullptr\|vehN` 与 `i`（哪个方向命中） |
| **D-2** | 回溯到了某列车，但 `owner->First() != consist->First()`（例如命中的是链内另一节、或 `consist` 不是链头） | 同上探针再加打 `owner= owner->First()= consist->First()=` |

**因果链（这才是走 X 的完整路径）**

```
38,11 本车预留 = LOWER (res=0x8)
  → GetTrainForReservation 回溯失败/比对不等 ⇒ owned=false
  → r3r_couple_foreign_res = true          (17471)
  → early-haverserved 被 !r3r_couple_foreign_res 屏蔽 (17523)
      ↑ 这个出口本该返回 FindFirstTrack(res_tracks) = 0x8 = LOWER ★玩家期望的分支★
  → 继续往下走，path_found=0x2 被 accept 门拒
  → 17950-17951 FindFirstTrack(tracks=0x9) = 0x1 = X
  → nores-final (18238) 交出 X → 车物理走上 X
```

⇒ **`RESV-FOREIGN-RES` 误报是"走 X"的第一因；`17950` 的最低位兜底是最后一段。**
只修 17950 不改归属判据，本车仍然不会沿自己那条 LOWER 走（只会换一种错法）。

**还有第二道锁（不要漏）**

`17523` 的早退条件除 `!r3r_couple_foreign_res` 外还要求 `!r3r_couple_no_lookahead`（第 338 轮 KI-359 加的"本车手上已有前瞻"自证，见 `17488-17521`）。
现场 `look=0` ⇒ 即使归属判据修好，这道锁**仍会拦住** `early-haverserved`。
⇒ 修归属只是解开第一道；第二道需要在两种口径里拍一个：
 **(i)** 允许"沿自己的预留继续走"并就地补前瞻（`FillTrainReservationLookAhead`）；
 **(ii)** 不吃捷径，改走 `ExtendTrainReservation()` + YAPF 重新取得 `CTTRF_RESERVATION_MADE`。

**修法方向（待拍板，未改码）**

- **A（正本）**：修 `owned` 判据——回溯失败时不要直接当"别人的"，改按 `follow` 的中间格是否有本车/是否本车预留链来判；
  且扩到 `R3RFollowReservationToOwner` 拿不到时用 `FollowTrainReservation(consist, nullptr, IgnoreLookahead)` 的终点格与 `res_tracks` 交集反查。
- **B（兜底）**：`17950-17951` 的 `FindFirstTrack(tracks)` 改成优先 `res_tracks`（自己的预留）里与 `enterdir` 匹配者，再退 `tracks`。
- 两者不互斥；**A 必须做**，B 只是止损。

**待验证判据**

1. `GetTrainForReservation(38,11, TRACK_LOWER)` 在该 tick 到底返回 `nullptr` 还是某车（D-1 / D-2 二分）。
2. `consist` 在该函数里是不是 `first->First()`；`owner->First()` 与 `consist->First()` 的实际车号。
3. `38,11` LOWER 的预留链往 veh48 那一侧逐格是否连续（若中间缺格 ⇒ 与 R48-1「预留链中断」同因）。

## 十二、玩家给的方向（第 359 轮）：「预留两端都压车 ⇒ 主人被认成 veh0」—— 代码已确证机制，REG-359-D 收敛为 D-2

**玩家原话（照录）**

> 「回溯失败吗，这个我倒是之前遇到过。应该是这样的，原生OpenTTD认预留主人的方法是从这一格往两边回溯，
> 碰到哪个车就把预留当作谁的，但是这里因为预留两端都压着一列车，所以可能预留把主人看成了veh0
> 导致veh48自己的预留变成了别人的。你往这个方向查查」

⇒ 方向成立，且**不是"回溯失败"**：回溯成功了，但**主人认错**（D-2 形态）。

### 12.1 代码事实（逐行）

三条独立机制叠加，任一条都足以把 owner 认成别的车：

1. **两向遍历"先命中先用"，首试方向是固定的**（`src\pbs.cpp:2040-2049`）：

```2040:2049:src/pbs.cpp
	Trackdir  trackdir = TrackToTrackdir(track);
	...
	for (int i = 0; i < 2; ++i, trackdir = ReverseTrackdir(trackdir)) {
		if (Train *t = R3RFollowReservationToOwner(tile, trackdir, rts)) return t;
	}
```

`TrackToTrackdir(track)` 就是 `(Trackdir)track`（`src\track_func.h:279-283`），`TRACK_LOWER = 3`
⇒ 首试 `TRACKDIR_LOWER_E`（向东），反向 `TRACKDIR_LOWER_W` 才是第二试。
⇒ **先东后西是枚举值的产物，与"谁真的订了这条轨"无关**；东侧一旦有车就直接 `return`，**西侧永不试**。

2. **回溯找的是"预留路径终点格上的车"，不是"最近的车"**（`pbs.cpp:1936-1945`）：
`FollowReservation()` 一路走到预留尽头 `ftoti.res.tile`，只在**那一格** `CheckTrainsOnTrack`；命中即 `return`。
⇒ 预留两头都压着车时，**先试的那一头站着谁，主人就是谁**。

3. **同一格多列车时取 index 最小**（`pbs.cpp:1380-1400` `CheckTrainsOnTrack`，KI-330 已证）：
`veh0` 是全局最小 index ⇒ **只要它在那格上就必然胜出**。

4. 调用方严格比对链头（`src\train_cmd.cpp:17455-17472`）：`owned = (owner != nullptr && owner->First() == consist->First())`
⇒ `owner=veh0` ⇒ `owned=false` ⇒ `r3r_couple_foreign_res=true` ⇒ 屏蔽 `early-haverserved`（`:17523`）
⇒ 落到 `17950-17951` 的 `FindFirstTrack(tracks)` 兜底 X。

### 12.2 与现场读数对账

- `:2006 RESV-FOREIGN-RES veh=48 tile=38,11 res=0x8` 与"owner 被判成非本车"一致，但这条探针**不区分**
  `nullptr`（真没人）与"别人的车"（`veh0`）⇒ 正是 REG-358 系"探针漏采"的延续。

### 12.3 关键发现：现成探针已在树里，只被 watch list 挡住（取证成本极低）

`pbs.cpp:1905-1922 R3RResvWalkLog()` 已经能打：起点格 / 首试方向 / 终点格 / 如何找到
（`blocked|direct|platform|tunnel|none`）/ **owner 车号** / **owner 链头**。
但它被 `if (!R3RIsWatchedResvTile(start)) return;`（`:1909`）挡住，`38,11` 不在关注表里
⇒ 全日志 `RESV-WALK` **0 条**（已核实）。

⇒ **最小代价取证 = 把 `38,11` 加进 watch list**（只读探针、零行为变更、不改判据），重编内测树后复跑同场景，
即可直接读出：首试方向、两支各自走到的终点格、终点格上是哪辆车（veh0 还是 veh48 的某节）。

### 12.4 复跑判据

1. 出现 `RESV-WALK tile=38,11 td=3 ... how=direct owner=0 head=0` ⇒ 玩家假设逐字成立，**D-2 实锤、D-1 排除**；
2. 若 `td=3` 走 `how=none` 而随后 `td=11 how=direct owner=48` ⇒ 反证"首试顺序"是唯一原因（东侧无车）；
3. 若两支都 `how=none` ⇒ 才是 D-1（真回溯失败），须查 `indirect_compatible_railtypes` 与链断格。

### 12.5 修法影响（据此收窄）

- 归属判据不能用"路径尽头的车"，必须问"**这条预留是不是我这条链的**"。两条现成候选：
  **(a)** 换用同文件的 `R3RFollowReservationToNearbyTrain()`（KI-210，`pbs.cpp:2010-2027`，"最近的车"语义）；
  **(b)** 正向判定：把 `FollowTrainReservation(consist, nullptr, IgnoreLookahead)` 的路径与 `res_tracks` 求交。
  ⇒ 建议 **(b) 为主、(a) 为兜底**：(a) 在"预留链中间压着别人的车"时同样会认错。
- `17950-17951` 的最低位兜底仍须按 KI-316 处理（停住等待），否则归属修好后别的路径还会交出 X。

**REG-359-D 状态更新**：机制已从代码确证（D-2 形态：主人认错，非回溯失败）；**具体命中车号/方向待 `RESV-WALK` 实测**；修法未落地。

## 十三、玩家新增口径（第 360 轮）：两端都有车时的**归属仲裁规则**

**玩家原话（照录，决定性口径）**

> 「关于回溯失败，我提出以下想法：回溯时，假如发现两端均有车，则优先划给处于非等挂状态的列车，
> 但假若预留格处于站台，则优先划给处于等挂状态的列车」

### 13.1 规则形式化（记作 REG-359-E）

输入：`GetTrainForReservation(tile, track)` —— `tile/track` 是待判归属的那一位预留。
先算两端：`A = R3RFollowReservationToOwner(tile, td0)`、`B = R3RFollowReservationToOwner(tile, ReverseTrackdir(td0))`。

1. **A、B 至多一个非空** ⇒ 直接返回那个（与现状逐字一致，零回归）。
2. **A、B 均非空（两端都压着车）** ⇒ 仲裁：
   - 若 `IsRailStationTile(tile)`（**预留格在站台**）⇒ 取 **等挂** 的那一个；
   - 否则（普通轨道 / 道岔）⇒ 取 **非等挂** 的那一个；
   - 两端同态（都等挂 / 都不等挂）⇒ 回落到现状（先试 `TrackToTrackdir` 方向）。

### 13.2 为什么这条规则在现场是对的

- 现场 `38,11` LOWER：一端 **veh48**（正在跑 GOTO_COUPLE、非等挂），另一端 **veh0**（解出的纯车底，`OT_WAIT_COUPLE` 等待者）。
  按规则 ⇒ 判给 veh48 ⇒ `owned=true` ⇒ `early-haverserved` 放行 ⇒ 沿 LOWER 走。**与玩家期望逐字一致**。
- 站台例外也符合直觉：站台上等待挂接的车底占的是**整格位**，其预留本就该归它自己；
  否则挂车方会把它脚下的位抢走（KI-125 / 第 126 轮整格位泄漏同族）。

### 13.3 `IsWaiting`（"等挂状态"）的候选判据 —— 待拍板

| 编号 | 判据 | 说明 |
|---|---|---|
| (a) | `current_order.IsType(OT_WAIT_COUPLE)` | 订单语义，`OT_WAIT_COUPLE=17`；第 242 轮 KI-348 / 第 172 轮已在用 |
| (b) | `R3RIsCarOnlyFormation(t)` | 纯车底形态（解出方被清成 car-only） |
| (c) | (a) && (b) | 最严 |
| (d) | (a) \|\| (b) | 最宽 |

倾向：**(a) 为主判据**（订单是语义真相），(b) 只在站台例外里做补充（解出方可能先被清成 car-only 才进 WAIT_COUPLE）。

### 13.4 落地位置与影响面

- 唯一改动点：`src\pbs.cpp` 的 `GetTrainForReservation()`（`:2036-2052`）——把"先命中先返回"的 `for` 循环
  改成"两向各取一次、再仲裁"。`R3RFollowReservationToOwner()`（`:1925-1984`）本身不动。
- **影响面是全局的**：`GetTrainForReservation` 的每个调用点都会跟着变（归属判据 `train_cmd.cpp:17455-17472`、
  `REDSIG` 探针、`RESV-AUDIT` 审计归主…）⇒ 不是"只改一处判据"，必须按 A/B 回归：
  **两端同态 / 只有一端有车时读数必须逐字不变**，只有"两端都有车"才改口。
- 与 **KI-350** 的关系：KI-350 登记的正是"先走 `TrackToTrackdir` 且命中即返回 ⇒ 归属不可用"，本规则是它的修法。
- 与 **REG-359-D** 的关系：D-2（认错主人）由本规则直接修掉；**D-1（两端都回溯失败）本规则管不到**，仍须单独判。

### 13.5 已知风险（必须先说清）

1. **仲裁输入可能已被污染**：`R3RFollowReservationToOwner` 末端 `CheckTrainsOnTrack` 在同一格上取 **index 最小**的链头
   （KI-330 已证），veh0 是全局最小 ⇒ 若某一端格上同时有 veh0 与 veh48 的车节，该端返回的就是 veh0，
   仲裁拿到的"那列车"根本不是真正的占位者。⇒ 落地时要么统计"该端格上有几列车"，要么改用
   `R3RFollowReservationToNearbyTrain()`（KI-210，最近车语义）作为该端的取车方式。
2. **逆向风险**：两列车都在正常跑（都非等挂）时新规则会改变现有归属结果 ⇒ 必须有负判据覆盖。
3. **站台格的特殊性**：站台预留可能是**整格位**（`HasStationReservation`），此时"哪一节压在上面"更不可靠。

### 13.6 待玩家拍板（Q1~Q4）

- **Q1** `IsWaiting` 取 (a)/(b)/(c)/(d) 哪一个？
- **Q2** 两端同态时是否维持现状（先试 `_E` 方向）？建议维持。
- **Q3** 站台例外是否只限整格位预留，还是含站台上的普通轨位？
- **Q4** 是改全局 `GetTrainForReservation`，还是只给归属判据新增 `GetTrainForReservationArbitrated`（更保守）？建议后者先行。

**状态**：规则已记录、**未改码、未构建**；`IsWaiting` 判据与 Q1~Q4 未拍板。

### 13.7 玩家拍板（第 361 轮，2026-10-29）—— 全部按建议，可直接落地

**玩家原话（照录，决定性）**

> 「Q1,仅用判据1，不补2
> Q2，两端同态基本不可能发生，保持现状即可
> Q3，站台上还能有普通轨位？我认为不可能发生因而没有考虑，如果你坚持考虑就按照你推荐的来
> Q4，按照你推荐的来（后者）」

| 问题 | 拍板结果 |
|---|---|
| **Q1** `IsWaiting` 判据 | **只取 (a)** = `current_order.IsType(OT_WAIT_COUPLE)`；**不补** `R3RIsCarOnlyFormation`（(b)/(c)/(d) 全部否决） |
| **Q2** 两端同态 | **保持现状**（先试 `TrackToTrackdir(track)` 方向），不做任何额外判定 |
| **Q3** 站台例外范围 | 玩家判断"站台上不可能有普通轨位"⇒ 按我推荐的**只限整格位预留**；普通轨位即使落在站台格上也走"取非等挂"。**实现时该分支实际不可达**（见 13.8），保留代码仅为语义自证 |
| **Q4** 改动面 | **只给归属判据新增 `GetTrainForReservationArbitrated`**，**不动** 全局 `GetTrainForReservation`（审计/探针/REDSIG 归属读数保持逐字不变） |

### 13.8 由 Q3 引出的实现约束（读码确认，非拍板项）

`GetTrainForReservation(tile, track)` 第一行就 `assert_msg_tile(HasReservedTracks(tile, TrackToTrackBits(track)), ...)`，
而 `HasReservedTracks` → `HasTrack()` → `GetTrackBits()` 的 `dbg_assert_tile(IsPlainRailTile(tile))`（`rail_map.h:168`，第 110 轮附记 7b 已踩过）。
⇒ **plain-rail 入口的 tile 一定不是 `IsRailStationTile`**，站台例外在这条路径上是死分支。

站台/路点预留是**整格位**，走的是另一个入口 `GetTrainForWholeTileReservation(tile)`（`pbs.cpp` + `pbs.h` 声明）。
⇒ Q3 的"站台例外"要在**整格位那条入口**上落实；plain-rail 那条只保留分支以保持两入口语义一致。

**落地形态（下一步实现）**
1. `pbs.cpp` 新增 `Train *GetTrainForReservationArbitrated(TileIndex tile, Track track)`：
   - 两向各算一次 `A = R3RFollowReservationToOwner(tile, td0, rts)`、`B = ...(tile, ReverseTrackdir(td0), rts)`；
   - `A==nullptr` 返回 B、`B==nullptr` 返回 A（**与现状逐字一致**）；
   - `A->First()==B->First()` 返回 A；
   - `wa=IsWaiting(A)`、`wb=IsWaiting(B)`，`wa==wb` ⇒ 返回 A（Q2 保持现状）；
   - `IsRailStationTile(tile)` ⇒ 取等挂者，否则取非等挂者（Q1 只用 `OT_WAIT_COUPLE`）。
2. `pbs.cpp` 新增 `Train *GetTrainForWholeTileReservationArbitrated(TileIndex tile)`：整格位入口，同样的两端仲裁（站台例外在此真正生效）。
3. `train_cmd.cpp` 归属判据（`~17455-17472`）改调 `*Arbitrated` 版本；**其它所有调用点一字不动**。
4. `pbs.h` 加两条声明 ⇒ 按既定纪律走全量重编（护栏会自动清 obj）。

**状态**：已拍板，**开始改码**（本轮记为第 361 轮）。

### 13.9 实现落地（第 361 轮，2026-10-29，已改码 / 构建中）

**新增（`src\pbs.cpp`，全部在 `GetTrainForReservation()` 之后）**

| 行 | 符号 | 职责 |
|---|---|---|
| `2060` | `static bool R3RIsWaitingCoupleTrain(const Train *t)` | 等挂判据：`t->First()->current_order.IsType(OT_WAIT_COUPLE)`（Q1 只此一条） |
| `2072` | `static void R3RResvArbLog(...)`（`#if R3R_PROBES`） | 仲裁改判证据行 `RESV-ARB tile= station= a=vehN wait= b=vehN wait= picked=`，按格边沿去重 |
| `2106` | `static Train *R3RArbitrateReservationOwner(tile,a,b)` | 仲裁本体（见下） |
| `2137` | `Train *GetTrainForReservationArbitrated(tile,track)` | plain rail 入口：两向都算 → 仲裁；**防御式无 assert**（`!IsValidTile` / `!IsPlainRailTile` / `!HasReservedTracks` 一律 `nullptr`） |
| `2209` | `Train *GetTrainForWholeTileReservationArbitrated(tile)` | 整格位入口：保留"格上停车即主人"早退，之后每轨两向都算 → 仲裁（Q3 站台例外在此生效） |

**新增声明（`src\pbs.h`）**：紧跟 `GetTrainForWholeTileReservation()` 之后两条。

**唯一调用点改动（`src\train_cmd.cpp`）**
- `:17463` `GetTrainForReservation(tile, FindFirstTrack(b))` → `GetTrainForReservationArbitrated(...)`
- `:17469` `GetTrainForWholeTileReservation(tile)` → `GetTrainForWholeTileReservationArbitrated(tile)`
- 其它调用点（`RESV-AUDIT` / `R3RPurgeStrayReservations` / `R3RReleaseReservationSafe` / `REDSIG` 探针）**一字不动** ⇒ Q4 兑现。

**仲裁判定顺序（逐字）**
1. `a == nullptr` → 返回 `b`；`b == nullptr` → 返回 `a`（**与现状逐字一致**）；
2. `a->First() == b->First()` → 返回 `a`（同一条链）；
3. `wa == wb` → 返回 `a`（Q2 保持现状：`a` = `TrackToTrackdir(track)` 首试方向）；
4. `IsRailStationTile(tile)` → 返回等挂者，否则返回非等挂者（Q3/Q1）。

**已知边界**
- plain rail 入口的"站台格"分支实际不可达（`HasReservedTracks` → `GetTrackBits()` 的 `IsPlainRailTile` 断言），保留仅为两入口语义一致；
- 整格位入口的"格上停车即主人"早退**不参与仲裁**：站台上的等待者本就该赢（Q3 口径），且同格多列车的取车顺序由引擎瓦片链表决定、无法可靠仲裁（D-11）；
- 两端都有车但**两端格子里的车不止一列**时，`CheckTrainsOnTrack` 取 index 最小（KI-330）⇒ 仲裁输入可能已被污染，本轮未处理（见 13.5 风险 1）；
- `RESV-ARB` 只在两端都非空且状态不同时打（极罕见），不构成刷屏风险。

**状态**：已改码，read_lints 0 条，**已构建通过**（`pbs.h` 变更 ⇒ 护栏判全量重编）。

### 13.10 构建自证（第 361 轮）

- 入口：复用工作区既有 `R3R_fullbuild.cmd`（`inc detach`，未新建任何 `.cmd`）；构建前 `tasklist` 确认 `openttd.exe` 未运行。
- 护栏：`build\R3R_incbuild.guard.log` = `GUARD: 1 header/lang file(s) are newer than the newest object file`
  / `GUARD: newer: src\pbs.h (2026-10-29 16:13:35)` / `GUARD: REMOVED 620 object file(s) - upgrading this build to a FULL rebuild`。
- 结果：`[692/692] Building CXX object ... win32_main.cpp.obj` → `[692/692] Linking CXX executable openttd.exe`；
  `build\R3R_incbuild.done` = `EXIT_CODE=0`，日志尾 `==== R3R build ended 2026/10/29 16:38:28 MODE=inc EXIT_CODE=0`，错误计数 0。
- 产物：`build\openttd.exe` **2026-10-29 16:38（52 403 712 B）**；`pbs.cpp.obj` 16:32、`train_cmd.cpp.obj` 16:35
  均晚于对应源码（16:1x）。
- 字面量：`findstr /C:"RESV-ARB" build\openttd.exe` 命中（临时核对文件已删）。
- 未做：游戏内复测；`RESV-ARB` 只在"两端都有车且状态不同"时出现，属极罕见路径，复跑时若一次都不出现也**不能**据此判定修复未生效（先用 `RESV-FOREIGN-RES` 是否消失判断）。

## 十四、实机复测取证（第 362 轮，2026-10-29）—— REG-359-E **通过**

### 14.1 采样有效性

- 玩家原话：「见 build/R3R_debug.log，列车踩着预留还走错岔道已不再发生」。
- 输入：`build\R3R_debug.log` **205 728 B / 16:43**，产生者 `build\openttd.exe` **2026-10-29 16:38（52 403 712 B）**
  = 第 361 轮仲裁版（§13.10）⇒ **采样有效**（log mtime 晚于 exe，且 exe 晚于全部源码）。

### 14.2 判据逐条判读

| # | 判据 | 实测 | 结论 |
|---|---|---|---|
| ① | `38,11` 不再出现 `RESV-FOREIGN-RES veh=48` | 全日志 **0 条** `RESV-FOREIGN-RES ... tile=38,11` | **通过** |
| ② | 出现 `RESV-ARB ... picked=veh48` ⇒ 仲裁按拍板生效 | **5 条，5/5 `picked=veh48`** | **通过** |
| ③ | 归属修好后 `early-haverserved` 生效、沿自己预留走 | **2 条** `CPL-RET veh=48 ... why=early-haverserved` | **通过** |
| ④ | 站台整格位等待者仍保有归属 | 本次会话解挂/等待均在 plain rail（`35,12`），无站台整格位样本 | **未覆盖**（无样本，非失败） |
| ⑤ | `RESV-AUDIT-*` stray 无回归 | 未在本轮范围内取样（日志无 `RESV-AUDIT-SUMMARY` 行，审计间隔 2048 tick 未到） | **未覆盖** |

### 14.3 关键日志锚点（原文）

```
1737:RESV-ARB tile=39,11 station=0 a=veh0 wait=1 b=veh48 wait=0 picked=veh48
1770:RESV-ARB tile=38,11 station=0 a=veh0 wait=1 b=veh48 wait=0 picked=veh48
1794:RESV-ARB tile=38,12 station=0 a=veh0 wait=1 b=veh48 wait=0 picked=veh48
1815:RESV-ARB tile=37,12 station=0 a=veh0 wait=1 b=veh48 wait=0 picked=veh48
1870:RESV-ARB tile=36,12 station=0 a=veh0 wait=1 b=veh48 wait=0 picked=veh48
1771:CPL-RET veh=48 tick=13727 tile=38,11 enterdir=0 tracks=0x9 res=0x8 ret=0x8 why=early-haverserved spd=100 real=2
1816:CPL-RET veh=48 tick=13751 tile=37,12 enterdir=0 tracks=0x9 res=0x1 ret=0x1 why=early-haverserved spd=129 real=2
```

判读：

- `1770` 是本 bug 的**原发点**：`38,11` 两端都有车，`a=veh0 wait=1`（`OT_WAIT_COUPLE` 等待车底）、`b=veh48 wait=0`（跑 GOTO_COUPLE 的机车）。
  非站台 ⇒ 按 Q1/Q3 拍板取**非等挂者** ⇒ `picked=veh48` —— 与玩家第 360 轮给的规则逐字一致。
- 紧接着 `1771` 就是期望分支：`tracks=0x9`（`{X, LOWER}`）、`res=0x8`（LOWER）、**`ret=0x8`** ⇒
  本车沿**自己的预留 LOWER** 继续走，`why=early-haverserved` 说明 `r3r_couple_foreign_res` 已不再误置为真。
  这正是第 360~361 轮反复追的那个出口（旧行为是 `owned=false` ⇒ 屏蔽 early-haverserved ⇒ `FindFirstTrack(tracks=0x9)=0x1=X` ⇒ 物理走错岔道）。
- `1816` 是同一机制在下一格 `37,12` 的第二次复现（`tracks=0x9 / res=0x1 / ret=0x1`，走 TRACK_X 亦为自身预留）⇒ 不是孤例、机制稳定。
- 玩家现象「踩着预留还走错岔道」= 上述 `ret` 落在自己没预留的那一支；本轮 5 处仲裁全部 `picked=veh48` 后再无该形态，**与玩家口述一致**。

### 14.4 唯一残留的 `RESV-FOREIGN-RES` 已判定为「真外来」

```
1900:RESV-FOREIGN-RES veh=48 tile=35,12 enterdir=0 res=0x1 tracks=0x1 spd=134 real=2
1901:CPL-ENTRY veh=48 tile=36,12 orderType=16 dontReserve=0
1902:CPL-ORIGIN veh=48 origin=36,12 td=0 ...
1909:CPL-PATHFOUND veh=48 found=1
1912:CPL-BEST veh=48 tile=27,12 td=0 cost=8900
```

- `35,12` 不是漏判：该格预留 `TRACK_X` 属于 **veh0**（等待车底）。证据 `build\R3R_debug.log:609`
  `RESERVECONSIST veh=0 tile=35,12 track=0x1 fb=0 bits=0x1 ok=0 resNow=1`；且 `535` `GEO settle-decouple-u ... veh=0 tile=35,12 ... starF=1 segid=1` ⇒ veh0 解挂后**就停在** `35,12`。
- 即：`veh48` 在 `36,12` 想往 `35,12` 的 `TRACK_X` 走，而该位是 veh0 的合法预留 ⇒ 判「不是我的一格」**正确**，且此格**没有** `RESV-ARB` 行 ⇒
  说明仲裁只在"两端都有车"时才介入，此格属"只有一端有车且不是本车"，走原路径（Q4 口径：非仲裁路径零行为变更）。
- 下游行为也正确：`1901` 从 `36,12` 重新取起点 → `1909 CPL-PATHFOUND found=1` → `1912 CPL-BEST 27,12` ⇒ **绕开被占格继续寻路**，
  未出现"踩着别人预留硬走"的形态。

### 14.5 顺带观察（非缺陷，不计条目）

- `655:TPR-BLOCKED veh=57 tile=35,12 ... other=0 caronly=1` 紧跟 `656:TPR-SEAM-EXEMPT` 且 `671:TRP veh=57 ... => ok=1 res=1`
  ⇒ 第 221 轮的解挂缝豁免（KI-330 修法①）仍正常工作，未因本轮改动回退。
- `1904/1905 CPL-ORIGIN-FOLLOW ... ok=0` 是 veh48 在 `36,12` 的跟随器试探（`nbits=0x1 / 0x120`），属 GOTO_COUPLE 寻路正常过程，随后 `found=1`。

### 14.6 结论与状态

- **REG-359-E 实机复测通过**（判据 ①②③ 全部达成，④⑤ 因本次会话无对应样本而未覆盖，非失败）。
- 未覆盖项的取数方式（下次复跑若遇到）：④ 让等待车底停在**站台**上被挂再解挂，看该整格位归属是否仍归等待者、且 `RESV-ARB station=1`；
  ⑤ 让会话连续跑够 `2048×3` 未暂停 tick 后查 `RESV-AUDIT-SUMMARY ... strayTiles=`，与第 240 轮基线对比。
- 仍未处理（与本轮无关、维持 §13.5 风险 1）：`CheckTrainsOnTrack` 同格取 index 最小（KI-330）可能污染仲裁输入；
  D-1（两端都回溯失败）本规则管不到。

## 十五、REG-359-F 起：veh48 预留到「不安全停车位」（第 363 轮，2026-10-29）

### 15.1 玩家报告（原话）

> 列车（veh48）预留到不安全停车位的问题没有解决，请你检查 CheckSafePositionOnNode，看看这个函数是否出现问题

### 15.2 现场锚点（`build\R3R_debug.log`，205 728 B / 16:43，第 361 轮仲裁版 exe 产生）

```
 790:CPL-SAFE-FAIL veh=48 tile=27,12
 874:CPL-SAFE-FAIL veh=48 tile=27,12
 922:CPL-SAFE-FAIL veh=48 tile=27,12
1212:RPFAIL veh=48 tick=13505 origin=42,10 tile=41,10 td=0 unsafe=0 nTry=35 nOK=34
2086:CPLRES veh=48 tick=13790 origin=36,12 destTile=67,12 destTd=8 node=67,12 root=0 unsafe=0 nTry=25 nOK=25 empty=0
2087:RESV-TERM veh=48 real=7 type=1 tile=67,12 td=8 tcount=-2 safe=1 tail=0 spd=0 vtile=35,12
```

- `CPL-SAFE-FAIL` 出现 3 次，全部在 `tile=27,12`（挂接寻路回溯失败），而第 362 轮判定"走错岔道已不再发生"。
- 与之并存的是 `CPLRES`/`RESV-TERM` 两次**成功**预留：`destTile=67,12 destTd=8`、`tile=67,12 td=8 safe=1`。
  即 veh48 一边在 `27,12` 挂接寻路失败，一边把普通寻路预留订到了 `67,12`。

### 15.3 疑点登记（D 系列）

**D-1（疑点，高）：`FindSafeCouplePositionProc` 里没有任何 `IsSafeWaitingPosition` 判据。**
- 对照：普通寻路的 `FindSafePositionProc`（`src\pathfinder\yapf\yapf_rail.cpp:165-173`）唯一判据就是
  `IsSafeWaitingPosition(Yapf().GetVehicle(), tile, td, true, !TrackFollower::Allow90degTurns())`，
  命中即把该格写成 `res_dest_tile/res_dest_td` 并停止遍历。
- 而挂接版 `FindSafeCouplePositionProc`（同文件 `:181-3xx`）全文只在「该格已有人订了预留」这一前提下做
  owner/分组/第二占位者判定，**从不过问这一格本身是否是安全等待位**。
  ⇒ 若回溯经过的格**完全没有预留**（`HasReservedTracks()==false` 且 `GetReservedTrackbits()==TRACK_BIT_NONE`），
  整段判据被跳过、直接放行。"不安全"这一维度在这条路径上等于没有检查。

**D-2（疑点，高）：`FindSafeCouplePositionProc` 似乎不设置 `res_dest_tile` / `res_dest_td`。**
- `:181-274` 可见范围内只有 `return true/false`，没有一处写 `this->res_dest_tile`。
  若全函数皆如此，则挂接寻路的预留目标仍来自其他地方（待查 `res_dest_tile` 的全部赋值点），
  这就能解释 `CPLRES ... destTile=67,12`（一个与挂接目标 `27,12` 毫无关系的普通路径节点）
  与 `CPL-SAFE-FAIL tile=27,12` 并存。

**待验证**：D-1/D-2 需 grep `res_dest_tile` 全部赋值点后定性。

### 15.4 源码取证结论（D-1/D-2 均成立）

`res_dest_tile` / `res_dest_td` 在 `yapf_rail.cpp` 里**只有两个写点**：

| 写点 | 函数 | 说明 |
|---|---|---|
| `:170-172` | `FindSafePositionProc` | **普通寻路**的安全位 proc：`IsSafeWaitingPosition(...)` 为真才写目标 |
| `:576-581` | `SetReservationTarget(node, tile, td)` | 由回溯结束后**外部**显式指定目标格 |

⇒ `FindSafeCouplePositionProc`（`:181-460`，也就是 `CheckSafePositionOnNode` 的唯一实现体）**全程不写 `res_dest_tile`**。

`CheckSafePositionOnNode(node)` 本体只是转发：

```cpp
// yapf_rail.cpp:595 附近
bool CheckSafePositionOnNode(Node *node) {
    return FindSafeCouplePositionProc(Yapf().GetVehicle(), node->GetLastTile(),
                                      node->GetLastTrackdir(), ...);
}
```

`FindSafeCouplePositionProc` 的完整判据链（`:200-460`）：

1. `check_train_on_tile(tile)` 取该格上的列车 `best` / `second_best`；
2. `best == nullptr` 分支——**只有当该格已存在预留位**（`HasReservedTracks(tile, ...)` 为真或
   `GetReservedTrackbits(tile) != TRACK_BIT_NONE`）时才追问"这个预留是不是我的 / 是不是挂接目标的"；
   若无预留 ⇒ 整段判据被跳过（末尾 `return true`）；
3. `best != nullptr` 分支——`best` 是自己 ⇒ `true`；`!R3RIsCoupleTarget(best)` ⇒
   打 `FSCP ... fail=notSeg best= ord= sf=` 并 `false`；分组不许 ⇒ `false`；有 `second_best` ⇒
   打 `FSCP ... fail=2nd` 并 `false`；否则 `true`；
4. 走到底时按需回滚本次摸路自己订的那一格（`UNRES-RAIL ... phase=yapf-fscp`）。

**结论：这个函数里没有任何一处调用 `IsSafeWaitingPosition`。**
"这一格停着安不安全"这一维度在挂接回溯路径上等于完全没有判据——它只回答"这一格有没有别的车/别人的预留挡我"。

### 15.5 与普通寻路的分工对比（为什么这不是笔误，而是设计缺口）

| | 普通寻路 `FindSafePositionProc` | 挂接寻路 `FindSafeCouplePositionProc` |
|---|---|---|
| `IsSafeWaitingPosition` | **唯一判据**（`:165-173`） | 无 |
| 写 `res_dest_tile` | 是（命中即写） | **从不写** |
| 目标格来源 | 自己算出来的安全位 | 回溯结束后由 `SetReservationTarget(pNode, res_target_tile, ...)` 外部指定，`res_target_tile = (consist_head_tile != INVALID_TILE) ? consist_head_tile : pNode->GetLastTile()`（`:1262`） |

也就是说：**挂接寻路的预留终点是"车底链头所在格"（`consist_head_tile`），按定义它不可能是安全等待位**
（车底就停在那儿）。所以"补一条 `IsSafeWaitingPosition` 进去"不能直接照抄——那会让挂接永远失败。

⇒ 真正要定的是：**谁在什么情况下把预留终点写成了不安全位**。候选（待玩家确认现象后二选一）：

- **候选 A**：挂接寻路失败后 `train_cmd.cpp` 的降级路径（普通寻路）被喂了错误的目标格 ⇒
  该查 `_r3r_couple_path_blocked` / `CPL-PF-FALLBACK` 之后的 dest 从哪来；
- **候选 B**：挂接寻路成功但 `consist_head_tile` 本身不是安全位（车底停在不该停的地方，
  如道岔中央 / 桥隧口 / 平交道口），使其后整条链的等待位都不安全 ⇒ 该查
  车底解挂落点（`GEO settle-decouple-u`）而不是寻路。

### 15.6 本轮现场读数（支持候选 B 的旁证，未定论）

```
789:FSCP tile=36,12 fail=notSeg best=57 ord=6 sf=1
790:CPL-SAFE-FAIL veh=48 tile=27,12
791:CPL-BLOCKED-WAIT veh=48 tile=42,10 dest=35,12
794:CPL-RET veh=48 tick=13445 tile=41,10 enterdir=0 tracks=0x19 res=0x0 ret=0x1 why=cpl-fail
795:TRP veh=48 mf=42,10 real=2 type=16 dest=1571 spd=0 fp=0 look=0 one=1 mstuck=0 fto=1 => ok=0 res=0
796:COUPLE-FAIL loco=48 order=16 tx=42 ty=10
```

- 挂接目标 `CPL-HEAD best=27,12 head=35,12`，回溯在 `36,12` 被 **veh57** 否决
  （`ord=6` ⇒ 不是 `OT_WAIT_COUPLE`，`sf=1` ⇒ 是段头，但 `R3RIsCoupleTarget` 为假）。
- 于是 `_r3r_couple_path_blocked = true` ⇒ `CPL-BLOCKED-WAIT` 原地等（第 346 轮口径）。
- 若 veh57 自己也被别的东西钉死，则 veh48 无限等 —— 这是**死锁**而非"预留到不安全位"。
  **两者现象容易混**，需玩家确认到底是哪一个。

### 15.7 待玩家确认（C 系列）

- **C-1**：「预留到不安全停车位」的准确所指——是 (a) 预留终点格落在道岔/桥隧/道口等不该停的地方，
  还是 (b) 车被送到某格后卡住不动（本轮的 `CPL-BLOCKED-WAIT` 形态），还是 (c) 铁轨上出现黑色预留线
  却没人走的"幽灵预留"？
- **C-2**：出现该现象的 tile 坐标（日志里 `RESV-TERM` / `CPLRES` 的 `destTile=` 或界面上看到的那一格），
  我按坐标反查那条 `RP cur=` / `CPL-BEST` 序列。
- **C-3**：是否发生在 veh48 挂不上车之后（`COUPLE-FAIL` 之后）才出现？

## 十六、REG-359-G：`R3RIsCoupleTarget` 对「两段式链」的判据对象（第 363 轮续，2026-10-29）

### 16.1 玩家原话（本轮新增线索）

> 另一个对话里面的你建议我查 R3RIsCoupleTarget，而我认为可以怀疑，因为 veh48 的 coupletarget 是两段式的链

### 16.2 猜测根因（待验证）D-3

现场 `789:FSCP tile=36,12 fail=notSeg best=57 ord=6 sf=1` 走的正是
`src\pathfinder\yapf\yapf_rail.cpp:227` 的 `if (!R3RIsCoupleTarget(best)) return false;`。

三个读数各自的含义：

| 读数 | 含义 | 与假设的关系 |
|---|---|---|
| `best=57` | 该格上 index 最小的那一节车 | **不保证是链头，也不保证是段头**（`CheckTrainsOnTrack` 家族，KI-330） |
| `sf=1` | `best->IsSegmentFront()` 为真 | 说明它恰好是某段段头 —— **两段式链的第二段段头同样满足** |
| `ord=6` | `best->current_order.GetType()` | 不是 `OT_WAIT_COUPLE(17)`；`6` 疑为 `OT_GOTO_WAYPOINT` 一类 |

⇒ **假设**：两段式链只有**链头**在跑订单，非链头段的 `current_order` 是残留值。
若 `R3RIsCoupleTarget(best)` 直接读 `best->current_order`，真目标就会被判成 `notSeg`，
挂接寻路整条作废 ⇒ `CPL-SAFE-FAIL` ⇒ `CPL-BLOCKED-WAIT` ⇒ `COUPLE-FAIL` 循环。

正确形态应是二选一：
- **甲**：把 `best` 归一到链头（`best->First()`）再判订单（单一订单宿主口径）；
- **乙**：链上**任一段**持 `OT_WAIT_COUPLE` 即算目标（多段各自等待口径）。

选哪个取决于既有口径：第 152/161 轮确立的是「排程跟控制段走」⇒ 倾向**甲**（控制段=链头段），
但需先确认"控制段可能不是链头段"的旧情形是否仍存在（第 158 轮 KI-254 的甲/乙之争未结）。

### 16.3 待验证（按顺序）

1. **V-1**：`R3RIsCoupleTarget` 内部是否已做 `->First()` 归一（若已做，本条假设立即证伪，问题回到 §15）。
2. **V-2**：`best=57` 是不是 veh48 目标链的一节 —— 查同 tick 的 `FSCP-CPL tile=36,12 ... veh=` 与
   `FSCP-SELF` 遍历行（该两行会打印 `cpl_first` 与"哪些车在这格上"）。
3. **V-3**：目标链是否真为两段 —— 查 `GEO settle-*` / `SEG=` / `INVAR-CTRL` 读数。
4. **V-4**：`ord=6` 到底是哪个 `OrderType`（查 `order_type.h` 枚举顺序，不凭记忆）。

### 16.4 与 §15 的关系

§15 的结论（`CheckSafePositionOnNode` 不判 `IsSafeWaitingPosition`）**仍然成立且不冲突**：
- `CheckSafePositionOnNode` 是**通行许可**（有没有别人挡我）；
- `R3RIsCoupleTarget` 是**目标识别**（这节车是不是我要挂的那条链）。
两条都在同一条回溯里，任一条错都会导致 `CPL-SAFE-FAIL`，但错的性质不同：
前者"该不该等"、后者"该不该认"。本轮先攻后者（V-1 一句话就能定性）。

### 16.5a **订正**：「调用方漏归一」假设已被**证伪**（同一轮内自查，2026-10-29）

`FindSafeCouplePositionProc` 的 `check_train_on_tile`（`yapf_rail.cpp:196-205`）**已经是归一的**：

```cpp
for (Train *tr : VehiclesOnTile<VehicleType::Train>(t)) {
    if (tr->vehstatus.Test(VehState::Crashed)) continue;
    if (tr->track == TRACK_BIT_WORMHOLE || HasBit((TrackBits)tr->track, TrackdirToTrack(td))) {
        Train *head = tr->First();                                   // :200 已归一到链头
        if (best != nullptr && head->index != best->index) second_best = head;
        if (best == nullptr || head->index < best->index) best = head;
    }
}
```

⇒ 传给 `R3RIsCoupleTarget()` 的 `best` **恒为链头**，`:2684` 的 `Train::From(t->First()) != t` 不会触发。
§16.5 正文的推理作废（保留原文以记录否定路径，实际结论见下）。

**真因只剩三个候选**（按 `R3RIsCoupleTarget` 内部判据的先后顺序）：

| 候选 | 判据 | 现场读数 | 判定 |
|---|---|---|---|
| **N-1** | `:2667 !t->IsSegmentFront()` | `sf=1` ⇒ 带 ★ | **排除** |
| **N-2** | `:2695` `R3RChainHasRealArticPart(t)` 为真 | 无读数（需查） | **待验** |
| **N-3** | `:2700+` `current_order` 不是 `OT_WAIT_COUPLE` | `ord=6` ≠ 17 | **待验**（与 KI-319 家族同形） |

**N-2 与玩家直觉"两段式链"最贴**：两段极可能就是两个**真铰接单元**焊成的链，
而第 170 轮「含真 artic part 的链一律不是耦合目标」的修法会让这类目标**永远挂不上**。

### 16.5 V-1 结果（已作废，见 16.5a 订正）

`R3RIsCoupleTarget`（`src\train_cmd.cpp:2665`）本身**不做归一**，它把归一责任推给调用方，
并在注释里明写了这一点：

```cpp
bool R3RIsCoupleTarget(const Train *t)
{
    if (t == nullptr || !t->IsSegmentFront()) return false;          // :2667  必须有 ★
    if (Train::From(t->First()) != t) return false;                  // :2684  **必须是链头**
    if (R3RChainHasRealArticPart(...)) return false;                 // :2695+ 含真 artic 拒绝
    ...                                                              // :2700+ 还要求 WAIT_COUPLE
}
```

`:2675-2677` 的注释原文（这是决定性证据）：

> `- it could never be coupled onto anyway: the pair flags store chain heads only`
> `  (R3RPairCoupleTargets() normalises both sides through First()), and`
> `  the pathfinder normalises its destination the same way (yapf_destrail.hpp`
> `  does t = t->First() before asking here).`

⇒ **归一只有两个地方做了：配对标志（`R3RPairCoupleTargets()`）与目标检测（`yapf_destrail.hpp`）。
`FSCP` 回溯这一路没有归一。**

对照现场：

| 路径 | 传入 `R3RIsCoupleTarget` 的参数 | 是否归一 | 结果 |
|---|---|---|---|
| destrail 目标检测 | `t = t->First()` | 是 | 正确认链头为 candidate ⇒ `CPL-PATHFOUND found=1` |
| **FSCP 回溯**（`yapf_rail.cpp:227`） | `best`（该格上 index 最小的**任意一节**） | **否** | `Train::From(best->First()) != best` ⇒ `false` ⇒ `notSeg` |

**这就是"两段式链"必然失败的原因**：两段链的**第二段段头**满足 `IsSegmentFront()`（`sf=1`），
但它的 `First()` 是链头（另一节），于是 `:2684` 直接判死 —— 与 `ord=6` 无关
（`ord` 只是探针顺手打的，真正卡住的是 `:2684`，不是 `:2700` 的订单判据）。

单段链为什么没事：单段链上任何一节 `->First()` 都是链头，且链头带 ★ ⇒ `:2667`/`:2684` 双通过。

⇒ **缺陷定性**：`src\pathfinder\yapf\yapf_rail.cpp:227` 传的是"格上的车"，而接口要求"链头"。
KI-188 的语义（等待车底必须是独立列车）在**目标检测**侧是对的，**回溯侧**被误用成了"这一节车本身必须是链头"。

### 16.6 修法草案（未落地，待拍板）

- **修法 1（最小）**：`yapf_rail.cpp:227` 改为
  `if (!R3RIsCoupleTarget(Train::From(best->First()))) return false;`
  —— 语义不变（仍要求"独立列车 + ★ + WAIT_COUPLE"），只是把"格上的车"换成"它所属的链头"。
- **修法 2（连带）**：同一函数里 `:241` 的 `R3RCoupleAllowed(Train::From(Yapf().GetVehicle()), best)`
  与 `:245` 的 `second_best` 诊断行也应按链头取，否则分组判定会用错对象。
- **风险**：`best->First()` 若跨到了**本车自己的链**（`Yapf().GetVehicle()`），
  `:2670` 那条"被自己吞并的段"的语义会把它误认成目标 —— 但完整链路上游 `:222`
  `if (best->index == Yapf().GetVehicle()->index) return true;` 已先豁免，
  且 `R3RCoupleAllowed` 里有同链拒绝，需在改动时一并复核。

### 16.7 待补取数

- **V-2**：`789:FSCP tile=36,12 fail=notSeg best=57` 同 tick 的 `FSCP-CPL tile=36,12 ... veh=` 与
  `FSCP-SELF ... veh=` 行（该两行会给出 `cpl_first` 与"这格上有哪几节"），用来实证 `best=57` 的
  `First()` 不是 57 本身。
- **V-3**：目标链是否真为两段（`SEG=` / `GEO settle-*` / `INVAR-CTRL`）。
- **V-4**：`ord=6` 的枚举名（`order_type.h`，不凭记忆）。

### 16.8 定论（第 363 轮）：四道门与现场读数逐一对照 ⇒ **"两段式链"不成立**

`R3RIsCoupleTarget` 完整判据链（`src\train_cmd.cpp:2665-2714`，已逐行核）：

```cpp
bool R3RIsCoupleTarget(const Train *t)
{
    if (t == nullptr || !t->IsSegmentFront()) return false;   // 门1 :2667  ★
    if (Train::From(t->First()) != t) return false;           // 门2 :2684  必须是链头
    if (R3RChainHasRealArticPart(t)) return false;            // 门3 :2698  含真 artic 拒绝
    if (t->cur_speed != 0) return false;                      // 门4 :2712  必须停稳
    return t->current_order.IsType(OT_WAIT_COUPLE);            // 门5 :2713  必须正在等挂
}
```

| 门 | 判据 | 现场读数（`log:789 FSCP tile=36,12 fail=notSeg best=57 ord=6 sf=1`） | 判定 |
|---|---|---|---|
| 门1 | `IsSegmentFront()` | `sf=1` ⇒ 带 ★ | **通过** |
| 门2 | 必须是链头 | `best` 在 `:200` 已由 `tr->First()` 归一 ⇒ 恒为链头 | **通过** |
| 门3 | `R3RChainHasRealArticPart` | 无读数 | 未观测 |
| 门4 | `cur_speed == 0` | 无读数；`ord=6` 表明它正在赶路 ⇒ 疑为假 | 未观测 |
| 门5 | `current_order.IsType(OT_WAIT_COUPLE)` | **`ord=6`** | **必然 false** |

`order_type.h:83` 确认 **`ord=6` = `OT_GOTO_WAYPOINT`**（`OT_WAIT_COUPLE` = 17，`:94`）。

⇒ **`notSeg` 由门5（并疑连带门4）触发：veh57 此刻正在执行「去路点」，还没轮到等待挂接。**

**关于"两段式链"的判断：不成立。**
理由是 `check_train_on_tile`（`:196-205`）对格上**每一节**都取 `tr->First()` 再比较，
所以判据对象**恒是整条链的链头**，与这条链有几段无关：
- 两段链的**第二段**在回溯路径上被撞到 ⇒ `best` = 链头（在别格）⇒ 按整条链判，语义正确；
- 单段链与两段链在本函数里**没有任何分支差异**。

⇒ 真正卡住的是"目标还没就位"（门5），这属**设计内拒绝**，不是新 bug；
与 KI-319 家族（等待车底订单还没轮到 `WAIT_COUPLE` ⇒ 判假 ⇒ 挂接方原地等）同形。

### 16.9 由此产生的真问题（才是要修的东西）

若 `veh57` 就是 `veh48` 的目标（`CPL-HEAD head=35,12` 指向它），那么：
- `veh57` 在 `35,12` 且订单是 `GOTO_WAYPOINT` ⇒ 它**要去某个路点**才切 `WAIT_COUPLE`；
- `veh48` 在 `42,10` 等 ⇒ 只要 `veh57` 到不了那个路点，`veh48` 就永远等（死锁形态）；
- 若 `veh57` 已到路点却没切订单 ⇒ 订单推进问题（KI-193/COUPLE-DEST-EMPTY 家族）。

但若 `veh57` 只是**路过**（目标是 `veh0` 那台纯车底），则门5 拒绝它**完全正确**
（`TPR-BLOCKED veh=57 ... caronly=1` 说明 `35,12` 上另有 `veh0` 纯车底）。

**V-5（决定后续方向的唯一关键取数）**：`veh48` 的 `consist_head_tile = 35,12` 到底指向
`veh0`（纯车底，那 `veh57` 只是障碍，本轮无需改码）还是 `veh57`（那就是死锁，要查订单推进）。

### 16.10 **V-5 已得证：`35,12` 上同时有两列车，`best=57` 是"解挂留下的机车半"，不是挂接目标**

取自 `build\R3R_debug.log`（第 363 轮同一份日志），按行号排出的完整现场：

| 行 | 原文（截断） | 读出的判定 |
|---|---|---|
| `:463-502` | `LINEND veh=57 tile=35,12 ... spd=52→0 ... len=2 dir=5 db=0` | veh57 在 `35,12` 从 52 减速到 0（`len=2` = 2 节） |
| `:499` | `CPL-PAIRSCAN ... act=48 ... actTile=42,10 actSpd=0 ... near=57 nearDist=9 **nearWhy=not-target** nearTile=35,12 nearOrd=1 nearStop=0` | veh48 在 `42,10` 等；最近的候选是 veh57 但被标 `not-target` |
| `:517-520` | `ORD-IDX-WATCH ... veh=0 ... n=6->33 ord=...A8A0->...C590 ... vtile=35,12` | **veh0 也在 `35,12`**，并被并入 33 条共享表 |
| `:522` | `settle-in tag=decouple-v veh=57 n=33->7 ord=...C590->...AAB0` | veh57 是**解挂留下的前半**，拿回自己的 7 条表 |
| `:530-535` | `settle-in tag=decouple-u veh=0 n=33->33` + `GEO settle-decouple-u n=0 veh=0 tile=35,12 ... **starF=1** segid=1` | **veh0 是解出的车底，带 ★（段头）、段 ID=1**，拿整份 33 条表 |
| `:638` | `CPL-PAIRUNLOCK tick=13412 **why=decouple** act=57 actHead=57 actTile=35,12 ... tgt=0 tgtHead=0 tgtTile=35,12` | 解挂事件：前半 veh57、后半 veh0，**两者同在 `35,12`** |
| `:653` | `ORD-IDX-WATCH site=post-processorders veh=57 ... **curType=6** dest=1706` | veh57 解挂后立即去路点（`OT_GOTO_WAYPOINT`=6） |
| `:655-657` | `TPR-BLOCKED veh=57 ... other=0 otherTile=35,12 ... caronly=1` / `TPR-SEAM-EXEMPT` / `TPR-ZERO-REACH` | 同格另一台是 `veh0` 纯车底（`caronly=1`） |

⇒ **`FSCP ... best=57` 里的 57 就是这台刚解挂的机车 `veh57`，它与合法目标 `veh0` 共用 `35,12` 一格。**
veh48 要等的目标是 `veh0`（`starF=1` + `segid=1` + 持 33 条含 `WAIT_COUPLE` 的表），完全合法。

### 16.11 真缺陷：`best` 是"取 index 最小的一台"，不是"格上存在任一合法目标"

`FindSafeCouplePositionProc`（`src\pathfinder\yapf\yapf_rail.cpp:194-249`，已逐行核）：

```cpp
Train *best = nullptr;
Train *second_best = nullptr;
auto check_train_on_tile = [&](TileIndex t) {
    for (Train *tr : VehiclesOnTile<VehicleType::Train>(t)) {
        if (tr->vehstatus.Test(VehState::Crashed)) continue;
        if (tr->track == TRACK_BIT_WORMHOLE || HasBit((TrackBits)tr->track, TrackdirToTrack(td))) {  // :199 轨道筛选
            Train *head = tr->First();                                    // :200 归一
            if (best != nullptr && head->index != best->index) second_best = head;
            if (best == nullptr || head->index < best->index) best = head; // :202 取最小 index
        }
    }
};
...
if (!R3RIsCoupleTarget(best)) { ...fail=notSeg...; return false; }      // :227-234
```

两条独立缺陷叠加，任一都足以让这格被误判为"障碍"：

- **缺陷 1（轨道筛选，`:199`）**：`best=57` 而不是 `best=0`，说明 **veh0 没被计入**——
  若 veh0 被计入，`index 0 < 57` 必然使 `best=0`。即 veh0 站的那条 track 与本次查询的
  `TrackdirToTrack(td)` 不同位（解挂后两半被摆在同一格的不同轨上）。
  ⇒ 判据的"这台车挡不挡路"其实是按**单条轨道**问的，但结论却被当成**整格**结论用。
- **缺陷 2（单点量化，`:202`/`:227`）**：即使 veh0 被计入，只要遍历中出现 index 更小的
  非目标车并成为 `best`，`R3RIsCoupleTarget(best)` 为假就 `return false`，
  **格上另一台合法的等待车底被完全忽略**。`second_best` 只在 `:242` 作为"第三人也否决"用，
  从不参与"是否存在合法目标"的存在量化。

⇒ 这就是"veh48 一直挂不上 / 只能把预留停在别处"的直接原因，与"两段式链"无关，
也与 `CheckSafePositionOnNode` 本身无关（它只是转发）。REG-359-D 的"两端都有车"是同一族的
归属仲裁问题，本条是它的**镜像面**：不是"该给谁"，而是"格上有合法主人却因判据只取一台而整体否决"。

### 16.12 修法候选（未落地，待拍板）

- **候选 A（最小、推荐）**：把 `best` 的单点判据改成"存在量化"——遍历格上全部候选链头，
  只要**存在**一个 `h` 满足 `R3RIsCoupleTarget(h) && R3RCoupleAllowed(loco, h)` 即视为可通行，
  并把该 `h` 作为"这一格的目标"向上传（而不是固定用 index 最小的那个）。
  好处：不碰 `HasReservedTracks` / track 筛选语义，只改"哪台车说了算"。
- **候选 B**：`second_best` 的存在不再一律否决（`:242-249`），改为"仅当 `best` 是目标
  且 `second_best` 也占着同一条 track 时才否决"（按轨道而非按格）。
- **候选 C（治根）**：解挂后把两半摆到**不同格**（`GEO settle-decouple-u` 落点算法），
  但这只是让症状不易出现，不解决"同格多车"的通用判定，且属上游落点语义，风险高。

**未确认项**：
- U-1：`35,12` 是否站台格（若为站台，`:206-211` 会先扫站台其它格，`best` 的归属更复杂，
  需 `FSCP-CPL` / `FSCP-SELF` 行实证）。
- U-2：本次是否**每 tick** 重复该拒绝（`FSCP fail=notSeg best=57` 的计数），
  决定它是"偶发一次"还是"死等"。
- U-3：veh57 去完路点（`dest=1706`）之后是否切到 `WAIT_COUPLE`；
  若切了而 veh48 仍挂不上，则缺陷 1+2 就是唯一原因。

## 十七、第 364 轮：玩家拍板「多链头格暂存」（2026-10-29）

### 17.1 玩家原话（口径拍板，原文保留）

> 那就这样：如果一格上面有多个链头，扫描就暂存它，直到那一格只有一个链头，再扫（不用担心走开的那一个，反正它也进不了配对）

⇒ 这是对 §16.12 候选 A 的**收窄版**：不做"存在量化 + 拿选中那台当目标"，而是
**"该格链头数 ≥ 2 ⇒ 本帧跳过这一格，下一 tick 重新扫"**。
"直到那一格只有一个链头"在实现上不需要真的等待：`FindSafeCouplePositionProc` 每个 tick
都会被挂接寻路重新调用一次，"下一 tick 再扫"就是天然重试。
"不用担心走开的那一个" ⇒ 玩家明确接受"暂存期间这一格被当作可通行"带来的宽容。

### 17.2 落地判据（把玩家口径写成代码）

在 `HasReservedTracks(tile, tracks)` 分支内（`src\pathfinder\yapf\yapf_rail.cpp:193`）：

1. **收集整格链头，不按本次查询的轨道位筛选**。`check_train_on_tile()` 的 `:199`
   轨道筛选必须绕过——REG-359-H 缺陷 1 的现场（`best=57` 而非 `best=0`）就证明
   veh0 是因 track 不匹配而"不可见"，用它来数链头等于继续漏数。
   归一仍用 `tr->First()`，按 `index` 去重。
2. **收集范围与 `check_train_on_tile` 一致**：主格 `tile` + 站台回扫格
   （`:206-211` 的 `IsCompatibleTrainStationTile` 链），保持与既有 `best/second_best`
   同一视野；U-1（`35,12` 是否站台）不论答案如何都不改变行为。
3. **判据**：`n_heads >= 2` **且** 至少一个链头满足
   `R3RIsCoupleTarget(h) && R3RCoupleAllowed(loco, h)` ⇒ 暂存。
   附加这个"含合法目标"条件的理由：玩家原话"反正它也进不了配对"说的是**那台非目标车
   不是障碍的理由**，但整格若全是无关车（两台都进不了配对）时暂存就等于对普通占位列放水，
   与既有"散链/别组的等待车底也是障碍"的语义冲突。因此只对"格上确实有目标、只是被同格
   另一台车遮盖"这一形态暂存。
4. **动作**：`return true`（本格不阻塞回溯），且**不写** `res_dest_tile`/`res_dest_td`
   （不把多链头格当预留终点），因此不会把车头预留钉在这一格。
   `second_best`（`:242-249`）分支保持原样作兜底（只有 `n_heads` 采集漏掉时才可能到达）。
5. **探针**：`FSCP tile=x,y skip=multihead n= h=a,b`，按"格 + 最小两个车号"变化各打一行
   （同一格同一对车只打一次，避免刷屏）。

### 17.3 预期与风险

- 预期：`789 FSCP tile=36,12 fail=notSeg best=57` 消失，改为 `skip=multihead`（若该格确有两台），
  随后回溯继续 → `CPL-PATHFOUND found=1` → `COUPLE-OK loco=48`。
- 风险 1：暂存期间路径穿过该格，若另一台车长期不动，物理上会在接近时被 `CheckTrainCollision`
  拦下（不会穿模，但可能变成另一个"等"）。属玩家已接受的代价。
- 风险 2：若 `36,12` 实际只有一台车（veh57 跨格但 veh0 不在该格），本改动不触发，
  `fail=notSeg` 仍会出现 —— 那就说明"同格"前提在 `36,12` 上不成立，需回头查 U-1。

### 17.4 待验证（复跑判据）

1. `FSCP ... skip=multihead` 至少一行，且其 `tile` 为报错格（`36,12` 或 `35,12`）。
2. 该格不再出现 `fail=notSeg best=57`（同一 tick 内）。
3. 随后出现 `CPL-PATHFOUND veh=48 found=1` 与 `COUPLE-OK loco=48`。
4. 负判据：两台无关车（都不是目标）占同格时**不**出现 `skip=multihead`，仍按障碍否决。
5. 负判据：单链头格行为逐字不变（`fail=notSeg` / `fail=other` / `fail=2nd` 判据不动）。

## 十八、第 365 轮：§十七 改动的首次实机复测（2026-10-29）

### 18.1 采样有效性

- 日志：`build\R3R_debug.log`，**1855 行**。
- `src\pathfinder\yapf\yapf_rail.cpp` @ **2026/10/29 17:31:48**（111 270 B）
- `build\openttd.exe` @ **2026/10/29 17:55:14**（52 408 320 B）
- ⇒ exe 晚于源码，**本次采样确实含 §十七 改动**（玩家自行编译）。

### 18.2 判据结果

| §17.4 判据 | 结果 | 证据 |
|---|---|---|
| ① 出现 `FSCP ... skip=multihead` | **不达成** | 全文 `skip=multihead` **0 条** ⇒ 新分支一次都没命中 |
| ② 报错格不再 `fail=notSeg best=57` | **不达成** | `:768` / `:829` `FSCP tile=36,12 fail=notSeg best=57 ord=6 sf=1` 仍在 |
| ③ `CPL-PATHFOUND found=1` → `COUPLE-OK loco=48` | **达成** | `:941 CPL-PATHFOUND veh=48 found=1` → `:1717 COUPLE-OK loco=48 rear=26 consist=0 co=1 real=7 type=1 tx=35 ty=12` |
| ④ 两台无关车同格不放过 | 无从验证（无 multihead 行） | 与改动设计不冲突 |
| ⑤ 单链头格判据逐字不变 | **成立** | `:768` fail=notSeg、`:873` `FSCP tile=37,12 fail=tryReserve td=0 want=0 type=1 tbits=0x29 res=0x20 plain=1` 全部照旧 |

### 18.3 结论句（本次成功不是本改动起的）

1. 新分支未触发 ⇒ **挂车成功不是 `skip=multihead` 带来的**。
2. 成功那一次（`:941` 起）走的是另一条回溯：`CPL-SCAN ... last=27,12 lastveh=0 why=acc-car` →
   `CPL-BEST veh=48 tile=27,12 td=0 cost=12184` → `CPL-BACK 27,12 ← 38,12 ← 39,11 ← 40,10 ← 42,10`
   → `CPL-TRACE/REACH`，**整条回溯完全不经过 36,12**，因此没撞上 `best=57` 的否决。
   即：目标落点从 `36,12/37,12` 那一侧换到了 `27,12` 这一侧（车底 veh0 当时在 27,12，`PFD-ACCEPT kind=car-only` 三行连打）。
3. 前几次失败（`:762`/`:823`/`:868`）仍是老形态：`found=1` 后回溯撞上 36,12/37,12 → `CPL-SAFE-FAIL`。
   ⇒ 缺陷本体未消除，只是这次换侧后被绕开。

### 18.4 新疑点 REG-364-A（必须落盘）

`36,12` 上「格头数 ≥ 2」**从未成立**，否则 `:768` 那行不可能还是 `fail=notSeg`
（新分支排在 `:227` 之前，命中即 `return true`，不会有 fail 行）。
⇒ 现场证据倾向「**veh0 不在 36,12**」，与 §17.3 风险 2 一致（veh0 在 `35,12`，`36,12` 只有 veh57）。

但 `:873-877` 给出了同一批格上的逐格清单，形态值得记下：

```
:873 FSCP tile=37,12 fail=tryReserve td=0 want=0 type=1 tbits=0x29 res=0x20 plain=1 depot=0 st=0 self=0
:874 FSCP-RES tile=37,12 tk=5 owner=57 otile=37,13 odir=4 ospd=100 ostop=0 ord=6 oroi=3 oend=42,13 oendtd=8 othru=0 octgt=0 ocplok=0 cpl=42,10
:875 FSCP-VEH tile=37,12 veh=59 track=0x20 dir=4 front=0 ord=0
:876 FSCP-CPL tile=37,12 veh=48 pos=42,10 dir=5 spd=0 end=40,10 endtd=0 look=0 real=2
```

⇒ `37,12` 上 `owner=57`、可见车 `veh=59`（`front=0`、`ord=0`），且 `ospd=100`（57 在跑）。
**待验证（下一轮若要收口）**：`FSCP-VEH`/`FSCP-RES` 只反映"通过 track 筛选后的那一台"，
所以仍需一条「整格全部链头」的 dump 来判定 36,12 / 37,12 到底是几台。
在拿到它之前，**不要再断言"同格多链头"**。

### 18.5 下一步的三个选项（待玩家拍板）

- **P-1 收工**：本次已 `COUPLE-OK`，接受"换侧绕过"（缺陷留着，下次换场景可能复发）。
- **P-2 补整格链头 dump**（只读探针）：把 `note_tile_head()` 里 `n_tile_heads`/各 head id 打出来，
  一次复跑即可把 REG-364-A 判死，再决定是否保留新分支。
- **P-3 放宽口径**：格上链头 ≥2 且**全是非目标**时也放过（等于允许穿过静止的无关联车），
  这与"多链头暂存"不是同一件事，风险明显更大，不建议在无 dump 数据前做。

### 18.6 「改了但没跑」到底改变了什么行为（玩家 2026-10-29 追问的答复）

分三档，必须分清：

**A. 本次实测（未命中 `n_heads >= 2`）——行为逐字不变。**
- 判据全在 `if (n_tile_heads >= 2 && tile_heads_have_target)` 之内，条件不成立时下面原样走 `:227` 起的老逻辑。
- 日志印证：`:768` / `:829` 的 `FSCP tile=36,12 fail=notSeg best=57 ord=6 sf=1`、
  `:873` 的 `FSCP tile=37,12 fail=tryReserve td=0 want=0 type=1 tbits=0x29 res=0x20 plain=1`
  字段与改动前完全同形 ⇒ §17.4 判据⑤成立。
- 所以"没生效"= **新分支零命中**，而不是"命中了但没起作用"。

**B. 唯一无条件发生的变化——开销，不可见。**
- `HasReservedTracks` 分支现在每格多跑一遍 `note_tile_head()`：额外一次 `VehiclesOnTile` 遍历，
  并对每个链头调 `R3RIsCoupleTarget()` + `R3RCoupleAllowed()` 各一次。
- 两个函数都是**只读判定**：`R3RCoupleAllowed()`（`src\couple_group.cpp:597`）只做
  `R3RCouplePairMatches()` 匹配 + `R3RCoupleAllowedIgnoringPair()`，不打日志、不改状态
  （日志由调用方在 `yapf_destrail.hpp` 的闸门后打）。⇒ 不会污染 `PFD-*` 计数。
- 作用域仅限挂接寻路的 `CheckSafePositionOnNode` 回溯，不影响普通寻路、不写入任何地图状态。

**C. 一旦命中（本场景零样本）——变化很大，方向是"放过"。**
- 旧行为：该格按 `best`（`index` 最小的链头）判定，非目标 ⇒ `return false` ⇒ 整条回溯否决。
- 新行为：链头 ≥2 且含合法目标 ⇒ 直接 `return true`，**本帧把该格当可通行**，
  且不写 `res_dest_tile`/`res_dest_td`（不当终点）。
- 实际后果：路径可以规划穿过一个停着两台车的格子；真开过去时由 `CheckTrainCollision` 在物理层拦下，
  最坏退化为"开到跟前再等"——这是玩家原话「不用担心走开的那一个」已接受的代价。
- 该后果**本次一次都没发生**，因为前提（`36,12` 上有 ≥2 个链头）不成立（见 §18.4 / REG-364-A）。

**一句话**：A 档证明"没命中时零变化"，B 档是唯一无条件差异（开销），C 档是命中后才有的差异（本次没有样本）。

### 18.7 玩家假设 H-1："两链头曾在 35,12，扫描却当成 36,12，所以 35,12 上的修复没生效"

**玩家原话（2026-10-29）**：「veh0 和 57 并不是双方链头都在 36,12。相反，它们两个链头曾经挤在 35,12，所以，
是否是扫描认为两个链头挤在 36,12 所以导致 35,12 实际上没有生效修复呢」

#### 取证 1：「两链头曾在 35,12」——**成立，坐实**

同一时刻两链头都在 35,12，五处互证：

```
:602 RESERVECONSIST veh=0  tile=35,12 track=0x1 fb=0 bits=0x1 ok=0 resNow=1
:629 R3R-RES-ONCE grant head=57 side=v force_proceed=0 tile=35,12 stale=0
:630 R3R-RES-ONCE grant head=0  side=u force_proceed=0 tile=35,12 stale=1
:631 CPL-PAIRUNLOCK ... act=57 actHead=57 actTile=35,12 ... tgt=0 tgtHead=0 tgtTile=35,12
:528 GEO settle-decouple-u n=0 veh=0 tile=35,12 x=564 y=200 dir=5 ... segid=1 clen=2
```

⇒ veh57（机车半）与 veh0（车底半）解挂后**同格不同轨**站在 35,12，玩家的记忆准确。

#### 取证 2：`35,12` **从未进入判据函数**

全日志 `FSCP` 行只有三格：

```
:768 FSCP tile=36,12 fail=notSeg best=57 ord=6 sf=1
:829 FSCP tile=36,12 fail=notSeg best=57 ord=6 sf=1
:873 FSCP tile=37,12 fail=tryReserve td=0 want=0 type=1 tbits=0x29 res=0x20 plain=1 depot=0 st=0 self=0
```

`FSCP tile=35,12` **0 条**。⇒ 35,12 不是那条回溯的节点，判据在这格**根本没机会执行**。

#### 取证 3：`36,12` 上只有 veh57 一条链 ⇒ 扫描没有"错把 35,12 算成 36,12"

`note_tile_head()` / `check_train_on_tile()` 取车一律走 `VehiclesOnTile<VehicleType::Train>(t)`（`yapf_rail.cpp:208` / `:228`），
**按车辆自身的 `v->tile` 精确枚举，没有任何邻格推算或跨格合并**。所以 36,12 上出现的车是真的脚踩在 36,12。

那为什么 `best=57`（链头号）会出现在 36,12？因为 `best` 取的是 `tr->First()` 的 index（`:231`），
而 veh57 这条链**跨了 35,12 与 36,12 两格**：链头那一节停在 35,12（`:528 veh=0 ... ` 同格），后节压在 36,12
（`LINEND veh=57 tile=35,12 ... len=2 dir=5` 说明它在 35,12 上只有 2 半格=一整车，其余节已在别格）。
⇒ 在 36,12 枚举到的是 veh57 的**后节**，`First()` 回指链头 57，于是日志写成 `best=57`。

**反证（决定性）**：假如 veh0 也在 36,12，则 `n_tile_heads` 会是 2 且 veh0 是合法目标
（`R3RIsCoupleTarget`=真、`R3RCoupleAllowed`=真）⇒ 新分支命中 ⇒ 打 `skip=multihead` 并 `return true`，
**绝不可能打出 `fail=notSeg`**。而这行恰恰出现了两次。
⇒ 36,12 上只有 veh57 一条链，格归属没有错位。

#### 判定

| 假设要素 | 判定 | 依据 |
|---|---|---|
| 两链头曾在 35,12 | **成立** | 取证 1（5 条锚点） |
| 扫描误认为它们挤在 36,12 | **不成立** | `VehiclesOnTile` 按 `v->tile` 精确取车，无推算；`fail=notSeg` 反证 36,12 只有一条链 |
| 因此 35,12 上的修复没生效 | **结论正确、机制不同** | 35,12 上不是"生效了但没用"，而是 `FSCP tile=35,12` 0 条 ⇒ **判据从未在那格执行** |

**真正机制**：新分支挂在 `FindSafeCouplePositionProc()` 里，只由 `CheckSafePositionOnNode` 对**回溯经过的节点格**调用。
本次回溯的目标格是 36,12（等待车旁边的邻格），路径从 36,12 朝 `42,10` 方向走，
**35,12 不在节点集合里** ⇒ 那格的"两链头"从未被问过。

**这也是为什么本次绕开而非命中**：只要目标被选在等待车旁边的邻格，需要放行的就不是 35,12，而是那个邻格（36,12）。
36,12 上只有 veh57 一条链 ⇒ 按老逻辑判 `notSeg` ⇒ 目标被否 ⇒ 换侧到 27,12 后成功（§18.3）。

#### 新疑点 REG-364-B

35,12 为什么不在回溯节点：是否因为落点算法把"停在等待车邻格"当作合法终点、回溯从邻格出发时
不再把等待车自己那格纳入检查？（若纳入，就等于要求"人可以停在自己要挂的车身上"，必然失败。）
⇒ 属设计问题，**不是 bug**，本轮不改。若将来要收口，需先答：多链头格的放行，究竟要覆盖回溯节点，还是要覆盖"目标等待车所在格"。

#### 对下一步的影响

- **P-2（补整格链头 dump）依然值得做**，但它能回答的只是"36,12 上几台"——现在已被 `fail=notSeg` 反证为 1 台，
  收益下降；更有价值的是在 `note_tile_head()` 里打 `tile` + `n` + 各 head，用**同一次复跑**验证 35,12 是否真的算出 2。
- **新增 P-4（不建议）**：把 35,12 这类"等待车所在格"也纳入放行 —— 语义错误（等于允许把车开到等待车身上），不考虑。
- 现状可接受的解释：**缺陷本体在"邻格被无关车链的尾巴占用时不可作终点"**，而不是"多链头格没放行"。
  本次换侧成功即为该形态的旁证。

### 18.8 「安全挂接位置函数压根没看过 35,12？」—— 结论：是，且这一步可以判死

玩家 2026-10-29 追问：「也就是说，列车执行寻找安全挂接位置的函数压根没看过 35，12？」

#### 先说 §18.7 的表述要收紧一处

§18.7 写的是"35,12 从未进入判据函数"。严格讲，`FindSafeCouplePositionProc()` 存在**静默返回路径**：

- 看哪些格：`CheckSafePositionOnNode(node)` → `node->IterateTiles(Yapf().GetVehicle(), Yapf(), *this, &...FindSafeCouplePositionProc)`（`yapf_rail.cpp:654-661`）。
  ⇒ 覆盖范围是**该 yapf 节点走过的所有格**（含站台整段），不是"只有终点那一格"。
- 打日志只在三条路上：`HasReservedTracks(tile,tracks)` 为真的分支内（`fail=notSeg` :279 / `fail=2nd` :294 / `fail=other` :318 / `skip=multihead` :254）、
  以及 `else if (GetReservedTrackbits(tile) != TRACK_BIT_NONE)`（`fail=tryReserve`，:324 起）。
- **两者都不成立（该格连一个预留位都没有）时，函数静默走完，一个字都不打。**

所以"日志 0 条"单靠本身只证明"没进入带预留的分支"，不能直接推出"没被遍历到"。

#### 但本次可以补上决定性一环：`fail=2nd` 与 `skip=multihead` 双零

`35,12` 上确实同时停着 veh0 与 veh57（§18.7 取证 1），而且 35,12 是**有预留**的
（`:602 RESERVECONSIST veh=0 tile=35,12 track=0x1 bits=0x1 ok=0 resNow=1`）。因此只要有任何节点覆盖 35,12，
就必然落到 `if (HasReservedTracks(...))` 分支，且 `best` = index 最小的 veh0（合法目标）、`second_best` = veh57 非空 ⇒ 必然打出其一：

| 版本 | 必然出现的行为 | 日志实测 |
|---|---|---|
| 新代码（含多链头分支） | `n_tile_heads>=2 && tile_heads_have_target` ⇒ `FSCP tile=35,12 skip=multihead n=2 h=0,57` | **0 条** |
| 旧逻辑（无该分支） | `second_best != nullptr` ⇒ `FSCP tile=35,12 fail=2nd best=0 2nd=57`（`yapf_rail.cpp:291-298`） | **0 条** |

全日志 `FSCP` 只有 6 行，全部集中在 `36,12`（notSeg ×2）与 `37,12`（tryReserve/RES/VEH/CPL 各 1）。
⇒ **35,12 既没走新分支也没走老分支 ⇒ 它没有被 `IterateTiles` 覆盖过。** 玩家的判断成立，这一步可以判死。

#### 为什么没覆盖

回溯机位（`yapf_rail.cpp:1335-1347`）从 `pNode`（目标格 `36,12`）沿 `parent` 逐级往 `42,10` 方向走，
`CPL-BACK` 打印的链里没有 35,12；35,12 是**等待车自己站的那一格**，它与"从 36,12 通往 42,10 的路径"不在同一条节点链上。

⇒ 最终表述：**不是"看了 35,12 但判成没问题"，而是这条回溯的节点集合里根本没有 35,12**。
这也顺带解释了为什么新分支零命中是必然的——它所依赖的"同格两链头"现场，恰好落在判据的视野之外。

### 18.10 「先正着规划、再反着安全检查？那正着规划为什么不规划进（35，12）？」—— 结论：正着规划的终点是 27,12，35,12 只是"平台扫描"认出来的目标车底所在格

玩家 2026-10-29 追问：「尝试预留的时候，是先正着规划，再反着安全检查？那正着规划为什么不规划进站台的（35，12）？」

#### 一、阶段划分确认

| 阶段 | 入口 | 日志 | 做什么 |
|---|---|---|---|
| 正着规划 | yapf 搜索 `PfDetectDestination` | `CPL-ENTRY` / `CPL-ORIGIN` / `CPL-BEST` / `CPL-PATHFOUND found=1` | 找**落点格**，产出节点链 |
| 反着检查+预留 | back-walk（`yapf_rail.cpp:1335`） | `CPL-BACK` / `FSCP` / `CPL-RESERVE` | 逐节点验安全、逐格订位 |

#### 二、关键读数：正向规划的落点是 `27,12`，六次全一致

```
765:CPL-BEST veh=48 tile=27,12 td=0 cost=12784
826:CPL-BEST veh=48 tile=27,12 td=0 cost=12784
871:CPL-BEST veh=48 tile=27,12 td=0 cost=12484
944:CPL-BEST veh=48 tile=27,12 td=0 cost=12184
1398:CPL-BEST veh=48 tile=27,12 td=0 cost=11384
1669:CPL-BEST veh=48 tile=27,12 td=0 cost=8900
```

`27,12` / `35,12` / `36,12` / `37,12` / `38,12` 同在 y=12 这一条横向站台上（轴 = X）。
⇒ 规划从没打算"停在 35,12"，它停的是**同一站台另一端**的 27,12。

#### 三、35,12 在正向规划里的真实角色：不是落点，是"扫描出来的目标车底"

`PfDetectDestination()` 在落点格（站台格）上拿不到车时，会**沿站台轴**向两侧扫，
只要扫到满足 `R3RIsCoupleTarget()` 的链头就判该格是合法挂接终点：

```521:543:src/pathfinder/yapf/yapf_destrail.hpp
			for (int dir = 0; dir < 2 && t == nullptr; ++dir) {
				TileIndex st = tile + (dir == 0 ? delta : -delta);
				while (IsCompatibleTrainStationTile(st, tile)) {
					if (HasReservedTracks(st, TrackToTrackBits(track))) {
						...
						for (Train *tr : VehiclesOnTile<VehicleType::Train>(st)) {
							Train *first = tr->First();
							if (tr->IsFrontEngine() && R3RIsCoupleTarget(first)) { t = first; break; }
```

现场互证：配对层已经直接把目标钉在 35,12 上，而规划层落点仍是 27,12。

```
667:CPL-PAIRSCAN ... act=48 ... best=0 bestDist=9 paired=1 ... near=57 nearDist=9 nearTile=35,12 nearOrd=6
672:CPL-PAIRLOCK why=scan act=48 ... tgt=0 tgtHead=0 tgtTile=35,12 ... tgtOrd=17
```

⇒ **35,12 = 目标车底 veh0 所在格（不是终点格）；27,12 = yapf 选定的落点格（站台另一端）。**

#### 四、为什么落点不是 35,12 本身

1. **它在站台上是"中段格"**：yapf 的 TrackFollower 进入站台格时一次跳到段末端（`tiles_skipped`），
   节点落点因此天然在段端；27,12 正是这条站台的端头。
2. **它上面停着两列车**（veh0 + veh57，§18.7 取证 1）：把"别人车身所在格"当落点既不合物理
   （车头不能与车厢同格），也不会成为最小代价解。
3. 判据本身根本不需要落点=目标格：只要在同站台上能认出 `R3RIsCoupleTarget()`，该格即被接受。

⇒ 所以"正着规划不进 35,12"是设计使然，不是漏掉：**规划只负责把机车引到能挂接的同站台合法停车位，
35,12 是它认出的挂接对象，不是它自己要去占的格。**

#### 五、与 §18.8/§18.9 的衔接（三句话）

- §18.8：35,12 从未被 `IterateTiles` 覆盖过（`fail=2nd` 与 `skip=multihead` 双零判死）。
- §18.9：回溯只检查节点链，起点格（根）还额外免检。
- §18.10：**连正向规划都没把 35,12 当落点**（落点是 27,12）——所以从规划到检查，35,12 全程只以"被扫描到的目标车底位置"身份出现，从未成为路径的一部分。

#### 六、新疑点 REG-364-D

落点 27,12 是站台另一端（距目标车底 35,12 有 8 格），若站台另一侧也有别车/尽头，这个"远端落点"
会不会把机车引到离目标车底过远或错误的一侧？判据：看 `CPL-RESERVE path=/booked=`（本次 `path=8 booked=0`）
与后续 `CPL-RESERVE reserved=1 next=3 path=6 booked=6` 两次的差异，是否与落点端有关。待复核。

### 18.11 玩家对「正向规划 + 反向安全检查」整体逻辑的复述 —— 逐句核对（四句对三句，一处必须纠正）

玩家 2026-10-29 复述：「列车准备铺设预留了 → 先正向按'能预留到安全停车位的可能危险的路径'规划到 destination
→ 再反向从 destination 沿正向路径回来检查是否和别人的预留冲突（冲突只可能在道岔这类 YAPF 节点，所以只查节点）
→ 冲突就让正向重新规划。」

#### 一、四步逐句判

| # | 玩家原话 | 判定 | 依据 |
|---|---|---|---|
| 1 | 准备铺预留了，于是开始寻路 | **对** | 挂接连路分支进 `FindNearestCoupleTrain()` |
| 2 | 正向「按能预留到安全停车位的路径」规划到 destination | **半对** | 正向只管最小代价搜到 destination；**安全停车位完全不在正向里判** |
| 3 | 反向从 destination 沿原路回来检查 | **对** | `yapf_rail.cpp:1335` `while (pNode->parent != nullptr)` |
| 4 | 检查的是「和别人的预留冲突」，且「只可能是道岔这类节点，所以只查节点」 | **需纠正两点** | 查的是**格上车的身份**不是别人订没订位；节点用 `IterateTiles` 展开成**它覆盖的全部格** |
| 5 | 冲突就让正向重新规划 | **需纠正** | 不是同一次里重规划，而是整条作废 + `return INVALID_TRACKDIR`，下一 tick 重来 |

#### 二、纠正点 1：正向规划里没有"安全停车位"这个概念

```654:662:src/pathfinder/yapf/yapf_rail.cpp
	inline bool CheckSafePositionOnNode(Node *node)
	{
		dbg_assert(node->parent != nullptr);
		if (!node->IterateTiles(Yapf().GetVehicle(), Yapf(), *this, &CYapfReserveTrack<Types>::FindSafeCouplePositionProc)) {
			return false;
		}
```

`FindSafeCouplePositionProc` 只在**回溯阶段**被调用，正向搜索的策略函数里没有它。
正向唯一的"危险"感知是普通的轨道占用代价（yapf 的 occupancy penalty），那是**性能启发**、不是安全判据。
⇒ 正向会毫不犹豫地把路径铺到一辆不认识的车上，安全性 100% 由事后回溯否决。

#### 三、纠正点 2：检查的不是"预留冲突"，是"这格上停的是谁"

`FindSafeCouplePositionProc` 的判据链（`yapf_rail.cpp:181-` ）：

1. 车库格 ⇒ `fail=depot` 直接拒；
2. 取出该格 `best`（最近的列车）：
   - `best->index == 询路车自己` ⇒ **通过**（`:271`，自己会开走）；
   - `R3RIsCoupleTarget(best)` 成立 ⇒ **通过**（那是要挂的车底）；
   - `!R3RCoupleAllowed(...)` ⇒ **拒**（别人/别组的车，`:290`）；
   - `second_best != nullptr` ⇒ **拒**（一格两列车，`:294`）；
3. 格上无车但**有预留位**：只做一件事——占位者是本链车头则**不算阻塞**（`:324-335`），绝不放宽位级互斥。

所以"冲突"的语义是**「这格被不是我目标的车占着/停着」**，不是"别人订了这个位"。
预留位只在第 3 步作为辅助出现，且方向是**放宽**（自己挡自己不算数），不是收紧。

#### 四、纠正点 3：节点 ≠ 道岔；节点是一个"格集合"

`CheckSafePositionOnNode` 调 `node->IterateTiles(...)`，把该节点覆盖的**每一格**都喂给判据。
yapf 的节点在站台段跳跃时会一次跨 `tiles_skipped` 格 ⇒ 一个节点可能覆盖整段站台。
"只检查节点"这句话只在"以节点为单位调用"这一层成立；覆盖面是节点内的所有格，不是每节点取一个代表格。
（也正因如此，§18.9 的"根节点免检"才有份量：根节点那一格/那一整段都没被展开过。）

#### 五、纠正点 4：失败后的动作不是"重新规划"，是"整条作废 + 下一 tick 重算"

```1347:1361:src/pathfinder/yapf/yapf_rail.cpp
			if (!this->CheckSafePositionOnNode(pPrev)) {
				...
				_r3r_couple_path_blocked = true;
				return INVALID_TRACKDIR;
			}
```

- `return INVALID_TRACKDIR` ⇒ 本次挂接寻路**整体返回无路径**，不会在同一次调用里换个节点重搜；
- `_r3r_couple_path_blocked = true` ⇒ 通知 `train_cmd.cpp` **不要降级去绕别的股道**，原地等障碍离开；
- 下一 tick 重新进 `FindNearestCoupleTrain()`，此时 origin 已变（车可能挪了、障碍可能走了）⇒ 这才叫"重新规划"。

#### 六、还要补一条玩家没提到的顺序：**检查全部通过之后才统一预订**

while 循环里只做 `CPL-BACK` + `CheckSafePositionOnNode`；预订在循环**结束之后**由 `rp()` 用两遍法完成
（`yapf_rail.cpp:1318-1331` 的 S2 注释：第一遍只读摸路、第二遍按序列下单，避免边摸边订被自己订的轨挡住）。
⇒ 因果不是"查到冲突所以没订"，而是"**整条查完都没冲突，才开始订**"；中途任一节点否决 ⇒ 一格都不订。

#### 七、修正后的完整时序（一句话版）

> 正向按最小代价搜到 destination（**完全不管安全位、不管别人车**）
> → 从 destination 沿节点链回溯，对**每个非根节点的全部覆盖格**问"这格被谁占着、是不是我要挂的车底"
> → 全过 ⇒ 用两遍法把走过的格一次性订上；任一节点不过 ⇒ 整条作废、标 blocked、本轮不订、下一 tick 从新起点重来。

### 18.12 「什么是一个节点，为什么要按节点扫描」—— 节点 = YAPF 搜索图的一个状态（tile+trackdir），一格不是节点；一个节点可以是一段格

玩家 2026-10-29 追问。

#### 一、节点的数据结构：`(tile, trackdir)` 键 + parent + cost + segment

```102:122:src/pathfinder/yapf/yapf_node_rail.hpp
struct CYapfRailNode : CYapfNodeT<CYapfNodeKeyTrackDir, CYapfRailNode> {
	typedef CYapfNodeT<CYapfNodeKeyTrackDir, CYapfRailNode> base;
	typedef CYapfRailSegment CachedData;

	CYapfRailSegment  *segment;
	uint16_t          num_signals_passed;
	union {
		uint32_t        inherited_flags;
	...
```

`CYapfNodeKeyTrackDir` 就是 `(tile << 4) | trackdir`（`CYapfRailSegmentKey::Set`，`:34-37`）。

⇒ **节点 = "在 tile 这一格、朝 trackdir 这个方向"这一个搜索状态**，不是"一格"。
同一格有 2~6 个 trackdir ⇒ 同一格会对应 2~6 个不同节点（左/右/直/斜各算一个）。

#### 二、节点可以覆盖一整段格：`segment` + `IterateTiles`

```67:76:src/pathfinder/yapf/yapf_node_rail.hpp
struct CYapfRailSegment {
	typedef CYapfRailSegmentKey Key;

	CYapfRailSegmentKey key;
	TileIndex last_tile = INVALID_TILE;
	Trackdir last_td = INVALID_TRACKDIR;
	int cost = -1;
```

节点的 `segment` 指向一段"无缝轨道"（`CYapfRailSegment`，`key` = 段首 tile+td），段的另一端记在 `last_tile/last_td` 里。
所以一个节点真正代表的是 **`GetTile()/GetTrackdir()` → `GetLastTile()/GetLastTrackdir()` 这一整段**。

`IterateTiles` 就是把这个区间重新走一遍并逐格回调：

```176:193:src/pathfinder/yapf/yapf_node_rail.hpp
	template <class Tbase, class Tpf, class Tfunc>
	bool IterateTiles(const Train *v, Tpf &yapf, Tfunc func) const
	{
		typename Tbase::TrackFollower follower{v, yapf.GetCompatibleRailTypes()};
		TileIndex cur = this->base::GetTile();
		Trackdir  cur_td = this->base::GetTrackdir();

		while (cur != GetLastTile() || cur_td != GetLastTrackdir()) {
			if (!(func(cur, cur_td))) return false;

			if (!follower.Follow(cur, cur_td)) break;
			cur = follower.new_tile;
			...
		}

		return func(cur, cur_td);
	}
```

注意 `follower.Follow()` 会**跳过整段站台**（`tiles_skipped`），所以一个节点覆盖的格数可以是 1，也可以是十几格
（`GetNodeLength` 里 `length += TILE_SIZE * ft.tiles_skipped;`，`:215` 就是按这个算几何长度的）。

#### 三、为什么回溯要"按节点"而不是"按格数组"

| 理由 | 说明 |
|---|---|
| ① 搜索图本来就是节点 | YAPF 是 A*/Dijkstra 的变体，开放表/关闭表/父指针全都以节点为单位。路径的**唯一权威表示**就是 `parent` 链，不存在一份独立的"格数组" |
| ② 段内格是确定的 | 段内没有分叉（岔口/信号/站台端会切断段），从 `(tile,td)` 沿 follower 走必然唯一到 `last_tile`；所以不需要为段内每格再记一份状态 |
| ③ 检查粒度需求来自"格上的车"，但驱动单位仍是节点 | 判据 `FindSafeCouplePositionProc(tile, td)` 确实一格一格问，`IterateTiles` 只是"按节点驱动、逐格回调"，两者不矛盾 |
| ④ cost/父指针只在节点上 | 回溯要顺着 `parent` 往上走，只能以节点为步进单位 |

⇒ 一句话：**"按节点扫描"不是为了让检查变粗，而是因为 YAPF 只有节点链这一份路径数据；节点内部再用 `IterateTiles` 展开成格。**

#### 四、顺手纠正一个说法：「冲突只可能在道岔这些节点」

- 道岔格确实常是节点边界（分叉 ⇒ 段必须断），但**它同样被 `IterateTiles` 展开成一格**去检查；
- 真正的节点边界不只有道岔：信号、站台末端、`EndSegmentReasons` 记录的各种原因都会切断段；
- 因此"只查节点"的准确含义是"**以节点为步进单位，节点内展开全部格**"，而不是"每节点只查一个代表格、跳过其余格"。

#### 五、本次现场对应的具体节点链（可回填验证）

`CPL-BACK pPrev=... parent=...` 每行就是一次节点跳转；若某行 `pPrev` 与 `parent` 的 tile 相差多格，
说明这一跳跨了站台（`tiles_skipped>0`），那中间那些格**只在这一跳的 `IterateTiles` 里被检查**。

### 18.13 玩家猜测：「跳过起点节点 ⇒ 等挂的车底正好在起点节点，所以没被扫到」—— 代码事实成立、推理链不成立

玩家 2026-10-29 追问。

#### 一、代码事实：根节点确实一次也没被检查（成立）

```1335:1362:src/pathfinder/yapf/yapf_rail.cpp
		while (pNode->parent != nullptr) {
			pPrev = pNode;
			pNode = pNode->parent;
			++r3r_hops;
			...
			if (!this->CheckSafePositionOnNode(pPrev)) {
```

`pPrev` 的取值序列 = 目标节点 → … → 根节点的**子**节点；`pNode` 变成根节点后循环退出。
⇒ `CheckSafePositionOnNode` 从未作用于根节点（= origin 所在的节点），根节点的覆盖格（origin 到该段末）**完全免检**。
玩家这一半判断是对的，且与 §18.9 的记录一致。

#### 二、但「等挂的车底在起点节点」这条推理不成立（分三种情形）

| 情形 | 实际行为 | 是否走到回溯 |
|---|---|---|
| ① 车底与机车**真共格**（目标就是 origin 所在的那一节） | `pNode->parent == nullptr` 走零长度分支，`return INVALID_TRACKDIR` | **不进入**（`:1299-1307`） |
| ② 车底在起点节点**段内的中间格**（起点段被 `tiles_skipped` 整段跳过，如站台） | 根节点免检 ⇒ 该格确实一次没被问 | 进入，但跳过该格 |
| ③ 车底是**独立的另一段**（本次 veh48 场景） | 根节点只覆盖机车自己那一段 ⇒ 车底不在根节点里 | 进入，与根节点无关 |

⇒ 玩家的假设只有在**情形②**下才可能成立；情形①会提前 return、连 `CPL-BACK` 都不会打。

#### 三、真正的漏洞仍然是「根节点段免检」，但受害者是**别人占的车**不是目标车底

根节点段免检 = 若起点段内（尤其被站台跳跃跨过的中间格）停着**非目标**的车，判据一次不问，
就会算出"路径安全"并预订——这是 §18.9 记录的漏洞。
而目标车底若真在那一段里，正向搜索的 destination 判据本身就会先命中（走情形①的零长度分支），
**不会**表现为"路径找到了却像没看见车底"。

#### 四、待验证（不得当结论）

#### 三补、`found=` 语义已核对：它排在回溯**之前**，所以本猜测对本次现象不适用

```1218:1221:src/pathfinder/yapf/yapf_rail.cpp
		if (!path_found) return INVALID_TRACKDIR;

		/* Found a destination, set as reservation target. */
		Node *pNode = Yapf().GetBestNode();
```

`found` = `Yapf().FindPath(v)` 的返回值（`:1204`/`:1208`），即"正向是否搜到 destination"。
`found=0` 时 `:1218` 直接 `return INVALID_TRACKDIR`，**根本走不到回溯循环** ⇒ 根节点免检那句话在此情形下无从谈起。

#### 三补 2、但玩家的猜测内核指向另一条真实分支：共格/同段末端 ⇒ 「零长度」→ 报无路径

```1299:1307:src/pathfinder/yapf/yapf_rail.cpp
			if (pNode->parent == nullptr) {
			/* R3R: zero-length couple path - the best node is the origin itself
			 * (the locomotive already shares a tile with the waiting consist's
			 * coupling end). There is no path segment to walk back or reserve;
			 * report no track so ChooseTrainTrack parks the locomotive and the
			 * collision/couple handlers perform the physical join. Without this
			 * guard pPrev stays nullptr and the walk-back below dereferences it. */
			return INVALID_TRACKDIR;
		}
```

若车底就躺在起点节点上（满足玩家假设的前提），YAPF 的最优节点 `pNode` 就是根节点 ⇒ 这里 `return INVALID_TRACKDIR`。
对外表现与"没找到路径"**完全一样**（上层只知道"挂接寻路返回无轨道"），但实际是"已经贴上了、不需要铺路"。
⇒ 玩家直觉里"车底在起点所以没被处理"的现象**确实存在**，但机制不是"免检漏扫"，而是"零长度分支把成功报成失败"。

| 现场形态 | 含义 |
|---|---|
| `CPL-PATHFOUND found=0` + 无任何 `CPL-BACK` | 正向就没搜到 destination（闸门/距离/主搜索上限），**与免检无关** |
| `CPL-PATHFOUND found=1` + 无 `CPL-BACK` + 无 `CPL-RESERVE` | 命中零长度分支（车底已在起点节点上）⇒ 报 INVALID_TRACKDIR |
| `CPL-PATHFOUND found=1` + 有 `CPL-BACK` 串 + 末行 parent 为根 | 正常回溯，根节点免检生效（此时才需查起点段内是否有第三方车） |

#### 四、待验证（不得当结论）

- [x] 本次现场是上表哪一行：**玩家 2026-10-29 回答「全日志没有 found=0，只有 found=1」** ⇒ 见 §18.14。

### 18.14 玩家实测口径：全日志 `found=1`，`found=0` 一条都没有 —— 正向搜索失败与本场景**彻底无关**

玩家 2026-10-29 原话：「见 R3R_debug.log，全日志没有 found=0，只有 found=1」。

#### 一、这条口径一次性排除掉的东西

| 之前列过的可能 | 结论 |
|---|---|
| 「距离/主搜索上限/闸门把候选筛掉了」导致 YAPF 搜不到 | **排除**（那会打 found=0） |
| `R3RCoupleScanDump` 里"等待链不在场"的解释 | **排除** |
| 我在 §18.13 表里的第 1 行 | **排除** |
| 「跳过根节点 ⇒ 车底在起点节点 ⇒ 没扫到」中的"正向没看见车底"路径 | **排除**（正向已达 destination，说明 destination 判据命中了车底） |

⇒ 现场必然是 §18.13 表的第 2 行或第 3 行：
**要么命中零长度分支（`pNode->parent == nullptr` → `return INVALID_TRACKDIR`），要么真的走完了回溯。**

#### 二、所以「跳过起点节点」这件事现在**变成了主嫌疑**（但机制必须改写）

`found=1` 说明：
1. YAPF 搜到了 `<destination>`（车底那一格）；
2. 于是 `GetBestNode()` 非空，进入 `:1299` 那段；
3. 若 `pNode->parent != nullptr` ⇒ 进入 `while` 回溯，**根节点（= 起点节点，含 origin 到该段末的整段格）从头到尾没交给 `CheckSafePositionOnNode`**。

⇒ 玩家的原话判断在这一层是对的：**起点节点确实一次没被查**。
但与玩家表述的差别在"等挂的车恰好在起点节点"这一句：**车底不在起点节点，而是"从起点节点出不去"**。
`found=1` 已证明车底被目的地判据看见了（否则不可能 found）。

#### 三、本轮真正要回答的问题（改写）

不是"有没有扫到车底"，而是：
- `found=1` 之后有没有 `CPL-BACK`？
  - **没有** ⇒ 命中零长度分支，`return INVALID_TRACKDIR`，上层看到"无轨道" ⇒ 这才是"找到了却像没找到"的唯一形态；
  - **有，但随后 `CPL-SAFE-FAIL` / `CPL-REFUSE` 打出来** ⇒ 回溯被第三方车否决（`_r3r_couple_path_blocked`），与根节点免检无关；
  - **有，且一路无 SAFE-FAIL、有 `CPL-RESERVE`** ⇒ 路径与预订都成功，问题在预订之后（走不动/被别的车挡/订单没推进）。

#### 四、待验证（不得当结论）

- [ ] `CPL-BACK` 在日志里的条数（0 条 = 零长度分支；>0 = 真回溯）
- [ ] `CPL-SAFE-FAIL` / `CPL-REFUSE` 条数与 tile
- [ ] `CPL-RESERVE` 条数与 `reserved=` 值

### 18.15 实测计数（2026-10-29，`build\R3R_debug.log`，玩家确认全日志无 found=0）

| 探针 | 条数 | 落盘文件 |
|---|---|---|
| `CPL-PATHFOUND` | **6**（全部 found=1） | `build\r359_cnt_b.txt` |
| `CPL-BACK` | **11** | `build\r359_cnt_a.txt` |
| `CPL-SAFE-FAIL` | **3** | `build\r359_cnt_a.txt` |
| `CPL-RESERVE` | **3** | `build\r359_cnt_b.txt` |
| `CPL-PF-FALLBACK` | **1** | `build\r359_cnt_b.txt` |

（取数命令：`Select-String -SimpleMatch` + `@(...).Count` 写盘；`find /c "..."` 在本环境会把 pattern 当文件名，不可用。）

#### 一、判定：回溯**确实发生** ⇒ 零长度分支被排除

`CPL-BACK` 11 条 ⇒ `while (pNode->parent != nullptr)` 至少跑了 11 次。
⇒ **不是** `:1299` 的 `pNode->parent == nullptr` 提前 `return INVALID_TRACKDIR`。
⇒ §18.13 表的"第 2 行（零长度）"被排除，现场落在"第 3 行：真回溯"。

#### 二、判定：玩家的猜测（根节点免检 ⇒ 车底没扫到）**因果不成立**

关键论据不是"车底不在起点节点"，而是——**起点节点那几格上停的就是机车自己，扫了也会通过**：

`CheckSafePositionOnNode` 逐格调 `FindSafeCouplePositionProc`，该函数在 `notWC` 判定之前有自身豁免
（第 45 轮修复，`yapf_rail.cpp`，记忆 83488206）：
```text
if (best->index == Yapf().GetVehicle()->index) return true;
```
根节点 = origin = 机车**当前所在**的 `(tile, trackdir)`，它覆盖的格从 origin 开始，
**整段都是机车自己**（或机车刚走过的、已被自身预留保护的格）。
⇒ 把根节点补进循环，这 11 次回溯的结果**一条都不会变**。
⇒ 玩家的假设即使按最有利的方式成立，也解释不了唯一的失败面（3 条 `CPL-SAFE-FAIL`）。

#### 三、真正的失败面已经浮出：6 次寻路里有 3 次被安全位否决

`PF=6 / BACK=11 / SAFE-FAIL=3 / RESERVE=3 / FALLBACK=1` 的形态说明：
- 每次寻路平均不到 2 跳 ⇒ 路径极短（起点几乎直达车底），与"跳过根节点"的量级矛盾：
  漏检窗口只有 1 段，而否决发生了 3 次，否决点必然落在**目标节点或中间节点**，不是根节点。
- `SAFE-FAIL` 打出来时 `_r3r_couple_path_blocked = true`（`:1359`），整条路径作废、不预订。
- 3 条 SAFE-FAIL 与 3 条 RESERVE 数量相同 ⇒ 疑似"否决—重试—再成功"或"部分成功部分被否"，
  需按行号配对确认（**未取，命令被用户取消**）。

#### 四、待验证（不得当结论）

- [x] 3 条 `CPL-SAFE-FAIL` 的 `veh/tile`：占位者是谁（自己 / 目标车底 / 第三方）—— 见 §18.16，全部是**第三方 veh=57 链**
- [x] 11 条 `CPL-BACK` 按 6 次 `CPL-PATHFOUND` 分组：见 §18.16 表
- [x] 3 条 `CPL-RESERVE` 的 `reserved=/next=/booked=` 与 1 条 `CPL-PF-FALLBACK` 的 `ret=`：见 §18.16 表

### 18.16 取证完成（2026-10-29，玩家授权只读）—— 否决点全部在**站台段内的 36,12 / 37,12**，占位者是过路的 veh=57

数据落盘：`build\r359_ctx.txt`（24 个匹配 + 后 3 行，400 列宽）、`build\r359_safe.txt`（3 条 SAFE-FAIL 前后各 6 行）。

#### 一、六次寻路的完整链条（逐行来自日志，行号即 `build\R3R_debug.log`）

| # | 行 | PATHFOUND | nodes | BACK 跳 | 否决/结果 | RESERVE |
|---|---|---|---|---|---|---|
| 1 | 762-772 | found=1, BEST=27,12 | 138 | 1 跳 `27,12→38,12` | `FSCP 36,12 fail=notSeg best=57 ord=6 sf=1` → SAFE-FAIL | 无（why=cpl-fail） |
| 2 | 823-833 | 同上 | 138 | 1 跳 | 同一个 `notSeg best=57` → SAFE-FAIL | 无 |
| 3 | 868-880 | found=1 | 138 | 1 跳 | `FSCP 37,12 fail=tryReserve tbits=0x29 res=0x20` + `owner=57 otile=37,13 spd=100 ord=6` → SAFE-FAIL | 无 |
| 4 | 941-1193 | found=1 | 132 | 4 跳 `27,12→38,12→39,11→40,10→42,10` | 回溯**到达根节点** | `reserved=0 next=0 path=8 booked=0`，why=cpl-partial-res |
| 5 | 1395-1600 | found=1 | 129 | 3 跳 `27,12→38,12→39,11→40,10` | 路径通过（未再查 36/37 那两格） | **`reserved=1 next=3 path=6 booked=6`**，ret=0x8 nores-final spd=5 |
| 6 | 1666-1680 | found=1 | **5**（closed=1） | 1 跳 `27,12→36,12` | `CPL-FIRST-TD ok=0 best=0` | `reserved=0 next=0 path=0 booked=0` → `CPL-PF-FALLBACK ret=0x0 dest=-1,-1` |

#### 二、结论一：**根节点免检与本次现象无关（玩家猜测正式证伪）**

否决点全在 `36,12` / `37,12`，而这两格位于回溯**第一跳**（`pPrev=27,12 → parent=38,12`）的节点覆盖范围内——
`IterateTiles` 把 `27,12..37,12` 逐格交给了 `FindSafeCouplePositionProc`（这正是 `FSCP tile=36,12` / `tile=37,12` 的来源），
**说明中间格被查到了**，不是漏查。根节点（`42,10`）是最后一跳，第 4 次回溯就到达了它，结果与失败无关。

#### 三、结论二：两次 `notSeg` 拒绝是**设计正确**，被拒的是过路车不是目标车底

```276:283:src/pathfinder/yapf/yapf_rail.cpp
				if (!R3RIsCoupleTarget(best)) {
					...
					fprintf(dbg, "FSCP tile=%d,%d fail=notSeg best=%d ord=%d sf=%d\n", ...);
					return false;
				}
```

日志 `best=57 ord=6 sf=1`：`sf=1` 只说明 57 是它自己那条链的段头，`ord=6` 不是 `OT_WAIT_COUPLE(17)`，
⇒ `R3RIsCoupleTarget(57)` 为假 ⇒ 判据把 `36,12` 当障碍是**按设计执行**。
目标车底的 `vOrd=17` 见 `:1602` 的 `CPL-PAIRMAP ... v=0 vTile=35,12 vOrd=17 vLen=107 vPairR=48`。

#### 四、结论三：`tryReserve` 那次是**过路车正在通过**，不是停车占位

`FSCP-RES tile=37,12 tk=5 owner=57 otile=37,13 odir=4 ospd=100 ostop=0 ord=6`：
57 号车在 `37,13`、**速度 100 正在行驶**、`ostop=0`，它预订了 `37,12` 的 `TRACK_RIGHT(0x20)`。
⇒ 这是临时占道，等它开走即可；第 5 次寻路（tick 13676）正是这种情形消失后的结果：`reserved=1 booked=6`。

#### 五、结论四：真正的疑点转移到**「预订成功之后」**（新登记，非本次可结）

- 第 5 次 `CPL-RESERVE reserved=1 next=3 path=6 booked=6` ⇒ 预留拿到 6 段，
  随后 `CPL-RET ... res=0x8 ret=0x8 why=nores-final spd=5`，**但没有 `COUPLE-OK`**。
- 第 6 次 `nodes` 从 129 骤降到 **5**、`path=0`，随后才 `CPL-PF-FALLBACK ret=0x0 dest=-1,-1`、
  `CRT veh=48 order=16 dir=5 origin=36,12 td=0 found=1`、`RESV-LA-REBUILD ... spd=132`。
  ⇒ 目标可达面在这一刻塌缩，形态与 §18.11 的 `_r3r_couple_path_blocked` 降级路径不同（这次是 `path=0` 而非被否）。

> 登记为**疑点 REG-359-K（待验证）**：第 5 次预留成功（booked=6）之后为何无 `COUPLE-OK`，
> 以及第 6 次为何 `nodes=5/path=0`（目标车底是否被推走、或 `origin` 从 `42,10` 变成 `36,12` 导致同一 search 只剩 5 节点）。
> 需要的数据：第 5 次之后到第 6 次之间的 `RESV-PAIR-AIR / CPL-PAIRMAP / TRP veh=48` 连续段（未取）。

### 18.17 玩家追问「第四次成功了，为什么列车没有铺预留」——**第四次根本不算成功：路径成功、预订失败**

玩家 2026-10-29 追问原话：「既然第四次成功了，那为什么列车没有铺预留」。

#### 一、读数：第四次是 `reserved=0 next=0 path=8 booked=0`（`build\R3R_debug.log:1190`）

```1190:1193:build\R3R_debug.log
CPL-RESERVE veh=48 reserved=0 next=0 path=8 booked=0
CRT veh=48 order=16 dir=5 origin=42,10 td=0 found=1
CPL-RET veh=48 tick=13505 tile=41,10 enterdir=0 tracks=0x19 res=0x1 ret=0x1 why=cpl-partial-res spd=0 real=2
RESV-END veh=48 tick=13505 from=42,10 endTile=40,10 endTd=0 endPos=37 curPos=0 endBits=2121 junction=1 real=2 spd=0
```

`reserved=0 / booked=0` ⇒ **一条位都没留下**。所谓"第四次成功"只成立在 `CPL-PATHFOUND found=1`（路径规划）+ 回溯通过安全位这两步，
第 4 次与第 5 次（`:1599` `reserved=1 next=3 path=6 booked=6`）的区别正在预订这一步。

#### 二、机制：预订是**两遍法的第二遍**，且失败即整批回滚

`rp()` 自己一格都不订（KI-504 E1b，代码内注释自陈）：

```1502:1520:src/pathfinder/yapf/yapf_rail.cpp
					/* R3R (KI-504, E1b): rp() is read-only - it deliberately books NOTHING.
					 * ...
					if (!IsRailStationTile(cur) && !IsRailDepotTile(cur)) {
						/* 只记录物理走过、且需要预订的格：站台/车库不订（与 rp_last_tile
						 * 的口径一致——预留止于会合点，不越过车底）；起点格也不订
						 * （TryReservePath 的节点链同样跳过根节点）。 */
						if (cur != st || depth != 0) r3r_resv_path.push_back(R3RCoupleStep{cur, cur_td});
					}
```

真正订位在 `R3RReservePathSequence()`，**任意一格失败就把已订的全部撤回**：

```606:623:src/pathfinder/yapf/yapf_rail.cpp
	bool R3RReservePathSequence(const std::vector<std::pair<TileIndex, Trackdir>> &steps, size_t &booked)
	{
		booked = 0;
		...
		for (const auto &step : steps) {
			this->ReserveSingleTrack(step.first, step.second);
			if (this->res_fail_tile != INVALID_TILE) {
				for (size_t i = booked; i > 0; --i) {
					UnreserveRailTrackdir(steps[i - 1].first, steps[i - 1].second);
				}
				booked = 0;
				return false;
			}
			++booked;
		}
```

⇒ `path=8 booked=0` 的唯一含义是：**8 个待订格里某一格失败，之前订上的被 `UnreserveRailTrackdir` 逐格撤净**。
所以铁轨上不会留预览，不是"没铺"，而是**铺了又全撤**（与 §18.12 的"幽灵预留闪一下"同源）。

#### 三、`res=0x1` 不是"订上了 1 条"，别误读

`res=0x1` 出现在 `CPL-RET` 行，取自脚下格 `41,10` 的预留位；那是 `ReserveTrackUnderConsist()` 的**整格保护位**，
与本次路径预订（`booked=0`）是两笔账。同理 `RESV-END ... endPos=37` 只说明本次摸路/延伸把游标推到 37，不代表订成功。

#### 四、只订"非站台非车库"格 ⇒ `path=8` 已经是扣掉站台段之后的计数

`:1515` 的条件把 `IsRailStationTile` / `IsRailDepotTile` 全排除。本场景站台段（`27,12..38,12`，即 `CPL-BACK` 那一跳跨的 11 格）
**不在 8 条之内**，它靠车底自身的整格位保护。⇒ 第 4 次的 8 格是 `42,10` 之外的普通轨格。

#### 五、疑点 REG-359-L（新登记）：**失败格没有日志，无法定位是哪一格挡住了预订**

`ReserveSingleTrack()` 失败时只设字段、**不打任何日志**：

```541:555:src/pathfinder/yapf/yapf_rail.cpp
	bool ReserveSingleTrack(TileIndex tile, Trackdir td)
	{
		...
		} else {
			if (!TryReserveRailTrackdir(Yapf().GetVehicle(), tile, td)) {
				/* Tile couldn't be reserved, undo. */
				this->res_fail_tile = tile;
				this->res_fail_td = td;
				return false;
			}
```

⇒ 全日志只有 `booked=0`，**看不到是哪一格、被谁挡住**。这是探针缺口，不是推断缺口。

- 期望值：应打印 `CPL-RES-FAIL tile=x,y td=N nth=i/total booked-before=k owner=vehX`
- 实测值：无
- 为什么存疑：第 3 次（tick 13485）刚在 `37,12` 上 `fail=tryReserve ... owner=57 res=0x20`，第 4 次走廊几乎相同，
  最可能仍是被 `veh=57` 的位挡住；但**没有任何一行能证明**，属推断。
- 还需什么数据：在 `R3RReservePathSequence` 循环内、`res_fail_tile != INVALID_TILE` 分支里加一行日志（只读，不改行为）。

#### 六、结论（可直接回答玩家）

1. 第 4 次不是"成功"，是"**路径成功 + 预订失败**"；`booked=0` 意味着中途失败、已订的位全被撤。
2. 所以看不到预览是**设计行为**（`R3RReservePathSequence` 的整批回滚），不是漏了铺。
3. 第 5 次之所以能看到位（`booked=6`），是同一走廊上占位者让开后的时序差别，不是路径变合法。
4. 下一步最省事的取证 = 执行第五节的探针补丁（一行 `fprintf`），下一份日志就能直接读出"哪格、第几个、被谁挡"。

### 18.18 探针已落地（2026-10-29，玩家指示「请你加探针吧」）—— 两条只读探针，仅改 `yapf_rail.cpp`

#### 一、改了什么

`src/pathfinder/yapf/yapf_rail.cpp` 的 `R3RReservePathSequence()`（原 :606-623）：

1. `for (const auto &step : steps)` 改为 `for (size_t i = 0; ...)`，只为把下标 `i` 报进日志（**循环体行为一字未改**）。
2. **失败分支**新增 `CPL-RES-FAIL`（REG-359-L 专用）。
3. **成功分支**新增 `CPL-RES-OK`（REG-359-K 专用，列出实际订下的格序）。

#### 二、`CPL-RES-FAIL` 字段与判读

```text
CPL-RES-FAIL veh=48 nth=3/8 tile=37,12 td=5 want=4 booked-before=3 failtd=5
             type=1 tbits=41 res=32 plain=1 st=0 depot=0
             owner=57 selfheld=0 otile=37,13 ospd=100 ord=6
```

| 字段 | 含义 | 判读口径 |
|---|---|---|
| `nth/total` | 第几格失败（0 基）/ 共几格待订 | `total` 应等于同一 tick 的 `CPL-RESERVE path=` |
| `booked-before` | 失败前已订上的格数 | 回滚后 `CPL-RESERVE` 必报 `booked=0`；两者对照可确认"铺了又撤" |
| `tile/td/want` | 失败格 / 该步的 trackdir / 它对应的轨道 | `want` = `TrackdirToTrack(td)` |
| `type/plain/st/depot` | 格型 | `st=1` ⇒ 站台整格位冲突；`plain=1` ⇒ 普通轨位冲突 |
| `tbits/res` | 该格全部轨道位 / 已被预留的位 | `plain=0` 时两列均为 `-1`（**故意不读**，见下） |
| `owner/otile/ospd/ord` | 该格现有预留的归属者及其位置/速度/命令 | `owner=-1` ⇒ 无主（孤儿位）；`ord=17` ⇒ 是合法等待车底 |
| `selfheld` | 归属者与询路车同链 | `1` ⇒ 自锁（KI-510 家族）；`0` ⇒ 第三方挡路 |

owner 取的是 **Arbitrated 版**（`GetTrainForReservationArbitrated` / `GetTrainForWholeTileReservationArbitrated`，KI-350 口径），
不是 `FSCP-RES` 用的 plain 版——plain 版在预留两端都有车时会随机命中错的一端。

> 安全细节：`GetTrackBits()` / `GetReservedTrackbits()` 在非 plain rail 上是 `dbg_assert`（KI-207c 附记 7b 那次
> `rail_map.h:168` 崩溃就是这个）。故先算 `plain = IsPlainRail(tile)`，非 plain 时两列直接打 `-1`，**永不进断言**。

#### 三、`CPL-RES-OK` 字段与判读

```text
CPL-RES-OK veh=48 total=6 tiles=42,10/0;41,10/0;40,10/0;39,11/4;38,12/0;27,12/0
```

- `total` = 订成功的格数（应与 `CPL-RESERVE booked=` 相同）
- `tiles=x,y/track;...` 按预订顺序列出，超过 16 格打 `;...(truncated)`
- 用途：直接回答 REG-359-K「第 5 次 booked=6 到底订了哪 6 格、有没有覆盖 36,12 / 37,12」

#### 四、预期下一份日志能定论的四件事

1. 第 4 次（`path=8 booked=0`）→ 必有一条 `CPL-RES-FAIL nth=k/8`，`k` 直接给出失败位置；`owner/ospd` 给出挡路者。
2. 若 `st=1` 而 `owner=57` ⇒ 站台整格位互斥（一条整格位挡住整段站台），与 KI-207c 同族。
3. 若 `selfheld=1` ⇒ 不是第三方，是询路车自己后半节压住交叉轨（KI-510 家族），修法完全不同。
4. 第 5 次成功时若有 `CPL-RES-OK total=6 tiles=…` 且**不含 36,12/37,12**，则说明这两格由"车底整格保护"覆盖，
   REG-359-K 应转向「预订覆盖范围与会合点是否对齐」，而非"哪格被挡"。

#### 五、状态

- REG-359-L：**已加探针，待复测**（等玩家给下一份 `build\R3R_debug.log`）
- REG-359-K：**已加对照组探针（CPL-RES-OK），待复测**
- 本轮未改任何判据、未改回滚逻辑、未碰 `src\*.h`

### 18.19 探针复测：第 4 次的失败格查出来了 —— **自锁，`nth=0`，第一格就被自己挡住**（2026-10-29，玩家回日志）

**输入**：`build\R3R_debug.log`（含 §18.18 的 `CPL-RES-FAIL` / `CPL-RES-OK` 两条探针）。
全日志只有 **1 条** `CPL-RES-FAIL`、**1 条** `CPL-RES-OK`、3 条 `CPL-RESERVE veh=48` —— 探针命中，且场景干净。

#### 一、决定性读数

```1097:1098:build\R3R_debug.log
CPL-RES-FAIL veh=48 nth=0/8 tile=41,10 td=0 want=0 booked-before=0 failtd=0 type=1 tbits=5 res=1 plain=1 st=0 depot=0 owner=48 selfheld=1 otile=42,10 ospd=0 ord=16
CPL-RESERVE veh=48 reserved=0 next=0 path=8 booked=0
```

逐字段读：

| 字段 | 值 | 含义 |
|---|---|---|
| `nth/total` | `0/8` | **第一格就失败**，还没订上任何一格 |
| `booked-before` | `0` | 与 `nth=0` 一致，没有"铺了又撤" |
| `tile` | `41,10` | 失败格，紧贴列车所在 `42,10` 的西侧 |
| `want` | `0` = `TRACK_X` | 要订的轨 |
| `tbits` | `0x5` | 该格轨道位 = `TRACK_X|TRACK_UPPER` |
| `res` | `0x1` | 该格**已被预留** `TRACK_X` |
| `owner` | `48` | 预留归属者 = **询路车自己** |
| `selfheld` | `1` | 自锁 |
| `ospd` / `ord` | `0` / `16` | 自己是停稳的、订单是 `OT_GOTO_COUPLE` |

#### 二、旁证：`41,10` 上的位是"本车自己的行驶预留"

```645:645:build\R3R_debug.log
RESV-EXT-PRE veh=48 tile=41,10 enterdir=0 tracks=0x1 reserved=0x0 doRes=1 real=2 spd=0
673:673:build\R3R_debug.log
UNRES-RAIL tile=41,10 track=0 phase=choose-track actor=48
1169:1169:build\R3R_debug.log
RESV-EXT-PRE veh=48 tile=41,10 enterdir=0 tracks=0x1 reserved=0x1 doRes=0 real=2 spd=60
```

- `:645` 早先 `reserved=0x0 doRes=1` ⇒ 那时该格空着、引擎打算订；
- `:673` 被 `UNRES-RAIL ... actor=48` 释放过；
- `:1169` 到 tick 13550 时 `reserved=0x1 doRes=0 spd=60` ⇒ **列车自己的行驶路径预留又把它订上了**（列车正往西开：`CPLSPD`:1195 `tile=41,10 spd=74 db=1`，`:1197 LOCO-MOVE ... endTile=40,10）`。

⇒ 挂接寻路在 `42,10` 停稳（`spd=0`）时，**自己的普通 PBS 行驶预留还覆盖着正前方的 `41,10 TRACK_X`**。

#### 三、机制：`TryReserveTrack` 把"已是自己的位"当作失败

```538:557:src/pbs.cpp
bool TryReserveRailTrack(TileIndex tile, Track track, bool trigger_stations)
{
	...
		case TileType::Railway:
			if (IsPlainRail(tile)) {
				bool changed = TryReserveTrack(tile, track);
				if (!changed && R3RReservationHeldByCouplePartner(tile)) {
					/* R3R (第 345 轮): the bits belong to our pair partner -- air for us. */
					R3RDbgWrite("RESV-PAIR-AIR veh=%u tile=%d,%d track=%u kind=rail\n", ...);
					return true;
				}
				if (changed && _settings_client.gui.show_track_reservation) MarkTileGroundDirtyByTile(tile, VMDF_NOT_MAP_MODE);
				return changed;
			}
```

`TryReserveTrack` 只是 `bits |= TrackToTrackBits(track)`，**位已置位时返回 `changed=false`** ⇒ 直接当"订不上"。
现有豁免只有 **第 345 轮加的「pair partner 的位 = 空气」**（`RESV-PAIR-AIR`），**没有「自己的位 = 空气」这一支**。
⇒ 只要挂接路径的任意一步撞上本车自己的行驶预留，整批预订就在那一步回滚（`booked=0`），铁轨上自然看不到挂接预览。

同 tick 的旁证：```1100:1100:build\R3R_debug.log
RPFAIL veh=48 tick=13505 origin=42,10 tile=41,10 td=0 unsafe=0 nTry=35 nOK=34
```
35 次尝试里 **34 次 OK，只有这 1 格（`41,10`）失败** —— 不是走廊被堵，是单点自锁。

#### 四、REG-359-K 被证伪：第 5 次**确实覆盖了** `36,12` / `37,12`

```1535:1536:build\R3R_debug.log
CPL-RES-OK veh=48 total=6 tiles=39,10/3;39,11/2;38,11/3;38,12/2;37,12/0;36,12/0
CPL-RESERVE veh=48 reserved=1 next=3 path=6 booked=6
```

- `total=6` = `booked=6`，逐格列表**包含 `37,12/0` 与 `36,12/0`** ⇒ 「第 5 次没覆盖目标格」的猜测不成立。
- 但同一 tick 稍后 `:1537 CPL-RET veh=48 tick=13676 tile=39,10 ... res=0x8 ret=0x8 why=nores-final spd=5`
  ⇒ 这 6 格随后被 `nores-final` 撤掉（列车已开动 `spd=5`，预订跟着行驶走）。
- 这一轮**最终挂上了**：`:1788 COUPLE-OK loco=48 rear=26 consist=0 co=1 real=7 type=1 tx=35 ty=12 x=574 y=200`
  ⇒ 之前"第 5 次之后为何无 `COUPLE-OK`"的疑点，在本次会话里**不成立**（只是晚了 250 行日志/约 100 tick）。

#### 五、结论

1. 第 4 次 `booked=0` 的真因：**自锁**——`41,10 TRACK_X` 被本车自己的行驶预留占着，`nth=0` 第一格即失败，与被挂车底（`veh=26/57`）无关。之前"最可能仍被 `veh=57` 挡"的推断**证伪**。
2. 不是站台整格位问题（`st=0 depot=0 plain=1`），KI-207c 家族不适用。
3. 现象"没有铺预留"= 设计上的整批回滚 + 撞自己；不是"没铺"。
4. 挂接最终成功（`COUPLE-OK rear=26`），所以第 4/5 次的差别只是**时序**（自己行驶预留何时让开/列车是否已越过 `41,10`）。

#### 六、修法候选（未落地，待玩家拍板）

- **A（最小、推荐先试）**：给 `TryReserveRailTrack` 的 plain rail 分支补一支「自己的位 = 空气」，与 `RESV-PAIR-AIR` 同构：
  `!changed && GetTrainForReservation(tile, track) 归属本链` ⇒ 打 `RESV-SELF-AIR` 并 `return true`（**不写位**，释放责任仍归行驶预留那侧）。
  风险：挂接预订与行驶预留共享同一批位；若行驶预留随后被释放（车开走/清预留），挂接路径的位会一起消失。故必须配套验证 ①挂接预留在成功后仍能被 `CPL-RET`/`nores-final` 正确收尾 ②不会出现"挂接以为订上了、车却撞车"。
- **B（治上游）**：停稳等待挂接（`cur_speed==0 && order==OT_GOTO_COUPLE`）时释放自身行驶预留（`ClearReservationUnderConsist` 或 `FreeTrainTrackReservation`），让正前方不再被自己占。风险更大（会丢 PBS 保护），但语义更干净。
- **C（不建议）**：让 `TryReserveTrack` 返回值区分"已置位"与"失败"——会动上游函数语义，波及全部调用点。

排在 A 之前需要的一个只读补证（若玩家要做）：`RESV-EXT-PRE` 在 `41,10` 的 `reserved=0x1` 是谁的位，可用现有 `owner=` 字段（本次已给 `owner=48`），无需再加探针。
- [ ] 本次现场是否出现过 `CPL-BACK`（若有，说明进入了回溯，情形①被排除）。
- [ ] 起点节点段是否真的跨了站台（`tiles_skipped>0`），需看 `CPL-TRACE`/`REACH` 首行 `depth=0` 的 tile 与 origin 的差。

> 登记为**猜测根因（待验证）**：不成立的部分已在上表排除，成立的部分（根节点免检）与本次 CPL 现象之间的因果链尚未取证。

### 18.9 「回溯=先规划后安全检查 ⇒ veh48 路径压根没预留到 35,12？」—— 结论：对，且起点格另有结构性问题

玩家 2026-10-29 追问：「这个回溯，是不是说先规划，后安全检查的安全检查，那也就是说 veh48 路径压根就没有规划预留到（35，12）」

#### 一、阶段归属：确认，back-walk 是"规划完之后"的阶段

```762:766:build/R3R_debug.log
CPL-PATHFOUND veh=48 found=1
... (CPL-BEST/CPL-TRACE)
CPL-BACK pPrev=27,12 td=0 parent=38,12 ptd=2
```

`CPL-PATHFOUND found=1` 先出现、`CPL-BACK` 后出现 ⇒ yapf 搜索已经把路径算完，back-walk 只做两件后事：
逐节点 `CheckSafePositionOnNode(pPrev)`（`yapf_rail.cpp:1347`）+ 逐格预留（`CPL-RESERVE`）。
**它不会改变路径走向**，路径不覆盖的格，检查与预留都不会碰到。

#### 二、veh48 这次规划的实际起点与走向（关键新事实）

四次寻路尝试的 `CPL-ORIGIN` 全部不在 35,12：

| 日志行 | origin | 该次的 CPL-BACK 链 |
|---|---|---|
| `:747` | `42,10` | `:766` 起 `27,12 → 38,12` |
| `:808` | `42,10` | `:827` `27,12 → 38,12` |
| `:853` | `42,10` | `:872` `27,12 → 38,12` |
| `:926` | `42,10` | `:945-948` `27,12 → 38,12 → 39,11 → 40,10 → 42,10` |
| `:1378` | `40,10` | `:1399-1401` `27,12 → 38,12 → 39,11 → 40,10` |
| `:1659` | `36,12` | `:1670` `27,12 → 36,12` |

即：**起点恒在 42,10 / 40,10 / 36,12 一带，终点是等待车那格 27,12**，节点链只覆盖 `27,12…42,10` 这条走廊。
`CPL-BACK` 全部 21 条匹配里**没有一条出现 35,12** ⇒ 路径没经过它，预留自然也没订它。

#### 三、预留同样是"只订走过的格"，且是两遍法

`:1318-1330` 的注释与 `CPL-RESERVE path=8 booked=0 / path=6 booked=6` 说明：`rp()` 第一遍只**读**着摸出走过的格序列，第二遍才按序列逐格下订，
刻意不按节点链订（节点 segment 会跨站台合并）。⇒ 预留格集合 ≡ 走过的格集合，35,12 不在其中。

#### 四、比"没看过 35,12"更要紧：**起点格（根节点）永不进安全检查**

```1335:1337:src/pathfinder/yapf/yapf_rail.cpp
		while (pNode->parent != nullptr) {
			pPrev = pNode;
			pNode = pNode->parent;
```

循环退出条件是 `pNode->parent == nullptr`，此时 `pNode` 已是根（origin 格）而**从未被赋给 `pPrev`**，
⇒ `CheckSafePositionOnNode()` 对根节点一次都不调用。配合 `:1299-1306` 的零长路径早退（`return INVALID_TRACKDIR`），
"起点格不安全"这件事在结构上无法被这条判据发现。

#### 五、必须拆开的两件事（对 §18.7/§18.8 的修正）

| | 35,12 两链头 | veh48 挂车寻路 |
|---|---|---|
| 日志位置 | `:528` / `:602` / `:629-631` | `:746` 起 |
| 性质 | 解挂 settle（`GEO settle-decouple-u`）+ `RESERVECONSIST` | `CPL-*` 挂车寻路 |
| 参与者 | veh0（车底半）、veh57（机车半） | veh48（询路车）、目标 27,12 |
| 相关性 | **与 veh48 的路径无交集** | 起点 42,10，与 35,12 相隔一整段 |

§18.7/18.8 把"35,12 两链头"与"36,12 `fail=notSeg`"并置讨论，容易读成同一场景的两面；实际是**不同 tick、不同 origin 的两件事**，
`36,12` 上的 `notSeg` 只与 veh57 那条链的后节有关，与 35,12 无因果。此点已在 §18.7 判定表里保留结论、机制描述以此节为准。

#### 六、35,12 上那笔预留的来源（避免混淆）

`RESERVECONSIST veh=0 tile=35,12 resNow=1` 出自 `ReserveTrackUnderConsist()`——等待车**每 tick 给自己脚下格订的保护位**，
与挂车路径预留是两套独立机制。所以"35,12 有预留"不等于"veh48 预留到 35,12"。

#### 七、新疑点 REG-364-C

`CPL-ORIGIN` 在 42,10 / 40,10 / 36,12 之间变化（机车在移动，合理），但**没有任何一次 origin 落在 35,12**。
若玩家认为 veh48 曾站在 35,12，需补一条 `LOCO-MOVE veh=48 tile=` 的连续轨迹来判定它是否到过那格——
本轮日志该窗口内没有此类行，故"veh48 与 35,12 无关"暂按成立处理，标记待复核。

---

## §18.20 玩家拍板 A：把自己的预留也当空气（第 368 轮，已落地）

**玩家原话（2026-10-29）**：「选A吧，把自己的预留一样当空气」。

### 一、决策

在 `src\pbs.cpp` 的 `TryReserveRailTrack()` 里，凡是 `TryReserveTrack()` 返回 `changed=false`
（= 该位已置位）的地方，除既有的「pair partner 的位 = 空气」（第 345 轮，`RESV-PAIR-AIR`）之外，
再补一支「**本车自己的位 = 空气**」（`RESV-SELF-AIR`）。

口径要点（与 pair 支刻意保持同构）：

- 只在挂接寻路期生效：`r3r_resv_actor` 有效 + 该链链头 `current_order.IsType(OT_GOTO_COUPLE)`；
- **不写位**（位本来就是自己的），因此不放 `MarkTileGroundDirtyByTile`；
- 释放责任仍在原有的行驶预留一侧，挂接路径不新增任何释放动作、不记账。

### 二、判据比 pair 支更严（避免误放行）

pair 支只问"整格的位是否都归 partner"；self 支必须问"这一位是不是**只属于我**"，
否则两端各压一列车时会误判。落地的判据（`R3RReservationHeldBySelf()`）：

| 情形 | 判据 | 结果 |
|---|---|---|
| plain rail | `R3RFollowReservationToOwner()` **两向都跑**，只要有任何一端非空且 `->First() != 本链头` | 不放行（保守回退到"订不上"） |
| plain rail | 两向都空（stray） | 不放行 |
| plain rail | 两向非空者全部 `->First() == 本链头` | 放行（`RESV-SELF-AIR kind=rail`） |
| station/waypoint 整格位 | `GetTrainForWholeTileReservation(tile)->First() == 本链头` | 放行（`RESV-SELF-AIR kind=wpt` 或 `stn`） |
| depot / tunnel / bridge / 道口 | 不动（与 pair 支一致） | 不放行 |

即：**"这一条预留的两端都是我自己"才当空气**；有一端压着别人（partner 或第三方）就交给原有判据。

### 三、为什么安全上说得通

被 self-air 放行的格子，本来就是**本车自己行驶预留覆盖的格子**，也就是列车本来就要走的路；
列车走的时候该位仍由行驶预留持有/重订，因此不存在"挂接以为订上了、车却撞车"的新形态。

### 四、已知风险（复测必查）

1. **共享位**：挂接预订与行驶预留共享同一批位。若行驶预留随后被撤（车开走 / `nores-final` 清预留），
   挂接路径以为占着的那一位会一起消失。第 345 轮 pair 支有同一隐患且已复测通过，本轮沿用。
2. **释放路径**：挂接路径若在收尾时对这些位调 `UnreserveRailTrack()`，会误释行驶预留的位 —— 需实测确认不出现。
3. 存量行为：`CPL-RET ... why=nores-final` 是否把 self-air 的格子也计入 `booked` 而做多余的释放动作。

### 五、复测判据

1. 同场景第 4 次不再出现 `CPL-RES-FAIL ... selfheld=1`，改为出现 `RESV-SELF-AIR veh=48 tile=41,10 track=0 kind=rail`
   且同一 tick `CPL-RESERVE ... booked>0`（铁轨上能看到预览）。
2. 出现 `RESV-SELF-AIR` 之后仍能走到 `COUPLE-OK`，且收尾行（`CPL-RET`）不出现异常 `unres`/`res` 读数。
3. 负判据：被**第三方**车占着的前方格仍必须是 `CPL-RES-FAIL ... selfheld=0 owner=vehX`（不得被误判为自己的）。
4. 负判据：被 **pair partner** 占着的格仍打 `RESV-PAIR-AIR`（不是 `RESV-SELF-AIR`），两条日志互不串。
5. 负判据：非挂接寻路（普通 `GOTO_STATION` 的预留）绝不出现 `RESV-SELF-AIR`。

### 六、落地与构建自证（2026-10-29）

仅改 `src\pbs.cpp`，未碰 `src\*.h`：

1. 文件内新增实现 `static bool R3RReservationHeldBySelf(TileIndex tile, Track track)`，
   放在 `GetTrainForWholeTileReservationArbitrated()` 之后（此处 `R3RFollowReservationToOwner()`
   与 `GetTrainForWholeTileReservation()` 都已可见）；
2. `TryReserveRailTrack()` 之前（`R3RReservationHeldByCouplePartner()` 注释上方）加前向声明；
3. `TryReserveRailTrack()` 的 plain rail 分支在 `RESV-PAIR-AIR` 之后补 self 支；station/waypoint 分支同构补一支。

覆盖率核对：挂接寻路的两处预留入口

- `yapf_rail.cpp` 的 `rp()` 回溯预留 → `TryReserveRailTrackdir()`；
- `yapf_rail.cpp` 的 `FindSafeCouplePositionProc()` → `TryReserveRailTrack()` 直接调用；

其中 `TryReserveRailTrackdir()`（`src\pbs.cpp:429`）在 432 行 `bool success = TryReserveRailTrack(tile, track, ...)`，
返回值原样透传 ⇒ **self-air 对两条入口都生效**，并顺带让 `success && HasPbsSignalOnTrackdir()` 把 PBS 信号置绿
（这正是 KI-361 那条注释想要的"自己订的位不该挡自己的信号"）。

构建：`R3R_fullbuild.cmd inc detach`（复用既有入口，未新建 .cmd）

| 项 | 读数 |
|---|---|
| `build\R3R_incbuild.done` | `EXIT_CODE=0`（构建结束 2026-10-29 20:32:52） |
| 日志 | `[2/3] Building CXX object ... pbs.cpp.obj` → `[3/3] Linking CXX executable openttd.exe` |
| 时间链 | `src\pbs.cpp` 20:30:16 → `pbs.cpp.obj` 20:31:08 → `build\openttd.exe` 20:32:28 |
| exe | 52 408 320 B，命中 `RESV-SELF-AIR` 字面量 |
| `read_lints` | `src\pbs.cpp` 0 条 |

状态：**已实现 + 已编译**。

---

## §18.21 实机复测：REG-359-L 已解决（2026-10-29，玩家报「预留的问题目前已经解决」）

### 一、采样有效性

| 项 | 读数 |
|---|---|
| `build\R3R_debug.log` | 195 887 B，2026-10-29 **20:38:27** |
| `build\openttd.exe` | 52 408 320 B，2026-10-29 **20:32:28**（= 第 368 轮 self-air 版） |
| 结论 | 日志晚于 exe ⇒ 采样有效，用的正是 self-air 版 |

标签计数（`Select-String`）：`RESV-SELF-AIR=2`、`RESV-PAIR-AIR=11`、`CPL-RES-FAIL=0`、
`CPL-RES-OK=1`、`CPL-RESERVE=2`、`COUPLE-OK=1`、`COUPLE-FAIL=12`、`RPFAIL=1`。

### 二、决定性读数（原文）

```
:1097  RESV-SELF-AIR veh=48 tile=41,10 track=0 kind=rail
:1098  RESV-SELF-AIR veh=48 tile=40,10 track=0 kind=rail
:1100  CPL-RES-OK veh=48 total=8 tiles=41,10/0;40,10/0;39,10/3;39,11/2;38,11/3;38,12/2;37,12/0;36,12/0
:1101  CPL-RESERVE veh=48 reserved=1 next=0 path=8 booked=8
:1102  CPL-RET veh=48 tick=135505 tile=41,10 enterdir=0 tracks=0x19 res=0x1 ret=0x1 why=cpl-partial-res spd=0 real=2
:1258  CPL-RET veh=48 tick=135577 tile=39,10 ... res=0x8 ret=0x8 why=early-haverserved spd=125 real=2
:1314  CPL-RET veh=48 tick=135598 tile=38,11 ... res=0x8 ret=0x8 why=early-haverserved spd=146 real=2
:1360  CPL-RET veh=48 tick=136615 tile=37,12 ... res=0x1 ret=0x1 why=early-haverserved spd=160 real=2
:1454  CPL-RESERVE veh=48 reserved=0 next=0 path=0 booked=0
:1490  COUPLE-OK loco=48 rear=26 consist=0 co=1 real=7 type=1 tx=35 ty=12 x=574 y=200
```

对照第 367 轮失败现场（`:1097 CPL-RES-FAIL veh=48 nth=0/8 tile=41,10 ... owner=48 selfheld=1`）：
**同一格 `41,10`、同一 veh=48，这次走的是 `RESV-SELF-AIR` 并继续往下订，8 格全订上（`booked=8`）**。

### 三、判据逐条判定

| # | 判据 | 结果 | 证据 |
|---|---|---|---|
| 1 | 不再 `CPL-RES-FAIL selfheld=1`，改出 `RESV-SELF-AIR` 且 `booked>0` | **通过** | `CPL-RES-FAIL=0`；`:1097/:1098` 两条 self-air；`:1101 booked=8` |
| 2 | 之后走到 `COUPLE-OK`，`CPL-RET` 无异常读数 | **通过** | `:1490 COUPLE-OK loco=48 rear=26`；`:1102 res=0x1 ret=0x1 why=cpl-partial-res`、`:1258/:1314/:1360 why=early-haverserved` 均为 res==ret（自洽），挂接成功后 `:1454 booked=0` 正常归零 |
| 3 | 负判据：第三方挡路仍须 `selfheld=0 owner=vehX` | **本次未覆盖** | 全日志 `CPL-RES-FAIL=0` ⇒ 本轮没有任何"被第三方挡"的样本，这条需另找场景 |
| 4 | 负判据：partner 位仍打 `RESV-PAIR-AIR`，两日志不串 | **通过** | `RESV-PAIR-AIR=11` 条；两条 `RESV-SELF-AIR` 都是 `veh=48 kind=rail`，无交叉 |
| 5 | 负判据：普通 `GOTO_STATION` 预留不得出现 `RESV-SELF-AIR` | **通过** | 两条 self-air 都夹在 `CPL-RES-OK` 序列内（`:1097/:1098` → `:1100`），属挂接寻路期 |

### 四、结论

REG-359-L（自锁）**已解决**：`self-air` 判据在实机生效，`41,10` 不再整批回滚，
8 格预留全部留下（铁轨上能看到预览），并顺利走到 `COUPLE-OK`。

### 五、仍留下的观察项

1. `COUPLE-FAIL=12` 与 `COUPLE-OK=1` 共存 —— **已核，与本修法无关**。12 条行号与车主：
   `:276/:324/:712 loco=48 @42,10`、`:684/:1738 loco=54 @1,15`、`:1636/:1667/:1686/:1703/:1727/:1755/:1772 loco=57 @42,13`。
   veh=48 自己的三条全在 `:1490 COUPLE-OK` **之前**（挂接前候选未命中，正常重试形态）；
   晚于成功的 8 条全部是 `loco=57`(7 条，`42,13`) 与 `loco=54`(1 条，`1,15`) —— **不同车、不同位置**，
   属"目标车底不在/未到位"的常规 `COUPLE-FAIL`，不在本次 self-air 的作用域内。
2. `:549 RPFAIL veh=57 ... origin=36,12 tile=42,13 nTry=2 nOK=1` —— 单次预留失败，只 1 条，本场景内自愈；
   但它与挂接路径**同源**，若后续复现增大，按 KI-346 家族方向查。
3. 已知风险（§18.20 四）未暴露：日志里没有"行驶预留被撤导致挂接位消失"的痕迹（挂接成功、收尾 `booked=0` 干净）。




