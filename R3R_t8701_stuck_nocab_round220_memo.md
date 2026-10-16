# 第 220 轮（2026-10-06）临时分析报告：T8701/2 复跑两问题——①机车卡住 ②倒车未触发 DB 惩罚

> 本文件是**在写**的报告：按玩家长期规则"拿到报告第一时间先写备忘"，本轮先把需求、现场锚点、已知判据落纸，再继续深挖并回填（每节末尾的「待深挖」就是回填位）。

## 零、玩家原话（需求）

> 「关于你追加的问题，我已经无从得知了，因为目前来看非列车车库没有我预料中的操作能造成崩溃了（⇒ 第 219 轮 KI-329 修复已被现场确认有效，U-1 作废）
> 但是，我又跑了一遍 T8701/2 的场景，用同一个场景，和**全部选不停车（直达）**命令和**全部选非直达**命令，发现了两个问题，第一个是**机车卡住**，第二个是**倒车又没触发 DB 惩罚**。」

⇒ 本轮要解两件事，各有一份现场日志：

| 组 | 日志 | 命令口径 |
|---|---|---|
| A | `build/R3R_debug_regular.log` | 全部非直达（regular） |
| B | `build/R3R_debug_non_stop.log` | 全部不停车/直达（non_stop） |

「DB 惩罚」= 引擎里那条**倒车驾驶且移动前端无驾驶室时的 32 km/h 限速**（`TCF_NO_DRIVING_CAB`），不是信号惩罚（KI-179 是另一条，勿混）。

## 一、现场规模与探针对照

| 探针 | regular | non_stop | 说明 |
|---|---|---|---|
| 行数 / 字节 | 12223 行 | 10891 行 | 两份日志**现象结构相同**，仅行号偏移 |
| `REVERSEDIR` | 21 | 21 | 逐行 `rev=0 stuck=0 db=0` |
| `DB-NORMALISE` | 6 | 6 | 5 次 `headDB=1 set>0 nocab=0`；1 次 `headDB=0 cleared=6` |
| `DB-CLEAR` | 5 | 5 | 4× `staleNocab=0 last=26 lastLead=1`（regular 1482/2566/4904/6008）＋ 1× `staleNocab=1 last=26 lastLead=0`（regular 11988） |
| `NOCAB-SET` | **1**（11985） | **1**（10857） | `this=48 db=1 last=26 lastSub=0x04 lastEng=0 lastLead=0` |
| `NOCAB-LIMIT` | **0** | **0** | 限速实际上从未生效 |
| `COUPLE-FAIL` | 366 | — | 集中于 `loco=57` |
| `COUPLE-DEST-EMPTY` | 53 | — | **实为 `loco=54`（tile=1,15），并非 57** —— 修正见 §2.5 |

## 二、问题②：倒车未触发 DB 惩罚——判据链与已有锚点（**已定位到判据，根因待收口**）

### 2.1 惩罚的判据源码（三处）

`src/train_cmd.cpp`：

```cpp
313:	const bool driving_backwards = this->vehicle_flags.Test(VehicleFlag::DrivingBackwards);
324:		u->vehicle_flags.Set(VehicleFlag::DrivingBackwards, driving_backwards);   // 头部一位 → 全链
...
460:	if (driving_backwards && !this->Last()->CanLeadTrain()) {
461-471:  /* NOCAB-SET 探针 */
472:		this->tcache.cached_tflags |= TCF_NO_DRIVING_CAB;
473:	}
```
```cpp
1152:	if (this->tcache.cached_tflags & TCF_NO_DRIVING_CAB) {
1156-1172: /* NOCAB-LIMIT 探针（每 db 状态只打一次） */
1173:		constexpr int BACKWARDS_NO_CAB_SPEED_LIMIT = 32;
1174:		max_speed = std::min<int>(max_speed, BACKWARDS_NO_CAB_SPEED_LIMIT);
1175:	}
```

⇒ 惩罚成立的**充要条件**：`ConsistChanged()` 跑的时刻，链头 `vehicle_flags.DrivingBackwards == true` **且** `Last()->CanLeadTrain() == false`。

### 2.2 现场读数（两份日志完全一致的结构）

- `NOCAB-SET` **只出现 1 次**，且落在日志**最末**（regular 11985 / non_stop 10857）。这是全日志唯一一次惩罚被正确设置：`this=48 db=1 last=26 lastSub=0x04 lastEng=0 lastLead=0` —— 头部已 db=1、尾车 26 确实不能领头 ⇒ 判据成立。
- `NOCAB-LIMIT = 0` ⇒ 那条 32 km/h 限速**从头到尾一次都没落地**（连一次 `GetCurrentMaxSpeedInfoInternal` 带标志都没发生）。
- `DB-NORMALISE` 6 次里有 **5 次**是 `headDB=1 set=27/21 nocab=0`（1406/2491/4836/5940/11848）。**注意 `DB-NORMALISE` 不是"一律清零 DB"**：`R3RNormaliseMixedChainDrivingBackwards()`（`train_cmd.cpp:7160-7212`）只在**链上 DB 混合**时才动手，且**统一到链头的值**（`head_db`）：`head_db=1` 时对不一致的车 `Set`（`set++`），`head_db=0` 时 `Reset`（`cleared++`）；随后 `head->ConsistChanged(CCF_TRACK)`，日志里的 `nocab` 正是这次 `ConsistChanged` 之后的 `TCF_NO_DRIVING_CAB`。⇒ `headDB=1 set=27 nocab=0` = **链确实处于 db=1（倒车）状态跑完了 `ConsistChanged`，惩罚仍未置位** ⇒ 按 2.1 判据只能是 `Last()->CanLeadTrain() == true`。这是**判决性**读数（不是"DB 被清零所以没惩罚"）。
- `DB-CLEAR` 5 次：4 次 `staleNocab=0 last=26 lastLead=1`（尾车 26 **可以**领头）；第 5 次（11988）`staleNocab=1 last=26 lastLead=0`。
- 唯一 1 次 `headDB=0 cleared=6 set=0 n=9`（11081）是正常形态（把混合归到 DB=0），不计入本问题。

### 2.3 根因（已收口）

**R3R 给"段"的尾车授予的假引擎身份，使 `Last()->CanLeadTrain()` 对一个无驾驶室的车厢返回 `true`，于是 `ConsistChanged()` 永不置 `TCF_NO_DRIVING_CAB`。**

判据与证据链（三环）：

1. **`CanLeadTrain()` 的后门**（`train_cmd.cpp:18796-18807`）：
   ```cpp
   if (this->GetEngine()->info.extra_flags.Test(ExtraEngineFlag::HasCab)) return true;   // 车厢 engine_type 没有 HasCab ⇒ 否
   if (this->IsArticulatedPart()) return this->GetFirstEnginePart()->IsEngine();        // 不是铰接件 ⇒ 否
   return this->IsEngine() || this->IsRearDualheaded();                                  // ← 假引擎在这里被判为"能领头"
   ```
