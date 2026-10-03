# R3R 段特质（号/名/分组）丢失与乱继承 —— 第 192 轮取证备忘

- 日期：2026-10-01
- 现场日志：`build\R3R_debug.log`（纯净版，玩家提供）
- 关联探针轮次：第 190/191 轮（SEGTR-SNAP / SEGSAVE-PARK / SEGLOAD-PARK / SEGTRAIT-RECOVER / DEPOT-XFER-ID / INVAR-CTRL）
- 玩家报告：①「名称被清空」②「名称（及号）乱继承到别的段」
- 本轮性质：**纯取证 + 根因定位**，未改任何源码。

---

## 一、上一轮（探针）修正是否生效 —— 结论：全部生效

| 探针 | 日志命中 | 判读 |
|---|---|---|
| `SEGTR-SNAP` | 129 行 | 六个提交点（load-raw / depot-edit / couple / decouple-v / decouple-u / load）全部有快照 |
| `SEGSAVE-PARK` | 24 行 | veh=0..23 三件套（unit/ubk/nbk/gid/gbk/seg/borrowed/prio）全列出 |
| `SEGLOAD-PARK` | 24 行 | 与 SEGSAVE-PARK **逐字段完全一致** |
| `SEGSAVE-ROW` / `SEGLOAD-ROW` | 各 2 行 | 段行（row）也进了存档往返 |
| `SEGTRAIT-RECOVER` | 1 行 | 兜底回收只在必要点触发一次，未刷屏 |
| `INVAR-CTRL` | 10 行 | 每个提交点都校验了 ctrl/prio 不变式 |
| `DEPOT-XFER-ID` | 9 行 | 车库拖动边每条都留痕 |

**存档往返自证（关键对照）**

```
226:SEGSAVE-PARK veh=0 unit=2 ubk=1 n="T8701/2-国际段1" nbk=""      gid=65534 gbk=-1    seg=1 borrowed=1 prio=2
252:SEGLOAD-PARK veh=0 unit=2 ubk=1 n="T8701/2-国际段1" nbk=""      gid=65534 gbk=-1    seg=1 borrowed=1 prio=2
232:SEGSAVE-PARK veh=6 unit=0 ubk=2 n=""               nbk="T8701/2-国际段1" gid=65534 gbk=65534 seg=2 borrowed=0 prio=1
258:SEGLOAD-PARK veh=6 unit=0 ubk=2 n=""               nbk="T8701/2-国际段1" gid=65534 gbk=65534 seg=2 borrowed=0 prio=1
```

⇒ **存档往返没有丢身份**（同名 side 的 `nbk`、`gbk`、`borrowed`、`prio` 全对齐）。
⇒ 因此「名称变空」不是存档格式问题，是**运行期某一提交点把它覆盖掉了**。

---

## 二、KI-294「链头自己的名称被清空」—— 根因已锁定

### 2.1 现场锚点

读档最初时刻（未合并的旧档）：

```
2:SEGTR-SNAP   i=1 id=0 front=0 star=1 ctrl=1 | live u=1 n="T8701/2-国内段" gid=65534 | bk u=0 n="" gbk=-1 | row none
6:SEGTR-SNAP   i=1 id=0 front=6 star=1 ctrl=1 | live u=2 n="T8701/2-国际段1" gid=65534 | bk u=0 n="" gbk=-1 | row none
```

⇒ 此刻 **车 0 自己的名字是 "T8701/2-国内段"**，备份为空。

车库拖动（把 0..5 块与 6..23 链合并）： 

```
178:DEPOT-BEFORE-HEAD-RANK head=6 tmax=1 amin=3
179:DEPOT-BEFORE-HEAD sel=3 head=6 block=0 tail=5
189:UNIT-PARK veh=6 bk=2
190:GRP-PARK  veh=6 g=65534
191:DEPOT-XFER-ID depot-edit head=0 ctrl=6 unit=2 own_bk=1 hasname=1 own_name=0     <<< 现场
196:SEGTRAIT-SYNC tag=depot-edit head=0 seg=2 unit=2 group=65534 borrowed=0 ctrl=6
197:INVAR-CTRL    tag=depot-edit head=0 nseg=2 ctrl=6 ctrl_pri=1 head_pri=2 borrowed=1
```

settle 后的段快照：

```
224:SEGTR-SNAP   i=1 id=1 front=0 star=1 ctrl=0 | live u=2 n="T8701/2-国际段1" gid=65534 | bk u=1 n="" gbk=-1 | row use u=1 n="" g=-1
225:SEGTR-SNAP   i=2 id=2 front=6 star=1 ctrl=1 | live u=0 n="" gid=65534 | bk u=2 n="T8701/2-国际段1" gbk=65534 | row use u=2 n="T8701/2-国际段1" g=65534
```

**现场签名 = 「号在、名丢」**：

- 车 0 的 `unitnumber_backup = 1`（自己的号 **保住**了）→ 行 191 `own_bk=1`
- 车 0 的 `name_backup = ""`（自己的名 **丢了**）→ 行 191 `own_name=0`、行 224 `bk u=1 n=""`
- 车 0 的活名已被换成控制段 6 的名 → 行 224 `live u=2 n="T8701/2-国际段1"`

然后存档落盘，把这个丢失状态写死：

```
226:SEGSAVE-PARK veh=0 unit=2 ubk=1 n="T8701/2-国际段1" nbk="" gid=65534 gbk=-1 seg=1 borrowed=1 prio=2
```

