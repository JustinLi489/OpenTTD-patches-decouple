# R3R 第 185 轮 临时分析报告：KI-280「名字乱传播」名侧对称修复

- 日期：2026-10-01（第四轮）
- 现场日志：`build\R3R_debug.log`（由第 183/184 轮的 exe 产生）
- 相关条目：`R3R_KNOWN_ISSUES.md` 的 KI-280（本轮状态由「未修（取证完毕、根因已定稿、待改码）」改为「已修」）、关联 KI-279（号侧，第 182 轮已修）、KI-261/KI-263（名字跟控制段走的既有设计）
- 前置分析：`R3R_round183_name_spread_and_four_r3r_memo.md` §3.1 / §四 / §七
- 本轮交付物：`src\train_cmd.cpp`（仅此一个 .cpp）+ 本报告 + KI 清单条目

---

## 一、现象复述（玩家口径）

玩家（2026-10-01 第二轮，同一现场）指认：

> veh6 是控制段（`UNIT-PARK veh=6 bk=2` / `ORD-XFER keep veh=27 owner=6` / `ORD-AFTER-COUPLE … owner=6`）；别的段与 veh6 耦合时 veh6 的名字被显性（符合 KI-261/263 设计），但**其他段与 veh6 解挂后仍继承 veh6 的名字**。

即：名字「显性」这一段是设计要的，**「不收回去」**才是缺陷。

## 二、取证：日志锚点（逐行原文）

污染链的起点与放大点（同一次会话，同一张 29 条表，owner=6）：

| 行号 | 探针原文 | 读法 |
| --- | --- | --- |
| 3743 | `NAME-XFER head=27 u=0 head_own=0` | veh27 是链头、本来**没有名字**（head_own=0），向等待方 veh0 借名 |
| 4551 | `ORD-XFER keep veh=27 owner=6 cur_real=5 parked_real=9` | 第一次借名**还没归还**时，27 又被挂上第二条链 |
| 4552 | `NAME-XFER head=27 u=23 head_own=1` | 此刻 27 的活字段是**借来的**名字，探针却把它算成 `head_own=1` |

于是这次停放把「上一次借来的名字」当成 27 自己的原名停进了 `name_backup`。

四条完整往返（`head_own=0` 的车却全部走 `side=head`，而不是应有的 `side=head-cleared`）：

| # | 借入 | 归还 |
| --- | --- | --- |
| 1 | `1306 NAME-XFER head=27 u=6 head_own=1` | `1530 NAME-RESTORE veh=27 side=head` |
| 2 | `1759 NAME-XFER head=50 u=6 head_own=0` | `2037 NAME-RESTORE veh=50 side=head` |
| 3 | `2439 NAME-XFER head=48 u=6 head_own=0` | `2760 NAME-RESTORE veh=48 side=head` |
| 4 | `2939 NAME-XFER head=29 u=23 head_own=0` | `3313 NAME-RESTORE veh=29 side=head` |

**全文统计（关键对照）**：

- `NAME-RESTORE` 共 12 次，**全部是 `side=head`**，`side=head-cleared` **恒为 0**；
- 号侧（第 182 轮 KI-279 已修）同期出现 2 次 `UNIT-RESTORE veh=0 id=1 side=head-cleared`。

⇒ 名侧的 `side=head-cleared` 支路在现场等价于**死代码**：归还端永远认为"链头有自己停放的原名"，于是把**别人的名字**当成自己的还了回去。

## 三、根因（读码定稿）：三处写点都会把「借来的名字」停成「自己的名字」

判据缺失的实质：**没有任何地方能区分"链头自己的名字"与"链头此刻借来的名字"**。号侧有 `was_borrowing`（KI-279）/「排程只在第一次借用时寄存」（KI-215）这条规则，名侧没有对称实现。

1. `train_cmd.cpp:10254-10258` `Couple()`
   - `head_had_own = !v->name.empty();`（**只判非空，无法区分来源**）
   - `if (v->name_backup.empty()) v->name_backup = v->name;`
   - ⇒ 第二次耦合时把上一次借到的名字停成自己的备份（3743 → 4552）。