2. **假引擎身份的授予点**（`vehicle_cmd.cpp:638-660` `SetSegmentTailFakeEngine()`，调用点 `:747`，位于 `CmdMakeSegment` 的 Execute 分支，紧跟 `t->SetSegmentFront()`）：`last->SetEngine(); last->ClearWagon(); last->ClearFreeWagon();`——**把 engine 子类型位（GVSF_ENGINE）嫁接到段尾普通车厢**。注释（`:632-635`）自己写明动机："making both ends engine-capable lets the reversed end take over as the leading vehicle of the chain afterwards"。⇒ 目的只是**簿记**（让翻转后的另一端点能当链头），但副作用是让 `IsEngine()` 变真。
3. **惩罚判据**（`train_cmd.cpp:460`）：`if (driving_backwards && !this->Last()->CanLeadTrain())` → 被 1 骗过。

**日志互证（regular）**：
- 5× `DB-NORMALISE headDB=1 set=27/21 nocab=0` = 倒车状态下 `ConsistChanged` 跑过、惩罚缺席。
- 4× `DB-CLEAR ... last=26 lastLead=1` = 尾车 26（普通客车）报告"能领头"，只可能是假引擎。
- 反证：**11985** `NOCAB-SET this=48 db=1 last=26 lastSub=0x04 lastEng=0 lastLead=0` —— 尾车 26 的假引擎身份**被撤销后**（`lastSub` 只剩 `GVSF_WAGON=0x04`、`lastEng=0`），惩罚**当场**就被置上了。⇒ 假引擎就是那个"惩罚杀手"，因果关系闭合。

**结论**：玩家报的"倒车又没触发 DB 惩罚"**成立，是 bug**。口径上，引擎惩罚的本意是"移动前端没有**驾驶室**就限速 32"，而段尾假引擎是 `RailVehicleType::Wagon` 的车厢（无 cab），物理上没有驾驶室 ⇒ 应当受限速。

### 2.4 待深挖项——已全部收口

| 编号 | 结论 |
|---|---|
| T-1 | **已答**：`CanLeadTrain()`（`train_cmd.cpp:18796`）最后一行 `IsEngine()` 被 R3R 的段尾假引擎（`vehicle_cmd.cpp:657 SetEngine()`）骗为 true。见 §2.3。 |
| T-2 | **已答**：`DB-CLEAR` 是**耦合提交点**的"撤销残留倒车态"块（`train_cmd.cpp:11147-11176`，注释 `:11129-11141` 说明动机：倒车鼻贴挂车会把 DB 带进合并链，导致从车厢端发车/滑错方向）。条件是 `v->vehicle_flags.Test(DrivingBackwards) && !IsRailDepotTile(v->tile)`；动作 = 逐节清 DB + 反转 GVF_GOINGUP/DOWN + `UpdateStatusAfterSwap` + `v->ConsistChanged(CCF_TRACK)`。⇒ 它是**刻意设计**，但它会把同一提交点里刚置上的惩罚一并重算掉（11985 → 11988）。 |
| T-3 | **已答**：`REVERSEDIR` 探针在 `train_cmd.cpp:4092-4109`（`rev=`=`VehicleRailFlag::Reversing`，`stuck=`=`VehicleRailFlag::Stuck`，`db=`=`DrivingBackwards`）。regular 21 次全部 `rev=0 stuck=0`；其中 **13 次 `db=0 → REVERSEDONE db=1`、8 次 `db=1 → REVERSEDONE db=0`**（1728/2731/4351/6135/7756/10480/10791/11433）⇒ 每次都走"逐车翻转 DB（倒车）"分支，**没有一次**走"物理换端"分支（该分流同样以 `Last()->CanLeadTrain()` 为准 ⇒ 与 T-1 同源）。 |
| T-4 | **已答**：11985 `NOCAB-SET`（那一刻尾车已变回普通车厢 `0x04`）→ **11988 `DB-CLEAR staleNocab=1`**（同一提交点把 DB 清掉并 `ConsistChanged(CCF_TRACK)` 重算 ⇒ 惩罚被抹）→ 之后链继续 settle / 解挂，末段（`LOCO-AFTER-DECOUPLE veh=48 tile=100,64` 之后）veh=48 每 tick `ok=0 res=0` 卡死，限速无从体现。⇒ "惩罚有效时间 = 0"在时序上完全成立。 |

### 2.5 修正记录

- §一/§3.2 原写 `COUPLE-DEST-EMPTY` 归属 `loco=57` 是**笔误**：实测 53 行全部为 **`loco=54`（tile=1,15）**。⇒ KI-193 的 500-tick 兜底对 veh=57 **一次都没触发**（57 的 `COUPLE-FAIL` 366 次纯刷屏，没有任何兜底介入）。

## 三、问题①：机车卡住——两个滞留点（**已定位现象，根因待收口**）

日志尾部同时存在**两列**互相独立的滞留机车：

### 3.1 veh=48：想去路点 8806，永远拿不到路径

```
SVC-DEPOT-SKIP veh=0 cur=17 real=25(17) spd=0 tile=100,64 tag=couple-protocol
TRP veh=48 mf=100,64 real=11 type=6 dest=8806 spd=0 fp=0 look=1 one=1 mstuck=0 fto=0 => ok=0 res=0
```
（`TRP` 探针字段见 `train_cmd.cpp:13633`；`type=6` = `OT_GOTO_WAYPOINT`，见 `order_type.h:83`；`one=1` = `HasForceReserveOnce()`；`look=1` = `lookahead != nullptr`。）

- 前因：`DECOUPLE-JUMP fire_real=24 dec_idx=24 wait_idx=65535 step=-1 v=48 u=0 n_u=33` → `DECOUPLE-ADV dec_idx=24 v_real=11 u_real=25` → `DECOUPLE-DONE u=0 co=1 real=25 tx=100 ty=64 x=1608 y=1026`。
- 解出方 `u=0`（27 节，`GEO settle-decouple-u head=0 n=27`、`SEGTR-SNAP tag=decouple-u head=0 nseg=2 ctrl=6 borrowed=1`）停在 `100,57..100,64` 且 `cur=17`（`OT_WAIT_COUPLE`），并整条占位：`RESERVECONSIST veh=0..26 tile=100,6x track=0x2 fb=0 bits=0x2 ok=0 resNow=1`（27 行全 `ok=0`）。
- 机车 `v=48` 侧 `real=11`，下一命令是路点 8806，但每 tick `ok=0 res=0`。

### 3.1 根因（S-1/S-2 已收口）

判定：veh=48 走的是 `R3RTryPathReserveCore()` 的**"被别的列车挡住"分支**（`train_cmd.cpp:13885-13895`）：

```cpp
Vehicle *other_train = nullptr;
PBSTileInfo origin = FollowTrainReservation(consist, &other_train);
if (other_train != nullptr && other_train->index != consist->index) {
    if (mark_as_stuck) MarkTrainAsStuck(consist);
    return TPRRF_NONE;          // ⇒ TRP ... => ok=0 res=0
}
```

