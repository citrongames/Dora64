# Android device testing

Results below are user-reported gameplay tests. APKs are installed and tested by
the maintainer, not by the build agent. A successful partial test does not imply
that all levels or every settings combination were covered.


## Latest common test: Android 1.0.3-rc1

On 2026-09-26 the maintainer reports the same test on all eight devices: clean
installation after removing the previous version and caches, device reboot,
startup, intro, menus, dialogues, two levels, touch controls and gamepad,
minimize/restore, repeat launch to check caching, FPS observation, and monitoring
for graphical bugs/crashes. All eight passed this scenario with no bugs or
crashes reported. This is a two-level test, not a complete playthrough on each.
Per-device render resolution and selected frame-rate mode were not explicitly
listed in this report; do not infer matched settings or benchmark methodology.

| Device | Reported SoC | RC1 result | User-reported FPS |
| --- | --- | --- | --- |
| Xiaomi Redmi Note 7 | Snapdragon 660 | Passed common scenario | 20–30 |
| Huawei P40 Pro | Kirin 990 5G | Passed common scenario | 40–50 |
| Huawei P50 | Snapdragon 888 4G | Passed common scenario | 40–50 |
| Xiaomi Mi 11 Ultra | Snapdragon 888 5G | Passed common scenario | Stable 120 |
| Huawei Pura 70 Ultra | Kirin 9010 | Passed common scenario | From 30 in 2D menus to stable 120 in active gameplay |
| vivo X300 FE | Snapdragon 8 Gen 5 | Passed common scenario | Stable 90 |
| Lenovo Legion Y700 (2023) | Snapdragon 8+ Gen 1 | Passed common scenario | Stable 90 |
| Lenovo Legion Y700 (2025) | Snapdragon 8 Gen 3 | Passed; maintainer reference device | Stable 120 |

Slower devices remain candidates for performance investigation. Pura's menu/game
difference is not proof of scheduler trouble; CPU/GPU work, presentation pacing
and scene-specific rendering need isolation before selecting an optimization.
No performance fix is added speculatively to the accepted release candidate.

Final Android 1.0.3 (55) retains RC1 fixes, assets and disabled development probes.
At the maintainer's request, first-run defaults and the Modern button now select
Original frame rate (2x Modern resolution retained). Display/Manual remain
available; stored user preferences and desktop profiles are unchanged. Final APK
is built/signed by the agent; the above gameplay results belong to RC1.

## Historical per-device investigations

| Device | Reported SoC | Coverage | Result |
| --- | --- | --- | --- |
| Lenovo Legion Y700 (2025) | Qualcomm SM8650-AB, Snapdragon 8 Gen 3 | Complete playthrough, all items, Modern defaults, Russian localization, cheats; subsequent touch/layout-editor and port-menu checks | No crashes or game bugs reported during the playthrough. Reported stable 120 FPS with drops during many translucent effects. |
| vivo X300 FE | Qualcomm SM8845, Snapdragon 8 Gen 5 | Launch, two levels, menus and settings | No crashes or bugs observed by the maintainer. Partial playthrough only; FPS not specified. |
| Lenovo Legion Y700 (2023) | Qualcomm SM8475P, Snapdragon 8+ Gen 1 | Same scenario as vivo: launch, two levels, menus and settings | No crashes or bugs observed by the maintainer. Partial playthrough only; FPS not specified. |
| Xiaomi Mi 11 Ultra | Qualcomm SM8350, Snapdragon 888 5G | Android 1.0.2: ROM selection, brief black screen with touch overlay, then crash | 1.0.2 startup failure reproduced in Vulkan shader compilation; 1.0.3-test2 (41) now starts and runs, with reported 120 FPS at 2x. Partial gameplay test; see below. |
| Huawei P50 | Qualcomm SM8350, Snapdragon 888 4G | Android 1.0.2: same startup symptoms reported | 1.0.2 startup failure; test2 now launches, but the maintainer reports occasional drops below 60 FPS even at 1x. Driver/log investigation is recorded below; remaining performance limitations are unresolved. |
| Huawei Pura 70 Ultra | Kirin 9010 (maintainer report), Maleoon 910 (ADB) | Development test3–test11: startup, gameplay image, animated 2D elements, minimize/restore | Test10: maintainer confirms graphics artifacts fixed, including post-resume bars. Test11: feels faster; remaining FPS drops and full-playthrough coverage are unresolved. See android-maleoon-white-frame.md. |
| Xiaomi Redmi Note 7 | Qualcomm SDM660, Snapdragon 660; Adreno 512 (ADB) | test12–test13, LineageOS Android 13, scene textures and CPU dialogue text | test13: user confirms text restored, faster first launch and almost immediate warm launch. Earlier test12 around 20 FPS; no later FPS measurement. RC1 retest pending. See [investigation](android-adreno512-black-frame.md). |
| Huawei P40 Pro | Kirin 990 5G (user), Mali-G76 (ADB) | test12–test14, scene/dialogue and moving letterbox bars | test14: user confirms text restored and old-background flashes in bars gone. RC1 retest pending. See [investigation](android-mali-g76-cpu-text.md). |

