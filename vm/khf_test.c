// Isolated tests of khf_overwrite_keyed against kht-derived expectations.
#include <lethe/kht_forest.h>
#include <lethe/kht_key.h>
#include <lethe/kht_tree.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static uint64_t fanouts[] = { 16, 32, 8 };
#define N 32

static void dump_roots(struct Khf *f, const char *when) {
	printf("--- roots %s (leaves=%llu) ---\n", when,
	    (unsigned long long)f->leaves);
	for (uint64_t i = 0; i < vec_len(&f->roots); i++) {
		struct KhtRoot *r = &f->roots[i];
		printf("  [%llu] pos=(%llu,%llu) key=%02x%02x%02x%02x\n",
		    (unsigned long long)i,
		    (unsigned long long)r->pos.level,
		    (unsigned long long)r->pos.offset,
		    r->key.bytes[0], r->key.bytes[1],
		    r->key.bytes[2], r->key.bytes[3]);
	}
}

static int check_all(struct Khf *f, struct KhtKey *want, const char *stage) {
	int bad = 0;
	for (uint64_t b = 0; b < N; b++) {
		struct KhtKey k = khf_leaf_key(f, b);
		if (memcmp(k.bytes, want[b].bytes, KHT_KEY_SIZE)) {
			printf("%s: leaf %llu BAD\n", stage, (unsigned long long)b);
			bad++;
		}
	}
	if (!bad)
		printf("%s: all %d leaves ok\n", stage, N);
	return bad;
}

int main(void) {
	struct KhtKey want[N];

	// Tree 1 provides the baseline fold; trees 2..3 provide overwrites.
	struct Kht t1 = kht_with_key(khtkey_new(), fanouts, 3);
	struct Kht t2 = kht_with_key(khtkey_new(), fanouts, 3);
	struct Kht t3 = kht_with_key(khtkey_new(), fanouts, 3);

	struct Khf f = khf_with_key(khtkey_new(), fanouts, 3);
	khf_consolidate_keyed(&f, t1.root.key);
	for (uint64_t b = 0; b < N; b++)
		want[b] = kht_leaf_key(&t1, b);
	f.leaves = N; // primed, as after reads
	check_all(&f, want, "after consolidate");

	// Single-block overwrite in the middle.
	khf_overwrite_keyed(&f, 8, 9, t2.root.key);
	want[8] = kht_leaf_key(&t2, 8);
	dump_roots(&f, "after overwrite [8,9)");
	check_all(&f, want, "after overwrite [8,9)");

	// Second overwrite on the now-fragmented forest.
	khf_overwrite_keyed(&f, 20, 22, t3.root.key);
	want[20] = kht_leaf_key(&t3, 20);
	want[21] = kht_leaf_key(&t3, 21);
	dump_roots(&f, "after overwrite [20,22)");
	check_all(&f, want, "after overwrite [20,22)");

	return 0;
}
