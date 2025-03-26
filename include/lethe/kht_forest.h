#pragma once

#ifdef __KERNEL__
    #include <lethe/kht_root.h>
    #include <lethe/kht_key.h>
    #include <lethe/kht_shape.h>
    #include <lethe/str.h>
    #include <lethe/vec.h>
    #include <linux/types.h>
#else
    #include <lethe/kht_root.h>
    #include <lethe/kht_key.h>
    #include <lethe/kht_shape.h>
    #include <lethe/str.h>
    #include <lethe/vec.h>
    #include <stdbool.h>
    #include <stdint.h>
#endif

struct Khf {
    uint64_t leaves;
    struct KhtShape shape;
    vec(struct KhtRoot) roots;
};

struct Khf khf_new(uint64_t *fanouts, uint64_t n);

struct Khf khf_with_key(struct KhtKey key, uint64_t *fanouts, uint64_t n);

uint64_t khf_leaves(struct Khf *self);

bool khf_is_consolidated(struct Khf *self);

struct KhtKey khf_node_key(struct Khf *self, struct KhtPos *n);

struct KhtKey khf_leaf_key(struct Khf *self, uint64_t leaf);

void khf_append(struct Khf *self, uint64_t leaves);

void khf_append_keyed(struct Khf *self, uint64_t leaves, struct KhtKey key);

void khf_overwrite(struct Khf *self, uint64_t start, uint64_t end);

void khf_overwrite_keyed(struct Khf *self, uint64_t start, uint64_t end, struct KhtKey key);

void khf_truncate(struct Khf *self, uint64_t leaves);

void khf_consolidate(struct Khf *self);

void khf_consolidate_keyed(struct Khf *self, struct KhtKey key);

vec(uint8_t) khf_serialize(struct Khf *self);

struct Khf khf_deserialize(vec(uint8_t) *bytes);

void khf_print(struct Khf *self);

struct Str khf_to_string(struct Khf *khf);

void khf_drop(struct Khf *self);
