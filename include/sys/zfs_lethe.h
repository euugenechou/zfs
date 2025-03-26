#pragma once

#ifdef _KERNEL
#include <sys/zfs_vfsops.h>
#include <sys/zfs_znode.h>

// zfs_lethe_meta_new() - Creates a new Lethe metadata instance.
//
// Arguments:
//     @zfsvfs: The ZFS VFS instance that the metadata belongs to.
//
// This function sets up a new Lethe metadata object and registers
// and a mapping to it in the ZFS' master node object. if needed. This is
// done by either creating a new Lethe metadata object from scratch, or
// simply loading it if it's already been created in the past. This function
// won't do anything if the metadata object was already loaded.
//
// Context:
//     This function expects that the metadata object's lock has already
//     been initialized. The lock definition is found in `zfs_vfops_os.h`.
void zfs_lethe_meta_new(zfsvfs_t *zfsvfs);

// zfs_lethe_meta_drop() - Destroys an existing Lethe metadata instance.
//
// Arguments:
//     @zfsvfs: The ZFS VFS instance that the metadata belongs to.
//
// This function drops an existing Lethe metadata instance, freeing all
// allocated memory associated with it. This function won't do anything
// if the metadata object isn't loaded.
//
// Context:
//     This function expects that the metadata object's lock has already
//     been initialized. The lock definition is found in `zfs_vfops_os.h`.
void zfs_lethe_meta_drop(zfsvfs_t *zfsvfs);

// zfs_lethe_meta_sync() - Syncs out an existing Lethe metadata instance.
//
// Arguments:
//     @zfsvfs: The ZFS VFS instance that the metadata belongs to.
//     @tx: The transaction in which the sync takes place.
//
// This function syncs out an existing Lethe metadata instance, thereby
// persisting it to persistent storage. This function is effectively
// responsible for handling the changes necessary when a new epoch starts.
// This function won't do anything if the metadata object isn't loaded.
//
// Context:
//     This function expects that the metadata object's lock has already
//     been initialized. The lock definition is found in `zfs_vfops_os.h`.
//     This function also expects that `zfs_lethe_meta_txhold()` has been
//     called to hold objects/buffers before the transaction is assigned.
void zfs_lethe_meta_sync(zfsvfs_t *zfsvfs, dmu_tx_t *tx);

// zfs_lethe_meta_txhold() - Acquires Lethe metadata holds for a transaction.
//
// Arguments:
//     @zfsvfs: The ZFS VFS instance that the metadata belongs to.
//     @tx: The transaction the holds are for.
//
// To perform a transaction as documented in `dmu.h`, we must first create a
// transaction, then hold the objects that will/might be modified during the
// transaction. The modification of the held objects/buffers can be performed
// after the transaction has been assigned to a transaction group. This function
// simply holds all the objects/buffers necessary to sync out the metadata.
//
// Context:
//     This function expects that the metadata object's lock has already
//     been initialized. The lock definition is found in `zfs_vfops_os.h`.
void zfs_lethe_meta_txhold(zfsvfs_t *zfsvfs, dmu_tx_t *tx);

struct Str zfs_lethe_meta_file_name(uint64_t z_id);

void zfs_lethe_meta_file_new(znode_t *zp, zfsvfs_t *zfsvfs);

void zfs_lethe_meta_file_drop(znode_t *zp);

void zfs_lethe_meta_file_sync(znode_t *zp, zfsvfs_t *zfsvfs, dmu_tx_t *tx);

void zfs_lethe_meta_file_txhold(znode_t *zp, dmu_tx_t *tx);
#endif
