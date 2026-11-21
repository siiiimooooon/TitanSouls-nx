"""switchpad_proto.py -- prototype of the Switch drawing that replaces the Google gamepad
in the controls screen (the same as ts_switchpad.c does). Original line art.

Usage: python -I tools/switchpad_proto.py <Google_Gamepad_XX.png> <output.png> [welcome]
"""
import sys
from PIL import Image

GREY, WHITE = (155, 155, 155, 255), (255, 255, 255, 255)


class Canvas:
    def __init__(self, im):
        self.im, self.p = im, im.load()

    def put(self, x, y, c):
        if 0 <= x < 480 and 0 <= y < 360:
            self.p[x, y] = c

    def line(self, x0, y0, x1, y1, c):
        dx, dy = abs(x1 - x0), -abs(y1 - y0)
        sx, sy = (1 if x0 < x1 else -1), (1 if y0 < y1 else -1)
        err = dx + dy
        while True:
            self.put(x0, y0, c)
            if x0 == x1 and y0 == y1:
                return
            e2 = 2 * err
            if e2 >= dy:
                err += dy
                x0 += sx
            if e2 <= dx:
                err += dx
                y0 += sy

    def circle(self, cx, cy, r, c):
        x, y, d = r, 0, 1 - r
        while x >= y:
            for a, b in ((x, y), (y, x), (-y, x), (-x, y), (-x, -y), (-y, -x), (y, -x), (x, -y)):
                self.put(cx + a, cy + b, c)
            y += 1
            if d < 0:
                d += 2 * y + 1
            else:
                x -= 1
                d += 2 * (y - x) + 1

    def arc_quadrant(self, cx, cy, r, qx, qy, c):
        x, y, d = r, 0, 1 - r
        while x >= y:
            for a, b in ((x, y), (y, x)):
                self.put(cx + qx * a, cy + qy * b, c)
            y += 1
            if d < 0:
                d += 2 * y + 1
            else:
                x -= 1
                d += 2 * (y - x) + 1

    def round_rect(self, x0, y0, x1, y1, r, c):
        self.line(x0 + r, y0, x1 - r, y0, c)
        self.line(x0 + r, y1, x1 - r, y1, c)
        self.line(x0, y0 + r, x0, y1 - r, c)
        self.line(x1, y0 + r, x1, y1 - r, c)
        self.arc_quadrant(x0 + r, y0 + r, r, -1, -1, c)
        self.arc_quadrant(x1 - r, y0 + r, r, 1, -1, c)
        self.arc_quadrant(x0 + r, y1 - r, r, -1, 1, c)
        self.arc_quadrant(x1 - r, y1 - r, r, 1, 1, c)


def draw_lite(cv, leaders, pad=GREY):
    # body: a Switch Lite from the front (like the BotW reference), very rounded ends
    cv.round_rect(142, 118, 338, 202, 30, pad)
    # screen: bezel and screen
    cv.round_rect(184, 123, 296, 197, 3, pad)
    cv.line(190, 129, 290, 129, pad)
    cv.line(190, 187, 290, 187, pad)
    cv.line(190, 129, 190, 187, pad)
    cv.line(290, 129, 290, 187, pad)
    # left: stick (base and cap), D-pad, minus
    cv.circle(163, 141, 9, pad)
    cv.circle(163, 141, 5, pad)
    pts = [(-3, -10), (3, -10), (3, -3), (10, -3), (10, 3), (3, 3), (3, 10), (-3, 10), (-3, 3), (-10, 3), (-10, -3), (-3, -3)]
    for i in range(len(pts)):
        a, b = pts[i], pts[(i + 1) % len(pts)]
        cv.line(163 + a[0], 173 + a[1], 163 + b[0], 173 + b[1], pad)
    cv.line(173, 124, 176, 124, pad)
    # right: X top, Y left, A right, B bottom; stick; plus
    for bx, by in ((317, 132), (308, 141), (326, 141), (317, 150)):
        cv.circle(bx, by, 3, pad)
    cv.circle(317, 173, 9, pad)
    cv.circle(317, 173, 5, pad)
    cv.line(304, 124, 307, 124, pad)
    cv.line(305, 123, 305, 126, pad)
    cv.line(306, 123, 306, 126, pad)
    if leaders:
        # MOVE -> left stick and D-pad (right-angled lines)
        cv.line(128, 127, 134, 127, WHITE)
        cv.line(134, 127, 134, 173, WHITE)
        cv.line(134, 141, 153, 141, WHITE)
        cv.line(134, 173, 152, 173, WHITE)
        # SHOOT -> X and Y
        cv.line(351, 115, 308, 115, WHITE)
        cv.line(308, 115, 308, 137, WHITE)
        cv.line(317, 115, 317, 128, WHITE)
        # ROLL / RUN -> A and B
        cv.line(351, 140, 344, 140, WHITE)
        cv.line(344, 140, 344, 158, WHITE)
        cv.line(344, 141, 330, 141, WHITE)
        cv.line(344, 158, 317, 158, WHITE)
        cv.line(317, 158, 317, 154, WHITE)


