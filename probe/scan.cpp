#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <tlhelp32.h>
#include <cstdio>
#include <cstdint>
#include <vector>

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
    if (!g_h) { printf("open failed %lu\n", GetLastError()); return 1; }
    g_client = GetModuleBase(g_pid, L"client.dll");

    uintptr_t lpPawn = ReadT<uintptr_t>(g_client + 0x256A948);
    uintptr_t lpCtrl = ReadT<uintptr_t>(g_client + 0x2542548);
    printf("lpPawn=0x%llx lpCtrl=0x%llx\n", (unsigned long long)lpPawn, (unsigned long long)lpCtrl);

    uintptr_t sysPtr = ReadT<uintptr_t>(g_client + 0x271FBB8);
    uintptr_t chunk0 = ReadT<uintptr_t>(sysPtr + 0x10);
    uintptr_t chunk1 = ReadT<uintptr_t>(sysPtr + 0x18);
    printf("chunk0=0x%llx chunk1=0x%llx\n", (unsigned long long)chunk0, (unsigned long long)chunk1);

    // scan chunk0 for pawn/ctrl pointers
    std::vector<uintptr_t> buf(0xF000 / 8);
    for (int c = 0; c < 2; c++) {
        uintptr_t chunk = c == 0 ? chunk0 : chunk1;
        if (!chunk || !Read(chunk, buf.data(), 0xF000)) { printf("chunk%d unreadable\n", c); continue; }
        for (size_t k = 0; k < buf.size(); k++) {
            if (buf[k] == lpPawn) printf("chunk%d: pawn ptr at +0x%llx\n", c, (unsigned long long)(k * 8));
            if (buf[k] == lpCtrl) printf("chunk%d: ctrl ptr at +0x%llx\n", c, (unsigned long long)(k * 8));
        }
    }
    // also check what the local controller identity looks like: find ctrl in chunk
    // dump nonzero identity slots of chunk0
    int nz = 0;
    for (int i = 0; i < 512 && nz < 20; i++) {
        uintptr_t id = chunk0 + 0x78 * i;
        uintptr_t first = ReadT<uintptr_t>(id);
        uintptr_t at70 = ReadT<uintptr_t>(id + 0x70);
        uintptr_t at68 = ReadT<uintptr_t>(id + 0x68);
        uintptr_t at10 = ReadT<uintptr_t>(id + 0x10);
        if (first || at70 || at68 || at10) {
            printf("id[%d] +0=%llx +0x10=%llx +0x68=%llx +0x70=%llx\n", i,
                (unsigned long long)first, (unsigned long long)at10,
                (unsigned long long)at68, (unsigned long long)at70);
            nz++;
        }
    }
    CloseHandle(g_h);
    return 0;
}