⇒ 「T8701/2-国内段」在这条链上**已经不存在于任何字段**（活名被覆盖、备份为空）⇒ 永久丢失。

### 2.2 根因：`R3RBorrowControlTraitsLive()` 的名门禁在车库路径**恒真**

代码：`src\train_cmd.cpp:5729` `R3RBorrowControlTraitsLive(Train *chain, const char *tag)`。
调用点：`R3RSyncChainAfterDepotEdit()` 内的 `src\train_cmd.cpp:5798-5804`：

```
5798: R3RRenumberPriorities(chain);
5799: R3RSyncDrivingOrders(chain, false);      <<< 先把 chain->r3r_orders_borrowed 置为 true
...
5804: R3RBorrowControlTraitsLive(chain, "depot-edit");
```

`R3RBorrowControlTraitsLive()` 内部两块**门禁不对称**：

- **号侧（无借用门禁）**：
  ```
  if (ctrl->unitnumber_backup != 0 && chain->unitnumber != ctrl->unitnumber_backup)
  {
      if (chain->unitnumber_backup == 0 && chain->unitnumber != 0)
          chain->unitnumber_backup = chain->unitnumber;   // ← 无条件停放自己的号
      chain->unitnumber = ctrl->unitnumber_backup;
      changed = true;
  }
  ```
- **名侧（有借用门禁）**：
  ```
  if (!keep_name && !ctrl->name_backup.empty())
  {
      if (!R3RNameIsBorrowed(chain) && chain->name_backup.empty())
          chain->name_backup = chain->name;               // ← 被门禁挡掉，从不执行
      if (!(chain->name == ctrl->name_backup.c_str()))
      {
          chain->name = ctrl->name_backup;                // ← 直接覆盖 ⇒ 自己的名字蒸发
          changed = true;
      }
  }
  ```

`R3RNameIsBorrowed(v)`（`src\train_cmd.cpp:2773`）的定义：

```
v->r3r_orders_borrowed && v->name_backup.empty() && !v->name.empty()
```

进入 `R3RBorrowControlTraitsLive` 时，三个条件**恒成立**：

1. `chain->r3r_orders_borrowed == true` —— 刚刚被 5799 行的 `R3RSyncDrivingOrders(chain,false)` 置真（链头确实借控制段的排程）；
2. `chain->name_backup.empty() == true` —— 链头此前一直是链头，名没被任何人借走过；
3. `!chain->name.empty() == true` —— 身上挂着的就是**它自己的**名（"T8701/2-国内段"）。

⇒ `R3RNameIsBorrowed(chain)` 返回 true ⇒ 第 1 行停放语句被跳过 ⇒ 第 2 行把活名直接覆盖 ⇒ **自己的名字既没进备份、也没了**。

**设计缺陷一句话**：「排程借用」与「名字借用」共用 `r3r_orders_borrowed` 一个标志，而车库合并路径里前者**必然**先被置真，于是后者被误判为「身上这件名字是借来的」，保护对象错位。

⇒ 第 185 轮 KI-280 给 `R3RParkTrainName()` 加借用门禁，方向是对的，但在**车库合并首次借名**这个场景里，保护条件本身恒真（因为「自己的名」与「借来的名」在判据上无法区分），门禁反而变成了「把好名字扔掉」的开关。

号侧因为没有这个门禁，所以号**完好** ⇒ 现场呈现「号在、名丢」这个非常有辨识度的签名。

### 2.3 修法（最小、与号侧同构、幂等）—— **已于第 192 轮落地并编译通过**（见 §八）

不要用 `R3RNameIsBorrowed()` 当门禁，改用**与借入值比对**：

```cpp
if (!keep_name && !ctrl->name_backup.empty())
{
    // 与"将要借入的名字"不同 ⇒ 身上这件是我自己的 ⇒ 先停放
    if (chain->name_backup.empty() && !(chain->name == ctrl->name_backup.c_str()))
        chain->name_backup = chain->name;
    if (!(chain->name == ctrl->name_backup.c_str()))
    {
        chain->name = ctrl->name_backup;
        changed = true;
    }
}
```

理由：第 2 行本来就以「是否等于借入值」为判据（幂等的），把第 1 行统一到同一个判据上即可 —— 首次进来（名是自己的）自动停放；第二次进来（名已是借来的）自动跳过，不再误停。

号侧建议同步收紧为 `chain->unitnumber_backup == 0 && chain->unitnumber != 0 && chain->unitnumber != ctrl->unitnumber_backup`（防"自己号恰好等于控制段号"的极端情形）。

**替代/加固方案**：给「名字借用」单开一个标志位（如 `r3r_name_borrowed`），与 `r3r_orders_borrowed` 解耦；或让 `R3RNameIsBorrowed()` 加一条「链上确实存在另一辆车/段行持有同名」的判据。代价与改动面都大于上面的最小修法，建议列为后续收口。

---

## 三、KI-295「名称/号乱继承到别的段」—— 现场已抓到

### 3.1 现场锚点

couple（机车 29 挂 0..23 车底链）settle 之后：

