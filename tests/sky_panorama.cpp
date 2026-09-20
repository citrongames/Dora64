#include "doraemon_sky.hpp"
#include "doraemon_sky_math.hpp"
#include "librecomp/addresses.hpp"
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <vector>

extern "C" void func_8008010C(uint8_t*, recomp_context*);
extern "C" void doraemon_draw_sky_panel(uint8_t*, recomp_context*);
namespace { size_t heapCursor = 0x1000000; }
namespace recomp {
    void* alloc(uint8_t* rdram, size_t bytes) {
        auto* result = rdram + heapCursor;
        heapCursor += bytes;
        return result;
    }
}
namespace {
    void require(bool value, const char* message) {
        if (!value) { std::fprintf(stderr, "FAIL: %s\n", message); std::exit(1); }
    }
    void word(uint8_t* rdram, uint32_t address, uint32_t value) { MEM_W(0, S32(address)) = value; }
    uint32_t word(uint8_t* rdram, uint32_t address) { return MEM_W(0, S32(address)); }
    void floating(uint8_t* rdram, uint32_t address, float value) {
        uint32_t bits; std::memcpy(&bits, &value, 4); word(rdram, address, bits);
    }
    constexpr uint32_t Cursor = 0x80001000, Stack = 0x80090000, Display = 0x800CA430;
    struct Rect { int left, top, right, bottom; uint32_t image; float s, t, ds, dt, ox, oy; };
    struct DecodeState { bool extended = false; uint32_t image = 0; float tileX = 0, tileY = 0, loadX = 0, loadY = 0; };
    void decode(uint8_t* rdram, uint32_t begin, uint32_t end, std::vector<Rect>& rects, DecodeState& state) {
        for (uint32_t at = begin; at < end; at += 8) {
            const auto a = word(rdram, at), b = word(rdram, at + 4);
            if (a == 0x06000000) {
                require(state.extended && (b & 0x80000000U), "full guest address for extended-memory DL");
                decode(rdram, b, b + 0xC000, rects, state);
                require(!state.extended, "original address mode restored after sky DL");
            }
            else if (a == 0x6400002C) state.extended = (b & 1) != 0;
            else if ((a >> 24) == 0xB8) return;
            else if ((a >> 24) == 0xFD) state.image = b;
            else if ((a >> 24) == 0xF4) { state.loadX = ((a >> 12) & 4095) / 4.0f; state.loadY = (a & 4095) / 4.0f; }
            else if ((a >> 24) == 0xF2) { state.tileX = ((a >> 12) & 4095) / 4.0f; state.tileY = (a & 4095) / 4.0f; }
            else if ((a >> 24) == 0xE4 || a == 0x64000002) {
                Rect r{};
                uint32_t st, delta;
                if (a == 0x64000002) {
                    const auto ul = word(rdram, at + 8), lr = word(rdram, at + 12);
                    r.left = int16_t(ul >> 16); r.top = int16_t(ul);
                    r.right = int16_t(lr >> 16); r.bottom = int16_t(lr);
                    st = word(rdram, at + 16); delta = word(rdram, at + 20);
                }
                else {
                    r.left = (b >> 12) & 4095; r.top = b & 4095;
                    r.right = (a >> 12) & 4095; r.bottom = a & 4095;
                    st = word(rdram, at + 12); delta = word(rdram, at + 20);
                }
                r.image = state.image; r.s = int16_t(st >> 16) / 32.0f; r.t = int16_t(st) / 32.0f;
                r.ds = int16_t(delta >> 16) / 1024.0f; r.dt = int16_t(delta) / 1024.0f;
                r.ox = state.loadX - state.tileX; r.oy = state.loadY - state.tileY;
                if (r.right > r.left && r.bottom > r.top) rects.push_back(r);
                at += 16;
            }
        }
    }
    std::vector<Rect> generate(uint8_t* rdram, bool wide, float width, int baseColumn, int x, int y, uint32_t display = Display) {
        doraemon::sky::set_output_width(width);
        word(rdram, Cursor, display);
        for (int col = 0; col < 2; col++) for (int row = 0; row < 2; row++) {
            recomp_context ctx{};
            ctx.f_odd = &ctx.f0.u32h;
            const uint32_t entry = 0x8015B5F0 + (row + 1) * 32 + doraemon::sky::wrap_sector(baseColumn + col) * 8;
            ctx.r2 = S32(entry); ctx.r4 = S32(Cursor); ctx.r5 = word(rdram, entry);
            ctx.r6 = 2; ctx.r7 = 1; ctx.r16 = row; ctx.r17 = col; ctx.r29 = S32(Stack);
            const int params[] = {88, 60, 32, 32, 1, 1, 320, 240};
            for (int i = 0; i < 8; i++) word(rdram, Stack + 0x10 + i * 4, params[i]);
            floating(rdram, Stack + 0x30, float(x + col * 320)); floating(rdram, Stack + 0x34, float(y + row * 240));
            floating(rdram, Stack + 0x38, 4.0f); floating(rdram, Stack + 0x3C, 4.0f);
            word(rdram, Stack + 0x40, word(rdram, entry + 4)); word(rdram, Stack + 0x44, 0);
            if (wide) {
                const auto before = ctx;
                doraemon_draw_sky_panel(rdram, &ctx);
                if (width > 323.2f && width <= 2048) require(std::memcmp(&ctx, &before, sizeof(ctx)) == 0, "panorama wrapper preserves caller registers");
            }
            else func_8008010C(rdram, &ctx);
        }
        std::vector<Rect> result; DecodeState state;
        decode(rdram, display, word(rdram, Cursor), result, state);
        return result;
    }
    const Rect* pixel(const std::vector<Rect>& rects, int x, int y) {
        const Rect* result = nullptr;
        for (const auto& r : rects) if (x * 4 >= r.left && x * 4 < r.right && y * 4 >= r.top && y * 4 < r.bottom) result = &r;
        return result;
    }
}
int main() {
    std::vector<uint8_t> memory(0x2000000); auto* rdram = memory.data();
    for (int i = 0; i < 12; i++) { word(rdram, 0x8015B5F0 + i * 8, 0x80200000 + i * 0x10000); word(rdram, 0x8015B5F4 + i * 8, 0x80300000 + i * 0x1000); }
    for (float width : {426.667f, 560.0f, 853.333f, 1706.667f, 2048.0f}) {
        for (int column = 0; column < 4; column++) for (int x : {-320, -319, -256, -160, -128, -17, -1, 0}) {
            const auto original = generate(rdram, false, width, column, x, -37);
            const auto wide = generate(rdram, true, width, column, x, -37);
            for (int py : {12, 60, 127, 200, 226}) for (int px = 16; px < 303; px++) {
                const auto* a = pixel(original, px, py); const auto* b = pixel(wide, px, py);
                require(a && b, "central sky coverage");
                const float au = a->s + (px - a->left / 4.0f) * a->ds + a->ox;
                const float bu = b->s + (px - b->left / 4.0f) * b->ds + b->ox;
                const float av = a->t + (py - a->top / 4.0f) * a->dt + a->oy;
                const float bv = b->t + (py - b->top / 4.0f) * b->dt + b->oy;
                if (a->image != b->image || std::abs(au-bu) > 0.032f || std::abs(av-bv) > 0.032f) {
                    std::fprintf(stderr, "Mismatch width=%.1f col=%d x=%d pixel=%d,%d image=%08X/%08X uv=%f,%f/%f,%f\n", width,column,x,px,py,a->image,b->image,au,av,bu,bv); return 1;
                }
            }
            for (int px = int(std::ceil(160-width/2)); px < int(std::floor(160+width/2)); px++) require(pixel(wide, px, 100), "wide sky coverage without holes");
        }
    }
    // Native 4:3 uses the original generator without modifications.
    const auto original = generate(rdram, false, 320, 0, -81, -23);
    const auto native = generate(rdram, true, 320, 0, -81, -23);
    require(original.size() == native.size(), "4:3 rectangle count");
    for (size_t i = 0; i < original.size(); i++) require(std::memcmp(&original[i], &native[i], sizeof(Rect)) == 0, "4:3 commands unchanged");
    doraemon::sky::reset();
    generate(rdram, true, 426.667f, 0, -81, -23, Display + 0x8150);
    require(heapCursor <= 0x1040000, "bounded frame arenas across cold reset");
    std::puts("Sky panorama: original central texture coordinates, wide coverage, 4:3, reset and caller state passed.");
}
