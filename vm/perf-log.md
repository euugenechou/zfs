# Lethe perf log

Non-debug builds (`vm/sync-build.sh perf`), fio 128k, 256M/job, 30s,
256M ARC, file vdev in the lethe Lima VM. Numbers are for
step-over-step comparison on this rig only.

## Baseline (pre-locking-work, commit 1cb48a135)

```
### randread jobs=1
perf: (groupid=0, jobs=1): err= 0: pid=4098: Sat Aug 15 00:34:55 2026
   READ: bw=10.2GiB/s (10.9GB/s), 10.2GiB/s-10.2GiB/s (10.9GB/s-10.9GB/s), io=305GiB (328GB), run=30001-30001msec
### randrw jobs=1
perf: (groupid=0, jobs=1): err= 0: pid=4124: Sat Aug 15 00:35:26 2026
   READ: bw=141MiB/s (147MB/s), 141MiB/s-141MiB/s (147MB/s-147MB/s), io=4216MiB (4420MB), run=30001-30001msec
  WRITE: bw=141MiB/s (148MB/s), 141MiB/s-141MiB/s (148MB/s-148MB/s), io=4226MiB (4431MB), run=30001-30001msec
### randread jobs=2
perf: (groupid=0, jobs=2): err= 0: pid=4157: Sat Aug 15 00:35:58 2026
   READ: bw=18.8GiB/s (20.2GB/s), 18.8GiB/s-18.8GiB/s (20.2GB/s-20.2GB/s), io=565GiB (607GB), run=30001-30001msec
### randrw jobs=2
perf: (groupid=0, jobs=2): err= 0: pid=4182: Sat Aug 15 00:36:29 2026
   READ: bw=101MiB/s (106MB/s), 101MiB/s-101MiB/s (106MB/s-106MB/s), io=3052MiB (3200MB), run=30146-30146msec
  WRITE: bw=102MiB/s (107MB/s), 102MiB/s-102MiB/s (107MB/s-107MB/s), io=3070MiB (3219MB), run=30146-30146msec
### randread jobs=4
perf: (groupid=0, jobs=4): err= 0: pid=4226: Sat Aug 15 00:37:02 2026
   READ: bw=28.5GiB/s (30.6GB/s), 28.5GiB/s-28.5GiB/s (30.6GB/s-30.6GB/s), io=855GiB (919GB), run=30001-30001msec
### randrw jobs=4
perf: (groupid=0, jobs=4): err= 0: pid=4265: Sat Aug 15 00:37:33 2026
   READ: bw=118MiB/s (123MB/s), 118MiB/s-118MiB/s (123MB/s-123MB/s), io=3616MiB (3792MB), run=30713-30713msec
  WRITE: bw=119MiB/s (125MB/s), 119MiB/s-119MiB/s (125MB/s-125MB/s), io=3658MiB (3836MB), run=30713-30713msec
### randread jobs=8
perf: (groupid=0, jobs=8): err= 0: pid=4310: Sat Aug 15 00:38:08 2026
   READ: bw=26.3GiB/s (28.2GB/s), 26.3GiB/s-26.3GiB/s (28.2GB/s-28.2GB/s), io=788GiB (846GB), run=30002-30002msec
### randrw jobs=8
perf: (groupid=0, jobs=8): err= 0: pid=4348: Sat Aug 15 00:38:39 2026
   READ: bw=119MiB/s (125MB/s), 119MiB/s-119MiB/s (125MB/s-125MB/s), io=3590MiB (3764MB), run=30074-30074msec
  WRITE: bw=121MiB/s (127MB/s), 121MiB/s-121MiB/s (127MB/s-127MB/s), io=3642MiB (3819MB), run=30074-30074msec
```

Note: `randread` numbers are inflated (multi-GiB/s) because each
`randread` pass is the first touch of that job's byte range on a
freshly created dataset -- fio lays the file out (writes it) and then
immediately reads back data that is still resident in the 256M ARC
before enough of it can be evicted, so most "reads" never hit the disk
or the decrypt path. `randrw` (steady-state mixed read/write, ~100-150
MiB/s across jobs=1..8) is the more meaningful number for
step-over-step comparison; treat `randread` here only as a same-rig,
same-methodology baseline for later steps, not as a real decrypt-path
read number.

## Step 0: read purification (commit bcd4723e1)

