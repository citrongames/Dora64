import argparse
import json
import re
from dataclasses import dataclass
from pathlib import Path


PROJECT_ROOT = Path(__file__).resolve().parents[1]
DEFAULT_SOURCE_DIRECTORY = PROJECT_ROOT / "build-tools" / "game" / "RecompiledFuncs"
DEFAULT_LOCALIZATION_DIRECTORY = PROJECT_ROOT / "assets" / "localization"
DEFAULT_OUTPUT = DEFAULT_LOCALIZATION_DIRECTORY / "dialogue_reference_source.json"

GLOBAL_DIALOGUE_MESSAGE_POINTER = 0x800E6B24

ASSEMBLY_COMMENT = re.compile(
    r"^\s*// 0x([0-9A-Fa-f]+):\s+(\w+)\s*(.*?)\s*$"
)
FUNCTION_START = re.compile(r"^RECOMP_FUNC void (\w+)\(")
MEMORY_OPERAND = re.compile(
    r"^([+-]?(?:0x)?[0-9A-Fa-f]+)\((\$\w+)\)$"
)
MAIN_SECTION = re.compile(
    r"\.rom_addr = (0x[0-9A-Fa-f]+),\s*"
    r"\.ram_addr = (0x[0-9A-Fa-f]+).*\.funcs = section_\d+_main_funcs\b"
)


@dataclass(frozen=True)
class Expression:
    kind: str
    value: int | None = None
    origin: int | None = None


UNKNOWN = Expression("unknown")
ZERO = Expression("constant", 0)


def parse_immediate(value: str) -> int:
    sign = -1 if value.startswith("-") else 1
    unsigned = value.lstrip("+-")
    base = 16 if unsigned.lower().startswith("0x") else 10
    return sign * int(unsigned, base)


def add_expressions(left: Expression, right: Expression) -> Expression:
    if left.kind == "constant" and right.kind == "constant":
        return Expression("constant", (left.value + right.value) & 0xFFFFFFFF)

    if left.kind in ("constant", "affine") and right.kind == "unknown":
        return Expression("affine", left.value)
    if right.kind in ("constant", "affine") and left.kind == "unknown":
        return Expression("affine", right.value)

    if left.kind in ("constant", "affine") and right.kind in (
        "constant",
        "affine",
    ):
        return Expression("affine", (left.value + right.value) & 0xFFFFFFFF)

    # A pointer read from a structure remains dynamic even if a small fixed
    # field offset is added to it. Preserve the first constant load that led
    # to the dynamic value so the report still identifies the root state.
    dynamic = left if left.kind.startswith("load_") else right
    if dynamic.kind.startswith("load_"):
        origin = dynamic.origin
        if origin is None and dynamic.kind == "load_constant":
            origin = dynamic.value
        return Expression("dynamic", origin=origin)
    return UNKNOWN


def address_expression(base: Expression, offset: int) -> Expression:
    if base.kind == "constant":
        return Expression("constant", (base.value + offset) & 0xFFFFFFFF)
    if base.kind == "affine":
        return Expression("affine", (base.value + offset) & 0xFFFFFFFF)
    if base.kind == "load_constant":
        return Expression("dynamic", origin=base.value)
    if base.kind in ("load_affine", "load_dynamic", "dynamic"):
        return Expression("dynamic", origin=base.origin)
    return UNKNOWN


def loaded_expression(address: Expression) -> Expression:
    if address.kind == "constant":
        return Expression("load_constant", address.value)
    if address.kind == "affine":
        return Expression("load_affine", address.value)
    if address.kind == "dynamic":
        return Expression("load_dynamic", origin=address.origin)
    return Expression("load_dynamic")


def memory_address(
    operand: str, registers: dict[str, Expression]
) -> Expression:
    match = MEMORY_OPERAND.match(operand)
    if match is None:
        return UNKNOWN
    offset = parse_immediate(match.group(1))
    base = registers.get(match.group(2), UNKNOWN)
    return address_expression(base, offset)


