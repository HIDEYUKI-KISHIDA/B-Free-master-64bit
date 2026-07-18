#!/usr/bin/env bash
# tools/_enosys_scan.sh
# 目的: B-Free libc / kernel ツリーで ENOSYS を返している箇所をすべて洗い出し、
#       「ENOSYS 削減 = 互換性向上」のための作業対象を可視化する。
#
# 走査対象:
#   - userland/libc/**.c          (libc 関数のスタブ実装)
#   - userland/libc/bfree_posix/**.c
#   - kernel/**.c                 (カーネル syscall の ENOSYS 返却)
#
# 出力:
#   - tools/enosys_scan.csv       file,line,symbol_guess,context
#   - tools/enosys_scan_report.md レポート(分類別件数)
set -euo pipefail
ROOT_DIR="$(cd "$(dirname "$0")/.." && pwd)"
OUT_CSV="$ROOT_DIR/tools/enosys_scan.csv"
OUT_MD="$ROOT_DIR/tools/enosys_scan_report.md"
TMP="$(mktemp -d)"
trap 'rm -rf "$TMP"' EXIT

# 走査対象ディレクトリ
declare -a TARGETS=(
    "$ROOT_DIR/userland/libc"
    "$ROOT_DIR/userland/libc/bfree_posix"
    "$ROOT_DIR/kernel"
)

# ENOSYS の出現箇所を全件抽出
: > "$TMP/raw.txt"
for d in "${TARGETS[@]}"; do
    [[ -d "$d" ]] || continue
    if command -v rg >/dev/null 2>&1; then
        rg -n --no-heading -F "ENOSYS" --type c "$d" >> "$TMP/raw.txt" || true
    else
        grep -RInF "ENOSYS" --include='*.c' --include='*.h' "$d" >> "$TMP/raw.txt" || true
    fi
done

# ファイル名から関数名を推測 (musl_libc_<name>.c / <name>.c)
awk -F: '
{
    file=$1
    line=$2
    rest=""
    for (i=3;i<=NF;i++) rest = rest (i==3?"":":") $i

    n=split(file, a, "/")
    base=a[n]
    sym=base
    sub(/\.c$/, "", sym)
    sub(/^musl_libc_/, "", sym)
    sub(/^bfree_/, "", sym)
    # ノイズ抑制: 自明な内部ファイル
    if (base ~ /^(libc_common|bfree_demo_stubs|README|.*\.h)$/) {
        sym = "(internal)"
    }

    # CSV 用にカンマと改行をエスケープ
    gsub(/,/, ";", rest)
    gsub(/\r/, "", rest)
    printf "%s,%s,%s,%s\n", file, line, sym, rest
}' "$TMP/raw.txt" > "$TMP/clean.csv"

# 重複行を削除
sort -u "$TMP/clean.csv" > "$OUT_CSV"

# 集計
total="$(wc -l < "$OUT_CSV" | tr -d ' ')"
unique_files="$(cut -d, -f1 "$OUT_CSV" | sort -u | wc -l | tr -d ' ')"
unique_symbols="$(cut -d, -f3 "$OUT_CSV" | sort -u | grep -v '^(internal)$' | wc -l | tr -d ' ')"

# シンボルごとの件数 Top
cut -d, -f3 "$OUT_CSV" | grep -v '^(internal)$' | sort | uniq -c | sort -rn > "$TMP/by_sym.txt"

# ディレクトリ別件数
awk -F, '
{
    # 第一フィールドからディレクトリ第1段階を取り出す
    p=$1
    sub(/^.*Program\/bfree_x86_64\//, "", p)
    n=split(p, a, "/")
    bucket=a[1] (n>1 ? "/" a[2] : "")
    cnt[bucket]++
}
END {
    for (k in cnt) printf "%6d %s\n", cnt[k], k
}' "$OUT_CSV" | sort -rn > "$TMP/by_dir.txt"

{
    echo "# ENOSYS Scan Report"
    echo ""
    echo "- 生成: $(date -u +"%Y-%m-%d %H:%M:%S UTC")"
    echo "- 走査対象: userland/libc, userland/libc/bfree_posix, kernel"
    echo "- ENOSYS 出現箇所: **${total}**"
    echo "- 関与ファイル数: ${unique_files}"
    echo "- 推定シンボル数 (file-name basis): ${unique_symbols}"
    echo ""
    echo "## ディレクトリ別件数"
    echo ""
    echo '```'
    cat "$TMP/by_dir.txt"
    echo '```'
    echo ""
    echo "## シンボル別 Top 50 (= 本実装に置換すれば ENOSYS 削減できる候補)"
    echo ""
    echo '```'
    head -50 "$TMP/by_sym.txt"
    echo '```'
    echo ""
    echo "## 全件 (file:line, symbol)"
    echo ""
    echo "_詳細は \`tools/enosys_scan.csv\` を参照_"
} > "$OUT_MD"

echo "[enosys_scan] total ENOSYS occurrences   : $total"
echo "[enosys_scan] unique files               : $unique_files"
echo "[enosys_scan] unique symbols (filename)  : $unique_symbols"
echo "[enosys_scan] csv    : $OUT_CSV"
echo "[enosys_scan] report : $OUT_MD"
