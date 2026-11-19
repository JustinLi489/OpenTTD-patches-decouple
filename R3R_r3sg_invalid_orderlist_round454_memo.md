# R3R 读档失败「Referencing invalid OrderList (R3SG)」— 第 454 轮

## 一、现象

玩家 2026-11-16 原话：「我读取既有的存档失败了，显示 referencing invalid orderlist(R3SG)」。

⇒ **高严重度**（存档打不开 = 数据可用性事故，比"某个功能不工作"更严重）。

## 二、立即可证的事实（行号锚点，2026-11-16 核对）

| 锚点 | 事实 |
| --- | --- |
| `src\sl\saveload.cpp:1533-1535` | 报错唯一来源：`case REF_ORDERLIST: if (OrderList::IsValidID(index)) return OrderList::Get(index); SlErrorCorruptWithChunk("Referencing invalid OrderList");` ⇒ 存档里某处写下的 OrderList 原始序号，读档时**池里没有该 ID** |
| `src\sl\saveload.cpp:1495` | 写侧：`case REF_ORDERLIST: return ((const OrderList*)obj)->index + 1;`（即存"序号+1"，`0` 表示 nullptr） |
| `src\sl\couple_group_sl.cpp:378` | `{ 'R3SG', Save_R3SG, Load_R3SG, Ptrs_R3SG, nullptr, CH_SPARSE_TABLE }` —— 段身份表 chunk 已落地（第 152 轮） |
| `src\sl\couple_group_sl.cpp:293` | `NSL("orders", SLE_REF(R3RSegmentRecord, orders, REF_ORDERLIST))` —— R3SG 字段表**第一项**就是 OrderList 引用 |
| `src\couple_group.h:547` | 注释自陈：「Saved by the R3SG chunk as a REF_ORDERLIST, so it **may point at a list which no vehicle references any more**」⇒ 设计上允许段行引用"已无车辆引用"的表 |
| `src\sl\couple_group_sl.cpp:952-958` | 第 156 轮已加 `R3RSegmentReconcileRows()`：读档后对"有行无车/有车空行"做对账（说明这条不变式历史上就是薄弱点） |

**结论句（已验证）**：报错 = 某个 `REF_ORDERLIST` 槽的序号在 OrderList 池里不存在。R3SG 的 `orders` 字段是形态最吻合的宿主，因为它是**唯一允许指向"无车辆引用"的 OrderList** 的引用槽。

## 三、疑点（先登记，逐条待验证）

- **REG-454-A（chunk 名可信度）**：错误文案里的 `(R3SG)` 是"当前正在处理的 chunk 名"。需确认 `SlErrorCorruptWithChunk` 在**指针修正阶段**（`Ptrs_R3SG`）打出的是 R3SG 自己还是"最后一个块"。若确为 R3SG ⇒ 直接锁定该字段表；若为最后块 ⇒ 仍可能是 `ORDL` 之前/之后其它引用槽。**验证判据**：读 `saveload.cpp` 的 `SlErrorCorruptWithChunk` + `_sl.chunk_name`/`sld` 生命周期。
- **REG-454-B（谁把表写丢了）**：`ORDL` chunk 是否会把"只被段行引用的 OrderList"也写出去？若 ORDL 只遍历 `Vehicle::Iterate()` 能找到的表，则段行引用的孤儿表**根本不入档**，读档必然报这个错。**验证判据**：读 `src\sl\order_sl.cpp` 的 `Save_ORDL` 遍历源 + 段行是否被纳入。
- **REG-454-C（写读不对称）**：`Save_R3SG`（:336）写的是 `rows[i].orders` 裸指针 ⇒ 索引；但 `Load_R3SG`（:308）是否对越界/无主行做了清洗（如 `in_use`/`R3RSegmentReconcileRows` 在**指针阶段之后**才跑，救不了指针阶段就炸的情形）。
- **REG-454-D（触发前提）**：该存档是否由"含 R3SG 的新版 exe"写出、还是更早版本？玩家说"既有的存档"，需区分：①老档根本没有 R3SG（则报错不可能是 R3SG）；②新档有 R3SG 但表没入档；③表入档但序号大于实际池大小。

## 四、猜测根因（待验证）

