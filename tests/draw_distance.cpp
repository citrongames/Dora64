#include "doraemon_draw_distance.h"
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <limits>
#include <initializer_list>

namespace {
    void require(bool value, const char* message) {
        if (!value) { std::fprintf(stderr, "FAIL: %s\n", message); std::exit(1); }
    }
}
int main() {
    require(doraemon_draw_distance_limit(1500, 0) == 1500, "default is original");
    for (float factor : {1.0f, 1.5f, 2.0f, 5.0f}) {
        require(doraemon_draw_distance_configure(factor) == factor, "valid factor retained");
        for (float limit : {1500.0f, 5000.0f}) {
            for (unsigned flags : {0U, 2U, 0x20U, 0x100U}) {
                const float scaled = doraemon_draw_distance_limit(limit, flags);
                require(scaled == limit * factor, "both actor range classes scale linearly");

                if (factor > 1) require(limit + 1 < scaled, "objects beyond original range admitted");
            }
            require(doraemon_draw_distance_limit(limit, 0x1000) == limit, "despawn range retained");
            require(doraemon_draw_distance_limit(limit, 0x1022) == limit, "despawn flag wins over category");
        }
    }
    require(doraemon_draw_distance_configure(-2) == 1, "cannot reduce below original");
    require(doraemon_draw_distance_configure(99) == 5, "upper bound enforced");
    require(doraemon_draw_distance_configure(std::numeric_limits<float>::infinity()) == 1, "invalid infinity resets");
    require(doraemon_draw_distance_configure(std::numeric_limits<float>::quiet_NaN()) == 1, "invalid NaN resets");
    require(doraemon_draw_distance_limit(1500, 0) == 1500, "restoring original applies immediately");
    std::puts("Draw distance: original defaults, range classes, bounds and despawn exclusions passed.");
}
