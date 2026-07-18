#!/usr/bin/env python3
import csv
import math
from pathlib import Path


def main() -> None:
    root = Path(__file__).resolve().parent.parent
    src = root / "POSIX_685_ABCDE.csv"
    dst = root / "POSIX_685_WORKLIST_20x35.md"
    batch_size = 20

    with src.open(encoding="utf-8", newline="") as f:
        rows = list(csv.reader(f))

    total = len(rows)
    batches = math.ceil(total / batch_size)

    lines = [
        "# POSIX 685 Worklist (20x35)",
        "",
        f"- total_symbols: {total}",
        f"- batch_size: {batch_size}",
        f"- planned_batches: {batches} (34 full + 1 partial)",
        "",
    ]

    for i in range(batches):
        start = i * batch_size
        end = min((i + 1) * batch_size, total)
        lines.append(f"## Batch {i + 1:02d} ({start + 1}-{end}, {end - start} items)")
        lines.append("")
        lines.append("| # | symbol | category | kernel_or_libc |")
        lines.append("|---|--------|----------|----------------|")
        for idx, row in enumerate(rows[start:end], start=start + 1):
            symbol = row[0] if len(row) > 0 else ""
            category = row[1] if len(row) > 1 else ""
            layer = row[2] if len(row) > 2 else ""
            lines.append(f"| {idx} | `{symbol}` | `{category}` | `{layer}` |")
        lines.append("")

    dst.write_text("\n".join(lines), encoding="utf-8")
    print(f"wrote: {dst}")


if __name__ == "__main__":
    main()
