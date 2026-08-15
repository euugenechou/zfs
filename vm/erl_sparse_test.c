// Reproduction harness for the intermittent kht_forest.c:352 BUG_ON during
// zpool import (khf_deserialize's roots_len sanity guard, firing when an
// ERL is deserialized with the wrong key -> garbage plaintext).
//
// Prime hypothesis (task-5b): ERL forest folds computed by
// khf_overwrite_keyed() go through a "gap fill" path
// (module/zfs/kht_forest.c ~209-222 and ~230-246) whenever the range being
// folded starts (or ends) beyond the last root currently present in
// self->roots -- i.e. whenever a *later* modified range's start is not
// contiguous with the forest's previously-known extent (self->leaves).
// That happens whenever erl_modified_ranges() produces more than one
// disjoint range in an epoch (sparse marks), or a later epoch's lowest new
// block skips ahead of the previous epoch's highest block.
//
// In that fallthrough case, khf_overwrite_keyed() picks `patch_root` as the
// LAST existing root by default (the loop never breaks), even though that
// root's real, structural coverage does NOT extend out to the new `start`.
// It then calls khf_coverage(patch_root, real_patch_root_start, start),
// asking khf_derive_key() to derive keys for positions that are NOT actual
// descendants of patch_root's subtree. khf_derive_key()/khtpath_next()
// never validate ancestry -- they mechanically walk a hash chain purely
// from the *target* position's own coordinates, seeded with patch_root's
// key. For same-level, non-descendant positions this degenerates further:
// khtpath_next() immediately returns false (from.level >= to.level), so
// khf_derive_key() returns patch_root->key completely unmodified -- i.e.
// multiple unrelated leaf positions in the gap all collide on the exact
// same key.
//
// This harness builds Erls with genuinely sparse, non-contiguous mark
// patterns (deterministic LCG, not random()) across many epochs, verifying
// after every epoch that (a) every previously-recorded block's key is
// still correctly derivable via erl_block_read_key, and (b) a full
// serialize_keyed/deserialize_keyed round trip (exactly the on-disk
// export/import path) succeeds and reproduces identical read keys. A
// mismatch in (a), or an abort()/garbage roots_len in (b), is the
// reproduction.
//
// Build (in ~/lethe-build), plain:
//   gcc -O1 -g -I include -o /tmp/erl_sparse_test vm/erl_sparse_test.c \
//       module/zfs/kht_*.c module/zfs/btree?*.c module/zfs/str.c \
//       module/zfs/speck.c module/zfs/sha.c module/zfs/hex.c
// Build with AddressSanitizer:
//   gcc -O1 -g -fsanitize=address -I include -o /tmp/erl_sparse_test \
//       vm/erl_sparse_test.c module/zfs/kht_*.c module/zfs/btree?*.c \
//       module/zfs/str.c module/zfs/speck.c module/zfs/sha.c module/zfs/hex.c
#include <lethe/kht_erl.h>
#include <lethe/kht_key.h>
#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static uint64_t fanouts[] = { 16, 32, 8 }; // descendants: leaf=1, l3=8, l2=256, l1=4096

#define MAXBLOCK 8192

static struct KhtKey recorded[MAXBLOCK];
static int written[MAXBLOCK];

static int failures = 0;

static void expect(int cond, const char *what) {
	if (!cond) {
		printf("FAIL: %s\n", what);
		failures++;
	}
}

// Deterministic LCG (Numerical Recipes constants) -- NOT random(), so runs
// are perfectly reproducible.
static uint32_t lcg_state = 1;

static void lcg_seed(uint32_t seed) {
	lcg_state = seed;
}

static uint32_t lcg_next(void) {
	lcg_state = lcg_state * 1664525u + 1013904223u;
	return lcg_state;
}

static uint64_t lcg_range(uint64_t n) {
	return n == 0 ? 0 : lcg_next() % n;
}

static int keys_equal(struct KhtKey *a, struct KhtKey *b) {
	return memcmp(a->bytes, b->bytes, KHT_KEY_SIZE) == 0;
}

