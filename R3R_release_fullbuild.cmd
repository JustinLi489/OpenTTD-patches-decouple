@echo off
setlocal enabledelayedexpansion
title R3R RELEASE FULL BUILD (build-release)
cd /d "%~dp0"

rem ======================================================================
rem  DOUBLE-CLICK FULL REBUILD OF THE PUBLISH (RELEASE) EXE.
rem
rem  What it does, in order:
rem     pre-flight (RAM, openttd.exe not running)
rem     configure build-release (RelWithDebInfo + forced /O2 /Ob2)
rem     3 hard gates  (no /Od, external libs really compiled in,
rem                    -DR3R_PROBES_DEFAULT=0 i.e. probes OFF)
rem     DELETE EVERY .obj          <- this is what makes it a FULL build
rem     ninja -j2 (615 objects + LTCG link, 25-50 min)
rem     verify the produced exe (recently relinked + release-sized)
rem
rem  Why delete every .obj: this box's ninja cannot track header deps
rem  (KI-15: cl prints "Note: including file:" in Chinese), so after any
rem  src\*.h change the stale .obj files link into a MIXED binary that
rem  crashes randomly.  Deleting them all is the only safe reaction.
rem
rem  CLOSE CODEBUDDY FIRST (KI-24): 8 GB box, cl.exe at -j2 plus the IDE
rem  gets paged out and the build freezes.
rem
rem  Usage:  double-click  (= full release build)
rem          R3R_release_fullbuild.cmd --check   (dry run: pre-flight +
rem                                              configure + gates, no
rem                                              .obj deleted, no compile)
rem ======================================================================

set "ROOT=%~dp0"
if "%ROOT:~-1%"=="\" set "ROOT=%ROOT:~0,-1%"

set "BLD=%ROOT%\build-release"
set "NINJA=D:\gcc new\mingw64\bin\ninja.exe"
set "CMAKEEXE=D:\gcc new\mingw64\bin\cmake.exe"
set "VCVARS=D:\VISUAL STUDIO\MAIN PACK\VC\Auxiliary\Build\vcvars64.bat"
set "VCPKGTC=D:\vcpkg-master\scripts\buildsystems\vcpkg.cmake"
set "VCPKGSRC=%ROOT%\build\vcpkg_installed"
set "RUNNER=%ROOT%\R3R_release_fullbuild_run.ps1"
set "LOG=%BLD%\R3R_release_fullbuild.log"
set "DONE=%BLD%\R3R_release_fullbuild.done"

rem  JOBS=2 on purpose (KI-24).  4 makes cl.exe get paged out and freeze.
set "JOBS=2"

set "CHECKONLY="
if /i "%~1"=="--check" set "CHECKONLY=1"

echo ==========================================================
echo   R3R RELEASE FULL BUILD   -^>  build-release\openttd.exe
echo ==========================================================
echo   started: %DATE% %TIME%
if defined CHECKONLY echo   MODE   : DRY RUN --check, nothing will be compiled
echo.

if not exist "%BLD%" mkdir "%BLD%"
del /q "%DONE%" >nul 2>&1

echo ---- [0] pre-flight: tools ----
for %%F in ("%NINJA%" "%CMAKEEXE%" "%VCVARS%" "%VCPKGTC%" "%RUNNER%") do (
    if not exist "%%~F" (
        echo   [FATAL] missing: %%~F
        > "%DONE%" echo EXIT_CODE=93
        goto :END
    )
)
echo   OK: ninja / cmake / vcvars64 / vcpkg toolchain / runner all present

echo.
echo ---- [1] pre-flight: RAM + running game ----
rem  The actual checks live in the helper .ps1 (ASCII only, no quoting hell).
powershell -NoProfile -ExecutionPolicy Bypass -File "%RUNNER%" -Action precheck
set "PC=%ERRORLEVEL%"
if not "%PC%"=="0" (
    echo   [FATAL] pre-flight failed with %PC%
    > "%DONE%" echo EXIT_CODE=%PC%
    goto :END
)

echo.
echo ---- [2] redirect TEMP/TMP to D: (C: is nearly full, LTCG needs GBs) ----
set "TPD=%BLD%\tmp"
if not exist "%TPD%" mkdir "%TPD%"
set "TEMP=%TPD%"
set "TMP=%TPD%"
echo   TEMP=%TEMP%

