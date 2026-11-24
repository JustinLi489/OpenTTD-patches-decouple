# 第 502 轮：挂接寻路终点静态化（S2b / S2c / S4）

日期：2026-11-24
玩家指令原文：**「按 S2b → S2c → S4 顺序做吧」**
涉及文件：`src\pathfinder\yapf\yapf_destrail.hpp`、`src\pathfinder\yapf\yapf_rail.cpp`
未涉及：无新结构体字段、无存档格式、无函数签名变化、未碰 `build-release`

---

## 一、口径回顾（来自第 497 轮备忘 §十七 / 第 501 轮 KI-676）

1. **静态终点的粒度必须是"一排"**（站 + 轴 + 排号），不能是单格：
   `TrackFollower` 进入站台格时一步跳到该站台末端（`tiles_skipped = length - 1`），
   钉单格必然复现第 475 轮 `endhit = 0` 死锁。
2. **该排不可达 ⇒ `found = 0`（挂接失败）**，玩家已接受该代价；原则是"宁失败，不错铺到别的到发线"。
3. **depot 挂接一律不动**，继续走原扫描分支。
4. 第 499 轮把"通用路径改造"整批回退到 `aeb31dedda` 基线 ⇒ 第 470~499 轮的
   `r3r_dest_end_tile` 等快照字段**已不存在**，第 497 轮的 `Cpl-HEUR` 也一并消失；
   本轮 S2c 是**重新**把启发目标钉到车底格，来源是新的 `snap_tile`，不是复活旧字段。

---

## 二、S2b：把终点判定变成 O(1)

### 2.1 改动

`CYapfDestinationTrainRailT` 新增只读快照成员（全部在 `protected:` 区，
`public:` 区新增三个访问器供 `yapf_rail.cpp` 用）：

```
bool      snap_valid   = false;
VehicleID snap_veh     = VehicleID::Invalid();
TileIndex snap_tile    = INVALID_TILE;
StationID snap_stn     = StationID::Invalid();   // ← 见 §五 编译错误
Axis      snap_axis    = Axis::X;
uint16_t  snap_row     = 0;
bool      snap_car_only= false;
int       snap_rej     = -1;   // -1=接受；0..3 = GROUP/FIT/TARGET/ORDER
```

- `R3RTakeStaticTargetSnapshot(const Train *v)`：`SetDestination()` 末尾调用一次。
  depot 直接 `return`；从 `v->r3r_couple_target` 取车底；依次校验
  `IsPrimaryVehicle` / `OT_WAIT_COUPLE` / `IsRailStationTile` / `R3RCouplePairMatches()`
  ⇒ 填 `snap_stn/axis/row/car_only` ⇒ 再一次性评估四道闸门写入 `snap_rej`。
- `R3RStaticTargetValid()`：O(1) 复验（ID 有效、仍是 `OT_WAIT_COUPLE` 主车、
  `t->tile == snap_tile`）。
- `R3RStaticIsSameLine(TileIndex)`：非站台格放行；同站 + 同轴 + 同排号。
- `PfDetectDestination(TileIndex, Trackdir)`：在"仅接受站台/车库格"的前置检查之后插入静态分支，
  命中则打印 `PFD-STATIC kind=car|wait stn=<n> row=<n>` 后返回。
  **其后的原动态链一行未删**（`GetTrainForReservation` 反查主人 → 站台横向扫描 →
  `R3RSameParkingLine` → 四道闸门），作为快照失效时的回退。

### 2.2 为什么快照不查 reservation

反查"这一格的预留属于谁"会被**机车自己刚铺过去的预留**命中：
`TryReservePath()` 铺到 best 节点后，该格预留记在本车名下，下一 tick 解析端点时
`GetTrainForReservation()` 先返回自己 ⇒ 第 276 轮续（KI-432）的 `consist_head_tile == 起点`
退化。配对锁 `r3r_couple_target` 是**语义**上的答案，比"从地图上猜主人"更可信，
因此快照直接信锁 + `R3RCouplePairMatches()`。

---

## 三、S2c：启发与接受同源

`SetDestination()` 末尾：

```cpp
this->R3RTakeStaticTargetSnapshot(v);
if (this->snap_valid) this->dest_tile = this->snap_tile;
```

- 旧值 `v->dest_tile` 来自 `CalcClosestStationTile()`，是**近似落点**，可能落在同站别的排；
  S2b 已把接受集合收到"车底那一排"，启发却还指着近似点 ⇒ 两个口径不一致。
- 快照成立时两者统一到 `snap_tile`；失效时 `dest_tile` 逐字保持旧值，零回归。
- admissible 性论证：octile 距离对任一固定目标点满足三角不等式，估值为下界；
  实际代价按轨道格数累计（站台跳段也按格数计），不会低于 octile。

