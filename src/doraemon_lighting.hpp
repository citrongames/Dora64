#pragma once

#include <cstdint>

namespace doraemon::lighting {
    // Called only by the graphics parser thread, around an OSTask.
    void begin_task(std::uint32_t address, std::uint32_t size);
    const void* light_data(std::uint32_t address);
    void end_task();
    void reset();
}

// Called after func_8000BDE0 finishes the graphics buffer, before submission.
extern "C" void doraemon_snapshot_frame_lights(
    std::uint8_t* rdram, std::uint32_t buffer);
