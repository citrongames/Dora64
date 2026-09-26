# Redmi Note 7: black game image on Adreno 512

## Captured baseline — 2026-09-26

User reports Xiaomi Redmi Note 7 / Qualcomm SDM660 Snapdragon 660 starts and
runs, but most of the scene is black and only a few textures are visible.
ADB confirms Redmi_Note_7 / lavender, LineageOS Android 13 (API 33), build
lineage_lavender-userdebug 13 TQ3A.230901.001 eng.achill.20231125.224728 dev-keys.
Do not generalize this firmware/driver result to every Snapdragon 660 device.
Installed app: 1.0.3-test11, versionCode 50. Latest settings: Original (1x)
resolution, Display frame rate, Expand, no MSAA, ThreePoint, Russian.

Native stderr reports specialized shaders ready with texture fallback=0.
No pipeline-creation error or wait timeout was found in the captured stderr.
A screenshot confirms black game geometry with a few colored fragments and
correctly visible touch overlay. No install, game launch, input injection,
setting change, or APK rebuild was performed during this investigation.

Workspace evidence: redmi660-20260926/{device.txt,settings.json,native-stderr.log,
android-trace.log,graphics-logcat.txt,vk.json,features.txt,screen.png}.
The native capability probe only creates a Vulkan instance and queries the GPU;
it does not draw or use game/ROM data. vkjson independently supplies extension
and limit information.

## Confirmed compatibility violations

GPU: Adreno (TM) 512, Vulkan 1.1.128, driverVersion 2149490688.

| Capability / limit | Reported value |
| --- | --- |
| dualSrcBlend / independentBlend | 1 / 1 |
| shaderSampledImageArrayDynamicIndexing | 1 |
| shaderSampledImageArrayNonUniformIndexing | 0 |
| descriptorBindingPartiallyBound / descriptorBindingVariableDescriptorCount / runtimeDescriptorArray | 0 / 0 / 0 |
| maxPerStageDescriptorSampledImages | 128 |
| maxDescriptorSetSampledImages | 768 |
| maxPerStageDescriptorSamplers | 16 |
| maxDescriptorSetSamplers | 96 |
| maxPerStageResources | 158 |
| maxPerStageDescriptorStorageBuffers / maxDescriptorSetStorageBuffers | 24 / 24 |

VK_EXT_descriptor_indexing and VK_EXT_scalar_block_layout are absent from the
reported extension list. Scalar-layout requirements of individual shaders need
their own audit; absence alone does not prove every existing buffer layout wrong.

Current Android code uses fixed arrays, not runtime-sized descriptor arrays:
RT64_TEXTURE_CACHE_SIZE=1024 in CMake, gTextures[1024] and gTMEM[1024] in
FbRendererCommon.hlsli. FramebufferRendererDescriptorTextureSet declares 1024
sampled-image descriptors and uses builder.end() on Android, unlike the desktop
boundless path. Therefore missing runtimeDescriptorArray is NOT by itself the
diagnosis. The actual confirmed mismatches are:

1. A single 1024-entry sampled-image set already exceeds this driver's stage
   and set limits; the raster layout adds two such sets. Plume marks bindings
   visible to all shader stages.
2. TextureSampler.hlsli explicitly uses NonUniformResourceIndex for RGBA/TMEM
   access, but the sampled-image nonuniform feature is unsupported.
3. FramebufferRendererDescriptorCommonSet declares 18 immutable samplers
   (bindings 7 through 24), exceeding the per-stage sampler limit of 16.

These are invalid uses of advertised capabilities even if driver calls return
success. They strongly account for the unusable texture path, but no compliant
replacement has yet been run to isolate the contribution of each mismatch.
This is distinct from Pura's missing dual-source blending and from Adreno 660's
recoverable SampleGrad pipeline compile error. Both existing fallback triggers
are false here; forcing either is not a complete fix.

## Candidate general fallback, not implemented yet

Select by capabilities and limits, not device name. Bind a small, fully populated
set of textures for each draw instead of the global cache. Remap the draw's RDP
tile indices to local slots, covering both RGBA and TMEM, texture replacements,
tile/framebuffer copies, and all required LOD tiles (not merely the common pair).
Use statically addressed slots or a proven dynamically uniform selection, with
no unsupported NonUniform decoration. Keep descriptor contents alive and
immutable while GPU work references them.

