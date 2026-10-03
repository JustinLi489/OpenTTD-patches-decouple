# 第 199 轮：D19Er-XXXX（列车 6）在一次 R3R 行为后丢名 —— 换端重排的身份迁移把名字用空快照抹掉

- 玩家报告时间：2026-10-02
- 现场日志：`build\R3R_debug.log`（280 754 B / 4 666 行，写入 2026-10-02 17:29:37）
- 对照二进制：`build\openttd.exe`（2026-10-02 17:13:55，51 613 184 B，**已含第 198 轮 KI-300 修复**）
- 本轮条目：`R3R_KNOWN_ISSUES.md` 的 `### 十九、第 199 轮`（KI-301）
- 结论：**已定位 + 已改码 + 已编译**（游戏内复测待做，判据见 §八）

---

## 一、玩家报告

> 「注意那个名为 D19Er-XXXX，编号为 6 号的列车，在日志后面一次 R3R 行为后，它自己的名称被清除了，变成了默认名字（列车 6）」

（玩家上一轮报告的"凭空多出列车 7"已在第 198 轮修掉：本轮现场第 894 行出现
`UNIT-RESTORE veh=0 id=1`，即 KI-300 的取回路径按预期执行，**并且全日志
`unit=7` / `un=7` / `id=7` 零命中**，该症状本轮未复现。）

## 二、对象的物理身份（先钉死"列车 6"是谁）

`D19Er-XXXX` 在本场景里**不是车 6**，而是 **车 48**：

```
22:SEGTR-SNAP   i=1 id=0 front=48 star=1 ctrl=1 | live u=6 n="D19Er-XXXX" gid=65534 | bk u=0 n="" gbk=-1 | row none u=0 n="" g=-1
```

`R3RDUMP` 的三节 dump 显示 48/49/50 是一个**去铰接（假铰接）三节组**：
`idx=48 head=48 tail=50 ... un=6 ARTH=1`、`idx=49/50 ... ARTM=1 un=0`。
⇒ 身份（号 6 + 名 D19Er-XXXX）挂在**组头车 48** 上，49/50 是组员。
玩家口中的"列车 6"= 号 6 = 车 48 那一组。名称只在 `SEGTR-SNAP` 里打印，本轮日志里
`D19Er` 只出现在第 22 / 24 行（`load-raw` / `load` 两次读档快照），此后**再没有出现过**。

## 三、日志锚点（原文）

```
 894:UNIT-RESTORE veh=0 id=1                          ← 第 198 轮修复生效
4005:OWNER-MOVE from=6 to=23 orders=29 prio=1 borrowed=0
4035:OWNER-MOVE from=23 to=6 orders=29 prio=1 borrowed=0
4040:OWNER-MOVE from=48 to=50 orders=6 prio=1 borrowed=0   ← 身份从 48 迁到 50
4102:NAME-XFER head=50 u=6 head_own=0 got_u=1 keep_u=1 borrowed=0 own=0
4566:CTRL-UNBORROW decouple-v head=50 unit=6 own_bk=0 hasname=1 own_name=0
4628:NAME-RESTORE veh=50 side=head-cleared            ← 名字被清成空 ⇒ 默认名"列车 6"
```

`head_own=0` / `own_name=0`（第 4102、4566 行）是决定性证据：**在挂车那一刻，链头 50
就已经没有任何自己的名字**（连备份都没有）。名字不是在挂/解里丢的，是在更早的
`OWNER-MOVE from=48 to=50`（第 4040 行）那一步丢的。

## 四、根因链（逐步推演）

1. **换端重排（A3 = 翻 v）**把 D19Er 组 [48,49,50] 段内反转 ⇒ 新链头 = 50。
   第 4b 步 `R3RMoveSegmentOwner(from=48, to=50)` 搬段级责任数据（现场第 4040 行
   `OWNER-MOVE ... orders=6 prio=1`）。**注意：它只 swap `name_backup` / `group_id_backup`，
   不碰活字段 `name`。**
2. 紧接着 `R3RRelocateFrontIdentity(from=48, to=50, owner_moved_by_flip=true)`
   （`src\train_cmd.cpp:9179`）：
   - `9226` 快照 `kept_to_name = to->name`（50 是组员，活名字为**空**）；
   - `9236` `to->CopyVehicleConfigAndStatistics(from)` ⇒ 50 拿到 `name = "D19Er-XXXX"`
     （`base_consist.cpp` 的 `CopyConsistPropertiesFrom` 是 `this->name = src->name`，
     **不清 src**，所以此刻 48 与 50 同时有名）；
   - **`9239` `to->name = kept_to_name;` ⇒ 50 的名字被一个空快照当场抹掉**（← 就是这一行）；
   - `9250` 号侧反而正确：`to->unitnumber_backup = from->unitnumber_backup`，活号由拷贝
     带入 ⇒ 50 最终 `un=6`（这也解释了为什么名字丢了、号却还在）。
3. `9307` `R3RParkTrainName(from=48, from_name_borrowed=false)`：48 的名字仍非空
   ⇒ 被**停进 48 的 `name_backup`**、`48->name` 清空。
   ⇒ 名字从"链头活字段"降级成"一节链内普通车厢的停放副本"，**再没有人会去取它**
   （`R3RRestoreTrainName()` 只在车重新成为链头时调用）。
4. 之后挂车（第 4102 行）时链头 50 无名可借，`head_own=0`；挂车按
   KI-261 的语义借入车组名 `T8701/2-国际段1`；`v->name_backup` 因空而未填（`own=0`）。
5. 解挂（第 4628 行）时归还端发现 `v->name_backup` 为空 ⇒ 走 KI-277 登记的
   `side=head-cleared` 分支把借来的名清掉 ⇒ 链头 50 活名 = 空、活号 = 6
   ⇒ 车辆列表显示**默认名"列车 6"**。

