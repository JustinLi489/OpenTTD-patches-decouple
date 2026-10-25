# 第 268 轮：解挂互锁「行为正常但显示不正常」（2026-10-21）

## 一、玩家原话（原样保留）

> 「我感觉行为算是正常了，但是显示没有正常，因为我没有看到黑黑的预留路径覆盖层和出站信号灯点绿」

输入日志：`D:\temp\codebuddy-dropped-files\d69a56c0-faa1-411d-8d34-8a5073d65b5e\R3R_debug.log`
314 889 B / 4 503 行 / 2026-10-21 19:24:38；
产生者 `build\openttd.exe` 52 036 096 B @19:19:34 = **第 267 轮 C-1 版**（本轮刚编译的那一个）⇒ 采样有效。

## 二、已确证的事实（本轮日志）

### 2.1 C-1 判据按设计工作（行为侧）

| 行 | 原文 |
|---|---|
| `:4134` | `DECOUPLE-INTERLOCK set head=21 follower=24 seam_car=47 tile=272,237` |
| `:4171` | `DECOUPLE-INTERLOCK wait head=21 follower=24 seam=272,237 seam_car=47 dist=0 res=0 age=0 left=0` |
| `:4219` | `DECOUPLE-INTERLOCK release head=21 follower=24 seam_car=47 dist=11 age=10 left=1 sig=1 steps=5 reason=signal-cleared` |

判读：
- `res=0`（age=0，解挂当刻预留失败）→ 10 tick 后 `release`；
- `sig=1 steps=5`：从后车 24 的车头沿其行进方向扫 5 格就命中了 PBS 信号机 ⇒ 两车之间**确实隔着信号机**；
- `left=1`：前车尾车 47 已离开站台格；
- **全日志 `DECOUPLE-INTERLOCK` 只有这 3 行，无 `timeout`、无 `hold`** ⇒ 放行来自 C-1 的信号判据，不是超时兜底。

⇒ **行为侧达标**，与玩家体感「行为算是正常了」一致。

### 2.2 显示侧的异常（待查）

玩家观察两点，均属**视觉**层：

1. 看不到黑色（PBS 冻结）预留路径覆盖层；
2. 出站信号灯没有点绿。

## 三、猜测根因（待验证，按玩家要求即时落盘）

- **G-1（首选）预留终点在出站信号机之前**：`TryPathReserve(consist, false, false)` 的语义是
  「铺到路径上的下一个**安全停车位**」，不是「铺过信号机」。若该安全位落在信号机之前
  （站台尽头/信号机前一格），则：①黑线止于信号机前 ⇒ 玩家看向前方看不到线；
  ②PBS 信号灯只有在「有列车预留**穿越**该信号机」时才变绿 ⇒ 灯不绿。
  与 `sig=1 steps=5` 并存不矛盾（信号机在 5 格之外，但预留没必要铺到那里）。
  待验证：日志里后车 24 在 4219 之后的 `TPR`/`RP cur=`/`RESV-WATCH` 行，看预留终点格。
- **G-2 站台整格预留不画黑线**：第 267 轮 §九 已登记「站台段走
  `SetRailStationReservation()` 整格位，黑色冻结预览只画 plain rail 位」。若后车此刻整段
  都在站台内，则即使有预留也**没有黑线可看**。待验证：`RESV-WATCH SET-STN/CLEAR-STN`
  与 `RESV-GHOST ... kind=station` 行的 tile 范围。
- **G-3 预留被即时清掉**：`R3RReleaseChainReservations()` / `RESV-GC` / `RESV-PURGE`
  在本 tick 内清掉了刚订的位。待验证：release 之后同 tick 是否有 `CLEAR-*`/`RESV-GC` 命中 24。
- **G-4 画面未刷新**（低概率）：引擎 `SetTrackReservation()` 自带 `MarkTileDirtyByTile`，
  除非路径根本没写进地图，否则该假设不成立。

## 四、下一组取证（按优先级）

1. `:4219` 起 40 行原文（看后车 24 起步后第一份 `TPR`/`CRT`/`RESVECONSIST`）；
2. 全日志 `TPR` 行中 `veh=24` 的行（origin/okay/td 值）；
3. 全日志 `RESV-WATCH`/`RESV-GC`/`CLEAR-*` 命中 24 或 265..272,237 的行；
4. `RP cur=` 行（yapf_rail.cpp 预留逐格 dump）中 24 的路径序列。

## 四点五、取证二（同日 19:40，已坐实的关键链条）

### 4b.1 后车的算术位置

