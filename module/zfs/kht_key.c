#ifdef __KERNEL__
    #include <lethe/kht_key.h>
    #include <lethe/sha.h>
    #include <linux/printk.h>
    #include <linux/random.h>
#else
    #include <lethe/kht_key.h>
    #include <lethe/sha.h>
    #include <inttypes.h>
    #include <stdio.h>
    #include <stdlib.h>
    #include <string.h>
#endif

struct KhtKey khtkey_new(void) {
    struct KhtKey self;
#ifdef __KERNEL__
    get_random_bytes(self.bytes, KHT_KEY_SIZE);
#else
    for (uint64_t i = 0; i < KHT_KEY_SIZE; i += 1) {
        self.bytes[i] = random();
    }
#endif
    return self;
}

struct KhtKey khtkey_from_bytes(uint8_t *bytes) {
    struct KhtKey self;
    memcpy(self.bytes, bytes, KHT_KEY_SIZE);
    return self;
}

struct KhtKey khtkey_hash(struct KhtKey *self, struct KhtPos *pos) {
    uint8_t bytes[KHT_KEY_SIZE + 2 * sizeof(uint64_t)];
    memcpy(bytes, self->bytes, KHT_KEY_SIZE);
    memcpy(bytes + KHT_KEY_SIZE, &pos->level, sizeof(uint64_t));
    memcpy(bytes + KHT_KEY_SIZE + sizeof(uint64_t), &pos->offset, sizeof(uint64_t));

    struct KhtKey new_key;
    sha3_256_digest(bytes, KHT_KEY_SIZE + 2 * sizeof(uint64_t), new_key.bytes);
    return new_key;
}

vec(uint8_t) khtkey_serialize(struct KhtKey *self) {
    vec(uint8_t) bytes = vec_new();

    for (uint64_t i = 0; i < KHT_KEY_SIZE; i += 1) {
        vec_push(&bytes, self->bytes[i]);
    }

    return bytes;
}

struct KhtKey khtkey_deserialize(vec(uint8_t) * bytes) {
    struct KhtKey self;

    for (uint64_t i = 0; i < KHT_KEY_SIZE; i += 1) {
        self.bytes[i] = (*bytes)[i];
    }
    vec_flush(bytes, 0, KHT_KEY_SIZE);

    return self;
}

void khtkey_print(struct KhtKey *self) {
    struct Str s = khtkey_to_string(self);
#ifdef __KERNEL__
    pr_info("%.*s\n", (int)str_len(&s), str_buf(&s));
#else
    printf("%.*s\n", (int)str_len(&s), str_buf(&s));
#endif
    str_drop(&s);
}

struct Str khtkey_to_string(struct KhtKey *self) {
    struct Str s = str_from_raw("KhtKey { [");

    char buf[3] = { 0 };
    for (uint64_t i = 0; i < KHT_KEY_SIZE; i += 1) {
#ifdef __KERNEL__
	snprintf(buf, 3, "%02x", self->bytes[i]);
#else
	snprintf(buf, 3, "%02" PRIx8, self->bytes[i]);
#endif
	str_push_raw(&s, buf);
    }

    str_push_raw(&s, "] }");

    return s;
}

#ifdef __KERNEL__
EXPORT_SYMBOL(khtkey_new);
EXPORT_SYMBOL(khtkey_from_bytes);
EXPORT_SYMBOL(khtkey_hash);
EXPORT_SYMBOL(khtkey_serialize);
EXPORT_SYMBOL(khtkey_deserialize);
EXPORT_SYMBOL(khtkey_print);
EXPORT_SYMBOL(khtkey_to_string);
#endif
