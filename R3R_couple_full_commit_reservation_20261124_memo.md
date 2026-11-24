# 挂接预留重构：改成「可执行的全程承诺」（2026-11-24，设计口径轮）

> 状态：**口径已定，未改源码、未构建**
> 本文记录玩家三条反驳的判定、代码证据锚点、修正后的阻力清单与待拍板项。
> 对应 KI：**KI-243 附记事实 7（"`CPL-RESERVE reserved=1` 不反映真实预留结果"的读数陷阱）
> 在本重构下结构性消失**；不新增条目，实施记录见 §五。

## 一、玩家三条口径与判定

| # | 玩家要点 | 判定 |
| --- | --- | --- |
| 1 | 死结1：让配对者的预留当空气，我认为可行 | **成立，且已实装一半**（第 345 轮 `RESV-PAIR-AIR`），我上一轮把它当成"未采纳的构想"是错的 |
| 2 | 死结2：对齐普通 = 和普通一样的行为；普通能穿站场进站，挂接凭什么不行 | **成立**，我上一轮"成功率塌成 0"的论证错误，收回 |
| 3 | 死结3：铺设者只负责承诺"开到某处"，后面交给列车自己滑行 | **成立**，"末格不可停"不构成死结，只构成清理责任 |

## 二、证据锚点

### 2.1 死结1 已被现有代码解掉 90%

- `src\pbs.cpp:492` `R3RReservationHeldByCouplePartner(TileIndex)`，注释原文：
  "The partner is by definition the train we are about to be joined with, so its
  reservation is air for us."，并写明动机 "the two halves of the same couple
  deadlock on each other"。
- 生效点 `src\pbs.cpp:551`（`TryReserveRailTrack` plain rail 分支失败后二次判定），
  命中打 `RESV-PAIR-AIR veh= tile= track= kind=rail` 并 `return true`——**不写地图**，
  伙伴保留所有权、日后自己释放。
- 同族 `src\pbs.cpp:559` `R3RReservationHeldBySelf`（第 368 轮 REG-359-L，自订当空气，
  日志 `RESV-SELF-AIR`）。
- 判据前置条件（决定它是否安全）：只认 `r3r_resv_actor`（寻路者本人）+ 链头当前是
  `OT_GOTO_COUPLE` + `R3RGetCouplePairPartner` 双向配对成立 + 只查 plain rail 与
  station/waypoint 轨道位。

**精确缺口**：`src\pbs.cpp:2829` `IsWaitingPositionFree()` 第一道闸
`if (TrackOverlapsTracks(reserved, track)) return false;` **没有**走 air 豁免。
这正是挂接必须靠 `unsafe_pos=true` 绕过**终点**判据的原因。

### 2.2 重构支点：`unsafe_pos` 一个布尔混管三件事

`src\pathfinder\yapf\yapf_rail.cpp:1015`
`TryReservePath(PBSTileInfo *target, TileIndex origin, bool unsafe_pos = false)`，
该参数当前同时控制：

| 行 | 行为 | unsafe_pos=true（挂接） | unsafe_pos=false（普通） |
| --- | --- | --- | --- |
| 1042 | 终点判据 | 跳过 `IsWaitingPositionFree` | 必须为空 |
| 1092 | 答案本 | 不记录 | 记录 |
| 1118 | 失败回滚 | **不回滚**（保留部分预留＝KI-563 A 分支） | 全撤 `UnreserveSingleTrack` |

⇒ 「变成可执行的全程承诺」= 把这个布尔拆成三个独立开关；挂接只改
`atomic=true` 与 `check_target_free = 空或伙伴`，`record_answer` 仍为 false（答案本为主寻路专用）。

### 2.3 收益：直接治 KI-680（玩家上一轮报的 P1 挂接侧）

`R3R_no_follow_reservation_round495_memo.md` §二 候选 B 已登记：
`:3276 CPLRES ... nTry=23 nOK=22` → `:3278 CPL-RET why=cpl-partial-res spd=160`，
即"站台段最后一格落空，但整段已铺到岔口前就放行" ⇒ 列车越过岔口后前方本就没有预留。
这正是部分预留放行的直接后果，原子提交后结构性消失。