- **排除法**：`MarkTrainAsStuck` 在这段函数里的直接调用点只有两处——`:13820`（仅当 `moving_front->track == TRACK_BIT_DEPOT`）与 `:13893`（被别车挡住）；另有 `ChooseTrainTrack(..., CTTF_MARK_STUCK)` 间接置位。veh=48 站在普通轨道、不在车库格 ⇒ 排除 `:13820`；`TRP-AUTO veh=48 ... stuck=1` 显示"以 `mark_as_stuck=true` 调用完列车真被标成 stuck"。剩下 `:13893` 与 `ChooseTrainTrack` 两条（U-1 未 100% 排除后者）。
- **机制（本条的根）**：解出方 `u=0`（27 节、`cur=17` WAIT_COUPLE）**停在机车同一格里**——`LOCO-AFTER-DECOUPLE veh=48 ... tile=100,64`，而 u 占 `100,57…100,64`（两者在 100,64 内像素相邻）。u 满足 `R3RIsReservationHolder()`（car-only formation ＋ 站台上 WAIT_COUPLE），于是每 tick `ReserveTrackUnderConsist()` 自持脚下整格：`RESERVECONSIST veh=0 tile=100,64 track=0x2 ... ok=0 resNow=1`。机车从**自己的格**取预留时，`FollowTrainReservation()` 回溯到的所有者是 veh 0 而不是 48 ⇒ `other_train = veh0 ≠ 48` ⇒ 永久 `TPRRF_NONE`。**机车被自己刚丢下的车底堵死在原地。**
- **S-2（已答，非原因）**：`one=1` 不该也不会被"吞掉"。`ForceReserveOnce` 只在**第一次成功预留**时被消费（成功出口 `:13906` 置 `r3r_one_shot.reserved=true`，`:13744` 调 `ClearForceReserveOnce()`），veh=48 从未成功 ⇒ 一直保留 `one=1`，与"每 tick 失败"完全自洽。`look=1` 是合并链时期残留的 lookahead，**不代表有可用路径**（对照 veh=57 的 `look=0`）。
- **U-1（未确认）**：`ChooseTrainTrack` 侧"路点 8806 不可达"这一条尚未排除。取数办法＝在 `:13892` 之前加一行只读 dump（`origin.okay / origin.tile / other_train`），预期 `other_train=veh0`、`origin.tile=100,64`。
- **同族前例**：本条与 KI-201 附记 4/6 的"拼接面 8px 残缝"是同一条根——**R3R 的列车刚性 + 位置不重排**，解挂后两半仍重叠同格。

### 3.2 veh=57 / veh=27（S-3 部分，S-4 已答）

```
TRP veh=57 mf=21,13 real=4 type=16 dest=923 spd=0 fp=0 look=0 one=0 mstuck=0 fto=0 => ok=0 res=0
CPL-S0-CHK veh=57 tile=21,13
COUPLE-FAIL loco=57 order=16 tx=21 ty=13 x=346 y=216
SKIP-STOPPED veh=27 order=0 real=17 spd=0 tile=1,15 parked=0 front=1 nord=33 idx=0 tt=0
```
（`type=16` = `OT_GOTO_COUPLE`，`type=17` = `OT_WAIT_COUPLE`，`type=2` = `OT_GOTO_DEPOT`。）

- **S-4（已答，非缺陷）**：veh=27 的 `order=0 + real=17` 是 **R3R 等待链的正常形态**——WAIT_COUPLE 是被动等待，`current_order` 已被 `Free()`，`real` 停在等待点；`SKIP-STOPPED` 只是 `CheckIfTrainNeedsService()` 的 KI-220 自动回库守卫，不动订单。
- **S-3（部分收口）**：veh=57 在 `real=4` 是第二个 `GOTO_COUPLE`（`dest=923`，自身在 21,13），`COUPLE-FAIL` 366 次、`look=0`（**完全没有 lookahead/预留**，与 veh=48 的 `look=1` 不同族）。KI-193 的 `COUPLE-DEST-EMPTY` 兜底**只作用于 veh=54**（tile=1,15）⇒ 57 的滞留**完全没有兜底介入**，与 54 是两回事。**仍未取到"57 的目标车底在哪"（`CPL-GATE/CPL-S0-CHK` 的 `tgt=` 未取）⇒ 列为 U-2。**

### 3.3 待深挖项——收口情况

| 编号 | 结论 |
|---|---|
| S-1 | **已答**（§3.1）：`R3RTryPathReserveCore():13892` 被"同格自持预留的解出方 veh=0"挡住 ⇒ 永久 `TPRRF_NONE`。 |
| S-2 | **已答**：`one=1` 非原因；失败路径不消费 `ForceReserveOnce`（只在成功出口消费）。 |
| S-3 | **部分**：KI-193 兜底对 veh=57 **零触发**（它只服务 veh=54）；57 的目标车底位置未取到 ⇒ U-2。 |
| S-4 | **已答**：`order=0 + real=17` 是 WAIT_COUPLE 等待链正常形态，非缺陷。 |

## 四、复测判据（草案，待收口后定稿）

1. 同场景下不再出现 `TRP veh=<loco> ... => ok=0 res=0` 的长串（机车能拿到路径）。
2. `COUPLE-FAIL` / `COUPLE-DEST-EMPTY` 不再刷（或刷后机车确实改道/回库）。
3. 需要倒车驾驶的区段（T8701/2 的掉头点）上 `NOCAB-LIMIT` 至少出现一次，且机车实际限速 ≤32 km/h。
4. `DB-NORMALISE` 后 `nocab` 与当时链形态自洽（尾车真不能领头时应为 1）。
5. 两份日志（regular / non_stop）行为差异仅限停站相关，不影响 1~4。

## 五、本轮性质

**取证基本收口（KI-331 已改码并编译），KI-330 两个滞留点已定位、修法待定。**

## 六、KI-331 修复落地（2026-10-06，第 220 轮）

### 6.1 修法

`src/train_cmd.cpp` 新增文件内静态判据（`~:310`，紧邻 `GetTrainVehicleMaxSpeed()` 之后、`ConsistChanged()` 之前）：

```cpp
static bool R3RLeadingEndHasDrivingCab(const Train *last)
{
	if (!last->CanLeadTrain()) return false;
	/* R3R fake engine: engine subtype grafted onto a plain, cab-less wagon. */
	if (last->IsEngine() && RailVehInfo(last->engine_type)->railveh_type == RailVehicleType::Wagon &&
			!last->GetEngine()->info.extra_flags.Test(ExtraEngineFlag::HasCab)) {
		return false;
	}
	return true;
}
```

`ConsistChanged()` 的判据由 `!this->Last()->CanLeadTrain()` 换成 `!R3RLeadingEndHasDrivingCab(this->Last())`；`NOCAB-SET` 探针追加 `leadCab=%d` 字段。

### 6.2 判据设计说明

- 只把「**subtype 嫁接的货车**（假引擎）」判成无驾驶室：`IsEngine()==true` **且** `railveh_type==Wagon` **且** NewGRF 未声明 `ExtraEngineFlag::HasCab`。
- 保留全部既有放行：NewGRF `HasCab` 的货车（真能领头）、真机车（`Singlehead/Multihead`）、`IsRearDualheaded()`、真铰接件（`IsArticulatedPart()` 走 `CanLeadTrain` 内部分支）一律不受影响。
- 不动 `tracerestrict.cpp:807` 的 inverted 分支（那是「正向且尾端无驾驶室」的另一种语义，非本次报告面）。
- 不需要加 `else` 清位：`ConsistChanged()` 每节车在 `:358/:360` 用 `=` 重置 `cached_tflags`（含 `TCF_NO_DRIVING_CAB`），本判据只在末尾 `|=` 置位，语义与上游逐位一致。

