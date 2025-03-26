#pragma once

#ifdef __KERNEL__
    #include <lethe/kht.h>
    #include <lethe/vec.h>
    #include <linux/types.h>
#else
    #include <lethe/kht.h>
    #include <lethe/vec.h>
    #include <stdbool.h>
    #include <stddef.h>
    #include <stdint.h>
#endif

#define BTREEMAP_KEY_TYPE uint64_t
#define BTREEMAP_VAL_TYPE struct Erl

struct BTreeMapNode {
    vec(BTREEMAP_KEY_TYPE) keys;
    vec(BTREEMAP_VAL_TYPE) vals;
    vec(struct BTreeMapNode) children;
};

struct BTreeMapNode btreemapnode_new(void);

void btreemapnode_drop(struct BTreeMapNode *self);

void btreemapnode_print(struct BTreeMapNode *self);

uint64_t btreemapnode_len(struct BTreeMapNode *self);

bool btreemapnode_is_empty(struct BTreeMapNode *self);

bool btreemapnode_is_full(struct BTreeMapNode *self, uint64_t degree);

bool btreemapnode_is_leaf(struct BTreeMapNode *self);

struct BTreeMapNode *btreemapnode_find(struct BTreeMapNode *self, BTREEMAP_KEY_TYPE key, uint64_t *index);

void btreemapnode_split_child(struct BTreeMapNode *self, uint64_t index, uint64_t degree);

void btreemapnode_insert_nonfull(
    struct BTreeMapNode *self,
    BTREEMAP_KEY_TYPE key,
    BTREEMAP_VAL_TYPE val,
    uint64_t degree
);

BTREEMAP_KEY_TYPE btreemapnode_min_key(struct BTreeMapNode *self);

BTREEMAP_VAL_TYPE btreemapnode_min_val(struct BTreeMapNode *self);

BTREEMAP_KEY_TYPE btreemapnode_max_key(struct BTreeMapNode *self);

BTREEMAP_VAL_TYPE btreemapnode_max_val(struct BTreeMapNode *self);

void btreemapnode_delete(struct BTreeMapNode *self, BTREEMAP_KEY_TYPE key, uint64_t degree);
