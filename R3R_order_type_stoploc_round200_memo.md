# R3R 第 200 轮 临时分析报告：订单类型位域与停靠位置位域重叠（KI-302）、删除"前半/后半"残留 UI（KI-303）、特质借用/返还复核

日期：2026-10-02
现场日志：`build\R3R_debug.log`（356 825 B / 5 559 行，mtime 2026-10-02 17:54:12）
本轮改动范围：`src/order_base.h`、`src/order_cmd.cpp`、`src/sl/order_sl.cpp`、`src/sl/extended_ver_sl.h`、`src/sl/extended_ver_sl.cpp`、`src/order_gui.cpp`、`src/widgets/order_widget.h`

> **编号更正（同日）**：本备忘初稿把本轮两条写成 KI-300 / KI-301，与「第 198 轮 KI-300」「第 199 轮 KI-301」**撞号**；自本备忘起统一改为 **KI-302（订单类型位域与停靠位置重叠，高）/ KI-303（删除前半/后半残留 UI，低）**，`R3R_KNOWN_ISSUES.md` 的「第 200 轮」小节同用该编号。

---

## 一、作业单（玩家 2026-10-02 原话）

1. 「看来这边的问题可以暂时结案了……请你查看日志，检查我刚才报告的问题是否修好。」（特质借用/返还/恢复）
2. 「关于调度计划，我又发现了一些问题。第一个是关于前 R3R 时期借鉴朋友的 pxp 版本残留的"前半""后半"UI，这两个 UI 出现在解挂命令的选择解挂边界选项的旁边，我需要删除这两个 UI。」
3. 「第二个是，选择停靠在站台的远端近端，我手动在调度命令里更改它，会把命令变成等待挂接。」

流程要求：明确需求/现象/根因/修法后先写临时备忘，再执行下一步。

---

## 二、问题 1：特质借用 / 返还 / 恢复 —— 已结案

### 判据与证据

| 探针 | 行号 | 原文 | 判读 |
| --- | --- | --- | --- |
| `SEGTR-SNAP tag=load-raw/load` | 1–20 | `head=0 nseg=1 ctrl=0 borrowed=0` + `live u=1 n="T8701/2-国内段"` | 读档后每段一行、`row`/`live` 自洽，链头即控制段 |
| `NAME-XFER` | 263 | `head=24 u=0 head_own=1 got_u=1 keep_u=1 borrowed=0 own=1` | 链头**自己有名**（`head_own=1 own=1`），借入后仍保留自己的备份 |
| `CTRL-UNBORROW` | 882 | `decouple-v head=24 unit=3 own_bk=0 hasname=1 own_name=0 own_g=65534` | 解挂返触发脱下：名已归还活字段（`own_name=0` 是"备份已排空"），分组退回 Invalid |
| `CTRL-UNBORROW` | 1659 | `decouple-u head=6 unit=2 ... own_g=65534` | 解出方一侧同样脱下 |
| `NAME-RESTORE-SKIP-BORROW` | 1719 | `veh=27 u=6 borrowed=1 hasname=1 own_name=1` | 借用态下**不再重复归还**（第 199 轮修复的守卫） |
| `UNIT-RESTORE-DEFER` | 5532 | `veh=6 id=2` | 号取回推迟到 settle，未丢 |
| `CTRL-UNBORROW depot-edit` | 5533 / 5537 | `head=0 unit=1` / `head=6 unit=2` | 车库编辑两点也正确脱下 |

结论：**借用 / 返还 / 恢复三条边均正常，无名称或号丢失，无分组残留。问题 1 结案。**

---

## 三、问题 2：删除解挂命令旁的"前半""后半"UI（KI-303）

### 是什么

解挂订单（`OT_DECOUPLE`）被选中时，订单窗口顶行（`WID_O_SEL_TOP_YARD`）会切到 **plane 5**，里面是一排放两个 30px 下拉框：

- `WID_O_DECOUPLE_FIRST_ORDERS` → 标签 `STR_ORDER_DECOUPLE_FIRST_ORDERS`（"前半"）
- `WID_O_DECOUPLE_SECOND_ORDERS` → 标签 `STR_ORDER_DECOUPLE_SECOND_ORDERS`（"后半"）

它们紧挨着同一行里被改成"解挂边界"的 `WID_O_REFIT_DROPDOWN`（`order_gui.cpp:3033`），正是玩家说的"选择解挂边界选项的旁边"。