// Verify every recorded block's key is still derivable. Returns count of
// mismatches found (also accumulates into `failures`).
static int check_all(struct Erl *erl, const char *stage) {
	int bad = 0;
	for (uint64_t b = 0; b < MAXBLOCK; b += 1) {
		if (!written[b])
			continue;
		struct KhtKey k = erl_block_read_key(erl, b);
		if (!keys_equal(&k, &recorded[b])) {
			if (bad < 10)
				printf("FAIL: %s: block %" PRIu64 " key mismatch\n", stage, b);
			bad += 1;
		}
	}
	if (bad)
		failures += bad;
	return bad;
}

// Serialize/deserialize round trip exactly mirroring the on-disk
// export/import path (lethe.c's __lethe_load_object_erl <-
// erl_deserialize_keyed). A garbage roots_len (the actual kernel BUG_ON)
// would abort() inside khf_deserialize when built without __KERNEL__.
// Beyond that, verify the round-tripped Erl reproduces identical read
// keys for every recorded block -- catches silent wrong-key corruption
// that happens not to trip the size guard.
static void check_roundtrip(struct Erl *erl, const char *stage) {
	struct KhtKey key = khtkey_new();
	vec(uint8_t) bytes = erl_serialize_keyed(erl, &key);

	// erl_deserialize_keyed consumes/mutates its input vec, so hand it a
	// fresh copy -- exactly like the on-disk blob being decrypted, never
	// the live in-memory one.
	vec(uint8_t) copy = vec_new();
	vec_reserve(&copy, vec_len(&bytes));
	vec_set_len(&copy, vec_len(&bytes));
	memcpy(copy, bytes, vec_len(&bytes));

	struct Erl restored = erl_deserialize_keyed(&copy, &key);
	vec_drop(&copy);

	int bad = 0;
	for (uint64_t b = 0; b < MAXBLOCK; b += 1) {
		if (!written[b])
			continue;
		struct KhtKey k = erl_block_read_key(&restored, b);
		if (!keys_equal(&k, &recorded[b])) {
			if (bad < 10)
				printf(
				    "FAIL: %s: round-trip block %" PRIu64 " key mismatch\n", stage, b
				);
			bad += 1;
		}
	}
	if (bad)
		failures += bad;

	erl_drop(&restored);
	vec_drop(&bytes);
}

static void mark_and_write(struct Erl *erl, uint64_t block) {
	struct KhtKey k = erl_block_write_key(erl, block);
	recorded[block] = k;
	written[block] = 1;
}

