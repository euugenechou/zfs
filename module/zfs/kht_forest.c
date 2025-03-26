#ifdef __KERNEL__
    #include <lethe/kht_forest.h>
    #include <lethe/kht_key.h>
    #include <lethe/kht_path.h>
    #include <lethe/kht_pos.h>
    #include <lethe/kht_shape.h>
    #include <lethe/str.h>
    #include <linux/printk.h>
    #include <linux/string.h>
#else
    #include <lethe/kht_forest.h>
    #include <lethe/kht_key.h>
    #include <lethe/kht_path.h>
    #include <lethe/kht_pos.h>
    #include <lethe/kht_shape.h>
    #include <lethe/str.h>
    #include <inttypes.h>
    #include <stdio.h>
    #include <string.h>
#endif

struct Khf khf_new(uint64_t *fanouts, uint64_t n) {
    struct KhtKey key = khtkey_new();
    return khf_with_key(key, fanouts, n);
}

struct Khf khf_with_key(struct KhtKey key, uint64_t *fanouts, uint64_t n) {
    struct KhtRoot root = khtroot_with_key(key);

    vec(struct KhtRoot) roots = vec_new();
    vec_push(&roots, root);

    struct Khf self = {
        .leaves = 0,
        .shape = khtshape_new(fanouts, n),
        .roots = roots,
    };

    return self;
}

uint64_t khf_leaves(struct Khf *self) {
    return self->leaves;
}

bool khf_is_consolidated(struct Khf *self) {
    return vec_len(&self->roots) == 1 && khtpos_is_root(&self->roots[0].pos);
}

static struct KhtKey khf_derive_key(struct Khf *self, struct KhtRoot *root, struct KhtPos *n) {
    if (root->pos.level == n->level && root->pos.offset == n->offset) {
        return root->key;
    }

    struct KhtPos pos;
    struct KhtKey key = root->key;
    struct KhtPath path = khtpath_new(&self->shape, root->pos, *n);

    while (khtpath_next(&path, &pos)) {
        key = khtkey_hash(&key, &pos);
    }

    return key;
}

static vec(struct KhtRoot)
    khf_coverage(struct Khf *self, struct KhtRoot *root, uint64_t start, uint64_t end) {
    vec(struct KhtRoot) roots = vec_new();

    struct KhtPos pos;
    struct KhtCoverage coverage = khtshape_coverage(&self->shape, start, end);

    while (khtcoverage_next(&coverage, &pos)) {
        struct KhtKey key = khf_derive_key(self, root, &pos);
        struct KhtRoot root = khtroot_with_pos_and_key(pos, key);
        vec_push(&roots, root);
    }

    return roots;
}

#ifndef __KERNEL__
static inline uint64_t min(uint64_t x, uint64_t y) {
    return x < y ? x : y;
}

static inline uint64_t max(uint64_t x, uint64_t y) {
    return x > y ? x : y;
}
#endif

struct KhtKey khf_node_key(struct Khf *self, struct KhtPos *n) {
    if (khf_is_consolidated(self)) {
        self->leaves = max(self->leaves, khtshape_end(&self->shape, n));
        return khf_derive_key(self, &self->roots[0], n);
    }

    if (self->leaves < khtshape_end(&self->shape, n)) {
        khf_append(self, khtshape_end(&self->shape, n) - self->leaves);
    }

    uint64_t size = vec_len(&self->roots);
    uint64_t left = 0;
    uint64_t right = size;
    uint64_t index = 0;

    while (left < right) {
        uint64_t mid = left + size / 2;
        struct KhtRoot *root = &self->roots[mid];

        if (khtshape_is_ancestor(&self->shape, &root->pos, n)) {
            index = mid;
            break;
        } else if (khtshape_end(&self->shape, &root->pos) <= khtshape_start(&self->shape, n)) {
            left = mid + 1;
        } else {
            right = mid;
        }

        size = right - left;
    }

    self->leaves = min(self->leaves, khtshape_end(&self->shape, n));
    return khf_derive_key(self, &self->roots[index], n);
}

