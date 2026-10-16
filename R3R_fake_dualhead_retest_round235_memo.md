# R3R 假双头（`FakeDualHead`）实机复测收口 —— 第 235 轮（2026-10-10）

> 上游 = 第 234 轮 KI-343（方案与判据见 `R3R_fake_dualhead_round234_memo.md` / `R3R_fake_dualhead_plan.md`）。
> 本轮 = **纯取证，零源码改动、零构建**（玩家原话「目前没有发现有关问题了，请你查看一下」）。
> 输入日志 = `D:\temp\codebuddy-dropped-files\cc3d3954-10b5-4da3-9a72-606bdad91a05\R3R_debug.log`（1965 行 / 171 606 B / 21:17:02）。
> **采样有效性**：日志产生者 = `d:\sourcecode of JGRPP\build\openttd.exe` @2026-10-10 21:03:46（51 738 112 B），与第 234 轮 §五 构建自证**逐字一致**（同一大小、同一 mtime）⇒ 日志确由第 234 轮假双头版 exe 产出，采样有效。

---

## 一、总判定

一句话：**第 234 轮的核心目标（消除 KI-342 的爆雷点）实机通过**；玩家「目前没有发现有关问题了」与日志一致。

| §七 判据 | 内容 | 本轮裁决 |
|---|---|---|
| 1 | 段尾假引擎不再早退（双头后半拿到引擎位 + `FakeDualHead`） | **达成** |
| 2 | 含双头链走逻辑翻后不再 `v_head_not_engine`、`NormaliseSubtypes` 不再断言 | **达成** |
| 3 | 后半节仍不可单独卖 | 未覆盖（需游戏内操作） |
| 4 | 功率/最高速与拆分前一致 | 部分（链属性正常输出，但无挂接前对照值） |
| 5 | 存档往返 `FakeDualHead` 不丢 | 未覆盖（需"授位→存档→读档"闭环） |
| 6 | 负判据：普通双头机车行为逐字不变 | 未覆盖（本日志无双头**非段链**样本） |

---

## 二、判据 1 取证 —— 段尾假引擎不再早退

**直接证据（`FOLD-V` / `FOLD-U` 逐车 dump）：**

- `899:  FOLD-V veh=65 subtype=0x29(front=1 eng=1 wagon=0) … mh=1 rdh=0 omp=66`（前半：有引擎位、非后半）
- `904:  FOLD-V veh=66 subtype=0x28(front=0 eng=1 wagon=0) … mh=1 rdh=1 omp=65`（**后半：`eng=1` 且 `rdh=1`**）
- `1849: FOLD-V veh=67 subtype=0x28(front=0 eng=1 wagon=0) … mh=1 rdh=1 omp=66`
- `910:  FOLD-U veh=62 subtype=0x28(front=0 eng=1 wagon=0) … mh=1 rdh=1 omp=61`
- `1856: FOLD-U veh=62 subtype=0x28(front=0 eng=1 wagon=0) … mh=1 rdh=1 omp=61`

**推理（关键）**：新判据 `IsDualHeadRear() == IsMultiheaded() && (!IsEngine() || HasBit(FakeDualHead))`。
以 `veh=66` 为例：`mh=1` 且 `eng=1`（`IsEngine()` 为真）⇒ `!IsEngine()` 为假 ⇒ `IsDualHeadRear()==1` **只能**来自 `HasBit(FakeDualHead)`。
⇒ **`FakeDualHead` 旗标确已设置**，`SetSegmentTailFakeEngine()` 的旧早退（KI-143，`vehicle_cmd.cpp:698`）确已移除。

**附加一致性**：后半 `subtype=0x28`（`front=0 eng=1`）vs 前半 `subtype=0x29`（`front=1 eng=1`）——只差 FRONT 位 ⇒ 后半"授了引擎位但没顶替链头身份"，与设计口径「授引擎位 + 打旗标、不给 FRONT」逐字吻合。✅

---

## 三、判据 2 取证 —— 双头链逻辑翻后不再爆雷

本日志含**两条**完整的"双头链整链倒置 + 挂接成功"闭环。

### 链路 B（行 895-940）

