#pragma once
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <cstdint>

namespace mem {
    bool attach(const wchar_t* procName);
    void detach();
    void invalidate();
    bool alive();
    uintptr_t moduleBase(const wchar_t* modName);
    uintptr_t client();
    bool read(uintptr_t addr, void* out, size_t size);
    bool write(uintptr_t addr, const void* in, size_t size);
    template <typename T> T readT(uintptr_t addr) { T v{}; read(addr, &v, sizeof(T)); return v; }
    template <typename T> bool writeT(uintptr_t addr, const T& v) { return write(addr, &v, sizeof(T)); }
}
