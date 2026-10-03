# R3R 第 209 轮备忘 — 玩家三条拍板 + KI-315 探针门禁修复

- 日期：2026-10-03
- 性质：**一条拍板落实 + 一处修码（仅 `src\train_cmd.cpp` 两行门禁，无头文件改动 ⇒ 增量合规）**
- 输入：第 208 轮现场（`build\R3R_debug.log`，371 行 / 2026-10-03 04:44）与源码取证
- 玩家原话：KI-313「否」、KI-314「否」、KI-315「可以修一修」
- 交付顺序：先写本备忘 + 更新 KI 条目（`R3R_KNOWN_ISSUES.md`），再改码、再构建

---

## 一、KI-313 拍板＝否：不做「编辑/复制命令时保留临时挂接分组」

**维持现状**：订单窗口新建/复制的命令默认不带「临时挂接分组」；重设调度后若原命令带分组，需玩家自己把分组重新设上。

现场锚点（第 208 轮）：

```
332:CG-GATE compatible=0 coupler=54 target=0 cdecl=0 tdecl=1 cmask=0x0 tmask=0x1
361:CPL-GATE reject=pair-mismatch  site=depot act=54 tgt=0  aOrd=16 tOrd=17 aStop=0 tStop=0 grp=0
```

对照第 204 轮同场景成功挂接：`CG-GATE compatible=1 coupler=54 target=0 cmask=0x1 tmask=0x1`。差别就在主动方 `cmask`：`0x1`（带 0 号组）→ `0x0`（编辑后丢了）。

判定为**数据/操作问题**，非代码缺陷：分组在 KI-306（第 202 轮）口径下只承担「跨公司授权 + 挂接白名单」，值是玩家数据；代码替玩家"猜"编辑前的意图会引入隐式行为。拍板否，故不改码。

**操作提示（给玩家，不改码）**：重设调度后，把主动方 `GOTO_COUPLE` 的「临时挂接分组」重新设成与等待方一致的组；或把等待命令上的分组声明清掉。两者取一即可恢复挂接。

---

## 二、KI-314 拍板＝否：手动停止的车底不作挂接目标

**维持现状**：`vehstatus.Stopped` 的链在 `TrainLocoHandler()` 早退 ⇒ `current_order` 恒 `OT_NOTHING` ⇒ 挂接闸门读 `tOrd=0` 判 `target-not-wait` 拒绝。R3R 自身停放（`r3r_parked`）的豁免**保留不动**。

现场锚点：

```
312:SKIP-STOPPED veh=27 order=0 real=17 spd=0 tile=1,15 parked=0 front=1 nord=33
360:CPL-GATE reject=target-not-wait site=depot act=54 tgt=27 aOrd=16 tOrd=0 aStop=0 tStop=1 grp=0
308:DEPOT-ARR veh=0 real=0(17) curType=17        ← 对照组：未停止的等待车底正常加载 WAIT_COUPLE
```

拍板否的合理性：手动"停止"是玩家的**显式意图**（不接受调度），与「停放等待」（R3R 语义：等人挂）必须区分，否则会把玩家的停止操作解释成"在等挂"；需要时按「开始」即可恢复为合法目标。

**记录一个已知副作用（不修）**：若玩家把等待车底停在站台上并忘了按「开始」，日志会持续出现 `CPL-GATE reject=target-not-wait` + `COUPLE-FAIL` + `COUPLE-DEST-EMPTY`（第 208 轮 L359-365）；这是现状语义的必然表现，属玩家可见的提示性噪音。

---

## 三、KI-315 修复（已实施）

### 3.1 问题（源码事实）

第 205~207 轮新增的订单索引哨兵是**唯一一批「先做真活、后调日志」**的探针：

- `R3RWatchOrderIndex()`（`src\train_cmd.cpp:1898`）函数体没有早退门，进入后立刻：算 key → 查/写函数内静态 `std::unordered_map<uint64_t, R3RIdxSnap>`（:1916-1919）→ `R3RChainHasRealArticPart(t)` 全链扫描（:1922）；
- 末尾 `R3RDbgWrite(...)`（:1931）在 `R3R_PROBES=0` 下展开为 `((void)0)`（`src\r3r_perf.h:240-241`）⇒ **只丢日志与格式串**；
- 调用点未加 `#if R3R_PROBES`（`src\train_cmd.cpp` 与 `src\order_cmd.cpp` 内 `#if R3R_PROBES` 计数均为 **0**），全树 26 个调用点，其中 10 个在 `TrainLocoHandler` 每列车每 tick 的路径上（`loco-entry` 等），订单编辑另有 6 对。

