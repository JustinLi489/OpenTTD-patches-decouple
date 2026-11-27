# R3R 第 526 轮 · 存档模块联机不同步审计（只读检查，未改源码）

- 日期：2026-11-27
- 触发：玩家「我需要你检查游戏的存档模块，主要防止游戏的联机游戏不同步问题」
- 本轮性质：**只读取证 + 登记**。未改任何 `src\*.cpp` / `src\*.h`，未构建。
- 代码基线：工作树 HEAD `0383af7f33`（v1.0.7 发布说明之后）

---

## 一、为什么"存档模块"决定了联机是否会不同步

OpenTTD 的联机不是"每帧传状态"，而是：

1. 服务端与客户端各自跑同一套确定性模拟，只同步**命令**；
2. 每帧比对 `_random.state[0..1]`（`src/network/network_client.cpp:324` `sync_err`），一不一致就判定 desync；
3. **中途加入的客户端拿到的是服务端当场生成的一份存档**，然后从存档那一帧继续跑。

由此得到本轮的唯一判据，后文全部围绕它：

> **凡是在服务端内存里已经"累积"、并且会改变模拟结果的状态，必须完整写进存档。
> 服务端没重开、客户端是"从存档起跑"，两者只要差一格预留/一次掉头/一条订单推进，就是 desync。**

注意：单机存读档"看起来正常"完全不能证明这一条——单机读档前后都是"重新起跑"，
而联机是"服务端继续跑 + 客户端从存档起跑"。

---

## 二、存档模块现状（R3R 自定义块）

R3R 在 `src/sl/couple_group_sl.cpp:341-347` 注册了 5 个块，全部挂在
`_couple_group_chunk_handlers`（`src/sl/saveload.cpp:361/413` 已注册进主表，未漏注册）：

| 块 | 类型 | 内容 | 结论 |
|---|---|---|---|
| `CGPP` | Table | CoupleGroup 池 | 正常 |
| `CGVR` | SparseTable | Vehicle::couple_groups，载入后 `AfterLoadCoupleGroups()` | 正常 |
| `R3VP` | SparseTable | 挂起日程/段身份/借用标记等 14 个字段 | 字段覆盖完整，见 §5.4 |
| `R3SG` | SparseTable | 段身份表（日程、单位号、名字、分组） | 正常，`ResetSaveloadData()` 有 `R3RSegmentTableReset()` |
| `R3DS` | SparseTable | 解挂缝互锁时间戳 | **读档字段错位，见 §3** |

已核对正确的部分（下一轮不要重复查）：

1. `_tick_counter` 本身入档：`src/sl/misc_sl.cpp:94`（`XSLFI_U64_TICK_COUNTER`），
   所以 R3DS 存"绝对 tick"、读档后用 `_tick_counter - tick` 算年龄是**成立**的。
2. `R3VP` 的 14 个字段与 `R3RHasParkState()`（`src/sl/couple_group_sl.cpp:163-179`）
   的 14 个判据**逐一对应，无遗漏**（漏一个就是"写了却没触发写"）。
3. `R3SG` 在 `ResetSaveloadData()`（`src/sl/saveload.cpp:3923-3924`）里清表，
   旧档无该块时不会残留上一局的段。
4. `R3VP`/`R3SG` 用表头驱动（`SlTableHeader`/`SlTableHeaderOrRiff`），
   旧档缺键按默认值读，不需要升 SLV。
5. `R3VP` 的指针走 `Ptrs_R3VP` 解析，`ORDL` 池覆盖"只被挂起指针引用"的表。

---

## 三、KI-693（高，代码级已确认）：R3DS 读档把"后车索引"当成"前车链头"

### 3.1 写入端

`src/train_cmd.cpp:2950` —— 表的键是**后车**：

```cpp
R3RDecompSeam &seam = _r3r_decomp_seams[follower->index];
seam.head = kept->index;      // 前车链头
seam.seam_car = seam_car->index;
```

`src/train_cmd.cpp:17541-17549`（`Save_R3DS`）：

```cpp
rec.key  = kv.first.base();   // = 后车索引
rec.a    = kv.second.head.base();   // = 前车链头
rec.b    = kv.second.seam_car.base();
```

### 3.2 读取端（错位）

`src/train_cmd.cpp:17568-17571`（`Load_R3DS`）：

