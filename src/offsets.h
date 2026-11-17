#pragma once
#include <cstdint>

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
    uintptr_t m_bDormant = 0x103;
    uintptr_t m_vecViewOffset = 0xF60;
    uintptr_t m_angEyeAngles = 0x3600;
    uintptr_t m_boneArray = 0x80;
    uintptr_t entityChunksOffset = 0x10;
    uintptr_t entityIdentityStride = 0x70;
    uintptr_t dwNetworkGameClient = 0x9B88C0;
    uintptr_t signOnStateOffset = 0x230;
};

extern Offsets g_off;
