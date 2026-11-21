/* ts_main.c -- Titan Souls' part of the boot (the runtime's main.c runs the
 * rest: runtime/source/main.c).
 *
 * port_load() goes from the APK to the game's first code: the three
 * libraries and classes.txt unpacked when needed, then loaded and bound
 * (ts_loader.c). port_run() runs their constructors, in the order
 * TVActivity loads them, then plays the NativeActivity (ts_activity.c).
 * Based on DuckTales-NX (Thorhax). MIT.
 */
#include <stdio.h>

#include "config.h"
#include "dcr_path.h"
#include "dcr_setup.h"
#include "error.h"
#include "ts.h"
#include "util.h"

const char *port_apk_help(void) {
  return "Copy the APK of your own Titan Souls (com.devolver.titansouls,\n"
         "1.03, armeabi-v7a) into that folder, under any name ending in .apk, and its\n"
         "OBB file (" TS_OBB_NAME ")\n"
         "into the obb/ folder inside it.";
}

int port_load(const char *apk) {
  /* the three libraries and classes.txt, from the APK when they are missing
   * or it has changed (ts_setup_plan.c) */
  dcr_setup_from_apk(apk);
  if (!ts_obb_present())
    fatal_error("The game's data file is missing.\n\n"
                "Copy " TS_OBB_NAME "\n"
                "(from Android/obb/" PORT_PACKAGE "/ on your phone)\n"
                "into %s/obb/",
                dcr_game_root());
  if (ts_load_modules() != 0)
    fatal_error("Could not load the game's libraries from %s.\n\n"
                "They are unpacked from the APK (lib/armeabi-v7a/) on launch: delete\n"
                "the .so files and .setup there to unpack them again.",
                dcr_game_root());
  return 0;
}

void port_run(void) {
  ts_run_constructors();
  ts_activity_run();
}
