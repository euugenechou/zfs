#include <lethe/log.h>
#include <lethe/speck.h>
#include <lethe/lethe.h>
#include <sys/debug.h>
#include <sys/dmu_objset.h>
#include <sys/dsl_dir.h>
#include <sys/nvpair.h>
#include <sys/spa_impl.h>
#include <sys/zap.h>

// ZAP entry names for master and object ERL maps.
#define LETHE_ROOT_OBJECT   "lethe_root_object"
#define LETHE_UBER_ERL      "lethe_uber_erl"
#define LETHE_MASTER_ERLMAP "lethe_master_erlmap"
#define LETHE_OBJECT_ERLMAP "lethe_object_erlmap"

// Prefixes used when generating object names.
#define NAME_OBJSET_PREFIX  "objset_"
#define NAME_OBJECT_PREFIX  "object_"

// Not sure why this doesn't already exist.
#ifdef __KERNEL__
	#define PRIu64 "llu"
#endif

// Default KHT fanouts used across all files initially.
// It remains future research to see what default works well.
static uint64_t DEFAULT_FANOUTS[] = { 16, 32, 8 };
static uint64_t DEFAULT_FANOUTS_LEN = 3;

void lethe_init(spa_t *spa) {
	lethe_info("lethe_init(): start\n");

	// Initialize object ERL store fields.
	rw_init(&spa->lethe_object_erlstore_lock, NULL, RW_DEFAULT, NULL);
	spa->lethe_object_erlstore = hashmap_new();

	// Initialize object ERL map fields.
	spa->lethe_object_erlmap_object = 0;
	spa->lethe_object_erlmap_loaded = B_FALSE;
	rw_init(&spa->lethe_object_erlmap_lock, NULL, RW_DEFAULT, NULL);
	nvlist_alloc(&spa->lethe_object_erlmap, NV_UNIQUE_NAME, KM_SLEEP);

	// Initialize master ERL store fields.
	rw_init(&spa->lethe_master_erlstore_lock, NULL, RW_DEFAULT, NULL);
	spa->lethe_master_erlstore = btreemap_new();

	// Initialize master ERL map fields.
	spa->lethe_master_erlmap_object = 0;
	spa->lethe_master_erlmap_loaded = B_FALSE;
	rw_init(&spa->lethe_master_erlmap_lock, NULL, RW_DEFAULT, NULL);
	nvlist_alloc(&spa->lethe_master_erlmap, NV_UNIQUE_NAME, KM_SLEEP);

	// Initialize uber ERL fields.
	spa->lethe_uber_erl_object = 0;
	spa->lethe_uber_erl_loaded = B_FALSE;
	rw_init(&spa->lethe_uber_erl_lock, NULL, RW_DEFAULT, NULL);
	spa->lethe_uber_erl = erl_new(DEFAULT_FANOUTS, DEFAULT_FANOUTS_LEN);

	// Initialize general fields.
	spa->lethe_root_object = 0;
	spa->lethe_root_object_loaded = B_FALSE;
	spa->lethe_epoch_dirty = B_FALSE;

	lethe_info("lethe_init(): end\n");
}

void lethe_fini(spa_t *spa) {
	lethe_info("lethe_fini(): start\n");

	// Destroy object ERL store fields.
	rw_destroy(&spa->lethe_object_erlstore_lock);
	hashmap_drop(&spa->lethe_object_erlstore);

	// Destroy object ERL map fields.
	rw_destroy(&spa->lethe_object_erlmap_lock);
	nvlist_free(spa->lethe_object_erlmap);

	// Destroy master ERL store fields.
	rw_destroy(&spa->lethe_master_erlstore_lock);
	btreemap_drop(&spa->lethe_master_erlstore);

	// Destroy master ERL map fields.
	rw_destroy(&spa->lethe_master_erlmap_lock);
	nvlist_free(spa->lethe_master_erlmap);

	// Destroy uber ERL fields.
	rw_destroy(&spa->lethe_uber_erl_lock);
	erl_drop(&spa->lethe_uber_erl);

	lethe_info("lethe_fini(): end\n");
}

void lethe_setup(spa_t *spa, dmu_tx_t *tx) {
	lethe_info("lethe_setup(): start\n");

	lethe_rw_enter(&spa->lethe_master_erlmap_lock, RW_WRITER);
	lethe_rw_enter(&spa->lethe_object_erlmap_lock, RW_WRITER);
	lethe_rw_enter(&spa->lethe_uber_erl_lock, RW_WRITER);

	// Allocate root object and mark is as loaded.
	spa->lethe_root_object = __lethe_alloc_root_object(spa, tx);
	spa->lethe_root_object_loaded = B_TRUE;

	// Allocate object ERL store and mark it as loaded.
	spa->lethe_object_erlmap_object = __lethe_alloc_object(
		spa,
		tx,
		LETHE_OBJECT_ERLMAP,
		DMU_OT_PACKED_NVLIST,
		DMU_OT_PACKED_NVLIST_SIZE
	);
	spa->lethe_object_erlmap_loaded = B_TRUE;

	// Allocate master ERL store and mark it as loaded.
	spa->lethe_master_erlmap_object = __lethe_alloc_object(
		spa,
		tx,
		LETHE_MASTER_ERLMAP,
		DMU_OT_PACKED_NVLIST,
		DMU_OT_PACKED_NVLIST_SIZE
	);
	spa->lethe_master_erlmap_loaded = B_TRUE;

	// Allocate uber ERL and mark it as loaded.
	spa->lethe_uber_erl_object = __lethe_alloc_object(
		spa,
		tx,
		LETHE_UBER_ERL,
		DMU_OTN_UINT8_METADATA,
		DMU_OTN_UINT64_METADATA
	);
	spa->lethe_uber_erl_loaded = B_TRUE;

	// Mark epoch as dirty since we have structures to sync.
	spa->lethe_epoch_dirty = B_TRUE;

	lethe_rw_exit(&spa->lethe_master_erlmap_lock);
	lethe_rw_exit(&spa->lethe_object_erlmap_lock);
	lethe_rw_exit(&spa->lethe_uber_erl_lock);

	lethe_info("lethe_setup(): end\n");
}

