// Delete-path purge property tests at the ERL level.
//
// 1. Master-slot rotation: after an object's slot in the master ERL is
//    re-marked and the epoch patches, the key that protected the object's
//    serialized ERL must be underivable, while untouched slots keep theirs.
// 2. Range purge: after a truncated block range is re-marked and the epoch
//    patches, those block keys must be underivable, while others keep theirs.
#include <lethe/kht_erl.h>
#include <lethe/kht_key.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static uint64_t fanouts[] = { 16, 32, 8 };

static int keys_equal(struct KhtKey *a, struct KhtKey *b) {
	return memcmp(a->bytes, b->bytes, KHT_KEY_SIZE) == 0;
}

static int failures = 0;

static void expect(int cond, const char *what) {
	printf("%s: %s\n", cond ? "ok" : "FAIL", what);
	if (!cond)
		failures++;
}

int main(void) {
	// --- Object delete: master-slot rotation ---
	{
		struct Erl master = erl_new(fanouts, 3);
		uint64_t obj_a = 5, obj_b = 9;

		struct KhtKey k_a = erl_block_write_key(&master, obj_a);
		struct KhtKey k_b = erl_block_write_key(&master, obj_b);
		erl_patch(&master);
		erl_reset(&master);

		struct KhtKey r_a = erl_block_read_key(&master, obj_a);
		struct KhtKey r_b = erl_block_read_key(&master, obj_b);
		expect(keys_equal(&r_a, &k_a), "pre-delete: A's key derivable");
		expect(keys_equal(&r_b, &k_b), "pre-delete: B's key derivable");

		// Delete A: re-mark its slot; the next patch folds a fresh key.
		erl_mark_block(&master, obj_a);
		erl_patch(&master);
		erl_reset(&master);

		r_a = erl_block_read_key(&master, obj_a);
		r_b = erl_block_read_key(&master, obj_b);
		expect(!keys_equal(&r_a, &k_a),
		    "post-delete: A's old key underivable");
		expect(keys_equal(&r_b, &k_b),
		    "post-delete: B's key still derivable");

		erl_drop(&master);
	}

	// --- Truncate: block-range rotation ---
	{
		struct Erl erl = erl_new(fanouts, 3);
		struct KhtKey last[16];

		for (uint64_t b = 0; b < 16; b += 1)
			last[b] = erl_block_write_key(&erl, b);
		erl_patch(&erl);
		erl_reset(&erl);

		// Truncate blocks [10, 16): re-mark them.
		for (uint64_t b = 10; b < 16; b += 1)
			erl_mark_block(&erl, b);
		erl_patch(&erl);
		erl_reset(&erl);

		int surviving_ok = 1, purged_gone = 1;
		for (uint64_t b = 0; b < 16; b += 1) {
			struct KhtKey k = erl_block_read_key(&erl, b);
			if (b < 10 && !keys_equal(&k, &last[b]))
				surviving_ok = 0;
			if (b >= 10 && keys_equal(&k, &last[b]))
				purged_gone = 0;
		}
		expect(surviving_ok, "post-truncate: surviving block keys intact");
		expect(purged_gone, "post-truncate: freed block keys underivable");

		erl_drop(&erl);
	}

	printf("%s\n", failures ? "FAIL" : "PASS");
	return failures ? 1 : 0;
}
