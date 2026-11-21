/* ts_java.c -- the Java side of Titan Souls, answered in C.
 *
 * The game is a NativeActivity: its Java (com.devolver.titansouls.TVActivity,
 * a subclass of android.app.NativeActivity) does not run here. The engine
 * reaches Java through the ANativeActivity's clazz (ts_activity.c hands it
 * g_activity) and the JavaVM: GetObjectClass, GetMethodID by name and
 * signature, Call*Method. Most of what libTestSuite.so names in Java is Google
 * Play Games (gpg::, com/google/android/gms/...) and Amazon GameCircle: on a
 * console with no network they get the runtime's defaults (null, 0, nothing),
 * and [debug] log_java_calls shows every call.
 *
 * What the engine needs from the system, it mostly takes from the NDK, not
 * Java: the language from AConfiguration, the window from ANativeWindow, the
 * OBB from a fixed path ("/sdcard/Android/obb/<package>/main.31...obb", a
 * string in the library), the saves from "/sdcard/TitanSoulsSave/". The
 * runtime maps /sdcard to the game folder. The handlers below cover the usual
 * Context calls in case the Java glue asks for them. Based on DuckTales-NX
 * (Thorhax). MIT.
 */
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include <switch.h>

#include "config.h"
#include "dcr_config.h"
#include "dcr_path.h"
#include "ts.h"
#include "util.h"

#define PKG PORT_PACKAGE
#define ACT "com/devolver/titansouls/TVActivity"
#define SD "/sdcard"
#define H(fn) static jvalue fn(JObj *self, const jvalue *a, const JMethod *m)

JObj *g_activity;

/* ============================================================== paths */
const char *ts_obb_android_path(void) { return SD "/Android/obb/" PKG "/" TS_OBB_NAME; }

int ts_obb_present(void) {
  char buf[DCR_PATH_MAX];
  const char *p = dcr_translate_path(ts_obb_android_path(), buf, sizeof buf);
  struct stat st;
  int ok = stat(p, &st) == 0 && st.st_size > 0;
  debugPrintf("[obb] %s: %s (%lld MB)\n", p, ok ? "found" : "MISSING",
              ok ? (long long)(st.st_size >> 20) : 0ll);
  return ok;
}

/* ============================================================ handlers */
H(h_obb_dir) {
  JObj *f = jni_new("java/io/File");
  jni_set_field(f, "path", jv_l(jni_str(SD "/Android/obb/" PKG)), 1);
  return jv_l(f);
}
H(h_ext_files_dir) {
  JObj *f = jni_new("java/io/File");
  jni_set_field(f, "path", jv_l(jni_str(DCR_ANDROID_EXT_FILES)), 1);
  return jv_l(f);
}
H(h_files_dir) {
  JObj *f = jni_new("java/io/File");
  jni_set_field(f, "path", jv_l(jni_str(DCR_ANDROID_FILES)), 1);
  return jv_l(f);
}
H(h_package_name) { return jv_l(jni_str(PKG)); }
H(h_finish) {
  debugPrintf("[java] %s: the game asked to close\n", m->name);
  ts_request_home();
  return jv_none();
}

/* Context.getClassLoader() and ClassLoader.loadClass("a.b.C"): the engine
 * (or the Play Games glue) finds some of its Java classes through the
 * activity's class loader instead of FindClass. Unanswered, loadClass gave
 * null, every static method ID on it was 0, and the game called through that
 * 0 in a loop, ~2,000 times a second, before drawing anything (hardware
 * 2026-11-18, 2nd run: "call through a bad method ID 0x0 from
 * libTestSuite+0x215271", JNIEnv::CallStaticObjectMethod). Here loadClass
 * answers the runtime's class of that name -- its methods then exist (the
 * unhandled ones logged once, with their names) -- and logs what is asked. */
H(h_class_loader) { return jv_l(jni_singleton("java/lang/ClassLoader")); }
H(h_load_class) {
  char name[112];
  snprintf(name, sizeof name, "%s", a ? jni_utf(a[0].l) : "");
  for (char *c = name; *c; c++)
    if (*c == '.')
      *c = '/';
  static unsigned calls;
  if (calls++ < 8) /* the engine asks on every call into TVActivity */
    debugPrintf("[java] ClassLoader.loadClass(%s)%s\n", name,
                jni_class_exists(name) ? "" : " -- not in the APK");
  return jv_l(jni_class(name)->obj);
}

