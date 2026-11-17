#include "mem.h"
#include <tlhelp32.h>

namespace {
    HANDLE g_proc = nullptr;
    DWORD g_pid = 0;
    uintptr_t g_client = 0;
}

namespace mem {

bool attach(const wchar_t* procName) {
    HANDLE snap = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    if (snap == INVALID_HANDLE_VALUE) return false;
    PROCESSENTRY32W pe{ sizeof(pe) };
    bool found = false;
    if (Process32FirstW(snap, &pe)) {
        do {
            if (_wcsicmp(pe.szExeFile, procName) == 0) {
                g_pid = pe.th32ProcessID;
                found = true;
                break;
            }
        } while (Process32NextW(snap, &pe));
    }
    CloseHandle(snap);
    if (!found) return false;

    g_proc = OpenProcess(PROCESS_VM_READ | PROCESS_VM_WRITE | PROCESS_VM_OPERATION | PROCESS_QUERY_INFORMATION, FALSE, g_pid);
    if (!g_proc) {
        g_proc = OpenProcess(PROCESS_VM_READ | PROCESS_QUERY_INFORMATION, FALSE, g_pid);
    }
    return g_proc != nullptr;
}

void detach() {
    if (g_proc) { CloseHandle(g_proc); g_proc = nullptr; }
}

void invalidate() { g_client = 0; }

bool alive() {
    if (!g_proc) return false;
    DWORD code = 0;
    if (!GetExitCodeProcess(g_proc, &code)) return false;
    return code == STILL_ACTIVE;
}

uintptr_t moduleBase(const wchar_t* modName) {
    if (!g_proc) return 0;
    HANDLE snap = CreateToolhelp32Snapshot(TH32CS_SNAPMODULE | TH32CS_SNAPMODULE32, g_pid);
    if (snap == INVALID_HANDLE_VALUE) return 0;
    MODULEENTRY32W me{ sizeof(me) };
    uintptr_t base = 0;
    if (Module32FirstW(snap, &me)) {
        do {
            if (_wcsicmp(me.szModule, modName) == 0) { base = (uintptr_t)me.modBaseAddr; break; }
        } while (Module32NextW(snap, &me));
    }
    CloseHandle(snap);
    return base;
}

uintptr_t client() {
    if (!g_client) g_client = moduleBase(L"client.dll");
    return g_client;
}

bool read(uintptr_t addr, void* out, size_t size) {
    if (!g_proc || !addr) return false;
    SIZE_T rd = 0;
    return ReadProcessMemory(g_proc, (LPCVOID)addr, out, size, &rd) && rd == size;
}

bool write(uintptr_t addr, const void* in, size_t size) {
    if (!g_proc || !addr) return false;
    SIZE_T wr = 0;
    return WriteProcessMemory(g_proc, (LPVOID)addr, in, size, &wr) && wr == size;
}

}
