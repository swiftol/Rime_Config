@echo off
chcp 65001 >nul
title Rime CNJP Candidate Diagnostic
powershell.exe -NoLogo -NoProfile -ExecutionPolicy Bypass -File "%~dp0diagnose-empty-candidates.ps1"
if errorlevel 1 (
  echo.
  echo Diagnostic failed. Error code: %errorlevel%
  pause
)
