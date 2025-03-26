#ifdef __KERNEL__
    #include <lethe/btreeset_node.h>
    #include <lethe/str.h>
    #include <lethe/vec.h>
    #include <linux/printk.h>
#else
    #include <lethe/btreeset_node.h>
    #include <lethe/str.h>
    #include <lethe/vec.h>
    #include <assert.h>
    #include <inttypes.h>
    #include <stdio.h>
#endif

struct BTreeSetNode btreesetnode_new(void) {
    struct BTreeSetNode self = {
        .keys = vec_new(),
        .children = vec_new(),
    };
    return self;
}

void btreesetnode_drop(struct BTreeSetNode *self) {
    vec_drop(&self->keys);

    for (uint64_t i = 0; i < vec_len(&self->children); i += 1) {
        btreesetnode_drop(&self->children[i]);
    }

    vec_drop(&self->children);
}

#ifndef __KERNEL__
static void btreesetnode_print_contents(struct BTreeSetNode *self) {
    printf("[");
    for (uint64_t i = 0; i < btreesetnode_len(self); i += 1) {
        printf("%" PRIu64, self->keys[i]);
        if (i + 1 != btreesetnode_len(self)) {
            printf(", ");
        }
    }
    printf("]\n");
}
#endif

static void btreesetnode_print_tree(struct BTreeSetNode *self, struct Str *s, bool last, bool root) {
    if (!root) {
#ifdef __KERNEL__
        pr_info("%.*s%s ", (int)str_len(s), str_buf(s), last ? "└───" : "├───");
#else
        printf("%.*s%s ", (int)str_len(s), str_buf(s), last ? "└───" : "├───");
#endif
    }

#ifdef __KERNEL__
    if (!root) {
        pr_cont("[");
    } else {
        pr_info("[");
    }
    for (uint64_t i = 0; i < btreesetnode_len(self); i += 1) {
        pr_cont("%llu", self->keys[i]);
        if (i + 1 != btreesetnode_len(self)) {
            pr_cont(", ");
        }
    }
    pr_cont("]");
#else
    btreesetnode_print_contents(self);
#endif

    if (!btreesetnode_is_leaf(self)) {
        for (uint64_t i = 0; i < vec_len(&self->children); i += 1) {
            struct Str t = str_clone(s);

            if (root) {
                str_push_raw(&t, "");
            } else if (last) {
                str_push_raw(&t, "     ");
            } else {
                str_push_raw(&t, "│    ");
            }

            btreesetnode_print_tree(&self->children[i], &t, i + 1 == vec_len(&self->children), false);

            str_drop(&t);
        }
    }
}

void btreesetnode_print(struct BTreeSetNode *self) {
    struct Str s = str_new();
    btreesetnode_print_tree(self, &s, true, true);
    str_drop(&s);
}

uint64_t btreesetnode_len(struct BTreeSetNode *self) {
    return vec_len(&self->keys);
}

bool btreesetnode_is_empty(struct BTreeSetNode *self) {
    return vec_is_empty(&self->keys);
}

bool btreesetnode_is_full(struct BTreeSetNode *self, uint64_t degree) {
    return vec_len(&self->keys) == 2 * degree - 1;
}

bool btreesetnode_is_leaf(struct BTreeSetNode *self) {
    return vec_len(&self->children) == 0;
}

static uint64_t btreesetnode_find_index(struct BTreeSetNode *self, BTREESET_KEY_TYPE key) {
    uint64_t size = btreesetnode_len(self);
    uint64_t left = 0;
    uint64_t right = size;

    while (left < right) {
        uint64_t mid = left + size / 2;

        if (self->keys[mid] == key) {
            return mid;
        } else if (self->keys[mid] < key) {
            left = mid + 1;
        } else {
            right = mid;
        }

        size = right - left;
    }

    return left;
}

struct BTreeSetNode *btreesetnode_find(struct BTreeSetNode *self, BTREESET_KEY_TYPE key, uint64_t *index) {
    for (;;) {
        *index = btreesetnode_find_index(self, key);
        if (*index < btreesetnode_len(self) && self->keys[*index] == key) {
            return self;
        } else if (btreesetnode_is_leaf(self)) {
            return NULL;
        } else {
            self = &self->children[*index];
        }
    }
}

