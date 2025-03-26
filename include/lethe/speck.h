#pragma once

#ifdef __KERNEL__
    #include <linux/types.h>
#else
    #include <stdint.h>
#endif

#define SPECK_KEY_LEN 32    // 256-bit (32-byte) keys.

void speck_key_schedule(uint64_t *k, uint64_t *rk);

void speck_encrypt(uint64_t *pt, uint64_t *ct, uint64_t *rk);

void speck_decrypt(uint64_t *ct, uint64_t *pt, uint64_t *rk);

void speck_encrypt_expand_key(uint64_t *pt, uint64_t *ct, uint64_t *k);

void speck_ctr_encrypt(
    uint8_t *ct,
    uint8_t *pt,
    uint64_t len,
    uint8_t *key,
    uint64_t nonce
);

void speck_ctr_decrypt(
    uint8_t *pt,
    uint8_t *ct,
    uint64_t len,
    uint8_t *key,
    uint64_t nonce
);
