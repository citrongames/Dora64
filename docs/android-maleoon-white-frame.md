# Huawei Pura 70 Ultra: white game image

## Status and evidence — 2026-09-26

The maintainer reports startup without a crash and a working port menu, but a
nearly white game scene with only dark fragments/shadows visible. Screenshots:
Screenshot_20260926_092833_com.n64recomp.dora64.jpg and
Screenshot_20260926_092842_com.n64recomp.dora64.jpg. Do not label this as proven
missing texture data solely from the screenshots.

USB model HBP-LX9 (HBP-L29), user-reported Kirin 9010. Android compatibility
reports 12/API 31. Installed APK is 1.0.3-test3 (42), not the P50 CPU probe.
Settings: 2x, Expand, Display, no MSAA, ThreePoint, Russian, draw/LOD distances 5.
GPU: Maleoon 910, Vulkan 1.2.231, vendor 0x19E5, driverVersion 386416813,
driverName Maleoon 910, driverInfo B236, conformance 1.2.2.3.

Native stderr contains ordinary game-frame output and no pipeline creation
failure in the captured run. Absence of an error return does not make unsupported
pipeline state valid. Expected Android log tags were empty in this capture.

## Confirmed unsupported rendering path

Both Android cmd gpu vkjson and a separate native capability-query program
report dualSrcBlend=0. Limits report maxFragmentDualSrcAttachments=0.
The independent query creates only a Vulkan instance and queries properties;
it does not draw, execute game code or use a ROM.

In contrast, RT64's RasterShader::createPipeline unconditionally uses
SRC1_ALPHA and INV_SRC1_ALPHA when alphaBlend is enabled. RasterPS.hlsl exposes
two outputs at location 0 with indices 0 and 1. RasterShaderUber always builds
the alpha-blending family. Plume enables the supported VkPhysicalDeviceFeatures
returned by the driver, so dualSrcBlend remains false; there is no exposed
capability/check or alternate path for this case. This is a confirmed Vulkan
valid-usage violation (00608/00609), not just a guessed vendor/compiler bug.
The white-image causal diagnosis still needs a correctly rendered replacement
path test; do not claim this is the only possible defect on Maleoon.

Sources in the checkout:
- lib/rt64/src/render/rt64_raster_shader.cpp: RasterShader::createPipeline.
- lib/rt64/src/shaders/RasterPS.hlsl: resultColor/resultAlpha and PSMain outputs.
- lib/rt64/src/contrib/plume/plume_vulkan.cpp: feature query/device creation.
- Plume's bundled Vulkan validusage.json independently lists the prohibition.

Primary specification:
https://docs.vulkan.org/refpages/latest/refpages/source/VkPipelineColorBlendAttachmentState.html

Descriptor indexing required for existing texture access is reported supported:
partially bound, variable count, runtime arrays and nonuniform sampled-image,
uniform-texel-buffer and storage-buffer indexing all report 1. This rules out
missing advertised descriptor features, not every possible driver issue.

The device supports independentBlend=1 and maxColorAttachments=8, offering a
candidate way to preserve both quantities via separate render targets. It does
not advertise attachment-order/interlock/local-read extensions in the captured
extension list. textureCompressionBC=0 is also recorded, but ordinary decoded
N64 textures do not require BC compression; it is not established as the cause.

## Why a simple SRC_ALPHA substitution is insufficient

RT64 stores N64 pixel coverage in resultColor.a and the actual RGB blend factor
in resultAlpha.a. Replacing SRC1_ALPHA with SRC_ALPHA alone would use the tiny
coverage encoding as opacity (range 7/65535 in HDR or 7/255 in SDR), producing
incorrect colors. Replacing the stored coverage with opacity would corrupt
coverage-dependent rendering/readback. Both the pipeline and shader/storage
path must change coherently.

Candidate correct fallback: color/opacity and coverage in separate render
targets using independent blending, then merge coverage back for existing
framebuffer consumers. It must account for clears, CVG_DST_SAVE/FULL/CLAMP/WRAP,
depth/decals, post-blend dithering, MSAA/resolve, texture feedback, target reuse
and transitions. Support detection must be capability-based and preserve the
current path on GPUs that support dual-source blending. This is a renderer
compatibility feature, not an Adreno shader compiler retry or CPU scheduling fix.