| 行 | 原文（节选） |
|---|---|
| `:4169` | `TPR-BLOCKED veh=24 tile=273,237 spd=0 mf=273,237 origin=265,237 okay=0 td=0 other=21 otherTile=267,237 otherSpd=9` |
| `:4181` | `TPR-BLOCKED veh=24 … origin=264,237 … otherTile=264,237 otherSpd=79` |
| `:4183` | `TPR-BLOCKED veh=24 … origin=263,237 … otherTile=263,237 otherSpd=80` |
| `:4219` | `DECOUPLE-INTERLOCK release … sig=1 steps=5 reason=signal-cleared` |
| `:4294` | `CRT veh=24 order=1 dir=1 origin=260,237 td=0 found=1` |

⇒ 后车 24 **车头就在站台格 `273,237`**（站台为 `265..277,237`，13 格，第 267 轮 §14.3 已登记）；
它的预留终点 `origin` 被前车尾车一路顶着从 `265` 退到 `263`，release 之后重新 CRT 到 `260,237`。

**⇒ 后车此刻的预留区间（≈`263..273,237`）绝大部分落在站台格上。**

### 4b.2 两套预留字段互不相通（代码坐实）

- `src\station_map.h:595-601` `SetRailStationReservation()`：只 `AssignBit(_me[t].m6, 2, b)`，
  **不写 m2 的 track 预留位**；`HasStationReservation()` 也只读 `m6` bit2
  （`src\station_map.h:583-587`）；`GetStationReservationTrackBits()` 在有整格位时**凭空**返回
  该格全部轨道位（`:609-612`），是派生值而非存储值。
- `src\rail_map.h:225-232` `GetRailReservationTrackBits()`：只读 `_m[t].m2` 的 bit8-11；
  `SetTrackReservation()`（`:240-258`）同样只写 m2，且已被 KI-359 加了
  `SET-RAIL/CLEAR-RAIL` 观察钩子 —— 而站台格走的是 `SET-STN/CLEAR-STN`，
  两条钩子互不覆盖。

⇒ **站台整格预留 ≠ plain rail 预留**，是两个独立存储、两套 API。

### 4b.3 由此得到的结论（本轮定性结论）

玩家看不到黑线 + 灯不绿，**同源**：后车这一刻的预留几乎全在站台格上，
而站台整格位：

1. 不在 `GetRailReservationTrackBits()` 里 ⇒ PBS 黑色冻结预览（按 plain rail 预留位逐轨绘制）
   **没有东西可画**；
2. 是否参与信号机绿灯判定**尚未核实**（下一步唯一关键代码点：`GetReservedTrackbits()`
   是否把 `GetStationReservationTrackBits()` 并进来）。

⇒ 目前 G-2 已坐实一半、G-1 与 G-3 暂未成必要条件（预留确实存在，只是落在"画不出来"的字段里）。
G-4（画面未刷新）可以排除：`SetTrackReservation`/`SetRailStationReservation` 都改地图字段，
引擎绘制走 `MarkTileDirtyByTile`，与 R3R 无关。

## 五、复测判据（本轮修完用）

1. 前车越过出站信号机后，后车前方**应出现黑色预留线**直到其下一个安全停车位；
2. 若预留穿越出站信号机，该 PBS 信号灯**必须变绿**；
3. 站台内仍按引擎语义不画黑线（不得为了「看得见」把站台整格位改成 plain rail 位）；
4. 负判据：普通 PBS 列车（不涉及解挂）的预览与信号灯逐像素不变；
5. 行为侧不回退：仍须 `release … reason=signal-cleared`，不得退化到 `timeout`。

## 六、修法 A 落地（2026-10-21 晚，仅 `src\train_cmd.cpp`）

### 6.1 定性收口

- G-1（预留终点落在信号机之前）**成立**，并且是黑线与绿灯同时缺失的**同一根因**：
  黑色冻结预览画的是 `GetRailReservationTrackBits()` 的轨道位，PBS 信号点绿也是
  「这一格被本车订下」，两者同源。预留没铺到信号机那一格 ⇒ 两个症状必然同时出现。
- G-2 只解释「站台段没有黑线」这一半（站台整格位在 `_me[t].m6` bit2，不在 `m2`），
  与 G-1 不冲突，属引擎既有语义，**不修**。
- 现场铁证：`:4213 TRP veh=24 ok=1 res=1`（放行那一次预留只订到 264，263 还是前车尾车
  的）→ `:4219 release`，直到 `:4294 CRT veh=24 origin=260,237` 才补到 260。
  ⇒ 放行瞬间后车手里只有 263/264 两格普通轨道，被车身挡住、灯也没绿。

### 6.2 改动（三处，全部在 `src\train_cmd.cpp`）

1. `R3RSeamSignalState()` 增加两个出参 `sig_tile_out / sig_td_out`（默认 `nullptr`，
   既有调用点不受影响），命中信号机时把格子与 trackdir 带出来。
