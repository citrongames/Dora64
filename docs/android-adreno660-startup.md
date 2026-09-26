# Snapdragon 888 startup crash — investigation, 2026-09-26

Status: signed APK 1.0.3-test2 (41) works on Xiaomi Mi 11 Ultra; the maintainer reports 120 FPS. Captured settings confirm 2x rendering. In-app logs confirm automatic fallback and successful specialized raster shaders. Huawei P50 also starts on test2, but occasional sub-60 FPS drops at 1x remain under investigation. These are partial gameplay tests.

The sections below record the original investigation. See the implementation update at the end for the current approach.

## Reports and evidence

Maintainer reports Android 1.0.2 crashes after ROM selection on both Xiaomi Mi 11
Ultra (Snapdragon 888 5G) and Huawei P50 (Snapdragon 888 4G). Investigate Xiaomi
first. Huawei has not supplied a crash log; identical symptoms do not yet prove
an identical driver failure.

Xiaomi M2102K1G: Android 14/API 34, Adreno 660, Vulkan 1.1.128,
driverVersion 2149654528, Qualcomm shader compiler EV031.35.01.12.
Installed app: versionCode 39, versionName 1.0.2, arm64-v8a.
Matching libDora64.so Build ID: 355eb4ee0c6e9cef6932c5dcf93aea8a923378b1.

The application's native abort follows failed graphics pipeline creation:

```
AdrenoVK-0: Failed to link shaders.
vkCreateGraphicsPipelines: VkResult=-13
VS=282f5ffd967d8ed0 PS=bb608ac83c8a3b51
Attempted to bind a failed graphics pipeline
```

Those shader hashes identify RasterVSDynamic.hlsl.spv and RasterPSDynamic.hlsl.spv.
Symbolized stack reaches PlumeVulkanCommandList::setPipeline at
plume_vulkan.cpp:3169 from RT64's framebuffer renderer. It is an intentional
abort when binding a pipeline the driver failed to create, not evidence of
corrupted emulated memory or a CPU instruction fault.

The declared device features needed here are available, including dual-source
blend, descriptor indexing, non-uniform sampled image indexing, and scalar block
layout. Feature availability alone does not establish driver correctness.

## Controlled compiler reproduction

A small native Vulkan program creates the relevant layout, render pass and
graphics pipeline using the released shader pair. It reproduces VkResult=-13
without launching Dora64, using a ROM, binding the failed pipeline, recording
GPU commands, or drawing anything. The diagnostic layout models the bindings
used by these shaders; it is not an entire copy of RT64's renderer.

The baseline fragment shader rebuilt with the bundled DXC is byte-identical to
the release blob. Shader variants were generated outside the repository and
APK. SPIR-V validation passed for the initial and sampler-rewrite variants.
Compilation success in this probe is not a gameplay or image-quality test.

| Change from original shader | Pipeline result |
| --- | --- |
| Original pair, HDR color target | -13 |
| RGBA8 color target | -13 |
| Remove decal depth path | -13 |
| Disable both game texture sampling paths | Success |
| Remove NonUniformResourceIndex annotations | -13 |
| Disable TMEM decoder | -13 |
| Force native sampler NONE | Success |
| Disable replacement mipmap branch | Success |
| Alias linear samplers to reduce number referenced to 17 or 16 | -13 |
| Replace SampleGrad with SampleLevel at LOD 0 (diagnostic only) | Success |
| Replace sampler switch with if-chain | -13 |
| Use one linear sampler for every SampleGrad | -13 |
| Keep SampleGrad, but use fixed texture gTextures[0] | Success |
| Keep dynamic texture, but use constant gradient vectors | -13 |
| Inline non-uniform resource access into each SampleGrad | -13 |
| Add NonUniform to OpSampledImage results | -13 |
| Reduce shader array declaration to 16 or 128 (layout still 1024) | -13 |

The failing combination is gradient sampling plus dynamically indexed texture
resources in this ubershader on this particular driver. Neither SampleGrad
alone nor dynamic texture indexing alone necessarily fails. The proprietary
compiler's internal failure is not exposed, so do not claim a specific internal
compiler bug or that every Adreno 660 firmware behaves identically.