echo.
echo ---- [3] reuse vcpkg_installed from the Debug build ----
if exist "%BLD%\vcpkg_installed" (
    echo   already present, skip copy
) else (
    if exist "%VCPKGSRC%" (
        xcopy "%VCPKGSRC%" "%BLD%\vcpkg_installed" /E /I /Q /Y >nul
        if errorlevel 1 (
            echo   [FATAL] xcopy vcpkg_installed failed
            > "%DONE%" echo EXIT_CODE=90
            goto :END
        )
        echo   copied from "%VCPKGSRC%"
    ) else (
        echo   WARNING: "%VCPKGSRC%" not found - vcpkg will install fresh
    )
)

echo.
echo ---- [4] load MSVC x64 environment ----
call "%VCVARS%"
if errorlevel 1 (
    echo   [FATAL] vcvars64.bat failed
    > "%DONE%" echo EXIT_CODE=91
    goto :END
)
where cl.exe

echo.
echo ---- [5] cached configure must know about the vcpkg toolchain ----
set "CACHEOK="
if exist "%BLD%\CMakeCache.txt" findstr /C:"CMAKE_TOOLCHAIN_FILE:" "%BLD%\CMakeCache.txt" >nul 2>&1 && set "CACHEOK=1"
if defined CACHEOK (
    echo   OK: cache already carries CMAKE_TOOLCHAIN_FILE
) else (
    if exist "%BLD%\CMakeCache.txt" (
        echo   cache has no CMAKE_TOOLCHAIN_FILE - deleting it for a clean configure
        del /q "%BLD%\CMakeCache.txt"
    )
)

echo.
echo ---- [6] cmake configure (RelWithDebInfo, forced /O2 /Ob2) ----
"%CMAKEEXE%" -S "%ROOT%" -B "%BLD%" -G Ninja ^
  -DCMAKE_MAKE_PROGRAM="%NINJA%" ^
  -DCMAKE_TOOLCHAIN_FILE="%VCPKGTC%" ^
  -DCMAKE_BUILD_TYPE=RelWithDebInfo ^
  -DCMAKE_CXX_FLAGS_RELWITHDEBINFO="/Zi /O2 /Ob2 /DNDEBUG" ^
  -DCMAKE_C_FLAGS_RELWITHDEBINFO="/Zi /O2 /Ob2 /DNDEBUG" ^
  -DOPTION_USE_ASSERTS=ON ^
  -DR3R_PROBES_DEFAULT=0 ^
  -DVCPKG_TARGET_TRIPLET=x64-windows-static
set "CFG=%ERRORLEVEL%"
echo   CMAKE_CONFIGURE_EXIT=%CFG%
if not "%CFG%"=="0" (
    > "%DONE%" echo EXIT_CODE=%CFG%
    goto :END
)

echo.
echo ---- [7] HARD GATE a: no /Od (i.e. the release flags really applied) ----
rem  Only look at real compiler FLAGS lines, so an unrelated "/Od" somewhere
rem  else in build.ninja cannot abort a perfectly good release build.
findstr /R /C:"FLAGS = .*/Od" "%BLD%\build.ninja" >nul 2>&1
if not errorlevel 1 (
    echo   [FATAL] build.ninja contains /Od - this would be a DEBUG build.
    > "%DONE%" echo EXIT_CODE=99
    goto :END
)
echo   OK: /Od absent
findstr /R /C:"FLAGS = .*/O2" "%BLD%\build.ninja" >nul 2>&1
if errorlevel 1 (
    echo   [FATAL] no FLAGS line carries /O2 - the exe would be unoptimised.
    > "%DONE%" echo EXIT_CODE=99
    goto :END
)
echo   OK: /O2 present

echo.
echo ---- [8] HARD GATE b: external libs really compiled in ----
rem  Without the vcpkg toolchain these defines vanish silently and the exe
rem  cannot read zlib/lzma/zstd savegames.  Never let that reach the link.
set "MISS="
findstr /C:"-DWITH_ZLIB "      "%BLD%\build.ninja" >nul 2>&1 || set "MISS=%MISS% WITH_ZLIB"
findstr /C:"-DWITH_LIBLZMA "   "%BLD%\build.ninja" >nul 2>&1 || set "MISS=%MISS% WITH_LIBLZMA"
findstr /C:"-DWITH_ZSTD "      "%BLD%\build.ninja" >nul 2>&1 || set "MISS=%MISS% WITH_ZSTD"
findstr /C:"-DWITH_LZO "       "%BLD%\build.ninja" >nul 2>&1 || set "MISS=%MISS% WITH_LZO"
findstr /C:"-DWITH_PNG "       "%BLD%\build.ninja" >nul 2>&1 || set "MISS=%MISS% WITH_PNG"
findstr /C:"-DWITH_OPUSFILE "  "%BLD%\build.ninja" >nul 2>&1 || set "MISS=%MISS% WITH_OPUSFILE"
if not "%MISS%"=="" (
    echo   [FATAL] these compile defines are missing:%MISS%
    echo   [FATAL] the exe would be unable to read compressed savegames.
    > "%DONE%" echo EXIT_CODE=92
    goto :END
)
echo   OK: zlib/lzma/zstd/lzo/png/opusfile all compiled in

