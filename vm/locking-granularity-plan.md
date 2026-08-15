# Lethe Locking Granularity Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Remove the global writer-lock funnel from Lethe's block-crypt hot path in three measured steps: read purification, a single reader-shared rwlock, and boxed ERLs with per-ERL mutexes.

**Architecture:** Spec: `vm/locking-granularity-design.md`. Step 0 makes read-side key derivation genuinely pure. Step A collapses the five always-taken-together SPA rwlocks into one, with decrypts as READER. Step C boxes ERLs behind stable pointers with embedded mutexes; a structure rwlock (READER = any derivation, WRITER = create/purge/load) protects container shape.

**Tech Stack:** OpenZFS 2.4.3 fork (kernel C), lethe KHT library (shared kernel/userspace), Lima VM `lethe` for build/test, fio for measurement.

## Global Constraints

- NO blocking DMU I/O while any lethe lock is held (deadlock invariant; see `vm/deadlock-evidence.md`).
- Lock order (Step C): `lethe_struct_lock`(READER) -> object box mutex -> master box mutex -> uber mutex. Never any other order.
- The lethe container library owns its values: `btreemapnode_delete()` / `hashmap_remove()` deep-drop removed values. Never drop a value still inside a container.
- The lethe library must keep compiling in BOTH kernel (`__KERNEL__`) and userspace (vm harnesses, libzpool) contexts.
- All kernel work happens in the `lethe` Lima VM. Build: `limactl shell lethe -- bash ~/lethe/vm/sync-build.sh` (debug build in `~/lethe-build`). If the VM wedges: `limactl stop -f lethe && limactl start lethe`.
- Kernel test scripts need a module reload each run; they handle it themselves (`scripts/zfs.sh`).
- Commit messages: short, lowercase, imperative (match `git log`: "purge keys on file delete, truncate, and dataset destroy").
- Performance runs use the NON-debug build only (`vm/sync-build.sh perf`, build dir `~/lethe-build-perf`).
- Code style: tabs in `module/zfs/lethe.c` (mixed tabs/spaces exist; match surrounding lines), 4-space indent in `module/zfs/kht_*.c` and `vm/*.c`.

---

### Task 1: Measurement + gauntlet infrastructure, record baseline

**Files:**
- Modify: `vm/sync-build.sh`
- Create: `vm/perf-fio.sh`
- Create: `vm/gauntlet.sh`
- Create: `vm/perf-log.md`

**Interfaces:**
- Produces: `vm/sync-build.sh perf` (non-debug build in `~/lethe-build-perf`); `vm/perf-fio.sh` (prints fio READ/WRITE lines per jobs level); `vm/gauntlet.sh` (exit 0 = all regression tests pass; auto-includes any `vm/test-*.sh` and known harnesses). Later tasks run both verbatim.

- [ ] **Step 1: Add a `perf` mode to sync-build.sh**

Replace the body of `vm/sync-build.sh` from the `[ "${1:-}" = "sync" ]` line down with:

```bash
MODE="${1:-debug}"

if [ "$MODE" = "perf" ]; then
	DST="${LETHE_BUILD_PERF:-$HOME/lethe-build-perf}"
	mkdir -p "$DST"
	rsync -a --delete \
		--exclude '.git' \
		--exclude 'vm/' \
		"$SRC"/ "$DST"/
fi

[ "$MODE" = "sync" ] && exit 0

cd "$DST"
if [ ! -x configure ]; then
	./autogen.sh
fi
if [ ! -f Makefile ]; then
	if [ "$MODE" = "perf" ]; then
		./configure --enable-debuginfo
	else
		./configure --enable-debug --enable-debuginfo
	fi
fi
make -s -j"$(nproc)"
```

(The initial rsync into `~/lethe-build` at the top of the script stays as-is; the perf branch re-rsyncs into its own tree. Update the usage comment at the top of the file to document `perf`.)

- [ ] **Step 2: Create vm/gauntlet.sh**

```bash
#!/usr/bin/env bash
# Run INSIDE the lethe VM: full regression gauntlet -- userspace harnesses
# then kernel end-to-end scripts. Exit 0 iff everything passes.
set -uo pipefail

BUILD="${LETHE_BUILD:-$HOME/lethe-build}"
SRC="${LETHE_SRC:-$HOME/lethe}"
FAILS=0

run() { echo "=== $* ==="; "$@"; local rc=$?; \
	[ $rc -ne 0 ] && { echo "!!! FAILED ($rc): $*"; FAILS=$((FAILS+1)); }; }

cd "$BUILD"

# Userspace harnesses (compiled fresh from the synced build tree).
for t in erl_harness khf_test erl_delete_test erl_readpure_test; do
	[ -f "$SRC/vm/$t.c" ] || continue
	if gcc -O1 -g -I include -o "/tmp/$t" "$SRC/vm/$t.c" \
		module/zfs/kht_*.c module/zfs/btree*.c module/zfs/str.c \
		module/zfs/speck.c module/zfs/sha.c module/zfs/hex.c; then
		run "/tmp/$t"
	else
		echo "!!! FAILED to compile $t"; FAILS=$((FAILS+1))
	fi
done

# Kernel end-to-end (each script loads modules + builds a fresh pool).
run bash "$SRC/vm/repro-deadlock.sh"
run bash "$SRC/vm/verify-integrity.sh"
run bash "$SRC/vm/test-delete.sh"
[ -f "$SRC/vm/test-readonly.sh" ] && run bash "$SRC/vm/test-readonly.sh"
[ -f "$SRC/vm/test-churn.sh" ] && run bash "$SRC/vm/test-churn.sh"

echo "=== gauntlet: $([ $FAILS -eq 0 ] && echo PASS || echo "FAIL ($FAILS)") ==="
exit $FAILS
```

Note: `repro-deadlock.sh` prints `=== fio exit code: N ===`; if its own exit status does not reflect fio failure, fix it while here (make it `exit $rc`). Check its tail before relying on it.

- [ ] **Step 3: Create vm/perf-fio.sh**

