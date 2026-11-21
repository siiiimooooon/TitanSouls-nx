/* config.h -- Titan Souls' own constants. The runtime's settings (folder,
 * package, screen size, the libraries' region) are in port_config.h
 * (runtime/source/rt_settings.h).
 *
 * Titan Souls (com.devolver.titansouls 1.03, versionCode 31), armeabi-v7a:
 * Acid Nerve's C++ engine ("Ag*" classes, GLES2) as a NativeActivity
 * (TVActivity, lib_name TestSuite) with android_native_app_glue, FMOD Ex for
 * sound, Amazon GameCircle and Google Play Games linked in. Its data is one
 * OBB of ~178 MB in the game's own container format (not a zip). MIT.
 */
#ifndef TS_CONFIG_H
#define TS_CONFIG_H

#include "rt_settings.h"

/* The libraries, in load order (each imports from those before it).
 * Highest p_vaddr+p_memsz (tools/apk_info.py): libfmodex 0x111aac (1.1 MB),
 * libAmazonGamesJni 0x5013d (0.3 MB), libTestSuite 0x3af2dd (3.7 MB): all
 * inside PORT_SO_REGION_BYTES (32 MB). */
#define TS_LIB_FMODEX "libfmodex.so"
#define TS_LIB_AMAZON "libAmazonGamesJni.so"
#define TS_LIB_GAME   "libTestSuite.so"
#define TS_VERSION      "1.03"
#define TS_VERSION_CODE 31

/* The expansion file, as Android names it: main.<versionCode>.<package>.obb.
 * The game builds the path itself from "/sdcard/Android/obb/<package>/"
 * (a string in libTestSuite.so); the runtime maps that folder to
 * <game folder>/obb/. */
#define TS_OBB_NAME "main.31." PORT_PACKAGE ".obb"

#endif /* TS_CONFIG_H */