Internet cross-check also found a Dawn change disabling shader-f16 on Maleoon:
https://dawn.googlesource.com/dawn.git/+/d9b8cc4521a36bea1d6eae30a0ada84919dbac39
Its documented failure is private/function f16 array robustness. The examined
RT64 raster path uses float32 and this finding is not evidence of our white
image cause; do not apply an unrelated float16 workaround.

Local evidence directory: pura70-20260926 (vulkan.json, feature-query.txt,
native-stderr.log, settings.json, vulkan-logcat.txt). Temporary device file:
/data/local/tmp/dora64-vulkan-features. No APK installed or game started by the
agent, no Pura shader/renderer mutation, build, commit, push or release made in
this investigation. A correct dual-source-free implementation and device
validation remain outstanding.


## Capability-based MRT fallback: test5 implementation (2026-09-26)

Implemented and built; visual gameplay confirmation is pending with the
maintainer. Selection uses the device's actual dualSrcBlend feature, never
vendor/device names. Plume now exposes dualSourceBlend, independentBlend and
maxColorAttachments. Vulkan obtains these from queried device features/limits;
other backends retain their existing dual-source behavior. If dual-source is
absent, independent blending and at least two color attachments are required;
otherwise setup reports failure rather than submitting invalid pipelines.

Raster shaders write RGB/opacity to attachment 0 and N64 coverage to attachment
1. Attachment 0 writes RGB only, using SRC_ALPHA/ONE_MINUS_SRC_ALPHA when needed.
Attachment 1 writes alpha only, preserving original copy/add coverage behavior,
format precision and sample count. At each raster scene start, a transfer copy
seeds coverage from primary alpha. FillRect clears both attachments. Post-blend
dithering disables attachment 1 writes. After the scene, a fullscreen alpha-only
pass merges coverage back into primary, before existing resolves/readback/texture
feedback. MSAA uses per-sample loads rather than averaging coverage. Auxiliary
resources follow target resize/release and framebuffer revision lifetimes.

All dynamic/specialized, smooth/flat, MS/non-MS templates have MRT variants,
including combinations with the earlier SampleGrad compatibility fallback.
Original dual-source devices allocate no auxiliary target and perform none of
the extra copy/merge/framebuffer-switch operations. The fallback has a real
memory/bandwidth cost (one same-format target plus copy and merge per raster
scene); device performance remains to be measured.

Related backend correction: Vulkan pipeline viewport/scissor count was
incorrectly tied to color attachment count. It is now one, matching RT64's
viewport usage, including MRT. No multiViewport feature is needed.

Validation completed:
- Optimized release APK build succeeded (RelWithDebInfo, non-debuggable).
- Fourteen new SPIR-V modules passed spirv-val, with no Index 1 exports.
- All eight MRT specialized templates passed the actual re-spirv optimizer and
  subsequent validation for the existing representative material constants.
- Normal and Adreno fallback dynamic shader binaries are byte-identical to the
  previous working versions.
- A separate compile-only probe on Pura (3UN0224531004450) created all eight
  depth/coverage MRT pipeline states with VK_SUCCESS, R16G16B16A16_UNORM.
  It made no draw calls and neither installed nor launched the game.
- Signing certificate and all 225 existing asset entries verified unchanged.
- Root, RT64 and Plume diff whitespace checks passed.

APK: Dora64-1.0.3-test5-Android-arm64.apk, versionCode 44
SHA-256: f14a40e2bea6143933f142a5e3f5f318425418dedc3ec6a637521e707c8c8402

This test retains test3 touch sizing and the test4 temporary CPU placement probe;
the latter defaults off and has not been enabled on Pura. Its production-policy
decision/removal is still required before publishing a release. No commits,
pushes or release changes made for test5.

Next user test: Pura, same scene/settings at 2x, inspect geometry/textures,
dialogue, water/smoke/transparency and FPS. Inspect native-stderr.log for
"using separate N64 coverage attachment" after the user starts the APK.
Do not mark Maleoon gameplay compatible until the maintainer confirms rendering.