void btreesetnode_split_child(struct BTreeSetNode *self, uint64_t index, uint64_t degree) {
#ifdef __KERNEL__
    BUG_ON(btreesetnode_is_full(self, degree));
    BUG_ON(!btreesetnode_is_full(&self->children[index], degree));
#else
    assert(!btreesetnode_is_full(self, degree));
    assert(btreesetnode_is_full(&self->children[index], degree));
#endif

    // We split the full child node into left and right nodes.
    struct BTreeSetNode *left = &self->children[index];
    struct BTreeSetNode right = btreesetnode_new();

    // Give the largest keys from the left to right.
    vec_drain(&right.keys, &left->keys, degree, vec_len(&left->keys));

    // Take the median (separator) key from the left.
    BTREESET_KEY_TYPE key = 0;
    vec_pop(&left->keys, &key);

    // Take the largest children as well if not a leaf.
    if (!btreesetnode_is_leaf(left)) {
        vec_drain(&right.children, &left->children, degree, vec_len(&left->children));
    }

    // Insert new key and child into the root.
    vec_insert(&self->keys, index, key);
    vec_insert(&self->children, index + 1, right);
}

void btreesetnode_insert_nonfull(struct BTreeSetNode *self, BTREESET_KEY_TYPE key, uint64_t degree) {
#ifdef __KERNEL__
    BUG_ON(btreesetnode_is_full(self, degree));
#else
    assert(!btreesetnode_is_full(self, degree));
#endif

    for (;;) {
        // Find index to insert key into or of child to recurse down.
        uint64_t i = btreesetnode_find_index(self, key);

        if (btreesetnode_is_leaf(self)) {
            // Insert key and stop.
            vec_insert(&self->keys, i, key);
            break;
        } else {
            // If child is full, split it and determine which to recurse down.
            if (btreesetnode_is_full(&self->children[i], degree)) {
                btreesetnode_split_child(self, i, degree);
                if (self->keys[i] < key) {
                    i += 1;
                }
            }

            // Continue with identified child.
            self = &self->children[i];
        }
    }
}

BTREESET_KEY_TYPE btreesetnode_min_key(struct BTreeSetNode *self) {
    while (!btreesetnode_is_leaf(self) && !btreesetnode_is_empty(&self->children[0])) {
        self = &self->children[0];
    }
    return self->keys[0];
}

BTREESET_KEY_TYPE btreesetnode_max_key(struct BTreeSetNode *self) {
    while (!btreesetnode_is_leaf(self) && !btreesetnode_is_empty(&self->children[0])) {
        self = &self->children[vec_len(&self->children) - 1];
    }
    return self->keys[vec_len(&self->keys) - 1];
}

