# 第 457 轮：`order_cmd.cpp:5952` 越界断言复发（KI-658）

> 玩家原话（2026-11-18）：「order_cmd.cpp 5952行断言崩溃了，崩溃日志见老地方」+
> 「剩余问题请说已定性处和需要拍板，取证处」。

## 一、现场（崩溃日志）

- 文件：`C:\Users\冯洁敏\Documents\OpenTTD\crash-20261118T133458Z.log`
  （本地 21:35:10 = UTC 13:35:10，133 796 B）。
- 版本串：`r3r-stable-2026-11-13-m (2)`，`Build date: Nov 18 2026 00:25:27`。
  **注意**：`Build date` 是 cmake 配置期常量，增量构建不刷新它，不能据此判定“跑的是旧 exe”；
  真正的判据是时间线 —— 本轮 exe `build\openttd.exe` mtime = **21:25:18**，
  崩溃日志里 `Game loaded at 21:27:05` → 崩溃 `21:35:10` ⇒ **跑的就是修完 KI-654 的新 exe**。
- 崩溃车辆：`veh 3: Train 2, vf:FTpa, tf:qLHSO, trk:0x01, tile 181x230 Station`。
- 断言：`assert(v->cur_implicit_order_index == 0 || v->cur_implicit_order_index < v->GetNumOrders())`
  现在在 `order_cmd.cpp:5952`（KI-654 那年是 5911，行号只因本轮补码而位移，判据一字未变）。

## 二、日志锚点（`build\R3R_debug.log`，11 077 行，末次写入 21:35:11）

关键两行（字段逐字）：

```
:11064 ORDIDX-OOB site=pre-processorders tag=- veh=3 impl=5 real=5 tt=5 num=5 eff=5 front=1 spd=2 parked=0 indepot=0 ticket=KI-472
:11065 ORD-IDX-WATCH site=pre-processorders tag=- veh=3 real=4->5 impl=4->5 tt=4->5 n=12->5 artic=0->0
        ord=00000159BAA347F0->00000159BAA32500 front=1 spd=2 parked=0 curType=0
        dest=4294967295 dtX=-1 dtY=-1 vtile=181,230
:11066 ORDIDX-OOB site=process-orders-bad tag=assert-4909 veh=3 impl=5 real=5 tt=5 num=5 eff=5 ...
```

同一 tick 内七个打点站点全部读到 `impl=5 num=5`：
`depotstay-in(:11052)` → `loco-entry` → `skipgate-in` → `post-skipgate` → `post-depotblk`
→ `pre-processorders` → `process-orders-bad`。共同读数：
`front=1 spd=2 parked=0 indepot=0 curType=0 dest=INVALID dtX=-1 dtY=-1 vtile=181,230`。

末尾两行（存档期）：`:11076 SEGSAVE-PARK veh=0 unit=1 seg=0 borrowed=1 prio=1`、
`:11077 SEGSAVE-PARK veh=3 unit=2 seg=0 borrowed=1 prio=1` ⇒ **veh 3 当时是借用方**。

## 三、定性结论（已成立）

1. **不是 KI-654 那条路径**：全日志 `ORD-RETURN-BK-CLAMP` = **0 条**（`Select-String -SimpleMatch` 统计），
   说明坏索引不是从 `R3RReturnBorrowedOrders()` 的 `orders_backup` 分支出来的。
2. 真实形态是「**换表不归一**」：
   - 换表前：`ord=...A347F0`，`n=12`，索引 4 —— 合法（12 条表，0..11）；
   - 换表后：`ord=...A32500`，`n=5`，索引 5 —— 越界（5 条表，0..4），`5 < 5` 为假 ⇒ 断言。
   ⇒ 索引是按**旧表**的合法范围带到**新表**上的，新表短 7 条，于是“合法”变“越界”。
3. 所以 KI-654 的修法只堵住了「归还 `orders_backup`」这一个入口；凡是**换 `orders` 指针**
   的其它入口（`R3RSyncDrivingOrders`、池层挂起/归还、单段化后重写段行、读档重演借用）
   仍可把越界索引带过去 —— 修法应该落在「换表」这个动作本身，而不是每条调用路径。