struct KhtKey khf_leaf_key(struct Khf *self, uint64_t leaf) {
    struct KhtPos pos = khtpos_new(khtshape_height(&self->shape) - 1, leaf);
    return khf_node_key(self, &pos);
}

void khf_append(struct Khf *self, uint64_t leaves) {
    if (khf_is_consolidated(self)) {
        self->leaves += leaves;
        return;
    }

    struct KhtKey key = khtkey_new();
    khf_append_keyed(self, leaves, key);
}

void khf_append_keyed(struct Khf *self, uint64_t leaves, struct KhtKey key) {
    if (khf_is_consolidated(self)) {
        if (memcmp(self->roots[0].key.bytes, key.bytes, KHT_KEY_SIZE) == 0) {
            self->leaves += leaves;
            return;
        } else if (self->leaves > 0) {
            struct KhtRoot root = self->roots[0];
            vec_drop(&self->roots);
            self->roots = khf_coverage(self, &root, 0, self->leaves);
        } else {
            vec_clear(&self->roots);
        }
    }

    struct KhtRoot root = khtroot_with_key(key);
    uint64_t target = self->leaves + leaves;
    uint64_t alignment = self->leaves % khtshape_descendants(&self->shape, 1);
    uint64_t needed = min(leaves, khtshape_descendants(&self->shape, 1) - alignment);

    vec(struct KhtRoot) roots = khf_coverage(self, &root, self->leaves, self->leaves + needed);
    vec_append(&self->roots, &roots);
    vec_drop(&roots);

    self->leaves += needed;
    while (self->leaves < target) {
        struct KhtPos pos = khtpos_new(1, self->leaves / khtshape_descendants(&self->shape, 1));
        struct KhtKey key = khf_derive_key(self, &root, &pos);
        struct KhtRoot root = khtroot_with_pos_and_key(pos, key);
        vec_push(&self->roots, root);
        self->leaves += khtshape_descendants(&self->shape, 1);
    }
}

void khf_overwrite(struct Khf *self, uint64_t start, uint64_t end) {
    struct KhtKey key = khtkey_new();
    khf_overwrite_keyed(self, start, end, key);
}

void khf_overwrite_keyed(struct Khf *self, uint64_t start, uint64_t end, struct KhtKey key) {
    if (khf_is_consolidated(self)) {
        if (self->leaves == 0) {
            self->leaves = end;
        }
        struct KhtRoot root = self->roots[0];
        vec_drop(&self->roots);
        self->roots = khf_coverage(self, &root, 0, self->leaves);
    }

    struct KhtRoot root = khtroot_with_key(key);
    vec(struct KhtRoot) roots = vec_new();
    vec(struct KhtRoot) patch = vec_new();
    struct KhtRoot *patch_root = NULL;

    uint64_t patch_start = 0;
    for (uint64_t i = 0; i < vec_len(&self->roots); i += 1) {
        patch_start = i;
        patch_root = &self->roots[i];
        if (start < khtshape_end(&self->shape, &patch_root->pos)) {
            break;
        }
    }

    if (khtshape_start(&self->shape, &patch_root->pos) != start) {
        vec(struct KhtRoot) roots =
            khf_coverage(self, patch_root, khtshape_start(&self->shape, &patch_root->pos), start);
        vec_append(&patch, &roots);
        vec_drop(&roots);
    }

    vec_drain(&roots, &self->roots, 0, patch_start);

    vec(struct KhtRoot) coverage = khf_coverage(self, &root, start, end);
    vec_append(&patch, &coverage);
    vec_drop(&coverage);

    uint64_t patch_end = vec_len(&self->roots);
    if (end < khtshape_end(&self->shape, &self->roots[vec_len(&self->roots) - 1].pos)) {
        for (uint64_t i = 0; i < vec_len(&self->roots); i += 1) {
            patch_end = i + 1;
            patch_root = &self->roots[i];
            if (end <= khtshape_end(&self->shape, &patch_root->pos)) {
                break;
            }
        }

        if (khtshape_end(&self->shape, &patch_root->pos) != end) {
            vec(struct KhtRoot) roots =
                khf_coverage(self, patch_root, end, khtshape_end(&self->shape, &patch_root->pos));
            vec_append(&patch, &roots);
            vec_drop(&roots);
        }
    }

    vec_append(&roots, &patch);
    vec_drop(&patch);

    vec_drain(&roots, &self->roots, patch_end, vec_len(&self->roots));
    vec_drop(&self->roots);

    self->roots = roots;
    self->leaves = khtshape_end(&self->shape, &self->roots[vec_len(&self->roots) - 1].pos);
}