### 6.3 与 §2.4 T-2/T-4 的关系（重要预期）

`DB-CLEAR`（耦合提交点 `:11178`，刻意清掉带进合并链的倒车态）仍会把它清空——那是**正确**行为（清后退车态，惩罚本就不该在）。修后应观察到的变化是：**纯倒车区段**（`REVERSEDIR db=1` 之后、未经过耦合提交点）的 `DB-NORMALISE headDB=1` 行 `nocab` 由 0 变 1，且 `NOCAB-LIMIT` 至少出现一次、实际限速 ≤32 km/h。

### 6.4 构建自证

- 复用既有 `_tmp_inc_build.cmd`（未新建任何 `.cmd`）；`build\R3R_incbuild.guard.log` = `GUARD: incremental is safe (no header/lang file is newer than the newest object)`。
- `src\train_cmd.cpp` @2026-10-06 21:40:07 → `build\CMakeFiles\openttd_lib.dir\src\train_cmd.cpp.obj` @21:41:39 → `build\openttd.exe` @21:44:32（51 671 552 B）。
- `[3/3] Linking CXX executable openttd.exe`；`build\R3R_incbuild.done` = `EXIT_CODE=0`；`error C*` / `fatal error` / `FAILED:` / `build stopped` 计数 **0**；`read_lints` 0 条。
- 产物自查：`build\openttd.exe` 内命中 `NOCAB-SET this=` 与新增字段 `leadCab=`。

## 七、KI-330 修法待定项（未落地）

- 滞留点 A（veh=48）：`u=0` 自持 `100,64` 整格预留把机车堵在自家格 ⇒ `FollowTrainReservation()` 回溯到 veh0。可选修法：①预留持有者判据（`R3RIsReservationHolder()`）排除「与另一列车同格且自身 spd==0 且非本车」；②`R3RTryPathReserveCore()` 被挡分支若 `origin.tile == consist->tile`（在自己的格上被别的车挡）则豁免。**须先加只读 dump 取 `origin/other_train` 实证（U-1）再动手。**
- 滞留点 B（veh=57）：`GOTO_COUPLE` 目标车底不存在、`look=0`、KI-193 兜底（只服务 veh=54）零介入 ⇒ 需要「目标车底查无」时的兜底（改道/回库/原地等待），U-2 待取 `CPL-GATE tgt=` 数据。

## 八、U-2 收口（第 221 轮取证，未改码）

**取数方式**：不重跑游戏，直接对玩家现场日志 `build\R3R_debug_regular.log` 过滤（`findstr /C:"veh=57"` → `build\_v57.txt`，950 行）。

### 8.1 veh=57 完整轨迹（自证 U-2）

```
1:ORD-IDX-WATCH site=load-rebuild veh=57 real=0->0 ... n=6->6 front=1        ← 自身排程 6 条
14:DEPOT-ARR veh=57 real=0(6) curType=6 tileDepot=1 tx=1 ty=15 destTx=16 destTy=14
32:REVERSEDIR veh=57 tile=16,14 dir=5 order=16                              ← 16,14 换端，进入 GOTO_COUPLE#1
33:TRP veh=57 mf=16,14 real=1 type=16 dest=1669 ... => ok=0 res=0           ← 1669 = (5,13)
114:COUPLE-OK loco=57 rear=26 consist=0 co=1 real=4 type=1 tx=12 ty=15      ← (12,15) 挂上车组 0，索引跳 4
137:LOCO-AFTER-DECOUPLE veh=57 curType=0 real=2 idxnext=6 tile=35,12        ← (35,12) 被解挂，回到自己排程
173:REVERSEDIR veh=57 tile=21,13 dir=5 order=16
179:DEPOT-ARR veh=57 real=4(16) curType=16 stuck=1 tx=21 ty=13 destTx=27 destTy=7   ← real=4 = GOTO_COUPLE#2 → (27,7)
880:TRP veh=57 mf=21,13 real=4 type=16 dest=923 spd=0 look=0 one=0 => ok=0 res=0    ← 923 = (27,7)
```

- `(27,7)` = `R3RDUMP-STATION idx=2 xy=27,7 lv=0` ⇒ **站台**（非车库）；`destDepot=0`。
- 终局循环：`CPL-ENTRY tile=21,13` → `CPL-ORIGIN origin=21,13 td=8` → **`CPL-PATHFOUND found=0`** → `CPL-SKIP retryIn=8` → `COUPLE-FAIL`（366 次），`look=0`。

### 8.2 U-2 答案：**零候选**，不是「有候选但状态不对」

- `CPL-GATE` 中 `act=57` **只有 3 行**（原文行 77/81/97），且**全部出现在 `COUPLE-OK`（原文行 114）之前**；`tgt` 恒为 **54**，`aOrd=16 tOrd=6`（原文行 77/81）/ `tOrd=2`（行 97）。
- ⇒ 54 是**机车**（自身持 `GOTO_WAYPOINT`/`GOTO_DEPOT`），不是等待车底：它在 57 第 1 次 GOTO_COUPLE 期间恰好与 57 相邻，属**无关干扰候选**，不是 57 的目标。
- 全图等待者普查：`curType=17` 仅 10 行，涉 `veh=0/6/26`，位置 1,15 / 12,15 / 35,12 / 102,58 / 100,64 / 100,63 / 99,105 / 44,116 / 101,97 / 100,57 —— **没有一个在 (27,7)**。
- ⇒ 终局阶段 57 的 GOTO_COUPLE **连一个候选都搜不到**（候选搜索返回 0 ⇒ `R3RCanCoupleNow` 的 `CPL-GATE` 一次都没被调用），`look=0`（无试探性预留）与之自洽。

### 8.3 对修法的影响（滞留点 B 口径修正）

原 §7 写法是「目标车底不存在、需兜底」；取证后**收紧为**：

- 真正的判据面 = 「**候选搜索返回 0**（`_r3r_couple_no_target`）**且** `R3RCoupleOrderDestinationReached()==false`」——两个条件同时成立才是本现场（车不在目的地 ⇒ KI-193 的 `COUPLE-DEST-EMPTY` 500-tick 闸门永远不点火；候选为 0 ⇒ 也永远挂不上）。
- 兜底必须挂在「零候选」这一面（连续 N 次零候选 → 改道/回库/原地等待三选一），**不能**只挂在到点面（那是 KI-193，已经存在，只服务 veh=54）。
- KI-331 无关：本轮不涉及。

### 8.4 新疑点 U-3（只登记，未下结论，勿当根因）

`CPL-GATE reject=target-not-wait site=depot act=54 tgt=27 aOrd=16 tOrd=0`。

- S-4 曾判 `veh=27 order=0 + real=17` 是「R3R 等待链正常形态」；但同一份日志有 10 行 `curType=17`，证明站台上真正的等待者**通常持有** `OT_WAIT_COUPLE`（`curType=17`）。
- 若 27 确实是等待者而 `current_order` 被 `Free()`，则 `R3RCanCoupleNow` 的 `target->current_order.IsType(OT_WAIT_COUPLE)` 会让它**永久不可挂**（同时解释 54 在自家车库格长期刷 `COUPLE-DEST-EMPTY`）。
- 反证待查：`SKIP-STOPPED veh=27 ... nord=33 front=1` ⇒ 27 疑为 **33 条订单链的链头**，未必是 car-only 车底。**必须先确认 27 的身份（是否 car-only / 是否真等待者）再定论**，本轮仅登记。