Use a smaller raster-only descriptor layout: avoid unused raytracing resources
and bind only the needed sampler variants, within all per-stage/set limits.
Audit buffer layout requirements against Vulkan 1.1 relaxed layout support.
Generate matching specialized and uber shader variants so the compatibility
path retains material specialization. Preserve the existing path on capable
GPUs. Expect some extra CPU binding cost; neither acceptable FPS nor visual
correctness on this device has yet been demonstrated.

Do not merely lower the global cache limit to 128, remove NonUniformResourceIndex,
force the Huawei MRT path, or replace the driver as an assumed fix. Those actions
do not address all confirmed violations and can silently break other scenes.

Specification references:
- https://docs.vulkan.org/refpages/latest/refpages/source/VkPhysicalDeviceDescriptorIndexingFeatures.html
- https://docs.vulkan.org/spec/latest/chapters/limits.html
- https://docs.vulkan.org/spec/latest/chapters/descriptorsets.html


## Implemented candidate: 1.0.3-test12 (51)

The Android Vulkan device selects localTextureDescriptors when sampled-image
nonuniform indexing is unavailable, either sampled-image limit is below 2050,
or the per-stage sampler limit is below 18. The normal descriptor/shader path
remains selected otherwise. This is a capability decision, not a device list.

The raster path binds separate eight-slot RGBA and TMEM sets per draw, mapping
the draw's RDP tile range rather than truncating the global texture cache. All
eight LOD tiles remain available. Framebuffer/tile copies take precedence over
reused cache slots; replacements follow the existing texture replacement map.
Each set remains unchanged during submission and is reused only after the
workload worker's existing GPU wait. Unused slots contain initialized zero
textures of the appropriate dimensionality and numeric type.

The reduced common set has five storage buffers, a constant buffer and nine
linear samplers; it omits nearest samplers and unused raytracing bindings.
Nearest-at-texel-center sampling uses integer wrap/mirror/clamp plus Load.
Mipmapped native textures retain SampleGrad and hardware filtering with constant
resource slots. No downgrade to point filtering or disabled localization.
Shader tile indices are local, without modifying shared GPU tile metadata.

Twelve local pixel shader variants cover uber/material specialization, flat
color, MSAA and separate coverage. All pass spirv-val for Vulkan 1.1, contain no
NonUniform or RuntimeDescriptorArray capability, and require no sampled-image
array dynamic indexing. Eight specialized templates also passed re-spirv and
post-optimization validation. This validates binaries, not gameplay output.

Compiler-cost investigation: an initial local shader using software anisotropy
and a subsequent native-gradient variant remained busy compiling their first
pipeline in standalone probes. Those probes were explicitly stopped; neither
was established to fail or hang. The final version factors TMEM byte-source
selection inside the shared decoder instead of duplicating all texture-format
decoding for eight slots. Native nearest addressing is also computed before
slot selection. Final uber PS size is 149552 bytes, down from 250072 bytes in
the intermediate native-gradient version. The user confirms this phone already
had slow startup and is willing to wait with the compilation message visible.

Build: optimized non-debuggable Release, permanent signature verified, all 225
asset entries byte-identical to test11 (including localization).
APK: Dora64-1.0.3-test12-Android-arm64.apk
SHA-256: 51da5f40e9d98bf8806d74cd35b0fc104ceb21e4fa3ada239325b5317ea89ba5
User gameplay validation is pending. No game installation or launch, commit,
push, or public release update by the agent. Standalone driver probe results
are recorded separately below after completion.


Standalone driver result for the final factored shader: state 0 pipeline
creation returned VK_SUCCESS (0), color format 91 (R16G16B16A16_UNORM),
using the actual local uber PS and dynamic VS, nine samplers and two fixed
eight-entry sampled-image sets, without descriptor-indexing/scalar-layout
extensions enabled. Evidence: redmi660-20260926/local-factored-probe.txt.
The process was stopped explicitly while compiling the next state, after
2m45s total elapsed, to avoid an unnecessary long eight-state sweep before
the maintainer's game test. Only state 0 is confirmed by this native probe;
no visual rendering test was performed. The diagnostic process was stopped
before handing off the APK, so it cannot affect the user's FPS measurement.


