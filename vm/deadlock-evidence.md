# Lethe deadlock & corruption: evidence and fixes (2026-08-14)

## Final bug inventory (all reproduced, fixed, and re-verified)

1. **Deadlock (taskq starvation).** `lethe_sync()` (txg_sync) and
   `lethe_bookmark_key()` (ZIO taskqs) take five global writer rwlocks and
   performed blocking DMU I/O under them; the lock holder's I/O completion
   needs the same taskq threads that are blocked on the locks. Fixed:
   two-phase `lethe_sync` (capture in memory under locks, write with locks
   dropped), eager ERL loading at import (`__lethe_load_all_erls`), and
   `spl_fstrans_mark` around ZIO-context derivations.
2. **Shared-key race.** `lethe_hijack_dsl_crypto_key()` memcpy'd per-block
   keys into the shared per-dataset `dck` (`zk_current_keydata`), stomped
   concurrently; salt rotation also bypassed the hijack on decrypt. Fixed:
   `key_override` parameter plumbed through `zio_do_crypt_data()`; the dck
   is never mutated.
3. **ZIL / meta-dnode key aliasing.** The ERL block index ignores
   `zb_level`, so ZIL blocks (level -2, object 0) aliased meta-dnode blocks
   (level 0, object 0) and rotated their key slots -> unreadable dnode
   blocks after export/import (traced key-for-key via LKEY logging). Fixed:
   ZIL blocks (`DMU_OT_INTENT_LOG`) are not overridden (they are also
   replayed post-crash when in-memory key state no longer exists).
4. **KHF library: `min`-for-`max` typo (the big one).**
   `khf_node_key()` shrank `self->leaves` to the queried block on every
   lookup; the next lookup then `khf_append()`ed random-keyed junk roots,
   breaking the sorted-roots invariant and the binary search (whose no-match
   fallback is roots[0]) -> mass key corruption (11,575 mismatches in a
   20-epoch userspace harness). One-word fix; harness now passes all seeds.
5. **KHF/ERL extent truncation.** Folding into a consolidated forest with
   `leaves == 0` guessed the extent as the fold's `end`, dropping all
   trailing coverage. Fixed: `erl_overwrite` primes `forest.leaves` from
   `erl->blocks` before folding.
6. **Read-vs-rewrite TOCTOU.** A decrypt of a block's previous version can
   race the writer marking the block into the new epoch. Mitigated: on
   decrypt failure the key is retried once with the previous-epoch (forest)
   key via `lethe_bookmark_prev_key()` — still within Lethe's
   secure-delete boundary (keys die at patch time).
7. **Soft-lockup robustness.** `khf_deserialize`/`btreeset_deserialize`
   looped on garbage counts from wrong-key plaintext (observed as a CPU
   soft-lockup inside `zpool import`). Fixed: length sanity guards that
   fail loudly.
8. **Scrub crash.** Lethe allocated its MOS objects with 16M blocks;
   `dsl_scan_visitbp()` ASSERTs large blocks imply `ds != NULL` (dataset),
   NULL-dereffing on MOS traversal in debug builds — killed txg_sync mid
   scrub. Fixed: `SPA_OLD_MAXBLOCKSIZE` (128K) for ERL objects.

Validation: 8-job fio randrw on an encrypted dataset with 256M ARC runs the
full 120s cleanly (was: deadlock <1 min, then EIO at ~3s); md5 of all 32
files identical across export/import; scrub clean. Userspace invariant
harnesses in `vm/erl_harness.c` / `vm/erl_minimize.c` / `vm/khf_test.c`.

Known remaining limitations: sparse-file folds (ranges beyond the forest
extent) are untested; L2ARC writes (`zio_do_crypt_abd` callers) don't use
Lethe keys — keep L2ARC off; crash recovery is analytically sound
(purge/rotation state commits in the same txg as the data it keys) but
crash-injection testing hasn't been done; snapshots/clones/dedup/raw-send
are unsupported.

Update 2026-08-14 (later): delete-path key purging is implemented and
tested — file delete, truncate, and dataset destroy now rotate the
relevant key slots at the next epoch (see `vm/delete-purge-design.md`,
`vm/erl_delete_test.c`, `vm/test-delete.sh`). Run with
`zfs_delete_inode=1` / `zfs_delete_dentry=1` for rm-time purging.

Update 2026-08-15: the locking-granularity project is done. Step 0
(commits a854b8a49/13c041929) purified the read path (no marking, no
creation, no lazy load on reads) so read-only epochs stop rewriting
every touched ERL. Step A (commit f0226fbe3) collapsed the five global
lethe rwlocks
that caused the deadlock above into one, taken `RW_READER` for decrypt
derivations and `RW_WRITER` for encrypt/write/purge/sync — the 120s
8-job reader/writer-mixing fio deadlock reproducer that used to wedge
now exits clean. Step C (commits 97e80146e/32207f534) went further:
ErlBoxes behind stable pointers with embedded per-box content mutexes,
and the single lock scoped down to just structure changes (`RW_READER`
for derivations, `RW_WRITER` for create/purge/load), so writers no
longer exclude each other pool-wide. Two latent bugs were found and
fixed along the way, both pre-existing and unrelated to this project's
own changes but blocking clean measurement: a btreemap internal-node
delete double-drop (bit-copied predecessor/successor Erls were dropped
twice — commit 816944def) and a `khf_overwrite_keyed` sparse-fold gap
that silently corrupted a real block's key when a new range's start
fell outside every existing root's coverage (commit bcd4723e1). Code
review of the Step C READER-demotion for `lethe_object_free_range`
caught a real capture-atomicity race — phase A's two capture passes
aren't jointly atomic, so a free_range landing between them under
READER could orphan an object's ERL — before it shipped; fixed by
restoring WRITER there (commit 176ef3f53). Final validation: three
consecutive 60s create/delete-vs-read churn runs (`vm/test-churn.sh`,
thousands of churn cycles each) all pass clean, and the full gauntlet
(`vm/gauntlet.sh`) passes end to end. Perf (`vm/perf-log.md`): in each
step's first (uncontended) sample, steady-state randrw bandwidth holds
within a few MiB/s of baseline at jobs=1/2/8, but jobs=4 shows a dip
under stepC (117/119 MiB/s stepA -> 110/112 stepC) that reproduces and
widens on a second, separately-run repeat sample (103/105); that
repeat sample also shows a new jobs=8 dip not seen in any first
sample, coincident with heavy host contention from other VMs running
during that repeat — so the jobs=1/2/8 "holds within noise" claim
above describes only the uncontended first samples, not the contended
repeat. Flagged as an open, unresolved concern rather than a settled
regression, since the structural goal (no more pool-wide writer
exclusion) isn't measurable in this single-threaded-per-job fio shape
anyway.

