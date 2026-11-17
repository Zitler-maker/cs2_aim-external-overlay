#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <tlhelp32.h>
#include <cstdio>
#include <cstdint>

struct Vec3 { float x, y, z; };

struct Offsets {
    uintptr_t dwEntityList = 0x271FBB8;
    uintptr_t dwLocalPlayerController = 0x2542548;
    uintptr_t dwLocalPlayerPawn = 0x256A948;
    uintptr_t dwViewMatrix = 0x2570250;
    uintptr_t dwViewAngles = 0x2581098;
    uintptr_t m_hPlayerPawn = 0x92C;
    uintptr_t m_iTeamNum = 0x3E7;
    uintptr_t m_iHealth = 0x34C;
    uintptr_t m_vOldOrigin = 0x14A4;
    uintptr_t m_pGameSceneNode = 0x330;
    uintptr_t m_modelState = 0x140;
    uintptr_t m_vecViewOffset = 0xF60;
    uintptr_t m_angEyeAngles = 0x3600;
    uintptr_t boneArrayFromModelState = 0x80;
} off;

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

void walkEntityList() {
    printf("--- entity walk ---\n");
    for (int i = 0; i < 64; i++) {
        uintptr_t entry = ReadT<uintptr_t>(g_client + off.dwEntityList + 8 * ((i & 0x7FFF) >> 9) + 16 * i);
        if (!entry) continue;
        uintptr_t ctrl = ReadT<uintptr_t>(entry + 120 * (i & 0x1FF));
        if (!ctrl) continue;
        uintptr_t pawnHandle = ReadT<uintptr_t>(ctrl + off.m_hPlayerPawn);
        if (!pawnHandle || pawnHandle == 0xFFFFFFFF) continue;
        uintptr_t listEntry2 = ReadT<uintptr_t>(g_client + off.dwEntityList + 0x8 * ((pawnHandle & 0x7FFF) >> 9) + 16 * (pawnHandle & 0x1FF));
        if (!listEntry2) continue;
        uintptr_t pawn = ReadT<uintptr_t>(listEntry2 + 120 * (pawnHandle & 0x1FF));
        if (!pawn) continue;
        int team = ReadT<int>(pawn + off.m_iTeamNum);
        int hp = ReadT<int>(pawn + off.m_iHealth);
        Vec3 org = ReadT<Vec3>(pawn + off.m_vOldOrigin);
        uintptr_t sceneNode = ReadT<uintptr_t>(pawn + off.m_pGameSceneNode);
        uintptr_t modelState = ReadT<uintptr_t>(sceneNode + off.m_modelState);
        uintptr_t bonePtr = ReadT<uintptr_t>(modelState + off.boneArrayFromModelState);
        Vec3 head = ReadT<Vec3>(bonePtr + 6 * 32);
        printf("[%02d] ctrl=%llx pawn=%llx team=%d hp=%d org=(%.1f %.1f %.1f) head=(%.1f %.1f %.1f) bones=%llx\n",
            i, (unsigned long long)ctrl, (unsigned long long)pawn, team, hp,
            org.x, org.y, org.z, head.x, head.y, head.z, (unsigned long long)bonePtr);
    }
}

int main() {
    g_pid = 20776;
    g_h = OpenProcess(PROCESS_VM_READ | PROCESS_QUERY_INFORMATION, FALSE, (DWORD)g_pid);
    if (!g_h) { printf("open failed %lu\n", GetLastError()); return 1; }
    g_client = GetModuleBase(g_pid, L"client.dll");
    printf("pid=%llu client=0x%llx\n", (unsigned long long)g_pid, (unsigned long long)g_client);

    uintptr_t lpCtrl = ReadT<uintptr_t>(g_client + off.dwLocalPlayerController);
    printf("lpCtrl=%llx\n", (unsigned long long)lpCtrl);
    if (lpCtrl) {
        uintptr_t pawnHandle = ReadT<uintptr_t>(lpCtrl + off.m_hPlayerPawn);
        printf("lpPawnHandle=%llx\n", (unsigned long long)pawnHandle);
        uintptr_t listEntry2 = ReadT<uintptr_t>(g_client + off.dwEntityList + 0x8 * ((pawnHandle & 0x7FFF) >> 9) + 16 * (pawnHandle & 0x1FF));
        uintptr_t pawn = ReadT<uintptr_t>(listEntry2 + 120 * (pawnHandle & 0x1FF));
        printf("lpPawn=%llx\n", (unsigned long long)pawn);
        if (pawn) {
            printf("lp team=%d hp=%d\n", ReadT<int>(pawn + off.m_iTeamNum), ReadT<int>(pawn + off.m_iHealth));
            Vec3 org = ReadT<Vec3>(pawn + off.m_vOldOrigin);
            printf("lp origin=(%.1f %.1f %.1f)\n", org.x, org.y, org.z);
            Vec3 vo = ReadT<Vec3>(pawn + off.m_vecViewOffset);
            printf("lp viewoff=(%.1f %.1f %.1f)\n", vo.x, vo.y, vo.z);
            Vec3 ea = ReadT<Vec3>(pawn + off.m_angEyeAngles);
            printf("lp eyeang=(%.1f %.1f %.1f)\n", ea.x, ea.y, ea.z);
        }
        uintptr_t lpPawnDirect = ReadT<uintptr_t>(g_client + off.dwLocalPlayerPawn);
        printf("lpPawnDirect=%llx team=%d hp=%d\n", (unsigned long long)lpPawnDirect,
            lpPawnDirect ? ReadT<int>(lpPawnDirect + off.m_iTeamNum) : -1,
            lpPawnDirect ? ReadT<int>(lpPawnDirect + off.m_iHealth) : -1);
        uintptr_t va = ReadT<uintptr_t>(g_client + off.dwViewAngles);
        printf("viewAnglesPtr=%llx ang=(%.1f %.1f %.1f)\n", (unsigned long long)va,
            va ? ReadT<float>(va) : -1.f, va ? ReadT<float>(va + 4) : -1.f, va ? ReadT<float>(va + 8) : -1.f);
    }
    walkEntityList();
    CloseHandle(g_h);
    return 0;
}
