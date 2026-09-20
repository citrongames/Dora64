import argparse
import json
from pathlib import Path

from extract_dialogue_catalog import (
    CONTROL_NO_ARGUMENT,
    CONTROL_WITH_ARGUMENT,
    DEFAULT_ROM,
    MESSAGE_END,
    NEW_LINE,
    build_japanese_mapping,
    decode_text,
)


DEFAULT_OUTPUT = (
    Path(__file__).resolve().parents[1]
    / "assets"
    / "localization"
    / "discovered_text_source.json"
)
MAX_RECORD_SIZE = 192
MIN_DIALOGUE_CHAIN_LENGTH = 2


def is_japanese(character: str) -> bool:
    return (
        "ぁ" <= character <= "ん"
        or "ァ" <= character <= "ヶ"
        or "一" <= character <= "龯"
    )


def parse_candidate(
    rom: bytes,
    mapping: dict[int, str],
    start: int,
    *,
    text_offset: int,
    terminator: int,
) -> tuple[bytes, str] | None:
    cursor = start + text_offset
    limit = min(start + MAX_RECORD_SIZE, len(rom))
    previous = -1
    while cursor < limit:
        value = rom[cursor]
        cursor += 1
        if value == terminator:
            raw = rom[start:cursor]
            return raw, decode_text(
                raw,
                mapping,
                start_index=text_offset,
                terminator=terminator,
            )
        if value == CONTROL_WITH_ARGUMENT:
            if cursor >= limit:
                return None
            cursor += 1
        elif value in (CONTROL_NO_ARGUMENT, NEW_LINE):
            pass
        elif value not in mapping:
            return None
        elif value == 0 and previous != NEW_LINE:
            return None
        previous = value
    return None


def useful_text(text: str, minimum_japanese: int) -> bool:
    visible = [character for character in text if character not in "\n"]
    japanese_count = sum(is_japanese(character) for character in visible)
    return japanese_count >= minimum_japanese and len(visible) <= 100


def candidate_document(
    category: str,
    start: int,
    raw: bytes,
    text: str,
    *,
    text_offset: int,
    terminator: int,
) -> dict[str, object]:
    document: dict[str, object] = {
        "id": f"{start:08X}",
        "category": category,
        "rom_offset": f"0x{start:08X}",
        "terminator": f"0x{terminator:02X}",
        "raw_hex": raw.hex(" ").upper(),
        "text": text,
    }
    if text_offset:
        document["prefix_hex"] = raw[:text_offset].hex(" ").upper()
    return document


def discover_dialogues(
    rom: bytes, mapping: dict[int, str]
) -> list[dict[str, object]]:
    records: list[dict[str, object]] = []
    for start in range(0, len(rom) - 4, 4):
        if start > 0 and rom[start - 1] not in (0, MESSAGE_END):
            continue
        parsed = parse_candidate(
            rom,
            mapping,
            start,
            text_offset=1,
            terminator=MESSAGE_END,
        )
        if parsed is None:
            continue
        raw, text = parsed
        if not useful_text(text, 4):
            continue
        aligned_end = (start + len(raw) + 3) & ~3
        if any(rom[start + len(raw) : aligned_end]):
            continue
        encoded = raw[1:]
        if encoded[0] != 0xB4 and bytes((NEW_LINE, 0)) not in encoded:
            continue
        records.append(
            candidate_document(
                "dialogue_candidate",
                start,
                raw,
                text,
                text_offset=1,
                terminator=MESSAGE_END,
            )
        )
    return records


def parse_dialogue_record(
    rom: bytes, mapping: dict[int, str], start: int
) -> tuple[bytes, str, int] | None:
    """Parse one aligned record from a consecutive dialogue table.

    Dialogue windows may continue in a new record without another opening
    quote. A zero glyph at the beginning of such a continuation is a normal
    leading space, so this parser intentionally accepts it.
    """
    if start % 4:
        return None

    cursor = start + 1
    limit = min(start + MAX_RECORD_SIZE, len(rom))
    while cursor < limit:
        value = rom[cursor]
        cursor += 1
        if value == MESSAGE_END:
            raw = rom[start:cursor]
            text = decode_text(raw, mapping, start_index=1)
            visible = [character for character in text if character != "\n"]
            if not (2 <= len(visible) <= 100):
                return None
            aligned_end = (cursor + 3) & ~3
            if any(rom[cursor:aligned_end]):
                return None
            return raw, text, aligned_end
        if value == CONTROL_WITH_ARGUMENT:
            if cursor >= limit:
                return None
            cursor += 1
        elif value in (CONTROL_NO_ARGUMENT, NEW_LINE):
            pass
        elif value not in mapping:
            return None
    return None


