#pragma once
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <thread>
#include "shared.h"

void gameThread();

namespace overlay {
    int run(HINSTANCE hInst);
    bool menuOpen();
    extern HWND hwnd;
}
