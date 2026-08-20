@echo off
echo === Manual Deploy ===
cd /d "%APPDATA%\Rime"
set "RIME_DICT_MANAGER=%ProgramFiles%\Rime\weasel\rime_dict_manager.exe"
if not exist "%RIME_DICT_MANAGER%" (
    echo [FAIL] Please set RIME_DICT_MANAGER to your rime_dict_manager.exe path.
    pause
    exit /b 1
)
"%RIME_DICT_MANAGER%" -b . test_translation
echo.
echo === Check Results ===
if exist "build\test_translation.table.bin" (
    echo [OK] table.bin generated
    dir /s build\test_translation.*
) else (
    echo [FAIL] table.bin not found
)
echo.
echo === Check Debug Output ===
if exist "build\translation_debug.txt" (
    echo [OK] Found translation_debug.txt
    type build\translation_debug.txt
) else (
    echo [WARN] No translation_debug.txt
)
pause
