# R3R 第 215 轮临时分析报告

## 提案：邻车边界框持续重叠超过 N 秒 ⇒ 判定撞毁（防穿模兜底）

- **日期**：2026-10-03
- **状态**：**待拍板 · 未实现 · 未改任何源码**
- **玩家原话（2026-10-03）**：「如果我们的列车耦合了很久（我初设 2 秒），我们就判它们撞毁，也就是边界框重叠了很久之后判定撞毁，这是用于防止穿模的，你觉得这个想法怎么样，先不上手」
- **前置上下文**：同日玩家已决定 **M3 不用改**（`COUPLE-SKIP-COMMIT-FAIL`，`R3R_COUPLE_COMMIT_FAIL_LIMIT = 4`，约 1 分钟后跳过挂接订单），本提案不是对 M3 的替代，是另一条独立的"兜底"设想。

---

## 一、必须先纠正的前提：引擎现在是「碰到就撞」，不是「重叠很久才撞」

玩家提案的隐含前提是"现在重叠很久才判撞毁，我们要把它显式化"。**实际相反**：

```15007:15013:src/train_cmd.cpp
static bool CheckTrainCollision(Train *moving_front)
{
	R3RScopeTimer r3r_coll_timer(&r3r_coll_ns, &r3r_coll_calls);
	/* can't collide in depot */
	if (moving_front->track == TRACK_BIT_DEPOT) return false;
```

`CheckTrainCollision()` 在列车每次移动后调用，**一帧重叠立即 `TrainCrashed()`**（`:14995`），全程无任何计时器。所以：

- 若把"瞬时"改成"持续 2 秒"，那是**放宽**触发条件 ⇒ 真追尾在 2 秒内不再被撞，列车可以"穿透"对方 2 秒。这与"防穿模"的目标**方向相反**。
- 提案真正想要的形态只能是第二种：**只在 R3R 自己"放行"的那些重叠上**加计时兜底，不动引擎原生追尾判据。

两种形态语义完全不同，必须先定下来是哪一种。

---

## 二、代码取证：在 R3R 里「边界框重叠」是稳态，不是瞬态

### 2.1 挂接命中**设计上就是** 1px 重叠

```14851:14865:src/train_cmd.cpp
	int min_diff = (v->gcache.cached_veh_length + 1) / 2 + (moving_front->gcache.cached_veh_length + 1) / 2 - 1;
	/* R3R: couple-on-overlap must trigger when the trains are touching/overlapping
	 * (diff <= min_diff). The original '>=' returned 0 exactly at diff == min_diff,
	 * leaving a one-pixel gap where a stopped loco touching the waiting consist
	 * would never couple (while GetCouplePosition only fires at the exact end-to-end
	 * distance). Use '>' so diff == min_diff still proceeds to the couple check. */
	const int r3r_sq_dist = x_diff * x_diff + y_diff * y_diff;
	if (r3r_sq_dist > min_diff * min_diff) return 0;
	const bool r3r_exact_touch = (r3r_sq_dist == min_diff * min_diff);
```

`min_diff` 比"两盒子刚好接触"的距离**还近 1px**（`-1`），且 `>=` 被改成 `>` ⇒ **挂接命中的那一帧，两车已经重叠 1px**。车库分支同构（`:14794`）。

⇒ 任何"重叠即计数"的计时器，**每一次正常挂接都会先记一次**。

### 2.2 拼接后 R3R **不做位置重排**，紧贴会长期保留

- `R3R_RESPACE_MAX_ERROR = 8`（`:7126`），且修复循环明确跳过超界对：

```7407:7416:src/train_cmd.cpp
			/* R3R (第 170 轮, R170-A): repair splice errors only. A pair whose
			 * spacing is off by a whole car length or more is structurally wrong
			 * (see R3R_RESPACE_MAX_ERROR) and is left exactly where it is, so the
			 * pass can never end up dragging the whole train along the track. */
			{
				const int dist_in = std::max(std::abs((int)a->x_pos - (int)b->x_pos), std::abs((int)a->y_pos - (int)b->y_pos));
				if (std::abs(dist_in - nominal) > R3R_RESPACE_MAX_ERROR) {
					skipped++;
					continue;
				}
			}
```

- 该常量自己的注释写明：`8` 就是"这两节属于同一个焊点"的容差（KI-201 附记 4 的折叠判据豁免）。

⇒ **8px 以内的紧贴/重叠是 R3R 官方认定的合法稳态**。以 2 秒计时器扫这个区间，会把正常挂好的列车当事故炸掉。

### 2.3 解挂的**下一帧必然紧贴**，且可能连续多帧