```cpp
seam.head      = VehicleID((uint16_t)rec.key);  // ← 写的是后车索引，这里当成前车
seam.seam_car  = VehicleID((uint16_t)rec.b);
seam.seam_tile = TileIndex(rec.tile);
```

`rec.a`（真正的前车链头）**读出来后完全没有被使用**。

### 3.3 后果链

1. `R3RDecoupleInterlockWait()`（`src/train_cmd.cpp:3156`）取
   `Train *const leader = Train::GetIfValid(seam.head);`。
2. 读档后 `seam.head` = 后车自己的索引 ⇒ `GetIfValid` **必然成功**，
   于是"这条缝已经过期"的守卫（`:3169-3174`）**永远不会触发**。
3. 放行判据 `R3RSeamBeyondSignalHeldByLeader()`（`:3080`）里
   `leader_head = leader->First()->index` 变成"后车自己"，
   前车永远不可能被认作持有下一格 ⇒ 判据倾向"前车已让开" ⇒ 后车**被提前放行**。
4. 服务端记的是正确前车、仍在等；客户端读档后先开。
   **同一帧、同一张地图、两个不同的车头动作 ⇒ 联机不同步。**

单机也能观察到的近亲现象：解挂后存档、读档，后车起步时机与不存档时不同
（但不存档没人会去对比，所以一直没暴露）。

### 3.4 修法（一行，待玩家拍板）

`src/train_cmd.cpp:17569` 改为：

```cpp
seam.head = VehicleID((uint16_t)rec.a);
```

同时建议把 `rec.key` 显式标注为"表键=后车"、`a` 标注为"前车链头"，
避免下一次又把两者弄混。

---

## 四、KI-694（高，结构性）：只存了 1 张会话累积表，其余 10 张全部只活在内存里

R3DS 的设计说明（`src/train_cmd.cpp:17498-17517`）本身已经把问题定义得很清楚：
"加入者从空表起跑，服务端已经数了一阵子，两端从同一 tick 起判据不同"。
问题是**同一类表远不止 `_r3r_decomp_seams` 这一张**。

| # | 表 | 定义 | 门禁/消费点 | 对模拟的影响 | 读档后加入者的起点 |
|---|---|---|---|---|---|
| 1 | `_r3r_waiter_purged` | `train_cmd.cpp:11248` | `:19859` 首次判定 ⇒ `R3RReleaseChainReservations()` + `lookahead.reset()` | **直接改地图预留位** | 空表 ⇒ 首 tick 重跑"清理+重订" |
| 2 | `r3r_orphan_seen`（函数内 static） | `pbs.cpp:605` | `:606-614` 跨 2 tick 陈旧才 `UnreserveRailTrack()` | **直接改地图预留位** | 空表 ⇒ 首 tick 只记录，不清 |
| 3 | `_r3r_wbar_state` | `train_cmd.cpp:12771` | `:12838`；`:12888 if (bs.overshoot) return 0;` | 挂接寻路是否在等待区路点截断（返回 0/1/2）⇒ 预留长度不同 | 空 `seen_on_wp/overshoot` ⇒ 已越过的车被判"没越过"，重新截断 |
| 4 | `_r3r_couple_enter_tick` | `:8388` | `:8402` → `:8432 R3RCouplePairOutranks()` → `:12607` | 两台机车抢同一车底时的**优先权** | 空表 ⇒ 先到者优势消失，可能换一台车挂上 |
| 5 | `_r3r_couple_commit_fail` | `:13130` | `:13459`/`:13467`（`>= 4` 放弃） | 4 次失败后**跳过订单** | 空表 ⇒ 服务端已放弃、客户端再试 4 次 |
| 6 | `_r3r_couple_wait_heals` | `:13165` | `:20432-20436`（`>= 2` 停止自愈掉头） | **是否掉头** | 空表 ⇒ 客户端多掉头 2 次 |
| 7 | `_r3r_couple_autoreserve` | `:11230` | `:20414-20419`（装货中会重新入表，可跨 tick） | 跨 tick 的"补预留"请求 | 空表 ⇒ 该补的预留不补 |
| 8 | `_r3r_force_replan` | `:5260` | `:5278` 消费一次 | 一次性绕过 `first_tile_okay` 早退 ⇒ 是否真的找路 | 空表 ⇒ 少一次强制重规划 |
| 9 | `_r3r_just_reversed` | `:11274` | `:19879` | 掉头当 tick 的"让路"门禁 | 空表 ⇒ 当 tick 多做一次站台重订 |
| 10 | `_r3r_depot_self_release` | `:13664`（256 tick 窗） | `:13673` 判窗 | 把自己的库格预留当"不是障碍" ⇒ **能否出库** | 空表 ⇒ 出库闸门重新阻塞 |
| 11 | `_r3r_couple_partial_reserve` | `:13704`（512 tick 窗） | `:13714`/`:13724-13728` | 认定库口那格是自己的预留 ⇒ **能否出库** | 空表 ⇒ 自锁，每 37 tick 空转 |

