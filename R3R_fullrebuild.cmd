@echo off
setlocal
title R3R FULL REBUILD
cd /d "%~dp0"

rem  Phase-2 marker (KI-31). MUST be tested as the FIRST argument and
rem  BEFORE the target parsing below and before any prompt, so the
rem  child process can never fall into :MENU again.
if /i "%~1"=="__go" goto :DBG_RUN

rem ============================================================
rem  Double-click helper for the two R3R build trees (KI-28):
rem
rem    build\          INTERNAL TEST build (Debug, probes ON).
rem                    This is the one you play day to day.
rem    build-release\  PUBLISH build (RelWithDebInfo + /O2, probes
rem                    OFF). Rebuilt only when a release is made.
rem
rem  Both targets are FULL rebuilds on purpose. This box's ninja
rem  does not track header dependencies (KI-15: cl prints
rem  "Note: including file:" in Chinese, so deps=msvc reports
rem  0 deps), so after any src\*.h change the stale .obj files
rem  link into a mixed binary and the game crashes randomly.
rem  Deleting every .obj is the only safe reaction.
rem
rem  Close CodeBuddy first (KI-24): the box has 8 GB RAM and
rem  cl.exe at -j2 plus the IDE gets paged out -> build freezes.
rem
rem  KI-30 (2026-09-14): target 2 used to print everything straight
rem  to the console and nothing else. Whenever the console died
rem  (Ctrl+C, closed window, parent shell torn down) the run left
rem  no trace at all, which is exactly the "target 2 never runs"
rem  symptom. It now always runs as a child cmd whose whole output
rem  is captured in build\R3R_fullrebuild.log plus a one-line
rem  build\R3R_fullrebuild.done carrying the exit code.
rem
rem  KI-31 (2026-09-14): the two-phase split was keyed off %MODE%, but
rem  %MODE% only exists when the target was given on the command line.
rem  On the menu path %MODE% is empty, so the child command line
rem      cmd /c ""%~f0" %MODE% __go"
rem  collapsed to
rem      cmd /c ""%~f0" __go"
rem  -> %~1 became __go, %~2 became empty, the  "%~2"=="__go"  test
rem  missed, and the CHILD fell through into :MENU. Its prompt went
rem  into the redirected log (invisible), the empty set /p made SEL
rem  default to 1, and a request for target 2 silently ran TARGET 1 -
rem  a ~21 minute build-release rebuild. :END's guard ("%~2"=="__go")
rem  missed for the same reason, so the child then stopped at an
rem  invisible pause.
rem  Fix: __go is now the FIRST argument and is detected before any
rem  prompt, so the child phase can never reach the menu; the child
rem  also gets  < nul  so a stray prompt fails fast instead of hanging
rem  unseen. The outer phase additionally verifies that the child
rem  really printed its work-phase banner before trusting its code.
rem
rem  Usage:  double-click, or  R3R_fullrebuild.cmd release
rem                            R3R_fullrebuild.cmd debug
rem ============================================================

set "NINJA=D:\gcc new\mingw64\bin\ninja.exe"
set "VCVARS=D:\VISUAL STUDIO\MAIN PACK\VC\Auxiliary\Build\vcvars64.bat"
set "JOBS=2"

set "MODE=%~1"
if /i "%MODE%"=="release" set "MODE=1"
if /i "%MODE%"=="rel"     set "MODE=1"
if /i "%MODE%"=="debug"   set "MODE=2"
if /i "%MODE%"=="dbg"     set "MODE=2"
if "%MODE%"=="1" goto :REL
if "%MODE%"=="2" goto :DBG

:MENU
echo ==========================================================
echo   R3R FULL REBUILD  -  pick a target
echo ==========================================================
echo.
echo   [1]  build-release   PUBLISH exe    - R3R probes OFF   (default)
echo   [2]  build           INTERNAL TEST  - R3R probes ON    (daily use)
echo   [0]  cancel
echo.
set "SEL="
set /p "SEL=  Choice [1/2/0] (Enter=1): "
if not defined SEL set "SEL=1"
if "%SEL%"=="0" goto :CANCEL
if "%SEL%"=="2" goto :DBG
goto :REL