void khf_truncate(struct Khf *self, uint64_t leaves) {
    if (khf_is_consolidated(self)) {
        if (self->leaves > 0) {
            self->leaves = self->leaves - min(self->leaves, leaves);

            struct KhtRoot root = self->roots[0];
            vec_drop(&self->roots);
            self->roots = khf_coverage(self, &root, 0, self->leaves);
        }
        return;
    }

    uint64_t start = 0;
    uint64_t index = 0;
    uint64_t target = self->leaves - min(self->leaves, leaves);

    for (uint64_t i = 0; i < vec_len(&self->roots); i += 1) {
        index = i;
        start = khtshape_start(&self->shape, &self->roots[i].pos);
        if (khtshape_end(&self->shape, &self->roots[i].pos) > target) {
            break;
        }
    }

    struct KhtRoot root = self->roots[index];
    vec_flush(&self->roots, index, vec_len(&self->roots));

    self->leaves = target;

    vec(struct KhtRoot) roots = khf_coverage(self, &root, start, target);
    vec_append(&self->roots, &roots);
    vec_drop(&roots);
}

void khf_consolidate(struct Khf *self) {
    struct KhtKey key = khtkey_new();
    khf_consolidate_keyed(self, key);
}

void khf_consolidate_keyed(struct Khf *self, struct KhtKey key) {
    struct KhtRoot root = khtroot_with_key(key);
    vec_clear(&self->roots);
    vec_push(&self->roots, root);
}

vec(uint8_t) khf_serialize(struct Khf *self) {
    vec(uint8_t) bytes = vec_new();
    uint64_t leaves = self->leaves;
    uint64_t roots_len = vec_len(&self->roots);

    for (uint64_t i = 0; i < sizeof(uint64_t); i += 1) {
        vec_push(&bytes, leaves & 0xFF);
        leaves >>= 8;
    }

    vec(uint8_t) shape_bytes = khtshape_serialize(&self->shape);
    vec_append(&bytes, &shape_bytes);
    vec_drop(&shape_bytes);

    for (uint64_t i = 0; i < sizeof(uint64_t); i += 1) {
        vec_push(&bytes, roots_len & 0xFF);
        roots_len >>= 8;
    }

    for (uint64_t i = 0; i < vec_len(&self->roots); i += 1) {
        vec(uint8_t) root_bytes = khtroot_serialize(&self->roots[i]);
        vec_append(&bytes, &root_bytes);
        vec_drop(&root_bytes);
    }

    return bytes;
}

struct Khf khf_deserialize(vec(uint8_t) *bytes) {
    uint64_t leaves = 0;
    uint64_t roots_len = 0;
    vec(struct KhtRoot) roots = vec_new();

    for (uint64_t i = 0; i < sizeof(uint64_t); i += 1) {
        leaves |= (uint64_t)(*bytes)[i] << (i * 8);
    }
    vec_flush(bytes, 0, sizeof(uint64_t));

    struct KhtShape shape = khtshape_deserialize(bytes);

    for (uint64_t i = 0; i < sizeof(uint64_t); i += 1) {
        roots_len |= (uint64_t)(*bytes)[i] << (i * 8);
    }
    vec_flush(bytes, 0, sizeof(uint64_t));

