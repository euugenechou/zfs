#pragma once

#ifdef __KERNEL__
    #include <lethe/btreemap_iter.h>
    #include <lethe/btreemap_node.h>
    #include <lethe/str.h>
    #include <lethe/vec.h>
    #include <linux/types.h>
#else
    #include <lethe/btreemap_iter.h>
    #include <lethe/btreemap_node.h>
    #include <lethe/str.h>
    #include <lethe/vec.h>
    #include <stddef.h>
#endif

struct BTreeMap {
    uint64_t len;
    uint64_t degree;
    struct BTreeMapNode root;
};

struct BTreeMap btreemap_new(void);

struct BTreeMap btreemap_with_degree(uint64_t degree);

void btreemap_drop(struct BTreeMap *self);

void btreemap_print(struct BTreeMap *self);

struct Str btreemap_to_string(struct BTreeMap *self);

void btreemap_tree_print(struct BTreeMap *self);

uint64_t btreemap_len(struct BTreeMap *self);

uint64_t btreemap_degree(struct BTreeMap *self);

bool btreemap_is_empty(struct BTreeMap *self);

bool btreemap_contains(struct BTreeMap *self, BTREEMAP_KEY_TYPE key);

BTREEMAP_VAL_TYPE *btreemap_get(struct BTreeMap *self, BTREEMAP_KEY_TYPE key);

void btreemap_insert(struct BTreeMap *self, BTREEMAP_KEY_TYPE key, BTREEMAP_VAL_TYPE val);

void btreemap_remove(struct BTreeMap *self, BTREEMAP_KEY_TYPE key);

void btreemap_clear(struct BTreeMap *self);

struct BTreeMapIter btreemap_iter(struct BTreeMap *self);

vec(uint8_t) btreemap_serialize(struct BTreeMap *self);

struct BTreeMap btreemap_deserialize(vec(uint8_t) *bytes);
