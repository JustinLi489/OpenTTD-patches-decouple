# R3R 挂接寻路预留终止在非安全位（道岔区）——第 310 轮

日期：2026-10-26
输入：玩家报告 + `build\R3R_debug.log`

## 0. 现场元信息（已核实）

| 项 | 值 |
|---|---|
| 日志 `build\R3R_debug.log` | 1 254 959 B，LastWriteTime **2026/10/26 12:53:52** |
| 产生者 `build\openttd.exe` | 52 253 696 B，LastWriteTime **2026/10/26 12:40:47** |
| 该 exe 对应轮次 | 第 308 轮（撤销 R307-1 之后、R307-2 保留）⇒ 采样有效，结论可归属到当前源码 |

## 1. 玩家原话（要点）

> 有一列车它发出的挂接寻路的预留在非安全停车位终止了，在一个道岔区终止了。

即：**挂接寻路的终点（预留末端）落在一个不安全的格子上（道岔区）**，而不是安全停车位。

## 2. 日志尾部初步现场（原文锚点）

`build\R3R_debug.log` 尾部（-Tail 25）逐字摘录：

```
[R3R] CPL-PAIRSCAN tick=2898 act=99 state=none ord=16 destStn=12 actTile=250,233 actSpd=5 pool=5 s_chain=138 s_target=5 ... best=-1 bestDist=0 paired=0 wait=8 near=75 nearDist=39 nearWhy=not-target nearTile=213,235 nearOrd=1 nearStop=0
[R3R] CPL-PAIRSCAN tick=2899 act=99 state=backoff ord=16 destStn=12 actTile=250,233 actSpd=5 pool=0 ... paired=0 wait=7 ...
DEPOTBLK veh=99 tile=250,233 track=01 eng=1 isdepot=0 ontile=0 idmatch=0 pend=0 stopped=0 parked=0 spd=5 destRaw=12 depotIdx=4294967295
LOCO-MOVE veh=99 tile=250,233 j=192 spd=5 track=01 dir=1 db=1 stuck=0 dtile=491798
```

读数含义（初判）：

- 挂接机车候选 = **veh=99**，订单 `ord=16` = `OT_GOTO_COUPLE`，目标站 `destStn=12`（`destRaw=12`，`dtile=491798`）
- 它在 `250,233`，`track=01`（TRACK_X），**`spd=5` 仍在动**，`db=1`（倒车链）
- 配对状态 **`paired=0`**（还没有配对锁），`wait` 由 8 递减到 6 ⇒ 等待车底的候选池在缩小
- `state` 在 `none` / `backoff` 之间来回 ⇒ 处于「找不到目标、退避重试」形态
- `nearWhy=not-target`（最近的候选 veh=75 @213,235 不是合法目标）

⇒ 至少存在**两列挂接车**在现场（veh=99 未配对；另有 `near=75` 车底）。**哪一列发出了那条"终止在道岔"的预留，尚未定位**（本轮待查项 A-1）。

## 3. 猜测根因（待验证，全部未实证）

- **G1 终点本身就在道岔格**：挂接寻路的终点不是订单写的车站/车库格，而是 YAPF 里 `FindNearestCoupleTrain` 选中的"可挂车底所在位置"。若该车底正停在一组道岔上，预留自然终止在道岔。← 最可能。
- **G2 末格安全判定缺失**：普通寻路由 `ExtendTrainReservation()` 逐步推进并在 `IsSafeWaitingPosition` / 信号处收口；挂接寻路途经 `DoTrainCouplePathfind()`，若这条路的末格只做"能不能挂"的判定、不做"能不能安全停"的判定，就会铺到不安全格。
- **G3 `rp()` 逐格铺设只顾"到得了"**：回溯里逐格 `reserve_if_present()` 的终止条件来自 destrail 的 `tg`，`tg` 是车底格 ⇒ 末格即车底格或其邻格，与安全性无关。
- **G4 安全位检查只查占用者**：`CheckSafePositionOnNode`/`FindSafeCouplePositionProc` 里"安全"的语义是"这格有没有别人占着/是不是我的等待者"，不是"这格能不能安全停车"（两件事被混为一谈）。

## 4. 本轮待查（按优先级）

- **A-1 定位那条预留**：找出日志里"预留末格在道岔"的那一次 —— 关注 `CPL-BEST` / `CPL-TRACE-RESULT` / `RP cur=` / `EXTEND-*` / `TPR*` 里最后一个被铺的格，再用该格的邻接轨道位判断它是不是道岔（track bits 多于 1 位，如 0x30/0x0A/0x09 等）。
- **A-2 该格的安全属性**：`IsPbsSignalOnTrackdir` / `IsSafeWaitingPosition` 在该格是否为真；若为假，就是"铺到了非安全位"的实锤。
- **A-3 终点来源**：该次寻路的 `CPL-BEST tile=` 与目标车底（`CPL-PAIR`/`wait=`）的所在格是否同一格；若同一格 ⇒ G1 成立。
- **A-4 为什么没在安全位收口**：挂接寻路的 destrail 是否跳过 `ExtendTrainReservation()` 的安全停车位收敛逻辑。

## 5. 复测判据（修法落地后）

1. 挂接预留的末格必须满足「安全停车位」或「PBS 信号前」二者之一；
2. 若目标车底停在道岔/非安全位，预留应在前一个安全位收口，最后一段靠低速蹭行 + 命中判定完成；
3. 负判据：目标车底本身停在安全位时，预留末端行为与修复前逐字不变（不得多留或少留）。

## 6. 日志侧取证结论（2026-10-26 第二轮）

| 检索 | 结果 | 判定 |
|---|---|---|
| `RESV-EXT-*` 标签种类 | 只有 `RESV-EXT-PRE`，共 **4 条** | 预留推进没有逐格日志 |
| `CPL-PATHFOUND` | 全日志 **仅 1 条，且 `found=0`** | 本会话挂接寻路**从未找到过路径** |
| `phase=yapf-resv` | 全日志 **9 处**（8031/8032/13319/13320/13797/14250/14279/14367/14758） | YAPF 预留铺设记录极稀疏，且 `RESV-WATCH` 是**关注格抽样**，不是全量 |
| `CPL-BEST` / `CPL-TRACE-RESULT` / `CPL-RESERVE` / `RP cur=` | **零命中** | 挂接路径 `rp()` 从未进入其记录分支（`pNode->parent == nullptr && pPrev != nullptr`，`yapf_rail.cpp:1010`），或 `best_td == INVALID_TRACKDIR`（:1078） |
| `RESV-NOBOOK` | **10 033 条**，主因 `reason=parked-no-pair` | 绝大多数 tick 挂接车被"未配对 ⇒ 拒绝订路"挡下（`train_cmd.cpp:17404`） |

⇒ **疑点 REG-310-A（新登记）**：玩家看到的那条"终止在道岔"的预留，**在本日志中找不到对应的逐格铺设记录**。原因是本版探针只覆盖"关注格抽样"（`RESV-WATCH`）与 YAPF 单段分支（`rp()`），并非全量预留日志。故 **不能** 用"日志没记录"反推"没铺过"；但也**不能**用日志实证该末格的 track bits。**该格是否道岔，只能采信玩家的画面观察。**

## 7. 源码取证结论（代码层，已确证，不依赖日志）

四处锚点构成完整因果链：

1. **挂接路径被无条件铺设** —— `src\train_cmd.cpp:16945-16948`：
   ```cpp
   /* R3R: always reserve the couple path - the loco needs a reservation
    * to move, and it must extend right up to the consist. ... */
   Track path_found = DoTrainCouplePathfind(consist, true);
   ```
   与普通寻路的差别是：它**早于** `res_dest` 的一切处理，也**不受** `res_dest` 后续调整影响。

2. **终点就是目标车底格，与安全性无关** —— `src\pathfinder\yapf\yapf_rail.cpp:1019`：
   ```cpp
   TileIndex tg = (consist_head_tile != INVALID_TILE) ? consist_head_tile : pPrev->GetLastTile();
   ```
   `tg` = **配对车底的链头格**（不是站台中心，不是订单目的地，也不是安全停车位）。

3. **`rp()` 到达即返回，全程没有安全判定** —— 同文件 `1081-1200`：
   - `1114: if (cur == tg) return true;`（到达即收，且**该格由父帧作为"进入格"补铺**：`1165-1167 reserve_dir_if_present(f3.new_tile, nd)`）
   - 全段**零次** `IsSafeWaitingPosition` / `IsPbsSignalOnTrackdir`（该文件仅 `:104` 的"找最近安全格"另一路径用到）。
   - 唯一被称为"安全"的 `CheckSafePositionOnNode`（`:996`）语义是**"这格有没有别的车占着/是不是我的等待者"**，**不是"能不能安全停车"** ⇒ G4 的猜测成立（两件事被混为一谈）。

4. **普通寻路的安全收口管不到挂接预留** —— `src\train_cmd.cpp:17189`：
   ```cpp
   while (!IsSafeWaitingPosition(consist, res_dest.tile, res_dest.trackdir, true, ...)) {
   ```
   该循环作用对象是 `res_dest`（`ExtendTrainReservation()` 的返回值，:16748），而 `ExtendTrainReservation` 与 `DoTrainCouplePathfind` 是**两条独立的铺路**（:16748 在前、:16948 在后），循环里所有分支（`:17210` / `:17220`）只做 `FreeTrainTrackReservation()` **整条清空**或 `TryReserveSafeTrack()` 补一格，**没有任何"把挂接预留截断到安全位"的动作**。

⇒ **结论：G1 + G2 + G3 全部成立（代码层确证）**。只要目标车底（链头）停在道岔/咽喉等 `IsSafeWaitingPosition == false` 的格上，挂接寻路就会把预留**一直铺到那格并在那里终止**，因为：
- 终点由"车底在哪"决定，不由"哪里能安全停"决定；
- 挂接路径自身不做安全性判断；
- 普通寻路的安全收口循环在时序上晚于它、且不复核它。

## 8. 为什么这是缺陷而不是"本该如此"

"预留必须延伸到车底"是挂接的必要条件（不铺到车底，机车走不过去），所以**末格落在车底格本身是对的**。缺陷在于**没有对"车底格不安全"这一前提做任何处理**：

- 若车底停在道岔,机车走到预留末端时若因距离/朝向/命中判定未成立（`GetCouplePosition` 只认"机车鼻尖 ↔ 车底尾"在容差内），就会**停在道岔上等待**——这是真实的运营危险（挡道岔、挡两条进路）。
- 现有代码对此没有任何闸门：既不拒绝配对（配对只查 `R3RIsCoupleTarget` 的订单/车种/货种/车数），也不在末格不安全时改为"上一安全位收口"。

## 9. 修法选项（未落地，待拍板）

| 选项 | 做法 | 代价 / 风险 |
|---|---|---|
| **A（推荐）配对侧闸门** | 建配对锁时要求等待车底的**链头格**满足 `IsSafeWaitingPosition(...)`；不满足就不建锁（`paired=0`），机车继续等 | 改动小、只在配对处；需给玩家一个可见反馈（否则"永远不挂"像卡死）。不解"车底停稳后才被推到道岔"的情形 |
| **B（字面按你要求）预留收口** | 挂接路径铺到**最后一个安全位/信号前**为止，末段不铺，靠低速蹭行 + `TrainCoupleHandler` 命中完成 | 末段无预留 ⇒ 可能与别的车抢道岔、且 `TryPathReserve` 的"必须整段预留"假设被打破；需新增"无预留短距移动"豁免，风险最高 |
| **C 只诊断/提示** | 保持现状，在末格不安全时打一行日志 + 界面提示"车底停在非安全位" | 零行为风险，但不解决危险 |
| **D 维持现状（隐式豁免）** | 现状即"挂接场景豁免安全判定" | 就是本 bug |

**我的建议**：A + C。A 从根上保证"不会把机车引到不安全格上等"，C 让玩家看得见原因。B 在引擎的"预留即通行权"模型下代价过大，不建议。

## 10. 状态

- 已登记：KI-491（`R3R_KNOWN_ISSUES.md` 第 310 轮小节），状态已由「待验证」改为「根因已定位（代码层）/修法待拍板」。
- 取证完成：A-1 日志侧不可得（探针不全，已登记疑点 REG-310-A）；A-2 未取（同上）；**A-3/A-4 已由源码确证**（终点=车底链头格；挂接路径完全不经过 `IsSafeWaitingPosition`）。
- 未改任何源码、未构建。等拍板 A/B/C/D 后落地。

## 11. 「为什么它可以不铺到安全位」——三层回答（2026-10-26 追加）

玩家追问的是"特权"从哪来。答案：**不是特权，是三道机制叠加后没有人承担"车底必须在安全位"这个前提。**

**第一层（结构）：它根本不在普通寻路那条链上。**
普通寻路的安全收口是 `ExtendTrainReservation()` + `train_cmd.cpp:17189` 的
`while (!IsSafeWaitingPosition(...))`；挂接寻路走的是 `yapf_rail.cpp` 里
`FindNearestCoupleTrain` 自己的 `rp()` 闭包（`:1081-1200`），**两套代码、互不调用**。
所以"安全位收口"这条规则从来没被挂接路径继承过——不是被豁免，是根本没接上。

**第二层（语义）：它的终点定义里没有"安全位"这个变量。**
`rp()` 的收车条件是 `1114: if (cur == tg) return true;`，`tg = consist_head_tile`（`:1019`）。
"停在哪"完全由"车底在哪"决定，而"车底在哪"是上游的事。

**第三层（必要条件）：它**必须**铺到车底，否则挂不上。**
PBS 要求通行必须有整段预留（引擎没有"无预留短距移动"这一说）。若按安全位收口，
机车就会停在安全位上，永远走不到车底 ⇒ 永远挂不上。
⇒ "铺到安全位"与"挂得上"在**当前模型**下互斥，除非车底本身停在安全位。

**新增猜测根因 G5（待验证，把握较高）：上游把"车底在安全位"这个前提取消掉了。**
- `src\train_cmd.cpp:17430-17433`（第 306 轮 KI-208 的 `parked-waiter` 门禁）：
  `if (consist->current_order.IsType(OT_WAIT_COUPLE)) { *reason = "parked-waiter"; return true; }`
  ⇒ 等待车底**不订路** ⇒ 它停车时不需要、也不会去挑一个安全停车位。
- `src\train_cmd.cpp:18050`（`TrainEnterStation`）：
  `if (consist->current_order.IsType(OT_WAIT_COUPLE)) return;`
  ⇒ 等待车底连正常到站流程（`MakeWaiting()` / `cur_speed = 0` / 更新到站）都不走，
  就地停下，停车点的安全性没有任何判据过问。
- 两条合起来 ⇒ **等待车底可以合法地停在道岔上**。而它是挂接预留的终点 ⇒ 预留必然终止在道岔。

**所以完整的因果链是：**
`parked-waiter` 不选安全位（G5） → 车底停在道岔 → `tg` = 道岔 → `rp()` 无安全判定（G2/G3）
→ 预留压到道岔 → 机车开到末端、命中未成立 → **停在道岔上等挂**（真实危险）。

**这也修正了 §9 的修法建议**：只做 A（配对侧闸门）会变成"车底停在道岔上就永远不挂"，
等于把危险换成死等。必须配套：**等待车底停稳时也要保证自己停在安全位**
（即在 `parked-waiter` 门禁生效的那一刻，若脚下不是 `IsSafeWaitingPosition` 为真，
仍允许它订一次到最近安全位的短路线，然后才彻底静默）。
⇒ 建议 A′：`parked-waiter` 门禁加一个前提「脚下是安全位」；不是则放行一次订路让它挪到安全位。
`parked-no-pair` 侧同理需要检查"自己停的位置是否安全"。

**待验证 U-310-1**：日志里等待车底（如 veh=75 / veh=99 的配对目标）停的格子 track bits 是否多位。
本版探针不逐格打印车底所在格的 track bits，无法从 `R3R_debug.log` 直接读出 ⇒ 需一次性探针或玩家画面确认。

## 12. 玩家新方案 N-310-1（2026-10-26，原话要点）

> 「挂接寻路不寻车底所在格，去寻车底所在站台格，同时不把等待车底的预留当危险。」

拆成两条待论证的主张：
- **N-310-1a 终点改语义**：`tg` 由「车底链头格 `consist_head_tile`」改为「车底所在**站台格**」。
- **N-310-1b 危险重定义**：等待车底自身的预留（`ReserveTrackUnderConsist` 铺的脚下/整格位）不再被挂接路径当作障碍/危险。

待论证清单（本轮逐条给结论）：
1. 「车底所在站台格」在三种停位下的定义：车底在站台上 / 车底在站台外普通轨道 / 车底在道岔或咽喉。
2. 终点从"格"换成"站台格"后，`rp()` 的 `if (cur == tg) return true;` 会让机车提前若干格收车 ⇒ 机车在站台内停住、车底在站台外 ⇒ `GetCouplePosition`（机车鼻尖↔车底尾容差）永不成命 ⇒ 是否必然退化成 COUPLE-FAIL 刷屏。
3. 站台格本身的预留语义（`SetRailStationReservation` 整格位）能否天然提供"安全停车"效果；`IsSafeWaitingPosition` 对站台格返回什么。
4. N-310-1b 的实际收益：`CheckSafePositionOnNode` 现在把"车底自己占的格"判成什么，放开后的后果。
5. 与既有 `parked-waiter` 门禁（`:17430`，车底不订路 ⇒ 车底本无预留）是否冲突 —— 若车底本来就没有预留，N-310-1b 可能是空操作。

状态=论证中。

## 13. 论证结论（2026-10-26，源码取证完毕）

### 13.1 三条关键新材料

**F-310-1（颠覆上一轮表述）**：`consist_head_tile` **只在 best 节点是站台格时才解析**。
```894:894:src/pathfinder/yapf/yapf_rail.cpp
if (pNode != nullptr && IsRailStationTile(pNode->GetLastTile())) {
```
车底停在普通轨道/道岔时该格不是站台 ⇒ `consist_head_tile` 保持 `INVALID_TILE` ⇒ `1019` 的
`tg` 回落到 `pPrev->GetLastTile()`（回溯末端格）。所以"挂接寻路寻车底格"这句话在**站台外并不精确**，
它其实是"寻到 YAPF 终止节点"，`consist_head_tile` 只在站台场景才真正是"车底格"。

