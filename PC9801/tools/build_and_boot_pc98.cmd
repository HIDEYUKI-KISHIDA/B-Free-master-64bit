@echo off
setlocal

REM Build (if needed) and boot automatically
powershell -NoProfile -ExecutionPolicy Bypass -File "%~dp0auto_build_and_boot.ps1" -NoExit

endlocal
