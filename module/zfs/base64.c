#include <lethe/base64.h>

static size_t base64_encoded_length(size_t nbytes) {
    return 4 * ((nbytes + 2) / 3);
}

static size_t base64_decoded_length(size_t nbytes) {
    return nbytes / 4 * 3;
}

static size_t base64_padded_length(struct Str *data) {
    size_t padded = 0;

    for (size_t i = 0; i < str_len(data); i += 1) {
        if (str_buf(data)[i] == '=') {
            padded += 1;
        }
    }

    return padded;
}

static const uint8_t encode_table[64] = {
    'A', 'B', 'C', 'D', 'E', 'F', 'G', 'H',
    'I', 'J', 'K', 'L', 'M', 'N', 'O', 'P',
    'Q', 'R', 'S', 'T', 'U', 'V', 'W', 'X',
    'Y', 'Z', 'a', 'b', 'c', 'd', 'e', 'f',
    'g', 'h', 'i', 'j', 'k', 'l', 'm', 'n',
    'o', 'p', 'q', 'r', 's', 't', 'u', 'v',
    'w', 'x', 'y', 'z', '0', '1', '2', '3',
    '4', '5', '6', '7', '8', '9', '+', '/'
};

static const uint8_t decode_table[256] = {
     0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,
     0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,
     0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0, 62,  0,  0,  0, 63,
    52, 53, 54, 55, 56, 57, 58, 59, 60, 61,  0,  0,  0,  0,  0,  0,
     0,  0,  1,  2,  3,  4,  5,  6,  7,  8,  9, 10, 11, 12, 13, 14,
    15, 16, 17, 18, 19, 20, 21, 22, 23, 24, 25,  0,  0,  0,  0,  0,
     0, 26, 27, 28, 29, 30, 31, 32, 33, 34, 35, 36, 37, 38, 39, 40,
    41, 42, 43, 44, 45, 46, 47, 48, 49, 50, 51,  0,  0,  0,  0,  0,
     0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,
     0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,
     0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,
     0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,
     0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,
     0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,
     0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,
     0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0
};

struct Str base64_encode(vec(uint8_t) *data) {
    struct Str encoded = str_new();

    for (size_t i = 0; i < base64_encoded_length(vec_len(data)); i += 1) {
        str_push(&encoded, '=');
    }

    const size_t moduli[3] = { 0, 2, 1 };

    size_t i = 0, j = 0;

    while (i < vec_len(data)) {
        size_t x = (i < vec_len(data)) ? (*data)[i++] : 0;
        size_t y = (i < vec_len(data)) ? (*data)[i++] : 0;
        size_t z = (i < vec_len(data)) ? (*data)[i++] : 0;

        size_t t = (x << 16) + (y << 8) + z;

        str_buf(&encoded)[j++] = (char)encode_table[(t >> 18) & 63];
        str_buf(&encoded)[j++] = (char)encode_table[(t >> 12) & 63];
        str_buf(&encoded)[j++] = (char)encode_table[(t >> 6)  & 63];
        str_buf(&encoded)[j++] = (char)encode_table[(t >> 0)  & 63];
    }

    for (size_t k = 0; k < moduli[vec_len(data) % 3]; k += 1) {
        str_buf(&encoded)[base64_encoded_length(vec_len(data)) - 1 - k] = '=';
    }

    return encoded;
}

vec(uint8_t) base64_decode(struct Str *data) {
    vec(uint8_t) decoded = vec_new();

    size_t padding = base64_padded_length(data);
    size_t length = base64_decoded_length(str_len(data)) - padding;

    for (size_t i = 0; i < length; i += 1) {
        vec_push(&decoded, 0);
    }

    size_t i = 0, j = 0;

    while (i < str_len(data)) {
        size_t w = (str_buf(data)[i] == '=') ? 0 & i++ : decode_table[(uint8_t)str_buf(data)[i++]];
        size_t x = (str_buf(data)[i] == '=') ? 0 & i++ : decode_table[(uint8_t)str_buf(data)[i++]];
        size_t y = (str_buf(data)[i] == '=') ? 0 & i++ : decode_table[(uint8_t)str_buf(data)[i++]];
        size_t z = (str_buf(data)[i] == '=') ? 0 & i++ : decode_table[(uint8_t)str_buf(data)[i++]];

        size_t t = (w << 18) + (x << 12) + (y << 6) + z;

        if (j < length) decoded[j++] = (t >> 16) & 255;
        if (j < length) decoded[j++] = (t >> 8)  & 255;
        if (j < length) decoded[j++] = (t >> 0)  & 255;
    }

    return decoded;
}