```14908:14913:src/train_cmd.cpp
	/* R3R (KI-240): a decouple cuts the released part off *seam to seam*, so the
	 * very next collision test sees the two halves touching. Because R3R gates
	 * coupling on the orders, those two halves (in the live report: the
	 * locomotive leaving for a waypoint in the south, the consist parked on
	 * WAIT_COUPLE in the north) are not a pair, and the test therefore used to end
	 * in TrainCrashed() immediately after every decouple.
```

### 2.4 现成反例：KI-241 —— 距离**永远**无法区分拼接缝与追尾

```14922:14941:src/train_cmd.cpp
	 * (c) KI-241 (2026-09-28): and the other half must not be *ahead* of the moving
	 * front along the direction this chain travels. Distance alone can never
	 * tell a seam from a rear-end collision - both sit at or inside min_diff -
	 * but the direction can: at a seam the two halves part, so the other half
	 * lies behind or beside the moving front, whereas in a collision it lies
	 * ahead, exactly where this chain is driving to. Live report (2026-09-28):
	 *     loco 27 decoupled into the *south* half (head 60,21, moving front = its
	 *     tail at 60,20, DrivingBackwards, so it travels north), the released
	 *     consist 0 stayed parked on 60,20/60,19 and the loco's next order (a
	 *     waypoint) lay in the north -> it reversed north straight through the
	 *     consist: log shows CRT veh=27 order=6 origin=60,18 found=1, 13 SEAM-FREE
	 *     lines (maxd 0..4, mfspd=0, mforder=6 vorder=17) and *zero* CRASH lines.
	 *     With this test the approach is a collision again and TrainCrashed() below
	 *     is reached. '列车的限制脱离方向': the direction the halves depart decides.

	const TileIndexDiffC r3r_mf_step = TileIndexDiffCByDiffDir(moving_front->GetMovingDirection());
	const bool r3r_seam_ahead = (r3r_mf_step.x * x_diff + r3r_mf_step.y * y_diff) > 0;
```

现场 13 行 `SEAM-FREE`（`maxd 0..4`）＝**连续多 tick 的重叠**，而那是解挂后机车正常倒车离开。**若当时存在"持续重叠 2 秒判撞毁"，机车 27 会原地爆炸**——而这正是提案想保护的场景。

⇒ 提案的判据（距离 + 持续时间）在 R3R 里**已被实证为不可靠**；可行判据必须叠加方向（`r3r_seam_ahead`）与订单语境（`WAIT_COUPLE`）。

### 2.5 车库完全在射程之外

`CheckTrainCollision()` 第一行 `if (moving_front->track == TRACK_BIT_DEPOT) return false;`（`:15011`）⇒ **库内编辑造成的任何重叠永远不会被这套判据看见**。车库拖动/编组（KI-212/213/226 的领域）不受约束，而"穿模"恰恰常在库里产生。

---

## 三、已有先例：`COUPLE-SEAM-CRASH`（90° 拼缝 → 立即撞毁）

```10518:10535:src/train_cmd.cpp
			if (seam_circ == 2) {
				/* 直角(90°)拼缝 = 事故。两车在这之前已完成物理拼接(ArrangeTrains)，这里立即
				 * 按撞毁处理，不再走下面的排程交接：车已毁，orders 交接没有意义。 */
				...
				if (!v->vehstatus.Test(VehState::Crashed)) {
					const uint num_victims = TrainCrashed(v);
					if (v->owner == _local_company) {
						AddTileNewsItem(GetEncodedString(STR_NEWS_TRAIN_CRASH, num_victims), NewsType::Accident, v->tile);
					}
				}
				return;
			}
```

这是"拼错了就炸"哲学的既有落地，但两个关键差异：

| | `COUPLE-SEAM-CRASH` | 本提案 |
| --- | --- | --- |
| 判据 | 瞬间、几何（`seam_circ == 2`） | 时间累积 + 距离 |
| 触发时机 | 提交点，一次性（`ArrangeTrains()` 之后） | 任意 tick 的稳态 |
| 误伤面 | 仅"真的拼成 90°"这一种 | 所有"持续紧贴"的正常稳态 |

⇒ 先例**不能**作为本提案的可行性背书；它之所以安全，正因为它是"提交点上的瞬时几何断言"，而不是"稳态看门狗"。

---

## 四、撞毁的代价不对等

```14733:14750:src/train_cmd.cpp
static uint TrainCrashed(Train *v)
{
	uint victims = 0;

	/* do not crash train twice */
	if (!v->vehstatus.Test(VehState::Crashed)) {
		victims = v->Crash();
		...
		AI::NewEvent(...);
		Game::NewEvent(...);
	}
	/* Try to re-reserve track under already crashed train too.
	 * Crash() clears the reservation! */
	v->ReserveTrackUnderConsist();

	return victims;
}
```

