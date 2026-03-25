#!/usr/bin/env python3
"""
pack_papp.py — Pack a flat binary into .papp format for PSRAM app loading.

Usage:
    python pack_papp.py input.bin output.papp [--entry-offset N]

The input binary must be a flat binary produced by:
    riscv32-esp-elf-objcopy -O binary app.elf app.bin

Output format:
    [32-byte papp header][.text+.rodata binary]
"""

import argparse
import struct
import sys
import os

PAPP_MAGIC = 0x50415050   # "PAPP" in little-endian
PAPP_VERSION = 1
HEADER_SIZE = 32


def pack(input_bin, output_papp, entry_offset=0, data_size=0, bss_size=0):
    with open(input_bin, "rb") as f:
        code = f.read()

    if len(code) == 0:
        print(f"Error: input binary {input_bin} is empty", file=sys.stderr)
        sys.exit(1)

    text_size = len(code) - data_size  # code portion = total - data
    if text_size <= 0:
        print(f"Error: data_size ({data_size}) >= binary size ({len(code)})",
              file=sys.stderr)
        sys.exit(1)

    header = struct.pack("<IIIIIIII",
                         PAPP_MAGIC,
                         PAPP_VERSION,
                         entry_offset,
                         text_size,
                         data_size,
                         bss_size,
                         0,              # flags (reserved)
                         0)              # reserved

    assert len(header) == HEADER_SIZE

    with open(output_papp, "wb") as f:
        f.write(header)
        f.write(code)

    total = HEADER_SIZE + len(code)
    print(f"Packed: text={text_size} data={data_size} bss={bss_size} "
          f"entry=+{entry_offset}")
    print(f"Output: {output_papp} ({total} bytes)")


def main():
    parser = argparse.ArgumentParser(
        description="Pack a flat binary into .papp PSRAM app format")
    parser.add_argument("input", help="Input flat binary (.bin)")
    parser.add_argument("output", help="Output .papp file")
    parser.add_argument("--entry-offset", type=int, default=0,
                        help="Byte offset to entry function (default: 0)")
    parser.add_argument("--data-size", type=int, default=0,
                        help="Bytes of .data at end of binary (default: 0)")
    parser.add_argument("--bss-size", type=int, default=0,
                        help="Bytes of .bss to allocate (default: 0)")
    args = parser.parse_args()

    pack(args.input, args.output,
         entry_offset=args.entry_offset,
         data_size=args.data_size,
         bss_size=args.bss_size)


if __name__ == "__main__":
    main()
