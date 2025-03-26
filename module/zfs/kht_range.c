#ifdef __KERNEL__
    #include <lethe/kht_range.h>
    #include <linux/printk.h>
#else
    #include <lethe/kht_range.h>
    #include <inttypes.h>
    #include <stdio.h>
#endif

struct KhtRange khtrange_new(uint64_t start, uint64_t end) {
    struct KhtRange self = {.start = start, .end = end};
    return self;
}

void khtrange_print(struct KhtRange *self) {
#ifdef __KERNEL__
    pr_info("KhtRange { start: %llu, end: %llu }", self->start, self->end);
#else
    printf("KhtRange { start: %" PRIu64 ", end: %" PRIu64 "}\n", self->start, self->end);
#endif
}

#ifdef __KERNEL__
EXPORT_SYMBOL(khtrange_new);
EXPORT_SYMBOL(khtrange_print);
#endif
