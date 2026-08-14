// Tiny structural repro: layout 0..15, patch, rewrite block 8, patch,
// print the forest and report which leaves derive wrong keys.
#include <lethe/kht_erl.h>
#include <lethe/kht_forest.h>
#include <lethe/kht_key.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static uint64_t fanouts[] = { 16, 32, 8 };
#define N 16

int main(int argc, char **argv) {
	uint64_t x = argc > 1 ? strtoull(argv[1], NULL, 10) : 8;
	struct Erl erl = erl_new(fanouts, 3);
	struct KhtKey last[N];

	for (uint64_t b = 0; b < N; b++)
		last[b] = erl_block_write_key(&erl, b);
	erl_patch(&erl);
	erl_reset(&erl);

	last[x] = erl_block_write_key(&erl, x);
	erl_patch(&erl);
	erl_reset(&erl);

	printf("=== forest roots after overwrite [%llu,%llu) ===\n",
	    (unsigned long long)x, (unsigned long long)x + 1);
	for (uint64_t i = 0; i < vec_len(&erl.forest.roots); i++) {
		struct KhtRoot *r = &erl.forest.roots[i];
		printf("root[%llu]: pos=(level %llu, offset %llu) key=%02x%02x%02x%02x\n",
		    (unsigned long long)i,
		    (unsigned long long)r->pos.level,
		    (unsigned long long)r->pos.offset,
		    r->key.bytes[0], r->key.bytes[1],
		    r->key.bytes[2], r->key.bytes[3]);
	}
	printf("leaves=%llu height=%llu\n",
	    (unsigned long long)erl.forest.leaves,
	    (unsigned long long)khtshape_height(&erl.forest.shape));

	for (uint64_t b = 0; b < N; b++) {
		struct KhtKey k = erl_block_read_key(&erl, b);
		printf("leaf %2llu: %s\n", (unsigned long long)b,
		    memcmp(k.bytes, last[b].bytes, KHT_KEY_SIZE) ? "BAD" : "ok");
	}
	return 0;
}