void btreesetnode_delete(struct BTreeSetNode *self, BTREESET_KEY_TYPE key, uint64_t degree) {
    uint64_t i = btreesetnode_find_index(self, key);

    // Case 1: Key found in node and node is a leaf.
    if (i < btreesetnode_len(self) && self->keys[i] == key && btreesetnode_is_leaf(self)) {
        vec_remove(&self->keys, i, &key);
        return;
    }

    // Case 2: Key found in node and node is an internal node.
    if (i < btreesetnode_len(self) && self->keys[i] == key && !btreesetnode_is_leaf(self)) {
        struct BTreeSetNode *pred = &self->children[i];
        struct BTreeSetNode *succ = &self->children[i + 1];

        if (btreesetnode_len(pred) >= degree) {
            // Case 2a: Child node that precedes k has at least t keys.
            self->keys[i] = btreesetnode_max_key(pred);
            btreesetnode_delete(pred, self->keys[i], degree);
        } else if (btreesetnode_len(succ) >= degree) {
            // Case 2b: Child node that succeeds k has at least t keys.
            self->keys[i] = btreesetnode_min_key(succ);
            btreesetnode_delete(succ, self->keys[i], degree);
        } else {
            // Case 2c: Successor and predecessor only have t - 1 keys.
            struct BTreeSetNode succ;
            vec_remove(&self->keys, i, &key);
            vec_remove(&self->children, i + 1, &succ);

            // Merge keys into predecessor.
            vec_push(&pred->keys, key);
            vec_append(&pred->keys, &succ.keys);
#ifdef __KERNEL__
            BUG_ON(!btreesetnode_is_full(pred, degree));
#else
            assert(btreesetnode_is_full(pred, degree));
#endif

            // Merge any children into predecessor and drop successor.
            vec_append(&pred->children, &succ.children);
            btreesetnode_drop(&succ);
            btreesetnode_delete(pred, key, degree);
        }

        return;
    }

    // If on a leaf, then no appropriate subtree contains the key.
    if (btreesetnode_is_leaf(self)) {
        return;
    }

    // Case 3: Key not found in internal node.
    if (btreesetnode_len(&self->children[i]) == degree - 1) {
        struct BTreeSetNode *mid = &self->children[i];
        struct BTreeSetNode *left = (i > 0) ? &self->children[i - 1] : NULL;
        struct BTreeSetNode *right = (i + 1 < vec_len(&self->children)) ? &self->children[i + 1] : NULL;

        if (left && btreesetnode_len(left) >= degree) {
            // Case 3a: Immediate left sibling has at least t keys.

            // Move key from parent down to child.
            BTREESET_KEY_TYPE parent_key;
            vec_remove(&self->keys, i - 1, &parent_key);
            vec_insert(&mid->keys, 0, parent_key);

            // Move rightmost key in left sibling to parent.
            BTREESET_KEY_TYPE left_key;
            vec_pop(&left->keys, &left_key);
            vec_insert(&self->keys, i - 1, left_key);

            // Move rightmost child in left sibling to child.
            if (!btreesetnode_is_leaf(left)) {
                struct BTreeSetNode left_child = btreesetnode_new();
                vec_pop(&left->children, &left_child);
                vec_insert(&mid->children, 0, left_child);
            }
        } else if (right && btreesetnode_len(right) >= degree) {
            // Case 3a: Immediate right sibling has at least t keys.

            // Move key from parent down to child.
            BTREESET_KEY_TYPE parent_key;
            vec_remove(&self->keys, i, &parent_key);
            vec_push(&mid->keys, parent_key);

            // Move leftmost key in right sibling to parent.
            BTREESET_KEY_TYPE right_key;
            vec_remove(&right->keys, 0, &right_key);
            vec_insert(&self->keys, i, right_key);

            // Move leftmost child in right sibling to child.
            if (!btreesetnode_is_leaf(right)) {
                struct BTreeSetNode right_child = btreesetnode_new();
                vec_remove(&right->children, 0, &right_child);
                vec_push(&mid->children, right_child);
            }
        } else if (left) {
            // Case 3b: Merge into left sibling.

            // Move key from parent down to left sibling (merged node).
            BTREESET_KEY_TYPE parent_key;
            vec_remove(&self->keys, i - 1, &parent_key);
            vec_push(&left->keys, parent_key);

            // Merge all keys and children from child into left sibling.
            vec_append(&left->keys, &mid->keys);
            vec_append(&left->children, &mid->children);

            // Drop the merged child.
            struct BTreeSetNode mid = btreesetnode_new();
            vec_remove(&self->children, i, &mid);
            btreesetnode_drop(&mid);

            // The only instance where you fix the child to recurse down.
            i -= 1;
        } else if (right) {
            // Case 3b: Merge into right sibling.

            // Move key from parent down to right sibling (merged node).
            BTREESET_KEY_TYPE parent_key;
            vec_remove(&self->keys, i, &parent_key);
            vec_push(&mid->keys, parent_key);

            // Merge all keys and children from right sibling into child.
            vec_append(&mid->keys, &right->keys);
            vec_append(&mid->children, &right->children);

            // Drop the right sibling.
            struct BTreeSetNode right = btreesetnode_new();
            vec_remove(&self->children, i + 1, &right);
            btreesetnode_drop(&right);
        }
    }

    btreesetnode_delete(&self->children[i], key, degree);
}
