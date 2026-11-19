# 第 460 轮：KI-653 实证钉死 + KI-656 第 458 轮修法对该列车「结构性无效」的取证

> 玩家原话（2026-11-18）：
> 「我是找不到不会弹窗的火车的。然后我走了一遍KI-656，依然发现"能载客的机车""三倍数据的列车"KI-657我有点力竭，所以没测试」
>
> 输入材料：`build\R3R_debug.log`（4629 行，2026-11-18 23:08），由 `build\openttd.exe`
>（2026-11-18 22:27:57，53 030 400 B，第 459 轮版）所在目录产生 —— 但**内容疑为第 458 轮 exe**
> 所写，见 §四 REG-460-A。

## 一、证据原文（行号 = `build\R3R_debug.log`，已抽到 `build\r3r_460_probe.txt`）

| 行 | 原文 |
|---|---|
| 9 | `BUY-REFIT eng=516 veh=0 n=3 pn=3 pu=00000001 pi=00000001 ru=00000001 ri=00000001 more=1 dflt=00000001` |
| 10 | `BUY-REFIT-PART i=0 eng=516 cargo=0 cap=0 mask=00000001 pcap=0` |
| 11 | `BUY-REFIT-PART i=1 eng=516 cargo=0 cap=1 mask=00000001 pcap=0` |
| 12 | `BUY-REFIT-PART i=2 eng=516 cargo=0 cap=1 mask=00000001 pcap=0` |
| 13-15 | `BUY-REFIT-PP i=0/1/2 eng=516` |
| 19 | `MAKESEG UPGRADE-BEFORE-SPLIT head=0` |
| 20 | `  UPGRADE-BEFORE-SPLIT veh=0 p=-1 n=1 subtype=0x09 bits(front=1 wagon=0 engine=1 freeW=0 artic=0) rail(AH=0 AM=0 SF=0 SB=0) eng=516 ov=65535 rw=138 pw=138 w=138` |
| 21 | `  UPGRADE-BEFORE-SPLIT veh=1 p=0 n=2 subtype=0x02 bits(... engine=0 ... artic=1) rail(AH=0 AM=0 SF=0 SB=0) eng=516 ov=65535 rw=138 pw=138 w=0` |
| 22 | `  UPGRADE-BEFORE-SPLIT veh=2 p=1 n=-1 subtype=0x02 bits(... artic=1) ... pw=138 w=0` |
| 23 | `MAKESEG-WEIGHT UPGRADE-BEFORE-SPLIT head=0 n=3 sumw=138 sumpw=414 cw=138 dmw=138` |
| 25 | `DEARTIC-SPLIT head=0 n=3 wtot=138 wq=46 wr=0 ptot=4077 pq=1359 wagon=0` |
| 26 | `DEARTIC-PART head=0 i=0 idx=0 art=0 gm=0 gh=1 eng=1 ov=46 rw=138 w=46 cargo=0/0` |
| 27 | `DEARTIC-PART head=0 i=1 idx=1 art=0 gm=1 gh=1 eng=1 ov=46 rw=138 w=46 cargo=0/0` |
| 28 | `DEARTIC-PART head=0 i=2 idx=2 art=0 gm=1 gh=0 eng=1 ov=46 rw=138 w=46 cargo=0/0` |
| 29-34 | `MAKESEG UPGRADE-AFTER-SPLIT head=0` + 三节均 `subtype=0x08 bits(engine=1) rail(AH=1/…) eng=516 ov=46 rw=138 pw=138 w=46` |
| 35 | `MAKESEG-WEIGHT UPGRADE-AFTER-SPLIT head=0 n=3 sumw=138 sumpw=414 cw=138 dmw=138` |
| 38-40 | `SEGLOAD-ROW id=1/2/3 u=1/2/3 n="" g=65534 borrowed=0 orders=0` |

## 二、KI-653：H-653-A **成立**（弹窗根因钉死）

- `more=1` 而 `ru==pu`、`ri==pi` ⇒ **不是 refit 掩码不等**，唯一命中项是 `carries_more`。
- `carries_more` 判据（`articulated_vehicles.cpp:372-377`）= 「实车某节 `cargo_cap>0` 的货种，在购买侧
  `GetCapacityOfArticulatedParts()` 里容量为 0」。
- 现场逐节对账：实车 `cap` = 0 / **1** / **1**，购买侧 `pcap` 全 0 ⇒ 第 1、2 节各贡献一个
  「购买模板里根本没有的容量」⇒ 弹窗必然触发。
- 这两节正是**非组头的铰接部件**（`i=1`、`i=2`，`subtype=0x02 artic=1`）。父车（组头）`cap=0`。
  ⇒ 与 KI-641「打散/保留部件容量」同源：CB36 `DetermineCapacity()` 给部件算出 1，
  而购买侧 `GetCapacityOfArticulatedParts()` 走的是**原型默认值**（0）。
- **结论**：KI-653 不再是「待判定」。玩家「找不到不会弹窗的火车」的日志解释 = 只要该 GRF 的部件
  原型默认容量为 0 而 CB36 实算非 0，就 100% 弹窗，与玩家操作无关。

## 三、KI-656：第 458 轮修法对本现场 **结构性无效**

### 3.1 修法门禁（`src\train_cmd.cpp:632-645`，第 458 轮）

