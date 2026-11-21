/* ts_input.c -- the game's input queue, fed from the Switch's controllers
 * and touchscreen.
 *
 * A NativeActivity gets its input through an AInputQueue: Android hands one
 * over in onInputQueueCreated, android_native_app_glue attaches it to its
 * looper (AInputQueue_attachLooper), and when the looper says it is ready the
 * glue drains it with getEvent / preDispatchEvent / finishEvent, passing each
 * event to the engine's onInputEvent, which reads it with the AInputEvent,
 * AKeyEvent and AMotionEvent getters. All of that is here, honestly
 * implemented:
 *   - the queue is a ring of events plus a bionic pipe: the pipe's read end
 *     is what the looper waits on (android_ndk.c's looper polls bionic fds),
 *     and it holds one byte while the ring is not empty;
 *   - an event begins with the runtime's RtInputEvent (type, device, action,
 *     key code, meta state), so android_ndk.c's AInputEvent_getType and
 *     AKeyEvent_* getters read it; the motion getters are below.
 *
 * What is sent: a standard Android gamepad (the one ts_java.c describes to
 * the engine) -- face buttons, shoulders, triggers, sticks as KEYCODE_BUTTON_*
 * and KEYCODE_DPAD_* key events, the sticks and triggers as one joystick
 * motion event when they move -- and the touchscreen as a phone's.
 * Based on DuckTales-NX (Thorhax). MIT.
 */
#include <malloc.h>
#include <math.h>
#include <stdio.h>
#include <string.h>
#include <sys/types.h>
#include <switch.h>

#include "dcr_config.h"
#include "ts.h"
#include "rt_pad.h"
#include "rt_window.h"
#include "util.h"

int b_pipe(int fds[2]);
ssize_t b_read(int fd, void *buf, size_t n);
ssize_t b_write(int fd, const void *buf, size_t n);
int b_ALooper_addFd(void *l, int fd, int ident, int events, void *cb, void *data);
int b_ALooper_removeFd(void *l, int fd);

/* ---- Android's constants ---- */
#define AINPUT_EVENT_TYPE_KEY 1
#define AINPUT_EVENT_TYPE_MOTION 2
#define AINPUT_SOURCE_GAMEPAD 0x00000401
#define AINPUT_SOURCE_TOUCHSCREEN 0x00001002
#define AINPUT_SOURCE_JOYSTICK 0x01000010
#define AKEY_EVENT_ACTION_DOWN 0
#define AKEY_EVENT_ACTION_UP 1
#define AMOTION_EVENT_ACTION_DOWN 0
#define AMOTION_EVENT_ACTION_UP 1
#define AMOTION_EVENT_ACTION_MOVE 2
#define AMOTION_EVENT_ACTION_POINTER_DOWN 5
#define AMOTION_EVENT_ACTION_POINTER_UP 6
#define ALOOPER_EVENT_INPUT 1

#define K_BACK 4
#define K_DPAD_UP 19
#define K_DPAD_DOWN 20
#define K_DPAD_LEFT 21
#define K_DPAD_RIGHT 22
#define K_A 96
#define K_B 97
#define K_X 99
#define K_Y 100
#define K_L1 102
#define K_R1 103
#define K_L2 104
#define K_R2 105
#define K_THUMBL 106
#define K_THUMBR 107
#define K_START 108

#define AXIS_X 0
#define AXIS_Y 1
#define AXIS_Z 11
#define AXIS_RZ 14
#define AXIS_HAT_X 15
#define AXIS_HAT_Y 16
#define AXIS_LTRIGGER 17
#define AXIS_RTRIGGER 18
#define N_AXES 20
#define MAX_POINTERS 10

/* ---- the event: RtInputEvent's layout first (android_ndk.c) ---- */
typedef struct {
  int32_t type, device_id, action, key_code, meta_state;
  int32_t source;
  int32_t count;
  int32_t ids[MAX_POINTERS];
  float x[MAX_POINTERS], y[MAX_POINTERS];
  float axis[N_AXES];
  int64_t time_ns;
} DtEvent;

/* ---- the queue ---- */
#define RING 256
typedef struct {
  Mutex m;
  int fds[2];
  int signaled; /* the pipe holds its one byte */
  DtEvent *ring[RING];
  unsigned head, tail;
  void *looper;
} DtQueue;

static DtQueue g_q;
static volatile uint32_t g_events;
static int g_log;