### 8.5 本轮未做

- **未改任何源码、未新增探针、未重跑游戏**（`_tmp_headless_run.ps1` 的 90 秒短探针被取消执行）。
- **U-1 仍未取到**：`TPR-BLOCKED` 的 `origin/other_train` 实证（滞留点 A / veh=48）需一次能跑到现场的运行。取到后与本节合并定稿修法。

## 九、口径校正（2026-10-06，玩家澄清）

玩家指出：本轮要修的是「**解挂之后机车不走**」与「**DB 不受惩罚**」两件事，而 §八 的「零候选」属于**挂接面**。对齐如下：

| 玩家口径 | 对应现场 | 机制面 | 状态 |
|---|---|---|---|
| ①解挂之后机车不走 | **滞留点 A：veh=48**（`LOCO-AFTER-DECOUPLE veh=48 tile=100,64` 之后每 tick `ok=0 res=0`） | **预留面**：解出方 `u=0` 自持 `100,64` 整格预留 ⇒ `FollowTrainReservation()` 回溯到 veh0 ≠ 48 ⇒ 永久 `TPRRF_NONE`（§3.1） | 根因已收口（S-1/S-2），**修法待定**（§七 两候选，卡 U-1） |
| ②DB 不受惩罚 | 全日志 `NOCAB-LIMIT=0` | 判据面：段尾假引擎骗过 `Last()->CanLeadTrain()` | **已修并编译**（§六 / KI-331） |
| （附）滞留点 B：veh=57 | `LOCO-AFTER-DECOUPLE veh=57 tile=35,12` → 21,13 不动 | **挂接面**：`GOTO_COUPLE` 候选搜索为 0（§八） | 取证完成，属**并列现象**，不是①的主因 |

⇒ 结论：**「解挂之后机车不走」的主修点是滞留点 A（与挂接无关）**；§八 的 U-2 收口只解释了同一场景里第二列滞留车（veh=57，其 `real=4` 订单本身就是 `GOTO_COUPLE`，挂不上所以不走）。修法优先级：**A 先行**（补 U-1 只读 dump → 在 §七 两候选中定稿），B 的零候选兜底排在 A 之后。

## 十、滞留点 A（DF4D-3058，veh=48）根因确定（2026-10-06）

玩家澄清：`veh=57` 名为 **HXN5B**，它的排程本来就设计成"后面挂不上车"，**不属于本轮问题，已排除**；"解挂之后机车不走"专指 **veh=48 = DF4D-3058**。

### 10.1 根因（静态可判定，不再依赖推测）

链条（每一环都有源码/日志支撑）：

1. **解挂后车底链头与机车同格**。`DECOUPLE-DONE u=0` 后，u 链 27 节停在 `100,64..100,57`，其链头 **veh=0 就停在 `100,64` —— 与机车 veh=48 同一格同一站台**（GEO settle-decouple-u 逐节 dump：`veh=0 tile=100,64 starF=1`，`veh=1 tile=100,63` …）。
2. 机车每 tick 走 `R3RTryPathReserveCore()` → `PBSTileInfo origin = FollowTrainReservation(consist, &other_train)`（`train_cmd.cpp:13947`）。
3. `FollowTrainReservation()`（`pbs.cpp:1050`）→ `FollowReservation()` → `CheckTrainsOnTrack()`（`pbs.cpp:986-1011`）扫描落点格上的列车。**关键三条**：
   - `VehiclesOnTile<VehicleType::Train>(tile)` —— **不排除 `consist` 自己**；
   - `t = t->First()` 后 `if (info.best == nullptr || t->index < info.best->index) info.best = t;` —— **取 index 最小的链头**（注释自陈 "ALWAYS return the lowest ID (anti-desync!)"）；
   - 若落点是站台，`pbs.cpp:1083-1093` 还会**沿整个站台逐格补扫**一遍。
4. 于是 `best = veh0`（0 < 48），`other_train = veh0`，`other_train->index != consist->index` ⇒ **`train_cmd.cpp:13953` 命中**：
   ```13953:13953:src/train_cmd.cpp
   if (other_train != nullptr && other_train->index != consist->index) {
   ```
   ⇒ `return TPRRF_NONE`（= 探针里的 `ok=0 res=0`），并在 `mark_as_stuck` 时 `MarkTrainAsStuck()`。

### 10.2 为什么"只有解挂之后"才卡

- **耦合态**：veh=0 与机车是同一条链，`t->First()` 对 veh=0 返回 **48** ⇒ `best = 48 == consist` ⇒ 13953 **不命中**，机车正常订路；
- **解挂之后**：veh=0 成为独立列车的链头，`First() = 0 ≠ 48` ⇒ 判据**翻转** ⇒ 每 tick 命中。

这条同时解释了 §3.1 观察到的两个现象：`TRP veh=48 ... one=1`（`ForceReserveOnce` 一直没被消耗 ⇒ 从未成功预留过）与 §3.1 的"被自己刚丢下的车底堵死"。

### 10.3 日志佐证（build\R3R_debug_regular.log，12041 行 = 解挂点）

| 行 | 内容 | 说明 |
|---|---|---|
| 12041 | `DEPOT-ARR veh=48 ... real=24(15) curType=15` | 机车在 100,64 停稳执行 DECOUPLE |
| 12066 / 12092 | `GEO settle-decouple-u ... veh=0 tile=100,64 starF=1` | 车底链头 **与机车同格** |
| 12146 | `LOCO-AFTER-DECOUPLE veh=48 real=11 tile=100,64` | 解挂后机车带着 real=11（GOTO_WAYPOINT→(102,68)） |
| 12168/12182/12184/12185/12223 | `TRP veh=48 mf=100,64 real=11 type=6 dest=8806 spd=0 fp=0 look=1 one=1 mstuck=0/1 fto=0 => ok=0 res=0` | **恒失败**，且从不在 `ChooseTrainTrack` 留下痕迹 |
| 12181 | `DEPOT-ARR veh=48 real=11(6) curType=6 stuck=1` | 被 `MarkTrainAsStuck()` |

**决定性对照**：机车此前每次成功预留都伴随一行 `CRT veh=48 ... found=1`（362/525/11383）或 `CPL-*`（11582-11633）；**解挂后（12146-12223）全无 `CRT`/`CPL` 行** —— 因为 `train_cmd.cpp:13953` 在 `14018` 的 `ChooseTrainTrack()` **之前**就 return 了。⇒ 排除"路点 (102,68) 不可达"（U-1 的另一个候选）。

### 10.4 仍待一次复跑确认（请玩家配合）

当前 `build\openttd.exe` @**2026-10-06 23:09:47**（51 675 136 B）已含两枚只读探针（零行为变更，增量构建 `EXIT_CODE=0`、`[3/3] Linking`、0 错误、`read_lints` 0）：

| 探针 | 源码位置 | 覆盖出口 |
|---|---|---|
| `TPR-BLOCKED` | `train_cmd.cpp:13962-13982` | `13954` 命中（被别的列车判为阻塞） |
| `TPR-WALK` | `train_cmd.cpp:14017-14041`（新枚举 `R3REDGE_TPRWALK` @`6461`） | block 判据**未**命中时的两个原本无痕出口：`reachable == TRACK_BIT_NONE`、`ChooseTrainTrack()` 订不到路 |