**F-310-2（玩家的第一半想法已经是现状）**：`rp()` 对站台格已有三处豁免——
- `1115: if (!IsRailStationTile(cur) && !IsRailDepotTile(cur)) rp_last_tile = cur;` ⇒ 预留目标只记普通轨道格；
- `1165`/`1191`：`if (!IsRailStationTile(f3.new_tile) && ...) reserve_dir_if_present(...)` ⇒ 不给站台格补 trackdir 预留；
- `1122-1142`：站台段只订轴轨道级 `plat_track`，且注释明确"车底持有整格预留时 TryReserveRailTrack 无害失败"。

⇒ **"预留不压到站台格"本来就是这样。** 玩家看到的"预留压到道岔"，前提是**车底不在站台上**。

**F-310-3（关键约束，推翻我自己上一轮的 A′）**：`IsSafeWaitingPosition`（`pbs.cpp:2387-2450`）
**对站台格没有任何豁免**，只认 depot、waypoint、信号、尽头站（`2408-2410`）。
⇒ **中间站台格 `IsSafeWaitingPosition` 返回 false**。所以上一轮提的 A′（要求车底脚下 `IsSafeWaitingPosition` 为真）
会把"车底正常停在中间站台"也判成不安全 ⇒ A′ 的判据必须改成「站台格 ∨ 安全位」。

### 13.2 两条主张逐条判定

| 主张 | 判定 | 依据 |
|---|---|---|
| **N-310-1a** 车底在站台格上时"改寻站台格" | **空操作**（已经是现状） | F-310-1 + F-310-2：`894` 只在站台解析车底、`1115`/`1165` 不给站台格订预留 |
| **N-310-1a** 车底在站台外（道岔/普通轨道）时"改寻站台格" | **不可行（内存矛盾）** | 该情形不存在"车底所在站台格"；若退而取"最近站台格"，机车会停在站台内，而 `GetCouplePosition` 要求"机车鼻尖↔车底尾"在容差内紧贴 ⇒ 永不成命 ⇒ COUPLE-FAIL 死循环。除非同时引入"末段无预留蹭行"（= 原方案 B 的全部风险） |
| **N-310-1b** 不把等待车底的预留当危险 | **已部分实现** | `FindSafeCouplePositionProc`（`yapf_rail.cpp:150-197`）对"车底自己的预留"本来就放行：`best->R3RIsCoupleTarget` + `R3RCoupleAllowed` + 无第二辆 + `FollowTrainReservation` 无别的车主。仍会 fail 的四种见下 |

`FindSafeCouplePositionProc` 仍会拒绝的四种（`yapf_rail.cpp:164-204`）：
`fail=notSeg`（不是等待段）/ `fail=2nd`（同格两辆车）/ `fail=other`（车底预留链已被别的车接管）/
`fail=tryReserve`（整格有第三方预留、抢不到）。
其中 **`other` 与 `tryReserve` 不能放开** —— 那不是"等待车底的预留"，是**第三方的通行权**，放开即撞车。

⇒ **N-310-1a 的施加对象错了：不该改"机车寻到哪"，该改"车底停在哪"。**

### 13.3 修正方案 A″（推荐，取代 A/A′）

判据（三选一即安全）：`IsRailStationTile(tile)` **∨** `IsRailDepotTile(tile)` **∨** `IsSafeWaitingPosition(...)`。
两个落点：
1. `parked-waiter` 门禁（`train_cmd.cpp:17430`）：加前提"脚下满足上述判据"，不满足则**仍放行一次订路**让它挪过去，挪到位后彻底静默；
2. 配对建锁处（`R3RGetCouplePairPartner` 成功那一刻）：再校验一次车底链头格，不满足不建锁（防"车底后来被推进道岔"）。

**效果**：车底一旦停在站台格，`894` 正常解析 `consist_head_tile` → `tg` = 站台格 → `rp()` 按 F-310-2 自动不把预留压出站台 → 机车开进站台紧贴车底命中。**pathfinder 一行都不用改。**

### 13.4 建议性补充

1. **判据必须含"站台格"独立项**（F-310-3），不能只写 `IsSafeWaitingPosition`；否则中间站台会被误判为不安全，把正常场景一起拦死。
2. **路点单独考虑**：`IsRailWaypointTile` 在 `IsSafeWaitingPosition` 里也返回 true（`:2395`，`treat_waypoint_as_safe` 参数），且路点同样吃整格预留（KI-207c）。若希望"停在路点等挂"合法，这条已自动成立；若不希望，需显式排除。
3. **挪位目标必须空**：`VehiclesOnTile(vt, tile)` 为空 + 有整格/轨道预留权限，否则会把车底推进另一列车里。
4. **挪位只允许一次**：失败即放弃配对并打一行日志（现场 `RESV-NOBOOK` 已有 10 033 条，不能再添每 tick 噪声）。
5. **尽头站台天然双判据通过**：`include_line_end=true` 时尽头站台格两者都为真，无额外处理。
6. **不要动 `FindSafeCouplePositionProc` 的 `other`/`tryReserve` 分支**（13.2 表下），那两处放开等于放弃第三方保护。

### 13.5 风险与边界

- 车底在站台格但被第三方整格预留：站台整格位是排他的，不会同时存在两方。
- 车底在车库格：`IsRailDepotTile` 已 true，行为不变。
- 车底在桥/隧道格：不在上述判据内 ⇒ 会被视为不安全。需先确认挂接场景是否存在桥隧等挂（若存在，判据要加 `IsRailTunnelBridgeTile`）。
- 本次**不改** `yapf_rail.cpp`；所有改动落在 `train_cmd.cpp` 的等待车底/配对两处 —— 也因此**不触发 `yapf_destrail.hpp` 量级的全量重编**。

状态=论证完毕，等玩家拍板 A″（或指出我漏掉的场景）。

## 14. 前提修正（2026-10-26，玩家原话）

> 「车底总是会有部分是压在站台格上面的，所以你的前提：车底停在道岔上，是可能正确的，但是，车底完全不在站台上，是完全错误的」

### 14.1 修正后的事实（源码复核）

`yapf_rail.cpp:894-945` 取的不是"车底整车在哪"，而是**车底靠机车那一端的格**：

```
935: if (best_on_consist)      consist_head_tile = pNode->GetLastTile();   // best 格上就是车底一端
937: else if (离 t 更近)        consist_head_tile = t->tile;                // 车底 Last() 端
940: else                        consist_head_tile = h->tile;                // 车底 First() 端
```

⇒ 车底跨站台边界（车身压站台、接头端在道岔）时，`tg`（`:1019`）= **接头端那一格 = 道岔格** ⇒ 预留一路铺到道岔。

⇒ 反过来，接头端落在站台格时 `tg` = 站台格，`rp()` 的站台豁免（`:1115` / `:1165` / `:1191`）自动生效，预留不出站台。**这就是"现象时好时坏"的来源：与整车无关，只看接头端落在哪一侧。**

### 14.2 对 N-310-1a 的重新判定（仍未通过）

"改寻车底所在站台格"等价于把 `tg` 从「接头端格」换成「车底压着的某个站台格」。

- 若该站台格 == 接头端格 ⇒ 与现状完全相同（空操作）。
- 若该站台格 ≠ 接头端格（接头端在道岔）⇒ 机车停在站台内，`GetCouplePosition`（`train_cmd.cpp:14109-14149`，`v_near = v->GetMovingFront()` vs `u->Last()`/`u`，判据是 `end_dist == need` 名义半长和）与接头端仍差整整若干节车长 ⇒ **永不命中** ⇒ COUPLE-FAIL 死循环。

⇒ 结论：**"寻站台格"只在接头端本来就在站台格时成立，而那时已是现状。** 它不能修"接头端在道岔"这个真正要修的情形。

### 14.3 判据必须改口径

A″ 的判据从「车底（整车/链头）在站台格」改为：

> **车底靠机车那一端（`t` 或 `h`，即 `consist_head_tile` 将要取的那一端）所在的格** ∈ {站台格, 车库格, 安全位}

注意 `consist_head_tile` 是在 pathfinder 里算的，pair 侧无法复算同一端点 ⇒ 需要在配对建锁处用同一套"近端"判据（`end_dist` 比较）取端点再判格型。

### 14.4 未解的关键问题（需玩家确认）

站台短于车底时，接头端**可能根本无法**落到站台格（怎么排都在道岔）。此时 A″ 只能拒绝配对 ⇒ 车底永远等不到。故必须确认：玩家场景里，车底是否**可以**把接头端停在站台格？（即站台是否能容纳到接头端）

## 15. 玩家第二次纠正：口径彻底改（2026-10-26）

> 玩家原话：「我认为你所描述的与我的场景几乎是八竿子打不着：1，我的站台，塞下挂接完成后的列车绰绰有余。2，机车和车底发出的预留，它们自始至终就没有接上」

### 15.1 因此以下三轮结论全部作废，不得再引用

- ❌「车底停在道岔上，所以要把它挪到站台」
- ❌「站台太短装不下车底，接头端物理上只能在道岔」
- ❌「现象是预留压到道岔/不安全位（unsafe stop）」

### 15.2 真实现象（写死）

1. 站台长度**足够**容纳挂接完成后的整列车 ⇒ 空间不是约束，任何"挪车底/站台太短"的推论都不成立。
2. **机车侧的预留 与 车底侧的预留，从头到尾没有接上** ⇒ 两段预留之间存在**空洞（gap，至少 1 格没有位）**。
3. PBS 不允许列车驶入没有预留的格 ⇒ 机车只能停在空洞前，与车底接头端始终差那 1~N 格 ⇒ 距离不等于名义半长和 ⇒ 永不命中 ⇒ 每帧重试。

⇒ 问题定性从「预留太长（压道岔）」改为「**预留太短/断开（两段预留不连续）**」。这是相反方向的缺陷。

### 15.3 待验证的缺口来源（候选，按可能性排序）

- **候选 1：等待车底根本不订"前方"预留。** KI-207c 的 RESV-NOBOOK 门禁（`TryPathReserveWithResultFlags` 里 `parked-waiter` 早退）就是为了"停稳的等待车底别再订整条出站路线"而加的；而车底脚下的 `ReserveTrackUnderConsist()` 只保护**车节自身所占的格**，不含车头前方那一格。⇒ 机车预留铺到车底前方一格时，下一格在"车底保护"与"机车路线"之间谁都没订。
- **候选 2：`rp()` 递归到 `cur == tg` 时提前 return true 且不订自己**（代码注释自陈"车头终点格只能靠进入格那一笔补"）⇒ 若终点格的补订失败（该格无可用位/有车/不是 plain rail 而是站台格），终点那一格就缺。
- **候选 3：站台格上有两套互不通用的位**——plain rail 的轨道预留位 vs 站台的整格预留位（`SetRailStationReservation`）。`rp()` 对站台走 `plat_track = AxisToTrack(axis)` 分支，若实际占位类型与判据不符，站台那几格既不算"我订的"也进不去。

### 15.4 下一步取证锚点（需要的日志行）

- `CPL-TRACE-RESULT` / `CPL-BEST veh= tile= td= cost=`
- `RP cur=x,y td= trk= bits=0x…`（逐格预留序列）
- `RESERVECONSIST veh= tile= ok= resNow=`
- `CPL-ENTRY tile=` / `CPL-HEAD best= head= on=`
- 关键判读：**RP 序列的最后一个格** 与 **车底所占的最靠机车那一格** 之间，坐标上差几格、差哪几格、那几格的 `bits=` 是多少（0 = 空，无位）。

判据：若空洞格 `bits=0x0` 且其上无车 ⇒ 是"谁都没订"（候选 1/2）；若空洞格是站台格且 `bits=0x0` 但站台整格位被车底持有 ⇒ 候选 3。

## 16. 玩家第三次纠正：时间顺序反转（2026-10-26）

> 玩家原话：「我再提出一个让你震惊的事实：机车发出预留时，作业站台上是空的！车底还在赶来的路上机车就发出了一个不安全也不合法不符合预期的预留」

### 16.1 这条事实改变了问题的性质

不是"两段预留接不上"（§15 的定性），而是**接不上的原因**：机车订预留的时刻，目标车底根本不在现场。

时间线（玩家口径）：

| # | 事件 | 站台 | 车底位置 | 机车预留 |
|---|------|------|----------|----------|
| 1 | 车底出发赶来 | 空 | 路上 | 无 |
| 2 | **机车发出预留** | **空** | **仍在路上** | **已铺出一条"不安全/不合法/不符合预期"的预留** |
| 3 | 车底到站台停稳 | 有车 | 站台 | 预留指向的那个格已经空掉/错位 |
| 4 | 挂车判定 | — | — | 两段预留不连续 ⇒ 差格 ⇒ 永不命中 |

⇒ **预留是"追一个移动目标"订出来的**，订的瞬间目标就在别的格；目标开走后，预留自然变成一段指向空地的孤线。

### 16.2 猜测根因（待验证，G-310-A）

`YapfTrainFindNearestCoupleTrain` 系列（`yapf_rail.cpp` 的 `FindNearestCoupleTrain` / `consist_head_tile` 选取）在 GOTO_COUPLE 机车的每次寻路里都会重新全图搜目标车底，**候选条件里没有"目标必须已停稳 / 已在目标站台"这一条**，于是：

- 车底在途 ⇒ 仍被选中为 `best`；
- `consist_head_tile`（`:935-941`）取的是**车底当时那一端所在的格**（移动中）；
- `rp()` 沿这条到"旧格"的路径逐格订预留 ⇒ 预留终点是一个马上会空掉的格，中途还会跨过站台/道岔边界；
- 车底开走后没有任何人重订，机车自己的 PBS 预留就成了一段"不安全也不合法"的线（终点不在安全等待位、也不是任何列车会到的地方）。

⇒ 这就同时解释了三件事：①站台空着也有预留；②预留指向的位置"不符合预期"；③后面两段预留接不上（因为中间那格从来没人回头补）。

### 16.3 待验证判据（取数点）

- 机车 GOTO_COUPLE 的寻路是否每 tick 都跑（`CPL-TRACE` 出现频率）。
- `CPL-BEST` 的 `veh=` 是否指向那个"还在路上"的车底，`tile=` 是否等于该车底当时的格（而非站台格）。
- 车底到达站台后，机车是否**重跑**过一次寻路（`CPL-BEST tile=` 是否更新为新格）；若不重跑 ⇒ 预留锁死在旧格，G-310-A 成立。
- `CPL-RESERVE` / `RP cur=` 末格与车底最终停稳格的坐标差。

### 16.4 若 G-310-A 成立的修法方向（先不改码）

在"是否允许为 GOTO_COUPLE 订路"的门禁里补一条：目标车底必须**已停稳（cur_speed==0）**且**其近端所在格已进入目标站台区**，否则本 tick 不订路、原地等（与 KI-207c 的 `RESV-NOBOOK` 同族，但对象是主动方而不是等待者）。

### 16.5 取证结论 1：候选判据里没有"停稳/到位"（代码事实，已取行号）

`yapf_destrail.hpp` 的 `PfDetectDestination()` 是挂车寻路决定"谁算目标"的唯一闸门，全部条件按顺序是：

| 行 | 判据 | 含义 |
|----|------|------|
| 606 | `R3RCoupleAllowed(Train::From(Yapf().GetVehicle()), t)` | 公司/分组/pair 锁允许 |
| 651 | `TrainFitStation(t)` | 站台装得下车底 |
| 662 | `R3RIsCoupleTarget(t)` | ★段头 + 订单为 `OT_WAIT_COUPLE` |
| 669 | `R3RIsCarOnlyFormation(t)` | 纯车底（零动力） |
| 686 | `t->IsPrimaryVehicle() && t->current_order.IsType(OT_WAIT_COUPLE)` | 独立链头且正在等挂 |

**这里没有、也不存在"目标必须已停稳（`cur_speed==0`）"或"目标近端已进入目标站台"这两条。** 745~800 行区间内也没有。

### 16.6 取证结论 2（推断 G-310-B，待验证）

正因 `R3RIsCoupleTarget()`（`train_cmd.cpp:1879`，记忆 KI-194 已核：含 `OT_WAIT_COUPLE`）要求目标**自己已经在 `OT_WAIT_COUPLE` 状态**，所以"还在赶来的路上"的车底（订单是 GOTO_STATION/GOTO_WAYPOINT）**必然不是候选** ⇒ 机车这一次 `FindNearestCoupleTrain()` 返回 `INVALID_TRACK`（日志 `CPL-PATHFOUND found=0`）。

⇒ 那用户看到的这条"不安全也不合法"的预留，**不是朝车底订的，而是挂车寻路失败之后退化成朝订单目的地（站台/车库）的普通行驶预留**。

⇒ 它"不符合预期"就在于此：机车此刻该做的是**原地等车底到位**，实际却按普通 GOTO 把自己开向站台并占住进路；车底到位后要进站，两段预留自然冲突/不连续，机车又因为站台被占/预留被切而停在外头 ⇒ 永远差格、永不命中。

### 16.7 待验证判据（下一轮取数点）

- 日志里 `CPL-PATHFOUND found=0` / `CPL-SCAN ... acc=0` 之后，**是否紧接着出现 `RP cur=` 或 `TRP` 预留行**。若有 ⇒ G-310-B 成立（失败退化成普通寻路）。
- 同一时段是否出现 `CPL-SKIP`/`resv=0`（真"不订"）；若全是"失败后照订" ⇒ 同样成立。
- 代码侧要看 `train_cmd.cpp` 的 `TryPathReserve()` 里 `OT_GOTO_COUPLE` 分支：`FindNearestCoupleTrain` 返回 `INVALID_TRACK` 之后是否直接 `return TPRRF_NONE`，还是落回 `ChooseTrainTrack()` 的普通路径。

## 17. 玩家给出设计口径：配对必须四条件 + 配对成功才允许寻路（2026-10-26）

> 玩家原话：「挂接配对必须是一个处于等挂，一个处于前往挂接且挂接分组相符合，两个都停稳才能配对，而我要求配对成功才能开始挂接寻路。这两个东西，应当形成一个闭环」

### 17.1 玩家口径（写死，后续一律以此为准）

**配对成立 = 以下四条同时为真：**

