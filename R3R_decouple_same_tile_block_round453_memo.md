# R3R 解挂后两部分无法拉开距离（疑同格判定挡下）—— 第 453 轮分析备忘

> 临时分析报告（本轮唯一可信状态载体）。结论只认本文件 + `R3R_KNOWN_ISSUES.md`。
> 状态：**取证中**（先落盘，再挖代码）。

---

## 一、玩家报告原文（2026-11-16）

> 「关于排程分配的图形界面，我目前实测没有问题。内部接线暂未测试。
> 目前发现的问题是：列车解挂之后，两部分无法脱离（无法拉开距离），疑似被同格判定挡下。
> 如果你发现是你傻瓜的问题，就直接修，如果是设计口径的问题，就先告诉我，让我决定」

拆成两条：
1. **分配规则 GUI（KI-643 多选调度）实机通过** —— 判据 ①~④ 玩家自述无问题；**「内部接线」未测**（即把勾选的掩码真正喂给求值器/调度分配的那一条链，仍为未验证）。
2. **本轮新问题**：解挂后机车与解出的车底**互相卡住、无法拉开距离**，玩家怀疑是「同格判定」挡下。

---

## 二、已知同族条目（先查这些，别重复造轮子）

| 条目 | 内容 | 与本轮的关系 |
| --- | --- | --- |
| KI-330（第 221 轮，记忆 34679553） | T8701 场景机车滞留 100,64：`TPR-BLOCKED veh=48 tile=100,64 ... otherTile=100,64 otherFirst=0 caronly=1`，8/8 出现在每次 `DECOUPLE-DONE` 之后且 `otherTile == mf`（同格） | **症状逐字吻合**。当时的结论：`train_cmd.cpp` FollowTrainReservation → `pbs.cpp` CheckTrainsOnTrack（不排除自己、取 index 最小链头）→ `other_train != consist` 即判阻塞 → `MarkTrainAsStuck` → return TPRRF_NONE。修法①「解挂缝豁免（同格 + cur_speed==0 不视作阻塞）」当时标注**待拍板、未落地** |
| KI-335（第 228 轮，记忆 85287007） | 解挂互锁 `DECOUPLE-INTERLOCK set/wait/clear`：让解出的车底等前车离开站台（dist > R3R_DECOMP_CLEAR_TILES）才起步 | 若机车端被同格判定卡住 ⇒ dist 恒 0 ⇒ **互锁永不放行** ⇒ 两部分都不动（可能的死锁组合） |

**待验证的猜测根因（REG-453-A，未实证）**：
解挂后机车（v 半）要开路 → 走进 `FollowTrainReservation` → `CheckTrainsOnTrack` 发现同格上有另一列车（刚解出的车底 u 半，`otherFirst=0`）→ 判阻塞 → `MarkTrainAsStuck` ⇒ 机车不动；同时 u 半（车底）被 KI-335 的解挂互锁挡着 ⇒ **两侧互等，形成死锁**，表现即玩家所说"无法拉开距离"。

**为什么"同格"在解挂场景下是必然**：解挂（DecoupleTrain）是纯粹的链切分，**不改任何车辆坐标**，所以切开的瞬间两半必然同格/紧邻；任何"格上已有别的列车就不许动"的判据在解挂缝隙处都会误伤 ≥1 个 tick，而如果两侧互为对方的"挡路者"，就不是 1 个 tick 而是永久。

---

## 三、性质初判（待取证确认后才下结论）

- 若确认是"调用点没排除解挂缝/没排除同格的被动等待方" ⇒ **实现缺陷（傻瓜问题），按用户授权直接修**，不动 pbs.cpp 的 desync 冻结区（沿用 KI-330 修法①的思路：在 `train_cmd.cpp` 的调用点做豁免）。
- 若确认是"R3R 有意要求解挂后必须由玩家手动给车底下令才分开" ⇒ 设计口径，先报玩家拍板。

---

## 四、取证清单（按序，每条结论立即回填本文件）

