#include "doraemon_sky.hpp"
#include "doraemon_sky_math.hpp"

#include <array>
#include <atomic>
#include <cstring>
#include <cstdlib>
#include <vector>

#include "librecomp/addresses.hpp"
#include "../lib/rt64/include/rt64_extended_gbi.h"

extern "C" void func_8008010C(uint8_t* rdram, recomp_context* ctx);

namespace {
    constexpr uint32_t FrameBase = 0xCA240;
    constexpr uint32_t FrameStride = 0x8150;
    constexpr uint32_t ArenaBytes = 0x10000;
    constexpr uint32_t ScratchOffset = 0xC000;
    constexpr uint32_t SectorTable = 0x8015B5F0;
    std::atomic<float> outputWidth{320.0f};
    std::array<uint32_t, 2> arenas{};
    uint32_t currentArena = 0;
    uint32_t arenaCursor = 0;
    float frameWidth = 320.0f;

    float guest_float(uint8_t* rdram, uint32_t address) {
        const uint32_t bits = MEM_W(0, S32(address));
        float value;
        std::memcpy(&value, &bits, sizeof(value));
        return value;
    }

    void emit(std::vector<uint32_t>& words, uint32_t a, uint32_t b) {
        words.push_back(a);
        words.push_back(b);
    }
}

void doraemon::sky::set_output_width(float width) {
    outputWidth.store(width, std::memory_order_relaxed);
}

void doraemon::sky::reset() {
    // The runtime drains old graphics work before resetting the guest heap.
    arenas = {};
    currentArena = 0;
    arenaCursor = 0;
    frameWidth = 320.0f;
}

