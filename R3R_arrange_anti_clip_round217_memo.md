# R3R 第 217 轮临时分析报告

## 主题：如何避免「无法 arrange trains」时的两车穿模

- **日期**：2026-10-03
- **状态**：**方案设计 + 取证完成 · 未改任何源码 · 未构建**
- **触发**：玩家 2026-10-03 第二轮：「那换一个方法。我只是想要避免在无法 arrange trains 的时候两个列车开始闹穿模，你能帮我想想吗」
- **与上一轮的关系**：玩家**放弃**第 216 轮的「attach 失败即撞毁」路线，把目标从「惩罚」改为「预防」。KI-325 相应**降级为已搁置**。
- **取证范围**：`src/train_cmd.cpp`（本轮只读，未改一个字符）。

---

## 一、玩家说的「无法 arrange trains」精确对应什么

### 1.1 引擎的 `ArrangeTrains()` 只管链序、**完全不碰像素位置**

```2782:2796:src/train_cmd.cpp
/**
 * Arrange the vehicles in the train in the correct order and
 * put the removed vehicles into the depot, using a separate chain.
 * ...
 */
static void ArrangeTrains(Train **dst_head, Train *dst, Train **src_head, Train *src, bool move_chain)
{
```

它的全部动作是：

| 步骤 | 函数 | 改什么 |
| --- | --- | --- |
| 摘除 | `RemoveFromConsist(src, move_chain)` | 只改 `Next`/`Previous` 指针 |
| 插入 | `InsertInConsist(dst, src)` | 只改 `Next`/`Previous` 指针 |
| 归一 | `NormaliseDualHeads(*src_head)` / `NormaliseDualHeads(*dst_head)` | 只改双头车的 `First`/`Next` |

**三个函数里没有一处触碰 `x_pos` / `y_pos` / `tile` / `track` / `direction`。**

⇒ **`ArrangeTrains()` 根本没有几何能力。** 这就是「无法 arrange trains」的字面正解：引擎提供的是**链序工具**，它无法把两列停在线路上不同位置的车「排」成一条几何上连续的链。上游之所以从不出问题，是因为上游只在**车库**里做这个操作（车库里车辆按索引堆叠显示，位置无意义）；R3R 把同一套工具用到了**真实轨道上**（`GOTO_COUPLE` 在站台/区间拼车），位置就是有意义的了。

⇒ 结论 1：**「arrange 失败」不是某个函数返回了错误码，而是「链序成功了，几何没跟上」这一状态本身。**

### 1.2 真正负责几何的是 `R3RRespaceChainAfterEdit()`，而它会**主动承认修不动**

```7334:7370:src/train_cmd.cpp
static int R3RRespaceChainAfterEdit(Train *head, const char *tag)
{
	if (head == nullptr) return 0;
	...
	Train *const front = head->GetMovingFront();
```

这个函数按 `GetMovingNext()` 逐对走链，比较**实际像素间距**与引擎自己的名义间距 `CalcNextVehicleOffset()`，不等的就用 `TrainController()` 把它们推回去。它的注释明确写着目的就是消除玩家看到的「chewed up」（被啃过）的接缝：

```7067:7080:src/train_cmd.cpp
/** Upper bound for the "push the half ahead forward" steps
 *  R3RRespaceChainAfterEdit() performs on a *single* pair whose boxes overlap.
 *  A splice error is a pixel or two wide, so this only exists so a pathological
 *  chain can never march off along the track.
 *  ...
 *  the last four pairs still
 *  stacked (settle-couple dump: dist=0 against nom=5), which is exactly the
 *  "chewed up" look the player reported. */
static const int R3R_RESPACE_MAX_STRETCH = 16;
```

**但它的两条退出计数正是"修不动"的自陈**：

```7484:7513:src/train_cmd.cpp
	if (moved != 0 || stretched != 0 || unfixed != 0 || walk_capped || skipped != 0) {
		R3RDumpChainGeometrySnapshot("respace-before", geo_before);
		R3RDumpChainGeometry("respace-after", head);
	}
	...
	if (unfixed != 0) {
		R3RDbgWrite("RESPACE-BADCHAIN %s head=%d unfixed=%d skip=%d stretch=%d px=%d visited=%d capped=%d\n",
				tag, (int)head->index.base(), unfixed, skipped, stretched, moved, visited, walk_capped ? 1 : 0);
	}
```

