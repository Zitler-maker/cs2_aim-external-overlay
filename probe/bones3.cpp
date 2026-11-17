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

    uintptr_t lpPawn = ReadT<uintptr_t>(g_client + 0x256A948);
    Vec3 org = ReadT<Vec3>(lpPawn + 0x14A4);
    printf("lp origin=(%.1f %.1f %.1f)\n", org.x, org.y, org.z);

    uintptr_t node = ReadT<uintptr_t>(lpPawn + 0x330);

    // modelState inline at node+0x140, bone array pointer candidates inside it
    uintptr_t candOffs[] = { 0x80, 0x88, 0x90, 0xA0, 0xB0, 0xC0, 0xD0, 0xE0, 0xF0, 0x100, 0x120, 0x130 };
    int strides[] = { 32, 48, 64 };
    int posOffs[] = { 0, 12, 16, 32 };
    for (int ci = 0; ci < 12; ci++) {
        uintptr_t arr = ReadT<uintptr_t>(node + 0x140 + candOffs[ci]);
        if (arr < 0x10000) continue;
        for (int si = 0; si < 3; si++) {
            for (int pi = 0; pi < 4; pi++) {
                Vec3 b6 = ReadT<Vec3>(arr + 6 * strides[si] + posOffs[pi]);
                float dx = b6.x - org.x, dy = b6.y - org.y, dz = b6.z - org.z;
                float dist = sqrtf(dx * dx + dy * dy + dz * dz);
                if (fabsf(dx) < 30.f && fabsf(dy) < 30.f && b6.z > org.z - 5.f && b6.z < org.z + 90.f && dist < 90.f) {
                    printf("HIT: inlineMS+0x%03llx stride=%d posOff=%d bone6=(%.1f %.1f %.1f) dist=%.1f\n",
                        (unsigned long long)candOffs[ci], strides[si], posOffs[pi], b6.x, b6.y, b6.z, dist);
                }
            }
        }
    }
    CloseHandle(g_h);
    return 0;
}
