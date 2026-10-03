# 第 182 轮：号侧「链头本来就没有号」的借用归还缺一支（KI-279）

日期：2026-10-01　范围：`src\train_cmd.cpp`（+13 行，未碰任何 `src\*.h`）

---

## 一、本轮缘起

第 181 轮续把 KI-277（名 / 号两侧借用不对称）修完，其「未做项 4」留下"反例回归只做了代码走查"。本轮按该走查清单逐条核对 `r3r_orders_borrowed` 与三特质（`unitnumber` / `name` / `group_id`）的**停放—归还配对**，发现**号侧与名侧仍不对称**：名侧在 KI-277 里补了两支，号侧只有一支。

---

## 二、取证（代码锚点）

### 2.1 名侧（第 181 轮续 KI-277，已修，作为模板）

`src\train_cmd.cpp` `DecoupleTrain()`：

```
7644: /* R3R (第 181 轮 / KI-277): u 侧先取回，必须早于下面 v 侧的判据 */
7646: if (u->name.empty()) R3RRestoreTrainName(u);
7647: if (!v->name_backup.empty()) { ... R3RDbgWrite("NAME-RESTORE veh=%d side=head\n", ...); }
7651: } else if (!u->name.empty() && std::string_view(v->name) == std::string_view(u->name)) {
7657:     v->name.clear();
7658:     R3RDbgWrite("NAME-RESTORE veh=%d side=head-cleared\n", ...);
7659: }
```

⇒ 名侧**两支**：「有停放副本」+「链头本来无值但有借用凭据（活字段仍等于等待方自己的名字）」。

### 2.2 号侧（本轮修前：只有一支）

```
7616: /* Restore the unit numbers: the decoupled part keeps its own number ... */
7618: if (v->unitnumber_backup != 0) {
7625:     if (u->unitnumber != 0 && u->unitnumber != v->unitnumber) ReleaseID(u->unitnumber);
7628:     u->unitnumber = v->unitnumber;
7632:     u->unitnumber_backup = 0;
7633:     v->unitnumber = v->unitnumber_backup;
7634:     v->unitnumber_backup = 0;
7635: }
```

⇒ **没有 `else if`**。"链头本来无号"时 `v->unitnumber_backup == 0`，整支被跳过。

### 2.3 为什么链头会没有号

- `Couple()` 借号块整块以 `u->unitnumber != 0` 为前提；v 侧停放条件 `if (v->unitnumber_backup == 0) v->unitnumber_backup = v->unitnumber;`（10229 一带）—— v 无号时停进去的就是 0。
- `R3RRestoreUnitNumber()`（2841）守卫是 `head->unitnumber != 0 || head->unitnumber_backup == 0` ⇒ 对无号链头是**空操作**。
- `NormaliseTrainHead()` 明确**不给 `GVSF_VIRTUAL` 的虚车发号** ⇒ 假引擎链头正常 `unitnumber == 0`。
- 解挂的 car-only 分支先让 u 取回 / 领取新号（7315 一带 `R3RRestoreUnitNumber(u); if (u->unitnumber == 0) 领新号`）。

### 2.4 重号是怎么产生的

设链头 v 本来无号、等待方 u 持有号 A：

| 时刻 | v->unitnumber | v->unitnumber_backup | u->unitnumber | u->unitnumber_backup |
|---|---|---|---|---|
| Couple 前 | 0 | 0 | A | 0 |
| Couple 后 | **A** | **0** | 0 | A |
| Decouple 后（修前） | **A** | 0 | **A** | 0 |

即：**号池一份登记 A，两辆车的活字段都等于 A**（真重号）。任一方随后被删 / 被卖，`ReleaseID(A)` 会把仍在使用中的 A 放回号池 ⇒ 号池把 A 再分配给新车 ⇒ 长期串号。

---

## 三、结论

**KI-279 成立（严重度 中）**：号侧归还缺「链头本来无号」一支。修法与名侧 `side=head-cleared` 逐字同构，条件严格、可逆、**不 ReleaseID**（号必须留回对方）。