4. **换表的那一下抓到了（决定性锚点）**：
   ```
   :10930 ORD-RETURN-TO-POOL veh=3 pool=1 real=5 impl=5
   ```
   = `R3RReturnBorrowedOrders()` 的**池层分支**（`pool__orders` 挂起层，KI-623 那一块）。
   该分支原注释写着「池表的坐标并不单独存……所以这里只换指针，活索引一个字都不动」，
   于是 `real=5/impl=5` 原样留在 5 条池表上。
   **这个前提是错的**，而且与 own 分支 KI-472 记的是同一条教训：
   借用期被推进的恰恰是活字段（`ProcessOrders` / `IncrementImplicitOrderIndex` 直接写
   `v->cur_*_index`），它此刻是**耦合方那张 12 条表**的坐标，不是池表的坐标。
   ⇒ 池层分支就是本条崩溃的唯一入口（KI-654 修的 `orders_backup` 分支全程 0 命中）。

## 四、疑点（本轮新登记）

- **REG-457-A**：换表那一步索引为何是 `4 -> 5`，而不是 `4 -> 4`？
  若换表只换指针、不动索引，快照差分应为 `4 -> 4`；现在 +1，说明「换表」与
  `Increment*OrderIndex()`（或 `UpdateRealOrderIndex()`）的相对顺序可疑，
  也可能是换表前先推进了一次索引、随后换到短表。
- **REG-457-B**：`SEGSAVE-PARK` 里 `veh=0`/`veh=3` 的 `seg=0`（段 ID 未分配）却 `borrowed=1`，
  多段/借用态下段 ID 分配时机与 `R3RSettleChainSegments` 的调用顺序待查。
- **REG-457-C**：崩溃时 `curType=0`（`OT_NOTHING`）而索引非 0 —— 说明这列车当前没有“正在执行”的
  订单，索引纯属残留；这解释了为什么断言里 `== 0` 的特例救不了它（它是 5，不是 0）。

## 五、修法（已落地，仅 `src\train_cmd.cpp`）

### 5.1 主修：池层归还按池表条数归一（KI-658 正解）

`R3RReturnBorrowedOrders()` 的 `was_follower && chain->r3r_pool_orders != nullptr` 分支，
在 `InvalidateVehicleOrder()` 之前插入归一，与 own 分支（KI-472 的 8136-8167）逐字同构：

- 坐标来源 = 书签 `orders_backup_real_index` / `orders_backup_implicit_index`
  （耦合借用一律 `refresh=false`，且被 `R3RRefreshOrderBookmark()` 守卫挡住 ⇒ 它仍是
  「上池表时跑到哪」）；
- 归一 = `num == 0 || INVALID || >= num ⇒ 0`；
- 写入三元索引（`real` / `implicit` / `timetable` 同值）+ `DeleteUnreachedImplicitOrders()`；
- 探针 `ORD-RETURN-TO-POOL-CLAMP veh= num= real=a->b impl=a->b bk=a/b`。

选「宁可落 0 也不留越界」的理由：池表可能已被删短/重建，书签未必仍有效；
位置错最多多跑一条订单，越界值是必崩。

### 5.2 预防：采纳 owner 索引时同样归一

`R3RSyncDrivingOrders()` 在读完 `owner_real/owner_implicit` 之后加同一套归一。
理由 = KI-658 的同族风险：**owner 不被 tick**（只有链头进 `ProcessOrders`），
所以 owner 自己的索引可以长期停在「表被删短之前」的旧坐标上而无人发现；
一旦被 samelist 的 `inherit_progress` 分支或切表后的无条件赋值采纳，越界值就落到链头。
探针 `ORD-SYNC-CLAMP veh= owner= num= real=a->b impl=a->b inherit=`（仅在真的改动时打）。

### 5.3 构建自证（2026-11-18 21:44，MODE=inc）

入口 = `R3R_fullbuild.cmd inc detach`（复用既有脚本）；构建前 `tasklist` 已确认 openttd.exe 未运行。

