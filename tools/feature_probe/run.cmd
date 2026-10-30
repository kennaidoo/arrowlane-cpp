@echo off
setlocal
set "ROOT=%~dp0..\.."
call "%ROOT%\scripts\env.cmd" || exit /b 1
cd /d "%~dp0"
for %%f in (*.cpp) do (
  cl /nologo /std:c++latest /EHsc /W4 /c "%%f" > "%%~nf.log" 2>&1
  if errorlevel 1 ( echo FAIL %%~nf ) else ( echo PASS %%~nf )
)