def discover_dialogue_chains(
    rom: bytes, mapping: dict[int, str]
) -> list[dict[str, object]]:
    parsed: dict[int, tuple[bytes, str, int]] = {}
    for start in range(0, len(rom) - 4, 4):
        record = parse_dialogue_record(rom, mapping, start)
        if record is not None:
            parsed[start] = record

    incoming = {record[2] for record in parsed.values() if record[2] in parsed}
    chains: list[dict[str, object]] = []
    visited: set[int] = set()
    for start in sorted(parsed):
        if start in incoming or start in visited:
            continue
        offsets: list[int] = []
        texts: list[str] = []
        cursor = start
        while cursor in parsed and cursor not in visited:
            visited.add(cursor)
            offsets.append(cursor)
            _, text, cursor = parsed[cursor]
            texts.append(text)
        if len(offsets) < MIN_DIALOGUE_CHAIN_LENGTH:
            continue
        japanese_count = sum(
            is_japanese(character) for text in texts for character in text
        )
        if japanese_count < len(offsets) * 2:
            continue
        chains.append(
            {
                "start": f"0x{offsets[0]:08X}",
                "end": f"0x{cursor:08X}",
                "record_count": len(offsets),
                "record_offsets": [f"0x{offset:08X}" for offset in offsets],
                "texts": texts,
            }
        )
    return chains


def discover_plain_records(
    rom: bytes, mapping: dict[int, str]
) -> list[dict[str, object]]:
    records: list[dict[str, object]] = []
    for start in range(len(rom) - 4):
        if start > 0 and rom[start - 1] not in (0, MESSAGE_END):
            continue
        if start >= 2 and rom[start - 2 : start] == bytes((NEW_LINE, 0)):
            continue
        if start % 4 == 0 and rom[start + 1] == 0xB4:
            # This is a dialogue record with a one-byte service prefix.
            continue
        parsed = parse_candidate(
            rom,
            mapping,
            start,
            text_offset=0,
            terminator=MESSAGE_END,
        )
        if parsed is None:
            continue
        raw, text = parsed
        if not useful_text(text, 4):
            continue
        records.append(
            candidate_document(
                "plain_candidate",
                start,
                raw,
                text,
                text_offset=0,
                terminator=MESSAGE_END,
            )
        )
    return records


def discover_colored_names(
    rom: bytes, mapping: dict[int, str]
) -> list[dict[str, object]]:
    records: list[dict[str, object]] = []
    for start in range(len(rom) - 4):
        if rom[start] != CONTROL_WITH_ARGUMENT:
            continue
        parsed = parse_candidate(
            rom,
            mapping,
            start,
            text_offset=0,
            terminator=NEW_LINE,
        )
        if parsed is None:
            continue
        raw, text = parsed
        if CONTROL_NO_ARGUMENT not in raw or not useful_text(text, 3):
            continue
        records.append(
            candidate_document(
                "colored_name_candidate",
                start,
                raw,
                text,
                text_offset=0,
                terminator=NEW_LINE,
            )
        )
    return records


def deduplicate(records: list[dict[str, object]]) -> list[dict[str, object]]:
    by_range: dict[tuple[str, str], dict[str, object]] = {}
    for record in records:
        key = (str(record["rom_offset"]), str(record["raw_hex"]))
        previous = by_range.get(key)
        if previous is None or record["category"] == "dialogue_candidate":
            by_range[key] = record
    return sorted(by_range.values(), key=lambda record: str(record["rom_offset"]))


def main() -> None:
    parser = argparse.ArgumentParser(
        description="Discover Japanese text records using structural heuristics."
    )
    parser.add_argument("--rom", type=Path, default=DEFAULT_ROM)
    parser.add_argument("--output", type=Path, default=DEFAULT_OUTPUT)
    arguments = parser.parse_args()

    rom = arguments.rom.read_bytes()
    mapping = build_japanese_mapping()
    records = deduplicate(
        discover_dialogues(rom, mapping)
        + discover_plain_records(rom, mapping)
        + discover_colored_names(rom, mapping)
    )
    dialogue_chains = discover_dialogue_chains(rom, mapping)
    document = {
        "format_version": 1,
        "rom": arguments.rom.name,
        "record_count": len(records),
        "records": records,
        "dialogue_chain_count": len(dialogue_chains),
        "dialogue_chains": dialogue_chains,
    }
    arguments.output.parent.mkdir(parents=True, exist_ok=True)
    arguments.output.write_text(
        json.dumps(document, ensure_ascii=False, indent=2) + "\n",
        encoding="utf-8",
    )
    print(f"Discovered {len(records)} text candidates")
    print(f"Discovered {len(dialogue_chains)} consecutive dialogue chains")
    print(arguments.output)


if __name__ == "__main__":
    main()