| 项 | 读数 |
|---|---|
| `build\R3R_incbuild.done` | `EXIT_CODE=0` @ `21:44:09` |
| `src\train_cmd.cpp` | `21:38:26`，1 384 204 B（改前 1 380 930 B，+3 274 B = 两处 clamp） |
| `...\openttd_lib.dir\src\train_cmd.cpp.obj` | `21:41:38`，16 647 874 B（晚于源码 ✓） |
| `build\openttd.exe` | `21:43:52`，53 030 400 B（晚于 obj ✓） |
| `read_lints` | `train_cmd.cpp` 0 条 |
| exe 字面量 | 命中 `ORD-RETURN-TO-POOL-CLAMP`、`ORD-SYNC-CLAMP`、`bk=%d/%d` |

注：exe 字节数与上一版完全相同（53 030 400），是 MSVC 按 512 对齐填充的结果，
不能用来判新旧；判新旧只认 mtime + 字面量命中（本次三项字面量均命中 ⇒ 新代码确实进去了）。

### 5.4 复测判据（待玩家）

1. 同场景（耦合/解挂后车底回池）不再在 `order_cmd.cpp:5952` 断言；
2. 应出现 `ORD-RETURN-TO-POOL-CLAMP veh=N num=M real=5->0 impl=5->0 bk=..` 这类行，
   即 `real/impl` 被压回 `[0, num)`；
3. 负判据：正常的池表归还不出现 `ORD-SYNC-CLAMP`/`ORD-RETURN-TO-POOL-CLAMP`
   （索引本来就在范围内时一行都不打），运输行为逐字不变；
4. 若又崩，`ORD-SYNC-CLAMP` 与 `ORD-RETURN-TO-POOL-CLAMP` 都 0 命中 ⇒ 还有第四个换表入口
   （下一步查单段化/段行重写），不要去重查已被本 memo 排除的三个。


## 六、剩余问题分工（玩家第 2 问）

| 问题 | 已定性 | 需拍板 | 待取证 |
|---|---|---|---|
| P-456-1 断言崩溃（KI-654/KI-658） | 形态=换表不归一（旧表 12 条 → 新表 5 条，索引 4→5）；KI-654 修的那条路径已排除 | — | REG-457-A/B/C |
| P-456-2 全表灰显/「（自动填充）」后缀（KI-655） | 后缀与灰显同源（`order_gui.cpp:1086/1089-1090/1697`；`order_base.h:1973` 回绕语义）；缺口=ownerless 排程三条路径绕过补齐，已补 | — | 游戏内复测 |
| P-456-3 改装信息报错（KI-653） | 只排除了「改谓词」方向（G6-2 已证伪） | — | **需玩家**：报错弹窗原文 + GRF 名 + 触发操作 + 带 `BUY-REFIT*` 的日志 |
| P-456-4 重量/功率/TE ×3、容量 +1（KI-656） | 两层机制：override 层（打散是均分烘焙，override 在则守恒；override 是存档字段 ⇒ 排除读档丢失）+ CB36 逐车重算（打散从不写 `cargo_cap`）；显示端逐节累加不去重 | **是否在打散时把非组头成员 `cargo_cap` 显式置 0**（改了会让“每节都显示容量”变成“只有组头显示”，属策略） | —（数值不用再取，玩家已明确“读数就是三倍”） |
| P-456-5 veh=120 预留到不安全停车位（KI-657） | 同格再订（`RESV-TERM 218,138` → `RESV-BEGIN prevEnd=218,138`）；`RESV-UNSAFE-TAIL` 0 条；订的线是 `218,122→218,138` 竖 7 格，`unsafe=0`；车体在 `211,116`、订单目的地 `221,129` | **修法方向**：(a) 只查 `CPLRES origin` 语义后判定“离车 22 格的预留是否合法”；(b) 直接给“订位离车体太远”加上限；(c) 不动 | REG-456-E（`CPLRES` 的 `origin` 语义，是“从当前预留远端续订”还是别的） |