void lethe_load(spa_t *spa) {
	lethe_info("lethe_load(): start\n");

	lethe_rw_enter(&spa->lethe_master_erlmap_lock, RW_WRITER);
	lethe_rw_enter(&spa->lethe_object_erlmap_lock, RW_WRITER);
	lethe_rw_enter(&spa->lethe_uber_erl_lock, RW_WRITER);

	__lethe_load_root(spa);
	__lethe_load_uber_erl(spa);
	__lethe_load_master_erlmap(spa);
	__lethe_load_object_erlmap(spa);

	lethe_rw_exit(&spa->lethe_master_erlmap_lock);
	lethe_rw_exit(&spa->lethe_object_erlmap_lock);
	lethe_rw_exit(&spa->lethe_uber_erl_lock);

	lethe_info("lethe_load(): end\n");
}

void __lethe_load_root(spa_t *spa) {
	if (!spa->lethe_root_object_loaded) {
		VERIFY(0 == zap_lookup(
			spa->spa_meta_objset,
			DMU_POOL_DIRECTORY_OBJECT,
			LETHE_ROOT_OBJECT,
			sizeof(uint64_t),
			1,
			&spa->lethe_root_object
		));
		spa->lethe_root_object_loaded = B_TRUE;
	}
}

void __lethe_load_object_erlmap(spa_t *spa) {
	if (!spa->lethe_object_erlmap_loaded) {
		lethe_info("(start)\n");

		VERIFY(spa->lethe_root_object_loaded);

		VERIFY(0 == zap_lookup(
			spa->spa_meta_objset,
			spa->lethe_root_object,
			LETHE_OBJECT_ERLMAP,
			sizeof(uint64_t),
			1,
			&spa->lethe_object_erlmap_object
		));

		__lethe_load_erlmap(
			spa,
			&spa->lethe_object_erlmap,
			spa->lethe_object_erlmap_object
		);

		spa->lethe_object_erlmap_loaded = B_TRUE;

		lethe_info("(end)\n");
	}
}

void __lethe_load_master_erlmap(spa_t *spa) {
	if (!spa->lethe_master_erlmap_loaded) {
		lethe_info("(start)\n");

		VERIFY(spa->lethe_root_object_loaded);

		VERIFY(0 == zap_lookup(
			spa->spa_meta_objset,
			spa->lethe_root_object,
			LETHE_MASTER_ERLMAP,
			sizeof(uint64_t),
			1,
			&spa->lethe_master_erlmap_object
		));

		__lethe_load_erlmap(
			spa,
			&spa->lethe_master_erlmap,
			spa->lethe_master_erlmap_object
		);

		spa->lethe_master_erlmap_loaded = B_TRUE;

		lethe_info("(end)\n");
	}
}

void __lethe_load_uber_erl(spa_t *spa) {
	if (!spa->lethe_uber_erl_loaded) {
		lethe_info("(start)\n");

		VERIFY(spa->lethe_root_object_loaded);

		VERIFY(0 == zap_lookup(
			spa->spa_meta_objset,
			spa->lethe_root_object,
			LETHE_UBER_ERL,
			sizeof(uint64_t),
			1,
			&spa->lethe_uber_erl_object
		));

		// Acquire the object's bonus buffer.
		dmu_buf_t *db = NULL;
		dmu_bonus_hold(
			spa->spa_meta_objset,
			spa->lethe_uber_erl_object,
			FTAG,
			&db
		);

		// Get the object's size.
		size_t size = *(uint64_t *)db->db_data;
		lethe_info("size = %zu\n", size);
		dmu_buf_rele(db, FTAG);

		// Allocate vector to hold the serialized ERL.
		vec(uint8_t) bytes = vec_new();
		vec_reserve(&bytes, size);
		vec_set_len(&bytes, size);

		// Read in the serialized ERL.
		dmu_read(
			spa->spa_meta_objset,
			spa->lethe_uber_erl_object,
			0,
			size,
			bytes,
			DMU_READ_PREFETCH
		);

		// Replace existing ERL and mark it as loaded.
		erl_drop(&spa->lethe_uber_erl);
		spa->lethe_uber_erl = erl_deserialize(&bytes);
		spa->lethe_uber_erl_loaded = B_TRUE;

		// Clean up.
		vec_drop(&bytes);

		lethe_info("(end)\n");
	}
}

void __lethe_load_erlmap(spa_t *spa, nvlist_t **nvp, uint64_t object) {
    lethe_info("(start)\n");

	// Acquire the ERL map object's bonus buffer, get its size, release.
	dmu_buf_t *db = NULL;
	dmu_bonus_hold(
		spa->spa_meta_objset,
		object,
		FTAG,
		&db
	);
	uint64_t size = *(uint64_t *)db->db_data;
	dmu_buf_rele(db, FTAG);

	// We should never be loading an empty ERL map.
	VERIFY(size != 0);

	// Read in the packed nvlist.
	char *packed_nvp = kmem_alloc(size, KM_SLEEP);
	dmu_read(
		spa->spa_meta_objset,
		object,
		0,
		size,
		packed_nvp,
		DMU_READ_PREFETCH
	);

	// Destroy the given nvlist if it's already been allocated.
	if (*nvp != NULL) {
		nvlist_free(*nvp);
	}

	// Unpack the packed nvlist into the given nvlist.
	nvlist_unpack(packed_nvp, size, nvp, KM_SLEEP);

	// Clean up.
	kmem_free(packed_nvp, size);

    lethe_info("(end)\n");
}