**上一份日志（20:59）早于该 exe，所以探针未跑到**（此前只能给"疑似"）。

复跑判据：同场景解挂后应出现
`TPR-BLOCKED veh=48 ... origin=100,64 okay=.. other=0 otherTile=100,64 otherFirst=0 caronly=1`
（`other=0` = 车底链头 / `otherFirst=0` = 该列车链头是自己 / `caronly=1` = 纯车底链）。
若该行**未**出现而 `TRP` 仍 `res=0`，则落到 `TPR-WALK`（`train_cmd.cpp:14017-14041`）出口，看 `new=` / `reach=`——那是另一个候选（`reachable == TRACK_BIT_NONE`）。

### 10.5 修法候选（待拍板，均未落地）

1. **解挂缝豁免（最小改动）**：`13953` 命中时，若 `other_train` 与 `consist` 同格（`other_train->tile == consist->tile`，或两者同站台且 `consist->cur_speed == 0`）则不视为阻塞，继续走 `ChooseTrainTrack()` 正常订路。风险最低，且不碰引擎的 `CheckTrainsOnTrack`。
2. **治根：解挂时把机车与解出方分离**，使 `u->First()` 不再停在机车格上（改解挂落点/位置分配）。影响面更大，需连查 `DecoupleTrain()` 的位置计算与站台容量。
3. （不建议）改 `CheckTrainsOnTrack` 让其排除 `consist` 自身/同格车 —— 这是引擎反 desync 敏感区，且会波及所有列车。

## 十一、第 221 轮复跑取证：滞留点 A 根因**做实**（2026-10-07）

**输入**：玩家新日志 `R3R_debug.log`（2026-10-07，D:\temp\codebuddy-dropped-files\...），由含 `TPR-BLOCKED` / `TPR-WALK` 的 `build\openttd.exe`（2026-10-06 23:09:47）产生。

### 11.1 决定性证据：`TPR-BLOCKED` 命中，字段与预测逐字吻合

```
7024: TPR-BLOCKED veh=48 tile=100,64 spd=2 mf=100,64 origin=100,65 okay=1 td=1 other=0 otherTile=100,64 otherMf=100,64 otherSpd=0 otherOrder=17 otherFirst=0 caronly=1
7069: TPR-BLOCKED veh=48 tile=100,64 spd=0 mf=100,64 origin=100,65 okay=1 td=1 other=0 otherTile=100,64 otherMf=100,64 otherSpd=0 otherOrder=17 otherFirst=0 caronly=1
```

| 字段 | 值 | 含义 / 与上一轮预测对比 |
|---|---|---|
| `other=0` | 0 | **就是解出的纯车底链头 veh=0**（预测 0 ✔） |
| `otherTile=100,64` | 100,64 | 与机车 `mf=100,64` **同格**（预测 100,64 ✔） |
| `otherMf=100,64` | | 该车底自身的 moving front 也停在同格 |
| `otherFirst=0` | 0 | `veh0->First() == veh0` = 已是独立列车（预测 ✔） |
| `caronly=1` | 1 | `R3RIsCarOnlyFormation` = 纯车底链 |
| `otherOrder=17` | 17 | `OT_WAIT_COUPLE`（它是原地等待者） |
| `otherSpd=0` | 0 | 它停着不动 |
| `origin=100,65 okay=1` | | **机车自己的预留终点是安全位（okay=1）** ⇒ 订路本身没问题 |

⇒ `other_train(0) != consist(48)` ⇒ **`train_cmd.cpp:13954` 命中** ⇒ `MarkTrainAsStuck()` + `return TPRRF_NONE`，**在 `ChooseTrainTrack()`（14018）之前返回**。上一轮 S-1/S-2 的静态推断**逐字成立**。

配套旁证：解挂点（6949 `DECOUPLE-DONE u=0 co=1 real=25 tx=100 ty=64`）之后 `TRP veh=48` 恒为 `ok=0 res=0`（7025 / 7070 / 7073），且**再无任何 `CRT veh=48`**（最后一条是 6059，全部 12 条都在解挂前）；`one=1` 说明 `ForceReserveOnce` 从未被消费。

### 11.2 排除另一候选（U-1b）：`reachable == TRACK_BIT_NONE` 不成立

- 全日志 `TPR-WALK veh=48` 共 **21 行**，**最后一行是 6179**（`mf=100,72 origin=100,72 td=9 okay=0 exit=3 new=100,71 reach=2`）；解挂点之后**一行都没有**。
- ⇒ veh=48 在解挂后**从未走到** 14017 的 TPR-WALK 探针，即 13954 之前就已返回 ⇒ **`reachable == TRACK_BIT_NONE` 与「`ChooseTrainTrack()` 订不到路」两条候选全部排除**。

### 11.3 关键修正：这不是「解挂独有」，而是**每次解挂都触发**的系统性现象

同一份日志 `TPR-BLOCKED` 共 **8 行**（730 / 1252 / 2897 / 3964 / 5058 / 5681 / 7024 / 7069），**每一条都紧跟一次 `DECOUPLE-DONE`**：

| 行 | 机车 | other | otherTile | otherFirst | otherOrder | 与 mf 同格 |
|---|---|---|---|---|---|---|
| 730 | veh=54 | 0 | 12,15 | 0 | 0 (OT_NOTHING) | 是 |
| 1252 | veh=57 | 0 | 35,12 | 0 | 0 | 是 |
| 2897 | veh=48 | 6 | 99,105 | 6 | 0 | 是 |
| 3964 | veh=51 | 6 | 44,116 | 6 | 0 | 是 |
| 5058 | veh=53 | 26 | 101,97 | 26 | 0 | 是 |
| 5681 | veh=50 | 26 | 100,57 | 26 | 0 | 是 |
| **7024** | **veh=48** | **0** | **100,64** | **0** | **17** | **是** |
| **7069** | **veh=48** | **0** | **100,64** | **0** | **17** | **是** |

- 前 6 次机车都在**数 tick 内自行脱离**（机车已带速滑走 / 车底很快移开），故未表现为「卡死」；只有 7024 这次演化为**永久滞留**。
- **唯一差异点**：只有 7024/7069 的 `otherOrder=17`（`OT_WAIT_COUPLE`）——解出方是**长期原地等待**的车底，它永远不离开机车所在格 ⇒ 即便 `origin.okay=1`（健康预留）也无法脱身，`ok=0 res=0` 每 tick 复现。
- ⇒ 结论升级：**「同格车底被误判为阻塞」是解挂路径的固有缺陷**，veh=48 只是它第一次表现为永久卡死。修法必须按「解挂通用缺陷」而非「本场景个例」来做。

### 11.4 一个仍未钉死的细节（U-1c，只登记，不影响修法）

`origin.tile = 100,65` 而 `otherTile = 100,64` —— 被判「碍事」的列车在**预留终点后方一格**，因此不可能由 1081 行 `CheckTrainsOnTrack(ftoti.res.tile = 100,65)` 直扫命中（`VehiclesOnTile(100,65)` 里没有 veh=0，其 `tile=100,64` 见 6911 行 `GEO settle-decouple-u n=0 veh=0 tile=100,64 x=1608 y=1026`）。**最可能是 `pbs.cpp:1083-1093` 的站台回扫**：`IsRailStationTile(100,65)` 为真时引擎沿站台逐格**向后**补扫，在 100,64 命中 veh=0。前 6 次 `origin == otherTile == mf`（预留终点就是自身格），不需要这条解释。

