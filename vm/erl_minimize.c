// Minimizer: layout blocks 0..N-1 (epoch 0), patch; then write a single
// block X (epoch 1), patch; then check every block's read key against its
// last write key. Reports which X breaks which blocks.
#include <lethe/kht_erl.h>
#include <lethe/kht_key.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static uint64_t fanouts[] = { 16, 32, 8 };

int main(int argc, char **argv) {
	int n = argc > 1 ? atoi(argv[1]) : 512;
	int patch_after_layout = argc > 2 ? atoi(argv[2]) : 1;
	struct KhtKey *last = calloc(n, sizeof(*last));
	int total = 0;

	for (uint64_t x = 0; x < (uint64_t)n; x++) {
		struct Erl erl = erl_new(fanouts, 3);
		for (uint64_t b = 0; b < (uint64_t)n; b++)
			last[b] = erl_block_write_key(&erl, b);
		if (patch_after_layout) {
			erl_patch(&erl);
			erl_reset(&erl);
		}

		last[x] = erl_block_write_key(&erl, x);
		erl_patch(&erl);
		erl_reset(&erl);

		int bad = 0;
		uint64_t first = 0, lastbad = 0;
		for (uint64_t b = 0; b < (uint64_t)n; b++) {
			struct KhtKey k = erl_block_read_key(&erl, b);
			if (memcmp(k.bytes, last[b].bytes, KHT_KEY_SIZE) != 0) {
				if (bad == 0)
					first = b;
				lastbad = b;
				bad++;
			}
		}
		if (bad) {
			printf("X=%llu -> %d bad (blk %llu..%llu)\n",
			    (unsigned long long)x, bad,
			    (unsigned long long)first,
			    (unsigned long long)lastbad);
			total += bad;
		}
		erl_drop(&erl);
	}
	printf("%s: total=%d (n=%d patch_after_layout=%d)\n",
	    total ? "FAIL" : "PASS", total, n, patch_after_layout);
	return total ? 1 : 0;
}
