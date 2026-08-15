// Regression test for the btreemap internal-node delete double-drop bug.
//
// btreemapnode_delete() cases 2a/2b bit-copy a predecessor/successor Erl
// (self->vals[i] = btreemapnode_max_val(pred)/min_val(succ)) and then
// recurse into the child to delete that same key, whose leaf case
// erl_drop()s the very same Erl's heap state (tree.shape.fanouts vec,
// forest.roots vec, modified btreeset). The surviving bit-copy in
// self->vals[i] is a use-after-free waiting to happen -- and is later
// double-dropped when the map itself is dropped. Case 2a/2b also silently
// leak the *old* self->vals[i] (the entry actually being deleted) since it
// is overwritten without ever being dropped.
//
// This harness inserts real Erls (each with live heap state) under 200
// keys, in a deterministic LCG-shuffled order to guarantee internal-node
// structure, then deletes every key (also in a deterministic LCG-shuffled
// order) one at a time. After each delete it spot-checks a handful of
// surviving keys: btreemap_get() must still return a live, correct Erl
// (verified via erl_block_read_key, not just non-NULL) -- this catches
// both crashes (ASan use-after-free/double-free) and silent value
// corruption from the aliasing bug.
//
// Build (in ~/lethe-build), plain:
//   gcc -O1 -g -I include -o /tmp/btreemap_delete_test \
//       vm/btreemap_delete_test.c module/zfs/kht_*.c module/zfs/btree?*.c \
//       module/zfs/str.c module/zfs/speck.c module/zfs/sha.c module/zfs/hex.c
// Build with AddressSanitizer to catch the bug directly:
//   gcc -O1 -g -fsanitize=address -I include -o /tmp/btreemap_delete_test \
//       vm/btreemap_delete_test.c module/zfs/kht_*.c module/zfs/btree?*.c \
//       module/zfs/str.c module/zfs/speck.c module/zfs/sha.c module/zfs/hex.c
#include <lethe/btreemap.h>
#include <lethe/kht_erl.h>
#include <lethe/kht_key.h>
#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define NKEYS 200
#define NBLOCKS 3

static uint64_t fanouts[] = { 16, 32, 8 };

static struct KhtKey recorded[NKEYS][NBLOCKS];
static int alive[NKEYS];

static int failures = 0;

static void expect(int cond, const char *what) {
    if (!cond) {
        printf("FAIL: %s\n", what);
        failures++;
    }
}

// Deterministic LCG (Numerical Recipes constants) -- NOT random(), so runs
// are perfectly reproducible across machines/ASan builds.
static uint32_t lcg_state = 1;

static void lcg_seed(uint32_t seed) {
    lcg_state = seed;
}

static uint32_t lcg_next(void) {
    lcg_state = lcg_state * 1664525u + 1013904223u;
    return lcg_state;
}

// Fisher-Yates shuffle of 0..n-1 using the LCG.
static void lcg_shuffle(uint64_t *arr, uint64_t n) {
    for (uint64_t i = n - 1; i > 0; i -= 1) {
        uint64_t j = lcg_next() % (i + 1);
        uint64_t tmp = arr[i];
        arr[i] = arr[j];
        arr[j] = tmp;
    }
}

// Verify that a surviving key's Erl is intact: non-NULL and its recorded
// block write-keys are still derivable via erl_block_read_key.
static void check_key(struct BTreeMap *map, uint64_t key, const char *stage) {
    struct Erl *erl = btreemap_get(map, key);
    if (!erl) {
        printf("FAIL: %s: key %" PRIu64 " missing but should survive\n", stage, key);
        failures++;
        return;
    }
    for (int b = 0; b < NBLOCKS; b += 1) {
        struct KhtKey k = erl_block_read_key(erl, (uint64_t)b);
        if (memcmp(k.bytes, recorded[key][b].bytes, KHT_KEY_SIZE) != 0) {
            printf("FAIL: %s: key %" PRIu64 " block %d value corrupted\n", stage, key, b);
            failures++;
        }
    }
}

int main(void) {
    struct BTreeMap map = btreemap_new();

    // Insert all NKEYS keys, each with a real Erl carrying live heap state
    // (fanouts vec via erl_new, plus forest roots / modified set from the
    // writes below), in a deterministic shuffled order.
    uint64_t insert_order[NKEYS];
    for (uint64_t i = 0; i < NKEYS; i += 1) {
        insert_order[i] = i;
    }
    lcg_seed(0xC0FFEEu);
    lcg_shuffle(insert_order, NKEYS);

    for (uint64_t idx = 0; idx < NKEYS; idx += 1) {
        uint64_t key = insert_order[idx];
        struct Erl erl = erl_new(fanouts, 3);
        for (int b = 0; b < NBLOCKS; b += 1) {
            recorded[key][b] = erl_block_write_key(&erl, (uint64_t)b);
        }
        erl_patch(&erl);
        erl_reset(&erl);
        // erl_patch resets state; re-derive the read keys post-patch so
        // they match what erl_block_read_key will return afterward (same
        // pattern as vm/erl_harness.c).
        for (int b = 0; b < NBLOCKS; b += 1) {
            recorded[key][b] = erl_block_read_key(&erl, (uint64_t)b);
        }
        btreemap_insert(&map, key, erl);
        alive[key] = 1;
    }

    expect(btreemap_len(&map) == NKEYS, "all keys inserted");

    // Delete every key, one at a time, in a second deterministic shuffled
    // order distinct from insertion order. With degree 2 and 200 keys the
    // tree has real internal structure, so this sequence forces the
    // internal-node delete cases (2a/2b/2c) to fire repeatedly.
    uint64_t delete_order[NKEYS];
    for (uint64_t i = 0; i < NKEYS; i += 1) {
        delete_order[i] = i;
    }
    lcg_seed(0xDEADBEEFu);
    lcg_shuffle(delete_order, NKEYS);

    for (uint64_t idx = 0; idx < NKEYS; idx += 1) {
        uint64_t key = delete_order[idx];

        btreemap_remove(&map, key);
        alive[key] = 0;

        expect(!btreemap_contains(&map, key), "deleted key gone");
        expect(btreemap_len(&map) == NKEYS - idx - 1, "length decremented");

        // Spot-check up to 5 surviving keys (deterministic pseudo-random
        // pick derived from idx, not the shuffle LCG) for corruption.
        uint64_t checked = 0;
        for (uint64_t off = 0; off < NKEYS && checked < 5; off += 1) {
            uint64_t cand = (idx + 1 + off * 37) % NKEYS;
            if (!alive[cand]) {
                continue;
            }
            check_key(&map, cand, "post-delete");
            checked += 1;
        }
    }

    expect(btreemap_len(&map) == 0, "map empty after all deletes");
    expect(btreemap_is_empty(&map), "map reports empty");

    btreemap_drop(&map);

    printf("%s: %d failures (NKEYS=%d)\n", failures ? "FAIL" : "PASS", failures, NKEYS);
    return failures ? 1 : 0;
}
