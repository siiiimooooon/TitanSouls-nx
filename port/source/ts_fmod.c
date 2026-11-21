/* ts_fmod.c -- how Titan Souls' sounds are created in FMOD Ex.
 *
 * Why: the long loads (6.4 s entering an area, 3.5 s and 2 s others) are
 * FMOD: the sampler (ts_sampler.c, hardware 2026-11-18, build 203538) found
 * one thread busy the whole load, 70 % of its samples inside libfmodex.so,
 * while the game's main loop only drew the loading screen. The game creates
 * its sounds -- the bosses' music among them (SOUND/MUSIC/BOSS/...ogg) -- as
 * samples: FMOD decodes each Ogg Vorbis file whole to PCM before it can
 * play, one core of a 1 GHz A57 for several seconds.
 *
 * What: System::createSound (the game's calls into libfmodex bind through
 * port_import_interpose) is wrapped. A LONG sound -- its name has "/MUSIC/",
 * or its data is at least 256 KB compressed -- is made a STREAM instead of a
 * sample: FMOD decodes it a little at a time while it plays, so creating it
 * takes no time. Short sounds (effects, which may play several at once, which
 * a stream cannot) are left as the game asks. FMOD_OPENMEMORY data is
 * copied for a stream (FMOD streams out of the caller's buffer, which the
 * game may free) and the copy freed in Sound::release. If FMOD refuses the
 * stream, the game's own call is made instead. [audio] stream_long_sounds in
 * config.ini turns it off (false: as the game asks, for A/B).
 *
 * That took the big load from 6.4 to 4.9 s (hardware 2026-11-18, build
 * 204130): what was left were ~120 short effects (11-200 KB each, 6 MB in
 * all) decoded one after another -- and the game creates them again every
 * time the area loads. FMOD_CREATECOMPRESSEDSAMPLE for them was tried
 * (build 204817): FMOD answered OK but took exactly as long -- this FMOD Ex
 * decodes Ogg whole anyway -- so it is not used. What is left (~3.4 s of a
 * 4.9 s load) could go only by keeping decoded effects across reloads of the
 * same area (a retry after dying):
 *
 * The cache. An effect (a sample from memory) the game lets go of with
 * Sound::release is not released: its channels are stopped and it is
 * parked, by a hash of its compressed data, its length and the mode. When
 * the game creates a sound from the same data again, the parked one is
 * handed back -- already decoded, no time. Parked sounds take at most
 * [audio] sound_cache_mb (default 96) of PCM; past that the ones parked
 * longest ago are really released. A sound the game still holds is never
 * handed out twice (a second request decodes a second one). 0 turns it off.
 *
 * Every createSound is timed into the load counters (ts_prof.c: "sound") and
 * the first ones are logged with their name or size, mode, result and time.
 * MIT.
 */
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <switch.h>

#include "dcr_config.h"
#include "so_util.h"
#include "ts.h"
#include "util.h"

uint64_t ts_prof_begin(void);
void ts_prof_end(int which, uint64_t t0, uint64_t bytes);
enum { P_SOUND = 6 }; /* ts_prof.c */

/* FMOD Ex 4.x (fmod.h) */
#define FMOD_CREATESTREAM 0x00000080u
#define FMOD_CREATESAMPLE 0x00000100u
#define FMOD_CREATECOMPRESSEDSAMPLE 0x00000200u
#define FMOD_OPENUSER 0x00000400u
#define FMOD_OPENMEMORY 0x00000800u
#define FMOD_OPENMEMORY_POINT 0x10000000u
#define FMOD_NONBLOCKING 0x00010000u

typedef struct {
  int cbsize;
  unsigned int length; /* the data's size, for FMOD_OPENMEMORY */
  /* ... the rest is FMOD's; not touched */
} ExInfoHead;

typedef int (*fn_create_sound)(void *sys, const char *name_or_data, unsigned mode, void *exinfo, void **sound);
typedef int (*fn_release)(void *sound);
static fn_create_sound r_create_sound;
static fn_release r_release;

#define LONG_BYTES (256u * 1024u)

/* the buffers kept for memory streams, by sound */
#define KEPT 128
static struct {
  void *sound, *buf;
} g_kept[KEPT];
static Mutex g_kept_lock;

static void keep(void *sound, void *buf) {
  mutexLock(&g_kept_lock);
  for (int i = 0; i < KEPT; i++)
    if (!g_kept[i].sound) {
      g_kept[i].sound = sound;
      g_kept[i].buf = buf;
      mutexUnlock(&g_kept_lock);
      return;
    }
  mutexUnlock(&g_kept_lock);
  debugPrintf("[fmod] more than %d memory streams alive: a buffer is never freed\n", KEPT);
}

/* ---------------------------------------------------------------- the cache */
#define FMOD_TIMEUNIT_PCMBYTES 0x00000004u
#define CACHE 512
typedef struct {
  uint64_t hash, parked_at;
  unsigned len, mode, pcm;
  void *sound;
  int alive; /* the game holds it */
} Cached;
static Cached g_cache[CACHE];
static int g_ncache;
static uint64_t g_parked; /* PCM bytes of the parked ones */
static unsigned g_hits, g_misses;
static Mutex g_cache_lock;

