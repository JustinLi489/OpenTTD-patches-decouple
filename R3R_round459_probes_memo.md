# 第 459 轮：只读探针补强（2026-11-18）

玩家指令原话：

> 「那么现在是能做的除了我复测就是你加探针了？那么请你先加探针吧」

本轮性质：**只加/扩展只读探针，零行为变更**。不改任何判据、不动位置/容量/override、不碰 `src\*.h`。

---

## 一、先清点：哪些待取证项已经有探针（不必重加）

| 待取证项 | 现有探针 | 位置 | 够不够 |
|---|---|---|---|
| KI-653 弹窗是否为 `carries_more` | `BUY-REFIT … more=` / `BUY-REFIT-PART … pcap=` / `BUY-REFIT-PP` | `src\articulated_vehicles.cpp:394/405/411` | **不够**：只在命中时打，负判据（买了不弹窗的车）零行 ⇒ 无法区分「没命中」与「没执行」 |
| KI-656 容量 ×3 / override | `CARGOWEIGHT-VEH … ov=` | `src\ground_vehicle.cpp` | 够 |
| KI-656 打散成员容量归零 | `CAP-MEMBER-ZERO veh= head= cap= cb=` | `src\train_cmd.cpp`（第 458 轮新加） | 够 |
| KI-657 不安全尾位 | `RESV-UNSAFE-TAIL … safe=0` | `src\train_cmd.cpp:19110` | 够（现场 0 条，形态已排除） |
| KI-657 origin 语义（REG-456-E） | `CPLRES … origin=%d,%d destTile= node= root= unsafe= nTry= nOK= empty=` | `src\pathfinder\yapf\yapf_rail.cpp:1200` | **不够**：只给了 origin 的坐标，没给「这个 origin 是从哪来的」⇒ 无法判"入口为何在 218,122" |
| REG-457-A（换表 4→5） | `ORD-IDX-WATCH … real/impl/tt/n/ord=/parked=/curType=` | `src\train_cmd.cpp:2747` | 够（site/tag 已标调用点） |

结论：**只有两处需要动**。

---

## 二、本轮新增/扩展（两处，均只读）

### P-459-1 `CONSIST-CHK`（`src\articulated_vehicles.cpp`）

**解决什么**：KI-653 的 H-653-A / H-653-B 判定。

现有 `BUY-REFIT` 系列整块在 `if (real_refit_union != purchase_refit_union || ... || carries_more)` 内部 ⇒
**买了不弹窗的车一行日志都没有**，于是"日志里没有 BUY-REFIT"既可能是"没命中"，也可能是"这版 exe 的探针没编进去"，
两种结论相反却不可区分（这正是前几轮反复卡住的地方）。

新增：在 `if` **之前**无条件写一行

```
CONSIST-CHK veh=<车号> eng=<引擎> n=<实车节数> pn=<购买侧节数> hit=<0|1> more=<0|1> ru= ri= pu= pi= dflt=
```

- `hit=0` 而玩家看到弹窗 ⇒ 探针与判定脱钩，必须回头查分支；
- `hit=1 more=1` ⇒ H-653-A 成立（实车某节有容量而购买列表该货种容量为 0）；
- `hit=1 more=0` 且 `ru!=pu || ri!=pi` ⇒ 走的是 refit 掩码分支，不是容量分支（H-653-A 被否）；
- `n != pn` ⇒ 实车链与购买链**长度不同**（铰接结构在造车期就不一致），这是第三条支路。

### P-459-2 `CPLRES` 扩展（`src\pathfinder\yapf\yapf_rail.cpp`）

**解决什么**：REG-456-E / KI-657 的"入口为什么在那里"。

原行只有 `origin=%d,%d`，看不出语义。追加四个字段（不改任何既有字段、不改顺序）：

```
vTile=<车当前格> vDest=<车的 dest_tile> gap=<切比雪夫距离> oSrc=<car|dest|other>
```

判读表：

| 形态 | 含义 | 后续动作 |
|---|---|---|
| `oSrc=car`、`gap=0` | origin 就是车脚下 —— 正常 | 那 22 格现象与 origin 无关，转查"订到了但车不走" |
| `oSrc=dest`、`gap>0` | origin 被设成**目的地**而不是起点 —— 语义倒置，可直接定性为 bug | 查调用方传参 |
| `oSrc=other`、`gap>0` | origin 是第三处（上一段预留终点 / 站台回扫 / 站台末端）—— 玩家报的"218,122"必属此类 | 按 gap 与实际停车点比对，锁死是哪个来源 |