```bash
#!/usr/bin/env bash
# Run INSIDE the lethe VM against the NON-DEBUG build:
#   bash ~/lethe/vm/sync-build.sh perf && bash ~/lethe/vm/perf-fio.sh
# Prints fio READ/WRITE bandwidth lines per (workload, jobs). Copy the
# output into vm/perf-log.md on the host with a heading for the step.
set -euo pipefail

BUILD="${LETHE_BUILD_PERF:-$HOME/lethe-build-perf}"
POOL=lethetest
IMG=/var/tmp/lethe-pool.img
DS=$POOL/enc

cd "$BUILD"
sudo ./zpool destroy $POOL 2>/dev/null || true
sudo ./scripts/zfs.sh -u 2>/dev/null || true
sudo ./scripts/zfs.sh
# Small ARC so steady-state reads miss and actually exercise decryption.
echo $((256 * 1024 * 1024)) | sudo tee /sys/module/zfs/parameters/zfs_arc_max >/dev/null
sudo rm -f "$IMG" && truncate -s 8G "$IMG"
sudo ./zpool create -f $POOL "$IMG"
echo "lethe-passphrase" | sudo ./zfs create -o encryption=on \
	-o keyformat=passphrase -o keylocation=prompt -o atime=off $DS
MNT=$(sudo ./zfs get -H -o value mountpoint $DS)

for jobs in 1 2 4 8; do
	for rw in randread randrw; do
		echo "### $rw jobs=$jobs"
		sudo fio --name=perf --directory="$MNT" --rw=$rw --bs=128k \
			--size=256M --numjobs=$jobs --time_based --runtime=30 \
			--group_reporting 2>&1 | grep -E "READ:|WRITE:|err="
	done
done

sudo ./zpool destroy $POOL
```

- [ ] **Step 4: Build both trees and verify the scripts run**

Run: `limactl shell lethe -- bash ~/lethe/vm/sync-build.sh` then `limactl shell lethe -- bash ~/lethe/vm/sync-build.sh perf`
Expected: both builds complete (`make` exits 0). The perf tree's `configure` must NOT show debug: check with `limactl shell lethe -- grep -c -- '--enable-debug ' ~/lethe-build-perf/config.log` (expect 0 matches of the debug flag with trailing space, i.e. only `--enable-debuginfo`).

- [ ] **Step 5: Run the gauntlet on the unmodified tree**

Run: `limactl shell lethe -- bash ~/lethe/vm/gauntlet.sh`
Expected: `=== gauntlet: PASS ===` (erl_readpure_test does not exist yet and is skipped). If repro-deadlock.sh's exit code needed fixing, re-run to confirm.

- [ ] **Step 6: Record the baseline**

Run: `limactl shell lethe -- bash ~/lethe/vm/perf-fio.sh`
Create `vm/perf-log.md` with this structure and paste the real output:

```markdown
# Lethe perf log

Non-debug builds (`vm/sync-build.sh perf`), fio 128k, 256M/job, 30s,
256M ARC, file vdev in the lethe Lima VM. Numbers are for
step-over-step comparison on this rig only.

## Baseline (pre-locking-work, commit <hash>)

<perf-fio.sh output>
```

- [ ] **Step 7: Commit**

```bash
git add vm/sync-build.sh vm/gauntlet.sh vm/perf-fio.sh vm/perf-log.md
git commit -m "add perf build mode, gauntlet runner, and fio baseline"
```

---

### Task 2: Read-purity property test (failing)

**Files:**
- Create: `vm/erl_readpure_test.c`

**Interfaces:**
- Consumes: `struct Erl` API from `include/lethe/kht_erl.h` (`erl_new`, `erl_block_write_key`, `erl_block_read_key`, `erl_block_prev_key`, `erl_patch`, `erl_reset`, `erl_serialize`, `erl_drop`), `btreeset_len`, `vec_len`.
- Produces: `vm/erl_readpure_test.c`, auto-picked-up by `vm/gauntlet.sh`. Exit 0 = reads are pure.

- [ ] **Step 1: Write the test**

```c
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
```

- [ ] **Step 2: Run it, verify it FAILS on current code**

Run: `limactl shell lethe -- bash -c 'bash ~/lethe/vm/sync-build.sh sync && cd ~/lethe-build && gcc -O1 -g -I include -o /tmp/erl_readpure_test ~/lethe/vm/erl_readpure_test.c module/zfs/kht_*.c module/zfs/btree*.c module/zfs/str.c module/zfs/speck.c module/zfs/sha.c module/zfs/hex.c && /tmp/erl_readpure_test'`
Expected: FAIL lines in scenario A ("forest extent stable" -- the consolidated-branch `leaves` clamp fires because consolidate leaves `leaves` at 0 after the first all-modified patch) and scenario B ("forest roots stable" / "serialized bytes stable" -- out-of-extent reads `khf_append` junk roots). Scenario C passes (duplicate `btreeset_insert` is contains-first). Exit code 1.

- [ ] **Step 3: Commit the failing test**

```bash
git add vm/erl_readpure_test.c
git commit -m "add read-purity property test (fails on current code)"
```

---

### Task 3: KHT library read purification

**Files:**
- Modify: `module/zfs/kht_forest.c:94-132` (`khf_node_key`)
- Modify: `module/zfs/kht_erl.c:69-75` (`erl_block_read_key`)

**Interfaces:**
- Consumes: nothing new.
- Produces: `khf_node_key`/`khf_leaf_key` are const (mutate nothing; out-of-extent or invariant-breaking queries return a random key so upstream MAC checks fail loudly). `erl_block_read_key` derives marked-block keys via `kht_leaf_key` directly (no re-mark). Signatures unchanged. Callers verified: only `erl_block_read_key`, `erl_block_prev_key`, `vm/khf_test.c`.

- [ ] **Step 1: Make khf_node_key const**

Replace `khf_node_key` in `module/zfs/kht_forest.c` with:

