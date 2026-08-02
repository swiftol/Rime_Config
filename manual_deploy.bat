@echo off
echo === Manual Deploy ===
cd /d "C:\Users\86131\AppData\Roaming\Rime"
"D:\Rime\weasel-0.17.4\rime_dict_manager.exe" -b . test_translation
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
