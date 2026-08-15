#ifdef __KERNEL__
    #include <lethe/btreemap_node.h>
    #include <lethe/str.h>
    #include <lethe/vec.h>
    #include <linux/printk.h>
#else
    #include <lethe/btreemap_node.h>
    #include <lethe/str.h>
    #include <lethe/vec.h>
    #include <assert.h>
    #include <inttypes.h>
    #include <stdio.h>
#endif

struct BTreeMapNode btreemapnode_new(void) {
    struct BTreeMapNode self = {
        .keys = vec_new(),
        .vals = vec_new(),
        .children = vec_new(),
    };
    return self;
}

void btreemapnode_drop(struct BTreeMapNode *self) {
    for (uint64_t i = 0; i < vec_len(&self->vals); i += 1) {
        erl_drop(&self->vals[i]);
    }

    for (uint64_t i = 0; i < vec_len(&self->children); i += 1) {
        btreemapnode_drop(&self->children[i]);
    }

    vec_drop(&self->keys);
    vec_drop(&self->vals);
    vec_drop(&self->children);
}

#ifndef __KERNEL__
static void btreemapnode_print_contents(struct BTreeMapNode *self) {
    printf("[");
    for (uint64_t i = 0; i < btreemapnode_len(self); i += 1) {
        printf("{ %" PRIu64 " }", self->keys[i]);
        if (i + 1 != btreemapnode_len(self)) {
            printf(", ");
        }
    }
    printf("]\n");
}
#endif

static void btreemapnode_print_tree(struct BTreeMapNode *self, struct Str *s, bool last, bool root) {
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
    for (uint64_t i = 0; i < btreemapnode_len(self); i += 1) {
        pr_cont("{ %llu }", self->keys[i]);
        if (i + 1 != btreemapnode_len(self)) {
            pr_cont(", ");
        }
    }
    pr_cont("]");
#else
    btreemapnode_print_contents(self);
#endif

    if (!btreemapnode_is_leaf(self)) {
        for (uint64_t i = 0; i < vec_len(&self->children); i += 1) {
            struct Str t = str_clone(s);

            if (root) {
                str_push_raw(&t, "");
            } else if (last) {
                str_push_raw(&t, "     ");
            } else {
                str_push_raw(&t, "│    ");
            }

            btreemapnode_print_tree(&self->children[i], &t, i + 1 == vec_len(&self->children), false);

            str_drop(&t);
        }
    }
}

void btreemapnode_print(struct BTreeMapNode *self) {
    struct Str s = str_new();
    btreemapnode_print_tree(self, &s, true, true);
    str_drop(&s);
}

uint64_t btreemapnode_len(struct BTreeMapNode *self) {
    return vec_len(&self->keys);
}

bool btreemapnode_is_empty(struct BTreeMapNode *self) {
    return vec_is_empty(&self->keys);
}

bool btreemapnode_is_full(struct BTreeMapNode *self, uint64_t degree) {
    return vec_len(&self->keys) == 2 * degree - 1;
}

bool btreemapnode_is_leaf(struct BTreeMapNode *self) {
    return vec_len(&self->children) == 0;
}

