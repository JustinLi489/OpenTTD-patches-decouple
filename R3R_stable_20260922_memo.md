# R3R 换挂（couple / decouple）实验收口备忘 —— 稳定版 2026-09-22

> 用途：把"2026-09-22 换挂实验成功"这件事固化下来 —— 冻结了哪个版本、修了哪几条、哪些还没验、
> 下一步的候选方向。配套清单：`R3R_KNOWN_ISSUES.md`（KI-117 / KI-162 / KI-163 / KI-164 / KI-165）。
> 快照目录：`build\r3r_stable_2026-09-22\`（README.txt 在目录内，含回退步骤）。

---

## 1. 一句话结论

**挂车（GOTO_COUPLE）→ 行车 → 解挂（DECOUPLE）全流程在游戏内跑通，用户 2026-09-22 确认
"整个换挂的实验很成功"。** 该状态已冻结为 `r3r-stable-2026-09-22`（git tag + `build\` 快照 + exe）。

## 2. 冻结了什么

| 项 | 值 |
|---|---|
| git commit | `1ce54c475f1615f579e0a9b850145180643bda6d`（2026-09-22 23:16:24 +0800，49 files / +5222 −664） |
| git tag | `r3r-stable-2026-09-22`（分支 `feature/decouple`，附注标签） |
| 上一稳定版 | `eac73849b7` = tag `r3r-stable-2026-09-19` |
| 快照目录 | `build\r3r_stable_2026-09-22\`（`src\` 41 个改动文件 + `openttd.exe` + `README.txt`） |
| exe | `build\openttd.exe` @ 2026-09-22 21:52:43（50 791 936 B，内测版 Debug，探针默认 ON） |
| 二进制可信度 | `build\R3R_fullbuild.done = EXIT_CODE=0` @ 09-22 04:37:01，**620/620 obj 全部晚于最新头文件**（陈旧 0）→ 非混合陈旧二进制（KI-117 由此闭环） |

> 本轮快照放的是 **`build\` 内测版**（不是 `build-release\` 发行版）：实验就是在它上面做的。
> 需要发行版时按 `R3R_fullrebuild.cmd` 选 [1] 单独编。

## 3. 这一轮把换挂修到能用的四条

按缺陷链顺序（上游 → 下游），四条都属"同一条挂/解挂链路上的连续硬伤"：

| KI | 一句话 | 落点 | 状态 |
|---|---|---|---|
| KI-162（第 91/92 轮） | `CPL-SAFE-FAIL fail=tryReserve` / `CPL-RESERVE` 后车不动 | 判定为**正常行车冲突**（占道车离开后自动预留成功）；机制非缺陷 | 已核实非缺陷 |
| KI-163 | `reach()`/`rp()` 轴向盲扫 32 格 → 预留开向**错误方向** | `yapf_rail.cpp` 新增 `R3RSkippedStationStretchContains()`，按 `TrackFollower` 真实跳段（`exitdir` + `tiles_skipped+1`）重建被跳过的站台段 | 已修（游戏内已见好转） |
| KI-164 | `OT_GOTO_STATION` 到达判定只看"是不是车站地块" → 在**另一座车站**的耦合点被判"已到达" → 挂上即解挂 | `train_cmd.cpp` 解挂钩子前改按 `StationID` 比较（与 `OT_GOTO_DEPOT` 分支对称） | 已修（游戏内已验证） |
| KI-165 | 候选解析**从不读订单目的地** → 机车开去"没有挂车命令"的另一座车站抢车底 | 目的地规则并入唯一许可函数 `R3RCoupleAllowed()`（`couple_group.cpp`），三层解析（寻路终点判定 / FSCP 回溯安全 / 到达闸门）共同遵守 | 已修（游戏内已验证） |

**KI-165 的设计要点（值得记住）**：三处解析过去各自为政，所以"寻路规划的目标"与"最终执行的耦合"
可能不一致。这次把目的地收进 `R3RCoupleAllowed()` 这一个函数，等价于给整条链路加了一道单一权威闸门；
`train_cmd.cpp` 里的 `R3RCoupleTargetAtOrderStation()` 只用来**取日志标签**（`dest-mismatch` vs
`group-mismatch`），行为一律由前者裁定。

## 4. 仍未验 / 有意不做的（别当成已完成）

1. **KI-165 残留四项**（明细见 `R3R_KNOWN_ISSUES.md` KI-165 末段）：
   - (a) `CPL-HIT` 两条碰撞式耦合兜底路径（`site=depot` / `site=open`）**故意不加过滤** ——
     加了会把"挂错站"变成撞车；
   - (b) 目的地车站当时**没有车底**时，机车只会原地等（`COUPLE-FAIL`），不会"先开到目的站台再等"；
     要做需在寻路层支持"无车底的合法终点"，属独立改造；
   - (c) 过滤器看候选**链头**所在格，车底恰好横跨两座相邻车站时可能误判（罕见）；
   - (d) 老存档里目的地为 `INVALID` 车站的 `GOTO_COUPLE` 将永远挂不上车站候选（UI 无法产生该形态）。
2. **KI-162 的"应有现象未验证"**：长时不动是否已被 KI-163 修复彻底压制，需要在同场景重测才能定性
   （KI-163 只解释了其中"预留方向错"那一部分）。
3. **KI-163 / KI-164 的逐条判据未单独复刻**：本轮的状态更新依据是用户对"整个换挂实验"的整体确认，
   如后续出现反例，按各自条目里的"复测判据"逐条抓日志即可。
4. **发行版未同步**：`build-release\` 仍是 09-19 07:40 那版（22 707 712 B），不含 KI-163/164/165 的修复；
   要发 release 必须先 `R3R_fullrebuild.cmd` 选 [1] 全量重编（KI-15）。
5. **探针仍在内测版里**（`build\` = 探针 ON）：跑功能复测时建议带 `R3R_DBG=0`，否则白吃约 5 ms/帧（KI-17/KI-26）。

## 5. 下一步候选（等用户拍板，未动手）

- **A. 继续换挂路线的收尾**：把 KI-165 残留 (b)（"目的地无车底也允许开过去等"）做掉 ——
  这是玩家视角最容易再遇到的下一步：现在"车底被别的机车拉走了"→ 机车只能原地等。
- **B. 换挂的排程归属**（老账，`R3R_multi_couple_decouple_orders_memo.md` §10/§11）：
  `DecoupleTrain()` 的"排程跟谁走"目前是单向默认（挂车拿车组排程、解挂还机车排程），做不出
  T8701 那种"两次解挂、归属相反"的多阶段场景；正解是接线 `ODOF_*`（订单逐次声明）+
  把 `orders_backup` 从单层升级为栈。属结构性改造。
- **C. 回到性能支线**（已搁置）：口径 A 已降到约 1.25×，若要冲 1.1× 需再砍约 1.4 ms/帧。
- **D. 其他已知未修项**：以 `R3R_KNOWN_ISSUES.md` 里"状态 ≠ 已修"的条目为准（第 47/71 轮残留、
  KI-156/157/158 的目测项等）。

## 6. 回退速查

```cmd
:: 1) 目录回退（推荐，不依赖 git）
xcopy /E /Y "d:\sourcecode of JGRPP\build\r3r_stable_2026-09-22\src\*" "d:\sourcecode of JGRPP\src\"
:: 2) 纯源码回退（会丢弃该提交之后的未提交改动）
git -C "d:\sourcecode of JGRPP" checkout r3r-stable-2026-09-22 -- src
:: 3) 只换 exe（回到本版本的运行行为）
copy /Y "d:\sourcecode of JGRPP\build\r3r_stable_2026-09-22\openttd.exe" "d:\sourcecode of JGRPP\build\openttd.exe"
```

- 改 `src\*.h` ⇒ 必须全量重编（删全部 `*.obj`，走 `R3R_fullrebuild.cmd`）；只改 `.cpp` 可增量。见 KI-15/KI-16。
- 本机 ninja 对头文件依赖跟踪失效的坑，见记忆 66636022 与 KI-117。
