@echo off
title RideShield Phase 1 Demo

echo.
echo ===============================================
echo         RideShield Phase 1 Demo
echo ===============================================
echo.

REM Run the PowerShell presentation script from this folder.
powershell.exe -NoProfile -ExecutionPolicy Bypass -File "%~dp0run-demo.ps1"

echo.
pause
