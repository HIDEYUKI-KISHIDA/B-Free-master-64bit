"""Emit one Wayland core-protocol identifier per line (interface_request / interface_event).

Vendored input: Program/bfree_x86_64/wayland-upstream.xml (same schema as upstream
protocol/wayland.xml; refreshed from Debian sources mirror when needed).
Output matches the plain-text style of posix2017_functions_only.txt.
"""
from __future__ import annotations

import argparse
import sys
import xml.etree.ElementTree as ET


def collect_identifiers(root: ET.Element) -> list[str]:
    ids: list[str] = []
    for iface in root.findall("interface"):
        iname = iface.get("name")
        if not iname:
            continue
        for req in iface.findall("request"):
            rname = req.get("name")
            if rname:
                ids.append(f"{iname}_{rname}")
        for ev in iface.findall("event"):
            ename = ev.get("name")
            if ename:
                ids.append(f"{iname}_{ename}")
    ids.sort()
    return ids


def main() -> int:
    p = argparse.ArgumentParser()
    p.add_argument(
        "-i",
        "--input",
        default="wayland-upstream.xml",
        help="Path to wayland.xml (default: wayland-upstream.xml in cwd)",
    )
    p.add_argument(
        "-o",
        "--output",
        default="wayland_core_requests_events_only.txt",
        help="Output text file path",
    )
    args = p.parse_args()

    tree = ET.parse(args.input)
    root = tree.getroot()
    if root.tag != "protocol":
        print(f"error: root element is {root.tag!r}, expected 'protocol'", file=sys.stderr)
        return 1

    ids = collect_identifiers(root)
    with open(args.output, "w", encoding="utf-8", newline="\n") as f:
        for line in ids:
            f.write(line + "\n")
    print(f"wrote {len(ids)} lines to {args.output}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
