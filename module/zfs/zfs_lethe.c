#include <sys/dmu.h>
#include <sys/zap.h>
#include <sys/zfs_context.h>
#include <sys/zfs_lethe.h>
#include <lethe/log.h>

#ifdef _KERNEL
#include <sys/zfs_znode.h>

static uint64_t
zfs_lethe_meta_load(objset_t *os, uint64_t lethe_meta_obj, struct Erl *lethe_meta)
{
	dmu_buf_t *db;
	uint64_t lethe_meta_size;

	// Acquire bonus buffer, get serialized metadata size, then release buffer.
	VERIFY(dmu_bonus_hold(os, lethe_meta_obj, FTAG, &db) == 0);
	lethe_meta_size = *(uint64_t *)db->db_data;
	dmu_buf_rele(db, FTAG);

	// If serialized size is zero, then there's nothing to deserialize.
	// This should never happen, but better safe than sorry.
	if (lethe_meta_size == 0) {
		return lethe_meta_size;
	}

	// Create vector to hold the serialized metadata.
	// There isn't an ergonomic way to do this, so this will have to do.
	vec(uint8_t) bytes = vec_new();
	vec_reserve(&bytes, lethe_meta_size);
	vec_set_len(&bytes, lethe_meta_size);

	// Fill vector with serialized metadata.
	VERIFY(dmu_read(
		os,
		lethe_meta_obj,
		0,
		lethe_meta_size,
		bytes,
		DMU_READ_PREFETCH
	) == 0);

	// Deserialize bytes back into metadata.
	*lethe_meta = erl_deserialize(&bytes);

	// Clean up.
	vec_drop(&bytes);

	return lethe_meta_size;
}

void
zfs_lethe_meta_new(zfsvfs_t *zfsvfs)
{
	// Acquire metadata lock.
	rw_enter(&zfsvfs->lethe_meta_lock, RW_WRITER);

	// Nothing to do if metadata was already loaded.
	if (zfsvfs->lethe_meta_loaded) {
		rw_exit(&zfsvfs->lethe_meta_lock);
		return;
	}

	// Find the metadata object.
	zap_lookup(
		zfsvfs->z_os,
		MASTER_NODE_OBJ,
		ZFS_LETHE_METADATA,
		sizeof(uint64_t),
		1,
		&zfsvfs->lethe_meta_obj
	);

	// Load the metadata if object was found.
	// Otherwise, make new metadata instance.
	if (zfsvfs->lethe_meta_obj != 0) {
		lethe_info("zfs_lethe_meta_new(): loaded\n");
		zfsvfs->lethe_meta_size = zfs_lethe_meta_load(
			zfsvfs->z_os,
			zfsvfs->lethe_meta_obj,
			&zfsvfs->lethe_meta
		);
	} else {
		lethe_info("zfs_lethe_meta_new(): created\n");
		uint64_t fanouts[] = { 8, 64, 32, 16, 4 };
		zfsvfs->lethe_meta = erl_new(fanouts, 5);
	}

	// Mark metadata as loaded, release metadata lock.
	zfsvfs->lethe_meta_loaded = B_TRUE;
	rw_exit(&zfsvfs->lethe_meta_lock);
}

void
zfs_lethe_meta_drop(zfsvfs_t *zfsvfs)
{
	// Acquire metadata lock.
	rw_enter(&zfsvfs->lethe_meta_lock, RW_WRITER);

	// Only drop the metadata if it was loaded.
	if (!zfsvfs->lethe_meta_loaded) {
		rw_exit(&zfsvfs->lethe_meta_lock);
		return;
	}

	// Drop the metadata.
	erl_drop(&zfsvfs->lethe_meta);

	// Release metadata lock.
	rw_exit(&zfsvfs->lethe_meta_lock);
}

