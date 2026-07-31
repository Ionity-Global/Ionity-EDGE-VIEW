@echo off
title Station Pico - Pico 2W + Waveshare 10.1" DVI Display
powershell -ExecutionPolicy Bypass -File "%~dp0StationPico.ps1" %*
if %ERRORLEVEL% NEQ 0 (
    echo.
    echo Something went wrong. Press any key to exit.
    pause >nul
)
