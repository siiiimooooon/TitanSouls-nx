/* ts.h -- what Titan Souls' port files share. MIT. */
#ifndef TS_H
#define TS_H

#include <stdint.h>

#include "jni.h"
#include "so_util.h"

/* ---- ts_loader.c ---- */
extern so_module g_mod_fmodex, g_mod_amazon, g_mod_game;
int ts_load_modules(void);
void ts_run_constructors(void);

/* ---- ts_java.c ---- */
extern JObj *g_activity;
void ts_java_init(void);
const char *ts_obb_android_path(void); /* where the game looks for the OBB */
int ts_obb_present(void);              /* the OBB is there */

/* ---- ts_activity.c ---- */
int ts_activity_run(void);
void ts_request_home(void); /* the game's own quit */

/* ---- ts_input.c ---- */
/* The game's AInputQueue: the activity hands it over in onInputQueueCreated;
 * ts_input_poll (the main thread, once per tick) reads the controllers and
 * the touchscreen and queues the events. */
void *ts_input_queue(void);
void ts_input_init(void);
void ts_input_poll(void);
void ts_input_release_all(void); /* focus lost: every key and finger up */
uint32_t ts_input_events(void);  /* events queued so far (the watchdog) */

/* ---- ts_achievements.c ---- */
void ts_ach_load(void);              /* achievements.txt, at start */
void ts_ach_unlock(const char *id);  /* the game printed "UNLOCK: <id>" */
int ts_ach_count(void);
int ts_ach_unlocked(void);
const char *ts_ach_name(int i);
const char *ts_ach_when(int i);      /* "" while locked */

/* ---- ts_overlay.c ---- */
void ts_overlay_init(void);              /* hooks the frame's present */
void ts_overlay_notice(const char *name); /* "achievement unlocked" for a few seconds */
void ts_overlay_toggle_list(void);       /* the achievements list on / off */
void ts_overlay_open_from_menu(void);    /* opened by the title menu's option (its prompts are under it) */
int ts_overlay_list_open(void);
void ts_overlay_list_move(int d);        /* the list's selection up (-1) / down (+1) */
void ts_overlay_set_language(const char *name); /* the game's "LANGUAGE: %s" */
void ts_font_captured(const void *rgba, int flipped); /* ts_glyphs.c: the font sheet's pixels */
void ts_font_after_upload(void);         /* ts_gl.c, after each glTexImage2D: its GL texture */
void ts_title_captured(int w, int h, unsigned fmt, unsigned type, const void *pixels); /* ts_gl.c: title.png? */

/* ---- ts_menu.c ---- */
void ts_menu_patch(so_module *m); /* staged libTestSuite: the title menu's ACHIEVEMENTS option */

/* ---- ts_sampler.c ---- */
void ts_sampler_start(void);          /* the sampler thread (idle until a long frame) */
void ts_sampler_begin(void);          /* a long frame began: sample */
void ts_sampler_report(unsigned ms);  /* it ended: log the busiest pcs */

#endif
