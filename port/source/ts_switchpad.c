/* ts_switchpad.c -- a Switch in the controls screen.
 *
 * The CONTROLES screen and the "Gamepad Required" one at start-up show a
 * line drawing of Google's Nexus Player gamepad (IMG/TITLE/Google_Gamepad_EN,
 * FR, GE, IT, PO, SP and _welcome.png: 480x360, the pad grey (white in the
 * welcome one), the labels and their leader lines white). When one of them
 * is uploaded (glTexImage2D, ts_gl.c), a copy is changed: everything but the
 * labels is erased (they are the language's text: kept as they are, x <= 125
 * on the left, x >= 354 on the right, y >= 240 in the welcome image) and a
 * Switch is drawn in the pad's color with our own 1-pixel lines --
 * rounded body, screen, left stick, D-pad and -, the face buttons, right
 * stick and + -- and new leader lines: MOVE to the left stick, FIRE to Y,
 * ROLL / RUN to B (what those buttons do on the console; the Switch's
 * buttons reach the game by letter, ts_input.c). The first version (one
 * leader each, to X and A) was wrong and plain; this one follows a
 * reference, Breath of the Wild's controls screen.
 * Prototype and pictures: tools/switchpad_proto.py. Part of
 * [controls] button_icons = switch.
 *
 * Told by its pixels: 480x360 RGBA bytes, the pad's outline at (195,97),
 * (280,97), (316,223) and nothing at (240,110), (240,160) -- in all seven
 * images, upright or upside down. MIT.
 */
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include "dcr_config.h"
#include "util.h"

#define GL_RGBA 0x1908
#define GL_UNSIGNED_BYTE 0x1401
#define W 480
#define H 360

typedef uint32_t px_t;
#define WHITE 0xffffffffu

typedef struct {
  px_t *p;
  int flipped;
  px_t pad; /* the pad's color in this image */
} Img;

static px_t *at(Img *m, int x, int y) { return &m->p[(m->flipped ? H - 1 - y : y) * W + x]; }

static void put(Img *m, int x, int y, px_t c) {
  if (x >= 0 && x < W && y >= 0 && y < H)
    *at(m, x, y) = c;
}

static void line(Img *m, int x0, int y0, int x1, int y1, px_t c) {
  int dx = abs(x1 - x0), dy = -abs(y1 - y0), sx = x0 < x1 ? 1 : -1, sy = y0 < y1 ? 1 : -1, err = dx + dy;
  for (;;) {
    put(m, x0, y0, c);
    if (x0 == x1 && y0 == y1)
      return;
    int e2 = 2 * err;
    if (e2 >= dy) {
      err += dy;
      x0 += sx;
    }
    if (e2 <= dx) {
      err += dx;
      y0 += sy;
    }
  }
}

/* midpoint circle: all octants (qx = qy = 0) or one quadrant */
static void arc(Img *m, int cx, int cy, int r, int qx, int qy, px_t c) {
  int x = r, y = 0, d = 1 - r;
  while (x >= y) {
    if (!qx) {
      const int o[8][2] = {{x, y}, {y, x}, {-y, x}, {-x, y}, {-x, -y}, {-y, -x}, {y, -x}, {x, -y}};
      for (int i = 0; i < 8; i++)
        put(m, cx + o[i][0], cy + o[i][1], c);
    } else {
      put(m, cx + qx * x, cy + qy * y, c);
      put(m, cx + qx * y, cy + qy * x, c);
    }
    y++;
    if (d < 0) {
      d += 2 * y + 1;
    } else {
      x--;
      d += 2 * (y - x) + 1;
    }
  }
}

static void round_rect(Img *m, int x0, int y0, int x1, int y1, int r, px_t c) {
  line(m, x0 + r, y0, x1 - r, y0, c);
  line(m, x0 + r, y1, x1 - r, y1, c);
  line(m, x0, y0 + r, x0, y1 - r, c);
  line(m, x1, y0 + r, x1, y1 - r, c);
  arc(m, x0 + r, y0 + r, r, -1, -1, c);
  arc(m, x1 - r, y0 + r, r, 1, -1, c);
  arc(m, x0 + r, y1 - r, r, -1, 1, c);
  arc(m, x1 - r, y1 - r, r, 1, 1, c);
}

static int isqrt(int v) {
  int r = 0;
  while ((r + 1) * (r + 1) <= v)
    r++;
  return r;
}

static void fill_round_rect(Img *m, int x0, int y0, int x1, int y1, int r, px_t c) {
  for (int y = y0; y <= y1; y++) {
    int dy = y0 + r - y > 0 ? y0 + r - y : y - (y1 - r) > 0 ? y - (y1 - r) : 0;
    int dx = dy ? r - isqrt(r * r - dy * dy) : 0;
    for (int x = x0 + dx; x <= x1 - dx; x++)
      put(m, x, y, c);
  }
}

static void fill_circle(Img *m, int cx, int cy, int r, px_t c) {
  for (int dy = -r; dy <= r; dy++) {
    int w = isqrt(r * r - dy * dy);
    for (int x = cx - w; x <= cx + w; x++)
      put(m, x, cy + dy, c);
  }
}

static void disc(Img *m, int cx, int cy, int r, px_t fill, px_t edge) {
  fill_circle(m, cx, cy, r, fill);
  arc(m, cx, cy, r, 0, 0, edge);
}

/* the game's palette (its font's button greys) */
#define C(r, g, b, a) ((px_t)(r) | (px_t)(g) << 8 | (px_t)(b) << 16 | (px_t)(a) << 24)
static const px_t BODY = C(35, 39, 58, 235), BEZEL = C(20, 22, 32, 255), PANEL = C(8, 8, 10, 255),
                  BTN = C(72, 77, 93, 255), CAP = C(98, 103, 120, 255);

