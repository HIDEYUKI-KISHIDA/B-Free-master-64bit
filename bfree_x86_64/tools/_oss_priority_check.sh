#!/usr/bin/env bash
# tools/_oss_priority_check.sh
# 役割: OSS 通関リスト (POSIX_OSS_GATEWAY_PRIORITY.csv) と
#       ENOSYS ポリシー (POSIX_685_POLICY_ENOSYS.txt) の整合性を点検する。
#
# 出力:
#   - OSS union 総数
#   - ENOSYS policy 総数
#   - 交差 (= OSS が要求しているのに ENOSYS で済ませているシンボル → 警告対象)
#   - OSS 未使用の ENOSYS シンボル
#   - POSIX 685 − OSS union − ENOSYS = 「Phase Z 候補だがまだ未登録」のリスト
set -euo pipefail
ROOT_DIR="$(cd "$(dirname "$0")/.." && pwd)"
CSV="$ROOT_DIR/POSIX_OSS_GATEWAY_PRIORITY.csv"
ENOSYS="$ROOT_DIR/POSIX_685_POLICY_ENOSYS.txt"
POSIX="$ROOT_DIR/posix2017_functions_only.txt"
TMP_DIR="$(mktemp -d)"
trap 'rm -rf "$TMP_DIR"' EXIT

[[ -f "$CSV"    ]] || { echo "missing $CSV"   >&2; exit 2; }
[[ -f "$ENOSYS" ]] || { echo "missing $ENOSYS">&2; exit 2; }
[[ -f "$POSIX"  ]] || { echo "missing $POSIX" >&2; exit 2; }

tail -n +2 "$CSV" | cut -d, -f1 | sort -u > "$TMP_DIR/oss.txt"

awk '
    /^[[:space:]]*#/ { next }
    {
        gsub(/\r/, "", $0)
        gsub(/^[[:space:]]+|[[:space:]]+$/, "", $0)
        if ($0 ~ /^[A-Za-z_][A-Za-z0-9_]*$/) print $0
    }' "$ENOSYS" | sort -u > "$TMP_DIR/enosys.txt"

awk '
    /^[[:space:]]*#/ { next }
    /^(Requirements|Threads|Flags)[[:space:]]*$/ { next }
    {
        gsub(/\r/, "", $0)
        gsub(/^[[:space:]]+|[[:space:]]+$/, "", $0)
        if ($0 ~ /^[A-Za-z_][A-Za-z0-9_]*$/) print $0
    }' "$POSIX" | sort -u > "$TMP_DIR/posix.txt"

oss_n=$(wc -l <"$TMP_DIR/oss.txt"   | tr -d ' ')
eno_n=$(wc -l <"$TMP_DIR/enosys.txt" | tr -d ' ')
pos_n=$(wc -l <"$TMP_DIR/posix.txt" | tr -d ' ')

echo "[oss_priority] POSIX 685   : $pos_n"
echo "[oss_priority] OSS union   : $oss_n"
echo "[oss_priority] ENOSYS list : $eno_n"

# 1. OSS ∩ ENOSYS = 「OSS が要求しているのに ENOSYS で済ませている」(危険)
comm -12 "$TMP_DIR/oss.txt" "$TMP_DIR/enosys.txt" > "$TMP_DIR/conflict.txt"
n_conflict=$(wc -l <"$TMP_DIR/conflict.txt" | tr -d ' ')
echo ""
echo "[oss_priority] (1) conflict: OSS が要求 ∧ ENOSYS 登録 = $n_conflict"
if [[ "$n_conflict" -gt 0 ]]; then
    sed 's/^/    /' "$TMP_DIR/conflict.txt"
fi

# 2. OSS が要求していない ENOSYS = OK (純粋な未提供)
comm -23 "$TMP_DIR/enosys.txt" "$TMP_DIR/oss.txt" > "$TMP_DIR/enosys_safe.txt"
n_safe=$(wc -l <"$TMP_DIR/enosys_safe.txt" | tr -d ' ')
echo ""
echo "[oss_priority] (2) ENOSYS で安全に切れる関数 = $n_safe"

# 3. OSS が要求している（= 本実装すべき）関数
echo ""
echo "[oss_priority] (3) OSS が要求している関数 (= Phase A〜D の実装ターゲット) = $oss_n"

# 4. POSIX 685 − OSS − ENOSYS = 「ガイドライン外」(libc が持ってるが OSS は使わない & Phase Z でもない)
comm -23 "$TMP_DIR/posix.txt" "$TMP_DIR/oss.txt" > "$TMP_DIR/non_oss.txt"
comm -23 "$TMP_DIR/non_oss.txt" "$TMP_DIR/enosys.txt" > "$TMP_DIR/unclassified.txt"
n_uncls=$(wc -l <"$TMP_DIR/unclassified.txt" | tr -d ' ')
echo ""
echo "[oss_priority] (4) 685 − OSS − ENOSYS = 未分類 (Phase Z 拡張候補) = $n_uncls"
echo "    (一部 sample 30 件:)"
head -30 "$TMP_DIR/unclassified.txt" | sed 's/^/      /'

# 出力レポートも出す
REPORT="$ROOT_DIR/POSIX_OSS_GATEWAY_PRIORITY_REPORT.md"
{
    echo "# POSIX OSS Gateway Priority Report"
    echo ""
    echo "- 生成: $(date -u +"%Y-%m-%d %H:%M:%S UTC")"
    echo "- POSIX 685             : $pos_n"
    echo "- OSS 通関 union        : $oss_n"
    echo "- ENOSYS policy         : $eno_n"
    echo "- conflict (OSS∩ENOSYS) : $n_conflict"
    echo "- ENOSYS safe           : $n_safe"
    echo "- 未分類 (Phase Z候補)  : $n_uncls"
    echo ""
    echo "## 計測したバイナリ"
    echo ""
    echo '```'
    ls -1 "$ROOT_DIR/tests/oss_bins" 2>/dev/null || true
    echo '```'
    echo ""
    echo "## conflict 一覧 (OSS が要求しているのに ENOSYS 登録)"
    echo ""
    if [[ "$n_conflict" -eq 0 ]]; then
        echo "_該当なし_"
    else
        echo '```'
        cat "$TMP_DIR/conflict.txt"
        echo '```'
    fi
    echo ""
    echo "## 未分類 (Phase Z 拡張候補)"
    echo ""
    echo '```'
    cat "$TMP_DIR/unclassified.txt"
    echo '```'
    echo ""
    echo "## OSS が要求している関数 Top 30 (used_count 降順)"
    echo ""
    echo '```'
    head -31 "$ROOT_DIR/POSIX_OSS_GATEWAY_PRIORITY.csv"
    echo '```'
} > "$REPORT"
echo ""
echo "[oss_priority] report: $REPORT"
