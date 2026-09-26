#include "doraemon_lighting.hpp"
#include "doraemon_sky.hpp"
#include "doraemon_model_pool.h"
#if defined(__ANDROID__)
#include "ultramodern/ultramodern.hpp"
#endif

#include <algorithm>
#include <array>
#include <chrono>
#include <cstdio>
#include <deque>
#include <vector>
#include <cstring>
#include <memory>
#include <mutex>

namespace {
    constexpr std::size_t kGraphicsBufferCount = 2;
    constexpr std::uint32_t kGraphicsBuffers = 0xCA240U;
    constexpr std::uint32_t kGraphicsStride = 0x8150U;
    constexpr std::uint32_t kDisplayListOffset = 0x120U;
    constexpr std::uint32_t kDisplayListCursor = 0x814CU;
    // This game streams nested display lists and geometry through this RAM bank.
    // RT64 may parse a prepared task after the game has reused the bank.
    constexpr std::uint32_t kFrameAssetsBegin = 0x1D0000U;
    constexpr std::uint32_t kFrameAssetsEnd = 0x1F0000U;
    constexpr std::uint32_t kLightsBase = 0x142BB8U;
    constexpr std::uint32_t kLightsSize = 9U * 0x1678U;

    struct FrameLights {
        std::uint32_t begin;
        std::uint32_t end;
        std::array<std::uint8_t, kLightsSize> data;
#if defined(__ANDROID__)
        std::vector<std::uint8_t> displayList;
        std::vector<std::uint8_t> frameAssets;
        std::uint32_t skyBegin = 0;
        std::uint32_t skyEnd = 0;
        std::vector<std::uint8_t> skyDisplayLists;
        std::uint32_t modelPoolBegin = 0;
        std::uint32_t modelCount = 0;
        std::vector<std::uint8_t> modelMatrices;
#endif
    };

    std::mutex framesMutex;
#if defined(__ANDROID__)
    // The game can replace the resource bank after finishing a display list
    // but before it submits the OSTask. Keep that frame's bank until submit.
    std::array<std::shared_ptr<FrameLights>, kGraphicsBufferCount> prepared;
    std::array<std::deque<std::shared_ptr<const FrameLights>>, kGraphicsBufferCount> submitted;
#if defined(DORA64_ANDROID_DIAGNOSTICS)
    std::size_t preparedBytes = 0;
    std::size_t pendingBytes = 0;
#endif
#else
    std::array<std::shared_ptr<const FrameLights>, kGraphicsBufferCount> frames;
#endif

    // A task retains its immutable snapshot until all light loads are parsed.
    // This state is owned by the graphics parser thread.
    std::shared_ptr<const FrameLights> currentFrame;
}

static std::shared_ptr<FrameLights> capture_frame(
    std::uint8_t* rdram, std::uint32_t buffer,
    std::uint32_t begin, std::uint32_t end)
{
    auto frame = std::make_shared<FrameLights>();
    frame->begin = begin;
    frame->end = end;
    // This Light pool is shared across both graphics buffers.
    std::memcpy(frame->data.data(), rdram + kLightsBase, kLightsSize);
#if defined(__ANDROID__)
    frame->displayList.assign(rdram + begin, rdram + end);
    frame->frameAssets.assign(rdram + kFrameAssetsBegin, rdram + kFrameAssetsEnd);
    const auto sky = doraemon::sky::frame_display_list_range(buffer);
    if (sky.end > sky.begin) {
        frame->skyBegin = sky.begin;
        frame->skyEnd = sky.end;
        frame->skyDisplayLists.assign(rdram + sky.begin, rdram + sky.end);
    }
    const auto modelBegin = doraemon_model_pool_base();
    const auto modelCount = doraemon_model_pool_used(rdram);
    if (modelBegin != 0 && modelCount != 0) {
        frame->modelPoolBegin = modelBegin;
        frame->modelCount = modelCount;
        frame->modelMatrices.resize(
            std::size_t(modelCount) * DORAEMON_MODEL_MATRICES_SIZE);
        for (std::uint32_t index = 0; index < modelCount; ++index) {
            std::memcpy(
                frame->modelMatrices.data() +
                    std::size_t(index) * DORAEMON_MODEL_MATRICES_SIZE,
                rdram + modelBegin +
                    std::size_t(index) * DORAEMON_MODEL_STRIDE +
                    DORAEMON_MODEL_MATRICES_OFFSET,
                DORAEMON_MODEL_MATRICES_SIZE);
        }
    }
#endif
    return frame;
}

#if defined(__ANDROID__) && defined(DORA64_ANDROID_DIAGNOSTICS)
static std::size_t snapshot_bytes(const FrameLights& frame) {
    return frame.data.size() + frame.displayList.size() +
        frame.frameAssets.size() + frame.skyDisplayLists.size() +
        frame.modelMatrices.size();
}
#endif

