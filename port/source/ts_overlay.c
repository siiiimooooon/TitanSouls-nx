/* ts_overlay.c -- the achievements' notice and list, drawn over the game.
 *
 * Drawn just before each frame is presented (the runtime's dcr_present_hook,
 * gl_mesa.c), in GLES 2 on the game's own context, and every bit of GL state
 * touched is saved first and put back after, so the game's next frame finds
 * its state as it left it. Laid out in 1280x720 units whatever the window.
 *
 * It looks like the game's own menus because it is drawn with their pieces:
 *   - the text in their typeface, IMG/Andrios-Font.ttf from the OBB, at
 *     16 px in monochrome and drawn at 2x, as the game draws it (matched
 *     pixel for pixel on a capture of the title menu's "SELECCIONAR" and
 *     "VOLVER"). It is rasterized once by the game's own FreeType, which
 *     libTestSuite.so exports, into an atlas of Latin-1 (the texts below
 *     are Latin-1);
 *   - the ornaments either side of the chosen line, which are in
 *     IMG/TITLE/title.png (10x13 at (67,290) and (83,290)): copied out of
 *     the pixels when the game uploads that texture (recognised by a hash
 *     of that spot), so they are there wherever the list is opened;
 *   - the button icon from the game's font sheet (IMG/TITLE/fontsmall*.png,
 *     ts_glyphs.c finds it when it is uploaded: the Switch-style icons when
 *     those are on), at 2x like the game's prompts.
 * If the typeface cannot be made, the font sheet's letters stand in (1x).
 * Laid out on a preview first: tools/overlay_proto3.py.
 *
 *   notice  "LOGRO DESBLOQUEADO" and the name, top centre, 4 s (fade in/out),
 *           queued when several come at once
 *   list    all 27, like the title menu: the scene darkened, the chosen line
 *           white between the ornaments, unlocked ones lighter than locked,
 *           the chosen one's state under the list and the "back" prompt where
 *           the game puts its own; opened from the title menu or by holding -
 *           (ts_input.c, which keeps the game's input away while it is open)
 * MIT.
 */
#include <GLES2/gl2.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <switch.h>

#include "dcr_path.h"
#include "rt_window.h"
#include "ts.h"
#include "util.h"

extern void (*dcr_present_hook)(void); /* gl_mesa.c */

#define SW 1280 /* the layout's units */
#define SH 720

/* ------------------------------------------------- the font sheet (icons) */
#define SHEET 512
#define GAP 2 /* the sheet's letters: pixels between them (the fallback text) */
static uint8_t g_left[1024], g_width[1024];
static int g_font_flipped, g_font_pending;
static GLuint g_font_tex;

void ts_font_captured(const void *rgba, int flipped) {
  const uint32_t *p = rgba;
  for (int cell = 0; cell < 1024; cell++) {
    int cx = (cell % 32) * 16, cy = (cell / 32) * 16, lo = 16, hi = -1;
    for (int y = 0; y < 16; y++) {
      int row = flipped ? SHEET - 1 - (cy + y) : cy + y;
      for (int x = 0; x < 16; x++)
        if (p[row * SHEET + cx + x] >> 24) {
          lo = x < lo ? x : lo;
          hi = x > hi ? x : hi;
        }
    }
    g_left[cell] = hi < 0 ? 0 : (uint8_t)lo;
    g_width[cell] = hi < 0 ? 0 : (uint8_t)(hi - lo + 1);
  }
  g_width[32] = 6; /* space */
  g_font_flipped = flipped;
  g_font_pending = 1;
}

/* right after glTexImage2D (ts_gl.c): the texture the sheet went into */
void ts_font_after_upload(void) {
  if (!g_font_pending)
    return;
  GLint id = 0;
  glGetIntegerv(GL_TEXTURE_BINDING_2D, &id);
  g_font_tex = (GLuint)id;
  g_font_pending = 0;
  debugPrintf("[achievements] the game's font is texture %d: notices can be drawn\n", (int)id);
}

/* The sheet's code for a Latin-1 character: CP437 up to 154 (E-acute 144,
 * U-umlaut 154), then it skips five (E-grave 169). */
static int sheet_cell(unsigned char c) {
  switch (c) {
  case 0xc9: return 144;
  case 0xdc: return 154;
  case 0xc8: return 169;
  default: return c < 128 ? c : '?';
  }
}

/* ------------------------------------------------- the title's ornaments */
#define ORN_W 26 /* both, side by side: x 67..92 of title.png */
#define ORN_H 13
#define ORN_X 67
#define ORN_Y 290
#define ORN_HASH 0xc385686du /* FNV-1a of that spot's alpha > 127, row by row */
static uint8_t g_orn[ORN_H][ORN_W];
static volatile int g_orn_state; /* 1: copied, 2: in g_orn_tex */
static GLuint g_orn_tex;

