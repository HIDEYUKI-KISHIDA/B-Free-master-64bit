#!/usr/bin/env python3
import argparse
from pathlib import Path


def main() -> int:
    parser = argparse.ArgumentParser(description="Embed a binary file as a C array.")
    parser.add_argument("input", help="Input binary path")
    parser.add_argument("output", help="Output C file path")
    parser.add_argument("symbol", help="Base symbol name")
    args = parser.parse_args()

    input_path = Path(args.input)
    output_path = Path(args.output)
    data = input_path.read_bytes()

    lines = []
    for index in range(0, len(data), 12):
        chunk = data[index:index + 12]
        lines.append("    " + ", ".join(f"0x{byte:02x}" for byte in chunk))

    output_path.parent.mkdir(parents=True, exist_ok=True)
    with output_path.open("w", encoding="ascii", newline="\n") as handle:
        handle.write(f"const unsigned char {args.symbol}[] = {{\n")
        if lines:
            handle.write(",\n".join(lines))
            handle.write("\n")
        handle.write("};\n")
        handle.write(f"const unsigned int {args.symbol}_size = sizeof({args.symbol});\n")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())