## Test5 device result and test6 attachment synchronization

Maintainer confirms test5 restores the game image and reports FPS above 100,
but intermittent striped models/textures and garbage in upper/lower black bars
remain. Current ADB screenshot captured at the request confirms image-colored
blocks in those bars; its instantaneous counter is 48.3, so the report is not
evidence of sustained >100 FPS. No performance policy changes in test6.

Evidence: workspace pura70-20260926/test5-current.png, test5-stderr.log and
test5-settings.json. Native stderr confirms the separate coverage attachment
and successful specialized shaders, texture fallback=0. Settings are 2x,
Expand/Display, MSAA None, ThreePoint, Russian. No game launch/install by agent.

Source audit identifies an actual missing memory dependency between render pass
instances. Both Plume render pass factories use LOAD/STORE but no explicit
external dependency. A barrier for an unrelated resource ends the current pass
without making the preserved attachments available to their next LOAD. Examples:
- RenderTarget::endSeparateCoverage barriers the auxiliary texture but not
  primary RGB, then reloads primary into an alpha-only merge pass.
- PresentQueue clears the swapchain, then barriers the game input texture,
  breaking the clear pass before VI drawing reloads the same swapchain.
- Raster depth-read/write transitions similarly preserve color attachments.

Test6 adds one common VK_SUBPASS_EXTERNAL -> 0 dependency to both compatible
pipeline and framebuffer render pass factories. Source/destination stages are
COLOR_ATTACHMENT_OUTPUT plus EARLY/LATE_FRAGMENT_TESTS; prior attachment writes
become visible to subsequent attachment reads/writes. BY_REGION retains local
framebuffer ordering. Explicit image barriers still handle sampling, transfer,
layouts and cross-queue synchronization. This is a general Vulkan contract fix,
not a device-name workaround, forced idle, quality reduction or shader change.

Reference: https://docs.vulkan.org/guide/latest/synchronization_examples.html
and https://docs.vulkan.org/spec/latest/chapters/renderpass.html

Optimized release build, signature, all 225 asset entries and whitespace
checks passed. Visual causality remains to be confirmed by the maintainer;
missing dependencies do not prove every Maleoon artifact shares this cause.

APK: Dora64-1.0.3-test6-Android-arm64.apk, versionCode 45
SHA-256: 6b4d35582461b2ec029e4e1d3937ebcf1c829b6282b7a570ba298d2f9201b957

Next: same Pura scene at 2x, inspect black bars and move camera around models,
then check transparency. Keep settings/data so the comparison isolates sync.
No commit/push/release update. Earlier test4 CPU probe remains off on Pura and
still needs a production decision before publishing.


## Test6 result: black bars fixed, remaining 2D lines and a hang

Maintainer confirms black bars now render correctly. Some 2D elements still
show pixel-like lines. Game then became unresponsive at the file-selection
screen, with music continuing; neither A/stick nor Port menu responded.
Do not call the missing-dependency patch a complete Maleoon compatibility fix.

Evidence captured while the app was still in this state (pid 28880):
workspace pura70-20260926/test6-hang.png, test6-hang-stderr.log,
test6-hang-trace.log, test6-hang-logcat.txt. Screenshot shows the translated
file-selection text, absent background/menu elements and FPS 28.3. It does not
capture the reported 2D striping sufficiently to identify its cause.
Native screen-update logging continues; audio/SP work has CPU activity while
the workload/presentation threads have negligible activity. This is consistent
with waiting but not proof of a GPU hang versus a CPU queue deadlock. Native
backtraces via debuggerd were denied (root required); /proc wchan returns masked
zeros. No root attempt, app launch or APK installation by the agent.

Test7 is diagnostic, not a claimed rendering fix. Android GPU fence waits use
5-second timed waits and report thread/queue/fence on the first timeout and
every 30 seconds thereafter. A timeout never resets a fence, frees resources,
skips rendering or permits work to proceed. CPU active frame waits retain the
same predicates and record present/workload IDs or interpolation counters on
the same timeout schedule. Idle cursor waits are unchanged. Successful resume
after a sustained wait is also logged. This separates GPU completion waits
from workload/present/interpolation dependencies without an optimization change.
Non-Android CPU waits and GPU fence behavior remain unchanged. Diagnostic code
is temporary and needs review/removal before release along with the CPU probe.

