@echo off
call "C:\Program Files\Microsoft Visual Studio\18\Community\VC\Auxiliary\Build\vcvars64.bat" >nul 2>&1
cd /d "%~dp0"
cl /nologo /std:c++20 /O1 /MT /EHsc /DNOMINMAX /DWIN32_LEAN_AND_MEAN probe_ctrl.cpp /Fe:probe_ctrl.exe /link kernel32.lib
