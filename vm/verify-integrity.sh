#!/usr/bin/env bash
# Run INSIDE the lethe VM after vm/repro-deadlock.sh: verify that data
# written under concurrent load decrypts correctly (race fix) and that a
# fresh import works with eagerly loaded ERLs (deadlock fix, import path).

set -uo pipefail

BUILD="${LETHE_BUILD:-$HOME/lethe-build}"
POOL=lethetest
MNT_DS=$POOL/enc

cd "$BUILD"

MNT=$(sudo ./zfs get -H -o value mountpoint $MNT_DS)

echo "=== checksums before export ==="
sudo find "$MNT" -type f -exec md5sum {} + | sort -k2 | sudo tee /tmp/md5.before >/dev/null
wc -l /tmp/md5.before

echo "=== export + import (drops all in-memory lethe state) ==="
sudo ./zpool export $POOL
sudo ./zpool import -d /var/tmp $POOL
echo "lethe-passphrase" | sudo ./zfs load-key $MNT_DS
sudo ./zfs mount $MNT_DS

echo "=== checksums after import ==="
sudo find "$MNT" -type f -exec md5sum {} + | sort -k2 | sudo tee /tmp/md5.after >/dev/null

if diff -u /tmp/md5.before /tmp/md5.after; then
	echo "OK: all file contents identical across export/import"
else
	echo "FAIL: checksum mismatch after reimport"
fi

echo "=== scrub ==="
sudo ./zpool scrub -w $POOL
sudo ./zpool status -v $POOL | tail -20

echo "=== dmesg tail (errors?) ==="
sudo dmesg | grep -iE "hung|blocked|checksum|error" | tail -10
echo "=== done ==="
