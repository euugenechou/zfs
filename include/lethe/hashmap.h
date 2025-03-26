#pragma once

#ifdef __KERNEL__
    #include <lethe/hashmap_iter.h>
    #include <lethe/hashmap_list.h>
    #include <linux/types.h>
#else
    #include <lethe/hashmap_iter.h>
    #include <lethe/hashmap_list.h>
    #include <stdbool.h>
    #include <stddef.h>
    #include <stdint.h>
#endif

struct HashMap {
    uint64_t len;
    uint64_t cap;
    struct HashMapList *lists;
};

struct HashMap hashmap_new(void);

struct HashMap hashmap_with_capacity(uint64_t cap);

void hashmap_drop(struct HashMap *self);

uint64_t hashmap_len(struct HashMap *self);

bool hashmap_is_empty(struct HashMap *self);

HASHMAP_VAL_TYPE *hashmap_get(struct HashMap *self, HASHMAP_KEY_TYPE key);

bool hashmap_contains(struct HashMap *self, HASHMAP_KEY_TYPE key);

bool hashmap_insert(struct HashMap *self, HASHMAP_KEY_TYPE key, HASHMAP_VAL_TYPE val);

bool hashmap_remove(struct HashMap *self, HASHMAP_KEY_TYPE key);

struct HashMapIter hashmap_iter(struct HashMap *self);

void hashmap_print(struct HashMap *self);
