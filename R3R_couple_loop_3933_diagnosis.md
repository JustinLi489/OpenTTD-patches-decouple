# R3R 死循环诊断:机车停 39,32 / 车底占 39,33,永远挂不上(2026-09-05 晚)

> 续作先读本文件。对应现场:build/R3R_debug.log(8207 行)。
> 状态:根因已定位到"命中判据 vs 拼接姿势错位",代码修复待用户拍板。

## 1. 现场时间线(日志行号)

| 行 | 事件 |
|---|---|
| 1-17 | 机车 veh=0 在 depot 38,27 首次 GOTO_COUPLE,原地 A1 直接拼成(COUPLE-OK,consist=3,isCG=1,9 节合并) |
| 27-30 | 机车整列驶到 tile 39,33(station 轨)停靠,3 段 CHAIN:idx0 y=543 / idx3 y=533 / idx6 y=523(机车在南、车底段在北,车底段已伸到 39,32) |
| 31 | DECOUPLE-FIRE @39,33 real=1 → DecoupleTrain,u=3 车底(6 节)留下,isCG=1 |
| 36-44 | 机车 ADVANCE 到 real=3(16)=GOTO_COUPLE;车底 WAIT_COUPLE(17) |
| 45-50 | 车底链(3,4,5,6,7,8)横跨 39,33/39,32:veh3,4 在 39,33;veh5,6,7,8 在 39,32 |
| 51-64 | 机车驶向 station 39,31(destTx=39 destTy=31),curType=6 |
| 4641 | 唯一一次 COUPLE-FAIL:机车在 37,28(order16,dest 39,31),GetCouplePosition/fallback 双双未命中 |
| 6211-6243 | **死循环单轮样本**:A1(COUPLE gap25)→A2(COUPLE-FLIP gap16)→回滚→A3(SWAPV+FLIP)→A3-ARRANGE→FOLDCHK COUPLE-FLIP-BOTH gap16→回滚 |
| 6256-6257 | DEPOT-ARR:机车 veh=0 spd=0 real=3(16) curType=16 @39,32,dest=39,31,tileEqDest=0(机车未到 dest) |
| 8078-8111 | 同一循环在日志后段仍在重复(14 轮 A1→A3 全折叠回滚),之后全被 RESERVECONSIST 刷屏 |
| 8112-8207 | 纯 RESERVECONSIST 死循环(每 tick 6 行,ok=0 resNow=1),日志 99% 被刷掉 |

## 2. 死循环机制

机车停 39,32 spd=0、current_order=OT_GOTO_COUPLE(16)、dest_tile=39,31,每 tick:

1. `TrainCoupleHandler`(4572)→ 非 depot 分支 → `GetCouplePosition`(4607)。
2. 机车与车底端**中心距恰好命中阈值**(见 §3),u 非空 → `Couple(v,u)` → `TryTrainCouple`。
3. `TryTrainCouple` A1→FOLDCHK 折叠→A2 逻辑翻 u→FOLDCHK 折叠→A3(v 物理 SWAP + u 逻辑翻)→FOLDCHK 折叠→全部回滚,return false。
4. `Couple` 仅发一次 news(每次同文案),机车原地不动;下 tick 回到 1 → 无限循环,每 tick 物理 SWAP v 两次、逻辑翻/撤 u,还配 6 行 RESERVECONSIST 日志。

`COUPLE-FAIL`(u==nullptr)只出现 1 次(4641)的原因:此处每 tick u 都被命中,失败发生在更深的 TryTrainCouple 折叠回滚,永远不会走到 u==nullptr 分支。

## 3. 根因:命中点与拼接姿势错位

证据(单轮样本 8079-8111):

- A1 拼接 `v_last=idx2 ↔ u_head` 后,FOLDCHK COUPLE 报 `A idx=2 y=516 B idx=3 y=533 exp=2 dist=17`(gap 15 > 8 阈值):机车链尾与车底链头中心距 17px,中间隔约两节车的空档。
- A2/A3 逻辑翻 u 后 FOLDCHK 报 `A idx=8 y=515 B idx=3 y=533 exp=2 dist=18`:车底链序 6→7→8→3→4→5,8 与 3 在链内相邻却相距 18px —— 车底链 6 节横跨 39,32/39,33 边界时自身就不连续(或 8 与 3 分属两个物理段,中间有空轨)。