## User result: test12 and CPU text investigation

Maintainer confirms restored scene textures on Redmi Note 7, around 20 FPS,
but CPU-rendered dialogue text has severe artifacts. ADB screenshot
`redmi660-20260926/text-artifacts.png` shows intact scene/portrait and only
sparse white vertical fragments of text (18.6 FPS overlay). No inference about
CPU versus GPU bottleneck follows from the FPS alone.

Maintainer also reports Huawei P40 Pro / Kirin 990 5G previously crashed, but
test12 starts and renders the game; CPU text is missing there too. No ADB driver
capability capture or full playthrough on P40 has been performed.

### Confirmed framebuffer format violations

Redmi's captured Vulkan format properties report no STORAGE_IMAGE support for
R8_UINT and no STORAGE_TEXEL_BUFFER support for R8_UINT or R16_UINT. The native
framebuffer path nevertheless used R8_UINT for its CPU-change mask and narrow
formatted buffers for GPU-to-RAM writes. SPIR-V additionally declares R32ui
storage for the mask/buffer and Rgba32f for the CPU-change color image, while
the resources were R8_UINT/R16_UINT and RGBA8/RGBA16 UNORM. Storage formats must
match exactly; successful driver object creation does not make this legal.

See https://docs.vulkan.org/guide/latest/storage_image_and_texel_buffers.html

### Test13 (52): format-matched Android framebuffer path

Android uses R32_UINT change masks and RGBA32_FLOAT change-color intermediates,
matching existing read shaders; these temporary images retain native resolution.
GPU-to-native color/depth shaders pack 8/16-bit pixels into R32_UINT stores.
Each invocation owns one word (no cross-invocation subword races); partial
boundary words preserve bytes outside the updated rows. Pixel conversions,
dither, byte ordering, RAM layout and logical copy sizes are retained. Buffer
allocations are rounded to cover the last word. Existing typed sampled-buffer
reads can still read the packed bytes as R8/R16. MSAA depth has its own variant.
This path is selected for all Android Vulkan devices to fix the format mismatch
even on drivers that tolerated it. Desktop selection is unchanged.

Five relevant SPIR-V modules pass Vulkan 1.1 validation, require neither extended
storage-image formats nor formatless writes, and expose the matching R32ui /
Rgba32f stores. No device stress/long compilation sweep or game launch was run.
Visual correction of the text on Redmi/P40 remains for maintainer verification.

### Persistent Vulkan pipeline cache

Android now supplies a VkPipelineCache to compute/graphics pipeline creation.
It loads from private app storage, keyed by vendor/device/driver/cache UUID.
A bounded, versioned file envelope checks length/checksum and the Vulkan header
before submitting cached bytes to the driver; rejection retries an empty cache.
Saving uses a temporary file and atomic rename. Cache use is non-fatal.

Startup uber pipelines are saved immediately after their completion, before
gameplay. Newly compiled pipelines checkpoint at most every 30 seconds on a
compiler thread; orderly device teardown flushes remaining entries. Abrupt
termination can still lose the most recent incremental entries, but not the
completed startup checkpoint. No disk work is added to the per-frame draw path.
Concurrent compiler calls use Vulkan's internally synchronized cache mode.
Cache effectiveness/load-save behavior on actual user launches is still pending.
Driver updates/cache clearing can require recompilation; warm loading is not a
promise that every pipeline creation call becomes instantaneous.

Local descriptors now skip writes only for unchanged permanently-owned dummy
slots; live texture bindings are always refreshed to avoid pointer-reuse bugs.
This removes unnecessary CPU work without altering GPU ordering or sampling.
Performance benefit has not yet been measured.

Test13 is optimized, non-debuggable Release, signed with the permanent key;
all 225 asset entries match test12, including localization. No commit/push or
public release update. APK SHA-256: 495904eda38024e273729442053c7b705d749bd079dc5cc7d38e9ba9071a04d6


## Follow-up: test13 user result and test14

Maintainer confirms test13 restores Redmi dialogue text and substantially reduces
startup compilation (warm launch almost instantaneous). Huawei P40 Pro still
has absent text. Investigation found remaining oversized/unsupported native
texel-buffer read views; test14 replaces Android native read/write descriptors
with storage buffers. See [Mali-G76 investigation](android-mali-g76-cpu-text.md).