## 三、修正后的真实阻力（4 条，全部可做）

### R1（硬）air 的格类型边界
`R3RReservationHeldByCouplePartner` 的 `switch` 只认 `TileType::Railway`（`IsPlainRail`）
与 `TileType::Station`（`HasStationRail`）；注释明写 "depot, level crossing, tunnel and
bridge reservations are left alone"。改成全程承诺后，挂接路径若经过道口 / 隧道桥 / 车库门口，
伙伴的预留在这些格上仍是硬冲突 ⇒ 原子提交下**永久失败**。
必须拍板：道口与 tunnel-bridge 的整格位是否纳入 air（车库位语义不同，可继续排除）。

### R2 air 不写地图 ⇒ 合并后的归属
`RESV-PAIR-AIR` 刻意不 `SetTrackReservation`。合并后 `First()` 改变，
`GetTrainForReservation(tile, track)` 解析出的 owner 会变成新链头——这**恰好**是需要的，
但必须验证合并提交点不会"重新订一遍"或"当作别人的位释放"（KI-203 / KI-345 家族）。

### R3 末格（contact tile）的清理责任
末格是列车永远不会单向往里开的一格（开进去即合链）。原子提交后它仍被订在机车名下，
**合并提交点必须释放或转交**，否则重演孤儿预留；另需保证列车滑行到末格时不会冲过预留
尽头（`TrainCheckIfLineEnds` 对这条路径的判据）。

### R4 抖动与失败形态（可接受，需实测）
原子提交后：任一格被第三方临时占用 ⇒ 整条撤 ⇒ 列车刹车 ⇒ 下一 tick 重订 ⇒ 再加速。
普通列车同构（只是路径通常更短）。行为一致，但挂接路径可能几十格，抖动更明显。
这不是死结，是"按普通同等对待"的必然代价。

## 四、已拍板（2026-11-24 玩家逐条回复）

| 编号 | 玩家原话 | 落定 |
| --- | --- | --- |
| R1 | 「把道口，桥隧纳入air即可」 | 纳入 |
| R2 | 「这算是一个需要搁置的内容」 | 搁置（air 不写地图 ⇒ 合并后归属） |
| R3 | 「同R2搁置，我暂时想不到问题」 | 搁置（接触格清理责任） |
| R4 | 「这是另外一个搁置的"配对锁解除的判据收紧"干的事情」 | 归入该项，本轮不做 |
| C1 | 「见R1」 | 道口 / 桥隧纳入 partner-air |
| C2 | 「你为什么会改原生的东西啊」 | **不改原生**，见 §4.1 |
| C3 | 「按你推荐的来」 | 删除 `unsafe_pos` 布尔，拆成三个显式参数 |
| C4 | 「按你推荐的来」 | 接触格释放挂 `Couple()` 合并提交点（但随 R3 搁置，见 §4.3） |

### 4.1 C2 的答复与由此确定的边界

原生 `ExtendTrainReservation()`（`src\train_cmd.cpp:17220`）**一行不动**。三条理由：

1. 它本来就是"逐格推进"：`CFollowTrackRail` 算出真实 `nd`，`TryReserveRailTrackdir` 失败就
   `break`，已订格不撤。这**恰好就是**"对齐普通"的形态，不需要改。
2. 挂接路径**依赖**它：`src\train_cmd.cpp:18409-18410` 的
   `r3r_couple_stuck_retry = IsType(OT_GOTO_COUPLE) && !GetCoupleIsDepot() && cur_speed == 0`
   就是为了让停住的挂接车继续走 `ExtendTrainReservation() + DoTrainCouplePathfind()` 重算路径
   （第 289 / 329 / 343 轮修 KI-484 的落点）。动它会复活那个死锁。
3. 于是原子性的**作用域被限定在 YAPF 段**（`TryReservePath()`），Extend 段保持原生非原子。
   ⇒「可执行的全程承诺」在挂接上的真实形态 = `Extend 段逐格推进 + YAPF 段一次提交`，
   与普通列车完全同构（普通也是 Extend 逐格 + YAPF 提交）。

