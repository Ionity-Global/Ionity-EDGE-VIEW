@echo off
powershell -ExecutionPolicy Bypass -NoProfile -File "%~dp0scripts\Install-Dependencies.ps1" %*