void
zfs_lethe_meta_sync(zfsvfs_t *zfsvfs, dmu_tx_t *tx)
{
	return; // TODO: remove when ready
	lethe_info("zfs_lethe_meta_sync()\n");

	// Acquire metadata lock.
	rw_enter(&zfsvfs->lethe_meta_lock, RW_WRITER);

	// Only sync the metadata if it was loaded.
	if (!zfsvfs->lethe_meta_loaded) {
		rw_exit(&zfsvfs->lethe_meta_lock);
		return;
	}

	// Serialize metadata first.
	vec(uint8_t) bytes = erl_serialize(&zfsvfs->lethe_meta);

	// Allocate object and add to ZAP if not yet created.
	if (zfsvfs->lethe_meta_obj == 0) {
		lethe_info("zfs_lethe_meta_sync(): alloc new object\n");

		zfsvfs->lethe_meta_obj = dmu_object_alloc(
			zfsvfs->z_os,
			DMU_OTN_ZAP_METADATA,
			vec_len(&bytes),
			DMU_OTN_UINT64_METADATA,
			sizeof(uint64_t),
			tx
		);

		VERIFY(zap_add(
			zfsvfs->z_os,
			MASTER_NODE_OBJ,
			ZFS_LETHE_METADATA,
			sizeof(uint64_t),
			1,
			&zfsvfs->lethe_meta_obj,
			tx
		) == 0);
	} else {
		lethe_info("zfs_lethe_meta_sync(): update existing object\n");
	}

	// Update size of serialized metadata.
	zfsvfs->lethe_meta_size = vec_len(&bytes);

	// Update DMU with serialized metadata.
	dmu_write(
		zfsvfs->z_os,
		zfsvfs->lethe_meta_obj,
		0,
		zfsvfs->lethe_meta_size,
		bytes,
		tx
	);

	dmu_buf_t *db;

	// Acquire bonus buffer.
	VERIFY(dmu_bonus_hold(
		zfsvfs->z_os,
		zfsvfs->lethe_meta_obj,
		FTAG,
		&db
	) == 0);

	// Mark bonus buffer as dirtied.
	dmu_buf_will_dirty(db, tx);

	// Update bonus buffer with updated serialized metadata size.
	*(uint64_t *)db->db_data = zfsvfs->lethe_meta_size;

	// Release bonus buffer.
	dmu_buf_rele(db, FTAG);

	// Clean up.
	vec_drop(&bytes);

	// Release metadata lock.
	rw_exit(&zfsvfs->lethe_meta_lock);
}

void
zfs_lethe_meta_txhold(zfsvfs_t *zfsvfs, dmu_tx_t *tx) {
	return; // TODO: remove when ready
	// Acquire metadata lock.
	rw_enter(&zfsvfs->lethe_meta_lock, RW_READER);

	// Serialize metadata to get its size.
	vec(uint8_t) bytes = erl_serialize(&zfsvfs->lethe_meta);

	if (zfsvfs->lethe_meta_obj == 0) {
		dmu_tx_hold_bonus(tx, DMU_NEW_OBJECT);
		dmu_tx_hold_write(
			tx,
			DMU_NEW_OBJECT,
			0,
			vec_len(&bytes)
		);
		dmu_tx_hold_zap(tx, MASTER_NODE_OBJ, FALSE, NULL);
	} else {
		dmu_tx_hold_bonus(tx, zfsvfs->lethe_meta_obj);
		dmu_tx_hold_write(
			tx,
			zfsvfs->lethe_meta_obj,
			0,
			vec_len(&bytes)
		);
	}

	// Clean up.
	vec_drop(&bytes);

	// Release metadata lock.
	rw_exit(&zfsvfs->lethe_meta_lock);
}

struct Str
zfs_lethe_meta_file_name(uint64_t z_id) {
	char scratch[21] = { 0 };
	snprintf(scratch, sizeof(scratch), "z_id.%llu", z_id);

	struct Str name = str_from_raw(scratch);
	str_push(&name, '\0');

	return name;
}

void
zfs_lethe_meta_file_new(znode_t *zp, zfsvfs_t *zfsvfs)
{
	// Acquire metadata lock.
	rw_enter(&zp->lethe_meta_lock, RW_WRITER);

	// Nothing to do if metadata was already loaded.
	if (zp->lethe_meta_loaded) {
		rw_exit(&zp->lethe_meta_lock);
		return;
	}

	// Generate the znode's unique name.
	struct Str name = zfs_lethe_meta_file_name(zp->z_id);

	// Find the metadata object.
	zap_lookup(
		zfsvfs->z_os,
		MASTER_NODE_OBJ,
		str_buf(&name),
		sizeof(uint64_t),
		1,
		&zfsvfs->lethe_meta_obj
	);

	// Drop the generated name.
	str_drop(&name);

	// Load the metadata if object was found.
	// Otherwise, make new metadata instance.
	if (zp->lethe_meta_obj != 0) {
		lethe_info("zfs_lethe_meta_file_new(): loaded\n");
		zp->lethe_meta_size = zfs_lethe_meta_load(
			zfsvfs->z_os,
			zp->lethe_meta_obj,
			&zp->lethe_meta
		);
	} else {
		lethe_info("zfs_lethe_meta_file_new(): created\n");
		uint64_t fanouts[] = { 8, 16, 4 };
		zp->lethe_meta = erl_new(fanouts, 3);
	}

	// Mark metadata as loaded, release metadata lock.
	zp->lethe_meta_loaded = B_TRUE;
	rw_exit(&zp->lethe_meta_lock);
}

