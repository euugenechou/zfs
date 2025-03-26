#ifdef __KERNEL__
    #include <lethe/btreemap_iter.h>
#else
    #include <lethe/btreemap_iter.h>
#endif

struct BTreeMapIter btreemapiter_new(struct BTreeMapNode *root) {
    struct BTreeMapIter self = {
        .nodes = vec_new(),
        .indices = vec_new(),
    };

    if (!btreemapnode_is_empty(root)) {
        while (!btreemapnode_is_leaf(root)) {
            vec_push(&self.nodes, root);
            vec_push(&self.indices, 0);
            root = &root->children[0];
        }
        vec_push(&self.nodes, root);
        vec_push(&self.indices, 0);
    }

    return self;
}

bool btreemapiter_next(struct BTreeMapIter *self, BTREEMAP_KEY_TYPE *key, BTREEMAP_VAL_TYPE **val) {
    if (vec_is_empty(&self->nodes)) {
        return false;
    }

    struct BTreeMapNode *n = self->nodes[vec_len(&self->nodes) - 1];
    uint64_t index = self->indices[vec_len(&self->indices) - 1];

    *key = n->keys[index];
    *val = &n->vals[index];
    index += 1;
    self->indices[vec_len(&self->indices) - 1] = index;

    if (index == btreemapnode_len(n)) {
        vec_truncate(&self->nodes, vec_len(&self->nodes) - 1);
        vec_truncate(&self->indices, vec_len(&self->indices) - 1);
    }

    if (index < vec_len(&n->children)) {
        n = &n->children[index];
        while (!btreemapnode_is_leaf(n)) {
            vec_push(&self->nodes, n);
            vec_push(&self->indices, 0);
            n = &n->children[0];
        }
        vec_push(&self->nodes, n);
        vec_push(&self->indices, 0);
    }

    return true;
}

void btreemapiter_drop(struct BTreeMapIter *self) {
    vec_drop(&self->nodes);
    vec_drop(&self->indices);
}
