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
    if (!g_h) { printf("open failed %lu\n", GetLastError()); return 1; }
    g_client = GetModuleBase(g_pid, L"client.dll");

    uintptr_t sysPtr = ReadT<uintptr_t>(g_client + 0x271FBB8);
    uintptr_t chunks[8];
    for (int c = 0; c < 8; c++) chunks[c] = ReadT<uintptr_t>(sysPtr + 0x10 + 8 * c);

    auto entity = [&](int i) -> uintptr_t {
        uintptr_t chunk = chunks[(i & 0x7FFF) >> 9];
        if (!chunk) return 0;
        return ReadT<uintptr_t>(chunk + 0x70 * (i & 0x1FF));
    };

    int players = 0;
    for (int i = 1; i <= 661 && players < 24; i++) {
        uintptr_t ent = entity(i);
        if (!ent || ent < 0x10000) continue;
        int team = ReadT<int>(ent + 0x3E7);
        if (team != 2 && team != 3) continue;
        int hp = ReadT<int>(ent + 0x34C);
        uint32_t h32 = ReadT<uint32_t>(ent + 0x92C);
        uintptr_t pawn = (h32 && h32 != 0xFFFFFFFF) ? entity(h32 & 0x7FFF) : 0;
        int pteam = pawn ? ReadT<int>(pawn + 0x3E7) : -1;
        int php = pawn ? ReadT<int>(pawn + 0x34C) : -1;
        Vec3 org = pawn ? ReadT<Vec3>(pawn + 0x14A4) : Vec3{};
        printf("slot=%d ent=0x%llx team=%d hp=%d handle=0x%x pawn=0x%llx pteam=%d php=%d org=(%.1f %.1f %.1f)\n",
            i, (unsigned long long)ent, team, hp, h32, (unsigned long long)pawn, pteam, php, org.x, org.y, org.z);
        if (pteam == 2 || pteam == 3) players++;
    }
    printf("players=%d\n", players);
    CloseHandle(g_h);
    return 0;
}
