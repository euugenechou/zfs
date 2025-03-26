#pragma once

#ifdef __KERNEL__
    #include <lethe/kht_pos.h>
    #include <lethe/sha.h>
    #include <lethe/str.h>
    #include <lethe/vec.h>
    #include <linux/types.h>
#else
    #include <lethe/kht_pos.h>
    #include <lethe/sha.h>
    #include <lethe/str.h>
    #include <lethe/vec.h>
    #include <stdint.h>
#endif

#define KHT_KEY_SIZE SHA3_256_MD_LEN

struct KhtKey {
    uint8_t bytes[KHT_KEY_SIZE];
};

struct KhtKey khtkey_new(void);

struct KhtKey khtkey_from_bytes(uint8_t *bytes);

struct KhtKey khtkey_hash(struct KhtKey *self, struct KhtPos *pos);

vec(uint8_t) khtkey_serialize(struct KhtKey *self);

struct KhtKey khtkey_deserialize(vec(uint8_t) *bytes);

void khtkey_print(struct KhtKey *self);

struct Str khtkey_to_string(struct KhtKey *self);