extern "C" void doraemon_snapshot_frame_lights(
    std::uint8_t* rdram, std::uint32_t buffer)
{
    if (buffer >= kGraphicsBufferCount) return;
    const auto graphics = kGraphicsBuffers + buffer * kGraphicsStride;
    const auto begin = graphics + kDisplayListOffset;
    std::uint32_t end = 0;
    std::memcpy(&end, rdram + graphics + kDisplayListCursor, 4);
    end &= 0x1FFFFFFFU;
    if (end < begin || end > graphics + kDisplayListCursor || (end & 7U) != 0) {
        return;
    }
#if defined(__ANDROID__)
    // PI DMA and the renderer use this same lock. Capture the bank when the
    // game finishes building this frame, before the next stage may load into
    // its overlapping RDRAM range.
    std::scoped_lock rdram_lock{ultramodern::get_graphics_rdram_mutex()};
#if defined(DORA64_ANDROID_DIAGNOSTICS)
    const auto started = std::chrono::steady_clock::now();
#endif
#endif
    auto frame = capture_frame(rdram, buffer, begin, end);
    std::lock_guard lock(framesMutex);
#if defined(__ANDROID__)
#if defined(DORA64_ANDROID_DIAGNOSTICS)
    if (prepared[buffer]) preparedBytes -= snapshot_bytes(*prepared[buffer]);
    preparedBytes += snapshot_bytes(*frame);
#endif
    prepared[buffer] = std::move(frame);
#if defined(DORA64_ANDROID_DIAGNOSTICS)
    static std::uint32_t buildSamples = 0;
    static std::uint64_t buildTotalUs = 0;
    static std::uint64_t buildMaxUs = 0;
    const auto buildUs = std::chrono::duration_cast<std::chrono::microseconds>(
        std::chrono::steady_clock::now() - started).count();
    buildTotalUs += static_cast<std::uint64_t>(buildUs);
    buildMaxUs = std::max(buildMaxUs, static_cast<std::uint64_t>(buildUs));
    if (++buildSamples == 300) {
        std::fprintf(stderr,
            "Dora64 built 300 snapshots: copy avg=%llu max=%llu us, prepared=%zu bytes\n",
            static_cast<unsigned long long>(buildTotalUs / buildSamples),
            static_cast<unsigned long long>(buildMaxUs), preparedBytes);
        std::fflush(stderr);
        buildSamples = 0;
        buildTotalUs = buildMaxUs = 0;
    }
#endif
#else
    frames[buffer] = std::move(frame);
#endif
}

