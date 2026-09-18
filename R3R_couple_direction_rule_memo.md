# R3R 挂车「端面方向判据」备忘（2026-09-19，第 64 轮）

> 一句话：把 `Couple()` 里的「无条件整链方向统一循环」（KI-114）换成**只看拼缝端面两节**的环形差判据；
> 直角（90°）判为事故按**撞毁**处理；反向只对拼入段**逐节反转 + `Flipped` 补偿**；同向**一个字节都不改**。
> 目标是彻底摆脱「整条链必须同向」这个前提。

---

## 1. 决策来源

用户 2026-09-19 就三问逐条拍板：

| 问 | 用户裁决 |
|---|---|
| ① `circ == 2`（直角）怎么办 | **不拒绝耦合，直接判撞毁**。「现实里也没有直角挂车的，要是直角插过去挂车应该是属于事故」——用户明确接受这一改动会**移动「耦合 / 撞毁」的边界** |
| ② 判据基准节 | **确认用端面两节**（`merged_first->Previous()` 与 `merged_first`），不是现在的链头 `v->direction` |
| ③ 路线 | **要的就是「不依赖列车整列同向」**（不再用 `w->direction = v->direction` 整链覆写） |

---

## 2. 判据（正式形式）

`direction` 是 `0..7` 的**环**（`Direction`，`ReverseDir(d) == d+4 mod 8`）。
拼缝两节：`a = merged_first->Previous()`（主动方 / 机车侧链尾），`b = merged_first`（拼入段端面）。

```
circ = min(|a->direction - b->direction|, 8 - |a->direction - b->direction|)   // ∈ [0,4]
```

| `circ` | 含义 | 处理 |
|---|---|---|
| `0` / `1` | 同向（含相邻轨道 45° 差） | **不动**（链已自洽，dont-touch） |
| `2` | 直角错位（90°） | **判为事故 → 撞毁**（`TrainCrashed` + `STR_NEWS_TRAIN_CRASH`） |
| `3` / `4` | 反向（nose-to-nose / tail-to-tail） | **对拼入段逐节反转**：`R3RReverseChainDirections(merged_first)` |

注意 `circ == 4` 才是「完全对脸」的主 case（`>= 3` 里最大的部分），不是只有 `3`。

---

## 3. 为什么 `circ ∈ {3,4}` 用「逐节反转」而不是「覆写成链头朝向」

1. **恒合轨**：轨道对 180° 旋转对称 —— 任一节「方向 d 合法」⇒「`ReverseDir(d)` 在同一块 track 上合法」。
   而旧的 `w->direction = v->direction` 在弯道上会把每节车写成一个**与它脚下轨道无关**的值，
   直接制造非法 `(track, direction)`（`TRACK_Y` 只允许 SE/NW，却被写成 N）——这就是 KI-114/KI-108 的状态来源。
2. **拼缝自洽**：对拼入段整体 `ReverseDir` 后，拼缝差恒满足 `circ(3)→1`、`circ(4)→0`，仍是合法链。
   旧写法给 `0`，看似更「整齐」，但在弯道瓦片上恰恰造出非法组合。
3. **净图像不变**：`Flipped` 是**渲染补偿寄存器**，净朝向 = `Flipped ? ReverseDir(direction) : direction`
   （`train_cmd.cpp` `Train::GetImage`）。改 `direction` 的同时 `Flip(Flipped)` ⇒ **一个像素都不动**，
   拼入段保持它等待时的姿态被拖走。这正符合 R3R「不碰位置 / direction 语义之外的图像」的禁忌。

---

## 4. 为什么可以放弃「整列同向」这个前提（旧注释的两条论据均不成立）

旧注释（原 `Couple()` 内，2026-09-07 裁决）声称必须统一，理由是：
「JGR 的 flip 逻辑从**链尾** direction 反推朝向」+「整列同向、`moving-*` 不撕裂」。

逐条核实（本轮代码树）：

1. **「从链尾反推朝向」查无此码**。`ReverseTrainSwapVeh` 实际是**成对 swap**（`std::swap(a->track,b->track)`、
   `std::swap(a->direction,b->direction)` 等），**不推任何东西**；全库 `Last()->direction` 只出现在
   `roadveh_cmd.cpp`。故该论据在本版本不成立（历史经验注释）。
2. **`moving-*` 只读 `DrivingBackwards`**：`IsMovingFront` / `GetMovingFront` / `GetMovingNext` /
   `GetMovingPrev` / `GetMovingBack`（`vehicle_base.h`）全部只测 `VehicleFlag::DrivingBackwards`，
   **与逐节 `direction` 无关** ⇒ 「不统一就撕裂 `moving-*`」不成立。

