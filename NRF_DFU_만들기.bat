@echo off
setlocal
cd /d "%~dp0"
where pwsh.exe >nul 2>nul
if errorlevel 1 (
  echo PowerShell 7 ^(pwsh.exe^) is required. Install it and try again.
  goto failed
)
pwsh.exe -NoProfile -ExecutionPolicy Bypass -File "tools\create_nrf_legacy_dfu.ps1"
if errorlevel 1 goto failed
echo.
echo The verified ZIP is ready. No hardware update was performed.
pause
exit /b 0
:failed
echo.
echo No verified nRF DFU release was created. Read the error above.
pause
exit /b 1