extern "C" void doraemon_draw_sky_panel(uint8_t* rdram, recomp_context* ctx) {
    using namespace doraemon::sky;
    const int originalColumn = int(ctx->r17);
    const int originalRow = int(ctx->r16);
    const uint32_t guestCursor = uint32_t(ctx->r4);
    const uint32_t displayList = MEM_W(0, int32_t(guestCursor));
    if (originalColumn == 0 && originalRow == 0) {
        frameWidth = outputWidth.load(std::memory_order_relaxed);
        currentArena = 0;
        const uint32_t physical = displayList & 0x1FFFFFFFU;
        if (frameWidth > 323.2f && frameWidth <= MaxWidth && physical >= FrameBase &&
            physical < FrameBase + 2 * FrameStride) {
            const uint32_t buffer = (physical - FrameBase) / FrameStride;
            if (arenas[buffer] == 0) {
                if (void* memory = recomp::alloc(rdram, ArenaBytes)) {
                    arenas[buffer] = uint32_t(static_cast<uint8_t*>(memory) - rdram) | 0x80000000U;
                }
            }
            currentArena = arenas[buffer];
            arenaCursor = currentArena;
        }
    }
    if (!currentArena) {
        func_8008010C(rdram, ctx);
        return;
    }
    // Each original row now emits every visible horizontal sector. The second
    // column would duplicate that work. Leave the caller's loop registers intact.
    if (originalColumn != 0) return;

    const uint32_t entry = uint32_t(ctx->r2);
    const uint32_t entryOffset = entry - SectorTable;
    if (entryOffset >= 12 * 8 || (entryOffset & 7U) != 0) {
        func_8008010C(rdram, ctx);
        return;
    }
    const int baseColumn = int((entryOffset % 32) / 8);
    const uint32_t rowTable = SectorTable + (entryOffset / 32) * 32;
    const float x = guest_float(rdram, uint32_t(ctx->r29) + 0x30);
    const auto columns = visible_columns(x, frameWidth);
    const uint32_t scratch = currentArena + ScratchOffset;
    const uint32_t scratchCommands = scratch + 0x100;
    const uint32_t scratchStack = currentArena + ArenaBytes - 0x100;
    std::vector<uint32_t> words;
    words.reserve(4096);
    // The nested list has been reached; texture pointers retain original N64 semantics.
    emit(words, (RT64_EXTENDED_OPCODE << 24) | G_EX_SETRDRAMEXTENDED_V1, 0);
    emit(words, (RT64_EXTENDED_OPCODE << 24) | G_EX_SETRECTASPECT_V1, G_EX_ASPECT_ADJUST);

    for (int column = columns.first; column <= columns.last; column++) {
        recomp_context panel = *ctx;
        panel.f_odd = ctx->f_odd == &ctx->f1.u32l ? &panel.f1.u32l : &panel.f0.u32h;
        panel.r29 = int32_t(scratchStack);
        panel.r4 = int32_t(scratch);
        const uint32_t sector = rowTable + wrap_sector(baseColumn + column) * 8;
        panel.r5 = MEM_W(0, int32_t(sector));
        for (int offset = 0x10; offset <= 0x44; offset += 4) {
            MEM_W(offset, panel.r29) = MEM_W(offset, ctx->r29);
        }
        MEM_W(0x30, panel.r29) = 0; // Canonical whole sector: no early horizontal clipping.
        MEM_W(0x40, panel.r29) = MEM_W(4, int32_t(sector));
        if (column != 0) MEM_W(0x24, panel.r29) = 1;
        MEM_W(0, int32_t(scratch)) = scratchCommands;
        func_8008010C(rdram, &panel);
        const uint32_t end = MEM_W(0, int32_t(scratch));
        // Recorded sectors generate 816 bytes. Scratch space is kept separate
        // from the guest stack and from both in-flight output display lists.
        if (end < scratchCommands || end > scratch + 0x2000) std::abort();
        const int shift = int(std::lround((x + column * SectorWidth) * 4.0f));
        for (uint32_t address = scratchCommands; address < end; address += 8) {
            const uint32_t a = MEM_W(0, S32(address));
            const uint32_t b = MEM_W(4, S32(address));
            if ((a >> 24) == 0xE4 && address + 24 <= end) {
                // Same three-command footprint as F3D's E4/B4/B3 rectangle.
                // Texture loads, UVs, derivatives and vertical clipping remain original.
                const uint32_t st = MEM_W(12, S32(address));
                const uint32_t delta = MEM_W(20, S32(address));
                emit(words, (RT64_EXTENDED_OPCODE << 24) | G_EX_TEXRECT_V1,
                    ((b >> 24) & 7U) | (G_EX_ORIGIN_NONE << 3) | (G_EX_ORIGIN_NONE << 15));
                emit(words, pack_coords(int((b >> 12) & 0xFFF) + shift, b & 0xFFF),
                    pack_coords(int((a >> 12) & 0xFFF) + shift, a & 0xFFF));
                emit(words, st, delta);
                address += 16;
            }
            else emit(words, a, b);
        }
    }
    emit(words, (RT64_EXTENDED_OPCODE << 24) | G_EX_SETRECTASPECT_V1, G_EX_ASPECT_AUTO);
    emit(words, 0xB8000000U, 0); // F3D end display list.
    const uint32_t bytes = uint32_t(words.size() * sizeof(uint32_t));
    if (arenaCursor + bytes > currentArena + ScratchOffset) std::abort();
    for (uint32_t i = 0; i < words.size(); i++) MEM_W(0, gpr(S32(arenaCursor + i * 4))) = words[i];
    // Hook DL pointers only carry 28 bits and still use segment lookup. Use a
    // regular F3D DL command with its full virtual address and extended RDRAM.
    MEM_W(0, int32_t(displayList)) = RT64_HOOK_MAGIC_NUMBER;
    MEM_W(4, int32_t(displayList)) = (RT64_HOOK_OP_ENABLE << 28) | RT64_EXTENDED_OPCODE;
    MEM_W(8, int32_t(displayList)) = (RT64_EXTENDED_OPCODE << 24) | G_EX_SETRDRAMEXTENDED_V1;
    MEM_W(12, int32_t(displayList)) = 1;
    MEM_W(16, int32_t(displayList)) = 0x06000000U;
    MEM_W(20, int32_t(displayList)) = arenaCursor;
    MEM_W(0, int32_t(guestCursor)) = displayList + 24;
    arenaCursor += bytes;
}