2. `train_cmd.cpp:2762-2768` `R3RParkTrainName()`（调用点 `3203` 车库身份迁移 / `8863` `R3RRelocateFrontIdentity`）
   - `if (v->name_backup.empty() && !v->name.empty()) v->name_backup = v->name; v->name.clear();`
   - ⇒ 身份迁移若发生在借名之后，停进备份的是借来的名字。
3. `train_cmd.cpp:5648-5655` `R3RBorrowControlTraitsLive()`（车库拖动 / 读档边）
   - `if (chain->name_backup.empty()) chain->name_backup = chain->name; chain->name = ctrl->name_backup;`
   - 这本身是"控制段 veh6 的名字显性到链头"的**正规通道**；但链头此刻若顶着借名，会把借名一起停进去。

归还端 `train_cmd.cpp:7660-7672` 只认 `!v->name_backup.empty()` ⇒ 备份被污染后**必走 `side=head`**，把别人的名字当作自己的原名还给链头 ⇒ 玩家症状。

第二条通路（同一症状）：归还块整块位于 `if (v->r3r_orders_borrowed)`（`train_cmd.cpp:7346`）之内 ⇒「没有借用排程」的解挂完全不执行名字/号归还。

## 四、修法（第 185 轮落地，仅 `src\train_cmd.cpp`）

### 4.1 新增只读判定

```cpp
static bool R3RNameIsBorrowed(const Vehicle *v)
{
    return v != nullptr && v->r3r_orders_borrowed && v->name_backup.empty() && !v->name.empty();
}
```

**判据依据**：借用中的链头若 `name_backup` 为空，说明耦合那一刻它**本来就没有自己的名字** —— 有名字的话 `Couple()` / `R3RParkTrainName()` 早把原件停进 `name_backup` 了。于是活字段上那件名字只可能是 lender（等待方 / 控制段）的。

规则表述可与既有两条完全对齐：**借用中不寄存**（KI-279 的 `was_borrowing`、KI-215 的"排程只在第一次借用时寄存"）。

### 4.2 四处门禁

| # | 落点 | 改法 |
| --- | --- | --- |
| ① | `Couple()` 名字块 | `head_had_own = !v->name.empty() && !was_borrowing`；停放行 `if (head_had_own && v->name_backup.empty())`；等待方 u 的停放加 `!R3RNameIsBorrowed(u) &&` |
| ② | `R3RParkTrainName()` | 签名加 `bool name_is_borrowed = false`，停放行加 `!name_is_borrowed &&` |
| ②a | 车库身份迁移（`CmdMoveRailVehicle()` cases #2/#3，原 `3203`） | 在 `if (src->r3r_orders_borrowed)` 清标志**之前**采样 `src_name_borrowed`，调用点传参 |
| ②b | `R3RRelocateFrontIdentity()`（原 `8863`） | 在 `kept_to_name` 快照处（借用标志被 swap 到 `to` 身上**之前**）采样 `from_name_borrowed`，调用点传参 |
| ③ | `R3RBorrowControlTraitsLive()` | 停放行加 `!R3RNameIsBorrowed(chain) &&` |

两处"提前采样"是必须的：`3203` 之前有 `src->r3r_orders_borrowed = false;`（消费借用态），`8863` 之前有 `std::swap(to->r3r_orders_borrowed, from->r3r_orders_borrowed)`（标志已搬家）。在那些位置之后调 `R3RNameIsBorrowed()` 会读到被改写过的标志。

### 4.3 探针（满足 KI-280 判据 ①，并为「第二条通路」留痕）

- `NAME-XFER` 扩为六个字段：`NAME-XFER head=%d u=%d head_own=%d got_u=%d keep_u=%d borrowed=%d own=%d`
  - `borrowed` = 采样到的 `was_borrowing`（耦合前链头是否已在借用中）
  - `own` = 停放后链头是否真有一件自己的原名（`!v->name_backup.empty()`）
  - **读法**：`borrowed=1 own=0` = 本次正确地没有把借名停成原名；旧 exe 在同样场景会给出 `borrowed=1 own=1`，这就是污染签名。
- 新增只读支路（挂在归还块 `if (v->r3r_orders_borrowed)` 的 `else` 上）：
  `NAME-RESTORE-SKIP veh=%d u=%d borrowed=0 head_name_matches_u=1`
  ⇒ 仅在「没有借用排程 + 链头活名字恰好等于等待方名字」时落一行。**本轮不改归还闸门结构**（理由见 §六）。

