@echo off
setlocal enabledelayedexpansion

set "PROJECT_DIR=%~dp0"
set "SDK_DIR=%PROJECT_DIR%.sdk\pico-sdk"
set "SRC_DIR=%PROJECT_DIR%_waveshare_src\Pico-DVI-LCD-Code\01-DVI"
set "BUILD_DIR=%SRC_DIR%\build_rp2350"
set "ARM_GCC_BIN=%PROJECT_DIR%.sdk\bin"

echo ========================================
echo  Station Pico - Build (RP2350)
echo ========================================

:: Optional target argument
set "BUILD_TARGET="
if NOT "%~1"=="" set "BUILD_TARGET=--target %~1"

:: Add ARM GCC to PATH
set "PATH=%ARM_GCC_BIN%;%PATH%"

:: Setup MSVC for host tools (pioasm, picotool)
call "C:\Program Files (x86)\Microsoft Visual Studio\18\BuildTools\VC\Auxiliary\Build\vcvarsall.bat" x64 >nul 2>&1

:: Create build directory
if not exist "%BUILD_DIR%" mkdir "%BUILD_DIR%"

:: Configure if needed
if not exist "%BUILD_DIR%\CMakeCache.txt" (
    echo.
    echo [1/2] Configuring CMake for RP2350...
    cd /d "%BUILD_DIR%"
    cmake -G Ninja ^
        -DPICO_SDK_PATH="%SDK_DIR%" ^
        -DPICO_BOARD=pico2_w ^
        -DPICO_PLATFORM=rp2350 ^
        -DDVI_DEFAULT_SERIAL_CONFIG=pico_sock_cfg ^
        -DCMAKE_BUILD_TYPE=Release ^
        -DPICO_TOOLCHAIN_PATH="%ARM_GCC_BIN%" ^
        "%SRC_DIR%"
    if %ERRORLEVEL% NEQ 0 (
        echo [FAIL] CMake configuration failed!
        exit /b 1
    )
) else (
    cd /d "%BUILD_DIR%"
)

:: Build
echo.
if "%BUILD_TARGET%"=="" (
    echo [2/2] Building ALL targets...
) else (
    echo [2/2] Building: %~1
)
cmake --build . %BUILD_TARGET% -- -j8

if %ERRORLEVEL% NEQ 0 (
    echo [FAIL] Build failed!
    exit /b 1
)
echo.
echo ========================================
echo  Build successful!
echo ========================================

:: List built UF2 files
echo.
echo  Built demos:
for /d %%d in ("%BUILD_DIR%\apps\*") do (
    if exist "%%d\*.uf2" (
        for %%f in ("%%d\*.uf2") do echo    %%~nf
    )
)

:: Flash if Pico is in BOOTSEL mode
if NOT "%BUILD_TARGET%"=="" (
    powershell -NoProfile -Command "$d = Get-Volume | Where-Object { $_.FileSystemLabel -eq 'RP2350' -or $_.FileSystemLabel -eq 'RPI-RP2' }; if ($d) { $d.DriveLetter }" > "%TEMP%\pico_drive.txt" 2>nul
    set /p PICO_DRIVE=<"%TEMP%\pico_drive.txt"
    if not "!PICO_DRIVE!"=="" (
        echo.
        echo Pico found on !PICO_DRIVE!: - flashing %~1...
        copy /y "%BUILD_DIR%\apps\%~1\%~1.uf2" !PICO_DRIVE!:\
        echo [OK] Flashed!
    )
    del "%TEMP%\pico_drive.txt" 2>nul
)