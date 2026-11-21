/* ts_glyphs.c -- Switch-style button icons.
 *
 * Titan Souls draws its button prompts as characters of its small font, a
 * 512x512 sheet (IMG/TITLE/fontsmall.png and per-device variants such as
 * fontsmall_shield.png) with a row of Xbox buttons at y=288 (and a dimmed
 * copy at y=416), 16x16 each: 0 A, 1 B, 2 X, 3 Y, 4 Back, 5 Guide, 6 Start,
 * 9 LT, 10 RT, ... There is no Switch set (the game is from 2015).
 *
 * So when that sheet is uploaded (glTexImage2D, ts_gl.c), a copy of it is
 * changed, pixel by pixel, from the player's own copy of the game -- nothing
 * of the game is shipped:
 *   A B X Y   the circle becomes the game's dark/mid greys (its border the
 *             darkest), the letter white: a Joy-Con's face button
 *   Back      the arrow is erased and a "-" drawn; Start: a "+"
 *   LT, RT    the letter is erased and "ZL" / "ZR" written (a 3x5 font)
 * and the same on the dimmed row (fill dark grey, letter grey). Prototype and
 * pictures: tools/glyphs_proto.py. The button the game means by "A" is the
 * Switch's A already (ts_input.c sends A as KEYCODE_BUTTON_A), so only the
 * pictures change. [controls] button_icons = original keeps the game's.
 *
 * The sheet is told by its pixels: 512x512 RGBA bytes, the Back cell's exact
 * palette and a green in the A cell (both variants have them), upright or
 * upside down (some engines flip images on upload). MIT.
 */
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include "dcr_config.h"
#include "ts.h"
#include "util.h"

#define GL_RGBA 0x1908
#define GL_UNSIGNED_BYTE 0x1401
#define SHEET 512

typedef uint32_t px_t; /* R,G,B,A bytes in memory: little-endian 0xAABBGGRR */
#define RGB(r, g, b) ((px_t)(r) | (px_t)(g) << 8 | (px_t)(b) << 16 | 0xff000000u)

static const px_t DARK = RGB(35, 39, 58), MID = RGB(72, 77, 93), LETTER = RGB(224, 215, 218),
                  DIM_LETTER = RGB(131, 123, 132), BACK_ARROW = RGB(191, 178, 188), GREEN = RGB(109, 201, 82);

/* 3x5 font, rows top to bottom, bit 2 = left */
static const uint8_t F_Z[5] = {7, 1, 2, 4, 7}, F_L[5] = {4, 4, 4, 4, 7}, F_R[5] = {6, 5, 6, 5, 5};

typedef struct {
  px_t *p;
  int flipped;
} Sheet;

static px_t *at(const Sheet *s, int x, int y) {
  return &s->p[(s->flipped ? SHEET - 1 - y : y) * SHEET + x];
}

static int lum(px_t c) { return (int)(c & 0xff) * 30 + (int)(c >> 8 & 0xff) * 59 + (int)(c >> 16 & 0xff) * 11; }
static int opaque(px_t c) { return (c >> 24) != 0; }

/* the distinct opaque colors of a cell, with counts */
typedef struct {
  px_t c[16];
  int n[16];
  int k;
} Pal;

static void palette(const Sheet *s, int cx, int y0, Pal *pal) {
  pal->k = 0;
  for (int y = y0; y < y0 + 16; y++)
    for (int x = cx * 16; x < cx * 16 + 16; x++) {
      px_t c = *at(s, x, y);
      if (!opaque(c))
        continue;
      int i = 0;
      while (i < pal->k && pal->c[i] != c)
        i++;
      if (i == pal->k) {
        if (pal->k == 16)
          continue;
        pal->c[i] = c;
        pal->n[i] = 0;
        pal->k++;
      }
      pal->n[i]++;
    }
}

static int pal_count(const Pal *p, px_t c) {
  for (int i = 0; i < p->k; i++)
    if (p->c[i] == c)
      return p->n[i];
  return 0;
}

static px_t pal_major(const Pal *p, px_t except) {
  int best = -1;
  for (int i = 0; i < p->k; i++)
    if (p->c[i] != except && (best < 0 || p->n[i] > p->n[best]))
      best = i;
  return best < 0 ? 0 : p->c[best];
}

