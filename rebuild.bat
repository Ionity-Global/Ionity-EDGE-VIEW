@echo off
setlocal enabledelayedexpansion

set "PROJECT_DIR=%~dp0"
set "SDK_DIR=%PROJECT_DIR%.sdk\pico-sdk"
set "SRC_DIR=%PROJECT_DIR%_waveshare_src\Pico-DVI-LCD-Code\01-DVI"
set "BUILD_DIR=%SRC_DIR%\build_rp2350"
set "ARM_GCC_BIN=%PROJECT_DIR%.sdk\bin"

echo Rebuilding edgeview (clean)...

:: Add ARM GCC to PATH
set "PATH=%ARM_GCC_BIN%;%PATH%"

:: Clean build for edgeview
cd /d "%BUILD_DIR%"
echo [1/2] Cleaning edgeview...
cmake --build . --target edgeview.clean -- -j8 2>nul
if exist "%BUILD_DIR%\apps\edgeview" (
    rmdir /s /q "%BUILD_DIR%\apps\edgeview" 2>nul
)
echo [2/2] Building edgeview...
cmake --build . --target edgeview -- -j8

if %ERRORLEVEL% NEQ 0 (
    echo [FAIL] Build failed!
    exit /b 1
)

echo.
echo ========================================
echo  Build successful!
echo  UF2: %BUILD_DIR%\apps\edgeview\edgeview.uf2
echo ========================================

:: Flash if Pico is in BOOTSEL mode
powershell -NoProfile -Command "$d = Get-Volume | Where-Object { $_.FileSystemLabel -eq 'RP2350' -or $_.FileSystemLabel -eq 'RPI-RP2' }; if ($d) { $d.DriveLetter }" > "%TEMP%\pico_drive.txt" 2>nul
set /p PICO_DRIVE=<"%TEMP%\pico_drive.txt"
if not "!PICO_DRIVE!"=="" (
    echo Pico found on !PICO_DRIVE!: - flashing...
    copy /y "%BUILD_DIR%\apps\edgeview\edgeview.uf2" !PICO_DRIVE!:\
    echo [OK] Flashed!
)
del "%TEMP%\pico_drive.txt" 2>nul