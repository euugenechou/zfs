#ifdef __KERNEL__
    #include <lethe/hex.h>
#else
    #include <lethe/hex.h>
#endif

static uint8_t upper(uint8_t byte) {
    return (byte >> 4) & 0x0F;
}

static uint8_t lower(uint8_t byte) {
    return byte & 0x0F;
}

static uint8_t merge(uint8_t lo, uint8_t hi) {
    return (hi << 4) | lo;
}

static char encode(uint8_t nibble) {
    return "0123456789abcdef"[nibble];
}

static char decode(char hexchar) {
    return hexchar >= 97 ? hexchar - 87 : hexchar - 48;
}

struct Str hex_encode(vec(uint8_t) *data) {
    struct Str encoded = str_new();

    for (size_t i = 0; i < vec_len(data); i += 1) {
        str_push(&encoded, encode(upper((*data)[i])));
        str_push(&encoded, encode(lower((*data)[i])));
    }

    return encoded;
}

vec(uint8_t) hex_decode(struct Str *data) {
    vec(uint8_t) decoded = vec_new();

    for (size_t i = 1; i < str_len(data); i += 2) {
        uint8_t lo = decode(str_buf(data)[i]);
        uint8_t hi = decode(str_buf(data)[i - 1]);
        vec_push(&decoded, merge(lo, hi));
    }

    return decoded;
}

#ifdef __KERNEL__
EXPORT_SYMBOL(hex_encode);
EXPORT_SYMBOL(hex_decode);
#endif
