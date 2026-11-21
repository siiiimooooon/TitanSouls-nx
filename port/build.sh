#!/bin/sh
# Build titansouls_nx.nsp in the AArch32 toolchain container: the runtime's
# docker_build.sh (arguments go to make: ./build.sh clean, rt-files).
# libnx32 is found next to this folder or where DCR_LIBNX32 says
# (tools/build_port.sh sets it).
exec "$(dirname "$0")/runtime/tools/docker_build.sh" "$@"