```c
struct KhtKey khf_node_key(struct Khf *self, struct KhtPos *n) {
    if (khf_is_consolidated(self)) {
        return khf_derive_key(self, &self->roots[0], n);
    }

    // A query beyond the forest extent is a block this forest never
    // covered. Reads must NOT extend the forest (appending here is what
    // historically corrupted the sorted-roots invariant); return a
    // random key so the caller's MAC check fails loudly instead.
    if (self->leaves < khtshape_end(&self->shape, n)) {
        return khtkey_new();
    }

    uint64_t size = vec_len(&self->roots);
    uint64_t left = 0;
    uint64_t right = size;
    uint64_t index = 0;
    bool found = false;

    while (left < right) {
        uint64_t mid = left + size / 2;
        struct KhtRoot *root = &self->roots[mid];

        if (khtshape_is_ancestor(&self->shape, &root->pos, n)) {
            index = mid;
            found = true;
            break;
        } else if (khtshape_end(&self->shape, &root->pos) <= khtshape_start(&self->shape, n)) {
            left = mid + 1;
        } else {
            right = mid;
        }

        size = right - left;
    }

    // An in-extent query with no covering root means the sorted-roots
    // invariant broke; deriving from roots[0] would silently produce a
    // wrong key.
    if (!found) {
        return khtkey_new();
    }

    return khf_derive_key(self, &self->roots[index], n);
}
```

(This deletes both `self->leaves = max(...)` mutations, the read-side `khf_append`, and the silent roots[0] fallback. The multi-line comment about the min/max bug moves with the deletion -- keep its history in the git log, not the file.)

Spec note: the design doc says the consolidated-forest extent is "primed at deserialize/creation time" so reads never repair it. No new priming site is needed: the consolidated branch above no longer consults `leaves` at all (derivation from roots[0] is position-based), and the fold paths already prime the extent (`erl_overwrite` primes from `erl->blocks`; serialized forests carry `leaves`). Same guarantee, fewer moving parts.

- [ ] **Step 2: Make marked-block reads bypass the write path**

Replace `erl_block_read_key` in `module/zfs/kht_erl.c` with:

```c
struct KhtKey erl_block_read_key(struct Erl *self, uint64_t block) {
    if (btreeset_contains(&self->modified, block)) {
        // Marked this epoch: the current tree key. Derive directly --
        // reads must not touch the modified set or block count.
        return kht_leaf_key(&self->tree, block);
    } else {
        return khf_leaf_key(&self->forest, block);
    }
}
```

- [ ] **Step 3: Run the read-purity test, verify it PASSES**

Same command as Task 2 Step 2.
Expected: all `ok:` lines, `PASS`, exit 0.

- [ ] **Step 4: Run the other userspace harnesses**

Run (in `~/lethe-build` in the VM, after `sync-build.sh sync`): compile and run `erl_harness`, `khf_test`, `erl_delete_test` with the same gcc pattern (swap the vm/ source file).
Expected: all pass. `erl_harness` is the important one -- it checks read keys reproduce written keys across patch epochs, which guards against the purification changing derivation results.

- [ ] **Step 5: Commit**

```bash
git add module/zfs/kht_forest.c module/zfs/kht_erl.c
git commit -m "make forest key derivation const on the read path"
```

---

### Task 4: lethe.c read purification, capture skip, dead code removal

**Files:**
- Modify: `module/zfs/lethe.c` (`__lethe_block_key` :1514-1575, `__lethe_capture_master_erlstore` :711-764, delete `lethe_block_read_key` :1376-1403 and `lethe_block_write_key` :1405-1434)
- Modify: `include/lethe/lethe.h` (drop the two dead declarations)

**Interfaces:**
- Consumes: Task 3's const derivation.
- Produces: `__lethe_block_key(spa, read, objset, object, block)` -- read side performs lookup + derive only (returns a random key on missing ERL); write side is the only creator/marker. `lethe_bookmark_key`/`lethe_bookmark_prev_key` signatures unchanged. Capture skips masters with empty modified sets.

- [ ] **Step 1: Rewrite __lethe_block_key**

```c
struct KhtKey __lethe_block_key(
	spa_t *spa,
	boolean_t read,
	uint64_t objset,
	uint64_t object,
	uint64_t block
) {
	// Eager import loading (__lethe_load_all_erls) guarantees every
	// mapped ERL is resident, so the hot path never consults the
	// nvlist maps and never loads (no DMU I/O under the lethe locks,
	// by construction).
	ASSERT(!__lethe_object_erlmap_contains(spa, objset, object) ||
	    __lethe_contains_object_erl(spa, objset, object));

	// Reads are pure: look up and derive, touching nothing. A missing
	// ERL means lethe never keyed this block (or purged it); the
	// random key makes the upstream MAC check fail loudly without
	// allocating read-side state.
	if (read) {
		struct Erl *erl = __lethe_get_object_erl(spa, objset, object);
		if (erl == NULL) {
			return khtkey_new();
		}
		return erl_block_read_key(erl, block);
	}

	// Writes create state on first touch.
	if (!__lethe_contains_object_erl(spa, objset, object)) {
		__lethe_insert_object_erl(
			spa,
			objset,
			object,
			erl_new(DEFAULT_FANOUTS, DEFAULT_FANOUTS_LEN)
		);
	}
	if (!__lethe_contains_master_erl(spa, objset)) {
		__lethe_insert_master_erl(
			spa,
			objset,
			erl_new(DEFAULT_FANOUTS, DEFAULT_FANOUTS_LEN)
		);
	}

	struct Erl *erl = __lethe_get_object_erl(spa, objset, object);
	struct Erl *master_erl = __lethe_get_master_erl(spa, objset);

	// Mark the object as modified so its ERL is captured this epoch.
	erl_mark_block(master_erl, object);
	spa->lethe_epoch_dirty = B_TRUE;

	return erl_block_write_key(erl, block);
}
```

(This removes the `__lethe_load_object_erl`/`__lethe_load_master_erl` lazy-load calls -- the only remaining callers of those functions are the eager-load path `__lethe_load_all_erls`; verify with `grep -n "__lethe_load_object_erl\|__lethe_load_master_erl" module/zfs/lethe.c` that the loaders are still called from there and delete nothing else.)

