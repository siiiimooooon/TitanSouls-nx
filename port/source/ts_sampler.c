/* ts_sampler.c -- where the game's threads are during a long frame (a load).
 *
 * The load counters (ts_prof.c) showed the time is not file reads, textures
 * or shaders: two threads busy ~5.5 s each in a 6.4 s load (hardware
 * 2026-11-18, build 202226). To see on what, this samples them: while a long
 * frame is on (from the runtime's snapshot, port_boost_snapshot, to its
 * report, port_boost_format), a thread of ours wakes every 2 ms and, for
 * each guest thread, pauses it, reads its pc and lr (svcGetThreadContext3),
 * and lets it go -- as the runtime's watchdog does, under b_pause_lock. The
 * pcs go into a fixed table (no allocation while a thread is paused: it may
 * hold the heap's lock). At the report the busiest 20 are logged as
 * "[prof] <thread> <module>+<offset> (lr <module>+<offset>): N samples";
 * tools/symbolize_ts.py names them.
 *
 * A thread blocked in the kernel shows up at its svc: those count as waits.
 * MIT.
 */
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <switch.h>

#include "bionic_pthread.h"
#include "ts.h"
#include "util.h"

typedef struct {
  uint64_t r[29];
  uint64_t fp, lr, sp, pc;
  uint32_t psr, _pad;
  uint8_t v[32][16];
  uint32_t fpcr, fpsr;
  uint64_t tpidr;
} KCtx;
_Static_assert(sizeof(KCtx) == 0x320, "kernel ThreadContext is 0x320 bytes");

static Result get_ctx(KCtx *ctx, Handle h) {
  register uint32_t r0 __asm__("r0") = (uint32_t)(uintptr_t)ctx;
  register uint32_t r1 __asm__("r1") = h;
  __asm__ volatile("svc 0x33" : "+r"(r0), "+r"(r1) : : "r2", "r3", "r12", "lr", "memory");
  return r0;
}

#define SLOTS 4096
typedef struct {
  uint32_t pc, lr;
  int tid;
  uint32_t n;
} Slot;

static Slot g_tab[SLOTS];
static uint32_t g_total, g_dropped;
static volatile int g_active;
static Mutex g_tab_lock;
static Thread g_thread;
static int g_self_tid = -1;
static KCtx g_ctx; /* only the sampler thread uses it */

static void add(int tid, uint32_t pc, uint32_t lr) {
  uint32_t h = (pc * 2654435761u ^ (uint32_t)tid * 40503u) & (SLOTS - 1);
  for (int k = 0; k < 64; k++) {
    Slot *s = &g_tab[(h + (uint32_t)k) & (SLOTS - 1)];
    if (s->n && s->pc == pc && s->tid == tid) {
      s->n++;
      return;
    }
    if (!s->n) {
      s->pc = pc;
      s->lr = lr;
      s->tid = tid;
      s->n = 1;
      return;
    }
  }
  g_dropped++;
}

static void sample_one(BThread *t, void *arg) {
  (void)arg;
  if (t->magic != BTHREAD_MAGIC || t->finished || !t->handle || t->tid == g_self_tid || t->gc_paused)
    return;
  b_pause_lock();
  if (R_SUCCEEDED(svcSetThreadActivity(t->handle, ThreadActivity_Paused))) {
    if (R_SUCCEEDED(get_ctx(&g_ctx, t->handle))) {
      add(t->tid, (uint32_t)g_ctx.pc, (uint32_t)g_ctx.r[14]);
      g_total++;
    }
    svcSetThreadActivity(t->handle, ThreadActivity_Runnable);
  }
  b_pause_unlock();
}

static void sampler_main(void *arg) {
  (void)arg;
  g_self_tid = b_gettid();
  for (;;) {
    if (!g_active) {
      svcSleepThread(20000000ll);
      continue;
    }
    mutexLock(&g_tab_lock);
    if (g_active)
      b_thread_foreach(sample_one, NULL);
    mutexUnlock(&g_tab_lock);
    svcSleepThread(2000000ll);
  }
}

void ts_sampler_start(void) {
  if (R_FAILED(threadCreate(&g_thread, sampler_main, NULL, NULL, 0x4000, 0x2B, -2)) ||
      R_FAILED(threadStart(&g_thread)))
    debugPrintf("[prof] no sampler thread\n");
}