Control device supplied by the maintainer: Lenovo Legion Y700 (2025), TB321FU,
Android 16, Adreno 750, Vulkan 1.3.128, driverVersion 2150604840. The exact same
unmodified probe and release shader pair returned VK_SUCCESS there. This
supports a device/driver-specific compatibility failure, but does not isolate
whether GPU generation or firmware/compiler version is the determining factor.
It is not proof that all Vulkan 1.1 implementations fail or all 1.3 devices work.

## Source trace and practical scope

`lib/rt64/src/shaders/TextureSampler.hlsli`, sampleTexture(), native mipmap
branch (~277–329), contains nine SampleGrad calls with a dynamically indexed
gTextures resource. The gradient-preserving restructuring attempts above did
not resolve compilation.

`TextureMap::use()` in `render/rt64_texture_cache.cpp` initializes hasMipmaps to
false; it enables it only for replacement textures with more than one mip level.
Ordinary decoded N64 textures do not take this host-mipmap branch. This is
distinct from the N64 texture LOD emulation in computeLOD(). Nonetheless, the
entire dynamic shader is compiled before use, so an unused mipmap branch can
prevent starting the game.

`render/rt64_shader_library.cpp` enables anisotropy on the linear samplers and
sets mipLODBias=-0.25. Therefore changing SampleGrad to SampleLevel at constant
LOD 0 is NOT a suitable final fix. An explicit-LOD compatibility path must
calculate mip level from the original gradients, preserve addressing and
trilinear filtering, and account for the loss of gradient-based anisotropy.
Alternatively, reuse RT64's manual mip sampling, taking care that its native
nearest sampler path currently samples mip 0 and must not be reused blindly for
other mip levels.

Initial proposal (superseded by automatic error-triggered fallback below): a separate Android compatibility shader for
the affected GPU/driver, leaving the normal shader on successful devices and
desktop builds. Preserve mipmap level selection; document anisotropy tradeoffs
for replacement texture packs. Validate pipeline creation first, then provide
a signed test APK for the maintainer's Xiaomi test. Test Huawei only after
Xiaomi succeeds; collect Huawei's driver identity rather than assuming it.
Do not publish or update the public release from compilation alone.

The shader build's explicit include dependencies currently omit
TextureSampler.hlsli; add it when modifying this path so incremental builds
cannot silently retain the old shader.

## External research

