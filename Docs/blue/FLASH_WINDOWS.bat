@echo off
setlocal EnableExtensions DisableDelayedExpansion
if /I "%~1"=="--help" goto usage
if /I "%~1"=="-h" goto usage

if not defined IDF_PATH goto platformio
if not exist "%IDF_PATH%\tools\idf.py" goto platformio
python --version >nul 2>&1
if errorlevel 1 goto missing_idf

pushd "%~dp0"
if errorlevel 1 exit /b 1
set "IDF_TARGET=esp32"

echo Building Lumiac for ESP32...
python "%IDF_PATH%\tools\idf.py" build
if errorlevel 1 goto failed

echo Flashing firmware and starting the serial monitor. Press Ctrl+] to exit.
if "%~1"=="" goto auto_port
python "%IDF_PATH%\tools\idf.py" -p "%~1" flash monitor
goto flash_done

:auto_port
python "%IDF_PATH%\tools\idf.py" flash monitor

:flash_done
if errorlevel 1 goto failed
popd
exit /b 0

:platformio
set "PIO_PYTHON=%USERPROFILE%\.platformio\penv\Scripts\python.exe"
if not exist "%PIO_PYTHON%" set "PIO_PYTHON=python"
"%PIO_PYTHON%" -m platformio --version >nul 2>&1
if errorlevel 1 goto missing_idf
pushd "%~dp0"
if errorlevel 1 exit /b 1
echo Building and flashing Lumiac with PlatformIO...
if "%~1"=="" goto pio_default_port
"%PIO_PYTHON%" -m platformio run -e esp32dev --target upload --upload-port "%~1"
if errorlevel 1 goto failed
echo Starting the serial monitor. Press Ctrl+C to exit.
"%PIO_PYTHON%" -m platformio device monitor -e esp32dev --port "%~1"
goto flash_done

:pio_default_port
"%PIO_PYTHON%" -m platformio run -e esp32dev --target upload
if errorlevel 1 goto failed
echo Starting the serial monitor. Press Ctrl+C to exit.
"%PIO_PYTHON%" -m platformio device monitor -e esp32dev
goto flash_done

:failed
set "FLASH_EXIT_CODE=%errorlevel%"
echo.
echo Build, flash, or monitor failed. Review the error above before trying again.
popd
exit /b %FLASH_EXIT_CODE%

:missing_idf
echo Neither ESP-IDF nor PlatformIO is available in this terminal.
echo Install PlatformIO, or install ESP-IDF v5.3.1 and open its Command Prompt.
echo Then run this script again:
echo   cd /d "%~dp0"
echo   FLASH_WINDOWS.bat COM5
echo Replace COM5 with your ESP32 serial port.
exit /b 1

:usage
echo Usage: FLASH_WINDOWS.bat [COM_PORT]
echo Example: FLASH_WINDOWS.bat COM5
echo Uses an active ESP-IDF terminal, or PlatformIO if ESP-IDF is not active.
echo PlatformIO defaults to COM5; ESP-IDF detects the port if omitted.
exit /b 0
