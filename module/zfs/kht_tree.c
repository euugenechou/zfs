#ifdef __KERNEL__
    #include <lethe/kht_path.h>
    #include <lethe/kht_tree.h>
    #include <lethe/str.h>
    #include <linux/printk.h>
#else
    #include <lethe/kht_path.h>
    #include <lethe/kht_tree.h>
    #include <lethe/str.h>
    #include <inttypes.h>
    #include <stdio.h>
#endif

struct Kht kht_new(uint64_t *fanouts, uint64_t n) {
    struct KhtKey key = khtkey_new();
    struct KhtPos pos = khtpos_new(0, 0);
    return kht_with_pos_and_key(pos, key, fanouts, n);
}

struct Kht kht_with_pos(struct KhtPos pos, uint64_t *fanouts, uint64_t n) {
    struct KhtKey key = khtkey_new();
    return kht_with_pos_and_key(pos, key, fanouts, n);
}

struct Kht kht_with_key(struct KhtKey key, uint64_t *fanouts, uint64_t n) {
    struct KhtPos pos = khtpos_new(0, 0);
    return kht_with_pos_and_key(pos, key, fanouts, n);
}

struct Kht kht_with_pos_and_key(struct KhtPos pos, struct KhtKey key, uint64_t *fanouts, uint64_t n) {
    struct KhtRoot root = khtroot_with_pos_and_key(pos, key);
    return kht_with_root(root, fanouts, n);
}

struct Kht kht_with_root(struct KhtRoot root, uint64_t *fanouts, uint64_t n) {
    struct Kht self = {
        .root = root,
        .shape = khtshape_new(fanouts, n),
    };
    return self;
}

struct KhtPos kht_position(struct Kht *self) {
    return self->root.pos;
}

uint64_t kht_height(struct Kht *self) {
    return khtshape_height(&self->shape);
}

uint64_t kht_start(struct Kht *self) {
    return khtshape_start(&self->shape, &self->root.pos);
}

uint64_t kht_end(struct Kht *self) {
    return khtshape_end(&self->shape, &self->root.pos);
}

bool kht_is_ancestor(struct Kht *self, struct KhtPos *n) {
    return khtshape_is_ancestor(&self->shape, &self->root.pos, n);
}

struct KhtKey kht_root_key(struct Kht *self) {
    return self->root.key;
}

struct KhtKey kht_node_key(struct Kht *self, struct KhtPos *n) {
    if (self->root.pos.level == n->level && self->root.pos.offset == n->offset) {
        return self->root.key;
    }

    struct KhtPos pos;
    struct KhtKey key = self->root.key;
    struct KhtPath path = khtpath_new(&self->shape, self->root.pos, *n);

    while (khtpath_next(&path, &pos)) {
        key = khtkey_hash(&key, &pos);
    }

    return key;
}

struct KhtKey kht_leaf_key(struct Kht *self, uint64_t leaf) {
    struct KhtPos pos = khtpos_new(khtshape_height(&self->shape) - 1, leaf);
    return kht_node_key(self, &pos);
}

vec(struct KhtPos) kht_coverage(struct Kht *self, uint64_t start, uint64_t end) {
    vec(struct KhtPos) roots = vec_new();

    struct KhtPos pos;
    struct KhtCoverage cover = khtshape_coverage(&self->shape, start, end);

    while (khtcoverage_next(&cover, &pos)) {
        vec_push(&roots, pos);
    }

    return roots;
}

vec(uint8_t) kht_serialize(struct Kht *self) {
    vec(uint8_t) bytes = vec_new();

    vec(uint8_t) root_bytes = khtroot_serialize(&self->root);
    vec_append(&bytes, &root_bytes);
    vec_drop(&root_bytes);

    vec(uint8_t) shape_bytes = khtshape_serialize(&self->shape);
    vec_append(&bytes, &shape_bytes);
    vec_drop(&shape_bytes);

    return bytes;
}

struct Kht kht_deserialize(vec(uint8_t) *bytes) {
    struct KhtRoot root = khtroot_deserialize(bytes);
    struct KhtShape shape = khtshape_deserialize(bytes);
    struct Kht self = {
        .root = root,
        .shape = shape,
    };
    return self;
}

static void kht_printer(struct Kht *self, struct Str *s, struct KhtPos pos, bool last) {
    if (pos.level == self->root.pos.level && pos.offset == self->root.pos.offset) {
#ifdef __KERNEL__
        pr_info("> ");
        for (uint64_t i = 0; i < KHT_KEY_SIZE; i += 1) {
            pr_cont("%02x", self->root.key.bytes[i]);
        }
        pr_cont(" (%llu, %llu)", pos.level, pos.offset);
#else
        printf("> ");
        for (uint64_t i = 0; i < KHT_KEY_SIZE; i += 1) {
            printf("%02" PRIx8, self->root.key.bytes[i]);
        }
        printf(" (%" PRIu64 ", %" PRIu64 ")\n", pos.level, pos.offset);
#endif
    } else {
#ifdef __KERNEL__
        pr_info("%.*s%s ", (int)str_len(s), str_buf(s), last ? "└───" : "├───");
        for (uint64_t i = 0; i < KHT_KEY_SIZE; i += 1) {
            pr_cont("%02x", self->root.key.bytes[i]);
        }
        pr_cont(" (%llu, %llu)", pos.level, pos.offset);
#else
        printf("%.*s%s ", (int)str_len(s), str_buf(s), last ? "└───" : "├───");
        for (uint64_t i = 0; i < KHT_KEY_SIZE; i += 1) {
            printf("%02" PRIx8, self->root.key.bytes[i]);
        }
        printf(" (%" PRIu64 ", %" PRIu64 ")\n", pos.level, pos.offset);
#endif
    }

    if (pos.level + 1 < khtshape_height(&self->shape)) {
        for (uint64_t i = 0; i < khtshape_fanout(&self->shape, pos.level); i += 1) {
            struct Str t = str_clone(s);

            if (pos.level == self->root.pos.level && pos.offset == self->root.pos.offset) {
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

            kht_printer(self, &t, child, i + 1 == khtshape_fanout(&self->shape, pos.level));

            str_drop(&t);
        }
    }
}

void kht_print(struct Kht *self) {
    struct Str s = str_new();
    kht_printer(self, &s, self->root.pos, true);
    str_drop(&s);
}

void kht_drop(struct Kht *self) {
    khtshape_drop(&self->shape);
}

#ifdef __KERNEL__
EXPORT_SYMBOL(kht_new);
EXPORT_SYMBOL(kht_with_key);
EXPORT_SYMBOL(kht_with_pos);
EXPORT_SYMBOL(kht_with_pos_and_key);
EXPORT_SYMBOL(kht_with_root);
EXPORT_SYMBOL(kht_position);
EXPORT_SYMBOL(kht_height);
EXPORT_SYMBOL(kht_start);
EXPORT_SYMBOL(kht_end);
EXPORT_SYMBOL(kht_is_ancestor);
EXPORT_SYMBOL(kht_root_key);
EXPORT_SYMBOL(kht_node_key);
EXPORT_SYMBOL(kht_leaf_key);
EXPORT_SYMBOL(kht_coverage);
EXPORT_SYMBOL(kht_serialize);
EXPORT_SYMBOL(kht_deserialize);
EXPORT_SYMBOL(kht_print);
EXPORT_SYMBOL(kht_drop);
#endif
