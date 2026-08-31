@echo off
chcp 65001 >nul
title Rime 中日直输真实候选诊断
powershell.exe -NoLogo -NoProfile -ExecutionPolicy Bypass -File "%~dp0run-real-candidate-diagnostic.ps1"

