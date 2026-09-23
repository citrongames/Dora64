# Dora64 for Android

The Android port lives in the existing Dora64 repository on the `android-port`
branch. Its application ID is `com.n64recomp.dora64`. It uses the pinned SDL2
submodule and the Dora64 RT64/N64ModernRuntime forks; do not replace those
forks with upstream checkouts.

## Build

Requirements: JDK 17, Android SDK platform 36, build tools 35.0.0, Android
NDK 27.0.12077973, Android CMake 3.22.1, Gradle wrapper, and host CMake.
The patched generated game code in `build-tools/game` must already be present;
see the repository's generation instructions. The original ROM is never
packaged into the APK.

From the repository root in WSL:

```sh
cmake -S lib/rt64/src/tools/file_to_c -B build-tools/file-to-c-host
cmake --build build-tools/file-to-c-host
export DORA64_HOST_FILE_TO_C="$PWD/build-tools/file-to-c-host/file_to_c"
export JAVA_HOME=/path/to/jdk17
export ANDROID_HOME=/path/to/android-sdk
./android/gradlew -p android :app:assembleDebug -Pdora64Runtime=true
```

The APK is `android/app/build/outputs/apk/debug/app-debug.apk`. It is signed
with the local Android debug key. A release signing key and Play publication
are separate later steps; keep the release key private.

The default build without `-Pdora64Runtime=true` is a small SDL/Vulkan probe
and contains no game runtime.

## First device test

On first launch, choose your own original Japanese 8 MiB `.z64` ROM in the
Android document picker. The app copies it into private app storage and checks
its hash before starting. The APK contains no ROM. A supported gamepad is the
current input path; touch controls have not been implemented yet.

This build has only been compiled and inspected. It has not been installed or
launched by the porting work. Please test the APK on your phones and tablets,
starting with an ARM64 Vulkan device and a connected gamepad. Report the
device model, Android version, chipset, and any launch error or logcat output.
