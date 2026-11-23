#!/bin/sh
set -eu
root=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
export DEVKITPRO=${DEVKITPRO:-/opt/devkitpro}
export DEVKITA64=${DEVKITA64:-$DEVKITPRO/devkitA64}
export DEVKITARM=${DEVKITARM:-$DEVKITPRO/devkitARM}
export LIBNX=${LIBNX:-$DEVKITPRO/libnx}
export PORTLIBS=${PORTLIBS:-$DEVKITPRO/portlibs/switch}
export PATH="$DEVKITPRO/tools/bin:$DEVKITA64/bin:$DEVKITARM/bin:$PATH"
hash=$(git -C "$root" rev-parse --short=16 HEAD)
case "${1:-core}" in
    core) directory=$root; makefile=atmosphere.mk; target=package3; checked=0 ;;
    loader) directory=$root/stratosphere/nexus_loader; makefile=system_module.mk; target=all; checked=0 ;;
    *) echo 'Usage: sh scripts/build-nexus.sh [core|loader]' >&2
       echo 'Nexus uses unmodified upstream HOC separately; historical combined HOC builds are not used.' >&2
       exit 2 ;;
esac
cd "$directory"
exec make -j"${JOBS:-4}" -f "$makefile" "$target" PYTHON=python3 \
    NEXUS_CORE="$root" ATMOSPHERE_CHECKED_LIBSTRATOSPHERE="$checked" \
    ATMOSPHERE_COMPILER_NAME=gcc ATMOSPHERE_BUILD_NAME=release \
    ATMOSPHERE_BOARD=nx-hac-001 ATMOSPHERE_CPU=arm-cortex-a57 \
    ATMOSPHERE_GIT_BRANCH=nexus ATMOSPHERE_GIT_REVISION=nexus-1.12.2-rf3 \
    ATMOSPHERE_GIT_HASH="$hash" \
    ATMOSPHERE_BUILD_SETTINGS='--param=ggc-min-expand=10 --param=ggc-min-heapsize=32768 -fstack-usage'
