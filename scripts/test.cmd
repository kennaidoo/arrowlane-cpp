@echo off
setlocal
set "ROOT=%~dp0.."
call "%ROOT%\scripts\env.cmd" || exit /b 1
"%CTEST%" --test-dir "%ROOT%\build" --output-on-failure
