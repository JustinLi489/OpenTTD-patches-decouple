@echo off
rem ============================================================
rem  Double-clickable entry point: start the R3R full rebuild in
rem  a separate minimized window and return immediately, so the
rem  build survives closing this window / the editor / CodeBuddy.
rem
rem  It is only a thin wrapper around:
rem      R3R_fullbuild.cmd detach
rem  See R3R_fullbuild.cmd for the exit codes and the log paths.
rem  ASCII only - cmd.exe reads .cmd with the OEM codepage (936).
rem ============================================================
setlocal
cd /d "%~dp0"
if not exist "R3R_fullbuild.cmd" (
    echo [FATAL] R3R_fullbuild.cmd is missing next to this file.
    pause
    exit /b 1
)
call "%~dp0R3R_fullbuild.cmd" detach
echo.
echo   Press any key to close this window.
pause >nul
exit /b 0