- **G-454-1（已证伪 2026-11-16）**：`ORDL` 写档只写"被活车引用"的表 ⇒ 该猜测**不成立**。`src\sl\order_sl.cpp` 的 `Save_ORDL()` 遍历的是 **`OrderList::Iterate()` 整个池**，池里每一张表都入档，与"有没有车辆引用"无关。⇒ 段行引用只有两种可能：①指向池外的对象 ②指向已被 delete 的内存。
- **G-454-2（部分证伪 2026-11-16）**："段行淘汰后残留陈旧指针"**不成立**：`couple_group.cpp:1053` 的 `R3RSegmentFree()` 执行 `*rec = R3RSegmentRecord{}`，整条记录（含 `orders`）被清零，不会留旧指针。
- **G-454-4（新首选，根因）**：**OrderList 被删除时，没人通知持有它的段行**。
  - `order_cmd.cpp:693` 的 `R3RFindOrderListReferrer(ol, except)` 是"这张表还有谁在用、不能删"的唯一闸门，但它只扫 **`Vehicle::Iterate()`** 的 `w->orders` / `w->orders_backup` / `w->r3r_follow_orders` 三个车辆字段（`:696-705`）。
  - 它**完全不知道** §二 那两条"可以指向无车引用表"的 R3R 停放槽：段行 `R3RSegmentRecord::orders`（R3SG 写出）与车辆字段 `r3r_pool_orders`（R3VP 写出）。
  - ⇒ 当最后一位**车**主放掉这张表、而段行仍握着它时，`OrderList::FreeChain(false)` 判定"没人用"→ `delete` ⇒ 段行的 `orders` 变悬垂指针（内存已归还）⇒ 下次写档 `SLE_REF` 取 `ptr->index + 1` 读到被复用的内存 ⇒ 写进一个不存在的序号 ⇒ 读档 `Ptrs_R3SG` 解析时 `OrderList::IsValidID()` 失败 ⇒ 玩家看到的报错。
  - 旁证：`couple_group.h:547` 的注释（"may point at a list which no vehicle references any more"）把所有责任推给读档侧，而 `FreeChain` 的保护网只覆盖车辆 ⇒ 这条不变式从一开始就没有实现方来守。
  - 另外 `couple_group.cpp:976` 的注释只写了反方向（"Freeing a row must never free the order list it points at"），**没写**"free the list must never leave a row dangling"。

## 四.1 修法方案（三层，全部不动存档格式）

- **F-454-A（救活既有坏档，必须）**：`Ptrs_R3SG()`（`couple_group_sl.cpp:363-372`）在 `SlObjectPtrOrNullFiltered()` 之前先自己校验原始序号：把 `rec->orders` 当整数读，若 `!= 0` 且该序号对应的 OrderList 不在池里，就地写 `rec->orders = nullptr` + 打日志 `R3SG-PTR-DROP`。之后 `SlObjectPtrOrNullFiltered` 看到空指针直接跳过 ⇒ **不再抛错、存档能进游戏**（代价：该段行停放的排程丢失，而那些数据在 `delete` 那一刻就已经不存在了）。
- **F-454-B（不再写出坏档，必须）**：`Save_R3SG()`（`:345-359`）写每一行前用**指针身份**扫一遍 `OrderList::Iterate()`（只比指针值，不解引用），不在池里就把 `rec->orders` 置空 + 打日志 `SEGSAVE-DROP-ORDERS id=`。⇒ 即便内存里已有悬垂指针，也绝不会再被写进档。
- **F-454-C（根因预防，建议同轮做）**：让 `R3RFindOrderListReferrer()` 也认段行——在 `couple_group.cpp` 增加 `bool R3RSegmentHasOrderListRef(const OrderList *)`（扫 `_r3r_segments`，纯指针比较），`FreeChain(false)` 在删表前调用：**只有段行在用时，先把那些行的 `orders` 清空**（段行只是镜像，镜像不该延长表的寿命），然后再照常删除。这样既保住引擎的 `first_shared` 不变式，又彻底消除悬垂指针。

## 四.2 与既有修复的关系

