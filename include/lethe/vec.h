#pragma once

#ifdef __KERNEL__
    #include <linux/types.h>
    #include <linux/vmalloc.h>
#else
    #include <errno.h>
    #include <stdlib.h>
    #include <string.h>
#endif

#define vec(type) type *

#define vec_new() NULL

#define vec_len(vptr) (*vptr ? ((size_t *)(*vptr))[-1] : 0)

#define vec_capacity(vptr) (*vptr ? ((size_t *)(*vptr))[-2] : 0)

#define vec_error(vptr) (*vptr ? ((size_t *)(*vptr))[-3] : 0)

#define vec_is_empty(vptr) (vec_len(vptr) == 0)

#define vec_is_ok(vptr) (vec_error(vptr) == 0)

#define vec_is_err(vptr) (vec_error(vptr) > 0)

#define vec_set_meta(vptr, index, value)                                                           \
    do {                                                                                           \
        if (*vptr) {                                                                               \
            size_t *_ptr = (size_t *)(*vptr);                                                      \
            _ptr[index] = value;                                                                   \
        }                                                                                          \
    } while (0)

#define vec_set_len(vptr, len) vec_set_meta(vptr, -1, len)

#define vec_set_cap(vptr, cap) vec_set_meta(vptr, -2, cap)

#define vec_set_err(vptr, err) vec_set_meta(vptr, -3, err)

#ifdef __KERNEL__
    #define vec_reserve(vptr, additional)                                                          \
        do {                                                                                       \
            size_t _len = vec_len(vptr);                                                           \
            size_t _cap = vec_capacity(vptr);                                                      \
            size_t _err = vec_error(vptr);                                                         \
            if (_len + additional > _cap) {                                                        \
                size_t _new_cap = (_cap == 0) ? 1 : _cap * 2;                                      \
                while (_len + additional > _new_cap) {                                             \
                    _new_cap *= 2;                                                                 \
                }                                                                                  \
                size_t *_src = *vptr ? ((size_t *)(*vptr)) - 3 : NULL;                             \
                size_t *_dst = vmalloc((3 * sizeof(size_t)) + (_new_cap * sizeof(**vptr)));        \
                if (!_dst) {                                                                       \
                    _err = ENOMEM;                                                                 \
                } else {                                                                           \
                    if (_src) {                                                                    \
                        memcpy(_dst, _src, (3 * sizeof(size_t)) + (_len * sizeof(**vptr)));        \
                        vfree(_src);                                                               \
                    }                                                                              \
                    *vptr = (void *)(&((size_t *)_dst)[3]);                                        \
                    _cap = _new_cap;                                                               \
                }                                                                                  \
            }                                                                                      \
            vec_set_len(vptr, _len); /* To prevent uninitialized value on first allocation. */     \
            vec_set_cap(vptr, _cap); /* To prevent uninitialized value on first allocation. */     \
            vec_set_err(vptr, _err); /* To prevent uninitialized value on first allocation. */     \
        } while (0)
#else
    #define vec_reserve(vptr, additional)                                                          \
        do {                                                                                       \
            size_t _len = vec_len(vptr);                                                           \
            size_t _cap = vec_capacity(vptr);                                                      \
            size_t _err = vec_error(vptr);                                                         \
            if (_len + additional > _cap) {                                                        \
                size_t _new_cap = (_cap == 0) ? 1 : _cap * 2;                                      \
                while (_len + additional > _new_cap) {                                             \
                    _new_cap *= 2;                                                                 \
                }                                                                                  \
                size_t *_ptr = *vptr ? ((size_t *)(*vptr)) - 3 : NULL;                             \
                _ptr = realloc(_ptr, (3 * sizeof(size_t)) + (_new_cap * sizeof(**vptr)));          \
                if (!_ptr) {                                                                       \
                    _err = ENOMEM;                                                                 \
                } else {                                                                           \
                    *vptr = (void *)(&((size_t *)_ptr)[3]);                                        \
                    _cap = _new_cap;                                                               \
                }                                                                                  \
            }                                                                                      \
            vec_set_len(vptr, _len); /* To prevent uninitialized value on first allocation. */     \
            vec_set_cap(vptr, _cap); /* To prevent uninitialized value on first allocation. */     \
            vec_set_err(vptr, _err); /* To prevent uninitialized value on first allocation. */     \
        } while (0)
#endif

#define vec_push(vptr, item)                                                                       \
    do {                                                                                           \
        vec_reserve(vptr, 1);                                                                      \
        if (vec_is_ok(vptr)) {                                                                     \
            (*vptr)[vec_len(vptr)] = item;                                                         \
            vec_set_len(vptr, vec_len(vptr) + 1);                                                  \
        }                                                                                          \
    } while (0)

#define vec_pop(vptr, itemptr)                                                                     \
    do {                                                                                           \
        if (vec_is_empty(vptr)) {                                                                  \
            vec_set_err(vptr, EFAULT);                                                             \
        } else {                                                                                   \
            vec_set_len(vptr, vec_len(vptr) - 1);                                                  \
            *itemptr = (*vptr)[vec_len(vptr)];                                                     \
        }                                                                                          \
    } while (0)

#define vec_clear(vptr)                                                                            \
    do {                                                                                           \
        vec_set_len(vptr, 0);                                                                      \
    } while (0)