echo.
echo ---- [9] HARD GATE c: R3R probes must default OFF in the published exe ----
findstr /C:"-DR3R_PROBES_DEFAULT=0" "%BLD%\build.ninja" >nul 2>&1
if errorlevel 1 (
    echo   [FATAL] build.ninja does not carry -DR3R_PROBES_DEFAULT=0
    echo   [FATAL] the published exe would ship with the R3R probes ON.
    > "%DONE%" echo EXIT_CODE=94
    goto :END
)
echo   OK: probes default OFF

if defined CHECKONLY (
    echo.
    echo ==========================================================
    echo   DRY RUN OK - all pre-flight checks and gates passed.
    echo   Run this script WITHOUT --check to do the real build.
    echo ==========================================================
    > "%DONE%" echo EXIT_CODE=0
    >> "%DONE%" echo MODE=CHECK_ONLY
    goto :END
)

echo.
echo ---- [10] delete EVERY object file (this is what makes it FULL) ----
del /s /q "%BLD%\*.obj" >nul 2>&1
set "OBJLEFT=0"
for /f %%c in ('dir /s /b "%BLD%\*.obj" 2^>nul ^| find /c /v ""') do set "OBJLEFT=%%c"
if "%OBJLEFT%"=="0" (
    echo   OK: no .obj left, every object will be rebuilt
) else (
    echo   [WARN] %OBJLEFT% .obj survived - the binary could be mixed, see KI-15
)

echo.
echo ---- [11] ninja -j%JOBS% openttd   (FULL rebuild + LTCG link) ----
echo   expect roughly 25-50 minutes; live output below
echo.
powershell -NoProfile -ExecutionPolicy Bypass -File "%RUNNER%" -Action build -Ninja "%NINJA%" -BuildDir "%BLD%" -Jobs %JOBS% -Log "%LOG%"
set "RC=%ERRORLEVEL%"
echo.
echo   NINJA_EXIT=%RC%

echo.
echo ---- [12] verify the produced exe ----
powershell -NoProfile -ExecutionPolicy Bypass -File "%RUNNER%" -Action verify -Exe "%BLD%\openttd.exe"
set "VF=%ERRORLEVEL%"

rem  Do NOT write "if A if B (...) else (...)" here: cmd binds the else to the
rem  INNER if, so a build that fails at ninja falls through both branches and
rem  never records its exit code.  Same trap for unescaped parentheses inside
rem  a block - they shift the block boundaries.  Flatten both.
set "OKBUILD="
if "%RC%"=="0" if "%VF%"=="0" set "OKBUILD=1"
set "FINAL=%RC%"
if "%FINAL%"=="0" set "FINAL=97"

if defined OKBUILD (
    > "%DONE%" echo EXIT_CODE=0
    echo.
    echo ==========================================================
    echo   BUILD OK  -^>  %BLD%\openttd.exe
    echo   probes OFF, external libs in, size in the release range.
    echo   This exe is ready to hand to the players.
    echo ==========================================================
) else (
    > "%DONE%" echo EXIT_CODE=%FINAL%
    echo.
    echo ==========================================================
    echo   BUILD FAILED   ninja=%RC%   verify=%VF%
    echo   90=xcopy  91=vcvars64  92=external libs  93=tool missing
    echo   94=probes flag  96=pre-flight  97=ninja/missing dir or verify
    echo   99=/Od found - debug flags
    echo   full compiler output: %LOG%
    echo ==========================================================
)

:END
echo.
echo   done file : %DONE%
if exist "%DONE%" for /f "usebackq delims=" %%L in ("%DONE%") do echo   %%L
echo   log file  : %LOG%
echo.
echo   This window stays open so you can read the result.
pause
endlocal
exit /b 0