- [ ] **Step 2: Skip clean masters in capture**

In `__lethe_capture_master_erlstore`, at the top of the `while (btreemapiter_next(...))` loop body, add:

```c
		// A master with no modified objects had no object ERLs
		// captured this epoch: nothing below it changed, its slot
		// keys are all still derivable, skip the rewrite.
		if (btreeset_is_empty(&master_erl->modified)) {
			continue;
		}
```

- [ ] **Step 3: Delete dead entry points**

Delete `lethe_block_read_key` and `lethe_block_write_key` from `module/zfs/lethe.c` and their declarations from `include/lethe/lethe.h`. Verify no callers: `grep -rn "lethe_block_read_key\|lethe_block_write_key" module/ include/ lib/` (expect only the definitions you are deleting; the znode/vnops hooks that used them are already commented out -- if a commented-out reference remains, leave it, it does not compile).

- [ ] **Step 4: Build the kernel module**

Run: `limactl shell lethe -- bash ~/lethe/vm/sync-build.sh`
Expected: clean build, no warnings about unused `__lethe_load_object_erl` (it still has the eager-load caller).

- [ ] **Step 5: Commit**

```bash
git add module/zfs/lethe.c include/lethe/lethe.h
git commit -m "purify the read path: no marking, no creation, no lazy load"
```

---

### Task 5: Read-only-workload kernel test, gauntlet, Step-0 measurement

**Files:**
- Create: `vm/test-readonly.sh`
- Modify: `vm/perf-log.md` (append Step 0 numbers)

**Interfaces:**
- Consumes: Task 4's purified read path; debug-build `lethe_info` logging (capture functions log with `__func__`, so `__lethe_capture` greps work).
- Produces: `vm/test-readonly.sh` (exit 0 = read-only workload leaves the epoch clean), auto-included in the gauntlet.

- [ ] **Step 1: Write vm/test-readonly.sh**

```bash
#!/usr/bin/env bash
# Run INSIDE the lethe VM (DEBUG build): a read-only workload must leave
# the lethe epoch clean -- no captures, no ERL rewrites, and identical
# file contents. Detects regressions of read-path purity at the system
# level (mark-on-read would show up as __lethe_capture activity).
set -uo pipefail

BUILD="${LETHE_BUILD:-$HOME/lethe-build}"
POOL=lethetest
IMG=/var/tmp/lethe-pool.img
DS=$POOL/enc
FAILS=0

check() {
	if [ "$1" -eq 0 ]; then echo "ok: $2"; else echo "FAIL: $2"; FAILS=$((FAILS+1)); fi
}

cd "$BUILD"
sudo ./zpool destroy $POOL 2>/dev/null
sudo ./scripts/zfs.sh -u 2>/dev/null
sudo ./scripts/zfs.sh
sudo rm -f "$IMG" && truncate -s 4G "$IMG"
sudo ./zpool create -f $POOL "$IMG"
echo "lethe-passphrase" | sudo ./zfs create -o encryption=on \
	-o keyformat=passphrase -o keylocation=prompt -o atime=off $DS
MNT=$(sudo ./zfs get -H -o value mountpoint $DS)

for f in a b c d; do
	sudo dd if=/dev/urandom of="$MNT/$f" bs=1M count=4 status=none
done
sudo ./zpool sync $POOL
M1=$(sudo md5sum "$MNT"/a "$MNT"/b "$MNT"/c "$MNT"/d | md5sum | cut -d' ' -f1)

# Reimport so reads decrypt from disk instead of the ARC, then let any
# import/mount-time writes settle before arming the log check.
sudo ./zpool export $POOL
sudo ./zpool import -d /var/tmp $POOL
echo "lethe-passphrase" | sudo ./zfs load-key -a
sudo ./zfs mount -a
sudo ./zpool sync $POOL
sudo dmesg -C

M2=$(sudo md5sum "$MNT"/a "$MNT"/b "$MNT"/c "$MNT"/d | md5sum | cut -d' ' -f1)
sudo ./zpool sync $POOL
sudo ./zpool sync $POOL

[ "$M2" = "$M1" ]; check $? "file contents intact after reimport"
CAPTURES=$(sudo dmesg | grep -c "__lethe_capture")
[ "$CAPTURES" -eq 0 ]; check $? "no captures after read-only workload (saw $CAPTURES)"

sudo ./zpool destroy $POOL
echo "=== $([ $FAILS -eq 0 ] && echo PASS || echo "FAIL ($FAILS)") ==="
exit $FAILS
```

- [ ] **Step 2: Run it**

Run: `limactl shell lethe -- bash -c 'bash ~/lethe/vm/sync-build.sh && bash ~/lethe/vm/test-readonly.sh'`
Expected: `=== PASS ===`. (Before Task 4 this test would fail its capture check -- if you want the TDD datapoint, stash Task 4, run, unstash.)

- [ ] **Step 3: Run the full gauntlet**

Run: `limactl shell lethe -- bash ~/lethe/vm/gauntlet.sh`
Expected: `=== gauntlet: PASS ===` -- now includes erl_readpure_test and test-readonly.sh.

- [ ] **Step 4: Measure and log**

Run: `limactl shell lethe -- bash -c 'bash ~/lethe/vm/sync-build.sh perf && bash ~/lethe/vm/perf-fio.sh'`
Append to `vm/perf-log.md` under `## Step 0: read purification (commit <hash>)`. Expected signal: randread numbers improve or hold; the big win is qualitative -- read-only epochs no longer rewrite every touched ERL.

- [ ] **Step 5: Commit**

```bash
git add vm/test-readonly.sh vm/perf-log.md
git commit -m "add read-only-workload test and step-0 perf numbers"
```

---

### Task 6: Step A -- collapse five rwlocks into one, readers share

**Files:**
- Modify: `include/sys/spa_impl.h:461-495` (replace five `krwlock_t` fields with one)
- Modify: `module/zfs/lethe.c` (every `lethe_rw_enter`/`lethe_rw_exit` site: lines ~35-92 init/fini, 107-153 setup, 158-183 load, 517-557 + 574-589 sync, 1060-1237 purge functions, and `lethe_bookmark_key`/`lethe_bookmark_prev_key`)
- Modify: `vm/perf-log.md` (append Step A numbers)