2. 新增 `static bool R3RSeamSignalReserved(const Train *consist, TileIndex tile, Trackdir td)`：
   纯只读，判据与绘制/灯色同源 —— 普通轨道看 `GetRailReservationTrackBits(tile)` 是否含
   `TrackdirToTrack(td)`（归属走 `GetTrainForReservation()`，它内部先查 R3R 账本，见 KI-361；
   走不到主人时按「已订」处理，宁放行不假等待）；站台格看 `HasStationReservation()`
   + `GetTrainForWholeTileReservation()`；隧道/桥梁/道口不参与判据。
3. 放行条件由 `sig_state != 0` 收紧为 `sig_state != 0 && (sig_state != 1 || sig_reserved)`：
   - 有信号机但没订到 ⇒ `hold … sigres=0 reason=signal-not-reserved`，下一 tick 的
     `TryPathReserve()` 随前车让路继续向前铺（就是 `:4294 origin=260` 那个过程）；
   - 铺到的**当 tick** 才放行，此时黑线与绿灯已经同时出现（`release … sigres=1`）。

### 6.3 为什么不是「强制续订到信号机之后」

试过的另一条路（放行时补一次 `TryReserveRailTrackdir(signal_tile)`）被否：
手订单格会造出 R3R 账本里没有的孤立预留位（KI-203/345/346 家族，`RESV-AUDIT-STRAY`
会把它当孤儿 reap 掉 ⇒ 黑线闪一下就消失），而且不进入 `path` 记账。用「等引擎自己铺到」
既满足判据、又不新增预留记账面的风险。

### 6.4 残留风险

- 若某场景下信号机永远不在后车的安全位之前（例如后车目的地就在信号机前的站台），
  会一直 hold 到 `R3R_DECOMP_WAIT_LIMIT = 3600` tick 兜底放行（`timeout`）。本轮判为
  可接受：既有超时路径未变，且该形态下"给后车看黑线"本来就没有意义。
- 归属判定若被 `GetTrainForReservation()` 判成别人（artic/借用边界），会表现为持续
  hold ⇒ 用日志 `hold … sigres=0 reason=signal-not-reserved` 可一眼区分。

### 6.5 构建自证（2026-10-21 19:41，内测树 `build\`）

复用既有 `_tmp_inc_build.cmd`（未新建任何 `.cmd`）；构建前 `tasklist` 确认 `openttd.exe` 未运行。

| 判据 | 实测 |
| --- | --- |
| 护栏 | `build\R3R_incbuild.guard.log` = `GUARD: incremental is safe (no header/lang file is newer than the newest object)` |
| 编译 | `[2/3] Building CXX object … src\train_cmd.cpp.obj` → `[3/3] Linking CXX executable openttd.exe`（Linking 命中 1 次） |
| 判决 | `build\R3R_incbuild.done` = `EXIT_CODE=0`（19:42:28），日志 1041 B |
| 错误计数 | `error C*` / `fatal error` / `FAILED:` / `build stopped` 合计 **0** |
| 时间戳 | `src\train_cmd.cpp` 19:36:46 → `train_cmd.cpp.obj` 19:40:03 → `build\openttd.exe` 19:41:45（52 036 096 B） |
| lint | `read_lints` 对 `src\train_cmd.cpp` = 0 条 |
| exe 字面量 | 6/6 命中：`DECOUPLE-INTERLOCK release`、`DECOUPLE-INTERLOCK hold`、`sigres=1`、`sigres=%d`、`signal-cleared`、`signal-not-reserved`（`no-signal-between` 亦命中） |

注：`reason=signal-cleared` 在 exe 内**搜不到整串**是正常的 —— 代码写作 `reason=%s`，`signal-cleared` 是独立字面量，必须分开核对（同上表）。

