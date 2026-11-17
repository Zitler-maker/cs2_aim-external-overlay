#include "shared.h"
#include "mem.h"
#include "offsets.h"
#include "config.h"
#include <windows.h>
#include <thread>
#include <cstring>

GameSnap g_snap;
std::mutex g_snapMtx;
Settings g_set;
std::atomic<bool> g_running{ true };

namespace {

constexpr int CHUNK_SLOTS = 512;
constexpr int CHUNK_BYTES = CHUNK_SLOTS * 0x70;

struct ChunkCache {
    uintptr_t ptr = 0;
    uint8_t* buf = nullptr;
    bool valid = false;
};
ChunkCache g_cc[16];

struct BoneCache {
    uintptr_t pawn = 0;
    uintptr_t bones = 0;
};
BoneCache g_boneCache[64];
int g_boneCacheCount = 0;
int g_boneCursor = 0;
uintptr_t g_lastLocalPawn = 0;
uintptr_t g_lastSys = 0;
#ifdef CHEAT_DBG
bool g_dbgWasConnected = false;
bool g_dbgHadLocal = false;
#endif

bool chunkLoad(int c, uintptr_t sysPtr) {
    if (c < 0 || c > 15) return false;
    ChunkCache& cc = g_cc[c];
    uintptr_t ptr = mem::readT<uintptr_t>(sysPtr + g_off.entityChunksOffset + 8 * c);
    if (!ptr) { cc.valid = false; cc.ptr = 0; return false; }
    if (cc.valid && cc.ptr == ptr) return true;
    if (!cc.buf) cc.buf = new uint8_t[CHUNK_BYTES];
    if (!mem::read(ptr, cc.buf, CHUNK_BYTES)) { cc.valid = false; cc.ptr = 0; return false; }
    cc.ptr = ptr;
    cc.valid = true;
    return true;
}

inline uintptr_t entityFromBuf(int index, uintptr_t sysPtr) {
    int c = (index & 0x7FFF) >> 9;
    if (!chunkLoad(c, sysPtr)) return 0;
    uintptr_t v = 0;
    memcpy(&v, g_cc[c].buf + g_off.entityIdentityStride * (index & 0x1FF), 8);
    return v;
}

void resetBoneCache() { g_boneCacheCount = 0; g_boneCursor = 0; }

uintptr_t boneArrayFor(uintptr_t pawn) {
    for (int i = 0; i < g_boneCacheCount; i++) {
        if (g_boneCache[i].pawn == pawn) return g_boneCache[i].bones;
    }
    uintptr_t node = mem::readT<uintptr_t>(pawn + g_off.m_pGameSceneNode);
    if (!node) return 0;
    uintptr_t bones = mem::readT<uintptr_t>(node + g_off.m_modelState + g_off.m_boneArray);
    if (g_boneCacheCount < 64) {
        g_boneCache[g_boneCacheCount].pawn = pawn;
        g_boneCache[g_boneCacheCount].bones = bones;
        g_boneCacheCount++;
    } else {
        g_boneCache[g_boneCursor].pawn = pawn;
        g_boneCache[g_boneCursor].bones = bones;
        g_boneCursor = (g_boneCursor + 1) % 64;
    }
    return bones;
}

Vec3 bonePos(uintptr_t pawn, int bone) {
    uintptr_t bones = boneArrayFor(pawn);
    if (!bones) return {};
    return mem::readT<Vec3>(bones + bone * 32);
}

void collect(GameSnap& s) {
    uintptr_t client = mem::client();
    if (!client) return;

    // server connection gate: idle when not fully signed on
    uintptr_t engine2 = mem::moduleBase(L"engine2.dll");
    if (engine2) {
        uintptr_t ngc = mem::readT<uintptr_t>(engine2 + g_off.dwNetworkGameClient);
        if (ngc) {
            uint32_t signOn = mem::readT<uint32_t>(ngc + g_off.signOnStateOffset);
            if (signOn < 6) {
#ifdef CHEAT_DBG
                if (g_dbgWasConnected) {
                    printf("[dbg] disconnected (signOn=%u) — idle\n", signOn);
                    fflush(stdout);
                    g_dbgWasConnected = false;
                }
#endif
                s.localValid = false;
                s.count = 0;
                return;
            }
#ifdef CHEAT_DBG
            if (!g_dbgWasConnected) {
                printf("[dbg] connected (signOn=%u) — rescanning\n", signOn);
                fflush(stdout);
                g_dbgWasConnected = true;
            }
#endif
        }
    }

    uintptr_t sysPtr = mem::readT<uintptr_t>(client + g_off.dwEntityList);
    if (!sysPtr) return;
    if (sysPtr != g_lastSys) {
#ifdef CHEAT_DBG
        printf("[dbg] EntitySystem changed 0x%llx -> 0x%llx — full rescan\n",
            (unsigned long long)g_lastSys, (unsigned long long)sysPtr);
        fflush(stdout);
#endif
        g_lastSys = sysPtr;
        resetBoneCache();
        for (auto& cc : g_cc) cc.valid = false;
    }

    Mat4 vm = mem::readT<Mat4>(client + g_off.dwViewMatrix);
    float scrW = (float)GetSystemMetrics(SM_CXSCREEN);
    float scrH = (float)GetSystemMetrics(SM_CYSCREEN);

    uintptr_t localPawnPtr = 0;
    uintptr_t ctrl = mem::readT<uintptr_t>(client + g_off.dwLocalPlayerController);
    if (ctrl) {
        uint32_t handle = mem::readT<uint32_t>(ctrl + g_off.m_hPlayerPawn);
        if (handle && handle != 0xFFFFFFFF) localPawnPtr = entityFromBuf(handle & 0x7FFF, sysPtr);
    }
    if (!localPawnPtr) localPawnPtr = mem::readT<uintptr_t>(client + g_off.dwLocalPlayerPawn);

    int localTeam = 0;
    Vec3 localEye{};
    bool localAlive = false;
    if (localPawnPtr) {
        localTeam = mem::readT<int>(localPawnPtr + g_off.m_iTeamNum);
        int hp = mem::readT<int>(localPawnPtr + g_off.m_iHealth);
        if (localTeam >= 2 && localTeam <= 3 && hp > 0 && hp <= 100) {
            localAlive = true;
            Vec3 origin = mem::readT<Vec3>(localPawnPtr + g_off.m_vOldOrigin);
            Vec3 vo = mem::readT<Vec3>(localPawnPtr + g_off.m_vecViewOffset);
            localEye = origin + vo;
        }
        if (localPawnPtr != g_lastLocalPawn) {
            g_lastLocalPawn = localPawnPtr;
            resetBoneCache();
        }
#ifdef CHEAT_DBG
        if (localAlive && !g_dbgHadLocal) {
            printf("[dbg] REACQUIRED local player: pawn=0x%llx team=%d hp=%d\n",
                (unsigned long long)localPawnPtr, localTeam,
                mem::readT<int>(localPawnPtr + g_off.m_iHealth));
            fflush(stdout);
            g_dbgHadLocal = true;
        }
        if (!localAlive) g_dbgHadLocal = false;
#endif
    }
    s.localValid = localAlive;
    s.localTeam = localTeam;
    s.localEye = localEye;
    s.localAngles = mem::readT<Vec3>(client + g_off.dwViewAngles);
    s.count = 0;
    if (!localAlive) return;

    uintptr_t seen[64];
    int seenCount = 0;
    for (int i = 1; i <= 64 && s.count < 63; i++) {
        uintptr_t ent = entityFromBuf(i, sysPtr);
        if (!ent) continue;
        uint32_t handle = mem::readT<uint32_t>(ent + g_off.m_hPlayerPawn);
        if (!handle || handle == 0xFFFFFFFF) continue;
        int team = mem::readT<int>(ent + g_off.m_iTeamNum);
        if (team < 2 || team > 3 || team == localTeam) continue;
        uintptr_t pawn = entityFromBuf(handle & 0x7FFF, sysPtr);
        if (!pawn) continue;
        int hp = mem::readT<int>(pawn + g_off.m_iHealth);
        if (hp <= 0 || hp > 100) continue;

        bool dup = false;
        for (int d = 0; d < seenCount; d++) {
            if (seen[d] == pawn) { dup = true; break; }
        }
        if (dup) continue;
        seen[seenCount++] = pawn;

        uintptr_t node = mem::readT<uintptr_t>(pawn + g_off.m_pGameSceneNode);
        if (!node) continue;
        if (mem::readT<bool>(node + g_off.m_bDormant)) continue;

        Vec3 origin = mem::readT<Vec3>(pawn + g_off.m_vOldOrigin);
        Vec3 head = bonePos(pawn, 6);
        if (len3(head) < 1.f || len3(origin) < 1.f) continue;

        PlayerSnap p;
        p.valid = w2s(vm, head, scrW, scrH, p.headScr);
        if (!p.valid) continue;
        w2s(vm, origin, scrW, scrH, p.feetScr);
        p.head = head;
        p.origin = origin;
        p.hp = hp;
        p.team = team;
        p.dist = len3(origin - localEye) * 0.0254f;
        s.players[s.count++] = p;
    }
}

void aim(const GameSnap& s) {
    if (!g_set.aimbot) return;
    if (!(GetAsyncKeyState(VK_XBUTTON2) & 0x8000)) return;
    if (!s.localValid || s.count == 0) return;

    uintptr_t client = mem::client();
    Vec3 cur = mem::readT<Vec3>(client + g_off.dwViewAngles);

    const PlayerSnap* best = nullptr;
    float bestD = 1e9f;
    for (int i = 0; i < s.count; i++) {
        const PlayerSnap& p = s.players[i];
        Vec3 ang = calcAngle(s.localEye, p.head);
        float d = angDist(cur, ang);
        if (d < bestD) { bestD = d; best = &p; }
    }
    if (!best || bestD > g_set.fov) return;

    Vec3 tgt = calcAngle(s.localEye, best->head);
    float sm = g_set.smooth < 1.f ? 1.f : g_set.smooth;
    Vec3 next{ cur.x + normDeg(tgt.x - cur.x) / sm, cur.y + normDeg(tgt.y - cur.y) / sm, 0.f };
    next.x = clampf(next.x, -89.f, 89.f);
    next.y = normDeg(next.y);
    mem::write(client + g_off.dwViewAngles, &next, sizeof(Vec3));
}

}

