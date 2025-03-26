#ifdef __KERNEL__
    #include <lethe/kht_root.h>
    #include <linux/printk.h>
#else
    #include <lethe/kht_root.h>
    #include <inttypes.h>
    #include <stdio.h>
#endif

struct KhtRoot khtroot_new(void) {
    struct KhtPos pos = khtpos_new(0, 0);
    struct KhtKey key = khtkey_new();
    return khtroot_with_pos_and_key(pos, key);
}

struct KhtRoot khtroot_with_pos(struct KhtPos pos) {
    struct KhtKey key = khtkey_new();
    return khtroot_with_pos_and_key(pos, key);
}

struct KhtRoot khtroot_with_key(struct KhtKey key) {
    struct KhtPos pos = khtpos_new(0, 0);
    return khtroot_with_pos_and_key(pos, key);
}

struct KhtRoot khtroot_with_pos_and_key(struct KhtPos pos, struct KhtKey key) {
    struct KhtRoot self = {
        .pos = pos,
        .key = key,
    };
    return self;
}

vec(uint8_t) khtroot_serialize(struct KhtRoot *self) {
    vec(uint8_t) bytes = vec_new();

    vec(uint8_t) pos = khtpos_serialize(&self->pos);
    vec_append(&bytes, &pos);
    vec_drop(&pos);

    vec(uint8_t) key = khtkey_serialize(&self->key);
    vec_append(&bytes, &key);
    vec_drop(&key);

    return bytes;
}

struct KhtRoot khtroot_deserialize(vec(uint8_t) * bytes) {
    struct KhtPos pos = khtpos_deserialize(bytes);
    struct KhtKey key = khtkey_deserialize(bytes);
    return khtroot_with_pos_and_key(pos, key);
}

void khtroot_print(struct KhtRoot *self) {
#ifdef __KERNEL__
    pr_info("kht_root { ");
    pr_cont("pos: (%llu, %llu)", self->pos.level, self->pos.offset);
    pr_cont("key [");
    for (uint64_t i = 0; i < KHT_KEY_SIZE; i += 1) {
        pr_cont("%02x", self->key.bytes[i]);
    }
    pr_cont("]");
    pr_cont(" }\n");
#else
    printf("kht_root { ");
    printf("pos: (%" PRIu64 ", %" PRIu64 ")", self->pos.level, self->pos.offset);
    printf("key [");
    for (uint64_t i = 0; i < KHT_KEY_SIZE; i += 1) {
        printf("%02" PRIx8, self->key.bytes[i]);
    }
    printf("]");
    printf(" }\n");
#endif
}

#ifdef __KERNEL__
EXPORT_SYMBOL(khtroot_new);
EXPORT_SYMBOL(khtroot_with_pos);
EXPORT_SYMBOL(khtroot_with_key);
EXPORT_SYMBOL(khtroot_with_pos_and_key);
EXPORT_SYMBOL(khtroot_serialize);
EXPORT_SYMBOL(khtroot_deserialize);
EXPORT_SYMBOL(khtroot_print);
#endif
