# R3R 耦合崩溃(C0000094 除零)诊断对话总结

> 日期:2026-09-07 · 仓库:`d:\sourcecode of JGRPP`(JGRPP 魔改版 OpenTTD)·
> 分支:`feature/decouple`(领先 origin 5 个 commit)·
> 未提交改动:`src/train_cmd.cpp`、`src/vehicle_cmd.cpp`

---

## 一、用户请求(本轮唯一消息)

用户报告「**还是尝试耦合的时候崩溃的**」,并附两份文件:

- 崩溃日志:`C:/Users/冯洁敏/Documents/OpenTTD/crash-20260906T205803Z.log`
- 调试探针日志:`D:/sourcecode of JGRPP/build/R3R_debug.log`

即:上一轮修复(artic 角色位崩溃、折叠修正候选4 逻辑翻 v 等)已落地编译后,运行回归时再次在**列车尝试耦合(couple)** 的场景崩溃,属新崩溃现场。

---

## 二、崩溃现象

- **异常码**:`C0000094`(整数除零)——注意与上一轮的"读 0 地址"(空指针)不同。
- **崩溃点**:`GroundVehicle<Train,0>::GetAcceleration + 1760`,`ground_vehicle.cpp:282`。
- **调用栈**:`GetAcceleration → Train::UpdateSpeed(6711) → TrainLocoHandler(9137) → Train::Tick(9268) → CallVehicleTicks(1679)`。
- **崩溃上下文**:`veh: 0: (Train [N/A], st:E, vs:D)`、`front: 2: (Train 2, st:FE, vs:D)`。
  - 关键点:崩溃车辆 veh0 已经**不再是前端**(无 FE),整列合并列车真正的 FrontEngine 是 **veh2**;但 veh0 仍以旧前端身份走完了含 `UpdateSpeed` 的前端路径。

---

## 三、关键代码定位(本崩溃涉及)

### 3.1 `src/ground_vehicle.cpp`

- 崩溃行(已验证,与诊断一致):

```
282: braking_accel = ClampTo<int32_t>(std::min<int64_t>(-braking_force - resistance, -10000) / mass);
```

- 160 行:`int64_t mass = this->gcache.cached_weight;` —— **mass 来源是重量缓存**。
- 104 行 `CargoChanged()`:仅当 `First() == this`(即对象自己是前端)才执行重量重算;139 行 `cached_weight = max(1u, weight)`。
  → **非前端对象一旦被清了缓存,`cached_weight` 就是 0**,后续任何对它调 `GetAcceleration` 都会除零。

### 3.2 `src/train_cmd.cpp`

- `ConsistChanged()`(约 287):以 `CCF_LENGTH` 调用时会清除 **除 this 外所有车辆** 的 weight / power / max_speed 缓存(477–495)——半保存状态,**只有前端存活**。
- 耦合成功后 `NormaliseTrainHead(v)` → `v->ConsistChanged(CCF_ARRANGE)`(约 4802),此时 `v` 已指向新链头,理论上会正确刷新全链缓存。
- 前端身份迁移(已验证,4621–4629):

```
4621: /* R3R 换端重排:逻辑翻 v 后合并链头可能不再是原机车对象 v(head != v,
4622:  * 仅发生在"多节真引擎 v 翻 v 后新链头 = 原链尾引擎"且成功拼合的路径)。
... */
4626: if (head != v) {
4627:     R3RRelocateFrontIdentity(v, head);
4628:     v = head;
4629: }
```

  → 原机车对象 veh0 降为链内普通引擎,F E 身份迁到 veh2(原链尾引擎)。
- 相关函数:耦合检测 `GetCouplePosition`、`TrainCoupleHandler`(4876 起)、`Couple`(~4247)、`TryTrainCouple`(~4381,折叠修正/翻 v 逻辑)、`R3RRelocateFrontIdentity`(~4324)、`R3RFlipChainBySegments`(段序保持的逐段翻转)。

---

## 四、诊断推理过程

