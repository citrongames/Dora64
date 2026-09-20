#pragma once
#include <cmath>
#include <cstdint>

namespace doraemon::sky {
    constexpr int SectorWidth = 320;
    constexpr int SectorCount = 4;
    constexpr int MaxWidth = 2048;
    struct Columns { int first, last; };
    inline Columns visible_columns(float origin, float width) {
        return { int(std::floor((160.0f - width * 0.5f - origin) / SectorWidth)),
            int(std::ceil((160.0f + width * 0.5f - origin) / SectorWidth)) - 1 };
    }
    inline int wrap_sector(int column) { return ((column % SectorCount) + SectorCount) % SectorCount; }
    inline uint32_t pack_coords(int x, int y) {
        return (uint32_t(uint16_t(x)) << 16) | uint16_t(y);
    }
}
