#!/usr/bin/env bash
# Run INSIDE the lethe VM (after vm/sync-build.sh): end-to-end test of
# delete-path key purging. Verifies that deleting files, truncating, and
# destroying datasets never harms surviving data, across txg syncs and a
# full export/import, and that the purge machinery actually ran.

set -uo pipefail

BUILD="${LETHE_BUILD:-$HOME/lethe-build}"
POOL=lethetest
IMG=/var/tmp/lethe-pool.img
DS=$POOL/enc
FAILS=0

cd "$BUILD"

check() { # check <ok-condition-exit-code> <label>
	if [ "$1" -eq 0 ]; then echo "ok: $2"; else echo "FAIL: $2"; FAILS=$((FAILS+1)); fi
}

reimport() {
	sudo ./zpool export $POOL &&
	sudo ./zpool import -d /var/tmp $POOL &&
	echo "lethe-passphrase" | sudo ./zfs load-key -a &&
	sudo ./zfs mount -a
}

# Fresh pool + encrypted dataset.
sudo ./zpool destroy $POOL 2>/dev/null
sudo ./scripts/zfs.sh -u 2>/dev/null
sudo ./scripts/zfs.sh
# Prompt purging: release deleted inodes/dentries immediately so
# zfs_rmnode -> dmu_object_free -> key purge runs at rm time instead of
# whenever the VFS evicts the cached inode.
echo 1 | sudo tee /sys/module/zfs/parameters/zfs_delete_inode >/dev/null
echo 1 | sudo tee /sys/module/zfs/parameters/zfs_delete_dentry >/dev/null
sudo rm -f "$IMG" && truncate -s 4G "$IMG"
sudo ./zpool create -f $POOL "$IMG"
echo "lethe-passphrase" | sudo ./zfs create \
	-o encryption=on -o keyformat=passphrase -o keylocation=prompt $DS
MNT=$(sudo ./zfs get -H -o value mountpoint $DS)
sudo dmesg -C

# --- 1. Create files, delete some, verify survivors ---
for f in f1 f2 f3 f4; do
	sudo dd if=/dev/urandom of="$MNT/$f" bs=1M count=2 status=none
done
sudo ./zpool sync $POOL
M1=$(sudo md5sum "$MNT/f1" | cut -d' ' -f1)
M3=$(sudo md5sum "$MNT/f3" | cut -d' ' -f1)
M3H=$(sudo head -c 1048576 "$MNT/f3" | md5sum | cut -d' ' -f1)

sudo rm "$MNT/f2" "$MNT/f4"
sudo ./zpool sync $POOL
sudo ./zpool sync $POOL   # epoch boundary after the purge

[ "$(sudo md5sum "$MNT/f1" | cut -d' ' -f1)" = "$M1" ]; check $? "f1 intact after deletes"
[ "$(sudo md5sum "$MNT/f3" | cut -d' ' -f1)" = "$M3" ]; check $? "f3 intact after deletes"

sudo dmesg | grep "purged" >/dev/null; check $? "object purge logged"
sudo dmesg | grep "freeing lethe_objset" >/dev/null; check $? "backing ERL objects freed"

# --- 2. Survivors across export/import ---
reimport; check $? "reimport after deletes"
[ "$(sudo md5sum "$MNT/f1" | cut -d' ' -f1)" = "$M1" ]; check $? "f1 intact after reimport"
[ "$(sudo md5sum "$MNT/f3" | cut -d' ' -f1)" = "$M3" ]; check $? "f3 intact after reimport"

# --- 3. Truncate, verify surviving head ---
sudo truncate -s 1M "$MNT/f3"
sudo ./zpool sync $POOL
sudo ./zpool sync $POOL
[ "$(sudo md5sum "$MNT/f3" | cut -d' ' -f1)" = "$M3H" ]; check $? "f3 head intact after truncate"
reimport; check $? "reimport after truncate"
[ "$(sudo md5sum "$MNT/f3" | cut -d' ' -f1)" = "$M3H" ]; check $? "f3 head intact after truncate+reimport"
[ "$(sudo md5sum "$MNT/f1" | cut -d' ' -f1)" = "$M1" ]; check $? "f1 intact after truncate+reimport"

# --- 4. Dataset destroy, then a new dataset must be unaffected ---
sudo ./zfs destroy $DS
sudo ./zpool sync $POOL
echo "lethe-passphrase" | sudo ./zfs create \
	-o encryption=on -o keyformat=passphrase -o keylocation=prompt $POOL/enc2
MNT2=$(sudo ./zfs get -H -o value mountpoint $POOL/enc2)
sudo dd if=/dev/urandom of="$MNT2/g1" bs=1M count=2 status=none
sudo ./zpool sync $POOL
G1=$(sudo md5sum "$MNT2/g1" | cut -d' ' -f1)
sudo dmesg | grep -E "objset: [0-9]+ purged" >/dev/null; check $? "objset purge logged for destroy"

sudo ./zpool export $POOL && sudo ./zpool import -d /var/tmp $POOL &&
	echo "lethe-passphrase" | sudo ./zfs load-key -a && sudo ./zfs mount -a
check $? "reimport after dataset destroy"
[ "$(sudo md5sum "$MNT2/g1" | cut -d' ' -f1)" = "$G1" ]; check $? "g1 intact after destroy+reimport"

# --- 5. Pool health ---
sudo ./zpool scrub -w $POOL
sudo ./zpool status $POOL | grep "with 0 errors" >/dev/null; check $? "scrub clean"

echo "=== $([ $FAILS -eq 0 ] && echo PASS || echo "FAIL ($FAILS)") ==="
exit $FAILS