- 影响面：**只影响日志解读，不影响任何修法** —— 三个候选都能同时覆盖「直击」与「回扫」两个入口（豁免判据取 `other_train->tile == consist->tile` 即可，两种入口的 other 都满足）。
- 若将来要区分，给 `TPR-BLOCKED` 追加一枚 `resStn=IsRailStationTile(origin.tile)` 即可（本轮未加）。

### 11.5 修法复核（基于实证更新，§10.5 三候选不变，候选 1 依据更强）

1. **解挂缝豁免（首选，最小改动）**：`13954` 命中且 **`other_train->tile == consist->tile`（同格）且 `consist->cur_speed == 0`** ⇒ 不视为阻塞，继续 `ChooseTrainTrack()` 正常订路。
   - **实证支持**：8 次命中**全部**满足 `otherTile == mf`；而 `origin.okay=1` 证明订路本身健康 ⇒ 豁免后立即恢复。
   - **比 §10.5 原写法更严（重要）**：原写法把「同站台」也纳入豁免，**实测不需要** ⇒ 收紧为**只豁免「同格」**。理由：真阻塞的形态应是「另一列车站在我**前方**的预留终点格上」，那时 `other_tile == origin.tile ≠ consist->tile`，同格豁免天然不误伤它。
   - 负判据见 11.6 第 5 条。
2. **治根（解挂时不把两列车放在同格）**：仍应做，但**不能只做这一条** —— §11.3 显示同格是 8/8 的常态，说明落点算法本身就会产生同格，属更上游的问题。
3. **不改 `CheckTrainsOnTrack`**：实证再次确认它只看「index 最小」、与订单无关，属引擎反 desync 冻结区。

### 11.6 复测判据（定稿）

1. 同场景解挂后**不再出现** `TPR-BLOCKED veh=48 ... otherFirst=0 caronly=1`（或出现但同 tick 内 `TRP veh=48 => ok=1 res=1` 恢复）。
2. 解挂后应重新出现 `CRT veh=48 ... found=1`（回到 6059 之前的形态）。
3. veh=48 能继续驶离 100,64、执行 `GOTO_WAYPOINT dest=8806`，不再累加 `mstuck=1`。
4. 另 7 处解挂（veh=50/51/53/54/57）行为不回退：`TPR-BLOCKED` 消失或同 tick 恢复。
5. **负判据（不得误伤真阻塞）**：构造「另一列车停在我前方的预留终点格上」，仍须 `ok=0 res=0` 且不移动。
6. `one=1`（`ForceReserveOnce` 永不被消费）随之一并消失（13954 不再提前返回）。



## 十二、第 222 轮（2026-10-07）：解挂缝豁免落地（已修）

玩家拍板：确认不需要在「同格」之外再加轨道位方向判据，按最简口径落地。

### 12.1 落点（仅 `src/train_cmd.cpp`，未碰任何 `src/*.h`）

`TryPathReserveWithResultFlags()` 的阻塞分支（原 `13954`，改后 `13955`-`14020`）：

- 保留 KI-330 U-1 的只读 `TPR-BLOCKED` 探针原样不动。
- 阻塞分支末尾的 `MarkTrainAsStuck + return TPRRF_NONE` 收进 `else`，条件变为：

```cpp
if (other_train->tile == consist->tile && consist->cur_speed == 0) { /* 豁免：继续往下走正常订路 */ }
else { if (mark_as_stuck) MarkTrainAsStuck(consist); return TPRRF_NONE; }
```

- 新增边沿探针标签 `R3REDGE_TPREXEMPT`（插在 `R3REDGE_TPRWALK` 之后 / `R3REDGE_COUNT` 之前），日志行
  `TPR-SEAM-EXEMPT veh= tile=x,y spd= other= origin=x,y okay= td= otherOrder= caronly=`。

### 12.2 为什么豁免不会撞车（玩家第 221 轮的疑问，本轮定论）

豁免只取消那个「一票否决」，**不给机车任何特权** —— 它仍然要走下面正常的 `ChooseTrainTrack` / `TryReserveRailTrack` 流程。真正防撞的是 PBS 预留本身，共三层：

1. **订路层（主防线）**：`TryReserveRailTrack` 的语义就是「这段轨是空的、且不跨任何别人的预留才成功」（`src/pbs.cpp:212` 注释、`:250` `TryReserveTrack`、`:285` 站台 `HasStationReservation`）。列车每前进一格都必须先预留成功；站着不动的车底脚下轨道**是它自己的预留**（`Train::ReserveTrackUnderConsist()`，`src/train_cmd.cpp:14708-14776`，即日志里 `RESERVECONSIST` 那行每 tick 在刷新）。⇒ 它占着的轨道，我预留不到，我就停在这一格外面。
2. **行进层（兜底）**：`TrainController` 移动循环每帧 `TrainCheckIfLineEnds(moving_front)`（`src/train_cmd.cpp:17676` 一带）＋ `CheckTrainCollision`（`:15123` 起），不会静默穿过去。
3. **判据层（结构性）**：真阻塞的形态是「另一列车停在我预留路径的最远端那一格上」，即 `other_tile == origin.tile` 且 `origin.tile != consist->tile` —— **不同格**，因此天然进不了 `other_train->tile == consist->tile` 这条豁免。反过来，「同格且它真的挡在我前进轨道上」在 PBS 下不是稳定状态：那段轨道是它的预留，我根本进不到这一格，两列车不可能同格。

⇒ 玩家拍板：不再追加「轨道位不同」的第二重收紧（原口径 2 不采用），只保留同格 + 停稳。

### 12.3 构建自证

复用既有 `_tmp_inc_build.cmd`（未新建任何 `.cmd`）：

- `build\R3R_incbuild.guard.log` = `GUARD: incremental is safe (no header/lang file is newer than the newest object)`
- `build\R3R_incbuild.log` = `[3/3] Linking CXX executable openttd.exe`，`error C*` / `fatal error` / `FAILED:` / `build stopped` 计数 **0**
- `build\R3R_incbuild.done` = `EXIT_CODE=0`（20:32:15）
- `src\train_cmd.cpp` 20:29:55 → `train_cmd.cpp.obj` 20:30:44 → `build\openttd.exe` **2026-10-07 20:32:05（51 677 696 B）**
- `read_lints` 0 条；exe 内命中新字面量 `TPR-SEAM-EXEMPT`

### 12.4 复测判据（§11.6 六条 + 本轮新增两条）

1~6 条同 §11.6。

7. **豁免侧证据**：豁免生效时日志出现 `TPR-SEAM-EXEMPT veh=<loco> tile=<与车底同格> other=<车底链头> caronly=1`，且其后能看到该机车的 `CRT` / `TRP ... => ok=1 res=1`。
8. **负判据（口径 1 下仍成立）**：另一列车停在我**前方预留终点格**（与我不格同）时，`other_train->tile != consist->tile` ⇒ 不豁免 ⇒ 照旧 `MarkTrainAsStuck` + `TPRRF_NONE`，`ok=0 res=0` 且不移动。