void *ts_input_queue(void) { return &g_q; }
uint32_t ts_input_events(void) { return g_events; }

static void push(const DtEvent *src) {
  DtEvent *e = malloc(sizeof *e);
  if (!e)
    return;
  *e = *src;
  e->time_ns = (int64_t)armTicksToNs(armGetSystemTick());
  mutexLock(&g_q.m);
  if (g_q.head - g_q.tail >= RING) {
    mutexUnlock(&g_q.m);
    free(e);
    static int warned;
    if (!warned++)
      debugPrintf("[input] queue full: the game is not reading its input (events dropped)\n");
    return;
  }
  g_q.ring[g_q.head++ % RING] = e;
  if (!g_q.signaled && g_q.fds[1] >= 0) {
    char c = 1;
    if (b_write(g_q.fds[1], &c, 1) == 1)
      g_q.signaled = 1;
  }
  mutexUnlock(&g_q.m);
  g_events++;
}

/* ================================================= AInputQueue (NDK) */
void b_AInputQueue_attachLooper(DtQueue *q, void *looper, int ident, void *cb, void *data) {
  if (!q)
    return;
  q->looper = looper;
  b_ALooper_addFd(looper, q->fds[0], ident, ALOOPER_EVENT_INPUT, cb, data);
  debugPrintf("[input] queue attached to looper %p (ident %d)\n", looper, ident);
}

void b_AInputQueue_detachLooper(DtQueue *q) {
  if (q && q->looper) {
    b_ALooper_removeFd(q->looper, q->fds[0]);
    q->looper = NULL;
  }
}

int32_t b_AInputQueue_hasEvents(DtQueue *q) {
  if (!q)
    return -1;
  mutexLock(&q->m);
  int r = q->head != q->tail;
  mutexUnlock(&q->m);
  return r;
}

int32_t b_AInputQueue_getEvent(DtQueue *q, DtEvent **out) {
  if (!q || !out)
    return -1;
  mutexLock(&q->m);
  if (q->head == q->tail) {
    if (q->signaled) {
      char c;
      b_read(q->fds[0], &c, 1);
      q->signaled = 0;
    }
    mutexUnlock(&q->m);
    *out = NULL;
    return -1;
  }
  *out = q->ring[q->tail++ % RING];
  mutexUnlock(&q->m);
  return 0;
}

int32_t b_AInputQueue_preDispatchEvent(DtQueue *q, DtEvent *e) { return 0; }

void b_AInputQueue_finishEvent(DtQueue *q, DtEvent *e, int handled) { free(e); }

/* ============================================== AInputEvent / AMotionEvent */
int32_t b_AInputEvent_getSource(const DtEvent *e) { return e ? e->source : 0; }
int64_t b_AKeyEvent_getEventTime(const DtEvent *e) { return e ? e->time_ns : 0; }
int64_t b_AMotionEvent_getEventTime(const DtEvent *e) { return e ? e->time_ns : 0; }
int32_t b_AMotionEvent_getAction(const DtEvent *e) { return e ? e->action : 0; }
size_t b_AMotionEvent_getPointerCount(const DtEvent *e) { return e ? (size_t)e->count : 0; }

static int idx_ok(const DtEvent *e, size_t i) { return e && i < (size_t)e->count && i < MAX_POINTERS; }

int32_t b_AMotionEvent_getPointerId(const DtEvent *e, size_t i) { return idx_ok(e, i) ? e->ids[i] : 0; }
float b_AMotionEvent_getX(const DtEvent *e, size_t i) { return idx_ok(e, i) ? e->x[i] : 0.0f; }
float b_AMotionEvent_getY(const DtEvent *e, size_t i) { return idx_ok(e, i) ? e->y[i] : 0.0f; }
float b_AMotionEvent_getRawX(const DtEvent *e, size_t i) { return b_AMotionEvent_getX(e, i); }
float b_AMotionEvent_getRawY(const DtEvent *e, size_t i) { return b_AMotionEvent_getY(e, i); }
float b_AMotionEvent_getPressure(const DtEvent *e, size_t i) { return idx_ok(e, i) ? 1.0f : 0.0f; }
float b_AMotionEvent_getSize(const DtEvent *e, size_t i) { return idx_ok(e, i) ? 0.1f : 0.0f; }

