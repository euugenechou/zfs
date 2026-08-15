# Delete-path key purging (2026-08-14)

Approved design for wiring secure-delete key purging into the ZFS deletion
paths. Snapshots and clones are explicitly OUT OF SCOPE: purging is
unconditional, and a snapshot taken on a Lethe dataset is unsupported
(snapshot reads never worked anyway — key derivation uses the write-time
objset id, which a snapshot doesn't share).

## Semantics

A file's keys become cryptographically underivable at the FIRST EPOCH
BOUNDARY (lethe_sync patch) after the purge event, matching the paper's
epoch model:

- Object delete: the object's slot in its master ERL is re-marked; the
  next master patch folds a fresh key over it, making the serialized
  object ERL — and so all of the file's block keys — underivable.
- Truncate: the fully-freed data block range is re-marked in the object
  ERL; the next patch folds fresh keys over the range. Partial edge
  blocks are rewritten by ZFS and covered by existing overwrite rotation.
- Dataset destroy: the objset's slot in the uber ERL is re-marked; the
  next uber patch makes the master ERL (and transitively everything under
  it) underivable.

## Components

1. `lethe_object_free(objset_t *os, uint64_t object)` — called from
   `dmu_object_free()` BEFORE its internal `dnode_free_range()` call
   (so the range hook no-ops for full deletes). Under the lethe locks
   (memory only): drop the object ERL from the store (`erl_drop`), mark
   the object in its master ERL, remove the erlmap entry, set
   `epoch_dirty`, and queue the backing DMU object for freeing.

2. `lethe_object_free_range(objset_t *os, uint64_t object, uint64_t
   start, uint64_t end)` — called from `dnode_free_range()` with the
   fully-freed data block range (end = UINT64_MAX for truncate-to-end;
   clamped to the ERL's block count). Marks the range in the object ERL
   if one is resident; no-op otherwise.

3. `lethe_objset_destroy(spa_t *spa, uint64_t objset)` — called from
   `dsl_destroy_head_sync_impl()` (syncing context; memory-only work is
   safe). Drops every object ERL of the objset and its master ERL, marks
   the objset in the uber ERL, removes all their erlmap entries, and
   queues every backing object for freeing.

4. Purge queue — `spa->lethe_purge_queue` (vec of {name, object}
   guarded by its own `lethe_purge_lock` mutex, never nested inside the
   five ERL locks). Drained by `lethe_sync()` phase B with no lethe
   locks held: `zap_remove()` the name from the lethe root object and
   `dmu_object_free()` the ERL object. Map-entry removals persist
   automatically because phase B rewrites the packed erlmaps whenever
   the epoch is dirty, in the same txg as the frees and the rotation.

Both DMU hooks are gated on `os->os_encrypted`, so unencrypted objsets
(including the MOS — and therefore phase B's own frees of ERL objects)
never touch the lethe locks.

## Invariants preserved

- No lethe lock is ever held across blocking DMU I/O (hooks are
  memory-only; all DMU work happens in phase B).
- Lock order: the five ERL locks in canonical order; the purge mutex is
  only ever taken alone.
- Crash atomicity: purge marks, map rewrites, ZAP removals, and object
  frees all land in the same txg; a crash before commit rolls back the
  file deletion and the purge together.

## Testing

- Userspace (`vm/erl_delete_test.c`): master-ERL slot rotation — the key
  a slot had before deletion must be underivable after mark+patch, while
  untouched slots keep their keys.
- Kernel (`vm/test-delete.sh`): create files; delete some; verify
  survivors across txg syncs and export/import; truncate test; `zfs
  destroy` + recreate test; scrub clean; DEBUG-log check that purge
  entries were processed. Then the existing fio + verify-integrity
  gauntlet for regressions.

## Out of scope (documented)

Snapshots/clones (unsupported entirely), block-granular purge deferral
tied to deadlists, `zvol` deletion paths beyond what `dmu_object_free`
already covers, and reclaiming ERL space for truncated ranges.
