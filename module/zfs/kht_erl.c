#ifdef __KERNEL__
    #include <lethe/kht_erl.h>
    #include <lethe/speck.h>
    #include <lethe/log.h>
#else
    #include <lethe/kht_erl.h>
    #include <lethe/speck.h>
    #include <lethe/log.h>
#endif

struct Erl erl_new(uint64_t *fanouts, uint64_t n) {
    return erl_with_blocks_and_fanouts(0, fanouts, n);
}

struct Erl erl_with_blocks_and_fanouts(uint64_t blocks, uint64_t *fanouts, uint64_t n) {
    struct KhtKey tree_key = khtkey_new();
    struct KhtKey forest_key = khtkey_new();

    struct Erl self = {
        .blocks = blocks,
        .tree = kht_with_key(tree_key, fanouts, n),
        .forest = khf_with_key(forest_key, fanouts, n),
        .modified = btreeset_new(),
    };

    return self;
}

void erl_drop(struct Erl *self) {
    kht_drop(&self->tree);
    khf_drop(&self->forest);
    btreeset_drop(&self->modified);
}

struct Erl erl_debug_new(struct KhtKey key, uint64_t blocks, uint64_t *fanouts, uint64_t n) {
    struct Erl self = {
        .blocks = blocks,
        .tree = kht_with_key(key, fanouts, n),
        .forest = khf_with_key(key, fanouts, n),
        .modified = btreeset_new(),
    };
    return self;
}

bool erl_is_modified(struct Erl *self) {
    return self->blocks > 0;
}

bool erl_all_modified(struct Erl *self) {
    return self->blocks == btreeset_len(&self->modified);
}

#ifndef __KERNEL__
static inline uint64_t min(uint64_t x, uint64_t y) { return x < y ? x : y; }

static inline uint64_t max(uint64_t x, uint64_t y) { return x > y ? x : y; }
#endif

void erl_mark_block(struct Erl *self, uint64_t block) {
    self->blocks = max(self->blocks, block + 1);
    btreeset_insert(&self->modified, block);
}

struct KhtKey erl_block_write_key(struct Erl *self, uint64_t block) {
    erl_mark_block(self, block);
    return kht_leaf_key(&self->tree, block);
}

struct KhtKey erl_block_read_key(struct Erl *self, uint64_t block) {
    if (btreeset_contains(&self->modified, block)) {
        return erl_block_write_key(self, block);
    } else {
        return khf_leaf_key(&self->forest, block);
    }
}

void erl_overwrite(struct Erl *self, uint64_t start, uint64_t end) {
    self->blocks = max(self->blocks, end);
    khf_overwrite_keyed(&self->forest, start, end, self->tree.root.key);
    for (uint64_t i = start; i < end; i += 1) {
        btreeset_insert(&self->modified, i);
    }
}

void erl_truncate(struct Erl *self, uint64_t blocks) {
    for (uint64_t i = 0; i < min(self->blocks, blocks); i += 1) {
        self->blocks -= 1;
        btreeset_remove(&self->modified, self->blocks);
        khf_truncate(&self->forest, 1);
    }
}

void erl_consolidate(struct Erl *self) {
    khf_consolidate_keyed(&self->forest, self->tree.root.key);
}

void erl_reset(struct Erl *self) {
    self->tree.root = khtroot_new();
    btreeset_clear(&self->modified);
}

vec(struct KhtRange) erl_modified_ranges(struct Erl *self) {
    vec(struct KhtRange) ranges = vec_new();

    if (!btreeset_is_empty(&self->modified)) {
        bool first = true;
        uint64_t start = 0;
        uint64_t prev = 0;
        uint64_t blocks = 1;
        uint64_t block = 0;
        struct BTreeSetIter iter = btreeset_iter(&self->modified);

        while (btreesetiter_next(&iter, &block)) {
            if (first) {
                first = false;
                start = block;
            } else if (block == prev + 1) {
                blocks += 1;
            } else {
                vec_push(&ranges, khtrange_new(start, start + blocks));
                blocks = 1;
                start = block;
            }
            prev = block;
        }

        vec_push(&ranges, khtrange_new(start, start + blocks));
        btreesetiter_drop(&iter);
    }

    return ranges;
}

