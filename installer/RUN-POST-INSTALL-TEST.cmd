@echo off
chcp 65001 >nul
title Rime Chinese-Japanese Post-Install Acceptance Test
echo Starting read-only acceptance test. This window will not close automatically.
echo.
powershell.exe -NoLogo -NoProfile -ExecutionPolicy Bypass -File "%~dp0post-install-acceptance-test.ps1"
set "RESULT=%ERRORLEVEL%"
echo.
echo TestExitCode=%RESULT%
echo Send the TXT report created on your Desktop to the developer.
pause
exit /b %RESULT%
