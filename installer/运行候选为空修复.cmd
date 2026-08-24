@echo off
chcp 65001 >nul
title Rime CNJP Empty Candidate Repair
powershell.exe -NoLogo -NoProfile -ExecutionPolicy Bypass -File "%~dp0repair-empty-candidates.ps1"
if errorlevel 1 (
  echo.
  echo Repair failed. Error code: %errorlevel%
  pause
)
