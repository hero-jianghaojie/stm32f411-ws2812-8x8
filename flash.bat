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
for %%f in (out\*.hex) do (
  set /a N+=1
  set "F!N!=%%f"
  set "NM!N!=%%~nf"
  set "NM=%%~nf"
  if /i "!NM:~0,7!"=="ws2812_" (
    set "CAT!N!=1"
  ) else if /i "!NM:~0,4!"=="oled" (
    set "CAT!N!=2"
  ) else (
    set "CAT!N!=3"
  )
)

echo ==================================================
echo   Images in out\
echo ==================================================
for %%L in (1 2 3) do (
  if "%%L"=="1" echo   [ WS2812 matrix ]
  if "%%L"=="2" echo   [ OLED 128x64 ]
  if "%%L"=="3" echo   [ diagnose / tools ]
  for /l %%i in (1,1,%N%) do (
    if "!CAT%%i!"=="%%L" (
      set "IDX=%%i"
      if %%i LSS 10 (echo     %%i^)  !NM%%i!) else (echo    %%i^)  !NM%%i!)
    )
  )
)
echo   --------------------------------------------------
if exist .last_choice (
  set /p LAST=<.last_choice
  echo   Last choice: !LAST!
)
echo   Number + Enter to flash; plain Enter repeats last
echo ==================================================
set "SEL="
set /p "SEL=Choice: "
if not defined SEL set SEL=!LAST!
if not defined SEL set SEL=1
set "IMG="
set "NAME="
call :pick %SEL%
if not defined IMG (
  echo   [ERROR] no item %SEL%
  pause
  exit /b 1
)
(echo !SEL!)>.last_choice

set "PINA="
call :maybe_pin

echo.
echo ==================================================
echo   Image : %IMG%
echo   Port  : %PORT%   Baud: %BAUD%
if defined PINA echo   Pin   : %PINA%
echo   STEP 1  close FlyMcu (it locks the port)
echo   STEP 2  hold BOOT0, tap NRST, release NRST,
echo           then release BOOT0
echo   STEP 3  wait for the log below
echo ==================================================
echo.

"%PY%" -u tools\flash_an3155.py --port %PORT% --baud %BAUD% --no-verify --go %PINA% --hex "%IMG%" --wait 40

echo.
echo ==================================================
echo   After "GO ok": BOOT0 to GND, tap NRST
echo ==================================================
pause
popd
goto :eof

:pick
set "IMG=!F%~1!"
set "NAME=!NM%~1!"
goto :eof

:maybe_pin
set "PRE=!NAME:~0,7!"
if /i not "!PRE!"=="ws2812_" goto :eof
echo.
echo   ---- WS2812 data pin ----
echo     0 = BROADCAST   1 = PA1   2 = PA2   3 = PA3   4 = PA4
echo     5 = PA5         6 = PA6   7 = PA7   8 = PA8   9 = PA15
echo    10 = PB0        11 = PB1  12 = PB2  13 = PB3  14 = PB4
echo    15 = PB5        16 = PB6  17 = PB7  18 = PB8  19 = PB9
echo    20 = PB10       21 = PB11 22 = PB12 23 = PB13 24 = PB14
echo    25 = PB15
echo   --------------------------------------------------
if exist .last_pin (
  set /p LASTPIN=<.last_pin
  echo   Last pin: !LASTPIN!
)
echo   Note: only ws2812_sel reads this; other builds are fixed.
set "SELP="
set /p "SELP=Pin [0-25, Enter = last/0]: "
if not defined SELP set "SELP=!LASTPIN!"
if not defined SELP set "SELP=0"
(echo !SELP!)>.last_pin
set "PINA=--pin-after !SELP!"
goto :eof
