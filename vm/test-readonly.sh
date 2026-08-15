#!/usr/bin/env bash
# Run INSIDE the lethe VM (DEBUG build): a read-only workload must leave
# the lethe epoch clean -- no captures, no ERL rewrites, and identical
# file contents. Detects regressions of read-path purity at the system
# level (mark-on-read would show up as __lethe_capture activity).
set -uo pipefail

BUILD="${LETHE_BUILD:-$HOME/lethe-build}"
POOL=lethetest
IMG=/var/tmp/lethe-pool.img
DS=$POOL/enc
FAILS=0

check() {
	if [ "$1" -eq 0 ]; then echo "ok: $2"; else echo "FAIL: $2"; FAILS=$((FAILS+1)); fi
}

cd "$BUILD"
sudo ./zpool destroy $POOL 2>/dev/null
sudo ./scripts/zfs.sh -u 2>/dev/null
sudo ./scripts/zfs.sh
sudo rm -f "$IMG" && truncate -s 4G "$IMG"
sudo ./zpool create -f $POOL "$IMG"
echo "lethe-passphrase" | sudo ./zfs create -o encryption=on \
	-o keyformat=passphrase -o keylocation=prompt -o atime=off $DS
MNT=$(sudo ./zfs get -H -o value mountpoint $DS)

for f in a b c d; do
	sudo dd if=/dev/urandom of="$MNT/$f" bs=1M count=4 status=none
done
sudo ./zpool sync $POOL
M1=$(sudo md5sum "$MNT"/a "$MNT"/b "$MNT"/c "$MNT"/d | md5sum | cut -d' ' -f1)

# Reimport so reads decrypt from disk instead of the ARC, then let any
# import/mount-time writes settle before arming the log check.
sudo ./zpool export $POOL
sudo ./zpool import -d /var/tmp $POOL
echo "lethe-passphrase" | sudo ./zfs load-key -a
sudo ./zfs mount -a
sudo ./zpool sync $POOL
sudo dmesg -C

M2=$(sudo md5sum "$MNT"/a "$MNT"/b "$MNT"/c "$MNT"/d | md5sum | cut -d' ' -f1)
sudo ./zpool sync $POOL
sudo ./zpool sync $POOL

[ "$M2" = "$M1" ]; check $? "file contents intact after reimport"
CAPTURES=$(sudo dmesg | grep -c "__lethe_capture")
[ "$CAPTURES" -eq 0 ]; check $? "no captures after read-only workload (saw $CAPTURES)"

sudo ./zpool destroy $POOL
echo "=== $([ $FAILS -eq 0 ] && echo PASS || echo "FAIL ($FAILS)") ==="
exit $FAILS
