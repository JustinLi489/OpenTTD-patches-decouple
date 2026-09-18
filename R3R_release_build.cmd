@echo off
setlocal
title R3R RelWithDebInfo build (build-release)

rem ============================================================
rem  R3R performance build: RelWithDebInfo + /O2 /Ob2  (= Release codegen)
rem  Writes into a SEPARATE dir (build-release) so the existing
rem  Debug build in .\build is left completely untouched.
rem
rem  Why: build\ was CMAKE_BUILD_TYPE=Debug  =>  /Zi /Ob0 /Od /RTC1 -MTd
rem  _DEBUG DBG_ASSERTS, i.e. ZERO optimisation and no inlining.
rem  All previous perf A/B numbers (KI-14/KI-19/KI-20) were
rem  "unoptimised binary vs official Release binary" -> invalid.
rem  ============================================================

set "ROOT=d:\sourcecode of JGRPP"
set "BLD=%ROOT%\build-release"
set "LOG=%BLD%\R3R_release_build.log"
set "DONE=%BLD%\R3R_release_build.done"
set "NINJA=D:\gcc new\mingw64\bin\ninja.exe"
set "CMAKEEXE=D:\gcc new\mingw64\bin\cmake.exe"
rem Parallelism. This box only has 8GB RAM and the IDE alone holds ~3.6GB,
rem so -j4 makes cl.exe get paged out and the build freezes (observed
rem 2026-09-13: everything stalled at 546/978 on tiny files).
rem Keep at 2 unless the RAM hogs are closed.
set "JOBS=2"
set "VCVARS=D:\VISUAL STUDIO\MAIN PACK\VC\Auxiliary\Build\vcvars64.bat"
set "VCPKGSRC=%ROOT%\build\vcpkg_installed"
rem ============================================================
rem  FIX 2026-09-13 (2nd round). The 1st round omitted the vcpkg
rem  toolchain file. -DVCPKG_TARGET_TRIPLET alone does NOTHING:
rem  vcpkg.cmake is never included, find_package(ZLIB/LibLZMA/
rem  zstd/LZO/PNG/OpusFile) all fail, the cache stores -NOTFOUND,
rem  configure still reports success, and the produced exe can
rem  only read UNCOMPRESSED savegames. Symptom in game:
rem    "loader for 'zstd' is not available"
rem    "loader for 'lzma' is not available"
rem  (zlib was lost too, plus PNG screenshots and OpusFile music.)
rem ============================================================
set "VCPKGTC=D:\vcpkg-master\scripts\buildsystems\vcpkg.cmake"

if not exist "%BLD%" mkdir "%BLD%"
del /q "%DONE%" >nul 2>&1

if /i "%~1"=="__body" goto :BODY

rem ---- re-invoke self with everything captured to the log ----
rem  "< nul": the body has no prompts today, but this guarantees that a
rem  future one fails fast instead of blocking with its text hidden
rem  in the log (same trap that produced KI-31 in the parent script).
"%COMSPEC%" /c ""%~f0" __body" > "%LOG%" 2>&1 < nul
exit /b 0

:BODY
echo ==========================================================
echo   R3R RELWITHDEBINFO BUILD  (separate dir: build-release)
echo ==========================================================
date /t
time /t

echo.
echo ---- [1] RAM snapshot ----
powershell -NoProfile -Command "$os=Get-CimInstance Win32_OperatingSystem; Write-Host ('  Free RAM GB : ' + [math]::Round($os.FreePhysicalMemory/1MB,2)); Write-Host ('  Total RAM GB: ' + [math]::Round($os.TotalVisibleMemorySize/1MB,1))"

echo.
echo ---- [2] redirect TEMP/TMP to D: (C: has ~0.7GB, LTCG needs GBs) ----
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
            exit /b 90
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
    echo [FATAL] vcvars64.bat failed
    > "%DONE%" echo EXIT_CODE=91
    exit /b 91
)
where cl.exe

if not exist "%VCPKGTC%" (
    echo [FATAL] vcpkg toolchain not found: %VCPKGTC%
    > "%DONE%" echo EXIT_CODE=93
    exit /b 93
)

echo.
echo ---- [4b] make sure the cached configure knows about the vcpkg toolchain ----
set "CACHEOK="
if exist "%BLD%\CMakeCache.txt" findstr /C:"CMAKE_TOOLCHAIN_FILE:" "%BLD%\CMakeCache.txt" >nul 2>&1 && set "CACHEOK=1"
if defined CACHEOK (
    echo   OK: cache already has CMAKE_TOOLCHAIN_FILE
) else (
    if exist "%BLD%\CMakeCache.txt" (
        echo   cache has no CMAKE_TOOLCHAIN_FILE - deleting it for a clean configure
        del /q "%BLD%\CMakeCache.txt"
    )
)

