"""overlay_proto3.py -- preview of the achievements list in the typeface of the game's menus
(same logic as ts_overlay.c): Andrios (IMG/Andrios-Font.ttf) at 16 px in monochrome, drawn at 2x
as the game draws it; the ornaments around the chosen option come from IMG/TITLE/title.png (67,290) and
(83,290), 10x13; the button icon is cell 18*32+0 of fontsmall at 2x; the B BACK prompt goes where
the game puts its own "A BACK".

Usage: python -I tools/overlay_proto3.py <Andrios-Font.ttf> <title.png> <fontsmall.png> <screenshot.jpg> <output.png> [sel] [lang]
The game files come from your own OBB (game/, outside the repository).
"""
import sys
from PIL import Image, ImageDraw, ImageFont

sys.path.insert(0, __file__.rsplit("\\", 1)[0].rsplit("/", 1)[0])
from overlay_proto import NAMES  # noqa: E402

TEXTS = {
    "en": ("ACHIEVEMENTS", "UNLOCKED", "LOCKED", "BACK"),
    "es": ("LOGROS", "DESBLOQUEADO", "BLOQUEADO", "VOLVER"),
    "fr": ("SUCCÈS", "DÉBLOQUÉ", "VERROUILLÉ", "RETOUR"),
    "de": ("ERFOLGE", "FREIGESCHALTET", "GESPERRT", "ZURÜCK"),
}
VISIBLE, ROWH, TOP = 9, 32, 192
DIM_A, UNL_A = 0.35, 0.6


def main():
    ttf, title_png, sheet_png, cap, out = sys.argv[1:6]
    sel = int(sys.argv[6]) if len(sys.argv) > 6 else 1
    lang = sys.argv[7] if len(sys.argv) > 7 else "en"
    title, unlocked, locked, back = TEXTS[lang]
    font = ImageFont.truetype(ttf, 16)
    orn = Image.open(title_png).convert("RGBA")
    sheet = Image.open(sheet_png).convert("RGBA")
    img = Image.open(cap).convert("RGBA").resize((1280, 720))
    have = {0: "2026-11-18", 1: "2026-11-18"}

    def over(src, x, y, a=1.0):
        if a < 1.0:
            al = src.split()[3].point(lambda v: int(v * a))
            src = src.copy()
            src.putalpha(al)
        img.alpha_composite(src, (int(x), int(y)))

    def shade(x0, y0, x1, y1, a):
        over(Image.new("RGBA", (x1 - x0, y1 - y0), (0, 0, 0, int(255 * a))), x0, y0)

    # a string at 1x (cap top at y=0 of the bitmap), as ts_overlay.c's atlas does: baseline at the
    # cap height of 'V', each glyph at its left bearing, advance from the font
    cap_h = 7

    def render(s):
        w = int(font.getlength(s)) + 2
        im = Image.new("L", (w, 24), 0)  # 6 rows above the capitals for the accents
        d = ImageDraw.Draw(im)
        d.fontmode = "1"
        d.text((0, 6 + cap_h), s, font=font, fill=255, anchor="ls")
        return im

    def width(s):
        return int(font.getlength(s)) * 2

    def text(s, x, y, a=1.0):
        r = render(s)
        m = r.resize((r.width * 2, r.height * 2), Image.NEAREST)
        src = Image.new("RGBA", m.size, (255, 255, 255, 0))
        src.putalpha(m)
        over(src, x, y - 12, a)

    def centered(s, y, a=1.0):
        text(s, (1280 - width(s)) // 2, y, a)

    shade(0, 0, 1280, 720, 0.88)
    shade(84, 604, 420, 640, 1.0)  # the game's own first prompt row (title menu)
    centered(title, 96)
    centered(f"{len(have)} / {len(NAMES)}", 136, DIM_A)
    first = min(max(sel - VISIBLE // 2, 0), len(NAMES) - VISIBLE)
    for r in range(VISIBLE):
        i = first + r
        y = TOP + r * ROWH
        name = NAMES[i].upper()
        a = 1.0 if i == sel else (UNL_A if i in have else DIM_A)
        if (r == 0 and first > 0) or (r == VISIBLE - 1 and first + VISIBLE < len(NAMES)):
            a *= 0.45
        centered(name, y, a)
        if i == sel:
            w = width(name)
            x = (1280 - w) // 2
            lx = min(530, x - 40 - 20)
            rx = max(730, x + w + 40)
            o = orn.crop((67, 290, 77, 303)).resize((20, 26), Image.NEAREST)
            over(o, lx, y - 6)
            o = orn.crop((83, 290, 93, 303)).resize((20, 26), Image.NEAREST)
            over(o, rx, y - 6)
    status = f"{unlocked}  {have[sel]}" if sel in have else locked
    centered(status, TOP + VISIBLE * ROWH + 24, 1.0 if sel in have else DIM_A)
    icon = sheet.crop((0, 288, 16, 304)).resize((32, 32), Image.NEAREST)
    over(icon, 88, 640)
    text(back, 128, 646)
    img.convert("RGB").save(out)


if __name__ == "__main__":
    main()