/* ------------------------------------------------- TVActivity's statics
 * The engine's bridge to Java: static methods of TVActivity that all answer
 * a String, called through the class loadClass gives (above). What the Java
 * does (TVActivity.java of the APK, read with jadx) and what is answered:
 *   isStaticDataResolved  "OK" once onCreate has run (staticDataResolved =
 *                         true). The engine polls it before loading anything:
 *                         unanswered, the screen stayed black (hardware
 *                         2026-11-18, 3rd run: ~2,000 polls a second, 0 OBB
 *                         reads, 0 frames)
 *   checkIfReady          "OK" when FileTask is done and Amazon is settled
 *                         (it is 0 = not used, off Amazon's devices)
 *   getModel              "AmazonTV" / "Stick" / "Ouya" / "Nexus" / "Shield" /
 *                         "ShieldPortable" / "Unknown" from Build.MODEL:
 *                         "ShieldPortable" -- the SHIELD Portable, a handheld
 *                         with the controller built in, like this console
 *   getContentDir         the app's data dir (/data/data/<pkg>)
 *   getLang               Locale.getDefault().getLanguage(): "en", "es"...
 *   isAmazonSignedIn      "Not signed in"
 *   getGameControllersNumber  "1"
 *   terminateApp          the game's own quit
 *   ouyaFileExists "NO", getOuyaData null, storeOuyaData "OK"
 * FileTask (an AsyncTask from onCreate) is ts_java_filetask() below. */
static char g_lang[8] = "en";

H(h_str_ok) { return jv_l(jni_str("OK")); }
H(h_model) { return jv_l(jni_str("ShieldPortable")); }
H(h_content_dir) { return jv_l(jni_str("/data/data/" PKG)); }
H(h_lang) { return jv_l(jni_str(g_lang)); }
H(h_amazon_signed_in) { return jv_l(jni_str("Not signed in")); }
H(h_controllers) { return jv_l(jni_str("1")); }
H(h_str_no) { return jv_l(jni_str("NO")); }
H(h_terminate) {
  debugPrintf("[java] TVActivity.terminateApp: the game asked to close\n");
  ts_request_home();
  return jv_l(jni_str("Success!"));
}

/* android.os.Environment.getExternalStorageDirectory(): a java.io.File */
H(h_ext_storage) {
  JObj *f = jni_new("java/io/File");
  jni_set_field(f, "path", jv_l(jni_str(SD)), 1);
  return jv_l(f);
}
H(h_file_path) {
  jvalue v = jni_get_field(self, "path");
  return jv_l(jni_str(v.l ? jni_utf(v.l) : ""));
}

/* ============================================================== tables */
const JMethodDef jni_method_defs[] = {
    {ACT, "getObbDir", NULL, h_obb_dir},
    {ACT, "getExternalFilesDir", NULL, h_ext_files_dir},
    {ACT, "getFilesDir", NULL, h_files_dir},
    {ACT, "getPackageName", NULL, h_package_name},
    {ACT, "finish", NULL, h_finish},
    {ACT, "isStaticDataResolved", NULL, h_str_ok},
    {ACT, "checkIfReady", NULL, h_str_ok},
    {ACT, "getModel", NULL, h_model},
    {ACT, "getContentDir", NULL, h_content_dir},
    {ACT, "getLang", NULL, h_lang},
    {ACT, "isAmazonSignedIn", NULL, h_amazon_signed_in},
    {ACT, "getGameControllersNumber", NULL, h_controllers},
    {ACT, "terminateApp", NULL, h_terminate},
    {ACT, "ouyaFileExists", NULL, h_str_no},
    {ACT, "getOuyaData", NULL, jni_h_null},
    {ACT, "storeOuyaData", NULL, h_str_ok},
    {ACT, "goImmersive", NULL, jni_h_void},
    {ACT, "getClassLoader", NULL, h_class_loader},
    {"android/app/NativeActivity", "getClassLoader", NULL, h_class_loader},
    {"android/content/Context", "getClassLoader", NULL, h_class_loader},
    {"java/lang/ClassLoader", "loadClass", NULL, h_load_class},
    {"android/os/Environment", "getExternalStorageDirectory", NULL, h_ext_storage},
    {"java/io/File", "getAbsolutePath", NULL, h_file_path},
    {"java/io/File", "getPath", NULL, h_file_path},
    {"java/io/File", "toString", NULL, h_file_path},
    {NULL, NULL, NULL, NULL},
};