```
6237:SEGTR-SNAP   i=1 id=1 front=29 star=1 ctrl=0 | live u=2 n="T8701/2-国际段1" gid=65534 | bk u=4 n="" gbk=-1 | row use u=4 n="" g=-1
6238:SEGTR-SNAP   i=2 id=2 front=23 star=1 ctrl=1 | live u=0 n="T8701/2-国际段1" gid=65534 | bk u=2 n="T8701/2-国际段1" gbk=-1 | row use u=2 n="T8701/2-国际段1" g=65534
6239:SEGTR-SNAP   i=3 id=3 front=5  star=1 ctrl=0 | live u=0 n=""               gid=65534 | bk u=2 n="T8701/2-国际段1" gbk=-1 | row use u=2 n="T8701/2-国际段1" g=65534
```

⇒ **段 2（front=23）与段 3（front=5）的 `bk` 与 `row` 逐字段完全相同**（`u=2`、名同为 "T8701/2-国际段1"、`g=65534`）。
⇒ 段 3 本来应该是「国内段」（车 0..5 那一段），现在它的段行也显示成 "T8701/2-国际段1" ⇒ 玩家看到的**「名称乱继承到别的段」**：两段同名同号。

回收动作本身有留痕：

```
6205:SEGTRAIT-RECOVER seg=5 unit=2 group=65535 name=1
6206:SEGTRAIT-SYNC-SEC tag=couple head=29 seg=3 unit=2 group=65534 parkedname=1
```

### 3.2 机制链条（**R-1 复核完成，下表已修正一次**）

#### R-1 复核结论（推翻初稿的表述）

初稿写「`Couple()` 名块对被挂链头 `u` 侧**未设** `R3RNameIsBorrowed(u)` 门禁」—— **这是错的**。
实测 `src\train_cmd.cpp:10423`（`Couple()` 的 NAME-XFER 名块，第 160 轮 KI-261 新增）**已经有**门禁：

```cpp
10422: const bool head_had_own = !v->name.empty() && !was_borrowing;
10423: if (!R3RNameIsBorrowed(u) && u->name_backup.empty()) u->name_backup = u->name;
10424: if (head_had_own && v->name_backup.empty()) v->name_backup = v->name;
10425: v->name = u->name;
```

但该门禁在本场景**不会挡住停放**：`u`（等待车底链）此刻 `r3r_orders_borrowed == 0`（它不是借用方，而是控制段的持有人/独立链头），于是 `R3RNameIsBorrowed(u)` 返回 **false** ⇒ 门禁放行 ⇒ 活名照停。
真正的病根不在"门禁缺失"，而在 **`u` 身上那件名字本身就已经是一件"滞留的借名"（stale borrow）**，而 `R3RNameIsBorrowed()` 只查 `r3r_orders_borrowed`，**没有任何手段识别滞留借名**。

#### 修正后的完整链条（每步都有日志锚点）

1. **借名覆盖（KI-294）**：行 191 depot-edit 把「T8701/2-国内段」覆盖成「T8701/2-国际段1」且没留备份（行 224 `bk n=""`）。
2. **借用结束却不归还（滞留点，本轮新定位）**：行 1607
   ```
   1607:SEGTR-SNAP   i=1 id=0 front=0 star=1 ctrl=1 | live u=7 n="T8701/2-国际段1" gid=65534 | bk u=0 n="" gbk=-1 | row none
   1606:SEGTR-SNAP tag=decouple-u head=0 nseg=1 ctrl=0 borrowed=0
   ```
   `borrowed` 已回到 **0**（借用关系结束），但活名仍挂着借来的「国际段1」、备份为空。
   归还端（`DecoupleTrain()` 的 NAME-RESTORE 块 / `R3RRestoreTrainName()`）**只在 `name_backup` 非空时归还**，备份是空的 ⇒ 没东西可还 ⇒ **借名永久滞留在车 0 的活字段里**，"以别人的名字当自己的名字"活着。
3. **滞留借名被合法停放**：行 5039（机车 27 挂等待车底链 0..5）
   ```
   5039:NAME-XFER head=27 u=0 head_own=0 got_u=1 keep_u=1 borrowed=0 own=0
   5079:SEGTR-SNAP   i=2 id=3 front=0 ... live u=0 n="T8701/2-国际段1" | bk u=7 n="T8701/2-国际段1" | row use u=7 n="T8701/2-国际段1"
   5065:SEGTRAIT-SYNC tag=couple head=27 seg=3 unit=7 group=65534 borrowed=0 ctrl=0
   ```
   `borrowed=0` 证实 `u` 不处于借用态 ⇒ `:10423` 门禁放行 ⇒ 行 5079 的 `bk` 与 1607 行的 `live` **逐字段相同**（`u=7`、`n="国际段1"`）⇒ 停放确实发生在这一次 couple。
4. **段行污染**：段 3 = 车 5,4,3,2,1,0（行 6229-6234 的 GEO dump 实测段头是 5，0 在段尾），是**非控制段** ⇒ 由 `R3RSyncHiddenSegmentTraits()`（`:5093`）负责写行，而它的取源规则是**优先备份、无备份才退回活值**：
   ```
   5122: const bool have_parked_name = !seg->name_backup.empty();
   5130: if (have_parked_name ? !(seg->name_backup == row->name.c_str()) : !(seg->name == row->name.c_str())) {
   5131:     row->name = have_parked_name ? ... name_backup ... : ... name ...;
   ```
   行 6205/6206 的 `SEGTRAIT-RECOVER seg=5 unit=2 group=65535 name=1` + `SEGTRAIT-SYNC-SEC ... seg=3 unit=2 group=65534 parkedname=1` 就是它命中 `have_parked_name=1` 的留痕 ⇒ 段 3 行被写成「国际段1/号 2」，与段 2（控制段，由 `R3RSyncSegmentTraits()` 从链头的借用活字段写）的 `u=2 n="国际段1" g=65534` **逐字段相同**（行 6239 == 行 6238）。
   （校验：`u=2` 与 `g=65534` 也对得上——`unit` 来自 `unitnumber_backup=2`、`group` 因 `group_id_backup` 无效而退回活值 `group_id=65534`，规则见 `:5120-5121`。）

