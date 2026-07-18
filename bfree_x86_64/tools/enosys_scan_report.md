# ENOSYS Scan Report

- 生成: 2026-05-15 23:12:44 UTC
- 走査対象: userland/libc, userland/libc/bfree_posix, kernel
- ENOSYS 出現箇所: **184**
- 関与ファイル数: 68
- 推定シンボル数 (file-name basis): 63

## ディレクトリ別件数

```
   164 userland/libc
    11 kernel/gpu_backend.c
     7 kernel/sysmain
     1 kernel/runtime_link_stubs.c
     1 kernel/gpu_backend.h
```

## シンボル別 Top 50 (= 本実装に置換すれば ENOSYS 削減できる候補)

```
     68 btron_thread_host
     11 gpu_backend
      4 syscall
      4 posix_spawnattr_sched
      3 timerfd
      3 statvfs
      3 process
      3 posix_typed_mem_stubs
      3 posix
      3 demo_stubs
      3 access
      2 utime
      2 unlinkat
      2 socketpair
      2 socket
      2 renameat
      2 putmsg_stub
      2 mknodat
      2 linkat
      2 getmsg
      2 fdetach
      2 fchownat
      2 fchown
      2 fchmodat
      2 fchmod
      2 fattach
      2 faccessat2
      2 faccessat
      2 close
      2 API_NAME
      1 utimensat
      1 unlink
      1 symlink
      1 sched_setscheduler
      1 sched_setparam
      1 sched_getscheduler
      1 sched_getparam
      1 runtime_link_stubs
      1 rmdir
      1 rename
      1 readv
      1 readlink
      1 posix_mem_offset
      1 poll
      1 mmap
      1 mknod
      1 mkdir
      1 lstat
      1 listen
      1 link
```

## 全件 (file:line, symbol)

_詳細は `tools/enosys_scan.csv` を参照_
