#!/usr/bin/env bash
# Run INSIDE the lethe VM (after vm/sync-build.sh): try to reproduce the
# Lethe deadlock/race on an encrypted dataset under concurrent I/O.
#
#   vm/repro-deadlock.sh            # set up pool + run concurrent load
#   vm/repro-deadlock.sh clean      # destroy pool + unload modules
#
# On a hang, khungtaskd (60s timeout, set by vm/lethe.yaml) prints blocked
# stacks to dmesg; this script also fires SysRq-w if fio stalls.

set -uo pipefail

BUILD="${LETHE_BUILD:-$HOME/lethe-build}"
POOL=lethetest
IMG=/var/tmp/lethe-pool.img
MNT_DS=$POOL/enc

cd "$BUILD"

if [ "${1:-}" = "clean" ]; then
	sudo ./zpool destroy $POOL 2>/dev/null
	sudo ./scripts/zfs.sh -u
	sudo rm -f "$IMG"
	exit 0
fi

# Load the freshly built modules (small ARC so reads hit disk -> decrypt).
sudo ./scripts/zfs.sh -u 2>/dev/null
sudo ./scripts/zfs.sh
echo $((256*1024*1024)) | sudo tee /sys/module/zfs/parameters/zfs_arc_max

# File-backed pool + encrypted dataset.
if ! sudo ./zpool list $POOL >/dev/null 2>&1; then
	truncate -s 8G "$IMG"
	sudo ./zpool create -f $POOL "$IMG"
	echo "lethe-passphrase" | sudo ./zfs create \
		-o encryption=on -o keyformat=passphrase -o keylocation=prompt \
		$MNT_DS
fi
MNT=$(sudo ./zfs get -H -o value mountpoint $MNT_DS)
sudo chmod 777 "$MNT"

echo "=== pool ready at $MNT; starting concurrent load ==="
dmesg_start=$(dmesg | wc -l)

# Concurrent mixed load: 8 writers+readers, enough data to exceed the ARC.
sudo timeout 300 fio --name=lethe-race --directory="$MNT" \
	--rw=randrw --rwmixread=50 --bs=16k --size=256m --nrfiles=4 \
	--numjobs=8 --iodepth=4 --ioengine=psync --direct=0 \
	--time_based --runtime=120 --group_reporting
rc=$?

echo "=== fio exit code: $rc (124 = timed out => likely hung) ==="
if [ $rc -ne 0 ]; then
	echo "=== capturing blocked task stacks (SysRq-w) ==="
	echo w | sudo tee /proc/sysrq-trigger >/dev/null
	sleep 2
fi

echo "=== new kernel messages ==="
dmesg | tail -n +$((dmesg_start + 1)) | tail -200
