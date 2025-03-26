#pragma once

#ifdef __KERNEL__
    #include <lethe/vec.h>
    #include <linux/types.h>
#else
    #include <lethe/vec.h>
    #include <stdbool.h>
    #include <stddef.h>
    #include <stdint.h>
#endif

#define BTREESET_KEY_TYPE uint64_t

struct BTreeSetNode {
    vec(BTREESET_KEY_TYPE) keys;
    vec(struct BTreeSetNode) children;
};

struct BTreeSetNode btreesetnode_new(void);

void btreesetnode_drop(struct BTreeSetNode *self);

void btreesetnode_print(struct BTreeSetNode *self);

uint64_t btreesetnode_len(struct BTreeSetNode *self);

bool btreesetnode_is_empty(struct BTreeSetNode *self);

bool btreesetnode_is_full(struct BTreeSetNode *self, uint64_t degree);

bool btreesetnode_is_leaf(struct BTreeSetNode *self);

struct BTreeSetNode *btreesetnode_find(struct BTreeSetNode *self, BTREESET_KEY_TYPE key, uint64_t *index);

void btreesetnode_split_child(struct BTreeSetNode *self, uint64_t index, uint64_t degree);

void btreesetnode_insert_nonfull(struct BTreeSetNode *self, BTREESET_KEY_TYPE key, uint64_t degree);

BTREESET_KEY_TYPE btreesetnode_min_key(struct BTreeSetNode *self);

BTREESET_KEY_TYPE btreesetnode_max_key(struct BTreeSetNode *self);

void btreesetnode_delete(struct BTreeSetNode *self, BTREESET_KEY_TYPE key, uint64_t degree);