### 来源

`git show 3f8100c879 -- src/order_base.h` 显示 `decouple_first_orders` / `decouple_second_orders` 两个字段与 `ODOF_*` 枚举由 **pxp 提交 3f8100c879（2026-08-14，"Feature: Decouple and consist-group train features"）** 引入，玩家口述的"前 R3R 时期借鉴朋友的 pxp 版本残留"属实。

### 现状

这两个下拉框通过 `ModifyOrder(..., MOF_DECOUPLE_ORDERS, (first << 4) | second)` 写 `OrderExtraInfo`，再由 `train_cmd.cpp` 的解挂策略解析器（`~7521-7580`、`7771-7779`）决定"解出方 / 留下方各自继承哪一份排程"。

### 修法（本轮）

**只删 UI，不动数据与语义**（把风险面压到最小）：

1. `src/widgets/order_widget.h`：删 `WID_O_DECOUPLE_FIRST_ORDERS` / `WID_O_DECOUPLE_SECOND_ORDERS`。
2. `src/order_gui.cpp`：删 plane 5 的两个 NWidgetLeaf、删 `DP_YARD_DECOUPLE_ORDERS` 常量与切换分支、删两个下拉的点击/选中处理与 `SetStringTip`/回显代码、删对应 `ODDI_*` 下拉索引。
3. **保留** `MOF_DECOUPLE_ORDERS` 命令、`SetDecoupleFirstOrdersType/SecondOrdersType`、`ODOF_*` 枚举与 `train_cmd.cpp` 的策略解析器 —— 于是：
   - 新订单 / 老订单的默认值不变；
   - 存档里已经写下的非默认 `ODOF_*` 仍按原样生效，但玩家无法再改。

> 待玩家确认（未做）：是否需要把 `ODOF_*` 整套语义也一并作废（即解挂永远走 `ODOF_KEEP_ORDERS`）。本轮按"删这两个 UI"的字面口径执行。

---

## 四、问题 3：改停靠位置把命令变成"等待挂接"（KI-302，高）

### 现象

玩家在调度命令里把车站订单的停靠位置改成"远端/近端"，该订单**当场变成 `OT_WAIT_COUPLE`（等待挂接）**，命令语义被改写。

### 根因（已用代码与 git 逐字证实）

`Order::type` 是一个 8 位字段，**同一段位被两个语义共用**：

```cpp
// src/order_base.h
inline OrderType GetType()       const { return (OrderType)GB(this->type, 0, 5); } // bit 0-4
inline OrderStopLocation GetStopLocation() const { return ...GB(this->type, 4, 2); } // bit 4-5  ← bit 4 重叠！
inline OrderNonStopFlags GetNonStopType()  const { return ...GB(this->type, 6, 2); }
inline void SetStopLocation(OrderStopLocation l) { SB(this->type, 4, 2, to_underlying(l)); }
```

上游 OpenTTD/JGRPP 的 `GetType()` 一直是 **4 位**（`GB(type, 0, 4)`），bit 4-7 是"按类型解释的载荷"（停靠位置 bit 4-5、不停站 bit 6-7、条件比较符 bit 5-7）。

**pxp 提交 `3f8100c879` 把它改成了 5 位**，以便容纳 `OT_DECOUPLE=15 / OT_GOTO_COUPLE=16 / OT_WAIT_COUPLE=17`。于是 bit 4 同时属于"订单类型"和"停靠位置"：

| 订单 | `type` 字节 | `GetType()`（5 位） | 结果 |
| --- | --- | --- | --- |
| GOTO_STATION + NearEnd(0) | `0x01` | 1 | 正常 |
| GOTO_STATION + **Middle(1)** | `0x11` | **17** | **变成 WAIT_COUPLE** ✗ |
| GOTO_STATION + FarEnd(2) | `0x21` | 1 | 正常 |
| GOTO_STATION + **Through(3)** | `0x31` | **17** | **变成 WAIT_COUPLE** ✗ |
| `OT_WAIT_COUPLE`（`MakeWaitCouple`） | `0x11` | 17 | 与上面第二行**同字节**，无法区分 |
| `OT_GOTO_COUPLE`（`MakeGoToCouple`） | `0x10` | 16 | 4 位基址下会解成 `OT_NOTHING` |

