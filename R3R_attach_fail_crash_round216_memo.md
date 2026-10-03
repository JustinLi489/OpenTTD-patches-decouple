# R3R 第 216 轮临时分析报告

## 问题：两列车接触了，但无法 attach wagon —— 它们会怎么样？玩家希望直接判撞毁

- **日期**：2026-10-03
- **状态**：**待拍板 · 未实现 · 未改任何源码**
- **玩家原话（2026-10-03）**：「哦，那我想要知道，如果两个列车接触了，结果发现无法 attach wagon，那么这两列车会怎么样，我希望它们直接判定撞毁」
- **与上一轮的关系**：这是第 215 轮「持续重叠判撞毁」的**具体化追问**（从"重叠很久"改为"attach 失败"），指向同一族漏洞；但落点是**另一条代码路径**，结论也不同。

---

## 一、现状取证：会怎么样

### 1.1 attach 失败的那一行

```9986:9997:src/train_cmd.cpp
	bool ok = CheckTrainAttachment(head).Succeeded();
	if (!ok) {
		RestoreTrainBackup(original_src);
		RestoreTrainBackup(original_dst);
		if (v_flipped) R3RUndoLogicalFlip(v, v_old_bounds, v_old_roles);
		if (u_flipped) R3RUndoLogicalFlip(u, u_old_bounds, u_old_roles);
		R3RRefreshChainCaches(v);
		R3RRefreshChainCaches(u);
		v->ConsistChanged(CCF_ARRANGE);
		u->ConsistChanged(CCF_ARRANGE);
		return false;
	}
```

⇒ **两列车完全恢复接触前的状态**：链序还原（`RestoreTrainBackup`）、方向/段头/artic 角色位还原（`R3RUndoLogicalFlip`）、缓存重算（`ConsistChanged`）。

不合并、不移动、不损坏、不扣钱、不伤乘客、不发新闻。**唯一的痕迹是这条分支本身不打日志**（现场只能从"没出现 `COUPLE-OK`"反推）。

### 1.2 然后每 tick 重试

`return false` ⇒ 本 tick 不挂接。机车的 `GOTO_COUPLE` 订单仍在，下一 tick `TrainCoupleHandler()` 再次搜索 → 两车仍接触 → 再次 `TryTrainCouple` → 再次失败 ⇒ **每 tick 一次，无限僵持**。

玩家可见现象：**机车贴着车底停住，既不挂上，也不撞毁**（正是 KI-106「机车恒 1px 重叠冻结」的形态）。

### 1.3 已有兜底：M3（玩家昨天刚拍板保留）

```11951:11968:src/train_cmd.cpp
	/* R3R (KI-193 fallback, round 114): a rendezvous which cannot be committed.
	 * Reaching this point means a qualifying waiting consist was found right here
	 * (R3RCanCoupleNow() passed) and Couple() ran -- this is NOT the "no consist wants
	 * to couple with me" case, which waits in place for ever (see the u == nullptr
	 * branch). It is "the partner is standing there, but the merge does not happen":
	 * a fold-correction rollback, a NewGRF "can attach wagon" veto, an articulation
	 * deadlock, ... Retrying that for ever leaves the loco parked on the platform and
	 * freezes the whole schedule behind it, so after R3R_COUPLE_COMMIT_FAIL_LIMIT
	 * failed attempts give up the rendezvous and advance the order, exactly as if the
	 * player had skipped it in the orders window. That is a safety valve for a broken
	 * coupling, not the normal way of giving up a rendezvous -- an ordinary coupling
	 * never gets near the limit.
	 * Counted only for a stationary loco standing ON its destination: a loco that is
	 * still shunting towards it has not had its rendezvous yet and must keep going. */
```

**这段注释里赫然写着 `a NewGRF "can attach wagon" veto`** —— 玩家说的「无法 attach wagon」正是 M3 注释里点名的场景之一。

`R3R_COUPLE_COMMIT_FAIL_LIMIT = 4`（`:11650`），注释自述「Four windows are about a minute of game time at normal speed」。每个等待窗口 = `R3R_COUPLE_DEST_IDLE_LIMIT = 500` tick（`:11616`，约 15 秒）。