Release build remains optimized (-O2/NDEBUG), non-debuggable. Build, signing,
unchanged assets and diff whitespace checks passed. No new GPU shaders or
rendering behavior changes were made relative to test6.

APK: Dora64-1.0.3-test7-Android-arm64.apk, versionCode 46
SHA-256: 542126c9ada1fc4d441a6b6a5ae72e5d0b47e9df64bd3c3b9414309892c03ce5

Next user action: install test7, reproduce the same file-selection transition,
leave the hang running for at least 10 seconds and report it while connected to
ADB. Read the new wait diagnostics before choosing a corrective change. If
2D lines appear first, capture them via ADB. No commit/push/release update.


## Test7 follow-up: stale sprite rows and recurring letterbox corruption

Maintainer describes some rows of moving 2D sprites staying at the previous
position, like an afterimage. Reproduces with Frame rate Original and original
render resolution (1x), according to the maintainer. Thus interpolation/upscale
alone cannot explain it. Three screenshots of the animated OK icon and native
stderr were captured; no active-wait timeout messages appeared, and rendering
continued. This capture did not reproduce the prior unresponsive state.

The earlier black-bar improvement is NOT a confirmed lasting fix. The maintainer
reported recurrence; test7-bars-returned.png confirms white blocks in top/bottom
bars during gameplay. Saved settings at that capture report Display/Original2x;
do not confuse that later state with the maintainer's earlier Original/1x test.
Evidence is in workspace pura70-20260926/test7-line*.png, test7-bars-*,
test7-lines-*-stderr.log and test7-followup-stderr.log.

Next isolated hypothesis: visibility/ordering when resources move between
physical Vulkan queues. Maleoon reports one queue family with two physical
queues (flags 31). Plume distributes virtual workers across both; RenderWorker
uses CPU fence waits for many handoffs. This deserves an inter-queue memory
dependency audit; it is not yet proof that this causes the observed defects.
Reference: https://docs.vulkan.org/spec/latest/chapters/synchronization.html
(fences coordinate queue/host; semaphores coordinate queue operations).

Test8 requests one physical queue per family on Android, using Plume's existing
virtual-queue sharing and submission mutex. On Pura's single-family device this
puts upload/render/presentation work onto one physical queue. Worker threads,
shaders, MRT coverage, attachment dependencies, assets, resolution and frame rate
are unchanged. This is a TEMPORARY diagnostic choice, not a vendor-specific
production fix, and must be revisited before release. Other devices with more
than one family are not guaranteed to use one queue across families.
Test7 timeout diagnostics remain. No forced vkDeviceWaitIdle or quality change.

Optimized non-debuggable Release build, signature, asset comparison and diff
whitespace checks passed. User testing is pending.
APK: Dora64-1.0.3-test8-Android-arm64.apk, versionCode 47
SHA-256: 5049ddbb7420e7cb0c9f7b20973782968903b14fa7de13c5fbc2cd8256949cd4

Next: same moving OK icon and gameplay scene, observe retained rows, bars and
FPS. If it hangs, leave it connected for 10 seconds for timeout log capture.
If artifacts disappear, verify whether missing inter-queue synchronization or
driver queue behavior is responsible before deciding a production policy.
No commit/push/release update.


## Test8 negative result and supplied videos

Maintainer reports no improvement with the one-physical-queue probe. Native
stderr confirms family 0 advertised two queues and requested one. This weakens
the hypothesis that distributing work across those two queues alone causes the
artifacts; it does not rule out other memory visibility/lifetime problems.
Restore the previous queue limit before the next test build. No new APK was
built during the video analysis.

Minimizing/restoring can bring garbage back into previously clean black bars.
The captured log confirms Android surface recreation. A clean still frame is
not evidence of a lasting fix because the corruption flickers.

User supplied D:/Games/dorarecomp/screen/:
- SVID_20260926_115932_1.mp4 (3.200 seconds, 1280x566): gameplay; top bars
  alternate between black and white rectangular regions, while the bottom bar
  also contains green image content. Extracted sequences confirm intermittency.