⇒ 发行版（`R3R_PROBES=0`）表现为：无日志、无字符串，但**每 tick 每列的 map 记账 + 铰接链扫描仍在**。

### 3.2 修法（两处，各一行）

1. `R3RWatchOrderIndex()` 首行：`if (!R3RDbgOn()) return;`
2. `R3RWatchOrderIndexSite()`（:1959，非 static 薄包装，供 `order_cmd.cpp` 等别的 TU 打点）首行：同一门禁

### 3.3 为什么这样修就够

- `R3RDbgOn()` 在 `R3R_PROBES=0` 时是 `return false;`（`src\r3r_perf.h:143-156`）⇒ 门禁被 MSVC 折叠为 `if (false) return;`，整个函数体成为死代码并被删除（静态表、key 计算、`R3RChainHasRealArticPart()` 扫描一并消失），且不产生任何指令。
- 运行期 `R3R_DBG=0`（KI-26 的「探针真的关掉」A/B 基准）时同样短路 —— 这一半是第 208 轮没能修掉的额外收益。
- 内测树（`build\`，`R3R_PROBES=1`）行为**逐字不变**：`R3RDbgOn()` 为真，日志内容/顺序/条数完全一样。

### 3.4 范围与不动项

- 不碰 `src\*.h`（避开 KI-183 护栏 → 保持增量构建）。
- 不碰 `R3RDbgWrite` 宏本身（它对"纯日志"探针仍然正确）。
- 不改任何探针的判据、字段、调用点与输出格式。
- `src\order_cmd.cpp:83-100` 的 RAII `R3ROrderIdxWatch` 不动：它构造/析构各调一次 `R3RWatchOrderIndexSite`，已被 3.2 第 2 条覆盖；探针关闭时退化为两次空调用（构造仍是 `Train::From` 判断，可接受；进一步优化需给它加 `#if`/门禁，留待真有 A/B 需求时再做）。

---

## 四、构建自证

复用既有 `_tmp_inc_build.cmd`（未新建任何 `.cmd`）：

- 护栏：`GUARD: incremental is safe (no header/lang file is newer than the newest object)` ⇒ 增量合法（未碰 `src\*.h` / `src\lang\*.txt`）。
- 构建：`[3/3] Linking CXX executable openttd.exe`；`build\R3R_incbuild.done` = `EXIT_CODE=0`；错误计数 0（`findstr /C:"error C" /C:"fatal error" /C:"FAILED:" /C:"build stopped"` 零命中）。
- 时间戳链：`src\train_cmd.cpp` 15:30 → `build\CMakeFiles\openttd_lib.dir\src\train_cmd.cpp.obj` 15:32 → `build\openttd.exe` 2026-10-03 15:35（51 637 248 B；上一版 51 634 176 B，差 **+3 072 B** ＝ 两处门禁在"探针开启"的内测树里仍编入，符合预期）。
- 产物自证：内测 exe 仍命中 `ORD-IDX-WATCH` ⇒ 探针开启时行为/日志不变。
- 未做：发行树（`build-release\`）未重编，故"发行版里静态表与链扫描确实消失"目前是**源码层判定**（`R3RDbgOn()` 在 `R3R_PROBES=0` 下为常量 `false`，`src\r3r_perf.h:153-155`），下次重编发行树时可用 exe 复核。

---

## 五、复测判据

1. **内测 exe（探针 ON）无行为变化**：跑既有场景，`ORD-IDX-WATCH` 行的内容/条数与第 208 轮同场景相当（首行 `-1->` 前缀、分桶 per site 不变）。
2. **关掉探针时真正省下那笔记账**：`R3R_debug.log` 无 `ORD-IDX-WATCH`（注意这一点修复前后**都一样** —— `R3RDbgWrite` 自身就有 `if (!R3RDbgOn()) return;`，`src\r3r_perf.h:226`；本次修的是它前面的记账与链扫描，不是日志）。要观测这笔节省，用 `R3R_DBG=0 R3R_PERF=1`（日志关、计时开，正是 `r3r_perf.h` 注释里推荐给 A/B 的组合）跑同一场景，比对 `R3R_perf.log` 的 `TrainLocoHandler` 桶 ns。
3. **观测能力不退**：订单编辑（删/插/拖动/改属性/批量）后 `real` 仍能被哨兵钉住（探针 ON 时）。
4. **第 208 轮结论不退**：仍无 `0->1`（除 `cmd-skip`）；KI-310「已结案·非缺陷」不变。
5. **工具陷阱复核**：凡"某写法是否存在/有几处"的判定一律用 `search_content(outputMode="content")` 或 `findstr /c`，禁用 `outputMode="count"`（本环境对同一行会误报 0）。
