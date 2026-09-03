# R3R「到达前调向 / 到站反转」(Reverse/Flip on Arrival) 开发备忘

> 目的:防止会话压缩后忘记方案与已确认事实。续作先读本文件。
> 更新:2026-09-03。分支 `feature/decouple`(领先 origin 3 commits;工作区含 M2 depot 手动操作改动,勿覆盖)。

## 1. 需求与已拍板决定

- 用户(2026-09-03 更新):**"调向"不再是 OT_GOTO_COUPLE 附属**,而是做成
  **OT_GOTO_STATION / OT_GOTO_WAYPOINT 订单的"到达后调向"标志**,放在订单列表中
  GOTO_COUPLE **之前**:机车段先驶往一个"转向点"站/路点 → 到点调头 → 再以正向姿态
  执行后面的 GOTO_COUPLE(避免 db/backup 倒车够段造成的折叠死结)。
- 翻转不可行的兜底行为(用户选定):**停车报错,人工介入**。
- 背景:倒车挂车折叠崩溃定位见记忆 31435600(veh=2 进 tile 39,30 找不到 _connecting_track,
  fold-geom 探针未删)。原"GOTO_COUPLE 附属"方案被本条取代(记忆 60885272 中相关条目以本条为准)。

## 2. 调研结论(2026-09-03)

### 2.1 重大发现:OT_GOTO_WAYPOINT 反转【已内建】
- `OrderWaypointFlag::Reverse`(waypoint order flags bit):列车到 waypoint 后以整列长度
  越过、再倒回(跑 single-track runaround 的标准玩法)。
- 全链路已存在:
  - order_gui.cpp 2466(waypoint detail 显示 WID_O_REVERSE)/ 3425(点击 → MOF_WAYPOINT_FLAGS);
  - station_cmd.cpp 4120-4134(VehicleEnterTile_Station 中 GOTO_WAYPOINT+Reverse → 设
    reverse_distance = cached_total_length,wait timetable 则 MakeWaiting);
  - ReverseTrainDirection(train_cmd.cpp 3070)/ reverse_distance 机制处理倒车。
- **结论:OT_GOTO_WAYPOINT 部分无需新功能**,实现时验证(必要时微调)即可,或向玩家说明已支持。

### 2.2 OT_GOTO_STATION 无此功能 → 本次主体
设计(执行语义):列车以该 station order 停靠(停稳、进 loading)后,自动把整列调向
(引擎换端、头尾交换),再以反向驶出平台继续后续订单(GOTO_COUPLE)。
这与玩家在停站列车上按"反转"命令(CmdReverseTrainDirection)完全等价。

### 2.3 存储位(零存档风险方案)
- **不碰 flags**:station order flags bit0-6=unload/wait-tt/load 已占用,bit7=travel-tt;
  存档 flags 仅 8bit(SLE_FILE_U8)。
- **不用 type 字节**:低5位 type、4-5 stop-location、6-7 non-stop(9位已超界,现有 order 混合使用)。
- **用 OrderExtraInfo::xdata2 bit0**(现有字段!):
  - xdata2 已序列化(sl/order_sl.cpp 144 行,依赖 XSLFI_ORDER_EXTRA_DATA version>=3,极老 feature,
    当前代码/存档均满足 → 读写零新增、向后兼容);
  - 注意 OT_CONDITIONAL 用 GetXData2Low 存 condition station(id-1),但那是 conditional 类型;
    **访问器按 IsType(OT_GOTO_STATION) 路由即可,不冲突**;
  - GetXData2Ref/SetXData2Low 自动 AllocExtraInfo(CheckExtraInfoAlloced),分配行为与
    GOTO_COUPLE 的 SetCoupleSlot 先例一致。
- 计划 API(order_base.h,仿 GetCoupleSlot/SetCoupleSlot 注释风格):
  `HasReverseAtStation()`/`SetReverseAtStation(bool)`;实现 = xdata2 bit0;仅在
  OT_GOTO_STATION 时有效。

### 2.4 执行链挂点
- 玩家反转命令核心逻辑(train_cmd.cpp CmdReverseTrainDirection 3344;非 single 分支 3379-3405):
  - loading 中且 moving_front/moving_back 不同站或 BeyondPlatformEnd → LeaveStation;
  - 加速度模型非 ORIGINAL 且 cur_speed!=0 → Flip(VehicleRailFlag::Reversing)(7924 在 speed0 时
    ReverseTrainDirection);
  - 否则 cur_speed=0 → ReverseTrainDirection(v)(3070,station 内同站停靠时**原地换端**,不 LeaveStation)。
