import argparse
import json
import re
from collections import Counter
from pathlib import Path


PROJECT_ROOT = Path(__file__).resolve().parents[1]
DEFAULT_LOCALIZATION_DIRECTORY = PROJECT_ROOT / "assets" / "localization"
CONTROL_PATTERN = re.compile(r"\{[^}]+\}")


def load_json(path: Path) -> dict[str, object]:
    return json.loads(path.read_text(encoding="utf-8"))


def visible_text(text: str) -> str:
    return CONTROL_PATTERN.sub("", text)


def control_codes(text: str) -> Counter[str]:
    return Counter(CONTROL_PATTERN.findall(text))


def indexed_records(
    records: object, path: Path, errors: list[str]
) -> dict[str, dict[str, object]]:
    if not isinstance(records, list):
        errors.append(f"{path}: record collection is not an array")
        return {}

    indexed: dict[str, dict[str, object]] = {}
    for record in records:
        if not isinstance(record, dict) or not isinstance(record.get("id"), str):
            errors.append(f"{path}: record without a valid id")
            continue
        record_id = record["id"]
        if record_id in indexed:
            errors.append(f"{path}: duplicate id {record_id}")
            continue
        indexed[record_id] = record
    return indexed


def check_common_record(
    record_id: str,
    source: dict[str, object],
    translated: dict[str, object],
    glyphs: set[str],
    require_complete: bool,
    errors: list[str],
) -> str | None:
    translation = translated.get("translation")
    if not isinstance(translation, str):
        errors.append(f"{record_id}: translation is not a string")
        return None
    if not translation:
        if require_complete:
            errors.append(f"{record_id}: empty translation")
        return None

    source_text = source.get("text")
    if not isinstance(source_text, str):
        errors.append(f"{record_id}: source text is not a string")
        return None
    if control_codes(source_text) != control_codes(translation):
        errors.append(f"{record_id}: formatting controls do not match source")

    text = visible_text(translation)
    unknown = sorted({character for character in text if character != "\n" and character not in glyphs})
    if unknown:
        errors.append(f"{record_id}: unsupported glyphs {''.join(unknown)!r}")
    return text


def validate_catalog_pair(
    source_path: Path,
    translation_path: Path,
    source_collection: str,
    translation_collection: str,
    glyphs: set[str],
    require_complete: bool,
    dialogue: bool,
    font: dict[str, object],
) -> tuple[list[str], list[str], int, int]:
    errors: list[str] = []
    warnings: list[str] = []
    source_records = indexed_records(
        load_json(source_path).get(source_collection), source_path, errors
    )
    translated_records = indexed_records(
        load_json(translation_path).get(translation_collection),
        translation_path,
        errors,
    )

    missing = sorted(source_records.keys() - translated_records.keys())
    extra = sorted(translated_records.keys() - source_records.keys())
    for record_id in missing:
        errors.append(f"{record_id}: missing from {translation_path.name}")
    for record_id in extra:
        errors.append(f"{record_id}: not present in {source_path.name}")

    max_lines = int(font["max_lines"])
    max_line = int(font["max_characters_per_line"])
    max_total = int(font["max_visible_characters_per_message"])
    for record_id in sorted(source_records.keys() & translated_records.keys()):
        source = source_records[record_id]
        translated = translated_records[record_id]
        text = check_common_record(
            record_id,
            source,
            translated,
            glyphs,
            require_complete,
            errors,
        )
        if text is None:
            continue

        if dialogue:
            lines = text.split("\n")
            source_text = visible_text(str(source["text"]))
            for quote in ("「", "」"):
                if text.count(quote) != source_text.count(quote):
                    errors.append(
                        f"{record_id}: {quote} count does not match source"
                    )
            if len(lines) > max_lines:
                errors.append(f"{record_id}: {len(lines)} lines, maximum is {max_lines}")
            if sum(map(len, lines)) > max_total:
                errors.append(f"{record_id}: visible text exceeds {max_total} characters")
            for index, line in enumerate(lines):
                limit = max_line - 1 if index == len(lines) - 1 else max_line
                if len(line) > limit:
                    errors.append(
                        f"{record_id}: line {index + 1} has {len(line)} characters, maximum is {limit}"
                    )
        else:
            source_text = visible_text(str(source["text"]))
            if len(text) > max(3 * len(source_text), 3):
                warnings.append(
                    f"{record_id}: localized text has {len(text)} characters; "
                    f"3x source allowance is {3 * len(source_text)}"
                )
            source_category = source.get("category")
            if translated.get("category") != source_category:
                errors.append(f"{record_id}: category does not match source")

    return errors, warnings, len(source_records), len(translated_records)


def main() -> None:
    parser = argparse.ArgumentParser(
        description="Validate generated Doraemon localization catalogs."
    )
    parser.add_argument("--localization", type=Path, default=DEFAULT_LOCALIZATION_DIRECTORY)
    parser.add_argument("--language", default="ru")
    parser.add_argument("--require-complete", action="store_true")
    parser.add_argument("--strict-width", action="store_true")
    arguments = parser.parse_args()

    root = arguments.localization
    manifest = load_json(root / "languages.json")
    language = next(
        (
            entry
            for entry in manifest.get("languages", [])
            if isinstance(entry, dict) and entry.get("code") == arguments.language
        ),
        None,
    )
    if language is None or language.get("original", False):
        raise SystemExit(f"Unknown translated language: {arguments.language}")

    font = load_json(root / str(language["font"]))
    glyphs = set(font["glyphs"])
    checks = (
        (
            root / "dialogue_source.json",
            root / arguments.language / "dialogue.json",
            "messages",
            "messages",
            True,
        ),
        (
            root / "game_text_source.json",
            root / arguments.language / "game_text.json",
            "records",
            "records",
            False,
        ),
    )

    all_errors: list[str] = []
    all_warnings: list[str] = []
    for source_path, translation_path, source_key, translation_key, dialogue in checks:
        errors, warnings, source_count, translation_count = validate_catalog_pair(
            source_path,
            translation_path,
            source_key,
            translation_key,
            glyphs,
            arguments.require_complete,
            dialogue,
            font,
        )
        print(
            f"{translation_path.name}: {translation_count}/{source_count} records, "
            f"{len(errors)} errors, {len(warnings)} width warnings"
        )
        all_errors.extend(errors)
        all_warnings.extend(warnings)

    for warning in all_warnings:
        print(f"warning: {warning}")
    for error in all_errors:
        print(f"error: {error}")
    if all_errors or (arguments.strict_width and all_warnings):
        raise SystemExit(1)


if __name__ == "__main__":
    main()