| # | 条件 | 一侧 | 另一侧 |
|---|------|------|--------|
| P1 | 一方订单 = `OT_WAIT_COUPLE`（等挂） | 车底 | — |
| P2 | 另一方订单 = `OT_GOTO_COUPLE`（前往挂接） | — | 机车 |
| P3 | 挂接分组相符合 | 双方 `couple_groups` 掩码相容 | |
| P4 | **双方都停稳**（`cur_speed == 0`） | 车底 | 机车 |

**闭环约束：** 配对不成立 ⇒ **禁止开始挂接寻路**（即不调 `FindNearestCoupleTrain`、不订任何预留、原地等）。配对成立 ⇒ 才允许寻路 → 预订 → 挂接。

### 17.2 这条口径直接否决当前实现的哪一部分

按 §16.5/16.6 已取证的代码事实：`PfDetectDestination()`（`yapf_destrail.hpp:606-696`）的候选判据只有 `R3RCoupleAllowed / TrainFitStation / R3RIsCoupleTarget / car-only / OT_WAIT_COUPLE`。**P4「双方停稳」不存在，且 P3「分组」在 `IgnoringPair` 版里被整段删掉了**（第 109 轮 KI-195 删除交集判据，记忆 41993530）。

⇒ 现状是：配对（P1~P3，缺 P4）在 `TrainCoupleHandler` 里判，**寻路层用的是另一套闸门**（`R3RCoupleAllowed`），两者不是同一个判据、也不是同一次判定 ⇒ **闭环根本没有形成**。这正是"车底在路上、站台空着，机车照样订预留"的直接原因：配对没成立，寻路照样跑。

### 17.3 闭环的正确形态（设计）

```
每 tick（车底与机车各自）:
  P = 配对判定(自己, 对方)  // P1&P2&P3&P4 全真
  if (!P) { 本方不得寻路；已订的预留若不该持有则释放；原地等 }
  else    { 允许 FindNearestCoupleTrain → rp() 订预留 → 命中 → Couple() }
挂接成功后: 双方清 pair 锁，回到普通订单态
```

要点：配对是**寻路的准入前提**，不是寻路的并行分支；且配对必须有**唯一的判定函数**，寻路层与守候层调同一个，避免"守候层判真、寻路层判假"的口径分裂。

### 17.4 待验证/待实现清单

- **C1** 找到配对锁的两个写入点（`r3r_couple_target` / `r3r_couple_requester` 的 setter 与调用者），确认两侧是同一 tick 内成对写入。
- **C2** 读 `R3RCouplePairMatches()` 实现，确认它当前是否含 P4（停稳）；若不含，就是必须补的那一条。
- **C3** 确认 `R3RCoupleAllowed()`（`yapf_destrail.hpp:606` 调的那个）与 `R3RCoupleAllowedIgnoringPair()` 的差异，找出**寻路层实际走的是哪一个**——若走 IgnoringPair，则 P3 在寻路层也不生效。
- **C4** 找 `OT_GOTO_COUPLE` 机车寻路的唯一入口，把"配对不成立 ⇒ 直接返回不订"的门禁插在**寻路之前**（而非寻路内部逐个候选否决），这样才真正闭合成环。
- **C5** 车底（等挂方）侧对应门禁已在 KI-207c（`RESV-NOBOOK`），但那是"停稳的等待者不订出站路线"，**不是配对门禁**，两者需并存而非互相替代。

## 18. 闭环现状核查（代码事实，行号已取）

### 18.1 环的钩子是对的，判据是残的

| 环节 | 函数 / 位置 | 现状 |
|------|-------------|------|
| 写配对锁 | `couple_group.cpp:563 R3RPairCoupleTargets()` | **不校验任何条件**，只把两个 ID 互指（`:575-576`） |
| 配对成立判定 | `couple_group.cpp:528 R3RCouplePairMatches()` | 只比 `c->r3r_couple_target==t->index && t->r3r_couple_requester==c->index`（`:534`）。**无订单、无分组、无速度** |
| 挂车寻路准入 | `yapf_destrail.hpp:606` → `couple_group.cpp:597 R3RCoupleAllowed()` | 第一关 `R3RCouplePairMatches`，第二关 `R3RCoupleAllowedIgnoringPair`（订单/分组/公司） |
| 回退安全检查 | `yapf_rail.cpp:178` | 同样走 `R3RCoupleAllowed` |
| 到达/命中闸门 | `train_cmd.cpp:14446`、`:18948`、`:19060` | 同样走 pair 判定 |
| 锁的写入时机 | `train_cmd.cpp:13743`，在配对扫描（间隔 `R3R_PAIR_RESCAN_TICKS`）里选完 `best` 后 | 一次性写入 |

⇒ **寻路层确实调的就是含 pair 的那个函数**（不是 `IgnoringPair` 版），所以"配对成功才寻路"的**钩子位置是对的**。

⇒ 缺的是判据：**P4「双方停稳」在整条链上任何一处都不存在**；P1/P2/P3 只在 `IgnoringPair` 里判，且锁一旦写下就**不再复检**——车底开走、机车动起来、分组变更，锁都还在。

⇒ 结论：这解释了 §16「站台空着、车底在路上，机车照样发了预留」——不是没有闭环，是**闭环的门槛里没有"停稳"这一条，且配对退化成了一次性缓存而不是每 tick 复核的条件**。

### 18.2 玩家口径与当前实现之间有一个死锁，必须先解开

玩家口径：`双方都停稳 ⇒ 配对 ⇒ 才允许挂接寻路`。

但机车要"停稳"，必须已经开到挂接位置；要开到挂接位置，必须已经寻路。⇒ **先停稳才能配对、先配对不对准寻路**，两条合起来把机车锁死在原地。

**唯一自洽的解释是两阶段：**

| 阶段 | 机车做什么 | 允许寻路吗 | 订什么预留 |
|------|-----------|-----------|-----------|
| 阶段 1 行驶 | 按 **普通 GOTO** 开到 GOTO_COUPLE 的目的地站台，减速停稳 | 普通寻路（不是挂接寻路） | 只订到站台的普通行驶预留 |
| 阶段 2 挂接 | 双方都停稳 + 配对成立后，才启动挂接寻路 | `FindNearestCoupleTrain` | 到车底的那段短预留 |

⇒ 现状把两个阶段**合成了一阶段**：机车在几百米外就开始挂接寻路、朝车底订预留，于是出现"站台还空着、车底还在路上，预留已经发出去了"。

### 18.3 待玩家拍板（按编号回填，勿重问）

- **C-1「停稳」的判据用哪个？** (a) `cur_speed == 0`（含滑行刚结束，日志里的是 `aStop/tStop` 之外的量）；(b) `vehstatus.Test(VehState::Stopped)`（只在站台/车库停靠时置位，普通轨道停住不算）。建议 (a)，因为车底可能停在非站台格。
- **C-2 阶段 1 的寻路确认？** 即 GOTO_COUPLE 机车的"前往"阶段按普通 GOTO 目的地行驶、**完全不订**到车底的预留，等停稳+配对成功后才切挂接寻路。是否认可这个两阶段？
- **C-3 车底在站台但仍在滑行时怎么办？** (a) 机车在阶段 1 终点等它停稳（推荐）；(b) 允许边滑行边配对，只在双方速度归零那 tick 才放开挂接寻路。
- **C-4 已订预留的撤销？** 配对条件在持有预留期间失效（车底开走/分组变更/锁被拆）时，是否立即释放本方的挂接预留（与 KI-207c 的 `RESV-NOBOOK` 同族，作用于主动方）。

### 18.4 落地预判（等 C-1~C-4 回填再动码）

- 最小改动点：`R3RCouplePairMatches()` 内追加 P1~P4 复核（一处补、五个调用点全覆盖），外加 `R3RPairCoupleTargets()` 写入时同样拒绝（避免写入不该存在的锁）。
- 第二阶段门禁：`yapf_destrail.hpp` 的 `PfDetectDestination` 前已有 `R3RCoupleAllowed`，停稳条件进 `PairMatches` 后自动生效，不必再改寻路。
- 风险：`PairMatches` 同时被命中/重叠闸门（`:18948/:19060`）使用，加 P4 可能影响"已经贴上去"的挂接判定；须核对那两处是否本来就要求速度 0。

## 19. 读码定案：G-310-B 证伪，闭环的洞在"配对存续期不复检"（2026-10-26）

### 19.1 G-310-B（失败后退化成普通寻路）**证伪**

`ChooseTrainTrack()`（`train_cmd.cpp:16571`）里 OT_GOTO_COUPLE 的挂车寻路分支，`found=0` 之后走的是：

```17051:17074:src/train_cmd.cpp
			/* R3R (KI-316, round 214): the round-212 fallback to the ordinary
			 * pathfinder ("no couplable consist anywhere -> drive to the
			 * destination of the GOTO_COUPLE order instead") has been removed on
			 * the player's ruling of 2026-10-03: a locomotive that finds nothing to
			 * couple onto must NOT set off towards the order destination, it must
			 * keep waiting where it stands ...
			FreeTrainTrackReservation(consist, origin.tile, origin.trackdir);
			if (mark_as_stuck) MarkTrainAsStuck(consist);
```

⇒ 找不到车底时**撤预留 + 标 stuck + 原地等**，不落回普通寻路。普通寻路那两处还被 `(!IsType(OT_GOTO_COUPLE) || r3r_couple_depot)` 守着（`:17110`、`:17136`），GOTO_COUPLE 非 depot 根本进不去。

⇒ **我上一轮推断的"失败退化成开往站台"不成立。** 那条"不安全也不合法的预留"不是退化的产物。

### 19.2 预留只在配对成立之后才订（闭环钩子确实存在）

- 挂车寻路成功 ⇒ `result_flags |= CTTRF_RESERVATION_MADE`（`:17107`）。
- 未就绪（`pair_ready == false`，`:17009`）⇒ 撤预留 + stuck + 原地等（`:17073-17074`），并打 `CPL-NOTREADY`（`:17081`）。
- 已就绪（配对锁在）⇒ 行驶中保持现状、继续走手上的预留（`:17017`：`if (keep_track != INVALID_TRACK && consist->cur_speed != 0 && pair_ready && r3r_wait_cut == 0) return { keep_track, result_flags };`）。

⇒ "配对成功才寻路"这一半**已经实现**了。

### 19.3 配对扫描里 P4 只做了一半

`R3REnsureCouplePair()` 的候选漏斗（`train_cmd.cpp:13674-13736`）：

| 行 | 判据 | 用户口径 | 现状 |
|----|------|----------|------|
| 13686 | 必须是整条链的链头（`t->First() == t`） | — | ✓ |
| 13692 | `R3RIsCoupleTarget(t)`（订单 `OT_WAIT_COUPLE` + 段头） | **P1** | ✓ |
| **13693** | **`t->vehstatus.Test(VehState::Stopped)` → 否则 `not-target` 跳过** | **P4（车底侧）** | **✓ 已有！** |
| 13703 | `R3RCoupleTargetAtOrderStation()` | **P2** | ✓ |
| 13723 | `R3RCoupleAllowedIgnoringPair()`（分组/公司） | **P3** | ✓ |
| — | **主动方（机车）自己是否停稳** | **P4（机车侧）** | **✗ 没有** |

⇒ 精确结论：**"两个都停稳"目前只做到了车底那一个**；机车侧没有，而且 `VehState::Stopped` 是"在站/停车"旗标，不是 `cur_speed == 0`。

### 19.4 真正的根因（可定案）：锁建立后永不复检

`R3RHasCouplePair()`（`couple_group.cpp:555-561`）只查两个 ID 是否互指：

```555:561:src/couple_group.cpp
bool R3RHasCouplePair(const Train *v)
{
	...
	return head->r3r_couple_target != VehicleID::Invalid() || head->r3r_couple_requester != VehicleID::Invalid();
}
```

于是：
1. 车底曾停稳 ⇒ 13693 放行 ⇒ 锁建立（`R3RPairCoupleTargets`，`train_cmd.cpp:13743`）。
2. 车底随后开走（离开等待位、去作业站台）。
3. **没有任何人撤销这把锁**（`R3RUnpairCoupleTargets` 的调用点全在挂接成功/订单推进处，没有"对方动了"这一条）。
4. 机车 `pair_ready == true` ⇒ 走 17017 的"行驶中保持现状" ⇒ 带着**指向旧格**的预留继续开。
5. 车底到站台时，机车的预留终点是车底**离开前**的格 ⇒ 两段预留不连续 ⇒ 永不命中。

⇒ **用户观察到的三件事全部对上**：站台空着（车底还在路上，但它"曾经停稳过"所以锁已成立）、机车有预留（17017 保持现状）、预留不安全不合法（指向一个已经空掉的格）。

⇒ 这就是"闭环没形成"的准确位置：**不是钩子没装，是环上少了一个"条件失效就拆环"的动作。**

### 19.5 修法（最小、且不引起抖车）

| 位置 | 改动 | 理由 |
|------|------|------|
| `train_cmd.cpp:13693` 附近 | 加主动方停稳判据：机车 `cur_speed == 0`（或 `VehState::Stopped`）才允许建立配对 | 补 P4 机车侧。**注意死锁**：机车必须已在等待位/侧线停稳才配对，不能要求它"开到挂接位置停稳"——见 §18.2 |
| `R3RCouplePairMatches()` 或 `R3RHasCouplePair()` | 存续期复检：车底不再 `Stopped`/已离开等待位 ⇒ 返回假，并调 `R3RUnpairCoupleTargets()` | 补"条件失效就拆环"，这是根因 |
| 存续期**不要**要求机车停稳 | 机车在开过去的路上必然 `cur_speed != 0`；若要求它停稳，17017 分支永不成立 ⇒ 每 tick 撤预留 ⇒ 复现第 271 轮修掉的抖车 | 口径必须区分"建立时停稳"与"存续时车底仍停稳" |

### 19.6 待玩家确认（C-5/C-6）

- **C-5** 存续期复检的对象：是"车底必须仍在 `Stopped`"，还是"车底必须仍在**原格**（tile 没变）"？前者宽（它原地转向/小幅移动仍算），后者严（一格都不许动）。倾向前者 + 「车底订单从 `OT_WAIT_COUPLE` 变为其他 ⇒ 立即拆锁」。
- **C-6** 拆锁瞬间机车该做什么：撤预留 + 原地等（复刻 `:17073-17074`），还是保留锁只撤预留？

## 20. 玩家否证：「车底一直没在站台执行等待挂接」（2026-10-26）—— §19.4 的"曾经停稳过"撤回

> 玩家原话：「什么意思啊，什么叫车底曾经停稳过，车底它一直就没在站台上执行过等待挂接的命令啊」

### 20.1 撤回

§19.4 第 1 步写的"车底**曾经**停稳过 ⇒ 13693 放行 ⇒ 锁建立"是**推断，无证据**（既无日志行，也无代码路径证明）。玩家已明确否证该前提：车底全程在路上，从未在站台执行 `OT_WAIT_COUPLE`。**该解释作废，不得再引用。**

### 20.2 否证带出的硬矛盾（这才是真问题）

若车底订单从不是 `OT_WAIT_COUPLE`，则配对扫描 `train_cmd.cpp:13692` 的 `R3RIsCoupleTarget(t)` 必为假 ⇒ 候选被跳过 ⇒ `best == nullptr` ⇒ `R3RPairCoupleTargets()` 不被调用 ⇒ **锁不该存在**。

可是机车的 `OT_GOTO_COUPLE` 预留确实发出去了（玩家现场）。⇒ 二者只能有一个成立：

| 假设 | 内容 | 若成立的后果 |
|------|------|--------------|
| **A** | `train_cmd.cpp:13693` 的语义被我读反了——它若实际是"**只有** `Stopped` 才放行"，那空车底在路上会被跳过；若实际是"`Stopped` 就跳过"（我当前读数），则**只挑行驶中的车底配对**，正合现场现象 | 配对判据方向性 bug，P1/P4 全部失效 |
| **B** | 配对锁不是"寻路前提"，而是"**寻路成功后回写**"——`R3RPairCoupleTargets()` 存在 `R3REnsureCouplePair` 之外的第二个调用点（例如挂车寻路命中处） | 玩家要的闭环被**倒置**：先寻路、后配对 |

A 与 B 互斥，且都能单独解释"车底在路上、机车有预留"。必须先定哪一个是真，否则改码必错。

### 20.3 本轮取证判据（逐条落结论）

- **E-1** 重读 `train_cmd.cpp:13686-13700` 原文，确认 `:13693` 是 `if (Stopped) continue` 还是 `if (!Stopped) continue`。
- **E-2** 列出 `R3RPairCoupleTargets(` 的**全部**调用点（不只是 13743），特别是 `yapf_rail.cpp` / `yapf_destrail.hpp` / `train_cmd.cpp` 命中段。
- **E-3** 读 `R3RIsCoupleTarget()` 定义，确认其是否真要求 `OT_WAIT_COUPLE`。
- **E-4** 若 A、B 都不成立，则第三假设 **C**：锁是**上一次**挂接遗留未拆（`R3RUnpairCoupleTargets` 漏调用），车底换了身份的旧锁残留 ⇒ 需查拆锁点覆盖度。

## 21. 定案（读码实证）：P1/P4 已实现，但被 `:13693` 的方向性判据反向绞杀（2026-10-26）

### 21.1 三份原文（行号 + 逐字）

**① `R3RIsCoupleTarget()` 已按玩家口径实现（`train_cmd.cpp:2483-2532`），且日期就是今天：**

```2517:2532:src/train_cmd.cpp
	/* R3R (2026-10-26, 玩家硬要求「第 4 步」): 目标必须在作业车站**停稳且符合条件**
	 * 之后才算合法候选。
	 * 旧判据只认订单类型（`OT_WAIT_COUPLE`），不认速度：等待车底把订单切到
	 * WAIT_COUPLE 的那一刻往往还在站台内滑行，挂接方当场就把它锁成 CPL-PAIR ...
	 * 代价（预期行为）：对方进站滑行期间 `found=0`，挂接方继续原地等；
	 * 对方停稳后下一 tick 即可被锁。 */
	if (t->cur_speed != 0) return false;
	return t->current_order.IsType(OT_WAIT_COUPLE);
```

⇒ **P1（等挂订单）+ P4 的车底侧（停稳）都已经在代码里了。**