### 4.2 R1 纳入 air 的技术前提（新发现，落码前必须知道）

道口与隧道桥的预留是**整格位布尔，没有 owner 记录**：

- `src\tunnelbridge_map.h:215` `HasAcrossTunnelBridgeReservation()` 只返回 `bool`，
  `SetAcrossTunnelBridgeReservation(t, bool)` 同样只有布尔；`IsTunnel` 分支走
  `HasTunnelReservation(t)`，也是布尔。
- 道口同理（`SetCrossingReservation(t, bool)` 布尔）。

⇒ 不能像站台那样"问 owner 再比伙伴"。可行办法是复用
`GetTrainForWholeTileReservation(tile)`（`src\pbs.cpp:2217`）：它先做"格上停车即主人"，
再沿每个被预留的 track 双向回溯 `R3RFollowReservationToOwner`。

据此纳入 air 的判据：

- `TileType::Road` 且 `IsLevelCrossing(tile)` ⇒ owner 取上式，为伙伴则 air。
- `TileType::TunnelBridge` 且 `GetTunnelBridgeTransportType(tile) == TransportType::Rail` ⇒ 同上。
- **保守边界（必须写进代码注释）**：隧道 mid tile 上通常没有车，`GetFirstVehicleOnTile` 取不到、
  回溯也可能取不到 ⇒ owner 为 nullptr ⇒ 判"不是伙伴"⇒ 该格仍按冲突处理。**不会撞车**，
  只是挂接会等在隧道口。这是刻意选择：宁可等，不撞。

### 4.3 最终实施清单（本轮只做 1~3，全部 .cpp ⇒ 可走增量）

| # | 文件 | 改动 |
| --- | --- | --- |
| 1 | `src\pbs.cpp` | `R3RReservationHeldByCouplePartner()` 加 `Road`(道口) / `TunnelBridge` 两个 case，owner 走 `GetTrainForWholeTileReservation()`；日志补 `kind=crossing|tunnelbridge` |
| 2 | `src\pbs.cpp` | `IsWaitingPositionFree()` 第一道闸加 partner-air 豁免（终点判据统一） |
| 3 | `src\pathfinder\yapf\yapf_rail.cpp` | `TryReservePath()` 的 `unsafe_pos` 拆成 `check_target_free` / `atomic` / `record_answer` 三个显式参数；挂接传 `true / true / false` |
| 4 | `src\train_cmd.cpp` | `Couple()` 合并提交点释放接触格预留 —— **随 R3 搁置** |

**风险登记（第 4 项搁置的代价）**：原子提交后，接触格变成"订者已合链、位无人释放"的孤儿，
与 KI-345 / KI-203 同源。若"配对锁解除判据收紧"一轮拖得太久，建议至少加一条
`RESV-GHOST` 兜底清扫（现有 `R3RReleaseChainReservations` 已能按 whole-tile 清扫，
只需确认接触格在清扫范围内）。

## 五、实施落地（第 503 轮，2026-11-24）

> 轮次口径更正：本备忘 §1~§4 的讨论与拍板发生在第 496 轮附近（备忘文件名沿用了当时的日期编号），
> 真正落库实施是在第 502 轮之后的第三次构建 ⇒ 统一记为 **第 503 轮**；`src\pbs.cpp`（2 处）与
> `src\pathfinder\yapf\yapf_rail.cpp`（3 处）的代码注释已同步为「第 503 轮」。

状态：**已改码 + 已编译（EXIT_CODE=0），游戏内复测待做**。只动 `src\pbs.cpp` 与 `src\pathfinder\yapf\yapf_rail.cpp`，未碰任何 `src\*.h`（走增量）。构建自证见 §5.5。

### 5.1 §4.3 清单 1+2：pbs.cpp