- SVID_20260926_120006_1.mp4 (6.068 seconds, 1280x566): animated OK icon on
  file selection. User's three full-size stills around 3 seconds clearly show
  thin detached contour rows above/below the moving icon, consistent with the
  reported residual-image appearance. Do not infer exact previous-frame age
  or a particular buffer without tracing the renderer.

The maintainer additionally recognizes a fragment of Doraemon in the flashing
upper bar. Record this as a user observation: sampled video crops independently
confirm corruption but do not yet positively identify that fragment.

Evidence derivatives in workspace pura70-20260926: video1-sheet.jpg,
video2-sheet.jpg, video2-ok-sequence.png, video1-bars.png,
video1-top-strips.png. The user-provided stills are under
C:/Users/black/Pictures/smplayer_screenshots/ with prefix
SVID_20260926_120006_1_00_00_03_ and suffixes 01/02/03.png.

Code audit so far: PresentQueue clears the full swapchain framebuffer using
vkCmdClearAttachments, then a source-image barrier ends that render pass before
VI drawing reopens it with LOAD. Full framebuffer renderArea/clear rectangle
are set; explicit attachment dependencies from test6 are present. New swapchain
image wrappers reset tracked layout. No confirmed stale framebuffer handle or
wrong clear extent has been found. Extra pass boundary merits isolation, but
neither video alone proves a driver clear bug nor an MRT coverage bug.

Separately, VulkanBuffer map/unmap lacks VMA invalidate/flush calls and ignores
read/written ranges; this needs checking against actual selected memory types.
The device exposes non-coherent cached host-visible memory as well as coherent
types. Do not claim this explains GPU-only swapchain corruption without evidence.

Next priority: localize whether corrupted rows already exist in the game color
target or appear during final presentation, then isolate the relevant buffer
preservation/clear/visibility path. No more user videos are needed at this stage.


## Test9: confirmed non-coherent readback defect

A small native Vulkan/VMA memory probe reproduced stale reads on the connected
Maleoon 910 without starting/controlling the game or installing an APK. It uses
the same VMA header and AUTO/host access flags as Plume. Upload selects memory
type 0, flags 0x7 (coherent); readback selects type 1, flags 0xb (cached but
non-coherent). nonCoherentAtomSize is 64 bytes.

After GPU fills, a transfer-to-host dependency and fence wait, the old map/read
sequence returned 1,940,928 stale uint32 values over 16 iterations of a 512 KiB
buffer. Reading immediately after vmaInvalidateAllocation returned ZERO wrong
values in every iteration. The first iteration was clean; reused buffers were
stale, often for the entire range. This proves the cache maintenance defect on
the device, not yet that every graphical artifact has this cause.

Evidence: workspace pura_memory_probe.cpp and
pura70-20260926/memory-visibility-probe.txt. The initial stale read deliberately
reproduces the old, invalid missing-invalidation sequence; it is not a valid
synchronization strategy. The corrected read uses the documented VMA sequence.
Reference: https://gpuopen-librariesandsdks.github.io/VulkanMemoryAllocator/html/memory_mapping.html

NativeTarget reads both GPU-generated pixel-change counts and framebuffer data
through this backend, copying the latter back into emulated RAM. Stale CPU
caches can therefore affect future framebuffer contents/change detection.

Changes:
- VulkanBuffer::map invalidates the requested read range before returning the
  pointer; unmap flushes the written range before unmapping. Null means whole
  allocation, empty range means no corresponding CPU access. VMA handles atom
  alignment and skips cache operations on coherent allocations.
- Both Vulkan buffer-copy entry points publish copies into READBACK heaps with
  a TRANSFER_WRITE -> HOST_READ dependency. No D3D12 resource-state change.
- NativeTarget's two read-only mappings explicitly unmap with an empty written
  range, avoiding unnecessary writeback of GPU results.
- Removed test8's ineffective one-queue limit and log; original max 4 restored
  (Pura advertises 2). No MRT shader, resolution, quality or clear-pass changes.

