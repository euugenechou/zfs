# Lethe locking granularity design (2026-08-14)

Approved plan for removing the global lock funnel from Lethe's block-crypt
hot path. Three steps, each independently landed, gauntlet-validated, and
measured: read purification (Step 0), a single reader-shared rwlock
(Step A), and boxed ERLs with per-ERL mutexes under a structure lock
(Step C). RCU was considered and explicitly rejected (see "Rejected
alternatives").

## Problem

Every encrypt and decrypt of every encrypted block goes through
`lethe_bookmark_key()`, which takes all five SPA-global rwlocks
(`lethe_master_erlstore_lock`, `lethe_object_erlstore_lock`,
`lethe_master_erlmap_lock`, `lethe_object_erlmap_lock`,
`lethe_uber_erl_lock`) as WRITER. The 2026-08-14 deadlock fixes made this
safe but left it fully serialized: all ZIO taskq threads funnel their key
derivations through one critical section. This is the throughput ceiling
YCSB will measure.

## Audit findings the design rests on

1. **The read path mutates today, in four places:**
   - `__lethe_block_key()` calls `erl_mark_block(master_erl, object)` and
     sets `spa->lethe_epoch_dirty` on every derivation, read or write
     (lethe.c:1555-1556).
   - A read of a marked block re-inserts it into the object ERL's
     modified btreeset (`erl_block_read_key` -> `erl_block_write_key`).
   - `khf_node_key()` clamps/grows `self->leaves` and can structurally
     `khf_append()` when the query exceeds the forest extent
     (kht_forest.c:94-131).
   - First touch of an object lazily creates its ERL (and can, in
     principle, lazily *load* it via blocking DMU I/O -- dead in practice
     because of eager import loading, but present in the code).
2. **The pure parts are pure.** Tree-side `kht_leaf_key` is a stack-only
   hash-chain walk (no allocation). Forest-side derivation is a binary
   search over the roots vec plus the same walk. `hashmap_get`,
   `btreemap_get`, `btreeset_contains` are pure lookups.
3. **ERL pointers are not stable.** `struct Erl` is stored by value
   inside btree node vecs (btreemap_node.h); inserts/removes split nodes
   and realloc vecs, invalidating every outstanding `struct Erl *`.
4. **Capture over-serializes.** `__lethe_capture_master_erlstore`
   iterates ALL master ERLs each dirty epoch, patching and rewriting even
   unmodified ones. Combined with mark-on-read, every object that is
   merely read gets its ERL patched, re-encrypted, and rewritten to the
   MOS every txg.
5. **Mark-on-read is removable.** Object ERLs are captured strictly from
   the master's modified set; a read of a marked block implies a writer
   already marked it; an unmarked master slot keeps its forest key
   stable, so a never-written object stays loadable across epochs
   without recapture.
6. `lethe_block_read_key()` / `lethe_block_write_key()` (lethe.c) have no
   callers (leftovers of the commented-out vnops hooks).

## Step 0: read purification

Goal: make read-side key derivation genuinely read-only so later steps
can run it under shared/reader locking.

- `__lethe_block_key()`: move `erl_mark_block(master_erl, object)` and
  `epoch_dirty = B_TRUE` under `if (!read)`.
- Split forest derivation into a const and an extending variant:
  - `khf_node_key()` (const): pure derivation; VERIFYs the query is
    within the forest extent. Used by `erl_block_read_key` (unmarked
    case) and `erl_block_prev_key`.
  - The extending behavior (today's leaves clamp + append) moves to the
    write/fold callers only.
  - The consolidated-forest extent is primed from `erl->blocks` at
    deserialize/creation time (generalizing the erl_overwrite fix), so
    reads never need to repair it.
- Capture: `__lethe_capture_master_erlstore` skips masters whose
  modified set is empty. (Object capture already iterates only modified
  objects; with mark-on-read gone this stops the per-epoch rewrite of
  read-only ERLs entirely. Uber slots rotate only for captured masters,
  which falls out of the existing write-key path.)
- Read-miss semantics change: decrypting a block with no ERL currently
  creates a fresh ERL and derives a garbage key (upstream MAC check
  fails). New behavior: return the zero key without creating anything --
  same observable failure, no read-side allocation. ERLs are created
  only on the write path.
- Hygiene: delete dead `lethe_block_read_key`/`lethe_block_write_key`;
  replace the lazy `__lethe_load_object_erl()` call inside
  `__lethe_block_key()` with a VERIFY (mapped implies resident, by
  eager import loading), making the no-I/O-under-locks invariant hold by
  construction.

## Step A: one rwlock, readers share

The five locks are always taken together in a fixed order -- one logical
lock pretending to be five. Replace all five with a single
`spa->lethe_lock` (krwlock_t):

- READER: `lethe_bookmark_key(read=B_TRUE)`, `lethe_bookmark_prev_key`.
- WRITER: write-side `lethe_bookmark_key`, `lethe_sync` phase A, the
  purge hooks, setup/load.

Mechanical diff; immediately measurable (all z_rd_int threads derive
concurrently); simplifies the code Step C restructures.

## Step C: boxed ERLs, structure lock, per-ERL mutexes

One global rwlock protects the *shape* of the store; per-ERL mutexes
protect *contents*.

### Structures

    struct ErlBox {
        struct Erl erl;
        kmutex_t   lock;    /* protects erl contents */
    };

- Containers store stable pointers: `BTREEMAP_VAL_TYPE` becomes
  `struct ErlBox *` (deep-drop adjusted: delete/remove frees the box).
  Boxing is required even without RCU: a kmutex embedded in a by-value
  element would be memcpy'd by node splits, and content mutations under
  a reader-mode global lock need a lock at a stable address.
- `spa->lethe_lock` from Step A is renamed in role: it becomes the
  structure lock (`lethe_struct_lock`).
- `lethe_epoch_dirty` becomes atomic (set outside any writer section).

### Locking rules

- **Every derivation -- read or write -- holds `lethe_struct_lock` as
  READER for its whole duration.** Readers do not serialize each other;
  holding it pins the containers (no box can appear, move, or be freed
  underneath a derivation).
- **WRITER is taken only by structural updaters:** first-touch ERL/store
  creation, `lethe_object_free`, `lethe_objset_destroy`, import load.
  Rare and short; free-after-remove is trivially safe because WRITER
  excludes all derivations.
- **Lock order: `lethe_struct_lock`(R) -> object box -> master box.**
  Never the reverse. The uber ERL needs no box (it lives by value in
  spa_t and is never moved by a container); it gets a dedicated kmutex
  ordered at the same level as a master box.
- Derivation: lock the object box; read = pure derive; write = mark +
  derive; unlock; then (write only) lock the master box, mark the
  object's slot, unlock.
- Write-side first touch (no box found): drop READER, take WRITER,
  re-check, allocate + insert the box, drop WRITER, retry the fast
  path from the top.
- Purge hooks: WRITER; remove boxes from containers (frees them), mark
  master/uber slots, queue MOS frees exactly as today.

### lethe_sync under the new locking

Phase A runs under READER, not WRITER: it mutates only ERL *contents*.
For each master (skipping clean ones), snapshot the modified set under
the master box mutex, drop it, then for each modified object lock its
box, patch + reset + serialize, and derive the object ERL's write key
from the master (object -> master order holds). Then capture dirty
masters, then the uber ERL. Per-box atomicity is sufficient: the box
mutex serializes any concurrent derivation entirely before or after that
ERL's patch, which is the epoch semantics the system already relies on;
the txg remains the on-disk commit point. Derivations on untouched ERLs
proceed concurrently with capture. Phase B is unchanged (no lethe locks;
purge-queue drain; map and uber writes).

## Testing

Every step lands behind the full existing gauntlet on the debug build:
`vm/repro-deadlock.sh` (fio 120s, err=0), `vm/verify-integrity.sh`
(md5-identical across export/import), `vm/test-delete.sh` (15 checks),
and the userspace harnesses (`erl_harness`, `erl_minimize`, `khf_test`,
`erl_delete_test`).

New tests:

1. **Read-purity property test** (userspace, Step 0): serialize an ERL,
   hammer `erl_block_read_key`/`erl_block_prev_key` across its extent,
   serialize again; the bytes must be identical.
2. **Read-only workload stays clean** (kernel, Step 0): mount, read
   files, `zpool sync` twice; assert no captures ran (epoch stayed
   clean).
3. **Delete-vs-read churn stress** (kernel, Step C): fio reads racing an
   rm/create churn loop; tortures the WRITER/READER handoff and
   box-free lifetimes.

## Measurement

Non-debug build (the debug build's lethe_info logging and ZFS asserts
distort everything). fio randread and randrw scaling curves (1 -> 8
jobs) on an encrypted dataset, recorded in `vm/perf-log.md`:

1. Baseline (current code) -- taken first.
2. After Step 0 (expect: read-only rewrite I/O disappears).
3. After Step A (expect: read scaling across jobs).
4. After Step C (expect: write-side scaling; read path unchanged from A
   except structure-lock hold times).

YCSB proper runs after the curves look sane.

## Rejected alternatives

- **RCU lookups** (rejected 2026-08-14): would remove the single shared
  reader cacheline (rwlock hold counts) and let readers proceed during
  creates/purges. At 8 VM vCPUs, next to per-derivation SHA chains and
  per-block AES, that delta is noise; the price was a custom RCU hash
  table, per-box refcounts, grace-period lifetime reasoning, and
  userspace shims. Revisit only if Step C measurements show reader-side
  contention on the structure lock.
- **Per-objset shard locks without boxing**: no write-side win for
  single-dataset YCSB (one dataset = one shard = one writer lock) and
  the pointer-instability trap remains.
- **Fully lock-free derivation (seqlock/COW)**: derivation under the box
  mutex is a dozen hash invocations; the mutex will not be the
  bottleneck, and COW of forest vecs on every write is strictly worse.

## Out of scope

Unchanged from the project's standing limits: snapshots, clones, dedup,
L2ARC, raw send/recv. Also out of scope here: memory eviction for the
eagerly-loaded ERL store, MOS-object growth, converting deserializer
BUG_ONs to error returns, and hash/container resizing (the existing
containers are kept).
