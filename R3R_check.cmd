@echo off
chcp 65001 >nul
setlocal EnableDelayedExpansion
title R3R build health check

set "ROOT=%~dp0"
if "%ROOT:~-1%"=="\" set "ROOT=%ROOT:~0,-1%"
set "BUILD=%ROOT%\build"
set "HDR=%ROOT%\src\vehicle_base.h"
set "VCVARS=D:\VISUAL STUDIO\MAIN PACK\VC\Auxiliary\Build\vcvars64.bat"
set "NINJA=D:\gcc new\mingw64\bin\ninja.exe"
set "INC_LOG=%BUILD%\R3R_inc_build.log"
set "RPT=%TEMP%\r3r_check.txt"

:MENU
cls
echo ============================================================
echo   R3R 编译状态体检   (KI-15: 本机 ninja deps=msvc 看不到头文件依赖)
echo ------------------------------------------------------------
echo   ROOT  = %ROOT%
echo   BUILD = %BUILD%
echo   HDR   = %HDR%
echo ============================================================
echo.

call :SCAN
if not exist "%RPT%" (
    echo [ERROR] 体检失败: 未生成报告 %RPT%
    goto :MENU_BAR
)
for /f "usebackq tokens=1,* delims==" %%a in ("%RPT%") do set "%%a=%%b"

if not defined OBJ_COUNT (
    echo [ERROR] 报告解析失败, 内容如下:
    type "%RPT%"
    goto :MENU_BAR
)

echo   ------- 时间戳 -------
echo   header(vehicle_base.h) : %HEADER_TIME%
echo   obj 总数                : %OBJ_COUNT%
echo   obj 早于头文件          : %OBJ_OLDER%
echo   obj 最老 / 最新         : %OBJ_OLDEST%   %OBJ_NEWEST%
echo   openttd.exe            : %EXE_TIME%   ^(%EXE_SIZE% bytes^)
echo ------------------------------------------------------------

if "%OBJ_COUNT%"=="0" (
    echo   结论: [安全但要等] build 下没有任何 .obj
    echo         下次编译会自动全量重编 615 个 obj, 约 25-40 分钟。
    echo         好处: 绝不会产生混合布局 exe ^(不会随机崩溃^)。
) else if not "%OBJ_OLDER%"=="0" (
    echo   结论: [危险] 有 %OBJ_OLDER% 个陈旧 obj ^(早于头文件^)
    echo         此时增量编译会链接出混合布局 exe = 随机崩溃!
    echo         必须先全量重编 -^> 菜单选 3
    echo         报告原文: %VERDICT%
) else (
    echo   结论: [安全] 全部 obj 均新于头文件, 可以增量编译 -^> 菜单选 2
    echo         报告原文: %VERDICT%
)

:MENU_BAR
echo ============================================================
echo   [1] 重新体检
echo   [2] 增量编译   ^(ninja -j4, 仅"全绿"状态可用^)
echo   [3] 全量重编   ^(调 rebuild_R3R.cmd: 清全部 obj + vcvars + ninja -j4^)
echo   [4] 查看上次编译日志尾部 20 行
echo   [5] 打开 build 目录
echo   [0] 退出
echo ============================================================
choice /c 123450 /n /m "请选择 > "
if errorlevel 6 goto :END
if errorlevel 5 goto :OPEN_DIR
if errorlevel 4 goto :TAIL_LOG
if errorlevel 3 goto :FULL
if errorlevel 2 goto :INC
goto :MENU

:SCAN
if not exist "%HDR%" (
    echo [ERROR] 找不到头文件 %HDR%
    exit /b 1
)
powershell -NoProfile -Command "$ErrorActionPreference='SilentlyContinue'; $b='%BUILD%'; $h=(Get-Item '%HDR%').LastWriteTime; $o=@(Get-ChildItem $b -Recurse -Filter *.obj -ErrorAction SilentlyContinue); $old=@($o | Where-Object { $_.LastWriteTime -le $h }).Count; $e=Get-Item (Join-Path $b 'openttd.exe'); $L=@(); $L+='HEADER_TIME=' + $h.ToString('yyyy-MM-dd HH:mm:ss'); $L+='OBJ_COUNT=' + $o.Count; $L+='OBJ_OLDER=' + $old; if ($o.Count -gt 0) { $L+='OBJ_OLDEST=' + ($o | Sort-Object LastWriteTime | Select-Object -First 1).LastWriteTime.ToString('yyyy-MM-dd HH:mm:ss'); $L+='OBJ_NEWEST=' + ($o | Sort-Object LastWriteTime -Descending | Select-Object -First 1).LastWriteTime.ToString('yyyy-MM-dd HH:mm:ss') } else { $L+='OBJ_OLDEST=n/a'; $L+='OBJ_NEWEST=n/a' }; if ($e) { $L+='EXE_TIME=' + $e.LastWriteTime.ToString('yyyy-MM-dd HH:mm:ss'); $L+='EXE_SIZE=' + $e.Length } else { $L+='EXE_TIME=missing'; $L+='EXE_SIZE=0' }; if ($o.Count -eq 0) { $L+='VERDICT=no obj at all => next build is FULL' } elseif ($old -gt 0) { $L+='VERDICT=' + $old + ' stale obj => MUST FULL REBUILD' } else { $L+='VERDICT=all obj newer than header => INCREMENTAL OK' }; Set-Content -Path '%RPT%' -Value $L -Encoding ASCII"
exit /b 0

:INC
if not exist "%NINJA%" (
    echo [ERROR] 找不到 ninja: %NINJA%
    pause
    goto :MENU
)
echo.
echo == 载入 MSVC x64 环境 ==
call "%VCVARS%" >nul
if errorlevel 1 (
    echo [ERROR] vcvars64.bat 载入失败: %VCVARS%
    pause
    goto :MENU
)
echo == 增量编译: ninja -C "%BUILD%" -j 4 openttd ==
"%NINJA%" -C "%BUILD%" -j 4 openttd > "%INC_LOG%" 2>&1
set "RC=%ERRORLEVEL%"
echo.
echo ============================================================
echo   NINJA_EXIT=%RC%     ^(0 = 成功^)
echo   日志: %INC_LOG%
echo ============================================================
pause
goto :MENU

:FULL
if not exist "%ROOT%\rebuild_R3R.cmd" (
    echo [ERROR] 找不到 %ROOT%\rebuild_R3R.cmd
    pause
    goto :MENU
)
echo.
echo == 调用全量重编脚本 rebuild_R3R.cmd ==
call "%ROOT%\rebuild_R3R.cmd"
goto :MENU

:TAIL_LOG
echo.
if exist "%INC_LOG%" (
    echo ------- %INC_LOG%  (tail 20) -------
    powershell -NoProfile -Command "Get-Content '%INC_LOG%' -Tail 20"
) else (
    echo 还没有增量编译日志: %INC_LOG%
)
echo.
pause
goto :MENU

:OPEN_DIR
start "" explorer "%BUILD%"
goto :MENU

:END
endlocal
exit /b 0
