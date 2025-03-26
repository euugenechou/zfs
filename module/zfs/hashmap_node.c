#include <lethe/hashmap_node.h>

#ifdef __KERNEL__
    #include <linux/printk.h>
#else
    #include <inttypes.h>
    #include <stddef.h>
    #include <stdio.h>
#endif

struct HashMapNode hashmapnode_new(HASHMAP_KEY_TYPE key, HASHMAP_VAL_TYPE val) {
    struct HashMapNode self = {
        .key = key,
        .val = val,
        .next = NULL,
        .prev = NULL,
    };
    return self;
}

void hashmapnode_drop(struct HashMapNode *self) {
    btreemap_drop(&self->val);
}

void hashmapnode_print(struct HashMapNode *self) {
#ifdef __KERNEL__
    pr_info("HashMapNode { key: %llu }\n", self->key);
#else
    printf("HashMapNode { key: %" PRIu64 " }\n", self->key);
#endif
}