void __lethe_load_object_erl(spa_t *spa, uint64_t objset, uint64_t object) {
    lethe_info("(start)\n");

	if (!__lethe_contains_object_erl(spa, objset, object)) {
		lethe_info("objset: %llu, object: %llu (start)\n", objset, object);

		// Get the name of the object ERL.
		struct Str name = __lethe_object_erl_name(objset, object);

		// Get the bytes of the serialized object ERL.
		vec(uint8_t) bytes = __lethe_load_erl_bytes(
			spa,
			str_buf(&name),
			spa->lethe_object_erlmap
		);

		// Get the ERL's key, deserialize the ERL, and decrypt it.
		struct KhtKey key = __lethe_object_erl_read_key(spa, objset, object);

		struct Str keystr = khtkey_to_string(&key);
		lethe_info(
			"%s: bytes = %zu, key = %.*s\n",
			str_buf(&name),
			vec_len(&bytes),
			(int)str_len(&keystr),
			str_buf(&keystr)
		);
		str_drop(&keystr);

		struct Erl erl = erl_deserialize_keyed(&bytes, &key);

		// Insert ERL into object ERL store.
		__lethe_insert_object_erl(spa, objset, object, erl);

		// Clean up.
		str_drop(&name);
		vec_drop(&bytes);

		VERIFY(__lethe_contains_object_erl(spa, objset, object));

		lethe_info("objset: %llu, object: %llu (end)\n", objset, object);
	}

    lethe_info("(end)\n");
}

void __lethe_load_master_erl(spa_t *spa, uint64_t objset) {
	if (!__lethe_contains_master_erl(spa, objset)) {
		lethe_info("objset = %llu (start)\n", objset);

		// Get the name of the master ERL.
		struct Str name = __lethe_master_erl_name(objset);

		// Get the bytes of the serialized master ERL.
		vec(uint8_t) bytes = __lethe_load_erl_bytes(
			spa,
			str_buf(&name),
			spa->lethe_master_erlmap
		);

		lethe_info("size = %zu\n", vec_len(&bytes));

		// Get the master ERL's key, deserialize the master ERL, and decrypt it.
		struct KhtKey key = __lethe_master_erl_read_key(spa, objset);
		struct Erl erl = erl_deserialize_keyed(&bytes, &key);

		// Insert master ERL into master ERL store.
		__lethe_insert_master_erl(spa, objset, erl);

		// Clean up.
		str_drop(&name);
		vec_drop(&bytes);

		VERIFY(__lethe_contains_master_erl(spa, objset));

		lethe_info("objset = %llu (end)\n", objset);
	}
}

vec(uint8_t) __lethe_load_erl_bytes(
	spa_t *spa,
	const char *name,
	nvlist_t *nvp
) {
	// Get the ID of the ERL object.
	uint64_t erlobject = 0;
	VERIFY(__lethe_erlmap_get(nvp, name, &erlobject));

	// Acquire the object's bonus buffer, get its size, release.
	dmu_buf_t *db = NULL;
	dmu_bonus_hold(spa->spa_meta_objset, erlobject, FTAG, &db);
	uint64_t size = *(uint64_t *)db->db_data;
	dmu_buf_rele(db, FTAG);

	lethe_info("name = %s, size = %llu (start)\n", name, size);

	// Allocate vector to hold the serialized ERL.
	vec(uint8_t) bytes = vec_new();
	vec_reserve(&bytes, size);
	vec_set_len(&bytes, size);

	// Read in the serialized ERL.
	dmu_read(
		spa->spa_meta_objset,
		erlobject,
		0,
		size,
		bytes,
		DMU_READ_PREFETCH
	);

	lethe_info("name = %s, size = %llu (end)\n", name, size);
	return bytes;
}

void lethe_sync(spa_t *spa, dmu_tx_t *tx) {
    lethe_info("current thread start: (%p)\n", (void *)current);

	lethe_rw_enter(&spa->lethe_master_erlstore_lock, RW_WRITER);
	lethe_rw_enter(&spa->lethe_object_erlstore_lock, RW_WRITER);
	lethe_rw_enter(&spa->lethe_master_erlmap_lock, RW_WRITER);
	lethe_rw_enter(&spa->lethe_object_erlmap_lock, RW_WRITER);
	lethe_rw_enter(&spa->lethe_uber_erl_lock, RW_WRITER);

	// Nothing to sync if nothing was modified.
	if (!spa->lethe_epoch_dirty) {
		lethe_rw_exit(&spa->lethe_master_erlstore_lock);
		lethe_rw_exit(&spa->lethe_object_erlstore_lock);
		lethe_rw_exit(&spa->lethe_master_erlmap_lock);
		lethe_rw_exit(&spa->lethe_object_erlmap_lock);
		lethe_rw_exit(&spa->lethe_uber_erl_lock);
		// lethe_info("lethe_sync(): end (sync'ed)\n");
		return;
	}

	// Sync the object ERL store first, then the master ERL store, then the
	// two ERL maps, then the uber ERL. It's imperative that the object ERL
	// store is sync'ed before the master ERL store because each object ERL
	// needs to be protected by a key generated from its corresponding
	// master ERL. It's also imperative that the master ERL store is sync'ed
	// before the uber ERL because each master ERL needs to be protected by
	// a key generated from the uber ERL.
	__lethe_sync_object_erlstore(spa, tx);
	__lethe_sync_master_erlstore(spa, tx);
	__lethe_sync_object_erlmap(spa, tx);
	__lethe_sync_master_erlmap(spa, tx);
	__lethe_sync_uber_erl(spa, tx);

	// Mark that modifications were sync'ed.
	spa->lethe_epoch_dirty = B_FALSE;

	lethe_rw_exit(&spa->lethe_master_erlstore_lock);
	lethe_rw_exit(&spa->lethe_object_erlstore_lock);
	lethe_rw_exit(&spa->lethe_master_erlmap_lock);
	lethe_rw_exit(&spa->lethe_object_erlmap_lock);
	lethe_rw_exit(&spa->lethe_uber_erl_lock);

	// lethe_info("lethe_sync(): end\n");
    lethe_info("current thread end: (%p)\n", (void *)current);
}

