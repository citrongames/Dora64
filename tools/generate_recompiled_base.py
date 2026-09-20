"""Generate the original code baseline separately; PC patches are not applied yet."""

import argparse
import hashlib
import json
from pathlib import Path
import re
import subprocess
import sys
import tempfile


PROJECT_ROOT = Path(__file__).resolve().parents[1]
FUNCTION_PATTERN = re.compile(
    r"RECOMP_FUNC void (\w+)\([^\n]+\) \{.*?(?=^RECOMP_FUNC|\Z)", re.M | re.S
)


def digest(data):
    return hashlib.sha256(data).hexdigest()


def normalized_static_names(text):
    # ELF's .main was section 3; the ROM metadata's .main is section 1.
    return re.sub(r"\bstatic_[13]_([0-9A-F]{8})\b", r"static_MAIN_\1", text)


def function_fingerprint(directory):
    functions = {}
    for path in sorted(directory.glob("*.c")):
        for match in FUNCTION_PATTERN.finditer(path.read_text(encoding="utf-8")):
            name = normalized_static_names(match[1])
            if name in functions:
                raise RuntimeError(f"Duplicate generated function: {name}")
            functions[name] = normalized_static_names(match[0]).strip()
    canonical = "\n\n".join(functions[name] for name in sorted(functions))
    return {"count": len(functions), "sha256": digest(canonical.encode("utf-8"))}


def rsp_fingerprint(path):
    text = path.read_text(encoding="utf-8")
    # RSPRecomp iterates an unordered container for this dispatch table.
    # Each case is one unconditional goto; case order has no semantic effect.
    pattern = re.compile(
        r"(do_indirect_jump:\n    switch [^\n]+\n)"
        r"((?:        case 0x[0-9A-F]+: goto L_[0-9A-F]+;\n)+)(    })"
    )
    def sort_cases(match):
        lines = match[2].splitlines(keepends=True)
        if len(set(lines)) != len(lines):
            raise RuntimeError("Duplicate RSP dispatch case")
        return match[1] + "".join(sorted(lines)) + match[3]
    canonical, count = pattern.subn(sort_cases, text)
    if count != 1:
        raise RuntimeError("Unexpected RSP dispatch table format")
    return digest(canonical.encode("utf-8"))


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--rom", required=True, type=Path, help="Original Japanese .z64 ROM")
    parser.add_argument("--tools-dir", type=Path, default=PROJECT_ROOT / "build-tools/n64recomp")
    parser.add_argument("--output-dir", type=Path, help="New directory; existing paths are never overwritten")
    args = parser.parse_args()
    metadata = PROJECT_ROOT / "recomp"
    baseline = json.loads((metadata / "baseline.json").read_text(encoding="utf-8"))
    rom = args.rom.resolve(strict=True)
    if rom.stat().st_size != baseline["rom"]["size"] or digest(rom.read_bytes()) != baseline["rom"]["sha256"]:
        raise RuntimeError("Unsupported ROM. Use the original Japanese big-endian .z64 (8 MiB).")

    tool_lock = json.loads((PROJECT_ROOT / "tools/n64recomp/lock.json").read_text(encoding="utf-8"))
    receipt_path = args.tools_dir / "build-receipt.json"
    if not receipt_path.is_file():
        raise RuntimeError("Build the tools first: python3 tools/bootstrap_n64recomp.py")
    receipt = json.loads(receipt_path.read_text(encoding="utf-8"))
    if (receipt["upstream_commit"] != tool_lock["commit"] or
            receipt["patch_sha256"] != tool_lock["patch_sha256"]):
        raise RuntimeError("Tool receipt is out of date; rebuild the pinned tools in a new work directory.")
    for name in ("N64Recomp", "RSPRecomp"):
        entry = receipt["executables"][name]
        if digest(Path(entry["path"]).read_bytes()) != entry["sha256"]:
            raise RuntimeError(f"{name} does not match its build receipt.")

    if args.output_dir:
        output = args.output_dir.resolve()
        output.mkdir(parents=True, exist_ok=False)
    else:
        parent = PROJECT_ROOT / "build-tools"
        parent.mkdir(exist_ok=True)
        output = Path(tempfile.mkdtemp(prefix="recompiled-base-", dir=parent))
    print(f"Generating baseline in {output}", flush=True)
    values = {"ROM": rom, "SYMBOLS": metadata / "doraemon.syms.toml",
              "FUNCTIONS": output / "RecompiledFuncs", "RSP": output / "aspMain.cpp"}
    for name, template in (("N64Recomp", "doraemon.toml.in"), ("RSPRecomp", "aspMain.toml.in")):
        text = (metadata / template).read_text(encoding="utf-8")
        for key, path in values.items():
            text = text.replace("@" + key + "@", json.dumps(path.as_posix()))
        config = output / template.removesuffix(".in")
        config.write_text(text, encoding="utf-8")
        log = output / (name + ".log")
        with log.open("w", encoding="utf-8") as stream:
            result = subprocess.run([receipt["executables"][name]["path"], str(config)],
                                    cwd=output, stdout=stream, stderr=subprocess.STDOUT)
        if result.returncode:
            raise RuntimeError(f"{name} failed with exit code {result.returncode}; see {log}")

    actual = function_fingerprint(output / "RecompiledFuncs")
    rsp_hash = rsp_fingerprint(output / "aspMain.cpp")
    if actual != baseline["functions"] or rsp_hash != baseline["normalized_rsp_sha256"]:
        raise RuntimeError(f"Generated code differs from the recorded ELF/RSP baseline; inspect {output}")
    report = {"rom_sha256": baseline["rom"]["sha256"], "functions": actual,
              "normalized_rsp_sha256": rsp_hash, "tool_patch_sha256": tool_lock["patch_sha256"],
              "pc_patches_applied": False}
    (output / "generation-receipt.json").write_text(json.dumps(report, indent=2) + "\n", encoding="utf-8")
    print(f"Verified {actual['count']} function bodies and aspMain against the recorded baseline.")
    print("Original baseline only: do not replace the working game sources until PC patches are migrated.")


if __name__ == "__main__":
    try:
        main()
    except (OSError, RuntimeError, subprocess.CalledProcessError) as error:
        print(f"Generation failed: {error}", file=sys.stderr)
        sys.exit(1)
