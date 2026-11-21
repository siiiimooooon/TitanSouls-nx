/* ts_setup_screen.c -- the screen while the game program sets itself up
 * (the first launch, and the first after each new NRO: dcr_setup.c).
 *
 * The runtime draws it as the PvZ Touch port's: the name in green, "Getting
 * the game ready (after an install or an update)", an ASCII bar with the
 * percentage and the step in English -- a developer's screen. Here it is a
 * plain light bar growing across the middle of a black screen, nothing
 * else; and black once setup is done ("Starting the game", 1000), as a
 * launch with nothing to set up looks while the game loads.
 * MIT.
 */
#include <stdio.h>
#include <switch.h>

#include "dcr_setup.h"
#include "util.h"

#define COLS 80 /* the boot console's (util.c) */
#define ROW 23
#define BAR 40

void port_setup_progress_draw(const char *what, int permille) {
  static int last = -1;
  (void)what;
  if (!log_console_active())
    return;
  if (permille < 0)
    permille = 0;
  int fill = permille >= 1000 ? -1 : permille * BAR / 1000; /* -1: done, black */
  if (fill == last)
    return;
  last = fill;
  printf("\x1b[0m\x1b[2J");
  if (fill > 0) {
    printf("\x1b[%d;%dH\x1b[47m", ROW, (COLS - BAR) / 2 + 1);
    for (int i = 0; i < fill; i++)
      putchar(' ');
    printf("\x1b[0m");
  }
  fflush(stdout);
  consoleUpdate(NULL);
}