void __lethe_sync_object_erlstore(spa_t *spa, dmu_tx_t *tx) {
	lethe_info("(start)\n");

	// Iterate over the master ERL store.
	uint64_t objset = 0;
	struct Erl *master_erl = NULL;
	struct BTreeMapIter master_iter = btreemap_iter(&spa->lethe_master_erlstore);

	while (btreemapiter_next(&master_iter, &objset, &master_erl)) {
		// Iterate over each of the modified objects.
		uint64_t object = 0;
		struct BTreeSetIter object_iter = btreeset_iter(&master_erl->modified);

		// TODO: remove debugging
		struct Str modified_str = btreeset_to_string(&master_erl->modified);
		lethe_info(
			"__lethe_sync_object_erlstore(): modified = %.*s\n",
			(int)str_len(&modified_str),
			str_buf(&modified_str)
		);
		str_drop(&modified_str);

		while (btreesetiter_next(&object_iter, &object)) {
			// Get name for the ERL.
			struct Str name = __lethe_object_erl_name(objset, object);
			lethe_info("__lethe_sync_object_erlstore(): %s\n", str_buf(&name));

			// Allocate new ERL object and map it if it doesn't exist.
			if (!__lethe_object_erlmap_contains(spa, objset, object)) {
				uint64_t erlobject = __lethe_alloc_object(
					spa,
					tx,
					str_buf(&name),
					DMU_OTN_UINT8_METADATA,
					DMU_OTN_UINT64_METADATA
				);
				VERIFY(__lethe_object_erlmap_insert(spa, objset, object, erlobject));
			}

			// Get the mapping for the ERL.
			uint64_t erlobject = 0;
			VERIFY(__lethe_object_erlmap_get(spa, objset, object, &erlobject));
			// lethe_info(
			// 	"__lethe_sync_object_erlstore(): %s -> %" PRIu64 "\n",
			// 	str_buf(&name),
			// 	erlobject
			// );

			// Get the modified ERL, patch it, then reset for next epoch.
			struct Erl *erl = __lethe_get_object_erl(spa, objset, object);
			VERIFY(erl != NULL);
			erl_patch(erl);
			erl_reset(erl);

			// Get the ERL's key, serialize it, encrypt it, and sync it.
			struct KhtKey key = __lethe_object_erl_write_key(spa, objset, object);
			vec(uint8_t) bytes = erl_serialize_keyed(erl, &key);

			struct Str keystr = khtkey_to_string(&key);
			lethe_info(
				"%s: bytes = %zu, key = %.*s\n",
				str_buf(&name),
				vec_len(&bytes),
				(int)str_len(&keystr),
				str_buf(&keystr)
			);
			str_drop(&keystr);

			// Sync the object, then reset it for the next epoch.
			__lethe_sync_erl_object(spa, tx, erlobject, &bytes);

			// Clean up.
			vec_drop(&bytes);
			str_drop(&name);
		}

		btreesetiter_drop(&object_iter);
	}

	btreemapiter_drop(&master_iter);

	lethe_info("(end)\n");
}

void __lethe_sync_master_erlstore(spa_t *spa, dmu_tx_t *tx) {
	lethe_info("(start)\n");

	// Iterate over the master ERL store.
	uint64_t objset = 0;
	struct Erl *erl = NULL;
	struct BTreeMapIter iter = btreemap_iter(&spa->lethe_master_erlstore);

	while (btreemapiter_next(&iter, &objset, &erl)) {
		// Get name for the master ERL.
		struct Str name = __lethe_master_erl_name(objset);
		lethe_info(
			"syncing %.*s\n",
			(int)str_len(&name),
			str_buf(&name)
		);

		// Allocate new master ERL object and map it if it doesn't exist.
		if (!__lethe_master_erlmap_contains(spa, objset)) {
			uint64_t erlobject = __lethe_alloc_object(
				spa,
				tx,
				str_buf(&name),
				DMU_OTN_UINT8_METADATA,
				DMU_OTN_UINT64_METADATA
			);
			__lethe_master_erlmap_insert(spa, objset, erlobject);
		}

		// Get the mapping for the master ERL.
		uint64_t erlobject = 0;
		VERIFY(__lethe_master_erlmap_get(spa, objset, &erlobject));

		// Get the modified master ERL, patch it, and reset if for the next epoch.
		struct Erl *master_erl = __lethe_get_master_erl(spa, objset);
		VERIFY(erl != NULL);
		erl_patch(erl);
		erl_reset(erl);

		// Get the master ERL's key, serialize it, encrypt it, and sync it.
		struct KhtKey key = __lethe_master_erl_write_key(spa, objset);
		vec(uint8_t) bytes = erl_serialize_keyed(master_erl, &key);

		struct Str keystr = khtkey_to_string(&key);
		lethe_info(
			"%s: bytes = %zu, key = %.*s\n",
			str_buf(&name),
			vec_len(&bytes),
			(int)str_len(&keystr),
			str_buf(&keystr)
		);
		str_drop(&keystr);

		__lethe_sync_erl_object(spa, tx, erlobject, &bytes);

		// Clean up.
		vec_drop(&bytes);
		str_drop(&name);
	}

	btreemapiter_drop(&iter);

	lethe_info("(end)\n");
}

