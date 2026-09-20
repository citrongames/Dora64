"""Record reviewed edits to generated function bodies as small source patches."""

import argparse
import difflib
import json
from pathlib import Path
import sys
from generate_recompiled_base import PROJECT_ROOT, function_fingerprint, rsp_fingerprint
from recomp_patch_utils import read_functions, sha, apply_patch


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--base", required=True, type=Path, help="Verified unpatched generation directory")
    parser.add_argument("--edited", required=True, type=Path, help="Edited generation directory")
    parser.add_argument("--patch-dir", type=Path, default=PROJECT_ROOT / "recomp/patches")
    args = parser.parse_args()
    expected = json.loads((PROJECT_ROOT / "recomp/baseline.json").read_text(encoding="utf-8"))
    if function_fingerprint(args.base / "RecompiledFuncs") != expected["functions"]:
        raise RuntimeError("The base is not the pinned, unpatched generation.")
    before = read_functions(args.base / "RecompiledFuncs")
    after = read_functions(args.edited / "RecompiledFuncs")
    if before.keys() != after.keys():
        raise RuntimeError("Functions were added/removed. Put new native helper functions in src/.")
    for name in ("funcs.h", "lookup.cpp"):
        if (args.base / "RecompiledFuncs" / name).read_text(encoding="utf-8") != (args.edited / "RecompiledFuncs" / name).read_text(encoding="utf-8"):
            raise RuntimeError(f"Changes to {name} are not function patches; update generation metadata instead.")
    if rsp_fingerprint(args.edited / "aspMain.cpp") != expected["normalized_rsp_sha256"]:
        raise RuntimeError("RSP edits are not function patches; update the RSP configuration instead.")
    import re
    registration = json.loads((PROJECT_ROOT / "recomp/registration.json").read_text(encoding="utf-8"))
    expected_table = (args.base / "RecompiledFuncs/recomp_overlays.inl").read_text(encoding="utf-8")
    for entry in registration["direct_call_only"]:
        pattern = r"^    \{ \.func = " + re.escape(entry["name"]) + r", \.offset = " + re.escape(entry["section_offset"]) + r", \.rom_size = 0x00000000 \},\n"
        expected_table, count = re.subn(pattern, "", expected_table, flags=re.M)
        if count != 1:
            raise RuntimeError(f"Unexpected base registration for {entry['name']}")
    if (args.edited / "RecompiledFuncs/recomp_overlays.inl").read_text(encoding="utf-8") != expected_table:
        raise RuntimeError("Registration edits are not function patches; update recomp/registration.json instead.")
    # Only function bodies are recorded. Reject edits elsewhere rather than losing them.
    allowed = {'#include "recomp.h"', '#include "funcs.h"', '#include "doraemon_recomp_hooks.h"'}
    from generate_recompiled_base import FUNCTION_PATTERN
    for path in (args.edited / "RecompiledFuncs").glob("*.c"):
        text = path.read_text(encoding="utf-8")
        first = FUNCTION_PATTERN.search(text)
        if first is None or any(line.strip() and line.strip() not in allowed for line in text[:first.start()].splitlines()):
            raise RuntimeError(f"Unrecorded preamble changes in {path}; move helpers/includes to src/.")
    hunks, entries = [], {}
    for name in sorted(before):
        if before[name] == after[name]:
            continue
        hunks.extend(difflib.unified_diff(before[name].splitlines(True), after[name].splitlines(True),
                     fromfile=f"a/{name}.c", tofile=f"b/{name}.c", n=2))
        entries[name] = {"before_sha256": sha(before[name]), "after_sha256": sha(after[name])}
    patch = "".join(hunks)
    applied, changed = apply_patch(before, patch)
    if applied != after or changed != entries.keys():
        raise RuntimeError("Patch does not reproduce the edited functions exactly.")
    manifest = {"format": 1, "patch_sha256": sha(patch), "functions": entries,
                "result": function_fingerprint(args.edited / "RecompiledFuncs")}
    args.patch_dir.mkdir(parents=True, exist_ok=True)
    (args.patch_dir / "game.patch").write_text(patch, encoding="utf-8", newline="\n")
    (args.patch_dir / "manifest.json").write_text(json.dumps(manifest, indent=2) + "\n", encoding="utf-8")
    print(f"Recorded {len(entries)} function patches in {args.patch_dir}; exact replay verified.")


if __name__ == "__main__":
    try:
        main()
    except (OSError, RuntimeError) as error:
        print(f"Patch recording failed: {error}", file=sys.stderr)
        sys.exit(1)
