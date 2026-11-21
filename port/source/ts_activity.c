/* ts_activity.c -- plays Android's part of a NativeActivity for Titan Souls.
 *
 * TVActivity extends android.app.NativeActivity. On a phone the
 * framework loads the libraries (its static initializer), then:
 *   onCreate          ANativeActivity_onCreate(activity, savedState, size):
 *                     the engine's android_native_app_glue fills
 *                     activity->callbacks, makes its command pipe and starts
 *                     the game's own thread (android_main), which loops on
 *                     its ALooper: commands from the pipe, input from the
 *                     AInputQueue
 *   onStart, onResume                       callbacks->onStart / onResume
 *   the input queue                         callbacks->onInputQueueCreated
 *   the window (a SurfaceView's surface)    callbacks->onNativeWindowCreated
 *                     -> APP_CMD_INIT_WINDOW: the engine makes its EGL
 *                     surface and context on its own thread and starts
 *                     drawing; eglSwapBuffers presents (gl_mesa.c)
 *   focus                                   callbacks->onWindowFocusChanged
 * and from then on the framework's UI thread only delivers input and
 * lifecycle changes. Each glue callback writes its command to the pipe and
 * waits until the game's thread has taken it -- as on Android.
 *
 * So here: the main thread builds the ANativeActivity (its JavaVM and JNIEnv
 * are jni_core.c's, its clazz the TVActivity object of ts_java.c),
 * calls onCreate and the callbacks in that order, and then plays the UI
 * thread: it reads the controllers and the touchscreen into the input queue
 * (ts_input.c) and turns the Switch's focus changes into onPause / onResume
 * and focus changes. HOME > Close is Android's onPause then the process
 * being killed: onPause (the engine saves), then the process ends, with a
 * 5 s backstop. Based on DuckTales-NX (Thorhax). MIT.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <switch.h>

#include "config.h"
#include "dcr_config.h"
#include "dcr_path.h"
#include "ts.h"
#include "error.h"
#include "gl_layer.h"
#include "rt_applet.h"
#include "rt_window.h"
#include "util.h"
#include "watchdog.h"

void dcr_boost_poll(void);
void dcr_boost_report(void);
void dcr_boost_launch_end(void);
void dcr_apkcache_report(void);
void *b_ANativeWindow_fromSurface(void *env, void *surface);
uint32_t dcr_audio_writes(void); /* opensles.c */

/* ---- Android's NativeActivity ABI (android/native_activity.h), 32-bit ---- */
typedef struct ANativeActivity ANativeActivity;
typedef struct {
  int32_t left, top, right, bottom;
} ARect;

typedef struct {
  void (*onStart)(ANativeActivity *);
  void (*onResume)(ANativeActivity *);
  void *(*onSaveInstanceState)(ANativeActivity *, size_t *out_size);
  void (*onPause)(ANativeActivity *);
  void (*onStop)(ANativeActivity *);
  void (*onDestroy)(ANativeActivity *);
  void (*onWindowFocusChanged)(ANativeActivity *, int has_focus);
  void (*onNativeWindowCreated)(ANativeActivity *, void *window);
  void (*onNativeWindowResized)(ANativeActivity *, void *window);
  void (*onNativeWindowRedrawNeeded)(ANativeActivity *, void *window);
  void (*onNativeWindowDestroyed)(ANativeActivity *, void *window);
  void (*onInputQueueCreated)(ANativeActivity *, void *queue);
  void (*onInputQueueDestroyed)(ANativeActivity *, void *queue);
  void (*onContentRectChanged)(ANativeActivity *, const ARect *rect);
  void (*onConfigurationChanged)(ANativeActivity *);
  void (*onLowMemory)(ANativeActivity *);
} ANativeActivityCallbacks;

struct ANativeActivity {
  ANativeActivityCallbacks *callbacks;
  void *vm;  /* JavaVM* */
  void *env; /* JNIEnv* of the main (UI) thread */
  void *clazz; /* the NativeActivity Java object */
  const char *internalDataPath;
  const char *externalDataPath;
  int32_t sdkVersion;
  void *instance; /* the glue's android_app */
  void *assetManager;
  const char *obbPath;
};
_Static_assert(sizeof(ANativeActivity) == 40, "ANativeActivity is 40 bytes on armeabi-v7a");
_Static_assert(sizeof(ANativeActivityCallbacks) == 64, "16 callbacks");

typedef void (*fn_on_create)(ANativeActivity *, void *saved_state, size_t saved_size);
typedef int (*fn_onload)(void *vm, void *reserved);

static ANativeActivityCallbacks g_cb;
static ANativeActivity g_act;
static void *g_window;
static volatile int g_quit;
static int g_resumed, g_focused;

/* For the watchdog: frames presented. */
uint64_t dcr_boot_frames(void) { return dcr_gl_frames(); }

void ts_request_home(void) { g_quit = 1; }

