#pragma once

#include <lethe/btreemap.h>
#include <lethe/hashmap.h>
#include <lethe/kht.h>
#include <sys/dsl_crypt.h>
#include <sys/spa.h>
#include <sys/zio.h>

//! This file contains the API for Lethe, which, at its heart, is an efficient
//! key management scheme designed to provide secure deletion through
//! cryptographic erasure. The API in this file is catered somewhat specifically
//! for ZFS, but can be easily tweaked to work with any storage system.
//!
//! The design of Lethe within ZFS is a little complex due to all the internal
//! layers.  Lethe specifically takes advantage of the native encryption that is
//! build into ZFS.  With ZFS native encryption, DSL crypto keys are generated
//! for each block that is persisted. All Lethe does is "hijack" these keys,
//! replacing them with its own keys.  In practice, Lethe could be more
//! efficient by replacing the generated salt for each block instead of the key,
//! but that is left as future work.
//!
//! A little background knowledge on ZFS needs to be presented to truly
//! understand how Lethe is integrated. Any proper usage of ZFS requires the use
//! of a storage pool, commonly referred to as a zpool. A zpool effectively
//! consists of object sets: objects that contains sets of objects, objects in
//! which describe pretty much everything in ZFS. As an example, a ZFS
//! filesystem is an object set that contains file objects and directory
//! objects, each of which contain block pointers to the data blocks that make
//! up the object.
//!
//! The main thing to note here is that block ID aliasing occurs. ZFS chooses to
//! ID blocks relative to the object that they consistute, instead of just
//! having absolute block IDs. Object ID aliasing also occurs for similar
//! reasons, in that ZFS chooses to ID objects relative to the object set
//! they're contained in, instead of just having absolute object IDs within the
//! zpool. As an example, assume ZFS filesystems A and B, both of which only
//! contain a single file; A's file and B's file can have the same object ID.
//!
//! The upside is that the ZIO layer in ZFS uses a bookmark structure,
//! `zbookmark_phys_t`, for all IO operations. The bookmark itself contains the
//! object set ID, the object ID, and the block ID that an IO operation is meant
//! for. The downside is that the ZIO layer is entirely decoupled from the ZPL
//! layer, which is used to handle POSIX functions like `read()` and `write()`
//! which interact with ZFS internals. This means that there isn't much choice
//! in where Lethe can be integrated into ZFS, leaving pretty much only the
//! storage pool allocator (SPA). Fortunately, that's exactly where ZFS already
//! integrates its DSL crypto key store for use with native encryption.
//!
//! The structure of Lethe within the SPA is split into 5 distinct components:
//!
//!  1) The object ERL store
//!  2) The master ERL store
//!  3) The object ERL map
//!  4) The master ERL map
//!  5) The uber ERL
//!
//! The object ERL store and master ERL store are both in-memory containers that
//! hold ERLs. The master ERL store is a `BTreeMap` that maps object set IDs to
//! master ERLs. The object ERL store is a `HashMap` that maps object set IDs to
//! object set specific ERL stores (each a `BTreeMap`), which then in turns maps
//! object IDs to object ERLs.
//!
//! The object ERL map and master ERL map are kept both in memory and on disk.
//! The purpose of the ERL map is to contain mappings of ERLs to on-disk
//! objects.  It is simply not possible keep all ERLs in memory, with a simple
//! reason for this impossibility being that system crashes and involuntary
//! remounts occur (yes, we do live in a world where failures can occur). Thus,
//! Lethe persists ERLs as full-fledged DMU objects. The ERL maps are simply
//! there to map ERLs identified by object set ID and object ID to their on-disk
//! object ID.
//!
//! Up last is the uber ERL, which is meant to provide the encryption keys to
//! protect persisted master ERLs, the same way master ERLs provide the
//! encryption keys to protect persisted object ERLs. The uber ERL itself is
//! also persisted upon sync (when an epoch concludes) and is protected by its
//! own key, which is referred to as either the uber key or epoch key. Data
//! protected by ERL keys that aren't rolled forward to the next epoch is
//! securely deleted when the uber key for the epoch key is securely deleted.
//! Actually storing the uber key on some medium that can handle in-place
//! overwrites for secure deletion is left as future work.
//!
//! TODO:
//!  - Make use of reader-writer lock
//!  - Change where Lethe marks itself as dirty