⇒ **结论修正：KI-295 不是独立缺陷，是 KI-294 的完整下游连带。** 病根是同一个"把借来的名当自己的名"：
- KI-294 = 在**承租方（链头）**侧覆盖 ⇒ 丢失；
- 中段（本备忘新增）= 借用结束时**无备份可归还** ⇒ 借名滞留成"自己的活名"；
- KI-295 = 在**出租方（等待方 u）**侧把这件滞留借名合法寄存，再被段行镜像 ⇒ 污染。

**KI-294 修好之后**：链头首次借名会自动寄存自己的名 ⇒ 借用结束能归还 ⇒ 不再有滞留借名 ⇒ 段 3 会写回「T8701/2-国内段」⇒ 段 2/段 3 不再同名同号。

**残留的第二条通路（建议顺手加守卫，非必须）**：`:5130-5131` 的"没有备份就退回活名"分支会把**借用中的活名**直接写进隐藏段行（行 583 `bk u=1 n=""` 而 `row use u=1 n="国际段1"` 就是这一支），建议加一句「`R3RNameIsBorrowed(seg)` 为真时不写名字」。KI-294 修好后该分支取到的是自己的名，属加固项。


---

## 四、附：观察项（暂不定性）

### 4.1 车 6 的 `group_id_backup` 在读档后变 Invalid

```
258:SEGLOAD-PARK veh=6 ... gbk=65534 seg=2 borrowed=0 prio=1      （读档瞬间：还在）
280:SEGTR-SNAP  i=2 id=2 front=6 ... bk u=2 n="T8701/2-国际段1" gbk=-1   （load-raw 快照：已变 -1）
```

⇒ 读档流程里有一次「备份==活值 ⇒ 清备份」的清理。
**R-2 复核结论（本轮已确认，全代码库只有两处会把 `group_id_backup` 写成 Invalid）**：

| 出处 | 条件 | 本场景是否命中 |
|---|---|---|
| `src\train_cmd.cpp:2842`（`R3RRestoreTrainGroupID()` 成功分支末尾） | 取回后清备份 | 否（车 6 不是链头，不会被调用） |
| `src\train_cmd.cpp:2852`（同一函数 `group_id_backup` 为 Invalid 时的收尾） | 无备份可取 ⇒ 保证字段归位 | 否（同理） |
| `src\sl\couple_group_sl.cpp:228`（`Ptrs_R3VP()` 读档清洗） | `if (v->group_id_backup == v->group_id) v->group_id_backup = GroupID::Invalid();` | **是 —— 就是这一处** |

行 258（`gbk=65534`，R3VP 刚读出）→ 行 280（`gbk=-1`，指针修正阶段跑完 `Ptrs_R3VP()` 之后）的翻转点正好落在读档清洗内，与车 6 是否为链头**无关**（该清洗对全池每辆车都跑）。
因为车 6 的 `group_id == group_id_backup == 65534`（它是控制段，链的分组本来就归一化到它的组），**清掉是无害的**（值相同，取回与否结果一致）。

**R-2 顺带查清的一件相关事**：`R3RBorrowControlTraitsLive()`（`:5729`）**只处理号与名两个特质，根本不碰分组** —— 所以链头自己的分组从来不会被寄存（全日志 `GRP-PARK` 只有 veh=6 一条、没有 veh=0 的），这解释了行 224 `row g=-1`（段 1 自己的组本来就是 Invalid，不是被清掉）。
⇒ 定性：**不是缺陷**。KI-296 可以收口。

### 4.2 段 3 的 `group` 一直显示 65534

行 6239 段 3 的 `row g=65534` ⇒ 段行分组取的是**链统一值**（控制段的组），而不是段 3 自己的组 —— 与「段行是链头活字段的镜像」设计一致（第 155/158 轮 KI-254/246 的已知口径），本轮不改。

---

## 五、结论汇总

| 编号 | 一句话 | 状态 | 严重度 |
|---|---|---|---|
| **KI-294** | 车库合并借名时 `R3RNameIsBorrowed(chain)` 恒真 ⇒ 链头自己的名字被覆盖且不留备份 ⇒ 永久丢失（号在名丢） | **已修（第 192 轮）**：源码已改 + 增量编译通过，**游戏内复测待做**（见 §八） | 高（丢数据） |
| **KI-295** | **KI-294 的下游连带**：借名覆盖后因无备份而**滞留**在链头活字段里（借用期结束也不归还）⇒ 它下次作为等待方 `u` 时被 `Couple():10423` 合法寄存（门禁查不出滞留借名）⇒ `R3RSyncHiddenSegmentTraits()` 优先用备份写隐藏段行 ⇒ 段与段同名同号 | **随 KI-294 一并修复**（已修 + 已编译，待复测；未给 `:10423` 另加门禁——见 §七.2） | 中（行为不符） |
| **KI-296** | 车 6 `gbk` 读档后 65534→Invalid | 已定位 = `couple_group_sl.cpp:228` 读档清洗，判定无害，可收口（R-2 已确认） | 低（观察项） |

