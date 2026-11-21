#!/bin/bash
# build_port.sh -- builds the port in WSL (Ubuntu-24.04, user nx) and leaves the NRO in out/ (next to tools/).
# Called by tools/build_port.bat. Steps:
#   1. copies port/ to /srv/nx/ts_nx (Linux: Docker mounts it faster and without permission trouble)
#   2. links portlibs32 (32-bit Mesa) and uses the libnx32 built by mc3_nx/tools/setup_deps.sh
#   3. ./build.sh (the 32-bit program, .nsp) and ./launcher/build.sh (the 64-bit NRO that installs it)
#   4. copies the .nro and the log to out/
set -eu
ROOT=$(cd "$(dirname "$0")/.." && pwd)
SRC=$ROOT/port
DST=/srv/nx/ts_nx
OUT=$ROOT/out
LOGS=/srv/nx/logs
STAMP=$(date +%Y%m%d-%H%M%S)
LOG=$LOGS/ts_build_$STAMP.log
mkdir -p "$DST" "$OUT" "$LOGS"

rsync -a --delete --exclude portlibs32 --exclude '*.nsp' --exclude '*.nro' --exclude 'build/' \
      --exclude 'launcher/build/' --exclude 'launcher/romfs/' "$SRC/" "$DST/"
ln -sfn /srv/nx/mc3_nx/portlibs32 "$DST/portlibs32"
export DCR_LIBNX32=/srv/nx/mc3_nx/deps/libnx32/prefix

cd "$DST"
chmod +x build.sh launcher/build.sh runtime/tools/*.sh 2>/dev/null || true
{
  echo "== build $STAMP"
  ./build.sh "$@"
  ./launcher/build.sh
} > "$LOG" 2>&1 && rc=0 || rc=$?

cp "$LOG" "$OUT/build_last.log"
if [ $rc -ne 0 ]; then
  echo "BUILD FAILED (code $rc). Errors:"
  grep -E "error|Error|undefined reference|multiple definition" "$LOG" | sort -u | head -20
  exit $rc
fi
cp launcher/*.nro "$OUT/"
ls -la "$OUT"/*.nro
echo "OK: build $STAMP"