:CANCEL
echo   cancelled.
goto :END

rem ------------------------------------------------------------
:REL
echo.
echo ==========================================================
echo   TARGET 1: build-release   PUBLISH exe, R3R probes OFF
echo ==========================================================
echo.
call :ENVCHECK
echo.
echo   Close CodeBuddy now if it is still open, then
pause >nul
echo.
echo ---- [1] delete every object file (force a real full rebuild) ----
if not exist "build-release" mkdir "build-release"
del /s /q "build-release\*.obj" >nul 2>&1
dir /s /b "build-release\*.obj" >nul 2>&1
if errorlevel 1 (echo   OK: no .obj left) else (echo   [WARN] some .obj survived - the binary may be mixed)
echo.
echo ---- [2] configure + gates + ninja -j%JOBS% + link ----
echo   live output: build-release\R3R_release_build.log
echo.
call "%~dp0R3R_release_build.cmd"
echo.
echo ---- [3] result ----
if not exist "build-release\R3R_release_build.done" (
    echo   [FAIL] build-release\R3R_release_build.done is missing - the
    echo   [FAIL] release build did NOT finish. Read
    echo   [FAIL] build-release\R3R_release_build.log
    goto :END
)
type "build-release\R3R_release_build.done"
echo.
rem  The done file may carry a trailing space (echo EXIT_CODE=%RC% > f),
rem  so match on a prefix instead of comparing the whole line.
findstr /B /C:"EXIT_CODE=0" "build-release\R3R_release_build.done" >nul 2>&1
if errorlevel 1 (
    echo   [FAIL] the release build did not succeed.
    echo   90=xcopy  91=vcvars64.bat  92=external libs missing
    echo   93=vcpkg toolchain missing  94=R3R_PROBES_DEFAULT=0 not in build.ninja
    goto :END
)
echo   OK: release exe produced with the R3R probes OFF.
echo   One-off probe override:  set R3R_DBG=1
goto :END

rem ------------------------------------------------------------
rem  TARGET 2 (internal test, probes ON)
rem  Two-phase: the outer phase does the pre-flight and then runs
rem  the work in a child cmd with the whole output redirected to
rem  build\R3R_fullrebuild.log. The inner phase (arg 1 == __go) is
rem  the actual work. That way a dead console can no longer hide
rem  what happened (KI-30).
rem ------------------------------------------------------------
:DBG
if /i "%~2"=="__go" goto :DBG_RUN
echo.
echo ==========================================================
echo   TARGET 2: build   INTERNAL TEST exe, R3R probes ON
echo ==========================================================
echo.
call :ENVCHECK
echo.
if not exist "build\build.ninja" (
    echo   [FATAL] build\build.ninja is missing. This helper does not
    echo   [FATAL] configure from scratch - restore the build dir first.
    goto :END
)
echo   Close CodeBuddy now if it is still open (KI-24: 8 GB box).
echo   The build runs in a child cmd, so its whole output is kept in
echo     build\R3R_fullrebuild.log
echo   and the verdict lands in build\R3R_fullrebuild.done.
echo.
echo   Starting in 15 s - press Ctrl+C NOW to abort.
powershell -NoProfile -Command "for($i=15;$i -ge 1;$i--){Write-Host ('  starting in ' + $i + ' s'); Start-Sleep -Seconds 1}"
echo.
echo ---- [2] running the build in a logged child process ----
if exist "build\R3R_fullrebuild.done" del /q "build\R3R_fullrebuild.done" >nul 2>&1
rem  __go MUST be the first argument (KI-31) - no %MODE% here.
rem  "< nul" makes any prompt inside the child fail fast instead of
rem  blocking with its text hidden in the redirected log.
cmd /c ""%~f0" __go" > "build\R3R_fullrebuild.log" 2>&1 < nul
set "RC=%ERRORLEVEL%"
rem  Did the child actually enter the work phase? Without this test a
rem  child that got stuck in :MENU still reports a plausible code.
findstr /C:"child process" "build\R3R_fullrebuild.log" >nul 2>&1
if errorlevel 1 (
    echo.
    echo   [FATAL] the child log has no work-phase banner - the build
    echo   [FATAL] did NOT run at all. Ignore the exit code below and
    echo   [FATAL] read build\R3R_fullrebuild.log
)
echo.
echo ---- [3] result ----
echo   child exit code = %RC%
if exist "build\R3R_fullrebuild.done" (type "build\R3R_fullrebuild.done") else (echo   build\R3R_fullrebuild.done is MISSING - that is what "the build never finished" looks like; read the log)
echo.
echo   last 40 lines of build\R3R_fullrebuild.log:
powershell -NoProfile -Command "if (Test-Path 'build\R3R_fullrebuild.log') { Get-Content 'build\R3R_fullrebuild.log' -Tail 40 } else { Write-Host '  (log file missing)' }"
goto :END

