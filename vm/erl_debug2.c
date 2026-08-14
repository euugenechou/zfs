// Ground truth for shape semantics + stage-by-stage key checks.
#include <lethe/kht_erl.h>
#include <lethe/kht_forest.h>
#include <lethe/kht_key.h>
#include <lethe/kht_shape.h>
#include <lethe/kht_pos.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static uint64_t fanouts[] = { 16, 32, 8 };
#define N 16

static void dump(const char *what, struct KhtKey *k) {
	printf("%s=%02x%02x%02x%02x ", what,
	    k->bytes[0], k->bytes[1], k->bytes[2], k->bytes[3]);
}

int main(void) {
	struct Erl erl = erl_new(fanouts, 3);
	struct KhtKey last[N];

	struct KhtShape *sh = &erl.forest.shape;
	uint64_t h = khtshape_height(sh);
	printf("height=%llu\n", (unsigned long long)h);
	for (uint64_t lvl = 0; lvl < h; lvl++) {
		struct KhtPos p = khtpos_new(lvl, 0);
		printf("level %llu: start=%llu end=%llu\n",
		    (unsigned long long)lvl,
		    (unsigned long long)khtshape_start(sh, &p),
		    (unsigned long long)khtshape_end(sh, &p));
	}
	struct KhtPos a30 = khtpos_new(3, 0), a42 = khtpos_new(4, 2), a48 = khtpos_new(4, 8);
	struct KhtPos a31 = khtpos_new(3, 1);
	printf("end(3,0)=%llu end(3,1)=%llu start(3,1)=%llu\n",
	    (unsigned long long)khtshape_end(sh, &a30),
	    (unsigned long long)khtshape_end(sh, &a31),
	    (unsigned long long)khtshape_start(sh, &a31));
	printf("is_ancestor((3,0),(4,2))=%d is_ancestor((4,8),(4,8))=%d is_ancestor((3,0),(4,8))=%d\n",
	    khtshape_is_ancestor(sh, &a30, &a42),
	    khtshape_is_ancestor(sh, &a48, &a48),
	    khtshape_is_ancestor(sh, &a30, &a48));

	for (uint64_t b = 0; b < N; b++)
		last[b] = erl_block_write_key(&erl, b);

	erl_patch(&erl);
	erl_reset(&erl);

	printf("--- after epoch-0 patch (consolidated) ---\n");
	for (uint64_t b = 0; b < N; b++) {
		struct KhtKey k = erl_block_read_key(&erl, b);
		if (memcmp(k.bytes, last[b].bytes, KHT_KEY_SIZE)) {
			printf("leaf %llu BAD: ", (unsigned long long)b);
			dump("want", &last[b]);
			dump("got", &k);
			printf("\n");
		}
	}
	printf("leaves now=%llu roots=%llu\n",
	    (unsigned long long)erl.forest.leaves,
	    (unsigned long long)vec_len(&erl.forest.roots));
	return 0;
}