无行为变更：只在已存在的 `if (r3r_cplres_prev != r3r_cplres_sig)` 里去重与打印，签名式不加入新字段
（否则去重粒度变化会改变日志条数，破坏与历史日志的可比性）。

---

## 三、刻意不加的

- **不加**新探针去查 REG-457-A/B/C：`ORD-IDX-WATCH` 已有 `site=`/`tag=`/`ord=`/`curType=`/`parked=`，
  现场那三条（`n=12->5`、`seg=0` 却 `borrowed=1`、`curType=0`）都能从现有字段读出，再加只会稀释判读。
- **不加** KI-656 的新探针：`CARGOWEIGHT-VEH` + `CAP-MEMBER-ZERO` 覆盖两侧。
- **不动**任何 `src\*.h`（避免触发全量重编，也避免改布局）。

---

## 四、复测时怎么用（给玩家的最小操作单）

1. 用新 exe 开局，**买一列会弹窗的**（中国火车包 DF3）与**一列不弹窗的**车。
   - 期望：两列车各出现 `CONSIST-CHK`；弹窗那列 `hit=1`，不弹窗那列 `hit=0`。
2. 走一遍 KI-656 链：设为段 → 存档 → 读档。
   - 期望：出现 `CAP-MEMBER-ZERO cap=<非0> cb=<非0>`；三节 `CARGEWEIGHT-VEH` 的 `ov=` 均非 `65535`。
3. 复现 KI-657 那个卡住场景。
   - 期望：出现 `CPLRES … vTile=211,116 gap=22 oSrc=other`（数字以实际为准），
     即可直接判定"入口不是车脚下"。

---

## 五、构建自证（已完成）

| 项 | 读数 |
|---|---|
| 入口 | `R3R_fullbuild.cmd inc detach`（复用既有脚本，未新建 `.cmd`） |
| 护栏 | `GUARD: incremental is safe (no header/lang file is newer than the newest object)` |
| 日志尾部 | `[2/4] Building CXX object … articulated_vehicles.cpp.obj` → `[3/4] … yapf_rail.cpp.obj` → `[4/4] Linking CXX executable openttd.exe`，`R3R build ended 2026/11/18 22:28:01.98 MODE=inc` |
| `build\R3R_incbuild.done` | `EXIT_CODE=0`（`2026-11-18 22:28:01`） |
| 源码 | `articulated_vehicles.cpp` 22:23（22 952 B）；`yapf_rail.cpp` 22:24（140 251 B） |
| obj | `articulated_vehicles.cpp.obj` 22:25（1 398 377 B）；`yapf_rail.cpp.obj` 22:25（9 902 580 B） |
| exe | `build\openttd.exe` `2026-11-18 22:27:57`，53 030 400 B |
| lint | 两文件 0 条 |
| 字面量 | `CONSIST-CHK veh=` ✓ / `oSrc=%s` ✓ / `CAP-MEMBER-ZERO` ✓ |

### 5.1 落地细节与偏差

- `CONSIST-CHK` 的全部字段已在 `if` **之前**无条件打印；为免与既有 `BUY-REFIT` 重复计算，
  节数/购买侧节数各自独立算了一次（`r3r_n` / `r3r_pn`），**不影响** `if` 内部原有的同名局部量。
- `CPLRES` 的四个新字段追加在**行尾**，既有字段名与顺序逐字未变；`r3r_cplres_sig` 未加入新字段
  ⇒ 去重粒度与历史日志一致（这是刻意取舍，见 §二）。
- `oSrc` 取 `car|dest|other`，判据是 `origin_tile` 分别与 `v->tile` / `v->dest_tile` 的等值比较；
  `vDest` 在车无目的地时会打印 `65535,65535`（`INVALID_TILE` 位宽直读），属预期噪声。

### 5.2 复测时注意

- `CONSIST-CHK` 每次造车都会打 ⇒ 日志量随购车次数线性增长，但一列一条，可接受；
  若嫌多可后续加"同引擎去重"，本轮刻意不做（负判据要求"每次都证明执行过"）。
- `CPLRES` 仍受签名去重约束 ⇒ 同一 `(node, origin, nTry, nOK, destTd)` 只出现一次，
  所以判 `oSrc` 时看**首条**即可，不必找"最新一条"。