其中 #1 #2 是**第一即发**的：它们改的是地图上的预留位，不需要时间窗，
加入后第一 tick 就可能两端不一样。

### 4.1 为什么列 #10 #11 特别值得注意

这两处的注释明确写了"**不动存档格式**"（`:13659-13663`、`:13700-13703`），
理由是"车一离开原格或超时，记录自动失效，不需要跨文件清理"。
这个理由在**单机**成立，在**联机加入**不成立：加入者读档时，服务端那条记录
还在 256/512 tick 窗内且仍在生效，客户端却是空表 ⇒ 同一列车在同一格，
服务端"视为自己的预留、放行"，客户端"视为别人的预留、阻塞"。

### 4.2 修法方向（供拍板，二选一）

- **方案 A（推荐，改动可控）**：把 R3DS 扩成"R3R 会话态块"。
  `kind` 字段已经在（`R3RDesyncStateRec::kind`，目前 0=seam、1=已废弃的孤儿戳），
  新增 `kind` 2..11，各带 `(veh, order_index, tick, counter, flags)`；
  `Load_R3DS()` 先**全部 clear**（现在只 clear 了 `_r3r_decomp_seams`），再按 kind 回填。
  `pbs.cpp` 的 `r3r_orphan_seen` 是函数内 static，需要在 `pbs.cpp` 里加一对
  `Save_R3ROrphanSeen()/Load_R3ROrphanSeen()`（声明放 `pbs.h` 或 extern 前向声明，
  避免动 `src\*.h` 触发全量重编）。
- **方案 B（长期更干净、周期长）**：把判据改成"可从存档已有状态重算"。
  例如 #1 的"首次清理"其实可以问 `R3RWaiterReservationIntact()`（已存在），
  #2 的"跨 2 tick"改成"该位无主 且 无任何链可达"的两段式确认。
  代价是每一处都要重新推演车底/站台现场，回归面很大。

短期不建议"服务端发完存档暂停 N tick"这种补丁：N 无法给准，且掩盖问题。

---

## 五、KI-695（中）：答案本缓存"命中"与"未命中"是否逐字等价，未验证

`src/pathfinder/yapf/yapf_rail.cpp:100-145` 的"答案本"
（`_r3r_answer_steps/_origin/_dest`）会把上一次 A* 的路径直接照抄来铺预留。
注释写"本表是纯运行时态：读档清空"，即认为清空无害。

但联机的要求比"无害"更强：**服务端命中缓存铺出的预留，必须与客户端未命中
重新 A* 铺出的预留逐格相同**。只要存在同代价多解、或缓存条目在
drop 时机之外变陈旧，两端就会铺出不同格。这一条目前没有任何对账手段，
登记的验证判据是：

1. 同一 tick 打印 `ANSWER hit=1/0` 与最终 `CPL-RES-OK tiles=` 清单；
2. 两端（命中/强制清空）在同一存档上跑同一段命令序列，比较预留位图逐格相同。

---

## 六、KI-696（低-中）：R3VP 的三个指针字段没有"池存活校验"

`R3SG` 在 `Save_R3SG()`（`src/sl/couple_group_sl.cpp:302-310`）里对
`rec->orders` 做了存活校验（KI-647），读档的 `Ptrs_R3SG()` 还会把越界索引清零。
`R3VP` 的 `orders_backup` / `r3r_follow_orders` / `r3r_pool_orders`
（`_r3r_park_desc`，`src/sl/couple_group_sl.cpp:122-160`）**没有任何对应校验**。
一旦某个挂起表先被销毁，保存时 `ReferenceToInt()` 会去读悬空对象的 `index`，
轻则写进一个不存在的池索引（读档 abort），重则读到垃圾。
与 KI-647 同族，只是没被同一轮覆盖。

