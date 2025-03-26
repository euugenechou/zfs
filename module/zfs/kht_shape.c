#ifdef __KERNEL__
    #include <lethe/kht_coverage.h>
    #include <lethe/kht_shape.h>
    #include <linux/printk.h>
#else
    #include <lethe/kht_coverage.h>
    #include <lethe/kht_shape.h>
    #include <assert.h>
    #include <inttypes.h>
    #include <stdio.h>
#endif

struct KhtShape khtshape_new(uint64_t *fanouts, uint64_t n) {
#ifdef __KERNEL__
    BUG_ON(!fanouts || n > sizeof(uint64_t));
#else
    assert(fanouts && n <= sizeof(uint64_t));
#endif

    uint64_t leaves = 1;
    for (uint64_t i = 0; i < n; i += 1) {
        leaves *= fanouts[i];
    }

    vec(uint64_t) descendants = vec_new();
    vec_push(&descendants, 0);
    for (uint64_t i = 0; i < n; i += 1) {
#ifdef __KERNEL__
        BUG_ON(fanouts[i] < 2 || fanouts[i] > 256);
#else
        assert(fanouts[i] >= 2 && fanouts[i] <= 256);
#endif
        vec_push(&descendants, leaves);
        leaves /= fanouts[i];
    }
    vec_push(&descendants, 1);

    struct KhtShape self = { .descendants = descendants };
    return self;
}

struct KhtShape khtshape_from_mask(uint64_t mask) {
    uint64_t n = 0;
    uint64_t fanouts[sizeof(uint64_t)] = {0};

    for (uint64_t i = 0; i < sizeof(uint64_t); i += 1) {
        uint64_t fanout = mask & 0xFF;
        if (fanout > 0) {
            fanouts[n] = fanout + 1;
            n += 1;
        }
        mask >>= 8;
    }

    return khtshape_new(fanouts, n);
}

uint64_t khtshape_to_mask(struct KhtShape *self) {
    uint64_t mask = 0;

    for (uint64_t i = 1; i + 1 < khtshape_height(self); i += 1) {
        mask |= (khtshape_fanout(self, i) - 1) << ((i - 1) * 8);
    }

    return mask;
}

uint64_t khtshape_height(struct KhtShape *self) {
    return vec_len(&self->descendants);
}

uint64_t khtshape_fanout(struct KhtShape *self, uint64_t level) {
    if (level == 0) {
        return 0;
    } else if (level == khtshape_height(self)) {
        return 1;
    } else {
        return self->descendants[level] / self->descendants[level + 1];
    }
}

uint64_t khtshape_descendants(struct KhtShape *self, uint64_t level) {
    return self->descendants[level];
}

uint64_t khtshape_start(struct KhtShape *self, struct KhtPos *n) {
    if (n->level == 0) {
        return 0;
    } else {
        return n->offset * self->descendants[n->level];
    }
}

uint64_t khtshape_end(struct KhtShape *self, struct KhtPos *n) {
    if (n->level == 0) {
        return 0;
    } else {
        return khtshape_start(self, n) + self->descendants[n->level];
    }
}

uint64_t khtshape_offset(struct KhtShape *self, uint64_t leaf, uint64_t level) {
    if (level == 0) {
        return 0;
    } else {
        return leaf / self->descendants[level];
    }
}

uint64_t khtshape_is_ancestor(struct KhtShape *self, struct KhtPos *n, struct KhtPos *m) {
    uint64_t n_start = khtshape_start(self, n);
    uint64_t n_end = khtshape_end(self, n);

    uint64_t m_start = khtshape_start(self, m);
    uint64_t m_end = khtshape_end(self, m);

    return !khtpos_is_root(m) && (khtpos_is_root(n) || (n_start <= m_start && m_end <= n_end));
}

struct KhtCoverage khtshape_coverage(struct KhtShape *self, uint64_t start, uint64_t end) {
    return khtcoverage_new(self, start, end);
}

vec(uint8_t) khtshape_serialize(struct KhtShape *self) {
    vec(uint8_t) bytes = vec_new();
    uint64_t mask = khtshape_to_mask(self);

    for (uint64_t i = 0; i < sizeof(uint64_t); i += 1) {
        vec_push(&bytes, mask & 0xFF);
        mask >>= 8;
    }

    return bytes;
}

struct KhtShape khtshape_deserialize(vec(uint8_t) * bytes) {
    uint64_t mask = 0;

    for (uint64_t i = 0; i < sizeof(uint64_t); i += 1) {
        mask |= (uint64_t)(*bytes)[i] << (i * 8);
    }
    vec_flush(bytes, 0, sizeof(uint64_t));

    return khtshape_from_mask(mask);
}

void khtshape_print(struct KhtShape *self) {
#ifdef __KERNEL__
    pr_info("KhtShape { [");
    for (uint64_t i = 0; i < vec_len(&self->descendants); i += 1) {
        pr_cont("%llu", self->descendants[i]);
        if (i + 1 != vec_len(&self->descendants)) {
            pr_cont(", ");
        }
    }
    pr_cont("] }\n");
#else
    printf("KhtShape { [");
    for (uint64_t i = 0; i < vec_len(&self->descendants); i += 1) {
        printf("%" PRIu64, self->descendants[i]);
        if (i + 1 != vec_len(&self->descendants)) {
            printf(", ");
        }
    }
    printf("] }\n");
#endif
}

void khtshape_drop(struct KhtShape *self) {
    vec_drop(&self->descendants);
}

#ifdef __KERNEL__
EXPORT_SYMBOL(khtshape_new);
EXPORT_SYMBOL(khtshape_from_mask);
EXPORT_SYMBOL(khtshape_to_mask);
EXPORT_SYMBOL(khtshape_height);
EXPORT_SYMBOL(khtshape_fanout);
EXPORT_SYMBOL(khtshape_descendants);
EXPORT_SYMBOL(khtshape_start);
EXPORT_SYMBOL(khtshape_end);
EXPORT_SYMBOL(khtshape_offset);
EXPORT_SYMBOL(khtshape_is_ancestor);
EXPORT_SYMBOL(khtshape_coverage);
EXPORT_SYMBOL(khtshape_serialize);
EXPORT_SYMBOL(khtshape_deserialize);
EXPORT_SYMBOL(khtshape_print);
EXPORT_SYMBOL(khtshape_drop);
#endif
