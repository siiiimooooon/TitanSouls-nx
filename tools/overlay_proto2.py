"""overlay_proto2.py -- preview of the achievements list in the style of the game's menus
(same logic as an earlier ts_overlay.c: no box, a centered upper-case list that scrolls, the chosen
one in white between > <, its state below, the BACK prompt at the bottom left with the game's icon).

Usage: python -I tools/overlay_proto2.py <fontsmall.png> <screenshot.jpg> <output.png> [selected]
"""
import sys
from PIL import Image

sys.path.insert(0, __file__.rsplit("\\", 1)[0].rsplit("/", 1)[0])
from overlay_proto import NAMES, metrics, rect, text, text_width  # noqa: E402

TXT, DIM = (224, 215, 218), (131, 123, 132)
VISIBLE, ROWH, TOP = 9, 34, 176


def cell(img, font, c, x, y):
    cx, cy = (c % 32) * 16, (c // 32) * 16
    img.alpha_composite(font.crop((cx, cy, cx + 16, cy + 16)), (x, y))


def main():
    font = Image.open(sys.argv[1]).convert("RGBA")
    m = metrics(font)
    img = Image.open(sys.argv[2]).convert("RGBA").resize((1280, 720))
    sel = int(sys.argv[4]) if len(sys.argv) > 4 else 1
    have = {0: "2026-11-18", 1: "2026-11-18"}
    rect(img, 0, 0, 1280, 720, (0, 0, 0), 0.88)

    def center(s, y, color, a=1.0):
        text(img, font, m, s, (1280 - text_width(s, m)) // 2, y, color, a)

    center("ACHIEVEMENTS", 96, TXT)
    center(f"{len(have)} / {len(NAMES)}", 122, DIM)
    first = min(max(sel - VISIBLE // 2, 0), len(NAMES) - VISIBLE)
    for r in range(VISIBLE):
        i = first + r
        y = TOP + r * ROWH
        name = NAMES[i].upper()
        if i == sel:
            w = text_width(name, m)
            x = (1280 - w) // 2
            text(img, font, m, name, x, y, TXT)
            text(img, font, m, ">", x - 16 - text_width(">", m), y, TXT)
            text(img, font, m, "<", x + w + 16, y, TXT)
        else:
            center(name, y, DIM, 1.0 if i in have else 0.45)
    if first > 0:
        center("^", TOP - 26, DIM)
    if first + VISIBLE < len(NAMES):
        center("v", TOP + VISIBLE * ROWH - 4, DIM)
    status = f"UNLOCKED   {have[sel]}" if sel in have else "LOCKED"
    center(status, TOP + VISIBLE * ROWH + 30, TXT if sel in have else DIM, 1.0 if sel in have else 0.8)
    cell(img, font, 18 * 32 + 1, 92, 645)
    text(img, font, m, "BACK", 128, 647, TXT)
    img.convert("RGB").save(sys.argv[3])


if __name__ == "__main__":
    main()