注释把这个签名解释得很清楚：

```7504:7507:src/train_cmd.cpp
	/* R3R (第 173 轮, KI-265 探针): unfixed>0 是"结构性坏链"的签名 —— 这一遍扫完
	 * 仍有接缝满足不了"实际间距 == 名义间距"，而且 respace 主动跳过了它（超界被
	 * skip，见 R170-A），也就是工具自己承认修不动。 */
```

⇒ **玩家说的「无法 arrange trains」= 日志里的 `RESPACE-BADCHAIN ... unfixed=N`（以及配套的 `skip=N`）。** 这是全代码库唯一一处"工具自陈修不动"的地方，也是穿模唯一能留存下来的出口。

---

## 二、根因：两条正确约束在这里正面冲突

穿模之所以修不掉，**不是有 bug，而是两个各自正确的安全设计在同一个接缝上互相抵消**：

| 约束 | 出处 | 内容 | 为什么正确 |
| --- | --- | --- | --- |
| **A. 超界不许动** | R170-A（第 170 轮 KI-201 附记） | `R3R_RESPACE_MAX_ERROR = 8`：某对实际间距与名义值差超过 8px ⇒ `skipped++; continue;` **完全不碰** | 玩家第 170 轮实报「位置被搬到站台另一端」。无上界的追间距会把整列沿轨道拖走 |
| **B. 必须动才不穿模** | 本轮玩家诉求 | 接缝错位要消除，只能靠移动像素 | 不移动 = 重叠保留 = 视觉穿模 |

**冲突的致命处在于：错得越离谱的接缝，越会被 A 跳过。** 也就是说，**工具恰好在"最该修的那一处"选择了放手**，而那一处正是玩家肉眼可见的穿模。
⇒ 第 172 轮现场 `RESPACE-AFTER-EDIT couple head=48 ... unfixed=1` 紧跟在那次穿模提交（`48(1010)/49(1014)/50(1017)/6(1013)/7(1008)`，y 坐标在拼接点 `50→6` 处不单调）之后，就是这条链的完整闭环证据。

**⚠️ 这是本轮最重要的结论：穿模是"设计取舍"的产物，不是漏改。任何只调参数（比如把 8 放大到 80）的做法都会重新踩响 R170-A。**

---

## 三、为什么穿模集中发生在「挂接」而不是「车库拖动」

| 场景 | 位置是否有物理意义 | 结果 |
| --- | --- | --- |
| 上游车库拖动（`CmdMoveRailVehicle`） | 否（库里按索引堆叠） | `ArrangeTrains()` 链序正确即完成，无几何问题 |
| R3R 在线路上挂车（`GOTO_COUPLE`） | **是**（两列停在真实 tile 上） | 链序合并成功，但两列各自的像素序列在拼接点不连续 ⇒ **穿模** |

补充放大因素（均有代码锚点）：

1. **命中判据本身就要求 1px 重叠**：`CheckTrainCollision()` 的挂接分支 `min_diff = (len + 1) / 2 + (len + 1) / 2 - 1`，且 R3R 把 `>=` 改成了 `>` —— 注释自陈 `the loco freezes already overlapping by 1px`（KI-106）。也就是说**在挂接成功之前，机车就已经停在对方的像素范围里了**。
2. **失败重试会继续往里挤**：第 172 轮现场，机车每失败一次**北推 1px**，`worst_gap` 从 7 一路压到 2。重叠由 1px 变成若干 px。
3. **提交点不校验几何**：`COUPLE-REFUSE-STILL-FOLDED`（第 172 轮加的硬闸门）只查**方向折叠**，不查**间距/位置序列**，所以"几何上不可能排好"的合并照样提交。
4. **`COUPLE-SEAM-FLIP` 只翻方向不动位置**：接缝自交时执行 `R3RReverseChainDirections(merged_first)`，注释与实现都表明它治不了位置交错。

⇒ **穿模的完整生成链**：`命中(重叠1px)` → `反复失败北推(重叠变大)` → `提交(链序合并)` → `respace 发现超界 ⇒ skip` → `unfixed>0` → **穿模留存**。