void ts_title_captured(int w, int h, unsigned fmt, unsigned type, const void *pixels) {
  if (g_orn_state || w != 512 || h != 512 || fmt != GL_RGBA || type != GL_UNSIGNED_BYTE || !pixels)
    return;
  const uint8_t *p = pixels;
  for (int flipped = 0; flipped < 2; flipped++) {
    uint32_t hash = 2166136261u;
    for (int y = 0; y < ORN_H; y++) {
      int row = flipped ? 511 - (ORN_Y + y) : ORN_Y + y;
      for (int x = 0; x < ORN_W; x++) {
        hash ^= p[((size_t)row * 512 + ORN_X + x) * 4 + 3] > 127;
        hash *= 16777619u;
      }
    }
    if (hash != ORN_HASH)
      continue;
    for (int y = 0; y < ORN_H; y++) {
      int row = flipped ? 511 - (ORN_Y + y) : ORN_Y + y;
      for (int x = 0; x < ORN_W; x++)
        g_orn[y][x] = p[((size_t)row * 512 + ORN_X + x) * 4 + 3] > 127;
    }
    g_orn_state = 1;
    debugPrintf("[achievements] title.png seen (%s): the list's ornaments\n", flipped ? "upside down" : "upright");
    return;
  }
}

/* -------------------------------------------------- the menus' typeface */
#define AT_W 512
#define AT_H 256
#define CELL_W 16
#define CELL_H 24
typedef struct {
  uint16_t x, y;
  uint8_t w, h, adv;
  int8_t left, top;
} Glyph;
static Glyph g_gl[256];
static int g_cap = 7; /* the capitals' height (the top of 'V') */
static GLuint g_tt_tex;
static int g_tt; /* 0: not tried, 1: made, -1: not available */

/* A file of the OBB, read whole: its index is u32 count, then per file u32
 * name length, the name, u64 size, u64 offset (from the end of the index). */
static uint8_t *read_obb_file(const char *want, long *size) {
  char buf[DCR_PATH_MAX];
  const char *path = dcr_translate_path(ts_obb_android_path(), buf, sizeof buf);
  FILE *f = fopen(path, "rb");
  if (!f)
    return NULL;
  uint32_t n = 0;
  uint64_t at = 0, len = 0;
  int found = 0;
  if (fread(&n, 4, 1, f) != 1)
    n = 0;
  uint32_t i;
  for (i = 0; i < n; i++) {
    uint32_t ln = 0;
    char name[512];
    uint64_t sz, off;
    if (fread(&ln, 4, 1, f) != 1 || ln >= sizeof name || fread(name, 1, ln, f) != ln || fread(&sz, 8, 1, f) != 1 ||
        fread(&off, 8, 1, f) != 1)
      break;
    name[ln] = 0;
    if (!found && !strcmp(name, want)) {
      at = off;
      len = sz;
      found = 1;
    }
  }
  if (i != n)
    found = 0; /* the index did not read whole */
  else
    at += (uint64_t)ftell(f); /* the data starts after the index */
  uint8_t *data = NULL;
  if (found && len > 0 && len < (16u << 20) && (data = malloc((size_t)len)) != NULL) {
    if (fseek(f, (long)at, SEEK_SET) != 0 || fread(data, 1, (size_t)len, f) != (size_t)len) {
      free(data);
      data = NULL;
    }
  }
  fclose(f);
  *size = data ? (long)len : 0;
  return data;
}

/* FreeType 2's public structs, 32-bit: FT_FaceRec.glyph at 84; in
 * FT_GlyphSlotRec face at 4, advance.x at 64, bitmap (rows, width, pitch,
 * buffer, num_grays, pixel_mode) at 76, bitmap_left 100, bitmap_top 104.
 * Checked as they are read (slot->face is the face, the sizes fit). */
#define FT_LOAD_RENDER 4
#define FT_LOAD_TARGET_MONO 0x20000