- Microsoft [SampleGrad](https://learn.microsoft.com/en-us/windows/win32/direct3dhlsl/dx-graphics-hlsl-to-samplegrad)
  and [SampleLevel](https://learn.microsoft.com/en-us/windows/win32/direct3dhlsl/dx-graphics-hlsl-to-samplelevel)
  describe gradient-driven versus explicit-level texture sampling.
- Khronos [descriptor indexing sample](https://docs.vulkan.org/samples/latest/samples/extensions/descriptor_indexing/README.html)
  explains dynamically uniform/non-uniform access and the required decoration
  on the final resource operand. This motivated the decoration experiment;
  it did not resolve Xiaomi's failure.
- [Goemon64Recomp-Android](https://github.com/ogdanimal/Goemon64Recomp-Android#troubleshooting-details)
  documents some Adreno startup failures and suggests disabling framebuffer
  effects or an alternate driver. That report does not identify our failing
  SampleGrad branch and does not justify disabling Dora64's effects.
- [Unity's Adreno framebuffer-fetch issue](https://issuetracker.unity.com/issues/15752/texture-sampling-overlap-causes-graphical-errors-on-specific-graphics-apis-and-devices-when-using-framebuffer-fetch)
  concerns missing tile-local barriers in particular driver versions, not our
  pipeline-link failure. Do not conflate the two.

No exact upstream fix for this shader/driver combination was found in the
searched primary sources. Our conclusion comes from local logs, source and
controlled on-device compilation, not matching a generic error string online.

## Local reproduction assets

Workspace: C:/Users/black/.codex/.chatgpt-projects/g-p-6a9846510614819184ada62bf3909cb3

- xiaomi888-20260926-091859/: crash logs, native traces, feature queries, pipeline results.
- xiaomi_pipeline_probe.cpp and xiaomi-pipeline-probe: standalone compilation harness.
- xiaomi-original-vs.spv and xiaomi-original-ps.spv: release shader blobs.
- xiaomi-shader-probes/: diagnostic shader variants, not for shipping.
- xiaomi_texture_variants.py, xiaomi_compile_variants.py, xiaomi_cache_probe.py:
  variant generation. The texture script now contains the last set of experiments;
  previous generated variants and result files are retained.

No APK was installed or launched by the agent. No source fix, commit, push or
release update was made during this investigation.


## Implementation update: automatic fallback, Android 1.0.3-test1 (40)

The maintainer explicitly requested a general fallback instead of a GPU/driver
allowlist and asked to minimize filtering compromises. Implemented locally;
no commit, push or release replacement yet.

### Selection and lifetime

- Plume exposes graphics pipeline creation status. Vulkan UNKNOWN and
  INVALID_SHADER_NV are eligible for a shader retry; all other creation failures
  (including memory exhaustion and device loss) are fatal to this path.
- RT64 first builds the original eight ubershader pipelines. After joining all
  compilation workers, a retryable failure triggers exactly one attempt to
  rebuild the whole family with the compatibility fragment shader. Failed or
  partial retry sets are never accepted as valid.
- No device name, vendor ID, driver version, or persistent user setting selects
  the fallback. It is compiled into Android builds, including an MSAA variant.
- Initial Android setup waits for resolution before any display lists or
  specialized shader requests. Complete failure is returned through the runtime's
  startup error dialog, before trying to bind a failed pipeline.
- A separate firstPipelineReady condition prevents waiting threads from relying
  on successful pointer creation to wake up.
- When fallback is active, RasterShaderCache skips specialized shader requests
  and returns the compatibility ubershader instead of old optimized shaders.
- A new shader family (e.g. after a multisampling change) runs the same bounded
  selection again. There is no compilation attempt in the per-frame hot path.

### Filtering

`TextureSamplerFallback.hlsli` only replaces the native SampleGrad mipmap branch.
It computes principal axes of the texel-space footprint, derives explicit mip
LOD from the minor axis, and distributes up to 16 trilinear samples along the
major axis. At the 16-tap cap it widens the minor footprint to limit aliasing.
Magnification and isotropic footprints use one sample. The original samplers
preserve wrap/mirror/clamp and apply their -0.25 LOD bias exactly once.

This preserves mipmap selection and provides software anisotropic filtering,
rather than forcing mip 0 or disabling mipmaps/anisotropy. Its kernel is an
approximation, not a claim of bit-identical hardware filtering; mipmapped texture
packs may cost more than on the normal hardware path. Ordinary decoded N64
textures and the RDP sampling/LOD algorithms retain their existing code.

Reference for explicit LOD plus sampler bias and anisotropic sampling:
https://registry.khronos.org/vulkan/specs/latest/html/vkspec.html#textures-level-of-detail-operation

### Validation and artifact

- Android release build succeeded (RelWithDebInfo native code, release signing).
- The ordinary fragment shader is byte-identical to APK 1.0.2.
- Both compatibility SPIR-V variants pass spirv-val for Vulkan 1.1.
- The final non-MSAA compatibility blob matches the independently tested blob.
- All eight depth/coverage pipeline combinations compile on Xiaomi Adreno 660
  and Lenovo Adreno 750, with real anisotropic sampler settings. These probes
  submit no rendering commands; they do not validate the rendered image.
- Certificate SHA-256:
  083af9220eeb6ef4679d24a93b53cef76afc2e296464a0fd5410746f463b781f
- All 225 existing packaged assets, including localization, are unchanged.
- APK: D:/Games/dorarecomp/apk/Dora64-1.0.3-test1-Android-arm64.apk
- APK SHA-256: 7a6059670b8e8eb296e833a42783927755265eaae9e69a32c29f045d62009a87

The full in-app automatic transition and gameplay have NOT been tested by the
agent. Maintainer should first update Xiaomi in place, check ROM/startup, enter
a level, inspect textures/water/effects and performance. Then compare Lenovo;
Huawei remains pending until Xiaomi succeeds. Keep ADB connected to collect
Dora64Vulkan messages confirming which path the app actually selected.


## Maintainer's first test of 1.0.3-test1 on Xiaomi

Reported: startup succeeds, game works, no visible bugs, longer initial black
screen; approximately 30 FPS. This is an initial test, not a full playthrough.

ADB confirms versionCode 40/versionName 1.0.3-test1, no DEBUGGABLE flag.
Actual compile commands for game code, render context and raster shader code
include -O2 and -DNDEBUG (RelWithDebInfo); this is the signed Release APK.

The app log confirms all eight normal pipelines fail with -13, then automatic
fallback succeeds. Setup runs 10:02:43.276–10:02:47.700; the fallback attempt
itself runs 10:02:44.516–10:02:47.693 (about 3.18 seconds). No recurring pipeline
failure/retry appears in the captured session. The longer black screen is
consistent with this one-time startup compilation work.

Captured settings: 2x, Expand, Display frame-rate mode, no MSAA, ThreePoint,
draw distance/model LOD 5, Russian. There is no configured 30 FPS limit.
Native log records display-list processing intervals around 24–30 ms in later
samples (earlier level samples up to 40–43 ms); that scope can include GPU waits
and does not establish a CPU-only bottleneck. Cost of software anisotropy versus
generic ubershader execution remains unmeasured. Next useful split: compare only
Game render resolution 2x versus 1x in the same scene, without selecting the
Original preset button or changing other settings.

Evidence: after-test1-logcat.txt, after-test1-settings.json and
after-test1-stderr.log in the workspace's xiaomi888-20260926-091859 directory.


## Performance follow-up and 1.0.3-test2 (41)

Maintainer's same-scene comparison on test1: changing only resolution from 2x
to 1x raises FPS from about 30 to 60. ADB confirms 1x settings and a currently
active 120 Hz main display (this alone does not exclude an app-specific cap).
The response to resolution points to pixel-dependent rendering cost, not a
Debug build. It does not identify the individual shader bottleneck by itself.

Inspection found test1 disabled all specialized material shaders when fallback
was active. This was conservative crash protection but left the expensive
ubershader in use permanently. Bundled ru/en texture packs also set
forceNearestFiltering=true, so their runtime mip/aniso branch is disabled;
do not attribute the 30 FPS measurement to executing 16 anisotropic samples.

Test2 restores material specialization using four compatible SPIR-V templates
(smooth/flat, single-sample/MSAA). The anisotropic taps are explicitly unrolled
only in those templates because re-spirv cannot optimize loop constructs.
The dynamic compatibility ubershader is unchanged byte-for-byte from test1.
Selection completes before loading optimizer inputs or submitting materials.
If optimization or specialized pipeline creation fails, the material keeps the
working ubershader; failed results are cached to avoid endless retry/bind errors.

A related worker-start race was fixed: threadRunning is initialized to true
before creating the compilation thread. Previously the constructor set false
after starting the thread while the worker also set true, permitting premature
termination; the worker could also overwrite a destructor stop request.

Per user request, Android displays an English `Compiling shaders…` label with
an indeterminate spinner using native Android views, independent of Vulkan.
Native setup shows it before compilation; the first output draw callback hides
it, as do setup failure and render-context teardown. UI calls run on the Android
UI thread. Visibility and animation still require maintainer device testing.

Validation: optimized Release APK assembled, same permanent signature, all
225 existing assets unchanged. The actual re-spirv optimizer successfully
processed all four new templates with its reference two-texture material:
326–332 KB templates became about 45 KB valid SPIR-V, without gradient sampling
or loop constructs. The smooth specialized fragment shader passed all eight
pipeline-state combinations on Xiaomi in the compiler-only probe. This is not
an exhaustive material test or a measurement of FPS improvement. The baseline
and compatibility dynamic shader blobs remain unchanged from test1.

APK: D:/Games/dorarecomp/apk/Dora64-1.0.3-test2-Android-arm64.apk
SHA-256: 3ebbbe6907c09da0c11bba415f011ac601d6dc216f44011d15e23a8368f48774

User test next: update Xiaomi in place, check the English startup message,
return only Game render resolution to 2x, enter the same scene and move the
camera/wait briefly for material compilation. Report FPS after it settles and
any graphics artifacts. Keep ADB connected; `Specialized raster shaders ready
(texture fallback=1)` confirms the app's specialized fallback path. No release
publication, commit or push yet. Huawei remains pending after Xiaomi validation.


## Xiaomi test2 gameplay confirmation — 2026-09-26

The maintainer reports that test2 works and performance rose to 120 FPS.
The captured settings show Original2x, Expand, Display frame-rate mode,
no MSAA, ThreePoint filtering, Russian localization and model LOD distance 5.
This is a partial gameplay test, not a full playthrough or a guarantee of
120 FPS in every scene.

Actual application logs (PID 31821) confirm the complete path:
- 10:18:43.418: original raster shader rejected; explicit-LOD fallback selected.
- 10:18:46.459: fallback ready, mipmaps and software anisotropy enabled.
- 10:18:46.579: Specialized raster shaders ready (texture fallback=1).

Evidence: after-test2-logcat.txt and after-test2-settings.json in the local
xiaomi888-20260926-091859 evidence directory. The maintainer is now testing
Huawei P50; its result and driver behavior remain unconfirmed.


## Huawei test2 follow-up — 2026-09-26

The maintainer confirms successful launch on Huawei P50 with test2. Performance
sometimes drops below 60 FPS even at 1x. Huawei driver details, specialization
logs and comparable scene measurements have not yet been captured; do not
assume the Xiaomi shader/performance diagnosis applies identically. Startup
compatibility is user-confirmed; performance investigation remains open.


## Huawei USB diagnostics — 2026-09-26

Huawei ABR-LX9 is now connected through USB after driver setup. Android reports
12/API 31, installed APK 1.0.3-test2 (41). Vulkan: Adreno 660, API 1.1.128,
driverVersion 2149654528; driver build 272cf717f5/Iee40f504b5 dated 2021-10-05,
compiler EV031.35.01.10. Xiaomi has the same numeric driverVersion but a different
compiler (EV031.35.01.12); numeric driverVersion alone is insufficient to match.

Captured settings: 1x render resolution, Frame rate Original, Expand, no MSAA,
ThreePoint, Japanese, draw/LOD distances 5. Xiaomi's successful 120 FPS test used
2x, Display and Russian. These are not matched settings or a matched scene.
Huawei touch scale is saved at 1.6; test3 containing the size fix is not yet
installed on this device at capture time.

The current game process runs. Native stderr records eight initial pipeline
failures (-13), then normal gameplay output, consistent with successful fallback.
The expected Dora64Vulkan informational messages are absent from captured logcat,
so specialization on Huawei is not yet directly verified. persist.log.tag reads
M; no system logging property was modified.

In-game SurfaceFlinger snapshots showed 60 Hz and subsequently a latency history
with a 90 Hz period (11,111,111 ns), indicating the display mode changed during
observation. The latter 127 valid presentation timestamps span about 2.45 seconds:
126 intervals average 19.43 ms (~51.5 presentations/sec), maximum 33.44 ms, with
11.1/22.3/33.4 ms steps. This brief sample supports uneven presentation, not an
established CPU/GPU bottleneck. Current Thermal Status was 0 with GPU around
40 C; this does not exclude vendor power management. Existing gfx parse timing
includes waiting and must not be treated as pure CPU time.

Evidence saved locally under huawei888-20260926-104210. Next controlled user test:
keep 1x and the same scene, change only Frame rate Original to Display, then
compare settled FPS. No gameplay was started or controlled by the agent.


## Huawei Display comparison and CPU scheduling — 2026-09-26

The maintainer changed only Frame rate to Display and reports the same sub-60
drops. Captured settings confirm 1x/Display. A 126-interval presentation sample
averaged 22.17 ms (~45.1/sec); this is a short observation, not an exact matched
benchmark against the earlier capture. Thermal status remains 0, current GPU
temperature about 40.6 C. Two reads of KGSL gpubusy showed ~34% and ~36%; these
do not establish a GPU bottleneck or rule out short GPU stalls.

Temporarily setting only log.tag.Dora64Vulkan=I and asking the maintainer to
restart did not recover that tag's messages, even in all logcat buffers.
Previous value was empty and is restored after this attempt. No global logging
property was changed. The restarted process records exactly eight initial
pipeline failures and normal gameplay; no subsequent pipeline or optimizer
errors appear in stderr. Four RT64 Shader workers are sleeping after consuming
0.16–0.26 seconds CPU each, consistent with completed work, but this alone is
not direct confirmation of successful specialization.

A 3.000-second atrace sched/freq/idle capture (117576 events, no reported buffer
loss) provides a stronger lead:
- Thread-4: 1301.9 ms running, only 0.2 ms on cores 4–7.
- RT64 Workload: 941.3 ms running, only 4.7 ms on cores 4–7.
- SP Task Thread: 233.4 ms running, only 1.0 ms on cores 4–7.
- Thread-5: 202.6 ms running, all on cores 0–3.

Kernel cpu_capacity reports 325 for CPU0, 828 for CPU4, 1024 for CPU7. Thus the
heavy threads spend over 99% of their CPU time on the low-capacity cluster.
Observed frequency changes for CPU0–3 range 1.0944–1.6128 GHz; CPU4–6 show
710.4 MHz changes. The heavy threads' allowed masks cover CPUs 0–7; /top-app was
also observed, so this is not evidence of hard affinity restriction in our code.
Global low_power=0 and sys.super_power_save=false do not imply the vendor's
ordinary performance policy favors fast cores.

Next user comparison: temporarily enable Huawei Settings > Battery > Performance
mode, keeping the same scene and 1x/Display, then compare FPS and scheduling.
This is a hypothesis test, not a confirmed root cause or a recommendation to
force fast-core affinity in the game. No new APK or game-code change in this step.
Source for the user-facing setting: https://consumer.huawei.com/en/support/content/en-us00782178/
Evidence: huawei888-20260926-104210/scheduler-trace.txt, scheduler-summary.txt,
threads-sampled.txt, latency-display.txt, settings-display.json and related logs.


## Huawei performance mode and test4 CPU placement probe

The maintainer reports drops even after adding the game to the gaming section
and enabling the mode. ADB confirms persist.sys.performance=true,
power_saving_on=0 and low_power=0. A second 3.001-second scheduling trace still
places all observed running time of Thread-4 (1291.9 ms), RT64 Workload
(923.4 ms), SP Task Thread (229.0 ms) and Thread-5 (197.3 ms) on CPUs 0–3.
The firmware's performance_hint service reports HAL Support: false and
HintSessionPreferredRate: -1, so standard ADPF hint sessions are unavailable.

Test4 is a controlled diagnostic, not a final scheduling policy. Temporary
src/android_cpu_probe.hpp reads debug.dora64.fast_cpu once per second on the
SDL event thread. Only value 1 enables the probe. It discovers CPU capacities
from sysfs and selects available CPUs above the smallest capacity (4–7 on
Huawei), intersected with each target thread's original mask. Unknown/uniform
topology or failed syscalls leave scheduling unchanged. It targets the output
draw thread, display-list parsing thread and RT64 Workload, identified by actual
callbacks/name; it does not pin the entire game or shader/audio workers.
Disabling the property restores saved masks while the process is alive; process
exit also ends the placement. It never sets clock rates or disables thermal
controls. This temporary probe must be removed or replaced by a justified
production policy before publishing; do not ship it silently as the fix.

The first successful specialized shader is also logged to native-stderr.log,
since Huawei filters the Android log tag even after a targeted logging override.
The prior logging override was restored to empty. Graphics, assets and touch
size correction from test3 are unchanged. Release build and signature/payload
checks passed; device testing remains with the maintainer.

APK: Dora64-1.0.3-test4-Android-arm64.apk (versionCode 43)
SHA-256: 251b1c22526bf899a86b2d8a3c8c86d10cbd626f1fd4de9dc1cb627cc84a8f47

Next: install test4 on Huawei, same scene at 1x/Display. Enable the temporary
property only on this device, verify the selected thread IDs in stderr and
compare performance/trace; toggle it off for a same-APK comparison. Do not
attribute causality until this experiment is measured.


## Huawei test4 result and stopping point — 2026-09-26

The maintainer reports FPS increased on test4 but still does not reach the
maximum, and elects to stop further Huawei performance tuning for now. No exact
FPS range was reported and no same-APK probe-off comparison was completed.
The result supports CPU placement contributing to the earlier limitation; it
does not establish the remaining bottleneck or a hardware performance ceiling.

ADB confirms installed 1.0.3-test4 (43). Final native stderr explicitly confirms
successful specialized raster shaders (texture fallback=1) and three successful
CPU placements onto four performance CPUs (capacity range 325..1024). Thus the
shader specialization path on Huawei is now directly confirmed. Evidence:
huawei888-20260926-104210/test4-final-stderr.log and test4-final-settings.json.

No further build, install, gameplay test, commit, push or publication is done
for this report. The temporary debug.dora64.fast_cpu property remains 1 on
Huawei for the user's current test4; the agent has not silently removed the
reported improvement. This nonpersistent property is only consumed by our
temporary test4 probe. Before publishing, remove the probe or explicitly design
and validate a production scheduling policy; the current diagnostic must not
be mistaken for a finalized general Android fix. Huawei performance work is
deferred at the maintainer's request.
