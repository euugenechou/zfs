#include <lethe/speck.h>

#define RL(x, r)    (((x) << (r)) | ((x) >> (64 - (r))))
#define RR(x, r)    (((x) >> (r)) | ((x) << (64 - (r))))
#define ER(x, y, k) (x = RR(x, 8), x += y, x ^= k, y = RL(y, 3), y ^= x)
#define DR(x, y, k) (y ^= x, y = RR(y, 3), x ^= k, x -= y, x = RL(x, 8))

void speck_key_schedule(uint64_t *k, uint64_t *rk) {
    uint64_t a = k[0], b = k[1], c = k[2], d = k[3];

    for (int i = 0; i < 33; i += 3) {
        rk[i + 0] = a; ER(b, a, i + 0);
        rk[i + 1] = a; ER(c, a, i + 1);
        rk[i + 2] = a; ER(d, a, i + 2);
    }

    rk[33] = a;
}

void speck_encrypt(uint64_t *pt, uint64_t *ct, uint64_t *rk) {
    ct[0] = pt[0]; ct[1] = pt[1];

    for (int i = 0; i < 34; i += 1) {
        ER(ct[1], ct[0], rk[i]);
    }
}

void speck_decrypt(uint64_t *ct, uint64_t *pt, uint64_t *rk) {
    pt[0] = ct[0]; pt[1] = ct[1];

    for (int i = 33; i >= 0; i -= 1) {
        DR(pt[1], pt[0], rk[i]);
    }
}

void speck_encrypt_expand_key(uint64_t *pt, uint64_t *ct, uint64_t *k) {
    uint64_t a = k[0], b = k[1], c = k[2], d = k[3];

    ct[0] = pt[0]; ct[1] = pt[1];

    for (uint64_t i = 0; i < 33; i += 3) {
        ER(ct[1], ct[0], a); ER(b, a, i + 0);
        ER(ct[1], ct[0], a); ER(c, a, i + 1);
        ER(ct[1], ct[0], a); ER(d, a, i + 2);
    }

    ER(ct[1], ct[0], a);
}

static void byteify(uint64_t *words, uint8_t *bytes, uint64_t nwords) {
    for (uint64_t i = 0, j = 0; i < nwords; i += 1, j += 8) {
        bytes[j + 0] = (uint8_t)(words[i] >> 0);
        bytes[j + 1] = (uint8_t)(words[i] >> 8);
        bytes[j + 2] = (uint8_t)(words[i] >> 16);
        bytes[j + 3] = (uint8_t)(words[i] >> 24);
        bytes[j + 4] = (uint8_t)(words[i] >> 32);
        bytes[j + 5] = (uint8_t)(words[i] >> 40);
        bytes[j + 6] = (uint8_t)(words[i] >> 48);
        bytes[j + 7] = (uint8_t)(words[i] >> 56);
    }
}

static void wordify(uint8_t *bytes, uint64_t *words, uint64_t nbytes) {
    for (uint64_t i = 0, j = 0; i < nbytes / 8; i += 1, j += 8) {
        words[i]  = (uint64_t)bytes[j + 0] << 0;
        words[i] |= (uint64_t)bytes[j + 1] << 8;
        words[i] |= (uint64_t)bytes[j + 2] << 16;
        words[i] |= (uint64_t)bytes[j + 3] << 24;
        words[i] |= (uint64_t)bytes[j + 4] << 32;
        words[i] |= (uint64_t)bytes[j + 5] << 40;
        words[i] |= (uint64_t)bytes[j + 6] << 48;
        words[i] |= (uint64_t)bytes[j + 7] << 56;
    }
}

void speck_ctr_encrypt(
    uint8_t *ct,
    uint8_t *pt,
    uint64_t len,
    uint8_t *key,
    uint64_t nonce
) {
    uint64_t K[4] = { 0 };
    wordify(key, K, 32);

    uint64_t Pk[2]  = { nonce, 0 };
    uint64_t Ck[2]  = { 0 };
    uint8_t  ck[16] = { 0 };

    uint64_t blocks = len / 16;
    uint64_t bytes  = len % 16;

    for (uint64_t i = 0; i < blocks; i += 1, Pk[1] += 1) {
        speck_encrypt_expand_key(Pk, Ck, K);
        byteify(Ck, ck, 2);

        for (uint64_t j = 0; j < 16; j += 1) {
            ct[16 * i + j] = pt[16 * i + j] ^ ck[j];
        }
    }

    speck_encrypt_expand_key(Pk, Ck, K);
    byteify(Ck, ck, 2);

    for (uint64_t i = 0; i < bytes; i += 1) {
        ct[16 * blocks + i] = pt[16 * blocks + i] ^ ck[i];
    }
}

void speck_ctr_decrypt(
    uint8_t *pt,
    uint8_t *ct,
    uint64_t len,
    uint8_t *key,
    uint64_t nonce
) {
    speck_ctr_encrypt(pt, ct, len, key, nonce);
}
