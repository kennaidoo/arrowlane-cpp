@echo off
setlocal
set "ROOT=%~dp0.."
call "%ROOT%\scripts\env.cmd" || exit /b 1
rem Release lives in build-release, so a Debug tree in build stays intact and
rem the two configurations never overwrite each other.
"%CMAKE%" -S "%ROOT%" -B "%ROOT%\build-release" -G Ninja -DCMAKE_BUILD_TYPE=Release -DCMAKE_MAKE_PROGRAM="%NINJA%" || exit /b 1
"%CMAKE%" --build "%ROOT%\build-release" || exit /b 1
"%CTEST%" --test-dir "%ROOT%\build-release" --output-on-failure || exit /b 1
echo RESULT: PASS
