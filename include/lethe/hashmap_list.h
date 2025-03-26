#pragma once

#include "hashmap_node.h"

#ifdef __KERNEL__
    #include <linux/types.h>
#else
    #include <stdbool.h>
    #include <stddef.h>
#endif

struct HashMapList {
    struct HashMapNode *head;
    struct HashMapNode *tail;
    uint64_t len;
};

struct HashMapList hashmaplist_new(void);

void hashmaplist_drop(struct HashMapList *self);

bool hashmaplist_is_empty(struct HashMapList *self);

struct HashMapNode *hashmaplist_find(struct HashMapList *self, HASHMAP_KEY_TYPE key);

struct HashMapNode *
hashmaplist_insert(struct HashMapList *self, HASHMAP_KEY_TYPE key, HASHMAP_VAL_TYPE val);

struct HashMapNode *hashmaplist_remove(struct HashMapList *self, HASHMAP_KEY_TYPE key);

void hashmaplist_print(struct HashMapList *self);
