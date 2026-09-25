# Dora64 for Android

The Android port is included in this repository alongside the desktop port. Its package
name is `com.n64recomp.dora64`. Build against the pinned Dora64 forks of RT64
and N64ModernRuntime, plus the pinned SDL2 submodule. The original ROM is not
packaged in the APK.

## Build

Requirements: JDK 17, Android SDK platform 36, build tools 35.0.0, Android
NDK 27.0.12077973, Android CMake 3.22.1, and the generated game code in
`build-tools/game`.

From the repository root in WSL:

```sh
cmake -S lib/rt64/src/tools/file_to_c -B build-tools/file-to-c-host
cmake --build build-tools/file-to-c-host
export DORA64_HOST_FILE_TO_C="$PWD/build-tools/file-to-c-host/file_to_c"
export JAVA_HOME=/path/to/jdk17
export ANDROID_HOME=/path/to/android-sdk
./android/gradlew -p android :app:assembleOptimized -Pdora64Runtime=true
```

The result is `android/app/build/outputs/apk/optimized/app-optimized.apk`, signed
with the local debug key so it can update previous test builds. This variant uses
native `RelWithDebInfo` (`-O2 -g -DNDEBUG`) for the generated game, RT64, and
runtime. Unstripped native libraries remain under
`android/app/build/intermediates/cxx/RelWithDebInfo/` for crash analysis; the APK
contains stripped libraries. The Android package is not debuggable.

Use `:app:assembleDebug` only when an unoptimized native build is needed for
debugging, not for gameplay performance measurements. The framebuffer shader
optimization workarounds for Adreno remain independent of this native build
type. The optimized variant disables RT64's per-command debug file log; Android
startup and periodic timing messages still go to `native-stderr.log`.

The build without `-Pdora64Runtime=true` is only a small SDL/Vulkan probe.
For a signed release, set `DORA64_SIGNING_PROPERTIES` to an absolute path to a
private Java properties file outside the repository, then run:

```sh
export DORA64_SIGNING_PROPERTIES=/private/path/signing.properties
./android/gradlew -p android :app:assembleRelease -Pdora64Runtime=true
```

The file supplies `storeFile` (absolute keystore path), `storePassword`, `keyAlias`
and `keyPassword`. Never commit the file or keystore. Back them up privately;
future updates must use the same key. Without this file, Release is unsigned.
Release uses the same native RelWithDebInfo optimizations as Optimized and outputs
`android/app/build/outputs/apk/release/app-release.apk` when signed.
Only runtime assets are packaged; local `*_source.json` catalogs are excluded.

## ROM and user data

On first launch, choose the supported original Japanese 8 MiB `.z64` ROM in
the Android document picker. The ROM and bundled assets stay in the app's
private internal files directory. ROM contents are validated before gameplay.

Saves, settings, and diagnostic logs live in the app's external files directory,
normally `/storage/emulated/0/Android/data/com.n64recomp.dora64/files/`:

- `saves/doraemon.n64.jp.bin` and its `.bak` backup: game progress.
- `doraemon_pc_settings.json`: game and graphics settings.
- `doraemon_input_settings.json`: input bindings.
- `doraemon_touch_settings.json`: touch positions, size, opacity and sensitivity.
- `native-stderr.log`, `android-trace.log`, `rt64/rt64.log`: diagnostics.

When upgrading an older build, Dora64 copies existing saves and settings from
the previous private directory if the destination file does not already exist.
It keeps the original files. Android removes the external app-specific
directory when the app is uninstalled, so copy saves elsewhere before uninstalling.
An APK installed as an update keeps this directory.

PowerShell examples with multiple ADB devices (replace `<serial>` with the
Lenovo serial reported by `./adb.exe devices`):

```powershell
./adb.exe -s "<serial>" pull "/sdcard/Android/data/com.n64recomp.dora64/files/native-stderr.log" "D:\Games\dorarecomp\logs\dora64-lenovo-stderr.txt"
./adb.exe -s "<serial>" pull "/sdcard/Android/data/com.n64recomp.dora64/files/rt64/rt64.log" "D:\Games\dorarecomp\logs\dora64-lenovo-rt64.txt"
./adb.exe -s "<serial>" pull "/sdcard/Android/data/com.n64recomp.dora64/files/saves/doraemon.n64.jp.bin" "D:\Games\dorarecomp\logs\doraemon.n64.jp.bin"
```

The user tests APKs on the devices. The porting work does not install or run
the APK. Test gameplay first on the Lenovo Legion Y700 (2025); investigate
other chipsets after the baseline build is stable.


## Current validation and integration

The maintainer completed the game with all items on Lenovo Legion Y700 (2025),
Snapdragon 8 Gen 3, using the Modern defaults, Russian localization and cheats.
Subsequent r32 touch controls/layout editing and r33 Android port-menu changes
were also confirmed working. Performance can still fall during heavy translucent
effects at high internal resolutions; 2x is the Android default and 4x is available.

Touch controls and the editor are documented in
[docs/android-touch-controls.md](../docs/android-touch-controls.md).
Android 1.0.1 (versionCode 37) is packaged alongside Windows/Linux 1.0.4.
It uses the merged renderer dependencies and permanent release signing.
Future releases using the same key can update in place.


Android 1.0.1 declares `appCategory="game"` and the legacy `isGame` flag for
Android 7. The maintainer confirmed automatic game-center detection with the r36
candidate. The final build retains the permanent signing key and can update 1.0
in place. It replaces the Android archive in the existing v1.0.4 release.
