@echo off
chcp 65001 >nul
title 中日英三语输入法 - 一键安装

echo.
echo ========================================
echo   中日英三语输入法 - 一键安装
echo ========================================
echo.

:: 检查小狼毫是否已安装
if not exist "%ProgramFiles(x86)%\Rime\weasel-server.exe" (
    if not exist "%ProgramFiles%\Rime\weasel-server.exe" (
        echo [错误] 未检测到小狼毫输入法！
        echo.
        echo 请先安装小狼毫：
        echo https://github.com/rime/weasel/releases
        echo.
        pause
        exit /b 1
    )
)

echo [1/4] 检测到小狼毫已安装 ✓
echo.

:: 备份现有配置
set RIME_DIR=%APPDATA%\Rime
if exist "%RIME_DIR%" (
    echo [2/4] 备份现有配置...
    if exist "%RIME_DIR%_backup" (
        echo 删除旧备份...
        rmdir /s /q "%RIME_DIR%_backup"
    )
    move "%RIME_DIR%" "%RIME_DIR%_backup" >nul
    echo 备份完成：%RIME_DIR%_backup ✓
) else (
    echo [2/4] 无需备份（首次安装）
)
echo.

:: 创建配置目录
echo [3/4] 安装三语配置文件...
mkdir "%RIME_DIR%" 2>nul

:: 复制所有配置文件
xcopy /s /e /y /i "%~dp0*.yaml" "%RIME_DIR%\" >nul
xcopy /s /e /y /i "%~dp0lua" "%RIME_DIR%\lua\" >nul
xcopy /s /e /y /i "%~dp0dicts" "%RIME_DIR%\dicts\" >nul
xcopy /s /e /y /i "%~dp0opencc" "%RIME_DIR%\opencc\" >nul

echo 配置文件安装完成 ✓
echo.

:: 重新部署
echo [4/4] 重新部署小狼毫...
echo.
echo 请按任意键开始部署...
pause >nul

:: 调用小狼毫部署工具
if exist "%ProgramFiles(x86)%\Rime\WeaselDeployer.exe" (
    start "" /wait "%ProgramFiles(x86)%\Rime\WeaselDeployer.exe" /deploy
) else (
    start "" /wait "%ProgramFiles%\Rime\WeaselDeployer.exe" /deploy
)

echo.
echo ========================================
echo   安装完成！
echo ========================================
echo.
echo 使用方法：
echo   1. 按 Win+Space 切换到小狼毫
echo   2. 按 Ctrl+Shift+1 → 中文
echo   3. 按 Ctrl+Shift+2 → 日文
echo   4. 按 Ctrl+Shift+3 → 英文
echo.
echo 详细说明请查看：安装指南.md
echo.
pause
