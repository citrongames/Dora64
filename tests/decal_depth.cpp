#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>

using uint = std::uint32_t;
using std::abs;
using std::min;
using std::max;

// CPU equivalent of the HLSL intrinsic; the tested formula itself is shared.
int firstbithigh(uint value) {
    int bit = -1;
    while (value != 0) {
        ++bit;
        value >>= 1;
    }
    return bit;
}

#include "shaders/DepthDelta.hlsli"

namespace {
    void require(bool condition, const char* message) {
        if (!condition) {
            std::fprintf(stderr, "FAIL: %s\n", message);
            std::exit(EXIT_FAILURE);
        }
    }
}

int main() {
    constexpr float unit = 1.0f / 32768.0f;
    // RDP normalization and comparator reference values, including exact POTs.
    const struct { float input; unsigned expected; } reference[] = {
        { 0.0f, 1 }, { 0.9f, 1 }, { 1.0f, 2 }, { 1.9f, 2 },
        { 2.0f, 4 }, { 3.0f, 4 }, { 4.0f, 8 }, { 28.0f, 32 },
        { 32.0f, 64 }, { 44.0f, 64 }, { 16383.0f, 16384 },
        { 16384.0f, 32768 }, { 65535.0f, 32768 }
    };
    for (const auto& row : reference) {
        require(RasterDepthDelta(row.input * unit, 0) == row.expected * unit,
            "RDP integer depth normalization");
        require(RasterDepthDelta(0, -row.input * unit) == row.expected * unit,
            "depth slope axis/sign must not change tolerance");
    }
    require(RasterDepthDelta(15.9f * unit, 16.9f * unit) == 32 * unit,
        "truncate each derivative before summing");
    require(RasterDepthDelta(20000 * unit, -20000 * unit) == 32768 * unit,
        "combined slope saturation");

    // Measured ground/decal samples from the user's 2026-09-06 capture.
    // CPU rasterization of recorded RSP geometry, at native pixel centers.
    const struct { float decalZ, groundZ, dy; bool previouslyVisible; } samples[] = {
        { .829805222f, .830652441f, .000878841741f, true },  // Frame 390.
        { .815309664f, .816664505f, .001301430350f, false }, // Frame 450.
        { .772898611f, .774643580f, .001350633225f, false }, // Frame 540.
        { .762117172f, .763947404f, .001343241913f, false }, // Frame 750.
        { .820375303f, .821757270f, .001369429790f, false }, // Frame 1170.
        { .791018908f, .792626881f, .001358349906f, false }, // Frame 1350.
        { .752042302f, .753949913f, .001337564477f, false }  // Frame 1680.
    };
    for (const auto& sample : samples) {
        const float gap = abs(sample.decalZ - sample.groundZ);
        require((gap <= sample.dy) == sample.previouslyVisible,
            "recorded geometry must reproduce the original disappearance");
        for (float scale : { 0.75f, 1.0f, 2.0f, 4.0f, 8.0f }) {
            const float nativeDy = (sample.dy / scale) * scale;
            require(gap <= RasterDepthDelta(0, nativeDy),
                "decal must remain on its ground surface at every resolution");
            require(gap <= RasterDepthDelta(nativeDy, 0),
                "camera rotation must preserve the depth tolerance");
        }
    }

    // A recorded foreground character triangle remains an occluder.
    require(abs(.795698220f - .775760510f) >
        RasterDepthDelta(0, .001343241913f), "reject a distinct foreground surface");
    require(abs(.795698220f - .815635930f) >
        RasterDepthDelta(0, .001343241913f), "reject a distinct background surface");
    // v2 frame 555: the entire ground part of the shadow failed with only dzpix.
    // The ground stores dz=64 and its Z encoding has exponent 1, doubling dzmem.
    const float closeGap = abs(.687962808f - .690352787f);
    require(closeGap > 64 * unit, "v2 must reproduce the incomplete first fix");
    require(closeGap <= RasterDecalDepthTolerance(64 * unit, 64 * unit, 1),
        "close ground needs the RDP stored-depth precision adjustment");
    require(RasterDecalDepthTolerance(unit, unit, 0) == 16 * unit,
        "coarsest stored depth has a minimum delta of 16");
    require(RasterDecalDepthTolerance(unit, unit, 1) == 8 * unit,
        "exponent 1 has a minimum delta of 8");
    require(RasterDecalDepthTolerance(unit, unit, 2) == 4 * unit,
        "exponent 2 has a minimum delta of 4");
    require(RasterDecalDepthTolerance(32 * unit, 64 * unit, 3) == 64 * unit,
        "fine depth encodings must not double the surface delta");
    require(RasterDecalDepthTolerance(128 * unit, 32 * unit, 3) == 128 * unit,
        "incoming delta can dominate the surface delta");
    require(RasterDecalDepthTolerance(unit, 1.0f, 0) == 1.0f,
        "saturated coarse stored delta forces coplanarity");

    for (float scale : { 0.75f, 1.0f, 2.0f, 4.0f, 8.0f }) {
        const float center = .690352787f;
        const float slope = .0012938f / scale;
        const float recovered = RasterSurfaceDepthDerivative(center,
            center - slope, center - 2 * slope, center + slope, center + 2 * slope);
        const float surfaceDelta = RasterDepthDelta(0, recovered * scale);
        require(surfaceDelta == 64 * unit, "recover planar ground dz at every resolution");
        require(closeGap <= RasterDecalDepthTolerance(64 * unit, surfaceDelta, 1),
            "reconstructed close-ground comparison");

        // A character on one side must not turn its depth gap into a huge dzmem.
        const float edge = RasterSurfaceDepthDerivative(center,
            center - slope, center - 2 * slope, .4f, .39f);
        require(RasterDepthDelta(0, edge * scale) == surfaceDelta,
            "use continuous ground beside a silhouette");
        require(abs(.795698220f - .775760510f) >
            RasterDecalDepthTolerance(64 * unit, surfaceDelta, 1),
            "separate foreground remains an occluder after memory adjustment");
    }
    require(RasterSurfaceDepthDerivative(.7f, .4f, .39f, .9f, .91f) == 0,
        "reject discontinuities on both sides instead of expanding decal tolerance");
    require(RasterSurfaceDepthDerivative(.7f, .7f, .7f, .7f, .7f) == 0,
        "flat or clamped-border depth must not create a slope");
    // Coincidental extrapolation across an edge is bounded by the other side.
    require(RasterSurfaceDepthDerivative(.7f, .6f, .5f, .699f, .2f) < .002f,
        "do not use a large one-sided gap even if it extrapolates to the center");

    std::puts("Decal depth regression checks passed.");
}
