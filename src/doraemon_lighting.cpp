#include "doraemon_lighting.hpp"

#include <array>
#include <cstring>
#include <memory>
#include <mutex>

namespace {
    constexpr std::uint32_t kGraphicsBuffers = 0xCA240U;
    constexpr std::uint32_t kGraphicsStride = 0x8150U;
    constexpr std::uint32_t kDisplayListOffset = 0x120U;
    constexpr std::uint32_t kDisplayListCursor = 0x814CU;
    constexpr std::uint32_t kLightsBase = 0x142BB8U;
    constexpr std::uint32_t kLightsSize = 9U * 0x1678U;

    struct FrameLights {
        std::uint32_t begin;
        std::uint32_t end;
        std::array<std::uint8_t, kLightsSize> data;
    };

    std::mutex framesMutex;
    std::array<std::shared_ptr<const FrameLights>, 2> frames;

    // A task retains its immutable snapshot until all light loads are parsed.
    // This state is owned by the graphics parser thread.
    std::shared_ptr<const FrameLights> currentFrame;
}

extern "C" void doraemon_snapshot_frame_lights(
    std::uint8_t* rdram, std::uint32_t buffer)
{
    if (buffer >= frames.size()) {
        return;
    }

    const auto graphics = kGraphicsBuffers + buffer * kGraphicsStride;
    auto frame = std::make_shared<FrameLights>();
    frame->begin = graphics + kDisplayListOffset;
    std::memcpy(&frame->end, rdram + graphics + kDisplayListCursor, 4);
    frame->end &= 0x1FFFFFFFU;
    if (frame->end < frame->begin ||
        frame->end > graphics + kDisplayListCursor ||
        (frame->end & 7U) != 0) {
        return;
    }

    // The original game double-buffers its display lists and matrices, but
    // reuses this Light pool every frame. It contains both the per-object
    // records at +0x38 and the world geometry records at +0x1038.
    std::memcpy(frame->data.data(), rdram + kLightsBase, kLightsSize);

    std::lock_guard lock(framesMutex);
    frames[buffer] = std::move(frame);
}

void doraemon::lighting::begin_task(
    std::uint32_t address, std::uint32_t size)
{
    address &= 0x1FFFFFFFU;
    currentFrame.reset();
    std::lock_guard lock(framesMutex);
    for (const auto& frame : frames) {
        if (frame && frame->begin == address &&
            static_cast<std::uint64_t>(frame->end) <=
                static_cast<std::uint64_t>(address) + size) {
            currentFrame = frame;
            break;
        }
    }
}

const void* doraemon::lighting::light_data(std::uint32_t address) {
    // Constants and lights outside the game's shared pool keep the regular
    // RDRAM path. No guest addresses or display list commands are rewritten.
    if (!currentFrame || address < kLightsBase ||
        address > kLightsBase + kLightsSize - 16U) {
        return nullptr;
    }
    return currentFrame->data.data() + (address - kLightsBase);
}

void doraemon::lighting::end_task() {
    currentFrame.reset();
}

void doraemon::lighting::reset() {
    // The runtime invokes the renderer reset after draining old tasks and
    // stopping guest threads, before clearing RDRAM for the new game.
    currentFrame.reset();
    std::lock_guard lock(framesMutex);
    frames = {};
}