void erl_patch(struct Erl *self) {
    if (erl_all_modified(self)) {
        erl_consolidate(self);
    } else {
        vec(struct KhtRange) ranges = erl_modified_ranges(self);

        for (uint64_t i = 0; i < vec_len(&ranges); i += 1) {
            struct KhtRange range = ranges[i];
            erl_overwrite(self, range.start, range.end);
        }

        vec_drop(&ranges);
    }

    erl_reset(self);
}

vec(uint8_t) erl_serialize(struct Erl *self) {
    vec(uint8_t) bytes = vec_new();
    uint64_t blocks = self->blocks;

    for (uint64_t i = 0; i < sizeof(uint64_t); i += 1) {
        vec_push(&bytes, blocks & 0xFF);
        blocks >>= 8;
    }

    vec(uint8_t) tree_bytes = kht_serialize(&self->tree);
    vec_append(&bytes, &tree_bytes);
    vec_drop(&tree_bytes);

    vec(uint8_t) forest_bytes = khf_serialize(&self->forest);
    vec_append(&bytes, &forest_bytes);
    vec_drop(&forest_bytes);

    vec(uint8_t) modified_bytes = btreeset_serialize(&self->modified);
    vec_append(&bytes, &modified_bytes);
    vec_drop(&modified_bytes);

    return bytes;
}

vec(uint8_t) erl_serialize_keyed(struct Erl *self, struct KhtKey *key) {
    vec(uint8_t) bytes = erl_serialize(self);

    vec(uint8_t) encrypted = vec_new();
    vec_reserve(&encrypted, vec_len(&bytes));
    vec_set_len(&encrypted, vec_len(&bytes));

    speck_ctr_encrypt(encrypted, bytes, vec_len(&bytes), key->bytes, 0);

    vec_drop(&bytes);

    return encrypted;
}

struct Erl erl_deserialize(vec(uint8_t) * bytes) {
    struct Erl self;

    self.blocks = 0;
    for (uint64_t i = 0; i < sizeof(uint64_t); i += 1) {
        self.blocks |= (uint64_t)(*bytes)[i] << (i * 8);
    }
    vec_flush(bytes, 0, sizeof(uint64_t));

    self.tree = kht_deserialize(bytes);
    self.forest = khf_deserialize(bytes);
    self.modified = btreeset_deserialize(bytes);

    return self;
}

struct Erl erl_deserialize_keyed(vec(uint8_t) *bytes, struct KhtKey *key) {
    lethe_info("bytes = %zu\n", vec_len(bytes));

    lethe_info("allocating vec (start)\n");
    vec(uint8_t) decrypted = vec_new();
    vec_reserve(&decrypted, vec_len(bytes));
    vec_set_len(&decrypted, vec_len(bytes));
    lethe_info("allocating vec (end)\n");

    lethe_info("decrypting bytes (start)\n");
    speck_ctr_decrypt(decrypted, *bytes, vec_len(bytes), key->bytes, 0);
    lethe_info("decrypting bytes (end)\n");

    struct Erl self = erl_deserialize(&decrypted);

    vec_drop(&decrypted);

    return self;
}

void erl_print(struct Erl *self) {
    khf_print(&self->forest);
}

struct Str erl_to_string(struct Erl *self) {
    return khf_to_string(&self->forest);
}

#ifdef __KERNEL__
EXPORT_SYMBOL(erl_new);
EXPORT_SYMBOL(erl_with_blocks_and_fanouts);
EXPORT_SYMBOL(erl_debug_new);
EXPORT_SYMBOL(erl_drop);
EXPORT_SYMBOL(erl_is_modified);
EXPORT_SYMBOL(erl_all_modified);
EXPORT_SYMBOL(erl_mark_block);
EXPORT_SYMBOL(erl_block_write_key);
EXPORT_SYMBOL(erl_block_read_key);
EXPORT_SYMBOL(erl_overwrite);
EXPORT_SYMBOL(erl_truncate);
EXPORT_SYMBOL(erl_consolidate);
EXPORT_SYMBOL(erl_reset);
EXPORT_SYMBOL(erl_modified_ranges);
EXPORT_SYMBOL(erl_patch);
EXPORT_SYMBOL(erl_serialize);
EXPORT_SYMBOL(erl_serialize_keyed);
EXPORT_SYMBOL(erl_deserialize);
EXPORT_SYMBOL(erl_deserialize_keyed);
EXPORT_SYMBOL(erl_print);
EXPORT_SYMBOL(erl_to_string);
#endif