⇒ 结论：**逐节 direction 允许不同向**，统一循环不是运行语义的必需品，而是 KI-114 的病灶。

---

## 5. 落点

`src/train_cmd.cpp` → `static void Couple(Train *v, Train *u)`，替换原 `6026-6051` 的整段注释 + `for` 循环。

- 新增前置声明 `static uint TrainCrashed(Train *v);`（该函数定义在文件后段 `~8695`，`Couple` 在 `~5979`）。
- 撞毁分支：`TrainCrashed(v)`（`v` 已是合并链头 ⇒ 满足 `Vehicle::Crash` 的「必须是链头」不变式），
  本机公司再补 `AddTileNewsItem(GetEncodedString(STR_NEWS_TRAIN_CRASH, num_victims), NewsType::Accident, v->tile)`，
  **然后 `return`** —— 不再走后续排程交接（车已毁，`orders` 交接无意义）。
- 反转分支：`R3RReverseChainDirections(merged_first)`（复用既有原语，逐节 `ReverseDir` + `Flip(Flipped)`）。
- 保留对 `merged_first..链尾` 的 `UpdateViewport(true, false)`（刷新被改动的节）。
- 探针：`COUPLE-SEAM a=<idx> b=<idx> circ=<n> act=<none|flip|crash>`。

---

## 6. 与既有条目的关系

- **KI-114（未修 → 已修）**：本改动是 KI-114 提出的「修法」的**加强版** —— 不是给旧循环加「轨道能否承载」的门
  （那只是让部分车 `continue`，仍是覆写逻辑），而是**整体废除覆写**，改为按拼缝最小修正。
  KI-114 证据里的 A3 双翻现场（`merged_head=8`，idx3..7 `dir=7`，idx8 `dir=0`）在新判据下
  `circ(prev=idx7 dir7, first=idx8 dir0) = 1` ⇒ **走「不动」分支**，逐节朝向原样保留，正是 KI-114 想要的结果。
- **KI-108 / KI-107**：其「更深根因」= 链车 `(track, direction)` 不一致；本改动从**写入端**消除，
  属于根治方向。KI-108 的兜底（`GetVehicleTrackdir()` 返回 `TrackToTrackdir`）**保留**，作为其它路径的保险。
- **KI-105**：提醒「direction 反转不是纯账目」——`R3RReverseChainDirections` 必须继续改 direction，
  本改动不改这一点（反转分支仍逐节改 direction，只做 `Flipped` 图像补偿）。

---

## 7. 影响面 / 风险

| 项 | 说明 |
|---|---|
| 撞毁边界移动 | `circ == 2` 从「照旧强制拼接（并写坏 direction）」变成「事故撞毁」。这是**用户明确接受**的语义改动 |
| `Flipped` 补偿不完整的角落 | `RailFlips` 引擎的 `UpdateDeltaXY` 不看 `Flipped` ⇒ direction 变了碰撞盒 / 裁剪框可能跟着变。「图像不变 ≠ 一切不变」，实测时留意 |
| 换端依赖 | 若后续发现 `CheckReverseTrain` / `ReverseTrainDirection` 真的依赖「整列同向」，需回到本判据补强（当前证据表明不依赖） |
| 撞毁时机 | 撞毁发生在**物理拼接之后**（`ArrangeTrains` 已完成），故是「已经连上的一列车被判事故」，不是「拒绝拼接」。折叠检查若已否决并回滚，则不会走到这里（直角错位的点积 ≈ 0，通常**不**触发折叠否决，故能走到） |

---

## 8. 待实测

1. 直角场景：机车以 90° 顶上车底 ⇒ 应出现 `COUPLE-SEAM ... circ=2 act=crash`，链被判撞毁、出事故新闻，**不再刷屏死循环**。
2. 反向场景（nose-to-nose）：应出现 `circ=4 act=flip`，拼入段逐节反转、**渲染姿态不发生原地翻面**，且链上各节 `(track, direction)` 自洽（不再出现 `VEHTD-MISMATCH`）。
3. 同向场景（`circ<=1`）：`act=none`，与旧行为逐位等价（旧循环此时本就是 no-op）。
4. 回归对照 KI-106 / KI-107 / KI-108 基线：站台耦合 `CPL-GEO` 4 次 / `CPL-HIT` 0 次 / `gap` 全 0 / `CRT found=1`。