---

## 四、方案（四选，可组合；按推荐度排序）

### 【方案 1 ★推荐】提交前「几何可排性」预检 —— 排不好就不合并

在 `TryTrainCouple()` 现有的提交硬闸门 `COUPLE-REFUSE-STILL-FOLDED`（第 172 轮，`:9193-9223`，位置在 `CheckTrainAttachment` 之前）**同一处**，把判据从"方向折叠"扩展为"几何不可排"：

- 预演拼接点对：`|dist − nominal| > R3R_RESPACE_MAX_ERROR(8)` ⇒ 判定**当前合并在几何上不可能排好** ⇒ 走已有的回滚骨架（`RestoreTrainBackup` + `R3RUndoLogicalFlip` + `ConsistChanged(CCF_ARRANGE)`）后 `return false`。

**效果**：永远不产生「合并了但修不动」的坏链 ⇒ **穿模不可能出现**，代价是"有时挂不上"。
**优点**：零位置风险（不移动任何像素）；复用现成分支骨架与现成阈值；与第 172 轮闸门是同构的姐妹分支。
**代价**：挂不上 → 双方僵持 → 由 **M3**（`R3R_COUPLE_COMMIT_FAIL_LIMIT = 4`，约 1 分钟）跳过订单兜底。玩家 2026-10-03 明确保留 M3，所以这条兜底链是完整的。

### 【方案 4 ★推荐，与方案 1 配对】把 `unfixed>0` 从"事后观察"升级为"提交回滚"

```7484:7503:src/train_cmd.cpp
	if (moved != 0 || stretched != 0 || unfixed != 0 || walk_capped || skipped != 0) {
```

提交点调完 `R3RRespaceChainAfterEdit()` 后，若 `unfixed != 0`（工具自陈修不动）⇒ **回滚这次合并**，而不是把坏链留在世界上。

- 位置回滚有现成素材：`R3RRespaceChainAfterEdit()` 内部已经拍了 `std::vector<R3RGeoPoint> geo_before`（`:7365`，`{veh, tile, x, y}`），只是目前仅用于日志。把同一快照用于**位置还原**即可。
- 效果：**穿模在"提交点之后"也逃不掉**，形成与方案 1 的双保险（前门预检 + 后门回滚）。

**风险**：`RestoreTrainBackup` 还原链序、`R3RUndoLogicalFlip` 还原方向/段头，**但两者都不还原像素位置**（`RestoreTrainBackup` 只重链序，注释见第 216 轮取证）。整套回滚必须额外补一次位置还原，**否则回滚本身会造成新的错位**。这是方案 4 唯一的技术难点，也是必须单独立项验证的点。

### 【方案 2】拒绝时让机车停车（治前因：不产生重叠）

把"每失败一次北推 1px"取消，拒绝分支里让机车 `cur_speed = 0` 并保持在挂接距离外。

- 收益：从**源头**消除重叠 ⇒ 连 1px 的穿模都不会出现。
- **必须与方案 1 配对**。第 173 轮的记录明确写道：拒绝分支清 `cur_speed`/`subspeed` 会让机车**停在挂车距离外再也挂不上**（这正是第 173 轮「修法 3」被故意搁置的原因：须先拿到 `FOLDCHK-REFUSE-REAL-ARTIC` 的实测数据）。
- 建议：**等方案 1 上线并拿到挂接成功率数据后再评估**，不要与方案 1 同时改，否则两类失败（几何拒绝 / 停车挂不上）无法区分。

### 【方案 3】真正把"修不动"变成"能修"（治本，工作量最大）

给 `R3RRespaceChainAfterEdit()` 增加**轨道约束下的整体平移**：被并入段沿其所在轨道向前推 Δ 像素，逐格校验可占用、不与第三列车冲突，推不动就放弃整次合并（而不是 skip 单对）。

- 这正是穿模的彻底解法（错位被真正消除，不需要任何兜底）。
- 风险：`TrainController()` 的既有行为已经证明"一次调用不保证移动每一节车"（注释 `:7085-7092`），要它做到"整体刚性平移且原子回滚"需要新的位置事务机制。
- 建议：**列为长期目标**，不在本轮实现。