---

## 四、S4：把端点解析静态化

### 4.1 `yapf_destrail.hpp` 新增三个 public 出口

```cpp
VehicleID R3RStaticTargetVehicle() const;   // 快照车底链头车号
TileIndex R3RStaticTargetTile()    const;   // 快照格
TileIndex R3RStaticTargetFarTile() const;   // 车底链尾格（供"取近端"O(1) 比较）
```

### 4.2 `yapf_rail.cpp` 回溯阶段（`:1666` 起）

```
const TileIndex r3r_snap_tile = Yapf().R3RStaticTargetTile();
if (pNode != nullptr && r3r_snap_tile != INVALID_TILE) {
    // best_on_consist：只比较 tr->First()->index == snap_veh
    // best_on_consist ? best 格 : (近端 ? snap_far : snap_tile)
    // 日志 CPL-HEAD-STATIC veh= best= snap= farvalid= head= on=
} else if (pNode != nullptr && IsRailStationTile(pNode->GetLastTile())) {
    ... 原动态解析（保留作回退）...
}
```

- `tg` 的定义 `:1857` **未改**：
  `TileIndex tg = (consist_head_tile != INVALID_TILE) ? consist_head_tile : pPrev->GetLastTile();`
  静态命中后它自然取快照值 ⇒ 达成"tg 用快照终点替换 pPrev 推导"。
- `best_on_consist` 必须保留：YAPF 若停在车底自己占着的格，路径终点就是它，
  预留目标必须同格（KI-200 硬约束），不能用"近端"覆盖。

### 4.3 三条**刻意不删**的补丁及理由

| 位置 | 内容 | 为什么不删 |
| --- | --- | --- |
| `yapf_rail.cpp:780-790` | `last_safe` 扫描跳过站台格 | 服务对象是 `steps`（`rp()` 摸出的路径格序列）里的站台跳格，与 `tg` 来源无关；删了"最后一个安全位"会落到站台格，中段截断回归 |
| `:900-910` | 站台段不算等待位 + 终点例外 | `rp_impl` 仍会走到站台格与 `tg`，跳格判据 `R3RSkippedStationStretchContains()` 仍必须存在（第 93 轮 KI-163 的教训） |
| `:1796-1812` | 两遍法（先摸后订） | 治的是 `TryReservePath` 根节点零次循环（"空手报成功"）与 KI-504"边摸边订被自己挡住"，与终点动态无关 |

**结论**：真正"因终点动态而生"的只有回溯阶段那一套端点解析，本轮静态路径已完全不执行它；
动态路径原样保留作回退。**若玩家要求物理删除上表三条，需要先各自跑一次回归**，
不建议盲删。

### 4.4 两遍法的收益 / 代价评估（2026-11-24 玩家问"删除有什么好处"）

**先澄清它到底是什么**（当前代码 `:1800-1815` + `:2046-2055` + `:2067-2105`）：
- 第一遍 = `rp_impl()` **纯只读摸路**，每步把 `(tile, trackdir)` 压进 `r3r_resv_path`，
  失败分支由 `rp()` 用 `mark/resize` 回滚，保证序列里只剩最终成功那条；
- 第二遍 = `R3RReservePathSequence(r3r_resv_path, booked)` 按物理格序 `ReserveSingleTrack()`，
  内含"最后一个安全位"扫描（`:778-790`）、失败即停保留已订格（KI-563）、
  部分预留时把 `SetReservationTarget` 收回到最后一格已订（`:2087-2090`）。
- **首次实现里第一遍就顺手订轨，两遍法是为了把订轨整体挪到摸完之后才拆出来的**。

**删掉（回到单遍边摸边订）的真实好处，只有三条，都很小**：
1. 省一个 `std::vector<R3RCoupleStep>` 与一次线性遍历：路径 10~30 格、每次挂接寻路一次，
   是微秒级；相比 A* 本身（数千节点）可忽略，**没有性能收益**。
2. 少一层间接：`rp` 的 `mark/resize` 包装、`R3RCoupleStep` 类型可删，
   `R3RReservePathSequence` 可内联回 `rp_impl`，代码约短 120 行（含注释与探针）。
3. 阅读时"订轨发生在哪"单一化（现在要同时看 `rp_impl` 记账与 `R3RReservePathSequence` 真订）。

**代价，前三条都是"已修 bug 复活"**：
1. **KI-504 复活**：边摸边订会被自己刚订的轨挡住；原生自订豁免（`pbs.cpp` KI-361）只覆盖部分形态，
   站台整段预订 `ReserveRailStationPlatform()` 一次置整段平台位，后续摸路经过平台时必然撞到自己。
