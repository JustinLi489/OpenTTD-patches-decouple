# R3R 耦合方向统一与图像方向裁决备忘(2026-09-07)

行号以当日代码为准,后续漂移按函数名检索。

## 背景:四条意见裁决进度

1. **造车出库**:接受,保持原样(`v->direction = DiagDirToDir(dir)`,初值来源不动)。
2. **artic 部件生成**:接受,保持原样(`v->direction = first->direction`)。
3. **翻转/拼接与 direction 的关系**:裁决中(本文档)。
4. **(耦合方向更新边界)**:未裁决。

## 意见3 裁决(2026-09-07)

用户澄清:
- 弃用的 "flip" 仅指**物理反转**(force end swap / 整列换端 `ReverseTrainSwapVeh`)。
- 想要的效果 B:耦合后拼入段(等待段)保留**倒退视觉**(保持等待时的朝向被拖/被推走),不是翻正视觉(A)。

### 方案:图像方向与 direction 解绑 = 复用 `VehicleRailFlag::Flipped`

用户曾提出 `graphicdirection` 字段/布尔(出库为真,防折叠翻转则变)。经核查,**该机制已在 JGRPP 中存在**,即
`VehicleRailFlag::Flipped`(train.h:32,"Reverse the visible direction of the vehicle"),理由:

- 车辆在轨道上只有两个合法朝向,图像方向合法值只有 `direction` / `ReverseDir(direction)` 两种——
  独立字段只会引入"图像与轨道朝向脱节"的非法态;布尔版(= Flipped)表达力完整。
- Flipped **不是物理反转**:不碰 direction、不碰运行、不碰 DB,只换这一节画出来的脸,与玩家操纵的 force end swap 是两码事。
- 渲染全链路已消费该位,零渲染改造:
  - `Train::GetImage`(train_cmd.cpp:1435):取图方向时 `if (Flipped) direction = ReverseDir(direction)`,覆盖所有 train 车辆(含 artic parts);
  - bounding box / artic offset(train_cmd.cpp:1409、2583-2600):奇偶长度按 Flipped 镜像;
  - 烟效 / 效果车辆方向(vehicle.cpp:4257 / 4465);
  - RailFlips 引擎走 REVERSED sprite(newgrf_engine.cpp:1039)。
- 维护点已是摆放类常态:造车概率(1611/1721/1801)、artic 造车(articulated_vehicles.cpp:512)、
  复制粘贴(vehicle_cmd.cpp:1391/1397)、单节 reverse 命令(3438)、模板车辆、autoreplace(540)。

### 语义对齐:方向变了、图像不变 = Flipped 位跟踪 direction 的改写

不是"拼接后补设一次 Flipped",而是在**每次 direction 被逻辑改写的同时,对同一批节 Flip Flipped**,
使渲染方向 = ReverseDir(新 direction) = 旧方向,视觉原地不动;方向翻回时再 Flip 一次,保持抵消。

## 清单(实现顺序与状态)

- [x] **清单4(确认)** —— 翻转范围与 artic parts 覆盖核实,结论:
  - `R3RFlipChainBySegments`(train_cmd.cpp:4228)第1步调 `R3RReverseChainDirections`(4004)对**全链
    (含 artic parts)**逐节 `direction = ReverseDir(direction)`;
  - artic 块(父车+连续 parts)整体成块倒序重链,块内顺序不拆(4271-4284);
  - 方向注入点唯一且成对:`R3RReverseChainDirections` 仅 2 个调用点 ——
    `R3RFlipChainBySegments`(4233,翻转)与 `R3RUndoLogicalFlip`(4066,回滚),成对对称、回滚不漂移。
  - **清单2/3 注入点 = `R3RReverseChainDirections` 内部**(全链均匀翻转,无跨 artic 边界遗漏)。
- [ ] **清单1(本次实施)** —— Couple 方向统一循环(train_cmd.cpp:4695-4696):
  - 现状:拼入段每节 `w->direction = v->direction`(整列运行语义统一,moving-* 不撕裂)。
  - 改动:对**被改写**的节(统一前 `w->direction != v->direction`,即 nose-to-nose 拼入段)同步
    `w->flags.Flip(VehicleRailFlag::Flipped)`,图像保持等待姿态(倒退被拖)。
  - 折叠修正路径不受影响:修正后拼入段 direction 已与机车同向,不触发 Flip。
  - 用 `!=` 而非无条件 Flip:兼容拼入段已有 Flipped 初值(造车概率/单节 reverse),保持原渲染结果。
- [x] **清单2** —— `R3RFlipChainBySegments` 的 direction 反转处(`R3RReverseChainDirections`,
    train_cmd.cpp:4004)同步 Flip Flipped:防折叠逻辑翻链时图像原地不动。
- [x] **清单3** —— `R3RUndoLogicalFlip` 回滚自动成对:`R3RReverseChainDirections` 仅 2 个调用点
    (Flip 在 R3RFlipChainBySegments、Undo 在 R3RUndoLogicalFlip),方向翻回时自动再 Flip 一次,
    无漂移 —— 清单2/3 是同一个注入,一并落地。

## 实施记录

- **清单1(2026-09-07 已落地)**:Couple 方向统一循环(train_cmd.cpp:4695 区域)对 direction
  被改写的节 `w->flags.Flip(VehicleRailFlag::Flipped)`,拼入段保持等待姿态被拖走。
- **清单2+3(2026-09-07 已落地,同一注入点)**:`R3RReverseChainDirections` 每节
  `direction = ReverseDir(...)` 后同步 `w->flags.Flip(VehicleRailFlag::Flipped)`,doc 注释同步
  更新(不再是"回滚专用")。编译验证:增量构建 BUILD_EXIT=0,openttd.exe 已更新;
  仅存 3 个既有 warning(train_cmd.cpp 3593/8892/8894),非本次引入。
- **标记 stable(2026-09-07)**:R3R_debug.log(20:25 运行)实测——场景2 直拼 gap25 折叠
  →A2只翻u(gap7,回滚健康)→A3只翻v(gap17,回滚健康)→A3双翻(物理v+逻辑u)→gap1→
  COUPLE-OK,9节全链方向统一 dir=7;无崩溃/错误/死循环刷屏,图像保持生效。
  已备份至 build\r3r_stable_2026-09-07\(README+openttd.exe+src\ 5文件:
  train.h/train_cmd.cpp/vehicle.cpp/vehicle_cmd.cpp/vehicle_gui.cpp,回退方法见 README)。
  前提核实:`RestoreTrainBackup`(train_cmd.cpp:1866)只重建 SetNext 链序、不碰 direction/flags;
  4 个 `R3RFlipChainBySegments` 调用点(候选1 u / 候选2 v / 候选3 v+u)均与
  `R3RUndoLogicalFlip` 回滚严格配对,翻转与回滚成对对称。渲染侧 artic/bbox 已按 Flipped
  镜像(UpdateDeltaXY 2583-2600、GetImage 1435、artic offset 1409),无需额外刷新。

## 实施备注

- Flipped 已全程被运行侧忽略(运行只读 direction / DB / moving-* 家族),本方案零运行语义改动。
- 4695 循环起点 `merged_first`(普通拼接 = u;折叠修正后 = 翻转段新链头)覆盖两种路径;
  循环含 artic parts,artic 图像随父车同步 Flip,offset 由 1409/2583-2600 自动镜像,无错位。
- 折叠修正成功路径:逻辑翻转后 Flipped=1(图像保持翻转前朝向),拼入段 direction 已与机车同向,
  4695-4696 的 `!=` 判定不触发额外 Flip,无双重翻转。
