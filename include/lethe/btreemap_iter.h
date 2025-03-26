#pragma once

#ifdef __KERNEL__
    #include <lethe/btreemap_node.h>
    #include <lethe/vec.h>
    #include <linux/types.h>
#else
    #include <lethe/btreemap_node.h>
    #include <lethe/vec.h>
    #include <stdbool.h>
    #include <stddef.h>
#endif

struct BTreeMapIter {
    vec(struct BTreeMapNode *) nodes;
    vec(uint64_t) indices;
};

struct BTreeMapIter btreemapiter_new(struct BTreeMapNode *root);

bool btreemapiter_next(struct BTreeMapIter *self, BTREEMAP_KEY_TYPE *key, BTREEMAP_VAL_TYPE **val);

void btreemapiter_drop(struct BTreeMapIter *self);
