#!/usr/bin/env bash
# tools/_oss_collect_run.sh
# 役割: WSL上で busybox/bash/python3/dropbear などを探し、tests/oss_bins/ にコピー後、
#       nm -u から POSIX 685 と交差する未定義参照を抽出して
#       POSIX_OSS_GATEWAY_PRIORITY.csv を生成する。
#
# 使い方:
#   bash ./tools/_oss_collect_run.sh
#
# 出力:
#   tests/oss_bins/                 ... 収集した OSS バイナリ
#   tools/oss_undef/<bin>.undef.txt ... nm -u の生結果
#   tools/oss_undef/<bin>.posix.txt ... POSIX 685 との交差
#   POSIX_OSS_GATEWAY_PRIORITY.csv  ... 全 OSS 横断の優先度リスト
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "$0")/.." && pwd)"
OSS_BIN_DIR="$ROOT_DIR/tests/oss_bins"
OUT_DIR="$ROOT_DIR/tools/oss_undef"
POSIX_LIST="$ROOT_DIR/posix2017_functions_only.txt"
CSV_OUT="$ROOT_DIR/POSIX_OSS_GATEWAY_PRIORITY.csv"

mkdir -p "$OSS_BIN_DIR" "$OUT_DIR"

if ! command -v nm >/dev/null 2>&1; then
    echo "[oss_collect] ERROR: 'nm' not found in PATH (apt install binutils)" >&2
    exit 3
fi

# ----------------------------------------------------------------------
# 1. ホストから OSS バイナリを収集
# ----------------------------------------------------------------------
declare -a CANDIDATES=(busybox bash dash python3 dropbear ssh sshd dropbearmulti)
declare -a COPIED=()

for name in "${CANDIDATES[@]}"; do
    p="$(command -v "$name" 2>/dev/null || true)"
    [[ -z "$p" || ! -f "$p" ]] && continue
    # シンボリックリンクは実体を解決
    real="$(readlink -f "$p" 2>/dev/null || echo "$p")"
    dest="$OSS_BIN_DIR/$name"
    if [[ -f "$real" ]]; then
        cp -f "$real" "$dest"
        chmod +r "$dest" || true
        COPIED+=("$name")
        echo "[oss_collect] collected $name <- $real"
    fi
done

# ----------------------------------------------------------------------
# 2. POSIX 685 シンボル集合をロード
# ----------------------------------------------------------------------
if [[ ! -f "$POSIX_LIST" ]]; then
    echo "[oss_collect] ERROR: missing $POSIX_LIST" >&2
    exit 2
fi

POSIX_SET="$(awk '
    /^[[:space:]]*#/ { next }
    /^(Requirements|Threads|Flags)[[:space:]]*$/ { next }
    {
        gsub(/\r/, "", $0)
        gsub(/^[[:space:]]+|[[:space:]]+$/, "", $0)
        if ($0 ~ /^[A-Za-z_][A-Za-z0-9_]*$/) print $0
    }' "$POSIX_LIST" | sort -u)"
posix_count="$(echo "$POSIX_SET" | wc -l)"
echo "[oss_collect] POSIX 685 symbols loaded: $posix_count"

POSIX_TMP="$(mktemp)"
echo "$POSIX_SET" > "$POSIX_TMP"
trap 'rm -f "$POSIX_TMP"' EXIT

# ----------------------------------------------------------------------
# 3. 各 OSS バイナリで nm -u → POSIX 685 と交差
# ----------------------------------------------------------------------
declare -A SYM_OSS_COUNT     # symbol -> count
declare -A SYM_OSS_LIST      # symbol -> "bin1;bin2;..."

shopt -s nullglob
bins=("$OSS_BIN_DIR"/*)
shopt -u nullglob

if [[ "${#bins[@]}" -eq 0 ]]; then
    echo "[oss_collect] WARN: no binaries under $OSS_BIN_DIR"
    exit 0
fi

for bin in "${bins[@]}"; do
    [[ -f "$bin" ]] || continue
    name="$(basename "$bin")"
    [[ "$name" == *.txt || "$name" == *.md ]] && continue

    undef_file="$OUT_DIR/${name}.undef.txt"
    posix_file="$OUT_DIR/${name}.posix.txt"

    # ストリップ済み ELF は通常シンボル表が空のため、動的シンボル表 (-D) を併用。
    # 出力例: "                 U printf@@GLIBC_2.2.5"
    {
        nm --undefined-only "$bin" 2>/dev/null || true
        nm -D --undefined-only "$bin" 2>/dev/null || true
    } | awk '{ s=$NF; sub(/@.*/, "", s); if (s ~ /^[A-Za-z_][A-Za-z0-9_]*$/) print s }' \
      | sort -u > "$undef_file" || {
        echo "[oss_collect] WARN: nm failed on $name"
        : > "$undef_file"
    }

    # POSIX 685 との交差
    comm -12 "$undef_file" "$POSIX_TMP" > "$posix_file" || true

    n_undef="$(wc -l <"$undef_file" | tr -d ' ')"
    n_posix="$(wc -l <"$posix_file" | tr -d ' ')"
    echo "[oss_collect] $name : undef=$n_undef, posix_hit=$n_posix"

    while IFS= read -r s; do
        [[ -z "$s" ]] && continue
        SYM_OSS_COUNT[$s]=$(( ${SYM_OSS_COUNT[$s]:-0} + 1 ))
        if [[ -n "${SYM_OSS_LIST[$s]:-}" ]]; then
            SYM_OSS_LIST[$s]="${SYM_OSS_LIST[$s]};$name"
        else
            SYM_OSS_LIST[$s]="$name"
        fi
    done < "$posix_file"
done

# ----------------------------------------------------------------------
# 4. CSV 出力（used_count 降順、symbol 昇順）
# ----------------------------------------------------------------------
{
    echo "symbol,used_by,used_count"
    for s in "${!SYM_OSS_COUNT[@]}"; do
        printf '%s\t%s\t%s\n' "$s" "${SYM_OSS_LIST[$s]}" "${SYM_OSS_COUNT[$s]}"
    done | sort -t$'\t' -k3,3nr -k1,1 | awk -F'\t' '{printf "%s,%s,%s\n", $1, $2, $3}'
} > "$CSV_OUT"

total_unique="$(( $(wc -l < "$CSV_OUT") - 1 ))"
echo
echo "[oss_collect] DONE"
echo "  per-binary undef : $OUT_DIR/*.undef.txt"
echo "  per-binary posix : $OUT_DIR/*.posix.txt"
echo "  priority CSV     : $CSV_OUT"
echo "  total unique POSIX symbols used by OSS: $total_unique"