1. `R3RReservationHeldByCouplePartner()` 的 `switch` 新增两个 case（原只认 `Railway` / `Station`）：
   - `TileType::Road`：`IsLevelCrossing(tile) && HasCrossingReservation(tile)` 之后，用
     `GetFirstVehicleOnTile(tile, VehicleType::Train)` 反推 owner，判 `->First() == partner`。
   - `TileType::TunnelBridge`：`GetTunnelBridgeTransportType(tile) == TransportType::Rail` 且
     `HasAcrossTunnelBridgeReservation(tile)`，owner 同上。
   - 二者都**没有** owner 字段，所以这是"格上停车即主人"的推断，不是查询。
2. `IsWaitingPositionFree()` 第一道闸，原
   `if (TrackOverlapsTracks(reserved, track)) return false;`
   改为 `... && !R3RReservationHeldByCouplePartner(tile)`，终点判据与写侧统一。
3. 函数头注释同步改写（原文"depot, level crossing, tunnel and bridge reservations are left alone"
   已过时）。

**刻意保留的保守边界**：隧道内部格无车 ⇒ owner 推不出 ⇒ 判冲突 ⇒ 挂接等在隧道口。
宁可等，不撞。

4. **写侧补齐（R1 的真正落点，`TryReserveRailTrack()`）**：只改 `R3RReservationHeldByCouplePartner()`
   是不够的 —— 那个函数只被 `IsWaitingPositionFree()`（读侧，回答"这算不算冲突"）与
   `TryReserveRailTrack()` 的 plain rail / station 分支调用；道口与隧道桥分支原本是
   "`!HasCrossingReservation` / `!HasTunnelReservation` 才订，否则 `break` → `return false`"，
   伙伴订着就直接失败。故两个分支各补一段 partner-air（与第 345 轮的 station 分支同构）：
   - `case TileType::Road`：把原 `if (IsLevelCrossing(tile) && !HasCrossingReservation(tile))`
     展开成「`IsLevelCrossing` → `!HasCrossingReservation` 走原路」+「否则若 partner-air ⇒
     `RESV-PAIR-AIR ... kind=crossing` + `return true`（不写地图）」。
   - `case TileType::TunnelBridge`：在隧道头 / 桥头两次订位尝试之后追加 partner-air ⇒
     `kind=tunnelbridge` + `return true`。
   - 与 plain rail / station 一致：**不写地图**，位仍归伙伴，合链后由伙伴/清扫负责（R2/R3 搁置）。
   - `R3RReservationHeldBySelf()`（第 368 轮 REG-359-L 的自豁免）本轮**没有**往这两个分支加，
     避免顺手扩大范围；若复测出现"自己订着的道口挡住自己"，另立条目。

### 5.2 §4.3 清单 3：yapf_rail.cpp

`TryReservePath(PBSTileInfo*, TileIndex, bool unsafe_pos)` 拆成

```cpp
bool TryReservePath(PBSTileInfo *target, TileIndex origin,
                    bool atomic = true, bool check_target_free = true, bool record_answer = true)
```

旧单布尔的三项职责按下面这张表一对一派生，函数体内四处引用同步：

| 旧用法 | 新参数 | 落点（`src\pathfinder\yapf\yapf_rail.cpp`） |
| --- | --- | --- |
| `if (!unsafe_pos && !IsWaitingPositionFree(...)) return false;` | `check_target_free` | `:1058` |
| `if (!unsafe_pos) _r3r_answer_build.push_back(...)` | `record_answer` | `:1107` |
| `if (!unsafe_pos && res_fail_tile != INVALID_TILE) { 回滚 }` | `atomic` | `:1133` |
| 日志 `unsafe=%d` 的取值 | `(int)!atomic` | `:1126` |

唯一的行为变更在挂接摸路的那个调用点（`FindSafePositionOnSafePositionNodes` 之后的单段分支）：

```cpp
reserved = this->TryReservePath(nullptr, pNode->GetLastTile(),
        /*atomic*/ true, /*check_target_free*/ true, /*record_answer*/ false);
```
（`:2113-2114`）。旧写法是 `unsafe_pos=true`，即"不查终点 / 格级失败不回滚 / 答案本不参与"。

