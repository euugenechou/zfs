#pragma once

#include "btreemap.h"

#ifdef __KERNEL__
    #include <linux/types.h>
#else
    #include <stdint.h>
#endif

#define HASHMAP_KEY_TYPE uint64_t
#define HASHMAP_VAL_TYPE struct BTreeMap

struct HashMapNode {
    HASHMAP_KEY_TYPE key;
    HASHMAP_VAL_TYPE val;
    struct HashMapNode *next;
    struct HashMapNode *prev;
};

struct HashMapNode hashmapnode_new(HASHMAP_KEY_TYPE key, HASHMAP_VAL_TYPE val);

void hashmapnode_drop(struct HashMapNode *self);

void hashmapnode_print(struct HashMapNode *self);
