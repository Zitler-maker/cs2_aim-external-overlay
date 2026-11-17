#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <tlhelp32.h>
#include <cstdio>
#include <cstdint>
#include <cmath>

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
    if (!g_h) { printf("open failed %lu\n", GetLastError()); return 1; }
    g_client = GetModuleBase(g_pid, L"client.dll");

    uintptr_t sysPtr = ReadT<uintptr_t>(g_client + 0x271FBB8);
    uintptr_t chunk0 = ReadT<uintptr_t>(sysPtr + 0x10);
    printf("chunk0=0x%llx\n", (unsigned long long)chunk0);

    // inline identities: chunk0 + 0x78 * i, entity ptr at identity + off
    uintptr_t entOffs[] = { 0x68, 0x70, 0x78, 0x60, 0x58, 0x50 };
    int found = 0;
    for (int i = 1; i < 512 && found < 40; i++) {
        uintptr_t identity = chunk0 + 0x78 * i;
        for (int oi = 0; oi < 6; oi++) {
            uintptr_t ent = ReadT<uintptr_t>(identity + entOffs[oi]);
            if (!ent || ent < 0x10000) continue;
            int team = ReadT<int>(ent + 0x3E7);
            int hp = ReadT<int>(ent + 0x34C);
            if ((team == 2 || team == 3) && hp > 0 && hp <= 100) {
                Vec3 org = ReadT<Vec3>(ent + 0x14A4);
                printf("PLAYER i=%d entOff=0x%llx ent=0x%llx team=%d hp=%d org=(%.1f %.1f %.1f)\n",
                    i, (unsigned long long)entOffs[oi], (unsigned long long)ent, team, hp, org.x, org.y, org.z);
                found++;
                break;
            }
        }
    }
    printf("total: %d\n", found);
    CloseHandle(g_h);
    return 0;
}
