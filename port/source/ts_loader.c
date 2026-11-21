/* ts_loader.c -- loading Titan Souls' three libraries.
 *
 * The APK carries three armeabi-v7a libraries, all loaded, in the order the
 * game's own one needs them (its DT_NEEDED: libfmodex.so, then
 * libAmazonGamesJni.so):
 *   libfmodex.so          FMOD Ex: the mixer and its outputs (OpenSL ES,
 *                         dlopen'ed; Java's AudioTrack is not used: TVActivity
 *                         never creates org.fmod.FMODAudioDevice)
 *   libAmazonGamesJni.so  Amazon GameCircle (Whispersync, achievements,
 *                         leaderboards): loaded so its symbols bind; the
 *                         Java side it talks to is not there, so it never
 *                         connects
 *   libTestSuite.so       Acid Nerve's engine and the game, a NativeActivity
 *                         (android_native_app_glue); imports both of the above
 * Each binds its imports to the shims first and then to the modules loaded
 * before it (so_util.c), so the order matters. Nothing writes code at run
 * time: each module is mapped and sealed in one go.
 *
 * Fixes to the game's code go in k_game_fixes (each checks the instruction it
 * replaces, so another build of the game is left alone). None yet.
 * Based on DuckTales-NX (Thorhax). MIT.
 */
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <switch.h>

#include "config.h"
#include "dcr_path.h"
#include "imports.h"
#include "ts.h"
#include "util.h"

so_module g_mod_fmodex, g_mod_amazon, g_mod_game;

typedef struct {
  const char *what;
  uint32_t off, old, new_;
} Fix;

static const Fix k_game_fixes[] = {
    {NULL, 0, 0, 0}, /* none yet */
};

static void apply_fixes(so_module *mod, const Fix *fixes, unsigned n) {
  for (unsigned i = 0; i < n; i++) {
    if (!fixes[i].what)
      continue;
    uint32_t *w = (uint32_t *)((uint8_t *)mod->load_base + fixes[i].off);
    if (fixes[i].off + 4 > mod->load_size || *w != fixes[i].old) {
      debugPrintf("[fix] %s: not applied (0x%x holds %08x, not %08x: another build?)\n", fixes[i].what,
                  (unsigned)fixes[i].off, fixes[i].off + 4 > mod->load_size ? 0u : (unsigned)*w,
                  (unsigned)fixes[i].old);
      continue;
    }
    *w = fixes[i].new_;
    debugPrintf("[fix] %s: applied at %s+0x%x\n", fixes[i].what, mod->base_name, (unsigned)fixes[i].off);
  }
}

/* libgcc's kernel helpers, as DATA. The runtime (so_fix_kuser_helpers)
 * rewrites the kuser addresses it finds in a module's executable segments:
 * the literal pools libgcc's __sync_* calls load them from. libTestSuite.so
 * keeps them elsewhere: 19 pointers to 0xffff0fc0 (__kuser_cmpxchg) and 19
 * to 0xffff0fa0 (__kuser_memory_barrier) in .data (0x39eeb0.., 0x39eefc..),
 * static function pointers its atomics load and call with blx. Horizon has
 * nothing at 0xffff0000: hardware 2026-11-18, first launch, an instruction
 * abort at pc ffff0fa0 from a constructor (lr libTestSuite+0x2830ad, which
 * had loaded the pointer at 0x39eefc). So the same rewrite is done here over
 * the WRITABLE segments, before the module is sealed: each such word is
 * pointed at the runtime's kuser.S. Only the two exact values are touched. */
void dcr_kuser_cmpxchg(void);        /* runtime/source/kuser.S */
void dcr_kuser_memory_barrier(void); /* runtime/source/kuser.S */

static void fix_kuser_data(so_module *m) {
  if (m->state != SO_STAGED) {
    debugPrintf("[fix] %s: kuser data pointers: module not staged, not rewritten\n", m->base_name);
    return;
  }
  uint8_t *img = (uint8_t *)m->load_base;
  int cmpxchg = 0, barrier = 0;
  for (int i = 0; i < m->phnum; i++) {
    const Elf32_Phdr *ph = &m->phdr[i];
    if (ph->p_type != PT_LOAD || (ph->p_flags & PF_X) || !(ph->p_flags & PF_W))
      continue;
    uint32_t *w = (uint32_t *)((uintptr_t)(img + ph->p_vaddr + 3) & ~3u);
    size_t nw = ph->p_filesz / 4;
    for (size_t k = 0; k < nw; k++) {
      if (w[k] == 0xffff0fc0u) {
        w[k] = (uint32_t)(uintptr_t)dcr_kuser_cmpxchg;
        cmpxchg++;
      } else if (w[k] == 0xffff0fa0u) {
        w[k] = (uint32_t)(uintptr_t)dcr_kuser_memory_barrier;
        barrier++;
      }
    }
  }
  debugPrintf("[fix] %s: kuser helpers in data -> kuser.S (%d cmpxchg, %d barrier)\n", m->base_name,
              cmpxchg, barrier);
}

static int load_one(so_module *mod, const char *name) {
  char path[512];
  snprintf(path, sizeof path, "%s/%s", dcr_game_root(), name);
  int rc = so_load(mod, path, NULL, PORT_SO_REGION_BYTES);
  if (rc < 0) {
    const char *why = rc == -1 ? "cannot open it, or it is not a 32-bit ARM ELF"
                    : rc == -2 ? "out of memory"
                    : rc == -3 ? "larger than PORT_SO_REGION_BYTES"
                    : rc == -4 ? "too many program headers" : "?";
    debugPrintf("[boot] so_load(%s) failed rc=%d: %s\n", path, rc, why);
    return -1;
  }
  so_relocate(mod);
  int fixed = so_fix_kuser_helpers(mod);
  fix_kuser_data(mod);
  int missing = so_resolve(mod, dcr_imports, dcr_imports_count, 1);
  debugPrintf("[boot] %s %u KB staged %p -> %p (%d unresolved imports, %d kuser fixes)\n",
              mod->base_name, (unsigned)(mod->load_size >> 10), mod->load_base,
              mod->load_virtbase, missing, fixed);
  if (mod == &g_mod_game) {
    apply_fixes(mod, k_game_fixes, sizeof k_game_fixes / sizeof k_game_fixes[0]);
    ts_menu_patch(mod); /* ACHIEVEMENTS in the title menu (ts_menu.c) */
  }
  so_finalize(mod);
  so_flush_caches(mod);
  return 0;
}

int ts_load_modules(void) {
  if (load_one(&g_mod_fmodex, TS_LIB_FMODEX) || load_one(&g_mod_amazon, TS_LIB_AMAZON) ||
      load_one(&g_mod_game, TS_LIB_GAME))
    return -1;
  return 0;
}

/* System.loadLibrary runs each library's constructors, in load order. */
void ts_run_constructors(void) {
  extern int g_so_trace_ctors;
  g_so_trace_ctors = dcr_is_emulator();
  so_module *mods[] = {&g_mod_fmodex, &g_mod_amazon, &g_mod_game};
  for (unsigned i = 0; i < 3; i++) {
    u64 t0 = armGetSystemTick();
    so_execute_init_array(mods[i]);
    debugPrintf("[boot] %s constructors done in %llu ms\n", mods[i]->base_name,
                (unsigned long long)(armTicksToNs(armGetSystemTick() - t0) / 1000000ull));
  }
}
