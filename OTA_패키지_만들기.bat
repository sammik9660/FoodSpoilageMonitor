@echo off
setlocal
cd /d "%~dp0"
where pwsh.exe >nul 2>nul
if errorlevel 1 (
  echo PowerShell 7 ^(pwsh.exe^) is required. Install it and try again.
  goto failed
)
if not exist "secrets\esp32-ota-signing-key.pem" (
  echo Signing key is missing: secrets\esp32-ota-signing-key.pem
  echo Restore the existing local key; this tool will not create a new one.
  goto failed
)
pwsh.exe -NoProfile -ExecutionPolicy Bypass -File "tools\build_esp32_ota_package.ps1"
if errorlevel 1 goto failed
echo.
echo Package creation finished. Select the .manifest as Manifest and .bin as Firmware on the gateway OTA page.
pause
exit /b 0
:failed
echo.
echo No verified OTA package was created.
pause
exit /b 1