```
### randread jobs=1
perf: (groupid=0, jobs=1): err= 0: pid=110579: Sat Aug 15 15:24:56 2026
   READ: bw=9.80GiB/s (10.5GB/s), 9.80GiB/s-9.80GiB/s (10.5GB/s-10.5GB/s), io=294GiB (316GB), run=30001-30001msec
### randrw jobs=1
perf: (groupid=0, jobs=1): err= 0: pid=110594: Sat Aug 15 15:25:26 2026
   READ: bw=139MiB/s (146MB/s), 139MiB/s-139MiB/s (146MB/s-146MB/s), io=4183MiB (4386MB), run=30004-30004msec
  WRITE: bw=140MiB/s (146MB/s), 140MiB/s-140MiB/s (146MB/s-146MB/s), io=4191MiB (4395MB), run=30004-30004msec
### randread jobs=2
perf: (groupid=0, jobs=2): err= 0: pid=110619: Sat Aug 15 15:25:58 2026
   READ: bw=18.3GiB/s (19.6GB/s), 18.3GiB/s-18.3GiB/s (19.6GB/s-19.6GB/s), io=548GiB (588GB), run=30001-30001msec
### randrw jobs=2
perf: (groupid=0, jobs=2): err= 0: pid=110635: Sat Aug 15 15:26:28 2026
   READ: bw=102MiB/s (107MB/s), 102MiB/s-102MiB/s (107MB/s-107MB/s), io=3072MiB (3221MB), run=30002-30002msec
  WRITE: bw=103MiB/s (108MB/s), 103MiB/s-103MiB/s (108MB/s-108MB/s), io=3092MiB (3242MB), run=30002-30002msec
### randread jobs=4
perf: (groupid=0, jobs=4): err= 0: pid=110673: Sat Aug 15 15:27:02 2026
   READ: bw=28.8GiB/s (30.9GB/s), 28.8GiB/s-28.8GiB/s (30.9GB/s-30.9GB/s), io=864GiB (927GB), run=30001-30001msec
### randrw jobs=4
perf: (groupid=0, jobs=4): err= 0: pid=110696: Sat Aug 15 15:27:32 2026
   READ: bw=119MiB/s (125MB/s), 119MiB/s-119MiB/s (125MB/s-125MB/s), io=3612MiB (3788MB), run=30383-30383msec
  WRITE: bw=120MiB/s (126MB/s), 120MiB/s-120MiB/s (126MB/s-126MB/s), io=3657MiB (3835MB), run=30383-30383msec
### randread jobs=8
perf: (groupid=0, jobs=8): err= 0: pid=110727: Sat Aug 15 15:28:08 2026
   READ: bw=26.9GiB/s (28.8GB/s), 26.9GiB/s-26.9GiB/s (28.8GB/s-28.8GB/s), io=806GiB (865GB), run=30002-30002msec
### randrw jobs=8
perf: (groupid=0, jobs=8): err= 0: pid=110750: Sat Aug 15 15:28:39 2026
   READ: bw=123MiB/s (129MB/s), 123MiB/s-123MiB/s (129MB/s-129MB/s), io=3805MiB (3989MB), run=30908-30908msec
  WRITE: bw=125MiB/s (131MB/s), 125MiB/s-125MiB/s (131MB/s-131MB/s), io=3853MiB (4041MB), run=30908-30908msec
```