def writes_first_register(operation: str) -> bool:
    if operation.startswith("b"):
        return False
    return operation not in {
        "j",
        "jal",
        "jr",
        "nop",
        "sb",
        "sh",
        "sw",
        "sd",
        "swc1",
        "sdc1",
    }


def scan_source_directory(source_directory: Path) -> list[dict[str, object]]:
    assignments: list[dict[str, object]] = []

    for path in sorted(source_directory.glob("funcs_*.c")):
        registers: dict[str, Expression] = {"$zero": ZERO}
        function_name: str | None = None

        for line_number, line in enumerate(
            path.read_text(encoding="utf-8", errors="replace").splitlines(), 1
        ):
            function_match = FUNCTION_START.match(line)
            if function_match is not None:
                function_name = function_match.group(1)
                registers = {"$zero": ZERO}

            instruction_match = ASSEMBLY_COMMENT.match(line)
            if instruction_match is None or function_name is None:
                continue

            pc = int(instruction_match.group(1), 16)
            operation = instruction_match.group(2)
            operand_text = instruction_match.group(3)
            operands = (
                [operand.strip() for operand in operand_text.split(",")]
                if operand_text
                else []
            )

            if operation == "lui" and len(operands) == 2:
                registers[operands[0]] = Expression(
                    "constant",
                    (parse_immediate(operands[1]) << 16) & 0xFFFFFFFF,
                )
            elif operation in ("addiu", "addi", "daddiu") and len(operands) == 3:
                registers[operands[0]] = add_expressions(
                    registers.get(operands[1], UNKNOWN),
                    Expression("constant", parse_immediate(operands[2])),
                )
            elif operation == "ori" and len(operands) == 3:
                source = registers.get(operands[1], UNKNOWN)
                registers[operands[0]] = (
                    Expression(
                        "constant",
                        (source.value | parse_immediate(operands[2]))
                        & 0xFFFFFFFF,
                    )
                    if source.kind == "constant"
                    else UNKNOWN
                )
            elif operation == "or" and len(operands) == 3:
                if operands[2] == "$zero":
                    registers[operands[0]] = registers.get(operands[1], UNKNOWN)
                elif operands[1] == "$zero":
                    registers[operands[0]] = registers.get(operands[2], UNKNOWN)
                else:
                    registers[operands[0]] = UNKNOWN
            elif operation == "move" and len(operands) == 2:
                registers[operands[0]] = registers.get(operands[1], UNKNOWN)
            elif operation in ("addu", "daddu") and len(operands) == 3:
                registers[operands[0]] = add_expressions(
                    registers.get(operands[1], UNKNOWN),
                    registers.get(operands[2], UNKNOWN),
                )
            elif operation in ("lw", "lwu", "lh", "lhu", "lb", "lbu") and len(
                operands
            ) == 2:
                registers[operands[0]] = loaded_expression(
                    memory_address(operands[1], registers)
                )
            elif operation == "sw" and len(operands) == 2:
                target = memory_address(operands[1], registers)
                if target == Expression("constant", GLOBAL_DIALOGUE_MESSAGE_POINTER):
                    source = registers.get(operands[0], UNKNOWN)
                    assignments.append(
                        {
                            "file": path.name,
                            "line": line_number,
                            "function": function_name,
                            "instruction_address": f"0x{pc:08X}",
                            "source_register": operands[0],
                            "source_kind": source.kind,
                            "source_value": (
                                f"0x{source.value:08X}"
                                if source.value is not None
                                else None
                            ),
                            "dynamic_origin": (
                                f"0x{source.origin:08X}"
                                if source.origin is not None
                                else None
                            ),
                        }
                    )
            elif operands and operands[0].startswith("$") and writes_first_register(
                operation
            ):
                registers[operands[0]] = UNKNOWN

    # Delay-slot instructions can appear twice in generated C. The original
    # MIPS instruction address is the stable identity for this audit.
    unique: dict[tuple[str, str], dict[str, object]] = {}
    for assignment in assignments:
        key = (str(assignment["file"]), str(assignment["instruction_address"]))
        unique.setdefault(key, assignment)
    return list(unique.values())


