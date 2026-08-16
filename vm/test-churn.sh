#!/usr/bin/env bash
# Run INSIDE the lethe VM (DEBUG build): fio reads racing a create/rm
# churn loop for 60s. Tortures the structure-lock WRITER/READER handoff
# (first-touch creates + purges vs in-flight derivations) and ErlBox
# free lifetimes.
set -uo pipefail

BUILD="${LETHE_BUILD:-$HOME/lethe-build}"
POOL=lethetest
IMG=/var/tmp/lethe-pool.img
DS=$POOL/enc
FAILS=0

check() {
	if [ "$1" -eq 0 ]; then echo "ok: $2"; else echo "FAIL: $2"; FAILS=$((FAILS+1)); fi
}

reimport() {
	sudo ./zpool export $POOL &&
	sudo ./zpool import -d /var/tmp $POOL &&
	echo "lethe-passphrase" | sudo ./zfs load-key -a &&
	sudo ./zfs mount -a
}

cd "$BUILD"
sudo ./zpool destroy $POOL 2>/dev/null
sudo ./scripts/zfs.sh -u 2>/dev/null
sudo ./scripts/zfs.sh
echo 1 | sudo tee /sys/module/zfs/parameters/zfs_delete_inode >/dev/null
echo 1 | sudo tee /sys/module/zfs/parameters/zfs_delete_dentry >/dev/null
sudo rm -f "$IMG" && truncate -s 4G "$IMG"
sudo ./zpool create -f $POOL "$IMG"
echo "lethe-passphrase" | sudo ./zfs create -o encryption=on \
	-o keyformat=passphrase -o keylocation=prompt -o atime=off $DS
MNT=$(sudo ./zfs get -H -o value mountpoint $DS)

sudo dd if=/dev/urandom of="$MNT/control" bs=1M count=8 status=none
sudo ./zpool sync $POOL
C1=$(sudo md5sum "$MNT/control" | cut -d' ' -f1)
sudo dmesg -C

sudo fio --name=churnread --directory="$MNT" --rw=randread --bs=128k \
	--size=64M --numjobs=4 --time_based --runtime=60 \
	--group_reporting >/tmp/churn-fio.log 2>&1 &
FIO=$!

END=$((SECONDS + 60))
i=0
while [ $SECONDS -lt $END ]; do
	sudo dd if=/dev/urandom of="$MNT/churn$i" bs=1M count=1 status=none
	[ $i -gt 0 ] && sudo rm -f "$MNT/churn$((i - 1))"
	i=$((i + 1))
done

wait $FIO
check $? "fio exited cleanly under churn ($i churn cycles)"
grep " err= 0" /tmp/churn-fio.log >/dev/null
check $? "fio reported err=0"

sudo dmesg | grep -iE "oops|kernel bug|general protection|page fault" >/dev/null
[ $? -ne 0 ]; check $? "no kernel splats in dmesg"

[ "$(sudo md5sum "$MNT/control" | cut -d' ' -f1)" = "$C1" ]
check $? "control file intact after churn"

sudo ./zpool sync $POOL
sudo ./zpool scrub -w $POOL
sudo ./zpool status $POOL | grep "with 0 errors" >/dev/null
check $? "scrub clean"

# Orphaned-map/wrong-key corruption from delete-vs-sync races only
# manifests at import (stale map entry -> load -> wrong-key decrypt),
# so a clean in-pool scrub isn't sufficient: force an export/import/
# load-key/mount cycle and re-verify the control file survives it.
reimport
check $? "reimport after churn"

[ "$(sudo md5sum "$MNT/control" | cut -d' ' -f1)" = "$C1" ]
check $? "control file intact after churn+reimport"

sudo dmesg | grep -iE "oops|kernel bug|general protection|page fault" >/dev/null
[ $? -ne 0 ]; check $? "no kernel splats in dmesg after reimport"

sudo ./zpool destroy $POOL
echo "=== $([ $FAILS -eq 0 ] && echo PASS || echo "FAIL ($FAILS)") ==="
exit $FAILS