| 行 | 内容 | 意义 |
|---|---|---|
| 895 | `FOLDCHK COUPLE n=10 worst_gap=8` | 候选0 直拼几何评估 |
| 896 | `FOLDCHK-DIR COUPLE dot=1` | 方向判据评估 |
| 898-905 | `IDENT FOLD-V head=65` 全链 dump（含 904 假双头后半） | 翻转前快照 |
| 906-911 | `IDENT FOLD-U head=61` 全链 dump（含 910 假双头后半） | 翻转前快照 |
| 912 | `A2-FLIP-U` | 走"逻辑翻 u"候选 |
| 913-914 | `DUALHEAD rev-dir-post front=61 … dir=1 … rear=62 … dir=1 … sep=2 adj=0 star=0` | 翻转后两半**朝向一致** |
| 915 | `FLIP-STEP2-DONE nseg=1 whole=1` | **整链倒置** |
| 917 | `OWNER-MOVE from=61 to=62 orders=14 prio=1 borrowed=0` | **排程身份迁到新链头** |
| 918 | `FLIP-DONE head=62 nseg=1 whole=1` | **新链头 62 = 双头后半**（KI-342 爆雷点） |
| 919-922 | `GEO flip-done`（`starF=1@62`、`starB=1@61`） | 段头/段尾身份重建 |
| 931 | `FOLDCHK COUPLE-FLIP-U n=10 worst_gap=8` | 翻后重评 |
| 932 | `COUPLE-PREFLIGHT head=65 last=66 fold=0 dirfold=1 gap=8 … splice=(66 dir=1, 62 dir=1) spd=0 db=1` | **硬闸门 `fold=0` 放行** |
| 933 | `CPL-WAITCLEAR veh=61 head=65 …` | 等待者被并入 |
| 935 | `RESPACE-AFTER-EDIT couple head=65 … unfixed=0 skip=0 front=0` | 几何重排无残留 |
| 940 | `COUPLE-OK loco=65 rear=61 consist=61 co=0 real=1 type=1` | **挂接成功** |

### 链路 D（行 1841-1887）

同构闭环：`1841 FOLDCHK COUPLE` → `1844-1849 IDENT FOLD-V head=66` → `1851-1856 IDENT FOLD-U head=61` → `1858 A2-FLIP-U` → `1859-1860 DUALHEAD rev-dir-post`（`dir=1/1` 一致）→ `1861 FLIP-STEP2-DONE nseg=1 whole=1` → `1863 OWNER-MOVE from=61 to=62` → `1864 FLIP-DONE head=62 nseg=1 whole=1` → `1878 FOLDCHK COUPLE-FLIP-U` → `1879 COUPLE-PREFLIGHT head=66 last=67 fold=0` → `1880 CPL-WAITCLEAR` → `1882 RESPACE-AFTER-EDIT … unfixed=0` → `1887 COUPLE-OK loco=66 rear=61 consist=61`。

### 关键否定证据（全 0 命中）

| 检索串 | 命中 | 说明 |
|---|---|---|
| `v_head_not_engine` | **0** | 候选2 守卫 |
| `give up this tick` | **0** | 候选3 守卫 |
| `ssert` / `Assertation` | **0** | `NormaliseSubtypes()` 断言未触发 |
| `crash` / `Segmentation` / `abort` | **0** | 无崩溃 |
| `COUPLE-REFUSE-STILL-FOLDED` | **0** | 提交前硬闸门未被触发 |

> **排除假阴性**：源码字面量确实存在 —— `train_cmd.cpp:10135`（候选2 `fall through`）与 `train_cmd.cpp:10196`（候选3 `give up this tick`）。0 命中只能是"这两个放弃分支真的没被走到"。

### 翻转后健康度

- `1926-1935 GEO settle-couple head=66`：段 11 = `66,68,69,70,67`（`starF=1@66` / `starB=1@67`）；段 12 = `62,63,65,64,61`（`starF=1@62` / `starB=1@61`）；全程 `dist=0 nom=8`，链尾 `dist=-1` 正常。
- `1915 CHAIN-ATTRS head=66 n=10 FE=2 SEG=2 pow=7000 wt=499 len=80 spd=160`
- `1941 CHAIN-ATTRS2 couple head=66 len=80 pow=7000 wt=499 spd=160 nocab=0 db=0`
- `1898 COUPLE-POSTFOLD head=66 folded=0`
- `1899 ORD-AFTER-COUPLE head=66 n=14 real=1 impl=1 tt=1 co=0 borrowed=1 owner=62 u_has_orders=0`

---

## 四、判据 6 的弱旁证（非实测）

本日志所有**非双头车厢**逐行 `mh=0 rdh=0 omp=-1`（`900-903` / `908-909` / `1846-1848` / `1853-1855`）⇒ `IsDualHeadRear()` 对普通车厢恒 0，与旧 `IsRearDualheaded()` 一致。

但**未授位的普通双头机车**（非 R3R 段链）本日志**零样本** ⇒ 判据 6 只能靠静态等价（新版判据对未授位双头 = `IsMultiheaded() && !IsEngine()` = 原 `IsRearDualheaded()`，逐字相同）**断言**，不能称"实测通过"。

---

## 五、新发现：KI-334 的 `sep` 判读口径需要修订

本日志首次提供了第 228 轮 §1.3 明确索要的样本 ——「**含双头机车的链真的走了一次逻辑翻**」（第 232 轮该出口为零样本）：

