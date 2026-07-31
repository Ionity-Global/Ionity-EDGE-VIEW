@echo off
title IO-nity Station Pico - Bridge Server
echo ============================================
echo  IO-nity Station Pico - Bridge Server
echo ============================================
echo.
echo Make sure the Pico is connected and running.
echo Find its IP on the Pico display (Network panel).
echo.

set /p PICO_IP="Enter Pico IP address [192.168.1.100]: "
if "%PICO_IP%"=="" set PICO_IP=192.168.1.100

echo.
echo Starting HTTPS server on port 8443...
echo Open https://YOUR-PC-IP:8443 on any device
echo.

python "%~dp0server.py" --pico-ip %PICO_IP% --port 8443
pause