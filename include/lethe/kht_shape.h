#pragma once

#ifdef __KERNEL__
    #include <lethe/kht_coverage.h>
    #include <lethe/kht_pos.h>
    #include <lethe/vec.h>
    #include <linux/types.h>
#else
    #include <lethe/kht_coverage.h>
    #include <lethe/kht_pos.h>
    #include <lethe/vec.h>
    #include <stdbool.h>
    #include <stdint.h>
#endif

struct KhtShape {
    vec(uint64_t) descendants;
};

struct KhtShape khtshape_new(uint64_t *fanouts, uint64_t n);

struct KhtShape khtshape_from_mask(uint64_t mask);

uint64_t khtshape_to_mask(struct KhtShape *self);

uint64_t khtshape_height(struct KhtShape *self);

uint64_t khtshape_fanout(struct KhtShape *self, uint64_t level);

uint64_t khtshape_descendants(struct KhtShape *self, uint64_t level);

uint64_t khtshape_start(struct KhtShape *self, struct KhtPos *n);

uint64_t khtshape_end(struct KhtShape *self, struct KhtPos *n);

uint64_t khtshape_offset(struct KhtShape *self, uint64_t leaf, uint64_t level);

uint64_t khtshape_is_ancestor(struct KhtShape *self, struct KhtPos *n, struct KhtPos *m);

struct KhtCoverage khtshape_coverage(struct KhtShape *self, uint64_t start, uint64_t end);

vec(uint8_t) khtshape_serialize(struct KhtShape *self);

struct KhtShape khtshape_deserialize(vec(uint8_t) *bytes);

void khtshape_print(struct KhtShape *self);

void khtshape_drop(struct KhtShape *self);
