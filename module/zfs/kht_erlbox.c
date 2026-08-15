#ifdef __KERNEL__
    #include <lethe/kht_erlbox.h>
    #include <linux/vmalloc.h>
#else
    #include <lethe/kht_erlbox.h>
    #include <stdlib.h>
#endif

struct ErlBox *erlbox_new(struct Erl erl) {
#ifdef __KERNEL__
    struct ErlBox *self = vmalloc(sizeof(*self));
#else
    struct ErlBox *self = malloc(sizeof(*self));
#endif
    self->erl = erl;
    lethe_mutex_init(&self->lock);
    return self;
}

void erlbox_drop(struct ErlBox *self) {
    lethe_mutex_destroy(&self->lock);
    erl_drop(&self->erl);
#ifdef __KERNEL__
    vfree(self);
#else
    free(self);
#endif
}

#ifdef __KERNEL__
EXPORT_SYMBOL(erlbox_new);
EXPORT_SYMBOL(erlbox_drop);
#endif
