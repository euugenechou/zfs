#pragma once

#ifdef __KERNEL__
    #include <linux/types.h>
#else
    #include <stdbool.h>
    #include <stddef.h>
#endif

struct Str {
    char *buf;
    size_t len;
    size_t cap;
};

struct Str str_new(void);

struct Str str_from_raw(const char *t);

struct Str str_clone(struct Str *t);

char *str_buf(struct Str *self);

size_t str_len(struct Str *self);

size_t str_capacity(struct Str *self);

int str_reserve(struct Str *self, size_t additional);

void str_clear(struct Str *self);

int str_push(struct Str *self, char c);

int str_pop(struct Str *self, char *c);

int str_push_raw(struct Str *self, const char *t);

int str_extend(struct Str *self, struct Str *t);

void str_drop(struct Str *self);
