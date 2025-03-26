#ifdef __KERNEL__
    #include <lethe/kht_pos.h>
    #include <linux/printk.h>
#else
    #include <lethe/kht_pos.h>
    #include <inttypes.h>
    #include <stdio.h>
#endif

struct KhtPos khtpos_new(uint64_t level, uint64_t offset) {
    struct KhtPos self = {
        .level = level,
        .offset = offset,
    };
    return self;
}

bool khtpos_is_root(struct KhtPos *self) {
    return self->level == 0 && self->offset == 0;
}

vec(uint8_t) khtpos_serialize(struct KhtPos *self) {
    vec(uint8_t) bytes = vec_new();
    uint64_t level = self->level;
    uint64_t offset = self->offset;

    for (uint64_t i = 0; i < sizeof(uint64_t); i += 1) {
        vec_push(&bytes, level & 0xFF);
        level >>= 8;
    }

    for (uint64_t i = 0; i < sizeof(uint64_t); i += 1) {
        vec_push(&bytes, offset & 0xFF);
        offset >>= 8;
    }

    return bytes;
}

struct KhtPos khtpos_deserialize(vec(uint8_t) * bytes) {
    uint64_t level = 0;
    uint64_t offset = 0;

    for (uint64_t i = 0; i < sizeof(uint64_t); i += 1) {
        level |= (uint64_t)(*bytes)[i] << (i * 8);
    }
    vec_flush(bytes, 0, sizeof(uint64_t));

    for (uint64_t i = 0; i < sizeof(uint64_t); i += 1) {
        offset |= (uint64_t)(*bytes)[i] << (i * 8);
    }
    vec_flush(bytes, 0, sizeof(uint64_t));

    return khtpos_new(level, offset);
}

void khtpos_print(struct KhtPos *self) {
#ifdef __KERNEL__
    pr_info("kht_pos { level: %llu, offset: %llu }\n", self->level, self->offset);
#else
    printf("kht_pos { level: %" PRIu64 ", offset: %" PRIu64 "}\n", self->level, self->offset);
#endif
}

#ifdef __KERNEL__
EXPORT_SYMBOL(khtpos_new);
EXPORT_SYMBOL(khtpos_is_root);
EXPORT_SYMBOL(khtpos_serialize);
EXPORT_SYMBOL(khtpos_deserialize);
EXPORT_SYMBOL(khtpos_print);
#endif
