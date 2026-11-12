# 第 410 轮：答案本失效通知接线 + 探针树/发布树性能疑问

玩家原话（2026-11-09，两条）：

> 补接线吧，还有，为什么你编译的是探针版啊，延迟高的离谱

对应上轮（第 409 轮）末尾留的两项：①`train_cmd.cpp` 侧显式 `R3RAnswerDrop` 调用点未接线；
②玩家实测用的是 `build\openttd.exe`（Debug 探针树），延迟不可接受。

---

## 一、为什么"换单"不能挂在 `InvalidateVehicleOrder()` 上

上轮设想的是在 `order_cmd.cpp` 的 `InvalidateVehicleOrder()` 里统一接线，本轮**否决**了这条：

- 全仓 `InvalidateVehicleOrder(` 调用点 **32 处**，其中大量是纯 UI 刷新（订单窗口重绘、到站后刷新），
  与"目的地变了"无关。挂在这里会把答案清得比命中还快，功能直接废掉。
- 更要命的是它**覆盖不全**：`ProcessOrders` 自动推进到下一条单、条件单跳转、`CmdSkipToOrder` 的内部推进
  都可能不经过它。漏掉任何一条，就会出现"抄旧答案开去旧目的地"。

**采用的方案（治根、零外部通知）**：答案本自己记住"这条答案是为哪个订单目的地算的"，回放时比一次。

- 记录：`R3RAnswerCommit(veh, order_dest)` 存 `veh->dest_tile`（**订单目的地**，不是 A* 的 `res_dest_tile`）。
- 回放：`R3RAnswerReplay()` 在找到 `steps` 之后、按 `(tile, td)` 定位之前，比对
  `_r3r_answer_dest[key] == veh->dest_tile`，不等即 `R3RAnswerDrop(veh->index, "dest-changed")` 并回落 A*。
- 为什么必须"记录时存"而不是"用当前值现算"：`res_dest_tile` 是 A* 算出来的具体终点格（可能是站台某一格），
  `dest_tile` 是订单目的地，两者不同概念，不能互比；只有前后两次都取 `dest_tile` 才自洽。

这条比对同时覆盖：玩家改单、自动跳到下一条单、条件单分支、GOTO_* 互换、订单表被整体替换。
代价 = 回放路径上一次 `map::find`（本来就要 find 一次 `_r3r_answer_steps`，同一量级）。

## 二、显式接线三处（链结构/朝向变化，`dest_tile` 抓不到）

这三类的共同点：**订单目的地一点没变，所以上面那条比对抓不住**，但答案已经错。

| 场景 | 落点 | reason | 为什么答案会错 |
|---|---|---|---|
| 卖车 | `CmdSellRailWagon()`，`MakeTrainBackup()` 之后（链还完整） | `sell` | `VehicleID` 被释放，新车复用同一 index，会抄到别人的答案 |
| 解挂 | `DecoupleTrain()`，`TryTrainDecouple()` 成功之后 | `decouple-front` / `decouple-rear` | 一条链拆成两条，两半的链头身份都换了 |
| 掉头 | `ReverseTrainDirection()`，紧邻 `R3RMarkChainJustReversed()` | `reverse` | 整列 direction 反转，旧答案是一条单向序列，对新的 `origin.trackdir` 无意义 |

新增文件内 helper（`train_cmd.cpp`，放在 `CmdSellRailWagon()` 之前，供其后三处调用）：

```cpp
extern void R3RAnswerDrop(VehicleID veh, const char *reason);

static void R3RDropAnswerChain(const Train *head, const char *reason)
{
	for (const Train *w = head; w != nullptr; w = w->Next()) R3RAnswerDrop(w->index, reason);
}
```

逐节清而非只清链头：任何一节都可能在后续编辑（车库拖动、解挂）里成为链头。

**刻意不接的**：
- `CmdMoveRailVehicle()`（车库拖动）：库内不寻路，出库后 origin 是库格，与旧答案首格定位必然失败，
  天然失效；接了只是多一次无谓遍历。
- `InvalidateVehicleOrder()`：理由见 §一。

## 三、"为什么编译的是探针版"——不是本轮的错，是既有口径

- `build\` 是 **CMAKE_BUILD_TYPE=Debug**：`/Zi /Ob0 /Od /RTC1 -MTd -D_DEBUG -DDBG_ASSERTS`，
  **零优化、禁内联、运行时检查全开、断言全开**。同机器上比 Release 慢 5~20× 是常态，
  在 YAPF 这种重循环 + 虚调用密集的仿真里更明显。
- 既有口径（用户 2026-10-19 明确要求）：日常改完源码只编探针内测树，发布树只在明确要求时编。
  所以第 408/409 轮只有 `build\openttd.exe` 更新，`build-release\openttd.exe` 停在 2026-11-08 19:04（不含答案本）。
- **玩家实测/谈手感必须用 `build-release\openttd.exe`**。`build\` 的用途只有一个：采探针日志定位 bug。
- 本轮按此要求补编发布树（见 §四）。

## 四、构建自证

### 内测树（探针 ON，只验证编译）

- 入口：`R3R_fullbuild.cmd inc detach`（复用既有入口，未新建任何 `.cmd`）
- 结果：`__FILL_IN__`

### 发布树（RelWithDebInfo /O2 /Ob2，玩家实测用这个）

- 入口：`R3R_release_build.cmd`（增量，写 `build-release\R3R_release_build.log` + `.done`）
- 结果：`__FILL_IN__`

## 五、复测判据

1. 同档同操作**不再崩**，无新 `crash-*.log`（第 409 轮的 nullptr 修复）。
2. 出现 `ANSWER-REPLAY ... booked=N`（N≥1），证明回放真在跑而不是全程回落 A*。
3. 出现 `ANSWER-STORE ... dest=x,y`，`dest` 等于该车当前订单目的地。
4. **改单判据**：车在半路上改订单目的地 ⇒ 必须出现 `ANSWER-DROP ... reason=dest-changed`，
   且随后开往**新**目的地（不得出现"照旧答案开到旧站"）。
5. **卖车判据**：卖车/卖整列后出现 `ANSWER-DROP ... reason=sell`（有答案时才打）。
6. **解挂判据**：解挂后出现 `reason=decouple-front` 与/或 `decouple-rear`。
7. **掉头判据**：站台/路点掉头后出现 `reason=reverse`。
8. `ANSWER-DROP reason=nothing-booked` 不刷屏。
9. 发布树 `build-release\openttd.exe` 延迟回到可接受范围（对比 `build\openttd.exe` 应有数量级差异）。

## 六、状态

- [x] 代码：`yapf_rail.cpp`（dest 记录/比对）、`train_cmd.cpp`（helper + 三处接线）
- [x] `read_lints` 两文件 0 条
- [ ] 内测树编译
- [ ] 发布树编译
- [ ] 游戏内复测（9 条判据）