/* One callback, logged, if the glue set it. */
#define CALL(name, ...)                                   \
  do {                                                    \
    if (g_cb.name) {                                      \
      debugPrintf("[activity] " #name "\n");              \
      g_cb.name(&g_act, ##__VA_ARGS__);                   \
    }                                                     \
  } while (0)

static void do_resume(void) {
  if (!g_resumed) {
    CALL(onResume);
    g_resumed = 1;
  }
  if (!g_focused) {
    CALL(onWindowFocusChanged, 1);
    g_focused = 1;
  }
}

static void do_pause(void) {
  if (g_focused) {
    ts_input_release_all();
    CALL(onWindowFocusChanged, 0);
    g_focused = 0;
  }
  if (g_resumed) {
    CALL(onPause);
    g_resumed = 0;
  }
}

/* ------------------------------------------------------------- lifecycle */
/* HOME, sleep, an overlay (rt_applet.c calls these from the frame loop's
 * rt_applet_poll). */
void port_focus_lost(void) { do_pause(); }
void port_focus_gained(void) { do_resume(); }

static void exit_guard(void *arg) {
  (void)arg;
  svcSleepThread(5000000000ll);
  debugPrintf("[activity] the game did not close within 5 s: ending the process\n");
  log_flush_ring();
  svcExitProcess();
}

static void exit_guard_start(void) {
  static Thread t;
  if (R_FAILED(threadCreate(&t, exit_guard, NULL, NULL, 0x4000, 0x2B, -2)) ||
      R_FAILED(threadStart(&t)))
    debugPrintf("[activity] no exit backstop thread\n");
}

static void report(void) {
  static u64 last_tick;
  static unsigned long last_presented;
  u64 tick = armGetSystemTick();
  unsigned long presented = (unsigned long)dcr_gl_frames();
  double fps = last_tick ? (double)(presented - last_presented) * 1e9 /
                               (double)armTicksToNs(tick - last_tick)
                         : 0.0;
  last_tick = tick;
  last_presented = presented;
  debugPrintf("[activity] %lu frames presented (%.1f fps), %u input events, %d Java objects; "
              "audio: %u buffers\n",
              presented, fps, (unsigned)ts_input_events(), jni_live_objects(),
              (unsigned)dcr_audio_writes());
  dcr_boost_report();
  dcr_apkcache_report();
}

static void housekeeping(int *launch_done, unsigned long *quiet_at, u64 *last_report) {
  dcr_boost_poll();
  unsigned long frames = (unsigned long)dcr_gl_frames();
  if (!*launch_done && frames > 0) {
    *launch_done = 1;
    dcr_boost_launch_end();
    debugPrintf("[activity] first frame presented\n");
    *quiet_at = frames + 600;
  }
  /* From ~10 s after the first picture the log goes to a RAM ring (util.c),
   * written out every 10 s and by the watchdog. */
  if (*quiet_at && frames >= *quiet_at) {
    *quiet_at = 0;
    log_set_quiet(1);
  }
  u64 now = armGetSystemTick();
  if (armTicksToNs(now - *last_report) >= 10000000000ull) {
    *last_report = now;
    report();
    log_flush_ring();
  }
}

static uint32_t input_counter(void) { return ts_input_events(); }

int ts_activity_run(void) {
  fn_on_create on_create = (fn_on_create)so_try_find_addr_rx(&g_mod_game, "ANativeActivity_onCreate");
  if (!on_create)
    fatal_error("%s has no ANativeActivity_onCreate: is the APK Titan Souls " TS_VERSION "?",
                TS_LIB_GAME);

  ts_java_init();
  ts_input_init();
  ts_ach_load();     /* achievements.txt */
  ts_overlay_init(); /* the achievements' notice and list, over each frame */
  dcr_watchdog_start();
  rt_watchdog_add_counter("input events", input_counter);
  ts_sampler_start();

  /* ---- System.loadLibrary("TestSuite"): JNI_OnLoad, if it has one ---- */
  fn_onload onload = (fn_onload)so_try_find_addr_rx(&g_mod_game, "JNI_OnLoad");
  if (onload)
    debugPrintf("[activity] JNI_OnLoad -> 0x%x\n", (unsigned)onload(g_jni_vm, NULL));

  /* ---- NativeActivity.onCreate ---- */
  g_act.callbacks = &g_cb;
  g_act.vm = g_jni_vm;
  g_act.env = g_jni_env;
  g_act.clazz = g_activity;
  g_act.internalDataPath = DCR_ANDROID_FILES;
  g_act.externalDataPath = DCR_ANDROID_EXT_FILES;
  g_act.sdkVersion = 22;
  g_act.assetManager = jni_singleton("android/content/res/AssetManager");
  g_act.obbPath = "/sdcard/Android/obb/" PORT_PACKAGE;
  debugPrintf("[activity] ANativeActivity_onCreate\n");
  on_create(&g_act, NULL, 0);
  jni_exception_report("ANativeActivity_onCreate");

  /* ---- onStart, onResume, the input queue, the window, focus ---- */
  CALL(onStart);
  CALL(onResume);
  g_resumed = 1;
  CALL(onInputQueueCreated, ts_input_queue());
  g_window = b_ANativeWindow_fromSurface(g_jni_env, NULL);
  CALL(onNativeWindowCreated, g_window);
  int w, h;
  dcr_window_size(&w, &h);
  ARect rect = {0, 0, w, h};
  CALL(onContentRectChanged, &rect);
  CALL(onWindowFocusChanged, 1);
  g_focused = 1;
  debugPrintf("[activity] activity up (%dx%d); this thread is the UI thread now\n", w, h);
  log_flush_ring();

  /* ---- the UI thread: input and lifecycle, ~120 times a second ---- */
  u64 last_report = armGetSystemTick();
  int launch_done = 0;
  unsigned long quiet_at = 0;
  while (!rt_exit_requested() && !g_quit && appletMainLoop()) {
    rt_applet_poll(); /* focus (port_focus_lost/gained) */
    if (!rt_focused()) {
      svcSleepThread(50000000ll);
      continue;
    }
    ts_input_poll();
    housekeeping(&launch_done, &quiet_at, &last_report);
    svcSleepThread(8000000ll);
  }

  /* ---- the way out ---- */
  log_set_quiet(0);
  rt_applet_stop(); /* also starts the clocks again if the game was paused */
  exit_guard_start();
  debugPrintf("[activity] leaving (%s): onPause, onStop, then the process ends\n",
              g_quit ? "the game's quit" : "HOME > Close");
  report();
  do_pause();
  CALL(onStop);
  debugPrintf("[activity] the game has closed\n");
  log_flush_ring();
  return 0;
}
