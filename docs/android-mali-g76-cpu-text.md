# Huawei P40 Pro: missing CPU dialogue text

## Test13 evidence — 2026-09-26

Maintainer confirms test13 restores dialogue text on Redmi Note 7. First startup
was much faster and subsequent startup almost instantaneous. On Huawei P40 Pro,
the game renders but CPU text remains completely absent (also absent before
test13). Maintainer additionally reports old scene textures flickering in the
letterbox area while the cinematic black bars move into place.

ADB: ELS_NX9 / ELS-N29, Kirin 990, Android API 29, display build
ELS-N29 12.0.0.386(C10E2R4P6). Installed 1.0.3-test13, code 52.
GPU Mali-G76, vendor 5045, device 1913716736, driver 75497472, Vulkan 1.1.97.
Settings: Russian, Original2x, Display, Expand, MSAA None, ThreePoint.
Screenshot shows a complete scene/portrait but a blank text area, overlay 31.2 FPS.
This is one captured frame, not a performance benchmark.

Native log confirms all intended paths: format-matched framebuffer storage,
local textures (16 sampled images/stage, nonuniform=0), separate coverage
because dual-source blending is unavailable. No pipeline creation failure.
The cache loaded 649393 bytes successfully and saved that same size at startup.

Evidence: workspace p40-20260926/{dialogue-test13.png,test13-stderr.log,
settings.json,vk.json}. No game launch/install/input injection by the agent.

## Confirmed remaining API violation

Both Mali and Redmi report maxTexelBufferElements=65536. A full 320x240 native
framebuffer has 76800 pixels, but the native read path made an R16_UINT texel
buffer view over the complete allocation. Allocations can retain their previous
larger size even for subsequent smaller updates. Plume creates the view with
range=buffer->desc.size. This exceeds the texel limit; lack of a returned error
does not establish correctness. In addition, FORMATTED + UNORDERED_ACCESS gives
the buffer storage-texel usage, while R16_UINT lacks storage-texel support on
both devices, even when that particular view is only used for sampled reads.
Test13 fixed storage writes but retained these invalid read views.

Source: https://docs.vulkan.org/refpages/latest/refpages/source/VkBufferViewCreateInfo.html
(range-00930 and format-08779). The API violations are confirmed; visual
causality of the absent text and old-background flashes still needs user testing.

## Test14 fix

Android native framebuffer reads AND writes now use StructuredBuffer<uint> /
RWStructuredBuffer<uint>, with matching storage-buffer descriptors. Input
8/16-bit pixels are extracted from packed words before the existing endian and
color conversions. GPU-to-RAM packing from test13 is retained. No texel-buffer
views or FORMATTED flag are used for Android native framebuffer memory.
This removes the texel-count limit and the narrow storage-format requirement;
Mali's storage-buffer range limit is 256 MiB, far above these native buffers.

The change covers full uploads, change comparison, previous-frame history,
color/depth writeback and MSAA depth. Format-matched mask/color intermediates,
GPU/CPU synchronization, resolution, localization, and persistent cache remain.
Desktop still selects the existing typed-buffer variants.

Five selected SPIR-V modules pass Vulkan 1.1 validation and contain no texel
buffer image types/capabilities; word array stride is 4 bytes. Release build and
permanent signature verified, all 225 assets identical to test13. No device
stress test or shader sweep was run. User should test the current dialogue and
transition into the cinematic bars; neither symptom is claimed fixed yet.

APK: Dora64-1.0.3-test14-Android-arm64.apk, versionCode 53.
SHA-256: cf0a57ed090c961e9b1ba7d59f372281c2f42a729b8bb33581223b829fd43a9d


## User validation: test14

Maintainer confirms dialogue text appeared and moving letterbox-bar glitches
disappeared. The common Android storage-buffer fix is accepted for these two
symptoms. No new FPS measurement or full playthrough was reported. RC1 bundles
the fix for the next complete device retest.
