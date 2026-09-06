# R3R 论证备忘：假铰接 flag —— 把 artic 语义从「身份」参数化为「状态」

日期：2026-09-06
状态：论证文档（未拍板，待用户裁决）
前置：`R3R_artic_detach_snapshot_memo.md`（论证A：升段时打散 + 数值覆写，已定稿待实现）

## 0. 一句话

用户的 NewGRF 类比（列车载客时由状态驱动显示不同图像/不同行为、实体身份不变）指向一个更深的
结论：**OpenTTD 的"铰接"是身份（subtype 位），不是状态**。论证目标：论证A 打散之后，给节们一个
**独立的"假铰接 flag"**，让系统对它们仍按"一个 artic 组"消费（组行为保留），但解除 artic 组
"内部顺序焊死"这一约束 —— 组内节序可像普通车一样重排。
论证结论：**机制上成立**，前提是把"artic 顺序焊死"的根因（规格挂父车 + 图像按组内位次查询）
用"逐节烘焙"消解 —— 这正是论证A 的覆写思路向视觉/语义层的推广。

## 1. 类比的技术根基（已核实）

NewGRF 给 artic 部件提供变量 **0x4D "Position within articulated vehicle"**
（newgrf_engine.cpp:754-762）：返回 `artic_before | artic_after << 8`（节前/节后各有几节 artic），
缓存于 `NCVV_POSITION_IN_VEHICLE`。含义：**artic part 的呈现（sprite/CB 状态）由"自己在组内的
位次"运行时查询决定**，与"载客驱动图像"同属"实例状态 → 呈现"机制。
推论：artic 顺序约束不是引擎强制的，是 GRF 依赖 0x4D 位次出图的约定；缓存需
`InvalidateNewGRFCache()` 刷新。

## 2. 事实盘点：artic 语义今天全部焊在 subtype 身份位上

`IsArticulatedPart()`（ground_vehicle.hpp:460；Vehicle 版 vehicle_base.h:1145）=
`HasBit(subtype, GVSF_ARTICULATED_PART)`。消费点分四类（均已核实）：

### A. 组单元语义（组 = 链上 1 个真单元，artic part 不算真车）
- 遍历：`GetNextVehicle/GetPrevVehicle`（vehicle_base.h:1218-1237）沿 artic 跳到组尾跨组；
  `GetFirstEnginePart/GetLastEnginePart`（1185-1212）；`GetNextUnit/GetPrevUnit/GetLastUnit`
  （train.h:273-303）再叠跳 rear-dualheaded。
- 计数/运营：`IsEngineCountable`（vehicle.cpp:1043）不算；`TCF_MOVING_UNIT_START` 只标组首
  （train_cmd.cpp:312-317）；`cached_num_engines` 组内只计父车（346-348）；站台占位/装载动画
  按 moving unit 共享（economy.cpp:1898-1902、2087-2091）。
- refit 整组共享新 subtype（vehicle_cmd.cpp:614-618）；换代整组随父车换、part 跳过
  （autoreplace_cmd.cpp:147-161，assert 见 285/641/946）；颜色随组首（vehicle.cpp:3143）；
  depot 清单/计数不算（vehiclelist.cpp:88-92、vehicle_gui.cpp:1351-1354）。

### B. 渲染组
cargo overlay 在组内合并（vehicle_gui.cpp:387-391 同 cargo 不新开槽；train_gui.cpp:364、
376-380、463-467 按组统计绘制）。

### C. 物理约束与顺序焊死
- 链拼接禁止插到 artic 组中间（train_cmd.cpp:1916-1920 assert）。
- 组内顺序在段翻转（`R3RFlipChainBySegments` train_cmd.cpp:4095-4175）中**从不重排**：
  block = 父车+连续 parts 整体收集（4116-4121），段内只把"块"倒序搬移、块内顺序不动（4139-4149）。
- 换端、耦合、折叠检查（R3R 历史所有 artic 死结）均以"artic 块不可拆不可变序"为前提。
- artic part 自身不为"独立车"提供图像方向语义；视觉是整组拼接，翻转时靠块整体搬移维持。

### D. 状态查询
0x4D 位次（见 §1）；artic part 的动力/自重按父车派生（train_cmd.cpp:10028-10032 动力归属、
9961-9965 自重跳过、GetPower 恒 0 见论证A memo §2）。

## 3. 论证

### 3.1 焊死顺序是"副产物"，不是引擎强制
artic 组之所以"内部不可重排"，引擎里没有任何"父车必须在儿子前"的运行时检查 —— 焊死来自
两个设计事实：① 整组规格按一次挂在父车（重排后规格归属错）；② 图像按 0x4D 位次查询
（重排后图像与车不符）。论证A 已消解 ①（数值覆写逐节摊开）；因此"假 flag + 内部可重排"
成立与否，**只剩 ② 要解决**：把每节视觉烘焙成节自身属性（见 §4），此后位次变化不再破坏图像。

### 3.2 一个决定性差异：段原子性
artic 组在真 artic 态不可被"劈"成更小的运营段（组内不能落 ★ 段头，artic 组不可拆）。
论证A 打散后组内节变普通车：若放任，段边界 ★ 可落到组内任意节，把"原本一个不可拆物理单元"
劈成多段 —— 升段反而打开了 artic 组内部的拆散口。假 artic flag 的一个硬利益就是**保住
"artic 组是段的最小可拆单元"**：组行为回来，段头只能落在组首，组不可劈。

