#!/usr/bin/env bash
#
# build_hatari_fork.sh: build emumd's fork of Hatari with emumd's own
# hatari/build-hatari.sh, unchanged, at the fork commit that emumd's submodule
# (emu/emumd) pins. Its cache (the fork's sources and build, and EmuTOS) goes
# to tools/dev/builds/emumd-cache/ instead of ~/.cache/emumd, which other
# projects on this computer share at other emumd versions. CMake also reads
# tools/dev/hatari_fork.cmake. Prints the fork's binary last.
#
# Usage:  tools/dev/build_hatari_fork.sh

set -Eeuo pipefail
trap 'echo "ERROR: build_hatari_fork.sh: failed at line $LINENO" >&2' ERR

DEV="$(cd "$(dirname "$0")" && pwd)"
ROOT="$(cd "$DEV/../.." && pwd)"
EMUMD="$ROOT/emu/emumd"

[[ -x "$EMUMD/hatari/build-hatari.sh" ]] || {
  echo "build_hatari_fork.sh: emu/emumd is missing; run: git submodule update --init emu/emumd" >&2
  exit 1
}

export EMUMD_CACHE="$DEV/builds/emumd-cache"
mkdir -p "$EMUMD_CACHE"
export CMAKE_TOOLCHAIN_FILE="$DEV/hatari_fork.cmake"
"$EMUMD/hatari/build-hatari.sh"
