@echo off
chcp 65001 >nul
setlocal enabledelayedexpansion
pushd "%~dp0"
set PY=C:\Users\30671\.dsh\dsh-runtimes\dsh-primary-runtime\dependencies\python\python.exe
if not exist "%PY%" set PY=python
set PYTHONIOENCODING=utf-8

if not defined PORT set PORT=COM6

echo ==================================================
echo   WS2812 data pin - writes to Flash (0x08060000)
echo ==================================================
echo     0 = BROADCAST (all candidate pins, default)
echo     1 = PA1     2 = PA2     3 = PA3     4 = PA4
echo     5 = PA5     6 = PA6     7 = PA7     8 = PA8
echo     9 = PA15
echo    10 = PB0    11 = PB1    12 = PB2    13 = PB3
echo    14 = PB4    15 = PB5    16 = PB6    17 = PB7
echo    18 = PB8    19 = PB9    20 = PB10   21 = PB11
echo    22 = PB12   23 = PB13   24 = PB14   25 = PB15
echo   --------------------------------------------------
echo   Requires ws2812_sel.hex already flashed on the board.
echo   --------------------------------------------------
set "SEL="
set /p "SEL=Pin number [0-25]: "
if not defined SEL set SEL=0
set /a OK=0
if %SEL% GEQ 0 if %SEL% LEQ 25 set /a OK=1
if %OK%==0 (
  echo   [ERROR] number must be 0..25
  pause
  exit /b 1
)
echo.
echo   STEP 1  close FlyMcu (it locks the port)
echo   STEP 2  hold BOOT0, tap NRST, release NRST, then release BOOT0
echo.
"%PY%" -u tools\flash_an3155.py --port %PORT% --pin-sel %SEL% --wait 40
echo.
echo   Then: BOOT0 to GND, tap NRST - the board uses the new pin.
pause
popd
