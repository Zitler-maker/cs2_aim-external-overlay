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
    printf("client=0x%llx\n", (unsigned long long)g_client);

    uintptr_t lpPawn = ReadT<uintptr_t>(g_client + 0x256A948);
    Vec3 org = ReadT<Vec3>(lpPawn + 0x14A4);
    printf("lp origin=(%.1f %.1f %.1f)\n", org.x, org.y, org.z);

    uintptr_t node = ReadT<uintptr_t>(lpPawn + 0x330);
    printf("sceneNode=0x%llx\n", (unsigned long long)node);
    uintptr_t ms = ReadT<uintptr_t>(node + 0x140);
    printf("modelState=0x%llx\n", (unsigned long long)ms);

    printf("--- pointers inside sceneNode (0..0x200, step 8) ---\n");
    for (uintptr_t off = 0; off <= 0x200; off += 8) {
        uintptr_t v = ReadT<uintptr_t>(node + off);
        if (v > 0x10000) {
            printf("sceneNode+0x%03llx -> 0x%llx\n", (unsigned long long)off, (unsigned long long)v);
        }
    }

    printf("--- pointers inside modelState (0..0x300, step 8) ---\n");
    for (uintptr_t off = 0; off <= 0x300; off += 8) {
        uintptr_t v = ReadT<uintptr_t>(ms + off);
        if (v > 0x10000) {
            printf("modelState+0x%03llx -> 0x%llx\n", (unsigned long long)off, (unsigned long long)v);
        }
    }

    // for each candidate pointer in modelState, check whether +6*32 holds a float3 near head height
    printf("--- bone array candidates (target z ~ %.0f) ---\n", org.z + 64.0);
    for (uintptr_t off = 0; off <= 0x300; off += 8) {
        uintptr_t v = ReadT<uintptr_t>(ms + off);
        if (v < 0x10000) continue;
        Vec3 b6 = ReadT<Vec3>(v + 6 * 32);
        float dx = b6.x - org.x, dy = b6.y - org.y, dz = b6.z - org.z;
        float dist = sqrtf(dx * dx + dy * dy + dz * dz);
        if (fabsf(dx) < 30.f && fabsf(dy) < 30.f && b6.z > org.z && b6.z < org.z + 90.f && dist < 90.f) {
            printf("HIT: modelState+0x%03llx -> 0x%llx  bone6=(%.1f %.1f %.1f) dist=%.1f\n",
                (unsigned long long)off, (unsigned long long)v, b6.x, b6.y, b6.z, dist);
        }
    }
    for (uintptr_t off = 0; off <= 0x200; off += 8) {
        uintptr_t v = ReadT<uintptr_t>(node + off);
        if (v < 0x10000) continue;
        Vec3 b6 = ReadT<Vec3>(v + 6 * 32);
        float dx = b6.x - org.x, dy = b6.y - org.y, dz = b6.z - org.z;
        float dist = sqrtf(dx * dx + dy * dy + dz * dz);
        if (fabsf(dx) < 30.f && fabsf(dy) < 30.f && b6.z > org.z && b6.z < org.z + 90.f && dist < 90.f) {
            printf("HIT: sceneNode+0x%03llx -> 0x%llx  bone6=(%.1f %.1f %.1f) dist=%.1f\n",
                (unsigned long long)off, (unsigned long long)v, b6.x, b6.y, b6.z, dist);
        }
    }
    CloseHandle(g_h);
    return 0;
}
