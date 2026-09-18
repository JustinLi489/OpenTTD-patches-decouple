@echo off
rem !! DO NOT change this file to UTF-8 and DO NOT use "chcp 65001" here !!
rem    This file is saved as GBK (codepage 936). cmd.exe under cp65001 mis-parses
rem    batch files that contain multi-byte characters and starts executing garbage
rem    from the middle of a line ("'xx' is not recognized as an internal ...").
chcp 936 >nul
setlocal EnableDelayedExpansion
title R3R / JGRPP 0.72.4 A-B 取数程序
color 0F

set "WORK=D:\r3r_probe\AB"
if not exist "%WORK%" mkdir "%WORK%" >nul 2>nul
set "CFG=%WORK%\config.txt"
set "PS=%~dp0R3R_AB_probe.ps1"
set "DEFSAVEDIR=%USERPROFILE%\Documents\OpenTTD\save"

set "SAVE="
set "R3REXE="
set "VANEXE="
if exist "%CFG%" for /f "usebackq tokens=1,* delims==" %%a in ("%CFG%") do (
  if /i "%%a"=="save"   set "SAVE=%%b"
  if /i "%%a"=="r3rexe" set "R3REXE=%%b"
  if /i "%%a"=="vanexe" set "VANEXE=%%b"
)
if not defined R3REXE set "R3REXE=D:\sourcecode of JGRPP\build\openttd.exe"

:menu
cls
echo ============================================================
echo   R3R  /  未改版 JGRPP 0.72.4   A-B 取数程序
echo ============================================================
echo   存档: %SAVE%
echo   R3R : %R3REXE%
echo   原版: %VANEXE%
echo ------------------------------------------------------------
echo   [1] 采集 R3R 版（自动启动 + 自动解析 R3R_perf.log）
echo   [2] 采集 JGRPP 0.72.4 版（自动启动 + 抄 2 行数字）
echo   [3] 生成对比报告
echo   [4] 修改 存档 / 程序 路径
echo   [0] 退出
echo ------------------------------------------------------------
choice /c 12340 /n /m "请选择: "
if errorlevel 5 goto :eof
if errorlevel 4 goto :setup
if errorlevel 3 goto :report
if errorlevel 2 goto :runvan
goto :runr3r

:setup
cls
echo ---------- 设置路径（直接回车 = 用默认值）----------
set "NEWEST="
if exist "%DEFSAVEDIR%\" for /f "delims=" %%f in ('dir /b /o-d "%DEFSAVEDIR%\*.sav" 2^>nul') do if not defined NEWEST set "NEWEST=%DEFSAVEDIR%\%%f"
echo 常用存档目录: %DEFSAVEDIR%
set "D=%SAVE%"
if not defined D set "D=%NEWEST%"
set /p "SAVE=存档 .sav 完整路径: "
if not defined SAVE set "SAVE=%D%"
set /p "R3REXE=R3R openttd.exe 路径: "
if not defined R3REXE set "R3REXE=D:\sourcecode of JGRPP\build\openttd.exe"
set /p "VANEXE=JGRPP 0.72.4 openttd.exe 路径: "
if not defined VANEXE echo   [警告] 未填原版路径，采集 2 时无法自动启动。
if not exist "%SAVE%" echo   [警告] 存档不存在: %SAVE%
if not exist "%R3REXE%" echo   [警告] R3R exe 不存在: %R3REXE%
if defined VANEXE if not exist "%VANEXE%" echo   [警告] 原版 exe 不存在: %VANEXE%
> "%CFG%" echo save=%SAVE%
>>"%CFG%" echo r3rexe=%R3REXE%
>>"%CFG%" echo vanexe=%VANEXE%
echo.
echo 已保存到 %CFG%
pause
goto :menu

:prepare
copy /y "%SAVE%" "%WORK%\ab_save.sav" >nul
if errorlevel 1 (
  echo [错误] 无法复制存档，请检查路径: %SAVE%
  pause
  exit /b 1
)
exit /b 0

:runr3r
cls
echo ---------- 采集 R3R 版 ----------
call :prepare
if not exist "%R3REXE%" ( echo [错误] R3R exe 不存在: %R3REXE% & pause & goto :menu )
for %%i in ("%R3REXE%") do set "R3RDIR=%%~dpi"
rem !! 必须去掉结尾的反斜杠：cmd 传出 -ExeDir "D:\...\build\" 时，CRT 会把 \" 当成
rem    被转义的引号，PowerShell 收到的是 "D:\...\build"" -> Join-Path 报 Illegal characters。
if "%R3RDIR:~-1%"=="\" set "R3RDIR=%R3RDIR:~0,-1%"
if exist "%R3RDIR%\R3R_perf.log" move /y "%R3RDIR%\R3R_perf.log" "%WORK%\R3R_perf_prev.log" >nul 2>nul

