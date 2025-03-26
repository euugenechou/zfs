#pragma once

#ifdef __KERNEL__
    #include <lethe/kht_coverage.h>
    #include <lethe/kht_root.h>
    #include <lethe/kht_shape.h>
    #include <linux/types.h>
#else
    #include <lethe/kht_coverage.h>
    #include <lethe/kht_root.h>
    #include <lethe/kht_shape.h>
    #include <stdint.h>
#endif

struct Kht {
    struct KhtRoot root;
    struct KhtShape shape;
};

struct Kht kht_new(uint64_t *fanouts, uint64_t n);

struct Kht kht_with_key(struct KhtKey key, uint64_t *fanouts, uint64_t n);

struct Kht kht_with_pos(struct KhtPos pos, uint64_t *fanouts, uint64_t n);

struct Kht kht_with_pos_and_key(struct KhtPos pos, struct KhtKey key, uint64_t *fanouts, uint64_t n);

struct Kht kht_with_root(struct KhtRoot root, uint64_t *fanouts, uint64_t n);

struct KhtPos kht_position(struct Kht *kht);

uint64_t kht_height(struct Kht *kht);

uint64_t kht_start(struct Kht *kht);

uint64_t kht_end(struct Kht *kht);

bool kht_is_ancestor(struct Kht *kht, struct KhtPos *n);

struct KhtKey kht_root_key(struct Kht *kht);

struct KhtKey kht_node_key(struct Kht *kht, struct KhtPos *n);

struct KhtKey kht_leaf_key(struct Kht *kht, uint64_t leaf);

vec(struct KhtPos) kht_coverage(struct Kht *kht, uint64_t start, uint64_t end);

vec(uint8_t) kht_serialize(struct Kht *kht);

struct Kht kht_deserialize(vec(uint8_t) *bytes);

void kht_print(struct Kht *kht);

void kht_drop(struct Kht *kht);