**Interfaces:**
- Consumes: Task 4 (read path is pure, so READER mode is safe).
- Produces: single `krwlock_t lethe_lock` in `spa_t`. All existing writer sites take it WRITER; `lethe_bookmark_key` takes READER when `read`, WRITER when `!read`; `lethe_bookmark_prev_key` takes READER. Task 8 renames this field to `lethe_struct_lock` and re-scopes it -- keep the sites greppable (`spa->lethe_lock`).

- [ ] **Step 1: Replace the fields**

In `include/sys/spa_impl.h`, delete the five `krwlock_t lethe_*_lock;` fields and add a single field above the uber ERL block:

```c
	// One lock for all in-memory lethe state (ERL stores, maps, uber).
	// Readers: pure key derivation (decrypt). Writers: everything else.
	krwlock_t lethe_lock;
```

- [ ] **Step 2: Update every site in lethe.c**

Mechanical: each block of five (or three, in `lethe_setup`) `lethe_rw_enter(...)` calls becomes one `lethe_rw_enter(&spa->lethe_lock, RW_WRITER)`; matching exits become one `lethe_rw_exit(&spa->lethe_lock)`. `lethe_init` inits one lock; `lethe_fini` destroys one. The phase-B map re-locks (lines 574-589) become `lethe_rw_enter(&spa->lethe_lock, RW_WRITER)` around each nvlist insert. `__lethe_sync_erlmap`'s READER acquisition (line ~827) becomes `&spa->lethe_lock`, READER.

In `lethe_bookmark_key`, choose the mode from the direction:

```c
	lethe_rw_enter(&spa->lethe_lock, read ? RW_READER : RW_WRITER);
	struct KhtKey key = __lethe_block_key(
		spa,
		read,
		bookmark->zb_objset,
		bookmark->zb_object,
		bookmark->zb_blkid
	);
	lethe_rw_exit(&spa->lethe_lock);
```

`lethe_bookmark_prev_key`: RW_READER (its body is lookup + `erl_block_prev_key`, pure after Task 3).

- [ ] **Step 3: Verify no stragglers**

Run: `grep -n "erlstore_lock\|erlmap_lock\|uber_erl_lock" module/ include/ lib/ -r`
Expected: no hits.

- [ ] **Step 4: Build + gauntlet**

Run: `limactl shell lethe -- bash -c 'bash ~/lethe/vm/sync-build.sh && bash ~/lethe/vm/gauntlet.sh'`
Expected: clean build, `=== gauntlet: PASS ===`. The deadlock reproducer matters most here: reader/writer mixing on one lock must survive the full 120s fio run.

- [ ] **Step 5: Measure and log**

Run perf build + `vm/perf-fio.sh`; append to `vm/perf-log.md` under `## Step A: single reader-shared rwlock (commit <hash>)`. Expected signal: randread scales with jobs where the baseline was flat.

- [ ] **Step 6: Commit**

```bash
git add include/sys/spa_impl.h module/zfs/lethe.c vm/perf-log.md
git commit -m "collapse the five lethe locks into one; decrypts take it shared"
```

---

### Task 7: ErlBox and pointer-valued containers

**Files:**
- Create: `include/lethe/kht_erlbox.h`
- Create: `module/zfs/kht_erlbox.c`
- Modify: `include/lethe/btreemap_node.h:15-16` (VAL type), `module/zfs/btreemap_node.c:26,265` (value drops)
- Modify: `module/Kbuild.in`, `lib/libzpool/Makefile.am` (add kht_erlbox.c, matching how kht_erl.c is listed)
- Modify: build-command comment lines in `vm/erl_harness.c`, `vm/erl_minimize.c`, `vm/khf_test.c`, `vm/erl_delete_test.c`, `vm/erl_readpure_test.c` (the `kht_*.c` glob already picks up the new file -- only update comments if they enumerate files explicitly)

**Interfaces:**
- Consumes: `struct Erl` (kht_erl.h).
- Produces:

```c
// include/lethe/kht_erlbox.h
struct ErlBox {
    struct Erl erl;
    lethe_mutex_t lock;    // protects erl contents; see locking design
};
struct ErlBox *erlbox_new(struct Erl erl);   // takes ownership of erl
void erlbox_drop(struct ErlBox *self);        // erl_drop + mutex destroy + free
```

`lethe_mutex_t` + `lethe_mutex_init/enter/exit/destroy`: kernel = `kmutex_t`/`mutex_*` (SPL), userspace = `pthread_mutex_t`/`pthread_mutex_*`. `BTREEMAP_VAL_TYPE` becomes `struct ErlBox *` -- `btreemap_get` now returns `struct ErlBox **` (NULL if absent), `btreemap_insert` takes an `ErlBox *`, `btreemapiter_next` yields `struct ErlBox **`. The containers still own their values: node drop/delete call `erlbox_drop` on stored pointers.

- [ ] **Step 1: Write the header**

```c
// include/lethe/kht_erlbox.h
#pragma once

#ifdef __KERNEL__
    #include <lethe/kht_erl.h>
    #include <sys/mutex.h>

    typedef kmutex_t lethe_mutex_t;
    #define lethe_mutex_init(m)    mutex_init((m), NULL, MUTEX_DEFAULT, NULL)
    #define lethe_mutex_destroy(m) mutex_destroy(m)
    #define lethe_mutex_enter(m)   mutex_enter(m)
    #define lethe_mutex_exit(m)    mutex_exit(m)
#else
    #include <lethe/kht_erl.h>
    #include <pthread.h>

    typedef pthread_mutex_t lethe_mutex_t;
    #define lethe_mutex_init(m)    pthread_mutex_init((m), NULL)
    #define lethe_mutex_destroy(m) pthread_mutex_destroy(m)
    #define lethe_mutex_enter(m)   pthread_mutex_lock(m)
    #define lethe_mutex_exit(m)    pthread_mutex_unlock(m)
#endif

// A heap-allocated ERL with a stable address and its own content lock.
// Containers store ErlBox pointers: node splits move the pointer, never
// the box, so the embedded mutex is safe and outstanding references
// survive container churn (under the structure lock's protection).
struct ErlBox {
    struct Erl erl;
    lethe_mutex_t lock;
};

struct ErlBox *erlbox_new(struct Erl erl);

void erlbox_drop(struct ErlBox *self);
```