## vivo X300 FE report

The maintainer supplied the device and SoC names above. Exact APK version,
Android version, graphics settings, language, input method, level names and
performance measurements were not specified. Do not infer these from the
Lenovo test or from the latest published build.

Original report: «запускается, работает, вылетов и багов нет. Прошёл 2 уровня,
посмотрел меню и настройки. всё норм».

## Lenovo Legion Y700 (2023) report

The maintainer reported the same successful test scenario as on vivo X300 FE:
launch, two levels, menus and settings, with no crashes or bugs observed.
Device/SoC names above are as supplied. Exact APK version, Android version,
graphics settings, input method and FPS were not specified. This is separate
from the full-playthrough result on Lenovo Legion Y700 (2025).

## Snapdragon 888 startup investigation

The maintainer confirmed both failing devices use Android APK 1.0.2. Work is
focused on Xiaomi first, with Huawei verification after Xiaomi is fixed.
See [the investigation](android-adreno660-startup.md) for source analysis,
crash evidence and the standalone shader compilation results. Signed 1.0.3-test2 now has a successful partial gameplay result on Xiaomi,
including reported 120 FPS at 2x. Huawei P50 also starts with test2, with
remaining performance drops documented below. Earlier
compiler-only results are recorded separately from gameplay tests.

For comparison, on 2026-09-26 the exact same released shader pair compiled
successfully in the same standalone Vulkan probe on the maintainer's Lenovo
Legion Y700 (2025), TB321FU, Android 16. This was a compiler-only diagnostic,
not an additional gameplay test, and does not change the original playthrough
report's unspecified Android/APK version.


### Xiaomi Mi 11 Ultra: first 1.0.3-test1 result

The maintainer reports successful startup/gameplay and no visible graphics bugs
on test1 (40), with a longer initial black screen and about 30 FPS. ADB confirms
the automatic shader fallback was selected successfully. This is an optimized,
non-debuggable Release APK. Settings captured after this test: 2x, Expand,
Display frame-rate mode, no MSAA, Russian; full playthrough and performance
investigation are pending. Huawei has not yet tested this build.

Additional Xiaomi test1 report: 1x raises FPS from about 30 (2x) to 60 in the same scene. Test2 restores compatible specialized shaders and adds the English compilation indicator; its confirmed Xiaomi result follows below.


### Xiaomi Mi 11 Ultra: 1.0.3-test2 confirmation

On 2026-09-26 the maintainer reports that test2 (41) works and FPS rose to 120.
ADB-captured settings confirm 2x, Expand, Display frame-rate mode, no MSAA,
ThreePoint filtering and Russian localization. The application log confirms
both automatic texture fallback and successful specialized raster shaders.
This is a partial test; level coverage and sustained performance under heavy
effects have not been established. Huawei P50 testing is next and remains
pending. No public release has been updated with these experimental builds.


### Huawei P50: 1.0.3-test2 confirmation — 2026-09-26

The maintainer reports successful startup with test2, but occasional drops below
60 FPS even at 1x. No exact minimum, scene, duration, driver version or Huawei
log is available yet; the successful Xiaomi performance result must not be
generalized to this device. Touch size adjustment also appears ineffective,
with unexpected stick/A proportions. Source inspection confirms the shared
touch sizing defect described in android-touch-controls.md; on-device checking
of its fix is pending.

Huawei USB diagnostics now captured on test2: Android 12/API 31, compiler
EV031.35.01.10; saved settings are 1x/Original FPS/Japanese. A brief
SurfaceFlinger capture measured about 51.5 presentations/sec. This is not yet
a matched comparison with Xiaomi (2x/Display/Russian); see the investigation
for details and the proposed one-setting comparison.


### Huawei P50: test4 outcome

The maintainer reports higher FPS with the test4 CPU-placement probe, still
below the desired maximum; no exact FPS range supplied. Final device logs
confirm three successful affinity changes and successful specialized shaders
with texture fallback. Further Huawei tuning is deferred at the maintainer's
request. This is a partial test, not full-playthrough validation. The diagnostic
CPU probe is not yet a production policy and no release was updated.


## Huawei Pura 70 Ultra report

Startup and working menu do not constitute gameplay compatibility: the supplied
screenshots show an unusable nearly white game image. ADB and a separate
capability query confirm an unsupported dual-source blending path; test5 now implements a capability-based MRT replacement. Shader compilation
on the device passed; visual gameplay validation remains pending. See
[Maleoon investigation](android-maleoon-white-frame.md).

