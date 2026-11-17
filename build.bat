@echo off
call "C:\Program Files\Microsoft Visual Studio\18\Community\VC\Auxiliary\Build\vcvars64.bat" >nul 2>&1
cd /d "%~dp0"

set SRC=src\main.cpp src\config.cpp src\mem.cpp src\game.cpp src\overlay.cpp imgui\imgui.cpp imgui\imgui_draw.cpp imgui\imgui_tables.cpp imgui\imgui_widgets.cpp imgui\backends\imgui_impl_win32.cpp imgui\backends\imgui_impl_dx11.cpp
set INC=/I. /Isrc /Iimgui
set DEFS=/DIMGUI_IMPL_WIN32_DISABLE_GAMEPAD /DWIN32_LEAN_AND_MEAN /DNOMINMAX
set LIBS=user32.lib d3d11.lib dxgi.lib d3dcompiler.lib imm32.lib winmm.lib

if "%1"=="dbg" goto dbg

cl /nologo /std:c++20 /O2 /MT /EHsc /W3 %DEFS% %INC% %SRC% /link /SUBSYSTEM:WINDOWS /ENTRY:wWinMainCRTStartup /DEBUG:NONE /OUT:bin\cs2_aim.exe %LIBS%
goto end

:dbg
cl /nologo /std:c++20 /Od /MTd /EHsc /W3 %DEFS% /DCHEAT_DBG %INC% %SRC% /link /SUBSYSTEM:CONSOLE /OUT:bin\cs2_aim_dbg.exe %LIBS%

:end
