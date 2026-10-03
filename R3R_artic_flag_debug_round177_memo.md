# R3R 第 177 轮临时分析报告 —— 「真假铰接」的 debug 判读方法与克隆去铰接可核化

- 日期：2026-10-01
- 轮次：第 177 轮
- KI 条目：KI-268（`R3R_KNOWN_ISSUES.md` 末尾「第 177 轮」小节）
- 改动文件：`src\vehicle.cpp`、`src\vehicle_cmd.cpp`（未碰任何 `src\*.h`）
- 状态：已实现 + 已编译；游戏内复测待做

---

## 一、玩家诉求

1. 第 170 轮「一附」曾做过"克隆（复制段/复制列车）时，把复制出来的链也执行去真铰接处理"的修复。
2. 玩家怀疑该修复**没有生效**。
3. 因此要求：**告诉我在 debug 模式下查看列车 flag、判断真假铰接的方法**。

## 二、取证：旧 exe 为什么"看不出"假铰接

### 2.1 `dump_vehicle` 的输出来源

```
console_cmds.cpp:3588  ConDumpVehicle("dump_vehicle <vehicle-id>")
    └─ IConsolePrint(..., VehicleInfoDumper(v))          // console_cmds.cpp:3597
        └─ vehicle.cpp  Vehicle::DumpVehicleFlags()       // 单行模式
           / Vehicle::DumpVehicleFlagsMultiline()        // 多行模式
                └─ DumpVehicleFlagsGeneric()              // vehicle.cpp:4678
```

`DumpVehicleFlagsGeneric()` 里与"铰接"有关的两段：

- `st:`（subtype）——`vehicle.cpp:4698` 一带，dump 字符 `F A W E f M V`，
  其中 `A` = `GVSF_ARTICULATED_PART` ⇒ **真铰接在旧 exe 里就能看到**。
- `tf:`（Train 的 `VehicleRailFlag`）——`vehicle.cpp:4740`–`4761`，列表止于
  `dump('c', "SpeedAdaptationExempt", ...)`。

**关键发现**：旧 `tf:` 列表的最后一项是 `SpeedAdaptationExempt`（bit 22），
而 `train.h` 里 `VehicleRailFlag` 还定义着：

| 位 | 枚举名 | 是否出现在旧 `tf:` dump |
|---|---|---|
| 2 | `SegmentFront` | 否 |
| 15 | `SegmentBack` | 否 |
| 24 | `ArticGroupHead` | 否 |
| 25 | `ArticGroupMember` | 否 |
| 26 | `SegmentFlipped` | 否 |
| 27 | `ForceReserveOnce` | 否 |

⇒ **去铰接组的组角色位（`H`/`M`）在旧 `dump_vehicle` 输出里完全不存在**。
这就是玩家"无法用 flag 判断真假铰接"的直接原因，不是玩家的操作问题。

### 2.2 旧的替代途径（无需重编即可用）

- `R3RDumpUpgradeDbg(head, tag)`（`vehicle_cmd.cpp:356`）⇒ 日志 `build\R3R_debug.log` 中
  `MAKESEG <tag> head=N` + 逐车 `subtype=0x%02x(... artic=...) rail(AH=... AM=... SF=...)`；
  `tag` 取值含 `UPGRADE-BEFORE-SPLIT` / `UPGRADE-AFTER-SPLIT`（段升级命令路径）。
- `R3RDumpCoupleIdentity(head, tag)`（`train_cmd.cpp:8320`）⇒ `IDENT <tag> head=N` +
  逐车 `subtype=0x%02x(... artic=...) AH=... AM=... SF=... dir=...`。
- 克隆路径的历史探针只有失败线：`R3R-CLONE-GROUP-MISMATCH src=%d dst=%d n_src=%u n_dst=%u -> sell clone`
  （`vehicle_cmd.cpp:2183`）——**成功克隆没有任何日志**，所以"修复有没有生效"在过去无法从日志判断，
  这正是玩家的怀疑无法自证的原因。

## 三、结论（真假铰接的判读口径）

以"位"为准，不看派生函数：

| 形态 | `st:` | `tf:` |
|---|---|---|
| **真铰接组** | 部件含 `A`（`GVSF_ARTICULATED_PART`） | 无 `H`、无 `M` |
| **假铰接（去铰接组）** | 全链**都不含** `A` | 父车含 `H`、部件含 `M` |

