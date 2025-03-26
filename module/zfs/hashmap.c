#include <lethe/hashmap.h>

#ifdef __KERNEL__
    #include <linux/printk.h>
    #include <linux/vmalloc.h>
#else
    #include <inttypes.h>
    #include <stdio.h>
    #include <stdlib.h>
#endif

#define DEFAULT_CAPACITY (1 << 18)

struct HashMap hashmap_new(void) {
    return hashmap_with_capacity(DEFAULT_CAPACITY);
}

struct HashMap hashmap_with_capacity(uint64_t cap) {
#ifdef __KERNEL__
    struct HashMap self = {
        .len = 0,
        .cap = cap,
        .lists = vmalloc(cap * sizeof(struct HashMapList)),
    };
#else
    struct HashMap self = {
        .len = 0,
        .cap = cap,
        .lists = malloc(cap * sizeof(struct HashMapList)),
    };
#endif

    for (uint64_t i = 0; i < cap; i += 1) {
        self.lists[i] = hashmaplist_new();
    }

    return self;
}

void hashmap_drop(struct HashMap *self) {
    for (uint64_t i = 0; i < self->cap; i += 1) {
        hashmaplist_drop(&self->lists[i]);
    }
#ifdef __KERNEL__
    vfree(self->lists);
#else
    free(self->lists);
#endif
    self->lists = NULL;
}

uint64_t hashmap_len(struct HashMap *self) {
    return self->len;
}

bool hashmap_is_empty(struct HashMap *self) {
    return self->len == 0;
}

bool hashmap_contains(struct HashMap *self, HASHMAP_KEY_TYPE key) {
    return hashmaplist_find(&self->lists[key % self->cap], key) != NULL;
}

HASHMAP_VAL_TYPE *hashmap_get(struct HashMap *self, HASHMAP_KEY_TYPE key) {
    struct HashMapNode *n = hashmaplist_find(&self->lists[key % self->cap], key);
    return n ? &n->val : NULL;
}

bool hashmap_insert(struct HashMap *self, HASHMAP_KEY_TYPE key, HASHMAP_VAL_TYPE val) {
    struct HashMapNode *n = hashmaplist_insert(&self->lists[key % self->cap], key, val);
    if (!n) {
        self->len += 1;
        return true;
    }
    return false;
}

bool hashmap_remove(struct HashMap *self, HASHMAP_KEY_TYPE key) {
    struct HashMapNode *n = hashmaplist_remove(&self->lists[key % self->cap], key);
    if (n) {
        hashmapnode_drop(n);
#ifdef __KERNEL__
        vfree(n);
#else
        free(n);
#endif
        self->len -= 1;
        return true;
    }
    return false;
}

struct HashMapIter hashmap_iter(struct HashMap *self) {
    return hashmapiter_new(self);
}

void hashmap_print(struct HashMap *self) {
#ifdef __KERNEL__
    if (hashmap_is_empty(self)) {
        pr_info("HashMap {}\n");
        return;
    }

    pr_info("HashMap {\n");
    for (uint64_t i = 0; i < self->cap; i += 1) {
        if (!hashmaplist_is_empty(&self->lists[i])) {
            pr_cont("    [%llu]: ", i);
            hashmaplist_print(&self->lists[i]);
            pr_cont(",\n");
        }
    }
    pr_cont("}\n");
#else
    if (hashmap_is_empty(self)) {
        printf("HashMap {}\n");
        return;
    }

    printf("HashMap {\n");
    for (uint64_t i = 0; i < self->cap; i += 1) {
        if (!hashmaplist_is_empty(&self->lists[i])) {
            printf("    [%" PRIu64 "]: ", i);
            hashmaplist_print(&self->lists[i]);
            printf(",\n");
        }
    }
    printf("}\n");
#endif
}
