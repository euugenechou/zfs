#!/usr/bin/env bash
# Run INSIDE the lethe VM against the NON-DEBUG build:
#   bash ~/lethe/vm/sync-build.sh perf && bash ~/lethe/vm/perf-fio.sh
# Prints fio READ/WRITE bandwidth lines per (workload, jobs). Copy the
# output into vm/perf-log.md on the host with a heading for the step.
set -euo pipefail

BUILD="${LETHE_BUILD_PERF:-$HOME/lethe-build-perf}"
POOL=lethetest
IMG=/var/tmp/lethe-pool.img
DS=$POOL/enc

cd "$BUILD"
sudo ./zpool destroy $POOL 2>/dev/null || true
sudo ./scripts/zfs.sh -u 2>/dev/null || true
sudo ./scripts/zfs.sh
# Small ARC so steady-state reads miss and actually exercise decryption.
echo $((256 * 1024 * 1024)) | sudo tee /sys/module/zfs/parameters/zfs_arc_max >/dev/null
sudo rm -f "$IMG" && truncate -s 8G "$IMG"
sudo ./zpool create -f $POOL "$IMG"
echo "lethe-passphrase" | sudo ./zfs create -o encryption=on \
	-o keyformat=passphrase -o keylocation=prompt -o atime=off $DS
MNT=$(sudo ./zfs get -H -o value mountpoint $DS)

for jobs in 1 2 4 8; do
	for rw in randread randrw; do
		echo "### $rw jobs=$jobs"
		sudo fio --name=perf --directory="$MNT" --rw=$rw --bs=128k \
			--size=256M --numjobs=$jobs --time_based --runtime=30 \
			--group_reporting 2>&1 | grep -E "READ:|WRITE:|err="
	done
done

sudo ./zpool destroy $POOL