void __lethe_sync_erl_object(
	spa_t *spa,
	dmu_tx_t *tx,
	uint64_t object,
	vec(uint8_t) *bytes
) {
	lethe_info("(start)\n");

	// Write the bytes of the ERL.
	dmu_write(spa->spa_meta_objset, object, 0, vec_len(bytes), *bytes, tx);

	// Acquire the ERL object's bonus buffer.
	dmu_buf_t *db = NULL;
	dmu_bonus_hold(spa->spa_meta_objset, object, FTAG, &db);

	// Mark bonus buffer as dirtied, update value, then release.
	dmu_buf_will_dirty(db, tx);
	*(uint64_t *)db->db_data = vec_len(bytes);
	dmu_buf_rele(db, FTAG);

	lethe_info("(end)\n");
}

void __lethe_sync_master_erlmap(spa_t *spa, dmu_tx_t *tx) {
	lethe_info("(start)\n");

	__lethe_sync_erlmap(
		spa,
		tx,
		LETHE_MASTER_ERLMAP,
		spa->lethe_master_erlmap,
		&spa->lethe_master_erlmap_object
	);

	lethe_info("(end)\n");
}

void __lethe_sync_object_erlmap(spa_t *spa, dmu_tx_t *tx) {
	lethe_info("(start)\n");

	__lethe_sync_erlmap(
		spa,
		tx,
		LETHE_OBJECT_ERLMAP,
		spa->lethe_object_erlmap,
		&spa->lethe_object_erlmap_object
	);

	lethe_info("(end)\n");
}

void __lethe_sync_erlmap(
	spa_t *spa,
	dmu_tx_t *tx,
	const char *name,
	nvlist_t *nvp,
	uint64_t *object
) {
	// Get the size of the nvlist.
	uint64_t size = 0;
	nvlist_size(nvp, (size_t *)&size, NV_ENCODE_XDR);

	// Pack the nvlist into contiguous memory.
	char *packed_nvp = kmem_alloc(size, KM_SLEEP);
	nvlist_pack(nvp, &packed_nvp, (size_t *)&size, NV_ENCODE_XDR, KM_SLEEP);

	// Write to the DMU.
	dmu_write(spa->spa_meta_objset, *object, 0, size, packed_nvp, tx);

	// Acquire the object's bonus buffer.
	dmu_buf_t *db = NULL;
	dmu_bonus_hold(spa->spa_meta_objset, *object, FTAG, &db);

	// Mark bonus buffer as dirtied, update value, then release.
	dmu_buf_will_dirty(db, tx);
	*(uint64_t *)db->db_data = size;
	dmu_buf_rele(db, FTAG);

	// Destroy the packed nvlist.
	kmem_free(packed_nvp, size);
}

void __lethe_sync_uber_erl(spa_t *spa, dmu_tx_t *tx) {
	// Patch the uber ERL and reset it for the next epoch.
	erl_patch(&spa->lethe_uber_erl);
	erl_reset(&spa->lethe_uber_erl);

	// Serialize the uber ERL.
	vec(uint8_t) bytes = erl_serialize(&spa->lethe_uber_erl);

	// Sync the serialized ERL bytes.
	__lethe_sync_erl_object(spa, tx, spa->lethe_uber_erl_object, &bytes);

	// Clean up.
	vec_drop(&bytes);
}

struct Str __lethe_object_erl_name(uint64_t objset, uint64_t object) {
	return __lethe_erl_name(B_FALSE, objset, object);
}

struct Str __lethe_master_erl_name(uint64_t objset) {
	return __lethe_erl_name(B_TRUE, objset, 0);
}

struct Str __lethe_erl_name(
	boolean_t is_master,
	uint64_t objset,
	uint64_t object
) {
	struct Str s = str_new();

	char objset_buf[21] = { 0 };
	snprintf(objset_buf, sizeof(objset_buf), "%" PRIu64, objset);

	char object_buf[21] = { 0 };
	snprintf(object_buf, sizeof(object_buf), "%" PRIu64, object);

	if (is_master) {
		str_push_raw(&s, "lethe_objset_master_");
		str_push_raw(&s, objset_buf);
	} else {
		str_push_raw(&s, "lethe_objset_");
		str_push_raw(&s, objset_buf);
		str_push_raw(&s, "_object_");
		str_push_raw(&s, object_buf);
	}

	str_push(&s, '\0');

	return s;
}

boolean_t __lethe_contains_object_erlstore(spa_t *spa, uint64_t objset) {
	return __lethe_get_object_erlstore(spa, objset) != NULL;
}

boolean_t __lethe_contains_object_erl(
	spa_t *spa,
	uint64_t objset,
	uint64_t object
) {
	return __lethe_get_object_erl(spa, objset, object) != NULL;
}

boolean_t __lethe_contains_master_erl(spa_t *spa, uint64_t objset) {
	return __lethe_get_master_erl(spa, objset) != NULL;
}

struct BTreeMap *__lethe_get_object_erlstore(spa_t *spa, uint64_t objset) {
	return hashmap_get(&spa->lethe_object_erlstore, objset);
}

struct Erl *__lethe_get_object_erl(
	spa_t *spa,
	uint64_t objset,
	uint64_t object
) {
	struct BTreeMap *erlstore = __lethe_get_object_erlstore(spa, objset);
	return erlstore ? btreemap_get(erlstore, object) : NULL;
}

struct Erl *__lethe_get_master_erl(
	spa_t *spa,
	uint64_t objset
) {
	return btreemap_get(&spa->lethe_master_erlstore, objset);
}