---

# Original deadlock reproduction evidence (2026-08-14)

Reproduced on the zfs-2.4.3 rebase in the `lethe` Lima VM (Ubuntu 24.04
arm64, kernel 6.8.0-136-generic) with `vm/repro-deadlock.sh`: file-backed
pool, encrypted dataset, `zfs_arc_max=256M`, fio 8-job randrw 50/50.
Wedged in under a minute; all fio jobs in D state, txg permanently stuck.

## The cycle

1. `txg_sync` calls `lethe_sync()` from `spa_sync_iterate_to_convergence`.
   It acquires ALL FIVE global lethe rwlocks as WRITER, then calls
   `dmu_write()` on an ERL object, which must first read in the dbuf:
   it blocks in `zio_wait()` for a read zio.
2. That read zio's completion must be processed by a `z_rd_int` taskq
   worker.
3. Every `z_rd_int` worker is stuck in
   `zio_done -> zio_pop_transforms -> zio_decrypt -> spa_do_crypt_abd ->
   lethe_bookmark_key -> rw_enter(WRITER)` waiting for the lethe locks
   held by `txg_sync`.
4. Circular wait: txg_sync -> z_rd_int availability -> lethe locks ->
   txg_sync. All other I/O queues behind the stuck txg
   (`dmu_tx_wait`/`txg_wait_synced`).

## khungtaskd report (excerpt)

    INFO: task z_rd_int_0:108821 blocked for more than 61 seconds.
     rwsem_down_write_slowpath
     down_write
     lethe_bookmark_key+0xdc/0x4c0 [zfs]
     spa_do_crypt_abd+0x320/0x3a8 [zfs]
     zio_decrypt+0x104/0x510 [zfs]
     zio_pop_transforms+0x6c/0xb8 [zfs]
     zio_done+0x170/0x1808 [zfs]
     zio_execute+0x104/0x238 [zfs]
     taskq_thread+0x294/0x550 [spl]

    INFO: task txg_sync:108854 blocked for more than 61 seconds.
     __cv_timedwait_io
     zio_wait+0x1a0/0x508 [zfs]
     dbuf_read+0x1e4/0x750 [zfs]
     dmu_buf_will_dirty_flags+0xd0/0x358 [zfs]
     dmu_write_impl+0xe0/0x220 [zfs]
     dmu_write+0x104/0x168 [zfs]
     __lethe_sync_erl_object+0xa8/0x188 [zfs]
     __lethe_sync_object_erlstore+0x210/0x438 [zfs]
     lethe_sync+0x1d0/0x700 [zfs]
     spa_sync_iterate_to_convergence+0xec/0x368 [zfs]
     spa_sync+0x234/0x650 [zfs]
     txg_sync_thread+0x24c/0x370 [zfs]

    INFO: task fio:108983 blocked for more than 61 seconds.
     txg_wait_synced_flags
     dmu_tx_wait
     dmu_tx_assign
     zfs_write

## Root-cause summary

`lethe_bookmark_key()` (the only live Lethe hook, called from
`spa_do_crypt_abd` for every encrypted block) and `lethe_sync()` both
take the same five global SPA-wide rwlocks as WRITER, and both perform
blocking DMU I/O (`dmu_read`/`dmu_bonus_hold`/`dmu_write`->`dbuf_read`)
and `vmalloc(GFP_KERNEL)` allocations (lethe vec/str) while holding
them. `lethe_bookmark_key` runs in ZIO pipeline taskq context
(`z_rd_int` via zio_decrypt / arc_hdr_decrypt; `z_wr_iss` via
zio_encrypt during spa_sync), where blocking on the locks starves the
taskq needed to complete the lock holder's I/O.

Any of these pairs closes the cycle; the observed one is
lethe_sync (txg_sync) vs lethe_bookmark_key (z_rd_int).

## Separate race (data corruption, not deadlock)

`lethe_hijack_dsl_crypto_key()` memcpys the per-block KHF key into
`dck->dck_key.zk_current_keydata`, where `dck` is the SHARED refcounted
per-dataset keystore entry (`zk_current_key.ck_data` points at that
buffer). Concurrent crypts of different blocks in the same dataset
overwrite each other's key material with no synchronization -> blocks
encrypted with the wrong key -> MAC/checksum errors on read. Also, once
`zk_salt` rotates (`ZFS_CURRENT_MAX_SALT_USES`), decryption of blocks
whose stored salt no longer matches takes the HKDF-from-master branch in
`zio_do_crypt_data`, bypassing the hijacked key entirely.
