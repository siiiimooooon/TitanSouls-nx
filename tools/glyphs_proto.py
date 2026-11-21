"""glyphs_proto.py -- prototype of the button icon recoloring (the same as ts_glyphs.c does).

Usage: python -I tools/glyphs_proto.py <fontsmall*.png> <output.png>
Works on the game's font sheet extracted to game/ (not in the repository) and saves an enlarged
before/after view of the cells to review the design.
"""
import sys
from PIL import Image, ImageDraw

DARK, MID, LIGHT_LETTER, DIM_LETTER = (35, 39, 58), (72, 77, 93), (224, 215, 218), (131, 123, 132)
WHITE_HUE = (224, 215, 218)

# 3x5 mini font (rows top to bottom, bit 2 = left)
FONT = {
    "Z": [0b111, 0b001, 0b010, 0b100, 0b111],
    "L": [0b100, 0b100, 0b100, 0b100, 0b111],
    "R": [0b110, 0b101, 0b110, 0b101, 0b101],
}


def cell_pixels(px, cx, y0):
    return [(x, y) for y in range(y0, y0 + 16) for x in range(cx * 16, cx * 16 + 16) if px[x, y][3]]


def counts(px, pts):
    c = {}
    for p in pts:
        c[px[p][:3]] = c.get(px[p][:3], 0) + 1
    return c


def lum(c):
    return 0.3 * c[0] + 0.59 * c[1] + 0.11 * c[2]


def face_button(px, cx, y0, dim):
    pts = cell_pixels(px, cx, y0)
    cnt = counts(px, pts)
    if not cnt:
        return
    if dim:
        # two colors: the one with more pixels is the fill, the other the letter
        fill = max(cnt, key=cnt.get)
        for p in pts:
            px[p] = (DARK if px[p][:3] == fill else DIM_LETTER) + (255,)
        return
    if WHITE_HUE in cnt:  # Xbox variant: white letter, colored circle
        letter = {WHITE_HUE}
    else:  # Shield variant: grey circle, colored letter
        letter = {c for c in cnt if c not in (DARK, MID)}
    circle = [c for c in cnt if c not in letter]
    border = min(circle, key=lum)
    for p in pts:
        c = px[p][:3]
        px[p] = (LIGHT_LETTER if c in letter else DARK if c == border else MID) + (255,)


def swap_letters(px, ca, cb, y0, dim):
    letter = DIM_LETTER if dim else LIGHT_LETTER
    under = DARK if dim else MID
    ma = {(x, y) for y in range(16) for x in range(16) if px[ca * 16 + x, y0 + y][:3] == letter}
    mb = {(x, y) for y in range(16) for x in range(16) if px[cb * 16 + x, y0 + y][:3] == letter}
    for (x, y) in ma: px[ca * 16 + x, y0 + y] = under + (255,)
    for (x, y) in mb: px[cb * 16 + x, y0 + y] = under + (255,)
    for (x, y) in mb:
        if px[ca * 16 + x, y0 + y][3]: px[ca * 16 + x, y0 + y] = letter + (255,)
    for (x, y) in ma:
        if px[cb * 16 + x, y0 + y][3]: px[cb * 16 + x, y0 + y] = letter + (255,)


def symbol_button(px, cx, y0, dim, sym):
    """Back/Start: the arrow (the lighter minority color) is erased and - or + is drawn."""
    pts = cell_pixels(px, cx, y0)
    cnt = counts(px, pts)
    fill = max(cnt, key=cnt.get)
    arrow = [c for c in cnt if c != fill and c != DARK] if not dim else [c for c in cnt if c != fill]
    arrow_c = arrow[0] if arrow else LIGHT_LETTER
    xs = [p[0] for p in pts if px[p][:3] == fill]
    ys = [p[1] for p in pts if px[p][:3] == fill]
    for p in pts:
        if px[p][:3] == arrow_c:
            px[p] = fill + (255,)
    mx, my = (min(xs) + max(xs)) // 2, (min(ys) + max(ys)) // 2
    marks = [(mx - 1, my), (mx, my), (mx + 1, my)]
    if sym == "+":
        marks += [(mx, my - 1), (mx, my + 1)]
    for p in marks:
        px[p] = arrow_c + (255,)


def trigger(px, cx, y0, dim, text):
    """LT/RT: the letter (the minority color) is erased and ZL / ZR is written in its place."""
    pts = cell_pixels(px, cx, y0)
    cnt = counts(px, pts)
    letter_c = DIM_LETTER
    lpts = [p for p in pts if px[p][:3] == letter_c]
    body = [p for p in pts if px[p][:3] != letter_c]
    fill = max(counts(px, body), key=counts(px, body).get)
    for p in lpts:
        px[p] = fill + (255,)
    # the 7 px wide text, centered where the letter was, clipped to the trigger's body
    lx = [p[0] for p in lpts]
    ly = [p[1] for p in lpts]
    cxm, top = (min(lx) + max(lx)) // 2, min(ly)
    x0 = cxm - 3
    body_set = set(body)
    xs_body = [p[0] for p in body]
    x0 = max(min(xs_body), min(x0, max(xs_body) - 6))
    for i, ch in enumerate(text):
        for r, bits in enumerate(FONT[ch]):
            for b in range(3):
                if bits >> (2 - b) & 1:
                    q = (x0 + i * 4 + b, top + r)
                    if q in body_set or px[q][3]:
                        px[q] = letter_c + (255,)


def main():
    src, dst = sys.argv[1], sys.argv[2]
    im = Image.open(src).convert("RGBA")
    before = im.copy()
    px = im.load()
    for y0, dim in ((288, False), (416, True)):
        for c in range(4):
            face_button(px, c, y0, dim)
        swap_letters(px, 0, 1, y0, dim)
        swap_letters(px, 2, 3, y0, dim)
        symbol_button(px, 4, y0, dim, "-")
        symbol_button(px, 6, y0, dim, "+")
        trigger(px, 9, y0, dim, "ZL")
        trigger(px, 10, y0, dim, "ZR")
    S = 5
    cols = [0, 1, 2, 3, 4, 6, 9, 10]
    out = Image.new("RGB", (len(cols) * 16 * S, 4 * 16 * S + 30), (40, 40, 60))
    d = ImageDraw.Draw(out)
    for r, (img, y0) in enumerate(((before, 288), (im, 288), (before, 416), (im, 416))):
        for i, c in enumerate(cols):
            cell = img.crop((c * 16, y0, c * 16 + 16, y0 + 16)).resize((16 * S, 16 * S), Image.NEAREST)
            bg = Image.new("RGBA", cell.size, (40, 40, 60, 255))
            bg.alpha_composite(cell)
            out.paste(bg.convert("RGB"), (i * 16 * S, r * 16 * S + 30))
    d.text((4, 4), "before / after (bright), before / after (dimmed)", fill=(255, 255, 0))
    out.save(dst)


if __name__ == "__main__":
    main()