static int (*f_get_length)(void *sound, unsigned *len, unsigned unit);
static int (*f_get_system)(void *sound, void **sys);
static int (*f_get_channel)(void *sys, int index, void **ch);
static int (*f_ch_sound)(void *ch, void **sound);
static int (*f_ch_stop)(void *ch);

static int cache_ready(void) {
  static int state; /* 0 not tried, 1 ok, -1 not available */
  if (!state) {
    f_get_length = (void *)so_try_find_addr_rx(&g_mod_fmodex, "_ZN4FMOD5Sound9getLengthEPjj");
    f_get_system = (void *)so_try_find_addr_rx(&g_mod_fmodex, "_ZN4FMOD5Sound15getSystemObjectEPPNS_6SystemE");
    f_get_channel = (void *)so_try_find_addr_rx(&g_mod_fmodex, "_ZN4FMOD6System10getChannelEiPPNS_7ChannelE");
    f_ch_sound = (void *)so_try_find_addr_rx(&g_mod_fmodex, "_ZN4FMOD7Channel15getCurrentSoundEPPNS_5SoundE");
    f_ch_stop = (void *)so_try_find_addr_rx(&g_mod_fmodex, "_ZN4FMOD7Channel4stopEv");
    state = f_get_length && f_get_system && f_get_channel && f_ch_sound && f_ch_stop ? 1 : -1;
    debugPrintf("[fmod] sound cache: %s, up to %d MB\n", state > 0 ? "on" : "FMOD calls not found, off",
                dcr_config()->sound_cache_mb);
  }
  return state > 0 && dcr_config()->sound_cache_mb > 0;
}

static uint64_t hash_data(const void *p, unsigned n) {
  uint64_t h = 1469598103934665603ull;
  const uint8_t *b = p;
  unsigned i = 0;
  for (; i + 8 <= n; i += 8) {
    uint64_t w;
    memcpy(&w, b + i, 8);
    h = (h ^ w) * 1099511628211ull;
  }
  for (; i < n; i++)
    h = (h ^ b[i]) * 1099511628211ull;
  return h;
}

/* every channel playing it, stopped (as releasing it would) */
static void stop_channels(void *sound) {
  void *sys = NULL;
  if (f_get_system(sound, &sys) || !sys)
    return;
  for (int i = 0; i < 4096; i++) {
    void *ch = NULL, *cur = NULL;
    if (f_get_channel(sys, i, &ch))
      break; /* past the channel pool */
    if (ch && !f_ch_sound(ch, &cur) && cur == sound)
      f_ch_stop(ch);
  }
}

/* a parked one for this data, now the game's again; NULL if none */
static void *cache_take(uint64_t hash, unsigned len, unsigned mode) {
  void *s = NULL;
  mutexLock(&g_cache_lock);
  for (int i = 0; i < g_ncache; i++) {
    Cached *c = &g_cache[i];
    if (!c->alive && c->hash == hash && c->len == len && c->mode == mode) {
      c->alive = 1;
      g_parked -= c->pcm;
      s = c->sound;
      break;
    }
  }
  if (s)
    g_hits++;
  else
    g_misses++;
  mutexUnlock(&g_cache_lock);
  return s;
}

static void cache_add(void *sound, uint64_t hash, unsigned len, unsigned mode) {
  unsigned pcm = 0;
  if (f_get_length(sound, &pcm, FMOD_TIMEUNIT_PCMBYTES))
    return; /* not counted: not cached */
  mutexLock(&g_cache_lock);
  if (g_ncache < CACHE)
    g_cache[g_ncache++] = (Cached){hash, 0, len, mode, pcm, sound, 1};
  if (g_misses % 50 == 0)
    debugPrintf("[fmod] sound cache: %u hits, %u decoded, %d known, %llu KB parked\n", g_hits, g_misses, g_ncache,
                (unsigned long long)(g_parked >> 10));
  mutexUnlock(&g_cache_lock);
}

/* the game let it go: parked if it is ours (1), else 0. Over the budget,
 * the ones parked longest ago are released for real. */
static int cache_park(void *sound) {
  mutexLock(&g_cache_lock);
  int at = -1;
  for (int i = 0; i < g_ncache; i++)
    if (g_cache[i].sound == sound && g_cache[i].alive) {
      at = i;
      break;
    }
  if (at < 0) {
    mutexUnlock(&g_cache_lock);
    return 0;
  }
  mutexUnlock(&g_cache_lock);
  stop_channels(sound);
  mutexLock(&g_cache_lock);
  g_cache[at].alive = 0;
  g_cache[at].parked_at = armGetSystemTick();
  g_parked += g_cache[at].pcm;
  uint64_t budget = (uint64_t)dcr_config()->sound_cache_mb << 20;
  unsigned evicted = 0;
  while (g_parked > budget) {
    int old = -1;
    for (int i = 0; i < g_ncache; i++)
      if (!g_cache[i].alive && (old < 0 || g_cache[i].parked_at < g_cache[old].parked_at))
        old = i;
    if (old < 0)
      break;
    r_release(g_cache[old].sound);
    g_parked -= g_cache[old].pcm;
    g_cache[old] = g_cache[--g_ncache];
    evicted++;
  }
  if (evicted)
    debugPrintf("[fmod] sound cache over %d MB: %u released, %llu KB parked\n", dcr_config()->sound_cache_mb,
                evicted, (unsigned long long)(g_parked >> 10));
  mutexUnlock(&g_cache_lock);
  return 1;
}