static int build_typeface(void) {
  long size = 0;
  uint8_t *ttf = read_obb_file("IMG/Andrios-Font.ttf", &size);
  if (!ttf) {
    debugPrintf("[achievements] Andrios-Font.ttf not read from the OBB: the sheet's letters\n");
    return -1;
  }
  int (*ft_init)(void **) = (void *)so_try_find_addr_rx(&g_mod_game, "FT_Init_FreeType");
  int (*ft_face)(void *, const void *, long, long, void **) =
      (void *)so_try_find_addr_rx(&g_mod_game, "FT_New_Memory_Face");
  int (*ft_size)(void *, unsigned, unsigned) = (void *)so_try_find_addr_rx(&g_mod_game, "FT_Set_Pixel_Sizes");
  int (*ft_char)(void *, unsigned long, int32_t) = (void *)so_try_find_addr_rx(&g_mod_game, "FT_Load_Char");
  int (*ft_done_face)(void *) = (void *)so_try_find_addr_rx(&g_mod_game, "FT_Done_Face");
  int (*ft_done)(void *) = (void *)so_try_find_addr_rx(&g_mod_game, "FT_Done_FreeType");
  void *lib = NULL, *face = NULL;
  uint32_t *px = NULL;
  int ok = -1, made = 0;
  if (!ft_init || !ft_face || !ft_size || !ft_char || !ft_done_face || !ft_done) {
    debugPrintf("[achievements] FreeType not found in the game: the sheet's letters\n");
    goto out;
  }
  int e1 = ft_init(&lib), e2 = e1 ? -1 : ft_face(lib, ttf, size, 0, &face), e3 = e2 ? -1 : ft_size(face, 0, 16);
  if (e1 || e2 || e3) {
    debugPrintf("[achievements] FreeType: init %d, face %d, size %d\n", e1, e2, e3);
    goto out;
  }
  uint8_t *slot = *(uint8_t **)((uint8_t *)face + 84);
  if (!slot || *(void **)(slot + 4) != face) {
    debugPrintf("[achievements] FreeType's face is not laid out as expected: the sheet's letters\n");
    goto out;
  }
  px = calloc((size_t)AT_W * AT_H, 4);
  if (!px)
    goto out;
  for (int c = 32; c < 256; c++) {
    if (c >= 127 && c < 160)
      continue;
    if (ft_char(face, (unsigned long)c, FT_LOAD_RENDER | FT_LOAD_TARGET_MONO))
      continue;
    unsigned rows = *(unsigned *)(slot + 76), width = *(unsigned *)(slot + 80);
    int pitch = *(int *)(slot + 84);
    const uint8_t *bm = *(const uint8_t **)(slot + 88);
    uint8_t mode = slot[94];
    int left = *(int *)(slot + 100), top = *(int *)(slot + 104);
    long adv = *(long *)(slot + 64);
    int k = c - 32, cx = (k % 32) * CELL_W, cy = (k / 32) * CELL_H;
    if (rows > CELL_H || width > CELL_W || adv < 0 || adv > (64 << 5) || (rows && !bm) || pitch < 0) {
      debugPrintf("[achievements] glyph %d: %ux%u, pitch %d, advance %ld -- skipped\n", c, width, rows, pitch, adv);
      continue;
    }
    for (unsigned y = 0; y < rows; y++)
      for (unsigned x = 0; x < width; x++) {
        int on = mode == 1 ? (bm[y * pitch + x / 8] >> (7 - x % 8)) & 1 : bm[y * pitch + x] > 127;
        if (on)
          px[(cy + y) * AT_W + cx + x] = 0xffffffffu;
      }
    g_gl[c] = (Glyph){(uint16_t)cx, (uint16_t)cy, (uint8_t)width, (uint8_t)rows, (uint8_t)((adv + 32) >> 6),
                      (int8_t)left, (int8_t)top};
    made++;
  }
  if (made < 60 || !g_gl['V'].h) {
    debugPrintf("[achievements] only %d glyphs from Andrios: the sheet's letters\n", made);
    goto out;
  }
  g_cap = g_gl['V'].top;
  glGenTextures(1, &g_tt_tex);
  glBindTexture(GL_TEXTURE_2D, g_tt_tex);
  glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, AT_W, AT_H, 0, GL_RGBA, GL_UNSIGNED_BYTE, px);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
  debugPrintf("[achievements] the menus' typeface: %d glyphs of Andrios, capitals %d px\n", made, g_cap);
  ok = 1;
out:
  if (face)
    ft_done_face(face);
  if (lib)
    ft_done(lib);
  free(px);
  free(ttf);
  return ok;
}

/* -------------------------------------------------------- the GL program */
static GLuint g_prog, g_white;
static GLint g_a_pos, g_a_uv, g_u_screen, g_u_color, g_u_tex;
static int g_gl_failed;

static GLuint compile(GLenum type, const char *src) {
  GLuint s = glCreateShader(type);
  glShaderSource(s, 1, &src, NULL);
  glCompileShader(s);
  GLint ok = 0;
  glGetShaderiv(s, GL_COMPILE_STATUS, &ok);
  if (!ok) {
    char log[256] = "";
    glGetShaderInfoLog(s, sizeof log, NULL, log);
    debugPrintf("[achievements] shader: %s\n", log);
  }
  return s;
}