    for (uint64_t i = 0; i < roots_len; i += 1) {
        struct KhtRoot root = khtroot_deserialize(bytes);
        vec_push(&roots, root);
    }

    struct Khf self = {
        .leaves = leaves,
        .shape = shape,
        .roots = roots,
    };

    return self;
}

static void
khf_printer(struct Khf *self, struct KhtRoot *root, struct Str *s, struct KhtPos pos, bool last) {
    if (pos.level == root->pos.level && pos.offset == root->pos.offset) {
#ifdef __KERNEL__
        pr_info("> ");
        for (uint64_t i = 0; i < KHT_KEY_SIZE; i += 1) {
            pr_cont("%02x", root->key.bytes[i]);
        }
        pr_cont(" (%llu, %llu)", pos.level, pos.offset);
#else
        printf("> ");
        for (uint64_t i = 0; i < KHT_KEY_SIZE; i += 1) {
            printf("%02" PRIx8, root->key.bytes[i]);
        }
        printf(" (%" PRIu64 ", %" PRIu64 ")\n", pos.level, pos.offset);
#endif
    } else {
        struct KhtKey key = khf_derive_key(self, root, &pos);
#ifdef __KERNEL__
        pr_info("%.*s%s ", (int)str_len(s), str_buf(s), last ? "└───" : "├───");
        for (uint64_t i = 0; i < KHT_KEY_SIZE; i += 1) {
            pr_cont("%02x", key.bytes[i]);
        }
        pr_cont(" (%llu, %llu)", pos.level, pos.offset);
#else
        printf("%.*s%s ", (int)str_len(s), str_buf(s), last ? "└───" : "├───");
        for (uint64_t i = 0; i < KHT_KEY_SIZE; i += 1) {
            printf("%02" PRIx8, key.bytes[i]);
        }
        printf(" (%" PRIu64 ", %" PRIu64 ")\n", pos.level, pos.offset);
#endif
    }

    if (pos.level + 1 < khtshape_height(&self->shape)) {
        for (uint64_t i = 0; i < khtshape_fanout(&self->shape, pos.level); i += 1) {
            struct Str t = str_clone(s);

            if (pos.level == root->pos.level && pos.offset == root->pos.offset) {
                str_push_raw(&t, "");
            } else if (last) {
                str_push_raw(&t, "     ");
            } else {
                str_push_raw(&t, "│    ");
            }

            struct KhtPos child = khtpos_new(
                pos.level + 1,
                pos.offset * khtshape_fanout(&self->shape, pos.level) + i
            );

            khf_printer(self, root, &t, child, i + 1 == khtshape_fanout(&self->shape, pos.level));

            str_drop(&t);
        }
    }
}

void khf_print(struct Khf *self) {
    struct Str s = str_new();
    for (uint64_t i = 0; i < vec_len(&self->roots); i += 1) {
        khf_printer(self, &self->roots[i], &s, self->roots[i].pos, true);
    }
    str_drop(&s);
}