`OrderStopLocation` 的枚举值为 `NearEnd=0, Middle=1, FarEnd=2, Through=3`，所以**只要选到 Middle 或 Through，bit 4 被置 1，`GetType()` 就回读到 17（`OT_WAIT_COUPLE`）**。反向也成立：`MakeWaitCouple()` 写出的 `0x11` 在停靠位置读侧是"中位停靠"。

### 为什么"看着像点击导致的"

`order_gui.cpp:3498-3519` 的名单行点击循环停靠位置：

```cpp
osl = (OrderStopLocation)((to_underlying(osl) + 1) % to_underlying(OrderStopLocation::End));
this->ModifyOrder(sel, MOF_STOP_LOCATION, to_underlying(osl));
```

从默认的 NearEnd(0) 点一下就是 Middle(1) → 订单立刻显示为"等待挂接"。第 178 轮（KI-178）加的 `order->IsType(OT_GOTO_STATION)` 类型门控**挡不住**——因为该订单本来就是车站订单，命令也没被拒绝，是**写入后位域串味**。

### 修法

1. **`src/order_base.h`**：恢复 4 位基址，把 R3R 扩展类型收进"基址 == `OT_DECOUPLE`(15)"的扩展位：

```cpp
inline OrderType GetType() const
{
    const uint8_t base = GB(this->type, 0, 4);
    if (base != OT_DECOUPLE) return static_cast<OrderType>(base);      // 0..14 与上游逐位一致
    return static_cast<OrderType>(OT_DECOUPLE + GB(this->type, 4, 2));  // 15=DECOUPLE / 16=GOTO_COUPLE / 17=WAIT_COUPLE
}
inline void SetType(OrderType type)
{
    if (type < OT_DECOUPLE) {
        SB(this->type, 0, 4, static_cast<uint8_t>(type));               // 保留 bit 4-7 载荷
    } else {
        SB(this->type, 0, 4, static_cast<uint8_t>(OT_DECOUPLE));
        SB(this->type, 4, 2, static_cast<uint8_t>(type) - OT_DECOUPLE);
    }
}
```

   于是：
   - 类型 ≤ 14 的订单字节**完全不变**（`this->type = OT_GOTO_STATION` 之类的裸赋值依旧正确）；
   - 停靠位置重新拥有专属 bit 4-5，Middle/Through 不再串味；
   - `OT_DECOUPLE` 的字节仍是 `0x0F`（向后兼容）；
   - `OT_GOTO_COUPLE` 变成 `0x1F`、`OT_WAIT_COUPLE` 变成 `0x2F`（需迁移，见第 3 点）。

2. **`src/order_cmd.cpp`**：`MakeGoToCouple` / `MakeWaitCouple` 改用 `SetType()`（`MakeDecouple` 同步改写以便阅读一致，字节不变）。

3. **`src/sl/order_sl.cpp` 读档迁移**（老档的 `0x10` / `0x11` 必须救回来，否则 `0x10` 会被当 `OT_NOTHING` 删除、`0x11` 会被当车站订单）：
   - `0x10` → `OT_GOTO_COUPLE`（`0x1F`）：bit0-3 == 0 且字节非 0，只可能是旧编法。
   - `0x11` → 有歧义（旧 `WAIT_COUPLE` 或"中位停靠的车站订单"）：用 `dest` 判 —— `dest` 无效（无站）⇒ 旧 `WAIT_COUPLE`（`0x2F`）；`dest` 有效 ⇒ 车站订单，保持 `0x11` 不动。

### 未确认项 / 复测判据

- R-1：老存档迁移的两条启发式是否够用（需玩家用现有测试档读一次确认无订单丢失）。
- R-2：`GetStopLocation()` 对 `OT_GOTO_COUPLE`/`OT_WAIT_COUPLE` 会返回扩展位残值（1 / 2），需确认没有"未按类型门控"的读点（本轮一并核查）。
- 复测判据：
  1. 车站订单在名单行点击循环 4 档（近端/中间/远端/通过），订单类型全程保持 `GOTO_STATION`，不再出现"等待挂接"；
  2. `WID_O_MGMT_BTN` 下拉里的停靠位置四项同样不再改类型；
  3. `OT_WAIT_COUPLE` / `OT_GOTO_COUPLE` / `OT_DECOUPLE` 订单的语义、显示与解挂/挂接行为无回归；
  4. 读旧档：解挂/挂接类订单数量与类型与存档前一致（日志 `SEGTR-SNAP` / `R3RDUMP-ORDER` 可核对）；
  5. 解挂命令旁的"前半/后半"两个下拉框消失，解挂边界下拉仍可用。

