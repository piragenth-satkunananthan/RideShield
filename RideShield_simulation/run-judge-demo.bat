@echo off
title RideShield Semi-Final Judge Demo

echo.
echo ===============================================
echo         RideShield Semi-Final Judge Demo
echo ===============================================
echo.

REM Run the PowerShell presentation script from this folder.
powershell.exe -NoProfile -ExecutionPolicy Bypass -File "%~dp0run-judge-demo.ps1"

echo.
pause
