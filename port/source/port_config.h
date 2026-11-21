/* port_config.h -- Titan Souls' settings for the android32 runtime.
 *
 * Macros only: the runtime's C files, its assembly and the launcher all read
 * this (runtime/source/rt_settings.h). What each setting does is next to its
 * default in the runtime; runtime/docs/ lists them all. MIT.
 */
#ifndef PORT_CONFIG_H
#define PORT_CONFIG_H

/* ------------------------------------------------------------------ the game */
#define PORT_TITLE    "Titan Souls"
#define PORT_NAME     "titansouls_nx"
#define PORT_PACKAGE  "com.devolver.titansouls"
#define PORT_BANNER   "titansouls_nx: Titan Souls (Acid Nerve engine, NativeActivity, armeabi-v7a)"

/* The APK, by what is in it (any name): it holds the game's library; of
 * several, the one that is this game at the version the port is made for. */
#define PORT_APK_DESC "Titan Souls 1.03 (com.devolver.titansouls, armeabi-v7a)"
#define PORT_APK_ROLES                                                                      \
  {.what = "the game", .need = (const char *const[]){"lib/armeabi-v7a/libTestSuite.so", NULL}, \
   .package = PORT_PACKAGE, .version_code = 31, .flags = RT_APK_PACKAGE_BONUS}
#define PORT_LAUNCHER_START_NOTE "(the first start unpacks the game's libraries from the APK)"

/* ------------------------------------------------------------------ audio */
/* FMOD Ex plays through OpenSL ES (it dlopens libOpenSLES.so; the Java side
 * never creates org.fmod.FMODAudioDevice): the runtime's OpenSL over audout. */
#define RT_OPENSLES 1

/* ------------------------------------------------------------------ files */
/* The OBB is opened by name (main.31.<package>.obb): log it when it is. */
#define RT_PATH_TRACE_EXTRA ".obb"

/* ------------------------------------------------------------------ input */
#define RT_PAD_MAX_PLAYERS 1

#endif