static int w_release(void *sound) {
  if (cache_park(sound))
    return 0; /* FMOD_OK: as far as the game knows, released */
  int r = r_release(sound);
  mutexLock(&g_kept_lock);
  for (int i = 0; i < KEPT; i++)
    if (g_kept[i].sound == sound) {
      free(g_kept[i].buf);
      g_kept[i].sound = g_kept[i].buf = NULL;
      break;
    }
  mutexUnlock(&g_kept_lock);
  return r;
}

static int w_create_sound(void *sys, const char *name_or_data, unsigned mode, void *exinfo, void **sound) {
  static unsigned calls;
  unsigned n = ++calls;
  int from_memory = (mode & (FMOD_OPENMEMORY | FMOD_OPENMEMORY_POINT)) != 0;
  unsigned bytes = from_memory && exinfo ? ((ExInfoHead *)exinfo)->length : 0;
  int is_long = (!from_memory && name_or_data && strstr(name_or_data, "/MUSIC/")) ||
                (from_memory && bytes >= LONG_BYTES);
  int stream = dcr_config()->stream_long_sounds && is_long && !(mode & (FMOD_CREATESTREAM | FMOD_OPENUSER));

  uint64_t t0 = ts_prof_begin();
  int cacheable = !stream && from_memory && bytes && name_or_data && sound &&
                  !(mode & (FMOD_CREATESTREAM | FMOD_OPENUSER | FMOD_NONBLOCKING | FMOD_OPENMEMORY_POINT)) &&
                  cache_ready();
  uint64_t hash = 0;
  if (cacheable) {
    hash = hash_data(name_or_data, bytes);
    void *s = cache_take(hash, bytes, mode);
    if (s) {
      *sound = s;
      ts_prof_end(P_SOUND, t0, bytes);
      if (g_hits <= 10 || g_hits % 100 == 0)
        debugPrintf("[fmod] createSound #%u memory %u KB: from the cache (hit %u, %u misses) in %llu us\n", n,
                    bytes >> 10, g_hits, g_misses,
                    (unsigned long long)(armTicksToNs(armGetSystemTick() - t0) / 1000ull));
      return 0;
    }
  }
  int r = -1;
  unsigned used = mode;
  if (stream) {
    used = (mode & ~(FMOD_CREATESAMPLE | FMOD_CREATECOMPRESSEDSAMPLE | FMOD_OPENMEMORY_POINT)) | FMOD_CREATESTREAM;
    void *copy = NULL;
    const char *src = name_or_data;
    if (from_memory) {
      copy = malloc(bytes ? bytes : 1);
      if (copy) {
        memcpy(copy, name_or_data, bytes);
        src = copy;
        used |= FMOD_OPENMEMORY;
      }
    }
    if (!from_memory || copy) {
      r = r_create_sound(sys, src, used, exinfo, sound);
      if (r == 0 && copy && sound && *sound)
        keep(*sound, copy);
      else
        free(copy);
    }
    if (r != 0) {
      debugPrintf("[fmod] createSound #%u: as a stream -> %d; as the game asked instead\n", n, r);
      used = mode;
    }
  }
  if (r != 0)
    r = r_create_sound(sys, name_or_data, mode, exinfo, sound);
  if (cacheable && r == 0 && *sound)
    cache_add(*sound, hash, bytes, mode);
  ts_prof_end(P_SOUND, t0, bytes);

  if (n <= 60 || r != 0) {
    uint64_t ms = armTicksToNs(armGetSystemTick() - t0) / 1000000ull;
    char what[96];
    if (from_memory)
      snprintf(what, sizeof what, "memory %u KB", bytes >> 10);
    else
      snprintf(what, sizeof what, "%.80s", name_or_data ? name_or_data : "(null)");
    debugPrintf("[fmod] createSound #%u %s mode 0x%x%s -> %d in %llu ms\n", n, what, mode,
                used == mode ? "" : " (made a stream)", r,
                (unsigned long long)ms);
  }
  return r;
}

void *port_import_interpose(const char *sym, void *real) {
  if (!real)
    return real;
  if (!strcmp(sym, "_ZN4FMOD6System11createSoundEPKcjP22FMOD_CREATESOUNDEXINFOPPNS_5SoundE")) {
    static int once;
    if (!once++) {
      mutexInit(&g_kept_lock);
      mutexInit(&g_cache_lock);
    }
    r_create_sound = (fn_create_sound)real;
    return (void *)w_create_sound;
  }
  if (!strcmp(sym, "_ZN4FMOD5Sound7releaseEv")) {
    r_release = (fn_release)real;
    return (void *)w_release;
  }
  return real;
}
