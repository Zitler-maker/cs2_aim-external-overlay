#pragma once
#include <mutex>
#include <atomic>
#include "vecmath.h"

struct PlayerSnap {
    bool valid = false;
    Vec3 head{}, origin{};
    int hp = 0, team = 0;
    float dist = 0.f;
    Vec2 headScr{}, feetScr{};
};

struct GameSnap {
    bool localValid = false;
    int localTeam = 0;
    Vec3 localEye{};
    Vec3 localAngles{};
    PlayerSnap players[64];
    int count = 0;
};

struct Settings {
    bool aimbot = true;
    bool esp = true;
    float fov = 8.f;
    float smooth = 1.f;
};

extern GameSnap g_snap;
extern std::mutex g_snapMtx;
extern Settings g_set;
extern std::atomic<bool> g_running;