void doraemon::lighting::submit_task(
    std::uint8_t* rdram, std::uint32_t address, std::uint32_t size)
{
#if defined(__ANDROID__)
    address &= 0x1FFFFFFFU;
    if (size == 0 || size > kDisplayListCursor - kDisplayListOffset) return;
    for (std::size_t index = 0; index < kGraphicsBufferCount; ++index) {
        const auto expected = kGraphicsBuffers + index * kGraphicsStride + kDisplayListOffset;
        if (address != expected) continue;

#if defined(DORA64_ANDROID_DIAGNOSTICS)
        const auto started = std::chrono::steady_clock::now();
#endif
        std::shared_ptr<FrameLights> frame;
        {
            std::lock_guard lock(framesMutex);
            frame = std::move(prepared[index]);
#if defined(DORA64_ANDROID_DIAGNOSTICS)
            if (frame) preparedBytes -= snapshot_bytes(*frame);
#endif
        }
        const bool builtFrame = frame && frame->begin == address &&
            frame->end <= address + size;
        if (!builtFrame) {
            static std::uint32_t fallbackReports = 0;
            if (fallbackReports++ < 8) {
                std::fprintf(stderr,
                    "Dora64 frame snapshot fallback: task=%08X size=%08X preparedEnd=%08X\n",
                    address, size, frame ? frame->end : 0);
                std::fflush(stderr);
            }
            frame = capture_frame(rdram, static_cast<std::uint32_t>(index),
                address, address + size);
        }
        else {
            // The game may append commands after the frame-builder returns.
            // Keep the earlier shared resources, but use the final task's DL.
#if defined(DORA64_ANDROID_DIAGNOSTICS)
            constexpr std::uint32_t probe = 0x1E19D0U - kFrameAssetsBegin;
            if (frame->frameAssets[probe] != rdram[0x1E19D0U]) {
                static std::uint32_t bankChangeReports = 0;
                if (bankChangeReports++ < 32) {
                    std::fprintf(stderr,
                        "Dora64 bank changed between build and submit: task=%08X old=%02X new=%02X\n",
                        address, unsigned(frame->frameAssets[probe]),
                        unsigned(rdram[0x1E19D0U]));
                    std::fflush(stderr);
                }
            }
#endif
            frame->end = address + size;
            frame->displayList.assign(rdram + address, rdram + address + size);
        }
#if defined(DORA64_ANDROID_DIAGNOSTICS)
        const auto elapsed = std::chrono::duration_cast<std::chrono::microseconds>(
            std::chrono::steady_clock::now() - started).count();
        const auto bytes = snapshot_bytes(*frame);
        const auto models = frame->modelCount;
#endif
        std::lock_guard lock(framesMutex);
        submitted[index].push_back(std::move(frame));
#if defined(DORA64_ANDROID_DIAGNOSTICS)
        pendingBytes += bytes;

        // Sparse diagnostics quantify the copies and prove whether pending
        // snapshots are accumulating during a long gameplay session.
        static std::uint32_t samples = 0;
        static std::uint64_t copiedBytes = 0;
        static std::uint64_t copiedUs = 0;
        static std::uint64_t maxCopiedBytes = 0;
        static std::uint64_t maxCopiedUs = 0;
        static std::uint32_t maxModels = 0;
        static std::size_t peakPendingBytes = 0;
        static std::size_t peakPendingTasks = 0;
        copiedBytes += bytes;
        copiedUs += static_cast<std::uint64_t>(elapsed);
        maxCopiedBytes = std::max(maxCopiedBytes, static_cast<std::uint64_t>(bytes));
        maxCopiedUs = std::max(maxCopiedUs, static_cast<std::uint64_t>(elapsed));
        maxModels = std::max(maxModels, models);
        peakPendingBytes = std::max(peakPendingBytes, pendingBytes);
        peakPendingTasks = std::max(peakPendingTasks,
            submitted[0].size() + submitted[1].size());
        if (++samples == 300) {
            std::fprintf(stderr,
                "Dora64 submitted 300 snapshots: copy avg=%llu max=%llu us, bytes avg=%llu max=%llu, models max=%u, pending=%zu prepared=%zu peakPending=%zu tasks peak=%zu\n",
                static_cast<unsigned long long>(copiedUs / samples),
                static_cast<unsigned long long>(maxCopiedUs),
                static_cast<unsigned long long>(copiedBytes / samples),
                static_cast<unsigned long long>(maxCopiedBytes),
                maxModels, pendingBytes, preparedBytes, peakPendingBytes, peakPendingTasks);
            std::fflush(stderr);
            samples = 0;
            copiedBytes = copiedUs = maxCopiedBytes = maxCopiedUs = 0;
            maxModels = 0;
            peakPendingBytes = pendingBytes;
            peakPendingTasks = submitted[0].size() + submitted[1].size();
        }
#endif
        return;
    }
#else
    (void)rdram;
    (void)address;
    (void)size;
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
        for (auto& queue : submitted) {
            if (!queue.empty() && queue.front()->begin == address) {
#if defined(DORA64_ANDROID_DIAGNOSTICS)
                pendingBytes -= snapshot_bytes(*queue.front());
#endif
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
#if defined(__ANDROID__) && defined(DORA64_ANDROID_DIAGNOSTICS)
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

doraemon::lighting::DisplayListSnapshot doraemon::lighting::current_sky_display_lists() {
#if defined(__ANDROID__)
    if (currentFrame && !currentFrame->skyDisplayLists.empty()) {
        return {currentFrame->skyDisplayLists.data(),
            currentFrame->skyBegin, currentFrame->skyEnd};
    }
#endif
    return {nullptr, 0, 0};
}

doraemon::lighting::DisplayListSnapshot doraemon::lighting::current_frame_assets() {
#if defined(__ANDROID__)
    if (currentFrame && !currentFrame->frameAssets.empty()) {
        return {currentFrame->frameAssets.data(), kFrameAssetsBegin, kFrameAssetsEnd};
    }
#endif
    return {nullptr, 0, 0};
}

doraemon::lighting::DisplayListSnapshot doraemon::lighting::current_display_list() {
#if defined(__ANDROID__)
    if (currentFrame && !currentFrame->displayList.empty()) {
        return {currentFrame->displayList.data(), currentFrame->begin, currentFrame->end};
    }
#endif
    return {nullptr, 0, 0};
}

const std::uint8_t* doraemon::lighting::model_matrix_data(
    std::uint32_t address)
{
#if defined(__ANDROID__)
    if (!currentFrame || currentFrame->modelCount == 0 ||
        address < currentFrame->modelPoolBegin) return nullptr;
    const auto relative = address - currentFrame->modelPoolBegin;
    const auto index = relative / DORAEMON_MODEL_STRIDE;
    const auto within = relative % DORAEMON_MODEL_STRIDE;
    if (index >= currentFrame->modelCount ||
        within < DORAEMON_MODEL_MATRICES_OFFSET ||
        within > DORAEMON_MODEL_MATRICES_OFFSET +
            DORAEMON_MODEL_MATRICES_SIZE - 0x40 ||
        (within - DORAEMON_MODEL_MATRICES_OFFSET) % 0x40 != 0) {
        return nullptr;
    }
    return currentFrame->modelMatrices.data() +
        std::size_t(index) * DORAEMON_MODEL_MATRICES_SIZE +
        within - DORAEMON_MODEL_MATRICES_OFFSET;
#else
    (void)address;
    return nullptr;
#endif
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
    for (auto& queue : submitted) queue.clear();
    prepared = {};
#if defined(DORA64_ANDROID_DIAGNOSTICS)
    preparedBytes = 0;
    pendingBytes = 0;
#endif
#else
    frames = {};
#endif
}