Optimized non-debuggable Release build, release signature, unchanged 225
asset entries, binary markers and diff whitespace checks passed. This APK has
not been installed or tested in the game by the agent. User validation pending:
moving OK contour rows, gameplay/black bars, minimize/restore and file loading.
Keep settings/data to compare the same scene. Test7 wait diagnostics remain for
the previously reported hang; the older CPU probe remains off on this device.

APK: Dora64-1.0.3-test9-Android-arm64.apk, versionCode 48
SHA-256: 7e115f8ab13b8133bd068ba40a48a53187d86f1c876275a629cc9a0ae12f5206
No commit, push or release update.


## Test9 user result; test10 newly allocated color-target initialization

Maintainer confirms 2D sprite stripes disappeared with test9. Garbage in the
upper/lower black bars still appears after minimizing/restoring the game.
ADB confirms versionCode 48 / 1.0.3-test9; captured native stderr is saved as
workspace pura70-20260926/test9-resume-stderr.log. Preserve the non-coherent
memory fix. Do not claim the remaining swapchain symptom is solved by it.

Additional maintainer observation: opening a dialogue restores black bars
without recreating the Android surface. This shifted the investigation away
from presentation to partially restored game render targets.

Confirmed source gap: RenderTarget::resize/setupColor allocates new textures
with undefined contents. WorkloadQueue resets colorFb->readHeight and later
imports native framebuffer data. RenderTarget::copyFromChanges explicitly
centers that import at original N64 width; expanded side columns are outside
its viewport/scissor. No initial clear occurred unless colorImg.formatChanged.
A same-format resize could therefore expose recycled memory in untouched wide
regions until another game draw covered them. White outer blocks/black center
in the supplied video and the dialogue recovery are consistent with this gap;
the precise resize event after resume still needs confirmation from the new log.
VI::viewRectangle/cropRectangle both return the whole image in this fork, so
one must not assume every observed black band lies outside the VI output.

Test10 clears only newly allocated/resized COLOR targets from the existing
resizedTargets set, once, after GPU setup and before framebuffer imports,
copies and draws. Existing targets are preserved; depth and full-copy
interpolation targets are unchanged. Native content is then restored as before;
new wide margins start black. Coverage is seeded from the initialized primary
target by the existing MRT path. No shader, resolution, queue or quality change.
Android emits one log line per initialized target with address and dimensions.

The provisional loadOp-CLEAR/swapchain-pass experiment was fully reverted before
packaging after the user's dialogue observation led to this concrete source gap.
It is NOT included in test10; presentation and Plume are unchanged from test9.

Optimized non-debuggable Release build, release signature, 225 unchanged
asset entries, native markers and diff checks passed. Game test remains the
maintainer's responsibility. Install over test9 without clearing app data,
repeatedly minimize/restore or lock/unlock in the same scene, and confirm both
black bars and animated OK remain correct. No compatibility claim yet.

APK: Dora64-1.0.3-test10-Android-arm64.apk, versionCode 49
SHA-256: 9d77cd4839e38e1b8ce5ffbd828c37ab92eab4f9a8975360c8c28d9eb1d28f68
No commit, push, installation or release update by the agent.


## Test10 user result and FPS follow-up

Maintainer reports all displayed graphics now correct, including the prior
post-resume bar corruption, but FPS around 30. ADB settings capture shows
frame_rate_mode=Original, render_resolution=Original2x, no MSAA, Expand.
The installed run logs specialized shaders ready. Initializations occur at
startup, surface/size changes (1084 -> 1158 -> 1084 width), resolution changes
and a later target activation, not continuously during ordinary frames.
This also confirms actual game-target resizing around surface recreation.
Do not classify this as a proven performance regression: first compare with
Graphics > Frame rate > Display, retaining 2x and other settings. User must
perform the setting change. Evidence: workspace pura70-20260926/
test10-lowfps-settings.json and test10-lowfps-stderr.log.


## Performance audit after test10; test11 coverage reuse

User confirms remaining performance drops also with Display selected. Captured
test10-display-settings.json confirms Display/2x/Expand/None MSAA. Latest native
stderr shows send_dl wall time around 22-24 ms (includes internal waits, so do
not label it pure CPU parse time), snapshot copy about 0.4 ms and snapshot
submission copy tens of microseconds. Evidence is under workspace pura70-20260926.

