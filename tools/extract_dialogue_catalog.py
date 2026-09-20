import argparse
import json
import re
from pathlib import Path


PROJECT_ROOT = Path(__file__).resolve().parents[1]
DEFAULT_ROM = PROJECT_ROOT / "doraemon.n64.jp.z64"
DEFAULT_OUTPUT_DIRECTORY = PROJECT_ROOT / "assets" / "localization"

NEW_LINE = 0xFE
MESSAGE_END = 0xFF
CONTROL_NO_ARGUMENT = 0xF7
CONTROL_WITH_ARGUMENT = 0xF8

DIALOGUE_REGIONS = (
    ("dialogue_00124100", 0x00124100, 0x001258E4),
    ("dialogue_0021CCC0", 0x0021CCC0, 0x0021CFC4),
    ("dialogue_0022B3E0", 0x0022B3E0, 0x0022BE68),
    ("dialogue_0022C2C0", 0x0022C2C0, 0x0022CD68),
    ("dialogue_00238870", 0x00238870, 0x00238F70),
    ("dialogue_00244E60", 0x00244E60, 0x002451E0),
    ("dialogue_00252560", 0x00252560, 0x002527DC),
    ("dialogue_00263E50", 0x00263E50, 0x00263E94),
    ("dialogue_00263EB0", 0x00263EB0, 0x00264130),
    ("dialogue_0027D360", 0x0027D360, 0x0027D6E4),
    ("dialogue_0028F1B4", 0x0028F1B4, 0x0028F674),
    ("dialogue_002A1A10", 0x002A1A10, 0x002A1DC8),
    ("dialogue_002B5E20", 0x002B5E20, 0x002B603C),
    ("dialogue_002BFD10", 0x002BFD10, 0x002BFF50),
    ("dialogue_002C9B30", 0x002C9B30, 0x002C9B9C),
    ("dialogue_002CE9E0", 0x002CE9E0, 0x002CEB44),
    ("dialogue_002E0680", 0x002E0680, 0x002E0980),
    ("dialogue_002E8580", 0x002E8580, 0x002E9058),
    ("original_catalog", 0x002EE2F0, 0x002EF218),
)

# Text tables that use the same one-byte record prefix and four-byte alignment
# as dialogue messages, but are rendered by other game systems.
GAME_SERVICE_TEXT_REGIONS = (
    ("boot_messages", "system_message", 0x000ACBC0, 0x000ACC28, 2),
    ("save_and_introductions", "system_message", 0x0022B2F0, 0x0022B3AC, 9),
    ("file_menu", "menu", 0x003DF6E0, 0x003DF7F4, 13),
)

# Consecutive FF-terminated records. Their stable IDs start immediately after
# the previous record, but the visible text may follow zero-filled alignment
# bytes. Those bytes are omitted from raw_hex because the runtime tables point
# directly at the visible text (or at its one-byte service prefix).
GAME_PLAIN_TEXT_REGIONS = (
    ("item_descriptions", "item_description", 0x001236E1, 0x00123C0C, 32),
    ("item_notifications", "item_notification", 0x00123F64, 0x00123FEF, 9),
)

# Colored labels begin with F8, contain their palette argument, and end at FE.
# Zero padding between records is ignored inside these confirmed ranges.
GAME_COLORED_TEXT_REGIONS = (
    ("item_labels_and_names", "item_name", 0x00123CA2, 0x00123F64, 40),
)


def add_range(mapping: dict[int, str], start: int, text: str) -> None:
    for index, character in enumerate(text):
        mapping[start + index] = character


def build_japanese_mapping() -> dict[int, str]:
    mapping: dict[int, str] = {0x00: " "}
    add_range(mapping, 0x01, "0123456789")
    add_range(
        mapping,
        0x10,
        "あいうえおかきくけこさしすせそたちつてとなにぬねの"
        "はひふへほまみむめもやゆよらりるれろわをん",
    )
    add_range(mapping, 0x3E, "がぎぐげござじずぜぞだぢづでどばびぶべぼぱぴぷぺぽ")
    add_range(mapping, 0x57, "ぁぃぅぇぉゃゅょっ")
    add_range(
        mapping,
        0x60,
        "アイウエオカキクケコサシスセソタチツテトナニヌネノ"
        "ハヒフヘホマミムメモヤユヨラリルレロワヲン",
    )
    add_range(mapping, 0x8E, "ガギグゲゴザジズゼゾダヂヅデドバビブベボパピプペポ")
    add_range(mapping, 0xA7, "ァィゥェォャュョッ")
    mapping.update(
        {
            0xB0: "ー",
            0xB1: "。",
            0xB2: "、",
            0xB3: "＊",
            0xB4: "「",
            0xB5: "」",
            0xB6: "（",
            0xB7: "）",
            0xB8: "！",
            0xB9: "？",
            0xBA: "♪",
            0xBB: ":",
            0xBC: "'",
            0xBD: '"',
            0xC0: "太",
            0xC1: "夫",
            0xC2: "A",
            0xC3: "B",
            0xC4: "C",
            0xC5: "Z",
            0xC6: "L",
            0xC7: "R",
            0xC8: "S",
            0xC9: "T",
        }
    )
    return mapping