/// Initializes the ERL stores and maps contained in `spa`. This function only
/// initializes the structures in memory. The actual allocation of on-disk
/// objects is left to `lethe_setup()` and the loading of existing on-disk
/// objects is left to `lethe_load()`.
void lethe_init(spa_t *spa);

/// Destroys the ERL stores and maps contained in `spa`. This function only
/// destroys (frees) the structures in memory. Any allocated on-disk objects are
/// left as is for use on remount.
void lethe_fini(spa_t *spa);

/// Allocates the objects needed for the master and object ERL maps contained in
/// `spa` during transaction `tx`. This function should only be called once
/// when `spa` is created in `spa_create()`.
void lethe_setup(spa_t *spa, dmu_tx_t *tx);

/// Loads the master and object ERL maps into their in-memory structures
/// contained in `spa`. This function should only be called once in
/// `spa_load()`, which is called when ZFS tries to import an exported SPA.
void lethe_load(spa_t *spa);

/// An on-disk ERL object orphaned by key purging, queued on
/// `spa->lethe_purge_queue` for `lethe_sync()` phase B to free (and to
/// remove its `name` from the lethe root object).
struct LethePurgeEntry {
	struct Str name;
	uint64_t object;
};

/// Purges the keys of a deleted object: drops its ERL from the object ERL
/// store, removes its ERL map entry, re-marks its slot in its master ERL
/// (so the next epoch patch makes the serialized object ERL underivable),
/// and queues the backing DMU object for freeing. Call from
/// `dmu_object_free()` before the dnode is torn down. Memory-only under
/// the lethe locks; no-op for untracked objects.
void lethe_object_free(spa_t *spa, uint64_t objset, uint64_t object);

/// Purges the keys of a fully-freed data block range [start, end) of an
/// object (truncate). Re-marks the range in the object's ERL so the next
/// epoch patch rotates those block keys. `end` may be UINT64_MAX for
/// truncate-to-end; the range is clamped to the ERL's block count. No-op
/// if the object has no resident ERL.
void lethe_object_free_range(
	spa_t *spa,
	uint64_t objset,
	uint64_t object,
	uint64_t start,
	uint64_t end
);

/// Purges every key for a destroyed objset: drops all of its object ERLs
/// and its master ERL, removes all their ERL map entries, re-marks the
/// objset's slot in the uber ERL (so the next epoch patch makes the whole
/// dataset underivable), and queues all backing DMU objects for freeing.
/// Call from the dataset-destroy sync path.
void lethe_objset_destroy(spa_t *spa, uint64_t objset);

/// Queues an orphaned on-disk ERL object (and its name in the lethe root
/// object) for `lethe_sync()` phase B to free. Takes ownership of `name`.
void __lethe_queue_purge(spa_t *spa, struct Str name, uint64_t object);

/// Loads the root object from the pool directory object in the `spa`. This
/// function should only be called once in `lethe_load()`, which is called when
/// ZFS tries to import an exported SPA.
void __lethe_load_root(spa_t *spa);

/// Loads the object ERL map into its in-memory structure contained in `spa`.
/// This is strictly used as a helper function in `lethe_load().`
void __lethe_load_object_erlmap(spa_t *spa);

/// Loads the master ERL map into its in-memory structure contained in `spa`.
/// This is strictly used as a helper function in `lethe_load().`
void __lethe_load_master_erlmap(spa_t *spa);