```632:645:src/train_cmd.cpp
			const Train *ghead = u->Previous();
			while (ghead != nullptr && ghead->IsArticGroupMember() && !ghead->IsArticGroupHead()) ghead = ghead->Previous();
			if (ghead != nullptr && ghead->IsArticGroupHead() && ghead->cargo_cap > 0) {
				...
				new_cap = 0;
			} else if (new_cap == 0 && u->cargo_cap > 0) {
				new_cap = u->cargo_cap;   // ← KI-641 兜底：保留旧值
			}
```

### 3.2 现场为什么走兜底

- 现场组头（`veh=0`）`cargo_cap = 0`（`BUY-REFIT-PART i=0 cap=0`，打散前后一致）。
- ⇒ 第一个分支要求 `ghead->cargo_cap > 0` **不成立** ⇒ 落到 `else if` ⇒ `new_cap = u->cargo_cap = 1`
  ⇒ 成员容量被**原样保留**，`CAP-MEMBER-ZERO` 一次都不打。
- 日志实测：`CAP-MEMBER-ZERO` **0 条**、`CARGOWEIGHT-VEH` **0 条**（4629 行全量计数）⇒ 与代码推断一致。

### 3.3 于是玩家看到的两件事都成立且必然

1. **「能载客的机车」**：打散后 `veh=1`、`veh=2` 变成 `engine=1`（假引擎），`cargo_cap` 仍是 1
   ⇒ 详情/列表里显示「引擎 可载 1」。
2. **「三倍数据的列车」**：`sumpw=414 = 138×3`，**打散前也是 414**（行 23 与行 35 逐字相同）
   ⇒ 三倍**不是打散造成的**，是「逐节功率累加」这一口径在真 artic 阶段就已经三倍。
   `cw=138`（整组缓存重量）与 `sumw=138` 在打散前后均守恒 ⇒ 重量侧没有三倍。

> 注意：`DEARTIC-PART` 的 `cargo=%u/%u` 打印的是 **`cargo_type`/`cargo_subtype`**，不是容量
>（`src\vehicle_cmd.cpp:580-586`）⇒ 行 26-28 的 `cargo=0/0` **不能**用来判容量，上一轮备忘里
> 若曾据此推断「容量已归 0」，那是误读，本轮订正。

## 四、疑点登记

- **REG-460-A（高优先，影响"修法是否触发"的取证）**：`CONSIST-CHK` 在 4629 行日志里 **0 条**，
  而同函数内的 `BUY-REFIT` 有 7 条 ⇒ 我第 459 轮新增的无条件探针**没有运行**。
  最可能原因 = 玩家这一轮复测用的仍是**第 458 轮 exe**（`openttd.exe` 22:04:30 那版），
  第 459 轮 exe 22:27:57 虽已编好但他没换（或换了但日志是旧的追加段）。
  影响范围：只影响「CONSIST-CHK / vTile-oSrc 是否可用」，**不影响** §二、§三 的结论
  （`BUY-REFIT*` 与 `CAP-MEMBER-ZERO` 两版代码相同）。
  待办：请玩家确认本轮用的 exe 时间戳，或用新 exe 重跑一次只看 `CONSIST-CHK` 有没有出现。
- **REG-460-B（中）**：`DEARTIC-SPLIT` 的 `ptot=4077 / pq=1359` 与同轮 `pw=138` 不自洽
  （`ptot` 像是 `RailVehInfo(engine)->power` 侧的量，`pw` 是 `cached_power`）。
  待查：详情窗口里「功率」到底取哪个字段，才能把「三倍」锚到具体数字。
- **REG-460-C（低）**：`SEGLOAD-ROW` 只有 id=1/2/3 三行且 `u=1/2/3`，但 `SEGTR-SNAP tag=load`
  只列出 `i=1 id=0 … ctrl=1` 一行；`ORD-IDX-WATCH` 的 `dest=4294967295 / curType=0 / inDepot=1`
  ⇒ 读档后段行/控制段口径与打散后的 ★ 分布是否一致，待核。

## 五、候选修法（待玩家拍板，均未落地）

| 方案 | 做法 | 运力影响 | 备注 |
|---|---|---|---|
| **3（推荐）** | 组头 `cap==0` 且成员 `cap>0` 时，把成员的容量**搬到组头**再把成员置 0 | **不变** | 一次性消除「能载客的机车」，且总量守恒；只改 `ConsistChanged()` 一处 |
| 1 | 去掉 `ghead->cargo_cap > 0` 门禁，成员无条件置 0 | **减少**（1 单位凭空消失） | 会削运力，除非同时确认这 1 是 GRF bug |
| 2 | 打散命令层面把整组容量集中到组头 | 不变 | 只覆盖「设为段」路径，覆盖不到读档/克隆/耦合 |

推荐 3 的判据：
1. 同一列车设为段后，`veh=1/2` 的 `cargo_cap` 归 0，`veh=0` 变成 2（若原型允许）或保持 0 但成员不显示可载客；
2. `MAKESEG-WEIGHT` 的 `cw`（整组容量）与打散前一致；
3. 负判据：原生 GRF 铰接车（不打散）容量逐字不变；
4. 造车弹窗应随之消失（`carries_more` 不再成立），可同时收掉 KI-653。

## 六、本轮状态

- **零源码改动、零构建**（只读取证 + 文档）。
- KI-653：状态由「待判定」改 **「根因已实证（H-653-A 成立）」**，修法待拍板。
- KI-656：状态改 **「第 458 轮修法对『组头容量为 0 + 成员容量非 0』的组无效（已取证）」**，方案 3 待拍板。
- KI-657：未复测（玩家力竭），保持原状，新探针已在树上。