boolean_t __lethe_insert_object_erlstore(
	spa_t *spa,
	uint64_t objset,
	struct BTreeMap erlstore
) {
	return hashmap_insert(&spa->lethe_object_erlstore, objset, erlstore);
}

boolean_t __lethe_insert_object_erl(
	spa_t *spa,
	uint64_t objset,
	uint64_t object,
	struct Erl erl
) {
	lethe_info("objset: %llu, object: %llu\n", objset, object);

	// Create object's ERL store if it doesn't exist.
	if (!__lethe_contains_object_erlstore(spa, objset)) {
		VERIFY(__lethe_insert_object_erlstore(spa, objset, btreemap_new()));
	}

	// Get object's ERL store.
	struct BTreeMap *erlstore = __lethe_get_object_erlstore(spa, objset);

	// Insert object's ERL into its ERL store if it doesn't exist.
	if (!btreemap_contains(erlstore, object)) {
		btreemap_insert(erlstore, object, erl);

		// Create master ERL for the object's object set if it doesn't exist.
		if (!__lethe_contains_master_erl(spa, objset)) {
			VERIFY(__lethe_insert_master_erl(
				spa,
				objset,
				erl_new(DEFAULT_FANOUTS, DEFAULT_FANOUTS_LEN)
			));
		}

		// Mark object as newly added (modified) under its master ERL.
		struct Erl *master_erl = __lethe_get_master_erl(spa, objset);
		erl_mark_block(master_erl, object);

		return B_TRUE;
	}

	return B_FALSE;
}

boolean_t __lethe_insert_master_erl(
	spa_t *spa,
	uint64_t objset,
	struct Erl erl
) {
	lethe_info("objset = %llu (start)\n", objset);
	if (!btreemap_contains(&spa->lethe_master_erlstore, objset)) {
		lethe_info("objset = %llu (end)\n", objset);
		btreemap_insert(&spa->lethe_master_erlstore, objset, erl);
		return B_TRUE;
	}
	lethe_info("objset = %llu (end)\n", objset);
	return B_FALSE;
}

boolean_t __lethe_remove_master_erl(spa_t *spa, uint64_t objset) {
	if (btreemap_contains(&spa->lethe_master_erlstore, objset)) {
		btreemap_remove(&spa->lethe_master_erlstore, objset);
		return B_TRUE;
	}
	return B_FALSE;
}

boolean_t __lethe_remove_object_erlstore(spa_t *spa, uint64_t objset) {
	if (hashmap_contains(&spa->lethe_object_erlstore, objset)) {
		hashmap_remove(&spa->lethe_object_erlstore, objset);
		return B_TRUE;
	}
	return B_FALSE;
}

boolean_t __lethe_remove_object_erl(
	spa_t *spa,
	uint64_t objset,
	uint64_t object
) {
	// If the object's ERL store doesn't exist, then the object ERL doesn't exist.
	if (!__lethe_contains_object_erlstore(spa, objset)) {
		return B_FALSE;
	}

	// Get the object's ERL store.
	struct BTreeMap *erlstore = __lethe_get_object_erlstore(spa, objset);

	// Remove object's ERL from the object's ERL store if it exists.
	if (btreemap_contains(erlstore, object)) {
		btreemap_remove(erlstore, object);

		// Create master ERL for the object's object set if it doesn't exist.
		if (!__lethe_contains_master_erl(spa, objset)) {
			__lethe_insert_master_erl(
				spa,
				objset,
				erl_new(DEFAULT_FANOUTS, DEFAULT_FANOUTS_LEN)
			);
		}

		// Mark object as removed (modified) under its master ERL.
		struct Erl *master_erl = __lethe_get_master_erl(spa, objset);
		erl_mark_block(master_erl, object);

		return B_TRUE;
	}

	return B_FALSE;
}

boolean_t __lethe_object_erlmap_contains(
	spa_t *spa,
	uint64_t objset,
	uint64_t object
) {
	struct Str name = __lethe_object_erl_name(objset, object);
	// lethe_info("__lethe_object_erlmap_contains(): %s\n", str_buf(&name));
	boolean_t res = __lethe_erlmap_contains(spa->lethe_object_erlmap, str_buf(&name));
	str_drop(&name);
	return res;
}

boolean_t __lethe_master_erlmap_contains(spa_t *spa, uint64_t objset) {
	struct Str name = __lethe_master_erl_name(objset);
	boolean_t res = __lethe_erlmap_contains(spa->lethe_master_erlmap, str_buf(&name));
	str_drop(&name);
	return res;
}

boolean_t __lethe_erlmap_contains(nvlist_t *nvp, const char *name) {
	return nvlist_exists(nvp, name);
}

boolean_t __lethe_object_erlmap_get(
	spa_t *spa,
	uint64_t objset,
	uint64_t object,
	uint64_t *erlobject
) {
	struct Str name = __lethe_object_erl_name(objset, object);
	boolean_t res = __lethe_erlmap_get(
		spa->lethe_object_erlmap,
		str_buf(&name),
		erlobject
	);
	str_drop(&name);
	return res;
}

boolean_t __lethe_master_erlmap_get(
	spa_t *spa,
	uint64_t objset,
	uint64_t *erlobject
) {
	struct Str name = __lethe_master_erl_name(objset);
	boolean_t res = __lethe_erlmap_get(
		spa->lethe_master_erlmap,
		str_buf(&name),
		erlobject
	);
	str_drop(&name);
	return res;
}

boolean_t __lethe_erlmap_get(
	nvlist_t *nvp,
	const char *name,
	uint64_t *erlobject
) {
	return nvlist_lookup_uint64(nvp, name, erlobject) == 0;
}

