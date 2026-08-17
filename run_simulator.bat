@echo off
chcp 65001 > nul
title GreenSlot IoT Simulator
echo ======================================================
echo       KHOI DONG GREENSLOT IOT SENSOR SIMULATOR
echo ======================================================
echo.
python --version > nul 2>&1
if errorlevel 1 (
    echo [LOI] Khong tim thay Python! Vui long cai dat Python 3.8+
    pause
    exit /b 1
)

echo [1/2] Dang kiem tra thu vien Python...
pip install -r requirements.txt --quiet

echo [2/2] Dang chay IoT Simulator...
python bridge.py --simulate %*

pause