def read_catalog_ids(path: Path, collection: str) -> dict[int, dict[str, object]]:
    if not path.exists():
        return {}
    document = json.loads(path.read_text(encoding="utf-8"))
    return {
        int(str(record["id"]), 16): record
        for record in document.get(collection, [])
        if isinstance(record, dict) and isinstance(record.get("id"), str)
    }


def read_main_rom_to_ram_delta(source_directory: Path) -> int:
    overlay_path = source_directory / "recomp_overlays.inl"
    for line in overlay_path.read_text(encoding="utf-8").splitlines():
        match = MAIN_SECTION.search(line)
        if match is not None:
            rom_address = int(match.group(1), 16)
            ram_address = int(match.group(2), 16)
            return (ram_address - rom_address) & 0xFFFFFFFF
    raise ValueError("main section ROM/RAM mapping was not found")


def attach_fixed_pointer_matches(
    assignments: list[dict[str, object]],
    localization_directory: Path,
    main_delta: int,
) -> list[str]:
    known = read_catalog_ids(
        localization_directory / "dialogue_source.json", "messages"
    )
    known.update(
        read_catalog_ids(
            localization_directory / "game_text_source.json", "records"
        )
    )
    unmatched: list[str] = []

    for assignment in assignments:
        if assignment["source_kind"] != "constant":
            continue
        pointer = int(str(assignment["source_value"]), 16)
        rom_offset = (pointer - main_delta) & 0xFFFFFFFF
        assignment["fixed_rom_offset"] = f"0x{rom_offset:08X}"
        record = known.get(rom_offset)
        if record is None:
            assignment["catalog_match"] = None
            unmatched.append(f"0x{rom_offset:08X}")
            continue
        assignment["catalog_match"] = {
            "id": record["id"],
            "region": record.get("region"),
            "category": record.get("category", "dialogue"),
        }
    return unmatched


def main() -> None:
    parser = argparse.ArgumentParser(
        description=(
            "Audit all direct writes to Doraemon's global dialogue message pointer."
        )
    )
    parser.add_argument(
        "--source-directory", type=Path, default=DEFAULT_SOURCE_DIRECTORY
    )
    parser.add_argument(
        "--localization-directory",
        type=Path,
        default=DEFAULT_LOCALIZATION_DIRECTORY,
    )
    parser.add_argument("--output", type=Path, default=DEFAULT_OUTPUT)
    arguments = parser.parse_args()

    assignments = scan_source_directory(arguments.source_directory)
    main_delta = read_main_rom_to_ram_delta(arguments.source_directory)
    unmatched = attach_fixed_pointer_matches(
        assignments, arguments.localization_directory, main_delta
    )

    source_kind_counts: dict[str, int] = {}
    for assignment in assignments:
        source_kind = str(assignment["source_kind"])
        source_kind_counts[source_kind] = source_kind_counts.get(source_kind, 0) + 1

    document = {
        "format_version": 1,
        "global_dialogue_message_pointer": (
            f"0x{GLOBAL_DIALOGUE_MESSAGE_POINTER:08X}"
        ),
        "main_rom_to_ram_delta": f"0x{main_delta:08X}",
        "assignment_count": len(assignments),
        "source_kind_counts": source_kind_counts,
        "assignments": assignments,
    }
    arguments.output.parent.mkdir(parents=True, exist_ok=True)
    arguments.output.write_text(
        json.dumps(document, ensure_ascii=False, indent=2) + "\n",
        encoding="utf-8",
    )

    print(f"Found {len(assignments)} direct dialogue pointer assignments")
    for source_kind, count in sorted(source_kind_counts.items()):
        print(f"  {source_kind}: {count}")
    print(arguments.output)

    if unmatched:
        raise SystemExit(
            "Fixed dialogue pointers missing from the extracted catalogs: "
            + ", ".join(unmatched)
        )


if __name__ == "__main__":
    main()
