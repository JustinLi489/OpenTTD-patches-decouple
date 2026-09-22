# R3R 备忘：机车挂上车底后在同一格立刻被解挂（第 94 轮）

## 1. 现象

机车 7 在 0 号车站的等待点 `60,51` 挂上一列车底，**挂上就掉**：`COUPLE-OK` 与 `DECOUPLE-FIRE`
落在同一地块、同一真实订单索引，中间只隔一个 tick；之后机车独自开走，车底留在原站，
再挂依旧同样结局 —— R3R 的"挂车 → 拉走 → 到站解挂"全流程走不通。

## 2. 日志证据（`build\R3R_debug.log`，2026-09-22）

```
1186: COUPLE-OK loco=7 ... real=4 type=1 tx=60 ty=51
1194: ORD-AFTER-COUPLE head=7 n=12 real=4
1195:   idx=0  type=17 (WAIT_COUPLE)
1196:   idx=1  type=1  dest=0     (GOTO_STATION -> 0 号车站)
1197:   idx=2  type=15 (DECOUPLE)
1198:   idx=3  type=17 (WAIT_COUPLE，本次被挂的等待点)
1199:   idx=4  type=1  dest=2     (当前真实订单：GOTO_STATION -> 2 号车站)
1200:   idx=5  type=15 (DECOUPLE)
1211: DEPOT-ARR veh=7 spd=0 real=4(1) curType=0 tileDepot=0 tx=60 ty=51
1219: DECOUPLE-FIRE consist=7 tx=60 ty=51 real=4
1241: LOCO-AFTER-DECOUPLE veh=7 curType=0 real=1 tile=60,51
```

类型映射（`order_type.h:81-95`）：`1=OT_GOTO_STATION`、`2=OT_GOTO_DEPOT`、`15=OT_DECOUPLE`、
`16=OT_GOTO_COUPLE`、`17=OT_WAIT_COUPLE`。

关键：耦合点 `60,51` 是 **0 号车站**的地块（车底停在 `idx3` 等待点）；挂车后真实索引跳到
`idx4` = 去 **2 号车站**；1186 与 1219 行是同一地块、同一 `real=4`。

## 3. 根因

`src/train_cmd.cpp` 解挂钩子前的到达判定（~11370）对 `OT_GOTO_STATION` 只判"是否停在**任意**
车站地块"：

```cpp
at_order_dest = IsTileType(consist->tile, TileType::Station);   // 旧代码
```

于是"去 2 号车站"的订单，只要停在**任何**车站（这里是 0 号车站等待点）就算"已到达"，
紧接着查下一条真实订单 `idx5 = OT_DECOUPLE` → 立刻解挂。

`OT_GOTO_DEPOT` 分支早在 2026-09-04 就因同型 bug（在 42,28 旧库里被判为"已到达 38,27"）
改成按 depot 索引比较，车站分支当时漏改。

## 4. 修复（仅 `src/train_cmd.cpp`，未碰 `src/*.h`，符合 KI-15 增量合规）

```cpp
at_order_dest = IsTileType(consist->tile, TileType::Station) &&
    cur_real->GetDestination().ToStationID() == GetStationIndex(consist->tile);
```

同一车站的多个站台格返回同一 `StationID`，正常到站不受影响；目标站被删除
（`INVALID_STATION`）时不再算作到达。`ToStationID()` 见 `order_type.h:46`。

## 5. 本轮附带清理的探针

| 文件 | 删除 |
| --- | --- |
| `src/sl/vehicle_sl.cpp` | `VEHS_ENTER` / `VEHS_DONE` / `r3r_veh_count` / `R3R_vehsraw.bin` 字节转储 / `LOADCENSUS-*` |
| `src/sl/saveload.cpp` | `INVALID_REF_VEHICLE` / `CHUNKLOAD` / `AFTER_LOADCHUNKS` / `BEFORE_PTRS` |
| `src/pbs.cpp` | `UNRESERVE-STATION` |
| `yapf_rail.cpp` | `DEPOTCHK`（硬编码 38,27）/ `CPL-FOLLOW` / `CHKRES`（硬编码预留转储） |
| `yapf_destrail.hpp` | `PFD-SCAN` / `hasResButNoTrain` |

保留：`REACH`/`RP`/`CPL-*`/`FSCP-*`/`FOLDCHK`/`RT`（KI-163 复测）、`SL-STNN-PURGE`（KI-95）、
`SLERROR`、`R3RDUMP`（仅读档一次，且受 `R3RFopenDbg` 的 `R3RDbgOn()` 闸门）。

## 6. 构建

`_tmp_inc_build.cmd` → `build\R3R_incbuild.done = EXIT_CODE=0`，日志末行
`[7/7] Linking CXX executable openttd.exe`；`build\openttd.exe` @ 2026-09-22 20:45:49（50 791 936 B）。
本轮改动的 6 个编译单元（`train_cmd` / `pbs` / `saveload` / `vehicle_sl` / `yapf_rail` /
受 `yapf_destrail.hpp` 影响的 `yapf_rail`）源文件 mtime 均晚于对应 obj，增量重编覆盖完整；
`yapf_destrail.hpp` 的唯一包含者是 `yapf_rail.cpp`（`grep` 全仓确认），故无需全量重编。

## 7. 状态与复测判据

状态＝**已修（编译通过；游戏内复测待做）**，严重度 高。

复测：同场景（0 号车站 `60,51` 等待点被机车 7 挂上）应看到 `COUPLE-OK ... real=4` 之后**不再**
出现同地块 `DECOUPLE-FIRE`；列车应驶向 2 号车站，抵达**该站**后才在 `idx5` 触发解挂。
若仍在 0 号车站触发，说明该站 `StationID` 与订单 dest 意外相等（等待点与目的地本就是同一站），
需打印两者编号再定。
