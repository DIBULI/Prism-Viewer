#!/usr/bin/env python3
"""Pixelate private camera previews before using screenshots in the manual."""

import argparse
import os
from pathlib import Path

from PIL import Image, ImageDraw, ImageFont


SCRIPT_DIR = Path(__file__).resolve().parent
DEFAULT_SOURCE = SCRIPT_DIR / "assets/private"
DEFAULT_OUTPUT = SCRIPT_DIR / "assets/screenshots/redacted"
FILES = (
    "02-camera-stream.jpeg",
    "03-camera-exposure.jpeg",
    "04-camera-metadata.jpeg",
)

# Four live preview viewports in the 1200 x 768 Viewer screenshots.
PREVIEW_BOXES = (
    (42, 255, 438, 465),
    (455, 255, 855, 465),
    (42, 507, 438, 717),
    (455, 507, 855, 717),
)


def resolve_label_font() -> str:
    configured = os.environ.get("PRISM_MANUAL_CJK_BOLD_FONT")
    candidates = (
        configured,
        "/System/Library/Fonts/STHeiti Medium.ttc",
        "/usr/share/fonts/opentype/noto/NotoSansCJK-Bold.ttc",
        "/usr/share/fonts/truetype/noto/NotoSansCJK-Bold.ttc",
    )
    for candidate in candidates:
        if candidate and Path(candidate).expanduser().is_file():
            return str(Path(candidate).expanduser())
    raise FileNotFoundError(
        "no CJK bold font found; set PRISM_MANUAL_CJK_BOLD_FONT"
    )


def pixelate(
    image: Image.Image,
    box: tuple[int, int, int, int],
    label_font: str,
) -> None:
    left, top, right, bottom = box
    crop = image.crop(box)
    small = crop.resize(
        (max(1, crop.width // 22), max(1, crop.height // 22)),
        Image.Resampling.BOX,
    )
    masked = small.resize(crop.size, Image.Resampling.NEAREST)
    image.paste(masked, box)

    draw = ImageDraw.Draw(image, "RGBA")
    label = "演示画面已打码"
    font = ImageFont.truetype(label_font, 15)
    bounds = draw.textbbox((0, 0), label, font=font)
    width = bounds[2] - bounds[0]
    height = bounds[3] - bounds[1]
    center_x = (left + right) // 2
    center_y = (top + bottom) // 2
    pad_x, pad_y = 12, 7
    label_box = (
        center_x - width // 2 - pad_x,
        center_y - height // 2 - pad_y,
        center_x + width // 2 + pad_x,
        center_y + height // 2 + pad_y,
    )
    draw.rounded_rectangle(label_box, radius=8, fill=(16, 24, 40, 205))
    draw.text(
        (center_x - width // 2, center_y - height // 2 - bounds[1]),
        label,
        font=font,
        fill=(255, 255, 255, 255),
    )


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(
        description="Pixelate the four camera preview panes in manual screenshots."
    )
    parser.add_argument(
        "--source-dir",
        type=Path,
        default=DEFAULT_SOURCE,
        help="directory containing the three private JPEG screenshots",
    )
    parser.add_argument(
        "--output-dir",
        type=Path,
        default=DEFAULT_OUTPUT,
        help="directory for the public redacted PNG files",
    )
    return parser.parse_args()


def main() -> None:
    args = parse_args()
    args.output_dir.mkdir(parents=True, exist_ok=True)
    label_font = resolve_label_font()
    for filename in FILES:
        source = args.source_dir / filename
        if not source.is_file():
            raise FileNotFoundError(
                f"private screenshot not found: {source}; see docs/manual/README.md"
            )
        image = Image.open(source).convert("RGB")
        if image.height != 768 or not 1190 <= image.width <= 1200:
            raise ValueError(f"unexpected screenshot size for {source}: {image.size}")
        for box in PREVIEW_BOXES:
            pixelate(image, box, label_font)
        target = args.output_dir / filename.replace(".jpeg", "-redacted.png")
        image.save(target, format="PNG", optimize=True)
        print(target)


if __name__ == "__main__":
    main()
