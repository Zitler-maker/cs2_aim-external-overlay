@echo off
call "C:\Program Files\Microsoft Visual Studio\18\Community\VC\Auxiliary\Build\vcvars64.bat" >nul 2>&1
cl /nologo /std:c++20 /O2 /EHsc /W3 /DUNICODE /D_UNICODE bones2.cpp /link /OUT:bones2.exe
