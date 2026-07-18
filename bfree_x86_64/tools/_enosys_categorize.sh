#!/usr/bin/env bash
# tools/_enosys_categorize.sh
# enosys_scan.csv を意味のあるカテゴリに分類する。
set -euo pipefail
ROOT_DIR="$(cd "$(dirname "$0")/.." && pwd)"
CSV="$ROOT_DIR/tools/enosys_scan.csv"
OUT="$ROOT_DIR/tools/enosys_categories.md"

if [[ ! -f "$CSV" ]]; then
    echo "[enosys_cat] missing $CSV; run _enosys_scan.sh first" >&2
    exit 2
fi

# 部分集合の集計
total=$(wc -l <"$CSV" | tr -d ' ')

# (1) GPU/glue 系 (kernel/gpu_backend, runtime_link)
n_gpu=$(awk -F, 'BEGIN{n=0} /\/(gpu_backend|runtime_link_stubs)\.[ch]/{n++} END{print n}' "$CSV")

# (2) bfree_posix の syscall ブリッジ層
n_posix_bridge=$(awk -F, 'BEGIN{n=0} /\/bfree_posix\//{n++} END{print n}' "$CSV")

# (3) musl_libc_*.c (libc 関数の中で ENOSYS している)
n_musl=$(awk -F, 'BEGIN{n=0} /musl_libc_[A-Za-z0-9_]+\.c/{n++} END{print n}' "$CSV")

# (4) カーネル syscall ディスパッチ
n_kernel_syscall=$(awk -F, 'BEGIN{n=0} /\/kernel\/sysmain\//{n++} END{print n}' "$CSV")

# (5) その他（API_NAME, demo_stubs 等のサンプル）
n_other=$(awk -F, 'BEGIN{n=0} /(API_NAME|bfree_demo_stubs)\.c/{n++} END{print n}' "$CSV")

# (2) bfree_posix のシンボル一覧
bridge_syms=$(awk -F, '/\/bfree_posix\//{print $3}' "$CSV" | sort -u)
n_bridge_uniq=$(echo "$bridge_syms" | grep -c . || true)

# (3) musl_libc_*.c のシンボル一覧
musl_syms=$(awk -F, '/musl_libc_[A-Za-z0-9_]+\.c/{print $3}' "$CSV" | sort -u)
n_musl_uniq=$(echo "$musl_syms" | grep -c . || true)

{
    echo "# ENOSYS 分類サマリ"
    echo ""
    echo "- 生成: $(date -u +"%Y-%m-%d %H:%M:%S UTC")"
    echo "- 出現箇所合計: **$total**"
    echo ""
    echo "## カテゴリ別件数"
    echo ""
    echo "| カテゴリ | 件数 | 意味 |"
    echo "|---------|-----:|------|"
    echo "| (1) GPU / runtime_link 系 | $n_gpu | GPU 抽象層の TK2GPU_ENOSYS。POSIX 互換性とは独立 |"
    echo "| (2) bfree_posix bridge    | $n_posix_bridge | syscall ラッパ層。**ここを潰すと「OSS 動作」が増える** |"
    echo "| (3) musl_libc_*.c stub    | $n_musl | libc 関数の中で ENOSYS している。完成度の本丸 |"
    echo "| (4) kernel/sysmain        | $n_kernel_syscall | カーネル syscall ディスパッチの ENOSYS フォールバック |"
    echo "| (5) サンプル/雛形         | $n_other | API_NAME.c / bfree_demo_stubs.c 等。実害なし |"
    echo ""
    echo "## (2) bfree_posix bridge で ENOSYS している関数 (${n_bridge_uniq} 件)"
    echo ""
    echo "**優先順位 高**: ここが ENOSYS だと OSS が「機能がない」と判断して諦める。"
    echo ""
    echo '```'
    echo "$bridge_syms"
    echo '```'
    echo ""
    echo "## (3) musl_libc_*.c で ENOSYS している関数 (${n_musl_uniq} 件)"
    echo ""
    echo "**優先順位 中**: ライブラリ層のスタブ。OSS によっては (2) より早くここで諦める。"
    echo ""
    echo '```'
    echo "$musl_syms"
    echo '```'
} > "$OUT"

echo "[enosys_cat] (1) GPU/glue           : $n_gpu"
echo "[enosys_cat] (2) bfree_posix bridge : $n_posix_bridge  (unique syms: $n_bridge_uniq)"
echo "[enosys_cat] (3) musl_libc_*.c stub : $n_musl       (unique syms: $n_musl_uniq)"
echo "[enosys_cat] (4) kernel/sysmain     : $n_kernel_syscall"
echo "[enosys_cat] (5) sample/demo        : $n_other"
echo "[enosys_cat] report : $OUT"