static int gl_setup(void) {
  if (g_prog || g_gl_failed)
    return g_prog != 0;
  static const char *vs = "attribute vec2 a_pos; attribute vec2 a_uv; uniform vec2 u_screen;\n"
                          "varying vec2 v_uv;\n"
                          "void main() { v_uv = a_uv;\n"
                          "  gl_Position = vec4(a_pos.x / u_screen.x * 2.0 - 1.0, 1.0 - a_pos.y / u_screen.y * 2.0, 0.0, 1.0); }\n";
  static const char *fs = "precision mediump float; varying vec2 v_uv; uniform sampler2D u_tex; uniform vec4 u_color;\n"
                          "void main() { gl_FragColor = texture2D(u_tex, v_uv) * u_color; }\n";
  GLuint p = glCreateProgram();
  glAttachShader(p, compile(GL_VERTEX_SHADER, vs));
  glAttachShader(p, compile(GL_FRAGMENT_SHADER, fs));
  glBindAttribLocation(p, 0, "a_pos");
  glBindAttribLocation(p, 1, "a_uv");
  glLinkProgram(p);
  GLint ok = 0;
  glGetProgramiv(p, GL_LINK_STATUS, &ok);
  if (!ok) {
    g_gl_failed = 1;
    debugPrintf("[achievements] the overlay's program did not link: no notices\n");
    return 0;
  }
  g_prog = p;
  g_a_pos = 0;
  g_a_uv = 1;
  g_u_screen = glGetUniformLocation(p, "u_screen");
  g_u_color = glGetUniformLocation(p, "u_color");
  g_u_tex = glGetUniformLocation(p, "u_tex");
  static const uint32_t white = 0xffffffffu;
  glGenTextures(1, &g_white);
  glBindTexture(GL_TEXTURE_2D, g_white);
  glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, 1, 1, 0, GL_RGBA, GL_UNSIGNED_BYTE, &white);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
  return 1;
}

/* ------------------------------------------------- the game's state, kept */
typedef struct {
  GLint prog, array_buf, elem_buf, active_tex, tex0, fbo, viewport[4], blend_src_rgb, blend_dst_rgb,
      blend_src_a, blend_dst_a, blend_eq_rgb, blend_eq_a;
  GLboolean blend, scissor, depth, cull, stencil, color_mask[4];
  struct {
    GLint enabled, size, type, norm, stride, buf;
    void *ptr;
  } attr[2];
} Saved;

static void save_state(Saved *s) {
  glGetIntegerv(GL_CURRENT_PROGRAM, &s->prog);
  glGetIntegerv(GL_ARRAY_BUFFER_BINDING, &s->array_buf);
  glGetIntegerv(GL_ELEMENT_ARRAY_BUFFER_BINDING, &s->elem_buf);
  glGetIntegerv(GL_ACTIVE_TEXTURE, &s->active_tex);
  glActiveTexture(GL_TEXTURE0);
  glGetIntegerv(GL_TEXTURE_BINDING_2D, &s->tex0);
  glGetIntegerv(GL_FRAMEBUFFER_BINDING, &s->fbo);
  glGetIntegerv(GL_VIEWPORT, s->viewport);
  glGetIntegerv(GL_BLEND_SRC_RGB, &s->blend_src_rgb);
  glGetIntegerv(GL_BLEND_DST_RGB, &s->blend_dst_rgb);
  glGetIntegerv(GL_BLEND_SRC_ALPHA, &s->blend_src_a);
  glGetIntegerv(GL_BLEND_DST_ALPHA, &s->blend_dst_a);
  glGetIntegerv(GL_BLEND_EQUATION_RGB, &s->blend_eq_rgb);
  glGetIntegerv(GL_BLEND_EQUATION_ALPHA, &s->blend_eq_a);
  s->blend = glIsEnabled(GL_BLEND);
  s->scissor = glIsEnabled(GL_SCISSOR_TEST);
  s->depth = glIsEnabled(GL_DEPTH_TEST);
  s->cull = glIsEnabled(GL_CULL_FACE);
  s->stencil = glIsEnabled(GL_STENCIL_TEST);
  glGetBooleanv(GL_COLOR_WRITEMASK, s->color_mask);
  for (int i = 0; i < 2; i++) {
    glGetVertexAttribiv(i, GL_VERTEX_ATTRIB_ARRAY_ENABLED, &s->attr[i].enabled);
    glGetVertexAttribiv(i, GL_VERTEX_ATTRIB_ARRAY_SIZE, &s->attr[i].size);
    glGetVertexAttribiv(i, GL_VERTEX_ATTRIB_ARRAY_TYPE, &s->attr[i].type);
    glGetVertexAttribiv(i, GL_VERTEX_ATTRIB_ARRAY_NORMALIZED, &s->attr[i].norm);
    glGetVertexAttribiv(i, GL_VERTEX_ATTRIB_ARRAY_STRIDE, &s->attr[i].stride);
    glGetVertexAttribiv(i, GL_VERTEX_ATTRIB_ARRAY_BUFFER_BINDING, &s->attr[i].buf);
    glGetVertexAttribPointerv(i, GL_VERTEX_ATTRIB_ARRAY_POINTER, &s->attr[i].ptr);
  }
}