#define vec_append(vptr1, vptr2)                                                                   \
    do {                                                                                           \
        if (!vec_is_empty(vptr2)) {                                                                \
            vec_reserve(vptr1, vec_len(vptr2));                                                    \
            if (vec_is_ok(vptr1)) {                                                                \
                memcpy(*vptr1 + vec_len(vptr1), *vptr2, vec_len(vptr2) * sizeof(**vptr2));         \
                vec_set_len(vptr1, vec_len(vptr1) + vec_len(vptr2));                               \
                vec_clear(vptr2);                                                                  \
            }                                                                                      \
        }                                                                                          \
    } while (0)

#define vec_drain(vptr1, vptr2, start, end)                                                        \
    do {                                                                                           \
        size_t _start = start; /* Coerce to avoid errors about comparison to 0. */                 \
        size_t _end = end; /* Coerce to avoid errors about comparison to 0. */                     \
        if (_start < _end && _end <= vec_len(vptr2)) {                                             \
            size_t _items = _end - _start;                                                         \
            size_t _shift = vec_len(vptr2) - _end;                                                 \
            vec_reserve(vptr1, _items);                                                            \
            if (vec_is_ok(vptr1)) {                                                                \
                memcpy(*vptr1 + vec_len(vptr1), *vptr2 + _start, _items * sizeof(**vptr2));        \
                memmove(*vptr2 + _start, *vptr2 + _end, _shift * sizeof(**vptr2));                 \
                vec_set_len(vptr1, vec_len(vptr1) + _items);                                       \
                vec_set_len(vptr2, vec_len(vptr2) - _items);                                       \
            }                                                                                      \
        }                                                                                          \
    } while (0)

#define vec_flush(vptr, start, end)                                                                \
    do {                                                                                           \
        size_t _start = start; /* Coerce to avoid errors about comparison to 0. */                 \
        size_t _end = end; /* Coerce to avoid errors about comparison to 0. */                     \
        if (_start < _end && _end <= vec_len(vptr)) {                                              \
            size_t _items = _end - _start;                                                         \
            size_t _shift = vec_len(vptr) - _end;                                                  \
            memmove(*vptr + _start, *vptr + _end, _shift * sizeof(**vptr));                        \
            vec_set_len(vptr, vec_len(vptr) - _items);                                             \
        }                                                                                          \
    } while (0)

#define vec_insert(vptr, index, item)                                                              \
    do {                                                                                           \
        size_t _index = index; /* Coerce to avoid errors about comparison to 0. */                 \
        if (_index > vec_len(vptr)) {                                                              \
            vec_set_err(vptr, EFAULT);                                                             \
        } else {                                                                                   \
            if (_index == vec_len(vptr)) {                                                         \
                vec_push(vptr, item);                                                              \
            } else {                                                                               \
                vec_reserve(vptr, 1);                                                              \
                if (vec_is_ok(vptr)) {                                                             \
                    size_t _shift = vec_len(vptr) - _index;                                        \
                    memmove(*vptr + _index + 1, *vptr + _index, _shift * sizeof(**vptr));          \
                    (*vptr)[_index] = item;                                                        \
                    vec_set_len(vptr, vec_len(vptr) + 1);                                          \
                }                                                                                  \
            }                                                                                      \
        }                                                                                          \
    } while (0)

#define vec_remove(vptr, index, itemptr)                                                           \
    do {                                                                                           \
        size_t _index = index; /* Coerce to avoid errors about comparison to 0. */                 \
        if (vec_is_empty(vptr) || _index >= vec_len(vptr)) {                                       \
            vec_set_err(vptr, EFAULT);                                                             \
        } else {                                                                                   \
            if (_index == vec_len(vptr) - 1) {                                                     \
                vec_pop(vptr, itemptr);                                                            \
            } else {                                                                               \
                *itemptr = (*vptr)[_index];                                                        \
                size_t _shift = vec_len(vptr) - _index - 1;                                        \
                memmove(*vptr + _index, *vptr + _index + 1, _shift * sizeof(**vptr));              \
                vec_set_len(vptr, vec_len(vptr) - 1);                                              \
            }                                                                                      \
        }                                                                                          \
    } while (0)

#define vec_truncate(vptr, size)                                                                   \
    do {                                                                                           \
        size_t _size = size; /* Coerce to avoid errors about comparison to 0. */                   \
        if (_size < vec_len(vptr)) {                                                               \
            vec_set_len(vptr, _size);                                                              \
        }                                                                                          \
    } while (0)

#ifdef __KERNEL__
    #define vec_drop(vptr)                                                                         \
        do {                                                                                       \
            if (*vptr) {                                                                           \
                vec_set_len(vptr, 0);                                                              \
                vec_set_cap(vptr, 0);                                                              \
                vec_set_err(vptr, 0);                                                              \
                vfree(&((size_t *)(*vptr))[-3]);                                                   \
                *vptr = NULL;                                                                      \
            }                                                                                      \
        } while (0)
#else
    #define vec_drop(vptr)                                                                         \
        do {                                                                                       \
            if (*vptr) {                                                                           \
                vec_set_len(vptr, 0);                                                              \
                vec_set_cap(vptr, 0);                                                              \
                vec_set_err(vptr, 0);                                                              \
                free(&((size_t *)(*vptr))[-3]);                                                    \
                *vptr = NULL;                                                                      \
            }                                                                                      \
        } while (0)
#endif

#define vec_reset(vptr) vec_drop(vptr)