void gameThread() {
#ifdef CHEAT_DBG
    DWORD lastPrint = 0;
    int collects = 0;
    DWORD lastAliveCheck = 0;
#endif
    int tick = 0;
    while (g_running) {
#ifndef CHEAT_DBG
        static DWORD lastAliveCheck = 0;
#endif
        DWORD now = GetTickCount();
        if (now - lastAliveCheck > 1000) {
            lastAliveCheck = now;
            if (!mem::alive()) {
#ifdef CHEAT_DBG
                printf("[dbg] cs2 process gone — re-attaching\n");
                fflush(stdout);
#endif
                mem::detach();
                mem::invalidate();
                g_lastSys = 0;
                g_lastLocalPawn = 0;
                resetBoneCache();
                for (auto& cc : g_cc) cc.valid = false;
                while (g_running && !mem::attach(L"cs2.exe")) Sleep(1000);
                if (!mem::alive()) continue;
            }
        }
        static DWORD lastIniCheck = 0;
        if (now - lastIniCheck > 1000) {
            lastIniCheck = now;
            if (config::reloadIfChanged()) {
#ifdef CHEAT_DBG
                printf("[dbg] offsets.ini reloaded\n");
                fflush(stdout);
#endif
                g_lastSys = 0;
                g_lastLocalPawn = 0;
                resetBoneCache();
                for (auto& cc : g_cc) cc.valid = false;
            }
        }
        GameSnap s;
        if (tick == 0) {
            collect(s);
#ifdef CHEAT_DBG
            collects++;
#endif
            {
                std::lock_guard<std::mutex> lk(g_snapMtx);
                g_snap = s;
            }
        } else {
            std::lock_guard<std::mutex> lk(g_snapMtx);
            s = g_snap;
        }
        aim(s);
#ifdef CHEAT_DBG
        if (now - lastPrint > 1000) {
            lastPrint = now;
            printf("[dbg] collects/s=%d local=%d team=%d eye=(%.0f %.0f %.0f) players=%d\n",
                collects, s.localValid, s.localTeam, s.localEye.x, s.localEye.y, s.localEye.z, s.count);
            for (int i = 0; i < s.count; i++) {
                const PlayerSnap& p = s.players[i];
                printf("  p%d hp=%d dist=%.1fm scr=(%.0f %.0f)\n", i, p.hp, p.dist, p.headScr.x, p.headScr.y);
            }
            fflush(stdout);
        }
#endif
        tick = (tick + 1) % 2;
        Sleep(1);
    }
}