## 五、构建自证（2026-10-01 14:27-14:34）

- 入口：`cmd /c "cd /d ""d:\sourcecode of JGRPP"" && _tmp_inc_build.cmd"`（复用既有文件，未新建任何 `.cmd`）。
- `build\R3R_incbuild.guard.log`：`GUARD: incremental is safe (no header/lang file is newer than the newest object)`。
- `build\R3R_incbuild.log`：仅第 515 行 `[3/3] Linking CXX executable openttd.exe`；`error C[0-9]` / `fatal error` / `FAILED:` / `build stopped` 零命中。
- `build\R3R_incbuild.done`：`EXIT_CODE=0`。
- 时间戳链：`src\train_cmd.cpp` 14:27:04 → `build\CMakeFiles\openttd_lib.dir\src\train_cmd.cpp.obj` 14:29:05（10 159 447 B）→ `build\openttd.exe` **14:33:32（51 564 544 B）**。
- `read_lints src\train_cmd.cpp`：0 条。
- exe 内字面量（Latin1 读二进制）：`NAME-RESTORE-SKIP`、`NAME-RESTORE-SKIP veh=%d u=%d borrowed=0`、`borrowed=%d own=%d`、`NAME-XFER`、`NAME-RESTORE veh=%d side=head` 全部命中。

## 六、未确认项 / 本轮未做

1. **KI-280「第二条通路」只加留痕、未改结构**。把名字/号归还移出 `if (v->r3r_orders_borrowed)` 看似直接，但会让"链头与等待方恰好同名"的巧合场景**误清链头自己的名字**（现存 `else if` 分支本就是 `v->name == u->name` 判定）。风险大于收益，先用 `NAME-RESTORE-SKIP` 取一轮实证再定。
2. **`1759 / 2439 / 2939`（`head_own=0` 却走 `side=head`）的 `name_backup` 究竟由 `3203` 还是 `8863` 填的**：仍未定。本轮已把两处采样点分开（`src_name_borrowed` / `from_name_borrowed`），但**探针未打 tag**，下一轮如仍复现，需在 `R3RParkTrainName()` 内部补一行 `tag` 参数区分来源。
3. **借用期间玩家手动改名**（此时 `name_backup` 为空）会被新判据视为"借名"而不再寄存 ⇒ 该次改名会随 `v->name.clear()` 丢掉。属已知边界，未做特殊处理；如现场出现再补"改名时同步停备份"的钩子。
4. **KI-281 / KI-282（`idx20 DECOUPLE` 被静默跳过）本轮未改码**：缺口 A（`at_order_dest` 只看"最近旅行命令"）与缺口 B（`GetDecoupleVehicle()` 只返回尾部切点，表达不了"解出链头段"）都要动解挂边界模型；KI-282 未确认项 ①②③（29 条表归属、`idx21 WAIT_COUPLE` 归哪半、`veh=30` 定性）仍待口径。
5. 第 183 轮待办 2 / 3（读码核对 `R3RSkipUnfireableDecoupleOrder()` 与 `Couple()` 落点判据；定性 `veh=30`）本轮未推进，顺延第 186 轮。

## 七、复测判据（用新的 `build\openttd.exe` 复跑同一现场）

1. 第二次耦合（`ORD-XFER … parked_real=` 之后那次）必须出现 `NAME-XFER … borrowed=1 own=0`，**不得**再出现 `borrowed=1 own=1`。
2. 解挂后除 lender 本人外，任何一节的显示名不得等于本次借入的名字；"链头本来没名字"的解挂应出现 `NAME-RESTORE veh=%d side=head-cleared`（此前该值为 0 次）。
3. 车库拖动耦合链（拖到列车前面 / 拖出被并入部分）后号与名都不串车（与 KI-263 的号侧判据一并看）。
4. 健康单条耦合（链头本来就有自己的名字）必须仍出现 `own=1`，且解挂后名字回到链头自己那一件 —— 不得因本修法丢名字。
5. 若复现"链头活名字等于等待方名字但非借用态"，应出现 `NAME-RESTORE-SKIP`（该支路本身不改行为）。