满 4 次 ⇒ `COUPLE-SKIP-COMMIT-FAIL`（`:11999`）⇒ `R3RUnpairCoupleTargets()` 丢 pair lock + 完整复刻 `CmdSkipToOrder()` 簿记 ⇒ **跳过订单**。

---

## 二、⚠️ 关键发现：玩家的希望 = 把 M3 的动作从「跳过订单」换成「撞毁」

| | 现在（M3） | 玩家希望 |
| --- | --- | --- |
| 触发点 | 有候选 + 已到目的地 + 合并未提交，累计 4 次（约 1 分钟） | 同一条路径 |
| 动作 | 丢 pair lock + 跳过订单 | `TrainCrashed()` |
| 状态 | **玩家 2026-10-03 明确「M3 不用改了」** | 与本条冲突 |

**同一个触发点不可能既"跳过订单"又"撞毁"。** 必须先二选一，或明确分工（见 §五 建议 B）。

---

## 三、attach 为什么会失败

```2600:2622:src/train_cmd.cpp
static CommandCost CheckTrainAttachment(Train *t)
{
	/* No multi-part train, no need to check. */
	if (t == nullptr || t->Next() == nullptr) return CommandCost();

	/* The maximum length for a train. For each part we decrease this by one
	 * and if the result is negative the train is simply too long. */
	int allowed_len = _settings_game.vehicle.max_train_length * TILE_SIZE - t->gcache.cached_veh_length;

	/* For free-wagon chains, check if they are within the max_train_length limit. */
	if (!t->IsEngine()) {
		t = t->Next();
		while (t != nullptr) {
			allowed_len -= t->gcache.cached_veh_length;
			t = t->Next();
		}

		if (allowed_len < 0) {
			R3RDumpAttachFail(t, nullptr, allowed_len, 0, STR_ERROR_TRAIN_TOO_LONG);
			return CommandCost(STR_ERROR_TRAIN_TOO_LONG);
		}
		return CommandCost();
	}
	...
	if (allowed_len < 0) {
		R3RDumpAttachFail(head, nullptr, allowed_len, 0, STR_ERROR_TRAIN_TOO_LONG);
		return CommandCost(STR_ERROR_TRAIN_TOO_LONG);
	}
	return CommandCost();
}
```

只有两种失败原因：

1. **`STR_ERROR_TRAIN_TOO_LONG`**（`:2619` / `:2706`）：合并后总长超过 `_settings_game.vehicle.max_train_length`（玩家设置，默认 7 格）。
2. **NewGRF 的 articulation / attach 回调拒绝**（`R3RDumpAttachFail` 的 `cb=0x%X` 参数，`:2584`），探针行 `ATTACH-FAIL head=… cb=0x…`。

⇒ **原因 1 是玩家的编组规模设置，不是物理相撞。** 用车毁去惩罚"我挂上去之后列车太长"没有任何语义依据——玩家只要把 `max_train_length` 调小就会触发。

---

## 四、回答「如果两列车接触了」

必须先分清两种"接触"：

| 接触场景 | 走哪条路径 | 结果 |
| --- | --- | --- |
| **没有挂接意图**（两列都在跑/一方在跑） | `CheckTrainCollision()`（`:15007`） | **已经会撞毁**了（`:14995`） |
| **有挂接意图**（机车执行 `GOTO_COUPLE`，对方 `WAIT_COUPLE`） | `TrainCoupleHandler()` → `GetCouplePosition()` → `TryTrainCouple()` | **免死金牌**：`min_diff` 故意 -1px（`:14851`）、`SEAM-FREE` 放行（`:14942`）；attach 失败则**完整回滚 + 每 tick 重试**，既不挂也不撞 |

⇒ 玩家想改的，本质是**关掉"挂接意图"这块免死金牌**：只要有挂接意图，两车再重叠也永远撞不了。

这是个真问题。但要小心：这块免死金牌正是挂接功能本身赖以工作的东西（第 215 轮 §二）。

---

## 五、撞毁方案的评估

### 支持

- 符合直觉：「接触了却挂不上 = 出事故了」，与真实铁路语义一致。
- 有先例：`COUPLE-SEAM-CRASH`（`:10518-10535`）就是"拼错了就立即撞毁 + 发新闻"，且已落地运行多轮。

