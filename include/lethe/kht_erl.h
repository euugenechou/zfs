#pragma once

#ifdef __KERNEL__
    #include <lethe/btreeset.h>
    #include <lethe/kht_forest.h>
    #include <lethe/kht_key.h>
    #include <lethe/kht_range.h>
    #include <lethe/kht_tree.h>
    #include <lethe/str.h>
    #include <lethe/vec.h>
    #include <linux/types.h>
#else
    #include <lethe/btreeset.h>
    #include <lethe/kht_forest.h>
    #include <lethe/kht_key.h>
    #include <lethe/kht_range.h>
    #include <lethe/kht_tree.h>
    #include <lethe/str.h>
    #include <lethe/vec.h>
    #include <stdbool.h>
    #include <stdint.h>
#endif

struct Erl {
    uint64_t blocks;
    struct Kht tree;
    struct Khf forest;
    struct BTreeSet modified;
};

struct Erl erl_new(uint64_t *fanouts, uint64_t n);

struct Erl erl_with_blocks_and_fanouts(uint64_t blocks, uint64_t *fanouts, uint64_t n);

struct Erl erl_debug_new(struct KhtKey key, uint64_t blocks, uint64_t *fanouts, uint64_t n);

void erl_drop(struct Erl *self);

bool erl_is_modified(struct Erl *self);

bool erl_all_modified(struct Erl *self);

void erl_mark_block(struct Erl *self, uint64_t block);

struct KhtKey erl_block_write_key(struct Erl *self, uint64_t block);

struct KhtKey erl_block_read_key(struct Erl *self, uint64_t block);

void erl_overwrite(struct Erl *self, uint64_t start, uint64_t end);

void erl_truncate(struct Erl *self, uint64_t blocks);

void erl_consolidate(struct Erl *self);

void erl_reset(struct Erl *self);

vec(struct KhtRange) erl_modified_ranges(struct Erl *self);

void erl_patch(struct Erl *self);

vec(uint8_t) erl_serialize(struct Erl *self);

vec(uint8_t) erl_serialize_keyed(struct Erl *self, struct KhtKey *key);

struct Erl erl_deserialize(vec(uint8_t) *bytes);

struct Erl erl_deserialize_keyed(vec(uint8_t) *bytes, struct KhtKey *key);

void erl_print(struct Erl *self);

struct Str erl_to_string(struct Erl *self);
