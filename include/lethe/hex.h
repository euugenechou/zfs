#pragma once

#ifdef __KERNEL__
    #include <lethe/str.h>
    #include <lethe/vec.h>
    #include <linux/types.h>
#else
    #include <lethe/str.h>
    #include <lethe/vec.h>
    #include <stdint.h>
#endif

struct Str hex_encode(vec(uint8_t) *data);

vec(uint8_t) hex_decode(struct Str *data);
