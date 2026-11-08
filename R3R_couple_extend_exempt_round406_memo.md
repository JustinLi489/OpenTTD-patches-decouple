# 第 406 轮（2026-11-08）：修法 A 落地 —— 驾驶侧 `ExtendTrainReservation` 会合点整格豁免

> 前置：第 405 轮纯取证（`R3R_couple_stuck_rootcause_round405_memo.md`、`R3R_KNOWN_ISSUES.md` 的「第 405 轮」小节）。
> 玩家口径：「我希望先按A做一下，C暂时搁置（如果没问题可能就不使用了）」⇒ 本轮**只做 A**，`led=` / `R3RReleaseReservationChecked` 的仲裁版（C）**未动**。

---

## 一、要解决的问题（一句话）

等待车底停在会合点站台格上，`SetRailStationReservation()` 把**整格**订走；挂接机车在 `ExtendTrainReservation()` 的 `choice-reserved` 处看到"整格有人"就执行**整条 trial 回滚**，连自己刚订的 `248,128/248,129` 一起撤掉 ⇒ 永远进不到会合点，`spd` 恒 2。

---

## 二、落地位置与判据（仅 `src\train_cmd.cpp`，未碰任何 `src\*.h`）

### 2.1 新边沿标签

`src\train_cmd.cpp` 的 `R3REDGE_*` 枚举（`R3REDGE_JUNCCHOOSE` 之后、`R3REDGE_COUNT` 之前）新增：

```cpp
R3REDGE_EXTGOALEXEMPT, ///< 第 406 轮 (REG-405-A): the choice refusal was waived -- the platform belongs to our locked couple partner
```

### 2.2 `ExtendTrainReservation()` 的 `choice-reserved` 分支

原结构：

```cpp
const uint32_t r3r_own_key = R3RResvActorID(v).base();
if (/* STALE-OWN-BIT 条件 */) { ... } else { break; }   // break = 整 trial 回滚
```

现结构：

```cpp
const uint32_t r3r_own_key = R3RResvActorID(v).base();
const Train *r3r_goal_pair = nullptr;
if (ft.is_station && v->current_order.IsType(OT_GOTO_COUPLE)) {
    const Train *const r3r_locked = R3RGetCouplePairPartner(v);
    if (r3r_locked != nullptr && r3r_whole_owner != nullptr &&
            r3r_whole_owner->First() == r3r_locked) {
        r3r_goal_pair = r3r_locked;
    }
}
if (r3r_goal_pair != nullptr) {
    if (R3RDbgEdge(R3REDGE_EXTGOALEXEMPT, ((uint64_t)r3r_own_key << 32) | (uint64_t)ft.new_tile.base(),
            (uint64_t)r3r_goal_pair->index.base())) {
        R3RDbgWrite("EXTEND-GOAL-EXEMPT veh=%u tile=%d,%d pair=%u whole=%d track=%d need=0x%X resv=0x%X\n", ...);
    }
    /* 当作普通 choice：落到 :16246 的跳格修正 → :16252 return PBSTileInfo(new_tile, old_td, false) */
} else if (/* STALE-OWN-BIT 条件（原样保留） */) {
    ...
} else {
    break;
}
```

### 2.3 四道判据为什么是这四道

| 判据 | 理由 |
| --- | --- |
| `ft.is_station` | 整格预留语义只出现在站台/路点（`HasStationTileRail`）。普通轨道的 `RESV-PAIR-AIR` 空气位豁免第 345 轮已在 `pbs.cpp` 的 `TryReserveRailTrack` 里做了，不需要在这里重复开口。 |
| `v->current_order.IsType(OT_GOTO_COUPLE)` | 豁免只属于"正在去挂车的机车"，不是通用通行证 ⇒ 满足负判据 4（普通列车遇别人占的站台仍 back-out）。 |
| `R3RGetCouplePairPartner(v) != nullptr` | 必须存在**双向配对锁**（`r3r_couple_target` / `r3r_couple_requester`）。锁保证对面确实在等这台车，而不是"随便一台车想穿别人的站台"。 |
| `r3r_whole_owner->First() == r3r_locked` | 整格主人必须**就是**配对目标所在的那条链（`First()` 做链级归一，避开车号在链中段造成的误判，即 U-405-2 的顾虑）。别人（含第三列车、含自己）占的格一律不豁免。 |

