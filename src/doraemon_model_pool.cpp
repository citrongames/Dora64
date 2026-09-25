#include "doraemon_model_pool.h"
#include "librecomp/addresses.hpp"
#include <atomic>
#include <cstdio>
#include <cstdlib>
#include <cstring>

int32_t doraemon_model_table_offset = 0x14A0;
int32_t doraemon_model_scratch_offset = 0x18A0;
int32_t doraemon_model_storage_offset = -0x4360;

namespace {
    constexpr uint32_t ModelBytes = DORAEMON_MODEL_POOL_CAPACITY * DORAEMON_MODEL_STRIDE;
    constexpr uint32_t TableBytes = DORAEMON_MODEL_POOL_CAPACITY * sizeof(uint32_t);
    constexpr uint32_t ArenaBytes = ModelBytes + 2 * TableBytes;
    std::atomic<uint32_t> arena{0};
    static_assert(DORAEMON_MODEL_POOL_CAPACITY < 0x8000, "Original indices are signed 16-bit");
}

void doraemon_model_pool_init(uint8_t* rdram) {
    if (arena.load(std::memory_order_relaxed)) return;
    void* memory = recomp::alloc(rdram, ArenaBytes);
    if (!memory) {
        std::fputs("Unable to allocate the 2048-element model pool\n", stderr);
        std::abort();
    }
    std::memset(memory, 0, ArenaBytes);
    const uint32_t base = uint32_t(static_cast<uint8_t*>(memory) - rdram) | 0x80000000U;
    doraemon_model_storage_offset = int32_t(base - 0x80110000U);
    doraemon_model_table_offset = int32_t(base + ModelBytes - 0x80140000U);
    doraemon_model_scratch_offset = int32_t(base + ModelBytes + TableBytes - 0x80140000U);
    arena.store(base, std::memory_order_release);
}

void doraemon_model_pool_reset() {
    // reset_game drains graphics before the runtime discards the guest heap.
    // Level changes reuse this allocation and run the original pool initializer.
    arena.store(0, std::memory_order_release);
    doraemon_model_table_offset = 0x14A0;
    doraemon_model_scratch_offset = 0x18A0;
    doraemon_model_storage_offset = -0x4360;
}

uint32_t doraemon_model_pool_base() {
    return arena.load(std::memory_order_acquire) & 0x1FFFFFFFU;
}

uint32_t doraemon_model_pool_used(uint8_t* rdram) {
    const uint32_t used = MEM_HU(0, S32(0x80141CA0U));
    return used <= DORAEMON_MODEL_POOL_CAPACITY ? used : 0;
}

int doraemon_model_pool_matrix_address(uint32_t address) {
    const uint32_t base = arena.load(std::memory_order_acquire);
    if (!base || address < base || address - base >= ModelBytes) return 0;
    const uint32_t offset = (address - base) % DORAEMON_MODEL_STRIDE;
    // +0x60/+0xA0 are the two scale buffers emitted by func_8001E94C.
    // They precede the remaining nine matrices at +0xE0 through +0x2E0.
    return offset >= DORAEMON_MODEL_MATRICES_OFFSET &&
        offset <= DORAEMON_MODEL_MATRICES_OFFSET +
            DORAEMON_MODEL_MATRICES_SIZE - 0x40 &&
        (offset - DORAEMON_MODEL_MATRICES_OFFSET) % 0x40 == 0;
}