static void khf_stringer(
    struct Khf *khf,
    struct KhtRoot *root,
    struct Str *s,
    struct KhtPos pos,
    bool last,
    struct Str *t
) {
    char scratch[128];

    if (pos.level == root->pos.level && pos.offset == root->pos.offset) {
#ifdef __KERNEL__
        str_push_raw(t, "> ");
        for (uint64_t i = 0; i < KHT_KEY_SIZE; i += 1) {
            memset(scratch, 0, sizeof(scratch));
            snprintf(scratch, sizeof(scratch), "%02x", root->key.bytes[i]);
            str_push_raw(t, scratch);
        }
        memset(scratch, 0, sizeof(scratch));
        snprintf(scratch, sizeof(scratch), " (%llu, %llu)", pos.level, pos.offset);
        str_push_raw(t, scratch);
#else
        str_push_raw(t, "> ");
        for (uint64_t i = 0; i < KHT_KEY_SIZE; i += 1) {
            memset(scratch, 0, sizeof(scratch));
            snprintf(scratch, sizeof(scratch), "%02" PRIx8, root->key.bytes[i]);
            str_push_raw(t, scratch);
        }
        memset(scratch, 0, sizeof(scratch));
        snprintf(scratch, sizeof(scratch), " (%" PRIu64 ", %" PRIu64 ")\n", pos.level, pos.offset);
        str_push_raw(t, scratch);
#endif
    } else {
        struct KhtKey key = khf_derive_key(khf, root, &pos);
#ifdef __KERNEL__
        str_extend(t, s);
        str_push_raw(t, last ? "└───" : "├───");
        for (uint64_t i = 0; i < KHT_KEY_SIZE; i += 1) {
            memset(scratch, 0, sizeof(scratch));
            snprintf(scratch, sizeof(scratch), "%02x", key.bytes[i]);
            str_push_raw(t, scratch);
        }
        memset(scratch, 0, sizeof(scratch));
        snprintf(scratch, sizeof(scratch), " (%llu, %llu)", pos.level, pos.offset);
        str_push_raw(t, scratch);
#else
        str_extend(t, s);
        str_push_raw(t, last ? "└───" : "├───");
        for (uint64_t i = 0; i < KHT_KEY_SIZE; i += 1) {
            memset(scratch, 0, sizeof(scratch));
            snprintf(scratch, sizeof(scratch), "%02" PRIx8, key.bytes[i]);
            str_push_raw(t, scratch);
        }
        memset(scratch, 0, sizeof(scratch));
        snprintf(scratch, sizeof(scratch), " (%" PRIu64 ", %" PRIu64 ")\n", pos.level, pos.offset);
        str_push_raw(t, scratch);
#endif
    }

    if (pos.level + 1 < khtshape_height(&khf->shape)) {
        for (uint64_t i = 0; i < khtshape_fanout(&khf->shape, pos.level); i += 1) {
            struct Str u = str_clone(s);

            if (pos.level == root->pos.level && pos.offset == root->pos.offset) {
                str_push_raw(&u, "");
            } else if (last) {
                str_push_raw(&u, "     ");
            } else {
                str_push_raw(&u, "│    ");
            }

            struct KhtPos child = khtpos_new(
                pos.level + 1,
                pos.offset * khtshape_fanout(&khf->shape, pos.level) + i
            );

            khf_stringer(khf, root, &u, child, i + 1 == khtshape_fanout(&khf->shape, pos.level), t);

            str_drop(&u);
        }
    }
}

struct Str khf_to_string(struct Khf *khf) {
    struct Str s = str_new();
    struct Str t = str_new();

    for (uint64_t i = 0; i < vec_len(&khf->roots); i += 1) {
        khf_stringer(khf, &khf->roots[i], &s, khf->roots[i].pos, true, &t);
    }

    char c;
    str_pop(&t, &c);
    str_push(&t, '\0');
    str_drop(&s);

    return t;
}

void khf_drop(struct Khf *self) {
    khtshape_drop(&self->shape);
    vec_drop(&self->roots);
}

#ifdef __KERNEL__
EXPORT_SYMBOL(khf_new);
EXPORT_SYMBOL(khf_with_key);
EXPORT_SYMBOL(khf_leaves);
EXPORT_SYMBOL(khf_is_consolidated);
EXPORT_SYMBOL(khf_node_key);
EXPORT_SYMBOL(khf_leaf_key);
EXPORT_SYMBOL(khf_append);
EXPORT_SYMBOL(khf_append_keyed);
EXPORT_SYMBOL(khf_overwrite);
EXPORT_SYMBOL(khf_overwrite_keyed);
EXPORT_SYMBOL(khf_truncate);
EXPORT_SYMBOL(khf_consolidate);
EXPORT_SYMBOL(khf_consolidate_keyed);
EXPORT_SYMBOL(khf_serialize);
EXPORT_SYMBOL(khf_deserialize);
EXPORT_SYMBOL(khf_print);
EXPORT_SYMBOL(khf_to_string);
EXPORT_SYMBOL(khf_drop);
#endif