**② 但配对扫描在此之前还有一道反向判据（`train_cmd.cpp:13693`）：**

```13692:13693:src/train_cmd.cpp
		if (!R3RIsCoupleTarget(t)) { ++r3r_s_target; r3r_near_note(t, "not-target"); continue; }
		if (t->vehstatus.Test(VehState::Stopped)) { ++r3r_s_stop; r3r_near_note(t, "stopped"); continue; }
```

⇒ `VehState::Stopped` 为真的车底被 `continue` **跳过**。

**③ 写锁函数不校验任何条件（`couple_group.cpp:563-578`）：**

```575:577:src/couple_group.cpp
	c->r3r_couple_target = t->index;
	t->r3r_couple_requester = c->index;
	return true;
```
（进函数前只有 `nullptr` 与 `c == t` 两道检查；`:572-573` 还会先把双方原有的锁清掉。）

### 21.2 两条判据的交叉表（这就是全部真相）

| 车底状态 | `:2530` `cur_speed == 0` | `:2531` `WAIT_COUPLE` | `:13693` `VehState::Stopped` | 能否被锁 |
|---|---|---|---|---|
| **在站台停稳、订单 WAIT_COUPLE**（玩家口径要的合法情形） | ✓ | ✓ | **真 ⇒ continue 跳过** | **✗ 被拒** |
| 在普通轨道/路点静止、`VehState::Stopped == false`、订单 `WAIT_COUPLE` | ✓ | ✓ | 假 ⇒ 放行 | **✓ 被锁**（现场现象） |
| 行驶中（无论订单） | ✗ | — | — | ✗ |

⇒ **`cur_speed == 0` 只能证明"停住"，不能证明"停在作业车站站台上"。** 停在线路上静止等挂的车底满足前三条、绕过第四道，于是被锁定、机车随即向它订预留——**而它从没在站台执行过等挂，正是玩家描述的场景。**

⇒ `R3RIsCoupleTarget` 的注释自陈口径是"抵达作业车站停稳"，但代码只落地了"停稳"，**"在作业车站站台上"这一条缺失**。`train_cmd.cpp:13703` 的 `R3RCoupleTargetAtOrderStation()` 比的是"机车目的站 vs 车底订单站"（KI-319），**不是**"车底此刻物理上是否站在那个站台格上"。

⇒ 同时暴露出反向 bug：`:13693` 把 `VehState::Stopped` 的车底排除，恰好把玩家口径里**唯一合法**的那一类（站台停稳等挂）挡在门外。两道判据互为反义，必须有一道是错的。

### 21.3 结论定性

- 上一轮 §19.4「车底曾经停稳过」——**错，撤回**（玩家否证成立）。
- §20.2 的假设 A（方向读反）——**部分成立**：不是我把 `:13693` 读反，而是 `:13693` 与 `:2530` 的设计意图相反，两者共存即 bug。
- §20.2 的假设 B（锁由寻路成功后回写）——**不成立**：`R3RPairCoupleTargets` 唯一实际调用点是 `:13743`（扫描选完 best 之后），寻路层 `R3RCoupleAllowed` 走的是"先判 pair 再放行"，锁确实是前提。
- 玩家要的闭环**判据层面已基本落地**（P1/P2/P3 有、P4 车底侧今天刚加），**缺的是**：(a) "在作业车站站台格"这一条；(b) `:13693` 方向错误（排除 Stopped）；(c) P4 的机车侧（主动方停稳）仍未实现；(d) 锁建立后不复检（§19.4 第 3 步仍然成立，与本条并存）。

### 21.4 待验证 E-5（下一轮取证）→ 已于 §22 证伪并撤回

`VehState::Stopped` 的置位点：若它在"站台停靠"时置位（`TrainEnterStation` 一类），则 `:13693` 必然绞杀全部合法候选，修法清晰；若它只在"玩家停驶/故障"时置位，则 `:13693` 是合理的（排除停驶车），问题就只剩 "没有校验站台格" 一条。**必须先确认，否则改错方向。**

## 22. 撤回 §21 的"判据互反"结论 + 本轮交付配对锁探针（2026-10-26）

### 22.1 `VehState::Stopped` = 「停驶/停运」旗标，不是「站台停稳」

置位/清除点（逐条取证）：

| 锚点 | 原文语义 |
|---|---|
| `ground_vehicle.cpp:64` | `if (total_power == 0) this->vehstatus.Set(VehState::Stopped);`（零功率车自动停驶） |
| `vehicle_cmd.cpp:344-349` | 车库「停放/停驶」按钮：`StopSeparation()` + `Set(Stopped)`；再点一次 `Reset(Stopped)` |
| `train_cmd.cpp:14940-14941` | `if (v->gcache.cached_power == 0 && !R3RIsCarOnlyFormation(v)) v->vehstatus.Set(VehState::Stopped);` |
| `articulated_vehicles.cpp:504` | artic 部件 `Reset(Stopped)` |
| `sl/vehicle_sl.cpp:619` | 非前引擎的引擎节 `Set(Stopped)` |

⇒ **撤回 §21.2 的交叉表与 §21.3 的"方向性 bug"定性。** `:13693`（跳过 `Stopped`）与 `:13622`（存续期要求 `!Stopped`）都是**合理**的：被停驶的车不该被挂、也不该继续占锁。站台上正常停稳等挂的车底是 `cur_speed == 0` 且 `Stopped == false`，两道判据都放行，**不会被绞杀**。

- §21.3 中「`:13693` 与 `:2530` 方向相反即 bug」——**证伪，撤回**。
- §21.3 中「缺『在作业车站站台格』校验」——**仍然成立**，且是本轮之后唯一确认的判据缺口（见 22.2）。
- §19.4 / §20.2 的其余结论不变（锁建立后无存续复检、`R3RPairCoupleTargets` 写锁零日志）。

### 22.2 由此得到的唯一判据缺口

`R3RIsCoupleTarget()`（`train_cmd.cpp:2517-2531`）只要求 `cur_speed == 0` + `Order == OT_WAIT_COUPLE`。`cur_speed == 0` 也包含：

- 信号灯/闭塞前停车；
- 让行/等待点前的临时停车；
- 线路中间因前车占用而停住。

⇒ 「车底在路上、却满足停稳判据」被锁成立，与玩家现场一致。**修法（待玩家拍板，未实施）**：加「`IsRailStationTile(t->tile)` 且该站台属于机车 GOTO_COUPLE 的作业车站」这一条（`R3RCoupleTargetAtOrderStation()` 现在只比订单站 ID，不校验车底物理所在格）。

### 22.3 本轮交付：配对锁探针（只读，不改任何行为）

起因（玩家原话）：「这已经是不知道第几轮你没有给我满意的结果了，我希望你加探针，要求能够得知被配对锁配对的车」。

事实：`R3RPairCoupleTargets()`（`couple_group.cpp:563-578`）**只写两个 ID，不打任何日志**；旧的 `CPL-PAIR`（`train_cmd.cpp:13859` 一带）受 `R3REDGE_CPLPAIR` 边沿节流，且只给 act/tgt/dist/tile，看不出锁建立时双方处于什么状态。⇒ 这就是"查不到"的根因。

新增三支探针（全部在 `src\train_cmd.cpp`，未碰任何 `src\*.h`）：

| 探针 | 位置 | 触发 | 用途 |
|---|---|---|---|
| `CPL-PAIRLOCK` | `R3REnsureCouplePair()` 成功写锁之后（`TryPathReserveWithResultFlags` 之后） | 锁建立瞬间，**不节流** | 谁锁了谁 + 双方状态 |
| `CPL-PAIRUNLOCK` | ① 订单不再是 `OT_GOTO_COUPLE` 的静默拆锁（`why=order-changed`）②`DecoupleTrain()`（`why=decouple`）③挂接成功（`why=coupled`） | 锁被解除时 | 谁被解开了、为什么 |
| `CPL-PAIRMAP` | `R3REnsureCouplePair()` 入口，每 256 tick | 周期 | 当前**全部存活锁**一次 dump；无锁时打 `locks=0` |

行内字段（`act` = 主动侧/机车，`tgt` = 被配对的车底；`CPL-PAIRMAP` 用 `v`）：

```
<tag>=<vehicle id> <tag>Head=<链头 id> <tag>Tile=x,y <tag>Spd=<cur_speed>
<tag>Ord=<current_order 类型，17=OT_WAIT_COUPLE,16=OT_GOTO_COUPLE>
<tag>Stopd=<VehState::Stopped 0/1> <tag>Front=<IsFrontEngine 0/1>
<tag>Len=<cached_total_length> <tag>PairT=<r3r_couple_target，65535=无>
<tag>PairR=<r3r_couple_requester，65535=无>
```

读法要点：
- **`PairT`/`PairR` 是锁的本体**：机车侧应见 `PairT=<车底 id>`，车底侧应见 `PairR=<机车 id>`。两边对不上就是锁写坏了。
- `Spd=0` + `Ord=17` + `Stopd=0` 才是「站台停稳等挂」的合法形态；若 `Spd=0` 但 `Tile` 不在作业车站站台格上，就是 22.2 的漏判（本轮要抓的现场）。
- `CPL-PAIRMAP` 每 256 tick 一行/一锁，可回答"此刻谁配给了谁"，不依赖事件点。

### 22.4 复测判据

1. 复跑同一场景，`CPL-PAIRLOCK` 出现且 `tgt` 就是玩家认为"不该被配"的那列车 —— 直接坐实配对对象。
2. 该行 `tgtTile` 不在作业车站站台格上（或 `tgtSpd != 0`）—— 即判据缺口。
3. `CPL-PAIRMAP` 里锁的生命周期可见：从 `role=requester` 出现到 `CPL-PAIRUNLOCK` 消失，中间不再有"无痕"窗口。
4. 正常场景：`CPL-PAIRLOCK` 后应紧跟 `CPL-PAIRUNLOCK why=coupled`，且 `CPL-PAIRMAP` 不再列出该对。
5. 负判据：停驶车（车库停放按钮按下，`Stopd=1`）不得出现在任何 `CPL-PAIRLOCK` 的 `tgt` 位。

### 22.5 构建自证

复用 `_tmp_inc_build.cmd`（未新建任何 `.cmd`，遵守既有规则）：

- `src\train_cmd.cpp` 13:57 → `build\CMakeFiles\openttd_lib.dir\src\train_cmd.cpp.obj` 13:59 → `build\openttd.exe` **14:01:36**（晚于源码与 obj）；
- `build\R3R_incbuild.done` = `EXIT_CODE=0`；日志尾部 `[n/n] Linking CXX executable openttd.exe`；错误计数 0；
- `read_lints` 0 条；
- `findstr` 在 `build\openttd.exe` 中命中 `CPL-PAIRLOCK` / `CPL-PAIRUNLOCK` / `CPL-PAIRMAP` 三串（字面量已入库，探针确实编进产物）。

改动范围：仅 `src\train_cmd.cpp` 新增 3 个 static 函数（约 `:13515-13588`）与 4 处调用点（`:10212` / `:12565` / `:13619` / `:13629` / `:13860`），零行为变更。

## 二十三、第 311 轮复测取证：配对锁被「被动侧」自拆（现象报告）

输入：玩家提供的 `D:\temp\codebuddy-dropped-files\065d8a73-702b-481e-b31e-ab3b3740ff02\R3R_debug.log`（44 587 行，末行 tick≈3032）。

读数锚点（行号 = 该日志行）：

- 锁确实建立了：`44511: CPL-PAIRLOCK tick=3032 why=scan act=99 actOrd=16 actTile=298,235 actSpd=30 actPairT=75 actPairR=1048575 tgt=75 tgtOrd=17 tgtSpd=0 tgtTile=265,234 tgtPairT=1048575 tgtPairR=99` ⇒ 双向自洽（`actPairT=75` / `tgtPairR=99`），被锁的 75 正是 `OT_WAIT_COUPLE(17)` 且 `Spd=0` 的等待车底。
- 同 tick 被拆：`44510 / 44513 / 44516 / …: CPL-PAIRUNLOCK tick=3032 why=order-changed act=75 actOrd=17 actTile=265,234 ... tgt=99` ⇒ 拆锁的是**被动侧 75 自己**，不是主动侧。
- 结论在扫描器上可见：`44572: CPL-PAIRSCAN tick=3032 act=99 state=steady ... best=75 paired=-1` 与 `44575: CPL-PAIRSCAN tick=3032 act=99 state=locked ... best=75 paired=1 wait=0 near=21 nearWhy=not-target` 交替出现 ⇒ 锁在「建立→拆除」之间抖动，`paired` 在 1 与 -1 之间翻转。
- 计数：全日志 `COUPLE-OK` 仅 **3** 条，而 `why=order-changed` 拆锁 **13 580** 条 ⇒ 抖动是全程主形态。
- 伴随现象：`44512: CPL-S0-CHK veh=21 ... stuck=1 fp=0 prog=0 wait=223→245`（停驶原地等待计数持续递增）、`44559: RESV-NOBOOK veh=51 reason=parked-no-pair`、`44565: RESV-NOBOOK veh=21 reason=parked-no-pair` ⇒ 两台 GOTO_COUPLE 机车因「无配对」被拒订路，整段停在 252,234 / 253,233。

根因（待验证）：`src\train_cmd.cpp:13612 R3REnsureCouplePair()` 由 `TrainLocoHandler()` 在 `:21481` 对**所有**前机车（含等待车底）无条件调用；其中 `:13626` 分支判断 `!coupler->current_order.IsType(OT_GOTO_COUPLE)` 就 `R3RUnpairCoupleTargets(coupler)`，而该函数（`src\couple_group.cpp:580`）会**同时清对方字段**。于是被挂方（订单为 `OT_WAIT_COUPLE`）每 tick 把主动侧刚建立的锁清掉。

判据影响：KI-492 复测判据第 4 条「不得出现锁凭空消失」**不通过**；`CPL-PAIRLOCK` 三支探针本身工作正常。

### 23.1 追问：75 的订单不是 WAIT_COUPLE，怎么建立配对锁？（2026-10-26 玩家提问）

答：75 **就是** WAIT_COUPLE(17)。日志双向互证：`44511` 的 `tgt=75 tgtOrd=17`、`44510` 的 `act=75 actOrd=17`。

搭锁的不是 75，是 99 的扫描；代码锚点：

- `R3REnsureCouplePair()`（`src\train_cmd.cpp:13612`）—只有**主动链**自身订单是 `OT_GOTO_COUPLE` 才继续（`:13626` 的否定分支就是拆锁）。
- 候选判据 `R3RIsCoupleTarget()`（`src\train_cmd.cpp:2483-2531`）五道全过才算目标：`IsSegmentFront`、自己是链头（`Train::From(t->First())==t`）、无真 artic（`R3RChainHasRealArticPart`）、`cur_speed==0`、`current_order.IsType(OT_WAIT_COUPLE)`（最后一行是 `return`，即 WAIT_COUPLE 是**唯一**合法目标状态）。
- 写入无协商：`R3RPairCoupleTargets()`（`src\couple_group.cpp:563-578`）先对两侧各调一次 `R3RUnpairCoupleTargets()`，再 `c->r3r_couple_target=t->index; t->r3r_couple_requester=c->index;`。

⇒ 目标侧从不需要 GOTO_COUPLE；它唯一的"意愿标记"就是 WAIT_COUPLE。而 17 ≠ 16，所以 75 每 tick 都命中 `:13626` 的「不是 GOTO_COUPLE ⇒ 锁过期」分支，`R3RUnpairCoupleTargets()`（`couple_group.cpp:580-595`，**会同时清对方字段**）把 99 的 `r3r_couple_target` 一并清掉。行序也证明这是循环不是一次性：`44510` UNLOCK(75→99) 写在 `44511` LOCK(99→75) **之前**，同一 tick 内先拆后建。

### 24. 全场景建锁序列取证：第 4~10 次（2026-10-26 玩家要求）

总量（同一日志）：`CPL-PAIRLOCK` **13 584** 条，`CPL-PAIRUNLOCK` **13 586** 条 ⇒ 1:1 抖动，全场景几乎不存在"锁存活"的时段。

阶段划分（两个完全不同的场地）：

- **阶段 A**：`tick=2338`，全部在**车库** `1889,188`（`DEPOTBLK ... track=80 isdepot=1 depotIdx=8`），`actSpd=tgtSpd=0`、`actLen=tgtLen=96`，行 1691 起一直循环到 1859 仍未停。
- **阶段 B**：`tick=3032`，`act=99 actTile=299,235 Spd=30` / `tgt=75 tgtTile=265,234 Spd=0`（行 44580/44583/44586），即 §23 已分析的那一段。

第 1~10 次建锁逐条（行号 → 主动/目标）：

| # | 行 | tick | act→tgt | 备注 |
|---|---|---|---|---|
| 1 | 1691 | 2338 | 21→24 | 库内首次，24 是 `WAIT_COUPLE` |
| 2 | 1723 | 2338 | 51→24 | 1705 行 24 刚拆掉 21→24 |
| 3 | 1748 | 2338 | 99→75 | |
| 4 | 1763 | 2338 | **21→123** | **21 换了目标** |
| 5 | 1768 | 2338 | 51→24 | |
| 6 | 1772 | 2338 | 99→75 | |
| 7 | 1776 | 2338 | 21→123 | |
| 8 | 1779 | 2338 | 51→24 | |
| 9 | 1782 | 2338 | 99→75 | |
| 10 | 1785 | 2338 | 21→123 | |

第 4 次换目标的原因：`1761: CPL-PAIRSCAN tick=2338 act=21 ... s_target=2 s_lock=2 best=123 paired=1 near=24 nearWhy=locked` ⇒ 24 已被 51 占住，21 按 `R3RCouplePairOutranks` 抢不到，扫描于是跳到 123。123 是**刚克隆出来的新链**（`1662-1663: GEO settle-clone ... head=123 n=24`、`1666 ORD-IDX-WATCH tag=clone veh=123 ... n=17`），与 24/75 完全同构（`tgtOrd=17`、`tgtSpd=0`、`tgtLen=96`、同一格）。

第 5 次起没有新组合，稳定成**三条固定配对交替抖动**，循环体固定为（`1767-1859` 逐行重复）：