### 2.4 语义边界（刻意不做的）

- **不清任何位、不订任何位**：豁免分支里没有任何 `UnreserveRailTrack*` 调用，满足负判据 5（KI-479 防误清不可回退）。
- **不改回滚粒度**（修法 B 未做）：仅对"会合点整格主人 == 配对目标"这一种情形不 back-out；其余 `choice-reserved` 仍按原样整 trial 回滚。
- **不做 C**：`r3r_backout_ledger` 仍用非仲裁 `GetTrainForReservation()`；`R3RReleaseReservationChecked` 未动 ⇒ 日志里 `led=`/`owner=` 仍可能报别人（REG-405-B 保持"未修/诊断口径"）。
- **探针关闭也生效**：行为判断完全不依赖 `R3RDbgEdge` 的返回值（只用来决定是否打日志），`R3R_PROBES=0` 的发布树同样得到修复。

---

## 三、日志判读口径（复测时按这几行看）

| 行 | 含义 |
| --- | --- |
| `EXTEND-GOAL-EXEMPT veh=0 tile=248,133 pair=3 whole=1 track=1 need=0x16 resv=0x2` | 豁免命中：格 248,133 的整格主人属于配对目标 3 的链 ⇒ 不再 back-out。`R3RDbgEdge` 按 (veh, tile) 边沿去重，几何变化才会再出一行。 |
| 不再出现 `EXTEND-BACKOUT veh=0 ... reason=choice-reserved` + 紧随的 `CLEAR-RAIL tile=248,128 track=1 phase=extend-backout` | 判据 1。 |
| `RESV-EXT-DEST bad=0` 且 `new=` 前移到会合方向 | 判据 1/2。 |
| `COUPLE-OK loco=0 rear=...` | 判据 3。 |
| 其它（非挂接）列车遇别人占的站台仍出 `EXTEND-BACKOUT ... choice-reserved` | 负判据 4。 |
| 无 `CLEAR-RAIL` 指向别人的位 | 负判据 5。 |

---

## 四、构建自证

- 入口：复用既有 `R3R_fullbuild.cmd inc detach`（未新建任何 `.cmd`）；护栏判**增量安全**（只改 `.cpp`，无头文件/语言文件比最新 obj 新）。
- `build\R3R_incbuild.log`：`[2/3] Building CXX object CMakeFiles\openttd_lib.dir\src\train_cmd.cpp.obj` → `[3/3] Linking CXX executable openttd.exe` → `==== R3R build ended 2026/11/08 17:24:28.63 MODE=inc EXIT_CODE=0`，全文无 `error C` / `fatal error` / `FAILED:` / `build stopped`。
- `build\R3R_incbuild.done` 内容 = `EXIT_CODE=0`。
- 时间戳：`src\train_cmd.cpp` 17:02:16 → `build\CMakeFiles\openttd_lib.dir\src\train_cmd.cpp.obj` 17:22:47 → `build\openttd.exe` **17:24:20（52 524 544 B）** ⇒ obj 晚于源码、exe 晚于 obj。
- 字面量自证：`build\openttd.exe` 内 `EXTEND-GOAL-EXEMPT` ×1、`EXTEND-BACKOUT` ×2、`STALE-OWN-BIT` ×1。
- `read_lints`：`src\train_cmd.cpp` 0 条。
- 发布树（`build-release`）**本轮未编**，仍停在第 405 轮之前的版本。

---

## 五、待复测与未确认

