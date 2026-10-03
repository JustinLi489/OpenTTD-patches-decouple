# R3R 第 172 轮 临时分析报告：最后一次挂车（COUPLE-OK @3749）列车直接穿模

- 日期：2026-09-30
- 现场日志：`build\R3R_debug.log`（5504 行 / 368 202 B，最后写入 2026-09-30 20:49:06；由第 171 轮 exe 会话产生，171 轮只加只读探针、零行为变更）
- 涉及源码：`src\train_cmd.cpp`（本轮**未改任何源码**，纯取证 + 代码定位）
- 关联：KI-201 附记 3/4/**附记 8**、KI-174（`FOLDCHK-DIR-OVERRIDE`）、KI-173、KI-06
- 状态：已定位、已给修法；**未实现、未编译、未实机复测**

---

## 一、结论摘要

最后一次挂车（3749 行 `COUPLE-OK`）穿模的原因是：**第 6 次重试时全链已紧贴到 `worst_gap=2`，KI-174 的 `FOLDCHK-DIR-OVERRIDE`（`train_cmd.cpp:8876-8879`）把 3699 行刚判出的"真折叠"结论丢掉，直接以"链尾接链头"硬拼提交**；随后 `COUPLE-SEAM-FLIP` 只把车底 18 节的 `direction` 由 3 改成 7（位置一像素未动），得到一条"**方向一致但位置倒退**"的病链——机车 48/49 与车底 6/7 纵向叠在一起（差 1~2px），即穿模。

⇒ 这是 **KI-201 附记 8 亲手定性过的同一类病链**，只是入口从"8px 豁免"换成了 KI-174 的 OVERRIDE：附记 8 收窄了豁免，没同步收窄 OVERRIDE，留了第二扇门。

---

## 二、现场证据

### 2.1 六次重试的形状（每次都有 `ARRANGE-IN dh=48 dst=50 sh=6 src=6 mc=1`，n=21）

| 尝试 | FOLDCHK | A=机车尾 idx50 的 y | B=车底头 idx6 的 y | worst_gap | dist | FOLDCHK-DIR dot | 结果 |
|---|---|---|---|---|---|---|---|
| 1 | 3495 | 1022 | 1013 | 7 | 9 | 9（3496） | REFUSE（3523） |
| 2 | 3532 | 1021 | 1013 | 6 | 8 | 8 | REFUSE（3560） |
| 3 | 3565 | 1020 | 1013 | 5 | 7 | 7 | REFUSE（3593） |
| 4 | 3599 | 1019 | 1013 | 4 | 6 | 6 | REFUSE（3627） |
| 5 | 3665 | 1018 | 1013 | 3 | 5 | 5 | REFUSE（3693） |
| 6 | **3698** | **1017** | 1013 | **2** | **4** | **4**（3699） | **OVERRIDE→提交→COUPLE-OK** |

要点：**车底 6 一直不动（y=1013），是机车每次失败后整体向北推进 1px**（尾部 1022→1017；头 48 由 1015 推进到 1010）。

### 2.2 第 1 次尝试原文（3488-3527）

```
3490:CPL-GEO site=geo v=48 vn=48 vlen=2 u=6 z=6 ulen=2 rev=0 dx=0 dy=2 diff=2 need=2 spd=120 db=0
3495:FOLDCHK COUPLE n=21 worst_gap=7 A idx=50 x=936 y=1022 tile=58,63 dir=7 trk=0x2 db=0 B idx=6 x=936 y=1013 tile=58,63 dir=3 trk=0x2 exp=2 nom=2 nom0=2 nom1=2 dist=9
3496:FOLDCHK-DIR COUPLE dot=9 A idx=50 ... y=1022 dir=7 B idx=6 ... y=1013 dir=3
3499:  FOLD-V veh=48 p=-1 n=49 subtype=0x09(front=1 eng=1) SF=1 dir=7 y=1015 engType=523
3501:  FOLD-V veh=50 p=49 n=-1 ... starB=1 dir=7 y=1022 engType=523
3504:  FOLD-U veh=6  p=-1 n=7  subtype=0x09(front=1 eng=1) SF=1 dir=3 y=1013 engType=372
3505:  FOLD-U veh=7  ... subtype=0x02(artic=1) dir=3 y=1008
3510:  FOLD-U veh=12 ... subtype=0x04(wagon=1) dir=3 y=989
3521:  FOLD-U veh=23 p=22 n=-1 subtype=0x02(artic=1) dir=3 y=944
3523:FOLDCHK-REFUSE-REAL-ARTIC v=48 u=6 v_real=0 u_real=1 -> rolled back, no logical flip this tick
3524:CPL-GEO-DONE v=48 u=6 merged=0 spd=120 x=936 y=1015
3527:CPL-HIT site=open loco=48 v=6 len_v=2 len_mf=2 min_diff=1 need=2 fit=2 dx=0 dy=-1 maxd=1 overlap=1 spd=21 dir=7
```

读法：机车 48/49/50 全 `dir=7`（NW 朝北），车底 6..23 全 `dir=3`（S 朝南）⇒ **方向相反 = 鼻对鼻**（车底鼻子朝南正对迎面机车）。车底含真 artic 部件（7/8/10/11…22/23 `artic=1`，9/12/15/18/21 是 `wagon=1` 段头）⇒ `u_real=1`。于是每次都是同一条死路：**折叠判定成立 → 折叠修正分支 → 真 artic 门禁拒绝 → 本 tick 不挂车 → 下 tick 重试**，期间机车持续北推。

### 2.3 成功那次的关键行

```
3698:FOLDCHK COUPLE n=21 worst_gap=2 A idx=50 y=1017 dir=7 B idx=6 y=1013 dir=3 exp=2 nom=2 dist=4
3699:FOLDCHK-DIR COUPLE dot=4 A idx=50 y=1017 dir=7 B idx=6 y=1013 dir=3
3700:FOLDCHK-DIR-OVERRIDE worst_gap=2 tight direct splice, fold verdict ignored   <-- 折叠判定被丢弃
3702:COUPLE-SEAM-FLIP circ=4 a=50 dirA=7 b=6 dirB=3                               <-- 只改方向、不动位置
3703:GEO respace-before n=0 veh=48 y=1010 dir=7 clen=2 dist=4 nom=4
3704:GEO respace-before n=1 veh=49 y=1014 dir=7 clen=5 dist=3 nom=3
3705:GEO respace-before n=2 veh=50 y=1017 dir=7 starB=1 clen=2 dist=4 nom=2
3706:GEO respace-before n=3 veh=6  y=1013 dir=7 starF=1 clen=2 dist=5 nom=5
3707:GEO respace-before n=4 veh=7  y=1008 dir=7 artic=1 clen=8 dist=5 nom=5
```

注意 3699 的 dot=+4：**附记 8 的收窄豁免在这里是"正确生效"的**（同一个 8px 接缝没被双重惩罚，折叠如实判出），问题全在下一行的 OVERRIDE 把它丢掉。

### 2.4 穿模读数

链序 `48→49→50→6→7→…` 与 y 坐标（3703-3707、3811-3832 `GEO settle-couple`、3853 起 `F` dump 三处一致）：

```
48 y=1010 → 49 y=1014 → 50 y=1017 → 6 y=1013 → 7 y=1008
                    ↑递增      ↑ 在 50→6 处折返
```

- 折返点 = 拼接点 `50↔6`；
- **跨车体重叠**：`49(1014)` 与 `6(1013)` 差 **1px**；`48(1010)` 与 `7(1008)` 差 **2px** ⇒ 机车 3 节车体与车底前两节交错叠置 = 玩家看到的穿模；
- 下一 tick 整链一起动（`F` dump 全链 y 各减 1），叠置关系被带着跑。

### 2.5 重排修不了（机器可读签名）

```
3747:RESPACE-AFTER-EDIT couple head=48 px=0 stretch=0 unfixed=1 skip=0 front=0 passes=1 visited=20 capped=0
3749:COUPLE-OK loco=48 rear=23 consist=6 co=1 real=11 type=1 tx=58 ty=63 x=936 y=1010
```

对坏对 `(a=50, b=6)` 走一遍 `R3RRespaceChainAfterEdit`（`train_cmd.cpp:6826-6875`）：
- 6842-6846 跳过门：`|dist-nominal| = |4-2| = 2 ≤ R3R_RESPACE_MAX_ERROR(8)` ⇒ 不跳过；
- `dist(4) > nominal(2)` ⇒ 走"拉近尾部"：`GetNewVehiclePos(b=6)` 按 **6 自己的 direction(7)** 前进一步 ⇒ y=1012 ⇒ `next_dist = |1017-1012| = 5 ≥ dist(4)` ⇒ **6868-6870 直接 `break`**。

⇒ **`unfixed=1` 就是"这一对结构性不可修"的机器可读签名**：车底头的朝向背离机车尾，任何"沿它自己的朝向前进"的重排只会把缝拉大。对照：更早三次正常挂车（`COUPLE-OK` 在 233 / 1104 / 2740 行）的重排摘要均为 `unfixed=0`，**本次是全日志唯一 `unfixed=1` 的一次**，与穿模 1:1 对应。

### 2.6 提交后引擎一直报这条病链

```
3806 / 3835:CRT-FOLD veh=48 tile=58,63 dir=7 backTile=58,59 rel=0,-4 dot=4
3853 起:TTB-PROBE fold-geom veh=7 ... prev=6 ；veh=6 ... prev=50   （连续刷，车速 22→96 仍在爬升）
```

即"提交后的坏几何不是静默的"：引擎每 tick 都在 `TTB-PROBE fold-geom`（`chosen_track == TRACK_BIT_NONE`，正是代码注释里"会让 TrainController 一开动就崩"的那类形状），本次侥幸没崩，但整列是带病运行的。

---

## 三、代码落点

| 位置 | 作用 |
|---|---|
| `train_cmd.cpp:8768-8778` | `ChainFolded` lambda：`worst_gap` 取全链 `max|dist-nom|`；`>8` 记 `FOLDCHK-ACCEPT` |
| `train_cmd.cpp:7747-7830` | `R3RCheckChainFoldedDirection`：逐对算 dot；**7794-7809 注释描述的正是同形状场景**（"veh=33、worst_gap=7、A idx=35 dir=7 / B idx=6 dir=3、exp=2 dist=9"，即附记 8 现场） |
| `train_cmd.cpp:~7812` | 豁免条件 = `|pair_dist - pair_expected| <= 8 && a->direction == b->direction`（附记 8 的收窄） |
| `train_cmd.cpp:8860-8879` | **`FOLDCHK-DIR-OVERRIDE`**（KI-174）：`direct_folded && direct_dir_fold && !direct_splice && direct_gap<=2` ⇒ 把方向判定置回 false |
| `train_cmd.cpp:8942-8952` | `FOLDCHK-REFUSE-REAL-ARTIC`：任一参与方含真 artic ⇒ 放弃折叠修正、本 tick 不挂车 |
| `train_cmd.cpp:9642-9704` | `COUPLE-SEAM-FLIP/CRASH`：拼缝 `circ>=3` 时逐节反转 `direction`（只改方向、不动位置） |
| `train_cmd.cpp:6826-6875` | `R3RRespaceChainAfterEdit`：6842-6846 跳过门、6868-6870 `next_dist >= dist → break` |

---

## 四、根因链（三层）

**(A) 直接原因：OVERRIDE 用几何距离推翻方向判定，其前提可被证伪。**
`worst_gap <= 2` ⇒ 全链每一对都满足 `|dist-nom| <= 2 <= 8` ⇒ 方向判据的豁免条件退化成**只剩 `a->direction == b->direction` 一项** ⇒ 此时还能被 flag 的对**必然是 direction 相反的一对**。而"相邻、处在名义间距上、方向却相反"在一条合并链里就是**真交错折叠**（附记 8 定义的病链形状）。
⇒ 形式上：`direct_dir_fold && worst_gap<=2` **本身就等价于"真折叠"**，OVERRIDE 恰好在唯一不该生效的场景生效。它想保护的"1px 紧贴的健康链"（KI-174 的目标）根本不会走到这里——健康链的紧贴对因方向相同已被 7812 豁免、判定根本不会 fire。

**(B) 前因：REFUSE-REAL-ARTIC 让"鼻对鼻 + 含真 artic 车底"永远无解。**
每 tick 拒绝 ⇒ 机车每 tick 北推 1px ⇒ `worst_gap` 从 7 一路降到 2 ⇒ **恰好为 OVERRIDE 造出了触发条件**。也就是说：**是"永远失败的挂车重试"把几何顶成了病态**。

**(C) 掩盖层：SEAM-FLIP 只改方向、respace 又修不了。**
SEAM-FLIP 把 `direction` 掰齐（3→7）后，"方向相反"这个可诊断特征被消掉（此后方向判定必然为假），随后 respace 因 6 的朝向背离 50 而拒修（6868-6870）⇒ 没有任何一层能纠正几何。

---

## 五、修复建议（按优先级）

1. **（首选，最小且可证）给 OVERRIDE 加前提**，二选一或同时：
   - (a) 被 flag 的对**不得是本次拼接对** `(v_last, u_head)`。真折叠恒发生在拼接对上（本次 flag 对正是 `50=v_last` / `6=u_head`）。实现需让 `R3RCheckChainFoldedDirection` 用出参回传 `wa/wb`（现只在函数内部使用）。
   - (b) 被 flag 的对上须 `a->direction == b->direction`。由 (A) 的证明，在 `worst_gap<=2` 下这等于**停用 OVERRIDE**；若担心 KI-174 老场景回退（倒车机车 dot=2 的死循环），正解是修根——**把混合 `direction` 归一**（照 KI-217 对 `DrivingBackwards` 的做法），而不是用几何覆盖方向判定。
2. **（硬闸门）提交前采样方向判定结论，然后断言**：在 SEAM-FLIP 之前若方向判定为真、且 flag 对为拼接对且方向相反 ⇒ **拒绝提交**（打 `COUPLE-REFUSE-STILL-FOLDED`），不允许用 SEAM-FLIP 掩盖。即把附记 8 的原则从"豁免条件"升级为"提交断言"。
3. **（治前因）`REFUSE-REAL-ARTIC` 时让机车停止推进**（命中即停车 / 不再请求前进）。这样 `worst_gap` 永远降不到 2，OVERRIDE 也就永无触发条件，可作 1、2 的第二道保险。
4. **（探针）** `COUPLE-OK` 前后加 `COUPLE-PREFLIGHT folded_dir=/splice_pair=/gap=` 与 `COUPLE-POSTFOLD check=`；并把 `RESPACE-AFTER-EDIT` 的 `unfixed>0` 当作"大病链"标记（本次它与穿模 1:1 对应，是现成的判据）。

---

## 六、未确认项

- **机车侧为何没被碰撞/停车守卫拦住**（每次 `merged=0` 之后仍北推 1px）：证据链止于 `3524 CPL-GEO-DONE merged=0` → `3527 CPL-HIT overlap=1 dy=-1 maxd=1`；候选解释是"命中门允许 `overlap=1` 且失败路径不停车"，**未取证**，留待下一轮。
- `REFUSE` 回滚是否 100% 还原（3523 回滚后几何有无残留平移）：需加"REFUSE 前后 GEO 对比"探针。
- KI-174 老场景（倒车机车 dot=2）在 KI-173 + 附记 8 之后是否还存在；若已不存在，OVERRIDE 可直接删除（需一次倒车挂车复测）。
- 无实机复测（本轮只做日志取证 + 代码定位）。

---

## 七、复测判据

1. 同场景（机车正向、鼻对鼻贴上 dir 相反且含真 artic 的静止车底）**不得出现 `FOLDCHK-DIR-OVERRIDE`**；应见 `REFUSE`，或走通折叠修正。
2. `COUPLE-OK` 后的 `GEO settle-couple` 沿链序必须**单调**（相邻车 y 同向变化），不得出现 `50(y=1017) → 6(y=1013)` 的折返。
3. `RESPACE-AFTER-EDIT ... unfixed=0`（不存在结构性不可修的缝）。
4. 提交后不再出现 `CRT-FOLD ... dot>0` 与 `TTB-PROBE fold-geom`。
5. 回归面：健康挂车（对照 233/1104/2740 行，`unfixed=0`）不回退；附记 8 的 COUPLE-FLIP 路径与 KI-174 的紧贴链场景（若仍存在）不被误伤。

---

## 八、实现落地（2026-09-30 第 172 轮实现；仅改 `src/train_cmd.cpp`，未碰 `src/*.h`）

本轮把「五、修复建议」的第 1 条与第 2 条都实现了；第 3 条（治前因）与第 4 条（探针）只做了一半，见「九、仍未做」。

### 8.1 让方向判据回传"被判折叠的那一对"（为 1、2 提供前提数据）

`R3RCheckChainFoldedDirection` 增两个出参（默认 `nullptr`，老调用点零改动）：

```cpp
static bool R3RCheckChainFoldedDirection(const Train *head, const char *tag,
        const Train **out_a = nullptr, const Train **out_b = nullptr)
```

- 函数入口先清空出参（调用方只在返回 `true` 时读，避免读到上一次的残留）；
- 在 `if (worst_dot > 0)` 块内（即确判折叠处）写回 `wa`/`wb`。

`ChainFolded` lambda 同步加两个透传参数并传给判据，调用点从
`ChainFolded(v, "COUPLE", &direct_gap, &direct_dir_fold)` 变成
`ChainFolded(v, "COUPLE", &direct_gap, &direct_dir_fold, &direct_fold_a, &direct_fold_b)`。

### 8.2 给 OVERRIDE 加上两条独立前提（对应建议 1 的 (a)+(b)，**同时**要求）

新增两个前提 + 一个新节流探针标签：

```cpp
const bool fold_pair_on_splice = (direct_fold_a != nullptr && direct_fold_a == v_last);
const bool fold_pair_dir_mixed = (direct_fold_a != nullptr && direct_fold_b != nullptr &&
        direct_fold_a->direction != direct_fold_b->direction);
const bool override_candidate = (direct_folded && direct_dir_fold && !direct_splice &&
        direct_gap >= 0 && direct_gap <= 2);

if (override_candidate && !fold_pair_on_splice && !fold_pair_dir_mixed) {
    R3RDbgWrite("FOLDCHK-DIR-OVERRIDE worst_gap=%d tight direct splice, fold verdict ignored\n", direct_gap);
    direct_folded = false;                 /* 只有"链内部 + 方向一致 + 残差极小"才允许覆盖 */
} else if (override_candidate && direct_fold_a != nullptr && direct_fold_b != nullptr) {
    /* 覆盖被否决 ⇒ 折叠结论保留 ⇒ 继续走既有折叠修正分支（含 REFUSE-REAL-ARTIC 门禁） */
    if (R3RDbgEdge(R3REDGE_FOLDOVR, ovr_key, ovr_payload)) {
        R3RDbgWrite("FOLDCHK-DIR-OVERRIDE-SKIP worst_gap=%d on_splice=%d dir_mixed=%d dirA=%d dirB=%d A=%d B=%d -> fold verdict kept\n", ...);
    }
}
```

语义（与「四、根因链 (A)」的证明一致）：**在 `worst_gap<=2` 下，`dir_mixed=1` ⟺ 真交错折叠**（豁免只剩"方向相同"一项，还被 flag 的对必然方向相反）。
- 前提 (a) `on_splice=1`（flag 对 = 本次拼接对 `v_last`）：合并只可能在这一对上制造折叠，OVERRIDE 在此生效等于取消唯一保护 ⇒ 本次现场 `A=50=v_last` 正是这种。
- 前提 (b) `dir_mixed=1`：把 OVERRIDE 限制在"链内部 + 方向一致 + 残差极小"的窄格里。
- 两条前提只要有一条不成立就不覆盖（`else if` 分支只打节流日志、不动判据），OVERRIDE 想保护的"1px 紧贴健康链"照旧：紧贴且同向的对已被 7817 的豁免吃掉，判据根本不会 flag，所以这一路健康场景永远不会走到 `else if`。
- 新标签 `R3REDGE_FOLDOVR` 加在枚举尾（`R3REDGE_DECOUPLETRY` 之后、`R3REDGE_COUNT` 之前），沿用 `R3RDbgEdge` 的"同状态 128 帧窗口至多一行"节流，key=(a,b) 车号对、payload=(gap,on_splice,dir_mixed)。

### 8.3 提交前硬闸门 `COUPLE-REFUSE-STILL-FOLDED`（对应建议 2）

位置：`TryTrainCouple()` 里 `bool ok = CheckTrainAttachment(head).Succeeded();`（`train_cmd.cpp:9225`）**之前**，即四类候选全部结束、任何 `SEAM-FLIP` 都没机会掩盖之后：

```cpp
{
    const Train *gate_a = nullptr;
    const Train *gate_b = nullptr;
    const bool gate_folded = R3RCheckChainFoldedDirection(head, "COUPLE-PREFLIGHT", &gate_a, &gate_b);
    if (gate_folded && gate_a != nullptr && gate_b != nullptr && gate_a == v_last &&
            gate_a->direction != gate_b->direction) {
        RestoreTrainBackup(original_src);  RestoreTrainBackup(original_dst);
        if (v_flipped) R3RUndoLogicalFlip(v, v_old_bounds, v_old_roles);
        if (u_flipped) R3RUndoLogicalFlip(u, u_old_bounds, u_old_roles);
        R3RRefreshChainCaches(v);          R3RRefreshChainCaches(u);
        v->ConsistChanged(CCF_ARRANGE);    u->ConsistChanged(CCF_ARRANGE);
        R3RDbgWrite("COUPLE-REFUSE-STILL-FOLDED head=%d a=%d dirA=%d b=%d dirB=%d -> rolled back, no merge this tick\n", ...);
        return false;
    }
}
```

- 它是**不变式断言**而不是某条候选的补丁：将来任何新增候选/前提/跳过条件，只要最终链的"本次拼接对"仍是方向折叠，就必须先过这一关才能提交，"绝不提交折叠链"不会被再次绕过（KI-265 的病链正是从 OVERRIDE 这扇侧门进来的）。
- 条件是"flag 对 = 拼接对 `v_last` **且**两车 direction 相反"：与 8.2 用同一份数据、同一条推理；若 flag 对只是"大残差但同向"，本闸门不动手，交给既有 `SpliceFolded`/候选回滚逻辑。
- 拒绝后与既有失败路径同一条出路（`REFUSE-REAL-ARTIC` / `CheckTrainAttachment` 失败）：本 tick 不挂车、下 tick 重试，回滚手段也逐行照抄该分支（`RestoreTrainBackup` 两侧 + `R3RUndoLogicalFlip` 两侧 + `R3RRefreshChainCaches` + 两侧 `ConsistChanged(CCF_ARRANGE)`），不引入任何新的状态复位手段。
- 探针 tag 用 `"COUPLE-PREFLIGHT"`：命中时同一次调用还会按既有格式打 `FOLDCHK-DIR`（新格式 `A idx=.. dir=.. B idx=.. dir=..`），与随后的 `COUPLE-REFUSE-STILL-FOLDED` 同 tag 前缀，便于一把 grep 取现场。

### 8.4 构建自证（复用既有 `_tmp_inc_build.cmd`，未新建任何 `.cmd`）

| 项 | 值 |
|---|---|
| 护栏 | `GUARD: incremental is safe (no header/lang file is newer than the newest object)` |
| 日志 | `[3/3] Linking CXX executable openttd.exe`；`error C*` / `fatal error` / `FAILED:` / `build stopped` 计数 **0** |
| 判决 | `build\R3R_incbuild.done` = `EXIT_CODE=0`（2026-09-30 21:25） |
| 时间戳 | `src\train_cmd.cpp` 21:18 → `train_cmd.cpp.obj` 21:22 → `build\openttd.exe` 21:24（51 548 160 B） |
| lint | `read_lints` = 0 条 |
| 产物自证 | `findstr` 命中 `COUPLE-PREFLIGHT` / `COUPLE-REFUSE-STILL-FOLDED head=%d a=%d dirA=%d b=%d dirB=%d -> rolled back, no merge this tick` / `FOLDCHK-DIR-OVERRIDE-SKIP worst_gap=%d on_splice=%d dir_mixed=%d ...` |

### 8.5 复测判据（在「七」的 5 条之上补 3 条）

6. 若 OVERRIDE 被否决：日志出现 `FOLDCHK-DIR-OVERRIDE-SKIP ... on_splice=1`（本次现场形状）或 `dir_mixed=1`，且**不再出现**裸的 `FOLDCHK-DIR-OVERRIDE ... fold verdict ignored`。
7. 若折叠修正实败：出现 `COUPLE-REFUSE-STILL-FOLDED head=48 a=50 dirA=7 b=6 dirB=3 -> rolled back`，随后 `GEO settle-*` 链序单调、无 `50→6` 折返。
8. 反向回归：健康紧贴挂车（残差<=2、全链同向）仍能直拼提交，`FOLDCHK-DIR-OVERRIDE` 照旧出现（前提 (a)(b) 为假），不被新闸门误拒。

---

## 九、仍未做（下一轮）

- **建议 3「治前因」未做**：`REFUSE-REAL-ARTIC` 命中时机车仍每 tick 北推 1px，`worst_gap` 仍会一路降到 2；本轮靠 8.2 把 OVERRIDE 关掉，使"降到 2"不再产生病链，但**无解的重试循环本身仍在**（日志仍会每次打 `FOLDCHK-REFUSE-REAL-ARTIC`）。正解 = 命中即停车/不再请求前进。
- **建议 4 探针只做了一半**：`COUPLE-PREFLIGHT` 只有"判为折叠"时才落 `FOLDCHK-DIR` 行，没做无条件 `folded_dir=/splice_pair=/gap=` 摘要行；`COUPLE-POSTFOLD check=` 未加。
- 「六、未确认项」三条（机车未被拦、REFUSE 回滚是否 100% 还原、KI-174 老场景是否还存在）本轮均未取证。
- 无实机复测：本轮只到"编译通过 + 产物自证"，8.5 的 8 条判据全部待游戏内复跑。

---

## 十、第 173 轮：把"每次重试"都变成可核对的现场（仅加只读探针，零行为变更）

- 日期：2026-09-30
- 涉及源码：`src\train_cmd.cpp`（**只加探针**，未改任何判据/运动/位置，未碰 `src\*.h`）
- 关联：KI-265、第二节 2.2 的六次重试形状、第八节 8.2（OVERRIDE 两条前提 + 提交前硬闸门 `COUPLE-REFUSE-STILL-FOLDED`）
- 状态：已实现 + 已编译（增量）；游戏内复测待做

### 10.1 为什么补探针（第九节"建议 4 只做了一半"的落地）

第 172 轮的结论建立在"读 3699 行 `FOLDCHK-DIR dot=4`"上；而判为折叠才落 `FOLDCHK-DIR`，该行又受 `R3RDbgEdge` 的 128 帧窗口节流 ⇒ 复测时"日志里没有 `FOLDCHK-DIR`"既可能是真没折叠、也可能是被节流吞掉，**无法区分**。更根本的是：折叠判据每次重试都跑，却只有"结果为折叠"才留痕，于是复测拿不到"6 次重试各自的几何"。本轮补齐这层取证。

### 10.2 四处探针

1. **`COUPLE-PREFLIGHT`（提交点无条件几何摘要）**——`train_cmd.cpp:9212-9245`，紧跟硬闸门自己的 `R3RCheckChainFoldedDirection(head, "COUPLE-PREFLIGHT", ...)`（:9226）之后、**闸门判据之前**无条件写 `head/last/fold/dirfold/gap/pair=(...) mixed/onsplice/splice=(...) spd/db`；节流标签 `R3REDGE_CPLSUM`（:5859，插在 `R3REDGE_FOLDOVR` 之后 / `R3REDGE_COUNT` 之前），key=(v,u) 车号对、payload=(gap,dirA,dirB,folded,mixed) ⇒ **每个不同几何一行**，"6 次重试"必留 6 行可逐行对比（gap 7→2、拼接对是谁、两车朝向、机车当时 spd）。
2. **`COUPLE-POSTFOLD`（提交后事后校验）**——`Couple()` 内 `COUPLE-OK` 逐节 dump 之后、`ORD-AFTER-COUPLE` 之前（:10172-10182）：在最终整链（`v->First()`）上重跑方向判据并写一行，为真时追加 ` INVARIANT-BROKEN-report-this`。**只写日志、不撤销提交**（提交后回滚要连排程交接/索引继承/窗口失效一起还原，风险大于收益）。它落在 `COUPLE-SEAM-FLIP` 之后，而 SEAM-FLIP 会把接缝两车 direction 改成一致 ⇒ 该行**预期恒为 `folded=0`**，只用于抓"闸门漏掉的侧门"，不能当"链没问题"的证明；金标准是提交前的 `COUPLE-PREFLIGHT`。
3. **`RESPACE-BADCHAIN`（结构性坏链签名）**——`R3RRespaceChainAfterEdit()` 汇总行之后（:6942）：`unfixed != 0` 时单打 `RESPACE-BADCHAIN %s head=%d unfixed=%d skip=%d stretch=%d px=%d visited=%d capped=%d`。现场 3747 行 `RESPACE-AFTER-EDIT couple ... unfixed=1` 正是穿模提交后那条，此后它有了独立签名，不必再从汇总行猜。
4. **`FOLDCHK-REFUSE-REAL-ARTIC` 扩容（"治前因"取证）**——原行追加 `gap=%d pair=(%d,%d) spd=%d db=%d`，用来回答"拒绝挂车有没有让机车停下来"：现场 6 次重试机车每失败一次北推 1px（`worst_gap` 7→2），若复测中 `spd>0` 且 `gap` 逐次变小，则确认"拒绝 ≠ 停车"。

### 10.3 构建自证

- 复用既有 `_tmp_inc_build.cmd`（未新建任何 `.cmd`）；护栏 `GUARD: incremental is safe (no header/lang file is newer than the newest object)` ⇒ 增量合法。
- `src\train_cmd.cpp` 21:36 → `train_cmd.cpp.obj` 21:37 → `build\openttd.exe` **2026-09-30 21:39**（51 548 160 B）；`build\R3R_incbuild.done` = `EXIT_CODE=0`；日志 `[3/3] Linking CXX executable openttd.exe`，`error C*` / `fatal error` / `FAILED:` / `build stopped` 计数 0；`read_lints` 0 条。
- 产物自证：`findstr` 在 `build\openttd.exe` 内命中 `COUPLE-PREFLIGHT head=`、`COUPLE-POSTFOLD head=`、`INVARIANT-BROKEN-report-this`、`RESPACE-BADCHAIN %s head=`、`FOLDCHK-REFUSE-REAL-ARTIC v=`（5/5 全中）。

### 10.4 本轮**故意不做**（等现场数据）

- **不给机车下刹车**（第九节建议 3「治前因」）。唯一杠杆是在拒绝分支清零 `cur_speed`/`subspeed`，但拒绝点可能来自"滚动逼近"或"停稳扫描"两条路径、共用同一函数；在拿到 10.2-4 的数据前贸然清零，有让机车**停在挂车距离外再也挂不上**的风险。
- **不在提交后回滚**（`COUPLE-POSTFOLD` 只记日志），理由见 10.2-2。
- **不动 `SEAM-FLIP`**：它把接缝两侧 direction 改成一致，本身不制造折叠；它的存在恰恰说明"必须拦在提交前"，故闸门刻意放在它之前。

### 10.5 复测判据（与 KI 条目「第 173 轮」第五节一致）

**目测**：(a) 不许再出现"机车压在车厢上"（穿模），挂不上时也应规规矩矩停在车底前面；(b) 若反复尝试，记一句"它是否一点点往前蹭"（预计会 —— 那是下一轮「治前因」的目标）；(c) 挂上后不得有图像错位／被狗啃／车厢相叠，也不得有 `TTB-PROBE fold-geom` 连续刷屏导致的卡顿。

**日志**：(d) 每次到提交点都应有 `COUPLE-PREFLIGHT` 一行，且不得紧接着出现裸的 `FOLDCHK-DIR-OVERRIDE ... fold verdict ignored`（应改 `FOLDCHK-DIR-OVERRIDE-SKIP ... on_splice=1` 或 `dir_mixed=1`）；(e) 挂不上应有 `COUPLE-REFUSE-STILL-FOLDED ... -> rolled back, no merge this tick`，挂上应有 `COUPLE-POSTFOLD ... folded=0` 且无 `INVARIANT-BROKEN`；(f) 留 10~20 行 `FOLDCHK-REFUSE-REAL-ARTIC ... gap=... spd=...`；(g) `RESPACE-BADCHAIN` 不应再出现（出现即还有一条侧门，需连同上下文 30 行）。

### 10.6 仍未做

- 8.2 的硬闸门与 10.2 的探针都只到"编译通过 + 产物自证"，**本轮无实机复测**。
- 第九节的"治前因"（命中即停车）与「六、未确认项」三条仍待现场数据。

---

## 十一、第 174 轮：复测判读 + 「治前因」落地（改行为 2 处，仅 `src\train_cmd.cpp`）

- 日期：2026-09-30
- 复测日志：`build\R3R_debug.log`（236 487 B，最后写入 21:54:55；由第 173 轮 exe（21:39）产生）
- 涉及源码：`src\train_cmd.cpp`（`src\*.h` 未动）
- 关联：第 172 轮修法 1/2/3、第 173 轮四条探针、KI-265、KI-174、KI-201 附记 8、KI-173
- 状态：已实现 + 已编译（增量）；**实机复测待做**

### 11.1 一句话结论

第 172 轮的修法 1、2 **全部按设计生效**（旧侧门确实关上了：裸 `OVERRIDE` 0 次、`OVERRIDE-SKIP` 4 次），**但穿模仍在** —— 病链换从"方向判据 `dot==0` 的盲区"进来；同时第 173 轮的取证目标 100% 达成，`spd`/`gap` 数据把「治前因」钉死，故本轮落地它，并顺手补上 `dot==0` 这一格。

### 11.2 探针统计（与 10.5 的判据逐条对照）

| 探针 | 次数 | 判读 |
|---|---|---|
| 裸 `FOLDCHK-DIR-OVERRIDE ... fold verdict ignored` | **0** | 判据 (d) 通过 |
| `FOLDCHK-DIR-OVERRIDE-SKIP ... on_splice=1 dir_mixed=1` | 4 | 判据 (d) 通过（折叠结论被保留） |
| `FOLDCHK-REFUSE-REAL-ARTIC ... gap=... spd=...` | 10 | 判据 (f) 通过（取到数了） |
| `COUPLE-PREFLIGHT` / `COUPLE-POSTFOLD` | 4 / 4 | 判据 (d)(e) 通过 |
| `COUPLE-REFUSE-STILL-FOLDED` | **0** | ⚠️ 判据 (e) 失败：硬闸门一次没拦到 |
| `INVARIANT-BROKEN-report-this` | 0 | 判据 (e) 字面通过，但因 POSTFOLD 被 SEAM-FLIP 抹平方向，**不构成"链没问题"的证明** |
| `RESPACE-BADCHAIN` | 0 | 判据 (g) 通过 |
| `TTB-PROBE fold-geom` | 0 | 判据 (c) 一半通过（无刷屏卡顿） |
| `CRT-FOLD` | **1** | ⚠️ 判据 (c) 失败：引擎独立检测证实几何仍坏 |
| `COUPLE-OK` | 4 | 前 3 次健康（246/904/2212 行），第 4 次（3445）即病链 |

### 11.3 现场形状

**重试循环**（10 行，全 `v=48 u=6 pair=(50,6) db=0`）：`gap` = 7,6,5,4,4,3,2,1,0,1；`spd` = 120,21,23,23,0,22,23,22,23,23 ⇒ **spd 全程非 0**。机车 48 的 y 从第一次拒绝的 **1015**（`CPL-GEO-DONE ... y=1015`）北推到提交 tick 的 **1006**（`GEO respace-before n=0 veh=48 y=1006`），**10 次拒绝推进 9px，车底 6 一动不动**。第 1 次拒绝同 tick 即 `CPL-HIT ... overlap=1 spd=21 dir=7`。

**提交那一 tick（3395-3401）**：

```
3395:FOLDCHK COUPLE n=21 worst_gap=2 A idx=50 x=936 y=1013 tile=58,63 dir=7 B idx=6 x=936 y=1013 tile=58,63 dir=3 exp=2 nom=2 dist=0
3396:COUPLE-PREFLIGHT head=48 last=50 fold=0 dirfold=0 gap=2 pair=(-1 dir=15, -1 dir=15) mixed=0 onsplice=0 splice=(50 dir=7, 6 dir=3) spd=22 db=0
3398:COUPLE-SEAM-FLIP circ=4 a=50 dirA=7 b=6 dirB=3
3399:GEO respace-before n=0 veh=48 y=1006 ...
3401:GEO respace-before n=2 veh=50 y=1013 ... dist=0 nom=2
```

拼接对 `(50,6)` **同格同位**（x=936 y=1013 完全相同）⇒ `Δ=(0,0)` ⇒ `dot=0`；判据只认 `dot>0` ⇒ **不判折叠**（`pair=(-1 dir=15, ...)` = "没判出任何折叠对"）。闸门与 OVERRIDE 前提同时失明 ⇒ 放行提交。

**提交后的链**：

```
48 y=1004 → 49 y=1008 → 50 y=1011 → 6 y=1013 → 7 y=1008 → 8 y=1003 → … → 23 y=944
                       ↑ 递增        ↑ 在 50→6 处折返
