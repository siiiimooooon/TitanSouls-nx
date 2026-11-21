/* ts_prof.c -- what a long frame (a load) spends its time on.
 *
 * The runtime logs every frame that takes long ("[frame] frame N took X ms;
 * CPU since: ..."): it snapshots its counters a moment into the frame
 * (port_boost_snapshot) and asks the port for more at the end
 * (port_boost_format, dcr_boost.c). Here: count, time and bytes of
 *   inflate   zlib in the runtime (port_prof_begin/end, bionic_zlib.c)
 *   fread     every fread the game makes (ts_imports_extra.c)
 *   fopen     every fopen
 *   tex       glTexImage2D / glTexSubImage2D (ts_gl.c)
 *   compile   glCompileShader, link glLinkProgram (ts_gl.c)
 *   sound     FMOD System::createSound (ts_fmod.c)
 * Hardware 2026-11-18 (build 200151): loads of 3.5 and 6.4 s, one thread
 * busy 5.4 s of the 6.4 -- CPU, not waiting -- with no way to tell on what.
 * The counters are a few atomic adds per call. MIT.
 */
#include <stdint.h>
#include <stdio.h>
#include <switch.h>

#include "bionic.h"
#include "ts.h"

enum { P_INFLATE, P_FREAD, P_FOPEN, P_TEX, P_COMPILE, P_LINK, P_SOUND, P_N };
static const char *const k_names[P_N] = {"inflate", "fread", "fopen", "tex", "compile", "link", "sound"};

typedef struct {
  uint64_t calls, ticks, bytes;
} Counter;

static Counter g_c[P_N], g_snap[P_N];

uint64_t ts_prof_begin(void) { return armGetSystemTick(); }

void ts_prof_end(int which, uint64_t t0, uint64_t bytes) {
  if ((unsigned)which >= P_N)
    return;
  uint64_t dt = armGetSystemTick() - t0;
  __atomic_add_fetch(&g_c[which].calls, 1, __ATOMIC_RELAXED);
  __atomic_add_fetch(&g_c[which].ticks, dt, __ATOMIC_RELAXED);
  __atomic_add_fetch(&g_c[which].bytes, bytes, __ATOMIC_RELAXED);
}

/* the runtime's counters (bionic.h): only inflate is ours to map */
uint64_t port_prof_begin(void) { return armGetSystemTick(); }
void port_prof_end(int counter, uint64_t t0, uint64_t bytes) {
  if (counter == RT_PROF_INFLATE)
    ts_prof_end(P_INFLATE, t0, bytes);
}

static uint64_t g_snap_tick;

void port_boost_snapshot(void) {
  g_snap_tick = armGetSystemTick();
  ts_sampler_begin();
  for (int i = 0; i < P_N; i++) {
    g_snap[i].calls = __atomic_load_n(&g_c[i].calls, __ATOMIC_RELAXED);
    g_snap[i].ticks = __atomic_load_n(&g_c[i].ticks, __ATOMIC_RELAXED);
    g_snap[i].bytes = __atomic_load_n(&g_c[i].bytes, __ATOMIC_RELAXED);
  }
}

int port_boost_format(char *out, size_t cap, int startup) {
  (void)startup;
  ts_sampler_report((unsigned)(armTicksToNs(armGetSystemTick() - g_snap_tick) / 1000000ull));
  int n = 0;
  for (int i = 0; i < P_N && n < (int)cap - 40; i++) {
    uint64_t calls = __atomic_load_n(&g_c[i].calls, __ATOMIC_RELAXED) - g_snap[i].calls;
    if (!calls)
      continue;
    uint64_t ms = armTicksToNs(__atomic_load_n(&g_c[i].ticks, __ATOMIC_RELAXED) - g_snap[i].ticks) / 1000000ull;
    uint64_t kb = (__atomic_load_n(&g_c[i].bytes, __ATOMIC_RELAXED) - g_snap[i].bytes) >> 10;
    n += snprintf(out + n, cap - (size_t)n, "%s%s %llu x %llu ms", n ? ", " : "; ", k_names[i],
                  (unsigned long long)calls, (unsigned long long)ms);
    if (kb)
      n += snprintf(out + n, cap - (size_t)n, " %llu KB", (unsigned long long)kb);
  }
  return n;
}
