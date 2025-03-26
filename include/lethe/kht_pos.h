#pragma once

#ifdef __KERNEL__
    #include <lethe/vec.h>
    #include <linux/types.h>
#else
    #include <lethe/vec.h>
    #include <stdbool.h>
    #include <stdint.h>
#endif

struct KhtPos {
    uint64_t level;
    uint64_t offset;
};

struct KhtPos khtpos_new(uint64_t level, uint64_t offset);

bool khtpos_is_root(struct KhtPos *self);

vec(uint8_t) khtpos_serialize(struct KhtPos *self);

struct KhtPos khtpos_deserialize(vec(uint8_t) *bytes);

void khtpos_print(struct KhtPos *self);