- `913` / `1859` `DUALHEAD rev-dir-post`（**逻辑翻出口**，首次有样本）
- `936-939` / `1883-1886` `DUALHEAD respace-post`

按 KI-334 第 228 轮登记的判读口径（`R3R_KNOWN_ISSUES.md:10666`）："`dir_f != dir_r` **或 `sep != 0`** ⇒ 逻辑翻/物理翻破坏了双头刚性"。

**实测**：`dir_f == dir_r` **全部成立**（913：1/1；1859：1/1；936-939：5/5；1883-1886：5/5），`orphan` 零命中，`star=0`；但 **`sep` 恒非 0**（2 / 3 / 4）。

**口径修订理由**：第 234 轮的方案前提就是「把双头**拆成单头**」（两半各自具备引擎身份、用 `FakeDualHead` 记事实）。R3R 段机制要求"段的两端必须是引擎/假引擎"，而双头后半是段尾唯一可授位的对象 ⇒ **两半分列段首/段尾、中间夹段内车厢，正是新语义的预期形态**（`sep>0 adj=0` 不再等价于"刚性破坏"）。

**新语义下应保留的判据只剩两条**：
- `dir_f != dir_r`（两半朝向必须一致，否则视觉/物理错位）
- `orphan=1`（配对丢失）

本日志两条全部健康 ⇒ **KI-334（"双头机车挂接时机车错位"）在本样本上未被复现**；但 KI-334 本体仍未收口（玩家尚未就"`sep>0` 的视觉表现是否可接受"表态）。

---

## 六、存量观察（与本轮改动无关，勿混判）

- `NOABSORB-SEG veh=61 n=0 len=4/5` 反复出现（`375/385/411/…/1950` 共数十行），与第 232 轮 `NOABSORB-SEG veh=61 n=0 len=4` 同族 ⇒ **KI-253 存量**。
- `1917 SEG idx=62 pow=0 wt=0 len=0 spd=0`：**不是缺陷**。探针（`train_cmd.cpp:11337-11343`）打的是"每个段头车自己的 `gcache`"，而 `cached_power/weight/total_length` 只由链头在 `ConsistChanged()` 中填充 ⇒ 非链头的段头 62 恒为 0，属预期。
- `1925 INVAR-CTRL tag=couple head=66 nseg=2 ctrl=62 ctrl_pri=1 head_pri=2 borrowed=1`：控制段(62) ≠ 链头段(66) ⇒ 第 152 轮 P1「控制段 == 链头所在段」仍处"借用兜底"形态（KI-254 待拍板项），与本轮无关。
- `SKIP-STOPPED veh=22 order=17`（`OT_WAIT_COUPLE`）/ `veh=23 order=16`（`OT_GOTO_COUPLE`）：等待/前往挂接的正常停驻。

---

## 七、仍未覆盖 / 建议补测

1. **判据 3**：拆掉/解出双头的一个半节后**尝试单独出售**该半节 ⇒ 应被拒（需游戏内操作，日志无法覆盖）。
2. **判据 4**：采**同一条双头链挂接前后的 `CHAIN-ATTRS2` 对照**（本日志只有挂接后的 `1915`/`1941`，无前值）。U-2 静态结论 = `CalculatePower()` 逐车累加 + `GetPower()` 对 `IsMultiheaded()` 减半 ⇒ 补引擎位不改变总计；建议实测同一列车挂接前后各读一次 `pow/spd`。
3. **判据 5**：走通"**授位 → 存档 → 读档 → 看 `FOLD-V/DUALHEAD` 的 `rdh` 是否仍为 1**"闭环。本日志仅开头有一次 `tag=load`（行 22-58），无法区分"位是本次会话新授"还是"跨存档带入"。
4. **判据 6**：造一条**普通（非 R3R 段）双头机车**跑一次挂接/翻转，比对行为逐字不变。

---

## 八、结论

第 234 轮 KI-343 的**核心目标（消除 KI-342 的 `v_head_not_engine` / `NormaliseSubtypes` 断言爆雷）实机通过**：

- 双头链整链倒置后新链头落在双头后半（62），**两条翻转守卫与 `NormaliseSubtypes()` 全部自然放行**；
- `COUPLE-OK` 正常达成（两条独立闭环），链路几何 / 段结构（两段、段头段尾 ★⊗ 齐备）/ 排程身份迁移（`OWNER-MOVE`）全部健康；
- **0 崩溃 / 0 断言 / 0 拒绝 / 0 放弃守卫命中**。

与玩家「目前没有发现有关问题了」逐字一致。剩余风险集中在**未被日志覆盖的判据 3/4/5/6**（出售拒绝、功率回归、存档往返、普通双头负判据），以及 **KI-334 的 `sep` 判读口径需按"拆成单头"新语义修订**这一文档级遗留。
