# R3R 第 230 轮：库型 GOTO_COUPLE「到库后什么都不发生」—— 只读探针取证（KI-339）

- 日期：2026-10-10
- 对应 KI：**KI-339**（见 `R3R_KNOWN_ISSUES.md` 末尾「第 230 轮」小节）
- 状态：**仅加只读探针 + 已增量编译通过；根因未定，等玩家实机日志**
- 本轮改动：**仅 `src\train_cmd.cpp` 一个 `.cpp`**（**未碰任何 `src\*.h` / `src\lang\*.txt`，增量构建合法**）

---

## 一、来源：第 229 轮复测遗留的「判据 2 后半未走完」

第 229 轮（KI-338，库型挂接先去车库）已修+已编译+主症复测通过，但复测报告
`R3R_couple_depot_first_round229_memo.md` **§8.2** 留下一处未定性：

- 车 `veh=65` 修好后**能开出自家车库**并按 `GOTO_COUPLE` 一路开到目标库
  （`563 1184,185 → 646 1183,174 → 881 1174,138 → 1052 1137,138 → … → 1520 968,134`）。
- 末端却停在：
  ```
  1565/1569/1573/1577/1581/1585 CPL-PAIR … dist=0 actTile=964,133 tgtTile=964,133
  ```
  **反复 `dist=0`，而全日志无 `COUPLE-OK loco=65`，也无 `COUPLE-FAIL loco=65`。**
- 该处当时记为「待玩家确认是**日志截断**（`1586+` 已进入 `SEGLOAD-PARK` 读档段）还是**真的卡在库内未挂上**；若为后者，属『先去车库』修好后暴露的**后扫描新现象**，另立条目」。
  ⇒ 本条（KI-339）就是这「另立条目」的落点。**注意：截断 vs 真卡，至今仍未定性。**

玩家本轮原话（承接第 229 轮）：**「怎么又一通雷霆思考然后超上下文啊，如果你再不确定，就加探针，就来问我」**
⇒ 本轮遵照执行：**不加猜测、直接加决定性探针，然后交由玩家实机复跑取数。**

---

## 二、静默分叉点：唯一的「什么都不发生」出口

`TrainLocoHandler`（`src\train_cmd.cpp`）里与库型挂接直接相关的只有这一块：

```cpp
// :17349
if (consist->track == TRACK_BIT_DEPOT && consist->IsEngine()) {
    // :17360
    const bool couple_targets_this_depot = r3r_pending_depot_couple ||
            (consist->current_order.IsType(OT_GOTO_COUPLE) &&
            consist->current_order.GetCoupleIsDepot() &&
            IsRailDepotTile(consist->tile) &&
            consist->current_order.GetDestination().ToDepotID() == GetDepotIndex(consist->tile));
    if (couple_targets_this_depot) {
        // :17365 … 这里才会调 TrainCoupleHandler(consist)（:17394）
    } else if (!consist->current_order.IsType(OT_GOTO_COUPLE)) {   // :17421
        NormalizeTrainVehInDepot(consist, true);
    }
    // ← 订单是 OT_GOTO_COUPLE 且 couple_targets_this_depot 为假时，两个分支都不进：
    //   既不挂接、也不归一化，车原地停驻，且这一段代码**不打任何日志**。
}
```

即：**「到了库却什么都不发生」在代码上只有一个出口** —— `couple_targets_this_depot == false`
而订单仍是 `OT_GOTO_COUPLE`。该出口当前完全静默，所以玩家日志里才出现
「`CPL-PAIR dist=0` 反复 + 无 `COUPLE-OK` + 无 `COUPLE-FAIL`」这种三无形态。

---

## 三、本轮加的只读探针：`DEPOTBLK`

### 3.1 新枚举
`src\train_cmd.cpp` 的 `R3REDGE_*` 列表，插在 `R3REDGE_DUALHEAD` 之后、`R3REDGE_COUNT` 之前：

```cpp
R3REDGE_DEPOTBLK,     ///< KI-339: the depot GOTO_COUPLE block's own verdict (the silent branch)
```

### 3.2 探针本体（插在 `R3REnsureCouplePair(consist);`（:17313）之后、depot 块（:17349）之前）
只在 `consist->current_order.IsType(OT_GOTO_COUPLE)` 时求值，把该判据**四个子条件**连同
`track / IsEngine / Stopped` 一起打包成 payload，走 `R3RDbgEdge` 边沿触发（每车号每状态只一行）：

