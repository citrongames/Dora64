#include "doraemon_draw_distance.h"
#include <algorithm>
#include <atomic>
#include <cmath>

namespace {
    std::atomic<float> distanceMultiplier{1.0f};
    std::atomic<float> modelLodMultiplier{5.0f};
}

float doraemon_draw_distance_configure(float multiplier) {
    multiplier = std::isfinite(multiplier) ? std::clamp(multiplier, 1.0f, 5.0f) : 1.0f;
    distanceMultiplier.store(multiplier, std::memory_order_relaxed);
    return multiplier;
}

float doraemon_draw_distance_limit(float original, uint32_t actor_flags) {
    // These actors are destroyed, not merely unloaded, outside their range.
    // Extending their lifetime would change transient effects/projectiles.
    if (actor_flags & 0x1000U) return original;
    const float multiplier = distanceMultiplier.load(std::memory_order_relaxed);
    if (multiplier == 1.0f) return original;
    return original * multiplier;
}

float doraemon_model_lod_configure(float multiplier) {
    multiplier = std::isfinite(multiplier) ? std::clamp(multiplier, 1.0f, 5.0f) : 1.0f;
    modelLodMultiplier.store(multiplier, std::memory_order_relaxed);
    return multiplier;
}

float doraemon_model_lod_limit(float original) {
    // Scale only the character geometry switch in func_80073E80.
    // Keep the real camera distance intact for culling and game logic.
    return original * modelLodMultiplier.load(std::memory_order_relaxed);
}