```
RESV-NOBOOK veh=51 ... reason=depot-paired
CPL-PAIRLOCK(51→24)   ← 24 在 1766 先拆掉 51 的锁
CPL-PAIRUNLOCK(75→99) ← 75 拆 99
CPL-PAIRLOCK(99→75)
CPL-PAIRUNLOCK(123→21)← 123 拆 21
CPL-PAIRLOCK(21→123)
CPL-PAIRUNLOCK(24→51) ← 24 又拆回来，回到起点
```

判定：

1. 抖动**不是**"某一对锁坏了"，而是**每一对都被被动侧（Ord=17）自拆**，机制与 §23 相同；无人拆锁时（比如 1755-1760）锁本来能存活。
2. 主动侧 21/51/99 全在车库内且 `actSpd=0`，每条建锁前都有 `RESV-NOBOOK reason=depot-paired` ⇒ 它们因"已配对"被拒订路，**永远出不了库**，抖动因此可以无限持续（日志里已持续到 1859 行仍未停）。
3. 因此"第 4 次换目标"只是被 51 抢占后的顺带结果，不是独立故障；用户观察到的"第 4 次之后行为变了"实际是**配对关系被锁定成三环**。

待验证：`tick=2338` 到底覆盖多少行（1683 起至 ?）——若这一整段都在同一 tick 内，则是同一 tick 内重入；若 `_tick_counter` 粒度粗，则是数百帧的持续抖动。两者都不影响根因，只影响"抖动速率"的口径。

### 25. 车库外第一次建锁（2026-10-26 玩家要求）

定位方式：`CPL-PAIRLOCK.*1889,188` 的最后一条 = **行 32436**；`CPL-PAIRLOCK(?!.*1889,188)` 的第一条 = **行 35530**。⇒ 车库内抖动在 32436 收尾，中间约 3094 行无建锁（车库里的车陆续开走）。

**行 35530，`tick=2922`，`why=scan`：**

```
CPL-PAIRLOCK tick=2922 why=scan act=99 actTile=253,233 actSpd=0 actOrd=16 actLen=96 actPairT=75
  tgt=75 tgtTile=265,234 tgtSpd=0 tgtOrd=17 tgtLen=96
CPL-PAIR   tick=2922 act=99 tgt=75 dist=13 actTile=253,233 tgtTile=265,234
CPL-PAIRSCAN tick=2922 act=99 state=steady ... best=75 bestDist=13 paired=-1 wait=-1
CPL-PAIRUNLOCK tick=2922 why=order-changed act=75 actOrd=17 ...
```

与车库内（§24）的差别，逐条列清：

| 维度 | 车库内（tick=2338，行 1691~32436） | 车库外第一次（tick=2922，行 35530） |
|---|---|---|
| 主动车速度 | `actSpd=0` 全程 | `actSpd=0` → 35561 `actSpd=27` → 35564 `actSpd=30`，**在跑** |
| 距离 | `dist=0/2/3`（同格或相邻） | `dist=13` |
| 目标车 | 24/123/75，同格 `1889,188` | 75 @ `265,234`，静止 `tgtSpd=0 tgtOrd=17` |
| 建锁前的障碍 | 无 | `35529 CPL-SAFE-FAIL veh=99 tile=259,234`，`35525 FSCP tile=259,233 fail=tryReserve`，`35528 FSCP-VEH tile=259,233 veh=122 track=0x1` ⇒ 安全位被 veh=122 占 |

判定：

1. 车库外第一次建锁**发生在主动车自己还没到位时**：99 的订单是 `GOTO_COUPLE`（`real=8`），`35484 COUPLE-WAIT-OFFDEST veh=99 tile=253,233 mf=259,233 destStn=12`，即它还在往目标车底走的途中；`R3REnsureCouplePair()` 的扫描只看"有没有合法候选（`R3RIsCoupleTarget`：`WAIT_COUPLE` + `cur_speed==0`）"，**不要求主动车已抵达**，于是提前把锁挂上 75。
2. 挂上之后 75 立刻拆（`35534`），因为 75 自己是 `WAIT_COUPLE(17)`，走 `:13626`"不是 `GOTO_COUPLE` ⇒ 拆"分支——**与车库内同一根因、零差异**。
3. 循环形态随之变了：车库内是"三对对称抖动"，这里是**单车对的连续抖动**（35530 建 / 35534 拆 / 35537 建 / 35541 拆 / 35544 建 / 35560 拆 / 35561 建 / 35563 拆 / 35564 建…），周期约 3~17 行，随 99 加速跑完全程都不会停。
4. 新增疑点 **REG-310-E**：`35486 / 35508 / 35509` 出现 `RESV-NOBOOK veh=99 tile=253,233 order=16 caronly=0 reason=parked-no-pair`，而 99 既不在站台也没停稳（`35546 STUCK ... spd=26`）。`reason=parked-no-pair` 不是第 127 轮 `parked-waiter`、也不是第 311 轮的 `depot-paired`，说明还有第三个 `RESV-NOBOOK` 出口；需回 `src\train_cmd.cpp` 确认它的守卫，否则可能又是一处"以配对状态为由拒绝订路"。
5. 顺带说明：车库外这一段的 `CPL-PAIRSCAN state=locked → steady paired=-1` 紧跟在建锁之后，即扫描先认为"已配对"（`state=steady`），下一行就被被动侧拆掉 ⇒ `state` 字段的取值不能作为"锁存活"的证据。

### 25.1 代码锚点：`parked-no-pair` 与拆锁**互锁**（REG-310-E 已解答）

`src\train_cmd.cpp:17495`（第 306 轮，玩家硬要求"配对存在前不得启动"）：

```17491:17519:src/train_cmd.cpp
	const bool r3r_goto_couple = consist->current_order.IsType(OT_GOTO_COUPLE);
	const bool r3r_in_depot = IsRailDepotTile(consist->tile);
	const Train *const r3r_partner = r3r_goto_couple ? R3RGetCouplePairPartner(consist) : nullptr;

	if (r3r_goto_couple && r3r_partner == nullptr && !r3r_in_depot) {
		*reason = "parked-no-pair";
		return true;
	}
	...
	if (consist->cur_speed != 0) return false;
```

注意这道门在**速度门之前**、且**不看速度**：只要 `GOTO_COUPLE` 且配对锁为空且不在车库格，就拒绝订路。

⇒ 车库外第一次建锁的真正因果不是"扫描提前锁"，而是**闭环自锁**：

1. 99 的订单已推进到 `GOTO_COUPLE(16)`、`real=8`，锁为空（`35507 state=backoff … paired=0`）⇒ `parked-no-pair` 命中 ⇒ `RESV-NOBOOK` + `TRP ok=0 res=0`（35508/35509、35536）。
2. `R3REnsureCouplePair()` 每 tick 独立扫描（不依赖寻路），扫到 75 合法 ⇒ 35530 建锁。
3. 锁一存在，`r3r_partner != nullptr` ⇒ 门禁立刻为假 ⇒ 35543 `TRP ok=1 res=1`，99 订到路并加速（35546 `STUCK … spd=26 prog=30`）。
4. 75 是 `WAIT_COUPLE(17)`，下一轮进 `:13626` 拆锁分支 ⇒ 35534/35541/35560/35563 拆。
5. 锁没了 ⇒ 回到第 1 步。

⇒ **两轮的代码互相打架**：第 306 轮把"启动"绑在"锁存在"上，而 75 的 `WAIT_COUPLE` 使锁永不存活。这就是车库外"车速 0→27→30 却在蹭行/STUCK"的直接原因，也解释了为什么它停不下来：每一 tick 的订路结果取决于"这一 tick 里锁在不在"，而不是几何。

修正 §25 第 1~2 条的表述：不是"扫描提前锁"，而是"**门禁要求锁 → 拆锁把锁拿走 → 门禁再拦**"的互锁；`dist=13` 只是当时的拼接面距离，与锁的建立无关（`R3RIsCoupleTarget` 不检查距离）。

### 26. 第 312 轮修法（玩家 2026-10-26「按你推荐的来改吧」）

玩家拍板：按上轮推荐的三处一起改（只改一处会被另一处抵消）。全部落在 `src\train_cmd.cpp`，未碰任何 `src\*.h`。

#### 26.1 `:13626` 拆锁分支按"持有哪一半"区分

旧代码对**任何**链头只要"当前订单不是 `OT_GOTO_COUPLE`"就 `R3RUnpairCoupleTargets()`：

- 主动侧（`r3r_couple_target` 非空）：确实该拆 —— 订单从 16 变别的，锁过期。
- 被动侧（只有 `r3r_couple_requester`）：它本来就是 `WAIT_COUPLE(17)`，**"我的订单不是 16"是它存在的定义**，与锁是否有效无关。而 `R3RUnpairCoupleTargets()` 会把对方的半把也清掉（`couple_group.cpp:588-594`）⇒ 主动侧刚建的锁每 tick 被拆。这就是 §24/§25 的 13 584 : 13 586。

新逻辑（只改这一处，`:13741` 那条"partner 不再合格就拆"随之只对主动侧可达 —— 被动侧在 `:13626` 已 return，不再走任何 `R3RUnpairCoupleTargets()`）：

1. `r3r_couple_target != Invalid()` ⇒ 老行为，拆双端，记 `CPL-PAIRUNLOCK why=order-changed`。
2. 否则（被动侧）：只有当"命主已消失或不再指向自己"（`R3RGetCouplePairPartner() == nullptr` 而自己的 `r3r_couple_requester` 非空）才清**自己那一半**，记 `CPL-PAIRUNLOCK why=target-dangling`；活锁一字不动。

#### 26.2 被动侧不再调用 `R3RUnpairCoupleTargets()`

由 26.1 直接保证。其余三个调用点复核过，全部只可能由主动侧走到：

- `:13741`（partner 不再合格）：被动侧已在 `:13626` return，不可达。
- `:14675`（`COUPLE-DEST-EMPTY` 后在原地等）/ `:14806`（`COUPLE-SKIP-COMMIT-FAIL`）：都在 `TrainCoupleHandler()` 内，其入参恒为 `OT_GOTO_COUPLE` 的主动方。
- `:10213/10214`（`Couple()` 成功后清锁）、`:12566/12567`（挂接提交路径）：正常交接，保留。

#### 26.3 `parked-no-pair` 的判据从"锁存在"改成"存在合法候选"

`src\train_cmd.cpp:17495`（第 306 轮玩家硬要求「配对存在前不得启动」）只问"此刻有没有锁"。新增文件内 `static bool R3RHasCoupleTargetCandidate(const Train *coupler)`，接受集与 `R3REnsureCouplePair()` 的扫描一致（不同链 / 是链头 / `R3RIsCoupleTarget` / 未 `Stopped` / `R3RCoupleTargetAtOrderStation` / `R3RCoupleAllowedIgnoringPair` / **未被别人锁定**），只在"锁为空"时才走一遍池子。

判据改为「无锁 **且** 无合法候选」才拦。理由：门的语义是"没有可挂的目标就别开走"，而不是"这一刻锁对象必须非空"；锁是由扫描每 tick 重建的派生物（且 `R3R_PAIR_RESCAN_TICKS=8` 有退避期），拿派生物当启动开关会让车每 8 tick 被钉一次。

#### 26.4 预期日志形态变化（复测判据）

1. `CPL-PAIRUNLOCK` 总量与 `CPL-PAIRLOCK` 解耦：稳态下一对车应只有 1 次 `CPL-PAIRLOCK`，直到真的解挂/换目标才出现 `CPL-PAIRUNLOCK`。
2. `why=order-changed` 不再出现在 `actOrd=17` 的行上（旧日志里 13 586 条几乎全是这种）。
3. `why=target-dangling` 只在主动方消失后出现（低频）。
4. `RESV-NOBOOK reason=parked-no-pair` 与 `TRP ok=0 res=0` 的抖动消失；车库外机车不再出现 `spd=0 → 27 → 30` 的蹭行 + `STUCK`。
5. 负判据：确实没有任何合法候选时（等待车底还没到 `WAIT_COUPLE`），`parked-no-pair` 仍须拦住，车仍须停在等待区。

### 26.5 构建自证（2026-10-26 14:44）

复用 `_tmp_inc_build.cmd`（未新建任何 `.cmd`）：

- `build\R3R_inc_guard.log` = `GUARD: incremental is safe`；
- `build\R3R_incbuild.log`：`[2/3] Building CXX object CMakeFiles\openttd_lib.dir\src\train_cmd.cpp.obj` → `[3/3] Linking CXX executable openttd.exe`；
- `build\R3R_incbuild.done` = `EXIT_CODE=0`（14:44:34）；
- `src\train_cmd.cpp` 14:42:18 → `train_cmd.cpp.obj` 14:42:58（14 483 402 B）→ `build\openttd.exe` 14:44:18（52 253 696 B）；
- `read_lints` 0 条；
- 只改 `src\train_cmd.cpp` 一个文件，未碰 `src\*.h`、未碰 `lang\*.txt` ⇒ 增量合规。

## 27. 第 312 轮实机复测取证（2026-10-26 14:49，玩家报「锁抖动应该不存在了」）

**采样**：`build\R3R_debug.log` 620 002 B / 5 822 行 / 14:49:23，产生者 `build\openttd.exe` 14:44:18（本轮修法版）⇒ 采样有效。

### 27.1 锁抖动：已消失（§26.4 判据 1、2 通过）

全日志只有 **9 条**锁事件（旧日志同口径 13 584 / 13 586）：

| 行 | tick | 事件 | act | tgt | 位置 | 存活 |
|---|---|---|---|---|---|---|
| :1693 | 2338 | `CPL-PAIRLOCK why=scan` | 21 | 24 | 1889,188 | → :2289 `why=coupled` tick=2479，**141 tick** |
| :1745 | 2338 | `CPL-PAIRLOCK why=scan` | 99 | 75 | 1889,188 | → :1785 `why=coupled` tick=2401，**63 tick** |
| :1770 | 2346 | `CPL-PAIRLOCK why=scan` | 51 | 123 | 1889,188 | → :2036 `why=coupled` tick=2449，**103 tick** |
| :3265 | 2701 | `CPL-PAIRUNLOCK why=decouple` | 99 | 75 | 267,237 / 273,237 | 解挂正常清锁 |
| :3661 | 2752 | `CPL-PAIRUNLOCK why=decouple` | 51 | 123 | 267,237 / 273,237 | 同上 |
| :4052 | 2803 | `CPL-PAIRUNLOCK why=decouple` | 21 | 24 | 267,237 / 273,237 | 同上 |
| :5380 | 2922 | `CPL-PAIRLOCK why=scan` | 99 | 75 | 255,233 → 265,234 | 尾部 tick=2953 仍 `state=steady best=75`，**未拆** |

- **`why=order-changed` = 0 条**、**`why=target-dangling` = 0 条**（旧日志前者 13 586 条，几乎全在 `actOrd=17` 上）⇒ §26.1 的按侧区分完全按预期生效，被动侧一次都没再拆活锁。
- 三对锁全部活到 `why=coupled`（挂接成功才清），建/拆比从 1:1 变成 **4 建 / 5 拆 且每条都有正当理由**。
- `COUPLE-OK` = 3 条，与三对锁一一对应。

### 27.2 「库外第一次建锁＝场景第四次建锁」：玩家判断成立

前三次建锁全在**同一格** `1889,188`（三对车同一 tick 前后批量扫到，`actSpd=0`，即库内/同一驻车点），第四次才是 `:5380 tick=2922 act=99 tile=255,233 actSpd=5 → tgt=75 tile=265,234`——**在线路上、车速非零**，即玩家说的"库外第一次建锁"。它之后 31 tick 仍 `state=steady`，没有再被拆。

### 27.3 启动门（`parked-no-pair`）：已不再抖，且拦的是真·无候选

- `TRP ok=0 res=0` = **8**、`ok=1 res=1` = **48**（旧日志是逐 tick 交替）⇒ 抖动消失。
- `RESV-NOBOOK reason=parked-no-pair` = **15 条**，全部是 `veh=99`（14 条，行 4781~5294，`tile=253,233`）与 `veh=51`（1 条，行 5576，`tile=249,233`），**全部发生在第四次建锁（:5380）之前**。
- 形态解读：99 从 249,233 开到 253,233 后停下、被门拦了 14 次（每次间隔约 22 行日志），直到 tick=2922 建锁成功才被放行（尾部 266,233 `actSpd=30`）。⇒ 这 14 次里 `R3RHasCoupleTargetCandidate(99)` 返回 false，即**当时 75 确实还不构成合法候选**（尚未进入 `WAIT_COUPLE` / 不在订单站台），门按 §26.3 的设计拦对了，属 §26.4 判据 5 的**负判据通过**。
- 待查（疑点 D-312-1）：这 14 次是否**都**为真无候选，还是其中一部分是「75 已被 51 锁定 ⇒ 我的候选集里排除 ⇒ 判无候选」造成的伪拦截（`R3RHasCoupleTargetCandidate` 里刻意要求 `R3RGetCouplePairPartner(t) == nullptr`，属保守选择）。尾部 `:…` `act=51 state=none … s_lock=1 near=75 nearDist=16 nearWhy=locked` 说明 51 视角下 75 被锁，需要看 99 在同一时段的 `CPL-PAIRSCAN` 才能定论。

### 27.4 尾部现场（tick=2951~2953），锁语义自洽

- `act=99 state=steady best=75 bestDist=2 paired=-1` ⇒ 99 持锁、目标 75 距离 2，`paired=-1` 表示本 tick 未新配（沿用既有锁，符合 steady 分支）。
- `act=51 state=none … pool=5 s_chain=138 s_target=4 s_lock=1 … near=75 nearWhy=locked` ⇒ 51 想挂但 75 已被 99 锁住，进入 `state=backoff`（`pool=0`），`actSpd=5` 仍在缓行。**这正是锁应起的作用：不再互相撕，改为排队等。**
- `veh=75..98`（24 节等待车底）在 265,234~270,234 上，`RESERVECONSIST ok=0 resNow=1` ⇒ 自保预留已存在（`ok=0` 是"已预留"而非失败），不是新问题。
- `veh=21` 已解挂并跑起来（205,412 `spd=293` / 205,420 `spd=339`，`real=6`），`veh=123` 同样在跑（`CRT order=1 found=1`）。

### 27.5 门禁那 14 次拦截的性质：真·无候选（判据 5 通过）

