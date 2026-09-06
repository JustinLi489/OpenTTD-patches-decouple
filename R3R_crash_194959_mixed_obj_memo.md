# R3R 崩溃 194959:买三节铰接列车崩溃 —— 混合 vtable 对象(依赖跟踪失效)备忘

日期:2026-09-06
关联:artic 铰接车 / Train::ConsistChanged / 新增 virtual 槽位 / Ninja+MSVC 依赖失效
状态:已修复(全量重编),根因与预防见下。

## 现象

新游戏中购买"三节铰接式列车"崩溃。回溯落点 `0x4D`(越界读空指针成员),实为:
`Vehicle::Previous()`/`GetPrevUnit()` 沿指针回退时,取到由**旧布局头文件**编译的对象,
随后经 vtable 派发 `call 0` → RIP=0。

## 根因(两层)

### 直接根因:混合布局二进制

给 `Vehicle`/`Train` 增补 virtual 槽位(新字段)后,共 594 个 obj 中仅 7 个在
`train.h`/`vehicle_base.h` 改动后被重编(改源文件时顺手重编),其余 **587 个 obj 仍由旧头
文件状态编译**,却被链接进同一 exe:

- 新头文件编译的调用方:按新布局读写 vtable / 槽位(读新字段 22-24);
- 旧头文件编译的对象:老 vftable 上无对应槽位 → 越界读到 0 → `call 0` → 崩溃。

判定依据:obj 时间戳(9/3、9/6 2:33 等)早于 `train.h`(9/6 2:54)、`vehicle_base.h`(9/6 2:56)。

### 环境根因:Ninja 头文件依赖跟踪完全失效(实测 `#deps 0`)

- `ninja -t deps <obj>` 实测输出 `#deps 0, deps mtime ... (VALID)` —— 一条头文件依赖都没记录;
- 原因:规则为 `deps = msvc`,ninja 需从 cl 标准输出解析 `/showIncludes` 前缀行;
  中文系统下 cl 输出 `注意: 包含文件:  D:\...\xxx.h`,ninja 1.12.1 无法匹配(只认英文
  `Note: including file:`),故 0 依赖;
- `set VSLANG=1033`(在 `call vcvars64.bat` 之后设置)**实测无效**:cl 14.51 报错仍输出
  中文"无法打开包括文件"。此 cl 版本消息语言不随 VSLANG 切换;
- 结论:**本机此构建配置下,任何 `src/*.h` 修改都不会触发增量重编**,只认 .cpp 源文件与
  `rev.cpp`(每次构建由 FindVersion 重新生成)。

## 修复

删除全部 `build\CMakeFiles\openttd_lib.dir` 下 obj(`*.obj`)+ 依赖文件后,在
`vcvars64.bat` 环境下全量重编:**597/597 obj 全部重编、链接成功**。

验证:
- `test5.sav` 加载 45 秒无崩溃、无新崩溃日志;
- 临时探针(CC 段 m1-m13)确认 `ConsistChanged` 全路径完整跑通;
- 探针代码已全部摘除,`train_cmd.cpp` 无残留 `R3RLOG_PROBE`。

## 预防(重要)

1. **改了任何 `src/*.h` → 必须强制全量重编**(仅改 .cpp 可增量,约 1 分钟/文件)。
   全量重编约 25-40 分钟(597 文件),别嫌慢——否则必然复现混合对象崩溃,且症状可能
   随机出现在任意 vtable/字段访问点。
2. 判别"是否漏重编":对比 `build\CMakeFiles\openttd_lib.dir\src\*.cpp.obj` 的
   LastWriteTime 是否全部晚于所改头文件;或 `ninja -t deps <某obj>` 看是否 `#deps 0`。
3. 本问题与既有备忘(改 `src\lang\*.txt` 后语言包版本不符,strings.cpp 不重编)同根,
   即"生成头/普通头改动不触发重编"。根源均为 ninja deps=msvc 解析不到本地化前缀。
4. 若要根治构建环境(可选、未做):改用 CMake 4.x 的 `-scanDependencies` scanned 路径
   (生成 `.ninja_dyndep`,不经 /showIncludes 文本解析),或换英文系统/英文 cl,或换
   clang-cl(其依赖输出为英文 depfile 形式)。