float b_AMotionEvent_getAxisValue(const DtEvent *e, int32_t axis, size_t i) {
  if (!idx_ok(e, i) || axis < 0 || axis >= N_AXES)
    return 0.0f;
  if (e->source == AINPUT_SOURCE_TOUCHSCREEN)
    return axis == AXIS_X ? e->x[i] : axis == AXIS_Y ? e->y[i] : 0.0f;
  return e->axis[axis];
}

/* ===================================================== the controllers */
static PadState g_pad;
static u64 g_held; /* Switch buttons down, as sent */

/* Switch button -> Android key code (by label: A is A). */
typedef struct {
  u64 sw;
  int key;
} KeyMap;

static KeyMap g_map[] = {
    {HidNpadButton_A, K_A},           {HidNpadButton_B, K_B},
    {HidNpadButton_X, K_X},           {HidNpadButton_Y, K_Y},
    {HidNpadButton_L, K_L1},          {HidNpadButton_R, K_R1},
    {HidNpadButton_ZL, K_L2},         {HidNpadButton_ZR, K_R2},
    {HidNpadButton_StickL, K_THUMBL}, {HidNpadButton_StickR, K_THUMBR},
    {HidNpadButton_Plus, K_START},    {HidNpadButton_Minus, K_BACK},
    {HidNpadButton_Up, K_DPAD_UP},    {HidNpadButton_Down, K_DPAD_DOWN},
    {HidNpadButton_Left, K_DPAD_LEFT}, {HidNpadButton_Right, K_DPAD_RIGHT},
};

static void send_key(int key, int down) {
  DtEvent e = {0};
  e.type = AINPUT_EVENT_TYPE_KEY;
  e.device_id = 1;
  e.action = down ? AKEY_EVENT_ACTION_DOWN : AKEY_EVENT_ACTION_UP;
  e.key_code = key;
  e.source = AINPUT_SOURCE_GAMEPAD;
  push(&e);
  static int first = 10; /* the first few always: is input arriving at all? */
  if (g_log || first > 0) {
    first--;
    debugPrintf("[input] key %d %s\n", key, down ? "down" : "up");
  }
}

static float g_axes_sent[N_AXES];

static void send_axes(const float *ax) {
  DtEvent e = {0};
  e.type = AINPUT_EVENT_TYPE_MOTION;
  e.device_id = 1;
  e.action = AMOTION_EVENT_ACTION_MOVE;
  e.source = AINPUT_SOURCE_JOYSTICK;
  e.count = 1;
  memcpy(e.axis, ax, sizeof e.axis);
  e.x[0] = ax[AXIS_X];
  e.y[0] = ax[AXIS_Y];
  push(&e);
  memcpy(g_axes_sent, ax, sizeof g_axes_sent);
  if (g_log)
    debugPrintf("[input] axes L %.2f,%.2f R %.2f,%.2f T %.2f,%.2f\n", (double)ax[AXIS_X],
                (double)ax[AXIS_Y], (double)ax[AXIS_Z], (double)ax[AXIS_RZ],
                (double)ax[AXIS_LTRIGGER], (double)ax[AXIS_RTRIGGER]);
}

static float dead(float v) { return fabsf(v) < 0.15f ? 0.0f : v; }