`:4775 tick=2890`（第一次 `parked-no-pair` 之后不远处）：

```
CPL-PAIRSCAN tick=2890 act=99 state=none ord=16 destStn=12 actTile=249,233 actSpd=0
  pool=5 s_chain=138 s_target=5 s_stop=0 s_dest=0 s_lock=0 s_ign=0 s_ignd=0
  best=-1 bestDist=0 paired=0 wait=8 near=75 nearDist=90 nearWhy=not-target nearTile=161,235 nearOrd=1
```

- `state=none` ⇒ 本 tick **真的扫了**（不是退避窗口），`s_target=5` 有 5 辆通过第一层，但 **`best=-1`** ⇒ 后续过滤全否，**池子里确实没有合法目标**。门按 §26.3 拦得对。
- 对照 `:5379 tick=2922`（建锁前一行）：`state=locked … best=75 bestDist=11 paired=1` ⇒ 这一 tick 扫到了，紧接 `:5380` 落锁。⇒ 门放开与建锁是**同一 tick 的因果**，不是随机抖动。
- 因此 §26.4 判据 5（负判据）**通过**：无候选时仍拦、有候选时不拦。

**疑点 D-312-1 → 已排除**：14 次拦截不是「75 被别人锁住导致我判无候选」的伪拦截，而是 `best=-1`（真无候选）。尾部 51 那条 `nearWhy=locked` 是另一时刻、另一辆车。

**疑点 D-312-2（新，未深挖，只登记）**：`:4775` 的 `near=75 nearTile=161,235 nearOrd=1 nearDist=90` 与已知事实矛盾——同一辆 `veh=75`（24 节车底，`tgtLen=96`）在建锁时位于 `265,234` 且 `tgtOrd=17`，32 tick 内不可能跑到 161,235 并变成订单 1。可能原因：①`near` 的诊断缓存跨 tick 失效（指针/位置过期）；②`near` 挑的是"最近嫌疑车"而非候选，其 `nearTile`/`nearOrd` 读数与 `near` 车号不同源。判定：**不影响本轮结论**（`best=-1` 与 15 次拦截的因果已由 `state=none` 独立证实），但若以后要用 `near*` 做诊断需先修这个口径。

> **2026-10-26 追加订正（§28.5）**：`s_target` 的语义读反了——它不是"通过第一层的数量"，而是**被 `R3RIsCoupleTarget()` 拒掉的数量**（`train_cmd.cpp:13803`）。故 `:4775` 的 `pool=5 s_target=5` 意为"5 条全部不是目标"，`best=-1` 与计数**完全自洽**，不是"5 条通过后被后续判据全否"。§27.5 第 1 条的措辞据此更正，结论（真·无候选）不变、且更强。

## 28. 第四次建锁（tick=2922，库外首次）合法性审查

**玩家提问（2026-10-26）**：能检查一下第四次建锁的场景看看它是否合法吗。

### 28.1 建锁现场原文（逐字）

```
:5379 CPL-PAIRSCAN tick=2922 act=99 state=locked ord=16 destStn=12 actTile=255,233 actSpd=5
      pool=5 s_chain=138 s_target=4 s_stop=0 s_dest=0 s_lock=0 s_ign=0 s_ignd=0
      best=75 bestDist=11 paired=1 wait=0 near=51 nearDist=98 nearWhy=not-target nearTile=205,281 nearOrd=1 nearStop=0
:5380 CPL-PAIRLOCK tick=2922 why=scan
      act=99 actHead=99 actTile=255,233 actSpd=5 actOrd=16 actStopd=0 actFront=1 actLen=96 actPairT=75 actPairR=1048575
      tgt=75 tgtHead=75 tgtTile=265,234 tgtSpd=0 tgtOrd=17 tgtStopd=0 tgtFront=1 tgtLen=96 tgtPairT=1048575 tgtPairR=99
:5381 CPL-PAIR tick=2922 act=99 tgt=75 dist=11 actTile=255,233 tgtTile=265,234
:5382 CPL-PAIRSCAN tick=2922 act=99 state=steady ord=16 destStn=12 actTile=255,233 actSpd=5
      pool=0 s_chain=0 s_target=0 ... best=75 bestDist=11 paired=-1
```

### 28.2 判据逐条核对（`R3RIsCoupleTarget()` 现为 5 道，`train_cmd.cpp:2483-2532`）

| # | 判据（代码） | 现场读数 | 结论 |
|---|---|---|---|
| 1 | `t->IsSegmentFront()` | `tgtFront=1`（`IsFrontEngine`）+ `tgtHead=75`（`First()==t`） | 段头/链头 ✅ |
| 2 | `Train::From(t->First()) != t → false` | `tgtHead=75`、`tgt=75` | 是独立链头，不是被吞掉的段 ✅ |
| 3 | `R3RChainHasRealArticPart(t)` 为假 | `tgtLen=96`（24 节 × 4px 假铰接），`ORD-IDX-WATCH … artic=0` | 无真 artic ✅ |
| 4 | `t->cur_speed != 0 → false`（2026-10-26 新判据「必须停稳」） | `tgtSpd=0` | 已停稳 ✅ |
| 5 | `IsType(OT_WAIT_COUPLE)` | `tgtOrd=17`（`OT_WAIT_COUPLE`） | ✅ |

扫描层其余 4 道（`:13803-13820`）：`s_stop=0`（未被玩家 Stop）、`s_dest=0`（目的地站判据全过）、`s_lock=0`（无候选被别人的锁排除）、`s_ign=0`/`s_ignd=0`（`R3RCoupleAllowedIgnoringPair` 全过，日志另有 `:5342 ignOK=1`）⇒ **一道都没排除**。

**计数恒等式**：`pool=5`、`s_target=4`（4 条被"不是目标"拒掉）、其余排除计数全 0 ⇒ 通过者恰好 **5−4=1** 条，就是 `best=75`。不存在"多个候选里随便挑一个"的可能。

### 28.3 订单语义一致性（最关键的一条）

- 主动方 99：`actOrd=16`（`OT_GOTO_COUPLE`）、`destStn=12`；`:5342 CPL-WHY aStn=21 tStn=12 destStn=12 ignOK=1` ⇒ **目标车所在站 == 订单目的站**。
- 被动方 75：`tgtOrd=17`、`:5332 TRP veh=75 mf=270,234 real=11 type=17 dest=4294967295`（`WAIT_COUPLE` 无目的地，正确）；`:5333 RESV-NOBOOK veh=75 … reason=parked-waiter` ⇒ 它已被新门禁认定"停稳的等待者"。
- `dist=11` = Manhattan(255,233 → 265,234) = 10+1 ✅；两车 `cached_total_length` 均 96，75 后端 265,234、前端 270,234（6 格 × 16px = 96px）几何自洽。

### 28.4 时序合法性：不是"刚切订单就锁"，而是停稳约 33 tick 后

| tick（约） | 行 | 事件 |
|---|---|---|
| ~2889 | `:5271` | `SPD-DROP veh=75 tile=265,234 prev=5 … order=3 real=10 dt=1` ⇒ 75 在 265,234 **从 5 归零** |
| 2890 | `:4775` | 99 扫描：`pool=5 s_target=5 best=-1`，`near=75 nearOrd=1` ⇒ 75 此刻订单仍是 type=1，**不是**合法目标 |
| ~2915 | `:5330/:5332` | `ORD-IDX-WATCH veh=75 real=10->11`、`TRP veh=75 real=11 type=17 spd=0` ⇒ 订单推进到 `WAIT_COUPLE` 且仍在停 |
| 2922 | `:5380` | 建锁 99↔75 |

⇒ 停稳（2889）→ 切 WAIT_COUPLE（~2915）→ 被锁（2922），**间隔约 33 tick**，正好是"停稳之后才允许配对"的预期形态，而非旧 bug 里"切订单瞬间就锁"。

### 28.5 双方锁位干净、无抢占

- `tgtPairT=1048575`（`Invalid`）⇒ 75 此前**没有被任何人锁**；`actPairR=1048575` ⇒ 99 原本没有 requester ⇒ 这是**全新锁**，不是覆盖既有锁、也不是抢别人已锁的目标。
- 反向验证：稍后 `act=51 state=none … s_lock=1 near=75 nearWhy=locked` ⇒ 51 也想锁 75 但被拒并转 `backoff`，说明"先到先得"生效且未互相撕（正是本轮修复目标）。

### 28.6 这不是"陌生人配对"：同一对车第二次会合

`:1798 COUPLE-OK loco=99 rear=98 consist=75` ⇒ **tick=2401 时 99 已把 75 这条 24 节车底（75..98）挂上过一次**（库内 `1889,188` 那次锁），`tick=2701` 在 `:3265 CPL-PAIRUNLOCK why=decouple` 解挂，随后 75 按自己的 17 条排程跑 `real=6→11`（`:3359-4866`，42,54 库 → 沿 y=235 东行 → 265,234），最后停稳进 `WAIT_COUPLE`；99 则持 `GOTO_COUPLE destStn=12` 从 249,233 追到 255,233 再挂。

⇒ **场景语义完全合法**：解挂 → 各自执行排程 → 在订单指定站台重新会合挂接。玩家的"库外第一次建锁"就是这次会合。

### 28.7 结论

**第四次建锁（tick=2922，`act=99 → tgt=75`）合法，且是本轮修复后应有的形态。** 5 道目标判据 + 4 道扫描判据全部逐条命中、计数上唯一、订单目的站一致、双方锁位干净、被动方已停稳约 33 tick、主动方是同一台车按订单赴约。无一处"凑合"或"误锁"。

### 28.8 遗留疑点（不否定上述结论）

**D-312-2 升级为系统性诊断缺陷**：现在有两个独立样本证明 `nearTile` 与候选车实际 `tile` 不同源——
- `:5379 near=51 nearTile=205,281`，而 `:5375 CRT veh=51 origin=241,233`（51 在 241,233）；
- `:4775 near=75 nearTile=161,235`，而 75 当时在 265,234。

`nearDist` 与 `nearTile` 算术自洽（`nearDist == Manhattan(actTile, nearTile)`），故错的是**取样对象**，不是距离公式。不影响建锁判定（`best`/`tgt*` 字段来自另一条路径且逐项自洽）。以后若要拿 `near*` 判"谁被谁挡"，必须先修口径。

## 29. 新增探针：建锁瞬间双方「当前调度计划」dump（第 313 轮，纯只读）

**玩家要求（2026-10-26）**：「请问你能否加探针读取建锁的时候双方的当前调度计划」。

### 29.1 为什么要这个探针

第 312 轮已经把锁抖动修掉，`CPL-PAIRLOCK` 只剩 4 条，合法性也可以靠 `actOrd/tgtOrd/tgtTile/aStn/tStn/destStn` 逐条对上（§28）。但现有 `CPL-PAIRLOCK` 行**只有一个 `Ord=<current_order 类型>` 数字**，回答不了下面这类问题：

1. 主动方这条 `GOTO_COUPLE` 在它的表里是**第几条**？后面还有没有别的命令（会不会挂上就立刻跳走）？
2. 被动方 `WAIT_COUPLE` 是第几条？它在等的下一条命令是什么（挂上之后车底要去哪）？
3. 双方此刻跑的是**谁的表**？R3R 有借用（`r3r_follow_orders`/`r3r_orders_borrowed`）与原生共享环（`IsOrderListShared`/`FirstShared`），同一辆车"显示的表"和"真正在跑的表"可以不同——这正是历史上多次排程错乱的现场。
4. `orders_backup` 是否已存在（解挂还原点是否占着），`real` 索引是否与 `current_order` 对得上。

⇒ 需要一行摘要 + 逐条订单的 dump，且必须在建锁那一 tick 取。

### 29.2 设计

- 新函数 `R3RDbgPairPlans(const char *what, const Train *a, const Train *b)`（`src\train_cmd.cpp`，紧跟 `R3RDbgPairEvent` 之后）。
- 调用点：**只有** `:13884 R3RDbgPairEvent("CPL-PAIRLOCK", "scan", coupler, best);` 后面一行。不加进 `R3RDbgPairEvent` 本体，理由是 `CPL-PAIRUNLOCK`（含 `decouple`/`coupled`/`order-changed`/`target-dangling`）也走那个函数，逐条订单会让低频事件变噪音；先只钉建锁点，需要时再挂解锁点。
- 每 side 一行摘要 + 逐条订单行：

```
[R3R] CPL-PLAN tick=2922 side=act veh=99 head=99 caronly=0 n=9 real=3 impl=3 tt=3 co=16 dest=491798
      borrowed=0 follow=0 followOwn=-1 ownShared=0 firstShared=-1 bak=0 bakReal=65535
  CPL-PLAN actORD idx=0 type=16 dest=491798
  ...
  CPL-PLAN actORD capped=1 shown=40 of=57   (仅超长表)
```

- 字段口径（写进函数头注释，避免下一轮再猜）：
  - `n/real/impl/tt` = `GetNumOrders()` 与三个游标；`n` 在借用时来自**借来的表**（`GetEffectiveOrders()`）。
  - `co/dest` = `current_order.GetType()` 与 `current_order.GetDestination().value`（16=GOTO_COUPLE、17=WAIT_COUPLE、15=DECOUPLE，同 §28.3）。
  - `borrowed/follow/followOwn` = `r3r_orders_borrowed`、`r3r_follow_orders != nullptr`、`r3r_follow_owner`（`-1`=无）。
  - `ownShared/firstShared` = `IsOrderListShared()` 与 `FirstShared()->index`（`-1`=无）——判断是否踩在原生共享环上（KI-470 家族）。
  - `bak/bakReal` = `orders_backup != nullptr` 与 `orders_backup_real_index`（解挂还原点）。
  - 逐条订单：`type/dest`，与 `ORD-AFTER-COUPLE` 的 `ORD idx=/type=/dest=` 口径逐字一致（同一读法，便于交叉比对）。
  - `R3R_PLAN_DUMP_MAX = 40` 条上限 + `capped=1 shown=/of=` 行，防 100+ 条计划刷屏。
- 开销：只在 `CPL-PAIRLOCK`（低频事件，第 312 轮日志 4 次）触发；`R3RFopenDbg` 与既有 dump 一致；`R3RDbgOn()` 关闭探针时由既有开关统一控制。

### 29.3 判据（本轮复测）

1. 出现 `CPL-PLAN tick= … side=act` 与 `side=tgt` 成对，且 `tick` 与同一条 `CPL-PAIRLOCK` 相同。
2. `act` 侧 `co=16` 且逐条订单里 `idx=real` 那条 `type` 也是 16（索引与当前订单对得上）；`tgt` 侧 `co=17` 同理。
3. 92 号现场（§28）应能读出：`act` 表里 `GOTO_COUPLE dest=站12` 前面还有若干条已走过、后面还有命令；`tgt` 表里 `WAIT_COUPLE` 之后紧跟车底自己的下一条命令。
4. 借用/共享场景（`borrowed=1` 或 `ownShared=1`）时 `followOwn`/`firstShared` 必须非 `-1`，与 `ORD-XFER`/`ORD-AFTER-COUPLE` 的 `borrowed=/owner=/own_first=` 一致。
5. 负判据：不出现 `CPL-PLAN` 行时（无建锁），日志体积与第 312 轮相当（不许因为探针让日志翻倍）。

### 29.4 构建自证（2026-10-26）

复用既有 `_tmp_inc_build.cmd`（未新建任何 `.cmd`）：

- `GUARD: incremental is safe`；日志 `[2/3] Building CXX object CMakeFiles\openttd_lib.dir\src\train_cmd.cpp.obj` → `[3/3] Linking CXX executable openttd.exe`；
- `build\R3R_incbuild.done` = `EXIT_CODE=0`（`LastWriteTime` 2026-10-26 15:24:16）；
- `src\train_cmd.cpp` 15:19:48（1 222 613 B）→ `build\CMakeFiles\openttd_lib.dir\src\train_cmd.cpp.obj` 15:21:44（14 487 265 B）→ `build\openttd.exe` 15:24:13（52 253 696 B）；
- `read_lints`：`src\train_cmd.cpp` 0 条；
- 产物字面量自证：`findstr /C:"CPL-PLAN" /C:"followOwn=" build\openttd.exe` ⇒ **FOUND**（两个新串都在 exe 内）。

状态＝**已实现 + 已编译，游戏内复测待做**（按 §29.3 五条判据；探针纯只读，零行为变更）。

## 30. 第 314 轮：玩家主张「场景第 4 次配对的第一次非法」，但日志里 0 次建锁（2026-10-26）

**玩家原话（2026-10-26）**：「请看日志吧，我认为场景第四次（这里是第一次）配对是非法的，这回希望能够给你证据」。

### 30.1 交付物与身份核对（先确认日志不是旧的）

| 项 | 读数 | 判读 |
|---|---|---|
| `build\R3R_debug.log` | 112 116 B / **948 行** / `LastWriteTime` 2026-10-26 **15:29:07** | 是本轮新抓的 |
| 产生者 `build\openttd.exe` | 15:24:13（52 253 696 B），内含 `CPL-PLAN`/`followOwn=` | **是第 313 轮含新探针的 exe** ⇒ 「无 `CPL-PLAN`」不能归因于旧 exe |
| 首行 `:1 SEGLOAD-PARK veh=24 …` | 读档期段行复原 | 日志起点＝**读档瞬间** |
| `:355 [R3R] CPL-PAIRMAP tick=2832 locks=0` | 锁表快照：`locks=0` | 读档后第一 tick 无任何锁 |
| 末行 `:948 RESV-WATCH CLEAR-BRIDGE tile=223,235 track=0 phase=controller actor=98 head=75`；最后 tick 戳 `:947 tick=2905` | 覆盖窗口 **tick 2832 → 2905** | 共 73 tick |

### 30.2 标签计数（findstr，逐条可复算）

```
find /C "COUPLE"            build\R3R_debug.log  → 0
find /C "CPL-"              build\R3R_debug.log  → 22
find /C "PAIRMAP"           build\R3R_debug.log  → 1
findstr /C:"CPL-PAIRLOCK" /C:"CPL-PAIRUNLOCK" /C:"CPL-PLAN"  → 0 条（无输出）
```