- 判据 1~5 见 `R3R_KNOWN_ISSUES.md`「第 406 轮」小节，与第 405 轮 §八 同源。
- **U-406-1**：豁免命中后 yapf 侧 `TryReserveRailTrack` 在该站台格仍要能订成功（依赖第 345 轮的 `R3RReservationHeldByCouplePartner`）；若实测出现 `CPL-RES-OK booked<total`，说明 yapf 侧豁免也要按"整格"再补一道。
- **U-406-2**：`r3r_whole_owner` 为空（只有单轨位、无车停在格上）的会合点情形不豁免；本场景 `whole=1` 已覆盖，是否还有 `whole=0` 的会合点需要后续日志确认。
- **U-406-3**：`EXTEND-GOAL-EXEMPT` 连续多 tick 出现即表示"豁免了但仍走不到会合点"，那时问题已不在本层（转查 yapf 终点判定 / 配对锁），不要回头再改这里。

---

## 六、实机复测取证（2026-11-08，第 406 轮修法 A 生效判定）

### 6.1 采样有效性

| 项 | 读数 |
| --- | --- |
| `build\R3R_debug.log` | 1 886 行 / 205 999 B，mtime **2026-11-08 17:31:15** |
| 产生者 `build\openttd.exe` | **2026-11-08 17:24:20**（52 524 544 B）= 第 406 轮修法 A 版 |
| 结论 | 采样晚于构建 7 分钟 ⇒ **有效**，可作正判据 |

关键字计数（整文件）：`EXTEND-GOAL-EXEMPT` = **1**、`EXTEND-BACKOUT` = **0**、`choice-reserved` = **0**、`RESV-EXT-DEST` = **6**（全部 `bad=0`）、`COUPLE-OK loco=` = **2**、`CLEAR-RAIL` = **1**（`phase=controller`）、`UNRES-RAIL` = **1**（`phase=controller`）。

### 6.2 关键锚点（逐字）

- `:1365 RESV-EXT-PRE veh=0 tile=248,128 enterdir=1 tracks=0x2 reserved=0x0 doRes=1 real=3 spd=80`
- `:1366 EXTEND-GOAL-EXEMPT veh=0 tile=248,133 pair=3 whole=1 track=1 need=0x16 resv=0x2` —— **豁免命中**，格 `248,133`、配对目标 `3`、整格主人 `whole=1`；与备忘 §二 预期行**逐字一致**。
- `:1413 LOCO-MOVE veh=0 tick=3819 ... endTile=248,133 endTd=1 endPos=168 ...` —— 紧随其后预留终点由 `endTile=248,127`（`:1341`/`:1355`）**前移到 `248,133`**（前进 4 格）。
- 途中 `spd`：`:1341` 80 → `:1649` 71 → `:1674` 58 → `:1698` 49 → `:1712` 45 → `:1744` 24 → `:1751` 19 → `:1767` 5 → `:1859` 0（耦合后）。**无 `spd=2` 滞留段**。
- `:1609 / :1674 / :1751 RESV-FOREIGN-RES veh=0 tile=248,130 / 248,131 / 248,132 enterdir=1 res=0x2 tracks=0x2 spd=80/58/19` —— 会合点站台各格虽是"外来"整格预留，但**不再触发 back-out**，车持续前进。
- `:1793 CPL-GEO site=geo v=0 vn=2 vlen=2 u=3 z=3 ulen=2 rev=0 dx=0 dy=2 diff=2 need=2 spd=5 db=1` → `:1794 ARRANGE-IN` → `:1798 FOLDCHK COUPLE n=9 worst_gap=0 ... fold=0 dirfold=0 gap=0 ... spd=5 db=1` → `:1801 RESPACE-AFTER-EDIT couple head=0 px=0 stretch=0 unfixed=0 skip=0 front=0 passes=1 visited=8 capped=0` → `:1804 COUPLE-OK loco=0 rear=8 consist=3 co=1 real=0 type=17 tx=248 ty=132 x=3976 y=2112`。
- `:1810-1813 CPL idx=5..8 tile=248,133 gap=0 nom==len` —— 合并链尾正落在会合点站台格，几何正常。
- 另一次（场景入口、车库内）：`:258 COUPLE-OK loco=0 rear=8 consist=3 co=1 real=1 type=1 tx=250 ty=121`。
- **无** `EXTEND-BACKOUT`、**无** `choice-reserved`、**无** `CLEAR-RAIL ... phase=extend-backout`；唯一 `:339 UNRES-RAIL tile=250,121 track=0 phase=controller actor=0` 与 `:341 CLEAR-RAIL tile=251,121 track=0 phase=controller actor=0 head=0` 是出库后**自身 controller** 释放自己刚走过的轨，不涉别人。
- `:1416 RESV-ARB tile=251,123 station=0 a=veh3 wait=1 b=veh0 wait=0 picked=veh0` —— 归属仲裁仍按原逻辑工作（修法 C 未做，符合口径）。