/* A Switch with its Joy-Con seen from the front, laid out like the
 * reference -- Breath of the Wild's controls screen -- and measured on it
 * (body 2.4 times as wide as tall, corners about a sixth of its height,
 * screen, rails, buttons at the same places), drawn in this game's palette
 * and 1-pixel lines: filled body, black screen, filled buttons, the stick
 * and the buttons the game uses ringed, with leader lines at right angles. (Earlier tries, outlines with made-up proportions,
 * were rightly called "cualquier cosa".) Prototype: tools/switchpad_proto.py. */
static void draw_lite(Img *m, int leaders) {
  px_t g = m->pad;
  fill_round_rect(m, 144, 119, 340, 201, 14, BODY);
  round_rect(m, 144, 119, 340, 201, 14, g);
  line(m, 175, 120, 175, 200, g); /* where the Joy-Con meet the console */
  line(m, 309, 120, 309, 200, g);
  fill_round_rect(m, 179, 123, 305, 197, 2, BEZEL);
  round_rect(m, 179, 123, 305, 197, 2, g);
  fill_round_rect(m, 187, 128, 297, 188, 0, PANEL);
  line(m, 190, 194, 196, 194, g); /* the speaker grilles */
  line(m, 288, 194, 294, 194, g);
  /* left Joy-Con: stick, the four directional buttons, minus, capture */
  disc(m, 160, 141, 7, BTN, g);
  disc(m, 160, 141, 4, CAP, g);
  disc(m, 160, 158, 3, BTN, g);
  disc(m, 154, 164, 3, BTN, g);
  disc(m, 166, 164, 3, BTN, g);
  disc(m, 160, 170, 3, BTN, g);
  line(m, 166, 125, 169, 125, g);
  for (int y = 180; y <= 183; y++)
    for (int x = 163; x <= 166; x++)
      put(m, x, y, (x == 163 || x == 166 || y == 180 || y == 183) ? g : BTN);
  /* right Joy-Con: X (top), Y (left), A (right), B (bottom), stick, plus, HOME */
  disc(m, 324, 135, 3, BTN, g);
  disc(m, 318, 141, 3, BTN, g);
  disc(m, 330, 141, 3, BTN, g);
  disc(m, 324, 147, 3, BTN, g);
  disc(m, 324, 164, 7, BTN, g);
  disc(m, 324, 164, 4, CAP, g);
  line(m, 315, 125, 318, 125, g);
  line(m, 316, 124, 316, 127, g);
  line(m, 317, 124, 317, 127, g);
  disc(m, 319, 181, 2, BTN, g);
  if (!leaders)
    return;
  /* What the buttons do, as tried on the console (hardware 2026-11-18): the
   * stick moves (the D-pad does not), Y shoots, B rolls / runs; X and A do
   * nothing in play. (data/control.xml lists more, but that is the PC
   * mapping, not the Android one.) Only those are marked. */
  arc(m, 160, 141, 10, 0, 0, WHITE);
  line(m, 128, 127, 136, 127, WHITE); /* MOVE -> left stick */
  line(m, 136, 127, 136, 141, WHITE);
  line(m, 136, 141, 149, 141, WHITE);
  arc(m, 318, 141, 5, 0, 0, WHITE); /* FIRE -> Y */
  line(m, 351, 115, 318, 115, WHITE);
  line(m, 318, 115, 318, 135, WHITE);
  arc(m, 324, 147, 5, 0, 0, WHITE); /* ROLL / RUN -> B */
  line(m, 351, 140, 345, 140, WHITE);
  line(m, 345, 140, 345, 147, WHITE);
  line(m, 345, 147, 330, 147, WHITE);
}

static int opaque_at(Img *m, int x, int y) { return (*at(m, x, y) >> 24) != 0; }

static int detect(Img *m) {
  for (int f = 0; f < 2; f++) {
    m->flipped = f;
    if (opaque_at(m, 195, 97) && opaque_at(m, 280, 97) && opaque_at(m, 316, 223) && !opaque_at(m, 240, 110) &&
        !opaque_at(m, 240, 160))
      return 1;
  }
  return 0;
}

void *ts_switchpad_patch(int level, int w, int h, unsigned fmt, unsigned type, const void *pixels) {
  if (!dcr_config()->switch_icons || level != 0 || w != W || h != H || fmt != GL_RGBA || type != GL_UNSIGNED_BYTE ||
      !pixels)
    return NULL;
  Img m = {(px_t *)(uintptr_t)pixels, 0, 0};
  if (!detect(&m))
    return NULL;
  px_t *copy = malloc((size_t)W * H * 4);
  if (!copy)
    return NULL;
  memcpy(copy, pixels, (size_t)W * H * 4);
  m.p = copy;
  m.pad = *at(&m, 195, 97);
  /* the welcome image: no labels at the left, its text at the bottom */
  int welcome = 1;
  for (int y = 120; y < 133 && welcome; y++)
    for (int x = 60; x <= 125; x++)
      if (opaque_at(&m, x, y)) {
        welcome = 0;
        break;
      }
  for (int y = 0; y < H; y++)
    for (int x = 0; x < W; x++) {
      int keep = welcome ? y >= 240 : (x <= 125 || x >= 354);
      if (!keep)
        *at(&m, x, y) = 0;
    }
  draw_lite(&m, !welcome);
  static int logged;
  if (logged++ < 8)
    debugPrintf("[glyphs] the %s gamepad picture: a Switch drawn (%s)\n", welcome ? "welcome" : "controls",
                m.flipped ? "upside down" : "upright");
  return copy;
}
