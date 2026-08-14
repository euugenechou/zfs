#!/usr/bin/env bash
# Run INSIDE the lethe Lima VM: sync the macOS-mounted repo to VM-local
# disk and build the ZFS kernel module + userland there.
#
#   vm/sync-build.sh            # sync + configure (first time) + make
#   vm/sync-build.sh sync       # sync only
#
# Build tree: ~/lethe-build (VM-local; never on the virtiofs mount).

set -euo pipefail

SRC="${LETHE_SRC:-$HOME/lethe}"
DST="${LETHE_BUILD:-$HOME/lethe-build}"

mkdir -p "$DST"
rsync -a --delete \
	--exclude '.git' \
	--exclude 'vm/' \
	"$SRC"/ "$DST"/

[ "${1:-}" = "sync" ] && exit 0

cd "$DST"
if [ ! -x configure ]; then
	./autogen.sh
fi
if [ ! -f Makefile ]; then
	./configure --enable-debug --enable-debuginfo
fi
make -s -j"$(nproc)"
