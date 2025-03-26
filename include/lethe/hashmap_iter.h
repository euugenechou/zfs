#pragma once

#ifdef __KERNEL__
    #include <lethe/hashmap_node.h>
    #include <linux/types.h>
#else
    #include <lethe/hashmap_node.h>
    #include <stdbool.h>
    #include <stdint.h>
#endif

struct HashMapIter {
    uint64_t bucket;
    uint64_t curr;
    uint64_t end;
    struct HashMap *map;
    struct HashMapListIter *iter;
};

struct HashMapIter hashmapiter_new(struct HashMap *map);

bool hashmapiter_next(struct HashMapIter *self, HASHMAP_KEY_TYPE *key, HASHMAP_VAL_TYPE **val);

void hashmapiter_drop(struct HashMapIter *self);
