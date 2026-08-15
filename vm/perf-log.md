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