/* a long frame began (ts_prof.c's port_boost_snapshot) */
void ts_sampler_begin(void) {
  mutexLock(&g_tab_lock);
  memset(g_tab, 0, sizeof g_tab);
  g_total = g_dropped = 0;
  g_active = 1;
  mutexUnlock(&g_tab_lock);
}

/* 1 when a is in one of the game's modules */
static int where(char *out, size_t cap, uint32_t a) {
  so_module *mods[] = {&g_mod_game, &g_mod_fmodex, &g_mod_amazon};
  for (unsigned i = 0; i < 3; i++) {
    uintptr_t b = (uintptr_t)mods[i]->load_virtbase;
    if (b && a >= b && a < b + mods[i]->load_size) {
      if (out)
        snprintf(out, cap, "%s+0x%x", mods[i]->base_name, (unsigned)(a - b));
      return 1;
    }
  }
  if (out)
    snprintf(out, cap, "rt+0x%08x", (unsigned)a);
  return 0;
}

static const char *tname(int tid) {
  BThread *t = b_thread_by_tid(tid);
  return t && t->name[0] ? t->name : "?";
}

/* the long frame ended (ts_prof.c's port_boost_format): the busiest pcs */
void ts_sampler_report(unsigned frame_ms) {
  mutexLock(&g_tab_lock);
  g_active = 0;
  if (!g_total) {
    mutexUnlock(&g_tab_lock);
    return;
  }
  /* Per thread: samples, distinct pcs, samples in the game's modules. A
   * thread asleep in the kernel is ~1 distinct pc; a busy one, many. (Build
   * 203037 listed only the top pcs: ~20 sleeping threads, 3207 samples each
   * at the same svc, filled it.) */
  enum { MAXT = 48 };
  int tids[MAXT];
  uint32_t per[MAXT], distinct[MAXT], in_game[MAXT];
  int nt = 0;
  for (int i = 0; i < SLOTS; i++) {
    if (!g_tab[i].n)
      continue;
    int k = 0;
    while (k < nt && tids[k] != g_tab[i].tid)
      k++;
    if (k == nt) {
      if (nt == MAXT)
        continue;
      tids[nt] = g_tab[i].tid;
      per[nt] = distinct[nt] = in_game[nt] = 0;
      nt++;
    }
    per[k] += g_tab[i].n;
    distinct[k]++;
    if (where(NULL, 0, g_tab[i].pc))
      in_game[k] += g_tab[i].n;
  }
  debugPrintf("[prof] long frame (%u ms): %u samples, %u not kept, %d threads\n", frame_ms,
              (unsigned)g_total, (unsigned)g_dropped, nt);
  for (int k = 0; k < nt; k++)
    if (distinct[k] > 3)
      debugPrintf("[prof]   busy thread %d (%s): %u samples, %u distinct pcs, %u%% in the game's code\n",
                  tids[k], tname(tids[k]), (unsigned)per[k], (unsigned)distinct[k],
                  per[k] ? (unsigned)(in_game[k] * 100 / per[k]) : 0u);
  /* the busiest pcs: first in the game's modules, then elsewhere (the
   * runtime, the kernel's svcs) but only of the busy threads */
  for (int pass = 0; pass < 2; pass++) {
    int shown = 0;
    while (shown < (pass ? 12 : 30)) {
      int best = -1;
      for (int i = 0; i < SLOTS; i++) {
        if (!g_tab[i].n || where(NULL, 0, g_tab[i].pc) != !pass)
          continue;
        if (pass) {
          int k = 0;
          while (k < nt && tids[k] != g_tab[i].tid)
            k++;
          if (k == nt || distinct[k] <= 3)
            continue;
        }
        if (best < 0 || g_tab[i].n > g_tab[best].n)
          best = i;
      }
      if (best < 0 || g_tab[best].n * 1000 < g_total)
        break;
      char pc[48], lr[48];
      where(pc, sizeof pc, g_tab[best].pc);
      where(lr, sizeof lr, g_tab[best].lr);
      debugPrintf("[prof]   %s %5u  thread %d  %s  (lr %s)\n", pass ? "rt  " : "game", (unsigned)g_tab[best].n,
                  g_tab[best].tid, pc, lr);
      g_tab[best].n = 0;
      shown++;
    }
  }
  mutexUnlock(&g_tab_lock);
}
