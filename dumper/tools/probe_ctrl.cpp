// Throwaway verification probe: checks which controller->pawn offset is real
// by walking the entity list in the live game.

#include <windows.h>
#include <tlhelp32.h>
#include <cstdio>
#include <cstdint>
#include <vector>
#include <string>

static HANDLE g_proc = nullptr;

template <typename T> bool rd(uint64_t a, T& v) {
    SIZE_T got = 0;
    return ReadProcessMemory(g_proc, (LPCVOID)a, &v, sizeof(T), &got) && got == sizeof(T);
}
static uint64_t ptr(uint64_t a) { uint64_t v = 0; rd(a, v); return v; }
static uint32_t u32(uint64_t a) { uint32_t v = 0; rd(a, v); return v; }

static uint64_t module(const wchar_t* name) {
    HANDLE s = CreateToolhelp32Snapshot(TH32CS_SNAPMODULE | TH32CS_SNAPMODULE32, GetProcessId(g_proc));
    if (s == INVALID_HANDLE_VALUE) return 0;
    MODULEENTRY32W me{ sizeof(me) };
    uint64_t base = 0;
    if (Module32FirstW(s, &me)) {
        do {
            if (_wcsicmp(me.szModule, name) == 0) { base = (uint64_t)me.modBaseAddr; break; }
        } while (Module32NextW(s, &me));
    }
    CloseHandle(s);
    return base;
}

int wmain() {
    HANDLE snap = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    PROCESSENTRY32W pe{ sizeof(pe) };
    uint32_t pid = 0;
    if (Process32FirstW(snap, &pe)) {
        do { if (_wcsicmp(pe.szExeFile, L"cs2.exe") == 0) { pid = pe.th32ProcessID; break; } }
        while (Process32NextW(snap, &pe));
    }
    CloseHandle(snap);
    if (!pid) { printf("cs2.exe not found\n"); return 1; }
    g_proc = OpenProcess(PROCESS_VM_READ | PROCESS_QUERY_INFORMATION, FALSE, pid);
    if (!g_proc) { printf("OpenProcess failed %lu\n", GetLastError()); return 1; }

    uint64_t client = module(L"client.dll");
    uint64_t engine2 = module(L"engine2.dll");
    printf("client base = 0x%llX\n", (unsigned long long)client);
    if (engine2) {
        uint64_t ngc = ptr(engine2 + 0x9B88C0);
        printf("signOnState = %u\n", ngc ? u32(ngc + 0x230) : 0);
    }

    uint64_t sys = ptr(client + 0x2709010);
    uint64_t ctrl = ptr(client + 0x252B0C8);
    printf("entitySystem = 0x%llX, localController = 0x%llX\n",
           (unsigned long long)sys, (unsigned long long)ctrl);
    if (!sys || !ctrl) return 1;

    auto ent = [&](uint32_t handle, bool& ok) -> uint64_t {
        int c = (handle & 0x7FFF) >> 9;
        int slot = handle & 0x1FF;
        uint64_t chunk = ptr(sys + 0x10 + 8ull * c);
        if (!chunk) { ok = false; return 0; }
        ok = true;
        return ptr(chunk + 0x70ull * slot);
    };

    for (uint32_t off : {0x8ECu, 0x92Cu}) {
        uint32_t h = u32(ctrl + off);
        bool ok = false;
        uint64_t pawn = h && h != 0xFFFFFFFF ? ent(h & 0x7FFF, ok) : 0;
        printf("ctrl+0x%X: handle=0x%X pawn=0x%llX", off, h, (unsigned long long)pawn);
        if (pawn) {
            int hp = (int)u32(pawn + 0x34C);
            int team = (int)u32(pawn + 0x3E7);
            uint64_t node = ptr(pawn + 0x330);
            printf(" hp=%d team=%d node=0x%llX", hp, team, (unsigned long long)node);
            {
                float ea0[2]{}, ea1[2]{}, vo[3]{};
                rd(pawn + 0x3600, ea0); rd(pawn + 0x3630, ea1); rd(pawn + 0xF60, vo);
                printf(" eye@0x3600=(%.1f %.1f) eye@0x3630=(%.1f %.1f) viewoff=(%.1f %.1f %.1f)",
                       ea0[0], ea0[1], ea1[0], ea1[1], vo[0], vo[1], vo[2]);
            }
            if (node) {
                uint8_t dormant7 = u32(node + 0xE7) & 0xFF;
                uint8_t dormant3 = u32(node + 0x103) & 0xFF;
                float ox, oy, oz;
                rd(node + 0xC8, ox); rd(node + 0xCC, oy); rd(node + 0xD0, oz);
                uint64_t bones = ptr(node + 0x140 + 0x80);
                float bx, by, bz;
                bool bone_ok = bones && rd(bones, bx) && rd(bones + 4, by) && rd(bones + 8, bz);
                printf(" dormant(0xE7)=%u dormant(0x103)=%u absOrigin=(%.0f %.0f %.0f) bones=0x%llX bone0=(%.1f %.1f %.1f)",
                       dormant7, dormant3, ox, oy, oz, (unsigned long long)bones, bx, by, bz);
            }
        }
        printf("\n");
    }

    // Dump a few controllers to sample m_hPlayerPawn offsets.
    for (int idx = 1; idx <= 64; ++idx) {
        int c = (idx & 0x7FFF) >> 9;
        uint64_t chunk = ptr(sys + 0x10 + 8ull * c);
        if (!chunk) continue;
        uint64_t ident = ptr(chunk + 0x70ull * (idx & 0x1FF));
        if (!ident) continue;
        uint64_t ent_ptr = ptr(ident + 0x8); // identity -> entity?
        printf("idx %d: ident=0x%llX ent=0x%llX h8EC=0x%X h92C=0x%X\n", idx,
               (unsigned long long)ident, (unsigned long long)ent_ptr,
               u32(ent_ptr + 0x8EC), u32(ent_ptr + 0x92C));
    }
    return 0;
}