**上一轮修正结论**：探针全部生效；存档往返自证通过；`SEGTRAIT-RECOVER` 未误触发刷屏。
**本轮源码改动（二次修订后追加）**：仅 `src\train_cmd.cpp` 的 `R3RBorrowControlTraitsLive()` 名侧停放门禁一处（见 §八），已增量编译通过。R-1/R-2 复核本身未改源码。

---

## 六、复测判据（修 KI-294 之后）

1. 同样场景（未合并旧档 + 车库拖动合并）⇒ 应出现 `DEPOT-XFER-ID depot-edit head=0 ctrl=6 unit=2 own_bk=1 hasname=1 own_name=**1**`（`own_name` 由 0 变 1）。
2. `SEGTR-SNAP tag=depot-edit` 的 `i=1 front=0` 行应为 `bk u=1 n="T8701/2-国内段"`（备份里能看到自己的名字）。
3. 该状态存档再读回 ⇒ `SEGSAVE-PARK veh=0 ... nbk="T8701/2-国内段"` 且 `SEGLOAD-PARK` 一致（不再为空）。
4. 解挂 / 链头换人后 ⇒ 出现 `NAME-RESTORE veh=0 side=head` 且界面名回到「T8701/2-国内段」。
5. KI-295 连带验证：couple head=29 之后 `SEGTR-SNAP` 中 `i=2 front=23` 与 `i=3 front=5` 的 `bk n=` **不再相同**（段 3 应为空或为自己的名）。
6. 反例不误伤：健康链（链头名从未被覆盖）重复进入 `R3RBorrowControlTraitsLive` ⇒ 幂等，`own_name` 只在首次为 1，第二次不再改写、不产生重复备份。

---

## 七、补记：R-1 / R-2 复核（同轮二次修订，**改的是分析结论，未改源码**）

### 7.1 R-1：`Couple()` 名块门禁"已存在但不生效"

- **实测**：`src\train_cmd.cpp:10423` 已有 `if (!R3RNameIsBorrowed(u) && u->name_backup.empty()) u->name_backup = u->name;`。初稿"未设门禁"**错误**，已改。
- **为什么不生效**：本场景 `u`（等待车底链头）`r3r_orders_borrowed == 0`（行 5039 `borrowed=0`），它不是借用方 ⇒ `R3RNameIsBorrowed(u)` 为 false ⇒ 放行。
- **真正的病根**：`R3RNameIsBorrowed()`（`src\train_cmd.cpp:2773`）
  ```cpp
  return v->r3r_orders_borrowed && v->name_backup.empty() && !v->name.empty();
  ```
  只能表达"**当前正在借用且手上有名**"，无法表达"**手上这件名是早先借来、借用关系已结束但没还**"。后者是 KI-294 制造的**滞留借名（stale borrow）**。
- ⇒ 三个停放点（`R3RBorrowControlTraitsLive():5762`、`Couple():10423`、`R3RParkTrainName()`）用的都是同一个判据。**单靠加门禁修不了**，必须先消灭"滞留借名"这个状态（即修 KI-294 让借用结束能归还）。

### 7.2 R-1 附带发现：KI-294 与 KI-295 共享同一个"滞留"环节

初稿认为 KI-294 是"覆盖丢失"、KI-295 是"复制污染"，两件事。复核后应改为**一条三段链**：

```
[depot-edit 借名]  :5762 门禁恒真 ⇒ 自己的名被覆盖且不留备份   ← KI-294（丢失）
        ↓
[decouple 还名]    name_backup 为空 ⇒ 没有东西可还 ⇒ 借名滞留   ← 新增环节（本备忘 3.2 第 2 步，行 1607 实证）
        ↓
[couple 寄存]      :10423 门禁查不出滞留借名 ⇒ 合法寄存 u 的活名  ← KI-295（污染）
        ↓
[段行镜像]         :5122/:5130-5131 优先用备份写隐藏段行          ← 污染可见化（行 6239）
```

⇒ 修 KI-294 即可同时消灭 KI-295（判据 5），不需要给 `:10423` 另加门禁。

### 7.3 R-2：`group_id_backup` 清理点（已确认可收口）

见 §4.1 表格。真正的清除点是 `src\sl\couple_group_sl.cpp:228`（`Ptrs_R3VP()` 读档清洗），不是 `R3RRestoreTrainGroupID()`。车 6 的原值与备份值相同 ⇒ 无害 ⇒ **KI-296 判定为"非缺陷（设计内的字段归一）"**。

### 7.4 仍未做 / 仍待确认

- **R-3（新）**：行 1607 的 `live u=**7**`（不是 2）说明 191→1607 之间还发生过一次号侧互换；**号侧**的滞留/归还链路未逐点核对（名侧已闭环）。若将来复测出现"号对了名不对/反之"，优先查这一段。
- **R-4（新）**：`R3RRecoverSegmentTraits()` 的"从段内抢第一件非空备份"语义（`:5116-5128`）本身与"段头换人"绑定（`R3RMoveSegmentOwner` 交换段头时把备份留在旧段头、再由它搬到新段头）。本场景它是**被动承受**污染（不是制造者），但若一个段内同时存在多辆车带备份，取谁是不确定的 ⇒ 未定性的边界，记此备查。
- 修法（`R3RBorrowControlTraitsLive():5762` 门禁改为与"将要借入的值"比对）**已落地并编译通过**（见 §八）。
- `R3RSyncHiddenSegmentTraits():5130-5131` 的加固项经复核**判定不必加**（见 §八.3）。

