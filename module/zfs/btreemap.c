#ifdef __KERNEL__
    #include <lethe/btreemap.h>
    #include <linux/printk.h>

    #ifndef PRIu64
        #define PRIu64 "llu"
    #endif
#else
    #include <lethe/btreemap.h>
    #include <assert.h>
    #include <stdio.h>
    #include <inttypes.h>
#endif

#define DEFAULT_DEGREE 2

struct BTreeMap btreemap_new(void) {
    return btreemap_with_degree(DEFAULT_DEGREE);
}

struct BTreeMap btreemap_with_degree(uint64_t degree) {
    struct BTreeMap self = {
        .len = 0,
        .degree = degree,
        .root = btreemapnode_new(),
    };
    return self;
}

void btreemap_drop(struct BTreeMap *self) {
    btreemapnode_drop(&self->root);
}

void btreemap_print(struct BTreeMap *self) {
    struct Str s = btreemap_to_string(self);

#ifdef __KERNEL__
    pr_info("%.*s\n", (int)str_len(&s), str_buf(&s));
#else
    printf("%.*s\n", (int)str_len(&s), str_buf(&s));
#endif

    str_drop(&s);
}

struct Str btreemap_to_string(struct BTreeMap *self) {
    struct Str s = str_new();
    struct BTreeMapIter iter = btreemap_iter(self);

    str_push(&s, '{');
    for (uint64_t i = 0; i < self->len; i += 1) {
        BTREEMAP_KEY_TYPE key;
        BTREEMAP_VAL_TYPE *val;
        btreemapiter_next(&iter, &key, &val);

        char keystr[21] = { 0 };
        snprintf(keystr, sizeof(keystr), "%" PRIu64, key);
        str_push_raw(&s, keystr);

        if (i + 1 != self->len) {
            str_push_raw(&s, ", ");
        }
    }
    str_push(&s, '}');

    btreemapiter_drop(&iter);
    return s;
}

void btreemap_tree_print(struct BTreeMap *self) {
    btreemapnode_print(&self->root);
}

uint64_t btreemap_len(struct BTreeMap *self) {
    return self->len;
}

uint64_t btreemap_degree(struct BTreeMap *self) {
    return self->degree;
}

bool btreemap_is_empty(struct BTreeMap *self) {
    return self->len == 0;
}

bool btreemap_contains(struct BTreeMap *self, BTREEMAP_KEY_TYPE key) {
    uint64_t index;
    return btreemapnode_find(&self->root, key, &index) != NULL;
}

BTREEMAP_VAL_TYPE *btreemap_get(struct BTreeMap *self, BTREEMAP_KEY_TYPE key) {
    uint64_t index;
    struct BTreeMapNode *n = btreemapnode_find(&self->root, key, &index);
    return n ? &n->vals[index] : NULL;
}

void btreemap_insert(struct BTreeMap *self, BTREEMAP_KEY_TYPE key, BTREEMAP_VAL_TYPE val) {
    if (btreemap_contains(self, key)) {
        erl_drop(&val);
        return;
    }

    if (btreemapnode_is_full(&self->root, self->degree)) {
        struct BTreeMapNode n = btreemapnode_new();
        vec_push(&n.children, self->root);
        self->root = n;
        btreemapnode_split_child(&self->root, 0, self->degree);
    }

    btreemapnode_insert_nonfull(&self->root, key, val, self->degree);
    self->len += 1;
}

void btreemap_remove(struct BTreeMap *self, BTREEMAP_KEY_TYPE key) {
    if (!btreemap_contains(self, key)) {
        return;
    }

    btreemapnode_delete(&self->root, key, self->degree);

    if (!btreemapnode_is_leaf(&self->root) && btreemapnode_is_empty(&self->root)) {
        struct BTreeMapNode root = btreemapnode_new();
        vec_pop(&self->root.children, &root);
        btreemapnode_drop(&self->root);
        self->root = root;
    }

    self->len -= 1;
}

void btreemap_clear(struct BTreeMap *self) {
    btreemapnode_drop(&self->root);
    self->root = btreemapnode_new();
    self->len = 0;
}

struct BTreeMapIter btreemap_iter(struct BTreeMap *self) {
    return btreemapiter_new(&self->root);
}

vec(uint8_t) btreemap_serialize(struct BTreeMap *self) {
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

    struct BTreeMapIter iter = btreemap_iter(self);

    for (uint64_t i = 0; i < self->len; i += 1) {
        BTREEMAP_KEY_TYPE key;
        BTREEMAP_VAL_TYPE *val;
        btreemapiter_next(&iter, &key, &val);

        for (uint64_t j = 0; j < sizeof(uint64_t); j += 1) {
            vec_push(&bytes, key & 0xFF);
            key >>= 8;
        }

        vec(uint8_t) val_bytes = erl_serialize(val);
        vec_append(&bytes, &val_bytes);
        vec_drop(&val_bytes);
    }

    btreemapiter_drop(&iter);

    return bytes;
}

struct BTreeMap btreemap_deserialize(vec(uint8_t) *bytes) {
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

    struct BTreeMap self = btreemap_with_degree(degree);

    for (uint64_t i = 0; i < len; i += 1) {
        BTREEMAP_KEY_TYPE key = 0;
        for (uint64_t j = 0; j < sizeof(uint64_t); j += 1) {
            key |= (uint64_t)(*bytes)[j] << (j * 8);
        }
        vec_flush(bytes, 0, sizeof(uint64_t));

        struct Erl val = erl_deserialize(bytes);
        btreemap_insert(&self, key, val);
    }

    return self;
}