```

`49(1008)` 与 `7(1008)` 完全同格同位、`48(1004)` 与 `8(1003)` 差 1px ⇒ 机车 3 节压在车底前段上。**引擎独立检测**（yapf，与折叠判据不同源）：`3503:CRT-FOLD veh=48 tile=58,62 dir=7 backTile=58,59 rel=0,-3 dot=3`（链尾在车头前方 3px ⇒ 空间折叠）。`COUPLE-POSTFOLD folded=0` 是 SEAM-FLIP（把车底 18 节方向 3→7、位置未动）造成的，正是 10.2-2 预告过的"POSTFOLD 恒为 0、不能当证明"的实例。

### 11.4 机制链（对照第四节 (A)(B)(C)）

- **(B) 前因照旧**：拒绝 ≠ 停车，重试循环把 `worst_gap` 7→2 并一路顶到"拼接对共位"。
- **(A) 换了入口**：第 172 轮是从 KI-174 的 `OVERRIDE` 进（dot=+4 被判出却被丢掉）；本轮 `OVERRIDE` 已被修法 1 关掉，病链改从 **`dot==0`** 进。
- **(C) 掩盖层照旧**：`SEAM-FLIP` 只改方向不动位置 ⇒ 方向线索消失，事后检查全部为"干净"。

⇒ 第 172 轮两条修法**未被证伪**，但它们挡不住"几何本身被顶穿"：**只要无解的重试循环还在推进，判据就永远在追一个移动的靶子**。这就是修法 3「治前因」的定位。

### 11.5 两处实现

1. **拒绝分支下刹车**（`train_cmd.cpp:9038-9062`，新探针 `FOLDCHK-REFUSE-BRAKE`）：在 `FOLDCHK-REFUSE-REAL-ARTIC` 之后、`return false` 之前，取 `Train *const v_front = v->First();`，记 `spd_before`，然后 `cur_speed = 0; subspeed = 0; progress = 0;`。用本文件既有停法（`train_cmd.cpp:15894` 一带的 `consist->cur_speed = 0; consist->progress = 0;`），不引入新的状态复位手段；`cur_speed` 只对前部机车主控有效，故对象取 `v->First()`。**敢下刹车的依据 = 11.3 的 `overlap=1`**：拒绝点必然来自"已经碰上"，机车本来就已够着，刹车不会把它停在够不着的地方。
2. **折叠判据补"共位"一格**（`train_cmd.cpp:7840-7857`）：算完 `dot` 后、比 `worst_dot` 前插入 `const bool pair_co_located = (pair_dist <= 0); if (pair_co_located) dot = std::max(dot, 0) + 1;`。依据：相邻两节**非 artic** 车各自带车长（`cached_veh_length >= 2`），几何上不可能共位，而 artic 成员已在上面按 `IsArticGroupMember` 跳过 ⇒ 共位必是坏链。为不改日志字段，共位按 `dot=+1` 记账。

### 11.6 构建自证

| 项 | 值 |
|---|---|
| 脚本 | 复用既有 `R3R_inc_build_tmp.cmd`（`call vcvars64.bat` → `cmake --build build -j 2`），**未新建任何 `.cmd`** |
| 判决 | `EXIT_CODE=0` |
| 时间戳 | `src\train_cmd.cpp` 22:38 → `build\openttd.exe` **2026-09-30 23:03:14**（51 556 864 B，较第 173 轮 +8 704 B） |
| 日志 | 除 `/showIncludes` 的 `注意: 包含文件` 噪音外，无 `error C*` / `fatal error` / `FAILED:` / `warning` |
| lint | `read_lints`（`src\train_cmd.cpp`）= **0 条** |
| 产物自证 | 在 `build\openttd.exe` 的 ASCII 字节流内检索 **4/4 全中**：`FOLDCHK-REFUSE-BRAKE v=%d front=%d spd_before=%d spd_after=%d db=%d`（本轮新增）、`FOLDCHK-REFUSE-REAL-ARTIC v=%d u=%d`、`COUPLE-PREFLIGHT head=%d`、`COUPLE-REFUSE-STILL-FOLDED` |

### 11.7 复测判据（在 10.5 之上新增/升级）

9. **`FOLDCHK-REFUSE-REAL-ARTIC` 必须紧跟 `FOLDCHK-REFUSE-BRAKE ... spd_before>0 spd_after=0`，且此后机车不再前进**（`CPL-GEO-DONE ... y=` 不再逐 tick 变小）—— 这是「治前因」是否生效的唯一判据。
10. 拼接对共位时 `COUPLE-PREFLIGHT ... fold=` 必须为 **1**（不再是 0），随后应见 `COUPLE-REFUSE-STILL-FOLDED ... -> rolled back, no merge this tick`。
11. 提交后**不得**再出现 `CRT-FOLD ... dot>0`；`GEO settle-couple` 链序必须单调（无 `50→6` 折返）。
12. 回归面：健康挂车（246/904/2212 行）不回退；"链内部 + 同向 + 残差极小"仍能直拼提交（`FOLDCHK-DIR-OVERRIDE` 照旧出现），不被 11.5-2 误伤。

### 11.8 仍未做

- **无实机复测**：本轮只到"编译通过 + 产物自证"，11.7 四条判据全部待游戏内复跑。
- 「六、未确认项」三条（**机车侧为何没被碰撞/停车守卫拦住**这一上游原因、`REFUSE` 回滚是否 100% 还原、KI-174 老场景是否还存在）仍未取证。
- 若复测显示刹车导致"停在挂车距离外挂不上"（10.4 担心的风险），需把刹车细化成"仅当 `CPL-HIT` 已判 `overlap` 时才刹"；本轮现场 10/10 次拒绝都带 `overlap=1`，故暂按无条件刹车落地。

