#pragma once

#ifdef __KERNEL__
    #include <lethe/hashmap_list.h>
    #include <linux/types.h>
#else
    #include <lethe/hashmap_list.h>
    #include <stdbool.h>
    #include <stddef.h>
    #include <stdint.h>
#endif

struct HashMapListIter {
    struct HashMapNode *curr;
    struct HashMapNode *end;
};

struct HashMapListIter hashmaplistiter_new(struct HashMapList *list);

bool hashmaplistiter_next(
	struct HashMapListIter *self,
	HASHMAP_KEY_TYPE *key,
	HASHMAP_VAL_TYPE **val
);

void hashmaplistiter_drop(struct HashMapListIter *self);
