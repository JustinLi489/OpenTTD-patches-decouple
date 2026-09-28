@echo off
rem ============================================================
rem  R3R FULL REBUILD - internal test tree (build\, R3R probes ON)
rem
rem  WHY ASCII ONLY: cmd.exe reads .cmd files with the OEM
rem  codepage (936 on this box). A UTF-8 .cmd containing Chinese
rem  text is executed from the middle of a line and prints
rem  "not recognized" garbage. Keep every byte of this file
rem  ASCII. Chinese explanations live in R3R_KNOWN_ISSUES.md.
rem
rem  WHY DETACHED: a full rebuild takes 25-40 min. If it is
rem  started from the editor's terminal, closing the editor kills
rem  the build and leaves no trace at all (KI-30). This script
rem  therefore writes everything to files and can relaunch itself
rem  in its own minimized window that outlives the parent shell.
rem
rem  USAGE (close the game first, then close the editor):
rem    R3R_fullbuild.cmd            build in this window (waits)
rem    R3R_fullbuild.cmd detach     relaunch in a separate
rem                                 minimized window, return at once
rem    or just double-click R3R_fullbuild_detach.cmd
rem
rem  OUTPUT
rem    live log : build\R3R_fullbuild.log
rem    verdict  : build\R3R_fullbuild.done   ->  EXIT_CODE=<n>
rem
rem  EXIT CODES
rem     0 = rebuild succeeded, build\openttd.exe is fresh
rem    95 = build.ninja asks for the probes OFF (wrong tree)
rem    96 = vcvars64.bat failed to load
rem    97 = openttd.exe is running (the link would hit LNK1168)
rem    98 = build\build.ninja is missing
rem    other = ninja's own exit code (read the log)
rem
rem  NOTE: this is a FULL rebuild on purpose. This ninja cannot
rem  track header dependencies (KI-15: cl prints the "including
rem  file" note in Chinese, so deps=msvc reports 0 deps); stale
rem  objects would be linked into a mixed-layout binary.
rem ============================================================
setlocal EnableExtensions
cd /d "%~dp0"

set "NINJA=D:\gcc new\mingw64\bin\ninja.exe"
set "VCVARS=D:\VISUAL STUDIO\MAIN PACK\VC\Auxiliary\Build\vcvars64.bat"
set "JOBS=2"
set "LOGF=build\R3R_fullbuild.log"
set "DONEF=build\R3R_fullbuild.done"

if /i "%~1"=="detach" goto :DETACH
goto :WORK

rem ------------------------------------------------------------
rem  Launch a detached copy of ourselves and return immediately.
rem  Start-Process gives the child its own console, so it is not
rem  killed when this window (or the editor) goes away.
rem ------------------------------------------------------------
:DETACH
echo Launching the full rebuild in a separate minimized window...
powershell -NoProfile -ExecutionPolicy Bypass -Command "Start-Process -FilePath '%~f0' -ArgumentList 'run' -WindowStyle Minimized"
echo   log     : %LOGF%
echo   verdict : %DONEF%
echo   This window can be closed now - the build keeps running.
exit /b 0

rem ------------------------------------------------------------
rem  The actual work.
rem ------------------------------------------------------------
:WORK
set "SELFDETACH=0"
if /i "%~1"=="run" set "SELFDETACH=1"

rem  The verdict file lives under build\; create that tree first so that
rem  even the "[FATAL] build.ninja is missing" path leaves a trace
rem  behind instead of ending silently.
if not exist "build" mkdir "build"

echo ============================================================
echo   R3R FULL REBUILD - internal test tree (probes ON)
echo   started %DATE% %TIME%
echo ============================================================
echo.

if not exist "build\build.ninja" (
    echo [FATAL] build\build.ninja is missing.
    echo [FATAL] This helper does not configure from scratch - restore
    echo [FATAL] the build\ directory first.
    > "%DONEF%" echo EXIT_CODE=98
    goto :HOLD
)

tasklist /fi "imagename eq openttd.exe" 2>nul | find /i "openttd.exe" >nul
if not errorlevel 1 (
    echo [FATAL] openttd.exe is still running. Close the game first,
    echo [FATAL] otherwise the link step fails with LNK1168.
    > "%DONEF%" echo EXIT_CODE=97
    goto :HOLD
)

rem  The internal test tree must keep the probes ON (KI-28).
findstr /C:"-DR3R_PROBES_DEFAULT=0" "build\build.ninja" >nul 2>&1
if not errorlevel 1 (
    echo [FATAL] build\build.ninja carries -DR3R_PROBES_DEFAULT=0.
    echo [FATAL] That is the release tree. This helper only rebuilds
    echo [FATAL] the internal test tree, which needs the probes ON.
    > "%DONEF%" echo EXIT_CODE=95
    goto :HOLD
)

echo [0/4] dropping leftover cl/ninja/cmake from an aborted run...
taskkill /F /IM cl.exe       >nul 2>&1
taskkill /F /IM ninja.exe    >nul 2>&1
taskkill /F /IM cmake.exe    >nul 2>&1
taskkill /F /IM mspdbsrv.exe >nul 2>&1
echo.

if not exist "build\tmp" mkdir "build\tmp"
set "TEMP=%~dp0build\tmp"
set "TMP=%~dp0build\tmp"
if exist "%DONEF%" del /q "%DONEF%" >nul 2>&1

echo [1/4] deleting every object file under build\ ...
del /s /q "build\*.obj" >nul 2>&1
dir /s /b "build\*.obj" >nul 2>&1
if errorlevel 1 (echo       OK: no .obj left) else (echo       [WARN] some .obj survived)
echo.

echo [2/4] loading the MSVC x64 environment (vcvars64)...
call "%VCVARS%" >nul 2>&1
if errorlevel 1 (
    echo [FATAL] vcvars64.bat failed to load.
    > "%DONEF%" echo EXIT_CODE=96
    goto :HOLD
)
echo       OK.
echo.

echo [3/4] ninja -C build -j%JOBS% openttd   (this is the long part)
echo       started %DATE% %TIME%
echo       live log: %LOGF%
echo       to watch it in another window run:
echo         powershell -NoProfile -Command "Get-Content %LOGF% -Wait -Tail 20"
echo.

rem  From here on everything goes into the log as well, so a dead
rem  console can never hide what happened (KI-30).
"%NINJA%" -C "build" -j %JOBS% openttd > "%LOGF%" 2>&1
set "RC=%ERRORLEVEL%"
echo       ninja ended %DATE% %TIME%
echo.

echo [4/4] result
if exist "build\openttd.exe" (
    powershell -NoProfile -Command "$e=Get-Item 'build\openttd.exe'; Write-Host ('      openttd.exe : ' + $e.Length + ' bytes   ' + $e.LastWriteTime)"
)
if "%RC%"=="0" (
    echo       OK: rebuild finished, build\openttd.exe is up to date.
) else (
    echo       [FAIL] ninja exit code = %RC%  - read %LOGF%
)
>> "%LOGF%" echo.
>> "%LOGF%" echo ==== R3R_fullbuild ended %DATE% %TIME%  EXIT_CODE=%RC%
> "%DONEF%" echo EXIT_CODE=%RC%

:HOLD
echo.
if "%SELFDETACH%"=="1" exit /b 0
echo   Press any key to close this window.
rem  "< CON" is what actually keeps the window open. A bare "pause"
rem  reads whatever stdin it inherited: when this script is started
rem  from an editor task, a pipe or a hidden window, that stdin is
rem  already closed/redirected, so pause returns at once and the
rem  window flashes away before the log path can be read.
pause < CON >nul
exit /b 0