- **自动触发点:Vehicle::BeginLoading(vehicle.cpp 3413)**:列车到站停稳进入 loading 前/后
  (current_order.GOTO_STATION→MakeLoading(true),3425-3438)。若当前 real/current order 是
  OT_GOTO_STATION 且目标==last_station_visited 且 HasReverseAtStation() → 复刻 3398-3405 逻辑
  调向一次(BeginLoading 每站仅一次,不会反复反转)。
- 兜底报错:若该停靠由"意外/implicit order"进入(else 支 3443 起)或反转前提不满足
  (例如 train 正越过平台端),则向 owner 发新闻/错误并保持原行为(人工介入)。
- 失败若来自空间/位置:不做任何重试(用户选定:报错人工介入)。

## 1.5 追加:第三次尝试——机车与车组一起掉头(2026-09-04,用户拍板)

- 需求:机车停在车组**前方**且同向(如从北边驶来、头朝南、鼻顶朝南车组尾部),
  要求不绕路、不单独调头即可挂上。代码原两次尝试(硬拼 v_last↔u_head、只翻 u)
  在这种几何下必折叠:机车的链尾与车组的链头隔着整列车组,翻过 u 后 u 头到了
  机车侧,却与仍朝南的 v 链尾方向相反、位置不对。
- 方案(已在 `train_cmd.cpp TryTrainCouple` 实现,第三次尝试):
  只翻 u 仍折叠时,把 **v 链和 u 链一起**各自 first/last 反转
  (`ReverseTrainSwapVehicles`,两链整列原地掉头),再拼 v_last↔u_head:
  掉头后机车回到整列最前端朝北(引擎仍居链首,链序不变),车组头也随之朝北,
  原“机车鼻顶车组尾”的相邻点恰好变成“机车链尾顶车组链头”,两链同向、一次拼成。
- 折叠日志区分:`COUPLE`(attempt1)/ `COUPLE-FLIP`(attempt2 翻 u)/ `COUPLE-FLIP-BOTH`(attempt3 两链都翻)。
- 失败恢复:任意后续失败(CheckTrainAttachment 等)须把 v/u 都翻回原状再报错。

### 2.5 order GUI(order_gui.cpp,待实现,下一大步)
- station order detail 现有底排按钮:WID_O_NON_STOP / WID_O_FULL_LOAD / WID_O_UNLOAD /
  WID_O_REFIT_DROPDOWN(2380-2471);left_sel 等是 stacked 平面(DP_LEFT_LOAD 等)。
- waypoint 用 left_sel 平面 DP_LEFT_REVERSE 显示 WID_O_REVERSE。方案待定:
  (a) station detail 新平面(DP_LEFT_LOAD_REVERSE:load 按钮 + reverse 按钮)太长;
  (b) 给 station order 行**新增 WID_O_REVERSE 按钮位**(改动 NWidget 布局);
  (c) 复用现有某按钮区域切换(DropDown)。
  倾向 (b):在 SetupOrderWidgets 布局加按钮,station detail 时启用。
- 显示:DrawOrderLine(1269-1282)加图标/文字;vehicle_gui 状态行暂不改。
- 添加 station/waypoint order 的下拉(O_DD_* / ODDI_*)无需动(标志在 detail 编辑)。
- MOF:新增 MOF_REVERSE_AT_STATION(order_cmd.cpp CmdModifyOrder 内,仿 MOF_WAYPOINT_FLAGS)。

### 2.6 本次改动文件清单
| 文件 | 改动 |
|---|---|
| order_base.h | Order::HasReverseAtStation/SetReverseAtStation(xdata2 bit0) |
| order_cmd.cpp | CmdModifyOrder 加 MOF 开关(改 order 校验不加新限制;MakeGoToStation 不变) |
| order_gui.cpp | station detail 反转按钮(UI)+ DrawOrderLine 图标 |
| vehicle.cpp | BeginLoading 内自动调向 hook(机车停稳) |
| train_cmd.cpp | (如需要)helper:反转执行复用 ReverseTrainDirection;错误 news 字符串 |
| lang/english.txt, simplified_chinese.txt | STR_ORDER_REVERSE_AT_STATION 等 |

## 3. 下一步
1. (本期做)order_base.h 存取器 + BeginLoading hook + MOF/CmdModifyOrder + GUI 按钮 + lang + 编译。
2. 验证:station order + ReverseAtStation → 机车到站自动调头反向驶出;再接 GOTO_COUPLE 挂接无 db。
3. waypoint 现成功能仅验证/微调;全链路跑通后向用户说明 waypoint 用法(WID_O_REVERSE)。
