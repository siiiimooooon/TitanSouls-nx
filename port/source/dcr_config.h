/* dcr_config.h -- the user's settings, from <game folder>/config.ini (dcr_config.c). */
#ifndef DCR_USER_CONFIG_H
#define DCR_USER_CONFIG_H

typedef struct {
  char locale[8];   /* [game] language: "en", "fr"... ("" = the console's language) */
  int swap_ab;      /* [controls] swap_a_b */
  int touch;        /* [controls] touchscreen */
  int res_w, res_h; /* [display] resolution */
  int boost;        /* [performance] boost_cpu_when_loading (off: stock clocks) */
  int read_buffer_kb; /* [performance] read_buffer_kb: stdio buffer of read-only files (0: newlib's 1 KB) */
  int stream_long_sounds; /* [audio] stream_long_sounds: music and long sounds as FMOD streams (ts_fmod.c) */
  int sound_cache_mb;     /* [audio] sound_cache_mb: decoded effects kept for the next load (ts_fmod.c; 0 off) */
  int switch_icons; /* [controls] button_icons: 1 switch (ts_glyphs.c), 0 original */
  int gl_selftest;  /* [debug] gl_selftest */
  int boot_log;     /* [debug] boot_log_on_screen */
  int log_jni;      /* [debug] log_java_calls */
  int log_input;    /* [debug] log_input */
} DcrConfig;

/* Read config.ini (writing it with the defaults, or adding missing options,
 * first). Early in main(); the defaults hold until then. */
void dcr_config_load(void);
const DcrConfig *dcr_config(void);

#endif