---

## 七、复现与验收方法（下一轮照做）

1. **单进程往返**：存档 → 读档 → 立刻再存一次，两次 `R3DS`/`R3VP`/`R3SG`
   三个块的字节应逐字相同；不同即说明读档没有完整还原。
2. **联机判定**：服务端 `-ddesync=2` + 客户端加入，先看
   `src/network/network_client.cpp:324` 的 `sync_err` 出现在第几帧；
   出现在加入后 1~2 tick ⇒ 命中 §4 的 #1/#2；出现在几十 tick 后 ⇒ 命中 #3~#11。
3. **针对性判据（KI-693）**：解挂后立即存档 → 读档 →
   日志里 `DECOUPLE-INTERLOCK` 的 `head=` 必须等于解挂前那条 `head=`；
   若等于后车索引，即本条未修。

---

## 八、未确认项（登记，勿当结论）

- U-1：`_r3r_pair_verdict` / `_r3r_pair_scan_tick`（`:8341`/`:8362`）是否只影响
  当 tick 判定、且每 tick 必重算——若是，则不入存档也无害；未逐条走完。
- U-2：`_r3r_loop_memo`（`:14404`，60 tick 窗）只记忆"失败"，空表会让加入者
  多走一次遍历；是否可能得出不同结论未证。
- U-3：`_r3r_couple_depot_news` / `_r3r_dispatch_*` / `_r3r_*_news_shown` 属提示节流，
  推定无模拟影响，未逐条确认。
- U-4：`Save_R3SG()` 在"保存过程中"原地把 `rec->orders` 改成 nullptr
  （`src/sl/couple_group_sl.cpp:308-310`）。保存应尽量只读；本次未发现它改变
  模拟结果（只清已死指针），但属危险写法，留档。

---

## 九、补充取证（同轮追加）：这 10 张表**没有任何清空入口**

逐张核对 `xxx.clear()`：`_r3r_waiter_purged` / `_r3r_wbar_state` /
`_r3r_couple_enter_tick` / `_r3r_couple_commit_fail` / `_r3r_couple_wait_heals` /
`_r3r_couple_autoreserve` / `_r3r_force_replan` / `_r3r_just_reversed` /
`_r3r_depot_self_release` / `_r3r_couple_partial_reserve` **全部为 0**。
`ResetSaveloadData()` 只清了段表（`src/sl/saveload.cpp:3923`），`Load_R3DS()` 只清了
`_r3r_decomp_seams`（`src/train_cmd.cpp:17560`）。

因此存在**两个方向相反**的失效：

- **方向 1（加入者）**：服务端有、客户端空 ⇒ 首 tick 重跑"首次清理/首次记录"。
- **方向 2（同进程换档）**：客户端/单机进程里上一局的残留条目还在，
  而 `VehicleID` 会被回收复用（`_r3r_couple_enter_tick` 自己就有
  `it->second.owner != id` 的防复用检查，`_r3r_waiter_purged` 等却没有）⇒
  读入另一份存档后，本该发生的"首次"被**抑制**。
  这同时会打穿"存→读→再存，两次字节应当相同"的自检。

这一条也解释了为什么"单机存读档看着没问题"：单机读档后前后都是重新起跑，
只有把两台机器/两个进程并排比才看得到。

## 十、结论的置信度分层（供拍板时参考）

- **确定**：KI-693（R3DS 的 `key`/`a` 错位）——纯代码对照即可证明，与运行无关。
- **高**：KI-694 的 #2（`r3r_orphan_seen` 跨 tick 门禁）、#3~#11 的窗/计数类
  （#10 #11 的注释直接写明"不动存档格式"）——判据显式读这些表。
- **中**：#1（`_r3r_waiter_purged`）：它确实会执行释放与重订，但**净结果是否与
  服务端相同**取决于重订能否逐格复原（同一 handler 内先放后订，中间不插别的车）。
  未证；验证方法是解挂/停站现场存档，读档后比对 1 tick 内的预留位图。
- **未验证**：KI-695（答案本命中/未命中等价）、KI-696（悬空指针）。

---

## 十一、待玩家拍板（本轮未改源码）

- **C-1**：立即修 KI-693（`src/train_cmd.cpp:17569` 一行，`rec.key` → `rec.a`），
  然后按既有流程 `R3R_fullbuild.cmd inc`。只影响读档后的解挂互锁，不改变存盘格式。
