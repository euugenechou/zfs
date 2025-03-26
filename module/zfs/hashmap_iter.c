#ifdef __KERNEL__
    #include <lethe/hashmap.h>
    #include <lethe/hashmap_list_iter.h>
    #include <linux/vmalloc.h>
#else
    #include <lethe/hashmap.h>
    #include <lethe/hashmap_list_iter.h>
    #include <stdlib.h>
#endif

struct HashMapIter hashmapiter_new(struct HashMap *map) {
    uint64_t bucket = 0;
    uint64_t curr = 0;
    uint64_t end = hashmap_len(map);

#ifdef __KERNEL__
    struct HashMapListIter *iter = vmalloc(sizeof(struct HashMapListIter));
#else
    struct HashMapListIter *iter = malloc(sizeof(struct HashMapListIter));
#endif

    *iter = hashmaplistiter_new(&map->lists[bucket]);

    struct HashMapIter self = {
	.curr = curr,
	.end = end,
	.bucket = bucket,
	.map = map,
	.iter = iter,
    };

    return self;
}

bool hashmapiter_next(
	struct HashMapIter *self,
	HASHMAP_KEY_TYPE *key,
	HASHMAP_VAL_TYPE **val
) {
    if (self->curr == self->end) {
        return false;
    }

    while (!hashmaplistiter_next(self->iter, key, val)) {
        self->bucket += 1;
        *self->iter = hashmaplistiter_new(&self->map->lists[self->bucket]);
    }

    self->curr += 1;

    return true;
}

void hashmapiter_drop(struct HashMapIter *self) {
    hashmaplistiter_drop(self->iter);
#ifdef __KERNEL__
    vfree(self->iter);
#else
    free(self->iter);
#endif
}
