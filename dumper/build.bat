@echo off
call "C:\Program Files\Microsoft Visual Studio\18\Community\VC\Auxiliary\Build\vcvars64.bat" >nul 2>&1
cd /d "%~dp0"

if not exist bin mkdir bin

cl /nologo /std:c++20 /O2 /MT /EHsc /W4 /permissive- /utf-8 ^
   /DWIN32_LEAN_AND_MEAN /DNOMINMAX /D_CRT_SECURE_NO_WARNINGS ^
   /Isrc ^
   src\main.cpp src\json.cpp src\log.cpp src\pe.cpp src\sig_scan.cpp ^
   src\process.cpp src\netvar.cpp src\offsets.cpp src\config.cpp src\hash.cpp ^
   /Fe:bin\cs2_offsets_dumper.exe /link /SUBSYSTEM:CONSOLE kernel32.lib advapi32.lib

if errorlevel 1 (
    echo build failed
    exit /b 1
)

if exist src\*.obj del /q src\*.obj >nul 2>&1
if exist schema_client.json copy /y schema_client.json bin\schema_client.json >nul
echo built: bin\cs2_offsets_dumper.exe