- **C-2**：KI-694 走**方案 A**（扩展 R3DS 为会话态块，新增 kind 2..11，
  Load 先全清再回填；`pbs.cpp` 增加导出函数）。改动集中在
  `src/train_cmd.cpp` / `src/sl/couple_group_sl.cpp` / `src/pbs.cpp`，不动 `src\*.h`。
- **C-3**：KI-694 走**方案 B**（把判据改成可从存档状态重算），先做 #1 #2 两张
  改地图预留位的表，#3~#11 留待后续。
- **C-4**：暂不改码，先按 §7 做两个自检（同进程存→读→再存字节比对；
  解挂现场读档后 `DECOUPLE-INTERLOCK head=` 比对），用实测确认后再决定。

---

## 十二、第 526 轮落地（玩家拍板：全部修，只编内测版）

玩家原话：「我认为，可以直接对你所述的所有问题都执行修复（先不编译发行版，而只编译内测版）。同时，我们都知道，原生的发行版在遇到不同步问题的时候，会产出崩溃日志在Docs/OpenTTD里面，我们要保证现阶段R3R的不同步问题也能产出崩溃日志」

### 12.1 改动清单（只改 .cpp，未碰任何 `src\*.h`）

| # | 文件 | 改动 |
|---|---|---|
| KI-693 | `src\train_cmd.cpp` `Load_R3DS()` | `seam.head = rec.a`（原为 `rec.key`） |
| KI-694 | `src\train_cmd.cpp` | `R3RSessionStateReset()` 清 20 张表；`R3DS` 扩为 16 种 kind（0..15），覆盖解挂缝、waiter_purged、wbar、couple_enter、pair_scan、pair_verdict、commit_fail、no_target、wait_heals、autoreserve、force_replan、just_reversed、depot_self_release、partial_reserve、orphan_seen |
| KI-694 | `src\pbs.cpp` | `r3r_orphan_seen` 由函数内 static 提为文件级，新增 `R3ROrphanSeenClear/Count/Get/Put` |
| KI-694 | `src\sl\saveload.cpp` `ResetSaveloadData()` | 追加 `R3RSessionStateReset()` |
| KI-695 | `src\pathfinder\yapf\yapf_rail.cpp` | 联机时答案本既不写入也不回放（`_networking`），两端都走原生 A* |
| KI-696 | `src\sl\couple_group_sl.cpp` | `Save_R3VP()` 对三个挂起表指针做存活校验；`Ptrs_R3VP()` 解析前清越界索引（与 R3SG 的 KI-647 同族） |
| KI-697 | `src\train_cmd.cpp` | 新增 `R3RUpdateStateChecksum()`，把 R3R 同步量并入原生 `_state_checksum` |
| KI-697 | `src\openttd.cpp` | 在 `if (_networking)` 段（公司统计之后）调用 `R3RUpdateStateChecksum()` |
| KI-697 | `src\train_cmd.cpp` | 新增 `R3RWriteDesyncSummary()`，输出段表/挂起日程/会话表摘要 |
| KI-697 | `src\crashlog.cpp` `FillDesyncCrashLog()` | CheckCaches 段之后追加 R3R 摘要 |

### 12.2 为什么这样才能"产出崩溃日志"

原生的 desync 判定只有两条判据（`src\network\network_client.cpp:318`）：
`_random.state[0]` 和 `_state_checksum.state`。后者在 `src\openttd.cpp` 里只累加
tick/日期/公司资金/基础设施数量，车辆侧只有 `Train::Tick()` 的一格坐标与
`ChooseTrainTrack()` 的一格路径结论。

R3R 的分叉常常**不体现**在这些量上（段表里谁跑到第几条、挂起日程指向哪张表、
`r3r_segment_id` 归属、会话计数）。那种情况下坐标/资金/随机数可以逐字相同 ⇒
原生判据说"同步正常" ⇒ 不触发 `DesyncCrashLog()` ⇒ `Documents\OpenTTD` 里
什么都不会有。

所以本轮补了两件事：
1. **能检出**：`R3RUpdateStateChecksum()` 把上述量并入同一个校验和（顺序确定性
   由"有序容器直接遍历 / 无序容器先排序"保证；日志节流字段与纯探针表**不**参与，
   避免假阳性）。分叉于是表现为一次普通的 state checksum 不符，走原生
   `DesyncCrashLog()` → `Documents\OpenTTD\desync-client-<UTC>.log`。