static uint64_t btreemapnode_find_index(struct BTreeMapNode *self, BTREEMAP_KEY_TYPE key) {
    uint64_t size = btreemapnode_len(self);
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

struct BTreeMapNode *btreemapnode_find(struct BTreeMapNode *self, BTREEMAP_KEY_TYPE key, uint64_t *index) {
    for (;;) {
        *index = btreemapnode_find_index(self, key);
        if (*index < btreemapnode_len(self) && self->keys[*index] == key) {
            return self;
        } else if (btreemapnode_is_leaf(self)) {
            return NULL;
        } else {
            self = &self->children[*index];
        }
    }
}

void btreemapnode_split_child(struct BTreeMapNode *self, uint64_t index, uint64_t degree) {
#ifdef __KERNEL__
    BUG_ON(btreemapnode_is_full(self, degree));
    BUG_ON(!btreemapnode_is_full(&self->children[index], degree));
#else
    assert(!btreemapnode_is_full(self, degree));
    assert(btreemapnode_is_full(&self->children[index], degree));
#endif

    // We split the full child node into left and right nodes.
    struct BTreeMapNode *left = &self->children[index];
    struct BTreeMapNode right = btreemapnode_new();

    // Give the largest keys and values from the left to right.
    vec_drain(&right.keys, &left->keys, degree, vec_len(&left->keys));
    vec_drain(&right.vals, &left->vals, degree, vec_len(&left->vals));

    // Take the median (separator) key and value from the left.
    BTREEMAP_KEY_TYPE key;
    BTREEMAP_VAL_TYPE val;
    vec_pop(&left->keys, &key);
    vec_pop(&left->vals, &val);

    // Take the largest children as well if not a leaf.
    if (!btreemapnode_is_leaf(left)) {
        vec_drain(&right.children, &left->children, degree, vec_len(&left->children));
    }

    // Insert new key, value, and child into the root.
    vec_insert(&self->keys, index, key);
    vec_insert(&self->vals, index, val);
    vec_insert(&self->children, index + 1, right);
}

void btreemapnode_insert_nonfull(
    struct BTreeMapNode *self,
    BTREEMAP_KEY_TYPE key,
    BTREEMAP_VAL_TYPE val,
    uint64_t degree
) {
#ifdef __KERNEL__
    BUG_ON(btreemapnode_is_full(self, degree));
#else
    assert(!btreemapnode_is_full(self, degree));
#endif

    for (;;) {
        // Find index to insert key into or of child to recurse down.
        uint64_t i = btreemapnode_find_index(self, key);

        if (btreemapnode_is_leaf(self)) {
            // Insert key and value then stop.
            vec_insert(&self->keys, i, key);
            vec_insert(&self->vals, i, val);
            break;
        } else {
            // If child is full, split it and determine which to recurse down.
            if (btreemapnode_is_full(&self->children[i], degree)) {
                btreemapnode_split_child(self, i, degree);
                if (self->keys[i] < key) {
                    i += 1;
                }
            }

            // Continue with identified child.
            self = &self->children[i];
        }
    }
}

BTREEMAP_KEY_TYPE btreemapnode_min_key(struct BTreeMapNode *self) {
    while (!btreemapnode_is_leaf(self) && !btreemapnode_is_empty(&self->children[0])) {
        self = &self->children[0];
    }
    return self->keys[0];
}

BTREEMAP_VAL_TYPE btreemapnode_min_val(struct BTreeMapNode *self) {
    while (!btreemapnode_is_leaf(self) && !btreemapnode_is_empty(&self->children[0])) {
        self = &self->children[0];
    }
    return self->vals[0];
}

BTREEMAP_KEY_TYPE btreemapnode_max_key(struct BTreeMapNode *self) {
    while (!btreemapnode_is_leaf(self) && !btreemapnode_is_empty(&self->children[0])) {
        self = &self->children[vec_len(&self->children) - 1];
    }
    return self->keys[vec_len(&self->keys) - 1];
}

BTREEMAP_VAL_TYPE btreemapnode_max_val(struct BTreeMapNode *self) {
    while (!btreemapnode_is_leaf(self) && !btreemapnode_is_empty(&self->children[0])) {
        self = &self->children[vec_len(&self->children) - 1];
    }
    return self->vals[vec_len(&self->vals) - 1];
}


// This function exceeds the frame size with --enable-debug.
#if defined(__GNUC__)
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wframe-larger-than="
#endif
// Removes `key` from the subtree rooted at `self`, moving its value out to
// `*out` instead of dropping it. Every path below performs a true,
// single-owner move of BTREEMAP_VAL_TYPE (an Erl, which owns heap state):
// either a vec_remove() straight into `*out`, or a recursive extract() that
// lands the value directly in its final resting slot. Nothing is ever
// bit-copied into two live locations at once, unlike the old
// btreemapnode_delete(), whose cases 2a/2b copied a predecessor/successor's
// value into self->vals[i] *and* then recursed into a delete() that
// erl_drop()'d that very same Erl out from under the copy (a
// use-after-free once the copy was later used or dropped), while also
// silently leaking the original self->vals[i] it overwrote without
// dropping.
bool btreemapnode_extract(struct BTreeMapNode *self, BTREEMAP_KEY_TYPE key, uint64_t degree, BTREEMAP_VAL_TYPE *out) {
    BTREEMAP_VAL_TYPE val;
    uint64_t i = btreemapnode_find_index(self, key);

    // Case 1: Key found in node and node is a leaf.
    if (i < btreemapnode_len(self) && self->keys[i] == key && btreemapnode_is_leaf(self)) {
        vec_remove(&self->keys, i, &key);
        vec_remove(&self->vals, i, out);
        return true;
    }

    // Case 2: Key found in node and node is an internal node.
    if (i < btreemapnode_len(self) && self->keys[i] == key && !btreemapnode_is_leaf(self)) {
        struct BTreeMapNode *pred = &self->children[i];
        struct BTreeMapNode *succ = &self->children[i + 1];

        if (btreemapnode_len(pred) >= degree) {
            // Case 2a: Child node that precedes k has at least t keys.
            // The entry being deleted moves out to *out; the promoted
            // predecessor value is extracted directly into its new home
            // (self->vals[i]), never existing in two places at once.
            *out = self->vals[i];
            self->keys[i] = btreemapnode_max_key(pred);
#ifdef __KERNEL__
            BUG_ON(!btreemapnode_extract(pred, self->keys[i], degree, &self->vals[i]));
#else
            {
                bool found = btreemapnode_extract(pred, self->keys[i], degree, &self->vals[i]);
                assert(found);
            }
#endif
        } else if (btreemapnode_len(succ) >= degree) {
            // Case 2b: Child node that succeeds k has at least t keys.
            *out = self->vals[i];
            self->keys[i] = btreemapnode_min_key(succ);
#ifdef __KERNEL__
            BUG_ON(!btreemapnode_extract(succ, self->keys[i], degree, &self->vals[i]));
#else
            {
                bool found = btreemapnode_extract(succ, self->keys[i], degree, &self->vals[i]);
                assert(found);
            }
#endif
        } else {
            // Case 2c: Successor and predecessor only have t - 1 keys.
            struct BTreeMapNode succ;
            vec_remove(&self->keys, i, &key);
            vec_remove(&self->vals, i, &val);
            vec_remove(&self->children, i + 1, &succ);

            // Merge keys and values into predecessor.
            vec_push(&pred->keys, key);
            vec_push(&pred->vals, val);
            vec_append(&pred->keys, &succ.keys);
            vec_append(&pred->vals, &succ.vals);
#ifdef __KERNEL__
            BUG_ON(!btreemapnode_is_full(pred, degree));
#else
            assert(btreemapnode_is_full(pred, degree));
#endif

            // Merge any children into predecessor and drop successor.
            vec_append(&pred->children, &succ.children);
            btreemapnode_drop(&succ);
            return btreemapnode_extract(pred, key, degree, out);
        }

        return true;
    }

    // If on a leaf, then no appropriate subtree contains the key.
    if (btreemapnode_is_leaf(self)) {
        return false;
    }

    // Case 3: Key not found in internal node.
    if (btreemapnode_len(&self->children[i]) == degree - 1) {
        struct BTreeMapNode *mid = &self->children[i];
        struct BTreeMapNode *left = (i > 0) ? &self->children[i - 1] : NULL;
        struct BTreeMapNode *right = (i + 1 < vec_len(&self->children)) ? &self->children[i + 1] : NULL;

        if (left && btreemapnode_len(left) >= degree) {
            // Case 3a: Immediate left sibling has at least t keys.

            // Move key and value from parent down to child.
            BTREEMAP_KEY_TYPE parent_key;
            BTREEMAP_VAL_TYPE parent_val;
            vec_remove(&self->keys, i - 1, &parent_key);
            vec_remove(&self->vals, i - 1, &parent_val);
            vec_insert(&mid->keys, 0, parent_key);
            vec_insert(&mid->vals, 0, parent_val);

            // Move rightmost key and value in left sibling to parent.
            BTREEMAP_KEY_TYPE left_key;
            BTREEMAP_VAL_TYPE left_val;
            vec_pop(&left->keys, &left_key);
            vec_pop(&left->vals, &left_val);
            vec_insert(&self->keys, i - 1, left_key);
            vec_insert(&self->vals, i - 1, left_val);

            // Move rightmost child in left sibling to child.
            if (!btreemapnode_is_leaf(left)) {
                struct BTreeMapNode left_child = btreemapnode_new();
                vec_pop(&left->children, &left_child);
                vec_insert(&mid->children, 0, left_child);
            }
        } else if (right && btreemapnode_len(right) >= degree) {
            // Case 3a: Immediate right sibling has at least t keys.

            // Move key and value from parent down to child.
            BTREEMAP_KEY_TYPE parent_key;
            BTREEMAP_VAL_TYPE parent_val;
            vec_remove(&self->keys, i, &parent_key);
            vec_remove(&self->vals, i, &parent_val);
            vec_push(&mid->keys, parent_key);
            vec_push(&mid->vals, parent_val);

            // Move leftmost key and value in right sibling to parent.
            BTREEMAP_KEY_TYPE right_key;
            BTREEMAP_VAL_TYPE right_val;
            vec_remove(&right->keys, 0, &right_key);
            vec_remove(&right->vals, 0, &right_val);
            vec_insert(&self->keys, i, right_key);
            vec_insert(&self->vals, i, right_val);

            // Move leftmost child in right sibling to child.
            if (!btreemapnode_is_leaf(right)) {
                struct BTreeMapNode right_child = btreemapnode_new();
                vec_remove(&right->children, 0, &right_child);
                vec_push(&mid->children, right_child);
            }
        } else if (left) {
            // Case 3b: Merge into left sibling.

            // Move key and value from parent down to left sibling (merged node).
            BTREEMAP_KEY_TYPE parent_key;
            BTREEMAP_VAL_TYPE parent_val;
            vec_remove(&self->keys, i - 1, &parent_key);
            vec_remove(&self->vals, i - 1, &parent_val);
            vec_push(&left->keys, parent_key);
            vec_push(&left->vals, parent_val);

            // Merge all keys, values, and children from child into left sibling.
            vec_append(&left->keys, &mid->keys);
            vec_append(&left->vals, &mid->vals);
            vec_append(&left->children, &mid->children);

            // Drop the merged child.
            struct BTreeMapNode mid = btreemapnode_new();
            vec_remove(&self->children, i, &mid);
            btreemapnode_drop(&mid);

            // The only instance where you fix the child to recurse down.
            i -= 1;
        } else if (right) {
            // Case 3b: Merge into right sibling.

            // Move key and value from parent down to right sibling (merged node).
            BTREEMAP_KEY_TYPE parent_key;
            BTREEMAP_VAL_TYPE parent_val;
            vec_remove(&self->keys, i, &parent_key);
            vec_remove(&self->vals, i, &parent_val);
            vec_push(&mid->keys, parent_key);
            vec_push(&mid->vals, parent_val);

            // Merge all keys, values, and children from right sibling into child.
            vec_append(&mid->keys, &right->keys);
            vec_append(&mid->vals, &right->vals);
            vec_append(&mid->children, &right->children);

            // Drop the right sibling.
            struct BTreeMapNode right = btreemapnode_new();
            vec_remove(&self->children, i + 1, &right);
            btreemapnode_drop(&right);
        }
    }

    return btreemapnode_extract(&self->children[i], key, degree, out);
}

void btreemapnode_delete(struct BTreeMapNode *self, BTREEMAP_KEY_TYPE key, uint64_t degree) {
    BTREEMAP_VAL_TYPE val;
    if (btreemapnode_extract(self, key, degree, &val)) {
        erl_drop(&val);
    }
}
#if defined(__GNUC__)
#pragma GCC diagnostic pop
#endif