1. **mass=0 是直接原因**:282 行分母为 `gcache.cached_weight`,而 veh0 此刻非前端且缓存被清(或被清后从未重建)。
2. **为什么 veh0 还能走到 GetAcceleration**:`Train::Tick` 的前端分支由 `IsFrontEngine()` 门控;若 veh0 在 Tick 开始时仍是前端、**但在 Tick 执行途中**(同一函数栈内)发生了耦合且把前端身份迁走,则函数不会重新检查,继续以旧 `consist=veh0` 参数跑到 `TrainLocoHandler` 9137 的 `UpdateSpeed` → `GetAcceleration`,此时 veh0 已非前端、缓存已被 `CCF_LENGTH` 清空 → **除零**。
3. **与 tick 循环的关系**:`CallVehicleTicks`(1679)只遍历"前端缓存"列表;本次崩溃不是 cache 过期后重复 tick 旧前端(那样 veh0 会走 else 分支、不进 `TrainLocoHandler`),而是**耦合在 veh0 自身 handler 调用栈内发生并 re-head**,函数未早退所致。
4. **时序模型**:
   - 耦合 handler 需要 `cur_speed == 0`(静止),但 `UpdateSpeed` 只在未早停(8781–8793 守卫之外、`cur_speed>0` 或非 Stopped)时才执行——两者通过"本次 handler 先把 cur_speed 置 0、随后同函数内 mode 续跑"衔接。
   - 候选路径:mode=false 首次调用(8798–8815 出库自动耦合等)内完成 coupling 后未早退,继续跑到 9137。
5. **日志佐证**(R3R_debug.log):
   - `FOLDCHK COUPLE n=9 idxA=2 idxB=3`、`A3-BOTH-FLIP-DONE merged_head=8`;
   - `COUPLE-OK loco=2 rear=3`;CPL 链序 `2-1-0-8-7-6-5-4-3` → 合并链头=veh2,veh0 位于第 3 位;
   - 与崩溃现场 `front=2`、`veh=0` 完全吻合:veh0 在**同一次 tick** 内被自己的耦合操作降级。

---

## 五、根因(定论)

> **TryTrainCouple 成功路径的"前端身份迁移"(`head != v` → `R3RRelocateFrontIdentity(v, head); v = head;`,由候选4/换端重排引入)使得"正在 tick、正在执行 GOTO_COUPLE 的机车 veh0"不再是链头;`TrainLocoHandler` / `Train::Tick` 仍携带旧参数 `consist=veh0` 继续执行到 `UpdateSpeed → GetAcceleration`;veh0 已被 `ConsistChanged(CCF_LENGTH)` 清空缓存、又因非前端无法重算 `cached_weight` → mass=0 → 整数除零崩溃。**

本质矛盾:
- 原版 OpenTTD 假设「耦合时,被耦合的两列车各自 tick,而**正在 tick 的前端永远是 order-front**,绝不会在自身 handler 中途被 re-head」;
- R3R 的换端重排(候选4 逻辑翻 v + P2 身份迁往新链头,用户已拍板)打破了该假设:机车 v 逻辑翻转后链头=原链尾,`R3RRelocateFrontIdentity` 把 primary 身份迁走,旧机车在**自己 tick 的中途**变成非前端 → 与 tick 循环的不变量冲突。

---

## 六、修复方向(已提出,待拍板/实现)

- **方案 A(最小侵入,推荐方向)**:耦合成功且 `head != v`(发生身份迁移)时,**禁止旧前端对象的当前 tick 继续以活动前端语义跑完 `TrainLocoHandler`**:
  - 在 Couple/TrainCoupleHandler 返回路径检测 `!IsFrontEngine() || First() != consist`,命中即安全早退(收尾 cur_speed、状态清理),把后续 tick 交给新链头 veh2;
  - 或把本次 tick 的剩余工作转交/重发给新前端。
