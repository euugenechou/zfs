#ifdef __KERNEL__
    #include <lethe/btreeset_iter.h>
#else
    #include <lethe/btreeset_iter.h>
#endif

struct BTreeSetIter btreesetiter_new(struct BTreeSetNode *root) {
    struct BTreeSetIter self = {
        .nodes = vec_new(),
        .indices = vec_new(),
    };

    if (!btreesetnode_is_empty(root)) {
        while (!btreesetnode_is_leaf(root)) {
            vec_push(&self.nodes, root);
            vec_push(&self.indices, 0);
            root = &root->children[0];
        }
        vec_push(&self.nodes, root);
        vec_push(&self.indices, 0);
    }

    return self;
}

bool btreesetiter_next(struct BTreeSetIter *self, BTREESET_KEY_TYPE *key) {
    if (vec_is_empty(&self->nodes)) {
        return false;
    }

    struct BTreeSetNode *n = self->nodes[vec_len(&self->nodes) - 1];
    uint64_t index = self->indices[vec_len(&self->indices) - 1];

    *key = n->keys[index];
    index += 1;
    self->indices[vec_len(&self->indices) - 1] = index;

    if (index == btreesetnode_len(n)) {
        vec_truncate(&self->nodes, vec_len(&self->nodes) - 1);
        vec_truncate(&self->indices, vec_len(&self->indices) - 1);
    }

    if (index < vec_len(&n->children)) {
        n = &n->children[index];
        while (!btreesetnode_is_leaf(n)) {
            vec_push(&self->nodes, n);
            vec_push(&self->indices, 0);
            n = &n->children[0];
        }
        vec_push(&self->nodes, n);
        vec_push(&self->indices, 0);
    }

    return true;
}

void btreesetiter_drop(struct BTreeSetIter *self) {
    vec_drop(&self->nodes);
    vec_drop(&self->indices);
}
