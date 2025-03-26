#pragma once

#ifdef __KERNEL__
    #include <linux/types.h>
#else
    #include <stdint.h>
#endif

struct KhtRange {
    uint64_t start;
    uint64_t end;
};

struct KhtRange khtrange_new(uint64_t start, uint64_t end);

void khtrange_print(struct KhtRange *self);
