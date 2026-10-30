@echo off
setlocal
set "ROOT=%~dp0.."
call "%ROOT%\scripts\env.cmd" || exit /b 1
"%CMAKE%" -S "%ROOT%" -B "%ROOT%\build" -G Ninja -DCMAKE_BUILD_TYPE=Release -DCMAKE_MAKE_PROGRAM="%NINJA%" || exit /b 1
"%CMAKE%" --build "%ROOT%\build" || exit /b 1
"%CTEST%" --test-dir "%ROOT%\build" --output-on-failure || exit /b 1
echo RESULT: PASS