static void poll_pad(void) {
  padUpdate(&g_pad);
  float st[4];
  u64 now = rt_pad_read(&g_pad, st);
  /* the sticks are not also D-pad keys here: the engine reads the axes */
  now &= ~(u64)(HidNpadButton_StickLLeft | HidNpadButton_StickLRight | HidNpadButton_StickLUp |
                HidNpadButton_StickLDown | HidNpadButton_StickRLeft | HidNpadButton_StickRRight |
                HidNpadButton_StickRUp | HidNpadButton_StickRDown);

  /* - held 0.8 s: the achievements list (ts_overlay.c) opens or closes. A
   * short press still reaches the game, as BACK, when it is let go. While
   * the list is open the game gets no buttons and centred sticks. */
  static u64 minus_since;
  static int minus_used;
  if (now & HidNpadButton_Minus) {
    u64 t = armGetSystemTick();
    if (!minus_since)
      minus_since = t;
    if (!minus_used && armTicksToNs(t - minus_since) >= 800000000ull) {
      ts_overlay_toggle_list();
      minus_used = 1;
    }
  } else {
    if (minus_since && !minus_used && !ts_overlay_list_open()) {
      send_key(K_BACK, 1);
      send_key(K_BACK, 0);
    }
    minus_since = 0;
    minus_used = 0;
  }
  now &= ~(u64)HidNpadButton_Minus;
  static int list_was_open;
  static u64 list_prev, swallow;
  /* Buttons still held from closing the list do not reach the game until
   * they are let go: the B that closed it reached the title menu as BACK and
   * asked to quit (hardware 2026-11-18). */
  swallow &= now;
  now &= ~swallow;
  if (!ts_overlay_list_open())
    list_was_open = 0;
  if (ts_overlay_list_open()) {
    /* B (or A) closes it too: it may have been opened from the title menu.
     * A button already held when it opened (the A that chose ACHIEVEMENTS)
     * does not count until it is let go and pressed again. */
    if (!list_was_open) {
      list_prev = now;
      list_was_open = 1;
    }
    u64 pressed = now & ~list_prev;
    list_prev = now;
    if (pressed & (HidNpadButton_B | HidNpadButton_A)) {
      ts_overlay_toggle_list();
      swallow = now;
    }
    /* Up / down (D-pad or left stick) move the selection: once on the
     * press, then every 100 ms after 400 ms held, like the game's menus. */
    static int move_dir;
    static u64 move_next;
    int dir = ((now & HidNpadButton_Down) || st[1] < -0.5f) - ((now & HidNpadButton_Up) || st[1] > 0.5f);
    u64 t = armGetSystemTick();
    if (dir != move_dir) {
      move_dir = dir;
      if (dir)
        ts_overlay_list_move(dir);
      move_next = t + armNsToTicks(400000000ull);
    } else if (dir && t >= move_next) {
      ts_overlay_list_move(dir);
      move_next = t + armNsToTicks(100000000ull);
    }
    now = 0;
    st[0] = st[1] = st[2] = st[3] = 0.0f;
  }

  u64 changed = now ^ g_held;
  for (unsigned i = 0; changed && i < sizeof g_map / sizeof g_map[0]; i++)
    if (changed & g_map[i].sw)
      send_key(g_map[i].key, (now & g_map[i].sw) != 0);
  g_held = now;

  /* Android's Y axes point down */
  float ax[N_AXES] = {0};
  ax[AXIS_X] = dead(st[0]);
  ax[AXIS_Y] = -dead(st[1]);
  ax[AXIS_Z] = dead(st[2]);
  ax[AXIS_RZ] = -dead(st[3]);
  ax[AXIS_LTRIGGER] = (now & HidNpadButton_ZL) ? 1.0f : 0.0f;
  ax[AXIS_RTRIGGER] = (now & HidNpadButton_ZR) ? 1.0f : 0.0f;
  /* The D-pad also as the hat axes, as Android gamepads report it (a SHIELD
   * controller sends both the HAT_X/HAT_Y motion and the DPAD keys). The
   * keys alone did nothing in the game's menus (hardware 2026-11-18). */
  ax[AXIS_HAT_X] = (float)(((now & HidNpadButton_Right) != 0) - ((now & HidNpadButton_Left) != 0));
  ax[AXIS_HAT_Y] = (float)(((now & HidNpadButton_Down) != 0) - ((now & HidNpadButton_Up) != 0));
  int moved = 0;
  for (int i = 0; i < N_AXES; i++)
    if (fabsf(ax[i] - g_axes_sent[i]) > 0.02f || (ax[i] == 0.0f) != (g_axes_sent[i] == 0.0f))
      moved = 1;
  if (moved)
    send_axes(ax);
}

/* ===================================================== the touchscreen */
typedef struct {
  int count;
  int32_t ids[MAX_POINTERS];
  float x[MAX_POINTERS], y[MAX_POINTERS];
} Touches;

static Touches g_touch;

static void send_touch(const Touches *t, int action) {
  static int first = 6;
  if (g_log || first > 0) {
    first--;
    debugPrintf("[input] touch action 0x%x, %d finger(s), first at %.0f,%.0f\n", action, t->count,
                t->count ? (double)t->x[0] : 0.0, t->count ? (double)t->y[0] : 0.0);
  }
  DtEvent e = {0};
  e.type = AINPUT_EVENT_TYPE_MOTION;
  e.device_id = 2;
  e.action = action;
  e.source = AINPUT_SOURCE_TOUCHSCREEN;
  e.count = t->count;
  memcpy(e.ids, t->ids, sizeof e.ids);
  memcpy(e.x, t->x, sizeof e.x);
  memcpy(e.y, t->y, sizeof e.y);
  push(&e);
}