static void set_flag(GLenum cap, GLboolean on) {
  if (on)
    glEnable(cap);
  else
    glDisable(cap);
}

static void restore_state(const Saved *s) {
  for (int i = 0; i < 2; i++) {
    glBindBuffer(GL_ARRAY_BUFFER, (GLuint)s->attr[i].buf);
    glVertexAttribPointer(i, s->attr[i].size, (GLenum)s->attr[i].type, (GLboolean)s->attr[i].norm,
                          s->attr[i].stride, s->attr[i].ptr);
    if (s->attr[i].enabled)
      glEnableVertexAttribArray(i);
    else
      glDisableVertexAttribArray(i);
  }
  glBindBuffer(GL_ARRAY_BUFFER, (GLuint)s->array_buf);
  glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, (GLuint)s->elem_buf);
  glUseProgram((GLuint)s->prog);
  glActiveTexture(GL_TEXTURE0);
  glBindTexture(GL_TEXTURE_2D, (GLuint)s->tex0);
  glActiveTexture((GLenum)s->active_tex);
  glBindFramebuffer(GL_FRAMEBUFFER, (GLuint)s->fbo);
  glViewport(s->viewport[0], s->viewport[1], s->viewport[2], s->viewport[3]);
  glBlendFuncSeparate((GLenum)s->blend_src_rgb, (GLenum)s->blend_dst_rgb, (GLenum)s->blend_src_a,
                      (GLenum)s->blend_dst_a);
  glBlendEquationSeparate((GLenum)s->blend_eq_rgb, (GLenum)s->blend_eq_a);
  set_flag(GL_BLEND, s->blend);
  set_flag(GL_SCISSOR_TEST, s->scissor);
  set_flag(GL_DEPTH_TEST, s->depth);
  set_flag(GL_CULL_FACE, s->cull);
  set_flag(GL_STENCIL_TEST, s->stencil);
  glColorMask(s->color_mask[0], s->color_mask[1], s->color_mask[2], s->color_mask[3]);
}

/* ------------------------------------------------------------ drawing */
#define MAXV 6 * 512
static GLfloat g_pos[MAXV * 2], g_uv[MAXV * 2];
static int g_nv;

static void quad(float x0, float y0, float x1, float y1, float u0, float v0, float u1, float v1) {
  if (g_nv + 6 > MAXV)
    return;
  const float px[6] = {x0, x1, x1, x0, x1, x0}, py[6] = {y0, y0, y1, y0, y1, y1};
  const float pu[6] = {u0, u1, u1, u0, u1, u0}, pv[6] = {v0, v0, v1, v0, v1, v1};
  for (int i = 0; i < 6; i++) {
    g_pos[2 * g_nv] = px[i];
    g_pos[2 * g_nv + 1] = py[i];
    g_uv[2 * g_nv] = pu[i];
    g_uv[2 * g_nv + 1] = pv[i];
    g_nv++;
  }
}

static void flush(GLuint tex, float r, float g, float b, float a) {
  if (!g_nv)
    return;
  glBindTexture(GL_TEXTURE_2D, tex);
  glUniform4f(g_u_color, r, g, b, a);
  glVertexAttribPointer(g_a_pos, 2, GL_FLOAT, GL_FALSE, 0, g_pos);
  glVertexAttribPointer(g_a_uv, 2, GL_FLOAT, GL_FALSE, 0, g_uv);
  glDrawArrays(GL_TRIANGLES, 0, g_nv);
  g_nv = 0;
}

static void rect(float x0, float y0, float x1, float y1, float r, float g, float b, float a) {
  quad(x0, y0, x1, y1, 0, 0, 1, 1);
  flush(g_white, r, g, b, a);
}

/* --------------------------------------------------------------- text
 * y is the top of the capitals. With the typeface: 2x, each glyph at its
 * bearing from the baseline, the pen moved by its advance; without it, the
 * sheet's letters at 1x. */
