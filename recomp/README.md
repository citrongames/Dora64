# ROM-based baseline generation

The baseline generation step removes the generation-time dependency on the local
`doraemon1` decompilation checkout and its ELF. The files in this directory are
metadata and custom hooks, not game instructions or a ROM dump.

For the complete PC game use `tools/generate_game.py` and the workflow in
[`patches/README.md`](patches/README.md). CMake defaults to the local output in
`build-tools/game/` and has no fallback to old tracked source. Output from
`generate_recompiled_base.py` is the historical baseline and does **not** contain
the PC fixes; it is used only as a reference when recording patches.

## Generate

Requirements: Python 3.10+, the original Japanese big-endian .z64 (8 MiB), and
the pinned tools built according to `tools/n64recomp/README.md`.

From the project root in WSL/Linux:

```sh
python3 tools/bootstrap_n64recomp.py
python3 tools/generate_recompiled_base.py --rom doraemon.n64.jp.z64
```

Native Windows, with Python 3 and the Visual Studio C++ toolchain installed:

```powershell
py -3 tools/bootstrap_n64recomp.py
py -3 tools/generate_recompiled_base.py --rom .\doraemon.n64.jp.z64
```

Any ROM filename/path is accepted; its exact size and SHA-256 must match
`baseline.json`. No ROM is copied or modified. A new ignored directory
`build-tools/recompiled-base-<unique suffix>/` receives the C files, RSP output,
resolved configs, logs and a generation receipt. Its path is printed.
`--output-dir` can select another **new** directory; existing paths are refused.
Keep custom output directories outside tracked source trees.
`--tools-dir` selects the workspace containing the bootstrap's build receipt.

The script checks the tool receipt against the current lock and verifies the
actual tool binary hashes. It then checks all 1092 generated function bodies
and the RSP code against the recorded historical baseline. It never reads the
old ELF, a sibling checkout or the tracked game C files.

## Contents and provenance

- `doraemon.syms.toml`: 1205 name/address/size entries in two sections.
  Exported once from the development ELF with the pinned N64Recomp's
  `--dump-context`. Its 28 manual callbacks and 702 size overrides are already
  incorporated; they must not be added again at generation time.
- `doraemon.toml.in`: symbol/ROM paths and the three original custom hooks
  (controller-pointer initialization and two CPU framebuffer notifications).
  These are only the baseline hooks; later PC changes are recorded separately
  in `patches/` and applied by `generate_game.py`.
- `aspMain.toml.in`: RSP code offset 0xA80B0, size 0xE20, IMEM address 0x1080
  and the 16 explicit indirect-branch targets, preserved from the working config.
- `baseline.json`: ROM identity and code fingerprints, with no game bytes.

The development ELF is used only to establish the initial metadata and
comparison baseline, not as a distributed or ongoing build dependency.

## Details needed to preserve behavior

The ELF's .makerom section has zero VRAM, but the entrypoint is loaded from ROM
0x1000 to 0x80000400. Symbol input computes ROM addresses from section VRAM, so
the metadata represents just this 0x50-byte entrypoint range explicitly.
Only code sections are retained: .makerom/.main become indices 0/1 instead of
1/3 among the old 58 ELF sections. Static names therefore change from
`static_3_<address>` to `static_1_<address>`. Function comparisons normalize only
that naming difference and C-file grouping/order, not instructions or calls.
The generated registration metadata is integrated together with its code;
do not mix old and new output files.

The stock dump omits zero-size symbols. Twelve known system symbols from the
data-symbol dump were restored to prevent generation of unsupported hardware
instructions (including the osGetCount call at 0x80091950). The zero-size
`func_8009FAD0` boundary also preserves the existing empty static wrapper at
that address; this migration does not change its behavior.

The original local tool patch forced osPfsInit to recompile only in symbols
mode. ELF mode instead used osPfsInit_recomp from the runtime. That one entry
was removed from the force list so both paths preserve the working runtime
call. The original developer's N64Recomp checkout was left untouched.

RSPRecomp emits the independent `case: goto` entries in its indirect-jump
dispatcher in a platform-dependent order. The fingerprint sorts only those
lines before hashing; all other RSP text is checked unchanged (with normalized
line endings). Generated RSP output itself is not edited by the wrapper.

## Validation and next step

The pinned tools were rebuilt on WSL/Linux and native Windows/MSVC. Both ran
generation from ROM + these metadata files successfully. All 1092 function
bodies match the fresh ELF baseline after static-name normalization; RSP code
matches the currently working aspMain after dispatch-order normalization.
No unit tests were added or run during migration.

The 123 changed PC function bodies are now preserved as replayable patches.
The complete generated game has been built on Windows and Linux; see
`patches/README.md` for development and validation details. The user confirmed
the generated Windows build works. Generated game files are now excluded from
tracking; the old local files are retained only as migration backups.
