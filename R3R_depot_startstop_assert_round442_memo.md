# 第 442 轮：`vehicle_gui.cpp:4346 assert(v->IsPrimaryVehicle())` 崩溃定位

## 一、现场

- 玩家 2026-11-14 原话：「还是 vehicle_gui.cpp 4346行的断言崩溃，这是为什么」。
- 「还是」= 同一行重复崩（此前轮次也咬过，但本文件与 `R3R_KNOWN_ISSUES.md` 里**没有**该行的条目 ⇒ 之前的修复没落盘，属漏记）。
- 崩溃断言（`src\vehicle_gui.cpp:4341-4348`）：

```4341:4348:src/vehicle_gui.cpp
void StartStopVehicle(const Vehicle *v, bool texteffect)
{
	/* R3R: car-only consists (free-wagon chains, no engine) cannot be started/stopped by themselves. */
	if (v->type == VehicleType::Train && Train::From(v)->IsFreeWagon()) return;

	assert(v->IsPrimaryVehicle());
	Command<Commands::StartStopVehicle>::Post(...);
}
```

- 判据：`Train::IsPrimaryVehicle()` = `IsFrontEngine() || IsFrontWagon()`（`src\train.h:341`）。断言成立 ⇒ 传进来的车**既不是前引擎也不是前自由车厢**，也就是「链中车 / 被降级的旧链头」。

## 二、三个调用点及守卫差距（全树仅 3 处）

| # | 调用点 | 传入对象 | 现有守卫 | 结论 |
|---|---|---|---|---|
| 1 | `vehicle_gui.cpp:4890`（车辆视窗 `WID_VV_START_STOP`） | `Vehicle::Get(this->window_number)` —— **窗口号恒等于该 VehicleID** | 无 | ❌ 窗口若指向已降级的车 ⇒ 必崩 |
| 2 | `depot_gui.cpp:1165`（车库行首启停旗） | `result.vehicle`（`vehicle_list[]` / `wagon_list[]` 的**构建时快照**） | 无 | ❌ 列表陈旧 ⇒ 可能非链头 |
| 3 | `viewport.cpp:5574`（Ctrl+点击地图车） | `v = v->First()` 后再传 | 取了链头 | ⚠️ 链头理论上 primary，但 `IsFreeWagon` 才被 4344 挡；链头若不是 FrontEngine/FrontWagon 仍崩 |

**关键：同一文件里 `ShowVehicle` 分支早就加过两道守卫，`StartStop` 分支没加** —— 说明「depot 列表会给出非链头车」是 R3R 已承认的既有事实（`depot_gui.cpp:1153-1158` 注释原文：*"the depot lists can hand us a vehicle which is not the head of its chain (a single wagon of a loose wagon chain, or a list entry generated before the vehicle got coupled into a longer chain)"*），只是启停分支漏了同一套守卫：

```1152:1166:src/depot_gui.cpp
			case DepotGUIAction::ShowVehicle: // show info window
				if (result.vehicle->First() != result.vehicle) break;
				if (result.vehicle->type == VehicleType::Train && Train::From(result.vehicle)->IsFreeWagon()) break;
				ShowVehicleViewWindow(result.vehicle);
				break;

			case DepotGUIAction::StartStop: // click start/stop flag
				StartStopVehicle(result.vehicle, false);
				break;
```

## 三、根因（分两层，第 1 层是本轮 C-2 直接引入）

### 3.1 治根：C-2 反序把「旧链头」降级，但没关它的车辆窗口

`src\train_cmd.cpp` 第 442 轮 C-2 的 ⑤ 步（`~5971-6002`）：

```5988:5997:src/train_cmd.cpp
						R3RRelocateFrontIdentity(old_head, new_head, false);
						if (old_was_car_only) {
							old_head->ClearFrontEngine();
							old_head->ClearEngine();
							old_head->SetWagon();
						}
```

- `R3RRelocateFrontIdentity()` 内部把 `GVSF_FRONT` 从 `old_head` 搬到 `new_head` ⇒ **`old_head->IsPrimaryVehicle()` 立刻变 false**。
- 而 `old_head` 上可能还开着 `WindowClass::VehicleView` / `Orders` / `Details` / `Refit` / `Timetable`，这些窗口的 `window_number` 就是旧 `VehicleID`，**关窗只发生在 `Vehicle::PreDestructor()`（`src\vehicle.cpp:1215+`）且只在 `IsPrimaryVehicle()` 为真时**（`src\vehicle.cpp` 那段 `if (v->IsPrimaryVehicle()) CloseWindowById(...)`）。`old_head` 没被销毁 ⇒ 窗口全部存活 ⇒ 玩家点视窗里的启停按钮 ⇒ `Vehicle::Get(window_number)` 拿到已降级车 ⇒ **4346 断言**。
- 这正是既有教训的复现（记忆 38203048 / KI 关于 `R3RDestroyCarOnlyFormation` 的那轮）：**R3R 任何"取消 primary 身份"的路径都必须同步关闭该车的 VehicleView 等窗口**。C-2 是本轮新加的取消 primary 路径，漏了这一步。

### 3.2 放大面：车库列表是构建时快照

`DepotClick()` 用的 `this->vehicle_list[pos]` / `wagon_list[pos]` 在窗口 `OnInit/OnPaint` 时由 `BuildDepotVehicleList()` 填一次（Train 存 `v->First()`）。C-2 / 耦合 / 解挂 / 车库拖动都会在**不重建列表**的情况下改变链结构 ⇒ 表中的 `Vehicle*` 指针仍然有效，但 `First()` 已经换人 ⇒ 点行首启停旗时 `IsPrimaryVehicle()` 为假。
（`ShowVehicle` 分支的两道守卫就是为这条路径加的，启停分支漏了。）