static int text_width(const char *s) {
  int w = 0;
  if (g_tt > 0) {
    for (; *s; s++)
      w += g_gl[(unsigned char)*s].adv;
    return w * 2;
  }
  for (; *s; s++)
    w += g_width[sheet_cell((unsigned char)*s)] + GAP;
  return w ? w - GAP : 0;
}

static void text(const char *s, float x, float y, float r, float g, float b, float a) {
  if (g_tt > 0) {
    float base = y + (float)(g_cap * 2);
    for (; *s; s++) {
      const Glyph *gl = &g_gl[(unsigned char)*s];
      if (gl->w) {
        float x0 = x + (float)(gl->left * 2), y0 = base - (float)(gl->top * 2);
        quad(x0, y0, x0 + (float)(gl->w * 2), y0 + (float)(gl->h * 2), (float)gl->x / AT_W, (float)gl->y / AT_H,
             (float)(gl->x + gl->w) / AT_W, (float)(gl->y + gl->h) / AT_H);
      }
      x += (float)(gl->adv * 2);
    }
    flush(g_tt_tex, r, g, b, a);
    return;
  }
  for (; *s; s++) {
    int cell = sheet_cell((unsigned char)*s);
    int w = g_width[cell];
    if (cell != 32 && w) {
      float cx = (float)((cell % 32) * 16 + g_left[cell]), cy = (float)((cell / 32) * 16);
      float u0 = cx / SHEET, u1 = (cx + (float)w) / SHEET;
      float v0 = cy / SHEET, v1 = (cy + 16) / SHEET;
      if (g_font_flipped) {
        v0 = 1.0f - v0;
        v1 = 1.0f - v1;
      }
      quad(x, y, x + (float)w, y + 16, u0, v0, u1, v1);
    }
    x += (float)(w + GAP);
  }
  flush(g_font_tex, r, g, b, a);
}

static void text_centered(const char *s, float y, float a) {
  text(s, (float)((SW - text_width(s)) / 2), y, 1, 1, 1, a);
}

/* one cell of the font sheet (a button icon), size x size at x, y */
static void cell_at(int cell, float x, float y, float size) {
  float cx = (float)((cell % 32) * 16), cy = (float)((cell / 32) * 16);
  float v0 = cy / SHEET, v1 = (cy + 16) / SHEET;
  if (g_font_flipped) {
    v0 = 1.0f - v0;
    v1 = 1.0f - v1;
  }
  quad(x, y, x + size, y + size, cx / SHEET, v0, (cx + 16) / SHEET, v1);
  flush(g_font_tex, 1, 1, 1, 1);
}

/* the ornaments around a line whose capitals start at y, at 2x */
static void ornaments(float lx, float rx, float y) {
  if (g_orn_state == 1) {
    static uint32_t px[ORN_H][ORN_W];
    for (int j = 0; j < ORN_H; j++)
      for (int i = 0; i < ORN_W; i++)
        px[j][i] = g_orn[j][i] ? 0xffffffffu : 0x00ffffffu;
    glGenTextures(1, &g_orn_tex);
    glBindTexture(GL_TEXTURE_2D, g_orn_tex);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, ORN_W, ORN_H, 0, GL_RGBA, GL_UNSIGNED_BYTE, px);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    g_orn_state = 2;
  }
  if (g_orn_state == 2) {
    quad(lx, y - 6, lx + 20, y + 20, 0, 0, 10.0f / ORN_W, 1);
    quad(rx, y - 6, rx + 20, y + 20, 16.0f / ORN_W, 0, 1, 1);
    flush(g_orn_tex, 1, 1, 1, 1);
  } else {
    text(">", lx + 4, y, 1, 1, 1, 1);
    text("<", rx + 4, y, 1, 1, 1, 1);
  }
}

static void upper(char *dst, size_t cap, const char *src) {
  size_t i = 0;
  for (; src[i] && i + 1 < cap; i++)
    dst[i] = (src[i] >= 'a' && src[i] <= 'z') ? (char)(src[i] - 32) : src[i];
  dst[i] = 0;
}

/* the game's colours: its text is white; the lines not chosen are white at
 * about 35% (93,103,102 over the title's scene) */
#define DIM_A 0.35f
#define UNLOCKED_A 0.6f

/* ------------------------------------------------------------ the notice */
#define QUEUE 8
static char g_queue[QUEUE][48];
static int g_q_head, g_q_len;
static u64 g_shown_at;
static Mutex g_q_lock;
static volatile int g_list_open, g_list_menu;
static volatile int g_sel; /* the list's selected achievement */

