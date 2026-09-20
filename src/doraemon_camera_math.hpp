#pragma once
#include <algorithm>
#include <cmath>

namespace doraemon::camera {
    constexpr float Pi = 3.14159265358979323846f;
    constexpr float ToRadians = Pi / 180.0f;
    constexpr float MinPitch = 0.0f, MaxPitch = 80.0f;
    struct Vec3 {
        float x, y, z;
        Vec3 operator+(Vec3 b) const { return {x+b.x, y+b.y, z+b.z}; }
        Vec3 operator-(Vec3 b) const { return {x-b.x, y-b.y, z-b.z}; }
        Vec3 operator*(float s) const { return {x*s, y*s, z*s}; }
    };
    inline float dot(Vec3 a, Vec3 b) { return a.x*b.x+a.y*b.y+a.z*b.z; }
    inline float length(Vec3 a) { return std::sqrt(dot(a,a)); }
    inline bool finite(Vec3 a) { return std::isfinite(a.x) && std::isfinite(a.y) && std::isfinite(a.z); }
    inline float wrap(float angle) { return std::remainder(angle, 360.0f); }
    inline float blend(float rate, float dt) { return 1.0f - std::exp(-rate * dt); }
    inline Vec3 direction(float yaw, float pitch) {
        const float y = yaw * ToRadians, p = pitch * ToRadians;
        return {std::sin(y)*std::cos(p), std::sin(p), std::cos(y)*std::cos(p)};
    }
    inline bool allows_manual(unsigned mode, int locked, int scripted, int paused, unsigned updateFlags) {
        return mode <= 1 && locked == 0 && scripted == 0 && paused == 0 && (updateFlags & 2) != 0;
    }
    inline void rotate(float& yaw, float& pitch, float x, float y, float mx, float my,
        float speed, float sensitivity, bool invert, float dt) {
        yaw = wrap(yaw - x*speed*dt - mx*sensitivity);
        pitch = std::clamp(pitch + (y*speed*dt + my*sensitivity)*(invert ? -1.0f : 1.0f), MinPitch, MaxPitch);
    }
    inline float collision_distance(float previous, float allowed, float dt) {
        // Pull in immediately, ease out only as far as the current unobstructed ray.
        return allowed < previous ? allowed : previous + (allowed-previous)*blend(8.0f, dt);
    }
}
