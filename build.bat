@echo off
powershell -ExecutionPolicy Bypass -NoProfile -File "%~dp0scripts\Build-EdgeView.ps1" %*
