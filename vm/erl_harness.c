// Userspace harness for the ERL key-schedule invariant: after any sequence
// of writes and patches, erl_block_read_key(b) must reproduce the key that
// block b was last written with. Mimics the lethe.c capture sequence
// (erl_patch + extra erl_reset).
//
// Build (in ~/lethe-build):
//   gcc -O1 -g -I include -o /tmp/erl_harness vm/erl_harness.c \
//       module/zfs/kht_*.c module/zfs/btree*.c module/zfs/str.c \
//       module/zfs/speck.c module/zfs/sha.c module/zfs/hex.c
#include <lethe/kht_erl.h>
#include <lethe/kht_key.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define NBLOCKS 512

static uint64_t fanouts[] = { 16, 32, 8 };

static int check(struct Erl *erl, struct KhtKey *last, int *written,
    const char *stage, int epoch) {
	int bad = 0;
	for (uint64_t b = 0; b < NBLOCKS; b++) {
		if (!written[b])
			continue;
		struct KhtKey k = erl_block_read_key(erl, b);
		if (memcmp(k.bytes, last[b].bytes, KHT_KEY_SIZE) != 0) {
			if (bad < 5)
				printf("MISMATCH epoch=%d stage=%s blk=%llu\n",
				    epoch, stage, (unsigned long long)b);
			bad++;
		}
	}
	return bad;
}

int main(int argc, char **argv) {
	int seed = argc > 1 ? atoi(argv[1]) : 1;
	int epochs = argc > 2 ? atoi(argv[2]) : 20;
	int writes = argc > 3 ? atoi(argv[3]) : 100;
	srandom(seed);

	struct Erl erl = erl_new(fanouts, 3);
	static struct KhtKey last[NBLOCKS];
	static int written[NBLOCKS];
	int total_bad = 0;

	for (int e = 0; e < epochs; e++) {
		if (e == 0) {
			// Epoch 0: sequential layout, like fio laying out files.
			for (uint64_t b = 0; b < NBLOCKS; b++) {
				last[b] = erl_block_write_key(&erl, b);
				written[b] = 1;
			}
		} else {
			for (int i = 0; i < writes; i++) {
				uint64_t b = random() % NBLOCKS;
				last[b] = erl_block_write_key(&erl, b);
				written[b] = 1;
			}
		}

		total_bad += check(&erl, last, written, "pre-patch", e);

		// Mimic __lethe_capture_*: patch (which internally resets),
		// then the extra explicit reset.
		erl_patch(&erl);
		erl_reset(&erl);

		total_bad += check(&erl, last, written, "post-patch", e);
	}

	printf("%s: %d mismatches (seed=%d epochs=%d writes=%d)\n",
	    total_bad ? "FAIL" : "PASS", total_bad, seed, epochs, writes);
	return total_bad ? 1 : 0;
}
