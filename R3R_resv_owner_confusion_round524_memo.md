# 第 524 轮：veh15 为什么依然铺不出整段预留（纯取证，零源码改动）

> 玩家原话：「注意到veh15依然没有整段铺设预留见build/R3R_debug.log，这是为什么」

## 一、输入与效度

| 项 | 值 |
|---|---|
| 日志 | `build\R3R_debug.log`，2 873 393 B，mtime 2026-11-26 22:34:01 |
| 产生者 | `build\openttd.exe`，mtime 2026-11-26 21:59:21，54 409 728 B（= 第 523 轮续四「纯刹车」版） |
| 判定 | 日志晚于 exe ⇒ **本日志已含刹车修复**，采样有效 |
| 场景 | veh15（三节机车链 15/16/17）在 `1455,1873` 等挂；目标车底 head=18，占 `1424..1437,1874` |

## 二、结论先行

**不是"没去订"，是"订不上然后整条回滚"。**

1. 刹车**已生效**：日志尾部 `CPL-S0-CHK veh=15 tile=1455,1873 … wait=45..62`，速度恒 `spd=0`、位置一格未动，`CPL-BRAKE-WAIT` 不再抖 0/2。等待语义成立。
2. 每 tick 仍在做挂接寻路与铺轨尝试：`RESV-BEGIN` → `TPR-WALK` → `EXTEND-BACKOUT`，以及 `CPL-ORIGIN/CPL-BEST/CPL-BACK/FSCP`。
3. **唯一卡点格 = `1447,1874`**：该格东向 `TRACK_X` 上残留了一个**无主预留**（`res=0x1`、`owner=-1`、`REACH-NONE`），而挂接路径要从 `1447,1873` 拐进该格的 `TRACK_UPPER`；X 与 UPPER 交叉 ⇒ `TryReserveRailTrack` 恒失败。
4. 失败后 `EXTEND-BACKOUT` 把本次已记的整条路径全部撤销（`n=1` / `n=2`），所以画面上**永远看不到整段黑色预留**——它每 tick 铺一次、每 tick 被自己拆掉。

## 三、取证（行号为 `build\R3R_debug.log`）

### 3.1 刹车已生效、位置钉死

```
:尾1 CPL-S0-CHK veh=15 tile=1455,1873 real=1 order=16 stuck=1 fp=0 prog=0 wait=49 …
:尾12 CPL-S0-CHK … wait=59
:尾37..39 CPL-S0-CHK … wait=60/61/62
```
`spd=0` 全程、`tile` 恒为 `1455,1873`。

### 3.2 每 tick 都在铺，但每 tick 都回滚

```
:2973 TPR-WALK veh=15 mf=1455,1873 origin=1447,1873 td=3 okay=0 exit=1 new=1447,1874 reach=4 spd=0 one=0 look=0
:2975 RESV-EXT-PRE veh=15 tile=1447,1874 enterdir=1 tracks=0x4 reserved=0x1 doRes=1 real=1 spd=0
:2976 RESV-HAVERES veh=15 tile=1447,1874 … res=0x0 tracks=0x4 … stuckretry=1 … nolook=1
:2977 RESV-PAIR-AIR veh=15 tile=1447,1874 track=2 kind=rail      ← 假放行，不写地图
:2984 UNRES-RAIL tile=1447,1874 track=2 phase=extend-backout actor=15
:2986 RESV-EXT-DEST veh=15 bad=1 tile=1455,1873 origin=1447,1873 otd=3 ookay=0 new=1447,1874 exit=1 tracks=0x4
:3031 EXTEND-BACKOUT veh=15 origin=1447,1873/3 stop=1447,1873/3 next=1447,1874 reason=reserve-fail err=0 owner=-1 whole=0 track=-1 need=0x0 resv=0x0 led=-1 n=1
:3091 EXTEND-BACKOUT … n=2
```

