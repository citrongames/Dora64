# Dora64

Private development repository for a native PC recompilation of *Doraemon: Nobita to Mittsu no Seireiseki* for Nintendo 64.

The project recompiles the original MIPS game code into native code and uses N64ModernRuntime and RT64 for the platform, audio, input, and rendering layers. It is a native recompilation project, not a conventional whole-system emulator.

## Repository layout

- `recomp/` — ROM metadata, RSP configuration and reproducible PC function patches.
- `build-tools/game/` — local generated game functions and RSP code; excluded from Git.
- `src/` — PC entry point, game hooks, input/audio integration, RT64 bridge, and the system overlay.
- `lib/rt64/` — private RT64 fork, included as a Git submodule.
- `lib/N64ModernRuntime/` — private runtime fork, included as a Git submodule.
- `BUGS_AND_TODO.md` — known issues found during playtesting.
- `PC_ENHANCEMENTS_ROADMAP.md` — planned PC-specific improvements.
- `MANUAL_REFERENCE.md` — notes extracted from the original Japanese manual.

## Required game data

The original ROM is copyrighted and is intentionally not included. Place your
own legally obtained original Japanese `.z64` ROM next to the executable.
Any filename is accepted (`.Z64` also works). The game checks the complete file
against the supported ROM's XXH3 hash; another game, modified ROM or unsupported
version will not be loaded. Matching files are used in place, without renaming
or copying them. Other files are left untouched.

If no supported ROM is found, a dialog explains what is needed and stays open
until dismissed. Only files directly next to the executable are searched;
parent directories, subdirectories and the launcher's working directory are
not searched. Settings and saves are kept in the executable's directory.

ROM images, saves, logs, local mods, and build outputs are excluded from Git.

## Clone

Clone recursively so the private runtime forks are checked out at their tested revisions:

```sh
git clone --recurse-submodules https://github.com/citrongames/Dora64.git
```

Access to all private repositories is required.

## Generate game code

Game function sources and RSP microcode are **not included** in Git. Both are
generated locally from your original Japanese ROM, then all PC patches are
applied and verified. No decompilation checkout or ELF is required.
Requirements: Python 3.10+, Git, CMake 3.20+ and a C/C++20 compiler.

From the repository root on WSL/Linux:

```sh
python3 tools/bootstrap_n64recomp.py
python3 tools/generate_game.py --rom /path/to/your.z64 --output-dir build-tools/game
```

On native Windows, with Visual Studio 2022 and the C++ workload installed:

```powershell
py -3 tools/bootstrap_n64recomp.py
py -3 tools/generate_game.py --rom "C:\path\to\your.z64" --output-dir build-tools/game
```

The tools are pinned in [`tools/n64recomp/`](tools/n64recomp/README.md).
The ROM is checked by size and SHA-256 and is never copied into generated output.
Each generation directory must be new. After changing patches or metadata,
generate into another directory and pass its absolute path to CMake with
`-DDORAEMON_GENERATED_DIR=...`. Stale generated output is rejected at configure
time. Ordinary native changes in src/ only need a rebuild.

See [`recomp/patches/README.md`](recomp/patches/README.md) for the workflow to add
and record future game changes; [`recomp/README.md`](recomp/README.md) describes
the ROM metadata. Generated files, ROMs and tool/build output stay local.

## Windows build

After generation, run from a Visual Studio developer terminal with CMake on PATH:

```powershell
cmake -S . -B build-windows -G "Visual Studio 17 2022" -A x64
cmake --build build-windows --config RelWithDebInfo --target Dora64 --parallel 8
```

Place your ROM beside `build-windows/RelWithDebInfo/Dora64.exe`
before launching. Windows is the primary playtesting platform.

## Linux build

Install a C++ compiler, CMake, Ninja, SDL2 development files, and Vulkan development
tools. After generation, run:

```sh
cmake -S . -B build-release -G Ninja -DCMAKE_BUILD_TYPE=RelWithDebInfo
cmake --build build-release --target Dora64 -j8
```

Place your ROM beside `build-release/Dora64` before launching it.
The WSL Vulkan path may behave differently from the native Windows build.
Keep Windows and Linux build/tool output in separate directories.

## Headless regression tests

The lighting snapshot test checks frame isolation, world/object/ambient lights,
the normal path for ROM constants, and cold-reset cleanup. The decal depth test
checks RDP depth-delta normalization and recorded camera angles that previously
caused shadows to disappear. The menu background test checks mosaic recognition
and gap-free tiling at native, wide, ultrawide and fractional output sizes,
as well as the recorded menu transition bounds. The sky panorama test uses the
original sector generator to compare central texture coordinates, verify wide
coverage, preserve caller state, and check extended-memory display-list addressing.
The modern camera test checks input consumption, camera ownership/locks, caller
state and orbit collision response with a simulated sliding collision solver;
it does not validate real level geometry. The cheats test checks player-only HP,
life counters, disabled behavior and race-profile selection on synthetic guest
memory. The test executables run without a ROM or a graphics window; configuring
this project still requires the generated game sources above. Tests are optional
and disabled by default.
Enable them in an existing build directory:

```sh
cmake -S . -B build-release -DDORAEMON_BUILD_TESTS=ON
cmake --build build-release --target lighting_snapshots_test decal_depth_test menu_background_test sky_panorama_test modern_camera_test cheats_test draw_distance_test model_pool_test
ctest --test-dir build-release --output-on-failure
```

For Visual Studio builds, also pass `--config RelWithDebInfo` to the build
command and `-C RelWithDebInfo` to CTest.


## Settings profiles

