"""Generate the PC-patched game from ROM, metadata and recorded function patches."""

import argparse
import json
import hashlib
from pathlib import Path
import re
import subprocess
import sys
from generate_recompiled_base import PROJECT_ROOT, FUNCTION_PATTERN, normalized_static_names, function_fingerprint
from recomp_patch_utils import read_functions, sha, apply_patch


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--rom", required=True, type=Path)
    parser.add_argument("--output-dir", required=True, type=Path, help="New output directory")
    parser.add_argument("--tools-dir", type=Path, default=PROJECT_ROOT / "build-tools/n64recomp")
    args = parser.parse_args()
    patch_dir = PROJECT_ROOT / "recomp/patches"
    manifest = json.loads((patch_dir / "manifest.json").read_text(encoding="utf-8"))
    patch = (patch_dir / "game.patch").read_text(encoding="utf-8")
    if manifest["format"] != 1 or sha(patch) != manifest["patch_sha256"]:
        raise RuntimeError("Patch manifest mismatch; use record_recomp_patches.py to record edits.")
    output = args.output_dir.resolve()
    subprocess.run([sys.executable, str(PROJECT_ROOT / "tools/generate_recompiled_base.py"),
                    "--rom", str(args.rom.resolve()), "--output-dir", str(output),
                    "--tools-dir", str(args.tools_dir.resolve())], check=True)
    source = output / "RecompiledFuncs"
    before = read_functions(source)
    for name, hashes in manifest["functions"].items():
        if name not in before or sha(before[name]) != hashes["before_sha256"]:
            raise RuntimeError(f"Wrong original body for {name}")
    after, changed = apply_patch(before, patch)
    if changed != manifest["functions"].keys():
        raise RuntimeError("Patch function list differs from manifest")
    for name in changed:
        if sha(after[name]) != manifest["functions"][name]["after_sha256"]:
            raise RuntimeError(f"Wrong patched body for {name}")
    for path in source.glob("*.c"):
        text = path.read_text(encoding="utf-8")
        def replace(match):
            name = normalized_static_names(match[1])
            return after[name].replace("static_MAIN_", "static_1_")
        text = FUNCTION_PATTERN.sub(replace, text)
        path.write_text('#include "doraemon_recomp_hooks.h"\n' + text, encoding="utf-8", newline="\n")
    if function_fingerprint(source) != manifest["result"]:
        raise RuntimeError("Final game functions do not match the recorded PC version.")
    # Symbol input registers four zero-sized runtime symbols that ELF input
    # only used for direct calls. Preserve the original indirect-call map.
    registration = json.loads((PROJECT_ROOT / "recomp/registration.json").read_text(encoding="utf-8"))
    table_path = source / "recomp_overlays.inl"
    table = table_path.read_text(encoding="utf-8")
    for entry in registration["direct_call_only"]:
        pattern = (r"^    \{ \.func = " + re.escape(entry["name"]) +
                   r", \.offset = " + re.escape(entry["section_offset"]) +
                   r", \.rom_size = 0x00000000 \},\n")
        table, count = re.subn(pattern, "", table, flags=re.M)
        if count != 1:
            raise RuntimeError(f"Unexpected registration for {entry['name']}")
    table_path.write_text(table, encoding="utf-8", newline="\n")
    arrays = {}
    for match in re.finditer(r"static FuncEntry (\w+)\[\] = \{(.*?)\n\};", table, re.S):
        arrays[match[1]] = re.findall(r"\.func = (\w+), \.offset = (0x[0-9A-F]+), \.rom_size = (0x[0-9A-F]+)", match[2])
    entries = []
    for rom, array in re.findall(r"\.rom_addr = (0x[0-9A-F]+), [^\n]+?\.funcs = (\w+)", table):
        entries.extend(f"{name} {int(rom, 16) + int(offset, 16):08X} {int(size, 16):08X}\n"
                       for name, offset, size in arrays[array])
    if len(entries) != registration["entry_count"] or sha("".join(sorted(entries))) != registration["entries_sha256"]:
        raise RuntimeError("Indirect-call registrations differ from the working PC version.")
    receipt_path = output / "generation-receipt.json"
    receipt = json.loads(receipt_path.read_text(encoding="utf-8"))
    receipt.update(pc_patches_applied=True, patch_sha256=manifest["patch_sha256"],
                   patched_functions=manifest["result"])
    inputs = ["tools/generate_game.py", "tools/generate_recompiled_base.py", "tools/recomp_patch_utils.py",
              "tools/n64recomp/lock.json", "recomp/baseline.json", "recomp/doraemon.syms.toml",
              "recomp/doraemon.toml.in", "recomp/aspMain.toml.in", "recomp/registration.json",
              "recomp/patches/manifest.json", "recomp/patches/game.patch"]
    receipt["input_sha256"] = {name: hashlib.sha256((PROJECT_ROOT / name).read_bytes()).hexdigest() for name in inputs}
    receipt_path.write_text(json.dumps(receipt, indent=2) + "\n", encoding="utf-8")
    print(f"Applied {len(changed)} function patches; all {len(after)} function bodies match the PC version.")
    print(f"Configure CMake with -DDORAEMON_GENERATED_DIR={output}")


if __name__ == "__main__":
    try:
        main()
    except (OSError, RuntimeError, subprocess.CalledProcessError) as error:
        print(f"Game generation failed: {error}", file=sys.stderr)
        sys.exit(1)
