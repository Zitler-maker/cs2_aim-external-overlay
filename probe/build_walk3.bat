@echo off
call "C:\Program Files\Microsoft Visual Studio\18\Community\VC\Auxiliary\Build\vcvars64.bat" >nul 2>&1
cl /nologo /std:c++20 /O2 /EHsc /W3 /DUNICODE /D_UNICODE walk3.cpp /link /OUT:walk3.exe