- `tracks=0x4` = 要订 `TRACK_UPPER`；`reserved=0x1` = 该格已有 `TRACK_X` 的预留。
- `need=0x0 resv=0x0`：走到这一步时 Extend 的记账里本格没有"我应订的位"，说明**前一跳就被 PAIR-AIR 当空气放过了**。

### 3.3 该 X 位"位在、认不出主人"

```
:3003 FSCP tile=1447,1874 fail=tryReserve td=2 want=2 type=1 tbits=0x5 res=0x1 plain=1 depot=0 st=0 self=0
:3004 FSCP-RES tile=1447,1874 tk=0 owner=-1
:3005 FSCP-REACH-NONE tile=1447,1874 (no chain reaches this tile along a contiguous reservation)
:3006 FSCP-CPL tile=1447,1874 veh=15 pos=1455,1873 dir=0 spd=0 end=1447,1873 endtd=3 look=0
:3110 / :3111 …
:3112 TRP veh=15 mf=1455,1873 real=1 type=16 dest=7677341 spd=0 … => ok=0 res=0
```

`self=0`（不认为是自己）、`owner=-1`（`GetTrainForReservation` 回溯不到车）、`REACH-NONE`（沿连续预留也回溯不到链）—— 三项互证：**这是一条孤儿预留**，不是"被活车占着"。

### 3.4 孤儿是哪来的：配对伙伴链开走时留下的

```
:1885 RESV-HAVERES veh=75 tile=1447,1874 enterdir=1 res=0x4 tracks=0x4 spd=74 real=3 order=1 …
:尾6  RESV-HAVERES veh=75 tile=1411,1871 enterdir=0 res=0x1 tracks=0x1 spd=80 real=2 order=6 …
```

- veh75 与 veh18 同属一条链（第 523 轮已证：`COUPLE-OK loco=75 rear=62 consist=18`）。
- `:1885` 时 veh75 在 `1447,1874` 一带**自己就有预留**（`res=0x4`）；日志尾部它已在 `1411,1871`、`spd=80` 向西驶离。
- 它离开后 `1447,1874` 的 `TRACK_X` 没人释放 ⇒ 位留在地图上，主人却随链断开 ⇒ `owner=-1`。

### 3.5 yapf 侧的 "booked=23" 是记假账

```
:2609 REACH cur=1447,1874 td=0 depth=13 trk=0 bits=0x5
:2633 CPL-RES-OK veh=15 total=23 booked=23 stop_safe=1 goal=1 tiles=…;1447,1874/0;…
:2743 RESV-PAIR-AIR veh=15 tile=1447,1874 track=0 kind=rail
:2754 CPL-RES-OK veh=15 total=19 booked=19 …;1447,1874/0;…
:2845 CPL-RES-OK veh=15 total=20 booked=20 …;1447,1873/3;1447,1874/2;…
:2872 RESV-PAIR-AIR veh=15 tile=1447,1874 track=2 kind=rail
```

`R3RReservationHeldByCouplePartner()`（`src\pbs.cpp:517-541`）对 plain rail 的语义是"该格每一个 held 位的 `GetTrainForReservation(tile,t)->First()` 都等于配对伙伴"⇒ 返回 true。而调用方 `TryReserveRailTrack()` 在 true 时**只打印 `RESV-PAIR-AIR` 然后 return true，不写地图**。

⇒ `CPL-RES-OK booked=23` 里包含"按空气放行"的格，**不等于地图上真订了 23 格**。yapf 内部路径成立、实际预留为空，正是"看着有路、实际铺不出"的来源。

## 四、机制串联（一句话版）

1. 伙伴链在 `1447,1874` 留下的 `TRACK_X` 位变成孤儿；
2. veh15 想从 `1447,1873` 拐进 `1447,1874` 的 `TRACK_UPPER`，与 X 交叉 ⇒ 真订失败；
3. `TryReserveRailTrack` 先试图用 `PAIR-AIR` 假放行：只要 `GetTrainForReservation` 还能认出伙伴就返回 true（不订位）；
4. 孤儿化之后认不出主人 ⇒ 假放行也失效 ⇒ `okay=0` / `bad=1` / `ookay=0`；
5. `EXTEND-BACKOUT` 把本次路径全撤 ⇒ 画面上没有整段预留；
6. 下一 tick 重复 2~5。刹车把"起点漂移"消掉了，但第 1 条的孤儿位一直在，所以**停多久都订不成**。

