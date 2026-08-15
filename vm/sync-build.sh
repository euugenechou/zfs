#!/usr/bin/env bash
# Run INSIDE the lethe Lima VM: sync the macOS-mounted repo to VM-local
# disk and build the ZFS kernel module + userland there.
#
#   vm/sync-build.sh            # sync + configure (first time) + make
#   vm/sync-build.sh sync       # sync only
#   vm/sync-build.sh perf       # sync + configure (non-debug) + make into
#                                # ~/lethe-build-perf, for fio baselines
#
# Build tree: ~/lethe-build (VM-local; never on the virtiofs mount).
# Perf build tree: ~/lethe-build-perf (VM-local, non-debug).

set -euo pipefail

SRC="${LETHE_SRC:-$HOME/lethe}"
DST="${LETHE_BUILD:-$HOME/lethe-build}"

MODE="${1:-debug}"

if [ "$MODE" = "perf" ]; then
	DST="${LETHE_BUILD_PERF:-$HOME/lethe-build-perf}"
	mkdir -p "$DST"
	rsync -a --delete \
		--exclude '.git' \
		--exclude 'vm/' \
		"$SRC"/ "$DST"/
else
	mkdir -p "$DST"
	rsync -a --delete \
		--exclude '.git' \
		--exclude 'vm/' \
		"$SRC"/ "$DST"/
fi

[ "$MODE" = "sync" ] && exit 0

cd "$DST"
if [ ! -x configure ]; then
	./autogen.sh
fi
if [ ! -f Makefile ]; then
	if [ "$MODE" = "perf" ]; then
		./configure --enable-debuginfo
	else
		./configure --enable-debug --enable-debuginfo
	fi
fi
make -s -j"$(nproc)"
