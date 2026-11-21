#!/bin/sh
# Build titansouls_nx.nro (the launcher) with the runtime's launcher build
# (devkitPro's 64-bit toolchain container). Build the wrapper first
# (../build.sh): the NRO carries ../titansouls_nx.nsp and ../titansouls_nx.build.
HERE="$(cd "$(dirname "$0")" && pwd)"
LAUNCHER_DIR="$HERE" PAYLOAD=titansouls_nx exec "$HERE/../runtime/launcher/build.sh" "$@"
