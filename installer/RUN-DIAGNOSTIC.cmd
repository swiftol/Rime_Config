@echo off
chcp 65001 >nul
title Rime CNJP Real Candidate Diagnostic
powershell.exe -NoLogo -NoProfile -ExecutionPolicy Bypass -File "%~dp0run-real-candidate-diagnostic.ps1"
echo.
echo ExitCode=%errorlevel%
echo The window will remain open. Send the TXT report from your Desktop.
pause