| 编号 | 要查的点 | 期望/判据 | 状态 |
| --- | --- | --- | --- |
| E-1 | `src\pbs.cpp` 的 `CheckTrainsOnTrack`：同格判定分支、是否排除自己/被动等待方 | 找到"同格另一列车即 return"的位置与行号 | **已取（见 §四.1）** |
| E-2 | `src\train_cmd.cpp` 的 `FollowTrainReservation`：`TPR-BLOCKED` 判定与 `MarkTrainAsStuck` 调用点 | 确认 `other_train != consist` 就判阻塞，且无同格豁免 | **已取（见 §四.2）——同格豁免**已存在**，且已是第 349 轮去速度版；但同格判据用的是「链头格」** |
| E-3 | `DECOUPLE-INTERLOCK` 的 set/wait/clear 判据与放行条件 | 确认 clear 是否只依赖几何距离 | **已取（见 §四.3）** |
| E-4 | 解挂后两侧的订单状态 | 若 v 半没有下一单，则不是阻塞问题 | 待取（需现场日志） |
| E-5 | 当前 exe 是否已含上述豁免 | 检索 exe 内字面量 | 待取（低优先，代码已确认树里有） |

### §四.1 `pbs.cpp` `CheckTrainsOnTrack`

- `FollowTrainReservation()`（`train_cmd.cpp`）对 PBS 路径逐格问"这格上有没有别的列车"，
  命中即通过 `other_train` 出参返回**该格上 index 最小的列车链头**（`pbs.cpp` 的
  `CheckTrainsOnTrack`，第 221 轮记忆已核；与本轮结论无冲突）。
- 因此 `other_train->tile` 是**那一链的链头所在格**，不是"它压在我脚下的那一节"的格。

### §四.2 `train_cmd.cpp` 的阻塞判定与两道豁免（真实行号，2026-11-16 核）

| 行号 | 内容 |
| --- | --- |
| `src\train_cmd.cpp:19960` | `if (other_train != nullptr && other_train->index != consist->index)` —— 只排除自己 |
| `:19977` | `TPR-BLOCKED veh=... tile=... mf=... origin=... otherTile=... otherFirst=... caronly=...` 诊断行 |
| `:20022-20025` | **配对豁免**：`tpr_pair_hit`（另一链链头 == `R3RGetCouplePairPartner(consist)`）⇒ 放行 |
| `:20026` | **同格判据**：`const bool tpr_same_tile = (other_train->tile == consist->tile);` |
| `:20031-20032` | **方向判据**：`tpr_same_tile_ahead = tpr_same_tile && R3ROtherChainAheadOfMovingFront(consist, Train::From(other_train));` |
| `:20037` | `else if (tpr_same_tile && !tpr_same_tile_ahead)` ⇒ `TPR-SEAM-EXEMPT`，放行（**第 349 轮已去掉 `cur_speed == 0` 条件**） |
| `:20062-20088` | 否则（含 `tpr_same_tile_ahead` 为真）⇒ 打 `TPR-SEAM-DIR-BLOCK`、`MarkTrainAsStuck(consist)`、`return TPRRF_NONE` |

`R3ROtherChainAheadOfMovingFront()`（`src\train_cmd.cpp:19430-19446`）：以
`GetMovingFront()` 的 `GetMovingDirection()`（**db=1 时 moving front 是链尾**）取像素步长，
遍历另一链全部车节，凡与 moving front 的像素切比雪夫距离 ≤ 32 且 `step·(dx,dy) > 0` 即判"在前方"。

**⇒ 关键结论（代码层已确定的两条硬事实）**

1. **同格豁免判据比 KI-330 的现场判据窄**：KI-330 现场证据写的是
   `otherTile == mf->tile`（与**移动前端**同格），而代码比较的是
   `other_train->tile == consist->tile`（与**链头**同格）。多节列车（尤其 db=1 反向、
   moving front 在链尾）两半解挂后，车底链头的格与机车链头的格可以不同 ⇒
   **豁免不触发、误判阻塞**。这是"傻瓜 bug"候选（记为 **P-453-1**）。