static int find_id(const Touches *t, int32_t id) {
  for (int i = 0; i < t->count; i++)
    if (t->ids[i] == id)
      return i;
  return -1;
}

static void poll_touch(void) {
  HidTouchScreenState ts = {0};
  if (!hidGetTouchScreenStates(&ts, 1))
    return;
  int w, h;
  dcr_window_size(&w, &h);
  Touches now = {0};
  for (int i = 0; i < ts.count && now.count < MAX_POINTERS; i++) {
    now.ids[now.count] = (int32_t)ts.touches[i].finger_id;
    now.x[now.count] = (float)ts.touches[i].x * (float)w / 1280.0f;
    now.y[now.count] = (float)ts.touches[i].y * (float)h / 720.0f;
    now.count++;
  }
  /* fingers lifted: POINTER_UP (or UP for the last), from the old set */
  for (int i = g_touch.count - 1; i >= 0; i--) {
    if (find_id(&now, g_touch.ids[i]) >= 0)
      continue;
    send_touch(&g_touch, g_touch.count == 1 ? AMOTION_EVENT_ACTION_UP
                                            : AMOTION_EVENT_ACTION_POINTER_UP | (i << 8));
    for (int j = i; j < g_touch.count - 1; j++) {
      g_touch.ids[j] = g_touch.ids[j + 1];
      g_touch.x[j] = g_touch.x[j + 1];
      g_touch.y[j] = g_touch.y[j + 1];
    }
    g_touch.count--;
  }
  /* fingers still down: their new places, one MOVE */
  int moved = 0;
  for (int i = 0; i < g_touch.count; i++) {
    int k = find_id(&now, g_touch.ids[i]);
    if (g_touch.x[i] != now.x[k] || g_touch.y[i] != now.y[k]) {
      g_touch.x[i] = now.x[k];
      g_touch.y[i] = now.y[k];
      moved = 1;
    }
  }
  if (moved)
    send_touch(&g_touch, AMOTION_EVENT_ACTION_MOVE);
  /* new fingers: DOWN (the first) or POINTER_DOWN */
  for (int k = 0; k < now.count; k++) {
    if (find_id(&g_touch, now.ids[k]) >= 0 || g_touch.count >= MAX_POINTERS)
      continue;
    int i = g_touch.count++;
    g_touch.ids[i] = now.ids[k];
    g_touch.x[i] = now.x[k];
    g_touch.y[i] = now.y[k];
    send_touch(&g_touch, i == 0 ? AMOTION_EVENT_ACTION_DOWN
                                : AMOTION_EVENT_ACTION_POINTER_DOWN | (i << 8));
  }
}

/* ================================================================ API */
void ts_input_init(void) {
  if (b_pipe(g_q.fds) != 0) {
    g_q.fds[0] = g_q.fds[1] = -1;
    debugPrintf("[input] no pipe for the input queue: the game will get no input\n");
  }
  rt_pad_setup(RT_PAD_MAX_PLAYERS, 1);
  padInitializeDefault(&g_pad);
  hidInitializeTouchScreen();
  g_log = dcr_config()->log_input;
  if (dcr_config()->swap_ab) {
    for (unsigned i = 0; i < sizeof g_map / sizeof g_map[0]; i++) {
      if (g_map[i].sw == HidNpadButton_A)
        g_map[i].key = K_B;
      else if (g_map[i].sw == HidNpadButton_B)
        g_map[i].key = K_A;
    }
  }
  debugPrintf("[input] gamepad + touchscreen ready (pipe %d/%d, A/B %s)\n", g_q.fds[0], g_q.fds[1],
              dcr_config()->swap_ab ? "swapped" : "normal");
}

void ts_input_poll(void) {
  poll_pad();
  if (dcr_config()->touch)
    poll_touch();
}

void ts_input_release_all(void) {
  for (unsigned i = 0; i < sizeof g_map / sizeof g_map[0]; i++)
    if (g_held & g_map[i].sw)
      send_key(g_map[i].key, 0);
  g_held = 0;
  float zero[N_AXES] = {0};
  send_axes(zero);
  while (g_touch.count > 0) {
    int i = g_touch.count - 1;
    send_touch(&g_touch, g_touch.count == 1 ? AMOTION_EVENT_ACTION_UP
                                            : AMOTION_EVENT_ACTION_POINTER_UP | (i << 8));
    g_touch.count--;
  }
}
