@echo off
setlocal
echo [INFO] Script start
echo [INFO] DOSBox-X path auto-detect start

rem DOSBox-X path auto-detect
set "DOSBOXX=C:\dosbox-x.exe"
if not exist "%DOSBOXX%" set "DOSBOXX=C:\DOSBOX-X\dosbox-x.exe"
if not exist "%DOSBOXX%" set "DOSBOXX=%~dp0..\..\..\DOSBOX-X\dosbox-x.exe"
if not exist "%DOSBOXX%" set "DOSBOXX=%~dp0..\..\..\dosbox-x.exe"
echo [INFO] DOSBox-X path: %DOSBOXX%
if not exist "%DOSBOXX%" (
  echo [ERROR] DOSBox-X (dosbox-x.exe) not found. Please place it in C:\ or C:\DOSBOX-X\ etc.
  pause
  exit /b 1
)

set "TOOLDIR=%~dp0"
set "BOOTIMG=%TOOLDIR%pc98_boot.img"
set "ITRONIMG=%TOOLDIR%pc98_itron.img"
echo [INFO] Image file path: %BOOTIMG% / %ITRONIMG%

set "BOOTSIZE=0"
set "ITRONSIZE=0"
if exist "%BOOTIMG%" for %%F in ("%BOOTIMG%") do set "BOOTSIZE=%%~zF"
if exist "%ITRONIMG%" for %%F in ("%ITRONIMG%") do set "ITRONSIZE=%%~zF"

echo [INFO] BOOTSIZE=%BOOTSIZE% ITRONSIZE=%ITRONSIZE%
if not "%BOOTSIZE%"=="1261568" powershell -NoProfile -ExecutionPolicy Bypass -File "%TOOLDIR%create_pc98_floppy_images.ps1"
if not "%ITRONSIZE%"=="1261568" powershell -NoProfile -ExecutionPolicy Bypass -File "%TOOLDIR%create_pc98_floppy_images.ps1"

if exist "%BOOTIMG%" for %%F in ("%BOOTIMG%") do set "BOOTSIZE=%%~zF"
if exist "%ITRONIMG%" for %%F in ("%ITRONIMG%") do set "ITRONSIZE=%%~zF"

echo [INFO] Recheck BOOTSIZE=%BOOTSIZE% ITRONSIZE=%ITRONSIZE%
if not "%BOOTSIZE%"=="1261568" (
  echo [ERROR] pc98_boot.img size error: %BOOTSIZE%
  pause
  exit /b 2
)
if not "%ITRONSIZE%"=="1261568" (
  echo [ERROR] pc98_itron.img size error: %ITRONSIZE%
  pause
  exit /b 3
)

set "CAPDIR=%TOOLDIR%captures"
if not exist "%CAPDIR%" mkdir "%CAPDIR%"
set "CONF=%TOOLDIR%dosbox-x.conf"
echo [INFO] dosbox-x.conf created: %CONF%
echo [dosbox]> "%CONF%"
echo captures=%CAPDIR%>> "%CONF%"

for /f "usebackq tokens=*" %%A in (`powershell -NoProfile -Command "$p=(Resolve-Path '%BOOTIMG%').Path -replace '\\','/'; $p"`) do set "BOOTFS=%%A"
for /f "usebackq tokens=*" %%A in (`powershell -NoProfile -Command "$p=(Resolve-Path '%ITRONIMG%').Path -replace '\\','/'; $p"`) do set "ITRONFS=%%A"
echo [INFO] Run DOSBox-X: "%DOSBOXX%" -conf "%CONF%" -set machine=pc98 -set memsize=8 -c "boot \"%BOOTFS%\" \"%ITRONFS%\""
"%DOSBOXX%" -conf "%CONF%" -set machine=pc98 -set memsize=8 -c "boot \"%BOOTFS%\" \"%ITRONFS%\""
echo [INFO] DOSBox-X command finished
set "ERR=%ERRORLEVEL%"

if not "%ERR%"=="0" (
  set "DOSIMG=C:\dosimg"
  if not exist "%DOSIMG%" mkdir "%DOSIMG%"
)
pause
exit /b %ERR%