void
zfs_lethe_meta_file_drop(znode_t *zp)
{
	// Acquire metadata lock.
	rw_enter(&zp->lethe_meta_lock, RW_WRITER);

	// Only drop the metadata if it was loaded.
	if (!zp->lethe_meta_loaded) {
		rw_exit(&zp->lethe_meta_lock);
		return;
	}

	// Drop the metadata.
	erl_drop(&zp->lethe_meta);

	// Release metadata lock.
	rw_exit(&zp->lethe_meta_lock);
}

void
zfs_lethe_meta_file_sync(znode_t *zp, zfsvfs_t *zfsvfs, dmu_tx_t *tx)
{
	lethe_info("zfs_lethe_meta_sync()\n");

	// Acquire metadata lock.
	rw_enter(&zp->lethe_meta_lock, RW_WRITER);

	// Only sync the metadata if it was loaded.
	if (!zp->lethe_meta_loaded) {
		rw_exit(&zp->lethe_meta_lock);
		return;
	}

	// Serialize metadata first.
	vec(uint8_t) bytes = erl_serialize(&zp->lethe_meta);

	// Allocate object and add to ZAP if not yet created.
	if (zp->lethe_meta_obj == 0) {
		lethe_info("zfs_lethe_meta_file_sync(): alloc new object\n");

		zp->lethe_meta_obj = dmu_object_alloc(
			zfsvfs->z_os,
			DMU_OTN_ZAP_METADATA,
			vec_len(&bytes),
			DMU_OTN_UINT64_METADATA,
			sizeof(uint64_t),
			tx
		);

		struct Str name = zfs_lethe_meta_file_name(zp->z_id);

		VERIFY(zap_add(
			zfsvfs->z_os,
			MASTER_NODE_OBJ,
			str_buf(&name),
			sizeof(uint64_t),
			1,
			&zfsvfs->lethe_meta_obj,
			tx
		) == 0);

		str_drop(&name);
	} else {
		lethe_info("zfs_lethe_meta_file_sync(): update existing object\n");
	}

	// Update size of serialized metadata.
	zp->lethe_meta_size = vec_len(&bytes);

	// Update DMU with serialized metadata.
	dmu_write(
		zfsvfs->z_os,
		zp->lethe_meta_obj,
		0,
		zp->lethe_meta_size,
		bytes,
		tx
	);

	dmu_buf_t *db;

	// Acquire bonus buffer.
	VERIFY(dmu_bonus_hold(
		zfsvfs->z_os,
		zp->lethe_meta_obj,
		FTAG,
		&db
	) == 0);

	// Mark bonus buffer as dirtied.
	dmu_buf_will_dirty(db, tx);

	// Update bonus buffer with updated serialized metadata size.
	*(uint64_t *)db->db_data = zp->lethe_meta_size;

	// Release bonus buffer.
	dmu_buf_rele(db, FTAG);

	// Clean up.
	vec_drop(&bytes);

	// Release metadata lock.
	rw_exit(&zp->lethe_meta_lock);
}

void
zfs_lethe_meta_file_txhold(znode_t *zp, dmu_tx_t *tx) {
	// Acquire metadata lock.
	rw_enter(&zp->lethe_meta_lock, RW_READER);

	// Serialize metadata to get its size.
	vec(uint8_t) bytes = erl_serialize(&zp->lethe_meta);

	if (zp->lethe_meta_obj == 0) {
		dmu_tx_hold_bonus(tx, DMU_NEW_OBJECT);
		dmu_tx_hold_write(
			tx,
			DMU_NEW_OBJECT,
			0,
			vec_len(&bytes)
		);
		dmu_tx_hold_zap(tx, MASTER_NODE_OBJ, FALSE, NULL);
	} else {
		dmu_tx_hold_bonus(tx, zp->lethe_meta_obj);
		dmu_tx_hold_write(
			tx,
			zp->lethe_meta_obj,
			0,
			vec_len(&bytes)
		);
	}

	// Clean up.
	vec_drop(&bytes);

	// Release metadata lock.
	rw_exit(&zp->lethe_meta_lock);
}

// struct KhtKey zfs_lethe_meta_block_key()



// Export symbols for ZFS module.
EXPORT_SYMBOL(zfs_lethe_meta_new);
EXPORT_SYMBOL(zfs_lethe_meta_drop);
EXPORT_SYMBOL(zfs_lethe_meta_sync);
EXPORT_SYMBOL(zfs_lethe_meta_txhold);
EXPORT_SYMBOL(zfs_lethe_meta_file_new);
EXPORT_SYMBOL(zfs_lethe_meta_file_drop);
EXPORT_SYMBOL(zfs_lethe_meta_file_sync);
EXPORT_SYMBOL(zfs_lethe_meta_file_txhold);
#endif