2. **检出后看得见**：`R3RWriteDesyncSummary()` 把 R3R 现状写进同一份日志
   （在 CheckCaches 段之后），包含每台有 R3R 状态的车、段身份表、以及
   11 张会话表的规模与关键行。

### 12.3 已验证 / 待验证

- 待验证：编译（本轮启动的是 `R3R_fullbuild.cmd inc detach`；因 `src\*.h` 比 obj 新，
  护栏升级为全量）。
- 待验证：`ReadLints` 对 7 个改动文件报 0 条（但没有 C++ 语义检查能力，以编译为准）。
- 待验证：联机双端 `-ddesync=2`，R3R 状态人为分叉时应出现 `sync_err` 且
  `Documents\OpenTTD\desync-*.log` 内含 `R3R state:` 段。

### 12.4 构建自证（已编译）

- 首次尝试 `R3R_fullbuild.cmd inc`：沙箱拦 `reg.exe` ⇒ `vcvars64` 找不到 Windows SDK ⇒
  `yvals.h(22): fatal error C1083: 无法打开包括文件: "crtdbg.h"`，`EXIT_CODE=1`（23:10:57）。
  与既有记忆一致，改用"手动设 INCLUDE/LIB/PATH + 直调 ninja"。
- 第二次：`ninja -C build -j8 openttd.exe` → `[11/11] Linking CXX executable openttd.exe`，
  唯一告警是 `train_cmd.cpp(18079): warning C4189 "backup" 局部变量已初始化但不引用`
  （desync 摘要里漏用一个变量），已修。
- 第三次（修告警后）：`[2/3] src\train_cmd.cpp.obj` → `[3/3] Linking`，rc=0，无告警。
- 时间戳：`src\train_cmd.cpp` 23:33:04 → `build\openttd.exe` **23:34:12**（其余改动源码 23:07~23:09）。
- exe 字面量扫描（`grep -a -c`）：`R3R state:` / `session: seam=` / `seg id=` /
  `couple_enter veh=` / `commit_fail veh=` / `seam follower=` 全部命中 1。
- 树确认：`build\build.ninja` 不含 `R3R_PROBES_DEFAULT=0` ⇒ 内测树（探针 ON）；
  发布树 `build-release` 按玩家要求**未编**。
- 注：直调 ninja 不写 `build\R3R_incbuild.done`，本轮以 rc=0 + `[11/11]`/`[3/3] Linking`
  + exe 时间戳三者为准；`R3R_incbuild.done` 仍是 23:10:57 那次失败的 `EXIT_CODE=1`。

### 12.5 发行版编译时发现并清掉的探针残留（第 526 轮续）

`src\pathfinder\yapf\yapf_rail.cpp` 有两处 `f41dd84fa2`（探针剥离提交）漏掉的死代码，
它们没有任何输出，只维护**只增不减的 static map**，且都在预留热路径上：

1. `ReservePath` 成功尾部：`r3r_cplres_seen`（键 = 车索引，值 = 签名）+ `r3r_vtx/vty/odx/ody`
   （后两者未使用，发行版编译告警 C4189）；整个 if 分支恒为真但什么也不做。
2. 预留循环内失败分支：`r3r_rpf_seen` + `r3r_ntry/r3r_nok` 计数器，同样只写 map。

两处删除后行为逐字不变（返回值、回滚、答案本记录全保留），发行版编译告警从 1 条降为 0 条，
并去掉一处按车辆索引无限增长的静态容器。

---

## 十三、提交 / 发行版编译 / 打包（玩家要求：版本号仍为 R3R-v1.0.7，替换既有 GitHub release）

### 13.1 git

| 项 | 值 |
| --- | --- |
| 分支 | `feature/decouple` |
| 提交 1 | `eca2e718db` R3R: savegame/desync hardening (round 526)（9 文件，+1159/−41） |
| 提交 2 | `03fd851b04` R3R: drop two probe remnants left behind by the probe strip（2 文件，+12/−56） |
| 提交 3 | `65164941b7` docs: record the v1.0.7 rebuild (git, release build, package) |
| 版本标签 | `R3R-v1.0.7`（annotated，已用 `-f` 移到 `65164941b7`；`git describe --tags` = `R3R-v1.0.7`） |
| 工作区 | 干净（本文件的最终数字修正见 §13.5 的后续提交，**标签不再移动**） |
| push | **未执行**（沿用既有口径：玩家确认后再推） |