Test5 update: game image restored, user reports >100 FPS, but intermittent
stripes and garbage in letterbox bars remain. Test6 adds missing Vulkan
attachment LOAD/STORE dependencies; awaiting maintainer visual validation.

Test6 update: maintainer confirms letterbox garbage fixed, but some 2D lines
remain and file selection became unresponsive with music playing. Test7 adds
GPU/CPU frame-wait diagnostics; full compatibility is not yet established.

Test7 follow-up: black-bar corruption recurred (captured); do not retain the
earlier "fixed" conclusion. User reports retained sprite rows even at Original
FPS/1x. No wait timeouts during artifact capture. Test8 isolates physical GPU
queue sharing; awaiting user result, not a production fix.

Pura test8: single physical queue did not improve artifacts. Supplied videos
confirm intermittent bar corruption and residual OK sprite rows. Test9 fixes
a separately reproduced non-coherent CPU readback defect (old sequence:
1,940,928 stale values; invalidate: zero), restores normal queue count, and
awaits maintainer validation in the game. Compatibility is not yet confirmed.

Pura test9 user result: 2D sprite stripes fixed; post-resume black-bar garbage
remains and opening a dialogue clears it. Test10 initializes newly resized game
color targets before centered native-framebuffer restoration, preventing
undefined wide margins. No swapchain experiment included. User test pending.

Pura test10: user confirms graphics fixed, including post-resume bars. Low-FPS report is not yet a regression: captured settings are Original FPS / 2x; awaiting Display comparison.

Pura: Display/2x performance drops confirmed by user after test10. Test11 reuses matching coverage between raster passes to avoid redundant full-image seeds; all graphics fixes retained, speedup and visual regression testing pending.


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


## Redmi Note 7 and Huawei P40 Pro — test12/test13

- Redmi Note 7 / Snapdragon 660 / Adreno 512: test12 restores textures, user
  reports around 20 FPS and corrupted CPU text. ADB screenshot confirms missing
  glyph pixels. Driver lacks the narrow storage formats used by native FB code.
- Huawei P40 Pro / Kirin 990 5G: user reports earlier crash, test12 now starts and
  renders; CPU text remains missing. No exact FPS/driver capture yet.
- test13 (52) fixes native framebuffer storage format mismatches on Android,
  adds persistent Vulkan pipeline caching and reduces redundant dummy-descriptor
  updates. Maintainer visual and cold/warm startup tests are pending. Neither
  device is considered fully validated. See [investigation](android-adreno512-black-frame.md).


### test13 accepted result and test14 candidate

Redmi Note 7: maintainer confirms CPU text restored; much faster first startup
and almost immediate warm startup. No new performance range or full playthrough.
P40 Pro: CPU text still absent, old background flashes while letterbox bars move.
ADB confirms Mali-G76, correct test13 fallback selection and successful persistent
cache load (649393 bytes). test14 removes remaining native texel-buffer limit /
usage violations via packed storage buffers; visual verification is pending.
See [P40 investigation](android-mali-g76-cpu-text.md).


## Android 1.0.3-rc1 — full device retest pending

Maintainer confirms P40 test14 fixes both absent CPU dialogue text and old scene
flashes in moving letterbox bars. All eight owned devices have now undergone
development testing; this does not mean each has tested the same latest binary.
RC1 provides one signed candidate for the full repeated device matrix.

VersionCode 54, permanent release signature, non-debuggable Release with optimized
RelWithDebInfo native code (symbols retained locally). All test14 gameplay,
compatibility, synchronization, shader/cache, localization, touch and lifecycle
behavior is retained. No graphics quality/defaults changed for this candidate.

Development-only frame timing, snapshot byte/hash/comparison reports, coverage
statistics, timed-wait probes and CPU-affinity experiment are compiled out using
DORA64_ANDROID_DIAGNOSTICS=OFF (default). The CPU experiment cannot be activated
by a leftover phone property in this build. The graphics RDRAM mutex, snapshot
capture/queues and wait predicates are unchanged. Error/startup/cache logs remain.
Diagnostic probes can still be built explicitly for future investigation.

Checks: optimized Release build, permanent signature, non-debuggable manifest,
225 assets identical to test14, expected compatibility code present and temporary
probe strings absent from the packaged native library. No APK install/game test
by the agent. No commits, pushes, tags or public release changes in this step.

Retest per device: startup and repeat launch; Russian dialogue/file menu; water
and alpha effects; moving cinematic bars; touch/gamepad; pause/resume and audio;
save/load. Record resolution and frame-rate mode alongside observed FPS. These
are suggested focused checks for the maintainer, not automated test claims.

APK: Dora64-1.0.3-rc1-Android-arm64.apk
SHA-256: ad0027b435b3aa897568132209855522938beb87765676eb77edc48b540dcf06