boolean_t __lethe_object_erlmap_insert(
	spa_t *spa,
	uint64_t objset,
	uint64_t object,
	uint64_t erlobject
) {
	struct Str name = __lethe_object_erl_name(objset, object);
	boolean_t res = __lethe_erlmap_insert(
		spa->lethe_object_erlmap,
		str_buf(&name),
		erlobject
	);
	str_drop(&name);
	return res;
}

boolean_t __lethe_master_erlmap_insert(
	spa_t *spa,
	uint64_t objset,
	uint64_t erlobject
) {
	struct Str name = __lethe_master_erl_name(objset);
	boolean_t res = __lethe_erlmap_insert(
		spa->lethe_master_erlmap,
		str_buf(&name),
		erlobject
	);
	str_drop(&name);
	return res;
}

boolean_t __lethe_erlmap_insert(
	nvlist_t *nvp,
	const char *name,
	uint64_t erlobject
) {
	if (__lethe_erlmap_contains(nvp, name)) {
		return B_FALSE;
	}
	return nvlist_add_uint64(nvp, name, erlobject) == 0;
}

boolean_t __lethe_object_erlmap_remove(
	spa_t *spa,
	uint64_t objset,
	uint64_t object
) {
	struct Str name = __lethe_object_erl_name(objset, object);
	boolean_t res = __lethe_erlmap_remove(
		spa->lethe_object_erlmap,
		str_buf(&name)
	);
	str_drop(&name);
	return res;
}

boolean_t __lethe_master_erlmap_remove(
	spa_t *spa,
	uint64_t objset
) {
	struct Str name = __lethe_master_erl_name(objset);
	boolean_t res = __lethe_erlmap_remove(
		spa->lethe_object_erlmap,
		str_buf(&name)
	);
	str_drop(&name);
	return res;
}

boolean_t __lethe_erlmap_remove(nvlist_t *nvp, const char *name) {
	return nvlist_remove_all(nvp, name) == 0;
}

struct KhtKey lethe_block_read_key(
	spa_t *spa,
	uint64_t objset,
	uint64_t object,
	uint64_t block
) {
    lethe_rw_enter(&spa->lethe_master_erlstore_lock, RW_WRITER);
	lethe_rw_enter(&spa->lethe_object_erlstore_lock, RW_WRITER);
    lethe_rw_enter(&spa->lethe_master_erlmap_lock, RW_WRITER);
	lethe_rw_enter(&spa->lethe_object_erlmap_lock, RW_WRITER);
	lethe_rw_enter(&spa->lethe_uber_erl_lock, RW_WRITER);

	struct KhtKey key = __lethe_block_key(
		spa,
		B_TRUE,
		objset,
		object,
		block
	);

    lethe_rw_exit(&spa->lethe_master_erlstore_lock);
	lethe_rw_exit(&spa->lethe_object_erlstore_lock);
    lethe_rw_exit(&spa->lethe_master_erlmap_lock);
	lethe_rw_exit(&spa->lethe_object_erlmap_lock);
	lethe_rw_exit(&spa->lethe_uber_erl_lock);

	return key;
}

struct KhtKey lethe_block_write_key(
	spa_t *spa,
	uint64_t objset,
	uint64_t object,
	uint64_t block
) {
    lethe_rw_enter(&spa->lethe_master_erlstore_lock, RW_WRITER);
	lethe_rw_enter(&spa->lethe_object_erlstore_lock, RW_WRITER);
    lethe_rw_enter(&spa->lethe_master_erlmap_lock, RW_WRITER);
	lethe_rw_enter(&spa->lethe_object_erlmap_lock, RW_WRITER);
	lethe_rw_enter(&spa->lethe_uber_erl_lock, RW_WRITER);

	lethe_info("(start)\n");
	struct KhtKey key = __lethe_block_key(
		spa,
		B_FALSE,
		objset,
		object,
		block
	);
	lethe_info("(end)\n");

    lethe_rw_exit(&spa->lethe_master_erlstore_lock);
	lethe_rw_exit(&spa->lethe_object_erlstore_lock);
    lethe_rw_exit(&spa->lethe_master_erlmap_lock);
	lethe_rw_exit(&spa->lethe_object_erlmap_lock);
	lethe_rw_exit(&spa->lethe_uber_erl_lock);

	return key;
}

struct KhtKey lethe_bookmark_key(
	spa_t *spa,
	boolean_t read,
	const zbookmark_phys_t *bookmark
) {
    lethe_info("current thread start: (%p)\n", (void *)current);
    // TODO: this function is the cause of the deadlock.
    // For approximate results, we just create an ERL and derive a block key
    // under it (should be around the same time as correct functionality).
    // struct Erl erl = erl_new(DEFAULT_FANOUTS, DEFAULT_FANOUTS_LEN);
    // struct KhtKey key = erl_block_write_key(&erl, bookmark->zb_blkid);
    // erl_drop(&erl);
    // spa->lethe_epoch_dirty = B_TRUE;
    // return key;

    lethe_rw_enter(&spa->lethe_master_erlstore_lock, RW_WRITER);
	lethe_rw_enter(&spa->lethe_object_erlstore_lock, RW_WRITER);
    lethe_rw_enter(&spa->lethe_master_erlmap_lock, RW_WRITER);
	lethe_rw_enter(&spa->lethe_object_erlmap_lock, RW_WRITER);
	lethe_rw_enter(&spa->lethe_uber_erl_lock, RW_WRITER);

	struct KhtKey key = __lethe_block_key(
		spa,
		read,
		bookmark->zb_objset,
		bookmark->zb_object,
		bookmark->zb_blkid
	);

    lethe_rw_exit(&spa->lethe_master_erlstore_lock);
	lethe_rw_exit(&spa->lethe_object_erlstore_lock);
    lethe_rw_exit(&spa->lethe_master_erlmap_lock);
	lethe_rw_exit(&spa->lethe_object_erlmap_lock);
	lethe_rw_exit(&spa->lethe_uber_erl_lock);

    lethe_info("current thread end: (%p)\n", (void *)current);
	return key;
}

