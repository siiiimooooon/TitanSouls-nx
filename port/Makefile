#---------------------------------------------------------------------------------
# Titan Souls -- Nintendo Switch wrapper (32-bit / AArch32)
#
# Ships NO game code and NO game assets: the player's own APK (v1.03) is read at
# run time and its libraries are unpacked from it on the first launch; the
# game's data is the player's own OBB (main.31.com.devolver.titansouls.obb).
#
# Based on DuckTales-NX (Thorhax, MIT) on the android32 runtime (aks796, MIT).
# The build is the runtime's (runtime/runtime.mk: devkitARM + libnx32 + mesa32
# from portlibs32/); ./build.sh runs it in the toolchain container.
# Output: titansouls_nx.nsp, which the launcher NRO carries.
#---------------------------------------------------------------------------------
TARGET               := titansouls_nx
PORT_NPDM_PROGRAM_ID := 0x0100000000001055
include runtime/runtime.mk