### 方案对比表

| | 方案 1 预检拒绝 | 方案 4 事后回滚 | 方案 2 拒绝时停车 | 方案 3 几何吸附 |
| --- | --- | --- | --- | --- |
| 消除穿模 | ✅ 彻底（不产生坏链） | ✅ 彻底（坏链被撤） | ✅ 从源头（不产生重叠） | ✅ 彻底且不留"挂不上" |
| 位置移动风险 | 无 | **有**（回滚需还原位置） | 无 | **高**（R170-A 老路） |
| 挂接成功率 | 下降 | 下降 | 下降更多 | 不下降 |
| 复用现成设施 | 高（同构分支 + 现成阈值） | 中（有 `geo_before` 快照可用） | 高 | 低 |
| 依赖前置数据 | 无 | 无 | **需要**（第 173 轮遗留数据） | 无 |
| 本轮建议 | **做** | **做（与 1 配对）** | 缓 | 长期 |

---

## 五、推荐落地路径

1. **第一步（本轮建议实施）**：方案 1。判据、回滚骨架、阈值全部现成，改动集中在 `TryTrainCouple()` 的一处闸门。
2. **第二步**：方案 4 的 `unfixed>0` 回滚，**但必须先解决"位置还原"**（复用 `geo_before` 快照）。在此之前方案 4 不能上线，否则回滚会造成新的错位。
3. **第三步**：观察挂接成功率，再决定是否上方案 2。
4. **方案 3** 长期保留。

**贯穿原则**：**绝不留下"半修"状态**。任何一次合并，结果只允许是「几何连续」或「没合并」两种。

---

## 六、复测判据（方案 1）

1. 出现新的拒绝标签（如 `COUPLE-REFUSE-GEOMETRY`）时，其后**不得**出现 `RESPACE-BADCHAIN`。
2. 同场景复跑，穿模提交（第 172 轮形态 `50(1017) → 6(1013)`）应**从日志消失**，取代它的是拒绝行 + 下一 tick 重试。
3. `RESPACE-AFTER-EDIT` 的 `unfixed=` **恒为 0**（关键判据；`skip=` 可能非 0，但只要有 `unfixed>0` 就说明方案 1 判据漏了）。
4. **健康挂接不得被误伤**：正常紧贴同向挂车仍必须 `COUPLE-OK`，`COUPLE-REFUSE-GEOMETRY` 不得出现在原本能挂上的场景里。
5. 挂不上的场景由 M3 收尾：应出现 `COUPLE-COMMIT-FAIL` 累计至 4 次后的 `COUPLE-SKIP-COMMIT-FAIL`，机车执行下一条命令（而非永久僵持）。

---

## 七、未确认项

- **U-1**：`RESPACE-AFTER-EDIT` 的 `unfixed>0` 在现场到底出现几次？本轮**未统计**（现场日志 `build\R3R_debug.log` 4150 行、停在挂接发生之前，没有 `COUPLE-OK`，因此也没有 respace 记录）。**这是决定方案 1 优先级的唯一数据**。
- **U-2**：方案 1 的判据应该只看拼接点那一对，还是看合并后**整链的位置单调性**？前者更宽松（保留挂接率），后者更严格（更彻底）。建议先按"拼接点对"实现，用 `skip`/新标签计数观察是否需要收紧。
- **U-3**：第 172 轮现场 6 次重试的"北推 1px"究竟发生在哪一层（寻路把车推近 / `TrainController` / 判据自身容差），未逐行定位；这决定方案 2 的具体落点。
- **U-4**：`CheckTrainCollision()` 挂接分支那个 `-1`（故意留 1px 重叠）能否直接去掉。若能，重叠从**判据层面**就不存在，是对方案 2 的替代，且风险比方案 2 小得多（不改运动、只改准入）。

---

## 八、本轮未做

- 未改任何源码，未构建，未新建任何 `.cmd`。
- 未统计 `RESPACE-BADCHAIN` / `unfixed=` 的历史出现次数（U-1）。
- 未验证"位置还原"的可行性（方案 4 的前置条件）。
- KI-325（attach 失败即撞毁）按玩家本轮指示**降级为已搁置**，未删除条目。