2. **第 350 轮 KI-523 的方向限制与解挂互锁可能组成死锁**：机车若要朝解出车底那一侧
   行驶（典型：解挂后反向回库），`tpr_same_tile_ahead` 恒真 ⇒ 机车 `MarkTrainAsStuck`
   永不动；同一时刻解出的车底被 `R3RDecoupleInterlockWait()` 按住（判据是"本到发线上
   除了我这条链没有别的列车"）⇒ 两侧互等，**永久无法拉开**（记为 **P-453-2**）。
   KI-523 注释里写的"转向或车底让开后自然失效"隐含前提是"车底会自己让开"，
   而纯车底（`R3RIsCarOnlyFormation`）被停驻门按住**永远不会自己走** ⇒ 该前提不成立。

### §四.3 `DECOUPLE-INTERLOCK`（KI-335）现状

| 行号 | 内容 |
| --- | --- |
| `src\train_cmd.cpp:3161` | `R3RRecordDecoupleSeam()` 定义（解出方**非纯车底**才登记，`R3RIsCarOnlyFormation(follower)` 早退）；调用点 `:11039` |
| `:3381-3391` | `R3RDecoupleInterlockWait()`：后车自己是 `OT_WAIT_COUPLE` 或纯车底 ⇒ 直接撤销登记、不锁 |
| `:3393-3396` | 后车已不在站台格 ⇒ 撤销、放行 |
| `:3436` | `leader_left = !IsRailStationTile(seam_car->tile)`（前车尾车离开站台格） |
| `:3438+` | 玩家 2026-10-21 拍板 C-1/C-2/C-3 后的判据：**本到发线上除了本链没有别的列车**；`:3476` 再问一次 `TryPathReserve`，成功即 `DECOUPLE-INTERLOCK release ... reason=platform-clear` |
| `:3147/:3139-3146` | C-3 明确**取消超时强制放行**（只保留 map 有界收缩），所以"等不到就放行"这条兜底不存在 |
| `:24120-24125` | 消费点：命中即 `cur_speed = 0; subspeed = 0; return true`（本 tick 不起步） |

⇒ 互锁只锁**后车**；只要前车（机车半）被别的东西按住不动，互锁就永不放行（P-453-2 的另一半）。

---

## 五、未确认项

- **U-453-1**：玩家现场没有日志。需要 `build\R3R_debug.log` 采样才能确认是
  `TPR-SEAM-DIR-BLOCK`（→P-453-2 设计口径）、`TPR-BLOCKED ... otherTile`（→P-453-1 判据过窄）
  还是订单为空/`DECOUPLE-INTERLOCK wait`。**下面 §六 已给出三条分支各自的判读法与修法**，
  任一条都能在拿到日志后 10 分钟内收口。
- **U-453-2**：玩家说的"两部分"是否都在站台上（KI-335 只覆盖站台格）；野外解挂是否同样卡住未知。
- **U-453-3**：现场是否 db=1（机车反向）——决定是否命中 KI-523 方向限制的关键；
  `TPR-SEAM-DIR-BLOCK` 行里有 `db=` 与 `dir=` 两个字段可直接判读。

---

## 六、三条分支的判读与处置（拿到日志后按此执行）

| 日志签名 | 定性 | 处置 |
| --- | --- | --- |
| `TPR-BLOCKED ... otherTile=X,Y` 且 `X,Y` **不在** `tile=`（链头格）上，但在 `mf=` 上或与 `mf=` 同格 | **P-453-1 实现缺陷（傻瓜 bug）** | 直接修：同格判据由"链头格相等"放宽为"另一链的某一节与我的 moving front（或链头）同格/贴身"，保留 KI-523 方向限制不动 |
| `TPR-SEAM-DIR-BLOCK ... db=1 ... caronly=1`（反复出现，且机车一直不动） | **P-453-2 设计口径（防撞 vs 分开）** | 先报玩家拍板：①维持现状（要求玩家给车底下一条离开的订单/换端）②对"另一链是纯车底/WAIT_COUPLE 停驻方"给方向豁免（承认可能撞、或改为允许"缓慢接近到 2px 停住"）③解挂时把两半沿轨道推开一段（改动最大、触几何禁忌） |
| `DECOUPLE-INTERLOCK wait ...` 反复出现且**前车有速度/在动** | 互锁判据误判（前车已让开仍未 clear） | 查 `R3RPlatformClearForConsist()` 的到发线判定、以及 `seam_car` 是否随身份迁移失配；属实现缺陷，直接修 |
| 只有 `DECOUPLE-DONE` 之后无任何探针，且两侧 `TRP`/`CPL` 全无 | 订单层问题（机车无下一单 / 车底被 P7 按住） | 先查 `ORD-AFTER-*`/`LOCO-AFTER-DECOUPLE`，属排程口径，报玩家 |