---

## 八、源码落地记录（第 192 轮，2026-10-01）

### 8.1 改动（仅 1 处，仅 .cpp）

`src\train_cmd.cpp` 的 `R3RBorrowControlTraitsLive()`（函数起点 `:5729`）—— 列车名侧的**停放门禁**：

```cpp
/* 旧（恒假 ⇒ 链头自己的名永不寄存，随后被直接覆盖） */
if (!R3RNameIsBorrowed(chain) && chain->name_backup.empty()) chain->name_backup = chain->name;

/* 新（与号侧 5751-5755 逐行同构：与"将要借入的值"比对） */
if (chain->name_backup.empty() && !chain->name.empty() && !(chain->name == ctrl->name_backup.c_str())) {
    chain->name_backup = chain->name;
}
```

紧接的赋值语句与整块外层门禁（`!keep_name && !ctrl->name_backup.empty()`）**一字未动**，于是：
- 首次进入（身上是自己的名 ≠ 将要借的名）⇒ 自动寄存 ✓
- 二次进入（已在穿借来的名 == 将要借的名）⇒ 跳过，不会把借名停成自己的名（第 185 轮 / KI-280 的意图由此保留）✓
- 幂等 ✓

`R3RNameIsBorrowed()` 的其他三个调用点（`:3123`、`:8948`、`:10423`）未动、函数本身未改 ⇒ 不会产生"未使用函数"告警。

### 8.2 构建自证

| 项 | 值 |
|---|---|
| 护栏 | `GUARD: incremental is safe (no header/lang file is newer than the newest object)` |
| 日志尾 | `[3/3] Linking CXX executable openttd.exe` |
| 判决 | `build\R3R_incbuild.done` = `EXIT_CODE=0` |
| 错误计数 | `error C*` / `fatal error` / `FAILED:` / `build stopped` = **0** |
| 时序 | `src\train_cmd.cpp` 23:17:34 → `train_cmd.cpp.obj` 23:18:56 → `build\openttd.exe` 2026-10-01 23:20:55（51 608 064 B） |
| lint | `read_lints` 0 条 |
| 探针 | **未新增**（沿用既有 `DEPOT-XFER-ID ... own_name=` / `SEGTR-SNAP tag=depot-edit` 即可验证） |

### 8.3 复核后"不必做"的三项（记录以免后人重复踩）

1. `R3RSyncHiddenSegmentTraits():5130-5131` 加「`R3RNameIsBorrowed(seg)` 为真则不写名字」守卫 —— **不必加**。KI-295 现场被污染的隐藏段是**被挂链头 `u`**，其 `r3r_orders_borrowed == 0`（它不是借用方）⇒ `R3RNameIsBorrowed(seg)` 恒假，守卫根本不会触发；该判据只能识别"正在借用"，识别不了"滞留借名"。真正的堵点是 KI-294。
2. `Couple():10423` 另加门禁 —— **不必加**（同 §七.2）。
3. 号侧"同步收紧"（`chain->unitnumber != ctrl->unitnumber_backup`）—— **已是现状**：该条件在**外层** `:5751` 里，故号侧本就具备与名侧新形态同等的保护，无需再改。

### 8.4 仍未做

- 游戏内复测（§六 判据 1~6；尤其第 5 条：KI-295 的段 2 / 段 3 `bk n=` 应不再相同）。
- R-3（号侧滞留/归还链未逐点核对）、R-4（`R3RRecoverSegmentTraits()` 段内多备份时取谁不确定）仍备查。
- 未提交 git。

---

## 九、号侧复核（R-3 收口）：`DecoupleTrain()` 无条件清停放副本 —— 段号被邻居的号顶掉

> 本节 = 对第 192 轮复测日志 `build\R3R_debug.log` 的**逐行分析**（先写分析，再谈改码）。

### 9.1 日志体检（先自证现场有效）

| 项 | 值 |
|---|---|
| 文件 | `build\R3R_debug.log`，156 378 B / 2026-10-01 23:36 |
| 产生者 | 第 192 轮修法 exe（`build\openttd.exe` 2026-10-01 23:20:55 / 51 608 064 B） |
| 断言区 | `=== ASSERT ===` 段为空 |
| 崩溃 | 工作区无本次新增 `crash-*.log`；无 `LOADCENSUS ... bad>0` |
| 场景 | 未合并旧档 + 车库拖动合并（与第 191 轮同一场景）；日志内含**两次读档**（27-50 与 197-220 各一组 `load-raw`/`load` 快照） |

⇒ 现场**有效**，可作证据。

### 9.2 名侧修法在本现场已生效（KI-294 复测判据 1、2 通过）

- `387:DEPOT-XFER-ID depot-edit head=0 ctrl=6 unit=2 own_bk=1 hasname=1 own_name=**1**`
  对照第 191 轮同位置是 `own_name=0` ⇒ 链头自己的名**已寄存**（判据 1 通过）。