- 终点查得起来了：这条路上终点本来就压在伙伴身上，以前只能靠跳过判据绕开；现在靠 5.1 的
  partner-air 豁免放行，`check_target_free=true` 与写侧判据终于一致。
- 失败整段回滚：`CPL-RESERVE reserved=1` 从此必伴随 `nOK>0`（KI-505 计数器保留兜底）。
- 答案本仍不参与：挂接有自己的一套两遍法摸路，不能被主寻路答案本污染。

`RPFAIL` 的日志字段名保留 `unsafe=` 不动（下游可能有解析），只把值改成 `(int)!atomic`。

### 5.3 本轮刻意没做

| 项 | 原因 |
| --- | --- |
| R2「air 不写地图 ⇒ 合并后归属」 | 玩家拍板搁置 |
| R3「接触格由谁清」 | 玩家拍板搁置（同 R2） |
| R4 抖动 | 归入"配对锁解除的判据收紧"那一轮 |
| 清单 4（`Couple()` 合并提交点释放接触格） | 随 R3 搁置 |
| 原生 `ExtendTrainReservation()` | 玩家原话「你为什么会改原生的东西啊」⇒ 一行不动（§4.1） |

### 5.4 复测判据（6 条）

1. **伙伴订着的道口**：挂接车辆经过伙伴占着的道口时不再 `CPL-RES-FAIL`；日志出现
   `RESV-PAIR-AIR ... kind=crossing`（若该格确实走 `TryReserveRailTrack` 分支）。
2. **伙伴订着的隧道/桥头**：同上，`kind=tunnelbridge`；隧道内部格仍会失败（保守边界），
   但失败后应整段回滚并在隧道口等待，不再出现"半条路已订、车被送进未订分支"。
3. **第三方订着的道口/桥隧仍失败**：`other` 不是配对伙伴时必须保持 `IsWaitingPositionFree=false`
   / 预订失败，负判据，用来证明豁免没有放宽成"谁订都行"。
4. **`CPL-RESERVE reserved=1` 不再空心**：同一 tick 的 `RPFAIL nOK=` / `CPL-RESERVE nok=` 必须 > 0。
5. **`RPFAIL` / `CPL-RET` 的 `unsafe=` 在挂接路径恒为 0**；普通寻路本来也是 0，全程应为 0。
   若出现 `unsafe=1`，说明还有调用点漏改。
6. **挂接成功率不下降**：同一场景多次挂接仍能 `COUPLE-OK`；`atomic=true` 的代价是
   "能订一半也不再开出去"，表现为预留预览从"半条"变"没有"，可以多等几个 tick 但最终必须挂上。
   ——这条是 R4 抖动问题的对价，玩家已接受。

### 5.5 构建自证（2026-11-24 17:01）

共三次增量构建，最后一次覆盖了完整改动集（注释轮次修正 + 写侧 partner-air）：

| 次序 | 内容 | 结果 |
| --- | --- | --- |
| 1 | 读侧 partner-air + `TryReservePath` 三参拆分 | `[4/4] Linking`，`EXIT_CODE=0`（16:57:37），exe 16:57:19 |
| 2 | 注释轮次 `第 496 轮`→`第 503 轮`（5 处） | `EXIT_CODE=0`（17:00:30），exe 17:00:24 |
| 3 | **写侧** `TryReserveRailTrack()` 的 Road / TunnelBridge partner-air | `[2/3] pbs.cpp.obj` → `[3/3] Linking`，`EXIT_CODE=0`（17:01:18），exe **17:01:14** |

最终自证（以第 3 次为准）：

