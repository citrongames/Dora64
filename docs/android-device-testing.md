# Android device testing

Results below are user-reported gameplay tests. APKs are installed and tested by
the maintainer, not by the build agent. A successful partial test does not imply
that all levels or every settings combination were covered.

| Device | Reported SoC | Coverage | Result |
| --- | --- | --- | --- |
| Lenovo Legion Y700 (2025) | Qualcomm SM8650-AB, Snapdragon 8 Gen 3 | Complete playthrough, all items, Modern defaults, Russian localization, cheats; subsequent touch/layout-editor and port-menu checks | No crashes or game bugs reported during the playthrough. Reported stable 120 FPS with drops during many translucent effects. |
| vivo X300 FE | Qualcomm SM8845, Snapdragon 8 Gen 5 | Launch, two levels, menus and settings | No crashes or bugs observed by the maintainer. Partial playthrough only; FPS not specified. |
| Lenovo Legion Y700 (2023) | Qualcomm SM8475P, Snapdragon 8+ Gen 1 | Same scenario as vivo: launch, two levels, menus and settings | No crashes or bugs observed by the maintainer. Partial playthrough only; FPS not specified. |
| Xiaomi Mi 11 Ultra | Qualcomm SM8350, Snapdragon 888 5G | Android 1.0.2: ROM selection, brief black screen with touch overlay, then crash | 1.0.2 startup failure reproduced in Vulkan shader compilation; 1.0.3-test2 (41) now starts and runs, with reported 120 FPS at 2x. Partial gameplay test; see below. |
| Huawei P50 | Qualcomm SM8350, Snapdragon 888 4G | Android 1.0.2: same startup symptoms reported | 1.0.2 startup failure; test2 now launches, but the maintainer reports occasional drops below 60 FPS even at 1x. Driver/log investigation is recorded below; remaining performance limitations are unresolved. |
| Huawei Pura 70 Ultra | Kirin 9010 (maintainer report), Maleoon 910 (ADB) | Development test3–test11: startup, gameplay image, animated 2D elements, minimize/restore | Test10: maintainer confirms graphics artifacts fixed, including post-resume bars. Test11: feels faster; remaining FPS drops and full-playthrough coverage are unresolved. See android-maleoon-white-frame.md. |

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
