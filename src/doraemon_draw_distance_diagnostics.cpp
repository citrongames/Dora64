#include "doraemon_model_pool.h"
#include "doraemon_draw_distance.h"
#include "librecomp/addresses.hpp"
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>

namespace {
    using Clock = std::chrono::steady_clock;
    constexpr uint32_t Actors = 0x800FB820;
    // Only called from the guest thread. Disabled unless explicitly requested.
    FILE* log_file() {
        static FILE* file = []() -> FILE* {
            const char* value = std::getenv("DORAEMON_DRAW_DISTANCE_DIAGNOSTICS");
            return value && std::strcmp(value, "1") == 0 ?
                std::fopen("doraemon-draw-distance.log", "w") : nullptr;
        }();
        return file;
    }
    uint32_t word(uint8_t* rdram, uint32_t address) { return MEM_W(0, gpr(S32(address))); }
    float number(uint8_t* rdram, uint32_t address) {
        uint32_t bits = word(rdram, address); float value;
        std::memcpy(&value, &bits, 4); return value;
    }
    void actor_line(FILE* file, uint8_t* rdram, uint32_t actor) {
        const float x = number(rdram, actor+0x10), y = number(rdram, actor+0x14), z = number(rdram, actor+0x18);
        const float dx = x-number(rdram, 0x800F0548), dy = y-number(rdram, 0x800F054C), dz = z-number(rdram, 0x800F0550);
        std::fprintf(file, "A %u type=%04X state=%d flags=%08X model=%u parts=%u+%u callback=%08X pos=%.1f,%.1f,%.1f distance=%.1f cached=%.1f vars=%08X,%08X,%08X,%08X\n",
            (actor-Actors)/256, unsigned(MEM_HU(0x48, gpr(S32(actor)))), int32_t(word(rdram,actor+4)),
            word(rdram,actor+0x8C), word(rdram,actor+0x38), word(rdram,actor+0x3C), word(rdram,actor+0x40),
            word(rdram,actor+0x80), x,y,z,std::sqrt(dx*dx+dy*dy+dz*dz), number(rdram,actor+0x9C),
            word(rdram,actor+0xA0),word(rdram,actor+0xA4),word(rdram,actor+0xA8),word(rdram,actor+0xAC));
    }
}

void doraemon_draw_distance_trace(uint8_t* rdram) {
    FILE* file = log_file(); if (!file) return;
    static auto previous = Clock::time_point{};
    static int previousStage = -1;
    static float previousFactor = 0;
    const auto now = Clock::now();
    const int stage = MEM_BU(0, gpr(S32(0x800F38BD)));
    const float factor = doraemon_draw_distance_limit(1.0f, 0);
    if (stage == previousStage && factor == previousFactor && now-previous < std::chrono::seconds(1)) return;
    previous=now; previousStage=stage; previousFactor=factor;
    std::fprintf(file, "SNAP stage=%d factor=%.2f pool=%u capacity=2048 pause=%d override=%d eye=%.1f,%.1f,%.1f player=%.1f,%.1f,%.1f\n",
        stage,factor,unsigned(MEM_HU(0,gpr(S32(0x80141CA0)))),int(MEM_B(0,gpr(S32(0x800F38E0)))),
        int(MEM_H(0,gpr(S32(0x80141CAA)))),number(rdram,0x800F0548),number(rdram,0x800F054C),number(rdram,0x800F0550),
        number(rdram,0x800F38B0),number(rdram,0x800F38B4),number(rdram,0x800F38B8));
    for (unsigned index=0; index<256; index++) {
        const uint32_t actor=Actors+index*256;
        if (MEM_H(0,gpr(S32(actor)))) actor_line(file,rdram,actor);
    }
    std::fflush(file);
}

void doraemon_draw_distance_allocation(uint8_t* rdram, uint32_t actor, int result) {
    FILE* file = log_file(); if (!file || result != -1) return;
    if (actor<Actors || actor>=Actors+256*256 || ((actor-Actors)&255)) return;
    static unsigned failures=0;
    ++failures;
    // Bound output if the same distant actor retries every update.
    if (failures>1000 && failures%100 != 0) return;
    std::fprintf(file, "ALLOC_FAIL count=%u stage=%u factor=%.2f pool=%u\n",failures,
        unsigned(MEM_BU(0,gpr(S32(0x800F38BD)))),doraemon_draw_distance_limit(1,0),unsigned(MEM_HU(0,gpr(S32(0x80141CA0)))));
    actor_line(file,rdram,actor); std::fflush(file);
}