### 3.3 结论先行
"假 flag 使组行为回来 + 内部序可重排"两者**可以并存**：组行为=§2 四类消费点对整组消费；
内部重排=节序/链序的普通车式操作。真正的难点与待拍板项在 §5。

## 4. 技术形态（两个候选）

### 形态甲：统一判定位（改动最小）
新增 VehicleRailFlag 位（如 `FakeArticulatedPart`，建议 =25），把 `IsArticulatedPart()` 语义
参数化为：`subtype bit || railflag`（可改一处 ground_vehicle.hpp + vehicle_base.h 判定）。
- 所有 §2 消费点自动把"假组"当 artic 组消费 —— 零散改动，风险低。
- 代价：artic 的"生成物身份"（真）与"运营分组状态"（假）共用一个判定入口，凡是今后想区分
  两态的代码都要再判 railflag。0x4D/`GetFirstEnginePart`/颜色/换代按"真 artic"语义对假组生效
  —— 需要在消费点逐个确认可接受（见 §5）。

### 形态乙：组头标记 + 成员标记（语义干净，工作量大）
组首节标 `ArticGroupHead`，组内节标 `ArticGroupMember`；artic 消费点按"从成员回走找组头、
从组头走到组尾"重写。真 artic 与假组共用同一组语义层，身份位彻底解耦。
- 工作量大（§2 全部消费点重写），但为后续"artic 状态化"打底最干净。

### 视觉烘焙（两个形态都需要的前置）
重排后图像正确的唯一通用手段：**打散瞬间把每节按 0x4D 位次查出的视觉烘焙进节自身**（覆写
sprite/palette，绕过 GRF 位次查询），之后节随 sprite 走、位次变化不再错图。这与论证A 把
数值烘焙进节的思路同构 —— 论证A 的覆写字段设计应预留 sprite/visual override 扩展位。

## 5. 冲突与待拍板

1. **"重排节 = 换图像"的语义**：烘焙后重排，视觉序列是"原 artic 整组图被切散重排"——
   每节仍是"自己在原组的图"。要的是这个（真·可互换整组视觉），还是要"每节重排到新位后重新
   查询 0x4D 出图"（保持组视觉连续，但引擎无权回写 GRF 缓存语义）？前者可行、后者不可行。
2. **artic part 是否有各朝向 sprite**：真 artic 部件图像常为单向组图。假组若允许段内逻辑翻转
   （direction 反），节图要能出反向图 —— 需核实 GRF 的 part sprite 是否随 direction 提供
   （列待实测；R3R 段翻转按"块整体搬移、块内不动"正是为了绕开该问题）。
3. **换代（autoreplace）**：artic 整组随父车 record 换代。打散+假组后组内节 record 可能同款；
   换代粒度为"整组"还是"每节"？整组换代生成新真 artic 后打散态是否重建（烘焙/覆写/flag 重做）？
4. **refit/cargo overlay**：真 artic 组 refit 整组同 cargo、overlay 合并一槽（vehicle_gui.cpp:387）。
   假组若保留组行为，则"每节独立 refit"被打散后获得的自由被收回 —— 确认这正是用户要的
   "行为依旧像铰接式"，否则冲突。
5. **组内节解挂**：假组组行为回来 → depot 中组内节不可单独拖出/卖（artic part 不可被当作
   独立车选中）。与论证A"打散成普通车"的表述需对齐：打散 = 数值/统计层面独立，运营/物理
   层面仍是一组。
6. **降级还原**：CmdDemoteSegment 还原真 artic 前须抹掉假 flag + 烘焙视觉 + 覆写
   （论证A §4 已有"清覆写"，新增清烘焙）。
7. **存档**：假 flag、视觉烘焙、覆写全部入档。
8. **0x4D 缓存**：烘焙/位次变动后需 `InvalidateNewGRFCache`（NCVV_POSITION_IN_VEHICLE）。

## 6. 与论证A 的关系（字段设计预留）

- 若本论证通过，论证A 的实现应把"per-vehicle 覆写"做成**一组可扩展烘焙字段**
  （weight/power/max_speed + 视觉 sprite/palette 预留），打散流程统一为
  "快照 → 摘身份 → 烘焙（数值+视觉）→ 打假组标记"。
- 论证A 备忘 §1 的"engine_type/sprite 直接继承"在假组方案下改为"sprite 按位次烘焙继承"。
- 本论证是论证A 的**可选叠加层**，不阻塞论证A 先行实现。

## 7. 关联

- `R3R_artic_detach_snapshot_memo.md`：论证A（打散 + 数值覆写，本论证的前置与载体）。
- `R3R_artic_fake_parent_identity.md`：旧的"假爸爸身份"设想（只改标记不解物理断口）——
  本论证继承其教训：**flag 必须接入真实消费点（遍历/渲染/折叠）才有效**。
- `R3R_couple_fold_catchup.md` / `R3R_flip_chain_decision.md`：段翻转对 artic 块的现状
  （§2.C），本论证若通过则段内 artic 块可被"拆序重排"，是这些死结的治本方向。
- `R3R_artic_couple_memo.md`：artic 组 = 不可拆物理单元的既有语义。