- KI-93（第 47 轮 order_cmd.cpp 的 `R3RFindOrderListReferrer` 引入）+ KI-465 修法 D（`r3r_follow_orders` 也算引用）都是"扩大引用判定"的同一族修法；本次是把该族漏掉的第 4、第 5 个引用槽（段行、`r3r_pool_orders`）补齐。
- 与 KI-95（`REF_VEHICLE` 指向已释放车辆，第 47 轮 `station_sl.cpp` 兜底）同构：都是"存档里写了已被释放的对象"，处理方式也应同构（读档侧自愈 + 写档侧过滤）。

## 五、处置顺序

1. 先读 `order_sl.cpp` 的 `Save_ORDL`/`Load_ORDL`，确认遍历源（10 分钟内可定性 G-454-1）。
2. 再读 `Ptrs_R3SG` 与 `SlErrorCorruptWithChunk`，确认 REG-454-A。
3. 定修法前**必须**向玩家确认一件会改变结论的事：该存档是**用哪个 exe 写出的**（含 R3SG 的版本，还是更早），以及失败是否可稳定复现。

## 六、问答清单（玩家可直接按编号回）

- **Q-454-1**：这个存档是**用哪个版本的 exe 存的**？（①当前这个含 R3SG 的内测版 ②上一轮回退前的版本 ③更早的、还不认识 R3SG 的版本）
- **Q-454-2**：失败是**每次必现**，还是只有这一个存档？
- **Q-454-3**：写入那个存档时，是否刚做过**解挂/耦合/车库拖动**之一（尤其是解挂后立刻存档）？
- **Q-454-4**：能不能把该存档复制一份留底（**不要**在失败后覆盖原档），我需要它做读档取证？

## 七、复测判据（修好后收口用）

1. 同一存档能正常进入游戏，不再出现 `Referencing invalid OrderList`。
2. `build\R3R_debug.log` 出现 `R3SG-PTR-DROP id=… use=… u=… reason=invalid-orderlist`，行数 = 坏档里悬垂的段行数（这是"自愈确实生效"的唯一正面证据；没有这行而档能进，说明报错来自别处，需重查）。
3. 段名/段号/挂接分组在读档后仍按段显示（说明 R3SG 行没被整体丢弃，只丢了那一个已死的排程引用）。
4. 进游戏后再存一次 ⇒ 新档再读**不出现** `R3SG-PTR-DROP`，改为出现 `SEGSAVE-DROP-ORDERS … reason=dangling`（写侧过滤生效）。
5. 负判据：正常单段链（`R3R_SEGMENT_NONE`）读档不出现任何 R3SG 行、行为逐字不变；健康段行（`orders` 还活着）不得出现任何 DROP 日志。
6. 负判据：`R3VP` 三个引用槽（`pool_orders`/`follow_orders`/`orders_backup`）读档后仍正常，不因本次修改产生新的无效引用。

## 八、未确认项

- U-454-1：OrderList 池的 ID 是否在 `ORDL` 里按 `CreateAtIndex` 保留原序号（若会重排，则"序号+1"的引用方式在写读之间本就有失配风险）。
- U-454-2：`Load_R3SG` 是否为每行分配了 `R3RSegmentGetOrCreate()` 并因此"无车也建行"，导致孤儿表更易产生。
- U-454-3（新，REG-454-E 疑点）：`Load_R3SG`（`couple_group_sl.cpp:319`）在 `index <= 0 || index > R3R_SEGMENT_ID_MAX` 时直接 `continue`，**没有消费该行的对象字节** ⇒ 若存档里真出现越界段号，读取器会错位。本次未改（越界段号按设计不该出现），但一旦日志出现"字段错乱"应先怀疑这里。

## 九、本轮改动清单（已落地）

| 位置 | 改动 |
| --- | --- |
| `src\sl\couple_group_sl.cpp`（`Save_R3SG` 之前） | 新增 `static bool R3ROrderListIsLive(const OrderList *ol)`：遍历 `OrderList::Iterate()` **只比指针值、不解引用**，判断段行持有的表是否仍在池中 |
| `Save_R3SG()` 循环内（`SlObjectSaveFiltered` 之前） | 若 `rec->orders != nullptr && !R3ROrderListIsLive(rec->orders)` ⇒ 打 `SEGSAVE-DROP-ORDERS id= use= u= borrowed= reason=dangling` 并把该字段置空 ⇒ **永不再写出无效序号** |
| `Ptrs_R3SG()` 循环内（`SlObjectPtrOrNullFiltered` 之前） | 把 `rec->orders` 当原始整数读（`index + 1`），若 `raw != 0 && !OrderList::IsValidID(raw - 1)` ⇒ 打 `R3SG-PTR-DROP id= raw= use= u= reason=invalid-orderlist` 并置空 ⇒ **既有坏档可以继续读**，不再抛 `SlErrorCorruptWithChunk` |
| **未动** | 存档格式（仍是 `SLE_REF(..., REF_ORDERLIST)`，磁盘布局零变化）；`src\*.h`；`order_cmd.cpp` 的 `FreeChain`/`R3RFindOrderListReferrer`；`R3VP` 的引用槽 |

