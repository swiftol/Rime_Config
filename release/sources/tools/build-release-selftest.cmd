@echo off
if not defined ZHONGRI_TEST_BUILD_DIR exit /b 2
call "C:\Program Files\Microsoft Visual Studio\18\Community\Common7\Tools\VsDevCmd.bat" -arch=x64 -host_arch=x64 >nul
if errorlevel 1 exit /b %errorlevel%
cl /nologo /std:c++17 /EHsc /O2 /utf-8 /Iwork\zhongri-weasel\include work\Rime_Config\installer\RimeCandidateSelfTest.cpp /Fe:"%ZHONGRI_TEST_BUILD_DIR%\RimeCandidateSelfTest.exe" /Fo:"%ZHONGRI_TEST_BUILD_DIR%\RimeCandidateSelfTest.obj"
exit /b %errorlevel%
