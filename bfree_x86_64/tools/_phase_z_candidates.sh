#!/usr/bin/env bash
# tools/_phase_z_candidates.sh
# Phase Z 追加候補 (Step 1) のリストアップ。
#
# 入力:
#   - posix2017_functions_only.txt        (685 POSIX シンボル)
#   - POSIX_685_POLICY_ENOSYS.txt          (既存 ENOSYS 60件)
#   - POSIX_OSS_GATEWAY_PRIORITY.csv       (OSS 通関 371件)
#
# 出力:
#   - tools/phase_z_candidates.txt         (ENOSYS 追加候補)
#   - tools/phase_z_candidates_report.md   (理由付きレポート)
set -euo pipefail
ROOT_DIR="$(cd "$(dirname "$0")/.." && pwd)"

POSIX_LIST="$ROOT_DIR/posix2017_functions_only.txt"
ENOSYS_LIST="$ROOT_DIR/POSIX_685_POLICY_ENOSYS.txt"
OSS_CSV="$ROOT_DIR/POSIX_OSS_GATEWAY_PRIORITY.csv"

OUT_TXT="$ROOT_DIR/tools/phase_z_candidates.txt"
OUT_MD="$ROOT_DIR/tools/phase_z_candidates_report.md"

TMP="$(mktemp -d)"
trap 'rm -rf "$TMP"' EXIT

# 既存 ENOSYS
awk '/^[[:space:]]*#/{next} {gsub(/\r/,""); gsub(/^[[:space:]]+|[[:space:]]+$/,""); if($0~/^[A-Za-z_][A-Za-z0-9_]*$/) print}' \
    "$ENOSYS_LIST" | sort -u > "$TMP/enosys.txt"

# OSS が呼ぶ
tail -n +2 "$OSS_CSV" | cut -d, -f1 | sort -u > "$TMP/oss.txt"

# 候補規則（接頭辞 / 個別シンボル）。OSS と既存 ENOSYS にあるものは除外する。
# 区分名は POSIX 規格のサブパッケージに対応。
cat > "$TMP/rules.txt" <<'EOF'
# group:pattern_or_symbol
AIO:aio_cancel
AIO:aio_error
AIO:aio_fsync
AIO:aio_read
AIO:aio_return
AIO:aio_suspend
AIO:aio_write
AIO:lio_listio
DBM:dbm_clearerr
DBM:dbm_close
DBM:dbm_delete
DBM:dbm_error
DBM:dbm_fetch
DBM:dbm_firstkey
DBM:dbm_nextkey
DBM:dbm_open
DBM:dbm_store
MSGCAT:catopen
MSGCAT:catgets
MSGCAT:catclose
WORDEXP:wordexp
WORDEXP:wordfree
FMTMSG:fmtmsg
LEGACY_STDIO:gets
LEGACY_STDIO:tempnam
LEGACY_STDIO:tmpnam
LEGACY_CRYPT:setkey
LEGACY_CRYPT:encrypt
LEGACY_CRYPT:crypt
LEGACY_TIME:getdate
LEGACY_RANDOM:drand48
LEGACY_RANDOM:erand48
LEGACY_RANDOM:jrand48
LEGACY_RANDOM:lcong48
LEGACY_RANDOM:lrand48
LEGACY_RANDOM:mrand48
LEGACY_RANDOM:nrand48
LEGACY_RANDOM:seed48
LEGACY_RANDOM:srand48
LEGACY_FTW:ftw
LEGACY_FTW:nftw
LEGACY_SEARCH:hcreate
LEGACY_SEARCH:hdestroy
LEGACY_SEARCH:hsearch
LEGACY_SEARCH:insque
LEGACY_SEARCH:remque
LEGACY_SEARCH:lsearch
LEGACY_SEARCH:lfind
LEGACY_SEARCH:tsearch
LEGACY_SEARCH:tfind
LEGACY_SEARCH:twalk
LEGACY_SEARCH:tdelete
LEGACY_BASE:a64l
LEGACY_BASE:l64a
LEGACY_SIGNAL:sighold
LEGACY_SIGNAL:sigignore
LEGACY_SIGNAL:sigpause
LEGACY_SIGNAL:sigrelse
LEGACY_SIGNAL:sigset
LEGACY_SIGNAL:psiginfo
LEGACY_MISC:swab
LEGACY_MISC:toascii
LEGACY_MISC:ulimit
LEGACY_MISC:sockatmark
EOF

# 候補集合
awk -F: '/^#/{next} {print $2}' "$TMP/rules.txt" | sort -u > "$TMP/raw_cand.txt"

