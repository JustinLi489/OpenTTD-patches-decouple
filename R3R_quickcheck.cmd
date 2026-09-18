@echo off
setlocal
cd /d "%~dp0"

rem ============================================================
rem  R3R QUICK CHECK - compile only the translation unit(s) you
rem  actually edited.
rem
rem  Why this exists (KI-24 + KI-15):
rem    The internal test tree build\ is a Debug build with the R3R
rem    probes ON. A real full rebuild is 600+ compiles at -j2, i.e.
rem    25-40 minutes, and it only runs safely with CodeBuddy closed
rem    (8 GB box). That is far too heavy just to answer "does my
rem    edit compile?".
rem    A single .obj needs only its own compile + the few generated
rem    headers, so it validates the change in a minute or two at a
rem    fraction of the memory.
rem
rem  This is NOT a substitute for a full rebuild after a src\*.h
rem  change (KI-15: stale .obj get linked into a mixed binary), it
rem  is only the fast pre-flight before one.
rem
rem  Usage:  R3R_quickcheck.cmd
rem          R3R_quickcheck.cmd CMakeFiles/openttd_lib.dir/src/train_cmd.cpp.obj
rem ============================================================

set "NINJA=D:\gcc new\mingw64\bin\ninja.exe"
set "VCVARS=D:\VISUAL STUDIO\MAIN PACK\VC\Auxiliary\Build\vcvars64.bat"

set "TARGET=%~1"
if "%TARGET%"=="" set "TARGET=CMakeFiles/openttd_lib.dir/src/train_cmd.cpp.obj"

if not exist "build\build.ninja" (
    echo [FATAL] build\build.ninja is missing - this helper does not configure.
    exit /b 97
)

set "TEMP=%~dp0build\tmp"
set "TMP=%~dp0build\tmp"
if not exist "%TEMP%" mkdir "%TEMP%"

call "%VCVARS%" >nul 2>&1
if errorlevel 1 (
    echo [FATAL] vcvars64.bat failed
    exit /b 96
)

echo ---- quick check: %TARGET% ----
"%NINJA%" -C "build" -j 2 "%TARGET%"
set "RC=%ERRORLEVEL%"
echo.
echo NINJA_EXIT=%RC%
exit /b %RC%
