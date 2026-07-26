@echo off
REM build_all_hdmi.bat — Build all emulator apps for HDMI and generate merged firmware
REM Usage: double-click or run from command prompt

echo ============================================
echo  RetroESP32-P4 Full HDMI Build
echo ============================================

REM Resolve $ROOT from this script's location.
set "ROOT=%~dp0"
if "%ROOT:~-1%"=="\" set "ROOT=%ROOT:~0,-1%"

REM Activate ESP-IDF environment (honors IDF_PATH / IDF_PYTHON_ENV_PATH, else auto-detects).
call "%ROOT%\tools\resolve_idf_env.bat"
if errorlevel 1 (
    echo ERROR: Failed to activate ESP-IDF environment
    pause
    exit /b 1
)

set BINS=%ROOT%\firmware_hdmi
set HDMI_DEFAULTS=%ROOT%\launcher\sdkconfig.hdmi.defaults
if not exist "%BINS%" mkdir "%BINS%"

REM === Build Launcher (HDMI) ===
echo.
echo === Building Launcher (HDMI) ===
cd /d "%ROOT%\launcher"
if exist "build" rmdir /s /q "build"
if exist "sdkconfig" del /q "sdkconfig"
idf.py -DSDKCONFIG_DEFAULTS="sdkconfig.defaults;sdkconfig.hdmi.defaults" build
if errorlevel 1 ( echo FAILED: Launcher HDMI & pause & exit /b 1 )
copy /y "build\launcher.bin" "%BINS%\launcher.bin"
copy /y "build\bootloader\bootloader.bin" "%BINS%\bootloader.bin"
copy /y "build\partition_table\partition-table.bin" "%BINS%\partition-table.bin"
copy /y "build\ota_data_initial.bin" "%BINS%\ota_data_initial.bin"
echo Launcher (HDMI): OK

REM === Build Emulator Apps (HDMI) ===
call :build_app nes        apps\nes        nes_app.bin
call :build_app gb         apps\gb         gb_app.bin
call :build_app sms        apps\sms        sms_app.bin
call :build_app spectrum   apps\spectrum   spectrum_app.bin
call :build_app stella     apps\stella     stella_app.bin
call :build_app prosystem  apps\prosystem  prosystem_app.bin
call :build_app handy      apps\handy      handy_app.bin
call :build_app pce        apps\pce        pce_app.bin
call :build_app atari800   apps\atari800   atari800_app.bin
call :build_app snes       apps\snes       snes_app.bin
call :build_app genesis    apps\genesis    genesis_app.bin
call :build_app neogeo     apps\neogeo     neogeo_app.bin

REM === Generate Merged HDMI Firmware ===
echo.
echo === Generating Merged HDMI Firmware ===
cd /d "%ROOT%"
powershell -ExecutionPolicy Bypass -File "%ROOT%\generate_merged_bin_hdmi.ps1"

echo.
echo ============================================
echo  ALL HDMI BUILDS COMPLETE
echo ============================================
pause
exit /b 0

:build_app
echo.
echo === Building %~1 (HDMI) ===
cd /d "%ROOT%\%~2"
if exist "build" rmdir /s /q "build"
if exist "sdkconfig" del /q "sdkconfig"
idf.py -DSDKCONFIG_DEFAULTS="sdkconfig.defaults;%HDMI_DEFAULTS%" build
if errorlevel 1 ( echo FAILED: %~1 HDMI & pause & exit /b 1 )
copy /y "build\%~3" "%BINS%\%~3"
echo %~1 (HDMI): OK
goto :eof