> 备注：KI-523 的方向限制是 **2026-10-28 实机撞车**（机车 57 反向 2px 撞进车底 0）之后
> 加的，只在"同格 + 另一半压在我行走方向前方"时拒绝预留。它不是无脑阻塞，
> 但它的解锁前提（"车底会自己让开"）对纯车底不成立 —— 这是本轮唯一发现的**结构性死锁组合**。

---

## 七、回归嫌疑与待拍板选项（2026-11-16）

### §7.1 回归嫌疑：KI-523（第 350 轮，2026-10-28）之后才可能出现

时间线自证：

| 时点 | 同格处理 | 结果 |
| --- | --- | --- |
| 第 221 轮（2026-10-07）之前 | 同格无豁免，`other != consist` 即判阻塞 | 机车不动（KI-330，靠日志做实） |
| 第 221/349 轮修法后 | 同格豁免（第 349 轮还去掉了 `cur_speed == 0` 条件） | 机车**放行起步**，能与车底分开（代价：极端方向会撞，故有 350 轮） |
| **第 350 轮 KI-523（2026-10-28）** | 同格豁免 + **方向限制**：另一半在我行走方向前方 ⇒ `TPR-SEAM-DIR-BLOCK`，不预留、不动 | 朝车底方向行驶的机车**永久不动** |

⇒ 玩家此前从未报「解挂后无法拉开」，本轮（2026-11-16）首次报，且 KI-523 是最近一次改动该判据的轮次 ⇒ 高度怀疑是 KI-523 的回归。KI-523 的守卫只写了"转向或车底让开后自然失效"，但**纯车底被 P7 停驻门按住永不动**（`R3RIsCarOnlyFormation` 不订路），所以"车底让开"这一前提在解挂缝的典型形态里不成立。

### §7.2 三条待拍板选项（P-453-2）

| 选项 | 做法 | 代价 | 适用 |
| --- | --- | --- | --- |
| **① 维持现状** | 要求玩家给车底下一条离开的订单，或让机车换端 | 零风险、零改动；但"解挂即可分开"的直觉不成立 | 若玩家接受手动调度 |
| **② 纯车底方向豁免（推荐先试）** | 只在「另一半是 `R3RIsCarOnlyFormation` 或 `OT_WAIT_COUPLE` 停驻方」时收起方向限制（豁免只对被动停驻方生效，对"有引擎、有订单、只是暂时停着"的链仍保留 KI-523 保护） | 仍可能以低速顶到车底（KI-523 现场是 spd=23 撞），需要配套"起步限速"或"接近到 ~2px 停住" | 解挂缝最常见形态 |
| **③ 解挂时沿轨道推开一段** | `DecoupleTrain()` 提交点把两半各推开 N px | 改动最大、触"R3R 不重排位置"禁忌，风险高于收益 | 不推荐 |

**② 的最小改法**（若玩家拍板）：`src\train_cmd.cpp:20031` 的
`tpr_same_tile_ahead` 追加 `&& !tpr_other_parked`，其中
`tpr_other_parked = R3RIsCarOnlyFormation(Train::From(other_train)) || (Train::From(other_train)->IsPrimaryVehicle() && Train::From(other_train)->current_order.IsType(OT_WAIT_COUPLE))`；
同时把 `TPR-SEAM-DIR-BLOCK` 行加 `parked=%d` 便于复测判读。

### §7.3 若日志显示 P-453-1（同格判据过窄）

