#include "doraemon_lighting.hpp"

#include <array>
#include <cstdio>
#include <deque>
#include <vector>
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
#if defined(__ANDROID__)
        std::vector<std::uint8_t> displayList;
#endif
    };

    std::mutex framesMutex;
#if defined(__ANDROID__)
    // Keep every prepared frame until the corresponding graphics task is read.
    // A low rendering rate can let the game reuse either graphics buffer first.
    std::array<std::deque<std::shared_ptr<const FrameLights>>, 2> frames;
#else
    std::array<std::shared_ptr<const FrameLights>, 2> frames;
#endif

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
#if defined(__ANDROID__)
    // The game has finished writing the primary display list at this point.
    // Hold those exact commands until RT64 finishes the matching OSTask.
    frame->displayList.assign(rdram + frame->begin, rdram + frame->end);
#endif

    std::lock_guard lock(framesMutex);
#if defined(__ANDROID__)
    frames[buffer].push_back(std::move(frame));
#else
    frames[buffer] = std::move(frame);
#endif
}

void doraemon::lighting::begin_task(
    std::uint32_t address, std::uint32_t size)
{
    address &= 0x1FFFFFFFU;
    currentFrame.reset();
    {
        std::lock_guard lock(framesMutex);
#if defined(__ANDROID__)
        for (auto& queue : frames) {
            if (!queue.empty() && queue.front()->begin == address) {
                currentFrame = std::move(queue.front());
                queue.pop_front();
                break;
            }
        }
#else
        for (const auto& frame : frames) {
            if (frame && frame->begin == address &&
                static_cast<std::uint64_t>(frame->end) <=
                    static_cast<std::uint64_t>(address) + size) {
                currentFrame = frame;
                break;
            }
        }
#endif
    }
#if defined(__ANDROID__)
    static bool reportedActive = false;
    static bool reportedMissing = false;
    const bool active = currentFrame && !currentFrame->displayList.empty();
    if ((active && !reportedActive) || (!active && !reportedMissing)) {
        std::fprintf(stderr, "Dora64 primary DL snapshot %s: task=%08X size=%08X bytes=%zu\n",
            active ? "active" : "missing", address, size,
            currentFrame ? currentFrame->displayList.size() : 0);
        std::fflush(stderr);
        (active ? reportedActive : reportedMissing) = true;
    }
#endif
}

doraemon::lighting::DisplayListSnapshot doraemon::lighting::current_display_list() {
#if defined(__ANDROID__)
    if (currentFrame && !currentFrame->displayList.empty()) {
        return {currentFrame->displayList.data(), currentFrame->begin, currentFrame->end};
    }
#endif
    return {nullptr, 0, 0};
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
#if defined(__ANDROID__)
    for (auto& queue : frames) queue.clear();
#else
    frames = {};
#endif
}