---

## 五、本轮改动清单

| 文件 | 内容 |
| --- | --- |
| `src/order_base.h` | `GetType()` 改 4 位基址 + `OT_DECOUPLE` 扩展位；新增 `SetType()` |
| `src/order_cmd.cpp` | `MakeDecouple` / `MakeGoToCouple` / `MakeWaitCouple` 走 `SetType()` |
| `src/sl/order_sl.cpp` | 读档迁移旧编法 `0x10` / `0x11`（调 `Order::R3RMigrateOldTypeEncoding()`） |
| `src/sl/extended_ver_sl.h` / `.cpp` | 新增特性位 `XSLFI_R3R_ORDER_TYPE_ENC`（save_version 1），供读档判定是否需迁移 |
| `src/order_gui.cpp` | 删"前半/后半"两个下拉框及其全部 UI 代码 |
| `src/widgets/order_widget.h` | 删两个 widget 常量 |

---

## 六、构建自证（2026-10-02 20:42 回填）

- **入口**：复用既有 `_tmp_inc_build.cmd`（**未新建任何 .cmd**，遵守「构建脚本一律复用既有文件」规则）。
- **护栏判定**：`build\R3R_incbuild.guard.log` = `GUARD: 1 header/lang file(s) are newer than the newest object file` / `GUARD: newest object = 2026-10-02 19:31:53` / `GUARD:   newer: src\order_base.h  (2026-10-02 20:11:03)` / `GUARD: REMOVED 620 object file(s) - upgrading this build to a FULL rebuild`。触发原因是**编号回填**（把注释里的 `KI-300` 改成 `KI-302`）碰了 `order_base.h` 的 mtime，按 KI-183 护栏如实升为全量，合规；非逻辑改动。
- **结果三件套**：`build\R3R_incbuild.log` 尾部 = `[692/692] Linking CXX executable openttd.exe`；`error C` / `fatal error` / `FAILED:` / `build stopped` 计数 **0**；`build\R3R_incbuild.done` = `EXIT_CODE=0`（2026-10-02 20:42:53）。
- **时间戳链**：`src\order_base.h` 20:11:03 / `src\order_cmd.cpp` 20:11:04 / `src\sl\order_sl.cpp` 20:11:05 / `src\sl\extended_ver_sl.h` 18:40:46 / `src\sl\extended_ver_sl.cpp` 18:40:57 / `src\widgets\order_widget.h` 18:43:35 / `src\order_gui.cpp` 18:45:50 → obj：`extended_ver_sl.cpp.obj` 20:25:31、`order_sl.cpp.obj` 20:26:00、`order_cmd.cpp.obj` 20:34:04、`order_gui.cpp.obj` 20:34:04 → `build\openttd.exe` **2026-10-02 20:42:30（51 609 600 B）**，晚于全部所改源码。
- **静态/串自证**：exe 内命中新特性位注册名 `r3r_order_type_encoding`（`extended_ver_sl.cpp:245` 字面量已入镜像）；被删的两个下拉框相关标识（`WID_O_DECOUPLE_FIRST_ORDERS` / `_SECOND_ORDERS` / `OrderClick_DecoupleOrders` / `DecoupleOrdersDropDownList` / `DecoupleOrdersLabel`）在全 `src\` 递归检索 **命中 0**，`DP_YARD_*` 只剩 0..4 五个（`order_gui.cpp:1794-1798`）。`Order::R3RMigrateOldTypeEncoding()` 本体无日志字面量（静默迁移），故不以探针串自证。
- **`read_lints`**：6 个改动文件 **0** 条。
- **两版 exe 的关系**：本轮 `src\widgets\order_widget.h` / `src\order_gui.cpp` 由**首次全量构建**（exe 19:34:55）编入；其后因 `order_base.h` 注释触碰 mtime 触发第二次全量（exe 20:42:30），全树重编并把 KI-302 的位域/迁移改动一并入镜像 —— 最终 exe 同时覆盖 KI-302 与 KI-303。
- **状态**：KI-302 / KI-303 均为「改码 + 编译通过」，**游戏内复测待做**（判据见 `R3R_KNOWN_ISSUES.md` 第二十节两条各自清单）。