def decode_text(
    raw: bytes,
    mapping: dict[int, str],
    *,
    start_index: int = 0,
    terminator: int = MESSAGE_END,
) -> str:
    decoded: list[str] = []
    index = start_index
    while index < len(raw):
        value = raw[index]
        if value == terminator:
            break
        if value == NEW_LINE:
            decoded.append("\n")
            index += 1
            if index < len(raw) and raw[index] == 0x00:
                index += 1
            continue
        if value == CONTROL_NO_ARGUMENT:
            decoded.append("{F7}")
            index += 1
            continue
        if value == CONTROL_WITH_ARGUMENT:
            if index + 1 >= len(raw):
                raise ValueError("F8 control at the end of a message")
            decoded.append(f"{{F8:{raw[index + 1]:02X}}}")
            index += 2
            continue

        decoded.append(mapping.get(value, f"{{GLYPH:{value:02X}}}"))
        index += 1
    return "".join(decoded)


def decode_message(raw: bytes, mapping: dict[int, str]) -> str:
    # The first byte controls the dialogue voice/effect.
    return decode_text(raw, mapping, start_index=1)


def read_terminated_record(
    rom: bytes,
    start: int,
    terminator: int,
    *,
    end_limit: int | None = None,
) -> bytes:
    cursor = start
    limit = len(rom) if end_limit is None else min(end_limit, len(rom))
    while cursor < limit:
        value = rom[cursor]
        cursor += 1
        if value == terminator:
            return rom[start:cursor]
        if value == CONTROL_WITH_ARGUMENT:
            if cursor >= limit:
                break
            cursor += 1
    raise ValueError(f"unterminated text record at ROM 0x{start:08X}")


def extract_dialogue_region(
    rom: bytes,
    mapping: dict[int, str],
    region_name: str,
    region_start: int,
    region_end: int,
) -> list[dict[str, object]]:
    messages: list[dict[str, object]] = []
    offset = region_start

    while offset < region_end:
        start = offset
        encoded_text = read_terminated_record(
            rom, start + 1, MESSAGE_END, end_limit=region_end
        )
        raw = rom[start : start + 1 + len(encoded_text)]
        end = start + len(raw)
        messages.append(
            {
                "id": f"{start:08X}",
                "region": region_name,
                "rom_offset": f"0x{start:08X}",
                "service_byte": f"0x{raw[0]:02X}",
                "raw_hex": raw.hex(" ").upper(),
                "text": decode_message(raw, mapping),
            }
        )

        aligned_end = (end + 3) & ~3
        padding = rom[end:aligned_end]
        if any(padding):
            raise ValueError(
                f"non-zero alignment padding after message at ROM 0x{start:08X}"
            )
        offset = aligned_end

    if offset != region_end:
        raise ValueError(
            f"dialogue extraction ended at 0x{offset:08X}, "
            f"expected 0x{region_end:08X}"
        )
    return messages


def extract_messages(rom: bytes) -> list[dict[str, object]]:
    mapping = build_japanese_mapping()
    messages: list[dict[str, object]] = []
    for region_name, region_start, region_end in DIALOGUE_REGIONS:
        messages.extend(
            extract_dialogue_region(
                rom, mapping, region_name, region_start, region_end
            )
        )
    return messages