最小改法：`:20026` 之后补一遍「`other_train->tile` 是否等于 `consist` 链上任一节的 `tile`」（`consist` 遍历上限 128 节），命中即视为同格。只改 `train_cmd.cpp`，不动 `pbs.cpp`。

---

## 七附、玩家拍板 = P-453-1（2026-11-16，本轮已落地）

**玩家原话**：「啊，我倾向于P-453-1，因为那次就是机车（链头）在前开着DB到站然后直接解挂的，解挂后后半部分movingfront和first是不一样的」

⇒ **现场定性**：机车（链头）带 **DB 倒车**到站后直接解挂；解出方（后半车底）的 `moving front` 与 `first` 不在同一格 ⇒ `CheckTrainsOnTrack` 回传的 `other_train->tile`（车底**链头**格）≠ `consist->tile`（机车**链头**格）⇒ `tpr_same_tile` 恒假 ⇒ KI-330 的同格豁免永不触发 ⇒ 机车把刚解下的车底判成"前路被占"→ `MarkTrainAsStuck`；同一时刻车底侧被 `DECOUPLE-INTERLOCK` 按住等"前车走远" ⇒ 两侧互等，现象就是**无法拉开距离**。
⇒ **实现缺陷（傻瓜 bug），不是设计口径**。§7.2/§7.3 的 P-453-2（KI-523 方向限制死锁）与 P-453-3（回归嫌疑）降级为"未被本次现场证实的旁支"，保留在册不再作为本轮结论。

### §7.5 改动清单（仅 `src\train_cmd.cpp`；未碰 `.h`；未碰 `pbs.cpp` 冻结区）

| 行号 | 内容 |
| --- | --- |
| `:19447-19467`（新增） | `static bool R3RChainTouchesTile(const Train *head, TileIndex tile)` —— 链上是否有任一节停在 `tile`；nullptr 守卫 + 128 节上限 + 纯读零改写 |
| `:20046-20062` | `tpr_same_tile` 由「`other_train->tile == consist->tile`」放宽为「链头格相同 **或** 另一链链头格落在我链覆盖格上 **或** 我的链头格落在另一链覆盖格上」；`tpr_same_tile_ahead`（KI-523 方向保护）原样保留 |
| `:20063-20064` | 新增 `tpr_touch_only`（仅日志用：链头格不同、靠贴身判据才认出解挂缝） |
| `:20105-20114` | `TPR-SEAM-EXEMPT` 行尾追加 `touch=%d otherTile=x,y mf=x,y`（前 11 个字段顺序不变，不破坏既有判读习惯） |

**为什么不动 KI-523**：`tpr_same_tile_ahead` 仍按像素（32px）+ `GetMovingDirection()` 判"另一半是否在我行走方向前方"。放宽 `tpr_same_tile` 只是让更多**真贴身**的解挂缝进入这一层判断（此前它们连判断机会都没有，直接被当阻塞）；真朝车底方向行驶时仍会被 `TPR-SEAM-DIR-BLOCK` 拦下。

**真阻塞不受影响**：另一列车停在我预留前路的尽头格时，两侧车节都不挨着 `consist->tile`，三个判据全假 ⇒ 照旧 `MarkTrainAsStuck`。

### §7.6 复测判据（5 条）

1. 同一场景（机车 DB 到站→解挂）解挂后 1~2 秒内两半开始分离、距离单调增大。
2. 日志出现 `TPR-SEAM-EXEMPT ... touch=1 otherTile=x,y`（`otherTile` 与同行的 `tile=` 不等）—— 这一行就是旧版被打成 `TPR-BLOCKED` 的那一 tick 的正面证据。
3. `DECOUPLE-DONE` 之后不再成对出现 `TPR-BLOCKED veh=<机车> ... otherTile=<车底链头格>` 与 `DECOUPLE-INTERLOCK wait`。
4. 负判据：另一列车（非解挂配对）停在我前方预留终点格 ⇒ 仍 `TRP ok=0 res=0` 不移动。
5. 负判据：反向朝车底那侧行驶 ⇒ 仍出现 `TPR-SEAM-DIR-BLOCK`（KI-523 未回退）。
