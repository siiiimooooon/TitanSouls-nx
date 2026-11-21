"""overlay_proto.py -- preview of the achievements banner and list (same logic as ts_overlay.c, first version),
with the game's real font (extracted to game/, not in the repository), over a background screenshot.

Usage: python -I tools/overlay_proto.py <fontsmall.png> <screenshot.jpg> <output.png>
"""
import sys
from PIL import Image, ImageDraw

SCALE, GAP, SPACE = 1, 2, 6
DARK, MID, TXT, DIM = (35, 39, 58), (72, 77, 93), (224, 215, 218), (131, 123, 132)


def metrics(font):
    p = font.load()
    left, width = [0] * 1024, [0] * 1024
    for c in range(1024):
        cx, cy = (c % 32) * 16, (c // 32) * 16
        xs = [x for y in range(16) for x in range(16) if p[cx + x, cy + y][3]]
        if xs:
            left[c], width[c] = min(xs), max(xs) - min(xs) + 1
    width[32] = SPACE
    return left, width


def text_width(s, m):
    return sum((m[1][ord(ch)] + GAP) * SCALE for ch in s) - GAP * SCALE if s else 0


def text(img, font, m, s, x, y, color, alpha=1.0):
    for ch in s:
        c = ord(ch)
        w = m[1][c]
        if w and c != 32:
            cx, cy = (c % 32) * 16 + m[0][c], (c // 32) * 16
            g = font.crop((cx, cy, cx + w, cy + 16)).resize((w * SCALE, 16 * SCALE), Image.NEAREST)
            a = g.split()[3].point(lambda v: int(v * alpha))
            solid = Image.new("RGBA", g.size, color + (255,))
            solid.putalpha(a)
            img.alpha_composite(solid, (int(x), int(y)))
        x += (w + GAP) * SCALE


def rect(img, x0, y0, x1, y1, color, alpha):
    ov = Image.new("RGBA", img.size, (0, 0, 0, 0))
    ImageDraw.Draw(ov).rectangle((x0, y0, x1 - 1, y1 - 1), fill=color + (int(255 * alpha),))
    img.alpha_composite(ov)


NAMES = ["First Blood", "Out of the Frying Pan", "Into the Fire", "Titan Soul", "TRUTH", "Aerodynamics",
         "Iron Human", "Iron Titan", "Iron God", "Hard Bizkit", "New Game", "Slimeball", "Laser Eye Surgery",
         "Brain Freeze", "Short Back and Sides", "Drug Trial", "Demo Man", "Cash Money", "COME AT ME BRO",
         "Bomberman", "Leak Spin", "Beating the Yeti", "Dental Plan", "Titanic", "A Collision of Souls",
         "Shadow of the Colossus", "The Switch"]


def main():
    font = Image.open(sys.argv[1]).convert("RGBA")
    m = metrics(font)
    img = Image.open(sys.argv[2]).convert("RGBA").resize((1280, 720))
    have = {0: "2026-11-18", 1: "2026-11-18"}
    # --- list (ts_overlay.c draw_list)
    rect(img, 0, 0, 1280, 720, (0, 0, 0), 0.6)
    pw, rowh = 900, 26
    rows = (len(NAMES) + 1) // 2
    ph = 70 + rows * rowh + 50
    x0, y0 = (1280 - pw) // 2, (720 - ph) // 2
    rect(img, x0 - 2, y0 - 2, x0 + pw + 2, y0 + ph + 2, MID, 1)
    rect(img, x0, y0, x0 + pw, y0 + ph, DARK, 0.97)
    head = f"ACHIEVEMENTS   {len(have)} / {len(NAMES)}"
    text(img, font, m, head, x0 + (pw - text_width(head, m)) // 2, y0 + 20, TXT)
    rect(img, x0 + 24, y0 + 52, x0 + pw - 24, y0 + 53, MID, 1)
    for i, name in enumerate(NAMES):
        col, row = i % 2, i // 2
        x, y = x0 + 32 + col * (pw // 2), y0 + 64 + row * rowh
        if i in have:
            text(img, font, m, name, x, y, TXT)
            d = have[i]
            text(img, font, m, d, x + pw // 2 - 56 - text_width(d, m), y, DIM)
        else:
            text(img, font, m, name, x, y, DIM, 0.45)
    hint = "HOLD  -  TO CLOSE"
    text(img, font, m, hint, x0 + (pw - text_width(hint, m)) // 2, y0 + ph - 34, DIM)
    # --- banner (ts_overlay.c draw_notice)
    head, name = "ACHIEVEMENT UNLOCKED", "Shadow of the Colossus"
    w = max(text_width(head, m), text_width(name, m)) + 2 * 20
    bx0, by0 = (1280 - w) // 2, 20
    bx1, by1 = bx0 + w, by0 + 20 + 2 * 18 + 8
    rect(img, bx0 - 2, by0 - 2, bx1 + 2, by1 + 2, MID, 1)
    rect(img, bx0, by0, bx1, by1, DARK, 0.92)
    text(img, font, m, head, bx0 + (w - text_width(head, m)) // 2, by0 + 8, DIM)
    text(img, font, m, name, bx0 + (w - text_width(name, m)) // 2, by0 + 8 + 20, TXT)
    img.convert("RGB").save(sys.argv[3])


if __name__ == "__main__":
    main()
