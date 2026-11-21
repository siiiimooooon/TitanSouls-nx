/* dcr_config.c -- Titan Souls' settings: config.ini's options, on the
 * runtime's INI engine (runtime/source/rt_cfg.c).
 *
 * Written with every option, its default and a line of explanation on the
 * first start; an existing file gets the options a newer build adds, at the
 * end, so edits and comments survive updates. Read once at start-up: changes
 * apply the next time the game starts.
 *
 * The CPU boost while loading is OFF by default: this port is measured and
 * tuned at the Switch's stock clocks. Based on DuckTales-NX (Thorhax). MIT.
 */
#include <stdio.h>
#include <stdlib.h>
#include <strings.h>
#include <switch.h>

#include "dcr_config.h"
#include "rt_cfg.h"
#include "util.h"

static DcrConfig g_cfg = {
    .touch = 1,
    .res_w = 1280,
    .res_h = 720,
    .boost = 0,
    .stream_long_sounds = 1,
    .sound_cache_mb = 96,
    .switch_icons = 1,
};

const DcrConfig *dcr_config(void) { return &g_cfg; }

static const CfgOpt k_opts[] = {
    {"game", "language", "auto",
     "auto (the console's language) or one of: en fr de it es pt ru ja ko zh_CN zh_TW.",
     CFG_TEXT, NULL, NULL, 0, 0, 0, 0},
    CFG_ROW_SWAP_AB("Swap A and B.", &g_cfg.swap_ab),
    {"controls", "touchscreen", "true",
     "The touchscreen works as the phone's.",
     CFG_BOOL, NULL, &g_cfg.touch, 0, 0, 0, 0},
    CFG_ROW_RESOLUTION("auto", CFG_HELP_RESOLUTION),
    {"performance", "boost_cpu_when_loading", "false",
     "Raise the CPU clock while the game loads. Off: the port runs at the\n"
     "# Switch's stock clocks, as it is tested.",
     CFG_BOOL, NULL, &g_cfg.boost, 0, 0, 0, 0},
    {"performance", "read_buffer_kb", "0",
     "Read buffer of the files the game reads (the OBB), in KB (0-1024). 0: the\n"
     "# default 1 KB, each refill one trip to the console's file service.",
     CFG_INT, NULL, &g_cfg.read_buffer_kb, 0, 1024, 0, 0},
    {"audio", "stream_long_sounds", "true",
     "Play the music and other long sounds as streams (decoded while they play)\n"
     "# instead of decoding them whole while the area loads: much shorter loads.\n"
     "# false: as the game asks (the original, slow loads).",
     CFG_BOOL, NULL, &g_cfg.stream_long_sounds, 0, 0, 0, 0},
    {"audio", "sound_cache_mb", "96",
     "Keep the sound effects the game lets go of, decoded, for when it asks for\n"
     "# them again (a retry after dying loads the same area): up to this many MB\n"
     "# (0-512). 0: off (each load decodes them all again).",
     CFG_INT, NULL, &g_cfg.sound_cache_mb, 0, 512, 0, 0},
    {"controls", "button_icons", "switch",
     "The buttons shown on screen: switch (grey buttons, white letters, + - ZL ZR,\n"
     "# and a Switch Lite in the controls screen) or original (the game's own).",
     CFG_CHOICE, "original,switch", &g_cfg.switch_icons, 0, 0, 0, 0},
    CFG_ROW_GL_SELFTEST(&g_cfg.gl_selftest),
    CFG_ROW_BOOT_LOG(CFG_HELP_BOOT_LOG, &g_cfg.boot_log),
    CFG_ROW_LOG_JNI("Write every Java method the game calls to debug.log (slow; for bug reports).",
                    &g_cfg.log_jni),
    {"debug", "log_input", "false", "Write every controller event sent to the game to debug.log.",
     CFG_BOOL, NULL, &g_cfg.log_input, 0, 0, 0, 0},
};

static void apply(void) {
  const RtConfig *rt = rt_config();
  g_cfg.res_w = rt->res_w;
  g_cfg.res_h = rt->res_h;

  const char *lang = rt_config_get("game", "language");
  g_cfg.locale[0] = 0;
  if (lang && *lang && strcasecmp(lang, "auto"))
    snprintf(g_cfg.locale, sizeof g_cfg.locale, "%s", lang);

  int docked = appletGetOperationMode() == AppletOperationMode_Console;
  debugPrintf("[config] language %s; %dx%d (%s); A/B %s; touch %s; CPU boost %s; read buffer %d KB; long sounds %s; sound cache %d MB\n",
              g_cfg.locale[0] ? g_cfg.locale : "auto", g_cfg.res_w, g_cfg.res_h,
              docked ? "docked" : "handheld", g_cfg.swap_ab ? "swapped" : "normal",
              g_cfg.touch ? "on" : "off", g_cfg.boost ? "on" : "off", g_cfg.read_buffer_kb,
              g_cfg.stream_long_sounds ? "streamed" : "as the game asks", g_cfg.sound_cache_mb);
}

static const CfgTable k_table = {
    .opts = k_opts,
    .nopts = CFG_COUNT(k_opts),
    .version = 1,
    .apply = apply,
};

void dcr_config_load(void) { rt_config_load(&k_table); }