对照 GetCouplePosition(4532-4565):
- `FollowTrainReservation` 找机车正前同轨车辆;命中 u 后按 `reverse`(同向=reverse)取 `z = u->Last()` 或 `u`,再要求精确端距 `(v_len+1)/2 + (u_len+1)/2`。
- 现场机车 nose 正顶车底尾(reverse=true,z=Last),中心距≈2px 命中,但**拼接时 TryTrainCouple 却总是拼 `v_last ↔ u_head`**(机车尾对车底头)。
- 机车鼻贴到的是车底尾,而拼接要求机车尾贴车底头 —— 两者隔着"机车全长 + 车底全长"的空间距离;A1/A2 逻辑翻不动空间,A3 把 v 物理 SWAP 后 v_last 换到另一端,但机车卡在 39,32 与车底(横跨 39,32-33)之间的空档,SWAP 后 v_last 与车底头仍差 ~17px → 折叠判定永不通过。

即:**GetCouplePosition/fallback 的"贴脸命中"与 TryTrainCouple 的"尾-头拼接"前提互相矛盾**;命中不代表具备可拼接几何。

### 3.1 用户实证确认(2026-09-05 会话:三列三节铰接车,与日志反推完全吻合)

- 机车 = idx0 组(idx0-idx1-idx2,爸爸 idx0,真引擎);车底 = idx3 组 + idx6 组(已耦合 6 节);当前版本耦合从未成功。
- 地图排布(北→南):`N idx2-idx1-idx0-idx8-idx7-idx6-idx5-idx4-idx3 S`。idx0/3/6 是爸爸(artic 父车),每组三节焊死;真正可拆边界是组间(idx5↔idx6、idx0↔idx8)。
- 机车正朝南移动、鼻尖顶车底尾,与车底同向 → `GetCouplePosition` 里 `dir_diff=Same` → `reverse=true` → `z = u->Last()`(`Last()` 走 first/last 缓存,不跳 artic,见 vehicle_base.h:761;只有 `GetLastUnit()` 才跳)。
- 由日志 A1 折叠对 `A idx=2 y=516 B idx=3 y=533` 反推:车底链 `First=idx3`(南端爸爸)、`Last=idx8`(北端 artic 子车)。故两端点角色是:
  - **命中端**(GetCouplePosition):`v = idx0`(机车爸爸,鼻尖)↔ `z = u->Last() = idx8`(artic 子车),半长和 ≈ 2px —— 机车鼻一贴上 idx8 即命中,每 tick 触发;
  - **拼接端**(TryTrainCouple):`v_last = idx2`(机车物理尾,`R3RTrainTail` Next 走到头、不跳 artic)↔ `u_head = idx3`(车底物理头),中间隔着整条链,中心距 ~17px → FOLDCHK 永不通过。
- 结论强化 §3:命中鼻尖 vs 拼接尾-头,端点角色错位被 artic 放大 —— artic 短车让 exp≈2px、命中窗口极窄且只在"完全贴脸"时触发,于是"命中→必然折叠→回滚→再命中"每 tick 稳定死循环。

## 4. 修复方向(候选,待用户拍板)

A. **命中判据与拼接姿势对齐**:reverse=true(机车鼻顶车底尾)时,TryTrainCouple 应拼 `v_head(或 SWAP 后尾)↔ u->Last()`;或 GetCouplePosition 在 reverse 时不返回(避免进入必然失败的拼接),由机车继续前移进入 CheckTrainCollision 的自动耦合路径。
B. **让机车再贴近一步再拼**:fallback 命中(间距 ≤ 阈值附近、同向直轨、中间无阻)时,把机车按当前方向前进到物理贴合(端距 0),再走 Couple —— 而不是在 2px/17px 间距上硬拼链指针。
C. **限制重试频率并最终报错**:连续 N 次折叠失败后停下发 STR_NEWS_ORDER_COUPLE_FAILED 并保持 cur_speed=0、不再每 tick 重试(防刷屏/性能爆炸)。

## 5. 附带观察

- RESERVECONSIST 探针(6690 附近)每 tick 对整条车底链打 6 行,是日志刷屏主因;恢复现场诊断后应改为 edge-triggered。
- DEPOT-ARR(8588)已有 per-vehicle edge-trigger 机制(8569-8603),RESERVECONSIST 可照抄。
- tile 39,31 是 GOTO_COUPLE 的 dest_tile,但机车停在 39,32 未进 39,31;需确认 GOTO_COUPLE 的 dest_tile 赋值来源(机车排程 order 3=GOTO_COUPLE dest=0,疑似 dest 指向车底所在 station)。