echo.
echo  即将启动 R3R，载入同一份存档副本: %WORK%\ab_save.sav
echo.
echo  【请在游戏里做这些事】
echo   1) 等存档载入完毕，让游戏自己跑 60 秒以上（不要暂停）
echo   2) 按 ` 键（Esc 下面那个反引号）打开控制台
echo   3) 输入： fps            回车     -- 记录 GL train ticks 三个数
echo   4) 输入： dump_veh_stats 回车     -- 记录 train primary 数字
echo   5) 最好按 Win+Shift+S 截个图，存到 %WORK%
echo   6) 关闭游戏（点窗口 X），本程序会自动继续
echo.
pause
start "" /wait /d "%R3RDIR%" "%R3REXE%" -g "%WORK%\ab_save.sav"

echo.
echo 游戏已退出。是否自动解析 R3R_perf.log ? (y/n)
choice /c yn /n /m "> "
if errorlevel 2 goto :menu
powershell -NoProfile -ExecutionPolicy Bypass -File "%PS%" -Mode parseR3R -Work "%WORK%" -ExeDir "%R3RDIR%" > "%WORK%\data_R3R.txt" 2> "%WORK%\data_R3R.err.txt"
>>"%WORK%\data_R3R.txt" echo exe=%R3REXE%
>>"%WORK%\data_R3R.txt" echo save=%SAVE%
echo.
findstr /c:"window_quality=not_running" "%WORK%\data_R3R.txt" >nul 2>nul
if not errorlevel 1 (
  echo [警告] R3R_perf.log 里没有“正在跑”的窗口 —— 采集时游戏多半处于暂停状态。
  echo        请让列车真正跑起来（不暂停）后再读 fps，然后重跑本项。
  echo.
)
echo ---------- R3R 自动采集结果 ----------
type "%WORK%\data_R3R.txt"
if exist "%WORK%\data_R3R.err.txt" (
  echo.
  echo ---------- PS 的 stderr（非致命，仅供排查）----------
  type "%WORK%\data_R3R.err.txt"
)
echo.
set "ST="
set /p "ST=【关键】你读 fps 的那一刻，列车是在跑吗？(跑=r / 停=s): "
if /i "%ST%"=="r" (>>"%WORK%\data_R3R.txt" echo state=running) else (>>"%WORK%\data_R3R.txt" echo state=stopped)
echo 已写入 %WORK%\data_R3R.txt
pause
goto :menu

:runvan
cls
echo ---------- 采集 JGRPP 0.72.4 版 ----------
call :prepare
if not defined VANEXE ( echo [错误] 未设置原版 exe 路径，请先在 [4] 里填写。 & pause & goto :menu )
if not exist "%VANEXE%" ( echo [错误] 原版 exe 不存在: %VANEXE% & pause & goto :menu )
for %%i in ("%VANEXE%") do set "VANDIR=%%~dpi"
if "%VANDIR:~-1%"=="\" set "VANDIR=%VANDIR:~0,-1%"

echo.
echo  即将启动 JGRPP 0.72.4，载入同一份存档副本: %WORK%\ab_save.sav
echo.
echo  【请在游戏里做这些事】
echo   1) 等存档载入完毕，让游戏自己跑 60 秒以上（不要暂停）
echo   2) 按 ` 键打开控制台
echo   3) 输入： fps            回车
echo   4) 输入： dump_veh_stats 回车
echo   5) 抄下 / 截图，然后关闭游戏
echo.
echo  【如果原版报“无法载入存档”】把弹窗原文抄下来，这一步本身也是关键证据。
echo.
pause
start "" /wait /d "%VANDIR%" "%VANEXE%" -g "%WORK%\ab_save.sav"

cls
echo ---------- 录入 JGRPP 0.72.4 的数据（回车=留空）----------
set "A=" & set "B=" & set "C=" & set "P=" & set "S=" & set "T=" & set "F=" & set "ST=" & set "NO=" & set "FAIL="
set /p "FAIL=原版能否载入该存档？(y=能/n=不能): "
set /p "F=Game loop rate (fps): "
set /p "A=GL train ticks 第1个数(小窗口 ms): "
set /p "B=GL train ticks 第2个数(中窗口 ms): "
set /p "C=GL train ticks 第3个数(大窗口 ms): "
set /p "P=dump_veh_stats 里 train primary 的数字: "
set /p "S=dump_veh_stats 里 train secondary 的数字: "
set /p "T=dump_veh_stats 里 Total vehicles 的数字: "
set /p "ST=【关键】读数那一刻列车在跑吗？(跑=r / 停=s): "
set /p "NO=补充说明(可空): "
(
  echo exe=%VANEXE%
  echo save=%SAVE%
  echo loadable=%FAIL%
  echo fps=%F%
  echo GLtrain1=%A%
  echo GLtrain2=%B%
  echo GLtrain3=%C%
  echo train_primary=%P%
  echo train_secondary=%S%
  echo totalvehicles=%T%
  echo note=%NO%
) > "%WORK%\data_JGRPP.txt"
if /i "%ST%"=="r" (>>"%WORK%\data_JGRPP.txt" echo state=running) else (>>"%WORK%\data_JGRPP.txt" echo state=stopped)
echo.
echo 已写入 %WORK%\data_JGRPP.txt
pause
goto :menu

:report
cls
powershell -NoProfile -ExecutionPolicy Bypass -File "%PS%" -Mode report -Work "%WORK%" -ExeDir "" >nul 2> "%WORK%\report.err.txt"
if not exist "%WORK%\report.txt" (
  echo [错误] 报告生成失败，PS 的报错如下:
  type "%WORK%\report.err.txt" 2>nul
  pause
  goto :menu
)
type "%WORK%\report.txt"
echo.
echo （同一份报告已存到 %WORK%\report.txt ，可直接发我）
pause
goto :menu