- `CPL-PAIRLOCK` = **0**、`CPL-PAIRUNLOCK` = **0**、`CPL-PLAN` = **0**、`COUPLE-OK/FAIL` = 0、`DECOUPLE` = 0。
- 22 条 `CPL-*` 的构成：`CPL-PAIRMAP` 1 条 + `CPL-PAIRSCAN` 15 条 + 99 自己的 `CPL-ENTRY`/`CPL-ORIGIN`/`CPL-WAITCUT`/`CPL-PATHFOUND`/`CPL-SCAN`/`CPL-SKIP` 6 条。

⇒ **这份日志里从头到尾没有发生过任何一次配对（建锁）**，`CPL-PLAN` 自然一次都没触发。判据 1~3 不是失败，而是**没有样本**。

### 30.3 时间轴对齐：日志差 17 tick 就够到 §28 那次建锁

- §28 取证的第 4 次建锁在同一存档的 **tick=2922**（`:5379/:5380 act=99 → tgt=75`，第 312 轮日志）。
- 本次日志止于 **tick=2905** ⇒ 距建锁还差 **17 tick**，恰好断在事件之前。
- 同一时间轴的旁证（证明两次日志是同一次读档、同一 tick 基数）：

| tick | 本次日志锚点 | 与 §28.4 表格对照 |
|---|---|---|
| 2896 | `:855 … pool=5 s_chain=138 s_target=5 best=-1 paired=0 wait=8 near=75 nearDist=84 nearWhy=not-target nearTile=167,235 nearOrd=1 nearStop=0` | §28.4 记的 2890「`pool=5 s_target=5 best=-1`、`near=75 nearOrd=1`」是同形态的延续：75 仍在跑 `type=1`、`real=10` |
| 2904 | `:942 … near=75 nearDist=35 nearTile=217,235 nearOrd=1` | 75 沿 y=235 东行（`:878-:933` 一串 `RESV-SIGPASS veh=75 tile=…,235` / `CRT veh=75 origin=…,235`），**仍未切 `WAIT_COUPLE`**（§28.4 记它 ~2915 才切） |

⇒ 这段时间里 99 想挂 75 被判 `not-target` 是**正确**的（75 还没到等待点），不是非法配对。

### 30.4 99 侧自身状态（为什么它也没订路）

`:860 TRP veh=99 real=8 type=16 dest=491798 spd=0 fp=0 look=1 one=0 mstuck=1 fto=1 => ok=0 res=0`；
`:861 RESV-NOBOOK veh=99 tile=249,233 order=16 caronly=0 reason=parked-no-pair`；
`:873-:877 CPL-ENTRY/CPL-ORIGIN/CPL-WAITCUT/CPL-PATHFOUND found=0`、`:887 CPL-SKIP veh=99 … retryIn=7`。

⇒ 第 312 轮 §26.3 改过的门（`parked-no-pair`＝「存在合法候选」而非「锁存在」）工作正常：此刻 75 尚不合法 ⇒ 拒绝启动，与设计一致。

### 30.5 结论与定性

1. **无法验证玩家的主张**：日志中没有「第 4 次配对（或任何配对）」，`CPL-PAIRLOCK`/`CPL-PLAN` 双双 0 条。
2. 覆盖窗口内唯一一度接近的候选对 99↔75，被 `not-target` 拒是**正确行为**（75 `real=10/order=1`）。
3. 新登记 **REG-314-A（低，采样缺口）**：玩家读档后仅 73 tick 就抓日志（112 116 B），比第 312 轮的 620 002 B / 5 822 行短一个数量级，导致关键事件不在窗口内。
4. 新登记 **疑点 U-314-A（待验证）**：玩家看到/认为的「非法配对」发生在何时何处？若确有一次挂接发生（界面上两车已连上），那它必然在 tick ≥ 2922，本次日志没有覆盖；若玩家指的是 99 追不上 75，则属「等待者未到等待点」，不是配对判定错误。

### 30.6 请玩家做的（二选一，前者更省事）

- **A（推荐）**：重新抓日志，要求覆盖到建锁之后——读档后**继续跑到 99 真的挂上 75**（约 tick 2922 起）再多跑几秒，然后才保存/复制 `build\R3R_debug.log`。自查三行：
  `findstr /C:"CPL-PAIRLOCK" build\R3R_debug.log` 非空、`findstr /C:"CPL-PLAN tick=" build\R3R_debug.log` 成对出现、`findstr /C:"CPL-PAIR tick=" ` 有 `act=99 tgt=75`。
- **B**：直接告知非法配对的 **tick** 与**双方车号**（例如「tick=3xxx，99 配上了 51/75 之类」），我按 tick 去日志定位。

## 31. 第 315 轮：新日志抓到唯一一次建锁（tick=2928，99↔75），`CPL-PLAN` 首次落地（2026-10-26）

**玩家原话**：「我又跑了一会儿日志，希望这次能够看到非法配对」。

### 31.1 日志身份

| 项 | 读数 |
|---|---|
| `build\R3R_debug.log` | **184 927 B** / `LastWriteTime` 2026-10-26 **15:40**（上一份 112 116 B / 15:29:07） |
| 覆盖窗口 | 含 `CPL-PAIRSCAN tick=2927/2928`，建锁行在 `:1549`，其后仍有内容 |

### 31.2 标签计数（find /C，可复算）

```
CPL-PAIRLOCK   → 1      ← 全日志仅此一次建锁，第 313 轮加的探针首次落地
CPL-PLAN       → 2      ← 摘要行；逐条订单行前缀是 "  actORD"/"  tgtORD"（不带 CPL-PLAN，故计数只 2）
CPL-PAIRUNLOCK → 0      ← 锁建立后直到日志结束都没解除
CPL-PAIRSCAN   → 50
COUPLE         → 10     ← 但 COUPLE-OK = 0 条，全部是 COUPLE-FAIL
```

`COUPLE-FAIL` 共 5 条，均在建锁前：`:997 :1027 :1127 :1229 :1419`，内容逐字相同
`COUPLE-FAIL loco=99 order=16 tx=253 ty=233 x=4050 y=3736` ⇒ 99 一直找不到目标，直到 2928 才锁上。

### 31.3 建锁原文（`:1549`，逐字）

```
[R3R] CPL-PAIRLOCK tick=2928 why=scan act=99 actHead=99 actTile=255,233 actSpd=5 actOrd=16 actStopd=0 actFront=1 actLen=96 actPairT=75 actPairR=1048575
                                                          tgt=75 tgtHead=75 tgtTile=265,234 tgtSpd=0 tgtOrd=17 tgtStopd=0 tgtFront=1 tgtLen=96 tgtPairT=1048575 tgtPairR=99
```

- 主动方 99：`actOrd=16`（OT_GOTO_COUPLE）、`actSpd=5`（还在滑行）、`actLen=96`（12 节×8px）。
- 被动方 75：`tgtOrd=17`（OT_WAIT_COUPLE）、`tgtSpd=0`（停稳）、`tgtLen=96`。
- 双向登记成立：`actPairT=75` 与 `tgtPairR=99` 互指，且各侧反向槽仍为 `1048575`(0xFFFFF=Invalid)。
- 同一 tick 的扫描摘要 `:1548`：`state=locked … pool=5 s_chain=138 s_target=4 s_stop=0 s_dest=0 s_lock=0 s_ign=0 s_ignd=0 best=75 bestDist=11 paired=1 wait=0 near=51 nearDist=149 nearWhy=not-target nearTile=205,332`。
  上一 tick `:1544`：`tick=2927 act=99 state=backoff … pool=0 s_chain=0 … best=-1 paired=0 wait=1`。
  ⇒ **tick 2927→2928 之间候选池从 0 变 5，`s_target=4` 通过目标过滤，最终 `best=75`**。

### 31.4 双方调度计划（第 313 轮新探针的正式产出）

**主动方 99**（`:1550`）：`n=9 real=8 impl=8 tt=8 co=16 dest=12 borrowed=0 follow=0 followOwn=-1 ownShared=1 firstShared=21 bak=0 bakReal=-1`

| idx | type | 含义 | dest |
|---|---|---|---|
| 0 | 16 | GOTO_COUPLE | **8** |
| 1 | 1 | GOTO_STATION | 13 |
| 2 | 1 | GOTO_STATION | 14 |
| 3 | 6 | GOTO_WAYPOINT | 17 |
| 4 | 2 | 修理/服务 | — |
| 5 | 1 | GOTO_STATION | 14 |
| 6 | 1 | GOTO_STATION | 13 |
| 7 | 1 | GOTO_STATION | 21 |
| **8** | **16** | **GOTO_COUPLE（正在执行）** | **12** |

**被动方 75**（`:1560`）：`n=17 real=11 impl=11 tt=11 co=17 dest=0 borrowed=0 follow=0 followOwn=-1 ownShared=1 firstShared=24 bak=0 bakReal=-1`

| idx | type | 含义 | dest |
|---|---|---|---|
| 0 | 17 | WAIT_COUPLE | — |
| 1 | 1 | GOTO_STATION | 6 |
| 2 | 1 | GOTO_STATION | 2 |
| 3 | 1 | GOTO_STATION | 8 |
| 4 | 1 | GOTO_STATION | 12 |
| 5 | **15** | **DECOUPLE** | — |
| 6 | 1 | GOTO_STATION | 19 |
| 7 | 6 | GOTO_WAYPOINT | 20 |
| 8 | 2 | 修理/服务 | — |
| 9 | 1 | GOTO_STATION | 19 |
| 10 | 1 | GOTO_STATION | 12 |
| **11** | **17** | **WAIT_COUPLE（正在执行）** | — |
| 12 | 1 | GOTO_STATION | 8 |
| 13 | 1 | GOTO_STATION | 2 |
| 14 | 1 | GOTO_STATION | 6 |
| 15 | 2 | 修理/服务 | 8 |
| 16 | **15** | **DECOUPLE** | — |

### 31.5 从计划能读出的判定

1. **两侧都是"两轮交接"结构**：99 表里 2 条 GOTO_COUPLE（idx0 dest=8、idx8 dest=12）；75 表里 2 条 WAIT_COUPLE（idx0、idx11）+ 2 条 DECOUPLE（idx5、idx16）。当前恰好都处在**第二轮**（99 第二次去挂、75 第二个等待点）。
2. **站点维度自洽**：75 停在 `265,234`，它 idx10 是 GOTO_STATION dest=12、idx11 是 WAIT_COUPLE、idx12 去站 8 ⇒ 75 刚刚在**站 12** 停稳进入等待点；99 的 idx8 正是 GOTO_COUPLE **dest=12**。⇒ 双方都指向站 12，配对在"站/订单语义"层面是匹配的。
3. **但 75 的等待点位于两条 DECOUPLE 之间**：idx5 已解挂过一次，idx11 是第二次等挂 ⇒ 若玩家口径是"第二轮应该等**另一台**机车（或该车底本轮不该被挂）"，那么这次就是错误对象；反之若"两轮都由 99 挂"，则配对正确。
4. **尚未真正挂上**：`COUPLE-OK` 全日志 0 条，`CPL-PAIRUNLOCK` 0 条，锁 `actPairT/tgtPairR` 互指但距 11px（`actTile=255,233` vs `tgtTile=265,234`），`actSpd=5` 仍在滑行。⇒ 建锁 ≠ 挂接成功，玩家看到的"非法配对"若指界面已连上，需确认是哪一刻。

### 31.6 结论与待玩家拍板

- **本次日志已能满足第 314 轮判据 1（出具一份"非法配对"的日志）**：唯一一次建锁的双方计划已逐条 dump 在 `:1550-1577`。
- 判定"非法"只缺**口径**，不缺口径外的数据：
  - 口径 α：75 第二个等待点应由谁挂？若答案不是 99，则 2928 这次就是错配。
  - 口径 β：99 的 idx8 `GOTO_COUPLE dest=12` 是否本就该挂 75（若 99 该去挂的是站 12 的另一列），需要玩家指出"正确对象是几号车"。
- 我方可继续自查的方向（需玩家点头，避免空转）：`s_target=4` 说明通过目标过滤的候选有 4 个，只有 `best=75` 被选中；可加只读探针把 `pool=5` 全部候选（车号/距离/落选原因）逐条 dump，判"为什么不是别的 3 个"。

## 32. 第 316 轮：玩家追问「建锁前 99 是否已在非法滑行」——**成立，实锤**（2026-10-26）

**玩家原话**：「那么，请问你是否发现，配对锁建立之前，99 就已经在非法滑行了」。

### 32.1 结论先行

**成立。** 顺序是 **先滑行、后建锁**，不是「建锁后允许滑行靠近」。证据：tick 2924→2927 连续 4 帧 `pool=0`（候选池空、无任何合法配对目标）且 `state=backoff`，99 仍以 `spd=46 → 51 → 25 → 5` 从 `253,233` 推进到 `255,233`（2 格）；候选池直到 **tick 2928** 才变成 `pool=5 / best=75`，同帧才写出唯一那条 `CPL-PAIRLOCK`（`:1549`）。

### 32.2 关键行原文（全部取自本轮日志，可直接复核）

**滑行帧（建锁前，全为 `pool=0` / `state=backoff`）**

```
:1502  CPL-PAIRSCAN tick=2924 act=99 state=backoff ord=16 destStn=12 actTile=253,233 actSpd=46 pool=0 … s_target=0 … best=-1 bestDist=0 paired=0 wait=4
:1511  CPL-PAIRSCAN tick=2925 act=99 state=backoff ord=16 destStn=12 actTile=254,233 actSpd=51 pool=0 … best=-1 paired=0 wait=3
:1514  CPL-PAIRSCAN tick=2926 act=99 state=backoff ord=16 destStn=12 actTile=254,233 actSpd=25 pool=0 … best=-1 paired=0 wait=2
:1539  CPL-PAIRSCAN tick=2926 act=99 state=backoff …                         actTile=254,233 actSpd=21 pool=0 … paired=0 wait=2
:1544  CPL-PAIRSCAN tick=2927 act=99 state=backoff ord=16 destStn=12 actTile=255,233 actSpd=5  pool=0 … best=-1 paired=0 wait=1
:1548  CPL-PAIRSCAN tick=2928 act=99 state=locked  ord=16 destStn=12 actTile=255,233 actSpd=5  pool=5 s_chain=138 s_target=4 … best=75 bestDist=11 paired=1 wait=0
:1549  CPL-PAIRLOCK tick=2928 why=scan act=99 … actTile=255,233 actSpd=5 actPairT=75
```

⇒ `actTile`/`actSpd` 在 `pool=0` 的 4 帧里一路变化（253→254→255；46→51→25→5），**位移与配对判定无关地发生了**。

**物理移动帧（同一批 tick，逐条对得上）**

```
:1441  LOCO-MOVE veh=99 tile=253,233 j=201 spd=16 track=01 dir=1 db=1 stuck=0 dtile=491798
:1508  LOCO-MOVE veh=99 tile=253,233 j=204 spd=48 track=01 dir=1 db=1 stuck=0 dtile=491798
:1509  LOCO-MOVE veh=99 tile=254,233 j=194 spd=53 track=01 dir=1 db=1 stuck=0 dtile=491798
:1541  LOCO-MOVE veh=99 tile=254,233 j=205 spd=21 track=01 dir=1 db=1 stuck=0 dtile=491798
:1545  LOCO-MOVE veh=99 tile=255,233 j=194 spd=5  track=01 dir=1 db=1 stuck=0 dtile=491798
:1608  LOCO-MOVE veh=99 tile=255,233 j=194 spd=5  track=01 dir=1 db=1 stuck=0 dtile=479497   ← 建锁后 dtile 被改写
```

- **全程 `db=1`（DrivingBackwards，倒车推进）**，`dir=1`，`track=01`（TRACK_X）。
- 峰值 `spd=53`（`:1509`）——这不是"贴靠车底"的低速蠕动，而是**巡航速度**。
- 建锁后 `dtile` 从 `491798` 变 `479497`（`:1545` → `:1608`）⇒ 配对成功反过来改写了运动目标，进一步证明**运动目标来自另一条判据链**。

**唤醒点：从「停住空转」到「开始滑行」的转折**

```
:1008  SPD-DROP veh=99 tile=253,233 prev=30 order=16 real=8 stuck=1 fp=0 prog=6 wait=0 parked=0 stop=0 track=01 dtile=491798 dt=4
:1011…:1430  CPL-S0-CHK veh=99 tile=253,233 real=8 order=16 stuck=1 fp=0 prog=0 wait=0…255 …（256 行，tick 2905→2922）
:1434  TRP veh=99 mf=259,233 real=8 type=16 dest=491798 spd=0 fp=0 look=0 one=0 mstuck=0 fto=0 => ok=0 res=0
:1436  TRP veh=99 mf=259,233 real=8 type=16 dest=491798 spd=0 fp=0 look=1 one=0 mstuck=0 fto=0 => ok=1 res=1
:1441  LOCO-MOVE veh=99 … spd=16 …        ← 唤醒后第一帧运动
:1510  RESV-EXT-PRE veh=99 tile=260,233 enterdir=2 tracks=0x1 reserved=0x1 doRes=0 real=8 spd=55
```

⇒ 99 曾在停驶分支里空转 256 帧（`CPL-S0-CHK wait=0…255`，`stuck=1`），随后 `:1436 TRP ok=1 res=1` 把它**唤醒并预留**，`:1441` 立刻进入移动分支。**唤醒时刻 `pool` 仍为 0。**

### 32.3 定性：「两把尺子」——运动许可与配对许不同源

| 判据 | 依据 | 建锁前读数 |
|---|---|---|
| 运动许可（TRP/预留） | `GOTO_COUPLE` 自带 `dest=491798`（站 12），按「去目的地」寻路 | `:1436 ok=1 res=1` ⇒ **放行** |
| 配对许可（CPL 扫描） | 需要候选车底处于 `OT_WAIT_COUPLE` 且通过过滤 | `:1502/:1511/:1514/:1539/:1544` 全 `pool=0` ⇒ **无目标** |

两条判据各说各话：运动侧认为"我还有目的地要跑"，配对侧认为"现在没人可以挂"。99 于是**带着目的地一路滑过去，滑到目标进入候选池为止**（tick 2928，`bestDist=11`）。这不是「配对判定错」，而是**滑行不受配对状态约束**。

### 32.4 与既有结论的关系（重要，避免重复取证）