/* ------------------------------------------------------------- language
 * The game's language, as it announces it on stdout ("LANGUAGE: %s\n",
 * when it starts and when it is changed in its options; ts_imports_extra.c
 * passes it here). The texts are Latin-1 (the typeface's codes; the
 * fallback maps the few accents to the sheet's). "back" is the game's own
 * m_back text. Titanese (its made-up script) shows English. */
enum { L_EN, L_FR, L_DE, L_IT, L_PT, L_ES, L_N };
static volatile int g_lang = L_EN;
static const struct {
  const char *name, *title, *unlocked_head, *unlocked, *locked, *back;
} k_text[L_N] = {
    [L_EN] = {"english", "ACHIEVEMENTS", "ACHIEVEMENT UNLOCKED", "UNLOCKED", "LOCKED", "BACK"},
    [L_FR] = {"french", "SUCC\xc8S", "SUCC\xc8S D\xc9" "BLOQU\xc9", "D\xc9" "BLOQU\xc9", "VERROUILL\xc9", "RETOUR"},
    [L_DE] = {"german", "ERFOLGE", "ERFOLG FREIGESCHALTET", "FREIGESCHALTET", "GESPERRT", "ZUR\xdc" "CK"},
    [L_IT] = {"italian", "OBIETTIVI", "OBIETTIVO SBLOCCATO", "SBLOCCATO", "BLOCCATO", "INDIETRO"},
    [L_PT] = {"portuguese", "CONQUISTAS", "CONQUISTA DESBLOQUEADA", "DESBLOQUEADA", "BLOQUEADA", "VOLTAR"},
    [L_ES] = {"spanish", "LOGROS", "LOGRO DESBLOQUEADO", "DESBLOQUEADO", "BLOQUEADO", "VOLVER"},
};

void ts_overlay_set_language(const char *name) {
  int l = L_EN;
  for (int i = 0; i < L_N; i++)
    if (name && !strncmp(name, k_text[i].name, strlen(k_text[i].name)))
      l = i;
  if (l != g_lang)
    debugPrintf("[achievements] the game's language: %s\n", k_text[l].name);
  g_lang = l;
}

void ts_overlay_list_move(int d) {
  int n = ts_ach_count(), s = g_sel + d;
  g_sel = s < 0 ? 0 : s >= n ? n - 1 : s;
}

void ts_overlay_notice(const char *name) {
  mutexLock(&g_q_lock);
  if (g_q_len < QUEUE) {
    snprintf(g_queue[(g_q_head + g_q_len) % QUEUE], sizeof g_queue[0], "%s", name);
    g_q_len++;
  }
  mutexUnlock(&g_q_lock);
}

void ts_overlay_toggle_list(void) {
  g_list_open = !g_list_open;
  g_list_menu = 0;
  debugPrintf("[achievements] list %s\n", g_list_open ? "open" : "closed");
}

void ts_overlay_open_from_menu(void) {
  if (g_list_open)
    return;
  g_list_open = 1;
  g_list_menu = 1;
  debugPrintf("[achievements] list open (title menu)\n");
}

int ts_overlay_list_open(void) { return g_list_open; }

/* the game's greys for the notice's box (its font's button colours) */
#define DARK 35 / 255.0f, 39 / 255.0f, 58 / 255.0f
#define MIDC 72 / 255.0f, 77 / 255.0f, 93 / 255.0f

static void draw_notice(void) {
  char name[48], head[48];
  float alpha;
  mutexLock(&g_q_lock);
  if (!g_q_len) {
    mutexUnlock(&g_q_lock);
    return;
  }
  u64 now = armGetSystemTick();
  if (!g_shown_at)
    g_shown_at = now;
  float t = (float)(armTicksToNs(now - g_shown_at) / 1000000ull) / 1000.0f;
  if (t > 4.0f) { /* the next one */
    g_q_head = (g_q_head + 1) % QUEUE;
    g_q_len--;
    g_shown_at = 0;
    mutexUnlock(&g_q_lock);
    return;
  }
  upper(name, sizeof name, g_queue[g_q_head]);
  mutexUnlock(&g_q_lock);
  alpha = t < 0.3f ? t / 0.3f : t > 3.5f ? (4.0f - t) / 0.5f : 1.0f;

  snprintf(head, sizeof head, "%s", k_text[g_lang].unlocked_head);
  int w = text_width(head), wn = text_width(name);
  w = (wn > w ? wn : w) + 2 * 28;
  float x0 = (float)(SW - w) / 2, y0 = 20, x1 = x0 + (float)w, y1 = y0 + 76;
  rect(x0 - 2, y0 - 2, x1 + 2, y1 + 2, MIDC, alpha);
  rect(x0, y0, x1, y1, DARK, 0.92f * alpha);
  text(head, x0 + (float)(w - text_width(head)) / 2, y0 + 16, 1, 1, 1, 0.6f * alpha);
  text(name, x0 + (float)(w - text_width(name)) / 2, y0 + 16 + 30, 1, 1, 1, alpha);
}