2. **"空手报成功"复活**：两遍法存在的直接原因就是 `TryReservePath` 按节点链订 + 单段分支把
   reservation target 挂到根节点 ⇒ 循环零次 ⇒ `reserved=1` 而一格没订
   （现场锚点 `build\R3R_debug.log:1631`，`nTry=0 empty=1`）。
3. **KI-563 的粒度没了**：`booked` 是目前唯一知道"实际订到第几格"的地方；删掉后只能整批成功/失败
   ⇒ 回到第 392 轮"CPL-RES-FAIL 533 次、列车一动不动"的形态。
4. **与 S4 的配合被破**：S4 让 `tg` 精确到车底端点，第二遍序列正好止于 `tg`；
   回到节点链订则节点 segment 是跨站台合并的，**会把车底整段一起订进去**。

**结论**：两遍法不属于"因终点动态而生的补丁"，S4 的静态化不构成删它的理由；
**不建议删**。若目标是降复杂度，安全的折中是"保留先摸后订的顺序，只把实现合并成一函数"
（视觉上一遍、语义上仍是两遍），可省 §4.4 好处 2，不承担四条代价。

---

## 五、构建与错误

### 5.1 首次编译失败（已修）

```
yapf_destrail.hpp(381): error C3861: "INVALID_STATION": 找不到标识符
yapf_destrail.hpp(408): error C3861: "INVALID_STATION": 找不到标识符
```

原因：该 TU 未引入 `INVALID_STATION` 宏，且本项目 `StationID` 是强类型。
修法：成员默认值与该分支的赋值都改成 `StationID::Invalid()`。
（同类前车之鉴：第 501 轮的 `AXIS_X` → `Axis::X`。）

### 5.2 最终构建自证（内测树，探针 ON）

- 入口：`Remove-Item` 删除 `build\CMakeFiles\openttd_lib.dir\src\pathfinder\yapf\yapf_rail.cpp.obj`
  后直接 ninja（`yapf_destrail.hpp` 全库只被 `yapf_rail.cpp:16` 包含 ⇒ 1 TU + 链接；
  **未新建任何 `.cmd`**、未跑全量护栏）
- 命令：`cmd /c 'call "D:\VISUAL STUDIO\MAIN PACK\VC\Auxiliary\Build\vcvars64.bat" >nul && "D:\gcc new\mingw64\bin\ninja.exe" -C build openttd.exe > build\R3R_inc502.log 2>&1'`
- 结果：`[2/3] Building CXX object ...yapf_rail.cpp.obj` → `[3/3] Linking CXX executable openttd.exe`
- `error C|fatal error|FAILED:|build stopped` 计数 **0**
- `build\openttd.exe` **2026-11-24 16:09:46**
- `read_lints` 两文件 **0 条**
- 探针入库自证：`findstr /m /c:CPL-HEAD-STATIC openttd.exe` 与 `/c:PFD-STATIC openttd.exe` 均命中
- 发布树 `build-release`：**未编**（停留在不含 S2b/S2c/S4 的版本）

---

## 六、待复测判据（游戏内）

1. 正常同排挂接出现 `CPL-HEAD-STATIC ... on=0|1`，紧接着 `COUPLE-OK`。
2. `CPL-SCAN` 的展开数（`closed + enter`）较第 501 轮明显下降 —— S2c 的直接收益。
3. 车底在 `y=1874` 时仍不接受 `1424,1875 / 1424,1876`；`PFD-STATIC` 或 `PFD-REJ-LINE`
   二选一出现，`CPL-SCAN` 的 `rej` 第 5 位（LINE）非 0。
4. 快照失效场景（无配对锁 / 车底被移走 / depot 挂接）走回退：日志仍打旧 `CPL-HEAD`，
   行为逐字不变。
5. 负判据：`best` 落在车底自己占着的格 ⇒ `on=1` 且 `head == best`（KI-200 不回归）。
6. 负判据：机车预留不得铺到车底另一侧；`CPL-TRACE / RP` 序列长度不增加。

---

## 七、未确认项

- **U-1**：S2c 后 estimate 的 admissible 性只做了静态论证，异排场景的展开数未实测。
- **U-2**：快照只在 `SetDestination()` 取一次；同一次搜索内车底若被挂走，靠
  `R3RStaticTargetValid()` 的 `t->tile == snap_tile` 失效回退，但**不做二次快照**
  （预期：本次搜索作废，下一 tick 重来）。
- **U-3**：`R3RStaticTargetFarTile()` 对"车底正在掉头 / 跨多格"的极端形态未实测。
- **U-4**：`res_target_tile` / `SetReservationTarget()` 未改成直接读快照 —— 它们已由
  `consist_head_tile` 驱动，语义等价，故未动。
- **U-5**：S4 未删除 §4.3 三条补丁（理由见表），若玩家认为必须物理删除，需逐条回归。