`Crash()` 会：标记 crashed、清空预订、伤及乘客（`victims`）、`RegisterGameEvents(GEF_TRAIN_CRASH)`、掉站评、发新闻（`:14719-14723`）。**不可逆的经济惩罚**。

而"穿模"是**渲染/几何不自洽**，根因多在 R3R 自身（`UpdateDeltaXY()` 的 `bounds` 与 `gcache.cached_veh_length` 两套几何不同步，KI-98 / KI-106 / KI-214）。**用玩家的钱与乘客为我们的 bug 买单**，是体验层面的灾难；而且撞毁会改变状态、把"接缝错位 8px"的案发现场变成"车炸了"，反而**断掉根因线索**。

---

## 五、时间尺度

```421:424:src/gfx_type.h
 * The value 27 together with a day length of 74 ticks makes one day 1998 milliseconds, almost exactly 2 seconds.
 * With a 2 second day, one standard month is 1 minute, and one standard year is slightly over 12 minutes.
 */
static const uint MILLISECONDS_PER_TICK = 27;
```

- `MILLISECONDS_PER_TICK = 27`（`gfx_type.h:424`）、`TICKS_PER_SECOND = 1000/27 ≈ 37`（`date_type.h:30`）
- ⇒ 玩家的「2 秒」≈ **74 tick**（正好一天）。
- 必须先定义"2 秒"是**真实毫秒**还是**游戏 tick**：`_game_speed`/快进会改变两者比例（`video_driver.hpp:174-179`），而 `_pause_mode` 下 tick 不推进（计时天然冻结）。用 tick 计数器（`_tick_counter`）最稳且确定性友好；用真实毫秒会在联网时引入 desync 风险。

---

## 六、结论

**方向可以理解，手段不合适。** 提案想解决的是真问题（R3R 放行的那批"重叠"偶尔以卡住的穿模形态残留，KI-98/KI-106/KI-214），但：

1. **前提反了**：引擎本来就是"碰到就撞"，2 秒计时是放宽而非收紧；不改语义定义就直接做，等于给所有追尾开 2 秒穿透窗口。
2. **判据被实证不可靠**：KI-241 现场原话「Distance alone can never tell a seam from a rear-end collision」。距离 + 时间是**必要但不充分**条件。
3. **正常重叠是稳态**：`min_diff` 的 `-1`、8px 焊接容差、解挂 seam-to-seam、拼接后不做位置重排 ⇒ 计时器会大面积误伤已挂好/刚解挂的列车。
4. **车库在射程外**：库内不判碰撞，而穿模常在库里产生 ⇒ 覆盖不全。
5. **代价不对等**：不可逆毁车去修渲染错位。

### 建议的替代路线（按优先级）

| # | 做法 | 说明 |
| --- | --- | --- |
| A | **只观测不惩罚**（第一步必做） | 加只读探针，统计"每对相邻车节 \|dist − nominal\| 的分布 + 连续 tick 数"。没有这轮数据就定阈值，几乎必然误伤。判据可复用既有 `overlap=fit-maxd`（`:14811`/`:14894`）。 |
| B | **提交前拒绝 + 回滚** | 已有先例 `COUPLE-REFUSE-STILL-FOLDED`（第 172 轮）。比事后炸车温和，且不冤枉玩家。 |
| C | **自动收拢/吸附**（治本） | 拼接后沿轨道整体平移把接缝推紧；重叠消失后**根本不需要**撞毁兜底。R3R 目前明确不做位置重排，这是补它的正确位置。 |
| D | **先分离、再警告** | 若一定要"惩罚"，把重叠两半沿轨道推开到合法距离 + 提示，而不是毁车。 |
| E | 若坚持撞毁 | 必须同时满足：①只作用于 R3R 放行的重叠，不动引擎原生追尾；②判据 = 重叠深度 + `r3r_seam_ahead` + `WAIT_COUPLE` 语境；③有 A 步的数据证伪误伤面；④给玩家可辨识提示；⑤确定性、无随机。 |

---

## 七、待玩家拍板项

1. **形态**：是要"放宽瞬时碰撞"（危险，不建议），还是"在 R3R 放行的重叠上加稳态看门狗"（可讨论）？
2. **阈值语义**：2 秒 = 真实毫秒还是游戏 tick？
3. **判据**：是否接受必须叠加方向/订单语境（即不能只看距离 + 时间）？
4. **先做 A（只观测）还是直接选 B/C（治本）？**
5. 若本轮就定为搁置，请在 `R3R_KNOWN_ISSUES.md` 的 KI-324 上标注「已搁置」。

## 八、本轮未做

- 未改任何源码，未构建，未新建/修改任何 `.cmd`。
- 未取任何实机数据（本报告全部结论来自源码静态取证）。
- 未评估"如果做了会误伤多少现场"——**这正是 A 步要回答的**。