Note for the kernel branch: `module/zfs/lethe.c` and the other lethe library files build in ZFS kernel-module context where the SPL's `<sys/mutex.h>` is on the include path. If the standalone kernel build of `kht_erlbox.c` cannot see it, match the include pattern used at the top of `module/zfs/hashmap.c` (which already includes kernel headers conditionally).

- [ ] **Step 2: Write kht_erlbox.c**

```c
// module/zfs/kht_erlbox.c
#ifdef __KERNEL__
    #include <lethe/kht_erlbox.h>
    #include <linux/vmalloc.h>
#else
    #include <lethe/kht_erlbox.h>
    #include <stdlib.h>
#endif

struct ErlBox *erlbox_new(struct Erl erl) {
#ifdef __KERNEL__
    struct ErlBox *self = vmalloc(sizeof(*self));
#else
    struct ErlBox *self = malloc(sizeof(*self));
#endif
    self->erl = erl;
    lethe_mutex_init(&self->lock);
    return self;
}

void erlbox_drop(struct ErlBox *self) {
    lethe_mutex_destroy(&self->lock);
    erl_drop(&self->erl);
#ifdef __KERNEL__
    vfree(self);
#else
    free(self);
#endif
}

#ifdef __KERNEL__
EXPORT_SYMBOL(erlbox_new);
EXPORT_SYMBOL(erlbox_drop);
#endif
```

(vmalloc matches the library's existing allocator convention -- vec and hashmap use it. `erlbox_new` runs on the write slow path under `spl_fstrans_mark`, so vmalloc's sleeping is safe there.)

- [ ] **Step 3: Switch the btreemap value type**

`include/lethe/btreemap_node.h`: add `#include <lethe/kht_erlbox.h>` to both branches of the `#ifdef __KERNEL__` include block and change:

```c
#define BTREEMAP_VAL_TYPE struct ErlBox *
```

`module/zfs/btreemap_node.c`: the two value-drop sites become box drops --
line 26 (`btreemapnode_drop`): `erl_drop(&self->vals[i]);` -> `erlbox_drop(self->vals[i]);`
line ~265 (delete path): `erl_drop(&val);` -> `erlbox_drop(val);`
Then `grep -n "erl_drop" module/zfs/btreemap_node.c module/zfs/btreemap.c module/zfs/btreemap_iter.c` and convert any remaining value-type drop the grep reveals (expect none in btreemap.c/btreemap_iter.c, but verify).

- [ ] **Step 4: Wire the new file into both builds**

Add `kht_erlbox.c` next to `kht_erl.c` in `module/Kbuild.in` and `lib/libzpool/Makefile.am` (grep for `kht_erl` in each to find the list format).

- [ ] **Step 5: Fix the kernel-side compile fallout in lethe.c (minimal shim)**

This task only makes the tree compile again; the real restructure is Task 8. Update the helpers to box at the boundary:

- `__lethe_get_object_erl` / `__lethe_get_master_erl` return `struct ErlBox *` (deref the `ErlBox **` from `btreemap_get`, NULL-safe).
- `__lethe_insert_object_erl(spa, objset, object, struct Erl erl)` wraps: `btreemap_insert(erlstore, object, erlbox_new(erl));` (same for `__lethe_insert_master_erl`).
- Every `struct Erl *erl = __lethe_get_...` consumer dereferences the box: `&box->erl`. Sites: `__lethe_block_key`, `lethe_bookmark_prev_key`, `__lethe_capture_object_erlstore` (its NULL-skip now checks the box), `__lethe_capture_master_erlstore` (iterator now yields `struct ErlBox **`), `lethe_object_free`, `lethe_object_free_range`, `lethe_objset_destroy`, `__lethe_load_object_erl`, `__lethe_load_master_erl`.
- Do NOT add any `lethe_mutex_enter` calls yet -- the single `lethe_lock` still serializes everything exactly as in Step A.

- [ ] **Step 6: Build userspace harnesses + kernel, run gauntlet**

Run: `limactl shell lethe -- bash -c 'bash ~/lethe/vm/sync-build.sh && bash ~/lethe/vm/gauntlet.sh'`
Expected: harnesses compile (the `kht_*.c` glob picks up kht_erlbox.c; pthread needs no extra flag for mutexes on glibc) and pass; kernel builds; `=== gauntlet: PASS ===`.

- [ ] **Step 7: Commit**

```bash
git add include/lethe/kht_erlbox.h module/zfs/kht_erlbox.c \
    include/lethe/btreemap_node.h module/zfs/btreemap_node.c \
    module/Kbuild.in lib/libzpool/Makefile.am module/zfs/lethe.c
git commit -m "box erls behind stable pointers with embedded content locks"
```

---

### Task 8: Step C -- structure lock + per-ERL mutexes

**Files:**
- Modify: `include/sys/spa_impl.h` (rename `lethe_lock` -> `lethe_struct_lock`, add `kmutex_t lethe_uber_lock`, `lethe_epoch_dirty` -> `volatile uint32_t`)
- Modify: `module/zfs/lethe.c` (hot path, sync phase A, purge functions, init/fini)
- Modify: `include/lethe/lethe.h` (helper signatures that now traffic in `struct ErlBox *`)

**Interfaces:**
- Consumes: Task 7's ErlBox helpers.
- Produces: final locking regime -- `lethe_struct_lock` READER for all derivations and sync capture, WRITER for create/purge/load; box mutexes for ERL contents; `lethe_uber_lock` for the uber ERL. Lock order: struct(R) -> object box -> master box -> uber. `lethe_epoch_dirty` accessed with `atomic_swap_32` (clear-and-test in sync) and plain store (set).