理由（`train.h`）：真实铰接组的角色从 subtype 推导，代码从不给它写 `ArticGroupHead`/
`ArticGroupMember`；只有 `DearticulateChainWithSnapshot()` / `R3RDearticulateOneGroup()` 把真实
铰接拆成独立车之后，才会显式打上 `H`/`M`。

## 四、本轮改动

### 4.1 `src\vehicle.cpp`：补全 `tf:` 的 R3R 位

在 `DumpVehicleFlagsGeneric()` 的 Train 分支、`SpeedAdaptationExempt` 之后追加：

| 字符 | 位 | 名称 |
|---|---|---|
| `H` | 24 | `ArticGroupHead` |
| `M` | 25 | `ArticGroupMember` |
| `S` | 2 | `SegmentFront` |
| `E` | 15 | `SegmentBack` |
| `Z` | 26 | `SegmentFlipped` |
| `O` | 27 | `ForceReserveOnce` |

- 与既有 `tf:` 字符 `R W P r h e q s L b p v z F B Y A K J X c` 无冲突。
- 追加在末尾 ⇒ 既有输出的前缀字符序列逐字不变。
- 单行模式打字符、多行模式打全名（沿用既有 `dump` lambda 机制）。

### 4.2 `src\vehicle_cmd.cpp`：克隆提交点加可核对日志

`CloneVehicle()` 的 `r3r_identity_rebuilt` 分支内（`ConsistChanged(CCF_ARRANGE)` 之前）追加：

```cpp
R3RDumpUpgradeDbg(Train::From(v_front), "CLONE-SRC");
R3RDumpUpgradeDbg(Train::From(w_front), "CLONE-DST");
```

只读，不改任何状态。

## 五、构建自证

复用既有 `_tmp_inc_build.cmd`（未新建任何 `.cmd`）：

| 项 | 实测 |
|---|---|
| 护栏 | `GUARD: incremental is safe (no header/lang file is newer than the newest object)` |
| 链接 | `[4/4] Linking CXX executable openttd.exe` |
| 判决 | `build\R3R_incbuild.done` = `EXIT_CODE=0` |
| 错误计数 | `error C* / fatal error / FAILED: / build stopped` = **0** |
| 时间戳链 | `src\vehicle.cpp` 00:33:22 → `vehicle.cpp.obj` 00:47:55；`src\vehicle_cmd.cpp` 00:33:40 → `vehicle_cmd.cpp.obj` 00:47:53 |
| 产物 | `build\openttd.exe` **2026-10-01 00:49:24（51 559 424 B）** |
| lint | `read_lints`（两文件）= 0 条 |
| exe 字面量 | `ArticGroupHead` / `ArticGroupMember` / `SegmentFlipped` / `ForceReserveOnce` / `CLONE-SRC` / `CLONE-DST` = **6/6 命中** |

## 六、未确认项 / 未做

1. **第 170 轮克隆去铰接修复是否真的生效，本轮未下结论**——本轮只把"能不能看出来"这件事解决掉，
   结论必须由玩家用新 exe 复跑后的日志给出（见第七节判据 3/5）。
2. `dump_vehicle` 仍只打**原始位**，不显示 `InArticGroup()` / `HasDearticulatedGroupRole()` 这类派生语义。
3. `R3RDumpUpgradeDbg` 的 `rail(...)` 仍只到 `AH/AM/SF`，未扩到 `SegmentBack`/`SegmentFlipped`/`ForceReserveOnce`，
   以免与 `train_cmd.cpp:8320` 的姊妹探针格式分叉。
4. 未给克隆路径加"失败以外的成功计数"，也未给 `dump_vehicle` 加"整链一次性 dump"的批量形式。

## 七、复测判据

1. 去铰接组：`dump_vehicle` 父车 `tf:` 含 `H`、部件含 `M`，且所有车 `st:` **无** `A`。
2. 真铰接车：`st:` 含 `A`、`tf:` **无** `H/M`（真实铰接列车读数不变）。
3. 车库克隆含去铰接组的链 ⇒ 日志出现成对的 `MAKESEG CLONE-SRC` / `MAKESEG CLONE-DST`，
   且逐行一致（`artic=0`、父车 `AH=1`、成员 `AM=1`）。
4. 克隆真铰接组 ⇒ **不出现** `CLONE-SRC`/`CLONE-DST`。
5. 反例：`CLONE-DST` 里若出现 `artic=1` 而无 `AM` ⇒ 第 170 轮修复失效，请把该段日志交给下一轮。
