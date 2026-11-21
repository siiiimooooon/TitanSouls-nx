/* ts_setup_plan.c -- Titan Souls' part of the first launch: its plan for the
 * runtime's dcr_setup.c, which does the work.
 *
 * The bar, in permille of the whole first launch:
 *   100- 600  the three libraries unpacked (by bytes)
 *   650- 950  the Java class list (by dex file)
 *        1000 the game starts
 *
 * .setup keys, which must not change (or every player unpacks again once):
 * libfmodex.so, libAmazonGamesJni.so, libTestSuite.so, classes.txt. MIT.
 */
#include "dcr_setup.h"

static const char *const k_libs[] = {"libfmodex.so", "libAmazonGamesJni.so", "libTestSuite.so"};

const RtSetupPlan port_setup_plan = {
    .libs = k_libs,
    .nlibs = 3,
    .libs_what = "Unpacking the game",
    .apk_requirement = "This port needs Titan Souls 1.03\n"
                       "(com.devolver.titansouls, armeabi-v7a): use the APK of that version.",
    .libs_p0 = 100,
    .libs_p1 = 600,
    .classes_p0 = 650,
    .classes_p1 = 950,
};