def extract_plain_text_region(
    rom: bytes,
    mapping: dict[int, str],
    region_name: str,
    category: str,
    region_start: int,
    region_end: int,
    expected_count: int,
) -> list[dict[str, object]]:
    records: list[dict[str, object]] = []
    offset = region_start
    while offset < region_end:
        record_offset = offset
        stored_raw = read_terminated_record(
            rom, offset, MESSAGE_END, end_limit=region_end
        )
        visible_index = 0
        while stored_raw[visible_index] == 0:
            visible_index += 1
        raw = stored_raw[visible_index:]
        text_offset = record_offset + visible_index
        records.append(
            {
                "id": f"{record_offset:08X}",
                "region": region_name,
                "category": category,
                "rom_offset": f"0x{record_offset:08X}",
                "text_offset": f"0x{text_offset:08X}",
                "terminator": f"0x{MESSAGE_END:02X}",
                "raw_hex": raw.hex(" ").upper(),
                "text": decode_text(raw, mapping),
            }
        )
        offset += len(stored_raw)

    if offset != region_end:
        raise ValueError(
            f"plain text extraction ended at 0x{offset:08X}, "
            f"expected 0x{region_end:08X}"
        )
    if len(records) != expected_count:
        raise ValueError(
            f"plain text region {region_name} produced {len(records)} records, "
            f"expected {expected_count}"
        )
    return records


def extract_colored_text_region(
    rom: bytes,
    mapping: dict[int, str],
    region_name: str,
    category: str,
    region_start: int,
    region_end: int,
    expected_count: int,
) -> list[dict[str, object]]:
    records: list[dict[str, object]] = []
    offset = region_start
    while offset < region_end:
        if rom[offset] != CONTROL_WITH_ARGUMENT:
            offset += 1
            continue

        raw = read_terminated_record(
            rom, offset, NEW_LINE, end_limit=region_end
        )
        if CONTROL_NO_ARGUMENT not in raw:
            offset += 1
            continue
        records.append(
            {
                "id": f"{offset:08X}",
                "region": region_name,
                "category": category,
                "rom_offset": f"0x{offset:08X}",
                "terminator": f"0x{NEW_LINE:02X}",
                "raw_hex": raw.hex(" ").upper(),
                "text": decode_text(raw, mapping, terminator=NEW_LINE),
            }
        )
        offset += len(raw)

    if len(records) != expected_count:
        raise ValueError(
            f"colored text region {region_name} produced {len(records)} records, "
            f"expected {expected_count}"
        )
    return records


def extract_game_text(rom: bytes) -> list[dict[str, object]]:
    mapping = build_japanese_mapping()
    records: list[dict[str, object]] = []
    for region_name, category, start, end, expected_count in (
        GAME_SERVICE_TEXT_REGIONS
    ):
        region_records = extract_dialogue_region(
            rom, mapping, region_name, start, end
        )
        if len(region_records) != expected_count:
            raise ValueError(
                f"service text region {region_name} produced "
                f"{len(region_records)} records, expected {expected_count}"
            )
        for record in region_records:
            record["category"] = category
        records.extend(region_records)

    for region_name, category, start, end, expected_count in (
        GAME_PLAIN_TEXT_REGIONS
    ):
        records.extend(
            extract_plain_text_region(
                rom,
                mapping,
                region_name,
                category,
                start,
                end,
                expected_count,
            )
        )

    for region_name, category, start, end, expected_count in (
        GAME_COLORED_TEXT_REGIONS
    ):
        records.extend(
            extract_colored_text_region(
                rom,
                mapping,
                region_name,
                category,
                start,
                end,
                expected_count,
            )
        )

    records.sort(key=lambda record: str(record["rom_offset"]))
    return records


def read_existing_translations(
    path: Path, collection_name: str
) -> dict[str, str]:
    if not path.exists():
        return {}
    document = json.loads(path.read_text(encoding="utf-8"))
    return {
        entry["id"]: entry.get("translation", "")
        for entry in document.get(collection_name, [])
        if isinstance(entry, dict) and isinstance(entry.get("id"), str)
    }


def write_translation_template(
    path: Path,
    language: str,
    messages: list[dict[str, object]],
    initial: dict[str, str],
) -> None:
    translations = read_existing_translations(path, "messages")
    for message_id, text in initial.items():
        translations.setdefault(message_id, text)

    document = {
        "format_version": 1,
        "language": language,
        "notes": (
            "Empty translations fall back to Japanese. "
            "New lines are written as \\n; formatting controls use {F7} and {F8:XX}."
        ),
        "messages": [
            {
                "id": message["id"],
                "translation": translations.get(str(message["id"]), ""),
            }
            for message in messages
        ],
    }
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(
        json.dumps(document, ensure_ascii=False, indent=2) + "\n",
        encoding="utf-8",
    )