const JFieldDef jni_field_defs[] = {
    {"android/os/Build$VERSION", "RELEASE", NULL, 0, "5.1"},
    {"android/os/Build$VERSION", "SDK_INT", NULL, 22, NULL},
    {"android/os/Build", "MODEL", NULL, 0, "SHIELD Android TV"},
    {"android/os/Build", "MANUFACTURER", NULL, 0, "NVIDIA"},
    {"android/os/Build", "BRAND", NULL, 0, "NVIDIA"},
    {"android/os/Build", "DEVICE", NULL, 0, "foster"},
    {"android/os/Build", "HARDWARE", NULL, 0, "tegra"},
    {NULL, NULL, NULL, 0, NULL},
};

const char *const jni_class_supers[][2] = {
    {ACT, "android/app/NativeActivity"},
    {"android/app/NativeActivity", "android/app/Activity"},
    {"android/app/Activity", "android/view/ContextThemeWrapper"},
    {"android/view/ContextThemeWrapper", "android/content/ContextWrapper"},
    {"android/content/ContextWrapper", "android/content/Context"},
    {NULL, NULL},
};

const char *const jni_missing_classes[] = {NULL};

/* ============================================================== setup */
/* The language for getLang: config.ini's, or the console's (first two
 * letters of the system language code: "en-US" -> "en"). */
static void choose_lang(void) {
  const char *cfg = dcr_config()->locale;
  if (cfg[0]) {
    snprintf(g_lang, sizeof g_lang, "%.2s", cfg);
    return;
  }
  u64 code = 0;
  if (R_SUCCEEDED(setInitialize())) {
    if (R_SUCCEEDED(setGetSystemLanguage(&code)))
      snprintf(g_lang, sizeof g_lang, "%.2s", (const char *)&code);
    setExit();
  }
  if (!g_lang[0])
    snprintf(g_lang, sizeof g_lang, "en");
}

/* mkdir -p of an Android path, through the runtime's mapping. */
static void mkdirs_android(const char *android_path) {
  char buf[DCR_PATH_MAX];
  const char *p = dcr_translate_path(android_path, buf, sizeof buf);
  char tmp[DCR_PATH_MAX];
  snprintf(tmp, sizeof tmp, "%s", p);
  for (char *c = tmp + 1; *c; c++) {
    if (*c == '/') {
      *c = 0;
      mkdir(tmp, 0777);
      *c = '/';
    }
  }
  mkdir(tmp, 0777);
}

/* TVActivity.FileTask: the save folder, and config.txt copied from the APK's
 * assets when there is none. (It also copies ts_data_android.wad from the
 * assets into the data dir; this APK has no such asset -- the data is the
 * OBB, which the engine opens itself -- so on a phone that copy fails
 * quietly too.) */
void *b_AAssetManager_open(void *mgr, const char *filename, int mode);
void b_AAsset_close(void *asset);
int b_AAsset_read(void *asset, void *buf, unsigned count);

static void ts_java_filetask(void) {
  mkdirs_android("/sdcard/TitanSoulsSave/data/SAVE");
  char buf[DCR_PATH_MAX];
  const char *cfg = dcr_translate_path("/sdcard/TitanSoulsSave/data/config.txt", buf, sizeof buf);
  struct stat st;
  if (stat(cfg, &st) == 0) {
    debugPrintf("[java] FileTask: %s already there\n", cfg);
    return;
  }
  void *a = b_AAssetManager_open(NULL, "config.txt", 0);
  FILE *out = a ? fopen(cfg, "wb") : NULL;
  if (!out) {
    debugPrintf("[java] FileTask: could not copy config.txt to %s\n", cfg);
    if (a)
      b_AAsset_close(a);
    return;
  }
  char chunk[1024];
  int n, total = 0;
  while ((n = b_AAsset_read(a, chunk, sizeof chunk)) > 0) {
    fwrite(chunk, 1, (size_t)n, out);
    total += n;
  }
  fclose(out);
  b_AAsset_close(a);
  debugPrintf("[java] FileTask: config.txt copied to %s (%d bytes)\n", cfg, total);
}

void ts_java_init(void) {
  jni_init();
  g_jni_log = dcr_config()->log_jni;
  g_activity = jni_singleton(ACT);
  choose_lang();
  ts_java_filetask();
  debugPrintf("[java] language %s, model ShieldPortable\n", g_lang);
  debugPrintf("[java] TVActivity ready: OBB %s\n", ts_obb_android_path());
}
