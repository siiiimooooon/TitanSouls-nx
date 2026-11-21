/* ts_gl.c -- the port's wrappers over the game's GL calls (port_gl_wrap,
 * runtime/source/gl_mesa.c: called once per GL entry point the game binds;
 * a non-zero return replaces it).
 *
 * glShaderSource: the first shaders' sources are logged (their first line
 * and length), and every one whose first character cannot start GLSL, so a
 * source that arrives broken is visible in debug.log (hardware 2026-11-18:
 * the OBB's shaders failed at 0:1(1) until the OBB stdio overlay went).
 * glTexImage2D / glTexSubImage2D, glCompileShader, glLinkProgram: timed for
 * the load counters (ts_prof.c). Every call goes through unchanged. MIT.
 */
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include "gl_layer.h"
#include "ts.h"
#include "util.h"

uint64_t ts_prof_begin(void);
void ts_prof_end(int which, uint64_t t0, uint64_t bytes);
enum { P_TEX = 3, P_COMPILE = 4, P_LINK = 5 }; /* ts_prof.c */

typedef void (*fn_shader_source)(unsigned shader, int count, const char *const *string, const int *length);
typedef void (*fn_tex_image)(unsigned target, int level, int ifmt, int w, int h, int border, unsigned fmt,
                             unsigned type, const void *pixels);
typedef void (*fn_tex_sub)(unsigned target, int level, int x, int y, int w, int h, unsigned fmt, unsigned type,
                           const void *pixels);
typedef void (*fn_uint)(unsigned);

static fn_shader_source g_shader_source;
static fn_tex_image g_tex_image;
static fn_tex_sub g_tex_sub;
static fn_uint g_compile, g_link;

static void log_source(unsigned shader, int count, const char *const *string, const int *length) {
  static unsigned calls;
  calls++;
  if (!string || count <= 0 || !string[0])
    return;
  size_t total = 0;
  for (int i = 0; i < count; i++)
    if (string[i])
      total += length && length[i] >= 0 ? (size_t)length[i] : strlen(string[i]);
  const char *s = string[0];
  size_t n0 = length && length[0] >= 0 ? (size_t)length[0] : strlen(s);
  char c = n0 ? s[0] : 0;
  int odd = !(c == '#' || c == '/' || c == ' ' || c == '\t' || c == '\n' || c == '\r' ||
              (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z'));
  if (calls > 4 && !odd)
    return;
  char line[100];
  size_t k = 0;
  for (; k < n0 && k < sizeof line - 1; k++) {
    char ch = s[k];
    line[k] = ch == '\n' ? '|' : (ch < 32 || ch > 126) ? '?' : ch;
  }
  line[k] = 0;
  debugPrintf("[shader] #%u (shader %u): %d part(s), %u bytes%s: \"%s\"\n", calls, shader, count,
              (unsigned)total, odd ? " -- DOES NOT START LIKE GLSL" : "", line);
}

static void ts_glShaderSource(unsigned shader, int count, const char *const *string, const int *length) {
  log_source(shader, count, string, length);
  g_shader_source(shader, count, string, length);
}

void *ts_glyphs_patch(int level, int w, int h, unsigned fmt, unsigned type, const void *pixels); /* ts_glyphs.c */
void *ts_switchpad_patch(int level, int w, int h, unsigned fmt, unsigned type, const void *pixels); /* ts_switchpad.c */

static void ts_glTexImage2D(unsigned target, int level, int ifmt, int w, int h, int border, unsigned fmt,
                            unsigned type, const void *pixels) {
  uint64_t t0 = ts_prof_begin();
  void *patched = ts_glyphs_patch(level, w, h, fmt, type, pixels); /* the font sheet: Switch icons */
  if (!patched)
    patched = ts_switchpad_patch(level, w, h, fmt, type, pixels); /* the controls picture: a Switch Lite */
  if (!patched && level == 0)
    ts_title_captured(w, h, fmt, type, pixels); /* the title menu's ornaments, for the achievements list */
  g_tex_image(target, level, ifmt, w, h, border, fmt, type, patched ? patched : pixels);
  ts_font_after_upload(); /* the font sheet's texture, for the achievements' notices */
  free(patched);
  ts_prof_end(P_TEX, t0, pixels ? (uint64_t)w * (uint64_t)h * 4u : 0);
}

static void ts_glTexSubImage2D(unsigned target, int level, int x, int y, int w, int h, unsigned fmt,
                               unsigned type, const void *pixels) {
  uint64_t t0 = ts_prof_begin();
  g_tex_sub(target, level, x, y, w, h, fmt, type, pixels);
  ts_prof_end(P_TEX, t0, pixels ? (uint64_t)w * (uint64_t)h * 4u : 0);
}

static void ts_glCompileShader(unsigned s) {
  uint64_t t0 = ts_prof_begin();
  g_compile(s);
  ts_prof_end(P_COMPILE, t0, 0);
}

static void ts_glLinkProgram(unsigned p) {
  uint64_t t0 = ts_prof_begin();
  g_link(p);
  ts_prof_end(P_LINK, t0, 0);
}

uintptr_t port_gl_wrap(const char *name, uintptr_t real) {
#define WRAP(fn, var, type)            \
  if (!strcmp(name, #fn)) {            \
    var = (type)real;                  \
    return (uintptr_t)ts_##fn;         \
  }
  WRAP(glShaderSource, g_shader_source, fn_shader_source)
  WRAP(glTexImage2D, g_tex_image, fn_tex_image)
  WRAP(glTexSubImage2D, g_tex_sub, fn_tex_sub)
  WRAP(glCompileShader, g_compile, fn_uint)
  WRAP(glLinkProgram, g_link, fn_uint)
#undef WRAP
  return 0;
}