/// Loads the uber ERL into its in-memory structure contained in `spa`.
/// This is strictly used as a helper function in `lethe_load().`
void __lethe_load_uber_erl(spa_t *spa);

/// Generic function for loading an ERL map into its in-memory structure
/// contained in `spa`. This is strictly used as a helper function in both
/// `lethe_load_object_erlmap()` and `lethe_load_master_erlmap()`.
void __lethe_load_erlmap(spa_t *spa, nvlist_t **nvp, uint64_t object);

/// Loads an object ERL into the object ERL store contained in `spa`. The ERL is
/// identified by its object set ID (`objset`) and object ID (`object`);
void __lethe_load_object_erl(spa_t *spa, uint64_t objset, uint64_t object);

/// Loads a master ERL into the master ERL store contained in `spa`. The ERL is
/// identified by its object set ID (`objset`).
void __lethe_load_master_erl(spa_t *spa, uint64_t objset);

/// Eagerly loads every ERL mapped in the master and object ERL maps into
/// their in-memory stores. Called at import (open context) so that the ZIO
/// crypt path never faults an ERL in from disk while holding the lethe locks.
void __lethe_load_all_erls(spa_t *spa);

/// Helper function for loading in the bytes of a serialized ERL given its name
/// and the ERL map (`nvp`) that contains the mapping of its name to its object
/// ID. The object ERL is assumed to be added to the MOS pointed to by the `spa`.
vec(uint8_t) __lethe_load_erl_bytes(
	spa_t *spa,
	const char *name,
	nvlist_t *nvp
);

/// Syncs out all Lethe metadata stored under `spa` during transaction `tx`.
/// This is the only sync-related function that should be called outside of
/// `dsl_lethe.c`. The other sync-related functions could have been `static`
/// functions, but declaring the prototypes here declutters things.
///
/// Runs in two phases: phase A patches, keys, and serializes every dirty ERL
/// entirely in memory while the lethe locks are held; phase B drops the locks
/// and performs all DMU allocation and writes. No lethe lock may ever be held
/// across blocking DMU I/O: the ZIO taskq threads that complete that I/O also
/// take these locks in `lethe_bookmark_key()`, so sleeping on I/O with a lock
/// held deadlocks the pool.
void lethe_sync(spa_t *spa, dmu_tx_t *tx);

/// One dirty ERL captured by phase A of `lethe_sync()`: its serialized
/// (encrypted) bytes plus everything phase B needs to allocate and write its
/// backing object without consulting the ERL stores again.
struct LetheSyncEntry {
	boolean_t is_master;
	uint64_t objset;
	uint64_t object;
	struct Str name;
	vec(uint8_t) bytes;
	uint64_t erlobject;
};

/// Captures the object ERL store for `lethe_sync()` phase A. ERLs that were
/// created or modified during the current epoch are patched, keyed, and
/// serialized into `entries`; entries with `erlobject == 0` still need their
/// backing DMU object allocated in phase B. Memory-only: all lethe locks are
/// held by the caller.
void __lethe_capture_object_erlstore(
	spa_t *spa,
	vec(struct LetheSyncEntry) *entries
);

/// Captures the master ERL store for `lethe_sync()` phase A, in the same way
/// as `__lethe_capture_object_erlstore()` captures the object ERL store.
void __lethe_capture_master_erlstore(
	spa_t *spa,
	vec(struct LetheSyncEntry) *entries
);

/// Helper function for syncing out an ERL during transaction `tx`. The ERL
/// should be serialized (perhaps also encrypted) and passed as `bytes`. The ID
/// of the ERL object is given by `object`. The object itself should be part of
/// the MOS pointed to by the `spa`.
void __lethe_sync_erl_object(
	spa_t *spa,
	dmu_tx_t *tx,
	uint64_t object,
	vec(uint8_t) *bytes
);

