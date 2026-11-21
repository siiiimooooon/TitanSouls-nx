/* ts_achievements.c -- Titan Souls' achievements, kept on the console.
 *
 * The game has 27 achievements and decides itself when one is earned: it
 * reports it to Google Play Games (not there on a console, so it went
 * nowhere) and prints "UNLOCK: <ID>" on stdout (hardware 2026-11-18:
 * "UNLOCK: FIRST_GATE", "UNLOCK: FIRST_BLOOD" in the logs of the first
 * sessions). So the game's printf is wrapped (ts_imports_extra.c), and that
 * line becomes ts_ach_unlock(ID): the achievement is written to
 * <game folder>/achievements.txt (one "ID date" line each, read at start)
 * and a notice is shown over the game for a few seconds (ts_overlay.c).
 * Builds before 1.0.1 named the file logros.txt: when there is no
 * achievements.txt yet, that one is read and saved under the new name.
 *
 * The names are the game's own (English): it keeps them only inside its
 * localization keys, "FIRST_GATE_name_Out_of_the_Frying_Pan" and so on; it
 * has no descriptions (those were Google Play's). MIT.
 */
#include <stdio.h>
#include <string.h>
#include <switch.h>
#include <time.h>

#include "dcr_path.h"
#include "ts.h"
#include "util.h"

static const struct {
  const char *id, *name;
} k_ach[] = {
    {"FIRST_BLOOD", "First Blood"},
    {"FIRST_GATE", "Out of the Frying Pan"},
    {"SECOND_GATE", "Into the Fire"},
    {"TITAN_SOUL", "Titan Soul"},
    {"TRUTH_SOUL", "TRUTH"},
    {"SPEED_RUN", "Aerodynamics"},
    {"IRON_NORMAL", "Iron Human"},
    {"IRON_HARD", "Iron Titan"},
    {"IRON_TRUTH", "Iron God"},
    {"NO_ROLLS", "Hard Bizkit"},
    {"HARD_MODE", "New Game"},
    {"SLIMEBALL", "Slimeball"},
    {"EYECUBE", "Laser Eye Surgery"},
    {"BRAIN", "Brain Freeze"},
    {"PLANT_VINES", "Short Back and Sides"},
    {"BLAZEIT", "Drug Trial"},
    {"ROCKY", "Demo Man"},
    {"AVARICE", "Cash Money"},
    {"ROLLER", "COME AT ME BRO"},
    {"BOMBER", "Bomberman"},
    {"IK_SPIN", "Leak Spin"},
    {"BEAT_YETI", "Beating the Yeti"},
    {"DENTIST", "Dental Plan"},
    {"SNAKE", "Titanic"},
    {"KING", "A Collision of Souls"},
    {"SHAD_COLOS", "Shadow of the Colossus"},
    {"GUARDIAN", "The Switch"},
};
#define N_ACH ((int)(sizeof k_ach / sizeof k_ach[0]))

static char g_when[N_ACH][20]; /* "" = locked, else "2026-11-18 21:40" */
static Mutex g_lock;

int ts_ach_count(void) { return N_ACH; }
const char *ts_ach_name(int i) { return i >= 0 && i < N_ACH ? k_ach[i].name : ""; }
const char *ts_ach_when(int i) { return i >= 0 && i < N_ACH ? g_when[i] : ""; }

int ts_ach_unlocked(void) {
  int n = 0;
  for (int i = 0; i < N_ACH; i++)
    n += g_when[i][0] != 0;
  return n;
}

static void file_path(char *buf, size_t cap) { snprintf(buf, cap, "%s/achievements.txt", dcr_game_root()); }
static void legacy_path(char *buf, size_t cap) { snprintf(buf, cap, "%s/logros.txt", dcr_game_root()); }

static void save_locked(void) {
  char path[512];
  file_path(path, sizeof path);
  FILE *f = fopen(path, "w");
  if (!f) {
    debugPrintf("[achievements] could not write %s\n", path);
    return;
  }
  fprintf(f, "# Titan Souls: unlocked achievements (ID date). Written by the port.\n");
  for (int i = 0; i < N_ACH; i++)
    if (g_when[i][0])
      fprintf(f, "%s %s\n", k_ach[i].id, g_when[i]);
  fclose(f);
}

void ts_ach_load(void) {
  mutexInit(&g_lock);
  char path[512], line[128];
  file_path(path, sizeof path);
  FILE *f = fopen(path, "r");
  int legacy = 0;
  if (!f) {
    legacy_path(path, sizeof path);
    f = fopen(path, "r");
    legacy = f != NULL;
  }
  if (f) {
    while (fgets(line, sizeof line, f)) {
      if (line[0] == '#')
        continue;
      char id[32], when[20] = "";
      if (sscanf(line, "%31s %19[^\r\n]", id, when) < 1)
        continue;
      for (int i = 0; i < N_ACH; i++)
        if (!strcmp(id, k_ach[i].id))
          snprintf(g_when[i], sizeof g_when[i], "%s", when[0] ? when : "?");
    }
    fclose(f);
  }
  if (legacy)
    save_locked(); /* the old file's achievements, now in achievements.txt */
  debugPrintf("[achievements] %d of %d unlocked (%s%s)\n", ts_ach_unlocked(), N_ACH, path,
              legacy ? ", moved to achievements.txt" : "");
}

void ts_ach_unlock(const char *id) {
  if (!id)
    return;
  int idx = -1;
  for (int i = 0; i < N_ACH; i++)
    if (!strcmp(id, k_ach[i].id))
      idx = i;
  if (idx < 0) {
    debugPrintf("[achievements] unknown achievement \"%s\"\n", id);
    return;
  }
  mutexLock(&g_lock);
  int fresh = !g_when[idx][0];
  if (fresh) {
    time_t t = time(NULL);
    struct tm tmv;
    localtime_r(&t, &tmv);
    strftime(g_when[idx], sizeof g_when[idx], "%Y-%m-%d %H:%M", &tmv);
    save_locked();
  }
  mutexUnlock(&g_lock);
  debugPrintf("[achievements] %s (%s): %s\n", id, k_ach[idx].name, fresh ? "UNLOCKED" : "already had it");
  if (fresh)
    ts_overlay_notice(k_ach[idx].name);
}