## 十三、第 222 轮复测（2026-10-08）：解挂缝豁免 **实机验证通过**

复测日志 = `D:\temp\codebuddy-dropped-files\f599deea-914d-4ca8-9e73-5a0f53b01042\R3R_debug.log`（1 072 295 B / 16 143 行 / 2026-10-08 18:42，内测版（探针 ON）产出）。

> 取证注意：该日志是 **GBK** 编码，含非 UTF-8 字节，`ripgrep`（含 IDE 的 search_content）会把它判为二进制而返回 **0 命中** —— 必须改用 `Select-String -Path <log> -Pattern '<str>' -Encoding Default`。

### 13.1 决定性四行（同一个滞留点 A：12292 解挂 → 12325 未停稳 → 12330 豁免）

```
12292: LOCO-AFTER-DECOUPLE veh=48 curType=0 real=11 idxnext=6 tile=100,64
12325: TPR-BLOCKED veh=48 tile=100,64 spd=2 mf=100,64 origin=100,65 okay=1 td=1 other=0 otherTile=100,64 otherMf=100,64 otherSpd=0 otherOrder=17 otherFirst=0 caronly=1
12326: TRP veh=48 mf=100,64 real=11 type=6 dest=8806 spd=0 fp=0 look=1 one=1 mstuck=1 fto=0 => ok=0 res=0
12327: TRP-AUTO veh=48 real=11 type=6 dest=8806 ok=0 res=0 stuck=1 wc=0
12330: TPR-SEAM-EXEMPT veh=48 tile=100,64 spd=0 other=0 origin=100,65 okay=1 td=1 otherOrder=17 caronly=1
12331: CRT veh=48 order=6 dir=3 origin=101,66 td=5 found=1
12334: TRP veh=48 mf=100,64 real=11 type=6 dest=8806 spd=0 fp=0 look=1 one=0 mstuck=0 fto=0 => ok=1 res=1
```

三个要点：

1. **同格判据逐字命中**：`otherTile=100,64 == mf=100,64`、`other=0`（解出的纯车底链头）、`otherOrder=17`（`OT_WAIT_COUPLE` 长期等待者）、`caronly=1` —— 与第 221 轮 `7024` 行的现场完全同形，就是同一个滞留点 A。
2. **"停稳"这个分量真的在起作用**：`12325` 那次 `spd=2`（还在滑行）⇒ **不豁免**、照旧 `MarkTrainAsStuck`+`ok=0`（`mstuck=1`）；到 `12330` `spd=0` 才豁免。⇒ 豁免不是"无条件放行"，两个条件同时成立才生效。
3. **豁免后立刻回到正常订路**：`12331 CRT found=1` → `12334 ok=1 res=1`，`mstuck` 归 0，`one=1`（`ForceReserveOnce` 永不被消费的旧症状）也归 0。

### 13.2 是否真的开走（排除"订到路但不动"）

```
12466: TRP veh=48 mf=102,68 real=12 type=6 dest=7014 spd=0 fp=0 look=1 one=0 mstuck=0 fto=1 => ok=1 res=1
12807: TRP veh=48 mf=102,54 real=13 type=16 dest=7396 spd=0 fp=0 look=1 one=0 mstuck=0 fto=1 => ok=1 res=1
```

位置 `100,64 → 102,68 → 102,54`，订单索引 `real 11 → 12 → 13` 连续推进 ⇒ 滞留点 A 彻底解除。

### 13.3 逐条核对 §11.6 + §12.4 判据

| # | 判据 | 结果 | 证据 |
|---|---|---|---|
| ① | 解挂后不再被该阻塞永久钉住 | ✅ | 13.1（豁免行 + 其后 `CRT`/`ok=1`） |
| ② | 重新出现 `CRT veh=48 found=1` | ✅ | `12331`（全日志 `CRT veh=48` 共 14 次） |
| ③ | veh=48 驶离 100,64 执行 8806、`mstuck` 不累加 | ✅ | 13.2；`mstuck=1` 只在 `12326` 出现一次、随后归 0 |
| ④ | 其余解挂点无回退 | ✅ | 全日志 **11 次** `LOCO-AFTER-DECOUPLE`（veh=54 / 57 / 48×4 / 51 / 53 / 50×2 / 59）；被短暂标记的 veh=57（@1313）/ veh=51（@6173）均在数 tick 内自愈（`1351` / `6216` 起 `ok=1 res=1`） |
| ⑤ | 负判据：前方预留终点格（**不同格**）真阻塞仍 `ok=0` | ⚠️ **本轮无样本** | 全日志 8 次 `TPR-BLOCKED` **全部**是 `otherTile == otherMf == mf`（同格）+ `spd=2`，没有一次"不同格真阻塞" ⇒ 无法在本日志验证 |
| ⑥ | `one=1` 随之消失 | ✅ | `12326 one=1` → `12334 one=0` |
| ⑦ | 豁免行出现且其后 `CRT` / `ok=1 res=1` | ✅ | 13.1 |
| ⑧ | 真阻塞不移动 | ⚠️ 未见 | 同 ⑤ |

### 13.4 8 次 `TPR-BLOCKED` 的形态（8/8 同格、8/8 行驶中）

`781 veh=54 12,15` / `1684 veh=57 35,12` / `5801 veh=48 99,105 other=6` / `7362 veh=51 44,116 other=6` / `9300 veh=53 101,97 other=26` / `10266 veh=50 100,57 other=26` / `12325 veh=48 100,64 other=0 otherOrder=17` / `14318 veh=50 27,11 other=26`。

⇒ **8/8 都是"解挂缝同格 + 还在滑行"**，与第 221 轮的 8/8 分布一致：这条分支的命中形态就是解挂瞬间，豁免的定位正确，且因为要等 `spd=0`，它们绝大多数在滑行结束那 tick 就被放行（本轮唯一"滑行中命中且没被同格豁免钉住"的就是 veh=48，且下一 tick 即豁免）。

### 13.5 未验证 / 存疑（不得当"已修"）

- **U-1（负判据无样本）**：判据 ⑤/⑧ 未实机验证。要拿到样本需要"另一列车停在我预留终点格上"的场景，本日志未出现。口径 1 下它是结构性的（不同格 ⇒ 进不了豁免），但**仍属未验证**。
- **U-2（`ok=0` 仍占多数）**：veh=48 全日志 `ok=1 res=1` 19 次 / `ok=0 res=0` 65 次。后者集中在 `real=7 type=16 dest=12515` 的等挂车段（`8742`~`9390`，`mf` 恒 `99,93`，且 `mstuck=0`）—— 是 `GOTO_COUPLE` 目标未到位时的正常形态（**未标 stuck、不刷屏**）。本轮判为正常，但未逐条穷尽。
- **U-3（尾部仍在等待）**：日志尾 `veh=50(42,10)` / `veh=53(99,109)` 仍在 `COUPLE-FAIL` + `COUPLE-WAIT-OFFDEST`（`type=16`、`mstuck=0`），属"等目标车底"常态，未定性为缺陷。
- **U-4**：日志 18:42 结束，全日志无崩溃 / assert，`STUCK veh=` 0 次。