```cpp
} else if (u->unitnumber != 0 && v->unitnumber == u->unitnumber) {
    v->unitnumber = 0;
    v->unitnumber_backup = 0;
    R3RDbgWrite("UNIT-RESTORE veh=%d side=head-cleared id=%u\n", ...);
}
```

---

## 四、反例（不误伤的论证）

| 情形 | 结果 |
|---|---|
| u 无号耦合 | 号块整块跳过，v 两字段不变；解挂时 u 若领新号 B ⇒ `v->unitnumber == u->unitnumber` 为假，不触发 |
| v 有号耦合 | `v->unitnumber_backup = 自己的号 != 0` ⇒ 走原支，不触发 |
| u 非 car-only（不领新号、未取回） | `u->unitnumber != 0` 可能为假 ⇒ 不触发（此时 u 活字段为 0，也不构成重号） |
| 号池唯一性 | u 新领的号不可能等于 v 的号，故新分支不会在"正常"路径上误命中 |

---

## 五、车库拖动边为何不加同一支

`R3RBorrowControlTraitsLive()`（5642 一带）让链头穿的是 `ctrl->unitnumber_backup`，且**不清空 ctrl 的活字段与备份** ⇒ 号池始终只有一份登记，控制段与链头是**同一个号的两处引用**（既有设计，见 5584 / 5608 注释）。因此车库侧不存在"两辆车的活字段同号"这一重号形态，无需补支。

若将来把控制段的号也改成取回活字段（像 `Couple()` 那样 `u->unitnumber = 0`），**必须同步补这一支**。

---

## 六、构建自证

复用 `_tmp_inc_build.cmd`（未新建 .cmd）：

- `build\R3R_incbuild.guard.log`：`GUARD: incremental is safe (no header/lang file is newer than the newest object)`
- `build\R3R_incbuild.log` 尾：`[3/3] Linking CXX executable openttd.exe`
- `build\R3R_incbuild.done`：`EXIT_CODE=0`
- 错误计数：`error C*` / `fatal error` / `FAILED:` / `build stopped` = **0**
- 时间戳：`src\train_cmd.cpp` 12:28:35 → `train_cmd.cpp.obj` 12:34:17 → `build\openttd.exe` @2026-10-01 12:40:19（51 561 472 B）
- `read_lints` 0 条；exe 命中 `UNIT-RESTORE veh=%d side=head-cleared id=%u`

---

## 七、未确认项 / 待复测

- **R-1（未确认）**：现场是否存在"虚车链头做耦合主动方"的实例。本轮无新日志，只有代码推演；若存档中从未出现，则该缺陷是"潜在"而非"已发生"。复测判据 1 可直接证实。
- **R-2（未做）**：解挂时若 u 走"不领新号"的非 car-only 路径，`u->unitnumber` 可能保持 0 ⇒ 新分支不触发（v 仍顶着 A、u 无号但 backup 有 A）。这种形态下不构成重号，是否需要进一步归一化未知，本轮未动。
- **R-3（未做）**：旧档不追溯 —— 已落盘的重号不会自动纠正，需解挂后重挂一次或手动改号。

待复测（3 条，同 `R3R_KNOWN_ISSUES.md` 第 182 轮小节）：

1. 无号链头（虚车链头，或 `CTRL-PARK` / `UNIT-PARK` 里 `bk=0`）耦合后再解挂 ⇒ 出现 `UNIT-RESTORE veh=N side=head-cleared`，链头回到无号、等待方保住自己的号，界面**不出现两列车同号**；
2. 反例不误伤：链头本来有号的解挂仍走原 `side=head` 路径（`UNIT-RESTORE veh=N id=X`），号池不泄漏（对照 KI-147 的释放计数）；
3. 存档往返后不出现重号；`CTRL-PARK` 段行号与界面号一致、`SEGFRONT-RESYNC-CHECK` 无异常。