## 五、疑点（按"有疑点即落盘"登记）

- **REG-524-A**：同一 tick 内，`:2977` 的 `RESV-PAIR-AIR` 认定"该格 held 全是伙伴的"，而 `:3003-3005` 的 FSCP 认定"`owner=-1` / 无链可达"。两套判定对同一格给出相反结论。待查：`GetTrainForReservation` 是否被 `:2984 UNRES-RAIL track=2` 的"释放空气位"影响（若该位实际属于伙伴，则 R3R 回滚会打掉伙伴的预留链，这是**自毁式回滚**）。
- **REG-524-B**：全日志 `RESV-AUDIT-STRAY` / `RESV-REAP` / `RESV-AUDIT-SUMMARY` **0 条** ⇒ 孤儿 GC 在本会话从未运行（或从未判定该格 stray）。即"能查能清"的兜底在这条现场上没有生效，孤儿只增不减。
- **REG-524-C**：`CPL-ORIGIN-FOLLOW` 六个 trackdir 全 `ok=0 nt=-1,-1`（`:尾19-24`），而同一 tick 的 `TPR-WALK` 却能从 `1455,1873` 走到 `1447,1873`。两者对"能否离开起点格"给出相反结论，`nbits=0x1/0x14/0x100` 是失败时的未初始化残留值（探针缺陷）。需确认这两处 TrackFollower 的参数差异（skip_signals / vehicle 检查）。
- **REG-524-D**：`CPL-SAFE-FAIL veh=15 tile=1442,1874` 的位置在 `CPL-BEST 1424,1874` 与 `snap 1438,1874` 之间偏西，说明安全位回溯在 `1442,1874` 就已放弃；与 `1447,1874` 的孤儿位是否同源未证（`1442,1874` 未出现在任何 `RESV-*` 行）。

## 六、结论与建议

- **刹车不是问题**，它已经把"窗口漂移"消掉了；本轮现象是纯预留归属问题。
- 要恢复"整段预留"必须先让 `1447,1874` 的孤儿位被清掉，两条路：
  1. **治标（可立即做）**：让孤儿 GC 真正跑起来并覆盖 plain rail 的"位在、owner=-1"格（现在 0 条审计行，等于没跑）；或提供一次性清理命令。
  2. **治根**：查"伙伴链开走时为什么漏放 `1447,1874` 的 X 位"，以及 `ExtendTrainReservation` 回滚时对 PAIR-AIR 假放行的位执行 `UNRES-RAIL` 是否在释放别人的位（REG-524-A）。
- 另需单独修 yapf 记账：PAIR-AIR 返回 true 时不应计入 `booked`（否则 `CPL-RES-OK booked=N` 会系统性高估）。

## 七、复测判据（修后）

1. 同场景 `1447,1874` 的 `res` 变为 `0x0`，或 `FSCP-RES` 能给出 `owner=18|75`。
2. 出现一次成功的整段预留：`TPR-WALK … okay=1` 且其后无 `EXTEND-BACKOUT reason=reserve-fail`。
3. 画面上出现从 `1455,1873` 到车底前的连续预留预览。
4. `CPL-RES-OK` 的 `booked` 与地图实际持有位数一致（不再含 PAIR-AIR 假账）。
5. `RESV-AUDIT-SUMMARY` 至少出现一次且 `strayTiles` 归 0。

## 八、本轮未做

- 未改任何源码、未构建。
- 未验证 REG-524-A（UNRES 是否释放伙伴位）——需要在该格下断点级取数或加只读探针。
- 未查 `1442,1874` 的 `CPL-SAFE-FAIL` 是否由同一孤儿位导致。
