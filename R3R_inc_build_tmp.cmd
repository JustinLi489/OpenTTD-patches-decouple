@echo off
rem R3R: temporary incremental build helper (delete after use).
call "D:\VISUAL STUDIO\MAIN PACK\VC\Auxiliary\Build\vcvars64.bat" >nul
cd /d "d:\sourcecode of JGRPP"
cmake --build build -j 2 > build\R3R_inc_build.log 2>&1
echo EXIT_CODE=%ERRORLEVEL%