- `419:SEGTR-SNAP tag=depot-edit ... i=1 id=1 front=0 star=1 ctrl=0 | live u=2 n="T8701/2-国际段1" | bk u=1 n="T8701/2-国内段" | row use u=1 n="T8701/2-国内段"`
  对照第 191 轮是 `bk u=1 n=""` ⇒ 停放副本里能看到自己的名字（判据 2 通过）。
- ⇒ 判据 3/4/6 需玩家再做「存档往返 / 解挂后看界面名 / 重复进入幂等」才能判，本节不下结论。

### 9.3 号侧独立缺陷：段 1 的号在 1130 → 1602 之间由 `1` 变成 `2`

| 日志行 | 探针原文（截取） | 判读 |
|---|---|---|
| 385 | `UNIT-PARK veh=6 bk=2` | 车库编辑把段 2 的号停进 veh6 备份（正常） |
| 387 | `DEPOT-XFER-ID depot-edit head=0 ctrl=6 unit=2 own_bk=1 hasname=1 own_name=1` | 链头 0 把自己的 1 寄存、活字段穿上控制段 6 的 2 ✓ |
| 419/420 | `SEGTR-SNAP depot-edit i=1 ... bk u=1 n="…国内段" \| row use u=1`；`i=2 ... bk u=2` | 健康（`row == bk`） |
| 464 | `NAME-XFER head=24 u=0 head_own=1 got_u=1 keep_u=1 borrowed=0 own=1` | 机车 24 挂上链 0 |
| 1096 | `UNIT-RESTORE veh=0 id=1` | 解挂时 u(0) 先把自己的号 **1** 取回（`:7428`，car-only 分支）✓ |
| **1101** | `DEPOT-XFER-ID decouple-u head=0 ctrl=6 unit=2 own_bk=1 hasname=1 own_name=1` | 解挂提交点 settle 把 u 的 **1 重新停进 backup**、活字段穿 6 的 2 ✓ |
| **1130** | `SEGTR-SNAP decouple-u ... i=1 id=1 front=0 \| live u=2 n="…国际段1" \| **bk u=1** n="…国内段" \| **row u=1**` | **此刻号侧完全健康**（`row == bk == 1`） |
| 1139 | `DECOUPLE-DONE u=0 co=1 real=4 tx=33 ty=9` | 解挂提交完成（其后的 `:7758` 号归还块） |
| 1532 | `NAME-XFER head=27 u=0 head_own=1 got_u=1 keep_u=1 borrowed=0 own=1` | 机车 27 挂上链 0（第二次耦合） |
| 1601 | `CTRL-PARK tag=couple head=27 seg=3 unit=4` | 控制段落定 |
| 1602 | `SEGTRAIT-SYNC-SEC tag=couple head=27 seg=1 unit=2 group=65534 parkedname=1` | 段 1 的段行被写成 **2** ← 污染可见化 |
| **1634** | `SEGTR-SNAP couple head=27 ... i=2 id=1 front=0 \| live u=0 n="…国际段1" \| **bk u=2** n="…国内段" \| **row use u=2**` | ← 段 1 的**号**变成段 2 的 2（1130 时还是 1） |
| 1923 | `SEGTR-SNAP decouple-v head=27 i=2 ... bk u=2 \| row u=2` | 污染落定，不再自愈 |

⇒ **号侧**在 1130→1602 之间漂移：段 1 的停放号 `1` 被替换成 `2`（与段 2 的号重合）。同一位置**名侧**（国内段）全程正确 ⇒ 名/号两条链在本场景**不同源**，KI-295 条目里「修 KI-294 即可同时消灭 KI-295」的口径**对名侧成立、对号侧不成立**。

### 9.4 根因：`DecoupleTrain()` 号归还块的无条件清零（`src\train_cmd.cpp:7758-7774`）

调用顺序（本轮已逐行核对源码）：

```
:7428  R3RRestoreUnitNumber(u)                        → 日志 1096 UNIT-RESTORE u=0 id=1（u 取回自己的 1）
:7697  R3RSettleChainSegments(v, "decouple-v")
:7698  R3RSettleChainSegments(u, "decouple-u")
         └─ :5271 R3RBorrowControlTraitsLive(chain, tag)
                                                  → 日志 1101（u 的 1 停进 backup、活字段穿 6 的 2）
:7744  DECOUPLE-DONE 探针                             → 日志 1139
:7758  if (v->unitnumber_backup != 0) {                ← 号归还块
:7768      u->unitnumber = v->unitnumber;              // = 2
:7772      u->unitnumber_backup = 0;                   // ★ 无条件清零
:7773      v->unitnumber = v->unitnumber_backup;       // 机车拿回自己的 3
:7774      v->unitnumber_backup = 0;
```

`:7769-7771` 的现存注释写明了这段的**目的**：

> u now holds that number itself; the copy parked by Couple (used by the depot-split path) is stale and must not stay behind as a second reference to the same pool entry.

即它假设 `u->unitnumber_backup` 必然是「Couple 停放的那份、与 `u->unitnumber` 同一个池条目」的陈旧副本。**该前提在 u 本身是借用态链头时不成立**：此时 backup 里躺的是 **u 自己的号**（settle 刚放进去的 1），而 `v->unitnumber`(2) 是**控制段的号**。无条件清零的后果两层：