**Esc → Game** has two large preset buttons that apply and save these settings
immediately. Individual settings can still be adjusted afterwards. A first
launch without `doraemon_pc_settings.json` applies and saves **Modern**.
Existing settings files retain their saved values. On a fresh launch the FPS
counter is off. If `doraemon_input_settings.json` is also absent, background
gamepad input starts enabled and is saved. Profile buttons do not change these
two preferences.

| Setting | Modern | Original |
| --- | --- | --- |
| Autosave | On | Off |
| Modern camera | On | Off |
| Frame rate | Display | Original |
| Game render resolution | Fit to window | Original (1x) |
| Game aspect ratio | Fit to window / fullscreen | Original (4:3) |
| Gameplay HUD layout | Fit to window / fullscreen | Original (4:3) |
| Object / NPC draw distance | 5x | 1x |

The profiles only change the settings listed above. Language, controls,
camera sensitivity/inversion/mouse capture, cheats, audio, output window size,
anti-aliasing and texture filtering retain their current values.

## Frame rate

**Esc → Graphics → Frame rate** offers **Original**, **Display**
(match the monitor) and **Manual** (60–360 FPS, initially 144). Settings persist
and apply without restarting. RT64 limits the target to the detected display
refresh rate; the menu shows both values. Select 144 Hz in the operating system
to use a 144 Hz display at its full rate.

The FPS overlay counts output frames, including interpolated frames. This
measures renderer output, not the monitor's physical scanout or the number of
distinct animation poses.

Higher rates use RT64 frame interpolation while game timing stays unchanged.
Windows gameplay at 144 Hz and a manual 120 FPS target has been verified by
the user. A sustained gameplay capture reached 143.1–143.9 FPS at 144 Hz.
Some CPU-written screens temporarily prevent interpolation. Select Original
to return to the original presentation.

## Object and NPC draw distance

**Esc → Graphics → Object / NPC draw distance** adjusts actor distance from
**1x (Original)** to **5x**. The Modern profile starts at 5x; the setting persists and applies
without restarting. Both model residency and rendering use the multiplier.
Distance-based destruction of temporary actors keeps its original range.
This does not extend the level mesh, fog, projection far plane or scripted
loading of level sections. Resident NPCs may update earlier. The model pool
is expanded to 2048 elements (2016 during gameplay, retaining the original
32-element reserve). Its storage and pointer tables use the runtime heap;
extra-distance models are no longer forcibly evicted to make room for spawns.
Windows gameplay validation at 5x covered five stages, reaching 283 model
entries with no allocation failures. Full playthrough validation remains open.
Optional diagnostics are enabled with `DORAEMON_DRAW_DISTANCE_DIAGNOSTICS=1`
and write `doraemon-draw-distance.log` in the working directory.

## Modern camera

Enable **Esc → Game → Modern camera** for continuous orbit control with the
right stick. Enable **Capture mouse** in the same menu to rotate with the mouse;
mouse capture is off by default, including with older settings files. Original
camera control is available through the Original profile. New installations
start with Modern camera enabled. Existing
camera key bindings (I/J/K/L by default) also rotate continuously. The same menu
provides stick speed, mouse sensitivity and independent X/Y inversion; settings persist.
Both inversion switches are off by default and affect mouse and stick rotation.
Zoom with the mouse wheel, Page Up / Page Down, or hold Camera mode (RB / E
by default) while moving the right stick vertically. Dedicated zoom bindings
can be changed in Controls. The chosen distance survives dialogue close-ups and
scene changes during the current run.
Vertical orbit is limited to 0–80 degrees: the camera can reach a horizontal
view of its focus, but cannot orbit below it.
Esc opens the PC menu and releases the mouse. Losing focus, pausing the game,
and entering a scripted camera also release mouse capture.

The modern camera replaces manual A/B control, preserves shake effects and
camera-relative movement, and shortens its distance at level obstacles. Original
locked/scripted cameras retain control in special areas and boss encounters.
The first playtest confirmed manual control and original camera ownership in
boss fights, cutscenes and races. Windows/Linux builds and automated tests pass;
the second playtest confirmed NPC dialogue locks, item close-up recovery and
zoom. The third playtest confirmed the optional mouse-capture switch.

## Cheats

Open **Esc → Cheats** for three independent options, all off by default:

- **Infinite HP** keeps the active player's health at its maximum (250).
- **Infinite lives (9)** keeps nine lives, including after a failed attempt.
- **First-race torpedo speed** uses the first race's torpedo movement profile in
  all three races, preserving race progression and rewards. Set it before the
  race starts; its checkbox is locked while the race is running.

Settings persist across launches. **Cheats On** stays in the bottom-right corner
whenever a cheat is enabled, independently of the FPS counter setting.
The Windows cheats build has been confirmed working by the user.

## Application icons

The supplied PNG exports and multi-size Windows ICO live in `assets/icons/`.
The original layered GIMP source is `resources/artwork/dora64_icon.xcf` and is
kept out of build output. CMake embeds the ICO in the Windows executable and
embeds the 256 pixel PNG for the SDL window icon on both platforms. Replacing
these exports and rebuilding updates the embedded resources and build assets.

Linux builds also contain a desktop entry and a per-user installer. Run it
once to register the build in your applications menu (Python 3, no sudo):

```sh
python3 build-release/install-desktop.py
```

The installer uses all seven PNG sizes in the hicolor icon theme and writes
`dora64.desktop` under `$XDG_DATA_HOME/applications` (by default
`~/.local/share/applications`). Its app ID matches the SDL2 X11/Wayland window
identity. Run the installer again after moving the build or changing the icons.
The game's ROM and save/config discovery are unchanged.