def convert(im, welcome):
    p = im.load()
    for y in range(360):
        for x in range(480):
            if not p[x, y][3]:
                continue
            keep = (y >= 240) if welcome else (x <= 125 or x >= 354)
            if not keep:
                p[x, y] = (0, 0, 0, 0)
    pad = GREY if not welcome else (250, 252, 249, 255)
    draw_lite(Canvas(im), not welcome, pad)


def main():
    im = Image.open(sys.argv[1]).convert("RGBA")
    before = im.copy()
    convert(im, len(sys.argv) > 3)
    out = Image.new("RGB", (960, 360), (24, 34, 30))
    for i, img in enumerate((before, im)):
        bg = Image.new("RGBA", (480, 360), (24, 34, 30, 255))
        bg.alpha_composite(img)
        out.paste(bg.convert("RGB"), (i * 480, 0))
    out.resize((1920, 720), Image.NEAREST).save(sys.argv[2])


if __name__ == "__main__":
    main()


# ---------------------------------------------------------------- version 2: solid console
BODY, SCREEN, BTN, OUTLINE = (35, 39, 58, 235), (8, 8, 10, 255), (72, 77, 93, 255), (155, 155, 155, 255)


def fill_round_rect(cv, x0, y0, x1, y1, r, c):
    import math
    for y in range(y0, y1 + 1):
        dy = max(y0 + r - y, y - (y1 - r), 0)
        dx = r - int(math.sqrt(max(r * r - dy * dy, 0))) if dy else 0
        for x in range(x0 + dx, x1 - dx + 1):
            cv.put(x, y, c)


def fill_circle(cv, cx, cy, r, c):
    import math
    for dy in range(-r, r + 1):
        w = int(math.sqrt(r * r - dy * dy))
        for x in range(cx - w, cx + w + 1):
            cv.put(x, cy + dy, c)


def disc(cv, cx, cy, r, fill, edge):
    fill_circle(cv, cx, cy, r, fill)
    cv.circle(cx, cy, r, edge)


def draw_lite2(cv, leaders, edge=OUTLINE):
    fill_round_rect(cv, 146, 112, 334, 208, 36, BODY)
    cv.round_rect(146, 112, 334, 208, 36, edge)
    fill_round_rect(cv, 186, 117, 294, 203, 4, (20, 22, 32, 255))
    cv.round_rect(186, 117, 294, 203, 4, edge)
    fill_round_rect(cv, 192, 123, 288, 191, 0, SCREEN)
    # left
    disc(cv, 166, 138, 10, BTN, edge)
    disc(cv, 166, 138, 6, (98, 103, 120, 255), edge)
    arms = [(-3, -11), (3, -11), (3, -3), (11, -3), (11, 3), (3, 3), (3, 11), (-3, 11), (-3, 3), (-11, 3), (-11, -3), (-3, -3)]
    for y in range(176 - 11, 176 + 12):
        for x in range(166 - 11, 166 + 12):
            if abs(x - 166) <= 3 or abs(y - 176) <= 3:
                cv.put(x, y, BTN)
    for i in range(12):
        a, b = arms[i], arms[(i + 1) % 12]
        cv.line(166 + a[0], 176 + a[1], 166 + b[0], 176 + b[1], edge)
    cv.line(175, 118, 178, 118, edge)
    # right
    for bx, by in ((314, 130), (304, 140), (324, 140), (314, 150)):
        disc(cv, bx, by, 4, BTN, edge)
    disc(cv, 314, 176, 10, BTN, edge)
    disc(cv, 314, 176, 6, (98, 103, 120, 255), edge)
    cv.line(302, 118, 305, 118, edge)
    cv.line(303, 117, 303, 120, edge)
    cv.line(304, 117, 304, 120, edge)
    if not leaders:
        return
    # rings around the groups (like BotW's blue one) and right-angled leader lines
    cv.circle(166, 138, 13, WHITE)
    cv.circle(166, 176, 15, WHITE)
    cv.line(128, 127, 138, 127, WHITE)
    cv.line(138, 127, 138, 176, WHITE)
    cv.line(138, 138, 152, 138, WHITE)
    cv.line(138, 176, 150, 176, WHITE)
    # SHOOT -> X and Y (each with its ring)
    cv.circle(314, 130, 6, WHITE)
    cv.circle(304, 140, 6, WHITE)
    cv.line(351, 115, 304, 115, WHITE)
    cv.line(304, 115, 304, 133, WHITE)
    cv.line(314, 115, 314, 123, WHITE)
    # ROLL -> A and B
    cv.circle(324, 140, 6, WHITE)
    cv.circle(314, 150, 6, WHITE)
    cv.line(351, 140, 342, 140, WHITE)
    cv.line(342, 140, 331, 140, WHITE)
    cv.line(342, 140, 342, 160, WHITE)
    cv.line(342, 160, 314, 160, WHITE)
    cv.line(314, 160, 314, 157, WHITE)


if __name__ == "__main__" and "v2" in sys.argv:
    pass