/// Syncs out the object ERL map during transaction `tx`. It is assumed that the
/// object ERL store has already been synced out. This assumption is made since
/// the object ERL map needs to know the corresponding object ID of each ERL,
/// which can only happen if each object ERL has already been allocated. The map
/// itself is stored as an `nvlist` under the MOS in the given `spa`.
void __lethe_sync_object_erlmap(spa_t *spa, dmu_tx_t *tx);

/// Syncs out the master ERL map during transaction `tx`. It is assumed that the
/// master ERL store has already been synced out. This assumption is made since
/// the master ERL map needs to know the corresponding object ID of each ERL,
/// which can only happen if each master ERL has already been allocated. The map
/// itself is stored as an `nvlist` under the MOS in the given `spa`.
void __lethe_sync_master_erlmap(spa_t *spa, dmu_tx_t *tx);

/// General function for syncing an ERL map (`nvp`) during transaction `tx`.
/// The ERL map is packed (under `lock`, which must not already be held by the
/// caller) and stored in the object with the specified ID (`object`) under the
/// MOS pointed to by the `spa`. The DMU write happens with no lethe locks
/// held.
void __lethe_sync_erlmap(
	spa_t *spa,
	dmu_tx_t *tx,
	nvlist_t *nvp,
	krwlock_t *lock,
	uint64_t object
);

/// Generates the name for an object ERL for use in an `nvlist`. The `objset`
/// and `object` parameters identify the object ERL to name.
struct Str __lethe_object_erl_name(uint64_t objset, uint64_t object);

/// Generates the name for a master ERL for use in an `nvlist`. The `objset`
/// parameter identifies the master ERL to name.
struct Str __lethe_master_erl_name(uint64_t objset);

/// General helper function for generating the name of an ERL identified by
/// its objset ID (`objset`) and object ID (`object`). The `is_master`
/// parameter indicates whether or not the name is meant for an object or master
/// ERL. The `object` parameter is ignored for a master ERL name.
struct Str __lethe_erl_name(
	boolean_t is_master,
	uint64_t objset,
	uint64_t object
);

/// Checks if the object ERL store contained in `spa` contains the object
/// ERL store for a specified object set ID (`objset`).
boolean_t __lethe_contains_object_erlstore(spa_t *spa, uint64_t objset);

/// Checks if the object ERL store contained in `spa` contains the object ERL
/// identified by object set ID (`objset`) and object ID (`object`).
boolean_t __lethe_contains_object_erl(
	spa_t *spa,
	uint64_t objset,
	uint64_t object
);

/// Checks if the master ERL store contained in `spa` contains the master ERL
/// identified by object set ID (`objset`).
boolean_t __lethe_contains_master_erl(spa_t *spa, uint64_t objset);

/// Gets the object ERL store identified by object set ID (`objset`).
struct BTreeMap *__lethe_get_object_erlstore(spa_t *spa, uint64_t objset);

/// Gets the object ERL identified by object set ID (`objset`) and object ID
/// (`object`). Calling this won't load the object ERL even if the object ERL
/// map contains a mapping to it in the `spa`.
struct Erl *__lethe_get_object_erl(
	spa_t *spa,
	uint64_t objset,
	uint64_t object
);

/// Gets the master ERL identified by object set ID (`objset`). Calling this
/// won't load the master ERL even if the master ERL map contains a mapping to
/// it in the `spa`.
struct Erl *__lethe_get_master_erl(
	spa_t *spa,
	uint64_t objset
);

/// Inserts the object ERL store (`erlstore`) for object set ID (`objset`)
/// into the object ERL store contained in `spa`.
boolean_t __lethe_insert_object_erlstore(
	spa_t *spa,
	uint64_t objset,
	struct BTreeMap erlstore
);

/// Inserts the object ERL (`erl`), identified by object set ID (`objset`) and
/// object ID (`object`), into the object ERL store contained in `spa`.
boolean_t __lethe_insert_object_erl(
	spa_t *spa,
	uint64_t objset,
	uint64_t object,
	struct Erl erl
);

