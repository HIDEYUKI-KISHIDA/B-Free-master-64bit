# ENOSYS 分類サマリ

- 生成: 2026-05-15 23:13:53 UTC
- 出現箇所合計: **184**

## カテゴリ別件数

| カテゴリ | 件数 | 意味 |
|---------|-----:|------|
| (1) GPU / runtime_link 系 | 13 | GPU 抽象層の TK2GPU_ENOSYS。POSIX 互換性とは独立 |
| (2) bfree_posix bridge    | 134 | syscall ラッパ層。**ここを潰すと「OSS 動作」が増える** |
| (3) musl_libc_*.c stub    | 20 | libc 関数の中で ENOSYS している。完成度の本丸 |
| (4) kernel/sysmain        | 7 | カーネル syscall ディスパッチの ENOSYS フォールバック |
| (5) サンプル/雛形         | 5 | API_NAME.c / bfree_demo_stubs.c 等。実害なし |

## (2) bfree_posix bridge で ENOSYS している関数 (43 件)

**優先順位 高**: ここが ENOSYS だと OSS が「機能がない」と判断して諦める。

```
accept
access
bind
btron_thread_host
chmod
chown
chroot
close
connect
faccessat
faccessat2
fattach
fchmod
fchmodat
fchown
fchownat
fdetach
fstatat
fstatvfs
getmsg
ipc_named_open_bridge
link
linkat
listen
lstat
mkdir
mknod
mknodat
poll
process
readlink
rename
renameat
rmdir
socket
socketpair
statvfs
symlink
timerfd
unlink
unlinkat
utime
utimensat
```

## (3) musl_libc_*.c で ENOSYS している関数 (14 件)

**優先順位 中**: ライブラリ層のスタブ。OSS によっては (2) より早くここで諦める。

```
confstr
getrlimit
getrusage
mmap
posix_mem_offset
posix_spawnattr_sched
posix_typed_mem_stubs
putmsg_stub
readv
sched_getparam
sched_getscheduler
sched_setparam
sched_setscheduler
statvfs
```
