import json
from pathlib import Path

from PIL import Image, ImageDraw, ImageFont


ATLAS_SIZE = 192
TILE_SIZE = 12
GUIDE_SCALE = 6


def build_mapping(config_path: Path) -> dict[int, str]:
    config = json.loads(config_path.read_text(encoding="utf-8"))
    return {
        int(code): character
        for character, code in config["glyphs"].items()
    }


def render_glyph(character: str, font: ImageFont.FreeTypeFont) -> Image.Image:
    mask = Image.new("L", (TILE_SIZE, TILE_SIZE), 0)
    draw = ImageDraw.Draw(mask)
    left, _top, right, _bottom = font.getbbox(character, anchor="ls")
    width = right - left
    x = (TILE_SIZE - width) // 2 - left
    draw.text((x, 10), character, font=font, fill=255, anchor="ls")
    return mask.point(lambda alpha: 255 if alpha >= 64 else 0)


def main() -> None:
    project_root = Path(__file__).resolve().parents[1]
    font_path = (
        project_root
        / "lib"
        / "rt64"
        / "src"
        / "contrib"
        / "imgui"
        / "misc"
        / "fonts"
        / "DroidSans.ttf"
    )
    output_directory = project_root / "assets" / "localization"
    output_directory.mkdir(parents=True, exist_ok=True)

    mapping = build_mapping(output_directory / "dialogue_font.json")
    font = ImageFont.truetype(str(font_path), 11)
    atlas_path = output_directory / "dialogue_font.bmp"
    if atlas_path.exists():
        # The checked-in atlas is hand drawn. Never replace it when refreshing
        # the labelled guide; only create the initial template when it is absent.
        with Image.open(atlas_path) as existing_atlas:
            atlas = existing_atlas.convert("RGB")
        if atlas.size != (ATLAS_SIZE, ATLAS_SIZE):
            raise ValueError(
                f"{atlas_path} must remain {ATLAS_SIZE}x{ATLAS_SIZE} pixels"
            )
    else:
        atlas = Image.new("L", (ATLAS_SIZE, ATLAS_SIZE), 0)
        for code, character in mapping.items():
            tile = render_glyph(character, font)
            x = (code & 0x0F) * TILE_SIZE
            y = (code >> 4) * TILE_SIZE
            atlas.paste(tile, (x, y))

        atlas.convert("RGB").save(atlas_path, format="BMP")

    guide_size = ATLAS_SIZE * GUIDE_SCALE
    guide = atlas.resize((guide_size, guide_size), Image.Resampling.NEAREST).convert("RGB")
    guide_draw = ImageDraw.Draw(guide)
    grid_step = TILE_SIZE * GUIDE_SCALE
    for coordinate in range(0, guide_size + 1, grid_step):
        guide_draw.line((coordinate, 0, coordinate, guide_size), fill=(0, 170, 255), width=1)
        guide_draw.line((0, coordinate, guide_size, coordinate), fill=(0, 170, 255), width=1)

    label_font = ImageFont.truetype(str(font_path), 11)
    labels = {code: f"{code:02X} {character}" for code, character in mapping.items()}
    labels.update({
        0x0B: "0B CURSOR 1",
        0x0C: "0C CURSOR 2",
        0x0D: "0D CURSOR 3",
        0x0E: "0E CURSOR 4",
        0xB4: "B4 OPEN QUOTE",
        0xB5: "B5 CLOSE QUOTE",
        0xF7: "F7 CONTROL",
        0xF8: "F8 CONTROL",
        0xFE: "FE NEWLINE",
        0xFF: "FF END",
    })
    for code, label in labels.items():
        x = (code & 0x0F) * grid_step + 3
        y = (code >> 4) * grid_step + 2
        box = guide_draw.textbbox((x, y), label, font=label_font)
        guide_draw.rectangle(box, fill=(0, 0, 0))
        guide_draw.text((x, y), label, font=label_font, fill=(0, 255, 255))

    guide_path = output_directory / "dialogue_font_guide.png"
    guide.save(guide_path, format="PNG")

    print(atlas_path)
    print(guide_path)


if __name__ == "__main__":
    main()