/// Inserts the ERL (`erl`), identified by object set ID (`objset`), into the
/// master ERL store contained in `spa`.
boolean_t __lethe_insert_master_erl(
	spa_t *spa,
	uint64_t objset,
	struct Erl erl
);

/// Removes an object ERL store identified by object set ID (`objset`) from the
/// object ERL store contained in `spa`.
boolean_t __lethe_remove_object_erlstore(spa_t *spa, uint64_t objset);

/// Removes an object ERL identified by object set ID (`objset`) and object ID
/// (`object`) from the object ERL store contained in `spa`.
boolean_t __lethe_remove_object_erl(
	spa_t *spa,
	uint64_t objset,
	uint64_t object
);

/// Removes a master ERL identified by object set ID (`objset`) from the master
/// ERL store contained in `spa`.
boolean_t __lethe_remove_master_erl(spa_t *spa, uint64_t objset);

/// Checks if a mapping to an object ERL exists in the object ERL store
/// contained in the `spa`.
boolean_t __lethe_object_erlmap_contains(
	spa_t *spa,
	uint64_t objset,
	uint64_t object
);

/// Checks if a mapping to a master ERL exists in the master ERL store contained
/// in the `spa`.
boolean_t __lethe_master_erlmap_contains(spa_t *spa, uint64_t objset);

/// Checks if a mapping to an ERL with the given `name` exists in the specified
/// ERL map (`nvp`).
boolean_t __lethe_erlmap_contains(nvlist_t *nvp, const char *name);

/// Gets a mapping from the object ERL map contained in `spa` to the ERL with
/// the specified object set ID (`objset`) and object ID (`object`). The mapped
/// ERL object is passed back through `erlobject`.
boolean_t __lethe_object_erlmap_get(
	spa_t *spa,
	uint64_t objset,
	uint64_t object,
	uint64_t *erlobject
);

/// Gets a mapping from the master ERL map contained in `spa` to the ERL with
/// the specified object set ID (`objset`). The mapped ERL object is passed back
/// through `erlobject`.
boolean_t __lethe_master_erlmap_get(
	spa_t *spa,
	uint64_t objset,
	uint64_t *erlobject
);

/// Gets the mapping from a specified ERL map (`nvp`) with the specified `name`.
/// The mapped ERL object is passed back through `erlobject`.
boolean_t __lethe_erlmap_get(
	nvlist_t *nvp,
	const char *name,
	uint64_t *erlobject
);

/// Inserts the mapping from the object ERL with the specified object set ID
/// (`objset`) and object ID (`object`) to the object ERL object with object ID
/// `erlobject`. Trying to add a duplicate mapping results in failure.
boolean_t __lethe_object_erlmap_insert(
	spa_t *spa,
	uint64_t objset,
	uint64_t object,
	uint64_t erlobject
);

/// Inserts the mapping from the master ERL with the specified object set ID
/// (`objset`) to the master ERL object with object ID `erlobject`. Trying to
/// add a duplicate mapping results in failure.
boolean_t __lethe_master_erlmap_insert(
	spa_t *spa,
	uint64_t objset,
	uint64_t erlobject
);

/// Inserts the mapping from `name` to `erlobject` into the specified ERL map
/// (`nvlist`). Trying to add a duplicate mapping results in failure.
boolean_t __lethe_erlmap_insert(
	nvlist_t *nvp,
	const char *name,
	uint64_t erlobject
);

/// Removes the mapping for the object ERL with the specified object set ID
/// (`objset`) and object ID (`object`). Trying to remove a non-existent mapping
/// results in failure.
boolean_t __lethe_object_erlmap_remove(
	spa_t *spa,
	uint64_t objset,
	uint64_t object
);

/// Removes the mapping for the master ERL with the specified object set ID
/// (`objset`). Trying to remove a non-existent mapping results in failure.
boolean_t __lethe_master_erlmap_remove(
	spa_t *spa,
	uint64_t objset
);

