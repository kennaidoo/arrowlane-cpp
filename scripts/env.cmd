@echo off
set "BT=C:\Program Files (x86)\Microsoft Visual Studio\18\BuildTools"
call "%BT%\VC\Auxiliary\Build\vcvars64.bat" >nul
if errorlevel 1 exit /b 1
set "CMAKE=%BT%\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe"
set "NINJA=%BT%\Common7\IDE\CommonExtensions\Microsoft\CMake\Ninja\ninja.exe"
set "CTEST=%BT%\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\ctest.exe"
exit /b 0
