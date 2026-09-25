# Release packaging

Build the `Dora64` target in **Release** configuration for each platform first.
Commit the source tree and package binaries built from that same commit.
Run `python3 tools/package_release.py --help` for all options. The script creates
separate archives, copies both walkthroughs and dependency licenses, records the
source commit/submodules, and writes `dist/SHA256SUMS.txt`. Output directories must
be outside the tracked tree (for example an ignored `build-release-*` directory).
An existing staging directory is rejected to avoid mixing builds.

Example Linux packaging (run from the repository root):

```sh
python3 tools/package_release.py --version 1.0.1 --platform Linux-x86_64 \
  --binary-dir /path/to/linux-release-build --output /path/to/release-output \
  --dxc-license /path/to/DirectXShaderCompiler-LICENSE.TXT
```

For Windows use `--platform Windows-x64`, point `--binary-dir` to the Release
directory containing `Dora64.exe`, `SDL2.dll`, `dxcompiler.dll`, and `dxil.dll`,
and supply `--crt-dir` pointing to the MSVC x64 CRT redistributable directory.
`--dxc-license` is the upstream DirectXShaderCompiler `LICENSE.TXT`; obtain it
from https://github.com/microsoft/DirectXShaderCompiler/blob/main/LICENSE.TXT.
Use the same output directory for both platforms, then upload both archives and
`SHA256SUMS.txt` to the release tagged at the recorded source commit.

## Required localization assets

The tracked `assets/localization/dialogue_index.json` and `game_text_index.json`
are **runtime dependencies**, together with the language manifest, font and
language packs. They contain only ROM offsets, byte lengths and categories.
The loader reads matching source bytes from the user's validated ROM via
`recomp::get_rom()`. Do not omit these indices from an archive.

The local `*_source.json` catalogs contain original Japanese text/bytes and are
only for translators and audits. They are ignored by Git and excluded from
releases, as are ROMs, saves and personal settings. The catalog extractor also
regenerates the distributable indices when text tables change.

Packaging verifies that the indices are tracked, contain only metadata, and
are present in the staged archive. Before publishing, manually verify English
and Russian dialogue, menu text and item notifications from an extracted build
without any `*_source.json` files. The 1.0 release missed this dependency; the
runtime indices replace it beginning with 1.0.1.


## Android and shared releases

The v1.0.4 release contains Windows/Linux 1.0.4 and Android 1.0. Build the signed
Android Release variant as described in android/README.md, then package it with:

```sh
python3 tools/package_android_release.py --version 1.0 --source-tag v1.0.4 \
  --apk android/app/build/outputs/apk/release/app-release.apk \
  --output /path/to/release-output
```

Use the same output directory as desktop packaging. After all three archives
exist, SHA256SUMS.txt must list all three (the Android packaging command writes
this combined list when run last). Verify the APK certificate/version with SDK
apksigner/aapt before publishing. The archive contains the APK, installation instructions, both walkthroughs,
dependency licenses and source metadata.
Keep signing material and native debug symbols private and outside the archives.
The release tag points to the common source commit, not to a platform version.


When replacing only the Android archive in an existing desktop release, keep the
desktop release tag unchanged and pass `--source-ref <full Android source commit>`
instead of `--source-tag`. For Android 1.0.1, keep Windows/Linux 1.0.4 assets
unchanged, replace the Android ZIP, and refresh SHA256SUMS.txt for all three.


The LOD update replaces all three archives on the existing v1.0.4 release page:
Windows/Linux 1.0.5 and Android 1.0.2 (versionCode 39). Keep the published tag
unchanged; pass `--source-ref <full HEAD commit>` to both packaging tools so
README links and VERSION metadata identify the rebuilt sources precisely.
Character LOD distance defaults to 5x, with Modern=5x and Original=1x.