static void face_button(const Sheet *s, int cx, int y0, int dim) {
  Pal pal;
  palette(s, cx, y0, &pal);
  if (!pal.k)
    return;
  if (dim) {
    px_t fill = pal_major(&pal, 0);
    for (int y = y0; y < y0 + 16; y++)
      for (int x = cx * 16; x < cx * 16 + 16; x++) {
        px_t *q = at(s, x, y);
        if (opaque(*q))
          *q = *q == fill ? DARK : DIM_LETTER;
      }
    return;
  }
  int xbox = pal_count(&pal, LETTER) > 0; /* white letter on a colored circle; else colored letter on grey */
  px_t border = 0;
  int border_l = 1 << 30;
  for (int i = 0; i < pal.k; i++) {
    int is_letter = xbox ? pal.c[i] == LETTER : (pal.c[i] != DARK && pal.c[i] != MID);
    if (!is_letter && lum(pal.c[i]) < border_l) {
      border_l = lum(pal.c[i]);
      border = pal.c[i];
    }
  }
  for (int y = y0; y < y0 + 16; y++)
    for (int x = cx * 16; x < cx * 16 + 16; x++) {
      px_t *q = at(s, x, y);
      if (!opaque(*q))
        continue;
      int is_letter = xbox ? *q == LETTER : (*q != DARK && *q != MID);
      *q = is_letter ? LETTER : *q == border ? DARK : MID;
    }
}

/* After face_button: the letters of two cells swapped. On the console the
 * game shows "X" for shooting and it is the Switch's Y that shoots, and "A"
 * for rolling and it is B (hardware 2026-11-18: the tutorial's "press X
 * to shoot"): its Android input reads KEYCODE_BUTTON_Y as its X and
 * BUTTON_B as its A. The Switch's buttons reach it by letter (ts_input.c),
 * so the pictures must change, not the input: the A cell shows the game's
 * own "B" letter, B shows "A", X shows "Y", Y shows "X". */
static void swap_letters(const Sheet *s, int ca, int cb, int y0, int dim) {
  px_t letter = dim ? DIM_LETTER : LETTER, under = dim ? DARK : MID;
  uint8_t ma[16][16], mb[16][16];
  for (int y = 0; y < 16; y++)
    for (int x = 0; x < 16; x++) {
      ma[y][x] = *at(s, ca * 16 + x, y0 + y) == letter;
      mb[y][x] = *at(s, cb * 16 + x, y0 + y) == letter;
    }
  for (int y = 0; y < 16; y++)
    for (int x = 0; x < 16; x++) {
      px_t *pa = at(s, ca * 16 + x, y0 + y), *pb = at(s, cb * 16 + x, y0 + y);
      if (ma[y][x])
        *pa = under;
      if (mb[y][x])
        *pb = under;
    }
  for (int y = 0; y < 16; y++)
    for (int x = 0; x < 16; x++) {
      if (mb[y][x] && opaque(*at(s, ca * 16 + x, y0 + y)))
        *at(s, ca * 16 + x, y0 + y) = letter;
      if (ma[y][x] && opaque(*at(s, cb * 16 + x, y0 + y)))
        *at(s, cb * 16 + x, y0 + y) = letter;
    }
}

static void symbol_button(const Sheet *s, int cx, int y0, int dim, int plus) {
  Pal pal;
  palette(s, cx, y0, &pal);
  if (!pal.k)
    return;
  px_t fill = pal_major(&pal, 0);
  px_t arrow = 0;
  for (int i = 0; i < pal.k; i++)
    if (pal.c[i] != fill && (dim || pal.c[i] != DARK))
      arrow = pal.c[i];
  if (!arrow)
    return;
  int x0 = 99, x1 = -1, y_0 = 9999, y_1 = -1;
  for (int y = y0; y < y0 + 16; y++)
    for (int x = cx * 16; x < cx * 16 + 16; x++) {
      px_t *q = at(s, x, y);
      if (*q == arrow)
        *q = fill;
      if (*q == fill) {
        x0 = x < x0 ? x : x0;
        x1 = x > x1 ? x : x1;
        y_0 = y < y_0 ? y : y_0;
        y_1 = y > y_1 ? y : y_1;
      }
    }
  if (x1 < 0)
    return;
  int mx = (x0 + x1) / 2, my = (y_0 + y_1) / 2;
  for (int d = -1; d <= 1; d++)
    *at(s, mx + d, my) = arrow;
  if (plus) {
    *at(s, mx, my - 1) = arrow;
    *at(s, mx, my + 1) = arrow;
  }
}