def write_game_text_translation_template(
    path: Path,
    language: str,
    records: list[dict[str, object]],
) -> None:
    translations = read_existing_translations(path, "records")
    document = {
        "format_version": 1,
        "language": language,
        "notes": (
            "Other game text systems. Formatting controls use {F7} and "
            "{F8:XX}; Japanese source stays in the generated "
            "game_text_source.json."
        ),
        "records": [
            {
                "id": record["id"],
                "category": record["category"],
                "translation": translations.get(str(record["id"]), ""),
            }
            for record in records
        ],
    }
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(
        json.dumps(document, ensure_ascii=False, indent=2) + "\n",
        encoding="utf-8",
    )


def translation_language_codes(localization_directory: Path) -> list[str]:
    manifest_path = localization_directory / "languages.json"
    if not manifest_path.exists():
        return []

    manifest = json.loads(manifest_path.read_text(encoding="utf-8"))
    if manifest.get("format_version") != 1:
        raise ValueError(f"Unsupported localization manifest: {manifest_path}")

    entries = manifest.get("languages")
    if not isinstance(entries, list):
        raise ValueError(f"Invalid language list: {manifest_path}")

    codes: list[str] = []
    for entry in entries:
        if not isinstance(entry, dict) or entry.get("original", False):
            continue
        code = entry.get("code")
        if not isinstance(code, str) or re.fullmatch(r"[A-Za-z0-9_-]{1,16}", code) is None:
            raise ValueError(f"Invalid language code in {manifest_path}: {code!r}")
        if code in codes:
            raise ValueError(f"Duplicate language code in {manifest_path}: {code}")
        codes.append(code)
    return codes


def main() -> None:
    parser = argparse.ArgumentParser(
        description=(
            "Extract Doraemon text records and update translation templates."
        )
    )
    parser.add_argument("--rom", type=Path, default=DEFAULT_ROM)
    parser.add_argument("--output", type=Path, default=DEFAULT_OUTPUT_DIRECTORY)
    arguments = parser.parse_args()

    rom = arguments.rom.read_bytes()
    messages = extract_messages(rom)
    game_text = extract_game_text(rom)
    arguments.output.mkdir(parents=True, exist_ok=True)

    source_document = {
        "format_version": 1,
        "rom": arguments.rom.name,
        "regions": [
            {
                "name": name,
                "start": f"0x{start:08X}",
                "end": f"0x{end:08X}",
            }
            for name, start, end in DIALOGUE_REGIONS
        ],
        "message_count": len(messages),
        "messages": messages,
    }
    source_path = arguments.output / "dialogue_source.json"
    source_path.write_text(
        json.dumps(source_document, ensure_ascii=False, indent=2) + "\n",
        encoding="utf-8",
    )

    game_text_document = {
        "format_version": 1,
        "rom": arguments.rom.name,
        "regions": [
            {
                "name": name,
                "category": category,
                "format": region_format,
                "start": f"0x{start:08X}",
                "end": f"0x{end:08X}",
                "expected_count": expected_count,
            }
            for region_format, regions in (
                ("service_aligned_ff", GAME_SERVICE_TEXT_REGIONS),
                ("plain_packed_ff", GAME_PLAIN_TEXT_REGIONS),
                ("colored_f8_fe", GAME_COLORED_TEXT_REGIONS),
            )
            for name, category, start, end, expected_count in regions
        ],
        "record_count": len(game_text),
        "records": game_text,
    }
    game_text_path = arguments.output / "game_text_source.json"
    game_text_path.write_text(
        json.dumps(game_text_document, ensure_ascii=False, indent=2) + "\n",
        encoding="utf-8",
    )

    english_initial = {
        "002EE2F0": "「*****」",
        "002EE2FC": "「DAD.」",
        "002EE308": "「OH, CORONA!」",
        "002EE318": "「LAND/SEA KINGS\nDEMONS CAUGHT\nTHEM?!」",
    }
    initial_translations = {
        "en": english_initial,
    }
    for language_code in translation_language_codes(arguments.output):
        write_translation_template(
            arguments.output / language_code / "dialogue.json",
            language_code,
            messages,
            initial_translations.get(language_code, {}),
        )
        write_game_text_translation_template(
            arguments.output / language_code / "game_text.json",
            language_code,
            game_text,
        )

    print(f"Extracted {len(messages)} messages")
    print(source_path)
    print(f"Extracted {len(game_text)} additional text records")
    print(game_text_path)


if __name__ == "__main__":
    main()
