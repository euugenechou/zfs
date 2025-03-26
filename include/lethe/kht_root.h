#pragma once

#ifdef __KERNEL__
    #include <lethe/kht_key.h>
    #include <lethe/kht_pos.h>
    #include <lethe/vec.h>
    #include <linux/types.h>
#else
    #include <lethe/kht_key.h>
    #include <lethe/kht_pos.h>
    #include <lethe/vec.h>
    #include <stdint.h>
#endif

struct KhtRoot {
    struct KhtPos pos;
    struct KhtKey key;
};

struct KhtRoot khtroot_new(void);

struct KhtRoot khtroot_with_pos(struct KhtPos pos);

struct KhtRoot khtroot_with_key(struct KhtKey key);

struct KhtRoot khtroot_with_pos_and_key(struct KhtPos pos, struct KhtKey key);

vec(uint8_t) khtroot_serialize(struct KhtRoot *self);

struct KhtRoot khtroot_deserialize(vec(uint8_t) *bytes);

void khtroot_print(struct KhtRoot *self);
