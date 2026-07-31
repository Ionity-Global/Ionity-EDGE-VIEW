@echo off
title Station Pico Build ^& Flash
echo Building and flashing Station Pico demo...
powershell -ExecutionPolicy Bypass -File "%~dp0build.ps1" -ProjectDir "%~dp0" -Flash
echo.
echo Done! Press any key to exit.
pause >nul