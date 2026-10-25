@echo off
rem  Incremental rebuild. The R3R_inc_guard.ps1 pre-check drops every *.obj as soon as a
rem  src header (or a lang txt) is newer than the newest object file, because this tree's
rem  ninja has no header dependencies for most objects - an incremental link would then mix
rem  objects built against different class layouts and crash on savegame load (KI-183).
setlocal
cd /d "%~dp0"
set "NINJA=D:\gcc new\mingw64\bin\ninja.exe"
set "VCVARS=D:\VISUAL STUDIO\MAIN PACK\VC\Auxiliary\Build\vcvars64.bat"
set "LOG=%~dp0build\R3R_incbuild.log"
set "DONE=%~dp0build\R3R_incbuild.done"
set "GUARDLOG=%~dp0build\R3R_incbuild.guard.log"

if exist "%DONE%" del /q "%DONE%" >nul 2>&1

rem  Only the build\openttd.exe of THIS tree can block the link. The player usually
rem  runs a packaged copy from another folder (e.g. D:\R3R-OpenTTD-*), which must
rem  not abort the build any more.
powershell -NoProfile -Command "$t='%~dp0build\openttd.exe'; $p=Get-Process -Name openttd -ErrorAction SilentlyContinue; if ($p -and ($p | Where-Object { $_.Path -and ($_.Path -ieq $t) })) { exit 1 } else { exit 0 }"
if not errorlevel 1 goto :gamecheck_done
    > "%DONE%" echo EXIT_CODE=97
    echo [FATAL] %~dp0build\openttd.exe is running - close the game first
    exit /b 97
:gamecheck_done

rem --- KI-183 guard: never link objects built against different class layouts --------
if not exist "build" mkdir "build"
powershell -NoProfile -ExecutionPolicy Bypass -File "%~dp0R3R_inc_guard.ps1" > "%GUARDLOG%" 2>&1
set "GRC=%ERRORLEVEL%"
type "%GUARDLOG%"
if "%GRC%"=="2" echo [WARN] header change detected: all objects were dropped, running a FULL rebuild
rem -----------------------------------------------------------------------------------

if not exist "build\tmp" mkdir "build\tmp"
set "TEMP=%~dp0build\tmp"
set "TMP=%~dp0build\tmp"

call "%VCVARS%" >nul 2>&1
"%NINJA%" -C "build" -j 2 openttd > "%LOG%" 2>&1
set "RC=%ERRORLEVEL%"

> "%DONE%" echo EXIT_CODE=%RC%
exit /b %RC%
