#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <tlhelp32.h>
#include <cstdio>
#include <cstdint>

struct Vec3 { float x, y, z; };
static uintptr_t g_pid = 0;
static HANDLE g_h = nullptr;
static uintptr_t g_client = 0;

uintptr_t GetModuleBase(uintptr_t pid, const wchar_t* mod) {
    HANDLE snap = CreateToolhelp32Snapshot(TH32CS_SNAPMODULE | TH32CS_SNAPMODULE32, (DWORD)pid);
    if (snap == INVALID_HANDLE_VALUE) return 0;
    MODULEENTRY32W me{ sizeof(me) };
    uintptr_t base = 0;
    if (Module32FirstW(snap, &me)) {
        do {
            if (_wcsicmp(me.szModule, mod) == 0) { base = (uintptr_t)me.modBaseAddr; break; }
        } while (Module32NextW(snap, &me));
    }
    CloseHandle(snap);
    return base;
}

bool Read(uintptr_t addr, void* out, size_t size) {
    SIZE_T rd = 0;
    return ReadProcessMemory(g_h, (LPCVOID)addr, out, size, &rd) && rd == size;
}

template <typename T> T ReadT(uintptr_t addr) {
    T v{};
    Read(addr, &v, sizeof(T));
    return v;
}

int main() {
    g_pid = 20776;
    g_h = OpenProcess(PROCESS_VM_READ | PROCESS_QUERY_INFORMATION, FALSE, (DWORD)g_pid);
    if (!g_h) { printf("open failed\n"); return 1; }
    g_client = GetModuleBase(g_pid, L"client.dll");
    uintptr_t va = g_client + 0x2581098;

    Vec3 before = ReadT<Vec3>(va);
    printf("angles before: (%.2f %.2f %.2f)\n", before.x, before.y, before.z);

    INPUT in[1] = {};
    in[0].type = INPUT_MOUSE;
    in[0].mi.dwFlags = MOUSEEVENTF_XDOWN;
    in[0].mi.mouseData = XBUTTON2;
    SendInput(1, in, sizeof(INPUT));
    Sleep(600);
    Vec3 during = ReadT<Vec3>(va);
    in[0].mi.dwFlags = MOUSEEVENTF_XUP;
    in[0].mi.mouseData = XBUTTON2;
    SendInput(1, in, sizeof(INPUT));
    Sleep(100);
    Vec3 after = ReadT<Vec3>(va);

    printf("angles during: (%.2f %.2f %.2f)\n", during.x, during.y, during.z);
    printf("angles after:  (%.2f %.2f %.2f)\n", after.x, after.y, after.z);
    printf("delta: (%.2f %.2f)\n", after.x - before.x, after.y - before.y);
    CloseHandle(g_h);
    return 0;
}
