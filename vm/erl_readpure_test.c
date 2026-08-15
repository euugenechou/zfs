// Read-purity property test: erl_block_read_key / erl_block_prev_key must
// not change any observable ERL state -- not the forest extent, not the
// roots, not the modified set, not the serialized bytes. Locking work
// (vm/locking-granularity-design.md) runs reads under shared locks; any
// read-side mutation is a data race there.
#include <lethe/kht_erl.h>
#include <lethe/kht_key.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static uint64_t fanouts[] = { 16, 32, 8 };
static int failures = 0;

static void expect(int cond, const char *what) {
    printf("%s: %s\n", cond ? "ok" : "FAIL", what);
    if (!cond)
        failures++;
}

// Snapshot every observable field, hammer reads, compare.
static void check_reads_pure(struct Erl *erl, uint64_t read_lo,
    uint64_t read_hi, const char *label) {
    uint64_t blocks = erl->blocks;
    uint64_t leaves = erl->forest.leaves;
    uint64_t roots = vec_len(&erl->forest.roots);
    uint64_t modified = btreeset_len(&erl->modified);
    vec(uint8_t) before = erl_serialize(erl);

    for (int rep = 0; rep < 4; rep += 1) {
        for (uint64_t b = read_lo; b < read_hi; b += 1) {
            (void)erl_block_read_key(erl, b);
            (void)erl_block_prev_key(erl, b);
        }
    }

    vec(uint8_t) after = erl_serialize(erl);
    int same_bytes = vec_len(&before) == vec_len(&after) &&
        memcmp(before, after, vec_len(&before)) == 0;

    char buf[128];
    snprintf(buf, sizeof(buf), "%s: blocks stable", label);
    expect(erl->blocks == blocks, buf);
    snprintf(buf, sizeof(buf), "%s: forest extent stable", label);
    expect(erl->forest.leaves == leaves, buf);
    snprintf(buf, sizeof(buf), "%s: forest roots stable", label);
    expect(vec_len(&erl->forest.roots) == roots, buf);
    snprintf(buf, sizeof(buf), "%s: modified set stable", label);
    expect(btreeset_len(&erl->modified) == modified, buf);
    snprintf(buf, sizeof(buf), "%s: serialized bytes stable", label);
    expect(same_bytes, buf);

    vec_drop(&before);
    vec_drop(&after);
}

int main(void) {
    // Scenario A: consolidated forest (every block written, then patched)
    // read across its full extent.
    {
        struct Erl erl = erl_new(fanouts, 3);
        for (uint64_t b = 0; b < 32; b += 1)
            (void)erl_block_write_key(&erl, b);
        erl_patch(&erl);
        erl_reset(&erl);
        check_reads_pure(&erl, 0, 32, "consolidated in-extent");
        erl_drop(&erl);
    }

    // Scenario B: non-consolidated forest (partial rewrite epoch), reads
    // both inside and BEYOND the forest extent. Out-of-extent reads must
    // not append junk roots.
    {
        struct Erl erl = erl_new(fanouts, 3);
        for (uint64_t b = 0; b < 16; b += 1)
            (void)erl_block_write_key(&erl, b);
        erl_patch(&erl);
        erl_reset(&erl);
        (void)erl_block_write_key(&erl, 2);
        (void)erl_block_write_key(&erl, 3);
        erl_patch(&erl);
        erl_reset(&erl);
        check_reads_pure(&erl, 0, 24, "partial-rewrite in+out-of-extent");
        erl_drop(&erl);
    }

    // Scenario C: reads of currently-marked blocks (tree-key path) in an
    // epoch with live modifications.
    {
        struct Erl erl = erl_new(fanouts, 3);
        for (uint64_t b = 0; b < 16; b += 1)
            (void)erl_block_write_key(&erl, b);
        erl_patch(&erl);
        erl_reset(&erl);
        (void)erl_block_write_key(&erl, 5);
        (void)erl_block_write_key(&erl, 6);
        check_reads_pure(&erl, 0, 16, "marked-block reads");
        erl_drop(&erl);
    }

    printf("%s\n", failures ? "FAIL" : "PASS");
    return failures ? 1 : 0;
}
