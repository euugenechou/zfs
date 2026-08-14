#ifdef __KERNEL__
    #include <lethe/btreeset.h>
    #include <linux/bug.h>
    #include <linux/printk.h>

    #ifndef PRIu64
        #define PRIu64 "llu"
    #endif
#else
    #include <lethe/btreeset.h>
    #include <assert.h>
    #include <inttypes.h>
    #include <stdio.h>
    #include <stdlib.h>
#endif

#define DEFAULT_DEGREE 2

struct BTreeSet btreeset_new(void) {
    return btreeset_with_degree(DEFAULT_DEGREE);
}

struct BTreeSet btreeset_with_degree(uint64_t degree) {
    struct BTreeSet self = {
        .len = 0,
        .degree = degree,
        .root = btreesetnode_new(),
    };
    return self;
}

void btreeset_drop(struct BTreeSet *self) {
    btreesetnode_drop(&self->root);
}

void btreeset_print(struct BTreeSet *self) {
    struct Str s = btreeset_to_string(self);

#ifdef __KERNEL__
    pr_info("%.*s\n", (int)str_len(&s), str_buf(&s));
#else
    printf("%.*s\n", (int)str_len(&s), str_buf(&s));
#endif

    str_drop(&s);
}

struct Str btreeset_to_string(struct BTreeSet *self) {
    struct Str s = str_new();
    struct BTreeSetIter iter = btreeset_iter(self);

    str_push(&s, '[');
    for (uint64_t i = 0; i < self->len; i += 1) {
        BTREESET_KEY_TYPE key;
        btreesetiter_next(&iter, &key);

        char keystr[21] = { 0 };
        snprintf(keystr, sizeof(keystr), "%" PRIu64, key);
        str_push_raw(&s, keystr);

        if (i + 1 != self->len) {
            str_push_raw(&s, ", ");
        }
    }
    str_push(&s, ']');

    btreesetiter_drop(&iter);
    return s;
}

void btreeset_tree_print(struct BTreeSet *self) {
    btreesetnode_print(&self->root);
}

uint64_t btreeset_len(struct BTreeSet *self) {
    return self->len;
}

uint64_t btreeset_degree(struct BTreeSet *self) {
    return self->degree;
}

bool btreeset_is_empty(struct BTreeSet *self) {
    return self->len == 0;
}

bool btreeset_contains(struct BTreeSet *self, BTREESET_KEY_TYPE key) {
    uint64_t index;
    return btreesetnode_find(&self->root, key, &index) != NULL;
}

void btreeset_insert(struct BTreeSet *self, BTREESET_KEY_TYPE key) {
    if (btreeset_contains(self, key)) {
        return;
    }

    if (btreesetnode_is_full(&self->root, self->degree)) {
        struct BTreeSetNode n = btreesetnode_new();
        vec_push(&n.children, self->root);
        self->root = n;
        btreesetnode_split_child(&self->root, 0, self->degree);
    }

    btreesetnode_insert_nonfull(&self->root, key, self->degree);
    self->len += 1;
}

bool btreeset_first(struct BTreeSet *self, BTREESET_KEY_TYPE *key) {
    if (btreeset_is_empty(self)) {
        return false;
    }
    *key = btreesetnode_min_key(&self->root);
    return true;
}

bool btreeset_last(struct BTreeSet *self, BTREESET_KEY_TYPE *key) {
    if (btreeset_is_empty(self)) {
        return false;
    }
    *key = btreesetnode_max_key(&self->root);
    return true;
}

void btreeset_remove(struct BTreeSet *self, BTREESET_KEY_TYPE key) {
    if (!btreeset_contains(self, key)) {
        return;
    }

    btreesetnode_delete(&self->root, key, self->degree);

    if (!btreesetnode_is_leaf(&self->root) && btreesetnode_is_empty(&self->root)) {
        struct BTreeSetNode root = btreesetnode_new();
        vec_pop(&self->root.children, &root);
        btreesetnode_drop(&self->root);
        self->root = root;
    }

    self->len -= 1;
}

void btreeset_clear(struct BTreeSet *self) {
    btreesetnode_drop(&self->root);
    self->root = btreesetnode_new();
    self->len = 0;
}

struct BTreeSetIter btreeset_iter(struct BTreeSet *self) {
    return btreesetiter_new(&self->root);
}

vec(uint8_t) btreeset_serialize(struct BTreeSet *self) {
    vec(uint8_t) bytes = vec_new();

    uint64_t len = self->len;
    uint64_t degree = self->degree;

    for (uint64_t i = 0; i < sizeof(uint64_t); i += 1) {
        vec_push(&bytes, len & 0xFF);
        len >>= 8;
    }

    for (uint64_t i = 0; i < sizeof(uint64_t); i += 1) {
        vec_push(&bytes, degree & 0xFF);
        degree >>= 8;
    }

    struct BTreeSetIter iter = btreeset_iter(self);

    for (uint64_t i = 0; i < self->len; i += 1) {
        BTREESET_KEY_TYPE key;
        btreesetiter_next(&iter, &key);

        for (uint64_t j = 0; j < sizeof(uint64_t); j += 1) {
            vec_push(&bytes, key & 0xFF);
            key >>= 8;
        }
    }

    btreesetiter_drop(&iter);

    return bytes;
}

struct BTreeSet btreeset_deserialize(vec(uint8_t) *bytes) {
    uint64_t len = 0;
    for (uint64_t i = 0; i < sizeof(uint64_t); i += 1) {
        len |= (uint64_t)(*bytes)[i] << (i * 8);
    }
    vec_flush(bytes, 0, sizeof(uint64_t));

    uint64_t degree = 0;
    for (uint64_t i = 0; i < sizeof(uint64_t); i += 1) {
        degree |= (uint64_t)(*bytes)[i] << (i * 8);
    }
    vec_flush(bytes, 0, sizeof(uint64_t));

    struct BTreeSet self = btreeset_with_degree(degree);

    // Each serialized entry consumes 8 bytes, so a count beyond the
    // remaining input is corrupt (or decrypted with the wrong key). Fail
    // loudly instead of looping for a garbage number of iterations.
#ifdef __KERNEL__
    BUG_ON(len > vec_len(bytes));
#else
    if (len > vec_len(bytes)) {
        abort();
    }
#endif

    for (uint64_t i = 0; i < len; i += 1) {
        BTREESET_KEY_TYPE key = 0;
        for (uint64_t j = 0; j < sizeof(uint64_t); j += 1) {
            key |= (uint64_t)(*bytes)[j] << (j * 8);
        }
        vec_flush(bytes, 0, sizeof(uint64_t));

        btreeset_insert(&self, key);
    }

    return self;
}
