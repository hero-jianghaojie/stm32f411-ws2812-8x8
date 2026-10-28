@echo off
chcp 65001 >nul
setlocal
set PYTHONIOENCODING=utf-8
set PY=C:\Users\30671\.dsh\dsh-runtimes\dsh-primary-runtime\dependencies\python\python.exe
pushd "%~dp0"
echo Listening COM6 for 15s ...
"%PY%" -u tools\watch_serial.py --port COM6 --seconds 15
echo.
pause
popd
