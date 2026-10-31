@echo off
rem Measure the size of the CLI under four configurations.
rem Usage: measure-size.cmd
setlocal
set "ROOT=%~dp0.."
call "%ROOT%\scripts\env.cmd" || exit /b 1
echo == MinSizeRel with zstd ==
"%CMAKE%" -S "%ROOT%" -B "%ROOT%\build-size" -G Ninja -DCMAKE_BUILD_TYPE=MinSizeRel -DCMAKE_MAKE_PROGRAM="%NINJA%" >nul || exit /b 1
"%CMAKE%" --build "%ROOT%\build-size" --target arrowlane-cpp >nul || exit /b 1
for %%F in ("%ROOT%\build-size\arrowlane-cpp.exe") do echo size=%%~zF
echo == MinSizeRel without zstd ==
"%CMAKE%" -S "%ROOT%" -B "%ROOT%\build-nozstd" -G Ninja -DCMAKE_BUILD_TYPE=MinSizeRel -DAL_BUILD_ZSTD=OFF -DCMAKE_MAKE_PROGRAM="%NINJA%" >nul || exit /b 1
"%CMAKE%" --build "%ROOT%\build-nozstd" --target arrowlane-cpp >nul || exit /b 1
for %%F in ("%ROOT%\build-nozstd\arrowlane-cpp.exe") do echo size=%%~zF
echo == Release without zstd ==
"%CMAKE%" -S "%ROOT%" -B "%ROOT%\build-nozstd-rel" -G Ninja -DCMAKE_BUILD_TYPE=Release -DAL_BUILD_ZSTD=OFF -DCMAKE_MAKE_PROGRAM="%NINJA%" >nul || exit /b 1
"%CMAKE%" --build "%ROOT%\build-nozstd-rel" --target arrowlane-cpp >nul || exit /b 1
for %%F in ("%ROOT%\build-nozstd-rel\arrowlane-cpp.exe") do echo size=%%~zF
