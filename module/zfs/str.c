#ifdef __KERNEL__
    #include <lethe/str.h>
    #include <linux/string.h>
    #include <linux/types.h>
    #include <linux/vmalloc.h>
#else
    #include <lethe/str.h>
    #include <errno.h>
    #include <stdlib.h>
    #include <string.h>
#endif

struct Str str_new(void) {
    struct Str self = {
        .buf = NULL,
        .len = 0,
        .cap = 0,
    };
    return self;
}

struct Str str_from_raw(const char *t) {
    struct Str self = str_new();

    size_t len = strlen(t);
    str_reserve(&self, len);
    memcpy(self.buf, t, len);
    self.len = len;

    return self;
}

struct Str str_clone(struct Str *t) {
    if (!t->buf) {
        return str_new();
    }

    struct Str self = {
#ifdef __KERNEL__
        .buf = vmalloc(t->cap),
#else
        .buf = malloc(t->cap),
#endif
        .len = t->len,
        .cap = t->cap,
    };

    memcpy(self.buf, t->buf, t->len);

    return self;
}

char *str_buf(struct Str *self) {
    return self->buf;
}

size_t str_len(struct Str *self) {
    return self->len;
}

bool str_is_empty(struct Str *self) {
    return self->len == 0;
}

size_t str_capacity(struct Str *self) {
    return self->cap;
}

int str_reserve(struct Str *self, size_t additional) {
    if (self->len + additional > self->cap) {
        size_t cap = (self->cap == 0) ? 1 : self->cap * 2;
        while (self->len + additional > cap) {
            cap *= 2;
        }
#ifdef __KERNEL__
        char *buf = vmalloc(cap);
        if (!buf) {
            return -ENOMEM;
        }
        memcpy(buf, self->buf, self->len);
        vfree(self->buf);
#else
        char *buf = realloc(self->buf, cap);
        if (!buf) {
            return -ENOMEM;
        }
#endif
        self->buf = buf;
        self->cap = cap;
    }
    return 0;
}

void str_clear(struct Str *self) {
    self->len = 0;
}

int str_push(struct Str *self, char c) {
    if (str_reserve(self, 1) < 0) {
        return -ENOMEM;
    }
    self->buf[self->len] = c;
    self->len += 1;
    return 0;
}

int str_pop(struct Str *self, char *c) {
    if (str_is_empty(self)) {
        return -EFAULT;
    }
    self->len -= 1;
    *c = self->buf[self->len];
    return 0;
}

int str_push_raw(struct Str *self, const char *t) {
    size_t len = strlen(t);
    if (str_reserve(self, len) < 0) {
        return -ENOMEM;
    }
    memcpy(self->buf + self->len, t, len);
    self->len += len;
    return 0;
}

int str_extend(struct Str *self, struct Str *t) {
    if (str_reserve(self, t->len) < 0) {
        return -ENOMEM;
    }
    memcpy(self->buf + self->len, t->buf, t->len);
    self->len += t->len;
    return 0;
}

void str_drop(struct Str *self) {
    if (self->buf) {
#ifdef __KERNEL__
        vfree(self->buf);
#else
        free(self->buf);
#endif
        self->buf = NULL;
        self->len = 0;
        self->cap = 0;
    }
}

#ifdef __KERNEL__
EXPORT_SYMBOL(str_new);
EXPORT_SYMBOL(str_from_raw);
EXPORT_SYMBOL(str_clone);
EXPORT_SYMBOL(str_buf);
EXPORT_SYMBOL(str_len);
EXPORT_SYMBOL(str_is_empty);
EXPORT_SYMBOL(str_capacity);
EXPORT_SYMBOL(str_reserve);
EXPORT_SYMBOL(str_clear);
EXPORT_SYMBOL(str_push);
EXPORT_SYMBOL(str_pop);
EXPORT_SYMBOL(str_push_raw);
EXPORT_SYMBOL(str_extend);
EXPORT_SYMBOL(str_drop);
#endif
