@echo off
chcp 65001 >nul
setlocal enabledelayedexpansion
pushd "%~dp0"

set PY=C:\Users\30671\.dsh\dsh-runtimes\dsh-primary-runtime\dependencies\python\python.exe
if not exist "%PY%" set PY=python
set PYTHONIOENCODING=utf-8

if not defined PORT set PORT=COM6
if not defined BAUD set BAUD=115200

set N=0
echo ==================================================
echo   Images in out\
echo ==================================================
for %%f in (out\*.hex) do (
  set /a N+=1
  set "F!N!=%%f"
  echo   !N!^) %%~nf
)
if %N%==0 (
  echo   [ERROR] no .hex in out\
  pause
  exit /b 1
)
echo   --------------------------------------------------
if exist .last_choice (
  set /p LAST=<.last_choice
  echo   Last choice: !LAST!
)
echo   Type a number then Enter; plain Enter repeats the last choice
echo ==================================================
set "SEL="
set /p "SEL=Choice: "
if not defined SEL set SEL=!LAST!
if not defined SEL set SEL=1
set "IMG=!F%SEL%!"
if not defined IMG (
  echo   [ERROR] no item %SEL%
  pause
  exit /b 1
)
(echo !SEL!)>.last_choice

echo.
echo ==================================================
echo   Image : %IMG%
echo   Port  : %PORT%   Baud: %BAUD%
echo   STEP 1  close FlyMcu (it locks the port)
echo   STEP 2  hold BOOT0, tap NRST, release NRST,
echo           then release BOOT0
echo   STEP 3  wait for the log below
echo ==================================================
echo.

"%PY%" -u tools\flash_an3155.py --port %PORT% --baud %BAUD% --no-verify --go --hex "%IMG%" --wait 40

echo.
echo ==================================================
echo   After "GO ok": BOOT0 to GND, tap NRST
echo   Run watch.bat to see the serial output
echo ==================================================
pause
popd