Audit of accumulated test changes:
- Test5 MRT fallback adds a full-size color copy to seed coverage, an additional
  blend attachment, alpha merge draw, and pass/layout transitions per raster
  submission. This is the largest structural added GPU workload. An Android
  framebuffer usually has one raster scene (extra scene splitting is RT-related),
  so merely grouping scenes would not materially improve this path.
- Test6 attachment dependencies order LOAD/STORE across pass boundaries; retain
  for correctness. Potential performance cost is not yet measured separately.
- Test7 timed waits use the existing predicates/fences. They do not add a fixed
  delay or an extra fence wait per frame. No timeout explains the current drops.
- Test8 one-native-queue probe is absent since test9.
- Test9 non-coherent cache maintenance is required and reproduced by a native
  probe. It can add overhead and make previously skipped framebuffer updates
  actually run; do not trade correctness for old higher FPS with broken data.
- Test10 initialization runs on allocation/resize only; logs confirm no steady
  per-frame full clear introduced. Keep the graphics fix.
- CPU affinity probe is disabled (empty debug.dora64.fast_cpu property). Its
  remaining ID publication/property polling is not evidence for a large drop.
- Adreno texture fallback is not selected on Maleoon (fallback=0); first
  specialized shader success is logged and shader compilation threads are idle
  in the sampled thread listing. Logging is periodic, not per draw call.
- Existing queue submit/fence/query-result waits predate these test changes.
- Thermalservice had stale cached hot readings, but current HAL readings were
  CPU 39-44 C / GPU 41 C and cooling device values 0. This snapshot does not
  prove thermal throttling caused the earlier or current performance difference.

Test11 removes an independently redundant copy: after coverage is merged into
primary alpha, the coverage image is already valid for the next raster pass.
Reuse it until primary is independently modified. Invalidation covers texture
release/reallocation, native framebuffer changes, clearColorTarget, target
copies, resolveFromTarget, generic markForResolve, and direct interleaved RT
clears. The raster path marks writes before merge; merge establishes matching
alpha afterward. Shader outputs, full alpha publication before all consumers,
MRT precision, memory coherency, new-target initialization, depth behavior and
quality settings are preserved. Native dual-source devices skip this path.

Temporary Android aggregate log every 240 coverage begins reports reused/seeded
counts, with no new GPU readbacks or waits. A speedup is not yet measured, and
return to the old reported 100+ FPS is not promised. First compare the same
scene/settings, then smoke/explosions, animated OK and minimize/restore.

Validation: optimized non-debuggable Release build, release signature, all
225 asset entries unchanged, binary markers and diff checks passed.
APK: Dora64-1.0.3-test11-Android-arm64.apk, versionCode 50
SHA-256: b8e347c3cdcb34b9d2e2669d7b1d5a0288dc5e337e51694cf04267ee4965dd5a
No game launch/install, commit, push or release update by the agent.


## Accepted development checkpoint — 2026-09-26

The maintainer reports that test11 feels faster and requests committing and
pushing this state before testing two additional phones. This is a qualitative
improvement, not a measured stable-FPS claim or a completed playthrough on Pura.
Test10's graphics fixes remain in place. No new GitHub release is requested.

Reference APK: `Dora64-1.0.3-test11-Android-arm64.apk`, versionCode 50,
optimized, non-debuggable Release signed with the permanent release key.
SHA-256: `b8e347c3cdcb34b9d2e2669d7b1d5a0288dc5e337e51694cf04267ee4965dd5a`.
The checkpoint preserves the tested runtime code; only documentation is updated
after this APK's build. Earlier entries describe intermediate builds and should
not be read as the current compatibility status.

Development diagnostics remain available for the next device tests: sparse
coverage statistics and reports of sustained frame waits. The CPU-affinity
experiment is disabled unless `debug.dora64.fast_cpu=1` is explicitly set; it
is not an automatic device policy and was disabled during the Pura performance
audit. Remove or explicitly reassess development probes before a public release.
