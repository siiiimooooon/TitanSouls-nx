/* ts_imports_extra.c -- the port_imports[] overlay (imports.h), searched
 * first by the loader and by dlsym alike: the port's own versions of a few
 * calls the game makes.
 *
 * No OBB serialization (DuckTales-NX's ts_obbio.c is gone). DuckTales opens
 * its OBB once and shares that FILE between threads, so DuckTales-NX keeps
 * a read position per THREAD. Titan Souls opens the OBB once per file it
 * reads (1379 fopen in a 9-minute session), each FILE its own; with that
 * overlay a FILE opened after another inherited the thread's old position,
 * and shader sources came out as garbage (hardware 2026-11-18, 5th run).
 *
 * fopen and fread pass through to the runtime's stdio, timed for the load
 * counters (ts_prof.c). fopen also sets the buffer of files opened only for
 * reading to [performance] read_buffer_kb of config.ini (0: newlib's 1 KB,
 * the runtime's default -- each refill one round trip to the filesystem
 * service), so it can be compared without a rebuild.
 * Based on DuckTales-NX (Thorhax). MIT.
 */
#include <stdarg.h>
#include <stdint.h>
#include <string.h>

#include "dcr_config.h"
#include "imports.h"
#include "ts.h"

uint64_t ts_prof_begin(void);
void ts_prof_end(int which, uint64_t t0, uint64_t bytes);
enum { P_FREAD = 1, P_FOPEN = 2 }; /* ts_prof.c */

void *b_fopen(const char *path, const char *mode);
int b_fclose(void *fp);
unsigned b_fread(void *p, unsigned sz, unsigned n, void *fp);
long b_ftell(void *fp);
int b_setvbuf(void *fp, char *buf, int mode, unsigned size);

/* The game's text for the title menu's ACHIEVEMENTS option (ts_menu.c):
 * data/gametext.xml in the OBB has an "m_achievements" entry that says
 * "ACHIEVEMENTS" in all seven languages (this version never showed it). As
 * the game reads that part of the OBB, each language's word is replaced in
 * what it reads: "ACHIEVEMENTS</TextString>" (25 bytes) becomes the
 * translation, the closing tag and spaces to the same length. The offsets
 * are absolute in main.31.com.devolver.titansouls.obb (tools/obb_tool.py:
 * gametext.xml at 0x1c90c8c). English and Titanese stay as they are. */
static const struct {
  long at;
  const char *word; /* UTF-8, at most 12 bytes */
} k_gametext[] = {
    {0x1c97cc7, "SUCC\xc3\x88S"}, /* french */
    {0x1c97d07, "ERFOLGE"},       /* german */
    {0x1c97d48, "OBIETTIVI"},     /* italian */
    {0x1c97d8c, "CONQUISTAS"},    /* portuguese */
    {0x1c97dcd, "LOGROS"},        /* spanish */
};
#define SPAN 25

#define OBB_FILES 32
static void *g_obb[OBB_FILES];

static int is_obb(void *f) {
  for (int i = 0; i < OBB_FILES; i++)
    if (g_obb[i] == f)
      return 1;
  return 0;
}

static void patch_gametext(unsigned char *buf, long pos, unsigned len) {
  for (unsigned k = 0; k < sizeof k_gametext / sizeof k_gametext[0]; k++) {
    char repl[SPAN + 1];
    int w = (int)strlen(k_gametext[k].word);
    memcpy(repl, k_gametext[k].word, (size_t)w);
    memcpy(repl + w, "</TextString>", 13);
    memset(repl + w + 13, ' ', (size_t)(SPAN - w - 13));
    long a = k_gametext[k].at, from = a > pos ? a : pos, to = a + SPAN < pos + (long)len ? a + SPAN : pos + (long)len;
    for (long i = from; i < to; i++)
      buf[i - pos] = (unsigned char)repl[i - a];
  }
}

static void *ts_fopen(const char *path, const char *mode) {
  uint64_t t0 = ts_prof_begin();
  void *f = b_fopen(path, mode);
  int kb = dcr_config()->read_buffer_kb;
  if (f && kb > 0 && mode && mode[0] == 'r' && !strchr(mode, '+'))
    b_setvbuf(f, NULL, 0 /* _IOFBF */, (unsigned)kb << 10);
  size_t n = path ? strlen(path) : 0;
  if (f && n > 4 && !strcmp(path + n - 4, ".obb"))
    for (int i = 0; i < OBB_FILES; i++)
      if (!g_obb[i]) {
        g_obb[i] = f;
        break;
      }
  ts_prof_end(P_FOPEN, t0, 0);
  return f;
}

static int ts_fclose(void *f) {
  for (int i = 0; i < OBB_FILES; i++)
    if (g_obb[i] == f)
      g_obb[i] = NULL;
  return b_fclose(f);
}

static unsigned ts_fread(void *p, unsigned sz, unsigned n, void *fp) {
  uint64_t t0 = ts_prof_begin();
  long pos = is_obb(fp) ? b_ftell(fp) : -1;
  unsigned r = b_fread(p, sz, n, fp);
  if (pos >= 0 && r) {
    long len = (long)r * (long)sz;
    if (pos < k_gametext[4].at + SPAN && pos + len > k_gametext[0].at)
      patch_gametext(p, pos, (unsigned)len);
  }
  ts_prof_end(P_FREAD, t0, (uint64_t)r * sz);
  return r;
}

/* printf / vprintf: the game prints "UNLOCK: <ID>" when it unlocks an
 * achievement (ts_achievements.c); everything goes on to the runtime's. */
int b_vprintf(const char *fmt, va_list ap);

static void watch(const char *fmt, va_list ap) {
  if (!fmt)
    return;
  int unlock = !strncmp(fmt, "UNLOCK: %s", 10), lang = !strncmp(fmt, "LANGUAGE: %s", 12);
  if (unlock || lang) {
    va_list cp;
    va_copy(cp, ap);
    const char *arg = va_arg(cp, const char *);
    va_end(cp);
    if (unlock)
      ts_ach_unlock(arg);
    else
      ts_overlay_set_language(arg); /* the game's language, for the port's texts */
  }
}

static int ts_printf(const char *fmt, ...) {
  va_list ap;
  va_start(ap, fmt);
  watch(fmt, ap);
  int r = b_vprintf(fmt, ap);
  va_end(ap);
  return r;
}

static int ts_vprintf(const char *fmt, va_list ap) {
  watch(fmt, ap);
  return b_vprintf(fmt, ap);
}

/* The C++ runtime calls libfmodex.so imports. libTestSuite.so exports them
 * (its static libstdc++), so gen_imports.py counted them as covered and left
 * them out of the table -- but libfmodex.so is loaded FIRST, before any
 * module exports them: hardware 2026-11-18, "libfmodex.so: 6 unresolved
 * imports" (__cxa_pure_virtual x5, _ZdlPv). The runtime's own shims
 * (bionic_cxx.c), bound here for every module. */
void b__ZdlPv(void *p);
void b___cxa_pure_virtual(void);

const DynLibFunction port_imports[] = {
    {"_ZdlPv", (uintptr_t)b__ZdlPv},
    {"__cxa_pure_virtual", (uintptr_t)b___cxa_pure_virtual},
    {"fopen", (uintptr_t)ts_fopen},
    {"fclose", (uintptr_t)ts_fclose},
    {"fread", (uintptr_t)ts_fread},
    {"printf", (uintptr_t)ts_printf},
    {"vprintf", (uintptr_t)ts_vprintf},
};
const int port_imports_count = sizeof port_imports / sizeof port_imports[0];
