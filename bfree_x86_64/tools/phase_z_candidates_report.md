# Phase Z 追加候補 (Step 1)

- 生成: 2026-05-15 23:00:09 UTC
- 既存 ENOSYS  : 60
- OSS 通関     : 371
- 候補総数     : **34**

## 採択基準

- POSIX 規格上は 685 に含まれるが、以下のいずれかに該当する:
  - **LEGACY**: glibc / Linux でも非推奨もしくは廃止 (gets, tmpnam, dbm_*, hcreate, drand48 など)
  - **SECURITY**: バッファ境界検査がないか TOCTOU 競合の温床 (gets, tempnam, tmpnam, setkey, encrypt)
  - **OBSOLETE SUBSYSTEM**: 商用 OS でも実装が消えている (catopen 系, fmtmsg, getdate, sockatmark)
  - **CALLED BY NONE**: 計測した 6 種 (bash / busybox / dash / python3 / ssh / sshd) のいずれも呼んでいない

## 候補リスト（カテゴリ別）

| group | symbol | 理由 |
|-------|--------|------|
| AIO | aio_cancel | 非同期 I/O。Linux でも本格非同期は io_uring に移行。OSS は libev/libuv で吸収 |
| AIO | aio_error | 非同期 I/O。Linux でも本格非同期は io_uring に移行。OSS は libev/libuv で吸収 |
| AIO | aio_fsync | 非同期 I/O。Linux でも本格非同期は io_uring に移行。OSS は libev/libuv で吸収 |
| AIO | aio_read | 非同期 I/O。Linux でも本格非同期は io_uring に移行。OSS は libev/libuv で吸収 |
| AIO | aio_return | 非同期 I/O。Linux でも本格非同期は io_uring に移行。OSS は libev/libuv で吸収 |
| AIO | aio_suspend | 非同期 I/O。Linux でも本格非同期は io_uring に移行。OSS は libev/libuv で吸収 |
| AIO | aio_write | 非同期 I/O。Linux でも本格非同期は io_uring に移行。OSS は libev/libuv で吸収 |
| AIO | lio_listio | 非同期 I/O。Linux でも本格非同期は io_uring に移行。OSS は libev/libuv で吸収 |
| DBM | dbm_clearerr | 古典 DB API。LMDB/SQLite に置換済み |
| MSGCAT | catopen | メッセージカタログ。gettext 系に置換済み |
| MSGCAT | catgets | メッセージカタログ。gettext 系に置換済み |
| MSGCAT | catclose | メッセージカタログ。gettext 系に置換済み |
| WORDEXP | wordexp | シェル展開 API。bash 自身が使うので OSS 通関側だが今回未検出。bash 内部実装で代替 |
| FMTMSG | fmtmsg | フォーマットメッセージ。System V 由来、Linux で実用例なし |
| LEGACY_STDIO | gets | gets/tmpnam: バッファ境界検査なし or TOCTOU 競合。glibc 推奨外 |
| LEGACY_STDIO | tempnam | gets/tmpnam: バッファ境界検査なし or TOCTOU 競合。glibc 推奨外 |
| LEGACY_STDIO | tmpnam | gets/tmpnam: バッファ境界検査なし or TOCTOU 競合。glibc 推奨外 |
| LEGACY_CRYPT | setkey | DES ベースの旧暗号 API。現代では使用禁止級 |
| LEGACY_CRYPT | encrypt | DES ベースの旧暗号 API。現代では使用禁止級 |
| LEGACY_TIME | getdate | ロケール依存で挙動不定。strptime 推奨 |
| LEGACY_RANDOM | drand48 | 48bit LCG。再現性以外の用途は random/getrandom 推奨 |
| LEGACY_FTW | ftw | 再帰トラバーサル旧 API。nftw も含め fts_* / openat ループ推奨 |
| LEGACY_FTW | nftw | 再帰トラバーサル旧 API。nftw も含め fts_* / openat ループ推奨 |
| LEGACY_SEARCH | hcreate | hcreate/insque 等の古典コンテナ。STL/Glib 互換で置換 |
| LEGACY_SEARCH | insque | hcreate/insque 等の古典コンテナ。STL/Glib 互換で置換 |
| LEGACY_SEARCH | lsearch | hcreate/insque 等の古典コンテナ。STL/Glib 互換で置換 |
| LEGACY_SEARCH | tdelete | hcreate/insque 等の古典コンテナ。STL/Glib 互換で置換 |
| LEGACY_BASE | a64l | 64 進数変換。実用例なし |
| LEGACY_SIGNAL | sighold | XSI 古典シグナル。sigaction 推奨 |
| LEGACY_SIGNAL | psiginfo | XSI 古典シグナル。sigaction 推奨 |
| LEGACY_MISC | swab | swab/toascii/ulimit/sockatmark: 旧 BSD/SysV 残骸 |
| LEGACY_MISC | toascii | swab/toascii/ulimit/sockatmark: 旧 BSD/SysV 残骸 |
| LEGACY_MISC | ulimit | swab/toascii/ulimit/sockatmark: 旧 BSD/SysV 残骸 |
| LEGACY_MISC | sockatmark | swab/toascii/ulimit/sockatmark: 旧 BSD/SysV 残骸 |

## ENOSYS リストに追記する行

```
a64l
aio_cancel
aio_error
aio_fsync
aio_read
aio_return
aio_suspend
aio_write
catclose
catgets
catopen
dbm_clearerr
drand48
encrypt
fmtmsg
ftw
getdate
gets
hcreate
insque
lio_listio
lsearch
nftw
psiginfo
setkey
sighold
sockatmark
swab
tdelete
tempnam
tmpnam
toascii
ulimit
wordexp
```