## 四、修法（三层：治根 + 兜底 + 入口守卫）

1. **治根（`src\train_cmd.cpp`，C-2 反序分支）**：`R3RRelocateFrontIdentity(old_head, new_head, false)` 之后、降级 `old_head` 之前，关掉旧链头的五个车辆窗口 —— 复用 `R3RDestroyCarOnlyFormation()` 里现成的写法（`CloseWindowById(WindowClass::VehicleView/Orders/Refit/Details/Timetable, old_head->index)`），并打 `ARTIC-FLIP-CLOSE old=` 探针。
   - 注意：**只在真的换头时关**（`before == nullptr` 且 `old_head->IsFrontEngine()`），组夹在链中间时 identity 不动，不许误关玩家窗口。
2. **兜底（`src\vehicle_gui.cpp:4346`）**：把 `assert(v->IsPrimaryVehicle())` 换成「非 primary 直接静默 return + 一次性探针」。理由：R3R 的链编辑会让 UI 侧的 VehicleID 引用陈旧，这是**合法存在**的中间态（`depot_gui.cpp:1153` 注释已承认），不该让游戏崩；探针 `STARTSTOP-SKIP veh= idx= type= front= fw= fwe=` 用于抓残余路径。
3. **入口守卫（`src\depot_gui.cpp:1164`）**：启停分支改为「解析到链头再操作」——`result.vehicle->First()` 取链头，Train 且 `IsFreeWagon()` 时 break（与 `ShowVehicle` 分支同款），这样点行首旗子作用于真实链头，行为符合直觉而不是"点了没反应"。

## 五、待确认（U 项）

- **U-1**：崩溃的确切入口（车辆视窗 vs 车库行首旗）需玩家下一次崩溃的 `crash-*.log` 命令栈：若栈里是 `VehicleViewWindow::OnClick` 则是 3.1 主因，若是 `DepotWindow::DepotClick` 则是 3.2 主因。本轮三层都修，故不阻塞复测。
- **U-2**：C-2 反序后 `ARTIC-FLIP ... primary=?` 探针是否曾出现过 `primary=0`。若出现 ⇒ 说明还有 identity 迁移没盖到的分支（如 `old_was_car_only` 之外的反序）。
- **U-3**：`viewport.cpp:5574` 的 `v = v->First()` 后未判 primary，本轮**不单独加**守卫（兜底已覆盖），若后续探针报 `type=Train front=0 fw=0` 且来自 viewport，再补。

## 六、复测判据

1. 车库内选中铰接组反转后，**立刻点该组任一行的启停旗**：不崩；操作用在真正的链头（日志 `ARTIC-FLIP ... primary=1`，新链头报「已停止/已启动」文字特效）。
2. 反转前把旧链头的车辆视窗打开，反转后该视窗**自动消失**（`ARTIC-FLIP-CLOSE old=N`），不再可点启停。
3. 负判据：组夹在链中间反转（`move=1`）时，**不得**出现 `ARTIC-FLIP-CLOSE`（玩家窗口不许被误关）。
4. 负判据：正常车库点启停旗/车辆视窗点启停，行为与文字特效逐字不变，日志无 `STARTSTOP-SKIP`。
5. 兜底路径自证：若仍出现 `STARTSTOP-SKIP`，说明存在未修的陈旧引用路径，需把那行日志连同 `veh=/idx=` 一起回传。

## 七、落地与构建自证（2026-11-14 第 442 轮）

- `src\depot_gui.cpp:1164-1169`：`DepotGUIAction::StartStop` 改为 `StartStopVehicle(result.vehicle->First(), false);` + 6 行注释说明"与 ShowVehicle 同一类陈旧列表问题"。
- `src\vehicle_gui.cpp:4344-4357`：`assert(v->IsPrimaryVehicle());` → `if (!v->IsPrimaryVehicle()) return;` + 9 行注释。
- 实际只落了 2 层（治根在 `R3RRelocateFrontIdentity` 内部已自带 `CloseWindowById` 五窗口，见 `train_cmd.cpp:11936-11944`，故 C-2 侧无需再改）。
- 构建（`R3R_fullbuild.cmd inc`，复用既有入口，未新建 `.cmd`）：
  - `GUARD: incremental is safe (no header/lang file is newer than the newest object)`
  - `[2/4] Building CXX object ... depot_gui.cpp.obj` → `[3/4] ... vehicle_gui.cpp.obj` → `[4/4] Linking CXX executable openttd.exe`
  - 日志尾 `==== R3R build ended 2026/11/14 3:53:45 MODE=inc EXIT_CODE=0`
  - `src\depot_gui.cpp` 03:45:40 → obj 03:51:36；`src\vehicle_gui.cpp` 03:45:47 → obj 03:51:37；`build\openttd.exe` 03:53:29（52 886 528 B）；`build\R3R_incbuild.done` 03:53:45 `EXIT_CODE=0`
  - `read_lints` 两文件 0 条
- 构建期踩坑：`Start-Process ... 'inc detach'` 本轮没留下进程（无 ninja、`guard.log` 停在 03:35），且前一次前台构建残留的 `Press any key` cmd 会锁住 `build\R3R_incbuild.log`/`.done`，让二次启动打出「另一个程序正在使用此文件」并误判 `ninja ended 0.24 秒 / up to date`。可靠流程 = 前台 `cmd /c R3R_fullbuild.cmd inc` → 结束后 `Stop-Process` 掉等待按键的 cmd → 用日志尾 + `[n/n] Linking` + obj/exe 时间戳三件套核对。

