#ifdef __KERNEL__
    #include <lethe/kht_coverage.h>
#else
    #include <lethe/kht_coverage.h>
#endif

struct KhtCoverage khtcoverage_new(struct KhtShape *shape, uint64_t start, uint64_t end) {
    struct KhtCoverage self = {
        .shape = shape,
        .start = start,
        .end = end,
        .level = khtshape_height(shape) - 1,
        .state = KhtCoverageStatePre,
    };
    return self;
}

bool khtcoverage_next(struct KhtCoverage *self, struct KhtPos *pos) {
    if (self->start > self->end) {
        return false;
    }

    for (;;) {
        switch (self->state) {
            case KhtCoverageStatePre: {
                if (self->level < 2) {
                    self->state = KhtCoverageStateIntra;
                } else if (
                    self->start % khtshape_descendants(self->shape, self->level - 1) != 0 &&
                    self->start + khtshape_descendants(self->shape, self->level) <= self->end
                ) {
                    uint64_t level = self->level;
                    uint64_t offset = khtshape_offset(self->shape, self->start, level);
                    *pos = khtpos_new(level, offset);
                    self->start += khtshape_descendants(self->shape, level);
                    return true;
                } else {
                    self->level -= 1;
                }
                break;
            }
            case KhtCoverageStateIntra: {
                if (self->start + khtshape_descendants(self->shape, 1) <= self->end) {
                    uint64_t level = 1;
                    uint64_t offset = khtshape_offset(self->shape, self->start, 1);
                    *pos = khtpos_new(level, offset);
                    self->start += khtshape_descendants(self->shape, level);
                    return true;
                } else {
                    self->level = 2;
                    self->state = KhtCoverageStatePost;
                }
                break;
            }
            case KhtCoverageStatePost: {
                if (self->level >= khtshape_height(self->shape)) {
                    return false;
                } else if (self->start + khtshape_descendants(self->shape, self->level) <= self->end) {
                    uint64_t level = self->level;
                    uint64_t offset = khtshape_offset(self->shape, self->start, level);
                    *pos = khtpos_new(level, offset);
                    self->start += khtshape_descendants(self->shape, level);
                    return true;
                } else {
                    self->level += 1;
                }
                break;
            }
        }
    }
}

#ifdef __KERNEL__
EXPORT_SYMBOL(khtcoverage_new);
EXPORT_SYMBOL(khtcoverage_next);
#endif
