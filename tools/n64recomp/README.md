# Pinned recompilation tools

These tools support the ROM-based generation described in `recomp/README.md`.
The game builds only from local generated output; game/RSP sources are not tracked.
This bootstrap only builds N64Recomp and RSPRecomp; it does not regenerate or
overwrite game files and does not require a ROM.

## Build

Requirements: Git, Python 3.10+, CMake 3.20+, a C/C++20 toolchain and internet
access for the first checkout. Run from the Dora64 repository:

```sh
python3 tools/bootstrap_n64recomp.py
```

On native Windows use Python 3 and Visual Studio 2022 with the C++ workload:

```powershell
py -3 tools/bootstrap_n64recomp.py
```

`--cmake` accepts a CMake executable path if CMake is not on PATH.
`--generator` optionally selects a CMake generator. `--jobs` defaults to at most
8. `--work-dir` selects a separate source/build workspace; the default is
`build-tools/n64recomp/`, already covered by the root `/build-*/` ignore rule.
Do not share the same build directory between Windows and WSL.

The script prints both executable paths and writes `build-receipt.json` with
their hashes. Hashes in this receipt identify local binaries; different
compilers/platforms are not expected to produce byte-identical executables.
Subsequent runs validate the checkout and perform an incremental build.
An unexpected revision or source edit stops the script without resetting files.
To change tool revisions, use a new work directory and update the lock/patch
deliberately. The script never runs `git pull` or selects the latest version.

## Locked inputs

- Upstream: https://github.com/N64Recomp/N64Recomp
- Commit: `ffb39cdad1da5de07eaaa48bd1db4a89a7986771`.
- Dependencies are pinned by that commit's recursive Git submodule revisions.
- `doraemon.patch` preserves the three local source modifications, with the
  osPfsInit correction for symbol input documented below. `lock.json` records SHA-256 of the patch and
  both the original and patched versions of each affected file.
- Upstream is MIT-licensed; its notice is preserved in `LICENSE.N64Recomp`.

The patch preserves the working development behavior, rather than asserting
that every historical change remains necessary with newer tool/runtime versions:

1. `cgenerator.cpp`: discard writes to MIPS `$zero`, while evaluating the
   expression to retain memory reads and other potential side effects.
2. `main.cpp`: force generation of seven named low-level functions that would
   otherwise be ignored or treated as runtime replacements.
3. `symbol_lists.cpp`: preserve the project's existing libultra function
   classification, including controller-pack/serial-interface helpers.

The historical force list also included `osPfsInit`. It is intentionally
excluded now: ELF input used the runtime's `osPfsInit_recomp`, and symbols input
must preserve that same call. The other seven entries are unchanged.

These files contain tool changes, not generated Doraemon game functions.
Do not replace the runtime's separate N64Recomp submodule with this checkout:
that dependency is part of the runtime build and remains at its tested revision.

## ROM-based source generation

ROM/function metadata and RSP configuration live in `recomp/`, and all current
game-code edits are preserved in `recomp/patches/`. Full generated Windows and
Linux game builds succeeded, and the user accepted the Windows playtest.
CMake now requires generated output; old game/RSP sources are excluded from
tracking and retained locally as backups. Dora64 uses a separate clean repository;
the previous development history remains in the private Dora64-history archive.

## Validation of the pinned baseline

N64RecompCLI and RSPRecomp built successfully in Release on WSL Ubuntu and
natively on Windows with Visual Studio 2022/MSVC. The WSL incremental rerun
also passed. The original three-file patch was verified byte for byte in step 1.
Step 2 removes only osPfsInit from the forced-generation list; the updated tools
were rebuilt on both platforms and verified by real ROM-based generation. No unit tests were added or run.

On this development machine Windows Git could not connect to GitHub, so the
Windows source/dependency checkout was downloaded through WSL first. Patch
verification, application and compilation then ran natively on Windows using
the same bootstrap. A first Windows run still requires working network access
or an already populated checkout at the pinned revision.

Step 3 additionally reproduces the PC game through function patches; see
`recomp/patches/README.md`. The original development tool checkout, runtime
submodule remain unchanged. Legacy generated files are kept locally but are
no longer tracked or used by the build.