static void draw3x5(const Sheet *s, const uint8_t *g, int x, int y, px_t c) {
  for (int r = 0; r < 5; r++)
    for (int b = 0; b < 3; b++)
      if (g[r] >> (2 - b) & 1 && opaque(*at(s, x + b, y + r)))
        *at(s, x + b, y + r) = c;
}

static void trigger(const Sheet *s, int cx, int y0, int right) {
  Pal pal;
  palette(s, cx, y0, &pal);
  if (!pal_count(&pal, DIM_LETTER))
    return;
  px_t fill = pal_major(&pal, DIM_LETTER);
  int lx0 = 99, lx1 = -1, ly0 = 9999, bx0 = 99, bx1 = -1;
  for (int y = y0; y < y0 + 16; y++)
    for (int x = cx * 16; x < cx * 16 + 16; x++) {
      px_t *q = at(s, x, y);
      if (*q == DIM_LETTER) {
        *q = fill;
        lx0 = x < lx0 ? x : lx0;
        lx1 = x > lx1 ? x : lx1;
        ly0 = y < ly0 ? y : ly0;
      }
      if (opaque(*q)) {
        bx0 = x < bx0 ? x : bx0;
        bx1 = x > bx1 ? x : bx1;
      }
    }
  if (lx1 < 0)
    return;
  int x = (lx0 + lx1) / 2 - 3;
  if (x > bx1 - 6)
    x = bx1 - 6;
  if (x < bx0)
    x = bx0;
  draw3x5(s, F_Z, x, ly0, DIM_LETTER);
  draw3x5(s, right ? F_R : F_L, x + 4, ly0, DIM_LETTER);
}

/* is this the font sheet, and which way up? */
static int detect(Sheet *s) {
  for (int f = 0; f < 2; f++) {
    s->flipped = f;
    Pal back, a;
    palette(s, 4, 288, &back);
    palette(s, 0, 288, &a);
    if (pal_count(&back, MID) == 43 && pal_count(&back, DARK) == 16 && pal_count(&back, BACK_ARROW) == 9 &&
        pal_count(&a, GREEN) > 0)
      return 1;
  }
  return 0;
}

/* glTexImage2D's pixels: a patched copy to upload instead (free it after), or NULL */
void *ts_glyphs_patch(int level, int w, int h, unsigned fmt, unsigned type, const void *pixels) {
  if (level != 0 || w != SHEET || h != SHEET || fmt != GL_RGBA || type != GL_UNSIGNED_BYTE || !pixels)
    return NULL;
  Sheet s = {(px_t *)(uintptr_t)pixels, 0};
  if (!detect(&s))
    return NULL;
  /* the achievements' notices are set in this font (ts_overlay.c) */
  if (!dcr_config()->switch_icons) {
    ts_font_captured(pixels, s.flipped);
    return NULL;
  }
  px_t *copy = malloc((size_t)SHEET * SHEET * 4);
  if (!copy) {
    ts_font_captured(pixels, s.flipped);
    return NULL;
  }
  memcpy(copy, pixels, (size_t)SHEET * SHEET * 4);
  s.p = copy;
  for (int dim = 0; dim < 2; dim++) {
    int y0 = dim ? 416 : 288;
    for (int c = 0; c < 4; c++)
      face_button(&s, c, y0, dim);
    swap_letters(&s, 0, 1, y0, dim); /* A <-> B */
    swap_letters(&s, 2, 3, y0, dim); /* X <-> Y */
    symbol_button(&s, 4, y0, dim, 0);
    symbol_button(&s, 6, y0, dim, 1);
    trigger(&s, 9, y0, 0);
    trigger(&s, 10, y0, 1);
  }
  static int logged;
  if (logged++ < 4)
    debugPrintf("[glyphs] the font sheet (%s): Switch-style button icons drawn\n",
                s.flipped ? "uploaded upside down" : "upright");
  ts_font_captured(copy, s.flipped);
  return copy;
}
