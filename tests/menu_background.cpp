#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <vector>

#include "render/rt64_menu_background.h"
#include "menu_background_trace.h"
#include "render/rt64_screen_transition.h"

namespace {
    void require(bool condition, const char* message) {
        if (!condition) {
            std::fprintf(stderr, "FAIL: %s\n", message);
            std::exit(EXIT_FAILURE);
        }
    }
}

int main() {
    using namespace RT64::MenuBackground;
    // Measured native rectangles and TMEM hashes from widescreen11.log, frame 360.
    std::array<Tile, TileCount> tiles{};
    for (int half = 0; half < 2; half++) {
        for (int row = 0; row < Rows; row++) {
            for (int col = 0; col < Columns; col++) {
                const int x = 16 + half * 48 + col * 96;
                const int y = 12 + row * 72;
                tiles[half * 9 + row * 3 + col] = { x * 4, y * 4, (x + 48) * 4, (y + 72) * 4,
                    half ? 0x1F33B15CEFDE8F54ULL : 0x06E9C413F3C12EBFULL };
            }
        }
    }
    require(matches(tiles, 64, 48, 1212, 908), "recognize the recorded menu mosaic");
    auto otherPalette = tiles;
    for (int i = 0; i < TileCount; i++) otherPalette[i].textureHash = i < 9 ? 123 : 456;
    require(matches(otherPalette, 64, 48, 1212, 908), "palette must not affect recognition");
    auto broken = tiles;
    broken[5].left += 4;
    require(!matches(broken, 64, 48, 1212, 908), "reject unrelated rectangle layouts");
    broken = tiles;
    broken[8].textureHash++;
    require(!matches(broken, 64, 48, 1212, 908), "reject non-repeating pictures");
    require(!matches(tiles, 200, 200, 900, 700), "do not widen an interior content scissor");

    for (const auto& fixture : menuTraceFixtures) {
        const auto& sc = fixture.scissor;
        require(matches(fixture.tiles, sc[0], sc[1], sc[2], sc[3]), "recognize actual file/pause/options/book traces");
        for (int row = 0; row < Rows; row++) {
            int sources = 0;
            for (int half = 0; half < 2; half++) for (int col = 0; col < Columns; col++) {
                const int index = half * Rows * Columns + row * Columns + col;
                if (!isRepeatSource(index)) continue;
                sources++;
                require(fixture.tiles[index].right - fixture.tiles[index].left == TileWidth * 4,
                    "only complete tiles are used for horizontal repeats");
            }
            require(sources == 2, "one complete motif per row without duplicate draws");
        }
    }

    // Real final-overlay bounds from doraemon-fade.log. File/options use a
    // full-screen scissor; pause/book already clip the last source pixel.
    using RT64::ScreenTransition::matches;
    require(matches(64, 48, 1216, 912, 0, 1280, 0xFC119623, 0xFF2FFFFF),
        "recognize pre-game menu transition with a larger scissor");
    require(matches(64, 48, 1212, 908, 64, 1212, 0xFC119623, 0xFF2FFFFF),
        "recognize in-game menu transition with clipped edges");
    require(matches(64, 48, 1212, 912, 64, 1212, 0xFC119623, 0xFF2FFFFF),
        "recognize transition during vertical scissor animation");
    require(!matches(64, 48, 1216, 912, 580, 696, 0xFC119623, 0xFF2FFFFF),
        "do not override a narrow horizontal reveal window");
    require(!matches(64, 48, 256, 336, 0, 1280, 0xFC119623, 0xFF2FFFFF),
        "background tiles are not screen transitions");
    require(!matches(128, 192, 1152, 832, 0, 1280, 0xFC119623, 0xFF2FFFFF),
        "book and other inset pictures are not screen transitions");
    require(!matches(64, 48, 1216, 912, 0, 1280, 0, 0),
        "do not match a different combiner mode");

    // Every horizontal output pixel gets exactly one tile: no holes or overlap,
    // including fractional scaling, very wide outputs and a native 4:3 control.
    const struct { int width, height; } sizes[] = {
        {320, 240}, {1280, 720}, {1920, 1080}, {1262, 704}, {1365, 767},
        {2560, 1080}, {3440, 1440}, {5120, 1440}, {7680, 1080}
    };
    for (const auto& size : sizes) {
        std::vector<int> coverage(size.width, 0);
        const float scale = float(size.height) / 240.0f;
        for (int col = 0; col < 2; col++) {
            const float left = float(16 + col * 48 - 160);
            const float right = left + 48;
            const auto repeats = visibleRepeats(size.width * .5f + left * scale,
                48 * scale, RepeatWidth * scale, float(size.width));
            for (int repeat = repeats.first; repeat <= repeats.last; repeat++) {
                const auto place = placeRepeat(left, right, RepeatWidth, scale, size.width * .5f, repeat);
                for (int x = std::max(0, int(place.left)); x < std::min(size.width, int(place.right)); x++) coverage[x]++;
            }
            const auto original = placeRepeat(left, right, RepeatWidth, scale, size.width * .5f, 0);
            require(original.left == std::round(size.width * .5f + left * scale), "preserve original central tile position");
            require(original.right == std::round(size.width * .5f + right * scale), "preserve original central tile width");
        }
        for (int count : coverage) require(count == 1, "tiles must meet without gaps or double blending");
    }
    std::puts("Menu background checks passed.");
}