echo.
echo ---- [5] cmake configure (RelWithDebInfo, forced /O2 /Ob2) ----
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
echo CMAKE_CONFIGURE_EXIT=%CFG%
if not "%CFG%"=="0" (
    > "%DONE%" echo EXIT_CODE=%CFG%
    exit /b %CFG%
)

echo.
echo ---- [5b] verify the generated flags (must NOT contain /Od) ----
if exist "%BLD%\build.ninja" (
    powershell -NoProfile -Command "$m = Select-String -Path '%BLD%\build.ninja' -Pattern 'FLAGS = ' | Select-Object -First 1; if ($m) { Write-Host ('  FIRST FLAGS => ' + $m.Line.Trim()); if ($m.Line -match '/Od') { Write-Host '  *** WARNING: STILL DEBUG FLAGS ***' } else { Write-Host '  OK: /Od absent' } } else { Write-Host '  build.ninja has no FLAGS line?' }"
)

echo.
echo ---- [5c] HARD GATE: external libs must actually be compiled in ----
rem If the vcpkg toolchain is missing these defines silently vanish and the
rem exe cannot open zlib/lzma/zstd savegames. Never let that reach the link.
set "MISS="
findstr /C:"-DWITH_ZLIB " "%BLD%\build.ninja" >nul 2>&1 || set "MISS=%MISS% WITH_ZLIB"
findstr /C:"-DWITH_LIBLZMA " "%BLD%\build.ninja" >nul 2>&1 || set "MISS=%MISS% WITH_LIBLZMA"
findstr /C:"-DWITH_ZSTD " "%BLD%\build.ninja" >nul 2>&1 || set "MISS=%MISS% WITH_ZSTD"
findstr /C:"-DWITH_LZO " "%BLD%\build.ninja" >nul 2>&1 || set "MISS=%MISS% WITH_LZO"
findstr /C:"-DWITH_PNG " "%BLD%\build.ninja" >nul 2>&1 || set "MISS=%MISS% WITH_PNG"
findstr /C:"-DWITH_OPUSFILE " "%BLD%\build.ninja" >nul 2>&1 || set "MISS=%MISS% WITH_OPUSFILE"
if not "%MISS%"=="" (
    echo   [FATAL] these compile defines are missing:%MISS%
    echo   [FATAL] the vcpkg libraries were not found, so the exe would be unable
    echo   [FATAL] to read zlib/lzma/zstd compressed savegames. Aborting BEFORE
    echo   [FATAL] the long build - check CMAKE_TOOLCHAIN_FILE / triplet.
    > "%DONE%" echo EXIT_CODE=92
    exit /b 92
)
echo   OK: zlib/lzma/zstd/lzo/png/opusfile all compiled in

echo.
echo ---- [5d] HARD GATE: R3R probes must default OFF in the published exe ----
rem KI-28: build\ = internal test (probes ON), build-release = published
rem build (probes OFF). If this define is missing the shipped exe would
rem carry the probe overhead and write R3R_debug.log behind the player's back.
findstr /C:"-DR3R_PROBES_DEFAULT=0" "%BLD%\build.ninja" >nul 2>&1
if errorlevel 1 (
    echo   [FATAL] build.ninja does not carry -DR3R_PROBES_DEFAULT=0
    echo   [FATAL] the published exe would ship with the R3R probes ON.
    > "%DONE%" echo EXIT_CODE=94
    exit /b 94
)
echo   OK: probes default OFF (runtime override: set R3R_DBG=1)

echo.
echo ---- [6] ninja -C build-release -j %JOBS% openttd  (FULL BUILD) ----
echo   (615 objects + LTCG link; expect roughly 25-50 minutes)
"%NINJA%" -C "%BLD%" -j %JOBS% openttd
set "RC=%ERRORLEVEL%"
echo NINJA_EXIT=%RC%

echo.
echo ---- [7] result ----
if exist "%BLD%\openttd.exe" (
    powershell -NoProfile -Command "$e=Get-Item '%BLD%\openttd.exe'; Write-Host ('  openttd.exe : ' + $e.Length + ' bytes   ' + $e.LastWriteTime)"
    powershell -NoProfile -Command "$b=[IO.File]::ReadAllBytes('%BLD%\openttd.exe'); $s=[Text.Encoding]::ASCII.GetString($b); foreach($n in @('ucrtbased.dll','vcruntime140d.dll','msvcp140d.dll')){ if($s.Contains($n)){ Write-Host ('  BAD: still links Debug CRT  ' + $n) } else { Write-Host ('  ok, not linked: ' + $n) } }"
) else (
    echo   openttd.exe NOT produced
)

> "%DONE%" echo EXIT_CODE=%RC%
echo ==========================================================
echo   BUILD FINISHED  rc=%RC%
echo ==========================================================
exit /b %RC%
