# 第 431 轮：非段链检测导致崩溃（KI-625）

**性质**：上一轮（第 429 轮，KI-624）引入的空指针解引用崩溃，**AI 自己的回归**。
**状态**：根因已做实（静态）→ 待修复 + 编译 + 复测。

---

## 一、现场（玩家 2026-11-13 03:26 / 03:28 两次连崩）

日志目录：`C:\Users\冯洁敏\Documents\OpenTTD\`

| 文件 | 大小 | 时间 |
| --- | --- | --- |
| `crash-20261112T192643Z.log` | 3 837 B | 03:26:45 |
| `crash-20261112T192831Z.log` | 3 836 B | 03:28:32 |

两次崩溃**完全同址**，玩家重开即再崩（可稳定复现）。

```
Build: r3r-stable-2026-11-13 (Release: 0.73.1), Build date: Nov 13 2026 02:39:15
Crash at: R3RSkipCoupleOrdersForRealArtic + 142 (src\train_cmd.cpp:22611)
Faulting instruction: 80 79 3C 00        ; cmp byte ptr [rcx+3Ch], 0
RCX = 0000000000000000   RAX = 0   RDX = 0
RDI = 00000100000001B3
Stack trace:
  [00] R3RSkipCoupleOrdersForRealArtic + 142   (src\train_cmd.cpp:22611)
  [01] TrainLocoHandler + 4404                 (src\train_cmd.cpp:23131)
```

读 `nullptr + 0x3C` ⇒ `0x3C` 就是 `Vehicle::type` 的偏移（`assert(v->type == Type)`），崩溃是**空指针的成员访问**，不是链数据损坏。

## 二、根因（static 已确认，无需再取证）

`src\train_cmd.cpp:22611`：

```cpp
const bool gate_non_segment = !R3RChainAllWagonsSegmented(consist);
```

被内联的 `R3RChainAllWagonsSegmented`（同文件 22544 起）写成：

```cpp
for (const Train *v = t; v != nullptr; v = Train::From(v->Next())) { ... }
```

`Rain::From` 的实现（`src\vehicle_base.h:1783-1787`）：

```cpp
static inline T *From(Vehicle *v)
{
    assert(v->type == Type);   // ← 先解引用，再转换
    return (T *)v;
}
```

⇒ **链尾车的 `v->Next()` 是 `nullptr`，`Train::From(nullptr)` 直接进 `assert` 解引用空指针**。
release 树带 `-DOPTION_USE_ASSERTS=ON` ⇒ `WITH_ASSERT` 生效 ⇒ 断言解引用执行 ⇒ 读 `[0+0x3C]` 崩溃。

反汇编与偏移逐字吻合：`80 79 3C 00` = `cmp byte ptr [rcx+3Ch],0`，`rcx=0`；`VehicleType::Train == 0`，所以比较立即数是 `00`。

**为什么第 429 轮没发现**：内测树（`build\`）从未跑过非段链场景；玩家第一次实机测试用的就是发行版 v1.0.6。

## 三、疑点登记

- **疑点 A（已解）**：fault addr `0x3C` 而非 0 —— 说明是 `this + offsetof(type)` 的断言，不是 `next` 指针本身被读。
- **疑点 B（待查）**：全库是否还有其它 `X::From(y->Next())` / `X::From(y->GetNextVehicle())` 同类写法（同样的空指针断言路径）。第 429 轮这段是我新写的，但历史 R3R 代码可能同病。

## 四、修法（待实施）

把循环改成「先判空、再转换」，不再对可能为空的返回值调 `Train::From`：

```cpp
for (const Train *v = t; v != nullptr; ) {
    /* ... 判据 ... */
    const Vehicle *n = v->Next();
    v = (n != nullptr) ? Train::From(n) : nullptr;
}
```

判据本身（`RailVehInfo(v->engine_type)->railveh_type != Wagon` 且 `IsEngine() || IsDualHeadRear()`，段外出现原型为车厢者即非段）**不变**，只修崩溃。

同时全库扫一遍同类写法，命中即改。

## 五、构建与复测

已完成：

- [x] 修复 `src\train_cmd.cpp:22548` 的循环推进（改为显式判空后 `Train::From`），判据一行未动。
- [x] 同类写法全库排查：`src\couple_group.cpp:76` 的 `T::From(t->Next())` **上方已有** `if (t->Next() == nullptr) return nullptr;`，安全；`src\train_cmd.cpp` 内其余 `Train::From(...)` 的来源是 `First()/GetFirstVehicle()/已判空变量`，且 8484/8500/8518 三处本来就是 `(next != nullptr) ? Train::From(next) : nullptr` 的正确写法——说明这是仓库既有约定，第 429 轮新代码没照抄才出的错。
- [x] `read_lints`：0 条。
- [x] 内测树增量编译：`R3R_fullbuild.cmd inc detach` → `build\R3R_incbuild.done` = `EXIT_CODE=0`，日志 `[5/5] Linking CXX executable openttd.exe`，`build\openttd.exe` @**2026-11-13 03:35:41**（52 867 072 B）。

未完成（被玩家中止，等指示）：

- [ ] 发行树重编：发行包 v1.0.6 由本 bug 污染（`Build date Nov 13 2026 02:39:15`），必须重出才可发布；已在启动 `R3R_release_fullbuild.cmd` 时被玩家拒绝执行，未再尝试。
- [ ] 游戏内复测（判据见下）。

复测判据：
  1. 载入同一存档、同一位置不再崩溃（这是硬判据）
  2. 非段链（机车 + 未升段车底）仍然被跳过 R3R 命令，日志 `ARTIC-SKIP-R3R ... reason=non-segment` 照常出现
  3. 全段链（每节都在段内）不受影响，不出现 `reason=non-segment`
  4. 纯机车链（单节、`Next()==nullptr`）不崩溃 —— 正是本次崩溃点
  5. 含真铰接链仍走 `reason=real-artic`，两条闸门互不干扰