> 标签被移动 ⇒ 推送到 GitHub 必须 `git push --force origin R3R-v1.0.7`，
> 且要先删掉远端已存在的同名 release 资产再上传新包。

### 13.2 发行版编译

| 项 | 值 |
| --- | --- |
| 构建树 | `build-release/`（RelWithDebInfo，`R3R_PROBES_DEFAULT=0`） |
| 环境 | 沙箱拦 `reg.exe` ⇒ `vcvars64` 取不到 SDK，手动设 `INCLUDE`/`LIB`/`PATH` 后直调 ninja（同第 12.4 节） |
| 步数 | 增量：`[10/10] Linking`（首轮）→ 清死代码后 `[5/5] Linking` → 移到 `03fd851b04` 后 `[4/4]` → 最终标签提交后 `[3/3] Linking` |
| 告警 | 首轮 1 条（`C4189 r3r_vty`，探针残留）→ 清理后 **0 条**；`error C` 0 |
| 产物 | `build-release\openttd.exe` **2026-11-27 23:56:39**，**30 500 864 B** |
| SHA-256（exe） | `000b0ea4eb6befd05b2b2c93e6d3ddf8dcf625fb4376382292e709b37ef85d6e` |
| 版本串 | `rev.cpp`: `_openttd_revision[] = "R3R-v1.0.7"`、`_openttd_release_version[] = "0.73.5"`、`_openttd_revision_tagged = 1`、`_openttd_revision_hash = "65164941b7…"`（= 标签提交，二者一致） |
| exe 自证 | `R3R-v1.0.7` 命中 1 次；`R3R state:` / `session: seam=` 各 1 次；`R3R_debug.log` / `CPL-SCAN` / `FOLDCHK` / `LOCO-MOVE` / `CARGOWEIGHT` **全 0** |

### 13.3 打包

| 项 | 值 |
| --- | --- |
| 文件 | `R3R-v1.0.7-win64-2026-11-27.zip`（同名覆盖） |
| 大小 | **18 221 353 B**（旧 17 687 933 B） |
| SHA-256 | `2dbba0374d87eb462e47ebd81f587d6adeb0c11e84b380721bdb00af56cd0001` |
| 条目 | **142**（与旧包逐条同名，顺序一致） |
| 顶层目录 | `R3R-OpenTTD-0.73.5-win64\` |
| 完整性 | `ZipFile.testzip()` = None（OK） |
| 与旧包唯一差异 | `openttd.exe`：30 470 656 B → **30 500 864 B** |
| 旧包备份 | `build-release\R3R-v1.0.7-win64-2026-11-27.prev.zip` |

打包方式：**以旧包为基底**逐条复制，只替换 `openttd.exe`。这样保留玩家自己后来加进包里的
`R3R教程.pptx`（旧包 142 条 = 备忘里 135 条 + 该课件 + 目录项等），也保证 `lang/`、
`scripts/` 与上一版逐字一致。

### 13.4 内测树同步

`build\openttd.exe` 重编为 **2026-11-27 23:51:57**（53 302 784 B），与发行版同源（`03fd851b04`），
`warning C`/`error C` 均 0。

### 13.5 本文件最终数字的落地方式（避免"改文档 → 改标签 → 重编"的死循环）

发行二进制必须与标签提交一一对应，因此顺序只能是：**先定标签提交 → 再编 exe → 再打包 →
最后才写数字**。写数字本身又是一次提交，会改变 HEAD：

- 标签 `R3R-v1.0.7` **固定在 `65164941b7`**，就是 exe 内嵌的 `_openttd_revision_hash`，
  两者一致；
- §13.2/§13.3 的最终数字（exe `000b0ea4…`、zip `2dbba037…`）写在标签**之后**的一个文档提交里，
  该提交不再移动标签，也不重新编译；
- 因此在该文档提交上 `git describe --tags` 会显示 `R3R-v1.0.7-<n>-g…`，这是正常的：
  **发布资产对应的是标签提交 `65164941b7`，不是 HEAD**。重编前请先 `git checkout R3R-v1.0.7`。
