#pragma once
#include <cmath>

struct Vec2 { float x = 0, y = 0; };
struct Vec3 { float x = 0, y = 0, z = 0; };

inline Vec3 operator-(const Vec3& a, const Vec3& b) { return { a.x - b.x, a.y - b.y, a.z - b.z }; }
inline Vec3 operator+(const Vec3& a, const Vec3& b) { return { a.x + b.x, a.y + b.y, a.z + b.z }; }
inline float len3(const Vec3& v) { return sqrtf(v.x * v.x + v.y * v.y + v.z * v.z); }

struct Mat4 {
    float m[4][4]{};
    float* operator[](int r) { return m[r]; }
    const float* operator[](int r) const { return m[r]; }
};

inline float clampf(float v, float lo, float hi) { return v < lo ? lo : (v > hi ? hi : v); }

inline float normDeg(float a) {
    while (a > 180.f) a -= 360.f;
    while (a < -180.f) a += 360.f;
    return a;
}

// screen: world -> screen, returns false if behind camera
inline bool w2s(const Mat4& vm, const Vec3& world, float scrW, float scrH, Vec2& out) {
    float w = vm[3][0] * world.x + vm[3][1] * world.y + vm[3][2] * world.z + vm[3][3];
    if (w < 0.01f) return false;
    float x = vm[0][0] * world.x + vm[0][1] * world.y + vm[0][2] * world.z + vm[0][3];
    float y = vm[1][0] * world.x + vm[1][1] * world.y + vm[1][2] * world.z + vm[1][3];
    out.x = (scrW * 0.5f) * (1.f + x / w);
    out.y = (scrH * 0.5f) * (1.f - y / w);
    return true;
}

inline Vec3 calcAngle(const Vec3& from, const Vec3& to) {
    Vec3 d = to - from;
    float yaw = atan2f(d.y, d.x) * 57.295779513f;
    float pitch = -atan2f(d.z, sqrtf(d.x * d.x + d.y * d.y)) * 57.295779513f;
    return { pitch, yaw, 0.f };
}

inline float angDist(const Vec3& a, const Vec3& b) {
    float dp = fabsf(normDeg(a.x - b.x));
    float dy = fabsf(normDeg(a.y - b.y));
    return sqrtf(dp * dp + dy * dy);
}
