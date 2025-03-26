#pragma once

#ifdef __KERNEL__
    #include <lethe/kht_pos.h>
    #include <lethe/kht_shape.h>
    #include <linux/types.h>
#else
    #include <lethe/kht_pos.h>
    #include <lethe/kht_shape.h>
    #include <stdbool.h>
#endif

struct KhtPath {
    struct KhtShape *shape;
    struct KhtPos from;
    struct KhtPos to;
};

struct KhtPath khtpath_new(struct KhtShape *shape, struct KhtPos from, struct KhtPos to);

bool khtpath_next(struct KhtPath *self, struct KhtPos *pos);
