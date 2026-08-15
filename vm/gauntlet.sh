#!/usr/bin/env bash
# Run INSIDE the lethe VM: full regression gauntlet -- userspace harnesses
# then kernel end-to-end scripts. Exit 0 iff everything passes.
set -uo pipefail

BUILD="${LETHE_BUILD:-$HOME/lethe-build}"
SRC="${LETHE_SRC:-$HOME/lethe}"
FAILS=0

run() { echo "=== $* ==="; "$@"; local rc=$?; \
	[ $rc -ne 0 ] && { echo "!!! FAILED ($rc): $*"; FAILS=$((FAILS+1)); }; }

cd "$BUILD"

# Userspace harnesses (compiled fresh from the synced build tree).
for t in erl_harness khf_test erl_delete_test erl_readpure_test btreemap_delete_test erl_sparse_test; do
	[ -f "$SRC/vm/$t.c" ] || continue
	if gcc -O1 -g -I include -o "/tmp/$t" "$SRC/vm/$t.c" \
		module/zfs/kht_*.c module/zfs/btree?*.c module/zfs/str.c \
		module/zfs/speck.c module/zfs/sha.c module/zfs/hex.c; then
		run "/tmp/$t"
	else
		echo "!!! FAILED to compile $t"; FAILS=$((FAILS+1))
	fi
done

# Kernel end-to-end (each script loads modules + builds a fresh pool).
run bash "$SRC/vm/repro-deadlock.sh"
run bash "$SRC/vm/verify-integrity.sh"
run bash "$SRC/vm/test-delete.sh"
[ -f "$SRC/vm/test-readonly.sh" ] && run bash "$SRC/vm/test-readonly.sh"
[ -f "$SRC/vm/test-churn.sh" ] && run bash "$SRC/vm/test-churn.sh"

echo "=== gauntlet: $([ $FAILS -eq 0 ] && echo PASS || echo "FAIL ($FAILS)") ==="
exit $FAILS
