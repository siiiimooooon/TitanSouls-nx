/* ts_libc.c -- bionic calls the game's libraries make that the runtime has
 * no shim for (gen_imports.py lists them as missing otherwise).
 *
 *   rewind  bionic's FILE is the runtime's (bionic_stdio.c), so it is fseek
 *           to 0 and clearerr there
 *   chown   libfmodex (its file output): there are no owners on the SD card
 *   nice    worker threads lower their priority; the scheduler here is the
 *           runtime's (dcr_sched.c), so it is a no-op
 *
 * More get added here as gen_imports.py reports them. Based on DuckTales-NX
 * (Thorhax). MIT.
 */
int b_fseek(void *fp, long off, int whence);
void b_clearerr(void *fp);

void b_rewind(void *fp) {
  b_fseek(fp, 0, 0 /* SEEK_SET */);
  b_clearerr(fp);
}

int b_chown(const char *path, unsigned uid, unsigned gid) { return 0; }

int b_nice(int inc) { return 0; }

/* POSIX regex (regcomp/regexec/regfree): ts_regex.c. */
