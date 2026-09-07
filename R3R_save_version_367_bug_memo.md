# R3R 存档版本 367 自读失败 bug —— 诊断与修复 (2026-09-07)

## 症状
本版保存的存档本版读取报错:
`Got an extended savegame version with a base version in the upstream mode range, giving up`

## 根因
- `ab42cc7109`(2026-09-06 checkpoint)把 `SAVEGAME_VERSION` 从
  `SLV_CUSTOM_SUBSIDY_DURATION`(=292)提升到 `SLV_R3R_ARTIC_OVERRIDE`(=367),
  用于给 artic 铰接车 de-articulate 后的 weight/power/max_speed override
  三字段(`src/saveload/vehicle_sl.cpp`)做存档版本门控。
- JGR 读端规则(`src/sl/saveload.cpp`):header base 带 EXT 标志(0x8000)的
  extended 档,其 base 必须 `< SLV_SAVELOAD_LIST_LENGTH`(=293);base>=293 且
  带 EXT 判定为自相矛盾,直接 `SlError` 放弃。
- 292<293 → 09-04 stable(含更早)存读正常;367>=293 → 09-06/09-07 exe 保存
  的档自己都读不回(用户 09-07 首次存读档即暴露)。

## 修复
`src/sl/saveload.cpp` 版本判定:base 落在 R3R 定义段
(`SLV_R3R_ARTIC_OVERRIDE .. MAX_LOAD_SAVEGAME_VERSION`)且带 EXT 的档放行
(继续 JGR 路径,不置 upstream_mode);该段之外带 EXT 的 upstream-range base
仍按原逻辑拒绝。读写自洽:367 档字段布局 = 292 档布局 + 3 override 字段,
版本条件(IsSavegameVersionBefore 等)全程基于 `_sl_version`,无其他阻塞点。

## 兼容性矩阵(修复后)
| 档来源 | base | 修复版 exe 读取 |
|---|---|---|
| 09-04 stable 及更早(R3R/JGR 292 时代) | 292 | 正常,override=0 |
| 09-06/09-07(367 时代)已存档 | 367 | 正常(本次修复目标) |
| 修复版新存档 | 367 | 正常 |
| 官方 JGRPP / 上游 OTTD | — | 拒绝(本就互不兼容的魔改分支) |

## 注意事项(未来 bump)
- 新增 R3R 存档字段时,枚举插在 `SL_MAX_VERSION` **之前**(紧邻 R3R 段),
  `SL_MAX_VERSION`/`MAX_LOAD_SAVEGAME_VERSION` 自动跟进,读端放行区间随之扩大。
- 不要越过 `SL_MAX_VERSION`,否则 4243 行 too-new 判定会拦。

## 验证
增量构建 `BUILD_EXIT=0`,`[3/3] Linking`,3 个既有 warning 无新增。
待实测:保存 → 重进读档。