- **方案 B**:耦合成功后立即 `ConsistChanged(CCF_ARRANGE)` 全链刷新并让旧对象走普通车厢分支(但其栈帧已深入 handler,仍需早退配合)。
- **方案 C(已否定,勿用)**:放弃身份迁移、保持 v 为链头——与用户拍板的 P2(候选4 逻辑翻 v 后身份迁往新链头)直接冲突。
- **补充排查项**:核 `TrainLocoHandler` 早停守卫(8781–8793)与 handler 时序——为何 `cur_speed==0` 耦合后还能同函数进入 `UpdateSpeed`,确认 mode=false/true 的早退点是否齐全。
- **注意**:不要只给 `GetAcceleration` 加 `mass==0` 防护掩盖症状;那只是止血,真正的语义 bug 是"已非前端的对象继续走前端加速路径"。

---

## 七、相关技术背景(历史记忆/决策)

- **候选4 决策(记忆 72616043)**:P1 直接实现候选4(翻 v 先于物理 SWAP);P2 身份一律归链头(逻辑翻 v 后原链尾成新链头,身份迁往新链头);P3 候选4 先、候选2 后;P4 `v_flipped` 拆分(逻辑翻 vs 物理 SWAP)防双重镜像损坏;P5 artic 端点错位死循环不做专项;P6 force 换端路径弃用不纳入;P8 编译测试纪律。
- **翻 v 实现(记忆 30805247)**:`TryTrainCouple` 候选2/3;`head` 局部变量全程追踪;成功提交点 `head != v` 调 `R3RRelocateFrontIdentity(v, head)` 后 `v = head`。
- **段序保持翻转(记忆 41007633 / 97996690)**:`R3RFlipChainBySegments` 段序不颠倒、逐段 artic 块倒序重链;每段新首打 ★。
- **artic 角色位崩溃(记忆 14009696)**:上一轮已修复(与本次除零无关)。
- **编译纪律(记忆 66636022 / 52814982)**:改 `.cpp` 可增量;改任何 `src/*.h` 必须删全部 `*.obj` 全量重编(vcvars64 环境,597 文件约 25–40 分钟),否则混合布局二进制随机崩溃。
- **待办(记忆 17190446)**:不成段车厢链拒绝 couple/uncouple——已确认要做,安排后续对话实现。

---

## 八、待办任务

1. **[进行中] 修复本崩溃**:耦合(head!=v 身份迁移)后旧前端在自身 handler 栈内继续执行导致 mass=0 除零 —— 落地方案 A(迁移发生后安全早退/转交 tick),或与用户复核后定方案。
2. **编译验证**:本轮只改 `train_cmd.cpp` 等 `.cpp` 可增量编译;若触碰头文件须全量重编(记忆 66636022)。链接前确认 `openttd.exe` 未被占用(否则 LNK1168)。
3. **运行时场景回归**(须用户交互验证):多机车折叠修正 / 只翻 u / 双翻 / 本次"静止耦合 + 翻 v 迁移"场景;持续盯 `fold-geom` / `COUPLE` 探针与 `R3R_debug.log`。
4. **后续功能(记忆 17190446)**:不成段车厢链拒绝 couple/uncouple + 段升级时链尾加假引擎、降级时撤销。
5. **清理**:工作区根目录未跟踪的 `R3R_tmp_*.cmd/.log/.ps1` 临时文件(共 11 个)待删。

---

## 九、当前工作状态

- **分支**:`feature/decouple`,领先 `origin/feature/decouple` 5 个 commit(未 push)。
- **未提交改动**:`src/train_cmd.cpp`、`src/vehicle_cmd.cpp`。
- **崩溃产物**:`crash-20260906T205803Z.log`(用户目录)、`build/R3R_debug.log`。
- **本轮产出**:确认新崩溃类型(除零)与旧崩溃(空指针)同源不同面——都源于「R3R 翻 v / 身份迁移改变了链头,而 tick/耦合流程的"前端不变量"未同步更新」,本次将修复推进到"耦合成功后旧前端不得继续走加速路径"。
