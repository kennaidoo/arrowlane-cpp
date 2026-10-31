@echo off
rem Compile and run the VPCLMULQDQ probe with MSVC.
rem Usage: probe-vpclmul.cmd
setlocal
set "ROOT=%~dp0.."
call "%ROOT%\scripts\env.cmd" || exit /b 1
if not exist "%ROOT%\build" mkdir "%ROOT%\build"
cl /nologo /std:c++latest /EHsc /arch:AVX2 /Fe:"%ROOT%\build\vpclmul_probe.exe" "%ROOT%\tools\feature_probe\vpclmul.cpp" || exit /b 1
"%ROOT%\build\vpclmul_probe.exe"