- 入口：`R3R_fullbuild.cmd inc detach`（复用既有入口，未新建任何 `.cmd`）。
- 护栏：`build\R3R_incbuild.guard.log` = `GUARD: incremental is safe (no header/lang file is newer than the newest object)` ⇒ 纯 `.cpp` 改动，增量合法。
- 编译日志：`build\R3R_incbuild.log` 尾部 `[2/3] Building CXX object ...\pbs.cpp.obj` → `[3/3] Linking CXX executable openttd.exe` → `MODE=inc EXIT_CODE=0`；错误计数 0。
- 判决：`build\R3R_incbuild.done` = `EXIT_CODE=0`。
- 时间戳链：`src\pbs.cpp`、`src\pathfinder\yapf\yapf_rail.cpp` → `pbs.cpp.obj` 17:01:08 → `build\openttd.exe` 17:01:14。
- `read_lints` 两文件 0 条。
- **产物字面量核对**：`findstr` 在 `build\openttd.exe` 命中 `RESV-PAIR-AIR`、`kind=crossing`、`kind=tunnelbridge` ⇒ 写侧新分支确实进了二进制。
  （`kind=` 的取值是 `%s` 实参，属独立字面量，可单独命中。）
- **坑记一笔**：本轮 exe 大小始终是 **54 364 672 B 一位不变**，尽管第三版多进了一千多字节代码 ——
  PE 段按对齐粒度填充，新增代码落进原有 padding 时文件大小可以完全不变。**判"有没有真链接"不能看大小，
  只能看 `[n/n] Linking` 行 + exe mtime + 字面量命中。**
- 状态：**已实现 + 已编译，游戏内复测待做**（判据见 §5.4）。

三处语义映射（与旧 `unsafe_pos` 逐项对应）：

| 旧（单个布尔） | 新（三个显式开关） |
| --- | --- |
| `!unsafe_pos` 控制 `IsWaitingPositionFree` | `check_target_free` |
| `!unsafe_pos` 控制 `res_fail_tile` 回滚 | `atomic` |
| `!unsafe_pos` 控制答案本 | `record_answer` |

调用点：
- 挂接摸路 `TryReservePath(nullptr, pNode->GetLastTile(), true)` →
  `(nullptr, ..., /*atomic*/true, /*check_target_free*/true, /*record_answer*/false)`。
  **这是本轮唯一改变行为的调用点。**
- 其余两处（`dont_reserve || TryReservePath(nullptr, ...)`、`TryReservePath(target, ...)`）
  走默认全 true，行为不变。
- 日志字段 `unsafe=%d` 保留原名（下游解析脚本可能依赖），值改为 `(int)!atomic`。

### 5.3 未做（按玩家口径搁置）

- §4.3 清单 4（`Couple()` 合并提交点释放接触格预留）：随 R3 搁置。
- R2（air 不写地图 ⇒ 合并后 owner 变成新链头）：搁置，无动作。
- R4（抖动）：归入"配对锁解除的判据收紧"。
- 原生 `ExtendTrainReservation()`：一行未动（见 §4.1）。

### 5.4 复测判据

1. 挂接路径经过的道口 / 桥隧若被**伙伴**订着 ⇒ 不失败（不再 `fail=tryReserve`）。
2. 挂接路径经过的道口 / 桥隧若被**第三方**订着 ⇒ 仍失败，车停等在道口 / 隧道口外。
3. 挂接不再出现"订了一半就转弯"：`CPL-RESERVE reserved=1` 必须伴随真实成功格数 > 0
   （即 KI-243 附记事实 7 的读数陷阱消失）。
4. 日志 `CPL-RET` / `RP` 的 `unsafe=` 字段在挂接路径恒为 `0`。
5. 负判据：普通列车寻路、正常到站、车库寻路逐字不变（三处调用点行为未动）。
6. 挂接成功率不因 atomic 下降。若下降，说明原先依赖"半条路径"才挂上，
   那部分是 §4.1 的 Extend 段，按 R4 处理。

### 5.5 风险

`atomic=true` 使挂接在任意一格被第三方临时占用时整段回滚 + 下一 tick 重试 ⇒ 抖动频率
高于原本"半条路径也能走"。这是玩家在 R4 明确接受的对价（"按普通同等对待"）。

## 五、未确认

- **U-1**：`IsWaitingPositionFree` 加 partner-air 后，`PfDetectDestination`
  （`src\pathfinder\yapf\yapf_destrail.hpp:186-190`）是否也需同一豁免（它服务普通寻路，
  挂接走另一条 destination 判据）。
- **U-2**：站台整段 `ReserveRailStationPlatform` 在原子提交失败时的回滚粒度。
