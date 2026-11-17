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
    printf("client=0x%llx\n", (unsigned long long)g_client);

    uintptr_t dwEntityList = 0x271FBB8;
    uintptr_t entListAddr = g_client + dwEntityList;
    uintptr_t sysPtr = ReadT<uintptr_t>(entListAddr);
    printf("read(client+dwEntityList) = 0x%llx\n", (unsigned long long)sysPtr);

    // dump first 0x80 bytes of what it points to
    printf("--- raw qwords at client+0x271FBB8 ---\n");
    for (int i = 0; i < 12; i++) {
        uintptr_t v = ReadT<uintptr_t>(entListAddr + 8 * i);
        printf("[+%02x] %016llx\n", i * 8, (unsigned long long)v);
    }

    // if it looks like a pointer into client.dll data, walk its internals
    if (sysPtr && sysPtr > g_client && sysPtr < g_client + 0x4000000) {
        printf("--- raw qwords at sysPtr ---\n");
        for (int i = 0; i < 16; i++) {
            uintptr_t v = ReadT<uintptr_t>(sysPtr + 8 * i);
            printf("[+%02x] %016llx\n", i * 8, (unsigned long long)v);
        }
        // highestEntityIndex per dump = 0x2120 relative to system instance
        uintptr_t highest = ReadT<uintptr_t>(sysPtr + 0x2120);
        printf("highestEntityIndex(+0x2120) = %llu\n", (unsigned long long)highest);
        uintptr_t offs[] = { 0x10u, 0x18u, 0x20u, 0x28u, 0x30u, 0x38u, 0x40u, 0x48u, 0x58u, 0x60u };
        for (int oi = 0; oi < 10; oi++) {
            uintptr_t off = offs[oi];
            uintptr_t v = ReadT<uintptr_t>(sysPtr + off);
            if (v && (v > g_client) && (v < g_client + 0x4000000)) {
                printf("sysPtr+0x%llx -> 0x%llx (client-relative, delta 0x%llx)\n",
                    (unsigned long long)off, (unsigned long long)v, (unsigned long long)(v - g_client));
            }
        }
    } else if (sysPtr) {
        printf("sysPtr not client-relative; dumping it anyway:\n");
        for (int i = 0; i < 16; i++) {
            uintptr_t v = ReadT<uintptr_t>(sysPtr + 8 * i);
            printf("[+%02x] %016llx\n", i * 8, (unsigned long long)v);
        }
        uintptr_t highest = ReadT<uintptr_t>(sysPtr + 0x2120);
        printf("highestEntityIndex(+0x2120) = %llu\n", (unsigned long long)highest);
    }

    // local pawn known good: check its gameSceneNode / modelState / bones live
    uintptr_t lpPawn = ReadT<uintptr_t>(g_client + 0x256A948);
    printf("lpPawn=0x%llx\n", (unsigned long long)lpPawn);
    if (lpPawn) {
        uintptr_t node = ReadT<uintptr_t>(lpPawn + 0x330);
        printf("sceneNode=0x%llx\n", (unsigned long long)node);
        uintptr_t ms = ReadT<uintptr_t>(node + 0x140);
        printf("modelState=0x%llx\n", (unsigned long long)ms);
        uintptr_t bones = ReadT<uintptr_t>(ms + 0x80);
        printf("bonePtr=0x%llx\n", (unsigned long long)bones);
        if (bones) {
            int boneIds[] = { 5, 6, 8, 10 };
            for (int bi = 0; bi < 4; bi++) {
                int b = boneIds[bi];
                Vec3 p = ReadT<Vec3>(bones + b * 32);
                printf("bone %d pos=(%.1f %.1f %.1f)\n", b, p.x, p.y, p.z);
            }
        }
        Vec3 org = ReadT<Vec3>(lpPawn + 0x14A4);
        printf("lp origin=(%.1f %.1f %.1f)\n", org.x, org.y, org.z);
        Vec3 vo = ReadT<Vec3>(lpPawn + 0xF60);
        printf("lp viewoff=(%.1f %.1f %.1f)\n", vo.x, vo.y, vo.z);
        Vec3 ea = ReadT<Vec3>(lpPawn + 0x3600);
        printf("lp eyeang=(%.1f %.1f %.1f)\n", ea.x, ea.y, ea.z);
        // view matrix
        float vm[16];
        Read(g_client + 0x2570250, vm, sizeof vm);
        printf("viewmatrix: %.2f %.2f %.2f %.2f | %.2f %.2f %.2f %.2f | %.2f %.2f %.2f %.2f | %.2f %.2f %.2f %.2f\n",
            vm[0], vm[1], vm[2], vm[3], vm[4], vm[5], vm[6], vm[7],
            vm[8], vm[9], vm[10], vm[11], vm[12], vm[13], vm[14], vm[15]);
        // view angles: try as pointer and as direct data
        uintptr_t vaPtr = ReadT<uintptr_t>(g_client + 0x2581098);
        printf("dwViewAngles val=0x%llx\n", (unsigned long long)vaPtr);
        Vec3 va = ReadT<Vec3>(g_client + 0x2581098);
        printf("as data: (%.1f %.1f %.1f)\n", va.x, va.y, va.z);
        Vec3 va2 = ReadT<Vec3>(vaPtr);
        printf("as ptr->: (%.1f %.1f %.1f)\n", va2.x, va2.y, va2.z);
    }
    CloseHandle(g_h);
    return 0;
}