### 反对（按严重度）

1. **会废掉重试机制。** 第 172 轮现场：机车连续 6 次失败、每次失败**北推 1px**，第 6 次才 `COUPLE-OK` 成功挂上。若"首次 attach 失败即撞毁"，那次成功挂接会变成**车毁事故**。重试是 R3R 挂接能成功的正常组成部分，不是异常。
2. **触发原因常常不是相撞。** `STR_ERROR_TRAIN_TOO_LONG` 是编组规模，`NewGRF` 回调拒绝是 GRF 规则，都不是"两车相撞"。
3. **与 M3 冲突**（§二）：一处两动作。
4. **自动路径、玩家不可预期。** 机车按订单自动去挂，玩家只是设了 `GOTO_COUPLE`，车却炸了，且没有可理解的因果提示（`COUPLE-SEAM-CRASH` 至少发 `STR_NEWS_TRAIN_CRASH` 新闻）。
5. **"这两列车"的销毁范围。** `TrainCrashed(v)`（`:14733`）只毁**一条链**。此时 `RestoreTrainBackup` 已把两车还原成两条独立链 ⇒ 要毁两列需 `TrainCrashed(v)` + `TrainCrashed(u)` 各调一次。而 `Crash()` 会伤乘客、清预订、掉评级——**车底可能正载着货和乘客**。
6. **收尾状态。** 撞毁后必须清：`_r3r_couple_commit_fail`（`:11642`）、pair lock（`R3RUnpairCoupleTargets`）、`_r3r_couple_dest_idle`、`_r3r_couple_no_target` 等计数表，以及已崩溃链的 orders 归属——否则留下悬垂状态（对照第 47 轮 `loading_vehicles` 悬垂导致的读档崩溃）。
7. **崩溃车会阻塞线路**：撞毁车留在轨道上会持续占格（`ReserveTrackUnderConsist()`），后续列车绕行/卡死。

### 建议形态（三选一）

| 方案 | 做法 | 评价 |
| --- | --- | --- |
| **A** | 在 **M3 的位置**（4 次失败、约 1 分钟后）撞毁，替换掉"跳过订单" | 最稳：复用一个已验证不会误触发的触发点，保留重试机会（第 172 轮那次 6 次重试是在**同一次**提交里的改写，不是 4 个窗口，需实测确认不误伤） |
| **B** | **按原因分工**：`STR_ERROR_TRAIN_TOO_LONG`（编组规模）→ 走 M3 跳过订单；NewGRF 回调拒绝 / 折叠死锁（几何不自洽）→ 撞毁 | 语义最准：只有"几何/规则上真的不该在那儿"才炸 |
| **C** | 不撞毁，改成**拒绝 + 把机车推回合法距离**（或后退一格再等） | 最温和，但需要新的位置重排机制（第 215 轮建议 C） |

**本报告倾向 A 或 B**，不建议"首次 attach 失败即撞毁"。

---

## 六、待玩家拍板

1. **触发时机**：首次 attach 失败就撞毁（会废掉重试，第 172 轮反例）？还是复用 M3 的 4 次窗口？
2. **与 M3 的分工**：M3 改成撞毁？还是 M3 保持跳过订单、只在"几何不自洽"类失败时撞毁（方案 B）？
3. **销毁范围**：只毁机车链，还是机车链 + 车底链都毁（车底可能载客载货）？
4. **失败原因是否区分**：`STR_ERROR_TRAIN_TOO_LONG` 要不要排除在撞毁之外？
5. **是否接受**：撞毁后需补的清理（计数表 / pair lock / 崩溃车占轨阻塞）作为配套设施一并做。

## 七、本轮未做

- 未改任何源码，未构建，未新建任何 `.cmd`。
- 未取实机数据：`ATTACH-FAIL` 探针行（`:2584`）在历史日志里**出现过几次、什么原因**尚未统计——**这是决定方案 A/B 的第一手数据**，建议先查 `build\R3R_debug.log`。
- 未核对第 172 轮的 6 次重试是否发生在单次 `TryTrainCouple` 内部（那不会触发 M3 计数）还是跨了 4 个等待窗口（会触发）。这一条直接决定方案 A 是否安全。