- [ ] **Step 1: spa_impl.h changes**

```c
	// Structure lock: READER = any key derivation or sync capture
	// (content changes go through per-ErlBox mutexes); WRITER = anything
	// that adds or removes ERLs (first-touch create, purge, import load).
	// Lock order: lethe_struct_lock(R) -> object box -> master box ->
	// lethe_uber_lock.
	krwlock_t lethe_struct_lock;
	kmutex_t lethe_uber_lock;               // Content lock for uber ERL.
	volatile uint32_t lethe_epoch_dirty;    // Epoch modifications to sync?
```

(`lethe_epoch_dirty` moves from `boolean_t` at line 463; delete the old field. Rename every `spa->lethe_lock` use; init/destroy `lethe_uber_lock` in `lethe_init`/`lethe_fini`.)

- [ ] **Step 2: Rewrite the hot path**

`lethe_bookmark_key` body (fstrans mark/unmark unchanged around it):

```c
	struct KhtKey key;
	uint64_t objset = bookmark->zb_objset;
	uint64_t object = bookmark->zb_object;
	uint64_t block = bookmark->zb_blkid;

retry:
	lethe_rw_enter(&spa->lethe_struct_lock, RW_READER);

	struct ErlBox *box = __lethe_get_object_erl(spa, objset, object);
	if (box == NULL) {
		lethe_rw_exit(&spa->lethe_struct_lock);
		if (read) {
			// Never keyed by lethe (or purged): random key makes
			// the upstream MAC check fail loudly.
			spl_fstrans_unmark(cookie);
			return khtkey_new();
		}
		// First write to this object: create its ERL (and its
		// objset's master ERL) under the structure lock, then retry.
		lethe_rw_enter(&spa->lethe_struct_lock, RW_WRITER);
		if (!__lethe_contains_object_erl(spa, objset, object)) {
			__lethe_insert_object_erl(
				spa,
				objset,
				object,
				erl_new(DEFAULT_FANOUTS, DEFAULT_FANOUTS_LEN)
			);
		}
		if (!__lethe_contains_master_erl(spa, objset)) {
			__lethe_insert_master_erl(
				spa,
				objset,
				erl_new(DEFAULT_FANOUTS, DEFAULT_FANOUTS_LEN)
			);
		}
		lethe_rw_exit(&spa->lethe_struct_lock);
		goto retry;
	}

	lethe_mutex_enter(&box->lock);
	key = read ? erl_block_read_key(&box->erl, block)
	           : erl_block_write_key(&box->erl, block);
	lethe_mutex_exit(&box->lock);

	if (!read) {
		// Object box -> master box is the sanctioned order.
		struct ErlBox *master = __lethe_get_master_erl(spa, objset);
		lethe_mutex_enter(&master->lock);
		erl_mark_block(&master->erl, object);
		lethe_mutex_exit(&master->lock);
		spa->lethe_epoch_dirty = 1;
	}

	lethe_rw_exit(&spa->lethe_struct_lock);
```

(`__lethe_block_key` folds into this and is deleted; `lethe_bookmark_prev_key` gets the same shape: READER, lookup, box mutex around `erl_block_prev_key`, no slow path. The ASSERT from Task 4 moves here, before the NULL check.)

- [ ] **Step 3: Rewrite lethe_sync phase A**

Replace the five-lock block (lines ~514-557) with:

```c
	// Phase A runs under the structure lock as READER: capture only
	// mutates ERL *contents* (through box mutexes), so concurrent
	// derivations on untouched ERLs proceed. Clear the dirty flag
	// FIRST: a write that lands mid-capture re-sets it and simply gets
	// captured next epoch (its marks survive in the modified sets).
	if (atomic_swap_32(&spa->lethe_epoch_dirty, 0) == 0) {
		vec_drop(&entries);
		vec_drop(&uber_bytes);
		return;
	}

	lethe_rw_enter(&spa->lethe_struct_lock, RW_READER);
	__lethe_capture_object_erlstore(spa, &entries);
	__lethe_capture_master_erlstore(spa, &entries);

	mutex_enter(&spa->lethe_uber_lock);
	VERIFY(spa->lethe_uber_erl_object != 0);
	erl_patch(&spa->lethe_uber_erl);
	erl_reset(&spa->lethe_uber_erl);
	uber_bytes = erl_serialize(&spa->lethe_uber_erl);
	mutex_exit(&spa->lethe_uber_lock);
	lethe_rw_exit(&spa->lethe_struct_lock);
```

Inside `__lethe_capture_object_erlstore`, per master: take the master box mutex, snapshot the modified set into a local `vec(uint64_t)` (btreeset_iter + vec_push), release the master mutex, then for each snapshotted object: look up its box (NULL -> purged -> skip), `lethe_mutex_enter(&box->lock)`, patch + reset, then (still holding the object mutex, per the object->master order) take the master box mutex to derive `erl_block_write_key(&master->erl, object)`, release master, `erl_serialize_keyed`, release object mutex, push the entry. `__lethe_capture_master_erlstore` similarly wraps each master's patch/derive in its box mutex and takes `lethe_uber_lock` for the uber-derived write key (`__lethe_master_erl_write_key` body: uber mutex around `erl_block_write_key(&spa->lethe_uber_erl, objset)`). Phase B is untouched except the map re-locks use `lethe_struct_lock` WRITER.

- [ ] **Step 4: Purge functions and loaders**

`lethe_object_free`, `lethe_object_free_range`, `lethe_objset_destroy`: single `lethe_rw_enter(&spa->lethe_struct_lock, RW_WRITER)` / exit around the existing bodies (they were converted to one lock in Task 6 -- only the field rename lands here). WRITER excludes all derivations, so container removes may free boxes with no further ceremony; still take the master box mutex around its `erl_mark_block` for uniformity. `lethe_object_free_range` marks a *content* range on one object ERL -- it can take READER + the object box mutex instead of WRITER (it removes nothing structural); do that, and mark the master under its own mutex. `lethe_load`/`__lethe_load_all_erls`: WRITER. `lethe_setup`: WRITER + uber mutex where it touches the uber ERL.

