"""Function-level patch support, independent of generated C file grouping."""

import hashlib
import re
from generate_recompiled_base import FUNCTION_PATTERN, normalized_static_names


def sha(text):
    return hashlib.sha256(text.encode("utf-8")).hexdigest()


def read_functions(directory):
    functions = {}
    for path in sorted(directory.glob("*.c")):
        for match in FUNCTION_PATTERN.finditer(path.read_text(encoding="utf-8")):
            name = normalized_static_names(match[1])
            if name in functions:
                raise RuntimeError(f"Duplicate function {name}")
            functions[name] = normalized_static_names(match[0]).strip() + "\n"
    return functions


def apply_patch(functions, patch):
    """Apply exact unified hunks. No offset search or fuzzy context matching."""
    result = dict(functions)
    lines = patch.splitlines(keepends=True)
    index = 0
    changed = set()
    while index < len(lines):
        match = re.fullmatch(r"--- a/(\w+)\.c\n", lines[index])
        if not match:
            raise RuntimeError(f"Invalid patch header at line {index + 1}")
        name = match[1]
        index += 1
        if name in changed or name not in functions or index >= len(lines) or lines[index] != f"+++ b/{name}.c\n":
            raise RuntimeError(f"Invalid or duplicate patch target: {name}")
        changed.add(name)
        index += 1
        original = functions[name].splitlines(keepends=True)
        output, cursor, hunks = [], 0, 0
        while index < len(lines) and lines[index].startswith("@@"):
            hunk = re.fullmatch(r"@@ -(\d+)(?:,(\d+))? \+(\d+)(?:,(\d+))? @@\n", lines[index])
            if not hunk:
                raise RuntimeError(f"Invalid hunk for {name}")
            old_start, old_count, new_start, new_count = [int(x) if x is not None else 1 for x in hunk.groups()]
            start = old_start - (1 if old_count else 0)
            if start < cursor or start > len(original):
                raise RuntimeError(f"Overlapping or out-of-range hunk in {name}")
            output.extend(original[cursor:start])
            cursor = start
            if len(output) != new_start - (1 if new_count else 0):
                raise RuntimeError(f"Incorrect new hunk offset in {name}")
            index += 1
            removed = added = 0
            while index < len(lines) and not lines[index].startswith(("@@", "--- a/")):
                line = lines[index]
                if line[:1] not in (" ", "-", "+"):
                    raise RuntimeError(f"Invalid patch line in {name}")
                if line[0] in " -":
                    if cursor >= len(original) or original[cursor] != line[1:]:
                        raise RuntimeError(f"Patch context mismatch in {name}, line {cursor + 1}")
                    cursor += 1
                    removed += 1
                if line[0] in " +":
                    output.append(line[1:])
                    added += 1
                index += 1
            if (removed, added) != (old_count, new_count):
                raise RuntimeError(f"Patch hunk length mismatch in {name}")
            hunks += 1
        if not hunks:
            raise RuntimeError(f"No hunks for {name}")
        output.extend(original[cursor:])
        result[name] = "".join(output)
    return result, changed