int main(void) {
	// --- Phase A: the exact scenario from the task brief -----------------
	// epoch 1: sparse marks {2,3,7} within a small extent.
	// epoch 2: a DISJOINT sparse set with a gap starting well beyond the
	// forest's current extent ({500, 900}) -- the fallthrough gap-fill
	// path in khf_overwrite_keyed can only trigger when a range's start
	// is not contiguous with the previously-known extent.
	{
		struct Erl erl = erl_new(fanouts, 3);

		uint64_t epoch1[] = { 2, 3, 7 };
		for (uint64_t i = 0; i < 3; i += 1)
			mark_and_write(&erl, epoch1[i]);
		erl_patch(&erl);
		erl_reset(&erl);

		check_all(&erl, "phaseA epoch1 post-patch");
		check_roundtrip(&erl, "phaseA epoch1 round-trip");

		uint64_t epoch2[] = { 500, 900 };
		for (uint64_t i = 0; i < 2; i += 1)
			mark_and_write(&erl, epoch2[i]);
		erl_patch(&erl);
		erl_reset(&erl);

		check_all(&erl, "phaseA epoch2 post-patch (old+new)");
		check_roundtrip(&erl, "phaseA epoch2 round-trip");

		erl_drop(&erl);
	}
	memset(written, 0, sizeof(written));

	// --- Phase B: many epochs of varied sparse patterns, deterministic ---
	// LCG-driven. Ranges deliberately straddle structural boundaries
	// implied by fanouts {16,32,8}: descendants are leaf=1, level3=8,
	// level2=256, level1=4096. Includes patterns that (a) jump far ahead
	// creating large gaps, (b) fill back in near a previous extent,
	// (c) straddle multiples of 8/256/4096, (d) read never-marked blocks
	// in between epochs (mixed marked/unmarked reads).
	{
		struct Erl erl = erl_new(fanouts, 3);
		lcg_seed(0xB16B00B5u);

		int nepochs = 40;
		for (int e = 0; e < nepochs; e += 1) {
			int nmarks = 1 + (int)lcg_range(6);
			// Range grows across epochs and occasionally jumps far
			// ahead, occasionally lands right at a structural boundary.
			uint64_t base = lcg_range(MAXBLOCK - 64);
			for (int i = 0; i < nmarks; i += 1) {
				uint64_t off = lcg_range(64);
				uint64_t block = base + off;
				if (block >= MAXBLOCK)
					block = MAXBLOCK - 1;
				mark_and_write(&erl, block);
			}
			// Occasionally also touch an exact structural boundary
			// (multiples of 8, 256) to specifically probe fold-vs-shape
			// alignment.
			if (e % 3 == 0) {
				uint64_t boundary = (lcg_range(MAXBLOCK / 256)) * 256;
				if (boundary < MAXBLOCK)
					mark_and_write(&erl, boundary);
			}
			if (e % 5 == 0) {
				uint64_t boundary = (lcg_range(MAXBLOCK / 8)) * 8;
				if (boundary < MAXBLOCK)
					mark_and_write(&erl, boundary);
			}

			char stage[64];
			snprintf(stage, sizeof(stage), "phaseB epoch%d pre-patch", e);
			check_all(&erl, stage);

			erl_patch(&erl);
			erl_reset(&erl);

			snprintf(stage, sizeof(stage), "phaseB epoch%d post-patch", e);
			check_all(&erl, stage);

			snprintf(stage, sizeof(stage), "phaseB epoch%d round-trip", e);
			check_roundtrip(&erl, stage);
		}

		erl_drop(&erl);
	}
	memset(written, 0, sizeof(written));

	// --- Phase C: mimic test-delete.sh's exact sequence -------------------
	// write a batch of "objects" (sparse object numbers, like real dnode
	// allocation with gaps), patch; delete some (re-mark their slots,
	// per erl_delete_test.c's pattern, with NO new data -- pure key
	// rotation), patch again; verify survivors intact and round trip.
	{
		struct Erl erl = erl_new(fanouts, 3);

		// Sparse initial object numbers, deliberately non-contiguous with
		// gaps of varying size (mimics dnode numbering after earlier,
		// unrelated deletions elsewhere in the objset).
		uint64_t objs[] = { 3, 4, 5, 40, 41, 300, 301, 302, 1000, 4097, 4098, 5000 };
		int nobjs = (int)(sizeof(objs) / sizeof(objs[0]));
		for (int i = 0; i < nobjs; i += 1)
			mark_and_write(&erl, objs[i]);
		erl_patch(&erl);
		erl_reset(&erl);

		check_all(&erl, "phaseC after create");
		check_roundtrip(&erl, "phaseC after create round-trip");

		// Delete a subset (re-mark only, no write) -- rotates their keys
		// forward so old on-disk ciphertext becomes undecryptable, exactly
		// like object deletion in the real delete path.
		uint64_t deleted[] = { 4, 41, 301, 4097 };
		int ndel = (int)(sizeof(deleted) / sizeof(deleted[0]));
		for (int i = 0; i < ndel; i += 1) {
			erl_mark_block(&erl, deleted[i]);
			written[deleted[i]] = 0; // no longer a valid survivor to check
		}
		erl_patch(&erl);
		erl_reset(&erl);

		check_all(&erl, "phaseC after delete (survivors)");
		check_roundtrip(&erl, "phaseC after delete round-trip");

		// Create new objects landing partly inside the gaps left by
		// deletion and partly beyond the previous max object number --
		// the sparse-fold-beyond-extent scenario the hypothesis targets.
		uint64_t recreated[] = { 4, 41, 6000, 6001, 8000 };
		int nrec = (int)(sizeof(recreated) / sizeof(recreated[0]));
		for (int i = 0; i < nrec; i += 1)
			mark_and_write(&erl, recreated[i]);
		erl_patch(&erl);
		erl_reset(&erl);

		check_all(&erl, "phaseC after recreate");
		check_roundtrip(&erl, "phaseC after recreate round-trip");

		erl_drop(&erl);
	}

	printf("%s: %d failures\n", failures ? "FAIL" : "PASS", failures);
	return failures ? 1 : 0;
}
