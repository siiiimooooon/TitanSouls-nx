/* ts_assets.c -- the NDK's AAsset calls, over the player's APK.
 *
 * Titan Souls reads a few small files from the APK's assets/ (12 entries,
 * ~140 KB: the loading screen's images and shaders, config.txt, the store
 * keys) through the ANativeActivity's AAssetManager. The runtime has no
 * AAsset (gen_imports.py lists the five calls as missing), so they are here:
 * AAssetManager_open finds "assets/<name>" in the APK's central directory
 * (rt_zip_walk_path) and reads the whole entry into memory -- inflating it
 * with miniz when it is deflated -- and the rest works on that copy. Every
 * asset is small, so nothing is streamed. (Backstab-NX does the same.)
 *
 * The AAssetManager pointer the game passes is ignored: there is one APK.
 * MIT.
 */
#include <miniz/miniz.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "dcr_path.h"
#include "rt_apkfind.h"
#include "util.h"

typedef struct {
  uint8_t *data;
  long size;
  long pos;
} TsAsset;

typedef struct {
  const char *want; /* "assets/<name>" */
  RtZipEntry found;
  int ok;
} FindCtx;

static int find_cb(const RtZipEntry *e, void *ctx) {
  FindCtx *c = ctx;
  if (strcmp(e->name, c->want) != 0)
    return 0;
  c->found = *e;
  c->ok = 1;
  return 1;
}

static uint8_t *read_entry(const char *apk, const RtZipEntry *e) {
  FILE *f = fopen(apk, "rb");
  if (!f)
    return NULL;
  uint8_t *raw = NULL, *out = NULL;
  uint64_t off = rt_zip_data_offset(f, 0, e);
  if (!off || fseek(f, (long)off, SEEK_SET) != 0)
    goto done;
  raw = malloc(e->comp_size ? e->comp_size : 1);
  if (!raw || fread(raw, 1, e->comp_size, f) != e->comp_size)
    goto done;
  if (e->method == 0) {
    out = raw;
    raw = NULL;
  } else if (e->method == 8) {
    out = malloc(e->size ? e->size : 1);
    if (out && tinfl_decompress_mem_to_mem(out, e->size, raw, e->comp_size, 0) != e->size) {
      free(out);
      out = NULL;
    }
  }
done:
  free(raw);
  fclose(f);
  return out;
}

void *b_AAssetManager_open(void *mgr, const char *filename, int mode) {
  (void)mgr;
  (void)mode;
  const char *apk = dcr_apk_path();
  if (!filename || !apk)
    return NULL;
  char want[512];
  snprintf(want, sizeof want, "assets/%s", filename);
  FindCtx c = {.want = want};
  rt_zip_walk_path(apk, find_cb, &c);
  if (!c.ok) {
    debugPrintf("[asset] %s: not in the APK\n", want);
    return NULL;
  }
  uint8_t *data = read_entry(apk, &c.found);
  if (!data) {
    debugPrintf("[asset] %s: could not read it (method %d)\n", want, c.found.method);
    return NULL;
  }
  TsAsset *a = calloc(1, sizeof *a);
  if (!a) {
    free(data);
    return NULL;
  }
  a->data = data;
  a->size = (long)c.found.size;
  debugPrintf("[asset] %s: %ld bytes\n", want, a->size);
  return a;
}

void b_AAsset_close(void *asset) {
  TsAsset *a = asset;
  if (!a)
    return;
  free(a->data);
  free(a);
}

long b_AAsset_getLength(void *asset) { return asset ? ((TsAsset *)asset)->size : 0; }

int b_AAsset_read(void *asset, void *buf, unsigned count) {
  TsAsset *a = asset;
  if (!a || !buf)
    return -1;
  long left = a->size - a->pos;
  long n = (long)count < left ? (long)count : left;
  if (n <= 0)
    return 0;
  memcpy(buf, a->data + a->pos, (size_t)n);
  a->pos += n;
  return (int)n;
}

long b_AAsset_seek(void *asset, long offset, int whence) {
  TsAsset *a = asset;
  if (!a)
    return -1;
  long base = whence == 0 ? 0 : whence == 1 ? a->pos : whence == 2 ? a->size : -1;
  if (base < 0)
    return -1;
  long p = base + offset;
  if (p < 0 || p > a->size)
    return -1;
  a->pos = p;
  return p;
}