1. **号被丢弃**：1 既没有 `ReleaseID`，也没留在任何字段里 ⇒ 号池少一个可用号（`PreDestructor` 的回收判据是 `unitnumber_backup != 0`，此处已为 0 ⇒ 永不回收）。
2. **下一次耦合把借来的号当自己的号停放**：第二次 Couple 时 `u->unitnumber_backup == 0`，于是 `Couple()` 的停放块（`if (u->unitnumber_backup == 0) u->unitnumber_backup = u->unitnumber;`）把当前**借来的** 2 停进备份，段 1 的段行随即被 `R3RSyncHiddenSegmentTraits()` 镜像成 2（日志 1602/1634）⇒ 段 1 与段 2 号重合，玩家侧表现为"号乱继承 / 两段同号"。

⇒ **R-3 收口**：§七 里记的「行 1607 `live u=7`（不是 2）说明中途还有一次号侧互换」正是这条「借来的号被当成自己的号停放」的路径；它**不是** KI-294 的下游连带，而是**独立缺陷**（见 §九.5 修法）。

### 9.5 修法（最小、与既有注释同义，仅 1 行）

注释已经写明了目的是"别留下同一池条目的第二份引用"，所以只需把它从「无条件清零」改成「**只有确实是同一池条目时才清**」：

```cpp
/* 旧：无条件清零 —— u 是借用态链头时会把 u 自己的号一起抹掉 */
u->unitnumber_backup = 0;

/* 新：只在"确实是同一个池条目的陈旧副本"时才清 */
if (u->unitnumber_backup == v->unitnumber) u->unitnumber_backup = 0;
```

等价性核对：

- **经典（非借用）路径**：Couple 把 `u->unitnumber`（= `v->unitnumber`）停进 backup ⇒ 条件**恒真** ⇒ 与旧代码行为**逐字节相同**；
- **借用态链头路径**（本现场）：backup=1、`v->unitnumber`=2 ⇒ 条件假 ⇒ 保留 u 自己的号；段 1 的行不再被写成 2，号 1 也不再泄出号池。

**刻意不动**：`R3RRecoverSegmentTraits()` 的 R-4 边界仍备查；`R3RSyncHiddenSegmentTraits():5130-5131` 的加固项维持 §八.3 的"不必加"。

### 9.6 落地与构建自证（2026-10-02 00:04）

- **改码**：`src\train_cmd.cpp:7772-7780` —— 保留原注释（`:7769-7771`，它本来就写明了目的是"别留下同一池条目的第二份引用"），在其后加注 KI-297 的来由，并把 `:7772` 的 `u->unitnumber_backup = 0;` 改为 `if (u->unitnumber_backup == v->unitnumber) u->unitnumber_backup = 0;`。**唯一改动点，仅 `.cpp`、未碰任何 `src\*.h`**。
- **构建**：复用既有 `_tmp_inc_build.cmd`（未新建任何 `.cmd`）；护栏 `build\R3R_incbuild.guard.log` = `GUARD: incremental is safe (no header/lang file is newer than the newest object)`；`build\R3R_incbuild.log:515` = `[3/3] Linking CXX executable openttd.exe`；`build\R3R_incbuild.done` = `EXIT_CODE=0`；错误计数 0（`error C*` / `fatal error` / `FAILED:` / `build stopped` 全零命中）。
- **时间戳**：`src\train_cmd.cpp` 2026-10-01 23:58:44 → `train_cmd.cpp.obj` 2026-10-02 00:02:17 → `build\openttd.exe` 2026-10-02 00:04:39（51 608 064 B）；`read_lints` 该文件 0 条。
- **说明**：本次改动只在**注释**里新增文字，未新增可检索的代码字面量 ⇒ 无 exe 串自证项，以 obj/exe 时间戳 + 构建日志三件套为凭。
- 状态：**已改码 + 已编译**，游戏内复测待做（判据见 §9.7）。

### 9.7 复测判据（给玩家：用新 `build\openttd.exe` 复跑同一场景）

1. 第二次 `couple head=27` 之后，`SEGTR-SNAP tag=couple` 里**段 1 与段 2 的号不再相同**，且段 1 的 `bk u` == 段 1 的 `row u`（本现场旧值：两者都变成 2，段 1 应为 1）。
2. `SEGTRAIT-SYNC-SEC tag=couple seg=1 unit=` 打印的是 **1**（旧值 2）。
3. 反复「挂 → 解 → 再挂」多轮后，任一段的 `bk u` 都不再等于**另一段**的 `bk u`（= KI-295 判据的号侧版本）。
4. 号池不缩水：同一批车反复挂解后，新造车/新车底的号不出现系统性跳空或耗尽（旧行为每次解挂吞掉一个号）。
5. 无回归：`UNIT-RESTORE` 仍只在该出现时出现；无停放号的普通站台解挂不产生 `UNIT-RESTORE`；车库拖动路径的号显示不变（`DEPOT-XFER-ID depot-edit` 语义不动）。

### 9.8 本轮仍未做 / 边界

- **R-4** 仍备查（一个段内多车带备份时 `R3RRecoverSegmentTraits()` 取谁不确定）。
- 只修了**写点**（丢弃停放副本），没有改「号池记账」口径：被历史版本吞掉的号（旧 exe 存档里已丢）**不追溯**。
- 未做「停放副本 == 活值」之外的一致性断言/探针（若将来又出现"号对名不对"，优先查 `Couple()` 的停放块与 `R3RSyncHiddenSegmentTraits()`）。
- 未提交 git。
