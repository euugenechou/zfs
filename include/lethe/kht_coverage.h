#pragma once

#ifdef __KERNEL__
    #include <lethe/kht_pos.h>
    #include <lethe/kht_shape.h>
    #include <linux/types.h>
#else
    #include <lethe/kht_pos.h>
    #include <lethe/kht_shape.h>
    #include <stdbool.h>
    #include <stdint.h>
#endif

enum KhtCoverageState { KhtCoverageStatePre, KhtCoverageStateIntra, KhtCoverageStatePost };

struct KhtCoverage {
    struct KhtShape *shape;
    uint64_t start;
    uint64_t end;
    uint64_t level;
    enum KhtCoverageState state;
};

struct KhtCoverage khtcoverage_new(struct KhtShape *shape, uint64_t start, uint64_t end);

bool khtcoverage_next(struct KhtCoverage *self, struct KhtPos *pos);
