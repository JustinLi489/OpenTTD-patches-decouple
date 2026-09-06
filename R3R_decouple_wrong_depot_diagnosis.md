# R3R 异常解挂诊断备忘(2026-09-04)

## 现象
复测确认:解挂仍在 42,28 发生,而非预期的 38,27。
附带:解挂后机车回 38,27 库执行 GOTO_COUPLE 找不到车底(车底留在 42,28) → COUPLE-FAIL 死循环刷屏。

## 日志证据(build/R3R_debug.log)
- 6207 `COUPLE-OK loco=0 real=4` @39,31:机车挂上车底,real 继承车底 idx4(GOTO_DEPOT)
- 6217 `DEPOT-ARR real=4(2)` @39,31 destTx=-1:耦合后瞬间,列车执行 idx4(GOTO_DEPOT→42,28)
- 6222 `DEPOT-ARR real=5(2)` @42,28 destTx=42 destTy=28 tileEqDest=1:列车完成 idx4 进 42,28 库,real 已推进到 idx5(GOTO_DEPOT→38,27),但 dest_tile 仍残留 42,28、车还停在 42,28 depot tile
- 6226 `DECOUPLE-FIRE real=5` @42,28:解挂!(错)
- 6252-6330:机车回 38,27,SKIP-STOPPED + COUPLE-FAIL 死循环

## 根因
train_cmd.cpp TrainLocoHandler 解挂判据(约 8306-8338):

```cpp
} else if (cur_real != nullptr && cur_real->IsType(OT_GOTO_DEPOT)) {
    at_order_dest = IsRailDepotTile(consist->tile);   // 只判"身在 depot tile"
}
```

列车在前一 GOTO_DEPOT(命令5→42,28)进库停靠后,real 已 advance 到下一
GOTO_DEPOT(命令6→38,27),但尚未驶出 42,28 库。此时 cur_real=idx5(GOTO_DEPOT→38,27),
车 tile=42,28(depot tile)。`IsRailDepotTile(42,28)=true` → 误判"已到达 idx5 目的地";
又因 idx6=OT_DECOUPLE → DECOUPLE-FIRE 在 42,28 提前解挂。列车根本没机会驶往 38,27。

补充:6222 的 destTx=42 是 idx4(42,28)进库后的残留 dest_tile,不代表 idx5 目标是 42,28;
从 39,31 耦合点到 42,28 只有一次行程(即 idx4),若 idx5 目标也是 42,28 则与用户排程
(命令6=38,27)矛盾——因此确认是判据缺陷而非排程错误。

## 修复(2026-09-04)
GOTO_DEPOT 分支改为**目标 depot 匹配**,参照 4517 行的既有用法
`GetDestination().ToDepotID() == GetDepotIndex(tile)`:

```cpp
} else if (cur_real != nullptr && cur_real->IsType(OT_GOTO_DEPOT)) {
    /* R3R: 必须停在本订单真正的目标 depot 上。列车完成上一 GOTO_DEPOT 进库后
     * real 已推进到下一 GOTO_DEPOT,但仍停在旧库 tile(尚未驶出);仅判
     * IsRailDepotTile 会把旧库误当新订单目的地,导致在错误 tile 提前解挂
     * (2026-09-04 实测:DECOUPLE-FIRE 在 42,28,而 cur_real 是 GOTO_DEPOT→38,27)。*/
    at_order_dest = IsRailDepotTile(consist->tile) &&
        cur_real->GetDestination().ToDepotID() == GetDepotIndex(consist->tile);
}
```

## 修复后预期时序
1. 列车拖车底执行 idx4 GOTO_DEPOT→42,28,进库停靠,real→idx5(命令6 GOTO_DEPOT→38,27)
2. 仍在 42,28 tile:目标 depot(38,27)≠当前 depot(42,28) → 不解挂
3. 列车驶出 42,28 → 38,27,进库停靠,tile=38,27==目标 → at_order_dest=true
4. next(idx6)=OT_DECOUPLE → DECOUPLE-FIRE @38,27(正确)
5. 机车恢复自己订单 idx4 GOTO_DEPOT→38,27 回家,车底留在 38,27 → GOTO_COUPLE 命中

## 待办
- [ ] vcvars64 环境下重编 openttd.exe
- [ ] 复测,盯 DECOUPLE-FIRE 是否改在 38,27
- [ ] 观察是否还有 COUPLE-FAIL 死循环
