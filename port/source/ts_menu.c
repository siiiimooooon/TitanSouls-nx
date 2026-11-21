/* ts_menu.c -- an ACHIEVEMENTS option in the game's title menu.
 *
 * The title menu (TitleMenu, built in TitleMenu::TitleMenu) is a UIMenu: a
 * std::vector<UIOption*> at +0x94 (begin) / +0x98 (end) -- the game builds
 * each option as new UIOption(std::string text_id, 100.0f, true) and
 * push_back()s it ("m_start", "m_options", "m_controls", "m_quit") -- the
 * selected index at +0x8c, which UIMenu::Control moves over the vector's
 * whole size, and TitleMenu::Action, which acts on 1 start, 2 options,
 * 3 controls, 4 quit and does nothing for any other index. The game's text
 * has an "m_achievements" entry (unused by this version: on other platforms
 * it was the store's achievements screen).
 *
 * So, without touching the game's code: TitleMenu's vtable (in .data, still
 * writable while the module is staged) gets two entries replaced --
 *   Added   the game's, then once per menu object one more option,
 *           UIOption("m_achievements"), made and appended with the game's own
 *           functions (it is drawn, navigated and sounded like the others);
 *   Action  if that option is the one chosen, the port's achievements list
 *           (ts_overlay.c) opens; anything else goes to the game's.
 * Every game function used is checked first (its first bytes, as in the 1.03
 * APK): if one differs, nothing is changed and the title menu is the game's.
 * The pause menu (GameMenu) is not a list -- three fixed texts, the index
 * clamped to 0..2 in code -- so it is not touched here. MIT.
 */
#include <stdint.h>
#include <string.h>

#include "so_util.h"
#include "ts.h"
#include "util.h"

/* offsets in libTestSuite.so 1.03 */
#define OFF_VT_TITLEMENU 0x392810 /* _ZTV9TitleMenu */
#define OFF_TITLE_ACTION 0x1d767c /* TitleMenu::Action() */
#define OFF_TITLE_ADDED 0x1d7a80  /* TitleMenu::Added() */
#define OFF_STR_CTOR 0x301a00     /* std::string::string(const char *, const allocator &), internal */
#define OFF_STR_DTOR 0x300880     /* std::string::~string(), internal */
#define OFF_UIOPTION 0x1daea0     /* UIOption::UIOption(std::string, float, bool) */
#define OFF_PUSH_BACK 0x1d41ec    /* std::vector<UIOption *>::push_back(UIOption *const &) */
#define OFF_NEW 0x2d2864          /* operator new(unsigned) */
#define MENU_VEC 0x94
#define MENU_SEL 0x8c

static const struct {
  uint32_t off;
  uint8_t bytes[8];
} k_check[] = {
    {OFF_STR_CTOR, {0x70, 0xb5, 0x0c, 0x46, 0x05, 0x46, 0x16, 0x46}},
    {OFF_STR_DTOR, {0x10, 0xb5, 0x04, 0x46, 0x06, 0x4b, 0x82, 0xb0}},
};

typedef void (*fn_self)(void *self);
typedef void (*fn_str_ctor)(void *s, const char *c, const void *alloc);
typedef void (*fn_str_dtor)(void *s);
typedef void (*fn_uioption)(void *self, void *text_id, float f, int b);
typedef void (*fn_push_back)(void *vec, void *const *item);
typedef void *(*fn_new)(unsigned n);

static fn_self r_action, r_added;
static fn_str_ctor f_str_ctor;
static fn_str_dtor f_str_dtor;
static fn_uioption f_uioption;
static fn_push_back f_push_back;
static fn_new f_new;

#define MENUS 4
static void *g_menu[MENUS];
static int g_index[MENUS];

static int our_index(void *self) {
  for (int i = 0; i < MENUS; i++)
    if (g_menu[i] == self)
      return g_index[i];
  return -1;
}

/* The option goes into the vector BEFORE the game's Added: UIMenu::Added
 * makes each option in the vector a child entity (Entity::AddChild), and
 * only children are updated and drawn. (The first build added it after: it
 * could be selected but was never drawn -- hardware 2026-11-18.) */
static void add_option(void *self);

static void w_added(void *self) {
  add_option(self);
  r_added(self);
}

static void add_option(void *self) {
  if (our_index(self) >= 0)
    return;
  uint8_t *me = self;
  void **begin = *(void ***)(me + MENU_VEC), **end = *(void ***)(me + MENU_VEC + 4);
  int index = (int)(end - begin);
  uint32_t text[1] = {0}; /* a std::string: one pointer */
  uint32_t alloc = 0;     /* std::allocator<char>: empty */
  f_str_ctor(text, "m_achievements", &alloc);
  void *opt = f_new(184);
  f_uioption(opt, text, 100.0f, 1);
  f_str_dtor(text);
  f_push_back(me + MENU_VEC, &opt);
  for (int i = 0; i < MENUS; i++)
    if (!g_menu[i]) {
      g_menu[i] = self;
      g_index[i] = index;
      break;
    }
  debugPrintf("[menu] title menu %p: ACHIEVEMENTS added as option %d\n", self, index);
}

static void w_action(void *self) {
  int sel = *(int *)((uint8_t *)self + MENU_SEL);
  int ours = our_index(self);
  if (ours >= 0 && sel == ours) {
    debugPrintf("[menu] ACHIEVEMENTS chosen: the list opens\n");
    ts_overlay_open_from_menu();
    return;
  }
  r_action(self);
}

/* while libTestSuite.so is staged (ts_loader.c, before so_finalize) */
void ts_menu_patch(so_module *m) {
  uint8_t *img = (uint8_t *)m->load_base;
  uintptr_t base = (uintptr_t)m->load_virtbase;
  for (unsigned i = 0; i < sizeof k_check / sizeof k_check[0]; i++)
    if (k_check[i].off + 8 > m->load_size || memcmp(img + k_check[i].off, k_check[i].bytes, 8)) {
      debugPrintf("[menu] 0x%x is not the expected code (another build?): no ACHIEVEMENTS option\n",
                  (unsigned)k_check[i].off);
      return;
    }
  uint32_t *vt = (uint32_t *)(img + OFF_VT_TITLEMENU);
  uint32_t want_action = (uint32_t)(base + OFF_TITLE_ACTION), want_added = (uint32_t)(base + OFF_TITLE_ADDED);
  int s_action = -1, s_added = -1;
  for (int i = 0; i < 40; i++) {
    if ((vt[i] & ~1u) == want_action)
      s_action = i;
    if ((vt[i] & ~1u) == want_added)
      s_added = i;
  }
  if (s_action < 0 || s_added < 0) {
    debugPrintf("[menu] TitleMenu's vtable: Action slot %d, Added slot %d: no ACHIEVEMENTS option\n", s_action,
                s_added);
    return;
  }
  r_action = (fn_self)(uintptr_t)vt[s_action];
  r_added = (fn_self)(uintptr_t)vt[s_added];
  f_str_ctor = (fn_str_ctor)(base + OFF_STR_CTOR + 1);
  f_str_dtor = (fn_str_dtor)(base + OFF_STR_DTOR + 1);
  f_uioption = (fn_uioption)(base + OFF_UIOPTION + 1);
  f_push_back = (fn_push_back)(base + OFF_PUSH_BACK + 1);
  f_new = (fn_new)(base + OFF_NEW + 1);
  vt[s_action] = (uint32_t)(uintptr_t)w_action;
  vt[s_added] = (uint32_t)(uintptr_t)w_added;
  debugPrintf("[menu] TitleMenu vtable slots %d (Action) and %d (Added) wrapped\n", s_action, s_added);
}
