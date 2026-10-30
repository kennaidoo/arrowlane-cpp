@echo off
rem Build once:
rem   docker build -f tools/Dockerfile.test -t arrowlane-cpp-test:wolfi .
rem Then run the whole suite in the container:
rem   tools\run-tests.cmd
rem seccomp must be unconfined so setarch -R can turn ASLR off for the test
rem processes; AddressSanitizer aborts at startup without it on this kernel.
setlocal
set "ROOT=%~dp0.."
docker run --rm --security-opt seccomp=unconfined ^
  -e CASES=%CASES% ^
  -e ROWS=%ROWS% ^
  -v "%ROOT%:/src:ro" ^
  -v "%ROOT%\..\arrowlane:/arrowlane:ro" ^
  -v "%ROOT%\build-wolfi:/tmp/build" ^
  arrowlane-cpp-test:wolfi