- [ ] **Step 5: Update declarations**

`include/lethe/lethe.h`: update every helper that now takes/returns `struct ErlBox *` (`__lethe_get_object_erl`, `__lethe_get_master_erl`) and delete `__lethe_block_key` if it was declared. Add `#include <lethe/kht_erlbox.h>`.

- [ ] **Step 6: Build + gauntlet**

Run: `limactl shell lethe -- bash -c 'bash ~/lethe/vm/sync-build.sh && bash ~/lethe/vm/gauntlet.sh'`
Expected: `=== gauntlet: PASS ===`. Watch dmesg during test-delete.sh for lock-order or use-after-free oopses (the purge-vs-capture interaction is the risky seam; a hang here means a lock-order violation -- get stacks via `limactl shell lethe -- sudo dmesg | tail -100` after khungtaskd fires).

- [ ] **Step 7: Commit**

```bash
git add include/sys/spa_impl.h module/zfs/lethe.c include/lethe/lethe.h
git commit -m "scope the structure lock to shape; per-erl mutexes for content"
```

---

### Task 9: Churn stress test, final gauntlet, Step-C measurement

**Files:**
- Create: `vm/test-churn.sh`
- Modify: `vm/perf-log.md` (append Step C numbers)
- Modify: `vm/deadlock-evidence.md` (append a dated note that the locking work landed, mirroring the existing update style)

**Interfaces:**
- Consumes: everything prior.
- Produces: `vm/test-churn.sh` (exit 0 = reads survive create/delete churn with intact data and a clean pool), auto-included in the gauntlet.

- [ ] **Step 1: Write vm/test-churn.sh**

```bash
#!/usr/bin/env bash
# Run INSIDE the lethe VM (DEBUG build): fio reads racing a create/rm
# churn loop for 60s. Tortures the structure-lock WRITER/READER handoff
# (first-touch creates + purges vs in-flight derivations) and ErlBox
# free lifetimes.
set -uo pipefail

BUILD="${LETHE_BUILD:-$HOME/lethe-build}"
POOL=lethetest
IMG=/var/tmp/lethe-pool.img
DS=$POOL/enc
FAILS=0

check() {
	if [ "$1" -eq 0 ]; then echo "ok: $2"; else echo "FAIL: $2"; FAILS=$((FAILS+1)); fi
}

cd "$BUILD"
sudo ./zpool destroy $POOL 2>/dev/null
sudo ./scripts/zfs.sh -u 2>/dev/null
sudo ./scripts/zfs.sh
echo 1 | sudo tee /sys/module/zfs/parameters/zfs_delete_inode >/dev/null
echo 1 | sudo tee /sys/module/zfs/parameters/zfs_delete_dentry >/dev/null
sudo rm -f "$IMG" && truncate -s 4G "$IMG"
sudo ./zpool create -f $POOL "$IMG"
echo "lethe-passphrase" | sudo ./zfs create -o encryption=on \
	-o keyformat=passphrase -o keylocation=prompt -o atime=off $DS
MNT=$(sudo ./zfs get -H -o value mountpoint $DS)

sudo dd if=/dev/urandom of="$MNT/control" bs=1M count=8 status=none
sudo ./zpool sync $POOL
C1=$(sudo md5sum "$MNT/control" | cut -d' ' -f1)
sudo dmesg -C

sudo fio --name=churnread --directory="$MNT" --rw=randread --bs=128k \
	--size=64M --numjobs=4 --time_based --runtime=60 \
	--group_reporting >/tmp/churn-fio.log 2>&1 &
FIO=$!

END=$((SECONDS + 60))
i=0
while [ $SECONDS -lt $END ]; do
	sudo dd if=/dev/urandom of="$MNT/churn$i" bs=1M count=1 status=none
	[ $i -gt 0 ] && sudo rm -f "$MNT/churn$((i - 1))"
	i=$((i + 1))
done

wait $FIO
check $? "fio exited cleanly under churn ($i churn cycles)"
grep " err= 0" /tmp/churn-fio.log >/dev/null
check $? "fio reported err=0"

sudo dmesg | grep -iE "oops|kernel bug|general protection|page fault" >/dev/null
[ $? -ne 0 ]; check $? "no kernel splats in dmesg"

[ "$(sudo md5sum "$MNT/control" | cut -d' ' -f1)" = "$C1" ]
check $? "control file intact after churn"

sudo ./zpool sync $POOL
sudo ./zpool scrub -w $POOL
sudo ./zpool status $POOL | grep "with 0 errors" >/dev/null
check $? "scrub clean"

sudo ./zpool destroy $POOL
echo "=== $([ $FAILS -eq 0 ] && echo PASS || echo "FAIL ($FAILS)") ==="
exit $FAILS
```

- [ ] **Step 2: Run it (several times)**

Run: `limactl shell lethe -- bash ~/lethe/vm/test-churn.sh` -- three consecutive runs.
Expected: `=== PASS ===` every time. Race bugs are probabilistic; three clean 60s runs with hundreds of create/purge cycles each is the acceptance bar.

- [ ] **Step 3: Full gauntlet**

Run: `limactl shell lethe -- bash ~/lethe/vm/gauntlet.sh`
Expected: `=== gauntlet: PASS ===` with all seven components (four harnesses + five kernel scripts as available).

- [ ] **Step 4: Measure, log, close out the evidence doc**

Run perf build + `vm/perf-fio.sh`; append to `vm/perf-log.md` under `## Step C: per-erl locking (commit <hash>)`. Expected signal: randrw write-side scaling improves vs Step A (writers no longer exclude each other pool-wide). Add a dated paragraph to `vm/deadlock-evidence.md` noting the locking-granularity work (pattern: the existing "Update 2026-08-14 (later)" paragraph), including one sentence of before/after numbers from the perf log.

- [ ] **Step 5: Commit**

```bash
git add vm/test-churn.sh vm/perf-log.md vm/deadlock-evidence.md
git commit -m "add churn stress test and step-c perf numbers"
```
