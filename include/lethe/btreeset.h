#pragma once

#ifdef __KERNEL__
    #include <lethe/btreeset_iter.h>
    #include <lethe/btreeset_node.h>
    #include <lethe/str.h>
    #include <lethe/vec.h>
    #include <linux/types.h>
#else
    #include <lethe/btreeset_iter.h>
    #include <lethe/btreeset_node.h>
    #include <lethe/str.h>
    #include <lethe/vec.h>
    #include <stdint.h>
#endif

struct BTreeSet {
    uint64_t len;
    uint64_t degree;
    struct BTreeSetNode root;
};

struct BTreeSet btreeset_new(void);

struct BTreeSet btreeset_with_degree(uint64_t degree);

void btreeset_drop(struct BTreeSet *self);

void btreeset_print(struct BTreeSet *self);

struct Str btreeset_to_string(struct BTreeSet *self);

void btreeset_tree_print(struct BTreeSet *self);

uint64_t btreeset_len(struct BTreeSet *self);

uint64_t btreeset_degree(struct BTreeSet *self);

bool btreeset_is_empty(struct BTreeSet *self);

bool btreeset_contains(struct BTreeSet *self, BTREESET_KEY_TYPE key);

void btreeset_insert(struct BTreeSet *self, BTREESET_KEY_TYPE key);

bool btreeset_first(struct BTreeSet *self, BTREESET_KEY_TYPE *key);

bool btreeset_last(struct BTreeSet *self, BTREESET_KEY_TYPE *key);

void btreeset_remove(struct BTreeSet *self, BTREESET_KEY_TYPE key);

void btreeset_clear(struct BTreeSet *self);

struct BTreeSetIter btreeset_iter(struct BTreeSet *self);

vec(uint8_t) btreeset_serialize(struct BTreeSet *self);

struct BTreeSet btreeset_deserialize(vec(uint8_t) *bytes);
