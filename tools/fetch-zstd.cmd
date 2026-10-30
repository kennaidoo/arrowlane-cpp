@echo off
setlocal
set "ROOT=%~dp0.."
set "VERSION=1.5.7"
set "SHA256=eb33e51f49a15e023950cd7825ca74a4a2b43db8354825ac24fc1b7ee09e6fa3"
if exist "%ROOT%\vendor\zstd\lib\zstd.h" (
  echo vendor/zstd already present
  exit /b 0
)
powershell -NoProfile -Command ^
  "$ErrorActionPreference='Stop';" ^
  "$url='https://github.com/facebook/zstd/releases/download/v%VERSION%/zstd-%VERSION%.tar.gz';" ^
  "$tar=Join-Path $env:TEMP 'zstd.tar.gz';" ^
  "Invoke-WebRequest -Uri $url -OutFile $tar;" ^
  "$hash=(Get-FileHash -Algorithm SHA256 $tar).Hash.ToLower();" ^
  "if ($hash -ne '%SHA256%') { throw ('zstd checksum mismatch: ' + $hash) };" ^
  "$work=Join-Path $env:TEMP 'zstd-src';" ^
  "if (Test-Path $work) { Remove-Item $work -Recurse -Force };" ^
  "New-Item -ItemType Directory -Force $work | Out-Null;" ^
  "& (Join-Path $env:SystemRoot 'System32\tar.exe') -xzf $tar -C $work 'zstd-%VERSION%/lib' 'zstd-%VERSION%/build';" ^
  "New-Item -ItemType Directory -Force '%ROOT%\vendor\zstd' | Out-Null;" ^
  "Move-Item (Join-Path $work 'zstd-%VERSION%\lib') '%ROOT%\vendor\zstd\lib';" ^
  "Move-Item (Join-Path $work 'zstd-%VERSION%\build') '%ROOT%\vendor\zstd\build';" ^
  "Remove-Item $work -Recurse -Force"
if errorlevel 1 exit /b 1
