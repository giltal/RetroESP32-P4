#!/usr/bin/env python3
"""
Generate .ctile and .cusage cache files for Neo Geo sprite ROMs.
Reads C ROM pairs from a game ZIP, interleaves them, applies the
convert_roms_tile algorithm (matching roms.c exactly), and outputs
ready-to-use cache files for the ESP32 emulator.

Usage: python gen_ctile.py <game.zip> [output_dir]
  output_dir defaults to same directory as the ZIP file.
"""

import sys
import os
import struct
import zipfile
import time

# Must match firmware: roms.c line 948
CUSAGE_MAGIC = 0x43553039  # "CU09"

# TILE_INVISIBLE = 1 (from transpack.h enum: TILE_NORMAL=0, TILE_INVISIBLE=1)
TILE_INVISIBLE = 1

# Blazing Star C ROM layout (from driver table):
# Pairs: (even_file, odd_file, dest_base, size_each)
# dest for even ROM has bit0=0, dest for odd ROM has bit0=1
# The dest_base (with bit0 stripped) gives the byte offset in the interleaved output.
CROM_PAIRS = [
    ("239-c1.c1", "239-c2.c2", 0x0000000, 0x400000),
    ("239-c3.c3", "239-c4.c4", 0x0800000, 0x400000),
    ("239-c5.c5", "239-c6.c6", 0x1000000, 0x400000),
    ("239-c7.c7", "239-c8.c8", 0x1800000, 0x400000),
]

TOTAL_SIZE = 0x2000000  # 32 MB


def convert_roms_tile(tile_data):
    """Convert one 128-byte raw tile to decoded 4bpp format.
    Returns (converted_128_bytes, usage_bits).
    Exactly matches convert_roms_tile() in roms.c.
    tile_data: bytes of length 128 (the raw tile)."""

    swap = bytearray(tile_data)  # copy
    out = bytearray(128)
    usage = 0
    out_idx = 0

    for y in range(16):
        # First dword: bits from swap[64..] region
        dw = 0
        for x in range(8):
            pen = ((swap[64 + (y << 2) + 3] >> x) & 1) << 3
            pen |= ((swap[64 + (y << 2) + 1] >> x) & 1) << 2
            pen |= ((swap[64 + (y << 2) + 2] >> x) & 1) << 1
            pen |= (swap[64 + (y << 2)] >> x) & 1
            dw |= pen << ((7 - x) << 2)
            usage |= (1 << pen)
        struct.pack_into('<I', out, out_idx, dw)
        out_idx += 4

        # Second dword: bits from swap[0..] region
        dw = 0
        for x in range(8):
            pen = ((swap[(y << 2) + 3] >> x) & 1) << 3
            pen |= ((swap[(y << 2) + 1] >> x) & 1) << 2
            pen |= ((swap[(y << 2) + 2] >> x) & 1) << 1
            pen |= (swap[(y << 2)] >> x) & 1
            dw |= pen << ((7 - x) << 2)
            usage |= (1 << pen)
        struct.pack_into('<I', out, out_idx, dw)
        out_idx += 4

    return bytes(out), usage


def main():
    if len(sys.argv) < 2:
        print(f"Usage: {sys.argv[0]} <game.zip> [output_dir]")
        sys.exit(1)

    zip_path = sys.argv[1]
    output_dir = sys.argv[2] if len(sys.argv) > 2 else os.path.dirname(zip_path)

    game_name = os.path.splitext(os.path.basename(zip_path))[0]
    ctile_path = os.path.join(output_dir, f"{game_name}.ctile")
    cusage_path = os.path.join(output_dir, f"{game_name}.cusage")

    print(f"Game: {game_name}")
    print(f"ZIP:  {zip_path}")
    print(f"Output: {ctile_path}")

    # Allocate interleaved buffer
    print(f"Allocating {TOTAL_SIZE // (1024*1024)} MB buffer...")
    tiles_buf = bytearray(TOTAL_SIZE)

    t0 = time.time()

    # Read and interleave C ROM pairs
    with zipfile.ZipFile(zip_path, 'r') as zf:
        for even_name, odd_name, dest_base, rom_size in CROM_PAIRS:
            print(f"  Interleaving {even_name} + {odd_name} -> offset 0x{dest_base:07X}")

            even_data = zf.read(even_name)
            odd_data = zf.read(odd_name)
            assert len(even_data) == rom_size, f"{even_name}: expected {rom_size}, got {len(even_data)}"
            assert len(odd_data) == rom_size, f"{odd_name}: expected {rom_size}, got {len(odd_data)}"

            # Interleave: even bytes at dest_base+0, dest_base+2, ...
            #             odd bytes at dest_base+1, dest_base+3, ...
            for i in range(rom_size):
                tiles_buf[dest_base + i * 2] = even_data[i]
                tiles_buf[dest_base + i * 2 + 1] = odd_data[i]

    t1 = time.time()
    print(f"Interleave done in {t1-t0:.1f}s")

    # Convert tiles and build spr_usage
    nb_tiles = TOTAL_SIZE >> 7  # 128 bytes per tile
    # spr_usage: one uint32 per 16 tiles
    nb_usage = nb_tiles >> 4
    spr_usage = [0] * nb_usage

    print(f"Converting {nb_tiles} tiles...")
    converted_buf = bytearray(TOTAL_SIZE)
    tiles_with_data = 0

    for tile in range(nb_tiles):
        offset = tile << 7
        raw = tiles_buf[offset:offset + 128]
        conv, usage = convert_roms_tile(raw)
        converted_buf[offset:offset + 128] = conv

        # spr_usage logic: if tile is invisible, set TILE_INVISIBLE in its 2-bit slot
        if (usage & ~1) == 0:
            spr_usage[tile >> 4] |= (TILE_INVISIBLE << ((tile & 0xF) * 2))
        else:
            tiles_with_data += 1

        if tile > 0 and tile % 100000 == 0:
            print(f"  {tile}/{nb_tiles} ({100*tile//nb_tiles}%)")

    t2 = time.time()
    print(f"Conversion done in {t2-t1:.1f}s  ({tiles_with_data} visible tiles)")

    # Write .ctile
    print(f"Writing {ctile_path} ({TOTAL_SIZE // (1024*1024)} MB)...")
    with open(ctile_path, 'wb') as f:
        f.write(converted_buf)

    # Write .cusage (magic header + spr_usage array)
    usage_bytes = struct.pack(f'<I', CUSAGE_MAGIC)
    for u in spr_usage:
        usage_bytes += struct.pack('<I', u)

    print(f"Writing {cusage_path} ({len(usage_bytes)} bytes)...")
    with open(cusage_path, 'wb') as f:
        f.write(usage_bytes)

    t3 = time.time()
    print(f"Done! Total time: {t3-t0:.1f}s")
    print(f"  {ctile_path}: {os.path.getsize(ctile_path):,} bytes")
    print(f"  {cusage_path}: {os.path.getsize(cusage_path):,} bytes")


if __name__ == '__main__':
    main()
