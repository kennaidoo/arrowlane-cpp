@echo off
rem Usage: build.cmd [Debug|Release]  (default Debug)
rem Release lands in build-release so the debug tree stays intact.
setlocal
set "ROOT=%~dp0.."
set "CONFIG=%~1"
if "%CONFIG%"=="" set "CONFIG=Debug"
set "BUILD=%ROOT%\build"
if /I "%CONFIG%"=="Release" set "BUILD=%ROOT%\build-release"
call "%ROOT%\scripts\env.cmd" || exit /b 1
"%CMAKE%" -S "%ROOT%" -B "%BUILD%" -G Ninja -DCMAKE_BUILD_TYPE=%CONFIG% -DCMAKE_MAKE_PROGRAM="%NINJA%" || exit /b 1
"%CMAKE%" --build "%BUILD%"