**一句话根因**：`R3RRelocateFrontIdentity()` 里"拷完还原 to 自己的名字"（第 144 轮 /
需求贰，为**直接拼接**路径写的）在**身份迁移路径**（`owner_moved_by_flip == true`）
被复用：那时 `to` 是刚从链内升上来的车厢、自己本来就没有名字，还原一个空快照
= 把刚搬过来的名字抹掉；而名字又不是完全销毁，它被 `R3RParkTrainName(from)` 停进了
已经降级为链内车的 `from` 的备份里，从此既不上屏也无人取回。

## 五、修法（只改 `src\train_cmd.cpp`，未碰 `src\*.h`）

与**号侧同一口径**："随前端身份迁移的数据必须无条件随身份走"。
在 `R3RRelocateFrontIdentity()` 内加一个判据统计一次，两处按它分叉：

- 新增：`const bool name_follows_identity = owner_moved_by_flip && kept_to_name.empty() && !from->name.empty();`
- `9239` 改为：`if (!name_follows_identity) to->name = kept_to_name;`
  （成立时不动 `to->name` —— 拷贝进来的 `from->name` 就是身份的名字）
- `9307` 改为：成立时 `from->name.clear();`（**不再**停备份，否则同一件名字同时活在
  链头与链内车上，`from` 将来重新当链头会冒出一个陈旧副本；借用态同理：借来的名随
  链头身份继续被 `to` 穿着）；不成立时维持 `R3RParkTrainName(from, from_name_borrowed)`。

**刻意保守**：`kept_to_name` 非空（`to` 有自己的名字，罕见情形）时**沿用第 144 轮原语义**
一字不动，避免影响"合并链对外显示控制段名字"的既有行为。

## 六、预期执行序列（复测时看）

1. 翻 v 后链头 50：`NAME-XFER` 之前它就该**有自己的名字**（不再是 `head_own=0`）；
2. 挂车：`NAME-XFER head=50 u=6 head_own=1 got_u=1`（`own=1`，名字被停进
   `v->name_backup`）；
3. 解挂：`NAME-RESTORE veh=50 side=head`（**不再是 `side=head-cleared`**），
   界面恢复 `D19Er-XXXX`，号仍为 6。

## 七、未确认项（如实登记）

- **R-1**：`R3RMoveSegmentOwner()` 只 swap `name_backup` 不搬活 `name`，这与
  `unitnumber` 由 `CopyVehicleConfigAndStatistics` 带入的分工是否在**其它调用点**
  （非翻 v 路径）也自洽，未逐一核对。
- **R-2**：`kept_to_name` 非空（`to` 自带名字）的翻转场景**现场未出现过**，本轮按
  第 144 轮原语义保守处理，未做"两名互换"设计。
- **R-3**：本轮未动解挂端的 `side=head-cleared` 分支（KI-277）——它不是病根，
  只是"链头本来就没名字"的如实反映；若复测后仍见到它，说明还有别的丢名路径。
- **R-4**：第 197 轮的 T3 判据曾把本条现场（同一场景的 `veh=50 side=head-cleared`）
  **误判为"KI-277 已登记的正常分支"**；本条即对该误判的更正——症状相同但根因不同，
  第 197 轮的 T3 判据口径需按本轮更正为"`side=head-cleared` 出现即视为**名侧缺陷**，
  必须回溯该链头在本会话内是否经历过身份迁移"。

## 八、复测判据

1. 同场景（三次挂/解）复跑：`NAME-RESTORE veh=50 side=head-cleared` **消失**，
   代之以 `side=head`；
2. 界面里号 6 那一列显示 `D19Er-XXXX`（不再是"列车 6"）；
3. 翻 v 之后的第一个 `SEGTR-SNAP`：链头（50）行 `live n=` 非空；
4. 挂车那一行 `NAME-XFER ... head_own=1 own=1`（此前是 `0/0`）；
5. 号侧第 198 轮判据不退：`UNIT-RESTORE-DEFER veh=0 id=1` 出现、全程无 `unit=7`；
6. 无回归：直接拼接路径（`owner_moved_by_flip == false`）的
   `NAME-XFER ... head_own=? got_u=1 keep_u=1` 逐字段与旧日志一致；
7. 全日志 `error` / `assert` / `fatal` 计数 0。

## 九、构建自证

复用既有 `_tmp_inc_build.cmd`（**未新建任何 .cmd**，本轮只改 `src\train_cmd.cpp` 一个文件、未碰 `src\*.h`）：

| 项 | 值 |
| --- | --- |
| `build\R3R_incbuild.guard.log` | `GUARD: incremental is safe (no header/lang file is newer than the newest object)` |
| `build\R3R_incbuild.done` | `EXIT_CODE=0` |
| `build\R3R_incbuild.log` 尾部 | `[3/3] Linking CXX executable openttd.exe` |
| 错误计数 | `error C\d` / `fatal error` / `FAILED:` / `build stopped` 合计 **0** |
| `src\train_cmd.cpp` | 2026-10-02 17:38:52（919 637 B） |
| `train_cmd.cpp.obj` | 2026-10-02 17:41:15（10 495 806 B） |
| `build\openttd.exe` | 2026-10-02 17:43:22（51 613 696 B） |
| `read_lints` | 0 条 |

时间戳链满足「源码 → obj → exe」严格递进；exe 较上一轮（第 198 轮 KI-300，51 613 184 B）**+512 B**，证明改动确已进镜像。本轮是**纯分支条件改动、未新增任何日志字面量**，因此不做 exe 串自证（无字面量可查），以上三件套足以收口。**状态：已修（改码 + 编译通过），游戏内复测待做**。