```cpp
if (consist->current_order.IsType(OT_GOTO_COUPLE)) {
    const bool r3r_on_depot_tile  = IsRailDepotTile(consist->tile);
    const bool r3r_couple_is_depot = consist->current_order.GetCoupleIsDepot();
    const uint r3r_dest_raw  = (uint)consist->current_order.GetDestination().base();
    const uint r3r_depot_idx = r3r_on_depot_tile ? (uint)GetDepotIndex(consist->tile).base() : UINT_MAX;
    const bool r3r_id_match  = r3r_couple_is_depot && r3r_on_depot_tile && (r3r_dest_raw == r3r_depot_idx);
    /* payload = track<<40 | isEngine<<32 | isdepot<<4 | ontile<<3 | idmatch<<2 | pend<<1 | stopped */
    if (R3RDbgEdge(R3REDGE_DEPOTBLK, (uint64_t)consist->index.base(), r3r_blk_pay)) {
        R3RDbgWrite("DEPOTBLK veh=%d tile=%d,%d track=%02X eng=%d isdepot=%d ontile=%d idmatch=%d pend=%d stopped=%d parked=%d spd=%d destRaw=%u depotIdx=%u\n", …);
    }
}
```

**零行为变更**（纯读 + 打日志），不影响任何耦合/寻路/停驻判定。

### 3.3 字段读法

| 字段 | 含义 / 判读 |
|---|---|
| `track` | `consist->track`；应含 `TRACK_BIT_DEPOT`（`0x80`） |
| `eng` | `consist->IsEngine()`；库型挂接要求链头是引擎，`0` ⇒ 身份层问题 |
| `isdepot` | `current_order.GetCoupleIsDepot()`；订单是否声明"挂接目标是车库" |
| `ontile` | `IsRailDepotTile(consist->tile)`；车此刻是否**真正站在库格里**（不是库门口/邻格） |
| `idmatch` | `dest == 本库 id`；目的地是否就是**当前这个**库 |
| `pend` | `r3r_pending_depot_couple`（R3R 库工具登记的待挂） |
| `stopped` / `parked` / `spd` | 停驻状态三连 |
| `destRaw` / `depotIdx` | 订单原始目的地 id / 当前库 id（不匹配时用来对账） |

---

## 四、候选成因（探针负责区分；本轮**不事先断言**）

| # | 形态（探针读数） | 含义 |
|---|---|---|
| A | `ontile=0` | 车**没进库格**（停在门口/邻格）⇒ 判据从第一项就假 |
| B | `eng=0` | 链头**不是引擎**（身份迁移/挂车后链头落在车厢上）⇒ 判据整块不进 |
| C | `isdepot=0` 或 `idmatch=0` | 订单目的地**不是当前库**（`GetCoupleIsDepot` 假，或 `destRaw != depotIdx`——另一个库/解析错） |
| D | 四项**全 1** 而仍无 `COUPLE-OK` | 判据成立、进了 `TrainCoupleHandler` 但**下游静默失败**（那是另一层，需再加探针） |

---

## 五、复测判据（玩家实跑后按此读）

1. **必出一行**：失败场景里应出现 `DEPOTBLK veh=65 …`（若一行都没有 = 车根本没进 `OT_GOTO_COUPLE` 的 handler 路径，方向完全不同）。
2. 按 `ontile / eng / isdepot / idmatch` 四值直接落到上表 A/B/C/D 之一。
3. **若落在 D**：同一 tick 附近应同时存在 `COUPLE-OK` 或 `COUPLE-FAIL`；两者皆无 ⇒ 下游是「进了函数但两条出口都没打」的第三种静默，需追加更细探针。
4. **负判据**：站台型挂接（`isdepot=0` 但订单是 `OT_GOTO_COUPLE`）**不得**被本探针影响 —— 探针只读，站台型仍照旧 `COUPLE-OK`。
5. **负判据**：健康库型挂接（判据四项全 1 且 `COUPLE-OK` 出现）不得出现任何行为变化。

---

## 六、未确认项（本轮明确不臆断）

- **U-1**：第 229 轮那条「无 `COUPLE-OK`」到底是**日志截断**还是**真卡**——需要**完整到出结果的新日志**（不受读档打断）。
- **U-2**：若为 D 型（下游静默），`TrainCoupleHandler` 内部在库型路径上的哪一条出口没打日志，尚未定位，需第二轮探针。
- **U-3**：`r3r_pending_depot_couple`（R3R 库工具登记路径）与本条「普通 `GOTO_COUPLE` 到库」路径是否共用出口——本轮探针会把 `pend` 一并打出，实跑即可分辨。

---

## 七、构建自证（第 230 轮）

- 复用既有 `_tmp_inc_build.cmd`（**未新建任何 `.cmd`**）；仅改 `src\train_cmd.cpp`，未碰任何 `src\*.h` / `src\lang\*.txt`。
- 结果与时间戳三件套见 `R3R_KNOWN_ISSUES.md`「第 230 轮 → 构建自证」小节（构建完成后回填）。

---

## 八、下一步

1. 玩家用本轮 exe 实跑同一场景（`veh=65` 停在 `964,133` 那个库），把**完整**到出结果的 `build\R3R_debug.log` 回传。
2. 按第五节判据落到 A/B/C/D。
3. 只有拿到读数后才动真正的修法；若仍不明确，按玩家口径再加探针 / 再问玩家。