### 6.3 判据结论表

| # | 判据 | 结论 | 依据 |
| --- | --- | --- | --- |
| 1 | 不再 `EXTEND-BACKOUT ... reason=choice-reserved`，且出现 `EXTEND-GOAL-EXEMPT` | **通过** | `EXTEND-BACKOUT`=0、`choice-reserved`=0；`:1366 EXTEND-GOAL-EXEMPT veh=0 tile=248,133 pair=3 whole=1`；`RESV-EXT-DEST` 6/6 `bad=0` |
| 2 | 预留终点前移到会合点、`spd` 不再恒 2 | **通过** | `endTile` 248,127 → **248,133**（`:1341`→`:1413`）；`spd` 80→71→58→49→45→24→19→5→0，无 `spd=2` 平台 |
| 3 | 出现 `COUPLE-OK loco=0 rear=...` | **通过** | `:1804 COUPLE-OK loco=0 rear=8 consist=3 co=1`（会合点）；`:258` 车库入口另一次 |
| 4 | 负判据：非挂接列车遇别人占的站台仍应 `EXTEND-BACKOUT` | **未覆盖** | 其它列车全部停驻（`SKIP-STOPPED veh=9/12/18/21/27/33`，`spd=0`），无"移动中普通列车遇站台外来整格预留"样本；`EXTEND-BACKOUT` 全程 0 行 ⇒ 既不能证真也不能证伪 |
| 5 | 负判据：不得出现清掉别人位的 `CLEAR-RAIL` | **通过** | 唯一 `:341 CLEAR-RAIL tile=251,121 track=0 phase=controller actor=0 head=0`，自身 controller 释放；无 `phase=extend-backout` 类释放 |

**总判定：修法 A 生效**（判据 1/2/3/5 通过，判据 4 因缺样本未覆盖）。REG-405-A 的现场症状（`spd=2` 永久停在 `248,127`、全程无 `COUPLE-OK`）**消失**。

### 6.4 旁证与观察项

- **U-406-1 已解决**：`CPL-RES-OK` 的 `booked/total` 正常收敛（`:1260 total=15 booked=1` → `:1334 total=13` → `:1407 total=11` → `:1632 total=1 booked=1 tiles=248,133/1`），最后只订到会合点格 ⇒ yapf 侧**无需**再补"整格"豁免。
- **U-406-2 未触发**：本场景会合点恒 `whole=1`，`whole=0` 分支仍无样本。
- **观察 O-406-A（不属本轮范围）**：耦合后 `:1815 ORD-AFTER-COUPLE head=0 n=5 real=0 ... borrowed=1 owner=3`、`:1828 settle-in real=1->0`、`:1850 loco-entry real=3->0` ⇒ 借用层索引在合并后被回零到 `WAIT_COUPLE(idx0)`，与 KI-04 / 第 353 轮 KI-527 家族同源，本轮**未动**，仅登记。
- **观察 O-406-B**：`:1826 CGRP-NORM-UNION head=0 nseg=2 mask=0` —— 合并链两段挂接分组并集为空，符合双方均无分组。
- 会话较短（1 886 行、约 4 300 tick），只覆盖"一次车库入口耦合 + 一次会合点耦合"；多列车争用同一会合点的压力场景未覆盖。

### 6.5 本轮未做

- 修法 B（回滚粒度）与修法 C（`r3r_backout_ledger` 仲裁化）**按玩家口径继续搁置**，未动 `led=` / `R3RReleaseReservationChecked`。
- `build-release` 发布树未编，仍停在旧版本。