rem ------------------------------------------------------------
:DBG_RUN
echo R3R full rebuild (internal test) - child process
echo log: build\R3R_fullrebuild.log
echo started %DATE% %TIME%
echo.
echo ---- [1] delete every object file (force a real full rebuild) ----
del /s /q "build\*.obj" >nul 2>&1
dir /s /b "build\*.obj" >nul 2>&1
if errorlevel 1 (echo   OK: no .obj left) else (echo   [WARN] some .obj survived - the binary may be mixed)
echo.
echo ---- [2] sanity: the internal build must keep the probes ON ----
findstr /C:"-DR3R_PROBES_DEFAULT=0" "build\build.ninja" >nul 2>&1
if not errorlevel 1 (
    echo   [FATAL] build\build.ninja carries -DR3R_PROBES_DEFAULT=0.
    echo   [FATAL] the internal test build must keep the probes ON - see KI-28.
    > "build\R3R_fullrebuild.done" echo EXIT_CODE=95
    exit /b 95
)
echo   OK: probes default ON (Debug, no -DR3R_PROBES_DEFAULT override)
echo.
echo ---- [3] TEMP/TMP to D: (C: is nearly full) ----
if not exist "build\tmp" mkdir "build\tmp"
set "TEMP=%~dp0build\tmp"
set "TMP=%~dp0build\tmp"
echo   TEMP=%TEMP%
echo.
echo ---- [4] load MSVC x64 env + ninja -C build -j%JOBS% openttd ----
call "%VCVARS%"
if errorlevel 1 (
    echo   [FATAL] vcvars64.bat failed
    > "build\R3R_fullrebuild.done" echo EXIT_CODE=96
    exit /b 96
)
echo   ninja starting %DATE% %TIME%
echo.
"%NINJA%" -C "build" -j %JOBS% openttd
set "RC=%ERRORLEVEL%"
echo.
echo   ninja ended %DATE% %TIME%  (no line here == killed, not failed)
echo.
echo ---- [5] result ----
> "build\R3R_fullrebuild.done" echo EXIT_CODE=%RC%
if exist "build\openttd.exe" (
    powershell -NoProfile -Command "$e=Get-Item 'build\openttd.exe'; Write-Host ('  openttd.exe : ' + $e.Length + ' bytes   ' + $e.LastWriteTime)"
)
echo   This exe has the R3R probes ON - it is the internal test build.
echo NINJA_EXIT=%RC%
exit /b %RC%

rem ------------------------------------------------------------
:ENVCHECK
powershell -NoProfile -Command "[math]::Round((Get-CimInstance Win32_OperatingSystem).FreePhysicalMemory/1MB,2)" > "%TEMP%\_r3r_free.txt" 2>nul
set /p FREEGB=<"%TEMP%\_r3r_free.txt"
del /q "%TEMP%\_r3r_free.txt" >nul 2>&1
echo   Free physical RAM : %FREEGB% GB    (want ~1.5 GB or more for -j2)
tasklist /fi "imagename eq openttd.exe" 2>nul | find /i "openttd.exe" >nul
if not errorlevel 1 (
    echo   [WARN] openttd.exe is running. Close the game first, otherwise
    echo   [WARN] the link step fails with LNK1168 - the file is locked.
)
exit /b 0

rem ------------------------------------------------------------
:END
echo.
if /i "%~1"=="__go" exit /b 0
if /i "%~2"=="__go" exit /b 0
pause
exit /b 0