# 既存 ENOSYS と OSS を除外
comm -23 "$TMP/raw_cand.txt" "$TMP/enosys.txt" > "$TMP/c1.txt"
comm -23 "$TMP/c1.txt" "$TMP/oss.txt"          > "$TMP/c2.txt"

# 候補のうち、本当に POSIX 685 に含まれているもののみ残す
awk '/^[[:space:]]*#/{next} /^(Requirements|Threads|Flags)[[:space:]]*$/{next} {gsub(/\r/,""); gsub(/^[[:space:]]+|[[:space:]]+$/,""); if($0~/^[A-Za-z_][A-Za-z0-9_]*$/) print}' \
    "$POSIX_LIST" | sort -u > "$TMP/posix.txt"

comm -12 "$TMP/c2.txt" "$TMP/posix.txt" > "$OUT_TXT"
n="$(wc -l <"$OUT_TXT" | tr -d ' ')"

{
    echo "# Phase Z 追加候補 (Step 1)"
    echo ""
    echo "- 生成: $(date -u +"%Y-%m-%d %H:%M:%S UTC")"
    echo "- 既存 ENOSYS  : $(wc -l <"$TMP/enosys.txt" | tr -d ' ')"
    echo "- OSS 通関     : $(wc -l <"$TMP/oss.txt" | tr -d ' ')"
    echo "- 候補総数     : **${n}**"
    echo ""
    echo "## 採択基準"
    echo ""
    echo "- POSIX 規格上は 685 に含まれるが、以下のいずれかに該当する:"
    echo "  - **LEGACY**: glibc / Linux でも非推奨もしくは廃止 (gets, tmpnam, dbm_*, hcreate, drand48 など)"
    echo "  - **SECURITY**: バッファ境界検査がないか TOCTOU 競合の温床 (gets, tempnam, tmpnam, setkey, encrypt)"
    echo "  - **OBSOLETE SUBSYSTEM**: 商用 OS でも実装が消えている (catopen 系, fmtmsg, getdate, sockatmark)"
    echo "  - **CALLED BY NONE**: 計測した 6 種 (bash / busybox / dash / python3 / ssh / sshd) のいずれも呼んでいない"
    echo ""
    echo "## 候補リスト（カテゴリ別）"
    echo ""
    echo '| group | symbol | 理由 |'
    echo '|-------|--------|------|'
    while IFS=: read -r grp sym; do
        [[ "$grp" =~ ^[[:space:]]*# ]] && continue
        # 候補として残っているかチェック
        if grep -qx -- "$sym" "$OUT_TXT"; then
            reason=""
            case "$grp" in
                AIO)            reason="非同期 I/O。Linux でも本格非同期は io_uring に移行。OSS は libev/libuv で吸収" ;;
                DBM)            reason="古典 DB API。LMDB/SQLite に置換済み" ;;
                MSGCAT)         reason="メッセージカタログ。gettext 系に置換済み" ;;
                WORDEXP)        reason="シェル展開 API。bash 自身が使うので OSS 通関側だが今回未検出。bash 内部実装で代替" ;;
                FMTMSG)         reason="フォーマットメッセージ。System V 由来、Linux で実用例なし" ;;
                LEGACY_STDIO)   reason="gets/tmpnam: バッファ境界検査なし or TOCTOU 競合。glibc 推奨外" ;;
                LEGACY_CRYPT)   reason="DES ベースの旧暗号 API。現代では使用禁止級" ;;
                LEGACY_TIME)    reason="ロケール依存で挙動不定。strptime 推奨" ;;
                LEGACY_RANDOM)  reason="48bit LCG。再現性以外の用途は random/getrandom 推奨" ;;
                LEGACY_FTW)     reason="再帰トラバーサル旧 API。nftw も含め fts_* / openat ループ推奨" ;;
                LEGACY_SEARCH)  reason="hcreate/insque 等の古典コンテナ。STL/Glib 互換で置換" ;;
                LEGACY_BASE)    reason="64 進数変換。実用例なし" ;;
                LEGACY_SIGNAL)  reason="XSI 古典シグナル。sigaction 推奨" ;;
                LEGACY_MISC)    reason="swab/toascii/ulimit/sockatmark: 旧 BSD/SysV 残骸" ;;
                *)              reason="(other)" ;;
            esac
            echo "| $grp | $sym | $reason |"
        fi
    done < "$TMP/rules.txt"
    echo ""
    echo "## ENOSYS リストに追記する行"
    echo ""
    echo '```'
    cat "$OUT_TXT"
    echo '```'
} > "$OUT_MD"

echo "[phase_z] candidates=$n"
echo "[phase_z] list   : $OUT_TXT"
echo "[phase_z] report : $OUT_MD"
