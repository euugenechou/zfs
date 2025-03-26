#ifdef __KERNEL__
    #include <lethe/kht_path.h>
#else
    #include <lethe/kht_path.h>
#endif

struct KhtPath khtpath_new(struct KhtShape *shape, struct KhtPos from, struct KhtPos to) {
    struct KhtPath self = {
        .shape = shape,
        .from = from,
        .to = to,
    };
    return self;
}

bool khtpath_next(struct KhtPath *self, struct KhtPos *pos) {
    if (self->from.level >= self->to.level) {
        return false;
    }

    uint64_t leaf = khtshape_start(self->shape, &self->to);
    uint64_t level = self->from.level + 1;
    uint64_t offset = khtshape_offset(self->shape, leaf, level);
    self->from = khtpos_new(level, offset);
    *pos = self->from;

    return true;
}

#ifdef __KERNEL__
EXPORT_SYMBOL(khtpath_new);
EXPORT_SYMBOL(khtpath_next);
#endif
