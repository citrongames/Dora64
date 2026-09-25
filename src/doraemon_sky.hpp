#pragma once

#include <cstdint>

namespace doraemon::sky {
    struct DisplayListRange {
        std::uint32_t begin;
        std::uint32_t end;
    };
    DisplayListRange frame_display_list_range(std::uint32_t buffer);
    void set_output_width(float nativeWidth);
    void reset();
}