**构建自证（2026-11-16 21:39）**

| 判据 | 实测 |
| --- | --- |
| 入口 | `R3R_fullbuild.cmd inc detach`（复用既有脚本，未新建任何 `.cmd`） |
| 护栏 | `build\R3R_incbuild.guard.log` = `GUARD: incremental is safe (no header/lang file is newer than the newest object)` |
| 步骤 | `[2/3] Building CXX object CMakeFiles\openttd_lib.dir\src\sl\couple_group_sl.cpp.obj` → `[3/3] Linking CXX executable openttd.exe` |
| 退出码 | `build\R3R_incbuild.done` = `EXIT_CODE=0`（21:39:32） |
| 错误计数 | `error C*` / `fatal error` / `FAILED:` / `build stopped` = **0** |
| 时间戳链 | `src\sl\couple_group_sl.cpp` 21:36:56 → obj 21:38:01 → `build\openttd.exe` 21:39:28 |
| 产物 | `build\openttd.exe` 53 017 600 B |
| lint | `read_lints` 0 条 |
| 字面量自证 | exe 内 4/4 命中：`R3SG-PTR-DROP`、`SEGSAVE-DROP-ORDERS`、`reason=invalid-orderlist`、`reason=dangling` |

**F-454-C（根因预防）= 已搁置（玩家 2026-11-17 拍板，见 §十）**：不再实施。

## 十、C 项搁置（玩家拍板，2026-11-17）

**玩家原话**：「所以现在坏档可读，但不再产生对吗，那 C 就不用了」

**确认**：对。A（`Ptrs_R3SG` 自愈）= 既有坏档可读；B（`Save_R3SG` 过滤）= 不再产出新坏档。二者合起来已把"存档打不开"这条链彻底断开。

**C 是否还有别的价值（2026-11-17 核查，结论=几乎没有）**

| 检查 | 结果 | 结论 |
| --- | --- | --- |
| C 能否保住那段段行排程 | 不能。表在 `OrderList::FreeChain()` 里已被 `delete`，C 只是删表前顺手把段行指针置空，与 A/B 的"丢弃"结果一致 | C 不是"保数据"修法 |
| 段行 `orders` 有没有被解引用 | 全代码仅三类访问：写档（`couple_group_sl.cpp:388` 前）、读档指针修正（`:411-419`）、`!= nullptr` 比较/赋值（`couple_group.cpp:1489`、`train_cmd.cpp:6959`）。检索 `SegmentGet/GetOrCreate/Alloc(...)->orders` = **0 命中** | 悬垂指针永不参与运算 ⇒ 不会崩、不会被当成表 |
| `couple_group.cpp:1489` 的"仍持有内容⇒跳过重分配"会不会被悬垂误导 | 该函数只在读档后跑一次，届时 A 已把悬垂置空 | 不成立 |
| 悬垂的存活窗口 | 下一次 `R3RSyncSegmentTraits`（耦合/解挂/车库编辑/读档）就把 `row->orders` 覆盖成有效表 | 窗口极短 |

⇒ C 的实际收益只剩"形式一致性 / 未来防御"。**不做**，等将来真出现第二个解引用方再补。本条与 KI-647 的"已搁置"状态一致。

**仍然有效的待办**（与本搁置无关）：`Load_R3SG` 越界段号 `continue` 未消费对象字节（U-454-3）；以及玩家侧复测——用新 exe 开原坏档，确认 `build\R3R_debug.log` 出现 `R3SG-PTR-DROP … reason=invalid-orderlist` 且段名/号/分组仍在。