/// Removes the mapping for the ERL with the specified `name` from the specified
/// ERL map (`nvp`). Trying to remove a non-existent mapping results in failure.
boolean_t __lethe_erlmap_remove(nvlist_t *nvp, const char *name);

/// Generates a block-level key from the object ERL store in the `spa` given a
/// `bookmark` used in the ZIO layer. The generated key is a read key if `read`
/// is true. Otherwise, it's a write key.
struct KhtKey lethe_bookmark_key(
	spa_t *spa,
	boolean_t read,
	const zbookmark_phys_t *bookmark
);

/// The previous-epoch (forest) key for the block named by `bookmark`, for
/// decrypting a block version that predates the current epoch's rewrite of
/// the same blkid. Returns a random (never-matching) key if the block's ERL
/// is not resident.
struct KhtKey lethe_bookmark_prev_key(
	spa_t *spa,
	const zbookmark_phys_t *bookmark
);

/// Generates a block read key from the object ERL store in the `spa` if `read`
/// is true, otherwise it generates a block write key. The block the key is
/// designated for is identified by object set ID (`objset`), object ID
/// (`object`), and block ID (`block`).
struct KhtKey __lethe_block_key(
	spa_t *spa,
	boolean_t read,
	uint64_t objset,
	uint64_t object,
	uint64_t block
);

/// Generates the read key for the object ERL specified by the given object set
/// ID (`objset`) and object ID (`object`).
struct KhtKey __lethe_object_erl_read_key(
	spa_t *spa,
	uint64_t objset,
	uint64_t object
);

/// Generates the write key for the object ERL specified by the given object set
/// ID (`objset`) and object ID (`object`).
struct KhtKey __lethe_object_erl_write_key(
	spa_t *spa,
	uint64_t objset,
	uint64_t object
);

/// Generates the key for the object ERL specified by the given object set ID
/// (`objset`) and object ID (`object`). If `read` is true, the generated key is
/// a read key. Otherwise, it's a write key.
struct KhtKey __lethe_object_erl_key(
	spa_t *spa,
	boolean_t read,
	uint64_t objset,
	uint64_t object
);

/// Generates the read key for the master ERL specified by the given object set
/// ID (`objset`).
struct KhtKey __lethe_master_erl_read_key(spa_t *spa, uint64_t objset);

/// Generates the write key for the master ERL specified by the given object set
/// ID (`objset`).
struct KhtKey __lethe_master_erl_write_key(spa_t *spa, uint64_t objset);

/// Generates the key for the master ERL specified by the given object set ID
/// (`objset`). If `read` is true, the generated key is a read key. Otherwise,
/// it's a write key.
struct KhtKey __lethe_master_erl_key(
	spa_t *spa,
	boolean_t read,
	uint64_t objset
);

/// Hijacks the contents of a DSL crypto key (`dck`), replacing its contents with
/// the contents of a Lethe-generated key (`key`).
/// TODO: Figure out if this breaks when ZFS rotates a salt.
void lethe_hijack_dsl_crypto_key(
	dsl_crypto_key_t *dck,
	struct KhtKey *key
);

/// General function for allocating a new DMU object during transaction `tx`.
/// The created object is added to Lethe's root object under the `spa` and is
/// identified by its specified `name`. The allocated object ID is returned. Any
/// object allocated through this function is stored as part of the MOS. The
/// type of the object is set by `object_type` and the type of its bonus buffer
/// is set by `bonus_type`.
uint64_t __lethe_alloc_object(
	spa_t *spa,
	dmu_tx_t *tx,
	const char *name,
	dmu_object_type_t object_type,
	dmu_object_type_t bonus_type
);

/// Allocates Lethe's root object during transaction `tx`. The allocated root
/// object is added to the pool directory object under the `spa`.
uint64_t __lethe_alloc_root_object(spa_t *spa, dmu_tx_t *tx);