**发布树 `build-release\` 本轮未编**（按 2026-10-19 口径：日常只编探针内测版），仍停留在不含 KI-409 修法 A 的旧版本。

## 七、疑点 REGI-268-A：修法 A 的「等引擎自己铺到」其实是死等（2026-10-21 晚静态复核）

**结论句：修法 A 的放行条件「信号机格必须已被本车预留」在本场景下不可能成立，会把
第 267 轮已达标的行为（约 0.3 s 放行）退化成 3600 tick 超时放行，必须改判据。**

证据链（全部 `src\train_cmd.cpp`，只读）：

1. `:15883` `if (origin.okay && (moving_front->tile != origin.tile || first_tile_okay))`
   ⇒ 直接 `return TPRRF_RESERVATION_OK`（`:15893`）。即**预留终点已是安全位时，
   `TryPathReserve()` 不会向前延伸**，它只在 `origin.okay == false` 时才继续
   `ChooseTrainTrack()`。
2. `:15887-15890` 只有 `train_braking_model == TBM_REALISTIC` 才在该短路分支里补一次
   `TryLongReserveChooseTrainTrackFromReservationEnd()`；而
   `src\table\settings\game_settings.ini:299` 的默认值是 `def = TBM_ORIGINAL`
   ⇒ 默认设置下**不存在**任何自动延伸。
3. 即使走长预留分支，`:15050-15108` 的延伸也受 `HasLongReservePbsSignalOnTrackdir()`
   与 `if (!long_reserve) return;` 约束 —— 普通出站 PBS 信号机不是 LR 信号，延伸不成立。
4. PBS 的预留终点按定义停在「下一个安全停车位」，而出站信号机前一格**就是**安全位
   ⇒ 后车静止等待时，预留**结构上**永远停在信号机之前 ⇒ `R3RSeamSignalReserved()`
   恒为 `false` ⇒ `hold` 只能靠 `R3R_DECOMP_WAIT_LIMIT = 3600` 兜底。

**定性修正**：§6.4 当时把「信号机永远不在安全位之前」判为边缘场景，实际它是**常态**，
故本条从"残留风险"升级为"硬伤"，`sig_reserved` 不能作为放行的必要条件。

## 八、修法 B（改判据）：放行看「信号机对本车显示绿」

**结论句：把放行条件从「信号机格被本车预留」改为「该 PBS 信号机对后车显示 Green」，
它是玩家肉眼可见的那个"点绿"，且不可能死等（前车驶出 block 后信号机必然转绿）。**

- 取状态：`src\rail_map.h:611` `GetSignalStateByTrackdir(tile, trackdir)`，
  `trackdir` 传的就是 `R3RSeamSignalState()` 已经带出来的 `sig_td`（"面朝我们"的那个
  信号），返回 `SignalState::Green` 即放行。
- 放行条件（`src\train_cmd.cpp`，seam 分支）：
  `sig_state != 0 && (sig_state != 1 || sig_green || sig_reserved)`
  —— `sig_green` 是新增主判据，`sig_reserved` 保留为**或**项（预留真覆盖到信号机时
  黑线与灯同时成立，可提前当 tick 放行），两者任一成立即 `release`。
- `hold` 时日志补 `sigred=` 与 `sigres=` 两个读数，便于一眼区分
  「红灯（前车还在 block 里）」与「绿灯但预留没覆盖（观察窗口太短）」。
- 黑线（预留覆盖层）在等待期仍然只到信号机前一格 —— 这是 PBS 安全位语义（见 §七第 4 条），
  **不是 R3R 能改的**，也不该为它手工补订（§6.3 的孤儿位风险）。玩家侧可选口径：
  若必须在等待期看到长黑线，唯一正规途径是开启 `realistic braking`（`TBM_REALISTIC`），
  让引擎自己做长预留；否则等待期只能看到「后车→信号机」这一小段。

## 九、修法 B 构建自证（2026-10-21 19:51，内测树 `build\`）

复用既有 `_tmp_inc_build.cmd`（未新建 `.cmd`）；构建前 `Get-Process openttd` = 未运行。

| 判据 | 实测 |
| --- | --- |
| 护栏 | `GUARD: incremental is safe`（无头文件/语言文件晚于最新 obj） |
| 编译 | `[2/3] Building CXX object … src\train_cmd.cpp.obj` → `[3/3] Linking CXX executable openttd.exe` |
| 判决 | `build\R3R_incbuild.done` = `EXIT_CODE=0` |
| 错误计数 | `error C*` / `fatal error` / `FAILED:` / `build stopped` = **0** |
| 时间戳 | `src\train_cmd.cpp` 19:46:33 → `train_cmd.cpp.obj` 19:48:00 → `build\openttd.exe` 19:51:27（52 036 096 B） |
| lint | 0 条 |
| exe 字面量 | `signal-green` / `signal-red` / `siggreen=%d` / `DECOUPLE-INTERLOCK hold` / `DECOUPLE-INTERLOCK release` 全部命中 |

发布树 `build-release\` 仍未编（只编探针内测版）。

---

## 十、修法 B 已被实机复测证伪（2026-10-21 20:00，见第 269 轮备忘）

**结论句：本文 §八 的修法 B 不可用 —— 现场 64 条 `hold … siggreen=0 sigres=0 reason=signal-red`、
`release` 0 条，玩家原话「这回列车直接走不动了，又不铺预留」。**

- `GetSignalStateByTrackdir()` 对**静止后车**恒 `Red`（它没订进下一个 block），`sigres` 结构上恒 `false`
  ⇒ 两个或项同时恒假 ⇒ 永远等不来放行。
- 已改**修法 C**：只问"越过信号机之后那一格是否仍由配对前车持有"，不读灯色、不要求预留前伸。
  实现与构建自证见 `R3R_decouple_display_round269_memo.md` §五/§六，条目见 KI-409 的
  「2026-10-21 20:00 实机复测（修法 B 版 exe）」小节。
