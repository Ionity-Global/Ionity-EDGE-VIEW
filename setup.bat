@echo off
title Station Pico Setup
echo ========================================
echo  Station Pico - One-Click Setup
echo  Pico 2W + Waveshare 10.1" DVI Display
echo ========================================
echo.
powershell -ExecutionPolicy Bypass -File "%~dp0setup.ps1" -ProjectDir "%~dp0"
if %ERRORLEVEL% NEQ 0 (
    echo.
    echo Setup failed with error code %ERRORLEVEL%
    pause
) else (
    echo.
    echo Setup complete! Press any key to exit.
    pause >nul
)