- 第 242 轮 KI-348 记的是另一头：`cur_speed==0` 的 GOTO_COUPLE 机车**永远进不了移动分支**（停在 `j<adv_spd` 分支），表现为"原地不动、Stuck=0"。本轮是它的**镜像面**：一旦被 TRP 唤醒（`ok=1 res=1`），就再也不回来，且唤醒条件不看 `pool`。
- 第 310/312 轮的 `RESV-NOBOOK … reason=parked-no-pair` 门（`parked-no-pair`＝「无合法候选」）只拦了 `cur_speed==0` 的停驻车（`:861` 那次拦住了），**拦不住已经带速的车**——`:1441` 之后 `spd>0`，该门不再成立。
- 因此本轮的「非法滑行」与上一轮「非法配对」是同一条根因的两半：**配对状态只影响"能不能建锁"，不影响"能不能跑"**。

### 32.5 待确认口径（决定修法方向）

- **C-1**：GOTO_COUPLE 列车在 `pool=0`（无任何合法候选）时，应该**停车等**（`cur_speed` 归零、不预留），还是允许它**先跑到目的地**（站 12）再等？现状是后者，且跑到距离 11px 时顺手建锁。
- **C-2**：若选「停车等」，停车位置取哪里——当前位置就地刹车，还是先滑到订单目的地（`dest=491798`）再等？
- **C-3**：`db=1` 倒车滑行是否也在本次修复范围内（`:1508/:1509/:1541/:1545/:1608` 全程倒车，建锁后仍 `db=1`）。

### 32.6 复测判据（待修法落地后）

1. `pool=0` 且 `state=backoff` 的帧里，`actSpd` 不再出现 46/51/53 这类巡航值（允许 ≤ 刹车余量）。
2. `actTile` 在 `pool=0` 期间位移不超过 1 格（刹车距离）。
3. `:1436 TRP ok=1 res=1` 这类「无配对目标却预留成功」不再出现，或出现后紧跟 `RESV-NOBOOK reason=parked-no-pair`。
4. 建锁仍能正常发生（`CPL-PAIRLOCK` 不消失），且建锁时 `actSpd` 接近 0。

## 33. 第 317 轮：`pool=0` 时本不该滑行——门**已经存在**，但是被自己放宽漏掉的（2026-10-26）

**玩家原话**：「pool=0的时候就该停车等。但，我之前就提出过，没有配对锁就禁止滑行。为什么没有配对锁反而还能滑行，这是一个缺陷吗」。

### 33.1 结论先行

**是缺陷。但不是"没实现"，是"实现了、又用一层放宽把它漏掉了"。**

玩家要的那条规则**就在代码里**，`src\train_cmd.cpp:17596 R3RIsParkedWaiting()`：

- `:17625` 判据：`if (r3r_goto_couple && r3r_partner == nullptr && !r3r_in_depot)`
- `:17641-17644`：再套一层 `if (!R3RHasCoupleTargetCandidate(consist))` 才 `*reason = "parked-no-pair"; return true;`（拒绝订路）
- `:17666` 才是速度门 `if (consist->cur_speed != 0) return false;`

即**这条门在速度门之前、且明确不看车速**，注释逐字写着（`:17598-17620`）：

> 玩家硬要求「第 5 步：必须先有配对再起动」：没有配对锁的 GOTO_COUPLE 一律不订路，**不看车速**……
> 拦掉路线预留后 `GetCurrentMaxSpeed()` 归零，车自己停在等待区车站的停车点上。
> ② 速度 0 的车本来就不订路（KI-348）……真正要拦的是"还在动"的车。

⇒ 设计意图与玩家诉求完全一致。**99 能滑，是因为这条门当 tick 放行了。**

### 33.2 为什么放行：门内那层"安全网"与扫描池不是同一把尺子

门内条件 `R3RHasCoupleTargetCandidate(consist)`（`:17564-17594`）**不看锁**，而是现场遍历全图，用与扫描相似但**独立实现**的接受集找候选：

```cpp
for (const Train *t : Train::Iterate()) {
    if (t->First() != t || t->First() == coupler->First()) continue;
    if (!R3RIsCoupleTarget(t)) continue;
    if (t->vehstatus.Test(VehState::Stopped)) continue;
    if (!R3RCoupleTargetAtOrderStation(coupler, t)) continue;
    if (R3RGetCouplePairPartner(t) != nullptr) continue;
    if (!R3RCoupleAllowedIgnoringPair(coupler, t)) continue;
    return true;
}
```

现场两套读数正面冲突：

| 来源 | 滑行期间的读数 | 含义 |
|---|---|---|
| `CPL-PAIRSCAN`（`:1502 :1511 :1514 :1539 :1544`，tick 2924~2927） | `pool=0 s_chain=0 s_target=0 s_stop=0 s_dest=0 best=-1 paired=0` | 扫描认为**池里一个候选都没有** |
| `TRP`（`:1436`） | `ok=1 res=1`（随后 `:1441 LOCO-MOVE spd=16`） | 预留**成功** ⇒ `parked-no-pair` 门没拦 ⇒ `R3RHasCoupleTargetCandidate()` 返回了 **true** |

⇒ **同一个 tick、同一列车，扫描说"没人可挂"，门说"有目标可挂"。** 门放行了，车就带着 `dest=491798` 一路滑（`:1509 spd=53` 峰值），滑到 tick 2928 `pool` 变 5、顺手建锁（`:1549`）。

`CPL-PAIRSCAN` 里 `s_chain=0` 尤其刺眼：连第一道"不同链/链头"都没过——而 75 明明在。这正是"两套实现各算一遍"的典型故障形态（与第 232 轮 KI-340"命中端与拼接端角色错位"同族）。

### 33.3 第二处漏：`look=1`（扩展已有预留）可能根本不经过这道门

`:1434 TRP veh=99 … look=0 … => ok=0 res=0` 紧跟 `:1436 TRP veh=99 … look=1 … => ok=1 res=1`。

- `lookahead != nullptr` 走的是 `FillTrainReservationLookAhead()`/`ExtendTrainReservation` 一路（`:17435-17437`），它**只延长旧预留**，不是"申请新预留"。
- `parked-no-pair` 门挂在新建预留路径上（注释自陈"本函数所在的路线预留路径"）。
- ⇒ 只要车在某个 tick 借一次新建预留起步，之后靠**扩展**就能持续往前铺，门再也咬不到它。

这一条与 33.2 是并列的两个候选机制，**尚未定验谁是真凶**（见 33.5）。

### 33.4 这层"安全网"是怎么被加上去的：历史债（重要，能解释"为什么反而更漏"）

`:17626-17640` 的注释记录了 add 它的原因：

> round 310：13 584 `CPL-PAIRLOCK` 对 13 586 `CPL-PAIRUNLOCK`（`why=order-changed, actOrd=17`），锁活不过一个 tick，**使 `parked-no-pair` 门每两 tick 触发一次，产生 0/27/30 的蠕动起步**。
> 所以改成：门的条件从「有没有锁」放宽为「**有没有任何值得锁的对象**」……

⇒ 因为锁会被对侧每 tick 拆掉（round 312 已修 `:13626` 那侧），门的"无锁"条件抖动，于是被替换成"自己扫一遍"。**修抖动时把判据从"锁"换成了"另一套扫描"，把一致性也一起丢了。** 这就是"没有配对锁反而还能滑行"的直接来路。

### 33.5 待定验的两个机制（下一轮各取一组即可）

- **M-1（接受集不一致）**：给 `R3RHasCoupleTargetCandidate()` 加只读探针，命中时打 `CPL-CAND-PASS veh= cand= ord= tile=`，与同 tick `CPL-PAIRSCAN` 的 `pool=` 并列。判据：滑行帧出现 `CPL-CAND-PASS` 而 `pool=0` ⇒ M-1 成立。
- **M-2（lookahead 绕过）**：在 `R3RTryPathReserveCore` 入口与 lookahead 扩展入口各打一行 `TRP-GATE veh= look= reason=`。判据：`TRP look=1 ok=1` 之前**没有**任何 `TRP-GATE` ⇒ M-2 成立。
- 两者可同时成立（互不排斥），修法也不同（M-1 统一判据；M-2 把门提到 lookahead 路径或让门清 `lookahead`）。

### 33.6 修法方向（待玩家拍板）

- **修-1（推荐，治根）**：把门内那层 `R3RHasCoupleTargetCandidate()` 自遍历**删掉**，改成直接读扫描池的结果（`pool > 0` 或"本 tick 扫描已完成且 pool==0"），让"能不能走"与"能不能锁"共用同一份候选集。为防 round 310 抖动复发，配套要求：扫描的 `pool` 必须**只在 backoff 窗口外**才被当作否定依据（backoff 期间维持上一 tick 的结论 = 迟滞）。
- **修-2**：`lookahead` 路径同样过门——门命中时 `consist->lookahead.reset()` 并归零 `cur_speed`（否则扩展预留会绕过一切新建预留的限制）。
- **修-3（护栏，可与上两条并行）**：加不变量探针 `PAIR-INVARIANT-VIOLATION veh= spd= pool= lock= tile=`：当 `GOTO_COUPLE && 无锁 && pool==0 && spd>0` 时无条件打一行。它不改变行为，但能让"这条规则被违反"从"要靠人肉比对两个标签"变成"日志里直接有一行"，且能长期防回归。
- **不建议**：把门挪回速度门之后（等于放弃玩家诉求），或再给门加更多"安全网"（每次放宽都会漏一次，本轮就是证据）。

## 34. 第 318 轮：三条修法全部落地（玩家拍板，2026-10-26）

**玩家原话**：「第二本帐可以删，也就是修法1可以执行。修法2也可以执行。修法三，加日志，也可以。修法1的那个迟滞，「按上一帧走」」

### 34.1 先修正 §33.2 的一个疑点（重要，避免下一轮拿错前提）

复核 `R3REnsureCouplePair()` 后确认：`CPL-PAIRSCAN` 行在**五个分支**都会打，其中只有 `none / refused / locked` 三个分支真的走过 `Train::Iterate()`；`backoff`（`:13849`）与 `steady`（`:13806`）是**提前 return**，此时 `r3r_s_pool` 恒为 0（局部计数器 `:13755`，只在遍历循环里自增 `:13873`）。

⇒ §33.2 写的「同一个 tick 扫描说 pool=0、门说有目标」**不必然是"两套判据冲突"**，也可能是「本 tick 扫描在 8-tick 退避窗口里根本没扫（`state=backoff`），而门内自遍历是实时的」。现场日志已不可复查（`build\R3R_debug.log` 被新会话覆盖），故此处登记为**疑点 REG-318-A（待验证）**：

- 期望值：滑行帧的 `CPL-PAIRSCAN` 行 `state=none` 且 `pool=0`；
- 待验：滑行帧是否为 `state=backoff`（此时 `pool=0` 属正常）。
- 对修法的影响：**无**。两个机制（M-1 接受集不一致 / M-1′ 退避滞后）都指向同一个真问题——「门的判据不是扫描的判据，且门比扫描更新鲜或更陈旧」，而修-1（统一到扫描结论 + 退避期按上一帧）对两者同时成立。

### 34.2 修-1（已实现）：删掉第二本账，门直接读扫描结论

- **删除** `R3RHasCoupleTargetCandidate()`（原 `:17564-17594`，含函数体）——这就是"第二本账"，全文件唯一调用点是门内 `:17641`。
- **新增** 扫描结论缓存（键=链头 index）：
  - `struct R3RPairVerdict { bool has_candidate; uint candidates; uint64_t tick; }`；
  - `static std::unordered_map<uint32_t, R3RPairVerdict> _r3r_pair_verdict`（声明在 `_r3r_pair_scan_tick` 旁，`:8423` 之后）。
- **写入点**（全部在 `R3REnsureCouplePair()` 内，与 `CPL-PAIRSCAN` 的 state 一一对应）：
  | state | 写入 | 理由 |
  |---|---|---|
  | `locked`（`:13934`） | `{true, 1}` | 已建锁 |
  | `steady`（`:13806`） | `{true, 1}` | 已持有效锁 |
  | `refused`（`:13930`） | `{false, 0}` | 有候选但**拿不到锁**；玩家规则是"先有配对再起动"，拿不到锁就不该走 |
  | `none`（`:13920`） | `{false, 0}` | 扫了，确实没人 |
  | `backoff`（`:13846`） | **不写** | 这就是"按上一帧走"的迟滞实现点 |
  | 非 GOTO_COUPLE（`:13726`） | `erase` | 订单已结束，缓存必须失效 |
- **读取点**：门（`R3RIsParkedWaiting()` 的 `parked-no-pair` 支）只查这张表；条目不存在 ⇒ 保守视为"没有候选"。
- **为什么不倒退成 0/27/30 蠕动**：旧抖动来自"锁活不过一 tick"，round 312 已修对侧（`why=order-changed` 不再拆活锁，`:13711-13729`）；现在退避窗口内维持上一帧结论，`false` 就持续 `false`（车停稳），直到下一次扫描（≤8 tick）扫出候选并建锁，建锁处会主动替它订一次路线（`:13937`）。抖动需要"结论每 tick 翻转"，而缓存只在真正扫描时改写，天然消除了这个形态。

### 34.3 修-2（已实现）：门命中时清掉 `lookahead`，不给"扩展旧预留"留后门

- 门内 `parked-no-pair` 命中时：若 `consist->lookahead != nullptr` 则 `consist->lookahead.reset()` 一次，并写 `RESV-NOBOOK-LOOKAHEAD veh= …`。
- **不写 `cur_speed`**：`lookahead` 一清，`GetCurrentMaxSpeed()` 归零，车按正常刹车曲线停住；直接写 `cur_speed = 0` 是瞬停（观感等同撞墙），而且与既有注释（`:17545`「the chain brakes by itself」）不一致。
- 只 reset `lookahead`、不动路径预留（`FreeTrainTrackReservation` 会连脚下保护一起清，那会牵动耦合寻路的 `ReserveTrackUnderConsist` 前置——与 KI-208/C4-3 定的边界冲突）。若复测证明"沿旧预留继续滑"仍在，再评估是否需要 `FreeTrainTrackReservation(consist, origin.tile, origin.trackdir)`。

### 34.4 修-3（已实现）：护栏探针 `PAIR-INVARIANT-VIOLATION`

```
PAIR-INVARIANT-VIOLATION veh=%d tile=%d,%d spd=%d cand=%u verdict=%d lock=0 real=%d
```
- 触发条件（门内，`parked-no-pair` 命中当刻）：`GOTO_COUPLE && partner==nullptr && !in_depot && 无候选 && cur_speed != 0`。
- 节流：按车一次 episode（`btree_set<VehicleID> _r3r_pair_invariant_logged`），获得候选 / 车速归零 / 订单结束 / 不再是 waiter 时 erase。
- 用途：把"这条硬规则被违反"从"人肉比对两个标签"变成日志里一行（含 `cand=` 与 `verdict=`，可直接区分"扫描说 0"与"缓存里干脆没条目"）。

### 34.5 复测判据

1. 滑行帧消失：`pool=0`（`state=none`）期间不再出现 `TRP … => ok=1 res=1`。
2. 出现 `PAIR-INVARIANT-VIOLATION` 且**紧跟** `RESV-NOBOOK … reason=parked-no-pair`（同一 tick），且该车在 ≤8 tick 内建锁（`CPL-PAIRSCAN state=locked`）后才起步。
3. 不再出现 `0 → 27 → 30` 蠕动起步（速度序列应为 `0 → 0 → … → 30` 单调一次到位）。
4. 正常挂车不受影响：`COUPLE-OK` 照常、`steady` 期间无 `PAIR-INVARIANT-VIOLATION`、库里挂接（`depot-paired`）无回归。
5. 负判据：`WAIT_COUPLE` 等待车底（`parked-waiter`）与纯车底（`parked-caronly`）行为逐字不变。

### 34.6 落地时补的一道护栏：缓存新鲜度（自查发现）

`_r3r_pair_verdict` 以链头 `index` 为键，而车辆池槽位会复用：若某 index 的旧结论一直留在表里（例如那条链被卖掉后新车占用同一槽位），新车会**凭上一辆车的结论**被放行。因此门内在读表时加新鲜度检查：

```cpp
static const uint64_t R3R_PAIR_VERDICT_MAX_AGE = 64;
const bool fresh = it != _r3r_pair_verdict.end() && (_tick_counter - it->second.tick) <= R3R_PAIR_VERDICT_MAX_AGE;
const int  verdict = !fresh ? -1 : (it->second.has_candidate ? 1 : 0);   // -1 = 无条目/已过期
```

- 正常路径永远不会触发过期：`none/refused/locked/steady` 都会在 ≤8 tick 内改写结论，`backoff` 窗口本身只有 8 tick。
- 过期一律按**无候选**处理（保守：宁可多停一个 tick），并让 `verdict=-1` 在日志里与 `verdict=0`（扫描说没人）可区分。
- 这不算"第二本账"：过期判定只判"这条记录还属不属于这辆车"，判据仍然是扫描的结论。

### 34.7 构建自证（第 318 轮）

- 复用既有 `_tmp_inc_build.cmd`（**未新建任何 .cmd**）；未改任何 `src\*.h` / `src\lang\*.txt`，护栏判 `GUARD: incremental is safe (no header/lang file is newer than the newest object)`。
- `src\train_cmd.cpp` @2026-10-26 16:02:04 → `build\CMakeFiles\openttd_lib.dir\src\train_cmd.cpp.obj` @16:03:16 → 日志 `[3/3] Linking CXX executable openttd.exe` → `build\openttd.exe` @**2026-10-26 16:05:56**（52 285 952 B）。
- `build\R3R_incbuild.done` = `EXIT_CODE=0`；日志内 `error C[0-9]|fatal error|FAILED:|build stopped` 计数 **0**。
- `read_lints`（train_cmd.cpp）**0** 条。
- 产物自证：`findstr /M` 在 exe 内 3/3 命中 `PAIR-INVARIANT-VIOLATION`、`RESV-NOBOOK-LOOKAHEAD`、`parked-no-pair`；全文件搜索确认 `R3RHasCoupleTargetCandidate` 只剩两处注释引用（`:8429` / `:17670`），无残留调用。
- 状态 = **已实现 + 已编译**，游戏内复测待做（判据见 §34.5）。




