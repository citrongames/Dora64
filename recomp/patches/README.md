# Maintaining game patches

`game.patch` contains small unified diffs against individual generated function
bodies. The file names inside it are function identifiers, not `funcs_N.c`.
Generation can regroup functions without moving or losing a patch. The current
migration preserves 123 changed functions (about 81 KB of patch text).

`manifest.json` records the original/resulting SHA-256 for every affected
function and a fingerprint of all 1092 resulting functions. Hunks apply only
at their exact offsets with exact context; there is no fuzzy patching. These
files store edits plus short context, not complete generated game source files.
Keep patch files with LF line endings, as enforced by `.gitattributes`.

Native PC behavior belongs in `src/doraemon_*`. In particular, autosave and
reset helpers formerly embedded in funcs_17.c now live unchanged in
`src/doraemon_game_hooks.c`. `src/doraemon_recomp_hooks.h` supplies shared C
declarations/includes. In-function extern declarations from existing fixes are
preserved. New helpers should also be written in src/, with only the necessary
calls/argument changes recorded in the generated function patches.

## Generate and build the complete PC game

Run from the repository root. Each output directory must be new; previous
generated output is never overwritten automatically.

```sh
python3 tools/generate_game.py --rom doraemon.n64.jp.z64 --output-dir build-tools/game-next
cmake -S . -B build-release -DDORAEMON_BUILD_TESTS=OFF -DDORAEMON_GENERATED_DIR="$PWD/build-tools/game-next"
cmake --build build-release --target Dora64 -j8
```

On native Windows use `py -3` instead of `python3`, pass an absolute Windows
path to DORAEMON_GENERATED_DIR and build with `--config RelWithDebInfo`.
The generation command uses only the provided ROM, project metadata/patches
and pinned tools. It does not read the tracked generated game files or ELF.
The bootstrap in `tools/n64recomp/README.md` builds the tools first if needed.

CMake accepts only PC-patched output, checks its input fingerprints and stops
if patches, metadata or generation scripts changed after generation. Ordinary
native edits in src/ only require rebuilding. Generated C files are deliberately
editable during development; refresh their patch record before committing.

## Add or change a hook

1. Generate an unpatched reference and a fully patched development copy:

   ```sh
   python3 tools/generate_recompiled_base.py --rom doraemon.n64.jp.z64 --output-dir build-tools/base-edit
   python3 tools/generate_game.py --rom doraemon.n64.jp.z64 --output-dir build-tools/game-edit
   ```

2. Implement the native behavior in src/. Locate the game function by name in
   `build-tools/game-edit/RecompiledFuncs/` and edit its body to call the helper.
   Configure DORAEMON_GENERATED_DIR to that directory and build for playtesting.
   Do not add standalone native functions or includes to generated files;
   declare them in doraemon_recomp_hooks.h and implement them in src/ instead.

3. Record the **whole edited PC version** against the unpatched reference:

   ```sh
   python3 tools/record_recomp_patches.py --base build-tools/base-edit --edited build-tools/game-edit
   git diff -- recomp/patches src
   ```

   This updates game.patch and manifest.json, including existing fixes. The
   recorder verifies replay before writing. It rejects added/removed generated
   functions, unsupported preamble edits, header/lookup edits, RSP edits and
   registration-table edits instead of silently dropping them. Metadata/RSP
   changes belong in recomp/, separately from function-body patches.

4. Generate once more into a new output directory, build and playtest that
   regenerated version. Commit the patch files and native source changes.
   Generated code, ROM, receipts and build output stay local.

Do not update hashes by hand to bypass a failed patch. A base mismatch means
the wrong ROM, tool/metadata revision or an intentional baseline change that
needs a separate migration review. `--patch-dir` can write a candidate patch set
elsewhere for review without replacing the repository's patch set.

## Registration compatibility

`recomp/registration.json` preserves the 1138 original registered ROM function
entries. Symbol input otherwise adds four zero-size runtime entries that ELF
input used only for direct calls. Generation omits those four entries and
checks the complete name/ROM-offset/size mapping. It does not remove their
direct-call declarations. Section indices and file order are implementation
details; runtime registration uses the new output consistently.

## Migration validation

All 1092 function bodies match the previously working PC version after the
known static-function-name normalization. Native autosave/reset helper bodies
were moved verbatim. RSP matches after independent dispatch-case ordering is
normalized. Both native Windows/MSVC and WSL/Linux game builds succeeded using
the newly generated files. Re-recording the real restored PC output produced
byte-identical patch and manifest files. No unit tests were added or run.

The user confirmed the generated Windows build works. A fresh Linux build
also succeeded from a source-only snapshot without the old game/RSP files
(reusing the checked-out dependency sources and pinned tool binaries only).
Native Windows was rebuilt with the default generated-source path.
Generated game sources
and RSP code are excluded from Git; their old local paths are ignored and never
used by CMake. The default DORAEMON_GENERATED_DIR is `build-tools/game/`.
A missing generation receipt stops configuration with generation instructions.
The current private Git history is retained; the public repository will be
created separately with a clean history.

For dialogue-reference audits, `tools/audit_dialogue_references.py` defaults to
`build-tools/game/RecompiledFuncs`. If using a different generation directory,
pass `--source-directory <output>/RecompiledFuncs`. The tool identifies the main
section by name, independently of its generated section index.