struct KhtKey __lethe_block_key(
	spa_t *spa,
	boolean_t read,
	uint64_t objset,
	uint64_t object,
	uint64_t block
) {
	// If a mapping to the object ERL supplying the key exists, load it.
	if (__lethe_object_erlmap_contains(spa, objset, object)) {
		__lethe_load_object_erl(spa, objset, object);
	}

	// If the object's ERL doesn't exist, create it.
	if (!__lethe_contains_object_erl(spa, objset, object)) {
		__lethe_insert_object_erl(
			spa,
			objset,
			object,
			erl_new(DEFAULT_FANOUTS, DEFAULT_FANOUTS_LEN)
		);
	}

	// If a mapping to the object's master ERL exists, load it.
	if (__lethe_master_erlmap_contains(spa, objset)) {
		__lethe_load_master_erl(spa, objset);
	}

	// If the object's master ERL doesn't exist, create it.
	if (!__lethe_contains_master_erl(spa, objset)) {
		__lethe_insert_master_erl(
			spa,
			objset,
			erl_new(DEFAULT_FANOUTS, DEFAULT_FANOUTS_LEN)
		);
	}

	// Get the object's ERL and its master ERL.
	struct Erl *erl = __lethe_get_object_erl(spa, objset, object);
	struct Erl *master_erl = __lethe_get_master_erl(spa, objset);

	// Mark the object as modified and the object ERL store as dirty.
	erl_mark_block(master_erl, object);
	spa->lethe_epoch_dirty = B_TRUE;

	// Generate appropriate key.
	struct KhtKey key = read ? erl_block_read_key(erl, block)
	                         : erl_block_write_key(erl, block);

	// Sanity logging.
	// struct Str s = khtkey_to_string(&key);
	// lethe_info(
	// 	"lethe_block_%s_key(): {\n"
	// 	"    %.*s\n"
	// 	"}\n",
	// 	read ? "read" : "write",
	// 	(int)str_len(&s),
	// 	str_buf(&s)
	// );
	// str_drop(&s);

	return key;
}

struct KhtKey __lethe_object_erl_read_key(
	spa_t *spa,
	uint64_t objset,
	uint64_t object
) {
	return __lethe_object_erl_key(spa, B_TRUE, objset, object);
}

struct KhtKey __lethe_object_erl_write_key(
	spa_t *spa,
	uint64_t objset,
	uint64_t object
) {
	return __lethe_object_erl_key(spa, B_FALSE, objset, object);
}

struct KhtKey __lethe_object_erl_key(
	spa_t *spa,
	boolean_t read,
	uint64_t objset,
	uint64_t object
) {
	if (!__lethe_contains_master_erl(spa, objset)) {
		__lethe_load_master_erl(spa, objset);
	}
	struct Erl *master_erl = __lethe_get_master_erl(spa, objset);

	struct KhtKey key = read ? erl_block_read_key(master_erl, object)
	                         : erl_block_write_key(master_erl, object);

	return key;
}

struct KhtKey __lethe_master_erl_read_key(spa_t *spa, uint64_t objset) {
	return __lethe_master_erl_key(spa, B_TRUE, objset);
}

struct KhtKey __lethe_master_erl_write_key(spa_t *spa, uint64_t objset) {
	return __lethe_master_erl_key(spa, B_FALSE, objset);
}

struct KhtKey __lethe_master_erl_key(
	spa_t *spa,
	boolean_t read,
	uint64_t objset
) {
	return read ? erl_block_read_key(&spa->lethe_uber_erl, objset)
	            : erl_block_write_key(&spa->lethe_uber_erl, objset);
}

void lethe_hijack_dsl_crypto_key(dsl_crypto_key_t *dck, struct KhtKey *key) {
	memcpy(&dck->dck_key.zk_current_keydata, &key->bytes, KHT_KEY_SIZE);
}

uint64_t __lethe_alloc_object(
	spa_t *spa,
	dmu_tx_t *tx,
	const char *name,
	dmu_object_type_t object_type,
	dmu_object_type_t bonus_type
) {
	// lethe_info("__lethe_alloc_object(): %s\n", name);

	// We shouldn't "allocate" anything if the root object isn't allocated.
	VERIFY(spa->lethe_root_object != 0);

	// Allocate new object, using maximum block size.
	uint64_t object = dmu_object_alloc(
		spa->spa_meta_objset,
		object_type,
		spa_maxblocksize(spa),
		bonus_type,
		sizeof(uint64_t),
		tx
	);

	// Add to Lethe's root object in the MOS.
	VERIFY(0 == zap_update(
		spa->spa_meta_objset,
		spa->lethe_root_object,
		name,
		sizeof(uint64_t),
		1,
		&object,
		tx
	));

	// lethe_info("__lethe_alloc_object(): %s -> %" PRIu64 "\n", name, object);
	return object;
}

uint64_t __lethe_alloc_root_object(spa_t *spa, dmu_tx_t *tx) {
	// Allocate root object as a ZAP object.
	uint64_t root_object = zap_create(
		spa->spa_meta_objset,
		DMU_OTN_ZAP_METADATA,
		DMU_OT_NONE,
		0,
		tx
	);

	// Add the allocated root object to the pool directory object.
	zap_update(
		spa->spa_meta_objset,
		DMU_POOL_DIRECTORY_OBJECT,
		LETHE_ROOT_OBJECT,
		sizeof(uint64_t),
		1,
		&root_object,
		tx
	);

	return root_object;
}
