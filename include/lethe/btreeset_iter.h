#pragma once

#ifdef __KERNEL__
    #include <lethe/btreeset_node.h>
    #include <lethe/vec.h>
    #include <linux/types.h>
#else
    #include <lethe/btreeset_node.h>
    #include <lethe/vec.h>
    #include <stdbool.h>
    #include <stddef.h>
#endif

struct BTreeSetIter {
    vec(struct BTreeSetNode *) nodes;
    vec(uint64_t) indices;
};

struct BTreeSetIter btreesetiter_new(struct BTreeSetNode *root);

bool btreesetiter_next(struct BTreeSetIter *self, BTREESET_KEY_TYPE *key);

void btreesetiter_drop(struct BTreeSetIter *self);