/* Like the game's title menu (and laid out on a preview of it): the scene
 * darkened, the title and the count, nine lines 32 apart (the menu's
 * spacing), the chosen one white between the title's ornaments -- where
 * the menu has them, or further out for a longer name -- the others white
 * at 35%, unlocked ones at 60%, the first and last faded when the list goes
 * on past them; the chosen one's state under it; and the "back" prompt
 * where the game's own is, at its size: the B icon (cell 0 of the sheet's
 * button row: the game's "A", which the physical B sends -- a B when the
 * Switch icons are on; A closes the list too, unannounced) and m_back. When
 * the title menu opened it, its own first prompt (SELECT) is blacked out:
 * ours covers the second one exactly. */
#define VISIBLE 9
#define ROWH 32
#define TOP 192
static void draw_list(void) {
  rect(0, 0, SW, SH, 0, 0, 0, 0.88f);
  if (g_list_menu)
    rect(84, 604, 420, 640, 0, 0, 0, 1);
  int n = ts_ach_count(), sel = g_sel;
  char buf[64];
  text_centered(k_text[g_lang].title, 96, 1);
  snprintf(buf, sizeof buf, "%d / %d", ts_ach_unlocked(), n);
  text_centered(buf, 136, DIM_A);
  int first = sel - VISIBLE / 2;
  if (first > n - VISIBLE)
    first = n - VISIBLE;
  if (first < 0)
    first = 0;
  for (int r = 0; r < VISIBLE && first + r < n; r++) {
    int i = first + r;
    float y = (float)(TOP + r * ROWH);
    upper(buf, sizeof buf, ts_ach_name(i));
    float a = i == sel ? 1.0f : ts_ach_when(i)[0] ? UNLOCKED_A : DIM_A;
    if ((r == 0 && first > 0) || (r == VISIBLE - 1 && first + VISIBLE < n))
      a *= 0.45f;
    text_centered(buf, y, a);
    if (i == sel) {
      int w = text_width(buf), x = (SW - w) / 2;
      int lx = x - 60 < 530 ? x - 60 : 530, rx = x + w + 40 > 730 ? x + w + 40 : 730;
      ornaments((float)lx, (float)rx, y);
    }
  }
  const char *when = ts_ach_when(sel);
  float ys = (float)(TOP + VISIBLE * ROWH + 24);
  if (when[0]) {
    snprintf(buf, sizeof buf, "%s  %.10s", k_text[g_lang].unlocked, when);
    text_centered(buf, ys, 1);
  } else {
    text_centered(k_text[g_lang].locked, ys, DIM_A);
  }
  cell_at(18 * 32 + 0, 88, 640, 32);
  text(k_text[g_lang].back, 128, 646, 1, 1, 1, 1);
}

static void present(void) {
  int busy = g_list_open;
  if (!busy) {
    mutexLock(&g_q_lock);
    busy = g_q_len > 0;
    mutexUnlock(&g_q_lock);
  }
  if (!busy || !g_font_tex || !gl_setup())
    return;
  int sw = 1280, sh = 720;
  dcr_window_size(&sw, &sh);
  Saved s;
  save_state(&s);
  if (!g_tt)
    g_tt = build_typeface(); /* once, the first time something is drawn */
  glBindFramebuffer(GL_FRAMEBUFFER, 0);
  glViewport(0, 0, sw, sh);
  glUseProgram(g_prog);
  glUniform2f(g_u_screen, (float)SW, (float)SH);
  glUniform1i(g_u_tex, 0);
  glActiveTexture(GL_TEXTURE0);
  glBindBuffer(GL_ARRAY_BUFFER, 0);
  glEnableVertexAttribArray(g_a_pos);
  glEnableVertexAttribArray(g_a_uv);
  glDisable(GL_SCISSOR_TEST);
  glDisable(GL_DEPTH_TEST);
  glDisable(GL_CULL_FACE);
  glDisable(GL_STENCIL_TEST);
  glColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);
  glEnable(GL_BLEND);
  glBlendEquation(GL_FUNC_ADD);
  glBlendFuncSeparate(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA, GL_ONE, GL_ONE_MINUS_SRC_ALPHA);
  if (g_list_open)
    draw_list();
  draw_notice();
  restore_state(&s);
}

void ts_overlay_init(void) {
  mutexInit(&g_q_lock);
  dcr_present_hook = present;
}
