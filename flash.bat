@echo off
chcp 65001 >nul
setlocal
set PYTHONIOENCODING=utf-8
pushd "%~dp0"

rem Prefer the bundled interpreter (it already has pyserial);
rem otherwise use python from PATH (needs: pip install pyserial).
set PY=C:\Users\30671\.dsh\dsh-runtimes\dsh-primary-runtime\dependencies\python\python.exe
if not exist "%PY%" set PY=python

echo ================================================
echo  STEP 1  close FlyMcu (it locks COM6)
echo  STEP 2  hold BOOT0, tap NRST, release NRST,
echo          then release BOOT0
echo  STEP 3  wait for the log below
echo ================================================
echo.

"%PY%" -u tools/flash_an3155.py --port COM6 --no-verify --go --hex out/ws2812_modes.hex --wait 40

echo.
echo ================================================
echo  After "GO ok":  BOOT0 to GND, tap NRST
echo  Then run watch.bat to see the serial output
echo ================================================
pause
popd