`randread` here is still ARC-inflated for the same reason as the
baseline above; `randrw` is the step-over-step metric and holds flat
against baseline (~139/140 -> ~123/125 MiB/s across jobs=1..8, same
shape as baseline's ~141/141 -> ~119/121). Includes the read-path
purification (commit b46bf30c/13c04192) plus the btreemap
internal-node double-drop fix and the khf_overwrite_keyed sparse-fold
key-derivation fix (commits 816944def, bcd4723e1) that were required
to get a clean reimport for this measurement; the big win from read
purification is qualitative (read-only epochs no longer rewrite every
touched ERL), not visible in these steady-state bandwidth numbers.

## Step A: single reader-shared rwlock (commit <hash>)

```
### randread jobs=1
perf: (groupid=0, jobs=1): err= 0: pid=259915: Sat Aug 15 15:48:54 2026
   READ: bw=9.85GiB/s (10.6GB/s), 9.85GiB/s-9.85GiB/s (10.6GB/s-10.6GB/s), io=296GiB (317GB), run=30001-30001msec
### randrw jobs=1
perf: (groupid=0, jobs=1): err= 0: pid=259931: Sat Aug 15 15:49:24 2026
   READ: bw=140MiB/s (147MB/s), 140MiB/s-140MiB/s (147MB/s-147MB/s), io=4205MiB (4409MB), run=30001-30001msec
  WRITE: bw=140MiB/s (147MB/s), 140MiB/s-140MiB/s (147MB/s-147MB/s), io=4211MiB (4416MB), run=30001-30001msec
### randread jobs=2
perf: (groupid=0, jobs=2): err= 0: pid=259950: Sat Aug 15 15:49:57 2026
   READ: bw=18.5GiB/s (19.9GB/s), 18.5GiB/s-18.5GiB/s (19.9GB/s-19.9GB/s), io=556GiB (597GB), run=30001-30001msec
### randrw jobs=2
perf: (groupid=0, jobs=2): err= 0: pid=259965: Sat Aug 15 15:50:27 2026
   READ: bw=103MiB/s (108MB/s), 103MiB/s-103MiB/s (108MB/s-108MB/s), io=3094MiB (3245MB), run=30003-30003msec
  WRITE: bw=104MiB/s (109MB/s), 104MiB/s-104MiB/s (109MB/s-109MB/s), io=3120MiB (3271MB), run=30003-30003msec
### randread jobs=4
perf: (groupid=0, jobs=4): err= 0: pid=260006: Sat Aug 15 15:51:00 2026
   READ: bw=27.7GiB/s (29.7GB/s), 27.7GiB/s-27.7GiB/s (29.7GB/s-29.7GB/s), io=830GiB (892GB), run=30002-30002msec
### randrw jobs=4
perf: (groupid=0, jobs=4): err= 0: pid=260028: Sat Aug 15 15:51:31 2026
   READ: bw=117MiB/s (123MB/s), 117MiB/s-117MiB/s (123MB/s-123MB/s), io=3603MiB (3778MB), run=30718-30718msec
  WRITE: bw=119MiB/s (125MB/s), 119MiB/s-119MiB/s (125MB/s-125MB/s), io=3648MiB (3825MB), run=30718-30718msec
### randread jobs=8
perf: (groupid=0, jobs=8): err= 0: pid=260053: Sat Aug 15 15:52:07 2026
   READ: bw=25.2GiB/s (27.0GB/s), 25.2GiB/s-25.2GiB/s (27.0GB/s-27.0GB/s), io=755GiB (811GB), run=30002-30002msec
### randrw jobs=8
perf: (groupid=0, jobs=8): err= 0: pid=260076: Sat Aug 15 15:52:38 2026
   READ: bw=124MiB/s (130MB/s), 124MiB/s-124MiB/s (130MB/s-130MB/s), io=3794MiB (3978MB), run=30670-30670msec
  WRITE: bw=126MiB/s (132MB/s), 126MiB/s-126MiB/s (132MB/s-132MB/s), io=3859MiB (4047MB), run=30670-30670msec
```

`randread` is still ARC-inflated for the same reason noted above (each
pass is effectively a cache-warm reread of data fio just laid out) and
is not a reliable signal for the reader/writer lock split introduced
in this step; five separate rwlocks vs. one shared rwlock isn't
distinguishable when almost nothing actually reaches the decrypt path
under contention. `randrw` (steady-state, mixed read/write, one
process per job contending the single `spa->lethe_lock`) is the
step-over-step metric. Paired table, READ/WRITE MiB/s by jobs:

| jobs | baseline READ | baseline WRITE | step0 READ | step0 WRITE | stepA READ | stepA WRITE |
|-----:|---------------:|----------------:|------------:|-------------:|------------:|-------------:|
| 1    | 141            | 141              | 139         | 140          | 140         | 140          |
| 2    | 101            | 102              | 102         | 103          | 103         | 104          |
| 4    | 118            | 119              | 119         | 120          | 117         | 119          |
| 8    | 119            | 121              | 123         | 125          | 124         | 126          |

StepA randrw holds within noise of both baseline and step0 across
jobs=1..8 (a couple MiB/s either way, same shape as the step0-vs-baseline
comparison) -- expected, since randrw at this depth/queue is not
lock-contention-bound on this rig (`iodepth` capped at 1 by the sync
`psync` engine in the deadlock reproducer's fio job, and here each `fio`
job here runs single-threaded I/O against its own file range). The
substantive result of this step is the gauntlet: the 120s 8-job
reader/writer-mixing fio deadlock reproducer completed cleanly (exit 0,
not the 124-timeout that would indicate a hang) against the single
shared `spa->lethe_lock`, with decrypt-side derivations taking it
`RW_READER` and encrypt/write/purge/sync taking it `RW_WRITER`.
